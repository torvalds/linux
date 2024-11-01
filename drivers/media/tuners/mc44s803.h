/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Driver for Freescale MC44S803 Low Power CMOS Broadband Tuner
 *
 *  Copyright (c) 2009 Jochen Friedrich <jochen@scram.de>
 */

#ifndef MC44S803_H
#define MC44S803_H

struct dvb_frontend;
struct i2c_adapter;

struct mc44s803_config {
	u8 i2c_address;
	u8 dig_out;
};

#if IS_REACHABLE(CONFIG_MEDIA_TUNER_MC44S803)
extern struct dvb_frontend *mc44s803_attach(struct dvb_frontend *fe,
	 struct i2c_adapter *i2c, struct mc44s803_config *cfg);
#else
static inline struct dvb_frontend *mc44s803_attach(struct dvb_frontend *fe,
	 struct i2c_adapter *i2c, struct mc44s803_config *cfg)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_MEDIA_TUNER_MC44S803 */

#endif
