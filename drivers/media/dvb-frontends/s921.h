/*
 *   Sharp s921 driver
 *
 *   Copyright (C) 2009 Mauro Carvalho Chehab
 *   Copyright (C) 2009 Douglas Landgraf <dougsland@redhat.com>
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License as
 *   published by the Free Software Foundation version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 */

#ifndef S921_H
#define S921_H

#include <linux/kconfig.h>
#include <linux/dvb/frontend.h>

struct s921_config {
	/* the demodulator's i2c address */
	u8 demod_address;
};

#if IS_ENABLED(CONFIG_DVB_S921)
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
static struct i2c_adapter *
	s921_get_tuner_i2c_adapter(struct dvb_frontend *fe)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif /* S921_H */
