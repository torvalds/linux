/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

/*@************************************************************
 * include files
 ************************************************************/

#include "mp_precomp.h"
#include "phydm_precomp.h"

const u16 phy_rate_table[] = {
	/*@20M*/
	1, 2, 5, 11,
	6, 9, 12, 18, 24, 36, 48, 54,
	6, 13, 19, 26, 39, 52, 58, 65, /*@MCS0~7*/
	13, 26, 39, 52, 78, 104, 117, 130, /*@MCS8~15*/
	19, 39, 58, 78, 117, 156, 175, 195, /*@MCS16~23*/
	26, 52, 78, 104, 156, 208, 234, 260, /*@MCS24~31*/
	6, 13, 19, 26, 39, 52, 58, 65, 78, 90, /*@1ss MCS0~9*/
	13, 26, 39, 52, 78, 104, 117, 130, 156, 180, /*@2ss MCS0~9*/
	19, 39, 58, 78, 117, 156, 175, 195, 234, 260, /*@3ss MCS0~9*/
	26, 52, 78, 104, 156, 208, 234, 260, 312, 360 /*@4ss MCS0~9*/
};

void phydm_traffic_load_decision(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 shift = 0;

	/*@---TP & Trafic-load calculation---*/

	if (dm->last_tx_ok_cnt > *dm->num_tx_bytes_unicast)
		dm->last_tx_ok_cnt = *dm->num_tx_bytes_unicast;

	if (dm->last_rx_ok_cnt > *dm->num_rx_bytes_unicast)
		dm->last_rx_ok_cnt = *dm->num_rx_bytes_unicast;

	dm->cur_tx_ok_cnt = *dm->num_tx_bytes_unicast - dm->last_tx_ok_cnt;
	dm->cur_rx_ok_cnt = *dm->num_rx_bytes_unicast - dm->last_rx_ok_cnt;
	dm->last_tx_ok_cnt = *dm->num_tx_bytes_unicast;
	dm->last_rx_ok_cnt = *dm->num_rx_bytes_unicast;

	/*@AP:  <<3(8bit), >>20(10^6,M), >>0(1sec)*/
	shift = 17 + (PHYDM_WATCH_DOG_PERIOD - 1);
	/*@WIN&CE:  <<3(8bit), >>20(10^6,M), >>1(2sec)*/

	dm->tx_tp = (dm->tx_tp >> 1) + (u32)((dm->cur_tx_ok_cnt >> shift) >> 1);
	dm->rx_tp = (dm->rx_tp >> 1) + (u32)((dm->cur_rx_ok_cnt >> shift) >> 1);

	dm->total_tp = dm->tx_tp + dm->rx_tp;

	/*@[Calculate TX/RX state]*/
	if (dm->tx_tp > (dm->rx_tp << 1))
		dm->txrx_state_all = TX_STATE;
	else if (dm->rx_tp > (dm->tx_tp << 1))
		dm->txrx_state_all = RX_STATE;
	else
		dm->txrx_state_all = BI_DIRECTION_STATE;

	/*@[Traffic load decision]*/
	dm->pre_traffic_load = dm->traffic_load;

	if (dm->cur_tx_ok_cnt > 1875000 || dm->cur_rx_ok_cnt > 1875000) {
		/* @( 1.875M * 8bit ) / 2sec= 7.5M bits /sec )*/
		dm->traffic_load = TRAFFIC_HIGH;
	} else if (dm->cur_tx_ok_cnt > 500000 || dm->cur_rx_ok_cnt > 500000) {
		/*@( 0.5M * 8bit ) / 2sec =  2M bits /sec )*/
		dm->traffic_load = TRAFFIC_MID;
	} else if (dm->cur_tx_ok_cnt > 100000 || dm->cur_rx_ok_cnt > 100000) {
		/*@( 0.1M * 8bit ) / 2sec =  0.4M bits /sec )*/
		dm->traffic_load = TRAFFIC_LOW;
	} else if (dm->cur_tx_ok_cnt > 25000 || dm->cur_rx_ok_cnt > 25000) {
		/*@( 0.025M * 8bit ) / 2sec =  0.1M bits /sec )*/
		dm->traffic_load = TRAFFIC_ULTRA_LOW;
	} else {
		dm->traffic_load = TRAFFIC_NO_TP;
	}

	/*@[Calculate consecutive idlel time]*/
	if (dm->traffic_load == 0)
		dm->consecutive_idlel_time += PHYDM_WATCH_DOG_PERIOD;
	else
		dm->consecutive_idlel_time = 0;

	#if 0
	PHYDM_DBG(dm, DBG_COMMON_FLOW,
		  "cur_tx_ok_cnt = %d, cur_rx_ok_cnt = %d, last_tx_ok_cnt = %d, last_rx_ok_cnt = %d\n",
		  dm->cur_tx_ok_cnt, dm->cur_rx_ok_cnt, dm->last_tx_ok_cnt,
		  dm->last_rx_ok_cnt);

	PHYDM_DBG(dm, DBG_COMMON_FLOW, "tx_tp = %d, rx_tp = %d\n", dm->tx_tp,
		  dm->rx_tp);
	#endif
}

void phydm_cck_new_agc_chk(struct dm_struct *dm)
{
	u32 new_agc_addr = 0x0;

	dm->cck_new_agc = false;
#if (RTL8723D_SUPPORT || RTL8822B_SUPPORT || RTL8821C_SUPPORT ||\
	RTL8197F_SUPPORT || RTL8710B_SUPPORT || RTL8192F_SUPPORT ||\
	RTL8195B_SUPPORT || RTL8198F_SUPPORT || RTL8822C_SUPPORT ||\
	RTL8721D_SUPPORT || RTL8710C_SUPPORT)
	if (dm->support_ic_type & (ODM_RTL8723D | ODM_RTL8822B | ODM_RTL8821C |
	    ODM_RTL8197F | ODM_RTL8710B | ODM_RTL8192F | ODM_RTL8195B |
	    ODM_RTL8721D | ODM_RTL8710C)) {
		new_agc_addr = R_0xa9c;
	} else if (dm->support_ic_type & (ODM_RTL8198F | ODM_RTL8822C |
		   ODM_RTL8814B | ODM_RTL8197G)) {
		new_agc_addr = R_0x1a9c;
	}

		/*@1: new agc  0: old agc*/
	dm->cck_new_agc = (boolean)odm_get_bb_reg(dm, new_agc_addr, BIT(17));
#endif
}

/*select 3 or 4 bit LNA */
void phydm_cck_lna_bit_num_chk(struct dm_struct *dm)
{
	boolean report_type = 0;
	#if (RTL8192E_SUPPORT)
	u32 value_824, value_82c;
	#endif

	#if (RTL8192E_SUPPORT)
	if (dm->support_ic_type & (ODM_RTL8192E)) {
	/* @0x824[9] = 0x82C[9] = 0xA80[7] those registers setting
	 * should be equal or CCK RSSI report may be incorrect
	 */
		value_824 = odm_get_bb_reg(dm, R_0x824, BIT(9));
		value_82c = odm_get_bb_reg(dm, R_0x82c, BIT(9));

		if (value_824 != value_82c)
			odm_set_bb_reg(dm, R_0x82c, BIT(9), value_824);
		odm_set_bb_reg(dm, R_0xa80, BIT(7), value_824);
		report_type = (boolean)value_824;
	}
	#endif

	#if (RTL8703B_SUPPORT || RTL8723D_SUPPORT || RTL8710B_SUPPORT)
	if (dm->support_ic_type &
	    (ODM_RTL8703B | ODM_RTL8723D | ODM_RTL8710B)) {
		report_type = (boolean)odm_get_bb_reg(dm, R_0x950, BIT(11));

		if (report_type != 1)
			pr_debug("[Warning] CCK should be 4bit LNA\n");
	}
	#endif

	#if (RTL8821C_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8821C) {
		if (dm->default_rf_set_8821c == SWITCH_TO_BTG)
			report_type = 1;
	}
	#endif

	dm->cck_agc_report_type = report_type;

	PHYDM_DBG(dm, ODM_COMP_INIT, "cck_agc_report_type=((%d))\n",
		  dm->cck_agc_report_type);
}

void phydm_init_cck_setting(struct dm_struct *dm)
{
	u32 reg_tmp = 0;
	u32 mask_tmp = 0;

	phydm_cck_new_agc_chk(dm);

	if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		return;

	reg_tmp = ODM_REG(CCK_RPT_FORMAT, dm);
	mask_tmp = ODM_BIT(CCK_RPT_FORMAT, dm);
	dm->is_cck_high_power = (boolean)odm_get_bb_reg(dm, reg_tmp, mask_tmp);

	PHYDM_DBG(dm, ODM_COMP_INIT, "ext_lna_gain=((%d))\n", dm->ext_lna_gain);

	phydm_config_cck_rx_antenna_init(dm);

	if (dm->support_ic_type & ODM_RTL8192F)
		phydm_config_cck_rx_path(dm, BB_PATH_AB);
	else if (dm->valid_path_set == BB_PATH_A)
		phydm_config_cck_rx_path(dm, BB_PATH_A);
	else if (dm->valid_path_set == BB_PATH_B)
		phydm_config_cck_rx_path(dm, BB_PATH_B);

	phydm_cck_lna_bit_num_chk(dm);
	phydm_get_cck_rssi_table_from_reg(dm);
}

#ifdef CONFIG_RFE_BY_HW_INFO
void phydm_init_hw_info_by_rfe(struct dm_struct *dm)
{
	#if (RTL8821C_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8821C)
		phydm_init_hw_info_by_rfe_type_8821c(dm);
	#endif
	#if (RTL8197F_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8197F)
		phydm_init_hw_info_by_rfe_type_8197f(dm);
	#endif
}
#endif

void phydm_common_info_self_init(struct dm_struct *dm)
{
	u32 reg_tmp = 0;
	u32 mask_tmp = 0;

	dm->run_in_drv_fw = RUN_IN_DRIVER;

	/*@BB IP Generation*/
	if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		dm->ic_ip_series = PHYDM_IC_JGR3;
	else if (dm->support_ic_type & ODM_IC_11AC_SERIES)
		dm->ic_ip_series = PHYDM_IC_AC;
	else if (dm->support_ic_type & ODM_IC_11N_SERIES)
		dm->ic_ip_series = PHYDM_IC_N;

	/*@BB phy-status Generation*/
	if (dm->support_ic_type & PHYSTS_3RD_TYPE_IC)
		dm->ic_phy_sts_type = PHYDM_PHYSTS_TYPE_3;
	else if (dm->support_ic_type & PHYSTS_2ND_TYPE_IC)
		dm->ic_phy_sts_type = PHYDM_PHYSTS_TYPE_2;
	else
		dm->ic_phy_sts_type = PHYDM_PHYSTS_TYPE_1;

	phydm_init_cck_setting(dm);

	reg_tmp = ODM_REG(BB_RX_PATH, dm);
	mask_tmp = ODM_BIT(BB_RX_PATH, dm);
	dm->rf_path_rx_enable = (u8)odm_get_bb_reg(dm, reg_tmp, mask_tmp);
#if (DM_ODM_SUPPORT_TYPE != ODM_CE)
	dm->is_net_closed = &dm->BOOLEAN_temp;

	phydm_init_debug_setting(dm);
#endif
	phydm_init_soft_ml_setting(dm);

	dm->phydm_sys_up_time = 0;

	if (dm->support_ic_type & ODM_IC_1SS)
		dm->num_rf_path = 1;
	else if (dm->support_ic_type & ODM_IC_2SS)
		dm->num_rf_path = 2;
	#if 0
	/* @RTK do not has IC which is equipped with 3 RF paths,
	 * so ODM_IC_3SS is an enpty macro and result in coverity check errors
	 */
	else if (dm->support_ic_type & ODM_IC_3SS)
		dm->num_rf_path = 3;
	#endif
	else if (dm->support_ic_type & ODM_IC_4SS)
		dm->num_rf_path = 4;
	else
		dm->num_rf_path = 1;

	phydm_trx_antenna_setting_init(dm, dm->num_rf_path);

	dm->tx_rate = 0xFF;
	dm->rssi_min_by_path = 0xFF;

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
	dm->pre_dbg_priority = DBGPORT_RELEASE;
	dm->tp_active_th = 5;
	dm->disable_phydm_watchdog = 0;

	dm->u8_dummy = 0xf;
	dm->u16_dummy = 0xffff;
	dm->u32_dummy = 0xffffffff;
#if (RTL8814B_SUPPORT)
/*@------------For spur detection Default Mode------------@*/
	dm->dsde_sel = DET_CSI;
	dm->csi_wgt = 4;
/*@-------------------------------------------------------@*/
#endif
	dm->pause_lv_table.lv_cckpd = PHYDM_PAUSE_RELEASE;
	dm->pause_lv_table.lv_dig = PHYDM_PAUSE_RELEASE;
	dm->pre_is_linked = false;
	dm->is_linked = false;
/*dym bw thre and it can config by registry*/
	if (dm->en_auto_bw_th == 0)
		dm->en_auto_bw_th = 20;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	if (!(dm->is_fcs_mode_enable)) {
		dm->is_fcs_mode_enable = &dm->boolean_dummy;
		pr_debug("[Warning] is_fcs_mode_enable=NULL\n");
	}
#endif
	/*init IOT table*/
	odm_memory_set(dm, &dm->iot_table, 0, sizeof(struct phydm_iot_center));
}

void phydm_iot_patch_id_update(void *dm_void, u32 iot_idx, boolean en)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_iot_center	*iot_table = &dm->iot_table;

	PHYDM_DBG(dm, DBG_CMN, "[IOT] 0x%x = %d\n", iot_idx, en);
	switch (iot_idx) {
	case 0x100f0401:
		iot_table->patch_id_100f0401 = en;
		PHYDM_DBG(dm, DBG_CMN, "[IOT] patch_id_100f0401 = %d\n",
			  iot_table->patch_id_100f0401);
		break;
	case 0x10120200:
		iot_table->patch_id_10120200 = en;
		PHYDM_DBG(dm, DBG_CMN, "[IOT] patch_id_10120200 = %d\n",
			  iot_table->patch_id_10120200);
		break;
	case 0x021f0800:
		iot_table->patch_id_021f0800 = en;
		PHYDM_DBG(dm, DBG_CMN, "[IOT] patch_id_021f0800 = %d\n",
			  iot_table->patch_id_021f0800);
		break;
	default:
		pr_debug("[%s] warning!\n", __func__);
		break;
	}
}

void phydm_cmn_sta_info_update(void *dm_void, u8 macid)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct cmn_sta_info *sta = dm->phydm_sta_info[macid];
	struct ra_sta_info *ra = NULL;

	if (is_sta_active(sta)) {
		ra = &sta->ra_info;
	} else {
		PHYDM_DBG(dm, DBG_RA_MASK, "[Warning] %s invalid sta_info\n",
			  __func__);
		return;
	}

	PHYDM_DBG(dm, DBG_RA_MASK, "%s ======>\n", __func__);
	PHYDM_DBG(dm, DBG_RA_MASK, "MACID=%d\n", sta->mac_id);

	/*@[Calculate TX/RX state]*/
	if (sta->tx_moving_average_tp > (sta->rx_moving_average_tp << 1))
		ra->txrx_state = TX_STATE;
	else if (sta->rx_moving_average_tp > (sta->tx_moving_average_tp << 1))
		ra->txrx_state = RX_STATE;
	else
		ra->txrx_state = BI_DIRECTION_STATE;

	ra->is_noisy = dm->noisy_decision;
}

void phydm_common_info_self_update(struct dm_struct *dm)
{
	u8 sta_cnt = 0, num_active_client = 0;
	u32 i, one_entry_macid = 0;
	u32 ma_rx_tp = 0;
	u32 tp_diff = 0;
	struct cmn_sta_info *sta;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PADAPTER adapter = (PADAPTER)dm->adapter;
	PMGNT_INFO mgnt_info = &((PADAPTER)adapter)->MgntInfo;

	sta = dm->phydm_sta_info[0];

	/* STA mode is linked to AP */
	if (is_sta_active(sta) && !ACTING_AS_AP(adapter))
		dm->bsta_state = true;
	else
		dm->bsta_state = false;
#endif

	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
		sta = dm->phydm_sta_info[i];
		if (is_sta_active(sta)) {
			sta_cnt++;

			if (sta_cnt == 1)
				one_entry_macid = i;

			phydm_cmn_sta_info_update(dm, (u8)i);
			#ifdef PHYDM_BEAMFORMING_SUPPORT
			/*@phydm_get_txbf_device_num(dm, (u8)i);*/
			#endif

			ma_rx_tp = sta->rx_moving_average_tp +
				   sta->tx_moving_average_tp;

			PHYDM_DBG(dm, DBG_COMMON_FLOW,
				  "TP[%d]: ((%d )) bit/sec\n", i, ma_rx_tp);

			if (ma_rx_tp > ACTIVE_TP_THRESHOLD)
				num_active_client++;
		}
	}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	dm->is_linked = (sta_cnt != 0) ? true : false;
#endif

	if (sta_cnt == 1) {
		dm->is_one_entry_only = true;
		dm->one_entry_macid = one_entry_macid;
		dm->one_entry_tp = ma_rx_tp;

		dm->tp_active_occur = 0;

		PHYDM_DBG(dm, DBG_COMMON_FLOW,
			  "one_entry_tp=((%d)), pre_one_entry_tp=((%d))\n",
			  dm->one_entry_tp, dm->pre_one_entry_tp);

		if (dm->one_entry_tp > dm->pre_one_entry_tp &&
		    dm->pre_one_entry_tp <= 2) {
			tp_diff = dm->one_entry_tp - dm->pre_one_entry_tp;

			if (tp_diff > dm->tp_active_th)
				dm->tp_active_occur = 1;
		}
		dm->pre_one_entry_tp = dm->one_entry_tp;
	} else {
		dm->is_one_entry_only = false;
	}

	dm->pre_number_linked_client = dm->number_linked_client;
	dm->pre_number_active_client = dm->number_active_client;

	dm->number_linked_client = sta_cnt;
	dm->number_active_client = num_active_client;

	/*Traffic load information update*/
	phydm_traffic_load_decision(dm);

	dm->phydm_sys_up_time += PHYDM_WATCH_DOG_PERIOD;

	dm->is_dfs_band = phydm_is_dfs_band(dm);
	dm->phy_dbg_info.show_phy_sts_cnt = 0;

	/*[Link Status Check]*/
	dm->first_connect = dm->is_linked && !dm->pre_is_linked;
	dm->first_disconnect = !dm->is_linked && dm->pre_is_linked;
	dm->pre_is_linked = dm->is_linked;
}

void phydm_common_info_self_reset(struct dm_struct *dm)
{
	struct odm_phy_dbg_info		*dbg_t = &dm->phy_dbg_info;

	dbg_t->beacon_cnt_in_period = dbg_t->num_qry_beacon_pkt;
	dbg_t->num_qry_beacon_pkt = 0;

	dm->rxsc_l = 0xff;
	dm->rxsc_20 = 0xff;
	dm->rxsc_40 = 0xff;
	dm->rxsc_80 = 0xff;
}

void *
phydm_get_structure(struct dm_struct *dm, u8 structure_type)

{
	void *structure = NULL;

	switch (structure_type) {
	case PHYDM_FALSEALMCNT:
		structure = &dm->false_alm_cnt;
		break;

	case PHYDM_CFOTRACK:
		structure = &dm->dm_cfo_track;
		break;

	case PHYDM_ADAPTIVITY:
		structure = &dm->adaptivity;
		break;
#ifdef CONFIG_PHYDM_DFS_MASTER
	case PHYDM_DFS:
		structure = &dm->dfs;
		break;
#endif
	default:
		break;
	}

	return structure;
}

void phydm_phy_info_update(struct dm_struct *dm)
{
#if (RTL8822B_SUPPORT)
	if (dm->support_ic_type == ODM_RTL8822B)
		dm->phy_dbg_info.condi_num = phydm_get_condi_num_8822b(dm);
#endif
}

void phydm_hw_setting(struct dm_struct *dm)
{
#if (RTL8821A_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8821)
		odm_hw_setting_8821a(dm);
#endif

#if (RTL8814A_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8814A)
		phydm_hwsetting_8814a(dm);
#endif

#if (RTL8822B_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8822B)
		phydm_hwsetting_8822b(dm);
#endif

#if (RTL8812A_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8812)
		phydm_hwsetting_8812a(dm);
#endif

#if (RTL8197F_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8197F)
		phydm_hwsetting_8197f(dm);
#endif

#if (RTL8192F_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8192F)
		phydm_hwsetting_8192f(dm);
#endif

#if (RTL8822C_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8822C)
		phydm_hwsetting_8822c(dm);
#endif

#ifdef PHYDM_CCK_RX_PATHDIV_SUPPORT
	phydm_cck_rx_pathdiv_watchdog(dm);
#endif
}

__odm_func__
boolean phydm_chk_bb_rf_pkg_set_valid(struct dm_struct *dm)
{
	boolean valid = true;

	if (dm->support_ic_type == ODM_RTL8822C) {
		#if (RTL8822C_SUPPORT)
		valid = phydm_chk_pkg_set_valid_8822c(dm,
						      RELEASE_VERSION_8822C,
						      RF_RELEASE_VERSION_8822C);
		#else
		valid = true; /*@Just for preventing compile warnings*/
		#endif
	#if (RTL8812F_SUPPORT)
	} else if (dm->support_ic_type == ODM_RTL8812F) {
		valid = phydm_chk_pkg_set_valid_8812f(dm,
						      RELEASE_VERSION_8812F,
						      RF_RELEASE_VERSION_8812F);
	#endif
	#if (RTL8197G_SUPPORT)
	} else if (dm->support_ic_type == ODM_RTL8197G) {
		valid = phydm_chk_pkg_set_valid_8197g(dm,
						      RELEASE_VERSION_8197G,
						      RF_RELEASE_VERSION_8197G);
	#endif
	#if (RTL8812F_SUPPORT)
	} else if (dm->support_ic_type == ODM_RTL8812F) {
		valid = phydm_chk_pkg_set_valid_8812f(dm,
						      RELEASE_VERSION_8812F,
						      RF_RELEASE_VERSION_8812F);
	#endif
	#if (RTL8198F_SUPPORT)
	} else if (dm->support_ic_type == ODM_RTL8198F) {
		valid = phydm_chk_pkg_set_valid_8198f(dm,
						      RELEASE_VERSION_8198F,
						      RF_RELEASE_VERSION_8198F);
	#endif
	#if (RTL8814B_SUPPORT)
	} else if (dm->support_ic_type == ODM_RTL8814B) {
		valid = phydm_chk_pkg_set_valid_8814b(dm,
						      RELEASE_VERSION_8814B,
						      RF_RELEASE_VERSION_8814B);
	#endif
	}

	return valid;
}

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
u64 phydm_supportability_init_win(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u64 support_ability = 0;

	switch (dm->support_ic_type) {
/*@---------------N Series--------------------*/
#if (RTL8188E_SUPPORT)
	case ODM_RTL8188E:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*ODM_BB_DYNAMIC_TXPWR |*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR |
			ODM_BB_PRIMARY_CCA;
		break;
#endif

#if (RTL8192E_SUPPORT)
	case ODM_RTL8192E:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*ODM_BB_DYNAMIC_TXPWR |*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR |
			ODM_BB_PRIMARY_CCA;
		break;
#endif

#if (RTL8723B_SUPPORT)
	case ODM_RTL8723B:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*ODM_BB_DYNAMIC_TXPWR |*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR |
			ODM_BB_PRIMARY_CCA;
		break;
#endif

#if (RTL8703B_SUPPORT)
	case ODM_RTL8703B:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*ODM_BB_DYNAMIC_TXPWR |*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

#if (RTL8723D_SUPPORT)
	case ODM_RTL8723D:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*ODM_BB_DYNAMIC_TXPWR |*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			ODM_BB_PWR_TRAIN |
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

#if (RTL8710B_SUPPORT)
	case ODM_RTL8710B:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*ODM_BB_DYNAMIC_TXPWR |*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			ODM_BB_PWR_TRAIN |
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

#if (RTL8188F_SUPPORT)
	case ODM_RTL8188F:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*ODM_BB_DYNAMIC_TXPWR |*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

#if (RTL8192F_SUPPORT)
	case ODM_RTL8192F:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			ODM_BB_PWR_TRAIN	|
			ODM_BB_RATE_ADAPTIVE |
			/*ODM_BB_PATH_DIV |*/
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ADAPTIVE_SOML |
			ODM_BB_ENV_MONITOR;
			/*ODM_BB_LNA_SAT_CHK |*/
			/*ODM_BB_PRIMARY_CCA*/

		break;
#endif

/*@---------------AC Series-------------------*/

#if (RTL8812A_SUPPORT || RTL8821A_SUPPORT)
	case ODM_RTL8812:
	case ODM_RTL8821:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			ODM_BB_DYNAMIC_TXPWR |
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

#if (RTL8814A_SUPPORT)
	case ODM_RTL8814A:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			ODM_BB_DYNAMIC_TXPWR |
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

#if (RTL8822B_SUPPORT)
	case ODM_RTL8822B:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			/*ODM_BB_ADAPTIVE_SOML |*/
			ODM_BB_RATE_ADAPTIVE |
			/*ODM_BB_PATH_DIV |*/
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

#if (RTL8821C_SUPPORT)
	case ODM_RTL8821C:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

/*@---------------JGR3 Series-------------------*/

#if (RTL8822C_SUPPORT)
	case ODM_RTL8822C:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			ODM_BB_DYNAMIC_TXPWR |
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_PATH_DIV |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

#if (RTL8814B_SUPPORT)
	case ODM_RTL8814B:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*ODM_BB_DYNAMIC_TXPWR |*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING;
			/*ODM_BB_ENV_MONITOR;*/
		break;
#endif

	default:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*ODM_BB_DYNAMIC_TXPWR |*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;

		pr_debug("[Warning] Supportability Init Warning !!!\n");
		break;
	}

	return support_ability;
}
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_CE))
u64 phydm_supportability_init_ce(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u64 support_ability = 0;

	switch (dm->support_ic_type) {
/*@---------------N Series--------------------*/
#if (RTL8188E_SUPPORT)
	case ODM_RTL8188E:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*@ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*@ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR |
			ODM_BB_PRIMARY_CCA;
		break;
#endif

#if (RTL8192E_SUPPORT)
	case ODM_RTL8192E:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*@ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*@ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR |
			ODM_BB_PRIMARY_CCA;
		break;
#endif

#if (RTL8723B_SUPPORT)
	case ODM_RTL8723B:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*@ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*@ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR |
			ODM_BB_PRIMARY_CCA;
		break;
#endif

#if (RTL8703B_SUPPORT)
	case ODM_RTL8703B:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*@ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*@ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

#if (RTL8723D_SUPPORT)
	case ODM_RTL8723D:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*@ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			ODM_BB_PWR_TRAIN	|
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

#if (RTL8710B_SUPPORT)
	case ODM_RTL8710B:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*@ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*@ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

#if (RTL8188F_SUPPORT)
	case ODM_RTL8188F:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*@ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*@ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

#if (RTL8192F_SUPPORT)
	case ODM_RTL8192F:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			ODM_BB_PWR_TRAIN |
			ODM_BB_RATE_ADAPTIVE |
			/*ODM_BB_PATH_DIV |*/
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			/*@ODM_BB_ADAPTIVE_SOML |*/
			ODM_BB_ENV_MONITOR;
			/*@ODM_BB_LNA_SAT_CHK |*/
			/*@ODM_BB_PRIMARY_CCA*/
			break;
#endif
/*@---------------AC Series-------------------*/

#if (RTL8812A_SUPPORT || RTL8821A_SUPPORT)
	case ODM_RTL8812:
	case ODM_RTL8821:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*@ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*@ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

#if (RTL8814A_SUPPORT)
	case ODM_RTL8814A:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*@ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*@ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

#if (RTL8822B_SUPPORT)
	case ODM_RTL8822B:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			ODM_BB_DYNAMIC_TXPWR	|
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*@ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			/*ODM_BB_PATH_DIV |*/
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

#if (RTL8821C_SUPPORT)
	case ODM_RTL8821C:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*@ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*@ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

/*@---------------JGR3 Series-------------------*/

#if (RTL8822C_SUPPORT)
	case ODM_RTL8822C:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			ODM_BB_DYNAMIC_TXPWR	|
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			ODM_BB_RATE_ADAPTIVE |
			/* ODM_BB_PATH_DIV | */
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

#if (RTL8814B_SUPPORT)
	case ODM_RTL8814B:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*@ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*@ODM_BB_PWR_TRAIN |*/
			/*ODM_BB_RATE_ADAPTIVE |*/
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING;
			/*ODM_BB_ENV_MONITOR;*/
		break;
#endif

	default:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*@ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*@ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;

		pr_debug("[Warning] Supportability Init Warning !!!\n");
		break;
	}

	return support_ability;
}
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
u64 phydm_supportability_init_ap(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u64 support_ability = 0;

	switch (dm->support_ic_type) {
/*@---------------N Series--------------------*/
#if (RTL8188E_SUPPORT)
	case ODM_RTL8188E:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR |
			ODM_BB_PRIMARY_CCA;
		break;
#endif

#if (RTL8192E_SUPPORT)
	case ODM_RTL8192E:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR |
			ODM_BB_PRIMARY_CCA;
		break;
#endif

#if (RTL8723B_SUPPORT)
	case ODM_RTL8723B:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

#if (RTL8198F_SUPPORT || RTL8197F_SUPPORT)
	case ODM_RTL8198F:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			/*ODM_BB_RATE_ADAPTIVE |*/
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING;
			/*ODM_BB_ADAPTIVE_SOML |*/
			/*ODM_BB_ENV_MONITOR |*/
			/*ODM_BB_LNA_SAT_CHK |*/
			/*ODM_BB_PRIMARY_CCA;*/
		break;
	case ODM_RTL8197F:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ADAPTIVE_SOML |
			ODM_BB_ENV_MONITOR |
			ODM_BB_LNA_SAT_CHK |
			ODM_BB_PRIMARY_CCA;
		break;
#endif

#if (RTL8192F_SUPPORT)
	case ODM_RTL8192F:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			/*ODM_BB_CFO_TRACKING |*/
			ODM_BB_ADAPTIVE_SOML |
			/*ODM_BB_PATH_DIV |*/
			ODM_BB_ENV_MONITOR |
			/*ODM_BB_LNA_SAT_CHK |*/
			/*ODM_BB_PRIMARY_CCA |*/
			0;
		break;
#endif

/*@---------------AC Series-------------------*/

#if (RTL8881A_SUPPORT)
	case ODM_RTL8881A:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

#if (RTL8814A_SUPPORT)
	case ODM_RTL8814A:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

#if (RTL8822B_SUPPORT)
	case ODM_RTL8822B:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			/*ODM_BB_ADAPTIVE_SOML |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

#if (RTL8821C_SUPPORT)
	case ODM_RTL8821C:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;

		break;
#endif

/*@---------------JGR3 Series-------------------*/

#if (RTL8814B_SUPPORT)
	case ODM_RTL8814B:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			/*ODM_BB_RATE_ADAPTIVE |*/
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING;
			/*ODM_BB_ENV_MONITOR;*/
		break;
#endif

#if (RTL8197G_SUPPORT)
	case ODM_RTL8197G:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

#if (RTL8812F_SUPPORT)
	case ODM_RTL8812F:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			ODM_BB_DYNAMIC_TXPWR	|
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			/*ODM_BB_CCK_PD |*/
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

	default:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;

		pr_debug("[Warning] Supportability Init Warning !!!\n");
		break;
	}

	return support_ability;
}
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_IOT))
u64 phydm_supportability_init_iot(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u64 support_ability = 0;

	switch (dm->support_ic_type) {
#if (RTL8710B_SUPPORT)
	case ODM_RTL8710B:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*ODM_BB_DYNAMIC_TXPWR |*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

#if (RTL8195A_SUPPORT)
	case ODM_RTL8195A:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*ODM_BB_DYNAMIC_TXPWR |*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

#if (RTL8195B_SUPPORT)
	case ODM_RTL8195B:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*ODM_BB_DYNAMIC_TXPWR |*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING;
			/*ODM_BB_ENV_MONITOR*/
		break;
#endif

#if (RTL8721D_SUPPORT)
	case ODM_RTL8721D:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*ODM_BB_DYNAMIC_TXPWR |*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif

#if (RTL8710C_SUPPORT)
	case ODM_RTL8710C:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*ODM_BB_DYNAMIC_TXPWR |*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_ADAPTIVITY |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;
		break;
#endif
	default:
		support_ability |=
			ODM_BB_DIG |
			ODM_BB_RA_MASK |
			/*ODM_BB_DYNAMIC_TXPWR |*/
			ODM_BB_FA_CNT |
			ODM_BB_RSSI_MONITOR |
			ODM_BB_CCK_PD |
			/*ODM_BB_PWR_TRAIN |*/
			ODM_BB_RATE_ADAPTIVE |
			ODM_BB_CFO_TRACKING |
			ODM_BB_ENV_MONITOR;

		pr_debug("[Warning] Supportability Init Warning !!!\n");
		break;
	}

	return support_ability;
}
#endif

void phydm_fwoffload_ability_init(struct dm_struct *dm,
				  enum phydm_offload_ability offload_ability)
{
	switch (offload_ability) {
	case PHYDM_PHY_PARAM_OFFLOAD:
		if (dm->support_ic_type & PHYDM_IC_SUPPORT_FW_PARAM_OFFLOAD)
			dm->fw_offload_ability |= PHYDM_PHY_PARAM_OFFLOAD;
		break;

	case PHYDM_RF_IQK_OFFLOAD:
		dm->fw_offload_ability |= PHYDM_RF_IQK_OFFLOAD;
		break;

	case PHYDM_RF_DPK_OFFLOAD:
		dm->fw_offload_ability |= PHYDM_RF_DPK_OFFLOAD;
		break;

	default:
		PHYDM_DBG(dm, ODM_COMP_INIT, "fwofflad, wrong init type!!\n");
		break;
	}

	PHYDM_DBG(dm, ODM_COMP_INIT, "fw_offload_ability = %x\n",
		  dm->fw_offload_ability);
}

void phydm_fwoffload_ability_clear(struct dm_struct *dm,
				   enum phydm_offload_ability offload_ability)
{
	switch (offload_ability) {
	case PHYDM_PHY_PARAM_OFFLOAD:
		if (dm->support_ic_type & PHYDM_IC_SUPPORT_FW_PARAM_OFFLOAD)
			dm->fw_offload_ability &= (~PHYDM_PHY_PARAM_OFFLOAD);
		break;

	case PHYDM_RF_IQK_OFFLOAD:
		dm->fw_offload_ability &= (~PHYDM_RF_IQK_OFFLOAD);
		break;

	case PHYDM_RF_DPK_OFFLOAD:
		dm->fw_offload_ability &= (~PHYDM_RF_DPK_OFFLOAD);
		break;	

	default:
		PHYDM_DBG(dm, ODM_COMP_INIT, "fwofflad, wrong init type!!\n");
		break;
	}

	PHYDM_DBG(dm, ODM_COMP_INIT, "fw_offload_ability = %x\n",
		  dm->fw_offload_ability);
}

void phydm_supportability_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u64 support_ability;

	if (dm->manual_supportability &&
	    *dm->manual_supportability != 0xffffffff) {
		support_ability = *dm->manual_supportability;
	} else if (*dm->mp_mode) {
		support_ability = 0;
	} else {
		#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
		support_ability = phydm_supportability_init_win(dm);
		#elif (DM_ODM_SUPPORT_TYPE & (ODM_AP))
		support_ability = phydm_supportability_init_ap(dm);
		#elif(DM_ODM_SUPPORT_TYPE & (ODM_CE))
		support_ability = phydm_supportability_init_ce(dm);
		#elif(DM_ODM_SUPPORT_TYPE & (ODM_IOT))
		support_ability = phydm_supportability_init_iot(dm);
		#endif

		/*@[Config Antenna Diversity]*/
		if (IS_FUNC_EN(dm->enable_antdiv))
			support_ability |= ODM_BB_ANT_DIV;

		/*@[Config TXpath Diversity]*/
		if (IS_FUNC_EN(dm->enable_pathdiv))
			support_ability |= ODM_BB_PATH_DIV;

		/*@[Config Adaptive SOML]*/
		if (IS_FUNC_EN(dm->en_adap_soml))
			support_ability |= ODM_BB_ADAPTIVE_SOML;

	}
	dm->support_ability = support_ability;
	PHYDM_DBG(dm, ODM_COMP_INIT, "IC=0x%x, mp=%d, Supportability=0x%llx\n",
		  dm->support_ic_type, *dm->mp_mode, dm->support_ability);
}

void phydm_rfe_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	PHYDM_DBG(dm, ODM_COMP_INIT, "RFE_Init\n");
#if (RTL8822B_SUPPORT == 1)
	if (dm->support_ic_type == ODM_RTL8822B)
		phydm_rfe_8822b_init(dm);
#endif
}

void phydm_dm_early_init(struct dm_struct *dm)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	phydm_init_debug_setting(dm);
#endif
}

enum phydm_init_result odm_dm_init(struct dm_struct *dm)
{
	enum phydm_init_result result = PHYDM_INIT_SUCCESS;

	if (!phydm_chk_bb_rf_pkg_set_valid(dm)) {
		pr_debug("[Warning][%s] Init fail\n", __func__);
		return PHYDM_INIT_FAIL_BBRF_REG_INVALID;
	}

	halrf_init(dm);
	phydm_supportability_init(dm);
	phydm_rfe_init(dm);
	phydm_common_info_self_init(dm);
	phydm_rx_phy_status_init(dm);
#ifdef PHYDM_AUTO_DEGBUG
	phydm_auto_dbg_engine_init(dm);
#endif
	phydm_dig_init(dm);
#ifdef PHYDM_SUPPORT_CCKPD
	phydm_cck_pd_init(dm);
#endif
	phydm_env_monitor_init(dm);
	phydm_adaptivity_init(dm);
	phydm_ra_info_init(dm);
	phydm_rssi_monitor_init(dm);
	phydm_cfo_tracking_init(dm);
	phydm_rf_init(dm);
	phydm_dc_cancellation(dm);
#ifdef PHYDM_TXA_CALIBRATION
	phydm_txcurrentcalibration(dm);
	phydm_get_pa_bias_offset(dm);
#endif
#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY
	odm_antenna_diversity_init(dm);
#endif
#ifdef CONFIG_ADAPTIVE_SOML
	phydm_adaptive_soml_init(dm);
#endif
#ifdef CONFIG_PATH_DIVERSITY
	phydm_tx_path_diversity_init(dm);
#endif
#ifdef CONFIG_DYNAMIC_TX_TWR
	phydm_dynamic_tx_power_init(dm);
#endif
#if (PHYDM_LA_MODE_SUPPORT)
	phydm_la_init(dm);
#endif

#ifdef PHYDM_BEAMFORMING_VERSION1
	phydm_beamforming_init(dm);
#endif

#if (RTL8188E_SUPPORT)
	odm_ra_info_init_all(dm);
#endif
#ifdef PHYDM_PRIMARY_CCA
	phydm_primary_cca_init(dm);
#endif
#ifdef CONFIG_PSD_TOOL
	phydm_psd_init(dm);
#endif

#ifdef CONFIG_SMART_ANTENNA
	phydm_smt_ant_init(dm);
#endif
#ifdef PHYDM_LNA_SAT_CHK_SUPPORT
	phydm_lna_sat_check_init(dm);
#endif
#ifdef CONFIG_MCC_DM
	phydm_mcc_init(dm);
#endif

#ifdef PHYDM_CCK_RX_PATHDIV_SUPPORT
	phydm_cck_rx_pathdiv_init(dm);
#endif

#ifdef CONFIG_MU_RSOML
	phydm_mu_rsoml_init(dm);
#endif

