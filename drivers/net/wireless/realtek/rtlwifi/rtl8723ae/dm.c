// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#include "../wifi.h"
#include "../base.h"
#include "../pci.h"
#include "../core.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "dm.h"
#include "../rtl8723com/dm_common.h"
#include "fw.h"
#include "hal_btc.h"

static u8 rtl8723e_dm_initial_gain_min_pwdb(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_digtable = &rtlpriv->dm_digtable;
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	long rssi_val_min = 0;

	if (mac->link_state == MAC80211_LINKED &&
	    mac->opmode == NL80211_IFTYPE_STATION &&
	    rtlpriv->link_info.bcn_rx_inperiod == 0)
		return 0;

	if ((dm_digtable->curmultista_cstate == DIG_MULTISTA_CONNECT) &&
	    (dm_digtable->cursta_cstate == DIG_STA_CONNECT)) {
		if (rtlpriv->dm.entry_min_undec_sm_pwdb != 0)
			rssi_val_min =
			    (rtlpriv->dm.entry_min_undec_sm_pwdb >
			     rtlpriv->dm.undec_sm_pwdb) ?
			    rtlpriv->dm.undec_sm_pwdb :
			    rtlpriv->dm.entry_min_undec_sm_pwdb;
		else
			rssi_val_min = rtlpriv->dm.undec_sm_pwdb;
	} else if (dm_digtable->cursta_cstate == DIG_STA_CONNECT ||
		   dm_digtable->cursta_cstate == DIG_STA_BEFORE_CONNECT) {
		rssi_val_min = rtlpriv->dm.undec_sm_pwdb;
	} else if (dm_digtable->curmultista_cstate ==
		DIG_MULTISTA_CONNECT) {
		rssi_val_min = rtlpriv->dm.entry_min_undec_sm_pwdb;
	}

	return (u8) rssi_val_min;
}

static void rtl8723e_dm_false_alarm_counter_statistics(struct ieee80211_hw *hw)
{
	u32 ret_value;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct false_alarm_statistics *falsealm_cnt = &(rtlpriv->falsealm_cnt);

	ret_value = rtl_get_bbreg(hw, ROFDM_PHYCOUNTER1, MASKDWORD);
	falsealm_cnt->cnt_parity_fail = ((ret_value & 0xffff0000) >> 16);

	ret_value = rtl_get_bbreg(hw, ROFDM_PHYCOUNTER2, MASKDWORD);
	falsealm_cnt->cnt_rate_illegal = (ret_value & 0xffff);
	falsealm_cnt->cnt_crc8_fail = ((ret_value & 0xffff0000) >> 16);

	ret_value = rtl_get_bbreg(hw, ROFDM_PHYCOUNTER3, MASKDWORD);
	falsealm_cnt->cnt_mcs_fail = (ret_value & 0xffff);
	falsealm_cnt->cnt_ofdm_fail = falsealm_cnt->cnt_parity_fail +
	    falsealm_cnt->cnt_rate_illegal +
	    falsealm_cnt->cnt_crc8_fail + falsealm_cnt->cnt_mcs_fail;

	rtl_set_bbreg(hw, RCCK0_FALSEALARMREPORT, BIT(14), 1);
	ret_value = rtl_get_bbreg(hw, RCCK0_FACOUNTERLOWER, MASKBYTE0);
	falsealm_cnt->cnt_cck_fail = ret_value;

	ret_value = rtl_get_bbreg(hw, RCCK0_FACOUNTERUPPER, MASKBYTE3);
	falsealm_cnt->cnt_cck_fail += (ret_value & 0xff) << 8;
	falsealm_cnt->cnt_all = (falsealm_cnt->cnt_parity_fail +
				 falsealm_cnt->cnt_rate_illegal +
				 falsealm_cnt->cnt_crc8_fail +
				 falsealm_cnt->cnt_mcs_fail +
				 falsealm_cnt->cnt_cck_fail);

	rtl_set_bbreg(hw, ROFDM1_LSTF, 0x08000000, 1);
	rtl_set_bbreg(hw, ROFDM1_LSTF, 0x08000000, 0);
	rtl_set_bbreg(hw, RCCK0_FALSEALARMREPORT, 0x0000c000, 0);
	rtl_set_bbreg(hw, RCCK0_FALSEALARMREPORT, 0x0000c000, 2);

	RT_TRACE(rtlpriv, COMP_DIG, DBG_TRACE,
		 "cnt_parity_fail = %d, cnt_rate_illegal = %d, cnt_crc8_fail = %d, cnt_mcs_fail = %d\n",
		 falsealm_cnt->cnt_parity_fail,
		 falsealm_cnt->cnt_rate_illegal,
		 falsealm_cnt->cnt_crc8_fail, falsealm_cnt->cnt_mcs_fail);

	RT_TRACE(rtlpriv, COMP_DIG, DBG_TRACE,
		 "cnt_ofdm_fail = %x, cnt_cck_fail = %x, cnt_all = %x\n",
		 falsealm_cnt->cnt_ofdm_fail,
		 falsealm_cnt->cnt_cck_fail, falsealm_cnt->cnt_all);
}

static void rtl92c_dm_ctrl_initgain_by_fa(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_digtable = &rtlpriv->dm_digtable;
	u8 value_igi = dm_digtable->cur_igvalue;

	if (rtlpriv->falsealm_cnt.cnt_all < DM_DIG_FA_TH0)
		value_igi--;
	else if (rtlpriv->falsealm_cnt.cnt_all < DM_DIG_FA_TH1)
		value_igi += 0;
	else if (rtlpriv->falsealm_cnt.cnt_all < DM_DIG_FA_TH2)
		value_igi++;
	else if (rtlpriv->falsealm_cnt.cnt_all >= DM_DIG_FA_TH2)
		value_igi += 2;
	if (value_igi > DM_DIG_FA_UPPER)
		value_igi = DM_DIG_FA_UPPER;
	else if (value_igi < DM_DIG_FA_LOWER)
		value_igi = DM_DIG_FA_LOWER;
	if (rtlpriv->falsealm_cnt.cnt_all > 10000)
		value_igi = 0x32;

	dm_digtable->cur_igvalue = value_igi;
	rtl8723e_dm_write_dig(hw);
}

static void rtl92c_dm_ctrl_initgain_by_rssi(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_digtable = &rtlpriv->dm_digtable;

	if (rtlpriv->falsealm_cnt.cnt_all > dm_digtable->fa_highthresh) {
		if ((dm_digtable->back_val - 2) <
		    dm_digtable->back_range_min)
			dm_digtable->back_val =
			    dm_digtable->back_range_min;
		else
			dm_digtable->back_val -= 2;
	} else if (rtlpriv->falsealm_cnt.cnt_all < dm_digtable->fa_lowthresh) {
		if ((dm_digtable->back_val + 2) >
		    dm_digtable->back_range_max)
			dm_digtable->back_val =
			    dm_digtable->back_range_max;
		else
			dm_digtable->back_val += 2;
	}

	if ((dm_digtable->rssi_val_min + 10 - dm_digtable->back_val) >
	    dm_digtable->rx_gain_max)
		dm_digtable->cur_igvalue = dm_digtable->rx_gain_max;
	else if ((dm_digtable->rssi_val_min + 10 -
		  dm_digtable->back_val) < dm_digtable->rx_gain_min)
		dm_digtable->cur_igvalue = dm_digtable->rx_gain_min;
	else
		dm_digtable->cur_igvalue = dm_digtable->rssi_val_min + 10 -
		    dm_digtable->back_val;

	RT_TRACE(rtlpriv, COMP_DIG, DBG_TRACE,
		 "rssi_val_min = %x back_val %x\n",
		  dm_digtable->rssi_val_min, dm_digtable->back_val);

	rtl8723e_dm_write_dig(hw);
}

