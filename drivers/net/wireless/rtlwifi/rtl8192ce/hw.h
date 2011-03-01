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

#ifndef __RTL92CE_HW_H__
#define __RTL92CE_HW_H__

void rtl92ce_get_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val);
void rtl92ce_read_eeprom_info(struct ieee80211_hw *hw);
void rtl92ce_interrupt_recognized(struct ieee80211_hw *hw,
				  u32 *p_inta, u32 *p_intb);
int rtl92ce_hw_init(struct ieee80211_hw *hw);
void rtl92ce_card_disable(struct ieee80211_hw *hw);
void rtl92ce_enable_interrupt(struct ieee80211_hw *hw);
void rtl92ce_disable_interrupt(struct ieee80211_hw *hw);
int rtl92ce_set_network_type(struct ieee80211_hw *hw, enum nl80211_iftype type);
void rtl92ce_set_qos(struct ieee80211_hw *hw, int aci);
void rtl92ce_set_beacon_related_registers(struct ieee80211_hw *hw);
void rtl92ce_set_beacon_interval(struct ieee80211_hw *hw);
void rtl92ce_update_interrupt_mask(struct ieee80211_hw *hw,
				   u32 add_msr, u32 rm_msr);
void rtl92ce_set_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val);
void rtl92ce_update_hal_rate_table(struct ieee80211_hw *hw);
void rtl92ce_update_hal_rate_mask(struct ieee80211_hw *hw, u8 rssi_level);
void rtl92ce_update_channel_access_setting(struct ieee80211_hw *hw);
bool rtl92ce_gpio_radio_on_off_checking(struct ieee80211_hw *hw, u8 *valid);
void rtl92ce_enable_hw_security_config(struct ieee80211_hw *hw);
void rtl92ce_set_key(struct ieee80211_hw *hw, u32 key_index,
		     u8 *p_macaddr, bool is_group, u8 enc_algo,
		     bool is_wepkey, bool clear_all);

#endif
