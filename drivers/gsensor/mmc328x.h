/*
 * Copyright (C) 2010 MEMSIC, Inc.
 *
 * Initial Code:
 *	Robbie Cao
 *	Dale Hou
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
 * Definitions for mmc328x magnetic sensor chip.
 */
#ifndef __MMC328X_H__
#define __MMC328X_H__

#include <linux/ioctl.h>

#define MMC328X_I2C_NAME		"mmc328x"

/*
 * This address comes must match the part# on your target.
 * Address to the sensor part# support as following list:
 *	MMC3280MS - 0110000b
 *	MMC3281MS - 0110001b
 *	MMC3282MS - 0110010b
 *	MMC3283MS - 0110011b
 *	MMC3284MS - 0110100b
 *	MMC3285MS - 0110101b
 *	MMC3286MS - 0110110b
 *	MMC3287MS - 0110111b
 * Please refer to sensor datasheet for detail.
 */
#define MMC328X_I2C_ADDR		0x30

/* MMC328X register address */
#define MMC328X_REG_CTRL		0x07
#define MMC328X_REG_DATA		0x00
#define MMC328X_REG_DS			0x06

/* MMC328X control bit */
#define MMC328X_CTRL_TM			0x01
#define MMC328X_CTRL_RM			0x20

/* Use 'm' as magic number */
#define MMC328X_IOM			'm'

/* IOCTLs for MMC328X device */
#define MMC328X_IOC_TM			_IO (MMC328X_IOM, 0x00)
#define MMC328X_IOC_RM			_IO (MMC328X_IOM, 0x01)
#define MMC328X_IOC_READ		_IOR(MMC328X_IOM, 0x02, int[3])
#define MMC328X_IOC_READXYZ		_IOR(MMC328X_IOM, 0x03, int[3])

#endif /* __MMC328X_H__ */

