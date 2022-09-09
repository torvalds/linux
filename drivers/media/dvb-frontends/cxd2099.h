/* SPDX-License-Identifier: GPL-2.0 */
/*
 * cxd2099.h: Driver for the Sony CXD2099AR Common Interface Controller
 *
 * Copyright (C) 2010-2011 Digital Devices GmbH
 */

#ifndef _CXD2099_H_
#define _CXD2099_H_

#include <media/dvb_ca_en50221.h>

struct cxd2099_cfg {
	u32 bitrate;
	u8  polarity;
	u8  clock_mode;

	u32 max_i2c;

	/* ptr to DVB CA struct */
	struct dvb_ca_en50221 **en;
};

#endif
