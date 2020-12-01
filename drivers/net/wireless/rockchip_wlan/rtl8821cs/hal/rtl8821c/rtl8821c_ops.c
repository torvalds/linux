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
#define _RTL8821C_OPS_C_

#include <drv_types.h>		/* basic_types.h, rtw_io.h and etc. */
#include <rtw_xmit.h>		/* struct xmit_priv */
#ifdef DBG_CONFIG_ERROR_DETECT
	#include <rtw_sreset.h>
#endif /* DBG_CONFIG_ERROR_DETECT */
#include <hal_data.h>		/* PHAL_DATA_TYPE, GET_HAL_DATA() */
#include <hal_com.h>		/* dump_chip_info() and etc. */
#include "../hal_halmac.h"	/* GET_RX_DESC_XXX_8821C() */
#include "rtl8821c.h"
#ifdef CONFIG_PCI_HCI
	#include "rtl8821ce_hal.h"
#endif
#include "rtl8821c_dm.h"
#ifdef CONFIG_SDIO_HCI
#include "sdio/rtl8821cs.h"
#endif

static void read_chip_version(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal;
	u32 value32;


	hal = GET_HAL_DATA(adapter);

	value32 = rtw_read32(adapter, REG_SYS_CFG1_8821C);
	hal->version_id.ICType = CHIP_8821C;
	hal->version_id.ChipType = ((value32 & BIT_RTL_ID_8821C) ? TEST_CHIP : NORMAL_CHIP);
	hal->version_id.CUTVersion = BIT_GET_CHIP_VER_8821C(value32);
	hal->version_id.VendorType = BIT_GET_VENDOR_ID_8821C(value32);
	hal->version_id.VendorType >>= 2;
	switch (hal->version_id.VendorType) {
	case 0:
		hal->version_id.VendorType = CHIP_VENDOR_TSMC;
		break;
	case 1:
		hal->version_id.VendorType = CHIP_VENDOR_SMIC;
		break;
	case 2:
		hal->version_id.VendorType = CHIP_VENDOR_UMC;
		break;
	}
	hal->version_id.RFType = ((value32 & BIT_RF_TYPE_ID_8821C) ? RF_TYPE_2T2R : RF_TYPE_1T1R);
	hal->RegulatorMode = ((value32 & BIT_SPSLDO_SEL_8821C) ? RT_LDO_REGULATOR : RT_SWITCHING_REGULATOR);

	value32 = rtw_read32(adapter, REG_SYS_STATUS1_8821C);
	hal->version_id.ROMVer = BIT_GET_RF_RL_ID_8821C(value32);

	/* For multi-function consideration. */
	hal->MultiFunc = RT_MULTI_FUNC_NONE;
	value32 = rtw_read32(adapter, REG_WL_BT_PWR_CTRL_8821C);
	hal->MultiFunc |= ((value32 & BIT_WL_FUNC_EN_8821C) ? RT_MULTI_FUNC_WIFI : 0);
	hal->MultiFunc |= ((value32 & BIT_BT_FUNC_EN_8821C) ? RT_MULTI_FUNC_BT : 0);
	hal->PolarityCtl = ((value32 & BIT_WL_HWPDN_SL_8821C) ? RT_POLARITY_HIGH_ACT : RT_POLARITY_LOW_ACT);

	dump_chip_info(hal->version_id);
}

/*
 * Return:
 *	_TRUE	valid ID
 *	_FALSE	invalid ID
 */
static u8 Hal_EfuseParseIDCode(PADAPTER adapter, u8 *map)
{
	u16 EEPROMId;


	/* Check 0x8129 again for making sure autoload status!! */
	EEPROMId = le16_to_cpu(*(u16 *)map);
	RTW_INFO("EEPROM ID = 0x%04x\n", EEPROMId);
	if (EEPROMId == RTL_EEPROM_ID)
		return _TRUE;

	RTW_INFO("<WARN> EEPROM ID is invalid!!\n");
	return _FALSE;
}

static void Hal_EfuseParseEEPROMVer(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	if (_TRUE == mapvalid)
		hal->EEPROMVersion = map[EEPROM_VERSION_8821C];
	else
		hal->EEPROMVersion = 1;

	RTW_INFO("EEPROM Version = %d\n", hal->EEPROMVersion);
}

static void Hal_EfuseParseTxPowerInfo(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);

	hal->txpwr_pg_mode = TXPWR_PG_WITH_PWR_IDX;

	if ((_TRUE == mapvalid) && (map[EEPROM_RF_BOARD_OPTION_8821C] != 0xFF))
		hal->EEPROMRegulatory = map[EEPROM_RF_BOARD_OPTION_8821C] & 0x7; /* bit0~2 */
	else
		hal->EEPROMRegulatory = EEPROM_DEFAULT_BOARD_OPTION & 0x7; /* bit0~2 */
	RTW_INFO("EEPROM Regulatory=0x%02x\n", hal->EEPROMRegulatory);
}

static void Hal_EfuseParseBoardType(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	if ((_TRUE == mapvalid) && (map[EEPROM_RF_BOARD_OPTION_8821C] != 0xFF))
		hal->InterfaceSel = (map[EEPROM_RF_BOARD_OPTION_8821C] & 0xE0) >> 5;
	else
		hal->InterfaceSel = (EEPROM_DEFAULT_BOARD_OPTION & 0xE0) >> 5;

	RTW_INFO("EEPROM Board Type=0x%02x\n", hal->InterfaceSel);
}

static void Hal_EfuseParseBTCoexistInfo(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	u8 setting;
	u32 tmpu4;
	u32 tmp_u32;

	if ((mapvalid == _TRUE) && (map[EEPROM_RF_BOARD_OPTION_8821C] != 0xFF)) {

		tmp_u32 = rtw_read32(adapter, REG_WL_BT_PWR_CTRL_8821C);
		/* 0xc1[7:5] = 0x01 (Combo card) */
		if ((((map[EEPROM_RF_BOARD_OPTION_8821C] & 0xe0) >> 5) == 0x01) && (tmp_u32 & BIT_BT_FUNC_EN_8821C))
			hal->EEPROMBluetoothCoexist = _TRUE;
		else
			hal->EEPROMBluetoothCoexist = _FALSE;
	} else
		hal->EEPROMBluetoothCoexist = _FALSE;

	hal->EEPROMBluetoothType = BT_RTL8821C;

	setting = map[EEPROM_RF_BT_SETTING_8821C];
	if ((_TRUE == mapvalid) && (setting != 0xFF)) {
		/* Bit[0]: Total antenna number
		 * 0: 2-Antenna (WL BT not share Ant, concurrent mode)
		 * 1: 1-Antenna (WL BT share Ant, TDMA mode)
		 */
		hal->EEPROMBluetoothAntNum = setting & BIT(0);
		/*
		 * Bit[6]: One-Ant structure use Ant2(aux.) path or Ant1(main) path
		 *	0: Ant2(aux.)
		 *	1: Ant1(main), default
		 */
		hal->ant_path = (setting & BIT(6)) ? RF_PATH_B : RF_PATH_A;
	} else {
		hal->EEPROMBluetoothAntNum = Ant_x1;
		hal->ant_path = RF_PATH_A;
	}

	RTW_INFO("EEPROM %s BT-coex, ant_num=%d\n",
		 hal->EEPROMBluetoothCoexist == _TRUE ? "Enable" : "Disable",
		 hal->EEPROMBluetoothAntNum == Ant_x2 ? 2 : 1);
}

static void Hal_EfuseParseChnlPlan(PADAPTER adapter, u8 *map, u8 autoloadfail)
{
	hal_com_config_channel_plan(
		adapter,
		map ? &map[EEPROM_COUNTRY_CODE_8821C] : NULL,
		map ? map[EEPROM_CHANNEL_PLAN_8821C] : 0xFF,
		adapter->registrypriv.alpha2,
		adapter->registrypriv.channel_plan,
		autoloadfail
	);
}

static void Hal_EfuseParseXtal(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	if ((_TRUE == mapvalid) && map[EEPROM_XTAL_8821C] != 0xFF)
		hal->crystal_cap = map[EEPROM_XTAL_8821C];
	else
		hal->crystal_cap = EEPROM_Default_CrystalCap;

	RTW_INFO("EEPROM crystal_cap=0x%02x\n", hal->crystal_cap);
}

static void Hal_EfuseParseThermalMeter(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	/* ThermalMeter from EEPROM */
	if ((_TRUE == mapvalid) && (map[EEPROM_THERMAL_METER_8821C] != 0xFF))
		hal->eeprom_thermal_meter = map[EEPROM_THERMAL_METER_8821C];
	else {
		hal->eeprom_thermal_meter = EEPROM_Default_ThermalMeter;
		hal->odmpriv.rf_calibrate_info.is_apk_thermal_meter_ignore = _TRUE;
	}

	RTW_INFO("EEPROM ThermalMeter=0x%02x\n", hal->eeprom_thermal_meter);
}

static void Hal_EfuseParseAntennaDiversity(PADAPTER adapter, u8 *map, u8 mapvalid)
{
#ifdef CONFIG_ANTENNA_DIVERSITY
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	struct registry_priv *registry_par = &adapter->registrypriv;


	if (hal->EEPROMBluetoothCoexist == _TRUE && hal->EEPROMBluetoothAntNum == Ant_x1)
		hal->AntDivCfg = 0;
	else {
		if (registry_par->antdiv_cfg == 2)/* 0:OFF , 1:ON, 2:By EFUSE */
			hal->AntDivCfg = (map[EEPROM_RF_BOARD_OPTION_8821C] & BIT3) ? _TRUE : _FALSE;
		else
			hal->AntDivCfg = registry_par->antdiv_cfg;
	}
	/*hal->TRxAntDivType = S0S1_TRX_HW_ANTDIV;*/
	hal->with_extenal_ant_switch = ((map[EEPROM_RF_BT_SETTING_8821C] & BIT7) >> 7);

	RTW_INFO("%s:EEPROM AntDivCfg=%d, AntDivType=%d, external_ant_switch:%d\n",
		 __func__, hal->AntDivCfg, hal->TRxAntDivType, hal->with_extenal_ant_switch);
#endif /* CONFIG_ANTENNA_DIVERSITY */
}

static void Hal_EfuseTxBBSwing(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal_data = GET_HAL_DATA(adapter);

	if (_TRUE == mapvalid) {
		hal_data->tx_bbswing_24G = map[EEPROM_TX_BBSWING_2G_8821C];
		if (0xFF == hal_data->tx_bbswing_24G)
			hal_data->tx_bbswing_24G = 0;
		hal_data->tx_bbswing_5G = map[EEPROM_TX_BBSWING_5G_8821C];
		if (0xFF == hal_data->tx_bbswing_5G)
			hal_data->tx_bbswing_5G = 0;
	} else {
		hal_data->tx_bbswing_24G = 0;
		hal_data->tx_bbswing_5G = 0;
	}
	RTW_INFO("EEPROM tx_bbswing_24G =0x%02x\n", hal_data->tx_bbswing_24G);
	RTW_INFO("EEPROM tx_bbswing_5G =0x%02x\n", hal_data->tx_bbswing_5G);
}

static void Hal_EfuseParseCustomerID(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	if (_TRUE == mapvalid)
		hal->EEPROMCustomerID = map[EEPROM_CUSTOMER_ID_8821C];
	else
		hal->EEPROMCustomerID = 0;
	RTW_INFO("EEPROM Customer ID=0x%02x\n", hal->EEPROMCustomerID);
}

static void Hal_DetectWoWMode(PADAPTER adapter)
{
#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
	adapter_to_pwrctl(adapter)->bSupportRemoteWakeup = _TRUE;
#else /* !(CONFIG_WOWLAN || CONFIG_AP_WOWLAN) */
	adapter_to_pwrctl(adapter)->bSupportRemoteWakeup = _FALSE;
#endif /* !(CONFIG_WOWLAN || CONFIG_AP_WOWLAN) */

	RTW_INFO("EEPROM SupportRemoteWakeup=%d\n", adapter_to_pwrctl(adapter)->bSupportRemoteWakeup);
}

static void hal_ReadPAType(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal_data = GET_HAL_DATA(adapter);

	if (mapvalid) {
		/* AUTO - Get INFO from eFuse*/
		if (GetRegAmplifierType2G(adapter) == 0) {
			switch (hal_data->rfe_type) {
			default:
					hal_data->PAType_2G = 0;
					hal_data->LNAType_2G = 0;
					hal_data->ExternalPA_2G = 0;
					hal_data->ExternalLNA_2G = 0;
				break;
			}
		} else {
			hal_data->ExternalPA_2G  = (GetRegAmplifierType2G(adapter) & ODM_BOARD_EXT_PA)  ? 1 : 0;
			hal_data->ExternalLNA_2G = (GetRegAmplifierType2G(adapter) & ODM_BOARD_EXT_LNA) ? 1 : 0;
		}

		/* AUTO */
		if (GetRegAmplifierType5G(adapter) == 0) {
			switch (hal_data->rfe_type) {
			default:
				hal_data->PAType_5G = 0;
				hal_data->LNAType_5G = 0;
				hal_data->external_pa_5g = 0;
				hal_data->external_lna_5g = 0;
				break;
			}
		} else {
			hal_data->external_pa_5g  = (GetRegAmplifierType5G(adapter) & ODM_BOARD_EXT_PA_5G)  ? 1 : 0;
			hal_data->external_lna_5g = (GetRegAmplifierType5G(adapter) & ODM_BOARD_EXT_LNA_5G) ? 1 : 0;
		}
	} else {
		/*Get INFO from registry*/
		hal_data->ExternalPA_2G  = EEPROM_Default_PAType;
		hal_data->external_pa_5g  = 0xFF;
		hal_data->ExternalLNA_2G = EEPROM_Default_LNAType;
		hal_data->external_lna_5g = 0xFF;

		if (GetRegAmplifierType2G(adapter) == 0) {
			hal_data->ExternalPA_2G  = 0;
			hal_data->ExternalLNA_2G = 0;
		} else {
			hal_data->ExternalPA_2G  = (GetRegAmplifierType2G(adapter) & ODM_BOARD_EXT_PA)  ? 1 : 0;
			hal_data->ExternalLNA_2G = (GetRegAmplifierType2G(adapter) & ODM_BOARD_EXT_LNA) ? 1 : 0;
		}

		if (GetRegAmplifierType5G(adapter) == 0) {
			hal_data->external_pa_5g  = 0;
			hal_data->external_lna_5g = 0;
		} else {
			hal_data->external_pa_5g  = (GetRegAmplifierType5G(adapter) & ODM_BOARD_EXT_PA_5G)  ? 1 : 0;
			hal_data->external_lna_5g = (GetRegAmplifierType5G(adapter) & ODM_BOARD_EXT_LNA_5G) ? 1 : 0;
		}
	}

	RTW_INFO("EEPROM PAType_2G is 0x%x, ExternalPA_2G = %d\n", hal_data->PAType_2G, hal_data->ExternalPA_2G);
	RTW_INFO("EEPROM PAType_5G is 0x%x, external_pa_5g = %d\n", hal_data->PAType_5G, hal_data->external_pa_5g);
	RTW_INFO("EEPROM LNAType_2G is 0x%x, ExternalLNA_2G = %d\n", hal_data->LNAType_2G, hal_data->ExternalLNA_2G);
	RTW_INFO("EEPROM LNAType_5G is 0x%x, external_lna_5g = %d\n", hal_data->LNAType_5G, hal_data->external_lna_5g);
}

static void Hal_ReadAmplifierType(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal_data = GET_HAL_DATA(adapter);

	if (hal_data->rfe_type < 8) { /*According to RF-EFUSE DOC : R15]*/
		RTW_INFO("WIFI Module is iPA/iLNA\n");
		return;
	}

	hal_ReadPAType(adapter, map, mapvalid);

	/* [2.4G] extPA */
	hal_data->TypeGPA  = hal_data->PAType_2G;

	/* [5G] extPA */
	hal_data->TypeAPA  = hal_data->PAType_5G;

	/* [2.4G] extLNA */
	hal_data->TypeGLNA = hal_data->LNAType_2G;

	/* [5G] extLNA */
	hal_data->TypeALNA = hal_data->LNAType_5G;

	RTW_INFO("EEPROM TypeGPA = 0x%X\n", hal_data->TypeGPA);
	RTW_INFO("EEPROM TypeAPA = 0x%X\n", hal_data->TypeAPA);
	RTW_INFO("EEPROM TypeGLNA = 0x%X\n", hal_data->TypeGLNA);
	RTW_INFO("EEPROM TypeALNA = 0x%X\n", hal_data->TypeALNA);
}

static u8 Hal_ReadRFEType(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	/* [20160-06-22 -R15]
		Type 0 - (2-Ant, DPDT), (2G_WLG, iPA, iLNA, iSW), (5G, iPA, iLNA, iSW)
		Type 1 - (1-Ant, SPDT@Ant1), (2G_WLG, iPA, iLNA, iSW), (5G, iPA, iLNA, iSW)
		Type 2 -(1-Ant, SPDT@Ant1) , (2G_BTG, iPA, iLNA, iSW), (5G, iPA, iLNA, iSW)
		Type 3 - (1-Ant, DPDT@Ant2), (2G_WLG, iPA, iLNA, iSW), (5G, iPA, iLNA, iSW)
		Type 4 - (1-Ant, DPDT@Ant2), (2G_BTG, iPA, iLNA, iSW), (5G, iPA, iLNA, iSW)
		Type 5 - (2-Ant), (2G_WLG, iPA, iLNA, iSW), (5G, iPA, iLNA, iSW)
		Type 6 - (2-Ant), (2G_WLG, iPA, iLNA, iSW), (5G, iPA, iLNA, iSW)
		Type 7 - (1-Ant), (2G_BTG, iPA, iLNA, iSW), (5G, iPA, iLNA, iSW)
	*/

	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);

	/* check registry valye */
	if (GetRegRFEType(adapter) != CONFIG_RTW_RFE_TYPE) {
		hal->rfe_type = GetRegRFEType(adapter);
		goto exit;
	}

	if (mapvalid) {
		/* check efuse map */
		hal->rfe_type = map[EEPROM_RFE_OPTION_8821C];
		if (0xFF != hal->rfe_type)
			goto exit;
	}

	/* error handle */
	hal->rfe_type = 0;
	RTW_ERR("\n\nEmpty EFUSE with unknown REF type!!\n\n");
	RTW_ERR("please program efuse or specify correct RFE type.\n");
	RTW_ERR("cmd: insmod rtl8821cx.ko rtw_RFE_type=<rfe_type>\n\n");
	return _FAIL;

exit:
	RTW_INFO("EEPROM rfe_type=0x%x\n", hal->rfe_type);
	return _SUCCESS;
}


static void Hal_EfuseParsePackageType(PADAPTER adapter, u8 *map, u8 mapvalid)
{
}

#ifdef CONFIG_USB_HCI
static void Hal_ReadUsbModeSwitch(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);

	if (_TRUE == mapvalid)
		/* check efuse 0x06 bit7 */
		hal->EEPROMUsbSwitch = (map[EEPROM_USB_MODE_8821CU] & BIT7) >> 7;
	else
		hal->EEPROMUsbSwitch = _FALSE;

	RTW_INFO("EEPROM USB Switch=%d\n", hal->EEPROMUsbSwitch);
}

static void hal_read_usb_pid_vid(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);

	if (mapvalid == _TRUE) {
		/* VID, PID */
		hal->EEPROMVID = ReadLE2Byte(&map[EEPROM_VID_8821CU]);
		hal->EEPROMPID = ReadLE2Byte(&map[EEPROM_PID_8821CU]);
	} else {
		hal->EEPROMVID = EEPROM_Default_VID;
		hal->EEPROMPID = EEPROM_Default_PID;
	}

	RTW_INFO("EEPROM VID = 0x%04X, PID = 0x%04X\n", hal->EEPROMVID, hal->EEPROMPID);
}

#endif /* CONFIG_USB_HCI */

/*
 * Description:
 *	Collect all information from efuse or files.
 *	This function will do
 *	1. Read registers to check hardware efuse available or not
 *	2. Read Efuse/EEPROM
 *	3. Read file if necessary
 *	4. Parsing Efuse data
 */
