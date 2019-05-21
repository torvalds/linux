/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __RTL92CU_SW_H__
#define __RTL92CU_SW_H__

#define EFUSE_MAX_SECTION	16

void rtl92cu_phy_rf6052_set_cck_txpower(struct ieee80211_hw *hw,
					u8 *powerlevel);
void rtl92cu_phy_rf6052_set_ofdm_txpower(struct ieee80211_hw *hw,
					u8 *ppowerlevel, u8 channel);
bool _rtl92cu_phy_config_bb_with_headerfile(struct ieee80211_hw *hw,
					    u8 configtype);
bool _rtl92cu_phy_config_bb_with_pgheaderfile(struct ieee80211_hw *hw,
						    u8 configtype);
void _rtl92cu_phy_lc_calibrate(struct ieee80211_hw *hw, bool is2t);
void rtl92cu_phy_set_rf_reg(struct ieee80211_hw *hw,
			   enum radio_path rfpath,
			   u32 regaddr, u32 bitmask, u32 data);
bool rtl92cu_phy_set_rf_power_state(struct ieee80211_hw *hw,
				   enum rf_pwrstate rfpwr_state);
u32 rtl92cu_phy_query_rf_reg(struct ieee80211_hw *hw,
			    enum radio_path rfpath, u32 regaddr, u32 bitmask);
void rtl92cu_phy_set_bw_mode_callback(struct ieee80211_hw *hw);

#endif
