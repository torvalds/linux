/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
	TDA665x tuner driver
	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

*/

#ifndef __TDA665x_H
#define __TDA665x_H

struct tda665x_config {
	char name[128];

	u8	addr;
	u32	frequency_min;
	u32	frequency_max;
	u32	frequency_offst;
	u32	ref_multiplier;
	u32	ref_divider;
};

#if IS_REACHABLE(CONFIG_DVB_TDA665x)

extern struct dvb_frontend *tda665x_attach(struct dvb_frontend *fe,
					   const struct tda665x_config *config,
					   struct i2c_adapter *i2c);

#else

static inline struct dvb_frontend *tda665x_attach(struct dvb_frontend *fe,
						  const struct tda665x_config *config,
						  struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: Driver disabled by Kconfig\n", __func__);
	return NULL;
}

#endif /* CONFIG_DVB_TDA665x */

#endif /* __TDA665x_H */
