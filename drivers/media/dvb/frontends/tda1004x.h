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

enum tda10046_xtal {
	TDA10046_XTAL_4M,
	TDA10046_XTAL_16M,
};

enum tda10046_agc {
	TDA10046_AGC_DEFAULT,		/* original configuration */
	TDA10046_AGC_IFO_AUTO_NEG,	/* IF AGC only, automatic, negtive */
	TDA10046_AGC_IFO_AUTO_POS,	/* IF AGC only, automatic, positive */
	TDA10046_AGC_TDA827X,		/* IF AGC only, special setup for tda827x */
	TDA10046_AGC_TDA827X_GPL,	/* same as above, but GPIOs 0 */
};

enum tda10046_if {
	TDA10046_FREQ_3617,		/* original config, 36,166 MHZ */
	TDA10046_FREQ_3613,		/* 36,13 MHZ */
	TDA10046_FREQ_045,		/* low IF, 4.0, 4.5, or 5.0 MHZ */
	TDA10046_FREQ_052,		/* low IF, 5.1667 MHZ for tda9889 */
};

struct tda1004x_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/* does the "inversion" need inverted? */
	u8 invert;

	/* Does the OCLK signal need inverted? */
	u8 invert_oclk;

	/* Xtal frequency, 4 or 16MHz*/
	enum tda10046_xtal xtal_freq;

	/* IF frequency */
	enum tda10046_if if_freq;

	/* AGC configuration */
	enum tda10046_agc agc_config;

	/* request firmware for device */
	/* set this to NULL if the card has a firmware EEPROM */
	int (*request_firmware)(struct dvb_frontend* fe, const struct firmware **fw, char* name);
};

extern struct dvb_frontend* tda10045_attach(const struct tda1004x_config* config,
					    struct i2c_adapter* i2c);

extern struct dvb_frontend* tda10046_attach(const struct tda1004x_config* config,
					    struct i2c_adapter* i2c);

extern int tda1004x_write_byte(struct dvb_frontend* fe, int reg, int data);

#endif // TDA1004X_H
