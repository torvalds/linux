/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2019-2022  Realtek Corporation
 */

#ifndef __RTW89_8852C_RFK_H__
#define __RTW89_8852C_RFK_H__

#include "core.h"

void rtw8852c_mcc_get_ch_info(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy);
void rtw8852c_rck(struct rtw89_dev *rtwdev);
void rtw8852c_dack(struct rtw89_dev *rtwdev);
void rtw8852c_iqk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx);
void rtw8852c_rx_dck(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx, bool is_afe);
void rtw8852c_rx_dck_track(struct rtw89_dev *rtwdev);
void rtw8852c_dpk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy);
void rtw8852c_dpk_track(struct rtw89_dev *rtwdev);
void rtw8852c_tssi(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy);
void rtw8852c_tssi_scan(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy);
void rtw8852c_tssi_cont_en_phyidx(struct rtw89_dev *rtwdev, bool en, u8 phy_idx);
void rtw8852c_wifi_scan_notify(struct rtw89_dev *rtwdev, bool scan_start,
			       enum rtw89_phy_idx phy_idx);
void rtw8852c_set_channel_rf(struct rtw89_dev *rtwdev,
			     struct rtw89_channel_params *param,
			     enum rtw89_phy_idx phy_idx);
void rtw8852c_lck_init(struct rtw89_dev *rtwdev);
void rtw8852c_lck_track(struct rtw89_dev *rtwdev);

#endif
