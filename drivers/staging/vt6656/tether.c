/*
 * Copyright (c) 2003 VIA Networking, Inc. All rights reserved.
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
 * File: tether.c
 *
 * Purpose:
 *
 * Author: Tevin Chen
 *
 * Date: May 21, 1996
 *
 * Functions:
 *      ETHbIsBufferCrc32Ok - Check CRC value of the buffer if Ok or not
 *
 * Revision History:
 *
 */

#include "device.h"
#include "tmacro.h"
#include "tcrc.h"
#include "tether.h"

/*
 * Description: Check CRC value of the buffer if Ok or not
 *
 * Parameters:
 *  In:
 *		pbyBuffer	    - pointer of buffer (normally is rx buffer)
 *		cbFrameLength	- length of buffer, including CRC portion
 *  Out:
 *      none
 *
 * Return Value: true if ok; false if error.
 *
 */
bool ETHbIsBufferCrc32Ok(u8 * pbyBuffer, unsigned int cbFrameLength)
{
	u32 dwCRC;

	dwCRC = CRCdwGetCrc32(pbyBuffer, cbFrameLength - 4);
	if (cpu_to_le32(*((u32 *)(pbyBuffer + cbFrameLength - 4))) != dwCRC)
		return false;
	return true;
}

