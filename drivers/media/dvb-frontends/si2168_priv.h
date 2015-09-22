/*
 * Silicon Labs Si2168 DVB-T/T2/C demodulator driver
 *
 * Copyright (C) 2014 Antti Palosaari <crope@iki.fi>
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
 */

#ifndef SI2168_PRIV_H
#define SI2168_PRIV_H

#include "si2168.h"
#include "dvb_frontend.h"
#include <linux/firmware.h>
#include <linux/i2c-mux.h>

#define SI2168_A20_FIRMWARE "dvb-demod-si2168-a20-01.fw"
#define SI2168_A30_FIRMWARE "dvb-demod-si2168-a30-01.fw"
#define SI2168_B40_FIRMWARE "dvb-demod-si2168-b40-01.fw"
#define SI2168_B40_FIRMWARE_FALLBACK "dvb-demod-si2168-02.fw"

/* state struct */
struct si2168_dev {
	struct i2c_adapter *adapter;
	struct dvb_frontend fe;
	enum fe_delivery_system delivery_system;
	enum fe_status fe_status;
	bool active;
	bool fw_loaded;
	u8 ts_mode;
	bool ts_clock_inv;
	bool ts_clock_gapped;
};

/* firmware command struct */
#define SI2168_ARGLEN      30
struct si2168_cmd {
	u8 args[SI2168_ARGLEN];
	unsigned wlen;
	unsigned rlen;
};

#endif
