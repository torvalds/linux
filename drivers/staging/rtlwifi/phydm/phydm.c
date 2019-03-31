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

static const u16 db_invert_table[12][8] = {
	{1, 1, 1, 2, 2, 2, 2, 3},
	{3, 3, 4, 4, 4, 5, 6, 6},
	{7, 8, 9, 10, 11, 13, 14, 16},
	{18, 20, 22, 25, 28, 32, 35, 40},
	{45, 50, 56, 63, 71, 79, 89, 100},
	{112, 126, 141, 158, 178, 200, 224, 251},
	{282, 316, 355, 398, 447, 501, 562, 631},
	{708, 794, 891, 1000, 1122, 1259, 1413, 1585},
	{1778, 1995, 2239, 2512, 2818, 3162, 3548, 3981},
	{4467, 5012, 5623, 6310, 7079, 7943, 8913, 10000},
	{11220, 12589, 14125, 15849, 17783, 19953, 22387, 25119},
	{28184, 31623, 35481, 39811, 44668, 50119, 56234, 65535},
};

/* ************************************************************
 * Local Function predefine.
 * *************************************************************/

/* START------------COMMON INFO RELATED--------------- */

static void odm_update_power_training_state(struct phy_dm_struct *dm);

/* ************************************************************
 * 3 Export Interface
 * *************************************************************/

/*Y = 10*log(X)*/
s32 odm_pwdb_conversion(s32 X, u32 total_bit, u32 decimal_bit)
{
	s32 integer = 0, decimal = 0;
	u32 i;

	if (X == 0)
		X = 1; /* log2(x), x can't be 0 */

	for (i = (total_bit - 1); i > 0; i--) {
		if (X & BIT(i)) {
			integer = i;
			if (i > 0) {
				/* decimal is 0.5dB*3=1.5dB~=2dB */
				decimal = (X & BIT(i - 1)) ? 2 : 0;
			}
			break;
		}
	}

	return 3 * (integer - decimal_bit) + decimal; /* 10*log(x)=3*log2(x), */;
}

s32 odm_sign_conversion(s32 value, u32 total_bit)
{
	if (value & BIT(total_bit - 1))
		value -= BIT(total_bit);
	return value;
}

void phydm_seq_sorting(void *dm_void, u32 *value, u32 *rank_idx, u32 *idx_out,
		       u8 seq_length)
{
	u8 i = 0, j = 0;
	u32 tmp_a, tmp_b;
	u32 tmp_idx_a, tmp_idx_b;

	for (i = 0; i < seq_length; i++) {
		rank_idx[i] = i;
		/**/
	}

	for (i = 0; i < (seq_length - 1); i++) {
		for (j = 0; j < (seq_length - 1 - i); j++) {
			tmp_a = value[j];
			tmp_b = value[j + 1];

			tmp_idx_a = rank_idx[j];
			tmp_idx_b = rank_idx[j + 1];

			if (tmp_a < tmp_b) {
				value[j] = tmp_b;
				value[j + 1] = tmp_a;

				rank_idx[j] = tmp_idx_b;
				rank_idx[j + 1] = tmp_idx_a;
			}
		}
	}

	for (i = 0; i < seq_length; i++) {
		idx_out[rank_idx[i]] = i + 1;
		/**/
	}
}

void odm_init_mp_driver_status(struct phy_dm_struct *dm)
{
	dm->mp_mode = false;
}

static void odm_update_mp_driver_status(struct phy_dm_struct *dm)
{
	/* Do nothing. */
}

static void phydm_init_trx_antenna_setting(struct phy_dm_struct *dm)
{
	/*#if (RTL8814A_SUPPORT == 1)*/

	if (dm->support_ic_type & (ODM_RTL8814A)) {
		u8 rx_ant = 0, tx_ant = 0;

		rx_ant = (u8)odm_get_bb_reg(dm, ODM_REG(BB_RX_PATH, dm),
					    ODM_BIT(BB_RX_PATH, dm));
		tx_ant = (u8)odm_get_bb_reg(dm, ODM_REG(BB_TX_PATH, dm),
					    ODM_BIT(BB_TX_PATH, dm));
		dm->tx_ant_status = (tx_ant & 0xf);
		dm->rx_ant_status = (rx_ant & 0xf);
	} else if (dm->support_ic_type & (ODM_RTL8723D | ODM_RTL8821C |
					  ODM_RTL8710B)) { /* JJ ADD 20161014 */
		dm->tx_ant_status = 0x1;
		dm->rx_ant_status = 0x1;
	}
	/*#endif*/
}

static void phydm_traffic_load_decision(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	/*---TP & Traffic-load calculation---*/

	if (dm->last_tx_ok_cnt > *dm->num_tx_bytes_unicast)
		dm->last_tx_ok_cnt = *dm->num_tx_bytes_unicast;

	if (dm->last_rx_ok_cnt > *dm->num_rx_bytes_unicast)
		dm->last_rx_ok_cnt = *dm->num_rx_bytes_unicast;

	dm->cur_tx_ok_cnt = *dm->num_tx_bytes_unicast - dm->last_tx_ok_cnt;
	dm->cur_rx_ok_cnt = *dm->num_rx_bytes_unicast - dm->last_rx_ok_cnt;
	dm->last_tx_ok_cnt = *dm->num_tx_bytes_unicast;
	dm->last_rx_ok_cnt = *dm->num_rx_bytes_unicast;

	dm->tx_tp = ((dm->tx_tp) >> 1) +
		    (u32)(((dm->cur_tx_ok_cnt) >> 18) >>
			  1); /* <<3(8bit), >>20(10^6,M), >>1(2sec)*/
	dm->rx_tp = ((dm->rx_tp) >> 1) +
		    (u32)(((dm->cur_rx_ok_cnt) >> 18) >>
			  1); /* <<3(8bit), >>20(10^6,M), >>1(2sec)*/
	dm->total_tp = dm->tx_tp + dm->rx_tp;

	dm->pre_traffic_load = dm->traffic_load;

	if (dm->cur_tx_ok_cnt > 1875000 ||
	    dm->cur_rx_ok_cnt >
		    1875000) { /* ( 1.875M * 8bit ) / 2sec= 7.5M bits /sec )*/

		dm->traffic_load = TRAFFIC_HIGH;
		/**/
	} else if (
		dm->cur_tx_ok_cnt > 500000 ||
		dm->cur_rx_ok_cnt >
			500000) { /*( 0.5M * 8bit ) / 2sec =  2M bits /sec )*/

		dm->traffic_load = TRAFFIC_MID;
		/**/
	} else if (
		dm->cur_tx_ok_cnt > 100000 ||
		dm->cur_rx_ok_cnt >
			100000) { /*( 0.1M * 8bit ) / 2sec =  0.4M bits /sec )*/

		dm->traffic_load = TRAFFIC_LOW;
		/**/
	} else {
		dm->traffic_load = TRAFFIC_ULTRA_LOW;
		/**/
	}
}

static void phydm_config_ofdm_tx_path(struct phy_dm_struct *dm, u32 path) {}

void phydm_config_ofdm_rx_path(struct phy_dm_struct *dm, u32 path)
{
	u8 ofdm_rx_path = 0;

	if (dm->support_ic_type & (ODM_RTL8192E)) {
	} else if (dm->support_ic_type & (ODM_RTL8812 | ODM_RTL8822B)) {
		if (path == PHYDM_A) {
			ofdm_rx_path = 1;
			/**/
		} else if (path == PHYDM_B) {
			ofdm_rx_path = 2;
			/**/
		} else if (path == PHYDM_AB) {
			ofdm_rx_path = 3;
			/**/
		}

		odm_set_bb_reg(dm, 0x808, MASKBYTE0,
			       ((ofdm_rx_path << 4) | ofdm_rx_path));
	}
}

static void phydm_config_cck_rx_antenna_init(struct phy_dm_struct *dm) {}

static void phydm_config_cck_rx_path(struct phy_dm_struct *dm, u8 path,
				     u8 path_div_en)
{
}

void phydm_config_trx_path(void *dm_void, u32 *const dm_value, u32 *_used,
			   char *output, u32 *_out_len)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;

	/* CCK */
	if (dm_value[0] == 0) {
		if (dm_value[1] == 1) { /*TX*/
			if (dm_value[2] == 1)
				odm_set_bb_reg(dm, 0xa04, 0xf0000000, 0x8);
			else if (dm_value[2] == 2)
				odm_set_bb_reg(dm, 0xa04, 0xf0000000, 0x4);
			else if (dm_value[2] == 3)
				odm_set_bb_reg(dm, 0xa04, 0xf0000000, 0xc);
		} else if (dm_value[1] == 2) { /*RX*/

			phydm_config_cck_rx_antenna_init(dm);

			if (dm_value[2] == 1)
				phydm_config_cck_rx_path(dm, PHYDM_A,
							 CCA_PATHDIV_DISABLE);
			else if (dm_value[2] == 2)
				phydm_config_cck_rx_path(dm, PHYDM_B,
							 CCA_PATHDIV_DISABLE);
			else if (dm_value[2] == 3 &&
				 dm_value[3] == 1) /*enable path diversity*/
				phydm_config_cck_rx_path(dm, PHYDM_AB,
							 CCA_PATHDIV_ENABLE);
			else if (dm_value[2] == 3 && dm_value[3] != 1)
				phydm_config_cck_rx_path(dm, PHYDM_B,
							 CCA_PATHDIV_DISABLE);
		}
	}
	/* OFDM */
	else if (dm_value[0] == 1) {
		if (dm_value[1] == 1) { /*TX*/
			phydm_config_ofdm_tx_path(dm, dm_value[2]);
			/**/
		} else if (dm_value[1] == 2) { /*RX*/
			phydm_config_ofdm_rx_path(dm, dm_value[2]);
			/**/
		}
	}

	PHYDM_SNPRINTF(
		output + used, out_len - used,
		"PHYDM Set path [%s] [%s] = [%s%s%s%s]\n",
		(dm_value[0] == 1) ? "OFDM" : "CCK",
		(dm_value[1] == 1) ? "TX" : "RX",
		(dm_value[2] & 0x1) ? "A" : "", (dm_value[2] & 0x2) ? "B" : "",
		(dm_value[2] & 0x4) ? "C" : "", (dm_value[2] & 0x8) ? "D" : "");
}

static void phydm_init_cck_setting(struct phy_dm_struct *dm)
{
	dm->is_cck_high_power = (bool)odm_get_bb_reg(
		dm, ODM_REG(CCK_RPT_FORMAT, dm), ODM_BIT(CCK_RPT_FORMAT, dm));

	/* JJ ADD 20161014 */
	/* JJ ADD 20161014 */
	if (dm->support_ic_type & (ODM_RTL8723D | ODM_RTL8822B | ODM_RTL8197F |
				   ODM_RTL8821C | ODM_RTL8710B))
		dm->cck_new_agc = odm_get_bb_reg(dm, 0xa9c, BIT(17)) ?
					  true :
					  false; /*1: new agc  0: old agc*/
	else
		dm->cck_new_agc = false;
}

static void phydm_init_soft_ml_setting(struct phy_dm_struct *dm)
{
	if (!dm->mp_mode) {
		if (dm->support_ic_type & ODM_RTL8822B)
			odm_set_bb_reg(dm, 0x19a8, MASKDWORD, 0xc10a0000);
	}
}

static void phydm_init_hw_info_by_rfe(struct phy_dm_struct *dm)
{
	if (dm->support_ic_type & ODM_RTL8822B)
		phydm_init_hw_info_by_rfe_type_8822b(dm);
}

static void odm_common_info_self_init(struct phy_dm_struct *dm)
{
	phydm_init_cck_setting(dm);
	dm->rf_path_rx_enable = (u8)odm_get_bb_reg(dm, ODM_REG(BB_RX_PATH, dm),
						   ODM_BIT(BB_RX_PATH, dm));
	odm_init_mp_driver_status(dm);
	phydm_init_trx_antenna_setting(dm);
	phydm_init_soft_ml_setting(dm);

	dm->phydm_period = PHYDM_WATCH_DOG_PERIOD;
	dm->phydm_sys_up_time = 0;

	if (dm->support_ic_type & ODM_IC_1SS)
		dm->num_rf_path = 1;
	else if (dm->support_ic_type & ODM_IC_2SS)
		dm->num_rf_path = 2;
	else if (dm->support_ic_type & ODM_IC_3SS)
		dm->num_rf_path = 3;
	else if (dm->support_ic_type & ODM_IC_4SS)
		dm->num_rf_path = 4;

	dm->tx_rate = 0xFF;

	dm->number_linked_client = 0;
	dm->pre_number_linked_client = 0;
	dm->number_active_client = 0;
	dm->pre_number_active_client = 0;

	dm->last_tx_ok_cnt = 0;
	dm->last_rx_ok_cnt = 0;
	dm->tx_tp = 0;
	dm->rx_tp = 0;
	dm->total_tp = 0;
	dm->traffic_load = TRAFFIC_LOW;

	dm->nbi_set_result = 0;
	dm->is_init_hw_info_by_rfe = false;
	dm->pre_dbg_priority = BB_DBGPORT_RELEASE;
}

static void odm_common_info_self_update(struct phy_dm_struct *dm)
{
	u8 entry_cnt = 0, num_active_client = 0;
	u32 i, one_entry_macid = 0;
	struct rtl_sta_info *entry;

	/* THis variable cannot be used because it is wrong*/
	if (*dm->band_width == ODM_BW40M) {
		if (*dm->sec_ch_offset == 1)
			dm->control_channel = *dm->channel - 2;
		else if (*dm->sec_ch_offset == 2)
			dm->control_channel = *dm->channel + 2;
	} else {
		dm->control_channel = *dm->channel;
	}

	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
		entry = dm->odm_sta_info[i];
		if (IS_STA_VALID(entry)) {
			entry_cnt++;
			if (entry_cnt == 1)
				one_entry_macid = i;
		}
	}

	if (entry_cnt == 1) {
		dm->is_one_entry_only = true;
		dm->one_entry_macid = one_entry_macid;
	} else {
		dm->is_one_entry_only = false;
	}

	dm->pre_number_linked_client = dm->number_linked_client;
	dm->pre_number_active_client = dm->number_active_client;

	dm->number_linked_client = entry_cnt;
	dm->number_active_client = num_active_client;

	/* Update MP driver status*/
	odm_update_mp_driver_status(dm);

	/*Traffic load information update*/
	phydm_traffic_load_decision(dm);

	dm->phydm_sys_up_time += dm->phydm_period;
}

static void odm_common_info_self_reset(struct phy_dm_struct *dm)
{
	dm->phy_dbg_info.num_qry_beacon_pkt = 0;
}

void *phydm_get_structure(struct phy_dm_struct *dm, u8 structure_type)

{
	void *p_struct = NULL;

	switch (structure_type) {
	case PHYDM_FALSEALMCNT:
		p_struct = &dm->false_alm_cnt;
		break;

	case PHYDM_CFOTRACK:
		p_struct = &dm->dm_cfo_track;
		break;

	case PHYDM_ADAPTIVITY:
		p_struct = &dm->adaptivity;
		break;

	default:
		break;
	}

	return p_struct;
}

static void odm_hw_setting(struct phy_dm_struct *dm)
{
	if (dm->support_ic_type & ODM_RTL8822B)
		phydm_hwsetting_8822b(dm);
}

static void phydm_supportability_init(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u32 support_ability = 0;

	if (dm->support_ic_type != ODM_RTL8821C)
		return;

	switch (dm->support_ic_type) {
	/*---------------AC Series-------------------*/

	case ODM_RTL8822B:
		support_ability |= ODM_BB_DIG | ODM_BB_FA_CNT | ODM_BB_CCK_PD |
				   ODM_BB_CFO_TRACKING | ODM_BB_RATE_ADAPTIVE |
				   ODM_BB_RSSI_MONITOR | ODM_BB_RA_MASK |
				   ODM_RF_TX_PWR_TRACK;
		break;

	default:
		support_ability |= ODM_BB_DIG | ODM_BB_FA_CNT | ODM_BB_CCK_PD |
				   ODM_BB_CFO_TRACKING | ODM_BB_RATE_ADAPTIVE |
				   ODM_BB_RSSI_MONITOR | ODM_BB_RA_MASK |
				   ODM_RF_TX_PWR_TRACK;

		ODM_RT_TRACE(dm, ODM_COMP_UNCOND,
			     "[Warning] Supportability Init Warning !!!\n");
		break;
	}

	if (*dm->enable_antdiv)
		support_ability |= ODM_BB_ANT_DIV;

	if (*dm->enable_adaptivity) {
		ODM_RT_TRACE(dm, ODM_COMP_INIT,
			     "ODM adaptivity is set to Enabled!!!\n");

		support_ability |= ODM_BB_ADAPTIVITY;

	} else {
		ODM_RT_TRACE(dm, ODM_COMP_INIT,
			     "ODM adaptivity is set to disabled!!!\n");
		/**/
	}

	ODM_RT_TRACE(dm, ODM_COMP_INIT, "PHYDM support_ability = ((0x%x))\n",
		     support_ability);
	odm_cmn_info_init(dm, ODM_CMNINFO_ABILITY, support_ability);
}

/*
 * 2011/09/21 MH Add to describe different team necessary resource allocate??
 */
void odm_dm_init(struct phy_dm_struct *dm)
{
	phydm_supportability_init(dm);
	odm_common_info_self_init(dm);
	odm_dig_init(dm);
	phydm_nhm_counter_statistics_init(dm);
	phydm_adaptivity_init(dm);
	phydm_ra_info_init(dm);
	odm_rate_adaptive_mask_init(dm);
	odm_cfo_tracking_init(dm);
	odm_edca_turbo_init(dm);
	odm_rssi_monitor_init(dm);
	phydm_rf_init(dm);
	odm_txpowertracking_init(dm);

	if (dm->support_ic_type & ODM_RTL8822B)
		phydm_txcurrentcalibration(dm);

	odm_antenna_diversity_init(dm);
	odm_auto_channel_select_init(dm);
	odm_dynamic_tx_power_init(dm);
	phydm_init_ra_info(dm);
	adc_smp_init(dm);

	phydm_beamforming_init(dm);

	if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		/* 11n series */
		odm_dynamic_bb_power_saving_init(dm);
	}

	phydm_psd_init(dm);
}

void odm_dm_reset(struct phy_dm_struct *dm)
{
	struct dig_thres *dig_tab = &dm->dm_dig_table;

	odm_ant_div_reset(dm);
	phydm_set_edcca_threshold_api(dm, dig_tab->cur_ig_value);
}

