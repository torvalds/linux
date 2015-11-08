/*
	Intersil ISL6423 SEC and LNB Power supply controller

	Copyright (C) Manu Abraham <abraham.manu@gmail.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef __ISL_6423_H
#define __ISL_6423_H

#include <linux/dvb/frontend.h>

enum isl6423_current {
	SEC_CURRENT_275m = 0,
	SEC_CURRENT_515m,
	SEC_CURRENT_635m,
	SEC_CURRENT_800m,
};

enum isl6423_curlim {
	SEC_CURRENT_LIM_ON = 1,
	SEC_CURRENT_LIM_OFF
};

struct isl6423_config {
	enum isl6423_current current_max;
	enum isl6423_curlim curlim;
	u8 addr;
	u8 mod_extern;
};

#if IS_REACHABLE(CONFIG_DVB_ISL6423)


extern struct dvb_frontend *isl6423_attach(struct dvb_frontend *fe,
					   struct i2c_adapter *i2c,
					   const struct isl6423_config *config);

#else
static inline struct dvb_frontend *isl6423_attach(struct dvb_frontend *fe,
						  struct i2c_adapter *i2c,
						  const struct isl6423_config *config)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}

#endif /* CONFIG_DVB_ISL6423 */

#endif /* __ISL_6423_H */
