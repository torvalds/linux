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

#include <linux/kconfig.h>
#include <linux/dvb/frontend.h>

struct rtl2830_platform_data {
	/*
	 * Clock frequency.
	 * Hz
	 * 4000000, 16000000, 25000000, 28800000
	 */
	u32 clk;

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

	/*
	 */
	struct dvb_frontend* (*get_dvb_frontend)(struct i2c_client *);
	struct i2c_adapter* (*get_i2c_adapter)(struct i2c_client *);
};

#endif /* RTL2830_H */
