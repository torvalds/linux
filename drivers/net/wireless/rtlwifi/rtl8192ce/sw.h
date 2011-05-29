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

#ifndef __RTL92CE_SW_H__
#define __RTL92CE_SW_H__

int rtl92c_init_sw_vars(struct ieee80211_hw *hw);
void rtl92c_deinit_sw_vars(struct ieee80211_hw *hw);
void rtl92c_init_var_map(struct ieee80211_hw *hw);
bool _rtl92c_cmd_send_packet(struct ieee80211_hw *hw,
			     struct sk_buff *skb);
void rtl92ce_phy_rf6052_set_cck_txpower(struct ieee80211_hw *hw,
					u8 *ppowerlevel);
void rtl92ce_phy_rf6052_set_ofdm_txpower(struct ieee80211_hw *hw,
					 u8 *ppowerlevel, u8 channel);
bool _rtl92ce_phy_config_bb_with_headerfile(struct ieee80211_hw *hw,
						  u8 configtype);
bool _rtl92ce_phy_config_bb_with_pgheaderfile(struct ieee80211_hw *hw,
						    u8 configtype);
void _rtl92ce_phy_lc_calibrate(struct ieee80211_hw *hw, bool is2t);
u32 rtl92ce_phy_query_rf_reg(struct ieee80211_hw *hw,
			    enum radio_path rfpath, u32 regaddr, u32 bitmask);
void rtl92ce_phy_set_bw_mode_callback(struct ieee80211_hw *hw);

#endif
