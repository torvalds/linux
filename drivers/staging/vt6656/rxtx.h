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
#include "baseband.h"

/* MIC HDR data header */
struct vnt_mic_hdr {
	u8 id;
	u8 tx_priority;
	u8 mic_addr2[6];
	__be32 tsc_47_16;
	__be16 tsc_15_0;
	__be16 payload_len;
	__be16 hlen;
	__le16 frame_control;
	u8 addr1[6];
	u8 addr2[6];
	u8 addr3[6];
	__le16 seq_ctrl;
	u8 addr4[6];
	u16 packing; /* packing to 48 bytes */
} __packed;

/* RsvTime buffer header */
struct vnt_rrv_time_rts {
	u16 wRTSTxRrvTime_ba;
	u16 wRTSTxRrvTime_aa;
	u16 wRTSTxRrvTime_bb;
	u16 wReserved;
	u16 wTxRrvTime_b;
	u16 wTxRrvTime_a;
} __packed;

struct vnt_rrv_time_cts {
	u16 wCTSTxRrvTime_ba;
	u16 wReserved;
	u16 wTxRrvTime_b;
	u16 wTxRrvTime_a;
} __packed;

struct vnt_rrv_time_ab {
	u16 wRTSTxRrvTime;
	u16 wTxRrvTime;
} __packed;

/* TX data header */
struct vnt_tx_datahead_g {
	struct vnt_phy_field b;
	struct vnt_phy_field a;
	u16 wDuration_b;
	u16 wDuration_a;
	u16 wTimeStampOff_b;
	u16 wTimeStampOff_a;
} __packed;

struct vnt_tx_datahead_g_fb {
	struct vnt_phy_field b;
	struct vnt_phy_field a;
	u16 wDuration_b;
	u16 wDuration_a;
	u16 wDuration_a_f0;
	u16 wDuration_a_f1;
	u16 wTimeStampOff_b;
	u16 wTimeStampOff_a;
} __packed;

struct vnt_tx_datahead_ab {
	struct vnt_phy_field ab;
	u16 wDuration;
	u16 wTimeStampOff;
} __packed;

struct vnt_tx_datahead_a_fb {
	struct vnt_phy_field a;
	u16 wDuration;
	u16 wTimeStampOff;
	u16 wDuration_f0;
	u16 wDuration_f1;
} __packed;

/* RTS buffer header */
struct vnt_rts_g {
	struct vnt_phy_field b;
	struct vnt_phy_field a;
	u16 wDuration_ba;
	u16 wDuration_aa;
	u16 wDuration_bb;
	u16 wReserved;
	struct ieee80211_rts data;
	struct vnt_tx_datahead_g data_head;
} __packed;

struct vnt_rts_g_fb {
	struct vnt_phy_field b;
	struct vnt_phy_field a;
	u16 wDuration_ba;
	u16 wDuration_aa;
	u16 wDuration_bb;
	u16 wReserved;
	u16 wRTSDuration_ba_f0;
	u16 wRTSDuration_aa_f0;
	u16 wRTSDuration_ba_f1;
	u16 wRTSDuration_aa_f1;
	struct ieee80211_rts data;
	struct vnt_tx_datahead_g_fb data_head;
} __packed;

struct vnt_rts_ab {
	struct vnt_phy_field ab;
	u16 wDuration;
	u16 wReserved;
	struct ieee80211_rts data;
	struct vnt_tx_datahead_ab data_head;
} __packed;

struct vnt_rts_a_fb {
	struct vnt_phy_field a;
	u16 wDuration;
	u16 wReserved;
	u16 wRTSDuration_f0;
	u16 wRTSDuration_f1;
	struct ieee80211_rts data;
	struct vnt_tx_datahead_a_fb data_head;
} __packed;

/* CTS buffer header */
struct vnt_cts {
	struct vnt_phy_field b;
	u16 wDuration_ba;
	u16 wReserved;
	struct ieee80211_cts data;
	u16 reserved2;
	struct vnt_tx_datahead_g data_head;
} __packed;

struct vnt_cts_fb {
	struct vnt_phy_field b;
	u16 wDuration_ba;
	u16 wReserved;
	u16 wCTSDuration_ba_f0;
	u16 wCTSDuration_ba_f1;
	struct ieee80211_cts data;
	u16 reserved2;
	struct vnt_tx_datahead_g_fb data_head;
} __packed;

union vnt_tx_data_head {
	/* rts g */
	struct vnt_rts_g rts_g;
	struct vnt_rts_g_fb rts_g_fb;
	/* rts a/b */
	struct vnt_rts_ab rts_ab;
	struct vnt_rts_a_fb rts_a_fb;
	/* cts g */
	struct vnt_cts cts_g;
	struct vnt_cts_fb cts_g_fb;
	/* no rts/cts */
	struct vnt_tx_datahead_a_fb data_head_a_fb;
	struct vnt_tx_datahead_ab data_head_ab;
};

struct vnt_tx_mic_hdr {
	struct vnt_mic_hdr hdr;
	union vnt_tx_data_head head;
} __packed;

union vnt_tx {
	struct vnt_tx_mic_hdr mic;
	union vnt_tx_data_head head;
};

union vnt_tx_head {
	struct {
		struct vnt_rrv_time_rts rts;
		union vnt_tx tx;
	} __packed tx_rts;
	struct {
		struct vnt_rrv_time_cts cts;
		union vnt_tx tx;
	} __packed tx_cts;
	struct {
		struct vnt_rrv_time_ab ab;
		union vnt_tx tx;
	} __packed tx_ab;
};

struct vnt_tx_fifo_head {
	u32 adwTxKey[4];
	u16 wFIFOCtl;
	u16 wTimeStamp;
	u16 wFragCtl;
	u16 wReserved;
} __packed;

struct vnt_tx_buffer {
	u8 byType;
	u8 byPKTNO;
	u16 wTxByteCount;
	struct vnt_tx_fifo_head fifo_head;
	union vnt_tx_head tx_head;
} __packed;

struct vnt_tx_short_buf_head {
	u16 fifo_ctl;
	u16 time_stamp;
	struct vnt_phy_field ab;
	u16 duration;
	u16 time_stamp_off;
} __packed;

struct vnt_beacon_buffer {
	u8 byType;
	u8 byPKTNO;
	u16 wTxByteCount;
	struct vnt_tx_short_buf_head short_head;
	struct ieee80211_hdr hdr;
} __packed;

void vDMA0_tx_80211(struct vnt_private *, struct sk_buff *skb);
int nsDMA_tx_packet(struct vnt_private *, u32 uDMAIdx, struct sk_buff *skb);
CMD_STATUS csMgmt_xmit(struct vnt_private *, struct vnt_tx_mgmt *);
CMD_STATUS csBeacon_xmit(struct vnt_private *, struct vnt_tx_mgmt *);
int bRelayPacketSend(struct vnt_private *, u8 *pbySkbData, u32 uDataLen,
	u32 uNodeIndex);

#endif /* __RXTX_H__ */
