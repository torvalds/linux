/*
    Conexant cx24123/cx24109 - DVB QPSK Satellite demod/tuner driver

    Copyright (C) 2005 Steven Toth <stoth@hauppauge.com>

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

#ifndef CX24123_H
#define CX24123_H

#include <linux/dvb/frontend.h>

struct cx24123_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/*
	   cards like Hauppauge Nova-S Plus/Nova-SE2 use an Intersil ISL6421 chip
	   for LNB control, while KWorld DVB-S 100 use the LNBDC and LNBTone bits
	   from register 0x29 of the CX24123 demodulator
	*/
	int use_isl6421;

	/* PLL maintenance */
	int (*pll_init)(struct dvb_frontend* fe);
	int (*pll_set)(struct dvb_frontend* fe, struct dvb_frontend_parameters* params);

	/* Need to set device param for start_dma */
	int (*set_ts_params)(struct dvb_frontend* fe, int is_punctured);

	void (*enable_lnb_voltage)(struct dvb_frontend* fe, int on);
};

extern struct dvb_frontend* cx24123_attach(const struct cx24123_config* config,
					   struct i2c_adapter* i2c);

#endif /* CX24123_H */
