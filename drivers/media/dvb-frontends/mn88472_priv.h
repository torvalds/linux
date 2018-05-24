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

#ifndef MN88472_PRIV_H
#define MN88472_PRIV_H

#include <media/dvb_frontend.h>
#include <media/dvb_math.h>
#include "mn88472.h"
#include <linux/firmware.h>
#include <linux/regmap.h>

#define MN88472_FIRMWARE "dvb-demod-mn88472-02.fw"

struct mn88472_dev {
	struct i2c_client *client[3];
	struct regmap *regmap[3];
	struct dvb_frontend fe;
	u16 i2c_write_max;
	unsigned int clk;
	unsigned int active:1;
	unsigned int ts_mode:1;
	unsigned int ts_clk:1;
};

#endif
