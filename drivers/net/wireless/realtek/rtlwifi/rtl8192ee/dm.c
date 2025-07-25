// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2014  Realtek Corporation.*/

#include "../wifi.h"
#include "../base.h"
#include "../pci.h"
#include "../core.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "dm.h"
#include "fw.h"
#include "trx.h"

static void rtl92ee_dm_false_alarm_counter_statistics(struct ieee80211_hw *hw)
{
	u32 ret_value;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct false_alarm_statistics *falsealm_cnt = &rtlpriv->falsealm_cnt;

	rtl_set_bbreg(hw, DM_REG_OFDM_FA_HOLDC_11N, BIT(31), 1);
	rtl_set_bbreg(hw, DM_REG_OFDM_FA_RSTD_11N, BIT(31), 1);

	ret_value = rtl_get_bbreg(hw, DM_REG_OFDM_FA_TYPE1_11N, MASKDWORD);
	falsealm_cnt->cnt_fast_fsync_fail = (ret_value & 0xffff);
	falsealm_cnt->cnt_sb_search_fail = ((ret_value & 0xffff0000) >> 16);

	ret_value = rtl_get_bbreg(hw, DM_REG_OFDM_FA_TYPE2_11N, MASKDWORD);
	falsealm_cnt->cnt_ofdm_cca = (ret_value & 0xffff);
	falsealm_cnt->cnt_parity_fail = ((ret_value & 0xffff0000) >> 16);

	ret_value = rtl_get_bbreg(hw, DM_REG_OFDM_FA_TYPE3_11N, MASKDWORD);
	falsealm_cnt->cnt_rate_illegal = (ret_value & 0xffff);
	falsealm_cnt->cnt_crc8_fail = ((ret_value & 0xffff0000) >> 16);

	ret_value = rtl_get_bbreg(hw, DM_REG_OFDM_FA_TYPE4_11N, MASKDWORD);
	falsealm_cnt->cnt_mcs_fail = (ret_value & 0xffff);

	falsealm_cnt->cnt_ofdm_fail = falsealm_cnt->cnt_parity_fail +
				      falsealm_cnt->cnt_rate_illegal +
				      falsealm_cnt->cnt_crc8_fail +
				      falsealm_cnt->cnt_mcs_fail +
				      falsealm_cnt->cnt_fast_fsync_fail +
				      falsealm_cnt->cnt_sb_search_fail;

	ret_value = rtl_get_bbreg(hw, DM_REG_SC_CNT_11N, MASKDWORD);
	falsealm_cnt->cnt_bw_lsc = (ret_value & 0xffff);
	falsealm_cnt->cnt_bw_usc = ((ret_value & 0xffff0000) >> 16);

	rtl_set_bbreg(hw, DM_REG_CCK_FA_RST_11N, BIT(12), 1);
	rtl_set_bbreg(hw, DM_REG_CCK_FA_RST_11N, BIT(14), 1);

	ret_value = rtl_get_bbreg(hw, DM_REG_CCK_FA_LSB_11N, MASKBYTE0);
	falsealm_cnt->cnt_cck_fail = ret_value;

	ret_value = rtl_get_bbreg(hw, DM_REG_CCK_FA_MSB_11N, MASKBYTE3);
	falsealm_cnt->cnt_cck_fail += (ret_value & 0xff) << 8;

	ret_value = rtl_get_bbreg(hw, DM_REG_CCK_CCA_CNT_11N, MASKDWORD);
	falsealm_cnt->cnt_cck_cca = ((ret_value & 0xff) << 8) |
				    ((ret_value & 0xFF00) >> 8);

	falsealm_cnt->cnt_all = falsealm_cnt->cnt_fast_fsync_fail +
				falsealm_cnt->cnt_sb_search_fail +
				falsealm_cnt->cnt_parity_fail +
				falsealm_cnt->cnt_rate_illegal +
				falsealm_cnt->cnt_crc8_fail +
				falsealm_cnt->cnt_mcs_fail +
				falsealm_cnt->cnt_cck_fail;

	falsealm_cnt->cnt_cca_all = falsealm_cnt->cnt_ofdm_cca +
				    falsealm_cnt->cnt_cck_cca;

	/*reset false alarm counter registers*/
	rtl_set_bbreg(hw, DM_REG_OFDM_FA_RSTC_11N, BIT(31), 1);
	rtl_set_bbreg(hw, DM_REG_OFDM_FA_RSTC_11N, BIT(31), 0);
	rtl_set_bbreg(hw, DM_REG_OFDM_FA_RSTD_11N, BIT(27), 1);
	rtl_set_bbreg(hw, DM_REG_OFDM_FA_RSTD_11N, BIT(27), 0);
	/*update ofdm counter*/
	rtl_set_bbreg(hw, DM_REG_OFDM_FA_HOLDC_11N, BIT(31), 0);
	rtl_set_bbreg(hw, DM_REG_OFDM_FA_RSTD_11N, BIT(31), 0);
	/*reset CCK CCA counter*/
	rtl_set_bbreg(hw, DM_REG_CCK_FA_RST_11N, BIT(13) | BIT(12), 0);
	rtl_set_bbreg(hw, DM_REG_CCK_FA_RST_11N, BIT(13) | BIT(12), 2);
	/*reset CCK FA counter*/
	rtl_set_bbreg(hw, DM_REG_CCK_FA_RST_11N, BIT(15) | BIT(14), 0);
	rtl_set_bbreg(hw, DM_REG_CCK_FA_RST_11N, BIT(15) | BIT(14), 2);

	rtl_dbg(rtlpriv, COMP_DIG, DBG_TRACE,
		"cnt_parity_fail = %d, cnt_rate_illegal = %d, cnt_crc8_fail = %d, cnt_mcs_fail = %d\n",
		falsealm_cnt->cnt_parity_fail,
		falsealm_cnt->cnt_rate_illegal,
		falsealm_cnt->cnt_crc8_fail, falsealm_cnt->cnt_mcs_fail);

	rtl_dbg(rtlpriv, COMP_DIG, DBG_TRACE,
		"cnt_ofdm_fail = %x, cnt_cck_fail = %x, cnt_all = %x\n",
		falsealm_cnt->cnt_ofdm_fail,
		falsealm_cnt->cnt_cck_fail, falsealm_cnt->cnt_all);
}

