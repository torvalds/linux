/*
 * tda80xx.c
 *
 * Philips TDA8044 / TDA8083 QPSK demodulator driver
 *
 * Copyright (C) 2001 Felix Domke <tmbinc@elitedvb.net>
 * Copyright (C) 2002-2004 Andreas Oberritter <obi@linuxtv.org>
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

#ifndef TDA80XX_H
#define TDA80XX_H

#include <linux/dvb/frontend.h>

struct tda80xx_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/* IRQ to use (0=>no IRQ used) */
	u32 irq;

	/* Register setting to use for 13v */
	u8 volt13setting;

	/* Register setting to use for 18v */
	u8 volt18setting;

	/* PLL maintenance */
	int (*pll_init)(struct dvb_frontend* fe);
	int (*pll_set)(struct dvb_frontend* fe, struct dvb_frontend_parameters* params);
};

extern struct dvb_frontend* tda80xx_attach(const struct tda80xx_config* config,
					   struct i2c_adapter* i2c);

#endif // TDA80XX_H
