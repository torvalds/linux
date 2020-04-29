/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
*/

#ifndef __MT20XX_H__
#define __MT20XX_H__

#include <linux/i2c.h>
#include <media/dvb_frontend.h>

#if IS_REACHABLE(CONFIG_MEDIA_TUNER_MT20XX)
extern struct dvb_frontend *microtune_attach(struct dvb_frontend *fe,
					     struct i2c_adapter* i2c_adap,
					     u8 i2c_addr);
#else
static inline struct dvb_frontend *microtune_attach(struct dvb_frontend *fe,
					     struct i2c_adapter* i2c_adap,
					     u8 i2c_addr)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif /* __MT20XX_H__ */
