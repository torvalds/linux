/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Fitipower FC0013 tuner driver
 *
 * Copyright (C) 2012 Hans-Frieder Vogt <hfvogt@gmx.net>
 */

#ifndef _FC0013_H_
#define _FC0013_H_

#include <media/dvb_frontend.h>
#include "fc001x-common.h"

#if IS_REACHABLE(CONFIG_MEDIA_TUNER_FC0013)
extern struct dvb_frontend *fc0013_attach(struct dvb_frontend *fe,
					struct i2c_adapter *i2c,
					u8 i2c_address, int dual_master,
					enum fc001x_xtal_freq xtal_freq);
#else
static inline struct dvb_frontend *fc0013_attach(struct dvb_frontend *fe,
					struct i2c_adapter *i2c,
					u8 i2c_address, int dual_master,
					enum fc001x_xtal_freq xtal_freq)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}

#endif

#endif
