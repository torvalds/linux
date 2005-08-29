/*
 *    Support for LGDT3302 and LGDT3303 - VSB/QAM
 *
 *    Copyright (C) 2005 Wilson Michaels <wilsonmichaels@earthlink.net>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef LGDT330X_H
#define LGDT330X_H

#include <linux/dvb/frontend.h>

typedef enum lg_chip_t {
		UNDEFINED,
		LGDT3302,
		LGDT3303
}lg_chip_type;

struct lgdt330x_config
{
	/* The demodulator's i2c address */
	u8 demod_address;

	/* LG demodulator chip LGDT3302 or LGDT3303 */
	lg_chip_type demod_chip;

	/* MPEG hardware interface - 0:parallel 1:serial */
	int serial_mpeg;

	/* PLL interface */
	int (*pll_rf_set) (struct dvb_frontend* fe, int index);
	int (*pll_set)(struct dvb_frontend* fe, struct dvb_frontend_parameters* params);

	/* Need to set device param for start_dma */
	int (*set_ts_params)(struct dvb_frontend* fe, int is_punctured);
};

extern struct dvb_frontend* lgdt330x_attach(const struct lgdt330x_config* config,
					    struct i2c_adapter* i2c);

#endif /* LGDT330X_H */

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
