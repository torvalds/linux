/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file is part of wl18xx
 *
 * Copyright (C) 2011 Texas Instruments. All rights reserved.
 */

#ifndef __WL18XX_CMD_H__
#define __WL18XX_CMD_H__

#include "../wlcore/wlcore.h"
#include "../wlcore/acx.h"

struct wl18xx_cmd_channel_switch {
	struct wl1271_cmd_header header;

	u8 role_id;

	/* The new serving channel */
	u8 channel;
	/* Relative time of the serving channel switch in TBTT units */
	u8 switch_time;
	/* Stop the role TX, should expect it after radar detection */
	u8 stop_tx;

	__le32 local_supported_rates;

	u8 channel_type;
	u8 band;

	u8 padding[2];
} __packed;

struct wl18xx_cmd_smart_config_start {
	struct wl1271_cmd_header header;

	__le32 group_id_bitmask;
} __packed;

struct wl18xx_cmd_smart_config_set_group_key {
	struct wl1271_cmd_header header;

	__le32 group_id;

	u8 key[16];
} __packed;

struct wl18xx_cmd_dfs_radar_debug {
	struct wl1271_cmd_header header;

	u8 channel;
	u8 padding[3];
} __packed;

struct wl18xx_cmd_dfs_master_restart {
	struct wl1271_cmd_header header;

	u8 role_id;
	u8 padding[3];
} __packed;

/* cac_start and cac_stop share the same params */
struct wlcore_cmd_cac_start {
	struct wl1271_cmd_header header;

	u8 role_id;
	u8 channel;
	u8 band;
	u8 bandwidth;
} __packed;

int wl18xx_cmd_channel_switch(struct wl1271 *wl,
			      struct wl12xx_vif *wlvif,
			      struct ieee80211_channel_switch *ch_switch);
int wl18xx_cmd_smart_config_start(struct wl1271 *wl, u32 group_bitmap);
int wl18xx_cmd_smart_config_stop(struct wl1271 *wl);
int wl18xx_cmd_smart_config_set_group_key(struct wl1271 *wl, u16 group_id,
					  u8 key_len, u8 *key);
int wl18xx_cmd_set_cac(struct wl1271 *wl, struct wl12xx_vif *wlvif, bool start);
int wl18xx_cmd_radar_detection_debug(struct wl1271 *wl, u8 channel);
int wl18xx_cmd_dfs_master_restart(struct wl1271 *wl, struct wl12xx_vif *wlvif);
#endif
