/*
    cx24110 - Single Chip Satellite Channel Receiver driver module

    Copyright (C) 2002 Peter Hettkamp <peter.hettkamp@htp-tel.de> based on
    work
    Copyright (C) 1999 Convergence Integrated Media GmbH <ralph@convergence.de>

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

#ifndef CX24110_H
#define CX24110_H

#include <linux/dvb/frontend.h>

struct cx24110_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/* PLL maintenance */
	int (*pll_init)(struct dvb_frontend* fe);
	int (*pll_set)(struct dvb_frontend* fe, struct dvb_frontend_parameters* params);
};

extern struct dvb_frontend* cx24110_attach(const struct cx24110_config* config,
					   struct i2c_adapter* i2c);

extern int cx24110_pll_write(struct dvb_frontend* fe, u32 data);

#endif // CX24110_H
