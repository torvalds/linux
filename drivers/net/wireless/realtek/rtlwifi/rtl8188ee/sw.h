/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2013  Realtek Corporation.*/

#ifndef __RTL92CE_SW_H__
#define __RTL92CE_SW_H__

int rtl88e_init_sw_vars(struct ieee80211_hw *hw);
void rtl88e_deinit_sw_vars(struct ieee80211_hw *hw);
bool rtl88e_get_btc_status(void);


#endif
