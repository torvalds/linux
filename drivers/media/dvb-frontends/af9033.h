/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Afatech AF9033 demodulator driver
 *
 * Copyright (C) 2009 Antti Palosaari <crope@iki.fi>
 * Copyright (C) 2012 Antti Palosaari <crope@iki.fi>
 */

#ifndef AF9033_H
#define AF9033_H

/*
 * I2C address: 0x1c, 0x1d, 0x1e, 0x1f
 */
struct af9033_config {
	/*
	 * clock Hz
	 * 12000000, 22000000, 24000000, 34000000, 32000000, 28000000, 26000000,
	 * 30000000, 36000000, 20480000, 16384000
	 */
	u32 clock;

	/*
	 * ADC multiplier
	 */
#define AF9033_ADC_MULTIPLIER_1X   0
#define AF9033_ADC_MULTIPLIER_2X   1
	u8 adc_multiplier;

	/*
	 * tuner
	 */
#define AF9033_TUNER_TUA9001     0x27 /* Infineon TUA 9001 */
#define AF9033_TUNER_FC0011      0x28 /* Fitipower FC0011 */
#define AF9033_TUNER_FC0012      0x2e /* Fitipower FC0012 */
#define AF9033_TUNER_MXL5007T    0xa0 /* MaxLinear MxL5007T */
#define AF9033_TUNER_TDA18218    0xa1 /* NXP TDA 18218HN */
#define AF9033_TUNER_FC2580      0x32 /* FCI FC2580 */
/* 50-5f Omega */
#define AF9033_TUNER_IT9135_38   0x38 /* Omega */
#define AF9033_TUNER_IT9135_51   0x51 /* Omega LNA config 1 */
#define AF9033_TUNER_IT9135_52   0x52 /* Omega LNA config 2 */
/* 60-6f Omega v2 */
#define AF9033_TUNER_IT9135_60   0x60 /* Omega v2 */
#define AF9033_TUNER_IT9135_61   0x61 /* Omega v2 LNA config 1 */
#define AF9033_TUNER_IT9135_62   0x62 /* Omega v2 LNA config 2 */
	u8 tuner;

	/*
	 * TS settings
	 */
#define AF9033_TS_MODE_USB       0
#define AF9033_TS_MODE_PARALLEL  1
#define AF9033_TS_MODE_SERIAL    2
	u8 ts_mode:2;

	/*
	 * input spectrum inversion
	 */
	bool spec_inv;

	/*
	 *
	 */
	bool dyn0_clk;

	/*
	 * PID filter ops
	 */
	struct af9033_ops *ops;

	/*
	 * frontend
	 * returned by that driver
	 */
	struct dvb_frontend **fe;

	/*
	 * regmap for IT913x integrated tuner driver
	 * returned by that driver
	 */
	struct regmap *regmap;
};

struct af9033_ops {
	int (*pid_filter_ctrl)(struct dvb_frontend *fe, int onoff);
	int (*pid_filter)(struct dvb_frontend *fe, int index, u16 pid,
			  int onoff);
};

#endif /* AF9033_H */
