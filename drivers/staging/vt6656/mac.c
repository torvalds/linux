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
 * File: mac.c
 *
 * Purpose:  MAC routines
 *
 * Author: Tevin Chen
 *
 * Date: May 21, 1996
 *
 * Functions:
 *
 * Revision History:
 */

#include "tmacro.h"
#include "tether.h"
#include "desc.h"
#include "mac.h"
#include "80211hdr.h"
#include "rndis.h"
#include "control.h"

//static int          msglevel                =MSG_LEVEL_DEBUG;
static int          msglevel                =MSG_LEVEL_INFO;

/*
 * Description:
 *      Write MAC Multicast Address Mask
 *
 * Parameters:
 *  In:
 *      uByteidx    - Index of Mask
 *      byData      - Mask Value to write
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvWriteMultiAddr(struct vnt_private *pDevice, u32 uByteIdx, u8 byData)
{
	u8 byData1;

    byData1 = byData;
    CONTROLnsRequestOut(pDevice,
                        MESSAGE_TYPE_WRITE,
                        (u16) (MAC_REG_MAR0 + uByteIdx),
                        MESSAGE_REQUEST_MACREG,
                        1,
                        &byData1);
}

/*
 * Description:
 *      Shut Down MAC
 *
 * Parameters:
 *  In:
 *  Out:
 *      none
 *
 *
 */
void MACbShutdown(struct vnt_private *pDevice)
{
    CONTROLnsRequestOutAsyn(pDevice,
                        MESSAGE_TYPE_MACSHUTDOWN,
                        0,
                        0,
                        0,
                        NULL
                        );
}

void MACvSetBBType(struct vnt_private *pDevice, u8 byType)
{
	u8 pbyData[2];

    pbyData[0] = byType;
    pbyData[1] = EnCFG_BBType_MASK;

    CONTROLnsRequestOut(pDevice,
                        MESSAGE_TYPE_WRITE_MASK,
                        MAC_REG_ENCFG0,
                        MESSAGE_REQUEST_MACREG,
			ARRAY_SIZE(pbyData),
                        pbyData
                        );
}

