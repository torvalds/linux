/*
 *  Nintendo 3DS bottom_lcd.c
 *
 *  Copyright (C) 2016 Sergi Granell
 *  Copyright (C) 2017 Paul LaMendola (paulguy)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/ioport.h>
#include <mach/platform.h>
#include <mach/bottom_lcd.h>

static u8 __iomem *bottom_lcd_fb = NULL;

void nintendo3ds_bottom_setup_fb(void)
{
	u8 __iomem *lcd_fb_pdc1_base;

	if (request_mem_region(NINTENDO3DS_GPU_REG_LCD_FB_PDC1, NINTENDO3DS_GPU_REG_LCD_FB_PDC_SIZE, "N3DS_LCD_FB_PDC1")) {
		lcd_fb_pdc1_base = ioremap_nocache(NINTENDO3DS_GPU_REG_LCD_FB_PDC1, NINTENDO3DS_GPU_REG_LCD_FB_PDC_SIZE);

		printk("LCD_FB_PDC1 mapped to: %p - %p\n", lcd_fb_pdc1_base,
			lcd_fb_pdc1_base +
			NINTENDO3DS_GPU_REG_LCD_FB_PDC_SIZE);
	} else {
		printk("LCD_FB_PDC1 region not available.\n");
		return;
	}

	iowrite32((NINTENDO3DS_LCD_BOT_HEIGHT << 16)
		| NINTENDO3DS_LCD_TOP_WIDTH, lcd_fb_pdc1_base + 0x5C);
	iowrite32(NINTENDO3DS_FB_BOT_1, lcd_fb_pdc1_base + 0x68);
	iowrite32(NINTENDO3DS_FB_BOT_2, lcd_fb_pdc1_base + 0x6C);
	iowrite32(0b000001, lcd_fb_pdc1_base + 0x70);
	iowrite32(0, lcd_fb_pdc1_base + 0x78);
	iowrite32(NINTENDO3DS_LCD_BOT_HEIGHT * 3, lcd_fb_pdc1_base + 0x90);

	iounmap(lcd_fb_pdc1_base);
	release_mem_region(NINTENDO3DS_GPU_REG_LCD_FB_PDC1,
		NINTENDO3DS_GPU_REG_LCD_FB_PDC_SIZE);
}

void nintendo3ds_bottom_lcd_map_fb(void)
{
	/* Map bottom screen framebuffer (VRAM) */
	if (request_mem_region(NINTENDO3DS_FB_BOT_1, NINTENDO3DS_FB_BOT_SIZE, "N3DS_BOT_FB")) {
		bottom_lcd_fb = ioremap_nocache(NINTENDO3DS_FB_BOT_1, NINTENDO3DS_FB_BOT_SIZE);

		printk("Nintendo 3DS: Bottom LCD FB mapped to: %p - %p\n",
			bottom_lcd_fb, bottom_lcd_fb + NINTENDO3DS_FB_BOT_SIZE);
	} else {
		printk("Nintendo 3DS: Bottom LCD FB region not available.\n");
	}
}

void nintendo3ds_bottom_lcd_unmap_fb(void)
{
	if (bottom_lcd_fb) {
		iounmap(bottom_lcd_fb);
		release_mem_region(NINTENDO3DS_FB_BOT_1, NINTENDO3DS_FB_BOT_SIZE);
	}
}

void nintendo3ds_bottom_lcd_draw_pixel(int x, int y, unsigned int color)
{
	u8 __iomem *dst;

	if (x < 0 || y < 0)
		return;

	dst = bottom_lcd_fb + ((NINTENDO3DS_LCD_BOT_HEIGHT - y - 1) +
		x * NINTENDO3DS_LCD_BOT_HEIGHT) * 3;
	iowrite8((color >> 0 ) & 0xFF, dst + 0);
	iowrite8((color >> 8 ) & 0xFF, dst + 1);
	iowrite8((color >> 16) & 0xFF, dst + 2);
}

void nintendo3ds_bottom_lcd_draw_fillrect(int x, int y, int w, int h, unsigned int color)
{
	int i, j;
	for (i = 0; i < h; i++)
		for (j = 0; j < w; j++)
			nintendo3ds_bottom_lcd_draw_pixel(x + j, y + i, color);
}

void nintendo3ds_bottom_lcd_clear_screen(unsigned int color)
{
	nintendo3ds_bottom_lcd_draw_fillrect(0, 0, NINTENDO3DS_LCD_BOT_WIDTH, NINTENDO3DS_LCD_BOT_HEIGHT, color);
}

void nintendo3ds_bottom_lcd_draw_char(const struct font_desc *font, int x, int y, unsigned int color, char c)
{
	int i, j;
	const u8 *src;

	src = font->data + c * font->height;

	for (i = 0; i < 8; i++) {
		for (j = 0; j < 8; j++) {
			if ((*src & (128 >> j)))
				nintendo3ds_bottom_lcd_draw_pixel(x+j, y+i, color);
		}
		src++;
	}
}

int nintendo3ds_bottom_lcd_draw_text(const struct font_desc *font, int x, int y, unsigned int fgcolor, unsigned int bgcolor, const char *text)
{
	char c;
	int sx = x;

	if (!text)
		return 0;

	while ((c = *text++)) {
		if (c == '\n') {
			x = sx;
			y += font->height;
		} else if (c == ' ') {
			x += font->width;
		} else if(c == '\t') {
			x += 4 * font->width;
		} else {
			nintendo3ds_bottom_lcd_draw_fillrect(x, y, font->width, font->height, bgcolor);
			nintendo3ds_bottom_lcd_draw_char(font, x, y, fgcolor, c);
			x += font->width;
		}
	}

	return x - sx;
}

void nintendo3ds_bottom_lcd_draw_textf(const struct font_desc *font, int x, int y, unsigned int fgcolor, unsigned int bgcolor, const char *text, ...)
{
	char buffer[256];
	va_list args;
	va_start(args, text);
	vsnprintf(buffer, 256, text, args);
	nintendo3ds_bottom_lcd_draw_text(font, x, y, bgcolor, fgcolor, buffer);
	va_end(args);
}
