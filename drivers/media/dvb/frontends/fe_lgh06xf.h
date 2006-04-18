/*
 *  fe_lgh06xf.h - ATSC Tuner support for LG TDVS H06xF
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef FE_LGH06XF_H
#define FE_LGH06XF_H
#include "dvb-pll.h"

static int lg_h06xf_pll_set(struct dvb_frontend* fe, struct i2c_adapter* i2c_adap,
		     struct dvb_frontend_parameters* params)
{
	u8 buf[4];
	struct i2c_msg msg = { .addr = 0x61, .flags = 0,
			       .buf = buf, .len = sizeof(buf) };
	int err;

	dvb_pll_configure(&dvb_pll_tdvs_tua6034, buf, params->frequency, 0);
	if ((err = i2c_transfer(i2c_adap, &msg, 1)) != 1) {
		printk(KERN_WARNING "lg_h06xf: %s error "
			"(addr %02x <- %02x, err = %i)\n",
			__FUNCTION__, buf[0], buf[1], err);
		if (err < 0)
			return err;
		else
			return -EREMOTEIO;
	}

	/* Set the Auxiliary Byte. */
	buf[0] = buf[2];
	buf[0] &= ~0x20;
	buf[0] |= 0x18;
	buf[1] = 0x50;
	msg.len = 2;
	if ((err = i2c_transfer(i2c_adap, &msg, 1)) != 1) {
		printk(KERN_WARNING "lg_h06xf: %s error "
			"(addr %02x <- %02x, err = %i)\n",
			__FUNCTION__, buf[0], buf[1], err);
		if (err < 0)
			return err;
		else
			return -EREMOTEIO;
	}

	return 0;
}
#endif
