// SPDX-License-Identifier: GPL-2.0+
/*
 * Generic FB driver for TFT LCD displays
 *
 * Copyright (C) 2013 Noralf Tronnes
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/gpio/consumer.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>

#include "fbtft.h"

#define DRVNAME	    "flexfb"

static char *chip;
module_param(chip, charp, 0000);
MODULE_PARM_DESC(chip, "LCD controller");

static unsigned int width;
module_param(width, uint, 0000);
MODULE_PARM_DESC(width, "Display width");

static unsigned int height;
module_param(height, uint, 0000);
MODULE_PARM_DESC(height, "Display height");

static s16 init[512];
static int init_num;
module_param_array(init, short, &init_num, 0000);
MODULE_PARM_DESC(init, "Init sequence");

static unsigned int setaddrwin;
module_param(setaddrwin, uint, 0000);
MODULE_PARM_DESC(setaddrwin, "Which set_addr_win() implementation to use");

static unsigned int buswidth = 8;
module_param(buswidth, uint, 0000);
MODULE_PARM_DESC(buswidth, "Width of databus (default: 8)");

static unsigned int regwidth = 8;
module_param(regwidth, uint, 0000);
MODULE_PARM_DESC(regwidth, "Width of controller register (default: 8)");

static bool nobacklight;
module_param(nobacklight, bool, 0000);
MODULE_PARM_DESC(nobacklight, "Turn off backlight functionality.");

static bool latched;
module_param(latched, bool, 0000);
MODULE_PARM_DESC(latched, "Use with latched 16-bit databus");

static const s16 *initp;
static int initp_num;

/* default init sequences */
static const s16 st7735r_init[] = {
	-1, 0x01,
	-2, 150,
	-1, 0x11,
	-2, 500,
	-1, 0xB1, 0x01, 0x2C, 0x2D,
	-1, 0xB2, 0x01, 0x2C, 0x2D,
	-1, 0xB3, 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D,
	-1, 0xB4, 0x07,
	-1, 0xC0, 0xA2, 0x02, 0x84,
	-1, 0xC1, 0xC5,
	-1, 0xC2, 0x0A, 0x00,
	-1, 0xC3, 0x8A, 0x2A,
	-1, 0xC4, 0x8A, 0xEE,
	-1, 0xC5, 0x0E,
	-1, 0x20,
	-1, 0x36, 0xC0,
	-1, 0x3A, 0x05,
	-1, 0xE0, 0x0f, 0x1a, 0x0f, 0x18, 0x2f, 0x28, 0x20, 0x22,
	    0x1f, 0x1b, 0x23, 0x37, 0x00, 0x07, 0x02, 0x10,
	-1, 0xE1, 0x0f, 0x1b, 0x0f, 0x17, 0x33, 0x2c, 0x29, 0x2e,
	    0x30, 0x30, 0x39, 0x3f, 0x00, 0x07, 0x03, 0x10,
	-1, 0x29,
	-2, 100,
	-1, 0x13,
	-2, 10,
	-3
};

static const s16 ssd1289_init[] = {
	-1, 0x00, 0x0001,
	-1, 0x03, 0xA8A4,
	-1, 0x0C, 0x0000,
	-1, 0x0D, 0x080C,
	-1, 0x0E, 0x2B00,
	-1, 0x1E, 0x00B7,
	-1, 0x01, 0x2B3F,
	-1, 0x02, 0x0600,
	-1, 0x10, 0x0000,
	-1, 0x11, 0x6070,
	-1, 0x05, 0x0000,
	-1, 0x06, 0x0000,
	-1, 0x16, 0xEF1C,
	-1, 0x17, 0x0003,
	-1, 0x07, 0x0233,
	-1, 0x0B, 0x0000,
	-1, 0x0F, 0x0000,
	-1, 0x41, 0x0000,
	-1, 0x42, 0x0000,
	-1, 0x48, 0x0000,
	-1, 0x49, 0x013F,
	-1, 0x4A, 0x0000,
	-1, 0x4B, 0x0000,
	-1, 0x44, 0xEF00,
	-1, 0x45, 0x0000,
	-1, 0x46, 0x013F,
	-1, 0x30, 0x0707,
	-1, 0x31, 0x0204,
	-1, 0x32, 0x0204,
	-1, 0x33, 0x0502,
	-1, 0x34, 0x0507,
	-1, 0x35, 0x0204,
	-1, 0x36, 0x0204,
	-1, 0x37, 0x0502,
	-1, 0x3A, 0x0302,
	-1, 0x3B, 0x0302,
	-1, 0x23, 0x0000,
	-1, 0x24, 0x0000,
	-1, 0x25, 0x8000,
	-1, 0x4f, 0x0000,
	-1, 0x4e, 0x0000,
	-1, 0x22,
	-3
};

static const s16 hx8340bn_init[] = {
	-1, 0xC1, 0xFF, 0x83, 0x40,
	-1, 0x11,
	-2, 150,
	-1, 0xCA, 0x70, 0x00, 0xD9,
	-1, 0xB0, 0x01, 0x11,
	-1, 0xC9, 0x90, 0x49, 0x10, 0x28, 0x28, 0x10, 0x00, 0x06,
	-2, 20,
	-1, 0xC2, 0x60, 0x71, 0x01, 0x0E, 0x05, 0x02, 0x09, 0x31, 0x0A,
	-1, 0xC3, 0x67, 0x30, 0x61, 0x17, 0x48, 0x07, 0x05, 0x33,
	-2, 10,
	-1, 0xB5, 0x35, 0x20, 0x45,
	-1, 0xB4, 0x33, 0x25, 0x4C,
	-2, 10,
	-1, 0x3A, 0x05,
	-1, 0x29,
	-2, 10,
	-3
};

static const s16 ili9225_init[] = {
	-1, 0x0001, 0x011C,
	-1, 0x0002, 0x0100,
	-1, 0x0003, 0x1030,
	-1, 0x0008, 0x0808,
	-1, 0x000C, 0x0000,
	-1, 0x000F, 0x0A01,
	-1, 0x0020, 0x0000,
	-1, 0x0021, 0x0000,
	-2, 50,
	-1, 0x0010, 0x0A00,
	-1, 0x0011, 0x1038,
	-2, 50,
	-1, 0x0012, 0x1121,
	-1, 0x0013, 0x004E,
	-1, 0x0014, 0x676F,
	-1, 0x0030, 0x0000,
	-1, 0x0031, 0x00DB,
	-1, 0x0032, 0x0000,
	-1, 0x0033, 0x0000,
	-1, 0x0034, 0x00DB,
	-1, 0x0035, 0x0000,
	-1, 0x0036, 0x00AF,
	-1, 0x0037, 0x0000,
	-1, 0x0038, 0x00DB,
	-1, 0x0039, 0x0000,
	-1, 0x0050, 0x0000,
	-1, 0x0051, 0x060A,
	-1, 0x0052, 0x0D0A,
	-1, 0x0053, 0x0303,
	-1, 0x0054, 0x0A0D,
	-1, 0x0055, 0x0A06,
	-1, 0x0056, 0x0000,
	-1, 0x0057, 0x0303,
	-1, 0x0058, 0x0000,
	-1, 0x0059, 0x0000,
	-2, 50,
	-1, 0x0007, 0x1017,
	-2, 50,
	-3
};

