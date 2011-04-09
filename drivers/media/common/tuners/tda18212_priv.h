/*
 * NXP TDA18212HN silicon tuner driver
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

#ifndef TDA18212_PRIV_H
#define TDA18212_PRIV_H

#include "tda18212.h"

#define LOG_PREFIX "tda18212"

#undef dbg
#define dbg(f, arg...) \
	if (debug) \
		printk(KERN_INFO   LOG_PREFIX": " f "\n" , ## arg)
#undef err
#define err(f, arg...)  printk(KERN_ERR     LOG_PREFIX": " f "\n" , ## arg)
#undef info
#define info(f, arg...) printk(KERN_INFO    LOG_PREFIX": " f "\n" , ## arg)
#undef warn
#define warn(f, arg...) printk(KERN_WARNING LOG_PREFIX": " f "\n" , ## arg)

struct tda18212_priv {
	struct tda18212_config *cfg;
	struct i2c_adapter *i2c;
};

#endif