/*
 * Description:
 *      Disable the Key Entry by MISCFIFO
 *
 * Parameters:
 *  In:
 *      dwIoBase        - Base Address for MAC
 *
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvDisableKeyEntry(struct vnt_private *pDevice, u32 uEntryIdx)
{
	u8 byData;

    byData = (u8) uEntryIdx;

    //issue write misc fifo command to device
    CONTROLnsRequestOut(pDevice,
                        MESSAGE_TYPE_CLRKEYENTRY,
                        0,
                        0,
                        1,
                        &byData
                        );
}

/*
 * Description:
 *      Set the Key by MISCFIFO
 *
 * Parameters:
 *  In:
 *      dwIoBase        - Base Address for MAC
 *
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvSetKeyEntry(struct vnt_private *pDevice, u16 wKeyCtl, u32 uEntryIdx,
	u32 uKeyIdx, u8 *pbyAddr, u32 *pdwKey)
{
	u8 *pbyKey;
	u16 wOffset;
	u32 dwData1, dwData2;
	int ii;
	u8 pbyData[24];

	if (pDevice->byLocalID <= MAC_REVISION_A1)
		if (pDevice->vnt_mgmt.byCSSPK == KEY_CTL_CCMP)
			return;

    wOffset = MISCFIFO_KEYETRY0;
    wOffset += (uEntryIdx * MISCFIFO_KEYENTRYSIZE);

    dwData1 = 0;
    dwData1 |= wKeyCtl;
    dwData1 <<= 16;
    dwData1 |= MAKEWORD(*(pbyAddr+4), *(pbyAddr+5));

	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"1. wOffset: %d, Data: %X,"\
		" KeyCtl:%X\n", wOffset, dwData1, wKeyCtl);

    dwData2 = 0;
    dwData2 |= *(pbyAddr+3);
    dwData2 <<= 8;
    dwData2 |= *(pbyAddr+2);
    dwData2 <<= 8;
    dwData2 |= *(pbyAddr+1);
    dwData2 <<= 8;
    dwData2 |= *(pbyAddr+0);

	DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"2. wOffset: %d, Data: %X\n",
		wOffset, dwData2);

    pbyKey = (u8 *)pdwKey;

    pbyData[0] = (u8)dwData1;
    pbyData[1] = (u8)(dwData1>>8);
    pbyData[2] = (u8)(dwData1>>16);
    pbyData[3] = (u8)(dwData1>>24);
    pbyData[4] = (u8)dwData2;
    pbyData[5] = (u8)(dwData2>>8);
    pbyData[6] = (u8)(dwData2>>16);
    pbyData[7] = (u8)(dwData2>>24);
    for (ii = 8; ii < 24; ii++)
	pbyData[ii] = *pbyKey++;

    CONTROLnsRequestOut(pDevice,
                        MESSAGE_TYPE_SETKEY,
                        wOffset,
                        (u16)uKeyIdx,
			ARRAY_SIZE(pbyData),
                        pbyData
                        );

}

void MACvRegBitsOff(struct vnt_private *pDevice, u8 byRegOfs, u8 byBits)
{
	u8 pbyData[2];

    pbyData[0] = 0;
    pbyData[1] = byBits;

    CONTROLnsRequestOut(pDevice,
                        MESSAGE_TYPE_WRITE_MASK,
                        byRegOfs,
                        MESSAGE_REQUEST_MACREG,
			ARRAY_SIZE(pbyData),
                        pbyData
                        );
}

void MACvRegBitsOn(struct vnt_private *pDevice, u8 byRegOfs, u8 byBits)
{
	u8 pbyData[2];

    pbyData[0] = byBits;
    pbyData[1] = byBits;

    CONTROLnsRequestOut(pDevice,
                        MESSAGE_TYPE_WRITE_MASK,
                        byRegOfs,
                        MESSAGE_REQUEST_MACREG,
			ARRAY_SIZE(pbyData),
                        pbyData
                        );
}

void MACvWriteWord(struct vnt_private *pDevice, u8 byRegOfs, u16 wData)
{
	u8 pbyData[2];

    pbyData[0] = (u8)(wData & 0xff);
    pbyData[1] = (u8)(wData >> 8);

    CONTROLnsRequestOut(pDevice,
                        MESSAGE_TYPE_WRITE,
                        byRegOfs,
                        MESSAGE_REQUEST_MACREG,
			ARRAY_SIZE(pbyData),
                        pbyData
                        );

}

void MACvWriteBSSIDAddress(struct vnt_private *pDevice, u8 *pbyEtherAddr)
{
	u8 pbyData[6];

    pbyData[0] = *((u8 *)pbyEtherAddr);
    pbyData[1] = *((u8 *)pbyEtherAddr+1);
    pbyData[2] = *((u8 *)pbyEtherAddr+2);
    pbyData[3] = *((u8 *)pbyEtherAddr+3);
    pbyData[4] = *((u8 *)pbyEtherAddr+4);
    pbyData[5] = *((u8 *)pbyEtherAddr+5);

    CONTROLnsRequestOut(pDevice,
                        MESSAGE_TYPE_WRITE,
                        MAC_REG_BSSID0,
                        MESSAGE_REQUEST_MACREG,
			ARRAY_SIZE(pbyData),
                        pbyData
                        );
}

void MACvEnableProtectMD(struct vnt_private *pDevice)
{
	u8 pbyData[2];

    pbyData[0] = EnCFG_ProtectMd;
    pbyData[1] = EnCFG_ProtectMd;

    CONTROLnsRequestOut(pDevice,
                        MESSAGE_TYPE_WRITE_MASK,
                        MAC_REG_ENCFG0,
                        MESSAGE_REQUEST_MACREG,
			ARRAY_SIZE(pbyData),
                        pbyData
                        );
}

void MACvDisableProtectMD(struct vnt_private *pDevice)
{
	u8 pbyData[2];

    pbyData[0] = 0;
    pbyData[1] = EnCFG_ProtectMd;

    CONTROLnsRequestOut(pDevice,
                        MESSAGE_TYPE_WRITE_MASK,
                        MAC_REG_ENCFG0,
                        MESSAGE_REQUEST_MACREG,
			ARRAY_SIZE(pbyData),
                        pbyData
                        );
}

void MACvEnableBarkerPreambleMd(struct vnt_private *pDevice)
{
	u8 pbyData[2];

    pbyData[0] = EnCFG_BarkerPream;
    pbyData[1] = EnCFG_BarkerPream;

    CONTROLnsRequestOut(pDevice,
                        MESSAGE_TYPE_WRITE_MASK,
                        MAC_REG_ENCFG2,
                        MESSAGE_REQUEST_MACREG,
			ARRAY_SIZE(pbyData),
                        pbyData
                        );
}

void MACvDisableBarkerPreambleMd(struct vnt_private *pDevice)
{
	u8 pbyData[2];

    pbyData[0] = 0;
    pbyData[1] = EnCFG_BarkerPream;

    CONTROLnsRequestOut(pDevice,
                        MESSAGE_TYPE_WRITE_MASK,
                        MAC_REG_ENCFG2,
                        MESSAGE_REQUEST_MACREG,
			ARRAY_SIZE(pbyData),
                        pbyData
                        );
}

void MACvWriteBeaconInterval(struct vnt_private *pDevice, u16 wInterval)
{
	u8 pbyData[2];

	pbyData[0] = (u8)(wInterval & 0xff);
	pbyData[1] = (u8)(wInterval >> 8);

    CONTROLnsRequestOut(pDevice,
			MESSAGE_TYPE_WRITE,
			MAC_REG_BI,
			MESSAGE_REQUEST_MACREG,
			ARRAY_SIZE(pbyData),
			pbyData
			);
}
