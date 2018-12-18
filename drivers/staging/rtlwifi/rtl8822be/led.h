/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __RTL8822B_LED_H__
#define __RTL8822B_LED_H__

void rtl8822be_init_sw_leds(struct ieee80211_hw *hw);
void rtl8822be_sw_led_on(struct ieee80211_hw *hw, struct rtl_led *pled);
void rtl8822be_sw_led_off(struct ieee80211_hw *hw, struct rtl_led *pled);
void rtl8822be_led_control(struct ieee80211_hw *hw,
			   enum led_ctl_mode ledaction);
#endif
