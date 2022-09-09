/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Fitipower FC0012 tuner driver - private includes
 *
 * Copyright (C) 2012 Hans-Frieder Vogt <hfvogt@gmx.net>
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
