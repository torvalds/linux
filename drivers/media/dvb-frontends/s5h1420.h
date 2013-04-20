/*
 * Driver for
 *    Samsung S5H1420 and
 *    PnpNetwork PN1010 QPSK Demodulator
 *
 * Copyright (C) 2005 Andrew de Quincey <adq_dvb@lidskialf.net>
 * Copyright (C) 2005-8 Patrick Boettcher <pb@linuxtv.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef S5H1420_H
#define S5H1420_H

#include <linux/dvb/frontend.h>

struct s5h1420_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/* does the inversion require inversion? */
	u8 invert:1;

	u8 repeated_start_workaround:1;
	u8 cdclk_polarity:1; /* 1 == falling edge, 0 == raising edge */

	u8 serial_mpeg:1;
};

#if IS_ENABLED(CONFIG_DVB_S5H1420)
extern struct dvb_frontend *s5h1420_attach(const struct s5h1420_config *config,
	     struct i2c_adapter *i2c);
extern struct i2c_adapter *s5h1420_get_tuner_i2c_adapter(struct dvb_frontend *fe);
#else
static inline struct dvb_frontend *s5h1420_attach(const struct s5h1420_config *config,
					   struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}

static inline struct i2c_adapter *s5h1420_get_tuner_i2c_adapter(struct dvb_frontend *fe)
{
	return NULL;
}
#endif // CONFIG_DVB_S5H1420

#endif // S5H1420_H