	return result;
}

void odm_dm_reset(struct dm_struct *dm)
{
	#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY
	odm_ant_div_reset(dm);
	#endif
	phydm_set_edcca_threshold_api(dm);
}

void phydm_supportability_en(void *dm_void, char input[][16], u32 *_used,
			     char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 dm_value[10] = {0};
	u64 pre_support_ability, one = 1;
	u64 comp = 0;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u8 i;

	for (i = 0; i < 5; i++) {
		if (input[i + 1])
			PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &dm_value[i]);
	}

	pre_support_ability = dm->support_ability;
	comp = dm->support_ability;

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\n================================\n");

	if (dm_value[0] == 100) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "[Supportability] PhyDM Selection\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "================================\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "00. (( %s ))DIG\n",
			 ((comp & ODM_BB_DIG) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "01. (( %s ))RA_MASK\n",
			 ((comp & ODM_BB_RA_MASK) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "02. (( %s ))DYN_TXPWR\n",
			 ((comp & ODM_BB_DYNAMIC_TXPWR) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "03. (( %s ))FA_CNT\n",
			 ((comp & ODM_BB_FA_CNT) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "04. (( %s ))RSSI_MNTR\n",
			 ((comp & ODM_BB_RSSI_MONITOR) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "05. (( %s ))CCK_PD\n",
			 ((comp & ODM_BB_CCK_PD) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "06. (( %s ))ANT_DIV\n",
			 ((comp & ODM_BB_ANT_DIV) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "07. (( %s ))SMT_ANT\n",
			 ((comp & ODM_BB_SMT_ANT) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "08. (( %s ))PWR_TRAIN\n",
			 ((comp & ODM_BB_PWR_TRAIN) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "09. (( %s ))RA\n",
			 ((comp & ODM_BB_RATE_ADAPTIVE) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "10. (( %s ))PATH_DIV\n",
			 ((comp & ODM_BB_PATH_DIV) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "11. (( %s ))DFS\n",
			 ((comp & ODM_BB_DFS) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "12. (( %s ))DYN_ARFR\n",
			 ((comp & ODM_BB_DYNAMIC_ARFR) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "13. (( %s ))ADAPTIVITY\n",
			 ((comp & ODM_BB_ADAPTIVITY) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "14. (( %s ))CFO_TRACK\n",
			 ((comp & ODM_BB_CFO_TRACKING) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "15. (( %s ))ENV_MONITOR\n",
			 ((comp & ODM_BB_ENV_MONITOR) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "16. (( %s ))PRI_CCA\n",
			 ((comp & ODM_BB_PRIMARY_CCA) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "17. (( %s ))ADPTV_SOML\n",
			 ((comp & ODM_BB_ADAPTIVE_SOML) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "18. (( %s ))LNA_SAT_CHK\n",
			 ((comp & ODM_BB_LNA_SAT_CHK) ? ("V") : (".")));

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "================================\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "[Supportability] PhyDM offload ability\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "================================\n");

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "00. (( %s ))PHY PARAM OFFLOAD\n",
			 ((dm->fw_offload_ability & PHYDM_PHY_PARAM_OFFLOAD) ?
			 ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "01. (( %s ))RF IQK OFFLOAD\n",
			 ((dm->fw_offload_ability & PHYDM_RF_IQK_OFFLOAD) ?
			 ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "================================\n");

	} else if (dm_value[0] == 101) {
		dm->support_ability = 0;
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Disable all support_ability components\n");
	} else {
		if (dm_value[1] == 1) { /* @enable */
			dm->support_ability |= (one << dm_value[0]);
		} else if (dm_value[1] == 2) {/* @disable */
			dm->support_ability &= ~(one << dm_value[0]);
		} else {
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "[Warning!!!]  1:enable,  2:disable\n");
		}
	}
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "pre-supportability = 0x%llx\n", pre_support_ability);
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "Cur-supportability = 0x%llx\n", dm->support_ability);
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "================================\n");

	*_used = used;
	*_out_len = out_len;
}

void phydm_watchdog_lps_32k(struct dm_struct *dm)
{
	PHYDM_DBG(dm, DBG_COMMON_FLOW, "%s ======>\n", __func__);

	phydm_common_info_self_update(dm);
	phydm_rssi_monitor_check(dm);
	phydm_dig_lps_32k(dm);
	phydm_common_info_self_reset(dm);
}

void phydm_watchdog_lps(struct dm_struct *dm)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE | ODM_IOT))
	PHYDM_DBG(dm, DBG_COMMON_FLOW, "%s ======>\n", __func__);

	phydm_common_info_self_update(dm);
	phydm_rssi_monitor_check(dm);
	phydm_basic_dbg_message(dm);
	phydm_receiver_blocking(dm);
	phydm_false_alarm_counter_statistics(dm);
	phydm_dig_by_rssi_lps(dm);
	#ifdef PHYDM_SUPPORT_CCKPD
	phydm_cck_pd_th(dm);
	#endif
	phydm_adaptivity(dm);
	#ifdef CONFIG_BW_INDICATION
	phydm_dyn_bw_indication(dm);
	#endif
	#if (DM_ODM_SUPPORT_TYPE & (ODM_CE))
	#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY
	/*@enable AntDiv in PS mode, request from SD4 Jeff*/
	odm_antenna_diversity(dm);
	#endif
	#endif
	phydm_common_info_self_reset(dm);
#endif
}

void phydm_watchdog_mp(struct dm_struct *dm)
{
}

void phydm_pause_dm_watchdog(void *dm_void, enum phydm_pause_type pause_type)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (pause_type == PHYDM_PAUSE) {
		dm->disable_phydm_watchdog = 1;
		PHYDM_DBG(dm, ODM_COMP_API, "PHYDM Stop\n");
	} else {
		dm->disable_phydm_watchdog = 0;
		PHYDM_DBG(dm, ODM_COMP_API, "PHYDM Start\n");
	}
}

u8 phydm_pause_func(void *dm_void, enum phydm_func_idx pause_func,
		    enum phydm_pause_type pause_type,
		    enum phydm_pause_level pause_lv, u8 val_lehgth,
		    u32 *val_buf)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_func_poiner *func_t = &dm->phydm_func_handler;
	s8 *pause_lv_pre = &dm->s8_dummy;
	u32 *bkp_val = &dm->u32_dummy;
	u32 ori_val[5] = {0};
	u64 pause_func_bitmap = (u64)BIT(pause_func);
	u8 i = 0;
	u8 en_2rcca = 0;
	u8 en_bw40m = 0;
	u8 pause_result = PAUSE_FAIL;

	PHYDM_DBG(dm, ODM_COMP_API, "\n");
	PHYDM_DBG(dm, ODM_COMP_API, "[%s][%s] LV=%d, Len=%d\n", __func__,
		  ((pause_type == PHYDM_PAUSE) ? "Pause" :
		  ((pause_type == PHYDM_RESUME) ? "Resume" : "Pause no_set")),
		  pause_lv, val_lehgth);

	if (pause_lv >= PHYDM_PAUSE_MAX_NUM) {
		PHYDM_DBG(dm, ODM_COMP_API, "[WARNING]Wrong LV=%d\n", pause_lv);
		return PAUSE_FAIL;
	}

	if (pause_func == F00_DIG) {
		PHYDM_DBG(dm, ODM_COMP_API, "[DIG]\n");

		if (val_lehgth != 1) {
			PHYDM_DBG(dm, ODM_COMP_API, "[WARNING] length != 1\n");
			return PAUSE_FAIL;
		}

		ori_val[0] = (u32)(dm->dm_dig_table.cur_ig_value);
		pause_lv_pre = &dm->pause_lv_table.lv_dig;
		bkp_val = (u32 *)(&dm->dm_dig_table.rvrt_val);
		/*@function pointer hook*/
		func_t->pause_phydm_handler = phydm_set_dig_val;

#ifdef PHYDM_SUPPORT_CCKPD
	} else if (pause_func == F05_CCK_PD) {
		PHYDM_DBG(dm, ODM_COMP_API, "[CCK_PD]\n");

		if (val_lehgth != 1) {
			PHYDM_DBG(dm, ODM_COMP_API, "[WARNING] length != 1\n");
			return PAUSE_FAIL;
		}

		ori_val[0] = (u32)dm->dm_cckpd_table.cck_pd_lv;
		pause_lv_pre = &dm->pause_lv_table.lv_cckpd;
		bkp_val = (u32 *)(&dm->dm_cckpd_table.rvrt_val);
		/*@function pointer hook*/
		func_t->pause_phydm_handler = phydm_set_cckpd_val;
#endif

#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY
	} else if (pause_func == F06_ANT_DIV) {
		PHYDM_DBG(dm, ODM_COMP_API, "[AntDiv]\n");

		if (val_lehgth != 1) {
			PHYDM_DBG(dm, ODM_COMP_API, "[WARNING] length != 1\n");
			return PAUSE_FAIL;
		}
		/*@default antenna*/
		ori_val[0] = (u32)(dm->dm_fat_table.rx_idle_ant);
		pause_lv_pre = &dm->pause_lv_table.lv_antdiv;
		bkp_val = (u32 *)(&dm->dm_fat_table.rvrt_val);
		/*@function pointer hook*/
		func_t->pause_phydm_handler = phydm_set_antdiv_val;

#endif
#ifdef PHYDM_SUPPORT_ADAPTIVITY
	} else if (pause_func == F13_ADPTVTY) {
		PHYDM_DBG(dm, ODM_COMP_API, "[Adaptivity]\n");

		if (val_lehgth != 2) {
			PHYDM_DBG(dm, ODM_COMP_API, "[WARNING] length != 2\n");
			return PAUSE_FAIL;
		}

		ori_val[0] = (u32)(dm->adaptivity.th_l2h); /*th_l2h*/
		ori_val[1] = (u32)(dm->adaptivity.th_h2l); /*th_h2l*/
		pause_lv_pre = &dm->pause_lv_table.lv_adapt;
		bkp_val = (u32 *)(&dm->adaptivity.rvrt_val);
		/*@function pointer hook*/
		func_t->pause_phydm_handler = phydm_set_edcca_val;

#endif
#ifdef CONFIG_ADAPTIVE_SOML
	} else if (pause_func == F17_ADPTV_SOML) {
		PHYDM_DBG(dm, ODM_COMP_API, "[AD-SOML]\n");

		if (val_lehgth != 1) {
			PHYDM_DBG(dm, ODM_COMP_API, "[WARNING] length != 1\n");
			return PAUSE_FAIL;
		}
		/*SOML_ON/OFF*/
		ori_val[0] = (u32)(dm->dm_soml_table.soml_on_off);

		pause_lv_pre = &dm->pause_lv_table.lv_adsl;
		bkp_val = (u32 *)(&dm->dm_soml_table.rvrt_val);
		 /*@function pointer hook*/
		func_t->pause_phydm_handler = phydm_set_adsl_val;

#endif
	} else {
		PHYDM_DBG(dm, ODM_COMP_API, "[WARNING] error func idx\n");
		return PAUSE_FAIL;
	}

	PHYDM_DBG(dm, ODM_COMP_API, "Pause_LV{new , pre} = {%d ,%d}\n",
		  pause_lv, *pause_lv_pre);

	if (pause_type == PHYDM_PAUSE || pause_type == PHYDM_PAUSE_NO_SET) {
		if (pause_lv <= *pause_lv_pre) {
			PHYDM_DBG(dm, ODM_COMP_API,
				  "[PAUSE FAIL] Pre_LV >= Curr_LV\n");
			return PAUSE_FAIL;
		}

		if (!(dm->pause_ability & pause_func_bitmap)) {
			for (i = 0; i < val_lehgth; i++)
				bkp_val[i] = ori_val[i];
		}

		dm->pause_ability |= pause_func_bitmap;
		PHYDM_DBG(dm, ODM_COMP_API, "pause_ability=0x%llx\n",
			  dm->pause_ability);

		if (pause_type == PHYDM_PAUSE) {
			for (i = 0; i < val_lehgth; i++)
				PHYDM_DBG(dm, ODM_COMP_API,
					  "[PAUSE SUCCESS] val_idx[%d]{New, Ori}={0x%x, 0x%x}\n",
					  i, val_buf[i], bkp_val[i]);
			func_t->pause_phydm_handler(dm, val_buf, val_lehgth);
		} else {
			for (i = 0; i < val_lehgth; i++)
				PHYDM_DBG(dm, ODM_COMP_API,
					  "[PAUSE NO Set: SUCCESS] val_idx[%d]{Ori}={0x%x}\n",
					  i, bkp_val[i]);
		}

		*pause_lv_pre = pause_lv;
		pause_result = PAUSE_SUCCESS;

	} else if (pause_type == PHYDM_RESUME) {
		if (pause_lv < *pause_lv_pre) {
			PHYDM_DBG(dm, ODM_COMP_API,
				  "[Resume FAIL] Pre_LV >= Curr_LV\n");
			return PAUSE_FAIL;
		}

		if ((dm->pause_ability & pause_func_bitmap) == 0) {
			PHYDM_DBG(dm, ODM_COMP_API,
				  "[RESUME] No Need to Revert\n");
			return PAUSE_SUCCESS;
		}

		dm->pause_ability &= ~pause_func_bitmap;
		PHYDM_DBG(dm, ODM_COMP_API, "pause_ability=0x%llx\n",
			  dm->pause_ability);

		*pause_lv_pre = PHYDM_PAUSE_RELEASE;

		for (i = 0; i < val_lehgth; i++) {
			PHYDM_DBG(dm, ODM_COMP_API,
				  "[RESUME] val_idx[%d]={0x%x}\n", i,
				  bkp_val[i]);
		}

		func_t->pause_phydm_handler(dm, bkp_val, val_lehgth);

		pause_result = PAUSE_SUCCESS;
	} else {
		PHYDM_DBG(dm, ODM_COMP_API, "[WARNING] error pause_type\n");
		pause_result = PAUSE_FAIL;
	}
	return pause_result;
}

void phydm_pause_func_console(void *dm_void, char input[][16], u32 *_used,
			      char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	char help[] = "-h";
	u32 var1[10] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 i;
	u8 length = 0;
	u32 buf[5] = {0};
	u8 set_result = 0;
	enum phydm_func_idx func = 0;
	enum phydm_pause_type type = 0;
	enum phydm_pause_level lv = 0;

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{Func} {1:pause,2:pause no set 3:Resume} {lv:0~3} Val[5:0]\n");

		goto out;
	}

	for (i = 0; i < 10; i++) {
		if (input[i + 1])
			PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);
	}

	func = (enum phydm_func_idx)var1[0];
	type = (enum phydm_pause_type)var1[1];
	lv = (enum phydm_pause_level)var1[2];

	for (i = 0; i < 5; i++)
		buf[i] = var1[3 + i];

	if (func == F00_DIG) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "[DIG]\n");
		length = 1;

	} else if (func == F05_CCK_PD) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "[CCK_PD]\n");
		length = 1;
	} else if (func == F06_ANT_DIV) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "[Ant_Div]\n");
		length = 1;
	} else if (func == F13_ADPTVTY) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "[Adaptivity]\n");
		length = 2;
	} else if (func == F17_ADPTV_SOML) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "[ADSL]\n");
		length = 1;
	} else {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "[Set Function Error]\n");
		length = 0;
	}

	if (length != 0) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{%s, lv=%d} val = %d, %d}\n",
			 ((type == PHYDM_PAUSE) ? "Pause" :
			 ((type == PHYDM_RESUME) ? "Resume" : "Pause no_set")),
			 lv, var1[3], var1[4]);

		set_result = phydm_pause_func(dm, func, type, lv, length, buf);
	}

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "set_result = %d\n", set_result);

