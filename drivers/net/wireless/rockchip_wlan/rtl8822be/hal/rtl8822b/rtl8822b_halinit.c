/******************************************************************************
 *
 * Copyright(c) 2015 - 2016 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _RTL8822B_HALINIT_C_

#include <drv_types.h>		/* PADAPTER, basic_types.h and etc. */
#include <hal_data.h>		/* GET_HAL_SPEC(), HAL_DATA_TYPE */
#include "../hal_halmac.h"	/* HALMAC_SECURITY_CAM_ENTRY_NUM_8822B */
#include "rtl8822b.h"


void rtl8822b_init_hal_spec(PADAPTER adapter)
{
	struct hal_spec_t *hal_spec;


	hal_spec = GET_HAL_SPEC(adapter);

	hal_spec->macid_num = 128;
	hal_spec->sec_cam_ent_num = HALMAC_SECURITY_CAM_ENTRY_NUM_8822B;
	hal_spec->sec_cap = SEC_CAP_CHK_BMC;
	hal_spec->nss_num = 2;
	hal_spec->band_cap = BAND_CAP_2G | BAND_CAP_5G;
	hal_spec->bw_cap = BW_CAP_20M | BW_CAP_40M | BW_CAP_80M;
	hal_spec->port_num = 5;
	hal_spec->proto_cap = PROTO_CAP_11B | PROTO_CAP_11G | PROTO_CAP_11N | PROTO_CAP_11AC;

	hal_spec->wl_func = 0
			    | WL_FUNC_P2P
			    | WL_FUNC_MIRACAST
			    | WL_FUNC_TDLS
			    ;
}

