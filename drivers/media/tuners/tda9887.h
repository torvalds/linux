/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
*/

#ifndef __TDA9887_H__
#define __TDA9887_H__

#include <linux/i2c.h>
#include <media/dvb_frontend.h>

/* ------------------------------------------------------------------------ */
#if IS_REACHABLE(CONFIG_MEDIA_TUNER_TDA9887)
extern struct dvb_frontend *tda9887_attach(struct dvb_frontend *fe,
					   struct i2c_adapter *i2c_adap,
					   u8 i2c_addr);
#else
static inline struct dvb_frontend *tda9887_attach(struct dvb_frontend *fe,
						  struct i2c_adapter *i2c_adap,
						  u8 i2c_addr)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif /* __TDA9887_H__ */
