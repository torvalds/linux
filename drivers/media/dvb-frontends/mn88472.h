/*
 * Panasonic MN88472 DVB-T/T2/C demodulator driver
 *
 * Copyright (C) 2013 Antti Palosaari <crope@iki.fi>
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
 */

#ifndef MN88472_H
#define MN88472_H

#include <linux/dvb/frontend.h>

struct mn88472_c_config {
	/*
	 * max bytes I2C client could write
	 * Value must be set.
	 */
	int i2c_wr_max;
};

#if IS_ENABLED(CONFIG_DVB_MN88472)
extern struct dvb_frontend *mn88472_c_attach(
	const struct mn88472_c_config *cfg,
	struct i2c_adapter *i2c
);
#else
static inline struct dvb_frontend *mn88472_c_attach(
	const struct mn88472_c_config *cfg,
	struct i2c_adapter *i2c
)
{
	dev_warn(&i2c->dev, "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif
