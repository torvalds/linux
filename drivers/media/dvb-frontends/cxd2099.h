/*
 * cxd2099.h: Driver for the CXD2099AR Common Interface Controller
 *
 * Copyright (C) 2010-2011 Digital Devices GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
