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
 * Definitions for mxc6202x accelorometer sensor chip.
 */
#ifndef __MXC6202X_H__
#define __MXC6202X_H__

#include <linux/ioctl.h>

#define MXC6202X_I2C_NAME		"mxc6202x"

/*
 * This address comes must match the part# on your target.
 * Address to the sensor part# support as following list:
 *   MXC62020	- 0x10
 *   MXC62021	- 0x11
 *   MXC62022	- 0x12
 *   MXC62023	- 0x13
 *   MXC62024	- 0x14
 *   MXC62025	- 0x15
 *   MXC62026	- 0x16
 *   MXC62027	- 0x17
 * Please refer to sensor datasheet for detail.
 */
#define MXC6202X_I2C_ADDR		0x10

/* MXC6202X register address */
#define MXC6202X_REG_CTRL		0x00
#define MXC6202X_REG_DATA		0x01

/* MXC6202X control bit */
#define MXC6202X_CTRL_PWRON		0x00	/* power on */
#define MXC6202X_CTRL_PWRDN		0x01	/* power donw */
#define MXC6202X_CTRL_ST		0x02	/* self test */
#define MXC6202X_CTRL_BGTST		0x04	/* bandgap test */
#define MXC6202X_CTRL_TOEN		0x08	/* temperature out en */

/* Use 'm' as magic number */
#define MXC6202X_IOM			'm'

/* IOCTLs for MXC6202X device */
#define MXC6202X_IOC_PWRON		_IO (MXC6202X_IOM, 0x00)
#define MXC6202X_IOC_PWRDN		_IO (MXC6202X_IOM, 0x01)
#define MXC6202X_IOC_ST			_IO (MXC6202X_IOM, 0x02)
#define MXC6202X_IOC_BGTST		_IO (MXC6202X_IOM, 0x03)
#define MXC6202X_IOC_TOEN		_IO (MXC6202X_IOM, 0x04)
#define MXC6202X_IOC_READXYZ		_IOR(MXC6202X_IOM, 0x05, int[3])
#define MXC6202X_IOC_READTEMP		_IOR(MXC6202X_IOM, 0x06, int)

#endif /* __MXC6202X_H__ */

