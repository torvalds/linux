/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Driver for Zarlink DVB-T MT352 demodulator
 *
 *  Written by Holger Waechtler <holger@qanu.de>
 *	 and Daniel Mack <daniel@qanu.de>
 *
 *  AVerMedia AVerTV DVB-T 771 support by
 *       Wolfram Joost <dbox2@frokaschwei.de>
 *
 *  Support for Samsung TDTC9251DH01C(M) tuner
 *  Copyright (C) 2004 Antonio Mancuso <antonio.mancuso@digitaltelevision.it>
 *                     Amauri  Celani  <acelani@essegi.net>
 *
 *  DVICO FusionHDTV DVB-T1 and DVICO FusionHDTV DVB-T Lite support by
 *       Christopher Pascoe <c.pascoe@itee.uq.edu.au>
 */

#ifndef MT352_H
#define MT352_H

#include <linux/dvb/frontend.h>

struct mt352_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/* frequencies in kHz */
	int adc_clock;  // default: 20480
	int if2;        // default: 36166

	/* set if no pll is connected to the secondary i2c bus */
	int no_tuner;

	/* Initialise the demodulator and PLL. Cannot be NULL */
	int (*demod_init)(struct dvb_frontend* fe);
};

#if IS_REACHABLE(CONFIG_DVB_MT352)
extern struct dvb_frontend* mt352_attach(const struct mt352_config* config,
					 struct i2c_adapter* i2c);
#else
static inline struct dvb_frontend* mt352_attach(const struct mt352_config* config,
					 struct i2c_adapter* i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif // CONFIG_DVB_MT352

static inline int mt352_write(struct dvb_frontend *fe, const u8 buf[], int len) {
	int r = 0;
	if (fe->ops.write)
		r = fe->ops.write(fe, buf, len);
	return r;
}

#endif // MT352_H
