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
 * Definitions for mmc31xx magnetic sensor chip.
 */
#ifndef __MMC31XX_H__
#define __MMC31XX_H__

#include <linux/ioctl.h>

#define MMC31XX_I2C_NAME		"mmc31xx"

/*
 * This address comes must match the part# on your target.
 * Address to the sensor part# support as following list:
 *   MMC3140	- 0x30
 *   MMC3141	- 0x32
 *   MMC3142	- 0x34
 *   MMC3143	- 0x36
 *   MMC3120	- 0x30
 *   MMC3121	- 0x32
 *   MMC3122	- 0x34
 *   MMC3123	- 0x36
 * Please refer to sensor datasheet for detail.
 */
#define MMC31XX_I2C_ADDR		0x30

/* MMC31XX register address */
#define MMC31XX_REG_CTRL		0x00
#define MMC31XX_REG_DATA		0x01

/* MMC31XX control bit */
#define MMC31XX_CTRL_TM			0x01
#define MMC31XX_CTRL_SET		0x02
#define MMC31XX_CTRL_RST		0x04

/* Use 'm' as magic number */
#define MMC31XX_IOM			'm'

/* IOCTLs for MMC31XX device */
#define MMC31XX_IOC_TM			_IO (MMC31XX_IOM, 0x00)
#define MMC31XX_IOC_SET			_IO (MMC31XX_IOM, 0x01)
#define MMC31XX_IOC_RESET		_IO (MMC31XX_IOM, 0x02)
#define MMC31XX_IOC_READ		_IOR(MMC31XX_IOM, 0x03, int[3])
#define MMC31XX_IOC_READXYZ		_IOR(MMC31XX_IOM, 0x04, int[3])

#endif /* __MMC31XX_H__ */

