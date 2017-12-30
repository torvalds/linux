/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#include "../mp_precomp.h"
#include "../phydm_precomp.h"

static void phydm_dynamic_switch_htstf_mumimo_8822b(struct phy_dm_struct *dm)
{
	/*if rssi > 40dBm, enable HT-STF gain controller,
	 *otherwise, if rssi < 40dBm, disable the controller
	 */
	/*add by Chun-Hung Ho 20160711 */
	if (dm->rssi_min >= 40)
		odm_set_bb_reg(dm, 0x8d8, BIT(17), 0x1);
	else if (dm->rssi_min < 35)
		odm_set_bb_reg(dm, 0x8d8, BIT(17), 0x0);

	ODM_RT_TRACE(dm, ODM_COMP_COMMON, "%s, rssi_min = %d\n", __func__,
		     dm->rssi_min);
}

static void _set_tx_a_cali_value(struct phy_dm_struct *dm, u8 rf_path,
				 u8 offset, u8 tx_a_bias_offset)
{
	u32 modi_tx_a_value = 0;
	u8 tmp1_byte = 0;
	bool is_minus = false;
	u8 comp_value = 0;

	switch (offset) {
	case 0x0:
		odm_set_rf_reg(dm, rf_path, 0x18, 0xFFFFF, 0X10124);
		break;
	case 0x1:
		odm_set_rf_reg(dm, rf_path, 0x18, 0xFFFFF, 0X10524);
		break;
	case 0x2:
		odm_set_rf_reg(dm, rf_path, 0x18, 0xFFFFF, 0X10924);
		break;
	case 0x3:
		odm_set_rf_reg(dm, rf_path, 0x18, 0xFFFFF, 0X10D24);
		break;
	case 0x4:
		odm_set_rf_reg(dm, rf_path, 0x18, 0xFFFFF, 0X30164);
		break;
	case 0x5:
		odm_set_rf_reg(dm, rf_path, 0x18, 0xFFFFF, 0X30564);
		break;
	case 0x6:
		odm_set_rf_reg(dm, rf_path, 0x18, 0xFFFFF, 0X30964);
		break;
	case 0x7:
		odm_set_rf_reg(dm, rf_path, 0x18, 0xFFFFF, 0X30D64);
		break;
	case 0x8:
		odm_set_rf_reg(dm, rf_path, 0x18, 0xFFFFF, 0X50195);
		break;
	case 0x9:
		odm_set_rf_reg(dm, rf_path, 0x18, 0xFFFFF, 0X50595);
		break;
	case 0xa:
		odm_set_rf_reg(dm, rf_path, 0x18, 0xFFFFF, 0X50995);
		break;
	case 0xb:
		odm_set_rf_reg(dm, rf_path, 0x18, 0xFFFFF, 0X50D95);
		break;
	default:
		ODM_RT_TRACE(dm, ODM_COMP_COMMON,
			     "Invalid TxA band offset...\n");
		return;
	}

	/* Get TxA value */
	modi_tx_a_value = odm_get_rf_reg(dm, rf_path, 0x61, 0xFFFFF);
	tmp1_byte = (u8)modi_tx_a_value & (BIT(3) | BIT(2) | BIT(1) | BIT(0));

	/* check how much need to calibration */
	switch (tx_a_bias_offset) {
	case 0xF6:
		is_minus = true;
		comp_value = 3;
		break;

	case 0xF4:
		is_minus = true;
		comp_value = 2;
		break;

	case 0xF2:
		is_minus = true;
		comp_value = 1;
		break;

	case 0xF3:
		is_minus = false;
		comp_value = 1;
		break;

	case 0xF5:
		is_minus = false;
		comp_value = 2;
		break;

	case 0xF7:
		is_minus = false;
		comp_value = 3;
		break;

	case 0xF9:
		is_minus = false;
		comp_value = 4;
		break;

	/* do nothing case */
	case 0xF0:
	default:
		ODM_RT_TRACE(dm, ODM_COMP_COMMON,
			     "No need to do TxA bias current calibration\n");
		return;
	}

	/* calc correct value to calibrate */
	if (is_minus) {
		if (tmp1_byte >= comp_value) {
			tmp1_byte -= comp_value;
			/*modi_tx_a_value += tmp1_byte;*/
		} else {
			tmp1_byte = 0;
		}
	} else {
		tmp1_byte += comp_value;
		if (tmp1_byte >= 7)
			tmp1_byte = 7;
	}

	/* Write back to RF reg */
	odm_set_rf_reg(dm, rf_path, 0x30, 0xFFFF,
		       (offset << 12 | (modi_tx_a_value & 0xFF0) | tmp1_byte));
}

