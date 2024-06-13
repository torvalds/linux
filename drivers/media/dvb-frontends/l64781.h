/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    driver for LSI L64781 COFDM demodulator

    Copyright (C) 2001 Holger Waechtler for Convergence Integrated Media GmbH
		       Marko Kohtala <marko.kohtala@luukku.com>


*/

#ifndef L64781_H
#define L64781_H

#include <linux/dvb/frontend.h>

struct l64781_config
{
	/* the demodulator's i2c address */
	u8 demod_address;
};

#if IS_REACHABLE(CONFIG_DVB_L64781)
extern struct dvb_frontend* l64781_attach(const struct l64781_config* config,
					  struct i2c_adapter* i2c);
#else
static inline struct dvb_frontend* l64781_attach(const struct l64781_config* config,
					  struct i2c_adapter* i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif // CONFIG_DVB_L64781

#endif // L64781_H
