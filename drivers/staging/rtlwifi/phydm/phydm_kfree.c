// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

/*============================================================*/
/*include files*/
/*============================================================*/
#include "mp_precomp.h"
#include "phydm_precomp.h"

/*<YuChen, 150720> Add for KFree Feature Requested by RF David.*/
/*This is a phydm API*/

static void phydm_set_kfree_to_rf_8814a(void *dm_void, u8 e_rf_path, u8 data)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct dm_rf_calibration_struct *cali_info = &dm->rf_calibrate_info;
	bool is_odd;

	if ((data % 2) != 0) { /*odd->positive*/
		data = data - 1;
		odm_set_rf_reg(dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(19),
			       1);
		is_odd = true;
	} else { /*even->negative*/
		odm_set_rf_reg(dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(19),
			       0);
		is_odd = false;
	}
	ODM_RT_TRACE(dm, ODM_COMP_MP, "%s(): RF_0x55[19]= %d\n", __func__,
		     is_odd);
	switch (data) {
	case 0:
		odm_set_rf_reg(dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14),
			       0);
		odm_set_rf_reg(dm, e_rf_path, REG_RF_TX_GAIN_OFFSET,
			       BIT(17) | BIT(16) | BIT(15), 0);
		cali_info->kfree_offset[e_rf_path] = 0;
		break;
	case 2:
		odm_set_rf_reg(dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14),
			       1);
		odm_set_rf_reg(dm, e_rf_path, REG_RF_TX_GAIN_OFFSET,
			       BIT(17) | BIT(16) | BIT(15), 0);
		cali_info->kfree_offset[e_rf_path] = 0;
		break;
	case 4:
		odm_set_rf_reg(dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14),
			       0);
		odm_set_rf_reg(dm, e_rf_path, REG_RF_TX_GAIN_OFFSET,
			       BIT(17) | BIT(16) | BIT(15), 1);
		cali_info->kfree_offset[e_rf_path] = 1;
		break;
	case 6:
		odm_set_rf_reg(dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14),
			       1);
		odm_set_rf_reg(dm, e_rf_path, REG_RF_TX_GAIN_OFFSET,
			       BIT(17) | BIT(16) | BIT(15), 1);
		cali_info->kfree_offset[e_rf_path] = 1;
		break;
	case 8:
		odm_set_rf_reg(dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14),
			       0);
		odm_set_rf_reg(dm, e_rf_path, REG_RF_TX_GAIN_OFFSET,
			       BIT(17) | BIT(16) | BIT(15), 2);
		cali_info->kfree_offset[e_rf_path] = 2;
		break;
	case 10:
		odm_set_rf_reg(dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14),
			       1);
		odm_set_rf_reg(dm, e_rf_path, REG_RF_TX_GAIN_OFFSET,
			       BIT(17) | BIT(16) | BIT(15), 2);
		cali_info->kfree_offset[e_rf_path] = 2;
		break;
	case 12:
		odm_set_rf_reg(dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14),
			       0);
		odm_set_rf_reg(dm, e_rf_path, REG_RF_TX_GAIN_OFFSET,
			       BIT(17) | BIT(16) | BIT(15), 3);
		cali_info->kfree_offset[e_rf_path] = 3;
		break;
	case 14:
		odm_set_rf_reg(dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14),
			       1);
		odm_set_rf_reg(dm, e_rf_path, REG_RF_TX_GAIN_OFFSET,
			       BIT(17) | BIT(16) | BIT(15), 3);
		cali_info->kfree_offset[e_rf_path] = 3;
		break;
	case 16:
		odm_set_rf_reg(dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14),
			       0);
		odm_set_rf_reg(dm, e_rf_path, REG_RF_TX_GAIN_OFFSET,
			       BIT(17) | BIT(16) | BIT(15), 4);
		cali_info->kfree_offset[e_rf_path] = 4;
		break;
	case 18:
		odm_set_rf_reg(dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14),
			       1);
		odm_set_rf_reg(dm, e_rf_path, REG_RF_TX_GAIN_OFFSET,
			       BIT(17) | BIT(16) | BIT(15), 4);
		cali_info->kfree_offset[e_rf_path] = 4;
		break;
	case 20:
		odm_set_rf_reg(dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14),
			       0);
		odm_set_rf_reg(dm, e_rf_path, REG_RF_TX_GAIN_OFFSET,
			       BIT(17) | BIT(16) | BIT(15), 5);
		cali_info->kfree_offset[e_rf_path] = 5;
		break;

	default:
		break;
	}

	if (!is_odd) {
		/*that means Kfree offset is negative, we need to record it.*/
		cali_info->kfree_offset[e_rf_path] =
			(-1) * cali_info->kfree_offset[e_rf_path];
		ODM_RT_TRACE(dm, ODM_COMP_MP, "%s(): kfree_offset = %d\n",
			     __func__, cali_info->kfree_offset[e_rf_path]);
	} else {
		ODM_RT_TRACE(dm, ODM_COMP_MP, "%s(): kfree_offset = %d\n",
			     __func__, cali_info->kfree_offset[e_rf_path]);
	}
}

