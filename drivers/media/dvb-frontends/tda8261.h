/*
	TDA8261 8PSK/QPSK tuner driver
	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

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

#ifndef __TDA8261_H
#define __TDA8261_H

enum tda8261_step {
	TDA8261_STEP_2000 = 0,	/* 2000 kHz */
	TDA8261_STEP_1000,	/* 1000 kHz */
	TDA8261_STEP_500,	/*  500 kHz */
	TDA8261_STEP_250,	/*  250 kHz */
	TDA8261_STEP_125	/*  125 kHz */
};

struct tda8261_config {
//	u8			buf[16];
	u8			addr;
	enum tda8261_step	step_size;
};

#if IS_REACHABLE(CONFIG_DVB_TDA8261)

extern struct dvb_frontend *tda8261_attach(struct dvb_frontend *fe,
					   const struct tda8261_config *config,
					   struct i2c_adapter *i2c);

#else

static inline struct dvb_frontend *tda8261_attach(struct dvb_frontend *fe,
						  const struct tda8261_config *config,
						  struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: Driver disabled by Kconfig\n", __func__);
	return NULL;
}

#endif //CONFIG_DVB_TDA8261

#endif// __TDA8261_H
