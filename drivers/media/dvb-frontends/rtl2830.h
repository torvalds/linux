/*
 * Realtek RTL2830 DVB-T demodulator driver
 *
 * Copyright (C) 2011 Antti Palosaari <crope@iki.fi>
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
 *    You should have received a copy of the GNU General Public License along
 *    with this program; if not, write to the Free Software Foundation, Inc.,
 *    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef RTL2830_H
#define RTL2830_H

#include <linux/dvb/frontend.h>

struct rtl2830_config {
	/*
	 * Demodulator I2C address.
	 */
	u8 i2c_addr;

	/*
	 * Xtal frequency.
	 * Hz
	 * 4000000, 16000000, 25000000, 28800000
	 */
	u32 xtal;

	/*
	 * TS output mode.
	 */
	u8 ts_mode;

	/*
	 * Spectrum inversion.
	 */
	bool spec_inv;

	/*
	 */
	u8 vtop;

	/*
	 */
	u8 krf;

	/*
	 */
	u8 agc_targ_val;
};

#if defined(CONFIG_DVB_RTL2830) || \
	(defined(CONFIG_DVB_RTL2830_MODULE) && defined(MODULE))
extern struct dvb_frontend *rtl2830_attach(
	const struct rtl2830_config *config,
	struct i2c_adapter *i2c
);

extern struct i2c_adapter *rtl2830_get_tuner_i2c_adapter(
	struct dvb_frontend *fe
);
#else
static inline struct dvb_frontend *rtl2830_attach(
	const struct rtl2830_config *config,
	struct i2c_adapter *i2c
)
{
	pr_warn("%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}

static inline struct i2c_adapter *rtl2830_get_tuner_i2c_adapter(
	struct dvb_frontend *fe
)
{
	return NULL;
}
#endif

#endif /* RTL2830_H */