static void rtl92ee_dm_cck_packet_detection_thresh(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_dig = &rtlpriv->dm_digtable;
	u8 cur_cck_cca_thresh;

	if (rtlpriv->mac80211.link_state >= MAC80211_LINKED) {
		if (dm_dig->rssi_val_min > 25) {
			cur_cck_cca_thresh = 0xcd;
		} else if ((dm_dig->rssi_val_min <= 25) &&
			   (dm_dig->rssi_val_min > 10)) {
			cur_cck_cca_thresh = 0x83;
		} else {
			if (rtlpriv->falsealm_cnt.cnt_cck_fail > 1000)
				cur_cck_cca_thresh = 0x83;
			else
				cur_cck_cca_thresh = 0x40;
		}
	} else {
		if (rtlpriv->falsealm_cnt.cnt_cck_fail > 1000)
			cur_cck_cca_thresh = 0x83;
		else
			cur_cck_cca_thresh = 0x40;
	}
	rtl92ee_dm_write_cck_cca_thres(hw, cur_cck_cca_thresh);
}

static void rtl92ee_dm_dig(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct dig_t *dm_dig = &rtlpriv->dm_digtable;
	u8 dig_min_0, dig_maxofmin;
	bool bfirstconnect , bfirstdisconnect;
	u8 dm_dig_max, dm_dig_min;
	u8 current_igi = dm_dig->cur_igvalue;
	u8 offset;

	/* AP,BT */
	if (mac->act_scanning)
		return;

	dig_min_0 = dm_dig->dig_min_0;
	bfirstconnect = (mac->link_state >= MAC80211_LINKED) &&
			!dm_dig->media_connect_0;
	bfirstdisconnect = (mac->link_state < MAC80211_LINKED) &&
			   dm_dig->media_connect_0;

	dm_dig_max = 0x5a;
	dm_dig_min = DM_DIG_MIN;
	dig_maxofmin = DM_DIG_MAX_AP;

	if (mac->link_state >= MAC80211_LINKED) {
		if ((dm_dig->rssi_val_min + 10) > dm_dig_max)
			dm_dig->rx_gain_max = dm_dig_max;
		else if ((dm_dig->rssi_val_min + 10) < dm_dig_min)
			dm_dig->rx_gain_max = dm_dig_min;
		else
			dm_dig->rx_gain_max = dm_dig->rssi_val_min + 10;

		if (rtlpriv->dm.one_entry_only) {
			offset = 0;
			if (dm_dig->rssi_val_min - offset < dm_dig_min)
				dig_min_0 = dm_dig_min;
			else if (dm_dig->rssi_val_min - offset >
				 dig_maxofmin)
				dig_min_0 = dig_maxofmin;
			else
				dig_min_0 = dm_dig->rssi_val_min - offset;
		} else {
			dig_min_0 = dm_dig_min;
		}

	} else {
		dm_dig->rx_gain_max = dm_dig_max;
		dig_min_0 = dm_dig_min;
		rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD, "no link\n");
	}

	if (rtlpriv->falsealm_cnt.cnt_all > 10000) {
		if (dm_dig->large_fa_hit != 3)
			dm_dig->large_fa_hit++;
		if (dm_dig->forbidden_igi < current_igi) {
			dm_dig->forbidden_igi = current_igi;
			dm_dig->large_fa_hit = 1;
		}

		if (dm_dig->large_fa_hit >= 3) {
			if (dm_dig->forbidden_igi + 1 > dm_dig->rx_gain_max)
				dm_dig->rx_gain_min =
						dm_dig->rx_gain_max;
			else
				dm_dig->rx_gain_min =
						dm_dig->forbidden_igi + 1;
			dm_dig->recover_cnt = 3600;
		}
	} else {
		if (dm_dig->recover_cnt != 0) {
			dm_dig->recover_cnt--;
		} else {
			if (dm_dig->large_fa_hit < 3) {
				if ((dm_dig->forbidden_igi - 1) <
				    dig_min_0) {
					dm_dig->forbidden_igi = dig_min_0;
					dm_dig->rx_gain_min =
								dig_min_0;
				} else {
					dm_dig->forbidden_igi--;
					dm_dig->rx_gain_min =
						dm_dig->forbidden_igi + 1;
				}
			} else {
				dm_dig->large_fa_hit = 0;
			}
		}
	}

	if (rtlpriv->dm.dbginfo.num_qry_beacon_pkt < 5)
		dm_dig->rx_gain_min = dm_dig_min;

	if (dm_dig->rx_gain_min > dm_dig->rx_gain_max)
		dm_dig->rx_gain_min = dm_dig->rx_gain_max;

	if (mac->link_state >= MAC80211_LINKED) {
		if (bfirstconnect) {
			current_igi = min(dm_dig->rssi_val_min, dig_maxofmin);

			dm_dig->large_fa_hit = 0;
		} else {
			if (rtlpriv->falsealm_cnt.cnt_all > DM_DIG_FA_TH2)
				current_igi += 4;
			else if (rtlpriv->falsealm_cnt.cnt_all > DM_DIG_FA_TH1)
				current_igi += 2;
			else if (rtlpriv->falsealm_cnt.cnt_all < DM_DIG_FA_TH0)
				current_igi -= 2;

			if (rtlpriv->dm.dbginfo.num_qry_beacon_pkt < 5 &&
			    rtlpriv->falsealm_cnt.cnt_all < DM_DIG_FA_TH1)
				current_igi = dm_dig->rx_gain_min;
		}
	} else {
		if (bfirstdisconnect) {
			current_igi = dm_dig->rx_gain_min;
		} else {
			if (rtlpriv->falsealm_cnt.cnt_all > 10000)
				current_igi += 4;
			else if (rtlpriv->falsealm_cnt.cnt_all > 8000)
				current_igi += 2;
			else if (rtlpriv->falsealm_cnt.cnt_all < 500)
				current_igi -= 2;
		}
	}

	if (current_igi > dm_dig->rx_gain_max)
		current_igi = dm_dig->rx_gain_max;
	if (current_igi < dm_dig->rx_gain_min)
		current_igi = dm_dig->rx_gain_min;

	rtl92ee_dm_write_dig(hw , current_igi);
	dm_dig->media_connect_0 = ((mac->link_state >= MAC80211_LINKED) ?
				   true : false);
	dm_dig->dig_min_0 = dig_min_0;
}

