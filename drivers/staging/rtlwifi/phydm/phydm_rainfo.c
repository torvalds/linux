// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

/* ************************************************************
 * include files
 * *************************************************************/
#include "mp_precomp.h"
#include "phydm_precomp.h"

void phydm_h2C_debug(void *dm_void, u32 *const dm_value, u32 *_used,
		     char *output, u32 *_out_len)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u8 h2c_parameter[H2C_MAX_LENGTH] = {0};
	u8 phydm_h2c_id = (u8)dm_value[0];
	u8 i;
	u32 used = *_used;
	u32 out_len = *_out_len;

	PHYDM_SNPRINTF(output + used, out_len - used,
		       "Phydm Send H2C_ID (( 0x%x))\n", phydm_h2c_id);
	for (i = 0; i < H2C_MAX_LENGTH; i++) {
		h2c_parameter[i] = (u8)dm_value[i + 1];
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "H2C: Byte[%d] = ((0x%x))\n", i,
			       h2c_parameter[i]);
	}

	odm_fill_h2c_cmd(dm, phydm_h2c_id, H2C_MAX_LENGTH, h2c_parameter);
}

void phydm_RA_debug_PCR(void *dm_void, u32 *const dm_value, u32 *_used,
			char *output, u32 *_out_len)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct ra_table *ra_tab = &dm->dm_ra_table;
	u32 used = *_used;
	u32 out_len = *_out_len;

	if (dm_value[0] == 100) {
		PHYDM_SNPRINTF(
			output + used, out_len - used,
			"[Get] PCR RA_threshold_offset = (( %s%d ))\n",
			((ra_tab->RA_threshold_offset == 0) ?
				 " " :
				 ((ra_tab->RA_offset_direction) ? "+" : "-")),
			ra_tab->RA_threshold_offset);
		/**/
	} else if (dm_value[0] == 0) {
		ra_tab->RA_offset_direction = 0;
		ra_tab->RA_threshold_offset = (u8)dm_value[1];
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "[Set] PCR RA_threshold_offset = (( -%d ))\n",
			       ra_tab->RA_threshold_offset);
	} else if (dm_value[0] == 1) {
		ra_tab->RA_offset_direction = 1;
		ra_tab->RA_threshold_offset = (u8)dm_value[1];
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "[Set] PCR RA_threshold_offset = (( +%d ))\n",
			       ra_tab->RA_threshold_offset);
	} else {
		PHYDM_SNPRINTF(output + used, out_len - used, "[Set] Error\n");
		/**/
	}
}

void odm_c2h_ra_para_report_handler(void *dm_void, u8 *cmd_buf, u8 cmd_len)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	u8 para_idx = cmd_buf[0]; /*Retry Penalty, NH, NL*/
	u8 i;

	ODM_RT_TRACE(dm, PHYDM_COMP_RA_DBG,
		     "[ From FW C2H RA Para ]  cmd_buf[0]= (( %d ))\n",
		     cmd_buf[0]);

	if (para_idx == RADBG_DEBUG_MONITOR1) {
		ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE,
			     "-------------------------------\n");
		if (dm->support_ic_type & PHYDM_IC_3081_SERIES) {
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  %d\n",
				     "RSSI =", cmd_buf[1]);
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  0x%x\n",
				     "rate =", cmd_buf[2] & 0x7f);
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  %d\n",
				     "SGI =", (cmd_buf[2] & 0x80) >> 7);
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  %d\n",
				     "BW =", cmd_buf[3]);
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  %d\n",
				     "BW_max =", cmd_buf[4]);
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  0x%x\n",
				     "multi_rate0 =", cmd_buf[5]);
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  0x%x\n",
				     "multi_rate1 =", cmd_buf[6]);
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  %d\n",
				     "DISRA =", cmd_buf[7]);
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  %d\n",
				     "VHT_EN =", cmd_buf[8]);
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  %d\n",
				     "SGI_support =", cmd_buf[9]);
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  %d\n",
				     "try_ness =", cmd_buf[10]);
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  0x%x\n",
				     "pre_rate =", cmd_buf[11]);
		} else {
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  %d\n",
				     "RSSI =", cmd_buf[1]);
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  %x\n",
				     "BW =", cmd_buf[2]);
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  %d\n",
				     "DISRA =", cmd_buf[3]);
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  %d\n",
				     "VHT_EN =", cmd_buf[4]);
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  %d\n",
				     "Highest rate =", cmd_buf[5]);
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  0x%x\n",
				     "Lowest rate =", cmd_buf[6]);
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  0x%x\n",
				     "SGI_support =", cmd_buf[7]);
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  %d\n",
				     "Rate_ID =", cmd_buf[8]);
		}
		ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE,
			     "-------------------------------\n");
	} else if (para_idx == RADBG_DEBUG_MONITOR2) {
		ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE,
			     "-------------------------------\n");
		if (dm->support_ic_type & PHYDM_IC_3081_SERIES) {
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  %d\n",
				     "rate_id =", cmd_buf[1]);
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  0x%x\n",
				     "highest_rate =", cmd_buf[2]);
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  0x%x\n",
				     "lowest_rate =", cmd_buf[3]);

			for (i = 4; i <= 11; i++)
				ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE,
					     "RAMASK =  0x%x\n", cmd_buf[i]);
		} else {
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE,
				     "%5s  %x%x  %x%x  %x%x  %x%x\n",
				     "RA Mask:", cmd_buf[8], cmd_buf[7],
				     cmd_buf[6], cmd_buf[5], cmd_buf[4],
				     cmd_buf[3], cmd_buf[2], cmd_buf[1]);
		}
		ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE,
			     "-------------------------------\n");
	} else if (para_idx == RADBG_DEBUG_MONITOR3) {
		for (i = 0; i < (cmd_len - 1); i++)
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE,
				     "content[%d] = %d\n", i, cmd_buf[1 + i]);
	} else if (para_idx == RADBG_DEBUG_MONITOR4) {
		ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  {%d.%d}\n",
			     "RA version =", cmd_buf[1], cmd_buf[2]);
	} else if (para_idx == RADBG_DEBUG_MONITOR5) {
		ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  0x%x\n",
			     "Current rate =", cmd_buf[1]);
		ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  %d\n",
			     "Retry ratio =", cmd_buf[2]);
		ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  %d\n",
			     "rate down ratio =", cmd_buf[3]);
		ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  0x%x\n",
			     "highest rate =", cmd_buf[4]);
		ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  {0x%x 0x%x}\n",
			     "Muti-try =", cmd_buf[5], cmd_buf[6]);
		ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "%5s  0x%x%x%x%x%x\n",
			     "RA mask =", cmd_buf[11], cmd_buf[10], cmd_buf[9],
			     cmd_buf[8], cmd_buf[7]);
	}
}

