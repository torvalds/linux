/******************************************************************************
 *
 * Copyright(c) 2014 - 2017 Realtek Corporation.
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

#include <drv_types.h>
#include <hal_data.h>

/* A mapping from HalData to ODM. */
enum odm_board_type_e boardType(u8 InterfaceSel)
{
	enum odm_board_type_e        board	= ODM_BOARD_DEFAULT;

#ifdef CONFIG_PCI_HCI
	INTERFACE_SELECT_PCIE   pcie	= (INTERFACE_SELECT_PCIE)InterfaceSel;
	switch (pcie) {
	case INTF_SEL0_SOLO_MINICARD:
		board |= ODM_BOARD_MINICARD;
		break;
	case INTF_SEL1_BT_COMBO_MINICARD:
		board |= ODM_BOARD_BT;
		board |= ODM_BOARD_MINICARD;
		break;
	default:
		board = ODM_BOARD_DEFAULT;
		break;
	}

#elif defined(CONFIG_USB_HCI)
	INTERFACE_SELECT_USB    usb	= (INTERFACE_SELECT_USB)InterfaceSel;
	switch (usb) {
	case INTF_SEL1_USB_High_Power:
		board |= ODM_BOARD_EXT_LNA;
		board |= ODM_BOARD_EXT_PA;
		break;
	case INTF_SEL2_MINICARD:
		board |= ODM_BOARD_MINICARD;
		break;
	case INTF_SEL4_USB_Combo:
		board |= ODM_BOARD_BT;
		break;
	case INTF_SEL5_USB_Combo_MF:
		board |= ODM_BOARD_BT;
		break;
	case INTF_SEL0_USB:
	case INTF_SEL3_USB_Solo:
	default:
		board = ODM_BOARD_DEFAULT;
		break;
	}

#endif
	/* RTW_INFO("===> boardType(): (pHalData->InterfaceSel, pDM_Odm->BoardType) = (%d, %d)\n", InterfaceSel, board); */

	return board;
}

void rtw_hal_update_iqk_fw_offload_cap(_adapter *adapter)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	struct PHY_DM_STRUCT *p_dm_odm = adapter_to_phydm(adapter);

	if (hal->RegIQKFWOffload) {
		rtw_sctx_init(&hal->iqk_sctx, 0);
		phydm_fwoffload_ability_init(p_dm_odm, PHYDM_RF_IQK_OFFLOAD);
	} else
		phydm_fwoffload_ability_clear(p_dm_odm, PHYDM_RF_IQK_OFFLOAD);

	RTW_INFO("IQK FW offload:%s\n", hal->RegIQKFWOffload ? "enable" : "disable");
}

#if ((RTL8822B_SUPPORT == 1) || (RTL8821C_SUPPORT == 1) || (RTL8814B_SUPPORT == 1))
void rtw_phydm_iqk_trigger(_adapter *adapter)
{
	struct PHY_DM_STRUCT *p_dm_odm = adapter_to_phydm(adapter);
	u8 clear = _FALSE;
	u8 segment = _FALSE;
	u8 rfk_forbidden = _FALSE;

	/*segment = _rtw_phydm_iqk_segment_chk(adapter);*/
	halrf_cmn_info_set(p_dm_odm, HALRF_CMNINFO_RFK_FORBIDDEN, rfk_forbidden);
	halrf_cmn_info_set(p_dm_odm, HALRF_CMNINFO_IQK_SEGMENT, segment);
	halrf_segment_iqk_trigger(p_dm_odm, clear, segment);
}
#endif

void rtw_phydm_iqk_trigger_dbg(_adapter *adapter, bool recovery, bool clear, bool segment)
{
	struct PHY_DM_STRUCT *p_dm_odm = adapter_to_phydm(adapter);

#if ((RTL8822B_SUPPORT == 1) || (RTL8821C_SUPPORT == 1) || (RTL8814B_SUPPORT == 1))
		halrf_segment_iqk_trigger(p_dm_odm, clear, segment);
#else
		halrf_iqk_trigger(p_dm_odm, recovery);
#endif
}
void rtw_phydm_lck_trigger(_adapter *adapter)
{
	struct PHY_DM_STRUCT *p_dm_odm = adapter_to_phydm(adapter);

	halrf_lck_trigger(p_dm_odm);
}
#ifdef CONFIG_DBG_RF_CAL
void rtw_hal_iqk_test(_adapter *adapter, bool recovery, bool clear, bool segment)
{
	struct PHY_DM_STRUCT *p_dm_odm = adapter_to_phydm(adapter);

	rtw_ps_deny(adapter, PS_DENY_IOCTL);
	LeaveAllPowerSaveModeDirect(adapter);

	rtw_phydm_ability_backup(adapter);
	rtw_phydm_func_disable_all(adapter);

	halrf_cmn_info_set(p_dm_odm, HALRF_CMNINFO_ABILITY, HAL_RF_IQK);

	rtw_phydm_iqk_trigger_dbg(adapter, recovery, clear, segment);
	rtw_phydm_ability_restore(adapter);

	rtw_ps_deny_cancel(adapter, PS_DENY_IOCTL);
}

void rtw_hal_lck_test(_adapter *adapter)
{
	struct PHY_DM_STRUCT *p_dm_odm = adapter_to_phydm(adapter);

	rtw_ps_deny(adapter, PS_DENY_IOCTL);
	LeaveAllPowerSaveModeDirect(adapter);

	rtw_phydm_ability_backup(adapter);
	rtw_phydm_func_disable_all(adapter);

	halrf_cmn_info_set(p_dm_odm, HALRF_CMNINFO_ABILITY, HAL_RF_LCK);

	rtw_phydm_lck_trigger(adapter);

	rtw_phydm_ability_restore(adapter);
	rtw_ps_deny_cancel(adapter, PS_DENY_IOCTL);
}
#endif

#ifdef CONFIG_FW_OFFLOAD_PARAM_INIT
void rtw_hal_update_param_init_fw_offload_cap(_adapter *adapter)
{
	struct PHY_DM_STRUCT *p_dm_odm = adapter_to_phydm(adapter);

	if (adapter->registrypriv.fw_param_init)
		phydm_fwoffload_ability_init(p_dm_odm, PHYDM_PHY_PARAM_OFFLOAD);
	else
		phydm_fwoffload_ability_clear(p_dm_odm, PHYDM_PHY_PARAM_OFFLOAD);

	RTW_INFO("Init-Parameter FW offload:%s\n", adapter->registrypriv.fw_param_init ? "enable" : "disable");
}
#endif

void record_ra_info(void *p_dm_void, u8 macid, struct cmn_sta_info *p_sta, u64 ra_mask)
{
	struct PHY_DM_STRUCT *p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	_adapter *adapter = p_dm->adapter;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct macid_ctl_t *macid_ctl = dvobj_to_macidctl(dvobj);

	rtw_macid_ctl_set_bw(macid_ctl, macid, p_sta->ra_info.ra_bw_mode);
	rtw_macid_ctl_set_vht_en(macid_ctl, macid, p_sta->ra_info.is_vht_enable);
	rtw_macid_ctl_set_rate_bmp0(macid_ctl, macid, ra_mask);
	rtw_macid_ctl_set_rate_bmp1(macid_ctl, macid, ra_mask >> 32);

	rtw_update_tx_rate_bmp(adapter_to_dvobj(adapter));
}

void rtw_phydm_ops_func_init(struct PHY_DM_STRUCT *p_phydm)
{
	struct _rate_adaptive_table_ *p_ra_t = &p_phydm->dm_ra_table;

	p_ra_t->record_ra_info = record_ra_info;
}

void Init_ODM_ComInfo(_adapter *adapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(adapter);
	struct PHY_DM_STRUCT		*pDM_Odm = &(pHalData->odmpriv);
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);
	int i;

	_rtw_memset(pDM_Odm, 0, sizeof(*pDM_Odm));

	pDM_Odm->adapter = adapter;

	/*phydm_op_mode could be change for different scenarios: ex: SoftAP - PHYDM_BALANCE_MODE*/
	pHalData->phydm_op_mode = PHYDM_PERFORMANCE_MODE;/*Service one device*/

	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_PLATFORM, ODM_CE);

	rtw_odm_init_ic_type(adapter);

	if (rtw_get_intf_type(adapter) == RTW_GSPI)
		odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_INTERFACE, ODM_ITRF_SDIO);
	else
		odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_INTERFACE, rtw_get_intf_type(adapter));

	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_MP_TEST_CHIP, IS_NORMAL_CHIP(pHalData->version_id));

	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_PATCH_ID, pHalData->CustomerID);

	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_BWIFI_TEST, adapter->registrypriv.wifi_spec);

#ifdef CONFIG_ADVANCE_OTA
	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_ADVANCE_OTA, adapter->registrypriv.adv_ota);
#endif
	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_RF_TYPE, pHalData->rf_type);

	{
		/* 1 ======= BoardType: ODM_CMNINFO_BOARD_TYPE ======= */
		u8 odm_board_type = ODM_BOARD_DEFAULT;

		if (pHalData->ExternalLNA_2G != 0) {
			odm_board_type |= ODM_BOARD_EXT_LNA;
			odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_EXT_LNA, 1);
		}
		if (pHalData->external_lna_5g != 0) {
			odm_board_type |= ODM_BOARD_EXT_LNA_5G;
			odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_5G_EXT_LNA, 1);
		}
		if (pHalData->ExternalPA_2G != 0) {
			odm_board_type |= ODM_BOARD_EXT_PA;
			odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_EXT_PA, 1);
		}
		if (pHalData->external_pa_5g != 0) {
			odm_board_type |= ODM_BOARD_EXT_PA_5G;
			odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_5G_EXT_PA, 1);
		}
		if (pHalData->EEPROMBluetoothCoexist)
			odm_board_type |= ODM_BOARD_BT;

		odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_BOARD_TYPE, odm_board_type);
		/* 1 ============== End of BoardType ============== */
	}

	rtw_hal_set_odm_var(adapter, HAL_ODM_REGULATION, NULL, _TRUE);

#ifdef CONFIG_DFS_MASTER
	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_DFS_REGION_DOMAIN, adapter->registrypriv.dfs_region_domain);
	odm_cmn_info_hook(pDM_Odm, ODM_CMNINFO_DFS_MASTER_ENABLE, &(adapter_to_rfctl(adapter)->dfs_master_enabled));
#endif

	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_GPA, pHalData->TypeGPA);
	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_APA, pHalData->TypeAPA);
	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_GLNA, pHalData->TypeGLNA);
	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_ALNA, pHalData->TypeALNA);

	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_RFE_TYPE, pHalData->rfe_type);

	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_EXT_TRSW, 0);

	/*Add by YuChen for kfree init*/
	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_REGRFKFREEENABLE, adapter->registrypriv.RegPwrTrimEnable);
	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_RFKFREEENABLE, pHalData->RfKFreeEnable);

	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_RF_ANTENNA_TYPE, pHalData->TRxAntDivType);
	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_BE_FIX_TX_ANT, pHalData->b_fix_tx_ant);
	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_WITH_EXT_ANTENNA_SWITCH, pHalData->with_extenal_ant_switch);

	/* (8822B) efuse 0x3D7 & 0x3D8 for TX PA bias */
	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_EFUSE0X3D7, pHalData->efuse0x3d7);
	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_EFUSE0X3D8, pHalData->efuse0x3d8);

	/*Add by YuChen for adaptivity init*/
	odm_cmn_info_hook(pDM_Odm, ODM_CMNINFO_ADAPTIVITY, &(adapter->registrypriv.adaptivity_en));
	phydm_adaptivity_info_init(pDM_Odm, PHYDM_ADAPINFO_CARRIER_SENSE_ENABLE, (adapter->registrypriv.adaptivity_mode != 0) ? TRUE : FALSE);
	phydm_adaptivity_info_init(pDM_Odm, PHYDM_ADAPINFO_DCBACKOFF, adapter->registrypriv.adaptivity_dc_backoff);
	phydm_adaptivity_info_init(pDM_Odm, PHYDM_ADAPINFO_DYNAMICLINKADAPTIVITY, (adapter->registrypriv.adaptivity_dml != 0) ? TRUE : FALSE);
	phydm_adaptivity_info_init(pDM_Odm, PHYDM_ADAPINFO_TH_L2H_INI, adapter->registrypriv.adaptivity_th_l2h_ini);
	phydm_adaptivity_info_init(pDM_Odm, PHYDM_ADAPINFO_TH_EDCCA_HL_DIFF, adapter->registrypriv.adaptivity_th_edcca_hl_diff);

	/*halrf info init*/
	halrf_cmn_info_init(pDM_Odm, HALRF_CMNINFO_EEPROM_THERMAL_VALUE, pHalData->eeprom_thermal_meter);
	halrf_cmn_info_init(pDM_Odm, HALRF_CMNINFO_FW_VER,
		((pHalData->firmware_version << 16) | pHalData->firmware_sub_version));

	if (rtw_odm_adaptivity_needed(adapter) == _TRUE)
		rtw_odm_adaptivity_config_msg(RTW_DBGDUMP, adapter);

#ifdef CONFIG_IQK_PA_OFF
	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_IQKPAOFF, 1);
