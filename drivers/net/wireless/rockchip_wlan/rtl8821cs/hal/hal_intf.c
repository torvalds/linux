/* SPDX-License-Identifier: GPL-2.0 */
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

#define _HAL_INTF_C_

#include <drv_types.h>
#include <hal_data.h>

const u32 _chip_type_to_odm_ic_type[] = {
	0,
	ODM_RTL8188E,
	ODM_RTL8192E,
	ODM_RTL8812,
	ODM_RTL8821,
	ODM_RTL8723B,
	ODM_RTL8814A,
	ODM_RTL8703B,
	ODM_RTL8188F,
	ODM_RTL8188F,
	ODM_RTL8822B,
	ODM_RTL8723D,
	ODM_RTL8821C,
	ODM_RTL8710B,
	ODM_RTL8192F,
	ODM_RTL8822C,
	ODM_RTL8814B,
/*	ODM_RTL8723F,  */
	0,
};

void rtw_hal_chip_configure(_adapter *padapter)
{
	padapter->hal_func.intf_chip_configure(padapter);
}

/*
 * Description:
 *	Read chip internal ROM data
 *
 * Return:
 *	_SUCCESS success
 *	_FAIL	 fail
 */
u8 rtw_hal_read_chip_info(_adapter *padapter)
{
	u8 rtn = _SUCCESS;
	u8 hci_type = rtw_get_intf_type(padapter);
	systime start = rtw_get_current_time();

	/*  before access eFuse, make sure card enable has been called */
	if ((hci_type == RTW_SDIO || hci_type == RTW_GSPI)
	    && !rtw_is_hw_init_completed(padapter))
		rtw_hal_power_on(padapter);

	rtn = padapter->hal_func.read_adapter_info(padapter);

	if ((hci_type == RTW_SDIO || hci_type == RTW_GSPI)
	    && !rtw_is_hw_init_completed(padapter))
		rtw_hal_power_off(padapter);

	RTW_INFO("%s in %d ms\n", __func__, rtw_get_passing_time_ms(start));

	return rtn;
}

void rtw_hal_read_chip_version(_adapter *padapter)
{
	padapter->hal_func.read_chip_version(padapter);
	rtw_odm_init_ic_type(padapter);
}

static void rtw_init_wireless_mode(_adapter *padapter)
{
	u8 proto_wireless_mode = 0;
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(padapter);
	if(hal_spec->proto_cap & PROTO_CAP_11B)
		proto_wireless_mode |= WIRELESS_11B;
	
	if(hal_spec->proto_cap & PROTO_CAP_11G)
		proto_wireless_mode |= WIRELESS_11G;
#ifdef CONFIG_80211AC_VHT
	if(hal_spec->band_cap & BAND_CAP_5G)
		proto_wireless_mode |= WIRELESS_11A;
#endif

#ifdef CONFIG_80211N_HT
	if(hal_spec->proto_cap & PROTO_CAP_11N) {

		if(hal_spec->band_cap & BAND_CAP_2G)
			proto_wireless_mode |= WIRELESS_11_24N;
		if(hal_spec->band_cap & BAND_CAP_5G)
			proto_wireless_mode |= WIRELESS_11_5N;
	}
#endif

#ifdef CONFIG_80211AC_VHT
	if(hal_spec->proto_cap & PROTO_CAP_11AC) 
		proto_wireless_mode |= WIRELESS_11AC;
#endif
	padapter->registrypriv.wireless_mode &= proto_wireless_mode;
}

void rtw_hal_def_value_init(_adapter *padapter)
{
	if (is_primary_adapter(padapter)) {
		/*init fw_psmode_iface_id*/
		adapter_to_pwrctl(padapter)->fw_psmode_iface_id = 0xff;
		/*wireless_mode*/
		rtw_init_wireless_mode(padapter);
		padapter->hal_func.init_default_value(padapter);

		rtw_init_hal_com_default_value(padapter);
		
		#ifdef CONFIG_FW_MULTI_PORT_SUPPORT
		adapter_to_dvobj(padapter)->dft.port_id = 0xFF;
		adapter_to_dvobj(padapter)->dft.mac_id = 0xFF;
		#endif
		#ifdef CONFIG_HW_P0_TSF_SYNC
		adapter_to_dvobj(padapter)->p0_tsf.sync_port = MAX_HW_PORT;
		adapter_to_dvobj(padapter)->p0_tsf.offset = 0;
		#endif

		GET_HAL_DATA(padapter)->rx_tsf_addr_filter_config = 0;
	}
}

u8 rtw_hal_data_init(_adapter *padapter)
{
	if (is_primary_adapter(padapter)) {
		padapter->hal_data_sz = sizeof(HAL_DATA_TYPE);
		padapter->HalData = rtw_zvmalloc(padapter->hal_data_sz);
		if (padapter->HalData == NULL) {
			RTW_INFO("cant not alloc memory for HAL DATA\n");
			return _FAIL;
		}
		rtw_phydm_priv_init(padapter);
	}
	return _SUCCESS;
}

void rtw_hal_data_deinit(_adapter *padapter)
{
	if (is_primary_adapter(padapter)) {
		if (padapter->HalData) {
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
			phy_free_filebuf(padapter);
#endif
			rtw_vmfree(padapter->HalData, padapter->hal_data_sz);
			padapter->HalData = NULL;
			padapter->hal_data_sz = 0;
		}
	}
}

void	rtw_hal_free_data(_adapter *padapter)
{
	/* free HAL Data	 */
	rtw_hal_data_deinit(padapter);
}
void rtw_hal_dm_init(_adapter *padapter)
{
	if (is_primary_adapter(padapter)) {
		PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

		padapter->hal_func.dm_init(padapter);

		_rtw_spinlock_init(&pHalData->IQKSpinLock);

		#ifdef CONFIG_TXPWR_PG_WITH_PWR_IDX
		if (pHalData->txpwr_pg_mode == TXPWR_PG_WITH_PWR_IDX)
			hal_load_txpwr_info(padapter);
		#endif
		phy_load_tx_power_ext_info(padapter, 1);
	}
}
void rtw_hal_dm_deinit(_adapter *padapter)
{
	if (is_primary_adapter(padapter)) {
		PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

		padapter->hal_func.dm_deinit(padapter);

		_rtw_spinlock_free(&pHalData->IQKSpinLock);
	}
}

enum rf_type rtw_chip_rftype_to_hal_rftype(_adapter *adapter, u8 limit)
{
	PHAL_DATA_TYPE hal_data = GET_HAL_DATA(adapter);
	u8 tx_num = 0, rx_num = 0;

	/*get RF PATH from version_id.RF_TYPE */
	if (IS_1T1R(hal_data->version_id)) {
		tx_num = 1;
		rx_num = 1;
	} else if (IS_1T2R(hal_data->version_id)) {
		tx_num = 1;
		rx_num = 2;
	} else if (IS_2T2R(hal_data->version_id)) {
		tx_num = 2;
		rx_num = 2;
	} else if (IS_2T3R(hal_data->version_id)) {
		tx_num = 2;
		rx_num = 3;
	} else if (IS_2T4R(hal_data->version_id)) {
		tx_num = 2;
		rx_num = 4;
	} else if (IS_3T3R(hal_data->version_id)) {
		tx_num = 3;
		rx_num = 3;
	} else if (IS_3T4R(hal_data->version_id)) {
		tx_num = 3;
		rx_num = 4;
	} else if (IS_4T4R(hal_data->version_id)) {
		tx_num = 4;
		rx_num = 4;
	}

	if (limit) {
		tx_num = rtw_min(tx_num, limit);
		rx_num = rtw_min(rx_num, limit);
	}

	return trx_num_to_rf_type(tx_num, rx_num);
}

void dump_hal_runtime_trx_mode(void *sel, _adapter *adapter)
{
	struct registry_priv *regpriv = &adapter->registrypriv;
	PHAL_DATA_TYPE hal_data = GET_HAL_DATA(adapter);
	int i;

	RTW_PRINT_SEL(sel, "txpath=0x%x, rxpath=0x%x\n", hal_data->txpath, hal_data->rxpath);
	for (i = 0; i < hal_data->tx_nss; i++)
		RTW_PRINT_SEL(sel, "txpath_%uss:0x%x, num:%u\n"
			, i + 1, hal_data->txpath_nss[i]
			, hal_data->txpath_num_nss[i]);
}

void dump_hal_trx_mode(void *sel, _adapter *adapter)
{
	struct registry_priv *regpriv = &adapter->registrypriv;
	PHAL_DATA_TYPE hal_data = GET_HAL_DATA(adapter);
	int i;

	RTW_PRINT_SEL(sel, "trx_path_bmp:0x%02x(%s), NumTotalRFPath:%u, max_tx_cnt:%u\n"
		, hal_data->trx_path_bmp
		, rf_type_to_rfpath_str(hal_data->rf_type)
		, hal_data->NumTotalRFPath
		, hal_data->max_tx_cnt
	);
	RTW_PRINT_SEL(sel, "tx_nss:%u, rx_nss:%u\n"
		, hal_data->tx_nss, hal_data->rx_nss);
	for (i = 0; i < hal_data->tx_nss; i++)
		RTW_PRINT_SEL(sel, "txpath_cap_num_%uss:%u\n"
			, i + 1, hal_data->txpath_cap_num_nss[i]);
	RTW_PRINT_SEL(sel, "\n");

	dump_hal_runtime_trx_mode(sel, adapter);
}

void _dump_rf_path(void *sel, _adapter *adapter)
{
	PHAL_DATA_TYPE hal_data = GET_HAL_DATA(adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	struct registry_priv *regsty = adapter_to_regsty(adapter);

	RTW_PRINT_SEL(sel, "[RF_PATH] ver_id.RF_TYPE:%s\n"
		, rf_type_to_rfpath_str(rtw_chip_rftype_to_hal_rftype(adapter, 0)));
	RTW_PRINT_SEL(sel, "[RF_PATH] HALSPEC's rf_reg_trx_path_bmp:0x%02x, rf_reg_path_avail_num:%u, max_tx_cnt:%u\n"
		, hal_spec->rf_reg_trx_path_bmp, hal_spec->rf_reg_path_avail_num, hal_spec->max_tx_cnt);
	RTW_PRINT_SEL(sel, "[RF_PATH] PG's trx_path_bmp:0x%02x, max_tx_cnt:%u\n"
		, hal_data->eeprom_trx_path_bmp, hal_data->eeprom_max_tx_cnt);
	RTW_PRINT_SEL(sel, "[RF_PATH] Registry's trx_path_bmp:0x%02x, tx_path_lmt:%u, rx_path_lmt:%u\n"
		, regsty->trx_path_bmp, regsty->tx_path_lmt, regsty->rx_path_lmt);
	RTW_PRINT_SEL(sel, "[RF_PATH] HALDATA's trx_path_bmp:0x%02x, max_tx_cnt:%u\n"
		, hal_data->trx_path_bmp, hal_data->max_tx_cnt);
	RTW_PRINT_SEL(sel, "[RF_PATH] HALDATA's rf_type:%s, NumTotalRFPath:%d\n"
		, rf_type_to_rfpath_str(hal_data->rf_type), hal_data->NumTotalRFPath);
}

#ifdef CONFIG_RTL8814A
extern enum rf_type rtl8814a_rfpath_decision(_adapter *adapter);
#endif

u8 rtw_hal_rfpath_init(_adapter *adapter)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);

#ifdef CONFIG_RTL8814A
if (IS_HARDWARE_TYPE_8814A(adapter)) {
	enum bb_path tx_bmp, rx_bmp;
	hal_data->rf_type = rtl8814a_rfpath_decision(adapter);
	rf_type_to_default_trx_bmp(hal_data->rf_type, &tx_bmp, &rx_bmp);
	hal_data->trx_path_bmp = (tx_bmp << 4) | rx_bmp;
	hal_data->NumTotalRFPath = 4;
	hal_data->max_tx_cnt = hal_spec->max_tx_cnt;
	hal_data->max_tx_cnt = rtw_min(hal_data->max_tx_cnt, rf_type_to_rf_tx_cnt(hal_data->rf_type));
} else
#endif
{
	struct registry_priv *regsty = adapter_to_regsty(adapter);
	u8 trx_path_bmp;
	u8 tx_path_num;
	u8 rx_path_num;
	int i;

	trx_path_bmp = hal_spec->rf_reg_trx_path_bmp;
	
	if (regsty->trx_path_bmp != 0x00) {
		/* restrict trx_path_bmp with regsty.trx_path_bmp */
		trx_path_bmp &= regsty->trx_path_bmp;
		if (!trx_path_bmp) {
			RTW_ERR("%s hal_spec.rf_reg_trx_path_bmp:0x%02x, regsty->trx_path_bmp:0x%02x no intersection\n"
				, __func__, hal_spec->rf_reg_trx_path_bmp, regsty->trx_path_bmp);
			return _FAIL;
		}
	} else if (hal_data->eeprom_trx_path_bmp != 0x00) {
		/* restrict trx_path_bmp with eeprom_trx_path_bmp */
		trx_path_bmp &= hal_data->eeprom_trx_path_bmp;
		if (!trx_path_bmp) {
			RTW_ERR("%s hal_spec.rf_reg_trx_path_bmp:0x%02x, hal_data->eeprom_trx_path_bmp:0x%02x no intersection\n"
				, __func__, hal_spec->rf_reg_trx_path_bmp, hal_data->eeprom_trx_path_bmp);
			return _FAIL;
		}
	}

	/* restrict trx_path_bmp with TX and RX num limit */
	trx_path_bmp = rtw_restrict_trx_path_bmp_by_trx_num_lmt(trx_path_bmp
		, regsty->tx_path_lmt, regsty->rx_path_lmt, &tx_path_num, &rx_path_num);
	if (!trx_path_bmp) {
		RTW_ERR("%s rtw_restrict_trx_path_bmp_by_trx_num_lmt(0x%02x, %u, %u) failed\n"
			, __func__, trx_path_bmp, regsty->tx_path_lmt, regsty->rx_path_lmt);
		return _FAIL;
	}
	hal_data->trx_path_bmp = trx_path_bmp;
	hal_data->rf_type = trx_bmp_to_rf_type((trx_path_bmp & 0xF0) >> 4, trx_path_bmp & 0x0F);
	hal_data->NumTotalRFPath = rtw_max(tx_path_num, rx_path_num);

	hal_data->max_tx_cnt = hal_spec->max_tx_cnt;
	hal_data->max_tx_cnt = rtw_min(hal_data->max_tx_cnt, tx_path_num);
	if (hal_data->eeprom_max_tx_cnt)
		hal_data->max_tx_cnt = rtw_min(hal_data->max_tx_cnt, hal_data->eeprom_max_tx_cnt);

	if (1)
		_dump_rf_path(RTW_DBGDUMP, adapter);
}

	RTW_INFO("%s trx_path_bmp:0x%02x(%s), NumTotalRFPath:%u, max_tx_cnt:%u\n"
		, __func__
		, hal_data->trx_path_bmp
		, rf_type_to_rfpath_str(hal_data->rf_type)
		, hal_data->NumTotalRFPath
		, hal_data->max_tx_cnt);

	return _SUCCESS;
}

void _dump_trx_nss(void *sel, _adapter *adapter)
{
	struct registry_priv *regpriv = &adapter->registrypriv;
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);

	RTW_PRINT_SEL(sel, "[TRX_Nss] HALSPEC - tx_nss:%d, rx_nss:%d\n", hal_spec->tx_nss_num, hal_spec->rx_nss_num);
	RTW_PRINT_SEL(sel, "[TRX_Nss] Registry - tx_nss:%d, rx_nss:%d\n", regpriv->tx_nss, regpriv->rx_nss);
	RTW_PRINT_SEL(sel, "[TRX_Nss] HALDATA - tx_nss:%d, rx_nss:%d\n", GET_HAL_TX_NSS(adapter), GET_HAL_RX_NSS(adapter));

}
#define NSS_VALID(nss) (nss > 0)

u8 rtw_hal_trxnss_init(_adapter *adapter)
{
	struct registry_priv *regpriv = &adapter->registrypriv;
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	PHAL_DATA_TYPE hal_data = GET_HAL_DATA(adapter);
	enum rf_type rf_path = GET_HAL_RFPATH(adapter);
	int i;

	hal_data->tx_nss = hal_spec->tx_nss_num;
	hal_data->rx_nss = hal_spec->rx_nss_num;

	if (NSS_VALID(regpriv->tx_nss))
		hal_data->tx_nss = rtw_min(hal_data->tx_nss, regpriv->tx_nss);
	hal_data->tx_nss = rtw_min(hal_data->tx_nss, hal_data->max_tx_cnt);
	if (NSS_VALID(regpriv->rx_nss))
		hal_data->rx_nss = rtw_min(hal_data->rx_nss, regpriv->rx_nss);
	hal_data->rx_nss = rtw_min(hal_data->rx_nss, rf_type_to_rf_rx_cnt(rf_path));

	for (i = 0; i < 4; i++) {
		if (hal_data->tx_nss < i + 1)
			break;

		if (IS_HARDWARE_TYPE_8814B(adapter) /* 8814B is always full-TX */
			#ifdef CONFIG_RTW_TX_NPATH_EN
			/* these IC is capable of full-TX when macro defined */
			|| IS_HARDWARE_TYPE_8192E(adapter) || IS_HARDWARE_TYPE_8192F(adapter)
			|| IS_HARDWARE_TYPE_8812(adapter) || IS_HARDWARE_TYPE_8822B(adapter)
			|| IS_HARDWARE_TYPE_8822C(adapter)
			#endif
		)
			hal_data->txpath_cap_num_nss[i] = hal_data->max_tx_cnt;
		else
			hal_data->txpath_cap_num_nss[i] = i + 1;
	}

	if (1)
		_dump_trx_nss(RTW_DBGDUMP, adapter);

	RTW_INFO("%s tx_nss:%u, rx_nss:%u\n", __func__
		, hal_data->tx_nss, hal_data->rx_nss);

	return _SUCCESS;
}

#ifdef CONFIG_RTW_SW_LED
void rtw_hal_sw_led_init(_adapter *padapter)
{
	struct led_priv *ledpriv = adapter_to_led(padapter);

	if (ledpriv->bRegUseLed == _FALSE)
		return;

	if (!is_primary_adapter(padapter))
		return;

	if (padapter->hal_func.InitSwLeds) {
		padapter->hal_func.InitSwLeds(padapter);
		rtw_led_set_ctl_en_mask_primary(padapter);
		rtw_led_set_iface_en(padapter, 1);
	}
}

void rtw_hal_sw_led_deinit(_adapter *padapter)
{
	struct led_priv *ledpriv = adapter_to_led(padapter);

	if (ledpriv->bRegUseLed == _FALSE)
		return;

	if (!is_primary_adapter(padapter))
		return;

	if (padapter->hal_func.DeInitSwLeds)
		padapter->hal_func.DeInitSwLeds(padapter);
}
#endif

u32 rtw_hal_power_on(_adapter *padapter)
{
	u32 ret = 0;
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);

	ret = padapter->hal_func.hal_power_on(padapter);

#ifdef CONFIG_BT_COEXIST
	if ((ret == _SUCCESS) && (pHalData->EEPROMBluetoothCoexist == _TRUE))
		rtw_btcoex_PowerOnSetting(padapter);
#endif

	return ret;
}
void rtw_hal_power_off(_adapter *padapter)
{
	struct macid_ctl_t *macid_ctl = &padapter->dvobj->macid_ctl;

	_rtw_memset(macid_ctl->h2c_msr, 0, MACID_NUM_SW_LIMIT);
	_rtw_memset(macid_ctl->op_num, 0, H2C_MSR_ROLE_MAX);

#ifdef CONFIG_LPS_1T1R
	GET_HAL_DATA(padapter)->lps_1t1r = 0;
#endif

#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_PowerOffSetting(padapter);
#endif

	padapter->hal_func.hal_power_off(padapter);
}


void rtw_hal_init_opmode(_adapter *padapter)
{
	NDIS_802_11_NETWORK_INFRASTRUCTURE networkType = Ndis802_11InfrastructureMax;
	struct  mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	sint fw_state;

	fw_state = get_fwstate(pmlmepriv);

	if (fw_state & WIFI_ADHOC_STATE)
		networkType = Ndis802_11IBSS;
	else if (fw_state & WIFI_STATION_STATE)
		networkType = Ndis802_11Infrastructure;
#ifdef CONFIG_AP_MODE
	else if (fw_state & WIFI_AP_STATE)
		networkType = Ndis802_11APMode;
#endif
#ifdef CONFIG_RTW_MESH
	else if (fw_state & WIFI_MESH_STATE)
		networkType = Ndis802_11_mesh;
#endif
	else
		return;

	rtw_setopmode_cmd(padapter, networkType, RTW_CMDF_DIRECTLY);
}

#ifdef CONFIG_NEW_NETDEV_HDL
uint rtw_hal_iface_init(_adapter *adapter)
{
	uint status = _SUCCESS;

	rtw_hal_set_hwreg(adapter, HW_VAR_MAC_ADDR, adapter_mac_addr(adapter));
	#ifdef RTW_HALMAC
	rtw_hal_hw_port_enable(adapter);
	#endif
	rtw_sec_restore_wep_key(adapter);
	rtw_hal_init_opmode(adapter);
	rtw_hal_start_thread(adapter);
	return status;
}
uint rtw_hal_init(_adapter *padapter)
{
	uint status = _SUCCESS;
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);

	halrf_set_rfsupportability(adapter_to_phydm(padapter));

	status = padapter->hal_func.hal_init(padapter);

	if(pHalData ->phydm_init_result) {

		status = _FAIL;
		RTW_ERR("%s phydm init fail reason=%u \n",
			__func__,
			pHalData ->phydm_init_result);
	}

	if (status == _SUCCESS) {
		rtw_set_hw_init_completed(padapter, _TRUE);
		if (padapter->registrypriv.notch_filter == 1)
			rtw_hal_notch_filter(padapter, 1);
		rtw_led_control(padapter, LED_CTL_POWER_ON);
		init_hw_mlme_ext(padapter);
		#ifdef CONFIG_RF_POWER_TRIM
		rtw_bb_rf_gain_offset(padapter);
		#endif /*CONFIG_RF_POWER_TRIM*/
		GET_PRIMARY_ADAPTER(padapter)->bup = _TRUE; /*temporary*/
		#ifdef CONFIG_MI_WITH_MBSSID_CAM
		rtw_mi_set_mbid_cam(padapter);
		#endif
		#ifdef CONFIG_SUPPORT_MULTI_BCN
		rtw_ap_multi_bcn_cfg(padapter);
		#endif
		#if (RTL8822B_SUPPORT == 1) || (RTL8192F_SUPPORT == 1)
		#ifdef CONFIG_DYNAMIC_SOML
		rtw_dyn_soml_config(padapter);
		#endif
		#endif
		#ifdef CONFIG_TDMADIG
		rtw_phydm_tdmadig(padapter, TDMADIG_INIT);
		#endif/*CONFIG_TDMADIG*/
		rtw_phydm_dyn_rrsr_en(padapter,padapter->registrypriv.en_dyn_rrsr);
		#ifdef RTW_HALMAC
		RTW_INFO("%s: padapter->registrypriv.set_rrsr_value=0x%x\n", __func__,padapter->registrypriv.set_rrsr_value);
		if(padapter->registrypriv.set_rrsr_value != 0xFFFFFFFF)
			rtw_phydm_set_rrsr(padapter, padapter->registrypriv.set_rrsr_value, TRUE);
		#endif
	} else {
		rtw_set_hw_init_completed(padapter, _FALSE);
		RTW_ERR("%s: hal_init fail\n", __func__);
	}
	return status;
}
#else
uint	 rtw_hal_init(_adapter *padapter)
{
	uint	status = _SUCCESS;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	int i;

	halrf_set_rfsupportability(adapter_to_phydm(padapter));

	status = padapter->hal_func.hal_init(padapter);

	if(pHalData ->phydm_init_result) {

		status = _FAIL;
		RTW_ERR("%s phydm init fail reason=%u \n",
				__func__,
				pHalData->phydm_init_result);
	}

	if (status == _SUCCESS) {
		rtw_set_hw_init_completed(padapter, _TRUE);
		rtw_mi_set_mac_addr(padapter);/*set mac addr of all ifaces*/
		#ifdef RTW_HALMAC
		rtw_restore_hw_port_cfg(padapter);
		#endif
		if (padapter->registrypriv.notch_filter == 1)
			rtw_hal_notch_filter(padapter, 1);

		for (i = 0; i < dvobj->iface_nums; i++)
			rtw_sec_restore_wep_key(dvobj->padapters[i]);

		rtw_led_control(padapter, LED_CTL_POWER_ON);

		init_hw_mlme_ext(padapter);

		rtw_hal_init_opmode(padapter);

		#ifdef CONFIG_RF_POWER_TRIM
		rtw_bb_rf_gain_offset(padapter);
		#endif /*CONFIG_RF_POWER_TRIM*/

		#ifdef CONFIG_SUPPORT_MULTI_BCN
		rtw_ap_multi_bcn_cfg(padapter);
		#endif

#if (RTL8822B_SUPPORT == 1) || (RTL8192F_SUPPORT == 1)
#ifdef CONFIG_DYNAMIC_SOML
		rtw_dyn_soml_config(padapter);
#endif
#endif
		#ifdef CONFIG_TDMADIG
		rtw_phydm_tdmadig(padapter, TDMADIG_INIT);
		#endif/*CONFIG_TDMADIG*/

		rtw_phydm_dyn_rrsr_en(padapter,padapter->registrypriv.en_dyn_rrsr);
		#ifdef RTW_HALMAC
		RTW_INFO("%s: padapter->registrypriv.set_rrsr_value=0x%x\n", __func__,padapter->registrypriv.set_rrsr_value);
		if(padapter->registrypriv.set_rrsr_value != 0xFFFFFFFF)
			rtw_phydm_set_rrsr(padapter, padapter->registrypriv.set_rrsr_value, TRUE);
		#endif

	} else {
		rtw_set_hw_init_completed(padapter, _FALSE);
		RTW_ERR("%s: fail\n", __func__);
	}


	return status;

}
#endif

uint rtw_hal_deinit(_adapter *padapter)
{
	uint	status = _SUCCESS;

	status = padapter->hal_func.hal_deinit(padapter);

	if (status == _SUCCESS) {
		rtw_led_control(padapter, LED_CTL_POWER_OFF);
		rtw_set_hw_init_completed(padapter, _FALSE);
	} else
		RTW_INFO("\n rtw_hal_deinit: hal_init fail\n");


	return status;
}

u8 rtw_hal_set_hwreg(_adapter *padapter, u8 variable, u8 *val)
{
	return padapter->hal_func.set_hw_reg_handler(padapter, variable, val);
}

void rtw_hal_get_hwreg(_adapter *padapter, u8 variable, u8 *val)
{
	padapter->hal_func.GetHwRegHandler(padapter, variable, val);
}

u8 rtw_hal_set_def_var(_adapter *padapter, HAL_DEF_VARIABLE eVariable, void *pValue)
{
	return padapter->hal_func.SetHalDefVarHandler(padapter, eVariable, pValue);
}
u8 rtw_hal_get_def_var(_adapter *padapter, HAL_DEF_VARIABLE eVariable, void *pValue)
{
	return padapter->hal_func.get_hal_def_var_handler(padapter, eVariable, pValue);
}

void rtw_hal_set_odm_var(_adapter *padapter, HAL_ODM_VARIABLE eVariable, void *pValue1, BOOLEAN bSet)
{
	padapter->hal_func.SetHalODMVarHandler(padapter, eVariable, pValue1, bSet);
}
void	rtw_hal_get_odm_var(_adapter *padapter, HAL_ODM_VARIABLE eVariable, void *pValue1, void *pValue2)
{
	padapter->hal_func.GetHalODMVarHandler(padapter, eVariable, pValue1, pValue2);
}

/* FOR SDIO & PCIE */
void rtw_hal_enable_interrupt(_adapter *padapter)
{
#if defined(CONFIG_PCI_HCI) || defined(CONFIG_SDIO_HCI) || defined (CONFIG_GSPI_HCI)
	padapter->hal_func.enable_interrupt(padapter);
#endif /* #if defined(CONFIG_PCI_HCI) || defined (CONFIG_SDIO_HCI) || defined (CONFIG_GSPI_HCI) */
}

/* FOR SDIO & PCIE */
void rtw_hal_disable_interrupt(_adapter *padapter)
{
#if defined(CONFIG_PCI_HCI) || defined(CONFIG_SDIO_HCI) || defined (CONFIG_GSPI_HCI)
	padapter->hal_func.disable_interrupt(padapter);
#endif /* #if defined(CONFIG_PCI_HCI) || defined (CONFIG_SDIO_HCI) || defined (CONFIG_GSPI_HCI) */
}


u8 rtw_hal_check_ips_status(_adapter *padapter)
{
	u8 val = _FALSE;
	if (padapter->hal_func.check_ips_status)
		val = padapter->hal_func.check_ips_status(padapter);
	else
		RTW_INFO("%s: hal_func.check_ips_status is NULL!\n", __FUNCTION__);

	return val;
}

s32 rtw_hal_fw_dl(_adapter *padapter, u8 wowlan)
{
	s32 ret;

	ret = padapter->hal_func.fw_dl(padapter, wowlan);

#ifdef CONFIG_LPS_1T1R
	GET_HAL_DATA(padapter)->lps_1t1r = 0;
#endif

	return ret;
}

#ifdef RTW_HALMAC
s32 rtw_hal_fw_mem_dl(_adapter *padapter, enum fw_mem mem)
{
	systime dlfw_start_time = rtw_get_current_time();
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct debug_priv *pdbgpriv = &dvobj->drv_dbg;
	s32 rst = _FALSE;

	rst = padapter->hal_func.fw_mem_dl(padapter, mem);
	RTW_INFO("%s in %dms\n", __func__, rtw_get_passing_time_ms(dlfw_start_time));

	if (rst == _FALSE)
		pdbgpriv->dbg_fw_mem_dl_error_cnt++;
	if (1)
		RTW_INFO("%s dbg_fw_mem_dl_error_cnt:%d\n", __func__, pdbgpriv->dbg_fw_mem_dl_error_cnt);
	return rst;
}
#endif

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
void rtw_hal_clear_interrupt(_adapter *padapter)
{
#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	padapter->hal_func.clear_interrupt(padapter);
#endif
}
#endif

#if defined(CONFIG_USB_HCI) || defined(CONFIG_PCI_HCI)
u32	rtw_hal_inirp_init(_adapter *padapter)
{
	if (is_primary_adapter(padapter))
		return padapter->hal_func.inirp_init(padapter);
	return _SUCCESS;
}
u32	rtw_hal_inirp_deinit(_adapter *padapter)
{

	if (is_primary_adapter(padapter))
		return padapter->hal_func.inirp_deinit(padapter);

	return _SUCCESS;
}
#endif /* #if defined(CONFIG_USB_HCI) || defined (CONFIG_PCI_HCI) */

#if defined(CONFIG_PCI_HCI)
void	rtw_hal_irp_reset(_adapter *padapter)
{
	padapter->hal_func.irp_reset(GET_PRIMARY_ADAPTER(padapter));
}

void rtw_hal_pci_dbi_write(_adapter *padapter, u16 addr, u8 data)
{
	u16 cmd[2];

	cmd[0] = addr;
	cmd[1] = data;

	padapter->hal_func.set_hw_reg_handler(padapter, HW_VAR_DBI, (u8 *) cmd);
}

u8 rtw_hal_pci_dbi_read(_adapter *padapter, u16 addr)
{
	padapter->hal_func.GetHwRegHandler(padapter, HW_VAR_DBI, (u8 *)(&addr));

	return (u8)addr;
}

void rtw_hal_pci_mdio_write(_adapter *padapter, u8 addr, u16 data)
{
	u16 cmd[2];

	cmd[0] = (u16)addr;
	cmd[1] = data;

	padapter->hal_func.set_hw_reg_handler(padapter, HW_VAR_MDIO, (u8 *) cmd);
}

u16 rtw_hal_pci_mdio_read(_adapter *padapter, u8 addr)
{
	padapter->hal_func.GetHwRegHandler(padapter, HW_VAR_MDIO, &addr);

	return (u8)addr;
}

u8 rtw_hal_pci_l1off_nic_support(_adapter *padapter)
{
	u8 l1off;

	padapter->hal_func.GetHwRegHandler(padapter, HW_VAR_L1OFF_NIC_SUPPORT, &l1off);
	return l1off;
}

u8 rtw_hal_pci_l1off_capability(_adapter *padapter)
{
	u8 l1off;

	padapter->hal_func.GetHwRegHandler(padapter, HW_VAR_L1OFF_CAPABILITY, &l1off);
	return l1off;
}


#endif /* #if defined(CONFIG_PCI_HCI) */

/* for USB Auto-suspend */
u8	rtw_hal_intf_ps_func(_adapter *padapter, HAL_INTF_PS_FUNC efunc_id, u8 *val)
{
	if (padapter->hal_func.interface_ps_func)
		return padapter->hal_func.interface_ps_func(padapter, efunc_id, val);
	return _FAIL;
}

#ifdef CONFIG_RTW_MGMT_QUEUE
s32	rtw_hal_mgmt_xmitframe_enqueue(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	return padapter->hal_func.hal_mgmt_xmitframe_enqueue(padapter, pxmitframe);
}
#endif

s32	rtw_hal_xmitframe_enqueue(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	return padapter->hal_func.hal_xmitframe_enqueue(padapter, pxmitframe);
}

s32	rtw_hal_xmit(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	return padapter->hal_func.hal_xmit(padapter, pxmitframe);
}

/*
 * [IMPORTANT] This function would be run in interrupt context.
 */
s32	rtw_hal_mgnt_xmit(_adapter *padapter, struct xmit_frame *pmgntframe)
{
#ifdef CONFIG_RTW_MGMT_QUEUE
	_irqL irqL;
	struct xmit_priv *pxmitpriv = &(padapter->xmitpriv);
#endif
	s32 ret = _FAIL;

	update_mgntframe_attrib_addr(padapter, pmgntframe);
#ifdef CONFIG_RTW_MGMT_QUEUE
	update_mgntframe_subtype(padapter, pmgntframe);
#endif

#if defined(CONFIG_IEEE80211W) || defined(CONFIG_RTW_MESH)
	if ((!MLME_IS_MESH(padapter) && SEC_IS_BIP_KEY_INSTALLED(&padapter->securitypriv) == _TRUE)
		#ifdef CONFIG_RTW_MESH
		|| (MLME_IS_MESH(padapter) && padapter->mesh_info.mesh_auth_id)
		#endif
	)
		rtw_mgmt_xmitframe_coalesce(padapter, pmgntframe->pkt, pmgntframe);
#endif

#ifdef CONFIG_RTW_MGMT_QUEUE
	if (MLME_IS_AP(padapter) || MLME_IS_MESH(padapter)) {
		_enter_critical_bh(&pxmitpriv->lock, &irqL);
		ret = mgmt_xmitframe_enqueue_for_sleeping_sta(padapter, pmgntframe);
		_exit_critical_bh(&pxmitpriv->lock, &irqL);

		#ifdef DBG_MGMT_QUEUE
		if (ret == _TRUE)
			RTW_INFO("%s doesn't be queued, dattrib->ra:"MAC_FMT" seq_num = %u, subtype = 0x%x\n",
			__func__, MAC_ARG(pmgntframe->attrib.ra), pmgntframe->attrib.seqnum, pmgntframe->attrib.subtype);
		#endif

		if (ret == RTW_QUEUE_MGMT)
			return ret;
	}
#endif

	ret = padapter->hal_func.mgnt_xmit(padapter, pmgntframe);
	return ret;
}

s32	rtw_hal_init_xmit_priv(_adapter *padapter)
{
	return padapter->hal_func.init_xmit_priv(padapter);
}
void	rtw_hal_free_xmit_priv(_adapter *padapter)
{
	padapter->hal_func.free_xmit_priv(padapter);
}

s32	rtw_hal_init_recv_priv(_adapter *padapter)
{
	return padapter->hal_func.init_recv_priv(padapter);
}
void	rtw_hal_free_recv_priv(_adapter *padapter)
{
	padapter->hal_func.free_recv_priv(padapter);
}

void rtw_sta_ra_registed(_adapter *padapter, struct sta_info *psta)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(padapter);

	if (psta == NULL) {
		RTW_ERR(FUNC_ADPT_FMT" sta is NULL\n", FUNC_ADPT_ARG(padapter));
		rtw_warn_on(1);
		return;
	}

#ifdef CONFIG_AP_MODE
	if (MLME_IS_AP(padapter) || MLME_IS_MESH(padapter)) {
		if (psta->cmn.aid > padapter->stapriv.max_aid) {
			RTW_ERR("station aid %d exceed the max number\n", psta->cmn.aid);
			rtw_warn_on(1);
			return;
		}
		rtw_ap_update_sta_ra_info(padapter, psta);
	}
#endif

	psta->cmn.ra_info.ra_bw_mode = rtw_get_tx_bw_mode(padapter, psta);
	/*set correct initial date rate for each mac_id */
	hal_data->INIDATA_RATE[psta->cmn.mac_id] = psta->init_rate;

	rtw_phydm_ra_registed(padapter, psta);
}

void rtw_hal_update_ra_mask(struct sta_info *psta)
{
	_adapter *padapter;

	if (!psta)
		return;

	padapter = psta->padapter;
	rtw_sta_ra_registed(padapter, psta);
}

/*	Start specifical interface thread		*/
void	rtw_hal_start_thread(_adapter *padapter)
{
#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
#ifndef CONFIG_SDIO_TX_TASKLET
	padapter->hal_func.run_thread(padapter);
#endif
#endif
}
/*	Start specifical interface thread		*/
void	rtw_hal_stop_thread(_adapter *padapter)
{
#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
#ifndef CONFIG_SDIO_TX_TASKLET

	padapter->hal_func.cancel_thread(padapter);

#endif
#endif
}

u32	rtw_hal_read_bbreg(_adapter *padapter, u32 RegAddr, u32 BitMask)
{
	u32 data = 0;
	if (padapter->hal_func.read_bbreg)
		data = padapter->hal_func.read_bbreg(padapter, RegAddr, BitMask);
	return data;
}
void	rtw_hal_write_bbreg(_adapter *padapter, u32 RegAddr, u32 BitMask, u32 Data)
{
	if (padapter->hal_func.write_bbreg)
		padapter->hal_func.write_bbreg(padapter, RegAddr, BitMask, Data);
}

u32 rtw_hal_read_rfreg(_adapter *padapter, enum rf_path eRFPath, u32 RegAddr, u32 BitMask)
{
	u32 data = 0;

	if (padapter->hal_func.read_rfreg) {
		data = padapter->hal_func.read_rfreg(padapter, eRFPath, RegAddr, BitMask);

		#ifdef DBG_IO
		if (match_rf_read_sniff_ranges(padapter, eRFPath, RegAddr, BitMask)) {
			RTW_INFO("DBG_IO rtw_hal_read_rfreg(%u, 0x%04x, 0x%08x) read:0x%08x(0x%08x)\n"
				, eRFPath, RegAddr, BitMask, (data << PHY_CalculateBitShift(BitMask)), data);
		}
		#endif
	}

	return data;
}

void rtw_hal_write_rfreg(_adapter *padapter, enum rf_path eRFPath, u32 RegAddr, u32 BitMask, u32 Data)
{
	if (padapter->hal_func.write_rfreg) {

		#ifdef DBG_IO
		if (match_rf_write_sniff_ranges(padapter, eRFPath, RegAddr, BitMask)) {
			RTW_INFO("DBG_IO rtw_hal_write_rfreg(%u, 0x%04x, 0x%08x) write:0x%08x(0x%08x)\n"
				, eRFPath, RegAddr, BitMask, (Data << PHY_CalculateBitShift(BitMask)), Data);
		}
		#endif

		padapter->hal_func.write_rfreg(padapter, eRFPath, RegAddr, BitMask, Data);

#ifdef CONFIG_PCI_HCI
		if (!IS_HARDWARE_TYPE_JAGUAR_AND_JAGUAR2(padapter)) /*For N-Series IC, suggest by Jenyu*/
			rtw_udelay_os(2);
#endif
	}
}

#ifdef CONFIG_SYSON_INDIRECT_ACCESS
u32 rtw_hal_read_syson_reg(PADAPTER padapter, u32 RegAddr, u32 BitMask)
{
	u32 data = 0;
	if (padapter->hal_func.read_syson_reg)
		data = padapter->hal_func.read_syson_reg(padapter, RegAddr, BitMask);

	return data;
}

void rtw_hal_write_syson_reg(_adapter *padapter, u32 RegAddr, u32 BitMask, u32 Data)
{
	if (padapter->hal_func.write_syson_reg)
		padapter->hal_func.write_syson_reg(padapter, RegAddr, BitMask, Data);
}
#endif

#if defined(CONFIG_PCI_HCI)
s32	rtw_hal_interrupt_handler(_adapter *padapter)
{
	s32 ret = _FAIL;
	ret = padapter->hal_func.interrupt_handler(padapter);
	return ret;
}

void	rtw_hal_unmap_beacon_icf(_adapter *padapter)
{
	padapter->hal_func.unmap_beacon_icf(padapter);
}
#endif
#if defined(CONFIG_USB_HCI) && defined(CONFIG_SUPPORT_USB_INT)
void	rtw_hal_interrupt_handler(_adapter *padapter, u16 pkt_len, u8 *pbuf)
{
	padapter->hal_func.interrupt_handler(padapter, pkt_len, pbuf);
}
#endif

void	rtw_hal_set_chnl_bw(_adapter *padapter, u8 channel, enum channel_width Bandwidth, u8 Offset40, u8 Offset80)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	/*u8 cch_160 = Bandwidth == CHANNEL_WIDTH_160 ? channel : 0;*/
	u8 cch_80 = Bandwidth == CHANNEL_WIDTH_80 ? channel : 0;
	u8 cch_40 = Bandwidth == CHANNEL_WIDTH_40 ? channel : 0;
	u8 cch_20 = Bandwidth == CHANNEL_WIDTH_20 ? channel : 0;

	if (rtw_phydm_is_iqk_in_progress(padapter))
		RTW_ERR("%s, %d, IQK may race condition\n", __func__, __LINE__);

#ifdef CONFIG_MP_INCLUDED
	/* MP mode channel don't use secondary channel */
	if (rtw_mp_mode_check(padapter) == _FALSE)
#endif
	{
		#if 0
		if (cch_160 != 0)
			cch_80 = rtw_get_scch_by_cch_offset(cch_160, CHANNEL_WIDTH_160, Offset80);
		#endif
		if (cch_80 != 0)
			cch_40 = rtw_get_scch_by_cch_offset(cch_80, CHANNEL_WIDTH_80, Offset80);
		if (cch_40 != 0)
			cch_20 = rtw_get_scch_by_cch_offset(cch_40, CHANNEL_WIDTH_40, Offset40);
	}

	pHalData->cch_80 = cch_80;
	pHalData->cch_40 = cch_40;
	pHalData->cch_20 = cch_20;

	if (0)
		RTW_INFO("%s cch:%u, %s, offset40:%u, offset80:%u (%u, %u, %u)\n", __func__
			, channel, ch_width_str(Bandwidth), Offset40, Offset80
			, pHalData->cch_80, pHalData->cch_40, pHalData->cch_20);

	padapter->hal_func.set_chnl_bw_handler(padapter, channel, Bandwidth, Offset40, Offset80);
}

void	rtw_hal_dm_watchdog(_adapter *padapter)
{

	rtw_hal_turbo_edca(padapter);
	padapter->hal_func.hal_dm_watchdog(padapter);
}

#ifdef CONFIG_LPS_LCLK_WD_TIMER
void	rtw_hal_dm_watchdog_in_lps(_adapter *padapter)
{
#if defined(CONFIG_CONCURRENT_MODE)
#ifndef CONFIG_FW_MULTI_PORT_SUPPORT
	if (padapter->hw_port != HW_PORT0)
		return;
#endif
#endif

	if (adapter_to_pwrctl(padapter)->bFwCurrentInPSMode == _TRUE)
		rtw_phydm_watchdog_in_lps_lclk(padapter);/* this function caller is in interrupt context */
}
#endif /*CONFIG_LPS_LCLK_WD_TIMER*/

void rtw_hal_bcn_related_reg_setting(_adapter *padapter)
{
	padapter->hal_func.SetBeaconRelatedRegistersHandler(padapter);
}

#ifdef CONFIG_HOSTAPD_MLME
s32	rtw_hal_hostap_mgnt_xmit_entry(_adapter *padapter, _pkt *pkt)
{
	if (padapter->hal_func.hostap_mgnt_xmit_entry)
		return padapter->hal_func.hostap_mgnt_xmit_entry(padapter, pkt);
	return _FAIL;
}
#endif /* CONFIG_HOSTAPD_MLME */

#ifdef DBG_CONFIG_ERROR_DETECT
void	rtw_hal_sreset_init(_adapter *padapter)
{
	padapter->hal_func.sreset_init_value(padapter);
}
void rtw_hal_sreset_reset(_adapter *padapter)
{
	padapter = GET_PRIMARY_ADAPTER(padapter);
	padapter->hal_func.silentreset(padapter);
}

void rtw_hal_sreset_reset_value(_adapter *padapter)
{
	padapter->hal_func.sreset_reset_value(padapter);
}

void rtw_hal_sreset_xmit_status_check(_adapter *padapter)
{
	padapter->hal_func.sreset_xmit_status_check(padapter);
}
void rtw_hal_sreset_linked_status_check(_adapter *padapter)
{
	padapter->hal_func.sreset_linked_status_check(padapter);
}
u8   rtw_hal_sreset_get_wifi_status(_adapter *padapter)
{
	return padapter->hal_func.sreset_get_wifi_status(padapter);
}

bool rtw_hal_sreset_inprogress(_adapter *padapter)
{
	padapter = GET_PRIMARY_ADAPTER(padapter);
	return padapter->hal_func.sreset_inprogress(padapter);
}
#endif /* DBG_CONFIG_ERROR_DETECT */

#ifdef CONFIG_IOL
int rtw_hal_iol_cmd(ADAPTER *adapter, struct xmit_frame *xmit_frame, u32 max_waiting_ms, u32 bndy_cnt)
{
	if (adapter->hal_func.IOL_exec_cmds_sync)
		return adapter->hal_func.IOL_exec_cmds_sync(adapter, xmit_frame, max_waiting_ms, bndy_cnt);
	return _FAIL;
}
#endif

#ifdef CONFIG_XMIT_THREAD_MODE
s32 rtw_hal_xmit_thread_handler(_adapter *padapter)
{
	return padapter->hal_func.xmit_thread_handler(padapter);
}
#endif

#ifdef CONFIG_RECV_THREAD_MODE
s32 rtw_hal_recv_hdl(_adapter *adapter)
{
	return adapter->hal_func.recv_hdl(adapter);
}
#endif

void rtw_hal_notch_filter(_adapter *adapter, bool enable)
{
	if (adapter->hal_func.hal_notch_filter)
		adapter->hal_func.hal_notch_filter(adapter, enable);
}

#ifdef CONFIG_FW_C2H_REG
inline bool rtw_hal_c2h_valid(_adapter *adapter, u8 *buf)
{
	HAL_DATA_TYPE *HalData = GET_HAL_DATA(adapter);
	bool ret = _FAIL;

	ret = C2H_ID_88XX(buf) || C2H_PLEN_88XX(buf);

	return ret;
}

inline s32 rtw_hal_c2h_evt_read(_adapter *adapter, u8 *buf)
{
	HAL_DATA_TYPE *HalData = GET_HAL_DATA(adapter);
	s32 ret = _FAIL;

	ret = c2h_evt_read_88xx(adapter, buf);

	return ret;
}

bool rtw_hal_c2h_reg_hdr_parse(_adapter *adapter, u8 *buf, u8 *id, u8 *seq, u8 *plen, u8 **payload)
{
	HAL_DATA_TYPE *HalData = GET_HAL_DATA(adapter);
	bool ret = _FAIL;

	*id = C2H_ID_88XX(buf);
	*seq = C2H_SEQ_88XX(buf);
	*plen = C2H_PLEN_88XX(buf);
	*payload = C2H_PAYLOAD_88XX(buf);
	ret = _SUCCESS;

	return ret;
}
#endif /* CONFIG_FW_C2H_REG */

#ifdef CONFIG_FW_C2H_PKT
bool rtw_hal_c2h_pkt_hdr_parse(_adapter *adapter, u8 *buf, u16 len, u8 *id, u8 *seq, u8 *plen, u8 **payload)
{
	HAL_DATA_TYPE *HalData = GET_HAL_DATA(adapter);
	bool ret = _FAIL;

	if (!buf || len > 256 || len < 3)
		goto exit;

	*id = C2H_ID_88XX(buf);
	*seq = C2H_SEQ_88XX(buf);
	*plen = len - 2;
	*payload = C2H_PAYLOAD_88XX(buf);
	ret = _SUCCESS;

exit:
	return ret;
}
#endif /* CONFIG_FW_C2H_PKT */

#if defined(CONFIG_MP_INCLUDED) && defined(CONFIG_RTL8723B)
#include <rtw_bt_mp.h> /* for MPTBT_FwC2hBtMpCtrl */
#endif
s32 c2h_handler(_adapter *adapter, u8 id, u8 seq, u8 plen, u8 *payload)
{
	u8 sub_id = 0;
	s32 ret = _SUCCESS;

	switch (id) {
	case C2H_FW_SCAN_COMPLETE:
		RTW_INFO("[C2H], FW Scan Complete\n");
		break;

#ifdef CONFIG_BT_COEXIST
	case C2H_BT_INFO:
		rtw_btcoex_BtInfoNotify(adapter, plen, payload);
		break;
	case C2H_BT_MP_INFO:
		#if defined(CONFIG_MP_INCLUDED) && defined(CONFIG_RTL8723B)
		MPTBT_FwC2hBtMpCtrl(adapter, payload, plen);
		#endif
		rtw_btcoex_BtMpRptNotify(adapter, plen, payload);
		break;
	case C2H_MAILBOX_STATUS:
		RTW_DBG_DUMP("C2H_MAILBOX_STATUS: ", payload, plen);
		break;
	case C2H_WLAN_INFO:
		rtw_btcoex_WlFwDbgInfoNotify(adapter, payload, plen);
		break;
#endif /* CONFIG_BT_COEXIST */

	case C2H_IQK_FINISH:
		c2h_iqk_offload(adapter, payload, plen);
		break;

#if defined(CONFIG_TDLS) && defined(CONFIG_TDLS_CH_SW)
	case C2H_FW_CHNL_SWITCH_COMPLETE:
#ifndef CONFIG_TDLS_CH_SW_V2
		rtw_tdls_chsw_oper_done(adapter);
#endif
		break;
#endif

	case C2H_BCN_EARLY_RPT:
		rtw_hal_bcn_early_rpt_c2h_handler(adapter);
		break;

#ifdef CONFIG_MCC_MODE
	case C2H_MCC:
		rtw_hal_mcc_c2h_handler(adapter, plen, payload);
		break;
#endif

#ifdef CONFIG_RTW_MAC_HIDDEN_RPT
	case C2H_MAC_HIDDEN_RPT:
		c2h_mac_hidden_rpt_hdl(adapter, payload, plen);
		break;
	case C2H_MAC_HIDDEN_RPT_2:
		c2h_mac_hidden_rpt_2_hdl(adapter, payload, plen);
		break;
#endif

	case C2H_DEFEATURE_DBG:
		c2h_defeature_dbg_hdl(adapter, payload, plen);
		break;

#ifdef CONFIG_RTW_CUSTOMER_STR
	case C2H_CUSTOMER_STR_RPT:
		c2h_customer_str_rpt_hdl(adapter, payload, plen);
		break;
	case C2H_CUSTOMER_STR_RPT_2:
		c2h_customer_str_rpt_2_hdl(adapter, payload, plen);
		break;
#endif
#ifdef RTW_PER_CMD_SUPPORT_FW
	case C2H_PER_RATE_RPT:
		c2h_per_rate_rpt_hdl(adapter, payload, plen);
		break;
#endif
#ifdef CONFIG_LPS_ACK
	case C2H_LPS_STATUS_RPT:
		c2h_lps_status_rpt(adapter, payload, plen);
		break;
#endif	
#ifdef CONFIG_FW_OFFLOAD_SET_TXPWR_IDX
	case C2H_SET_TXPWR_FINISH:
		c2h_txpwr_idx_offload_done(adapter, payload, plen);
		break;
#endif
	case C2H_EXTEND:
		sub_id = payload[0];
		/* no handle, goto default */
		/* fall through */

	default:
		if (phydm_c2H_content_parsing(adapter_to_phydm(adapter), id, plen, payload) != TRUE)
			ret = _FAIL;
		break;
	}

	if (ret != _SUCCESS) {
		if (id == C2H_EXTEND)
			RTW_WARN("%s: unknown C2H(0x%02x, 0x%02x)\n", __func__, id, sub_id);
		else
			RTW_WARN("%s: unknown C2H(0x%02x)\n", __func__, id);
	}

	return ret;
}

#ifndef RTW_HALMAC
s32 rtw_hal_c2h_handler(_adapter *adapter, u8 id, u8 seq, u8 plen, u8 *payload)
{
	s32 ret = _FAIL;

	ret = adapter->hal_func.c2h_handler(adapter, id, seq, plen, payload);
	if (ret != _SUCCESS)
		ret = c2h_handler(adapter, id, seq, plen, payload);

	return ret;
}

s32 rtw_hal_c2h_id_handle_directly(_adapter *adapter, u8 id, u8 seq, u8 plen, u8 *payload)
{
	switch (id) {
	case C2H_CCX_TX_RPT:
	case C2H_BT_MP_INFO:
	case C2H_FW_CHNL_SWITCH_COMPLETE:
	case C2H_IQK_FINISH:
	case C2H_MCC:
	case C2H_BCN_EARLY_RPT:
	case C2H_AP_REQ_TXRPT:
	case C2H_SPC_STAT:
	case C2H_SET_TXPWR_FINISH:
		return _TRUE;
	default:
		return _FALSE;
	}
}
#endif /* !RTW_HALMAC */

