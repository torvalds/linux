/*
 *  Driver for Zarlink DVB-T ZL10353 demodulator
 *
 *  Copyright (C) 2006 Christopher Pascoe <c.pascoe@itee.uq.edu.au>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.=
 */

#ifndef ZL10353_H
#define ZL10353_H

#include <linux/dvb/frontend.h>

struct zl10353_config
{
	/* demodulator's I2C address */
	u8 demod_address;

	/* function which configures the PLL buffer (for secondary I2C
	 * connected tuner) or tunes the PLL (for direct connected tuner) */
	int (*pll_set)(struct dvb_frontend *fe,
		       struct dvb_frontend_parameters *params, u8 *pllbuf);
};

extern struct dvb_frontend* zl10353_attach(const struct zl10353_config *config,
					   struct i2c_adapter *i2c);

extern int zl10353_write(struct dvb_frontend *fe, u8 *ibuf, int ilen);

#endif /* ZL10353_H */