static const s16 ili9320_init[] = {
	-1, 0x00E5, 0x8000,
	-1, 0x0000, 0x0001,
	-1, 0x0001, 0x0100,
	-1, 0x0002, 0x0700,
	-1, 0x0003, 0x1030,
	-1, 0x0004, 0x0000,
	-1, 0x0008, 0x0202,
	-1, 0x0009, 0x0000,
	-1, 0x000A, 0x0000,
	-1, 0x000C, 0x0000,
	-1, 0x000D, 0x0000,
	-1, 0x000F, 0x0000,
	-1, 0x0010, 0x0000,
	-1, 0x0011, 0x0007,
	-1, 0x0012, 0x0000,
	-1, 0x0013, 0x0000,
	-2, 200,
	-1, 0x0010, 0x17B0,
	-1, 0x0011, 0x0031,
	-2, 50,
	-1, 0x0012, 0x0138,
	-2, 50,
	-1, 0x0013, 0x1800,
	-1, 0x0029, 0x0008,
	-2, 50,
	-1, 0x0020, 0x0000,
	-1, 0x0021, 0x0000,
	-1, 0x0030, 0x0000,
	-1, 0x0031, 0x0505,
	-1, 0x0032, 0x0004,
	-1, 0x0035, 0x0006,
	-1, 0x0036, 0x0707,
	-1, 0x0037, 0x0105,
	-1, 0x0038, 0x0002,
	-1, 0x0039, 0x0707,
	-1, 0x003C, 0x0704,
	-1, 0x003D, 0x0807,
	-1, 0x0050, 0x0000,
	-1, 0x0051, 0x00EF,
	-1, 0x0052, 0x0000,
	-1, 0x0053, 0x013F,
	-1, 0x0060, 0x2700,
	-1, 0x0061, 0x0001,
	-1, 0x006A, 0x0000,
	-1, 0x0080, 0x0000,
	-1, 0x0081, 0x0000,
	-1, 0x0082, 0x0000,
	-1, 0x0083, 0x0000,
	-1, 0x0084, 0x0000,
	-1, 0x0085, 0x0000,
	-1, 0x0090, 0x0010,
	-1, 0x0092, 0x0000,
	-1, 0x0093, 0x0003,
	-1, 0x0095, 0x0110,
	-1, 0x0097, 0x0000,
	-1, 0x0098, 0x0000,
	-1, 0x0007, 0x0173,
	-3
};

static const s16 ili9325_init[] = {
	-1, 0x00E3, 0x3008,
	-1, 0x00E7, 0x0012,
	-1, 0x00EF, 0x1231,
	-1, 0x0001, 0x0100,
	-1, 0x0002, 0x0700,
	-1, 0x0003, 0x1030,
	-1, 0x0004, 0x0000,
	-1, 0x0008, 0x0207,
	-1, 0x0009, 0x0000,
	-1, 0x000A, 0x0000,
	-1, 0x000C, 0x0000,
	-1, 0x000D, 0x0000,
	-1, 0x000F, 0x0000,
	-1, 0x0010, 0x0000,
	-1, 0x0011, 0x0007,
	-1, 0x0012, 0x0000,
	-1, 0x0013, 0x0000,
	-2, 200,
	-1, 0x0010, 0x1690,
	-1, 0x0011, 0x0223,
	-2, 50,
	-1, 0x0012, 0x000D,
	-2, 50,
	-1, 0x0013, 0x1200,
	-1, 0x0029, 0x000A,
	-1, 0x002B, 0x000C,
	-2, 50,
	-1, 0x0020, 0x0000,
	-1, 0x0021, 0x0000,
	-1, 0x0030, 0x0000,
	-1, 0x0031, 0x0506,
	-1, 0x0032, 0x0104,
	-1, 0x0035, 0x0207,
	-1, 0x0036, 0x000F,
	-1, 0x0037, 0x0306,
	-1, 0x0038, 0x0102,
	-1, 0x0039, 0x0707,
	-1, 0x003C, 0x0702,
	-1, 0x003D, 0x1604,
	-1, 0x0050, 0x0000,
	-1, 0x0051, 0x00EF,
	-1, 0x0052, 0x0000,
	-1, 0x0053, 0x013F,
	-1, 0x0060, 0xA700,
	-1, 0x0061, 0x0001,
	-1, 0x006A, 0x0000,
	-1, 0x0080, 0x0000,
	-1, 0x0081, 0x0000,
	-1, 0x0082, 0x0000,
	-1, 0x0083, 0x0000,
	-1, 0x0084, 0x0000,
	-1, 0x0085, 0x0000,
	-1, 0x0090, 0x0010,
	-1, 0x0092, 0x0600,
	-1, 0x0007, 0x0133,
	-3
};

static const s16 ili9341_init[] = {
	-1, 0x28,
	-2, 20,
	-1, 0xCF, 0x00, 0x83, 0x30,
	-1, 0xED, 0x64, 0x03, 0x12, 0x81,
	-1, 0xE8, 0x85, 0x01, 0x79,
	-1, 0xCB, 0x39, 0x2c, 0x00, 0x34, 0x02,
	-1, 0xF7, 0x20,
	-1, 0xEA, 0x00, 0x00,
	-1, 0xC0, 0x26,
	-1, 0xC1, 0x11,
	-1, 0xC5, 0x35, 0x3E,
	-1, 0xC7, 0xBE,
	-1, 0xB1, 0x00, 0x1B,
	-1, 0xB6, 0x0a, 0x82, 0x27, 0x00,
	-1, 0xB7, 0x07,
	-1, 0x3A, 0x55,
	-1, 0x36, 0x48,
	-1, 0x11,
	-2, 120,
	-1, 0x29,
	-2, 20,
	-3
};

static const s16 ssd1351_init[] = {
	-1, 0xfd, 0x12,
	-1, 0xfd, 0xb1,
	-1, 0xae,
	-1, 0xb3, 0xf1,
	-1, 0xca, 0x7f,
	-1, 0xa0, 0x74,
	-1, 0x15, 0x00, 0x7f,
	-1, 0x75, 0x00, 0x7f,
	-1, 0xa1, 0x00,
	-1, 0xa2, 0x00,
	-1, 0xb5, 0x00,
	-1, 0xab, 0x01,
	-1, 0xb1, 0x32,
	-1, 0xb4, 0xa0, 0xb5, 0x55,
	-1, 0xbb, 0x17,
	-1, 0xbe, 0x05,
	-1, 0xc1, 0xc8, 0x80, 0xc8,
	-1, 0xc7, 0x0f,
	-1, 0xb6, 0x01,
	-1, 0xa6,
	-1, 0xaf,
	-3
};

/**
 * struct flexfb_lcd_controller - Describes the LCD controller properties
 * @name: Model name of the chip
 * @width: Width of display in pixels
 * @height: Height of display in pixels
 * @setaddrwin: Which set_addr_win() implementation to use
 * @regwidth: LCD Controller Register width in bits
 * @init_seq: LCD initialization sequence
 * @init_seq_sz: Size of LCD initialization sequence
 */
struct flexfb_lcd_controller {
	const char *name;
	unsigned int width;
	unsigned int height;
	unsigned int setaddrwin;
	unsigned int regwidth;
	const s16 *init_seq;
	int init_seq_sz;
};

static const struct flexfb_lcd_controller flexfb_chip_table[] = {
	{
		.name = "st7735r",
		.width = 120,
		.height = 160,
		.init_seq = st7735r_init,
		.init_seq_sz = ARRAY_SIZE(st7735r_init),
	},
	{
		.name = "hx8340bn",
		.width = 176,
		.height = 220,
		.init_seq = hx8340bn_init,
		.init_seq_sz = ARRAY_SIZE(hx8340bn_init),
	},
	{
		.name = "ili9225",
		.width = 176,
		.height = 220,
		.regwidth = 16,
		.init_seq = ili9225_init,
		.init_seq_sz = ARRAY_SIZE(ili9225_init),
	},
	{
		.name = "ili9320",
		.width = 240,
		.height = 320,
		.setaddrwin = 1,
		.regwidth = 16,
		.init_seq = ili9320_init,
		.init_seq_sz = ARRAY_SIZE(ili9320_init),
	},
	{
		.name = "ili9325",
		.width = 240,
		.height = 320,
		.setaddrwin = 1,
		.regwidth = 16,
		.init_seq = ili9325_init,
		.init_seq_sz = ARRAY_SIZE(ili9325_init),
	},
	{
		.name = "ili9341",
		.width = 240,
		.height = 320,
		.init_seq = ili9341_init,
		.init_seq_sz = ARRAY_SIZE(ili9341_init),
	},
	{
		.name = "ssd1289",
		.width = 240,
		.height = 320,
		.setaddrwin = 2,
		.regwidth = 16,
		.init_seq = ssd1289_init,
		.init_seq_sz = ARRAY_SIZE(ssd1289_init),
	},
	{
		.name = "ssd1351",
		.width = 128,
		.height = 128,
		.setaddrwin = 3,
		.init_seq = ssd1351_init,
		.init_seq_sz = ARRAY_SIZE(ssd1351_init),
	},
};

