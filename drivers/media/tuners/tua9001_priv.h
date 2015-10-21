/*
 * Infineon TUA9001 silicon tuner driver
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
 */

#ifndef TUA9001_PRIV_H
#define TUA9001_PRIV_H

#include "tua9001.h"
#include <linux/math64.h>
#include <linux/regmap.h>

struct tua9001_reg_val {
	u8 reg;
	u16 val;
};

struct tua9001_dev {
	struct dvb_frontend *fe;
	struct i2c_client *client;
	struct regmap *regmap;
};

#endif
