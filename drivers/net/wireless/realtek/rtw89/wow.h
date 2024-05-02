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

struct rtw89_cipher_suite {
	u8 oui[3];
	u8 type;
} __packed;

struct rtw89_rsn_ie {
	u8 tag_number;
	u8 tag_length;
	__le16 rsn_version;
	struct rtw89_cipher_suite group_cipher_suite;
	__le16 pairwise_cipher_suite_cnt;
	struct rtw89_cipher_suite pairwise_cipher_suite;
	__le16 akm_cipher_suite_cnt;
	struct rtw89_cipher_suite akm_cipher_suite;
} __packed;

#ifdef CONFIG_PM
int rtw89_wow_suspend(struct rtw89_dev *rtwdev, struct cfg80211_wowlan *wowlan);
int rtw89_wow_resume(struct rtw89_dev *rtwdev);
void rtw89_wow_parse_akm(struct rtw89_dev *rtwdev, struct sk_buff *skb);
#else
static inline
void rtw89_wow_parse_akm(struct rtw89_dev *rtwdev, struct sk_buff *skb)
{
}
#endif

#endif
