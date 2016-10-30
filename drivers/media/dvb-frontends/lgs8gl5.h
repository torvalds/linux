/*
    Legend Silicon LGS-8GL5 DMB-TH OFDM demodulator driver

    Copyright (C) 2008 Sirius International (Hong Kong) Limited
	Timothy Lee <timothy.lee@siriushk.com>

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

#ifndef LGS8GL5_H
#define LGS8GL5_H

#include <linux/dvb/frontend.h>

struct lgs8gl5_config {
	/* the demodulator's i2c address */
	u8 demod_address;
};

#if IS_REACHABLE(CONFIG_DVB_LGS8GL5)
extern struct dvb_frontend *lgs8gl5_attach(
	const struct lgs8gl5_config *config, struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *lgs8gl5_attach(
	const struct lgs8gl5_config *config, struct i2c_adapter *i2c) {
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_LGS8GL5 */

#endif /* LGS8GL5_H */
