// SPDX-License-Identifier: GPL-2.0+
/*
 * FB driver for Two KS0108 LCD controllers in AGM1264K-FL display
 *
 * Copyright (C) 2014 ololoshka2871
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include "fbtft.h"

/* Uncomment text line to use negative image on display */
/*#define NEGATIVE*/

#define WHITE		0xff
#define BLACK		0

#define DRVNAME		"fb_agm1264k-fl"
#define WIDTH		64
#define HEIGHT		64
#define TOTALWIDTH	(WIDTH * 2)	 /* because 2 x ks0108 in one display */
#define FPS			20

#define EPIN		gpio.wr
#define RS			gpio.dc
#define RW			gpio.aux[2]
#define CS0			gpio.aux[0]
#define CS1			gpio.aux[1]

/* diffusing error (Floyd-Steinberg) */
#define DIFFUSING_MATRIX_WIDTH	2
#define DIFFUSING_MATRIX_HEIGHT	2

static const signed char
diffusing_matrix[DIFFUSING_MATRIX_WIDTH][DIFFUSING_MATRIX_HEIGHT] = {
	{-1, 3},
	{3, 2},
};

static const unsigned char gamma_correction_table[] = {
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
1, 1, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6,
6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 11, 11, 11, 12, 12, 13,
13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21,
22, 22, 23, 23, 24, 25, 25, 26, 26, 27, 28, 28, 29, 30, 30, 31, 32,
33, 33, 34, 35, 35, 36, 37, 38, 39, 39, 40, 41, 42, 43, 43, 44, 45,
46, 47, 48, 49, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 73, 74, 75, 76, 77, 78, 79, 81,
82, 83, 84, 85, 87, 88, 89, 90, 91, 93, 94, 95, 97, 98, 99, 100, 102,
103, 105, 106, 107, 109, 110, 111, 113, 114, 116, 117, 119, 120, 121,
123, 124, 126, 127, 129, 130, 132, 133, 135, 137, 138, 140, 141, 143,
145, 146, 148, 149, 151, 153, 154, 156, 158, 159, 161, 163, 165, 166,
168, 170, 172, 173, 175, 177, 179, 181, 182, 184, 186, 188, 190, 192,
194, 196, 197, 199, 201, 203, 205, 207, 209, 211, 213, 215, 217, 219,
221, 223, 225, 227, 229, 231, 234, 236, 238, 240, 242, 244, 246, 248,
251, 253, 255
};

static int init_display(struct fbtft_par *par)
{
	u8 i;

	par->fbtftops.reset(par);

	for (i = 0; i < 2; ++i) {
		write_reg(par, i, 0x3f); /* display on */
		write_reg(par, i, 0x40); /* set x to 0 */
		write_reg(par, i, 0xb0); /* set page to 0 */
		write_reg(par, i, 0xc0); /* set start line to 0 */
	}

	return 0;
}

static void reset(struct fbtft_par *par)
{
	if (!par->gpio.reset)
		return;

	dev_dbg(par->info->device, "%s()\n", __func__);

	gpiod_set_value(par->gpio.reset, 0);
	udelay(20);
	gpiod_set_value(par->gpio.reset, 1);
	mdelay(120);
}

/* Check if all necessary GPIOS defined */
static int verify_gpios(struct fbtft_par *par)
{
	int i;

	dev_dbg(par->info->device,
		"%s()\n", __func__);

	if (!par->EPIN) {
		dev_err(par->info->device,
			"Missing info about 'wr' (aka E) gpio. Aborting.\n");
		return -EINVAL;
	}
	for (i = 0; i < 8; ++i) {
		if (!par->gpio.db[i]) {
			dev_err(par->info->device,
				"Missing info about 'db[%i]' gpio. Aborting.\n",
				i);
			return -EINVAL;
		}
	}
	if (!par->CS0) {
		dev_err(par->info->device,
			"Missing info about 'cs0' gpio. Aborting.\n");
		return -EINVAL;
	}
	if (!par->CS1) {
		dev_err(par->info->device,
			"Missing info about 'cs1' gpio. Aborting.\n");
		return -EINVAL;
	}
	if (!par->RW) {
		dev_err(par->info->device,
			"Missing info about 'rw' gpio. Aborting.\n");
		return -EINVAL;
	}

	return 0;
}

