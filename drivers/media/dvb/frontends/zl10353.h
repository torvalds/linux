/*
 *  Driver for Zarlink DVB-T ZL10353 demodulator
 *
 *  Copyright (C) 2006, 2007 Christopher Pascoe <c.pascoe@itee.uq.edu.au>
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

	/* frequencies in units of 0.1kHz */
	int adc_clock;	/* default: 450560 (45.056  MHz) */
	int if2;	/* default: 361667 (36.1667 MHz) */

	/* set if no pll is connected to the secondary i2c bus */
	int no_tuner;

	/* set if parallel ts output is required */
	int parallel_ts;

	/* set if i2c_gate_ctrl disable is required */
	u8 disable_i2c_gate_ctrl:1;
};

#if defined(CONFIG_DVB_ZL10353) || (defined(CONFIG_DVB_ZL10353_MODULE) && defined(MODULE))
extern struct dvb_frontend* zl10353_attach(const struct zl10353_config *config,
					   struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend* zl10353_attach(const struct zl10353_config *config,
					   struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_ZL10353 */

#endif /* ZL10353_H */