static void _txa_bias_cali_4_each_path(struct phy_dm_struct *dm, u8 rf_path,
				       u8 efuse_value)
{
	/* switch on set TxA bias */
	odm_set_rf_reg(dm, rf_path, 0xEF, 0xFFFFF, 0x200);

	/* Set 12 sets of TxA value */
	_set_tx_a_cali_value(dm, rf_path, 0x0, efuse_value);
	_set_tx_a_cali_value(dm, rf_path, 0x1, efuse_value);
	_set_tx_a_cali_value(dm, rf_path, 0x2, efuse_value);
	_set_tx_a_cali_value(dm, rf_path, 0x3, efuse_value);
	_set_tx_a_cali_value(dm, rf_path, 0x4, efuse_value);
	_set_tx_a_cali_value(dm, rf_path, 0x5, efuse_value);
	_set_tx_a_cali_value(dm, rf_path, 0x6, efuse_value);
	_set_tx_a_cali_value(dm, rf_path, 0x7, efuse_value);
	_set_tx_a_cali_value(dm, rf_path, 0x8, efuse_value);
	_set_tx_a_cali_value(dm, rf_path, 0x9, efuse_value);
	_set_tx_a_cali_value(dm, rf_path, 0xa, efuse_value);
	_set_tx_a_cali_value(dm, rf_path, 0xb, efuse_value);

	/* switch off set TxA bias */
	odm_set_rf_reg(dm, rf_path, 0xEF, 0xFFFFF, 0x0);
}

/*
 * for 8822B PCIE D-cut patch only
 * Normal driver and MP driver need this patch
 */

void phydm_txcurrentcalibration(struct phy_dm_struct *dm)
{
	u8 efuse0x3D8, efuse0x3D7;
	u32 orig_rf0x18_path_a = 0, orig_rf0x18_path_b = 0;

	/* save original 0x18 value */
	orig_rf0x18_path_a = odm_get_rf_reg(dm, ODM_RF_PATH_A, 0x18, 0xFFFFF);
	orig_rf0x18_path_b = odm_get_rf_reg(dm, ODM_RF_PATH_B, 0x18, 0xFFFFF);

	/* define efuse content */
	efuse0x3D8 = dm->efuse0x3d8;
	efuse0x3D7 = dm->efuse0x3d7;

	/* check efuse content to judge whether need to calibration or not */
	if (efuse0x3D7 == 0xFF) {
		ODM_RT_TRACE(
			dm, ODM_COMP_COMMON,
			"efuse content 0x3D7 == 0xFF, No need to do TxA cali\n");
		return;
	}

	/* write RF register for calibration */
	_txa_bias_cali_4_each_path(dm, ODM_RF_PATH_A, efuse0x3D7);
	_txa_bias_cali_4_each_path(dm, ODM_RF_PATH_B, efuse0x3D8);

	/* restore original 0x18 value */
	odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x18, 0xFFFFF, orig_rf0x18_path_a);
	odm_set_rf_reg(dm, ODM_RF_PATH_B, 0x18, 0xFFFFF, orig_rf0x18_path_b);
}

void phydm_hwsetting_8822b(struct phy_dm_struct *dm)
{
	phydm_dynamic_switch_htstf_mumimo_8822b(dm);
}
