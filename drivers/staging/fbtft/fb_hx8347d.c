/*
 * FB driver for the HX8347D LCD Controller
 *
 * Copyright (C) 2013 Christian Vogelgsang
 *
 * Based on driver code found here: https://github.com/watterott/r61505u-Adapter
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

#define DRVNAME		"fb_hx8347d"
#define WIDTH		320
#define HEIGHT		240
#define DEFAULT_GAMMA	"0 0 0 0 0 0 0 0 0 0 0 0 0 0\n" \
			"0 0 0 0 0 0 0 0 0 0 0 0 0 0"

static int init_display(struct fbtft_par *par)
{
	par->fbtftops.reset(par);

	/* driving ability */
	write_reg(par, 0xEA, 0x00);
	write_reg(par, 0xEB, 0x20);
	write_reg(par, 0xEC, 0x0C);
	write_reg(par, 0xED, 0xC4);
	write_reg(par, 0xE8, 0x40);
	write_reg(par, 0xE9, 0x38);
	write_reg(par, 0xF1, 0x01);
	write_reg(par, 0xF2, 0x10);
	write_reg(par, 0x27, 0xA3);

	/* power voltage */
	write_reg(par, 0x1B, 0x1B);
	write_reg(par, 0x1A, 0x01);
	write_reg(par, 0x24, 0x2F);
	write_reg(par, 0x25, 0x57);

	/* VCOM offset */
	write_reg(par, 0x23, 0x8D); /* for flicker adjust */

	/* power on */
	write_reg(par, 0x18, 0x36);
	write_reg(par, 0x19, 0x01); /* start osc */
	write_reg(par, 0x01, 0x00); /* wakeup */
	write_reg(par, 0x1F, 0x88);
	mdelay(5);
	write_reg(par, 0x1F, 0x80);
	mdelay(5);
	write_reg(par, 0x1F, 0x90);
	mdelay(5);
	write_reg(par, 0x1F, 0xD0);
	mdelay(5);

	/* color selection */
	write_reg(par, 0x17, 0x05); /* 65k */

	/*panel characteristic */
	write_reg(par, 0x36, 0x00);

	/*display on */
	write_reg(par, 0x28, 0x38);
	mdelay(40);
	write_reg(par, 0x28, 0x3C);

	/* orientation */
	write_reg(par, 0x16, 0x60 | (par->bgr << 3));

	return 0;
}

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	write_reg(par, 0x02, (xs >> 8) & 0xFF);
	write_reg(par, 0x03, xs & 0xFF);
	write_reg(par, 0x04, (xe >> 8) & 0xFF);
	write_reg(par, 0x05, xe & 0xFF);
	write_reg(par, 0x06, (ys >> 8) & 0xFF);
	write_reg(par, 0x07, ys & 0xFF);
	write_reg(par, 0x08, (ye >> 8) & 0xFF);
	write_reg(par, 0x09, ye & 0xFF);
	write_reg(par, 0x22);
}

/*
 * Gamma string format:
 *   VRP0 VRP1 VRP2 VRP3 VRP4 VRP5 PRP0 PRP1 PKP0 PKP1 PKP2 PKP3 PKP4 CGM
 *   VRN0 VRN1 VRN2 VRN3 VRN4 VRN5 PRN0 PRN1 PKN0 PKN1 PKN2 PKN3 PKN4 CGM
 */
#define CURVE(num, idx)  curves[num * par->gamma.num_values + idx]
static int set_gamma(struct fbtft_par *par, unsigned long *curves)
{
	unsigned long mask[] = {
		0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x7f, 0x7f, 0x1f, 0x1f,
		0x1f, 0x1f, 0x1f, 0x0f,
	};
	int i, j;
	int acc = 0;

	/* apply mask */
	for (i = 0; i < par->gamma.num_curves; i++)
		for (j = 0; j < par->gamma.num_values; j++) {
			acc += CURVE(i, j);
			CURVE(i, j) &= mask[j];
		}

	if (acc == 0) /* skip if all values are zero */
		return 0;

	for (i = 0; i < par->gamma.num_curves; i++) {
		write_reg(par, 0x40 + (i * 0x10), CURVE(i, 0));
		write_reg(par, 0x41 + (i * 0x10), CURVE(i, 1));
		write_reg(par, 0x42 + (i * 0x10), CURVE(i, 2));
		write_reg(par, 0x43 + (i * 0x10), CURVE(i, 3));
		write_reg(par, 0x44 + (i * 0x10), CURVE(i, 4));
		write_reg(par, 0x45 + (i * 0x10), CURVE(i, 5));
		write_reg(par, 0x46 + (i * 0x10), CURVE(i, 6));
		write_reg(par, 0x47 + (i * 0x10), CURVE(i, 7));
		write_reg(par, 0x48 + (i * 0x10), CURVE(i, 8));
		write_reg(par, 0x49 + (i * 0x10), CURVE(i, 9));
		write_reg(par, 0x4A + (i * 0x10), CURVE(i, 10));
		write_reg(par, 0x4B + (i * 0x10), CURVE(i, 11));
		write_reg(par, 0x4C + (i * 0x10), CURVE(i, 12));
	}
	write_reg(par, 0x5D, (CURVE(1, 0) << 4) | CURVE(0, 0));

	return 0;
}

#undef CURVE

static struct fbtft_display display = {
	.regwidth = 8,
	.width = WIDTH,
	.height = HEIGHT,
	.gamma_num = 2,
	.gamma_len = 14,
	.gamma = DEFAULT_GAMMA,
	.fbtftops = {
		.init_display = init_display,
		.set_addr_win = set_addr_win,
		.set_gamma = set_gamma,
	},
};

FBTFT_REGISTER_DRIVER(DRVNAME, "himax,hx8347d", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:hx8347d");
MODULE_ALIAS("platform:hx8347d");

MODULE_DESCRIPTION("FB driver for the HX8347D LCD Controller");
MODULE_AUTHOR("Christian Vogelgsang");
MODULE_LICENSE("GPL");
