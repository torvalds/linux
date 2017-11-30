/*
 * cxd2841er.h
 *
 * Sony CXD2441ER digital demodulator driver public definitions
 *
 * Copyright 2012 Sony Corporation
 * Copyright (C) 2014 NetUP Inc.
 * Copyright (C) 2014 Sergey Kozlov <serjk@netup.ru>
 * Copyright (C) 2014 Abylay Ospan <aospan@netup.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
  */

#ifndef CXD2841ER_H
#define CXD2841ER_H

#include <linux/dvb/frontend.h>

#define CXD2841ER_USE_GATECTRL	1	/* bit 0 */
#define CXD2841ER_AUTO_IFHZ	2	/* bit 1 */
#define CXD2841ER_TS_SERIAL	4	/* bit 2 */
#define CXD2841ER_ASCOT		8	/* bit 3 */
#define CXD2841ER_EARLY_TUNE	16	/* bit 4 */
#define CXD2841ER_NO_WAIT_LOCK	32	/* bit 5 */
#define CXD2841ER_NO_AGCNEG	64	/* bit 6 */
#define CXD2841ER_TSBITS	128	/* bit 7 */

enum cxd2841er_xtal {
	SONY_XTAL_20500, /* 20.5 MHz */
	SONY_XTAL_24000, /* 24 MHz */
	SONY_XTAL_41000 /* 41 MHz */
};

struct cxd2841er_config {
	u8	i2c_addr;
	enum cxd2841er_xtal	xtal;
	u32	flags;
};

#if IS_REACHABLE(CONFIG_DVB_CXD2841ER)
extern struct dvb_frontend *cxd2841er_attach_s(struct cxd2841er_config *cfg,
					       struct i2c_adapter *i2c);

extern struct dvb_frontend *cxd2841er_attach_t_c(struct cxd2841er_config *cfg,
					       struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *cxd2841er_attach_s(
					struct cxd2841er_config *cfg,
					struct i2c_adapter *i2c)
{
	pr_warn("%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}

static inline struct dvb_frontend *cxd2841er_attach_t_c(
		struct cxd2841er_config *cfg, struct i2c_adapter *i2c)
{
	pr_warn("%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}

#endif

#endif
