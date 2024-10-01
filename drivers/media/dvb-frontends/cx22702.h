/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    Conexant 22702 DVB OFDM demodulator driver

    based on:
	Alps TDMB7 DVB OFDM demodulator driver

    Copyright (C) 2001-2002 Convergence Integrated Media GmbH
	  Holger Waechtler <holger@convergence.de>

    Copyright (C) 2004 Steven Toth <stoth@linuxtv.org>


*/

#ifndef CX22702_H
#define CX22702_H

#include <linux/dvb/frontend.h>

struct cx22702_config {
	/* the demodulator's i2c address */
	u8 demod_address;

	/* serial/parallel output */
#define CX22702_PARALLEL_OUTPUT 0
#define CX22702_SERIAL_OUTPUT   1
	u8 output_mode;
};

#if IS_REACHABLE(CONFIG_DVB_CX22702)
extern struct dvb_frontend *cx22702_attach(
	const struct cx22702_config *config,
	struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *cx22702_attach(
	const struct cx22702_config *config,
	struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif
