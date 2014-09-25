/*
 * Afatech AF9033 demodulator driver
 *
 * Copyright (C) 2009 Antti Palosaari <crope@iki.fi>
 * Copyright (C) 2012 Antti Palosaari <crope@iki.fi>
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
 *
 *    You should have received a copy of the GNU General Public License along
 *    with this program; if not, write to the Free Software Foundation, Inc.,
 *    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef AF9033_H
#define AF9033_H

#include <linux/kconfig.h>

struct af9033_config {
	/*
	 * I2C address
	 */
	u8 i2c_addr;

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
};


struct af9033_ops {
	int (*pid_filter_ctrl)(struct dvb_frontend *fe, int onoff);
	int (*pid_filter)(struct dvb_frontend *fe, int index, u16 pid,
			  int onoff);
};


#if IS_ENABLED(CONFIG_DVB_AF9033)
extern
struct dvb_frontend *af9033_attach(const struct af9033_config *config,
				   struct i2c_adapter *i2c,
				   struct af9033_ops *ops);

#else
static inline
struct dvb_frontend *af9033_attach(const struct af9033_config *config,
				   struct i2c_adapter *i2c,
				   struct af9033_ops *ops)
{
	pr_warn("%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}

static inline int af9033_pid_filter_ctrl(struct dvb_frontend *fe, int onoff)
{
	pr_warn("%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

static inline int af9033_pid_filter(struct dvb_frontend *fe, int index, u16 pid,
	int onoff)
{
	pr_warn("%s: driver disabled by Kconfig\n", __func__);
	return -ENODEV;
}

#endif

#endif /* AF9033_H */
