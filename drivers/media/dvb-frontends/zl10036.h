/**
 * Driver for Zarlink ZL10036 DVB-S silicon tuner
 *
 * Copyright (C) 2006 Tino Reichardt
 * Copyright (C) 2007-2009 Matthias Schwarzott <zzam@gentoo.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef DVB_ZL10036_H
#define DVB_ZL10036_H

#include <linux/i2c.h>
#include "dvb_frontend.h"

struct zl10036_config {
	u8 tuner_address;
	int rf_loop_enable;
};

#if IS_REACHABLE(CONFIG_DVB_ZL10036)
/**
 * Attach a zl10036 tuner to the supplied frontend structure.
 *
 * @fe: Frontend to attach to.
 * @config: zl10036_config structure.
 * @i2c: pointer to struct i2c_adapter.
 * return: FE pointer on success, NULL on failure.
 */
extern struct dvb_frontend *zl10036_attach(struct dvb_frontend *fe,
	const struct zl10036_config *config, struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *zl10036_attach(struct dvb_frontend *fe,
	const struct zl10036_config *config, struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif /* DVB_ZL10036_H */
