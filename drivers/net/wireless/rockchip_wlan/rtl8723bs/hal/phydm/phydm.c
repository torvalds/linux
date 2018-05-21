/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/

/* ************************************************************
 * include files
 * ************************************************************ */

#include "mp_precomp.h"
#include "phydm_precomp.h"


const u16 phy_rate_table[] = {	/*20M*/
	1, 2, 5, 11,
	6, 9, 12, 18, 24, 36, 48, 54,
	6, 13, 19, 26, 39, 52, 58, 65,		/*MCS0~7*/
	13, 26, 39, 52, 78, 104, 117, 130		/*MCS8~15*/
};

void
phydm_traffic_load_decision(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8		bit_shift_num = 0;

	/*---TP & Trafic-load calculation---*/

	if (p_dm->last_tx_ok_cnt > (*(p_dm->p_num_tx_bytes_unicast)))
		p_dm->last_tx_ok_cnt = (*(p_dm->p_num_tx_bytes_unicast));

	if (p_dm->last_rx_ok_cnt > (*(p_dm->p_num_rx_bytes_unicast)))
		p_dm->last_rx_ok_cnt = (*(p_dm->p_num_rx_bytes_unicast));

	p_dm->cur_tx_ok_cnt =  *(p_dm->p_num_tx_bytes_unicast) - p_dm->last_tx_ok_cnt;
	p_dm->cur_rx_ok_cnt =  *(p_dm->p_num_rx_bytes_unicast) - p_dm->last_rx_ok_cnt;
	p_dm->last_tx_ok_cnt =  *(p_dm->p_num_tx_bytes_unicast);
	p_dm->last_rx_ok_cnt =  *(p_dm->p_num_rx_bytes_unicast);

	bit_shift_num = 17 + (PHYDM_WATCH_DOG_PERIOD - 1); /*AP:  <<3(8bit), >>20(10^6,M), >>0(1sec)*/
													/*WIN&CE:  <<3(8bit), >>20(10^6,M), >>1(2sec)*/

	p_dm->tx_tp = ((p_dm->tx_tp) >> 1) + (u32)(((p_dm->cur_tx_ok_cnt) >> bit_shift_num) >> 1);
	p_dm->rx_tp = ((p_dm->rx_tp) >> 1) + (u32)(((p_dm->cur_rx_ok_cnt) >> bit_shift_num) >> 1);

	p_dm->total_tp = p_dm->tx_tp + p_dm->rx_tp;

	/*[Calculate TX/RX state]*/
	if (p_dm->tx_tp > (p_dm->rx_tp << 1))
		p_dm->txrx_state_all = TX_STATE;
	else if (p_dm->rx_tp > (p_dm->tx_tp << 1))
		p_dm->txrx_state_all = RX_STATE;
	else
		p_dm->txrx_state_all = BI_DIRECTION_STATE;

	/*[Calculate consecutive idlel time]*/
	if (p_dm->total_tp == 0)
		p_dm->consecutive_idlel_time += PHYDM_WATCH_DOG_PERIOD;
	else
		p_dm->consecutive_idlel_time = 0;

	/*[Traffic load decision]*/
	p_dm->pre_traffic_load = p_dm->traffic_load;

	if (p_dm->cur_tx_ok_cnt > 1875000 || p_dm->cur_rx_ok_cnt > 1875000) {		/* ( 1.875M * 8bit ) / 2sec= 7.5M bits /sec )*/

		p_dm->traffic_load = TRAFFIC_HIGH;
		/**/
	} else if (p_dm->cur_tx_ok_cnt > 500000 || p_dm->cur_rx_ok_cnt > 500000) { /*( 0.5M * 8bit ) / 2sec =  2M bits /sec )*/

		p_dm->traffic_load = TRAFFIC_MID;
		/**/
	} else if (p_dm->cur_tx_ok_cnt > 100000 || p_dm->cur_rx_ok_cnt > 100000)  { /*( 0.1M * 8bit ) / 2sec =  0.4M bits /sec )*/

		p_dm->traffic_load = TRAFFIC_LOW;
		/**/
	} else {

		p_dm->traffic_load = TRAFFIC_ULTRA_LOW;
		/**/
	}

	/*
	PHYDM_DBG(p_dm, DBG_COMMON_FLOW, ("cur_tx_ok_cnt = %d, cur_rx_ok_cnt = %d, last_tx_ok_cnt = %d, last_rx_ok_cnt = %d\n",
		p_dm->cur_tx_ok_cnt, p_dm->cur_rx_ok_cnt, p_dm->last_tx_ok_cnt, p_dm->last_rx_ok_cnt));

	PHYDM_DBG(p_dm, DBG_COMMON_FLOW, ("tx_tp = %d, rx_tp = %d\n",
		p_dm->tx_tp, p_dm->rx_tp));
	*/
		
}

void
phydm_init_cck_setting(
	struct PHY_DM_STRUCT		*p_dm
)
{
#if (RTL8192E_SUPPORT == 1)
	u32 value_824, value_82c;
#endif

	p_dm->is_cck_high_power = (boolean) odm_get_bb_reg(p_dm, ODM_REG(CCK_RPT_FORMAT, p_dm), ODM_BIT(CCK_RPT_FORMAT, p_dm));

	phydm_config_cck_rx_antenna_init(p_dm);
	phydm_config_cck_rx_path(p_dm, BB_PATH_A);

#if (RTL8192E_SUPPORT == 1)
	if (p_dm->support_ic_type & (ODM_RTL8192E)) {

		/* 0x824[9] = 0x82C[9] = 0xA80[7]  those registers setting should be equal or CCK RSSI report may be incorrect */
		value_824 = odm_get_bb_reg(p_dm, 0x824, BIT(9));
		value_82c = odm_get_bb_reg(p_dm, 0x82c, BIT(9));

		if (value_824 != value_82c)
			odm_set_bb_reg(p_dm, 0x82c, BIT(9), value_824);
		odm_set_bb_reg(p_dm, 0xa80, BIT(7), value_824);
		p_dm->cck_agc_report_type = (boolean)value_824;

		PHYDM_DBG(p_dm, ODM_COMP_INIT, ("cck_agc_report_type = (( %d )), ext_lna_gain = (( %d ))\n", p_dm->cck_agc_report_type, p_dm->ext_lna_gain));
	}
#endif

#if ((RTL8703B_SUPPORT == 1) || (RTL8723D_SUPPORT == 1) || (RTL8710B_SUPPORT == 1))
	if (p_dm->support_ic_type & (ODM_RTL8703B | ODM_RTL8723D | ODM_RTL8710B)) {

		p_dm->cck_agc_report_type = odm_get_bb_reg(p_dm, 0x950, BIT(11)) ? 1 : 0; /*1: 4bit LNA, 0: 3bit LNA */

		if (p_dm->cck_agc_report_type != 1) {
			dbg_print("[Warning] 8703B/8723D/8710B CCK should be 4bit LNA, ie. 0x950[11] = 1\n");
			/**/
		}
	}
#endif

#if ((RTL8723D_SUPPORT == 1) || (RTL8822B_SUPPORT == 1) || (RTL8197F_SUPPORT == 1) || (RTL8821C_SUPPORT == 1) || (RTL8710B_SUPPORT == 1))
	if (p_dm->support_ic_type & (ODM_RTL8723D | ODM_RTL8822B | ODM_RTL8197F | ODM_RTL8821C | ODM_RTL8710B))
		p_dm->cck_new_agc = odm_get_bb_reg(p_dm, 0xa9c, BIT(17)) ? true : false;          /*1: new agc  0: old agc*/
	else
#endif
	{
		p_dm->cck_new_agc = false;
		/**/
	}

	phydm_get_cck_rssi_table_from_reg(p_dm);

}

void
phydm_init_hw_info_by_rfe(
	struct PHY_DM_STRUCT		*p_dm
)
{
#if (RTL8822B_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_RTL8822B)
		phydm_init_hw_info_by_rfe_type_8822b(p_dm);
#endif
#if (RTL8821C_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_RTL8821C)
		phydm_init_hw_info_by_rfe_type_8821c(p_dm);
#endif
#if (RTL8197F_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_RTL8197F)
		phydm_init_hw_info_by_rfe_type_8197f(p_dm);
#endif
}

void
phydm_common_info_self_init(
	struct PHY_DM_STRUCT		*p_dm
)
{
	phydm_init_cck_setting(p_dm);
	p_dm->rf_path_rx_enable = (u8) odm_get_bb_reg(p_dm, ODM_REG(BB_RX_PATH, p_dm), ODM_BIT(BB_RX_PATH, p_dm));
#if (DM_ODM_SUPPORT_TYPE != ODM_CE)
	p_dm->p_is_net_closed = &p_dm->BOOLEAN_temp;

	phydm_init_debug_setting(p_dm);
#endif
	phydm_init_trx_antenna_setting(p_dm);
	phydm_init_soft_ml_setting(p_dm);

	p_dm->phydm_period = PHYDM_WATCH_DOG_PERIOD;
	p_dm->phydm_sys_up_time = 0;

	if (p_dm->support_ic_type & ODM_IC_1SS)
		p_dm->num_rf_path = 1;
	else if (p_dm->support_ic_type & ODM_IC_2SS)
		p_dm->num_rf_path = 2;
	else if (p_dm->support_ic_type & ODM_IC_3SS)
		p_dm->num_rf_path = 3;
	else if (p_dm->support_ic_type & ODM_IC_4SS)
		p_dm->num_rf_path = 4;

	p_dm->tx_rate = 0xFF;
	p_dm->rssi_min_by_path = 0xFF;

	p_dm->number_linked_client = 0;
	p_dm->pre_number_linked_client = 0;
	p_dm->number_active_client = 0;
	p_dm->pre_number_active_client = 0;

	p_dm->last_tx_ok_cnt = 0;
	p_dm->last_rx_ok_cnt = 0;
	p_dm->tx_tp = 0;
	p_dm->rx_tp = 0;
	p_dm->total_tp = 0;
	p_dm->traffic_load = TRAFFIC_LOW;

	p_dm->nbi_set_result = 0;
	p_dm->is_init_hw_info_by_rfe = false;
	p_dm->pre_dbg_priority = BB_DBGPORT_RELEASE;
	p_dm->tp_active_th = 5;
	p_dm->disable_phydm_watchdog = 0;

	p_dm->u8_dummy = 0xf;
	p_dm->u16_dummy = 0xffff;
	p_dm->u32_dummy = 0xffffffff;
	
	/*odm_memory_set(p_dm, &(p_dm->pause_lv_table.lv_dig), 0, sizeof(struct phydm_pause_lv));*/
	p_dm->pause_lv_table.lv_cckpd = PHYDM_PAUSE_RELEASE;
	p_dm->pause_lv_table.lv_dig = PHYDM_PAUSE_RELEASE;

}

void
phydm_cmn_sta_info_update(
	void	*p_dm_void,
	u8	macid
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct cmn_sta_info			*p_sta = p_dm->p_phydm_sta_info[macid];
	struct ra_sta_info				*p_ra = NULL;

	if (is_sta_active(p_sta)) {
		p_ra = &(p_sta->ra_info);
	} else {
		PHYDM_DBG(p_dm, DBG_RA_MASK, ("[Warning] %s invalid sta_info\n", __func__));
		return;
	}

	PHYDM_DBG(p_dm, DBG_RA_MASK, ("%s ======>\n", __func__));
	PHYDM_DBG(p_dm, DBG_RA_MASK, ("MACID=%d\n", p_sta->mac_id));

	/*[Calculate TX/RX state]*/
	if (p_sta->tx_moving_average_tp > (p_sta->rx_moving_average_tp << 1))
		p_ra->txrx_state= TX_STATE;
	else if (p_sta->rx_moving_average_tp > (p_sta->tx_moving_average_tp << 1))
		p_ra->txrx_state = RX_STATE;
	else
		p_ra->txrx_state = BI_DIRECTION_STATE;

}