/* ili9320, ili9325 */
static void flexfb_set_addr_win_1(struct fbtft_par *par,
				  int xs, int ys, int xe, int ye)
{
	switch (par->info->var.rotate) {
	/* R20h = Horizontal GRAM Start Address */
	/* R21h = Vertical GRAM Start Address */
	case 0:
		write_reg(par, 0x0020, xs);
		write_reg(par, 0x0021, ys);
		break;
	case 180:
		write_reg(par, 0x0020, width - 1 - xs);
		write_reg(par, 0x0021, height - 1 - ys);
		break;
	case 270:
		write_reg(par, 0x0020, width - 1 - ys);
		write_reg(par, 0x0021, xs);
		break;
	case 90:
		write_reg(par, 0x0020, ys);
		write_reg(par, 0x0021, height - 1 - xs);
		break;
	}
	write_reg(par, 0x0022); /* Write Data to GRAM */
}

/* ssd1289 */
static void flexfb_set_addr_win_2(struct fbtft_par *par,
				  int xs, int ys, int xe, int ye)
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
	write_reg(par, 0x22, 0);
}

/* ssd1351 */
static void set_addr_win_3(struct fbtft_par *par,
			   int xs, int ys, int xe, int ye)
{
	write_reg(par, 0x15, xs, xe);
	write_reg(par, 0x75, ys, ye);
	write_reg(par, 0x5C);
}

static int flexfb_verify_gpios_dc(struct fbtft_par *par)
{
	fbtft_par_dbg(DEBUG_VERIFY_GPIOS, par, "%s()\n", __func__);

	if (!par->gpio.dc) {
		dev_err(par->info->device,
			"Missing info about 'dc' gpio. Aborting.\n");
		return -EINVAL;
	}

	return 0;
}

static int flexfb_verify_gpios_db(struct fbtft_par *par)
{
	int i;
	int num_db = buswidth;

	fbtft_par_dbg(DEBUG_VERIFY_GPIOS, par, "%s()\n", __func__);

	if (!par->gpio.dc) {
		dev_err(par->info->device, "Missing info about 'dc' gpio. Aborting.\n");
		return -EINVAL;
	}
	if (!par->gpio.wr) {
		dev_err(par->info->device, "Missing info about 'wr' gpio. Aborting.\n");
		return -EINVAL;
	}
	if (latched && !par->gpio.latch) {
		dev_err(par->info->device, "Missing info about 'latch' gpio. Aborting.\n");
		return -EINVAL;
	}
	if (latched)
		num_db = buswidth / 2;
	for (i = 0; i < num_db; i++) {
		if (!par->gpio.db[i]) {
			dev_err(par->info->device,
				"Missing info about 'db%02d' gpio. Aborting.\n",
				i);
			return -EINVAL;
		}
	}

	return 0;
}

static void flexfb_chip_load_param(const struct flexfb_lcd_controller *chip)
{
	if (!width)
		width = chip->width;
	if (!height)
		height = chip->height;
	setaddrwin = chip->setaddrwin;
	if (chip->regwidth)
		regwidth = chip->regwidth;
	if (!init_num) {
		initp = chip->init_seq;
		initp_num = chip->init_seq_sz;
	}
}