void phydm_support_ability_debug(void *dm_void, u32 *const dm_value, u32 *_used,
				 char *output, u32 *_out_len)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u32 pre_support_ability;
	u32 used = *_used;
	u32 out_len = *_out_len;

	pre_support_ability = dm->support_ability;
	PHYDM_SNPRINTF(output + used, out_len - used, "\n%s\n",
		       "================================");
	if (dm_value[0] == 100) {
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "[Supportability] PhyDM Selection\n");
		PHYDM_SNPRINTF(output + used, out_len - used, "%s\n",
			       "================================");
		PHYDM_SNPRINTF(
			output + used, out_len - used, "00. (( %s ))DIG\n",
			((dm->support_ability & ODM_BB_DIG) ? ("V") : (".")));
		PHYDM_SNPRINTF(
			output + used, out_len - used, "01. (( %s ))RA_MASK\n",
			((dm->support_ability & ODM_BB_RA_MASK) ? ("V") :
								  (".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "02. (( %s ))DYNAMIC_TXPWR\n",
			       ((dm->support_ability & ODM_BB_DYNAMIC_TXPWR) ?
					("V") :
					(".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "03. (( %s ))FA_CNT\n",
			       ((dm->support_ability & ODM_BB_FA_CNT) ? ("V") :
									(".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "04. (( %s ))RSSI_MONITOR\n",
			       ((dm->support_ability & ODM_BB_RSSI_MONITOR) ?
					("V") :
					(".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "05. (( %s ))CCK_PD\n",
			       ((dm->support_ability & ODM_BB_CCK_PD) ? ("V") :
									(".")));
		PHYDM_SNPRINTF(
			output + used, out_len - used, "06. (( %s ))ANT_DIV\n",
			((dm->support_ability & ODM_BB_ANT_DIV) ? ("V") :
								  (".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "08. (( %s ))PWR_TRAIN\n",
			       ((dm->support_ability & ODM_BB_PWR_TRAIN) ?
					("V") :
					(".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "09. (( %s ))RATE_ADAPTIVE\n",
			       ((dm->support_ability & ODM_BB_RATE_ADAPTIVE) ?
					("V") :
					(".")));
		PHYDM_SNPRINTF(
			output + used, out_len - used, "10. (( %s ))PATH_DIV\n",
			((dm->support_ability & ODM_BB_PATH_DIV) ? ("V") :
								   (".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "13. (( %s ))ADAPTIVITY\n",
			       ((dm->support_ability & ODM_BB_ADAPTIVITY) ?
					("V") :
					(".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "14. (( %s ))struct cfo_tracking\n",
			       ((dm->support_ability & ODM_BB_CFO_TRACKING) ?
					("V") :
					(".")));
		PHYDM_SNPRINTF(
			output + used, out_len - used, "15. (( %s ))NHM_CNT\n",
			((dm->support_ability & ODM_BB_NHM_CNT) ? ("V") :
								  (".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "16. (( %s ))PRIMARY_CCA\n",
			       ((dm->support_ability & ODM_BB_PRIMARY_CCA) ?
					("V") :
					(".")));
		PHYDM_SNPRINTF(
			output + used, out_len - used, "17. (( %s ))TXBF\n",
			((dm->support_ability & ODM_BB_TXBF) ? ("V") : (".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "18. (( %s ))DYNAMIC_ARFR\n",
			       ((dm->support_ability & ODM_BB_DYNAMIC_ARFR) ?
					("V") :
					(".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "20. (( %s ))EDCA_TURBO\n",
			       ((dm->support_ability & ODM_MAC_EDCA_TURBO) ?
					("V") :
					(".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "21. (( %s ))DYNAMIC_RX_PATH\n",
			       ((dm->support_ability & ODM_BB_DYNAMIC_RX_PATH) ?
					("V") :
					(".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "24. (( %s ))TX_PWR_TRACK\n",
			       ((dm->support_ability & ODM_RF_TX_PWR_TRACK) ?
					("V") :
					(".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "25. (( %s ))RX_GAIN_TRACK\n",
			       ((dm->support_ability & ODM_RF_RX_GAIN_TRACK) ?
					("V") :
					(".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "26. (( %s ))RF_CALIBRATION\n",
			       ((dm->support_ability & ODM_RF_CALIBRATION) ?
					("V") :
					(".")));
		PHYDM_SNPRINTF(output + used, out_len - used, "%s\n",
			       "================================");
	} else {
		if (dm_value[1] == 1) { /* enable */
			dm->support_ability |= BIT(dm_value[0]);
		} else if (dm_value[1] == 2) /* disable */
			dm->support_ability &= ~(BIT(dm_value[0]));
		else {
			PHYDM_SNPRINTF(output + used, out_len - used, "%s\n",
				       "[Warning!!!]  1:enable,  2:disable");
		}
	}
	PHYDM_SNPRINTF(output + used, out_len - used,
		       "pre-support_ability  =  0x%x\n", pre_support_ability);
	PHYDM_SNPRINTF(output + used, out_len - used,
		       "Curr-support_ability =  0x%x\n", dm->support_ability);
	PHYDM_SNPRINTF(output + used, out_len - used, "%s\n",
		       "================================");
}

void phydm_watchdog_mp(struct phy_dm_struct *dm) {}
/*
 * 2011/09/20 MH This is the entry pointer for all team to execute HW outsrc DM.
 * You can not add any dummy function here, be care, you can only use DM struct
 * to perform any new ODM_DM.
 */
void odm_dm_watchdog(struct phy_dm_struct *dm)
{
	odm_common_info_self_update(dm);
	phydm_basic_dbg_message(dm);
	odm_hw_setting(dm);

	odm_false_alarm_counter_statistics(dm);
	phydm_noisy_detection(dm);

	odm_rssi_monitor_check(dm);

	if (*dm->is_power_saving) {
		odm_dig_by_rssi_lps(dm);
		phydm_adaptivity(dm);
		odm_antenna_diversity(
			dm); /*enable AntDiv in PS mode, request from SD4 Jeff*/
		ODM_RT_TRACE(dm, ODM_COMP_COMMON,
			     "DMWatchdog in power saving mode\n");
		return;
	}

	phydm_check_adaptivity(dm);
	odm_update_power_training_state(dm);
	odm_DIG(dm);
	phydm_adaptivity(dm);
	odm_cck_packet_detection_thresh(dm);

	phydm_ra_info_watchdog(dm);
	odm_edca_turbo_check(dm);
	odm_cfo_tracking(dm);
	odm_dynamic_tx_power(dm);
	odm_antenna_diversity(dm);

	phydm_beamforming_watchdog(dm);

	phydm_rf_watchdog(dm);

	odm_dtc(dm);

	odm_common_info_self_reset(dm);
}

/*
 * Init /.. Fixed HW value. Only init time.
 */
void odm_cmn_info_init(struct phy_dm_struct *dm, enum odm_cmninfo cmn_info,
		       u32 value)
{
	/* This section is used for init value */
	switch (cmn_info) {
	/* Fixed ODM value. */
	case ODM_CMNINFO_ABILITY:
		dm->support_ability = (u32)value;
		break;

	case ODM_CMNINFO_RF_TYPE:
		dm->rf_type = (u8)value;
		break;

	case ODM_CMNINFO_PLATFORM:
		dm->support_platform = (u8)value;
		break;

	case ODM_CMNINFO_INTERFACE:
		dm->support_interface = (u8)value;
		break;

	case ODM_CMNINFO_MP_TEST_CHIP:
		dm->is_mp_chip = (u8)value;
		break;

	case ODM_CMNINFO_IC_TYPE:
		dm->support_ic_type = value;
		break;

	case ODM_CMNINFO_CUT_VER:
		dm->cut_version = (u8)value;
		break;

	case ODM_CMNINFO_FAB_VER:
		dm->fab_version = (u8)value;
		break;

	case ODM_CMNINFO_RFE_TYPE:
		dm->rfe_type = (u8)value;
		phydm_init_hw_info_by_rfe(dm);
		break;

	case ODM_CMNINFO_RF_ANTENNA_TYPE:
		dm->ant_div_type = (u8)value;
		break;

	case ODM_CMNINFO_WITH_EXT_ANTENNA_SWITCH:
		dm->with_extenal_ant_switch = (u8)value;
		break;

	case ODM_CMNINFO_BE_FIX_TX_ANT:
		dm->dm_fat_table.b_fix_tx_ant = (u8)value;
		break;

	case ODM_CMNINFO_BOARD_TYPE:
		if (!dm->is_init_hw_info_by_rfe)
			dm->board_type = (u8)value;
		break;

	case ODM_CMNINFO_PACKAGE_TYPE:
		if (!dm->is_init_hw_info_by_rfe)
			dm->package_type = (u8)value;
		break;

	case ODM_CMNINFO_EXT_LNA:
		if (!dm->is_init_hw_info_by_rfe)
			dm->ext_lna = (u8)value;
		break;

	case ODM_CMNINFO_5G_EXT_LNA:
		if (!dm->is_init_hw_info_by_rfe)
			dm->ext_lna_5g = (u8)value;
		break;

	case ODM_CMNINFO_EXT_PA:
		if (!dm->is_init_hw_info_by_rfe)
			dm->ext_pa = (u8)value;
		break;

	case ODM_CMNINFO_5G_EXT_PA:
		if (!dm->is_init_hw_info_by_rfe)
			dm->ext_pa_5g = (u8)value;
		break;

	case ODM_CMNINFO_GPA:
		if (!dm->is_init_hw_info_by_rfe)
			dm->type_gpa = (u16)value;
		break;

	case ODM_CMNINFO_APA:
		if (!dm->is_init_hw_info_by_rfe)
			dm->type_apa = (u16)value;
		break;

	case ODM_CMNINFO_GLNA:
		if (!dm->is_init_hw_info_by_rfe)
			dm->type_glna = (u16)value;
		break;

	case ODM_CMNINFO_ALNA:
		if (!dm->is_init_hw_info_by_rfe)
			dm->type_alna = (u16)value;
		break;

	case ODM_CMNINFO_EXT_TRSW:
		if (!dm->is_init_hw_info_by_rfe)
			dm->ext_trsw = (u8)value;
		break;
	case ODM_CMNINFO_EXT_LNA_GAIN:
		dm->ext_lna_gain = (u8)value;
		break;
	case ODM_CMNINFO_PATCH_ID:
		dm->patch_id = (u8)value;
		break;
	case ODM_CMNINFO_BINHCT_TEST:
		dm->is_in_hct_test = (bool)value;
		break;
	case ODM_CMNINFO_BWIFI_TEST:
		dm->wifi_test = (u8)value;
		break;
	case ODM_CMNINFO_SMART_CONCURRENT:
		dm->is_dual_mac_smart_concurrent = (bool)value;
		break;
	case ODM_CMNINFO_DOMAIN_CODE_2G:
		dm->odm_regulation_2_4g = (u8)value;
		break;
	case ODM_CMNINFO_DOMAIN_CODE_5G:
		dm->odm_regulation_5g = (u8)value;
		break;
	case ODM_CMNINFO_CONFIG_BB_RF:
		dm->config_bbrf = (bool)value;
		break;
	case ODM_CMNINFO_IQKFWOFFLOAD:
		dm->iqk_fw_offload = (u8)value;
		break;
	case ODM_CMNINFO_IQKPAOFF:
		dm->rf_calibrate_info.is_iqk_pa_off = (bool)value;
		break;
	case ODM_CMNINFO_REGRFKFREEENABLE:
		dm->rf_calibrate_info.reg_rf_kfree_enable = (u8)value;
		break;
	case ODM_CMNINFO_RFKFREEENABLE:
		dm->rf_calibrate_info.rf_kfree_enable = (u8)value;
		break;
	case ODM_CMNINFO_NORMAL_RX_PATH_CHANGE:
		dm->normal_rx_path = (u8)value;
		break;
	case ODM_CMNINFO_EFUSE0X3D8:
		dm->efuse0x3d8 = (u8)value;
		break;
	case ODM_CMNINFO_EFUSE0X3D7:
		dm->efuse0x3d7 = (u8)value;
		break;
	/* To remove the compiler warning, must add an empty default statement
	 * to handle the other values.
	 */
	default:
		/* do nothing */
		break;
	}
}

void odm_cmn_info_hook(struct phy_dm_struct *dm, enum odm_cmninfo cmn_info,
		       void *value)
{
	/*  */
	/* Hook call by reference pointer. */
	/*  */
	switch (cmn_info) {
	/*  */
	/* Dynamic call by reference pointer. */
	/*  */
	case ODM_CMNINFO_MAC_PHY_MODE:
		dm->mac_phy_mode = (u8 *)value;
		break;

	case ODM_CMNINFO_TX_UNI:
		dm->num_tx_bytes_unicast = (u64 *)value;
		break;

	case ODM_CMNINFO_RX_UNI:
		dm->num_rx_bytes_unicast = (u64 *)value;
		break;

	case ODM_CMNINFO_WM_MODE:
		dm->wireless_mode = (u8 *)value;
		break;

	case ODM_CMNINFO_BAND:
		dm->band_type = (u8 *)value;
		break;

	case ODM_CMNINFO_SEC_CHNL_OFFSET:
		dm->sec_ch_offset = (u8 *)value;
		break;

	case ODM_CMNINFO_SEC_MODE:
		dm->security = (u8 *)value;
		break;

	case ODM_CMNINFO_BW:
		dm->band_width = (u8 *)value;
		break;

	case ODM_CMNINFO_CHNL:
		dm->channel = (u8 *)value;
		break;

	case ODM_CMNINFO_DMSP_GET_VALUE:
		dm->is_get_value_from_other_mac = (bool *)value;
		break;

	case ODM_CMNINFO_BUDDY_ADAPTOR:
		dm->buddy_adapter = (void **)value;
		break;

	case ODM_CMNINFO_DMSP_IS_MASTER:
		dm->is_master_of_dmsp = (bool *)value;
		break;

	case ODM_CMNINFO_SCAN:
		dm->is_scan_in_process = (bool *)value;
		break;

	case ODM_CMNINFO_POWER_SAVING:
		dm->is_power_saving = (bool *)value;
		break;

	case ODM_CMNINFO_ONE_PATH_CCA:
		dm->one_path_cca = (u8 *)value;
		break;

	case ODM_CMNINFO_DRV_STOP:
		dm->is_driver_stopped = (bool *)value;
		break;

	case ODM_CMNINFO_PNP_IN:
		dm->is_driver_is_going_to_pnp_set_power_sleep = (bool *)value;
		break;

	case ODM_CMNINFO_INIT_ON:
		dm->pinit_adpt_in_progress = (bool *)value;
		break;

	case ODM_CMNINFO_ANT_TEST:
		dm->antenna_test = (u8 *)value;
		break;

	case ODM_CMNINFO_NET_CLOSED:
		dm->is_net_closed = (bool *)value;
		break;

	case ODM_CMNINFO_FORCED_RATE:
		dm->forced_data_rate = (u16 *)value;
		break;
	case ODM_CMNINFO_ANT_DIV:
		dm->enable_antdiv = (u8 *)value;
		break;
	case ODM_CMNINFO_ADAPTIVITY:
		dm->enable_adaptivity = (u8 *)value;
		break;
	case ODM_CMNINFO_FORCED_IGI_LB:
		dm->pu1_forced_igi_lb = (u8 *)value;
		break;

	case ODM_CMNINFO_P2P_LINK:
		dm->dm_dig_table.is_p2p_in_process = (u8 *)value;
		break;

	case ODM_CMNINFO_IS1ANTENNA:
		dm->is_1_antenna = (bool *)value;
		break;

	case ODM_CMNINFO_RFDEFAULTPATH:
		dm->rf_default_path = (u8 *)value;
		break;

	case ODM_CMNINFO_FCS_MODE:
		dm->is_fcs_mode_enable = (bool *)value;
		break;
	/*add by YuChen for beamforming PhyDM*/
	case ODM_CMNINFO_HUBUSBMODE:
		dm->hub_usb_mode = (u8 *)value;
		break;
	case ODM_CMNINFO_FWDWRSVDPAGEINPROGRESS:
		dm->is_fw_dw_rsvd_page_in_progress = (bool *)value;
		break;
	case ODM_CMNINFO_TX_TP:
		dm->current_tx_tp = (u32 *)value;
		break;
	case ODM_CMNINFO_RX_TP:
		dm->current_rx_tp = (u32 *)value;
		break;
	case ODM_CMNINFO_SOUNDING_SEQ:
		dm->sounding_seq = (u8 *)value;
		break;
	case ODM_CMNINFO_FORCE_TX_ANT_BY_TXDESC:
		dm->dm_fat_table.p_force_tx_ant_by_desc = (u8 *)value;
		break;
	case ODM_CMNINFO_SET_S0S1_DEFAULT_ANTENNA:
		dm->dm_fat_table.p_default_s0_s1 = (u8 *)value;
		break;

	default:
		/*do nothing*/
		break;
	}
}

void odm_cmn_info_ptr_array_hook(struct phy_dm_struct *dm,
				 enum odm_cmninfo cmn_info, u16 index,
				 void *value)
{
	/*Hook call by reference pointer.*/
	switch (cmn_info) {
	/*Dynamic call by reference pointer.	*/
	case ODM_CMNINFO_STA_STATUS:
		dm->odm_sta_info[index] = (struct rtl_sta_info *)value;

		if (IS_STA_VALID(dm->odm_sta_info[index]))
			dm->platform2phydm_macid_table[index] = index;

		break;
	/* To remove the compiler warning, must add an empty default statement
	 * to handle the other values.
	 */
	default:
		/* do nothing */
		break;
	}
}

/*
 * Update band/CHannel/.. The values are dynamic but non-per-packet.
 */
void odm_cmn_info_update(struct phy_dm_struct *dm, u32 cmn_info, u64 value)
{
	/* This init variable may be changed in run time. */
	switch (cmn_info) {
	case ODM_CMNINFO_LINK_IN_PROGRESS:
		dm->is_link_in_process = (bool)value;
		break;

	case ODM_CMNINFO_ABILITY:
		dm->support_ability = (u32)value;
		break;

	case ODM_CMNINFO_RF_TYPE:
		dm->rf_type = (u8)value;
		break;

	case ODM_CMNINFO_WIFI_DIRECT:
		dm->is_wifi_direct = (bool)value;
		break;

	case ODM_CMNINFO_WIFI_DISPLAY:
		dm->is_wifi_display = (bool)value;
		break;

	case ODM_CMNINFO_LINK:
		dm->is_linked = (bool)value;
		break;

	case ODM_CMNINFO_CMW500LINK:
		dm->is_linkedcmw500 = (bool)value;
		break;

	case ODM_CMNINFO_LPSPG:
		dm->is_in_lps_pg = (bool)value;
		break;

	case ODM_CMNINFO_STATION_STATE:
		dm->bsta_state = (bool)value;
		break;

	case ODM_CMNINFO_RSSI_MIN:
		dm->rssi_min = (u8)value;
		break;

	case ODM_CMNINFO_DBG_COMP:
		dm->debug_components = (u32)value;
		break;

	case ODM_CMNINFO_DBG_LEVEL:
		dm->debug_level = (u32)value;
		break;
	case ODM_CMNINFO_RA_THRESHOLD_HIGH:
		dm->rate_adaptive.high_rssi_thresh = (u8)value;
		break;

	case ODM_CMNINFO_RA_THRESHOLD_LOW:
		dm->rate_adaptive.low_rssi_thresh = (u8)value;
		break;
	/* The following is for BT HS mode and BT coexist mechanism. */
	case ODM_CMNINFO_BT_ENABLED:
		dm->is_bt_enabled = (bool)value;
		break;

	case ODM_CMNINFO_BT_HS_CONNECT_PROCESS:
		dm->is_bt_connect_process = (bool)value;
		break;

	case ODM_CMNINFO_BT_HS_RSSI:
		dm->bt_hs_rssi = (u8)value;
		break;

	case ODM_CMNINFO_BT_OPERATION:
		dm->is_bt_hs_operation = (bool)value;
		break;

	case ODM_CMNINFO_BT_LIMITED_DIG:
		dm->is_bt_limited_dig = (bool)value;
		break;

	case ODM_CMNINFO_BT_DIG:
		dm->bt_hs_dig_val = (u8)value;
		break;

	case ODM_CMNINFO_BT_BUSY:
		dm->is_bt_busy = (bool)value;
		break;

	case ODM_CMNINFO_BT_DISABLE_EDCA:
		dm->is_bt_disable_edca_turbo = (bool)value;
		break;

	case ODM_CMNINFO_AP_TOTAL_NUM:
		dm->ap_total_num = (u8)value;
		break;

	case ODM_CMNINFO_POWER_TRAINING:
		dm->is_disable_power_training = (bool)value;
		break;

	default:
		/* do nothing */
		break;
	}
}

u32 phydm_cmn_info_query(struct phy_dm_struct *dm,
			 enum phydm_info_query info_type)
{
	struct false_alarm_stat *false_alm_cnt =
		(struct false_alarm_stat *)phydm_get_structure(
			dm, PHYDM_FALSEALMCNT);

	switch (info_type) {
	case PHYDM_INFO_FA_OFDM:
		return false_alm_cnt->cnt_ofdm_fail;

	case PHYDM_INFO_FA_CCK:
		return false_alm_cnt->cnt_cck_fail;

	case PHYDM_INFO_FA_TOTAL:
		return false_alm_cnt->cnt_all;

	case PHYDM_INFO_CCA_OFDM:
		return false_alm_cnt->cnt_ofdm_cca;

	case PHYDM_INFO_CCA_CCK:
		return false_alm_cnt->cnt_cck_cca;

	case PHYDM_INFO_CCA_ALL:
		return false_alm_cnt->cnt_cca_all;

	case PHYDM_INFO_CRC32_OK_VHT:
		return false_alm_cnt->cnt_vht_crc32_ok;

	case PHYDM_INFO_CRC32_OK_HT:
		return false_alm_cnt->cnt_ht_crc32_ok;

	case PHYDM_INFO_CRC32_OK_LEGACY:
		return false_alm_cnt->cnt_ofdm_crc32_ok;

	case PHYDM_INFO_CRC32_OK_CCK:
		return false_alm_cnt->cnt_cck_crc32_ok;

	case PHYDM_INFO_CRC32_ERROR_VHT:
		return false_alm_cnt->cnt_vht_crc32_error;

	case PHYDM_INFO_CRC32_ERROR_HT:
		return false_alm_cnt->cnt_ht_crc32_error;

	case PHYDM_INFO_CRC32_ERROR_LEGACY:
		return false_alm_cnt->cnt_ofdm_crc32_error;

	case PHYDM_INFO_CRC32_ERROR_CCK:
		return false_alm_cnt->cnt_cck_crc32_error;

	case PHYDM_INFO_EDCCA_FLAG:
		return false_alm_cnt->edcca_flag;

	case PHYDM_INFO_OFDM_ENABLE:
		return false_alm_cnt->ofdm_block_enable;

	case PHYDM_INFO_CCK_ENABLE:
		return false_alm_cnt->cck_block_enable;

	case PHYDM_INFO_DBG_PORT_0:
		return false_alm_cnt->dbg_port0;

	default:
		return 0xffffffff;
	}
}

void odm_init_all_timers(struct phy_dm_struct *dm) {}

void odm_cancel_all_timers(struct phy_dm_struct *dm) {}

void odm_release_all_timers(struct phy_dm_struct *dm) {}

/* 3============================================================
 * 3 Tx Power Tracking
 * 3============================================================
 */

/* need to ODM CE Platform
 * move to here for ANT detection mechanism using
 */

u32 odm_convert_to_db(u32 value)
{
	u8 i;
	u8 j;

	value = value & 0xFFFF;

	for (i = 0; i < 12; i++) {
		if (value <= db_invert_table[i][7])
			break;
	}

	if (i >= 12)
		return 96; /* maximum 96 dB */

	for (j = 0; j < 8; j++) {
		if (value <= db_invert_table[i][j])
			break;
	}

	return (i << 3) + j + 1;
}

u32 odm_convert_to_linear(u32 value)
{
	u8 i;
	u8 j;

	/* 1dB~96dB */

	value = value & 0xFF;

	i = (u8)((value - 1) >> 3);
	j = (u8)(value - 1) - (i << 3);

	return db_invert_table[i][j];
}

/*
 * ODM multi-port consideration, added by Roger, 2013.10.01.
 */
void odm_asoc_entry_init(struct phy_dm_struct *dm) {}

/* Justin: According to the current RRSI to adjust Response Frame TX power */
void odm_dtc(struct phy_dm_struct *dm) {}

static void odm_update_power_training_state(struct phy_dm_struct *dm)
{
	struct false_alarm_stat *false_alm_cnt =
		(struct false_alarm_stat *)phydm_get_structure(
			dm, PHYDM_FALSEALMCNT);
	struct dig_thres *dig_tab = &dm->dm_dig_table;
	u32 score = 0;

	if (!(dm->support_ability & ODM_BB_PWR_TRAIN))
		return;

	ODM_RT_TRACE(dm, ODM_COMP_RA_MASK, "%s()============>\n", __func__);
	dm->is_change_state = false;

	/* Debug command */
	if (dm->force_power_training_state) {
		if (dm->force_power_training_state == 1 &&
		    !dm->is_disable_power_training) {
			dm->is_change_state = true;
			dm->is_disable_power_training = true;
		} else if (dm->force_power_training_state == 2 &&
			   dm->is_disable_power_training) {
			dm->is_change_state = true;
			dm->is_disable_power_training = false;
		}

		dm->PT_score = 0;
		dm->phy_dbg_info.num_qry_phy_status_ofdm = 0;
		dm->phy_dbg_info.num_qry_phy_status_cck = 0;
		ODM_RT_TRACE(dm, ODM_COMP_RA_MASK,
			     "%s(): force_power_training_state = %d\n",
			     __func__, dm->force_power_training_state);
		return;
	}

	if (!dm->is_linked)
		return;

	/* First connect */
	if (dm->is_linked && !dig_tab->is_media_connect_0) {
		dm->PT_score = 0;
		dm->is_change_state = true;
		dm->phy_dbg_info.num_qry_phy_status_ofdm = 0;
		dm->phy_dbg_info.num_qry_phy_status_cck = 0;
		ODM_RT_TRACE(dm, ODM_COMP_RA_MASK, "%s(): First Connect\n",
			     __func__);
		return;
	}

	/* Compute score */
	if (dm->nhm_cnt_0 >= 215) {
		score = 2;
	} else if (dm->nhm_cnt_0 >= 190) {
		score = 1; /* unknown state */
	} else {
		u32 rx_pkt_cnt;

		rx_pkt_cnt = (u32)(dm->phy_dbg_info.num_qry_phy_status_ofdm) +
			     (u32)(dm->phy_dbg_info.num_qry_phy_status_cck);

		if ((false_alm_cnt->cnt_cca_all > 31 && rx_pkt_cnt > 31) &&
		    false_alm_cnt->cnt_cca_all >= rx_pkt_cnt) {
			if ((rx_pkt_cnt + (rx_pkt_cnt >> 1)) <=
			    false_alm_cnt->cnt_cca_all)
				score = 0;
			else if ((rx_pkt_cnt + (rx_pkt_cnt >> 2)) <=
				 false_alm_cnt->cnt_cca_all)
				score = 1;
			else
				score = 2;
		}
		ODM_RT_TRACE(dm, ODM_COMP_RA_MASK,
			     "%s(): rx_pkt_cnt = %d, cnt_cca_all = %d\n",
			     __func__, rx_pkt_cnt, false_alm_cnt->cnt_cca_all);
	}
	ODM_RT_TRACE(
		dm, ODM_COMP_RA_MASK,
		"%s(): num_qry_phy_status_ofdm = %d, num_qry_phy_status_cck = %d\n",
		__func__, (u32)(dm->phy_dbg_info.num_qry_phy_status_ofdm),
		(u32)(dm->phy_dbg_info.num_qry_phy_status_cck));
	ODM_RT_TRACE(dm, ODM_COMP_RA_MASK, "%s(): nhm_cnt_0 = %d, score = %d\n",
		     __func__, dm->nhm_cnt_0, score);

	/* smoothing */
	dm->PT_score = (score << 4) + (dm->PT_score >> 1) + (dm->PT_score >> 2);
	score = (dm->PT_score + 32) >> 6;
	ODM_RT_TRACE(dm, ODM_COMP_RA_MASK,
		     "%s(): PT_score = %d, score after smoothing = %d\n",
		     __func__, dm->PT_score, score);

	/* mode decision */
	if (score == 2) {
		if (dm->is_disable_power_training) {
			dm->is_change_state = true;
			dm->is_disable_power_training = false;
			ODM_RT_TRACE(dm, ODM_COMP_RA_MASK,
				     "%s(): Change state\n", __func__);
		}
		ODM_RT_TRACE(dm, ODM_COMP_RA_MASK,
			     "%s(): Enable Power Training\n", __func__);
	} else if (score == 0) {
		if (!dm->is_disable_power_training) {
			dm->is_change_state = true;
			dm->is_disable_power_training = true;
			ODM_RT_TRACE(dm, ODM_COMP_RA_MASK,
				     "%s(): Change state\n", __func__);
		}
		ODM_RT_TRACE(dm, ODM_COMP_RA_MASK,
			     "%s(): Disable Power Training\n", __func__);
	}

	dm->phy_dbg_info.num_qry_phy_status_ofdm = 0;
	dm->phy_dbg_info.num_qry_phy_status_cck = 0;
}

/*===========================================================*/
/* The following is for compile only*/
/*===========================================================*/
/*#define TARGET_CHNL_NUM_2G_5G	59*/
/*===========================================================*/

void phydm_noisy_detection(struct phy_dm_struct *dm)
{
	u32 total_fa_cnt, total_cca_cnt;
	u32 score = 0, i, score_smooth;

	total_cca_cnt = dm->false_alm_cnt.cnt_cca_all;
	total_fa_cnt = dm->false_alm_cnt.cnt_all;

	for (i = 0; i <= 16; i++) {
		if (total_fa_cnt * 16 >= total_cca_cnt * (16 - i)) {
			score = 16 - i;
			break;
		}
	}

	/* noisy_decision_smooth = noisy_decision_smooth>>1 + (score<<3)>>1; */
	dm->noisy_decision_smooth =
		(dm->noisy_decision_smooth >> 1) + (score << 2);

	/* Round the noisy_decision_smooth: +"3" comes from (2^3)/2-1 */
	score_smooth = (total_cca_cnt >= 300) ?
			       ((dm->noisy_decision_smooth + 3) >> 3) :
			       0;

	dm->noisy_decision = (score_smooth >= 3) ? 1 : 0;
	ODM_RT_TRACE(
		dm, ODM_COMP_NOISY_DETECT,
		"[NoisyDetection] total_cca_cnt=%d, total_fa_cnt=%d, noisy_decision_smooth=%d, score=%d, score_smooth=%d, dm->noisy_decision=%d\n",
		total_cca_cnt, total_fa_cnt, dm->noisy_decision_smooth, score,
		score_smooth, dm->noisy_decision);
}

void phydm_set_ext_switch(void *dm_void, u32 *const dm_value, u32 *_used,
			  char *output, u32 *_out_len)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u32 ext_ant_switch = dm_value[0];

	if (dm->support_ic_type & (ODM_RTL8821 | ODM_RTL8881A)) {
		/*Output Pin Settings*/
		odm_set_mac_reg(dm, 0x4C, BIT(23),
				0); /*select DPDT_P and DPDT_N as output pin*/
		odm_set_mac_reg(dm, 0x4C, BIT(24), 1); /*by WLAN control*/

		odm_set_bb_reg(dm, 0xCB4, 0xF, 7); /*DPDT_P = 1b'0*/
		odm_set_bb_reg(dm, 0xCB4, 0xF0, 7); /*DPDT_N = 1b'0*/

		if (ext_ant_switch == MAIN_ANT) {
			odm_set_bb_reg(dm, 0xCB4, (BIT(29) | BIT(28)), 1);
			ODM_RT_TRACE(
				dm, ODM_COMP_API,
				"***8821A set ant switch = 2b'01 (Main)\n");
		} else if (ext_ant_switch == AUX_ANT) {
			odm_set_bb_reg(dm, 0xCB4, BIT(29) | BIT(28), 2);
			ODM_RT_TRACE(dm, ODM_COMP_API,
				     "***8821A set ant switch = 2b'10 (Aux)\n");
		}
	}
}

static void phydm_csi_mask_enable(void *dm_void, u32 enable)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u32 reg_value = 0;

	reg_value = (enable == CSI_MASK_ENABLE) ? 1 : 0;

	if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		odm_set_bb_reg(dm, 0xD2C, BIT(28), reg_value);
		ODM_RT_TRACE(dm, ODM_COMP_API,
			     "Enable CSI Mask:  Reg 0xD2C[28] = ((0x%x))\n",
			     reg_value);

	} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		odm_set_bb_reg(dm, 0x874, BIT(0), reg_value);
		ODM_RT_TRACE(dm, ODM_COMP_API,
			     "Enable CSI Mask:  Reg 0x874[0] = ((0x%x))\n",
			     reg_value);
	}
}

static void phydm_clean_all_csi_mask(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		odm_set_bb_reg(dm, 0xD40, MASKDWORD, 0);
		odm_set_bb_reg(dm, 0xD44, MASKDWORD, 0);
		odm_set_bb_reg(dm, 0xD48, MASKDWORD, 0);
		odm_set_bb_reg(dm, 0xD4c, MASKDWORD, 0);

	} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		odm_set_bb_reg(dm, 0x880, MASKDWORD, 0);
		odm_set_bb_reg(dm, 0x884, MASKDWORD, 0);
		odm_set_bb_reg(dm, 0x888, MASKDWORD, 0);
		odm_set_bb_reg(dm, 0x88c, MASKDWORD, 0);
		odm_set_bb_reg(dm, 0x890, MASKDWORD, 0);
		odm_set_bb_reg(dm, 0x894, MASKDWORD, 0);
		odm_set_bb_reg(dm, 0x898, MASKDWORD, 0);
		odm_set_bb_reg(dm, 0x89c, MASKDWORD, 0);
	}
}

static void phydm_set_csi_mask_reg(void *dm_void, u32 tone_idx_tmp,
				   u8 tone_direction)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u8 byte_offset, bit_offset;
	u32 target_reg;
	u8 reg_tmp_value;
	u32 tone_num = 64;
	u32 tone_num_shift = 0;
	u32 csi_mask_reg_p = 0, csi_mask_reg_n = 0;

	/* calculate real tone idx*/
	if ((tone_idx_tmp % 10) >= 5)
		tone_idx_tmp += 10;

	tone_idx_tmp = (tone_idx_tmp / 10);

	if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		tone_num = 64;
		csi_mask_reg_p = 0xD40;
		csi_mask_reg_n = 0xD48;

	} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		tone_num = 128;
		csi_mask_reg_p = 0x880;
		csi_mask_reg_n = 0x890;
	}

	if (tone_direction == FREQ_POSITIVE) {
		if (tone_idx_tmp >= (tone_num - 1))
			tone_idx_tmp = tone_num - 1;

		byte_offset = (u8)(tone_idx_tmp >> 3);
		bit_offset = (u8)(tone_idx_tmp & 0x7);
		target_reg = csi_mask_reg_p + byte_offset;

	} else {
		tone_num_shift = tone_num;

		if (tone_idx_tmp >= tone_num)
			tone_idx_tmp = tone_num;

		tone_idx_tmp = tone_num - tone_idx_tmp;

		byte_offset = (u8)(tone_idx_tmp >> 3);
		bit_offset = (u8)(tone_idx_tmp & 0x7);
		target_reg = csi_mask_reg_n + byte_offset;
	}

	reg_tmp_value = odm_read_1byte(dm, target_reg);
	ODM_RT_TRACE(dm, ODM_COMP_API,
		     "Pre Mask tone idx[%d]:  Reg0x%x = ((0x%x))\n",
		     (tone_idx_tmp + tone_num_shift), target_reg,
		     reg_tmp_value);
	reg_tmp_value |= BIT(bit_offset);
	odm_write_1byte(dm, target_reg, reg_tmp_value);
	ODM_RT_TRACE(dm, ODM_COMP_API,
		     "New Mask tone idx[%d]:  Reg0x%x = ((0x%x))\n",
		     (tone_idx_tmp + tone_num_shift), target_reg,
		     reg_tmp_value);
}

static void phydm_set_nbi_reg(void *dm_void, u32 tone_idx_tmp, u32 bw)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u32 nbi_table_128[NBI_TABLE_SIZE_128] = {
		25, 55, 85, 115, 135, 155, 185, 205, 225, 245,
		/*1~10*/ /*tone_idx X 10*/
		265, 285, 305, 335, 355, 375, 395, 415, 435, 455, /*11~20*/
		485, 505, 525, 555, 585, 615, 635}; /*21~27*/

	u32 nbi_table_256[NBI_TABLE_SIZE_256] = {
		25,   55,   85,   115,  135,  155,  175,  195,  225,
		245, /*1~10*/
		265,  285,  305,  325,  345,  365,  385,  405,  425,
		445, /*11~20*/
		465,  485,  505,  525,  545,  565,  585,  605,  625,
		645, /*21~30*/
		665,  695,  715,  735,  755,  775,  795,  815,  835,
		855, /*31~40*/
		875,  895,  915,  935,  955,  975,  995,  1015, 1035,
		1055, /*41~50*/
		1085, 1105, 1125, 1145, 1175, 1195, 1225, 1255, 1275}; /*51~59*/

	u32 reg_idx = 0;
	u32 i;
	u8 nbi_table_idx = FFT_128_TYPE;

	if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		nbi_table_idx = FFT_128_TYPE;
	} else if (dm->support_ic_type & ODM_IC_11AC_1_SERIES) {
		nbi_table_idx = FFT_256_TYPE;
	} else if (dm->support_ic_type & ODM_IC_11AC_2_SERIES) {
		if (bw == 80)
			nbi_table_idx = FFT_256_TYPE;
		else /*20M, 40M*/
			nbi_table_idx = FFT_128_TYPE;
	}

	if (nbi_table_idx == FFT_128_TYPE) {
		for (i = 0; i < NBI_TABLE_SIZE_128; i++) {
			if (tone_idx_tmp < nbi_table_128[i]) {
				reg_idx = i + 1;
				break;
			}
		}

	} else if (nbi_table_idx == FFT_256_TYPE) {
		for (i = 0; i < NBI_TABLE_SIZE_256; i++) {
			if (tone_idx_tmp < nbi_table_256[i]) {
				reg_idx = i + 1;
				break;
			}
		}
	}

	if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		odm_set_bb_reg(dm, 0xc40, 0x1f000000, reg_idx);
		ODM_RT_TRACE(dm, ODM_COMP_API,
			     "Set tone idx:  Reg0xC40[28:24] = ((0x%x))\n",
			     reg_idx);
		/**/
	} else {
		odm_set_bb_reg(dm, 0x87c, 0xfc000, reg_idx);
		ODM_RT_TRACE(dm, ODM_COMP_API,
			     "Set tone idx: Reg0x87C[19:14] = ((0x%x))\n",
			     reg_idx);
		/**/
	}
}

static void phydm_nbi_enable(void *dm_void, u32 enable)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u32 reg_value = 0;

	reg_value = (enable == NBI_ENABLE) ? 1 : 0;

	if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		odm_set_bb_reg(dm, 0xc40, BIT(9), reg_value);
		ODM_RT_TRACE(dm, ODM_COMP_API,
			     "Enable NBI Reg0xC40[9] = ((0x%x))\n", reg_value);

	} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		odm_set_bb_reg(dm, 0x87c, BIT(13), reg_value);
		ODM_RT_TRACE(dm, ODM_COMP_API,
			     "Enable NBI Reg0x87C[13] = ((0x%x))\n", reg_value);
	}
}

static u8 phydm_calculate_fc(void *dm_void, u32 channel, u32 bw, u32 second_ch,
			     u32 *fc_in)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u32 fc = *fc_in;
	u32 start_ch_per_40m[NUM_START_CH_40M + 1] = {
		36,  44,  52,  60,  100, 108, 116,     124,
		132, 140, 149, 157, 165, 173, 173 + 8,
	};
	u32 start_ch_per_80m[NUM_START_CH_80M + 1] = {
		36, 52, 100, 116, 132, 149, 165, 165 + 16,
	};
	u32 *start_ch = &start_ch_per_40m[0];
	u32 num_start_channel = NUM_START_CH_40M;
	u32 channel_offset = 0;
	u32 i;

	/*2.4G*/
	if (channel <= 14 && channel > 0) {
		if (bw == 80)
			return SET_ERROR;

		fc = 2412 + (channel - 1) * 5;

		if (bw == 40 && second_ch == PHYDM_ABOVE) {
			if (channel >= 10) {
				ODM_RT_TRACE(
					dm, ODM_COMP_API,
					"CH = ((%d)), Scnd_CH = ((%d)) Error setting\n",
					channel, second_ch);
				return SET_ERROR;
			}
			fc += 10;
		} else if (bw == 40 && (second_ch == PHYDM_BELOW)) {
			if (channel <= 2) {
				ODM_RT_TRACE(
					dm, ODM_COMP_API,
					"CH = ((%d)), Scnd_CH = ((%d)) Error setting\n",
					channel, second_ch);
				return SET_ERROR;
			}
			fc -= 10;
		}
	}
	/*5G*/
	else if (channel >= 36 && channel <= 177) {
		if (bw == 20) {
			fc = 5180 + (channel - 36) * 5;
			*fc_in = fc;
			return SET_SUCCESS;
		}

		if (bw == 40) {
			num_start_channel = NUM_START_CH_40M;
			start_ch = &start_ch_per_40m[0];
			channel_offset = CH_OFFSET_40M;
		} else if (bw == 80) {
			num_start_channel = NUM_START_CH_80M;
			start_ch = &start_ch_per_80m[0];
			channel_offset = CH_OFFSET_80M;
		}

		for (i = 0; i < num_start_channel; i++) {
			if (channel < start_ch[i + 1]) {
				channel = start_ch[i] + channel_offset;
				break;
			}
		}

		ODM_RT_TRACE(dm, ODM_COMP_API, "Mod_CH = ((%d))\n", channel);

		fc = 5180 + (channel - 36) * 5;

	} else {
		ODM_RT_TRACE(dm, ODM_COMP_API, "CH = ((%d)) Error setting\n",
			     channel);
		return SET_ERROR;
	}

	*fc_in = fc;

	return SET_SUCCESS;
}

static u8 phydm_calculate_intf_distance(void *dm_void, u32 bw, u32 fc,
					u32 f_interference,
					u32 *tone_idx_tmp_in)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u32 bw_up, bw_low;
	u32 int_distance;
	u32 tone_idx_tmp;
	u8 set_result = SET_NO_NEED;

	bw_up = fc + bw / 2;
	bw_low = fc - bw / 2;

	ODM_RT_TRACE(dm, ODM_COMP_API,
		     "[f_l, fc, fh] = [ %d, %d, %d ], f_int = ((%d))\n", bw_low,
		     fc, bw_up, f_interference);

	if (f_interference >= bw_low && f_interference <= bw_up) {
		int_distance = (fc >= f_interference) ? (fc - f_interference) :
							(f_interference - fc);
		tone_idx_tmp =
			int_distance << 5; /* =10*(int_distance /0.3125) */
		ODM_RT_TRACE(
			dm, ODM_COMP_API,
			"int_distance = ((%d MHz)) Mhz, tone_idx_tmp = ((%d.%d))\n",
			int_distance, (tone_idx_tmp / 10), (tone_idx_tmp % 10));
		*tone_idx_tmp_in = tone_idx_tmp;
		set_result = SET_SUCCESS;
	}

	return set_result;
}

static u8 phydm_csi_mask_setting(void *dm_void, u32 enable, u32 channel, u32 bw,
				 u32 f_interference, u32 second_ch)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u32 fc;
	u8 tone_direction;
	u32 tone_idx_tmp;
	u8 set_result = SET_SUCCESS;

	if (enable == CSI_MASK_DISABLE) {
		set_result = SET_SUCCESS;
		phydm_clean_all_csi_mask(dm);

	} else {
		ODM_RT_TRACE(
			dm, ODM_COMP_API,
			"[Set CSI MASK_] CH = ((%d)), BW = ((%d)), f_intf = ((%d)), Scnd_CH = ((%s))\n",
			channel, bw, f_interference,
			(((bw == 20) || (channel > 14)) ?
				 "Don't care" :
				 (second_ch == PHYDM_ABOVE) ? "H" : "L"));

		/*calculate fc*/
		if (phydm_calculate_fc(dm, channel, bw, second_ch, &fc) ==
		    SET_ERROR) {
			set_result = SET_ERROR;
		} else {
			/*calculate interference distance*/
			if (phydm_calculate_intf_distance(
				    dm, bw, fc, f_interference,
				    &tone_idx_tmp) == SET_SUCCESS) {
				tone_direction = (f_interference >= fc) ?
							 FREQ_POSITIVE :
							 FREQ_NEGATIVE;
				phydm_set_csi_mask_reg(dm, tone_idx_tmp,
						       tone_direction);
				set_result = SET_SUCCESS;
			} else {
				set_result = SET_NO_NEED;
			}
		}
	}

	if (set_result == SET_SUCCESS)
		phydm_csi_mask_enable(dm, enable);
	else
		phydm_csi_mask_enable(dm, CSI_MASK_DISABLE);

	return set_result;
}

u8 phydm_nbi_setting(void *dm_void, u32 enable, u32 channel, u32 bw,
		     u32 f_interference, u32 second_ch)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u32 fc;
	u32 tone_idx_tmp;
	u8 set_result = SET_SUCCESS;

	if (enable == NBI_DISABLE) {
		set_result = SET_SUCCESS;
	} else {
		ODM_RT_TRACE(
			dm, ODM_COMP_API,
			"[Set NBI] CH = ((%d)), BW = ((%d)), f_intf = ((%d)), Scnd_CH = ((%s))\n",
			channel, bw, f_interference,
			(((second_ch == PHYDM_DONT_CARE) || (bw == 20) ||
			  (channel > 14)) ?
				 "Don't care" :
				 (second_ch == PHYDM_ABOVE) ? "H" : "L"));

		/*calculate fc*/
		if (phydm_calculate_fc(dm, channel, bw, second_ch, &fc) ==
		    SET_ERROR) {
			set_result = SET_ERROR;
		} else {
			/*calculate interference distance*/
			if (phydm_calculate_intf_distance(
				    dm, bw, fc, f_interference,
				    &tone_idx_tmp) == SET_SUCCESS) {
				phydm_set_nbi_reg(dm, tone_idx_tmp, bw);
				set_result = SET_SUCCESS;
			} else {
				set_result = SET_NO_NEED;
			}
		}
	}

	if (set_result == SET_SUCCESS)
		phydm_nbi_enable(dm, enable);
	else
		phydm_nbi_enable(dm, NBI_DISABLE);

	return set_result;
}

void phydm_api_debug(void *dm_void, u32 function_map, u32 *const dm_value,
		     u32 *_used, char *output, u32 *_out_len)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 channel = dm_value[1];
	u32 bw = dm_value[2];
	u32 f_interference = dm_value[3];
	u32 second_ch = dm_value[4];
	u8 set_result = 0;

	/*PHYDM_API_NBI*/
	/*--------------------------------------------------------------------*/
	if (function_map == PHYDM_API_NBI) {
		if (dm_value[0] == 100) {
			PHYDM_SNPRINTF(
				output + used, out_len - used,
				"[HELP-NBI]  EN(on=1, off=2)   CH   BW(20/40/80)  f_intf(Mhz)    Scnd_CH(L=1, H=2)\n");
			return;

		} else if (dm_value[0] == NBI_ENABLE) {
			PHYDM_SNPRINTF(
				output + used, out_len - used,
				"[Enable NBI] CH = ((%d)), BW = ((%d)), f_intf = ((%d)), Scnd_CH = ((%s))\n",
				channel, bw, f_interference,
				((second_ch == PHYDM_DONT_CARE) || (bw == 20) ||
				 (channel > 14)) ?
					"Don't care" :
					((second_ch == PHYDM_ABOVE) ? "H" :
								      "L"));
			set_result =
				phydm_nbi_setting(dm, NBI_ENABLE, channel, bw,
						  f_interference, second_ch);

		} else if (dm_value[0] == NBI_DISABLE) {
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "[Disable NBI]\n");
			set_result =
				phydm_nbi_setting(dm, NBI_DISABLE, channel, bw,
						  f_interference, second_ch);

		} else {
			set_result = SET_ERROR;
		}

		PHYDM_SNPRINTF(
			output + used, out_len - used, "[NBI set result: %s]\n",
			(set_result == SET_SUCCESS) ?
				"Success" :
				((set_result == SET_NO_NEED) ? "No need" :
							       "Error"));
	}

	/*PHYDM_CSI_MASK*/
	/*--------------------------------------------------------------------*/
	else if (function_map == PHYDM_API_CSI_MASK) {
		if (dm_value[0] == 100) {
			PHYDM_SNPRINTF(
				output + used, out_len - used,
				"[HELP-CSI MASK]  EN(on=1, off=2)   CH   BW(20/40/80)  f_intf(Mhz)    Scnd_CH(L=1, H=2)\n");
			return;

		} else if (dm_value[0] == CSI_MASK_ENABLE) {
			PHYDM_SNPRINTF(
				output + used, out_len - used,
				"[Enable CSI MASK] CH = ((%d)), BW = ((%d)), f_intf = ((%d)), Scnd_CH = ((%s))\n",
				channel, bw, f_interference,
				(channel > 14) ?
					"Don't care" :
					(((second_ch == PHYDM_DONT_CARE) ||
					  (bw == 20) || (channel > 14)) ?
						 "H" :
						 "L"));
			set_result = phydm_csi_mask_setting(
				dm, CSI_MASK_ENABLE, channel, bw,
				f_interference, second_ch);

		} else if (dm_value[0] == CSI_MASK_DISABLE) {
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "[Disable CSI MASK]\n");
			set_result = phydm_csi_mask_setting(
				dm, CSI_MASK_DISABLE, channel, bw,
				f_interference, second_ch);

		} else {
			set_result = SET_ERROR;
		}

		PHYDM_SNPRINTF(output + used, out_len - used,
			       "[CSI MASK set result: %s]\n",
			       (set_result == SET_SUCCESS) ?
				       "Success" :
				       ((set_result == SET_NO_NEED) ?
						"No need" :
						"Error"));
	}
}
