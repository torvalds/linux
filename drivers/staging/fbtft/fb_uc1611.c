/*
 * FB driver for the UltraChip UC1611 LCD controller
 *
 * The display is 4-bit grayscale (16 shades) 240x160.
 *
 * Copyright (C) 2015 Henri Chain
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

#define DRVNAME		"fb_uc1611"
#define WIDTH		240
#define HEIGHT		160
#define BPP		8
#define FPS		40

/*
 * LCD voltage is a combination of ratio, gain, pot and temp
 *
 * V_LCD = V_BIAS * ratio
 * V_LCD = (C_V0 + C_PM × pot) * (1 + (T - 25) * temp)
 * C_V0 and C_PM depend on ratio and gain
 * T is ambient temperature
 */

/* BR -> actual ratio: 0-3 -> 5, 10, 11, 13 */
static unsigned int ratio = 2;
module_param(ratio, uint, 0000);
MODULE_PARM_DESC(ratio, "BR[1:0] Bias voltage ratio: 0-3 (default: 2)");

static unsigned int gain = 3;
module_param(gain, uint, 0000);
MODULE_PARM_DESC(gain, "GN[1:0] Bias voltage gain: 0-3 (default: 3)");

static unsigned int pot = 16;
module_param(pot, uint, 0000);
MODULE_PARM_DESC(pot, "PM[6:0] Bias voltage pot.: 0-63 (default: 16)");

/* TC -> % compensation per deg C: 0-3 -> -.05, -.10, -.015, -.20 */
static unsigned int temp;
module_param(temp, uint, 0000);
MODULE_PARM_DESC(temp, "TC[1:0] Temperature compensation: 0-3 (default: 0)");

/* PC[1:0] -> LCD capacitance: 0-3 -> <20nF, 20-28 nF, 29-40 nF, 40-56 nF */
static unsigned int load = 1;
module_param(load, uint, 0000);
MODULE_PARM_DESC(load, "PC[1:0] Panel Loading: 0-3 (default: 1)");

/* PC[3:2] -> V_LCD: 0, 1, 3 -> ext., int. with ratio = 5, int. standard */
static unsigned int pump = 3;
module_param(pump, uint, 0000);
MODULE_PARM_DESC(pump, "PC[3:2] Pump control: 0,1,3 (default: 3)");

static int init_display(struct fbtft_par *par)
{
	int ret;

	/* Set CS active high */
	par->spi->mode |= SPI_CS_HIGH;
	ret = spi_setup(par->spi);
	if (ret) {
		dev_err(par->info->device, "Could not set SPI_CS_HIGH\n");
		return ret;
	}

	/* Reset controller */
	write_reg(par, 0xE2);

	/* Set bias ratio */
	write_reg(par, 0xE8 | (ratio & 0x03));

	/* Set bias gain and potentiometer */
	write_reg(par, 0x81);
	write_reg(par, (gain & 0x03) << 6 | (pot & 0x3F));

	/* Set temperature compensation */
	write_reg(par, 0x24 | (temp & 0x03));

	/* Set panel loading */
	write_reg(par, 0x28 | (load & 0x03));

	/* Set pump control */
	write_reg(par, 0x2C | (pump & 0x03));

	/* Set inverse display */
	write_reg(par, 0xA6 | (0x01 & 0x01));

	/* Set 4-bit grayscale mode */
	write_reg(par, 0xD0 | (0x02 & 0x03));

	/* Set Display enable */
	write_reg(par, 0xA8 | 0x07);

	return 0;
}

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	switch (par->info->var.rotate) {
	case 90:
	case 270:
		/* Set column address */
		write_reg(par, ys & 0x0F);
		write_reg(par, 0x10 | (ys >> 4));

		/* Set page address (divide xs by 2) (not used by driver) */
		write_reg(par, 0x60 | ((xs >> 1) & 0x0F));
		write_reg(par, 0x70 | (xs >> 5));
		break;
	default:
		/* Set column address (not used by driver) */
		write_reg(par, xs & 0x0F);
		write_reg(par, 0x10 | (xs >> 4));

		/* Set page address (divide ys by 2) */
		write_reg(par, 0x60 | ((ys >> 1) & 0x0F));
		write_reg(par, 0x70 | (ys >> 5));
		break;
	}
}

static int blank(struct fbtft_par *par, bool on)
{
	fbtft_par_dbg(DEBUG_BLANK, par, "%s(blank=%s)\n",
		      __func__, on ? "true" : "false");

	if (on)
		write_reg(par, 0xA8 | 0x00);
	else
		write_reg(par, 0xA8 | 0x07);
	return 0;
}

