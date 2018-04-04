/*
	Fujitsu MB86A16 DVB-S/DSS DC Receiver driver

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

#ifndef __MB86A16_H
#define __MB86A16_H

#include <linux/dvb/frontend.h>
#include <media/dvb_frontend.h>


struct mb86a16_config {
	u8 demod_address;

	int (*set_voltage)(struct dvb_frontend *fe,
			   enum fe_sec_voltage voltage);
};



#if IS_REACHABLE(CONFIG_DVB_MB86A16)

extern struct dvb_frontend *mb86a16_attach(const struct mb86a16_config *config,
					   struct i2c_adapter *i2c_adap);

#else

static inline struct dvb_frontend *mb86a16_attach(const struct mb86a16_config *config,
					   struct i2c_adapter *i2c_adap)
{
	printk(KERN_WARNING "%s: Driver disabled by Kconfig\n", __func__);
	return NULL;
}

#endif /* CONFIG_DVB_MB86A16 */

#endif /* __MB86A16_H */
