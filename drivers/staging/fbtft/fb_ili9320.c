/*
 * FB driver for the ILI9320 LCD Controller
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
#include <linux/spi/spi.h>
#include <linux/delay.h>

#include "fbtft.h"

#define DRVNAME		"fb_ili9320"
#define WIDTH		240
#define HEIGHT		320
#define DEFAULT_GAMMA	"07 07 6 0 0 0 5 5 4 0\n" \
			"07 08 4 7 5 1 2 0 7 7"

static unsigned read_devicecode(struct fbtft_par *par)
{
	int ret;
	u8 rxbuf[8] = {0, };

	write_reg(par, 0x0000);
	ret = par->fbtftops.read(par, rxbuf, 4);
	return (rxbuf[2] << 8) | rxbuf[3];
}

static int init_display(struct fbtft_par *par)
{
	unsigned devcode;

	par->fbtftops.reset(par);

	devcode = read_devicecode(par);
	fbtft_par_dbg(DEBUG_INIT_DISPLAY, par, "Device code: 0x%04X\n",
		      devcode);
	if ((devcode != 0x0000) && (devcode != 0x9320))
		dev_warn(par->info->device,
			 "Unrecognized Device code: 0x%04X (expected 0x9320)\n",
			devcode);

	/* Initialization sequence from ILI9320 Application Notes */

	/* *********** Start Initial Sequence ********* */
	/* Set the Vcore voltage and this setting is must. */
	write_reg(par, 0x00E5, 0x8000);

	/* Start internal OSC. */
	write_reg(par, 0x0000, 0x0001);

	/* set SS and SM bit */
	write_reg(par, 0x0001, 0x0100);

	/* set 1 line inversion */
	write_reg(par, 0x0002, 0x0700);

	/* Resize register */
	write_reg(par, 0x0004, 0x0000);

	/* set the back and front porch */
	write_reg(par, 0x0008, 0x0202);

	/* set non-display area refresh cycle */
	write_reg(par, 0x0009, 0x0000);

	/* FMARK function */
	write_reg(par, 0x000A, 0x0000);

	/* RGB interface setting */
	write_reg(par, 0x000C, 0x0000);

	/* Frame marker Position */
	write_reg(par, 0x000D, 0x0000);

	/* RGB interface polarity */
	write_reg(par, 0x000F, 0x0000);

	/* ***********Power On sequence *************** */
	/* SAP, BT[3:0], AP, DSTB, SLP, STB */
	write_reg(par, 0x0010, 0x0000);

	/* DC1[2:0], DC0[2:0], VC[2:0] */
	write_reg(par, 0x0011, 0x0007);

	/* VREG1OUT voltage */
	write_reg(par, 0x0012, 0x0000);

	/* VDV[4:0] for VCOM amplitude */
	write_reg(par, 0x0013, 0x0000);

	/* Dis-charge capacitor power voltage */
	mdelay(200);

	/* SAP, BT[3:0], AP, DSTB, SLP, STB */
	write_reg(par, 0x0010, 0x17B0);

	/* R11h=0x0031 at VCI=3.3V DC1[2:0], DC0[2:0], VC[2:0] */
	write_reg(par, 0x0011, 0x0031);
	mdelay(50);

	/* R12h=0x0138 at VCI=3.3V VREG1OUT voltage */
	write_reg(par, 0x0012, 0x0138);
	mdelay(50);

	/* R13h=0x1800 at VCI=3.3V VDV[4:0] for VCOM amplitude */
	write_reg(par, 0x0013, 0x1800);

	/* R29h=0x0008 at VCI=3.3V VCM[4:0] for VCOMH */
	write_reg(par, 0x0029, 0x0008);
	mdelay(50);

	/* GRAM horizontal Address */
	write_reg(par, 0x0020, 0x0000);

	/* GRAM Vertical Address */
	write_reg(par, 0x0021, 0x0000);

	/* ------------------ Set GRAM area --------------- */
	/* Horizontal GRAM Start Address */
	write_reg(par, 0x0050, 0x0000);

	/* Horizontal GRAM End Address */
	write_reg(par, 0x0051, 0x00EF);

	/* Vertical GRAM Start Address */
	write_reg(par, 0x0052, 0x0000);

	/* Vertical GRAM End Address */
	write_reg(par, 0x0053, 0x013F);

	/* Gate Scan Line */
	write_reg(par, 0x0060, 0x2700);

	/* NDL,VLE, REV */
	write_reg(par, 0x0061, 0x0001);

	/* set scrolling line */
	write_reg(par, 0x006A, 0x0000);

	/* -------------- Partial Display Control --------- */
	write_reg(par, 0x0080, 0x0000);
	write_reg(par, 0x0081, 0x0000);
	write_reg(par, 0x0082, 0x0000);
	write_reg(par, 0x0083, 0x0000);
	write_reg(par, 0x0084, 0x0000);
	write_reg(par, 0x0085, 0x0000);

	/* -------------- Panel Control ------------------- */
	write_reg(par, 0x0090, 0x0010);
	write_reg(par, 0x0092, 0x0000);
	write_reg(par, 0x0093, 0x0003);
	write_reg(par, 0x0095, 0x0110);
	write_reg(par, 0x0097, 0x0000);
	write_reg(par, 0x0098, 0x0000);
	write_reg(par, 0x0007, 0x0173); /* 262K color and display ON */

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
	case 0:
		write_reg(par, 0x3, (par->bgr << 12) | 0x30);
		break;
	case 270:
		write_reg(par, 0x3, (par->bgr << 12) | 0x28);
		break;
	case 180:
		write_reg(par, 0x3, (par->bgr << 12) | 0x00);
		break;
	case 90:
		write_reg(par, 0x3, (par->bgr << 12) | 0x18);
		break;
	}
	return 0;
}

/*
 * Gamma string format:
 *  VRP0 VRP1 RP0 RP1 KP0 KP1 KP2 KP3 KP4 KP5
 *  VRN0 VRN1 RN0 RN1 KN0 KN1 KN2 KN3 KN4 KN5
 */
#define CURVE(num, idx)  curves[num * par->gamma.num_values + idx]
static int set_gamma(struct fbtft_par *par, unsigned long *curves)
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
	write_reg(par, 0x0035, CURVE(0, 3) << 8 | CURVE(0, 2));
	write_reg(par, 0x0036, CURVE(0, 1) << 8 | CURVE(0, 0));

	write_reg(par, 0x0037, CURVE(1, 5) << 8 | CURVE(1, 4));
	write_reg(par, 0x0038, CURVE(1, 7) << 8 | CURVE(1, 6));
	write_reg(par, 0x0039, CURVE(1, 9) << 8 | CURVE(1, 8));
	write_reg(par, 0x003C, CURVE(1, 3) << 8 | CURVE(1, 2));
	write_reg(par, 0x003D, CURVE(1, 1) << 8 | CURVE(1, 0));

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

FBTFT_REGISTER_DRIVER(DRVNAME, "ilitek,ili9320", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:ili9320");
MODULE_ALIAS("platform:ili9320");

MODULE_DESCRIPTION("FB driver for the ILI9320 LCD Controller");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
