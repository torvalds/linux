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
#define _RTL8822B_OPS_C_

#include <drv_types.h>		/* basic_types.h, rtw_io.h and etc. */
#include <rtw_xmit.h>		/* struct xmit_priv */
#ifdef DBG_CONFIG_ERROR_DETECT
#include <rtw_sreset.h>
#endif /* DBG_CONFIG_ERROR_DETECT */
#include <hal_data.h>		/* PHAL_DATA_TYPE, GET_HAL_DATA() */
#include <hal_com.h>		/* rtw_hal_config_rftype(), dump_chip_info() and etc. */
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
	},
};


static void read_chip_version(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal;
	u32 value32;


	hal = GET_HAL_DATA(adapter);

	value32 = rtw_read32(adapter, REG_SYS_CFG1_8822B);
	hal->VersionID.ICType = CHIP_8822B;
	hal->VersionID.ChipType = ((value32 & BIT_RTL_ID_8822B) ? TEST_CHIP : NORMAL_CHIP);
	hal->VersionID.CUTVersion = BIT_GET_CHIP_VER_8822B(value32);
	hal->VersionID.VendorType = BIT_GET_VENDOR_ID_8822B(value32);
	hal->VersionID.VendorType >>= 2;
	switch (hal->VersionID.VendorType) {
	case 0:
		hal->VersionID.VendorType = CHIP_VENDOR_TSMC;
		break;
	case 1:
		hal->VersionID.VendorType = CHIP_VENDOR_SMIC;
		break;
	case 2:
		hal->VersionID.VendorType = CHIP_VENDOR_UMC;
		break;
	}

	hal->VersionID.RFType = ((value32 & BIT_RF_TYPE_ID_8822B) ? RF_TYPE_2T2R : RF_TYPE_1T1R);
	if (adapter->registrypriv.special_rf_path == 1)
		hal->VersionID.RFType = RF_TYPE_1T1R;	/* RF_1T1R; */

	hal->RegulatorMode = ((value32 & BIT_SPSLDO_SEL_8822B) ? RT_LDO_REGULATOR : RT_SWITCHING_REGULATOR);

	value32 = rtw_read32(adapter, REG_SYS_STATUS1_8822B);
	hal->VersionID.ROMVer = BIT_GET_RF_RL_ID_8822B(value32);

	/* For multi-function consideration. */
	hal->MultiFunc = RT_MULTI_FUNC_NONE;
	value32 = rtw_read32(adapter, REG_WL_BT_PWR_CTRL_8822B);
	hal->MultiFunc |= ((value32 & BIT_WL_FUNC_EN_8822B) ? RT_MULTI_FUNC_WIFI : 0);
	hal->MultiFunc |= ((value32 & BIT_BT_FUNC_EN_8822B) ? RT_MULTI_FUNC_BT : 0);
	hal->PolarityCtl = ((value32 & BIT_WL_HWPDN_SL_8822B) ? RT_POLARITY_HIGH_ACT : RT_POLARITY_LOW_ACT);

	rtw_hal_config_rftype(adapter);

	dump_chip_info(hal->VersionID);
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
		hal->EEPROMVersion = map[EEPROM_VERSION_8822B];
	else
		hal->EEPROMVersion = 1;

	RTW_INFO("EEPROM Version = %d\n", hal->EEPROMVersion);
}

static u8 Hal_GetChnlGroup(u8 chnl, u8 *pGroup)
{
	u8 bIn24G = _TRUE;


	if (chnl <= 14) {
		bIn24G = _TRUE;

		if (1 <= chnl && chnl <= 2)
			*pGroup = 0;
		else if (3 <= chnl && chnl <= 5)
			*pGroup = 1;
		else if (6 <= chnl && chnl <= 8)
			*pGroup = 2;
		else if (9 <= chnl && chnl <= 11)
			*pGroup = 3;
		else if (12 <= chnl && chnl <= 14)
			*pGroup = 4;
		else
			RTW_INFO("%s: in 2.4 G, but Channel %d in Group not found\n", __FUNCTION__, chnl);
	} else {
		bIn24G = _FALSE;

		if (36 <= chnl && chnl <=  42)
			*pGroup = 0;
		else if (44 <= chnl && chnl <= 48)
			*pGroup = 1;
		else if (50 <= chnl && chnl <= 58)
			*pGroup = 2;
		else if (60 <= chnl && chnl <= 64)
			*pGroup = 3;
		else if (100 <= chnl && chnl <= 106)
			*pGroup = 4;
		else if (108 <= chnl && chnl <= 114)
			*pGroup = 5;
		else if (116 <= chnl && chnl <= 122)
			*pGroup = 6;
		else if (124 <= chnl && chnl <= 130)
			*pGroup = 7;
		else if (132 <= chnl && chnl <= 138)
			*pGroup = 8;
		else if (140 <= chnl && chnl <= 144)
			*pGroup = 9;
		else if (149 <= chnl && chnl <= 155)
			*pGroup = 10;
		else if (157 <= chnl && chnl <= 161)
			*pGroup = 11;
		else if (165 <= chnl && chnl <= 171)
			*pGroup = 12;
		else if (173 <= chnl && chnl <= 177)
			*pGroup = 13;
		else
			RTW_INFO("%s: in 5G, but Channel %d in Group not found\n", __FUNCTION__, chnl);
	}

	return bIn24G;
}

static void load_default_txpower(PADAPTER adapter, PTxPowerInfo24G tbl2g4, PTxPowerInfo5G tbl5g)
{
	u32 rfpath, group, txcount;


	for (rfpath = 0; rfpath < MAX_RF_PATH; rfpath++) {
		/* 2.4G default value */
		for (group = 0 ; group < MAX_CHNL_GROUP_24G; group++) {
			tbl2g4->IndexCCK_Base[rfpath][group] = EEPROM_DEFAULT_24G_INDEX;
			tbl2g4->IndexBW40_Base[rfpath][group] = EEPROM_DEFAULT_24G_INDEX;
		}
		for (txcount = 0; txcount < MAX_TX_COUNT; txcount++) {
			if (txcount == 0) {
				tbl2g4->BW20_Diff[rfpath][0] = EEPROM_DEFAULT_24G_HT20_DIFF;
				tbl2g4->OFDM_Diff[rfpath][0] = EEPROM_DEFAULT_24G_OFDM_DIFF;
			} else {
				tbl2g4->BW20_Diff[rfpath][txcount] = EEPROM_DEFAULT_DIFF;
				tbl2g4->BW40_Diff[rfpath][txcount] = EEPROM_DEFAULT_DIFF;
				tbl2g4->CCK_Diff[rfpath][txcount] = EEPROM_DEFAULT_DIFF;
				tbl2g4->OFDM_Diff[rfpath][txcount] = EEPROM_DEFAULT_DIFF;
			}
		}

		/* 5G default value */
		for (group = 0; group < MAX_CHNL_GROUP_5G; group++)
			tbl5g->IndexBW40_Base[rfpath][group] = EEPROM_DEFAULT_5G_INDEX;
		for (txcount = 0; txcount < MAX_TX_COUNT; txcount++) {
			if (txcount == 0) {
				tbl5g->OFDM_Diff[rfpath][0] = EEPROM_DEFAULT_5G_OFDM_DIFF;
				tbl5g->BW20_Diff[rfpath][0] = EEPROM_DEFAULT_5G_HT20_DIFF;
				tbl5g->BW80_Diff[rfpath][0] = EEPROM_DEFAULT_DIFF;
				tbl5g->BW160_Diff[rfpath][0] = EEPROM_DEFAULT_DIFF;
			} else {
				tbl5g->OFDM_Diff[rfpath][txcount] = EEPROM_DEFAULT_DIFF;
				tbl5g->BW20_Diff[rfpath][txcount] = EEPROM_DEFAULT_DIFF;
				tbl5g->BW40_Diff[rfpath][txcount] = EEPROM_DEFAULT_DIFF;
				tbl5g->BW80_Diff[rfpath][txcount] = EEPROM_DEFAULT_DIFF;
				tbl5g->BW160_Diff[rfpath][txcount] = EEPROM_DEFAULT_DIFF;
			}
		}
	}
}

static inline u8 power_valid(u8 power)
{
	if (power <= 63)
		return _TRUE;

	return _FALSE;
}

static inline s8 power_diff(s8 diff)
{
	/* bit sign number to 8 bit sign number */
	if (diff & BIT(3))
		diff |= 0xF0;

	return diff;
}

static void load_txpower_from_map(
	PADAPTER adapter, PTxPowerInfo24G tbl2g4, PTxPowerInfo5G tbl5g,
	u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal;
	u32 rfpath, eeAddr, group, txcount = 0;
	u8 power;
	s8 diff;


	hal = GET_HAL_DATA(adapter);

	hal->bTXPowerDataReadFromEEPORM = _FALSE;
	_rtw_memset(tbl2g4, 0, sizeof(TxPowerInfo24G));
	_rtw_memset(tbl5g, 0, sizeof(TxPowerInfo5G));
	load_default_txpower(adapter, tbl2g4, tbl5g);

	if (_FALSE == mapvalid)
		return;

	eeAddr = EEPROM_TX_PWR_INX_8822B;
	if (0xFF == map[eeAddr])
		return;

	hal->bTXPowerDataReadFromEEPORM = _TRUE;

	for (rfpath = 0; rfpath < hal->NumTotalRFPath; rfpath++) {
		/* 2.4G */
		for (group = 0; group < MAX_CHNL_GROUP_24G; group++) {
			power = map[eeAddr++];
			if (power_valid(power) == _TRUE)
				tbl2g4->IndexCCK_Base[rfpath][group] = power;
		}

		for (group = 0; group < MAX_CHNL_GROUP_24G - 1; group++) {
			power = map[eeAddr++];
			if (power_valid(power) == _TRUE)
				tbl2g4->IndexBW40_Base[rfpath][group] = power;
		}

		for (txcount = 0; txcount < MAX_TX_COUNT; txcount++) {
			if (txcount == 0) {
				tbl2g4->BW40_Diff[rfpath][0] = 0;

				diff = (map[eeAddr] & 0xF0) >> 4;
				tbl2g4->BW20_Diff[rfpath][0] = power_diff(diff);

				diff = map[eeAddr] & 0x0F;
				tbl2g4->OFDM_Diff[rfpath][0] = power_diff(diff);

				tbl2g4->CCK_Diff[rfpath][0] = 0;

				eeAddr++;
			} else {
				diff = (map[eeAddr] & 0xF0) >> 4;
				tbl2g4->BW40_Diff[rfpath][txcount] = power_diff(diff);

				diff = map[eeAddr] & 0x0F;
				tbl2g4->BW20_Diff[rfpath][txcount] = power_diff(diff);

				eeAddr++;

				diff = (map[eeAddr] & 0xF0) >> 4;
				tbl2g4->OFDM_Diff[rfpath][txcount] = power_diff(diff);

				diff = map[eeAddr] & 0x0F;
				tbl2g4->CCK_Diff[rfpath][txcount] = power_diff(diff);

				eeAddr++;
			}
		}

		/* 5G */
		for (group = 0; group < MAX_CHNL_GROUP_5G; group++) {
			power = map[eeAddr++];
			if (power_valid(power) == _TRUE)
				tbl5g->IndexBW40_Base[rfpath][group] = power;
		}

		for (txcount = 0; txcount < MAX_TX_COUNT; txcount++) {
			if (txcount == 0) {
				tbl5g->BW40_Diff[rfpath][0] = 0;

				diff = (map[eeAddr] & 0xF0) >> 4;
				tbl5g->BW20_Diff[rfpath][0] = power_diff(diff);

				diff = map[eeAddr] & 0x0F;
				tbl5g->OFDM_Diff[rfpath][0] = power_diff(diff);

				eeAddr++;
			} else {
				diff = (map[eeAddr] & 0xF0) >> 4;
				tbl5g->BW40_Diff[rfpath][txcount] = power_diff(diff);

				diff = map[eeAddr] & 0x0F;
				tbl5g->BW20_Diff[rfpath][txcount] = power_diff(diff);

				eeAddr++;
			}
		}

		diff = (map[eeAddr] & 0xF0) >> 4;
		tbl5g->OFDM_Diff[rfpath][1] = power_diff(diff);

		diff = map[eeAddr] & 0x0F;
		tbl5g->OFDM_Diff[rfpath][2] = power_diff(diff);

		eeAddr++;

		diff = map[eeAddr] & 0x0F;
		tbl5g->OFDM_Diff[rfpath][3] = power_diff(diff);

		eeAddr++;

		for (txcount = 0; txcount < MAX_TX_COUNT; txcount++) {
			diff = (map[eeAddr] & 0xF0) >> 4;
			tbl5g->BW80_Diff[rfpath][txcount] = power_diff(diff);

			diff = map[eeAddr] & 0x0F;
			tbl5g->BW160_Diff[rfpath][txcount] = power_diff(diff);

			eeAddr++;
		}
	}
}

