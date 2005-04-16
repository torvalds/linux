/*
 * at76c651.c
 *
 * Atmel DVB-C Frontend Driver (at76c651)
 *
 * Copyright (C) 2001 fnbrd <fnbrd@gmx.de>
 *             & 2002-2004 Andreas Oberritter <obi@linuxtv.org>
 *             & 2003 Wolfram Joost <dbox2@frokaschwei.de>
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
 *
 * AT76C651
 * http://www.nalanda.nitc.ac.in/industry/datasheets/atmel/acrobat/doc1293.pdf
 * http://www.atmel.com/atmel/acrobat/doc1320.pdf
 */

#ifndef AT76C651_H
#define AT76C651_H

#include <linux/dvb/frontend.h>

struct at76c651_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/* PLL maintenance */
	int (*pll_init)(struct dvb_frontend* fe);
	int (*pll_set)(struct dvb_frontend* fe, struct dvb_frontend_parameters* params);
};

extern struct dvb_frontend* at76c651_attach(const struct at76c651_config* config,
					    struct i2c_adapter* i2c);

#endif // AT76C651_H
