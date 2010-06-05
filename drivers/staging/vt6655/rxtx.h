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
 * File: rxtx.h
 *
 * Purpose:
 *
 * Author: Jerry Chen
 *
 * Date: Jun. 27, 2002
 *
 */

#ifndef __RXTX_H__
#define __RXTX_H__

#include "ttype.h"
#include "device.h"
#include "wcmd.h"

/*---------------------  Export Definitions -------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

/*
void vGenerateMACHeader(
    PSDevice pDevice,
    DWORD dwTxBufferAddr,
    unsigned char *pbySkbData,
    unsigned int cbPacketSize,
    BOOL bDMA0Used,
    PUINT pcbHeadSize,
    PUINT pcbAppendPayload
     );

void vProcessRxMACHeader (
    PSDevice pDevice,
    DWORD dwRxBufferAddr,
    unsigned int cbPacketSize,
    BOOL bIsWEP,
    PUINT pcbHeadSize
    );
*/


void
vGenerateMACHeader (
    PSDevice         pDevice,
    unsigned char *pbyBufferAddr,
    WORD             wDuration,
    PSEthernetHeader psEthHeader,
    BOOL             bNeedEncrypt,
    WORD             wFragType,
    unsigned int uDMAIdx,
    unsigned int uFragIdx
    );


unsigned int
cbGetFragCount(
    PSDevice         pDevice,
    PSKeyItem        pTransmitKey,
    unsigned int	cbFrameBodySize,
    PSEthernetHeader psEthHeader
    );


void
vGenerateFIFOHeader (
    PSDevice         pDevice,
    BYTE             byPktTyp,
    unsigned char *pbyTxBufferAddr,
    BOOL             bNeedEncrypt,
    unsigned int	cbPayloadSize,
    unsigned int	uDMAIdx,
    PSTxDesc         pHeadTD,
    PSEthernetHeader psEthHeader,
    unsigned char *pPacket,
    PSKeyItem        pTransmitKey,
    unsigned int	uNodeIndex,
    PUINT            puMACfragNum,
    PUINT            pcbHeaderSize
    );


void vDMA0_tx_80211(PSDevice  pDevice, struct sk_buff *skb, unsigned char *pbMPDU, unsigned int cbMPDULen);
CMD_STATUS csMgmt_xmit(PSDevice pDevice, PSTxMgmtPacket pPacket);
CMD_STATUS csBeacon_xmit(PSDevice pDevice, PSTxMgmtPacket pPacket);

#endif // __RXTX_H__
