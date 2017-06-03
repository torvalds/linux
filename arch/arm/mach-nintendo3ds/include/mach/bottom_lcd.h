/*
 *  Nintendo 3DS bottom_lcd.h
 *
 *  Copyright (C) 2016 Sergi Granell
 *  Copyright (C) 2017 Paul LaMendola (paulguy)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __NINTENDO3DS_BOTTOM_LCD_H
#define __NINTENDO3DS_BOTTOM_LCD_H

#include <linux/font.h>
#include <asm/io.h>

#define COLOR_RED		0xFF0000
#define COLOR_GREEN		0x00FF00
#define COLOR_BLUE		0x0000FF
#define COLOR_CYAN		0x00FFFF
#define COLOR_PINK		0xFF00FF
#define COLOR_YELLOW		0xFFFF00
#define COLOR_BLACK		0x000000
#define COLOR_GREY		0x808080
#define COLOR_WHITE		0xFFFFFF
#define COLOR_ORANGE		0xFF9900
#define COLOR_LIGHT_GREEN	0x00CC00
#define COLOR_PURPLE		0x660033

void nintendo3ds_bottom_setup_fb(void);
void nintendo3ds_bottom_lcd_map_fb(void);
void nintendo3ds_bottom_lcd_unmap_fb(void);

void nintendo3ds_bottom_lcd_draw_pixel(int x, int y, unsigned int color);
void nintendo3ds_bottom_lcd_draw_fillrect(int x, int y, int w, int h, unsigned int color);
void nintendo3ds_bottom_lcd_clear_screen(unsigned int color);
void nintendo3ds_bottom_lcd_draw_char(const struct font_desc *font, int x, int y, unsigned int color, char c);
int nintendo3ds_bottom_lcd_draw_text(const struct font_desc *font, int x, int y, unsigned int fgcolor, unsigned int bgcolor, const char *text);
void nintendo3ds_bottom_lcd_draw_textf(const struct font_desc *font, int x, int y, unsigned int fgcolor, unsigned int bgcolor, const char *text, ...);

#endif
