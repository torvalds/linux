/*
 *  Driver for Xceive XC4000 "QAM/8VSB single chip tuner"
 *
 *  Copyright (c) 2007 Steven Toth <stoth@linuxtv.org>
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

#ifndef __XC4000_H__
#define __XC4000_H__

#include <linux/firmware.h>

struct dvb_frontend;
struct i2c_adapter;

struct xc4000_config {
	u8	i2c_address;
	/* if non-zero, power management is enabled by default */
	u8	default_pm;
	/* value to be written to XREG_AMPLITUDE in DVB-T mode (0: no write) */
	u8	dvb_amplitude;
	/* if non-zero, register 0x0E is set to filter analog TV video output */
	u8	set_smoothedcvbs;
	/* IF for DVB-T */
	u32	if_khz;
};

/* xc4000 callback command */
#define XC4000_TUNER_RESET		0

/* For each bridge framework, when it attaches either analog or digital,
 * it has to store a reference back to its _core equivalent structure,
 * so that it can service the hardware by steering gpio's etc.
 * Each bridge implementation is different so cast devptr accordingly.
 * The xc4000 driver cares not for this value, other than ensuring
 * it's passed back to a bridge during tuner_callback().
 */

#if IS_ENABLED(CONFIG_MEDIA_TUNER_XC4000)
extern struct dvb_frontend *xc4000_attach(struct dvb_frontend *fe,
					  struct i2c_adapter *i2c,
					  struct xc4000_config *cfg);
#else
static inline struct dvb_frontend *xc4000_attach(struct dvb_frontend *fe,
						 struct i2c_adapter *i2c,
						 struct xc4000_config *cfg)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif
