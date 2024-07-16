/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file is part of wl12xx
 *
 * Copyright (C) 2012 Texas Instruments. All rights reserved.
 */

#ifndef __WL12XX_SCAN_H__
#define __WL12XX_SCAN_H__

#include "../wlcore/wlcore.h"
#include "../wlcore/cmd.h"
#include "../wlcore/scan.h"

#define WL12XX_MAX_CHANNELS_5GHZ 23

struct basic_scan_params {
	/* Scan option flags (WL1271_SCAN_OPT_*) */
	__le16 scan_options;
	u8 role_id;
	/* Number of scan channels in the list (maximum 30) */
	u8 n_ch;
	/* This field indicates the number of probe requests to send
	   per channel for an active scan */
	u8 n_probe_reqs;
	u8 tid_trigger;
	u8 ssid_len;
	u8 use_ssid_list;

	/* Rate bit field for sending the probes */
	__le32 tx_rate;

	u8 ssid[IEEE80211_MAX_SSID_LEN];
	/* Band to scan */
	u8 band;

	u8 scan_tag;
	u8 padding2[2];
} __packed;

struct basic_scan_channel_params {
	/* Duration in TU to wait for frames on a channel for active scan */
	__le32 min_duration;
	__le32 max_duration;
	__le32 bssid_lsb;
	__le16 bssid_msb;
	u8 early_termination;
	u8 tx_power_att;
	u8 channel;
	/* FW internal use only! */
	u8 dfs_candidate;
	u8 activity_detected;
	u8 pad;
} __packed;

struct wl1271_cmd_scan {
	struct wl1271_cmd_header header;

	struct basic_scan_params params;
	struct basic_scan_channel_params channels[WL1271_SCAN_MAX_CHANNELS];

	/* src mac address */
	u8 addr[ETH_ALEN];
	u8 padding[2];
} __packed;

struct wl1271_cmd_sched_scan_config {
	struct wl1271_cmd_header header;

	__le32 intervals[SCAN_MAX_CYCLE_INTERVALS];

	s8 rssi_threshold; /* for filtering (in dBm) */
	s8 snr_threshold;  /* for filtering (in dB) */

	u8 cycles;       /* maximum number of scan cycles */
	u8 report_after; /* report when this number of results are received */
	u8 terminate;    /* stop scanning after reporting */

	u8 tag;
	u8 bss_type; /* for filtering */
	u8 filter_type;

	u8 ssid_len;     /* For SCAN_SSID_FILTER_SPECIFIC */
	u8 ssid[IEEE80211_MAX_SSID_LEN];

	u8 n_probe_reqs; /* Number of probes requests per channel */

	u8 passive[SCAN_MAX_BANDS];
	u8 active[SCAN_MAX_BANDS];

	u8 dfs;

	u8 n_pactive_ch; /* number of pactive (passive until fw detects energy)
			    channels in BG band */
	u8 role_id;
	u8 padding[1];
	struct conn_scan_ch_params channels_2[MAX_CHANNELS_2GHZ];
	struct conn_scan_ch_params channels_5[WL12XX_MAX_CHANNELS_5GHZ];
	struct conn_scan_ch_params channels_4[MAX_CHANNELS_4GHZ];
} __packed;

struct wl1271_cmd_sched_scan_start {
	struct wl1271_cmd_header header;

	u8 tag;
	u8 role_id;
	u8 padding[2];
} __packed;

struct wl1271_cmd_sched_scan_stop {
	struct wl1271_cmd_header header;

	u8 tag;
	u8 role_id;
	u8 padding[2];
} __packed;

int wl12xx_scan_start(struct wl1271 *wl, struct wl12xx_vif *wlvif,
		      struct cfg80211_scan_request *req);
int wl12xx_scan_stop(struct wl1271 *wl, struct wl12xx_vif *wlvif);
void wl12xx_scan_completed(struct wl1271 *wl, struct wl12xx_vif *wlvif);
int wl12xx_sched_scan_start(struct wl1271 *wl, struct wl12xx_vif  *wlvif,
			    struct cfg80211_sched_scan_request *req,
			    struct ieee80211_scan_ies *ies);
void wl12xx_scan_sched_scan_stop(struct wl1271 *wl,  struct wl12xx_vif *wlvif);
#endif
