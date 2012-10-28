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

#ifndef __TEA5767_H__
#define __TEA5767_H__

#include <linux/i2c.h>
#include "dvb_frontend.h"

enum tea5767_xtal {
	TEA5767_LOW_LO_32768    = 0,
	TEA5767_HIGH_LO_32768   = 1,
	TEA5767_LOW_LO_13MHz    = 2,
	TEA5767_HIGH_LO_13MHz   = 3,
};

struct tea5767_ctrl {
	unsigned int		port1:1;
	unsigned int		port2:1;
	unsigned int		high_cut:1;
	unsigned int		st_noise:1;
	unsigned int		soft_mute:1;
	unsigned int		japan_band:1;
	unsigned int		deemph_75:1;
	unsigned int		pllref:1;
	enum tea5767_xtal	xtal_freq;
};

#if defined(CONFIG_MEDIA_TUNER_TEA5767) || (defined(CONFIG_MEDIA_TUNER_TEA5767_MODULE) && defined(MODULE))
extern int tea5767_autodetection(struct i2c_adapter* i2c_adap, u8 i2c_addr);

extern struct dvb_frontend *tea5767_attach(struct dvb_frontend *fe,
					   struct i2c_adapter* i2c_adap,
					   u8 i2c_addr);
#else
static inline int tea5767_autodetection(struct i2c_adapter* i2c_adap,
					u8 i2c_addr)
{
	printk(KERN_INFO "%s: not probed - driver disabled by Kconfig\n",
	       __func__);
	return -EINVAL;
}

static inline struct dvb_frontend *tea5767_attach(struct dvb_frontend *fe,
						   struct i2c_adapter* i2c_adap,
						   u8 i2c_addr)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif /* __TEA5767_H__ */
