/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2014  Realtek Corporation.*/

#ifndef __RTL92E_SW_H__
#define __RTL92E_SW_H__

int rtl92ee_init_sw_vars(struct ieee80211_hw *hw);
void rtl92ee_deinit_sw_vars(struct ieee80211_hw *hw);
bool rtl92ee_get_btc_status(void);

#endif