static void rtl8723e_dm_initial_gain_multi_sta(struct ieee80211_hw *hw)
{
	static u8 binitialized;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct dig_t *dm_digtable = &rtlpriv->dm_digtable;
	long rssi_strength = rtlpriv->dm.entry_min_undec_sm_pwdb;
	bool multi_sta = false;

	if (mac->opmode == NL80211_IFTYPE_ADHOC)
		multi_sta = true;

	if (!multi_sta || (dm_digtable->cursta_cstate != DIG_STA_DISCONNECT)) {
		binitialized = false;
		dm_digtable->dig_ext_port_stage = DIG_EXT_PORT_STAGE_MAX;
		return;
	} else if (!binitialized) {
		binitialized = true;
		dm_digtable->dig_ext_port_stage = DIG_EXT_PORT_STAGE_0;
		dm_digtable->cur_igvalue = 0x20;
		rtl8723e_dm_write_dig(hw);
	}

	if (dm_digtable->curmultista_cstate == DIG_MULTISTA_CONNECT) {
		if ((rssi_strength < dm_digtable->rssi_lowthresh) &&
		    (dm_digtable->dig_ext_port_stage != DIG_EXT_PORT_STAGE_1)) {

			if (dm_digtable->dig_ext_port_stage ==
			    DIG_EXT_PORT_STAGE_2) {
				dm_digtable->cur_igvalue = 0x20;
				rtl8723e_dm_write_dig(hw);
			}

			dm_digtable->dig_ext_port_stage = DIG_EXT_PORT_STAGE_1;
		} else if (rssi_strength > dm_digtable->rssi_highthresh) {
			dm_digtable->dig_ext_port_stage = DIG_EXT_PORT_STAGE_2;
			rtl92c_dm_ctrl_initgain_by_fa(hw);
		}
	} else if (dm_digtable->dig_ext_port_stage != DIG_EXT_PORT_STAGE_0) {
		dm_digtable->dig_ext_port_stage = DIG_EXT_PORT_STAGE_0;
		dm_digtable->cur_igvalue = 0x20;
		rtl8723e_dm_write_dig(hw);
	}

	RT_TRACE(rtlpriv, COMP_DIG, DBG_TRACE,
		 "curmultista_cstate = %x dig_ext_port_stage %x\n",
		 dm_digtable->curmultista_cstate,
		 dm_digtable->dig_ext_port_stage);
}

static void rtl8723e_dm_initial_gain_sta(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_digtable = &rtlpriv->dm_digtable;

	RT_TRACE(rtlpriv, COMP_DIG, DBG_TRACE,
		 "presta_cstate = %x, cursta_cstate = %x\n",
		  dm_digtable->presta_cstate,
		  dm_digtable->cursta_cstate);

	if (dm_digtable->presta_cstate == dm_digtable->cursta_cstate ||
	    dm_digtable->cursta_cstate == DIG_STA_BEFORE_CONNECT ||
	    dm_digtable->cursta_cstate == DIG_STA_CONNECT) {
		if (dm_digtable->cursta_cstate != DIG_STA_DISCONNECT) {
			dm_digtable->rssi_val_min =
			    rtl8723e_dm_initial_gain_min_pwdb(hw);
			rtl92c_dm_ctrl_initgain_by_rssi(hw);
		}
	} else {
		dm_digtable->rssi_val_min = 0;
		dm_digtable->dig_ext_port_stage = DIG_EXT_PORT_STAGE_MAX;
		dm_digtable->back_val = DM_DIG_BACKOFF_DEFAULT;
		dm_digtable->cur_igvalue = 0x20;
		dm_digtable->pre_igvalue = 0;
		rtl8723e_dm_write_dig(hw);
	}
}

static void rtl8723e_dm_cck_packet_detection_thresh(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_digtable = &rtlpriv->dm_digtable;

	if (dm_digtable->cursta_cstate == DIG_STA_CONNECT) {
		dm_digtable->rssi_val_min = rtl8723e_dm_initial_gain_min_pwdb(hw);

		if (dm_digtable->pre_cck_pd_state == CCK_PD_STAGE_LOWRSSI) {
			if (dm_digtable->rssi_val_min <= 25)
				dm_digtable->cur_cck_pd_state =
				    CCK_PD_STAGE_LOWRSSI;
			else
				dm_digtable->cur_cck_pd_state =
				    CCK_PD_STAGE_HIGHRSSI;
		} else {
			if (dm_digtable->rssi_val_min <= 20)
				dm_digtable->cur_cck_pd_state =
				    CCK_PD_STAGE_LOWRSSI;
			else
				dm_digtable->cur_cck_pd_state =
				    CCK_PD_STAGE_HIGHRSSI;
		}
	} else {
		dm_digtable->cur_cck_pd_state = CCK_PD_STAGE_MAX;
	}

	if (dm_digtable->pre_cck_pd_state != dm_digtable->cur_cck_pd_state) {
		if (dm_digtable->cur_cck_pd_state == CCK_PD_STAGE_LOWRSSI) {
			if (rtlpriv->falsealm_cnt.cnt_cck_fail > 800)
				dm_digtable->cur_cck_fa_state =
				    CCK_FA_STAGE_HIGH;
			else
				dm_digtable->cur_cck_fa_state =
				    CCK_FA_STAGE_LOW;
			if (dm_digtable->pre_cck_fa_state !=
			    dm_digtable->cur_cck_fa_state) {
				if (dm_digtable->cur_cck_fa_state ==
				    CCK_FA_STAGE_LOW)
					rtl_set_bbreg(hw, RCCK0_CCA, MASKBYTE2,
						      0x83);
				else
					rtl_set_bbreg(hw, RCCK0_CCA, MASKBYTE2,
						      0xcd);

				dm_digtable->pre_cck_fa_state =
				    dm_digtable->cur_cck_fa_state;
			}

			rtl_set_bbreg(hw, RCCK0_SYSTEM, MASKBYTE1, 0x40);

		} else {
			rtl_set_bbreg(hw, RCCK0_CCA, MASKBYTE2, 0xcd);
			rtl_set_bbreg(hw, RCCK0_SYSTEM, MASKBYTE1, 0x47);
			dm_digtable->pre_cck_fa_state = 0;
			dm_digtable->cur_cck_fa_state = 0;

		}
		dm_digtable->pre_cck_pd_state = dm_digtable->cur_cck_pd_state;
	}

	RT_TRACE(rtlpriv, COMP_DIG, DBG_TRACE,
		 "CCKPDStage=%x\n", dm_digtable->cur_cck_pd_state);

}

static void rtl8723e_dm_ctrl_initgain_by_twoport(struct ieee80211_hw *hw)
{
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_digtable = &rtlpriv->dm_digtable;

	if (mac->act_scanning)
		return;

	if (mac->link_state >= MAC80211_LINKED)
		dm_digtable->cursta_cstate = DIG_STA_CONNECT;
	else
		dm_digtable->cursta_cstate = DIG_STA_DISCONNECT;

	rtl8723e_dm_initial_gain_sta(hw);
	rtl8723e_dm_initial_gain_multi_sta(hw);
	rtl8723e_dm_cck_packet_detection_thresh(hw);

	dm_digtable->presta_cstate = dm_digtable->cursta_cstate;

}

static void rtl8723e_dm_dig(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_digtable = &rtlpriv->dm_digtable;

	if (!rtlpriv->dm.dm_initialgain_enable)
		return;
	if (!dm_digtable->dig_enable_flag)
		return;

	rtl8723e_dm_ctrl_initgain_by_twoport(hw);

}

static void rtl8723e_dm_dynamic_txpower(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	long undec_sm_pwdb;

	if (!rtlpriv->dm.dynamic_txpower_enable)
		return;

	if (rtlpriv->dm.dm_flag & HAL_DM_HIPWR_DISABLE) {
		rtlpriv->dm.dynamic_txhighpower_lvl = TXHIGHPWRLEVEL_NORMAL;
		return;
	}

	if ((mac->link_state < MAC80211_LINKED) &&
	    (rtlpriv->dm.entry_min_undec_sm_pwdb == 0)) {
		RT_TRACE(rtlpriv, COMP_POWER, DBG_TRACE,
			 "Not connected to any\n");

		rtlpriv->dm.dynamic_txhighpower_lvl = TXHIGHPWRLEVEL_NORMAL;

		rtlpriv->dm.last_dtp_lvl = TXHIGHPWRLEVEL_NORMAL;
		return;
	}

	if (mac->link_state >= MAC80211_LINKED) {
		if (mac->opmode == NL80211_IFTYPE_ADHOC) {
			undec_sm_pwdb =
			    rtlpriv->dm.entry_min_undec_sm_pwdb;
			RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
				 "AP Client PWDB = 0x%lx\n",
				  undec_sm_pwdb);
		} else {
			undec_sm_pwdb =
			    rtlpriv->dm.undec_sm_pwdb;
			RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
				 "STA Default Port PWDB = 0x%lx\n",
				  undec_sm_pwdb);
		}
	} else {
		undec_sm_pwdb =
		    rtlpriv->dm.entry_min_undec_sm_pwdb;

		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
			 "AP Ext Port PWDB = 0x%lx\n",
			  undec_sm_pwdb);
	}

	if (undec_sm_pwdb >= TX_POWER_NEAR_FIELD_THRESH_LVL2) {
		rtlpriv->dm.dynamic_txhighpower_lvl = TXHIGHPWRLEVEL_LEVEL1;
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
			 "TXHIGHPWRLEVEL_LEVEL1 (TxPwr=0x0)\n");
	} else if ((undec_sm_pwdb <
		    (TX_POWER_NEAR_FIELD_THRESH_LVL2 - 3)) &&
		   (undec_sm_pwdb >=
		    TX_POWER_NEAR_FIELD_THRESH_LVL1)) {
		rtlpriv->dm.dynamic_txhighpower_lvl = TXHIGHPWRLEVEL_LEVEL1;
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
			 "TXHIGHPWRLEVEL_LEVEL1 (TxPwr=0x10)\n");
	} else if (undec_sm_pwdb <
		   (TX_POWER_NEAR_FIELD_THRESH_LVL1 - 5)) {
		rtlpriv->dm.dynamic_txhighpower_lvl = TXHIGHPWRLEVEL_NORMAL;
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
			 "TXHIGHPWRLEVEL_NORMAL\n");
	}

	if (rtlpriv->dm.dynamic_txhighpower_lvl != rtlpriv->dm.last_dtp_lvl) {
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
			 "PHY_SetTxPowerLevel8192S() Channel = %d\n",
			  rtlphy->current_channel);
		rtl8723e_phy_set_txpower_level(hw, rtlphy->current_channel);
	}

	rtlpriv->dm.last_dtp_lvl = rtlpriv->dm.dynamic_txhighpower_lvl;
}