u8 rtl8821c_read_efuse(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal;
	u8 val8;
	u8 *efuse_map = NULL;
	u8 valid;
	u8 ret = _FAIL;

	hal = GET_HAL_DATA(adapter);
	efuse_map = hal->efuse_eeprom_data;

	/* 1. Read registers to check hardware eFuse available or not */
	val8 = rtw_read8(adapter, REG_SYS_EEPROM_CTRL_8821C);
	hal->bautoload_fail_flag = (val8 & BIT_AUTOLOAD_SUS_8821C) ? _FALSE : _TRUE;
	/*
	* In 8821C, bautoload_fail_flag is used to present eFuse map is valid
	* or not, no matter the map comes from hardware or files.
	*/

	/* 2. Read eFuse */
	EFUSE_ShadowMapUpdate(adapter, EFUSE_WIFI, 0);

	/* 3. Read Efuse file if necessary */
#ifdef CONFIG_EFUSE_CONFIG_FILE
	if (check_phy_efuse_tx_power_info_valid(adapter) == _FALSE)
		if (Hal_readPGDataFromConfigFile(adapter) != _SUCCESS)
			RTW_INFO("%s: <WARN> invalid phy efuse and read from file fail, will use driver default!!\n", __FUNCTION__);
#endif /* CONFIG_EFUSE_CONFIG_FILE */

	/* 4. Parse Efuse data */
	valid = Hal_EfuseParseIDCode(adapter, efuse_map);
	if (_TRUE == valid)
		hal->bautoload_fail_flag = _FALSE;
	else
		hal->bautoload_fail_flag = _TRUE;

	Hal_EfuseParseEEPROMVer(adapter, efuse_map, valid);
	hal_config_macaddr(adapter, hal->bautoload_fail_flag);
	Hal_EfuseParseTxPowerInfo(adapter, efuse_map, valid);
	Hal_EfuseParseBoardType(adapter, efuse_map, valid);
	Hal_EfuseParseBTCoexistInfo(adapter, efuse_map, valid);
	Hal_EfuseParseChnlPlan(adapter, efuse_map, hal->bautoload_fail_flag);
	Hal_EfuseParseXtal(adapter, efuse_map, valid);
	Hal_EfuseParseThermalMeter(adapter, efuse_map, valid);
	Hal_EfuseParseAntennaDiversity(adapter, efuse_map, valid);
	Hal_EfuseParseCustomerID(adapter, efuse_map, valid);
	Hal_DetectWoWMode(adapter);
	if (Hal_ReadRFEType(adapter, efuse_map, valid) != _SUCCESS)
		goto exit;
	Hal_ReadAmplifierType(adapter, efuse_map, valid);
	Hal_EfuseTxBBSwing(adapter, efuse_map, valid);

	/* Data out of Efuse Map */
	Hal_EfuseParsePackageType(adapter, efuse_map, valid);

#ifdef CONFIG_USB_HCI
	Hal_ReadUsbModeSwitch(adapter, efuse_map, valid);
	hal_read_usb_pid_vid(adapter, efuse_map, valid);
#endif /* CONFIG_USB_HCI */

	/* set coex. ant info once efuse parsing is done */
	rtw_btcoex_set_ant_info(adapter);

	if (hal_read_mac_hidden_rpt(adapter) != _SUCCESS)
		goto exit;
	{
		struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);

		if (hal_spec->hci_type <= 3 && hal_spec->hci_type >= 1) {
			hal->EEPROMBluetoothCoexist = _FALSE;
			RTW_INFO("EEPROM Disable BT-coex by hal_spec\n");
			rtw_btcoex_wifionly_AntInfoSetting(adapter);
		}
	}

	rtw_phydm_read_efuse(adapter);

	ret = _SUCCESS;

exit:
	return ret;
}

void rtl8821c_run_thread(PADAPTER adapter)
{
}

void rtl8821c_cancel_thread(PADAPTER adapter)
{
}

/*
 * Description:
 *	Using 0x100 to check the power status of FW.
 */
static u8 check_ips_status(PADAPTER adapter)
{
	u8 val8;


	RTW_INFO(FUNC_ADPT_FMT ": Read 0x100=0x%02x 0x86=0x%02x\n",
		 FUNC_ADPT_ARG(adapter),
		 rtw_read8(adapter, 0x100), rtw_read8(adapter, 0x86));

	val8 = rtw_read8(adapter, 0x100);
	if (val8 == 0xEA)
		return _TRUE;

	return _FALSE;
}

#ifdef DBG_CONFIG_ERROR_DETECT
static void xmit_status_check(PADAPTER p)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(p);
	struct sreset_priv *psrtpriv = &hal->srestpriv;
	struct xmit_priv *pxmitpriv = &p->xmitpriv;
	systime current_time = 0;
	unsigned int diff_time = 0;
	u32 txdma_status = 0;

	txdma_status = rtw_read32(p, REG_TXDMA_STATUS_8821C);
	if (txdma_status != 0x00) {
		RTW_INFO("%s REG_TXDMA_STATUS:0x%08x\n", __FUNCTION__, txdma_status);
		psrtpriv->tx_dma_status_cnt++;
		psrtpriv->self_dect_case = 4;
		rtw_hal_sreset_reset(p);
	}
	#ifdef DBG_PRE_TX_HANG
	#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI)
	{
		u8 hisr = rtw_read8(p, REG_HISR1_8821C + 3);
		if (hisr & BIT_PRETXERR) {
			RTW_ERR("PRE_TX_HANG\n");
			rtw_write8(p, REG_HISR1_8821C + 3, BIT_PRETXERR);
		}
	}
	#endif
	#endif/*DBG_PRE_TX_HANG*/

#ifdef CONFIG_USB_HCI
	current_time = rtw_get_current_time();

	if (0 == pxmitpriv->free_xmitbuf_cnt || 0 == pxmitpriv->free_xmit_extbuf_cnt) {
		diff_time = rtw_get_passing_time_ms(psrtpriv->last_tx_time);

		if (diff_time > 2000) {
			if (psrtpriv->last_tx_complete_time == 0)
				psrtpriv->last_tx_complete_time = current_time;
			else {
				diff_time = rtw_get_passing_time_ms(psrtpriv->last_tx_complete_time);
				if (diff_time > 4000) {

					RTW_INFO("%s tx hang %s\n", __FUNCTION__,
						(rtw_odm_adaptivity_needed(p)) ? "ODM_BB_ADAPTIVITY" : "");

					if (!rtw_odm_adaptivity_needed(p)) {
						psrtpriv->self_dect_tx_cnt++;
						psrtpriv->self_dect_case = 1;
						rtw_hal_sreset_reset(p);
					}
				}
			}
		}
	}

#endif /* CONFIG_USB_HCI */

	if (psrtpriv->dbg_trigger_point == SRESET_TGP_XMIT_STATUS) {
		psrtpriv->dbg_trigger_point = SRESET_TGP_NULL;
		rtw_hal_sreset_reset(p);
		return;
	}
}

#ifdef CONFIG_USB_HCI
static void check_rx_count(PADAPTER p)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(p);
	struct sreset_priv *psrtpriv = &hal->srestpriv;
	u16 cur_mac_rxff_ptr;

	cur_mac_rxff_ptr = rtw_read16(p, REG_RXFF_PTR_V1_8821C);

#if 0
	RTW_INFO("%s,psrtpriv->last_mac_rxff_ptr = %d , cur_mac_rxff_ptr = %d\n", __func__, psrtpriv->last_mac_rxff_ptr, cur_mac_rxff_ptr);
#endif

	if (psrtpriv->last_mac_rxff_ptr == cur_mac_rxff_ptr) {
		psrtpriv->rx_cnt++;
#if 0
		RTW_INFO("%s,MAC case rx_cnt=%d\n", __func__, psrtpriv->rx_cnt);
#endif
		goto exit;
	}

	psrtpriv->rx_cnt = 0;

exit:

	psrtpriv->last_mac_rxff_ptr = cur_mac_rxff_ptr;

	if (psrtpriv->rx_cnt > 3) {
		psrtpriv->self_dect_case = 2;
		psrtpriv->self_dect_rx_cnt++;
		rtw_hal_sreset_reset(p);
	}
}
#endif/*#ifdef CONFIG_USB_HCI*/

static void linked_status_check(PADAPTER p)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(p);
	struct sreset_priv *psrtpriv = &hal->srestpriv;
	struct	pwrctrl_priv *pwrpriv = adapter_to_pwrctl(p);
	u32 rx_dma_status = 0;

	rx_dma_status = rtw_read32(p, REG_RXDMA_STATUS_8821C);
	if (rx_dma_status != 0x00) {
		RTW_INFO("%s REG_RXDMA_STATUS:0x%08x\n", __FUNCTION__, rx_dma_status);
		psrtpriv->rx_dma_status_cnt++;
		psrtpriv->self_dect_case = 5;
#ifdef CONFIG_USB_HCI
		rtw_hal_sreset_reset(p);
#endif /* CONFIG_USB_HCI */
	}

	if (psrtpriv->self_dect_fw) {
		psrtpriv->self_dect_case = 3;
#ifdef CONFIG_USB_HCI
		rtw_hal_sreset_reset(p);
#endif /* CONFIG_USB_HCI */
	}

#ifdef CONFIG_USB_HCI
	check_rx_count(p);
#endif /* CONFIG_USB_HCI */

	if (psrtpriv->dbg_trigger_point == SRESET_TGP_LINK_STATUS) {
		psrtpriv->dbg_trigger_point = SRESET_TGP_NULL;
		rtw_hal_sreset_reset(p);
		return;
	}
}
#endif /* DBG_CONFIG_ERROR_DETECT */

static const struct hw_port_reg port_cfg[] = {
	/*port 0*/
	{
	.net_type = (REG_CR_8821C + 2),
	.net_type_shift = 0,
	.macaddr = REG_MACID,
	.bssid = REG_BSSID,
	.bcn_ctl = REG_BCN_CTRL,
	.tsf_rst = REG_DUAL_TSF_RST,
	.tsf_rst_bit = BIT_TSFTR_RST,
	.bcn_space = REG_MBSSID_BCN_SPACE,
	.bcn_space_shift = 0,
	.bcn_space_mask = 0xffff,
	.ps_aid = REG_BCN_PSR_RPT_8821C,
	.ta = REG_TRANSMIT_ADDRSS_0_8821C,
	},
	/*port 1*/
	{
	.net_type = (REG_CR_8821C + 2),
	.net_type_shift = 2,
	.macaddr = REG_MACID1,
	.bssid = REG_BSSID1,
	.bcn_ctl = REG_BCN_CTRL_CLINT0,
	.tsf_rst = REG_DUAL_TSF_RST,
	.tsf_rst_bit = BIT_TSFTR_CLI0_RST,
	.bcn_space = REG_MBSSID_BCN_SPACE,
	.bcn_space_shift = 16,
	.bcn_space_mask = 0xfff,
	.ps_aid = REG_BCN_PSR_RPT1_8821C,
	.ta = REG_TRANSMIT_ADDRSS_1_8821C,
	},
	/*port 2*/
	{
	.net_type =  REG_CR_EXT_8821C,
	.net_type_shift = 0,
	.macaddr = REG_MACID2,
	.bssid = REG_BSSID2,
	.bcn_ctl = REG_BCN_CTRL_CLINT1,
	.tsf_rst = REG_DUAL_TSF_RST,
	.tsf_rst_bit = BIT_TSFTR_CLI1_RST,
	.bcn_space = REG_MBSSID_BCN_SPACE2,
	.bcn_space_shift = 0,
	.bcn_space_mask = 0xfff,
	.ps_aid = REG_BCN_PSR_RPT2_8821C,
	.ta = REG_TRANSMIT_ADDRSS_2_8821C,
	},
	/*port 3*/
	{
	.net_type =  REG_CR_EXT_8821C,
	.net_type_shift = 2,
	.macaddr = REG_MACID3,
	.bssid = REG_BSSID3,
	.bcn_ctl = REG_BCN_CTRL_CLINT2,
	.tsf_rst = REG_DUAL_TSF_RST,
	.tsf_rst_bit = BIT_TSFTR_CLI2_RST,
	.bcn_space = REG_MBSSID_BCN_SPACE2,
	.bcn_space_shift = 16,
	.bcn_space_mask = 0xfff,
	.ps_aid = REG_BCN_PSR_RPT3_8821C,
	.ta = REG_TRANSMIT_ADDRSS_3_8821C,
	},
	/*port 4*/
	{
	.net_type =  REG_CR_EXT_8821C,
	.net_type_shift = 4,
	.macaddr = REG_MACID4,
	.bssid = REG_BSSID4,
	.bcn_ctl = REG_BCN_CTRL_CLINT3,
	.tsf_rst = REG_DUAL_TSF_RST,
	.tsf_rst_bit = BIT_TSFTR_CLI3_RST,
	.bcn_space = REG_MBSSID_BCN_SPACE3,
	.bcn_space_shift = 0,
	.bcn_space_mask = 0xfff,
	.ps_aid = REG_BCN_PSR_RPT4_8821C,
	.ta = REG_TRANSMIT_ADDRSS_4_8821C,
	},
};

static u32 hw_bcn_ctrl_addr(_adapter *adapter, u8 hw_port)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	u32 addr = 0;

	if (hw_port >= hal_spec->port_num) {
		RTW_ERR(FUNC_ADPT_FMT" HW Port(%d) invalid\n", FUNC_ADPT_ARG(adapter), hw_port);
		rtw_warn_on(1);
		goto exit;
	}

	addr = port_cfg[hw_port].bcn_ctl;

exit:
	return addr;
}

static void hw_bcn_ctrl_set(_adapter *adapter, u8 hw_port, u8 bcn_ctl_val)
{
	u32 bcn_ctl_addr = 0;

	if (hw_port >= MAX_HW_PORT) {
		RTW_ERR(FUNC_ADPT_FMT" HW Port(%d) invalid\n", FUNC_ADPT_ARG(adapter), hw_port);
		rtw_warn_on(1);
		return;
	}

	bcn_ctl_addr = port_cfg[hw_port].bcn_ctl;
	rtw_write8(adapter, bcn_ctl_addr, bcn_ctl_val);
}

static void hw_bcn_ctrl_add(_adapter *adapter, u8 hw_port, u8 bcn_ctl_val)
{
	u32 bcn_ctl_addr = 0;
	u8 val8 = 0;

	if (hw_port >= MAX_HW_PORT) {
		RTW_ERR(FUNC_ADPT_FMT" HW Port(%d) invalid\n", FUNC_ADPT_ARG(adapter), hw_port);
		rtw_warn_on(1);
		return;
	}

	bcn_ctl_addr = port_cfg[hw_port].bcn_ctl;
	val8 = rtw_read8(adapter, bcn_ctl_addr) | bcn_ctl_val;
	rtw_write8(adapter, bcn_ctl_addr, val8);
}

static void hw_bcn_ctrl_clr(_adapter *adapter, u8 hw_port, u8 bcn_ctl_val)
{
	u32 bcn_ctl_addr = 0;
	u8 val8 = 0;

	if (hw_port >= MAX_HW_PORT) {
		RTW_ERR(FUNC_ADPT_FMT" HW Port(%d) invalid\n", FUNC_ADPT_ARG(adapter), hw_port);
		rtw_warn_on(1);
		return;
	}

	bcn_ctl_addr = port_cfg[hw_port].bcn_ctl;
	val8 = rtw_read8(adapter, bcn_ctl_addr);
	val8 &= ~bcn_ctl_val;
	rtw_write8(adapter, bcn_ctl_addr, val8);
}


static void hw_bcn_func(_adapter *adapter, u8 enable)
{
	if (enable)
		hw_bcn_ctrl_add(adapter, get_hw_port(adapter), BIT_EN_BCN_FUNCTION);
	else
		hw_bcn_ctrl_clr(adapter, get_hw_port(adapter), BIT_EN_BCN_FUNCTION);
}

void hw_port0_tsf_sync(_adapter *adapter)
{
	rtw_write8(adapter, REG_SCHEDULER_RST, rtw_read8(adapter, REG_SCHEDULER_RST) | BIT_SYNC_CLI_ONCE_BY_TBTT_8821C);
}

void hw_tsf_reset(_adapter *adapter)
{
	u8 hw_port = rtw_hal_get_port(adapter);
	u32 tsf_rst_addr = 0;
	u8 tsf_rst_bit = 0;

	if (hw_port >= MAX_HW_PORT) {
		RTW_ERR(FUNC_ADPT_FMT" HW Port(%d) invalid\n", FUNC_ADPT_ARG(adapter), hw_port);
		rtw_warn_on(1);
		return;
	}

	tsf_rst_addr = port_cfg[hw_port].tsf_rst;
	tsf_rst_bit = port_cfg[hw_port].tsf_rst_bit;
	rtw_write8(adapter, tsf_rst_addr, tsf_rst_bit);
}

static void hw_var_set_monitor(PADAPTER adapter)
{
#ifdef CONFIG_WIFI_MONITOR
	u8 tmp_8bit;
	u32 tmp_32bit;
	struct net_device *ndev = adapter->pnetdev;
	struct mon_reg_backup *mon = &GET_HAL_DATA(adapter)->mon_backup;

	mon->known_rcr = 1;
	rtw_hal_get_hwreg(adapter, HW_VAR_RCR, (u8 *)& mon->rcr);

	/* Receive all type */
	tmp_32bit = BIT_AAP_8821C | BIT_APP_PHYSTS_8821C;

	if (ndev->type == ARPHRD_IEEE80211_RADIOTAP) {
		/* Append FCS */
		tmp_32bit |= BIT_APP_FCS_8821C;
	}

	rtw_hal_set_hwreg(adapter, HW_VAR_RCR, (u8 *)& tmp_32bit);

	if (1)
		rtw_halmac_config_rx_info(adapter_to_dvobj(adapter),
			HALMAC_DRV_INFO_PHY_SNIFFER);
	else
		rtw_halmac_config_rx_info(adapter_to_dvobj(adapter),
			HALMAC_DRV_INFO_PHY_PLCP);

	tmp_8bit = rtw_read8(adapter, REG_RX_DRVINFO_SZ_8821C);
	rtw_write8(adapter, REG_RX_DRVINFO_SZ_8821C, (tmp_8bit | 0x80));

	/* Receive all data frames */
	mon->known_rxfilter = 1;
	mon->rxfilter0 = rtw_read16(adapter, REG_RXFLTMAP0_8821C);
	mon->rxfilter1 = rtw_read16(adapter, REG_RXFLTMAP1_8821C);
	mon->rxfilter2 = rtw_read16(adapter, REG_RXFLTMAP2_8821C);
	rtw_write16(adapter, REG_RXFLTMAP0_8821C, 0xFFFF);
	rtw_write16(adapter, REG_RXFLTMAP1_8821C, 0xFFFF);
	rtw_write16(adapter, REG_RXFLTMAP2_8821C, 0xFFFF);
#endif /* CONFIG_WIFI_MONITOR */
}

void hw_set_ta(_adapter *adapter, u8 hw_port, u8 *val)
{
	u8 idx = 0;
	u32 reg = port_cfg[hw_port].ta;

	for (idx = 0 ; idx < ETH_ALEN; idx++)
		rtw_write8(adapter, (reg + idx), val[idx]);

	RTW_INFO("%s("ADPT_FMT") hw port -%d TA: "MAC_FMT"\n",
		__func__, ADPT_ARG(adapter), hw_port, MAC_ARG(val));
}
void hw_set_aid(_adapter *adapter, u8 hw_port, u8 aid)
{
	rtw_write16(adapter, port_cfg[hw_port].ps_aid, (0xF800 | aid));
	RTW_INFO("%s("ADPT_FMT") hw port -%d AID: %d\n",
			__func__, ADPT_ARG(adapter), hw_port, aid);
}
#ifdef CONFIG_CLIENT_PORT_CFG
void rtw_hw_client_port_cfg(_adapter *adapter)
{
	struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 clt_port = get_clt_port(adapter);

	if (clt_port == CLT_PORT_INVALID)
		return;
	RTW_INFO("%s ("ADPT_FMT")\n", __func__, ADPT_ARG(adapter));

	/*Network type*/
	rtw_halmac_set_network_type(adapter_to_dvobj(adapter), clt_port, _HW_STATE_STATION_);
	/*A1*/
	rtw_halmac_set_mac_address(adapter_to_dvobj(adapter), clt_port, adapter_mac_addr(adapter));
	/*A2*/
	hw_set_ta(adapter, clt_port, pmlmeinfo->network.MacAddress);
	/*A3*/
	rtw_halmac_set_bssid(adapter_to_dvobj(adapter), clt_port, pmlmeinfo->network.MacAddress);
	/*Beacon space*/
	rtw_halmac_set_bcn_interval(adapter_to_dvobj(adapter), clt_port, pmlmeinfo->bcn_interval);
	/*AID*/
	hw_set_aid(adapter, clt_port, pmlmeinfo->aid);
	/*Beacon control*/
	hw_bcn_ctrl_set(adapter, clt_port, (BIT_P0_EN_RXBCN_RPT | BIT_EN_BCN_FUNCTION));

	RTW_INFO("%s ("ADPT_FMT") clt_port:%d\n", __func__, ADPT_ARG(adapter), clt_port);
}

/*#define DBG_TSF_MONITOR*/
void rtw_hw_client_port_clr(_adapter *adapter)
{
	u8 null_addr[ETH_ALEN] = {0};
	u8 clt_port = get_clt_port(adapter);

	if (clt_port == CLT_PORT_INVALID)
		return;
	RTW_INFO("%s ("ADPT_FMT") ==> \n", __func__, ADPT_ARG(adapter));
	#ifdef DBG_TSF_MONITOR
	/*Beacon control*/
	hw_bcn_ctrl_clr(adapter, clt_port, BIT_EN_BCN_FUNCTION);
	hw_tsf_reset(adapter);
	#endif
	/*Network type*/
	rtw_halmac_set_network_type(adapter_to_dvobj(adapter), clt_port, _HW_STATE_NOLINK_);
	/*A1*/
	rtw_halmac_set_mac_address(adapter_to_dvobj(adapter), clt_port, null_addr);
	/*A2*/
	hw_set_ta(adapter, clt_port, null_addr);
	/*A3*/
	rtw_halmac_set_bssid(adapter_to_dvobj(adapter), clt_port, null_addr);
 	#ifdef DBG_TSF_MONITOR
	if (0)
	#endif
	/*Beacon control*/
	hw_bcn_ctrl_set(adapter, clt_port, (BIT_DIS_TSF_UDT | BIT_EN_BCN_FUNCTION));
	/*AID*/
	hw_set_aid(adapter, clt_port, 0);
	RTW_INFO("%s("ADPT_FMT") clt_port:%d\n", __func__, ADPT_ARG(adapter), clt_port);
}
#endif

static void hw_var_set_opmode(PADAPTER Adapter, u8 *val)
{
	u8	mode = *((u8 *)val);
	static u8 isMonitor = _FALSE;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);

	if (isMonitor == _TRUE) {
#ifdef CONFIG_WIFI_MONITOR
		struct mon_reg_backup *backup = &GET_HAL_DATA(Adapter)->mon_backup;

		if (backup->known_rcr) {
			backup->known_rcr = 0;
			rtw_hal_set_hwreg(Adapter, HW_VAR_RCR, (u8 *)&backup->rcr);

			if (!!(backup->rcr &= BIT_APP_PHYSTS_8821C))
				rtw_halmac_config_rx_info(adapter_to_dvobj(Adapter),
					HALMAC_DRV_INFO_PHY_STATUS);
			else
				rtw_halmac_config_rx_info(adapter_to_dvobj(Adapter),
					HALMAC_DRV_INFO_NONE);

			rtw_hal_rcr_set_chk_bssid(Adapter, MLME_ACTION_NONE);
		}
		if (backup->known_rxfilter) {
			backup->known_rxfilter = 0;
			rtw_write16(Adapter, REG_RXFLTMAP0_8821C, backup->rxfilter0);
			rtw_write16(Adapter, REG_RXFLTMAP1_8821C, backup->rxfilter1);
			rtw_write16(Adapter, REG_RXFLTMAP2_8821C, backup->rxfilter2);
		}
#endif /* CONFIG_WIFI_MONITOR */
		isMonitor = _FALSE;
	}

	if (mode == _HW_STATE_MONITOR_) {
		isMonitor = _TRUE;

		/* set net_type */
		Set_MSR(Adapter, _HW_STATE_NOLINK_);
		hw_var_set_monitor(Adapter);
		return;
	}

	rtw_hal_set_hwreg(Adapter, HW_VAR_MAC_ADDR, adapter_mac_addr(Adapter)); /* set mac addr to mac register */
	RTW_INFO(ADPT_FMT ": hw_port(%d) set mode=%d\n", ADPT_ARG(Adapter), get_hw_port(Adapter), mode);

#ifdef CONFIG_MI_WITH_MBSSID_CAM /*For Port0 - MBSS CAM*/
	if (Adapter->hw_port != HW_PORT0) {
		RTW_ERR(ADPT_FMT ": Configure MBSSID cam on HW_PORT%d\n", ADPT_ARG(Adapter), Adapter->hw_port);
		rtw_warn_on(1);
	} else
		hw_var_set_opmode_mbid(Adapter, mode);
#else

	rtw_iface_disable_tsf_update(Adapter);

	/* set net_type */
	Set_MSR(Adapter, mode);

	if ((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_)) {
		#ifdef CONFIG_INTERRUPT_BASED_TXBCN
		if (!rtw_mi_get_ap_num(Adapter) && !rtw_mi_get_mesh_num(Adapter)) {
			/*CONFIG_INTERRUPT_BASED_TXBCN*/
			#ifdef CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
			rtw_write8(Adapter, REG_DRVERLYINT, 0x05);/* restore early int time to 5ms */
			#if defined(CONFIG_SDIO_HCI)
			rtl8821cs_update_interrupt_mask(Adapter, 0, SDIO_HIMR_BCNERLY_INT_MSK);
			#endif
			#endif/* CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT */

			#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
			#if defined(CONFIG_SDIO_HCI)
			rtl8821cs_update_interrupt_mask(Adapter, 0, (SDIO_HIMR_TXBCNOK_MSK | SDIO_HIMR_TXBCNERR_MSK));
			#endif
			#endif /* CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR */
		}
		#endif /* CONFIG_INTERRUPT_BASED_TXBCN */

		if (!rtw_mi_get_ap_num(Adapter) && !rtw_mi_get_mesh_num(Adapter))
			StopTxBeacon(Adapter);
		hw_bcn_ctrl_set(Adapter, get_hw_port(Adapter), BIT_EN_BCN_FUNCTION_8821C | BIT_DIS_TSF_UDT_8821C);

	} else if (mode == _HW_STATE_ADHOC_) {
		ResumeTxBeacon(Adapter);
		hw_bcn_ctrl_set(Adapter, get_hw_port(Adapter), BIT_EN_BCN_FUNCTION_8821C | BIT_DIS_TSF_UDT_8821C);

	} else if (mode == _HW_STATE_AP_) {

		if (Adapter->hw_port == HW_PORT0) {
			#ifdef CONFIG_INTERRUPT_BASED_TXBCN
			#ifdef CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
			#if defined(CONFIG_SDIO_HCI)
			rtl8821cs_update_interrupt_mask(Adapter, SDIO_HIMR_BCNERLY_INT_MSK, 0);
			#endif
			#endif/* CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT */

			#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
			#if defined(CONFIG_SDIO_HCI)
			rtl8821cs_update_interrupt_mask(Adapter, (SDIO_HIMR_TXBCNOK_MSK | SDIO_HIMR_TXBCNERR_MSK), 0);
			#endif
			#endif/* CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR */
			#endif /* CONFIG_INTERRUPT_BASED_TXBCN */

			/* enable to rx data frame */
			rtw_write16(Adapter, REG_RXFLTMAP2_8821C, 0xFFFF);

			/* Beacon Control related register for first time */
			rtw_write8(Adapter, REG_BCNDMATIM_8821C, 0x02); /* 2ms	*/

			rtw_write8(Adapter, REG_ATIMWND_8821C, 0x0c); /* ATIM:12ms */

			/*rtw_write16(Adapter, REG_TSFTR_SYN_OFFSET_8821C, 0x7fff);*//* +32767 (~32ms) */
			hw_tsf_reset(Adapter);
			/* don't enable update TSF0 for if1 (due to TSF update when beacon/probe rsp are received) */
		#if defined(CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR)
			hw_bcn_ctrl_set(Adapter, get_hw_port(Adapter), BIT_EN_BCN_FUNCTION_8821C | BIT_DIS_TSF_UDT_8821C | BIT_P0_EN_TXBCN_RPT_8821C);
		#else
			hw_bcn_ctrl_set(Adapter, get_hw_port(Adapter), BIT_EN_BCN_FUNCTION_8821C | BIT_DIS_TSF_UDT_8821C);
		#endif
		} else {
			RTW_ERR(ADPT_FMT ": set AP mode on HW_PORT%d\n", ADPT_ARG(Adapter), Adapter->hw_port);
			rtw_warn_on(1);
		}
	}
#endif
}

#ifdef CONFIG_AP_PORT_SWAP
void rtw_hal_port_reconfig(_adapter *adapter, u8 port)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	u32 bssid_offset = 0;
	u8 bssid[6] = {0};
	u8 vnet_type = 0;
	u8 vbcn_ctrl = 0;
	u8 i;

	if (port > (hal_spec->port_num - 1)) {
		RTW_INFO("[WARN] "ADPT_FMT"- hw_port : %d,will switch to invalid port-%d\n",
			 ADPT_ARG(adapter), adapter->hw_port, port);
		rtw_warn_on(1);
	}

	RTW_PRINT(ADPT_FMT" - hw_port : %d,will switch to port-%d\n",
		  ADPT_ARG(adapter), adapter->hw_port, port);

	/*backup*/
	vnet_type = (rtw_read8(adapter, port_cfg[adapter->hw_port].net_type) >> port_cfg[adapter->hw_port].net_type_shift) & 0x03;
	vbcn_ctrl = rtw_read8(adapter, port_cfg[adapter->hw_port].bcn_ctl);
	if (is_client_associated_to_ap(adapter)) {
		RTW_INFO("port0-iface is STA mode and linked\n");
		bssid_offset = port_cfg[adapter->hw_port].bssid;
		for (i = 0; i < 6; i++)
			bssid[i] = rtw_read8(adapter, bssid_offset + i);
	}
	/*reconfigure*/
	adapter->hw_port = port;
	rtw_hal_set_hwreg(adapter, HW_VAR_MAC_ADDR, adapter_mac_addr(adapter));
	Set_MSR(adapter, vnet_type);

	if (is_client_associated_to_ap(adapter)) {
		rtw_hal_set_hwreg(adapter, HW_VAR_BSSID, bssid);
		hw_tsf_reset(adapter);
		#ifdef CONFIG_FW_MULTI_PORT_SUPPORT
		rtw_set_default_port_id(adapter);
		#endif
	}
#if defined(CONFIG_BT_COEXIST) && defined(CONFIG_FW_MULTI_PORT_SUPPORT)
	if (GET_HAL_DATA(adapter)->EEPROMBluetoothCoexist == _TRUE)
		rtw_hal_set_wifi_btc_port_id_cmd(adapter);
#endif

	rtw_write8(adapter, port_cfg[adapter->hw_port].bcn_ctl, vbcn_ctrl);
}
static void rtl8821c_ap_port_switch(_adapter *adapter, u8 mode)
{
	u8 hw_port = get_hw_port(adapter);
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	u8 ap_nums = 0;
	_adapter *if_port0 = NULL;
	int i;

	RTW_INFO(ADPT_FMT ": hw_port(%d) will set mode to %d\n", ADPT_ARG(adapter), hw_port, mode);
#if 0
	#ifdef CONFIG_P2P
	if (!rtw_p2p_chk_state(&adapter->wdinfo, P2P_STATE_NONE)) {
		RTW_INFO("%s, role=%d, p2p_state=%d, pre_p2p_state=%d\n", __func__,
			rtw_p2p_role(&adapter->wdinfo), rtw_p2p_state(&adapter->wdinfo), rtw_p2p_pre_state(&adapter->wdinfo));
	}
	#endif
#endif

	if (mode != _HW_STATE_AP_)
		return;

	if ((mode == _HW_STATE_AP_) && (hw_port == HW_PORT0))
		return;

	/*check and prepare switch port to port0 for AP mode's BCN function*/
	ap_nums = rtw_mi_get_ap_num(adapter);
	if (ap_nums > 0) {
		RTW_ERR("SortAP mode numbers:%d, must move setting to MBSSID CAM, not support yet\n", ap_nums);
		rtw_warn_on(1);
		return;
	}

	/*Get iface of port-0*/
	for (i = 0; i < dvobj->iface_nums; i++) {
		if (get_hw_port(dvobj->padapters[i]) == HW_PORT0) {
			if_port0 = dvobj->padapters[i];
			break;
		}
	}

	if (if_port0 == NULL) {
		RTW_ERR("%s if_port0 == NULL\n", __func__);
		rtw_warn_on(1);
		return;
	}
	rtw_hal_port_reconfig(if_port0, hw_port);

	adapter->hw_port = HW_PORT0;
	RTW_INFO(ADPT_FMT ": Cfg SoftAP mode to hw_port(%d) done\n", ADPT_ARG(adapter), adapter->hw_port);

}
#endif
static void hw_var_hw_port_cfg(_adapter *adapter, u8 enable)
{
	if (enable)
		hw_bcn_ctrl_add(adapter, get_hw_port(adapter), (BIT_P0_EN_RXBCN_RPT | BIT_DIS_TSF_UDT | BIT_EN_BCN_FUNCTION));
	else
		hw_bcn_ctrl_clr(adapter, get_hw_port(adapter), BIT_EN_BCN_FUNCTION);
}
static void hw_var_set_bcn_func(PADAPTER adapter, u8 enable)
{
	u8 val8;

	if (enable) {
		/* enable TX BCN report
		 *  Reg REG_FWHW_TXQ_CTRL_8821C[2] = 1
		 *  Reg REG_BCN_CTRL_8821C[3][5] = 1
		 */
		val8 = rtw_read8(adapter, REG_FWHW_TXQ_CTRL_8821C);
		val8 |= BIT_EN_BCN_TRXRPT_V1_8821C;
		rtw_write8(adapter, REG_FWHW_TXQ_CTRL_8821C, val8);

		if (adapter->hw_port == HW_PORT0)
			hw_bcn_ctrl_add(adapter, get_hw_port(adapter), BIT_EN_BCN_FUNCTION_8821C | BIT_P0_EN_TXBCN_RPT_8821C);
		else
			hw_bcn_ctrl_add(adapter, get_hw_port(adapter), BIT_EN_BCN_FUNCTION_8821C);
	} else {
		if (adapter->hw_port == HW_PORT0) {
			val8 = BIT_EN_BCN_FUNCTION_8821C | BIT_P0_EN_TXBCN_RPT_8821C;

			#ifdef CONFIG_BT_COEXIST
			/* Always enable port0 beacon function for PSTDMA */
			if (GET_HAL_DATA(adapter)->EEPROMBluetoothCoexist)
				val8 = BIT_P0_EN_TXBCN_RPT_8821C;
			#endif
			hw_bcn_ctrl_clr(adapter, get_hw_port(adapter), val8);
		} else
			hw_bcn_ctrl_clr(adapter, get_hw_port(adapter), BIT_EN_BCN_FUNCTION_8821C);
	}
}

static void hw_var_set_mlme_disconnect(PADAPTER adapter)
{
	u8 val8;

#ifdef DBG_IFACE_STATUS
	DBG_IFACE_STATUS_DUMP(adapter);
#endif

#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_mi_check_status(adapter, MI_LINKED) == _FALSE)
#endif
		/* reject all data frames under not link state */
		rtw_write16(adapter, REG_RXFLTMAP2_8821C, 0);

	/* reset TSF*/
	hw_tsf_reset(adapter);

	/* disable update TSF*/
	rtw_iface_disable_tsf_update(adapter);

	#ifdef CONFIG_CLIENT_PORT_CFG
	if (MLME_IS_STA(adapter))
		rtw_hw_client_port_clr(adapter);
	#endif
}

static void hw_var_set_mlme_sitesurvey(PADAPTER adapter, u8 enable)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	u16 value_rxfltmap2;
	u8 val8;
	PHAL_DATA_TYPE hal;
	struct mlme_priv *pmlmepriv;
	int i;
	_adapter *iface;


#ifdef DBG_IFACE_STATUS
	DBG_IFACE_STATUS_DUMP(adapter);
#endif

	hal = GET_HAL_DATA(adapter);
	pmlmepriv = &adapter->mlmepriv;

#ifdef CONFIG_FIND_BEST_CHANNEL
	/* Receive all data frames */
	value_rxfltmap2 = 0xFFFF;
#else
	/* not to receive data frame */
	value_rxfltmap2 = 0;
#endif

	if (enable) {
		/*
		 * 1. configure REG_RXFLTMAP2
		 * 2. disable TSF update &  buddy TSF update to avoid updating wrong TSF due to clear RCR_CBSSID_BCN
		 * 3. config RCR to receive different BSSID BCN or probe rsp
		 */

		rtw_write16(adapter, REG_RXFLTMAP2_8821C, value_rxfltmap2);

		rtw_hal_rcr_set_chk_bssid(adapter, MLME_SCAN_ENTER);

		if (rtw_mi_get_ap_num(adapter) || rtw_mi_get_mesh_num(adapter))
			StopTxBeacon(adapter);
	} else {
		/* sitesurvey done
		 * 1. enable rx data frame
		 * 2. config RCR not to receive different BSSID BCN or probe rsp
		 * 3. can enable TSF update &  buddy TSF right now due to HW support(IC before 8821C not support ex:8812A/8814A/8192E...)
		 */
		if (rtw_mi_check_fwstate(adapter, WIFI_ASOC_STATE | WIFI_AP_STATE | WIFI_MESH_STATE))/* enable to rx data frame */
			rtw_write16(adapter, REG_RXFLTMAP2_8821C, 0xFFFF);

		rtw_hal_rcr_set_chk_bssid(adapter, MLME_SCAN_DONE);

		#ifdef CONFIG_AP_MODE
		if (rtw_mi_get_ap_num(adapter) || rtw_mi_get_mesh_num(adapter)) {
			ResumeTxBeacon(adapter);
			rtw_mi_tx_beacon_hdl(adapter);
		}
		#endif
	}
}
static void hw_var_set_mlme_join(PADAPTER adapter, u8 type)
{
	u8 val8;
	u16 val16;
	u32 val32;
	u8 RetryLimit;
	PHAL_DATA_TYPE hal;
	struct mlme_priv *pmlmepriv;

	RetryLimit = RL_VAL_STA;
	hal = GET_HAL_DATA(adapter);
	pmlmepriv = &adapter->mlmepriv;


#ifdef CONFIG_CONCURRENT_MODE
	if (type == 0) {
		/* prepare to join */
		if (rtw_mi_get_ap_num(adapter) || rtw_mi_get_mesh_num(adapter))
			StopTxBeacon(adapter);

		/* enable to rx data frame.Accept all data frame */
		rtw_write16(adapter, REG_RXFLTMAP2_8821C, 0xFFFF);

		hw_bcn_func(adapter, _TRUE);
		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
			RetryLimit = (hal->CustomerID == RT_CID_CCX) ? RL_VAL_AP : RL_VAL_STA;
		else /* Ad-hoc Mode */
			RetryLimit = RL_VAL_AP;
		#ifdef CONFIG_CLIENT_PORT_CFG
		rtw_hw_client_port_cfg(adapter);
		#endif

		rtw_iface_enable_tsf_update(adapter);

	} else if (type == 1) {
		/* joinbss_event call back when join res < 0 */
		if (rtw_mi_check_status(adapter, MI_LINKED) == _FALSE)
			rtw_write16(adapter, REG_RXFLTMAP2_8821C, 0x00);

		rtw_iface_disable_tsf_update(adapter);

		if (rtw_mi_get_ap_num(adapter) || rtw_mi_get_mesh_num(adapter)) {
			ResumeTxBeacon(adapter);

			/* reset TSF 1/2 after resume_tx_beacon */
			val8 = BIT_TSFTR_RST_8821C | BIT_TSFTR_CLI0_RST_8821C;
			rtw_write8(adapter, REG_DUAL_TSF_RST_8821C, val8);
		}
		#ifdef CONFIG_CLIENT_PORT_CFG
		if (MLME_IS_STA(adapter))
			rtw_hw_client_port_clr(adapter);
		#endif
	} else if (type == 2) {
		/* sta add event callback */
		if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE)) {
			rtw_write8(adapter, 0x542, 0x02);
			RetryLimit = RL_VAL_AP;
		}

		if (rtw_mi_get_ap_num(adapter) || rtw_mi_get_mesh_num(adapter)) {
			ResumeTxBeacon(adapter);

			/* reset TSF 1/2 after resume_tx_beacon */
			rtw_write8(adapter, REG_DUAL_TSF_RST_8821C, BIT_TSFTR_RST_8821C | BIT_TSFTR_CLI0_RST_8821C);
		}
	}

	val16 = BIT_LRL_8821C(RetryLimit) | BIT_SRL_8821C(RetryLimit);
	rtw_write16(adapter, REG_RETRY_LIMIT_8821C, val16);
#else /* !CONFIG_CONCURRENT_MODE */
	if (type == 0) {
		/* prepare to join */

		/* enable to rx data frame.Accept all data frame */
		rtw_write16(adapter, REG_RXFLTMAP2_8821C, 0xFFFF);

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
			RetryLimit = (hal->CustomerID == RT_CID_CCX) ? RL_VAL_AP : RL_VAL_STA;
		else /* Ad-hoc Mode */
			RetryLimit = RL_VAL_AP;
		hw_bcn_func(adapter, _TRUE);

		rtw_iface_enable_tsf_update(adapter);

	} else if (type == 1) {
		/* joinbss_event call back when join res < 0 */
		rtw_write16(adapter, REG_RXFLTMAP2_8821C, 0x00);

		rtw_iface_disable_tsf_update(adapter);

	} else if (type == 2) {
		/* sta add event callback */
		if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE))
			RetryLimit = RL_VAL_AP;
	}

	val16 = BIT_LRL_8821C(RetryLimit) | BIT_SRL_8821C(RetryLimit);
	rtw_write16(adapter, REG_RETRY_LIMIT_8821C, val16);
#endif /* !CONFIG_CONCURRENT_MODE */
}

static void hw_var_set_acm_ctrl(PADAPTER adapter, u8 ctrl)
{
	u8 hwctrl = 0;

	if (ctrl) {
		hwctrl |= BIT_ACMHWEN_8821C;

		if (ctrl & BIT(1)) /* BE */
			hwctrl |= BIT_BEQ_ACM_EN_8821C;
		else
			hwctrl &= (~BIT_BEQ_ACM_EN_8821C);

		if (ctrl & BIT(2)) /* VI */
			hwctrl |= BIT_VIQ_ACM_EN_8821C;
		else
			hwctrl &= (~BIT_VIQ_ACM_EN_8821C);

		if (ctrl & BIT(3)) /* VO */
			hwctrl |= BIT_VOQ_ACM_EN_8821C;
		else
			hwctrl &= (~BIT_VOQ_ACM_EN_8821C);
	}

	RTW_INFO("[HW_VAR_ACM_CTRL] Write 0x%02X\n", hwctrl);
	rtw_write8(adapter, REG_ACMHWCTRL_8821C, hwctrl);
}

void hw_var_lps_rfon_chk(_adapter *adapter, u8 rfon_ctrl)
{
#ifdef CONFIG_LPS_ACK
	struct pwrctrl_priv 	*pwrpriv = adapter_to_pwrctl(adapter);

	if (rfon_ctrl == rf_on) {
		if (rtw_sctx_wait(&pwrpriv->lps_ack_sctx, __func__)) {
			if (pwrpriv->lps_ack_status > 0)
				RTW_INFO(FUNC_ADPT_FMT" RF_ON function is not ready !!!\n", FUNC_ADPT_ARG(adapter));
		} else {
			RTW_WARN("LPS RFON sctx query timeout, operation abort!!\n");
		}
		pwrpriv->lps_ack_status = -1;
	}
#endif
}

static void hw_var_set_sec_cfg(PADAPTER adapter, u8 cfg)
{
	u16 reg_scr_ori;
	u16 reg_scr;

	reg_scr = reg_scr_ori = rtw_read16(adapter, REG_SECCFG_8821C);
	reg_scr |= (BIT_CHK_KEYID_8821C | BIT_RXDEC_8821C | BIT_TXENC_8821C);

	if (_rtw_camctl_chk_cap(adapter, SEC_CAP_CHK_BMC))
		reg_scr |= BIT_CHK_BMC_8821C;

	if (_rtw_camctl_chk_flags(adapter, SEC_STATUS_STA_PK_GK_CONFLICT_DIS_BMC_SEARCH))
		reg_scr |= BIT_NOSKMC_8821C;

	if (reg_scr != reg_scr_ori)
		rtw_write16(adapter, REG_SECCFG_8821C, reg_scr);

	RTW_INFO("%s: [HW_VAR_SEC_CFG] 0x%x=0x%x\n", __FUNCTION__,
		 REG_SECCFG_8821C, rtw_read32(adapter, REG_SECCFG_8821C));
}

static void hw_var_set_sec_dk_cfg(PADAPTER adapter, u8 enable)
{
	struct security_priv *sec = &adapter->securitypriv;
	u8 reg_scr = rtw_read8(adapter, REG_SECCFG_8821C);

	if (enable) {
		/* Enable default key related setting */
		reg_scr |= BIT_TXBCUSEDK_8821C;
		if (sec->dot11AuthAlgrthm != dot11AuthAlgrthm_8021X)
			reg_scr |= BIT_RXUHUSEDK_8821C | BIT_TXUHUSEDK_8821C;
	} else {
		/* Disable default key related setting */
		reg_scr &= ~(BIT_RXBCUSEDK_8821C | BIT_TXBCUSEDK_8821C | BIT_RXUHUSEDK_8821C | BIT_TXUHUSEDK_8821C);
	}

	rtw_write8(adapter, REG_SECCFG_8821C, reg_scr);

	RTW_INFO("%s: [HW_VAR_SEC_DK_CFG] 0x%x=0x%08x\n", __FUNCTION__,
		 REG_SECCFG_8821C, rtw_read32(adapter, REG_SECCFG_8821C));
}

static void hw_var_set_bcn_valid(PADAPTER adapter)
{
	u8 val8 = 0;

	/* only port 0 can TX BCN */
	val8 = rtw_read8(adapter, REG_FIFOPAGE_CTRL_2_8821C + 1);
	val8 = val8 | BIT(7);
	rtw_write8(adapter, REG_FIFOPAGE_CTRL_2_8821C + 1, val8);
}

static void hw_var_set_ack_preamble(PADAPTER adapter, u8 bShortPreamble)
{
	u8 val8 = 0;


	val8 = rtw_read8(adapter, REG_WMAC_TRXPTCL_CTL_8821C + 2);
	val8 |= BIT(4) | BIT(5);

	if (bShortPreamble)
		val8 |= BIT1;
	else
		val8 &= (~BIT1);

	rtw_write8(adapter, REG_WMAC_TRXPTCL_CTL_8821C + 2, val8);
}

void rtl8821c_dl_rsvd_page(PADAPTER adapter, u8 mstatus)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	BOOLEAN bcn_valid = _FALSE;
	u8 DLBcnCount = 0;
	u32 poll = 0;
	u8 val8, org_bcn, org_cr;
	u8 hw_port = rtw_hal_get_port(adapter);

	RTW_INFO(FUNC_ADPT_FMT ":+ hw_port=%d mstatus(%x)\n",
		 FUNC_ADPT_ARG(adapter), hw_port, mstatus);

	if (mstatus == RT_MEDIA_CONNECT) {
#if 0
		BOOLEAN bRecover = _FALSE;
#endif
		u8 v8;

		/* We should set AID, correct TSF, HW seq enable before set JoinBssReport to Fw in 8821C -bit 10:0 */
		rtw_write16(adapter, port_cfg[hw_port].ps_aid, (0xF800 | pmlmeinfo->aid));

		/* Enable SW TX beacon - Set REG_CR bit 8. DMA beacon by SW */
		v8 = rtw_read8(adapter, REG_CR_8821C + 1);
		org_cr = v8;
		v8 |= (BIT_ENSWBCN_8821C >> 8);
		rtw_write8(adapter, REG_CR_8821C + 1, v8);

		/*
		 * Disable Hw protection for a time which revserd for Hw sending beacon.
		 * Fix download reserved page packet fail that access collision with the protection time.
		 */
		val8 = rtw_read8(adapter, REG_BCN_CTRL_8821C);
		org_bcn = val8;
		val8 &= ~BIT_EN_BCN_FUNCTION_8821C;
		val8 |= BIT_DIS_TSF_UDT_8821C;
		rtw_write8(adapter, REG_BCN_CTRL_8821C, val8);

#if 0
		/* Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame. */
		RegFwHwTxQCtrl = rtw_read8(adapter, REG_FWHW_TXQ_CTRL_8821C + 2);

		if (RegFwHwTxQCtrl & BIT(6))
			bRecover = _TRUE;

		/* To tell Hw the packet is not a real beacon frame. */
		RegFwHwTxQCtrl &= ~BIT(6);
		rtw_write8(adapter, REG_FWHW_TXQ_CTRL_8821C + 2, RegFwHwTxQCtrl);
#endif

		/* Clear beacon valid check bit. */
		rtw_hal_set_hwreg(adapter, HW_VAR_BCN_VALID, NULL);

		DLBcnCount = 0;
		poll = 0;
		do {
			/* download rsvd page. */
			rtw_hal_set_fw_rsvd_page(adapter, _FALSE);
			DLBcnCount++;
			do {
				rtw_yield_os();

				/* check rsvd page download OK. */
				rtw_hal_get_hwreg(adapter, HW_VAR_BCN_VALID, (u8 *)(&bcn_valid));
				poll++;
			} while (!bcn_valid && (poll % 10) != 0 && !RTW_CANNOT_RUN(adapter));

		} while (!bcn_valid && DLBcnCount <= 100 && !RTW_CANNOT_RUN(adapter));

		if (RTW_CANNOT_RUN(adapter))
			;
		else if (!bcn_valid)
			RTW_ERR(FUNC_ADPT_FMT ": DL RSVD page failed! DLBcnCount:%u, poll:%u\n",
				 FUNC_ADPT_ARG(adapter), DLBcnCount, poll);
		else {
			struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);

			pwrctl->fw_psmode_iface_id = adapter->iface_id;
			rtw_hal_set_fw_rsvd_page(adapter, _TRUE);
			RTW_INFO(ADPT_FMT ": DL RSVD page success! DLBcnCount:%u, poll:%u\n",
				 ADPT_ARG(adapter), DLBcnCount, poll);
		}

		rtw_write8(adapter, REG_CR_8821C + 1, org_cr);
		rtw_write8(adapter, REG_BCN_CTRL_8821C, org_bcn);
#if 0
		/*
		 * To make sure that if there exists an adapter which would like to send beacon.
		 * If exists, the origianl value of 0x422[6] will be 1, we should check this to
		 * prevent from setting 0x422[6] to 0 after download reserved page, or it will cause
		 * the beacon cannot be sent by HW.
		 */
		if (bRecover) {
			RegFwHwTxQCtrl |= BIT(6);
			rtw_write8(adapter, REG_FWHW_TXQ_CTRL_8821C + 2, RegFwHwTxQCtrl);
		}
#endif
#ifndef CONFIG_PCI_HCI
		/* Clear CR[8] or beacon packet will not be send to TxBuf anymore. */
		v8 = rtw_read8(adapter, REG_CR_8821C + 1);
		v8 &= ~BIT(0); /* ~ENSWBCN */
		rtw_write8(adapter, REG_CR_8821C + 1, v8);
#endif /* !CONFIG_PCI_HCI */
	}

}

static void rtl8821c_set_h2c_fw_joinbssrpt(PADAPTER adapter, u8 mstatus)
{
	if (mstatus == RT_MEDIA_CONNECT)
		rtl8821c_dl_rsvd_page(adapter, RT_MEDIA_CONNECT);
}

#ifdef CONFIG_WOWLAN
static void hw_var_vendor_wow_mode(_adapter *adapter, u8 en)
{
#ifdef CONFIG_CONCURRENT_MODE
	_adapter *iface = NULL;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	u8 igi = 0, mac_addr[ETH_ALEN];

	RTW_INFO("%s: en(%d)--->\n", __func__, en);
	if (en) {
		rtw_hal_get_hwreg(adapter, HW_VAR_MAC_ADDR, mac_addr);
		/* RTW_INFO("suspend mac addr: "MAC_FMT"\n", MAC_ARG(mac_addr)); */
		rtw_halmac_set_bssid(dvobj, HW_PORT4, mac_addr);
		dvobj->rxfltmap2_bf_suspend = rtw_read16(adapter, REG_RXFLTMAP2);
		dvobj->bcn_ctrl_clint3_bf_suspend = rtw_read8(adapter, REG_BCN_CTRL_CLINT3);
		dvobj->rcr_bf_suspend = rtw_read32(adapter, REG_RCR);
		dvobj->cr_ext_bf_suspend = rtw_read32(adapter, REG_CR_EXT);
		/*RTW_INFO("RCR: 0x%02x, REG_CR_EXT: 0x%02x , REG_BCN_CTRL_CLINT3: 0x%02x, REG_RXFLTMAP2:0x%02x, REG_MACID_DROP0_8822B:0x%02x\n"
		, rtw_read32(adapter, REG_RCR), rtw_read8(adapter, REG_CR_EXT), rtw_read8(adapter, REG_BCN_CTRL_CLINT3)
		, rtw_read32(adapter, REG_RXFLTMAP2), rtw_read8(adapter, REG_MACID_DROP0_8822B)); */
		rtw_write32(adapter, REG_RCR, (rtw_read32(adapter, REG_RCR) & (~(RCR_AM))) | RCR_CBSSID_DATA | RCR_CBSSID_BCN);
		/* set PORT4 to ad hoc mode to filter not necessary Beacons */
		rtw_write8(adapter, REG_CR_EXT, (rtw_read8(adapter, REG_CR_EXT)& (~BIT5)) | BIT4);
		rtw_write8(adapter, REG_BCN_CTRL_CLINT3, rtw_read8(adapter, REG_BCN_CTRL_CLINT3) | BIT3);
		rtw_write16(adapter, REG_RXFLTMAP2, 0xffff);
		/* RTW_INFO("RCR: 0x%02x, REG_CR_EXT: 0x%02x , REG_BCN_CTRL_CLINT3: 0x%02x, REG_RXFLTMAP2:0x%02x, REG_MACID_DROP0_8822B:0x%02x\n"
		, rtw_read32(adapter, REG_RCR), rtw_read8(adapter, REG_CR_EXT), rtw_read8(adapter, REG_BCN_CTRL_CLINT3)
		, rtw_read32(adapter, REG_RXFLTMAP2), rtw_read8(adapter, REG_MACID_DROP0_8822B)); */
		
		/* The WRC's RSSI is weak. Set the IGI to lower */
		odm_write_dig(adapter_to_phydm(adapter), 0x24);
	} else {
		/* restore the rcr, port ctrol setting */
		rtw_write32(adapter, REG_CR_EXT, dvobj->cr_ext_bf_suspend);
		rtw_write32(adapter, REG_RCR, dvobj->rcr_bf_suspend);
		rtw_write8(adapter, REG_BCN_CTRL_CLINT3, dvobj->bcn_ctrl_clint3_bf_suspend);
		rtw_write16(adapter, REG_RXFLTMAP2, dvobj->rxfltmap2_bf_suspend);
		
		/* RTW_INFO("RCR: 0x%02x, REG_CR_EXT: 0x%02x , REG_BCN_CTRL_CLINT3: 0x%02x, REG_RXFLTMAP2:0x%02x, REG_MACID_DROP0_8822B:0x%02x\n"
		, rtw_read32(adapter, REG_RCR), rtw_read8(adapter, REG_CR_EXT), rtw_read8(adapter, REG_BCN_CTRL_CLINT3)
		, rtw_read32(adapter, REG_RXFLTMAP2), rtw_read8(adapter, REG_MACID_DROP0_8822B)); */
	}
#endif /* CONFIG_CONCURRENT_MODE */
}
#endif /* CONFIG_WOWLAN */

/*
 * Parameters:
 *	adapter
 *	enable		_TRUE: enable; _FALSE: disable
 */
static u8 rx_agg_switch(PADAPTER adapter, u8 enable)
{
	/* if (rtl8821c_config_rx_agg(adapter, enable)) */
	return _SUCCESS;

	return _FAIL;
}

u8 rtl8821c_sethwreg(PADAPTER adapter, u8 variable, u8 *val)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	u8 ret = _SUCCESS;
	u8 val8;
	u16 val16;
	u32 val32;


	switch (variable) {
	case HW_VAR_SET_OPMODE:
		hw_var_set_opmode(adapter, val);
		break;
	/*
		case HW_VAR_INIT_RTS_RATE:
			break;
	*/
	case HW_VAR_BASIC_RATE:
		rtw_var_set_basic_rate(adapter, val);
		break;

	case HW_VAR_TXPAUSE:
		rtw_write8(adapter, REG_TXPAUSE_8821C, *val);
		break;

	case HW_VAR_BCN_FUNC:
		hw_var_set_bcn_func(adapter, *val);
		break;

	case HW_VAR_PORT_CFG:
		hw_var_hw_port_cfg(adapter, *val);
		break;

	case HW_VAR_MLME_DISCONNECT:
		hw_var_set_mlme_disconnect(adapter);
		break;

	case HW_VAR_MLME_SITESURVEY:
		hw_var_set_mlme_sitesurvey(adapter, *val);
#ifdef CONFIG_BT_COEXIST
		if (hal->EEPROMBluetoothCoexist)
			rtw_btcoex_ScanNotify(adapter, *val ? _TRUE : _FALSE);
		else
#endif /* CONFIG_BT_COEXIST */
		rtw_btcoex_wifionly_scan_notify(adapter);
		break;

	case HW_VAR_MLME_JOIN:
		hw_var_set_mlme_join(adapter, *val);
		break;

	case HW_VAR_SLOT_TIME:
		rtw_write8(adapter, REG_SLOT_8821C, *val);
		break;

	case HW_VAR_RESP_SIFS:
		/* RESP_SIFS for CCK */
		rtw_write8(adapter, REG_RESP_SIFS_CCK_8821C, val[0]);
		rtw_write8(adapter, REG_RESP_SIFS_CCK_8821C + 1, val[1]);
		/* RESP_SIFS for OFDM */
		rtw_write8(adapter, REG_RESP_SIFS_OFDM_8821C, val[2]);
		rtw_write8(adapter, REG_RESP_SIFS_OFDM_8821C + 1, val[3]);
		break;

	case HW_VAR_ACK_PREAMBLE:
		hw_var_set_ack_preamble(adapter, *val);
		break;

	case HW_VAR_SEC_CFG:
		hw_var_set_sec_cfg(adapter, *val);
		break;

	case HW_VAR_SEC_DK_CFG:
		if (val)
			hw_var_set_sec_dk_cfg(adapter, _TRUE);
		else
			hw_var_set_sec_dk_cfg(adapter, _FALSE);
		break;

	case HW_VAR_BCN_VALID:
		hw_var_set_bcn_valid(adapter);
		break;

	case HW_VAR_CAM_INVALID_ALL:
		val32 = BIT_SECCAM_POLLING_8821C | BIT_SECCAM_CLR_8821C;
		rtw_write32(adapter, REG_CAMCMD_8821C, val32);
		break;

	case HW_VAR_AC_PARAM_VO:
		rtw_write32(adapter, REG_EDCA_VO_PARAM_8821C, *(u32 *)val);
		break;

	case HW_VAR_AC_PARAM_VI:
		rtw_write32(adapter, REG_EDCA_VI_PARAM_8821C, *(u32 *)val);
		break;

	case HW_VAR_AC_PARAM_BE:
		hal->ac_param_be = *(u32 *)val;
		rtw_write32(adapter, REG_EDCA_BE_PARAM_8821C, *(u32 *)val);
		break;

	case HW_VAR_AC_PARAM_BK:
		rtw_write32(adapter, REG_EDCA_BK_PARAM_8821C, *(u32 *)val);
		break;

	case HW_VAR_ACM_CTRL:
		hw_var_set_acm_ctrl(adapter, *val);
		break;
	/*
		case HW_VAR_AMPDU_MIN_SPACE:
			break;
	*/
#ifdef CONFIG_80211N_HT
	case HW_VAR_AMPDU_FACTOR: {
		u32 AMPDULen = *val; /* enum AGGRE_SIZE */

		AMPDULen = (0x2000 << AMPDULen) - 1;
		rtw_write32(adapter, REG_AMPDU_MAX_LENGTH_8821C, AMPDULen);
	}
	break;
#endif /* CONFIG_80211N_HT */
	case HW_VAR_RXDMA_AGG_PG_TH:
		/*
		 * TH=1 => invalidate RX DMA aggregation
		 * TH=0 => validate RX DMA aggregation, use init value.
		 */
		if (*val == 0)
			/* enable RXDMA aggregation */
			rx_agg_switch(adapter, _TRUE);
		else
			/* disable RXDMA aggregation */
			rx_agg_switch(adapter, _FALSE);
		break;

	case HW_VAR_H2C_FW_PWRMODE:
		rtl8821c_set_FwPwrMode_cmd(adapter, *val);
		break;

	case HW_VAR_H2C_FW_PWRMODE_RFON_CTRL:
		rtl8821c_set_FwPwrMode_rfon_ctrl_cmd(adapter, *val);
		break;

	case HW_VAR_LPS_RFON_CHK :
		hw_var_lps_rfon_chk(adapter, *val);
		break;
	/*
		case HW_VAR_H2C_PS_TUNE_PARAM:
			break;
	*/
	case HW_VAR_H2C_FW_JOINBSSRPT:
		rtl8821c_set_h2c_fw_joinbssrpt(adapter, *val);
		break;
	case HW_VAR_H2C_INACTIVE_IPS:
#ifdef CONFIG_WOWLAN
		rtl8821c_set_fw_pwrmode_inips_cmd_wowlan(adapter, *val);
#endif /* CONFIG_WOWLAN */
#ifdef CONFIG_WOWLAN
	case HW_VAR_VENDOR_WOW_MODE:
		hw_var_vendor_wow_mode(adapter, *(u8 *)val);
		break;
#endif /* CONFIG_WOWLAN */
	case HW_VAR_DL_RSVD_PAGE:
		#ifdef CONFIG_BT_COEXIST
		if (check_fwstate(&adapter->mlmepriv, WIFI_AP_STATE) == _TRUE)
			rtl8821c_download_BTCoex_AP_mode_rsvd_page(adapter);
		#endif
		break;
#ifdef CONFIG_P2P
	case HW_VAR_H2C_FW_P2P_PS_OFFLOAD:
		#ifdef CONFIG_FW_MULTI_PORT_SUPPORT
		if (*val == P2P_PS_ENABLE)
			rtw_set_default_port_id(adapter);
		#endif
		rtw_set_p2p_ps_offload_cmd(adapter, *val);
		break;
#endif

		/*
			case HW_VAR_TRIGGER_GPIO_0:
			case HW_VAR_BT_SET_COEXIST:
			case HW_VAR_BT_ISSUE_DELBA:
				break;
		*/

	/*
		case HW_VAR_SWITCH_EPHY_WoWLAN:
		case HW_VAR_EFUSE_USAGE:
		case HW_VAR_EFUSE_BYTES:
		case HW_VAR_EFUSE_BT_USAGE:
		case HW_VAR_EFUSE_BT_BYTES:
			break;
	*/
	case HW_VAR_FIFO_CLEARN_UP: {
		struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
		u8 trycnt = 100;
		u32 reg_hw_ssn;

		/* pause tx */
		rtw_write8(adapter, REG_TXPAUSE_8821C, 0xff);

		/* keep hw sn */
		if (adapter->xmitpriv.hw_ssn_seq_no == 1)
			reg_hw_ssn = REG_HW_SEQ1_8821C;
		else if (adapter->xmitpriv.hw_ssn_seq_no == 2)
			reg_hw_ssn = REG_HW_SEQ2_8821C;
		else if (adapter->xmitpriv.hw_ssn_seq_no == 3)
			reg_hw_ssn = REG_HW_SEQ3_8821C;
		else
			reg_hw_ssn = REG_HW_SEQ0_8821C;

		adapter->xmitpriv.nqos_ssn = rtw_read16(adapter, reg_hw_ssn);

		if (pwrpriv->bkeepfwalive != _TRUE) {
			/* RX DMA stop */
			val32 = rtw_read32(adapter, REG_RXPKT_NUM_8821C);
			val32 |= BIT_RW_RELEASE_EN;
			rtw_write32(adapter, REG_RXPKT_NUM_8821C, val32);
			do {
				val32 = rtw_read32(adapter, REG_RXPKT_NUM_8821C);
				val32 &= BIT_RXDMA_IDLE_8821C;
				if (val32)
					break;

				RTW_INFO("[HW_VAR_FIFO_CLEARN_UP] val=%x times:%d\n", val32, trycnt);
			} while (--trycnt);
			if (trycnt == 0)
				RTW_INFO("[HW_VAR_FIFO_CLEARN_UP] Stop RX DMA failed!\n");
#if 0
			/* RQPN Load 0 */
			rtw_write16(adapter, REG_RQPN_NPQ, 0);
			rtw_write32(adapter, REG_RQPN, 0x80000000);
			rtw_mdelay_os(2);
#endif
		}
	}
	break;

	case HW_VAR_RESTORE_HW_SEQ: {
		/* restore Sequence No. */
		u32 reg_hw_ssn;

		if (adapter->xmitpriv.hw_ssn_seq_no == 1)
			reg_hw_ssn = REG_HW_SEQ1_8821C;
		else if (adapter->xmitpriv.hw_ssn_seq_no == 2)
			reg_hw_ssn = REG_HW_SEQ2_8821C;
		else if (adapter->xmitpriv.hw_ssn_seq_no == 3)
			reg_hw_ssn = REG_HW_SEQ3_8821C;
		else
			reg_hw_ssn = REG_HW_SEQ0_8821C;

		rtw_write8(adapter, reg_hw_ssn, adapter->xmitpriv.nqos_ssn);
	}
	break;

	case HW_VAR_CHECK_TXBUF: {
		u16 rtylmtorg;
		u8 RetryLimit = 0x01;
		systime start;
		u32 passtime;
		u32 timelmt = 2000;	/* ms */
		u32 waittime = 10;	/* ms */
		u32 high, low, normal, extra, publc;
		u16 rsvd, available;
		u8 empty;


		rtylmtorg = rtw_read16(adapter, REG_RETRY_LIMIT_8821C);

		val16 = BIT_LRL_8821C(RetryLimit) | BIT_SRL_8821C(RetryLimit);
		rtw_write16(adapter, REG_RETRY_LIMIT_8821C, val16);

		/* Check TX FIFO empty or not */
		empty = _FALSE;
		high = 0;
		low = 0;
		normal = 0;
		extra = 0;
		publc = 0;
		start = rtw_get_current_time();
		while ((rtw_get_passing_time_ms(start) < timelmt)
		       && !RTW_CANNOT_RUN(adapter)) {
			high = rtw_read32(adapter, REG_FIFOPAGE_INFO_1_8821C);
			low = rtw_read32(adapter, REG_FIFOPAGE_INFO_2_8821C);
			normal = rtw_read32(adapter, REG_FIFOPAGE_INFO_3_8821C);
			extra = rtw_read32(adapter, REG_FIFOPAGE_INFO_4_8821C);
			publc = rtw_read32(adapter, REG_FIFOPAGE_INFO_5_8821C);

			rsvd = BIT_GET_HPQ_V1_8821C(high);
			available = BIT_GET_HPQ_AVAL_PG_V1_8821C(high);
			if (rsvd != available) {
				rtw_msleep_os(waittime);
				continue;
			}

			rsvd = BIT_GET_LPQ_V1_8821C(low);
			available = BIT_GET_LPQ_AVAL_PG_V1_8821C(low);
			if (rsvd != available) {
				rtw_msleep_os(waittime);
				continue;
			}

			rsvd = BIT_GET_NPQ_V1_8821C(normal);
			available = BIT_GET_NPQ_AVAL_PG_V1_8821C(normal);
			if (rsvd != available) {
				rtw_msleep_os(waittime);
				continue;
			}

			rsvd = BIT_GET_EXQ_V1_8821C(extra);
			available = BIT_GET_EXQ_AVAL_PG_V1_8821C(extra);
			if (rsvd != available) {
				rtw_msleep_os(waittime);
				continue;
			}

			rsvd = BIT_GET_PUBQ_V1_8821C(publc);
			available = BIT_GET_PUBQ_AVAL_PG_V1_8821C(publc);
			if (rsvd != available) {
				rtw_msleep_os(waittime);
				continue;
			}

			empty = _TRUE;
			break;
		}

		passtime = rtw_get_passing_time_ms(start);
		if (_TRUE == empty)
			RTW_INFO("[HW_VAR_CHECK_TXBUF] Empty in %d ms\n", passtime);
		else if (RTW_CANNOT_RUN(adapter))
			RTW_INFO("[HW_VAR_CHECK_TXBUF] bDriverStopped or bSurpriseRemoved\n");
		else {
			RTW_INFO("[HW_VAR_CHECK_TXBUF] NOT empty in %d ms\n", passtime);
			RTW_INFO("[HW_VAR_CHECK_TXBUF] 0x230=0x%08x 0x234=0x%08x 0x238=0x%08x 0x23c=0x%08x 0x240=0x%08x\n",
				 high, low, normal, extra, publc);

		}

		rtw_write16(adapter, REG_RETRY_LIMIT_8821C, rtylmtorg);
	}
	break;
	/*
		case HW_VAR_PCIE_STOP_TX_DMA:
			break;
	*/

	/*
		case HW_VAR_SYS_CLKR:
			break;
	*/
#ifdef CONFIG_GPIO_WAKEUP
	case HW_SET_GPIO_WL_CTRL: {
		struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
		u8 enable = *val;
		u8 value = 0;
		u8 addr = REG_PAD_CTRL1_8821C + 3;

		if (pwrpriv->wowlan_gpio_index == 6) {
			value = rtw_read8(adapter, addr);

			if (enable == _TRUE && (value & BIT(1)))
				/* set 0x64[25] = 0 to control GPIO 6 */
				rtw_write8(adapter, addr, value & (~BIT(1)));
			else if (enable == _FALSE)
				rtw_write8(adapter, addr, value | BIT(1));

			RTW_INFO("[HW_SET_GPIO_WL_CTRL] 0x%02X=0x%02X\n",
				 addr, rtw_read8(adapter, addr));
		}
	}
	break;
#endif
	case HW_VAR_NAV_UPPER: {
#define HAL_NAV_UPPER_UNIT	128	/* micro-second */
		u32 usNavUpper = *(u32 *)val;

		if (usNavUpper > HAL_NAV_UPPER_UNIT * 0xFF) {
			RTW_INFO(FUNC_ADPT_FMT ": [HW_VAR_NAV_UPPER] value(0x%08X us) is larger than (%d * 0xFF)!!!\n",
				FUNC_ADPT_ARG(adapter), usNavUpper, HAL_NAV_UPPER_UNIT);
			break;
		}

		usNavUpper = (usNavUpper + HAL_NAV_UPPER_UNIT - 1) / HAL_NAV_UPPER_UNIT;
		rtw_write8(adapter, REG_NAV_CTRL_8821C + 2, (u8)usNavUpper);
	}
	break;

	/*
		case HW_VAR_RPT_TIMER_SETTING:
		case HW_VAR_TX_RPT_MAX_MACID:
		case HW_VAR_CHK_HI_QUEUE_EMPTY:
			break;
	*/

	/*
		case HW_VAR_AMPDU_MAX_TIME:
		case HW_VAR_WIRELESS_MODE:
		case HW_VAR_USB_MODE:
		break;
	*/
#ifdef CONFIG_AP_PORT_SWAP
	case HW_VAR_PORT_SWITCH:
		{
			u8 mode = *((u8 *)val);

			rtl8821c_ap_port_switch(adapter, mode);
		}
		break;
#endif

	/*
		case HW_VAR_SOUNDING_RATE:
		case HW_VAR_SOUNDING_STATUS:
		case HW_VAR_SOUNDING_FW_NDPA:
		case HW_VAR_SOUNDING_CLK:
			break;
	*/
#ifdef CONFIG_BEAMFORMING	
	case HW_VAR_SOUNDING_ENTER:
		rtl8821c_phy_bf_enter(adapter, (struct sta_info*)val);
		break;	

	case HW_VAR_SOUNDING_LEAVE:
		rtl8821c_phy_bf_leave(adapter, val);
		break;

	case HW_VAR_SOUNDING_SET_GID_TABLE:	
		rtl8821c_phy_bf_set_gid_table(adapter, (struct beamformer_entry*)val);
		break;
#endif
	case HW_VAR_FREECNT:

		val8 = *((u8*)val);

		if (val8 == 0) {
			/* disable free run counter set 0x577[3]=0 */
			rtw_write8(adapter, REG_MISC_CTRL,
				rtw_read8(adapter, REG_MISC_CTRL)&(~BIT_EN_FREECNT));

			/* reset FREE_RUN_COUNTER set 0x553[5]=1 */
			val8 = rtw_read8(adapter, REG_DUAL_TSF_RST);
			val8 |=  BIT_FREECNT_RST;
			rtw_write8(adapter, REG_DUAL_TSF_RST, val8);

		} else if (val8 == 1){

			/* enable free run counter */

			/* disable first set 0x577[3]=0 */
			rtw_write8(adapter, REG_MISC_CTRL,
				rtw_read8(adapter, REG_MISC_CTRL)&(~BIT_EN_FREECNT));

			/* reset FREE_RUN_COUNTER set 0x553[5]=1 */
			val8 = rtw_read8(adapter, REG_DUAL_TSF_RST);
			val8 |=  BIT_FREECNT_RST;
			rtw_write8(adapter, REG_DUAL_TSF_RST, val8);

			/* enable free run counter 0x577[3]=1 */
			rtw_write8(adapter, REG_MISC_CTRL,
				rtw_read8(adapter, REG_MISC_CTRL)|BIT_EN_FREECNT);
		}
		break;

	default:
		ret = SetHwReg(adapter, variable, val);
		break;
	}

	return ret;
}

#ifdef CONFIG_PROC_DEBUG
struct qinfo {
	u32 head:11;
	u32 tail:11;
	u32 empty:1;
	u32 ac:2;
	u32 macid:7;
};

struct bcn_qinfo {
	u16 head:12;
	u16 rsvd:4;
};

static void dump_qinfo(void *sel, struct qinfo *info, u32 pkt_num, const char *tag)
{
	RTW_PRINT_SEL(sel, "%shead:0x%02x, tail:0x%02x, pkt_num:%u, macid:%u, ac:%u\n",
		tag ? tag : "", info->head, info->tail, pkt_num, info->macid, info->ac);
}

static void dump_bcn_qinfo(void *sel, struct bcn_qinfo *info, u32 pkt_num, const char *tag)
{
	RTW_PRINT_SEL(sel, "%shead:0x%02x, pkt_num:%u\n",
		      tag ? tag : "", info->head, pkt_num);
}

static void dump_mac_qinfo(void *sel, _adapter *adapter)
{
	u32 q0_info;
	u32 q1_info;
	u32 q2_info;
	u32 q3_info;
	u32 q4_info;
	u32 q5_info;
	u32 q6_info;
	u32 q7_info;
	u32 mg_q_info;
	u32 hi_q_info;
	u16 bcn_q_info;
	u32 q0_q1_info;
	u32 q2_q3_info;
	u32 q4_q5_info;
	u32 q6_q7_info;
	u32 mg_hi_q_info;
	u32 cmd_bcn_q_info;


	q0_info = rtw_read32(adapter, REG_Q0_INFO_8821C);
	q1_info = rtw_read32(adapter, REG_Q1_INFO_8821C);
	q2_info = rtw_read32(adapter, REG_Q2_INFO_8821C);
	q3_info = rtw_read32(adapter, REG_Q3_INFO_8821C);
	q4_info = rtw_read32(adapter, REG_Q4_INFO_8821C);
	q5_info = rtw_read32(adapter, REG_Q5_INFO_8821C);
	q6_info = rtw_read32(adapter, REG_Q6_INFO_8821C);
	q7_info = rtw_read32(adapter, REG_Q7_INFO_8821C);
	mg_q_info = rtw_read32(adapter, REG_MGQ_INFO_8821C);
	hi_q_info = rtw_read32(adapter, REG_HIQ_INFO_8821C);
	bcn_q_info = rtw_read16(adapter, REG_BCNQ_INFO_8821C);

	q0_q1_info = rtw_read32(adapter, REG_Q0_Q1_INFO_8821C);
	q2_q3_info = rtw_read32(adapter, REG_Q2_Q3_INFO_8821C);
	q4_q5_info = rtw_read32(adapter, REG_Q4_Q5_INFO_8821C);
	q6_q7_info = rtw_read32(adapter, REG_Q6_Q7_INFO_8821C);
	mg_hi_q_info = rtw_read32(adapter, REG_MGQ_HIQ_INFO_8821C);
	cmd_bcn_q_info = rtw_read32(adapter, REG_CMDQ_BCNQ_INFO_8821C);

	dump_qinfo(sel, (struct qinfo *)&q0_info, q0_q1_info&0xFFF, "Q0 ");
	dump_qinfo(sel, (struct qinfo *)&q1_info, (q0_q1_info>>15)&0xFFF, "Q1 ");
	dump_qinfo(sel, (struct qinfo *)&q2_info, q2_q3_info&0xFFF, "Q2 ");
	dump_qinfo(sel, (struct qinfo *)&q3_info, (q2_q3_info>>15)&0xFFF, "Q3 ");
	dump_qinfo(sel, (struct qinfo *)&q4_info, q4_q5_info&0xFFF, "Q4 ");
	dump_qinfo(sel, (struct qinfo *)&q5_info, (q4_q5_info>>15)&0xFFF, "Q5 ");
	dump_qinfo(sel, (struct qinfo *)&q6_info, q6_q7_info&0xFFF, "Q6 ");
	dump_qinfo(sel, (struct qinfo *)&q7_info, (q6_q7_info>>15)&0xFFF, "Q7 ");
	dump_qinfo(sel, (struct qinfo *)&mg_q_info, mg_hi_q_info&0xFFF, "MG ");
	dump_qinfo(sel, (struct qinfo *)&hi_q_info, (mg_hi_q_info>>15)&0xFFF, "HI ");
	dump_bcn_qinfo(sel, (struct bcn_qinfo *)&bcn_q_info, cmd_bcn_q_info&0xFFF, "BCN ");

}

static void dump_mac_txfifo(void *sel, _adapter *adapter)
{
	u32 hpq, lpq, npq, epq, pubq;

	hpq = rtw_read32(adapter, REG_FIFOPAGE_INFO_1_8821C);
	lpq = rtw_read32(adapter, REG_FIFOPAGE_INFO_2_8821C);
	npq = rtw_read32(adapter, REG_FIFOPAGE_INFO_3_8821C);
	epq = rtw_read32(adapter, REG_FIFOPAGE_INFO_4_8821C);
	pubq = rtw_read32(adapter, REG_FIFOPAGE_INFO_5_8821C);

	hpq = (hpq & 0xFFF0000)>>16;
	lpq = (lpq & 0xFFF0000)>>16;
	npq = (npq & 0xFFF0000)>>16;
	epq = (epq & 0xFFF0000)>>16;
	pubq = (pubq & 0xFFF0000)>>16;

	RTW_PRINT_SEL(sel, "Tx: available page num: ");
	if ((hpq == 0xAEA) && (hpq == lpq) && (hpq == pubq))
		RTW_PRINT_SEL(sel, "N/A (reg val = 0xea)\n");
	else
		RTW_PRINT_SEL(sel, "HPQ: %d, LPQ: %d, NPQ: %d, EPQ: %d, PUBQ: %d\n"
			, hpq, lpq, npq, epq, pubq);
}
#endif

