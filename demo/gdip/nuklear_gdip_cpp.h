/*
 * Nuklear - 1.40.8 - public domain
 * no warrenty implied; use at your own risk.
 * authored from 2015-2017 by Micha Mettke
 */
/*
 * ==============================================================
 *
 *                              API
 *
 * ===============================================================
 */
#ifndef NK_GDIP_H_
#define NK_GDIP_H_

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <gdiplus.h>

using namespace Gdiplus;

/* font */
typedef struct GdipFont GdipFont;
NK_API GdipFont* nk_gdipfont_create(const char *name, int size);
NK_API GdipFont* nk_gdipfont_create_from_file(const WCHAR* filename, int size);
NK_API GdipFont* nk_gdipfont_create_from_memory(const void* membuf, int membufSize, int size);
NK_API void nk_gdipfont_del(GdipFont *font);

NK_API struct nk_context* nk_gdip_init(HWND hwnd, unsigned int width, unsigned int height);
NK_API void nk_gdip_set_font(GdipFont *font);
NK_API int nk_gdip_handle_event(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
NK_API void nk_gdip_render(enum nk_anti_aliasing AA, struct nk_color clear);
NK_API void nk_gdip_shutdown(void);

/* image */
NK_API struct nk_image nk_gdip_load_image_from_file(const WCHAR* filename);
NK_API struct nk_image nk_gdip_load_image_from_memory(const void* membuf, nk_uint membufSize);
NK_API void nk_gdip_image_free(struct nk_image image);

#endif
/*
 * ==============================================================
 *
 *                          IMPLEMENTATION
 *
 * ===============================================================
 */
#ifdef NK_GDIP_IMPLEMENTATION

#include <stdlib.h>
#include <malloc.h>

struct GdipFont
{
    struct nk_user_font nk;
    Font* handle;
};

static struct gdip_t {
  ULONG_PTR token;

  Graphics* window;
  Graphics* memory;
  Image* bitmap;
  Pen* pen;
  SolidBrush* brush;
  StringFormat* format;
  FontCollection* fontCollection[10];
  INT curFontCollection;

  struct nk_context ctx;
} g_gdip;
static struct gdip_t *gdip = &g_gdip;

static Color convert_color(struct nk_color c)
{
    return Color(c.a, c.r, c.g, c.b);
}

static void
nk_gdip_scissor(float x, float y, float w, float h)
{
    gdip->memory->SetClip(Rect((INT)x, (INT)y, (INT)(w + 1), (INT)(h + 1)), CombineModeReplace);
}

static void
nk_gdip_stroke_line(short x0, short y0, short x1,
    short y1, unsigned int line_thickness, struct nk_color col)
{
    gdip->pen->SetWidth((REAL)line_thickness);
    gdip->pen->SetColor(convert_color(col));
    gdip->memory->DrawLine(gdip->pen, x0, y0, x1, y1);
}

static void
nk_gdip_stroke_rect(short x, short y, unsigned short w,
    unsigned short h, unsigned short r, unsigned short line_thickness, struct nk_color col)
{
    gdip->pen->SetWidth((REAL)line_thickness);
    gdip->pen->SetColor(convert_color(col));
    if (r == 0) {
        gdip->memory->DrawRectangle(gdip->pen, x, y, w, h);
    } else {
        INT d = 2 * r;
        gdip->memory->DrawArc(gdip->pen, x, y, d, d, 180, 90);
        gdip->memory->DrawLine(gdip->pen, x + r, y, x + w - r, y);
        gdip->memory->DrawArc(gdip->pen, x + w - d, y, d, d, 270, 90);
        gdip->memory->DrawLine(gdip->pen, x + w, y + r, x + w, y + h - r);
        gdip->memory->DrawArc(gdip->pen, x + w - d, y + h - d, d, d, 0, 90);
        gdip->memory->DrawLine(gdip->pen, x, y + r, x, y + h - r);
        gdip->memory->DrawArc(gdip->pen, x, y + h - d, d, d, 90, 90);
        gdip->memory->DrawLine(gdip->pen, x + r, y + h, x + w - r, y + h);
    }
}

static void
nk_gdip_fill_rect(short x, short y, unsigned short w,
    unsigned short h, unsigned short r, struct nk_color col)
{
    gdip->brush->SetColor(convert_color(col));
    if (r == 0) {
        gdip->memory->FillRectangle(gdip->brush, x, y, w, h);
    } else {
        INT d = 2 * r;
        gdip->memory->FillRectangle(gdip->brush, x + r, y, w - d, h);
        gdip->memory->FillRectangle(gdip->brush, x, y + r, r, h - d);
        gdip->memory->FillRectangle(gdip->brush, x + w - r, y + r, r, h - d);
        gdip->memory->FillPie(gdip->brush, x, y, d, d, 180, 90);
        gdip->memory->FillPie(gdip->brush, x + w - d, y, d, d, 270, 90);
        gdip->memory->FillPie(gdip->brush, x + w - d, y + h - d, d, d, 0, 90);
        gdip->memory->FillPie(gdip->brush, x, y + h - d, d, d, 90, 90);
    }
}

static void
nk_gdip_fill_triangle(short x0, short y0, short x1,
    short y1, short x2, short y2, struct nk_color col)
{
    Point points[] = {
        { x0, y0 },
        { x1, y1 },
        { x2, y2 },
    };

    gdip->brush->SetColor(convert_color(col));
    gdip->memory->FillPolygon(gdip->brush, points, 3, FillModeAlternate);
}

static void
nk_gdip_stroke_triangle(short x0, short y0, short x1,
    short y1, short x2, short y2, unsigned short line_thickness, struct nk_color col)
{
    Point points[] = {
        { x0, y0 },
        { x1, y1 },
        { x2, y2 },
        { x0, y0 },
    };
    gdip->pen->SetWidth((REAL)line_thickness);
    gdip->pen->SetColor(convert_color(col));
    gdip->memory->DrawPolygon(gdip->pen, points, 4);
}

static void
nk_gdip_fill_polygon(const struct nk_vec2i *pnts, int count, struct nk_color col)
{
    int i = 0;
    #define MAX_PointS 64
    Point points[MAX_PointS];
    gdip->brush->SetColor(convert_color(col));
    for (i = 0; i < count && i < MAX_PointS; ++i) {
        points[i].X = pnts[i].x;
        points[i].Y = pnts[i].y;
    }
    gdip->memory->FillPolygon(gdip->brush, points, i, FillModeAlternate);
    #undef MAX_PointS
}

static void
nk_gdip_stroke_polygon(const struct nk_vec2i *pnts, int count,
    unsigned short line_thickness, struct nk_color col)
{
    gdip->pen->SetWidth((REAL)line_thickness);
    gdip->pen->SetColor(convert_color(col));
    if (count > 0) {
        int i;
        for (i = 1; i < count; ++i)
            gdip->memory->DrawLine(gdip->pen, pnts[i-1].x, pnts[i-1].y, pnts[i].x, pnts[i].y);
        gdip->memory->DrawLine(gdip->pen, pnts[count-1].x, pnts[count-1].y, pnts[0].x, pnts[0].y);
    }
}

static void
nk_gdip_stroke_polyline(const struct nk_vec2i *pnts,
    int count, unsigned short line_thickness, struct nk_color col)
{
    gdip->pen->SetWidth((REAL)line_thickness);
    gdip->pen->SetColor(convert_color(col));
    if (count > 0) {
        int i;
        for (i = 1; i < count; ++i)
            gdip->memory->DrawLine(gdip->pen, pnts[i-1].x, pnts[i-1].y, pnts[i].x, pnts[i].y);
    }
}

static void
nk_gdip_fill_circle(short x, short y, unsigned short w,
    unsigned short h, struct nk_color col)
{
    gdip->brush->SetColor(convert_color(col));
    gdip->memory->FillEllipse(gdip->brush, x, y, w, h);
}

static void
nk_gdip_stroke_circle(short x, short y, unsigned short w,
    unsigned short h, unsigned short line_thickness, struct nk_color col)
{
    gdip->pen->SetWidth((REAL)line_thickness);
    gdip->pen->SetColor(convert_color(col));
    gdip->memory->DrawEllipse(gdip->pen, x, y, w, h);
}

static void
nk_gdip_stroke_curve(struct nk_vec2i p1,
    struct nk_vec2i p2, struct nk_vec2i p3, struct nk_vec2i p4,
    unsigned short line_thickness, struct nk_color col)
{
    gdip->pen->SetWidth((REAL)line_thickness);
    gdip->pen->SetColor(convert_color(col));
    gdip->memory->DrawBezier(gdip->pen, p1.x, p1.y, p2.x, p2.y, p3.x, p3.y, p4.x, p4.y);
}

static void
nk_gdip_draw_text(short x, short y, unsigned short w, unsigned short h,
    const char *text, int len, GdipFont *font, struct nk_color cbg, struct nk_color cfg)
{
    int wsize;
    WCHAR* wstr;
    RectF layout = { (FLOAT)x, (FLOAT)y, (FLOAT)w, (FLOAT)h };

    if(!text || !font || !len) return;

    wsize = MultiByteToWideChar(CP_UTF8, 0, text, len, NULL, 0);
    wstr = (WCHAR*)_alloca(wsize * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, text, len, wstr, wsize);

    gdip->brush->SetColor(convert_color(cfg));
    gdip->memory->DrawString(wstr, wsize, font->handle, layout, gdip->format, gdip->brush);
}

static void
nk_gdip_draw_image(short x, short y, unsigned short w, unsigned short h,
    struct nk_image img, struct nk_color col)
{
    Image *image = (Image*)img.handle.ptr;
    if (img.region[0] || img.region[1] || img.region[2] || img.region[3]) {
      gdip->memory->DrawImage(image, Rect(x, y, w, h), img.region[0], img.region[1], img.region[2], img.region[3], UnitPixel);
    } else {
      gdip->memory->DrawImage(image, x, y, w, h);
    }
}

static void
nk_gdip_clear(struct nk_color col)
{
    gdip->memory->Clear(convert_color(col));
}

static void
nk_gdip_blit(Graphics *graphics)
{
    graphics->DrawImage(gdip->bitmap, 0, 0);
}

static struct nk_image
nk_gdip_image_to_nk(Image *image) {
    struct nk_image img;
    UINT uwidth, uheight;
    img = nk_image_ptr( (void*)image );
    uheight = image->GetHeight();
    uwidth = image->GetWidth();
    img.h = uheight;
    img.w = uwidth;
    return img;
}

struct nk_image
nk_gdip_load_image_from_file(const WCHAR *filename)
{
    Image *image = new Image(filename);
    if (!image) return nk_image_id(0);
    return nk_gdip_image_to_nk(image);
}

struct nk_image
nk_gdip_load_image_from_memory(const void *membuf, nk_uint membufSize)
{
    IStream *stream = SHCreateMemStream((const BYTE*)membuf, membufSize);
    if (!stream)
        return nk_image_id(0);
    
    Image* image = new Image(stream);
    stream->Release();

    if (!image)
        return nk_image_id(0);

    return nk_gdip_image_to_nk(image);
}

void
nk_gdip_image_free(struct nk_image image)
{
    if (!image.handle.ptr)
        return;
    delete (Image*)image.handle.ptr;
}

GdipFont*
nk_gdipfont_create(const char *name, int size)
{
    GdipFont *font = (GdipFont*)calloc(1, sizeof(GdipFont));
    FontFamily *family;

    int wsize = MultiByteToWideChar(CP_UTF8, 0, name, -1, NULL, 0);
    WCHAR* wname = (WCHAR*)_alloca((wsize + 1) * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, wsize);
    wname[wsize] = 0;

    family = new FontFamily(wname, NULL);
    font->handle = new Font(family, (REAL)size, FontStyleRegular, UnitPixel);
    delete family;

    return font;
}

FontCollection* 
nk_gdip_getCurFontCollection(){
    return gdip->fontCollection[gdip->curFontCollection];
}

GdipFont*
nk_gdipfont_create_from_collection(int size){
return NULL;/*
    FontFamily **families;
    INT count;
    GdipFont *font = (GdipFont*)calloc(1, sizeof(GdipFont));
    if( GdipGetFontCollectionFamilyCount(nk_gdip_getCurFontCollection(), &count) ) return NULL;
    families = (FontFamily**)calloc(1, sizeof(FontFamily*));
    if( !families ) return NULL;
    if( GdipGetFontCollectionFamilyList(nk_gdip_getCurFontCollection(), count, families, &count) ) return NULL;
    if( count < 1 ) return NULL;
    if( GdipCreateFont(families[count-1], (REAL)size, FontStyleRegular, UnitPixel, &font->handle) ) return NULL;
    free(families);
    gdip->curFontCollection++;
    return font;*/
}

GdipFont*
nk_gdipfont_create_from_memory(const void* membuf, int membufSize, int size)
{
return NULL;/*
    if( !nk_gdip_getCurFontCollection() )
        if( GdipNewPrivateFontCollection(&gdip->fontCollection[gdip->curFontCollection]) ) return NULL;
    if( GdipPrivateAddMemoryFont(nk_gdip_getCurFontCollection(), membuf, membufSize) ) return NULL;
    return nk_gdipfont_create_from_collection(size);*/
}

GdipFont*
nk_gdipfont_create_from_file(const WCHAR* filename, int size)
{
return NULL;/*
    if( !nk_gdip_getCurFontCollection() )
        if( GdipNewPrivateFontCollection(&gdip->fontCollection[gdip->curFontCollection]) ) return NULL;
    if( GdipPrivateAddFontFile(nk_gdip_getCurFontCollection(), filename) ) return NULL;    
    return nk_gdipfont_create_from_collection(size);*/
}

static float
nk_gdipfont_get_text_width(nk_handle handle, float height, const char *text, int len)
{
    GdipFont *font = (GdipFont *)handle.ptr;
    RectF layout = { 0.0f, 0.0f, 65536.0f, 65536.0f };
    RectF bbox;
    int wsize;
    WCHAR* wstr;
    if (!font || !text)
        return 0;

    (void)height;
    wsize = MultiByteToWideChar(CP_UTF8, 0, text, len, NULL, 0);
    wstr = (WCHAR*)_alloca(wsize * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, text, len, wstr, wsize);

    gdip->memory->MeasureString(wstr, wsize, font->handle, layout, gdip->format, &bbox, NULL, NULL);
    return bbox.Width;
}

void
nk_gdipfont_del(GdipFont *font)
{
    if(!font) return;
    delete font->handle;
    free(font);
}

static void
nk_gdip_clipboard_paste(nk_handle usr, struct nk_text_edit *edit)
{
    HGLOBAL mem;
    SIZE_T size;
    LPCWSTR wstr;
    int utf8size;
    char* utf8;
    (void)usr;

    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) && OpenClipboard(NULL))
        return;

    mem = (HGLOBAL)GetClipboardData(CF_UNICODETEXT);
    if (!mem) {
        CloseClipboard();
        return;
    }

    size = GlobalSize(mem) - 1;
    if (!size) {
        CloseClipboard();
        return;
    }

    wstr = (LPCWSTR)GlobalLock(mem);
    if (!wstr) {
        CloseClipboard();
        return;
    }

    utf8size = WideCharToMultiByte(CP_UTF8, 0, wstr, (int)(size / sizeof(wchar_t)), NULL, 0, NULL, NULL);
    if (!utf8size) {
        GlobalUnlock(mem);
        CloseClipboard();
        return;
    }

    utf8 = (char*)malloc(utf8size);
    if (!utf8) {
        GlobalUnlock(mem);
        CloseClipboard();
        return;
    }

    WideCharToMultiByte(CP_UTF8, 0, wstr, (int)(size / sizeof(wchar_t)), utf8, utf8size, NULL, NULL);
    nk_textedit_paste(edit, utf8, utf8size);
    free(utf8);
    GlobalUnlock(mem);
    CloseClipboard();
}

