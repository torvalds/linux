/*
    Driver for S5H1420 QPSK Demodulators

    Copyright (C) 2005 Andrew de Quincey <adq_dvb@lidskialf.net>

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

#ifndef S5H1420_H
#define S5H1420_H

#include <linux/dvb/frontend.h>

struct s5h1420_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/* does the inversion require inversion? */
	u8 invert:1;

	/* PLL maintenance */
	int (*pll_init)(struct dvb_frontend* fe);
	int (*pll_set)(struct dvb_frontend* fe, struct dvb_frontend_parameters* params, u32* freqout);
};

extern struct dvb_frontend* s5h1420_attach(const struct s5h1420_config* config,
	     struct i2c_adapter* i2c);

#endif // S5H1420_H
