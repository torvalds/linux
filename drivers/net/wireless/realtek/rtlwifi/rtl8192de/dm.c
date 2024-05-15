// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#include "../wifi.h"
#include "../base.h"
#include "../core.h"
#include "../rtl8192d/reg.h"
#include "../rtl8192d/def.h"
#include "../rtl8192d/dm_common.h"
#include "../rtl8192d/phy_common.h"
#include "../rtl8192d/fw_common.h"
#include "phy.h"
#include "dm.h"

#define UNDEC_SM_PWDB	entry_min_undec_sm_pwdb

static void rtl92d_dm_init_dynamic_txpower(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->dm.dynamic_txpower_enable = true;
	rtlpriv->dm.last_dtp_lvl = TXHIGHPWRLEVEL_NORMAL;
	rtlpriv->dm.dynamic_txhighpower_lvl = TXHIGHPWRLEVEL_NORMAL;
}

static void rtl92d_dm_dynamic_txpower(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	long undec_sm_pwdb;

	if ((!rtlpriv->dm.dynamic_txpower_enable)
	    || rtlpriv->dm.dm_flag & HAL_DM_HIPWR_DISABLE) {
		rtlpriv->dm.dynamic_txhighpower_lvl = TXHIGHPWRLEVEL_NORMAL;
		return;
	}
	if ((mac->link_state < MAC80211_LINKED) &&
	    (rtlpriv->dm.UNDEC_SM_PWDB == 0)) {
		rtl_dbg(rtlpriv, COMP_POWER, DBG_TRACE,
			"Not connected to any\n");
		rtlpriv->dm.dynamic_txhighpower_lvl = TXHIGHPWRLEVEL_NORMAL;
		rtlpriv->dm.last_dtp_lvl = TXHIGHPWRLEVEL_NORMAL;
		return;
	}
	if (mac->link_state >= MAC80211_LINKED) {
		if (mac->opmode == NL80211_IFTYPE_ADHOC) {
			undec_sm_pwdb =
			    rtlpriv->dm.UNDEC_SM_PWDB;
			rtl_dbg(rtlpriv, COMP_POWER, DBG_LOUD,
				"IBSS Client PWDB = 0x%lx\n",
				undec_sm_pwdb);
		} else {
			undec_sm_pwdb =
			    rtlpriv->dm.undec_sm_pwdb;
			rtl_dbg(rtlpriv, COMP_POWER, DBG_LOUD,
				"STA Default Port PWDB = 0x%lx\n",
				undec_sm_pwdb);
		}
	} else {
		undec_sm_pwdb =
		    rtlpriv->dm.UNDEC_SM_PWDB;

		rtl_dbg(rtlpriv, COMP_POWER, DBG_LOUD,
			"AP Ext Port PWDB = 0x%lx\n",
			undec_sm_pwdb);
	}
	if (rtlhal->current_bandtype == BAND_ON_5G) {
		if (undec_sm_pwdb >= 0x33) {
			rtlpriv->dm.dynamic_txhighpower_lvl =
						 TXHIGHPWRLEVEL_LEVEL2;
			rtl_dbg(rtlpriv, COMP_HIPWR, DBG_LOUD,
				"5G:TxHighPwrLevel_Level2 (TxPwr=0x0)\n");
		} else if ((undec_sm_pwdb < 0x33)
			   && (undec_sm_pwdb >= 0x2b)) {
			rtlpriv->dm.dynamic_txhighpower_lvl =
						 TXHIGHPWRLEVEL_LEVEL1;
			rtl_dbg(rtlpriv, COMP_HIPWR, DBG_LOUD,
				"5G:TxHighPwrLevel_Level1 (TxPwr=0x10)\n");
		} else if (undec_sm_pwdb < 0x2b) {
			rtlpriv->dm.dynamic_txhighpower_lvl =
						 TXHIGHPWRLEVEL_NORMAL;
			rtl_dbg(rtlpriv, COMP_HIPWR, DBG_LOUD,
				"5G:TxHighPwrLevel_Normal\n");
		}
	} else {
		if (undec_sm_pwdb >=
		    TX_POWER_NEAR_FIELD_THRESH_LVL2) {
			rtlpriv->dm.dynamic_txhighpower_lvl =
						 TXHIGHPWRLEVEL_LEVEL2;
			rtl_dbg(rtlpriv, COMP_POWER, DBG_LOUD,
				"TXHIGHPWRLEVEL_LEVEL1 (TxPwr=0x0)\n");
		} else
		    if ((undec_sm_pwdb <
			 (TX_POWER_NEAR_FIELD_THRESH_LVL2 - 3))
			&& (undec_sm_pwdb >=
			    TX_POWER_NEAR_FIELD_THRESH_LVL1)) {

			rtlpriv->dm.dynamic_txhighpower_lvl =
						 TXHIGHPWRLEVEL_LEVEL1;
			rtl_dbg(rtlpriv, COMP_POWER, DBG_LOUD,
				"TXHIGHPWRLEVEL_LEVEL1 (TxPwr=0x10)\n");
		} else if (undec_sm_pwdb <
			   (TX_POWER_NEAR_FIELD_THRESH_LVL1 - 5)) {
			rtlpriv->dm.dynamic_txhighpower_lvl =
						 TXHIGHPWRLEVEL_NORMAL;
			rtl_dbg(rtlpriv, COMP_POWER, DBG_LOUD,
				"TXHIGHPWRLEVEL_NORMAL\n");
		}
	}
	if ((rtlpriv->dm.dynamic_txhighpower_lvl != rtlpriv->dm.last_dtp_lvl)) {
		rtl_dbg(rtlpriv, COMP_POWER, DBG_LOUD,
			"PHY_SetTxPowerLevel8192S() Channel = %d\n",
			rtlphy->current_channel);
		rtl92d_phy_set_txpower_level(hw, rtlphy->current_channel);
	}
	rtlpriv->dm.last_dtp_lvl = rtlpriv->dm.dynamic_txhighpower_lvl;
}

static void rtl92d_dm_pwdb_monitor(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	/* AP & ADHOC & MESH will return tmp */
	if (rtlpriv->mac80211.opmode != NL80211_IFTYPE_STATION)
		return;
	/* Indicate Rx signal strength to FW. */
	if (rtlpriv->dm.useramask) {
		u32 temp = rtlpriv->dm.undec_sm_pwdb;

		temp <<= 16;
		temp |= 0x100;
		/* fw v12 cmdid 5:use max macid ,for nic ,
		 * default macid is 0 ,max macid is 1 */
		rtl92d_fill_h2c_cmd(hw, H2C_RSSI_REPORT, 3, (u8 *) (&temp));
	} else {
		rtl_write_byte(rtlpriv, 0x4fe,
			       (u8) rtlpriv->dm.undec_sm_pwdb);
	}
}

void rtl92de_dm_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->dm.dm_type = DM_TYPE_BYDRIVER;
	rtl_dm_diginit(hw, 0x20);
	rtlpriv->dm_digtable.rx_gain_max = DM_DIG_FA_UPPER;
	rtlpriv->dm_digtable.rx_gain_min = DM_DIG_FA_LOWER;
	rtl92d_dm_init_dynamic_txpower(hw);
	rtl92d_dm_init_edca_turbo(hw);
	rtl92d_dm_init_rate_adaptive_mask(hw);
	rtl92d_dm_initialize_txpower_tracking(hw);
}

void rtl92de_dm_watchdog(struct ieee80211_hw *hw)
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
	 *    (Prevent from broken by IPS/HW/SW Rf off.) */

	if ((ppsc->rfpwr_state == ERFON) && ((!fw_current_inpsmode) &&
	    fwps_awake) && (!ppsc->rfchange_inprogress)) {
		rtl92d_dm_pwdb_monitor(hw);
		rtl92d_dm_false_alarm_counter_statistics(hw);
		rtl92d_dm_find_minimum_rssi(hw);
		rtl92d_dm_dig(hw);
		/* rtl92d_dm_dynamic_bb_powersaving(hw); */
		rtl92d_dm_dynamic_txpower(hw);
		/* rtl92d_dm_check_txpower_tracking_thermal_meter(hw); */
		/* rtl92d_dm_refresh_rate_adaptive_mask(hw); */
		/* rtl92d_dm_interrupt_migration(hw); */
		rtl92d_dm_check_edca_turbo(hw);
	}
}
