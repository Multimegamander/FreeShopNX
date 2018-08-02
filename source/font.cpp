#include "common.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#define FONT_FACES_MAX PlSharedFontType_Total

static FT_Error s_font_libret=1, s_font_facesret[FONT_FACES_MAX];

static FT_Library s_font_library;
static FT_Face s_font_faces[FONT_FACES_MAX];
static FT_Face s_font_lastusedface;
static size_t s_font_faces_total = 0;
static u32 g_scale = 0;

static bool FontSetType(u32 font)
{
    u32 i=0;
    FT_Error ret=0;

    g_scale = font;

    for (i=0; i<s_font_faces_total; i++) {
        ret = FT_Set_Char_Size(
                s_font_faces[i],        /* handle to face object           */
                0,                      /* char_width in 1/64th of points  */
                g_scale*64,             /* char_height in 1/64th of points */
                300,                    /* horizontal device resolution    */
                300);                   /* vertical device resolution      */

         if (ret) return false;
    }

    return true;
}

static inline bool FontLoadGlyph(glyph_t* glyph, u32 font, uint32_t codepoint)
{
    FT_Face face;
    FT_Error ret=0;
    FT_GlyphSlot slot;
    FT_UInt glyph_index;
    FT_Bitmap* bitmap;
    u32 i=0;

    if (s_font_faces_total==0) return false;

    for (i=0; i<s_font_faces_total; i++) {
        face = s_font_faces[i];
        s_font_lastusedface = face;
        glyph_index = FT_Get_Char_Index(face, codepoint);
        if (glyph_index==0) continue;

        ret = FT_Load_Glyph(
                face,          /* handle to face object */
                glyph_index,   /* glyph index           */
                FT_LOAD_DEFAULT);

        if (ret==0)
        {
            ret = FT_Render_Glyph( face->glyph,             /* glyph slot  */
                                   FT_RENDER_MODE_NORMAL);  /* render mode */
        }

        if (ret) return false;

        break;
    }

    slot = face->glyph;
    bitmap = &slot->bitmap;

    glyph->width   = bitmap->width;
    glyph->height  = bitmap->rows;
    glyph->pitch   = bitmap->pitch;
    glyph->data    = bitmap->buffer;
    glyph->advance = slot->advance.x >> 6;
    glyph->posX    = slot->bitmap_left;
    glyph->posY    = slot->bitmap_top;
    return true;
}

static void DrawGlyph(uint32_t x, uint32_t y, color_t clr, const glyph_t* glyph)
{
    uint32_t i, j;
    const uint8_t* data = glyph->data;
    x += glyph->posX;
    y -= glyph->posY; //y += glyph->posY;
    for (j = 0; j < glyph->height; j ++)
    {
        for (i = 0; i < glyph->width; i ++)
        {
            clr.a = data[i];
            if (!clr.a) continue;
            DrawPixel(x+i, y+j, clr);
        }
        data+= glyph->pitch;
    }
}

static inline uint8_t DecodeByte(const char** ptr)
{
    uint8_t c = (uint8_t)**ptr;
    *ptr += 1;
    return c;
}

// UTF-8 code adapted from http://www.json.org/JSON_checker/utf8_decode.c

static inline int8_t DecodeUTF8Cont(const char** ptr)
{
    int c = DecodeByte(ptr);
    return ((c & 0xC0) == 0x80) ? (c & 0x3F) : -1;
}

static inline uint32_t DecodeUTF8(const char** ptr)
{
    uint32_t r;
    uint8_t c;
    int8_t c1, c2, c3;

    c = DecodeByte(ptr);
    if ((c & 0x80) == 0)
        return c;
    if ((c & 0xE0) == 0xC0)
    {
        c1 = DecodeUTF8Cont(ptr);
        if (c1 >= 0)
        {
            r = ((c & 0x1F) << 6) | c1;
            if (r >= 0x80)
                return r;
        }
    } else if ((c & 0xF0) == 0xE0)
    {
        c1 = DecodeUTF8Cont(ptr);
        if (c1 >= 0)
        {
            c2 = DecodeUTF8Cont(ptr);
            if (c2 >= 0)
            {
                r = ((c & 0x0F) << 12) | (c1 << 6) | c2;
                if (r >= 0x800 && (r < 0xD800 || r >= 0xE000))
                    return r;
            }
        }
    } else if ((c & 0xF8) == 0xF0)
    {
        c1 = DecodeUTF8Cont(ptr);
        if (c1 >= 0)
        {
            c2 = DecodeUTF8Cont(ptr);
            if (c2 >= 0)
            {
                c3 = DecodeUTF8Cont(ptr);
                if (c3 >= 0)
                {
                    r = ((c & 0x07) << 18) | (c1 << 12) | (c2 << 6) | c3;
                    if (r >= 0x10000 && r < 0x110000)
                        return r;
                }
            }
        }
    }
    return 0xFFFD;
}

