/*
    TDA10021/TDA10023  - Single Chip Cable Channel Receiver driver module
			 used on the the Siemens DVB-C cards

    Copyright (C) 1999 Convergence Integrated Media GmbH <ralph@convergence.de>
    Copyright (C) 2004 Markus Schulz <msc@antzsystem.de>
		   Support for TDA10021

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

#ifndef TDA1002x_H
#define TDA1002x_H

#include <linux/dvb/frontend.h>

struct tda1002x_config
{
	/* the demodulator's i2c address */
	u8 demod_address;
	u8 invert;
};

#if defined(CONFIG_DVB_TDA10021) || (defined(CONFIG_DVB_TDA10021_MODULE) && defined(MODULE))
extern struct dvb_frontend* tda10021_attach(const struct tda1002x_config* config,
					    struct i2c_adapter* i2c, u8 pwm);
#else
static inline struct dvb_frontend* tda10021_attach(const struct tda1002x_config* config,
					    struct i2c_adapter* i2c, u8 pwm)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __FUNCTION__);
	return NULL;
}
#endif // CONFIG_DVB_TDA10021

#if defined(CONFIG_DVB_TDA10023) || (defined(CONFIG_DVB_TDA10023_MODULE) && defined(MODULE))
extern struct dvb_frontend* tda10023_attach(const struct tda1002x_config* config,
					    struct i2c_adapter* i2c, u8 pwm);
#else
static inline struct dvb_frontend* tda10023_attach(const struct tda1002x_config* config,
					    struct i2c_adapter* i2c, u8 pwm)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __FUNCTION__);
	return NULL;
}
#endif // CONFIG_DVB_TDA10023

#endif // TDA1002x_H
