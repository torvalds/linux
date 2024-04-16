/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ddbridge-hw.h: Digital Devices bridge hardware maps
 *
 * Copyright (C) 2010-2017 Digital Devices GmbH
 *                         Ralph Metzler <rjkm@metzlerbros.de>
 *                         Marcus Metzler <mocm@metzlerbros.de>
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

#endif /* _DDBRIDGE_HW_H_ */
