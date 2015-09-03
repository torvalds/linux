/*
 * Allegro A8293 SEC driver
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

#ifndef A8293_H
#define A8293_H

#include "dvb_frontend.h"
#include <linux/kconfig.h>

/*
 * I2C address
 * 0x08, 0x09, 0x0a, 0x0b
 */

/**
 * struct a8293_platform_data - Platform data for the a8293 driver
 * @dvb_frontend: DVB frontend.
 */
struct a8293_platform_data {
	struct dvb_frontend *dvb_frontend;
};


struct a8293_config {
	u8 i2c_addr;
};

#if IS_REACHABLE(CONFIG_DVB_A8293)
extern struct dvb_frontend *a8293_attach(struct dvb_frontend *fe,
	struct i2c_adapter *i2c, const struct a8293_config *cfg);
#else
static inline struct dvb_frontend *a8293_attach(struct dvb_frontend *fe,
	struct i2c_adapter *i2c, const struct a8293_config *cfg)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif /* A8293_H */
