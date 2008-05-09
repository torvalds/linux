/*
 *  Driver for Microtune MT2266 "Direct conversion low power broadband tuner"
 *
 *  Copyright (c) 2007 Olivier DANET <odanet@caramail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#ifndef MT2266_H
#define MT2266_H

struct dvb_frontend;
struct i2c_adapter;

struct mt2266_config {
	u8 i2c_address;
};

#if defined(CONFIG_MEDIA_TUNER_MT2266) || (defined(CONFIG_MEDIA_TUNER_MT2266_MODULE) && defined(MODULE))
extern struct dvb_frontend * mt2266_attach(struct dvb_frontend *fe, struct i2c_adapter *i2c, struct mt2266_config *cfg);
#else
static inline struct dvb_frontend * mt2266_attach(struct dvb_frontend *fe, struct i2c_adapter *i2c, struct mt2266_config *cfg)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif // CONFIG_MEDIA_TUNER_MT2266

#endif