void rtl92ee_dm_write_cck_cca_thres(struct ieee80211_hw *hw, u8 cur_thres)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_dig = &rtlpriv->dm_digtable;

	if (dm_dig->cur_cck_cca_thres != cur_thres)
		rtl_write_byte(rtlpriv, DM_REG_CCK_CCA_11N, cur_thres);

	dm_dig->pre_cck_cca_thres = dm_dig->cur_cck_cca_thres;
	dm_dig->cur_cck_cca_thres = cur_thres;
}

void rtl92ee_dm_write_dig(struct ieee80211_hw *hw, u8 current_igi)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_dig = &rtlpriv->dm_digtable;

	if (dm_dig->stop_dig)
		return;

	if (dm_dig->cur_igvalue != current_igi) {
		rtl_set_bbreg(hw, ROFDM0_XAAGCCORE1, 0x7f, current_igi);
		if (rtlpriv->phy.rf_type != RF_1T1R)
			rtl_set_bbreg(hw, ROFDM0_XBAGCCORE1, 0x7f, current_igi);
	}
	dm_dig->pre_igvalue = dm_dig->cur_igvalue;
	dm_dig->cur_igvalue = current_igi;
}

static void rtl92ee_rssi_dump_to_register(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, RA_RSSIDUMP,
		       rtlpriv->stats.rx_rssi_percentage[0]);
	rtl_write_byte(rtlpriv, RB_RSSIDUMP,
		       rtlpriv->stats.rx_rssi_percentage[1]);
	/*It seems the following values are not initialized.
	  *According to Windows code,
	  *these value will only be valid with JAGUAR chips
	  */
	/* Rx EVM */
	rtl_write_byte(rtlpriv, RS1_RXEVMDUMP, rtlpriv->stats.rx_evm_dbm[0]);
	rtl_write_byte(rtlpriv, RS2_RXEVMDUMP, rtlpriv->stats.rx_evm_dbm[1]);
	/* Rx SNR */
	rtl_write_byte(rtlpriv, RA_RXSNRDUMP,
		       (u8)(rtlpriv->stats.rx_snr_db[0]));
	rtl_write_byte(rtlpriv, RB_RXSNRDUMP,
		       (u8)(rtlpriv->stats.rx_snr_db[1]));
	/* Rx Cfo_Short */
	rtl_write_word(rtlpriv, RA_CFOSHORTDUMP,
		       rtlpriv->stats.rx_cfo_short[0]);
	rtl_write_word(rtlpriv, RB_CFOSHORTDUMP,
		       rtlpriv->stats.rx_cfo_short[1]);
	/* Rx Cfo_Tail */
	rtl_write_word(rtlpriv, RA_CFOLONGDUMP, rtlpriv->stats.rx_cfo_tail[0]);
	rtl_write_word(rtlpriv, RB_CFOLONGDUMP, rtlpriv->stats.rx_cfo_tail[1]);
}

static void rtl92ee_dm_find_minimum_rssi(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *rtl_dm_dig = &rtlpriv->dm_digtable;
	struct rtl_mac *mac = rtl_mac(rtlpriv);

	/* Determine the minimum RSSI  */
	if ((mac->link_state < MAC80211_LINKED) &&
	    (rtlpriv->dm.entry_min_undec_sm_pwdb == 0)) {
		rtl_dm_dig->min_undec_pwdb_for_dm = 0;
		rtl_dbg(rtlpriv, COMP_BB_POWERSAVING, DBG_LOUD,
			"Not connected to any\n");
	}
	if (mac->link_state >= MAC80211_LINKED) {
		if (mac->opmode == NL80211_IFTYPE_AP ||
		    mac->opmode == NL80211_IFTYPE_ADHOC) {
			rtl_dm_dig->min_undec_pwdb_for_dm =
				rtlpriv->dm.entry_min_undec_sm_pwdb;
			rtl_dbg(rtlpriv, COMP_BB_POWERSAVING, DBG_LOUD,
				"AP Client PWDB = 0x%lx\n",
				rtlpriv->dm.entry_min_undec_sm_pwdb);
		} else {
			rtl_dm_dig->min_undec_pwdb_for_dm =
			    rtlpriv->dm.undec_sm_pwdb;
			rtl_dbg(rtlpriv, COMP_BB_POWERSAVING, DBG_LOUD,
				"STA Default Port PWDB = 0x%x\n",
				rtl_dm_dig->min_undec_pwdb_for_dm);
		}
	} else {
		rtl_dm_dig->min_undec_pwdb_for_dm =
			rtlpriv->dm.entry_min_undec_sm_pwdb;
		rtl_dbg(rtlpriv, COMP_BB_POWERSAVING, DBG_LOUD,
			"AP Ext Port or disconnect PWDB = 0x%x\n",
			rtl_dm_dig->min_undec_pwdb_for_dm);
	}
	rtl_dbg(rtlpriv, COMP_DIG, DBG_LOUD,
		"MinUndecoratedPWDBForDM =%d\n",
		rtl_dm_dig->min_undec_pwdb_for_dm);
}

static void rtl92ee_dm_check_rssi_monitor(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_dig = &rtlpriv->dm_digtable;
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	struct rtl_dm *dm = rtl_dm(rtlpriv);
	struct rtl_sta_info *drv_priv;
	u8 h2c[4] = { 0 };
	long max = 0, min = 0xff;
	u8 i = 0;

	if (mac->opmode == NL80211_IFTYPE_AP ||
	    mac->opmode == NL80211_IFTYPE_ADHOC ||
	    mac->opmode == NL80211_IFTYPE_MESH_POINT) {
		/* AP & ADHOC & MESH */
		spin_lock_bh(&rtlpriv->locks.entry_list_lock);
		list_for_each_entry(drv_priv, &rtlpriv->entry_list, list) {
			struct rssi_sta *stat = &drv_priv->rssi_stat;

			if (stat->undec_sm_pwdb < min)
				min = stat->undec_sm_pwdb;
			if (stat->undec_sm_pwdb > max)
				max = stat->undec_sm_pwdb;

			h2c[3] = 0;
			h2c[2] = (u8)(dm->undec_sm_pwdb & 0xFF);
			h2c[1] = 0x20;
			h2c[0] = ++i;
			rtl92ee_fill_h2c_cmd(hw, H2C_92E_RSSI_REPORT, 4, h2c);
		}
		spin_unlock_bh(&rtlpriv->locks.entry_list_lock);

		/* If associated entry is found */
		if (max != 0) {
			dm->entry_max_undec_sm_pwdb = max;
			RTPRINT(rtlpriv, FDM, DM_PWDB,
				"EntryMaxPWDB = 0x%lx(%ld)\n", max, max);
		} else {
			dm->entry_max_undec_sm_pwdb = 0;
		}
		/* If associated entry is found */
		if (min != 0xff) {
			dm->entry_min_undec_sm_pwdb = min;
			RTPRINT(rtlpriv, FDM, DM_PWDB,
				"EntryMinPWDB = 0x%lx(%ld)\n", min, min);
		} else {
			dm->entry_min_undec_sm_pwdb = 0;
		}
	}

	/* Indicate Rx signal strength to FW. */
	if (dm->useramask) {
		h2c[3] = 0;
		h2c[2] = (u8)(dm->undec_sm_pwdb & 0xFF);
		h2c[1] = 0x20;
		h2c[0] = 0;
		rtl92ee_fill_h2c_cmd(hw, H2C_92E_RSSI_REPORT, 4, h2c);
	} else {
		rtl_write_byte(rtlpriv, 0x4fe, dm->undec_sm_pwdb);
	}
	rtl92ee_rssi_dump_to_register(hw);
	rtl92ee_dm_find_minimum_rssi(hw);
	dm_dig->rssi_val_min = rtlpriv->dm_digtable.min_undec_pwdb_for_dm;
}

static void rtl92ee_dm_init_primary_cca_check(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dynamic_primary_cca *primarycca = &rtlpriv->primarycca;

	primarycca->dup_rts_flag = 0;
	primarycca->intf_flag = 0;
	primarycca->intf_type = 0;
	primarycca->monitor_flag = 0;
	primarycca->ch_offset = 0;
	primarycca->mf_state = 0;
}

static bool rtl92ee_dm_is_edca_turbo_disable(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->mac80211.mode == WIRELESS_MODE_B)
		return true;

	return false;
}

void rtl92ee_dm_init_edca_turbo(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->dm.current_turbo_edca = false;
	rtlpriv->dm.is_cur_rdlstate = false;
	rtlpriv->dm.is_any_nonbepkts = false;
}

static void rtl92ee_dm_check_edca_turbo(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	static u64 last_txok_cnt;
	static u64 last_rxok_cnt;
	u64 cur_txok_cnt = 0;
	u64 cur_rxok_cnt = 0;
	u32 edca_be_ul = 0x5ea42b;
	u32 edca_be_dl = 0x5ea42b; /*not sure*/
	u32 edca_be = 0x5ea42b;
	bool is_cur_rdlstate;
	bool b_edca_turbo_on = false;

	if (rtlpriv->dm.dbginfo.num_non_be_pkt > 0x100)
		rtlpriv->dm.is_any_nonbepkts = true;
	rtlpriv->dm.dbginfo.num_non_be_pkt = 0;

	cur_txok_cnt = rtlpriv->stats.txbytesunicast - last_txok_cnt;
	cur_rxok_cnt = rtlpriv->stats.rxbytesunicast - last_rxok_cnt;

	/*b_bias_on_rx = false;*/
	b_edca_turbo_on = ((!rtlpriv->dm.is_any_nonbepkts) &&
			   (!rtlpriv->dm.disable_framebursting)) ?
			  true : false;

	if (rtl92ee_dm_is_edca_turbo_disable(hw))
		goto check_exit;

	if (b_edca_turbo_on) {
		is_cur_rdlstate = (cur_rxok_cnt > cur_txok_cnt * 4) ?
				    true : false;

		edca_be = is_cur_rdlstate ? edca_be_dl : edca_be_ul;
		rtl_write_dword(rtlpriv , REG_EDCA_BE_PARAM , edca_be);
		rtlpriv->dm.is_cur_rdlstate = is_cur_rdlstate;
		rtlpriv->dm.current_turbo_edca = true;
	} else {
		if (rtlpriv->dm.current_turbo_edca) {
			u8 tmp = AC0_BE;

			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_AC_PARAM,
						      (u8 *)(&tmp));
		}
		rtlpriv->dm.current_turbo_edca = false;
	}

check_exit:
	rtlpriv->dm.is_any_nonbepkts = false;
	last_txok_cnt = rtlpriv->stats.txbytesunicast;
	last_rxok_cnt = rtlpriv->stats.rxbytesunicast;
}

static void rtl92ee_dm_dynamic_edcca(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 reg_c50 , reg_c58;
	bool fw_current_in_ps_mode = false;

	rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_FW_PSMODE_STATUS,
				      (u8 *)(&fw_current_in_ps_mode));
	if (fw_current_in_ps_mode)
		return;

	reg_c50 = rtl_get_bbreg(hw, ROFDM0_XAAGCCORE1, MASKBYTE0);
	reg_c58 = rtl_get_bbreg(hw, ROFDM0_XBAGCCORE1, MASKBYTE0);

	if (reg_c50 > 0x28 && reg_c58 > 0x28) {
		if (!rtlpriv->rtlhal.pre_edcca_enable) {
			rtl_write_byte(rtlpriv, ROFDM0_ECCATHRESHOLD, 0x03);
			rtl_write_byte(rtlpriv, ROFDM0_ECCATHRESHOLD + 2, 0x00);
			rtlpriv->rtlhal.pre_edcca_enable = true;
		}
	} else if (reg_c50 < 0x25 && reg_c58 < 0x25) {
		if (rtlpriv->rtlhal.pre_edcca_enable) {
			rtl_write_byte(rtlpriv, ROFDM0_ECCATHRESHOLD, 0x7f);
			rtl_write_byte(rtlpriv, ROFDM0_ECCATHRESHOLD + 2, 0x7f);
			rtlpriv->rtlhal.pre_edcca_enable = false;
		}
	}
}

static void rtl92ee_dm_adaptivity(struct ieee80211_hw *hw)
{
	rtl92ee_dm_dynamic_edcca(hw);
}

static void rtl92ee_dm_write_dynamic_cca(struct ieee80211_hw *hw,
					 u8 cur_mf_state)
{
	struct dynamic_primary_cca *primarycca = &rtl_priv(hw)->primarycca;

	if (primarycca->mf_state != cur_mf_state)
		rtl_set_bbreg(hw, DM_REG_L1SBD_PD_CH_11N, BIT(8) | BIT(7),
			      cur_mf_state);

	primarycca->mf_state = cur_mf_state;
}

static void rtl92ee_dm_dynamic_primary_cca_check(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct false_alarm_statistics *falsealm_cnt = &rtlpriv->falsealm_cnt;
	struct dynamic_primary_cca *primarycca = &rtlpriv->primarycca;
	bool is40mhz = false;
	u64 ofdm_cca, ofdm_fa, bw_usc_cnt, bw_lsc_cnt;
	u8 sec_ch_offset;
	u8 cur_mf_state;
	static u8 count_down = MONITOR_TIME;

	ofdm_cca = falsealm_cnt->cnt_ofdm_cca;
	ofdm_fa = falsealm_cnt->cnt_ofdm_fail;
	bw_usc_cnt = falsealm_cnt->cnt_bw_usc;
	bw_lsc_cnt = falsealm_cnt->cnt_bw_lsc;
	is40mhz = rtlpriv->mac80211.bw_40;
	sec_ch_offset = rtlpriv->mac80211.cur_40_prime_sc;
	/* NIC: 2: sec is below,  1: sec is above */

	if (rtlpriv->mac80211.opmode == NL80211_IFTYPE_AP) {
		cur_mf_state = MF_USC_LSC;
		rtl92ee_dm_write_dynamic_cca(hw, cur_mf_state);
		return;
	}

	if (rtlpriv->mac80211.link_state < MAC80211_LINKED)
		return;

	if (is40mhz)
		return;

	if (primarycca->pricca_flag == 0) {
		/* Primary channel is above
		 * NOTE: duplicate CTS can remove this condition
		 */
		if (sec_ch_offset == 2) {
			if ((ofdm_cca > OFDMCCA_TH) &&
			    (bw_lsc_cnt > (bw_usc_cnt + BW_IND_BIAS)) &&
			    (ofdm_fa > (ofdm_cca >> 1))) {
				primarycca->intf_type = 1;
				primarycca->intf_flag = 1;
				cur_mf_state = MF_USC;
				rtl92ee_dm_write_dynamic_cca(hw, cur_mf_state);
				primarycca->pricca_flag = 1;
			} else if ((ofdm_cca > OFDMCCA_TH) &&
				   (bw_lsc_cnt > (bw_usc_cnt + BW_IND_BIAS)) &&
				   (ofdm_fa < (ofdm_cca >> 1))) {
				primarycca->intf_type = 2;
				primarycca->intf_flag = 1;
				cur_mf_state = MF_USC;
				rtl92ee_dm_write_dynamic_cca(hw, cur_mf_state);
				primarycca->pricca_flag = 1;
				primarycca->dup_rts_flag = 1;
			} else {
				primarycca->intf_type = 0;
				primarycca->intf_flag = 0;
				cur_mf_state = MF_USC_LSC;
				rtl92ee_dm_write_dynamic_cca(hw, cur_mf_state);
				primarycca->dup_rts_flag = 0;
			}
		} else if (sec_ch_offset == 1) {
			if ((ofdm_cca > OFDMCCA_TH) &&
			    (bw_usc_cnt > (bw_lsc_cnt + BW_IND_BIAS)) &&
			    (ofdm_fa > (ofdm_cca >> 1))) {
				primarycca->intf_type = 1;
				primarycca->intf_flag = 1;
				cur_mf_state = MF_LSC;
				rtl92ee_dm_write_dynamic_cca(hw, cur_mf_state);
				primarycca->pricca_flag = 1;
			} else if ((ofdm_cca > OFDMCCA_TH) &&
				   (bw_usc_cnt > (bw_lsc_cnt + BW_IND_BIAS)) &&
				   (ofdm_fa < (ofdm_cca >> 1))) {
				primarycca->intf_type = 2;
				primarycca->intf_flag = 1;
				cur_mf_state = MF_LSC;
				rtl92ee_dm_write_dynamic_cca(hw, cur_mf_state);
				primarycca->pricca_flag = 1;
				primarycca->dup_rts_flag = 1;
			} else {
				primarycca->intf_type = 0;
				primarycca->intf_flag = 0;
				cur_mf_state = MF_USC_LSC;
				rtl92ee_dm_write_dynamic_cca(hw, cur_mf_state);
				primarycca->dup_rts_flag = 0;
			}
		}
	} else {/* PrimaryCCA->PriCCA_flag==1 */
		count_down--;
		if (count_down == 0) {
			count_down = MONITOR_TIME;
			primarycca->pricca_flag = 0;
			cur_mf_state = MF_USC_LSC;
			/* default */
			rtl92ee_dm_write_dynamic_cca(hw, cur_mf_state);
			primarycca->dup_rts_flag = 0;
			primarycca->intf_type = 0;
			primarycca->intf_flag = 0;
		}
	}
}

