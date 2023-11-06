/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __REALTEK_PCI92SE_LED_H__
#define __REALTEK_PCI92SE_LED_H__

void rtl92se_sw_led_on(struct ieee80211_hw *hw, enum rtl_led_pin pin);
void rtl92se_sw_led_off(struct ieee80211_hw *hw, enum rtl_led_pin pin);
void rtl92se_led_control(struct ieee80211_hw *hw, enum led_ctl_mode ledaction);

#endif