static u8 hw_var_get_bcn_valid(PADAPTER adapter)
{
	u8 val8 = 0;
	u8 ret = _FALSE;

	/* only port 0 can TX BCN */
	val8 = rtw_read8(adapter, REG_FIFOPAGE_CTRL_2_8821C + 1);
	ret = (BIT(7) & val8) ? _TRUE : _FALSE;

	return ret;
}

void rtl8821c_read_wmmedca_reg(PADAPTER adapter, u16 *vo_params, u16 *vi_params, u16 *be_params, u16 *bk_params)
{
	u8 vo_reg_params[4];
	u8 vi_reg_params[4];
	u8 be_reg_params[4];
	u8 bk_reg_params[4];

	rtl8821c_gethwreg(adapter, HW_VAR_AC_PARAM_VO, vo_reg_params);
	rtl8821c_gethwreg(adapter, HW_VAR_AC_PARAM_VI, vi_reg_params);
	rtl8821c_gethwreg(adapter, HW_VAR_AC_PARAM_BE, be_reg_params);
	rtl8821c_gethwreg(adapter, HW_VAR_AC_PARAM_BK, bk_reg_params);

	vo_params[0] = vo_reg_params[0];
	vo_params[1] = vo_reg_params[1] & 0x0F;
	vo_params[2] = (vo_reg_params[1] & 0xF0) >> 4;
	vo_params[3] = ((vo_reg_params[3] << 8) | (vo_reg_params[2])) * 32;

	vi_params[0] = vi_reg_params[0];
	vi_params[1] = vi_reg_params[1] & 0x0F;
	vi_params[2] = (vi_reg_params[1] & 0xF0) >> 4;
	vi_params[3] = ((vi_reg_params[3] << 8) | (vi_reg_params[2])) * 32;

	be_params[0] = be_reg_params[0];
	be_params[1] = be_reg_params[1] & 0x0F;
	be_params[2] = (be_reg_params[1] & 0xF0) >> 4;
	be_params[3] = ((be_reg_params[3] << 8) | (be_reg_params[2])) * 32;

	bk_params[0] = bk_reg_params[0];
	bk_params[1] = bk_reg_params[1] & 0x0F;
	bk_params[2] = (bk_reg_params[1] & 0xF0) >> 4;
	bk_params[3] = ((bk_reg_params[3] << 8) | (bk_reg_params[2])) * 32;

	vo_params[1] = (1 << vo_params[1]) - 1;
	vo_params[2] = (1 << vo_params[2]) - 1;
	vi_params[1] = (1 << vi_params[1]) - 1;
	vi_params[2] = (1 << vi_params[2]) - 1;
	be_params[1] = (1 << be_params[1]) - 1;
	be_params[2] = (1 << be_params[2]) - 1;
	bk_params[1] = (1 << bk_params[1]) - 1;
	bk_params[2] = (1 << bk_params[2]) - 1;
}

void rtl8821c_gethwreg(PADAPTER adapter, u8 variable, u8 *val)
{
	PHAL_DATA_TYPE hal;
	u8 val8;
	u16 val16;
	u32 val32;


	hal = GET_HAL_DATA(adapter);

	switch (variable) {
	/*
		case HW_VAR_INIT_RTS_RATE:
		case HW_VAR_BASIC_RATE:
			break;
	*/
	case HW_VAR_TXPAUSE:
		*val = rtw_read8(adapter, REG_TXPAUSE_8821C);
		break;
	/*
		case HW_VAR_BCN_FUNC:
		case HW_VAR_MLME_DISCONNECT:
		case HW_VAR_MLME_SITESURVEY:
		case HW_VAR_MLME_JOIN:
		case HW_VAR_BEACON_INTERVAL:
		case HW_VAR_SLOT_TIME:
		case HW_VAR_RESP_SIFS:
		case HW_VAR_ACK_PREAMBLE:
		case HW_VAR_SEC_CFG:
		case HW_VAR_SEC_DK_CFG:
			break;
	*/
	case HW_VAR_BCN_VALID:
		*val = hw_var_get_bcn_valid(adapter);
		break;
	/*
		case HW_VAR_CAM_INVALID_ALL:
	*/
	case HW_VAR_AC_PARAM_VO:
		val32 = rtw_read32(adapter, REG_EDCA_VO_PARAM);
		val[0] = val32 & 0xFF;
		val[1] = (val32 >> 8) & 0xFF;
		val[2] = (val32 >> 16) & 0xFF;
		val[3] = (val32 >> 24) & 0x07;
		break;

	case HW_VAR_AC_PARAM_VI:
		val32 = rtw_read32(adapter, REG_EDCA_VI_PARAM);
		val[0] = val32 & 0xFF;
		val[1] = (val32 >> 8) & 0xFF;
		val[2] = (val32 >> 16) & 0xFF;
		val[3] = (val32 >> 24) & 0x07;
		break;

	case HW_VAR_AC_PARAM_BE:
		val32 = rtw_read32(adapter, REG_EDCA_BE_PARAM);
		val[0] = val32 & 0xFF;
		val[1] = (val32 >> 8) & 0xFF;
		val[2] = (val32 >> 16) & 0xFF;
		val[3] = (val32 >> 24) & 0x07;
		break;

	case HW_VAR_AC_PARAM_BK:
		val32 = rtw_read32(adapter, REG_EDCA_BK_PARAM);
		val[0] = val32 & 0xFF;
		val[1] = (val32 >> 8) & 0xFF;
		val[2] = (val32 >> 16) & 0xFF;
		val[3] = (val32 >> 24) & 0x07;
		break;
	/*
		case HW_VAR_ACM_CTRL:
		case HW_VAR_AMPDU_MIN_SPACE:
		case HW_VAR_AMPDU_FACTOR:
		case HW_VAR_RXDMA_AGG_PG_TH:
		case HW_VAR_H2C_FW_PWRMODE:
		case HW_VAR_H2C_PS_TUNE_PARAM:
		case HW_VAR_H2C_FW_JOINBSSRPT:
			break;
	*/
		/*
			case HW_VAR_H2C_FW_P2P_PS_OFFLOAD:
			case HW_VAR_TRIGGER_GPIO_0:
			case HW_VAR_BT_SET_COEXIST:
			case HW_VAR_BT_ISSUE_DELBA:
				break;
		*/

	/*
		case HW_VAR_ANTENNA_DIVERSITY_SELECT:
		case HW_VAR_SWITCH_EPHY_WoWLAN:
		case HW_VAR_EFUSE_USAGE:
		case HW_VAR_EFUSE_BYTES:
		case HW_VAR_EFUSE_BT_USAGE:
		case HW_VAR_EFUSE_BT_BYTES:
		case HW_VAR_FIFO_CLEARN_UP:
		case HW_VAR_RESTORE_HW_SEQ:
		case HW_VAR_CHECK_TXBUF:
		case HW_VAR_PCIE_STOP_TX_DMA:
			break;
	*/



#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
	case HW_VAR_SYS_CLKR:
		*val = rtw_read8(adapter, REG_SYS_CLK_CTRL_8821C);
		break;
#endif
	/*
		case HW_VAR_NAV_UPPER:
		case HW_VAR_RPT_TIMER_SETTING:
		case HW_VAR_TX_RPT_MAX_MACID:
			break;
	*/
	case HW_VAR_CHK_HI_QUEUE_EMPTY:
		val16 = rtw_read16(adapter, REG_TXPKT_EMPTY_8821C);
		*val = (val16 & BIT_HQQ_EMPTY_8821C) ? _TRUE : _FALSE;
		break;
	case HW_VAR_CHK_MGQ_CPU_EMPTY:
		val16 = rtw_read16(adapter, REG_TXPKT_EMPTY_8821C);
		*val = (val16 & BIT_MGQ_CPU_EMPTY_8821C) ? _TRUE : _FALSE;
		break;
	/*
		case HW_VAR_AMPDU_MAX_TIME:
		case HW_VAR_WIRELESS_MODE:
		case HW_VAR_USB_MODE:
		case HW_VAR_DO_IQK:
		case HW_VAR_SOUNDING_ENTER:
		case HW_VAR_SOUNDING_LEAVE:
		case HW_VAR_SOUNDING_RATE:
		case HW_VAR_SOUNDING_STATUS:
		case HW_VAR_SOUNDING_FW_NDPA:
		case HW_VAR_SOUNDING_CLK:
			break;
	*/
	case HW_VAR_FW_PS_STATE:
		/* driver read REG_SYS_CFG5 - BIT_LPS_STATUS REG_1070[3] to get hw ps state */
		*((u16 *)val) = rtw_read8(adapter, REG_SYS_CFG5);
		break;
#ifdef CONFIG_PROC_DEBUG
	case HW_VAR_DUMP_MAC_QUEUE_INFO:
		dump_mac_qinfo(val, adapter);
		break;

	case HW_VAR_DUMP_MAC_TXFIFO:
		dump_mac_txfifo(val, adapter);
		break;
#endif
	/*
		case HW_VAR_ASIX_IOT:
		case HW_VAR_H2C_BT_MP_OPER:
			break;
	*/

	case HW_VAR_BCN_CTRL_ADDR:
		*((u32 *)val) = hw_bcn_ctrl_addr(adapter, adapter->hw_port);
		break;

	case HW_VAR_FREECNT:
		*val = rtw_read8(adapter, REG_MISC_CTRL)&BIT_EN_FREECNT;
		break;

	default:
		GetHwReg(adapter, variable, val);
		break;
	}
}

/*
 * Description:
 *	Change default setting of specified variable.
 */
u8 rtl8821c_sethaldefvar(PADAPTER adapter, HAL_DEF_VARIABLE variable, void *pval)
{
	PHAL_DATA_TYPE hal;
	u8 bResult, val8;


	hal = GET_HAL_DATA(adapter);
	bResult = _SUCCESS;

	switch (variable) {
	/*
		case HAL_DEF_UNDERCORATEDSMOOTHEDPWDB:
		case HAL_DEF_IS_SUPPORT_ANT_DIV:
		case HAL_DEF_DRVINFO_SZ:
		case HAL_DEF_MAX_RECVBUF_SZ:
		case HAL_DEF_RX_PACKET_OFFSET:
		case HAL_DEF_RX_DMA_SZ_WOW:
		case HAL_DEF_RX_DMA_SZ:
		case HAL_DEF_RX_PAGE_SIZE:
		case HAL_DEF_DBG_DUMP_RXPKT:
		case HAL_DEF_RA_DECISION_RATE:
		case HAL_DEF_RA_SGI:
		case HAL_DEF_PT_PWR_STATUS:
		case HW_VAR_MAX_RX_AMPDU_FACTOR:
		case HAL_DEF_DBG_DUMP_TXPKT:
		case HAL_DEF_TX_PAGE_BOUNDARY:
		case HAL_DEF_TX_PAGE_BOUNDARY_WOWLAN:
		case HAL_DEF_ANT_DETECT:
		case HAL_DEF_PCI_SUUPORT_L1_BACKDOOR:
		case HAL_DEF_PCI_AMD_L1_SUPPORT:
		case HAL_DEF_PCI_ASPM_OSC:
		case HAL_DEF_EFUSE_USAGE:
		case HAL_DEF_EFUSE_BYTES:
		case HW_VAR_BEST_AMPDU_DENSITY:
			break;
	*/

	default:
		bResult = SetHalDefVar(adapter, variable, pval);
		break;
	}

	return bResult;
}

void rtl8821c_ra_info_dump(_adapter *padapter, void *sel)
{
	u8 mac_id;
	struct sta_info *psta;
	u32 rate_mask1, rate_mask2;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct macid_ctl_t *macid_ctl = dvobj_to_macidctl(dvobj);

	for (mac_id = 0; mac_id < macid_ctl->num; mac_id++) {
		if (rtw_macid_is_used(macid_ctl, mac_id) && !rtw_macid_is_bmc(macid_ctl, mac_id)) {
			psta = macid_ctl->sta[mac_id];
			if (!psta)
				continue;

			dump_sta_info(sel, psta);
			rate_mask1 = macid_ctl->rate_bmp0[mac_id];
			rate_mask2 = macid_ctl->rate_bmp1[mac_id];
			_RTW_PRINT_SEL(sel, "rate_mask2:0x%08x, rate_mask1:0x%08x\n", rate_mask2, rate_mask1);
		}
	}
}

/*
 * Description:
 *	Query setting of specified variable.
 */
u8 rtl8821c_gethaldefvar(PADAPTER adapter, HAL_DEF_VARIABLE variable, void *pval)
{
	PHAL_DATA_TYPE hal;
	u8 bResult;
	u8 val8 = 0;
	u32 val32 = 0;


	hal = GET_HAL_DATA(adapter);
	bResult = _SUCCESS;

	switch (variable) {
	/*
		case HAL_DEF_UNDERCORATEDSMOOTHEDPWDB:
			break;
	*/
	case HAL_DEF_IS_SUPPORT_ANT_DIV:
#ifdef CONFIG_ANTENNA_DIVERSITY
		*(u8 *)pval = _TRUE;
#else
		*(u8 *)pval = _FALSE;
#endif
		break;
	case HAL_DEF_MAX_RECVBUF_SZ:
		*((u32 *)pval) = MAX_RECVBUF_SZ;
		break;
	case HAL_DEF_RX_PACKET_OFFSET:
		rtw_halmac_get_rx_desc_size(adapter_to_dvobj(adapter), &val32);
		rtw_halmac_get_rx_drv_info_sz(adapter_to_dvobj(adapter), &val8);
		*((u32 *)pval) = val32 + val8;
		break;
	/*
	case HAL_DEF_DRVINFO_SZ:
		rtw_halmac_get_rx_drv_info_sz(adapter_to_dvobj(adapter), &val8);
		*((u32 *)pval) = val8;
		break;
		case HAL_DEF_RX_DMA_SZ_WOW:
		case HAL_DEF_RX_DMA_SZ:
		case HAL_DEF_RX_PAGE_SIZE:
		case HAL_DEF_DBG_DUMP_RXPKT:
		case HAL_DEF_RA_DECISION_RATE:
		case HAL_DEF_RA_SGI:
			break;
	*/
	/* only for 8188E */
	case HAL_DEF_PT_PWR_STATUS:
		break;

	case HAL_DEF_TX_LDPC:
		*(u8 *)pval = ((hal->phy_spec.ldpc_cap >> 8) & 0xFF) ? _TRUE : _FALSE;
		break;

	case HAL_DEF_RX_LDPC:
		*(u8 *)pval = (hal->phy_spec.ldpc_cap & 0xFF) ? _TRUE : _FALSE;
		break;

	case HAL_DEF_TX_STBC:
		*(u8 *)pval = (((hal->phy_spec.stbc_cap >> 8) & 0xFF) && hal->max_tx_cnt > 1) ? _TRUE : _FALSE;
		break;

	/* support 1RX for STBC */
	case HAL_DEF_RX_STBC:
		*(u8 *)pval = hal->phy_spec.stbc_cap & 0xFF;
		break;

	/* support Explicit TxBF for HT/VHT */
	case HAL_DEF_EXPLICIT_BEAMFORMER:
#ifdef CONFIG_BEAMFORMING
		*(u8 *)pval = ((hal->phy_spec.txbf_cap >> 20)& 0xF) ? _TRUE : _FALSE;
#else
		*(u8 *)pval = _FALSE;
#endif
		break;

	case HAL_DEF_EXPLICIT_BEAMFORMEE:
#ifdef CONFIG_BEAMFORMING
		*(u8 *)pval = ((hal->phy_spec.txbf_cap >> 16) & 0xF) ? _TRUE : _FALSE;
#else
		*(u8 *)pval = _FALSE;
#endif
		break;

	
	case HAL_DEF_VHT_MU_BEAMFORMER:
#ifdef CONFIG_BEAMFORMING
		*(u8 *)pval = ((hal->phy_spec.txbf_cap >> 28)& 0xF) ? _TRUE : _FALSE;
#else
		*(u8 *)pval = _FALSE;
#endif
		break;
            
	case HAL_DEF_VHT_MU_BEAMFORMEE:
#ifdef CONFIG_BEAMFORMING
		*(u8 *)pval = ((hal->phy_spec.txbf_cap >> 24)& 0xF) ? _TRUE : _FALSE;
#else
		*(u8 *)pval = _FALSE;
#endif
		break;

	case HAL_DEF_BEAMFORMER_CAP:
		*(u8 *)pval = (hal->phy_spec.txbf_param >> 24)& 0xFF ;
		break;

	case HAL_DEF_BEAMFORMEE_CAP:
		*(u8 *)pval = (hal->phy_spec.txbf_param >> 16) & 0xFF ;
		break;

	case HW_DEF_RA_INFO_DUMP:
		rtl8821c_ra_info_dump(adapter, pval);
		break;
	/*
		case HAL_DEF_DBG_DUMP_TXPKT:
		case HAL_DEF_TX_PAGE_SIZE:
			break;
	*/
	case HAL_DEF_TX_PAGE_BOUNDARY:
		rtw_halmac_get_rsvd_drv_pg_bndy(adapter_to_dvobj(adapter), (u16 *)pval);
		break;
	/*	case HAL_DEF_TX_PAGE_BOUNDARY_WOWLAN:
		case HAL_DEF_ANT_DETECT:
		case HAL_DEF_PCI_SUUPORT_L1_BACKDOOR:
		case HAL_DEF_PCI_AMD_L1_SUPPORT:
		case HAL_DEF_PCI_ASPM_OSC:
		case HAL_DEF_EFUSE_USAGE:
		case HAL_DEF_EFUSE_BYTES:
			break;
	*/
	case HW_VAR_BEST_AMPDU_DENSITY:
		*((u32 *)pval) = AMPDU_DENSITY_VALUE_4;
		break;

	default:
		bResult = GetHalDefVar(adapter, variable, pval);
		break;
	}

	return bResult;
}

/* xmit section */
void rtl8821c_init_xmit_priv(_adapter *adapter)
{
	struct xmit_priv *pxmitpriv = &adapter->xmitpriv;

	pxmitpriv->hw_ssn_seq_no = rtw_get_hwseq_no(adapter);
	pxmitpriv->nqos_ssn = 0;
}


void fill_txdesc_force_bmc_camid(struct pkt_attrib *pattrib, u8 *ptxdesc)
{
	if ((pattrib->encrypt > 0) && (!pattrib->bswenc)
	    && (pattrib->bmc_camid != INVALID_SEC_MAC_CAM_ID)) {

		SET_TX_DESC_EN_DESC_ID_8821C(ptxdesc, 1);
		SET_TX_DESC_MACID_8821C(ptxdesc, pattrib->bmc_camid);
	}
}

void fill_txdesc_bmc_tx_rate(struct pkt_attrib *pattrib, u8 *ptxdesc)
{
	SET_TX_DESC_USE_RATE_8821C(ptxdesc, 1);
	SET_TX_DESC_DATARATE_8821C(ptxdesc, MRateToHwRate(pattrib->rate));
	SET_TX_DESC_DISDATAFB_8821C(ptxdesc, 1);
}

void rtl8821c_fill_txdesc_sectype(struct pkt_attrib *pattrib, u8 *ptxdesc)
{
	if ((pattrib->encrypt > 0) && !pattrib->bswenc) {
		/* SEC_TYPE : 0:NO_ENC,1:WEP40/TKIP,2:WAPI,3:AES */
		switch (pattrib->encrypt) {
		case _WEP40_:
		case _WEP104_:
		case _TKIP_:
		case _TKIP_WTMIC_:
			SET_TX_DESC_SEC_TYPE_8821C(ptxdesc, 0x1);
			break;
#ifdef CONFIG_WAPI_SUPPORT
		case _SMS4_:
			SET_TX_DESC_SEC_TYPE_8821C(ptxdesc, 0x2);
			break;
#endif
		case _AES_:
			SET_TX_DESC_SEC_TYPE_8821C(ptxdesc, 0x3);
			break;
		case _CCMP_256_:
		case _GCMP_:
		case _GCMP_256_:
			SET_TX_DESC_SEC_TYPE_8821C(ptxdesc, 0x2);
			break;
		case _NO_PRIVACY_:
		default:
			SET_TX_DESC_SEC_TYPE_8821C(ptxdesc, 0x0);
			break;
		}
	}
}

void rtl8821c_fill_txdesc_vcs(PADAPTER adapter, struct pkt_attrib *pattrib, u8 *ptxdesc)
{
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);


	if (pattrib->vcs_mode) {
		switch (pattrib->vcs_mode) {
		case RTS_CTS:
			SET_TX_DESC_RTSEN_8821C(ptxdesc, 1);
			break;
		case CTS_TO_SELF:
			SET_TX_DESC_CTS2SELF_8821C(ptxdesc, 1);
			break;
		case NONE_VCS:
		default:
			break;
		}

		if (pmlmeinfo->preamble_mode == PREAMBLE_SHORT)
			SET_TX_DESC_RTS_SHORT_8821C(ptxdesc, 1);

		SET_TX_DESC_RTSRATE_8821C(ptxdesc, 0x8);/* RTS Rate=24M */
		SET_TX_DESC_RTS_RTY_LOWEST_RATE_8821C(ptxdesc, 0xf);
	}
}

u8 rtl8821c_bw_mapping(PADAPTER adapter, struct pkt_attrib *pattrib)
{
	u8 BWSettingOfDesc = 0;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	if (hal->current_channel_bw == CHANNEL_WIDTH_80) {
		if (pattrib->bwmode == CHANNEL_WIDTH_80)
			BWSettingOfDesc = 2;
		else if (pattrib->bwmode == CHANNEL_WIDTH_40)
			BWSettingOfDesc = 1;
		else
			BWSettingOfDesc = 0;
	} else if (hal->current_channel_bw == CHANNEL_WIDTH_40) {
		if ((pattrib->bwmode == CHANNEL_WIDTH_40) || (pattrib->bwmode == CHANNEL_WIDTH_80))
			BWSettingOfDesc = 1;
		else
			BWSettingOfDesc = 0;
	} else
		BWSettingOfDesc = 0;

	return BWSettingOfDesc;
}

u8 rtl8821c_sc_mapping(PADAPTER adapter, struct pkt_attrib *pattrib)
{
	u8 SCSettingOfDesc = 0;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	if (hal->current_channel_bw == CHANNEL_WIDTH_80) {
		if (pattrib->bwmode == CHANNEL_WIDTH_80)
			SCSettingOfDesc = VHT_DATA_SC_DONOT_CARE;
		else if (pattrib->bwmode == CHANNEL_WIDTH_40) {
			if (hal->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER)
				SCSettingOfDesc = VHT_DATA_SC_40_LOWER_OF_80MHZ;
			else if (hal->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER)
				SCSettingOfDesc = VHT_DATA_SC_40_UPPER_OF_80MHZ;
			else
				RTW_INFO("SCMapping: DONOT CARE Mode Setting\n");
		} else {
			if ((hal->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) && (hal->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER))
				SCSettingOfDesc = VHT_DATA_SC_20_LOWEST_OF_80MHZ;
			else if ((hal->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER) && (hal->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER))
				SCSettingOfDesc = VHT_DATA_SC_20_LOWER_OF_80MHZ;
			else if ((hal->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) && (hal->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER))
				SCSettingOfDesc = VHT_DATA_SC_20_UPPER_OF_80MHZ;
			else if ((hal->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER) && (hal->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER))
				SCSettingOfDesc = VHT_DATA_SC_20_UPPERST_OF_80MHZ;
			else
				RTW_INFO("SCMapping: DONOT CARE Mode Setting\n");
		}
	} else if (hal->current_channel_bw == CHANNEL_WIDTH_40) {
		if (pattrib->bwmode == CHANNEL_WIDTH_40)
			SCSettingOfDesc = VHT_DATA_SC_DONOT_CARE;
		else if (pattrib->bwmode == CHANNEL_WIDTH_20) {
			if (hal->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER)
				SCSettingOfDesc = VHT_DATA_SC_20_UPPER_OF_80MHZ;
			else if (hal->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER)
				SCSettingOfDesc = VHT_DATA_SC_20_LOWER_OF_80MHZ;
			else
				SCSettingOfDesc = VHT_DATA_SC_DONOT_CARE;
		}
	} else
		SCSettingOfDesc = VHT_DATA_SC_DONOT_CARE;

	return SCSettingOfDesc;
}

void rtl8821c_fill_txdesc_phy(PADAPTER adapter, struct pkt_attrib *pattrib, u8 *ptxdesc)
{
	if (pattrib->ht_en) {
		/* Set Bandwidth and sub-channel settings. */
		SET_TX_DESC_DATA_BW_8821C(ptxdesc, rtl8821c_bw_mapping(adapter, pattrib));
		SET_TX_DESC_DATA_SC_8821C(ptxdesc, rtl8821c_sc_mapping(adapter, pattrib));
	}
}

void rtl8821c_cal_txdesc_chksum(PADAPTER adapter, u8 *ptxdesc)
{
	struct halmac_adapter *halmac;
	struct halmac_api *api;


	halmac = adapter_to_halmac(adapter);
	api = HALMAC_GET_API(halmac);

	api->halmac_fill_txdesc_checksum(halmac, ptxdesc);
}


#ifdef CONFIG_MP_INCLUDED
void rtl8821c_prepare_mp_txdesc(PADAPTER adapter, struct mp_priv *pmp_priv)
{
	u32 desc_size = 0;
	u8 *desc;
	struct pkt_attrib *attrib;
	u32 pkt_size;
	s32 bmcast;
	u8 data_rate, pwr_status, offset;

	rtw_halmac_get_tx_desc_size(adapter_to_dvobj(adapter), &desc_size);

	desc = pmp_priv->tx.desc;
	attrib = &pmp_priv->tx.attrib;
	pkt_size = attrib->last_txcmdsz;
	bmcast = IS_MCAST(attrib->ra);

	SET_TX_DESC_LS_8821C(desc, 1);
	SET_TX_DESC_TXPKTSIZE_8821C(desc, pkt_size);

	offset = desc_size;
	SET_TX_DESC_OFFSET_8821C(desc, offset);

#if defined(CONFIG_PCI_HCI) || defined(CONFIG_SDIO_HCI)
	SET_TX_DESC_PKT_OFFSET_8821C(desc, 0); /* Don't need to set PACKET Offset bit,it's no use 512bytes of length */
#else
	SET_TX_DESC_PKT_OFFSET_8821C(desc, 1);
#endif

	if (bmcast)
		SET_TX_DESC_BMC_8821C(desc, 1);

	SET_TX_DESC_MACID_8821C(desc, attrib->mac_id);
	SET_TX_DESC_RATE_ID_8821C(desc, attrib->raid);
	SET_TX_DESC_QSEL_8821C(desc, attrib->qsel);

	if (pmp_priv->preamble)
		SET_TX_DESC_DATA_SHORT_8821C(desc, 1);

	if (!attrib->qos_en)
		SET_TX_DESC_EN_HWSEQ_8821C(desc, 1);
	else
		SET_TX_DESC_SW_SEQ_8821C(desc, attrib->seqnum);

	if (pmp_priv->bandwidth <= CHANNEL_WIDTH_160)
		SET_TX_DESC_DATA_BW_8821C(desc, pmp_priv->bandwidth);
	else {
		RTW_INFO("%s: <ERROR> unknown bandwidth %d, use 20M\n",
			 __FUNCTION__, pmp_priv->bandwidth);
		SET_TX_DESC_DATA_BW_8821C(desc, CHANNEL_WIDTH_20);
	}

	SET_TX_DESC_DISDATAFB_8821C(desc, 1);
	SET_TX_DESC_USE_RATE_8821C(desc, 1);
	SET_TX_DESC_DATARATE_8821C(desc, pmp_priv->rateidx);
}
#endif /* CONFIG_MP_INCLUDED */

#define OFFSET_SZ	0
static void fill_default_txdesc(struct xmit_frame *pxmitframe, u8 *pbuf)
{
	PADAPTER adapter = pxmitframe->padapter;
	HAL_DATA_TYPE *phal_data = GET_HAL_DATA(adapter);
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	s32 bmcst = IS_MCAST(pattrib->ra);
	u32 desc_size = 0;
	u8 offset, pkt_offset = 0;
#define RA_SW_DEFINE_CONT	0x01
	u8	drv_fixed_reate = _FALSE;
	u8 hw_port = rtw_hal_get_port(adapter);

#if 0
#ifndef CONFIG_USE_USB_BUFFER_ALLOC_TX
	if (adapter->registrypriv.mp_mode == 0) {
		if ((PACKET_OFFSET_SZ != 0) && (!bagg_pkt) &&
		    (rtw_usb_bulk_size_boundary(adapter, TXDESC_SIZE + pattrib->last_txcmdsz) == _FALSE)) {
			ptxdesc = (pmem + PACKET_OFFSET_SZ);
			/* RTW_INFO("==> non-agg-pkt,shift pointer...\n"); */
			pull = 1;
		}
	}
#endif	/* CONFIG_USE_USB_BUFFER_ALLOC_TX*/
#endif
	rtw_halmac_get_tx_desc_size(adapter_to_dvobj(adapter), &desc_size);

	_rtw_memset(pbuf, 0, desc_size);

	/*SET_TX_DESC_LS_8821C(pbuf, 1);*/ /*for USB only*/

	SET_TX_DESC_TXPKTSIZE_8821C(pbuf, pattrib->last_txcmdsz);

	offset = desc_size + OFFSET_SZ;

#ifdef CONFIG_TX_EARLY_MODE
	if (pxmitframe->frame_tag == DATA_FRAMETAG)
		if (bagg_pkt)
			offset += EARLY_MODE_INFO_SIZE ;/*0x28	*/
	pkt_offset = pxmitframe->pkt_offset = 0x01;
#endif
	SET_TX_DESC_OFFSET_8821C(pbuf, offset);

	if (bmcst)
		SET_TX_DESC_BMC_8821C(pbuf, 1);

#if 0
#ifndef CONFIG_USE_USB_BUFFER_ALLOC_TX
	if (adapter->registrypriv.mp_mode == 0) {
		if ((PACKET_OFFSET_SZ != 0) && (!bagg_pkt)) {
			if ((pull) && (pxmitframe->pkt_offset > 0)) {
				pxmitframe->pkt_offset = pxmitframe->pkt_offset - 1;
				pkt_offset = pxmitframe->pkt_offset;
			}
		}
	}
	/*RTW_INFO("%s, pkt_offset=0x%02x\n", __func__, pxmitframe->pkt_offset);*/
#endif
#endif

	/* pkt_offset, unit:8 bytes padding*/
	if (pkt_offset > 0)
		SET_TX_DESC_PKT_OFFSET_8821C(pbuf, pkt_offset);


	SET_TX_DESC_MACID_8821C(pbuf, pattrib->mac_id);
	SET_TX_DESC_RATE_ID_8821C(pbuf, pattrib->raid);
	SET_TX_DESC_QSEL_8821C(pbuf, pattrib->qsel);

	/* 2009.11.05. tynli_test. Suggested by SD4 Filen for FW LPS.
	* (1) The sequence number of each non-Qos frame / broadcast / multicast /
	* mgnt frame should be controled by Hw because Fw will also send null data
	* which we cannot control when Fw LPS enable.
	* --> default enable non-Qos data sequense number. 2010.06.23. by tynli.
	* (2) Enable HW SEQ control for beacon packet, because we use Hw beacon.
	* (3) Use HW Qos SEQ to control the seq num of Ext port non-Qos packets.
	*/

	/* HW sequence, to fix to use 0 queue. todo: 4AC packets to use auto queue select */
	if (!pattrib->qos_en) {
		SET_TX_DESC_DISQSELSEQ_8821C(pbuf, 1);
		SET_TX_DESC_EN_HWSEQ_8821C(pbuf, 1);
		SET_TX_DESC_HW_SSN_SEL_8821C(pbuf, pattrib->hw_ssn_sel);
		SET_TX_DESC_EN_HWEXSEQ_8821C(pbuf, 0);
	} else
		SET_TX_DESC_SW_SEQ_8821C(pbuf, pattrib->seqnum);

	if (pxmitframe->frame_tag == DATA_FRAMETAG) {

		rtl8821c_fill_txdesc_sectype(pattrib, pbuf);

		if (bmcst)
			fill_txdesc_force_bmc_camid(pattrib, pbuf);

#if defined(CONFIG_USB_TX_AGGREGATION) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
		if (pxmitframe->agg_num > 1) {
			/*RTW_INFO("%s agg_num:%d\n",__func__,pxmitframe->agg_num );*/
			SET_TX_DESC_DMA_TXAGG_NUM_8821C(pbuf, pxmitframe->agg_num);
		}
#endif /*CONFIG_USB_TX_AGGREGATION*/

		rtl8821c_fill_txdesc_vcs(adapter, pattrib, pbuf);

#ifdef CONFIG_SUPPORT_DYNAMIC_TXPWR
		rtw_phydm_set_dyntxpwr(adapter, pbuf, pattrib->mac_id);
#endif
		if ((pattrib->ether_type != 0x888e) &&
		    (pattrib->ether_type != 0x0806) &&
		    (pattrib->ether_type != 0x88B4) &&
		    (pattrib->dhcp_pkt != 1)
#ifdef CONFIG_AUTO_AP_MODE
		    && (pattrib->pctrl != _TRUE)
#endif
		   ) {
			/* Non EAP & ARP & DHCP type data packet */

			if (pattrib->ampdu_en == _TRUE) {
				SET_TX_DESC_AGG_EN_8821C(pbuf, 1);
				SET_TX_DESC_MAX_AGG_NUM_8821C(pbuf, 0x1F);
				SET_TX_DESC_AMPDU_DENSITY_8821C(pbuf, pattrib->ampdu_spacing);
			} else
				SET_TX_DESC_BK_8821C(pbuf, 1);

			if (adapter->fix_bw != 0xFF)
				pattrib->bwmode =  adapter->fix_bw;

			rtl8821c_fill_txdesc_phy(adapter, pattrib, pbuf);

			if (phal_data->current_band_type == BAND_ON_5G)/*Data Rate Fallback Limit rate*/
				SET_TX_DESC_DATA_RTY_LOWEST_RATE_8821C(pbuf, 4);
			else
				SET_TX_DESC_DATA_RTY_LOWEST_RATE_8821C(pbuf, 0);

			if (bmcst) {
				drv_fixed_reate = _TRUE;
				fill_txdesc_bmc_tx_rate(pattrib, pbuf);
			}

			/* modify data rate by iwpriv*/
			if (adapter->fix_rate != 0xFF) {
				drv_fixed_reate = _TRUE;
				SET_TX_DESC_USE_RATE_8821C(pbuf, 1);

				if (adapter->fix_rate & BIT(7))
					SET_TX_DESC_DATA_SHORT_8821C(pbuf, 1);
				SET_TX_DESC_DATARATE_8821C(pbuf, adapter->fix_rate & 0x7F);
				if (!adapter->data_fb)
					SET_TX_DESC_DISDATAFB_8821C(pbuf, 1);
			}

			if (pattrib->ldpc)
				SET_TX_DESC_DATA_LDPC_8821C(pbuf, 1);
			if (pattrib->stbc)
				SET_TX_DESC_DATA_STBC_8821C(pbuf, 1);

#ifdef CONFIG_CMCC_TEST
			SET_TX_DESC_DATA_SHORT_8821C(pbuf, 1); /* use cck short premble */
#endif

#ifdef CONFIG_WMMPS_STA
			if (pattrib->trigger_frame)
				SET_TX_DESC_TRI_FRAME_8821C (pbuf, 1);
#endif /* CONFIG_WMMPS_STA */
		} else {

			/*
			 * EAP data packet and ARP packet.
			 * Use the 1M data rate to send the EAP/ARP packet.
			 * This will maybe make the handshake smooth.
			 */
			SET_TX_DESC_BK_8821C(pbuf, 1);
			drv_fixed_reate = _TRUE;
			SET_TX_DESC_USE_RATE_8821C(pbuf, 1);

			/* HW will ignore this setting if the transmission rate is legacy OFDM.*/
			if (pmlmeinfo->preamble_mode == PREAMBLE_SHORT)
				SET_TX_DESC_DATA_SHORT_8821C(pbuf, 1);
#ifdef CONFIG_IP_R_MONITOR
			if((pattrib->ether_type == ETH_P_ARP) &&
				(IsSupportedTxOFDM(adapter->registrypriv.wireless_mode))) 
				SET_TX_DESC_DATARATE_8821C(pbuf, MRateToHwRate(IEEE80211_OFDM_RATE_6MB));
			 else
#endif/*CONFIG_IP_R_MONITOR*/
				SET_TX_DESC_DATARATE_8821C(pbuf, MRateToHwRate(pmlmeext->tx_rate));

			RTW_INFO(FUNC_ADPT_FMT ": SP Packet(0x%04X) rate=0x%x SeqNum = %d\n",
				FUNC_ADPT_ARG(adapter), pattrib->ether_type, MRateToHwRate(pmlmeext->tx_rate), pattrib->seqnum);
		}
#ifdef CONFIG_TDLS
#ifdef CONFIG_XMIT_ACK
		/* CCX-TXRPT ack for xmit mgmt frames. */
		if (pxmitframe->ack_report) {
#ifdef DBG_CCX
			RTW_INFO("%s set spe_rpt\n", __func__);
#endif
			SET_TX_DESC_SPE_RPT_8821C(pbuf, 1);
		}
#endif /* CONFIG_XMIT_ACK */
#endif

	} else if (pxmitframe->frame_tag == MGNT_FRAMETAG) {
		SET_TX_DESC_MBSSID_8821C(pbuf, pattrib->mbssid & 0xF);
		SET_TX_DESC_USE_RATE_8821C(pbuf, 1);
		drv_fixed_reate = _TRUE;

		SET_TX_DESC_DATARATE_8821C(pbuf, MRateToHwRate(pattrib->rate));

		/* VHT NDPA or HT NDPA Packet for Beamformer.*/
#ifdef CONFIG_BEAMFORMING
		if ((pattrib->subtype == WIFI_NDPA) ||
		    ((pattrib->subtype == WIFI_ACTION_NOACK) && (pattrib->order == 1))) {
			SET_TX_DESC_NAVUSEHDR_8821C(pbuf, 1);

			SET_TX_DESC_DATA_BW_8821C(pbuf, rtl8821c_bw_mapping(adapter, pattrib));
			/*SET_TX_DESC_DATA_SC_8821C(pbuf, rtl8821c_sc_mapping(adapter, pattrib));*/
			SET_TX_DESC_SIGNALING_TA_PKT_SC_8821C(pbuf, rtl8821c_sc_mapping(adapter,pattrib));

			SET_TX_DESC_RTY_LMT_EN_8821C(pbuf, 1);
			SET_TX_DESC_RTS_DATA_RTY_LMT_8821C(pbuf, 5);
			SET_TX_DESC_DISDATAFB_8821C(pbuf, 1);

			/*if(pattrib->rts_cca)
				SET_TX_DESC_NDPA_8821C(ptxdesc, 2);
			else*/
			SET_TX_DESC_NDPA_8821C(pbuf, 1);
		} else
#endif
		{
			SET_TX_DESC_RTY_LMT_EN_8821C(pbuf, 1);
			if (pattrib->retry_ctrl == _TRUE)
				SET_TX_DESC_RTS_DATA_RTY_LMT_8821C(pbuf, 6);
			else
				SET_TX_DESC_RTS_DATA_RTY_LMT_8821C(pbuf, 12);
		}

#ifdef CONFIG_XMIT_ACK
		/* CCX-TXRPT ack for xmit mgmt frames. */
		if (pxmitframe->ack_report) {
#ifdef DBG_CCX
			RTW_INFO("%s set spe_rpt\n", __func__);
#endif
			SET_TX_DESC_SPE_RPT_8821C(pbuf, 1);
		}
#endif /* CONFIG_XMIT_ACK */
	} else if (pxmitframe->frame_tag == TXAGG_FRAMETAG)
		RTW_INFO("%s: TXAGG_FRAMETAG\n", __func__);
#ifdef CONFIG_MP_INCLUDED
	else if (pxmitframe->frame_tag == MP_FRAMETAG) {
		RTW_INFO("%s: MP_FRAMETAG\n", __func__);
		fill_txdesc_for_mp(adapter, pbuf);
	}
#endif
	else {
		RTW_INFO("%s: frame_tag=0x%x\n", __func__, pxmitframe->frame_tag);

		SET_TX_DESC_USE_RATE_8821C(pbuf, 1);
		drv_fixed_reate = _TRUE;
		SET_TX_DESC_DATARATE_8821C(pbuf, MRateToHwRate(pmlmeext->tx_rate));
	}

	if (drv_fixed_reate == _TRUE)
		SET_TX_DESC_SW_DEFINE_8821C(pbuf, RA_SW_DEFINE_CONT);

	if (adapter->power_offset != 0)
		SET_TX_DESC_TXPWR_OFSET_8821C(pbuf, adapter->power_offset);

#ifdef CONFIG_ANTENNA_DIVERSITY
	if (!bmcst && pattrib->psta)
		odm_set_tx_ant_by_tx_info(adapter_to_phydm(adapter), pbuf, pattrib->psta->cmn.mac_id);
#endif

#ifdef CONFIG_BEAMFORMING
	SET_TX_DESC_G_ID_8821C(pbuf, pattrib->txbf_g_id);
	SET_TX_DESC_P_AID_8821C(pbuf, pattrib->txbf_p_aid);
#endif

	SET_TX_DESC_PORT_ID_8821C(pbuf, hw_port);
	SET_TX_DESC_MULTIPLE_PORT_8821C(pbuf, hw_port);
}

void rtl8821c_dbg_dump_tx_desc(PADAPTER adapter, int frame_tag, u8 *ptxdesc)
{
	u8 bDumpTxPkt;
	u8 bDumpTxDesc = _FALSE;


	rtw_hal_get_def_var(adapter, HAL_DEF_DBG_DUMP_TXPKT, &bDumpTxPkt);

	/* 1 for data frame, 2 for mgnt frame */
	if (bDumpTxPkt == 1) {
		RTW_INFO("dump tx_desc for data frame\n");
		if ((frame_tag & 0x0f) == DATA_FRAMETAG)
			bDumpTxDesc = _TRUE;
	} else if (bDumpTxPkt == 2) {
		RTW_INFO("dump tx_desc for mgnt frame\n");
		if ((frame_tag & 0x0f) == MGNT_FRAMETAG)
			bDumpTxDesc = _TRUE;
	}

	/* 8821C TX SIZE = 48(HALMAC_TX_DESC_SIZE_8821C) */
	if (_TRUE == bDumpTxDesc) {
		RTW_INFO("=====================================\n");
		RTW_INFO("Offset00(0x%08x)\n", *((u32 *)(ptxdesc)));
		RTW_INFO("Offset04(0x%08x)\n", *((u32 *)(ptxdesc + 4)));
		RTW_INFO("Offset08(0x%08x)\n", *((u32 *)(ptxdesc + 8)));
		RTW_INFO("Offset12(0x%08x)\n", *((u32 *)(ptxdesc + 12)));
		RTW_INFO("Offset16(0x%08x)\n", *((u32 *)(ptxdesc + 16)));
		RTW_INFO("Offset20(0x%08x)\n", *((u32 *)(ptxdesc + 20)));
		RTW_INFO("Offset24(0x%08x)\n", *((u32 *)(ptxdesc + 24)));
		RTW_INFO("Offset28(0x%08x)\n", *((u32 *)(ptxdesc + 28)));
		RTW_INFO("Offset32(0x%08x)\n", *((u32 *)(ptxdesc + 32)));
		RTW_INFO("Offset36(0x%08x)\n", *((u32 *)(ptxdesc + 36)));
		RTW_INFO("Offset40(0x%08x)\n", *((u32 *)(ptxdesc + 40)));
		RTW_INFO("Offset44(0x%08x)\n", *((u32 *)(ptxdesc + 44)));
		RTW_INFO("=====================================\n");
	}
}

/*
 * Description:
 *
 * Parameters:
 *	pxmitframe	xmitframe
 *	pbuf		where to fill tx desc
 */
void rtl8821c_update_txdesc(struct xmit_frame *pxmitframe, u8 *pbuf)
{
	fill_default_txdesc(pxmitframe, pbuf);
	rtl8821c_cal_txdesc_chksum(pxmitframe->padapter, pbuf);
	rtl8821c_dbg_dump_tx_desc(pxmitframe->padapter, pxmitframe->frame_tag, pbuf);
}

/*
 * Description:
 *	In normal chip, we should send some packet to HW which will be used by FW
 *	in FW LPS mode.
 *	The function is to fill the Tx descriptor of this packets,
 *	then FW can tell HW to send these packet directly.
 */
static void fill_fake_txdesc(PADAPTER adapter, u8 *pDesc, u32 BufferLen,
			     u8 IsPsPoll, u8 IsBTQosNull, u8 bDataFrame)
{
	struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
	struct xmit_priv		*pxmitpriv = &adapter->xmitpriv;
	u32 desc_size = 0;
	u8 hw_port = rtw_hal_get_port(adapter);

	rtw_halmac_get_tx_desc_size(adapter_to_dvobj(adapter), &desc_size);

	/* Clear all status */
	_rtw_memset(pDesc, 0, desc_size);

	SET_TX_DESC_LS_8821C(pDesc, 1);

	SET_TX_DESC_OFFSET_8821C(pDesc, desc_size);

	SET_TX_DESC_TXPKTSIZE_8821C(pDesc, BufferLen);
	SET_TX_DESC_QSEL_8821C(pDesc, QSLT_MGNT); /* Fixed queue of Mgnt queue */

	if (pmlmeext->cur_wireless_mode & WIRELESS_11B)
		SET_TX_DESC_RATE_ID_8821C(pDesc, RATEID_IDX_B);
	else
		SET_TX_DESC_RATE_ID_8821C(pDesc, RATEID_IDX_G);

	/* Set NAVUSEHDR to prevent Ps-poll AId filed to be changed to error vlaue by HW */
	if (_TRUE == IsPsPoll)
		SET_TX_DESC_NAVUSEHDR_8821C(pDesc, 1);
	else {
		SET_TX_DESC_DISQSELSEQ_8821C(pDesc, 1);
		SET_TX_DESC_EN_HWSEQ_8821C(pDesc, 1);
		SET_TX_DESC_HW_SSN_SEL_8821C(pDesc, pxmitpriv->hw_ssn_seq_no);/*pattrib->hw_ssn_sel*/
		SET_TX_DESC_EN_HWEXSEQ_8821C(pDesc, 0);
	}

	if (_TRUE == IsBTQosNull)
		SET_TX_DESC_BT_NULL_8821C(pDesc, 1);

	SET_TX_DESC_USE_RATE_8821C(pDesc, 1);
	SET_TX_DESC_DATARATE_8821C(pDesc, MRateToHwRate(pmlmeext->tx_rate));

#ifdef CONFIG_MCC_MODE
	/* config Null data retry number */
	if (IsPsPoll == _FALSE && IsBTQosNull == _FALSE && bDataFrame == _FALSE) {
		if (rtw_hal_check_mcc_status(adapter, MCC_STATUS_PROCESS_MCC_START_SETTING)) {
			u8 rty_num = adapter->mcc_adapterpriv.null_rty_num;
			if (rty_num != 0) {
				SET_TX_DESC_RTY_LMT_EN_8821C(pDesc, 1);
				SET_TX_DESC_RTS_DATA_RTY_LMT_8821C(pDesc, rty_num);
			}
		}
	}
#endif

	/*
	 * Encrypt the data frame if under security mode excepct null data.
	 */
	if (_TRUE == bDataFrame) {
		u32 EncAlg;

		EncAlg = adapter->securitypriv.dot11PrivacyAlgrthm;
		switch (EncAlg) {
		case _NO_PRIVACY_:
			SET_TX_DESC_SEC_TYPE_8821C(pDesc, 0x0);
			break;
		case _WEP40_:
		case _WEP104_:
		case _TKIP_:
			SET_TX_DESC_SEC_TYPE_8821C(pDesc, 0x1);
			break;
		case _SMS4_:
			SET_TX_DESC_SEC_TYPE_8821C(pDesc, 0x2);
			break;
		case _AES_:
			SET_TX_DESC_SEC_TYPE_8821C(pDesc, 0x3);
			break;
		case _CCMP_256_:
		case _GCMP_:
		case _GCMP_256_:
			SET_TX_DESC_SEC_TYPE_8821C(pDesc, 0x2);
			break;
		default:
			SET_TX_DESC_SEC_TYPE_8821C(pDesc, 0x0);
			break;
		}
	}
	SET_TX_DESC_PORT_ID_8821C(pDesc, hw_port);
	SET_TX_DESC_MULTIPLE_PORT_8821C(pDesc, hw_port);

#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	/*
	 * USB interface drop packet if the checksum of descriptor isn't correct.
	 * Using this checksum can let hardware recovery from packet bulk out error (e.g. Cancel URC, Bulk out error.).
	 */
	rtl8821c_cal_txdesc_chksum(adapter, pDesc);
#endif
}

void rtl8821c_rxdesc2attribute(struct rx_pkt_attrib *pattrib, u8 *desc)
{
	/* initial value */
	_rtw_memset(pattrib, 0, sizeof(struct rx_pkt_attrib));

	pattrib->pkt_len = (u16)GET_RX_DESC_PKT_LEN_8821C(desc);
	pattrib->pkt_rpt_type = (GET_RX_DESC_C2H_8821C(desc)) ? C2H_PACKET : NORMAL_RX;

	if (pattrib->pkt_rpt_type == NORMAL_RX) {
		/* Get from RX DESC */
		pattrib->crc_err = (u8)GET_RX_DESC_CRC32_8821C(desc);
		pattrib->icv_err = (u8)GET_RX_DESC_ICV_ERR_8821C(desc);

		pattrib->drvinfo_sz = (u8)GET_RX_DESC_DRV_INFO_SIZE_8821C(desc) << 3;
		pattrib->encrypt = (u8)GET_RX_DESC_SECURITY_8821C(desc);
		pattrib->qos = (u8)GET_RX_DESC_QOS_8821C(desc);
		pattrib->shift_sz = (u8)GET_RX_DESC_SHIFT_8821C(desc);
		pattrib->physt = (u8)GET_RX_DESC_PHYST_8821C(desc);
		pattrib->bdecrypted = (u8)GET_RX_DESC_SWDEC_8821C(desc) ? 0 : 1;

		pattrib->priority = (u8)GET_RX_DESC_TID_8821C(desc);
		pattrib->amsdu = (u8)GET_RX_DESC_AMSDU_8821C(desc);
		pattrib->mdata = (u8)GET_RX_DESC_MD_8821C(desc);
		pattrib->mfrag = (u8)GET_RX_DESC_MF_8821C(desc);

		pattrib->seq_num = (u16)GET_RX_DESC_SEQ_8821C(desc);
		pattrib->frag_num = (u8)GET_RX_DESC_FRAG_8821C(desc);
		pattrib->ampdu = (u8)GET_RX_DESC_PAGGR_8821C(desc);
		pattrib->ampdu_eof = (u8)GET_RX_DESC_RX_EOF_8821C(desc);
		pattrib->ppdu_cnt = (u8)GET_RX_DESC_PPDU_CNT_8821C(desc);
		pattrib->free_cnt = (u32)GET_RX_DESC_TSFL_8821C(desc);

		pattrib->bw = CHANNEL_WIDTH_MAX;
		pattrib->data_rate = (u8)GET_RX_DESC_RX_RATE_8821C(desc);
	}
}

void rtl8821c_query_rx_desc(union recv_frame *precvframe, u8 *pdesc)
{
	rtl8821c_rxdesc2attribute(&precvframe->u.hdr.attrib, pdesc);
}

static void InitBeaconParameters(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);

	/* TBTT setup time */
	rtw_write8(adapter, REG_TBTT_PROHIBIT_8821C, TBTT_PROHIBIT_SETUP_TIME);

	/* TBTT hold time: 0x540[19:8] */
	rtw_write8(adapter, REG_TBTT_PROHIBIT_8821C + 1, TBTT_PROHIBIT_HOLD_TIME_STOP_BCN & 0xFF);
	rtw_write8(adapter, REG_TBTT_PROHIBIT_8821C + 2,
		(rtw_read8(adapter, REG_TBTT_PROHIBIT_8821C + 2) & 0xF0) | (TBTT_PROHIBIT_HOLD_TIME_STOP_BCN >> 8));

	rtw_write8(adapter, REG_DRVERLYINT_8821C, DRIVER_EARLY_INT_TIME_8821C); /* 5ms */
	rtw_write8(adapter, REG_BCNDMATIM_8821C, BCN_DMA_ATIME_INT_TIME_8821C); /* 2ms */

	/*
	 * Suggested by designer timchen. Change beacon AIFS to the largest number
	 * beacause test chip does not contension before sending beacon.
	 */
	rtw_write16(adapter, REG_BCNTCFG_8821C, 0x4413);
}

static void beacon_function_enable(PADAPTER adapter, u8 Enable, u8 Linked)
{
	hw_bcn_ctrl_add(adapter, get_hw_port(adapter), BIT_DIS_TSF_UDT_8821C | BIT_EN_BCN_FUNCTION_8821C);
}

static void set_beacon_related_registers(PADAPTER adapter)
{
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	/* reset TSF, enable update TSF, correcting TSF On Beacon */
#if 0
	/* * REG_MBSSID_BCN_SPACE */
	/* * REG_BCNDMATIM */
	/* * REG_ATIMWND */
	/* * REG_TBTT_PROHIBIT */
	/* * REG_DRVERLYINT */
	/* * REG_BCN_MAX_ERR */
	/* * REG_BCNTCFG  (0x510) */
	/* * REG_DUAL_TSF_RST */
	/* * REG_BCN_CTRL  (0x550) */
#endif

	/* ATIM window */
	rtw_write16(adapter, REG_ATIMWND_8821C, 2);

	/* Beacon interval (in unit of TU). */
	rtw_hal_set_hwreg(adapter, HW_VAR_BEACON_INTERVAL, (u8 *)&pmlmeinfo->bcn_interval);

	InitBeaconParameters(adapter);

	rtw_write8(adapter, REG_SLOT_8821C, 0x09);

	/* Reset TSF Timer to zero */
	hw_tsf_reset(adapter);

	rtw_write8(adapter, REG_RXTSF_OFFSET_CCK_8821C, 0x50);
	rtw_write8(adapter, REG_RXTSF_OFFSET_OFDM_8821C, 0x50);

	beacon_function_enable(adapter, _TRUE, _TRUE);

	ResumeTxBeacon(adapter);
}

void rtl8821c_set_hal_ops(PADAPTER adapter)
{
	struct hal_ops *ops_func = &adapter->hal_func;

	/*** initialize section ***/
	ops_func->read_chip_version = read_chip_version;
	/*
		ops->init_default_value = NULL;
	*/
	ops_func->read_adapter_info = rtl8821c_read_efuse;
	ops_func->hal_power_on = rtl8821c_power_on;
	ops_func->hal_power_off = rtl8821c_power_off;

	ops_func->dm_init = rtl8821c_phy_init_dm_priv;
	ops_func->dm_deinit = rtl8821c_phy_deinit_dm_priv;

	/*** xmit section ***/
	/*
		ops->init_xmit_priv = NULL;
		ops->free_xmit_priv = NULL;
		ops->hal_xmit = NULL;
		ops->mgnt_xmit = NULL;
		ops->hal_xmitframe_enqueue = NULL;
	#ifdef CONFIG_XMIT_THREAD_MODE
		ops->xmit_thread_handler = NULL;
	#endif
	*/
	/*
		ops_func->run_thread = rtl8821c_run_thread;
		ops_func->cancel_thread = rtl8821c_cancel_thread;
	*/
	/*** recv section ***/
	/*
		ops->init_recv_priv = NULL;
		ops->free_recv_priv = NULL;
	#if defined(CONFIG_USB_HCI) || defined(CONFIG_PCI_HCI)
		ops->inirp_init = NULL;
		ops->inirp_deinit = NULL;
	#endif
	*/
	/*** interrupt hdl section ***/
	/*
		ops->enable_interrupt = NULL;
		ops->disable_interrupt = NULL;
	*/
	ops_func->check_ips_status = check_ips_status;
	/*
	#if defined(CONFIG_PCI_HCI)
		ops->interrupt_handler = NULL;
	#endif
	#if defined(CONFIG_USB_HCI) && defined(CONFIG_SUPPORT_USB_INT)
		ops->interrupt_handler = NULL;
	#endif
	#if defined(CONFIG_PCI_HCI)
		ops->irp_reset = NULL;
	#endif
	*/

	/*** DM section ***/
	ops_func->set_chnl_bw_handler = rtl8821c_set_channel_bw;

	ops_func->set_tx_power_level_handler = rtl8821c_set_tx_power_level;
	ops_func->set_tx_power_index_handler = rtl8821c_set_tx_power_index;
	ops_func->get_tx_power_index_handler = hal_com_get_txpwr_idx;

	ops_func->hal_dm_watchdog = rtl8821c_phy_haldm_watchdog;

	ops_func->GetHalODMVarHandler = GetHalODMVar;
	ops_func->SetHalODMVarHandler = SetHalODMVar;

	ops_func->SetBeaconRelatedRegistersHandler = set_beacon_related_registers;

#ifdef CONFIG_ANTENNA_DIVERSITY
	/*
		ops->AntDivBeforeLinkHandler = NULL;
		ops->AntDivCompareHandler = NULL;
	*/
#endif
	/*
		ops->interface_ps_func = NULL;
	*/

	ops_func->read_bbreg = rtl8821c_read_bb_reg;
	ops_func->write_bbreg = rtl8821c_write_bb_reg;
	ops_func->read_rfreg = rtl8821c_read_rf_reg;
	ops_func->write_rfreg = rtl8821c_write_rf_reg;
	ops_func->read_wmmedca_reg = rtl8821c_read_wmmedca_reg;

#ifdef CONFIG_HOSTAPD_MLME
	/*
		ops->hostap_mgnt_xmit_entry = NULL;
	*/
#endif
	/*
		ops->EfusePowerSwitch = NULL;
		ops->BTEfusePowerSwitch = NULL;
		ops->ReadEFuse = NULL;
		ops->EFUSEGetEfuseDefinition = NULL;
		ops->EfuseGetCurrentSize = NULL;
		ops->Efuse_PgPacketRead = NULL;
		ops->Efuse_PgPacketWrite = NULL;
		ops->Efuse_WordEnableDataWrite = NULL;
		ops->Efuse_PgPacketWrite_BT = NULL;
	*/
#ifdef DBG_CONFIG_ERROR_DETECT
	ops_func->sreset_init_value = sreset_init_value;
	ops_func->sreset_reset_value = sreset_reset_value;
	ops_func->silentreset = sreset_reset;
	ops_func->sreset_xmit_status_check = xmit_status_check;
	ops_func->sreset_linked_status_check  = linked_status_check;
	ops_func->sreset_get_wifi_status  = sreset_get_wifi_status;
	ops_func->sreset_inprogress = sreset_inprogress;
#endif /* DBG_CONFIG_ERROR_DETECT */

#ifdef CONFIG_IOL
	/*
		ops->IOL_exec_cmds_sync = NULL;
	*/
#endif

	ops_func->hal_notch_filter = rtl8821c_notch_filter_switch;
	ops_func->hal_mac_c2h_handler = c2h_handler_rtl8821c;
	ops_func->fill_h2c_cmd = rtl8821c_fillh2ccmd;
	ops_func->fill_fake_txdesc = fill_fake_txdesc;
	ops_func->fw_dl = rtl8821c_fw_dl;

#ifdef CONFIG_LPS_PG
	ops_func->fw_mem_dl = rtl8821c_fw_mem_dl;
#endif
#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
	/*
		ops->clear_interrupt = NULL;
	*/
#endif
	/*
		ops_func->hal_get_tx_buff_rsvd_page_num = NULL;
	*/
#ifdef CONFIG_GPIO_API
	/*
		ops->update_hisr_hsisr_ind = NULL;
	*/
#endif

	/* HALMAC related functions */
	ops_func->init_mac_register = rtl8821c_init_phy_parameter_mac;
	ops_func->init_phy = rtl8821c_phy_init;

}
