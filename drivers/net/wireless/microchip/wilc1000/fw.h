/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2012 - 2018 Microchip Technology Inc., and its subsidiaries.
 * All rights reserved.
 */

#ifndef WILC_FW_H
#define WILC_FW_H

#include <linux/ieee80211.h>

#define WILC_MAX_NUM_STA			9
#define WILC_MAX_RATES_SUPPORTED		12
#define WILC_MAX_NUM_PMKIDS			16
#define WILC_MAX_NUM_SCANNED_CH			14

struct wilc_assoc_resp {
	__le16 capab_info;
	__le16 status_code;
	__le16 aid;
} __packed;

struct wilc_pmkid {
	u8 bssid[ETH_ALEN];
	u8 pmkid[WLAN_PMKID_LEN];
} __packed;

struct wilc_pmkid_attr {
	u8 numpmkid;
	struct wilc_pmkid pmkidlist[WILC_MAX_NUM_PMKIDS];
} __packed;

struct wilc_reg_frame {
	u8 reg;
	u8 reg_id;
	__le16 frame_type;
} __packed;

struct wilc_drv_handler {
	__le32 handler;
	u8 mode;
} __packed;

struct wilc_wep_key {
	u8 index;
	u8 key_len;
	u8 key[0];
} __packed;

struct wilc_sta_wpa_ptk {
	u8 mac_addr[ETH_ALEN];
	u8 key_len;
	u8 key[0];
} __packed;

struct wilc_ap_wpa_ptk {
	u8 mac_addr[ETH_ALEN];
	u8 index;
	u8 key_len;
	u8 key[0];
} __packed;

struct wilc_gtk_key {
	u8 mac_addr[ETH_ALEN];
	u8 rsc[8];
	u8 index;
	u8 key_len;
	u8 key[0];
} __packed;

struct wilc_op_mode {
	__le32 mode;
} __packed;

struct wilc_noa_opp_enable {
	u8 ct_window;
	u8 cnt;
	__le32 duration;
	__le32 interval;
	__le32 start_time;
} __packed;

struct wilc_noa_opp_disable {
	u8 cnt;
	__le32 duration;
	__le32 interval;
	__le32 start_time;
} __packed;

struct wilc_join_bss_param {
	char ssid[IEEE80211_MAX_SSID_LEN];
	u8 ssid_terminator;
	u8 bss_type;
	u8 ch;
	__le16 cap_info;
	u8 sa[ETH_ALEN];
	u8 bssid[ETH_ALEN];
	__le16 beacon_period;
	u8 dtim_period;
	u8 supp_rates[WILC_MAX_RATES_SUPPORTED + 1];
	u8 wmm_cap;
	u8 uapsd_cap;
	u8 ht_capable;
	u8 rsn_found;
	u8 rsn_grp_policy;
	u8 mode_802_11i;
	u8 p_suites[3];
	u8 akm_suites[3];
	u8 rsn_cap[2];
	u8 noa_enabled;
	__le32 tsf_lo;
	u8 idx;
	u8 opp_enabled;
	union {
		struct wilc_noa_opp_disable opp_dis;
		struct wilc_noa_opp_enable opp_en;
	};
} __packed;
#endif
