/*
 * Realtek RTL2832 DVB-T demodulator driver
 *
 * Copyright (C) 2012 Thomas Mair <thomas.mair86@gmail.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License along
 *	with this program; if not, write to the Free Software Foundation, Inc.,
 *	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef RTL2832_H
#define RTL2832_H

#include <linux/kconfig.h>
#include <linux/dvb/frontend.h>

struct rtl2832_config {
	/*
	 * Demodulator I2C address.
	 */
	u8 i2c_addr;

	/*
	 * Xtal frequency.
	 * Hz
	 * 4000000, 16000000, 25000000, 28800000
	 */
	u32 xtal;

	/*
	 * IFs for all used modes.
	 * Hz
	 * 4570000, 4571429, 36000000, 36125000, 36166667, 44000000
	 */
	u32 if_dvbt;

	/*
	 * tuner
	 * XXX: This must be keep sync with dvb_usb_rtl28xxu demod driver.
	 */
#define RTL2832_TUNER_TUA9001   0x24
#define RTL2832_TUNER_FC0012    0x26
#define RTL2832_TUNER_E4000     0x27
#define RTL2832_TUNER_FC0013    0x29
#define RTL2832_TUNER_R820T	0x2a
#define RTL2832_TUNER_R828D	0x2b
	u8 tuner;
};

#if IS_ENABLED(CONFIG_DVB_RTL2832)
extern struct dvb_frontend *rtl2832_attach(
	const struct rtl2832_config *cfg,
	struct i2c_adapter *i2c
);
#else
static inline struct dvb_frontend *rtl2832_attach(
	const struct rtl2832_config *config,
	struct i2c_adapter *i2c
)
{
	pr_warn("%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif


#endif /* RTL2832_H */