void rtl8723e_dm_write_dig(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_digtable = &rtlpriv->dm_digtable;

	RT_TRACE(rtlpriv, COMP_DIG, DBG_LOUD,
		 "cur_igvalue = 0x%x, pre_igvalue = 0x%x, back_val = %d\n",
		  dm_digtable->cur_igvalue, dm_digtable->pre_igvalue,
		  dm_digtable->back_val);

	if (dm_digtable->pre_igvalue != dm_digtable->cur_igvalue) {
		rtl_set_bbreg(hw, ROFDM0_XAAGCCORE1, 0x7f,
			      dm_digtable->cur_igvalue);
		rtl_set_bbreg(hw, ROFDM0_XBAGCCORE1, 0x7f,
			      dm_digtable->cur_igvalue);

		dm_digtable->pre_igvalue = dm_digtable->cur_igvalue;
	}
}

static void rtl8723e_dm_pwdb_monitor(struct ieee80211_hw *hw)
{
}

static void rtl8723e_dm_check_edca_turbo(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));

	static u64 last_txok_cnt;
	static u64 last_rxok_cnt;
	static u32 last_bt_edca_ul;
	static u32 last_bt_edca_dl;
	u64 cur_txok_cnt = 0;
	u64 cur_rxok_cnt = 0;
	u32 edca_be_ul = 0x5ea42b;
	u32 edca_be_dl = 0x5ea42b;
	bool bt_change_edca = false;

	if ((last_bt_edca_ul != rtlpriv->btcoexist.bt_edca_ul) ||
	    (last_bt_edca_dl != rtlpriv->btcoexist.bt_edca_dl)) {
		rtlpriv->dm.current_turbo_edca = false;
		last_bt_edca_ul = rtlpriv->btcoexist.bt_edca_ul;
		last_bt_edca_dl = rtlpriv->btcoexist.bt_edca_dl;
	}

	if (rtlpriv->btcoexist.bt_edca_ul != 0) {
		edca_be_ul = rtlpriv->btcoexist.bt_edca_ul;
		bt_change_edca = true;
	}

	if (rtlpriv->btcoexist.bt_edca_dl != 0) {
		edca_be_ul = rtlpriv->btcoexist.bt_edca_dl;
		bt_change_edca = true;
	}

	if (mac->link_state != MAC80211_LINKED) {
		rtlpriv->dm.current_turbo_edca = false;
		return;
	}
	if ((bt_change_edca) || ((!rtlpriv->dm.is_any_nonbepkts) &&
	     (!rtlpriv->dm.disable_framebursting))) {

		cur_txok_cnt = rtlpriv->stats.txbytesunicast - last_txok_cnt;
		cur_rxok_cnt = rtlpriv->stats.rxbytesunicast - last_rxok_cnt;

		if (cur_rxok_cnt > 4 * cur_txok_cnt) {
			if (!rtlpriv->dm.is_cur_rdlstate ||
			    !rtlpriv->dm.current_turbo_edca) {
				rtl_write_dword(rtlpriv,
						REG_EDCA_BE_PARAM,
						edca_be_dl);
				rtlpriv->dm.is_cur_rdlstate = true;
			}
		} else {
			if (rtlpriv->dm.is_cur_rdlstate ||
			    !rtlpriv->dm.current_turbo_edca) {
				rtl_write_dword(rtlpriv,
						REG_EDCA_BE_PARAM,
						edca_be_ul);
				rtlpriv->dm.is_cur_rdlstate = false;
			}
		}
		rtlpriv->dm.current_turbo_edca = true;
	} else {
		if (rtlpriv->dm.current_turbo_edca) {
			u8 tmp = AC0_BE;
			rtlpriv->cfg->ops->set_hw_reg(hw,
						      HW_VAR_AC_PARAM,
						      (u8 *)(&tmp));
			rtlpriv->dm.current_turbo_edca = false;
		}
	}

	rtlpriv->dm.is_any_nonbepkts = false;
	last_txok_cnt = rtlpriv->stats.txbytesunicast;
	last_rxok_cnt = rtlpriv->stats.rxbytesunicast;
}

static void rtl8723e_dm_initialize_txpower_tracking_thermalmeter(
				struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->dm.txpower_tracking = true;
	rtlpriv->dm.txpower_trackinginit = false;

	RT_TRACE(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
		 "pMgntInfo->txpower_tracking = %d\n",
		  rtlpriv->dm.txpower_tracking);
}

static void rtl8723e_dm_initialize_txpower_tracking(struct ieee80211_hw *hw)
{
	rtl8723e_dm_initialize_txpower_tracking_thermalmeter(hw);
}

void rtl8723e_dm_check_txpower_tracking(struct ieee80211_hw *hw)
{
	return;
}

void rtl8723e_dm_init_rate_adaptive_mask(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rate_adaptive *p_ra = &rtlpriv->ra;

	p_ra->ratr_state = DM_RATR_STA_INIT;
	p_ra->pre_ratr_state = DM_RATR_STA_INIT;

	if (rtlpriv->dm.dm_type == DM_TYPE_BYDRIVER)
		rtlpriv->dm.useramask = true;
	else
		rtlpriv->dm.useramask = false;

}

static void rtl8723e_dm_refresh_rate_adaptive_mask(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rate_adaptive *p_ra = &rtlpriv->ra;
	u32 low_rssithresh_for_ra, high_rssithresh_for_ra;
	struct ieee80211_sta *sta = NULL;

	if (is_hal_stop(rtlhal)) {
		RT_TRACE(rtlpriv, COMP_RATE, DBG_LOUD,
			 " driver is going to unload\n");
		return;
	}

	if (!rtlpriv->dm.useramask) {
		RT_TRACE(rtlpriv, COMP_RATE, DBG_LOUD,
			 " driver does not control rate adaptive mask\n");
		return;
	}

	if (mac->link_state == MAC80211_LINKED &&
	    mac->opmode == NL80211_IFTYPE_STATION) {
		switch (p_ra->pre_ratr_state) {
		case DM_RATR_STA_HIGH:
			high_rssithresh_for_ra = 50;
			low_rssithresh_for_ra = 20;
			break;
		case DM_RATR_STA_MIDDLE:
			high_rssithresh_for_ra = 55;
			low_rssithresh_for_ra = 20;
			break;
		case DM_RATR_STA_LOW:
			high_rssithresh_for_ra = 60;
			low_rssithresh_for_ra = 25;
			break;
		default:
			high_rssithresh_for_ra = 50;
			low_rssithresh_for_ra = 20;
			break;
		}

		if (rtlpriv->link_info.bcn_rx_inperiod == 0)
			switch (p_ra->pre_ratr_state) {
			case DM_RATR_STA_HIGH:
			default:
				p_ra->ratr_state = DM_RATR_STA_MIDDLE;
				break;
			case DM_RATR_STA_MIDDLE:
			case DM_RATR_STA_LOW:
				p_ra->ratr_state = DM_RATR_STA_LOW;
				break;
			}
		else if (rtlpriv->dm.undec_sm_pwdb > high_rssithresh_for_ra)
			p_ra->ratr_state = DM_RATR_STA_HIGH;
		else if (rtlpriv->dm.undec_sm_pwdb > low_rssithresh_for_ra)
			p_ra->ratr_state = DM_RATR_STA_MIDDLE;
		else
			p_ra->ratr_state = DM_RATR_STA_LOW;

		if (p_ra->pre_ratr_state != p_ra->ratr_state) {
			RT_TRACE(rtlpriv, COMP_RATE, DBG_LOUD,
				 "RSSI = %ld\n",
				 rtlpriv->dm.undec_sm_pwdb);
			RT_TRACE(rtlpriv, COMP_RATE, DBG_LOUD,
				 "RSSI_LEVEL = %d\n", p_ra->ratr_state);
			RT_TRACE(rtlpriv, COMP_RATE, DBG_LOUD,
				 "PreState = %d, CurState = %d\n",
				 p_ra->pre_ratr_state, p_ra->ratr_state);

			rcu_read_lock();
			sta = rtl_find_sta(hw, mac->bssid);
			if (sta)
				rtlpriv->cfg->ops->update_rate_tbl(hw, sta,
							   p_ra->ratr_state,
								      true);
			rcu_read_unlock();

			p_ra->pre_ratr_state = p_ra->ratr_state;
		}
	}
}

void rtl8723e_dm_rf_saving(struct ieee80211_hw *hw, u8 bforce_in_normal)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct ps_t *dm_pstable = &rtlpriv->dm_pstable;
	static u8 initialize;
	static u32 reg_874, reg_c70, reg_85c, reg_a74;

	if (initialize == 0) {
		reg_874 = (rtl_get_bbreg(hw, RFPGA0_XCD_RFINTERFACESW,
					 MASKDWORD) & 0x1CC000) >> 14;

		reg_c70 = (rtl_get_bbreg(hw, ROFDM0_AGCPARAMETER1,
					 MASKDWORD) & BIT(3)) >> 3;

		reg_85c = (rtl_get_bbreg(hw, RFPGA0_XCD_SWITCHCONTROL,
					 MASKDWORD) & 0xFF000000) >> 24;

		reg_a74 = (rtl_get_bbreg(hw, 0xa74, MASKDWORD) & 0xF000) >> 12;

		initialize = 1;
	}

	if (!bforce_in_normal) {
		if (dm_pstable->rssi_val_min != 0) {
			if (dm_pstable->pre_rfstate == RF_NORMAL) {
				if (dm_pstable->rssi_val_min >= 30)
					dm_pstable->cur_rfstate = RF_SAVE;
				else
					dm_pstable->cur_rfstate = RF_NORMAL;
			} else {
				if (dm_pstable->rssi_val_min <= 25)
					dm_pstable->cur_rfstate = RF_NORMAL;
				else
					dm_pstable->cur_rfstate = RF_SAVE;
			}
		} else {
			dm_pstable->cur_rfstate = RF_MAX;
		}
	} else {
		dm_pstable->cur_rfstate = RF_NORMAL;
	}

	if (dm_pstable->pre_rfstate != dm_pstable->cur_rfstate) {
		if (dm_pstable->cur_rfstate == RF_SAVE) {
			rtl_set_bbreg(hw, RFPGA0_XCD_RFINTERFACESW,
				      BIT(5), 0x1);
			rtl_set_bbreg(hw, RFPGA0_XCD_RFINTERFACESW,
				      0x1C0000, 0x2);
			rtl_set_bbreg(hw, ROFDM0_AGCPARAMETER1, BIT(3), 0);
			rtl_set_bbreg(hw, RFPGA0_XCD_SWITCHCONTROL,
				      0xFF000000, 0x63);
			rtl_set_bbreg(hw, RFPGA0_XCD_RFINTERFACESW,
				      0xC000, 0x2);
			rtl_set_bbreg(hw, 0xa74, 0xF000, 0x3);
			rtl_set_bbreg(hw, 0x818, BIT(28), 0x0);
			rtl_set_bbreg(hw, 0x818, BIT(28), 0x1);
		} else {
			rtl_set_bbreg(hw, RFPGA0_XCD_RFINTERFACESW,
				      0x1CC000, reg_874);
			rtl_set_bbreg(hw, ROFDM0_AGCPARAMETER1, BIT(3),
				      reg_c70);
			rtl_set_bbreg(hw, RFPGA0_XCD_SWITCHCONTROL, 0xFF000000,
				      reg_85c);
			rtl_set_bbreg(hw, 0xa74, 0xF000, reg_a74);
			rtl_set_bbreg(hw, 0x818, BIT(28), 0x0);
			rtl_set_bbreg(hw, RFPGA0_XCD_RFINTERFACESW,
				      BIT(5), 0x0);
		}

		dm_pstable->pre_rfstate = dm_pstable->cur_rfstate;
	}
}

