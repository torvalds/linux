/*
 * FB driver for the S6D1121 LCD Controller
 *
 * Copyright (C) 2013 Roman Rolinsky
 *
 * Based on fb_ili9325.c by Noralf Tronnes
 * Based on ili9325.c by Jeroen Domburg
 * Init code from UTFT library by Henning Karlsen
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
#include <linux/gpio.h>
#include <linux/delay.h>

#include "fbtft.h"

#define DRVNAME		"fb_s6d1121"
#define WIDTH		240
#define HEIGHT		320
#define BPP		16
#define FPS		20
#define DEFAULT_GAMMA	"26 09 24 2C 1F 23 24 25 22 26 25 23 0D 00\n" \
			"1C 1A 13 1D 0B 11 12 10 13 15 36 19 00 0D"

static int init_display(struct fbtft_par *par)
{
	par->fbtftops.reset(par);

	if (par->gpio.cs != -1)
		gpio_set_value(par->gpio.cs, 0);  /* Activate chip */

	/* Initialization sequence from Lib_UTFT */

	write_reg(par, 0x0011, 0x2004);
	write_reg(par, 0x0013, 0xCC00);
	write_reg(par, 0x0015, 0x2600);
	write_reg(par, 0x0014, 0x252A);
	write_reg(par, 0x0012, 0x0033);
	write_reg(par, 0x0013, 0xCC04);
	write_reg(par, 0x0013, 0xCC06);
	write_reg(par, 0x0013, 0xCC4F);
	write_reg(par, 0x0013, 0x674F);
	write_reg(par, 0x0011, 0x2003);
	write_reg(par, 0x0016, 0x0007);
	write_reg(par, 0x0002, 0x0013);
	write_reg(par, 0x0003, 0x0003);
	write_reg(par, 0x0001, 0x0127);
	write_reg(par, 0x0008, 0x0303);
	write_reg(par, 0x000A, 0x000B);
	write_reg(par, 0x000B, 0x0003);
	write_reg(par, 0x000C, 0x0000);
	write_reg(par, 0x0041, 0x0000);
	write_reg(par, 0x0050, 0x0000);
	write_reg(par, 0x0060, 0x0005);
	write_reg(par, 0x0070, 0x000B);
	write_reg(par, 0x0071, 0x0000);
	write_reg(par, 0x0078, 0x0000);
	write_reg(par, 0x007A, 0x0000);
	write_reg(par, 0x0079, 0x0007);
	write_reg(par, 0x0007, 0x0051);
	write_reg(par, 0x0007, 0x0053);
	write_reg(par, 0x0079, 0x0000);

	write_reg(par, 0x0022); /* Write Data to GRAM */

	return 0;
}

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	switch (par->info->var.rotate) {
	/* R20h = Horizontal GRAM Start Address */
	/* R21h = Vertical GRAM Start Address */
	case 0:
		write_reg(par, 0x0020, xs);
		write_reg(par, 0x0021, ys);
		break;
	case 180:
		write_reg(par, 0x0020, WIDTH - 1 - xs);
		write_reg(par, 0x0021, HEIGHT - 1 - ys);
		break;
	case 270:
		write_reg(par, 0x0020, WIDTH - 1 - ys);
		write_reg(par, 0x0021, xs);
		break;
	case 90:
		write_reg(par, 0x0020, ys);
		write_reg(par, 0x0021, HEIGHT - 1 - xs);
		break;
	}
	write_reg(par, 0x0022); /* Write Data to GRAM */
}

static int set_var(struct fbtft_par *par)
{
	switch (par->info->var.rotate) {
	/* AM: GRAM update direction */
	case 0:
		write_reg(par, 0x03, 0x0003 | (par->bgr << 12));
		break;
	case 180:
		write_reg(par, 0x03, 0x0000 | (par->bgr << 12));
		break;
	case 270:
		write_reg(par, 0x03, 0x000A | (par->bgr << 12));
		break;
	case 90:
		write_reg(par, 0x03, 0x0009 | (par->bgr << 12));
		break;
	}

	return 0;
}

/*
 * Gamma string format:
 * PKP0 PKP1 PKP2 PKP3 PKP4 PKP5 PKP6 PKP7 PKP8 PKP9 PKP10 PKP11 VRP0 VRP1
 * PKN0 PKN1 PKN2 PKN3 PKN4 PKN5 PKN6 PKN7 PRN8 PRN9 PRN10 PRN11 VRN0 VRN1
 */
#define CURVE(num, idx)  curves[num * par->gamma.num_values + idx]
static int set_gamma(struct fbtft_par *par, unsigned long *curves)
{
	unsigned long mask[] = {
		0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f,
		0x3f, 0x3f, 0x1f, 0x1f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f,
		0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x1f, 0x1f,
	};
	int i, j;

	/* apply mask */
	for (i = 0; i < 2; i++)
		for (j = 0; j < 14; j++)
			CURVE(i, j) &= mask[i * par->gamma.num_values + j];

	write_reg(par, 0x0030, CURVE(0, 1) << 8 | CURVE(0, 0));
	write_reg(par, 0x0031, CURVE(0, 3) << 8 | CURVE(0, 2));
	write_reg(par, 0x0032, CURVE(0, 5) << 8 | CURVE(0, 3));
	write_reg(par, 0x0033, CURVE(0, 7) << 8 | CURVE(0, 6));
	write_reg(par, 0x0034, CURVE(0, 9) << 8 | CURVE(0, 8));
	write_reg(par, 0x0035, CURVE(0, 11) << 8 | CURVE(0, 10));

	write_reg(par, 0x0036, CURVE(1, 1) << 8 | CURVE(1, 0));
	write_reg(par, 0x0037, CURVE(1, 3) << 8 | CURVE(1, 2));
	write_reg(par, 0x0038, CURVE(1, 5) << 8 | CURVE(1, 4));
	write_reg(par, 0x0039, CURVE(1, 7) << 8 | CURVE(1, 6));
	write_reg(par, 0x003A, CURVE(1, 9) << 8 | CURVE(1, 8));
	write_reg(par, 0x003B, CURVE(1, 11) << 8 | CURVE(1, 10));

	write_reg(par, 0x003C, CURVE(0, 13) << 8 | CURVE(0, 12));
	write_reg(par, 0x003D, CURVE(1, 13) << 8 | CURVE(1, 12));

	return 0;
}
#undef CURVE

static struct fbtft_display display = {
	.regwidth = 16,
	.width = WIDTH,
	.height = HEIGHT,
	.bpp = BPP,
	.fps = FPS,
	.gamma_num = 2,
	.gamma_len = 14,
	.gamma = DEFAULT_GAMMA,
	.fbtftops = {
		.init_display = init_display,
		.set_addr_win = set_addr_win,
		.set_var = set_var,
		.set_gamma = set_gamma,
	},
};

FBTFT_REGISTER_DRIVER(DRVNAME, "samsung,s6d1121", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:s6d1121");
MODULE_ALIAS("platform:s6d1121");

MODULE_DESCRIPTION("FB driver for the S6D1121 LCD Controller");
MODULE_AUTHOR("Roman Rolinsky");
MODULE_LICENSE("GPL");
