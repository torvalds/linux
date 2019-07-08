/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * stv0367.h
 *
 * Driver for ST STV0367 DVB-T & DVB-C demodulator IC.
 *
 * Copyright (C) ST Microelectronics.
 * Copyright (C) 2010,2011 NetUP Inc.
 * Copyright (C) 2010,2011 Igor M. Liplianin <liplianin@netup.ru>
 */

#ifndef STV0367_H
#define STV0367_H

#include <linux/dvb/frontend.h>
#include <media/dvb_frontend.h>

#define STV0367_ICSPEED_53125	53125000
#define STV0367_ICSPEED_58000	58000000

struct stv0367_config {
	u8 demod_address;
	u32 xtal;
	u32 if_khz;/*4500*/
	int if_iq_mode;
	int ts_mode;
	int clk_pol;
};

#if IS_REACHABLE(CONFIG_DVB_STV0367)
extern struct
dvb_frontend *stv0367ter_attach(const struct stv0367_config *config,
					struct i2c_adapter *i2c);
extern struct
dvb_frontend *stv0367cab_attach(const struct stv0367_config *config,
					struct i2c_adapter *i2c);
extern struct
dvb_frontend *stv0367ddb_attach(const struct stv0367_config *config,
					struct i2c_adapter *i2c);
#else
static inline struct
dvb_frontend *stv0367ter_attach(const struct stv0367_config *config,
					struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
static inline struct
dvb_frontend *stv0367cab_attach(const struct stv0367_config *config,
					struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
static inline struct
dvb_frontend *stv0367ddb_attach(const struct stv0367_config *config,
					struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif
