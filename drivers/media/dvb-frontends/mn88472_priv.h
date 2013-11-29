/*
 * Panasonic MN88472 DVB-T/T2/C demodulator driver
 *
 * Copyright (C) 2013 Antti Palosaari <crope@iki.fi>
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

#ifndef MN88472_PRIV_H
#define MN88472_PRIV_H

#include "dvb_frontend.h"
#include "mn88472.h"
#include "dvb_math.h"
#include <linux/firmware.h>
#include <linux/i2c-mux.h>

#define MN88472_FIRMWARE "dvb-demod-mn88472-02.fw"

struct mn88472_state {
	struct i2c_adapter *i2c;
	const struct mn88472_c_config *cfg;
	struct dvb_frontend fe;
	fe_delivery_system_t delivery_system;
	bool warm; /* FW running */
};

#endif
