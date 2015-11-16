/*
 * FB driver for the ILI9341 LCD display controller
 *
 * This display uses 9-bit SPI: Data/Command bit + 8 data bits
 * For platforms that doesn't support 9-bit, the driver is capable
 * of emulating this using 8-bit transfer.
 * This is done by transferring eight 9-bit words in 9 bytes.
 *
 * Copyright (C) 2013 Christian Vogelgsang
 * Based on adafruit22fb.c by Noralf Tronnes
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

#define DRVNAME		"fb_ili9341"
#define WIDTH		240
#define HEIGHT		320
#define TXBUFLEN	(4 * PAGE_SIZE)
#define DEFAULT_GAMMA	"1F 1A 18 0A 0F 06 45 87 32 0A 07 02 07 05 00\n" \
			"00 25 27 05 10 09 3A 78 4D 05 18 0D 38 3A 1F"

static int init_display(struct fbtft_par *par)
{
	par->fbtftops.reset(par);

	/* startup sequence for MI0283QT-9A */
	write_reg(par, 0x01); /* software reset */
	mdelay(5);
	write_reg(par, 0x28); /* display off */
	/* --------------------------------------------------------- */
	write_reg(par, 0xCF, 0x00, 0x83, 0x30);
	write_reg(par, 0xED, 0x64, 0x03, 0x12, 0x81);
	write_reg(par, 0xE8, 0x85, 0x01, 0x79);
	write_reg(par, 0xCB, 0x39, 0X2C, 0x00, 0x34, 0x02);
	write_reg(par, 0xF7, 0x20);
	write_reg(par, 0xEA, 0x00, 0x00);
	/* ------------power control-------------------------------- */
	write_reg(par, 0xC0, 0x26);
	write_reg(par, 0xC1, 0x11);
	/* ------------VCOM --------- */
	write_reg(par, 0xC5, 0x35, 0x3E);
	write_reg(par, 0xC7, 0xBE);
	/* ------------memory access control------------------------ */
	write_reg(par, 0x3A, 0x55); /* 16bit pixel */
	/* ------------frame rate----------------------------------- */
	write_reg(par, 0xB1, 0x00, 0x1B);
	/* ------------Gamma---------------------------------------- */
	/* write_reg(par, 0xF2, 0x08); */ /* Gamma Function Disable */
	write_reg(par, 0x26, 0x01);
	/* ------------display-------------------------------------- */
	write_reg(par, 0xB7, 0x07); /* entry mode set */
	write_reg(par, 0xB6, 0x0A, 0x82, 0x27, 0x00);
	write_reg(par, 0x11); /* sleep out */
	mdelay(100);
	write_reg(par, 0x29); /* display on */
	mdelay(20);

	return 0;
}

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	/* Column address set */
	write_reg(par, 0x2A,
		(xs >> 8) & 0xFF, xs & 0xFF, (xe >> 8) & 0xFF, xe & 0xFF);

	/* Row address set */
	write_reg(par, 0x2B,
		(ys >> 8) & 0xFF, ys & 0xFF, (ye >> 8) & 0xFF, ye & 0xFF);

	/* Memory write */
	write_reg(par, 0x2C);
}

#define MEM_Y   (7) /* MY row address order */
#define MEM_X   (6) /* MX column address order */
#define MEM_V   (5) /* MV row / column exchange */
#define MEM_L   (4) /* ML vertical refresh order */
#define MEM_H   (2) /* MH horizontal refresh order */
#define MEM_BGR (3) /* RGB-BGR Order */
static int set_var(struct fbtft_par *par)
{
	switch (par->info->var.rotate) {
	case 0:
		write_reg(par, 0x36, (1 << MEM_X) | (par->bgr << MEM_BGR));
		break;
	case 270:
		write_reg(par, 0x36,
			(1 << MEM_V) | (1 << MEM_L) | (par->bgr << MEM_BGR));
		break;
	case 180:
		write_reg(par, 0x36, (1 << MEM_Y) | (par->bgr << MEM_BGR));
		break;
	case 90:
		write_reg(par, 0x36, (1 << MEM_Y) | (1 << MEM_X) |
				     (1 << MEM_V) | (par->bgr << MEM_BGR));
		break;
	}

	return 0;
}

/*
  Gamma string format:
    Positive: Par1 Par2 [...] Par15
    Negative: Par1 Par2 [...] Par15
*/
#define CURVE(num, idx)  curves[num * par->gamma.num_values + idx]
static int set_gamma(struct fbtft_par *par, unsigned long *curves)
{
	int i;

	for (i = 0; i < par->gamma.num_curves; i++)
		write_reg(par, 0xE0 + i,
			CURVE(i, 0), CURVE(i, 1), CURVE(i, 2),
			CURVE(i, 3), CURVE(i, 4), CURVE(i, 5),
			CURVE(i, 6), CURVE(i, 7), CURVE(i, 8),
			CURVE(i, 9), CURVE(i, 10), CURVE(i, 11),
			CURVE(i, 12), CURVE(i, 13), CURVE(i, 14));

	return 0;
}
#undef CURVE

static struct fbtft_display display = {
	.regwidth = 8,
	.width = WIDTH,
	.height = HEIGHT,
	.txbuflen = TXBUFLEN,
	.gamma_num = 2,
	.gamma_len = 15,
	.gamma = DEFAULT_GAMMA,
	.fbtftops = {
		.init_display = init_display,
		.set_addr_win = set_addr_win,
		.set_var = set_var,
		.set_gamma = set_gamma,
	},
};

FBTFT_REGISTER_DRIVER(DRVNAME, "ilitek,ili9341", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:ili9341");
MODULE_ALIAS("platform:ili9341");

MODULE_DESCRIPTION("FB driver for the ILI9341 LCD display controller");
MODULE_AUTHOR("Christian Vogelgsang");
MODULE_LICENSE("GPL");
