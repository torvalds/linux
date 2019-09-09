// SPDX-License-Identifier: GPL-2.0+
/*
 * FB driver for the SSD1306 OLED Controller
 *
 * Copyright (C) 2013 Noralf Tronnes
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>

#include "fbtft.h"

#define DRVNAME		"fb_ssd1306"
#define WIDTH		128
#define HEIGHT		64

/*
 * write_reg() caveat:
 *
 * This doesn't work because D/C has to be LOW for both values:
 * write_reg(par, val1, val2);
 *
 * Do it like this:
 * write_reg(par, val1);
 * write_reg(par, val2);
 */

/* Init sequence taken from the Adafruit SSD1306 Arduino library */
static int init_display(struct fbtft_par *par)
{
	par->fbtftops.reset(par);

	if (par->gamma.curves[0] == 0) {
		mutex_lock(&par->gamma.lock);
		if (par->info->var.yres == 64)
			par->gamma.curves[0] = 0xCF;
		else
			par->gamma.curves[0] = 0x8F;
		mutex_unlock(&par->gamma.lock);
	}

	/* Set Display OFF */
	write_reg(par, 0xAE);

	/* Set Display Clock Divide Ratio/ Oscillator Frequency */
	write_reg(par, 0xD5);
	write_reg(par, 0x80);

	/* Set Multiplex Ratio */
	write_reg(par, 0xA8);
	if (par->info->var.yres == 64)
		write_reg(par, 0x3F);
	else if (par->info->var.yres == 48)
		write_reg(par, 0x2F);
	else
		write_reg(par, 0x1F);

	/* Set Display Offset */
	write_reg(par, 0xD3);
	write_reg(par, 0x0);

	/* Set Display Start Line */
	write_reg(par, 0x40 | 0x0);

	/* Charge Pump Setting */
	write_reg(par, 0x8D);
	/* A[2] = 1b, Enable charge pump during display on */
	write_reg(par, 0x14);

	/* Set Memory Addressing Mode */
	write_reg(par, 0x20);
	/* Vertical addressing mode  */
	write_reg(par, 0x01);

	/* Set Segment Re-map */
	/* column address 127 is mapped to SEG0 */
	write_reg(par, 0xA0 | 0x1);

	/* Set COM Output Scan Direction */
	/* remapped mode. Scan from COM[N-1] to COM0 */
	write_reg(par, 0xC8);

	/* Set COM Pins Hardware Configuration */
	write_reg(par, 0xDA);
	if (par->info->var.yres == 64)
		/* A[4]=1b, Alternative COM pin configuration */
		write_reg(par, 0x12);
	else if (par->info->var.yres == 48)
		/* A[4]=1b, Alternative COM pin configuration */
		write_reg(par, 0x12);
	else
		/* A[4]=0b, Sequential COM pin configuration */
		write_reg(par, 0x02);

	/* Set Pre-charge Period */
	write_reg(par, 0xD9);
	write_reg(par, 0xF1);

	/* Set VCOMH Deselect Level */
	write_reg(par, 0xDB);
	/* according to the datasheet, this value is out of bounds */
	write_reg(par, 0x40);

	/* Entire Display ON */
	/* Resume to RAM content display. Output follows RAM content */
	write_reg(par, 0xA4);

	/* Set Normal Display
	 * 0 in RAM: OFF in display panel
	 * 1 in RAM: ON in display panel
	 */
	write_reg(par, 0xA6);

	/* Set Display ON */
	write_reg(par, 0xAF);

	return 0;
}

static void set_addr_win_64x48(struct fbtft_par *par)
{
	/* Set Column Address */
	write_reg(par, 0x21);
	write_reg(par, 0x20);
	write_reg(par, 0x5F);

	/* Set Page Address */
	write_reg(par, 0x22);
	write_reg(par, 0x0);
	write_reg(par, 0x5);
}

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	/* Set Lower Column Start Address for Page Addressing Mode */
	write_reg(par, 0x00 | 0x0);
	/* Set Higher Column Start Address for Page Addressing Mode */
	write_reg(par, 0x10 | 0x0);
	/* Set Display Start Line */
	write_reg(par, 0x40 | 0x0);

	if (par->info->var.xres == 64 && par->info->var.yres == 48)
		set_addr_win_64x48(par);
}

static int blank(struct fbtft_par *par, bool on)
{
	fbtft_par_dbg(DEBUG_BLANK, par, "(%s=%s)\n",
		      __func__, on ? "true" : "false");

	if (on)
		write_reg(par, 0xAE);
	else
		write_reg(par, 0xAF);
	return 0;
}

/* Gamma is used to control Contrast */
static int set_gamma(struct fbtft_par *par, u32 *curves)
{
	/* apply mask */
	curves[0] &= 0xFF;

	/* Set Contrast Control for BANK0 */
	write_reg(par, 0x81);
	write_reg(par, curves[0]);

	return 0;
}

static int write_vmem(struct fbtft_par *par, size_t offset, size_t len)
{
	u16 *vmem16 = (u16 *)par->info->screen_buffer;
	u32 xres = par->info->var.xres;
	u32 yres = par->info->var.yres;
	u8 *buf = par->txbuf.buf;
	int x, y, i;
	int ret = 0;

	for (x = 0; x < xres; x++) {
		for (y = 0; y < yres / 8; y++) {
			*buf = 0x00;
			for (i = 0; i < 8; i++)
				if (vmem16[(y * 8 + i) * xres + x])
					*buf |= BIT(i);
			buf++;
		}
	}

	/* Write data */
	gpiod_set_value(par->gpio.dc, 1);
	ret = par->fbtftops.write(par, par->txbuf.buf, xres * yres / 8);
	if (ret < 0)
		dev_err(par->info->device, "write failed and returned: %d\n",
			ret);

	return ret;
}

static struct fbtft_display display = {
	.regwidth = 8,
	.width = WIDTH,
	.height = HEIGHT,
	.gamma_num = 1,
	.gamma_len = 1,
	.gamma = "00",
	.fbtftops = {
		.write_vmem = write_vmem,
		.init_display = init_display,
		.set_addr_win = set_addr_win,
		.blank = blank,
		.set_gamma = set_gamma,
	},
};

FBTFT_REGISTER_DRIVER(DRVNAME, "solomon,ssd1306", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:ssd1306");
MODULE_ALIAS("platform:ssd1306");

MODULE_DESCRIPTION("SSD1306 OLED Driver");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
