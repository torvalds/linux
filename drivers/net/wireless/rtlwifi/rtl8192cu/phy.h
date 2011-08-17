/******************************************************************************
 *
 * Copyright(c) 2009-2010  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#include "../rtl8192ce/phy.h"

void rtl92cu_bb_block_on(struct ieee80211_hw *hw);
bool rtl8192_phy_check_is_legal_rfpath(struct ieee80211_hw *hw, u32 rfpath);
void rtl92c_phy_set_io(struct ieee80211_hw *hw);
bool _rtl92cu_phy_config_mac_with_headerfile(struct ieee80211_hw *hw);
bool rtl92cu_phy_bb_config(struct ieee80211_hw *hw);
u32 rtl92cu_phy_query_rf_reg(struct ieee80211_hw *hw,
			     enum radio_path rfpath, u32 regaddr, u32 bitmask);
void rtl92cu_phy_set_rf_reg(struct ieee80211_hw *hw,
			    enum radio_path rfpath,
			    u32 regaddr, u32 bitmask, u32 data);
bool rtl92cu_phy_mac_config(struct ieee80211_hw *hw);
bool _rtl92cu_phy_config_bb_with_pgheaderfile(struct ieee80211_hw *hw,
					      u8 configtype);
void _rtl92cu_phy_lc_calibrate(struct ieee80211_hw *hw, bool is2t);
bool _rtl92cu_phy_config_bb_with_headerfile(struct ieee80211_hw *hw,
					    u8 configtype);
void rtl92cu_phy_set_bw_mode_callback(struct ieee80211_hw *hw);
bool rtl92cu_phy_set_rf_power_state(struct ieee80211_hw *hw,
				    enum rf_pwrstate rfpwr_state);
