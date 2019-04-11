/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2010  Realtek Corporation.*/

#ifndef __RTL8821AE_RF_H__
#define __RTL8821AE_RF_H__

#define RF6052_MAX_TX_PWR		0x3F

void rtl8821ae_phy_rf6052_set_bandwidth(struct ieee80211_hw *hw,
					u8 bandwidth);
void rtl8821ae_phy_rf6052_set_cck_txpower(struct ieee80211_hw *hw,
					  u8 *ppowerlevel);
void rtl8821ae_phy_rf6052_set_ofdm_txpower(struct ieee80211_hw *hw,
					   u8 *ppowerlevel_ofdm,
					   u8 *ppowerlevel_bw20,
					   u8 *ppowerlevel_bw40,
					   u8 channel);
bool rtl8821ae_phy_rf6052_config(struct ieee80211_hw *hw);

#endif