static void
nk_gdip_clipboard_copy(nk_handle usr, const char *text, int len)
{
    HGLOBAL mem;
    wchar_t* wstr;
    int wsize;
    (void)usr;

    if (!OpenClipboard(NULL))
        return;

    wsize = MultiByteToWideChar(CP_UTF8, 0, text, len, NULL, 0);
    if (!wsize) {
        CloseClipboard();
        return;
    }

    mem = (HGLOBAL)GlobalAlloc(GMEM_MOVEABLE, (wsize + 1) * sizeof(wchar_t));
    if (!mem) {
        CloseClipboard();
        return;
    }

    wstr = (wchar_t*)GlobalLock(mem);
    if (!wstr) {
        GlobalFree(mem);
        CloseClipboard();
        return;
    }

    MultiByteToWideChar(CP_UTF8, 0, text, len, wstr, wsize);
    wstr[wsize] = 0;
    GlobalUnlock(mem);
    if (!SetClipboardData(CF_UNICODETEXT, mem))
        GlobalFree(mem);
    CloseClipboard();
}

NK_API struct nk_context*
nk_gdip_init(HWND hwnd, unsigned int width, unsigned int height)
{
    int i;
    GdiplusStartupInput startup;
    GdiplusStartup(&gdip->token, &startup, NULL);

    gdip->window = new Graphics(hwnd);
    gdip->bitmap = new Bitmap(width, height, gdip->window);
    gdip->memory = new Graphics(gdip->bitmap);
    gdip->pen = new Pen(Color((ARGB)0), 1.0f);
    gdip->brush = new SolidBrush(Color((ARGB)0));
    gdip->format = new StringFormat(StringFormat::GenericTypographic());
    gdip->format->SetFormatFlags(StringFormatFlagsNoFitBlackBox | 
        StringFormatFlagsMeasureTrailingSpaces | StringFormatFlagsNoWrap |
        StringFormatFlagsNoClip);

    for(i=0; i< sizeof(gdip->fontCollection)/sizeof(gdip->fontCollection[0]); i++)
        gdip->fontCollection[i] = NULL;
    nk_init_default(&gdip->ctx, NULL);
    gdip->ctx.clip.copy = nk_gdip_clipboard_copy;
    gdip->ctx.clip.paste = nk_gdip_clipboard_paste;
    gdip->curFontCollection = 0;
    return &gdip->ctx;
}

