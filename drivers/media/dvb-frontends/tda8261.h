/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
	TDA8261 8PSK/QPSK tuner driver
	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

*/

#ifndef __TDA8261_H
#define __TDA8261_H

enum tda8261_step {
	TDA8261_STEP_2000 = 0,	/* 2000 kHz */
	TDA8261_STEP_1000,	/* 1000 kHz */
	TDA8261_STEP_500,	/*  500 kHz */
	TDA8261_STEP_250,	/*  250 kHz */
	TDA8261_STEP_125	/*  125 kHz */
};

struct tda8261_config {
//	u8			buf[16];
	u8			addr;
	enum tda8261_step	step_size;
};

#if IS_REACHABLE(CONFIG_DVB_TDA8261)

extern struct dvb_frontend *tda8261_attach(struct dvb_frontend *fe,
					   const struct tda8261_config *config,
					   struct i2c_adapter *i2c);

#else

static inline struct dvb_frontend *tda8261_attach(struct dvb_frontend *fe,
						  const struct tda8261_config *config,
						  struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: Driver disabled by Kconfig\n", __func__);
	return NULL;
}

#endif //CONFIG_DVB_TDA8261

#endif// __TDA8261_H