static void rtl92ee_dm_dynamic_atc_switch(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_dm *rtldm = rtl_dm(rtl_priv(hw));
	u8 crystal_cap;
	u32 packet_count;
	int cfo_khz_a , cfo_khz_b , cfo_ave = 0, adjust_xtal = 0;
	int cfo_ave_diff;

	if (rtlpriv->mac80211.link_state < MAC80211_LINKED) {
		if (rtldm->atc_status == ATC_STATUS_OFF) {
			rtl_set_bbreg(hw, ROFDM1_CFOTRACKING, BIT(11),
				      ATC_STATUS_ON);
			rtldm->atc_status = ATC_STATUS_ON;
		}
		/* Disable CFO tracking for BT */
		if (rtlpriv->cfg->ops->get_btc_status()) {
			if (!rtlpriv->btcoexist.btc_ops->
			    btc_is_bt_disabled(rtlpriv)) {
				rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					"odm_DynamicATCSwitch(): Disable CFO tracking for BT!!\n");
				return;
			}
		}
		/* Reset Crystal Cap */
		if (rtldm->crystal_cap != rtlpriv->efuse.crystalcap) {
			rtldm->crystal_cap = rtlpriv->efuse.crystalcap;
			crystal_cap = rtldm->crystal_cap & 0x3f;
			rtl_set_bbreg(hw, REG_MAC_PHY_CTRL, 0xFFF000,
				      (crystal_cap | (crystal_cap << 6)));
		}
	} else {
		cfo_khz_a = (int)(rtldm->cfo_tail[0] * 3125) / 1280;
		cfo_khz_b = (int)(rtldm->cfo_tail[1] * 3125) / 1280;
		packet_count = rtldm->packet_count;

		if (packet_count == rtldm->packet_count_pre)
			return;

		rtldm->packet_count_pre = packet_count;

		if (rtlpriv->phy.rf_type == RF_1T1R)
			cfo_ave = cfo_khz_a;
		else
			cfo_ave = (int)(cfo_khz_a + cfo_khz_b) >> 1;

		cfo_ave_diff = (rtldm->cfo_ave_pre >= cfo_ave) ?
			       (rtldm->cfo_ave_pre - cfo_ave) :
			       (cfo_ave - rtldm->cfo_ave_pre);

		if (cfo_ave_diff > 20 && !rtldm->large_cfo_hit) {
			rtldm->large_cfo_hit = true;
			return;
		}
		rtldm->large_cfo_hit = false;

		rtldm->cfo_ave_pre = cfo_ave;

		if (cfo_ave >= -rtldm->cfo_threshold &&
		    cfo_ave <= rtldm->cfo_threshold && rtldm->is_freeze == 0) {
			if (rtldm->cfo_threshold == CFO_THRESHOLD_XTAL) {
				rtldm->cfo_threshold = CFO_THRESHOLD_XTAL + 10;
				rtldm->is_freeze = 1;
			} else {
				rtldm->cfo_threshold = CFO_THRESHOLD_XTAL;
			}
		}

		if (cfo_ave > rtldm->cfo_threshold && rtldm->crystal_cap < 0x3f)
			adjust_xtal = ((cfo_ave - CFO_THRESHOLD_XTAL) >> 2) + 1;
		else if ((cfo_ave < -rtlpriv->dm.cfo_threshold) &&
			 rtlpriv->dm.crystal_cap > 0)
			adjust_xtal = ((cfo_ave + CFO_THRESHOLD_XTAL) >> 2) - 1;

		if (adjust_xtal != 0) {
			rtldm->is_freeze = 0;
			rtldm->crystal_cap += adjust_xtal;

			if (rtldm->crystal_cap > 0x3f)
				rtldm->crystal_cap = 0x3f;
			else if (rtldm->crystal_cap < 0)
				rtldm->crystal_cap = 0;

			crystal_cap = rtldm->crystal_cap & 0x3f;
			rtl_set_bbreg(hw, REG_MAC_PHY_CTRL, 0xFFF000,
				      (crystal_cap | (crystal_cap << 6)));
		}

		if (cfo_ave < CFO_THRESHOLD_ATC &&
		    cfo_ave > -CFO_THRESHOLD_ATC) {
			if (rtldm->atc_status == ATC_STATUS_ON) {
				rtl_set_bbreg(hw, ROFDM1_CFOTRACKING, BIT(11),
					      ATC_STATUS_OFF);
				rtldm->atc_status = ATC_STATUS_OFF;
			}
		} else {
			if (rtldm->atc_status == ATC_STATUS_OFF) {
				rtl_set_bbreg(hw, ROFDM1_CFOTRACKING, BIT(11),
					      ATC_STATUS_ON);
				rtldm->atc_status = ATC_STATUS_ON;
			}
		}
	}
}

