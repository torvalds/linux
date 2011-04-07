/*
 * Sony CXD2820R demodulator driver
 *
 * Copyright (C) 2010 Antti Palosaari <crope@iki.fi>
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


#ifndef CXD2820R_PRIV_H
#define CXD2820R_PRIV_H

#include "dvb_frontend.h"
#include "dvb_math.h"
#include "cxd2820r.h"

#define LOG_PREFIX "cxd2820r"

#undef dbg
#define dbg(f, arg...) \
	if (cxd2820r_debug) \
		printk(KERN_INFO   LOG_PREFIX": " f "\n" , ## arg)
#undef err
#define err(f, arg...)  printk(KERN_ERR     LOG_PREFIX": " f "\n" , ## arg)
#undef info
#define info(f, arg...) printk(KERN_INFO    LOG_PREFIX": " f "\n" , ## arg)
#undef warn
#define warn(f, arg...) printk(KERN_WARNING LOG_PREFIX": " f "\n" , ## arg)

/*
 * FIXME: These are totally wrong and must be added properly to the API.
 * Only temporary solution in order to get driver compile.
 */
#define SYS_DVBT2             SYS_DAB
#define TRANSMISSION_MODE_1K  0
#define TRANSMISSION_MODE_16K 0
#define TRANSMISSION_MODE_32K 0
#define GUARD_INTERVAL_1_128  0
#define GUARD_INTERVAL_19_128 0
#define GUARD_INTERVAL_19_256 0

struct reg_val_mask {
	u32 reg;
	u8  val;
	u8  mask;
};

struct cxd2820r_priv {
	struct i2c_adapter *i2c;
	struct dvb_frontend fe[2];
	struct cxd2820r_config cfg;
	struct i2c_adapter tuner_i2c_adapter;

	struct mutex fe_lock; /* FE lock */
	int active_fe:2; /* FE lock, -1=NONE, 0=DVB-T/T2, 1=DVB-C */

	int ber_running:1;

	u8 bank[2];
	u8 gpio[3];

	fe_delivery_system_t delivery_system;
};

#endif /* CXD2820R_PRIV_H */
