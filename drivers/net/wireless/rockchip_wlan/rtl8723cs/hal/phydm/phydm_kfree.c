/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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

/*============================================================*/
/*include files*/
/*============================================================*/
#include "mp_precomp.h"
#include "phydm_precomp.h"


/*<YuChen, 150720> Add for KFree Feature Requested by RF David.*/
/*This is a phydm API*/

void
phydm_set_kfree_to_rf_8814a(
	void		*p_dm_void,
	u8		e_rf_path,
	u8		data
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm_odm->rf_calibrate_info);
	boolean is_odd;

	if ((data % 2) != 0) {	/*odd->positive*/
		data = data - 1;
		odm_set_rf_reg(p_dm_odm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(19), 1);
		is_odd = true;
	} else {		/*even->negative*/
		odm_set_rf_reg(p_dm_odm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(19), 0);
		is_odd = false;
	}
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_MP, ODM_DBG_LOUD, ("phy_ConfigKFree8814A(): RF_0x55[19]= %d\n", is_odd));
	switch (data) {
	case 0:
		odm_set_rf_reg(p_dm_odm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14), 0);
		odm_set_rf_reg(p_dm_odm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(17) | BIT(16) | BIT(15), 0);
		p_rf_calibrate_info->kfree_offset[e_rf_path] = 0;
		break;
	case 2:
		odm_set_rf_reg(p_dm_odm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14), 1);
		odm_set_rf_reg(p_dm_odm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(17) | BIT(16) | BIT(15), 0);
		p_rf_calibrate_info->kfree_offset[e_rf_path] = 0;
		break;
	case 4:
		odm_set_rf_reg(p_dm_odm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14), 0);
		odm_set_rf_reg(p_dm_odm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(17) | BIT(16) | BIT(15), 1);
		p_rf_calibrate_info->kfree_offset[e_rf_path] = 1;
		break;
	case 6:
		odm_set_rf_reg(p_dm_odm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14), 1);
		odm_set_rf_reg(p_dm_odm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(17) | BIT(16) | BIT(15), 1);
		p_rf_calibrate_info->kfree_offset[e_rf_path] = 1;
		break;
	case 8:
		odm_set_rf_reg(p_dm_odm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14), 0);
		odm_set_rf_reg(p_dm_odm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(17) | BIT(16) | BIT(15), 2);
		p_rf_calibrate_info->kfree_offset[e_rf_path] = 2;
		break;
	case 10:
		odm_set_rf_reg(p_dm_odm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14), 1);
		odm_set_rf_reg(p_dm_odm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(17) | BIT(16) | BIT(15), 2);
		p_rf_calibrate_info->kfree_offset[e_rf_path] = 2;
		break;
	case 12:
		odm_set_rf_reg(p_dm_odm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14), 0);
		odm_set_rf_reg(p_dm_odm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(17) | BIT(16) | BIT(15), 3);
		p_rf_calibrate_info->kfree_offset[e_rf_path] = 3;
		break;
	case 14:
		odm_set_rf_reg(p_dm_odm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14), 1);
		odm_set_rf_reg(p_dm_odm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(17) | BIT(16) | BIT(15), 3);
		p_rf_calibrate_info->kfree_offset[e_rf_path] = 3;
		break;
	case 16:
		odm_set_rf_reg(p_dm_odm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14), 0);
		odm_set_rf_reg(p_dm_odm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(17) | BIT(16) | BIT(15), 4);
		p_rf_calibrate_info->kfree_offset[e_rf_path] = 4;
		break;
	case 18:
		odm_set_rf_reg(p_dm_odm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14), 1);
		odm_set_rf_reg(p_dm_odm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(17) | BIT(16) | BIT(15), 4);
		p_rf_calibrate_info->kfree_offset[e_rf_path] = 4;
		break;
	case 20:
		odm_set_rf_reg(p_dm_odm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14), 0);
		odm_set_rf_reg(p_dm_odm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(17) | BIT(16) | BIT(15), 5);
		p_rf_calibrate_info->kfree_offset[e_rf_path] = 5;
		break;

	default:
		break;
	}

	if (is_odd == false) {
		/*that means Kfree offset is negative, we need to record it.*/
		p_rf_calibrate_info->kfree_offset[e_rf_path] = (-1) * p_rf_calibrate_info->kfree_offset[e_rf_path];
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_MP, ODM_DBG_LOUD, ("phy_ConfigKFree8814A(): kfree_offset = %d\n", p_rf_calibrate_info->kfree_offset[e_rf_path]));
	} else
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_MP, ODM_DBG_LOUD, ("phy_ConfigKFree8814A(): kfree_offset = %d\n", p_rf_calibrate_info->kfree_offset[e_rf_path]));

}


void
phydm_set_kfree_to_rf(
	void		*p_dm_void,
	u8		e_rf_path,
	u8		data
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (p_dm_odm->support_ic_type & ODM_RTL8814A)
		phydm_set_kfree_to_rf_8814a(p_dm_odm, e_rf_path, data);
}

void
phydm_config_kfree(
	void	*p_dm_void,
	u8	channel_to_sw,
	u8	*kfree_table
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm_odm->rf_calibrate_info);
	u8			rfpath = 0, max_rf_path = 0;
	u8			channel_idx = 0;

	if (p_dm_odm->support_ic_type & ODM_RTL8814A)
		max_rf_path = 4;	/*0~3*/
	else if (p_dm_odm->support_ic_type & (ODM_RTL8812 | ODM_RTL8192E | ODM_RTL8822B))
		max_rf_path = 2;	/*0~1*/
	else
		max_rf_path = 1;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_MP, ODM_DBG_LOUD, ("===>phy_ConfigKFree8814A()\n"));

	if (p_rf_calibrate_info->reg_rf_kfree_enable == 2) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_MP, ODM_DBG_LOUD, ("phy_ConfigKFree8814A(): reg_rf_kfree_enable == 2, Disable\n"));
		return;
	} else if (p_rf_calibrate_info->reg_rf_kfree_enable == 1 || p_rf_calibrate_info->reg_rf_kfree_enable == 0) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_MP, ODM_DBG_LOUD, ("phy_ConfigKFree8814A(): reg_rf_kfree_enable == true\n"));
		/*Make sure the targetval is defined*/
		if (((p_rf_calibrate_info->reg_rf_kfree_enable == 1) && (kfree_table[0] != 0xFF)) || (p_rf_calibrate_info->rf_kfree_enable == true)) {
			/*if kfree_table[0] == 0xff, means no Kfree*/
			if (*p_dm_odm->p_band_type == ODM_BAND_2_4G) {
				if (channel_to_sw <= 14 && channel_to_sw >= 1)
					channel_idx = PHYDM_2G;
			} else if (*p_dm_odm->p_band_type == ODM_BAND_5G) {
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

			for (rfpath = ODM_RF_PATH_A;  rfpath < max_rf_path; rfpath++) {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_MP, ODM_DBG_LOUD, ("phydm_kfree(): PATH_%d: %#x\n", rfpath, kfree_table[channel_idx * max_rf_path + rfpath]));
				phydm_set_kfree_to_rf(p_dm_odm, rfpath, kfree_table[channel_idx * max_rf_path + rfpath]);
			}
		} else {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_MP, ODM_DBG_LOUD, ("phy_ConfigKFree8814A(): targetval not defined, Don't execute KFree Process.\n"));
			return;
		}
	}
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_MP, ODM_DBG_LOUD, ("<===phy_ConfigKFree8814A()\n"));
}