void
phydm_common_info_self_update(
	struct PHY_DM_STRUCT		*p_dm
)
{
	u8	sta_cnt = 0, num_active_client = 0;
	u32	i, one_entry_macid = 0;
	u32	ma_rx_tp = 0;
	struct cmn_sta_info	*p_sta;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

	struct _ADAPTER	*adapter =  p_dm->adapter;
	PMGNT_INFO	p_mgnt_info = &adapter->MgntInfo;

	p_sta = p_dm->p_phydm_sta_info[0];
	if (p_mgnt_info->mAssoc) {
		p_sta->dm_ctrl |= STA_DM_CTRL_ACTIVE;
		for (i = 0; i < 6; i++)
			p_sta->mac_addr[i] = p_mgnt_info->Bssid[i];
	} else if (GetFirstClientPort(adapter)) {
		struct _ADAPTER	*p_client_adapter = GetFirstClientPort(adapter);

		p_sta->dm_ctrl |= STA_DM_CTRL_ACTIVE;
		for (i = 0; i < 6; i++)
			p_sta->mac_addr[i] = p_client_adapter->MgntInfo.Bssid[i];
	} else {
		p_sta->dm_ctrl = p_sta->dm_ctrl & (~STA_DM_CTRL_ACTIVE);
		for (i = 0; i < 6; i++)
			p_sta->mac_addr[i] = 0;
	}

	/* STA mode is linked to AP */
	if (is_sta_active(p_sta) && !ACTING_AS_AP(adapter))
		p_dm->bsta_state = true;
	else
		p_dm->bsta_state = false;
#endif

	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
		p_sta = p_dm->p_phydm_sta_info[i];
		if (is_sta_active(p_sta)) {
			sta_cnt++;
			
			if (sta_cnt == 1)
				one_entry_macid = i;

			phydm_cmn_sta_info_update(p_dm, (u8)i);

			ma_rx_tp = p_sta->rx_moving_average_tp + p_sta->tx_moving_average_tp;
			PHYDM_DBG(p_dm, DBG_COMMON_FLOW, ("TP[%d]: ((%d )) bit/sec\n", i, ma_rx_tp));

			if (ma_rx_tp > ACTIVE_TP_THRESHOLD)
				num_active_client++;
		}
	}

	if (sta_cnt == 1) {
		p_dm->is_one_entry_only = true;
		p_dm->one_entry_macid = one_entry_macid;
		p_dm->one_entry_tp = ma_rx_tp;

		p_dm->tp_active_occur = 0;

		PHYDM_DBG(p_dm, DBG_COMMON_FLOW, ("one_entry_tp=((%d)), pre_one_entry_tp=((%d))\n",
			p_dm->one_entry_tp, p_dm->pre_one_entry_tp));

		if ((p_dm->one_entry_tp > p_dm->pre_one_entry_tp) && (p_dm->pre_one_entry_tp <= 2)) {
			if ((p_dm->one_entry_tp - p_dm->pre_one_entry_tp) > p_dm->tp_active_th)
				p_dm->tp_active_occur = 1;
		}
		p_dm->pre_one_entry_tp = p_dm->one_entry_tp;
	} else
		p_dm->is_one_entry_only = false;

	p_dm->pre_number_linked_client = p_dm->number_linked_client;
	p_dm->pre_number_active_client = p_dm->number_active_client;

	p_dm->number_linked_client = sta_cnt;
	p_dm->number_active_client = num_active_client;

	/*Traffic load information update*/
	phydm_traffic_load_decision(p_dm);

	p_dm->phydm_sys_up_time += p_dm->phydm_period;

	p_dm->is_dfs_band = phydm_is_dfs_band(p_dm);

}

void
phydm_common_info_self_reset(
	struct PHY_DM_STRUCT		*p_dm
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	p_dm->phy_dbg_info.num_qry_beacon_pkt = 0;
#endif
}

void *
phydm_get_structure(
	struct PHY_DM_STRUCT		*p_dm,
	u8			structure_type
)

{
	void	*p_struct = NULL;
#if RTL8195A_SUPPORT
	switch (structure_type) {
	case	PHYDM_FALSEALMCNT:
		p_struct = &false_alm_cnt;
		break;

	case	PHYDM_CFOTRACK:
		p_struct = &dm_cfo_track;
		break;

	case	PHYDM_ADAPTIVITY:
		p_struct = &(p_dm->adaptivity);
		break;

	default:
		break;
	}

#else
	switch (structure_type) {
	case	PHYDM_FALSEALMCNT:
		p_struct = &(p_dm->false_alm_cnt);
		break;

	case	PHYDM_CFOTRACK:
		p_struct = &(p_dm->dm_cfo_track);
		break;

	case	PHYDM_ADAPTIVITY:
		p_struct = &(p_dm->adaptivity);
		break;

	case	PHYDM_DFS:
		p_struct = &(p_dm->dfs);
		break;

	default:
		break;
	}

#endif
	return	p_struct;
}

void
phydm_hw_setting(
	struct PHY_DM_STRUCT		*p_dm
)
{
#if (RTL8821A_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_RTL8821)
		odm_hw_setting_8821a(p_dm);
#endif

#if (RTL8814A_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_RTL8814A)
		phydm_hwsetting_8814a(p_dm);
#endif

#if (RTL8822B_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_RTL8822B)
		phydm_hwsetting_8822b(p_dm);
#endif

#if (RTL8197F_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_RTL8197F)
		phydm_hwsetting_8197f(p_dm);
#endif
}


#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
u64
phydm_supportability_init_win(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u64			support_ability = 0;

	switch (p_dm->support_ic_type) {

	/*---------------N Series--------------------*/
	#if (RTL8188E_SUPPORT == 1)	
	case	ODM_RTL8188E:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR			|
			ODM_BB_PRIMARY_CCA;
		break;
	#endif

	#if (RTL8192E_SUPPORT == 1)
	case	ODM_RTL8192E:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR			|
			ODM_BB_PRIMARY_CCA;
		break;
	#endif

	#if (RTL8723B_SUPPORT == 1)
	case	ODM_RTL8723B:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR		|
			ODM_BB_PRIMARY_CCA;
		break;
	#endif

	#if (RTL8703B_SUPPORT == 1)
	case	ODM_RTL8703B:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;
		break;
	#endif

	#if (RTL8723D_SUPPORT == 1)
	case	ODM_RTL8723D:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/* ODM_BB_PWR_TRAIN	| */
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;
		break;
	#endif

	#if (RTL8710B_SUPPORT == 1)
	case	ODM_RTL8710B:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;
		break;
	#endif

	#if (RTL8188F_SUPPORT == 1)
	case	ODM_RTL8188F:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;
		break;
	#endif
	
	/*---------------AC Series-------------------*/

	#if ((RTL8812A_SUPPORT == 1) || (RTL8821A_SUPPORT == 1))
	case	ODM_RTL8812:
	case	ODM_RTL8821:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			ODM_BB_DYNAMIC_TXPWR	|
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;
		break;
	#endif

	#if (RTL8814A_SUPPORT == 1) 
	case ODM_RTL8814A:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			ODM_BB_DYNAMIC_TXPWR	|
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;
		break;
	#endif
	
	#if (RTL8814B_SUPPORT == 1) 
	case ODM_RTL8814B:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;
		break;
	#endif

	#if (RTL8822B_SUPPORT == 1) 
	case ODM_RTL8822B:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR			|
			ODM_BB_ADAPTIVE_SOML;
		break;
	#endif

	#if (RTL8821C_SUPPORT == 1) 
	case ODM_RTL8821C:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;
		break;
	#endif

	default:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;

			dbg_print("[Warning] Supportability Init Warning !!!\n");
		break;

	}

	/*[Config Antenna Diveristy]*/
	if (*(p_dm->p_enable_antdiv))
		support_ability |= ODM_BB_ANT_DIV;
	
	/*[Config Adaptivity]*/
	if (*(p_dm->p_enable_adaptivity))
		support_ability |= ODM_BB_ADAPTIVITY;

	return support_ability;
}
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_CE))
u64
phydm_supportability_init_ce(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u64			support_ability = 0;

	switch (p_dm->support_ic_type) {

	/*---------------N Series--------------------*/
	#if (RTL8188E_SUPPORT == 1)	
	case	ODM_RTL8188E:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR			|
			ODM_BB_PRIMARY_CCA;
		break;
	#endif

	#if (RTL8192E_SUPPORT == 1)
	case	ODM_RTL8192E:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR			|
			ODM_BB_PRIMARY_CCA;
		break;
	#endif

	#if (RTL8723B_SUPPORT == 1)
	case	ODM_RTL8723B:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR			|
			ODM_BB_PRIMARY_CCA;
		break;
	#endif

	#if (RTL8703B_SUPPORT == 1)
	case	ODM_RTL8703B:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;
		break;
	#endif

	#if (RTL8723D_SUPPORT == 1)
	case	ODM_RTL8723D:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/* ODM_BB_PWR_TRAIN	| */	
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;
		break;
	#endif

	#if (RTL8710B_SUPPORT == 1)
	case	ODM_RTL8710B:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;
		break;
	#endif

	#if (RTL8188F_SUPPORT == 1)
	case	ODM_RTL8188F:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;
		break;
	#endif
		
	/*---------------AC Series-------------------*/

	#if ((RTL8812A_SUPPORT == 1) || (RTL8821A_SUPPORT == 1))
	case	ODM_RTL8812:
	case	ODM_RTL8821:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;
		break;
	#endif

	#if (RTL8814A_SUPPORT == 1) 
	case ODM_RTL8814A:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;
		break;
	#endif
	
	#if (RTL8814B_SUPPORT == 1) 
	case ODM_RTL8814B:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;
		break;
	#endif

	#if (RTL8822B_SUPPORT == 1) 
	case ODM_RTL8822B:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;
		break;
	#endif

	#if (RTL8821C_SUPPORT == 1) 
	case ODM_RTL8821C:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;
		break;
	#endif

	default:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;

			dbg_print("[Warning] Supportability Init Warning !!!\n");
		break;

	}
	
	/*[Config Antenna Diveristy]*/
	if (*(p_dm->p_enable_antdiv))
		support_ability |= ODM_BB_ANT_DIV;
	
	/*[Config Adaptivity]*/
	if (*(p_dm->p_enable_adaptivity))
		support_ability |= ODM_BB_ADAPTIVITY;
	
	return support_ability;
}
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
u64
phydm_supportability_init_ap(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u64			support_ability = 0;

	switch (p_dm->support_ic_type) {

	/*---------------N Series--------------------*/
	#if (RTL8188E_SUPPORT == 1)	
	case	ODM_RTL8188E:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR			|
			ODM_BB_PRIMARY_CCA;
		break;
	#endif

	#if (RTL8192E_SUPPORT == 1)
	case	ODM_RTL8192E:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR			|
			ODM_BB_PRIMARY_CCA;
		break;
	#endif

	#if (RTL8723B_SUPPORT == 1)
	case	ODM_RTL8723B:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;
		break;
	#endif
		
	#if ((RTL8198F_SUPPORT == 1) || (RTL8197F_SUPPORT == 1))
	case	ODM_RTL8198F:
	case	ODM_RTL8197F:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ADAPTIVE_SOML	|
			ODM_BB_ENV_MONITOR		|
			ODM_BB_LNA_SAT_CHK		|
			ODM_BB_PRIMARY_CCA;
		break;
	#endif
	
	/*---------------AC Series-------------------*/

	#if (RTL8881A_SUPPORT == 1)
	case	ODM_RTL8881A:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;
		break;
	#endif

	#if (RTL8814A_SUPPORT == 1) 
	case ODM_RTL8814A:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;
		break;
	#endif
	
	#if (RTL8814B_SUPPORT == 1) 
	case ODM_RTL8814B:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;
		break;
	#endif

	#if (RTL8822B_SUPPORT == 1) 
	case ODM_RTL8822B:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR			|
			ODM_BB_ADAPTIVE_SOML;
		break;
	#endif

	#if (RTL8821C_SUPPORT == 1) 
	case ODM_RTL8821C:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;

		break;
	#endif

	default:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;

			dbg_print("[Warning] Supportability Init Warning !!!\n");
		break;

	}

	#if 0
	/*[Config Antenna Diveristy]*/
	if (*(p_dm->p_enable_antdiv))
		support_ability |= ODM_BB_ANT_DIV;
	
	/*[Config Adaptivity]*/
	if (*(p_dm->p_enable_adaptivity))
		support_ability |= ODM_BB_ADAPTIVITY;
	#endif

	return support_ability;
}
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_IOT))
u64
phydm_supportability_init_iot(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u64			support_ability = 0;

	switch (p_dm->support_ic_type) {

	#if (RTL8710B_SUPPORT == 1)
	case	ODM_RTL8710B:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;
		break;
	#endif

	#if (RTL8195A_SUPPORT == 1)
	case	ODM_RTL8195A:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;
		break;
	#endif
	
	default:
		support_ability |=
			ODM_BB_DIG				|
			ODM_BB_RA_MASK			|
			/*ODM_BB_DYNAMIC_TXPWR	|*/
			ODM_BB_FA_CNT			|
			ODM_BB_RSSI_MONITOR		|
			ODM_BB_CCK_PD			|
			/*ODM_BB_PWR_TRAIN		|*/
			ODM_BB_RATE_ADAPTIVE	|
			ODM_BB_CFO_TRACKING		|
			ODM_BB_ENV_MONITOR;

			dbg_print("[Warning] Supportability Init Warning !!!\n");
		break;

	}
	
	/*[Config Antenna Diveristy]*/
	if (*(p_dm->p_enable_antdiv))
		support_ability |= ODM_BB_ANT_DIV;
	
	/*[Config Adaptivity]*/
	if (*(p_dm->p_enable_adaptivity))
		support_ability |= ODM_BB_ADAPTIVITY;
	
	return support_ability;
}
#endif

