/*
 * Fitipower FC0012 tuner driver - include
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _FC0012_H_
#define _FC0012_H_

#include <linux/kconfig.h>
#include "dvb_frontend.h"
#include "fc001x-common.h"

struct fc0012_config {
	/*
	 * I2C address
	 */
	u8 i2c_address;

	/*
	 * clock
	 */
	enum fc001x_xtal_freq xtal_freq;

	bool dual_master;

	/*
	 * RF loop-through
	 */
	bool loop_through;

	/*
	 * clock output
	 */
	bool clock_out;
};

#if IS_REACHABLE(CONFIG_MEDIA_TUNER_FC0012)
extern struct dvb_frontend *fc0012_attach(struct dvb_frontend *fe,
					struct i2c_adapter *i2c,
					const struct fc0012_config *cfg);
#else
static inline struct dvb_frontend *fc0012_attach(struct dvb_frontend *fe,
					struct i2c_adapter *i2c,
					const struct fc0012_config *cfg)
{
	pr_warn("%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif
