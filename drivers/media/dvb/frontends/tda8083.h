/*
    Driver for Grundig 29504-491, a Philips TDA8083 based QPSK Frontend

    Copyright (C) 2001 Convergence Integrated Media GmbH

    written by Ralph Metzler <ralph@convergence.de>

    adoption to the new DVB frontend API and diagnostic ioctl's
    by Holger Waechtler <holger@convergence.de>

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

#ifndef TDA8083_H
#define TDA8083_H

#include <linux/dvb/frontend.h>

struct tda8083_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/* PLL maintenance */
	int (*pll_init)(struct dvb_frontend* fe);
	int (*pll_set)(struct dvb_frontend* fe, struct dvb_frontend_parameters* params);
};

extern struct dvb_frontend* tda8083_attach(const struct tda8083_config* config,
					   struct i2c_adapter* i2c);

#endif // TDA8083_H
