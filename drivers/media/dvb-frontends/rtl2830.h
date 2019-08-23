/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Realtek RTL2830 DVB-T demodulator driver
 *
 * Copyright (C) 2011 Antti Palosaari <crope@iki.fi>
 */

#ifndef RTL2830_H
#define RTL2830_H

#include <linux/dvb/frontend.h>

/**
 * struct rtl2830_platform_data - Platform data for the rtl2830 driver
 * @clk: Clock frequency (4000000, 16000000, 25000000, 28800000).
 * @spec_inv: Spectrum inversion.
 * @vtop: AGC take-over point.
 * @krf: AGC ratio.
 * @agc_targ_val: AGC.
 * @get_dvb_frontend: Get DVB frontend.
 * @get_i2c_adapter: Get I2C adapter.
 * @pid_filter: Set PID to PID filter.
 * @pid_filter_ctrl: Control PID filter.
 */
struct rtl2830_platform_data {
	u32 clk;
	bool spec_inv;
	u8 vtop;
	u8 krf;
	u8 agc_targ_val;

	struct dvb_frontend* (*get_dvb_frontend)(struct i2c_client *);
	struct i2c_adapter* (*get_i2c_adapter)(struct i2c_client *);
	int (*pid_filter)(struct dvb_frontend *, u8, u16, int);
	int (*pid_filter_ctrl)(struct dvb_frontend *, int);
};

#endif /* RTL2830_H */
