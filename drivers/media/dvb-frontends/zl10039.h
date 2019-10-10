/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    Driver for Zarlink ZL10039 DVB-S tuner

    Copyright (C) 2007 Jan D. Louw <jd.louw@mweb.co.za>

*/

#ifndef ZL10039_H
#define ZL10039_H

#if IS_REACHABLE(CONFIG_DVB_ZL10039)
struct dvb_frontend *zl10039_attach(struct dvb_frontend *fe,
					u8 i2c_addr,
					struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *zl10039_attach(struct dvb_frontend *fe,
					u8 i2c_addr,
					struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_ZL10039 */

#endif /* ZL10039_H */
