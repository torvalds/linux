/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
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

#ifndef __REALTEK_RTL_PCI_PS_H__
#define __REALTEK_RTL_PCI_PS_H__

#define MAX_SW_LPS_SLEEP_INTV	5

bool rtl_ps_set_rf_state(struct ieee80211_hw *hw,
			 enum rf_pwrstate state_toset, u32 changesource);
bool rtl_ps_enable_nic(struct ieee80211_hw *hw);
bool rtl_ps_disable_nic(struct ieee80211_hw *hw);
void rtl_ips_nic_off(struct ieee80211_hw *hw);
void rtl_ips_nic_on(struct ieee80211_hw *hw);
void rtl_ips_nic_off_wq_callback(void *data);
void rtl_lps_enter(struct ieee80211_hw *hw);
void rtl_lps_leave(struct ieee80211_hw *hw);

void rtl_swlps_beacon(struct ieee80211_hw *hw, void *data, unsigned int len);
void rtl_swlps_wq_callback(void *data);
void rtl_swlps_rfon_wq_callback(void *data);
void rtl_swlps_rf_awake(struct ieee80211_hw *hw);
void rtl_swlps_rf_sleep(struct ieee80211_hw *hw);
void rtl_p2p_ps_cmd(struct ieee80211_hw *hw, u8 p2p_ps_state);
void rtl_p2p_info(struct ieee80211_hw *hw, void *data, unsigned int len);
void rtl_lps_change_work_callback(struct work_struct *work);

#endif
