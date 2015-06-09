/*
 * FB driver for the UC1701 LCD Controller
 *
 * The display is monochrome and the video memory is RGB565.
 * Any pixel value except 0 turns the pixel on.
 *
 * Copyright (C) 2014 Juergen Holzmann
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>

#include "fbtft.h"

#define DRVNAME	"fb_uc1701"
#define WIDTH	  102
#define HEIGHT	 64
#define PAGES	  (HEIGHT/8)

/* 1: Display on/off */
#define LCD_DISPLAY_ENABLE    0xAE
/* 2: display start line set */
#define LCD_START_LINE	0x40
/* 3: Page address set (lower 4 bits select one of the pages) */
#define LCD_PAGE_ADDRESS      0xB0
/* 4: column address */
#define LCD_COL_ADDRESS       0x10
/* 8: select orientation */
#define LCD_BOTTOMVIEW	0xA0
/* 9: inverted display */
#define LCD_DISPLAY_INVERT    0xA6
/* 10: show memory content or switch all pixels on */
#define LCD_ALL_PIXEL	 0xA4
/* 11: lcd bias set */
#define LCD_BIAS	      0xA2
/* 14: Reset Controller */
#define LCD_RESET_CMD	 0xE2
/* 15: output mode select (turns display upside-down) */
#define LCD_SCAN_DIR	  0xC0
/* 16: power control set */
#define LCD_POWER_CONTROL     0x28
/* 17: voltage regulator resistor ratio set */
#define LCD_VOLTAGE	   0x20
/* 18: Volume mode set */
#define LCD_VOLUME_MODE       0x81
/* 22: NOP command */
#define LCD_NO_OP	     0xE3
/* 25: advanced program control */
#define LCD_ADV_PROG_CTRL     0xFA
/* 25: advanced program control2 */
#define LCD_ADV_PROG_CTRL2    0x10
#define LCD_TEMPCOMP_HIGH     0x80
/* column offset for normal orientation */
#define SHIFT_ADDR_NORMAL     0
/* column offset for bottom view orientation */
#define SHIFT_ADDR_TOPVIEW    30


static int init_display(struct fbtft_par *par)
{
	fbtft_par_dbg(DEBUG_INIT_DISPLAY, par, "%s()\n", __func__);

	par->fbtftops.reset(par);

	/* softreset of LCD */
	write_reg(par, LCD_RESET_CMD);
	mdelay(10);

	/* set startpoint */
	/* LCD_START_LINE | (pos & 0x3F) */
	write_reg(par, LCD_START_LINE);

	/* select orientation BOTTOMVIEW */
	write_reg(par, LCD_BOTTOMVIEW | 1);
	/* output mode select (turns display upside-down) */
	write_reg(par, LCD_SCAN_DIR | 0x00);

	/* Normal Pixel mode */
	write_reg(par, LCD_ALL_PIXEL | 0);

	/* positive display */
	write_reg(par, LCD_DISPLAY_INVERT | 0);

	/* bias 1/9 */
	write_reg(par, LCD_BIAS | 0);

	/* power control mode: all features on */
	/* LCD_POWER_CONTROL | (val&0x07) */
	write_reg(par, LCD_POWER_CONTROL | 0x07);

	/* set voltage regulator R/R */
	/* LCD_VOLTAGE | (val&0x07) */
	write_reg(par, LCD_VOLTAGE | 0x07);

	/* volume mode set */
	/* LCD_VOLUME_MODE,val&0x3f,LCD_NO_OP */
	write_reg(par, LCD_VOLUME_MODE);
	/* LCD_VOLUME_MODE,val&0x3f,LCD_NO_OP */
	write_reg(par, 0x09);
	/* ???? */
	/* LCD_VOLUME_MODE,val&0x3f,LCD_NO_OP */
	write_reg(par, LCD_NO_OP);

	/* advanced program control */
	write_reg(par, LCD_ADV_PROG_CTRL);
	write_reg(par, LCD_ADV_PROG_CTRL2|LCD_TEMPCOMP_HIGH);

	/* enable display */
	write_reg(par, LCD_DISPLAY_ENABLE | 1);

	return 0;
}

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	fbtft_par_dbg(DEBUG_SET_ADDR_WIN, par, "%s(xs=%d, ys=%d, xe=%d, ye=%d)\n", __func__, xs, ys, xe, ye);

	/* goto address */
	/* LCD_PAGE_ADDRESS | ((page) & 0x1F),
	 (((col)+SHIFT_ADDR_NORMAL) & 0x0F),
	 LCD_COL_ADDRESS | ((((col)+SHIFT_ADDR_NORMAL)>>4) & 0x0F) */
	write_reg(par, LCD_PAGE_ADDRESS);
	/* LCD_PAGE_ADDRESS | ((page) & 0x1F),
	 (((col)+SHIFT_ADDR_NORMAL) & 0x0F),
	  LCD_COL_ADDRESS | ((((col)+SHIFT_ADDR_NORMAL)>>4) & 0x0F) */
	write_reg(par, 0x00);
	/* LCD_PAGE_ADDRESS | ((page) & 0x1F),
	 (((col)+SHIFT_ADDR_NORMAL) & 0x0F),
	  LCD_COL_ADDRESS | ((((col)+SHIFT_ADDR_NORMAL)>>4) & 0x0F) */
	write_reg(par, LCD_COL_ADDRESS);
}

