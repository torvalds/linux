// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2023  Realtek Corporation
 */

#include "debug.h"
#include "phy.h"
#include "reg.h"
#include "rtw8922a.h"
#include "rtw8922a_rfk.h"

static void rtw8922a_tssi_cont_en(struct rtw89_dev *rtwdev, bool en,
				  enum rtw89_rf_path path)
{
	static const u32 tssi_trk_man[2] = {R_TSSI_PWR_P0, R_TSSI_PWR_P1};

	if (en)
		rtw89_phy_write32_mask(rtwdev, tssi_trk_man[path], B_TSSI_CONT_EN, 0);
	else
		rtw89_phy_write32_mask(rtwdev, tssi_trk_man[path], B_TSSI_CONT_EN, 1);
}

void rtw8922a_tssi_cont_en_phyidx(struct rtw89_dev *rtwdev, bool en, u8 phy_idx)
{
	if (rtwdev->mlo_dbcc_mode == MLO_1_PLUS_1_1RF) {
		if (phy_idx == RTW89_PHY_0)
			rtw8922a_tssi_cont_en(rtwdev, en, RF_PATH_A);
		else
			rtw8922a_tssi_cont_en(rtwdev, en, RF_PATH_B);
	} else {
		rtw8922a_tssi_cont_en(rtwdev, en, RF_PATH_A);
		rtw8922a_tssi_cont_en(rtwdev, en, RF_PATH_B);
	}
}