#endif
	rtw_hal_update_iqk_fw_offload_cap(adapter);
	#ifdef CONFIG_FW_OFFLOAD_PARAM_INIT
	rtw_hal_update_param_init_fw_offload_cap(adapter);
	#endif

	/* Pointer reference */
	/*Antenna diversity relative parameters*/
	odm_cmn_info_hook(pDM_Odm, ODM_CMNINFO_ANT_DIV, &(pHalData->AntDivCfg));
	odm_cmn_info_hook(pDM_Odm, ODM_CMNINFO_MP_MODE, &(adapter->registrypriv.mp_mode));

	odm_cmn_info_hook(pDM_Odm, ODM_CMNINFO_BB_OPERATION_MODE, &(pHalData->phydm_op_mode));
	odm_cmn_info_hook(pDM_Odm, ODM_CMNINFO_TX_UNI, &(dvobj->traffic_stat.tx_bytes));
	odm_cmn_info_hook(pDM_Odm, ODM_CMNINFO_RX_UNI, &(dvobj->traffic_stat.rx_bytes));

	odm_cmn_info_hook(pDM_Odm, ODM_CMNINFO_BAND, &(pHalData->current_band_type));
	odm_cmn_info_hook(pDM_Odm, ODM_CMNINFO_FORCED_RATE, &(pHalData->ForcedDataRate));

	odm_cmn_info_hook(pDM_Odm, ODM_CMNINFO_SEC_CHNL_OFFSET, &(pHalData->nCur40MhzPrimeSC));
	odm_cmn_info_hook(pDM_Odm, ODM_CMNINFO_SEC_MODE, &(adapter->securitypriv.dot11PrivacyAlgrthm));
	odm_cmn_info_hook(pDM_Odm, ODM_CMNINFO_BW, &(pHalData->current_channel_bw));
	odm_cmn_info_hook(pDM_Odm, ODM_CMNINFO_CHNL, &(pHalData->current_channel));
	odm_cmn_info_hook(pDM_Odm, ODM_CMNINFO_NET_CLOSED, &(adapter->net_closed));

	odm_cmn_info_hook(pDM_Odm, ODM_CMNINFO_SCAN, &(pHalData->bScanInProcess));
	odm_cmn_info_hook(pDM_Odm, ODM_CMNINFO_POWER_SAVING, &(pwrctl->bpower_saving));
	/*Add by Yuchen for phydm beamforming*/
	odm_cmn_info_hook(pDM_Odm, ODM_CMNINFO_TX_TP, &(dvobj->traffic_stat.cur_tx_tp));
	odm_cmn_info_hook(pDM_Odm, ODM_CMNINFO_RX_TP, &(dvobj->traffic_stat.cur_rx_tp));
	odm_cmn_info_hook(pDM_Odm, ODM_CMNINFO_ANT_TEST, &(pHalData->antenna_test));
#ifdef CONFIG_RTL8723B
	odm_cmn_info_hook(pDM_Odm, ODM_CMNINFO_IS1ANTENNA, &pHalData->EEPROMBluetoothAntNum);
	odm_cmn_info_hook(pDM_Odm, ODM_CMNINFO_RFDEFAULTPATH, &pHalData->ant_path);
#endif /*CONFIG_RTL8723B*/
#ifdef CONFIG_USB_HCI
	odm_cmn_info_hook(pDM_Odm, ODM_CMNINFO_HUBUSBMODE, &(dvobj->usb_speed));
#endif

	/*halrf info hook*/
#ifdef CONFIG_MP_INCLUDED
	halrf_cmn_info_hook(pDM_Odm, HALRF_CMNINFO_CON_TX, &(adapter->mppriv.mpt_ctx.is_start_cont_tx));
	halrf_cmn_info_hook(pDM_Odm, HALRF_CMNINFO_SINGLE_TONE, &(adapter->mppriv.mpt_ctx.is_single_tone));
	halrf_cmn_info_hook(pDM_Odm, HALRF_CMNINFO_CARRIER_SUPPRESSION, &(adapter->mppriv.mpt_ctx.is_carrier_suppression));
	halrf_cmn_info_hook(pDM_Odm, HALRF_CMNINFO_MP_RATE_INDEX, &(adapter->mppriv.mpt_ctx.mpt_rate_index));
#endif/*CONFIG_MP_INCLUDED*/
	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++)
		odm_cmn_info_ptr_array_hook(pDM_Odm, ODM_CMNINFO_STA_STATUS, i, NULL);

	phydm_init_debug_setting(pDM_Odm);
	rtw_phydm_ops_func_init(pDM_Odm);
	/* TODO */
	/* odm_cmn_info_hook(pDM_Odm, ODM_CMNINFO_BT_OPERATION, _FALSE); */
	/* odm_cmn_info_hook(pDM_Odm, ODM_CMNINFO_BT_DISABLE_EDCA, _FALSE); */
}


static u32 edca_setting_UL[HT_IOT_PEER_MAX] =
/*UNKNOWN, REALTEK_90, REALTEK_92SE, BROADCOM,*/
/*RALINK, ATHEROS, CISCO, MERU, MARVELL, 92U_AP, SELF_AP(DownLink/Tx) */
{ 0x5e4322, 0xa44f, 0x5e4322, 0x5ea32b, 0x5ea422, 0x5ea322, 0x3ea430, 0x5ea42b, 0x5ea44f, 0x5e4322, 0x5e4322};

static u32 edca_setting_DL[HT_IOT_PEER_MAX] =
/*UNKNOWN, REALTEK_90, REALTEK_92SE, BROADCOM,*/
/*RALINK, ATHEROS, CISCO, MERU, MARVELL, 92U_AP, SELF_AP(UpLink/Rx)*/
{ 0xa44f, 0x5ea44f,	 0x5e4322, 0x5ea42b, 0xa44f, 0xa630, 0x5ea630, 0x5ea42b, 0xa44f, 0xa42b, 0xa42b};

static u32 edca_setting_dl_g_mode[HT_IOT_PEER_MAX] =
/*UNKNOWN, REALTEK_90, REALTEK_92SE, BROADCOM,*/
/*RALINK, ATHEROS, CISCO, MERU, MARVELL, 92U_AP, SELF_AP */
{ 0x4322, 0xa44f, 0x5e4322, 0xa42b, 0x5e4322, 0x4322,	 0xa42b, 0x5ea42b, 0xa44f, 0x5e4322, 0x5ea42b};

