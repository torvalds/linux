/*
 * FB driver for the ILI9481 LCD Controller
 *
 * Copyright (c) 2014 Petr Olivka
 * Copyright (c) 2013 Noralf Tronnes
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
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <video/mipi_display.h>

#include "fbtft.h"

#define DRVNAME	"fb_ili9481"
#define WIDTH		320
#define HEIGHT		480

static const s16 default_init_sequence[] = {
	/* SLP_OUT - Sleep out */
	-1, MIPI_DCS_EXIT_SLEEP_MODE,
	-2, 50,
	/* Power setting */
	-1, 0xD0, 0x07, 0x42, 0x18,
	/* VCOM */
	-1, 0xD1, 0x00, 0x07, 0x10,
	/* Power setting for norm. mode */
	-1, 0xD2, 0x01, 0x02,
	/* Panel driving setting */
	-1, 0xC0, 0x10, 0x3B, 0x00, 0x02, 0x11,
	/* Frame rate & inv. */
	-1, 0xC5, 0x03,
	/* Pixel format */
	-1, MIPI_DCS_SET_PIXEL_FORMAT, 0x55,
	/* Gamma */
	-1, 0xC8, 0x00, 0x32, 0x36, 0x45, 0x06, 0x16,
		  0x37, 0x75, 0x77, 0x54, 0x0C, 0x00,
	/* DISP_ON */
	-1, MIPI_DCS_SET_DISPLAY_ON,
	-3
};

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	write_reg(par, MIPI_DCS_SET_COLUMN_ADDRESS,
		  xs >> 8, xs & 0xff, xe >> 8, xe & 0xff);

	write_reg(par, MIPI_DCS_SET_PAGE_ADDRESS,
		  ys >> 8, ys & 0xff, ye >> 8, ye & 0xff);

	write_reg(par, MIPI_DCS_WRITE_MEMORY_START);
}

#define HFLIP 0x01
#define VFLIP 0x02
#define ROW_X_COL 0x20
static int set_var(struct fbtft_par *par)
{
	switch (par->info->var.rotate) {
	case 270:
		write_reg(par, MIPI_DCS_SET_ADDRESS_MODE,
			  ROW_X_COL | HFLIP | VFLIP | (par->bgr << 3));
		break;
	case 180:
		write_reg(par, MIPI_DCS_SET_ADDRESS_MODE,
			  VFLIP | (par->bgr << 3));
		break;
	case 90:
		write_reg(par, MIPI_DCS_SET_ADDRESS_MODE,
			  ROW_X_COL | (par->bgr << 3));
		break;
	default:
		write_reg(par, MIPI_DCS_SET_ADDRESS_MODE,
			  HFLIP | (par->bgr << 3));
		break;
	}

	return 0;
}

static struct fbtft_display display = {
	.regwidth = 8,
	.width = WIDTH,
	.height = HEIGHT,
	.init_sequence = default_init_sequence,
	.fbtftops = {
		.set_addr_win = set_addr_win,
		.set_var = set_var,
	},
};

FBTFT_REGISTER_DRIVER(DRVNAME, "ilitek,ili9481", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:ili9481");
MODULE_ALIAS("platform:ili9481");

MODULE_DESCRIPTION("FB driver for the ILI9481 LCD Controller");
MODULE_AUTHOR("Petr Olivka");
MODULE_LICENSE("GPL");