NK_API void
nk_gdip_set_font(GdipFont *gdipfont)
{
    struct nk_user_font *font = &gdipfont->nk;
    font->userdata = nk_handle_ptr(gdipfont);
    font->height = gdipfont->handle->GetSize();
    font->width = nk_gdipfont_get_text_width;
    nk_style_set_font(&gdip->ctx, font);
}

NK_API int
nk_gdip_handle_event(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_SIZE:
        if (gdip->window)
        {
            unsigned int width = LOWORD(lparam);
            unsigned int height = HIWORD(lparam);
            delete gdip->window;
            delete gdip->memory;
            delete gdip->bitmap;
            gdip->window = new Graphics(wnd);
            gdip->bitmap = new Bitmap(width, height, gdip->window);
            gdip->memory = new Graphics(gdip->bitmap);
        }
        break;

    case WM_PAINT:
    {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(wnd, &paint);
        Graphics *graphics = new Graphics(dc);
        nk_gdip_blit(graphics);
        delete graphics;
        EndPaint(wnd, &paint);
        return 1;
    }

    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    {
        int down = !((lparam >> 31) & 1);
        int ctrl = GetKeyState(VK_CONTROL) & (1 << 15);

        switch (wparam)
        {
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
            nk_input_key(&gdip->ctx, NK_KEY_SHIFT, down);
            return 1;

        case VK_DELETE:
            nk_input_key(&gdip->ctx, NK_KEY_DEL, down);
            return 1;

        case VK_RETURN:
            nk_input_key(&gdip->ctx, NK_KEY_ENTER, down);
            return 1;

        case VK_TAB:
            nk_input_key(&gdip->ctx, NK_KEY_TAB, down);
            return 1;

        case VK_LEFT:
            if (ctrl)
                nk_input_key(&gdip->ctx, NK_KEY_TEXT_WORD_LEFT, down);
            else
                nk_input_key(&gdip->ctx, NK_KEY_LEFT, down);
            return 1;

        case VK_RIGHT:
            if (ctrl)
                nk_input_key(&gdip->ctx, NK_KEY_TEXT_WORD_RIGHT, down);
            else
                nk_input_key(&gdip->ctx, NK_KEY_RIGHT, down);
            return 1;

        case VK_BACK:
            nk_input_key(&gdip->ctx, NK_KEY_BACKSPACE, down);
            return 1;

        case VK_HOME:
            nk_input_key(&gdip->ctx, NK_KEY_TEXT_START, down);
            nk_input_key(&gdip->ctx, NK_KEY_SCROLL_START, down);
            return 1;

        case VK_END:
            nk_input_key(&gdip->ctx, NK_KEY_TEXT_END, down);
            nk_input_key(&gdip->ctx, NK_KEY_SCROLL_END, down);
            return 1;

        case VK_NEXT:
            nk_input_key(&gdip->ctx, NK_KEY_SCROLL_DOWN, down);
            return 1;

        case VK_PRIOR:
            nk_input_key(&gdip->ctx, NK_KEY_SCROLL_UP, down);
            return 1;

        case 'C':
            if (ctrl) {
                nk_input_key(&gdip->ctx, NK_KEY_COPY, down);
                return 1;
            }
            break;

        case 'V':
            if (ctrl) {
                nk_input_key(&gdip->ctx, NK_KEY_PASTE, down);
                return 1;
            }
            break;

        case 'X':
            if (ctrl) {
                nk_input_key(&gdip->ctx, NK_KEY_CUT, down);
                return 1;
            }
            break;

        case 'Z':
            if (ctrl) {
                nk_input_key(&gdip->ctx, NK_KEY_TEXT_UNDO, down);
                return 1;
            }
            break;

        case 'R':
            if (ctrl) {
                nk_input_key(&gdip->ctx, NK_KEY_TEXT_REDO, down);
                return 1;
            }
            break;
        }
        return 0;
    }

    case WM_CHAR:
        if (wparam >= 32)
        {
            nk_input_unicode(&gdip->ctx, (nk_rune)wparam);
            return 1;
        }
        break;

    case WM_LBUTTONDOWN:
        nk_input_button(&gdip->ctx, NK_BUTTON_LEFT, (short)LOWORD(lparam), (short)HIWORD(lparam), 1);
        SetCapture(wnd);
        return 1;

    case WM_LBUTTONUP:
        nk_input_button(&gdip->ctx, NK_BUTTON_DOUBLE, (short)LOWORD(lparam), (short)HIWORD(lparam), 0);
        nk_input_button(&gdip->ctx, NK_BUTTON_LEFT, (short)LOWORD(lparam), (short)HIWORD(lparam), 0);
        ReleaseCapture();
        return 1;

    case WM_RBUTTONDOWN:
        nk_input_button(&gdip->ctx, NK_BUTTON_RIGHT, (short)LOWORD(lparam), (short)HIWORD(lparam), 1);
        SetCapture(wnd);
        return 1;

    case WM_RBUTTONUP:
        nk_input_button(&gdip->ctx, NK_BUTTON_RIGHT, (short)LOWORD(lparam), (short)HIWORD(lparam), 0);
        ReleaseCapture();
        return 1;

    case WM_MBUTTONDOWN:
        nk_input_button(&gdip->ctx, NK_BUTTON_MIDDLE, (short)LOWORD(lparam), (short)HIWORD(lparam), 1);
        SetCapture(wnd);
        return 1;

    case WM_MBUTTONUP:
        nk_input_button(&gdip->ctx, NK_BUTTON_MIDDLE, (short)LOWORD(lparam), (short)HIWORD(lparam), 0);
        ReleaseCapture();
        return 1;

    case WM_MOUSEWHEEL:
        nk_input_scroll(&gdip->ctx, nk_vec2(0,(float)(short)HIWORD(wparam) / WHEEL_DELTA));
        return 1;

    case WM_MOUSEMOVE:
        nk_input_motion(&gdip->ctx, (short)LOWORD(lparam), (short)HIWORD(lparam));
        return 1;

    case WM_LBUTTONDBLCLK:
        nk_input_button(&gdip->ctx, NK_BUTTON_DOUBLE, (short)LOWORD(lparam), (short)HIWORD(lparam), 1);
        return 1;
    }

    return 0;
}

