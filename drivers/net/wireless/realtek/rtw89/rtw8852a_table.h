/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#ifndef __RTW89_8852A_TABLE_H__
#define __RTW89_8852A_TABLE_H__

#include "core.h"

extern const struct rtw89_phy_table rtw89_8852a_phy_bb_table;
extern const struct rtw89_phy_table rtw89_8852a_phy_radioa_table;
extern const struct rtw89_phy_table rtw89_8852a_phy_radiob_table;
extern const struct rtw89_phy_table rtw89_8852a_phy_nctl_table;
extern const struct rtw89_txpwr_table rtw89_8852a_byr_table;
extern const struct rtw89_phy_dig_gain_table rtw89_8852a_phy_dig_table;
extern const struct rtw89_txpwr_track_cfg rtw89_8852a_trk_cfg;
extern const s8 rtw89_8852a_txpwr_lmt_2g[RTW89_2G_BW_NUM][RTW89_NTX_NUM]
					[RTW89_RS_LMT_NUM][RTW89_BF_NUM]
					[RTW89_REGD_NUM][RTW89_2G_CH_NUM];
extern const s8 rtw89_8852a_txpwr_lmt_5g[RTW89_5G_BW_NUM][RTW89_NTX_NUM]
					[RTW89_RS_LMT_NUM][RTW89_BF_NUM]
					[RTW89_REGD_NUM][RTW89_5G_CH_NUM];
extern const s8 rtw89_8852a_txpwr_lmt_ru_2g[RTW89_RU_NUM][RTW89_NTX_NUM]
					   [RTW89_REGD_NUM][RTW89_2G_CH_NUM];
extern const s8 rtw89_8852a_txpwr_lmt_ru_5g[RTW89_RU_NUM][RTW89_NTX_NUM]
					   [RTW89_REGD_NUM][RTW89_5G_CH_NUM];

#endif
