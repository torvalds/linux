/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2013  Realtek Corporation.*/

#ifndef __RTL92C_RF_H__
#define __RTL92C_RF_H__

#define RF6052_MAX_TX_PWR		0x3F

void rtl88e_phy_rf6052_set_bandwidth(struct ieee80211_hw *hw,
				     u8 bandwidth);
void rtl88e_phy_rf6052_set_cck_txpower(struct ieee80211_hw *hw,
				       u8 *ppowerlevel);
void rtl88e_phy_rf6052_set_ofdm_txpower(struct ieee80211_hw *hw,
					u8 *ppowerlevel_ofdm,
					u8 *ppowerlevel_bw20,
					u8 *ppowerlevel_bw40,
					u8 channel);
bool rtl88e_phy_rf6052_config(struct ieee80211_hw *hw);

#endif
