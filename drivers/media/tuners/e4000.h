/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Elonics E4000 silicon tuner driver
 *
 * Copyright (C) 2012 Antti Palosaari <crope@iki.fi>
 */

#ifndef E4000_H
#define E4000_H

#include <media/dvb_frontend.h>

/*
 * I2C address
 * 0x64, 0x65, 0x66, 0x67
 */
struct e4000_config {
	/*
	 * frontend
	 */
	struct dvb_frontend *fe;

	/*
	 * clock
	 */
	u32 clock;
};

#endif
