/*
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

#ifndef __TUNER_SIMPLE_H__
#define __TUNER_SIMPLE_H__

#include <linux/i2c.h>
#include "dvb_frontend.h"

#if defined(CONFIG_TUNER_SIMPLE) || (defined(CONFIG_TUNER_SIMPLE_MODULE) && defined(MODULE))
extern struct dvb_frontend *simple_tuner_attach(struct dvb_frontend *fe,
						struct i2c_adapter *i2c_adap,
						u8 i2c_addr,
						unsigned int type);
#else
static inline struct dvb_frontend *simple_tuner_attach(struct dvb_frontend *fe,
						       struct i2c_adapter *i2c_adap,
						       u8 i2c_addr,
						       unsigned int type)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif /* __TUNER_SIMPLE_H__ */
