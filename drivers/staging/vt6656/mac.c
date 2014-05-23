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
#include "control.h"

//static int          msglevel                =MSG_LEVEL_DEBUG;
static int          msglevel                =MSG_LEVEL_INFO;

/*
 * Description:
 *      Write MAC Multicast Address Mask
 *
 * Parameters:
 *  In:
 *	mc_filter (mac filter)
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvWriteMultiAddr(struct vnt_private *priv, u64 mc_filter)
{
	__le64 le_mc = cpu_to_le64(mc_filter);

	CONTROLnsRequestOut(priv, MESSAGE_TYPE_WRITE, MAC_REG_MAR0,
		MESSAGE_REQUEST_MACREG, sizeof(le_mc), (u8 *)&le_mc);
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
void MACbShutdown(struct vnt_private *priv)
{
	CONTROLnsRequestOut(priv, MESSAGE_TYPE_MACSHUTDOWN, 0, 0, 0, NULL);
}

void MACvSetBBType(struct vnt_private *priv, u8 type)
{
	u8 data[2];

	data[0] = type;
	data[1] = EnCFG_BBType_MASK;

	CONTROLnsRequestOut(priv, MESSAGE_TYPE_WRITE_MASK, MAC_REG_ENCFG0,
		MESSAGE_REQUEST_MACREG,	ARRAY_SIZE(data), data);
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
void MACvDisableKeyEntry(struct vnt_private *priv, u8 entry_idx)
{
	CONTROLnsRequestOut(priv, MESSAGE_TYPE_CLRKEYENTRY, 0, 0,
		sizeof(entry_idx), &entry_idx);
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

void MACvRegBitsOff(struct vnt_private *priv, u8 reg_ofs, u8 bits)
{
	u8 data[2];

	data[0] = 0;
	data[1] = bits;

	CONTROLnsRequestOut(priv, MESSAGE_TYPE_WRITE_MASK,
		reg_ofs, MESSAGE_REQUEST_MACREG, ARRAY_SIZE(data), data);
}

void MACvRegBitsOn(struct vnt_private *priv, u8 reg_ofs, u8 bits)
{
	u8 data[2];

	data[0] = bits;
	data[1] = bits;

	CONTROLnsRequestOut(priv, MESSAGE_TYPE_WRITE_MASK,
		reg_ofs, MESSAGE_REQUEST_MACREG, ARRAY_SIZE(data), data);
}

void MACvWriteWord(struct vnt_private *priv, u8 reg_ofs, u16 word)
{
	u8 data[2];

	data[0] = (u8)(word & 0xff);
	data[1] = (u8)(word >> 8);

	CONTROLnsRequestOut(priv, MESSAGE_TYPE_WRITE,
		reg_ofs, MESSAGE_REQUEST_MACREG, ARRAY_SIZE(data), data);
}

void MACvWriteBSSIDAddress(struct vnt_private *priv, u8 *addr)
{
	CONTROLnsRequestOut(priv, MESSAGE_TYPE_WRITE, MAC_REG_BSSID0,
		MESSAGE_REQUEST_MACREG, ETH_ALEN, addr);
}

void MACvEnableProtectMD(struct vnt_private *priv)
{
	u8 data[2];

	data[0] = EnCFG_ProtectMd;
	data[1] = EnCFG_ProtectMd;

	CONTROLnsRequestOut(priv, MESSAGE_TYPE_WRITE_MASK,
		MAC_REG_ENCFG0, MESSAGE_REQUEST_MACREG, ARRAY_SIZE(data), data);
}

void MACvDisableProtectMD(struct vnt_private *priv)
{
	u8 data[2];

	data[0] = 0;
	data[1] = EnCFG_ProtectMd;

	CONTROLnsRequestOut(priv, MESSAGE_TYPE_WRITE_MASK,
		MAC_REG_ENCFG0, MESSAGE_REQUEST_MACREG, ARRAY_SIZE(data), data);
}

void MACvEnableBarkerPreambleMd(struct vnt_private *priv)
{
	u8 data[2];

	data[0] = EnCFG_BarkerPream;
	data[1] = EnCFG_BarkerPream;

	CONTROLnsRequestOut(priv, MESSAGE_TYPE_WRITE_MASK,
		MAC_REG_ENCFG2, MESSAGE_REQUEST_MACREG, ARRAY_SIZE(data), data);
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
