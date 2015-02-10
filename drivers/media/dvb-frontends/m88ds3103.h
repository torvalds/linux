/*
 * Montage M88DS3103 demodulator driver
 *
 * Copyright (C) 2013 Antti Palosaari <crope@iki.fi>
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

#ifndef M88DS3103_H
#define M88DS3103_H

#include <linux/dvb/frontend.h>

struct m88ds3103_config {
	/*
	 * I2C address
	 * Default: none, must set
	 * 0x68, ...
	 */
	u8 i2c_addr;

	/*
	 * clock
	 * Default: none, must set
	 * 27000000
	 */
	u32 clock;

	/*
	 * max bytes I2C provider is asked to write at once
	 * Default: none, must set
	 * 33, 65, ...
	 */
	u16 i2c_wr_max;

	/*
	 * TS output mode
	 * Default: M88DS3103_TS_SERIAL
	 */
#define M88DS3103_TS_SERIAL             0 /* TS output pin D0, normal */
#define M88DS3103_TS_SERIAL_D7          1 /* TS output pin D7 */
#define M88DS3103_TS_PARALLEL           2 /* TS Parallel mode */
#define M88DS3103_TS_CI                 3 /* TS CI Mode */
	u8 ts_mode;

	/*
	 * TS clk in KHz
	 * Default: 0.
	 */
	u32 ts_clk;

	/*
	 * TS clk polarity.
	 * Default: 0. 1-active at falling edge; 0-active at rising edge.
	 */
	u8 ts_clk_pol:1;

	/*
	 * spectrum inversion
	 * Default: 0
	 */
	u8 spec_inv:1;

	/*
	 * AGC polarity
	 * Default: 0
	 */
	u8 agc_inv:1;

	/*
	 * clock output
	 * Default: M88DS3103_CLOCK_OUT_DISABLED
	 */
#define M88DS3103_CLOCK_OUT_DISABLED        0
#define M88DS3103_CLOCK_OUT_ENABLED         1
#define M88DS3103_CLOCK_OUT_ENABLED_DIV2    2
	u8 clock_out;

	/*
	 * DiSEqC envelope mode
	 * Default: 0
	 */
	u8 envelope_mode:1;

	/*
	 * AGC configuration
	 * Default: none, must set
	 */
	u8 agc;

	/*
	 * LNB H/V pin polarity
	 * Default: 0.
	 * 1: pin high set to VOLTAGE_13, pin low to set VOLTAGE_18.
	 * 0: pin high set to VOLTAGE_18, pin low to set VOLTAGE_13.
	 */
	u8 lnb_hv_pol:1;

	/*
	 * LNB enable pin polarity
	 * Default: 0.
	 * 1: pin high to enable, pin low to disable.
	 * 0: pin high to disable, pin low to enable.
	 */
	u8 lnb_en_pol:1;
};

/*
 * Driver implements own I2C-adapter for tuner I2C access. That's since chip
 * has I2C-gate control which closes gate automatically after I2C transfer.
 * Using own I2C adapter we can workaround that.
 */

#if defined(CONFIG_DVB_M88DS3103) || \
		(defined(CONFIG_DVB_M88DS3103_MODULE) && defined(MODULE))
extern struct dvb_frontend *m88ds3103_attach(
		const struct m88ds3103_config *config,
		struct i2c_adapter *i2c,
		struct i2c_adapter **tuner_i2c);
#else
static inline struct dvb_frontend *m88ds3103_attach(
		const struct m88ds3103_config *config,
		struct i2c_adapter *i2c,
		struct i2c_adapter **tuner_i2c)
{
	pr_warn("%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif
