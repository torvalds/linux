/*
 *  Driver for DVB-T lgdt3304 demodulator
 *
 *  Copyright (C) 2008 Markus Rechberger <mrechberger@gmail.com>
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

#ifndef LGDT3304_H
#define LGDT3304_H

#include <linux/dvb/frontend.h>

struct lgdt3304_config
{
	/* demodulator's I2C address */
	u8 i2c_address;
};

#if defined(CONFIG_DVB_LGDT3304) || (defined(CONFIG_DVB_LGDT3304_MODULE) && defined(MODULE))
extern struct dvb_frontend* lgdt3304_attach(const struct lgdt3304_config *config,
					   struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend* lgdt3304_attach(const struct lgdt3304_config *config,
					   struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_LGDT */

#endif /* LGDT3304_H */
