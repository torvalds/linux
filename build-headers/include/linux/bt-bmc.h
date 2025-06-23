/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Copyright (c) 2015-2016, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_BT_BMC_H
#define _LINUX_BT_BMC_H

#include <linux/ioctl.h>

#define __BT_BMC_IOCTL_MAGIC	0xb1
#define BT_BMC_IOCTL_SMS_ATN	_IO(__BT_BMC_IOCTL_MAGIC, 0x00)

#endif /* _LINUX_BT_BMC_H */
