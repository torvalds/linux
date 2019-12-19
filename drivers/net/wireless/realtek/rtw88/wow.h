/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#ifndef __RTW_WOW_H__
#define __RTW_WOW_H__

enum rtw_wow_pattern_type {
	RTW_PATTERN_BROADCAST = 0,
	RTW_PATTERN_MULTICAST,
	RTW_PATTERN_UNICAST,
	RTW_PATTERN_VALID,
	RTW_PATTERN_INVALID,
};

enum rtw_wake_reason {
	RTW_WOW_RSN_RX_PTK_REKEY = 0x1,
	RTW_WOW_RSN_RX_GTK_REKEY = 0x2,
	RTW_WOW_RSN_RX_DEAUTH = 0x8,
	RTW_WOW_RSN_DISCONNECT = 0x10,
	RTW_WOW_RSN_RX_MAGIC_PKT = 0x21,
	RTW_WOW_RSN_RX_PATTERN_MATCH = 0x23,
};

struct rtw_fw_media_status_iter_data {
	struct rtw_dev *rtwdev;
	u8 connect;
};

struct rtw_fw_key_type_iter_data {
	struct rtw_dev *rtwdev;
	u8 group_key_type;
	u8 pairwise_key_type;
};

static inline bool rtw_wow_mgd_linked(struct rtw_dev *rtwdev)
{
	struct ieee80211_vif *wow_vif = rtwdev->wow.wow_vif;
	struct rtw_vif *rtwvif = (struct rtw_vif *)wow_vif->drv_priv;

	return (rtwvif->net_type == RTW_NET_MGD_LINKED);
}

int rtw_wow_suspend(struct rtw_dev *rtwdev, struct cfg80211_wowlan *wowlan);
int rtw_wow_resume(struct rtw_dev *rtwdev);

#endif
