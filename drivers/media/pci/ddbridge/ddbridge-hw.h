/*
 * ddbridge-hw.h: Digital Devices bridge hardware maps
 *
 * Copyright (C) 2010-2017 Digital Devices GmbH
 *                         Ralph Metzler <rjkm@metzlerbros.de>
 *                         Marcus Metzler <mocm@metzlerbros.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _DDBRIDGE_HW_H_
#define _DDBRIDGE_HW_H_

#include "ddbridge.h"

/******************************************************************************/

#define DDVID 0xdd01 /* Digital Devices Vendor ID */

/******************************************************************************/

struct ddb_device_id {
	u16 vendor;
	u16 device;
	u16 subvendor;
	u16 subdevice;
	const struct ddb_info *info;
};

/******************************************************************************/

const struct ddb_info *get_ddb_info(u16 vendor, u16 device,
				    u16 subvendor, u16 subdevice);

#endif /* _DDBRIDGE_HW_H */
