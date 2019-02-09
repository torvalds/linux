// SPDX-License-Identifier: GPL-2.0+
/*
 * Custom FB driver for tinylcd.com display
 *
 * Copyright (C) 2013 Noralf Tronnes
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <video/mipi_display.h>

#include "fbtft.h"

#define DRVNAME		"fb_tinylcd"
#define WIDTH		320
#define HEIGHT		480

static int init_display(struct fbtft_par *par)
{
	par->fbtftops.reset(par);

	write_reg(par, 0xB0, 0x80);
	write_reg(par, 0xC0, 0x0A, 0x0A);
	write_reg(par, 0xC1, 0x45, 0x07);
	write_reg(par, 0xC2, 0x33);
	write_reg(par, 0xC5, 0x00, 0x42, 0x80);
	write_reg(par, 0xB1, 0xD0, 0x11);
	write_reg(par, 0xB4, 0x02);
	write_reg(par, 0xB6, 0x00, 0x22, 0x3B);
	write_reg(par, 0xB7, 0x07);
	write_reg(par, MIPI_DCS_SET_ADDRESS_MODE, 0x58);
	write_reg(par, 0xF0, 0x36, 0xA5, 0xD3);
	write_reg(par, 0xE5, 0x80);
	write_reg(par, 0xE5, 0x01);
	write_reg(par, 0xB3, 0x00);
	write_reg(par, 0xE5, 0x00);
	write_reg(par, 0xF0, 0x36, 0xA5, 0x53);
	write_reg(par, 0xE0, 0x00, 0x35, 0x33, 0x00, 0x00, 0x00,
			     0x00, 0x35, 0x33, 0x00, 0x00, 0x00);
	write_reg(par, MIPI_DCS_SET_PIXEL_FORMAT, 0x55);
	write_reg(par, MIPI_DCS_EXIT_SLEEP_MODE);
	udelay(250);
	write_reg(par, MIPI_DCS_SET_DISPLAY_ON);

	return 0;
}

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
	case 270:
		write_reg(par, 0xB6, 0x00, 0x02, 0x3B);
		write_reg(par, MIPI_DCS_SET_ADDRESS_MODE, 0x28);
		break;
	case 180:
		write_reg(par, 0xB6, 0x00, 0x22, 0x3B);
		write_reg(par, MIPI_DCS_SET_ADDRESS_MODE, 0x58);
		break;
	case 90:
		write_reg(par, 0xB6, 0x00, 0x22, 0x3B);
		write_reg(par, MIPI_DCS_SET_ADDRESS_MODE, 0x38);
		break;
	default:
		write_reg(par, 0xB6, 0x00, 0x22, 0x3B);
		write_reg(par, MIPI_DCS_SET_ADDRESS_MODE, 0x08);
		break;
	}

	return 0;
}

static struct fbtft_display display = {
	.regwidth = 8,
	.width = WIDTH,
	.height = HEIGHT,
	.fbtftops = {
		.init_display = init_display,
		.set_addr_win = set_addr_win,
		.set_var = set_var,
	},
};

FBTFT_REGISTER_DRIVER(DRVNAME, "neosec,tinylcd", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("spi:tinylcd");

MODULE_DESCRIPTION("Custom FB driver for tinylcd.com display");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
