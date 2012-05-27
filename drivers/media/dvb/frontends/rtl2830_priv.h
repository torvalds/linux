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

#ifndef RTL2830_PRIV_H
#define RTL2830_PRIV_H

#include "dvb_frontend.h"
#include "dvb_math.h"
#include "rtl2830.h"

#define LOG_PREFIX "rtl2830"

#undef dbg
#define dbg(f, arg...) \
	if (rtl2830_debug) \
		printk(KERN_INFO            LOG_PREFIX": " f "\n" , ## arg)
#undef err
#define err(f, arg...)  printk(KERN_ERR     LOG_PREFIX": " f "\n" , ## arg)
#undef info
#define info(f, arg...) printk(KERN_INFO    LOG_PREFIX": " f "\n" , ## arg)
#undef warn
#define warn(f, arg...) printk(KERN_WARNING LOG_PREFIX": " f "\n" , ## arg)

struct rtl2830_priv {
	struct i2c_adapter *i2c;
	struct dvb_frontend fe;
	struct rtl2830_config cfg;
	struct i2c_adapter tuner_i2c_adapter;

	bool sleeping;

	u8 page; /* active register page */
};

struct rtl2830_reg_val_mask {
	u16 reg;
	u8  val;
	u8  mask;
};

#endif /* RTL2830_PRIV_H */
