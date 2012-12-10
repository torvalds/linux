/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 * File: control.c
 *
 * Purpose: Handle USB control endpoint
 *
 * Author: Jerry Chen
 *
 * Date: Apr. 5, 2004
 *
 * Functions:
 *      CONTROLnsRequestOut - Write variable length bytes to MEM/BB/MAC/EEPROM
 *      CONTROLnsRequestIn - Read variable length bytes from MEM/BB/MAC/EEPROM
 *      ControlvWriteByte - Write one byte to MEM/BB/MAC/EEPROM
 *      ControlvReadByte - Read one byte from MEM/BB/MAC/EEPROM
 *      ControlvMaskByte - Read one byte from MEM/BB/MAC/EEPROM and clear/set
 *				some bits in the same address
 *
 * Revision History:
 *      04-05-2004 Jerry Chen:  Initial release
 *      11-24-2004 Warren Hsu: Add ControlvWriteByte, ControlvReadByte,
 *					ControlvMaskByte
 *
 */

#include "control.h"
#include "rndis.h"

/*---------------------  Static Definitions -------------------------*/
/* static int          msglevel                =MSG_LEVEL_INFO;  */
/* static int          msglevel                =MSG_LEVEL_DEBUG; */
/*---------------------  Static Classes  ----------------------------*/

/*---------------------  Static Variables  --------------------------*/

/*---------------------  Static Functions  --------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

void ControlvWriteByte(struct vnt_private *pDevice, u8 reg, u8 reg_off,
			u8 data)
{

	CONTROLnsRequestOut(pDevice, MESSAGE_TYPE_WRITE, reg_off, reg,
		sizeof(u8), &data);

	return;
}

void ControlvReadByte(struct vnt_private *pDevice, u8 reg, u8 reg_off,
			u8 *data)
{
	CONTROLnsRequestIn(pDevice, MESSAGE_TYPE_READ,
			reg_off, reg, sizeof(u8), data);
	return;
}

void ControlvMaskByte(struct vnt_private *pDevice, u8 reg_type, u8 reg_off,
			u8 reg_mask, u8 data)
{
	u8 reg_data[2];

	reg_data[0] = data;
	reg_data[1] = reg_mask;

	CONTROLnsRequestOut(pDevice, MESSAGE_TYPE_WRITE_MASK, reg_off,
			reg_type, ARRAY_SIZE(reg_data), reg_data);

	return;
}
