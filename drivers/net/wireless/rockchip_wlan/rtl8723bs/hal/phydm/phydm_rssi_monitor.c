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
 
#ifdef PHYDM_SUPPORT_RSSI_MONITOR

#ifdef PHYDM_3RD_REFORM_RSSI_MONOTOR
void
phydm_rssi_monitor_h2c(
	void	*p_dm_void,
	u8	macid
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_	*p_ra_t = &p_dm->dm_ra_table;
	struct cmn_sta_info			*p_sta = p_dm->p_phydm_sta_info[macid];
	struct ra_sta_info				*p_ra = NULL;
	u8		h2c_val[H2C_MAX_LENGTH] = {0};
	u8		stbc_en, ldpc_en;
	u8		bf_en = 0;
	u8		is_rx, is_tx;

	if (is_sta_active(p_sta)) {
		p_ra = &(p_sta->ra_info);
	} else {
		PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("[Warning] %s invalid sta_info\n", __func__));
		return;
	}
	
	PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("%s ======>\n", __func__));
	PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("MACID=%d\n", p_sta->mac_id));

	is_rx = (p_ra->txrx_state == RX_STATE) ? 1 : 0;
	is_tx = (p_ra->txrx_state == TX_STATE) ? 1 : 0;
	stbc_en = (p_sta->stbc_en) ? 1 : 0;
	ldpc_en = (p_sta->ldpc_en) ? 1 : 0;

	#ifdef CONFIG_BEAMFORMING
	if ((p_sta->bf_info.ht_beamform_cap & BEAMFORMING_HT_BEAMFORMEE_ENABLE) ||
		(p_sta->bf_info.vht_beamform_cap & BEAMFORMING_VHT_BEAMFORMEE_ENABLE)) {
		bf_en = 1;
	}
	#endif

	if (p_ra_t->RA_threshold_offset != 0) {
		PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("RA_th_ofst = (( %s%d ))\n",
			((p_ra_t->RA_offset_direction) ? "+" : "-"), p_ra_t->RA_threshold_offset));
	}

	h2c_val[0] = p_sta->mac_id;
	h2c_val[1] = 0;
	h2c_val[2] = p_sta->rssi_stat.rssi;
	h2c_val[3] = is_rx | (stbc_en << 1) | ((p_dm->noisy_decision & 0x1) << 2) |  (bf_en << 6);
	h2c_val[4] = (p_ra_t->RA_threshold_offset & 0x7f) | ((p_ra_t->RA_offset_direction & 0x1) << 7);
	h2c_val[5] = 0;
	h2c_val[6] = 0;

	PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("PHYDM h2c[0x42]=0x%x %x %x %x %x %x %x\n",
		h2c_val[6], h2c_val[5], h2c_val[4], h2c_val[3], h2c_val[2], h2c_val[1], h2c_val[0]));

	#if (RTL8188E_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8188E)
		odm_ra_set_rssi_8188e(p_dm, (u8)(p_sta->mac_id & 0xFF), p_sta->rssi_stat.rssi & 0x7F);
	else
	#endif 
	{
		odm_fill_h2c_cmd(p_dm, ODM_H2C_RSSI_REPORT, H2C_MAX_LENGTH, h2c_val);
	}
}

void
phydm_calculate_rssi_min_max(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct cmn_sta_info		*p_sta;
	s8	rssi_max_tmp = 0, rssi_min_tmp = 100;
	u8	i;
	u8	sta_cnt = 0;

	if (!p_dm->is_linked)
		return;

	PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("%s ======>\n", __func__));

	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
		p_sta = p_dm->p_phydm_sta_info[i];
		if (is_sta_active(p_sta)) {

			sta_cnt++;

			if (p_sta->rssi_stat.rssi < rssi_min_tmp)
				rssi_min_tmp = p_sta->rssi_stat.rssi;

			if (p_sta->rssi_stat.rssi > rssi_max_tmp)
				rssi_max_tmp = p_sta->rssi_stat.rssi;

			/*[Send RSSI to FW]*/
			if (p_sta->ra_info.disable_ra == false)
				phydm_rssi_monitor_h2c(p_dm, i);

			if (sta_cnt == p_dm->number_linked_client)
				break;
		}
	}

	p_dm->rssi_max = (u8)rssi_max_tmp;
	p_dm->rssi_min = (u8)rssi_min_tmp;

}
#endif


#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

s32
phydm_find_minimum_rssi(
	struct PHY_DM_STRUCT	*p_dm,
	struct _ADAPTER			*p_adapter,
	boolean					*p_is_link_temp

)
{
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
	PMGNT_INFO		p_mgnt_info = &(p_adapter->MgntInfo);
	boolean			act_as_ap = ACTING_AS_AP(p_adapter);

	/* 1.Determine the minimum RSSI */
	if ((!p_mgnt_info->bMediaConnect) ||
	    (act_as_ap && (p_hal_data->EntryMinUndecoratedSmoothedPWDB == 0))) {/* We should check AP mode and Entry info.into consideration, revised by Roger, 2013.10.18*/

		p_hal_data->MinUndecoratedPWDBForDM = 0;
		*p_is_link_temp = false;

	} else
		*p_is_link_temp = true;


	if (p_mgnt_info->bMediaConnect) {	/* Default port*/

		if (act_as_ap || p_mgnt_info->mIbss) {
			p_hal_data->MinUndecoratedPWDBForDM = p_hal_data->EntryMinUndecoratedSmoothedPWDB;
			/**/
		} else {
			p_hal_data->MinUndecoratedPWDBForDM = p_hal_data->UndecoratedSmoothedPWDB;
			/**/
		}
	} else { /* associated entry pwdb*/
		p_hal_data->MinUndecoratedPWDBForDM = p_hal_data->EntryMinUndecoratedSmoothedPWDB;
		/**/
	}

	return p_hal_data->MinUndecoratedPWDBForDM;
}

void
odm_rssi_monitor_check_mp(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm->dm_ra_table;
	u8			h2c_parameter[H2C_0X42_LENGTH] = {0};
	u32			i;
	boolean			is_ext_ra_info = true;
	u8			cmdlen = H2C_0X42_LENGTH;
	u8			tx_bf_en = 0, stbc_en = 0;

	struct _ADAPTER		*adapter = p_dm->adapter;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);
	struct sta_info		*p_entry = NULL;
	s32			tmp_entry_max_pwdb = 0, tmp_entry_min_pwdb = 0xff;
	PMGNT_INFO		p_mgnt_info = &adapter->MgntInfo;
	PMGNT_INFO		p_default_mgnt_info = &adapter->MgntInfo;
	u64			cur_tx_ok_cnt = 0, cur_rx_ok_cnt = 0;
#if (BEAMFORMING_SUPPORT == 1)
#ifndef BEAMFORMING_VERSION_1
	enum beamforming_cap beamform_cap = BEAMFORMING_CAP_NONE;
#endif
#endif
	struct _ADAPTER	*p_loop_adapter = GetDefaultAdapter(adapter);

	if (p_dm->support_ic_type == ODM_RTL8188E) {
		is_ext_ra_info = false;
		cmdlen = 3;
	}

	while (p_loop_adapter) {

		if (p_loop_adapter != NULL) {
			p_mgnt_info = &p_loop_adapter->MgntInfo;
			cur_tx_ok_cnt = p_loop_adapter->TxStats.NumTxBytesUnicast - p_mgnt_info->lastTxOkCnt;
			cur_rx_ok_cnt = p_loop_adapter->RxStats.NumRxBytesUnicast - p_mgnt_info->lastRxOkCnt;
			p_mgnt_info->lastTxOkCnt = cur_tx_ok_cnt;
			p_mgnt_info->lastRxOkCnt = cur_rx_ok_cnt;
		}

		for (i = 0; i < ASSOCIATE_ENTRY_NUM; i++) {

			if (IsAPModeExist(p_loop_adapter)) {
				if (GetFirstExtAdapter(p_loop_adapter) != NULL &&
				    GetFirstExtAdapter(p_loop_adapter) == p_loop_adapter)
				p_entry = AsocEntry_EnumStation(p_loop_adapter, i);
				else if (GetFirstGOPort(p_loop_adapter) != NULL &&
					 IsFirstGoAdapter(p_loop_adapter))
				p_entry = AsocEntry_EnumStation(p_loop_adapter, i);
			} else {
				if (GetDefaultAdapter(p_loop_adapter) == p_loop_adapter)
					p_entry = AsocEntry_EnumStation(p_loop_adapter, i);
			}

			if (p_entry != NULL) {
				if (p_entry->bAssociated) {

					RT_DISP_ADDR(FDM, DM_PWDB, ("p_entry->mac_addr ="), GET_STA_INFO(p_entry).mac_addr);
					RT_DISP(FDM, DM_PWDB, ("p_entry->rssi = 0x%x(%d)\n",
						GET_STA_INFO(p_entry).rssi_stat.rssi, GET_STA_INFO(p_entry).rssi_stat.rssi));

					/* 2 BF_en */
#if (BEAMFORMING_SUPPORT == 1)
#ifndef BEAMFORMING_VERSION_1
					beamform_cap = phydm_beamforming_get_entry_beam_cap_by_mac_id(p_dm, GET_STA_INFO(p_entry).mac_id);
					if (beamform_cap & (BEAMFORMER_CAP_HT_EXPLICIT | BEAMFORMER_CAP_VHT_SU))
						tx_bf_en = 1;
#else
					if (Beamform_GetSupportBeamformerCap(GetDefaultAdapter(adapter), p_entry))
						tx_bf_en = 1;
#endif
#endif
					/* 2 STBC_en */
					if ((IS_WIRELESS_MODE_AC(adapter) && TEST_FLAG(p_entry->VHTInfo.STBC, STBC_VHT_ENABLE_TX)) ||
						TEST_FLAG(p_entry->HTInfo.STBC, STBC_HT_ENABLE_TX))
						stbc_en = 1;

					if (GET_STA_INFO(p_entry).rssi_stat.rssi < tmp_entry_min_pwdb)
						tmp_entry_min_pwdb = GET_STA_INFO(p_entry).rssi_stat.rssi;
					if (GET_STA_INFO(p_entry).rssi_stat.rssi > tmp_entry_max_pwdb)
						tmp_entry_max_pwdb = GET_STA_INFO(p_entry).rssi_stat.rssi;

					h2c_parameter[4] = (p_ra_table->RA_threshold_offset & 0x7f) | (p_ra_table->RA_offset_direction << 7);
					PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("RA_threshold_offset = (( %s%d ))\n", ((p_ra_table->RA_threshold_offset == 0) ? " " : ((p_ra_table->RA_offset_direction) ? "+" : "-")), p_ra_table->RA_threshold_offset));

					if (is_ext_ra_info) {
						if (cur_rx_ok_cnt > (cur_tx_ok_cnt * 6))
							h2c_parameter[3] |= RAINFO_BE_RX_STATE;

						if (tx_bf_en)
							h2c_parameter[3] |= RAINFO_BF_STATE;
						else {
							if (stbc_en)
								h2c_parameter[3] |= RAINFO_STBC_STATE;
						}

						if (p_dm->noisy_decision)
							h2c_parameter[3] |= RAINFO_NOISY_STATE;
						else
							h2c_parameter[3] &= (~RAINFO_NOISY_STATE);

						if (p_dm->h2c_rarpt_connect) {
							h2c_parameter[3] |= RAINFO_INIT_RSSI_RATE_STATE;
							PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("h2c_rarpt_connect = (( %d ))\n", p_dm->h2c_rarpt_connect));
						}

					}

					h2c_parameter[2] = (u8)(GET_STA_INFO(p_entry).rssi_stat.rssi & 0xFF);
					/* h2c_parameter[1] = 0x20;*/ /* fw v12 cmdid 5:use max macid ,for nic ,default macid is 0 ,max macid is 1 */
					h2c_parameter[0] = (GET_STA_INFO(p_entry).mac_id);

					odm_fill_h2c_cmd(p_dm, ODM_H2C_RSSI_REPORT, cmdlen, h2c_parameter);
				}
			} else
				break;
		}

		p_loop_adapter = GetNextExtAdapter(p_loop_adapter);
	}


	/*Default port*/
	if (tmp_entry_max_pwdb != 0) {	/* If associated entry is found */
		p_hal_data->EntryMaxUndecoratedSmoothedPWDB = tmp_entry_max_pwdb;
		RT_DISP(FDM, DM_PWDB, ("EntryMaxPWDB = 0x%x(%d)\n",	tmp_entry_max_pwdb, tmp_entry_max_pwdb));
	} else
		p_hal_data->EntryMaxUndecoratedSmoothedPWDB = 0;

	if (tmp_entry_min_pwdb != 0xff) { /* If associated entry is found */
		p_hal_data->EntryMinUndecoratedSmoothedPWDB = tmp_entry_min_pwdb;
		RT_DISP(FDM, DM_PWDB, ("EntryMinPWDB = 0x%x(%d)\n", tmp_entry_min_pwdb, tmp_entry_min_pwdb));

	} else
		p_hal_data->EntryMinUndecoratedSmoothedPWDB = 0;

	/* Default porti sent RSSI to FW */
	if (p_hal_data->bUseRAMask) {
		PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("1 RA First Link, RSSI[%d] = ((%d)) , ra_rpt_linked = ((%d))\n",
			WIN_DEFAULT_PORT_MACID, p_hal_data->UndecoratedSmoothedPWDB, p_hal_data->ra_rpt_linked));
		if (p_hal_data->UndecoratedSmoothedPWDB > 0) {

			PRT_HIGH_THROUGHPUT			p_ht_info = GET_HT_INFO(p_default_mgnt_info);
			PRT_VERY_HIGH_THROUGHPUT	p_vht_info = GET_VHT_INFO(p_default_mgnt_info);

			/* BF_en*/
#if (BEAMFORMING_SUPPORT == 1)
#ifndef BEAMFORMING_VERSION_1
			beamform_cap = phydm_beamforming_get_entry_beam_cap_by_mac_id(p_dm, p_default_mgnt_info->m_mac_id);

			if (beamform_cap & (BEAMFORMER_CAP_HT_EXPLICIT | BEAMFORMER_CAP_VHT_SU))
				tx_bf_en = 1;
#else
			if (Beamform_GetSupportBeamformerCap(GetDefaultAdapter(adapter), NULL))
				tx_bf_en = 1;
#endif
#endif

			/* STBC_en*/
			if ((IS_WIRELESS_MODE_AC(adapter) && TEST_FLAG(p_vht_info->VhtCurStbc, STBC_VHT_ENABLE_TX)) ||
			    TEST_FLAG(p_ht_info->HtCurStbc, STBC_HT_ENABLE_TX))
				stbc_en = 1;

			h2c_parameter[4] = (p_ra_table->RA_threshold_offset & 0x7f) | (p_ra_table->RA_offset_direction << 7);
			PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("RA_threshold_offset = (( %s%d ))\n", ((p_ra_table->RA_threshold_offset == 0) ? " " : ((p_ra_table->RA_offset_direction) ? "+" : "-")), p_ra_table->RA_threshold_offset));

			if (is_ext_ra_info) {
				if (tx_bf_en)
					h2c_parameter[3] |= RAINFO_BF_STATE;
				else {
					if (stbc_en)
						h2c_parameter[3] |= RAINFO_STBC_STATE;
				}

				if (p_dm->h2c_rarpt_connect) {
					h2c_parameter[3] |= RAINFO_INIT_RSSI_RATE_STATE;
					PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("h2c_rarpt_connect = (( %d ))\n", p_dm->h2c_rarpt_connect));
				}


				if (p_dm->noisy_decision == 1) {
					h2c_parameter[3] |= RAINFO_NOISY_STATE;
					PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("[RSSIMonitorCheckMP] Send H2C to FW\n"));
				} else
					h2c_parameter[3] &= (~RAINFO_NOISY_STATE);

				PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("[RSSIMonitorCheckMP] h2c_parameter=%x\n", h2c_parameter[3]));
			}

			h2c_parameter[2] = (u8)(p_hal_data->UndecoratedSmoothedPWDB & 0xFF);
			/*h2c_parameter[1] = 0x20;*/	/* fw v12 cmdid 5:use max macid ,for nic ,default macid is 0 ,max macid is 1*/
			h2c_parameter[0] = WIN_DEFAULT_PORT_MACID;		/* fw v12 cmdid 5:use max macid ,for nic ,default macid is 0 ,max macid is 1*/

			odm_fill_h2c_cmd(p_dm, ODM_H2C_RSSI_REPORT, cmdlen, h2c_parameter);
		}
	
	} else
		PlatformEFIOWrite1Byte(adapter, 0x4fe, (u8)p_hal_data->UndecoratedSmoothedPWDB);

	{
		struct _ADAPTER *p_loop_adapter = GetDefaultAdapter(adapter);
		boolean		default_pointer_value, *p_is_link_temp = &default_pointer_value;
		s32	global_rssi_min = 0xFF, local_rssi_min;
		boolean		is_link = false;

		while (p_loop_adapter) {
			local_rssi_min = phydm_find_minimum_rssi(p_dm, p_loop_adapter, p_is_link_temp);
			/* dbg_print("p_hal_data->is_linked=%d, local_rssi_min=%d\n", p_hal_data->is_linked, local_rssi_min); */

			if (*p_is_link_temp)
				is_link = true;

			if ((local_rssi_min < global_rssi_min) && (*p_is_link_temp))
				global_rssi_min = local_rssi_min;

			p_loop_adapter = GetNextExtAdapter(p_loop_adapter);
		}

		p_hal_data->bLinked = is_link;

		p_dm->is_linked = is_link;
		p_dm->rssi_min = (u8)((is_link) ? global_rssi_min : 0);

	}


}

