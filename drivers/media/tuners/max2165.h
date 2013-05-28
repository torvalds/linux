/*
 *  Driver for Maxim MAX2165 silicon tuner
 *
 *  Copyright (c) 2009 David T. L. Wong <davidtlwong@gmail.com>
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
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __MAX2165_H__
#define __MAX2165_H__

#include <linux/kconfig.h>

struct dvb_frontend;
struct i2c_adapter;

struct max2165_config {
	u8 i2c_address;
	u8 osc_clk; /* in MHz, selectable values: 4,16,18,20,22,24,26,28 */
};

#if IS_ENABLED(CONFIG_MEDIA_TUNER_MAX2165)
extern struct dvb_frontend *max2165_attach(struct dvb_frontend *fe,
	struct i2c_adapter *i2c,
	struct max2165_config *cfg);
#else
static inline struct dvb_frontend *max2165_attach(struct dvb_frontend *fe,
	struct i2c_adapter *i2c,
	struct max2165_config *cfg)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif
