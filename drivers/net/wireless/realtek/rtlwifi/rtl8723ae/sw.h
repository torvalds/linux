/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __RTL8723E_SW_H__
#define __RTL8723E_SW_H__

int rtl8723e_init_sw_vars(struct ieee80211_hw *hw);
void rtl8723e_deinit_sw_vars(struct ieee80211_hw *hw);
void rtl8723e_init_var_map(struct ieee80211_hw *hw);
bool rtl8723e_get_btc_status(void);


#endif