void phydm_ra_dynamic_retry_count(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	if (!(dm->support_ability & ODM_BB_DYNAMIC_ARFR))
		return;

	if (dm->pre_b_noisy != dm->noisy_decision) {
		if (dm->noisy_decision) {
			ODM_RT_TRACE(dm, ODM_COMP_RATE_ADAPTIVE,
				     "->Noisy Env. RA fallback value\n");
			odm_set_mac_reg(dm, 0x430, MASKDWORD, 0x0);
			odm_set_mac_reg(dm, 0x434, MASKDWORD, 0x04030201);
		} else {
			ODM_RT_TRACE(dm, ODM_COMP_RATE_ADAPTIVE,
				     "->Clean Env. RA fallback value\n");
			odm_set_mac_reg(dm, 0x430, MASKDWORD, 0x01000000);
			odm_set_mac_reg(dm, 0x434, MASKDWORD, 0x06050402);
		}
		dm->pre_b_noisy = dm->noisy_decision;
	}
}

void phydm_ra_dynamic_retry_limit(void *dm_void) {}

void phydm_print_rate(void *dm_void, u8 rate, u32 dbg_component)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u8 legacy_table[12] = {1, 2, 5, 11, 6, 9, 12, 18, 24, 36, 48, 54};
	u8 rate_idx = rate & 0x7f; /*remove bit7 SGI*/
	u8 vht_en = (rate_idx >= ODM_RATEVHTSS1MCS0) ? 1 : 0;
	u8 b_sgi = (rate & 0x80) >> 7;

	ODM_RT_TRACE(dm, dbg_component, "( %s%s%s%s%d%s%s)\n",
		     ((rate_idx >= ODM_RATEVHTSS1MCS0) &&
		      (rate_idx <= ODM_RATEVHTSS1MCS9)) ?
			     "VHT 1ss  " :
			     "",
		     ((rate_idx >= ODM_RATEVHTSS2MCS0) &&
		      (rate_idx <= ODM_RATEVHTSS2MCS9)) ?
			     "VHT 2ss " :
			     "",
		     ((rate_idx >= ODM_RATEVHTSS3MCS0) &&
		      (rate_idx <= ODM_RATEVHTSS3MCS9)) ?
			     "VHT 3ss " :
			     "",
		     (rate_idx >= ODM_RATEMCS0) ? "MCS " : "",
		     (vht_en) ? ((rate_idx - ODM_RATEVHTSS1MCS0) % 10) :
				((rate_idx >= ODM_RATEMCS0) ?
					 (rate_idx - ODM_RATEMCS0) :
					 ((rate_idx <= ODM_RATE54M) ?
						  legacy_table[rate_idx] :
						  0)),
		     (b_sgi) ? "-S" : "  ",
		     (rate_idx >= ODM_RATEMCS0) ? "" : "M");
}

void phydm_c2h_ra_report_handler(void *dm_void, u8 *cmd_buf, u8 cmd_len)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct ra_table *ra_tab = &dm->dm_ra_table;
	u8 macid = cmd_buf[1];
	u8 rate = cmd_buf[0];
	u8 rate_idx = rate & 0x7f; /*remove bit7 SGI*/
	u8 rate_order;

	if (cmd_len >= 4) {
		if (cmd_buf[3] == 0) {
			ODM_RT_TRACE(dm, ODM_COMP_RATE_ADAPTIVE,
				     "TX Init-rate Update[%d]:", macid);
			/**/
		} else if (cmd_buf[3] == 0xff) {
			ODM_RT_TRACE(dm, ODM_COMP_RATE_ADAPTIVE,
				     "FW Level: Fix rate[%d]:", macid);
			/**/
		} else if (cmd_buf[3] == 1) {
			ODM_RT_TRACE(dm, ODM_COMP_RATE_ADAPTIVE,
				     "Try Success[%d]:", macid);
			/**/
		} else if (cmd_buf[3] == 2) {
			ODM_RT_TRACE(dm, ODM_COMP_RATE_ADAPTIVE,
				     "Try Fail & Try Again[%d]:", macid);
			/**/
		} else if (cmd_buf[3] == 3) {
			ODM_RT_TRACE(dm, ODM_COMP_RATE_ADAPTIVE,
				     "rate Back[%d]:", macid);
			/**/
		} else if (cmd_buf[3] == 4) {
			ODM_RT_TRACE(dm, ODM_COMP_RATE_ADAPTIVE,
				     "start rate by RSSI[%d]:", macid);
			/**/
		} else if (cmd_buf[3] == 5) {
			ODM_RT_TRACE(dm, ODM_COMP_RATE_ADAPTIVE,
				     "Try rate[%d]:", macid);
			/**/
		}
	} else {
		ODM_RT_TRACE(dm, ODM_COMP_RATE_ADAPTIVE, "Tx rate Update[%d]:",
			     macid);
		/**/
	}

	phydm_print_rate(dm, rate, ODM_COMP_RATE_ADAPTIVE);

	ra_tab->link_tx_rate[macid] = rate;

	/*trigger power training*/

	rate_order = phydm_rate_order_compute(dm, rate_idx);

	if ((dm->is_one_entry_only) ||
	    ((rate_order > ra_tab->highest_client_tx_order) &&
	     (ra_tab->power_tracking_flag == 1))) {
		phydm_update_pwr_track(dm, rate_idx);
		ra_tab->power_tracking_flag = 0;
	}

	/*trigger dynamic rate ID*/
}

void odm_rssi_monitor_init(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct ra_table *ra_tab = &dm->dm_ra_table;

	ra_tab->firstconnect = false;
}

void odm_ra_post_action_on_assoc(void *dm_void) {}

void phydm_init_ra_info(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	if (dm->support_ic_type == ODM_RTL8822B) {
		u32 ret_value;

		ret_value = odm_get_bb_reg(dm, 0x4c8, MASKBYTE2);
		odm_set_bb_reg(dm, 0x4cc, MASKBYTE3, (ret_value - 1));
	}
}

void phydm_modify_RA_PCR_threshold(void *dm_void, u8 RA_offset_direction,
				   u8 RA_threshold_offset

				   )
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct ra_table *ra_tab = &dm->dm_ra_table;

	ra_tab->RA_offset_direction = RA_offset_direction;
	ra_tab->RA_threshold_offset = RA_threshold_offset;
	ODM_RT_TRACE(dm, ODM_COMP_RA_MASK,
		     "Set RA_threshold_offset = (( %s%d ))\n",
		     ((RA_threshold_offset == 0) ?
			      " " :
			      ((RA_offset_direction) ? "+" : "-")),
		     RA_threshold_offset);
}

static void odm_rssi_monitor_check_mp(void *dm_void) {}

static void odm_rssi_monitor_check_ce(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct ra_table *ra_tab = &dm->dm_ra_table;
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	struct rtl_sta_info *entry;
	int i;
	int tmp_entry_min_pwdb = 0xff;
	unsigned long cur_tx_ok_cnt = 0, cur_rx_ok_cnt = 0;
	u8 UL_DL_STATE = 0, STBC_TX = 0, tx_bf_en = 0;
	u8 h2c_parameter[H2C_0X42_LENGTH] = {0};
	u8 cmdlen = H2C_0X42_LENGTH;
	u8 macid = 0;

	if (!dm->is_linked)
		return;

	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
		entry = (struct rtl_sta_info *)dm->odm_sta_info[i];
		if (!IS_STA_VALID(entry))
			continue;

		if (is_multicast_ether_addr(entry->mac_addr) ||
		    is_broadcast_ether_addr(entry->mac_addr))
			continue;

		if (entry->rssi_stat.undecorated_smoothed_pwdb == (-1))
			continue;

		/* calculate min_pwdb */
		if (entry->rssi_stat.undecorated_smoothed_pwdb <
		    tmp_entry_min_pwdb)
			tmp_entry_min_pwdb =
				entry->rssi_stat.undecorated_smoothed_pwdb;

		/* report RSSI */
		cur_tx_ok_cnt = rtlpriv->stats.txbytesunicast_inperiod;
		cur_rx_ok_cnt = rtlpriv->stats.rxbytesunicast_inperiod;

		if (cur_rx_ok_cnt > (cur_tx_ok_cnt * 6))
			UL_DL_STATE = 1;
		else
			UL_DL_STATE = 0;

		if (mac->opmode == NL80211_IFTYPE_AP ||
		    mac->opmode == NL80211_IFTYPE_ADHOC) {
			struct ieee80211_sta *sta = container_of(
				(void *)entry, struct ieee80211_sta, drv_priv);
			macid = sta->aid + 1;
		}

		h2c_parameter[0] = macid;
		h2c_parameter[2] =
			entry->rssi_stat.undecorated_smoothed_pwdb & 0x7F;

		if (UL_DL_STATE)
			h2c_parameter[3] |= RAINFO_BE_RX_STATE;

		if (tx_bf_en)
			h2c_parameter[3] |= RAINFO_BF_STATE;
		if (STBC_TX)
			h2c_parameter[3] |= RAINFO_STBC_STATE;
		if (dm->noisy_decision)
			h2c_parameter[3] |= RAINFO_NOISY_STATE;

		if (entry->rssi_stat.is_send_rssi == RA_RSSI_STATE_SEND) {
			h2c_parameter[3] |= RAINFO_INIT_RSSI_RATE_STATE;
			entry->rssi_stat.is_send_rssi = RA_RSSI_STATE_HOLD;
		}

		h2c_parameter[4] = (ra_tab->RA_threshold_offset & 0x7f) |
				   (ra_tab->RA_offset_direction << 7);

		odm_fill_h2c_cmd(dm, ODM_H2C_RSSI_REPORT, cmdlen,
				 h2c_parameter);
	}

	if (tmp_entry_min_pwdb != 0xff)
		dm->rssi_min = tmp_entry_min_pwdb;
}

static void odm_rssi_monitor_check_ap(void *dm_void) {}

void odm_rssi_monitor_check(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	if (!(dm->support_ability & ODM_BB_RSSI_MONITOR))
		return;

	switch (dm->support_platform) {
	case ODM_WIN:
		odm_rssi_monitor_check_mp(dm);
		break;

	case ODM_CE:
		odm_rssi_monitor_check_ce(dm);
		break;

	case ODM_AP:
		odm_rssi_monitor_check_ap(dm);
		break;

	default:
		break;
	}
}

void odm_rate_adaptive_mask_init(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct odm_rate_adaptive *odm_ra = &dm->rate_adaptive;

	odm_ra->type = dm_type_by_driver;
	if (odm_ra->type == dm_type_by_driver)
		dm->is_use_ra_mask = true;
	else
		dm->is_use_ra_mask = false;

	odm_ra->ratr_state = DM_RATR_STA_INIT;

	odm_ra->ldpc_thres = 35;
	odm_ra->is_use_ldpc = false;

	odm_ra->high_rssi_thresh = 50;
	odm_ra->low_rssi_thresh = 20;
}

/*-----------------------------------------------------------------------------
 * Function:	odm_refresh_rate_adaptive_mask()
 *
 * Overview:	Update rate table mask according to rssi
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	05/27/2009	hpfan	Create version 0.
 *
 *---------------------------------------------------------------------------
 */
void odm_refresh_rate_adaptive_mask(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct ra_table *ra_tab = &dm->dm_ra_table;

	if (!dm->is_linked)
		return;

	if (!(dm->support_ability & ODM_BB_RA_MASK)) {
		ODM_RT_TRACE(dm, ODM_COMP_RA_MASK,
			     "%s(): Return cos not supported\n", __func__);
		return;
	}

	ra_tab->force_update_ra_mask_count++;
	/* 2011/09/29 MH In HW integration first stage, we provide 4 different
	 * handle to operate at the same time.
	 * In the stage2/3, we need to prive universal interface and merge all
	 * HW dynamic mechanism.
	 */
	switch (dm->support_platform) {
	case ODM_WIN:
		odm_refresh_rate_adaptive_mask_mp(dm);
		break;

	case ODM_CE:
		odm_refresh_rate_adaptive_mask_ce(dm);
		break;

	case ODM_AP:
		odm_refresh_rate_adaptive_mask_apadsl(dm);
		break;
	}
}

static u8 phydm_trans_platform_bw(void *dm_void, u8 BW)
{
	if (BW == HT_CHANNEL_WIDTH_20)
		BW = PHYDM_BW_20;

	else if (BW == HT_CHANNEL_WIDTH_20_40)
		BW = PHYDM_BW_40;

	else if (BW == HT_CHANNEL_WIDTH_80)
		BW = PHYDM_BW_80;

	return BW;
}

static u8 phydm_trans_platform_rf_type(void *dm_void, u8 rf_type)
{
	if (rf_type == RF_1T2R)
		rf_type = PHYDM_RF_1T2R;

	else if (rf_type == RF_2T4R)
		rf_type = PHYDM_RF_2T4R;

	else if (rf_type == RF_2T2R)
		rf_type = PHYDM_RF_2T2R;

	else if (rf_type == RF_1T1R)
		rf_type = PHYDM_RF_1T1R;

	else if (rf_type == RF_2T2R_GREEN)
		rf_type = PHYDM_RF_2T2R_GREEN;

	else if (rf_type == RF_3T3R)
		rf_type = PHYDM_RF_3T3R;

	else if (rf_type == RF_4T4R)
		rf_type = PHYDM_RF_4T4R;

	else if (rf_type == RF_2T3R)
		rf_type = PHYDM_RF_1T2R;

	else if (rf_type == RF_3T4R)
		rf_type = PHYDM_RF_3T4R;

	return rf_type;
}

static u32 phydm_trans_platform_wireless_mode(void *dm_void, u32 wireless_mode)
{
	return wireless_mode;
}

u8 phydm_vht_en_mapping(void *dm_void, u32 wireless_mode)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u8 vht_en_out = 0;

	if ((wireless_mode == PHYDM_WIRELESS_MODE_AC_5G) ||
	    (wireless_mode == PHYDM_WIRELESS_MODE_AC_24G) ||
	    (wireless_mode == PHYDM_WIRELESS_MODE_AC_ONLY)) {
		vht_en_out = 1;
		/**/
	}

	ODM_RT_TRACE(dm, ODM_COMP_RA_MASK,
		     "wireless_mode= (( 0x%x )), VHT_EN= (( %d ))\n",
		     wireless_mode, vht_en_out);
	return vht_en_out;
}

