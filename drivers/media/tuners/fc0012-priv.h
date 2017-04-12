/*
 * Fitipower FC0012 tuner driver - private includes
 *
 * Copyright (C) 2012 Hans-Frieder Vogt <hfvogt@gmx.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _FC0012_PRIV_H_
#define _FC0012_PRIV_H_

struct fc0012_priv {
	struct i2c_adapter *i2c;
	const struct fc0012_config *cfg;

	u32 frequency;
	u32 bandwidth;
};

#endif
