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
#define _RTL8822B_OPS_C_

#include <drv_types.h>		/* basic_types.h, rtw_io.h and etc. */
#include <rtw_xmit.h>		/* struct xmit_priv */
#ifdef DBG_CONFIG_ERROR_DETECT
#include <rtw_sreset.h>
#endif /* DBG_CONFIG_ERROR_DETECT */
#include <hal_data.h>		/* PHAL_DATA_TYPE, GET_HAL_DATA() */
#include <hal_com.h>		/* dump_chip_info() and etc. */
#include "../hal_halmac.h"	/* GET_RX_DESC_XXX_8822B() */
#include "rtl8822b.h"
#include "rtl8822b_hal.h"


static const struct hw_port_reg port_cfg[] = {
	/*port 0*/
	{
	.net_type = (REG_CR_8822B + 2),
	.net_type_shift = 0,
	.macaddr = REG_MACID_8822B,
	.bssid = REG_BSSID_8822B,
	.bcn_ctl = REG_BCN_CTRL_8822B,
	.tsf_rst = REG_DUAL_TSF_RST,
	.tsf_rst_bit = BIT_TSFTR_RST_8822B,
	.bcn_space = REG_MBSSID_BCN_SPACE_8822B,
	.bcn_space_shift = 0,
	.bcn_space_mask = 0xffff,
	.ps_aid = REG_BCN_PSR_RPT_8822B,
	.ta = REG_TRANSMIT_ADDRSS_0_8822B,
	},
	/*port 1*/
	{
	.net_type = (REG_CR_8822B + 2),
	.net_type_shift = 2,
	.macaddr = REG_MACID1_8822B,
	.bssid = REG_BSSID1_8822B,
	.bcn_ctl = REG_BCN_CTRL_CLINT0_8822B,
	.tsf_rst = REG_DUAL_TSF_RST,
	.tsf_rst_bit = BIT_TSFTR_CLI0_RST_8822B,
	.bcn_space = REG_MBSSID_BCN_SPACE_8822B,
	.bcn_space_shift = 16,
	.bcn_space_mask = 0xfff,
	.ps_aid = REG_BCN_PSR_RPT1_8822B,
	.ta = REG_TRANSMIT_ADDRSS_1_8822B,
	},
	/*port 2*/
	{
	.net_type =  REG_CR_EXT_8822B,
	.net_type_shift = 0,
	.macaddr = REG_MACID2_8822B,
	.bssid = REG_BSSID2_8822B,
	.bcn_ctl = REG_BCN_CTRL_CLINT1_8822B,
	.tsf_rst = REG_DUAL_TSF_RST,
	.tsf_rst_bit = BIT_TSFTR_CLI1_RST_8822B,
	.bcn_space = REG_MBSSID_BCN_SPACE2_8822B,
	.bcn_space_shift = 0,
	.bcn_space_mask = 0xfff,
	.ps_aid = REG_BCN_PSR_RPT2_8822B,
	.ta = REG_TRANSMIT_ADDRSS_2_8822B,
	},
	/*port 3*/
	{
	.net_type =  REG_CR_EXT_8822B,
	.net_type_shift = 2,
	.macaddr = REG_MACID3_8822B,
	.bssid = REG_BSSID3_8822B,
	.bcn_ctl = REG_BCN_CTRL_CLINT2_8822B,
	.tsf_rst = REG_DUAL_TSF_RST,
	.tsf_rst_bit = BIT_TSFTR_CLI2_RST_8822B,
	.bcn_space = REG_MBSSID_BCN_SPACE2_8822B,
	.bcn_space_shift = 16,
	.bcn_space_mask = 0xfff,
	.ps_aid = REG_BCN_PSR_RPT3_8822B,
	.ta = REG_TRANSMIT_ADDRSS_3_8822B,
	},
	/*port 4*/
	{
	.net_type =  REG_CR_EXT_8822B,
	.net_type_shift = 4,
	.macaddr = REG_MACID4_8822B,
	.bssid = REG_BSSID4_8822B,
	.bcn_ctl = REG_BCN_CTRL_CLINT3_8822B,
	.tsf_rst = REG_DUAL_TSF_RST,
	.tsf_rst_bit = BIT_TSFTR_CLI3_RST_8822B,
	.bcn_space = REG_MBSSID_BCN_SPACE3_8822B,
	.bcn_space_shift = 0,
	.bcn_space_mask = 0xfff,
	.ps_aid = REG_BCN_PSR_RPT4_8822B,
	.ta = REG_TRANSMIT_ADDRSS_4_8822B,
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

#ifdef CONFIG_CLIENT_PORT_CFG
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
#endif

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

static void read_chip_version(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal;
	u32 value32;


	hal = GET_HAL_DATA(adapter);

	value32 = rtw_read32(adapter, REG_SYS_CFG1_8822B);
	hal->version_id.ICType = CHIP_8822B;
	hal->version_id.ChipType = ((value32 & BIT_RTL_ID_8822B) ? TEST_CHIP : NORMAL_CHIP);
	hal->version_id.CUTVersion = BIT_GET_CHIP_VER_8822B(value32);
	hal->version_id.VendorType = BIT_GET_VENDOR_ID_8822B(value32);
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

	hal->version_id.RFType = ((value32 & BIT_RF_TYPE_ID_8822B) ? RF_TYPE_2T2R : RF_TYPE_1T1R);

	hal->RegulatorMode = ((value32 & BIT_SPSLDO_SEL_8822B) ? RT_LDO_REGULATOR : RT_SWITCHING_REGULATOR);

	value32 = rtw_read32(adapter, REG_SYS_STATUS1_8822B);
	hal->version_id.ROMVer = BIT_GET_RF_RL_ID_8822B(value32);

	/* For multi-function consideration. */
	hal->MultiFunc = RT_MULTI_FUNC_NONE;
	value32 = rtw_read32(adapter, REG_WL_BT_PWR_CTRL_8822B);
	hal->MultiFunc |= ((value32 & BIT_WL_FUNC_EN_8822B) ? RT_MULTI_FUNC_WIFI : 0);
	hal->MultiFunc |= ((value32 & BIT_BT_FUNC_EN_8822B) ? RT_MULTI_FUNC_BT : 0);
	hal->PolarityCtl = ((value32 & BIT_WL_HWPDN_SL_8822B) ? RT_POLARITY_HIGH_ACT : RT_POLARITY_LOW_ACT);

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

	RTW_WARN("EEPROM ID is invalid!!\n");
	return _FALSE;
}

static void Hal_EfuseParseEEPROMVer(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	if (_TRUE == mapvalid)
		hal->EEPROMVersion = map[EEPROM_VERSION_8822B];
	else
		hal->EEPROMVersion = 1;

	RTW_INFO("EEPROM Version = %d\n", hal->EEPROMVersion);
}

static void Hal_EfuseParseTxPowerInfo(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);

	hal->txpwr_pg_mode = TXPWR_PG_WITH_PWR_IDX;

	if ((_TRUE == mapvalid) && (map[EEPROM_RF_BOARD_OPTION_8822B] != 0xFF))
		hal->EEPROMRegulatory = map[EEPROM_RF_BOARD_OPTION_8822B] & 0x7; /* bit0~2 */
	else
		hal->EEPROMRegulatory = EEPROM_DEFAULT_BOARD_OPTION & 0x7; /* bit0~2 */
	RTW_INFO("EEPROM Regulatory=0x%02x\n", hal->EEPROMRegulatory);
}

static void Hal_EfuseParseBoardType(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	if ((_TRUE == mapvalid) && (map[EEPROM_RF_BOARD_OPTION_8822B] != 0xFF))
		hal->InterfaceSel = (map[EEPROM_RF_BOARD_OPTION_8822B] & 0xE0) >> 5;
	else
		hal->InterfaceSel = (EEPROM_DEFAULT_BOARD_OPTION & 0xE0) >> 5;

	RTW_INFO("EEPROM Board Type=0x%02x\n", hal->InterfaceSel);
}

static void Hal_EfuseParseBTCoexistInfo(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	u8 setting;
	u32 tmpu4;

	if ((_TRUE == mapvalid) && (map[EEPROM_RF_BOARD_OPTION_8822B] != 0xFF)) {
		/* 0xc1[7:5] = 0x01 */
		if (((map[EEPROM_RF_BOARD_OPTION_8822B] & 0xe0) >> 5) == 0x01)
			hal->EEPROMBluetoothCoexist = _TRUE;
		else
			hal->EEPROMBluetoothCoexist = _FALSE;
	} else
		hal->EEPROMBluetoothCoexist = _FALSE;

	hal->EEPROMBluetoothType = BT_RTL8822B;

	setting = map[EEPROM_RF_BT_SETTING_8822B];
	if ((_TRUE == mapvalid) && (setting != 0xFF)) {
		hal->EEPROMBluetoothAntNum = setting & BIT(0);
		/*
		 * EFUSE_0xC3[6] == 0, S1(Main)-RF_PATH_A;
		 * EFUSE_0xC3[6] == 1, S0(Aux)-RF_PATH_B
		 */
		hal->ant_path = (setting & BIT(6)) ? RF_PATH_B : RF_PATH_A;
	} else {
		hal->EEPROMBluetoothAntNum = Ant_x2;
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
		map ? &map[EEPROM_COUNTRY_CODE_8822B] : NULL,
		map ? map[EEPROM_ChannelPlan_8822B] : 0xFF,
		adapter->registrypriv.alpha2,
		adapter->registrypriv.channel_plan,
		autoloadfail
	);
}

static void Hal_EfuseParseXtal(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	if ((_TRUE == mapvalid) && map[EEPROM_XTAL_8822B] != 0xFF)
		hal->crystal_cap = map[EEPROM_XTAL_8822B];
	else
		hal->crystal_cap = EEPROM_Default_CrystalCap;

	RTW_INFO("EEPROM crystal_cap=0x%02x\n", hal->crystal_cap);
}

static void Hal_EfuseParseThermalMeter(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	/* ThermalMeter from EEPROM */
	if ((_TRUE == mapvalid) && (map[EEPROM_THERMAL_METER_8822B] != 0xFF))
		hal->eeprom_thermal_meter = map[EEPROM_THERMAL_METER_8822B];
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


	if (hal->EEPROMBluetoothAntNum == Ant_x1)
		hal->AntDivCfg = 0;
	else {
		if (registry_par->antdiv_cfg == 2)/* 0:OFF , 1:ON, 2:By EFUSE */
			hal->AntDivCfg = 1;
		else
			hal->AntDivCfg = registry_par->antdiv_cfg;
	}

	/* If TRxAntDivType is AUTO in advanced setting, use EFUSE value instead. */
	if (registry_par->antdiv_type == 0) {
		hal->TRxAntDivType = map[EEPROM_RFE_OPTION_8822B];
		if (hal->TRxAntDivType == 0xFF)
			hal->TRxAntDivType = S0S1_SW_ANTDIV; /* internal switch S0S1 */
		else if (hal->TRxAntDivType == 0x10)
			hal->TRxAntDivType = S0S1_SW_ANTDIV; /* internal switch S0S1 */
		else if (hal->TRxAntDivType == 0x11)
			hal->TRxAntDivType = S0S1_SW_ANTDIV; /* internal switch S0S1 */
		else
			RTW_INFO("EEPROM efuse[0x%x]=0x%02x is unknown type\n",
				 EEPROM_RFE_OPTION_8723B, hal->TRxAntDivType);
	} else
		hal->TRxAntDivType = registry_par->antdiv_type;

	RTW_INFO("EEPROM AntDivCfg=%d, AntDivType=%d\n",
		 hal->AntDivCfg, hal->TRxAntDivType);
#endif /* CONFIG_ANTENNA_DIVERSITY */
}

static void Hal_EfuseParseCustomerID(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	if (_TRUE == mapvalid)
		hal->EEPROMCustomerID = map[EEPROM_CustomID_8822B];
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
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);

	if (mapvalid) {
		/* AUTO */
		if (GetRegAmplifierType2G(adapter) == 0) {
			hal->PAType_2G = ReadLE1Byte(&map[EEPROM_2G_5G_PA_TYPE_8822B]);
			hal->LNAType_2G = ReadLE1Byte(&map[EEPROM_2G_LNA_TYPE_GAIN_SEL_AB_8822B]);

			if (hal->PAType_2G == 0xFF)
				hal->PAType_2G = 0;

			if (hal->LNAType_2G == 0xFF)
				hal->LNAType_2G = 0;

			hal->ExternalPA_2G = (hal->PAType_2G & BIT4) ? 1 : 0;
			hal->ExternalLNA_2G = (hal->LNAType_2G & BIT3) ? 1 : 0;
		} else {
			hal->ExternalPA_2G  = (GetRegAmplifierType2G(adapter) & ODM_BOARD_EXT_PA)  ? 1 : 0;
			hal->ExternalLNA_2G = (GetRegAmplifierType2G(adapter) & ODM_BOARD_EXT_LNA) ? 1 : 0;
		}

		/* AUTO */
		if (GetRegAmplifierType5G(adapter) == 0) {
			hal->PAType_5G = ReadLE1Byte(&map[EEPROM_2G_5G_PA_TYPE_8822B]);
			hal->LNAType_5G = ReadLE1Byte(&map[EEPROM_5G_LNA_TYPE_GAIN_SEL_AB_8822B]);
			if (hal->PAType_5G == 0xFF)
				hal->PAType_5G = 0;
			if (hal->LNAType_5G == 0xFF)
				hal->LNAType_5G = 0;

			hal->external_pa_5g = (hal->PAType_5G & BIT0) ? 1 : 0;
			hal->external_lna_5g = (hal->LNAType_5G & BIT3) ? 1 : 0;
		} else {
			hal->external_pa_5g  = (GetRegAmplifierType5G(adapter) & ODM_BOARD_EXT_PA_5G)  ? 1 : 0;
			hal->external_lna_5g = (GetRegAmplifierType5G(adapter) & ODM_BOARD_EXT_LNA_5G) ? 1 : 0;
		}
	} else {
		hal->ExternalPA_2G  = EEPROM_Default_PAType;
		hal->external_pa_5g  = 0xFF;
		hal->ExternalLNA_2G = EEPROM_Default_LNAType;
		hal->external_lna_5g = 0xFF;

		/* AUTO */
		if (GetRegAmplifierType2G(adapter) == 0) {
			hal->ExternalPA_2G  = 0;
			hal->ExternalLNA_2G = 0;
		} else {
			hal->ExternalPA_2G  = (GetRegAmplifierType2G(adapter) & ODM_BOARD_EXT_PA)  ? 1 : 0;
			hal->ExternalLNA_2G = (GetRegAmplifierType2G(adapter) & ODM_BOARD_EXT_LNA) ? 1 : 0;
		}

		/* AUTO */
		if (GetRegAmplifierType5G(adapter) == 0) {
			hal->external_pa_5g  = 0;
			hal->external_lna_5g = 0;
		} else {
			hal->external_pa_5g  = (GetRegAmplifierType5G(adapter) & ODM_BOARD_EXT_PA_5G)  ? 1 : 0;
			hal->external_lna_5g = (GetRegAmplifierType5G(adapter) & ODM_BOARD_EXT_LNA_5G) ? 1 : 0;
		}
	}

	RTW_INFO("EEPROM PAType_2G is 0x%x, ExternalPA_2G = %d\n", hal->PAType_2G, hal->ExternalPA_2G);
	RTW_INFO("EEPROM PAType_5G is 0x%x, external_pa_5g = %d\n", hal->PAType_5G, hal->external_pa_5g);
	RTW_INFO("EEPROM LNAType_2G is 0x%x, ExternalLNA_2G = %d\n", hal->LNAType_2G, hal->ExternalLNA_2G);
	RTW_INFO("EEPROM LNAType_5G is 0x%x, external_lna_5g = %d\n", hal->LNAType_5G, hal->external_lna_5g);
}

static void Hal_ReadAmplifierType(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	u8 extTypePA_2G_A = (map[EEPROM_2G_LNA_TYPE_GAIN_SEL_AB_8822B] & BIT2) >> 2;
	u8 extTypePA_2G_B = (map[EEPROM_2G_LNA_TYPE_GAIN_SEL_AB_8822B] & BIT6) >> 6;
	u8 extTypePA_5G_A = (map[EEPROM_5G_LNA_TYPE_GAIN_SEL_AB_8822B] & BIT2) >> 2;
	u8 extTypePA_5G_B = (map[EEPROM_5G_LNA_TYPE_GAIN_SEL_AB_8822B] & BIT6) >> 6;
	u8 extTypeLNA_2G_A = (map[EEPROM_2G_LNA_TYPE_GAIN_SEL_AB_8822B] & (BIT1 | BIT0)) >> 0;
	u8 extTypeLNA_2G_B = (map[EEPROM_2G_LNA_TYPE_GAIN_SEL_AB_8822B] & (BIT5 | BIT4)) >> 4;
	u8 extTypeLNA_5G_A = (map[EEPROM_5G_LNA_TYPE_GAIN_SEL_AB_8822B] & (BIT1 | BIT0)) >> 0;
	u8 extTypeLNA_5G_B = (map[EEPROM_5G_LNA_TYPE_GAIN_SEL_AB_8822B] & (BIT5 | BIT4)) >> 4;

	hal_ReadPAType(adapter, map, mapvalid);

	/* [2.4G] Path A and B are both extPA */
	if ((hal->PAType_2G & (BIT5 | BIT4)) == (BIT5 | BIT4))
		hal->TypeGPA  = extTypePA_2G_B  << 2 | extTypePA_2G_A;

	/* [5G] Path A and B are both extPA */
	if ((hal->PAType_5G & (BIT1 | BIT0)) == (BIT1 | BIT0))
		hal->TypeAPA  = extTypePA_5G_B  << 2 | extTypePA_5G_A;

	/* [2.4G] Path A and B are both extLNA */
	if ((hal->LNAType_2G & (BIT7 | BIT3)) == (BIT7 | BIT3))
		hal->TypeGLNA = extTypeLNA_2G_B << 2 | extTypeLNA_2G_A;

	/* [5G] Path A and B are both extLNA */
	if ((hal->LNAType_5G & (BIT7 | BIT3)) == (BIT7 | BIT3))
		hal->TypeALNA = extTypeLNA_5G_B << 2 | extTypeLNA_5G_A;

	RTW_INFO("EEPROM TypeGPA = 0x%X\n", hal->TypeGPA);
	RTW_INFO("EEPROM TypeAPA = 0x%X\n", hal->TypeAPA);
	RTW_INFO("EEPROM TypeGLNA = 0x%X\n", hal->TypeGLNA);
	RTW_INFO("EEPROM TypeALNA = 0x%X\n", hal->TypeALNA);
}

static u8 Hal_ReadRFEType(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	/* check registry value */
	if (GetRegRFEType(adapter) != CONFIG_RTW_RFE_TYPE) {
		hal->rfe_type = GetRegRFEType(adapter);
		goto exit;
	}

	if (mapvalid) {
		/* check efuse map */
		hal->rfe_type = ReadLE1Byte(&map[EEPROM_RFE_OPTION_8822B]);
		if (0xFF != hal->rfe_type)
			goto exit;
	}

	/* error handle */
	hal->rfe_type = 0;

	/* If ignore incorrect rfe_type may cause card drop. */
	/* it's DIFFICULT do debug especially on COB project */
	RTW_ERR("\n\nEmpty EFUSE with unknown REF type!!\n\n");
	RTW_ERR("please program efuse or specify correct RFE type.\n");
	RTW_ERR("cmd: insmod rtl8822bx.ko rtw_RFE_type=<rfe_type>\n\n");

	return _FAIL;

exit:
	RTW_INFO("EEPROM rfe_type=0x%x\n", hal->rfe_type);
	return _SUCCESS;
}

static void Hal_EfuseParsePackageType(PADAPTER adapter, u8 *map, u8 mapvalid)
{
}

static void Hal_EfuseParsePABias(PADAPTER adapter)
{
	struct hal_com_data *hal;
	u8 data[2] = {0xFF, 0xFF};
	u8 ret;


	ret = rtw_efuse_access(adapter, 0, 0x3D7, 2, data);
	if (_FAIL == ret) {
		RTW_ERR("%s: Fail to read PA Bias from eFuse!\n", __FUNCTION__);
		return;
	}

	hal = GET_HAL_DATA(adapter);
	hal->efuse0x3d7 = data[0];	/* efuse[0x3D7] */
	hal->efuse0x3d8 = data[1];	/* efuse[0x3D8] */

	RTW_INFO("EEPROM efuse[0x3D7]=0x%x\n", hal->efuse0x3d7);
	RTW_INFO("EEPROM efuse[0x3D8]=0x%x\n", hal->efuse0x3d8);
}


#ifdef CONFIG_USB_HCI
static void Hal_ReadUsbModeSwitch(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);

	if (_TRUE == mapvalid)
		/* check efuse 0x06 bit7 */
		hal->EEPROMUsbSwitch = (map[EEPROM_USB_MODE_8822BU] & BIT7) >> 7;
	else
		hal->EEPROMUsbSwitch = _FALSE;

	RTW_INFO("EEPROM USB Switch=%d\n", hal->EEPROMUsbSwitch);
}

