// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2024  Realtek Corporation.*/

#include "../wifi.h"
#include "../core.h"
#include "../rtl8192d/reg.h"
#include "../rtl8192d/def.h"
#include "../rtl8192d/dm_common.h"
#include "../rtl8192d/fw_common.h"
#include "dm.h"

static void rtl92du_dm_init_1r_cca(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct ps_t *dm_pstable = &rtlpriv->dm_pstable;

	dm_pstable->pre_ccastate = CCA_MAX;
	dm_pstable->cur_ccasate = CCA_MAX;
}

static void rtl92du_dm_1r_cca(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct ps_t *dm_pstable = &rtlpriv->dm_pstable;
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	int pwdb = rtlpriv->dm_digtable.min_undec_pwdb_for_dm;

	if (rtlhal->macphymode != SINGLEMAC_SINGLEPHY ||
	    rtlhal->current_bandtype != BAND_ON_5G)
		return;

	if (pwdb != 0) {
		if (dm_pstable->pre_ccastate == CCA_2R ||
		    dm_pstable->pre_ccastate == CCA_MAX)
			dm_pstable->cur_ccasate = (pwdb >= 35) ? CCA_1R : CCA_2R;
		else
			dm_pstable->cur_ccasate = (pwdb <= 30) ? CCA_2R : CCA_1R;
	} else {
		dm_pstable->cur_ccasate = CCA_MAX;
	}

	if (dm_pstable->pre_ccastate == dm_pstable->cur_ccasate)
		return;

	rtl_dbg(rtlpriv, COMP_BB_POWERSAVING, DBG_TRACE,
		"Old CCA state: %d new CCA state: %d\n",
		dm_pstable->pre_ccastate, dm_pstable->cur_ccasate);

	if (dm_pstable->cur_ccasate == CCA_1R) {
		if (rtlpriv->phy.rf_type == RF_2T2R)
			rtl_set_bbreg(hw, ROFDM0_TRXPATHENABLE, MASKBYTE0, 0x13);
		else /* Is this branch reachable? */
			rtl_set_bbreg(hw, ROFDM0_TRXPATHENABLE, MASKBYTE0, 0x23);
	} else { /* CCA_2R or CCA_MAX */
		rtl_set_bbreg(hw, ROFDM0_TRXPATHENABLE, MASKBYTE0, 0x33);
	}
}

static void rtl92du_dm_pwdb_monitor(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	const u32 max_macid = 32;
	u32 temp;

	/* AP & ADHOC & MESH will return tmp */
	if (rtlpriv->mac80211.opmode != NL80211_IFTYPE_STATION)
		return;

	/* Indicate Rx signal strength to FW. */
	if (rtlpriv->dm.useramask) {
		temp = rtlpriv->dm.undec_sm_pwdb << 16;
		temp |= max_macid << 8;

		rtl92d_fill_h2c_cmd(hw, H2C_RSSI_REPORT, 3, (u8 *)(&temp));
	} else {
		rtl_write_byte(rtlpriv, 0x4fe, (u8)rtlpriv->dm.undec_sm_pwdb);
	}
}

void rtl92du_dm_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->dm.dm_type = DM_TYPE_BYDRIVER;
	rtl_dm_diginit(hw, 0x20);
	rtlpriv->dm_digtable.rx_gain_max = DM_DIG_FA_UPPER;
	rtlpriv->dm_digtable.rx_gain_min = DM_DIG_FA_LOWER;
	rtl92d_dm_init_edca_turbo(hw);
	rtl92du_dm_init_1r_cca(hw);
	rtl92d_dm_init_rate_adaptive_mask(hw);
	rtl92d_dm_initialize_txpower_tracking(hw);
}

void rtl92du_dm_watchdog(struct ieee80211_hw *hw)
{
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	bool fw_current_inpsmode = false;
	bool fwps_awake = true;

	/* 1. RF is OFF. (No need to do DM.)
	 * 2. Fw is under power saving mode for FwLPS.
	 *    (Prevent from SW/FW I/O racing.)
	 * 3. IPS workitem is scheduled. (Prevent from IPS sequence
	 *    to be swapped with DM.
	 * 4. RFChangeInProgress is TRUE.
	 *    (Prevent from broken by IPS/HW/SW Rf off.)
	 */

	if (ppsc->rfpwr_state != ERFON || fw_current_inpsmode ||
	    !fwps_awake || ppsc->rfchange_inprogress)
		return;

	rtl92du_dm_pwdb_monitor(hw);
	rtl92d_dm_false_alarm_counter_statistics(hw);
	rtl92d_dm_find_minimum_rssi(hw);
	rtl92d_dm_dig(hw);
	rtl92d_dm_check_txpower_tracking_thermal_meter(hw);
	rtl92d_dm_check_edca_turbo(hw);
	rtl92du_dm_1r_cca(hw);
}
