// SPDX-License-Identifier: GPL-2.0+
/*
 * FB driver for the ILI9163 LCD Controller
 *
 * Copyright (C) 2015 Kozhevnikov Anatoly
 *
 * Based on ili9325.c by Noralf Tronnes and
 * .S.U.M.O.T.O.Y. by Max MC Costa (https://github.com/sumotoy/TFT_ILI9163C).
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <video/mipi_display.h>

#include "fbtft.h"

#define DRVNAME		"fb_ili9163"
#define WIDTH		128
#define HEIGHT		128
#define BPP		16
#define FPS		30

#ifdef GAMMA_ADJ
#define GAMMA_LEN	15
#define GAMMA_NUM	1
#define DEFAULT_GAMMA	"36 29 12 22 1C 15 42 B7 2F 13 12 0A 11 0B 06\n"
#endif

/* ILI9163C commands */
#define CMD_FRMCTR1	0xB1 /* Frame Rate Control */
			     /*	(In normal mode/Full colors) */
#define CMD_FRMCTR2	0xB2 /* Frame Rate Control (In Idle mode/8-colors) */
#define CMD_FRMCTR3	0xB3 /* Frame Rate Control */
			     /*	(In Partial mode/full colors) */
#define CMD_DINVCTR	0xB4 /* Display Inversion Control */
#define CMD_RGBBLK	0xB5 /* RGB Interface Blanking Porch setting */
#define CMD_DFUNCTR	0xB6 /* Display Function set 5 */
#define CMD_SDRVDIR	0xB7 /* Source Driver Direction Control */
#define CMD_GDRVDIR	0xB8 /* Gate Driver Direction Control  */

#define CMD_PWCTR1	0xC0 /* Power_Control1 */
#define CMD_PWCTR2	0xC1 /* Power_Control2 */
#define CMD_PWCTR3	0xC2 /* Power_Control3 */
#define CMD_PWCTR4	0xC3 /* Power_Control4 */
#define CMD_PWCTR5	0xC4 /* Power_Control5 */
#define CMD_VCOMCTR1	0xC5 /* VCOM_Control 1 */
#define CMD_VCOMCTR2	0xC6 /* VCOM_Control 2 */
#define CMD_VCOMOFFS	0xC7 /* VCOM Offset Control */
#define CMD_PGAMMAC	0xE0 /* Positive Gamma Correction Setting */
#define CMD_NGAMMAC	0xE1 /* Negative Gamma Correction Setting */
#define CMD_GAMRSEL	0xF2 /* GAM_R_SEL */

/*
 * This display:
 * http://www.ebay.com/itm/Replace-Nokia-5110-LCD-1-44-Red-Serial-128X128-SPI-
 * Color-TFT-LCD-Display-Module-/271422122271
 * This particular display has a design error! The controller has 3 pins to
 * configure to constrain the memory and resolution to a fixed dimension (in
 * that case 128x128) but they leaved those pins configured for 128x160 so
 * there was several pixel memory addressing problems.
 * I solved by setup several parameters that dinamically fix the resolution as
 * needit so below the parameters for this display. If you have a strain or a
 * correct display (can happen with chinese) you can copy those parameters and
 * create setup for different displays.
 */

#ifdef RED
#define __OFFSET		32 /*see note 2 - this is the red version */
#else
#define __OFFSET		0  /*see note 2 - this is the black version */
#endif

