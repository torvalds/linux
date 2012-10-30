/*
	NxtWave Communications - NXT6000 demodulator driver

    Copyright (C) 2002-2003 Florian Schirmer <jolt@tuxbox.org>
    Copyright (C) 2003 Paul Andreassen <paul@andreassen.com.au>

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

#ifndef NXT6000_H
#define NXT6000_H

#include <linux/dvb/frontend.h>

struct nxt6000_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/* should clock inversion be used? */
	u8 clock_inversion:1;
};

#if defined(CONFIG_DVB_NXT6000) || (defined(CONFIG_DVB_NXT6000_MODULE) && defined(MODULE))
extern struct dvb_frontend* nxt6000_attach(const struct nxt6000_config* config,
					   struct i2c_adapter* i2c);
#else
static inline struct dvb_frontend* nxt6000_attach(const struct nxt6000_config* config,
					   struct i2c_adapter* i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif // CONFIG_DVB_NXT6000

#endif // NXT6000_H
