/*
 * Infineon TUA 9001 silicon tuner driver
 *
 * Copyright (C) 2009 Antti Palosaari <crope@iki.fi>
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

#ifndef TUA9001_H
#define TUA9001_H

#include <linux/kconfig.h>
#include "dvb_frontend.h"

struct tua9001_config {
	/*
	 * I2C address
	 */
	u8 i2c_addr;
};

/*
 * TUA9001 I/O PINs:
 *
 * CEN - chip enable
 * 0 = chip disabled (chip off)
 * 1 = chip enabled (chip on)
 *
 * RESETN - chip reset
 * 0 = reset disabled (chip reset off)
 * 1 = reset enabled (chip reset on)
 *
 * RXEN - RX enable
 * 0 = RX disabled (chip idle)
 * 1 = RX enabled (chip tuned)
 */

#define TUA9001_CMD_CEN     0
#define TUA9001_CMD_RESETN  1
#define TUA9001_CMD_RXEN    2

#if IS_REACHABLE(CONFIG_MEDIA_TUNER_TUA9001)
extern struct dvb_frontend *tua9001_attach(struct dvb_frontend *fe,
		struct i2c_adapter *i2c, struct tua9001_config *cfg);
#else
static inline struct dvb_frontend *tua9001_attach(struct dvb_frontend *fe,
		struct i2c_adapter *i2c, struct tua9001_config *cfg)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif
