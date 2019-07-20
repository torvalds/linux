/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Silicon Labs Si2168 DVB-T/T2/C demodulator driver
 *
 * Copyright (C) 2014 Antti Palosaari <crope@iki.fi>
 */

#ifndef SI2168_H
#define SI2168_H

#include <linux/dvb/frontend.h>
/*
 * I2C address
 * 0x64
 */
struct si2168_config {
	/*
	 * frontend
	 * returned by driver
	 */
	struct dvb_frontend **fe;

	/*
	 * tuner I2C adapter
	 * returned by driver
	 */
	struct i2c_adapter **i2c_adapter;

	/* TS mode */
#define SI2168_TS_PARALLEL	0x06
#define SI2168_TS_SERIAL	0x03
#define SI2168_TS_TRISTATE	0x00
	u8 ts_mode;

	/* TS clock inverted */
	bool ts_clock_inv;

	/* TS clock gapped */
	bool ts_clock_gapped;

	/* Inverted spectrum */
	bool spectral_inversion;
};

#endif
