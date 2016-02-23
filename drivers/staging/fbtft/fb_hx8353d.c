/*
 * FB driver for the HX8353D LCD Controller
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

#include "fbtft.h"

#define DRVNAME "fb_hx8353d"
#define DEFAULT_GAMMA "50 77 40 08 BF 00 03 0F 00 01 73 00 72 03 B0 0F 08 00 0F"

static int init_display(struct fbtft_par *par)
{

	par->fbtftops.reset(par);
	mdelay(150);

	/* SETEXTC */
	write_reg(par, 0xB9, 0xFF, 0x83, 0x53);

	/* RADJ */
	write_reg(par, 0xB0, 0x3C, 0x01);

	/* VCOM */
	write_reg(par, 0xB6, 0x94, 0x6C, 0x50);

	/* PWR */
	write_reg(par, 0xB1, 0x00, 0x01, 0x1B, 0x03, 0x01, 0x08, 0x77, 0x89);

	/* COLMOD */
	write_reg(par, 0x3A, 0x05);

	/* MEM ACCESS */
	write_reg(par, 0x36, 0xC0);

	/* SLPOUT - Sleep out & booster on */
	write_reg(par, 0x11);
	mdelay(150);

	/* DISPON - Display On */
	write_reg(par, 0x29);

	/* RGBSET */
	write_reg(par, 0x2D,
		 0,  2,  4,  6,  8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30,
		32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62,
		 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
		16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
		32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
		48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
		 0,  2,  4,  6,  8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30,
		32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62);

	return 0;
};

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	/* column address */
	write_reg(par, 0x2a, xs >> 8, xs & 0xff, xe >> 8, xe & 0xff);

	/* Row address */
	write_reg(par, 0x2b, ys >> 8, ys & 0xff, ye >> 8, ye & 0xff);

	/* memory write */
	write_reg(par, 0x2c);
}

#define my BIT(7)
#define mx BIT(6)
#define mv BIT(5)
static int set_var(struct fbtft_par *par)
{
	/* madctl - memory data access control
	     rgb/bgr:
	     1. mode selection pin srgb
		rgb h/w pin for color filter setting: 0=rgb, 1=bgr
	     2. madctl rgb bit
		rgb-bgr order color filter panel: 0=rgb, 1=bgr */
	switch (par->info->var.rotate) {
	case 0:
		write_reg(par, 0x36, mx | my | (par->bgr << 3));
		break;
	case 270:
		write_reg(par, 0x36, my | mv | (par->bgr << 3));
		break;
	case 180:
		write_reg(par, 0x36, par->bgr << 3);
		break;
	case 90:
		write_reg(par, 0x36, mx | mv | (par->bgr << 3));
		break;
	}

	return 0;
}

/*
  gamma string format:
*/
static int set_gamma(struct fbtft_par *par, unsigned long *curves)
{
	write_reg(par, 0xE0,
		curves[0], curves[1], curves[2], curves[3],
		curves[4], curves[5], curves[6], curves[7],
		curves[8], curves[9], curves[10], curves[11],
		curves[12], curves[13], curves[14], curves[15],
		curves[16], curves[17], curves[18]);

	return 0;
}

static struct fbtft_display display = {
	.regwidth = 8,
	.width = 128,
	.height = 160,
	.gamma_num = 1,
	.gamma_len = 19,
	.gamma = DEFAULT_GAMMA,
	.fbtftops = {
		.init_display = init_display,
		.set_addr_win = set_addr_win,
		.set_var = set_var,
		.set_gamma = set_gamma,
	},
};

FBTFT_REGISTER_DRIVER(DRVNAME, "himax,hx8353d", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:hx8353d");
MODULE_ALIAS("platform:hx8353d");

MODULE_DESCRIPTION("FB driver for the HX8353D LCD Controller");
MODULE_AUTHOR("Petr Olivka");
MODULE_LICENSE("GPL");
