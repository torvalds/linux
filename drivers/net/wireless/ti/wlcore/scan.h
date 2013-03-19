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

#ifndef __SCAN_H__
#define __SCAN_H__

#include "wlcore.h"

int wlcore_scan(struct wl1271 *wl, struct ieee80211_vif *vif,
		const u8 *ssid, size_t ssid_len,
		struct cfg80211_scan_request *req);
int wl1271_scan_build_probe_req(struct wl1271 *wl,
				const u8 *ssid, size_t ssid_len,
				const u8 *ie, size_t ie_len, u8 band);
void wl1271_scan_stm(struct wl1271 *wl, struct wl12xx_vif *wlvif);
void wl1271_scan_complete_work(struct work_struct *work);
int wl1271_scan_sched_scan_config(struct wl1271 *wl,
				     struct wl12xx_vif *wlvif,
				     struct cfg80211_sched_scan_request *req,
				     struct ieee80211_sched_scan_ies *ies);
int wl1271_scan_sched_scan_start(struct wl1271 *wl, struct wl12xx_vif *wlvif);
void wlcore_scan_sched_scan_results(struct wl1271 *wl);

#define WL1271_SCAN_MAX_CHANNELS       24
#define WL1271_SCAN_DEFAULT_TAG        1
#define WL1271_SCAN_CURRENT_TX_PWR     0
#define WL1271_SCAN_OPT_ACTIVE         0
#define WL1271_SCAN_OPT_PASSIVE	       1
#define WL1271_SCAN_OPT_SPLIT_SCAN     2
#define WL1271_SCAN_OPT_PRIORITY_HIGH  4
/* scan even if we fail to enter psm */
#define WL1271_SCAN_OPT_FORCE          8
#define WL1271_SCAN_BAND_2_4_GHZ 0
#define WL1271_SCAN_BAND_5_GHZ 1

#define WL1271_SCAN_TIMEOUT    30000 /* msec */

enum {
	WL1271_SCAN_STATE_IDLE,
	WL1271_SCAN_STATE_2GHZ_ACTIVE,
	WL1271_SCAN_STATE_2GHZ_PASSIVE,
	WL1271_SCAN_STATE_5GHZ_ACTIVE,
	WL1271_SCAN_STATE_5GHZ_PASSIVE,
	WL1271_SCAN_STATE_DONE
};

struct wl1271_cmd_trigger_scan_to {
	struct wl1271_cmd_header header;

	__le32 timeout;
} __packed;

#define MAX_CHANNELS_2GHZ	14
#define MAX_CHANNELS_4GHZ	4

/*
 * This max value here is used only for the struct definition of
 * wlcore_scan_channels. This struct is used by both 12xx
 * and 18xx (which have different max 5ghz channels value).
 * In order to make sure this is large enough, just use the
 * max possible 5ghz channels.
 */
#define MAX_CHANNELS_5GHZ	42

#define SCAN_MAX_CYCLE_INTERVALS 16
#define SCAN_MAX_BANDS 3

enum {
	SCAN_SSID_FILTER_ANY      = 0,
	SCAN_SSID_FILTER_SPECIFIC = 1,
	SCAN_SSID_FILTER_LIST     = 2,
	SCAN_SSID_FILTER_DISABLED = 3
};

enum {
	SCAN_BSS_TYPE_INDEPENDENT,
	SCAN_BSS_TYPE_INFRASTRUCTURE,
	SCAN_BSS_TYPE_ANY,
};

#define SCAN_CHANNEL_FLAGS_DFS		BIT(0) /* channel is passive until an
						  activity is detected on it */
#define SCAN_CHANNEL_FLAGS_DFS_ENABLED	BIT(1)

struct conn_scan_ch_params {
	__le16 min_duration;
	__le16 max_duration;
	__le16 passive_duration;

	u8  channel;
	u8  tx_power_att;

	/* bit 0: DFS channel; bit 1: DFS enabled */
	u8  flags;

	u8  padding[3];
} __packed;

#define SCHED_SCAN_MAX_SSIDS 16

enum {
	SCAN_SSID_TYPE_PUBLIC = 0,
	SCAN_SSID_TYPE_HIDDEN = 1,
};

struct wl1271_ssid {
	u8 type;
	u8 len;
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	/* u8 padding[2]; */
} __packed;

struct wl1271_cmd_sched_scan_ssid_list {
	struct wl1271_cmd_header header;

	u8 n_ssids;
	struct wl1271_ssid ssids[SCHED_SCAN_MAX_SSIDS];
	u8 role_id;
	u8 padding[2];
} __packed;

struct wlcore_scan_channels {
	u8 passive[SCAN_MAX_BANDS]; /* number of passive scan channels */
	u8 active[SCAN_MAX_BANDS];  /* number of active scan channels */
	u8 dfs;		   /* number of dfs channels in 5ghz */
	u8 passive_active; /* number of passive before active channels 2.4ghz */

	struct conn_scan_ch_params channels_2[MAX_CHANNELS_2GHZ];
	struct conn_scan_ch_params channels_5[MAX_CHANNELS_5GHZ];
	struct conn_scan_ch_params channels_4[MAX_CHANNELS_4GHZ];
};

enum {
	SCAN_TYPE_SEARCH	= 0,
	SCAN_TYPE_PERIODIC	= 1,
	SCAN_TYPE_TRACKING	= 2,
};

bool
wlcore_set_scan_chan_params(struct wl1271 *wl,
			    struct wlcore_scan_channels *cfg,
			    struct ieee80211_channel *channels[],
			    u32 n_channels,
			    u32 n_ssids,
			    int scan_type);

int
wlcore_scan_sched_scan_ssid_list(struct wl1271 *wl,
				 struct wl12xx_vif *wlvif,
				 struct cfg80211_sched_scan_request *req);

#endif /* __WL1271_SCAN_H__ */