static int set_var(struct fbtft_par *par)
{
	/* par->info->fix.visual = FB_VISUAL_PSEUDOCOLOR; */
	par->info->var.grayscale = 1;
	par->info->var.red.offset    = 0;
	par->info->var.red.length    = 8;
	par->info->var.green.offset  = 0;
	par->info->var.green.length  = 8;
	par->info->var.blue.offset   = 0;
	par->info->var.blue.length   = 8;
	par->info->var.transp.offset = 0;
	par->info->var.transp.length = 0;

	switch (par->info->var.rotate) {
	case 90:
		/* Set RAM address control */
		write_reg(par, 0x88
			| (0x0 & 0x1) << 2 /* Increment positively */
			| (0x1 & 0x1) << 1 /* Increment page first */
			| (0x1 & 0x1));    /* Wrap around (default) */

		/* Set LCD mapping */
		write_reg(par, 0xC0
			| (0x0 & 0x1) << 2 /* Mirror Y OFF */
			| (0x0 & 0x1) << 1 /* Mirror X OFF */
			| (0x0 & 0x1));    /* MS nibble last (default) */
		break;
	case 180:
		/* Set RAM address control */
		write_reg(par, 0x88
			| (0x0 & 0x1) << 2 /* Increment positively */
			| (0x0 & 0x1) << 1 /* Increment column first */
			| (0x1 & 0x1));    /* Wrap around (default) */

		/* Set LCD mapping */
		write_reg(par, 0xC0
			| (0x1 & 0x1) << 2 /* Mirror Y ON */
			| (0x0 & 0x1) << 1 /* Mirror X OFF */
			| (0x0 & 0x1));    /* MS nibble last (default) */
		break;
	case 270:
		/* Set RAM address control */
		write_reg(par, 0x88
			| (0x0 & 0x1) << 2 /* Increment positively */
			| (0x1 & 0x1) << 1 /* Increment page first */
			| (0x1 & 0x1));    /* Wrap around (default) */

		/* Set LCD mapping */
		write_reg(par, 0xC0
			| (0x1 & 0x1) << 2 /* Mirror Y ON */
			| (0x1 & 0x1) << 1 /* Mirror X ON */
			| (0x0 & 0x1));    /* MS nibble last (default) */
		break;
	default:
		/* Set RAM address control */
		write_reg(par, 0x88
			| (0x0 & 0x1) << 2 /* Increment positively */
			| (0x0 & 0x1) << 1 /* Increment column first */
			| (0x1 & 0x1));    /* Wrap around (default) */

		/* Set LCD mapping */
		write_reg(par, 0xC0
			| (0x0 & 0x1) << 2 /* Mirror Y OFF */
			| (0x1 & 0x1) << 1 /* Mirror X ON */
			| (0x0 & 0x1));    /* MS nibble last (default) */
		break;
	}

	return 0;
}

static int write_vmem(struct fbtft_par *par, size_t offset, size_t len)
{
	u8 *vmem8 = (u8 *)(par->info->screen_buffer);
	u8 *buf8 = par->txbuf.buf;
	u16 *buf16 = par->txbuf.buf;
	int line_length = par->info->fix.line_length;
	int y_start = offset / line_length;
	int y_end = (offset + len - 1) / line_length;
	int x, y, i;
	int ret = 0;

	switch (par->pdata->display.buswidth) {
	case 8:
		switch (par->info->var.rotate) {
		case 90:
		case 270:
			i = y_start * line_length;
			for (y = y_start; y <= y_end; y++) {
				for (x = 0; x < line_length; x += 2) {
					*buf8 = vmem8[i] >> 4;
					*buf8 |= vmem8[i + 1] & 0xF0;
					buf8++;
					i += 2;
				}
			}
			break;
		default:
			/* Must be even because pages are two lines */
			y_start &= 0xFE;
			i = y_start * line_length;
			for (y = y_start; y <= y_end; y += 2) {
				for (x = 0; x < line_length; x++) {
					*buf8 = vmem8[i] >> 4;
					*buf8 |= vmem8[i + line_length] & 0xF0;
					buf8++;
					i++;
				}
				i += line_length;
			}
			break;
		}
		gpio_set_value(par->gpio.dc, 1);

		/* Write data */
		ret = par->fbtftops.write(par, par->txbuf.buf, len / 2);
		break;
	case 9:
		switch (par->info->var.rotate) {
		case 90:
		case 270:
			i = y_start * line_length;
			for (y = y_start; y <= y_end; y++) {
				for (x = 0; x < line_length; x += 2) {
					*buf16 = 0x100;
					*buf16 |= vmem8[i] >> 4;
					*buf16 |= vmem8[i + 1] & 0xF0;
					buf16++;
					i += 2;
				}
			}
			break;
		default:
			/* Must be even because pages are two lines */
			y_start &= 0xFE;
			i = y_start * line_length;
			for (y = y_start; y <= y_end; y += 2) {
				for (x = 0; x < line_length; x++) {
					*buf16 = 0x100;
					*buf16 |= vmem8[i] >> 4;
					*buf16 |= vmem8[i + line_length] & 0xF0;
					buf16++;
					i++;
				}
				i += line_length;
			}
			break;
		}

		/* Write data */
		ret = par->fbtftops.write(par, par->txbuf.buf, len);
		break;
	default:
		dev_err(par->info->device, "unsupported buswidth %d\n",
			par->pdata->display.buswidth);
	}

	if (ret < 0)
		dev_err(par->info->device, "write failed and returned: %d\n",
			ret);

	return ret;
}

static struct fbtft_display display = {
	.txbuflen = -1,
	.regwidth = 8,
	.width = WIDTH,
	.height = HEIGHT,
	.bpp = BPP,
	.fps = FPS,
	.fbtftops = {
		.write_vmem = write_vmem,
		.init_display = init_display,
		.set_addr_win = set_addr_win,
		.set_var = set_var,
		.blank = blank,
	},
};

FBTFT_REGISTER_DRIVER(DRVNAME, "ultrachip,uc1611", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:uc1611");
MODULE_ALIAS("platform:uc1611");

MODULE_DESCRIPTION("FB driver for the UC1611 LCD controller");
MODULE_AUTHOR("Henri Chain");
MODULE_LICENSE("GPL");