NK_API void
nk_gdip_shutdown(void)
{
    int i;
    for(i=0; i< gdip->curFontCollection; i++)
        delete gdip->fontCollection[i];
    delete gdip->window;
    delete gdip->memory;
    delete gdip->bitmap;
    delete gdip->pen;
    delete gdip->brush;
    delete gdip->format;
    GdiplusShutdown(gdip->token);

    nk_free(&gdip->ctx);
    gdip = NULL;
}

NK_API void
nk_gdip_prerender_gui(enum nk_anti_aliasing AA)
{
    const struct nk_command *cmd;

    gdip->memory->SetTextRenderingHint(AA != NK_ANTI_ALIASING_OFF ?
        TextRenderingHintClearTypeGridFit : TextRenderingHintSingleBitPerPixelGridFit);
    gdip->memory->SetSmoothingMode(AA != NK_ANTI_ALIASING_OFF ?
        SmoothingModeHighQuality : SmoothingModeNone);

    nk_foreach(cmd, &gdip->ctx)
    {
        switch (cmd->type) {
        case NK_COMMAND_NOP: break;
        case NK_COMMAND_SCISSOR: {
            const struct nk_command_scissor *s =(const struct nk_command_scissor*)cmd;
            nk_gdip_scissor(s->x, s->y, s->w, s->h);
        } break;
        case NK_COMMAND_LINE: {
            const struct nk_command_line *l = (const struct nk_command_line *)cmd;
            nk_gdip_stroke_line(l->begin.x, l->begin.y, l->end.x,
                l->end.y, l->line_thickness, l->color);
        } break;
        case NK_COMMAND_RECT: {
            const struct nk_command_rect *r = (const struct nk_command_rect *)cmd;
            nk_gdip_stroke_rect(r->x, r->y, r->w, r->h,
                (unsigned short)r->rounding, r->line_thickness, r->color);
        } break;
        case NK_COMMAND_RECT_FILLED: {
            const struct nk_command_rect_filled *r = (const struct nk_command_rect_filled *)cmd;
            nk_gdip_fill_rect(r->x, r->y, r->w, r->h,
                (unsigned short)r->rounding, r->color);
        } break;
        case NK_COMMAND_CIRCLE: {
            const struct nk_command_circle *c = (const struct nk_command_circle *)cmd;
            nk_gdip_stroke_circle(c->x, c->y, c->w, c->h, c->line_thickness, c->color);
        } break;
        case NK_COMMAND_CIRCLE_FILLED: {
            const struct nk_command_circle_filled *c = (const struct nk_command_circle_filled *)cmd;
            nk_gdip_fill_circle(c->x, c->y, c->w, c->h, c->color);
        } break;
        case NK_COMMAND_TRIANGLE: {
            const struct nk_command_triangle*t = (const struct nk_command_triangle*)cmd;
            nk_gdip_stroke_triangle(t->a.x, t->a.y, t->b.x, t->b.y,
                t->c.x, t->c.y, t->line_thickness, t->color);
        } break;
        case NK_COMMAND_TRIANGLE_FILLED: {
            const struct nk_command_triangle_filled *t = (const struct nk_command_triangle_filled *)cmd;
            nk_gdip_fill_triangle(t->a.x, t->a.y, t->b.x, t->b.y,
                t->c.x, t->c.y, t->color);
        } break;
        case NK_COMMAND_POLYGON: {
            const struct nk_command_polygon *p =(const struct nk_command_polygon*)cmd;
            nk_gdip_stroke_polygon(p->points, p->point_count, p->line_thickness,p->color);
        } break;
        case NK_COMMAND_POLYGON_FILLED: {
            const struct nk_command_polygon_filled *p = (const struct nk_command_polygon_filled *)cmd;
            nk_gdip_fill_polygon(p->points, p->point_count, p->color);
        } break;
        case NK_COMMAND_POLYLINE: {
            const struct nk_command_polyline *p = (const struct nk_command_polyline *)cmd;
            nk_gdip_stroke_polyline(p->points, p->point_count, p->line_thickness, p->color);
        } break;
        case NK_COMMAND_TEXT: {
            const struct nk_command_text *t = (const struct nk_command_text*)cmd;
            nk_gdip_draw_text(t->x, t->y, t->w, t->h,
                (const char*)t->string, t->length,
                (GdipFont*)t->font->userdata.ptr,
                t->background, t->foreground);
        } break;
        case NK_COMMAND_CURVE: {
            const struct nk_command_curve *q = (const struct nk_command_curve *)cmd;
            nk_gdip_stroke_curve(q->begin, q->ctrl[0], q->ctrl[1],
                q->end, q->line_thickness, q->color);
        } break;
        case NK_COMMAND_IMAGE: {
            const struct nk_command_image *i = (const struct nk_command_image *)cmd;
            nk_gdip_draw_image(i->x, i->y, i->w, i->h, i->img, i->col);
        } break;
        case NK_COMMAND_RECT_MULTI_COLOR:
        case NK_COMMAND_ARC:
        case NK_COMMAND_ARC_FILLED:
        default: break;
        }
    }
}

NK_API void
nk_gdip_render_gui(enum nk_anti_aliasing AA)
{
    nk_gdip_prerender_gui(AA);
    nk_gdip_blit(gdip->window);
    nk_clear(&gdip->ctx);
}

NK_API void
nk_gdip_render(enum nk_anti_aliasing AA, struct nk_color clear)
{
    nk_gdip_clear(clear);
    nk_gdip_render_gui(AA);
}

#endif
