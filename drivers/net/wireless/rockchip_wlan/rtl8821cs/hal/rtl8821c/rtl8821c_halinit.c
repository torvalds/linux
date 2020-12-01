/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
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
#define _RTL8821C_HALINIT_C_

#include <drv_types.h>		/* PADAPTER, basic_types.h and etc. */
#include <hal_data.h>		/* GET_HAL_SPEC(), HAL_DATA_TYPE */
#include "../hal_halmac.h"	/* HALMAC API */
#include "rtl8821c.h"

void init_hal_spec_rtl8821c(PADAPTER adapter)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);

	rtw_halmac_fill_hal_spec(adapter_to_dvobj(adapter), hal_spec);

	hal_spec->ic_name = "rtl8821c";
	hal_spec->macid_num = 128;
	/* hal_spec->sec_cam_ent_num follow halmac setting */
	hal_spec->sec_cap = SEC_CAP_CHK_BMC | SEC_CAP_CHK_EXTRA_SEC;
	hal_spec->macid_cap = MACID_DROP;

	hal_spec->rfpath_num_2g = 2;
	hal_spec->rfpath_num_5g = 1;
	hal_spec->rf_reg_path_num = hal_spec->rf_reg_path_avail_num = 1;
	hal_spec->rf_reg_trx_path_bmp = 0x11;
	hal_spec->max_tx_cnt = 1;

	hal_spec->tx_nss_num = 1;
	hal_spec->rx_nss_num = 1;
	hal_spec->band_cap = BAND_CAP_2G | BAND_CAP_5G;
	hal_spec->bw_cap = BW_CAP_20M | BW_CAP_40M | BW_CAP_80M;
	hal_spec->port_num = 5;
	hal_spec->hci_type = 0;
	hal_spec->proto_cap = PROTO_CAP_11B | PROTO_CAP_11G | PROTO_CAP_11N | PROTO_CAP_11AC;

	hal_spec->txgi_max = 63;
	hal_spec->txgi_pdbm = 2;

	hal_spec->wl_func = 0
			    | WL_FUNC_P2P
			    | WL_FUNC_MIRACAST
			    | WL_FUNC_TDLS
			    ;

	hal_spec->tx_aclt_unit_factor = 8;

	hal_spec->rx_tsf_filter = 1;

	hal_spec->pg_txpwr_saddr = 0x10;
	hal_spec->pg_txgi_diff_factor = 1;

	rtw_macid_ctl_init_sleep_reg(adapter_to_macidctl(adapter)
		, REG_MACID_SLEEP_8821C
		, REG_MACID_SLEEP1_8821C
		, REG_MACID_SLEEP2_8821C
		, REG_MACID_SLEEP3_8821C);
	
	rtw_macid_ctl_init_drop_reg(adapter_to_macidctl(adapter)
		, REG_MACID_DROP0_8821C
		, REG_MACID_DROP1_8821C
		, REG_MACID_DROP2_8821C
		, REG_MACID_DROP3_8821C);
}

