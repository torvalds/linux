/*
 * Custom FB driver for tinylcd.com display
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>

#include "fbtft.h"

#define DRVNAME		"fb_tinylcd"
#define WIDTH		320
#define HEIGHT		480


static int init_display(struct fbtft_par *par)
{
	fbtft_par_dbg(DEBUG_INIT_DISPLAY, par, "%s()\n", __func__);

	par->fbtftops.reset(par);

	write_reg(par, 0xB0, 0x80);
	write_reg(par, 0xC0, 0x0A, 0x0A);
	write_reg(par, 0xC1, 0x45, 0x07);
	write_reg(par, 0xC2, 0x33);
	write_reg(par, 0xC5, 0x00, 0x42, 0x80);
	write_reg(par, 0xB1, 0xD0, 0x11);
	write_reg(par, 0xB4, 0x02);
	write_reg(par, 0xB6, 0x00, 0x22, 0x3B);
	write_reg(par, 0xB7, 0x07);
	write_reg(par, 0x36, 0x58);
	write_reg(par, 0xF0, 0x36, 0xA5, 0xD3);
	write_reg(par, 0xE5, 0x80);
	write_reg(par, 0xE5, 0x01);
	write_reg(par, 0xB3, 0x00);
	write_reg(par, 0xE5, 0x00);
	write_reg(par, 0xF0, 0x36, 0xA5, 0x53);
	write_reg(par, 0xE0, 0x00, 0x35, 0x33, 0x00, 0x00, 0x00,
			     0x00, 0x35, 0x33, 0x00, 0x00, 0x00);
	write_reg(par, 0x3A, 0x55);
	write_reg(par, 0x11);
	udelay(250);
	write_reg(par, 0x29);

	return 0;
}

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	fbtft_par_dbg(DEBUG_SET_ADDR_WIN, par,
		"%s(xs=%d, ys=%d, xe=%d, ye=%d)\n", __func__, xs, ys, xe, ye);

	/* Column address */
	write_reg(par, 0x2A, xs >> 8, xs & 0xFF, xe >> 8, xe & 0xFF);

	/* Row address */
	write_reg(par, 0x2B, ys >> 8, ys & 0xFF, ye >> 8, ye & 0xFF);

	/* Memory write */
	write_reg(par, 0x2C);
}

static int set_var(struct fbtft_par *par)
{
	fbtft_par_dbg(DEBUG_INIT_DISPLAY, par, "%s()\n", __func__);

	switch (par->info->var.rotate) {
	case 270:
		write_reg(par, 0xB6, 0x00, 0x02, 0x3B);
		write_reg(par, 0x36, 0x28);
		break;
	case 180:
		write_reg(par, 0xB6, 0x00, 0x22, 0x3B);
		write_reg(par, 0x36, 0x58);
		break;
	case 90:
		write_reg(par, 0xB6, 0x00, 0x22, 0x3B);
		write_reg(par, 0x36, 0x38);
		break;
	default:
		write_reg(par, 0xB6, 0x00, 0x22, 0x3B);
		write_reg(par, 0x36, 0x08);
		break;
	}

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
FBTFT_REGISTER_DRIVER(DRVNAME, "neosec,tinylcd", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("spi:tinylcd");

MODULE_DESCRIPTION("Custom FB driver for tinylcd.com display");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