static void DrawText_(u32 font, uint32_t x, uint32_t y, color_t clr, const char *text, uint32_t max_width, const char *end_text)
{
    uint32_t origX = x;
    if (s_font_faces_total==0) return;
    if (!FontSetType(font)) return;
    s_font_lastusedface = s_font_faces[0];

    while (*text)
    {
        if (max_width && x-origX >= max_width) {
            text = end_text;
            max_width = 0;
        }

        glyph_t glyph;
        uint32_t codepoint = DecodeUTF8(&text);

        if (codepoint == '\n')
        {
            if (max_width) {
                text = end_text;
                max_width = 0;
                continue;
            }

            x = origX;
            y += s_font_lastusedface->size->metrics.height / 64;
            continue;
        }

        if (!FontLoadGlyph(&glyph, font, codepoint))
        {
            if (!FontLoadGlyph(&glyph, font, '?'))
                continue;
        }

        DrawGlyph(x, y, clr, &glyph);
        x += glyph.advance;
    }
}

struct coord DrawText(u32 font, uint32_t x, uint32_t y, color_t clr, const char *text)
{
    DrawText_(font, x, y, clr, text, 0, NULL);
    uint32_t w, h;
    GetTextDimensions(font, text, &w, &h);
    struct coord pos = {x+w, y+h-(g_scale*2)};
    return pos;
}

struct coord DrawTextTruncate(u32 font, uint32_t x, uint32_t y, color_t clr, const char *text, uint32_t max_width, const char *end_text)
{
    DrawText_(font, x, y, clr, text, max_width, end_text);
    uint32_t w, h;
    GetTextDimensions(font, text, &w, &h);
    struct coord pos = {x + w, y + h - (g_scale * 2)};
    return pos;
}

void GetTextDimensions(u32 font, const char* text, uint32_t* width_out, uint32_t* height_out)
{
    uint32_t x = 0, y = 0;
    uint32_t width = 0, height = 0;
    if (s_font_faces_total==0) return;
    if (!FontSetType(font)) return;
    s_font_lastusedface = s_font_faces[0];

    while (*text)
    {
        glyph_t glyph;
        uint32_t codepoint = DecodeUTF8(&text);

        y = s_font_lastusedface->size->metrics.height / 64;
        if (codepoint == '\n')
        {
            x = 0;
            height += s_font_lastusedface->size->metrics.height / 64;
            continue;
        }

        if (!FontLoadGlyph(&glyph, font, codepoint))
        {
            if (!FontLoadGlyph(&glyph, font, '?'))
                continue;
        }

        x += glyph.advance;

        if (x > width)
            width = x;
        if (y > height)
            height = y;
    }
    if (width_out)
        *width_out = width;
    if (height_out)
        *height_out = height;
}

bool fontInitialize(void)
{
    FT_Error ret=0;
    u64 LanguageCode = 0;
    u32 i;
    Result rc = 0;

    if (R_SUCCEEDED(rc)) rc = setGetSystemLanguage(&LanguageCode);

    for (i=0; i<FONT_FACES_MAX; i++) s_font_facesret[i] = 1;

    ret = FT_Init_FreeType(&s_font_library);
    s_font_libret = ret;
    if (s_font_libret) return false;

    PlFontData fonts[PlSharedFontType_Total];

    rc = plGetSharedFont(LanguageCode, fonts, FONT_FACES_MAX, &s_font_faces_total);
    if (R_FAILED(rc)) return false;

    for (i=0; i<s_font_faces_total; i++) {
        ret = FT_New_Memory_Face(s_font_library,
                                 (const FT_Byte *) fonts[i].address, /* first byte in memory */
                                 fonts[i].size,                      /* size in bytes        */
                                 0,                                  /* face_index           */
                                 &s_font_faces[i]);

        s_font_facesret[i] = ret;
        if (ret) return false;
    }

    return true;
}

void fontExit()
{
    u32 i=0;

    for (i=0; i<s_font_faces_total; i++)
        if (s_font_facesret[i]==0) FT_Done_Face(s_font_faces[i]);

    if (s_font_libret==0) FT_Done_FreeType(s_font_library);
}