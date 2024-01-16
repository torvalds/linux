/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *    Support for OR51132 (pcHDTV HD-3000) - VSB/QAM
 *
 *    Copyright (C) 2005 Kirk Lapray <kirk_lapray@bigfoot.com>
*/

#ifndef OR51132_H
#define OR51132_H

#include <linux/firmware.h>
#include <linux/dvb/frontend.h>

struct or51132_config
{
	/* The demodulator's i2c address */
	u8 demod_address;

	/* Need to set device param for start_dma */
	int (*set_ts_params)(struct dvb_frontend* fe, int is_punctured);
};

#if IS_REACHABLE(CONFIG_DVB_OR51132)
extern struct dvb_frontend* or51132_attach(const struct or51132_config* config,
					   struct i2c_adapter* i2c);
#else
static inline struct dvb_frontend* or51132_attach(const struct or51132_config* config,
					   struct i2c_adapter* i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif // CONFIG_DVB_OR51132

#endif // OR51132_H
