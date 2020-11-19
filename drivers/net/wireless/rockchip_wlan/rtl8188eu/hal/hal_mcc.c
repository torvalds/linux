/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2015 - 2017 Realtek Corporation.
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
#ifdef CONFIG_MCC_MODE
#define _HAL_MCC_C_

#include <drv_types.h> /* PADAPTER */
#include <rtw_mcc.h> /* mcc structure */
#include <hal_data.h> /* HAL_DATA */
#include <rtw_pwrctrl.h> /* power control */

/*  use for AP/GO + STA/GC case */
#define MCC_DURATION_IDX 0 /* druration for station side */
#define MCC_TSF_SYNC_OFFSET_IDX 1
#define MCC_START_TIME_OFFSET_IDX 2
#define MCC_INTERVAL_IDX 3
#define MCC_GUARD_OFFSET0_IDX 4
#define MCC_GUARD_OFFSET1_IDX 5
#define MCC_STOP_THRESHOLD 6
#define TU 1024 /* 1 TU equals 1024 microseconds */
/* druration, TSF sync offset, start time offset, interval (unit:TU (1024 microseconds))*/
u8 mcc_switch_channel_policy_table[][7]={
	{20, 50, 40, 100, 0, 0, 30},
	{80, 50, 10, 100, 0, 0, 30},
	{36, 50, 32, 100, 0, 0, 30},
	{30, 50, 35, 100, 0, 0, 30},
};

const int mcc_max_policy_num = sizeof(mcc_switch_channel_policy_table) /sizeof(u8) /7;

static void dump_iqk_val_table(PADAPTER padapter)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	struct hal_iqk_reg_backup *iqk_reg_backup = pHalData->iqk_reg_backup;
	u8 total_rf_path = pHalData->NumTotalRFPath;
	u8 rf_path_idx = 0;
	u8 backup_chan_idx = 0;
	u8 backup_reg_idx = 0;

#ifdef CONFIG_MCC_MODE_V2
#else

	RTW_INFO("=============dump IQK backup table================\n");
	for (backup_chan_idx = 0; backup_chan_idx < MAX_IQK_INFO_BACKUP_CHNL_NUM; backup_chan_idx++) {
		for (rf_path_idx = 0; rf_path_idx < total_rf_path; rf_path_idx++) {
			for(backup_reg_idx = 0; backup_reg_idx < MAX_IQK_INFO_BACKUP_REG_NUM; backup_reg_idx++) {
				RTW_INFO("ch:%d. bw:%d. rf path:%d. reg[%d] = 0x%02x \n"
						, iqk_reg_backup[backup_chan_idx].central_chnl
						, iqk_reg_backup[backup_chan_idx].bw_mode
						, rf_path_idx
						, backup_reg_idx
						, iqk_reg_backup[backup_chan_idx].reg_backup[rf_path_idx][backup_reg_idx]
						);
			}
		}
	}	
	RTW_INFO("=============================================\n");

#endif
}

static void rtw_hal_mcc_build_p2p_noa_attr(PADAPTER padapter, u8 *ie, u32 *ie_len)
{
	struct mcc_adapter_priv *pmccadapriv = &padapter->mcc_adapterpriv;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *pmccobjpriv = &(dvobj->mcc_objpriv);
	u8 p2p_noa_attr_ie[MAX_P2P_IE_LEN] = {0x00};
	u32 p2p_noa_attr_len = 0;
	u8 noa_desc_num = 1;
	u8 opp_ps = 0; /* Disable OppPS */
	u8 noa_count = 255;
	u32 noa_duration;
	u32 noa_interval;
	u8 noa_index = 0;
	u8 mcc_policy_idx = 0;

	mcc_policy_idx = pmccobjpriv->policy_index;
	noa_duration = mcc_switch_channel_policy_table[mcc_policy_idx][MCC_DURATION_IDX] * TU;
	noa_interval = mcc_switch_channel_policy_table[mcc_policy_idx][MCC_INTERVAL_IDX] * TU;

	/* P2P OUI(4 bytes) */
	_rtw_memcpy(p2p_noa_attr_ie, P2P_OUI, 4);
	p2p_noa_attr_len = p2p_noa_attr_len + 4;

	/* attrute ID(1 byte) */
	p2p_noa_attr_ie[p2p_noa_attr_len] = P2P_ATTR_NOA;
	p2p_noa_attr_len = p2p_noa_attr_len + 1;
	
	/* attrute length(2 bytes) length = noa_desc_num*13 + 2 */
	RTW_PUT_LE16(p2p_noa_attr_ie + p2p_noa_attr_len, (noa_desc_num * 13 + 2));
	p2p_noa_attr_len = p2p_noa_attr_len + 2;

	/* Index (1 byte) */
	p2p_noa_attr_ie[p2p_noa_attr_len] = noa_index;
	p2p_noa_attr_len = p2p_noa_attr_len + 1;

	/* CTWindow and OppPS Parameters (1 byte) */
	p2p_noa_attr_ie[p2p_noa_attr_len] = opp_ps;
	p2p_noa_attr_len = p2p_noa_attr_len+ 1;

	/* NoA Count (1 byte) */
	p2p_noa_attr_ie[p2p_noa_attr_len] = noa_count;
	p2p_noa_attr_len = p2p_noa_attr_len + 1;

	/* NoA Duration (4 bytes) unit: microseconds */
	RTW_PUT_LE32(p2p_noa_attr_ie + p2p_noa_attr_len, noa_duration);
	p2p_noa_attr_len = p2p_noa_attr_len + 4;

	/* NoA Interval (4 bytes) unit: microseconds */
	RTW_PUT_LE32(p2p_noa_attr_ie + p2p_noa_attr_len, noa_interval);
	p2p_noa_attr_len = p2p_noa_attr_len + 4;

	/* NoA Start Time (4 bytes) unit: microseconds */
	RTW_PUT_LE32(p2p_noa_attr_ie + p2p_noa_attr_len, pmccadapriv->noa_start_time);
	if (0)
		RTW_INFO("indxe:%d, start_time=0x%02x:0x%02x:0x%02x:0x%02x\n"
		, noa_index
		, p2p_noa_attr_ie[p2p_noa_attr_len]
		, p2p_noa_attr_ie[p2p_noa_attr_len + 1]
		, p2p_noa_attr_ie[p2p_noa_attr_len + 2]
		, p2p_noa_attr_ie[p2p_noa_attr_len + 3]);

	p2p_noa_attr_len = p2p_noa_attr_len + 4;
	rtw_set_ie(ie, _VENDOR_SPECIFIC_IE_, p2p_noa_attr_len, (u8 *)p2p_noa_attr_ie, ie_len);
}


/**
 * rtw_hal_mcc_update_go_p2p_ie - update go p2p ie(add NoA attribute)
 * @padapter: the adapter to be update go p2p ie
 */
static void rtw_hal_mcc_update_go_p2p_ie(PADAPTER padapter)
{
	struct mcc_adapter_priv *pmccadapriv = &padapter->mcc_adapterpriv;
	struct mcc_obj_priv *mccobjpriv = &(adapter_to_dvobj(padapter)->mcc_objpriv);
	u8 *pos = NULL;


	/* no noa attribute, build it */
	if (pmccadapriv->p2p_go_noa_ie_len == 0)
		rtw_hal_mcc_build_p2p_noa_attr(padapter, pmccadapriv->p2p_go_noa_ie, &pmccadapriv->p2p_go_noa_ie_len);
	else {
		/* has noa attribut, modify it */
		u32 noa_duration = 0;
		
		/* update index */
		pos = pmccadapriv->p2p_go_noa_ie + pmccadapriv->p2p_go_noa_ie_len - 15;
		/* 0~255 */
		(*pos) = ((*pos) + 1) % 256;
		if (0)
			RTW_INFO("indxe:%d\n", (*pos));


		/* update duration */
		noa_duration = mcc_switch_channel_policy_table[mccobjpriv->policy_index][MCC_DURATION_IDX] * TU;
		pos = pmccadapriv->p2p_go_noa_ie + pmccadapriv->p2p_go_noa_ie_len - 12;
		RTW_PUT_LE32(pos, noa_duration);

		/* update start time */
		pos = pmccadapriv->p2p_go_noa_ie + pmccadapriv->p2p_go_noa_ie_len - 4;
		RTW_PUT_LE32(pos, pmccadapriv->noa_start_time);
		if (0)
			RTW_INFO("start_time=0x%02x:0x%02x:0x%02x:0x%02x\n"
			, ((u8*)(pos))[0]
			, ((u8*)(pos))[1]
			, ((u8*)(pos))[2]
			, ((u8*)(pos))[3]);

	}

	if (0) {
		RTW_INFO("p2p_go_noa_ie_len:%d\n", pmccadapriv->p2p_go_noa_ie_len);
		RTW_INFO_DUMP("\n", pmccadapriv->p2p_go_noa_ie, pmccadapriv->p2p_go_noa_ie_len);
	}
	update_beacon(padapter, _VENDOR_SPECIFIC_IE_, P2P_OUI, _TRUE);
}

/**
 * rtw_hal_mcc_remove_go_p2p_ie - remove go p2p ie(add NoA attribute)
 * @padapter: the adapter to be update go p2p ie
 */
static void rtw_hal_mcc_remove_go_p2p_ie(PADAPTER padapter)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mcc_adapter_priv *pmccadapriv = &padapter->mcc_adapterpriv;

	/* chech has noa ie or not */
	if (pmccadapriv->p2p_go_noa_ie_len == 0)
		return;

	pmccadapriv->p2p_go_noa_ie_len = 0;
	update_beacon(padapter, _VENDOR_SPECIFIC_IE_, P2P_OUI, _TRUE);
}

/* restore IQK value for all interface */
void rtw_hal_mcc_restore_iqk_val(PADAPTER padapter)
{
	u8 take_care_iqk = _FALSE;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	_adapter *iface = NULL;
	struct mcc_adapter_priv *mccadapriv = NULL;
	u8 i = 0;

	rtw_hal_get_hwreg(padapter, HW_VAR_CH_SW_NEED_TO_TAKE_CARE_IQK_INFO, &take_care_iqk);
	if (take_care_iqk == _TRUE && MCC_EN(padapter)) {
		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			if (iface == NULL)
				continue;

			mccadapriv = &iface->mcc_adapterpriv;
			if (mccadapriv->role == MCC_ROLE_MAX)
				continue;

			rtw_hal_ch_sw_iqk_info_restore(iface, CH_SW_USE_CASE_MCC);
		}
	}

	if (0)
		dump_iqk_val_table(padapter);
}

u8 rtw_hal_check_mcc_status(PADAPTER padapter, u8 mcc_status)
{
	struct mcc_obj_priv *pmccobjpriv = &(adapter_to_dvobj(padapter)->mcc_objpriv);

	if (pmccobjpriv->mcc_status & (mcc_status))
		return _TRUE;
	else
		return _FALSE;
}

void rtw_hal_set_mcc_status(PADAPTER padapter, u8 mcc_status)
{
	struct mcc_obj_priv *pmccobjpriv = &(adapter_to_dvobj(padapter)->mcc_objpriv);

	pmccobjpriv->mcc_status |= (mcc_status);
}

void rtw_hal_clear_mcc_status(PADAPTER padapter, u8 mcc_status)
{
	struct mcc_obj_priv *pmccobjpriv = &(adapter_to_dvobj(padapter)->mcc_objpriv);

	pmccobjpriv->mcc_status &= (~mcc_status);
}

static void rtw_hal_mcc_update_policy_table(PADAPTER adapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct mcc_obj_priv *mccobjpriv = &(dvobj->mcc_objpriv);
	u8 mcc_duration = mccobjpriv->duration;
	s8 mcc_policy_idx = mccobjpriv->policy_index;
	u8 interval = mcc_switch_channel_policy_table[mcc_policy_idx][MCC_INTERVAL_IDX];
	u8 new_mcc_duration_time = 0;
	u8 new_starttime_offset = 0;

	/* convert % to ms */
	new_mcc_duration_time = mcc_duration * interval / 100;

	/* start time offset = (interval - duration time)/2 */
	new_starttime_offset = (interval - new_mcc_duration_time) >> 1;

	/* update modified parameters */
	mcc_switch_channel_policy_table[mcc_policy_idx][MCC_DURATION_IDX]
		= new_mcc_duration_time;

	mcc_switch_channel_policy_table[mcc_policy_idx][MCC_START_TIME_OFFSET_IDX]
		= new_starttime_offset;
	

}

static void rtw_hal_config_mcc_switch_channel_setting(PADAPTER padapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *mccobjpriv = &(dvobj->mcc_objpriv);
	struct registry_priv *registry_par = &padapter->registrypriv;
	u8 mcc_duration = 0;
	s8 mcc_policy_idx = 0;

	mcc_policy_idx = registry_par->rtw_mcc_policy_table_idx;
	mcc_duration = mccobjpriv->duration;

	if (mcc_policy_idx < 0 || mcc_policy_idx >= mcc_max_policy_num) {
		mccobjpriv->policy_index = 0;
		RTW_INFO("[MCC] can't find table(%d), use default policy(%d)\n",
			mcc_policy_idx, mccobjpriv->policy_index);
	} else
		mccobjpriv->policy_index = mcc_policy_idx;

	/* convert % to time */
	if (mcc_duration != 0)
		rtw_hal_mcc_update_policy_table(padapter);

	RTW_INFO("[MCC] policy(%d): %d,%d,%d,%d,%d,%d\n"
		, mccobjpriv->policy_index
		, mcc_switch_channel_policy_table[mccobjpriv->policy_index][MCC_DURATION_IDX]
		, mcc_switch_channel_policy_table[mccobjpriv->policy_index][MCC_TSF_SYNC_OFFSET_IDX]
		, mcc_switch_channel_policy_table[mccobjpriv->policy_index][MCC_START_TIME_OFFSET_IDX]
		, mcc_switch_channel_policy_table[mccobjpriv->policy_index][MCC_INTERVAL_IDX]
		, mcc_switch_channel_policy_table[mccobjpriv->policy_index][MCC_GUARD_OFFSET0_IDX]
		, mcc_switch_channel_policy_table[mccobjpriv->policy_index][MCC_GUARD_OFFSET1_IDX]);

}

static void rtw_hal_mcc_assign_tx_threshold(PADAPTER padapter) 
{
	struct registry_priv *preg = &padapter->registrypriv;
	struct mcc_adapter_priv *pmccadapriv = &padapter->mcc_adapterpriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	switch (pmccadapriv->role) {
	case MCC_ROLE_STA:
	case MCC_ROLE_GC:
		switch (pmlmeext->cur_bwmode) {
		case CHANNEL_WIDTH_20:
			/*
			* target tx byte(bytes) = target tx tp(Mbits/sec) * 1024 * 1024 / 8 * (duration(ms) / 1024)
			*					= target tx tp(Mbits/sec) * 128 * duration(ms)
			* note:
			* target tx tp(Mbits/sec) * 1024 * 1024 / 8 ==> Mbits to bytes
			* duration(ms) / 1024 ==> msec to sec
			*/
			pmccadapriv->mcc_target_tx_bytes_to_port = preg->rtw_mcc_sta_bw20_target_tx_tp * 128 * pmccadapriv->mcc_duration;
			break;
		case CHANNEL_WIDTH_40:
			pmccadapriv->mcc_target_tx_bytes_to_port = preg->rtw_mcc_sta_bw40_target_tx_tp * 128 * pmccadapriv->mcc_duration;
			break;
		case CHANNEL_WIDTH_80:
			pmccadapriv->mcc_target_tx_bytes_to_port = preg->rtw_mcc_sta_bw80_target_tx_tp * 128 * pmccadapriv->mcc_duration;
			break;
		case CHANNEL_WIDTH_160:
		case CHANNEL_WIDTH_80_80:
			RTW_INFO(FUNC_ADPT_FMT": not support bwmode = %d\n"
				, FUNC_ADPT_ARG(padapter), pmlmeext->cur_bwmode);
			break;
		}
		break;
	case MCC_ROLE_AP:
	case MCC_ROLE_GO:
		switch (pmlmeext->cur_bwmode) {
		case CHANNEL_WIDTH_20:
			pmccadapriv->mcc_target_tx_bytes_to_port = preg->rtw_mcc_ap_bw20_target_tx_tp * 128 * pmccadapriv->mcc_duration;
			break;
		case CHANNEL_WIDTH_40:
			pmccadapriv->mcc_target_tx_bytes_to_port = preg->rtw_mcc_ap_bw40_target_tx_tp * 128 * pmccadapriv->mcc_duration;
			break;
		case CHANNEL_WIDTH_80:
			pmccadapriv->mcc_target_tx_bytes_to_port = preg->rtw_mcc_ap_bw80_target_tx_tp * 128 * pmccadapriv->mcc_duration;
			break;
		case CHANNEL_WIDTH_160:
		case CHANNEL_WIDTH_80_80:
			RTW_INFO(FUNC_ADPT_FMT": not support bwmode = %d\n"
				, FUNC_ADPT_ARG(padapter), pmlmeext->cur_bwmode);
			break;
		}
		break;
	default:
		RTW_INFO(FUNC_ADPT_FMT": unknown role = %d\n"
			, FUNC_ADPT_ARG(padapter), pmccadapriv->role);
		break;
	}
}

#ifdef CONFIG_MCC_PHYDM_OFFLOAD
static void mcc_cfg_phdym_rf_ch (_adapter *adapter)
{
		struct mcc_adapter_priv *mccadapriv = &adapter->mcc_adapterpriv;
		struct mlme_ext_priv *mlmeext = &adapter->mlmeextpriv;
		HAL_DATA_TYPE *hal = GET_HAL_DATA(adapter);
		struct dm_struct *dm = &hal->odmpriv;
		struct _phydm_mcc_dm_ *mcc_dm = &dm->mcc_dm;
		u8 order = 0;

		set_channel_bwmode(adapter, mlmeext->cur_channel, mlmeext->cur_ch_offset, mlmeext->cur_bwmode);
		order = mccadapriv->order;
		mcc_dm->mcc_rf_channel[order] = phy_query_rf_reg(adapter, RF_PATH_A, 0x18, 0xffffffff);
}

static void mcc_cfg_phdym_update_macid (_adapter *adapter, u8 add, u8 mac_id)
{
		struct mcc_adapter_priv *mccadapriv = &adapter->mcc_adapterpriv;
		struct mlme_ext_priv *mlmeext = &adapter->mlmeextpriv;
		HAL_DATA_TYPE *hal = GET_HAL_DATA(adapter);
		struct dm_struct *dm = &hal->odmpriv;
		struct _phydm_mcc_dm_ *mcc_dm = &dm->mcc_dm;
		u8 order = 0, i = 0;

		order = mccadapriv->order;
		if (add) {
			for (i = 0; i < NUM_STA; i++) {
				if (mcc_dm->sta_macid[order][i] == 0xff) {
					mcc_dm->sta_macid[order][i] = mac_id;
					break;
				}
			}
		} else {
			for (i = 0; i < NUM_STA; i++) {
				if (mcc_dm->sta_macid[order][i] == mac_id) {
					mcc_dm->sta_macid[order][i] = 0xff;
					break;
				}
			}
		}

		
}

static void mcc_cfg_phdym_start(_adapter *adapter, u8 start)
{
	struct dvobj_priv *dvobj;
	struct mcc_obj_priv *mccobjpriv;
	HAL_DATA_TYPE *hal;
	struct dm_struct *dm;
	struct _phydm_mcc_dm_ *mcc_dm;
	u8 rfk_forbidden = _TRUE;
	u8 i = 0, j = 0;

	dvobj = adapter_to_dvobj(adapter);
	mccobjpriv = adapter_to_mccobjpriv(adapter);
	hal = GET_HAL_DATA(adapter);
	dm = &hal->odmpriv;
	mcc_dm = &dm->mcc_dm;

	if (start) {
		#ifdef CONFIG_MCC_PHYDM_OFFLOAD
		mcc_dm->mcc_status = mccobjpriv->mcc_phydm_offload;
		#endif

		rfk_forbidden = _TRUE;
		halrf_cmn_info_set(dm, HALRF_CMNINFO_RFK_FORBIDDEN, rfk_forbidden);
	} else {
		rfk_forbidden = _FALSE;
		halrf_cmn_info_set(dm, HALRF_CMNINFO_RFK_FORBIDDEN, rfk_forbidden);

		#ifdef CONFIG_MCC_PHYDM_OFFLOAD
		for(i = 0; i < MAX_MCC_NUM; i ++) {
			for(j = 0; j < NUM_STA; j ++) {
				if (mcc_dm->sta_macid[i][j] != 0xff)
					/* clear all used value for mcc stop */
					/* do nothing for mcc start due to phydm will init to 0xff */
					mcc_dm->sta_macid[i][j] = 0xff;
			}
			mcc_dm->mcc_rf_channel[i] = 0xff;
		}
		mcc_dm->mcc_status = 0;
		#endif
	}
}

static void mcc_cfg_phdym_dump(_adapter *adapter, void *sel)
{
	HAL_DATA_TYPE *hal;
	struct dm_struct *dm;
	struct _phydm_mcc_dm_ *mcc_dm;
	u8 rfk_forbidden = _TRUE;
	u8 i = 0, j = 0;


	hal = GET_HAL_DATA(adapter);
	dm = &hal->odmpriv;
	mcc_dm = &dm->mcc_dm;

	rfk_forbidden = halrf_cmn_info_get(dm, HALRF_CMNINFO_RFK_FORBIDDEN);
	RTW_PRINT_SEL(sel, "dump mcc dm info\n");
	RTW_PRINT_SEL(sel, "mcc_status=%d\n", mcc_dm->mcc_status);
	RTW_PRINT_SEL(sel, "rfk_forbidden=%d\n", rfk_forbidden);
	for(i = 0; i < MAX_MCC_NUM; i ++) {

		if (mcc_dm->mcc_rf_channel[i] != 0xff)
			RTW_PRINT_SEL(sel, "mcc_dm->mcc_rf_channel[%d] = 0x%02x\n", i, mcc_dm->mcc_rf_channel[i]);
		
		for(j = 0; j < NUM_STA; j ++) {
			if (mcc_dm->sta_macid[i][j] != 0xff)
				RTW_PRINT_SEL(sel, "mcc_dm->sta_macid[%d][%d] = %d\n", i, j, mcc_dm->sta_macid[i][j]);
		}
	}
}

static void mcc_cfg_phdym_offload(_adapter *adapter, u8 enable)
{
	struct mcc_obj_priv *mccobjpriv = adapter_to_mccobjpriv(adapter);
	_adapter *iface = NULL;
	struct mcc_adapter_priv *mccadapriv = NULL;
	HAL_DATA_TYPE *hal = NULL;
	struct dm_struct *dm = NULL;
	struct _phydm_mcc_dm_ *mcc_dm = NULL;
	struct sta_priv *stapriv = NULL;
	struct sta_info *sta = NULL;
	struct wlan_network *cur_network = NULL;
	_irqL irqL;
	_list	*head = NULL, *list = NULL;
	u8 i = 0;


	hal = GET_HAL_DATA(adapter);
	dm = &hal->odmpriv;
	mcc_dm = &dm->mcc_dm;

	/* due to phydm will rst related date, driver must set related data */
	if (enable) {
		for (i = 0; i < MAX_MCC_NUM; i++) {
			iface = mccobjpriv->iface[i];
			if (!iface)
				continue;
			stapriv = &iface->stapriv;
			mccadapriv = &iface->mcc_adapterpriv;
			switch (mccadapriv->role) {
			case MCC_ROLE_STA:
			case MCC_ROLE_GC:
				cur_network = &iface->mlmepriv.cur_network;
				sta = rtw_get_stainfo(stapriv, cur_network->network.MacAddress);
				if (sta)
					mcc_cfg_phdym_update_macid(iface, _TRUE, sta->cmn.mac_id);
				break;
			case MCC_ROLE_AP:
			case MCC_ROLE_GO:
				_enter_critical_bh(&stapriv->asoc_list_lock, &irqL);

				head = &stapriv->asoc_list;
				list = get_next(head);
		
				while ((rtw_end_of_queue_search(head, list)) == _FALSE) {
					sta = LIST_CONTAINOR(list, struct sta_info, asoc_list);
					list = get_next(list);
					mcc_cfg_phdym_update_macid(iface, _TRUE, sta->cmn.mac_id);
				}

				_exit_critical_bh(&stapriv->asoc_list_lock, &irqL);
				break;
			default:
				RTW_INFO("Unknown role\n");
				rtw_warn_on(1);
				break;
			}
			
		}
	}

	mcc_dm->mcc_status = enable;
}

static void rtw_hal_mcc_cfg_phydm (_adapter *adapter, enum mcc_cfg_phydm_ops ops, void *data)
{
	switch (ops) {
	case MCC_CFG_PHYDM_OFFLOAD:
		mcc_cfg_phdym_offload(adapter, *(u8 *)data);
		break;
	case MCC_CFG_PHYDM_RF_CH:
		mcc_cfg_phdym_rf_ch(adapter);
		break;
	case MCC_CFG_PHYDM_ADD_CLIENT:
		mcc_cfg_phdym_update_macid(adapter, _TRUE, *(u8 *)data);
		break;
	case MCC_CFG_PHYDM_REMOVE_CLIENT:
		mcc_cfg_phdym_update_macid(adapter, _FALSE, *(u8 *)data);
		break;
	case MCC_CFG_PHYDM_START:
		mcc_cfg_phdym_start(adapter, _TRUE);
		break;
	case MCC_CFG_PHYDM_STOP:
		mcc_cfg_phdym_start(adapter, _FALSE);
		break;
	case MCC_CFG_PHYDM_DUMP:
		mcc_cfg_phdym_dump(adapter, data);
		break;
	case MCC_CFG_PHYDM_MAX:
	default:
		RTW_ERR("[MCC] rtw_hal_mcc_cfg_phydm ops error (%d)\n", ops);
		break;

	}
}
#endif

static void rtw_hal_config_mcc_role_setting(PADAPTER padapter, u8 order)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *pmccobjpriv = &(pdvobjpriv->mcc_objpriv);
	struct mcc_adapter_priv *pmccadapriv = &padapter->mcc_adapterpriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct wlan_network *cur_network = &(pmlmepriv->cur_network);
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta = NULL;
	struct registry_priv *preg = &padapter->registrypriv;
	_irqL irqL;
	_list	*phead =NULL, *plist = NULL;
	u8 policy_index = 0;
	u8 mcc_duration = 0;
	u8 mcc_interval = 0;
	u8 starting_ap_num = DEV_AP_STARTING_NUM(pdvobjpriv);
	u8 ap_num = DEV_AP_NUM(pdvobjpriv);

	policy_index = pmccobjpriv->policy_index;
	mcc_duration = mcc_switch_channel_policy_table[pmccobjpriv->policy_index][MCC_DURATION_IDX]
		- mcc_switch_channel_policy_table[pmccobjpriv->policy_index][MCC_GUARD_OFFSET0_IDX]
			- mcc_switch_channel_policy_table[pmccobjpriv->policy_index][MCC_GUARD_OFFSET1_IDX];
	mcc_interval = mcc_switch_channel_policy_table[pmccobjpriv->policy_index][MCC_INTERVAL_IDX];

	if (starting_ap_num == 0 && ap_num == 0) {
		pmccadapriv->order = order;

		if (pmccadapriv->order == 0) {
			/* setting is smiliar to GO/AP */
			/* pmccadapriv->mcc_duration = mcc_interval - mcc_duration;*/
			pmccadapriv->mgmt_queue_macid = MCC_ROLE_SOFTAP_GO_MGMT_QUEUE_MACID;
		} else if (pmccadapriv->order == 1) {
			/* pmccadapriv->mcc_duration = mcc_duration; */
			pmccadapriv->mgmt_queue_macid = MCC_ROLE_STA_GC_MGMT_QUEUE_MACID;
		} else {
			RTW_INFO("[MCC] not support >= 3 interface\n");
			rtw_warn_on(1);
		}

		rtw_hal_mcc_assign_tx_threshold(padapter);

		psta = rtw_get_stainfo(pstapriv, cur_network->network.MacAddress);
		if (psta) {
			/* combine AP/GO macid and mgmt queue macid to bitmap */
			pmccadapriv->mcc_macid_bitmap = BIT(psta->cmn.mac_id) | BIT(pmccadapriv->mgmt_queue_macid);
			#ifdef CONFIG_MCC_PHYDM_OFFLOAD
			rtw_hal_mcc_cfg_phydm(padapter, MCC_CFG_PHYDM_ADD_CLIENT, &psta->cmn.mac_id);
			#endif
		} else {
			RTW_INFO(FUNC_ADPT_FMT":AP/GO station info is NULL\n", FUNC_ADPT_ARG(padapter));
			rtw_warn_on(1);
		}
	} else {
		/* GO/AP is 1nd order  GC/STA is 2nd order */
		switch (pmccadapriv->role) {
		case MCC_ROLE_STA:
		case MCC_ROLE_GC:
			pmccadapriv->order = 1;
			pmccadapriv->mcc_duration = mcc_duration;

			rtw_hal_mcc_assign_tx_threshold(padapter);
			/* assign used mac to avoid affecting RA */
			pmccadapriv->mgmt_queue_macid = MCC_ROLE_STA_GC_MGMT_QUEUE_MACID;

			psta = rtw_get_stainfo(pstapriv, cur_network->network.MacAddress);
			if (psta) {
				/* combine AP/GO macid and mgmt queue macid to bitmap */
				pmccadapriv->mcc_macid_bitmap = BIT(psta->cmn.mac_id) | BIT(pmccadapriv->mgmt_queue_macid);
				#ifdef CONFIG_MCC_PHYDM_OFFLOAD
				rtw_hal_mcc_cfg_phydm(padapter, MCC_CFG_PHYDM_ADD_CLIENT, &psta->cmn.mac_id);
				#endif
			} else {
				RTW_INFO(FUNC_ADPT_FMT":AP/GO station info is NULL\n", FUNC_ADPT_ARG(padapter));
				rtw_warn_on(1);
			}
			break;
		case MCC_ROLE_AP:
		case MCC_ROLE_GO:
			pmccadapriv->order = 0;
			/* total druation value equals interval */
			pmccadapriv->mcc_duration = mcc_interval - mcc_duration;
			pmccadapriv->p2p_go_noa_ie_len = 0; /* not NoA attribute at init time */

			rtw_hal_mcc_assign_tx_threshold(padapter);

			_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);

			phead = &pstapriv->asoc_list;
			plist = get_next(phead);
			pmccadapriv->mcc_macid_bitmap = 0;
	
			while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
				psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
				plist = get_next(plist);
				pmccadapriv->mcc_macid_bitmap |= BIT(psta->cmn.mac_id);
				#ifdef CONFIG_MCC_PHYDM_OFFLOAD
				rtw_hal_mcc_cfg_phydm(padapter, MCC_CFG_PHYDM_ADD_CLIENT, &psta->cmn.mac_id);
				#endif
			}

			_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

			psta = rtw_get_bcmc_stainfo(padapter);

			if (psta != NULL)
				pmccadapriv->mgmt_queue_macid = psta->cmn.mac_id;
			else {
				pmccadapriv->mgmt_queue_macid = MCC_ROLE_SOFTAP_GO_MGMT_QUEUE_MACID;
				RTW_INFO(FUNC_ADPT_FMT":bcmc station is NULL, use macid %d\n"
					, FUNC_ADPT_ARG(padapter), pmccadapriv->mgmt_queue_macid);
			}

			/* combine client macid and mgmt queue macid to bitmap */
			pmccadapriv->mcc_macid_bitmap |= BIT(pmccadapriv->mgmt_queue_macid);
			break;
		default:
			RTW_INFO("Unknown role\n");
			rtw_warn_on(1);
			break;
		}

	}

	/* setting Null data parameters */
	if (pmccadapriv->role == MCC_ROLE_STA) {
			pmccadapriv->null_early = 3;
			pmccadapriv->null_rty_num= 5;
	} else if (pmccadapriv->role == MCC_ROLE_GC) {
			pmccadapriv->null_early = 2;
			pmccadapriv->null_rty_num= 5;
	} else {
			pmccadapriv->null_early = 0;
			pmccadapriv->null_rty_num= 0;
	}

	RTW_INFO("********* "FUNC_ADPT_FMT" *********\n", FUNC_ADPT_ARG(padapter));
	RTW_INFO("order:%d\n", pmccadapriv->order);
	RTW_INFO("role:%d\n", pmccadapriv->role);
	RTW_INFO("mcc duration:%d\n", pmccadapriv->mcc_duration);
	RTW_INFO("null_early:%d\n", pmccadapriv->null_early);
	RTW_INFO("null_rty_num:%d\n", pmccadapriv->null_rty_num);
	RTW_INFO("mgmt queue macid:%d\n", pmccadapriv->mgmt_queue_macid);
	RTW_INFO("bitmap:0x%02x\n", pmccadapriv->mcc_macid_bitmap);
	RTW_INFO("target tx bytes:%d\n", pmccadapriv->mcc_target_tx_bytes_to_port);
	RTW_INFO("**********************************\n");

	pmccobjpriv->iface[pmccadapriv->order] = padapter;
	#ifdef CONFIG_MCC_PHYDM_OFFLOAD
	rtw_hal_mcc_cfg_phydm(padapter, MCC_CFG_PHYDM_RF_CH, NULL);
	#endif

}

static void rtw_hal_mcc_rqt_tsf(PADAPTER padapter, u64 *out_tsf)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *mccobjpriv = &(dvobj->mcc_objpriv);
	PADAPTER order0_iface = NULL;
	PADAPTER order1_iface = NULL;
	struct submit_ctx *tsf_req_sctx = NULL;
	enum _hw_port tsfx = MAX_HW_PORT;
	enum _hw_port tsfy = MAX_HW_PORT;
	u8 cmd[H2C_MCC_RQT_TSF_LEN] = {0};

	_enter_critical_mutex(&mccobjpriv->mcc_tsf_req_mutex, NULL);

	order0_iface = mccobjpriv->iface[0];
	order1_iface = mccobjpriv->iface[1];

	tsf_req_sctx = &mccobjpriv->mcc_tsf_req_sctx;
	rtw_sctx_init(tsf_req_sctx, MCC_EXPIRE_TIME);
	mccobjpriv->mcc_tsf_req_sctx_order = 0;
	tsfx = rtw_hal_get_port(order0_iface);
	tsfy = rtw_hal_get_port(order1_iface);

	SET_H2CCMD_MCC_RQT_TSFX(cmd, tsfx);
	SET_H2CCMD_MCC_RQT_TSFY(cmd, tsfy);

	rtw_hal_fill_h2c_cmd(padapter, H2C_MCC_RQT_TSF, H2C_MCC_RQT_TSF_LEN, cmd);

	if (!rtw_sctx_wait(tsf_req_sctx, __func__))
		RTW_INFO(FUNC_ADPT_FMT": wait for mcc tsf req C2H time out\n", FUNC_ADPT_ARG(padapter));

	if (tsf_req_sctx->status  == RTW_SCTX_DONE_SUCCESS && out_tsf != NULL) {
		out_tsf[0] = order0_iface->mcc_adapterpriv.tsf;
		out_tsf[1] = order1_iface->mcc_adapterpriv.tsf;
	}


	_exit_critical_mutex(&mccobjpriv->mcc_tsf_req_mutex, NULL);
}

static u8 rtw_hal_mcc_check_start_time_is_valid(PADAPTER padapter, u8 case_num,
	u32 tsfdiff, s8 *upper_bound_0, s8 *lower_bound_0, s8 *upper_bound_1, s8 *lower_bound_1)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *mccobjpriv = &(dvobj->mcc_objpriv);
	u8 duration_0 = 0, duration_1 = 0;
	s8 final_upper_bound = 0, final_lower_bound = 0;
	u8 intersection =  _FALSE;
	u8 min_start_time = 5;
	u8 max_start_time = 95;
	
	duration_0 = mccobjpriv->iface[0]->mcc_adapterpriv.mcc_duration;
	duration_1 = mccobjpriv->iface[1]->mcc_adapterpriv.mcc_duration;

	switch(case_num) {
	case 1:
		*upper_bound_0 = tsfdiff;
		*lower_bound_0 = tsfdiff - duration_1;
		*upper_bound_1 = 150 - duration_1;
		*lower_bound_1= 0;
		break;
	case 2:
		*upper_bound_0 = tsfdiff + 100;
		*lower_bound_0 = tsfdiff + 100 - duration_1;
		*upper_bound_1 = 150 - duration_1;
		*lower_bound_1= 0;
		break;
	case 3:
		*upper_bound_0 = tsfdiff + 50;
		*lower_bound_0 = tsfdiff + 50 - duration_1;
		*upper_bound_1 = 150 - duration_1;
		*lower_bound_1= 0;
		break;
	case 4:
		*upper_bound_0 = tsfdiff;
		*lower_bound_0 = tsfdiff - duration_1;
		*upper_bound_1 = 150 - duration_1;
		*lower_bound_1= 0;
		break;
	case 5:
		*upper_bound_0 = 200 - tsfdiff;
		*lower_bound_0 = 200 - tsfdiff - duration_1;
		*upper_bound_1 = 150 - duration_1;
		*lower_bound_1= 0;
		break;
	case 6:
		*upper_bound_0 = tsfdiff - 50;
		*lower_bound_0 = tsfdiff - 50 - duration_1;
		*upper_bound_1 = 150 - duration_1;
		*lower_bound_1= 0;
		break;
	default:
		RTW_ERR("[MCC] %s: error case number(%d\n)", __func__, case_num);
	}


	/* check Intersection or not */
	if ((*lower_bound_1 >= *upper_bound_0) ||
		(*lower_bound_0 >= *upper_bound_1))
		intersection = _FALSE;
	else
		intersection = _TRUE;

	if (intersection) {
		if (*upper_bound_0 > *upper_bound_1)
			final_upper_bound = *upper_bound_1;
		else
			final_upper_bound = *upper_bound_0;

		if (*lower_bound_0 > *lower_bound_1)
			final_lower_bound = *lower_bound_0;
		else
			final_lower_bound = *lower_bound_1;

		mccobjpriv->start_time = (final_lower_bound + final_upper_bound) / 2;

		/* check start time less than 5ms, request by Pablo@SD1 */
		if (mccobjpriv->start_time <= min_start_time) {
			mccobjpriv->start_time = 6;
			if (mccobjpriv->start_time < final_lower_bound && mccobjpriv->start_time > final_upper_bound) {
				intersection = _FALSE;
				goto exit;
			}
		}

		/* check start time less than 95ms */
		if (mccobjpriv->start_time >= max_start_time) {
			mccobjpriv->start_time = 90;
			if (mccobjpriv->start_time < final_lower_bound && mccobjpriv->start_time > final_upper_bound) {
				intersection = _FALSE;
				goto exit;
			}
		}
	}

exit:
	return intersection;
}

static void rtw_hal_mcc_decide_duration(PADAPTER padapter)
{
	struct registry_priv *registry_par = &padapter->registrypriv;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *mccobjpriv = &(dvobj->mcc_objpriv);
	struct mcc_adapter_priv *mccadapriv = NULL, *mccadapriv_order0 = NULL, *mccadapriv_order1 = NULL;
	_adapter *iface = NULL, *iface_order0 = NULL,  *iface_order1 = NULL;
	u8 duration = 0, i = 0, duration_time;
	u8 mcc_interval = 150;

	iface_order0 = mccobjpriv->iface[0];
	iface_order1 = mccobjpriv->iface[1];
	mccadapriv_order0 = &iface_order0->mcc_adapterpriv;
	mccadapriv_order1 = &iface_order1->mcc_adapterpriv;
	
	if (mccobjpriv->duration == 0) {
		/* default */
		duration = 30;/*(%)*/
		RTW_INFO("%s: mccobjpriv->duration=0, use default value(%d)\n",
			__FUNCTION__, duration);
	} else {
		duration = mccobjpriv->duration;/*(%)*/
		RTW_INFO("%s: mccobjpriv->duration=%d\n",
			__FUNCTION__, duration);
	}

	mccobjpriv->interval = mcc_interval;
	mccobjpriv->mcc_stop_threshold = 2000 * 4 / 300 - 6;
	/* convert % to ms, for primary adapter */
	duration_time = mccobjpriv->interval * duration / 100;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];

		if (!iface)
			continue;

		mccadapriv = &iface->mcc_adapterpriv;
		if (mccadapriv->role == MCC_ROLE_MAX)
			continue;

		if (is_primary_adapter(iface))
			mccadapriv->mcc_duration = duration_time;
		else
			mccadapriv->mcc_duration = mccobjpriv->interval - duration_time;
	}

	RTW_INFO("[MCC]"  FUNC_ADPT_FMT " order 0 duration=%d\n", FUNC_ADPT_ARG(iface_order0), mccadapriv_order0->mcc_duration);
	RTW_INFO("[MCC]"  FUNC_ADPT_FMT " order 1 duration=%d\n", FUNC_ADPT_ARG(iface_order1), mccadapriv_order1->mcc_duration);
}

static u8 rtw_hal_mcc_update_timing_parameters(PADAPTER padapter, u8 force_update)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	u8 need_update = _FALSE;
	u8 starting_ap_num = DEV_AP_STARTING_NUM(dvobj);
	u8 ap_num = DEV_AP_NUM(dvobj);


	/* for STA+STA, modify policy table */
	if (starting_ap_num == 0 && ap_num == 0) {
		struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
		struct mcc_obj_priv *pmccobjpriv = &(dvobj->mcc_objpriv);
		struct mcc_adapter_priv *pmccadapriv = NULL;
		_adapter *iface = NULL;
		u64 tsf[MAX_MCC_NUM] = {0};
		u64 tsf0 = 0, tsf1 = 0;
		u32 beaconperiod_0 = 0, beaconperiod_1 = 0, tsfdiff = 0;
		s8 upper_bound_0 = 0, lower_bound_0 = 0;
		s8 upper_bound_1 = 0, lower_bound_1 = 0;
		u8 valid = _FALSE;
		u8 case_num = 1;
		u8 i = 0;
		
		/* query TSF */
		rtw_hal_mcc_rqt_tsf(padapter, tsf);

		/* selecet policy table according TSF diff */
		tsf0 = tsf[0];
		beaconperiod_0 = pmccobjpriv->iface[0]->mlmepriv.cur_network.network.Configuration.BeaconPeriod;
		tsf0 = rtw_modular64(tsf0, (beaconperiod_0 * TU));

		tsf1 = tsf[1];
		beaconperiod_1 = pmccobjpriv->iface[1]->mlmepriv.cur_network.network.Configuration.BeaconPeriod;
		tsf1 = rtw_modular64(tsf1, (beaconperiod_1 * TU));

		if (tsf0 > tsf1)
			tsfdiff = tsf0- tsf1;
		else
			tsfdiff = (tsf0 +  beaconperiod_0 * TU) - tsf1;

		/* convert to ms */
		tsfdiff = (tsfdiff / TU);

		/* force update*/
		if (force_update) {
			RTW_INFO("orig TSF0:%lld, orig TSF1:%lld\n",
				pmccobjpriv->iface[0]->mcc_adapterpriv.tsf, pmccobjpriv->iface[1]->mcc_adapterpriv.tsf);
			RTW_INFO("tsf0:%lld, tsf1:%lld\n", tsf0, tsf1);
			RTW_INFO("%s: force=%d, last_tsfdiff=%d, tsfdiff=%d, THRESHOLD=%d\n",
				__func__, force_update, pmccobjpriv->last_tsfdiff, tsfdiff, MCC_UPDATE_PARAMETER_THRESHOLD);
			pmccobjpriv->last_tsfdiff = tsfdiff;
			need_update = _TRUE;
		} else {
			if (pmccobjpriv->last_tsfdiff > tsfdiff) {
				/* last tsfdiff - current tsfdiff > THRESHOLD, update parameters */
				if (pmccobjpriv->last_tsfdiff > (tsfdiff + MCC_UPDATE_PARAMETER_THRESHOLD)) {
					RTW_INFO("orig TSF0:%lld, orig TSF1:%lld\n",
						pmccobjpriv->iface[0]->mcc_adapterpriv.tsf, pmccobjpriv->iface[1]->mcc_adapterpriv.tsf);
					RTW_INFO("tsf0:%lld, tsf1:%lld\n", tsf0, tsf1);
					RTW_INFO("%s: force=%d, last_tsfdiff=%d, tsfdiff=%d, THRESHOLD=%d\n",
						__func__, force_update, pmccobjpriv->last_tsfdiff, tsfdiff, MCC_UPDATE_PARAMETER_THRESHOLD);

					pmccobjpriv->last_tsfdiff = tsfdiff;
					need_update = _TRUE;
				} else {
					need_update = _FALSE;
				}
			} else if (tsfdiff > pmccobjpriv->last_tsfdiff){
				/* current tsfdiff - last tsfdiff > THRESHOLD, update parameters */
				if (tsfdiff > (pmccobjpriv->last_tsfdiff + MCC_UPDATE_PARAMETER_THRESHOLD)) {
					RTW_INFO("orig TSF0:%lld, orig TSF1:%lld\n",
						pmccobjpriv->iface[0]->mcc_adapterpriv.tsf, pmccobjpriv->iface[1]->mcc_adapterpriv.tsf);
					RTW_INFO("tsf0:%lld, tsf1:%lld\n", tsf0, tsf1);
					RTW_INFO("%s: force=%d, last_tsfdiff=%d, tsfdiff=%d, THRESHOLD=%d\n",
						__func__, force_update, pmccobjpriv->last_tsfdiff, tsfdiff, MCC_UPDATE_PARAMETER_THRESHOLD);

					pmccobjpriv->last_tsfdiff = tsfdiff;
					need_update = _TRUE;
				} else {
					need_update = _FALSE;
				}
			} else {
				need_update = _FALSE;
			}
		}

		if (need_update == _FALSE)
			goto exit;

		rtw_hal_mcc_decide_duration(padapter);

		if (tsfdiff <= 50) {
	
			/* RX TBTT 0 */
			case_num = 1;
			valid = rtw_hal_mcc_check_start_time_is_valid(padapter, case_num, tsfdiff,
				&upper_bound_0, &lower_bound_0, &upper_bound_1, &lower_bound_1);

			if (valid)
				goto valid_result;
	
			/* RX TBTT 1 */
			case_num = 2;
			valid = rtw_hal_mcc_check_start_time_is_valid(padapter, case_num, tsfdiff,
				&upper_bound_0, &lower_bound_0, &upper_bound_1, &lower_bound_1);

			if (valid)
				goto valid_result;
			
			/* RX TBTT 2 */
			case_num = 3;
			valid = rtw_hal_mcc_check_start_time_is_valid(padapter, case_num, tsfdiff,
				&upper_bound_0, &lower_bound_0, &upper_bound_1, &lower_bound_1);

			if (valid)
				goto valid_result;

			if (valid == _FALSE) {
				RTW_INFO("[MCC] do not find fit start time\n");
				RTW_INFO("[MCC] tsfdiff:%d, duration:%d(%c), interval:%d\n",
					tsfdiff, pmccobjpriv->duration, 37, pmccobjpriv->interval);

			}

		} else {

			/* RX TBTT 0 */
			case_num = 4;
			valid = rtw_hal_mcc_check_start_time_is_valid(padapter, case_num, tsfdiff,
				&upper_bound_0, &lower_bound_0, &upper_bound_1, &lower_bound_1);

			if (valid)
				goto valid_result;
			
			
			/* RX TBTT 1 */
			case_num = 5;
			valid = rtw_hal_mcc_check_start_time_is_valid(padapter, case_num, tsfdiff,
				&upper_bound_0, &lower_bound_0, &upper_bound_1, &lower_bound_1);

			if (valid)
				goto valid_result;

			
			/* RX TBTT 2 */
			case_num = 6;
			valid = rtw_hal_mcc_check_start_time_is_valid(padapter, case_num, tsfdiff,
				&upper_bound_0, &lower_bound_0, &upper_bound_1, &lower_bound_1);

			if (valid)
				goto valid_result;

			if (valid == _FALSE) {
				RTW_INFO("[MCC] do not find fit start time\n");
				RTW_INFO("[MCC] tsfdiff:%d, duration:%d(%c), interval:%d\n",
					tsfdiff, pmccobjpriv->duration, 37, pmccobjpriv->interval);
			}
		}

		

	valid_result:
		RTW_INFO("********************\n");
		RTW_INFO("%s: case_num:%d, start time:%d\n",
				__func__, case_num, pmccobjpriv->start_time);
		RTW_INFO("%s: upper_bound_0:%d, lower_bound_0:%d\n",
				__func__, upper_bound_0, lower_bound_0);
		RTW_INFO("%s: upper_bound_1:%d, lower_bound_1:%d\n",
				__func__, upper_bound_1, lower_bound_1);
		
		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			if (iface == NULL)
				continue;

			pmccadapriv = &iface->mcc_adapterpriv;
			pmccadapriv = &iface->mcc_adapterpriv;
			if (pmccadapriv->role == MCC_ROLE_MAX)
				continue;
#if 0
			if (pmccadapriv->order == 0) {
				pmccadapriv->mcc_duration = mcc_duration;
			} else if (pmccadapriv->order == 1) {
				pmccadapriv->mcc_duration = mcc_interval - mcc_duration;
			} else {
				RTW_INFO("[MCC] not support >= 3 interface\n");
				rtw_warn_on(1);
			}
#endif
			RTW_INFO("********************\n");
			RTW_INFO(FUNC_ADPT_FMT": order:%d, role:%d\n",
				FUNC_ADPT_ARG(iface), pmccadapriv->order, pmccadapriv->role);
			RTW_INFO(FUNC_ADPT_FMT": mcc duration:%d, target tx bytes:%d\n",
				FUNC_ADPT_ARG(iface), pmccadapriv->mcc_duration, pmccadapriv->mcc_target_tx_bytes_to_port);
			RTW_INFO(FUNC_ADPT_FMT": mgmt queue macid:%d, bitmap:0x%02x\n",
				FUNC_ADPT_ARG(iface), pmccadapriv->mgmt_queue_macid, pmccadapriv->mcc_macid_bitmap);
			RTW_INFO("********************\n");
		}
		
	}
exit:
	return need_update;
}

static u8 rtw_hal_decide_mcc_role(PADAPTER padapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	_adapter *iface = NULL;
	struct mcc_adapter_priv *pmccadapriv = NULL;
	struct wifidirect_info *pwdinfo = NULL;
	struct mlme_priv *pmlmepriv = NULL;
	u8 ret = _SUCCESS, i = 0;
	u8 order = 1;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (iface == NULL)
			continue;

		pmccadapriv = &iface->mcc_adapterpriv;
		pwdinfo = &iface->wdinfo;

		if (MLME_IS_GO(iface))
			pmccadapriv->role = MCC_ROLE_GO;
		else if (MLME_IS_AP(iface))
			pmccadapriv->role = MCC_ROLE_AP;
		else if (MLME_IS_GC(iface))
			pmccadapriv->role = MCC_ROLE_GC;
		else if (MLME_IS_STA(iface)) {
			if (MLME_IS_LINKING(iface) || MLME_IS_ASOC(iface))
				pmccadapriv->role = MCC_ROLE_STA;
			else {
				/* bypass non-linked/non-linking interface */
				RTW_INFO(FUNC_ADPT_FMT" mlme state:0x%2x\n",
					FUNC_ADPT_ARG(iface), MLME_STATE(iface));
				continue;
			}
		} else {
			/* bypass non-linked/non-linking interface */
			RTW_INFO(FUNC_ADPT_FMT" P2P Role:%d, mlme state:0x%2x\n",
				FUNC_ADPT_ARG(iface), pwdinfo->role, MLME_STATE(iface));
			continue;
		}

		if (padapter == iface) {
			/* current adapter is order 0 */
			rtw_hal_config_mcc_role_setting(iface, 0);
		} else {
			rtw_hal_config_mcc_role_setting(iface, order);
			order ++;
		}
	}

	rtw_hal_mcc_update_timing_parameters(padapter, _TRUE);
exit:
	return ret;
}

static void rtw_hal_construct_CTS(PADAPTER padapter, u8 *pframe, u32 *pLength)
{
	u8 broadcast_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	/* frame type, length = 1*/
	set_frame_sub_type(pframe, WIFI_RTS);

	/* frame control flag, length = 1 */
	*(pframe + 1) = 0;

	/* frame duration, length = 2 */
	*(pframe + 2) = 0x00;
	*(pframe + 3) = 0x78;

	/* frame recvaddr, length = 6 */
	_rtw_memcpy((pframe + 4), broadcast_addr, ETH_ALEN);
	_rtw_memcpy((pframe + 4 + ETH_ALEN), adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy((pframe + 4 + ETH_ALEN*2), adapter_mac_addr(padapter), ETH_ALEN);
	*pLength = 22;
}

/* avoid wrong information for power limit */
void rtw_hal_mcc_upadate_chnl_bw(_adapter *padapter, u8 ch, u8 ch_offset, u8 bw, u8 print)
{

	u8 center_ch, chnl_offset80 = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	PHAL_DATA_TYPE	hal = GET_HAL_DATA(padapter);
	u8 cch_160, cch_80, cch_40, cch_20;

	center_ch = rtw_get_center_ch(ch, bw, ch_offset);

	if (bw == CHANNEL_WIDTH_80) {
		if (center_ch > ch)
			chnl_offset80 = HAL_PRIME_CHNL_OFFSET_LOWER;
		else if (center_ch < ch)
			chnl_offset80 = HAL_PRIME_CHNL_OFFSET_UPPER;
		else
			chnl_offset80 = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	}

	/* set Channel */
	/* saved channel/bw info */
	rtw_set_oper_ch(padapter, ch);
	rtw_set_oper_bw(padapter, bw);
	rtw_set_oper_choffset(padapter, ch_offset);

	cch_80 = bw == CHANNEL_WIDTH_80 ? center_ch : 0;
	cch_40 = bw == CHANNEL_WIDTH_40 ? center_ch : 0;
	cch_20 = bw == CHANNEL_WIDTH_20 ? center_ch : 0;

	if (cch_80 != 0)
		cch_40 = rtw_get_scch_by_cch_offset(cch_80, CHANNEL_WIDTH_80, chnl_offset80);
	if (cch_40 != 0)
		cch_20 = rtw_get_scch_by_cch_offset(cch_40, CHANNEL_WIDTH_40, ch_offset);


	hal->cch_80 = cch_80;
	hal->cch_40 = cch_40;
	hal->cch_20 = cch_20;
	hal->current_channel = center_ch;
	hal->CurrentCenterFrequencyIndex1 = center_ch;
	hal->current_channel_bw = bw;
	hal->nCur40MhzPrimeSC = ch_offset;
	hal->nCur80MhzPrimeSC = chnl_offset80;
	hal->current_band_type = ch > 14 ? BAND_ON_5G:BAND_ON_2_4G;

	if (print) {
		RTW_INFO(FUNC_ADPT_FMT" cch:%u, %s, offset40:%u, offset80:%u (%u, %u, %u), band:%s\n"
			, FUNC_ADPT_ARG(padapter), center_ch, ch_width_str(bw)
			, ch_offset, chnl_offset80
			, hal->cch_80, hal->cch_40, hal->cch_20
			, band_str(hal->current_band_type));
	}
}

u8 rtw_hal_dl_mcc_fw_rsvd_page(_adapter *adapter, u8 *pframe, u16 *index,
	u8 tx_desc, u32 page_size, u8 *total_page_num, RSVDPAGE_LOC *rsvd_page_loc, u8 *page_num)
{
	u32 len = 0;
	_adapter *iface = NULL;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct mcc_obj_priv *pmccobjpriv = &(dvobj->mcc_objpriv);
	struct mlme_ext_info *pmlmeinfo = NULL;
	struct mlme_ext_priv *pmlmeext = NULL;
	struct hal_com_data *hal = GET_HAL_DATA(adapter);
	struct mcc_adapter_priv *mccadapriv = NULL;
	u8 ret = _SUCCESS, i = 0, j  =0, order = 0, CurtPktPageNum = 0;
	u8 *start = NULL;
	u8 path = RF_PATH_A;

	if (page_num) {
#ifdef CONFIG_MCC_MODE_V2
		if (!hal->RegIQKFWOffload)
			RTW_WARN("[MCC] must enable FW IQK for New IC\n");
#endif /* CONFIG_MCC_MODE_V2 */
		*total_page_num += (2 * MAX_MCC_NUM+ 1);
		RTW_INFO("[MCC] allocate mcc rsvd page num = %d\n", *total_page_num);
		goto exit;
	}

	/* check proccess mcc start setting */
	if (!rtw_hal_check_mcc_status(adapter, MCC_STATUS_PROCESS_MCC_START_SETTING)) {
		ret = _FAIL;
		goto exit;
	}

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (iface == NULL)
			continue;

		mccadapriv = &iface->mcc_adapterpriv;
		if (mccadapriv->role == MCC_ROLE_MAX)
			continue;

		order = mccadapriv->order;
		pmccobjpriv->mcc_loc_rsvd_paga[order] = *total_page_num;

		switch (mccadapriv->role) {
		case MCC_ROLE_STA:
		case MCC_ROLE_GC:
			/* Build NULL DATA */
			RTW_INFO("LocNull(order:%d): %d\n"
				, order, pmccobjpriv->mcc_loc_rsvd_paga[order]);
			len = 0;

			rtw_hal_construct_NullFunctionData(iface
				, &pframe[*index], &len, _FALSE, 0, 0, _FALSE);
			rtw_hal_fill_fake_txdesc(iface, &pframe[*index-tx_desc],
				len, _FALSE, _FALSE, _FALSE);

			CurtPktPageNum = (u8)PageNum(tx_desc + len, page_size);
			*total_page_num += CurtPktPageNum;
			*index += (CurtPktPageNum * page_size);
			RSVD_PAGE_CFG("LocNull", CurtPktPageNum, *total_page_num, *index);
			break;
		case MCC_ROLE_AP:
			/* Bulid CTS */
			RTW_INFO("LocCTS(order:%d): %d\n"
				, order, pmccobjpriv->mcc_loc_rsvd_paga[order]);

			len = 0;
			rtw_hal_construct_CTS(iface, &pframe[*index], &len);
			rtw_hal_fill_fake_txdesc(iface, &pframe[*index-tx_desc],
				len, _FALSE, _FALSE, _FALSE);

			CurtPktPageNum = (u8)PageNum(tx_desc + len, page_size);
			*total_page_num += CurtPktPageNum;
			*index += (CurtPktPageNum * page_size);
			RSVD_PAGE_CFG("LocCTS", CurtPktPageNum, *total_page_num, *index);
			break;
		case MCC_ROLE_GO:
		/* To DO */
			break;
		default:
			RTW_INFO(FUNC_ADPT_FMT": unknown role = %d\n"
				, FUNC_ADPT_ARG(iface), mccadapriv->role);
			break;
		}
	}

	for (i = 0; i < MAX_MCC_NUM; i++) {
		u8 center_ch = 0, ch = 0, bw = 0, bw_offset = 0;
		u8 power_index = 0;
		u8 rate_array_sz = 0;
		u8 *rates = NULL;
		u8 rate = 0;
		u8 shift = 0;
		u32 power_index_4bytes = 0;
		u8 total_rate = 0;
		u8 *total_rate_offset = NULL;

		iface = pmccobjpriv->iface[i];
		pmlmeext = &iface->mlmeextpriv;
		ch = pmlmeext->cur_channel;
		bw = pmlmeext->cur_bwmode;
		bw_offset = pmlmeext->cur_ch_offset;
		center_ch = rtw_get_center_ch(ch, bw, bw_offset);
		rtw_hal_mcc_upadate_chnl_bw(iface, ch, bw_offset, bw, _TRUE);

		start = &pframe[*index - tx_desc];
		_rtw_memset(start, 0, page_size);
		pmccobjpriv->mcc_pwr_idx_rsvd_page[i] = *total_page_num;
		RTW_INFO(ADPT_FMT" order:%d, pwr_idx_rsvd_page location[%d]: %d\n",
			ADPT_ARG(iface), mccadapriv->order,
			i, pmccobjpriv->mcc_pwr_idx_rsvd_page[i]);

		total_rate_offset = start;
			
		for (path = RF_PATH_A; path < hal->NumTotalRFPath; ++path) {
			total_rate = 0;
			/* PATH A for 0~63 byte, PATH B for 64~127 byte*/
			if (path == RF_PATH_A)
				start = total_rate_offset + 1;
			else if (path == RF_PATH_B)
				start = total_rate_offset + 64;
			else {
				RTW_INFO("[MCC] %s: unknow RF PATH(%d)\n", __func__, path);
				break;
			}

			/* CCK */
			if (ch <= 14) {
				rate_array_sz = rates_by_sections[CCK].rate_num;
				rates = rates_by_sections[CCK].rates;
				for (j = 0; j < rate_array_sz; ++j) {
					power_index = rtw_hal_get_tx_power_index(iface, path, rates[j], bw, center_ch, NULL);
					rate = PHY_GetRateIndexOfTxPowerByRate(rates[j]);

					shift = rate % 4;
					if (shift == 0) {
						*start = rate;
						start++;
						total_rate++;

						#ifdef DBG_PWR_IDX_RSVD_PAGE
						RTW_INFO("TXPWR("ADPT_FMT"): [%c][%s]ch:%u, %s, pwr_idx:%u\n",
							ADPT_ARG(iface), rf_path_char(path), ch_width_str(bw),
							center_ch, MGN_RATE_STR(rates[j]), power_index);
						#endif
					}

					*start = power_index;
					start++;

					#ifdef DBG_PWR_IDX_RSVD_PAGE
					RTW_INFO("TXPWR("ADPT_FMT"): [%c][%s]ch:%u, %s, pwr_idx:%u\n",
						ADPT_ARG(iface), rf_path_char(path), ch_width_str(bw),
						center_ch, MGN_RATE_STR(rates[j]), power_index);

					
					shift = rate % 4;
					power_index_4bytes |= ((power_index & 0xff) << (shift * 8));
					if (shift == 3) {
						rate = rate - 3;
						RTW_INFO("(index:0x%02x, rfpath:%d, rate:0x%02x)\n", index, path, rate);
						power_index_4bytes = 0;
						total_rate++;
					}
					#endif
						
				}
			}

			/* OFDM */
			rate_array_sz = rates_by_sections[OFDM].rate_num;
			rates = rates_by_sections[OFDM].rates;
			for (j = 0; j < rate_array_sz; ++j) {
				power_index = rtw_hal_get_tx_power_index(iface, path, rates[j], bw, center_ch, NULL);
				rate = PHY_GetRateIndexOfTxPowerByRate(rates[j]);

				shift = rate % 4;
				if (shift == 0) {
					*start = rate;
					start++;
					total_rate++;

					#ifdef DBG_PWR_IDX_RSVD_PAGE
					RTW_INFO("TXPWR("ADPT_FMT"): [%c][%s]ch:%u, %s, pwr_idx:%u\n",
						ADPT_ARG(iface), rf_path_char(path), ch_width_str(bw),
						center_ch, MGN_RATE_STR(rates[j]), power_index);
					#endif

				}

				*start = power_index;
				start++;

				#ifdef DBG_PWR_IDX_RSVD_PAGE
				RTW_INFO("TXPWR("ADPT_FMT"): [%c][%s]ch:%u, %s, pwr_idx:%u\n",
					ADPT_ARG(iface), rf_path_char(path), ch_width_str(bw),
					center_ch, MGN_RATE_STR(rates[j]), power_index);

				shift = rate % 4;
				power_index_4bytes |= ((power_index & 0xff) << (shift * 8));
				if (shift == 3) {
					rate = rate - 3;
					RTW_INFO("(index:0x%02x, rfpath:%d, rate:0x%02x)\n", index, path, rate);
					power_index_4bytes = 0;
					total_rate++;
				}
				#endif
			}

			/* HT_MCS0_MCS7 */
			rate_array_sz = rates_by_sections[HT_MCS0_MCS7].rate_num;
			rates = rates_by_sections[HT_MCS0_MCS7].rates;
			for (j = 0; j < rate_array_sz; ++j) {
				power_index = rtw_hal_get_tx_power_index(iface, path, rates[j], bw, center_ch, NULL);
				rate = PHY_GetRateIndexOfTxPowerByRate(rates[j]);

				shift = rate % 4;
				if (shift == 0) {
					*start = rate;
					start++;
					total_rate++;

					#ifdef DBG_PWR_IDX_RSVD_PAGE
					RTW_INFO("TXPWR("ADPT_FMT"): [%c][%s]ch:%u, %s, pwr_idx:%u\n",
						ADPT_ARG(iface), rf_path_char(path), ch_width_str(bw),
						center_ch, MGN_RATE_STR(rates[j]), power_index);
					#endif

				}

				*start = power_index;
				start++;

				#ifdef DBG_PWR_IDX_RSVD_PAGE
				RTW_INFO("TXPWR("ADPT_FMT"): [%c][%s]ch:%u, %s, pwr_idx:%u\n",
					ADPT_ARG(iface), rf_path_char(path), ch_width_str(bw),
					center_ch, MGN_RATE_STR(rates[j]), power_index);

				shift = rate % 4;
				power_index_4bytes |= ((power_index & 0xff) << (shift * 8));
				if (shift == 3) {
					rate = rate - 3;
					RTW_INFO("(index:0x%02x, rfpath:%d, rate:0x%02x)\n", index, path, rate);
					power_index_4bytes = 0;
					total_rate++;
				}
				#endif
			}

			/* HT_MCS8_MCS15 */
			rate_array_sz = rates_by_sections[HT_MCS8_MCS15].rate_num;
			rates = rates_by_sections[HT_MCS8_MCS15].rates;
			for (j = 0; j < rate_array_sz; ++j) {
				power_index = rtw_hal_get_tx_power_index(iface, path, rates[j], bw, center_ch, NULL);
				rate = PHY_GetRateIndexOfTxPowerByRate(rates[j]);

				shift = rate % 4;
				if (shift == 0) {
					*start = rate;
					start++;
					total_rate++;

					#ifdef DBG_PWR_IDX_RSVD_PAGE
					RTW_INFO("TXPWR("ADPT_FMT"): [%c][%s]ch:%u, %s, pwr_idx:%u\n",
						ADPT_ARG(iface), rf_path_char(path), ch_width_str(bw),
						center_ch, MGN_RATE_STR(rates[j]), power_index);
					#endif
				}

				*start = power_index;
				start++;

				#ifdef DBG_PWR_IDX_RSVD_PAGE
				RTW_INFO("TXPWR("ADPT_FMT"): [%c][%s]ch:%u, %s, pwr_idx:%u\n",
					ADPT_ARG(iface), rf_path_char(path), ch_width_str(bw),
					center_ch, MGN_RATE_STR(rates[j]), power_index);
				
				shift = rate % 4;
				power_index_4bytes |= ((power_index & 0xff) << (shift * 8));
				if (shift == 3) {
					rate = rate - 3;
					RTW_INFO("(index:0x%02x, rfpath:%d, rate:0x%02x)\n", index, path, rate);
					power_index_4bytes = 0;
					total_rate++;
				}
				#endif
			}

			/* VHT_1SSMCS0_1SSMCS9 */
			rate_array_sz = rates_by_sections[VHT_1SSMCS0_1SSMCS9].rate_num;
			rates = rates_by_sections[VHT_1SSMCS0_1SSMCS9].rates;
			for (j = 0; j < rate_array_sz; ++j) {
				power_index = rtw_hal_get_tx_power_index(iface, path, rates[j], bw, center_ch, NULL);
				rate = PHY_GetRateIndexOfTxPowerByRate(rates[j]);

				shift = rate % 4;
				if (shift == 0) {
					*start = rate;
					start++;
					total_rate++;
					#ifdef DBG_PWR_IDX_RSVD_PAGE
					RTW_INFO("TXPWR("ADPT_FMT"): [%c][%s]ch:%u, %s, pwr_idx:0x%02x\n",
						ADPT_ARG(iface), rf_path_char(path), ch_width_str(bw),
						center_ch, MGN_RATE_STR(rates[j]), power_index);
					#endif
				}
				*start = power_index;
				start++;
				#ifdef DBG_PWR_IDX_RSVD_PAGE
				RTW_INFO("TXPWR("ADPT_FMT"): [%c][%s]ch:%u, %s, pwr_idx:%u\n",
					ADPT_ARG(iface), rf_path_char(path), ch_width_str(bw),
					center_ch, MGN_RATE_STR(rates[j]), power_index);

				shift = rate % 4;
				power_index_4bytes |= ((power_index & 0xff) << (shift * 8));
				if (shift == 3) {
					rate = rate - 3;
					RTW_INFO("(index:0x%02x, rfpath:%d, rate:0x%02x)\n", index, path, rate);
					power_index_4bytes = 0;
					total_rate++;
				}
				#endif
			}

			/* VHT_2SSMCS0_2SSMCS9 */
			rate_array_sz = rates_by_sections[VHT_2SSMCS0_2SSMCS9].rate_num;
			rates = rates_by_sections[VHT_2SSMCS0_2SSMCS9].rates;
			for (j = 0; j < rate_array_sz; ++j) {
				power_index = rtw_hal_get_tx_power_index(iface, path, rates[j], bw, center_ch, NULL);
				rate = PHY_GetRateIndexOfTxPowerByRate(rates[j]);

				shift = rate % 4;
				if (shift == 0) {
					*start = rate;
					start++;
					total_rate++;
					#ifdef DBG_PWR_IDX_RSVD_PAGE
					RTW_INFO("TXPWR("ADPT_FMT"): [%c][%s]ch:%u, %s, pwr_idx:%u\n",
						ADPT_ARG(iface), rf_path_char(path), ch_width_str(bw),
						center_ch, MGN_RATE_STR(rates[j]), power_index);
					#endif
				}
				*start = power_index;
				start++;
				#ifdef DBG_PWR_IDX_RSVD_PAGE
				RTW_INFO("TXPWR("ADPT_FMT"): [%c][%s]ch:%u, %s, pwr_idx:%u\n",
					ADPT_ARG(iface), rf_path_char(path), ch_width_str(bw),
					center_ch, MGN_RATE_STR(rates[j]), power_index);

				shift = rate % 4;
				power_index_4bytes |= ((power_index & 0xff) << (shift * 8));
				if (shift == 3) {
					rate = rate - 3;
					RTW_INFO("(index:0x%02x, rfpath:%d, rate:0x%02x)\n", index, path, rate);
					power_index_4bytes = 0;
						total_rate++;
				}
				#endif
			}
				
		}
		/*  total rate store in offset 0 */
		*total_rate_offset = total_rate;

#ifdef DBG_PWR_IDX_RSVD_PAGE
			RTW_INFO("total_rate=%d\n", total_rate);
			RTW_INFO(" ======================="ADPT_FMT"===========================\n", ADPT_ARG(iface));
			RTW_INFO_DUMP("\n", total_rate_offset, 128);
			RTW_INFO(" ==================================================\n");
#endif

			CurtPktPageNum = 1;
			*total_page_num += CurtPktPageNum;
			*index += (CurtPktPageNum * page_size);
			RSVD_PAGE_CFG("mcc_pwr_idx_rsvd_page", CurtPktPageNum, *total_page_num, *index);
		}

exit:
	return ret;
}

/*
* 1. Download MCC rsvd page
* 2. Re-Download beacon after download rsvd page
*/
static void rtw_hal_set_fw_mcc_rsvd_page(PADAPTER padapter)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	struct mcc_adapter_priv *pmccadapriv = &padapter->mcc_adapterpriv;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	PADAPTER port0_iface = dvobj_get_port0_adapter(dvobj);
	PADAPTER iface = NULL;
	struct mcc_obj_priv *pmccobjpriv = &(dvobj->mcc_objpriv);
	u8 mstatus = RT_MEDIA_CONNECT, i = 0;

	RTW_INFO(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));

	rtw_hal_set_hwreg(port0_iface, HW_VAR_H2C_FW_JOINBSSRPT, (u8 *)(&mstatus));

	/* Re-Download beacon */
	for (i = 0; i < MAX_MCC_NUM; i++) {
		iface = pmccobjpriv->iface[i];
		if (iface == NULL)
			continue;

		pmccadapriv = &iface->mcc_adapterpriv;

		if (pmccadapriv->role == MCC_ROLE_AP
			|| pmccadapriv->role == MCC_ROLE_GO) {
			tx_beacon_hdl(iface, NULL);
		}
	}
}

static void rtw_hal_set_mcc_rsvdpage_cmd(_adapter *padapter)
{
	u8 cmd[H2C_MCC_LOCATION_LEN] = {0}, i = 0, order = 0;
	_adapter *iface = NULL;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(padapter);
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *pmccobjpriv = &(dvobj->mcc_objpriv);

	SET_H2CCMD_MCC_PWRIDX_OFFLOAD_EN(cmd, _TRUE);
	SET_H2CCMD_MCC_PWRIDX_OFFLOAD_RFNUM(cmd, hal->NumTotalRFPath);
	for (order = 0; order < MAX_MCC_NUM; order++) {
		iface = pmccobjpriv->iface[i];

		SET_H2CCMD_MCC_RSVDPAGE_LOC((cmd + order), pmccobjpriv->mcc_loc_rsvd_paga[order]);
		SET_H2CCMD_MCC_PWRIDX_RSVDPAGE_LOC ((cmd + order), pmccobjpriv->mcc_pwr_idx_rsvd_page[order]);
	}

#ifdef CONFIG_MCC_MODE_DEBUG
	RTW_INFO("=========================\n");
	RTW_INFO("MCC RSVD PAGE LOC:\n");
	for (i = 0; i < H2C_MCC_LOCATION_LEN; i++)
		pr_dbg("0x%x ", cmd[i]);
	pr_dbg("\n");
	RTW_INFO("=========================\n");
#endif /* CONFIG_MCC_MODE_DEBUG */

	rtw_hal_fill_h2c_cmd(padapter, H2C_MCC_LOCATION, H2C_MCC_LOCATION_LEN, cmd);
}

static void rtw_hal_set_mcc_time_setting_cmd(PADAPTER padapter)
{
	struct mcc_adapter_priv *pmccadapriv = &padapter->mcc_adapterpriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *mccobjpriv = &(dvobj->mcc_objpriv);
	u8 cmd[H2C_MCC_TIME_SETTING_LEN] = {0};
	u8 fw_eable = 1;
	u8 swchannel_early_time = MCC_SWCH_FW_EARLY_TIME;
	u8 starting_ap_num = DEV_AP_STARTING_NUM(dvobj);
	u8 ap_num = DEV_AP_NUM(dvobj);	

	if (starting_ap_num == 0 && ap_num == 0)
		/* For STA+GC/STA+STA, TSF of GC/STA does not need to sync from TSF of other STA/GC */
		fw_eable = 0;
	else
		/* Only for STA+GO/STA+AP, TSF of AP/GO need to sync from TSF of STA */
		fw_eable = 1;

	if (fw_eable == 1) {
		PADAPTER order0_iface = NULL;
		PADAPTER order1_iface = NULL;
		u8 policy_idx = mccobjpriv->policy_index;
		u8 tsf_sync_offset = mcc_switch_channel_policy_table[policy_idx][MCC_TSF_SYNC_OFFSET_IDX];
		u8 start_time_offset = mcc_switch_channel_policy_table[policy_idx][MCC_START_TIME_OFFSET_IDX];
		u8 interval = mcc_switch_channel_policy_table[policy_idx][MCC_INTERVAL_IDX];
		u8 guard_offset0 = mcc_switch_channel_policy_table[policy_idx][MCC_GUARD_OFFSET0_IDX];
		u8 guard_offset1 = mcc_switch_channel_policy_table[policy_idx][MCC_GUARD_OFFSET1_IDX];
		enum _hw_port tsf_bsae_port = MAX_HW_PORT;
		enum _hw_port tsf_sync_port = MAX_HW_PORT;
		order0_iface = mccobjpriv->iface[0];
		order1_iface = mccobjpriv->iface[1];

		tsf_bsae_port = rtw_hal_get_port(order1_iface);
		tsf_sync_port = rtw_hal_get_port(order0_iface);
		
		/* FW set enable */
		SET_H2CCMD_MCC_TIME_SETTING_FW_EN(cmd, fw_eable);
		/* TSF Sync offset */
		SET_H2CCMD_MCC_TIME_SETTING_TSF_SYNC_OFFSET(cmd, tsf_sync_offset);
		/* start time offset */
		SET_H2CCMD_MCC_TIME_SETTING_START_TIME(cmd, (start_time_offset + guard_offset0));
		/* interval */
		SET_H2CCMD_MCC_TIME_SETTING_INTERVAL(cmd, interval);
		/* Early time to inform driver by C2H before switch channel */
		SET_H2CCMD_MCC_TIME_SETTING_EARLY_SWITCH_RPT(cmd, swchannel_early_time);
		/* Port0 sync from Port1, not support multi-port */
		SET_H2CCMD_MCC_TIME_SETTING_ORDER_BASE(cmd, tsf_bsae_port);
		SET_H2CCMD_MCC_TIME_SETTING_ORDER_SYNC(cmd, tsf_sync_port);
	} else {
		/* start time offset */
		SET_H2CCMD_MCC_TIME_SETTING_START_TIME(cmd, mccobjpriv->start_time);
		/* interval */
		SET_H2CCMD_MCC_TIME_SETTING_INTERVAL(cmd, mccobjpriv->interval);
		/* Early time to inform driver by C2H before switch channel */
		SET_H2CCMD_MCC_TIME_SETTING_EARLY_SWITCH_RPT(cmd, swchannel_early_time);
	}

#ifdef CONFIG_MCC_MODE_DEBUG
	{
		u8 i = 0;

		RTW_INFO("=========================\n");
		RTW_INFO("NoA:\n");
		for (i = 0; i < H2C_MCC_TIME_SETTING_LEN; i++)
			pr_dbg("0x%x ", cmd[i]);
		pr_dbg("\n");
		RTW_INFO("=========================\n");
	}
#endif /* CONFIG_MCC_MODE_DEBUG */

	rtw_hal_fill_h2c_cmd(padapter, H2C_MCC_TIME_SETTING, H2C_MCC_TIME_SETTING_LEN, cmd);
}

static void rtw_hal_set_mcc_IQK_offload_cmd(PADAPTER padapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *pmccobjpriv = &(dvobj->mcc_objpriv);
	struct mcc_adapter_priv *pmccadapriv = NULL;
	_adapter *iface = NULL;
	u8 cmd[H2C_MCC_IQK_PARAM_LEN] = {0}, bready = 0, i = 0, order = 0;
	u16 TX_X = 0, TX_Y = 0, RX_X = 0, RX_Y = 0;
	u8 total_rf_path = GET_HAL_DATA(padapter)->NumTotalRFPath;
	u8 rf_path_idx = 0, last_order = MAX_MCC_NUM - 1, last_rf_path_index = total_rf_path - 1;

	/* by order, last order & last_rf_path_index must set ready bit = 1 */
	for (i = 0; i < MAX_MCC_NUM; i++) {
		iface = pmccobjpriv->iface[i];
		if (iface == NULL)
			continue;

		pmccadapriv = &iface->mcc_adapterpriv;
		order = pmccadapriv->order;

		for (rf_path_idx = 0; rf_path_idx < total_rf_path; rf_path_idx ++) {

			_rtw_memset(cmd, 0, H2C_MCC_IQK_PARAM_LEN);
			TX_X = pmccadapriv->mcc_iqk_arr[rf_path_idx].TX_X & 0x7ff;/* [10:0]  */
			TX_Y = pmccadapriv->mcc_iqk_arr[rf_path_idx].TX_Y & 0x7ff;/* [10:0]  */
			RX_X = pmccadapriv->mcc_iqk_arr[rf_path_idx].RX_X & 0x3ff;/* [9:0]  */
			RX_Y = pmccadapriv->mcc_iqk_arr[rf_path_idx].RX_Y & 0x3ff;/* [9:0]  */

			/* ready or not */
			if (order == last_order && rf_path_idx == last_rf_path_index)
				bready = 1;
			else
				bready = 0;

			SET_H2CCMD_MCC_IQK_READY(cmd, bready);
			SET_H2CCMD_MCC_IQK_ORDER(cmd, order);
			SET_H2CCMD_MCC_IQK_PATH(cmd, rf_path_idx);

			/* fill RX_X[7:0] to (cmd+1)[7:0] bitlen=8 */
			SET_H2CCMD_MCC_IQK_RX_L(cmd, (u8)(RX_X & 0xff));
			/* fill RX_X[9:8] to (cmd+2)[1:0] bitlen=2 */
			SET_H2CCMD_MCC_IQK_RX_M1(cmd, (u8)((RX_X >> 8) & 0x03));
			/* fill RX_Y[5:0] to (cmd+2)[7:2] bitlen=6 */
			SET_H2CCMD_MCC_IQK_RX_M2(cmd, (u8)(RX_Y & 0x3f));
			/* fill RX_Y[9:6] to (cmd+3)[3:0] bitlen=4 */
			SET_H2CCMD_MCC_IQK_RX_H(cmd, (u8)((RX_Y >> 6) & 0x0f));


			/* fill TX_X[7:0] to (cmd+4)[7:0] bitlen=8 */
			SET_H2CCMD_MCC_IQK_TX_L(cmd, (u8)(TX_X & 0xff));
			/* fill TX_X[10:8] to (cmd+5)[2:0] bitlen=3 */
			SET_H2CCMD_MCC_IQK_TX_M1(cmd, (u8)((TX_X >> 8) & 0x07));
			/* fill TX_Y[4:0] to (cmd+5)[7:3] bitlen=5 */
			SET_H2CCMD_MCC_IQK_TX_M2(cmd, (u8)(TX_Y & 0x1f));
			/* fill TX_Y[10:5] to (cmd+6)[5:0] bitlen=6 */
			SET_H2CCMD_MCC_IQK_TX_H(cmd, (u8)((TX_Y >> 5) & 0x3f));

#ifdef CONFIG_MCC_MODE_DEBUG
			RTW_INFO("=========================\n");
			RTW_INFO(FUNC_ADPT_FMT" IQK:\n", FUNC_ADPT_ARG(iface));
			RTW_INFO("TX_X: 0x%02x\n", TX_X);
			RTW_INFO("TX_Y: 0x%02x\n", TX_Y);
			RTW_INFO("RX_X: 0x%02x\n", RX_X);
			RTW_INFO("RX_Y: 0x%02x\n", RX_Y);
			RTW_INFO("cmd[0]:0x%02x\n", cmd[0]);
			RTW_INFO("cmd[1]:0x%02x\n", cmd[1]);
			RTW_INFO("cmd[2]:0x%02x\n", cmd[2]);
			RTW_INFO("cmd[3]:0x%02x\n", cmd[3]);
			RTW_INFO("cmd[4]:0x%02x\n", cmd[4]);
			RTW_INFO("cmd[5]:0x%02x\n", cmd[5]);
			RTW_INFO("cmd[6]:0x%02x\n", cmd[6]);
			RTW_INFO("=========================\n");
#endif /* CONFIG_MCC_MODE_DEBUG */

			rtw_hal_fill_h2c_cmd(padapter, H2C_MCC_IQK_PARAM, H2C_MCC_IQK_PARAM_LEN, cmd);
		}
	}
}


static void rtw_hal_set_mcc_macid_cmd(PADAPTER padapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct mcc_adapter_priv *pmccadapriv = NULL;
	_adapter *iface = NULL;
	u8 cmd[H2C_MCC_MACID_BITMAP_LEN] = {0}, i = 0, order = 0;
	u16 bitmap = 0;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (iface == NULL)
			continue;

		pmccadapriv = &iface->mcc_adapterpriv;
		if (pmccadapriv->role == MCC_ROLE_MAX)
			continue;
		
		order = pmccadapriv->order;
		bitmap = pmccadapriv->mcc_macid_bitmap;

		if (order >= (H2C_MCC_MACID_BITMAP_LEN/2)) {
			RTW_INFO(FUNC_ADPT_FMT" only support 3 interface at most(%d)\n"
				, FUNC_ADPT_ARG(padapter), order);
			continue;
		}
		SET_H2CCMD_MCC_MACID_BITMAP_L((cmd + order * 2), (u8)(bitmap & 0xff));
		SET_H2CCMD_MCC_MACID_BITMAP_H((cmd + order * 2), (u8)((bitmap >> 8) & 0xff));
	}

#ifdef CONFIG_MCC_MODE_DEBUG
	RTW_INFO("=========================\n");
	RTW_INFO("MACID BITMAP: ");
	for (i = 0; i < H2C_MCC_MACID_BITMAP_LEN; i++)
		printk("0x%x ", cmd[i]);
	printk("\n");
	RTW_INFO("=========================\n");
#endif /* CONFIG_MCC_MODE_DEBUG */
	rtw_hal_fill_h2c_cmd(padapter, H2C_MCC_MACID_BITMAP, H2C_MCC_MACID_BITMAP_LEN, cmd);
}

#ifdef CONFIG_MCC_MODE_V2
static u8 get_pri_ch_idx_by_adapter(u8 center_ch, u8 channel, u8 bw, u8 ch_offset40)
{
	u8 pri_ch_idx = 0, chnl_offset80 = 0;

	if (bw == CHANNEL_WIDTH_80) {
		if (center_ch > channel)
			chnl_offset80 = HAL_PRIME_CHNL_OFFSET_LOWER;
		else if (center_ch < channel)
			chnl_offset80 = HAL_PRIME_CHNL_OFFSET_UPPER;
		else
			chnl_offset80 = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	}

	if (bw == CHANNEL_WIDTH_80) {
		/* primary channel is at lower subband of 80MHz & 40MHz */
		if ((ch_offset40 == HAL_PRIME_CHNL_OFFSET_LOWER) && (chnl_offset80 == HAL_PRIME_CHNL_OFFSET_LOWER))
			pri_ch_idx = VHT_DATA_SC_20_LOWEST_OF_80MHZ;
		/* primary channel is at lower subband of 80MHz & upper subband of 40MHz */
		else if ((ch_offset40 == HAL_PRIME_CHNL_OFFSET_UPPER) && (chnl_offset80 == HAL_PRIME_CHNL_OFFSET_LOWER))
			pri_ch_idx = VHT_DATA_SC_20_LOWER_OF_80MHZ;
		/* primary channel is at upper subband of 80MHz & lower subband of 40MHz */
		else if ((ch_offset40 == HAL_PRIME_CHNL_OFFSET_LOWER) && (chnl_offset80 == HAL_PRIME_CHNL_OFFSET_UPPER))
			pri_ch_idx = VHT_DATA_SC_20_UPPER_OF_80MHZ;
		/* primary channel is at upper subband of 80MHz & upper subband of 40MHz */
		else if ((ch_offset40 == HAL_PRIME_CHNL_OFFSET_UPPER) && (chnl_offset80 == HAL_PRIME_CHNL_OFFSET_UPPER))
			pri_ch_idx = VHT_DATA_SC_20_UPPERST_OF_80MHZ;
		else {
			if (chnl_offset80 == HAL_PRIME_CHNL_OFFSET_LOWER)
				pri_ch_idx = VHT_DATA_SC_40_LOWER_OF_80MHZ;
			else if (chnl_offset80 == HAL_PRIME_CHNL_OFFSET_UPPER)
				pri_ch_idx = VHT_DATA_SC_40_UPPER_OF_80MHZ;
			else
				RTW_INFO("SCMapping: DONOT CARE Mode Setting\n");
		}
	} else if (bw == CHANNEL_WIDTH_40) {
		/* primary channel is at upper subband of 40MHz */
		if (ch_offset40== HAL_PRIME_CHNL_OFFSET_UPPER)
			pri_ch_idx = VHT_DATA_SC_20_UPPER_OF_80MHZ;
		/* primary channel is at lower subband of 40MHz */
		else if (ch_offset40 == HAL_PRIME_CHNL_OFFSET_LOWER)
			pri_ch_idx = VHT_DATA_SC_20_LOWER_OF_80MHZ;
		else
			RTW_INFO("SCMapping: DONOT CARE Mode Setting\n");
	}

	return  pri_ch_idx;
}

