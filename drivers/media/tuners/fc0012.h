/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Fitipower FC0012 tuner driver - include
 *
 * Copyright (C) 2012 Hans-Frieder Vogt <hfvogt@gmx.net>
 */

#ifndef _FC0012_H_
#define _FC0012_H_

#include <media/dvb_frontend.h>
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