void rtw_hal_turbo_edca(_adapter *adapter)
{
	HAL_DATA_TYPE		*hal_data = GET_HAL_DATA(adapter);
	struct dvobj_priv		*dvobj = adapter_to_dvobj(adapter);
	struct recv_priv		*precvpriv = &(adapter->recvpriv);
	struct registry_priv		*pregpriv = &adapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &(adapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	/* Parameter suggested by Scott  */
#if 0
	u32	EDCA_BE_UL = edca_setting_UL[p_mgnt_info->iot_peer];
	u32	EDCA_BE_DL = edca_setting_DL[p_mgnt_info->iot_peer];
#endif
	u32	EDCA_BE_UL = 0x5ea42b;
	u32	EDCA_BE_DL = 0x00a42b;
	u8	ic_type = rtw_get_chip_type(adapter);

	u8	iot_peer = 0;
	u8	wireless_mode = 0xFF;                 /* invalid value */
	u8	traffic_index;
	u32	edca_param;
	u64	cur_tx_bytes = 0;
	u64	cur_rx_bytes = 0;
	u8	bbtchange = _TRUE;
	u8	is_bias_on_rx = _FALSE;
	u8	is_linked = _FALSE;
	u8	interface_type;

	if (hal_data->dis_turboedca)
		return;

	if (rtw_mi_check_status(adapter, MI_ASSOC))
		is_linked = _TRUE;

	if (is_linked != _TRUE) {
		precvpriv->is_any_non_be_pkts = _FALSE;
		return;
	}

	if ((pregpriv->wifi_spec == 1)) { /* || (pmlmeinfo->HT_enable == 0)) */
		precvpriv->is_any_non_be_pkts = _FALSE;
		return;
	}

	interface_type = rtw_get_intf_type(adapter);
	wireless_mode = pmlmeext->cur_wireless_mode;

	iot_peer = pmlmeinfo->assoc_AP_vendor;

	if (iot_peer >=  HT_IOT_PEER_MAX) {
		precvpriv->is_any_non_be_pkts = _FALSE;
		return;
	}

	if (ic_type == RTL8188E) {
		if ((iot_peer == HT_IOT_PEER_RALINK) || (iot_peer == HT_IOT_PEER_ATHEROS))
			is_bias_on_rx = _TRUE;
	}

	/* Check if the status needs to be changed. */
	if ((bbtchange) || (!precvpriv->is_any_non_be_pkts)) {
		cur_tx_bytes = dvobj->traffic_stat.cur_tx_bytes;
		cur_rx_bytes = dvobj->traffic_stat.cur_rx_bytes;

		/* traffic, TX or RX */
		if (is_bias_on_rx) {
			if (cur_tx_bytes > (cur_rx_bytes << 2)) {
				/* Uplink TP is present. */
				traffic_index = UP_LINK;
			} else {
				/* Balance TP is present. */
				traffic_index = DOWN_LINK;
			}
		} else {
			if (cur_rx_bytes > (cur_tx_bytes << 2)) {
				/* Downlink TP is present. */
				traffic_index = DOWN_LINK;
			} else {
				/* Balance TP is present. */
				traffic_index = UP_LINK;
			}
		}
#if 0
		if ((p_dm_odm->dm_edca_table.prv_traffic_idx != traffic_index)
			|| (!p_dm_odm->dm_edca_table.is_current_turbo_edca))
#endif
		{
			if (interface_type == RTW_PCIE) {
				EDCA_BE_UL = 0x6ea42b;
				EDCA_BE_DL = 0x6ea42b;
			}

			/* 92D txop can't be set to 0x3e for cisco1250 */
			if ((iot_peer == HT_IOT_PEER_CISCO) && (wireless_mode == ODM_WM_N24G)) {
				EDCA_BE_DL = edca_setting_DL[iot_peer];
				EDCA_BE_UL = edca_setting_UL[iot_peer];
			}
			/* merge from 92s_92c_merge temp*/
			else if ((iot_peer == HT_IOT_PEER_CISCO) && ((wireless_mode == ODM_WM_G) || (wireless_mode == (ODM_WM_B | ODM_WM_G)) || (wireless_mode == ODM_WM_A) || (wireless_mode == ODM_WM_B)))
				EDCA_BE_DL = edca_setting_dl_g_mode[iot_peer];
			else if ((iot_peer == HT_IOT_PEER_AIRGO) && ((wireless_mode == ODM_WM_G) || (wireless_mode == ODM_WM_A)))
				EDCA_BE_DL = 0xa630;
			else if (iot_peer == HT_IOT_PEER_MARVELL) {
				EDCA_BE_DL = edca_setting_DL[iot_peer];
				EDCA_BE_UL = edca_setting_UL[iot_peer];
			} else if (iot_peer == HT_IOT_PEER_ATHEROS) {
				/* Set DL EDCA for Atheros peer to 0x3ea42b.*/
				/* Suggested by SD3 Wilson for ASUS TP issue.*/
				EDCA_BE_DL = edca_setting_DL[iot_peer];
			}

			if ((ic_type == RTL8812) || (ic_type == RTL8821) || (ic_type == RTL8192E)) { /* add 8812AU/8812AE */
				EDCA_BE_UL = 0x5ea42b;
				EDCA_BE_DL = 0x5ea42b;

				RTW_DBG("8812A: EDCA_BE_UL=0x%x EDCA_BE_DL =0x%x\n", EDCA_BE_UL, EDCA_BE_DL);
			}

			if (interface_type == RTW_PCIE &&
				((ic_type == RTL8822B)
				|| (ic_type == RTL8814A))) {
				EDCA_BE_UL = 0x6ea42b;
				EDCA_BE_DL = 0x6ea42b;
			}

			if (traffic_index == DOWN_LINK)
				edca_param = EDCA_BE_DL;
			else
				edca_param = EDCA_BE_UL;
#ifdef 	CONFIG_RTW_CUSTOMIZE_BEEDCA
			edca_param = CONFIG_RTW_CUSTOMIZE_BEEDCA;
#endif
			rtw_hal_set_hwreg(adapter, HW_VAR_AC_PARAM_BE, (u8 *)(&edca_param));

			RTW_DBG("Turbo EDCA =0x%x\n", edca_param);

			hal_data->prv_traffic_idx = traffic_index;
		}

		hal_data->is_turbo_edca = _TRUE;
	} else {
		/*  */
		/* Turn Off EDCA turbo here. */
		/* Restore original EDCA according to the declaration of AP. */
		/*  */
		if (hal_data->is_turbo_edca) {
			edca_param = hal_data->ac_param_be;
			rtw_hal_set_hwreg(adapter, HW_VAR_AC_PARAM_BE, (u8 *)(&edca_param));
			hal_data->is_turbo_edca = _FALSE;
		}
	}

}

s8 rtw_phydm_get_min_rssi(_adapter *adapter)
{
	struct PHY_DM_STRUCT *phydm = adapter_to_phydm(adapter);
	s8 rssi_min = 0;

	rssi_min = phydm_cmn_info_query(phydm, (enum phydm_info_query_e) PHYDM_INFO_RSSI_MIN);
	return rssi_min;
}

u8 rtw_phydm_get_cur_igi(_adapter *adapter)
{
	struct PHY_DM_STRUCT *phydm = adapter_to_phydm(adapter);
	u8 cur_igi = 0;

	cur_igi = phydm_cmn_info_query(phydm, (enum phydm_info_query_e) PHYDM_INFO_CURR_IGI);
	return cur_igi;
}

u32 rtw_phydm_get_phy_cnt(_adapter *adapter, enum phy_cnt cnt)
{
	struct PHY_DM_STRUCT *phydm = adapter_to_phydm(adapter);

	if (cnt == FA_OFDM)
		return  phydm_cmn_info_query(phydm, (enum phydm_info_query_e) PHYDM_INFO_FA_OFDM);
	else if (cnt == FA_CCK)
		return  phydm_cmn_info_query(phydm, (enum phydm_info_query_e) PHYDM_INFO_FA_CCK);
	else if (cnt == FA_TOTAL)
		return  phydm_cmn_info_query(phydm, (enum phydm_info_query_e) PHYDM_INFO_FA_TOTAL);
	else if (cnt == CCA_OFDM)
		return	phydm_cmn_info_query(phydm, (enum phydm_info_query_e) PHYDM_INFO_CCA_OFDM);
	else if (cnt == CCA_CCK)
		return	phydm_cmn_info_query(phydm, (enum phydm_info_query_e) PHYDM_INFO_CCA_CCK);
	else if (cnt == CCA_ALL)
		return	phydm_cmn_info_query(phydm, (enum phydm_info_query_e) PHYDM_INFO_CCA_ALL);
	else if (cnt == CRC32_OK_VHT)
		return	phydm_cmn_info_query(phydm, (enum phydm_info_query_e) PHYDM_INFO_CRC32_OK_VHT);
	else if (cnt == CRC32_OK_HT)
		return	phydm_cmn_info_query(phydm, (enum phydm_info_query_e) PHYDM_INFO_CRC32_OK_HT);
	else if (cnt == CRC32_OK_LEGACY)
		return	phydm_cmn_info_query(phydm, (enum phydm_info_query_e) PHYDM_INFO_CRC32_OK_LEGACY);
	else if (cnt == CRC32_OK_CCK)
		return	phydm_cmn_info_query(phydm, (enum phydm_info_query_e) PHYDM_INFO_CRC32_OK_CCK);
	else if (cnt == CRC32_ERROR_VHT)
		return	phydm_cmn_info_query(phydm, (enum phydm_info_query_e) PHYDM_INFO_CRC32_ERROR_VHT);
	else if (cnt == CRC32_ERROR_HT)
		return	phydm_cmn_info_query(phydm, (enum phydm_info_query_e) PHYDM_INFO_CRC32_ERROR_HT);
	else if (cnt == CRC32_ERROR_LEGACY)
		return	phydm_cmn_info_query(phydm, (enum phydm_info_query_e) PHYDM_INFO_CRC32_ERROR_LEGACY);
	else if (cnt == CRC32_ERROR_CCK)
		return	phydm_cmn_info_query(phydm, (enum phydm_info_query_e) PHYDM_INFO_CRC32_ERROR_CCK);
	else
		return 0;
}

u8 rtw_phydm_is_iqk_in_progress(_adapter *adapter)
{
	u8 rts = _FALSE;
	struct PHY_DM_STRUCT *podmpriv = adapter_to_phydm(adapter);

	odm_acquire_spin_lock(podmpriv, RT_IQK_SPINLOCK);
	if (podmpriv->rf_calibrate_info.is_iqk_in_progress == _TRUE) {
		RTW_ERR("IQK InProgress\n");
		rts = _TRUE;
	}
	odm_release_spin_lock(podmpriv, RT_IQK_SPINLOCK);

	return rts;
}

void SetHalODMVar(
	PADAPTER				Adapter,
	HAL_ODM_VARIABLE		eVariable,
	PVOID					pValue1,
	BOOLEAN					bSet)
{
	struct PHY_DM_STRUCT *podmpriv = adapter_to_phydm(Adapter);
	/* _irqL irqL; */
	switch (eVariable) {
	case HAL_ODM_STA_INFO: {
		struct sta_info *psta = (struct sta_info *)pValue1;

		if (bSet) {
			RTW_INFO("### Set STA_(%d) info ###\n", psta->cmn.mac_id);
			odm_cmn_info_ptr_array_hook(podmpriv, ODM_CMNINFO_STA_STATUS, psta->cmn.mac_id, psta);
			psta->cmn.dm_ctrl = STA_DM_CTRL_ACTIVE;
			phydm_cmn_sta_info_hook(podmpriv, psta->cmn.mac_id, &(psta->cmn));
		} else {
			RTW_INFO("### Clean STA_(%d) info ###\n", psta->cmn.mac_id);
			/* _enter_critical_bh(&pHalData->odm_stainfo_lock, &irqL); */
			psta->cmn.dm_ctrl = 0;
			odm_cmn_info_ptr_array_hook(podmpriv, ODM_CMNINFO_STA_STATUS, psta->cmn.mac_id, NULL);
			phydm_cmn_sta_info_hook(podmpriv, psta->cmn.mac_id, NULL);

			/* _exit_critical_bh(&pHalData->odm_stainfo_lock, &irqL); */
		}
	}
		break;
	case HAL_ODM_P2P_STATE:
		odm_cmn_info_update(podmpriv, ODM_CMNINFO_WIFI_DIRECT, bSet);
		break;
	case HAL_ODM_WIFI_DISPLAY_STATE:
		odm_cmn_info_update(podmpriv, ODM_CMNINFO_WIFI_DISPLAY, bSet);
		break;
	case HAL_ODM_REGULATION:
		/* used to auto enable/disable adaptivity by SD7 */
		odm_cmn_info_init(podmpriv, ODM_CMNINFO_DOMAIN_CODE_2G, 0);
		odm_cmn_info_init(podmpriv, ODM_CMNINFO_DOMAIN_CODE_5G, 0);
		break;
	case HAL_ODM_INITIAL_GAIN: {
		u8 rx_gain = *((u8 *)(pValue1));
		/*printk("rx_gain:%x\n",rx_gain);*/
		if (rx_gain == 0xff) {/*restore rx gain*/
			/*odm_write_dig(podmpriv,pDigTable->backup_ig_value);*/
			odm_pause_dig(podmpriv, PHYDM_RESUME, PHYDM_PAUSE_LEVEL_0, rx_gain);
		} else {
			/*pDigTable->backup_ig_value = pDigTable->cur_ig_value;*/
			/*odm_write_dig(podmpriv,rx_gain);*/
			odm_pause_dig(podmpriv, PHYDM_PAUSE, PHYDM_PAUSE_LEVEL_0, rx_gain);
		}
	}
	break;
	case HAL_ODM_RX_INFO_DUMP: {
		u8 cur_igi = 0;
		s8 rssi_min;
		void *sel;

		sel = pValue1;
		cur_igi = rtw_phydm_get_cur_igi(Adapter);
		rssi_min = rtw_phydm_get_min_rssi(Adapter);

		_RTW_PRINT_SEL(sel, "============ Rx Info dump ===================\n");
		_RTW_PRINT_SEL(sel, "is_linked = %d, rssi_min = %d(%%), current_igi = 0x%x\n", podmpriv->is_linked, rssi_min, cur_igi);
		_RTW_PRINT_SEL(sel, "cnt_cck_fail = %d, cnt_ofdm_fail = %d, Total False Alarm = %d\n",
			rtw_phydm_get_phy_cnt(Adapter, FA_CCK),
			rtw_phydm_get_phy_cnt(Adapter, FA_OFDM),
			rtw_phydm_get_phy_cnt(Adapter, FA_TOTAL));

		if (podmpriv->is_linked) {
			_RTW_PRINT_SEL(sel, "rx_rate = %s", HDATA_RATE(podmpriv->rx_rate));
			if (IS_HARDWARE_TYPE_8814A(Adapter))
				_RTW_PRINT_SEL(sel, " RSSI_A = %d(%%), RSSI_B = %d(%%), RSSI_C = %d(%%), RSSI_D = %d(%%)\n",
					podmpriv->RSSI_A, podmpriv->RSSI_B, podmpriv->RSSI_C, podmpriv->RSSI_D);
			else
				_RTW_PRINT_SEL(sel, " RSSI_A = %d(%%), RSSI_B = %d(%%)\n", podmpriv->RSSI_A, podmpriv->RSSI_B);
#ifdef DBG_RX_SIGNAL_DISPLAY_RAW_DATA
			rtw_dump_raw_rssi_info(Adapter, sel);
#endif
		}
	}
		break;
	case HAL_ODM_RX_Dframe_INFO: {
		void *sel;

		sel = pValue1;

		/*_RTW_PRINT_SEL(sel , "HAL_ODM_RX_Dframe_INFO\n");*/
#ifdef DBG_RX_DFRAME_RAW_DATA
		rtw_dump_rx_dframe_info(Adapter, sel);
#endif
	}
		break;

#ifdef CONFIG_ANTENNA_DIVERSITY
	case HAL_ODM_ANTDIV_SELECT: {
		u8	antenna = (*(u8 *)pValue1);
		HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
		/*switch antenna*/
		odm_update_rx_idle_ant(&pHalData->odmpriv, antenna);
		/*RTW_INFO("==> HAL_ODM_ANTDIV_SELECT, Ant_(%s)\n", (antenna == MAIN_ANT) ? "MAIN_ANT" : "AUX_ANT");*/

	}
		break;
#endif

	default:
		break;
	}
}

void GetHalODMVar(
	PADAPTER				Adapter,
	HAL_ODM_VARIABLE		eVariable,
	PVOID					pValue1,
	PVOID					pValue2)
{
	struct PHY_DM_STRUCT *podmpriv = adapter_to_phydm(Adapter);

	switch (eVariable) {
#ifdef CONFIG_ANTENNA_DIVERSITY
	case HAL_ODM_ANTDIV_SELECT: {
		struct phydm_fat_struct	*pDM_FatTable = &podmpriv->dm_fat_table;
		*((u8 *)pValue1) = pDM_FatTable->rx_idle_ant;
	}
		break;
#endif
	case HAL_ODM_INITIAL_GAIN:
		*((u8 *)pValue1) = rtw_phydm_get_cur_igi(Adapter);
		break;
	default:
		break;
	}
}

#ifdef RTW_HALMAC
#include "../hal_halmac.h"
#endif

enum hal_status
rtw_phydm_fw_iqk(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u8 clear,
	u8 segment
)
{
	#ifdef RTW_HALMAC
	struct _ADAPTER *adapter = p_dm_odm->adapter;

	if (rtw_halmac_iqk(adapter_to_dvobj(adapter), clear, segment) == 0)
		return HAL_STATUS_SUCCESS;
	#endif
	return HAL_STATUS_FAILURE;
}

enum hal_status
rtw_phydm_cfg_phy_para(
	struct PHY_DM_STRUCT	*p_dm_odm,
	enum phydm_halmac_param config_type,
	u32 offset,
	u32 data,
	u32 mask,
	enum rf_path e_rf_path,
	u32 delay_time)
{
	#ifdef RTW_HALMAC
	struct _ADAPTER *adapter = p_dm_odm->adapter;
	struct rtw_phy_parameter para;

	switch (config_type) {
	case PHYDM_HALMAC_CMD_MAC_W8:
		para.cmd = 0; /* MAC register */
		para.data.mac.offset = offset;
		para.data.mac.value = data;
		para.data.mac.msk = mask;
		para.data.mac.msk_en = 1;
		para.data.mac.size = 1;
	break;
	case PHYDM_HALMAC_CMD_MAC_W16:
		para.cmd = 0; /* MAC register */
		para.data.mac.offset = offset;
		para.data.mac.value = data;
		para.data.mac.msk = mask;
		para.data.mac.msk_en = 1;
		para.data.mac.size = 2;
	break;
	case PHYDM_HALMAC_CMD_MAC_W32:
		para.cmd = 0; /* MAC register */
		para.data.mac.offset = offset;
		para.data.mac.value = data;
		para.data.mac.msk = mask;
		para.data.mac.msk_en = 1;
		para.data.mac.size = 4;
	break;
	case PHYDM_HALMAC_CMD_BB_W8:
		para.cmd = 1; /* BB register */
		para.data.bb.offset = offset;
		para.data.bb.value = data;
		para.data.bb.msk = mask;
		para.data.bb.msk_en = 1;
		para.data.bb.size = 1;
	break;
	case PHYDM_HALMAC_CMD_BB_W16:
		para.cmd = 1; /* BB register */
		para.data.bb.offset = offset;
		para.data.bb.value = data;
		para.data.bb.msk = mask;
		para.data.bb.msk_en = 1;
		para.data.bb.size = 2;
	break;
	case PHYDM_HALMAC_CMD_BB_W32:
		para.cmd = 1; /* BB register */
		para.data.bb.offset = offset;
		para.data.bb.value = data;
		para.data.bb.msk = mask;
		para.data.bb.msk_en = 1;
		para.data.bb.size = 4;
	break;
	case PHYDM_HALMAC_CMD_RF_W:
		para.cmd = 2; /* RF register */
		para.data.rf.offset = offset;
		para.data.rf.value = data;
		para.data.rf.msk = mask;
		para.data.rf.msk_en = 1;
		if (e_rf_path == RF_PATH_A)
			para.data.rf.path = 0;
		else if (e_rf_path == RF_PATH_B)
			para.data.rf.path = 1;
		else if (e_rf_path == RF_PATH_C)
			para.data.rf.path = 2;
		else if (e_rf_path == RF_PATH_D)
			para.data.rf.path = 3;
		else
			para.data.rf.path = 0;
	break;
	case PHYDM_HALMAC_CMD_DELAY_US:
		para.cmd = 3; /* Delay */
		para.data.delay.unit = 0; /* microsecond */
		para.data.delay.value = delay_time;
	break;
	case PHYDM_HALMAC_CMD_DELAY_MS:
		para.cmd = 3; /* Delay */
		para.data.delay.unit = 1; /* millisecond */
		para.data.delay.value = delay_time;
	break;
	case PHYDM_HALMAC_CMD_END:
		para.cmd = 0xFF; /* End command */
	break;
	default:
		return HAL_STATUS_FAILURE;
	}

	if (rtw_halmac_cfg_phy_para(adapter_to_dvobj(adapter), &para))
		return HAL_STATUS_FAILURE;
	#endif /*RTW_HALMAC*/
	return HAL_STATUS_SUCCESS;
}


#ifdef CONFIG_LPS_LCLK_WD_TIMER
void rtw_phydm_wd_lps_lclk_hdl(_adapter *adapter)
{
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(adapter);
	struct PHY_DM_STRUCT	*podmpriv = &(pHalData->odmpriv);
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct sta_info *psta = NULL;
	u8 rssi_min = 0;
	u32	rssi_rpt = 0;
	bool is_linked = _FALSE;

	if (!rtw_is_hw_init_completed(adapter))
		return;

	if (rtw_mi_check_status(adapter, MI_ASSOC))
		is_linked = _TRUE;

	if (is_linked == _FALSE)
		return;

	psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));
	if (psta == NULL)
		return;

	odm_cmn_info_update(&pHalData->odmpriv, ODM_CMNINFO_LINK, is_linked);

	phydm_watchdog_lps_32k(&pHalData->odmpriv);
}

void rtw_phydm_watchdog_in_lps_lclk(_adapter *adapter)
{
	struct mlme_priv	*pmlmepriv = &adapter->mlmepriv;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct sta_info *psta = NULL;
	u8 cur_igi = 0;
	s8 min_rssi = 0;

	if (!rtw_is_hw_init_completed(adapter))
		return;

	psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));
	if (psta == NULL)
		return;

	cur_igi = rtw_phydm_get_cur_igi(adapter);
	min_rssi = rtw_phydm_get_min_rssi(adapter);
	if (min_rssi <= 0)
		min_rssi = psta->cmn.rssi_stat.rssi;
	/*RTW_INFO("%s "ADPT_FMT" cur_ig_value=%d, min_rssi = %d\n", __func__,  ADPT_ARG(adapter), cur_igi, min_rssi);*/

	if (min_rssi <= 0)
		return;

	if ((cur_igi > min_rssi + 5) ||
		(cur_igi < min_rssi - 5)) {
#ifdef CONFIG_LPS
		rtw_dm_in_lps_wk_cmd(adapter);
#endif
	}
}
#endif /*CONFIG_LPS_LCLK_WD_TIMER*/

void dump_sta_traffic(void *sel, _adapter *adapter, struct sta_info *psta)
{
	struct ra_sta_info *ra_info;
	u8 curr_sgi = _FALSE;

	if (!psta)
		return;
	RTW_PRINT_SEL(sel, "====== mac_id : %d ======\n", psta->cmn.mac_id);

	ra_info = &psta->cmn.ra_info;
	curr_sgi = (ra_info->curr_tx_rate & 0x80) ? _TRUE : _FALSE;
	RTW_PRINT_SEL(sel, "tx_rate : %s(%s)  rx_rate : %s, rx_rate_bmc : %s, rssi : %d %%\n"
		, HDATA_RATE((ra_info->curr_tx_rate & 0x7F)), (curr_sgi) ? "S" : "L"
		, HDATA_RATE((psta->curr_rx_rate & 0x7F)), HDATA_RATE((psta->curr_rx_rate_bmc & 0x7F)), psta->cmn.rssi_stat.rssi
	);

	if (0) {
		RTW_PRINT_SEL(sel, "tx_bytes:%llu(%llu - %llu)\n"
			, psta->sta_stats.tx_bytes - psta->sta_stats.last_tx_bytes
			, psta->sta_stats.tx_bytes, psta->sta_stats.last_tx_bytes
		);
		RTW_PRINT_SEL(sel, "rx_uc_bytes:%llu(%llu - %llu)\n"
			, sta_rx_uc_bytes(psta) - sta_last_rx_uc_bytes(psta)
			, sta_rx_uc_bytes(psta), sta_last_rx_uc_bytes(psta)
		);
		RTW_PRINT_SEL(sel, "rx_mc_bytes:%llu(%llu - %llu)\n"
			, psta->sta_stats.rx_mc_bytes - psta->sta_stats.last_rx_mc_bytes
			, psta->sta_stats.rx_mc_bytes, psta->sta_stats.last_rx_mc_bytes
		);
		RTW_PRINT_SEL(sel, "rx_bc_bytes:%llu(%llu - %llu)\n"
			, psta->sta_stats.rx_bc_bytes - psta->sta_stats.last_rx_bc_bytes
			, psta->sta_stats.rx_bc_bytes, psta->sta_stats.last_rx_bc_bytes
		);
	}

	RTW_PRINT_SEL(sel, "TP {Tx,Rx,Total} = { %d , %d , %d } Mbps\n",
		(psta->sta_stats.tx_tp_mbytes << 3), (psta->sta_stats.rx_tp_mbytes << 3),
		(psta->sta_stats.tx_tp_mbytes + psta->sta_stats.rx_tp_mbytes) << 3);

	RTW_PRINT_SEL(sel, "Moving-AVG TP {Tx,Rx,Total} = { %d , %d , %d } Mbps\n\n",
		(psta->cmn.tx_moving_average_tp << 3), (psta->cmn.rx_moving_average_tp << 3),
		(psta->cmn.tx_moving_average_tp + psta->cmn.rx_moving_average_tp) << 3);

}