static int write_vmem(struct fbtft_par *par, size_t offset, size_t len)
{
	u16 *vmem16 = (u16 *)par->info->screen_base;
	u8 *buf = par->txbuf.buf;
	int x, y, i;
	int ret = 0;

	fbtft_par_dbg(DEBUG_WRITE_VMEM, par, "%s()\n", __func__);

	for (y = 0; y < PAGES; y++) {
		buf = par->txbuf.buf;
		for (x = 0; x < WIDTH; x++) {
			*buf = 0x00;
			for (i = 0; i < 8; i++)
				*buf |= (vmem16[((y*8*WIDTH)+(i*WIDTH))+x] ? 1 : 0) << i;
			buf++;
		}
		/* LCD_PAGE_ADDRESS | ((page) & 0x1F),
		 (((col)+SHIFT_ADDR_NORMAL) & 0x0F),
		  LCD_COL_ADDRESS | ((((col)+SHIFT_ADDR_NORMAL)>>4) & 0x0F) */
		write_reg(par, LCD_PAGE_ADDRESS|(u8)y);
		/* LCD_PAGE_ADDRESS | ((page) & 0x1F),
		 (((col)+SHIFT_ADDR_NORMAL) & 0x0F),
		  LCD_COL_ADDRESS | ((((col)+SHIFT_ADDR_NORMAL)>>4) & 0x0F) */
		write_reg(par, 0x00);
		/* LCD_PAGE_ADDRESS | ((page) & 0x1F),
		 (((col)+SHIFT_ADDR_NORMAL) & 0x0F),
		  LCD_COL_ADDRESS | ((((col)+SHIFT_ADDR_NORMAL)>>4) & 0x0F) */
		write_reg(par, LCD_COL_ADDRESS);
		gpio_set_value(par->gpio.dc, 1);
		ret = par->fbtftops.write(par, par->txbuf.buf, WIDTH);
		gpio_set_value(par->gpio.dc, 0);
	}

	if (ret < 0)
		dev_err(par->info->device, "write failed and returned: %d\n",
			ret);

	return ret;
}


static struct fbtft_display display = {
	.regwidth = 8,
	.width = WIDTH,
	.height = HEIGHT,
	.fbtftops = {
		.init_display = init_display,
		.set_addr_win = set_addr_win,
		.write_vmem = write_vmem,
	},
	.backlight = 1,
};
FBTFT_REGISTER_DRIVER(DRVNAME, "UltraChip,uc1701", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("spi:uc1701");

MODULE_DESCRIPTION("FB driver for the UC1701 LCD Controller");
MODULE_AUTHOR("Juergen Holzmann");
MODULE_LICENSE("GPL");
