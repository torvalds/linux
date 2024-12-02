/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    Conexant cx24117/cx24132 - Dual DVBS/S2 Satellite demod/tuner driver

    Copyright (C) 2013 Luis Alves <ljalvs@gmail.com>
	(based on cx24116.h by Steven Toth)

*/

#ifndef CX24117_H
#define CX24117_H

#include <linux/dvb/frontend.h>

struct cx24117_config {
	/* the demodulator's i2c address */
	u8 demod_address;
};

#if IS_REACHABLE(CONFIG_DVB_CX24117)
extern struct dvb_frontend *cx24117_attach(
	const struct cx24117_config *config,
	struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *cx24117_attach(
	const struct cx24117_config *config,
	struct i2c_adapter *i2c)
{
	dev_warn(&i2c->dev, "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif /* CX24117_H */
