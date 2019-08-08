/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    cx24110 - Single Chip Satellite Channel Receiver driver module

    Copyright (C) 2002 Peter Hettkamp <peter.hettkamp@htp-tel.de> based on
    work
    Copyright (C) 1999 Convergence Integrated Media GmbH <ralph@convergence.de>


*/

#ifndef CX24110_H
#define CX24110_H

#include <linux/dvb/frontend.h>

struct cx24110_config
{
	/* the demodulator's i2c address */
	u8 demod_address;
};

static inline int cx24110_pll_write(struct dvb_frontend *fe, u32 val)
{
	u8 buf[] = {
		(u8)((val >> 24) & 0xff),
		(u8)((val >> 16) & 0xff),
		(u8)((val >> 8) & 0xff)
	};

	if (fe->ops.write)
		return fe->ops.write(fe, buf, 3);
	return 0;
}

#if IS_REACHABLE(CONFIG_DVB_CX24110)
extern struct dvb_frontend* cx24110_attach(const struct cx24110_config* config,
					   struct i2c_adapter* i2c);
#else
static inline struct dvb_frontend* cx24110_attach(const struct cx24110_config* config,
						  struct i2c_adapter* i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif // CONFIG_DVB_CX24110

#endif // CX24110_H