static void phydm_set_kfree_to_rf(void *dm_void, u8 e_rf_path, u8 data)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_RTL8814A)
		phydm_set_kfree_to_rf_8814a(dm, e_rf_path, data);
}

void phydm_config_kfree(void *dm_void, u8 channel_to_sw, u8 *kfree_table)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct dm_rf_calibration_struct *cali_info = &dm->rf_calibrate_info;
	u8 rfpath = 0, max_rf_path = 0;
	u8 channel_idx = 0;

	if (dm->support_ic_type & ODM_RTL8814A)
		max_rf_path = 4; /*0~3*/
	else if (dm->support_ic_type &
		 (ODM_RTL8812 | ODM_RTL8192E | ODM_RTL8822B))
		max_rf_path = 2; /*0~1*/
	else
		max_rf_path = 1;

	ODM_RT_TRACE(dm, ODM_COMP_MP, "===>%s()\n", __func__);

	if (cali_info->reg_rf_kfree_enable == 2) {
		ODM_RT_TRACE(dm, ODM_COMP_MP,
			     "%s(): reg_rf_kfree_enable == 2, Disable\n",
			     __func__);
		return;
	}

	if (cali_info->reg_rf_kfree_enable != 1 &&
	    cali_info->reg_rf_kfree_enable != 0) {
		ODM_RT_TRACE(dm, ODM_COMP_MP, "<===%s()\n", __func__);
		return;
	}

	ODM_RT_TRACE(dm, ODM_COMP_MP, "%s(): reg_rf_kfree_enable == true\n",
		     __func__);
	/*Make sure the targetval is defined*/
	if (((cali_info->reg_rf_kfree_enable == 1) &&
	     (kfree_table[0] != 0xFF)) ||
	    cali_info->rf_kfree_enable) {
		/*if kfree_table[0] == 0xff, means no Kfree*/
		if (*dm->band_type == ODM_BAND_2_4G) {
			if (channel_to_sw <= 14 && channel_to_sw >= 1)
				channel_idx = PHYDM_2G;
		} else if (*dm->band_type == ODM_BAND_5G) {
			if (channel_to_sw >= 36 && channel_to_sw <= 48)
				channel_idx = PHYDM_5GLB1;
			if (channel_to_sw >= 52 && channel_to_sw <= 64)
				channel_idx = PHYDM_5GLB2;
			if (channel_to_sw >= 100 && channel_to_sw <= 120)
				channel_idx = PHYDM_5GMB1;
			if (channel_to_sw >= 124 && channel_to_sw <= 144)
				channel_idx = PHYDM_5GMB2;
			if (channel_to_sw >= 149 && channel_to_sw <= 177)
				channel_idx = PHYDM_5GHB;
		}

		for (rfpath = ODM_RF_PATH_A; rfpath < max_rf_path; rfpath++) {
			ODM_RT_TRACE(dm, ODM_COMP_MP, "%s(): PATH_%d: %#x\n",
				     __func__, rfpath,
				     kfree_table[channel_idx * max_rf_path +
						 rfpath]);
			phydm_set_kfree_to_rf(
				dm, rfpath,
				kfree_table[channel_idx * max_rf_path +
					    rfpath]);
		}
	} else {
		ODM_RT_TRACE(
			dm, ODM_COMP_MP,
			"%s(): targetval not defined, Don't execute KFree Process.\n",
			__func__);
		return;
	}

	ODM_RT_TRACE(dm, ODM_COMP_MP, "<===%s()\n", __func__);
}
