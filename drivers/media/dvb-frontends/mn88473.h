/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Panasonic MN88473 DVB-T/T2/C demodulator driver
 *
 * Copyright (C) 2014 Antti Palosaari <crope@iki.fi>
 */

#ifndef MN88473_H
#define MN88473_H

#include <linux/dvb/frontend.h>

struct mn88473_config {
	/*
	 * Max num of bytes given I2C adapter could write at once.
	 * Default: unlimited
	 */
	u16 i2c_wr_max;

	/*
	 * Xtal frequency Hz.
	 * Default: 25000000
	 */
	u32 xtal;


	/* Everything after that is returned by the driver. */

	/*
	 * DVB frontend.
	 */
	struct dvb_frontend **fe;
};

#endif
