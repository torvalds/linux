  /*
     Driver for Philips tda1004xh OFDM Frontend

     (c) 2004 Andrew de Quincey

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

#ifndef TDA1004X_H
#define TDA1004X_H

#include <linux/dvb/frontend.h>
#include <linux/firmware.h>

struct tda1004x_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/* does the "inversion" need inverted? */
	u8 invert:1;

	/* Does the OCLK signal need inverted? */
	u8 invert_oclk:1;

	/* PLL maintenance */
	int (*pll_init)(struct dvb_frontend* fe);
	int (*pll_set)(struct dvb_frontend* fe, struct dvb_frontend_parameters* params);

	/* request firmware for device */
	int (*request_firmware)(struct dvb_frontend* fe, const struct firmware **fw, char* name);
};

extern struct dvb_frontend* tda10045_attach(const struct tda1004x_config* config,
					    struct i2c_adapter* i2c);

extern struct dvb_frontend* tda10046_attach(const struct tda1004x_config* config,
					    struct i2c_adapter* i2c);

extern int tda1004x_write_byte(struct dvb_frontend* fe, int reg, int data);

#endif // TDA1004X_H
