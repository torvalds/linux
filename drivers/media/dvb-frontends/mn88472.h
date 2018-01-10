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

#ifndef MN88472_H
#define MN88472_H

#include <linux/dvb/frontend.h>

/* Define old names for backward compatibility */
#define VARIABLE_TS_CLOCK   MN88472_TS_CLK_VARIABLE
#define FIXED_TS_CLOCK      MN88472_TS_CLK_FIXED
#define SERIAL_TS_MODE      MN88472_TS_MODE_SERIAL
#define PARALLEL_TS_MODE    MN88472_TS_MODE_PARALLEL

/**
 * struct mn88472_config - Platform data for the mn88472 driver
 * @xtal: Clock frequency.
 * @ts_mode: TS mode.
 * @ts_clock: TS clock config.
 * @i2c_wr_max: Max number of bytes driver writes to I2C at once.
 * @fe: pointer to a frontend pointer
 * @get_dvb_frontend: Get DVB frontend callback.
 */
struct mn88472_config {
	unsigned int xtal;

#define MN88472_TS_MODE_SERIAL      0
#define MN88472_TS_MODE_PARALLEL    1
	int ts_mode;

#define MN88472_TS_CLK_FIXED        0
#define MN88472_TS_CLK_VARIABLE     1
	int ts_clock;

	u16 i2c_wr_max;

	/* Everything after that is returned by the driver. */

	/*
	 * DVB frontend.
	 */
	struct dvb_frontend **fe;
	struct dvb_frontend* (*get_dvb_frontend)(struct i2c_client *);
};

#endif