u8 phydm_rate_id_mapping(void *dm_void, u32 wireless_mode, u8 rf_type, u8 bw)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u8 rate_id_idx = 0;
	u8 phydm_BW;
	u8 phydm_rf_type;

	phydm_BW = phydm_trans_platform_bw(dm, bw);
	phydm_rf_type = phydm_trans_platform_rf_type(dm, rf_type);
	wireless_mode = phydm_trans_platform_wireless_mode(dm, wireless_mode);

	ODM_RT_TRACE(
		dm, ODM_COMP_RA_MASK,
		"wireless_mode= (( 0x%x )), rf_type = (( 0x%x )), BW = (( 0x%x ))\n",
		wireless_mode, phydm_rf_type, phydm_BW);

	switch (wireless_mode) {
	case PHYDM_WIRELESS_MODE_N_24G: {
		if (phydm_BW == PHYDM_BW_40) {
			if (phydm_rf_type == PHYDM_RF_1T1R)
				rate_id_idx = PHYDM_BGN_40M_1SS;
			else if (phydm_rf_type == PHYDM_RF_2T2R)
				rate_id_idx = PHYDM_BGN_40M_2SS;
			else
				rate_id_idx = PHYDM_ARFR5_N_3SS;

		} else {
			if (phydm_rf_type == PHYDM_RF_1T1R)
				rate_id_idx = PHYDM_BGN_20M_1SS;
			else if (phydm_rf_type == PHYDM_RF_2T2R)
				rate_id_idx = PHYDM_BGN_20M_2SS;
			else
				rate_id_idx = PHYDM_ARFR5_N_3SS;
		}
	} break;

	case PHYDM_WIRELESS_MODE_N_5G: {
		if (phydm_rf_type == PHYDM_RF_1T1R)
			rate_id_idx = PHYDM_GN_N1SS;
		else if (phydm_rf_type == PHYDM_RF_2T2R)
			rate_id_idx = PHYDM_GN_N2SS;
		else
			rate_id_idx = PHYDM_ARFR5_N_3SS;
	}

	break;

	case PHYDM_WIRELESS_MODE_G:
		rate_id_idx = PHYDM_BG;
		break;

	case PHYDM_WIRELESS_MODE_A:
		rate_id_idx = PHYDM_G;
		break;

	case PHYDM_WIRELESS_MODE_B:
		rate_id_idx = PHYDM_B_20M;
		break;

	case PHYDM_WIRELESS_MODE_AC_5G:
	case PHYDM_WIRELESS_MODE_AC_ONLY: {
		if (phydm_rf_type == PHYDM_RF_1T1R)
			rate_id_idx = PHYDM_ARFR1_AC_1SS;
		else if (phydm_rf_type == PHYDM_RF_2T2R)
			rate_id_idx = PHYDM_ARFR0_AC_2SS;
		else
			rate_id_idx = PHYDM_ARFR4_AC_3SS;
	} break;

	case PHYDM_WIRELESS_MODE_AC_24G: {
		/*Becareful to set "Lowest rate" while using PHYDM_ARFR4_AC_3SS
		 *in 2.4G/5G
		 */
		if (phydm_BW >= PHYDM_BW_80) {
			if (phydm_rf_type == PHYDM_RF_1T1R)
				rate_id_idx = PHYDM_ARFR1_AC_1SS;
			else if (phydm_rf_type == PHYDM_RF_2T2R)
				rate_id_idx = PHYDM_ARFR0_AC_2SS;
			else
				rate_id_idx = PHYDM_ARFR4_AC_3SS;
		} else {
			if (phydm_rf_type == PHYDM_RF_1T1R)
				rate_id_idx = PHYDM_ARFR2_AC_2G_1SS;
			else if (phydm_rf_type == PHYDM_RF_2T2R)
				rate_id_idx = PHYDM_ARFR3_AC_2G_2SS;
			else
				rate_id_idx = PHYDM_ARFR4_AC_3SS;
		}
	} break;

	default:
		rate_id_idx = 0;
		break;
	}

	ODM_RT_TRACE(dm, ODM_COMP_RA_MASK, "RA rate ID = (( 0x%x ))\n",
		     rate_id_idx);

	return rate_id_idx;
}

void phydm_update_hal_ra_mask(void *dm_void, u32 wireless_mode, u8 rf_type,
			      u8 BW, u8 mimo_ps_enable, u8 disable_cck_rate,
			      u32 *ratr_bitmap_msb_in, u32 *ratr_bitmap_lsb_in,
			      u8 tx_rate_level)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u8 phydm_rf_type;
	u8 phydm_BW;
	u32 ratr_bitmap = *ratr_bitmap_lsb_in,
	    ratr_bitmap_msb = *ratr_bitmap_msb_in;

	wireless_mode = phydm_trans_platform_wireless_mode(dm, wireless_mode);

	phydm_rf_type = phydm_trans_platform_rf_type(dm, rf_type);
	phydm_BW = phydm_trans_platform_bw(dm, BW);

	ODM_RT_TRACE(dm, ODM_COMP_RA_MASK,
		     "Platform original RA Mask = (( 0x %x | %x ))\n",
		     ratr_bitmap_msb, ratr_bitmap);

	switch (wireless_mode) {
	case PHYDM_WIRELESS_MODE_B: {
		ratr_bitmap &= 0x0000000f;
	} break;

	case PHYDM_WIRELESS_MODE_G: {
		ratr_bitmap &= 0x00000ff5;
	} break;

	case PHYDM_WIRELESS_MODE_A: {
		ratr_bitmap &= 0x00000ff0;
	} break;

	case PHYDM_WIRELESS_MODE_N_24G:
	case PHYDM_WIRELESS_MODE_N_5G: {
		if (mimo_ps_enable)
			phydm_rf_type = PHYDM_RF_1T1R;

		if (phydm_rf_type == PHYDM_RF_1T1R) {
			if (phydm_BW == PHYDM_BW_40)
				ratr_bitmap &= 0x000ff015;
			else
				ratr_bitmap &= 0x000ff005;
		} else if (phydm_rf_type == PHYDM_RF_2T2R ||
			   phydm_rf_type == PHYDM_RF_2T4R ||
			   phydm_rf_type == PHYDM_RF_2T3R) {
			if (phydm_BW == PHYDM_BW_40)
				ratr_bitmap &= 0x0ffff015;
			else
				ratr_bitmap &= 0x0ffff005;
		} else { /*3T*/

			ratr_bitmap &= 0xfffff015;
			ratr_bitmap_msb &= 0xf;
		}
	} break;

	case PHYDM_WIRELESS_MODE_AC_24G: {
		if (phydm_rf_type == PHYDM_RF_1T1R) {
			ratr_bitmap &= 0x003ff015;
		} else if (phydm_rf_type == PHYDM_RF_2T2R ||
			   phydm_rf_type == PHYDM_RF_2T4R ||
			   phydm_rf_type == PHYDM_RF_2T3R) {
			ratr_bitmap &= 0xfffff015;
		} else { /*3T*/

			ratr_bitmap &= 0xfffff010;
			ratr_bitmap_msb &= 0x3ff;
		}

		if (phydm_BW ==
		    PHYDM_BW_20) { /* AC 20MHz doesn't support MCS9 */
			ratr_bitmap &= 0x7fdfffff;
			ratr_bitmap_msb &= 0x1ff;
		}
	} break;

	case PHYDM_WIRELESS_MODE_AC_5G: {
		if (phydm_rf_type == PHYDM_RF_1T1R) {
			ratr_bitmap &= 0x003ff010;
		} else if (phydm_rf_type == PHYDM_RF_2T2R ||
			   phydm_rf_type == PHYDM_RF_2T4R ||
			   phydm_rf_type == PHYDM_RF_2T3R) {
			ratr_bitmap &= 0xfffff010;
		} else { /*3T*/

			ratr_bitmap &= 0xfffff010;
			ratr_bitmap_msb &= 0x3ff;
		}

		if (phydm_BW ==
		    PHYDM_BW_20) { /* AC 20MHz doesn't support MCS9 */
			ratr_bitmap &= 0x7fdfffff;
			ratr_bitmap_msb &= 0x1ff;
		}
	} break;

	default:
		break;
	}

	if (wireless_mode != PHYDM_WIRELESS_MODE_B) {
		if (tx_rate_level == 0)
			ratr_bitmap &= 0xffffffff;
		else if (tx_rate_level == 1)
			ratr_bitmap &= 0xfffffff0;
		else if (tx_rate_level == 2)
			ratr_bitmap &= 0xffffefe0;
		else if (tx_rate_level == 3)
			ratr_bitmap &= 0xffffcfc0;
		else if (tx_rate_level == 4)
			ratr_bitmap &= 0xffff8f80;
		else if (tx_rate_level >= 5)
			ratr_bitmap &= 0xffff0f00;
	}

	if (disable_cck_rate)
		ratr_bitmap &= 0xfffffff0;

	ODM_RT_TRACE(
		dm, ODM_COMP_RA_MASK,
		"wireless_mode= (( 0x%x )), rf_type = (( 0x%x )), BW = (( 0x%x )), MimoPs_en = (( %d )), tx_rate_level= (( 0x%x ))\n",
		wireless_mode, phydm_rf_type, phydm_BW, mimo_ps_enable,
		tx_rate_level);

	*ratr_bitmap_lsb_in = ratr_bitmap;
	*ratr_bitmap_msb_in = ratr_bitmap_msb;
	ODM_RT_TRACE(dm, ODM_COMP_RA_MASK,
		     "Phydm modified RA Mask = (( 0x %x | %x ))\n",
		     *ratr_bitmap_msb_in, *ratr_bitmap_lsb_in);
}

u8 phydm_RA_level_decision(void *dm_void, u32 rssi, u8 ratr_state)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u8 ra_rate_floor_table[RA_FLOOR_TABLE_SIZE] = {
		20, 34, 38, 42,
		46, 50, 100}; /*MCS0 ~ MCS4 , VHT1SS MCS0 ~ MCS4 , G 6M~24M*/
	u8 new_ratr_state = 0;
	u8 i;

	ODM_RT_TRACE(
		dm, ODM_COMP_RA_MASK,
		"curr RA level = ((%d)), Rate_floor_table ori [ %d , %d, %d , %d, %d, %d]\n",
		ratr_state, ra_rate_floor_table[0], ra_rate_floor_table[1],
		ra_rate_floor_table[2], ra_rate_floor_table[3],
		ra_rate_floor_table[4], ra_rate_floor_table[5]);

	for (i = 0; i < RA_FLOOR_TABLE_SIZE; i++) {
		if (i >= (ratr_state))
			ra_rate_floor_table[i] += RA_FLOOR_UP_GAP;
	}

	ODM_RT_TRACE(
		dm, ODM_COMP_RA_MASK,
		"RSSI = ((%d)), Rate_floor_table_mod [ %d , %d, %d , %d, %d, %d]\n",
		rssi, ra_rate_floor_table[0], ra_rate_floor_table[1],
		ra_rate_floor_table[2], ra_rate_floor_table[3],
		ra_rate_floor_table[4], ra_rate_floor_table[5]);

	for (i = 0; i < RA_FLOOR_TABLE_SIZE; i++) {
		if (rssi < ra_rate_floor_table[i]) {
			new_ratr_state = i;
			break;
		}
	}

	return new_ratr_state;
}

void odm_refresh_rate_adaptive_mask_mp(void *dm_void) {}

