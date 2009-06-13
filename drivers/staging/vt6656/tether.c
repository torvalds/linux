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
 *      ETHbyGetHashIndexByCrc32 - Caculate multicast hash value by CRC32
 *      ETHbIsBufferCrc32Ok - Check CRC value of the buffer if Ok or not
 *
 * Revision History:
 *
 */

#if !defined(__DEVICE_H__)
#include "device.h"
#endif
#if !defined(__TMACRO_H__)
#include "tmacro.h"
#endif
#if !defined(__TBIT_H__)
#include "tbit.h"
#endif
#if !defined(__TCRC_H__)
#include "tcrc.h"
#endif
#if !defined(__TETHER_H__)
#include "tether.h"
#endif




/*---------------------  Static Definitions -------------------------*/

/*---------------------  Static Classes  ----------------------------*/

/*---------------------  Static Variables  --------------------------*/

/*---------------------  Static Functions  --------------------------*/

/*---------------------  Export Variables  --------------------------*/



/*
 * Description: Caculate multicast hash value by CRC32
 *
 * Parameters:
 *  In:
 *		pbyMultiAddr    - Multicast Address
 *  Out:
 *      none
 *
 * Return Value: Hash value
 *
 */
BYTE ETHbyGetHashIndexByCrc32 (PBYTE pbyMultiAddr)
{
    int     ii;
    BYTE    byTmpHash;
    BYTE    byHash = 0;

    // get the least 6-bits from CRC generator
    byTmpHash = (BYTE)(CRCdwCrc32(pbyMultiAddr, U_ETHER_ADDR_LEN,
            0xFFFFFFFFL) & 0x3F);
    // reverse most bit to least bit
    for (ii = 0; ii < (sizeof(byTmpHash) * 8); ii++) {
        byHash <<= 1;
        if (BITbIsBitOn(byTmpHash, 0x01))
            byHash |= 1;
        byTmpHash >>= 1;
    }

    // adjust 6-bits to the right most
    return (byHash >> 2);
}


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
 * Return Value: TRUE if ok; FALSE if error.
 *
 */
BOOL ETHbIsBufferCrc32Ok (PBYTE pbyBuffer, UINT cbFrameLength)
{
    DWORD dwCRC;

    dwCRC = CRCdwGetCrc32(pbyBuffer, cbFrameLength - 4);
    if (cpu_to_le32(*((PDWORD)(pbyBuffer + cbFrameLength - 4))) != dwCRC) {
        return FALSE;
    }
    return TRUE;
}

