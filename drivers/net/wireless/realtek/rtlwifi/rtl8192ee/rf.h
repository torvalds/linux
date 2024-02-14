/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2014  Realtek Corporation.*/

#ifndef __RTL92E_RF_H__
#define __RTL92E_RF_H__

#define RF6052_MAX_TX_PWR		0x3F

void rtl92ee_phy_rf6052_set_bandwidth(struct ieee80211_hw *hw,
				      u8 bandwidth);
bool rtl92ee_phy_rf6052_config(struct ieee80211_hw *hw);

#endif
