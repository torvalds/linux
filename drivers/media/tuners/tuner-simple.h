/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
*/

#ifndef __TUNER_SIMPLE_H__
#define __TUNER_SIMPLE_H__

#include <linux/i2c.h>
#include <media/dvb_frontend.h>

#if IS_REACHABLE(CONFIG_MEDIA_TUNER_SIMPLE)
extern struct dvb_frontend *simple_tuner_attach(struct dvb_frontend *fe,
						struct i2c_adapter *i2c_adap,
						u8 i2c_addr,
						unsigned int type);
#else
static inline struct dvb_frontend *simple_tuner_attach(struct dvb_frontend *fe,
						       struct i2c_adapter *i2c_adap,
						       u8 i2c_addr,
						       unsigned int type)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif /* __TUNER_SIMPLE_H__ */
