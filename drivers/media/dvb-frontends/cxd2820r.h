/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Sony CXD2820R demodulator driver
 *
 * Copyright (C) 2010 Antti Palosaari <crope@iki.fi>
 */


#ifndef CXD2820R_H
#define CXD2820R_H

#include <linux/dvb/frontend.h>

#define CXD2820R_GPIO_D (0 << 0) /* disable */
#define CXD2820R_GPIO_E (1 << 0) /* enable */
#define CXD2820R_GPIO_O (0 << 1) /* output */
#define CXD2820R_GPIO_I (1 << 1) /* input */
#define CXD2820R_GPIO_L (0 << 2) /* output low */
#define CXD2820R_GPIO_H (1 << 2) /* output high */

#define CXD2820R_TS_SERIAL        0x08
#define CXD2820R_TS_SERIAL_MSB    0x28
#define CXD2820R_TS_PARALLEL      0x30
#define CXD2820R_TS_PARALLEL_MSB  0x70

/*
 * I2C address: 0x6c, 0x6d
 */

/**
 * struct cxd2820r_platform_data - Platform data for the cxd2820r driver
 * @ts_mode: TS mode.
 * @ts_clk_inv: TS clock inverted.
 * @if_agc_polarity: IF AGC polarity.
 * @spec_inv: Input spectrum inverted.
 * @gpio_chip_base: GPIO.
 * @get_dvb_frontend: Get DVB frontend.
 */
struct cxd2820r_platform_data {
	u8 ts_mode;
	bool ts_clk_inv;
	bool if_agc_polarity;
	bool spec_inv;
	int **gpio_chip_base;

	struct dvb_frontend* (*get_dvb_frontend)(struct i2c_client *);
/* private: For legacy media attach wrapper. Do not set value. */
	bool attach_in_use;
};

/**
 * struct cxd2820r_config - configuration for cxd2020r demod
 *
 * @i2c_address: Demodulator I2C address. Driver determines DVB-C slave I2C
 *		 address automatically from master address.
 *		 Default: none, must set. Values: 0x6c, 0x6d.
 * @ts_mode:	TS output mode. Default: none, must set. Values: FIXME?
 * @ts_clock_inv: TS clock inverted. Default: 0. Values: 0, 1.
 * @if_agc_polarity: Default: 0. Values: 0, 1
 * @spec_inv:	Spectrum inversion. Default: 0. Values: 0, 1.
 */
struct cxd2820r_config {
	/* Demodulator I2C address.
	 * Driver determines DVB-C slave I2C address automatically from master
	 * address.
	 * Default: none, must set
	 * Values: 0x6c, 0x6d
	 */
	u8 i2c_address;

	/* TS output mode.
	 * Default: none, must set.
	 * Values:
	 */
	u8 ts_mode;

	/* TS clock inverted.
	 * Default: 0
	 * Values: 0, 1
	 */
	bool ts_clock_inv;

	/* IF AGC polarity.
	 * Default: 0
	 * Values: 0, 1
	 */
	bool if_agc_polarity;

	/* Spectrum inversion.
	 * Default: 0
	 * Values: 0, 1
	 */
	bool spec_inv;
};


#if IS_REACHABLE(CONFIG_DVB_CXD2820R)
/**
 * Attach a cxd2820r demod
 *
 * @config: pointer to &struct cxd2820r_config with demod configuration.
 * @i2c: i2c adapter to use.
 * @gpio_chip_base: if zero, disables GPIO setting. Otherwise, if
 *		    CONFIG_GPIOLIB is set dynamically allocate
 *		    gpio base; if is not set, use its value to
 *		    setup the GPIO pins.
 *
 * return: FE pointer on success, NULL on failure.
 */
extern struct dvb_frontend *cxd2820r_attach(
	const struct cxd2820r_config *config,
	struct i2c_adapter *i2c,
	int *gpio_chip_base
);
#else
static inline struct dvb_frontend *cxd2820r_attach(
	const struct cxd2820r_config *config,
	struct i2c_adapter *i2c,
	int *gpio_chip_base
)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}

#endif

#endif /* CXD2820R_H */