void
phydm_fwoffload_ability_init(
	struct PHY_DM_STRUCT		*p_dm,
	enum phydm_offload_ability	offload_ability
)
{

	switch (offload_ability) {

	case	PHYDM_PHY_PARAM_OFFLOAD:
		if (p_dm->support_ic_type & (ODM_RTL8814A | ODM_RTL8822B | ODM_RTL8821C))
			p_dm->fw_offload_ability |= PHYDM_PHY_PARAM_OFFLOAD;
		break;

	case	PHYDM_RF_IQK_OFFLOAD:
		p_dm->fw_offload_ability |= PHYDM_RF_IQK_OFFLOAD;
		break;

	default:
		PHYDM_DBG(p_dm, ODM_COMP_INIT, ("fwofflad, wrong init type!!\n"));
		break;

	}

	PHYDM_DBG(p_dm, ODM_COMP_INIT,
		("fw_offload_ability = %x\n", p_dm->fw_offload_ability));

}
void
phydm_fwoffload_ability_clear(
	struct PHY_DM_STRUCT		*p_dm,
	enum phydm_offload_ability	offload_ability
)
{

	switch (offload_ability) {

	case	PHYDM_PHY_PARAM_OFFLOAD:
		if (p_dm->support_ic_type & (ODM_RTL8814A | ODM_RTL8822B | ODM_RTL8821C))
			p_dm->fw_offload_ability &= (~PHYDM_PHY_PARAM_OFFLOAD);
		break;

	case	PHYDM_RF_IQK_OFFLOAD:
		p_dm->fw_offload_ability &= (~PHYDM_RF_IQK_OFFLOAD);
		break;

	default:
		PHYDM_DBG(p_dm, ODM_COMP_INIT, ("fwofflad, wrong init type!!\n"));
		break;

	}

	PHYDM_DBG(p_dm, ODM_COMP_INIT,
		("fw_offload_ability = %x\n", p_dm->fw_offload_ability));

}

void
phydm_supportability_init(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u64	support_ability;
	
	if (*(p_dm->p_mp_mode) == true) {
		support_ability = 0;
		/**/
	} else {

		#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
		support_ability = phydm_supportability_init_win(p_dm);
		#elif (DM_ODM_SUPPORT_TYPE & (ODM_AP))
		support_ability = phydm_supportability_init_ap(p_dm);
		#elif(DM_ODM_SUPPORT_TYPE & (ODM_CE))
		support_ability = phydm_supportability_init_ce(p_dm);
		#elif(DM_ODM_SUPPORT_TYPE & (ODM_IOT))
		support_ability = phydm_supportability_init_iot(p_dm);
		#endif
	}
	odm_cmn_info_init(p_dm, ODM_CMNINFO_ABILITY, support_ability);
	PHYDM_DBG(p_dm, ODM_COMP_INIT, ("IC = ((0x%x)), Supportability Init = ((0x%llx))\n", p_dm->support_ic_type, p_dm->support_ability));
}

void
phydm_rfe_init(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	
	PHYDM_DBG(p_dm, ODM_COMP_INIT, ("RFE_Init\n"));
#if (RTL8822B_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8822B) {
		phydm_rfe_8822b_init(p_dm);
		/**/
	}
#endif
}

void
phydm_dm_early_init(
	struct PHY_DM_STRUCT	*p_dm
)
{
	#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	halrf_init(p_dm);
	#endif
}

void
odm_dm_init(
	struct PHY_DM_STRUCT		*p_dm
)
{
	halrf_init(p_dm);
	phydm_supportability_init(p_dm);
	phydm_rfe_init(p_dm);
	phydm_common_info_self_init(p_dm);
	phydm_rx_phy_status_init(p_dm);
	phydm_auto_dbg_engine_init(p_dm);
	phydm_dig_init(p_dm);
	phydm_cck_pd_init(p_dm);
	phydm_nhm_init(p_dm);
	phydm_adaptivity_init(p_dm);
	phydm_ra_info_init(p_dm);
	phydm_rssi_monitor_init(p_dm);
	phydm_cfo_tracking_init(p_dm);
	phydm_rf_init(p_dm);
	odm_txpowertracking_init(p_dm);
	phydm_dc_cancellation(p_dm);
#ifdef PHYDM_TXA_CALIBRATION
	phydm_txcurrentcalibration(p_dm);
	phydm_get_pa_bias_offset(p_dm);
#endif
	odm_antenna_diversity_init(p_dm);
	phydm_adaptive_soml_init(p_dm);
#ifdef CONFIG_DYNAMIC_RX_PATH
	phydm_dynamic_rx_path_init(p_dm);
#endif
	odm_auto_channel_select_init(p_dm);
	phydm_path_diversity_init(p_dm);
	phydm_dynamic_tx_power_init(p_dm);
#if (PHYDM_LA_MODE_SUPPORT == 1)
	adc_smp_init(p_dm);
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	phydm_beamforming_init(p_dm);
#endif
#if (RTL8188E_SUPPORT == 1)
	odm_ra_info_init_all(p_dm);
#endif

	phydm_primary_cca_init(p_dm);

	#ifdef CONFIG_PSD_TOOL
	phydm_psd_init(p_dm);
	#endif
	
	#ifdef CONFIG_SMART_ANTENNA
	phydm_smt_ant_init(p_dm);
	#endif

}

void
odm_dm_reset(
	struct PHY_DM_STRUCT		*p_dm
)
{
	struct phydm_dig_struct *p_dig_t = &p_dm->dm_dig_table;

	odm_ant_div_reset(p_dm);
	phydm_set_edcca_threshold_api(p_dm, p_dig_t->cur_ig_value);
}