s32 rtw_hal_is_disable_sw_channel_plan(PADAPTER padapter)
{
	return GET_HAL_DATA(padapter)->bDisableSWChannelPlan;
}

#ifdef CONFIG_PROTSEL_MACSLEEP
static s32 _rtw_hal_macid_sleep(_adapter *adapter, u8 macid, u8 sleep)
{
	struct macid_ctl_t *macid_ctl = adapter_to_macidctl(adapter);
	u16 reg_sleep_info = macid_ctl->reg_sleep_info;
	u16 reg_sleep_ctrl = macid_ctl->reg_sleep_ctrl;
	const u32 sel_mask_sel = BIT(0) | BIT(1) | BIT(2);
	u8 bit_shift;
	u32 val32;
	s32 ret = _FAIL;

	if (macid >= macid_ctl->num) {
		RTW_ERR(ADPT_FMT" %s invalid macid(%u)\n"
			, ADPT_ARG(adapter), sleep ? "sleep" : "wakeup" , macid);
		goto exit;
	}

	if (macid < 32) {
		bit_shift = macid;
	#if (MACID_NUM_SW_LIMIT > 32)
	} else if (macid < 64) {
		bit_shift = macid - 32;
	#endif
	#if (MACID_NUM_SW_LIMIT > 64)
	} else if (macid < 96) {
		bit_shift = macid - 64;
	#endif
	#if (MACID_NUM_SW_LIMIT > 96)
	} else if (macid < 128) {
		bit_shift = macid - 96;
	#endif
	} else {
		rtw_warn_on(1);
		goto exit;
	}

	if (!reg_sleep_ctrl || !reg_sleep_info) {
		rtw_warn_on(1);
		goto exit;
	}

	val32 = rtw_read32(adapter, reg_sleep_ctrl);
	val32 = (val32 &~sel_mask_sel) | ((macid / 32) & sel_mask_sel);
	rtw_write32(adapter, reg_sleep_ctrl, val32);

	val32 = rtw_read32(adapter, reg_sleep_info);
	RTW_INFO(ADPT_FMT" %s macid=%d, ori reg_0x%03x=0x%08x\n"
		, ADPT_ARG(adapter), sleep ? "sleep" : "wakeup"
		, macid, reg_sleep_info, val32);

	ret = _SUCCESS;

	if (sleep) {
		if (val32 & BIT(bit_shift))
			goto exit;
		val32 |= BIT(bit_shift);
	} else {
		if (!(val32 & BIT(bit_shift)))
			goto exit;
		val32 &= ~BIT(bit_shift);
	}

	rtw_write32(adapter, reg_sleep_info, val32);

exit:
	return ret;
}
#else
static s32 _rtw_hal_macid_sleep(_adapter *adapter, u8 macid, u8 sleep)
{
	struct macid_ctl_t *macid_ctl = adapter_to_macidctl(adapter);
	u16 reg_sleep;
	u8 bit_shift;
	u32 val32;
	s32 ret = _FAIL;

	if (macid >= macid_ctl->num) {
		RTW_ERR(ADPT_FMT" %s invalid macid(%u)\n"
			, ADPT_ARG(adapter), sleep ? "sleep" : "wakeup" , macid);
		goto exit;
	}

	if (macid < 32) {
		reg_sleep = macid_ctl->reg_sleep_m0;
		bit_shift = macid;
	#if (MACID_NUM_SW_LIMIT > 32)
	} else if (macid < 64) {
		reg_sleep = macid_ctl->reg_sleep_m1;
		bit_shift = macid - 32;
	#endif
	#if (MACID_NUM_SW_LIMIT > 64)
	} else if (macid < 96) {
		reg_sleep = macid_ctl->reg_sleep_m2;
		bit_shift = macid - 64;
	#endif
	#if (MACID_NUM_SW_LIMIT > 96)
	} else if (macid < 128) {
		reg_sleep = macid_ctl->reg_sleep_m3;
		bit_shift = macid - 96;
	#endif
	} else {
		rtw_warn_on(1);
		goto exit;
	}

	if (!reg_sleep) {
		rtw_warn_on(1);
		goto exit;
	}

	val32 = rtw_read32(adapter, reg_sleep);
	RTW_INFO(ADPT_FMT" %s macid=%d, ori reg_0x%03x=0x%08x\n"
		, ADPT_ARG(adapter), sleep ? "sleep" : "wakeup"
		, macid, reg_sleep, val32);

	ret = _SUCCESS;

	if (sleep) {
		if (val32 & BIT(bit_shift))
			goto exit;
		val32 |= BIT(bit_shift);
	} else {
		if (!(val32 & BIT(bit_shift)))
			goto exit;
		val32 &= ~BIT(bit_shift);
	}

	rtw_write32(adapter, reg_sleep, val32);

exit:
	return ret;
}
#endif

inline s32 rtw_hal_macid_sleep(_adapter *adapter, u8 macid)
{
	return _rtw_hal_macid_sleep(adapter, macid, 1);
}

inline s32 rtw_hal_macid_wakeup(_adapter *adapter, u8 macid)
{
	return _rtw_hal_macid_sleep(adapter, macid, 0);
}

#ifdef CONFIG_PROTSEL_MACSLEEP
static s32 _rtw_hal_macid_bmp_sleep(_adapter *adapter, struct macid_bmp *bmp, u8 sleep)
{
	struct macid_ctl_t *macid_ctl = adapter_to_macidctl(adapter);
	u16 reg_sleep_info = macid_ctl->reg_sleep_info;
	u16 reg_sleep_ctrl = macid_ctl->reg_sleep_ctrl;
	const u32 sel_mask_sel = BIT(0) | BIT(1) | BIT(2);
	u32 m;
	u8 mid = 0;
	u32 val32;

	do {
		if (mid == 0) {
			m = bmp->m0;
		#if (MACID_NUM_SW_LIMIT > 32)
		} else if (mid == 1) {
			m = bmp->m1;
		#endif
		#if (MACID_NUM_SW_LIMIT > 64)
		} else if (mid == 2) {
			m = bmp->m2;
		#endif
		#if (MACID_NUM_SW_LIMIT > 96)
		} else if (mid == 3) {
			m = bmp->m3;
		#endif
		} else {
			rtw_warn_on(1);
			break;
		}

		if (m == 0)
			goto move_next;

		if (!reg_sleep_ctrl || !reg_sleep_info) {
			rtw_warn_on(1);
			break;
		}

		val32 = rtw_read32(adapter, reg_sleep_ctrl);
		val32 = (val32 &~sel_mask_sel) | (mid & sel_mask_sel);
		rtw_write32(adapter, reg_sleep_ctrl, val32);

		val32 = rtw_read32(adapter, reg_sleep_info);
		RTW_INFO(ADPT_FMT" %s m%u=0x%08x, ori reg_0x%03x=0x%08x\n"
			, ADPT_ARG(adapter), sleep ? "sleep" : "wakeup"
			, mid, m, reg_sleep_info, val32);

		if (sleep) {
			if ((val32 & m) == m)
				goto move_next;
			val32 |= m;
		} else {
			if ((val32 & m) == 0)
				goto move_next;
			val32 &= ~m;
		}

		rtw_write32(adapter, reg_sleep_info, val32);

move_next:
		mid++;
	} while (mid * 32 < MACID_NUM_SW_LIMIT);

	return _SUCCESS;
}
#else
static s32 _rtw_hal_macid_bmp_sleep(_adapter *adapter, struct macid_bmp *bmp, u8 sleep)
{
	struct macid_ctl_t *macid_ctl = adapter_to_macidctl(adapter);
	u16 reg_sleep;
	u32 m;
	u8 mid = 0;
	u32 val32;

	do {
		if (mid == 0) {
			m = bmp->m0;
			reg_sleep = macid_ctl->reg_sleep_m0;
		#if (MACID_NUM_SW_LIMIT > 32)
		} else if (mid == 1) {
			m = bmp->m1;
			reg_sleep = macid_ctl->reg_sleep_m1;
		#endif
		#if (MACID_NUM_SW_LIMIT > 64)
		} else if (mid == 2) {
			m = bmp->m2;
			reg_sleep = macid_ctl->reg_sleep_m2;
		#endif
		#if (MACID_NUM_SW_LIMIT > 96)
		} else if (mid == 3) {
			m = bmp->m3;
			reg_sleep = macid_ctl->reg_sleep_m3;
		#endif
		} else {
			rtw_warn_on(1);
			break;
		}

		if (m == 0)
			goto move_next;

		if (!reg_sleep) {
			rtw_warn_on(1);
			break;
		}

		val32 = rtw_read32(adapter, reg_sleep);
		RTW_INFO(ADPT_FMT" %s m%u=0x%08x, ori reg_0x%03x=0x%08x\n"
			, ADPT_ARG(adapter), sleep ? "sleep" : "wakeup"
			, mid, m, reg_sleep, val32);

		if (sleep) {
			if ((val32 & m) == m)
				goto move_next;
			val32 |= m;
		} else {
			if ((val32 & m) == 0)
				goto move_next;
			val32 &= ~m;
		}

		rtw_write32(adapter, reg_sleep, val32);

move_next:
		mid++;
	} while (mid * 32 < MACID_NUM_SW_LIMIT);

	return _SUCCESS;
}
#endif

inline s32 rtw_hal_macid_sleep_all_used(_adapter *adapter)
{
	struct macid_ctl_t *macid_ctl = adapter_to_macidctl(adapter);

	return _rtw_hal_macid_bmp_sleep(adapter, &macid_ctl->used, 1);
}

inline s32 rtw_hal_macid_wakeup_all_used(_adapter *adapter)
{
	struct macid_ctl_t *macid_ctl = adapter_to_macidctl(adapter);

	return _rtw_hal_macid_bmp_sleep(adapter, &macid_ctl->used, 0);
}

