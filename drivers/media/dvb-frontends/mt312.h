/*
    Driver for Zarlink MT312 Satellite Channel Decoder

    Copyright (C) 2003 Andreas Oberritter <obi@linuxtv.org>

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

    References:
    http://products.zarlink.com/product_profiles/MT312.htm
    http://products.zarlink.com/product_profiles/SL1935.htm
*/

#ifndef MT312_H
#define MT312_H

#include <linux/dvb/frontend.h>

struct mt312_config {
	/* the demodulator's i2c address */
	u8 demod_address;

	/* inverted voltage setting */
	unsigned int voltage_inverted:1;
};

#if IS_ENABLED(CONFIG_DVB_MT312)
struct dvb_frontend *mt312_attach(const struct mt312_config *config,
					struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *mt312_attach(
	const struct mt312_config *config, struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_MT312 */

#endif /* MT312_H */
