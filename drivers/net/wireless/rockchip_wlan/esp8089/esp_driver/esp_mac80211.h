/* Copyright (c) 2008 -2014 Espressif System.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 *     MAC80211 support module
 */
#ifndef _ESP_MAC80211_H_
#define _ESP_MAC80211_H_
#include <linux/ieee80211.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35))
struct ieee80211_hdr_3addr {
	__le16 frame_control;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctrl;
} __packed;
#endif

/*MGMT --------------------------------------------------------- */
struct esp_80211_deauth {
	struct ieee80211_hdr_3addr hdr;
	__le16 reason_code;
} __packed;

/*CONTROL --------------------------------------------------------- */

/*DATA --------------------------------------------------------- */
struct esp_80211_nulldata {
	struct ieee80211_hdr_3addr hdr;
} __packed;

/*IE --------------------------------------------------------- */
struct esp_80211_wmm_ac_param {
	u8 aci_aifsn; /* AIFSN, ACM, ACI */
	u8 cw; /* ECWmin, ECWmax (CW = 2^ECW - 1) */
	__le16 txop_limit;
} __packed;

struct esp_80211_wmm_param_element {
	/* Element ID： 221 (0xdd); length: 24 */
	/* required fields for WMM version 1 */
	u8 oui[3]; /* 00:50:f2 */
	u8 oui_type; /* 2 */
	u8 oui_subtype; /* 1 */
	u8 version; /* 1 for WMM version 1.0 */
	u8 qos_info; /* AP/STA specif QoS info */
	u8 reserved; /* 0 */
	struct esp_80211_wmm_ac_param ac[4]; /* AC_BE, AC_BK, AC_VI, AC_VO */
} __packed;


#endif /* _ESP_MAC80211_H_ */
