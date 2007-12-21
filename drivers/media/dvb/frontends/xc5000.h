/*
 *  Driver for Xceive XC5000 "QAM/8VSB single chip tuner"
 *
 *  Copyright (c) 2007 Steven Toth <stoth@hauppauge.com>
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

#ifndef __XC5000_H__
#define __XC5000_H__

#include <linux/firmware.h>

struct dvb_frontend;
struct i2c_adapter;

struct xc5000_config {
	u8  i2c_address;
	u32 if_khz;
	int (*request_firmware)(struct dvb_frontend *fe,
		const struct firmware **fw, char *name);
	int (*tuner_reset)(struct dvb_frontend* fe);
};

#if defined(CONFIG_DVB_TUNER_XC5000) || defined(CONFIG_DVB_TUNER_XC5000_MODULE)
extern struct dvb_frontend* xc5000_attach(struct dvb_frontend *fe,
					  struct i2c_adapter *i2c,
					  struct xc5000_config *cfg);
#else
static inline struct dvb_frontend* xc5000_attach(struct dvb_frontend *fe,
						 struct i2c_adapter *i2c,
						 struct xc5000_config *cfg)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __FUNCTION__);
	return NULL;
}
#endif // CONFIG_DVB_TUNER_XC5000

#endif // __XC5000_H__