static struct fbtft_display flex_display = { };

static int flexfb_chip_init(const struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(flexfb_chip_table); i++)
		if (!strcmp(chip, flexfb_chip_table[i].name)) {
			flexfb_chip_load_param(&flexfb_chip_table[i]);
			return 0;
		}

	dev_err(dev, "chip=%s is not supported\n", chip);

	return -EINVAL;
}

static int flexfb_probe_common(struct spi_device *sdev,
			       struct platform_device *pdev)
{
	struct device *dev;
	struct fb_info *info;
	struct fbtft_par *par;
	int ret;

	initp = init;
	initp_num = init_num;

	if (sdev)
		dev = &sdev->dev;
	else
		dev = &pdev->dev;

	fbtft_init_dbg(dev, "%s(%s)\n", __func__,
		       sdev ? "'SPI device'" : "'Platform device'");

	if (chip) {
		ret = flexfb_chip_init(dev);
		if (ret)
			return ret;
	}

	if (width == 0 || height == 0) {
		dev_err(dev, "argument(s) missing: width and height has to be set.\n");
		return -EINVAL;
	}
	flex_display.width = width;
	flex_display.height = height;
	fbtft_init_dbg(dev, "Display resolution: %dx%d\n", width, height);
	fbtft_init_dbg(dev, "chip = %s\n", chip ? chip : "not set");
	fbtft_init_dbg(dev, "setaddrwin = %d\n", setaddrwin);
	fbtft_init_dbg(dev, "regwidth = %d\n", regwidth);
	fbtft_init_dbg(dev, "buswidth = %d\n", buswidth);

	info = fbtft_framebuffer_alloc(&flex_display, dev, dev->platform_data);
	if (!info)
		return -ENOMEM;

	par = info->par;
	if (sdev)
		par->spi = sdev;
	else
		par->pdev = pdev;
	if (!par->init_sequence)
		par->init_sequence = initp;
	par->fbtftops.init_display = fbtft_init_display;

	/* registerwrite functions */
	switch (regwidth) {
	case 8:
		par->fbtftops.write_register = fbtft_write_reg8_bus8;
		break;
	case 16:
		par->fbtftops.write_register = fbtft_write_reg16_bus8;
		break;
	default:
		dev_err(dev,
			"argument 'regwidth': %d is not supported.\n",
			regwidth);
		return -EINVAL;
	}

	/* bus functions */
	if (sdev) {
		par->fbtftops.write = fbtft_write_spi;
		switch (buswidth) {
		case 8:
			par->fbtftops.write_vmem = fbtft_write_vmem16_bus8;
			if (!par->startbyte)
				par->fbtftops.verify_gpios = flexfb_verify_gpios_dc;
			break;
		case 9:
			if (regwidth == 16) {
				dev_err(dev, "argument 'regwidth': %d is not supported with buswidth=%d and SPI.\n",
					regwidth, buswidth);
				return -EINVAL;
			}
			par->fbtftops.write_register = fbtft_write_reg8_bus9;
			par->fbtftops.write_vmem = fbtft_write_vmem16_bus9;
			if (par->spi->master->bits_per_word_mask
			    & SPI_BPW_MASK(9)) {
				par->spi->bits_per_word = 9;
				break;
			}

			dev_warn(dev,
				 "9-bit SPI not available, emulating using 8-bit.\n");
			/* allocate buffer with room for dc bits */
			par->extra = devm_kzalloc(par->info->device,
						  par->txbuf.len
						  + (par->txbuf.len / 8) + 8,
						  GFP_KERNEL);
			if (!par->extra) {
				ret = -ENOMEM;
				goto out_release;
			}
			par->fbtftops.write = fbtft_write_spi_emulate_9;

			break;
		default:
			dev_err(dev,
				"argument 'buswidth': %d is not supported with SPI.\n",
				buswidth);
			return -EINVAL;
		}
	} else {
		par->fbtftops.verify_gpios = flexfb_verify_gpios_db;
		switch (buswidth) {
		case 8:
			par->fbtftops.write = fbtft_write_gpio8_wr;
			par->fbtftops.write_vmem = fbtft_write_vmem16_bus8;
			break;
		case 16:
			par->fbtftops.write_register = fbtft_write_reg16_bus16;
			if (latched)
				par->fbtftops.write = fbtft_write_gpio16_wr_latched;
			else
				par->fbtftops.write = fbtft_write_gpio16_wr;
			par->fbtftops.write_vmem = fbtft_write_vmem16_bus16;
			break;
		default:
			dev_err(dev,
				"argument 'buswidth': %d is not supported with parallel.\n",
				buswidth);
			return -EINVAL;
		}
	}

