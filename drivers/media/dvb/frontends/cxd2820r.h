/*
 * Sony CXD2820R demodulator driver
 *
 * Copyright (C) 2010 Antti Palosaari <crope@iki.fi>
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

	/* IFs for all used modes.
	 * Default: none, must set
	 * Values: <kHz>
	 */
	u16 if_dvbt_6;
	u16 if_dvbt_7;
	u16 if_dvbt_8;
	u16 if_dvbt2_5;
	u16 if_dvbt2_6;
	u16 if_dvbt2_7;
	u16 if_dvbt2_8;
	u16 if_dvbc;

	/* GPIOs for all used modes.
	 * Default: none, disabled
	 * Values: <see above>
	 */
	u8 gpio_dvbt[3];
	u8 gpio_dvbt2[3];
	u8 gpio_dvbc[3];
};


#if defined(CONFIG_DVB_CXD2820R) || \
	(defined(CONFIG_DVB_CXD2820R_MODULE) && defined(MODULE))
extern struct dvb_frontend *cxd2820r_attach(
	const struct cxd2820r_config *config,
	struct i2c_adapter *i2c,
	struct dvb_frontend *fe
);
#else
static inline struct dvb_frontend *cxd2820r_attach(
	const struct cxd2820r_config *config,
	struct i2c_adapter *i2c,
	struct dvb_frontend *fe
)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}

#endif

#endif /* CXD2820R_H */
