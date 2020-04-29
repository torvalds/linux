/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *   Sharp s921 driver
 *
 *   Copyright (C) 2009 Mauro Carvalho Chehab
 *   Copyright (C) 2009 Douglas Landgraf <dougsland@redhat.com>
 */

#ifndef S921_H
#define S921_H

#include <linux/dvb/frontend.h>

struct s921_config {
	/* the demodulator's i2c address */
	u8 demod_address;
};

#if IS_REACHABLE(CONFIG_DVB_S921)
extern struct dvb_frontend *s921_attach(const struct s921_config *config,
					   struct i2c_adapter *i2c);
extern struct i2c_adapter *s921_get_tuner_i2c_adapter(struct dvb_frontend *);
#else
static inline struct dvb_frontend *s921_attach(
	const struct s921_config *config, struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
static inline struct i2c_adapter *
	s921_get_tuner_i2c_adapter(struct dvb_frontend *fe)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif /* S921_H */
