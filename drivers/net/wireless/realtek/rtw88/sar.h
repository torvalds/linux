/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2021  Realtek Corporation
 */

#include "main.h"

/* NL80211_SAR_TYPE_POWER means unit is in 0.25 dBm,
 * where 0.25 = 1/4 = 2^(-2), so make factor 2.
 */
#define RTW_COMMON_SAR_FCT 2

struct rtw_sar_arg {
	u8 sar_band;
	u8 path;
	u8 rs;
};

extern const struct cfg80211_sar_capa rtw_sar_capa;

s8 rtw_query_sar(struct rtw_dev *rtwdev, const struct rtw_sar_arg *arg);
int rtw_set_sar_specs(struct rtw_dev *rtwdev,
		      const struct cfg80211_sar_specs *sar);
