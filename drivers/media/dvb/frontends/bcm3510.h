/*
 * Support for the Broadcom BCM3510 ATSC demodulator (1st generation Air2PC)
 *
 *  Copyright (C) 2001-5, B2C2 inc.
 *
 *  GPL/Linux driver written by Patrick Boettcher <patrick.boettcher@desy.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef BCM3510_H
#define BCM3510_H

#include <linux/dvb/frontend.h>
#include <linux/firmware.h>

struct bcm3510_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/* request firmware for device */
	int (*request_firmware)(struct dvb_frontend* fe, const struct firmware **fw, char* name);
};

#if defined(CONFIG_DVB_BCM3510) || (defined(CONFIG_DVB_BCM3510_MODULE) && defined(MODULE))
extern struct dvb_frontend* bcm3510_attach(const struct bcm3510_config* config,
					   struct i2c_adapter* i2c);
#else
static inline struct dvb_frontend* bcm3510_attach(const struct bcm3510_config* config,
						  struct i2c_adapter* i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __FUNCTION__);
	return NULL;
}
#endif // CONFIG_DVB_BCM3510

#endif
