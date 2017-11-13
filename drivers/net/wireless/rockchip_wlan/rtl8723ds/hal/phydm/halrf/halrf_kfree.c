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
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);
	boolean is_odd;

	if ((data % 2) != 0) {	/*odd->positive*/
		data = data - 1;
		odm_set_rf_reg(p_dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(19), 1);
		is_odd = true;
	} else {		/*even->negative*/
		odm_set_rf_reg(p_dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(19), 0);
		is_odd = false;
	}
	ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("phy_ConfigKFree8814A(): RF_0x55[19]= %d\n", is_odd));
	switch (data) {
	case 0:
		odm_set_rf_reg(p_dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14), 0);
		odm_set_rf_reg(p_dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(17) | BIT(16) | BIT(15), 0);
		p_rf_calibrate_info->kfree_offset[e_rf_path] = 0;
		break;
	case 2:
		odm_set_rf_reg(p_dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14), 1);
		odm_set_rf_reg(p_dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(17) | BIT(16) | BIT(15), 0);
		p_rf_calibrate_info->kfree_offset[e_rf_path] = 0;
		break;
	case 4:
		odm_set_rf_reg(p_dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14), 0);
		odm_set_rf_reg(p_dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(17) | BIT(16) | BIT(15), 1);
		p_rf_calibrate_info->kfree_offset[e_rf_path] = 1;
		break;
	case 6:
		odm_set_rf_reg(p_dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14), 1);
		odm_set_rf_reg(p_dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(17) | BIT(16) | BIT(15), 1);
		p_rf_calibrate_info->kfree_offset[e_rf_path] = 1;
		break;
	case 8:
		odm_set_rf_reg(p_dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14), 0);
		odm_set_rf_reg(p_dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(17) | BIT(16) | BIT(15), 2);
		p_rf_calibrate_info->kfree_offset[e_rf_path] = 2;
		break;
	case 10:
		odm_set_rf_reg(p_dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14), 1);
		odm_set_rf_reg(p_dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(17) | BIT(16) | BIT(15), 2);
		p_rf_calibrate_info->kfree_offset[e_rf_path] = 2;
		break;
	case 12:
		odm_set_rf_reg(p_dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14), 0);
		odm_set_rf_reg(p_dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(17) | BIT(16) | BIT(15), 3);
		p_rf_calibrate_info->kfree_offset[e_rf_path] = 3;
		break;
	case 14:
		odm_set_rf_reg(p_dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14), 1);
		odm_set_rf_reg(p_dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(17) | BIT(16) | BIT(15), 3);
		p_rf_calibrate_info->kfree_offset[e_rf_path] = 3;
		break;
	case 16:
		odm_set_rf_reg(p_dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14), 0);
		odm_set_rf_reg(p_dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(17) | BIT(16) | BIT(15), 4);
		p_rf_calibrate_info->kfree_offset[e_rf_path] = 4;
		break;
	case 18:
		odm_set_rf_reg(p_dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14), 1);
		odm_set_rf_reg(p_dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(17) | BIT(16) | BIT(15), 4);
		p_rf_calibrate_info->kfree_offset[e_rf_path] = 4;
		break;
	case 20:
		odm_set_rf_reg(p_dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(14), 0);
		odm_set_rf_reg(p_dm, e_rf_path, REG_RF_TX_GAIN_OFFSET, BIT(17) | BIT(16) | BIT(15), 5);
		p_rf_calibrate_info->kfree_offset[e_rf_path] = 5;
		break;

	default:
		break;
	}

	if (is_odd == false) {
		/*that means Kfree offset is negative, we need to record it.*/
		p_rf_calibrate_info->kfree_offset[e_rf_path] = (-1) * p_rf_calibrate_info->kfree_offset[e_rf_path];
		ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("phy_ConfigKFree8814A(): kfree_offset = %d\n", p_rf_calibrate_info->kfree_offset[e_rf_path]));
	} else
		ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("phy_ConfigKFree8814A(): kfree_offset = %d\n", p_rf_calibrate_info->kfree_offset[e_rf_path]));

}



//
//
//
void
phydm_get_thermal_trim_offset_8821c(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct odm_power_trim_data	*p_power_trim_info = &(p_dm->power_trim_data);

	u8 pg_therm = 0xff;

	odm_efuse_one_byte_read(p_dm, PPG_THERMAL_OFFSET_8821C, &pg_therm, false);

	if (pg_therm != 0xff) {
		pg_therm = pg_therm & 0x1f;
		if ((pg_therm & BIT(0)) == 0)
			p_power_trim_info->thermal = (-1 * (pg_therm >> 1));
		else
			p_power_trim_info->thermal = (pg_therm >> 1);

		p_power_trim_info->flag |= KFREE_FLAG_THERMAL_K_ON;
	}

	ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("[kfree] 8821c thermal trim flag:0x%02x\n", p_power_trim_info->flag));

	if (p_power_trim_info->flag & KFREE_FLAG_THERMAL_K_ON)
		ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("[kfree] 8821c thermal:%d\n", p_power_trim_info->thermal));
}



void
phydm_get_power_trim_offset_8821c(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct odm_power_trim_data	*p_power_trim_info = &(p_dm->power_trim_data);

	u8 pg_power = 0xff, i;

	odm_efuse_one_byte_read(p_dm, PPG_BB_GAIN_2G_TXAB_OFFSET_8821C, &pg_power, false);

	if (pg_power != 0xff) {
		p_power_trim_info->bb_gain[0][0] = pg_power;
		odm_efuse_one_byte_read(p_dm, PPG_BB_GAIN_5GL1_TXA_OFFSET_8821C, &pg_power, false);
		p_power_trim_info->bb_gain[1][0] = pg_power;
		odm_efuse_one_byte_read(p_dm, PPG_BB_GAIN_5GL2_TXA_OFFSET_8821C, &pg_power, false);
		p_power_trim_info->bb_gain[2][0] = pg_power;
		odm_efuse_one_byte_read(p_dm, PPG_BB_GAIN_5GM1_TXA_OFFSET_8821C, &pg_power, false);
		p_power_trim_info->bb_gain[3][0] = pg_power;
		odm_efuse_one_byte_read(p_dm, PPG_BB_GAIN_5GM2_TXA_OFFSET_8821C, &pg_power, false);
		p_power_trim_info->bb_gain[4][0] = pg_power;
		odm_efuse_one_byte_read(p_dm, PPG_BB_GAIN_5GH1_TXA_OFFSET_8821C, &pg_power, false);
		p_power_trim_info->bb_gain[5][0] = pg_power;
		p_power_trim_info->flag = p_power_trim_info->flag | KFREE_FLAG_ON | KFREE_FLAG_ON_2G | KFREE_FLAG_ON_5G;
	}

	ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("[kfree] 8821c power trim flag:0x%02x\n", p_power_trim_info->flag));

	if (p_power_trim_info->flag & KFREE_FLAG_ON) {
		for (i = 0; i < KFREE_BAND_NUM; i++)
			ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("[kfree] 8821c power_trim_data->bb_gain[%d][0]=0x%X\n", i, p_power_trim_info->bb_gain[i][0]));
	}
}



void
phydm_set_kfree_to_rf_8821c(
	void		*p_dm_void,
	u8		e_rf_path,
	boolean		wlg_btg,
	u8		data
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);
	u8	wlg, btg;

	odm_set_rf_reg(p_dm, e_rf_path, 0xde, BIT(0), 1);
	odm_set_rf_reg(p_dm, e_rf_path, 0xde, BIT(5), 1);
	odm_set_rf_reg(p_dm, e_rf_path, 0x55, BIT(6), 1);
	odm_set_rf_reg(p_dm, e_rf_path, 0x65, BIT(6), 1);

	if (wlg_btg == true) {
		wlg = data & 0xf;
		btg = (data & 0xf0) >> 4;

		odm_set_rf_reg(p_dm, e_rf_path, 0x55, BIT(19), (wlg & BIT(0)));
		odm_set_rf_reg(p_dm, e_rf_path, 0x55, (BIT(18) | BIT(17) | BIT(16) | BIT(15) | BIT(14)), (wlg >> 1));

		odm_set_rf_reg(p_dm, e_rf_path, 0x65, BIT(19), (btg & BIT(0)));
		odm_set_rf_reg(p_dm, e_rf_path, 0x65, (BIT(18) | BIT(17) | BIT(16) | BIT(15) | BIT(14)), (btg >> 1));
	} else {
		odm_set_rf_reg(p_dm, e_rf_path, 0x55, BIT(19), (data & BIT(0)));
		odm_set_rf_reg(p_dm, e_rf_path, 0x55, (BIT(18) | BIT(17) | BIT(16) | BIT(15) | BIT(14)), ((data & 0x1f) >> 1));
	}

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE,
		("[kfree] 8821c 0x55[19:14]=0x%X 0x65[19:14]=0x%X\n",
		odm_get_rf_reg(p_dm, e_rf_path, 0x55, (BIT(19) | BIT(18) | BIT(17) | BIT(16) | BIT(15) | BIT(14))),
		odm_get_rf_reg(p_dm, e_rf_path, 0x65, (BIT(19) | BIT(18) | BIT(17) | BIT(16) | BIT(15) | BIT(14)))
		));
}



void
phydm_clear_kfree_to_rf_8821c(
	void		*p_dm_void,
	u8		e_rf_path,
	u8		data
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);

	odm_set_rf_reg(p_dm, e_rf_path, 0xde, BIT(0), 1);
	odm_set_rf_reg(p_dm, e_rf_path, 0xde, BIT(5), 1);
	odm_set_rf_reg(p_dm, e_rf_path, 0x55, BIT(6), 1);
	odm_set_rf_reg(p_dm, e_rf_path, 0x65, BIT(6), 1);

	odm_set_rf_reg(p_dm, e_rf_path, 0x55, BIT(19), (data & BIT(0)));
	odm_set_rf_reg(p_dm, e_rf_path, 0x55, (BIT(18) | BIT(17) | BIT(16) | BIT(15) | BIT(14)), (data >> 1));

	odm_set_rf_reg(p_dm, e_rf_path, 0x65, BIT(19), (data & BIT(0)));
	odm_set_rf_reg(p_dm, e_rf_path, 0x65, (BIT(18) | BIT(17) | BIT(16) | BIT(15) | BIT(14)), (data >> 1));

	odm_set_rf_reg(p_dm, e_rf_path, 0xde, BIT(0), 0);
	odm_set_rf_reg(p_dm, e_rf_path, 0xde, BIT(5), 0);
	odm_set_rf_reg(p_dm, e_rf_path, 0x55, BIT(6), 0);
	odm_set_rf_reg(p_dm, e_rf_path, 0x65, BIT(6), 0);


	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_TRACE,
		("[kfree] 8821c 0x55[19:14]=0x%X 0x65[19:14]=0x%X\n",
		odm_get_rf_reg(p_dm, e_rf_path, 0x55, (BIT(19) | BIT(18) | BIT(17) | BIT(16) | BIT(15) | BIT(14))),
		odm_get_rf_reg(p_dm, e_rf_path, 0x65, (BIT(19) | BIT(18) | BIT(17) | BIT(16) | BIT(15) | BIT(14)))
		));
}



void
phydm_get_thermal_trim_offset_8822b(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct odm_power_trim_data	*p_power_trim_info = &(p_dm->power_trim_data);

	u8 pg_therm = 0xff;

#if 0
	u32	thermal_trim_enable = 0xff;

	odm_efuse_logical_map_read(p_dm, 1, 0xc8, &thermal_trim_enable);

	ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("[kfree] 8822b 0xc8:0x%2x\n", thermal_trim_enable));

	thermal_trim_enable = (thermal_trim_enable & BIT(5)) >> 5;

	ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("[kfree] 8822b thermal trim Enable:%d\n", thermal_trim_enable));

	if ((p_rf_calibrate_info->reg_rf_kfree_enable == 0 && thermal_trim_enable == 1) ||
		p_rf_calibrate_info->reg_rf_kfree_enable == 1) {
#endif

		odm_efuse_one_byte_read(p_dm, PPG_THERMAL_OFFSET, &pg_therm, false);

		if (pg_therm != 0xff) {
			pg_therm = pg_therm & 0x1f;
			if ((pg_therm & BIT(0)) == 0)
				p_power_trim_info->thermal = (-1 * (pg_therm >> 1));
			else
				p_power_trim_info->thermal = (pg_therm >> 1);

			p_power_trim_info->flag |= KFREE_FLAG_THERMAL_K_ON;
		}

		ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("[kfree] 8822b thermal trim flag:0x%02x\n", p_power_trim_info->flag));

		if (p_power_trim_info->flag & KFREE_FLAG_THERMAL_K_ON)
			ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("[kfree] 8822b thermal:%d\n", p_power_trim_info->thermal));
#if 0
	} else
		return;
#endif

}



void
phydm_get_power_trim_offset_8822b(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct odm_power_trim_data	*p_power_trim_info = &(p_dm->power_trim_data);
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);

	u8 pg_power = 0xff, i, j;

#if 0
	u32	power_trim_enable = 0xff;

	odm_efuse_logical_map_read(p_dm, 1, 0xc8, &power_trim_enable);

	ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("[kfree] 8822b 0xc8:0x%2x\n", power_trim_enable));

	power_trim_enable = (power_trim_enable & BIT(4)) >> 4;

	ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("[kfree] 8822b power trim Enable:%d\n", power_trim_enable));

	if ((p_rf_calibrate_info->reg_rf_kfree_enable == 0 && power_trim_enable == 1) ||
		p_rf_calibrate_info->reg_rf_kfree_enable == 1) {
#endif

		odm_efuse_one_byte_read(p_dm, PPG_BB_GAIN_2G_TXAB_OFFSET, &pg_power, false);

		if (pg_power != 0xff) {
			/*Path A*/
			odm_efuse_one_byte_read(p_dm, PPG_BB_GAIN_2G_TXAB_OFFSET, &pg_power, false);
			p_power_trim_info->bb_gain[0][0] = (pg_power & 0xf);

			/*Path B*/
			odm_efuse_one_byte_read(p_dm, PPG_BB_GAIN_2G_TXAB_OFFSET, &pg_power, false);
			p_power_trim_info->bb_gain[0][1] = ((pg_power & 0xf0) >> 4);

			p_power_trim_info->flag |= KFREE_FLAG_ON_2G;
			p_power_trim_info->flag |= KFREE_FLAG_ON;
		}

		odm_efuse_one_byte_read(p_dm, PPG_BB_GAIN_5GL1_TXA_OFFSET, &pg_power, false);
		
		if (pg_power != 0xff) {
			/*Path A*/
			odm_efuse_one_byte_read(p_dm, PPG_BB_GAIN_5GL1_TXA_OFFSET, &pg_power, false);
			p_power_trim_info->bb_gain[1][0] = pg_power;
			odm_efuse_one_byte_read(p_dm, PPG_BB_GAIN_5GL2_TXA_OFFSET, &pg_power, false);
			p_power_trim_info->bb_gain[2][0] = pg_power;
			odm_efuse_one_byte_read(p_dm, PPG_BB_GAIN_5GM1_TXA_OFFSET, &pg_power, false);
			p_power_trim_info->bb_gain[3][0] = pg_power;
			odm_efuse_one_byte_read(p_dm, PPG_BB_GAIN_5GM2_TXA_OFFSET, &pg_power, false);
			p_power_trim_info->bb_gain[4][0] = pg_power;
			odm_efuse_one_byte_read(p_dm, PPG_BB_GAIN_5GH1_TXA_OFFSET, &pg_power, false);
			p_power_trim_info->bb_gain[5][0] = pg_power;

			/*Path B*/
			odm_efuse_one_byte_read(p_dm, PPG_BB_GAIN_5GL1_TXB_OFFSET, &pg_power, false);
			p_power_trim_info->bb_gain[1][1] = pg_power;
			odm_efuse_one_byte_read(p_dm, PPG_BB_GAIN_5GL2_TXB_OFFSET, &pg_power, false);
			p_power_trim_info->bb_gain[2][1] = pg_power;
			odm_efuse_one_byte_read(p_dm, PPG_BB_GAIN_5GM1_TXB_OFFSET, &pg_power, false);
			p_power_trim_info->bb_gain[3][1] = pg_power;
			odm_efuse_one_byte_read(p_dm, PPG_BB_GAIN_5GM2_TXB_OFFSET, &pg_power, false);
			p_power_trim_info->bb_gain[4][1] = pg_power;
			odm_efuse_one_byte_read(p_dm, PPG_BB_GAIN_5GH1_TXB_OFFSET, &pg_power, false);
			p_power_trim_info->bb_gain[5][1] = pg_power;
			
			p_power_trim_info->flag |= KFREE_FLAG_ON_5G;
			p_power_trim_info->flag |= KFREE_FLAG_ON;
		}

		ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("[kfree] 8822b power trim flag:0x%02x\n", p_power_trim_info->flag));

		if (p_power_trim_info->flag & KFREE_FLAG_ON) {
			for (i = 0; i < KFREE_BAND_NUM; i++) {
				for (j = 0; j < 2; j++)
					ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("[kfree] 8822b power_trim_data->bb_gain[%d][%d]=0x%X\n", i, j, p_power_trim_info->bb_gain[i][j]));
			}
		}
#if 0
	} else
		return;
#endif
}



void
phydm_set_pa_bias_to_rf_8822b(
	void		*p_dm_void,
	u8		e_rf_path,
	s8		tx_pa_bias
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);
	u32	rf_reg_51 = 0, rf_reg_52 = 0, rf_reg_3f = 0;

	rf_reg_51 = odm_get_rf_reg(p_dm, e_rf_path, 0x51, RFREGOFFSETMASK);
	rf_reg_52 = odm_get_rf_reg(p_dm, e_rf_path, 0x52, RFREGOFFSETMASK);

	ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("[kfree] 8822b 2g rf(0x51)=0x%X rf(0x52)=0x%X path=%d\n",
 		rf_reg_51, rf_reg_52, e_rf_path));

	/*rf3f => rf52[19:17] = rf3f[2:0] rf52[16:15] = rf3f[4:3] rf52[3:0] = rf3f[8:5]*/
	/*rf3f => rf51[6:3] = rf3f[12:9] rf52[13] = rf3f[13]*/
	rf_reg_3f = ((rf_reg_52 & 0xe0000) >> 17) |
					(((rf_reg_52 & 0x18000) >> 15) << 3) |
					((rf_reg_52 & 0xf) << 5) |
					(((rf_reg_51 & 0x78) >> 3) << 9) |
					(((rf_reg_52 & 0x2000) >> 13) << 13);

	ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD,
			("[kfree] 8822b 2g original tx_pa_bias=%d rf_reg_3f=0x%X path=%d\n",
			tx_pa_bias, rf_reg_3f, e_rf_path));

	tx_pa_bias = (s8)((rf_reg_3f & (BIT(12) | BIT(11) | BIT(10) | BIT(9))) >> 9) + tx_pa_bias;

	if (tx_pa_bias < 0)
		tx_pa_bias = 0;
	else if (tx_pa_bias > 7)
		tx_pa_bias = 7;

	rf_reg_3f = ((rf_reg_3f & 0xfe1ff) | (tx_pa_bias << 9));

	ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD,
			("[kfree] 8822b 2g offset efuse 0x3d5 0x3d6 tx_pa_bias=%d rf_reg_3f=0x%X path=%d\n",
			tx_pa_bias, rf_reg_3f, e_rf_path));

	odm_set_rf_reg(p_dm, e_rf_path, 0xef, BIT(10), 0x1);
	odm_set_rf_reg(p_dm, e_rf_path, 0x33, RFREGOFFSETMASK, 0x0);
	odm_set_rf_reg(p_dm, e_rf_path, 0x3f, RFREGOFFSETMASK, rf_reg_3f);
	odm_set_rf_reg(p_dm, e_rf_path, 0x33, BIT(0), 0x1);
	odm_set_rf_reg(p_dm, e_rf_path, 0x3f, RFREGOFFSETMASK, rf_reg_3f);
	odm_set_rf_reg(p_dm, e_rf_path, 0x33, BIT(1), 0x1);
	odm_set_rf_reg(p_dm, e_rf_path, 0x3f, RFREGOFFSETMASK, rf_reg_3f);
	odm_set_rf_reg(p_dm, e_rf_path, 0x33, (BIT(1) | BIT(0)), 0x3);
	odm_set_rf_reg(p_dm, e_rf_path, 0x3f, RFREGOFFSETMASK, rf_reg_3f);
	odm_set_rf_reg(p_dm, e_rf_path, 0xef, BIT(10), 0x0);

	ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD,
		("[kfree] 8822b 2g tx pa bias rf_0x3f(0x%X) path=%d\n",
		odm_get_rf_reg(p_dm, e_rf_path, 0x3f, (BIT(12) | BIT(11) | BIT(10) | BIT(9))), e_rf_path));
}



void
phydm_get_pa_bias_offset_8822b(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct odm_power_trim_data	*p_power_trim_info = &(p_dm->power_trim_data);

	u8 pg_pa_bias = 0xff, e_rf_path = 0;
	s8 tx_pa_bias[2] = {0};

	odm_efuse_one_byte_read(p_dm, PPG_PA_BIAS_2G_TXA_OFFSET, &pg_pa_bias, false);

	if (pg_pa_bias != 0xff) {
		/*paht a*/
		odm_efuse_one_byte_read(p_dm, PPG_PA_BIAS_2G_TXA_OFFSET, &pg_pa_bias, false);
		pg_pa_bias = pg_pa_bias & 0xf;
		
		if ((pg_pa_bias & BIT(0)) == 0)
			tx_pa_bias[0] = (-1 * (pg_pa_bias >> 1));
		else
			tx_pa_bias[0] = (pg_pa_bias >> 1);

		/*paht b*/
		odm_efuse_one_byte_read(p_dm, PPG_PA_BIAS_2G_TXB_OFFSET, &pg_pa_bias, false);
		pg_pa_bias = pg_pa_bias & 0xf;
		
		if ((pg_pa_bias & BIT(0)) == 0)
			tx_pa_bias[1] = (-1 * (pg_pa_bias >> 1));
		else
			tx_pa_bias[1] = (pg_pa_bias >> 1);

		ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("[kfree] 8822b 2g tx_patha_pa_bias:%d   tx_pathb_pa_bias:%d\n", tx_pa_bias[0], tx_pa_bias[1]));

		for (e_rf_path = RF_PATH_A;  e_rf_path < 2; e_rf_path++)
			phydm_set_pa_bias_to_rf_8822b(p_dm, e_rf_path, tx_pa_bias[e_rf_path]);
	}
	else
		ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("[kfree] 8822b 2g tx pa bias no pg\n"));
}



void
phydm_set_kfree_to_rf_8822b(
	void		*p_dm_void,
	u8		e_rf_path,
	u8		data
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);

	odm_set_rf_reg(p_dm, e_rf_path, 0xde, BIT(0), 1);
	odm_set_rf_reg(p_dm, e_rf_path, 0xde, BIT(4), 1);
	odm_set_rf_reg(p_dm, e_rf_path, 0x65, MASKLWORD, 0x9000);
	odm_set_rf_reg(p_dm, e_rf_path, 0x55, BIT(5), 1);

	odm_set_rf_reg(p_dm, e_rf_path, 0x55, BIT(19), (data & BIT(0)));
	odm_set_rf_reg(p_dm, e_rf_path, 0x55, (BIT(18) | BIT(17) | BIT(16) | BIT(15) | BIT(14)), ((data & 0x1f) >> 1));

	ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD,
		("[kfree] 8822b 0x55[19:14]=0x%X path=%d\n",
		odm_get_rf_reg(p_dm, e_rf_path, 0x55, (BIT(19) | BIT(18) | BIT(17) | BIT(16) | BIT(15) | BIT(14))),
		e_rf_path
		));
}



void
phydm_clear_kfree_to_rf_8822b(
	void		*p_dm_void,
	u8		e_rf_path,
	u8		data
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);

	odm_set_rf_reg(p_dm, e_rf_path, 0xde, BIT(0), 1);
	odm_set_rf_reg(p_dm, e_rf_path, 0xde, BIT(4), 1);
	odm_set_rf_reg(p_dm, e_rf_path, 0x65, MASKLWORD, 0x9000);
	odm_set_rf_reg(p_dm, e_rf_path, 0x55, BIT(5), 1);

	odm_set_rf_reg(p_dm, e_rf_path, 0x55, BIT(19), (data & BIT(0)));
	odm_set_rf_reg(p_dm, e_rf_path, 0x55, (BIT(18) | BIT(17) | BIT(16) | BIT(15) | BIT(14)), ((data & 0x1f) >> 1));

	odm_set_rf_reg(p_dm, e_rf_path, 0xde, BIT(0), 0);
	odm_set_rf_reg(p_dm, e_rf_path, 0xde, BIT(4), 1);
	odm_set_rf_reg(p_dm, e_rf_path, 0x65, MASKLWORD, 0x9000);
	odm_set_rf_reg(p_dm, e_rf_path, 0x55, BIT(5), 0);
	odm_set_rf_reg(p_dm, e_rf_path, 0x55, BIT(7), 0);

	ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD,
		("[kfree] 8822b clear power trim 0x55[19:14]=0x%X path=%d\n",
		odm_get_rf_reg(p_dm, e_rf_path, 0x55, (BIT(19) | BIT(18) | BIT(17) | BIT(16) | BIT(15) | BIT(14))),
		e_rf_path
		));
}



void
phydm_set_kfree_to_rf(
	void		*p_dm_void,
	u8		e_rf_path,
	u8		data
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (p_dm->support_ic_type & ODM_RTL8814A)
		phydm_set_kfree_to_rf_8814a(p_dm, e_rf_path, data);

	if ((p_dm->support_ic_type & ODM_RTL8821C) && (*p_dm->p_band_type == ODM_BAND_2_4G))
		phydm_set_kfree_to_rf_8821c(p_dm, e_rf_path, true, data);
	else if (p_dm->support_ic_type & ODM_RTL8821C)
		phydm_set_kfree_to_rf_8821c(p_dm, e_rf_path, false, data);

	if (p_dm->support_ic_type & ODM_RTL8822B)
		phydm_set_kfree_to_rf_8822b(p_dm, e_rf_path, data);
}



void
phydm_clear_kfree_to_rf(
	void		*p_dm_void,
	u8		e_rf_path,
	u8		data
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (p_dm->support_ic_type & ODM_RTL8822B)
		phydm_clear_kfree_to_rf_8822b(p_dm, e_rf_path, 1);

	if (p_dm->support_ic_type & ODM_RTL8821C)
		phydm_clear_kfree_to_rf_8821c(p_dm, e_rf_path, 1);
}




void
phydm_get_thermal_trim_offset(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm->adapter;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);
	PEFUSE_HAL		pEfuseHal = &(p_hal_data->EfuseHal);
	u1Byte			eFuseContent[DCMD_EFUSE_MAX_SECTION_NUM * EFUSE_MAX_WORD_UNIT * 2];

	if (HAL_MAC_Dump_EFUSE(&GET_HAL_MAC_INFO(adapter), EFUSE_WIFI, eFuseContent, pEfuseHal->PhysicalLen_WiFi, HAL_MAC_EFUSE_PHYSICAL, HAL_MAC_EFUSE_PARSE_DRV) != RT_STATUS_SUCCESS)
		ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("[kfree] dump efuse fail !!!\n"));
#endif

	if (p_dm->support_ic_type & ODM_RTL8821C)
		phydm_get_thermal_trim_offset_8821c(p_dm_void);
	else if (p_dm->support_ic_type & ODM_RTL8822B)
		phydm_get_thermal_trim_offset_8822b(p_dm_void);
}



void
phydm_get_power_trim_offset(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm->adapter;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);
	PEFUSE_HAL		pEfuseHal = &(p_hal_data->EfuseHal);
	u1Byte			eFuseContent[DCMD_EFUSE_MAX_SECTION_NUM * EFUSE_MAX_WORD_UNIT * 2];

	if (HAL_MAC_Dump_EFUSE(&GET_HAL_MAC_INFO(adapter), EFUSE_WIFI, eFuseContent, pEfuseHal->PhysicalLen_WiFi, HAL_MAC_EFUSE_PHYSICAL, HAL_MAC_EFUSE_PARSE_DRV) != RT_STATUS_SUCCESS)
		ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("[kfree] dump efuse fail !!!\n"));
#endif

	if (p_dm->support_ic_type & ODM_RTL8821C)
		phydm_get_power_trim_offset_8821c(p_dm_void);
	else if (p_dm->support_ic_type & ODM_RTL8822B)
		phydm_get_power_trim_offset_8822b(p_dm_void);
}



void
phydm_get_pa_bias_offset(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER		*adapter = p_dm->adapter;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);
	PEFUSE_HAL		pEfuseHal = &(p_hal_data->EfuseHal);
	u1Byte			eFuseContent[DCMD_EFUSE_MAX_SECTION_NUM * EFUSE_MAX_WORD_UNIT * 2];

	if (HAL_MAC_Dump_EFUSE(&GET_HAL_MAC_INFO(adapter), EFUSE_WIFI, eFuseContent, pEfuseHal->PhysicalLen_WiFi, HAL_MAC_EFUSE_PHYSICAL, HAL_MAC_EFUSE_PARSE_DRV) != RT_STATUS_SUCCESS)
		ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("[kfree] dump efuse fail !!!\n"));
#endif

	if (p_dm->support_ic_type & ODM_RTL8822B)
		phydm_get_pa_bias_offset_8822b(p_dm_void);
}



s8
phydm_get_thermal_offset(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct odm_power_trim_data	*p_power_trim_info = &(p_dm->power_trim_data);

	if (p_power_trim_info->flag & KFREE_FLAG_THERMAL_K_ON)
		return p_power_trim_info->thermal;
	else
		return 0;
}



void
phydm_config_kfree(
	void	*p_dm_void,
	u8	channel_to_sw
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);
	struct odm_power_trim_data	*p_power_trim_info = &(p_dm->power_trim_data);

	u8			rfpath = 0, max_rf_path = 0;
	u8			channel_idx = 0, i, j;

	if (p_dm->support_ic_type & ODM_RTL8814A)
		max_rf_path = 4;	/*0~3*/
	else if (p_dm->support_ic_type & (ODM_RTL8812 | ODM_RTL8192E | ODM_RTL8822B))
		max_rf_path = 2;	/*0~1*/
	else if (p_dm->support_ic_type & ODM_RTL8821C)
		max_rf_path = 1;

	ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("===>[kfree] phy_ConfigKFree()\n"));

	if (p_rf_calibrate_info->reg_rf_kfree_enable == 2) {
		ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("[kfree] phy_ConfigKFree(): reg_rf_kfree_enable == 2, Disable\n"));
		return;
	} else if (p_rf_calibrate_info->reg_rf_kfree_enable == 1 || p_rf_calibrate_info->reg_rf_kfree_enable == 0) {
		ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("[kfree] phy_ConfigKFree(): reg_rf_kfree_enable == true\n"));
		/*Make sure the targetval is defined*/
		if (p_power_trim_info->flag & KFREE_FLAG_ON) {
			/*if kfree_table[0] == 0xff, means no Kfree*/

			for (i = 0; i < KFREE_BAND_NUM; i++) {
				for (j = 0; j < max_rf_path; j++)
					ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("[kfree] power_trim_data->bb_gain[%d][%d]=0x%X\n", i, j, p_power_trim_info->bb_gain[i][j]));
			}

			if (*p_dm->p_band_type == ODM_BAND_2_4G && p_power_trim_info->flag & KFREE_FLAG_ON_2G) {
				
				if (channel_to_sw >= 1 && channel_to_sw <= 14)
					channel_idx = PHYDM_2G;

				for (rfpath = RF_PATH_A;  rfpath < max_rf_path; rfpath++) {
					ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("[kfree] phydm_kfree(): channel_to_sw=%d PATH_%d bb_gain:0x%X\n", channel_to_sw, rfpath, p_power_trim_info->bb_gain[channel_idx][rfpath]));
					phydm_set_kfree_to_rf(p_dm, rfpath, p_power_trim_info->bb_gain[channel_idx][rfpath]);
				}

			} else if (*p_dm->p_band_type == ODM_BAND_5G && p_power_trim_info->flag & KFREE_FLAG_ON_5G) {

				if (channel_to_sw >= 36 && channel_to_sw <= 48)
					channel_idx = PHYDM_5GLB1;
				if (channel_to_sw >= 52 && channel_to_sw <= 64)
					channel_idx = PHYDM_5GLB2;
				if (channel_to_sw >= 100 && channel_to_sw <= 120)
					channel_idx = PHYDM_5GMB1;
				if (channel_to_sw >= 122 && channel_to_sw <= 144)
					channel_idx = PHYDM_5GMB2;
				if (channel_to_sw >= 149 && channel_to_sw <= 177)
					channel_idx = PHYDM_5GHB;

				for (rfpath = RF_PATH_A;  rfpath < max_rf_path; rfpath++) {
					ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("[kfree] phydm_kfree(): channel_to_sw=%d PATH_%d bb_gain:0x%X\n", channel_to_sw, rfpath, p_power_trim_info->bb_gain[channel_idx][rfpath]));
					phydm_set_kfree_to_rf(p_dm, rfpath, p_power_trim_info->bb_gain[channel_idx][rfpath]);
				}
			} else {
				ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("[kfree] Set default Register\n"));
				for (rfpath = RF_PATH_A;  rfpath < max_rf_path; rfpath++)
					phydm_clear_kfree_to_rf(p_dm, rfpath, p_power_trim_info->bb_gain[channel_idx][rfpath]);
			}
		} else {
			ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("[kfree] phy_ConfigKFree(): targetval not defined, Don't execute KFree Process.\n"));
			return;
		}
	}
	ODM_RT_TRACE(p_dm, ODM_COMP_MP, ODM_DBG_LOUD, ("<===[kfree] phy_ConfigKFree()\n"));
}