static void Hal_EfuseParseTxPowerInfo(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	TxPowerInfo24G tbl2G4;
	TxPowerInfo5G tbl5g;
	u8 rfpath, ch, group = 0, txcount = 1;


	load_txpower_from_map(adapter, &tbl2G4, &tbl5g, map, mapvalid);

	for (rfpath = 0; rfpath < hal->NumTotalRFPath; rfpath++) {
		for (ch = 0; ch < CENTER_CH_2G_NUM; ch++) {
			Hal_GetChnlGroup(ch + 1, &group);

			if (ch == 14 - 1) {
				hal->Index24G_CCK_Base[rfpath][ch] = tbl2G4.IndexCCK_Base[rfpath][5];
				hal->Index24G_BW40_Base[rfpath][ch] = tbl2G4.IndexBW40_Base[rfpath][group];
			} else {
				hal->Index24G_CCK_Base[rfpath][ch] = tbl2G4.IndexCCK_Base[rfpath][group];
				hal->Index24G_BW40_Base[rfpath][ch] = tbl2G4.IndexBW40_Base[rfpath][group];
			}

			RTW_INFO("======= Path %d, Channel %d, Group %d =======\n", rfpath, ch + 1, group);
			RTW_INFO("Index24G_CCK_Base[%d][%d]=0x%x\n", rfpath, ch, hal->Index24G_CCK_Base[rfpath][ch]);
			RTW_INFO("Index24G_BW40_Base[%d][%d]=0x%x\n", rfpath, ch, hal->Index24G_BW40_Base[rfpath][ch]);
		}

		for (ch = 0; ch < CENTER_CH_5G_ALL_NUM; ch++) {
			Hal_GetChnlGroup(center_ch_5g_all[ch], &group);
			hal->Index5G_BW40_Base[rfpath][ch] = tbl5g.IndexBW40_Base[rfpath][group];

			RTW_INFO("======= Path %d, Channel %d, Group %d =======\n", rfpath, center_ch_5g_all[ch], group);
			RTW_INFO("Index5G_BW40_Base[%d][%d]=0x%x\n", rfpath, ch, hal->Index5G_BW40_Base[rfpath][ch]);
		}

		for (ch = 0; ch < CENTER_CH_5G_80M_NUM; ch++) {
			u8 upper = 0, lower = 0;

			Hal_GetChnlGroup(center_ch_5g_80m[ch], &group);
			upper = tbl5g.IndexBW40_Base[rfpath][group];
			lower = tbl5g.IndexBW40_Base[rfpath][group + 1];
			hal->Index5G_BW80_Base[rfpath][ch] = (upper + lower) / 2;

			RTW_INFO("======= Path %d, Channel %d, Group %d =======\n", rfpath, center_ch_5g_80m[ch], group);
			RTW_INFO("Index5G_BW80_Base[%d][%d]=0x%x\n", rfpath, ch, hal->Index5G_BW80_Base[rfpath][ch]);
		}

		for (txcount = 0; txcount < hal->NumTotalRFPath; txcount++) {
			hal->CCK_24G_Diff[rfpath][txcount] = tbl2G4.CCK_Diff[rfpath][txcount];
			hal->OFDM_24G_Diff[rfpath][txcount] = tbl2G4.OFDM_Diff[rfpath][txcount];
			hal->BW20_24G_Diff[rfpath][txcount] = tbl2G4.BW20_Diff[rfpath][txcount];
			hal->BW40_24G_Diff[rfpath][txcount] = tbl2G4.BW40_Diff[rfpath][txcount];

			hal->OFDM_5G_Diff[rfpath][txcount] = tbl5g.OFDM_Diff[rfpath][txcount];
			hal->BW20_5G_Diff[rfpath][txcount] = tbl5g.BW20_Diff[rfpath][txcount];
			hal->BW40_5G_Diff[rfpath][txcount] = tbl5g.BW40_Diff[rfpath][txcount];
			hal->BW80_5G_Diff[rfpath][txcount] = tbl5g.BW80_Diff[rfpath][txcount];

			RTW_INFO("----------------------------------- 2.4G %dSS ----------------------------------\n", txcount + 1);
			RTW_INFO("CCK_24G_Diff[%d][%d]=%d\n", rfpath, txcount, hal->CCK_24G_Diff[rfpath][txcount]);
			RTW_INFO("OFDM_24G_Diff[%d][%d]=%d\n", rfpath, txcount, hal->OFDM_24G_Diff[rfpath][txcount]);
			RTW_INFO("BW20_24G_Diff[%d][%d]=%d\n", rfpath, txcount, hal->BW20_24G_Diff[rfpath][txcount]);
			RTW_INFO("BW40_24G_Diff[%d][%d]=%d\n", rfpath, txcount, hal->BW40_24G_Diff[rfpath][txcount]);
			RTW_INFO("------------------------------------ 5G %dSS -----------------------------------\n", txcount + 1);
			RTW_INFO("OFDM_5G_Diff[%d][%d]=%d\n", rfpath, txcount, hal->OFDM_5G_Diff[rfpath][txcount]);
			RTW_INFO("BW20_5G_Diff[%d][%d]=%d\n", rfpath, txcount, hal->BW20_5G_Diff[rfpath][txcount]);
			RTW_INFO("BW40_5G_Diff[%d][%d]=%d\n", rfpath, txcount, hal->BW40_5G_Diff[rfpath][txcount]);
			RTW_INFO("BW80_5G_Diff[%d][%d]=%d\n", rfpath, txcount, hal->BW80_5G_Diff[rfpath][txcount]);
		}
	}

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
		 * EFUSE_0xC3[6] == 0, S1(Main)-ODM_RF_PATH_A;
		 * EFUSE_0xC3[6] == 1, S0(Aux)-ODM_RF_PATH_B
		 */
		hal->ant_path = (setting & BIT(6)) ? ODM_RF_PATH_B : ODM_RF_PATH_A;
	} else {
		hal->EEPROMBluetoothAntNum = Ant_x2;
		hal->ant_path = ODM_RF_PATH_A;
	}

	RTW_INFO("EEPROM %s BT-coex, ant_num=%d\n",
		 hal->EEPROMBluetoothCoexist == _TRUE ? "Enable" : "Disable",
		 hal->EEPROMBluetoothAntNum == Ant_x2 ? 2 : 1);
}

static void Hal_EfuseParseChnlPlan(PADAPTER adapter, u8 *map, u8 autoloadfail)
{
	adapter->mlmepriv.ChannelPlan = hal_com_config_channel_plan(
		adapter,
		map ? &map[EEPROM_COUNTRY_CODE_8822B] : NULL,
		map ? map[EEPROM_ChannelPlan_8822B] : 0xFF,
		adapter->registrypriv.alpha2,
		adapter->registrypriv.channel_plan,
		RTW_CHPLAN_REALTEK_DEFINE,
		autoloadfail
	);

	RTW_INFO("EEPROM ChannelPlan=0x%02x\n", adapter->mlmepriv.ChannelPlan);
}

static void Hal_EfuseParseXtal(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	if ((_TRUE == mapvalid) && map[EEPROM_XTAL_8822B] != 0xFF)
		hal->CrystalCap = map[EEPROM_XTAL_8822B];
	else
		hal->CrystalCap = EEPROM_Default_CrystalCap;

	RTW_INFO("EEPROM CrystalCap=0x%02x\n", hal->CrystalCap);
}

static void Hal_EfuseParseThermalMeter(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	/* ThermalMeter from EEPROM */
	if ((_TRUE == mapvalid) && (map[EEPROM_THERMAL_METER_8822B] != 0xFF))
		hal->EEPROMThermalMeter = map[EEPROM_THERMAL_METER_8822B];
	else {
		hal->EEPROMThermalMeter = EEPROM_Default_ThermalMeter;
		hal->odmpriv.RFCalibrateInfo.bAPKThermalMeterIgnore = _TRUE;
	}

	RTW_INFO("EEPROM ThermalMeter=0x%02x\n", hal->EEPROMThermalMeter);
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
				__FUNCTION__, EEPROM_RFE_OPTION_8723B, hal->TRxAntDivType);
	} else
		hal->TRxAntDivType = registry_par->antdiv_type;

	RTW_INFO("EEPROM AntDivCfg=%d, AntDivType=%d\n",
		 __FUNCTION__, hal->AntDivCfg, hal->TRxAntDivType);
#endif /* CONFIG_ANTENNA_DIVERSITY */
}

static void Hal_EfuseParseCustomerID(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	if (_TRUE == mapvalid)
		hal->EEPROMCustomerID = map[EEPROM_CustomID_8723B];
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

			hal->ExternalPA_5G = (hal->PAType_5G & BIT0) ? 1 : 0;
			hal->ExternalLNA_5G = (hal->LNAType_5G & BIT3) ? 1 : 0;
		} else {
			hal->ExternalPA_5G  = (GetRegAmplifierType5G(adapter) & ODM_BOARD_EXT_PA_5G)  ? 1 : 0;
			hal->ExternalLNA_5G = (GetRegAmplifierType5G(adapter) & ODM_BOARD_EXT_LNA_5G) ? 1 : 0;
		}
	} else {
		hal->ExternalPA_2G  = EEPROM_Default_PAType;
		hal->ExternalPA_5G  = 0xFF;
		hal->ExternalLNA_2G = EEPROM_Default_LNAType;
		hal->ExternalLNA_5G = 0xFF;

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
			hal->ExternalPA_5G  = 0;
			hal->ExternalLNA_5G = 0;
		} else {
			hal->ExternalPA_5G  = (GetRegAmplifierType5G(adapter) & ODM_BOARD_EXT_PA_5G)  ? 1 : 0;
			hal->ExternalLNA_5G = (GetRegAmplifierType5G(adapter) & ODM_BOARD_EXT_LNA_5G) ? 1 : 0;
		}
	}

	#if 0
	RTW_INFO("EEPROM PAType_2G is 0x%x, ExternalPA_2G = %d\n", hal->PAType_2G, hal->ExternalPA_2G);
	RTW_INFO("EEPROM PAType_5G is 0x%x, ExternalPA_5G = %d\n", hal->PAType_5G, hal->ExternalPA_5G);
	RTW_INFO("EEPROM LNAType_2G is 0x%x, ExternalLNA_2G = %d\n", hal->LNAType_2G, hal->ExternalLNA_2G);
	RTW_INFO("EEPROM LNAType_5G is 0x%x, ExternalLNA_5G = %d\n", hal->LNAType_5G, hal->ExternalLNA_5G);
	#endif
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

	#if 0
	RTW_INFO("EEPROM TypeGPA = 0x%X\n", hal->TypeGPA);
	RTW_INFO("EEPROM TypeAPA = 0x%X\n", hal->TypeAPA);
	RTW_INFO("EEPROM TypeGLNA = 0x%X\n", hal->TypeGLNA);
	RTW_INFO("EEPROM TypeALNA = 0x%X\n", hal->TypeALNA);
	#endif
}

static u8 Hal_ReadRFEType(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);

	/* check registry valye */
	if (GetRegRFEType(adapter) != CONFIG_RTW_RFE_TYPE) {
		hal->RFEType = GetRegRFEType(adapter);
		goto exit;
	}

	if (mapvalid) {
		/* check efuse map */
		hal->RFEType = ReadLE1Byte(&map[EEPROM_RFE_OPTION_8822B]);
		if (0xFF != hal->RFEType)
			goto exit;
	}

	/* error handle */
	hal->RFEType = 0;
	RTW_ERR("\n\nEmpty EFUSE with unknown REF type!!\n\n");
	printk("please program efuse or specify correct RFE type.\n");
	printk("cmd: insmod rtl8822bx.ko rtw_RFE_type=<rfe_type>\n\n");
	return _FAIL;

exit:
	RTW_INFO("EEPROM RFEType=0x%x\n", hal->RFEType);
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
	hal->tx_pa_bias_a = data[0];	/* efuse[0x3D7] */
	hal->tx_pa_bias_b = data[1];	/* efuse[0x3D8] */

	RTW_INFO("EEPROM PA_BIAS_A=0x%x\n", hal->tx_pa_bias_a);
	RTW_INFO("EEPROM PA_BIAS_B=0x%x\n", hal->tx_pa_bias_b);
}

#ifdef CONFIG_RF_POWER_TRIM
static void Hal_ReadRFGainOffset(PADAPTER adapter, u8 *map, u8 mapvalid)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);

	/*
	 * BB_RF Gain Offset from EEPROM
	 */
	if ((_TRUE == mapvalid) || (adapter->registrypriv.RegPwrTrimEnable == 1)) {
#if 0
		hal->EEPROMRFGainOffset = map[EEPROM_RF_GAIN_OFFSET];
		hal->EEPROMRFGainVal = EFUSE_Read1Byte(adapter, EEPROM_RF_GAIN_VAL);
#else
		hal->EEPROMRFGainOffset = 0;
		hal->EEPROMRFGainVal = 0xFF;
#endif
	} else {
		hal->EEPROMRFGainOffset = 0;
		hal->EEPROMRFGainVal = 0xFF;
	}
	RTW_INFO("EEPROM RFGainOffset=0x%02x\n", hal->EEPROMRFGainOffset);
	RTW_INFO("EEPROM RFGainVal=0x%02x\n", hal->EEPROMRFGainVal);
}
#endif /* CONFIG_RF_POWER_TRIM */

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
	struct dvobj_priv *d;
	PHAL_DATA_TYPE hal;
	u8 val8;
	u32 efuse_size;
	u8 *efuse_map = NULL;
	u8 valid;
	int err;
	HALMAC_EFUSE_READ_CFG cfg;

	d = adapter_to_dvobj(adapter);
	hal = GET_HAL_DATA(adapter);
	efuse_size = EEPROM_MAX_SIZE;
	efuse_map = hal->efuse_eeprom_data;

	err = rtw_halmac_get_logical_efuse_size(d, &efuse_size);
	if (err || !efuse_size) {
		RTW_INFO("%s: <ERROR> fail to get efuse size!\n", __FUNCTION__);
		efuse_size = EEPROM_MAX_SIZE;
	}
	if (efuse_size > EEPROM_MAX_SIZE) {
		RTW_INFO("%s: <ERROR> size of efuse data(%d) is large than expected(%d)!\n",
			 __FUNCTION__, efuse_size, EEPROM_MAX_SIZE);
		efuse_size = EEPROM_MAX_SIZE;
	}

	/* 1. Read registers to check hardware eFuse available or not */
	val8 = rtw_read8(adapter, REG_SYS_EEPROM_CTRL_8822B);
	hal->EepromOrEfuse = (val8 & BIT_EERPOMSEL_8822B) ? _TRUE : _FALSE;
	hal->bautoload_fail_flag = (val8 & BIT_AUTOLOAD_SUS_8822B) ? _FALSE : _TRUE;
	if (hal->bautoload_fail_flag == _TRUE)
		goto load_efuse_file;

	/*
	 * In 8822B, bautoload_fail_flag is used to present eFuse map is valid
	 * or not, no matter the map comes from hardware or files.
	 */

	/* 2. Read eFuse */
	err = rtw_halmac_read_logical_efuse_map(d, efuse_map, efuse_size);
	if (err) {
		RTW_INFO("%s: <ERROR> fail to get efuse map!\n", __FUNCTION__);
		goto load_efuse_file;
	}
	goto parse_efuse;

load_efuse_file:
	/* 3. Read Efuse file if necessary */
#ifdef CONFIG_EFUSE_CONFIG_FILE
	if (check_phy_efuse_tx_power_info_valid(adapter) == _FALSE) {
		if (Hal_readPGDataFromConfigFile(adapter) != _SUCCESS)
			RTW_INFO("%s: <WARN> invalid phy efuse and read from file fail, will use driver default!!\n", __FUNCTION__);
	}
#else
	/* check RFE_Type */

#endif /* CONFIG_EFUSE_CONFIG_FILE */

parse_efuse:
	rtw_dump_cur_efuse(adapter);

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
	if (Hal_ReadRFEType(adapter, efuse_map, valid) == _FAIL)
		return _FAIL;

	/* Data out of Efuse Map */
	Hal_EfuseParsePackageType(adapter, efuse_map, valid);
	/* Hal_EfuseParsePABias(adapter); */
#ifdef CONFIG_RF_POWER_TRIM
	Hal_ReadRFGainOffset(adapter, efuse_map, valid);
#endif /* CONFIG_RF_POWER_TRIM */

#ifdef CONFIG_USB_HCI
	Hal_ReadUsbModeSwitch(adapter, efuse_map, valid);
#endif /* CONFIG_USB_HCI */
	return _SUCCESS;
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

static void update_ra_mask(PADAPTER adapter, u32 mac_id, u8 rssi_level)
{
	u64 mask, rate_bitmap, *dm_RA_Mask = NULL;
	u8 shortGIrate = _FALSE, *dm_RteID = NULL;
	u8 arg[4] = {0};
	struct sta_info *psta;
	PHAL_DATA_TYPE hal;
	struct macid_ctl_t *macid_ctl;


	RTW_INFO(FUNC_ADPT_FMT ": mac_id=%d rssi_level=%d\n",
		 FUNC_ADPT_ARG(adapter), mac_id, rssi_level);

	macid_ctl = &adapter->dvobj->macid_ctl;
	psta = NULL;
	if (mac_id < macid_ctl->num)
		psta = macid_ctl->sta[mac_id];
	if (psta == NULL) {
		RTW_PRINT(FUNC_ADPT_FMT " macid:%u, sta is NULL\n",
			  FUNC_ADPT_ARG(adapter), mac_id);
		return;
	}

	hal = GET_HAL_DATA(adapter);
	shortGIrate = query_ra_short_GI(psta);
	mask = psta->ra_mask;

	rate_bitmap = 0xffffffff;
	rate_bitmap = PhyDM_Get_Rate_Bitmap_Ex(&hal->odmpriv, mac_id, mask, rssi_level, dm_RA_Mask, dm_RteID);
	RTW_INFO("%s => mac_id:%d, networkType:0x%02x, mask:0x%016llx\n\t ==> rssi_level:%d, rate_bitmap:0x%016llx, shortGIrate=%d\n",
		__FUNCTION__, mac_id, psta->wireless_mode, mask, rssi_level, rate_bitmap, shortGIrate);

	mask &= rate_bitmap;

#ifdef CONFIG_BT_COEXIST
	if (hal->EEPROMBluetoothCoexist) {
		rate_bitmap = rtw_btcoex_GetRaMask(adapter);
		mask &= ~rate_bitmap;
	}
#endif /* CONFIG_BT_COEXIST */

	arg[0] = mac_id;
	arg[1] = psta->raid;
	arg[2] = shortGIrate;
	arg[3] = psta->init_rate;

	rtl8822b_set_FwMacIdConfig_cmd(adapter, mask, arg);
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

	rtw_write16(adapter, REG_TBTT_PROHIBIT_8822B, 0x6404); /* ms */

	rtw_write8(adapter, REG_DRVERLYINT_8822B, DRIVER_EARLY_INT_TIME_8822B); /* 5ms */
	rtw_write8(adapter, REG_BCNDMATIM_8822B, BCN_DMA_ATIME_INT_TIME_8822B); /* 2ms */

	/*
	 * Suggested by designer timchen. Change beacon AIFS to the largest number
	 * beacause test chip does not contension before sending beacon.
	 */
	rtw_write16(adapter, REG_BCNTCFG_8822B, 0x660F);

	hal->RegBcnCtrlVal = rtw_read8(adapter, REG_BCN_CTRL_8822B);
	hal->RegTxPause = rtw_read8(adapter, REG_TXPAUSE_8822B);
	hal->RegFwHwTxQCtrl = rtw_read8(adapter, REG_FWHW_TXQ_CTRL_8822B + 2);
	hal->RegReg542 = rtw_read8(adapter, REG_TBTT_PROHIBIT_8822B + 2);
	hal->RegCR_1 = rtw_read8(adapter, REG_CR_8822B + 1);
}

void rtl8822b_resume_tx_beacon(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	hal->RegFwHwTxQCtrl |= (BIT_EN_BCNQ_DL_8822B >> 16);
	rtw_write8(adapter, REG_FWHW_TXQ_CTRL_8822B + 2, hal->RegFwHwTxQCtrl);

	rtw_write8(adapter, REG_TBTT_PROHIBIT_8822B + 1, 0xff);
	hal->RegReg542 |= BIT(0);
	rtw_write8(adapter, REG_TBTT_PROHIBIT_8822B + 2, hal->RegReg542);
}

void rtl8822b_stop_tx_beacon(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);


	hal->RegFwHwTxQCtrl &= ~(BIT_EN_BCNQ_DL_8822B >> 16);
	rtw_write8(adapter, REG_FWHW_TXQ_CTRL_8822B + 2, hal->RegFwHwTxQCtrl);

	rtw_write8(adapter, REG_TBTT_PROHIBIT_8822B + 1, 0x64);
	hal->RegReg542 &= ~BIT(0);
	rtw_write8(adapter, REG_TBTT_PROHIBIT_8822B + 2, hal->RegReg542);
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
	 * REG_BCN_INTERVAL
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
#ifdef CONFIG_CONCURRENT_MODE
	/* Port 1 bcn interval */
	if (adapter->hw_port == HW_PORT1) {
		u16 val16;

		val16 = rtw_read16(adapter, (REG_MBSSID_BCN_SPACE_8822B + 2));
		val16 |= (pmlmeinfo->bcn_interval & BIT_MASK_BCN_SPACE_CLINT0_8822B);
		rtw_write16(adapter, REG_MBSSID_BCN_SPACE_8822B + 2, val16);
	} else
#endif
		/* Port 0 bcn interval */
		rtw_write16(adapter, REG_MBSSID_BCN_SPACE_8822B, pmlmeinfo->bcn_interval);

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

	rtl8822b_resume_tx_beacon(adapter);
}

/*
 * arg[0] = macid
 * arg[1] = raid
 * arg[2] = shortGIrate
 * arg[3] = init_rate
 */
void add_rateatid(PADAPTER adapter, u64 rate_bitmap, u8 *arg, u8 rssi_level)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	u64 *dm_RA_Mask = NULL;
	u8 *dm_RteID = NULL;
	u8 macid;

	macid = arg[0];

	if (rssi_level != DM_RATR_STA_INIT)
		rate_bitmap = PhyDM_Get_Rate_Bitmap_Ex(&hal->odmpriv, macid, rate_bitmap, rssi_level, dm_RA_Mask, dm_RteID);

	rtl8822b_set_FwMacIdConfig_cmd(adapter, rate_bitmap, arg);
}

static void xmit_status_check(PADAPTER p)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(p);
	struct sreset_priv *psrtpriv = &hal->srestpriv;
	struct xmit_priv *pxmitpriv = &p->xmitpriv;
	unsigned long current_time = 0;
	unsigned int diff_time = 0;
	u32 txdma_status = 0;

	txdma_status = rtw_read32(p, REG_TXDMA_STATUS_8822B);
	if (txdma_status != 0x00) {
		RTW_INFO("%s REG_TXDMA_STATUS:0x%08x\n", __FUNCTION__, txdma_status);
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

					if (!(ability & ODM_BB_ADAPTIVITY))
						rtw_hal_sreset_reset(p);
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

static void linked_status_check(PADAPTER p)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(p);
	struct sreset_priv *psrtpriv = &hal->srestpriv;
	u32 rx_dma_status = 0;

	rx_dma_status = rtw_read32(p, REG_RXDMA_STATUS_8822B);
	if (rx_dma_status != 0x00)
		RTW_INFO("%s REG_RXDMA_STATUS:0x%08x\n", __FUNCTION__, rx_dma_status);

	if (psrtpriv->dbg_trigger_point == SRESET_TGP_LINK_STATUS) {
		psrtpriv->dbg_trigger_point = SRESET_TGP_NULL;
		rtw_hal_sreset_reset(p);
		return;
	}
}

#ifdef CONFIG_TSF_RESET_OFFLOAD
static int reset_tsf(PADAPTER adapter, u8 reset_port)
{
	u8 reset_cnt_before = 0, reset_cnt_after = 0, loop_cnt = 0;
	u32 reg_reset_tsf_cnt = (HW_PORT0 == reset_port) ?
				REG_FW_RESET_TSF_CNT_0 : REG_FW_RESET_TSF_CNT_1;


	/* site survey will cause reset_tsf fail */
	rtw_mi_buddy_scan_abort(adapter, _FALSE);
	reset_cnt_after = reset_cnt_before = rtw_read8(adapter, reg_reset_tsf_cnt);
	rtl8822b_reset_tsf(adapter, reset_port);

	while ((reset_cnt_after == reset_cnt_before) && (loop_cnt < 10)) {
		rtw_msleep_os(100);
		loop_cnt++;
		reset_cnt_after = rtw_read8(adapter, reg_reset_tsf_cnt);
	}

	return (loop_cnt >= 10) ? _FAIL : _TRUE;
}
#endif /* CONFIG_TSF_RESET_OFFLOAD */

static void set_opmode_monitor(PADAPTER adapter)
{
	u32 rcr_bits;
	u16 value_rxfltmap2;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;


	/* Leave IPS */
	rtw_pm_set_ips(adapter, IPS_NONE);
	LeaveAllPowerSaveMode(adapter);

	/* Receive all type */
	rcr_bits = BIT_AAP_8822B | BIT_APM_8822B | BIT_AM_8822B
		   | BIT_AB_8822B | BIT_APWRMGT_8822B
		   | BIT_APP_PHYSTS_8822B;

	/* Append FCS */
	rcr_bits |= BIT_APP_FCS_8822B;

	/* Receive all data frames */
	value_rxfltmap2 = 0xFFFF;

	rtl8822b_rcr_config(adapter, rcr_bits);

	rtw_write16(adapter, REG_RXFLTMAP_8822B, value_rxfltmap2);
}

