/*
 * This file is part of wl1271
 *
 * Copyright (C) 2009-2010 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __WL1271_SCAN_H__
#define __WL1271_SCAN_H__

#include "wl1271.h"

int wl1271_scan(struct wl1271 *wl, const u8 *ssid, size_t ssid_len,
		struct cfg80211_scan_request *req);
int wl1271_scan_build_probe_req(struct wl1271 *wl,
				const u8 *ssid, size_t ssid_len,
				const u8 *ie, size_t ie_len, u8 band);
void wl1271_scan_stm(struct wl1271 *wl);
void wl1271_scan_complete_work(struct work_struct *work);

#define WL1271_SCAN_MAX_CHANNELS       24
#define WL1271_SCAN_DEFAULT_TAG        1
#define WL1271_SCAN_CURRENT_TX_PWR     0
#define WL1271_SCAN_OPT_ACTIVE         0
#define WL1271_SCAN_OPT_PASSIVE	       1
#define WL1271_SCAN_OPT_PRIORITY_HIGH  4
#define WL1271_SCAN_CHAN_MIN_DURATION  30000  /* TU */
#define WL1271_SCAN_CHAN_MAX_DURATION  60000  /* TU */
#define WL1271_SCAN_BAND_2_4_GHZ 0
#define WL1271_SCAN_BAND_5_GHZ 1
#define WL1271_SCAN_PROBE_REQS 3

enum {
	WL1271_SCAN_STATE_IDLE,
	WL1271_SCAN_STATE_2GHZ_ACTIVE,
	WL1271_SCAN_STATE_2GHZ_PASSIVE,
	WL1271_SCAN_STATE_5GHZ_ACTIVE,
	WL1271_SCAN_STATE_5GHZ_PASSIVE,
	WL1271_SCAN_STATE_DONE
};

struct basic_scan_params {
	__le32 rx_config_options;
	__le32 rx_filter_options;
	/* Scan option flags (WL1271_SCAN_OPT_*) */
	__le16 scan_options;
	/* Number of scan channels in the list (maximum 30) */
	u8 n_ch;
	/* This field indicates the number of probe requests to send
	   per channel for an active scan */
	u8 n_probe_reqs;
	/* Rate bit field for sending the probes */
	__le32 tx_rate;
	u8 tid_trigger;
	u8 ssid_len;
	/* in order to align */
	u8 padding1[2];
	u8 ssid[IW_ESSID_MAX_SIZE];
	/* Band to scan */
	u8 band;
	u8 use_ssid_list;
	u8 scan_tag;
	u8 padding2;
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
} __packed;

struct wl1271_cmd_trigger_scan_to {
	struct wl1271_cmd_header header;

	__le32 timeout;
} __packed;

#endif /* __WL1271_SCAN_H__ */
