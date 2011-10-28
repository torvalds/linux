/*
 * NXP TDA18218HN silicon tuner driver
 *
 * Copyright (C) 2010 Antti Palosaari <crope@iki.fi>
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
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef TDA18218_H
#define TDA18218_H

#include "dvb_frontend.h"

struct tda18218_config {
	u8 i2c_address;
	u8 i2c_wr_max;
	u8 loop_through:1;
};

#if defined(CONFIG_MEDIA_TUNER_TDA18218) || \
	(defined(CONFIG_MEDIA_TUNER_TDA18218_MODULE) && defined(MODULE))
extern struct dvb_frontend *tda18218_attach(struct dvb_frontend *fe,
	struct i2c_adapter *i2c, struct tda18218_config *cfg);
#else
static inline struct dvb_frontend *tda18218_attach(struct dvb_frontend *fe,
	struct i2c_adapter *i2c, struct tda18218_config *cfg)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif
