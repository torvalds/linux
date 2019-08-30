/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    Conexant CX22700 DVB OFDM demodulator driver

    Copyright (C) 2001-2002 Convergence Integrated Media GmbH
	Holger Waechtler <holger@convergence.de>


*/

#ifndef CX22700_H
#define CX22700_H

#include <linux/dvb/frontend.h>

struct cx22700_config
{
	/* the demodulator's i2c address */
	u8 demod_address;
};

#if IS_REACHABLE(CONFIG_DVB_CX22700)
extern struct dvb_frontend* cx22700_attach(const struct cx22700_config* config,
					   struct i2c_adapter* i2c);
#else
static inline struct dvb_frontend* cx22700_attach(const struct cx22700_config* config,
					   struct i2c_adapter* i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif // CONFIG_DVB_CX22700

#endif // CX22700_H
