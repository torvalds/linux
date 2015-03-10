/*
 * FB driver for the S6D02A1 LCD Controller
 *
 * Based on fb_st7735r.c by Noralf Tronnes
 * Init code from UTFT library by Henning Karlsen
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

#include "fbtft.h"

#define DRVNAME "fb_s6d02a1"

static int default_init_sequence[] = {

	-1, 0xf0, 0x5a, 0x5a,

	-1, 0xfc, 0x5a, 0x5a,

	-1, 0xfa, 0x02, 0x1f, 0x00, 0x10, 0x22, 0x30, 0x38, 0x3A, 0x3A, 0x3A, 0x3A, 0x3A, 0x3d, 0x02, 0x01,

	-1, 0xfb, 0x21, 0x00, 0x02, 0x04, 0x07, 0x0a, 0x0b, 0x0c, 0x0c, 0x16, 0x1e, 0x30, 0x3f, 0x01, 0x02,

	/* power setting sequence */
	-1, 0xfd, 0x00, 0x00, 0x00, 0x17, 0x10, 0x00, 0x01, 0x01, 0x00, 0x1f, 0x1f,

	-1, 0xf4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0x3f, 0x07, 0x00, 0x3C, 0x36, 0x00, 0x3C, 0x36, 0x00,

	-1, 0xf5, 0x00, 0x70, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6d, 0x66, 0x06,

	-1, 0xf6, 0x02, 0x00, 0x3f, 0x00, 0x00, 0x00, 0x02, 0x00, 0x06, 0x01, 0x00,

	-1, 0xf2, 0x00, 0x01, 0x03, 0x08, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x04, 0x08, 0x08,

	-1, 0xf8, 0x11,

	-1, 0xf7, 0xc8, 0x20, 0x00, 0x00,

	-1, 0xf3, 0x00, 0x00,

	-1, 0x11,
	-2, 50,

	-1, 0xf3, 0x00, 0x01,
	-2, 50,
	-1, 0xf3, 0x00, 0x03,
	-2, 50,
	-1, 0xf3, 0x00, 0x07,
	-2, 50,
	-1, 0xf3, 0x00, 0x0f,
	-2, 50,

	-1, 0xf4, 0x00, 0x04, 0x00, 0x00, 0x00, 0x3f, 0x3f, 0x07, 0x00, 0x3C, 0x36, 0x00, 0x3C, 0x36, 0x00,
	-2, 50,

	-1, 0xf3, 0x00, 0x1f,
	-2, 50,
	-1, 0xf3, 0x00, 0x7f,
	-2, 50,

	-1, 0xf3, 0x00, 0xff,
	-2, 50,

	-1, 0xfd, 0x00, 0x00, 0x00, 0x17, 0x10, 0x00, 0x00, 0x01, 0x00, 0x16, 0x16,

	-1, 0xf4, 0x00, 0x09, 0x00, 0x00, 0x00, 0x3f, 0x3f, 0x07, 0x00, 0x3C, 0x36, 0x00, 0x3C, 0x36, 0x00,

	/* initializing sequence */

	-1, 0x36, 0x08,

	-1, 0x35, 0x00,

	-1, 0x3a, 0x05,

	/* gamma setting sequence */
	-1, 0x26, 0x01,	/* preset gamma curves, possible values 0x01, 0x02, 0x04, 0x08 */

	-2, 150,
	-1, 0x29,
	-1, 0x2c,
	/* end marker */
	-3

};

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	fbtft_par_dbg(DEBUG_SET_ADDR_WIN, par,
		"%s(xs=%d, ys=%d, xe=%d, ye=%d)\n", __func__, xs, ys, xe, ye);

	/* Column address */
	write_reg(par, 0x2A, xs >> 8, xs & 0xFF, xe >> 8, xe & 0xFF);

	/* Row adress */
	write_reg(par, 0x2B, ys >> 8, ys & 0xFF, ye >> 8, ye & 0xFF);

	/* Memory write */
	write_reg(par, 0x2C);
}

#define MY (1 << 7)
#define MX (1 << 6)
#define MV (1 << 5)
static int set_var(struct fbtft_par *par)
{
	fbtft_par_dbg(DEBUG_INIT_DISPLAY, par, "%s()\n", __func__);

	/* MADCTL - Memory data access control
	     RGB/BGR:
		1. Mode selection pin SRGB
			RGB H/W pin for color filter setting: 0=RGB, 1=BGR
		2. MADCTL RGB bit
			RGB-BGR ORDER color filter panel: 0=RGB, 1=BGR */
	switch (par->info->var.rotate) {
	case 0:
		write_reg(par, 0x36, MX | MY | (par->bgr << 3));
		break;
	case 270:
		write_reg(par, 0x36, MY | MV | (par->bgr << 3));
		break;
	case 180:
		write_reg(par, 0x36, par->bgr << 3);
		break;
	case 90:
		write_reg(par, 0x36, MX | MV | (par->bgr << 3));
		break;
	}

	return 0;
}

static struct fbtft_display display = {
	.regwidth = 8,
	.width = 128,
	.height = 160,
	.init_sequence = default_init_sequence,
	.fbtftops = {
		.set_addr_win = set_addr_win,
		.set_var = set_var,
	},
};
FBTFT_REGISTER_DRIVER(DRVNAME, "samsung,s6d02a1", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:s6d02a1");
MODULE_ALIAS("platform:s6d02a1");

MODULE_DESCRIPTION("FB driver for the S6D02A1 LCD Controller");
MODULE_AUTHOR("WOLFGANG BUENING");
MODULE_LICENSE("GPL");
