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

#include "device.h"
#include "wcmd.h"

struct vnt_tx_buffer {
	u8 byType;
	u8 byPKTNO;
	u16 wTxByteCount;
	u32 adwTxKey[4];
	u16 wFIFOCtl;
	u16 wTimeStamp;
	u16 wFragCtl;
	u16 wReserved;
} __packed;

typedef struct tagSBEACON_BUFFER
{
    u8                            byType;
    u8                            byPKTNO;
    u16                            wTxByteCount;

    u16                            wFIFOCtl;
    u16                            wTimeStamp;
} __packed BEACON_BUFFER, *PBEACON_BUFFER;

void vDMA0_tx_80211(struct vnt_private *, struct sk_buff *skb);
int nsDMA_tx_packet(struct vnt_private *, u32 uDMAIdx, struct sk_buff *skb);
CMD_STATUS csMgmt_xmit(struct vnt_private *, struct vnt_tx_mgmt *);
CMD_STATUS csBeacon_xmit(struct vnt_private *, struct vnt_tx_mgmt *);
int bRelayPacketSend(struct vnt_private *, u8 *pbySkbData, u32 uDataLen,
	u32 uNodeIndex);

#endif /* __RXTX_H__ */
