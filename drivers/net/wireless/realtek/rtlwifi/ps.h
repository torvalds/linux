/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __REALTEK_RTL_PCI_PS_H__
#define __REALTEK_RTL_PCI_PS_H__

#define MAX_SW_LPS_SLEEP_INTV	5

bool rtl_ps_enable_nic(struct ieee80211_hw *hw);
bool rtl_ps_disable_nic(struct ieee80211_hw *hw);
void rtl_ips_nic_off(struct ieee80211_hw *hw);
void rtl_ips_nic_on(struct ieee80211_hw *hw);
void rtl_ips_nic_off_wq_callback(struct work_struct *work);
void rtl_lps_enter(struct ieee80211_hw *hw, bool may_block);
void rtl_lps_leave(struct ieee80211_hw *hw, bool may_block);

void rtl_lps_set_psmode(struct ieee80211_hw *hw, u8 rt_psmode);

void rtl_swlps_beacon(struct ieee80211_hw *hw, void *data, unsigned int len);
void rtl_swlps_wq_callback(struct work_struct *work);
void rtl_swlps_rfon_wq_callback(struct work_struct *work);
void rtl_swlps_rf_awake(struct ieee80211_hw *hw);
void rtl_swlps_rf_sleep(struct ieee80211_hw *hw);
void rtl_p2p_ps_cmd(struct ieee80211_hw *hw , u8 p2p_ps_state);
void rtl_p2p_info(struct ieee80211_hw *hw, void *data, unsigned int len);
void rtl_lps_change_work_callback(struct work_struct *work);

#endif
