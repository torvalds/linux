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

#ifndef __RTL8821AE_HW_H__
#define __RTL8821AE_HW_H__

void rtl8821ae_get_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val);
void rtl8821ae_read_eeprom_info(struct ieee80211_hw *hw);

void rtl8821ae_interrupt_recognized(struct ieee80211_hw *hw,
				    u32 *p_inta, u32 *p_intb);
int rtl8821ae_hw_init(struct ieee80211_hw *hw);
void rtl8821ae_card_disable(struct ieee80211_hw *hw);
void rtl8821ae_enable_interrupt(struct ieee80211_hw *hw);
void rtl8821ae_disable_interrupt(struct ieee80211_hw *hw);
int rtl8821ae_set_network_type(struct ieee80211_hw *hw,
			       enum nl80211_iftype type);
void rtl8821ae_set_check_bssid(struct ieee80211_hw *hw, bool check_bssid);
void rtl8821ae_set_qos(struct ieee80211_hw *hw, int aci);
void rtl8821ae_set_beacon_related_registers(struct ieee80211_hw *hw);
void rtl8821ae_set_beacon_interval(struct ieee80211_hw *hw);
void rtl8821ae_update_interrupt_mask(struct ieee80211_hw *hw,
				     u32 add_msr, u32 rm_msr);
void rtl8821ae_set_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val);
void rtl8821ae_update_hal_rate_tbl(struct ieee80211_hw *hw,
				   struct ieee80211_sta *sta,
				   u8 rssi_level);
void rtl8821ae_update_channel_access_setting(struct ieee80211_hw *hw);
bool rtl8821ae_gpio_radio_on_off_checking(struct ieee80211_hw *hw, u8 *valid);
void rtl8821ae_enable_hw_security_config(struct ieee80211_hw *hw);
void rtl8821ae_set_key(struct ieee80211_hw *hw, u32 key_index,
		       u8 *p_macaddr, bool is_group, u8 enc_algo,
		       bool is_wepkey, bool clear_all);

void rtl8821ae_bt_reg_init(struct ieee80211_hw *hw);
void rtl8821ae_bt_hw_init(struct ieee80211_hw *hw);
void rtl8821ae_suspend(struct ieee80211_hw *hw);
void rtl8821ae_resume(struct ieee80211_hw *hw);
void rtl8821ae_allow_all_destaddr(struct ieee80211_hw *hw,
				  bool allow_all_da,
				  bool write_into_reg);
void _rtl8821ae_stop_tx_beacon(struct ieee80211_hw *hw);
void _rtl8821ae_resume_tx_beacon(struct ieee80211_hw *hw);
void rtl8821ae_add_wowlan_pattern(struct ieee80211_hw *hw,
				  struct rtl_wow_pattern *rtl_pattern,
				  u8 index);

#endif
