/*
 * Copyright (C) 2010 MEMSIC, Inc.
 *
 * Initial Code:
 *	Robbie Cao
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

/*
 * Definitions for mxc622x accelorometer sensor chip.
 */
#ifndef __MXC622X_H__
#define __MXC622X_H__

#include <linux/ioctl.h>

#define MXC622X_I2C_NAME		"mxc622x"

/*
 * This address comes must match the part# on your target.
 * Address to the sensor part# support as following list:
 *   MXC6220	- 0x10
 *   MXC6221	- 0x11
 *   MXC6222	- 0x12
 *   MXC6223	- 0x13
 *   MXC6224	- 0x14
 *   MXC6225	- 0x15
 *   MXC6226	- 0x16
 *   MXC6227	- 0x17
 * Please refer to sensor datasheet for detail.
 */
#define MXC622X_I2C_ADDR		0x15

/* MXC622X register address */
#define MXC622X_REG_CTRL		0x04
#define MXC622X_REG_DATA		0x00

/* MXC622X control bit */
#define MXC622X_CTRL_PWRON		0x00	/* power on */
#define MXC622X_CTRL_PWRDN		0x80	/* power donw */

/* Use 'm' as magic number */
#define MXC622X_IOM			'm'

/* IOCTLs for MXC622X device */
#define MXC622X_IOC_PWRON		_IO (MXC622X_IOM, 0x00)
#define MXC622X_IOC_PWRDN		_IO (MXC622X_IOM, 0x01)
#define MXC622X_IOC_READXYZ		_IOR(MXC622X_IOM, 0x05, int[3])
#define MXC622X_IOC_READSTATUS		_IOR(MXC622X_IOM, 0x07, int[3])
#define MXC622X_IOC_SETDETECTION	_IOW(MXC622X_IOM, 0x08, unsigned char)

#endif /* __MXC622X_H__ */

