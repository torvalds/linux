/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2019-2022  Realtek Corporation
 */

#ifndef __RTW89_WOW_H__
#define __RTW89_WOW_H__

enum rtw89_wake_reason {
	RTW89_WOW_RSN_RX_PTK_REKEY = 0x1,
	RTW89_WOW_RSN_RX_GTK_REKEY = 0x2,
	RTW89_WOW_RSN_RX_DEAUTH = 0x8,
	RTW89_WOW_RSN_DISCONNECT = 0x10,
	RTW89_WOW_RSN_RX_MAGIC_PKT = 0x21,
	RTW89_WOW_RSN_RX_PATTERN_MATCH = 0x23,
	RTW89_WOW_RSN_RX_NLO = 0x55,
};

int rtw89_wow_suspend(struct rtw89_dev *rtwdev, struct cfg80211_wowlan *wowlan);
int rtw89_wow_resume(struct rtw89_dev *rtwdev);

#endif
