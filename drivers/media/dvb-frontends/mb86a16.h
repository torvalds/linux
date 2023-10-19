/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
	Fujitsu MB86A16 DVB-S/DSS DC Receiver driver

	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

*/

#ifndef __MB86A16_H
#define __MB86A16_H

#include <linux/dvb/frontend.h>
#include <media/dvb_frontend.h>


struct mb86a16_config {
	u8 demod_address;

	int (*set_voltage)(struct dvb_frontend *fe,
			   enum fe_sec_voltage voltage);
};



#if IS_REACHABLE(CONFIG_DVB_MB86A16)

extern struct dvb_frontend *mb86a16_attach(const struct mb86a16_config *config,
					   struct i2c_adapter *i2c_adap);

#else

static inline struct dvb_frontend *mb86a16_attach(const struct mb86a16_config *config,
					   struct i2c_adapter *i2c_adap)
{
	printk(KERN_WARNING "%s: Driver disabled by Kconfig\n", __func__);
	return NULL;
}

#endif /* CONFIG_DVB_MB86A16 */

#endif /* __MB86A16_H */