static void rtl92ee_dm_init_txpower_tracking(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_dm *dm = rtl_dm(rtlpriv);
	u8 path;

	dm->txpower_tracking = true;
	dm->default_ofdm_index = 30;
	dm->default_cck_index = 20;

	dm->swing_idx_cck_base = dm->default_cck_index;
	dm->cck_index = dm->default_cck_index;

	for (path = RF90_PATH_A; path < MAX_RF_PATH; path++) {
		dm->swing_idx_ofdm_base[path] = dm->default_ofdm_index;
		dm->ofdm_index[path] = dm->default_ofdm_index;
		dm->delta_power_index[path] = 0;
		dm->delta_power_index_last[path] = 0;
		dm->power_index_offset[path] = 0;
	}
}

void rtl92ee_dm_init_rate_adaptive_mask(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rate_adaptive *p_ra = &rtlpriv->ra;

	p_ra->ratr_state = DM_RATR_STA_INIT;
	p_ra->pre_ratr_state = DM_RATR_STA_INIT;

	if (rtlpriv->dm.dm_type == DM_TYPE_BYDRIVER)
		rtlpriv->dm.useramask = true;
	else
		rtlpriv->dm.useramask = false;

	p_ra->ldpc_thres = 35;
	p_ra->use_ldpc = false;
	p_ra->high_rssi_thresh_for_ra = 50;
	p_ra->low_rssi_thresh_for_ra40m = 20;
}

static bool _rtl92ee_dm_ra_state_check(struct ieee80211_hw *hw,
				       s32 rssi, u8 *ratr_state)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rate_adaptive *p_ra = &rtlpriv->ra;
	const u8 go_up_gap = 5;
	u32 high_rssithresh_for_ra = p_ra->high_rssi_thresh_for_ra;
	u32 low_rssithresh_for_ra = p_ra->low_rssi_thresh_for_ra40m;
	u8 state;

	/* Threshold Adjustment:
	 * when RSSI state trends to go up one or two levels,
	 * make sure RSSI is high enough.
	 * Here GoUpGap is added to solve
	 * the boundary's level alternation issue.
	 */
	switch (*ratr_state) {
	case DM_RATR_STA_INIT:
	case DM_RATR_STA_HIGH:
		break;
	case DM_RATR_STA_MIDDLE:
		high_rssithresh_for_ra += go_up_gap;
		break;
	case DM_RATR_STA_LOW:
		high_rssithresh_for_ra += go_up_gap;
		low_rssithresh_for_ra += go_up_gap;
		break;
	default:
		rtl_dbg(rtlpriv, COMP_RATR, DBG_DMESG,
			"wrong rssi level setting %d !\n", *ratr_state);
		break;
	}

	/* Decide RATRState by RSSI. */
	if (rssi > high_rssithresh_for_ra)
		state = DM_RATR_STA_HIGH;
	else if (rssi > low_rssithresh_for_ra)
		state = DM_RATR_STA_MIDDLE;
	else
		state = DM_RATR_STA_LOW;

	if (*ratr_state != state) {
		*ratr_state = state;
		return true;
	}

	return false;
}

