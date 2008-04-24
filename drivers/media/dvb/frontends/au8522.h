/*
    Auvitek AU8522 QAM/8VSB demodulator driver

    Copyright (C) 2008 Steven Toth <stoth@hauppauge.com>

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

#ifndef __AU8522_H__
#define __AU8522_H__

#include <linux/dvb/frontend.h>

struct au8522_config {
	/* the demodulator's i2c address */
	u8 demod_address;

	/* Return lock status based on tuner lock, or demod lock */
#define AU8522_TUNERLOCKING 0
#define AU8522_DEMODLOCKING 1
	u8 status_mode;
};

#if defined(CONFIG_DVB_AU8522) || 				\
	    (defined(CONFIG_DVB_AU8522_MODULE) && defined(MODULE))
extern struct dvb_frontend *au8522_attach(const struct au8522_config *config,
					  struct i2c_adapter *i2c);
#else
static inline
struct dvb_frontend *au8522_attach(const struct au8522_config *config,
				   struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_AU8522 */

#endif /* __AU8522_H__ */

/*
 * Local variables:
 * c-basic-offset: 8
 */
