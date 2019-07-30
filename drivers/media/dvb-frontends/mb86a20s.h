/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *   Fujitsu mb86a20s driver
 *
 *   Copyright (C) 2010 Mauro Carvalho Chehab
 */

#ifndef MB86A20S_H
#define MB86A20S_H

#include <linux/dvb/frontend.h>

/**
 * struct mb86a20s_config - Define the per-device attributes of the frontend
 *
 * @fclk:		Clock frequency. If zero, assumes the default
 *			(32.57142 Mhz)
 * @demod_address:	the demodulator's i2c address
 * @is_serial:		if true, TS is serial. Otherwise, TS is parallel
 */
struct mb86a20s_config {
	u32	fclk;
	u8	demod_address;
	bool	is_serial;
};

#if IS_REACHABLE(CONFIG_DVB_MB86A20S)
/**
 * Attach a mb86a20s demod
 *
 * @config: pointer to &struct mb86a20s_config with demod configuration.
 * @i2c: i2c adapter to use.
 *
 * return: FE pointer on success, NULL on failure.
 */
extern struct dvb_frontend *mb86a20s_attach(const struct mb86a20s_config *config,
					   struct i2c_adapter *i2c);

#else
static inline struct dvb_frontend *mb86a20s_attach(
	const struct mb86a20s_config *config, struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif /* MB86A20S */