static void set_opmode_port0(PADAPTER adapter, u8 mode)
{
	u8 is_ap_exist;
	u8 val8;
	u32 val32;

#ifdef CONFIG_CONCURRENT_MODE
	is_ap_exist = rtw_mi_check_status(adapter, MI_AP_MODE);
#else /* !CONFIG_CONCURRENT_MODE */
	is_ap_exist = _FALSE;
#endif /* !CONFIG_CONCURRENT_MODE */

	/* disable Port0 TSF update */
	val8 = rtw_read8(adapter, REG_BCN_CTRL_8822B);
	val8 |= BIT_DIS_TSF_UDT_8822B;
	rtw_write8(adapter, REG_BCN_CTRL_8822B, val8);

	Set_MSR(adapter, mode);

	RTW_INFO(FUNC_ADPT_FMT ": hw_port(%d) mode=%d\n",
		 FUNC_ADPT_ARG(adapter), adapter->hw_port, mode);

	switch (mode) {
	case _HW_STATE_NOLINK_:
	case _HW_STATE_STATION_:
		if (_FALSE == is_ap_exist) {
			rtl8822b_stop_tx_beacon(adapter);
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

		/* clear rx ctrl frame */
		rtw_write16(adapter, REG_RXFLTMAP1_8822B, 0);
		break;

	case _HW_STATE_ADHOC_:
		rtl8822b_resume_tx_beacon(adapter);
		val8 = BIT_DIS_TSF_UDT_8822B | BIT_EN_BCN_FUNCTION_8822B;
		rtw_write8(adapter, REG_BCN_CTRL_8822B, val8);

		/* clear rx ctrl frame */
		rtw_write16(adapter, REG_RXFLTMAP1_8822B, 0);
		break;

	case _HW_STATE_AP_:
#ifdef CONFIG_PCI_HCI
		UpdateInterruptMask8822BE(adapter, RT_BCN_INT_MASKS, 0, 0, 0);
#endif /* CONFIG_PCI_HCI */

		rtl8822b_resume_tx_beacon(adapter);

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
		val8 &= (~BIT_P0_EN_RXBCN_RPT_8822B);
		rtw_write8(adapter, REG_FWHW_TXQ_CTRL_8822B, val8);

		/* enable BCN0 Function */
		val8 = rtw_read8(adapter, REG_BCN_CTRL_8822B);
		val8 |= BIT_EN_BCN_FUNCTION_8822B | BIT_DIS_TSF_UDT_8822B | BIT_P0_EN_TXBCN_RPT_8822B;
		rtw_write8(adapter, REG_BCN_CTRL_8822B, val8);

		/* Enable ATIM */
		val8 = rtw_read8(adapter, REG_DIS_ATIM_8822B);
		val8 &= ~BIT_DIS_ATIM_ROOT_8822B;
		rtw_write8(adapter, REG_DIS_ATIM_8822B, val8);

		/* Enable HW seq for BCN
		   0x4FC[0]: EN_HWSEQ
		   0x4FC[1]: EN_HWSEQEXT
		   According TX desc
		*/
		rtw_write8(adapter, REG_DUMMY_PAGE4_V1_8822B, 0x01);

		/* Set RCR */
		/* CBSSID_DATA must set to 0, reject ICV_ERR packet */
		rtl8822b_rcr_clear(adapter, BIT_CBSSID_DATA_8822B);

		/* enable to rx data frame */
		rtw_write16(adapter, REG_RXFLTMAP_8822B, 0xFFFF);
		/* enable to rx ps-poll */
		rtw_write16(adapter, REG_RXFLTMAP1_8822B, 0x0400);

		/* Beacon Control related register for first time */
		rtw_write8(adapter, REG_BCNDMATIM_8822B, 0x02); /* 2ms */

		rtw_write8(adapter, REG_ATIMWND_8822B, 0x0a); /* 10ms */
		rtw_write16(adapter, REG_BCNTCFG_8822B, 0x00);
		rtw_write16(adapter, REG_TBTT_PROHIBIT_8822B, 0xff04);
		rtw_write16(adapter, REG_TSFTR_SYN_OFFSET_8822B, 0x7fff); /* +32767 (~32ms) */

		/* reset TSF */
		rtw_write8(adapter, REG_DUAL_TSF_RST_8822B, BIT_TSFTR_RST_8822B);

		/* SW_BCN_SEL - Port0 */
		rtw_hal_set_hwreg(adapter, HW_VAR_DL_BCN_SEL, NULL);

		/* select BCN on port 0 */
		val8 = rtw_read8(adapter, REG_CCK_CHECK_8822B);
		val8 &= ~BIT_BCN_PORT_SEL_8822B;
		rtw_write8(adapter, REG_CCK_CHECK_8822B, val8);

#ifdef CONFIG_TSF_RESET_OFFLOAD
		/* Reset TSF for STA+AP concurrent mode */
		if (rtw_mi_buddy_check_fwstate(adapter, (WIFI_STATION_STATE | WIFI_ASOC_STATE))) {
			if (reset_tsf(adapter, HW_PORT0) == _FALSE)
				RTW_INFO(FUNC_ADPT_FMT ": ERROR! Reset port0 TSF fail\n",
					 FUNC_ADPT_ARG(adapter));
		}
#endif /* CONFIG_TSF_RESET_OFFLOAD */

		break;
	}
}

static void set_opmode_port1(PADAPTER adapter, u8 mode)
{
#ifdef CONFIG_CONCURRENT_MODE
	u8 is_ap_exist;
	u8 val8;

	is_ap_exist = rtw_mi_check_status(adapter, MI_AP_MODE);

	/* disable Port1 TSF update */
	val8 = rtw_read8(adapter, REG_BCN_CTRL_CLINT0_8822B);
	val8 |= BIT_CLI0_DIS_TSF_UDT_8822B;
	rtw_write8(adapter, REG_BCN_CTRL_CLINT0_8822B, val8);

	Set_MSR(adapter, mode);

	RTW_INFO(FUNC_ADPT_FMT ": hw_port(%d) mode=%d\n",
		 FUNC_ADPT_ARG(adapter), adapter->hw_port, mode);

	switch (mode) {
	case _HW_STATE_NOLINK_:
	case _HW_STATE_STATION_:
		if (_FALSE == is_ap_exist) {
			rtl8822b_stop_tx_beacon(adapter);
#ifdef CONFIG_PCI_HCI
			UpdateInterruptMask8822BE(adapter, 0, 0, RT_BCN_INT_MASKS, 0);
#endif /* CONFIG_PCI_HCI */
		}

		/* disable beacon function */
		val8 = BIT_CLI0_DIS_TSF_UDT_8822B | BIT_CLI0_EN_BCN_FUNCTION_8822B;
		rtw_write8(adapter, REG_BCN_CTRL_CLINT0_8822B, val8);

		/* clear rx ctrl frame */
		rtw_write16(adapter, REG_RXFLTMAP1_8822B, 0);
		break;

	case _HW_STATE_ADHOC_:
		rtl8822b_resume_tx_beacon(adapter);
		val8 = BIT_CLI0_DIS_TSF_UDT_8822B | BIT_CLI0_EN_BCN_FUNCTION_8822B;
		rtw_write8(adapter, REG_BCN_CTRL_CLINT0_8822B, val8);

		/* clear rx ctrl frame */
		rtw_write16(adapter, REG_RXFLTMAP1_8822B, 0);
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

static void hw_var_set_opmode(PADAPTER adapter, u8 mode)
{
	u8 val8;
	static u8 isMonitor = _FALSE;


	if (isMonitor == _TRUE) {
		u32 rcr;
		/* reset RCR */
		rcr = BIT_APM_8822B
		      | BIT_AM_8822B
		      | BIT_AB_8822B
		      | BIT_CBSSID_DATA_8822B
		      | BIT_CBSSID_BCN_8822B
		      | BIT_HTC_LOC_CTRL_8822B
		      | BIT_VHT_DACK_8822B
		      | BIT_APP_PHYSTS_8822B
		      | BIT_APP_ICV_8822B
		      | BIT_APP_MIC_8822B
		      ;
		rtl8822b_rcr_config(adapter, rcr);
		isMonitor = _FALSE;
	}

	if (mode == _HW_STATE_MONITOR_) {
		isMonitor = _TRUE;

		Set_MSR(adapter, _HW_STATE_NOLINK_);
		set_opmode_monitor(adapter);
		return;
	}

	/* clear crc bit */
	if (rtl8822b_rcr_check(adapter, BIT_ACRC32_8822B))
		rtl8822b_rcr_clear(adapter, BIT_ACRC32_8822B);

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
}

static void hw_var_set_macaddr(PADAPTER adapter, u8 *val)
{
	u8 port;


	port = adapter->hw_port;
	rtw_halmac_set_mac_address(adapter_to_dvobj(adapter), port, val);
}

static void hw_var_set_bssid(PADAPTER adapter, u8 *val)
{
	u8 idx = 0;
	u32 reg_bssid;

#ifdef CONFIG_CONCURRENT_MODE
	if (adapter->hw_port == HW_PORT1)
		reg_bssid = REG_BSSID1_8822B;
	else
#endif
		reg_bssid = REG_BSSID_8822B;

	for (idx = 0; idx < 6; idx++)
		rtw_write8(adapter, (reg_bssid + idx), val[idx]);
}

static void hw_var_set_basic_rate(PADAPTER adapter, u8 *ratetbl)
{
#define RATE_1M		BIT(0)
#define RATE_2M		BIT(1)
#define RATE_5_5M	BIT(2)
#define RATE_11M	BIT(3)
#define RATE_6M		BIT(4)
#define RATE_9M		BIT(5)
#define RATE_12M	BIT(6)
#define RATE_18M	BIT(7)
#define RATE_24M	BIT(8)
#define RATE_36M	BIT(9)
#define RATE_48M	BIT(10)
#define RATE_54M	BIT(11)
#define RATE_MCS0	BIT(12)
#define RATE_MCS1	BIT(13)
#define RATE_MCS2	BIT(14)
#define RATE_MCS3	BIT(15)
#define RATE_MCS4	BIT(16)
#define RATE_MCS5	BIT(17)
#define RATE_MCS6	BIT(18)
#define RATE_MCS7	BIT(19)

#define RATES_CCK	(RATE_11M | RATE_5_5M | RATE_2M | RATE_1M)
#define RATES_OFDM	(RATE_54M | RATE_48M | RATE_36M | RATE_24M | RATE_18M | RATE_12M | RATE_9M | RATE_6M)

	struct mlme_ext_info *mlmext_info = &adapter->mlmeextpriv.mlmext_info;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	u16 input_b = 0, masked = 0, ioted = 0, BrateCfg = 0;
	u16 rrsr_2g_force_mask = RATES_CCK;
	u16 rrsr_2g_allow_mask = RATE_24M | RATE_12M | RATE_6M | RATES_CCK;
	u16 rrsr_5g_force_mask = RATE_6M;
	u16 rrsr_5g_allow_mask = RATES_OFDM;
	u32 val32;

	HalSetBrateCfg(adapter, ratetbl, &BrateCfg);
	input_b = BrateCfg;

	/* apply force and allow mask */
	if (hal->CurrentBandType == BAND_ON_2_4G) {
		BrateCfg |= rrsr_2g_force_mask;
		BrateCfg &= rrsr_2g_allow_mask;
	} else {
		BrateCfg |= rrsr_5g_force_mask;
		BrateCfg &= rrsr_5g_allow_mask;
	}

	masked = BrateCfg;

	/* IOT consideration */
	if (mlmext_info->assoc_AP_vendor == HT_IOT_PEER_CISCO) {
		/* if peer is cisco and didn't use ofdm rate, we enable 6M ack */
		if ((BrateCfg & (RATE_24M | RATE_12M | RATE_6M)) == 0)
			BrateCfg |= RATE_6M;
	}

	ioted = BrateCfg;

	hal->BasicRateSet = BrateCfg;

	RTW_INFO("[HW_VAR_BASIC_RATE] %#x->%#x->%#x\n", input_b, masked, ioted);

	/* Set RRSR rate table. */
	val32 = rtw_read32(adapter, REG_RRSR_8822B);
	val32 &= ~(BIT_MASK_RRSC_BITMAP << BIT_SHIFT_RRSC_BITMAP);
	val32 |= BIT_RRSC_BITMAP(BrateCfg);
	val32 = rtw_write32(adapter, REG_RRSR_8822B, val32);
}

static void hw_var_set_bcn_func(PADAPTER adapter, u8 enable)
{
	u32 bcn_ctrl_reg;
	u8 val8;

#ifdef CONFIG_CONCURRENT_MODE
	if (adapter->hw_port == HW_PORT1)
		bcn_ctrl_reg = REG_BCN_CTRL_CLINT0_8822B;
	else
#endif
		bcn_ctrl_reg = REG_BCN_CTRL_8822B;

	if (enable) {
		/* enable TX BCN report
		 *  Reg REG_FWHW_TXQ_CTRL_8822B[2] = 1
		 *  Reg REG_BCN_CTRL_8822B[3][5] = 1
		 */
		val8 = rtw_read8(adapter, REG_FWHW_TXQ_CTRL_8822B);
		val8 |= BIT_EN_BCN_TRXRPT_V1_8822B;
		rtw_write8(adapter, REG_FWHW_TXQ_CTRL_8822B, val8);

		if (bcn_ctrl_reg == REG_BCN_CTRL_CLINT0_8822B) {
			/* setting port 1 */
			val8 = rtw_read8(adapter, REG_BCN_CTRL_8822B);
			val8 |= BIT_EN_BCN_FUNCTION_8822B | BIT_P0_EN_TXBCN_RPT_8822B;
			rtw_write8(adapter, REG_BCN_CTRL_8822B, val8);

			val8 = rtw_read8(adapter, REG_BCN_CTRL_CLINT0_8822B);
			val8 |= BIT_CLI0_EN_BCN_FUNCTION_8822B;
			rtw_write8(adapter, bcn_ctrl_reg, val8);
		} else {
			/* setting port 0 */
			val8 = BIT_EN_BCN_FUNCTION_8822B | BIT_P0_EN_TXBCN_RPT_8822B;
			rtw_write8(adapter, bcn_ctrl_reg, val8);
		}
	} else {
		val8 = rtw_read8(adapter, bcn_ctrl_reg);
		val8 &= ~(BIT_EN_BCN_FUNCTION_8822B | BIT_P0_EN_TXBCN_RPT_8822B);
#ifdef CONFIG_BT_COEXIST
		/* Always enable port0 beacon function for PSTDMA */
		if (GET_HAL_DATA(adapter)->EEPROMBluetoothCoexist) {
			if (REG_BCN_CTRL_8822B == bcn_ctrl_reg)
				val8 |= BIT_EN_BCN_FUNCTION_8822B;
		}
#endif /* CONFIG_BT_COEXIST */
		rtw_write8(adapter, bcn_ctrl_reg, val8);
	}
}

static void hw_var_set_correct_tsf(PADAPTER adapter)
{
#ifdef CONFIG_MI_WITH_MBSSID_CAM
	/* do nothing */
#else /* !CONFIG_MI_WITH_MBSSID_CAM */
	u64 tsf;
	struct mlme_ext_priv *pmlmeext;
	struct mlme_ext_info *pmlmeinfo;


	pmlmeext = &adapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;

	tsf = pmlmeext->TSFValue - rtw_modular64(pmlmeext->TSFValue, (pmlmeinfo->bcn_interval * 1024)) - 1024; /* us */

	if (((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE)
	    || ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE))
		rtl8822b_stop_tx_beacon(adapter);

	rtw_hal_correct_tsf(adapter, adapter->hw_port, tsf);

#ifdef CONFIG_CONCURRENT_MODE
	/* Update buddy port's TSF if it is SoftAP for beacon TX issue!*/
	if (((pmlmeinfo->state & 0x03) == WIFI_FW_STATION_STATE)
	    && (rtw_mi_check_status(adapter, MI_AP_MODE) == _TRUE)) {
		struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
		u32 i;
		PADAPTER iface;

		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			if (!iface)
				continue;
			if (iface == adapter)
				continue;

			if ((check_fwstate(&iface->mlmepriv, WIFI_AP_STATE) == _TRUE)
			    && (check_fwstate(&iface->mlmepriv, WIFI_ASOC_STATE) == _TRUE)) {
				rtw_hal_correct_tsf(iface, iface->hw_port, tsf);
#ifdef CONFIG_TSF_RESET_OFFLOAD
				if (reset_tsf(iface, iface->hw_port) == _FALSE)
					RTW_INFO(FUNC_ADPT_FMT": [ERROR] Reset port%d TSF fail!\n",
						FUNC_ADPT_ARG(iface), iface->hw_port);
#endif	/* CONFIG_TSF_RESET_OFFLOAD */
			}
		}
	}
#endif /* CONFIG_CONCURRENT_MODE */

	if (((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE)
	    || ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE))
		rtl8822b_resume_tx_beacon(adapter);
#endif /* !CONFIG_MI_WITH_MBSSID_CAM */
}

static void hw_var_set_check_bssid(PADAPTER adapter, u8 enable)
{
	u32 rcr;

	rcr = BIT_CBSSID_DATA_8822B | BIT_CBSSID_BCN_8822B;
	if (enable)
		rtl8822b_rcr_add(adapter, rcr);
	else
		rtl8822b_rcr_clear(adapter, rcr);

	RTW_INFO("%s: [HW_VAR_CHECK_BSSID] 0x%x=0x%x\n", __FUNCTION__,
		 REG_RCR_8822B, rtw_read32(adapter, REG_RCR_8822B));
}

static void hw_var_set_mlme_disconnect(PADAPTER adapter)
{
	u8 val8;
	u8 ld_sta_num = 0, lg_sta_num = 0;

#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_mi_check_status(adapter, MI_LINKED) == _FALSE)
#endif
		/* reject all data frames under not link state */
		rtw_write16(adapter, REG_RXFLTMAP_8822B, 0);

#ifdef CONFIG_CONCURRENT_MODE
	if (adapter->hw_port == HW_PORT1) {
		/* reset TSF1(CLINT0) */
		rtw_write8(adapter, REG_DUAL_TSF_RST_8822B, BIT_TSFTR_CLI0_RST_8822B);

		/* disable update TSF1(CLINT0) */
		val8 = rtw_read8(adapter, REG_BCN_CTRL_CLINT0_8822B);
		val8 |= BIT_CLI0_DIS_TSF_UDT_8822B;
		rtw_write8(adapter, REG_BCN_CTRL_CLINT0_8822B, val8);

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
		val8 = rtw_read8(adapter, REG_BCN_CTRL_8822B);
		val8 |= BIT_DIS_TSF_UDT_8822B;
		rtw_write8(adapter, REG_BCN_CTRL_8822B, val8);
	}

	rtw_mi_status_no_self(adapter, NULL, &ld_sta_num, &lg_sta_num, NULL, NULL, NULL);

	/* clear update TSF only BSSID match for no linked station */
	if (ld_sta_num == 0 && lg_sta_num == 0)
		rtl8822b_rx_tsf_addr_filter_config(adapter, 0);
}

static void hw_var_set_mlme_sitesurvey(PADAPTER adapter, u8 enable)
{
	struct dvobj_priv *dvobj;
	PHAL_DATA_TYPE hal;
	struct mlme_priv *pmlmepriv;
	PADAPTER iface;
	u32 rcr_bit, reg_bcn_ctl;
	u16 value_rxfltmap2;
	u8 val8, i;


	dvobj = adapter_to_dvobj(adapter);
	hal = GET_HAL_DATA(adapter);
	pmlmepriv = &adapter->mlmepriv;

#ifdef CONFIG_FIND_BEST_CHANNEL
	rcr_bit = BIT_CBSSID_BCN_8822B | BIT_CBSSID_DATA_8822B;

	/* Receive all data frames */
	value_rxfltmap2 = 0xFFFF;
#else /* CONFIG_FIND_BEST_CHANNEL */
	rcr_bit = BIT_CBSSID_BCN_8822B;

	/* config RCR to receive different BSSID & not to receive data frame */
	value_rxfltmap2 = 0;
#endif /* CONFIG_FIND_BEST_CHANNEL */

	if (rtw_mi_check_fwstate(adapter, WIFI_AP_STATE))
		rcr_bit = BIT_CBSSID_BCN_8822B;
#ifdef CONFIG_TDLS
	/* TDLS will clear RCR_CBSSID_DATA bit for connection. */
	else if (adapter->tdlsinfo.link_established == _TRUE)
		rcr_bit = BIT_CBSSID_BCN_8822B;
#endif /* CONFIG_TDLS */

	if (enable) {
		/*
		 * 1. configure REG_RXFLTMAP2
		 * 2. config RCR to receive different BSSID BCN or probe rsp
		 */

		rtw_write16(adapter, REG_RXFLTMAP_8822B, value_rxfltmap2);

		rtl8822b_rcr_clear(adapter, rcr_bit);

		/* Save orignal RRSR setting. */
		hal->RegRRSR = rtw_read16(adapter, REG_RRSR_8822B);

		if (rtw_mi_check_status(adapter, MI_AP_MODE))
			rtl8822b_stop_tx_beacon(adapter);
	} else {
		/* sitesurvey done
		 * 1. enable rx data frame
		 * 2. config RCR not to receive different BSSID BCN or probe rsp
		 */

		if (rtw_mi_check_fwstate(adapter, _FW_LINKED | WIFI_AP_STATE))
			/* enable to rx data frame */
			rtw_write16(adapter, REG_RXFLTMAP_8822B, 0xFFFF);

#ifdef CONFIG_MI_WITH_MBSSID_CAM
		rtl8822b_rcr_clear(adapter, BIT_CBSSID_BCN_8822B | BIT_CBSSID_DATA_8822B);
#else /* CONFIG_MI_WITH_MBSSID_CAM */
		rtl8822b_rcr_add(adapter, rcr_bit);
#endif /* CONFIG_MI_WITH_MBSSID_CAM */

		/* Restore orignal RRSR setting. */
		rtw_write16(adapter, REG_RRSR_8822B, hal->RegRRSR);

		if (rtw_mi_check_status(adapter, MI_AP_MODE)) {
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

	RetryLimit = 0x30;
	hal = GET_HAL_DATA(adapter);
	pmlmepriv = &adapter->mlmepriv;


#ifdef CONFIG_CONCURRENT_MODE
	if (type == 0) {
		/* prepare to join */
		if (rtw_mi_check_status(adapter, MI_AP_MODE))
			rtl8822b_stop_tx_beacon(adapter);

		/* enable to rx data frame.Accept all data frame */
		rtw_write16(adapter, REG_RXFLTMAP_8822B, 0xFFFF);

#ifdef CONFIG_MI_WITH_MBSSID_CAM
		val32 = BIT_CBSSID_DATA_8822B | BIT_CBSSID_BCN_8822B;
		rtl8822b_rcr_clear(adapter, val32);
#else /* !CONFIG_MI_WITH_MBSSID_CAM */
		if (rtw_mi_check_status(adapter, MI_AP_MODE))
			val32 = BIT_CBSSID_BCN_8822B;
		else
			val32 = BIT_CBSSID_DATA_8822B | BIT_CBSSID_BCN_8822B;
		rtl8822b_rcr_add(adapter, val32);
#endif /* !CONFIG_MI_WITH_MBSSID_CAM */

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
			RetryLimit = (hal->CustomerID == RT_CID_CCX) ? 7 : 48;
		else /* Ad-hoc Mode */
			RetryLimit = 0x7;

		/*
		 * for 8822B, must enable BCN function if BIT_CBSSID_BCN_8822B(bit 7) of REG_RCR(0x608) is enable to recv BSSID bcn
		 */
		hw_var_set_bcn_func(adapter, _TRUE);

		/* update TSF only BSSID match for station mode */
		rtl8822b_rx_tsf_addr_filter_config(adapter, BIT_CHK_TSF_EN_8822B | BIT_CHK_TSF_CBSSID_8822B);
	} else if (type == 1) {
		/* joinbss_event call back when join res < 0 */
		if (rtw_mi_check_status(adapter, MI_LINKED) == _FALSE)
			rtw_write16(adapter, REG_RXFLTMAP_8822B, 0x00);

		if (rtw_mi_check_status(adapter, MI_AP_MODE)) {
			rtl8822b_resume_tx_beacon(adapter);

			/* reset TSF 1/2 after resume_tx_beacon */
			val8 = BIT_TSFTR_RST_8822B | BIT_TSFTR_CLI0_RST_8822B;
			rtw_write8(adapter, REG_DUAL_TSF_RST_8822B, val8);
		}
	} else if (type == 2) {
		/* sta add event callback */

#ifdef CONFIG_MI_WITH_MBSSID_CAM
#else /* !CONFIG_MI_WITH_MBSSID_CAM */
		/* enable update TSF */
		if (adapter->hw_port == HW_PORT1) {
			val8 = rtw_read8(adapter, REG_BCN_CTRL_CLINT0_8822B);
			val8 &= ~BIT_DIS_TSF_UDT_8822B;
			rtw_write8(adapter, REG_BCN_CTRL_CLINT0_8822B, val8);
		} else {
			val8 = rtw_read8(adapter, REG_BCN_CTRL_8822B);
			val8 &= ~BIT_DIS_TSF_UDT_8822B;
			rtw_write8(adapter, REG_BCN_CTRL_8822B, val8);
		}
#endif /* !CONFIG_MI_WITH_MBSSID_CAM */

		if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE)) {
			rtw_write8(adapter, 0x542, 0x02);
			RetryLimit = 0x7;
		}

		if (rtw_mi_check_status(adapter, MI_AP_MODE)) {
			rtl8822b_resume_tx_beacon(adapter);

			/* reset TSF 1/2 after resume_tx_beacon */
			rtw_write8(adapter, REG_DUAL_TSF_RST_8822B, BIT_TSFTR_RST_8822B | BIT_TSFTR_CLI0_RST_8822B);
		}
	}

	val16 = BIT_LRL_8822B(RetryLimit) | BIT_SRL_8822B(RetryLimit);
	rtw_write16(adapter, REG_RETRY_LIMIT_8822B, val16);
#else /* !CONFIG_CONCURRENT_MODE */
	if (type == 0) {
		/* prepare to join */

		/* enable to rx data frame.Accept all data frame */
		rtw_write16(adapter, REG_RXFLTMAP_8822B, 0xFFFF);

		hw_var_set_check_bssid(adapter, !adapter->in_cta_test);

		/*
		 * for 8822B, must enable BCN function if BIT_CBSSID_BCN_8822B(bit 7) of REG_RCR(0x608) is enable to recv BSSID bcn
		 */
		hw_var_set_bcn_func(adapter, _TRUE);

		/* update TSF only BSSID match for station mode */
		rtl8822b_rx_tsf_addr_filter_config(adapter, BIT_CHK_TSF_EN_8822B | BIT_CHK_TSF_CBSSID_8822B);

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
			RetryLimit = (hal->CustomerID == RT_CID_CCX) ? 7 : 48;
		else /* Ad-hoc Mode */
			RetryLimit = 0x7;
	} else if (type == 1) {
		/* joinbss_event call back when join res < 0 */
		rtw_write16(adapter, REG_RXFLTMAP_8822B, 0x00);
	} else if (type == 2) {
		/* sta add event callback */

		/* enable update TSF */
		val8 = rtw_read8(adapter, REG_BCN_CTRL_8822B);
		val8 &= ~BIT_DIS_TSF_UDT_8822B;
		rtw_write8(adapter, REG_BCN_CTRL_8822B, val8);

		if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE))
			RetryLimit = 0x7;
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

static void hw_var_set_rcr_am(PADAPTER adapter, u8 enable)
{
	u32 rcr;

	rcr = BIT_AM_8822B;
	if (enable)
		rtl8822b_rcr_add(adapter, rcr);
	else
		rtl8822b_rcr_clear(adapter, rcr);

	RTW_INFO("%s: [HW_VAR_ON_RCR_AM] RCR(0x%x)=0x%x\n",
		__FUNCTION__, REG_RCR_8822B, rtw_read32(adapter, REG_RCR_8822B));
}

static void hw_var_set_bcn_interval(PADAPTER adapter, u16 bcn_interval)
{
	u16 val16 = 0;

#ifdef CONFIG_CONCURRENT_MODE
	if (adapter->hw_port == HW_PORT1) {
		/* Port 1(clint 0) */
		val16 = rtw_read16(adapter, (REG_MBSSID_BCN_SPACE_8822B + 2));
		val16 |= (bcn_interval & BIT_MASK_BCN_SPACE_CLINT0_8822B);
		rtw_write16(adapter, REG_MBSSID_BCN_SPACE_8822B + 2, val16);
	} else
#endif
	{
		/* Port 0 */
		val16 = rtw_read16(adapter, (REG_MBSSID_BCN_SPACE_8822B));
		val16 |= (bcn_interval & BIT_MASK_BCN_SPACE0_8822B);
		rtw_write16(adapter, REG_MBSSID_BCN_SPACE_8822B, val16);
	}

	RTW_INFO("%s: [HW_VAR_BEACON_INTERVAL] 0x%x=0x%x\n", __FUNCTION__,
		REG_MBSSID_BCN_SPACE_8822B, rtw_read32(adapter, REG_MBSSID_BCN_SPACE_8822B));
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

static void hw_var_set_cam_empty_entry(PADAPTER adapter, u8 ucIndex)
{
	u8 i;
	u32 ulCommand = 0;
	u32 ulContent = 0;
	u32 ulEncAlgo = CAM_AES;

	for (i = 0; i < CAM_CONTENT_COUNT; i++) {
		/* filled id in CAM config 2 byte */
		if (i == 0)
			ulContent |= (ucIndex & 0x03) | ((u16)(ulEncAlgo) << 2);
		else
			ulContent = 0;

		/* polling bit, and No Write enable, and address */
		ulCommand = CAM_CONTENT_COUNT * ucIndex + i;
		ulCommand |= BIT_SECCAM_POLLING_8822B | BIT_SECCAM_WE_8822B;
		/* write content 0 is equall to mark invalid */
		rtw_write32(adapter, REG_CAMWRITE_8822B, ulContent);
		rtw_write32(adapter, REG_CAMCMD_8822B, ulCommand);
	}
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


	RTW_INFO(FUNC_ADPT_FMT ":+ hw_port=%d mstatus(%x)\n",
		 FUNC_ADPT_ARG(adapter), get_hw_port(adapter), mstatus);

	if (mstatus == RT_MEDIA_CONNECT) {
#if 0
		u8 bRecover = _FALSE;
#endif
		u8 v8;

		/* We should set AID, correct TSF, HW seq enable before set JoinBssReport to Fw in 88/92C. */
		rtw_write16(adapter, REG_BCN_PSR_RPT_8822B, (0xC000 | pmlmeinfo->aid));

		/* Enable SW TX beacon */
		v8 = rtw_read8(adapter, REG_CR_8822B + 1);
		v8 |= (BIT_ENSWBCN_8822B >> 8);
		rtw_write8(adapter, REG_CR_8822B + 1, v8);

		/*
		 * Disable Hw protection for a time which revserd for Hw sending beacon.
		 * Fix download reserved page packet fail that access collision with the protection time.
		 */
		val8 = rtw_read8(adapter, REG_BCN_CTRL_8822B);
		val8 &= ~BIT_EN_BCN_FUNCTION_8822B;
		val8 |= BIT_DIS_TSF_UDT_8822B;
		rtw_write8(adapter, REG_BCN_CTRL_8822B, val8);

#if 0
		/* Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame. */
		if (hal->RegFwHwTxQCtrl & BIT(6))
			bRecover = _TRUE;

		/* To tell Hw the packet is not a real beacon frame. */
		val8 = hal->RegFwHwTxQCtrl & ~BIT(6);
		rtw_write8(adapter, REG_FWHW_TXQ_CTRL_8822B + 2, val8);
		hal->RegFwHwTxQCtrl &= ~BIT(6);
#endif

		/* Clear beacon valid check bit. */
		rtw_hal_set_hwreg(adapter, HW_VAR_BCN_VALID, NULL);
		rtw_hal_set_hwreg(adapter, HW_VAR_DL_BCN_SEL, NULL);

		DLBcnCount = 0;
		poll = 0;
		do {
			/* download rsvd page. */
			rtw_hal_set_fw_rsvd_page(adapter, 0);
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
			RTW_INFO(ADPT_FMT ": DL RSVD page success! DLBcnCount:%u, poll:%u\n",
				 ADPT_ARG(adapter), DLBcnCount, poll);
		}

		val8 = rtw_read8(adapter, REG_BCN_CTRL_8822B);
		val8 |= BIT_EN_BCN_FUNCTION_8822B;
		val8 &= ~BIT_DIS_TSF_UDT_8822B;
		rtw_write8(adapter, REG_BCN_CTRL_8822B, val8);
#if 0
		/*
		 * To make sure that if there exists an adapter which would like to send beacon.
		 * If exists, the origianl value of 0x422[6] will be 1, we should check this to
		 * prevent from setting 0x422[6] to 0 after download reserved page, or it will cause
		 * the beacon cannot be sent by HW.
		 */
		if (bRecover) {
			rtw_write8(adapter, REG_FWHW_TXQ_CTRL_8822B + 2, hal->RegFwHwTxQCtrl | BIT(6));
			hal->RegFwHwTxQCtrl |= BIT(6);
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
 * Description: Get the reserved page number in Tx packet buffer.
 * Retrun value: the page number.
 */
u8 get_txbuffer_rsvdpagenum(PADAPTER adapter, bool wowlan)
{
	u8 RsvdPageNum = HALMAC_RSVD_DRV_PGNUM_8822B;


	return RsvdPageNum;
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

void hw_var_port_switch_8822b(_adapter *adapter)
{
        struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
        _adapter *if_p0 = NULL;
        _adapter *if_pn = adapter;
        u32 bssid_offset = 0;
        u8 bssid_p0[6] = {0};
        u8 bssid_pn[6] = {0};
        u8 tsftr_p0[8] = {0};
        u8 tsftr_pn[8] = {0};
        u8 vnet_type_p0 = 0;
        u8 vnet_type_pn = 0;
        u8 vbcn_ctrl_p0= 0;
        u8 vbcn_ctrl_pn= 0;
        u8 i = 0;
        u8 val8 = 0;
        u16 bcn_intv_p0 = 0;
        u16 bcn_intv_pn = 0;
        u32 addr_p0 = 0;
        u32 addr_pn = 0;
        u8 shift_p0 = 0;
        u8 shift_pn = 0;
        u16 mask_p0 = 0;
        u16 mask_pn = 0;
        u32 val32 = 0;

        if (if_pn->hw_port == HW_PORT0)
                return;

        /* Get iface of port-0 */
        for (i = 0; i < dvobj->iface_nums; i++) {
                if (get_hw_port(dvobj->padapters[i]) == HW_PORT0) {
                        if_p0 = dvobj->padapters[i];
                        break;
                }
        }

        if (if_p0 == NULL) {
                RTW_ERR("%s port 0 adapter == NULL\n", __func__);
                rtw_warn_on(1);
                return;
        }

        /* 1. BCN function */

	/* Disable port_0 BCN function */
        vbcn_ctrl_p0= rtw_read8(adapter,
		port_cfg[if_p0->hw_port].bcn_ctl);

        rtw_write8(adapter, port_cfg[if_p0->hw_port].bcn_ctl,
		vbcn_ctrl_p0 & ~BIT3);

	/* Disable port_n BCN function */
        vbcn_ctrl_pn = rtw_read8(adapter,
		port_cfg[if_pn->hw_port].bcn_ctl);

        rtw_write8(adapter, port_cfg[if_pn->hw_port].bcn_ctl,
		vbcn_ctrl_pn & ~BIT3);

        /* 2. networt type */
        GetHwReg(if_p0, HW_VAR_MEDIA_STATUS, &vnet_type_p0);
        GetHwReg(if_pn, HW_VAR_MEDIA_STATUS, &vnet_type_pn);

        Set_MSR(if_p0, vnet_type_pn);
        Set_MSR(if_pn, vnet_type_p0);

        /* 3. BSSID */
        if (is_client_associated_to_ap(if_pn)) {
                RTW_INFO(ADPT_FMT" is STA mode and linked\n", ADPT_ARG(if_pn));
                bssid_offset = port_cfg[if_pn->hw_port].bssid;
                for (i = 0; i < 6; i++)
                        bssid_pn[i] = rtw_read8(if_pn, bssid_offset + i);
        }

        if (is_client_associated_to_ap(if_p0)) {
                RTW_INFO(ADPT_FMT" is STA mode and linked\n", ADPT_ARG(if_p0));
                bssid_offset = port_cfg[if_p0->hw_port].bssid;
                for (i = 0; i < 6; i++)
                        bssid_p0[i] = rtw_read8(if_p0, bssid_offset + i);

		hw_var_set_bssid(if_pn, bssid_p0);
        }

        if (is_client_associated_to_ap(if_pn))
		hw_var_set_bssid(if_p0, bssid_pn);

        /* 4. MAC addr */
        hw_var_set_macaddr(if_p0, adapter_mac_addr(if_pn));
        hw_var_set_macaddr(if_pn, adapter_mac_addr(if_p0));

        /* 5. TSF */
        /*0x554[30:28] -BIT_BCN_TIMER_SEL_FWRD*/

	/* Read port n TSF */
        val8 = rtw_read8(adapter, REG_MBSSID_BCN_SPACE_8822B + 3);
        val8 &= 0x8F;
        val8 |= if_pn->hw_port << 4;
        rtw_write8(adapter, REG_MBSSID_BCN_SPACE_8822B + 3, val8);

        for (i = 0; i < 8; i++)
                tsftr_pn[i] = rtw_read8(adapter, REG_TSFTR_8822B + i);

	/* Read port 0 TSF */
        val8 = rtw_read8(adapter, REG_MBSSID_BCN_SPACE_8822B + 3);
        val8 &= 0x8F;
        val8 |= if_p0->hw_port << 4;
        rtw_write8(adapter, REG_MBSSID_BCN_SPACE_8822B + 3, val8);

        for (i = 0; i < 8; i++)
                tsftr_p0[i] = rtw_read8(adapter, REG_TSFTR_8822B + i);

	/* write port n TSF to port 0 */
        val8 = rtw_read8(adapter, REG_MBSSID_BCN_SPACE_8822B + 3);
        val8 &= 0x8F;
        val8 |= if_p0->hw_port << 4; /* to port 0 */
        rtw_write8(adapter, REG_MBSSID_BCN_SPACE_8822B + 3, val8);

        for (i = 0; i < 8; i++)
                rtw_write8(adapter, REG_TSFTR_8822B + i, tsftr_pn[i]);

	/* write port 0 TSF to port n */
        val8 = rtw_read8(adapter, REG_MBSSID_BCN_SPACE_8822B + 3);
        val8 &= 0x8F;
        val8 |= if_pn->hw_port << 4; /* to port n */
        rtw_write8(adapter, REG_MBSSID_BCN_SPACE_8822B + 3, val8);

        for (i = 0; i < 8; i++)
                rtw_write8(adapter, REG_TSFTR_8822B + i, tsftr_p0[i]);

        /* 6 beacon interval */
        addr_pn = port_cfg[if_pn->hw_port].bcn_space;
        mask_pn = port_cfg[if_pn->hw_port].bcn_space_mask;
        shift_pn =  port_cfg[if_pn->hw_port].bcn_space_shift;
        val32 = rtw_read32(adapter, addr_pn);
        bcn_intv_pn = (val32 >> shift_pn) & mask_pn;

        addr_p0 = port_cfg[if_p0->hw_port].bcn_space;
        mask_p0 = port_cfg[if_p0->hw_port].bcn_space_mask;
        shift_p0 =  port_cfg[if_p0->hw_port].bcn_space_shift;
        val32 = rtw_read32(adapter, addr_p0);
        bcn_intv_p0 = (val32 >> shift_p0) & mask_p0;

        val32 |= (bcn_intv_pn & mask_p0) << shift_p0;
        rtw_write32(adapter, addr_p0, val32);

        val32 |= (bcn_intv_p0 & mask_pn) << shift_pn;
        rtw_write32(adapter, addr_pn, val32);

        /* 7. BCN ctrl */
        rtw_write8(adapter, port_cfg[if_p0->hw_port].bcn_ctl, vbcn_ctrl_pn);
        rtw_write8(adapter, port_cfg[if_pn->hw_port].bcn_ctl, vbcn_ctrl_p0);

        /*reconfigure*/
        if_p0->hw_port = if_pn->hw_port;
        if_pn->hw_port = HW_PORT0;
}

void rtl8822b_sethwreg(PADAPTER adapter, u8 variable, u8 *val)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	u8 val8;
	u16 val16;
	u32 val32;


	switch (variable) {
/*
	case HW_VAR_MEDIA_STATUS:
		break;
*/
	case HW_VAR_SET_OPMODE:
		hw_var_set_opmode(adapter, *val);
		break;

	case HW_VAR_MAC_ADDR:
		hw_var_set_macaddr(adapter, val);
		break;

	case HW_VAR_BSSID:
		hw_var_set_bssid(adapter, val);
		break;
/*
	case HW_VAR_INIT_RTS_RATE:
		break;
*/
	case HW_VAR_BASIC_RATE:
		hw_var_set_basic_rate(adapter, val);
		break;

	case HW_VAR_TXPAUSE:
		rtw_write8(adapter, REG_TXPAUSE_8822B, *val);
		break;

	case HW_VAR_BCN_FUNC:
		hw_var_set_bcn_func(adapter, *val);
		break;

	case HW_VAR_CORRECT_TSF:
		hw_var_set_correct_tsf(adapter);
		break;

	case HW_VAR_CHECK_BSSID:
		hw_var_set_check_bssid(adapter, *val);
		break;

	case HW_VAR_MLME_DISCONNECT:
		hw_var_set_mlme_disconnect(adapter);
		break;

	case HW_VAR_MLME_SITESURVEY:
		hw_var_set_mlme_sitesurvey(adapter, *val);
#ifdef CONFIG_BT_COEXIST
		if (hal->EEPROMBluetoothCoexist)
			rtw_btcoex_ScanNotify(adapter, *val ? _TRUE : _FALSE);
#endif
		break;

	case HW_VAR_MLME_JOIN:
		hw_var_set_mlme_join(adapter, *val);

#ifdef CONFIG_BT_COEXIST
		if (hal->EEPROMBluetoothCoexist) {
			switch (*val) {
			case 0:
				/* Notify coex. mechanism before join */
				rtw_btcoex_ConnectNotify(adapter, _TRUE);
				break;
			case 1:
			case 2:
				/* Notify coex. mechanism after join, whether successful or failed */
				rtw_btcoex_ConnectNotify(adapter, _FALSE);
				break;
			}
		}
#endif /* CONFIG_BT_COEXIST */
		break;

	case HW_VAR_ON_RCR_AM:
		hw_var_set_rcr_am(adapter, 1);
		break;

	case HW_VAR_OFF_RCR_AM:
		hw_var_set_rcr_am(adapter, 0);
		break;

	case HW_VAR_BEACON_INTERVAL:
		hw_var_set_bcn_interval(adapter, *(u16 *)val);
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
/*
	case HW_VAR_RF_TYPE:
		break;
*/
	case HW_VAR_CAM_EMPTY_ENTRY:
		hw_var_set_cam_empty_entry(adapter, *val);
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
		hal->AcParam_BE = *(u32 *)val;
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
	case HW_VAR_AMPDU_FACTOR: {
		u32 AMPDULen = *val; /* enum AGGRE_SIZE */

		AMPDULen = (0x2000 << AMPDULen) - 1;
		rtw_write32(adapter, REG_AMPDU_MAX_LENGTH_8822B, AMPDULen);
	}
	break;

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
	case HW_VAR_H2C_FW_JOINBSSRPT:
		hw_var_set_h2c_fw_joinbssrpt(adapter, *val);
		break;
/*
	case HW_VAR_FWLPS_RF_ON:
		break;
*/
#ifdef CONFIG_P2P_PS
	case HW_VAR_H2C_FW_P2P_PS_OFFLOAD:
		rtl8822b_set_p2p_ps_offload_cmd(adapter, *val);
		break;
#endif
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

		/* pause tx */
		rtw_write8(adapter, REG_TXPAUSE_8822B, 0xff);

		/* keep sn */
		adapter->xmitpriv.nqos_ssn = rtw_read16(adapter, REG_HW_SEQ2_8822B);

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
		/* restore Sequence No. */
		rtw_write8(adapter, REG_HW_SEQ2_8822B, adapter->xmitpriv.nqos_ssn);
		break;

	case HW_VAR_CHECK_TXBUF: {
		u16 rtylmtorg;
		u8 RetryLimit = 0x01;
		u32 start, passtime;
		u32 timelmt = 2000;	/* ms */
		u32 waittime = 10;	/* ms */
		u32 high, low, normal, extra, publc;
		u16 rsvd, available;
		u8 empty;


		rtylmtorg = rtw_read16(adapter, REG_RETRY_LIMIT_8822B);

		val16 = BIT_LRL_8822B(RetryLimit) | BIT_SRL_8822B(RetryLimit);
		rtw_write16(adapter, REG_RETRY_LIMIT_8822B, val16);

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
			high = rtw_read32(adapter, REG_FIFOPAGE_INFO_1_8822B);
			low = rtw_read32(adapter, REG_FIFOPAGE_INFO_2_8822B);
			normal = rtw_read32(adapter, REG_FIFOPAGE_INFO_3_8822B);
			extra = rtw_read32(adapter, REG_FIFOPAGE_INFO_4_8822B);
			publc = rtw_read32(adapter, REG_FIFOPAGE_INFO_5_8822B);

			rsvd = BIT_GET_HPQ_V1_8822B(high);
			available = BIT_GET_HPQ_AVAL_PG_V1_8822B(high);
			if (rsvd != available) {
				rtw_msleep_os(waittime);
				continue;
			}

			rsvd = BIT_GET_LPQ_V1_8822B(low);
			available = BIT_GET_LPQ_AVAL_PG_V1_8822B(low);
			if (rsvd != available) {
				rtw_msleep_os(waittime);
				continue;
			}

			rsvd = BIT_GET_NPQ_V1_8822B(normal);
			available = BIT_GET_NPQ_AVAL_PG_V1_8822B(normal);
			if (rsvd != available) {
				rtw_msleep_os(waittime);
				continue;
			}

			rsvd = BIT_GET_EXQ_V1_8822B(extra);
			available = BIT_GET_EXQ_AVAL_PG_V1_8822B(extra);
			if (rsvd != available) {
				rtw_msleep_os(waittime);
				continue;
			}

			rsvd = BIT_GET_PUBQ_V1_8822B(publc);
			available = BIT_GET_PUBQ_AVAL_PG_V1_8822B(publc);
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
			RTW_PRINT("[HW_VAR_CHECK_TXBUF] NOT empty in %d ms\n", passtime);
			RTW_PRINT("[HW_VAR_CHECK_TXBUF] 0x230=0x%08x 0x234=0x%08x 0x238=0x%08x 0x23c=0x%08x 0x240=0x%08x\n",
				  high, low, normal, extra, publc);
		}

		rtw_write16(adapter, REG_RETRY_LIMIT_8822B, rtylmtorg);
	}
	break;
/*
	case HW_VAR_PCIE_STOP_TX_DMA:
		break;
*/
	case HW_VAR_APFM_ON_MAC:
		hal->bMacPwrCtrlOn = *val;
		RTW_INFO("[HW_VAR_APFM_ON_MAC] bMacPwrCtrlOn=%d\n", hal->bMacPwrCtrlOn);
		break;
/*
	case HW_VAR_HCI_SUS_STATE:
#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
	case HW_VAR_WOWLAN:
	case HW_VAR_WAKEUP_REASON:
	case HW_VAR_RPWM_TOG:
#endif
		break;
*/
#ifdef CONFIG_GPIO_WAKEUP
	case HW_SET_GPIO_WL_CTRL: {
		u8 enable = *val;
		u8 shift = 2;
		u32 addr;
		u8 bit_en;

		addr = REG_LED_CFG_8822B + shift;
		bit_en = BIT_GPIO13_14_WL_CTRL_EN_8822B >> (shift * 8);

		val8 = rtw_read8(adapter, addr);

		if (enable == _TRUE) {
			if (val8 & bit_en) {
				val8 &= ~bit_en;
				rtw_write8(adapter, addr, val8);
			}
		} else {
			if (!(val8 & bit_en)) {
				val8 |= bit_en;
				rtw_write8(adapter, addr, val8);
			}

		}
		RTW_INFO("[HW_SET_GPIO_WL_CTRL] 0x%02X=0x%02X\n",
			 addr, rtw_read8(adapter, addr));
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
	case HW_VAR_C2H_HANDLE:
	case HW_VAR_RPT_TIMER_SETTING:
	case HW_VAR_TX_RPT_MAX_MACID:
	case HW_VAR_CHK_HI_QUEUE_EMPTY:
		break;
*/
	case HW_VAR_DL_BCN_SEL:
#ifdef CONFIG_CONCURRENT_MODE
		if (adapter->hw_port == HW_PORT1) {
			/* Port1 */
			/* ToDo */
		} else
#endif /* CONFIG_CONCURRENT_MODE */
		{
			/* Port0 */
			/* ToDo */
		}
		break;
/*
	case HW_VAR_AMPDU_MAX_TIME:
	case HW_VAR_WIRELESS_MODE:
	case HW_VAR_USB_MODE:
*/
	case HW_VAR_PORT_SWITCH:
		hw_var_port_switch_8822b(adapter);
		break;

	case HW_VAR_DO_IQK:
		if (*val)
			hal->bNeedIQK = _TRUE;
		else
			hal->bNeedIQK = _FALSE;
		break;

	case HW_VAR_DM_IN_LPS:
		rtl8822b_phy_haldm_in_lps(adapter);
		break;
/*
	case HW_VAR_SET_REQ_FW_PS:
	case HW_VAR_FW_PS_STATE:
		break;
*/
#ifdef CONFIG_BEAMFORMING
	case HW_VAR_SOUNDING_ENTER:
		rtl8822b_phy_sounding_enter(adapter, (struct sta_info*)val);
		break;

	case HW_VAR_SOUNDING_LEAVE:
		rtl8822b_phy_sounding_leave(adapter, val);
		break;
/*
	case HW_VAR_SOUNDING_RATE:
	case HW_VAR_SOUNDING_STATUS:
	case HW_VAR_SOUNDING_FW_NDPA:
	case HW_VAR_SOUNDING_CLK:
		break;
*/
	case HW_VAR_SOUNDING_SET_GID_TABLE:
		rtl8822b_phy_sounding_set_gid_table(adapter, (struct beamformer_entry*)val);
		break;
#endif /* CONFIG_BEAMFORMING */
/*
	case HW_VAR_HW_REG_TIMER_INIT:
	case HW_VAR_HW_REG_TIMER_RESTART:
	case HW_VAR_HW_REG_TIMER_START:
	case HW_VAR_HW_REG_TIMER_STOP:
		break;
*/
	case HW_VAR_DL_RSVD_PAGE:
#ifdef CONFIG_BT_COEXIST
		if (check_fwstate(&adapter->mlmepriv, WIFI_AP_STATE) == _TRUE)
			rtl8822b_download_BTCoex_AP_mode_rsvd_page(adapter);
#endif
		break;
/*
	case HW_VAR_MACID_LINK:
	case HW_VAR_MACID_NOLINK:
		break;
*/
	case HW_VAR_MACID_SLEEP: {
		u32 reg_macid_sleep;
		u8 bit_shift;
		u8 id = *(u8 *)val;

		if (id < 32) {
			reg_macid_sleep = REG_MACID_SLEEP_8822B;
			bit_shift = id;
		} else if (id < 64) {
			reg_macid_sleep = REG_MACID_SLEEP1_8822B;
			bit_shift = id - 32;
		} else if (id < 96) {
			reg_macid_sleep = REG_MACID_SLEEP2_8822B;
			bit_shift = id - 64;
		} else if (id < 128) {
			reg_macid_sleep = REG_MACID_SLEEP3_8822B;
			bit_shift = id - 96;
		} else {
			rtw_warn_on(1);
			break;
		}

		val32 = rtw_read32(adapter, reg_macid_sleep);
		RTW_INFO(FUNC_ADPT_FMT ": [HW_VAR_MACID_SLEEP] macid=%d, org reg_0x%03x=0x%08X\n",
			FUNC_ADPT_ARG(adapter), id, reg_macid_sleep, val32);

		if (val32 & BIT(bit_shift))
			break;

		val32 |= BIT(bit_shift);
		rtw_write32(adapter, reg_macid_sleep, val32);
	}
	break;

	case HW_VAR_MACID_WAKEUP: {
		u32 reg_macid_sleep;
		u8 bit_shift;
		u8 id = *(u8 *)val;

		if (id < 32) {
			reg_macid_sleep = REG_MACID_SLEEP_8822B;
			bit_shift = id;
		} else if (id < 64) {
			reg_macid_sleep = REG_MACID_SLEEP1_8822B;
			bit_shift = id - 32;
		} else if (id < 96) {
			reg_macid_sleep = REG_MACID_SLEEP2_8822B;
			bit_shift = id - 64;
		} else if (id < 128) {
			reg_macid_sleep = REG_MACID_SLEEP3_8822B;
			bit_shift = id - 96;
		} else {
			rtw_warn_on(1);
			break;
		}

		val32 = rtw_read32(adapter, reg_macid_sleep);
		RTW_INFO(FUNC_ADPT_FMT ": [HW_VAR_MACID_WAKEUP] macid=%d, org reg_0x%03x=0x%08X\n",
			FUNC_ADPT_ARG(adapter), id, reg_macid_sleep, val32);

		if (!(val32 & BIT(bit_shift)))
			break;

		val32 &= ~BIT(bit_shift);
		rtw_write32(adapter, reg_macid_sleep, val32);
	}
	break;
/*
	case HW_VAR_DUMP_MAC_QUEUE_INFO:
	case HW_VAR_ASIX_IOT:
#ifdef CONFIG_MBSSID_CAM
	case HW_VAR_MBSSID_CAM_WRITE:
	case HW_VAR_MBSSID_CAM_CLEAR:
	case HW_VAR_RCR_MBSSID_EN:
#endif
	case HW_VAR_EN_HW_UPDATE_TSF:
	case HW_VAR_CH_SW_NEED_TO_TAKE_CARE_IQK_INFO:
	case HW_VAR_CH_SW_IQK_INFO_BACKUP:
	case HW_VAR_CH_SW_IQK_INFO_RESTORE:
		break;
*/
#ifdef CONFIG_TDLS
	case HW_VAR_TDLS_WRCR:
		rtl8822b_rcr_clear(adapter, BIT_CBSSID_DATA_8822B);
		break;

	case HW_VAR_TDLS_RS_RCR:
		rtl8822b_rcr_add(adapter, BIT_CBSSID_DATA_8822B);
		break;
/*
#ifdef CONFIG_TDLS_CH_SW
	case HW_VAR_TDLS_BCN_EARLY_C2H_RPT:
		break;
#endif
*/
#endif

	default:
		SetHwReg(adapter, variable, val);
		break;
	}
}

struct qinfo {
	u32 head:8;
	u32 pkt_num:7;
	u32 tail:8;
	u32 ac:2;
	u32 macid:7;
};

struct bcn_qinfo {
	u16 head:8;
	u16 pkt_num:8;
};

static void dump_qinfo(void *sel, struct qinfo *info, const char *tag)
{
	RTW_PRINT_SEL(sel, "%shead:0x%02x, tail:0x%02x, pkt_num:%u, macid:%u, ac:%u\n",
		tag ? tag : "", info->head, info->tail, info->pkt_num, info->macid, info->ac);
}

static void dump_bcn_qinfo(void *sel, struct bcn_qinfo *info, const char *tag)
{
	RTW_PRINT_SEL(sel, "%shead:0x%02x, pkt_num:%u\n",
		      tag ? tag : "", info->head, info->pkt_num);
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

	dump_qinfo(sel, (struct qinfo *)&q0_info, "Q0 ");
	dump_qinfo(sel, (struct qinfo *)&q1_info, "Q1 ");
	dump_qinfo(sel, (struct qinfo *)&q2_info, "Q2 ");
	dump_qinfo(sel, (struct qinfo *)&q3_info, "Q3 ");
	dump_qinfo(sel, (struct qinfo *)&q4_info, "Q4 ");
	dump_qinfo(sel, (struct qinfo *)&q5_info, "Q5 ");
	dump_qinfo(sel, (struct qinfo *)&q6_info, "Q6 ");
	dump_qinfo(sel, (struct qinfo *)&q7_info, "Q7 ");
	dump_qinfo(sel, (struct qinfo *)&mg_q_info, "MG ");
	dump_qinfo(sel, (struct qinfo *)&hi_q_info, "HI ");
	dump_bcn_qinfo(sel, (struct bcn_qinfo *)&bcn_q_info, "BCN ");
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

void rtl8822b_gethwreg(PADAPTER adapter, u8 variable, u8 *val)
{
	PHAL_DATA_TYPE hal;
	u8 val8;
	u16 val16;
	u32 val32;


	hal = GET_HAL_DATA(adapter);

	switch (variable) {
/*
	case HW_VAR_MEDIA_STATUS:
	case HW_VAR_SET_OPMODE:
	case HW_VAR_MAC_ADDR:
	case HW_VAR_BSSID:
	case HW_VAR_INIT_RTS_RATE:
	case HW_VAR_BASIC_RATE:
		break;
*/
	case HW_VAR_TXPAUSE:
		*val = rtw_read8(adapter, REG_TXPAUSE_8822B);
		break;
/*
	case HW_VAR_BCN_FUNC:
	case HW_VAR_CORRECT_TSF:
	case HW_VAR_CHECK_BSSID:
	case HW_VAR_MLME_DISCONNECT:
	case HW_VAR_MLME_SITESURVEY:
	case HW_VAR_MLME_JOIN:
	case HW_VAR_ON_RCR_AM:
	case HW_VAR_OFF_RCR_AM:
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
	case HW_VAR_RF_TYPE:
	case HW_VAR_CAM_EMPTY_ENTRY:
	case HW_VAR_CAM_INVALID_ALL:
	case HW_VAR_AC_PARAM_VO:
	case HW_VAR_AC_PARAM_VI:
	case HW_VAR_AC_PARAM_BE:
	case HW_VAR_AC_PARAM_BK:
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
			rtl8822b_rcr_get(adapter, &val32);
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
	case HW_VAR_APFM_ON_MAC:
		*val = hal->bMacPwrCtrlOn;
		break;
/*
	case HW_VAR_HCI_SUS_STATE:
		break;
*/
#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
/*
	case HW_VAR_WOWLAN:
		break;
*/
	case HW_VAR_WAKEUP_REASON:
		rtw_halmac_get_wow_reason(adapter_to_dvobj(adapter), val);
		break;
/*
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
	case HW_VAR_C2H_HANDLE:
	case HW_VAR_RPT_TIMER_SETTING:
	case HW_VAR_TX_RPT_MAX_MACID:
		break;
*/
	case HW_VAR_CHK_HI_QUEUE_EMPTY:
		val16 = rtw_read16(adapter, REG_TXPKT_EMPTY_8822B);
		*val = (val16 & BIT_HQQ_EMPTY_8822B) ? _TRUE : _FALSE;
		break;
/*
	case HW_VAR_DL_BCN_SEL:
	case HW_VAR_AMPDU_MAX_TIME:
	case HW_VAR_WIRELESS_MODE:
	case HW_VAR_USB_MODE:
	case HW_VAR_PORT_SWITCH:
	case HW_VAR_DO_IQK:
	case HW_VAR_DM_IN_LPS:
	case HW_VAR_SET_REQ_FW_PS:
	case HW_VAR_FW_PS_STATE:
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
	case HW_VAR_DL_RSVD_PAGE:
	case HW_VAR_MACID_LINK:
	case HW_VAR_MACID_NOLINK:
	case HW_VAR_MACID_SLEEP:
	case HW_VAR_MACID_WAKEUP:
		break;
*/
	case HW_VAR_DUMP_MAC_QUEUE_INFO:
		dump_mac_qinfo(val, adapter);
		break;
/*
	case HW_VAR_ASIX_IOT:
#ifdef CONFIG_MBSSID_CAM
	case HW_VAR_MBSSID_CAM_WRITE:
	case HW_VAR_MBSSID_CAM_CLEAR:
	case HW_VAR_RCR_MBSSID_EN:
#endif
	case HW_VAR_EN_HW_UPDATE_TSF:
	case HW_VAR_CH_SW_NEED_TO_TAKE_CARE_IQK_INFO:
	case HW_VAR_CH_SW_IQK_INFO_BACKUP:
	case HW_VAR_CH_SW_IQK_INFO_RESTORE:
#ifdef CONFIG_TDLS
	case HW_VAR_TDLS_WRCR:
	case HW_VAR_TDLS_RS_RCR:
#ifdef CONFIG_TDLS_CH_SW
	case HW_VAR_TDLS_BCN_EARLY_C2H_RPT:
#endif
#endif
		break;
*/
	default:
		GetHwReg(adapter, variable, val);
		break;
	}
}

void rtl8822b_sethwregwithbuf(PADAPTER adapter, u8 variable, u8 *pbuf, int len)
{
	PHAL_DATA_TYPE hal;


	hal = GET_HAL_DATA(adapter);

	switch (variable) {
	case HW_VAR_C2H_HANDLE:
		rtl8822b_c2h_handler(adapter, pbuf, len);
		break;

	default:
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
	case HW_DEF_RA_INFO_DUMP:
	case HAL_DEF_DBG_DUMP_TXPKT:
	case HAL_DEF_TX_PAGE_SIZE:
	case HAL_DEF_TX_PAGE_BOUNDARY:
	case HAL_DEF_TX_PAGE_BOUNDARY_WOWLAN:
	case HAL_DEF_ANT_DETECT:
	case HAL_DEF_PCI_SUUPORT_L1_BACKDOOR:
	case HAL_DEF_PCI_AMD_L1_SUPPORT:
	case HAL_DEF_PCI_ASPM_OSC:
	case HAL_DEF_MACID_SLEEP:
	case HAL_DEF_DBG_DIS_PWT:
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

/*
 * Description:
 *	Query setting of specified variable.
 */
u8 rtl8822b_gethaldefvar(PADAPTER adapter, HAL_DEF_VARIABLE variable, void *pval)
{
	PHAL_DATA_TYPE hal;
	struct dvobj_priv *d;
	u8 bResult;
	u8 val8;


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

	case HAL_DEF_MAX_RECVBUF_SZ:
		*((u32 *)pval) = HALMAC_RX_FIFO_SIZE_8822B;
		break;

	case HAL_DEF_RX_PACKET_OFFSET:
		rtw_halmac_get_drv_info_sz(d, &val8);
		*((u32 *)pval) = HALMAC_RX_DESC_SIZE_8822B + val8;
		break;
/*
	case HAL_DEF_DRVINFO_SZ:
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

	/* support 1T STBC under 2TX */
	case HAL_DEF_TX_STBC:
		if (hal->rf_type == RF_1T2R || hal->rf_type == RF_1T1R)
			*(u8 *)pval = 0;
		else
			*(u8 *)pval = 1;
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
		val8 = 0;
		rtw_hal_get_hwreg(adapter, HW_VAR_RF_TYPE, &val8);
		switch (val8) {
		case RF_1T1R:
		case RF_1T2R:
			*(u8 *)pval = 0;
			break;
		default:
		case RF_2T2R:
			*(u8 *)pval = 1;
			break;
		}
		break;

	case HAL_DEF_BEAMFORMEE_CAP:
		*(u8 *)pval = 3;
		break;

	case HW_VAR_MAX_RX_AMPDU_FACTOR:
		/* 8822B RX FIFO is 24KB */
		*(HT_CAP_AMPDU_FACTOR *)pval = MAX_AMPDU_FACTOR_16K;
		break;

	case HW_DEF_RA_INFO_DUMP: {
#if 0
		u8 mac_id = *(u8 *)pval;
		u32 cmd;
		u32 ra_info1, ra_info2;
		u32 rate_mask1, rate_mask2;
		u8 curr_tx_rate, curr_tx_sgi, hight_rate, lowest_rate;

		RTW_INFO("============ RA status check  Mac_id:%d ===================\n", mac_id);

		cmd = 0x40000100 | mac_id;
		rtw_write32(adapter, REG_HMEBOX_DBG_2_8723B, cmd);
		rtw_msleep_os(10);
		ra_info1 = rtw_read32(adapter, 0x2F0);
		curr_tx_rate = ra_info1 & 0x7F;
		curr_tx_sgi = (ra_info1 >> 7) & 0x01;
		RTW_INFO("[ ra_info1:0x%08x ] =>cur_tx_rate=%s, cur_sgi:%d, PWRSTS=0x%02x\n",
			 ra_info1,
			 HDATA_RATE(curr_tx_rate),
			 curr_tx_sgi,
			 (ra_info1 >> 8) & 0x07);

		cmd = 0x40000400 | mac_id;
		rtw_write32(adapter, REG_HMEBOX_DBG_2_8723B, cmd);
		rtw_msleep_os(10);
		ra_info1 = rtw_read32(adapter, 0x2F0);
		ra_info2 = rtw_read32(adapter, 0x2F4);
		rate_mask1 = rtw_read32(adapter, 0x2F8);
		rate_mask2 = rtw_read32(adapter, 0x2FC);
		hight_rate = ra_info2 & 0xFF;
		lowest_rate = (ra_info2 >> 8)  & 0xFF;

		RTW_INFO("[ ra_info1:0x%08x ] =>RSSI=%d, BW_setting=0x%02x, DISRA=0x%02x, VHT_EN=0x%02x\n",
			 ra_info1,
			 ra_info1 & 0xFF,
			 (ra_info1 >> 8)  & 0xFF,
			 (ra_info1 >> 16) & 0xFF,
			 (ra_info1 >> 24) & 0xFF);

		RTW_INFO("[ ra_info2:0x%08x ] =>hight_rate=%s, lowest_rate=%s, SGI=0x%02x, RateID=%d\n",
			 ra_info2,
			 HDATA_RATE(hight_rate),
			 HDATA_RATE(lowest_rate),
			 (ra_info2 >> 16) & 0xFF,
			 (ra_info2 >> 24) & 0xFF);

		RTW_INFO("rate_mask2=0x%08x, rate_mask1=0x%08x\n", rate_mask2, rate_mask1);
#endif
	}
	break;
/*
	case HAL_DEF_DBG_DUMP_TXPKT:
		break;
*/
	case HAL_DEF_TX_PAGE_SIZE:
		*(u32 *)pval = HALMAC_TX_PAGE_SIZE_8822B;
		break;
/*
	case HAL_DEF_TX_PAGE_BOUNDARY:
	case HAL_DEF_TX_PAGE_BOUNDARY_WOWLAN:
	case HAL_DEF_ANT_DETECT:
	case HAL_DEF_PCI_SUUPORT_L1_BACKDOOR:
	case HAL_DEF_PCI_AMD_L1_SUPPORT:
	case HAL_DEF_PCI_ASPM_OSC:
		break;
*/
	case HAL_DEF_MACID_SLEEP:
		*(u8 *)pval = _TRUE; /* support macid sleep */
		break;
/*
	case HAL_DEF_DBG_DIS_PWT:
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

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
/*
 * Description: Prepare some information to Fw for WoWLAN.
 *	(1) Download wowlan Fw.
 *	(2) Download RSVD page packets.
 *	(3) Enable AP offload if needed.
 */
static void SetFwRelatedForWoWLAN(PADAPTER adapter, u8 bHostIsGoingtoSleep)
{
	u8 status = _FAIL;
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	u8 bRecover = _FALSE;

	/*
	 * 1. Before WoWLAN we need to re-download WoWLAN Fw.
	 */
	status = rtl8723b_FirmwareDownload(adapter, bHostIsGoingtoSleep);
	if (status != _SUCCESS) {
		RTW_INFO("%s: Re-Download Firmware failed!!\n", __FUNCTION__);
		return;
	}
	RTW_INFO("%s: Re-Download Firmware Success !!\n", , __FUNCTION__);

	/*
	 * 2. Re-Init the variables about Fw related setting.
	 */
	rtl8723b_InitializeFirmwareVars(adapter);
}
#endif /* CONFIG_WOWLAN || CONFIG_AP_WOWLAN */

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


	if (hal->CurrentChannelBW == CHANNEL_WIDTH_80) {
		if (pattrib->bwmode == CHANNEL_WIDTH_80)
			BWSettingOfDesc = 2;
		else if (pattrib->bwmode == CHANNEL_WIDTH_40)
			BWSettingOfDesc = 1;
		else
			BWSettingOfDesc = 0;
	} else if (hal->CurrentChannelBW == CHANNEL_WIDTH_40) {
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


	if (hal->CurrentChannelBW == CHANNEL_WIDTH_80) {
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
	} else if (hal->CurrentChannelBW == CHANNEL_WIDTH_40) {
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

void rtl8822b_cal_txdesc_chksum(PADAPTER adapter, u8 *ptxdesc)
{
	PHALMAC_ADAPTER halmac;
	PHALMAC_API api;


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
	u8 data_rate, pwr_status, offset;


	desc = pmp_priv->tx.desc;
	attrib = &pmp_priv->tx.attrib;
	pkt_size = attrib->last_txcmdsz;
	bmcast = IS_MCAST(attrib->ra);

	SET_TX_DESC_LS_8822B(desc, 1);
	SET_TX_DESC_TXPKTSIZE_8822B(desc, pkt_size);

	offset = HALMAC_TX_DESC_SIZE_8822B;
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
		RTW_INFO("%s: <ERROR> unknown bandwidth %d, use 20M\n",
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


	_rtw_memset(pbuf, 0, HALMAC_TX_DESC_SIZE_8822B);

	adapter = pxmitframe->padapter;
	hal = GET_HAL_DATA(adapter);
	pmlmeext = &adapter->mlmeextpriv;
	pmlmeinfo = &(pmlmeext->mlmext_info);

	pattrib = &pxmitframe->attrib;
	bmcst = IS_MCAST(pattrib->ra);

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
			if (pmlmeext->cur_channel > 14)
				/* for 5G, OFDM 6M */
				SET_TX_DESC_DATA_RTY_LOWEST_RATE_8822B(pbuf, 4);
			else
				/* for 2.4G, CCK 1M */
				SET_TX_DESC_DATA_RTY_LOWEST_RATE_8822B(pbuf, 0);

			if (hal->fw_ractrl == _FALSE) {
				SET_TX_DESC_USE_RATE_8822B(pbuf, 1);

				if (hal->INIDATA_RATE[pattrib->mac_id] & BIT(7))
					SET_TX_DESC_DATA_SHORT_8822B(pbuf, 1);

				SET_TX_DESC_DATARATE_8822B(pbuf, hal->INIDATA_RATE[pattrib->mac_id] & 0x7F);
			}

			/* modify data rate by iwpriv */
			if (adapter->fix_rate != 0xFF) {
				SET_TX_DESC_USE_RATE_8822B(pbuf, 1);
				if (adapter->fix_rate & BIT(7))
					SET_TX_DESC_DATA_SHORT_8822B(pbuf, 1);
				SET_TX_DESC_DATARATE_8822B(pbuf, adapter->fix_rate & 0x7F);
				if (!adapter->data_fb)
					SET_TX_DESC_DISDATAFB_8822B(pbuf, 1);
			}

			if (pattrib->ldpc)
				SET_TX_DESC_DATA_LDPC_8822B(pbuf, 1);
			if (pattrib->stbc)
				SET_TX_DESC_DATA_STBC_8822B(pbuf, 1);

#ifdef CONFIG_CMCC_TEST
			SET_TX_DESC_DATA_SHORT_8822B(pbuf, 1); /* use cck short premble */
#endif
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
			SET_TX_DESC_DATARATE_8822B(pbuf, MRateToHwRate(pmlmeext->tx_rate));

			RTW_INFO(FUNC_ADPT_FMT ": SP Packet(0x%04X) rate=0x%x\n",
				FUNC_ADPT_ARG(adapter), pattrib->ether_type, MRateToHwRate(pmlmeext->tx_rate));
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

#ifdef CONFIG_INTEL_PROXIM
		if ((adapter->proximity.proxim_on == _TRUE)
		    && (pattrib->intel_proxim == _TRUE)) {
			RTW_INFO("%s: pattrib->rate=%d\n", __FUNCTION__, pattrib->rate);
			SET_TX_DESC_DATARATE_8822B(pbuf, pattrib->rate);
		} else
#endif
		{
			SET_TX_DESC_DATARATE_8822B(pbuf, MRateToHwRate(pmlmeext->tx_rate));
		}

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
		RTW_INFO("%s: MP_FRAMETAG\n", __FUNCTION__);
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
		offset = HALMAC_TX_DESC_SIZE_8822B;
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
	if (!pattrib->qos_en)
		SET_TX_DESC_EN_HWSEQ_8822B(pbuf, 1);

#ifdef CONFIG_BEAMFORMING
	SET_TX_DESC_G_ID_8822B(pbuf, pattrib->txbf_g_id);
	SET_TX_DESC_P_AID_8822B(pbuf, pattrib->txbf_p_aid);
#endif /* CONFIG_BEAMFORMING */
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

#ifdef CONFIG_ANTENNA_DIVERSITY
	ODM_SetTxAntByTxInfo(&GET_HAL_DATA(pxmitframe->adapter)->odmpriv, pbuf, pxmitframe->attrib.mac_id);
#endif /* CONFIG_ANTENNA_DIVERSITY */

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
	_rtw_memset(pDesc, 0, HALMAC_TX_DESC_SIZE_8822B);

	SET_TX_DESC_LS_8822B(pDesc, 1);

	SET_TX_DESC_OFFSET_8822B(pDesc, HALMAC_TX_DESC_SIZE_8822B);

	SET_TX_DESC_TXPKTSIZE_8822B(pDesc, BufferLen);
	SET_TX_DESC_QSEL_8822B(pDesc, QSLT_MGNT); /* Fixed queue of Mgnt queue */

	/* Set NAVUSEHDR to prevent Ps-poll AId filed to be changed to error vlaue by HW */
	if (_TRUE == IsPsPoll)
		SET_TX_DESC_NAVUSEHDR_8822B(pDesc, 1);
	else {
		SET_TX_DESC_EN_HWSEQ_8822B(pDesc, 1);
		SET_TX_DESC_HW_SSN_SEL_8822B(pDesc, 0);
	}

	if (_TRUE == IsBTQosNull)
		SET_TX_DESC_BT_NULL_8822B(pDesc, 1);

	SET_TX_DESC_USE_RATE_8822B(pDesc, 1);
	SET_TX_DESC_DATARATE_8822B(pDesc, DESC_RATE1M);

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

void rtl8822b_rxdesc2attribute(struct rx_pkt_attrib *a, u8 *desc)
{
	_rtw_memset(a, 0, sizeof(struct rx_pkt_attrib));

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


	ops = &adapter->HalFunc;
	hal = GET_HAL_DATA(adapter);

	/*
	 * Initialize hal_com_data variables
	 */
	hal->tx_pa_bias_a = 0xFF;
	hal->tx_pa_bias_b = 0xFF;

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
/*
	ops->InitSwLeds = NULL;
	ops->DeInitSwLeds = NULL;
*/
	ops->set_bwmode_handler = rtl8822b_set_bw_mode;
	ops->set_channel_handler = rtl8822b_set_channel;
	ops->set_chnl_bw_handler = rtl8822b_set_channel_bw;

	ops->set_tx_power_level_handler = rtl8822b_set_tx_power_level;
	ops->get_tx_power_level_handler = rtl8822b_get_tx_power_level;

	ops->set_tx_power_index_handler = rtl8822b_set_tx_power_index;
	ops->get_tx_power_index_handler = rtl8822b_get_tx_power_index;

	ops->hal_dm_watchdog = rtl8822b_phy_haldm_watchdog;
#ifdef CONFIG_LPS_LCLK_WD_TIMER
	ops->hal_dm_watchdog_in_lps = rtl8822b_phy_haldm_watchdog_in_lps;
#endif

	ops->SetHwRegHandler = rtl8822b_sethwreg;
	ops->GetHwRegHandler = rtl8822b_gethwreg;
	ops->SetHwRegHandlerWithBuf = rtl8822b_sethwregwithbuf;
	ops->GetHalDefVarHandler = rtl8822b_gethaldefvar;
	ops->SetHalDefVarHandler = rtl8822b_sethaldefvar;

	ops->GetHalODMVarHandler = GetHalODMVar;
	ops->SetHalODMVarHandler = SetHalODMVar;

	ops->UpdateRAMaskHandler = update_ra_mask;
	ops->SetBeaconRelatedRegistersHandler = set_beacon_related_registers;

	ops->Add_RateATid = add_rateatid;
/*
	ops->interface_ps_func = NULL;
*/
	ops->read_bbreg = rtl8822b_read_bb_reg;
	ops->write_bbreg = rtl8822b_write_bb_reg;
	ops->read_rfreg = rtl8822b_read_rf_reg;
	ops->write_rfreg = rtl8822b_write_rf_reg;

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
/*
	ops->c2h_handler = NULL;
*/
	ops->c2h_id_filter_ccx = c2h_id_filter_ccx_8822b;
	ops->fill_h2c_cmd = rtl8822b_fillh2ccmd;
	ops->fill_fake_txdesc = fill_fake_txdesc;
	ops->fw_dl = rtl8822b_fw_dl;

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN) || defined(CONFIG_PCI_HCI)
/*
	ops->clear_interrupt = NULL;
*/
#endif

	ops->hal_get_tx_buff_rsvd_page_num = get_txbuffer_rsvdpagenum;

#ifdef CONFIG_GPIO_API
/*
	ops->update_hisr_hsisr_ind = NULL;
*/
#endif

	ops->fw_correct_bcn = rtl8822b_fw_update_beacon_cmd;

	/* HALMAC related functions */
	ops->init_mac_register = rtl8822b_phy_init_mac_register;
	ops->init_phy = rtl8822b_phy_init;
	ops->reqtxrpt = rtl8822b_req_txrpt_cmd;
}
