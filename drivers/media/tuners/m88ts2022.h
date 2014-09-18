/*
 * Montage M88TS2022 silicon tuner driver
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

#ifndef M88TS2022_H
#define M88TS2022_H

#include "dvb_frontend.h"

struct m88ts2022_config {
	/*
	 * clock
	 * 16000000 - 32000000
	 */
	u32 clock;

	/*
	 * RF loop-through
	 */
	u8 loop_through:1;

	/*
	 * clock output
	 */
#define M88TS2022_CLOCK_OUT_DISABLED        0
#define M88TS2022_CLOCK_OUT_ENABLED         1
#define M88TS2022_CLOCK_OUT_ENABLED_XTALOUT 2
	u8 clock_out:2;

	/*
	 * clock output divider
	 * 1 - 31
	 */
	u8 clock_out_div:5;

	/*
	 * pointer to DVB frontend
	 */
	struct dvb_frontend *fe;
};

#endif
