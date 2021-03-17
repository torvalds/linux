/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * lnbh25.c
 *
 * Driver for LNB supply and control IC LNBH25
 *
 * Copyright (C) 2014 NetUP Inc.
 * Copyright (C) 2014 Sergey Kozlov <serjk@netup.ru>
 * Copyright (C) 2014 Abylay Ospan <aospan@netup.ru>
 */

#ifndef LNBH25_H
#define LNBH25_H

#include <linux/i2c.h>
#include <linux/dvb/frontend.h>

/* 22 kHz tone enabled. Tone output controlled by DSQIN pin */
#define	LNBH25_TEN	0x01
/* Low power mode activated (used only with 22 kHz tone output disabled) */
#define LNBH25_LPM	0x02
/* DSQIN input pin is set to receive external 22 kHz TTL signal source */
#define LNBH25_EXTM	0x04

struct lnbh25_config {
	u8	i2c_address;
	u8	data2_config;
};

#if IS_REACHABLE(CONFIG_DVB_LNBH25)
struct dvb_frontend *lnbh25_attach(
	struct dvb_frontend *fe,
	struct lnbh25_config *cfg,
	struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *lnbh25_attach(
	struct dvb_frontend *fe,
	struct lnbh25_config *cfg,
	struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif
