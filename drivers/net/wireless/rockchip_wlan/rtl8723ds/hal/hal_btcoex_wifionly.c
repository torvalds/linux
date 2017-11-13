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
#include "btc/mp_precomp.h"
#include <hal_btcoex_wifionly.h>

struct  wifi_only_cfg GLBtCoexistWifiOnly;

void halwifionly_write1byte(PVOID pwifionlyContext, u32 RegAddr, u8 Data)
{
	struct wifi_only_cfg *pwifionlycfg = (struct wifi_only_cfg *)pwifionlyContext;
	PADAPTER		Adapter = pwifionlycfg->Adapter;

	rtw_write8(Adapter, RegAddr, Data);
}

void halwifionly_write2byte(PVOID pwifionlyContext, u32 RegAddr, u16 Data)
{
	struct wifi_only_cfg *pwifionlycfg = (struct wifi_only_cfg *)pwifionlyContext;
	PADAPTER		Adapter = pwifionlycfg->Adapter;

	rtw_write16(Adapter, RegAddr, Data);
}

void halwifionly_write4byte(PVOID pwifionlyContext, u32 RegAddr, u32 Data)
{
	struct wifi_only_cfg *pwifionlycfg = (struct wifi_only_cfg *)pwifionlyContext;
	PADAPTER		Adapter = pwifionlycfg->Adapter;

	rtw_write32(Adapter, RegAddr, Data);
}

u8 halwifionly_read1byte(PVOID pwifionlyContext, u32 RegAddr)
{
	struct wifi_only_cfg *pwifionlycfg = (struct wifi_only_cfg *)pwifionlyContext;
	PADAPTER		Adapter = pwifionlycfg->Adapter;

	return rtw_read8(Adapter, RegAddr);
}

u16 halwifionly_read2byte(PVOID pwifionlyContext, u32 RegAddr)
{
	struct wifi_only_cfg *pwifionlycfg = (struct wifi_only_cfg *)pwifionlyContext;
	PADAPTER		Adapter = pwifionlycfg->Adapter;

	return rtw_read16(Adapter, RegAddr);
}

u32 halwifionly_read4byte(PVOID pwifionlyContext, u32 RegAddr)
{
	struct wifi_only_cfg *pwifionlycfg = (struct wifi_only_cfg *)pwifionlyContext;
	PADAPTER		Adapter = pwifionlycfg->Adapter;

	return rtw_read32(Adapter, RegAddr);
}

void halwifionly_bitmaskwrite1byte(PVOID pwifionlyContext, u32 regAddr, u8 bitMask, u8 data)
{
	u8 originalValue, bitShift = 0;
	u8 i;

	struct wifi_only_cfg *pwifionlycfg = (struct wifi_only_cfg *)pwifionlyContext;
	PADAPTER		Adapter = pwifionlycfg->Adapter;

	if (bitMask != 0xff) {
		originalValue = rtw_read8(Adapter, regAddr);
		for (i = 0; i <= 7; i++) {
			if ((bitMask >> i) & 0x1)
				break;
		}
		bitShift = i;
		data = ((originalValue) & (~bitMask)) | (((data << bitShift)) & bitMask);
	}
	rtw_write8(Adapter, regAddr, data);
}

void halwifionly_phy_set_rf_reg(PVOID pwifionlyContext, enum rf_path eRFPath, u32 RegAddr, u32 BitMask, u32 Data)
{
	struct wifi_only_cfg *pwifionlycfg = (struct wifi_only_cfg *)pwifionlyContext;
	PADAPTER		Adapter = pwifionlycfg->Adapter;

	phy_set_rf_reg(Adapter, eRFPath, RegAddr, BitMask, Data);
}

void halwifionly_phy_set_bb_reg(PVOID pwifionlyContext, u32 RegAddr, u32 BitMask, u32 Data)
{
	struct wifi_only_cfg *pwifionlycfg = (struct wifi_only_cfg *)pwifionlyContext;
	PADAPTER		Adapter = pwifionlycfg->Adapter;

	phy_set_bb_reg(Adapter, RegAddr, BitMask, Data);
}

void hal_btcoex_wifionly_switchband_notify(PADAPTER padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u8 is_5g = _FALSE;

	if (pHalData->current_band_type == BAND_ON_5G)
		is_5g = _TRUE;

	if (IS_HARDWARE_TYPE_8822B(padapter))
		ex_hal8822b_wifi_only_switchbandnotify(&GLBtCoexistWifiOnly, is_5g);
	else if (IS_HARDWARE_TYPE_8821C(padapter))
		ex_hal8821c_wifi_only_switchbandnotify(&GLBtCoexistWifiOnly, is_5g);
}

void hal_btcoex_wifionly_scan_notify(PADAPTER padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u8 is_5g = _FALSE;

	if (pHalData->current_band_type == BAND_ON_5G)
		is_5g = _TRUE;

	if (IS_HARDWARE_TYPE_8822B(padapter))
		ex_hal8822b_wifi_only_scannotify(&GLBtCoexistWifiOnly, is_5g);
	else if (IS_HARDWARE_TYPE_8821C(padapter))
		ex_hal8821c_wifi_only_scannotify(&GLBtCoexistWifiOnly, is_5g);
}

void hal_btcoex_wifionly_hw_config(PADAPTER padapter)
{
	struct wifi_only_cfg *pwifionlycfg = &GLBtCoexistWifiOnly;

	if (IS_HARDWARE_TYPE_8723B(padapter))
		ex_hal8723b_wifi_only_hw_config(pwifionlycfg);
	else if (IS_HARDWARE_TYPE_8822B(padapter))
		ex_hal8822b_wifi_only_hw_config(pwifionlycfg);
	else if (IS_HARDWARE_TYPE_8821C(padapter))
		ex_hal8821c_wifi_only_hw_config(pwifionlycfg);
}

void hal_btcoex_wifionly_initlizevariables(PADAPTER padapter)
{
	struct wifi_only_cfg		*pwifionlycfg = &GLBtCoexistWifiOnly;
	struct wifi_only_haldata	*pwifionly_haldata = &pwifionlycfg->haldata_info;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	_rtw_memset(&GLBtCoexistWifiOnly, 0, sizeof(GLBtCoexistWifiOnly));

	pwifionlycfg->Adapter = padapter;

#ifdef CONFIG_PCI_HCI
	pwifionlycfg->chip_interface = WIFIONLY_INTF_PCI;
#elif defined(CONFIG_USB_HCI)
	pwifionlycfg->chip_interface = WIFIONLY_INTF_USB;
#elif defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	pwifionlycfg->chip_interface = WIFIONLY_INTF_SDIO;
#else
	pwifionlycfg->chip_interface = WIFIONLY_INTF_UNKNOWN;
#endif

	pwifionly_haldata->customer_id = CUSTOMER_NORMAL;
}

void hal_btcoex_wifionly_AntInfoSetting(PADAPTER padapter)
{
	struct wifi_only_cfg		*pwifionlycfg = &GLBtCoexistWifiOnly;
	struct wifi_only_haldata	*pwifionly_haldata = &pwifionlycfg->haldata_info;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	pwifionly_haldata->efuse_pg_antnum = pHalData->EEPROMBluetoothAntNum;
	pwifionly_haldata->efuse_pg_antpath = pHalData->ant_path;
	pwifionly_haldata->rfe_type = pHalData->rfe_type;
	pwifionly_haldata->ant_div_cfg = pHalData->AntDivCfg;
}