void odm_refresh_rate_adaptive_mask_ce(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct ra_table *ra_tab = &dm->dm_ra_table;
	void *adapter = dm->adapter;
	u32 i;
	struct rtl_sta_info *entry;
	u8 ratr_state_new;

	if (!dm->is_use_ra_mask) {
		ODM_RT_TRACE(
			dm, ODM_COMP_RA_MASK,
			"<---- %s(): driver does not control rate adaptive mask\n",
			__func__);
		return;
	}

	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
		entry = dm->odm_sta_info[i];

		if (!IS_STA_VALID(entry))
			continue;

		if (is_multicast_ether_addr(entry->mac_addr))
			continue;
		else if (is_broadcast_ether_addr(entry->mac_addr))
			continue;

		ratr_state_new = phydm_RA_level_decision(
			dm, entry->rssi_stat.undecorated_smoothed_pwdb,
			entry->rssi_level);

		if ((entry->rssi_level != ratr_state_new) ||
		    (ra_tab->force_update_ra_mask_count >=
		     FORCED_UPDATE_RAMASK_PERIOD)) {
			ra_tab->force_update_ra_mask_count = 0;
			ODM_RT_TRACE(
				dm, ODM_COMP_RA_MASK,
				"Update Tx RA Level: ((%x)) -> ((%x)),  RSSI = ((%d))\n",
				entry->rssi_level, ratr_state_new,
				entry->rssi_stat.undecorated_smoothed_pwdb);

			entry->rssi_level = ratr_state_new;
			rtl_hal_update_ra_mask(adapter, entry,
					       entry->rssi_level);
		} else {
			ODM_RT_TRACE(dm, ODM_COMP_RA_MASK,
				     "Stay in RA level  = (( %d ))\n\n",
				     ratr_state_new);
			/**/
		}
	}
}

void odm_refresh_rate_adaptive_mask_apadsl(void *dm_void) {}

void odm_refresh_basic_rate_mask(void *dm_void) {}

u8 phydm_rate_order_compute(void *dm_void, u8 rate_idx)
{
	u8 rate_order = 0;

	if (rate_idx >= ODM_RATEVHTSS4MCS0) {
		rate_idx -= ODM_RATEVHTSS4MCS0;
		/**/
	} else if (rate_idx >= ODM_RATEVHTSS3MCS0) {
		rate_idx -= ODM_RATEVHTSS3MCS0;
		/**/
	} else if (rate_idx >= ODM_RATEVHTSS2MCS0) {
		rate_idx -= ODM_RATEVHTSS2MCS0;
		/**/
	} else if (rate_idx >= ODM_RATEVHTSS1MCS0) {
		rate_idx -= ODM_RATEVHTSS1MCS0;
		/**/
	} else if (rate_idx >= ODM_RATEMCS24) {
		rate_idx -= ODM_RATEMCS24;
		/**/
	} else if (rate_idx >= ODM_RATEMCS16) {
		rate_idx -= ODM_RATEMCS16;
		/**/
	} else if (rate_idx >= ODM_RATEMCS8) {
		rate_idx -= ODM_RATEMCS8;
		/**/
	}
	rate_order = rate_idx;

	return rate_order;
}

static void phydm_ra_common_info_update(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct ra_table *ra_tab = &dm->dm_ra_table;
	u16 macid;
	u8 rate_order_tmp;
	u8 cnt = 0;

	ra_tab->highest_client_tx_order = 0;
	ra_tab->power_tracking_flag = 1;

	if (dm->number_linked_client != 0) {
		for (macid = 0; macid < ODM_ASSOCIATE_ENTRY_NUM; macid++) {
			rate_order_tmp = phydm_rate_order_compute(
				dm, ((ra_tab->link_tx_rate[macid]) & 0x7f));

			if (rate_order_tmp >=
			    (ra_tab->highest_client_tx_order)) {
				ra_tab->highest_client_tx_order =
					rate_order_tmp;
				ra_tab->highest_client_tx_rate_order = macid;
			}

			cnt++;

			if (cnt == dm->number_linked_client)
				break;
		}
		ODM_RT_TRACE(
			dm, ODM_COMP_RATE_ADAPTIVE,
			"MACID[%d], Highest Tx order Update for power tracking: %d\n",
			(ra_tab->highest_client_tx_rate_order),
			(ra_tab->highest_client_tx_order));
	}
}

void phydm_ra_info_watchdog(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	phydm_ra_common_info_update(dm);
	phydm_ra_dynamic_retry_limit(dm);
	phydm_ra_dynamic_retry_count(dm);
	odm_refresh_rate_adaptive_mask(dm);
	odm_refresh_basic_rate_mask(dm);
}

void phydm_ra_info_init(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct ra_table *ra_tab = &dm->dm_ra_table;

	ra_tab->highest_client_tx_rate_order = 0;
	ra_tab->highest_client_tx_order = 0;
	ra_tab->RA_threshold_offset = 0;
	ra_tab->RA_offset_direction = 0;
}