static unsigned long
request_gpios_match(struct fbtft_par *par, const struct fbtft_gpio *gpio)
{
	dev_dbg(par->info->device,
		"%s('%s')\n", __func__, gpio->name);

	if (strcasecmp(gpio->name, "wr") == 0) {
		/* left ks0108 E pin */
		par->EPIN = gpio->gpio;
		return GPIOD_OUT_LOW;
	} else if (strcasecmp(gpio->name, "cs0") == 0) {
		/* left ks0108 controller pin */
		par->CS0 = gpio->gpio;
		return GPIOD_OUT_HIGH;
	} else if (strcasecmp(gpio->name, "cs1") == 0) {
		/* right ks0108 controller pin */
		par->CS1 = gpio->gpio;
		return GPIOD_OUT_HIGH;
	}

	/* if write (rw = 0) e(1->0) perform write */
	/* if read (rw = 1) e(0->1) set data on D0-7*/
	else if (strcasecmp(gpio->name, "rw") == 0) {
		par->RW = gpio->gpio;
		return GPIOD_OUT_LOW;
	}

	return FBTFT_GPIO_NO_MATCH;
}

/* This function oses to enter commands
 * first byte - destination controller 0 or 1
 * following - commands
 */
static void write_reg8_bus8(struct fbtft_par *par, int len, ...)
{
	va_list args;
	int i, ret;
	u8 *buf = par->buf;

	if (unlikely(par->debug & DEBUG_WRITE_REGISTER)) {
		va_start(args, len);
		for (i = 0; i < len; i++)
			buf[i] = (u8)va_arg(args, unsigned int);

		va_end(args);
		fbtft_par_dbg_hex(DEBUG_WRITE_REGISTER, par, par->info->device,
				  u8, buf, len, "%s: ", __func__);
}

	va_start(args, len);

	*buf = (u8)va_arg(args, unsigned int);

	if (*buf > 1) {
		va_end(args);
		dev_err(par->info->device,
			"Incorrect chip select request (%d)\n", *buf);
		return;
	}

	/* select chip */
	if (*buf) {
		/* cs1 */
		gpiod_set_value(par->CS0, 1);
		gpiod_set_value(par->CS1, 0);
	} else {
		/* cs0 */
		gpiod_set_value(par->CS0, 0);
		gpiod_set_value(par->CS1, 1);
	}

	gpiod_set_value(par->RS, 0); /* RS->0 (command mode) */
	len--;

	if (len) {
		i = len;
		while (i--)
			*buf++ = (u8)va_arg(args, unsigned int);
		ret = par->fbtftops.write(par, par->buf, len * (sizeof(u8)));
		if (ret < 0) {
			va_end(args);
			dev_err(par->info->device,
				"write() failed and returned %d\n", ret);
			return;
		}
	}

	va_end(args);
}

static struct
{
	int xs, ys_page, xe, ye_page;
} addr_win;

/* save display writing zone */
static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	addr_win.xs = xs;
	addr_win.ys_page = ys / 8;
	addr_win.xe = xe;
	addr_win.ye_page = ye / 8;
}

static void
construct_line_bitmap(struct fbtft_par *par, u8 *dest, signed short *src,
		      int xs, int xe, int y)
{
	int x, i;

	for (x = xs; x < xe; ++x) {
		u8 res = 0;

		for (i = 0; i < 8; i++)
			if (src[(y * 8 + i) * par->info->var.xres + x])
				res |= 1 << i;
#ifdef NEGATIVE
		*dest++ = res;
#else
		*dest++ = ~res;
#endif
	}
}

static void iterate_diffusion_matrix(u32 xres, u32 yres, int x,
				     int y, signed short *convert_buf,
				     signed short pixel, signed short error)
{
	u16 i, j;

	/* diffusion matrix row */
	for (i = 0; i < DIFFUSING_MATRIX_WIDTH; ++i)
		/* diffusion matrix column */
		for (j = 0; j < DIFFUSING_MATRIX_HEIGHT; ++j) {
			signed short *write_pos;
			signed char coeff;

			/* skip pixels out of zone */
			if (x + i < 0 || x + i >= xres || y + j >= yres)
				continue;
			write_pos = &convert_buf[(y + j) * xres + x + i];
			coeff = diffusing_matrix[i][j];
			if (-1 == coeff) {
				/* pixel itself */
				*write_pos = pixel;
			} else {
				signed short p = *write_pos + error * coeff;

				if (p > WHITE)
					p = WHITE;
				if (p < BLACK)
					p = BLACK;
				*write_pos = p;
			}
		}
}

