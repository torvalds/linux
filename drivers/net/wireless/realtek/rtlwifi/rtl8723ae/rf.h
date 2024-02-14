/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __RTL8723E_RF_H__
#define __RTL8723E_RF_H__

#define RF6052_MAX_TX_PWR		0x3F

void rtl8723e_phy_rf6052_set_bandwidth(struct ieee80211_hw *hw,
				       u8 bandwidth);
void rtl8723e_phy_rf6052_set_cck_txpower(struct ieee80211_hw *hw,
					 u8 *ppowerlevel);
void rtl8723e_phy_rf6052_set_ofdm_txpower(struct ieee80211_hw *hw,
					  u8 *ppowerlevel, u8 channel);
bool rtl8723e_phy_rf6052_config(struct ieee80211_hw *hw);

#endif
