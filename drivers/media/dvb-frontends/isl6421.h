/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * isl6421.h - driver for lnb supply and control ic ISL6421
 *
 * Copyright (C) 2006 Andrew de Quincey
 * Copyright (C) 2006 Oliver Endriss
 *
 * the project's page is at https://linuxtv.org
 */

#ifndef _ISL6421_H
#define _ISL6421_H

#include <linux/dvb/frontend.h>

/* system register bits */
#define ISL6421_OLF1	0x01
#define ISL6421_EN1	0x02
#define ISL6421_VSEL1	0x04
#define ISL6421_LLC1	0x08
#define ISL6421_ENT1	0x10
#define ISL6421_ISEL1	0x20
#define ISL6421_DCL	0x40

#if IS_REACHABLE(CONFIG_DVB_ISL6421)
/* override_set and override_clear control which system register bits (above) to always set & clear */
extern struct dvb_frontend *isl6421_attach(struct dvb_frontend *fe, struct i2c_adapter *i2c, u8 i2c_addr,
			  u8 override_set, u8 override_clear, bool override_tone);
#else
static inline struct dvb_frontend *isl6421_attach(struct dvb_frontend *fe, struct i2c_adapter *i2c, u8 i2c_addr,
						  u8 override_set, u8 override_clear, bool override_tone)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif // CONFIG_DVB_ISL6421

#endif
