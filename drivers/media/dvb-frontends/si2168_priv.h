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

#define SI2168_FIRMWARE "dvb-demod-si2168-01.fw"

/* state struct */
struct si2168 {
	struct i2c_client *client;
	struct i2c_adapter *adapter;
	struct mutex i2c_mutex;
	struct dvb_frontend fe;
	fe_delivery_system_t delivery_system;
	fe_status_t fe_status;
	bool active;
};

/* firmare command struct */
#define SI2157_ARGLEN      30
struct si2168_cmd {
	u8 args[SI2157_ARGLEN];
	unsigned wlen;
	unsigned rlen;
};

#endif
