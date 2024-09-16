/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2019-2022  Realtek Corporation
 */

#ifndef __RTW89_WOW_H__
#define __RTW89_WOW_H__

#define RTW89_KEY_PN_0 GENMASK_ULL(7, 0)
#define RTW89_KEY_PN_1 GENMASK_ULL(15, 8)
#define RTW89_KEY_PN_2 GENMASK_ULL(23, 16)
#define RTW89_KEY_PN_3 GENMASK_ULL(31, 24)
#define RTW89_KEY_PN_4 GENMASK_ULL(39, 32)
#define RTW89_KEY_PN_5 GENMASK_ULL(47, 40)

#define RTW89_IGTK_IPN_0 GENMASK_ULL(7, 0)
#define RTW89_IGTK_IPN_1 GENMASK_ULL(15, 8)
#define RTW89_IGTK_IPN_2 GENMASK_ULL(23, 16)
#define RTW89_IGTK_IPN_3 GENMASK_ULL(31, 24)
#define RTW89_IGTK_IPN_4 GENMASK_ULL(39, 32)
#define RTW89_IGTK_IPN_5 GENMASK_ULL(47, 40)
#define RTW89_IGTK_IPN_6 GENMASK_ULL(55, 48)
#define RTW89_IGTK_IPN_7 GENMASK_ULL(63, 56)

#define RTW89_WOW_VALID_CHECK 0xDD
#define RTW89_WOW_SYMBOL_CHK_PTK BIT(0)
#define RTW89_WOW_SYMBOL_CHK_GTK BIT(1)

enum rtw89_wake_reason {
	RTW89_WOW_RSN_RX_PTK_REKEY = 0x1,
	RTW89_WOW_RSN_RX_GTK_REKEY = 0x2,
	RTW89_WOW_RSN_RX_DEAUTH = 0x8,
	RTW89_WOW_RSN_DISCONNECT = 0x10,
	RTW89_WOW_RSN_RX_MAGIC_PKT = 0x21,
	RTW89_WOW_RSN_RX_PATTERN_MATCH = 0x23,
	RTW89_WOW_RSN_RX_NLO = 0x55,
};

enum rtw89_fw_alg {
	RTW89_WOW_FW_ALG_WEP40 = 0x1,
	RTW89_WOW_FW_ALG_WEP104 = 0x2,
	RTW89_WOW_FW_ALG_TKIP = 0x3,
	RTW89_WOW_FW_ALG_CCMP = 0x6,
	RTW89_WOW_FW_ALG_CCMP_256 = 0x7,
	RTW89_WOW_FW_ALG_GCMP = 0x8,
	RTW89_WOW_FW_ALG_GCMP_256 = 0x9,
	RTW89_WOW_FW_ALG_AES_CMAC = 0xa,
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

struct rtw89_cipher_info {
	u32 cipher;
	u8 fw_alg;
	enum ieee80211_key_len len;
};

struct rtw89_set_key_info_iter_data {
	u32 gtk_cipher;
	u32 igtk_cipher;
	bool rx_ready;
	bool error;
};

static inline int rtw89_wow_get_sec_hdr_len(struct rtw89_dev *rtwdev)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;

	if (!(rtwdev->chip->chip_id == RTL8852A || rtw89_is_rtl885xb(rtwdev)))
		return 0;

	switch (rtw_wow->ptk_alg) {
	case RTW89_WOW_FW_ALG_WEP40:
		return 4;
	case RTW89_WOW_FW_ALG_TKIP:
	case RTW89_WOW_FW_ALG_CCMP:
	case RTW89_WOW_FW_ALG_GCMP_256:
		return 8;
	default:
		return 0;
	}
}

#ifdef CONFIG_PM
static inline bool rtw89_wow_mgd_linked(struct rtw89_dev *rtwdev)
{
	struct ieee80211_vif *wow_vif = rtwdev->wow.wow_vif;
	struct rtw89_vif_link *rtwvif_link = (struct rtw89_vif_link *)wow_vif->drv_priv;

	return rtwvif_link->net_type == RTW89_NET_TYPE_INFRA;
}

static inline bool rtw89_wow_no_link(struct rtw89_dev *rtwdev)
{
	struct ieee80211_vif *wow_vif = rtwdev->wow.wow_vif;
	struct rtw89_vif_link *rtwvif_link = (struct rtw89_vif_link *)wow_vif->drv_priv;

	return rtwvif_link->net_type == RTW89_NET_TYPE_NO_LINK;
}

static inline bool rtw_wow_has_mgd_features(struct rtw89_dev *rtwdev)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;

	return !bitmap_empty(rtw_wow->flags, RTW89_WOW_FLAG_NUM);
}

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
