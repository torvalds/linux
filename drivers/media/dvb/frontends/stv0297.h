/*
    Driver for STV0297 demodulator

    Copyright (C) 2003-2004 Dennis Noermann <dennis.noermann@noernet.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef STV0297_H
#define STV0297_H

#include <linux/dvb/frontend.h>
#include "dvb_frontend.h"

struct stv0297_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/* does the "inversion" need inverted? */
	u8 invert:1;

	/* PLL maintenance */
	int (*pll_init)(struct dvb_frontend* fe);
	int (*pll_set)(struct dvb_frontend* fe, struct dvb_frontend_parameters* params);
};

extern struct dvb_frontend* stv0297_attach(const struct stv0297_config* config,
					   struct i2c_adapter* i2c, int pwm);
extern int stv0297_enable_plli2c(struct dvb_frontend* fe);

#endif // STV0297_H
