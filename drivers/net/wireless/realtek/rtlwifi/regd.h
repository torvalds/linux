/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __RTL_REGD_H__
#define __RTL_REGD_H__

/* for kernel 3.14 , both value are changed to IEEE80211_CHAN_NO_IR*/
#define IEEE80211_CHAN_NO_IBSS IEEE80211_CHAN_NO_IR
#define IEEE80211_CHAN_PASSIVE_SCAN IEEE80211_CHAN_NO_IR

struct country_code_to_enum_rd {
	u16 countrycode;
	const char *iso_name;
};

enum country_code_type_t {
	COUNTRY_CODE_FCC = 0,
	COUNTRY_CODE_IC = 1,
	COUNTRY_CODE_ETSI = 2,
	COUNTRY_CODE_SPAIN = 3,
	COUNTRY_CODE_FRANCE = 4,
	COUNTRY_CODE_MKK = 5,
	COUNTRY_CODE_MKK1 = 6,
	COUNTRY_CODE_ISRAEL = 7,
	COUNTRY_CODE_TELEC = 8,
	COUNTRY_CODE_MIC = 9,
	COUNTRY_CODE_GLOBAL_DOMAIN = 10,
	COUNTRY_CODE_WORLD_WIDE_13 = 11,
	COUNTRY_CODE_TELEC_NETGEAR = 12,
	COUNTRY_CODE_WORLD_WIDE_13_5G_ALL = 13,

	/*add new channel plan above this line */
	COUNTRY_CODE_MAX
};

int rtl_regd_init(struct ieee80211_hw *hw,
		  void (*reg_notifier) (struct wiphy *wiphy,
		  struct regulatory_request *request));
void rtl_reg_notifier(struct wiphy *wiphy, struct regulatory_request *request);

#endif