void dump_sta_info(void *sel, struct sta_info *psta)
{
	struct ra_sta_info *ra_info;
	u8 curr_tx_sgi = _FALSE;
	u8 curr_tx_rate = 0;

	if (!psta)
		return;

	ra_info = &psta->cmn.ra_info;

	RTW_PRINT_SEL(sel, "============ STA [" MAC_FMT "]  ===================\n",
		MAC_ARG(psta->cmn.mac_addr));
	RTW_PRINT_SEL(sel, "mac_id : %d\n", psta->cmn.mac_id);
	RTW_PRINT_SEL(sel, "wireless_mode : 0x%02x\n", psta->wireless_mode);
	RTW_PRINT_SEL(sel, "mimo_type : %d\n", psta->cmn.mimo_type);
	RTW_PRINT_SEL(sel, "bw_mode : %s, ra_bw_mode : %s\n",
			ch_width_str(psta->cmn.bw_mode), ch_width_str(ra_info->ra_bw_mode));
	RTW_PRINT_SEL(sel, "rate_id : %d\n", ra_info->rate_id);
	RTW_PRINT_SEL(sel, "rssi : %d (%%), rssi_level : %d\n", psta->cmn.rssi_stat.rssi, ra_info->rssi_level);
	RTW_PRINT_SEL(sel, "is_support_sgi : %s, is_vht_enable : %s\n",
			(ra_info->is_support_sgi) ? "Y" : "N", (ra_info->is_vht_enable) ? "Y" : "N");
	RTW_PRINT_SEL(sel, "disable_ra : %s, disable_pt : %s\n",
				(ra_info->disable_ra) ? "Y" : "N", (ra_info->disable_pt) ? "Y" : "N");
	RTW_PRINT_SEL(sel, "is_noisy : %s\n", (ra_info->is_noisy) ? "Y" : "N");
	RTW_PRINT_SEL(sel, "txrx_state : %d\n", ra_info->txrx_state);/*0: uplink, 1:downlink, 2:bi-direction*/

	curr_tx_sgi = (ra_info->curr_tx_rate & 0x80) ? _TRUE : _FALSE;
	curr_tx_rate = ra_info->curr_tx_rate & 0x7F;
	RTW_PRINT_SEL(sel, "curr_tx_rate : %s (%s)\n",
			HDATA_RATE(curr_tx_rate), (curr_tx_sgi) ? "S" : "L");
	RTW_PRINT_SEL(sel, "curr_tx_bw : %s\n", ch_width_str(ra_info->curr_tx_bw));
	RTW_PRINT_SEL(sel, "curr_retry_ratio : %d\n", ra_info->curr_retry_ratio);
	RTW_PRINT_SEL(sel, "ra_mask : 0x%016llx\n\n", ra_info->ramask);
}

void rtw_phydm_ra_registed(_adapter *adapter, struct sta_info *psta)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);

	if (psta == NULL) {
		RTW_ERR(FUNC_ADPT_FMT" sta is NULL\n", FUNC_ADPT_ARG(adapter));
		rtw_warn_on(1);
		return;
	}

	phydm_ra_registed(&hal_data->odmpriv, psta->cmn.mac_id, psta->cmn.rssi_stat.rssi);
	if (_DRV_DEBUG_ <= rtw_drv_log_level)
		dump_sta_info(RTW_DBGDUMP, psta);
}

#ifdef CONFIG_LPS_PG
static void _lps_pg_state_update(_adapter *adapter)
{
	u8	is_in_lpspg = _FALSE;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(adapter);
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct sta_info *psta = NULL;

	if ((pwrpriv->lps_level == LPS_PG) && (pwrpriv->pwr_mode != PS_MODE_ACTIVE) && (pwrpriv->rpwm <= PS_STATE_S2))
		is_in_lpspg = _TRUE;
	psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));

	if (psta)
		psta->cmn.ra_info.disable_ra = (is_in_lpspg) ? _TRUE : _FALSE;
}
#endif

/*#define DBG_PHYDM_STATE_CHK*/


static u8 _rtw_phydm_rfk_condition_check(_adapter *adapter)
{
	u8 rst = _FALSE;

	if (rtw_mi_stayin_union_ch_chk(adapter))
		rst = _TRUE;

	#ifdef CONFIG_MCC_MODE
	/*not in MCC State*/
	if (MCC_EN(adapter))
		if (!rtw_hal_check_mcc_status(adapter, MCC_STATUS_DOING_MCC))
			rst = _TRUE;
	#endif

	#if defined(CONFIG_TDLS) && defined(CONFIG_TDLS_CH_SW)

	#endif

	return rst;
}
#if ((RTL8822B_SUPPORT == 1) || (RTL8821C_SUPPORT == 1) || (RTL8814B_SUPPORT == 1))
static u8 _rtw_phydm_iqk_segment_chk(_adapter *adapter)
{
	u8 rst = _FALSE;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);

#if 0
	if (dvobj->traffic_stat.cur_tx_tp > 2 || dvobj->traffic_stat.cur_rx_tp > 2)
		rst = _TRUE;
#else
	rst = _TRUE;
#endif
	return rst;
}
#endif

/*check the tx low rate while unlinked to any AP;for pwr tracking */
static u8 _rtw_phydm_pwr_tracking_rate_check(_adapter *adapter)
{
	int i;
	_adapter *iface;
	u8		if_tx_rate = 0xFF;
	u8		tx_rate = 0xFF;
	struct mlme_ext_priv	*pmlmeext = NULL;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		pmlmeext = &(iface->mlmeextpriv);
		if ((iface) && rtw_is_adapter_up(iface)) {
#ifdef CONFIG_P2P
			if (!rtw_p2p_chk_role(&(iface)->wdinfo, P2P_ROLE_DISABLE))
				if_tx_rate = IEEE80211_OFDM_RATE_6MB;
			else
#endif
				if_tx_rate = pmlmeext->tx_rate;
			if(if_tx_rate < tx_rate)
				tx_rate = if_tx_rate;

			RTW_DBG("%s i=%d tx_rate =0x%x\n", __func__, i, if_tx_rate);
		}
	}
	RTW_DBG("%s tx_low_rate (unlinked to any AP)=0x%x\n", __func__, tx_rate);
	return tx_rate;
}

void rtw_phydm_watchdog(_adapter *adapter)
{
	u8	bLinked = _FALSE;
	u8	bsta_state = _FALSE;
	u8	bBtDisabled = _TRUE;
	u8	rfk_forbidden = _TRUE;
	u8	segment_iqk = _TRUE;
	u8	tx_unlinked_low_rate = 0xFF;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(adapter);
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);

	if (!rtw_is_hw_init_completed(adapter)) {
		RTW_DBG("%s skip due to hw_init_completed == FALSE\n", __func__);
		return;
	}
	if (rtw_mi_check_fwstate(adapter, _FW_UNDER_SURVEY))
		pHalData->bScanInProcess = _TRUE;
	else
		pHalData->bScanInProcess = _FALSE;

	if (rtw_mi_check_status(adapter, MI_ASSOC)) {
		bLinked = _TRUE;
		if (rtw_mi_check_status(adapter, MI_STA_LINKED))
		bsta_state = _TRUE;
	}

	odm_cmn_info_update(&pHalData->odmpriv, ODM_CMNINFO_LINK, bLinked);
	odm_cmn_info_update(&pHalData->odmpriv, ODM_CMNINFO_STATION_STATE, bsta_state);

#ifdef CONFIG_BT_COEXIST
	bBtDisabled = rtw_btcoex_IsBtDisabled(adapter);
#endif /* CONFIG_BT_COEXIST */
	odm_cmn_info_update(&pHalData->odmpriv, ODM_CMNINFO_BT_ENABLED,
							(bBtDisabled == _TRUE) ? _FALSE : _TRUE);
	odm_cmn_info_update(&pHalData->odmpriv, ODM_CMNINFO_POWER_TRAINING,
							(pHalData->bDisableTXPowerTraining) ? _TRUE : _FALSE);
#ifdef CONFIG_LPS_PG
	_lps_pg_state_update(adapter);
#endif

	if (bLinked == _TRUE) {
		rfk_forbidden = (_rtw_phydm_rfk_condition_check(adapter) == _TRUE) ? _FALSE : _TRUE;
		halrf_cmn_info_set(&pHalData->odmpriv, HALRF_CMNINFO_RFK_FORBIDDEN, rfk_forbidden);

		#if ((RTL8822B_SUPPORT == 1) || (RTL8821C_SUPPORT == 1) || (RTL8814B_SUPPORT == 1))
		segment_iqk = _rtw_phydm_iqk_segment_chk(adapter);
		halrf_cmn_info_set(&pHalData->odmpriv, HALRF_CMNINFO_IQK_SEGMENT, segment_iqk);
		#endif
	} else {
		tx_unlinked_low_rate = _rtw_phydm_pwr_tracking_rate_check(adapter);
		halrf_cmn_info_set(&pHalData->odmpriv, HALRF_CMNINFO_RATE_INDEX, tx_unlinked_low_rate);
	}
#ifdef DBG_PHYDM_STATE_CHK
	RTW_INFO("%s rfk_forbidden = %s, segment_iqk = %s\n",
		__func__, (rfk_forbidden) ? "Y" : "N", (segment_iqk) ? "Y" : "N");
#endif

	/*if (!rtw_mi_stayin_union_band_chk(adapter)) {
		#ifdef DBG_PHYDM_STATE_CHK
		RTW_ERR("Not stay in union band, skip phydm\n");
		#endif
		goto _exit;
	}*/
	if (pwrctl->bpower_saving)
		phydm_watchdog_lps(&pHalData->odmpriv);
	else
		phydm_watchdog(&pHalData->odmpriv);

	#ifdef CONFIG_RTW_ACS
	rtw_acs_update_current_info(adapter);
	#endif

_exit:
	return;
}