static void hal_read_usb_pid_vid(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);

	if (_TRUE == mapvalid) {
		/* VID, PID */
		hal->EEPROMVID = ReadLE2Byte(&map[EEPROM_VID_8822BU]);
		hal->EEPROMPID = ReadLE2Byte(&map[EEPROM_PID_8822BU]);
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
u8 rtl8822b_read_efuse(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal;
	u8 val8;
	u8 *efuse_map = NULL;
	u8 valid;
	u8 ret = _FAIL;

	hal = GET_HAL_DATA(adapter);
	efuse_map = hal->efuse_eeprom_data;

	/* 1. Read registers to check hardware eFuse available or not */
	val8 = rtw_read8(adapter, REG_SYS_EEPROM_CTRL_8822B);
	hal->EepromOrEfuse = (val8 & BIT_EERPOMSEL_8822B) ? _TRUE : _FALSE;
	hal->bautoload_fail_flag = (val8 & BIT_AUTOLOAD_SUS_8822B) ? _FALSE : _TRUE;
	/*
	 * In 8822B, bautoload_fail_flag is used to present eFuse map is valid
	 * or not, no matter the map comes from hardware or files.
	 */

	/* 2. Read eFuse */
	EFUSE_ShadowMapUpdate(adapter, EFUSE_WIFI, 0);

	/* 3. Read Efuse file if necessary */
#ifdef CONFIG_EFUSE_CONFIG_FILE
	if (check_phy_efuse_tx_power_info_valid(adapter) == _FALSE) {
		if (Hal_readPGDataFromConfigFile(adapter) != _SUCCESS)
			RTW_WARN("%s: invalid phy efuse and read from file fail, will use driver default!!\n", __FUNCTION__);
	}
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
	Hal_ReadAmplifierType(adapter, efuse_map, valid);
	if (Hal_ReadRFEType(adapter, efuse_map, valid) != _SUCCESS)
		goto exit;

	/* Data out of Efuse Map */
	Hal_EfuseParsePackageType(adapter, efuse_map, valid);
	Hal_EfuseParsePABias(adapter);

#ifdef CONFIG_USB_HCI
	Hal_ReadUsbModeSwitch(adapter, efuse_map, valid);
	hal_read_usb_pid_vid(adapter, efuse_map, valid);
#endif /* CONFIG_USB_HCI */

	/* set coex. ant info once efuse parsing is done */
	rtw_btcoex_set_ant_info(adapter);

	hal_read_mac_hidden_rpt(adapter);
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

void rtl8822b_run_thread(PADAPTER adapter)
{
}

void rtl8822b_cancel_thread(PADAPTER adapter)
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

static void InitBeaconParameters(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	u16 val16;
	u8 val8;


	val8 = BIT_DIS_TSF_UDT_8822B;
	val16 = val8 | (val8 << 8); /* port0 and port1 */
#ifdef CONFIG_BT_COEXIST
	if (hal->EEPROMBluetoothCoexist)
		/* Enable port0 beacon function for PSTDMA under BTCOEX */
		val16 |= EN_BCN_FUNCTION;
#endif
	rtw_write16(adapter, REG_BCN_CTRL_8822B, val16);

	/* TBTT setup time */
	rtw_write8(adapter, REG_TBTT_PROHIBIT_8822B, TBTT_PROHIBIT_SETUP_TIME);

	/* TBTT hold time: 0x540[19:8] */
	rtw_write8(adapter, REG_TBTT_PROHIBIT_8822B + 1, TBTT_PROHIBIT_HOLD_TIME_STOP_BCN & 0xFF);
	rtw_write8(adapter, REG_TBTT_PROHIBIT_8822B + 2,
		(rtw_read8(adapter, REG_TBTT_PROHIBIT_8822B + 2) & 0xF0) | (TBTT_PROHIBIT_HOLD_TIME_STOP_BCN >> 8));

	rtw_write8(adapter, REG_DRVERLYINT_8822B, DRIVER_EARLY_INT_TIME_8822B); /* 5ms */
	rtw_write8(adapter, REG_BCNDMATIM_8822B, BCN_DMA_ATIME_INT_TIME_8822B); /* 2ms */

	/*
	 * Suggested by designer timchen. Change beacon AIFS to the largest number
	 * beacause test chip does not contension before sending beacon.
	 */
	rtw_write16(adapter, REG_BCNTCFG_8822B, 0x4413);
}

static void beacon_function_enable(PADAPTER adapter, u8 Enable, u8 Linked)
{
	u8 val8;
	u32 bcn_ctrl_reg;

	/* port0 */
	bcn_ctrl_reg = REG_BCN_CTRL_8822B;
	val8  = BIT_DIS_TSF_UDT_8822B | BIT_EN_BCN_FUNCTION_8822B;
#ifdef CONFIG_CONCURRENT_MODE
	/* port1 */
	if (adapter->hw_port == HW_PORT1) {
		bcn_ctrl_reg = REG_BCN_CTRL_CLINT0_8822B;
		val8 = BIT_CLI0_DIS_TSF_UDT_8822B | BIT_CLI0_EN_BCN_FUNCTION_8822B;
	}
#endif

	rtw_write8(adapter, bcn_ctrl_reg, val8);
	rtw_write8(adapter, REG_RD_CTRL_8822B + 1, 0x6F);
}

static void set_beacon_related_registers(PADAPTER adapter)
{
	u8 val8;
	u32 value32;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	u32 bcn_ctrl_reg, bcn_interval_reg;


	/* reset TSF, enable update TSF, correcting TSF On Beacon */
	/*
	 * REG_MBSSID_BCN_SPACE
	 * REG_BCNDMATIM
	 * REG_ATIMWND
	 * REG_TBTT_PROHIBIT
	 * REG_DRVERLYINT
	 * REG_BCN_MAX_ERR
	 * REG_BCNTCFG (0x510)
	 * REG_DUAL_TSF_RST
	 * REG_BCN_CTRL (0x550)
	 */

	bcn_ctrl_reg = REG_BCN_CTRL_8822B;
#ifdef CONFIG_CONCURRENT_MODE
	if (adapter->hw_port == HW_PORT1)
		bcn_ctrl_reg = REG_BCN_CTRL_CLINT0_8822B;
#endif

	/*
	 * ATIM window
	 */
	rtw_write16(adapter, REG_ATIMWND_8822B, 2);

	/*
	 * Beacon interval (in unit of TU).
	 */
	rtw_hal_set_hwreg(adapter, HW_VAR_BEACON_INTERVAL, (u8 *)&pmlmeinfo->bcn_interval);

	InitBeaconParameters(adapter);

	rtw_write8(adapter, REG_SLOT_8822B, 0x09);

	/* Reset TSF Timer to zero */
	val8 = BIT_TSFTR_RST_8822B;
#ifdef CONFIG_CONCURRENT_MODE
	if (adapter->hw_port == HW_PORT1)
		val8 = BIT_TSFTR_CLI0_RST_8822B;
#endif
	rtw_write8(adapter, REG_DUAL_TSF_RST_8822B, val8);
	val8 = BIT_TSFTR_RST_8822B;
	rtw_write8(adapter, REG_DUAL_TSF_RST_8822B, val8);

	rtw_write8(adapter, REG_RXTSF_OFFSET_CCK_8822B, 0x50);
	rtw_write8(adapter, REG_RXTSF_OFFSET_OFDM_8822B, 0x50);

	beacon_function_enable(adapter, _TRUE, _TRUE);

	ResumeTxBeacon(adapter);
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

	txdma_status = rtw_read32(p, REG_TXDMA_STATUS_8822B);
	if (txdma_status != 0x00) {
		RTW_INFO("%s REG_TXDMA_STATUS:0x%08x\n", __FUNCTION__, txdma_status);
		psrtpriv->tx_dma_status_cnt++;
		psrtpriv->self_dect_case = 4;
		rtw_hal_sreset_reset(p);
	}
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
					u32 ability = 0;

					ability = rtw_phydm_ability_get(p);

					RTW_INFO("%s tx hang %s\n", __FUNCTION__,
						(ability & ODM_BB_ADAPTIVITY) ? "ODM_BB_ADAPTIVITY" : "");

					if (!(ability & ODM_BB_ADAPTIVITY)) {
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

	cur_mac_rxff_ptr = rtw_read16(p, REG_RXFF_PTR_V1_8822B);

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

	rx_dma_status = rtw_read32(p, REG_RXDMA_STATUS_8822B);
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
#if defined(CONFIG_USB_HCI) || defined(CONFIG_PCI_HCI)
		rtw_hal_sreset_reset(p);
#endif /* CONFIG_USB_HCI || CONFIG_PCI_HCI */
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

static void set_opmode_monitor(PADAPTER adapter)
{
	u32 rcr_bits;
	u16 value_rxfltmap2;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;


	/* Receive all type */
	rcr_bits = BIT_AAP_8822B | BIT_APM_8822B | BIT_AM_8822B
		   | BIT_AB_8822B | BIT_APWRMGT_8822B
		   | BIT_APP_PHYSTS_8822B;

#ifndef CONFIG_CUSTOMER_ALIBABA_GENERAL
	/* Append FCS */
	rcr_bits |= BIT_APP_FCS_8822B;
#endif

	rtw_hal_get_hwreg(adapter, HW_VAR_RCR, (u8 *)&GET_HAL_DATA(adapter)->rcr_backup);
	rtw_hal_set_hwreg(adapter, HW_VAR_RCR, (u8 *)&rcr_bits);

	/* Receive all data frames */
	value_rxfltmap2 = 0xFFFF;
	rtw_write16(adapter, REG_RXFLTMAP2_8822B, value_rxfltmap2);
}

static void set_opmode_port0(PADAPTER adapter, u8 mode)
{
	u8 is_tx_bcn;
	u8 val8;
	u16 val16;
	u32 val32;


#ifdef CONFIG_CONCURRENT_MODE
	is_tx_bcn = rtw_mi_get_ap_num(adapter) || rtw_mi_get_mesh_num(adapter);
#else /* !CONFIG_CONCURRENT_MODE */
	is_tx_bcn = 0;
#endif /* !CONFIG_CONCURRENT_MODE */

	/* disable Port0 TSF update */
	rtw_iface_disable_tsf_update(adapter);

	Set_MSR(adapter, mode);

	RTW_INFO(FUNC_ADPT_FMT ": hw_port(%d) mode=%d\n",
		 FUNC_ADPT_ARG(adapter), adapter->hw_port, mode);

	switch (mode) {
	case _HW_STATE_NOLINK_:
	case _HW_STATE_STATION_:
		if (!is_tx_bcn) {
			StopTxBeacon(adapter);
#ifdef CONFIG_PCI_HCI
			UpdateInterruptMask8822BE(adapter, 0, 0, RT_BCN_INT_MASKS, 0);
#endif /* CONFIG_PCI_HCI */
		}

		/* disable beacon function */
		val8 = BIT_DIS_TSF_UDT_8822B | BIT_EN_BCN_FUNCTION_8822B;
		rtw_write8(adapter, REG_BCN_CTRL_8822B, val8);

		/* disable atim wnd(only for Port0) */
		val8 = rtw_read8(adapter, REG_DIS_ATIM_8822B);
		val8 |= BIT_DIS_ATIM_ROOT_8822B;
		rtw_write8(adapter, REG_DIS_ATIM_8822B, val8);
		break;

	case _HW_STATE_ADHOC_:
		ResumeTxBeacon(adapter);
		val8 = BIT_DIS_TSF_UDT_8822B | BIT_EN_BCN_FUNCTION_8822B;
		rtw_write8(adapter, REG_BCN_CTRL_8822B, val8);
		break;

	case _HW_STATE_AP_:
#ifdef CONFIG_PCI_HCI
		UpdateInterruptMask8822BE(adapter, RT_BCN_INT_MASKS, 0, 0, 0);
#endif /* CONFIG_PCI_HCI */

		/*
		 * enable BCN0 Function for if1
		 * disable update TSF0 for if1
		 * enable TX BCN report:
		 * Reg REG_FWHW_TXQ_CTRL_8822B [2] = 1
		 * Reg REG_BCN_CTRL_8822B[3][5] = 1
		 * Enable ATIM
		 * Enable HW seq for BCN
		 */
		/* enable TX BCN report */
		/* disable RX BCN report */
		val8 = rtw_read8(adapter, REG_FWHW_TXQ_CTRL_8822B);
		val8 |= BIT_EN_BCN_TRXRPT_V1_8822B;
		rtw_write8(adapter, REG_FWHW_TXQ_CTRL_8822B, val8);

		/* enable BCN0 Function */
		val8 = rtw_read8(adapter, REG_BCN_CTRL_8822B);
		val8 |= BIT_EN_BCN_FUNCTION_8822B | BIT_DIS_TSF_UDT_8822B | BIT_P0_EN_TXBCN_RPT_8822B;
		val8 &= (~BIT_P0_EN_RXBCN_RPT_8822B);
		rtw_write8(adapter, REG_BCN_CTRL_8822B, val8);

		/* Enable ATIM */
		val8 = rtw_read8(adapter, REG_DIS_ATIM_8822B);
		val8 &= ~BIT_DIS_ATIM_ROOT_8822B;
		rtw_write8(adapter, REG_DIS_ATIM_8822B, val8);

		/* Enable HW seq for BCN
			0x4FC[0]: EN_HWSEQ
=			0x4FC[1]: EN_HWSEQEXT
			According TX desc
		*/
		rtw_write8(adapter, REG_DUMMY_PAGE4_V1_8822B, 0x01);

		/* enable to rx data frame */
		rtw_write16(adapter, REG_RXFLTMAP2_8822B, 0xFFFF);

		/* enable to rx ps-poll */
		val16 = rtw_read16(adapter, REG_RXFLTMAP1_8822B);
		val16 |= BIT_CTRLFLT10EN_8822B;
		rtw_write16(adapter, REG_RXFLTMAP1_8822B, val16);

		/* Beacon Control related register for first time */
		rtw_write8(adapter, REG_BCNDMATIM_8822B, 0x02); /* 2ms */

		rtw_write8(adapter, REG_ATIMWND_8822B, 0x0c); /* 12ms */


		rtw_write16(adapter, REG_TSFTR_SYN_OFFSET_8822B, 0x7fff); /* +32767 (~32ms) */

		/* reset TSF */
		rtw_write8(adapter, REG_DUAL_TSF_RST_8822B, BIT_TSFTR_RST_8822B);

		/* SW_BCN_SEL - Port0 */
		rtw_hal_set_hwreg(adapter, HW_VAR_DL_BCN_SEL, NULL);

		/* select BCN on port 0 */
		val8 = rtw_read8(adapter, REG_CCK_CHECK_8822B);
		val8 &= ~BIT_BCN_PORT_SEL_8822B;
		rtw_write8(adapter, REG_CCK_CHECK_8822B, val8);
		break;
	}
}

static void set_opmode_port1(PADAPTER adapter, u8 mode)
{
#ifdef CONFIG_CONCURRENT_MODE
	u8 is_tx_bcn;
	u8 val8;

	is_tx_bcn = rtw_mi_get_ap_num(adapter) || rtw_mi_get_mesh_num(adapter);

	/* disable Port1 TSF update */
	rtw_iface_disable_tsf_update(adapter);

	Set_MSR(adapter, mode);

	RTW_INFO(FUNC_ADPT_FMT ": hw_port(%d) mode=%d\n",
		 FUNC_ADPT_ARG(adapter), adapter->hw_port, mode);

	switch (mode) {
	case _HW_STATE_NOLINK_:
	case _HW_STATE_STATION_:
		if (!is_tx_bcn) {
			StopTxBeacon(adapter);
#ifdef CONFIG_PCI_HCI
			UpdateInterruptMask8822BE(adapter, 0, 0, RT_BCN_INT_MASKS, 0);
#endif /* CONFIG_PCI_HCI */
		}

		/* disable beacon function */
		val8 = BIT_CLI0_DIS_TSF_UDT_8822B | BIT_CLI0_EN_BCN_FUNCTION_8822B;
		rtw_write8(adapter, REG_BCN_CTRL_CLINT0_8822B, val8);
		break;

	case _HW_STATE_ADHOC_:
		ResumeTxBeacon(adapter);
		val8 = BIT_CLI0_DIS_TSF_UDT_8822B | BIT_CLI0_EN_BCN_FUNCTION_8822B;
		rtw_write8(adapter, REG_BCN_CTRL_CLINT0_8822B, val8);
		break;

	case _HW_STATE_AP_:
#ifdef CONFIG_PCI_HCI
		UpdateInterruptMask8822BE(adapter, RT_BCN_INT_MASKS, 0, 0, 0);
#endif /* CONFIG_PCI_HCI */

		/* ToDo */
		break;
	}
#endif /* CONFIG_CONCURRENT_MODE */
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

static void hw_var_set_opmode(PADAPTER adapter, u8 mode)
{
	u8 val8;
	static u8 isMonitor = _FALSE;


	if (isMonitor == _TRUE) {
		/* reset RCR from backup */
		rtw_hal_set_hwreg(adapter, HW_VAR_RCR, (u8 *)&GET_HAL_DATA(adapter)->rcr_backup);
		rtw_hal_rcr_set_chk_bssid(adapter, MLME_ACTION_NONE);
		isMonitor = _FALSE;
	}

	if (mode == _HW_STATE_MONITOR_) {
		isMonitor = _TRUE;

		Set_MSR(adapter, _HW_STATE_NOLINK_);
		set_opmode_monitor(adapter);
		return;
	}

	/* clear crc bit */
	if (rtw_hal_rcr_check(adapter, BIT_ACRC32_8822B))
		rtw_hal_rcr_clear(adapter, BIT_ACRC32_8822B);

#ifdef CONFIG_MI_WITH_MBSSID_CAM /*For Port0 - MBSS CAM*/
	if (adapter->hw_port != HW_PORT0) {
		RTW_ERR(ADPT_FMT ": Configure MBSSID cam on HW_PORT%d\n", ADPT_ARG(adapter), adapter->hw_port);
		rtw_warn_on(1);
	} else
		hw_var_set_opmode_mbid(adapter, mode);
#else

	switch (adapter->hw_port) {
	case HW_PORT0:
		set_opmode_port0(adapter, mode);
		break;

	case HW_PORT1:
		set_opmode_port1(adapter, mode);
		break;

	default:
		break;
	}
#endif
}

static void hw_var_hw_port_cfg(_adapter *adapter, u8 enable)
{
	if (enable)
		hw_bcn_ctrl_add(adapter, get_hw_port(adapter), (BIT_P0_EN_RXBCN_RPT | BIT_DIS_TSF_UDT | BIT_EN_BCN_FUNCTION));
	else
		hw_bcn_ctrl_clr(adapter, get_hw_port(adapter), BIT_EN_BCN_FUNCTION);
}

static void hw_var_set_bcn_func(PADAPTER adapter, u8 enable)
{
	u8 val8 = 0;

	if (enable) {
		/* enable TX BCN report
		 *  Reg REG_FWHW_TXQ_CTRL_8822B[2] = 1
		 *  Reg REG_BCN_CTRL_8822B[3][5] = 1
		 */
		val8 = rtw_read8(adapter, REG_FWHW_TXQ_CTRL_8822B);
		val8 |= BIT_EN_BCN_TRXRPT_V1_8822B;
		rtw_write8(adapter, REG_FWHW_TXQ_CTRL_8822B, val8);

		
		switch (adapter->hw_port) {
		case HW_PORT0:
			val8 =  BIT_EN_BCN_FUNCTION_8822B | BIT_P0_EN_TXBCN_RPT_8822B;
			hw_bcn_ctrl_clr(adapter, get_hw_port(adapter), BIT_P0_EN_RXBCN_RPT_8822B);
			break;
#ifdef CONFIG_CONCURRENT_MODE
		case HW_PORT1:
			val8 =  BIT_CLI0_EN_BCN_FUNCTION_8822B;
			hw_bcn_ctrl_clr(adapter, get_hw_port(adapter), BIT_CLI0_EN_RXBCN_RPT_8822B);
			break;
		case HW_PORT2:
			val8 =  BIT_CLI1_EN_BCN_FUNCTION_8822B;
			hw_bcn_ctrl_clr(adapter, get_hw_port(adapter), BIT_CLI1_EN_RXBCN_RPT_8822B);
			break;
		case HW_PORT3:
			val8 =  BIT_CLI2_EN_BCN_FUNCTION_8822B;
			hw_bcn_ctrl_clr(adapter, get_hw_port(adapter), BIT_CLI2_EN_RXBCN_RPT_8822B);
			break;
		case HW_PORT4:
			val8 =  BIT_CLI3_EN_BCN_FUNCTION_8822B;
			hw_bcn_ctrl_clr(adapter, get_hw_port(adapter), BIT_CLI3_EN_RXBCN_RPT_8822B);
			break;
#endif /* CONFIG_CONCURRENT_MODE */
		default:
			RTW_ERR(FUNC_ADPT_FMT" Unknow hw port(%d) \n", FUNC_ADPT_ARG(adapter), adapter->hw_port);
			rtw_warn_on(1);
			break;

		}
		hw_bcn_ctrl_add(adapter, get_hw_port(adapter), val8);
	} else {

		switch (adapter->hw_port) {
		case HW_PORT0:
			val8 =  BIT_EN_BCN_FUNCTION_8822B | BIT_P0_EN_TXBCN_RPT_8822B;
#ifdef CONFIG_BT_COEXIST
			/* Always enable port0 beacon function for PSTDMA */
			if (GET_HAL_DATA(adapter)->EEPROMBluetoothCoexist)
				val8 = BIT_P0_EN_TXBCN_RPT_8822B;
#endif /* CONFIG_BT_COEXIST */
			break;
#ifdef CONFIG_CONCURRENT_MODE
		case HW_PORT1:
			val8 =  BIT_CLI0_EN_BCN_FUNCTION_8822B;
			break;
		case HW_PORT2:
			val8 =  BIT_CLI1_EN_BCN_FUNCTION_8822B;
			break;
		case HW_PORT3:
			val8 =  BIT_CLI2_EN_BCN_FUNCTION_8822B;
			break;
		case HW_PORT4:
			val8 =  BIT_CLI3_EN_BCN_FUNCTION_8822B;
			break;
#endif /* CONFIG_CONCURRENT_MODE */
		default:
			RTW_ERR(FUNC_ADPT_FMT" Unknow hw port(%d) \n", FUNC_ADPT_ARG(adapter), adapter->hw_port);
			rtw_warn_on(1);
			break;
		}

		hw_bcn_ctrl_clr(adapter, get_hw_port(adapter), val8);
	}
}

static void hw_var_set_mlme_disconnect(PADAPTER adapter)
{
	u8 val8;
	struct mi_state mstate;

#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_mi_check_status(adapter, MI_LINKED) == _FALSE)
#endif
		/* reject all data frames under not link state */
		rtw_write16(adapter, REG_RXFLTMAP2_8822B, 0);

#ifdef CONFIG_CONCURRENT_MODE
	if (adapter->hw_port == HW_PORT1) {
		/* reset TSF1(CLINT0) */
		rtw_write8(adapter, REG_DUAL_TSF_RST_8822B, BIT_TSFTR_CLI0_RST_8822B);

		/* disable update TSF1(CLINT0) */
		rtw_iface_disable_tsf_update(adapter);

		/* disable Port1's beacon function */
		val8 = rtw_read8(adapter, REG_BCN_CTRL_CLINT0_8822B);
		val8 &= ~BIT_CLI0_EN_BCN_FUNCTION_8822B;
		rtw_write8(adapter, REG_BCN_CTRL_CLINT0_8822B, val8);
	} else
#endif
	{
		/* reset TSF */
		rtw_write8(adapter, REG_DUAL_TSF_RST_8822B, BIT_TSFTR_RST_8822B);

		/* disable update TSF */
		rtw_iface_disable_tsf_update(adapter);
	}

	rtw_mi_status_no_self(adapter, &mstate);

	/* clear update TSF only BSSID match for no linked station */
	if (MSTATE_STA_LD_NUM(&mstate) == 0 && MSTATE_STA_LG_NUM(&mstate) == 0)
		rtl8822b_rx_tsf_addr_filter_config(adapter, 0);

#ifdef CONFIG_CLIENT_PORT_CFG
	if (MLME_IS_STA(adapter))
		rtw_hw_client_port_clr(adapter);
#endif

}

static void hw_var_set_mlme_sitesurvey(PADAPTER adapter, u8 enable)
{
	struct dvobj_priv *dvobj;
	PHAL_DATA_TYPE hal;
	struct mlme_priv *pmlmepriv;
	PADAPTER iface;
	u32 reg_bcn_ctl;
	u16 value_rxfltmap2;
	u8 val8, i;


	dvobj = adapter_to_dvobj(adapter);
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
		 * 2. config RCR to receive different BSSID BCN or probe rsp
		 */

		rtw_write16(adapter, REG_RXFLTMAP2_8822B, value_rxfltmap2);

		rtw_hal_rcr_set_chk_bssid(adapter, MLME_SCAN_ENTER);

		if (rtw_mi_get_ap_num(adapter) || rtw_mi_get_mesh_num(adapter))
			StopTxBeacon(adapter);
	} else {
		/* sitesurvey done
		 * 1. enable rx data frame
		 * 2. config RCR not to receive different BSSID BCN or probe rsp
		 */

		if (rtw_mi_check_fwstate(adapter, _FW_LINKED | WIFI_AP_STATE | WIFI_MESH_STATE))
			/* enable to rx data frame */
			rtw_write16(adapter, REG_RXFLTMAP2_8822B, 0xFFFF);

		rtw_hal_rcr_set_chk_bssid(adapter, MLME_SCAN_DONE);

		if (rtw_mi_get_ap_num(adapter) || rtw_mi_get_mesh_num(adapter)) {
			ResumeTxBeacon(adapter);
			rtw_mi_tx_beacon_hdl(adapter);
		}
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
		rtw_write16(adapter, REG_RXFLTMAP2_8822B, 0xFFFF);

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
			RetryLimit = (hal->CustomerID == RT_CID_CCX) ? RL_VAL_AP : RL_VAL_STA;
		else /* Ad-hoc Mode */
			RetryLimit = RL_VAL_AP;

		/*
		 * for 8822B, must enable BCN function if BIT_CBSSID_BCN_8822B(bit 7) of REG_RCR(0x608) is enable to recv BSSID bcn
		 */
		hw_var_set_bcn_func(adapter, _TRUE);

		/* update TSF only BSSID match for station mode */
		rtl8822b_rx_tsf_addr_filter_config(adapter, BIT_CHK_TSF_EN_8822B | BIT_CHK_TSF_CBSSID_8822B);
		#ifdef CONFIG_CLIENT_PORT_CFG
		rtw_hw_client_port_cfg(adapter);
		#endif

		rtw_iface_enable_tsf_update(adapter);

	} else if (type == 1) {
		/* joinbss_event call back when join res < 0 */
		if (rtw_mi_check_status(adapter, MI_LINKED) == _FALSE)
			rtw_write16(adapter, REG_RXFLTMAP2_8822B, 0x00);

		rtw_iface_disable_tsf_update(adapter);

		if (rtw_mi_get_ap_num(adapter) || rtw_mi_get_mesh_num(adapter)) {
			ResumeTxBeacon(adapter);

			/* reset TSF 1/2 after resume_tx_beacon */
			val8 = BIT_TSFTR_RST_8822B | BIT_TSFTR_CLI0_RST_8822B;
			rtw_write8(adapter, REG_DUAL_TSF_RST_8822B, val8);
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
			rtw_write8(adapter, REG_DUAL_TSF_RST_8822B, BIT_TSFTR_RST_8822B | BIT_TSFTR_CLI0_RST_8822B);
		}
	}

	val16 = BIT_LRL_8822B(RetryLimit) | BIT_SRL_8822B(RetryLimit);
	rtw_write16(adapter, REG_RETRY_LIMIT_8822B, val16);
#else /* !CONFIG_CONCURRENT_MODE */
	if (type == 0) {
		/* prepare to join */

		/* enable to rx data frame. Accept all data frame */
		rtw_write16(adapter, REG_RXFLTMAP2_8822B, 0xFFFF);

		/*
		 * for 8822B, must enable BCN function if BIT_CBSSID_BCN_8822B(bit 7) of REG_RCR(0x608) is enabled to recv BSSID bcn
		 */
		hw_var_set_bcn_func(adapter, _TRUE);

		/* update TSF only BSSID match for station mode */
		rtl8822b_rx_tsf_addr_filter_config(adapter, BIT_CHK_TSF_EN_8822B | BIT_CHK_TSF_CBSSID_8822B);

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
			RetryLimit = (hal->CustomerID == RT_CID_CCX) ? RL_VAL_AP : RL_VAL_STA;
		else /* Ad-hoc Mode */
			RetryLimit = RL_VAL_AP;

		rtw_iface_enable_tsf_update(adapter);

	} else if (type == 1) {
		/* joinbss_event call back when join res < 0 */
		rtw_write16(adapter, REG_RXFLTMAP2_8822B, 0x00);

		rtw_iface_disable_tsf_update(adapter);

	} else if (type == 2) {
		/* sta add event callback */
		if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE))
			RetryLimit = RL_VAL_AP;
	}

	val16 = BIT_LRL_8822B(RetryLimit) | BIT_SRL_8822B(RetryLimit);
	rtw_write16(adapter, REG_RETRY_LIMIT_8822B, val16);
#endif /* !CONFIG_CONCURRENT_MODE */
}

static void hw_var_set_acm_ctrl(PADAPTER adapter, u8 ctrl)
{
	u8 hwctrl = 0;

	if (ctrl) {
		hwctrl |= BIT_ACMHWEN_8822B;

		if (ctrl & BIT(1)) /* BE */
			hwctrl |= BIT_BEQ_ACM_EN_8822B;
		else
			hwctrl &= (~BIT_BEQ_ACM_EN_8822B);

		if (ctrl & BIT(2)) /* VI */
			hwctrl |= BIT_VIQ_ACM_EN_8822B;
		else
			hwctrl &= (~BIT_VIQ_ACM_EN_8822B);

		if (ctrl & BIT(3)) /* VO */
			hwctrl |= BIT_VOQ_ACM_EN_8822B;
		else
			hwctrl &= (~BIT_VOQ_ACM_EN_8822B);
	}

	RTW_INFO("[HW_VAR_ACM_CTRL] Write 0x%02X\n", hwctrl);
	rtw_write8(adapter, REG_ACMHWCTRL_8822B, hwctrl);
}

static void hw_var_set_sec_dk_cfg(PADAPTER adapter, u8 enable)
{
	struct security_priv *sec = &adapter->securitypriv;
	u8 reg_scr = rtw_read8(adapter, REG_SECCFG_8822B);

	if (enable) {
		/* Enable default key related setting */
		reg_scr |= BIT_TXBCUSEDK_8822B;
		if (sec->dot11AuthAlgrthm != dot11AuthAlgrthm_8021X)
			reg_scr |= BIT_RXUHUSEDK_8822B | BIT_TXUHUSEDK_8822B;
	} else {
		/* Disable default key related setting */
		reg_scr &= ~(BIT_RXBCUSEDK_8822B | BIT_TXBCUSEDK_8822B | BIT_RXUHUSEDK_8822B | BIT_TXUHUSEDK_8822B);
	}

	rtw_write8(adapter, REG_SECCFG_8822B, reg_scr);

	RTW_INFO("%s: [HW_VAR_SEC_DK_CFG] 0x%x=0x%08x\n", __FUNCTION__,
		 REG_SECCFG_8822B, rtw_read32(adapter, REG_SECCFG_8822B));
}

static void hw_var_set_bcn_valid(PADAPTER adapter)
{
	u8 val8 = 0;

	/* only port 0 can TX BCN */
	val8 = rtw_read8(adapter, REG_FIFOPAGE_CTRL_2_8822B + 1);
	val8 = val8 | BIT(7);
	rtw_write8(adapter, REG_FIFOPAGE_CTRL_2_8822B + 1, val8);
}

static void hw_var_set_ack_preamble(PADAPTER adapter, u8 bShortPreamble)
{
	u8 val8 = 0;


	val8 = rtw_read8(adapter, REG_WMAC_TRXPTCL_CTL_8822B + 2);
	val8 |= BIT(4) | BIT(5);

	if (bShortPreamble)
		val8 |= BIT1;
	else
		val8 &= (~BIT1);

	rtw_write8(adapter, REG_WMAC_TRXPTCL_CTL_8822B + 2, val8);
}

void hw_var_set_dl_rsvd_page(PADAPTER adapter, u8 mstatus)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	u8 bcn_valid = _FALSE;
	u8 DLBcnCount = 0;
	u32 poll = 0;
	u8 val8;
	u8 restore[2];
	u8 hw_port = rtw_hal_get_port(adapter);

	RTW_INFO(FUNC_ADPT_FMT ":+ hw_port=%d mstatus(%x)\n",
		 FUNC_ADPT_ARG(adapter), hw_port, mstatus);

	if (mstatus == RT_MEDIA_CONNECT) {
#if 0
		u8 bRecover = _FALSE;
#endif
		u8 v8;

		/* We should set AID, correct TSF, HW seq enable before set JoinBssReport to Fw in 8822B. */
		rtw_write16(adapter, port_cfg[hw_port].ps_aid, (0xF800 | pmlmeinfo->aid));

		/* Enable SW TX beacon */
		v8 = rtw_read8(adapter, REG_CR_8822B + 1);
		restore[0] = v8;
		v8 |= (BIT_ENSWBCN_8822B >> 8);
		rtw_write8(adapter, REG_CR_8822B + 1, v8);

		/*
		 * Disable Hw protection for a time which revserd for Hw sending beacon.
		 * Fix download reserved page packet fail that access collision with the protection time.
		 */
		val8 = rtw_read8(adapter, REG_BCN_CTRL_8822B);
		restore[1] = val8;
		val8 &= ~BIT_EN_BCN_FUNCTION_8822B;
		val8 |= BIT_DIS_TSF_UDT_8822B;
		rtw_write8(adapter, REG_BCN_CTRL_8822B, val8);

#if 0
		/* Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame. */
		RegFwHwTxQCtrl = rtw_read8(adapter, REG_FWHW_TXQ_CTRL_8822B + 2);

		if (RegFwHwTxQCtrl & BIT(6))
			bRecover = _TRUE;

		/* To tell Hw the packet is not a real beacon frame. */
		RegFwHwTxQCtrl &= ~BIT(6);
		rtw_write8(adapter, REG_FWHW_TXQ_CTRL_8822B + 2, RegFwHwTxQCtrl);
#endif

		/* Clear beacon valid check bit. */
		rtw_hal_set_hwreg(adapter, HW_VAR_BCN_VALID, NULL);
		rtw_hal_set_hwreg(adapter, HW_VAR_DL_BCN_SEL, NULL);

		DLBcnCount = 0;
		poll = 0;
		do {
			/* download rsvd page. */
			rtw_hal_set_fw_rsvd_page(adapter, _FALSE);
			DLBcnCount++;
			do {
				rtw_yield_os();

				/* check rsvd page download OK. */
				rtw_hal_get_hwreg(adapter, HW_VAR_BCN_VALID, (u8 *)&bcn_valid);
				poll++;
			} while (!bcn_valid && (poll % 10) != 0 && !RTW_CANNOT_RUN(adapter));

		} while (!bcn_valid && DLBcnCount <= 100 && !RTW_CANNOT_RUN(adapter));

		if (RTW_CANNOT_RUN(adapter))
			;
		else if (!bcn_valid)
			RTW_INFO(FUNC_ADPT_FMT ": DL RSVD page failed! DLBcnCount:%u, poll:%u\n",
				 FUNC_ADPT_ARG(adapter), DLBcnCount, poll);
		else {
			struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);

			pwrctl->fw_psmode_iface_id = adapter->iface_id;
			rtw_hal_set_fw_rsvd_page(adapter, _TRUE);
			RTW_INFO(ADPT_FMT ": DL RSVD page success! DLBcnCount:%u, poll:%u\n",
				 ADPT_ARG(adapter), DLBcnCount, poll);
		}

		rtw_write8(adapter, REG_BCN_CTRL, restore[1]);
		rtw_write8(adapter,  REG_CR + 1, restore[0]);
#if 0
		/*
		 * To make sure that if there exists an adapter which would like to send beacon.
		 * If exists, the origianl value of 0x422[6] will be 1, we should check this to
		 * prevent from setting 0x422[6] to 0 after download reserved page, or it will cause
		 * the beacon cannot be sent by HW.
		 */
		if (bRecover) {
			RegFwHwTxQCtrl |= BIT(6);
			rtw_write8(adapter, REG_FWHW_TXQ_CTRL_8822B + 2, RegFwHwTxQCtrl);
		}
#endif
#ifndef CONFIG_PCI_HCI
		/* Clear CR[8] or beacon packet will not be send to TxBuf anymore. */
		v8 = rtw_read8(adapter, REG_CR_8822B + 1);
		v8 &= ~BIT(0); /* ~ENSWBCN */
		rtw_write8(adapter, REG_CR_8822B + 1, v8);
#endif /* !CONFIG_PCI_HCI */
	}
}