static int init_display(struct fbtft_par *par)
{
	par->fbtftops.reset(par);

	if (par->gpio.cs)
		gpiod_set_value(par->gpio.cs, 0);  /* Activate chip */

	write_reg(par, MIPI_DCS_SOFT_RESET); /* software reset */
	mdelay(500);
	write_reg(par, MIPI_DCS_EXIT_SLEEP_MODE); /* exit sleep */
	mdelay(5);
	write_reg(par, MIPI_DCS_SET_PIXEL_FORMAT, MIPI_DCS_PIXEL_FMT_16BIT);
	/* default gamma curve 3 */
	write_reg(par, MIPI_DCS_SET_GAMMA_CURVE, 0x02);
#ifdef GAMMA_ADJ
	write_reg(par, CMD_GAMRSEL, 0x01); /* Enable Gamma adj */
#endif
	write_reg(par, MIPI_DCS_ENTER_NORMAL_MODE);
	write_reg(par, CMD_DFUNCTR, 0xff, 0x06);
	/* Frame Rate Control (In normal mode/Full colors) */
	write_reg(par, CMD_FRMCTR1, 0x08, 0x02);
	write_reg(par, CMD_DINVCTR, 0x07); /* display inversion  */
	/* Set VRH1[4:0] & VC[2:0] for VCI1 & GVDD */
	write_reg(par, CMD_PWCTR1, 0x0A, 0x02);
	/* Set BT[2:0] for AVDD & VCL & VGH & VGL  */
	write_reg(par, CMD_PWCTR2, 0x02);
	/* Set VMH[6:0] & VML[6:0] for VOMH & VCOML */
	write_reg(par, CMD_VCOMCTR1, 0x50, 0x63);
	write_reg(par, CMD_VCOMOFFS, 0);

	write_reg(par, MIPI_DCS_SET_COLUMN_ADDRESS, 0, 0, 0, WIDTH);
	write_reg(par, MIPI_DCS_SET_PAGE_ADDRESS, 0, 0, 0, HEIGHT);

	write_reg(par, MIPI_DCS_SET_DISPLAY_ON); /* display ON */
	write_reg(par, MIPI_DCS_WRITE_MEMORY_START); /* Memory Write */

	return 0;
}

static void set_addr_win(struct fbtft_par *par, int xs, int ys,
			 int xe, int ye)
{
	switch (par->info->var.rotate) {
	case 0:
		write_reg(par, MIPI_DCS_SET_COLUMN_ADDRESS,
			  xs >> 8, xs & 0xff, xe >> 8, xe & 0xff);
		write_reg(par, MIPI_DCS_SET_PAGE_ADDRESS,
			  (ys + __OFFSET) >> 8, (ys + __OFFSET) & 0xff,
			  (ye + __OFFSET) >> 8, (ye + __OFFSET) & 0xff);
		break;
	case 90:
		write_reg(par, MIPI_DCS_SET_COLUMN_ADDRESS,
			  (xs + __OFFSET) >> 8, (xs + __OFFSET) & 0xff,
			  (xe + __OFFSET) >> 8, (xe + __OFFSET) & 0xff);
		write_reg(par, MIPI_DCS_SET_PAGE_ADDRESS,
			  ys >> 8, ys & 0xff, ye >> 8, ye & 0xff);
		break;
	case 180:
	case 270:
		write_reg(par, MIPI_DCS_SET_COLUMN_ADDRESS,
			  xs >> 8, xs & 0xff, xe >> 8, xe & 0xff);
		write_reg(par, MIPI_DCS_SET_PAGE_ADDRESS,
			  ys >> 8, ys & 0xff, ye >> 8, ye & 0xff);
		break;
	default:
		/* Fix incorrect setting */
		par->info->var.rotate = 0;
	}
	write_reg(par, MIPI_DCS_WRITE_MEMORY_START);
}

