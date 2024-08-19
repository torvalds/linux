/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#ifndef __RTW89_8852A_RFK_H__
#define __RTW89_8852A_RFK_H__

#include "core.h"

void rtw8852a_rck(struct rtw89_dev *rtwdev);
void rtw8852a_dack(struct rtw89_dev *rtwdev,
		   enum rtw89_chanctx_idx chanctx_idx);
void rtw8852a_iqk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
		  enum rtw89_chanctx_idx chanctx_idx);
void rtw8852a_rx_dck(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
		     bool is_afe, enum rtw89_chanctx_idx chanctx_idx);
void rtw8852a_dpk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
		  enum rtw89_chanctx_idx chanctx_idx);
void rtw8852a_dpk_track(struct rtw89_dev *rtwdev);
void rtw8852a_tssi(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
		   enum rtw89_chanctx_idx chanctx_idx);
void rtw8852a_tssi_scan(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy,
			const struct rtw89_chan *chan);
void rtw8852a_tssi_track(struct rtw89_dev *rtwdev);
void rtw8852a_wifi_scan_notify(struct rtw89_dev *rtwdev, bool scan_start,
			       enum rtw89_phy_idx phy_idx);

#endif
