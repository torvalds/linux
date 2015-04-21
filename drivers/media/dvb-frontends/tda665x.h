/*
	TDA665x tuner driver
	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

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

#ifndef __TDA665x_H
#define __TDA665x_H

struct tda665x_config {
	char name[128];

	u8	addr;
	u32	frequency_min;
	u32	frequency_max;
	u32	frequency_offst;
	u32	ref_multiplier;
	u32	ref_divider;
};

#if IS_REACHABLE(CONFIG_DVB_TDA665x)

extern struct dvb_frontend *tda665x_attach(struct dvb_frontend *fe,
					   const struct tda665x_config *config,
					   struct i2c_adapter *i2c);

#else

static inline struct dvb_frontend *tda665x_attach(struct dvb_frontend *fe,
						  const struct tda665x_config *config,
						  struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: Driver disabled by Kconfig\n", __func__);
	return NULL;
}

#endif /* CONFIG_DVB_TDA665x */

#endif /* __TDA665x_H */