static s32 _rtw_hal_macid_drop(_adapter *adapter, u8 macid, u8 drop)
{
	struct macid_ctl_t *macid_ctl = adapter_to_macidctl(adapter);
#ifndef CONFIG_PROTSEL_MACSLEEP
	u16 reg_drop = 0;
#else
	u16 reg_drop_info = macid_ctl->reg_drop_info;
	u16 reg_drop_ctrl = macid_ctl->reg_drop_ctrl;
	const u32 sel_mask_sel = BIT(0) | BIT(1) | BIT(2);
#endif /* CONFIG_PROTSEL_MACSLEEP */
	u8 bit_shift;
	u32 val32;
	s32 ret = _FAIL;
/* some IC doesn't have this register */
#ifndef REG_PKT_BUFF_ACCESS_CTRL
#define REG_PKT_BUFF_ACCESS_CTRL 0
#endif

	if (macid >= macid_ctl->num) {
		RTW_ERR(ADPT_FMT" %s invalid macid(%u)\n"
			, ADPT_ARG(adapter), drop ? "drop" : "undrop" , macid);
		goto exit;
	}
	
	if(_rtw_macid_ctl_chk_cap(adapter, MACID_DROP)) {
		if (macid < 32) {
#ifndef CONFIG_PROTSEL_MACSLEEP
			reg_drop = macid_ctl->reg_drop_m0;
#endif /* CONFIG_PROTSEL_MACSLEEP */
			bit_shift = macid;
		#if (MACID_NUM_SW_LIMIT > 32)
		} else if (macid < 64) {
#ifndef CONFIG_PROTSEL_MACSLEEP
			reg_drop = macid_ctl->reg_drop_m1;
#endif /* CONFIG_PROTSEL_MACSLEEP */
			bit_shift = macid - 32;
		#endif
		#if (MACID_NUM_SW_LIMIT > 64)
		} else if (macid < 96) {
#ifndef CONFIG_PROTSEL_MACSLEEP
			reg_drop = macid_ctl->reg_drop_m2;
#endif /* CONFIG_PROTSEL_MACSLEEP */
			bit_shift = macid - 64;
		#endif
		#if (MACID_NUM_SW_LIMIT > 96)
		} else if (macid < 128) {
#ifndef CONFIG_PROTSEL_MACSLEEP
			reg_drop = macid_ctl->reg_drop_m3;
#endif /* CONFIG_PROTSEL_MACSLEEP */
			bit_shift = macid - 96;
		#endif
		} else {
			rtw_warn_on(1);
			goto exit;
		}

#ifndef CONFIG_PROTSEL_MACSLEEP
		if (!reg_drop) {
			rtw_warn_on(1);
			goto exit;
		}
		val32 = rtw_read32(adapter, reg_drop);
		/*RTW_INFO(ADPT_FMT" %s macid=%d, ori reg_0x%03x=0x%08x \n"
			, ADPT_ARG(adapter), drop ? "drop" : "undrop"
			, macid, reg_drop, val32);*/
#else
		if (!reg_drop_ctrl || !reg_drop_info) {
			rtw_warn_on(1);
			goto exit;
		}

		val32 = rtw_read32(adapter, reg_drop_ctrl);
		val32 = (val32 &~sel_mask_sel) | ((macid / 32) & sel_mask_sel);
		rtw_write32(adapter, reg_drop_ctrl, val32);

		val32 = rtw_read32(adapter, reg_drop_info);
		/*RTW_INFO(ADPT_FMT" %s macid=%d, ori reg_0x%03x=0x%08x\n"
			, ADPT_ARG(adapter), drop ? "drop" : "undrop"
			, macid, reg_drop_info, val32);*/
#endif /* CONFIG_PROTSEL_MACSLEEP */
		ret = _SUCCESS;

		if (drop) {
			if (val32 & BIT(bit_shift))
				goto exit;
			val32 |= BIT(bit_shift);
		} else {
			if (!(val32 & BIT(bit_shift)))
				goto exit;
			val32 &= ~BIT(bit_shift);
		}

#ifndef CONFIG_PROTSEL_MACSLEEP
		rtw_write32(adapter, reg_drop, val32);
		RTW_INFO(ADPT_FMT" %s macid=%d, done reg_0x%03x=0x%08x\n"
			, ADPT_ARG(adapter), drop ? "drop" : "undrop"
			, macid, reg_drop, val32);
#else
		rtw_write32(adapter, reg_drop_info, val32);
		RTW_INFO(ADPT_FMT" %s macid=%d, done reg_0x%03x=0x%08x\n"
			, ADPT_ARG(adapter), drop ? "drop" : "undrop"
			, macid, reg_drop_info, val32);
#endif /* CONFIG_PROTSEL_MACSLEEP */
		
		
	} else if(_rtw_macid_ctl_chk_cap(adapter, MACID_DROP_INDIRECT)) {
		u16 start_addr = macid_ctl->macid_txrpt/8;
		u32 txrpt_h4b = 0;
		u8 i;
		
		/* each address means 1 byte */
		start_addr += macid*(macid_ctl->macid_txrpt_pgsz/8);
		/* select tx report buffer */
		rtw_write8(adapter, REG_PKT_BUFF_ACCESS_CTRL, TXREPORT_BUF_SELECT);
		/* set tx report buffer start address for reading */
		rtw_write32(adapter, REG_PKTBUF_DBG_CTRL, start_addr);
		txrpt_h4b = rtw_read32(adapter, REG_PKTBUF_DBG_DATA_H);
		/* OFFSET5 BIT2 is BIT10 of high 4 bytes */
		if (drop) {
			if (txrpt_h4b & BIT(10))
				goto exit;
			txrpt_h4b |= BIT(10);
		} else {
			if (!(txrpt_h4b & BIT(10)))
				goto exit;
			txrpt_h4b &= ~BIT(10);
		}
		/* set to macid drop field */
		rtw_write32(adapter, REG_PKTBUF_DBG_DATA_H, txrpt_h4b);
		/* 0x20800000 only write BIT10 of tx report buf */
		rtw_write32(adapter, REG_PKTBUF_DBG_CTRL, 0x20800000 | start_addr);
#if 0 /* some ICs doesn't clear the write done bit */
		/* checking TX queue status */
		for (i = 0 ; i < 50 ; i++) {
			txrpt_h4b = rtw_read32(adapter, REG_PKTBUF_DBG_CTRL);
			if (txrpt_h4b & BIT(23)) {
				RTW_INFO("%s: wait to write TX RTP buf (%d)!\n", __func__, i);
				rtw_mdelay_os(10);
			} else {
				RTW_INFO("%s: wait to write TX RTP buf done (%d)!\n", __func__, i);
				break;
			}
		}
#endif
		rtw_write32(adapter, REG_PKTBUF_DBG_CTRL, start_addr);
		RTW_INFO("start_addr=%x, data_H:%08x, data_L:%08x, macid=%d, txrpt_h4b=%x\n", start_addr
		,rtw_read32(adapter, REG_PKTBUF_DBG_DATA_H), rtw_read32(adapter, REG_PKTBUF_DBG_DATA_L), macid, txrpt_h4b);
	} else {
		RTW_INFO("There is no definition for camctl cap , please correct it\n");
	}
exit:
	return ret;
}

inline s32 rtw_hal_macid_drop(_adapter *adapter, u8 macid)
{
	return _rtw_hal_macid_drop(adapter, macid, 1);
}

inline s32 rtw_hal_macid_undrop(_adapter *adapter, u8 macid)
{
	return _rtw_hal_macid_drop(adapter, macid, 0);
}

s32 rtw_hal_fill_h2c_cmd(PADAPTER padapter, u8 ElementID, u32 CmdLen, u8 *pCmdBuffer)
{
	_adapter *pri_adapter = GET_PRIMARY_ADAPTER(padapter);

	if (GET_HAL_DATA(pri_adapter)->bFWReady == _TRUE)
		return padapter->hal_func.fill_h2c_cmd(padapter, ElementID, CmdLen, pCmdBuffer);
	else if (padapter->registrypriv.mp_mode == 0)
		RTW_PRINT(FUNC_ADPT_FMT" FW doesn't exit when no MP mode, by pass H2C id:0x%02x\n"
			  , FUNC_ADPT_ARG(padapter), ElementID);
	return _FAIL;
}

void rtw_hal_fill_fake_txdesc(_adapter *padapter, u8 *pDesc, u32 BufferLen,
			      u8 IsPsPoll, u8 IsBTQosNull, u8 bDataFrame)
{
	padapter->hal_func.fill_fake_txdesc(padapter, pDesc, BufferLen, IsPsPoll, IsBTQosNull, bDataFrame);

}

u8 rtw_hal_get_txbuff_rsvd_page_num(_adapter *adapter, bool wowlan)
{
	u8 num = 0;


	if (adapter->hal_func.hal_get_tx_buff_rsvd_page_num) {
		num = adapter->hal_func.hal_get_tx_buff_rsvd_page_num(adapter, wowlan);
	} else {
#ifdef RTW_HALMAC
		num = GET_HAL_DATA(adapter)->drv_rsvd_page_number;
#endif /* RTW_HALMAC */
	}

	return num;
}

#ifdef CONFIG_GPIO_API
void rtw_hal_update_hisr_hsisr_ind(_adapter *padapter, u32 flag)
{
	if (padapter->hal_func.update_hisr_hsisr_ind)
		padapter->hal_func.update_hisr_hsisr_ind(padapter, flag);
}

int rtw_hal_gpio_func_check(_adapter *padapter, u8 gpio_num)
{
	int ret = _SUCCESS;

	if (padapter->hal_func.hal_gpio_func_check)
		ret = padapter->hal_func.hal_gpio_func_check(padapter, gpio_num);

	return ret;
}

void rtw_hal_gpio_multi_func_reset(_adapter *padapter, u8 gpio_num)
{
	if (padapter->hal_func.hal_gpio_multi_func_reset)
		padapter->hal_func.hal_gpio_multi_func_reset(padapter, gpio_num);
}
#endif

#ifdef CONFIG_FW_CORRECT_BCN
void rtw_hal_fw_correct_bcn(_adapter *padapter)
{
	if (padapter->hal_func.fw_correct_bcn)
		padapter->hal_func.fw_correct_bcn(padapter);
}
#endif

void rtw_hal_set_tx_power_level(_adapter *adapter, u8 channel)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);

	if (phy_chk_ch_setting_consistency(adapter, channel) != _SUCCESS)
		return;

	hal_data->set_entire_txpwr = 1;

	adapter->hal_func.set_tx_power_level_handler(adapter, channel);
	rtw_hal_set_txpwr_done(adapter);

	hal_data->set_entire_txpwr = 0;
}

void rtw_hal_update_txpwr_level(_adapter *adapter)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);

	rtw_hal_set_tx_power_level(adapter, hal_data->current_channel);
	rtw_rfctl_update_op_mode(adapter_to_rfctl(adapter), 0, 0);
}

void rtw_hal_set_txpwr_done(_adapter *adapter)
{
	if (adapter->hal_func.set_txpwr_done)
		adapter->hal_func.set_txpwr_done(adapter);
}

void rtw_hal_set_tx_power_index(_adapter *adapter, u32 powerindex
	, enum rf_path rfpath, u8 rate)
{
	adapter->hal_func.set_tx_power_index_handler(adapter, powerindex, rfpath, rate);
}

u8 rtw_hal_get_tx_power_index(_adapter *adapter, enum rf_path rfpath
	, RATE_SECTION rs, enum MGN_RATE rate, enum channel_width bw, BAND_TYPE band, u8 cch, u8 opch
	, struct txpwr_idx_comp *tic)
{
	return adapter->hal_func.get_tx_power_index_handler(adapter, rfpath
		, rs, rate, bw, band, cch, opch, tic);
}