static void rtl8723e_dm_dynamic_bb_powersaving(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct ps_t *dm_pstable = &rtlpriv->dm_pstable;

	if (((mac->link_state == MAC80211_NOLINK)) &&
	    (rtlpriv->dm.entry_min_undec_sm_pwdb == 0)) {
		dm_pstable->rssi_val_min = 0;
		RT_TRACE(rtlpriv, DBG_LOUD, DBG_LOUD,
			 "Not connected to any\n");
	}

	if (mac->link_state == MAC80211_LINKED) {
		if (mac->opmode == NL80211_IFTYPE_ADHOC) {
			dm_pstable->rssi_val_min =
			    rtlpriv->dm.entry_min_undec_sm_pwdb;
			RT_TRACE(rtlpriv, DBG_LOUD, DBG_LOUD,
				 "AP Client PWDB = 0x%lx\n",
				  dm_pstable->rssi_val_min);
		} else {
			dm_pstable->rssi_val_min =
			    rtlpriv->dm.undec_sm_pwdb;
			RT_TRACE(rtlpriv, DBG_LOUD, DBG_LOUD,
				 "STA Default Port PWDB = 0x%lx\n",
				  dm_pstable->rssi_val_min);
		}
	} else {
		dm_pstable->rssi_val_min =
		    rtlpriv->dm.entry_min_undec_sm_pwdb;

		RT_TRACE(rtlpriv, DBG_LOUD, DBG_LOUD,
			 "AP Ext Port PWDB = 0x%lx\n",
			  dm_pstable->rssi_val_min);
	}

	rtl8723e_dm_rf_saving(hw, false);
}

void rtl8723e_dm_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->dm.dm_type = DM_TYPE_BYDRIVER;
	rtl_dm_diginit(hw, 0x20);
	rtl8723_dm_init_dynamic_txpower(hw);
	rtl8723_dm_init_edca_turbo(hw);
	rtl8723e_dm_init_rate_adaptive_mask(hw);
	rtl8723e_dm_initialize_txpower_tracking(hw);
	rtl8723_dm_init_dynamic_bb_powersaving(hw);
}

void rtl8723e_dm_watchdog(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	bool fw_current_inpsmode = false;
	bool fw_ps_awake = true;
	rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
				      (u8 *)(&fw_current_inpsmode));
	rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_FWLPS_RF_ON,
				      (u8 *)(&fw_ps_awake));

	if (ppsc->p2p_ps_info.p2p_ps_mode)
		fw_ps_awake = false;

	spin_lock(&rtlpriv->locks.rf_ps_lock);
	if ((ppsc->rfpwr_state == ERFON) &&
	    ((!fw_current_inpsmode) && fw_ps_awake) &&
	    (!ppsc->rfchange_inprogress)) {
		rtl8723e_dm_pwdb_monitor(hw);
		rtl8723e_dm_dig(hw);
		rtl8723e_dm_false_alarm_counter_statistics(hw);
		rtl8723e_dm_dynamic_bb_powersaving(hw);
		rtl8723e_dm_dynamic_txpower(hw);
		rtl8723e_dm_check_txpower_tracking(hw);
		rtl8723e_dm_refresh_rate_adaptive_mask(hw);
		rtl8723e_dm_bt_coexist(hw);
		rtl8723e_dm_check_edca_turbo(hw);
	}
	spin_unlock(&rtlpriv->locks.rf_ps_lock);
	if (rtlpriv->btcoexist.init_set)
		rtl_write_byte(rtlpriv, 0x76e, 0xc);
}

static void rtl8723e_dm_init_bt_coexist(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->btcoexist.bt_rfreg_origin_1e
		= rtl_get_rfreg(hw, (enum radio_path)0, RF_RCK1, 0xfffff);
	rtlpriv->btcoexist.bt_rfreg_origin_1f
		= rtl_get_rfreg(hw, (enum radio_path)0, RF_RCK2, 0xf0);

	rtlpriv->btcoexist.cstate = 0;
	rtlpriv->btcoexist.previous_state = 0;
	rtlpriv->btcoexist.cstate_h = 0;
	rtlpriv->btcoexist.previous_state_h = 0;
	rtlpriv->btcoexist.lps_counter = 0;

	/*  Enable counter statistics */
	rtl_write_byte(rtlpriv, 0x76e, 0x4);
	rtl_write_byte(rtlpriv, 0x778, 0x3);
	rtl_write_byte(rtlpriv, 0x40, 0x20);

	rtlpriv->btcoexist.init_set = true;
}

void rtl8723e_dm_bt_coexist(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmp_byte = 0;
	if (!rtlpriv->btcoexist.bt_coexistence) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[DM]{BT], BT not exist!!\n");
		return;
	}

	if (!rtlpriv->btcoexist.init_set) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[DM][BT], rtl8723e_dm_bt_coexist()\n");
		rtl8723e_dm_init_bt_coexist(hw);
	}

	tmp_byte = rtl_read_byte(rtlpriv, 0x40);
	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[DM][BT], 0x40 is 0x%x\n", tmp_byte);
	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
		 "[DM][BT], bt_dm_coexist start\n");
	rtl8723e_dm_bt_coexist_8723(hw);
}
