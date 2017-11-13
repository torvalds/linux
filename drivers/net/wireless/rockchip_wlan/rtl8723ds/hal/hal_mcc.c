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

#define MCC_DURATION_IDX 0
#define MCC_TSF_SYNC_OFFSET_IDX 1
#define MCC_START_TIME_OFFSET_IDX 2
#define MCC_INTERVAL_IDX 3
#define MCC_GUARD_OFFSET0_IDX 4
#define MCC_GUARD_OFFSET1_IDX 5
#define TU 1024 /* 1 TU equals 1024 microseconds */
/* port 1 druration, TSF sync offset, start time offset, interval (unit:TU (1024 microseconds))*/
u8 mcc_switch_channel_policy_table[][6]={
	{35, 50, 30, 100, 0, 0},
	{19, 50, 40, 100, 2, 2},
	{25, 50, 30, 100, 5, 5},
};

const int mcc_max_policy_num = sizeof(mcc_switch_channel_policy_table) /sizeof(u8) /6;

static void dump_iqk_val_table(PADAPTER padapter)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	struct hal_iqk_reg_backup *iqk_reg_backup = pHalData->iqk_reg_backup;
	u8 total_rf_path = pHalData->NumTotalRFPath;
	u8 rf_path_idx = 0;
	u8 backup_chan_idx = 0;
	u8 backup_reg_idx = 0;

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
	u32 noa_duration = 0x20;
	u32 noa_interval = 0x64;
	u8 noa_index = 0;
	u8 mcc_policy_idx = 0;

	mcc_policy_idx = pmccobjpriv->policy_index;
	noa_duration = mcc_switch_channel_policy_table[mcc_policy_idx][MCC_DURATION_IDX];
	noa_interval = mcc_switch_channel_policy_table[mcc_policy_idx][MCC_INTERVAL_IDX];

	/* P2P OUI(4 bytes) */
	_rtw_memcpy(p2p_noa_attr_ie, P2P_OUI, 4);
	p2p_noa_attr_len = p2p_noa_attr_len + 4;

	/* attrute ID(1 byte) */
	p2p_noa_attr_ie[p2p_noa_attr_len] = P2P_ATTR_NOA;
	p2p_noa_attr_len = p2p_noa_attr_len + 1;
	
	/* attrute length(2 bytes) length = noa_desc_num*13 + 2 */
	RTW_PUT_LE16(p2p_noa_attr_ie + p2p_noa_attr_len, (noa_desc_num*13 + 2));
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
	RTW_PUT_LE32(p2p_noa_attr_ie + p2p_noa_attr_len, (noa_duration * TU));
	p2p_noa_attr_len = p2p_noa_attr_len + 4;

	/* NoA Interval (4 bytes) unit: microseconds */
	RTW_PUT_LE32(p2p_noa_attr_ie + p2p_noa_attr_len, (noa_interval * TU));
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
	u8 *pos = NULL;


	/* no noa attribute, build it */
	if (pmccadapriv->p2p_go_noa_ie_len == 0)
		rtw_hal_mcc_build_p2p_noa_attr(padapter, pmccadapriv->p2p_go_noa_ie, &pmccadapriv->p2p_go_noa_ie_len);
	else {
	/* has noa attribut, modify it */
		/* update index */
		pos = pmccadapriv->p2p_go_noa_ie + pmccadapriv->p2p_go_noa_ie_len - 15;
		/* 0~255 */
		(*pos) = ((*pos) + 1) % 256;
		if (1)
			RTW_INFO("indxe:%d\n", (*pos));

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
		u8 i = 0;
		RTW_INFO("p2p_go_noa_ie_len:%d\n", pmccadapriv->p2p_go_noa_ie_len);
		
		for (i = 0;i < pmccadapriv->p2p_go_noa_ie_len; i++) {
			if ((i+1)%8 != 0)
				printk("0x%02x ", pmccadapriv->p2p_go_noa_ie[i]);
			else
				printk("0x%02x\n", pmccadapriv->p2p_go_noa_ie[i]);
		}
		printk("\n");
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
	u8 i = 0;

	rtw_hal_get_hwreg(padapter, HW_VAR_CH_SW_NEED_TO_TAKE_CARE_IQK_INFO, &take_care_iqk);
	if (take_care_iqk == _TRUE && MCC_EN(padapter)) {
		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			if (iface == NULL)
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

void rtw_hal_mcc_update_switch_channel_policy_table(PADAPTER padapter)
{
	struct registry_priv *registry_par = &padapter->registrypriv;
	u8 idx = 0;

	if (registry_par->rtw_mcc_policy_table_idx < 0)
		return;

	if (registry_par->rtw_mcc_policy_table_idx >= mcc_max_policy_num) {
		RTW_INFO("[MCC] mcc_policy_table_idx error, do not update policy table\n");
		return;
	}

	idx = registry_par->rtw_mcc_policy_table_idx;
	
	if (registry_par->rtw_mcc_duration > 0)
		mcc_switch_channel_policy_table[idx][MCC_DURATION_IDX] = registry_par->rtw_mcc_duration;

	if (registry_par->rtw_mcc_tsf_sync_offset > 0)
		mcc_switch_channel_policy_table[idx][MCC_TSF_SYNC_OFFSET_IDX] = registry_par->rtw_mcc_tsf_sync_offset;

	if (registry_par->rtw_mcc_start_time_offset > 0)
		mcc_switch_channel_policy_table[idx][MCC_START_TIME_OFFSET_IDX] = registry_par->rtw_mcc_start_time_offset;

	if (registry_par->rtw_mcc_interval > 0)
		mcc_switch_channel_policy_table[idx][MCC_INTERVAL_IDX] = registry_par->rtw_mcc_interval;

	if (registry_par->rtw_mcc_guard_offset0 >= 0)
		mcc_switch_channel_policy_table[idx][MCC_GUARD_OFFSET0_IDX] = registry_par->rtw_mcc_guard_offset0;

	if (registry_par->rtw_mcc_guard_offset1 >= 0)
		mcc_switch_channel_policy_table[idx][MCC_GUARD_OFFSET1_IDX] = registry_par->rtw_mcc_guard_offset1;

}

static void rtw_hal_config_mcc_switch_channel_setting(PADAPTER padapter)
{
	struct mcc_adapter_priv *pmccadapriv = &padapter->mcc_adapterpriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *pmccobjpriv = &(dvobj->mcc_objpriv);
	struct registry_priv *registry_par = &padapter->registrypriv;
	u8 interval = pmlmepriv->cur_network.network.Configuration.BeaconPeriod;
	u8 i = 0;
	s8 mcc_policy_idx = 0;

	rtw_hal_mcc_update_switch_channel_policy_table(padapter);
	mcc_policy_idx = registry_par->rtw_mcc_policy_table_idx;

	if (mcc_policy_idx < 0 || mcc_policy_idx >= mcc_max_policy_num) {
		pmccobjpriv->policy_index = 0;
		RTW_INFO("[MCC] can't find table(%d,%d,%d), use default policy(%d)\n"
			, pmccobjpriv->duration, interval, mcc_policy_idx, pmccobjpriv->policy_index);
	} else
		pmccobjpriv->policy_index = mcc_policy_idx;

	RTW_INFO("[MCC] policy(%d): %d,%d,%d,%d,%d,%d\n"
		, pmccobjpriv->policy_index
		, mcc_switch_channel_policy_table[pmccobjpriv->policy_index][MCC_DURATION_IDX]
		, mcc_switch_channel_policy_table[pmccobjpriv->policy_index][MCC_TSF_SYNC_OFFSET_IDX]
		, mcc_switch_channel_policy_table[pmccobjpriv->policy_index][MCC_START_TIME_OFFSET_IDX]
		, mcc_switch_channel_policy_table[pmccobjpriv->policy_index][MCC_INTERVAL_IDX]
		, mcc_switch_channel_policy_table[pmccobjpriv->policy_index][MCC_GUARD_OFFSET0_IDX]
		, mcc_switch_channel_policy_table[pmccobjpriv->policy_index][MCC_GUARD_OFFSET1_IDX]);

}

static void rtw_hal_config_mcc_role_setting(PADAPTER padapter)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *pmccobjpriv = &(pdvobjpriv->mcc_objpriv);
	struct mcc_adapter_priv *pmccadapriv = &padapter->mcc_adapterpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct wlan_network *cur_network = &(pmlmepriv->cur_network);
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta = NULL;
	struct registry_priv *preg = &padapter->registrypriv;
	u8 policy_index = 0;
	u8 mcc_duration = 0;
	u8 mcc_interval = 0;

	policy_index = pmccobjpriv->policy_index;
	mcc_duration = mcc_switch_channel_policy_table[pmccobjpriv->policy_index][MCC_DURATION_IDX]
		- mcc_switch_channel_policy_table[pmccobjpriv->policy_index][MCC_GUARD_OFFSET0_IDX]
			- mcc_switch_channel_policy_table[pmccobjpriv->policy_index][MCC_GUARD_OFFSET1_IDX];
	mcc_interval = mcc_switch_channel_policy_table[pmccobjpriv->policy_index][MCC_INTERVAL_IDX];

	/* GO/AP is 1nd order  GC/STA is 2nd order */
	switch (pmccadapriv->role) {
	case MCC_ROLE_STA:
	case MCC_ROLE_GC:
		pmccadapriv->order = 1;
		pmccadapriv->mcc_duration = mcc_duration;

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

		/* assign used mac to avoid affecting RA */
		pmccadapriv->mgmt_queue_macid = MCC_ROLE_STA_GC_MGMT_QUEUE_MACID;

		psta = rtw_get_stainfo(pstapriv, cur_network->network.MacAddress);
		if (psta) {
			/* combine AP/GO macid and mgmt queue macid to bitmap */
			pmccadapriv->mcc_macid_bitmap = BIT(psta->cmn.mac_id) | BIT(pmccadapriv->mgmt_queue_macid);
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


		psta = rtw_get_bcmc_stainfo(padapter);

		if (psta != NULL)
			pmccadapriv->mgmt_queue_macid = psta->cmn.mac_id;
		else {
			pmccadapriv->mgmt_queue_macid = MCC_ROLE_SOFTAP_GO_MGMT_QUEUE_MACID;
			RTW_INFO(FUNC_ADPT_FMT":bcmc station is NULL, use macid %d\n"
				, FUNC_ADPT_ARG(padapter), pmccadapriv->mgmt_queue_macid);
		}

		/* combine client macid and mgmt queue macid to bitmap */
		pmccadapriv->mcc_macid_bitmap = (0xff << 8) | BIT(pmccadapriv->mgmt_queue_macid);
		break;
	default:
		RTW_INFO("Unknown role\n");
		rtw_warn_on(1);
		break;
	}

	pmccobjpriv->iface[pmccadapriv->order] = padapter;
	RTW_INFO(FUNC_ADPT_FMT": order:%d, role:%d, mcc duration:%d, target tx bytes:%d, mgmt queue macid:%d, bitmap:0x%02x\n"
		, FUNC_ADPT_ARG(padapter), pmccadapriv->order, pmccadapriv->role, pmccadapriv->mcc_duration
			, pmccadapriv->mcc_target_tx_bytes_to_port, pmccadapriv->mgmt_queue_macid, pmccadapriv->mcc_macid_bitmap);
}

static void rtw_hal_clear_mcc_macid(PADAPTER padapter)
{
	u16 media_status_rpt;
	struct mcc_adapter_priv *pmccadapriv = &padapter->mcc_adapterpriv;

	switch (pmccadapriv->role) {
	case MCC_ROLE_STA:
	case MCC_ROLE_GC:
		break;
	case MCC_ROLE_AP:
	case MCC_ROLE_GO:
	/* nothing to do */
		break;
	default:
		RTW_INFO("Unknown role\n");
		rtw_warn_on(1);
		break;
	}
}
static u8 rtw_hal_decide_mcc_role(PADAPTER padapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	_adapter *iface = NULL;
	struct mcc_adapter_priv *pmccadapriv = NULL;
	struct wifidirect_info *pwdinfo = NULL;
	struct mlme_priv *pmlmepriv = NULL;
	u8 ret = _SUCCESS, i = 0;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (iface == NULL)
			continue;

		pmccadapriv = &iface->mcc_adapterpriv;

		if (MLME_IS_GO(iface))
			pmccadapriv->role = MCC_ROLE_GO;
		else if (MLME_IS_AP(iface))
			pmccadapriv->role = MCC_ROLE_AP;
		else if (MLME_IS_GC(iface))
			pmccadapriv->role = MCC_ROLE_GC;
		else if (MLME_IS_STA(iface))
			pmccadapriv->role = MCC_ROLE_STA;
		else {
			pwdinfo = &iface->wdinfo;
			pmlmepriv = &iface->mlmepriv;

			RTW_INFO(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(iface));
			RTW_INFO("Unknown:P2P state:%d, mlme state:0x%2x, mlmext info state:0x%02x\n",
				pwdinfo->role, pmlmepriv->fw_state, iface->mlmeextpriv.mlmext_info.state);
			rtw_warn_on(1);
			ret =  _FAIL;
			goto exit;
		}

		if (ret == _SUCCESS)
			rtw_hal_config_mcc_role_setting(iface);
	}

exit:
	return ret;
}

static void rtw_hal_init_mcc_parameter(PADAPTER padapter)
{
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

u8 rtw_hal_dl_mcc_fw_rsvd_page(_adapter *adapter, u8 *pframe, u16 *index,
	u8 tx_desc, u32 page_size, u8 *page_num, u32 *total_pkt_len,
		RSVDPAGE_LOC *rsvd_page_loc)
{
	u32 len = 0;
	_adapter *iface = NULL;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct mcc_obj_priv *pmccobjpriv = &(dvobj->mcc_objpriv);
	struct mlme_ext_info *pmlmeinfo = NULL;
	struct mlme_ext_priv *pmlmeext = NULL;
	u8 ret = _SUCCESS, i = 0, order = 0, CurtPktPageNum = 0;
	u8 bssid[ETH_ALEN] = {0};

	/* check proccess mcc start setting */
	if (!rtw_hal_check_mcc_status(adapter, MCC_STATUS_PROCESS_MCC_START_SETTING)) {
		ret = _FAIL;
		goto exit;
	}

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (iface == NULL)
			continue;

		order = iface->mcc_adapterpriv.order;
		dvobj->mcc_objpriv.mcc_loc_rsvd_paga[order] = *page_num;

		switch (iface->mcc_adapterpriv.role) {
		case MCC_ROLE_STA:
		case MCC_ROLE_GC:
			/* Build NULL DATA */
			RTW_INFO("LocNull(order:%d): %d\n"
				, order, dvobj->mcc_objpriv.mcc_loc_rsvd_paga[order]);
			len = 0;
			pmlmeext = &iface->mlmeextpriv;
			pmlmeinfo = &pmlmeext->mlmext_info;

			_rtw_memcpy(bssid, get_my_bssid(&pmlmeinfo->network), ETH_ALEN);
			rtw_hal_construct_NullFunctionData(iface
				, &pframe[*index], &len, bssid, _FALSE, 0, 0, _FALSE);
			rtw_hal_fill_fake_txdesc(iface, &pframe[*index-tx_desc],
				len, _FALSE, _FALSE, _FALSE);

			CurtPktPageNum = (u8)PageNum(tx_desc + len, page_size);
			*page_num += CurtPktPageNum;
			*index += (CurtPktPageNum * page_size);
			*total_pkt_len = *index + len;
			break;
		case MCC_ROLE_AP:
			/* Bulid CTS */
			RTW_INFO("LocCTS(order:%d): %d\n"
				, order, dvobj->mcc_objpriv.mcc_loc_rsvd_paga[order]);

			len = 0;
			rtw_hal_construct_CTS(iface, &pframe[*index], &len);
			rtw_hal_fill_fake_txdesc(iface, &pframe[*index-tx_desc],
				len, _FALSE, _FALSE, _FALSE);

			CurtPktPageNum = (u8)PageNum(tx_desc + len, page_size);
			*page_num += CurtPktPageNum;
			*index += (CurtPktPageNum * page_size);
			*total_pkt_len = *index + len;
			break;
		case MCC_ROLE_GO:
		/* To DO */
			break;
		}
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
	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = pmccobjpriv->iface[i];
		pmccadapriv = &iface->mcc_adapterpriv;
		if (pmccadapriv->role == MCC_ROLE_AP
			|| pmccadapriv->role == MCC_ROLE_GO)
			tx_beacon_hdl(iface, NULL);
	}
}

static void rtw_hal_set_mcc_rsvdpage_cmd(_adapter *padapter)
{
	u8 cmd[H2C_MCC_LOCATION_LEN] = {0}, i = 0, order = 0;
	_adapter *iface = NULL;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *pmccobjpriv = &(dvobj->mcc_objpriv);


	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (iface == NULL)
			continue;

		order = iface->mcc_adapterpriv.order;
		if (order >= H2C_MCC_LOCATION_LEN) {
			RTW_INFO(FUNC_ADPT_FMT" only support 3 interface at most(%d)\n"
				, FUNC_ADPT_ARG(padapter), order);
			continue;
		}

		SET_H2CCMD_MCC_RSVDPAGE_LOC((cmd + order), (pmccobjpriv->mcc_loc_rsvd_paga[order]));
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

static void rtw_hal_set_mcc_noa_cmd(PADAPTER padapter)
{
	struct mcc_adapter_priv *pmccadapriv = &padapter->mcc_adapterpriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *pmccobjpriv = &(dvobj->mcc_objpriv);
	u8 cmd[H2C_MCC_NOA_PARAM_LEN] = {0};
	u8 policy_idx = pmccobjpriv->policy_index;
	u8 noa_fw_eable = 1;
	u8 noa_tsf_sync_offset = mcc_switch_channel_policy_table[policy_idx][MCC_TSF_SYNC_OFFSET_IDX];
	u8 noa_start_time_offset = mcc_switch_channel_policy_table[policy_idx][MCC_START_TIME_OFFSET_IDX];
	u8 noa_interval = mcc_switch_channel_policy_table[policy_idx][MCC_INTERVAL_IDX];
	u8 guard_offset0 = mcc_switch_channel_policy_table[policy_idx][MCC_GUARD_OFFSET0_IDX];
	u8 guard_offset1 = mcc_switch_channel_policy_table[policy_idx][MCC_GUARD_OFFSET1_IDX];
	u8 swchannel_early_time = MCC_SWCH_FW_EARLY_TIME;
	u8 i = 0;

	/* FW set NOA enable */
	SET_H2CCMD_MCC_NOA_FW_EN(cmd, noa_fw_eable);
	/* TSF Sync offset */
	SET_H2CCMD_MCC_NOA_TSF_SYNC_OFFSET(cmd, noa_tsf_sync_offset);
	/* NoA start time offset */
	SET_H2CCMD_MCC_NOA_START_TIME(cmd, (noa_start_time_offset + guard_offset0));
	/* NoA interval */
	SET_H2CCMD_MCC_NOA_INTERVAL(cmd, noa_interval);
	/* Early time to inform driver by C2H before switch channel */
	SET_H2CCMD_MCC_EARLY_TIME(cmd, swchannel_early_time);

#ifdef CONFIG_MCC_MODE_DEBUG
	RTW_INFO("=========================\n");
	RTW_INFO("NoA:\n");
	for (i = 0; i < H2C_MCC_NOA_PARAM_LEN; i++)
		pr_dbg("0x%x ", cmd[i]);
	pr_dbg("\n");
	RTW_INFO("=========================\n");
#endif /* CONFIG_MCC_MODE_DEBUG */

	rtw_hal_fill_h2c_cmd(padapter, H2C_MCC_NOA_PARAM, H2C_MCC_NOA_PARAM_LEN, cmd);
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
	for (i = 0; i < dvobj->iface_nums; i++) {
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
		pr_dbg("0x%x ", cmd[i]);
	pr_dbg("\n");
	RTW_INFO("=========================\n");
#endif /* CONFIG_MCC_MODE_DEBUG */
	rtw_hal_fill_h2c_cmd(padapter, H2C_MCC_MACID_BITMAP, H2C_MCC_MACID_BITMAP_LEN, cmd);
}

static void rtw_hal_set_mcc_ctrl_cmd(PADAPTER padapter, u8 stop)
{
	u8 cmd[H2C_MCC_CTRL_LEN] = {0}, i = 0;
	u8 order = 0, totalnum = 0, chidx = 0, bw = 0, bw40sc = 0, bw80sc = 0;
	u8 duration = 0, role = 0, incurch = 0, rfetype = 0, distxnull = 0, c2hrpt = 0, chscan = 0;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *pmccobjpriv = &(dvobj->mcc_objpriv);
	struct mlme_ext_priv *pmlmeext = NULL;
	struct mlme_ext_info *pmlmeinfo = NULL;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	_adapter *iface = NULL;

	RTW_INFO(FUNC_ADPT_FMT": stop=%d\n", FUNC_ADPT_ARG(padapter), stop);

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = pmccobjpriv->iface[i];
		if (iface == NULL)
			continue;

		if (stop) {
			if (iface != padapter)
				continue;
		}


		order = iface->mcc_adapterpriv.order;
		if (!stop)
			totalnum = dvobj->iface_nums;
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

		duration = iface->mcc_adapterpriv.mcc_duration;
		role = iface->mcc_adapterpriv.role;

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

static u8 rtw_hal_set_mcc_start_setting(PADAPTER padapter, u8 status)
{
	u8 ret = _SUCCESS;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(dvobj);

	if (pwrpriv->pwr_mode != PS_MODE_ACTIVE) {
		rtw_warn_on(1);
		RTW_INFO("PS mode is not active before start mcc, force exit ps mode\n");
		LeaveAllPowerSaveModeDirect(padapter);
	}

	if (dvobj->iface_nums > MAX_MCC_NUM) {
		RTW_INFO("%s: current iface num(%d) > MAX_MCC_NUM(%d)\n", __func__, dvobj->iface_nums, MAX_MCC_NUM);
		ret = _FAIL;
		goto exit;
	}

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

	/* configure NoA setting */
	rtw_hal_set_mcc_noa_cmd(padapter);

	/* IQK value offload */
	rtw_hal_set_mcc_IQK_offload_cmd(padapter);

	/* set mac id to fw */
	rtw_hal_set_mcc_macid_cmd(padapter);

	/* set mcc parameter  */
	rtw_hal_set_mcc_ctrl_cmd(padapter, _FALSE);

exit:
	return ret;
}

static void rtw_hal_set_mcc_stop_setting(PADAPTER padapter, u8 status)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	_adapter *iface = NULL;
	u8 i = 0;
	/*
	 * when adapter disconnect, stop mcc mod
	 * total=0xf means stop mcc mode
	 */

	switch (status) {
	default:
		/* let fw switch to other interface channel */
		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			if (iface == NULL)
				continue;
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
	_adapter *iface = NULL;
	u8 i = 0;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (iface == NULL)
			continue;
		/* release network queue */
		rtw_netif_wake_queue(iface->pnetdev);
		iface->mcc_adapterpriv.mcc_tx_bytes_from_kernel = 0;
		iface->mcc_adapterpriv.mcc_last_tx_bytes_from_kernel = 0;
		iface->mcc_adapterpriv.mcc_tx_bytes_to_port = 0;

		if (iface->mcc_adapterpriv.role == MCC_ROLE_GO)
			rtw_hal_mcc_remove_go_p2p_ie(iface);
	}
}

static void rtw_hal_mcc_start_posthdl(PADAPTER padapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	_adapter *iface = NULL;
	u8 i = 0;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (iface == NULL)
			continue;
		iface->mcc_adapterpriv.mcc_tx_bytes_from_kernel = 0;
		iface->mcc_adapterpriv.mcc_last_tx_bytes_from_kernel = 0;
		iface->mcc_adapterpriv.mcc_tx_bytes_to_port = 0;
	}
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
	systime start_time = rtw_get_current_time();

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
			rtw_hal_mcc_stop_posthdl(padapter);
		}
	}

exit:

	rtw_hal_mcc_status_hdl(padapter, status);
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

/**
 * rtw_hal_mcc_c2h_handler - mcc c2h handler
 */
void rtw_hal_mcc_c2h_handler(PADAPTER padapter, u8 buflen, u8 *tmpBuf)
{
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);
	struct mcc_obj_priv *pmccobjpriv = &(adapter_to_dvobj(padapter)->mcc_objpriv);
	struct mcc_adapter_priv *pmccadapriv = &padapter->mcc_adapterpriv;
	struct submit_ctx *mcc_sctx = &pmccobjpriv->mcc_sctx;
	_irqL irqL;

	/* RTW_INFO("[length]=%d, [C2H data]="MAC_FMT"\n", buflen, MAC_ARG(tmpBuf)); */
	/* To avoid reg is set, but driver recive c2h to set wrong oper_channel */
	if (MCC_RPT_STOPMCC == pmccobjpriv->mcc_c2h_status) {
		RTW_INFO(FUNC_ADPT_FMT" MCC alread stops return\n", FUNC_ADPT_ARG(padapter));
		return;
	}

	pmccobjpriv->mcc_c2h_status = tmpBuf[0];
	switch (pmccobjpriv->mcc_c2h_status) {
	case MCC_RPT_SUCCESS:
		pdvobjpriv->oper_channel = tmpBuf[1];
		_enter_critical_bh(&pmccobjpriv->mcc_lock, &irqL);
		pmccobjpriv->cur_mcc_success_cnt++;
		_exit_critical_bh(&pmccobjpriv->mcc_lock, &irqL);
		break;
	case MCC_RPT_TXNULL_FAIL:
		RTW_INFO("[MCC] TXNULL FAIL\n");
		break;
	case MCC_RPT_STOPMCC:
		RTW_INFO("[MCC] MCC stop (time:%d)\n", rtw_get_current_time());
		pmccobjpriv->mcc_c2h_status = MCC_RPT_STOPMCC;
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

		RTW_INFO("[MCC] MCC ready (time:%d)\n", pmccobjpriv->mcc_launch_time);
		rtw_sctx_done(&mcc_sctx);
		break;
	case MCC_RPT_SWICH_CHANNEL_NOTIFY:
		pdvobjpriv->oper_channel = tmpBuf[1];
		rtw_hal_mcc_sw_ch_fw_notify_hdl(padapter);
		break;
	case MCC_RPT_UPDATE_NOA_START_TIME:
		rtw_hal_mcc_update_noa_start_time_hdl(padapter, buflen, tmpBuf);
		break;
	default:
		/* RTW_INFO("[MCC] Other MCC status(%d)\n", pmccobjpriv->mcc_c2h_status); */
		break;
	}
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
	u8 cur_cnt = 0, prev_cnt = 0, diff_cnt = 0, check_ret = _FAIL;
	_irqL irqL;

/* #define MCC_RESTART 1 */

	if (!MCC_EN(padapter))
		return;

	_enter_critical_mutex(&pmccobjpriv->mcc_mutex, NULL);

	if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_DOING_MCC)) {

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

			if (diff_cnt < 30) {
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
	u8 need_ch_setting_union = _TRUE, i = 0, flags = 0, role = 0;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct mcc_adapter_priv *pmccadapriv = NULL;
	struct mlme_ext_priv *pmlmeext = NULL;

	if (!MCC_EN(padapter))
		goto exit;

	if (!rtw_hal_check_mcc_status(padapter, MCC_STATUS_NEED_MCC))
		goto exit;

	for (i = 0; i < dvobj->iface_nums; i++) {
		if (!dvobj->padapters[i])
				continue;

		pmlmeext = &dvobj->padapters[i]->mlmeextpriv;
		pmccadapriv = &dvobj->padapters[i]->mcc_adapterpriv;
		role = pmccadapriv->role;

		switch (role) {
		case MCC_ROLE_AP:
		case MCC_ROLE_GO:
			*ch = pmlmeext->cur_channel;
			*bw = pmlmeext->cur_bwmode;
			*offset = pmlmeext->cur_ch_offset;
			need_ch_setting_union = _FALSE;
			break;
		case MCC_ROLE_STA:
		case MCC_ROLE_GC:
			break;
		default:
			RTW_INFO("unknown role\n");
			rtw_warn_on(1);
			break;
		}

		/* check other scan flag */
		flags = mlmeext_scan_backop_flags(pmlmeext);
		if (mlmeext_chk_scan_backop_flags(pmlmeext, SS_BACKOP_PS_ANNC))
			flags &= ~SS_BACKOP_PS_ANNC;

		if (mlmeext_chk_scan_backop_flags(pmlmeext, SS_BACKOP_TX_RESUME))
			flags &= ~SS_BACKOP_TX_RESUME;

		mlmeext_assign_scan_backop_flags(pmlmeext, flags);

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

		if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_DOING_MCC))
			pmccadapriv->mcc_tx_bytes_to_port += len;
			if (0)
				RTW_INFO("%s(order:%d): mcc tx bytes to port:%d, mcc target tx bytes to port:%d\n"
					, __func__, pmccadapriv->order, pmccadapriv->mcc_tx_bytes_to_port
					, pmccadapriv->mcc_target_tx_bytes_to_port);
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
				/* issue null data to all station connected to AP before scan */
				rtw_hal_mcc_issue_null_data(padapter, 0, 1);
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

		if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_NEED_MCC))
				ret = rtw_hal_set_mcc_setting(padapter,  MCC_SETCMD_STATUS_START_SCAN_DONE);

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

			*ch = pmlmeext->cur_channel;
			*bw = pmlmeext->cur_bwmode;
			*offset = pmlmeext->cur_ch_offset;

			RTW_INFO(FUNC_ADPT_FMT" en_mcc:%d(%d,%d,%d,)\n"
				, FUNC_ADPT_ARG(padapter), padapter->registrypriv.en_mcc
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

void rtw_hal_dump_mcc_info(void *sel, struct dvobj_priv *dvobj)
{
	struct mcc_obj_priv *pmccobjpriv = &(dvobj->mcc_objpriv);
	struct mcc_adapter_priv *pmccadapriv = NULL;
	_adapter *iface = NULL, *adapter = NULL;
	struct registry_priv *regpriv = NULL;
	u8 i = 0;

	/* regpriv is common for all adapter */
	adapter = dvobj->padapters[IFACE_ID0];

	RTW_PRINT_SEL(sel, "**********************************************\n");
	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (!iface)
			continue;

		regpriv = &iface->registrypriv;
		pmccadapriv = &iface->mcc_adapterpriv;
		if (pmccadapriv) {
			RTW_PRINT_SEL(sel, "adapter mcc info:\n");
			RTW_PRINT_SEL(sel, "ifname:%s\n", ADPT_ARG(iface));
			RTW_PRINT_SEL(sel, "order:%d\n", pmccadapriv->order);
			RTW_PRINT_SEL(sel, "duration:%d\n", pmccadapriv->mcc_duration);
			RTW_PRINT_SEL(sel, "target tx bytes:%d\n", pmccadapriv->mcc_target_tx_bytes_to_port);
			RTW_PRINT_SEL(sel, "current TP:%d\n", pmccadapriv->mcc_tp);
			RTW_PRINT_SEL(sel, "mgmt queue macid:%d\n", pmccadapriv->mgmt_queue_macid);
			RTW_PRINT_SEL(sel, "macid bitmap:0x%02x\n\n", pmccadapriv->mcc_macid_bitmap);
			RTW_PRINT_SEL(sel, "registry data:\n");
			RTW_PRINT_SEL(sel, "en_mcc:%d\n", regpriv->en_mcc);
			RTW_PRINT_SEL(sel, "ap target tx TP(BW:20M):%d Mbps\n", regpriv->rtw_mcc_ap_bw20_target_tx_tp);
			RTW_PRINT_SEL(sel, "ap target tx TP(BW:40M):%d Mbps\n", regpriv->rtw_mcc_ap_bw40_target_tx_tp);
			RTW_PRINT_SEL(sel, "ap target tx TP(BW:80M):%d Mbps\n", regpriv->rtw_mcc_ap_bw80_target_tx_tp);
			RTW_PRINT_SEL(sel, "sta target tx TP(BW:20M):%d Mbps\n", regpriv->rtw_mcc_sta_bw20_target_tx_tp);
			RTW_PRINT_SEL(sel, "sta target tx TP(BW:40M ):%d Mbps\n", regpriv->rtw_mcc_sta_bw40_target_tx_tp);
			RTW_PRINT_SEL(sel, "sta target tx TP(BW:80M):%d Mbps\n", regpriv->rtw_mcc_sta_bw80_target_tx_tp);
			RTW_PRINT_SEL(sel, "single tx criteria:%d Mbps\n", regpriv->rtw_mcc_single_tx_cri);
			if (MLME_IS_GO(iface))
				rtw_hal_mcc_dump_noa_content(sel, iface);
			RTW_PRINT_SEL(sel, "**********************************************\n");
		}
	}
	RTW_PRINT_SEL(sel, "------------------------------------------\n");
	RTW_PRINT_SEL(sel, "policy index:%d\n", pmccobjpriv->policy_index);
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

			set_channel_bwmode(iface, ch, bw, offset);

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

#endif /* CONFIG_MCC_MODE */