static void rtw_hal_set_mcc_ctrl_cmd_v2(PADAPTER padapter, u8 stop)
{
	u8 cmd[H2C_MCC_CTRL_LEN] = {0}, i = 0;
	u8 order = 0, totalnum = 0;
	u8 center_ch = 0, pri_ch_idx = 0, bw = 0;
	u8 duration = 0, role = 0, incurch = 0, rfetype = 0, distxnull = 0, c2hrpt = 0;
	u8 dis_sw_retry = 0, null_early_time=2, tsfx = 0, update_parm = 0;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *pmccobjpriv = &(dvobj->mcc_objpriv);
	struct mcc_adapter_priv *mccadapriv = NULL;
	struct mlme_ext_priv *pmlmeext = NULL;
	struct mlme_ext_info *pmlmeinfo = NULL;
	_adapter *iface = NULL;

	RTW_INFO(FUNC_ADPT_FMT": stop=%d\n", FUNC_ADPT_ARG(padapter), stop);

	for (i = 0; i < MAX_MCC_NUM; i++) {
		iface = pmccobjpriv->iface[i];
		if (iface == NULL)
			continue;

		if (stop) {
			if (iface != padapter)
				continue;
		}

		mccadapriv = &iface->mcc_adapterpriv;
		order = mccadapriv->order;

		if (!stop)
			totalnum = MAX_MCC_NUM;
		else
			totalnum = 0xff; /* 0xff means stop */

		pmlmeext = &iface->mlmeextpriv;
		center_ch = rtw_get_center_ch(pmlmeext->cur_channel, pmlmeext->cur_bwmode, pmlmeext->cur_ch_offset);
		pri_ch_idx = get_pri_ch_idx_by_adapter(center_ch, pmlmeext->cur_channel, pmlmeext->cur_bwmode, pmlmeext->cur_ch_offset);
		bw = pmlmeext->cur_bwmode;
		duration = mccadapriv->mcc_duration;
		role = mccadapriv->role;

		incurch = _FALSE;
		dis_sw_retry = _TRUE;

		/* STA/GC TX NULL data to inform AP/GC for ps mode */
		switch (role) {
		case MCC_ROLE_GO:
		case MCC_ROLE_AP:
			distxnull = MCC_DISABLE_TX_NULL;
			break;
		case MCC_ROLE_GC:
			set_channel_bwmode(iface, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);
			distxnull = MCC_ENABLE_TX_NULL;
			break;
		case MCC_ROLE_STA:
			distxnull = MCC_ENABLE_TX_NULL;
			break;
		}

		null_early_time = mccadapriv->null_early;

		c2hrpt = MCC_C2H_REPORT_ALL_STATUS;
		tsfx = rtw_hal_get_port(iface);
		update_parm = 0;

		SET_H2CCMD_MCC_CTRL_V2_ORDER(cmd, order);
		SET_H2CCMD_MCC_CTRL_V2_TOTALNUM(cmd, totalnum);
		SET_H2CCMD_MCC_CTRL_V2_CENTRAL_CH(cmd, center_ch);
		SET_H2CCMD_MCC_CTRL_V2_PRIMARY_CH(cmd, pri_ch_idx);
		SET_H2CCMD_MCC_CTRL_V2_BW(cmd, bw);
		SET_H2CCMD_MCC_CTRL_V2_DURATION(cmd, duration);
		SET_H2CCMD_MCC_CTRL_V2_ROLE(cmd, role);
		SET_H2CCMD_MCC_CTRL_V2_INCURCH(cmd, incurch);
		SET_H2CCMD_MCC_CTRL_V2_DIS_SW_RETRY(cmd, dis_sw_retry);
		SET_H2CCMD_MCC_CTRL_V2_DISTXNULL(cmd, distxnull);
		SET_H2CCMD_MCC_CTRL_V2_C2HRPT(cmd, c2hrpt);
		SET_H2CCMD_MCC_CTRL_V2_TSFX(cmd, tsfx);
		SET_H2CCMD_MCC_CTRL_V2_NULL_EARLY(cmd, null_early_time);
		SET_H2CCMD_MCC_CTRL_V2_UPDATE_PARM(cmd, update_parm);

#ifdef CONFIG_MCC_MODE_DEBUG
		RTW_INFO("=========================\n");
		RTW_INFO(FUNC_ADPT_FMT" MCC INFO:\n", FUNC_ADPT_ARG(iface));
		RTW_INFO("cmd[0]:0x%02x\n", cmd[0]);
		RTW_INFO("cmd[1]:0x%02x\n", cmd[1]);
		RTW_INFO("cmd[2]:0x%02x\n", cmd[2]);
		RTW_INFO("cmd[3]:0x%02x\n", cmd[3]);
		RTW_INFO("cmd[4]:0x%02x\n", cmd[4]);
		RTW_INFO("cmd[5]:0x%02x\n", cmd[5]);
		RTW_INFO("cmd[6]:0x%02x\n", cmd[6]);
		RTW_INFO("=========================\n");
#endif /* CONFIG_MCC_MODE_DEBUG */

		rtw_hal_fill_h2c_cmd(padapter, H2C_MCC_CTRL_V2, H2C_MCC_CTRL_LEN, cmd);
	}
}

#else
static void rtw_hal_set_mcc_ctrl_cmd_v1(PADAPTER padapter, u8 stop)
{
	u8 cmd[H2C_MCC_CTRL_LEN] = {0}, i = 0;
	u8 order = 0, totalnum = 0, chidx = 0, bw = 0, bw40sc = 0, bw80sc = 0;
	u8 duration = 0, role = 0, incurch = 0, rfetype = 0, distxnull = 0, c2hrpt = 0, chscan = 0;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *pmccobjpriv = &(dvobj->mcc_objpriv);
	struct mcc_adapter_priv *mccadapriv = NULL;
	struct mlme_ext_priv *pmlmeext = NULL;
	struct mlme_ext_info *pmlmeinfo = NULL;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	_adapter *iface = NULL;

	RTW_INFO(FUNC_ADPT_FMT": stop=%d\n", FUNC_ADPT_ARG(padapter), stop);

	for (i = 0; i < MAX_MCC_NUM; i++) {
		iface = pmccobjpriv->iface[i];
		if (iface == NULL)
			continue;

		if (stop) {
			if (iface != padapter)
				continue;
		}

		mccadapriv = &iface->mcc_adapterpriv;
		order = mccadapriv->order;

		if (!stop)
			totalnum = MAX_MCC_NUM;
		else
			totalnum = 0xff; /* 0xff means stop */

		pmlmeext = &iface->mlmeextpriv;
		chidx = pmlmeext->cur_channel;
		bw = pmlmeext->cur_bwmode;
		bw40sc = pmlmeext->cur_ch_offset;

		/* decide 80 band width offset */
		if (bw == CHANNEL_WIDTH_80) {
			u8 center_ch = rtw_get_center_ch(chidx, bw, bw40sc);

			if (center_ch > chidx)
				bw80sc = HAL_PRIME_CHNL_OFFSET_LOWER;
			else if (center_ch < chidx)
				bw80sc = HAL_PRIME_CHNL_OFFSET_UPPER;
			else
				bw80sc = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		} else
			bw80sc = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

		duration = mccadapriv->mcc_duration;
		role = mccadapriv->role;

		incurch = _FALSE;

		if (IS_HARDWARE_TYPE_8812(padapter))
			rfetype = pHalData->rfe_type; /* RFETYPE (only for 8812)*/
		else
			rfetype = 0;

		/* STA/GC TX NULL data to inform AP/GC for ps mode */
		switch (role) {
		case MCC_ROLE_GO:
		case MCC_ROLE_AP:
			distxnull = MCC_DISABLE_TX_NULL;
			break;
		case MCC_ROLE_GC:
		case MCC_ROLE_STA:
			distxnull = MCC_ENABLE_TX_NULL;
			break;
		}

		c2hrpt = MCC_C2H_REPORT_ALL_STATUS;
		chscan = MCC_CHIDX;

		SET_H2CCMD_MCC_CTRL_ORDER(cmd, order);
		SET_H2CCMD_MCC_CTRL_TOTALNUM(cmd, totalnum);
		SET_H2CCMD_MCC_CTRL_CHIDX(cmd, chidx);
		SET_H2CCMD_MCC_CTRL_BW(cmd, bw);
		SET_H2CCMD_MCC_CTRL_BW40SC(cmd, bw40sc);
		SET_H2CCMD_MCC_CTRL_BW80SC(cmd, bw80sc);
		SET_H2CCMD_MCC_CTRL_DURATION(cmd, duration);
		SET_H2CCMD_MCC_CTRL_ROLE(cmd, role);
		SET_H2CCMD_MCC_CTRL_INCURCH(cmd, incurch);
		SET_H2CCMD_MCC_CTRL_RFETYPE(cmd, rfetype);
		SET_H2CCMD_MCC_CTRL_DISTXNULL(cmd, distxnull);
		SET_H2CCMD_MCC_CTRL_C2HRPT(cmd, c2hrpt);
		SET_H2CCMD_MCC_CTRL_CHSCAN(cmd, chscan);

#ifdef CONFIG_MCC_MODE_DEBUG
		RTW_INFO("=========================\n");
		RTW_INFO(FUNC_ADPT_FMT" MCC INFO:\n", FUNC_ADPT_ARG(iface));
		RTW_INFO("cmd[0]:0x%02x\n", cmd[0]);
		RTW_INFO("cmd[1]:0x%02x\n", cmd[1]);
		RTW_INFO("cmd[2]:0x%02x\n", cmd[2]);
		RTW_INFO("cmd[3]:0x%02x\n", cmd[3]);
		RTW_INFO("cmd[4]:0x%02x\n", cmd[4]);
		RTW_INFO("cmd[5]:0x%02x\n", cmd[5]);
		RTW_INFO("cmd[6]:0x%02x\n", cmd[6]);
		RTW_INFO("=========================\n");
#endif /* CONFIG_MCC_MODE_DEBUG */

		rtw_hal_fill_h2c_cmd(padapter, H2C_MCC_CTRL, H2C_MCC_CTRL_LEN, cmd);
	}
}
#endif

static void rtw_hal_set_mcc_ctrl_cmd(PADAPTER padapter, u8 stop)
{
	#ifdef CONFIG_MCC_MODE_V2
		/* new cmd 0x17 */
		rtw_hal_set_mcc_ctrl_cmd_v2(padapter, stop);
	#else
		/* old cmd 0x18 */
		rtw_hal_set_mcc_ctrl_cmd_v1(padapter, stop);
	#endif
}

static u8 check_mcc_support(PADAPTER adapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	u8 sta_linked_num = DEV_STA_LD_NUM(dvobj);
	u8 starting_ap_num = DEV_AP_STARTING_NUM(dvobj);
	u8 ap_num = DEV_AP_NUM(dvobj);
	u8 ret = _FAIL;

	RTW_INFO("[MCC] sta_linked_num=%d, starting_ap_num=%d,ap_num=%d\n",
		sta_linked_num, starting_ap_num, ap_num);

	/* case for sta + sta case  */
	if (sta_linked_num == MAX_MCC_NUM) {
		ret = _SUCCESS;
		goto exit;
	}

	/* case for starting AP + linked sta */
	if ((starting_ap_num + sta_linked_num) == MAX_MCC_NUM) {
		ret = _SUCCESS;
		goto exit;
	}

	/* case for started AP + linked sta */
	if ((ap_num + sta_linked_num) == MAX_MCC_NUM) {
		ret = _SUCCESS;
		goto exit;
	}

exit:
		return ret;
}

static void rtw_hal_mcc_start_prehdl(PADAPTER padapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	_adapter *iface = NULL;
	struct mcc_adapter_priv *mccadapriv = NULL;
	u8 i = 1;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (iface == NULL)
			continue;

		mccadapriv = &iface->mcc_adapterpriv;
		mccadapriv->role = MCC_ROLE_MAX;
	}
}

static u8 rtw_hal_set_mcc_start_setting(PADAPTER padapter, u8 status)
{
	u8 ret = _SUCCESS, enable_tsf_auto_sync = _FALSE;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(dvobj);

	if (pwrpriv->pwr_mode != PS_MODE_ACTIVE) {
		rtw_warn_on(1);
		RTW_INFO("PS mode is not active before start mcc, force exit ps mode\n");
		LeaveAllPowerSaveModeDirect(padapter);
	}

	if (check_mcc_support(padapter) == _FAIL) {
		ret = _FAIL;
		goto exit;
	}

	rtw_hal_mcc_start_prehdl(padapter);

	/* configure mcc switch channel setting */
	rtw_hal_config_mcc_switch_channel_setting(padapter);

	if (rtw_hal_decide_mcc_role(padapter) == _FAIL) {
		ret = _FAIL;
		goto exit;
	}

	/* set mcc status to indicate process mcc start setting */
	rtw_hal_set_mcc_status(padapter, MCC_STATUS_PROCESS_MCC_START_SETTING);

	/* only download rsvd page for connect */
	if (status == MCC_SETCMD_STATUS_START_CONNECT) {
		/* download mcc rsvd page */
		rtw_hal_set_fw_mcc_rsvd_page(padapter);
		rtw_hal_set_mcc_rsvdpage_cmd(padapter);
	}

	/* configure time setting */
	rtw_hal_set_mcc_time_setting_cmd(padapter);

#ifndef CONFIG_MCC_MODE_V2
	/* IQK value offload */
	rtw_hal_set_mcc_IQK_offload_cmd(padapter);
#endif

	/* set mac id to fw */
	rtw_hal_set_mcc_macid_cmd(padapter);

	if (dvobj->p0_tsf.sync_port != MAX_HW_PORT ) {
		/* disable tsf auto sync */
		RTW_INFO("[MCC] disable HW TSF sync\n");
		rtw_hal_set_hwreg(padapter, HW_VAR_TSF_AUTO_SYNC, &enable_tsf_auto_sync);
	} else {
		RTW_INFO("[MCC] already disable HW TSF sync\n");
	}

	/* set mcc parameter  */
	rtw_hal_set_mcc_ctrl_cmd(padapter, _FALSE);

exit:
	return ret;
}

static void rtw_hal_set_mcc_stop_setting(PADAPTER padapter, u8 status)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *mccobjpriv = &dvobj->mcc_objpriv;
	_adapter *iface = NULL;
	struct mcc_adapter_priv *mccadapriv = NULL;
	u8 i = 0;
	/*
	 * when adapter disconnect, stop mcc mod
	 * total=0xf means stop mcc mode
	 */

	switch (status) {
	default:
		/* let fw switch to other interface channel */
		for (i = 0; i < MAX_MCC_NUM; i++) {
			iface = mccobjpriv->iface[i];
			if (iface == NULL)
				continue;

			mccadapriv = &iface->mcc_adapterpriv;

			/* use other interface to set cmd */
			if (iface != padapter) {
				rtw_hal_set_mcc_ctrl_cmd(iface, _TRUE);
				break;
			}
		}
		break;
	}
}

static void rtw_hal_mcc_status_hdl(PADAPTER padapter, u8 status)
{
	switch (status) {
	case MCC_SETCMD_STATUS_STOP_DISCONNECT:
		rtw_hal_clear_mcc_status(padapter, MCC_STATUS_NEED_MCC | MCC_STATUS_DOING_MCC);
		break;
	case MCC_SETCMD_STATUS_STOP_SCAN_START:
		rtw_hal_set_mcc_status(padapter, MCC_STATUS_NEED_MCC);
		rtw_hal_clear_mcc_status(padapter, MCC_STATUS_DOING_MCC);
		break;

	case MCC_SETCMD_STATUS_START_CONNECT:
	case MCC_SETCMD_STATUS_START_SCAN_DONE:
		rtw_hal_set_mcc_status(padapter, MCC_STATUS_NEED_MCC | MCC_STATUS_DOING_MCC);
		break;
	default:
		RTW_INFO(FUNC_ADPT_FMT" error status(%d)\n", FUNC_ADPT_ARG(padapter), status);
		break;
	}
}

static void rtw_hal_mcc_stop_posthdl(PADAPTER padapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *mccobjpriv = &(adapter_to_dvobj(padapter)->mcc_objpriv);
	struct mcc_adapter_priv *mccadapriv = NULL;
	_adapter *iface = NULL;
	PHAL_DATA_TYPE hal;
	u8 i = 0;
	u8 enable_rx_bar = _FALSE;

	hal = GET_HAL_DATA(padapter);

	for (i = 0; i < MAX_MCC_NUM; i++) {
		iface = mccobjpriv->iface[i];
		if (iface == NULL)
			continue;

		/* release network queue */
		rtw_netif_wake_queue(iface->pnetdev);
		mccadapriv = &iface->mcc_adapterpriv;
		mccadapriv->mcc_tx_bytes_from_kernel = 0;
		mccadapriv->mcc_last_tx_bytes_from_kernel = 0;
		mccadapriv->mcc_tx_bytes_to_port = 0;

		if (mccadapriv->role == MCC_ROLE_GO)
			rtw_hal_mcc_remove_go_p2p_ie(iface);

#ifdef CONFIG_TDLS
		if (MLME_IS_STA(iface)) {
			if (mccadapriv->backup_tdls_en) {
				rtw_enable_tdls_func(iface);
				RTW_INFO("%s: Disable MCC, Enable TDLS\n", __func__);
				mccadapriv->backup_tdls_en = _FALSE;
			}
		}
#endif /* CONFIG_TDLS */

		mccadapriv->role = MCC_ROLE_MAX;
		mccobjpriv->iface[i] = NULL;
	}

	/* force switch channel */
	hal->current_channel = 0;
	hal->current_channel_bw = CHANNEL_WIDTH_MAX;
	#ifdef CONFIG_MCC_PHYDM_OFFLOAD
	rtw_hal_mcc_cfg_phydm(padapter, MCC_CFG_PHYDM_STOP, NULL);
	#endif
}

static void rtw_hal_mcc_start_posthdl(PADAPTER padapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *mccobjpriv = &(adapter_to_dvobj(padapter)->mcc_objpriv);
	struct mcc_adapter_priv *mccadapriv = NULL;
	struct pwrctrl_priv	*pwrpriv = adapter_to_pwrctl(padapter);
	_adapter *iface = NULL;
	u8 i = 0, order = 0;
	u8 enable_rx_bar = _TRUE;

	for (i = 0; i < MAX_MCC_NUM; i++) {
		iface = mccobjpriv->iface[i];
		if (iface == NULL)
			continue;

		mccadapriv = &iface->mcc_adapterpriv;
		if (mccadapriv->role == MCC_ROLE_MAX)
			continue;
		
		mccadapriv->mcc_tx_bytes_from_kernel = 0;
		mccadapriv->mcc_last_tx_bytes_from_kernel = 0;
		mccadapriv->mcc_tx_bytes_to_port = 0;

#ifdef CONFIG_TDLS
		if (MLME_IS_STA(iface)) {
			if (rtw_is_tdls_enabled(iface)) {
				mccadapriv->backup_tdls_en = _TRUE;
				rtw_disable_tdls_func(iface, _TRUE);
				RTW_INFO("%s: Enable MCC, Disable TDLS\n", __func__);
			}
		}
#endif /* CONFIG_TDLS */
	}
	#ifdef CONFIG_MCC_PHYDM_OFFLOAD
	rtw_hal_mcc_cfg_phydm(padapter, MCC_CFG_PHYDM_START, NULL);
	#endif
}

/*
 * rtw_hal_set_mcc_setting - set mcc setting
 * @padapter: currnet padapter to stop/start MCC
 * @stop: stop mcc or not
 * @return val: 1 for SUCCESS, 0 for fail
 */
