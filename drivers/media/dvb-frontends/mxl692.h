/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Driver for the MaxLinear MxL69x family of tuners/demods
 *
 * Copyright (C) 2020 Brad Love <brad@nextdimension.cc>
 *
 * based on code:
 * Copyright (c) 2016 MaxLinear, Inc. All rights reserved
 * which was released under GPL V2
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