out:
	*_used = used;
	*_out_len = out_len;
}

void phydm_pause_dm_by_asso_pkt(struct dm_struct *dm,
				enum phydm_pause_type pause_type, u8 rssi)
{
	u32 igi_val = rssi + 10;
	u32 th_buf[2];

	PHYDM_DBG(dm, ODM_COMP_API, "[%s][%s] rssi=%d\n", __func__,
		  ((pause_type == PHYDM_PAUSE) ? "Pause" :
		  ((pause_type == PHYDM_RESUME) ? "Resume" : "Pause no_set")),
		  rssi);

	if (pause_type == PHYDM_RESUME) {
		phydm_pause_func(dm, F00_DIG, PHYDM_RESUME,
				 PHYDM_PAUSE_LEVEL_1, 1, &igi_val);

		phydm_pause_func(dm, F13_ADPTVTY, PHYDM_RESUME,
				 PHYDM_PAUSE_LEVEL_1, 2, th_buf);
	} else {
		odm_write_dig(dm, (u8)igi_val);
		phydm_pause_func(dm, F00_DIG, PHYDM_PAUSE,
				 PHYDM_PAUSE_LEVEL_1, 1, &igi_val);

		th_buf[0] = 0xff;
		th_buf[1] = 0xff;

		phydm_pause_func(dm, F13_ADPTVTY, PHYDM_PAUSE,
				 PHYDM_PAUSE_LEVEL_1, 2, th_buf);
	}
}

u8 phydm_stop_dm_watchdog_check(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->disable_phydm_watchdog == 1) {
		PHYDM_DBG(dm, DBG_COMMON_FLOW, "Disable phydm\n");
		return true;
	} else {
		return false;
	}
}

void phydm_watchdog(struct dm_struct *dm)
{
	PHYDM_DBG(dm, DBG_COMMON_FLOW, "%s ======>\n", __func__);

	phydm_common_info_self_update(dm);
	phydm_phy_info_update(dm);
	phydm_rssi_monitor_check(dm);
	phydm_basic_dbg_message(dm);
	phydm_dm_summary(dm, FIRST_MACID);
#ifdef PHYDM_AUTO_DEGBUG
	phydm_auto_dbg_engine(dm);
#endif
	phydm_receiver_blocking(dm);

	if (phydm_stop_dm_watchdog_check(dm) == true)
		return;

	phydm_hw_setting(dm);

	#ifdef PHYDM_TDMA_DIG_SUPPORT
	if (dm->original_dig_restore == 0)
		phydm_tdma_dig_timer_check(dm);
	else
	#endif
	{
		phydm_false_alarm_counter_statistics(dm);
		phydm_noisy_detection(dm);
		phydm_dig(dm);
		#ifdef PHYDM_SUPPORT_CCKPD
		phydm_cck_pd_th(dm);
		#endif
	}

#ifdef PHYDM_POWER_TRAINING_SUPPORT
	phydm_update_power_training_state(dm);
#endif
	phydm_adaptivity(dm);
	phydm_ra_info_watchdog(dm);
#ifdef CONFIG_PATH_DIVERSITY
	phydm_tx_path_diversity(dm);
#endif
	phydm_cfo_tracking(dm);
#ifdef CONFIG_DYNAMIC_TX_TWR
	phydm_dynamic_tx_power(dm);
#endif
#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY
	odm_antenna_diversity(dm);
#endif
#ifdef CONFIG_ADAPTIVE_SOML
	phydm_adaptive_soml(dm);
#endif

#ifdef PHYDM_BEAMFORMING_VERSION1
	phydm_beamforming_watchdog(dm);
#endif

	halrf_watchdog(dm);
#ifdef PHYDM_PRIMARY_CCA
	phydm_primary_cca(dm);
#endif
#ifdef CONFIG_BW_INDICATION
	phydm_dyn_bw_indication(dm);
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	odm_dtc(dm);
#endif

	phydm_env_mntr_watchdog(dm);

#ifdef PHYDM_LNA_SAT_CHK_SUPPORT
	phydm_lna_sat_chk_watchdog(dm);
#endif

#ifdef CONFIG_MCC_DM
	phydm_mcc_switch(dm);
#endif

#ifdef CONFIG_MU_RSOML
	phydm_mu_rsoml_decision(dm);
#endif

	phydm_common_info_self_reset(dm);
}

/*@
 * Init /.. Fixed HW value. Only init time.
 */
void odm_cmn_info_init(struct dm_struct *dm, enum odm_cmninfo cmn_info,
		       u64 value)
{
	/* This section is used for init value */
	switch (cmn_info) {
	/* @Fixed ODM value. */
	case ODM_CMNINFO_ABILITY:
		dm->support_ability = (u64)value;
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
		dm->support_ic_type = (u32)value;
		break;

	case ODM_CMNINFO_CUT_VER:
		dm->cut_version = (u8)value;
		break;

	case ODM_CMNINFO_FAB_VER:
		dm->fab_version = (u8)value;
		break;
	case ODM_CMNINFO_FW_VER:
		dm->fw_version = (u8)value;
		break;
	case ODM_CMNINFO_FW_SUB_VER:
		dm->fw_sub_version = (u8)value;
		break;
	case ODM_CMNINFO_RFE_TYPE:
#if (RTL8821C_SUPPORT)
		if (dm->support_ic_type & ODM_RTL8821C)
			dm->rfe_type_expand = (u8)value;
		else
#endif
			dm->rfe_type = (u8)value;

#ifdef CONFIG_RFE_BY_HW_INFO
		phydm_init_hw_info_by_rfe(dm);
#endif
		break;

	case ODM_CMNINFO_RF_ANTENNA_TYPE:
		dm->ant_div_type = (u8)value;
		break;

	case ODM_CMNINFO_WITH_EXT_ANTENNA_SWITCH:
		dm->with_extenal_ant_switch = (u8)value;
		break;

#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY
	case ODM_CMNINFO_BE_FIX_TX_ANT:
		dm->dm_fat_table.b_fix_tx_ant = (u8)value;
		break;
#endif

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
		dm->iot_table.win_patch_id = (u8)value;
		break;
	case ODM_CMNINFO_BINHCT_TEST:
		dm->is_in_hct_test = (boolean)value;
		break;
	case ODM_CMNINFO_BWIFI_TEST:
		dm->wifi_test = (u8)value;
		break;
	case ODM_CMNINFO_SMART_CONCURRENT:
		dm->is_dual_mac_smart_concurrent = (boolean)value;
		break;
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	case ODM_CMNINFO_CONFIG_BB_RF:
		dm->config_bbrf = (boolean)value;
		break;
#endif
	case ODM_CMNINFO_IQKPAOFF:
		dm->rf_calibrate_info.is_iqk_pa_off = (boolean)value;
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
	case ODM_CMNINFO_VALID_PATH_SET:
		dm->valid_path_set = (u8)value;
		break;
	case ODM_CMNINFO_EFUSE0X3D8:
		dm->efuse0x3d8 = (u8)value;
		break;
	case ODM_CMNINFO_EFUSE0X3D7:
		dm->efuse0x3d7 = (u8)value;
		break;
	case ODM_CMNINFO_ADVANCE_OTA:
		dm->p_advance_ota = (u8)value;
		break;

#ifdef CONFIG_PHYDM_DFS_MASTER
	case ODM_CMNINFO_DFS_REGION_DOMAIN:
		dm->dfs_region_domain = (u8)value;
		break;
#endif
	case ODM_CMNINFO_SOFT_AP_SPECIAL_SETTING:
		dm->soft_ap_special_setting = (u32)value;
		break;

	case ODM_CMNINFO_X_CAP_SETTING:
		dm->dm_cfo_track.crystal_cap_default = (u8)value;
		break;

	case ODM_CMNINFO_DPK_EN:
		/*@dm->dpk_en = (u1Byte)value;*/
		halrf_cmn_info_set(dm, HALRF_CMNINFO_DPK_EN, (u64)value);
		break;

	case ODM_CMNINFO_HP_HWID:
		dm->hp_hw_id = (boolean)value;
		break;
	case ODM_CMNINFO_TSSI_ENABLE:
		dm->en_tssi_mode = (u8)value;
		break;
	case ODM_CMNINFO_DIS_DPD:
		dm->en_dis_dpd = (boolean)value;
		break;
	case ODM_CMNINFO_EN_AUTO_BW_TH:
		dm->en_auto_bw_th = (u8)value;
		break;
#if (RTL8721D_SUPPORT)
	case ODM_CMNINFO_POWER_VOLTAGE:
		dm->power_voltage = (u8)value;
		break;
	case ODM_CMNINFO_ANTDIV_GPIO:
		dm->antdiv_gpio = (u8)value;
		break;
#endif
	default:
		break;
	}
}

