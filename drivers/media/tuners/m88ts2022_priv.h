/*
 * Montage M88TS2022 silicon tuner driver
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

#ifndef M88TS2022_PRIV_H
#define M88TS2022_PRIV_H

#include "m88ts2022.h"
#include <linux/regmap.h>

struct m88ts2022_dev {
	struct m88ts2022_config cfg;
	struct i2c_client *client;
	struct regmap *regmap;
	u32 frequency_khz;
};

struct m88ts2022_reg_val {
	u8 reg;
	u8 val;
};

#endif