void
phydm_support_ability_debug(
	void		*p_dm_void,
	u32		*const dm_value,
	u32			*_used,
	char			*output,
	u32			*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u64			pre_support_ability, one = 1;
	u32 used = *_used;
	u32 out_len = *_out_len;

	pre_support_ability = p_dm->support_ability;

	PHYDM_SNPRINTF((output + used, out_len - used, "\n%s\n", "================================"));
	if (dm_value[0] == 100) {
		PHYDM_SNPRINTF((output + used, out_len - used, "[Supportability] PhyDM Selection\n"));
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "================================"));
		PHYDM_SNPRINTF((output + used, out_len - used, "00. (( %s ))DIG\n", ((p_dm->support_ability & ODM_BB_DIG) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "01. (( %s ))RA_MASK\n", ((p_dm->support_ability & ODM_BB_RA_MASK) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "02. (( %s ))DYN_TXPWR\n", ((p_dm->support_ability & ODM_BB_DYNAMIC_TXPWR) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "03. (( %s ))FA_CNT\n", ((p_dm->support_ability & ODM_BB_FA_CNT) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "04. (( %s ))RSSI_MNTR\n", ((p_dm->support_ability & ODM_BB_RSSI_MONITOR) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "05. (( %s ))CCK_PD\n", ((p_dm->support_ability & ODM_BB_CCK_PD) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "06. (( %s ))ANT_DIV\n", ((p_dm->support_ability & ODM_BB_ANT_DIV) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "07. (( %s ))SMT_ANT\n", ((p_dm->support_ability & ODM_BB_SMT_ANT) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "08. (( %s ))PWR_TRAIN\n", ((p_dm->support_ability & ODM_BB_PWR_TRAIN) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "09. (( %s ))RA\n", ((p_dm->support_ability & ODM_BB_RATE_ADAPTIVE) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "10. (( %s ))PATH_DIV\n", ((p_dm->support_ability & ODM_BB_PATH_DIV) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "11. (( %s ))DFS\n", ((p_dm->support_ability & ODM_BB_DFS) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "12. (( %s ))DYN_ARFR\n", ((p_dm->support_ability & ODM_BB_DYNAMIC_ARFR) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "13. (( %s ))ADAPTIVITY\n", ((p_dm->support_ability & ODM_BB_ADAPTIVITY) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "14. (( %s ))CFO_TRACK\n", ((p_dm->support_ability & ODM_BB_CFO_TRACKING) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "15. (( %s ))ENV_MONITOR\n", ((p_dm->support_ability & ODM_BB_ENV_MONITOR) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "16. (( %s ))PRI_CCA\n", ((p_dm->support_ability & ODM_BB_PRIMARY_CCA) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "17. (( %s ))ADPTV_SOML\n", ((p_dm->support_ability & ODM_BB_ADAPTIVE_SOML) ? ("V") : ("."))));
		/*PHYDM_SNPRINTF((output + used, out_len - used, "18. (( %s ))TBD\n", ((p_dm->support_ability & ODM_BB_TBD) ? ("V") : ("."))));*/
		/*PHYDM_SNPRINTF((output + used, out_len - used, "19. (( %s ))TBD\n", ((p_dm->support_ability & ODM_BB_TBD) ? ("V") : ("."))));*/
		PHYDM_SNPRINTF((output + used, out_len - used, "20. (( %s ))DYN_RX_PATH\n", ((p_dm->support_ability & ODM_BB_DYNAMIC_RX_PATH) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "================================"));
		PHYDM_SNPRINTF((output + used, out_len - used, "[Supportability] PhyDM offload ability\n"));
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "================================"));
		PHYDM_SNPRINTF((output + used, out_len - used, "00. (( %s ))PHY PARAM OFFLOAD\n", ((p_dm->fw_offload_ability & PHYDM_PHY_PARAM_OFFLOAD) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "01. (( %s ))RF IQK OFFLOAD\n", ((p_dm->fw_offload_ability & PHYDM_RF_IQK_OFFLOAD) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "================================"));

	}
	/*
	else if(dm_value[0] == 101)
	{
		p_dm->support_ability = 0 ;
		dbg_print("Disable all support_ability components\n");
		PHYDM_SNPRINTF((output+used, out_len-used,"%s\n", "Disable all support_ability components"));
	}
	*/
	else {

		if (dm_value[1] == 1) { /* enable */
			p_dm->support_ability |= (one << dm_value[0]);
			if (BIT(dm_value[0]) & ODM_BB_PATH_DIV)
				phydm_path_diversity_init(p_dm);
		} else if (dm_value[1] == 2)	/* disable */
			p_dm->support_ability &= ~(one << dm_value[0]);
		else
			PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "[Warning!!!]  1:enable,  2:disable"));
	}
	PHYDM_SNPRINTF((output + used, out_len - used, "pre-support_ability  =  0x%llx\n",  pre_support_ability));
	PHYDM_SNPRINTF((output + used, out_len - used, "Curr-support_ability =  0x%llx\n", p_dm->support_ability));
	PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "================================"));

	*_used = used;
	*_out_len = out_len;
}

void
phydm_watchdog_lps_32k(
	struct PHY_DM_STRUCT		*p_dm
)
{
	PHYDM_DBG(p_dm, DBG_COMMON_FLOW, ("%s ======>\n", __func__));

	phydm_common_info_self_update(p_dm);
	phydm_rssi_monitor_check(p_dm);
	phydm_dig_lps_32k(p_dm);
	phydm_common_info_self_reset(p_dm);
}

void
phydm_watchdog_lps(
	struct PHY_DM_STRUCT		*p_dm
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	PHYDM_DBG(p_dm, DBG_COMMON_FLOW, ("%s ======>\n", __func__));

	phydm_common_info_self_update(p_dm);
	phydm_rssi_monitor_check(p_dm);
	phydm_basic_dbg_message(p_dm);
	phydm_receiver_blocking(p_dm);
	odm_false_alarm_counter_statistics(p_dm);
	phydm_dig_by_rssi_lps(p_dm);
	phydm_cck_pd_th(p_dm);
	phydm_adaptivity(p_dm);
	#if (DM_ODM_SUPPORT_TYPE & (ODM_CE))
	odm_antenna_diversity(p_dm); /*enable AntDiv in PS mode, request from SD4 Jeff*/
	#endif
	phydm_common_info_self_reset(p_dm);
#endif
}

void
phydm_watchdog_mp(
	struct PHY_DM_STRUCT		*p_dm
)
{
#ifdef CONFIG_DYNAMIC_RX_PATH
	phydm_dynamic_rx_path_caller(p_dm);
#endif
}

void
phydm_pause_dm_watchdog(
	void					*p_dm_void,
	enum phydm_pause_type		pause_type
)
{
	struct PHY_DM_STRUCT			*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (pause_type == PHYDM_PAUSE) {
		p_dm->disable_phydm_watchdog = 1;
		PHYDM_DBG(p_dm, ODM_COMP_API, ("PHYDM Stop\n"));
	} else {
		p_dm->disable_phydm_watchdog = 0;
		PHYDM_DBG(p_dm, ODM_COMP_API, ("PHYDM Start\n"));
	}
}

u8
phydm_pause_func(
	void						*p_dm_void,
	enum phydm_func_idx_e	pause_func,
	enum phydm_pause_type	pause_type,
	enum phydm_pause_level	pause_lv,
	u8						val_lehgth,
	u32						*val_buf
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	s8	*pause_lv_pre = &(p_dm->s8_dummy);
	u32	*bkp_val = &(p_dm->u32_dummy);
	u32	ori_val[5] = {0};
	u64	pause_func_bitmap = (u64)BIT(pause_func);
	u8	i;



	PHYDM_DBG(p_dm, ODM_COMP_API, ("[%s][%s] LV=%d, Len=%d\n", __func__, 
		((pause_type == PHYDM_PAUSE) ? "Pause" : "Resume"),  pause_lv, val_lehgth));

	if (pause_lv >= PHYDM_PAUSE_MAX_NUM) {
		PHYDM_DBG(p_dm, ODM_COMP_API, ("[WARNING] Wrong LV=%d\n", pause_lv));
		return PAUSE_FAIL;
	}

	if (pause_func == F00_DIG) {

		PHYDM_DBG(p_dm, ODM_COMP_API, ("[DIG]\n"));

		if (val_lehgth != 1) {
			PHYDM_DBG(p_dm, ODM_COMP_API, ("[WARNING] val_length != 1\n"));
			return PAUSE_FAIL;
		}
		
		ori_val[0] = (u32)(p_dm->dm_dig_table.cur_ig_value); /*0xc50*/
		pause_lv_pre = &(p_dm->pause_lv_table.lv_dig);
		bkp_val = (u32*)(&(p_dm->dm_dig_table.rvrt_val));
		p_dm->phydm_func_handler.pause_phydm_handler = phydm_set_dig_val; /*function pointer hook*/
	
	} else
	
#ifdef PHYDM_SUPPORT_CCKPD
	if (pause_func == F05_CCK_PD) {
		
		PHYDM_DBG(p_dm, ODM_COMP_API, ("[CCK_PD]\n"));

		if (val_lehgth != 2) {
			PHYDM_DBG(p_dm, ODM_COMP_API, ("[WARNING] val_length != 2\n"));
			return PAUSE_FAIL;
		}
		
		ori_val[0] = p_dm->dm_cckpd_table.cur_cck_cca_thres; /*0xa0a*/
		ori_val[1] = p_dm->dm_cckpd_table.cck_cca_th_aaa;	/*0xaaa*/
		pause_lv_pre = &(p_dm->pause_lv_table.lv_cckpd);
		bkp_val = &(p_dm->dm_cckpd_table.rvrt_val[0]);
		p_dm->phydm_func_handler.pause_phydm_handler = phydm_set_cckpd_val; /*function pointer hook*/
		
	} else 
#endif

#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY
	if (pause_func == F06_ANT_DIV) {

		PHYDM_DBG(p_dm, ODM_COMP_API, ("[AntDiv]\n"));

		if (val_lehgth != 1) {
			PHYDM_DBG(p_dm, ODM_COMP_API, ("[WARNING] val_length != 1\n"));
			return PAUSE_FAIL;
		}
		
		ori_val[0] = (u32)(p_dm->dm_fat_table.rx_idle_ant); /*default antenna*/
		pause_lv_pre = &(p_dm->pause_lv_table.lv_antdiv);
		bkp_val = (u32*)(&(p_dm->dm_fat_table.rvrt_val));
		p_dm->phydm_func_handler.pause_phydm_handler = phydm_set_antdiv_val; /*function pointer hook*/
	
	} else
#endif

	if (pause_func == F13_ADPTVTY) {

		PHYDM_DBG(p_dm, ODM_COMP_API, ("[Adaptivity]\n"));

		if (val_lehgth != 2) {
			PHYDM_DBG(p_dm, ODM_COMP_API, ("[WARNING] val_length != 2\n"));
			return PAUSE_FAIL;
		}

		ori_val[0] = (u32)(p_dm->adaptivity.th_l2h);	/*th_l2h*/
		ori_val[1] = (u32)(p_dm->adaptivity.th_h2l);	/*th_h2l*/
		pause_lv_pre = &(p_dm->pause_lv_table.lv_adapt);
		bkp_val = (u32 *)(&(p_dm->adaptivity.rvrt_val));
		p_dm->phydm_func_handler.pause_phydm_handler = phydm_set_edcca_val; /*function pointer hook*/

	} else

	{
		PHYDM_DBG(p_dm, ODM_COMP_API, ("[WARNING] error func idx\n"));
		return PAUSE_FAIL;
	}

	PHYDM_DBG(p_dm, ODM_COMP_API, ("Pause_LV{new , pre} = {%d ,%d}\n", pause_lv, *pause_lv_pre));

	if ((pause_type == PHYDM_PAUSE) || (pause_type == PHYDM_PAUSE_NO_SET)) {
		
		if (pause_lv > *pause_lv_pre) {

			if (!(p_dm->pause_ability & pause_func_bitmap)) {

				for (i = 0; i < val_lehgth; i ++)
					bkp_val[i] = ori_val[i];
			}

			p_dm->pause_ability |= pause_func_bitmap;
			PHYDM_DBG(p_dm, ODM_COMP_API, ("pause_ability=0x%llx\n", p_dm->pause_ability));
			
			if (pause_type == PHYDM_PAUSE) {

				for (i = 0; i < val_lehgth; i ++) {
					PHYDM_DBG(p_dm, ODM_COMP_API, ("[PAUSE SUCCESS] val_idx[%d]{New, Ori}={0x%x, 0x%x}\n",i, val_buf[i], bkp_val[i]));
				/**/
				}
				p_dm->phydm_func_handler.pause_phydm_handler(p_dm, val_buf, val_lehgth);
			} else {
			
				for (i = 0; i < val_lehgth; i ++) {
					PHYDM_DBG(p_dm, ODM_COMP_API, ("[PAUSE NO Set: SUCCESS] val_idx[%d]{Ori}={0x%x}\n",i, bkp_val[i]));
				/**/
				}
			}

			*pause_lv_pre = pause_lv;
			return PAUSE_SUCCESS;
			
		} else {
			PHYDM_DBG(p_dm, ODM_COMP_API, ("[PAUSE FAIL] Pre_LV >= Curr_LV\n"));
			return PAUSE_FAIL;
		}

	} else if (pause_type == PHYDM_RESUME) {
		p_dm->pause_ability &= ~pause_func_bitmap;
		PHYDM_DBG(p_dm, ODM_COMP_API, ("pause_ability=0x%llx\n", p_dm->pause_ability));
		
		*pause_lv_pre = PHYDM_PAUSE_RELEASE;
		
		for (i = 0; i < val_lehgth; i ++) {
			PHYDM_DBG(p_dm, ODM_COMP_API, ("[RESUME] val_idx[%d]={0x%x}\n", i, bkp_val[i]));
		}
		
		p_dm->phydm_func_handler.pause_phydm_handler(p_dm, bkp_val, val_lehgth);
		
		return PAUSE_SUCCESS;
	} else {
		PHYDM_DBG(p_dm, ODM_COMP_API, ("[WARNING] error pause_type\n"));
		return PAUSE_FAIL;
	}
	
}

void
phydm_pause_func_console(
	void		*p_dm_void,
	char		input[][16],
	u32		*_used,
	char		*output,
	u32		*_out_len,
	u32		input_num
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	char		help[] = "-h";
	u32		var1[10] = {0};
	u32		used = *_used;
	u32		out_len = *_out_len;
	u32		i;
	u8		val_length = 0;
	u32		val_buf[5] = {0};
	u8		set_result = 0;
	enum phydm_func_idx_e	func = 0;
	enum phydm_pause_type	pause_type = 0;
	enum phydm_pause_level	pause_lv = 0;
	
	if ((strcmp(input[1], help) == 0)) {
		PHYDM_SNPRINTF((output + used, out_len - used, "{Func} {1:pause, 2:Resume} {lv} Val[5:0]\n"));
		
	} else {

		for (i = 0; i < 10; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);
			}
		}

		func = (enum phydm_func_idx_e)var1[0];
		pause_type = (enum phydm_pause_type)var1[1];
		pause_lv = (enum phydm_pause_level)var1[2];
	

		for (i = 0; i < 5; i++) {
			val_buf[i] = var1[3 + i];
		}

		if (func == F00_DIG) {
			PHYDM_SNPRINTF((output + used, out_len - used, "[DIG]\n"));
			val_length = 1;
			
		} else if (func == F05_CCK_PD) {
			PHYDM_SNPRINTF((output + used, out_len - used, "[CCK_PD]\n"));
			val_length = 2;
		} else if (func == F06_ANT_DIV) {
			PHYDM_SNPRINTF((output + used, out_len - used, "[Ant_Div]\n"));
			val_length = 1;
		} else if (func == F13_ADPTVTY) {
			PHYDM_SNPRINTF((output + used, out_len - used, "[Adaptivity]\n"));
			val_length = 2;
		} else {
			PHYDM_SNPRINTF((output + used, out_len - used, "[Set Function Error]\n"));
			val_length = 0;
		}

		if (val_length != 0) {
			
			PHYDM_SNPRINTF((output + used, out_len - used, "{%s, lv=%d} val = %d, %d}\n", 
				((pause_type == PHYDM_PAUSE) ? "Pause" : "Resume"), pause_lv, var1[3], var1[4]));
			
			set_result= phydm_pause_func(p_dm, func, pause_type, pause_lv, val_length, val_buf);
		}

		PHYDM_SNPRINTF((output + used, out_len - used, "set_result = %d\n", set_result));
	}


	*_used = used;
	*_out_len = out_len;
}

u8
phydm_stop_dm_watchdog_check(
	void					*p_dm_void
)
{
	struct PHY_DM_STRUCT			*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (p_dm->disable_phydm_watchdog == 1) {

		PHYDM_DBG(p_dm, DBG_COMMON_FLOW, ("Disable phydm\n"));
		return true;
		
	} else if (phydm_acs_check(p_dm) == true) {
	
		PHYDM_DBG(p_dm, DBG_COMMON_FLOW, ("Disable phydm by ACS\n"));
		return true;
		
	} else
		return false;
	
}

/*
 * 2011/09/20 MH This is the entry pointer for all team to execute HW out source DM.
 * You can not add any dummy function here, be care, you can only use DM structure
 * to perform any new ODM_DM.
 *   */
void
phydm_watchdog(
	struct PHY_DM_STRUCT		*p_dm
)
{
	PHYDM_DBG(p_dm, DBG_COMMON_FLOW, ("%s ======>\n", __func__));

	phydm_common_info_self_update(p_dm);
	phydm_rssi_monitor_check(p_dm);
	phydm_basic_dbg_message(p_dm);
	phydm_auto_dbg_engine(p_dm);
	phydm_receiver_blocking(p_dm);
	
	if (phydm_stop_dm_watchdog_check(p_dm) == true)
		return;

	phydm_hw_setting(p_dm);

	#if 0 /*(DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))*/
	if (*(p_dm->p_is_power_saving) == true) {

		PHYDM_DBG(p_dm, DBG_COMMON_FLOW, ("PHYDM power saving mode\n"));
		phydm_dig_by_rssi_lps(p_dm);
		phydm_adaptivity(p_dm);

		#if (DM_ODM_SUPPORT_TYPE & (ODM_CE))
		odm_antenna_diversity(p_dm); /*enable AntDiv in PS mode, request from SD4 Jeff*/
		#endif
		return;
	}
	#endif

	#ifdef PHYDM_TDMA_DIG_SUPPORT
	if (p_dm->original_dig_restore == 0)
		phydm_tdma_dig_timer_check(p_dm);
	else 
	#endif
	{
		odm_false_alarm_counter_statistics(p_dm);
		phydm_noisy_detection(p_dm);
		phydm_dig(p_dm);
		phydm_cck_pd_th(p_dm);
	}

	phydm_adaptivity(p_dm);
	phydm_ra_info_watchdog(p_dm);
	odm_path_diversity(p_dm);
	odm_cfo_tracking(p_dm);
	odm_dynamic_tx_power(p_dm);
	odm_antenna_diversity(p_dm);
	phydm_adaptive_soml(p_dm);
#ifdef CONFIG_DYNAMIC_RX_PATH
	phydm_dynamic_rx_path(p_dm);
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	phydm_beamforming_watchdog(p_dm);
#endif

	halrf_watchdog(p_dm);
	phydm_primary_cca(p_dm);

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	odm_dtc(p_dm);
#endif

	phydm_ccx_monitor(p_dm);

#ifdef PHYDM_LNA_SAT_CHK_SUPPORT
	phydm_lna_sat_chk_watchdog(p_dm);
#endif
	
#ifdef PHYDM_POWER_TRAINING_SUPPORT
	phydm_update_power_training_state(p_dm);
#endif

	phydm_common_info_self_reset(p_dm);

}


/*
 * Init /.. Fixed HW value. Only init time.
 *   */
void
odm_cmn_info_init(
	struct PHY_DM_STRUCT		*p_dm,
	enum odm_cmninfo_e	cmn_info,
	u64			value
)
{
	/*  */
	/* This section is used for init value */
	/*  */
	switch	(cmn_info) {
	/*  */
	/* Fixed ODM value. */
	/*  */
	case	ODM_CMNINFO_ABILITY:
		p_dm->support_ability = (u64)value;
		break;

	case	ODM_CMNINFO_RF_TYPE:
		p_dm->rf_type = (u8)value;
		break;

	case	ODM_CMNINFO_PLATFORM:
		p_dm->support_platform = (u8)value;
		break;

	case	ODM_CMNINFO_INTERFACE:
		p_dm->support_interface = (u8)value;
		break;

	case	ODM_CMNINFO_MP_TEST_CHIP:
		p_dm->is_mp_chip = (u8)value;
		break;

	case	ODM_CMNINFO_IC_TYPE:
		p_dm->support_ic_type = (u32)value;
		break;

	case	ODM_CMNINFO_CUT_VER:
		p_dm->cut_version = (u8)value;
		break;

	case	ODM_CMNINFO_FAB_VER:
		p_dm->fab_version = (u8)value;
		break;

	case	ODM_CMNINFO_RFE_TYPE:
		p_dm->rfe_type = (u8)value;
		phydm_init_hw_info_by_rfe(p_dm);
		break;

	case    ODM_CMNINFO_RF_ANTENNA_TYPE:
		p_dm->ant_div_type = (u8)value;
		break;

	case	ODM_CMNINFO_WITH_EXT_ANTENNA_SWITCH:
		p_dm->with_extenal_ant_switch = (u8)value;
		break;

	case    ODM_CMNINFO_BE_FIX_TX_ANT:
		p_dm->dm_fat_table.b_fix_tx_ant = (u8)value;
		break;

	case	ODM_CMNINFO_BOARD_TYPE:
		if (!p_dm->is_init_hw_info_by_rfe)
			p_dm->board_type = (u8)value;
		break;

	case	ODM_CMNINFO_PACKAGE_TYPE:
		if (!p_dm->is_init_hw_info_by_rfe)
			p_dm->package_type = (u8)value;
		break;

	case	ODM_CMNINFO_EXT_LNA:
		if (!p_dm->is_init_hw_info_by_rfe)
			p_dm->ext_lna = (u8)value;
		break;

	case	ODM_CMNINFO_5G_EXT_LNA:
		if (!p_dm->is_init_hw_info_by_rfe)
			p_dm->ext_lna_5g = (u8)value;
		break;

	case	ODM_CMNINFO_EXT_PA:
		if (!p_dm->is_init_hw_info_by_rfe)
			p_dm->ext_pa = (u8)value;
		break;

	case	ODM_CMNINFO_5G_EXT_PA:
		if (!p_dm->is_init_hw_info_by_rfe)
			p_dm->ext_pa_5g = (u8)value;
		break;

	case	ODM_CMNINFO_GPA:
		if (!p_dm->is_init_hw_info_by_rfe)
			p_dm->type_gpa = (u16)value;
		break;

	case	ODM_CMNINFO_APA:
		if (!p_dm->is_init_hw_info_by_rfe)
			p_dm->type_apa = (u16)value;
		break;

	case	ODM_CMNINFO_GLNA:
		if (!p_dm->is_init_hw_info_by_rfe)
			p_dm->type_glna = (u16)value;
		break;

	case	ODM_CMNINFO_ALNA:
		if (!p_dm->is_init_hw_info_by_rfe)
			p_dm->type_alna = (u16)value;
		break;

	case	ODM_CMNINFO_EXT_TRSW:
		if (!p_dm->is_init_hw_info_by_rfe)
			p_dm->ext_trsw = (u8)value;
		break;
	case	ODM_CMNINFO_EXT_LNA_GAIN:
		p_dm->ext_lna_gain = (u8)value;
		break;
	case	ODM_CMNINFO_PATCH_ID:
		p_dm->iot_table.win_patch_id = (u8)value;
		break;
	case	ODM_CMNINFO_BINHCT_TEST:
		p_dm->is_in_hct_test = (boolean)value;
		break;
	case	ODM_CMNINFO_BWIFI_TEST:
		p_dm->wifi_test = (u8)value;
		break;
	case	ODM_CMNINFO_SMART_CONCURRENT:
		p_dm->is_dual_mac_smart_concurrent = (boolean)value;
		break;
	case	ODM_CMNINFO_DOMAIN_CODE_2G:
		p_dm->odm_regulation_2_4g = (u8)value;
		break;
	case	ODM_CMNINFO_DOMAIN_CODE_5G:
		p_dm->odm_regulation_5g = (u8)value;
		break;
#if (DM_ODM_SUPPORT_TYPE &  (ODM_AP))
	case	ODM_CMNINFO_CONFIG_BB_RF:
		p_dm->config_bbrf = (boolean)value;
		break;
#endif
	case	ODM_CMNINFO_IQKPAOFF:
		p_dm->rf_calibrate_info.is_iqk_pa_off = (boolean)value;
		break;
	case	ODM_CMNINFO_REGRFKFREEENABLE:
		p_dm->rf_calibrate_info.reg_rf_kfree_enable = (u8)value;
		break;
	case	ODM_CMNINFO_RFKFREEENABLE:
		p_dm->rf_calibrate_info.rf_kfree_enable = (u8)value;
		break;
	case	ODM_CMNINFO_NORMAL_RX_PATH_CHANGE:
		p_dm->normal_rx_path = (u8)value;
		break;
	case	ODM_CMNINFO_EFUSE0X3D8:
		p_dm->efuse0x3d8 = (u8)value;
		break;
	case	ODM_CMNINFO_EFUSE0X3D7:
		p_dm->efuse0x3d7 = (u8)value;
		break;
	case	ODM_CMNINFO_ADVANCE_OTA:
		p_dm->p_advance_ota = (u8)value;
		break;
		
#ifdef CONFIG_PHYDM_DFS_MASTER
	case	ODM_CMNINFO_DFS_REGION_DOMAIN:
		p_dm->dfs_region_domain = (u8)value;
		break;
#endif
	case	ODM_CMNINFO_SOFT_AP_SPECIAL_SETTING:
		p_dm->soft_ap_special_setting = (u32)value;
		break;

	case	ODM_CMNINFO_DPK_EN:
		/*p_dm->dpk_en = (u1Byte)value;*/
		halrf_cmn_info_set(p_dm, HALRF_CMNINFO_DPK_EN, (u64)value);
		break;

	case	ODM_CMNINFO_HP_HWID:
		p_dm->hp_hw_id = (boolean)value;
		break;
	/* To remove the compiler warning, must add an empty default statement to handle the other values. */
	default:
		/* do nothing */
		break;

	}

}


void
odm_cmn_info_hook(
	struct PHY_DM_STRUCT		*p_dm,
	enum odm_cmninfo_e	cmn_info,
	void			*p_value
)
{
	/*  */
	/* Hook call by reference pointer. */
	/*  */
	switch	(cmn_info) {
	/*  */
	/* Dynamic call by reference pointer. */
	/*  */
	case	ODM_CMNINFO_TX_UNI:
		p_dm->p_num_tx_bytes_unicast = (u64 *)p_value;
		break;

	case	ODM_CMNINFO_RX_UNI:
		p_dm->p_num_rx_bytes_unicast = (u64 *)p_value;
		break;

	case	ODM_CMNINFO_BAND:
		p_dm->p_band_type = (u8 *)p_value;
		break;

	case	ODM_CMNINFO_SEC_CHNL_OFFSET:
		p_dm->p_sec_ch_offset = (u8 *)p_value;
		break;

	case	ODM_CMNINFO_SEC_MODE:
		p_dm->p_security = (u8 *)p_value;
		break;

	case	ODM_CMNINFO_BW:
		p_dm->p_band_width = (u8 *)p_value;
		break;

	case	ODM_CMNINFO_CHNL:
		p_dm->p_channel = (u8 *)p_value;
		break;

	case	ODM_CMNINFO_SCAN:
		p_dm->p_is_scan_in_process = (boolean *)p_value;
		break;

	case	ODM_CMNINFO_POWER_SAVING:
		p_dm->p_is_power_saving = (boolean *)p_value;
		break;

	case	ODM_CMNINFO_ONE_PATH_CCA:
		p_dm->p_one_path_cca = (u8 *)p_value;
		break;

	case	ODM_CMNINFO_DRV_STOP:
		p_dm->p_is_driver_stopped = (boolean *)p_value;
		break;

	case	ODM_CMNINFO_PNP_IN:
		p_dm->p_is_driver_is_going_to_pnp_set_power_sleep = (boolean *)p_value;
		break;

	case	ODM_CMNINFO_INIT_ON:
		p_dm->pinit_adpt_in_progress = (boolean *)p_value;
		break;

	case	ODM_CMNINFO_ANT_TEST:
		p_dm->p_antenna_test = (u8 *)p_value;
		break;

	case	ODM_CMNINFO_NET_CLOSED:
		p_dm->p_is_net_closed = (boolean *)p_value;
		break;

	case	ODM_CMNINFO_FORCED_RATE:
		p_dm->p_forced_data_rate = (u16 *)p_value;
		break;
	case	ODM_CMNINFO_ANT_DIV:
		p_dm->p_enable_antdiv = (u8 *)p_value;
		break;
	case	ODM_CMNINFO_ADAPTIVITY:
		p_dm->p_enable_adaptivity = (u8 *)p_value;
		break;

	case	ODM_CMNINFO_P2P_LINK:
		p_dm->dm_dig_table.is_p2p_in_process = (u8 *)p_value;
		break;

	case	ODM_CMNINFO_IS1ANTENNA:
		p_dm->p_is_1_antenna = (boolean *)p_value;
		break;

	case	ODM_CMNINFO_RFDEFAULTPATH:
		p_dm->p_rf_default_path = (u8 *)p_value;
		break;

	case	ODM_CMNINFO_FCS_MODE:
		p_dm->p_is_fcs_mode_enable = (boolean *)p_value;
		break;
	/*add by YuChen for beamforming PhyDM*/
	case	ODM_CMNINFO_HUBUSBMODE:
		p_dm->hub_usb_mode = (u8 *)p_value;
		break;
	case	ODM_CMNINFO_FWDWRSVDPAGEINPROGRESS:
		p_dm->p_is_fw_dw_rsvd_page_in_progress = (boolean *)p_value;
		break;
	case	ODM_CMNINFO_TX_TP:
		p_dm->p_current_tx_tp = (u32 *)p_value;
		break;
	case	ODM_CMNINFO_RX_TP:
		p_dm->p_current_rx_tp = (u32 *)p_value;
		break;
	case	ODM_CMNINFO_SOUNDING_SEQ:
		p_dm->p_sounding_seq = (u8 *)p_value;
		break;
#ifdef CONFIG_PHYDM_DFS_MASTER
	case	ODM_CMNINFO_DFS_MASTER_ENABLE:
		p_dm->dfs_master_enabled = (u8 *)p_value;
		break;
#endif
	case	ODM_CMNINFO_FORCE_TX_ANT_BY_TXDESC:
		p_dm->dm_fat_table.p_force_tx_ant_by_desc = (u8 *)p_value;
		break;
	case	ODM_CMNINFO_SET_S0S1_DEFAULT_ANTENNA:
		p_dm->dm_fat_table.p_default_s0_s1 = (u8 *)p_value;
		break;
	case	ODM_CMNINFO_SOFT_AP_MODE:
		p_dm->p_soft_ap_mode = (u32 *)p_value;
		break;
	case ODM_CMNINFO_MP_MODE:
		p_dm->p_mp_mode = (u8 *)p_value;
		break;
	case	ODM_CMNINFO_INTERRUPT_MASK:
		p_dm->p_interrupt_mask = (u32 *)p_value;
		break;
	case ODM_CMNINFO_BB_OPERATION_MODE:
		p_dm->p_bb_op_mode = (u8 *)p_value;
		break;

	default:
		/*do nothing*/
		break;

	}

}
/*
 * Update band/CHannel/.. The values are dynamic but non-per-packet.
 *   */
void
odm_cmn_info_update(
	struct PHY_DM_STRUCT		*p_dm,
	u32			cmn_info,
	u64			value
)
{
	/*  */
	/* This init variable may be changed in run time. */
	/*  */
	switch	(cmn_info) {
	case ODM_CMNINFO_LINK_IN_PROGRESS:
		p_dm->is_link_in_process = (boolean)value;
		break;

	case	ODM_CMNINFO_ABILITY:
		p_dm->support_ability = (u64)value;
		break;

	case	ODM_CMNINFO_RF_TYPE:
		p_dm->rf_type = (u8)value;
		break;

	case	ODM_CMNINFO_WIFI_DIRECT:
		p_dm->is_wifi_direct = (boolean)value;
		break;

	case	ODM_CMNINFO_WIFI_DISPLAY:
		p_dm->is_wifi_display = (boolean)value;
		break;

	case	ODM_CMNINFO_LINK:
		p_dm->is_linked = (boolean)value;
		break;

	case	ODM_CMNINFO_CMW500LINK:
		p_dm->iot_table.is_linked_cmw500 = (boolean)value;
		break;

	case	ODM_CMNINFO_STATION_STATE:
		p_dm->bsta_state = (boolean)value;
		break;

	case	ODM_CMNINFO_RSSI_MIN:
		p_dm->rssi_min = (u8)value;
		break;

	case	ODM_CMNINFO_RSSI_MIN_BY_PATH:
		p_dm->rssi_min_by_path = (u8)value;
		break;

	case	ODM_CMNINFO_DBG_COMP:
		p_dm->debug_components = (u64)value;
		break;

	case	ODM_CMNINFO_DBG_LEVEL:
		p_dm->debug_level = (u32)value;
		break;

#ifdef ODM_CONFIG_BT_COEXIST
	/* The following is for BT HS mode and BT coexist mechanism. */
	case ODM_CMNINFO_BT_ENABLED:
		p_dm->bt_info_table.is_bt_enabled = (boolean)value;
		break;

	case ODM_CMNINFO_BT_HS_CONNECT_PROCESS:
		p_dm->bt_info_table.is_bt_connect_process = (boolean)value;
		break;

	case ODM_CMNINFO_BT_HS_RSSI:
		p_dm->bt_info_table.bt_hs_rssi = (u8)value;
		break;

	case	ODM_CMNINFO_BT_OPERATION:
		p_dm->bt_info_table.is_bt_hs_operation = (boolean)value;
		break;

	case	ODM_CMNINFO_BT_LIMITED_DIG:
		p_dm->bt_info_table.is_bt_limited_dig = (boolean)value;
		break;
#endif

	case	ODM_CMNINFO_AP_TOTAL_NUM:
		p_dm->ap_total_num = (u8)value;
		break;

	case	ODM_CMNINFO_POWER_TRAINING:
		p_dm->is_disable_power_training = (boolean)value;
		break;

#ifdef CONFIG_PHYDM_DFS_MASTER
	case	ODM_CMNINFO_DFS_REGION_DOMAIN:
		p_dm->dfs_region_domain = (u8)value;
		break;
#endif

#if 0
	case	ODM_CMNINFO_OP_MODE:
		p_dm->op_mode = (u8)value;
		break;

	case	ODM_CMNINFO_BAND:
		p_dm->band_type = (u8)value;
		break;

	case	ODM_CMNINFO_SEC_CHNL_OFFSET:
		p_dm->sec_ch_offset = (u8)value;
		break;

	case	ODM_CMNINFO_SEC_MODE:
		p_dm->security = (u8)value;
		break;

	case	ODM_CMNINFO_BW:
		p_dm->band_width = (u8)value;
		break;

	case	ODM_CMNINFO_CHNL:
		p_dm->channel = (u8)value;
		break;
#endif
	default:
		/* do nothing */
		break;
	}


}

u32
phydm_cmn_info_query(
	struct PHY_DM_STRUCT		*p_dm,
	enum phydm_info_query_e		info_type
)
{
	struct phydm_fa_struct		*p_fa_t = &(p_dm->false_alm_cnt);
	struct phydm_dig_struct	*p_dig_t = &p_dm->dm_dig_table;
	struct _CCX_INFO			*ccx_info = &p_dm->dm_ccx_info;

	switch (info_type) {

	/*=== [FA Relative] ===========================================*/
	case PHYDM_INFO_FA_OFDM:
		return p_fa_t->cnt_ofdm_fail;

	case PHYDM_INFO_FA_CCK:
		return p_fa_t->cnt_cck_fail;

	case PHYDM_INFO_FA_TOTAL:
		return p_fa_t->cnt_all;

	case PHYDM_INFO_CCA_OFDM:
		return p_fa_t->cnt_ofdm_cca;

	case PHYDM_INFO_CCA_CCK:
		return p_fa_t->cnt_cck_cca;

	case PHYDM_INFO_CCA_ALL:
		return p_fa_t->cnt_cca_all;

	case PHYDM_INFO_CRC32_OK_VHT:
		return p_fa_t->cnt_vht_crc32_ok;

	case PHYDM_INFO_CRC32_OK_HT:
		return p_fa_t->cnt_ht_crc32_ok;

	case PHYDM_INFO_CRC32_OK_LEGACY:
		return p_fa_t->cnt_ofdm_crc32_ok;

	case PHYDM_INFO_CRC32_OK_CCK:
		return p_fa_t->cnt_cck_crc32_ok;

	case PHYDM_INFO_CRC32_ERROR_VHT:
		return p_fa_t->cnt_vht_crc32_error;

	case PHYDM_INFO_CRC32_ERROR_HT:
		return p_fa_t->cnt_ht_crc32_error;

	case PHYDM_INFO_CRC32_ERROR_LEGACY:
		return p_fa_t->cnt_ofdm_crc32_error;

	case PHYDM_INFO_CRC32_ERROR_CCK:
		return p_fa_t->cnt_cck_crc32_error;

	case PHYDM_INFO_EDCCA_FLAG:
		return p_fa_t->edcca_flag;

	case PHYDM_INFO_OFDM_ENABLE:
		return p_fa_t->ofdm_block_enable;

	case PHYDM_INFO_CCK_ENABLE:
		return p_fa_t->cck_block_enable;

	case PHYDM_INFO_DBG_PORT_0:
		return p_fa_t->dbg_port0;
				
	case PHYDM_INFO_CRC32_OK_HT_AGG:
		return p_fa_t->cnt_ht_crc32_ok_agg;
		
	case PHYDM_INFO_CRC32_ERROR_HT_AGG:
		return p_fa_t->cnt_ht_crc32_error_agg;
		
	/*=== [DIG] ================================================*/	
	
	case PHYDM_INFO_CURR_IGI:
		return p_dig_t->cur_ig_value;

	/*=== [RSSI] ===============================================*/
	case PHYDM_INFO_RSSI_MIN:
		return (u32)p_dm->rssi_min;
		
	case PHYDM_INFO_RSSI_MAX:
		return (u32)p_dm->rssi_max;

	case PHYDM_INFO_CLM_RATIO :
		return (u32)ccx_info->clm_ratio;
	case PHYDM_INFO_NHM_RATIO :
		return (u32)ccx_info->nhm_ratio;
	default:
		return 0xffffffff;

	}
}


#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void
odm_init_all_work_items(struct PHY_DM_STRUCT	*p_dm)
{

	struct _ADAPTER		*p_adapter = p_dm->adapter;
#if USE_WORKITEM

#ifdef CONFIG_DYNAMIC_RX_PATH
	odm_initialize_work_item(p_dm,
			 &p_dm->dm_drp_table.phydm_dynamic_rx_path_workitem,
		 (RT_WORKITEM_CALL_BACK)phydm_dynamic_rx_path_workitem_callback,
				 (void *)p_adapter,
				 "DynamicRxPathWorkitem");

#endif

#ifdef CONFIG_ADAPTIVE_SOML
	odm_initialize_work_item(p_dm,
			 &p_dm->dm_soml_table.phydm_adaptive_soml_workitem,
		 (RT_WORKITEM_CALL_BACK)phydm_adaptive_soml_workitem_callback,
				 (void *)p_adapter,
				 "AdaptiveSOMLWorkitem");
#endif

#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
	odm_initialize_work_item(p_dm,
		 &p_dm->dm_swat_table.phydm_sw_antenna_switch_workitem,
			 (RT_WORKITEM_CALL_BACK)odm_sw_antdiv_workitem_callback,
				 (void *)p_adapter,
				 "AntennaSwitchWorkitem");
#endif
#if (defined(CONFIG_HL_SMART_ANTENNA))
	odm_initialize_work_item(p_dm,
			 &p_dm->dm_sat_table.hl_smart_antenna_workitem,
		 (RT_WORKITEM_CALL_BACK)phydm_beam_switch_workitem_callback,
				 (void *)p_adapter,
				 "hl_smart_ant_workitem");

	odm_initialize_work_item(p_dm,
		 &p_dm->dm_sat_table.hl_smart_antenna_decision_workitem,
		 (RT_WORKITEM_CALL_BACK)phydm_beam_decision_workitem_callback,
				 (void *)p_adapter,
				 "hl_smart_ant_decision_workitem");
#endif

	odm_initialize_work_item(
		p_dm,
		&(p_dm->path_div_switch_workitem),
		(RT_WORKITEM_CALL_BACK)odm_path_div_chk_ant_switch_workitem_callback,
		(void *)p_adapter,
		"SWAS_WorkItem");

	odm_initialize_work_item(
		p_dm,
		&(p_dm->cck_path_diversity_workitem),
		(RT_WORKITEM_CALL_BACK)odm_cck_tx_path_diversity_work_item_callback,
		(void *)p_adapter,
		"CCKTXPathDiversityWorkItem");

	odm_initialize_work_item(
		p_dm,
		&(p_dm->ra_rpt_workitem),
		(RT_WORKITEM_CALL_BACK)halrf_update_init_rate_work_item_callback,
		(void *)p_adapter,
		"ra_rpt_workitem");

#if (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) || (defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
	odm_initialize_work_item(
		p_dm,
		&(p_dm->fast_ant_training_workitem),
		(RT_WORKITEM_CALL_BACK)odm_fast_ant_training_work_item_callback,
		(void *)p_adapter,
		"fast_ant_training_workitem");
#endif

#endif /*#if USE_WORKITEM*/

#if (BEAMFORMING_SUPPORT == 1)
	odm_initialize_work_item(
		p_dm,
		&(p_dm->beamforming_info.txbf_info.txbf_enter_work_item),
		(RT_WORKITEM_CALL_BACK)hal_com_txbf_enter_work_item_callback,
		(void *)p_adapter,
		"txbf_enter_work_item");

	odm_initialize_work_item(
		p_dm,
		&(p_dm->beamforming_info.txbf_info.txbf_leave_work_item),
		(RT_WORKITEM_CALL_BACK)hal_com_txbf_leave_work_item_callback,
		(void *)p_adapter,
		"txbf_leave_work_item");

	odm_initialize_work_item(
		p_dm,
		&(p_dm->beamforming_info.txbf_info.txbf_fw_ndpa_work_item),
		(RT_WORKITEM_CALL_BACK)hal_com_txbf_fw_ndpa_work_item_callback,
		(void *)p_adapter,
		"txbf_fw_ndpa_work_item");

	odm_initialize_work_item(
		p_dm,
		&(p_dm->beamforming_info.txbf_info.txbf_clk_work_item),
		(RT_WORKITEM_CALL_BACK)hal_com_txbf_clk_work_item_callback,
		(void *)p_adapter,
		"txbf_clk_work_item");

	odm_initialize_work_item(
		p_dm,
		&(p_dm->beamforming_info.txbf_info.txbf_rate_work_item),
		(RT_WORKITEM_CALL_BACK)hal_com_txbf_rate_work_item_callback,
		(void *)p_adapter,
		"txbf_rate_work_item");

	odm_initialize_work_item(
		p_dm,
		&(p_dm->beamforming_info.txbf_info.txbf_status_work_item),
		(RT_WORKITEM_CALL_BACK)hal_com_txbf_status_work_item_callback,
		(void *)p_adapter,
		"txbf_status_work_item");

	odm_initialize_work_item(
		p_dm,
		&(p_dm->beamforming_info.txbf_info.txbf_reset_tx_path_work_item),
		(RT_WORKITEM_CALL_BACK)hal_com_txbf_reset_tx_path_work_item_callback,
		(void *)p_adapter,
		"txbf_reset_tx_path_work_item");

	odm_initialize_work_item(
		p_dm,
		&(p_dm->beamforming_info.txbf_info.txbf_get_tx_rate_work_item),
		(RT_WORKITEM_CALL_BACK)hal_com_txbf_get_tx_rate_work_item_callback,
		(void *)p_adapter,
		"txbf_get_tx_rate_work_item");
#endif

	odm_initialize_work_item(
		p_dm,
		&(p_dm->adaptivity.phydm_pause_edcca_work_item),
		(RT_WORKITEM_CALL_BACK)phydm_pause_edcca_work_item_callback,
		(void *)p_adapter,
		"phydm_pause_edcca_work_item");

	odm_initialize_work_item(
		p_dm,
		&(p_dm->adaptivity.phydm_resume_edcca_work_item),
		(RT_WORKITEM_CALL_BACK)phydm_resume_edcca_work_item_callback,
		(void *)p_adapter,
		"phydm_resume_edcca_work_item");

#if (PHYDM_LA_MODE_SUPPORT == 1)
	odm_initialize_work_item(
		p_dm,
		&(p_dm->adcsmp.adc_smp_work_item),
		(RT_WORKITEM_CALL_BACK)adc_smp_work_item_callback,
		(void *)p_adapter,
		"adc_smp_work_item");

	odm_initialize_work_item(
		p_dm,
		&(p_dm->adcsmp.adc_smp_work_item_1),
		(RT_WORKITEM_CALL_BACK)adc_smp_work_item_callback,
		(void *)p_adapter,
		"adc_smp_work_item_1");
#endif

}

void
odm_free_all_work_items(struct PHY_DM_STRUCT	*p_dm)
{
#if USE_WORKITEM

#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
	odm_free_work_item(&(p_dm->dm_swat_table.phydm_sw_antenna_switch_workitem));
#endif

#ifdef CONFIG_DYNAMIC_RX_PATH
	odm_free_work_item(&(p_dm->dm_drp_table.phydm_dynamic_rx_path_workitem));
#endif

#ifdef CONFIG_ADAPTIVE_SOML
	odm_free_work_item(&(p_dm->dm_soml_table.phydm_adaptive_soml_workitem));
#endif


#if (defined(CONFIG_HL_SMART_ANTENNA))
	odm_free_work_item(&(p_dm->dm_sat_table.hl_smart_antenna_workitem));
	odm_free_work_item(&(p_dm->dm_sat_table.hl_smart_antenna_decision_workitem));
#endif

	odm_free_work_item(&(p_dm->path_div_switch_workitem));
	odm_free_work_item(&(p_dm->cck_path_diversity_workitem));
#if (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) || (defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
	odm_free_work_item(&(p_dm->fast_ant_training_workitem));
#endif
	odm_free_work_item(&(p_dm->ra_rpt_workitem));
	/*odm_free_work_item((&p_dm->sbdcnt_workitem));*/
#endif

#if (BEAMFORMING_SUPPORT == 1)
	odm_free_work_item((&p_dm->beamforming_info.txbf_info.txbf_enter_work_item));
	odm_free_work_item((&p_dm->beamforming_info.txbf_info.txbf_leave_work_item));
	odm_free_work_item((&p_dm->beamforming_info.txbf_info.txbf_fw_ndpa_work_item));
	odm_free_work_item((&p_dm->beamforming_info.txbf_info.txbf_clk_work_item));
	odm_free_work_item((&p_dm->beamforming_info.txbf_info.txbf_rate_work_item));
	odm_free_work_item((&p_dm->beamforming_info.txbf_info.txbf_status_work_item));
	odm_free_work_item((&p_dm->beamforming_info.txbf_info.txbf_reset_tx_path_work_item));
	odm_free_work_item((&p_dm->beamforming_info.txbf_info.txbf_get_tx_rate_work_item));
#endif

	odm_free_work_item((&p_dm->adaptivity.phydm_pause_edcca_work_item));
	odm_free_work_item((&p_dm->adaptivity.phydm_resume_edcca_work_item));

#if (PHYDM_LA_MODE_SUPPORT == 1)
	odm_free_work_item((&p_dm->adcsmp.adc_smp_work_item));
	odm_free_work_item((&p_dm->adcsmp.adc_smp_work_item_1));
#endif

}
#endif /*#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)*/

void
odm_init_all_timers(
	struct PHY_DM_STRUCT	*p_dm
)
{
#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	odm_ant_div_timers(p_dm, INIT_ANTDIV_TIMMER);
#endif

	phydm_adaptive_soml_timers(p_dm, INIT_SOML_TIMMER);

#ifdef PHYDM_LNA_SAT_CHK_SUPPORT
	phydm_lna_sat_chk_timers(p_dm, INIT_LNA_SAT_CHK_TIMMER);
#endif

#ifdef CONFIG_DYNAMIC_RX_PATH
	phydm_dynamic_rx_path_timers(p_dm, INIT_DRP_TIMMER);
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	odm_initialize_timer(p_dm, &p_dm->path_div_switch_timer,
		(void *)odm_path_div_chk_ant_switch_callback, NULL, "PathDivTimer");
	odm_initialize_timer(p_dm, &p_dm->cck_path_diversity_timer,
		(void *)odm_cck_tx_path_diversity_callback, NULL, "cck_path_diversity_timer");
	odm_initialize_timer(p_dm, &p_dm->sbdcnt_timer,
			     (void *)phydm_sbd_callback, NULL, "SbdTimer");
#if (BEAMFORMING_SUPPORT == 1)
	odm_initialize_timer(p_dm, &p_dm->beamforming_info.txbf_info.txbf_fw_ndpa_timer,
		(void *)hal_com_txbf_fw_ndpa_timer_callback, NULL, "txbf_fw_ndpa_timer");
#endif
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#if (BEAMFORMING_SUPPORT == 1)
	odm_initialize_timer(p_dm, &p_dm->beamforming_info.beamforming_timer,
		(void *)beamforming_sw_timer_callback, NULL, "beamforming_timer");
#endif
#endif
}

void
odm_cancel_all_timers(
	struct PHY_DM_STRUCT	*p_dm
)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	/*  */
	/* 2012/01/12 MH Temp BSOD fix. We need to find NIC allocate mem fail reason in */
	/* win7 platform. */
	/*  */
	HAL_ADAPTER_STS_CHK(p_dm);
#endif

#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	odm_ant_div_timers(p_dm, CANCEL_ANTDIV_TIMMER);
#endif

	phydm_adaptive_soml_timers(p_dm, CANCEL_SOML_TIMMER);

#ifdef PHYDM_LNA_SAT_CHK_SUPPORT
	phydm_lna_sat_chk_timers(p_dm, CANCEL_LNA_SAT_CHK_TIMMER);
#endif


#ifdef CONFIG_DYNAMIC_RX_PATH
	phydm_dynamic_rx_path_timers(p_dm, CANCEL_DRP_TIMMER);
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	odm_cancel_timer(p_dm, &p_dm->path_div_switch_timer);
	odm_cancel_timer(p_dm, &p_dm->cck_path_diversity_timer);
	odm_cancel_timer(p_dm, &p_dm->sbdcnt_timer);
#if (BEAMFORMING_SUPPORT == 1)
	odm_cancel_timer(p_dm, &p_dm->beamforming_info.txbf_info.txbf_fw_ndpa_timer);
#endif
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#if (BEAMFORMING_SUPPORT == 1)
	odm_cancel_timer(p_dm, &p_dm->beamforming_info.beamforming_timer);
#endif
#endif

}


void
odm_release_all_timers(
	struct PHY_DM_STRUCT	*p_dm
)
{
#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	odm_ant_div_timers(p_dm, RELEASE_ANTDIV_TIMMER);
#endif
	phydm_adaptive_soml_timers(p_dm, RELEASE_SOML_TIMMER);

#ifdef PHYDM_LNA_SAT_CHK_SUPPORT
	phydm_lna_sat_chk_timers(p_dm, RELEASE_LNA_SAT_CHK_TIMMER);
#endif

#ifdef CONFIG_DYNAMIC_RX_PATH
	phydm_dynamic_rx_path_timers(p_dm, RELEASE_DRP_TIMMER);
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	odm_release_timer(p_dm, &p_dm->path_div_switch_timer);
	odm_release_timer(p_dm, &p_dm->cck_path_diversity_timer);
	odm_release_timer(p_dm, &p_dm->sbdcnt_timer);
#if (BEAMFORMING_SUPPORT == 1)
	odm_release_timer(p_dm, &p_dm->beamforming_info.txbf_info.txbf_fw_ndpa_timer);
#endif
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#if (BEAMFORMING_SUPPORT == 1)
	odm_release_timer(p_dm, &p_dm->beamforming_info.beamforming_timer);
#endif
#endif
}


/* 3============================================================
 * 3 Tx Power Tracking
 * 3============================================================ */




#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
void
odm_init_all_threads(
	struct PHY_DM_STRUCT	*p_dm
)
{
#ifdef TPT_THREAD
	k_tpt_task_init(p_dm->priv);
#endif
}

void
odm_stop_all_threads(
	struct PHY_DM_STRUCT	*p_dm
)
{
#ifdef TPT_THREAD
	k_tpt_task_stop(p_dm->priv);
#endif
}
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
/* Justin: According to the current RRSI to adjust Response Frame TX power, 2012/11/05 */
void odm_dtc(struct PHY_DM_STRUCT *p_dm)
{
#ifdef CONFIG_DM_RESP_TXAGC
#define DTC_BASE            35	/* RSSI higher than this value, start to decade TX power */
#define DTC_DWN_BASE       (DTC_BASE-5)	/* RSSI lower than this value, start to increase TX power */

	/* RSSI vs TX power step mapping: decade TX power */
	static const u8 dtc_table_down[] = {
		DTC_BASE,
		(DTC_BASE + 5),
		(DTC_BASE + 10),
		(DTC_BASE + 15),
		(DTC_BASE + 20),
		(DTC_BASE + 25)
	};

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
		(DTC_DWN_BASE - 35)
	};

	u8 i;
	u8 dtc_steps = 0;
	u8 sign;
	u8 resp_txagc = 0;

#if 0
	/* As DIG is disabled, DTC is also disable */
	if (!(p_dm->support_ability & ODM_XXXXXX))
		return;
#endif

	if (p_dm->rssi_min > DTC_BASE) {
		/* need to decade the CTS TX power */
		sign = 1;
		for (i = 0; i < ARRAY_SIZE(dtc_table_down); i++) {
			if ((dtc_table_down[i] >= p_dm->rssi_min) || (dtc_steps >= 6))
				break;
			else
				dtc_steps++;
		}
	}
#if 0
	else if (p_dm->rssi_min > DTC_DWN_BASE) {
		/* needs to increase the CTS TX power */
		sign = 0;
		dtc_steps = 1;
		for (i = 0; i < ARRAY_SIZE(dtc_table_up); i++) {
			if ((dtc_table_up[i] <= p_dm->rssi_min) || (dtc_steps >= 10))
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
	odm_write_1byte(p_dm, 0x06d9, resp_txagc);

	PHYDM_DBG(p_dm, ODM_COMP_PWR_TRAIN, ("%s rssi_min:%u, set RESP_TXAGC to %s %u\n",
		__func__, p_dm->rssi_min, sign ? "minus" : "plus", dtc_steps));
#endif /* CONFIG_RESP_TXAGC_ADJUST */
}

#endif /* #if (DM_ODM_SUPPORT_TYPE == ODM_CE) */


/*<20170126, BB-Kevin>8188F D-CUT DC cancellation and 8821C*/
void
phydm_dc_cancellation(
	struct PHY_DM_STRUCT	*p_dm

)
{	
#ifdef PHYDM_DC_CANCELLATION
	u32		offset_i_hex[ODM_RF_PATH_MAX] = {0};
	u32		offset_q_hex[ODM_RF_PATH_MAX] = {0};
	u32		reg_value32[ODM_RF_PATH_MAX] = {0};
	u8		path = RF_PATH_A;

	if (!(p_dm->support_ic_type & ODM_DC_CANCELLATION_SUPPORT))
		return;

	if ((p_dm->support_ic_type & ODM_RTL8188F) && (p_dm->cut_version < ODM_CUT_D))
		return;

	/*DC_Estimation (only for 2x2 ic now) */

	for (path = RF_PATH_A; path < ODM_RF_PATH_MAX; path++) {
		if (p_dm->support_ic_type & (ODM_RTL8188F | ODM_RTL8710B)) {
			if (!phydm_set_bb_dbg_port(p_dm,
				BB_DBGPORT_PRIORITY_2, 0x235)) {/*set debug port to 0x235*/
				PHYDM_DBG(p_dm, ODM_COMP_API,
					("[DC Cancellation] Set Debug port Fail"));
				return;
			}
		} else if (p_dm->support_ic_type & (ODM_RTL8821C | ODM_RTL8822B)) {
			if (!phydm_set_bb_dbg_port(p_dm, BB_DBGPORT_PRIORITY_2, 0x200)) {
				/*set debug port to 0x200*/
				PHYDM_DBG(p_dm, ODM_COMP_API,
					("[DC Cancellation] Set Debug port Fail"));
				return;
			}
			phydm_bb_dbg_port_header_sel(p_dm, 0x0);
			if (p_dm->rf_type > RF_1T1R) {
				if (!phydm_set_bb_dbg_port(p_dm, BB_DBGPORT_PRIORITY_2, 0x202)) {
					/*set debug port to 0x200*/
					PHYDM_DBG(p_dm, ODM_COMP_API,
						("[DC Cancellation] Set Debug port Fail"));
					return;
				}
				phydm_bb_dbg_port_header_sel(p_dm, 0x0);
			}
		}
	
		odm_write_dig(p_dm, 0x7E);
	
		if (p_dm->support_ic_type & ODM_IC_11N_SERIES)
			odm_set_bb_reg(p_dm, 0x88c, BIT(21)|BIT(20), 0x3);
		else {
			odm_set_bb_reg(p_dm, 0xc00, BIT(1)|BIT(0), 0x0);
			if (p_dm->rf_type > RF_1T1R)
				odm_set_bb_reg(p_dm, 0xe00, BIT(1)|BIT(0), 0x0);
		}
		odm_set_bb_reg(p_dm, 0xa78, MASKBYTE1, 0x0); /*disable CCK DCNF*/
	
		PHYDM_DBG(p_dm, ODM_COMP_API, ("DC cancellation Begin!!!"));
	
		phydm_stop_ck320(p_dm, true);	/*stop ck320*/

		/* the same debug port both for path-a and path-b*/
		reg_value32[path] = phydm_get_bb_dbg_port_value(p_dm);

		phydm_stop_ck320(p_dm, false);	/*start ck320*/

		if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {
			odm_set_bb_reg(p_dm, 0x88c, BIT(21)|BIT(20), 0x0);
		} else {
			odm_set_bb_reg(p_dm, 0xc00, BIT(1)|BIT(0), 0x3);
			odm_set_bb_reg(p_dm, 0xe00, BIT(1)|BIT(0), 0x3);
		}
		odm_write_dig(p_dm, 0x20);
		phydm_release_bb_dbg_port(p_dm);

		PHYDM_DBG(p_dm, ODM_COMP_API, ("DC cancellation OK!!!"));
	}
		
	/*DC_Cancellation*/
	odm_set_bb_reg(p_dm, 0xa9c, BIT(20), 0x1); /*DC compensation to CCK data path*/
	if (p_dm->support_ic_type & (ODM_RTL8188F | ODM_RTL8710B)) {
		offset_i_hex[0] = (reg_value32[0] & 0xffc0000) >> 18;
		offset_q_hex[0] = (reg_value32[0] & 0x3ff00) >> 8;

		/*Before filling into registers, offset should be multiplexed (-1)*/
		offset_i_hex[0] = (offset_i_hex[0] >= 0x200) ? (0x400 - offset_i_hex[1]) : (0x1ff - offset_i_hex[1]);
		offset_q_hex[0] = (offset_q_hex[0] >= 0x200) ? (0x400 - offset_q_hex[1]) : (0x1ff - offset_q_hex[1]);

		odm_set_bb_reg(p_dm, 0x950, 0x1ff, offset_i_hex[1]);
		odm_set_bb_reg(p_dm, 0x950, 0x1ff0000, offset_q_hex[1]);
	} else if (p_dm->support_ic_type & (ODM_RTL8821C | ODM_RTL8822B)) {
	
		/* Path-a */
		offset_i_hex[0] = (reg_value32[0] & 0xffc00) >> 10;
		offset_q_hex[0] = reg_value32[0] & 0x3ff;

		/*Before filling into registers, offset should be multiplexed (-1)*/
		offset_i_hex[0] = 0x400 - offset_i_hex[0];
		offset_q_hex[0] = 0x400 - offset_q_hex[0];

		odm_set_bb_reg(p_dm, 0xc10, 0x3c000000, ((0x3c0 & offset_i_hex[0]) >> 6));
		odm_set_bb_reg(p_dm, 0xc10, 0xfc00, (0x3f & offset_i_hex[0]));
		odm_set_bb_reg(p_dm, 0xc14, 0x3c000000, ((0x3c0 & offset_q_hex[0]) >> 6));
		odm_set_bb_reg(p_dm, 0xc14, 0xfc00, (0x3f & offset_q_hex[0]));

		/* Path-b */
		if (p_dm->rf_type > RF_1T1R) {
			
			offset_i_hex[1] = (reg_value32[1] & 0xffc00) >> 10;
			offset_q_hex[1] = reg_value32[1] & 0x3ff;

		/*Before filling into registers, offset should be multiplexed (-1)*/
			offset_i_hex[1] = 0x400 - offset_i_hex[1];
			offset_q_hex[1] = 0x400 - offset_q_hex[1];

			odm_set_bb_reg(p_dm, 0xe10, 0x3c000000, ((0x3c0 & offset_i_hex[1]) >> 6));
			odm_set_bb_reg(p_dm, 0xe10, 0xfc00, (0x3f & offset_i_hex[1]));
			odm_set_bb_reg(p_dm, 0xe14, 0x3c000000, ((0x3c0 & offset_q_hex[1]) >> 6));
			odm_set_bb_reg(p_dm, 0xe14, 0xfc00, (0x3f & offset_q_hex[1]));
		}
	}
#endif
}

void
phydm_receiver_blocking(
	void *p_dm_void
)
{
#ifdef CONFIG_RECEIVER_BLOCKING
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32	channel = *p_dm->p_channel;
	u8	bw = *p_dm->p_band_width;
	u32	bb_regf0 = odm_get_bb_reg(p_dm, 0xf0, MASKDWORD);

	if (!(p_dm->support_ic_type & ODM_RECEIVER_BLOCKING_SUPPORT))
		return;

	if ((p_dm->support_ic_type & ODM_RTL8188E && ((bb_regf0 & 0xf000) >> 12) < 8) ||
		p_dm->support_ic_type & ODM_RTL8192E) { /*8188E_T version*/
		if (p_dm->consecutive_idlel_time > 10 && *p_dm->p_mp_mode == false && p_dm->adaptivity_enable == true) {
			if ((bw == CHANNEL_WIDTH_20) && (channel == 1)) {
				phydm_nbi_setting(p_dm, FUNC_ENABLE, channel, 20, 2410, PHYDM_DONT_CARE);
				p_dm->is_receiver_blocking_en = true;
			} else if ((bw == CHANNEL_WIDTH_20) && (channel == 13)) {
				phydm_nbi_setting(p_dm, FUNC_ENABLE, channel, 20, 2473, PHYDM_DONT_CARE);
				p_dm->is_receiver_blocking_en = true;
			} else if (p_dm->is_receiver_blocking_en && channel != 1 && channel != 13) {
				phydm_nbi_enable(p_dm, FUNC_DISABLE);
				odm_set_bb_reg(p_dm, 0xc40, 0x1f000000, 0x1f);
				p_dm->is_receiver_blocking_en = false;
			}
			return;
		}
	} else if ((p_dm->support_ic_type & ODM_RTL8188E && ((bb_regf0 & 0xf000) >> 12) >= 8)) { /*8188E_S version*/
		if (p_dm->consecutive_idlel_time > 10 && *p_dm->p_mp_mode == false && p_dm->adaptivity_enable == true) {
			if ((bw == CHANNEL_WIDTH_20) && (channel == 13)) {
				phydm_nbi_setting(p_dm, FUNC_ENABLE, channel, 20, 2473, PHYDM_DONT_CARE);
				p_dm->is_receiver_blocking_en = true;
			} else if (p_dm->is_receiver_blocking_en && channel != 13) {
				phydm_nbi_enable(p_dm, FUNC_DISABLE);
				odm_set_bb_reg(p_dm, 0xc40, 0x1f000000, 0x1f);
				p_dm->is_receiver_blocking_en = false;
			}
			return;
		}
	}

	if (p_dm->is_receiver_blocking_en) {
		phydm_nbi_enable(p_dm, FUNC_DISABLE);
		odm_set_bb_reg(p_dm, 0xc40, 0x1f000000, 0x1f);
		p_dm->is_receiver_blocking_en = false;
	}

#endif
}