static int write_vmem(struct fbtft_par *par, size_t offset, size_t len)
{
	u16 *vmem16 = (u16 *)par->info->screen_buffer;
	u8 *buf = par->txbuf.buf;
	int x, y;
	int ret = 0;

	/* buffer to convert RGB565 -> grayscale16 -> Dithered image 1bpp */
	signed short *convert_buf = kmalloc_array(par->info->var.xres *
		par->info->var.yres, sizeof(signed short), GFP_NOIO);

	if (!convert_buf)
		return -ENOMEM;

	/* converting to grayscale16 */
	for (x = 0; x < par->info->var.xres; ++x)
		for (y = 0; y < par->info->var.yres; ++y) {
			u16 pixel = vmem16[y *  par->info->var.xres + x];
			u16 b = pixel & 0x1f;
			u16 g = (pixel & (0x3f << 5)) >> 5;
			u16 r = (pixel & (0x1f << (5 + 6))) >> (5 + 6);

			pixel = (299 * r + 587 * g + 114 * b) / 200;
			if (pixel > 255)
				pixel = 255;

			/* gamma-correction by table */
			convert_buf[y *  par->info->var.xres + x] =
				(signed short)gamma_correction_table[pixel];
		}

	/* Image Dithering */
	for (x = 0; x < par->info->var.xres; ++x)
		for (y = 0; y < par->info->var.yres; ++y) {
			signed short pixel =
				convert_buf[y *  par->info->var.xres + x];
			signed short error_b = pixel - BLACK;
			signed short error_w = pixel - WHITE;
			signed short error;

			/* what color close? */
			if (abs(error_b) >= abs(error_w)) {
				/* white */
				error = error_w;
				pixel = 0xff;
			} else {
				/* black */
				error = error_b;
				pixel = 0;
			}

			error /= 8;

			iterate_diffusion_matrix(par->info->var.xres,
						 par->info->var.yres,
						 x, y, convert_buf,
						 pixel, error);
		}

	/* 1 string = 2 pages */
	for (y = addr_win.ys_page; y <= addr_win.ye_page; ++y) {
		/* left half of display */
		if (addr_win.xs < par->info->var.xres / 2) {
			construct_line_bitmap(par, buf, convert_buf,
					      addr_win.xs,
					      par->info->var.xres / 2, y);

			len = par->info->var.xres / 2 - addr_win.xs;

			/* select left side (sc0)
			 * set addr
			 */
			write_reg(par, 0x00, BIT(6) | (u8)addr_win.xs);
			write_reg(par, 0x00, (0x17 << 3) | (u8)y);

			/* write bitmap */
			gpiod_set_value(par->RS, 1); /* RS->1 (data mode) */
			ret = par->fbtftops.write(par, buf, len);
			if (ret < 0)
				dev_err(par->info->device,
					"write failed and returned: %d\n",
					ret);
		}
		/* right half of display */
		if (addr_win.xe >= par->info->var.xres / 2) {
			construct_line_bitmap(par, buf,
					      convert_buf,
					      par->info->var.xres / 2,
					      addr_win.xe + 1, y);

			len = addr_win.xe + 1 - par->info->var.xres / 2;

			/* select right side (sc1)
			 * set addr
			 */
			write_reg(par, 0x01, 1 << 6);
			write_reg(par, 0x01, (0x17 << 3) | (u8)y);

			/* write bitmap */
			gpiod_set_value(par->RS, 1); /* RS->1 (data mode) */
			par->fbtftops.write(par, buf, len);
			if (ret < 0)
				dev_err(par->info->device,
					"write failed and returned: %d\n",
					ret);
		}
	}
	kfree(convert_buf);

	gpiod_set_value(par->CS0, 1);
	gpiod_set_value(par->CS1, 1);

	return ret;
}

static int write(struct fbtft_par *par, void *buf, size_t len)
{
	fbtft_par_dbg_hex(DEBUG_WRITE, par, par->info->device, u8, buf, len,
			  "%s(len=%d): ", __func__, len);

	gpiod_set_value(par->RW, 0); /* set write mode */

	while (len--) {
		u8 i, data;

		data = *(u8 *)buf++;

		/* set data bus */
		for (i = 0; i < 8; ++i)
			gpiod_set_value(par->gpio.db[i], data & (1 << i));
		/* set E */
		gpiod_set_value(par->EPIN, 1);
		udelay(5);
		/* unset E - write */
		gpiod_set_value(par->EPIN, 0);
		udelay(1);
	}

	return 0;
}

static struct fbtft_display display = {
	.regwidth = 8,
	.width = TOTALWIDTH,
	.height = HEIGHT,
	.fps = FPS,
	.fbtftops = {
		.init_display = init_display,
		.set_addr_win = set_addr_win,
		.verify_gpios = verify_gpios,
		.request_gpios_match = request_gpios_match,
		.reset = reset,
		.write = write,
		.write_register = write_reg8_bus8,
		.write_vmem = write_vmem,
	},
};

FBTFT_REGISTER_DRIVER(DRVNAME, "displaytronic,fb_agm1264k-fl", &display);

MODULE_ALIAS("platform:" DRVNAME);

MODULE_DESCRIPTION("Two KS0108 LCD controllers in AGM1264K-FL display");
MODULE_AUTHOR("ololoshka2871");
MODULE_LICENSE("GPL");
