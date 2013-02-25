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
 *
 *    You should have received a copy of the GNU General Public License along
 *    with this program; if not, write to the Free Software Foundation, Inc.,
 *    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef M88TS2022_H
#define M88TS2022_H

#include "dvb_frontend.h"

struct m88ts2022_config {
	/*
	 * I2C address
	 * 0x60, ...
	 */
	u8 i2c_addr;

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
};

#if defined(CONFIG_MEDIA_TUNER_M88TS2022) || \
	(defined(CONFIG_MEDIA_TUNER_M88TS2022_MODULE) && defined(MODULE))
extern struct dvb_frontend *m88ts2022_attach(struct dvb_frontend *fe,
	struct i2c_adapter *i2c, const struct m88ts2022_config *cfg);
#else
static inline struct dvb_frontend *m88ts2022_attach(struct dvb_frontend *fe,
	struct i2c_adapter *i2c, const struct m88ts2022_config *cfg)
{
	pr_warn("%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif
