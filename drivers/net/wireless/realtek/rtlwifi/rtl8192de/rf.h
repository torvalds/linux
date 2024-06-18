/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __RTL92D_RF_H__
#define __RTL92D_RF_H__

bool rtl92d_phy_rf6052_config(struct ieee80211_hw *hw);
bool rtl92d_phy_enable_anotherphy(struct ieee80211_hw *hw, bool bmac0);
void rtl92d_phy_powerdown_anotherphy(struct ieee80211_hw *hw, bool bmac0);

#endif
