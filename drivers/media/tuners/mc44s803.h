/*
 *  Driver for Freescale MC44S803 Low Power CMOS Broadband Tuner
 *
 *  Copyright (c) 2009 Jochen Friedrich <jochen@scram.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.=
 */

#ifndef MC44S803_H
#define MC44S803_H

#include <linux/kconfig.h>

struct dvb_frontend;
struct i2c_adapter;

struct mc44s803_config {
	u8 i2c_address;
	u8 dig_out;
};

#if IS_ENABLED(CONFIG_MEDIA_TUNER_MC44S803)
extern struct dvb_frontend *mc44s803_attach(struct dvb_frontend *fe,
	 struct i2c_adapter *i2c, struct mc44s803_config *cfg);
#else
static inline struct dvb_frontend *mc44s803_attach(struct dvb_frontend *fe,
	 struct i2c_adapter *i2c, struct mc44s803_config *cfg)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_MEDIA_TUNER_MC44S803 */

#endif
