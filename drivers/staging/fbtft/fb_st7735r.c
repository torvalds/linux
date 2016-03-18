/*
 * FB driver for the ST7735R LCD Controller
 *
 * Copyright (C) 2013 Noralf Tronnes
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
#include <video/mipi_display.h>

#include "fbtft.h"

#define DRVNAME "fb_st7735r"
#define DEFAULT_GAMMA   "0F 1A 0F 18 2F 28 20 22 1F 1B 23 37 00 07 02 10\n" \
			"0F 1B 0F 17 33 2C 29 2E 30 30 39 3F 00 07 03 10"

static int default_init_sequence[] = {
	-1, MIPI_DCS_SOFT_RESET,
	-2, 150,                               /* delay */

	-1, MIPI_DCS_EXIT_SLEEP_MODE,
	-2, 500,                               /* delay */

	/* FRMCTR1 - frame rate control: normal mode
	     frame rate = fosc / (1 x 2 + 40) * (LINE + 2C + 2D) */
	-1, 0xB1, 0x01, 0x2C, 0x2D,

	/* FRMCTR2 - frame rate control: idle mode
	     frame rate = fosc / (1 x 2 + 40) * (LINE + 2C + 2D) */
	-1, 0xB2, 0x01, 0x2C, 0x2D,

	/* FRMCTR3 - frame rate control - partial mode
	     dot inversion mode, line inversion mode */
	-1, 0xB3, 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D,

	/* INVCTR - display inversion control
	     no inversion */
	-1, 0xB4, 0x07,

	/* PWCTR1 - Power Control
	     -4.6V, AUTO mode */
	-1, 0xC0, 0xA2, 0x02, 0x84,

	/* PWCTR2 - Power Control
	     VGH25 = 2.4C VGSEL = -10 VGH = 3 * AVDD */
	-1, 0xC1, 0xC5,

	/* PWCTR3 - Power Control
	     Opamp current small, Boost frequency */
	-1, 0xC2, 0x0A, 0x00,

	/* PWCTR4 - Power Control
	     BCLK/2, Opamp current small & Medium low */
	-1, 0xC3, 0x8A, 0x2A,

	/* PWCTR5 - Power Control */
	-1, 0xC4, 0x8A, 0xEE,

	/* VMCTR1 - Power Control */
	-1, 0xC5, 0x0E,

	-1, MIPI_DCS_EXIT_INVERT_MODE,

	-1, MIPI_DCS_SET_PIXEL_FORMAT, MIPI_DCS_PIXEL_FMT_16BIT,

	-1, MIPI_DCS_SET_DISPLAY_ON,
	-2, 100,                               /* delay */

	-1, MIPI_DCS_ENTER_NORMAL_MODE,
	-2, 10,                               /* delay */

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

#define MY BIT(7)
#define MX BIT(6)
#define MV BIT(5)
static int set_var(struct fbtft_par *par)
{
	/* MADCTL - Memory data access control
	     RGB/BGR:
	     1. Mode selection pin SRGB
		RGB H/W pin for color filter setting: 0=RGB, 1=BGR
	     2. MADCTL RGB bit
		RGB-BGR ORDER color filter panel: 0=RGB, 1=BGR */
	switch (par->info->var.rotate) {
	case 0:
		write_reg(par, MIPI_DCS_SET_ADDRESS_MODE,
			  MX | MY | (par->bgr << 3));
		break;
	case 270:
		write_reg(par, MIPI_DCS_SET_ADDRESS_MODE,
			  MY | MV | (par->bgr << 3));
		break;
	case 180:
		write_reg(par, MIPI_DCS_SET_ADDRESS_MODE,
			  par->bgr << 3);
		break;
	case 90:
		write_reg(par, MIPI_DCS_SET_ADDRESS_MODE,
			  MX | MV | (par->bgr << 3));
		break;
	}

	return 0;
}

/*
  Gamma string format:
    VRF0P VOS0P PK0P PK1P PK2P PK3P PK4P PK5P PK6P PK7P PK8P PK9P SELV0P SELV1P SELV62P SELV63P
    VRF0N VOS0N PK0N PK1N PK2N PK3N PK4N PK5N PK6N PK7N PK8N PK9N SELV0N SELV1N SELV62N SELV63N
*/
#define CURVE(num, idx)  curves[num * par->gamma.num_values + idx]
static int set_gamma(struct fbtft_par *par, unsigned long *curves)
{
	int i, j;

	/* apply mask */
	for (i = 0; i < par->gamma.num_curves; i++)
		for (j = 0; j < par->gamma.num_values; j++)
			CURVE(i, j) &= 0x3f;

	for (i = 0; i < par->gamma.num_curves; i++)
		write_reg(par, 0xE0 + i,
			CURVE(i, 0), CURVE(i, 1), CURVE(i, 2), CURVE(i, 3),
			CURVE(i, 4), CURVE(i, 5), CURVE(i, 6), CURVE(i, 7),
			CURVE(i, 8), CURVE(i, 9), CURVE(i, 10), CURVE(i, 11),
			CURVE(i, 12), CURVE(i, 13), CURVE(i, 14), CURVE(i, 15));

	return 0;
}
#undef CURVE

static struct fbtft_display display = {
	.regwidth = 8,
	.width = 128,
	.height = 160,
	.init_sequence = default_init_sequence,
	.gamma_num = 2,
	.gamma_len = 16,
	.gamma = DEFAULT_GAMMA,
	.fbtftops = {
		.set_addr_win = set_addr_win,
		.set_var = set_var,
		.set_gamma = set_gamma,
	},
};

FBTFT_REGISTER_DRIVER(DRVNAME, "sitronix,st7735r", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:st7735r");
MODULE_ALIAS("platform:st7735r");

MODULE_DESCRIPTION("FB driver for the ST7735R LCD Controller");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
