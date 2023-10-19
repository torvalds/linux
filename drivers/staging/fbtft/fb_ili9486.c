// SPDX-License-Identifier: GPL-2.0+
/*
 * FB driver for the ILI9486 LCD Controller
 *
 * Copyright (C) 2014 Noralf Tronnes
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <video/mipi_display.h>

#include "fbtft.h"

#define DRVNAME		"fb_ili9486"
#define WIDTH		320
#define HEIGHT		480

/* this init sequence matches PiScreen */
static const s16 default_init_sequence[] = {
	/* Interface Mode Control */
	-1, 0xb0, 0x0,
	-1, MIPI_DCS_EXIT_SLEEP_MODE,
	-2, 250,
	/* Interface Pixel Format */
	-1, MIPI_DCS_SET_PIXEL_FORMAT, 0x55,
	/* Power Control 3 */
	-1, 0xC2, 0x44,
	/* VCOM Control 1 */
	-1, 0xC5, 0x00, 0x00, 0x00, 0x00,
	/* PGAMCTRL(Positive Gamma Control) */
	-1, 0xE0, 0x0F, 0x1F, 0x1C, 0x0C, 0x0F, 0x08, 0x48, 0x98,
		  0x37, 0x0A, 0x13, 0x04, 0x11, 0x0D, 0x00,
	/* NGAMCTRL(Negative Gamma Control) */
	-1, 0xE1, 0x0F, 0x32, 0x2E, 0x0B, 0x0D, 0x05, 0x47, 0x75,
		  0x37, 0x06, 0x10, 0x03, 0x24, 0x20, 0x00,
	/* Digital Gamma Control 1 */
	-1, 0xE2, 0x0F, 0x32, 0x2E, 0x0B, 0x0D, 0x05, 0x47, 0x75,
		  0x37, 0x06, 0x10, 0x03, 0x24, 0x20, 0x00,
	-1, MIPI_DCS_EXIT_SLEEP_MODE,
	-1, MIPI_DCS_SET_DISPLAY_ON,
	/* end marker */
	-3
};

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	write_reg(par, MIPI_DCS_SET_COLUMN_ADDRESS,
		  xs >> 8, xs & 0xFF, xe >> 8, xe & 0xFF);

	write_reg(par, MIPI_DCS_SET_PAGE_ADDRESS,
		  ys >> 8, ys & 0xFF, ye >> 8, ye & 0xFF);

	write_reg(par, MIPI_DCS_WRITE_MEMORY_START);
}

static int set_var(struct fbtft_par *par)
{
	switch (par->info->var.rotate) {
	case 0:
		write_reg(par, MIPI_DCS_SET_ADDRESS_MODE,
			  0x80 | (par->bgr << 3));
		break;
	case 90:
		write_reg(par, MIPI_DCS_SET_ADDRESS_MODE,
			  0x20 | (par->bgr << 3));
		break;
	case 180:
		write_reg(par, MIPI_DCS_SET_ADDRESS_MODE,
			  0x40 | (par->bgr << 3));
		break;
	case 270:
		write_reg(par, MIPI_DCS_SET_ADDRESS_MODE,
			  0xE0 | (par->bgr << 3));
		break;
	default:
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

FBTFT_REGISTER_DRIVER(DRVNAME, "ilitek,ili9486", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:ili9486");
MODULE_ALIAS("platform:ili9486");

MODULE_DESCRIPTION("FB driver for the ILI9486 LCD Controller");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