void odm_cmn_info_hook(struct dm_struct *dm, enum odm_cmninfo cmn_info,
		       void *value)
{
	/* @Hook call by reference pointer. */
	switch (cmn_info) {
	/* @Dynamic call by reference pointer. */
	case ODM_CMNINFO_TX_UNI:
		dm->num_tx_bytes_unicast = (u64 *)value;
		break;

	case ODM_CMNINFO_RX_UNI:
		dm->num_rx_bytes_unicast = (u64 *)value;
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

	case ODM_CMNINFO_SCAN:
		dm->is_scan_in_process = (boolean *)value;
		break;

	case ODM_CMNINFO_POWER_SAVING:
		dm->is_power_saving = (boolean *)value;
		break;

	case ODM_CMNINFO_TDMA:
		dm->is_tdma = (boolean *)value;
		break;

	case ODM_CMNINFO_ONE_PATH_CCA:
		dm->one_path_cca = (u8 *)value;
		break;

	case ODM_CMNINFO_DRV_STOP:
		dm->is_driver_stopped = (boolean *)value;
		break;
	case ODM_CMNINFO_INIT_ON:
		dm->pinit_adpt_in_progress = (boolean *)value;
		break;

	case ODM_CMNINFO_ANT_TEST:
		dm->antenna_test = (u8 *)value;
		break;

	case ODM_CMNINFO_NET_CLOSED:
		dm->is_net_closed = (boolean *)value;
		break;

	case ODM_CMNINFO_FORCED_RATE:
		dm->forced_data_rate = (u16 *)value;
		break;
	case ODM_CMNINFO_ANT_DIV:
		dm->enable_antdiv = (u8 *)value;
		break;
	case ODM_CMNINFO_PATH_DIV:
		dm->enable_pathdiv = (u8 *)value;
		break;
	case ODM_CMNINFO_ADAPTIVE_SOML:
		dm->en_adap_soml = (u8 *)value;
		break;
	case ODM_CMNINFO_ADAPTIVITY:
		dm->edcca_mode = (u8 *)value;
		break;

	case ODM_CMNINFO_P2P_LINK:
		dm->dm_dig_table.is_p2p_in_process = (u8 *)value;
		break;

	case ODM_CMNINFO_IS1ANTENNA:
		dm->is_1_antenna = (boolean *)value;
		break;

	case ODM_CMNINFO_RFDEFAULTPATH:
		dm->rf_default_path = (u8 *)value;
		break;

	case ODM_CMNINFO_FCS_MODE: /* @fast channel switch (= MCC mode)*/
		dm->is_fcs_mode_enable = (boolean *)value;
		break;

	case ODM_CMNINFO_HUBUSBMODE:
		dm->hub_usb_mode = (u8 *)value;
		break;
	case ODM_CMNINFO_FWDWRSVDPAGEINPROGRESS:
		dm->is_fw_dw_rsvd_page_in_progress = (boolean *)value;
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
#ifdef CONFIG_PHYDM_DFS_MASTER
	case ODM_CMNINFO_DFS_MASTER_ENABLE:
		dm->dfs_master_enabled = (u8 *)value;
		break;
#endif

#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY
	case ODM_CMNINFO_FORCE_TX_ANT_BY_TXDESC:
		dm->dm_fat_table.p_force_tx_by_desc = (u8 *)value;
		break;
	case ODM_CMNINFO_SET_S0S1_DEFAULT_ANTENNA:
		dm->dm_fat_table.p_default_s0_s1 = (u8 *)value;
		break;
	case ODM_CMNINFO_BF_ANTDIV_DECISION:
		dm->dm_fat_table.is_no_csi_feedback = (boolean *)value;
		break;
#endif

	case ODM_CMNINFO_SOFT_AP_MODE:
		dm->soft_ap_mode = (u32 *)value;
		break;
	case ODM_CMNINFO_MP_MODE:
		dm->mp_mode = (u8 *)value;
		break;
	case ODM_CMNINFO_INTERRUPT_MASK:
		dm->interrupt_mask = (u32 *)value;
		break;
	case ODM_CMNINFO_BB_OPERATION_MODE:
		dm->bb_op_mode = (u8 *)value;
		break;
	case ODM_CMNINFO_MANUAL_SUPPORTABILITY:
		dm->manual_supportability = (u32 *)value;
		break;
	case ODM_CMNINFO_EN_DYM_BW_INDICATION:
		dm->dis_dym_bw_indication = (u8 *)value;
	default:
		/*do nothing*/
		break;
	}
}

/*@
 * Update band/CHannel/.. The values are dynamic but non-per-packet.
 */
void odm_cmn_info_update(struct dm_struct *dm, u32 cmn_info, u64 value)
{
	/* This init variable may be changed in run time. */
	switch (cmn_info) {
	case ODM_CMNINFO_LINK_IN_PROGRESS:
		dm->is_link_in_process = (boolean)value;
		break;

	case ODM_CMNINFO_ABILITY:
		dm->support_ability = (u64)value;
		break;

	case ODM_CMNINFO_RF_TYPE:
		dm->rf_type = (u8)value;
		break;

	case ODM_CMNINFO_WIFI_DIRECT:
		dm->is_wifi_direct = (boolean)value;
		break;

	case ODM_CMNINFO_WIFI_DISPLAY:
		dm->is_wifi_display = (boolean)value;
		break;

	case ODM_CMNINFO_LINK:
		dm->is_linked = (boolean)value;
		break;

	case ODM_CMNINFO_CMW500LINK:
		dm->iot_table.is_linked_cmw500 = (boolean)value;
		break;

	case ODM_CMNINFO_STATION_STATE:
		dm->bsta_state = (boolean)value;
		break;

	case ODM_CMNINFO_RSSI_MIN:
		dm->rssi_min = (u8)value;
		break;

	case ODM_CMNINFO_RSSI_MIN_BY_PATH:
		dm->rssi_min_by_path = (u8)value;
		break;

	case ODM_CMNINFO_DBG_COMP:
		dm->debug_components = (u64)value;
		break;

#ifdef ODM_CONFIG_BT_COEXIST
	/* The following is for BT HS mode and BT coexist mechanism. */
	case ODM_CMNINFO_BT_ENABLED:
		dm->bt_info_table.is_bt_enabled = (boolean)value;
		break;

	case ODM_CMNINFO_BT_HS_CONNECT_PROCESS:
		dm->bt_info_table.is_bt_connect_process = (boolean)value;
		break;

	case ODM_CMNINFO_BT_HS_RSSI:
		dm->bt_info_table.bt_hs_rssi = (u8)value;
		break;

	case ODM_CMNINFO_BT_OPERATION:
		dm->bt_info_table.is_bt_hs_operation = (boolean)value;
		break;

	case ODM_CMNINFO_BT_LIMITED_DIG:
		dm->bt_info_table.is_bt_limited_dig = (boolean)value;
		break;
#endif

	case ODM_CMNINFO_AP_TOTAL_NUM:
		dm->ap_total_num = (u8)value;
		break;

#ifdef CONFIG_PHYDM_DFS_MASTER
	case ODM_CMNINFO_DFS_REGION_DOMAIN:
		dm->dfs_region_domain = (u8)value;
		break;
#endif

	case ODM_CMNINFO_BT_CONTINUOUS_TURN:
		dm->is_bt_continuous_turn = (boolean)value;
		break;
	case ODM_CMNINFO_IS_DOWNLOAD_FW:
		dm->is_download_fw = (boolean)value;
		break;
	case ODM_CMNINFO_PHYDM_PATCH_ID:
		dm->iot_table.phydm_patch_id = (u32)value;
		break;
	case ODM_CMNINFO_RRSR_VAL:
		dm->dm_ra_table.rrsr_val_init = (u32)value;
		break;
	case ODM_CMNINFO_LINKED_BF_SUPPORT:
		dm->linked_bf_support = (u8)value;
		break;
	default:
		break;
	}
}

u32 phydm_cmn_info_query(struct dm_struct *dm, enum phydm_info_query info_type)
{
	struct phydm_fa_struct *fa_t = &dm->false_alm_cnt;
	struct phydm_dig_struct *dig_t = &dm->dm_dig_table;
	struct ccx_info *ccx_info = &dm->dm_ccx_info;

	switch (info_type) {
	/*@=== [FA Relative] ===========================================*/
	case PHYDM_INFO_FA_OFDM:
		return fa_t->cnt_ofdm_fail;

	case PHYDM_INFO_FA_CCK:
		return fa_t->cnt_cck_fail;

	case PHYDM_INFO_FA_TOTAL:
		return fa_t->cnt_all;

	case PHYDM_INFO_CCA_OFDM:
		return fa_t->cnt_ofdm_cca;

	case PHYDM_INFO_CCA_CCK:
		return fa_t->cnt_cck_cca;

	case PHYDM_INFO_CCA_ALL:
		return fa_t->cnt_cca_all;

	case PHYDM_INFO_CRC32_OK_VHT:
		return fa_t->cnt_vht_crc32_ok;

	case PHYDM_INFO_CRC32_OK_HT:
		return fa_t->cnt_ht_crc32_ok;

	case PHYDM_INFO_CRC32_OK_LEGACY:
		return fa_t->cnt_ofdm_crc32_ok;

	case PHYDM_INFO_CRC32_OK_CCK:
		return fa_t->cnt_cck_crc32_ok;

	case PHYDM_INFO_CRC32_ERROR_VHT:
		return fa_t->cnt_vht_crc32_error;

	case PHYDM_INFO_CRC32_ERROR_HT:
		return fa_t->cnt_ht_crc32_error;

	case PHYDM_INFO_CRC32_ERROR_LEGACY:
		return fa_t->cnt_ofdm_crc32_error;

	case PHYDM_INFO_CRC32_ERROR_CCK:
		return fa_t->cnt_cck_crc32_error;

	case PHYDM_INFO_EDCCA_FLAG:
		return fa_t->edcca_flag;

	case PHYDM_INFO_OFDM_ENABLE:
		return fa_t->ofdm_block_enable;

	case PHYDM_INFO_CCK_ENABLE:
		return fa_t->cck_block_enable;

	case PHYDM_INFO_DBG_PORT_0:
		return fa_t->dbg_port0;

	case PHYDM_INFO_CRC32_OK_HT_AGG:
		return fa_t->cnt_ht_crc32_ok_agg;

	case PHYDM_INFO_CRC32_ERROR_HT_AGG:
		return fa_t->cnt_ht_crc32_error_agg;

	/*@=== [DIG] ================================================*/

	case PHYDM_INFO_CURR_IGI:
		return dig_t->cur_ig_value;

	/*@=== [RSSI] ===============================================*/
	case PHYDM_INFO_RSSI_MIN:
		return (u32)dm->rssi_min;

	case PHYDM_INFO_RSSI_MAX:
		return (u32)dm->rssi_max;

	case PHYDM_INFO_CLM_RATIO:
		return (u32)ccx_info->clm_ratio;
	case PHYDM_INFO_NHM_RATIO:
		return (u32)ccx_info->nhm_ratio;
	case PHYDM_INFO_NHM_NOISE_PWR:
		return (u32)ccx_info->nhm_level;
	default:
		return 0xffffffff;
	}
}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void odm_init_all_work_items(struct dm_struct *dm)
{
	void *adapter = dm->adapter;
#if USE_WORKITEM

#ifdef CONFIG_ADAPTIVE_SOML
	odm_initialize_work_item(dm,
				 &dm->dm_soml_table.phydm_adaptive_soml_workitem,
				 (RT_WORKITEM_CALL_BACK)phydm_adaptive_soml_workitem_callback,
				 (void *)adapter,
				 "AdaptiveSOMLWorkitem");
#endif

#ifdef ODM_EVM_ENHANCE_ANTDIV
	odm_initialize_work_item(dm,
				 &dm->phydm_evm_antdiv_workitem,
				 (RT_WORKITEM_CALL_BACK)phydm_evm_antdiv_workitem_callback,
				 (void *)adapter,
				 "EvmAntdivWorkitem");
#endif

#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
	odm_initialize_work_item(dm,
				 &dm->dm_swat_table.phydm_sw_antenna_switch_workitem,
				 (RT_WORKITEM_CALL_BACK)odm_sw_antdiv_workitem_callback,
				 (void *)adapter,
				 "AntennaSwitchWorkitem");
#endif
#if (defined(CONFIG_HL_SMART_ANTENNA))
	odm_initialize_work_item(dm,
				 &dm->dm_sat_table.hl_smart_antenna_workitem,
				 (RT_WORKITEM_CALL_BACK)phydm_beam_switch_workitem_callback,
				 (void *)adapter,
				 "hl_smart_ant_workitem");

	odm_initialize_work_item(dm,
				 &dm->dm_sat_table.hl_smart_antenna_decision_workitem,
				 (RT_WORKITEM_CALL_BACK)phydm_beam_decision_workitem_callback,
				 (void *)adapter,
				 "hl_smart_ant_decision_workitem");
#endif

	odm_initialize_work_item(
		dm,
		&dm->ra_rpt_workitem,
		(RT_WORKITEM_CALL_BACK)halrf_update_init_rate_work_item_callback,
		(void *)adapter,
		"ra_rpt_workitem");

#if (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) || (defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
	odm_initialize_work_item(
		dm,
		&dm->fast_ant_training_workitem,
		(RT_WORKITEM_CALL_BACK)odm_fast_ant_training_work_item_callback,
		(void *)adapter,
		"fast_ant_training_workitem");
#endif

#endif /*#if USE_WORKITEM*/

#ifdef PHYDM_BEAMFORMING_SUPPORT
	odm_initialize_work_item(
		dm,
		&dm->beamforming_info.txbf_info.txbf_enter_work_item,
		(RT_WORKITEM_CALL_BACK)hal_com_txbf_enter_work_item_callback,
		(void *)adapter,
		"txbf_enter_work_item");

	odm_initialize_work_item(
		dm,
		&dm->beamforming_info.txbf_info.txbf_leave_work_item,
		(RT_WORKITEM_CALL_BACK)hal_com_txbf_leave_work_item_callback,
		(void *)adapter,
		"txbf_leave_work_item");

	odm_initialize_work_item(
		dm,
		&dm->beamforming_info.txbf_info.txbf_fw_ndpa_work_item,
		(RT_WORKITEM_CALL_BACK)hal_com_txbf_fw_ndpa_work_item_callback,
		(void *)adapter,
		"txbf_fw_ndpa_work_item");

	odm_initialize_work_item(
		dm,
		&dm->beamforming_info.txbf_info.txbf_clk_work_item,
		(RT_WORKITEM_CALL_BACK)hal_com_txbf_clk_work_item_callback,
		(void *)adapter,
		"txbf_clk_work_item");

	odm_initialize_work_item(
		dm,
		&dm->beamforming_info.txbf_info.txbf_rate_work_item,
		(RT_WORKITEM_CALL_BACK)hal_com_txbf_rate_work_item_callback,
		(void *)adapter,
		"txbf_rate_work_item");

	odm_initialize_work_item(
		dm,
		&dm->beamforming_info.txbf_info.txbf_status_work_item,
		(RT_WORKITEM_CALL_BACK)hal_com_txbf_status_work_item_callback,
		(void *)adapter,
		"txbf_status_work_item");

	odm_initialize_work_item(
		dm,
		&dm->beamforming_info.txbf_info.txbf_reset_tx_path_work_item,
		(RT_WORKITEM_CALL_BACK)hal_com_txbf_reset_tx_path_work_item_callback,
		(void *)adapter,
		"txbf_reset_tx_path_work_item");

	odm_initialize_work_item(
		dm,
		&dm->beamforming_info.txbf_info.txbf_get_tx_rate_work_item,
		(RT_WORKITEM_CALL_BACK)hal_com_txbf_get_tx_rate_work_item_callback,
		(void *)adapter,
		"txbf_get_tx_rate_work_item");
#endif

#if (PHYDM_LA_MODE_SUPPORT == 1)
	odm_initialize_work_item(
		dm,
		&dm->adcsmp.adc_smp_work_item,
		(RT_WORKITEM_CALL_BACK)adc_smp_work_item_callback,
		(void *)adapter,
		"adc_smp_work_item");

	odm_initialize_work_item(
		dm,
		&dm->adcsmp.adc_smp_work_item_1,
		(RT_WORKITEM_CALL_BACK)adc_smp_work_item_callback,
		(void *)adapter,
		"adc_smp_work_item_1");
#endif
}