static u8 rtw_hal_set_mcc_setting(PADAPTER padapter, u8 status)
{
	u8 ret = _FAIL;
	struct mcc_obj_priv *pmccobjpriv = &(adapter_to_dvobj(padapter)->mcc_objpriv);
	u8 stop = (status < MCC_SETCMD_STATUS_START_CONNECT) ? _TRUE : _FALSE;
	u32 start_time = rtw_get_current_time();

	RTW_INFO("===> "FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));

	rtw_sctx_init(&pmccobjpriv->mcc_sctx, MCC_EXPIRE_TIME);
	pmccobjpriv->mcc_c2h_status = MCC_RPT_MAX;

	if (stop == _FALSE) {
		/* handle mcc start */
		if (rtw_hal_set_mcc_start_setting(padapter, status) == _FAIL)
			goto exit;

		/* wait for C2H */
		if (!rtw_sctx_wait(&pmccobjpriv->mcc_sctx, __func__))
			RTW_INFO(FUNC_ADPT_FMT": wait for mcc start C2H time out\n", FUNC_ADPT_ARG(padapter));
		else
			ret = _SUCCESS;

		if (ret == _SUCCESS) {
			RTW_INFO(FUNC_ADPT_FMT": mcc start sucecssfully\n", FUNC_ADPT_ARG(padapter));
			rtw_hal_mcc_status_hdl(padapter, status);
			rtw_hal_mcc_start_posthdl(padapter);
		}
	} else {

		/* set mcc status to indicate process mcc start setting */
		rtw_hal_set_mcc_status(padapter, MCC_STATUS_PROCESS_MCC_STOP_SETTING);

		/* handle mcc stop */
		rtw_hal_set_mcc_stop_setting(padapter, status);

		/* wait for C2H */
		if (!rtw_sctx_wait(&pmccobjpriv->mcc_sctx, __func__))
			RTW_INFO(FUNC_ADPT_FMT": wait for mcc stop C2H time out\n", FUNC_ADPT_ARG(padapter));
		else {
			ret = _SUCCESS;
			rtw_hal_mcc_status_hdl(padapter, status);
			rtw_hal_mcc_stop_posthdl(padapter);
		}
	}

exit:
	/* clear mcc status */
	rtw_hal_clear_mcc_status(padapter
		, MCC_STATUS_PROCESS_MCC_START_SETTING | MCC_STATUS_PROCESS_MCC_STOP_SETTING);

	RTW_INFO(FUNC_ADPT_FMT" in %dms <===\n"
		, FUNC_ADPT_ARG(padapter), rtw_get_passing_time_ms(start_time));
	return ret;
}

/**
 * rtw_hal_mcc_check_case_not_limit_traffic - handler flow ctrl for special case
 * @cur_iface: fw stay channel setting of this iface
 * @next_iface: fw will swich channel setting of this iface
 */
static void rtw_hal_mcc_check_case_not_limit_traffic(PADAPTER cur_iface, PADAPTER next_iface)
{
	u8 cur_bw = cur_iface->mlmeextpriv.cur_bwmode;
	u8 next_bw = next_iface->mlmeextpriv.cur_bwmode;

	/* for both interface are VHT80, doesn't limit_traffic according to iperf results */
	if (cur_bw == CHANNEL_WIDTH_80 && next_bw == CHANNEL_WIDTH_80) {
		cur_iface->mcc_adapterpriv.mcc_tp_limit = _FALSE;
		next_iface->mcc_adapterpriv.mcc_tp_limit = _FALSE;
	}
}


/**
 * rtw_hal_mcc_sw_ch_fw_notify_hdl - handler flow ctrl
 */
static void rtw_hal_mcc_sw_ch_fw_notify_hdl(PADAPTER padapter)
{
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *pmccobjpriv = &(pdvobjpriv->mcc_objpriv);
	struct mcc_adapter_priv *cur_mccadapriv = NULL, *next_mccadapriv = NULL;
	_adapter *iface = NULL, *cur_iface = NULL, *next_iface = NULL;
	struct registry_priv *preg = &padapter->registrypriv;
	u8 cur_op_ch = pdvobjpriv->oper_channel;
	u8 i = 0, iface_num = pdvobjpriv->iface_nums, cur_order = 0, next_order = 0;
	static u8 cnt = 1;
	u32 single_tx_cri = preg->rtw_mcc_single_tx_cri;

	for (i = 0; i < iface_num; i++) {
		iface = pdvobjpriv->padapters[i];
		if (iface == NULL)
			continue;

		if (cur_op_ch == iface->mlmeextpriv.cur_channel) {
			cur_iface = iface;
			cur_mccadapriv = &cur_iface->mcc_adapterpriv;
			cur_order = cur_mccadapriv->order;
			next_order = (cur_order + 1) % iface_num;
			next_iface = pmccobjpriv->iface[next_order];
			next_mccadapriv = &next_iface->mcc_adapterpriv;
			break;
		}
	}

	if (cur_iface == NULL || next_iface == NULL) {
		RTW_ERR("cur_iface=%p,next_iface=%p\n", cur_iface, next_iface);
		rtw_warn_on(1);
		return;
	}

	/* check other interface tx busy traffic or not under every 2 switch channel notify(Mbits/100ms) */
	if (cnt == 2) {
		cur_mccadapriv->mcc_tp = (cur_mccadapriv->mcc_tx_bytes_from_kernel
			- cur_mccadapriv->mcc_last_tx_bytes_from_kernel) * 10 * 8 / 1024 / 1024;
		cur_mccadapriv->mcc_last_tx_bytes_from_kernel = cur_mccadapriv->mcc_tx_bytes_from_kernel;

		next_mccadapriv->mcc_tp = (next_mccadapriv->mcc_tx_bytes_from_kernel
			- next_mccadapriv->mcc_last_tx_bytes_from_kernel) * 10 * 8 / 1024 / 1024;
		next_mccadapriv->mcc_last_tx_bytes_from_kernel = next_mccadapriv->mcc_tx_bytes_from_kernel;

		cnt = 1;
	} else
		cnt = 2;

	/* check single TX or cuncurrnet TX */
	if (next_mccadapriv->mcc_tp < single_tx_cri) {
		/* single TX, does not stop */
		cur_mccadapriv->mcc_tx_stop = _FALSE;
		cur_mccadapriv->mcc_tp_limit = _FALSE;
	} else {
		/* concurrent TX, stop */
		cur_mccadapriv->mcc_tx_stop = _TRUE;
		cur_mccadapriv->mcc_tp_limit = _TRUE;
	}

	if (cur_mccadapriv->mcc_tp < single_tx_cri) {
		next_mccadapriv->mcc_tx_stop  = _FALSE;
		next_mccadapriv->mcc_tp_limit = _FALSE;
	} else {
		next_mccadapriv->mcc_tx_stop = _FALSE;
		next_mccadapriv->mcc_tp_limit = _TRUE;
		next_mccadapriv->mcc_tx_bytes_to_port = 0;
	}

	/* stop current iface kernel queue or not */
	if (cur_mccadapriv->mcc_tx_stop)
		rtw_netif_stop_queue(cur_iface->pnetdev);
	else
		rtw_netif_wake_queue(cur_iface->pnetdev);

	/* stop next iface kernel queue or not */
	if (next_mccadapriv->mcc_tx_stop)
		rtw_netif_stop_queue(next_iface->pnetdev);
	else
		rtw_netif_wake_queue(next_iface->pnetdev);

	/* start xmit tasklet */
	rtw_os_xmit_schedule(next_iface);

	rtw_hal_mcc_check_case_not_limit_traffic(cur_iface, next_iface);

	if (0) {
		RTW_INFO("order:%d, mcc_tx_stop:%d, mcc_tp:%d\n",
			cur_mccadapriv->order, cur_mccadapriv->mcc_tx_stop, cur_mccadapriv->mcc_tp);
		dump_os_queue(0, cur_iface);
		RTW_INFO("order:%d, mcc_tx_stop:%d, mcc_tp:%d\n",
			next_mccadapriv->order, next_mccadapriv->mcc_tx_stop, next_mccadapriv->mcc_tp);
		dump_os_queue(0, next_iface);
	}
}

static void rtw_hal_mcc_update_noa_start_time_hdl(PADAPTER padapter, u8 buflen, u8 *tmpBuf)
{
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *pmccobjpriv = &(pdvobjpriv->mcc_objpriv);
	struct mcc_adapter_priv *pmccadapriv = NULL;
	PADAPTER iface = NULL;
	u8 i = 0;
	u8 policy_idx = pmccobjpriv->policy_index;
	u8 noa_tsf_sync_offset = mcc_switch_channel_policy_table[policy_idx][MCC_TSF_SYNC_OFFSET_IDX];
	u8 noa_start_time_offset = mcc_switch_channel_policy_table[policy_idx][MCC_START_TIME_OFFSET_IDX];
	
	for (i = 0; i < pdvobjpriv->iface_nums; i++) {
		iface = pdvobjpriv->padapters[i];
		if (iface == NULL)
			continue;
		
		pmccadapriv = &iface->mcc_adapterpriv;
		if (pmccadapriv->role == MCC_ROLE_MAX)
			continue;

		/* GO & channel match */
		if (pmccadapriv->role == MCC_ROLE_GO) {
			/* convert GO TBTT from FW to noa_start_time(TU convert to mircosecond) */
			pmccadapriv->noa_start_time = RTW_GET_LE32(tmpBuf + 2) + noa_start_time_offset * TU;

			if (0) {
				RTW_INFO("TBTT:0x%02x\n", RTW_GET_LE32(tmpBuf + 2));
				RTW_INFO("noa_tsf_sync_offset:%d, noa_start_time_offset:%d\n", noa_tsf_sync_offset, noa_start_time_offset);
				RTW_INFO(FUNC_ADPT_FMT"buf=0x%02x:0x%02x:0x%02x:0x%02x, noa_start_time=0x%02x\n"
					, FUNC_ADPT_ARG(iface)
					, tmpBuf[2]
					, tmpBuf[3]
					, tmpBuf[4]
					, tmpBuf[5]
					,pmccadapriv->noa_start_time);
				}

			rtw_hal_mcc_update_go_p2p_ie(iface);

			break;
		}
	}

}

static u8 mcc_get_reg_hdl(PADAPTER adapter, const u8 *val)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct mcc_obj_priv *mccobjpriv = &(dvobj->mcc_objpriv);
	struct hal_com_data *hal = GET_HAL_DATA(adapter);
	_adapter *cur_iface = NULL;
	u8 ret = _SUCCESS;
	u8 cur_order = 0;

	u16 dbg_reg[DBG_MCC_REG_NUM] = {0x4d4,0x522,0xc50,0xe50};
	u16 dbg_rf_reg[DBG_MCC_RF_REG_NUM] = {0x18};
	u8 i;
	u32 reg_val;
	u8 path = 0, path_nums = 0;

	if (!rtw_hal_check_mcc_status(adapter, MCC_STATUS_DOING_MCC)) {
		ret = _FAIL;
		goto exit;
	}

	if (!val)
		cur_order = 0xff;
	else
		cur_order = *val;

	if (cur_order >= MAX_MCC_NUM && cur_order != 0xff) {
		RTW_ERR("%s: cur_order=%d\n", __func__, cur_order);
		ret = _FAIL;
		goto exit;
	}

	path_nums = hal->NumTotalRFPath;
	if (cur_order == 0xff)
		cur_iface = adapter;
	else
		cur_iface = mccobjpriv->iface[cur_order];

	if (!cur_iface) {
		RTW_ERR("%s: cur_iface = NULL,  cur_order=%d\n", __func__, cur_order);
		ret = _FAIL;
		goto exit;
	}

	_enter_critical_mutex(&mccobjpriv->mcc_dbg_reg_mutex, NULL);
	if (!RTW_CANNOT_IO(adapter)) {
		/* RTW_INFO("=================================\n");
		RTW_INFO(ADPT_FMT": cur_order:%d\n", ADPT_ARG(cur_iface), cur_order); */
		
		for (i = 0; i < ARRAY_SIZE(dbg_reg); i++) {
			reg_val = rtw_read32(adapter, dbg_reg[i]);
			mccobjpriv->dbg_reg[i] = dbg_reg[i];
			mccobjpriv->dbg_reg_val[i] = reg_val;
			/* RTW_PRINT("REG_%X:0x%08x\n", dbg_reg[i], reg_val); */
		}
		for (i = 0; i < ARRAY_SIZE(dbg_rf_reg); i++) {
			for (path = 0; path < path_nums; path++) {
				reg_val = rtw_hal_read_rfreg(adapter, path, dbg_rf_reg[i], 0xffffffff);
				/* RTW_PRINT("RF_PATH_%d_REG_%X:0x%08x\n",
					path, dbg_rf_reg[i], reg_val); */
				mccobjpriv->dbg_rf_reg[i] = dbg_rf_reg[i];
				mccobjpriv->dbg_rf_reg_val[i][path] = reg_val;
			}
		}
	}
	_exit_critical_mutex(&mccobjpriv->mcc_dbg_reg_mutex, NULL);

exit:
	return ret;
}

static u8 mcc_get_reg_cmd(_adapter *adapter, u8 cur_order)
{
	struct cmd_obj *cmdobj;
	struct drvextra_cmd_parm *pdrvextra_cmd_parm;
	struct cmd_priv *pcmdpriv = &adapter->cmdpriv;
	u8 *mcc_cur_order = NULL;
	u8 res = _SUCCESS;

	
	cmdobj = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (cmdobj == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		rtw_mfree((u8 *)cmdobj, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	mcc_cur_order = rtw_zmalloc(sizeof(u8));
	if (mcc_cur_order == NULL) {
		rtw_mfree((u8 *)cmdobj, sizeof(struct cmd_obj));
		rtw_mfree((u8 *)pdrvextra_cmd_parm, sizeof(struct drvextra_cmd_parm));
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = MCC_CMD_WK_CID;
	pdrvextra_cmd_parm->type = MCC_GET_DBG_REG_WK_CID;
	pdrvextra_cmd_parm->size = 1;
	pdrvextra_cmd_parm->pbuf = mcc_cur_order;

	_rtw_memcpy(mcc_cur_order, &cur_order, 1);

	init_h2fwcmd_w_parm_no_rsp(cmdobj, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));
	res = rtw_enqueue_cmd(pcmdpriv, cmdobj);

exit:
	return res;
}

static void rtw_hal_mcc_rpt_tsf_hdl(PADAPTER padapter, u8 buflen, u8 *tmpBuf)
{
	struct dvobj_priv *dvobjpriv = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *mccobjpriv = &(adapter_to_dvobj(padapter)->mcc_objpriv);
	struct submit_ctx *mcc_tsf_req_sctx = &mccobjpriv->mcc_tsf_req_sctx;
	struct mcc_adapter_priv *mccadapriv = NULL;
	_adapter *iface = NULL;
	u8 order = 0;

	order = mccobjpriv->mcc_tsf_req_sctx_order;
	iface = mccobjpriv->iface[order];
	mccadapriv = &iface->mcc_adapterpriv;
	mccadapriv->tsf = RTW_GET_LE64(tmpBuf + 2);


	if (0)
		RTW_INFO(FUNC_ADPT_FMT" TSF(order:%d):0x%02llx\n", FUNC_ADPT_ARG(iface), mccadapriv->order, mccadapriv->tsf);

	if (mccadapriv->order == (MAX_MCC_NUM - 1))
		rtw_sctx_done(&mcc_tsf_req_sctx);
	else
		mccobjpriv->mcc_tsf_req_sctx_order ++;

}

/**
 * rtw_hal_mcc_c2h_handler - mcc c2h handler
 */
void rtw_hal_mcc_c2h_handler(PADAPTER padapter, u8 buflen, u8 *tmpBuf)
{
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *pmccobjpriv = &(adapter_to_dvobj(padapter)->mcc_objpriv);
	struct mcc_adapter_priv *pmccadapriv = &padapter->mcc_adapterpriv;
	struct submit_ctx *mcc_sctx = &pmccobjpriv->mcc_sctx;
	_adapter *cur_adapter = NULL;
	u8 cur_ch = 0, cur_bw = 0, cur_ch_offset = 0;
	_irqL irqL;

	/* RTW_INFO("[length]=%d, [C2H data]="MAC_FMT"\n", buflen, MAC_ARG(tmpBuf)); */
	/* To avoid reg is set, but driver recive c2h to set wrong oper_channel */
	if (MCC_RPT_STOPMCC == pmccobjpriv->mcc_c2h_status) {
		RTW_INFO(FUNC_ADPT_FMT" MCC alread stops return\n", FUNC_ADPT_ARG(padapter));
		return;
	}

	_enter_critical_bh(&pmccobjpriv->mcc_lock, &irqL);
	pmccobjpriv->mcc_c2h_status = tmpBuf[0];
	pmccobjpriv->current_order = tmpBuf[1];
	cur_adapter = pmccobjpriv->iface[pmccobjpriv->current_order];
	cur_ch = cur_adapter->mlmeextpriv.cur_channel;
	cur_bw = cur_adapter->mlmeextpriv.cur_bwmode;
	cur_ch_offset = cur_adapter->mlmeextpriv.cur_ch_offset;
	rtw_set_oper_ch(cur_adapter, cur_ch);
	rtw_set_oper_bw(cur_adapter, cur_bw);
	rtw_set_oper_choffset(cur_adapter, cur_ch_offset);
	_exit_critical_bh(&pmccobjpriv->mcc_lock, &irqL);

	if (0)
		RTW_INFO("%d,order:%d,TSF:0x%llx\n", tmpBuf[0], tmpBuf[1], RTW_GET_LE64(tmpBuf + 2));
	
	switch (pmccobjpriv->mcc_c2h_status) {
	case MCC_RPT_SUCCESS:
		_enter_critical_bh(&pmccobjpriv->mcc_lock, &irqL);
		pmccobjpriv->cur_mcc_success_cnt++;
		rtw_hal_mcc_upadate_chnl_bw(cur_adapter, cur_ch, cur_ch_offset, cur_bw, _FALSE);
		mcc_get_reg_cmd(padapter, pmccobjpriv->current_order);
		_exit_critical_bh(&pmccobjpriv->mcc_lock, &irqL);
		break;
	case MCC_RPT_TXNULL_FAIL:
		RTW_INFO("[MCC] TXNULL FAIL\n");
		break;
	case MCC_RPT_STOPMCC:
		RTW_INFO("[MCC] MCC stop\n");
		pmccobjpriv->mcc_c2h_status = MCC_RPT_STOPMCC;
		rtw_hal_mcc_upadate_chnl_bw(cur_adapter, cur_ch, cur_ch_offset, cur_bw, _TRUE);
		rtw_sctx_done(&mcc_sctx);
		break;
	case MCC_RPT_READY:
		_enter_critical_bh(&pmccobjpriv->mcc_lock, &irqL);
		/* initialize counter & time */
		pmccobjpriv->mcc_launch_time = rtw_get_current_time();
		pmccobjpriv->mcc_c2h_status = MCC_RPT_READY;
		pmccobjpriv->cur_mcc_success_cnt = 0;
		pmccobjpriv->prev_mcc_success_cnt = 0;
		pmccobjpriv->mcc_tolerance_time = MCC_TOLERANCE_TIME;
		_exit_critical_bh(&pmccobjpriv->mcc_lock, &irqL);

		RTW_INFO("[MCC] MCC ready\n");
		rtw_sctx_done(&mcc_sctx);
		break;
	case MCC_RPT_SWICH_CHANNEL_NOTIFY:
		rtw_hal_mcc_sw_ch_fw_notify_hdl(padapter);
		break;
	case MCC_RPT_UPDATE_NOA_START_TIME:
		rtw_hal_mcc_update_noa_start_time_hdl(padapter, buflen, tmpBuf);
		break;
	case MCC_RPT_TSF:
		_enter_critical_bh(&pmccobjpriv->mcc_lock, &irqL);
		rtw_hal_mcc_rpt_tsf_hdl(padapter, buflen, tmpBuf);
		_exit_critical_bh(&pmccobjpriv->mcc_lock, &irqL);
		break;
	default:
		/* RTW_INFO("[MCC] Other MCC status(%d)\n", pmccobjpriv->mcc_c2h_status); */
		break;
	}
}

void rtw_hal_mcc_update_parameter(PADAPTER padapter, u8 force_update)
{	
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *mccobjpriv = &(dvobj->mcc_objpriv);
	u8 cmd[H2C_MCC_TIME_SETTING_LEN] = {0};
	u8 swchannel_early_time = MCC_SWCH_FW_EARLY_TIME;
	u8 ap_num = DEV_AP_NUM(dvobj);	

	if (ap_num == 0) {
		u8 need_update = _FALSE;
		u8 start_time_offset = 0, interval = 0, duration = 0;

		need_update = rtw_hal_mcc_update_timing_parameters(padapter, force_update);

		if (need_update == _FALSE)
			return;
		
		start_time_offset = mccobjpriv->start_time;
		interval = mccobjpriv->interval;
		duration = mccobjpriv->iface[0]->mcc_adapterpriv.mcc_duration;

		SET_H2CCMD_MCC_TIME_SETTING_START_TIME(cmd, start_time_offset);
		SET_H2CCMD_MCC_TIME_SETTING_INTERVAL(cmd, interval);
		SET_H2CCMD_MCC_TIME_SETTING_EARLY_SWITCH_RPT(cmd, swchannel_early_time);
		SET_H2CCMD_MCC_TIME_SETTING_UPDATE(cmd, _TRUE);
		SET_H2CCMD_MCC_TIME_SETTING_ORDER0_DURATION(cmd, duration);
	} else {
		PADAPTER order0_iface = NULL;
		PADAPTER order1_iface = NULL;
		u8 policy_idx = mccobjpriv->policy_index;
		u8 duration = mcc_switch_channel_policy_table[policy_idx][MCC_DURATION_IDX];
		u8 tsf_sync_offset = mcc_switch_channel_policy_table[policy_idx][MCC_TSF_SYNC_OFFSET_IDX];
		u8 start_time_offset = mcc_switch_channel_policy_table[policy_idx][MCC_START_TIME_OFFSET_IDX];
		u8 interval = mcc_switch_channel_policy_table[policy_idx][MCC_INTERVAL_IDX];
		u8 guard_offset0 = mcc_switch_channel_policy_table[policy_idx][MCC_GUARD_OFFSET0_IDX];
		u8 guard_offset1 = mcc_switch_channel_policy_table[policy_idx][MCC_GUARD_OFFSET1_IDX];
		u8 order0_duration = 0;
		u8 i = 0;
		enum _hw_port tsf_bsae_port = MAX_HW_PORT;
		enum _hw_port tsf_sync_port = MAX_HW_PORT;

		RTW_INFO("%s: policy_idx=%d\n", __func__, policy_idx);

		order0_iface = mccobjpriv->iface[0];
		order1_iface = mccobjpriv->iface[1];

		/* GO/AP is order 0, GC/STA is order 1 */
		order0_duration = order0_iface->mcc_adapterpriv.mcc_duration = interval - duration;
		order0_iface->mcc_adapterpriv.mcc_duration = duration;

		tsf_bsae_port = rtw_hal_get_port(order1_iface);
		tsf_sync_port = rtw_hal_get_port(order0_iface);

		/* update IE */
		for (i = 0; i < dvobj->iface_nums; i++) {
			PADAPTER iface = NULL;
			struct mcc_adapter_priv *mccadapriv = NULL;

			iface = dvobj->padapters[i];
			if (iface == NULL)
				continue;
		
			mccadapriv = &iface->mcc_adapterpriv;
			if (mccadapriv->role == MCC_ROLE_MAX)
				continue;
			
			if (mccadapriv->role == MCC_ROLE_GO)
				rtw_hal_mcc_update_go_p2p_ie(iface);
		}

		/* update H2C cmd */
		/* FW set enable */
		SET_H2CCMD_MCC_TIME_SETTING_FW_EN(cmd, _TRUE);
		/* TSF Sync offset */
		SET_H2CCMD_MCC_TIME_SETTING_TSF_SYNC_OFFSET(cmd, tsf_sync_offset);
		/* start time offset */
		SET_H2CCMD_MCC_TIME_SETTING_START_TIME(cmd, (start_time_offset + guard_offset0));
		/* interval */
		SET_H2CCMD_MCC_TIME_SETTING_INTERVAL(cmd, interval);
		/* Early time to inform driver by C2H before switch channel */
		SET_H2CCMD_MCC_TIME_SETTING_EARLY_SWITCH_RPT(cmd, swchannel_early_time);
		/* Port0 sync from Port1, not support multi-port */
		SET_H2CCMD_MCC_TIME_SETTING_ORDER_BASE(cmd, tsf_bsae_port);
		SET_H2CCMD_MCC_TIME_SETTING_ORDER_SYNC(cmd, tsf_sync_port);
		SET_H2CCMD_MCC_TIME_SETTING_UPDATE(cmd, _TRUE);
		SET_H2CCMD_MCC_TIME_SETTING_ORDER0_DURATION(cmd, order0_duration);
	}

	rtw_hal_fill_h2c_cmd(padapter, H2C_MCC_TIME_SETTING, H2C_MCC_TIME_SETTING_LEN, cmd);
}

/**
 * rtw_hal_mcc_sw_status_check - check mcc swich channel status
 * @padapter: primary adapter
 */
void rtw_hal_mcc_sw_status_check(PADAPTER padapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *pmccobjpriv = &(dvobj->mcc_objpriv);
	struct pwrctrl_priv	*pwrpriv = dvobj_to_pwrctl(dvobj);
	struct mcc_adapter_priv *mccadapriv = NULL;
	_adapter *iface = NULL;
	u8 cur_cnt = 0, prev_cnt = 0, diff_cnt = 0, check_ret = _FAIL, threshold = 0;
	u8 policy_idx = pmccobjpriv->policy_index;
	u8 noa_enable = _FALSE;
	u8 i = 0;
	_irqL irqL;
	u8 ap_num = DEV_AP_NUM(dvobj);	

/* #define MCC_RESTART 1 */

	if (!MCC_EN(padapter))
		return;

	_enter_critical_mutex(&pmccobjpriv->mcc_mutex, NULL);

	if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_DOING_MCC)) {

		/* check noa enable or not */
		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			if (iface == NULL)
				continue;

			mccadapriv = &iface->mcc_adapterpriv;
			if (mccadapriv->role == MCC_ROLE_MAX)
				continue;
			
			if (iface->wdinfo.p2p_ps_mode == P2P_PS_NOA) {
				noa_enable = _TRUE;
				break;
			}
		}		

		if (!noa_enable && ap_num == 0)
			rtw_hal_mcc_update_parameter(padapter, _FALSE);

		threshold = pmccobjpriv->mcc_stop_threshold;

		if (pwrpriv->pwr_mode != PS_MODE_ACTIVE) {
			rtw_warn_on(1);
			RTW_INFO("PS mode is not active under mcc, force exit ps mode\n");
			LeaveAllPowerSaveModeDirect(padapter);
		}

		if (rtw_get_passing_time_ms(pmccobjpriv->mcc_launch_time) > 2000) {
			_enter_critical_bh(&pmccobjpriv->mcc_lock, &irqL);

			cur_cnt = pmccobjpriv->cur_mcc_success_cnt;
			prev_cnt = pmccobjpriv->prev_mcc_success_cnt;
			if (cur_cnt < prev_cnt)
				diff_cnt = (cur_cnt + 255) - prev_cnt;
			else
				diff_cnt = cur_cnt - prev_cnt;

			if (diff_cnt < threshold) {
				pmccobjpriv->mcc_tolerance_time--;
				RTW_INFO("%s: diff_cnt:%d, tolerance_time:%d\n",
					__func__, diff_cnt, pmccobjpriv->mcc_tolerance_time);
			} else
				pmccobjpriv->mcc_tolerance_time = MCC_TOLERANCE_TIME;

			pmccobjpriv->prev_mcc_success_cnt = pmccobjpriv->cur_mcc_success_cnt;

			if (pmccobjpriv->mcc_tolerance_time != 0)
				check_ret = _SUCCESS;

			_exit_critical_bh(&pmccobjpriv->mcc_lock, &irqL);

			if (check_ret != _SUCCESS) {
				RTW_INFO("============ MCC swich channel check fail (%d)=============\n", diff_cnt);
				/* restart MCC */
				#ifdef MCC_RESTART
					rtw_hal_set_mcc_setting(padapter, MCC_SETCMD_STATUS_STOP_DISCONNECT);
					rtw_hal_set_mcc_setting(padapter, MCC_SETCMD_STATUS_START_CONNECT);
				#endif /* MCC_RESTART */
			}
		} else {
			_enter_critical_bh(&pmccobjpriv->mcc_lock, &irqL);
			pmccobjpriv->prev_mcc_success_cnt = pmccobjpriv->cur_mcc_success_cnt;
			_exit_critical_bh(&pmccobjpriv->mcc_lock, &irqL);
		}

	}
	_exit_critical_mutex(&pmccobjpriv->mcc_mutex, NULL);
}

