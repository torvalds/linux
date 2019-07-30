/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2010  Realtek Corporation.*/

#ifndef __RTL8821AE_LED_H__
#define __RTL8821AE_LED_H__

void rtl8821ae_init_sw_leds(struct ieee80211_hw *hw);
void rtl8821ae_sw_led_on(struct ieee80211_hw *hw, struct rtl_led *pled);
void rtl8812ae_sw_led_on(struct ieee80211_hw *hw, struct rtl_led *pled);
void rtl8821ae_sw_led_off(struct ieee80211_hw *hw, struct rtl_led *pled);
void rtl8812ae_sw_led_off(struct ieee80211_hw *hw, struct rtl_led *pled);
void rtl8821ae_led_control(struct ieee80211_hw *hw,
			   enum led_ctl_mode ledaction);

#endif
