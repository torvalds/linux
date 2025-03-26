/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#ifndef __RTW89_SAR_H__
#define __RTW89_SAR_H__

#include "core.h"

#define RTW89_SAR_TXPWR_MAC_MAX 63
#define RTW89_SAR_TXPWR_MAC_MIN -64

struct rtw89_sar_parm {
	u32 center_freq;
};

struct rtw89_sar_handler {
	const char *descr_sar_source;
	u8 txpwr_factor_sar;
	int (*query_sar_config)(struct rtw89_dev *rtwdev,
				const struct rtw89_sar_parm *sar_parm, s32 *cfg);
};

extern const struct cfg80211_sar_capa rtw89_sar_capa;

s8 rtw89_query_sar(struct rtw89_dev *rtwdev, const struct rtw89_sar_parm *sar_parm);
int rtw89_print_sar(struct rtw89_dev *rtwdev, char *buf, size_t bufsz,
		    const struct rtw89_sar_parm *sar_parm);
int rtw89_print_tas(struct rtw89_dev *rtwdev, char *buf, size_t bufsz);
int rtw89_ops_set_sar_specs(struct ieee80211_hw *hw,
			    const struct cfg80211_sar_specs *sar);
void rtw89_tas_init(struct rtw89_dev *rtwdev);
void rtw89_tas_reset(struct rtw89_dev *rtwdev, bool force);
void rtw89_tas_track(struct rtw89_dev *rtwdev);
void rtw89_tas_scan(struct rtw89_dev *rtwdev, bool start);
void rtw89_tas_chanctx_cb(struct rtw89_dev *rtwdev,
			  enum rtw89_chanctx_state state);

#endif
