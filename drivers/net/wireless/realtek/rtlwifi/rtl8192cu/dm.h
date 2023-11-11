/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#include "../rtl8192ce/dm.h"

void rtl92cu_dm_dynamic_txpower(struct ieee80211_hw *hw);
void dm_savepowerindex(struct ieee80211_hw *hw);
void dm_writepowerindex(struct ieee80211_hw *hw, u8 value);
void dm_restorepowerindex(struct ieee80211_hw *hw);
