/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    Driver for Grundig 29504-491, a Philips TDA8083 based QPSK Frontend

    Copyright (C) 2001 Convergence Integrated Media GmbH

    written by Ralph Metzler <ralph@convergence.de>

    adoption to the new DVB frontend API and diagnostic ioctl's
    by Holger Waechtler <holger@convergence.de>


*/

#ifndef TDA8083_H
#define TDA8083_H

#include <linux/dvb/frontend.h>

struct tda8083_config
{
	/* the demodulator's i2c address */
	u8 demod_address;
};

#if IS_REACHABLE(CONFIG_DVB_TDA8083)
extern struct dvb_frontend *tda8083_attach(const struct tda8083_config *config,
					   struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *tda8083_attach(const struct tda8083_config *config,
						  struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif // CONFIG_DVB_TDA8083

#endif // TDA8083_H
