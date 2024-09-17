// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2014  Realtek Corporation.*/

#include "../wifi.h"
#include "dm_common.h"
#include "../rtl8723ae/dm.h"
#include <linux/module.h>

/* These routines are common to RTL8723AE and RTL8723bE */

void rtl8723_dm_init_dynamic_txpower(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->dm.dynamic_txpower_enable = false;

	rtlpriv->dm.last_dtp_lvl = TXHIGHPWRLEVEL_NORMAL;
	rtlpriv->dm.dynamic_txhighpower_lvl = TXHIGHPWRLEVEL_NORMAL;
}
EXPORT_SYMBOL_GPL(rtl8723_dm_init_dynamic_txpower);

void rtl8723_dm_init_edca_turbo(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	rtlpriv->dm.current_turbo_edca = false;
	rtlpriv->dm.is_any_nonbepkts = false;
	rtlpriv->dm.is_cur_rdlstate = false;
}
EXPORT_SYMBOL_GPL(rtl8723_dm_init_edca_turbo);

void rtl8723_dm_init_dynamic_bb_powersaving(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct ps_t *dm_pstable = &rtlpriv->dm_pstable;

	dm_pstable->pre_ccastate = CCA_MAX;
	dm_pstable->cur_ccasate = CCA_MAX;
	dm_pstable->pre_rfstate = RF_MAX;
	dm_pstable->cur_rfstate = RF_MAX;
	dm_pstable->rssi_val_min = 0;
	dm_pstable->initialize = 0;
}
EXPORT_SYMBOL_GPL(rtl8723_dm_init_dynamic_bb_powersaving);
