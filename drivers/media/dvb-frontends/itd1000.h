/*
 *  Driver for the Integrant ITD1000 "Zero-IF Tuner IC for Direct Broadcast Satellite"
 *
 *  Copyright (c) 2007 Patrick Boettcher <pb@linuxtv.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 */

#ifndef ITD1000_H
#define ITD1000_H

struct dvb_frontend;
struct i2c_adapter;

struct itd1000_config {
	u8 i2c_address;
};

#if IS_REACHABLE(CONFIG_DVB_TUNER_ITD1000)
extern struct dvb_frontend *itd1000_attach(struct dvb_frontend *fe, struct i2c_adapter *i2c, struct itd1000_config *cfg);
#else
static inline struct dvb_frontend *itd1000_attach(struct dvb_frontend *fe, struct i2c_adapter *i2c, struct itd1000_config *cfg)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif
