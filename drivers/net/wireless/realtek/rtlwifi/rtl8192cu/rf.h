/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __RTL92CU_RF_H__
#define __RTL92CU_RF_H__

#define RF6052_MAX_TX_PWR		0x3F
#define RF6052_MAX_PATH			2

void rtl92cu_phy_rf6052_set_bandwidth(struct ieee80211_hw *hw, u8 bandwidth);
void rtl92c_phy_rf6052_set_cck_txpower(struct ieee80211_hw *hw,
				       u8 *ppowerlevel);
void rtl92c_phy_rf6052_set_ofdm_txpower(struct ieee80211_hw *hw,
					u8 *ppowerlevel, u8 channel);
bool rtl92cu_phy_rf6052_config(struct ieee80211_hw *hw);
bool rtl92cu_phy_config_rf_with_headerfile(struct ieee80211_hw *hw,
					   enum radio_path rfpath);
void rtl92cu_phy_rf6052_set_cck_txpower(struct ieee80211_hw *hw,
					u8 *ppowerlevel);
void rtl92cu_phy_rf6052_set_ofdm_txpower(struct ieee80211_hw *hw,
					 u8 *ppowerlevel, u8 channel);

#endif
