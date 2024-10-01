/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    Conexant cx24123/cx24109 - DVB QPSK Satellite demod/tuner driver

    Copyright (C) 2005 Steven Toth <stoth@linuxtv.org>

*/

#ifndef CX24123_H
#define CX24123_H

#include <linux/dvb/frontend.h>

struct cx24123_config {
	/* the demodulator's i2c address */
	u8 demod_address;

	/* Need to set device param for start_dma */
	int (*set_ts_params)(struct dvb_frontend *fe, int is_punctured);

	/* 0 = LNB voltage normal, 1 = LNB voltage inverted */
	int lnb_polarity;

	/* this device has another tuner */
	u8 dont_use_pll;
	void (*agc_callback) (struct dvb_frontend *);
};

#if IS_REACHABLE(CONFIG_DVB_CX24123)
extern struct dvb_frontend *cx24123_attach(const struct cx24123_config *config,
					   struct i2c_adapter *i2c);
extern struct i2c_adapter *cx24123_get_tuner_i2c_adapter(struct dvb_frontend *);
#else
static inline struct dvb_frontend *cx24123_attach(
	const struct cx24123_config *config, struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
static inline struct i2c_adapter *
	cx24123_get_tuner_i2c_adapter(struct dvb_frontend *fe)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif /* CX24123_H */