static void hw_var_set_h2c_fw_joinbssrpt(PADAPTER adapter, u8 mstatus)
{
	if (mstatus == RT_MEDIA_CONNECT)
		hw_var_set_dl_rsvd_page(adapter, RT_MEDIA_CONNECT);
}

/*
 * Parameters:
 *	adapter
 *	enable		_TRUE: enable; _FALSE: disable
 */
static u8 rx_agg_switch(PADAPTER adapter, u8 enable)
{
	int err;

	err = rtw_halmac_rx_agg_switch(adapter_to_dvobj(adapter), enable);
	if (err)
		return _FAIL;

	return _SUCCESS;
}


#ifdef CONFIG_AP_PORT_SWAP
/*
 * Parameters:
 *	if_ap		ap interface
 *	if_port0		port0 interface
 */

static void hw_port_reconfig(_adapter * if_ap, _adapter *if_port0)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(if_port0);
	struct mlme_ext_priv *pmlmeext = &if_port0->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	u32 bssid_offset = 0;
	u8 bssid[6] = {0};
	u8 vnet_type = 0;
	u8 vbcn_ctrl = 0;
	u8 i;
	u8 port = if_ap->hw_port;

	if (port > (hal_spec->port_num - 1)) {
		RTW_INFO("[WARN] "ADPT_FMT"- hw_port : %d,will switch to invalid port-%d\n",
			 ADPT_ARG(if_port0), if_port0->hw_port, port);
		rtw_warn_on(1);
	}

	RTW_PRINT(ADPT_FMT" - hw_port : %d,will switch to port-%d\n",
		  ADPT_ARG(if_port0), if_port0->hw_port, port);

	/*backup*/
	GetHwReg(if_port0, HW_VAR_MEDIA_STATUS, &vnet_type);
	vbcn_ctrl = rtw_read8(if_port0, port_cfg[if_port0->hw_port].bcn_ctl);

	if (is_client_associated_to_ap(if_port0)) {
		RTW_INFO("port0-iface("ADPT_FMT") is STA mode and linked\n", ADPT_ARG(if_port0));
		bssid_offset = port_cfg[if_port0->hw_port].bssid;
		for (i = 0; i < 6; i++)
			bssid[i] = rtw_read8(if_port0, bssid_offset + i);
	}

	/*reconfigure*/
	if_port0->hw_port = port;
	/* adapter mac addr switch to port mac addr */
	rtw_hal_set_hwreg(if_port0, HW_VAR_MAC_ADDR, adapter_mac_addr(if_port0));
	Set_MSR(if_port0, vnet_type);
	rtw_write8(if_port0, port_cfg[if_port0->hw_port].bcn_ctl, vbcn_ctrl);

	if (is_client_associated_to_ap(if_port0)) {
		rtw_hal_set_hwreg(if_port0, HW_VAR_BSSID, bssid);
		#ifdef CONFIG_FW_MULTI_PORT_SUPPORT
		rtw_set_default_port_id(if_port0);
		#endif
	}

#if defined(CONFIG_BT_COEXIST) && defined(CONFIG_FW_MULTI_PORT_SUPPORT)
	if (GET_HAL_DATA(if_port0)->EEPROMBluetoothCoexist == _TRUE)
		rtw_hal_set_wifi_btc_port_id_cmd(if_port0);
#endif

	if_ap->hw_port =HW_PORT0;
	/* port mac addr switch to adapter mac addr */
	rtw_hal_set_hwreg(if_ap, HW_VAR_MAC_ADDR, adapter_mac_addr(if_ap));
}

static void hw_var_ap_port_switch(_adapter *adapter, u8 mode)
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

	if (hw_port == HW_PORT0)
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
	/* if_port0 switch to hw_port */
	hw_port_reconfig(adapter, if_port0);
	RTW_INFO(ADPT_FMT ": Cfg SoftAP mode to hw_port(%d) done\n", ADPT_ARG(adapter), adapter->hw_port);

}
#endif

u8 rtl8822b_sethwreg(PADAPTER adapter, u8 variable, u8 *val)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	u8 ret = _SUCCESS;
	u8 val8;
	u16 val16;
	u32 val32;


	switch (variable) {
	case HW_VAR_SET_OPMODE:
		hw_var_set_opmode(adapter, *val);
		break;
/*
	case HW_VAR_INIT_RTS_RATE:
		break;
*/
	case HW_VAR_BASIC_RATE:
		rtw_var_set_basic_rate(adapter, val);
		break;

	case HW_VAR_TXPAUSE:
		rtw_write8(adapter, REG_TXPAUSE_8822B, *val);
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

	case HW_VAR_RCR:
		ret = rtl8822b_rcr_config(adapter, *((u32 *)val));
		break;

	case HW_VAR_SLOT_TIME:
		rtw_write8(adapter, REG_SLOT_8822B, *val);
		break;

	case HW_VAR_RESP_SIFS:
		/* RESP_SIFS for CCK */
		rtw_write8(adapter, REG_RESP_SIFS_CCK_8822B, val[0]);
		rtw_write8(adapter, REG_RESP_SIFS_CCK_8822B + 1, val[1]);
		/* RESP_SIFS for OFDM */
		rtw_write8(adapter, REG_RESP_SIFS_OFDM_8822B, val[2]);
		rtw_write8(adapter, REG_RESP_SIFS_OFDM_8822B + 1, val[3]);
		break;

	case HW_VAR_ACK_PREAMBLE:
		hw_var_set_ack_preamble(adapter, *val);
		break;

/*
	case HW_VAR_SEC_CFG:
		follow hal_com.c
		break;
*/

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
		val32 = BIT_SECCAM_POLLING_8822B | BIT_SECCAM_CLR_8822B;
		rtw_write32(adapter, REG_CAMCMD_8822B, val32);
		break;

	case HW_VAR_AC_PARAM_VO:
		rtw_write32(adapter, REG_EDCA_VO_PARAM_8822B, *(u32 *)val);
		break;

	case HW_VAR_AC_PARAM_VI:
		rtw_write32(adapter, REG_EDCA_VI_PARAM_8822B, *(u32 *)val);
		break;

	case HW_VAR_AC_PARAM_BE:
		hal->ac_param_be = *(u32 *)val;
		rtw_write32(adapter, REG_EDCA_BE_PARAM_8822B, *(u32 *)val);
		break;

	case HW_VAR_AC_PARAM_BK:
		rtw_write32(adapter, REG_EDCA_BK_PARAM_8822B, *(u32 *)val);
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
		rtw_write32(adapter, REG_AMPDU_MAX_LENGTH_8822B, AMPDULen);
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
/*
	case HW_VAR_SET_RPWM:
	case HW_VAR_CPWM:
		break;
*/
	case HW_VAR_H2C_FW_PWRMODE:
		rtl8822b_set_FwPwrMode_cmd(adapter, *val);
		break;
/*
	case HW_VAR_H2C_PS_TUNE_PARAM:
		break;
*/
	case HW_VAR_H2C_INACTIVE_IPS:
#ifdef CONFIG_WOWLAN
		rtl8822b_set_fw_pwrmode_inips_cmd_wowlan(adapter, *val);
#endif /* CONFIG_WOWLAN */
		break;
	case HW_VAR_H2C_FW_JOINBSSRPT:
		hw_var_set_h2c_fw_joinbssrpt(adapter, *val);
		break;
	case HW_VAR_DL_RSVD_PAGE:
#ifdef CONFIG_BT_COEXIST
		if (check_fwstate(&adapter->mlmepriv, WIFI_AP_STATE) == _TRUE)
			rtl8822b_download_BTCoex_AP_mode_rsvd_page(adapter);
#endif
		break;
#ifdef CONFIG_P2P_PS
	case HW_VAR_H2C_FW_P2P_PS_OFFLOAD:
		#ifdef CONFIG_FW_MULTI_PORT_SUPPORT
		if (*val == P2P_PS_ENABLE)
			rtw_set_default_port_id(adapter);
		#endif
		rtw_set_p2p_ps_offload_cmd(adapter, *val);
		break;
#endif /* CONFIG_P2P_PS */
/*
	case HW_VAR_TRIGGER_GPIO_0:
	case HW_VAR_BT_SET_COEXIST:
	case HW_VAR_BT_ISSUE_DELBA:
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
		rtw_write8(adapter, REG_TXPAUSE_8822B, 0xff);

		/* keep hw sn */
		if (adapter->xmitpriv.hw_ssn_seq_no == 1)
			reg_hw_ssn = REG_HW_SEQ1_8822B;
		else if (adapter->xmitpriv.hw_ssn_seq_no == 2)
			reg_hw_ssn = REG_HW_SEQ2_8822B;
		else if (adapter->xmitpriv.hw_ssn_seq_no == 3)
			reg_hw_ssn = REG_HW_SEQ3_8822B;
		else
			reg_hw_ssn = REG_HW_SEQ0_8822B;

		adapter->xmitpriv.nqos_ssn = rtw_read16(adapter, reg_hw_ssn);

		if (pwrpriv->bkeepfwalive != _TRUE) {
			/* RX DMA stop */
			val32 = rtw_read32(adapter, REG_RXPKT_NUM_8822B);
			val32 |= BIT_RW_RELEASE_EN;
			rtw_write32(adapter, REG_RXPKT_NUM_8822B, val32);
			do {
				val32 = rtw_read32(adapter, REG_RXPKT_NUM_8822B);
				val32 &= BIT_RXDMA_IDLE_8822B;
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

	case HW_VAR_RESTORE_HW_SEQ:
		{
			/* restore Sequence No. */
			u32 reg_hw_ssn;

			if (adapter->xmitpriv.hw_ssn_seq_no == 1)
				reg_hw_ssn = REG_HW_SEQ1_8822B;
			else if (adapter->xmitpriv.hw_ssn_seq_no == 2)
				reg_hw_ssn = REG_HW_SEQ2_8822B;
			else if (adapter->xmitpriv.hw_ssn_seq_no == 3)
				reg_hw_ssn = REG_HW_SEQ3_8822B;
			else
				reg_hw_ssn = REG_HW_SEQ0_8822B;

			rtw_write8(adapter, reg_hw_ssn, adapter->xmitpriv.nqos_ssn);
		}
		break;

	case HW_VAR_CHECK_TXBUF: {
		u16 rtylmtorg;
		u8 RetryLimit = 0x01;
		systime start;
		u32 passtime;
		u32 timelmt = 2000;	/* ms */
		int err;
		u8 empty;


		rtylmtorg = rtw_read16(adapter, REG_RETRY_LIMIT_8822B);

		val16 = BIT_LRL_8822B(RetryLimit) | BIT_SRL_8822B(RetryLimit);
		rtw_write16(adapter, REG_RETRY_LIMIT_8822B, val16);

		/* Check TX FIFO empty or not */
		empty = _FALSE;
		start = rtw_get_current_time();
		err = rtw_halmac_txfifo_wait_empty(adapter_to_dvobj(adapter), timelmt);
		if (!err)
			empty = _TRUE;
		passtime = rtw_get_passing_time_ms(start);

		if (_TRUE == empty)
			RTW_INFO("[HW_VAR_CHECK_TXBUF] Empty in %d ms\n", passtime);
		else if (RTW_CANNOT_RUN(adapter))
			RTW_WARN("[HW_VAR_CHECK_TXBUF] bDriverStopped or bSurpriseRemoved\n");
		else {
			RTW_ERR("[HW_VAR_CHECK_TXBUF] NOT empty in %d ms\n", passtime);

		}
		rtw_write16(adapter, REG_RETRY_LIMIT_8822B, rtylmtorg);
	}
	break;
/*
	case HW_VAR_PCIE_STOP_TX_DMA:
	case HW_VAR_APFM_ON_MAC
	case HW_VAR_HCI_SUS_STATE:
#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
	case HW_VAR_WOWLAN:
	case HW_VAR_WAKEUP_REASON:
#endif
	case HW_VAR_RPWM_TOG:
		break;
*/
#ifdef CONFIG_GPIO_WAKEUP
	case HW_SET_GPIO_WL_CTRL: {
		u8 enable = *val;
		u8 value = 0;
		u8 addr = REG_PAD_CTRL1_8822B + 3;

		if (WAKEUP_GPIO_IDX == 6) {
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
/*
	case HW_VAR_SYS_CLKR:
		break;
*/
	case HW_VAR_NAV_UPPER: {
#define HAL_NAV_UPPER_UNIT	128	/* micro-second */
		u32 usNavUpper = *(u32 *)val;

		if (usNavUpper > HAL_NAV_UPPER_UNIT * 0xFF) {
			RTW_INFO(FUNC_ADPT_FMT ": [HW_VAR_NAV_UPPER] value(0x%08X us) is larger than (%d * 0xFF)!!!\n",
				FUNC_ADPT_ARG(adapter), usNavUpper, HAL_NAV_UPPER_UNIT);
			break;
		}

		usNavUpper = (usNavUpper + HAL_NAV_UPPER_UNIT - 1) / HAL_NAV_UPPER_UNIT;
		rtw_write8(adapter, REG_NAV_CTRL_8822B + 2, (u8)usNavUpper);
	}
	break;

/*
	case HW_VAR_RPT_TIMER_SETTING:
	case HW_VAR_TX_RPT_MAX_MACID:
	case HW_VAR_CHK_HI_QUEUE_EMPTY:
	case HW_VAR_AMPDU_MAX_TIME:
	case HW_VAR_WIRELESS_MODE:
	case HW_VAR_USB_MODE:
		break;
*/
#ifdef CONFIG_AP_PORT_SWAP
	case HW_VAR_PORT_SWITCH:
		{
			u8 mode = *((u8 *)val);

			hw_var_ap_port_switch(adapter, mode);
		}
		break;
#endif

#ifdef CONFIG_BEAMFORMING
	case HW_VAR_SOUNDING_ENTER:
		rtl8822b_phy_bf_enter(adapter, (struct sta_info*)val);
		break;

	case HW_VAR_SOUNDING_LEAVE:
		rtl8822b_phy_bf_leave(adapter, val);
		break;
/*
	case HW_VAR_SOUNDING_RATE:
		break;
*/
	case HW_VAR_SOUNDING_STATUS:
		rtl8822b_phy_bf_sounding_status(adapter, *val);
		break;
/*
	case HW_VAR_SOUNDING_FW_NDPA:
	case HW_VAR_SOUNDING_CLK:
		break;
*/
	case HW_VAR_SOUNDING_SET_GID_TABLE:
		rtl8822b_phy_bf_set_gid_table(adapter, (struct beamformer_entry*)val);
		break;

	case HW_VAR_SOUNDING_CSI_REPORT:
		rtl8822b_phy_bf_set_csi_report(adapter, (struct _RT_CSI_INFO*)val);
		break;
#endif /* CONFIG_BEAMFORMING */
/*
	case HW_VAR_HW_REG_TIMER_INIT:
	case HW_VAR_HW_REG_TIMER_RESTART:
	case HW_VAR_HW_REG_TIMER_START:
	case HW_VAR_HW_REG_TIMER_STOP:
		break;
*/

/*
	case HW_VAR_MACID_LINK:
	case HW_VAR_MACID_NOLINK:
	case HW_VAR_DUMP_MAC_QUEUE_INFO:
	case HW_VAR_ASIX_IOT:
	case HW_VAR_EN_HW_UPDATE_TSF:
	case HW_VAR_CH_SW_NEED_TO_TAKE_CARE_IQK_INFO:
	case HW_VAR_CH_SW_IQK_INFO_BACKUP:
	case HW_VAR_CH_SW_IQK_INFO_RESTORE:
		break;
*/
#ifdef CONFIG_TDLS
#ifdef CONFIG_TDLS_CH_SW
	case HW_VAR_TDLS_BCN_EARLY_C2H_RPT:
		rtl8822b_set_BcnEarly_C2H_Rpt_cmd(adapter, *val);
		break;
#endif
#endif

	case HW_VAR_FREECNT:

		val8 = (u8)*val;

		if (val8==0) {
			/* disable free run counter set 0x577[3]=0 */
			rtw_write8(adapter, REG_MISC_CTRL,
				rtw_read8(adapter, REG_MISC_CTRL)&(~BIT_EN_FREECNT));

			/* reset FREE_RUN_COUNTER set 0x553[5]=1 */
			val8 = rtw_read8(adapter, REG_DUAL_TSF_RST);
			val8 |=  BIT_FREECNT_RST;
			rtw_write8(adapter, REG_DUAL_TSF_RST, val8);

		} else if (val8==1){

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

	case HW_VAR_SET_SOML_PARAM:
#ifdef CONFIG_DYNAMIC_SOML
		rtw_dyn_soml_para_set(adapter, 4, 20, 1, 0);
#endif
		break;

	default:
		ret = SetHwReg(adapter, variable, val);
		break;
	}

	return ret;
}

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

	q0_info = rtw_read32(adapter, REG_Q0_INFO_8822B);
	q1_info = rtw_read32(adapter, REG_Q1_INFO_8822B);
	q2_info = rtw_read32(adapter, REG_Q2_INFO_8822B);
	q3_info = rtw_read32(adapter, REG_Q3_INFO_8822B);
	q4_info = rtw_read32(adapter, REG_Q4_INFO_8822B);
	q5_info = rtw_read32(adapter, REG_Q5_INFO_8822B);
	q6_info = rtw_read32(adapter, REG_Q6_INFO_8822B);
	q7_info = rtw_read32(adapter, REG_Q7_INFO_8822B);
	mg_q_info = rtw_read32(adapter, REG_MGQ_INFO_8822B);
	hi_q_info = rtw_read32(adapter, REG_HIQ_INFO_8822B);
	bcn_q_info = rtw_read16(adapter, REG_BCNQ_INFO_8822B);

	q0_q1_info = rtw_read32(adapter, REG_Q0_Q1_INFO_8822B);
	q2_q3_info = rtw_read32(adapter, REG_Q2_Q3_INFO_8822B);
	q4_q5_info = rtw_read32(adapter, REG_Q4_Q5_INFO_8822B);
	q6_q7_info = rtw_read32(adapter, REG_Q6_Q7_INFO_8822B);
	mg_hi_q_info = rtw_read32(adapter, REG_MGQ_HIQ_INFO_8822B);
	cmd_bcn_q_info = rtw_read32(adapter, REG_CMDQ_BCNQ_INFO_8822B);

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

	hpq = rtw_read32(adapter, REG_FIFOPAGE_INFO_1_8822B);
	lpq = rtw_read32(adapter, REG_FIFOPAGE_INFO_2_8822B);
	npq = rtw_read32(adapter, REG_FIFOPAGE_INFO_3_8822B);
	epq = rtw_read32(adapter, REG_FIFOPAGE_INFO_4_8822B);
	pubq = rtw_read32(adapter, REG_FIFOPAGE_INFO_5_8822B);

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

static u8 hw_var_get_bcn_valid(PADAPTER adapter)
{
	u8 val8 = 0;
	u8 ret = _FALSE;

	/* only port 0 can TX BCN */
	val8 = rtw_read8(adapter, REG_FIFOPAGE_CTRL_2_8822B + 1);
	ret = (BIT(7) & val8) ? _TRUE : _FALSE;

	return ret;
}

void rtl8822b_read_wmmedca_reg(PADAPTER adapter, u16 *vo_params, u16 *vi_params, u16 *be_params, u16 *bk_params)
{
	u8 vo_reg_params[4];
	u8 vi_reg_params[4];
	u8 be_reg_params[4];
	u8 bk_reg_params[4];

	rtl8822b_gethwreg(adapter, HW_VAR_AC_PARAM_VO, vo_reg_params);
	rtl8822b_gethwreg(adapter, HW_VAR_AC_PARAM_VI, vi_reg_params);
	rtl8822b_gethwreg(adapter, HW_VAR_AC_PARAM_BE, be_reg_params);
	rtl8822b_gethwreg(adapter, HW_VAR_AC_PARAM_BK, bk_reg_params);

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

void rtl8822b_gethwreg(PADAPTER adapter, u8 variable, u8 *val)
{
	PHAL_DATA_TYPE hal;
	u8 val8;
	u16 val16;
	u32 val32;
	u64 val64;


	hal = GET_HAL_DATA(adapter);

	switch (variable) {
/*
	case HW_VAR_INIT_RTS_RATE:
	case HW_VAR_BASIC_RATE:
		break;
*/
	case HW_VAR_TXPAUSE:
		*val = rtw_read8(adapter, REG_TXPAUSE_8822B);
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
	case HW_VAR_FREECNT:
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
	case HW_VAR_SET_RPWM:
	case HW_VAR_CPWM:
	case HW_VAR_H2C_FW_PWRMODE:
	case HW_VAR_H2C_PS_TUNE_PARAM:
	case HW_VAR_H2C_FW_JOINBSSRPT:
		break;
*/
	case HW_VAR_FWLPS_RF_ON:
		/* When we halt NIC, we should check if FW LPS is leave. */
		if (rtw_is_surprise_removed(adapter) ||
		    (adapter_to_pwrctl(adapter)->rf_pwrstate == rf_off)) {
			/*
			 * If it is in HW/SW Radio OFF or IPS state,
			 * we do not check Fw LPS Leave,
			 * because Fw is unload.
			 */
			*val = _TRUE;
		} else {
			rtw_hal_get_hwreg(adapter, HW_VAR_RCR, (u8 *)&val32);

			if (adapter_to_pwrctl(adapter)->wowlan_mode == _TRUE)
				val32 &= (BIT_UC_MD_EN_8822B | BIT_BC_MD_EN_8822B);
			else
				val32 &= (BIT_UC_MD_EN_8822B | BIT_BC_MD_EN_8822B | BIT_TIM_PARSER_EN_8822B);

			if (val32)
				*val = _FALSE;
			else
				*val = _TRUE;
		}
		break;
/*
	case HW_VAR_H2C_FW_P2P_PS_OFFLOAD:
	case HW_VAR_TRIGGER_GPIO_0:
	case HW_VAR_BT_SET_COEXIST:
	case HW_VAR_BT_ISSUE_DELBA:
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

/*
	case HW_VAR_HCI_SUS_STATE:
		break;
*/
#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
/*
	case HW_VAR_WOWLAN:
		break;

	case HW_VAR_WAKEUP_REASON:
		rtw_halmac_get_wow_reason(adapter_to_dvobj(adapter), val);
		break;

	case HW_VAR_RPWM_TOG:
		break;
*/
#endif
/*
#ifdef CONFIG_GPIO_WAKEUP
	case HW_SET_GPIO_WL_CTRL:
		break;
#endif
*/
	case HW_VAR_SYS_CLKR:
		*val = rtw_read8(adapter, REG_SYS_CLK_CTRL_8822B);
		break;
/*
	case HW_VAR_NAV_UPPER:
	case HW_VAR_RPT_TIMER_SETTING:
	case HW_VAR_TX_RPT_MAX_MACID:
		break;
*/
	case HW_VAR_CHK_HI_QUEUE_EMPTY:
		val16 = rtw_read16(adapter, REG_TXPKT_EMPTY_8822B);
		*val = (val16 & BIT_HQQ_EMPTY_8822B) ? _TRUE : _FALSE;
		break;
	case HW_VAR_CHK_MGQ_CPU_EMPTY:
		val16 = rtw_read16(adapter, REG_TXPKT_EMPTY_8822B);
		*val = (val16 & BIT_MGQ_CPU_EMPTY_8822B) ? _TRUE : _FALSE;
		break;
/*
	case HW_VAR_DL_BCN_SEL:
	case HW_VAR_AMPDU_MAX_TIME:
	case HW_VAR_WIRELESS_MODE:
	case HW_VAR_USB_MODE:
	case HW_VAR_PORT_SWITCH:
	case HW_VAR_DO_IQK:
	case HW_VAR_SOUNDING_ENTER:
	case HW_VAR_SOUNDING_LEAVE:
	case HW_VAR_SOUNDING_RATE:
	case HW_VAR_SOUNDING_STATUS:
	case HW_VAR_SOUNDING_FW_NDPA:
	case HW_VAR_SOUNDING_CLK:
	case HW_VAR_HW_REG_TIMER_INIT:
	case HW_VAR_HW_REG_TIMER_RESTART:
	case HW_VAR_HW_REG_TIMER_START:
	case HW_VAR_HW_REG_TIMER_STOP:
	case HW_VAR_MACID_LINK:
	case HW_VAR_MACID_NOLINK:
		break;
*/
	case HW_VAR_FW_PS_STATE:
		/* driver read REG_SYS_CFG5 - BIT_LPS_STATUS REG_1070[3] to get hw ps state */
		*((u16 *)val) = rtw_read8(adapter, REG_SYS_CFG5);
		break;

	case HW_VAR_DUMP_MAC_QUEUE_INFO:
		dump_mac_qinfo(val, adapter);
		break;

	case HW_VAR_DUMP_MAC_TXFIFO:
		dump_mac_txfifo(val, adapter);
		break;
/*
	case HW_VAR_ASIX_IOT:
	case HW_VAR_EN_HW_UPDATE_TSF:
	case HW_VAR_CH_SW_NEED_TO_TAKE_CARE_IQK_INFO:
	case HW_VAR_CH_SW_IQK_INFO_BACKUP:
	case HW_VAR_CH_SW_IQK_INFO_RESTORE:
#ifdef CONFIG_TDLS
#ifdef CONFIG_TDLS_CH_SW
	case HW_VAR_TDLS_BCN_EARLY_C2H_RPT:
#endif
#endif
		break;
*/

	case HW_VAR_BCN_CTRL_ADDR:
		*((u32 *)val) = hw_bcn_ctrl_addr(adapter, adapter->hw_port);
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
u8 rtl8822b_sethaldefvar(PADAPTER adapter, HAL_DEF_VARIABLE variable, void *pval)
{
	PHAL_DATA_TYPE hal;
	u8 bResult;


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
	case HAL_DEF_TX_LDPC:
	case HAL_DEF_RX_LDPC:
	case HAL_DEF_TX_STBC:
	case HAL_DEF_RX_STBC:
	case HAL_DEF_EXPLICIT_BEAMFORMER:
	case HAL_DEF_EXPLICIT_BEAMFORMEE:
	case HAL_DEF_VHT_MU_BEAMFORMER:
	case HAL_DEF_VHT_MU_BEAMFORMEE:
	case HAL_DEF_BEAMFORMER_CAP:
	case HAL_DEF_BEAMFORMEE_CAP:
	case HW_VAR_MAX_RX_AMPDU_FACTOR:
	case HAL_DEF_DBG_DUMP_TXPKT:
	case HAL_DEF_TX_PAGE_SIZE:
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
void rtl8822b_ra_info_dump(_adapter *padapter, void *sel)
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
u8 rtl8822b_gethaldefvar(PADAPTER adapter, HAL_DEF_VARIABLE variable, void *pval)
{
	PHAL_DATA_TYPE hal;
	struct dvobj_priv *d;
	u8 bResult;
	u8 val8 = 0;
	u32 val32 = 0;


	d = adapter_to_dvobj(adapter);
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

/*
	case HAL_DEF_DRVINFO_SZ:
		break;
*/
	case HAL_DEF_MAX_RECVBUF_SZ:
		*((u32 *)pval) = MAX_RECVBUF_SZ;
		break;

	case HAL_DEF_RX_PACKET_OFFSET:
		val32 = rtl8822b_get_rx_desc_size(adapter);
		val8 = rtl8822b_get_rx_drv_info_size(adapter);
		*((u32 *)pval) = val32 + val8;
		break;
/*
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
	case HAL_DEF_RX_LDPC:
		*(u8 *)pval = _TRUE;
		break;

	/* support 1RX for STBC */
	case HAL_DEF_RX_STBC:
		*(u8 *)pval = 1;
		break;

	/* support Explicit TxBF for HT/VHT */
	case HAL_DEF_EXPLICIT_BEAMFORMER:
	case HAL_DEF_EXPLICIT_BEAMFORMEE:
	case HAL_DEF_VHT_MU_BEAMFORMER:
	case HAL_DEF_VHT_MU_BEAMFORMEE:
		*(u8 *)pval = _TRUE;
		break;

	case HAL_DEF_BEAMFORMER_CAP:
		val8 = GET_HAL_TX_NSS(adapter);
		*(u8 *)pval = (val8 - 1);
		break;

	case HAL_DEF_BEAMFORMEE_CAP:
		*(u8 *)pval = 3;
		break;

	case HW_VAR_MAX_RX_AMPDU_FACTOR:
		/* 8822B RX FIFO is 24KB */
		*(HT_CAP_AMPDU_FACTOR *)pval = MAX_AMPDU_FACTOR_16K;
		break;

	case HW_DEF_RA_INFO_DUMP:
		rtl8822b_ra_info_dump(adapter, pval);
		break;
/*
	case HAL_DEF_DBG_DUMP_TXPKT:
	case HAL_DEF_TX_PAGE_SIZE:
	case HAL_DEF_TX_PAGE_BOUNDARY:
	case HAL_DEF_TX_PAGE_BOUNDARY_WOWLAN:
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

void rtl8822b_fill_txdesc_sectype(struct pkt_attrib *pattrib, u8 *ptxdesc)
{
	if ((pattrib->encrypt > 0) && !pattrib->bswenc) {
		/* SEC_TYPE : 0:NO_ENC,1:WEP40/TKIP,2:WAPI,3:AES */
		switch (pattrib->encrypt) {
		case _WEP40_:
		case _WEP104_:
		case _TKIP_:
		case _TKIP_WTMIC_:
			SET_TX_DESC_SEC_TYPE_8822B(ptxdesc, 0x1);
			break;
#ifdef CONFIG_WAPI_SUPPORT
		case _SMS4_:
			SET_TX_DESC_SEC_TYPE_8822B(ptxdesc, 0x2);
			break;
#endif
		case _AES_:
			SET_TX_DESC_SEC_TYPE_8822B(ptxdesc, 0x3);
			break;
		case _NO_PRIVACY_:
		default:
			SET_TX_DESC_SEC_TYPE_8822B(ptxdesc, 0x0);
			break;
		}
	}
}

void rtl8822b_fill_txdesc_vcs(PADAPTER adapter, struct pkt_attrib *pattrib, u8 *ptxdesc)
{
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);


	if (pattrib->vcs_mode) {
		switch (pattrib->vcs_mode) {
		case RTS_CTS:
			SET_TX_DESC_RTSEN_8822B(ptxdesc, 1);
			break;
		case CTS_TO_SELF:
			SET_TX_DESC_CTS2SELF_8822B(ptxdesc, 1);
			break;
		case NONE_VCS:
		default:
			break;
		}

		if (pmlmeinfo->preamble_mode == PREAMBLE_SHORT)
			SET_TX_DESC_RTS_SHORT_8822B(ptxdesc, 1);

		/* RTS Rate=24M */
		SET_TX_DESC_RTSRATE_8822B(ptxdesc, 0x8);

		/* compatibility for MCC consideration, use pmlmeext->cur_channel */
		if (pmlmeext->cur_channel > 14)
			/* RTS retry to rate OFDM 6M for 5G */
			SET_TX_DESC_RTS_RTY_LOWEST_RATE_8822B(ptxdesc, 4);
		else
			/* RTS retry to rate CCK 1M for 2.4G */
			SET_TX_DESC_RTS_RTY_LOWEST_RATE_8822B(ptxdesc, 0);
	}
}

u8 rtl8822b_bw_mapping(PADAPTER adapter, struct pkt_attrib *pattrib)
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

u8 rtl8822b_sc_mapping(PADAPTER adapter, struct pkt_attrib *pattrib)
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

void rtl8822b_fill_txdesc_phy(PADAPTER adapter, struct pkt_attrib *pattrib, u8 *ptxdesc)
{
	if (pattrib->ht_en) {
		/* Set Bandwidth and sub-channel settings. */
		SET_TX_DESC_DATA_BW_8822B(ptxdesc, rtl8822b_bw_mapping(adapter, pattrib));
		SET_TX_DESC_DATA_SC_8822B(ptxdesc, rtl8822b_sc_mapping(adapter, pattrib));
	}
}

/**
 * rtl8822b_fill_txdesc_tx_rate() - Set rate in tx description
 * @adapter	struct _ADAPTER*
 * @attrib	packet attribute
 * @rate	DESC_RATE*
 * @shrt	1/0 means short/long PLCP for CCK, short/long GI for HT/VHT
 * @fallback	enable rate fallback or not
 * @desc	buffer of tx description
 *
 * Fill rate related fields of tx description when driver want to use specific
 * data rate to send this packet.
 */
static void rtl8822b_fill_txdesc_tx_rate(struct _ADAPTER *adapter,
				struct pkt_attrib *attrib,
				u8 rate, u8 shrt, u8 fallback, u8 *desc)
{
	u8 disfb;
	u8 bw;


	rate = rate & 0x7F;
	shrt = shrt ? 1 : 0;
	disfb = fallback ? 0 : 1;

	SET_TX_DESC_USE_RATE_8822B(desc, 1);
	SET_TX_DESC_DATARATE_8822B(desc, rate);
	SET_TX_DESC_DATA_SHORT_8822B(desc, shrt);
	SET_TX_DESC_DISDATAFB_8822B(desc, disfb);

	/* HT MCS rate can't support bandwidth higher than 40MHz */
	bw = GET_TX_DESC_DATA_BW_8822B(desc);
	if (((rate >= DESC_RATEMCS0) && (rate <= DESC_RATEMCS31)) && (bw > 1)) {
		RTW_WARN(FUNC_ADPT_FMT ": Use HT rate(%s) on bandwidth "
			 "higher than 40MHz(%u>%u) is illegal, "
			 "switch bandwidth to 40MHz!\n",
			 FUNC_ADPT_ARG(adapter),
			 HDATA_RATE(rate), attrib->bwmode,
			 CHANNEL_WIDTH_40);

		if (attrib->bwmode > CHANNEL_WIDTH_40)
			attrib->bwmode = CHANNEL_WIDTH_40;
		rtl8822b_fill_txdesc_phy(adapter, attrib, desc);
	}
}

#ifdef CONFIG_CONCURRENT_MODE
void rtl8822b_fill_txdesc_force_bmc_camid(struct pkt_attrib *pattrib, u8 *ptxdesc)
{
	if ((pattrib->encrypt > 0) && (!pattrib->bswenc)
	    && (pattrib->bmc_camid != INVALID_SEC_MAC_CAM_ID)) {
		SET_TX_DESC_EN_DESC_ID_8822B(ptxdesc, 1);
		SET_TX_DESC_MACID_8822B(ptxdesc, pattrib->bmc_camid);
	}
}
#endif

void rtl8822b_fill_txdesc_bmc_tx_rate(struct pkt_attrib *pattrib, u8 *ptxdesc)
{
	SET_TX_DESC_USE_RATE_8822B(ptxdesc, 1);
	SET_TX_DESC_DATARATE_8822B(ptxdesc, MRateToHwRate(pattrib->rate));
	SET_TX_DESC_DISDATAFB_8822B(ptxdesc, 1);
}

/*
 * Description:
 *	Fill tx description for beamforming packets
 */
void rtl8822b_fill_txdesc_bf(struct xmit_frame *frame, u8 *desc)
{
#ifndef CONFIG_BEAMFORMING
	return;
#else /* CONFIG_BEAMFORMING */
	struct pkt_attrib *attrib;


	attrib = &frame->attrib;

	SET_TX_DESC_G_ID_8822B(desc, attrib->txbf_g_id);
	SET_TX_DESC_P_AID_8822B(desc, attrib->txbf_p_aid);

	SET_TX_DESC_MU_DATARATE_8822B(desc, MRateToHwRate(attrib->rate));
	/*SET_TX_DESC_MU_RC_8822B(desc, 0);*/

	/* Force to disable STBC when txbf is enabled */
	if (attrib->txbf_p_aid && attrib->stbc)
		SET_TX_DESC_DATA_STBC_8822B(desc, 0);
#endif /* CONFIG_BEAMFORMING */
}

/*
 * Description:
 *	Fill tx description for beamformer,
 *	include following management packets:
 *	1. VHT NDPA
 *	2. HT NDPA
 *	3. Beamforming Report Poll
 */
void rtl8822b_fill_txdesc_mgnt_bf(struct xmit_frame *frame, u8 *desc)
{
#ifndef CONFIG_BEAMFORMING
	return;
#else /* CONFIG_BEAMFORMING */
	PADAPTER adapter;
	struct pkt_attrib *attrib;
	u8 ndpa = 0;
	u8 ht_ndpa = 0;
	u8 report_poll = 0;


	adapter = frame->padapter;
	attrib = &frame->attrib;

	if (attrib->subtype == WIFI_NDPA)
		ndpa = 1;
	if ((attrib->subtype == WIFI_ACTION_NOACK) && (attrib->order == 1))
		ht_ndpa = 1;
	if (attrib->subtype == WIFI_BF_REPORT_POLL)
		report_poll = 1;

	if ((!ndpa) && (!ht_ndpa) && (!report_poll))
		return;

	/*SET_TX_DESC_TXPKTSIZE_8822B(desc, pattrib->last_txcmdsz);*/
	/*SET_TX_DESC_OFFSET_8822B(desc, HALMAC_TX_DESC_SIZE_8822B);*/
	SET_TX_DESC_DISRTSFB_8822B(desc, 1);
	SET_TX_DESC_DISDATAFB(desc, 1);
	/*SET_TX_DESC_SW_SEQ_8822B(desc, pattrib->seqnum);*/
	SET_TX_DESC_DATA_BW_8822B(desc, rtl8822b_bw_mapping(adapter, attrib));
	SET_TX_DESC_SIGNALING_TA_PKT_SC_8822B(desc,
					rtl8822b_sc_mapping(adapter, attrib));
	/*SET_TX_DESC_RTY_LMT_EN_8822B(ptxdesc, 1);*/
	SET_TX_DESC_RTS_DATA_RTY_LMT_8822B(desc, 5);
	SET_TX_DESC_NDPA_8822B(desc, 1);
	SET_TX_DESC_NAVUSEHDR_8822B(desc, 1);
	/*SET_TX_DESC_QSEL_8822B(desc, QSLT_MGNT);*/
	/*
	 * NSS2MCS0 for VHT
	 * MCS8 for HT
	 */
	SET_TX_DESC_DATARATE_8822B(desc, MRateToHwRate(attrib->rate));
	/*SET_TX_DESC_USE_RATE_8822B(desc, 1);*/
	/*SET_TX_DESC_MACID_8822B(desc, pattrib->mac_id);*/ /* ad-hoc mode */
	/*SET_TX_DESC_G_ID_8822B(desc, 63);*/
	/*
	 * partial AID of 1st STA, at infrastructure mode, either SU or MU; 
	 * MACID, at ad-hoc mode
	 *
	 * For WMAC to restore the received CSI report of STA1.
	 * WMAC would set p_aid field to 0 in PLCP header for MU.
	 */
	/*SET_TX_DESC_P_AID_8822B(desc, pattrib->txbf_p_aid);*/
	SET_TX_DESC_SND_PKT_SEL_8822B(desc, attrib->bf_pkt_type);
#endif /* CONFIG_BEAMFORMING */
}

void rtl8822b_cal_txdesc_chksum(PADAPTER adapter, u8 *ptxdesc)
{
	struct halmac_adapter *halmac;
	struct halmac_api *api;


	halmac = adapter_to_halmac(adapter);
	api = HALMAC_GET_API(halmac);

	api->halmac_fill_txdesc_checksum(halmac, ptxdesc);
}


#ifdef CONFIG_MP_INCLUDED
void rtl8822b_prepare_mp_txdesc(PADAPTER adapter, struct mp_priv *pmp_priv)
{
	u8 *desc;
	struct pkt_attrib *attrib;
	u32 pkt_size;
	s32 bmcast;
	u32 desc_size;
	u8 data_rate, pwr_status, offset;


	desc = pmp_priv->tx.desc;
	attrib = &pmp_priv->tx.attrib;
	pkt_size = attrib->last_txcmdsz;
	bmcast = IS_MCAST(attrib->ra);
	desc_size = rtl8822b_get_tx_desc_size(adapter);

	SET_TX_DESC_LS_8822B(desc, 1);
	SET_TX_DESC_TXPKTSIZE_8822B(desc, pkt_size);

	offset = desc_size;
	SET_TX_DESC_OFFSET_8822B(desc, offset);
#if defined(CONFIG_PCI_HCI)
	SET_TX_DESC_PKT_OFFSET_8822B(desc, 0); /* 8822BE pkt_offset is 0 */
#else
	SET_TX_DESC_PKT_OFFSET_8822B(desc, 1);
#endif

	if (bmcast)
		SET_TX_DESC_BMC_8822B(desc, 1);

	SET_TX_DESC_MACID_8822B(desc, attrib->mac_id);
	SET_TX_DESC_RATE_ID_8822B(desc, attrib->raid);
	SET_TX_DESC_QSEL_8822B(desc, attrib->qsel);

	if (pmp_priv->preamble)
		SET_TX_DESC_DATA_SHORT_8822B(desc, 1);

	if (!attrib->qos_en)
		SET_TX_DESC_EN_HWSEQ_8822B(desc, 1);
	else
		SET_TX_DESC_SW_SEQ_8822B(desc, attrib->seqnum);

	if (pmp_priv->bandwidth <= CHANNEL_WIDTH_160)
		SET_TX_DESC_DATA_BW_8822B(desc, pmp_priv->bandwidth);
	else {
		RTW_ERR("%s: unknown bandwidth %d, use 20M\n",
			 __FUNCTION__, pmp_priv->bandwidth);
		SET_TX_DESC_DATA_BW_8822B(desc, CHANNEL_WIDTH_20);
	}

	SET_TX_DESC_DISDATAFB_8822B(desc, 1);
	SET_TX_DESC_USE_RATE_8822B(desc, 1);
	SET_TX_DESC_DATARATE_8822B(desc, pmp_priv->rateidx);
}
#endif /* CONFIG_MP_INCLUDED */

static void fill_default_txdesc(struct xmit_frame *pxmitframe, u8 *pbuf)
{
	PADAPTER adapter;
	PHAL_DATA_TYPE hal;
	struct mlme_ext_priv *pmlmeext;
	struct mlme_ext_info *pmlmeinfo;
	struct pkt_attrib *pattrib;
	s32 bmcst;
	u32 desc_size;
	u8 hw_port;

	adapter = pxmitframe->padapter;
	hal = GET_HAL_DATA(adapter);
	pmlmeext = &adapter->mlmeextpriv;
	pmlmeinfo = &(pmlmeext->mlmext_info);

	pattrib = &pxmitframe->attrib;
	bmcst = IS_MCAST(pattrib->ra);
	hw_port = rtw_hal_get_port(adapter);

	desc_size = rtl8822b_get_tx_desc_size(adapter);
	_rtw_memset(pbuf, 0, desc_size);

	if (pxmitframe->frame_tag == DATA_FRAMETAG) {
		u8 drv_userate = 0;

		SET_TX_DESC_MACID_8822B(pbuf, pattrib->mac_id);
		SET_TX_DESC_RATE_ID_8822B(pbuf, pattrib->raid);
		SET_TX_DESC_QSEL_8822B(pbuf, pattrib->qsel);
		SET_TX_DESC_SW_SEQ_8822B(pbuf, pattrib->seqnum);

		rtl8822b_fill_txdesc_sectype(pattrib, pbuf);
		rtl8822b_fill_txdesc_vcs(adapter, pattrib, pbuf);

#ifdef CONFIG_CONCURRENT_MODE
		if (bmcst)
			rtl8822b_fill_txdesc_force_bmc_camid(pattrib, pbuf);
#endif

#ifdef CONFIG_P2P
		if (!rtw_p2p_chk_state(&adapter->wdinfo, P2P_STATE_NONE)) {
			if (pattrib->icmp_pkt == 1 && adapter->registrypriv.wifi_spec == 1)
				drv_userate = 1;
		}
#endif
#ifdef CONFIG_SUPPORT_DYNAMIC_TXPWR
		rtw_phydm_set_dyntxpwr(adapter, pbuf, pattrib->mac_id);
#endif

		if ((pattrib->ether_type != 0x888e) &&
		    (pattrib->ether_type != 0x0806) &&
		    (pattrib->ether_type != 0x88B4) &&
		    (pattrib->dhcp_pkt != 1) &&
		    (drv_userate != 1)
#ifdef CONFIG_AUTO_AP_MODE
		    && (pattrib->pctrl != _TRUE)
#endif
		   ) {
			/* Non EAP & ARP & DHCP type data packet */

			if (pattrib->ampdu_en == _TRUE) {
				SET_TX_DESC_AGG_EN_8822B(pbuf, 1);
				SET_TX_DESC_MAX_AGG_NUM_8822B(pbuf, 0x1F);
				SET_TX_DESC_AMPDU_DENSITY_8822B(pbuf, pattrib->ampdu_spacing);
			} else
				SET_TX_DESC_BK_8822B(pbuf, 1);

			rtl8822b_fill_txdesc_phy(adapter, pattrib, pbuf);

			/* compatibility for MCC consideration, use pmlmeext->cur_channel */
			if (!bmcst) {
				if (pmlmeext->cur_channel > 14)
					/* for 5G, OFDM 6M */
					SET_TX_DESC_DATA_RTY_LOWEST_RATE_8822B(pbuf, 4);
				else
					/* for 2.4G, CCK 1M */
					SET_TX_DESC_DATA_RTY_LOWEST_RATE_8822B(pbuf, 0);
			}

			if (hal->fw_ractrl == _FALSE)
				rtl8822b_fill_txdesc_tx_rate(adapter, pattrib,
					hal->INIDATA_RATE[pattrib->mac_id] & 0x7F,
					hal->INIDATA_RATE[pattrib->mac_id] & BIT(7) ? 1 : 0,
					1, pbuf);

			if (bmcst) {
				SET_TX_DESC_SW_DEFINE_8822B(pbuf, 0x01);
				rtl8822b_fill_txdesc_bmc_tx_rate(pattrib, pbuf);
			}

			/* modify data rate by iwpriv */
			if (adapter->fix_rate != 0xFF)
				rtl8822b_fill_txdesc_tx_rate(adapter, pattrib,
					adapter->fix_rate & 0x7F,
					adapter->fix_rate & BIT(7) ? 1 : 0,
					adapter->data_fb, pbuf);

			if (pattrib->ldpc)
				SET_TX_DESC_DATA_LDPC_8822B(pbuf, 1);
			if (pattrib->stbc)
				SET_TX_DESC_DATA_STBC_8822B(pbuf, 1);

#ifdef CONFIG_CMCC_TEST
			SET_TX_DESC_DATA_SHORT_8822B(pbuf, 1); /* use cck short premble */
#endif

#ifdef CONFIG_WMMPS_STA
			if (pattrib->trigger_frame)
				SET_TX_DESC_TRI_FRAME_8822B (pbuf, 1);
#endif /* CONFIG_WMMPS_STA */

		} else {
			/*
			 * EAP data packet and ARP packet.
			 * Use the 1M data rate to send the EAP/ARP packet.
			 * This will maybe make the handshake smooth.
			 */

			SET_TX_DESC_BK_8822B(pbuf, 1);
			SET_TX_DESC_USE_RATE_8822B(pbuf, 1);
			if (pmlmeinfo->preamble_mode == PREAMBLE_SHORT)
				SET_TX_DESC_DATA_SHORT_8822B(pbuf, 1);
#ifdef CONFIG_IP_R_MONITOR
			if((pattrib->ether_type == ETH_P_ARP) &&
				(IsSupportedTxOFDM(adapter->registrypriv.wireless_mode))) 
				SET_TX_DESC_DATARATE_8822B(pbuf, MRateToHwRate(IEEE80211_OFDM_RATE_6MB));
			 else
#endif/*CONFIG_IP_R_MONITOR*/
				SET_TX_DESC_DATARATE_8822B(pbuf, MRateToHwRate(pmlmeext->tx_rate));

			RTW_INFO(FUNC_ADPT_FMT ": SP Packet(0x%04X) rate=0x%x SeqNum = %d\n",
				FUNC_ADPT_ARG(adapter), pattrib->ether_type, MRateToHwRate(pmlmeext->tx_rate), pattrib->seqnum);

		}

#if defined(CONFIG_USB_TX_AGGREGATION) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
		SET_TX_DESC_DMA_TXAGG_NUM_8822B(pbuf, pxmitframe->agg_num);
#endif

#ifdef CONFIG_TDLS
#ifdef CONFIG_XMIT_ACK
		/* CCX-TXRPT ack for xmit mgmt frames. */
		if (pxmitframe->ack_report) {
#ifdef DBG_CCX
			RTW_INFO("%s set spe_rpt\n", __func__);
#endif
			SET_TX_DESC_SPE_RPT_8822B(pbuf, 1);
			SET_TX_DESC_SW_DEFINE_8822B(pbuf, (u8)(GET_PRIMARY_ADAPTER(adapter)->xmitpriv.seq_no));
		}
#endif /* CONFIG_XMIT_ACK */
#endif
	} else if (pxmitframe->frame_tag == MGNT_FRAMETAG) {
		SET_TX_DESC_MACID_8822B(pbuf, pattrib->mac_id);
		SET_TX_DESC_QSEL_8822B(pbuf, pattrib->qsel);
		SET_TX_DESC_RATE_ID_8822B(pbuf, pattrib->raid);
		SET_TX_DESC_SW_SEQ_8822B(pbuf, pattrib->seqnum);
		SET_TX_DESC_USE_RATE_8822B(pbuf, 1);

		SET_TX_DESC_MBSSID_8822B(pbuf, pattrib->mbssid & 0xF);

		SET_TX_DESC_DATARATE_8822B(pbuf, MRateToHwRate(pattrib->rate));

		SET_TX_DESC_RTY_LMT_EN_8822B(pbuf, 1);
		if (pattrib->retry_ctrl == _TRUE)
			SET_TX_DESC_RTS_DATA_RTY_LMT_8822B(pbuf, 6);
		else
			SET_TX_DESC_RTS_DATA_RTY_LMT_8822B(pbuf, 12);

		rtl8822b_fill_txdesc_mgnt_bf(pxmitframe, pbuf);

#ifdef CONFIG_XMIT_ACK
		/* CCX-TXRPT ack for xmit mgmt frames. */
		if (pxmitframe->ack_report) {
#ifdef DBG_CCX
			RTW_INFO("%s set spe_rpt\n", __FUNCTION__);
#endif
			SET_TX_DESC_SPE_RPT_8822B(pbuf, 1);
			SET_TX_DESC_SW_DEFINE_8822B(pbuf, (u8)(GET_PRIMARY_ADAPTER(adapter)->xmitpriv.seq_no));
		}
#endif /* CONFIG_XMIT_ACK */
	} else if (pxmitframe->frame_tag == TXAGG_FRAMETAG)
		RTW_INFO("%s: TXAGG_FRAMETAG\n", __FUNCTION__);
#ifdef CONFIG_MP_INCLUDED
	else if (pxmitframe->frame_tag == MP_FRAMETAG) {
		RTW_DBG("%s: MP_FRAMETAG\n", __FUNCTION__);
		fill_txdesc_for_mp(adapter, pbuf);
	}
#endif
	else {
		RTW_INFO("%s: frame_tag=0x%x\n", __FUNCTION__, pxmitframe->frame_tag);

		SET_TX_DESC_MACID_8822B(pbuf, pattrib->mac_id);
		SET_TX_DESC_RATE_ID_8822B(pbuf, pattrib->raid);
		SET_TX_DESC_QSEL_8822B(pbuf, pattrib->qsel);
		SET_TX_DESC_SW_SEQ_8822B(pbuf, pattrib->seqnum);
		SET_TX_DESC_USE_RATE_8822B(pbuf, 1);
		SET_TX_DESC_DATARATE_8822B(pbuf, MRateToHwRate(pmlmeext->tx_rate));
	}

	SET_TX_DESC_TXPKTSIZE_8822B(pbuf, pattrib->last_txcmdsz);

	{
		u8 pkt_offset, offset;

		pkt_offset = 0;
		offset = desc_size;
#ifdef CONFIG_USB_HCI
		pkt_offset = pxmitframe->pkt_offset;
		offset += (pxmitframe->pkt_offset >> 3);
#endif /* CONFIG_USB_HCI */

#ifdef CONFIG_TX_EARLY_MODE
		if (pxmitframe->frame_tag == DATA_FRAMETAG) {
			pkt_offset = 1;
			offset += EARLY_MODE_INFO_SIZE;
		}
#endif /* CONFIG_TX_EARLY_MODE */

		SET_TX_DESC_PKT_OFFSET_8822B(pbuf, pkt_offset);
		SET_TX_DESC_OFFSET_8822B(pbuf, offset);
#ifdef CONFIG_TCP_CSUM_OFFLOAD_TX
	if (pattrib->hw_csum == 1) {
		int offset = 48 + pkt_offset*8 + 8;

		SET_TX_DESC_OFFSET_8822B(pbuf, offset);
		SET_TX_DESC_CHK_EN_8822B(pbuf, 1);
		SET_TX_DESC_WHEADER_LEN_8822B(pbuf, (pattrib->hdrlen + pattrib->iv_len + XATTRIB_GET_MCTRL_LEN(pattrib))>>1);
	}
#endif
	}

	if (bmcst)
		SET_TX_DESC_BMC_8822B(pbuf, 1);

	/*
	 * 2009.11.05. tynli_test. Suggested by SD4 Filen for FW LPS.
	 * (1) The sequence number of each non-Qos frame / broadcast / multicast /
	 * mgnt frame should be controlled by Hw because Fw will also send null data
	 * which we cannot control when Fw LPS enable.
	 * --> default enable non-Qos data sequense number. 2010.06.23. by tynli.
	 * (2) Enable HW SEQ control for beacon packet, because we use Hw beacon.
	 * (3) Use HW Qos SEQ to control the seq num of Ext port non-Qos packets.
	 * 2010.06.23. Added by tynli.
	 */
	if (!pattrib->qos_en) {
		SET_TX_DESC_DISQSELSEQ_8822B(pbuf, 1);
		SET_TX_DESC_EN_HWSEQ_8822B(pbuf, 1);
		SET_TX_DESC_HW_SSN_SEL_8822B(pbuf, pattrib->hw_ssn_sel);
	}

	SET_TX_DESC_PORT_ID_8822B(pbuf, hw_port);
	SET_TX_DESC_MULTIPLE_PORT_8822B(pbuf, hw_port);

#ifdef CONFIG_ANTENNA_DIVERSITY
	if (!bmcst && pattrib->psta)
		odm_set_tx_ant_by_tx_info(adapter_to_phydm(adapter), pbuf, pattrib->psta->cmn.mac_id);
#endif

	rtl8822b_fill_txdesc_bf(pxmitframe, pbuf);
}

/*
 * Description:
 *
 * Parameters:
 *	pxmitframe	xmitframe
 *	pbuf		where to fill tx desc
 */
void rtl8822b_update_txdesc(struct xmit_frame *pxmitframe, u8 *pbuf)
{
	fill_default_txdesc(pxmitframe, pbuf);
	rtl8822b_cal_txdesc_chksum(pxmitframe->padapter, pbuf);
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
	/* Clear all status */
	struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
	struct xmit_priv		*pxmitpriv = &adapter->xmitpriv;
	u32 desc_size;
	u8 hw_port = rtw_hal_get_port(adapter);

	desc_size = rtl8822b_get_tx_desc_size(adapter);
	_rtw_memset(pDesc, 0, desc_size);

	SET_TX_DESC_LS_8822B(pDesc, 1);

	SET_TX_DESC_OFFSET_8822B(pDesc, desc_size);

	SET_TX_DESC_TXPKTSIZE_8822B(pDesc, BufferLen);
	SET_TX_DESC_QSEL_8822B(pDesc, QSLT_MGNT); /* Fixed queue of Mgnt queue */

	if (pmlmeext->cur_wireless_mode & WIRELESS_11B)
		SET_TX_DESC_RATE_ID_8822B(pDesc, RATEID_IDX_B);
	else
		SET_TX_DESC_RATE_ID_8822B(pDesc, RATEID_IDX_G);

	/* Set NAVUSEHDR to prevent Ps-poll AId filed to be changed to error vlaue by HW */
	if (_TRUE == IsPsPoll)
		SET_TX_DESC_NAVUSEHDR_8822B(pDesc, 1);
	else {
		SET_TX_DESC_DISQSELSEQ_8822B(pDesc, 1);
		SET_TX_DESC_EN_HWSEQ_8822B(pDesc, 1);
		SET_TX_DESC_HW_SSN_SEL_8822B(pDesc, pxmitpriv->hw_ssn_seq_no);/*pattrib->hw_ssn_sel*/
		SET_TX_DESC_EN_HWEXSEQ_8822B(pDesc, 0);
	}

	if (_TRUE == IsBTQosNull)
		SET_TX_DESC_BT_NULL_8822B(pDesc, 1);

	SET_TX_DESC_USE_RATE_8822B(pDesc, 1);
	SET_TX_DESC_DATARATE_8822B(pDesc, MRateToHwRate(pmlmeext->tx_rate));

#ifdef CONFIG_MCC_MODE
	/* config Null data retry number */
	if (IsPsPoll == _FALSE && IsBTQosNull == _FALSE && bDataFrame == _FALSE) {
		if (rtw_hal_check_mcc_status(adapter, MCC_STATUS_PROCESS_MCC_START_SETTING)) {
			u8 rty_num = adapter->mcc_adapterpriv.null_rty_num;
			if (rty_num != 0) {
				SET_TX_DESC_RTY_LMT_EN_8822B(pDesc, 1);
				SET_TX_DESC_RTS_DATA_RTY_LMT_8822B(pDesc, rty_num);
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
			SET_TX_DESC_SEC_TYPE_8822B(pDesc, 0x0);
			break;
		case _WEP40_:
		case _WEP104_:
		case _TKIP_:
			SET_TX_DESC_SEC_TYPE_8822B(pDesc, 0x1);
			break;
		case _SMS4_:
			SET_TX_DESC_SEC_TYPE_8822B(pDesc, 0x2);
			break;
		case _AES_:
			SET_TX_DESC_SEC_TYPE_8822B(pDesc, 0x3);
			break;
		default:
			SET_TX_DESC_SEC_TYPE_8822B(pDesc, 0x0);
			break;
		}
	}

	SET_TX_DESC_PORT_ID_8822B(pDesc, hw_port);
	SET_TX_DESC_MULTIPLE_PORT_8822B(pDesc, hw_port);
#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	/*
	 * USB interface drop packet if the checksum of descriptor isn't correct.
	 * Using this checksum can let hardware recovery from packet bulk out error (e.g. Cancel URC, Bulk out error.).
	 */
	rtl8822b_cal_txdesc_chksum(adapter, pDesc);
#endif
}

void rtl8822b_dbg_dump_tx_desc(PADAPTER adapter, int frame_tag, u8 *ptxdesc)
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

	/* 8822B TX SIZE = 48(HALMAC_TX_DESC_SIZE_8822B) */
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
/* xmit section */
void rtl8822b_init_xmit_priv(_adapter *adapter)
{
	struct xmit_priv *pxmitpriv = &adapter->xmitpriv;

	pxmitpriv->hw_ssn_seq_no = rtw_get_hwseq_no(adapter);
	pxmitpriv->nqos_ssn = 0;
}

void rtl8822b_rxdesc2attribute(struct rx_pkt_attrib *a, u8 *desc)
{
	/* initial value */
	_rtw_memset(a, 0, sizeof(struct rx_pkt_attrib));
	a->bw = CHANNEL_WIDTH_MAX;

	/* Get from RX DESC */
	a->pkt_len = (u16)GET_RX_DESC_PKT_LEN_8822B(desc);
	a->pkt_rpt_type = GET_RX_DESC_C2H_8822B(desc) ? C2H_PACKET : NORMAL_RX;

	if (a->pkt_rpt_type == NORMAL_RX) {
		a->crc_err = (u8)GET_RX_DESC_CRC32_8822B(desc);
		a->icv_err = (u8)GET_RX_DESC_ICV_ERR_8822B(desc);
		a->drvinfo_sz = (u8)GET_RX_DESC_DRV_INFO_SIZE_8822B(desc) << 3;
		a->encrypt = (u8)GET_RX_DESC_SECURITY_8822B(desc);
		a->qos = (u8)GET_RX_DESC_QOS_8822B(desc);
		a->shift_sz = (u8)GET_RX_DESC_SHIFT_8822B(desc);
		a->physt = (u8)GET_RX_DESC_PHYST_8822B(desc);
		a->bdecrypted = (u8)GET_RX_DESC_SWDEC_8822B(desc) ? 0 : 1;

		a->priority = (u8)GET_RX_DESC_TID_8822B(desc);
		a->amsdu = (u8)GET_RX_DESC_AMSDU_8822B(desc);
		a->mdata = (u8)GET_RX_DESC_MD_8822B(desc);
		a->mfrag = (u8)GET_RX_DESC_MF_8822B(desc);

		a->seq_num = (u16)GET_RX_DESC_SEQ_8822B(desc);
		a->frag_num = (u8)GET_RX_DESC_FRAG_8822B(desc);

		a->data_rate = (u8)GET_RX_DESC_RX_RATE_8822B(desc);
		a->ppdu_cnt = (u8)GET_RX_DESC_PPDU_CNT_8822B(desc);
		a->free_cnt = (u32)GET_RX_DESC_TSFL_8822B(desc);

#ifdef CONFIG_TCP_CSUM_OFFLOAD_RX
		/* RX TCP checksum offload related variables */
		a->csum_valid = (u8)GET_RX_DESC_CHK_VLD_8822B(desc);
		a->csum_err = (u8)GET_RX_DESC_CHKERR_8822B(desc);
#endif /* CONFIG_TCP_CSUM_OFFLOAD_RX */
	}
}

void rtl8822b_query_rx_desc(union recv_frame *precvframe, u8 *pdesc)
{
	rtl8822b_rxdesc2attribute(&precvframe->u.hdr.attrib, pdesc);
}

void rtl8822b_set_hal_ops(PADAPTER adapter)
{
	struct hal_com_data *hal;
	struct hal_ops *ops;


	hal = GET_HAL_DATA(adapter);
	ops = &adapter->hal_func;

	/*
	 * Initialize hal_com_data variables
	 */
	hal->efuse0x3d7 = 0xFF;
	hal->efuse0x3d8 = 0xFF;

	/*
	 * Initialize operation callback functions
	 */
	/*** initialize section ***/
	ops->read_chip_version = read_chip_version;
/*
	ops->init_default_value = NULL;
	ops->intf_chip_configure = NULL;
*/
	ops->read_adapter_info = rtl8822b_read_efuse;
	ops->hal_power_on = rtl8822b_power_on;
	ops->hal_power_off = rtl8822b_power_off;
	ops->hal_init = rtl8822b_init;
	ops->hal_deinit = rtl8822b_deinit;
	ops->dm_init = rtl8822b_phy_init_dm_priv;
	ops->dm_deinit = rtl8822b_phy_deinit_dm_priv;

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
	ops->run_thread = rtl8822b_run_thread;
	ops->cancel_thread = rtl8822b_cancel_thread;

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
	ops->check_ips_status = check_ips_status;
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
	ops->set_chnl_bw_handler = rtl8822b_set_channel_bw;

	ops->set_tx_power_level_handler = rtl8822b_set_tx_power_level;
	ops->set_tx_power_index_handler = rtl8822b_set_tx_power_index;
	ops->get_tx_power_index_handler = hal_com_get_txpwr_idx;

	ops->hal_dm_watchdog = rtl8822b_phy_haldm_watchdog;

	ops->set_hw_reg_handler = rtl8822b_sethwreg;
	ops->GetHwRegHandler = rtl8822b_gethwreg;
	ops->get_hal_def_var_handler = rtl8822b_gethaldefvar;
	ops->SetHalDefVarHandler = rtl8822b_sethaldefvar;

	ops->GetHalODMVarHandler = GetHalODMVar;
	ops->SetHalODMVarHandler = SetHalODMVar;

	ops->SetBeaconRelatedRegistersHandler = set_beacon_related_registers;

/*
	ops->interface_ps_func = NULL;
*/
	ops->read_bbreg = rtl8822b_read_bb_reg;
	ops->write_bbreg = rtl8822b_write_bb_reg;
	ops->read_rfreg = rtl8822b_read_rf_reg;
	ops->write_rfreg = rtl8822b_write_rf_reg;
	ops->read_wmmedca_reg = rtl8822b_read_wmmedca_reg;

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
	ops->sreset_init_value = sreset_init_value;
	ops->sreset_reset_value = sreset_reset_value;
	ops->silentreset = sreset_reset;
	ops->sreset_xmit_status_check = xmit_status_check;
	ops->sreset_linked_status_check = linked_status_check;
	ops->sreset_get_wifi_status = sreset_get_wifi_status;
	ops->sreset_inprogress = sreset_inprogress;
#endif /* DBG_CONFIG_ERROR_DETECT */

#ifdef CONFIG_IOL
/*
	ops->IOL_exec_cmds_sync = NULL;
*/
#endif

	ops->hal_notch_filter = rtl8822b_notch_filter_switch;
	ops->hal_mac_c2h_handler = rtl8822b_c2h_handler;
	ops->fill_h2c_cmd = rtl8822b_fillh2ccmd;
	ops->fill_fake_txdesc = fill_fake_txdesc;
	ops->fw_dl = rtl8822b_fw_dl;

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN) || defined(CONFIG_PCI_HCI)
/*
	ops->clear_interrupt = NULL;
*/
#endif
/*
	ops->hal_get_tx_buff_rsvd_page_num = NULL;
*/
#ifdef CONFIG_GPIO_API
/*
	ops->update_hisr_hsisr_ind = NULL;
*/
#endif

	/* HALMAC related functions */
	ops->init_mac_register = rtl8822b_phy_init_mac_register;
	ops->init_phy = rtl8822b_phy_init;
	ops->reqtxrpt = rtl8822b_req_txrpt_cmd;
}