s8 rtw_hal_get_txpwr_target_extra_bias(_adapter *adapter, enum rf_path rfpath
	, RATE_SECTION rs, enum MGN_RATE rate, enum channel_width bw, BAND_TYPE band, u8 cch)
{
	s8 val = 0;

	if (adapter->hal_func.get_txpwr_target_extra_bias) {
		val = adapter->hal_func.get_txpwr_target_extra_bias(adapter
				, rfpath, rs, rate, bw, band, cch);
	}

	return val;
}

#ifdef RTW_HALMAC
/*
 * Description:
 *	Initialize MAC registers
 *
 * Return:
 *	_TRUE	success
 *	_FALSE	fail
 */
u8 rtw_hal_init_mac_register(PADAPTER adapter)
{
	return adapter->hal_func.init_mac_register(adapter);
}

/*
 * Description:
 *	Initialize PHY(BB/RF) related functions
 *
 * Return:
 *	_TRUE	success
 *	_FALSE	fail
 */
u8 rtw_hal_init_phy(PADAPTER adapter)
{
	return adapter->hal_func.init_phy(adapter);
}
#endif /* RTW_HALMAC */

#ifdef CONFIG_RFKILL_POLL
bool rtw_hal_rfkill_poll(_adapter *adapter, u8 *valid)
{
	bool ret;

	if (adapter->hal_func.hal_radio_onoff_check)
		ret = adapter->hal_func.hal_radio_onoff_check(adapter, valid);
	else {
		*valid = 0;
		ret = _FALSE;
	}
	return ret;
}
#endif

#define rtw_hal_error_msg(ops_fun)		\
	RTW_PRINT("### %s - Error : Please hook hal_func.%s ###\n", __FUNCTION__, ops_fun)

u8 rtw_hal_ops_check(_adapter *padapter)
{
	u8 ret = _SUCCESS;
#if 1
	/*** initialize section ***/
	if (NULL == padapter->hal_func.read_chip_version) {
		rtw_hal_error_msg("read_chip_version");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.init_default_value) {
		rtw_hal_error_msg("init_default_value");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.intf_chip_configure) {
		rtw_hal_error_msg("intf_chip_configure");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.read_adapter_info) {
		rtw_hal_error_msg("read_adapter_info");
		ret = _FAIL;
	}

	if (NULL == padapter->hal_func.hal_power_on) {
		rtw_hal_error_msg("hal_power_on");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.hal_power_off) {
		rtw_hal_error_msg("hal_power_off");
		ret = _FAIL;
	}

	if (NULL == padapter->hal_func.hal_init) {
		rtw_hal_error_msg("hal_init");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.hal_deinit) {
		rtw_hal_error_msg("hal_deinit");
		ret = _FAIL;
	}

	/*** xmit section ***/
	if (NULL == padapter->hal_func.init_xmit_priv) {
		rtw_hal_error_msg("init_xmit_priv");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.free_xmit_priv) {
		rtw_hal_error_msg("free_xmit_priv");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.hal_xmit) {
		rtw_hal_error_msg("hal_xmit");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.mgnt_xmit) {
		rtw_hal_error_msg("mgnt_xmit");
		ret = _FAIL;
	}
#ifdef CONFIG_XMIT_THREAD_MODE
	if (NULL == padapter->hal_func.xmit_thread_handler) {
		rtw_hal_error_msg("xmit_thread_handler");
		ret = _FAIL;
	}
#endif
	if (NULL == padapter->hal_func.hal_xmitframe_enqueue) {
		rtw_hal_error_msg("hal_xmitframe_enqueue");
		ret = _FAIL;
	}
#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
#ifndef CONFIG_SDIO_TX_TASKLET
	if (NULL == padapter->hal_func.run_thread) {
		rtw_hal_error_msg("run_thread");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.cancel_thread) {
		rtw_hal_error_msg("cancel_thread");
		ret = _FAIL;
	}
#endif
#endif

	/*** recv section ***/
	if (NULL == padapter->hal_func.init_recv_priv) {
		rtw_hal_error_msg("init_recv_priv");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.free_recv_priv) {
		rtw_hal_error_msg("free_recv_priv");
		ret = _FAIL;
	}
#ifdef CONFIG_RECV_THREAD_MODE
	if (NULL == padapter->hal_func.recv_hdl) {
		rtw_hal_error_msg("recv_hdl");
		ret = _FAIL;
	}
#endif
#if defined(CONFIG_USB_HCI) || defined(CONFIG_PCI_HCI)
	if (NULL == padapter->hal_func.inirp_init) {
		rtw_hal_error_msg("inirp_init");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.inirp_deinit) {
		rtw_hal_error_msg("inirp_deinit");
		ret = _FAIL;
	}
#endif /* #if defined(CONFIG_USB_HCI) || defined (CONFIG_PCI_HCI) */


	/*** interrupt hdl section ***/
#if defined(CONFIG_PCI_HCI)
	if (NULL == padapter->hal_func.irp_reset) {
		rtw_hal_error_msg("irp_reset");
		ret = _FAIL;
	}
#endif/*#if defined(CONFIG_PCI_HCI)*/
#if (defined(CONFIG_PCI_HCI)) || (defined(CONFIG_USB_HCI) && defined(CONFIG_SUPPORT_USB_INT))
	if (NULL == padapter->hal_func.interrupt_handler) {
		rtw_hal_error_msg("interrupt_handler");
		ret = _FAIL;
	}
#endif /*#if (defined(CONFIG_PCI_HCI)) || (defined(CONFIG_USB_HCI) && defined(CONFIG_SUPPORT_USB_INT))*/

#if defined(CONFIG_PCI_HCI) || defined(CONFIG_SDIO_HCI) || defined (CONFIG_GSPI_HCI)
	if (NULL == padapter->hal_func.enable_interrupt) {
		rtw_hal_error_msg("enable_interrupt");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.disable_interrupt) {
		rtw_hal_error_msg("disable_interrupt");
		ret = _FAIL;
	}
#endif /* defined(CONFIG_PCI_HCI) || defined (CONFIG_SDIO_HCI) || defined (CONFIG_GSPI_HCI) */


	/*** DM section ***/
	if (NULL == padapter->hal_func.dm_init) {
		rtw_hal_error_msg("dm_init");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.dm_deinit) {
		rtw_hal_error_msg("dm_deinit");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.hal_dm_watchdog) {
		rtw_hal_error_msg("hal_dm_watchdog");
		ret = _FAIL;
	}

	/*** xxx section ***/
	if (NULL == padapter->hal_func.set_chnl_bw_handler) {
		rtw_hal_error_msg("set_chnl_bw_handler");
		ret = _FAIL;
	}

	if (NULL == padapter->hal_func.set_hw_reg_handler) {
		rtw_hal_error_msg("set_hw_reg_handler");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.GetHwRegHandler) {
		rtw_hal_error_msg("GetHwRegHandler");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.get_hal_def_var_handler) {
		rtw_hal_error_msg("get_hal_def_var_handler");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.SetHalDefVarHandler) {
		rtw_hal_error_msg("SetHalDefVarHandler");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.GetHalODMVarHandler) {
		rtw_hal_error_msg("GetHalODMVarHandler");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.SetHalODMVarHandler) {
		rtw_hal_error_msg("SetHalODMVarHandler");
		ret = _FAIL;
	}

	if (NULL == padapter->hal_func.SetBeaconRelatedRegistersHandler) {
		rtw_hal_error_msg("SetBeaconRelatedRegistersHandler");
		ret = _FAIL;
	}

	if (NULL == padapter->hal_func.fill_h2c_cmd) {
		rtw_hal_error_msg("fill_h2c_cmd");
		ret = _FAIL;
	}

#ifdef RTW_HALMAC
	if (NULL == padapter->hal_func.hal_mac_c2h_handler) {
		rtw_hal_error_msg("hal_mac_c2h_handler");
		ret = _FAIL;
	}
#elif !defined(CONFIG_RTL8188E)
	if (NULL == padapter->hal_func.c2h_handler) {
		rtw_hal_error_msg("c2h_handler");
		ret = _FAIL;
	}
#endif

#if defined(CONFIG_LPS) || defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
	if (NULL == padapter->hal_func.fill_fake_txdesc) {
		rtw_hal_error_msg("fill_fake_txdesc");
		ret = _FAIL;
	}
#endif

#ifndef RTW_HALMAC
	if (NULL == padapter->hal_func.hal_get_tx_buff_rsvd_page_num) {
		rtw_hal_error_msg("hal_get_tx_buff_rsvd_page_num");
		ret = _FAIL;
	}
#endif /* !RTW_HALMAC */

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	if (NULL == padapter->hal_func.clear_interrupt) {
		rtw_hal_error_msg("clear_interrupt");
		ret = _FAIL;
	}
#endif
#endif /* CONFIG_WOWLAN */

	if (NULL == padapter->hal_func.fw_dl) {
		rtw_hal_error_msg("fw_dl");
		ret = _FAIL;
	}

	#ifdef CONFIG_FW_CORRECT_BCN
	if (IS_HARDWARE_TYPE_8814A(padapter)
	    && NULL == padapter->hal_func.fw_correct_bcn) {
		rtw_hal_error_msg("fw_correct_bcn");
		ret = _FAIL;
	}
	#endif

	if (!padapter->hal_func.set_tx_power_level_handler) {
		rtw_hal_error_msg("set_tx_power_level_handler");
		ret = _FAIL;
	}
	if (!padapter->hal_func.set_tx_power_index_handler) {
		rtw_hal_error_msg("set_tx_power_index_handler");
		ret = _FAIL;
	}
	if (!padapter->hal_func.get_tx_power_index_handler) {
		rtw_hal_error_msg("get_tx_power_index_handler");
		ret = _FAIL;
	}

	/*** SReset section ***/
#ifdef DBG_CONFIG_ERROR_DETECT
	if (NULL == padapter->hal_func.sreset_init_value) {
		rtw_hal_error_msg("sreset_init_value");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.sreset_reset_value) {
		rtw_hal_error_msg("sreset_reset_value");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.silentreset) {
		rtw_hal_error_msg("silentreset");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.sreset_xmit_status_check) {
		rtw_hal_error_msg("sreset_xmit_status_check");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.sreset_linked_status_check) {
		rtw_hal_error_msg("sreset_linked_status_check");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.sreset_get_wifi_status) {
		rtw_hal_error_msg("sreset_get_wifi_status");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.sreset_inprogress) {
		rtw_hal_error_msg("sreset_inprogress");
		ret = _FAIL;
	}
#endif  /* #ifdef DBG_CONFIG_ERROR_DETECT */

#ifdef RTW_HALMAC
	if (NULL == padapter->hal_func.init_mac_register) {
		rtw_hal_error_msg("init_mac_register");
		ret = _FAIL;
	}
	if (NULL == padapter->hal_func.init_phy) {
		rtw_hal_error_msg("init_phy");
		ret = _FAIL;
	}
#endif /* RTW_HALMAC */

#ifdef CONFIG_RFKILL_POLL
	if (padapter->hal_func.hal_radio_onoff_check == NULL) {
		rtw_hal_error_msg("hal_radio_onoff_check");
		ret = _FAIL;
	}
#endif
#endif
	return  ret;
}