u8 odm_find_rts_rate(void *dm_void, u8 tx_rate, bool is_erp_protect)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u8 rts_ini_rate = ODM_RATE6M;

	if (is_erp_protect) { /* use CCK rate as RTS*/
		rts_ini_rate = ODM_RATE1M;
	} else {
		switch (tx_rate) {
		case ODM_RATEVHTSS3MCS9:
		case ODM_RATEVHTSS3MCS8:
		case ODM_RATEVHTSS3MCS7:
		case ODM_RATEVHTSS3MCS6:
		case ODM_RATEVHTSS3MCS5:
		case ODM_RATEVHTSS3MCS4:
		case ODM_RATEVHTSS3MCS3:
		case ODM_RATEVHTSS2MCS9:
		case ODM_RATEVHTSS2MCS8:
		case ODM_RATEVHTSS2MCS7:
		case ODM_RATEVHTSS2MCS6:
		case ODM_RATEVHTSS2MCS5:
		case ODM_RATEVHTSS2MCS4:
		case ODM_RATEVHTSS2MCS3:
		case ODM_RATEVHTSS1MCS9:
		case ODM_RATEVHTSS1MCS8:
		case ODM_RATEVHTSS1MCS7:
		case ODM_RATEVHTSS1MCS6:
		case ODM_RATEVHTSS1MCS5:
		case ODM_RATEVHTSS1MCS4:
		case ODM_RATEVHTSS1MCS3:
		case ODM_RATEMCS15:
		case ODM_RATEMCS14:
		case ODM_RATEMCS13:
		case ODM_RATEMCS12:
		case ODM_RATEMCS11:
		case ODM_RATEMCS7:
		case ODM_RATEMCS6:
		case ODM_RATEMCS5:
		case ODM_RATEMCS4:
		case ODM_RATEMCS3:
		case ODM_RATE54M:
		case ODM_RATE48M:
		case ODM_RATE36M:
		case ODM_RATE24M:
			rts_ini_rate = ODM_RATE24M;
			break;
		case ODM_RATEVHTSS3MCS2:
		case ODM_RATEVHTSS3MCS1:
		case ODM_RATEVHTSS2MCS2:
		case ODM_RATEVHTSS2MCS1:
		case ODM_RATEVHTSS1MCS2:
		case ODM_RATEVHTSS1MCS1:
		case ODM_RATEMCS10:
		case ODM_RATEMCS9:
		case ODM_RATEMCS2:
		case ODM_RATEMCS1:
		case ODM_RATE18M:
		case ODM_RATE12M:
			rts_ini_rate = ODM_RATE12M;
			break;
		case ODM_RATEVHTSS3MCS0:
		case ODM_RATEVHTSS2MCS0:
		case ODM_RATEVHTSS1MCS0:
		case ODM_RATEMCS8:
		case ODM_RATEMCS0:
		case ODM_RATE9M:
		case ODM_RATE6M:
			rts_ini_rate = ODM_RATE6M;
			break;
		case ODM_RATE11M:
		case ODM_RATE5_5M:
		case ODM_RATE2M:
		case ODM_RATE1M:
			rts_ini_rate = ODM_RATE1M;
			break;
		default:
			rts_ini_rate = ODM_RATE6M;
			break;
		}
	}

	if (*dm->band_type == 1) {
		if (rts_ini_rate < ODM_RATE6M)
			rts_ini_rate = ODM_RATE6M;
	}
	return rts_ini_rate;
}

static void odm_set_ra_dm_arfb_by_noisy(struct phy_dm_struct *dm) {}

void odm_update_noisy_state(void *dm_void, bool is_noisy_state_from_c2h)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	/* JJ ADD 20161014 */
	if (dm->support_ic_type == ODM_RTL8821 ||
	    dm->support_ic_type == ODM_RTL8812 ||
	    dm->support_ic_type == ODM_RTL8723B ||
	    dm->support_ic_type == ODM_RTL8192E ||
	    dm->support_ic_type == ODM_RTL8188E ||
	    dm->support_ic_type == ODM_RTL8723D ||
	    dm->support_ic_type == ODM_RTL8710B)
		dm->is_noisy_state = is_noisy_state_from_c2h;
	odm_set_ra_dm_arfb_by_noisy(dm);
};

void phydm_update_pwr_track(void *dm_void, u8 rate)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	ODM_RT_TRACE(dm, ODM_COMP_TX_PWR_TRACK, "Pwr Track Get rate=0x%x\n",
		     rate);

	dm->tx_rate = rate;
}

/* RA_MASK_PHYDMLIZE, will delete it later*/

bool odm_ra_state_check(void *dm_void, s32 rssi, bool is_force_update,
			u8 *ra_tr_state)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct odm_rate_adaptive *ra = &dm->rate_adaptive;
	const u8 go_up_gap = 5;
	u8 high_rssi_thresh_for_ra = ra->high_rssi_thresh;
	u8 low_rssi_thresh_for_ra = ra->low_rssi_thresh;
	u8 ratr_state;

	ODM_RT_TRACE(dm, ODM_COMP_RA_MASK,
		     "RSSI= (( %d )), Current_RSSI_level = (( %d ))\n", rssi,
		     *ra_tr_state);
	ODM_RT_TRACE(dm, ODM_COMP_RA_MASK,
		     "[Ori RA RSSI Thresh]  High= (( %d )), Low = (( %d ))\n",
		     high_rssi_thresh_for_ra, low_rssi_thresh_for_ra);
	/* threshold Adjustment:
	 * when RSSI state trends to go up one or two levels, make sure RSSI is
	 * high enough. Here go_up_gap is added to solve the boundary's level
	 * alternation issue.
	 */

	switch (*ra_tr_state) {
	case DM_RATR_STA_INIT:
	case DM_RATR_STA_HIGH:
		break;

	case DM_RATR_STA_MIDDLE:
		high_rssi_thresh_for_ra += go_up_gap;
		break;

	case DM_RATR_STA_LOW:
		high_rssi_thresh_for_ra += go_up_gap;
		low_rssi_thresh_for_ra += go_up_gap;
		break;

	default:
		WARN_ONCE(true, "wrong rssi level setting %d !", *ra_tr_state);
		break;
	}

	/* Decide ratr_state by RSSI.*/
	if (rssi > high_rssi_thresh_for_ra)
		ratr_state = DM_RATR_STA_HIGH;
	else if (rssi > low_rssi_thresh_for_ra)
		ratr_state = DM_RATR_STA_MIDDLE;

	else
		ratr_state = DM_RATR_STA_LOW;
	ODM_RT_TRACE(dm, ODM_COMP_RA_MASK,
		     "[Mod RA RSSI Thresh]  High= (( %d )), Low = (( %d ))\n",
		     high_rssi_thresh_for_ra, low_rssi_thresh_for_ra);

	if (*ra_tr_state != ratr_state || is_force_update) {
		ODM_RT_TRACE(dm, ODM_COMP_RA_MASK,
			     "[RSSI Level Update] %d->%d\n", *ra_tr_state,
			     ratr_state);
		*ra_tr_state = ratr_state;
		return true;
	}

	return false;
}