/**
 * rtw_hal_mcc_change_scan_flag - change scan flag under mcc
 *
 * MCC mode under sitesurvey goto AP channel to tx bcn & data
 * MCC mode under sitesurvey doesn't support TX data for station mode (FW not support)
 *
 * @padapter: the adapter to be change scan flag
 * @ch: pointer to rerurn ch
 * @bw: pointer to rerurn bw
 * @offset: pointer to rerurn offset
 */
u8 rtw_hal_mcc_change_scan_flag(PADAPTER padapter, u8 *ch, u8 *bw, u8 *offset)
{
	u8 need_ch_setting_union = _TRUE, i = 0, flags = 0, back_op = _FALSE;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct mcc_adapter_priv *mccadapriv = NULL;
	struct mlme_ext_priv *mlmeext = NULL;
	_adapter *iface = NULL;

	if (!MCC_EN(padapter))
		goto exit;

	if (!rtw_hal_check_mcc_status(padapter, MCC_STATUS_NEED_MCC))
		goto exit;

	/* disable PS_ANNC & TX_RESUME for all interface */
	/* ToDo: TX_RESUME by interface in SCAN_BACKING_OP */
	mlmeext = &padapter->mlmeextpriv;
	
	flags = mlmeext_scan_backop_flags(mlmeext);
	if (mlmeext_chk_scan_backop_flags(mlmeext, SS_BACKOP_PS_ANNC))
		flags &= ~SS_BACKOP_PS_ANNC;

	if (mlmeext_chk_scan_backop_flags(mlmeext, SS_BACKOP_TX_RESUME))
		flags &= ~SS_BACKOP_TX_RESUME;

	mlmeext_assign_scan_backop_flags(mlmeext, flags);

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (!iface)
			continue;

		mlmeext = &iface->mlmeextpriv;

		if (MLME_IS_GO(iface) || MLME_IS_AP(iface))
			back_op = _TRUE;
		else if (MLME_IS_GC(iface) && (iface != padapter))
			/* switch to another linked interface(GO) to receive beacon to avoid no beacon disconnect */
			back_op = _TRUE;
		else if (MLME_IS_STA(iface) && MLME_IS_ASOC(iface) && (iface != padapter))
			/* switch to another linked interface(STA) to receive beacon to avoid no beacon disconnect  */
			back_op = _TRUE;
		else {
			/* bypass non-linked/non-linking interface/scan interface */
			continue;
		}
		
		if (back_op) {
			*ch = mlmeext->cur_channel;
			*bw = mlmeext->cur_bwmode;
			*offset = mlmeext->cur_ch_offset;
			need_ch_setting_union = _FALSE;
		}
	}
exit:
	return need_ch_setting_union;
}

/**
 * rtw_hal_mcc_calc_tx_bytes_from_kernel - calculte tx bytes from kernel to check concurrent tx or not
 * @padapter: the adapter to be record tx bytes
 * @len: data len
 */
inline void rtw_hal_mcc_calc_tx_bytes_from_kernel(PADAPTER padapter, u32 len)
{
	struct mcc_adapter_priv *pmccadapriv = &padapter->mcc_adapterpriv;

	if (MCC_EN(padapter)) {
		if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_DOING_MCC)) {
			pmccadapriv->mcc_tx_bytes_from_kernel += len;
			if (0)
				RTW_INFO("%s(order:%d): mcc tx bytes from kernel:%lld\n"
					, __func__, pmccadapriv->order, pmccadapriv->mcc_tx_bytes_from_kernel);
		}
	}
}

/**
 * rtw_hal_mcc_calc_tx_bytes_to_port - calculte tx bytes to write port in order to flow crtl
 * @padapter: the adapter to be record tx bytes
 * @len: data len
 */
inline void rtw_hal_mcc_calc_tx_bytes_to_port(PADAPTER padapter, u32 len)
{
	if (MCC_EN(padapter)) {
		struct mcc_obj_priv *pmccobjpriv = &(adapter_to_dvobj(padapter)->mcc_objpriv);
		struct mcc_adapter_priv *pmccadapriv = &padapter->mcc_adapterpriv;

		if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_DOING_MCC)) {
			pmccadapriv->mcc_tx_bytes_to_port += len;
			if (0)
				RTW_INFO("%s(order:%d): mcc tx bytes to port:%d, mcc target tx bytes to port:%d\n"
					, __func__, pmccadapriv->order, pmccadapriv->mcc_tx_bytes_to_port
					, pmccadapriv->mcc_target_tx_bytes_to_port);
		}
	}
}

/**
 * rtw_hal_mcc_stop_tx_bytes_to_port - stop write port to hw or not
 * @padapter: the adapter to be stopped
 */
inline u8 rtw_hal_mcc_stop_tx_bytes_to_port(PADAPTER padapter)
{
	if (MCC_EN(padapter)) {
		struct mcc_obj_priv *pmccobjpriv = &(adapter_to_dvobj(padapter)->mcc_objpriv);
		struct mcc_adapter_priv *pmccadapriv = &padapter->mcc_adapterpriv;

		if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_DOING_MCC)) {
			if (pmccadapriv->mcc_tp_limit) {
				if (pmccadapriv->mcc_tx_bytes_to_port >= pmccadapriv->mcc_target_tx_bytes_to_port) {
					pmccadapriv->mcc_tx_stop = _TRUE;
					rtw_netif_stop_queue(padapter->pnetdev);
					return _TRUE;
				}
			}
		}
	}

	return _FALSE;
}

static void rtw_hal_mcc_assign_scan_flag(PADAPTER padapter, u8 scan_done)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct mcc_adapter_priv *mccadapriv = NULL;
	_adapter *iface = NULL;
	struct mlme_ext_priv *pmlmeext = NULL;
	u8 i = 0, flags;

	if (!MCC_EN(padapter))
		return;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (iface == NULL)
			continue;

		mccadapriv = &iface->mcc_adapterpriv;
		if (mccadapriv->role == MCC_ROLE_MAX)
			continue;

		pmlmeext = &iface->mlmeextpriv;
		if (is_client_associated_to_ap(iface)) {
			flags = mlmeext_scan_backop_flags_sta(pmlmeext);
			if (scan_done) {
				if (mlmeext_chk_scan_backop_flags_sta(pmlmeext, SS_BACKOP_EN)) {
					flags &= ~SS_BACKOP_EN;
					mlmeext_assign_scan_backop_flags_sta(pmlmeext, flags);
				}
			} else {
				if (!mlmeext_chk_scan_backop_flags_sta(pmlmeext, SS_BACKOP_EN)) {
					flags |= SS_BACKOP_EN;
					mlmeext_assign_scan_backop_flags_sta(pmlmeext, flags);
				}
			}

		}
	}
}

/**
 * rtw_hal_set_mcc_setting_scan_start - setting mcc under scan start
 * @padapter: the adapter to be setted
 * @ch_setting_changed: softap channel setting to be changed or not
 */
u8 rtw_hal_set_mcc_setting_scan_start(PADAPTER padapter)
{
	u8 ret = _FAIL;

	if (MCC_EN(padapter)) {
		struct mcc_obj_priv *pmccobjpriv = &(adapter_to_dvobj(padapter)->mcc_objpriv);

		_enter_critical_mutex(&pmccobjpriv->mcc_mutex, NULL);
		if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_NEED_MCC)) {
			if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_DOING_MCC)) {
				ret = rtw_hal_set_mcc_setting(padapter,  MCC_SETCMD_STATUS_STOP_SCAN_START);
				rtw_hal_mcc_assign_scan_flag(padapter, 0);
			}
		}
		_exit_critical_mutex(&pmccobjpriv->mcc_mutex, NULL);
	}

	return ret;
}

/**
 * rtw_hal_set_mcc_setting_scan_complete - setting mcc after scan commplete
 * @padapter: the adapter to be setted
 * @ch_setting_changed: softap channel setting to be changed or not
 */
u8 rtw_hal_set_mcc_setting_scan_complete(PADAPTER padapter)
{
	u8 ret = _FAIL;

	if (MCC_EN(padapter)) {
		struct mcc_obj_priv *pmccobjpriv = &(adapter_to_dvobj(padapter)->mcc_objpriv);

		_enter_critical_mutex(&pmccobjpriv->mcc_mutex, NULL);

		if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_NEED_MCC)) {
				rtw_hal_mcc_assign_scan_flag(padapter, 1);
				ret = rtw_hal_set_mcc_setting(padapter,  MCC_SETCMD_STATUS_START_SCAN_DONE);	
		}
		_exit_critical_mutex(&pmccobjpriv->mcc_mutex, NULL);
	}

	return ret;
}


/**
 * rtw_hal_set_mcc_setting_start_bss_network - setting mcc under softap start
 * @padapter: the adapter to be setted
 * @chbw_grouped: channel bw offset can not be allowed or not
 */
u8 rtw_hal_set_mcc_setting_start_bss_network(PADAPTER padapter, u8 chbw_allow)
{
	u8 ret = _FAIL;

	if (MCC_EN(padapter)) {
		/* channel bw offset can not be allowed, start MCC */
		if (chbw_allow == _FALSE) {
				struct mcc_obj_priv *pmccobjpriv = &(adapter_to_dvobj(padapter)->mcc_objpriv);

				rtw_hal_mcc_restore_iqk_val(padapter);
				_enter_critical_mutex(&pmccobjpriv->mcc_mutex, NULL);
				ret = rtw_hal_set_mcc_setting(padapter, MCC_SETCMD_STATUS_START_CONNECT);
				_exit_critical_mutex(&pmccobjpriv->mcc_mutex, NULL);
			}
		}

	return ret;
}

/**
 * rtw_hal_set_mcc_setting_disconnect - setting mcc under mlme disconnect(stop softap/disconnect from AP)
 * @padapter: the adapter to be setted
 */
u8 rtw_hal_set_mcc_setting_disconnect(PADAPTER padapter)
{
	u8 ret = _FAIL;

	if (MCC_EN(padapter)) {
		struct mcc_obj_priv *pmccobjpriv = &(adapter_to_dvobj(padapter)->mcc_objpriv);

		_enter_critical_mutex(&pmccobjpriv->mcc_mutex, NULL);
		if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_NEED_MCC)) {
			if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_DOING_MCC))
				ret = rtw_hal_set_mcc_setting(padapter,  MCC_SETCMD_STATUS_STOP_DISCONNECT);
		}
		_exit_critical_mutex(&pmccobjpriv->mcc_mutex, NULL);
	}

	return ret;
}

/**
 * rtw_hal_set_mcc_setting_join_done_chk_ch - setting mcc under join done
 * @padapter: the adapter to be checked
 */
u8 rtw_hal_set_mcc_setting_join_done_chk_ch(PADAPTER padapter)
{
	u8 ret = _FAIL;

	if (MCC_EN(padapter)) {
		struct mi_state mstate;

		rtw_mi_status_no_self(padapter, &mstate);

		if (MSTATE_STA_LD_NUM(&mstate) || MSTATE_STA_LG_NUM(&mstate) || MSTATE_AP_NUM(&mstate)) {
			bool chbw_allow = _TRUE;
			u8 u_ch, u_offset, u_bw;
			struct mlme_ext_priv *cur_mlmeext = &padapter->mlmeextpriv;
			struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);

			if (rtw_mi_get_ch_setting_union_no_self(padapter, &u_ch, &u_bw, &u_offset) <= 0) {
				dump_adapters_status(RTW_DBGDUMP , dvobj);
				rtw_warn_on(1);
			}

			RTW_INFO(FUNC_ADPT_FMT" union no self: %u,%u,%u\n"
				, FUNC_ADPT_ARG(padapter), u_ch, u_bw, u_offset);

			/* chbw_allow? */
			chbw_allow = rtw_is_chbw_grouped(cur_mlmeext->cur_channel
				, cur_mlmeext->cur_bwmode, cur_mlmeext->cur_ch_offset
					, u_ch, u_bw, u_offset);

			RTW_INFO(FUNC_ADPT_FMT" chbw_allow:%d\n"
				, FUNC_ADPT_ARG(padapter), chbw_allow);

			/* if chbw_allow = false, start MCC setting */
			if (chbw_allow == _FALSE) {
				struct mcc_obj_priv *pmccobjpriv = &dvobj->mcc_objpriv;

				rtw_hal_mcc_restore_iqk_val(padapter);
				_enter_critical_mutex(&pmccobjpriv->mcc_mutex, NULL);
				ret = rtw_hal_set_mcc_setting(padapter, MCC_SETCMD_STATUS_START_CONNECT);
				_exit_critical_mutex(&pmccobjpriv->mcc_mutex, NULL);
		}
	}
	}

	return ret;
}

/**
 * rtw_hal_set_mcc_setting_chk_start_clnt_join - check change channel under start clnt join
 * @padapter: the adapter to be checked
 * @ch: pointer to rerurn ch
 * @bw: pointer to rerurn bw
 * @offset: pointer to rerurn offset
 * @chbw_allow: allow to use adapter's channel setting
 */
u8 rtw_hal_set_mcc_setting_chk_start_clnt_join(PADAPTER padapter, u8 *ch, u8 *bw, u8 *offset, u8 chbw_allow)
{
	u8 ret = _FAIL;

	/* if chbw_allow = false under en_mcc = TRUE, we do not change channel related setting  */
	if (MCC_EN(padapter)) {
		/* restore union channel related setting to current channel related setting */
		if (chbw_allow == _FALSE) {
			struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

			/* issue null data to other interface connected to AP */
			rtw_hal_mcc_issue_null_data(padapter, chbw_allow, _TRUE);

			*ch = pmlmeext->cur_channel;
			*bw = pmlmeext->cur_bwmode;
			*offset = pmlmeext->cur_ch_offset;

			RTW_INFO(FUNC_ADPT_FMT" en_mcc:%d(%d,%d,%d,)\n"
				, FUNC_ADPT_ARG(padapter), MCC_EN(padapter)
				, *ch, *bw, *offset);
			ret = _SUCCESS;
		}
	}

	return ret;
}

static void rtw_hal_mcc_dump_noa_content(void *sel, PADAPTER padapter)
{
	struct mcc_adapter_priv *pmccadapriv = NULL;
	u8 *pos = NULL;
	pmccadapriv = &padapter->mcc_adapterpriv;
	/* last position for NoA attribute */
	pos = pmccadapriv->p2p_go_noa_ie + pmccadapriv->p2p_go_noa_ie_len;


	RTW_PRINT_SEL(sel, "\nStart to dump NoA Content\n");
	RTW_PRINT_SEL(sel, "NoA Counts:%d\n", *(pos - 13));
	RTW_PRINT_SEL(sel, "NoA Duration(TU):%d\n", (RTW_GET_LE32(pos - 12))/TU);
	RTW_PRINT_SEL(sel, "NoA Interval(TU):%d\n", (RTW_GET_LE32(pos - 8))/TU);
	RTW_PRINT_SEL(sel, "NoA Start time(microseconds):0x%02x\n", RTW_GET_LE32(pos - 4));
	RTW_PRINT_SEL(sel, "End to dump NoA Content\n");
}