void odm_free_all_work_items(struct dm_struct *dm)
{
#if USE_WORKITEM

#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
	odm_free_work_item(&dm->dm_swat_table.phydm_sw_antenna_switch_workitem);
#endif

#ifdef CONFIG_ADAPTIVE_SOML
	odm_free_work_item(&dm->dm_soml_table.phydm_adaptive_soml_workitem);
#endif

#ifdef ODM_EVM_ENHANCE_ANTDIV
	odm_free_work_item(&dm->phydm_evm_antdiv_workitem);
#endif

#if (defined(CONFIG_HL_SMART_ANTENNA))
	odm_free_work_item(&dm->dm_sat_table.hl_smart_antenna_workitem);
	odm_free_work_item(&dm->dm_sat_table.hl_smart_antenna_decision_workitem);
#endif

#if (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) || (defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
	odm_free_work_item(&dm->fast_ant_training_workitem);
#endif
	odm_free_work_item(&dm->ra_rpt_workitem);
/*odm_free_work_item((&dm->sbdcnt_workitem));*/
#endif

#ifdef PHYDM_BEAMFORMING_SUPPORT
	odm_free_work_item((&dm->beamforming_info.txbf_info.txbf_enter_work_item));
	odm_free_work_item((&dm->beamforming_info.txbf_info.txbf_leave_work_item));
	odm_free_work_item((&dm->beamforming_info.txbf_info.txbf_fw_ndpa_work_item));
	odm_free_work_item((&dm->beamforming_info.txbf_info.txbf_clk_work_item));
	odm_free_work_item((&dm->beamforming_info.txbf_info.txbf_rate_work_item));
	odm_free_work_item((&dm->beamforming_info.txbf_info.txbf_status_work_item));
	odm_free_work_item((&dm->beamforming_info.txbf_info.txbf_reset_tx_path_work_item));
	odm_free_work_item((&dm->beamforming_info.txbf_info.txbf_get_tx_rate_work_item));
#endif

#if (PHYDM_LA_MODE_SUPPORT == 1)
	odm_free_work_item((&dm->adcsmp.adc_smp_work_item));
	odm_free_work_item((&dm->adcsmp.adc_smp_work_item_1));
#endif
}
#endif /*#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)*/

void odm_init_all_timers(struct dm_struct *dm)
{
#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	odm_ant_div_timers(dm, INIT_ANTDIV_TIMMER);
#endif
#if (defined(PHYDM_TDMA_DIG_SUPPORT))
#ifdef IS_USE_NEW_TDMA
	phydm_tdma_dig_timers(dm, INIT_TDMA_DIG_TIMMER);
#endif
#endif
#ifdef CONFIG_ADAPTIVE_SOML
	phydm_adaptive_soml_timers(dm, INIT_SOML_TIMMER);
#endif
#ifdef PHYDM_LNA_SAT_CHK_SUPPORT
#ifdef PHYDM_LNA_SAT_CHK_TYPE1
	phydm_lna_sat_chk_timers(dm, INIT_LNA_SAT_CHK_TIMMER);
#endif
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	odm_initialize_timer(dm, &dm->sbdcnt_timer,
			     (void *)phydm_sbd_callback, NULL, "SbdTimer");
#ifdef PHYDM_BEAMFORMING_SUPPORT
	odm_initialize_timer(dm, &dm->beamforming_info.txbf_info.txbf_fw_ndpa_timer,
			     (void *)hal_com_txbf_fw_ndpa_timer_callback, NULL,
			     "txbf_fw_ndpa_timer");
#endif
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#ifdef PHYDM_BEAMFORMING_SUPPORT
	odm_initialize_timer(dm, &dm->beamforming_info.beamforming_timer,
			     (void *)beamforming_sw_timer_callback, NULL,
			     "beamforming_timer");
#endif
#endif
}

void odm_cancel_all_timers(struct dm_struct *dm)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	/* @2012/01/12 MH Temp BSOD fix. We need to find NIC allocate mem fail reason in win7*/
	if (dm->adapter == NULL)
		return;
#endif

#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	odm_ant_div_timers(dm, CANCEL_ANTDIV_TIMMER);
#endif
#ifdef PHYDM_TDMA_DIG_SUPPORT
#ifdef IS_USE_NEW_TDMA
	phydm_tdma_dig_timers(dm, CANCEL_TDMA_DIG_TIMMER);
#endif
#endif
#ifdef CONFIG_ADAPTIVE_SOML
	phydm_adaptive_soml_timers(dm, CANCEL_SOML_TIMMER);
#endif
#ifdef PHYDM_LNA_SAT_CHK_SUPPORT
#ifdef PHYDM_LNA_SAT_CHK_TYPE1
	phydm_lna_sat_chk_timers(dm, CANCEL_LNA_SAT_CHK_TIMMER);
#endif
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	odm_cancel_timer(dm, &dm->sbdcnt_timer);
#ifdef PHYDM_BEAMFORMING_SUPPORT
	odm_cancel_timer(dm, &dm->beamforming_info.txbf_info.txbf_fw_ndpa_timer);
#endif
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#ifdef PHYDM_BEAMFORMING_SUPPORT
	odm_cancel_timer(dm, &dm->beamforming_info.beamforming_timer);
#endif
#endif
}

void odm_release_all_timers(struct dm_struct *dm)
{
#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	odm_ant_div_timers(dm, RELEASE_ANTDIV_TIMMER);
#endif
#ifdef PHYDM_TDMA_DIG_SUPPORT
#ifdef IS_USE_NEW_TDMA
	phydm_tdma_dig_timers(dm, RELEASE_TDMA_DIG_TIMMER);
#endif
#endif
#ifdef CONFIG_ADAPTIVE_SOML
	phydm_adaptive_soml_timers(dm, RELEASE_SOML_TIMMER);
#endif
#ifdef PHYDM_LNA_SAT_CHK_SUPPORT
#ifdef PHYDM_LNA_SAT_CHK_TYPE1
	phydm_lna_sat_chk_timers(dm, RELEASE_LNA_SAT_CHK_TIMMER);
#endif
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	odm_release_timer(dm, &dm->sbdcnt_timer);
#ifdef PHYDM_BEAMFORMING_SUPPORT
	odm_release_timer(dm, &dm->beamforming_info.txbf_info.txbf_fw_ndpa_timer);
#endif
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#ifdef PHYDM_BEAMFORMING_SUPPORT
	odm_release_timer(dm, &dm->beamforming_info.beamforming_timer);
#endif
#endif
}

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
void odm_init_all_threads(
	struct dm_struct *dm)
{
#ifdef TPT_THREAD
	k_tpt_task_init(dm->priv);
#endif
}

void odm_stop_all_threads(
	struct dm_struct *dm)
{
#ifdef TPT_THREAD
	k_tpt_task_stop(dm->priv);
#endif
}
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
/* @Justin: According to the current RRSI to adjust Response Frame TX power,
 * 2012/11/05
 */
void odm_dtc(struct dm_struct *dm)
{
#ifdef CONFIG_DM_RESP_TXAGC
/* RSSI higher than this value, start to decade TX power */
#define DTC_BASE 35

/* RSSI lower than this value, start to increase TX power */
#define DTC_DWN_BASE (DTC_BASE - 5)

	/* RSSI vs TX power step mapping: decade TX power */
	static const u8 dtc_table_down[] = {
		DTC_BASE,
		(DTC_BASE + 5),
		(DTC_BASE + 10),
		(DTC_BASE + 15),
		(DTC_BASE + 20),
		(DTC_BASE + 25)};

	/* RSSI vs TX power step mapping: increase TX power */
	static const u8 dtc_table_up[] = {
		DTC_DWN_BASE,
		(DTC_DWN_BASE - 5),
		(DTC_DWN_BASE - 10),
		(DTC_DWN_BASE - 15),
		(DTC_DWN_BASE - 15),
		(DTC_DWN_BASE - 20),
		(DTC_DWN_BASE - 20),
		(DTC_DWN_BASE - 25),
		(DTC_DWN_BASE - 25),
		(DTC_DWN_BASE - 30),
		(DTC_DWN_BASE - 35)};

	u8 i;
	u8 dtc_steps = 0;
	u8 sign;
	u8 resp_txagc = 0;

	if (dm->rssi_min > DTC_BASE) {
		/* need to decade the CTS TX power */
		sign = 1;
		for (i = 0; i < ARRAY_SIZE(dtc_table_down); i++) {
			if (dtc_table_down[i] >= dm->rssi_min || dtc_steps >= 6)
				break;
			else
				dtc_steps++;
		}
	}
#if 0
	else if (dm->rssi_min > DTC_DWN_BASE) {
		/* needs to increase the CTS TX power */
		sign = 0;
		dtc_steps = 1;
		for (i = 0; i < ARRAY_SIZE(dtc_table_up); i++) {
			if (dtc_table_up[i] <= dm->rssi_min || dtc_steps >= 10)
				break;
			else
				dtc_steps++;
		}
	}
#endif
	else {
		sign = 0;
		dtc_steps = 0;
	}

	resp_txagc = dtc_steps | (sign << 4);
	resp_txagc = resp_txagc | (resp_txagc << 5);
	odm_write_1byte(dm, 0x06d9, resp_txagc);

	PHYDM_DBG(dm, ODM_COMP_PWR_TRAIN,
		  "%s rssi_min:%u, set RESP_TXAGC to %s %u\n", __func__,
		  dm->rssi_min, sign ? "minus" : "plus", dtc_steps);
#endif /* @CONFIG_RESP_TXAGC_ADJUST */
}

#endif /* @#if (DM_ODM_SUPPORT_TYPE == ODM_CE) */

