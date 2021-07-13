/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    Driver for Spase SP8870 demodulator

    Copyright (C) 1999 Juergen Peitz


*/

#ifndef SP8870_H
#define SP8870_H

#include <linux/dvb/frontend.h>
#include <linux/firmware.h>

struct sp8870_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/* request firmware for device */
	int (*request_firmware)(struct dvb_frontend* fe, const struct firmware **fw, char* name);
};

#if IS_REACHABLE(CONFIG_DVB_SP8870)
extern struct dvb_frontend* sp8870_attach(const struct sp8870_config* config,
					  struct i2c_adapter* i2c);
#else
static inline struct dvb_frontend* sp8870_attach(const struct sp8870_config* config,
					  struct i2c_adapter* i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif // CONFIG_DVB_SP8870

#endif // SP8870_H