u32 rtl8822b_power_on(PADAPTER adapter)
{
	struct dvobj_priv *d;
	PHAL_DATA_TYPE hal;
	u8 bMacPwrCtrlOn;
	int err = 0;
	u8 ret = _TRUE;


	d = adapter_to_dvobj(adapter);

	bMacPwrCtrlOn = _FALSE;
	rtw_hal_get_hwreg(adapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if (bMacPwrCtrlOn == _TRUE)
		goto out;

	err = rtw_halmac_poweron(d);
	if (err) {
		RTW_INFO("%s: Power ON Fail!!\n", __FUNCTION__);
		ret = _FALSE;
		goto out;
	}

	bMacPwrCtrlOn = _TRUE;
	rtw_hal_set_hwreg(adapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);

#ifdef CONFIG_BT_COEXIST
	hal = GET_HAL_DATA(adapter);
	if (hal->EEPROMBluetoothCoexist)
		rtw_btcoex_PowerOnSetting(adapter);
#endif /* CONFIG_BT_COEXIST */
out:
	return ret;
}

void rtl8822b_power_off(PADAPTER adapter)
{
	struct dvobj_priv *d;
	u8 bMacPwrCtrlOn;
	int err = 0;


	d = adapter_to_dvobj(adapter);

	bMacPwrCtrlOn = _FALSE;
	rtw_hal_get_hwreg(adapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if (bMacPwrCtrlOn == _FALSE)
		goto out;

	err = rtw_halmac_poweroff(d);
	if (err) {
		RTW_INFO("%s: Power OFF Fail!!\n", __FUNCTION__);
		goto out;
	}

	bMacPwrCtrlOn = _FALSE;
	rtw_hal_set_hwreg(adapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);

	adapter->bFWReady = _FALSE;

out:
	return;
}

#ifdef CONFIG_EMBEDDED_FWIMG
/* New FW array is TBD. So use phydm FW.
 * TODO remove extern reference after FW array is defined
 */
extern u8 Array_MP_8822B_FW_NIC[];
extern u32 ArrayLength_MP_8822B_FW_NIC;
#endif

u8 rtl8822b_hal_init(PADAPTER adapter)
{
	struct dvobj_priv *d;
	PHAL_DATA_TYPE hal;
	int err;


	d = adapter_to_dvobj(adapter);
	hal = GET_HAL_DATA(adapter);

	adapter->bFWReady = _FALSE;
	hal->fw_ractrl = _FALSE;

#ifdef CONFIG_EMBEDDED_FWIMG
	RTW_INFO("%s: Load embedded fw img\n", __FUNCTION__);
	err = rtw_halmac_init_hal_fw(d, Array_MP_8822B_FW_NIC, ArrayLength_MP_8822B_FW_NIC);
#else
	RTW_INFO("%s: Load fw file\n", __FUNCTION__);
	err = rtw_halmac_init_hal_fw_file(d, REALTEK_CONFIG_PATH "RTL8822Bfw_NIC.bin");
#endif
	if (err) {
		RTW_INFO("%s: fail\n", __FUNCTION__);
		return _FALSE;
	}

	
	RTW_INFO("%s: successful, fw_ver=%d fw_subver=%d\n", __FUNCTION__, hal->FirmwareVersion, hal->FirmwareSubVersion);

	/* Sync driver status with hardware setting */
	rtl8822b_rcr_get(adapter, NULL);
	adapter->bFWReady = _TRUE;
	hal->fw_ractrl = _TRUE;

	return _TRUE;
}

u8 rtl8822b_mac_verify(PADAPTER adapter)
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

void rtl8822b_init_misc(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal;
	u8 v8 = 0;
	u32 v32 = 0;


	hal = GET_HAL_DATA(adapter);


	/*
	 * Sync driver status and hardware setting
	 */

	/* initial channel setting */
	if (IS_A_CUT(hal->VersionID) || IS_B_CUT(hal->VersionID)) {
		/* for A/B cut use under only 5G */
		u8 i = 0;
		struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
		PADAPTER iface = NULL;

		RTW_INFO("%s: under only 5G for A/B cut\n", __FUNCTION__);
		RTW_INFO("%s: not support HT/VHT RX STBC for A/B cut\n", __FUNCTION__);

		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			if (iface) {
				iface->registrypriv.wireless_mode = WIRELESS_MODE_5G;
				iface->registrypriv.channel = 149;

				iface->registrypriv.stbc_cap &= ~(BIT0 | BIT4);
			}
		}
	}

	hal->CurrentBandType = BAND_MAX;
	set_channel_bwmode(adapter, adapter->registrypriv.channel, 0, 0);

	/* initial security setting */
	invalidate_cam_all(adapter);

	/* check RCR/ICV bit */
	rtl8822b_rcr_clear(adapter, BIT_ACRC32_8822B | BIT_AICV_8822B);

	/* clear rx ctrl frame */
	rtw_write16(adapter, REG_RXFLTMAP1_8822B, 0);
}

u32 rtl8822b_init(PADAPTER adapter)
{
	u8 ok = _TRUE;
	PHAL_DATA_TYPE hal;

	hal = GET_HAL_DATA(adapter);

	ok = rtl8822b_hal_init(adapter);
	if (_FALSE == ok)
		return _FAIL;

	rtl8822b_phy_init_haldm(adapter);
#ifdef CONFIG_BEAMFORMING
	rtl8822b_phy_init_beamforming(adapter);
#endif

#ifdef CONFIG_BT_COEXIST
	/* Init BT hw config. */
	if (_TRUE == hal->EEPROMBluetoothCoexist)
		rtw_btcoex_HAL_Initialize(adapter, _FALSE);
#endif /* CONFIG_BT_COEXIST */

	rtl8822b_init_misc(adapter);

	return _SUCCESS;
}

u32 rtl8822b_deinit(PADAPTER adapter)
{
	struct dvobj_priv *d;
	PHAL_DATA_TYPE hal;
	int err;


	d = adapter_to_dvobj(adapter);
	hal = GET_HAL_DATA(adapter);

	adapter->bFWReady = _FALSE;
	hal->fw_ractrl = _FALSE;

	err = rtw_halmac_deinit_hal(d);
	if (err)
		return _FAIL;

	return _SUCCESS;
}

void rtl8822b_init_default_value(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal;
	u8 i;


	hal = GET_HAL_DATA(adapter);

	adapter->registrypriv.wireless_mode = WIRELESS_MODE_24G | WIRELESS_MODE_5G;

	/* init default value */
	hal->fw_ractrl = _FALSE;

	if (!adapter_to_pwrctl(adapter)->bkeepfwalive)
		hal->LastHMEBoxNum = 0;

	/* init phydm default value */
	hal->bIQKInitialized = _FALSE;
	hal->odmpriv.RFCalibrateInfo.TM_Trigger = 0; /* for IQK */
	hal->odmpriv.RFCalibrateInfo.ThermalValue_HP_index = 0;
	for (i = 0; i < HP_THERMAL_NUM; i++)
		hal->odmpriv.RFCalibrateInfo.ThermalValue_HP[i] = 0;

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
