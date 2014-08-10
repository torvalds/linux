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

/* MIC HDR data header */
struct vnt_mic_hdr {
	u8 id;
	u8 tx_priority;
	u8 mic_addr2[ETH_ALEN];
	u8 ccmp_pn[6];
	__be16 payload_len;
	__be16 hlen;
	__le16 frame_control;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctrl;
	u8 addr4[ETH_ALEN];
	u16 packing; /* packing to 48 bytes */
} __packed;

void
vGenerateMACHeader(
	struct vnt_private *,
	unsigned char *pbyBufferAddr,
	unsigned short wDuration,
	PSEthernetHeader psEthHeader,
	bool bNeedEncrypt,
	unsigned short wFragType,
	unsigned int uDMAIdx,
	unsigned int uFragIdx
);

unsigned int
cbGetFragCount(
	struct vnt_private *,
	PSKeyItem        pTransmitKey,
	unsigned int	cbFrameBodySize,
	PSEthernetHeader psEthHeader
);

void
vGenerateFIFOHeader(struct vnt_private *, unsigned char byPktTyp,
		    unsigned char *pbyTxBufferAddr, bool bNeedEncrypt,
		    unsigned int cbPayloadSize, unsigned int uDMAIdx,
		    PSTxDesc pHeadTD, PSEthernetHeader psEthHeader,
		    unsigned char *pPacket, PSKeyItem pTransmitKey,
		    unsigned int uNodeIndex, unsigned int *puMACfragNum,
		    unsigned int *pcbHeaderSize);

void vDMA0_tx_80211(struct vnt_private *, struct sk_buff *skb,
		    unsigned char *pbMPDU, unsigned int cbMPDULen);
CMD_STATUS csMgmt_xmit(struct vnt_private *, PSTxMgmtPacket pPacket);
CMD_STATUS csBeacon_xmit(struct vnt_private *, PSTxMgmtPacket pPacket);

#endif // __RXTX_H__
