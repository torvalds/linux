/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __INC_RTL92S_RF_H
#define __INC_RTL92S_RF_H

#define	RF6052_MAX_TX_PWR	0x3F

void rtl92s_phy_rf6052_set_bandwidth(struct ieee80211_hw *hw,
				     u8 bandwidth);
bool rtl92s_phy_rf6052_config(struct ieee80211_hw *hw) ;
void rtl92s_phy_rf6052_set_ccktxpower(struct ieee80211_hw *hw,
				      u8 powerlevel);
void rtl92s_phy_rf6052_set_ofdmtxpower(struct ieee80211_hw *hw,
				       u8 *p_pwrlevel, u8 chnl);

#endif

