/*
 * Afatech AF9013 demodulator driver
 *
 * Copyright (C) 2007 Antti Palosaari <crope@iki.fi>
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
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef _AF9013_H_
#define _AF9013_H_

#include <linux/dvb/frontend.h>

enum af9013_ts_mode {
	AF9013_OUTPUT_MODE_PARALLEL,
	AF9013_OUTPUT_MODE_SERIAL,
	AF9013_OUTPUT_MODE_USB, /* only for AF9015 */
};

enum af9013_tuner {
	AF9013_TUNER_MXL5003D   =   3, /* MaxLinear */
	AF9013_TUNER_MXL5005D   =  13, /* MaxLinear */
	AF9013_TUNER_MXL5005R   =  30, /* MaxLinear */
	AF9013_TUNER_ENV77H11D5 = 129, /* Panasonic */
	AF9013_TUNER_MT2060     = 130, /* Microtune */
	AF9013_TUNER_MC44S803   = 133, /* Freescale */
	AF9013_TUNER_QT1010     = 134, /* Quantek */
	AF9013_TUNER_UNKNOWN    = 140, /* for can tuners ? */
	AF9013_TUNER_MT2060_2   = 147, /* Microtune */
	AF9013_TUNER_TDA18271   = 156, /* NXP */
	AF9013_TUNER_QT1010A    = 162, /* Quantek */
	AF9013_TUNER_TDA18218   = 179, /* NXP */
};

/* AF9013/5 GPIOs (mostly guessed)
   demod#1-gpio#0 - set demod#2 i2c-addr for dual devices
   demod#1-gpio#1 - xtal setting (?)
   demod#1-gpio#3 - tuner#1
   demod#2-gpio#0 - tuner#2
   demod#2-gpio#1 - xtal setting (?)
*/
#define AF9013_GPIO_ON (1 << 0)
#define AF9013_GPIO_EN (1 << 1)
#define AF9013_GPIO_O  (1 << 2)
#define AF9013_GPIO_I  (1 << 3)

#define AF9013_GPIO_LO (AF9013_GPIO_ON|AF9013_GPIO_EN)
#define AF9013_GPIO_HI (AF9013_GPIO_ON|AF9013_GPIO_EN|AF9013_GPIO_O)

#define AF9013_GPIO_TUNER_ON  (AF9013_GPIO_ON|AF9013_GPIO_EN)
#define AF9013_GPIO_TUNER_OFF (AF9013_GPIO_ON|AF9013_GPIO_EN|AF9013_GPIO_O)

struct af9013_config {
	/* demodulator's I2C address */
	u8 demod_address;

	/* frequencies in kHz */
	u32 adc_clock;

	/* tuner ID */
	u8 tuner;

	/* tuner IF */
	u16 tuner_if;

	/* TS data output mode */
	u8 output_mode:2;

	/* RF spectrum inversion */
	u8 rf_spec_inv:1;

	/* API version */
	u8 api_version[4];

	/* GPIOs */
	u8 gpio[4];
};


#if defined(CONFIG_DVB_AF9013) || \
	(defined(CONFIG_DVB_AF9013_MODULE) && defined(MODULE))
extern struct dvb_frontend *af9013_attach(const struct af9013_config *config,
	struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *af9013_attach(
const struct af9013_config *config, struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_AF9013 */

#endif /* _AF9013_H_ */
