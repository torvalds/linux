/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    Legend Silicon LGS-8GL5 DMB-TH OFDM demodulator driver

    Copyright (C) 2008 Sirius International (Hong Kong) Limited
	Timothy Lee <timothy.lee@siriushk.com>


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
