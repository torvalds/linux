// SPDX-License-Identifier: GPL-2.0+
/*
 * FB driver for the SH1106 OLED Controller
 * Based on the SSD1306 driver by Noralf Tronnes
 *
 * Copyright (C) 2017 Heiner Kallweit
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/delay.h>

#include "fbtft.h"

#define DRVNAME		"fb_sh1106"
#define WIDTH		128
#define HEIGHT		64

/* Init sequence based on the Adafruit SSD1306 Arduino library */
static int init_display(struct fbtft_par *par)
{
	if (!par->info->var.xres || par->info->var.xres > WIDTH ||
	    !par->info->var.yres || par->info->var.yres > HEIGHT ||
	    par->info->var.yres % 8) {
		dev_err(par->info->device, "Invalid screen size\n");
		return -EINVAL;
	}

	if (par->info->var.rotate) {
		dev_err(par->info->device, "Display rotation not supported\n");
		return -EINVAL;
	}

	par->fbtftops.reset(par);

	/* Set Display OFF */
	write_reg(par, 0xAE);

	/* Set Display Clock Divide Ratio/ Oscillator Frequency */
	write_reg(par, 0xD5, 0x80);

	/* Set Multiplex Ratio */
	write_reg(par, 0xA8, par->info->var.yres - 1);

	/* Set Display Offset */
	write_reg(par, 0xD3, 0x00);

	/* Set Display Start Line */
	write_reg(par, 0x40 | 0x0);

	/* Set Segment Re-map */
	/* column address 127 is mapped to SEG0 */
	write_reg(par, 0xA0 | 0x1);

	/* Set COM Output Scan Direction */
	/* remapped mode. Scan from COM[N-1] to COM0 */
	write_reg(par, 0xC8);

	/* Set COM Pins Hardware Configuration */
	if (par->info->var.yres == 64)
		/* A[4]=1b, Alternative COM pin configuration */
		write_reg(par, 0xDA, 0x12);
	else if (par->info->var.yres == 48)
		/* A[4]=1b, Alternative COM pin configuration */
		write_reg(par, 0xDA, 0x12);
	else
		/* A[4]=0b, Sequential COM pin configuration */
		write_reg(par, 0xDA, 0x02);

	/* Set Pre-charge Period */
	write_reg(par, 0xD9, 0xF1);

	/* Set VCOMH Deselect Level */
	write_reg(par, 0xDB, 0x40);

	/* Set Display ON */
	write_reg(par, 0xAF);

	msleep(150);

	return 0;
}

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
}

static int blank(struct fbtft_par *par, bool on)
{
	fbtft_par_dbg(DEBUG_BLANK, par, "%s(blank=%s)\n",
		      __func__, on ? "true" : "false");

	write_reg(par, on ? 0xAE : 0xAF);

	return 0;
}

/* Gamma is used to control Contrast */
static int set_gamma(struct fbtft_par *par, u32 *curves)
{
	/* apply mask */
	curves[0] &= 0xFF;

	/* Set Contrast Control for BANK0 */
	write_reg(par, 0x81, curves[0]);

	return 0;
}

static int write_vmem(struct fbtft_par *par, size_t offset, size_t len)
{
	u16 *vmem16 = (u16 *)par->info->screen_buffer;
	u32 xres = par->info->var.xres;
	int page, page_start, page_end, x, i, ret;
	u8 *buf = par->txbuf.buf;

	/* offset refers to vmem with 2 bytes element size */
	page_start = offset / (8 * 2 * xres);
	page_end = DIV_ROUND_UP(offset + len, 8 * 2 * xres);

	for (page = page_start; page < page_end; page++) {
		/* set page and set column to 2 because of vidmem width 132 */
		write_reg(par, 0xb0 | page, 0x00 | 2, 0x10 | 0);

		memset(buf, 0, xres);
		for (x = 0; x < xres; x++)
			for (i = 0; i < 8; i++)
				if (vmem16[(page * 8 + i) * xres + x])
					buf[x] |= BIT(i);

		/* Write data */
		ret = fbtft_write_buf_dc(par, buf, xres, 1);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void write_register(struct fbtft_par *par, int len, ...)
{
	va_list args;
	int i;

	va_start(args, len);

	for (i = 0; i < len; i++)
		par->buf[i] = va_arg(args, unsigned int);

	/* keep DC low for all command bytes to transfer */
	fbtft_write_buf_dc(par, par->buf, len, 0);

	va_end(args);
}

static struct fbtft_display display = {
	.regwidth = 8,
	.width = WIDTH,
	.height = HEIGHT,
	.txbuflen = WIDTH,
	.gamma_num = 1,
	.gamma_len = 1,
	/* set default contrast to 0xcd = 80% */
	.gamma = "cd",
	.fbtftops = {
		.write_vmem = write_vmem,
		.write_register = write_register,
		.init_display = init_display,
		.set_addr_win = set_addr_win,
		.blank = blank,
		.set_gamma = set_gamma,
	},
};

FBTFT_REGISTER_DRIVER(DRVNAME, "sinowealth,sh1106", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:sh1106");
MODULE_ALIAS("platform:sh1106");

MODULE_DESCRIPTION("SH1106 OLED Driver");
MODULE_AUTHOR("Heiner Kallweit");
MODULE_LICENSE("GPL");
