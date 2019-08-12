// SPDX-License-Identifier: GPL-2.0+
/*
 * FB driver for the SSD1289 LCD Controller
 *
 * Copyright (C) 2013 Noralf Tronnes
 *
 * Init sequence taken from ITDB02_Graph16.cpp - (C)2010-2011 Henning Karlsen
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio/consumer.h>

#include "fbtft.h"

#define DRVNAME		"fb_ssd1289"
#define WIDTH		240
#define HEIGHT		320
#define DEFAULT_GAMMA	"02 03 2 5 7 7 4 2 4 2\n" \
			"02 03 2 5 7 5 4 2 4 2"

static unsigned int reg11 = 0x6040;
module_param(reg11, uint, 0000);
MODULE_PARM_DESC(reg11, "Register 11h value");

static int init_display(struct fbtft_par *par)
{
	par->fbtftops.reset(par);

	if (par->gpio.cs)
		gpiod_set_value(par->gpio.cs, 0);  /* Activate chip */

	write_reg(par, 0x00, 0x0001);
	write_reg(par, 0x03, 0xA8A4);
	write_reg(par, 0x0C, 0x0000);
	write_reg(par, 0x0D, 0x080C);
	write_reg(par, 0x0E, 0x2B00);
	write_reg(par, 0x1E, 0x00B7);
	write_reg(par, 0x01,
		  BIT(13) | (par->bgr << 11) | BIT(9) | (HEIGHT - 1));
	write_reg(par, 0x02, 0x0600);
	write_reg(par, 0x10, 0x0000);
	write_reg(par, 0x05, 0x0000);
	write_reg(par, 0x06, 0x0000);
	write_reg(par, 0x16, 0xEF1C);
	write_reg(par, 0x17, 0x0003);
	write_reg(par, 0x07, 0x0233);
	write_reg(par, 0x0B, 0x0000);
	write_reg(par, 0x0F, 0x0000);
	write_reg(par, 0x41, 0x0000);
	write_reg(par, 0x42, 0x0000);
	write_reg(par, 0x48, 0x0000);
	write_reg(par, 0x49, 0x013F);
	write_reg(par, 0x4A, 0x0000);
	write_reg(par, 0x4B, 0x0000);
	write_reg(par, 0x44, 0xEF00);
	write_reg(par, 0x45, 0x0000);
	write_reg(par, 0x46, 0x013F);
	write_reg(par, 0x23, 0x0000);
	write_reg(par, 0x24, 0x0000);
	write_reg(par, 0x25, 0x8000);
	write_reg(par, 0x4f, 0x0000);
	write_reg(par, 0x4e, 0x0000);
	write_reg(par, 0x22);
	return 0;
}

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	switch (par->info->var.rotate) {
	/* R4Eh - Set GDDRAM X address counter */
	/* R4Fh - Set GDDRAM Y address counter */
	case 0:
		write_reg(par, 0x4e, xs);
		write_reg(par, 0x4f, ys);
		break;
	case 180:
		write_reg(par, 0x4e, par->info->var.xres - 1 - xs);
		write_reg(par, 0x4f, par->info->var.yres - 1 - ys);
		break;
	case 270:
		write_reg(par, 0x4e, par->info->var.yres - 1 - ys);
		write_reg(par, 0x4f, xs);
		break;
	case 90:
		write_reg(par, 0x4e, ys);
		write_reg(par, 0x4f, par->info->var.xres - 1 - xs);
		break;
	}

	/* R22h - RAM data write */
	write_reg(par, 0x22);
}

static int set_var(struct fbtft_par *par)
{
	if (par->fbtftops.init_display != init_display) {
		/* don't risk messing up register 11h */
		fbtft_par_dbg(DEBUG_INIT_DISPLAY, par,
			      "%s: skipping since custom init_display() is used\n",
			      __func__);
		return 0;
	}

	switch (par->info->var.rotate) {
	case 0:
		write_reg(par, 0x11, reg11 | 0x30);
		break;
	case 270:
		write_reg(par, 0x11, reg11 | 0x28);
		break;
	case 180:
		write_reg(par, 0x11, reg11 | 0x00);
		break;
	case 90:
		write_reg(par, 0x11, reg11 | 0x18);
		break;
	}

	return 0;
}

/*
 * Gamma string format:
 * VRP0 VRP1 PRP0 PRP1 PKP0 PKP1 PKP2 PKP3 PKP4 PKP5
 * VRN0 VRN1 PRN0 PRN1 PKN0 PKN1 PKN2 PKN3 PKN4 PKN5
 */
#define CURVE(num, idx)  curves[(num) * par->gamma.num_values + (idx)]
static int set_gamma(struct fbtft_par *par, u32 *curves)
{
	unsigned long mask[] = {
		0x1f, 0x1f, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
		0x1f, 0x1f, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	};
	int i, j;

	/* apply mask */
	for (i = 0; i < 2; i++)
		for (j = 0; j < 10; j++)
			CURVE(i, j) &= mask[i * par->gamma.num_values + j];

	write_reg(par, 0x0030, CURVE(0, 5) << 8 | CURVE(0, 4));
	write_reg(par, 0x0031, CURVE(0, 7) << 8 | CURVE(0, 6));
	write_reg(par, 0x0032, CURVE(0, 9) << 8 | CURVE(0, 8));
	write_reg(par, 0x0033, CURVE(0, 3) << 8 | CURVE(0, 2));
	write_reg(par, 0x0034, CURVE(1, 5) << 8 | CURVE(1, 4));
	write_reg(par, 0x0035, CURVE(1, 7) << 8 | CURVE(1, 6));
	write_reg(par, 0x0036, CURVE(1, 9) << 8 | CURVE(1, 8));
	write_reg(par, 0x0037, CURVE(1, 3) << 8 | CURVE(1, 2));
	write_reg(par, 0x003A, CURVE(0, 1) << 8 | CURVE(0, 0));
	write_reg(par, 0x003B, CURVE(1, 1) << 8 | CURVE(1, 0));

	return 0;
}

#undef CURVE

static struct fbtft_display display = {
	.regwidth = 16,
	.width = WIDTH,
	.height = HEIGHT,
	.gamma_num = 2,
	.gamma_len = 10,
	.gamma = DEFAULT_GAMMA,
	.fbtftops = {
		.init_display = init_display,
		.set_addr_win = set_addr_win,
		.set_var = set_var,
		.set_gamma = set_gamma,
	},
};

FBTFT_REGISTER_DRIVER(DRVNAME, "solomon,ssd1289", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:ssd1289");
MODULE_ALIAS("platform:ssd1289");

MODULE_DESCRIPTION("FB driver for the SSD1289 LCD Controller");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
