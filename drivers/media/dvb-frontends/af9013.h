/*
 * Afatech AF9013 demodulator driver
 *
 * Copyright (C) 2007 Antti Palosaari <crope@iki.fi>
 * Copyright (C) 2011 Antti Palosaari <crope@iki.fi>
 *
 * Thanks to Afatech who kindly provided information.
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
 */

#ifndef AF9013_H
#define AF9013_H

#include <linux/dvb/frontend.h>

/*
 * I2C address: 0x1c, 0x1d
 */

/**
 * struct af9013_platform_data - Platform data for the af9013 driver
 * @clk: Clock frequency.
 * @tuner: Used tuner model.
 * @if_frequency: IF frequency.
 * @ts_mode: TS mode.
 * @ts_output_pin: TS output pin.
 * @spec_inv: Input spectrum inverted.
 * @api_version: Firmware API version.
 * @gpio: GPIOs.
 * @get_dvb_frontend: Get DVB frontend callback.
 *
 * AF9013/5 GPIOs (mostly guessed):
 *   * demod#1-gpio#0 - set demod#2 i2c-addr for dual devices
 *   * demod#1-gpio#1 - xtal setting (?)
 *   * demod#1-gpio#3 - tuner#1
 *   * demod#2-gpio#0 - tuner#2
 *   * demod#2-gpio#1 - xtal setting (?)
 */
struct af9013_platform_data {
	/*
	 * 20480000, 25000000, 28000000, 28800000
	 */
	u32 clk;
#define AF9013_TUNER_MXL5003D      3 /* MaxLinear */
#define AF9013_TUNER_MXL5005D     13 /* MaxLinear */
#define AF9013_TUNER_MXL5005R     30 /* MaxLinear */
#define AF9013_TUNER_ENV77H11D5  129 /* Panasonic */
#define AF9013_TUNER_MT2060      130 /* Microtune */
#define AF9013_TUNER_MC44S803    133 /* Freescale */
#define AF9013_TUNER_QT1010      134 /* Quantek */
#define AF9013_TUNER_UNKNOWN     140 /* for can tuners ? */
#define AF9013_TUNER_MT2060_2    147 /* Microtune */
#define AF9013_TUNER_TDA18271    156 /* NXP */
#define AF9013_TUNER_QT1010A     162 /* Quantek */
#define AF9013_TUNER_MXL5007T    177 /* MaxLinear */
#define AF9013_TUNER_TDA18218    179 /* NXP */
	u8 tuner;
	u32 if_frequency;
#define AF9013_TS_MODE_USB       0
#define AF9013_TS_MODE_PARALLEL  1
#define AF9013_TS_MODE_SERIAL    2
	u8 ts_mode;
	u8 ts_output_pin;
	bool spec_inv;
	u8 api_version[4];
#define AF9013_GPIO_ON (1 << 0)
#define AF9013_GPIO_EN (1 << 1)
#define AF9013_GPIO_O  (1 << 2)
#define AF9013_GPIO_I  (1 << 3)
#define AF9013_GPIO_LO (AF9013_GPIO_ON|AF9013_GPIO_EN)
#define AF9013_GPIO_HI (AF9013_GPIO_ON|AF9013_GPIO_EN|AF9013_GPIO_O)
#define AF9013_GPIO_TUNER_ON  (AF9013_GPIO_ON|AF9013_GPIO_EN)
#define AF9013_GPIO_TUNER_OFF (AF9013_GPIO_ON|AF9013_GPIO_EN|AF9013_GPIO_O)
	u8 gpio[4];

	struct dvb_frontend* (*get_dvb_frontend)(struct i2c_client *);

/* private: For legacy media attach wrapper. Do not set value. */
	bool attach_in_use;
	u8 i2c_addr;
	u32 clock;
};

#define af9013_config       af9013_platform_data
#define AF9013_TS_USB       AF9013_TS_MODE_USB
#define AF9013_TS_PARALLEL  AF9013_TS_MODE_PARALLEL
#define AF9013_TS_SERIAL    AF9013_TS_MODE_SERIAL

#if IS_REACHABLE(CONFIG_DVB_AF9013)
/**
 * Attach an af9013 demod
 *
 * @config: pointer to &struct af9013_config with demod configuration.
 * @i2c: i2c adapter to use.
 *
 * return: FE pointer on success, NULL on failure.
 */
extern struct dvb_frontend *af9013_attach(const struct af9013_config *config,
	struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *af9013_attach(
const struct af9013_config *config, struct i2c_adapter *i2c)
{
	pr_warn("%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_AF9013 */

#endif /* AF9013_H */