static void rtl92ee_dm_refresh_rate_adaptive_mask(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rate_adaptive *p_ra = &rtlpriv->ra;
	struct ieee80211_sta *sta = NULL;

	if (is_hal_stop(rtlhal)) {
		rtl_dbg(rtlpriv, COMP_RATE, DBG_LOUD,
			"driver is going to unload\n");
		return;
	}

	if (!rtlpriv->dm.useramask) {
		rtl_dbg(rtlpriv, COMP_RATE, DBG_LOUD,
			"driver does not control rate adaptive mask\n");
		return;
	}

	if (mac->link_state == MAC80211_LINKED &&
	    mac->opmode == NL80211_IFTYPE_STATION) {
		if (rtlpriv->dm.undec_sm_pwdb < p_ra->ldpc_thres) {
			p_ra->use_ldpc = true;
			p_ra->lower_rts_rate = true;
		} else if (rtlpriv->dm.undec_sm_pwdb >
			   (p_ra->ldpc_thres - 5)) {
			p_ra->use_ldpc = false;
			p_ra->lower_rts_rate = false;
		}
		if (_rtl92ee_dm_ra_state_check(hw, rtlpriv->dm.undec_sm_pwdb,
					       &p_ra->ratr_state)) {
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

static void rtl92ee_dm_init_dynamic_atc_switch(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->dm.crystal_cap = rtlpriv->efuse.crystalcap;

	rtlpriv->dm.atc_status = rtl_get_bbreg(hw, ROFDM1_CFOTRACKING, BIT(11));
	rtlpriv->dm.cfo_threshold = CFO_THRESHOLD_XTAL;
}

void rtl92ee_dm_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 cur_igvalue = rtl_get_bbreg(hw, DM_REG_IGI_A_11N, DM_BIT_IGI_11N);

	rtlpriv->dm.dm_type = DM_TYPE_BYDRIVER;

	rtl_dm_diginit(hw, cur_igvalue);
	rtl92ee_dm_init_rate_adaptive_mask(hw);
	rtl92ee_dm_init_primary_cca_check(hw);
	rtl92ee_dm_init_edca_turbo(hw);
	rtl92ee_dm_init_txpower_tracking(hw);
	rtl92ee_dm_init_dynamic_atc_switch(hw);
}

static void rtl92ee_dm_common_info_self_update(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 cnt;

	rtlpriv->dm.one_entry_only = false;

	if (rtlpriv->mac80211.opmode == NL80211_IFTYPE_STATION &&
	    rtlpriv->mac80211.link_state >= MAC80211_LINKED) {
		rtlpriv->dm.one_entry_only = true;
		return;
	}

	if (rtlpriv->mac80211.opmode == NL80211_IFTYPE_AP ||
	    rtlpriv->mac80211.opmode == NL80211_IFTYPE_ADHOC ||
	    rtlpriv->mac80211.opmode == NL80211_IFTYPE_MESH_POINT) {
		spin_lock_bh(&rtlpriv->locks.entry_list_lock);
		cnt = list_count_nodes(&rtlpriv->entry_list);
		spin_unlock_bh(&rtlpriv->locks.entry_list_lock);

		if (cnt == 1)
			rtlpriv->dm.one_entry_only = true;
	}
}

void rtl92ee_dm_dynamic_arfb_select(struct ieee80211_hw *hw,
				    u8 rate, bool collision_state)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rate >= DESC92C_RATEMCS8  && rate <= DESC92C_RATEMCS12) {
		if (collision_state == 1) {
			if (rate == DESC92C_RATEMCS12) {
				rtl_write_dword(rtlpriv, REG_DARFRC, 0x0);
				rtl_write_dword(rtlpriv, REG_DARFRC + 4,
						0x07060501);
			} else if (rate == DESC92C_RATEMCS11) {
				rtl_write_dword(rtlpriv, REG_DARFRC, 0x0);
				rtl_write_dword(rtlpriv, REG_DARFRC + 4,
						0x07070605);
			} else if (rate == DESC92C_RATEMCS10) {
				rtl_write_dword(rtlpriv, REG_DARFRC, 0x0);
				rtl_write_dword(rtlpriv, REG_DARFRC + 4,
						0x08080706);
			} else if (rate == DESC92C_RATEMCS9) {
				rtl_write_dword(rtlpriv, REG_DARFRC, 0x0);
				rtl_write_dword(rtlpriv, REG_DARFRC + 4,
						0x08080707);
			} else {
				rtl_write_dword(rtlpriv, REG_DARFRC, 0x0);
				rtl_write_dword(rtlpriv, REG_DARFRC + 4,
						0x09090808);
			}
		} else {   /* collision_state == 0 */
			if (rate == DESC92C_RATEMCS12) {
				rtl_write_dword(rtlpriv, REG_DARFRC,
						0x05010000);
				rtl_write_dword(rtlpriv, REG_DARFRC + 4,
						0x09080706);
			} else if (rate == DESC92C_RATEMCS11) {
				rtl_write_dword(rtlpriv, REG_DARFRC,
						0x06050000);
				rtl_write_dword(rtlpriv, REG_DARFRC + 4,
						0x09080807);
			} else if (rate == DESC92C_RATEMCS10) {
				rtl_write_dword(rtlpriv, REG_DARFRC,
						0x07060000);
				rtl_write_dword(rtlpriv, REG_DARFRC + 4,
						0x0a090908);
			} else if (rate == DESC92C_RATEMCS9) {
				rtl_write_dword(rtlpriv, REG_DARFRC,
						0x07070000);
				rtl_write_dword(rtlpriv, REG_DARFRC + 4,
						0x0a090808);
			} else {
				rtl_write_dword(rtlpriv, REG_DARFRC,
						0x08080000);
				rtl_write_dword(rtlpriv, REG_DARFRC + 4,
						0x0b0a0909);
			}
		}
	} else {  /* MCS13~MCS15,  1SS, G-mode */
		if (collision_state == 1) {
			if (rate == DESC92C_RATEMCS15) {
				rtl_write_dword(rtlpriv, REG_DARFRC,
						0x00000000);
				rtl_write_dword(rtlpriv, REG_DARFRC + 4,
						0x05040302);
			} else if (rate == DESC92C_RATEMCS14) {
				rtl_write_dword(rtlpriv, REG_DARFRC,
						0x00000000);
				rtl_write_dword(rtlpriv, REG_DARFRC + 4,
						0x06050302);
			} else if (rate == DESC92C_RATEMCS13) {
				rtl_write_dword(rtlpriv, REG_DARFRC,
						0x00000000);
				rtl_write_dword(rtlpriv, REG_DARFRC + 4,
						0x07060502);
			} else {
				rtl_write_dword(rtlpriv, REG_DARFRC,
						0x00000000);
				rtl_write_dword(rtlpriv, REG_DARFRC + 4,
						0x06050402);
			}
		} else{   /* collision_state == 0 */
			if (rate == DESC92C_RATEMCS15) {
				rtl_write_dword(rtlpriv, REG_DARFRC,
						0x03020000);
				rtl_write_dword(rtlpriv, REG_DARFRC + 4,
						0x07060504);
			} else if (rate == DESC92C_RATEMCS14) {
				rtl_write_dword(rtlpriv, REG_DARFRC,
						0x03020000);
				rtl_write_dword(rtlpriv, REG_DARFRC + 4,
						0x08070605);
			} else if (rate == DESC92C_RATEMCS13) {
				rtl_write_dword(rtlpriv, REG_DARFRC,
						0x05020000);
				rtl_write_dword(rtlpriv, REG_DARFRC + 4,
						0x09080706);
			} else {
				rtl_write_dword(rtlpriv, REG_DARFRC,
						0x04020000);
				rtl_write_dword(rtlpriv, REG_DARFRC + 4,
						0x08070605);
			}
		}
	}
}

void rtl92ee_dm_watchdog(struct ieee80211_hw *hw)
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
		rtl92ee_dm_common_info_self_update(hw);
		rtl92ee_dm_false_alarm_counter_statistics(hw);
		rtl92ee_dm_check_rssi_monitor(hw);
		rtl92ee_dm_dig(hw);
		rtl92ee_dm_adaptivity(hw);
		rtl92ee_dm_cck_packet_detection_thresh(hw);
		rtl92ee_dm_refresh_rate_adaptive_mask(hw);
		rtl92ee_dm_check_edca_turbo(hw);
		rtl92ee_dm_dynamic_atc_switch(hw);
		rtl92ee_dm_dynamic_primary_cca_check(hw);
	}
	spin_unlock(&rtlpriv->locks.rf_ps_lock);
}