	/* set_addr_win function */
	switch (setaddrwin) {
	case 0:
		/* use default */
		break;
	case 1:
		par->fbtftops.set_addr_win = flexfb_set_addr_win_1;
		break;
	case 2:
		par->fbtftops.set_addr_win = flexfb_set_addr_win_2;
		break;
	case 3:
		par->fbtftops.set_addr_win = set_addr_win_3;
		break;
	default:
		dev_err(dev, "argument 'setaddrwin': unknown value %d.\n",
			setaddrwin);
		return -EINVAL;
	}

	if (!nobacklight)
		par->fbtftops.register_backlight = fbtft_register_backlight;

	ret = fbtft_register_framebuffer(info);
	if (ret < 0)
		goto out_release;

	return 0;

out_release:
	fbtft_framebuffer_release(info);

	return ret;
}

static int flexfb_remove_common(struct device *dev, struct fb_info *info)
{
	struct fbtft_par *par;

	if (!info)
		return -EINVAL;
	par = info->par;
	if (par)
		fbtft_par_dbg(DEBUG_DRIVER_INIT_FUNCTIONS, par, "%s()\n",
			      __func__);
	fbtft_unregister_framebuffer(info);
	fbtft_framebuffer_release(info);

	return 0;
}

static int flexfb_probe_spi(struct spi_device *spi)
{
	return flexfb_probe_common(spi, NULL);
}

static int flexfb_remove_spi(struct spi_device *spi)
{
	struct fb_info *info = spi_get_drvdata(spi);

	return flexfb_remove_common(&spi->dev, info);
}

static int flexfb_probe_pdev(struct platform_device *pdev)
{
	return flexfb_probe_common(NULL, pdev);
}

static int flexfb_remove_pdev(struct platform_device *pdev)
{
	struct fb_info *info = platform_get_drvdata(pdev);

	return flexfb_remove_common(&pdev->dev, info);
}

static struct spi_driver flexfb_spi_driver = {
	.driver = {
		.name   = DRVNAME,
	},
	.probe  = flexfb_probe_spi,
	.remove = flexfb_remove_spi,
};

static const struct platform_device_id flexfb_platform_ids[] = {
	{ "flexpfb", 0 },
	{ },
};
MODULE_DEVICE_TABLE(platform, flexfb_platform_ids);

static struct platform_driver flexfb_platform_driver = {
	.driver = {
		.name   = DRVNAME,
	},
	.id_table = flexfb_platform_ids,
	.probe  = flexfb_probe_pdev,
	.remove = flexfb_remove_pdev,
};

static int __init flexfb_init(void)
{
	int ret, ret2;

	ret = spi_register_driver(&flexfb_spi_driver);
	ret2 = platform_driver_register(&flexfb_platform_driver);
	if (ret < 0)
		return ret;
	return ret2;
}

static void __exit flexfb_exit(void)
{
	spi_unregister_driver(&flexfb_spi_driver);
	platform_driver_unregister(&flexfb_platform_driver);
}

/* ------------------------------------------------------------------------- */

module_init(flexfb_init);
module_exit(flexfb_exit);

MODULE_DESCRIPTION("Generic FB driver for TFT LCD displays");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
