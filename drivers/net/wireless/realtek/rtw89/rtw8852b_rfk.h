/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2019-2022  Realtek Corporation
 */

#ifndef __RTW89_8852B_RFK_H__
#define __RTW89_8852B_RFK_H__

#include "core.h"

void rtw8852b_rck(struct rtw89_dev *rtwdev);
void rtw8852b_dack(struct rtw89_dev *rtwdev);
void rtw8852b_iqk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx);
void rtw8852b_rx_dck(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx);
void rtw8852b_dpk_init(struct rtw89_dev *rtwdev);
void rtw8852b_dpk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy);
void rtw8852b_dpk_track(struct rtw89_dev *rtwdev);
void rtw8852b_tssi(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy, bool hwtx_en);
void rtw8852b_tssi_scan(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy);
void rtw8852b_wifi_scan_notify(struct rtw89_dev *rtwdev, bool scan_start,
			       enum rtw89_phy_idx phy_idx);
void rtw8852b_set_channel_rf(struct rtw89_dev *rtwdev,
			     const struct rtw89_chan *chan,
			     enum rtw89_phy_idx phy_idx);

#endif
