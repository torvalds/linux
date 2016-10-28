/*
 *    Support for AltoBeam GB20600 (a.k.a DMB-TH) demodulator
 *    ATBM8830, ATBM8831
 *
 *    Copyright (C) 2009 David T.L. Wong <davidtlwong@gmail.com>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 */

#ifndef __ATBM8830_H__
#define __ATBM8830_H__

#include <linux/dvb/frontend.h>
#include <linux/i2c.h>

#define ATBM8830_PROD_8830 0
#define ATBM8830_PROD_8831 1

struct atbm8830_config {

	/* product type */
	u8 prod;

	/* the demodulator's i2c address */
	u8 demod_address;

	/* parallel or serial transport stream */
	u8 serial_ts;

	/* transport stream clock output only when receiving valid stream */
	u8 ts_clk_gated;

	/* Decoder sample TS data at rising edge of clock */
	u8 ts_sampling_edge;

	/* Oscillator clock frequency */
	u32 osc_clk_freq; /* in kHz */

	/* IF frequency */
	u32 if_freq; /* in kHz */

	/* Swap I/Q for zero IF */
	u8 zif_swap_iq;

	/* Tuner AGC settings */
	u8 agc_min;
	u8 agc_max;
	u8 agc_hold_loop;
};

#if IS_REACHABLE(CONFIG_DVB_ATBM8830)
extern struct dvb_frontend *atbm8830_attach(const struct atbm8830_config *config,
		struct i2c_adapter *i2c);
#else
static inline
struct dvb_frontend *atbm8830_attach(const struct atbm8830_config *config,
		struct i2c_adapter *i2c) {
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_ATBM8830 */

#endif /* __ATBM8830_H__ */
