/*
 * Copyright (c) 2011-2014 Espressif System.
 *
 *     MAC80211 support module
 */
#ifndef _ESP_MAC80211_H_
#define _ESP_MAC80211_H_

struct esp_80211_wmm_ac_param {
	u8 aci_aifsn; /* AIFSN, ACM, ACI */
	u8 cw; /* ECWmin, ECWmax (CW = 2^ECW - 1) */
	u16 txop_limit;
};

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
};


#endif /* _ESP_MAC80211_H_ */
