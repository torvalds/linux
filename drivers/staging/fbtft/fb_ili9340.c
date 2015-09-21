/*
 * FB driver for the ILI9340 LCD Controller
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
#include <linux/gpio.h>
#include <linux/delay.h>

#include "fbtft.h"

#define DRVNAME		"fb_ili9340"
#define WIDTH		240
#define HEIGHT		320

/* Init sequence taken from: Arduino Library for the Adafruit 2.2" display */
static int init_display(struct fbtft_par *par)
{
	par->fbtftops.reset(par);

	write_reg(par, 0xEF, 0x03, 0x80, 0x02);
	write_reg(par, 0xCF, 0x00, 0XC1, 0X30);
	write_reg(par, 0xED, 0x64, 0x03, 0X12, 0X81);
	write_reg(par, 0xE8, 0x85, 0x00, 0x78);
	write_reg(par, 0xCB, 0x39, 0x2C, 0x00, 0x34, 0x02);
	write_reg(par, 0xF7, 0x20);
	write_reg(par, 0xEA, 0x00, 0x00);

	/* Power Control 1 */
	write_reg(par, 0xC0, 0x23);

	/* Power Control 2 */
	write_reg(par, 0xC1, 0x10);

	/* VCOM Control 1 */
	write_reg(par, 0xC5, 0x3e, 0x28);

	/* VCOM Control 2 */
	write_reg(par, 0xC7, 0x86);

	/* COLMOD: Pixel Format Set */
	/* 16 bits/pixel */
	write_reg(par, 0x3A, 0x55);

	/* Frame Rate Control */
	/* Division ratio = fosc, Frame Rate = 79Hz */
	write_reg(par, 0xB1, 0x00, 0x18);

	/* Display Function Control */
	write_reg(par, 0xB6, 0x08, 0x82, 0x27);

	/* Gamma Function Disable */
	write_reg(par, 0xF2, 0x00);

	/* Gamma curve selected  */
	write_reg(par, 0x26, 0x01);

	/* Positive Gamma Correction */
	write_reg(par, 0xE0,
		0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
		0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00);

	/* Negative Gamma Correction */
	write_reg(par, 0xE1,
		0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
		0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F);

	/* Sleep OUT */
	write_reg(par, 0x11);

	mdelay(120);

	/* Display ON */
	write_reg(par, 0x29);

	return 0;
}

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	/* Column address */
	write_reg(par, 0x2A, xs >> 8, xs & 0xFF, xe >> 8, xe & 0xFF);

	/* Row address */
	write_reg(par, 0x2B, ys >> 8, ys & 0xFF, ye >> 8, ye & 0xFF);

	/* Memory write */
	write_reg(par, 0x2C);
}

#define ILI9340_MADCTL_MV  0x20
#define ILI9340_MADCTL_MX  0x40
#define ILI9340_MADCTL_MY  0x80
static int set_var(struct fbtft_par *par)
{
	u8 val;

	switch (par->info->var.rotate) {
	case 270:
		val = ILI9340_MADCTL_MV;
		break;
	case 180:
		val = ILI9340_MADCTL_MY;
		break;
	case 90:
		val = ILI9340_MADCTL_MV | ILI9340_MADCTL_MY | ILI9340_MADCTL_MX;
		break;
	default:
		val = ILI9340_MADCTL_MX;
		break;
	}
	/* Memory Access Control  */
	write_reg(par, 0x36, val | (par->bgr << 3));

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

FBTFT_REGISTER_DRIVER(DRVNAME, "ilitek,ili9340", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:ili9340");
MODULE_ALIAS("platform:ili9340");

MODULE_DESCRIPTION("FB driver for the ILI9340 LCD Controller");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