#endif

void
phydm_rssi_monitor_check(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (!(p_dm->support_ability & ODM_BB_RSSI_MONITOR))
		return;

	if ((p_dm->phydm_sys_up_time % 2) == 1) /*for AP watchdog period = 1 sec*/
		return;

	PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("%s ======>\n", __func__));

#ifdef PHYDM_3RD_REFORM_RSSI_MONOTOR
	phydm_calculate_rssi_min_max(p_dm);
#else
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	odm_rssi_monitor_check_mp(p_dm);
	#endif
#endif

	PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("RSSI {max, min} = {%d, %d}\n",
		p_dm->rssi_max, p_dm->rssi_min));

}

void
phydm_rssi_monitor_init(
	void		*p_dm_void
)
{

	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_	*p_ra_table = &p_dm->dm_ra_table;
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	struct _ADAPTER		*adapter = p_dm->adapter;
	HAL_DATA_TYPE		*p_hal_data = GET_HAL_DATA(adapter);

	p_ra_table->PT_collision_pre = true;	/*used in odm_dynamic_arfb_select(WIN only)*/

	p_hal_data->UndecoratedSmoothedPWDB = -1;
	p_hal_data->ra_rpt_linked = false;
#endif

	p_ra_table->firstconnect = false;
	p_dm->rssi_max = 0;
	p_dm->rssi_min = 0;

}

#endif