/*
 * 7) MY:  1(bottom to top),	0(top to bottom)    Row Address Order
 * 6) MX:  1(R to L),		0(L to R)	    Column Address Order
 * 5) MV:  1(Exchanged),	0(normal)	    Row/Column exchange
 * 4) ML:  1(bottom to top),	0(top to bottom)    Vertical Refresh Order
 * 3) RGB: 1(BGR),		0(RGB)		    Color Space
 * 2) MH:  1(R to L),		0(L to R)	    Horizontal Refresh Order
 * 1)
 * 0)
 *
 *	MY, MX, MV, ML,RGB, MH, D1, D0
 *	0 | 0 | 0 | 0 | 1 | 0 | 0 | 0	//normal
 *	1 | 0 | 0 | 0 | 1 | 0 | 0 | 0	//Y-Mirror
 *	0 | 1 | 0 | 0 | 1 | 0 | 0 | 0	//X-Mirror
 *	1 | 1 | 0 | 0 | 1 | 0 | 0 | 0	//X-Y-Mirror
 *	0 | 0 | 1 | 0 | 1 | 0 | 0 | 0	//X-Y Exchange
 *	1 | 0 | 1 | 0 | 1 | 0 | 0 | 0	//X-Y Exchange, Y-Mirror
 *	0 | 1 | 1 | 0 | 1 | 0 | 0 | 0	//XY exchange
 *	1 | 1 | 1 | 0 | 1 | 0 | 0 | 0
 */
static int set_var(struct fbtft_par *par)
{
	u8 mactrl_data = 0; /* Avoid compiler warning */

	switch (par->info->var.rotate) {
	case 0:
		mactrl_data = 0x08;
		break;
	case 180:
		mactrl_data = 0xC8;
		break;
	case 270:
		mactrl_data = 0xA8;
		break;
	case 90:
		mactrl_data = 0x68;
		break;
	}

	/* Colorspcae */
	if (par->bgr)
		mactrl_data |= BIT(2);
	write_reg(par, MIPI_DCS_SET_ADDRESS_MODE, mactrl_data);
	write_reg(par, MIPI_DCS_WRITE_MEMORY_START);
	return 0;
}

#ifdef GAMMA_ADJ
#define CURVE(num, idx)  curves[(num) * par->gamma.num_values + (idx)]
static int gamma_adj(struct fbtft_par *par, u32 *curves)
{
	static const unsigned long mask[] = {
		0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
		0x1f, 0x3f, 0x0f, 0x0f, 0x7f, 0x1f,
		0x3F, 0x3F, 0x3F, 0x3F, 0x3F};
	int i, j;

	for (i = 0; i < GAMMA_NUM; i++)
		for (j = 0; j < GAMMA_LEN; j++)
			CURVE(i, j) &= mask[i * par->gamma.num_values + j];

	write_reg(par, CMD_PGAMMAC,
		  CURVE(0, 0),
		  CURVE(0, 1),
		  CURVE(0, 2),
		  CURVE(0, 3),
		  CURVE(0, 4),
		  CURVE(0, 5),
		  CURVE(0, 6),
		  (CURVE(0, 7) << 4) | CURVE(0, 8),
		  CURVE(0, 9),
		  CURVE(0, 10),
		  CURVE(0, 11),
		  CURVE(0, 12),
		  CURVE(0, 13),
		  CURVE(0, 14),
		  CURVE(0, 15));

	/* Write Data to GRAM mode */
	write_reg(par, MIPI_DCS_WRITE_MEMORY_START);

	return 0;
}

#undef CURVE
#endif

static struct fbtft_display display = {
	.regwidth = 8,
	.width = WIDTH,
	.height = HEIGHT,
	.bpp = BPP,
	.fps = FPS,
#ifdef GAMMA_ADJ
	.gamma_num = GAMMA_NUM,
	.gamma_len = GAMMA_LEN,
	.gamma = DEFAULT_GAMMA,
#endif
	.fbtftops = {
		.init_display = init_display,
		.set_addr_win = set_addr_win,
		.set_var = set_var,
#ifdef GAMMA_ADJ
		.set_gamma = gamma_adj,
#endif
	},
};

FBTFT_REGISTER_DRIVER(DRVNAME, "ilitek,ili9163", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:ili9163");
MODULE_ALIAS("platform:ili9163");

MODULE_DESCRIPTION("FB driver for the ILI9163 LCD Controller");
MODULE_AUTHOR("Kozhevnikov Anatoly");
MODULE_LICENSE("GPL");