u32 rtl8821c_power_on(PADAPTER adapter)
{
	struct dvobj_priv *d;
	PHAL_DATA_TYPE hal;
	u8 bMacPwrCtrlOn;
	int err = 0;
	u8 ret = _SUCCESS;


	d = adapter_to_dvobj(adapter);

	bMacPwrCtrlOn = _FALSE;
	rtw_hal_get_hwreg(adapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if (bMacPwrCtrlOn == _TRUE)
		goto out;

	err = rtw_halmac_poweron(d);

	#ifdef CONFIG_POWER_STATE_UNEXPECTED_HDL
	if ((-2) == err) {
		RTW_ERR("%s:Power ON Fail, Try to power on again !!\n", __FUNCTION__);
		rtw_halmac_poweroff(d);
		rtw_msleep_os(2);
		err = rtw_halmac_poweron(d);
	}
	#endif

	if (err) {
		RTW_ERR("%s: Power ON Fail!!\n", __FUNCTION__);
		rtw_warn_on(1);
		ret = _FAIL;
		goto out;
	}

	bMacPwrCtrlOn = _TRUE;
	rtw_hal_set_hwreg(adapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);

out:
	return ret;
}

void rtl8821c_power_off(PADAPTER adapter)
{
	struct dvobj_priv *d;
	u8 bMacPwrCtrlOn;
	int err = 0;


	d = adapter_to_dvobj(adapter);

	bMacPwrCtrlOn = _FALSE;
	rtw_hal_get_hwreg(adapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if (bMacPwrCtrlOn == _FALSE)
		goto out;
	
	bMacPwrCtrlOn = _FALSE;
	rtw_hal_set_hwreg(adapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	GET_HAL_DATA(adapter)->bFWReady = _FALSE;

	err = rtw_halmac_poweroff(d);
	if (err) {
		RTW_ERR("%s: Power OFF Fail!!\n", __FUNCTION__);
		rtw_warn_on(1);
		goto out;
	}

	

out:
	return;
}

u8 rtl8821c_hal_init_main(PADAPTER adapter)
{
	struct dvobj_priv *d = adapter_to_dvobj(adapter);
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	int err;
	s32 ret;

	hal->bFWReady = _FALSE;
	hal->fw_ractrl = _FALSE;

#ifdef CONFIG_NO_FW
	err = rtw_halmac_init_hal(d);
#else
	#ifdef CONFIG_FILE_FWIMG
	rtw_get_phy_file_path(adapter, MAC_FILE_FW_NIC);
	err = rtw_halmac_init_hal_fw_file(d, rtw_phy_para_file_path);
	#else
	err = rtw_halmac_init_hal_fw(d, array_mp_8821c_fw_nic, array_length_mp_8821c_fw_nic);
	#endif

	if (!err) {
		hal->bFWReady = _TRUE;
		hal->fw_ractrl = _TRUE;
	}
	RTW_INFO("FW Version:%d SubVersion:%d\n", hal->firmware_version, hal->firmware_sub_version);
#endif
	if (err) {
		RTW_INFO("%s: fail\n", __FUNCTION__);
		return _FALSE;
	}

	RTW_INFO("%s: successful\n", __FUNCTION__);

	return _TRUE;
}

u8 rtl8821c_mac_verify(PADAPTER adapter)
{
	struct dvobj_priv *d;
	int err;


	d = adapter_to_dvobj(adapter);

	err = rtw_halmac_self_verify(d);
	if (err) {
		RTW_INFO("%s fail\n", __FUNCTION__);
		return _FALSE;
	}

	RTW_INFO("%s successful\n", __FUNCTION__);
	return _TRUE;
}

void rtl8821c_hal_init_misc(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal_data = GET_HAL_DATA(adapter);
	u8 drv_info_sz = 0;
	u32	rcr_bits;
	/*
	 * Sync driver status and hardware setting
	 */

	/* initial security setting */
	invalidate_cam_all(adapter);

	/* enable to rx ps-poll ,disable Control frame filter*/
	rtw_write16(adapter, REG_RXFLTMAP1_8821C, 0x0400);
	/* Accept all data frames */
	rtw_write16(adapter, REG_RXFLTMAP2_8821C, 0xFFFF);

	/* Accept all management frames */
	rtw_write16(adapter, REG_RXFLTMAP0_8821C, 0xFFFF);

	/*RCR setting - Sync driver status with hardware setting */
	rtw_hal_get_hwreg(adapter, HW_VAR_RCR, (u8 *)&rcr_bits);

	rcr_bits &= ~(BIT_AICV_8821C | BIT_ACRC32_8821C | BIT_APP_FCS_8821C | BIT_APWRMGT_8821C);

	rtw_halmac_get_rx_drv_info_sz(adapter_to_dvobj(adapter), &drv_info_sz);
	if (drv_info_sz)
		rcr_bits |= BIT_APP_PHYSTS_8821C;

#ifdef CONFIG_RX_PACKET_APPEND_FCS
	rcr_bits |= BIT_APP_FCS_8821C;
#endif

#ifdef CONFIG_RX_PACKET_APPEND_ICV_ERROR
	rcr_bits |= BIT_AICV_8821C;
#endif

	rtw_hal_set_hwreg(adapter, HW_VAR_RCR, (u8 *)&rcr_bits);

#ifdef CONFIG_XMIT_ACK
	rtl8821c_set_mgnt_xmit_ack(adapter);
#endif /*CONFIG_XMIT_ACK*/

	/*Disable BAR, suggested by Scott */
	rtw_write32(adapter, REG_BAR_MODE_CTRL_8821C, 0x01ffff|rtw_read8(adapter,REG_RA_TRY_RATE_AGG_LMT_8821C)<<24);
	/*Disable secondary CCA 20M,40M?*/
	rtw_write8(adapter, REG_MISC_CTRL_8821C, 0x03);

	/*Enable MAC security engine*/
	rtw_write16(adapter, REG_CR, (rtw_read16(adapter, REG_CR) | BIT_MAC_SEC_EN));

	rtl8821c_rx_tsf_addr_filter_config(adapter, BIT_CHK_TSF_EN_8821C | BIT_CHK_TSF_CBSSID_8821C);

	/*for 1212 module - 5G RX issue*/
	if (hal_data->rfe_type == 2)
		rtw_write8(adapter, REG_PAD_CTRL1 + 3, 0x36);
#ifdef CONFIG_AMPDU_PRETX_CD
	rtl8821c_pretx_cd_config(adapter);
#endif
}

u32 rtl8821c_hal_init(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal;

	hal = GET_HAL_DATA(adapter);

	if (_FALSE == rtl8821c_hal_init_main(adapter))
		return _FAIL;

	rtl8821c_hal_init_misc(adapter);

	rtl8821c_phy_init_haldm(adapter);
#ifdef CONFIG_BEAMFORMING
	rtl8821c_phy_bf_init(adapter);
#endif

#ifdef CONFIG_FW_MULTI_PORT_SUPPORT
	/*HW / FW init*/
	rtw_hal_set_default_port_id_cmd(adapter, 0);
#endif

#ifdef CONFIG_BT_COEXIST
	/* Init BT hw config. */
	if (_TRUE == hal->EEPROMBluetoothCoexist) {
		rtw_btcoex_HAL_Initialize(adapter, _FALSE);
		#ifdef CONFIG_FW_MULTI_PORT_SUPPORT
		rtw_hal_set_wifi_btc_port_id_cmd(adapter);
		#endif
	} else
#endif /* CONFIG_BT_COEXIST */
		rtw_btcoex_wifionly_hw_config(adapter);

	return _SUCCESS;
}

u32 rtl8821c_hal_deinit(PADAPTER adapter)
{
	struct dvobj_priv *d;
	PHAL_DATA_TYPE hal;
	int err;


	d = adapter_to_dvobj(adapter);
	hal = GET_HAL_DATA(adapter);

	hal->bFWReady = _FALSE;
	hal->fw_ractrl = _FALSE;

	err = rtw_halmac_deinit_hal(d);
	if (err)
		return _FAIL;

	return _SUCCESS;
}

void rtl8821c_init_default_value(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal;
	u8 i;


	hal = GET_HAL_DATA(adapter);


	/* init default value */
	hal->fw_ractrl = _FALSE;

	/* init phydm default value */
	hal->bIQKInitialized = _FALSE;

	/* init Efuse variables */
	hal->EfuseUsedBytes = 0;
	hal->EfuseUsedPercentage = 0;
#ifdef HAL_EFUSE_MEMORY
	hal->EfuseHal.fakeEfuseBank = 0;
	hal->EfuseHal.fakeEfuseUsedBytes = 0;
	_rtw_memset(hal->EfuseHal.fakeEfuseContent, 0xFF, EFUSE_MAX_HW_SIZE);
	_rtw_memset(hal->EfuseHal.fakeEfuseInitMap, 0xFF, EFUSE_MAX_MAP_LEN);
	_rtw_memset(hal->EfuseHal.fakeEfuseModifiedMap, 0xFF, EFUSE_MAX_MAP_LEN);
	hal->EfuseHal.BTEfuseUsedBytes = 0;
	hal->EfuseHal.BTEfuseUsedPercentage = 0;
	_rtw_memset(hal->EfuseHal.BTEfuseContent, 0xFF, EFUSE_MAX_BT_BANK * EFUSE_MAX_HW_SIZE);
	_rtw_memset(hal->EfuseHal.BTEfuseInitMap, 0xFF, EFUSE_BT_MAX_MAP_LEN);
	_rtw_memset(hal->EfuseHal.BTEfuseModifiedMap, 0xFF, EFUSE_BT_MAX_MAP_LEN);
	hal->EfuseHal.fakeBTEfuseUsedBytes = 0;
	_rtw_memset(hal->EfuseHal.fakeBTEfuseContent, 0xFF, EFUSE_MAX_BT_BANK * EFUSE_MAX_HW_SIZE);
	_rtw_memset(hal->EfuseHal.fakeBTEfuseInitMap, 0xFF, EFUSE_BT_MAX_MAP_LEN);
	_rtw_memset(hal->EfuseHal.fakeBTEfuseModifiedMap, 0xFF, EFUSE_BT_MAX_MAP_LEN);
#endif
}
