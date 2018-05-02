/*
 * Elonics R820T silicon tuner driver
 *
 * Copyright (C) 2012 Antti Palosaari <crope@iki.fi>
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

#ifndef R820T_H
#define R820T_H

#include <media/dvb_frontend.h>

enum r820t_chip {
	CHIP_R820T,
	CHIP_R620D,
	CHIP_R828D,
	CHIP_R828,
	CHIP_R828S,
	CHIP_R820C,
};

struct r820t_config {
	u8 i2c_addr;		/* 0x34 */
	u32 xtal;
	enum r820t_chip rafael_chip;
	unsigned max_i2c_msg_len;
	bool use_diplexer;
	bool use_predetect;
};

#if IS_REACHABLE(CONFIG_MEDIA_TUNER_R820T)
struct dvb_frontend *r820t_attach(struct dvb_frontend *fe,
				  struct i2c_adapter *i2c,
				  const struct r820t_config *cfg);
#else
static inline struct dvb_frontend *r820t_attach(struct dvb_frontend *fe,
						struct i2c_adapter *i2c,
						const struct r820t_config *cfg)
{
	pr_warn("%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif
