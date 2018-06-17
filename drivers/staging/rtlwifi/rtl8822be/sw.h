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

#ifndef __RTL8822B_SW_H__
#define __RTL8822B_SW_H__

int rtl8822be_init_sw_vars(struct ieee80211_hw *hw);
void rtl8822be_deinit_sw_vars(struct ieee80211_hw *hw);
bool rtl8822be_get_btc_status(void);
#endif
