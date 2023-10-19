/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Driver for Zarlink DVB-T ZL10353 demodulator
 *
 *  Copyright (C) 2006, 2007 Christopher Pascoe <c.pascoe@itee.uq.edu.au>
 */

#ifndef ZL10353_H
#define ZL10353_H

#include <linux/dvb/frontend.h>

struct zl10353_config
{
	/* demodulator's I2C address */
	u8 demod_address;

	/* frequencies in units of 0.1kHz */
	int adc_clock;	/* default: 450560 (45.056  MHz) */
	int if2;	/* default: 361667 (36.1667 MHz) */

	/* set if no pll is connected to the secondary i2c bus */
	int no_tuner;

	/* set if parallel ts output is required */
	int parallel_ts;

	/* set if i2c_gate_ctrl disable is required */
	u8 disable_i2c_gate_ctrl:1;

	/* clock control registers (0x51-0x54) */
	u8 clock_ctl_1;  /* default: 0x46 */
	u8 pll_0;        /* default: 0x15 */
};

#if IS_REACHABLE(CONFIG_DVB_ZL10353)
extern struct dvb_frontend* zl10353_attach(const struct zl10353_config *config,
					   struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend* zl10353_attach(const struct zl10353_config *config,
					   struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_ZL10353 */

#endif /* ZL10353_H */
