/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2010  Realtek Corporation.*/

#ifndef __RTL8821AE_SW_H__
#define __RTL8821AE_SW_H__

int rtl8821ae_init_sw_vars(struct ieee80211_hw *hw);
void rtl8821ae_deinit_sw_vars(struct ieee80211_hw *hw);
void rtl8821ae_init_var_map(struct ieee80211_hw *hw);
bool rtl8821ae_get_btc_status(void);

#endif
