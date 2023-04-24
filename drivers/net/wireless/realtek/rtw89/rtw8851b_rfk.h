/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2022-2023  Realtek Corporation
 */

#ifndef __RTW89_8851B_RFK_H__
#define __RTW89_8851B_RFK_H__

#include "core.h"

void rtw8851b_aack(struct rtw89_dev *rtwdev);
void rtw8851b_rck(struct rtw89_dev *rtwdev);
void rtw8851b_dack(struct rtw89_dev *rtwdev);
void rtw8851b_iqk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx);
void rtw8851b_set_channel_rf(struct rtw89_dev *rtwdev,
			     const struct rtw89_chan *chan,
			     enum rtw89_phy_idx phy_idx);

#endif