static void mcc_dump_dbg_reg(void *sel, _adapter *adapter)
{
	struct mcc_obj_priv *mccobjpriv = adapter_to_mccobjpriv(adapter);
	HAL_DATA_TYPE *hal = GET_HAL_DATA(adapter);
	u8 i,j;
	_irqL irqL;

	_enter_critical_bh(&mccobjpriv->mcc_lock, &irqL);
	RTW_PRINT_SEL(sel, "current order=%d\n", mccobjpriv->current_order);
	_exit_critical_bh(&mccobjpriv->mcc_lock, &irqL);

	_enter_critical_mutex(&mccobjpriv->mcc_dbg_reg_mutex, NULL);
	for (i = 0; i < ARRAY_SIZE(mccobjpriv->dbg_reg); i++)
			RTW_PRINT_SEL(sel, "REG_0x%X:0x%08x\n", mccobjpriv->dbg_reg[i], mccobjpriv->dbg_reg_val[i]);

	for (i = 0; i < ARRAY_SIZE(mccobjpriv->dbg_rf_reg); i++) {
		for (j = 0; j < hal->NumTotalRFPath; j++)
			RTW_PRINT_SEL(sel, "RF_PATH_%d_REG_0x%X:0x%08x\n",
				j, mccobjpriv->dbg_rf_reg[i], mccobjpriv->dbg_rf_reg_val[i][j]);
	}
	_exit_critical_mutex(&mccobjpriv->mcc_dbg_reg_mutex, NULL);
}


void rtw_hal_dump_mcc_info(void *sel, struct dvobj_priv *dvobj)
{
	struct mcc_obj_priv *mccobjpriv = &(dvobj->mcc_objpriv);
	struct mcc_adapter_priv *mccadapriv = NULL;
	_adapter *iface = NULL, *pri_adapter = NULL;
	struct registry_priv *regpriv = NULL;
	HAL_DATA_TYPE *hal = NULL;
	u8 i = 0, j = 0;
	u64 tsf[MAX_MCC_NUM] = {0};

	/* regpriv is common for all adapter */
	pri_adapter = dvobj_get_primary_adapter(dvobj);
	hal = GET_HAL_DATA(pri_adapter);

	RTW_PRINT_SEL(sel, "**********************************************\n");
	RTW_PRINT_SEL(sel, "en_mcc:%d\n", MCC_EN(pri_adapter));
	RTW_PRINT_SEL(sel, "primary adapter("ADPT_FMT") duration:%d%c\n",
		ADPT_ARG(dvobj_get_primary_adapter(dvobj)), mccobjpriv->duration, 37);
	RTW_PRINT_SEL(sel, "runtime duration:%s\n", mccobjpriv->enable_runtime_duration ? "enable":"disable");
	RTW_PRINT_SEL(sel, "phydm offload:%s\n", mccobjpriv->mcc_phydm_offload ? "enable":"disable");

	if (rtw_hal_check_mcc_status(pri_adapter, MCC_STATUS_DOING_MCC)) {
		rtw_hal_mcc_rqt_tsf(pri_adapter, tsf);

		for (i = 0; i < MAX_MCC_NUM; i++) {
			iface = mccobjpriv->iface[i];
			if (!iface)
				continue;

			regpriv = &iface->registrypriv;
			mccadapriv = &iface->mcc_adapterpriv;

			if (mccadapriv) {
				u8 p2p_ps_mode = iface->wdinfo.p2p_ps_mode;

				RTW_PRINT_SEL(sel, "adapter mcc info:\n");
				RTW_PRINT_SEL(sel, "ifname:%s\n", ADPT_ARG(iface));
				RTW_PRINT_SEL(sel, "order:%d\n", mccadapriv->order);
				RTW_PRINT_SEL(sel, "duration:%d\n", mccadapriv->mcc_duration);
				RTW_PRINT_SEL(sel, "target tx bytes:%d\n", mccadapriv->mcc_target_tx_bytes_to_port);
				RTW_PRINT_SEL(sel, "current TP:%d\n", mccadapriv->mcc_tp);
				RTW_PRINT_SEL(sel, "mgmt queue macid:%d\n", mccadapriv->mgmt_queue_macid);
				RTW_PRINT_SEL(sel, "macid bitmap:0x%02x\n", mccadapriv->mcc_macid_bitmap);
				RTW_PRINT_SEL(sel, "P2P NoA:%s\n\n", p2p_ps_mode == P2P_PS_NOA ? "enable":"disable");
				RTW_PRINT_SEL(sel, "registry data:\n");
				RTW_PRINT_SEL(sel, "ap target tx TP(BW:20M):%d Mbps\n", regpriv->rtw_mcc_ap_bw20_target_tx_tp);
				RTW_PRINT_SEL(sel, "ap target tx TP(BW:40M):%d Mbps\n", regpriv->rtw_mcc_ap_bw40_target_tx_tp);
				RTW_PRINT_SEL(sel, "ap target tx TP(BW:80M):%d Mbps\n", regpriv->rtw_mcc_ap_bw80_target_tx_tp);
				RTW_PRINT_SEL(sel, "sta target tx TP(BW:20M):%d Mbps\n", regpriv->rtw_mcc_sta_bw20_target_tx_tp);
				RTW_PRINT_SEL(sel, "sta target tx TP(BW:40M ):%d Mbps\n", regpriv->rtw_mcc_sta_bw40_target_tx_tp);
				RTW_PRINT_SEL(sel, "sta target tx TP(BW:80M):%d Mbps\n", regpriv->rtw_mcc_sta_bw80_target_tx_tp);
				RTW_PRINT_SEL(sel, "single tx criteria:%d Mbps\n", regpriv->rtw_mcc_single_tx_cri);
				RTW_PRINT_SEL(sel, "HW TSF=0x%llx\n", tsf[mccadapriv->order]);
				if (MLME_IS_GO(iface))
					rtw_hal_mcc_dump_noa_content(sel, iface);
				RTW_PRINT_SEL(sel, "**********************************************\n");
			}
		}

		mcc_dump_dbg_reg(sel, pri_adapter);
	}

	#ifdef CONFIG_MCC_PHYDM_OFFLOAD
	RTW_PRINT_SEL(sel, "@@@@@@@@@@@@@@@@@@@@\n");
	rtw_hal_mcc_cfg_phydm(pri_adapter, MCC_CFG_PHYDM_DUMP, sel);
	RTW_PRINT_SEL(sel, "@@@@@@@@@@@@@@@@@@@@\n");
	#endif
	
	RTW_PRINT_SEL(sel, "------------------------------------------\n");
	RTW_PRINT_SEL(sel, "policy index:%d\n", mccobjpriv->policy_index);	
	RTW_PRINT_SEL(sel, "------------------------------------------\n");
	RTW_PRINT_SEL(sel, "define data:\n");
	RTW_PRINT_SEL(sel, "ap target tx TP(BW:20M):%d Mbps\n", MCC_AP_BW20_TARGET_TX_TP);
	RTW_PRINT_SEL(sel, "ap target tx TP(BW:40M):%d Mbps\n", MCC_AP_BW40_TARGET_TX_TP);
	RTW_PRINT_SEL(sel, "ap target tx TP(BW:80M):%d Mbps\n", MCC_AP_BW80_TARGET_TX_TP);
	RTW_PRINT_SEL(sel, "sta target tx TP(BW:20M):%d Mbps\n", MCC_STA_BW20_TARGET_TX_TP);
	RTW_PRINT_SEL(sel, "sta target tx TP(BW:40M):%d Mbps\n", MCC_STA_BW40_TARGET_TX_TP);
	RTW_PRINT_SEL(sel, "sta target tx TP(BW:80M):%d Mbps\n", MCC_STA_BW80_TARGET_TX_TP);
	RTW_PRINT_SEL(sel, "single tx criteria:%d Mbps\n", MCC_SINGLE_TX_CRITERIA);
	RTW_PRINT_SEL(sel, "------------------------------------------\n");
}

inline void update_mcc_mgntframe_attrib(_adapter *padapter, struct pkt_attrib *pattrib)
{
	if (MCC_EN(padapter)) {
		if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_DOING_MCC)) {
			/* use QSLT_MGNT to check mgnt queue or bcn queue */
			if (pattrib->qsel == QSLT_MGNT) {
				pattrib->mac_id = padapter->mcc_adapterpriv.mgmt_queue_macid;
				pattrib->qsel = QSLT_VO;
			}
		}
	}
}

inline u8 rtw_hal_mcc_link_status_chk(_adapter *padapter, const char *msg)
{
	u8 ret = _TRUE, i = 0;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	_adapter *iface;
	struct mlme_ext_priv *mlmeext;

	if (MCC_EN(padapter)) {
		if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_NEED_MCC)) {
			for (i = 0; i < dvobj->iface_nums; i++) {
				iface = dvobj->padapters[i];
				mlmeext = &iface->mlmeextpriv;
				if (mlmeext_scan_state(mlmeext) != SCAN_DISABLE) {
					#ifdef DBG_EXPIRATION_CHK
						RTW_INFO(FUNC_ADPT_FMT" don't enter %s under scan for MCC mode\n", FUNC_ADPT_ARG(padapter), msg);
					#endif
					ret = _FALSE;
					goto exit;
				}
			}
		}
	}

exit:
	return ret;
}

void rtw_hal_mcc_issue_null_data(_adapter *padapter, u8 chbw_allow, u8 ps_mode)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	_adapter *iface = NULL;
	systime start = rtw_get_current_time();
	u8 i = 0;

	if (!MCC_EN(padapter))
		return;

	if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_DOING_MCC))
		return;

	if (chbw_allow == _TRUE)
		return;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		/* issue null data to inform ap station will leave */
		if (is_client_associated_to_ap(iface)) {
			struct mlme_ext_priv *mlmeext = &iface->mlmeextpriv;
			struct mlme_ext_info *mlmeextinfo = &mlmeext->mlmext_info;
			u8 ch = mlmeext->cur_channel;
			u8 bw = mlmeext->cur_bwmode;
			u8 offset = mlmeext->cur_ch_offset;
			struct sta_info *sta = rtw_get_stainfo(&iface->stapriv, get_my_bssid(&(mlmeextinfo->network)));

			if (!sta)
				continue;

			set_channel_bwmode(iface, ch, offset, bw);

			if (ps_mode)
				rtw_hal_macid_sleep(iface, sta->cmn.mac_id);
			else
				rtw_hal_macid_wakeup(iface, sta->cmn.mac_id);

			issue_nulldata(iface, NULL, ps_mode, 3, 50);
		}
	}
	RTW_INFO("%s(%d ms)\n", __func__, rtw_get_passing_time_ms(start));
}

u8 *rtw_hal_mcc_append_go_p2p_ie(PADAPTER padapter, u8 *pframe, u32 *len)
{
	struct mcc_adapter_priv *pmccadapriv = &padapter->mcc_adapterpriv;

	if (!MCC_EN(padapter))
		return pframe;
	
	if (!rtw_hal_check_mcc_status(padapter, MCC_STATUS_DOING_MCC))
		return pframe;

	if (pmccadapriv->p2p_go_noa_ie_len == 0)
		return pframe;

	_rtw_memcpy(pframe, pmccadapriv->p2p_go_noa_ie, pmccadapriv->p2p_go_noa_ie_len);
	*len = *len + pmccadapriv->p2p_go_noa_ie_len;

	return pframe + pmccadapriv->p2p_go_noa_ie_len;
}

void rtw_hal_dump_mcc_policy_table(void *sel)
{
	u8 idx = 0;
	RTW_PRINT_SEL(sel, "duration\t,tsf sync offset\t,start time offset\t,interval\t,guard offset0\t,guard offset1\n");

	for (idx = 0; idx < mcc_max_policy_num; idx ++) {
		RTW_PRINT_SEL(sel, "%d\t\t,%d\t\t\t,%d\t\t\t,%d\t\t,%d\t\t,%d\n"
			, mcc_switch_channel_policy_table[idx][MCC_DURATION_IDX]
			, mcc_switch_channel_policy_table[idx][MCC_TSF_SYNC_OFFSET_IDX]
			, mcc_switch_channel_policy_table[idx][MCC_START_TIME_OFFSET_IDX]
			, mcc_switch_channel_policy_table[idx][MCC_INTERVAL_IDX]
			, mcc_switch_channel_policy_table[idx][MCC_GUARD_OFFSET0_IDX]
			, mcc_switch_channel_policy_table[idx][MCC_GUARD_OFFSET1_IDX]);
	}
}

void rtw_hal_mcc_update_macid_bitmap(PADAPTER padapter, int mac_id, u8 add)
{
	struct mcc_adapter_priv *pmccadapriv = &padapter->mcc_adapterpriv;

	if (!MCC_EN(padapter))
		return;

	if (!rtw_hal_check_mcc_status(padapter, MCC_STATUS_DOING_MCC))
		return;

	if (pmccadapriv->role == MCC_ROLE_GC || pmccadapriv->role == MCC_ROLE_STA)
		return;

	if (mac_id < 0) {
		RTW_WARN("%s: mac_id < 0(%d)\n", __func__, mac_id);
		return;
	}

	RTW_INFO(ADPT_FMT" %s macid=%d, ori mcc_macid_bitmap=0x%08x\n"
		, ADPT_ARG(padapter), add ? "add" : "clear"
		, mac_id, pmccadapriv->mcc_macid_bitmap);

	if (add) {
		#ifdef CONFIG_MCC_PHYDM_OFFLOAD
		rtw_hal_mcc_cfg_phydm(padapter, MCC_CFG_PHYDM_ADD_CLIENT, &mac_id);
		#endif
		pmccadapriv->mcc_macid_bitmap |= BIT(mac_id);
	} else {
		#ifdef CONFIG_MCC_PHYDM_OFFLOAD
		rtw_hal_mcc_cfg_phydm(padapter, MCC_CFG_PHYDM_REMOVE_CLIENT, &mac_id);
		#endif
		pmccadapriv->mcc_macid_bitmap &= ~(BIT(mac_id));
	}
	rtw_hal_set_mcc_macid_cmd(padapter);
}

void rtw_hal_mcc_process_noa(PADAPTER padapter)
{
	struct wifidirect_info *pwdinfo = &(padapter->wdinfo);
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *pmccobjpriv = &(dvobj->mcc_objpriv);

	if (!MCC_EN(padapter))
		return;

	if (!rtw_hal_check_mcc_status(padapter, MCC_STATUS_DOING_MCC))
		return;

	if (!MLME_IS_GC(padapter))
		return;

	switch(pwdinfo->p2p_ps_mode) {
	case P2P_PS_NONE:
		RTW_INFO("[MCC] Disable NoA under MCC\n");
		rtw_hal_mcc_update_parameter(padapter, _TRUE);
		break;
	case P2P_PS_NOA:
		RTW_INFO("[MCC] Enable NoA under MCC\n");
		break;
	default:
		break;

	}
}

void rtw_hal_mcc_parameter_init(PADAPTER padapter)
{
	if (!padapter->registrypriv.en_mcc)
		return;

	if (is_primary_adapter(padapter)) {
		SET_MCC_EN_FLAG(padapter, padapter->registrypriv.en_mcc);
		SET_MCC_DURATION(padapter, padapter->registrypriv.rtw_mcc_duration);
		SET_MCC_RUNTIME_DURATION(padapter, padapter->registrypriv.rtw_mcc_enable_runtime_duration);
		SET_MCC_PHYDM_OFFLOAD(padapter, padapter->registrypriv.rtw_mcc_phydm_offload);
	}
}


static u8 set_mcc_duration_hdl(PADAPTER adapter, const u8 *val)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct mcc_obj_priv *mccobjpriv = &(dvobj->mcc_objpriv);
	_adapter *iface = NULL;
	u8 duration = 50;
	u8 ret = _SUCCESS, noa_enable = _FALSE, i = 0;
	enum mcc_duration_setting type;

	if (!mccobjpriv->enable_runtime_duration)
		goto exit;

#ifdef CONFIG_P2P_PS
	/* check noa enable or not */
	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (iface->wdinfo.p2p_ps_mode == P2P_PS_NOA) {
			noa_enable = _TRUE;
			break;
		}
	}
#endif /* CONFIG_P2P_PS */

	type = val[0];
	duration = val[1];

	if (type == MCC_DURATION_MAPPING) {
		switch (duration) {
			/* 0 = fair scheduling */
			case 0:
				mccobjpriv->duration= 40;
				mccobjpriv->policy_index = 2;
				mccobjpriv->mchan_sched_mode = MCC_FAIR_SCHEDULE;
				break;
			/* 1 = favor STA */
			case 1:
				mccobjpriv->duration= 70;
				mccobjpriv->policy_index = 1;
				mccobjpriv->mchan_sched_mode = MCC_FAVOR_STA;
				break;
			/* 2 = favor P2P*/
			case 2:
			default:
				mccobjpriv->duration= 30;
				mccobjpriv->policy_index = 0;
				mccobjpriv->mchan_sched_mode = MCC_FAVOR_P2P;
				break;
		}
	} else {
		mccobjpriv->duration = duration;
		rtw_hal_mcc_update_policy_table(adapter);
	}

	/* only update sw parameter under MCC 
	    it will be force update during */
	if (noa_enable)
		goto exit;

	if (rtw_hal_check_mcc_status(adapter, MCC_STATUS_DOING_MCC))
		rtw_hal_mcc_update_parameter(adapter, _TRUE);
exit:
	return ret;
}

u8 rtw_set_mcc_duration_cmd(_adapter *adapter, u8 type, u8 val)
{
	struct cmd_obj *cmdobj;
	struct drvextra_cmd_parm *pdrvextra_cmd_parm;
	struct cmd_priv *pcmdpriv = &adapter->cmdpriv;
	u8 *buf = NULL;
	u8 sz = 2;
	u8 res = _SUCCESS;

	
	cmdobj = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (cmdobj == NULL) {
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
	if (pdrvextra_cmd_parm == NULL) {
		rtw_mfree((u8 *)cmdobj, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}

	buf = rtw_zmalloc(sizeof(u8) * sz);
	if (buf == NULL) {
		rtw_mfree((u8 *)cmdobj, sizeof(struct cmd_obj));
		rtw_mfree((u8 *)pdrvextra_cmd_parm, sizeof(struct drvextra_cmd_parm));
		res = _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = MCC_CMD_WK_CID;
	pdrvextra_cmd_parm->type = MCC_SET_DURATION_WK_CID;
	pdrvextra_cmd_parm->size = sz;
	pdrvextra_cmd_parm->pbuf = buf;

	_rtw_memcpy(buf, &type, 1);
	_rtw_memcpy(buf + 1, &val, 1);

	init_h2fwcmd_w_parm_no_rsp(cmdobj, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));
	res = rtw_enqueue_cmd(pcmdpriv, cmdobj);

exit:
	return res;
}

#ifdef CONFIG_MCC_PHYDM_OFFLOAD
static u8 mcc_phydm_offload_enable_hdl(_adapter *adapter, const u8 *val)
{
	struct mcc_obj_priv *mccobjpriv =  adapter_to_mccobjpriv(adapter);
	u8 ret = _SUCCESS;
	u8 enable = *val;

	/*only modify driver parameter during non-mcc status */
	if (!rtw_hal_check_mcc_status(adapter, MCC_STATUS_DOING_MCC)) {
		mccobjpriv->mcc_phydm_offload = enable;
	} else {
		/*modify both driver & phydm parameter during mcc status */
		mccobjpriv->mcc_phydm_offload = enable;
		rtw_hal_mcc_cfg_phydm(adapter, MCC_CFG_PHYDM_OFFLOAD, &mccobjpriv->mcc_phydm_offload);
	}

	RTW_INFO("[MCC] phydm offload enable hdl(%d)\n", mccobjpriv->mcc_phydm_offload);

	return ret;
}

u8 rtw_set_mcc_phydm_offload_enable_cmd(_adapter *adapter, u8 enable, u8 enqueue)
{
	u8 res = _SUCCESS;

	if (enqueue) {
		struct cmd_obj *cmdobj;
		struct drvextra_cmd_parm *pdrvextra_cmd_parm;
		struct cmd_priv *pcmdpriv = &adapter->cmdpriv;
		u8 *mcc_phydm_offload_enable = NULL;

		
		cmdobj = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
		if (cmdobj == NULL) {
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm = (struct drvextra_cmd_parm *)rtw_zmalloc(sizeof(struct drvextra_cmd_parm));
		if (pdrvextra_cmd_parm == NULL) {
			rtw_mfree((u8 *)cmdobj, sizeof(struct cmd_obj));
			res = _FAIL;
			goto exit;
		}

		mcc_phydm_offload_enable = rtw_zmalloc(sizeof(u8));
		if (mcc_phydm_offload_enable == NULL) {
			rtw_mfree((u8 *)cmdobj, sizeof(struct cmd_obj));
			rtw_mfree((u8 *)pdrvextra_cmd_parm, sizeof(struct drvextra_cmd_parm));
			res = _FAIL;
			goto exit;
		}

		pdrvextra_cmd_parm->ec_id = MCC_CMD_WK_CID;
		pdrvextra_cmd_parm->type = MCC_SET_PHYDM_OFFLOAD_WK_CID;
		pdrvextra_cmd_parm->size = 1;
		pdrvextra_cmd_parm->pbuf = mcc_phydm_offload_enable;

		_rtw_memcpy(mcc_phydm_offload_enable, &enable, 1);
		init_h2fwcmd_w_parm_no_rsp(cmdobj, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));
		res = rtw_enqueue_cmd(pcmdpriv, cmdobj);
	} else {
		mcc_phydm_offload_enable_hdl(adapter, &enable);
	}

exit:
	return res;
}
#endif

u8 rtw_mcc_cmd_hdl(_adapter *adapter, u8 type, const u8 *val)
{
	struct mcc_obj_priv *mccobjpriv =  adapter_to_mccobjpriv(adapter);
	u8 ret = _SUCCESS;

	switch (type) {
	case MCC_SET_DURATION_WK_CID:
		set_mcc_duration_hdl(adapter, val);
		break;
	case MCC_GET_DBG_REG_WK_CID:
		mcc_get_reg_hdl(adapter, val);
		break;
	#ifdef CONFIG_MCC_PHYDM_OFFLOAD
	case MCC_SET_PHYDM_OFFLOAD_WK_CID:
		mcc_phydm_offload_enable_hdl(adapter, val);
		break;
	#endif
	default:
		RTW_ERR("[MCC] rtw_mcc_cmd_hdl fail(%d)\n", type);
		break;
	}



	return ret;
}

#endif /* CONFIG_MCC_MODE */
