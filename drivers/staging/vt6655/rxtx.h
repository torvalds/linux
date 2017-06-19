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

#define DEFAULT_MSDU_LIFETIME_RES_64us	8000 /* 64us */
#define DEFAULT_MGN_LIFETIME_RES_64us	125  /* 64us */


/*---------------------  Export Definitions -------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

/* MIC HDR data header */
struct vnt_mic_hdr {
	u8 id;
	u8 tx_priority;
	u8 mic_addr2[ETH_ALEN];
	u8 ccmp_pn[IEEE80211_CCMP_PN_LEN];
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

/* RsvTime buffer header */
struct vnt_rrv_time_rts {
	__le16 rts_rrv_time_ba;
	__le16 rts_rrv_time_aa;
	__le16 rts_rrv_time_bb;
	u16 reserved;
	__le16 rrv_time_b;
	__le16 rrv_time_a;
} __packed;

struct vnt_rrv_time_cts {
	__le16 cts_rrv_time_ba;
	u16 reserved;
	__le16 rrv_time_b;
	__le16 rrv_time_a;
} __packed;

struct vnt_rrv_time_ab {
	__le16 rts_rrv_time;
	__le16 rrv_time;
} __packed;

/* TX data header */
struct vnt_tx_datahead_g {
	struct vnt_phy_field b;
	struct vnt_phy_field a;
	__le16 duration_b;
	__le16 duration_a;
	__le16 time_stamp_off_b;
	__le16 time_stamp_off_a;
} __packed;

struct vnt_tx_datahead_g_fb {
	struct vnt_phy_field b;
	struct vnt_phy_field a;
	__le16 duration_b;
	__le16 duration_a;
	__le16 duration_a_f0;
	__le16 duration_a_f1;
	__le16 time_stamp_off_b;
	__le16 time_stamp_off_a;
} __packed;

struct vnt_tx_datahead_ab {
	struct vnt_phy_field ab;
	__le16 duration;
	__le16 time_stamp_off;
} __packed;

struct vnt_tx_datahead_a_fb {
	struct vnt_phy_field a;
	__le16 duration;
	__le16 time_stamp_off;
	__le16 duration_f0;
	__le16 duration_f1;
} __packed;

/* RTS buffer header */
struct vnt_rts_g {
	struct vnt_phy_field b;
	struct vnt_phy_field a;
	__le16 duration_ba;
	__le16 duration_aa;
	__le16 duration_bb;
	u16 reserved;
	struct ieee80211_rts data;
} __packed;

struct vnt_rts_g_fb {
	struct vnt_phy_field b;
	struct vnt_phy_field a;
	__le16 duration_ba;
	__le16 duration_aa;
	__le16 duration_bb;
	u16 wReserved;
	__le16 rts_duration_ba_f0;
	__le16 rts_duration_aa_f0;
	__le16 rts_duration_ba_f1;
	__le16 rts_duration_aa_f1;
	struct ieee80211_rts data;
} __packed;

struct vnt_rts_ab {
	struct vnt_phy_field ab;
	__le16 duration;
	u16 reserved;
	struct ieee80211_rts data;
} __packed;

struct vnt_rts_a_fb {
	struct vnt_phy_field a;
	__le16 duration;
	u16 reserved;
	__le16 rts_duration_f0;
	__le16 rts_duration_f1;
	struct ieee80211_rts data;
} __packed;

/* CTS buffer header */
struct vnt_cts {
	struct vnt_phy_field b;
	__le16 duration_ba;
	u16 reserved;
	struct ieee80211_cts data;
	u16 reserved2;
} __packed;

struct vnt_cts_fb {
	struct vnt_phy_field b;
	__le16 duration_ba;
	u16 reserved;
	__le16 cts_duration_ba_f0;
	__le16 cts_duration_ba_f1;
	struct ieee80211_cts data;
	u16 reserved2;
} __packed;

struct vnt_tx_fifo_head {
	u8 tx_key[WLAN_KEY_LEN_CCMP];
	__le16 fifo_ctl;
	__le16 time_stamp;
	__le16 frag_ctl;
	__le16 current_rate;
} __packed;

struct vnt_tx_short_buf_head {
	__le16 fifo_ctl;
	u16 time_stamp;
	struct vnt_phy_field ab;
	__le16 duration;
	__le16 time_stamp_off;
} __packed;

int vnt_generate_fifo_header(struct vnt_private *priv, u32 dma_idx,
			     struct vnt_tx_desc *head_td, struct sk_buff *skb);
int vnt_beacon_make(struct vnt_private *priv, struct ieee80211_vif *vif);
int vnt_beacon_enable(struct vnt_private *priv, struct ieee80211_vif *vif,
		      struct ieee80211_bss_conf *conf);

#endif /* __RXTX_H__ */
