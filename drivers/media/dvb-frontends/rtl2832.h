/*
 * Realtek RTL2832 DVB-T demodulator driver
 *
 * Copyright (C) 2012 Thomas Mair <thomas.mair86@gmail.com>
 * Copyright (C) 2012-2014 Antti Palosaari <crope@iki.fi>
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

#include <linux/dvb/frontend.h>
#include <linux/i2c-mux.h>

/**
 * struct rtl2832_platform_data - Platform data for the rtl2832 driver
 * @clk: Clock frequency (4000000, 16000000, 25000000, 28800000).
 * @tuner: Used tuner model.
 * @get_dvb_frontend: Get DVB frontend.
 * @get_i2c_adapter: Get I2C adapter.
 * @slave_ts_ctrl: Control slave TS interface.
 * @pid_filter: Set PID to PID filter.
 * @pid_filter_ctrl: Control PID filter.
 */
struct rtl2832_platform_data {
	u32 clk;
	/*
	 * XXX: This list must be kept sync with dvb_usb_rtl28xxu USB IF driver.
	 */
#define RTL2832_TUNER_FC2580    0x21
#define RTL2832_TUNER_TUA9001   0x24
#define RTL2832_TUNER_FC0012    0x26
#define RTL2832_TUNER_E4000     0x27
#define RTL2832_TUNER_FC0013    0x29
#define RTL2832_TUNER_R820T     0x2a
#define RTL2832_TUNER_R828D     0x2b
#define RTL2832_TUNER_SI2157    0x2c
	u8 tuner;

	struct dvb_frontend* (*get_dvb_frontend)(struct i2c_client *);
	struct i2c_adapter* (*get_i2c_adapter)(struct i2c_client *);
	int (*slave_ts_ctrl)(struct i2c_client *, bool);
	int (*pid_filter)(struct dvb_frontend *, u8, u16, int);
	int (*pid_filter_ctrl)(struct dvb_frontend *, int);
/* private: Register access for SDR module use only */
	struct regmap *regmap;
};

#endif /* RTL2832_H */
