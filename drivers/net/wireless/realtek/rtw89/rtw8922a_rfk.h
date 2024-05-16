/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2023  Realtek Corporation
 */

#ifndef __RTW89_8922A_RFK_H__
#define __RTW89_8922A_RFK_H__

#include "core.h"

void rtw8922a_tssi_cont_en_phyidx(struct rtw89_dev *rtwdev, bool en, u8 phy_idx);
void rtw8922a_set_channel_rf(struct rtw89_dev *rtwdev,
			     const struct rtw89_chan *chan,
			     enum rtw89_phy_idx phy_idx);
void rtw8922a_rfk_hw_init(struct rtw89_dev *rtwdev);
void rtw8922a_pre_set_channel_rf(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx);
void rtw8922a_post_set_channel_rf(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx);

#endif
