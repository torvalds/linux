/*
 *    Support for OR51132 (pcHDTV HD-3000) - VSB/QAM
 *
 *    Copyright (C) 2005 Kirk Lapray <kirk_lapray@bigfoot.com>
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
 *
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