/*@<20170126, BB-Kevin>8188F D-CUT DC cancellation and 8821C*/
void phydm_dc_cancellation(struct dm_struct *dm)
{
#ifdef PHYDM_DC_CANCELLATION
	u32 offset_i_hex[PHYDM_MAX_RF_PATH] = {0};
	u32 offset_q_hex[PHYDM_MAX_RF_PATH] = {0};
	u32 reg_value32[PHYDM_MAX_RF_PATH] = {0};
	u8 path = RF_PATH_A;
	u8 set_result;

	if (!(dm->support_ic_type & ODM_DC_CANCELLATION_SUPPORT))
		return;
	if ((dm->support_ic_type & ODM_RTL8188F) &&
	    dm->cut_version < ODM_CUT_D)
		return;
	if ((dm->support_ic_type & ODM_RTL8192F) &&
	    dm->cut_version == ODM_CUT_A)
		return;
	if (*dm->band_width == CHANNEL_WIDTH_5)
		return;
	if (*dm->band_width == CHANNEL_WIDTH_10)
		return;

	PHYDM_DBG(dm, ODM_COMP_API, "%s ======>\n", __func__);

	/*@DC_Estimation (only for 2x2 ic now) */

	for (path = RF_PATH_A; path < PHYDM_MAX_RF_PATH; path++) {
		if (path > RF_PATH_A &&
		    dm->support_ic_type & (ODM_RTL8821C | ODM_RTL8188F |
					  ODM_RTL8710B | ODM_RTL8721D |
					  ODM_RTL8710C | ODM_RTL8723D))
			break;
		else if (path > RF_PATH_B &&
			 dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8192F))
			break;
		if (phydm_stop_ic_trx(dm, PHYDM_SET) == PHYDM_SET_FAIL) {
			PHYDM_DBG(dm, ODM_COMP_API, "STOP_TRX_FAIL\n");
			return;
		}
		odm_write_dig(dm, 0x7e);
		/*@Disable LNA*/
		if (dm->support_ic_type & ODM_RTL8821C)
			halrf_rf_lna_setting(dm, HALRF_LNA_DISABLE);
		/*Turn off 3-wire*/
		phydm_stop_3_wire(dm, PHYDM_SET);
		if (dm->support_ic_type & (ODM_RTL8188F | ODM_RTL8723D |
			ODM_RTL8710B)) {
			/*set debug port to 0x235*/
			if (!phydm_set_bb_dbg_port(dm, DBGPORT_PRI_1, 0x235)) {
				PHYDM_DBG(dm, ODM_COMP_API,
					  "Set Debug port Fail\n");
				return;
			}
		} else if (dm->support_ic_type & (ODM_RTL8721D |
			ODM_RTL8710C)) {
			/*set debug port to 0x200*/
			if (!phydm_set_bb_dbg_port(dm, DBGPORT_PRI_2, 0x200)) {
				PHYDM_DBG(dm, ODM_COMP_API,
					  "Set Debug port Fail\n");
				return;
			}
		} else if (dm->support_ic_type & ODM_RTL8821C) {
			if (!phydm_set_bb_dbg_port(dm, DBGPORT_PRI_1, 0x200)) {
				/*set debug port to 0x200*/
				PHYDM_DBG(dm, ODM_COMP_API,
					  "Set Debug port Fail\n");
				return;
			}
			phydm_bb_dbg_port_header_sel(dm, 0x0);
		} else if (dm->support_ic_type & ODM_RTL8822B) {
			if (path == RF_PATH_A &&
			    !phydm_set_bb_dbg_port(dm, DBGPORT_PRI_1, 0x200)) {
				/*set debug port to 0x200*/
				PHYDM_DBG(dm, ODM_COMP_API,
					  "Set Debug port Fail\n");
				return;
			}
			if (path == RF_PATH_B &&
			    !phydm_set_bb_dbg_port(dm, DBGPORT_PRI_1, 0x202)) {
				/*set debug port to 0x200*/
				PHYDM_DBG(dm, ODM_COMP_API,
					  "Set Debug port Fail\n");
				return;
			}
			phydm_bb_dbg_port_header_sel(dm, 0x0);
		} else if (dm->support_ic_type & ODM_RTL8192F) {
			if (path == RF_PATH_A &&
			    !phydm_set_bb_dbg_port(dm, DBGPORT_PRI_1, 0x235)) {
				/*set debug port to 0x235*/
				PHYDM_DBG(dm, ODM_COMP_API,
					  "Set Debug port Fail\n");
				return;
			}
			if (path == RF_PATH_B &&
			    !phydm_set_bb_dbg_port(dm, DBGPORT_PRI_1, 0x23d)) {
				/*set debug port to 0x23d*/
				PHYDM_DBG(dm, ODM_COMP_API,
					  "Set Debug port Fail\n");
				return;
			}
		}

		/*@disable CCK DCNF*/
		odm_set_bb_reg(dm, R_0xa78, MASKBYTE1, 0x0);

		PHYDM_DBG(dm, ODM_COMP_API, "DC cancellation Begin!!!\n");

		phydm_stop_ck320(dm, true); /*stop ck320*/

		/* the same debug port both for path-a and path-b*/
		reg_value32[path] = phydm_get_bb_dbg_port_val(dm);

		phydm_stop_ck320(dm, false); /*start ck320*/

		phydm_release_bb_dbg_port(dm);
		/* @Turn on 3-wire*/
		phydm_stop_3_wire(dm, PHYDM_REVERT);
		/* @Enable LNA*/
		if (dm->support_ic_type & ODM_RTL8821C)
			halrf_rf_lna_setting(dm, HALRF_LNA_ENABLE);

		odm_write_dig(dm, 0x20);

		set_result = phydm_stop_ic_trx(dm, PHYDM_REVERT);

		PHYDM_DBG(dm, ODM_COMP_API, "DC cancellation OK!!!\n");
	}

	/*@DC_Cancellation*/
	/*@DC compensation to CCK data path*/
	odm_set_bb_reg(dm, R_0xa9c, BIT(20), 0x1);
	if (dm->support_ic_type & (ODM_RTL8188F | ODM_RTL8723D |
		ODM_RTL8710B)) {
		offset_i_hex[0] = (reg_value32[0] & 0xffc0000) >> 18;
		offset_q_hex[0] = (reg_value32[0] & 0x3ff00) >> 8;

		/*@Before filling into registers,
		 *offset should be multiplexed (-1)
		 */
		offset_i_hex[0] = (offset_i_hex[0] >= 0x200) ?
				  (0x400 - offset_i_hex[0]) :
				  (0x1ff - offset_i_hex[0]);
		offset_q_hex[0] = (offset_q_hex[0] >= 0x200) ?
				  (0x400 - offset_q_hex[0]) :
				  (0x1ff - offset_q_hex[0]);

		odm_set_bb_reg(dm, R_0x950, 0x1ff, offset_i_hex[0]);
		odm_set_bb_reg(dm, R_0x950, 0x1ff0000, offset_q_hex[0]);
	} else if (dm->support_ic_type & (ODM_RTL8821C | ODM_RTL8822B)) {
		/* Path-a */
		offset_i_hex[0] = (reg_value32[0] & 0xffc00) >> 10;
		offset_q_hex[0] = reg_value32[0] & 0x3ff;

		/*@Before filling into registers,
		 *offset should be multiplexed (-1)
		 */
		offset_i_hex[0] = 0x400 - offset_i_hex[0];
		offset_q_hex[0] = 0x400 - offset_q_hex[0];

		odm_set_bb_reg(dm, R_0xc10, 0x3c000000,
			       (0x3c0 & offset_i_hex[0]) >> 6);
		odm_set_bb_reg(dm, R_0xc10, 0xfc00, 0x3f & offset_i_hex[0]);
		odm_set_bb_reg(dm, R_0xc14, 0x3c000000,
			       (0x3c0 & offset_q_hex[0]) >> 6);
		odm_set_bb_reg(dm, R_0xc14, 0xfc00, 0x3f & offset_q_hex[0]);

		/* Path-b */
		if (dm->rf_type > RF_1T1R) {
			offset_i_hex[1] = (reg_value32[1] & 0xffc00) >> 10;
			offset_q_hex[1] = reg_value32[1] & 0x3ff;

			/*@Before filling into registers,
			 *offset should be multiplexed (-1)
			 */
			offset_i_hex[1] = 0x400 - offset_i_hex[1];
			offset_q_hex[1] = 0x400 - offset_q_hex[1];

			odm_set_bb_reg(dm, R_0xe10, 0x3c000000,
				       (0x3c0 & offset_i_hex[1]) >> 6);
			odm_set_bb_reg(dm, R_0xe10, 0xfc00,
				       0x3f & offset_i_hex[1]);
			odm_set_bb_reg(dm, R_0xe14, 0x3c000000,
				       (0x3c0 & offset_q_hex[1]) >> 6);
			odm_set_bb_reg(dm, R_0xe14, 0xfc00,
				       0x3f & offset_q_hex[1]);
		}
	} else if (dm->support_ic_type & (ODM_RTL8192F)) {
		/* Path-a I:df4[27:18],Q:df4[17:8]*/
		offset_i_hex[0] = (reg_value32[0] & 0xffc0000) >> 18;
		offset_q_hex[0] = (reg_value32[0] & 0x3ff00) >> 8;

		/*@Before filling into registers,
		 *offset should be multiplexed (-1)
		 */
		offset_i_hex[0] = (offset_i_hex[0] >= 0x200) ?
				  (0x400 - offset_i_hex[0]) :
				  (0xff - offset_i_hex[0]);
		offset_q_hex[0] = (offset_q_hex[0] >= 0x200) ?
				  (0x400 - offset_q_hex[0]) :
				  (0xff - offset_q_hex[0]);
		/*Path-a I:c10[7:0],Q:c10[15:8]*/
		odm_set_bb_reg(dm, R_0xc10, 0xff, offset_i_hex[0]);
		odm_set_bb_reg(dm, R_0xc10, 0xff00, offset_q_hex[0]);

		/* Path-b */
		if (dm->rf_type > RF_1T1R) {
			/* @I:df4[27:18],Q:df4[17:8]*/
			offset_i_hex[1] = (reg_value32[1] & 0xffc0000) >> 18;
			offset_q_hex[1] = (reg_value32[1] & 0x3ff00) >> 8;

			/*@Before filling into registers,
			 *offset should be multiplexed (-1)
			 */
			offset_i_hex[1] = (offset_i_hex[1] >= 0x200) ?
					  (0x400 - offset_i_hex[1]) :
					  (0xff - offset_i_hex[1]);
			offset_q_hex[1] = (offset_q_hex[1] >= 0x200) ?
					  (0x400 - offset_q_hex[1]) :
					  (0xff - offset_q_hex[1]);
			/*Path-b I:c18[7:0],Q:c18[15:8]*/
			odm_set_bb_reg(dm, R_0xc18, 0xff, offset_i_hex[1]);
			odm_set_bb_reg(dm, R_0xc18, 0xff00, offset_q_hex[1]);
		}
	} else if (dm->support_ic_type & (ODM_RTL8721D | ODM_RTL8710C)) {
	 /*judy modified 20180517*/
		offset_i_hex[0] = (reg_value32[0] & 0xff80000) >> 19;
		offset_q_hex[0] = (reg_value32[0] & 0x3fe00) >> 9;

		/*@Before filling into registers,
		 *offset should be multiplexed (-1)
		 */
		offset_i_hex[0] = 0x200 - offset_i_hex[0];
		offset_q_hex[0] = 0x200 - offset_q_hex[0];

		odm_set_bb_reg(dm, R_0x950, 0x1ff, offset_i_hex[0]);
		odm_set_bb_reg(dm, R_0x950, 0x1ff0000, offset_q_hex[0]);
	}
#endif
}

void phydm_receiver_blocking(void *dm_void)
{
#ifdef CONFIG_RECEIVER_BLOCKING
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 chnl = *dm->channel;
	u8 bw = *dm->band_width;
	u32 bb_regf0 = odm_get_bb_reg(dm, R_0xf0, 0xf000);

	if (!(dm->support_ic_type & ODM_RECEIVER_BLOCKING_SUPPORT) ||
	    *dm->edcca_mode != PHYDM_EDCCA_ADAPT_MODE)
		return;

	if ((dm->support_ic_type & ODM_RTL8188E && bb_regf0 < 8) ||
	    dm->support_ic_type & ODM_RTL8192E) {
	    /*@8188E_T version*/
		if (dm->consecutive_idlel_time <= 10 || *dm->mp_mode)
			goto end;

		if (bw == CHANNEL_WIDTH_20 && chnl == 1) {
			phydm_nbi_setting(dm, FUNC_ENABLE, chnl, 20, 2410,
					  PHYDM_DONT_CARE);
			dm->is_rx_blocking_en = true;
		} else if ((bw == CHANNEL_WIDTH_20) && (chnl == 13)) {
			phydm_nbi_setting(dm, FUNC_ENABLE, chnl, 20, 2473,
					  PHYDM_DONT_CARE);
			dm->is_rx_blocking_en = true;
		} else if (dm->is_rx_blocking_en && chnl != 1 && chnl != 13) {
			phydm_nbi_enable(dm, FUNC_DISABLE);
			odm_set_bb_reg(dm, R_0xc40, 0x1f000000, 0x1f);
			dm->is_rx_blocking_en = false;
		}
		return;
	} else if ((dm->support_ic_type & ODM_RTL8188E && bb_regf0 >= 8)) {
	/*@8188E_S version*/
		if (dm->consecutive_idlel_time <= 10 || *dm->mp_mode)
			goto end;

		if (bw == CHANNEL_WIDTH_20 && chnl == 13) {
			phydm_nbi_setting(dm, FUNC_ENABLE, chnl, 20, 2473,
					  PHYDM_DONT_CARE);
			dm->is_rx_blocking_en = true;
		} else if (dm->is_rx_blocking_en && chnl != 13) {
			phydm_nbi_enable(dm, FUNC_DISABLE);
			odm_set_bb_reg(dm, R_0xc40, 0x1f000000, 0x1f);
			dm->is_rx_blocking_en = false;
		}
		return;
	}

end:
	if (dm->is_rx_blocking_en) {
		phydm_nbi_enable(dm, FUNC_DISABLE);
		odm_set_bb_reg(dm, R_0xc40, 0x1f000000, 0x1f);
		dm->is_rx_blocking_en = false;
	}
#endif
}

void phydm_dyn_bw_indication(void *dm_void)
{
#ifdef CONFIG_BW_INDICATION
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 en_auto_bw_th = dm->en_auto_bw_th;

	if (!(dm->support_ic_type & ODM_DYM_BW_INDICATION_SUPPORT))
		return;

	/*driver decide bw cobime timing*/
	if (dm->dis_dym_bw_indication) {
		if (*dm->dis_dym_bw_indication)
			return;
	}

	/*check for auto bw*/
	if (dm->rssi_min <= en_auto_bw_th && dm->is_linked) {
		phydm_bw_fixed_enable(dm, FUNC_DISABLE);
		return;
	}

	phydm_bw_fixed_setting(dm);
#endif
}