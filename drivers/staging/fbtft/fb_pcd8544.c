// SPDX-License-Identifier: GPL-2.0+
/*
 * FB driver for the PCD8544 LCD Controller
 *
 * The display is monochrome and the video memory is RGB565.
 * Any pixel value except 0 turns the pixel on.
 *
 * Copyright (C) 2013 Noralf Tronnes
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>

#include "fbtft.h"

#define DRVNAME	       "fb_pcd8544"
#define WIDTH          84
#define HEIGHT         48
#define TXBUFLEN       (84 * 6)
#define DEFAULT_GAMMA  "40" /* gamma controls the contrast in this driver */

static unsigned int tc;
module_param(tc, uint, 0000);
MODULE_PARM_DESC(tc, "TC[1:0] Temperature coefficient: 0-3 (default: 0)");

static unsigned int bs = 4;
module_param(bs, uint, 0000);
MODULE_PARM_DESC(bs, "BS[2:0] Bias voltage level: 0-7 (default: 4)");

static int init_display(struct fbtft_par *par)
{
	par->fbtftops.reset(par);

	/* Function set
	 *
	 * 5:1  1
	 * 2:0  PD - Powerdown control: chip is active
	 * 1:0  V  - Entry mode: horizontal addressing
	 * 0:1  H  - Extended instruction set control: extended
	 */
	write_reg(par, 0x21);

	/* H=1 Temperature control
	 *
	 * 2:1  1
	 * 1:x  TC1 - Temperature Coefficient: 0x10
	 * 0:x  TC0
	 */
	write_reg(par, 0x04 | (tc & 0x3));

	/* H=1 Bias system
	 *
	 * 4:1  1
	 * 3:0  0
	 * 2:x  BS2 - Bias System
	 * 1:x  BS1
	 * 0:x  BS0
	 */
	write_reg(par, 0x10 | (bs & 0x7));

	/* Function set
	 *
	 * 5:1  1
	 * 2:0  PD - Powerdown control: chip is active
	 * 1:1  V  - Entry mode: vertical addressing
	 * 0:0  H  - Extended instruction set control: basic
	 */
	write_reg(par, 0x22);

	/* H=0 Display control
	 *
	 * 3:1  1
	 * 2:1  D  - DE: 10=normal mode
	 * 1:0  0
	 * 0:0  E
	 */
	write_reg(par, 0x08 | 4);

	return 0;
}

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	/* H=0 Set X address of RAM
	 *
	 * 7:1  1
	 * 6-0: X[6:0] - 0x00
	 */
	write_reg(par, 0x80);

	/* H=0 Set Y address of RAM
	 *
	 * 7:0  0
	 * 6:1  1
	 * 2-0: Y[2:0] - 0x0
	 */
	write_reg(par, 0x40);
}

static int write_vmem(struct fbtft_par *par, size_t offset, size_t len)
{
	u16 *vmem16 = (u16 *)par->info->screen_buffer;
	u8 *buf = par->txbuf.buf;
	int x, y, i;
	int ret = 0;

	for (x = 0; x < 84; x++) {
		for (y = 0; y < 6; y++) {
			*buf = 0x00;
			for (i = 0; i < 8; i++)
				*buf |= (vmem16[(y * 8 + i) * 84 + x] ?
					 1 : 0) << i;
			buf++;
		}
	}

	/* Write data */
	gpio_set_value(par->gpio.dc, 1);
	ret = par->fbtftops.write(par, par->txbuf.buf, 6 * 84);
	if (ret < 0)
		dev_err(par->info->device, "write failed and returned: %d\n",
			ret);

	return ret;
}

static int set_gamma(struct fbtft_par *par, u32 *curves)
{
	/* apply mask */
	curves[0] &= 0x7F;

	write_reg(par, 0x23); /* turn on extended instruction set */
	write_reg(par, 0x80 | curves[0]);
	write_reg(par, 0x22); /* turn off extended instruction set */

	return 0;
}

static struct fbtft_display display = {
	.regwidth = 8,
	.width = WIDTH,
	.height = HEIGHT,
	.txbuflen = TXBUFLEN,
	.gamma_num = 1,
	.gamma_len = 1,
	.gamma = DEFAULT_GAMMA,
	.fbtftops = {
		.init_display = init_display,
		.set_addr_win = set_addr_win,
		.write_vmem = write_vmem,
		.set_gamma = set_gamma,
	},
	.backlight = 1,
};

FBTFT_REGISTER_DRIVER(DRVNAME, "philips,pdc8544", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("spi:pdc8544");

MODULE_DESCRIPTION("FB driver for the PCD8544 LCD Controller");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
