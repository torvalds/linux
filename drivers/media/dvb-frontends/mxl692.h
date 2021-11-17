/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Driver for the MaxLinear MxL69x family of tuners/demods
 *
 * Copyright (C) 2020 Brad Love <brad@nextdimension.cc>
 *
 * based on code:
 * Copyright (c) 2016 MaxLinear, Inc. All rights reserved
 * which was released under GPL V2
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _MXL692_H_
#define _MXL692_H_

#include <media/dvb_frontend.h>

#define MXL692_FIRMWARE "dvb-demod-mxl692.fw"

struct mxl692_config {
	unsigned char  id;
	u8 i2c_addr;
	/*
	 * frontend
	 * returned by driver
	 */
	struct dvb_frontend **fe;
};

#endif /* _MXL692_H_ */
