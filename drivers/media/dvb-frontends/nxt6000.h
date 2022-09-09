/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
	NxtWave Communications - NXT6000 demodulator driver

    Copyright (C) 2002-2003 Florian Schirmer <jolt@tuxbox.org>
    Copyright (C) 2003 Paul Andreassen <paul@andreassen.com.au>

*/

#ifndef NXT6000_H
#define NXT6000_H

#include <linux/dvb/frontend.h>

struct nxt6000_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/* should clock inversion be used? */
	u8 clock_inversion:1;
};

#if IS_REACHABLE(CONFIG_DVB_NXT6000)
extern struct dvb_frontend* nxt6000_attach(const struct nxt6000_config* config,
					   struct i2c_adapter* i2c);
#else
static inline struct dvb_frontend* nxt6000_attach(const struct nxt6000_config* config,
					   struct i2c_adapter* i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif // CONFIG_DVB_NXT6000

#endif // NXT6000_H
