/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Driver for Microtune MT2266 "Direct conversion low power broadband tuner"
 *
 *  Copyright (c) 2007 Olivier DANET <odanet@caramail.com>
 */

#ifndef MT2266_H
#define MT2266_H

struct dvb_frontend;
struct i2c_adapter;

struct mt2266_config {
	u8 i2c_address;
};

#if IS_REACHABLE(CONFIG_MEDIA_TUNER_MT2266)
extern struct dvb_frontend * mt2266_attach(struct dvb_frontend *fe, struct i2c_adapter *i2c, struct mt2266_config *cfg);
#else
static inline struct dvb_frontend * mt2266_attach(struct dvb_frontend *fe, struct i2c_adapter *i2c, struct mt2266_config *cfg)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif // CONFIG_MEDIA_TUNER_MT2266

#endif
