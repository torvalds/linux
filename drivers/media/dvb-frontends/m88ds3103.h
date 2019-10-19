/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Montage Technology M88DS3103/M88RS6000 demodulator driver
 *
 * Copyright (C) 2013 Antti Palosaari <crope@iki.fi>
 */

#ifndef M88DS3103_H
#define M88DS3103_H

#include <linux/dvb/frontend.h>

/*
 * I2C address
 * 0x68,
 */

/**
 * enum m88ds3103_ts_mode - TS connection mode
 * @M88DS3103_TS_SERIAL:	TS output pin D0, normal
 * @M88DS3103_TS_SERIAL_D7:	TS output pin D7
 * @M88DS3103_TS_PARALLEL:	TS Parallel mode
 * @M88DS3103_TS_CI:		TS CI Mode
 */
enum m88ds3103_ts_mode {
	M88DS3103_TS_SERIAL,
	M88DS3103_TS_SERIAL_D7,
	M88DS3103_TS_PARALLEL,
	M88DS3103_TS_CI
};

/**
 * enum m88ds3103_clock_out
 * @M88DS3103_CLOCK_OUT_DISABLED:	Clock output is disabled
 * @M88DS3103_CLOCK_OUT_ENABLED:	Clock output is enabled with crystal
 *					clock.
 * @M88DS3103_CLOCK_OUT_ENABLED_DIV2:	Clock output is enabled with half
 *					crystal clock.
 */
enum m88ds3103_clock_out {
	M88DS3103_CLOCK_OUT_DISABLED,
	M88DS3103_CLOCK_OUT_ENABLED,
	M88DS3103_CLOCK_OUT_ENABLED_DIV2
};

/**
 * struct m88ds3103_platform_data - Platform data for the m88ds3103 driver
 * @clk: Clock frequency.
 * @i2c_wr_max: Max bytes I2C adapter can write at once.
 * @ts_mode: TS mode.
 * @ts_clk: TS clock (KHz).
 * @ts_clk_pol: TS clk polarity. 1-active at falling edge; 0-active at rising
 *  edge.
 * @spec_inv: Input spectrum inversion.
 * @agc: AGC configuration.
 * @agc_inv: AGC polarity.
 * @clk_out: Clock output.
 * @envelope_mode: DiSEqC envelope mode.
 * @lnb_hv_pol: LNB H/V pin polarity. 0: pin high set to VOLTAGE_18, pin low to
 *  set VOLTAGE_13. 1: pin high set to VOLTAGE_13, pin low to set VOLTAGE_18.
 * @lnb_en_pol: LNB enable pin polarity. 0: pin high to disable, pin low to
 *  enable. 1: pin high to enable, pin low to disable.
 * @get_dvb_frontend: Get DVB frontend.
 * @get_i2c_adapter: Get I2C adapter.
 */
struct m88ds3103_platform_data {
	u32 clk;
	u16 i2c_wr_max;
	enum m88ds3103_ts_mode ts_mode;
	u32 ts_clk;
	enum m88ds3103_clock_out clk_out;
	u8 ts_clk_pol:1;
	u8 spec_inv:1;
	u8 agc;
	u8 agc_inv:1;
	u8 envelope_mode:1;
	u8 lnb_hv_pol:1;
	u8 lnb_en_pol:1;

	struct dvb_frontend* (*get_dvb_frontend)(struct i2c_client *);
	struct i2c_adapter* (*get_i2c_adapter)(struct i2c_client *);

/* private: For legacy media attach wrapper. Do not set value. */
	u8 attach_in_use:1;
};

/**
 * struct m88ds3103_config - m88ds3102 configuration
 *
 * @i2c_addr:	I2C address. Default: none, must set. Example: 0x68, ...
 * @clock:	Device's clock. Default: none, must set. Example: 27000000
 * @i2c_wr_max: Max bytes I2C provider is asked to write at once.
 *		Default: none, must set. Example: 33, 65, ...
 * @ts_mode:	TS output mode, as defined by &enum m88ds3103_ts_mode.
 *		Default: M88DS3103_TS_SERIAL.
 * @ts_clk:	TS clk in KHz. Default: 0.
 * @ts_clk_pol:	TS clk polarity.Default: 0.
 *		1-active at falling edge; 0-active at rising edge.
 * @spec_inv:	Spectrum inversion. Default: 0.
 * @agc_inv:	AGC polarity. Default: 0.
 * @clock_out:	Clock output, as defined by &enum m88ds3103_clock_out.
 *		Default: M88DS3103_CLOCK_OUT_DISABLED.
 * @envelope_mode: DiSEqC envelope mode. Default: 0.
 * @agc:	AGC configuration. Default: none, must set.
 * @lnb_hv_pol:	LNB H/V pin polarity. Default: 0. Values:
 *		1: pin high set to VOLTAGE_13, pin low to set VOLTAGE_18;
 *		0: pin high set to VOLTAGE_18, pin low to set VOLTAGE_13.
 * @lnb_en_pol:	LNB enable pin polarity. Default: 0. Values:
 *		1: pin high to enable, pin low to disable;
 *		0: pin high to disable, pin low to enable.
 */
struct m88ds3103_config {
	u8 i2c_addr;
	u32 clock;
	u16 i2c_wr_max;
	u8 ts_mode;
	u32 ts_clk;
	u8 ts_clk_pol:1;
	u8 spec_inv:1;
	u8 agc_inv:1;
	u8 clock_out;
	u8 envelope_mode:1;
	u8 agc;
	u8 lnb_hv_pol:1;
	u8 lnb_en_pol:1;
};

#if defined(CONFIG_DVB_M88DS3103) || \
		(defined(CONFIG_DVB_M88DS3103_MODULE) && defined(MODULE))
/**
 * Attach a m88ds3103 demod
 *
 * @config: pointer to &struct m88ds3103_config with demod configuration.
 * @i2c: i2c adapter to use.
 * @tuner_i2c: on success, returns the I2C adapter associated with
 *		m88ds3103 tuner.
 *
 * return: FE pointer on success, NULL on failure.
 * Note: Do not add new m88ds3103_attach() users! Use I2C bindings instead.
 */
extern struct dvb_frontend *m88ds3103_attach(
		const struct m88ds3103_config *config,
		struct i2c_adapter *i2c,
		struct i2c_adapter **tuner_i2c);
extern int m88ds3103_get_agc_pwm(struct dvb_frontend *fe, u8 *_agc_pwm);
#else
static inline struct dvb_frontend *m88ds3103_attach(
		const struct m88ds3103_config *config,
		struct i2c_adapter *i2c,
		struct i2c_adapter **tuner_i2c)
{
	pr_warn("%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#define m88ds3103_get_agc_pwm NULL
#endif

#endif
