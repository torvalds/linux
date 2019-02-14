/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2014  Realtek Corporation.*/

#ifndef __RTL8723BE_SW_H__
#define __RTL8723BE_SW_H__

int rtl8723be_init_sw_vars(struct ieee80211_hw *hw);
void rtl8723be_deinit_sw_vars(struct ieee80211_hw *hw);
void rtl8723be_init_var_map(struct ieee80211_hw *hw);
bool rtl8723be_get_btc_status(void);


#endif
