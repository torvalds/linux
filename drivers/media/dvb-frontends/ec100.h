/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * E3C EC100 demodulator driver
 *
 * Copyright (C) 2009 Antti Palosaari <crope@iki.fi>
 */

#ifndef EC100_H
#define EC100_H

#include <linux/dvb/frontend.h>

struct ec100_config {
	/* demodulator's I2C address */
	u8 demod_address;
};


#if IS_REACHABLE(CONFIG_DVB_EC100)
extern struct dvb_frontend *ec100_attach(const struct ec100_config *config,
	struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *ec100_attach(
	const struct ec100_config *config, struct i2c_adapter *i2c)
{
	pr_warn("%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif /* EC100_H */
