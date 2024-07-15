/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __RTL92D_HW_COMMON_H__
#define __RTL92D_HW_COMMON_H__

void rtl92de_stop_tx_beacon(struct ieee80211_hw *hw);
void rtl92de_resume_tx_beacon(struct ieee80211_hw *hw);
void rtl92d_get_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val);
void rtl92d_set_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val);
bool rtl92de_llt_write(struct ieee80211_hw *hw, u32 address, u32 data);
void rtl92de_enable_hw_security_config(struct ieee80211_hw *hw);
void rtl92de_set_qos(struct ieee80211_hw *hw, int aci);
void rtl92de_read_eeprom_info(struct ieee80211_hw *hw);
void rtl92de_update_hal_rate_tbl(struct ieee80211_hw *hw,
				 struct ieee80211_sta *sta,
				 u8 rssi_level, bool update_bw);
void rtl92de_update_channel_access_setting(struct ieee80211_hw *hw);
bool rtl92de_gpio_radio_on_off_checking(struct ieee80211_hw *hw, u8 *valid);
void rtl92de_set_key(struct ieee80211_hw *hw, u32 key_index,
		     u8 *p_macaddr, bool is_group, u8 enc_algo,
		     bool is_wepkey, bool clear_all);

#endif
