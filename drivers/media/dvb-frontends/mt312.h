/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    Driver for Zarlink MT312 Satellite Channel Decoder

    Copyright (C) 2003 Andreas Oberritter <obi@linuxtv.org>


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

#if IS_REACHABLE(CONFIG_DVB_MT312)
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
