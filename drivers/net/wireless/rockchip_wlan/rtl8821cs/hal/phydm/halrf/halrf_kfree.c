/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017  Realtek Corporation.
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

/*@============================================================*/
/*@include files*/
/*@============================================================*/
#include "mp_precomp.h"
#include "phydm_precomp.h"

/*@<YuChen, 150720> Add for KFree Feature Requested by RF David.*/
/*@This is a phydm API*/

void phydm_set_kfree_to_rf_8814a(void *dm_void, u8 e_rf_path, u8 data)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct dm_rf_calibration_struct *cali_info = &dm->rf_calibrate_info;
	boolean is_odd;
	u32 tx_gain_bitmask = (BIT(17) | BIT(16) | BIT(15));

	if ((data % 2) != 0) { /*odd->positive*/
		data = data - 1;
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(19), 1);
		is_odd = true;
	} else { /*even->negative*/
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(19), 0);
		is_odd = false;
	}
	RF_DBG(dm, DBG_RF_MP, "phy_ConfigKFree8814A(): RF_0x55[19]= %d\n",
	       is_odd);
	switch (data) {
	case 0:
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(14), 0);
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, tx_gain_bitmask, 0);
		cali_info->kfree_offset[e_rf_path] = 0;
		break;
	case 2:
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(14), 1);
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, tx_gain_bitmask, 0);
		cali_info->kfree_offset[e_rf_path] = 0;
		break;
	case 4:
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(14), 0);
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, tx_gain_bitmask, 1);
		cali_info->kfree_offset[e_rf_path] = 1;
		break;
	case 6:
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(14), 1);
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, tx_gain_bitmask, 1);
		cali_info->kfree_offset[e_rf_path] = 1;
		break;
	case 8:
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(14), 0);
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, tx_gain_bitmask, 2);
		cali_info->kfree_offset[e_rf_path] = 2;
		break;
	case 10:
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(14), 1);
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, tx_gain_bitmask, 2);
		cali_info->kfree_offset[e_rf_path] = 2;
		break;
	case 12:
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(14), 0);
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, tx_gain_bitmask, 3);
		cali_info->kfree_offset[e_rf_path] = 3;
		break;
	case 14:
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(14), 1);
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, tx_gain_bitmask, 3);
		cali_info->kfree_offset[e_rf_path] = 3;
		break;
	case 16:
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(14), 0);
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, tx_gain_bitmask, 4);
		cali_info->kfree_offset[e_rf_path] = 4;
		break;
	case 18:
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(14), 1);
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, tx_gain_bitmask, 4);
		cali_info->kfree_offset[e_rf_path] = 4;
		break;
	case 20:
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(14), 0);
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, tx_gain_bitmask, 5);
		cali_info->kfree_offset[e_rf_path] = 5;
		break;

	default:
		break;
	}

	if (!is_odd) {
		/*that means Kfree offset is negative, we need to record it.*/
		cali_info->kfree_offset[e_rf_path] =
				(-1) * cali_info->kfree_offset[e_rf_path];
		RF_DBG(dm, DBG_RF_MP,
		       "phy_ConfigKFree8814A(): kfree_offset = %d\n",
		       cali_info->kfree_offset[e_rf_path]);
	} else {
		RF_DBG(dm, DBG_RF_MP,
		       "phy_ConfigKFree8814A(): kfree_offset = %d\n",
		       cali_info->kfree_offset[e_rf_path]);
	}
}

void phydm_get_thermal_trim_offset_8821c(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_therm = 0xff;

	odm_efuse_one_byte_read(dm, PPG_THERMAL_OFFSET_21C, &pg_therm, false);

	if (pg_therm != 0xff) {
		pg_therm = pg_therm & 0x1f;
		if ((pg_therm & BIT(0)) == 0)
			power_trim_info->thermal = (-1 * (pg_therm >> 1));
		else
			power_trim_info->thermal = (pg_therm >> 1);

		power_trim_info->flag |= KFREE_FLAG_THERMAL_K_ON;
	}

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8821c thermal trim flag:0x%02x\n",
	       power_trim_info->flag);

	if (power_trim_info->flag & KFREE_FLAG_THERMAL_K_ON)
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8821c thermal:%d\n",
		       power_trim_info->thermal);
}

void phydm_get_power_trim_offset_8821c(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_power = 0xff, i;

	odm_efuse_one_byte_read(dm, PPG_2G_TXAB_21C, &pg_power, false);

	if (pg_power != 0xff) {
		power_trim_info->bb_gain[0][0] = pg_power;
		odm_efuse_one_byte_read(dm, PPG_5GL1_TXA_21C, &pg_power, false);
		power_trim_info->bb_gain[1][0] = pg_power;
		odm_efuse_one_byte_read(dm, PPG_5GL2_TXA_21C, &pg_power, false);
		power_trim_info->bb_gain[2][0] = pg_power;
		odm_efuse_one_byte_read(dm, PPG_5GM1_TXA_21C, &pg_power, false);
		power_trim_info->bb_gain[3][0] = pg_power;
		odm_efuse_one_byte_read(dm, PPG_5GM2_TXA_21C, &pg_power, false);
		power_trim_info->bb_gain[4][0] = pg_power;
		odm_efuse_one_byte_read(dm, PPG_5GH1_TXA_21C, &pg_power, false);
		power_trim_info->bb_gain[5][0] = pg_power;
		power_trim_info->flag =
			power_trim_info->flag | KFREE_FLAG_ON |
			KFREE_FLAG_ON_2G | KFREE_FLAG_ON_5G;
	}

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8821c power trim flag:0x%02x\n",
	       power_trim_info->flag);

	if (power_trim_info->flag & KFREE_FLAG_ON) {
		for (i = 0; i < KFREE_BAND_NUM; i++)
			RF_DBG(dm, DBG_RF_MP,
			       "[kfree] 8821c pwr_trim->bb_gain[%d][0]=0x%X\n",
			       i, power_trim_info->bb_gain[i][0]);
	}
}

void phydm_set_kfree_to_rf_8821c(void *dm_void, u8 e_rf_path, boolean wlg_btg,
				 u8 data)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 wlg, btg;
	u32 gain_bmask = (BIT(18) | BIT(17) | BIT(16) | BIT(15) | BIT(14));
	u32 s_gain_bmask = (BIT(19) | BIT(18) | BIT(17) |
			    BIT(16) | BIT(15) | BIT(14));

	odm_set_rf_reg(dm, e_rf_path, RF_0xde, BIT(0), 1);
	odm_set_rf_reg(dm, e_rf_path, RF_0xde, BIT(5), 1);
	odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(6), 1);
	odm_set_rf_reg(dm, e_rf_path, RF_0x65, BIT(6), 1);

	if (wlg_btg) {
		wlg = data & 0xf;
		btg = (data & 0xf0) >> 4;

		odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(19), (wlg & BIT(0)));
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, gain_bmask, (wlg >> 1));

		odm_set_rf_reg(dm, e_rf_path, RF_0x65, BIT(19), (btg & BIT(0)));
		odm_set_rf_reg(dm, e_rf_path, RF_0x65, gain_bmask, (btg >> 1));
	} else {
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(19), data & BIT(0));
		odm_set_rf_reg(dm, e_rf_path, RF_0x55, gain_bmask,
			       ((data & 0x1f) >> 1));
	}

	RF_DBG(dm, DBG_RF_MP,
	       "[kfree] 8821c 0x55[19:14]=0x%X 0x65[19:14]=0x%X\n",
	       odm_get_rf_reg(dm, e_rf_path, RF_0x55, s_gain_bmask),
	       odm_get_rf_reg(dm, e_rf_path, RF_0x65, s_gain_bmask));
}

void phydm_clear_kfree_to_rf_8821c(void *dm_void, u8 e_rf_path, u8 data)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 gain_bmask = (BIT(18) | BIT(17) | BIT(16) | BIT(15) | BIT(14));
	u32 s_gain_bmask = (BIT(19) | BIT(18) | BIT(17) |
			    BIT(16) | BIT(15) | BIT(14));

	odm_set_rf_reg(dm, e_rf_path, RF_0xde, BIT(0), 1);
	odm_set_rf_reg(dm, e_rf_path, RF_0xde, BIT(5), 1);
	odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(6), 1);
	odm_set_rf_reg(dm, e_rf_path, RF_0x65, BIT(6), 1);

	odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(19), (data & BIT(0)));
	odm_set_rf_reg(dm, e_rf_path, RF_0x55, gain_bmask, (data >> 1));

	odm_set_rf_reg(dm, e_rf_path, RF_0x65, BIT(19), (data & BIT(0)));
	odm_set_rf_reg(dm, e_rf_path, RF_0x65, gain_bmask, (data >> 1));

	odm_set_rf_reg(dm, e_rf_path, RF_0xde, BIT(0), 0);
	odm_set_rf_reg(dm, e_rf_path, RF_0xde, BIT(5), 0);
	odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(6), 0);
	odm_set_rf_reg(dm, e_rf_path, RF_0x65, BIT(6), 0);

	RF_DBG(dm, DBG_RF_MP,
	       "[kfree] 8821c 0x55[19:14]=0x%X 0x65[19:14]=0x%X\n",
	       odm_get_rf_reg(dm, e_rf_path, RF_0x55, s_gain_bmask),
	       odm_get_rf_reg(dm, e_rf_path, RF_0x65, s_gain_bmask));
}

void phydm_get_thermal_trim_offset_8822b(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_therm = 0xff;

	odm_efuse_one_byte_read(dm, PPG_THERMAL_OFFSET_22B, &pg_therm, false);

	if (pg_therm != 0xff) {
		pg_therm = pg_therm & 0x1f;
		if ((pg_therm & BIT(0)) == 0)
			power_trim_info->thermal = (-1 * (pg_therm >> 1));
		else
			power_trim_info->thermal = (pg_therm >> 1);

		power_trim_info->flag |= KFREE_FLAG_THERMAL_K_ON;
	}

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8822b thermal trim flag:0x%02x\n",
	       power_trim_info->flag);

	if (power_trim_info->flag & KFREE_FLAG_THERMAL_K_ON)
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8822b thermal:%d\n",
		       power_trim_info->thermal);
}

void phydm_get_power_trim_offset_8822b(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_power = 0xff, i, j;

	odm_efuse_one_byte_read(dm, PPG_2G_TXAB_22B, &pg_power, false);

	if (pg_power != 0xff) {
		/*Path A*/
		odm_efuse_one_byte_read(dm, PPG_2G_TXAB_22B, &pg_power, false);
		power_trim_info->bb_gain[0][0] = (pg_power & 0xf);

		/*Path B*/
		odm_efuse_one_byte_read(dm, PPG_2G_TXAB_22B, &pg_power, false);
		power_trim_info->bb_gain[0][1] = ((pg_power & 0xf0) >> 4);

		power_trim_info->flag |= KFREE_FLAG_ON_2G;
		power_trim_info->flag |= KFREE_FLAG_ON;
	}

	odm_efuse_one_byte_read(dm, PPG_5GL1_TXA_22B, &pg_power, false);

	if (pg_power != 0xff) {
		/*Path A*/
		odm_efuse_one_byte_read(dm, PPG_5GL1_TXA_22B, &pg_power, false);
		power_trim_info->bb_gain[1][0] = pg_power;
		odm_efuse_one_byte_read(dm, PPG_5GL2_TXA_22B, &pg_power, false);
		power_trim_info->bb_gain[2][0] = pg_power;
		odm_efuse_one_byte_read(dm, PPG_5GM1_TXA_22B, &pg_power, false);
		power_trim_info->bb_gain[3][0] = pg_power;
		odm_efuse_one_byte_read(dm, PPG_5GM2_TXA_22B, &pg_power, false);
		power_trim_info->bb_gain[4][0] = pg_power;
		odm_efuse_one_byte_read(dm, PPG_5GH1_TXA_22B, &pg_power, false);
		power_trim_info->bb_gain[5][0] = pg_power;

		/*Path B*/
		odm_efuse_one_byte_read(dm, PPG_5GL1_TXB_22B, &pg_power, false);
		power_trim_info->bb_gain[1][1] = pg_power;
		odm_efuse_one_byte_read(dm, PPG_5GL2_TXB_22B, &pg_power, false);
		power_trim_info->bb_gain[2][1] = pg_power;
		odm_efuse_one_byte_read(dm, PPG_5GM1_TXB_22B, &pg_power, false);
		power_trim_info->bb_gain[3][1] = pg_power;
		odm_efuse_one_byte_read(dm, PPG_5GM2_TXB_22B, &pg_power, false);
		power_trim_info->bb_gain[4][1] = pg_power;
		odm_efuse_one_byte_read(dm, PPG_5GH1_TXB_22B, &pg_power, false);
		power_trim_info->bb_gain[5][1] = pg_power;

		power_trim_info->flag |= KFREE_FLAG_ON_5G;
		power_trim_info->flag |= KFREE_FLAG_ON;
	}

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8822b power trim flag:0x%02x\n",
	       power_trim_info->flag);

	if (!(power_trim_info->flag & KFREE_FLAG_ON))
		return;

	for (i = 0; i < KFREE_BAND_NUM; i++) {
		for (j = 0; j < 2; j++)
			RF_DBG(dm, DBG_RF_MP,
			       "[kfree] 8822b PwrTrim->bb_gain[%d][%d]=0x%X\n",
			       i, j, power_trim_info->bb_gain[i][j]);
	}
}

void phydm_set_pa_bias_to_rf_8822b(void *dm_void, u8 e_rf_path, s8 tx_pa_bias)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 rf_reg_51 = 0, rf_reg_52 = 0, rf_reg_3f = 0;
	u32 tx_pa_bias_bmask = (BIT(12) | BIT(11) | BIT(10) | BIT(9));

	rf_reg_51 = odm_get_rf_reg(dm, e_rf_path, RF_0x51, RFREGOFFSETMASK);
	rf_reg_52 = odm_get_rf_reg(dm, e_rf_path, RF_0x52, RFREGOFFSETMASK);

	RF_DBG(dm, DBG_RF_MP,
	       "[kfree] 8822b 2g rf(0x51)=0x%X rf(0x52)=0x%X path=%d\n",
	       rf_reg_51, rf_reg_52, e_rf_path);

#if 0
	/*rf3f => rf52[19:17] = rf3f[2:0] rf52[16:15] = rf3f[4:3] rf52[3:0] = rf3f[8:5]*/
	/*rf3f => rf51[6:3] = rf3f[12:9] rf52[13] = rf3f[13]*/
#endif
	rf_reg_3f = ((rf_reg_52 & 0xe0000) >> 17) |
		    (((rf_reg_52 & 0x18000) >> 15) << 3) |
		    ((rf_reg_52 & 0xf) << 5) |
		    (((rf_reg_51 & 0x78) >> 3) << 9) |
		    (((rf_reg_52 & 0x2000) >> 13) << 13);

	RF_DBG(dm, DBG_RF_MP,
	       "[kfree] 8822b 2g original pa_bias=%d rf_reg_3f=0x%X path=%d\n",
	       tx_pa_bias, rf_reg_3f, e_rf_path);

	tx_pa_bias = (s8)((rf_reg_3f & tx_pa_bias_bmask) >> 9) + tx_pa_bias;

	if (tx_pa_bias < 0)
		tx_pa_bias = 0;
	else if (tx_pa_bias > 7)
		tx_pa_bias = 7;

	rf_reg_3f = ((rf_reg_3f & 0xfe1ff) | (tx_pa_bias << 9));

	RF_DBG(dm, DBG_RF_MP,
	       "[kfree] 8822b 2g 0x%X 0x%X pa_bias=%d rfreg_3f=0x%X path=%d\n",
	       PPG_PABIAS_2GA_22B, PPG_PABIAS_2GB_22B,
	       tx_pa_bias, rf_reg_3f, e_rf_path);

	odm_set_rf_reg(dm, e_rf_path, RF_0xef, BIT(10), 0x1);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x0);
	odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK, rf_reg_3f);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, BIT(0), 0x1);
	odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK, rf_reg_3f);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, BIT(1), 0x1);
	odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK, rf_reg_3f);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, (BIT(1) | BIT(0)), 0x3);
	odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK, rf_reg_3f);
	odm_set_rf_reg(dm, e_rf_path, RF_0xef, BIT(10), 0x0);

	RF_DBG(dm, DBG_RF_MP,
	       "[kfree] 8822b 2g tx pa bias rf_0x3f(0x%X) path=%d\n",
	       odm_get_rf_reg(dm, e_rf_path, RF_0x3f,
			      (BIT(12) | BIT(11) | BIT(10) | BIT(9))),
			      e_rf_path);
}

void phydm_get_pa_bias_offset_8822b(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_pa_bias = 0xff, e_rf_path = 0;
	s8 tx_pa_bias[2] = {0};

	odm_efuse_one_byte_read(dm, PPG_PABIAS_2GA_22B, &pg_pa_bias, false);

	if (pg_pa_bias != 0xff) {
		/*paht a*/
		odm_efuse_one_byte_read(dm, PPG_PABIAS_2GA_22B,
					&pg_pa_bias, false);
		pg_pa_bias = pg_pa_bias & 0xf;

		if ((pg_pa_bias & BIT(0)) == 0)
			tx_pa_bias[0] = (-1 * (pg_pa_bias >> 1));
		else
			tx_pa_bias[0] = (pg_pa_bias >> 1);

		/*paht b*/
		odm_efuse_one_byte_read(dm, PPG_PABIAS_2GB_22B,
					&pg_pa_bias, false);
		pg_pa_bias = pg_pa_bias & 0xf;

		if ((pg_pa_bias & BIT(0)) == 0)
			tx_pa_bias[1] = (-1 * (pg_pa_bias >> 1));
		else
			tx_pa_bias[1] = (pg_pa_bias >> 1);

		RF_DBG(dm, DBG_RF_MP,
		       "[kfree] 8822b 2g PathA_pa_bias:%d PathB_pa_bias:%d\n",
		       tx_pa_bias[0], tx_pa_bias[1]);

		for (e_rf_path = RF_PATH_A; e_rf_path < 2; e_rf_path++)
			phydm_set_pa_bias_to_rf_8822b(dm, e_rf_path,
						      tx_pa_bias[e_rf_path]);

		power_trim_info->pa_bias_flag |= PA_BIAS_FLAG_ON;
	} else {
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8822b 2g tx pa bias no pg\n");
	}
}

void phydm_set_kfree_to_rf_8822b(void *dm_void, u8 e_rf_path, u8 data)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 gain_bmask = (BIT(18) | BIT(17) | BIT(16) | BIT(15) | BIT(14));

	odm_set_rf_reg(dm, e_rf_path, RF_0xde, BIT(0), 1);
	odm_set_rf_reg(dm, e_rf_path, RF_0xde, BIT(4), 1);
	odm_set_rf_reg(dm, e_rf_path, RF_0x65, MASKLWORD, 0x9000);
	odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(5), 1);

	odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(19), (data & BIT(0)));
	odm_set_rf_reg(dm, e_rf_path, RF_0x55, gain_bmask,
		       ((data & 0x1f) >> 1));

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8822b 0x55[19:14]=0x%X path=%d\n",
	       odm_get_rf_reg(dm, e_rf_path, RF_0x55,
			      (BIT(19) | BIT(18) | BIT(17) | BIT(16) |
			      BIT(15) | BIT(14))), e_rf_path);
}

void phydm_clear_kfree_to_rf_8822b(void *dm_void, u8 e_rf_path, u8 data)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 gain_bmask = (BIT(18) | BIT(17) | BIT(16) | BIT(15) | BIT(14));

	odm_set_rf_reg(dm, e_rf_path, RF_0xde, BIT(0), 1);
	odm_set_rf_reg(dm, e_rf_path, RF_0xde, BIT(4), 1);
	odm_set_rf_reg(dm, e_rf_path, RF_0x65, MASKLWORD, 0x9000);
	odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(5), 1);

	odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(19), (data & BIT(0)));
	odm_set_rf_reg(dm, e_rf_path, RF_0x55, gain_bmask,
		       ((data & 0x1f) >> 1));

	odm_set_rf_reg(dm, e_rf_path, RF_0xde, BIT(0), 0);
	odm_set_rf_reg(dm, e_rf_path, RF_0xde, BIT(4), 1);
	odm_set_rf_reg(dm, e_rf_path, RF_0x65, MASKLWORD, 0x9000);
	odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(5), 0);
	odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(7), 0);

	RF_DBG(dm, DBG_RF_MP,
	       "[kfree] 8822b clear power trim 0x55[19:14]=0x%X path=%d\n",
	       odm_get_rf_reg(dm, e_rf_path, RF_0x55,
			      (BIT(19) | BIT(18) | BIT(17) | BIT(16) |
			      BIT(15) | BIT(14))), e_rf_path);
}

void phydm_get_thermal_trim_offset_8710b(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_therm = 0xff;

	odm_efuse_one_byte_read(dm, 0x0EF, &pg_therm, false);

	if (pg_therm != 0xff) {
		pg_therm = pg_therm & 0x1f;
		if ((pg_therm & BIT(0)) == 0)
			power_trim_info->thermal = (-1 * (pg_therm >> 1));
		else
			power_trim_info->thermal = (pg_therm >> 1);

		power_trim_info->flag |= KFREE_FLAG_THERMAL_K_ON;
	}

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8710b thermal trim flag:0x%02x\n",
	       power_trim_info->flag);

	if (power_trim_info->flag & KFREE_FLAG_THERMAL_K_ON)
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8710b thermal:%d\n",
		       power_trim_info->thermal);
}

void phydm_get_power_trim_offset_8710b(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_power = 0xff;

	odm_efuse_one_byte_read(dm, 0xEE, &pg_power, false);

	if (pg_power != 0xff) {
		/*Path A*/
		odm_efuse_one_byte_read(dm, 0xEE, &pg_power, false);
		power_trim_info->bb_gain[0][0] = (pg_power & 0xf);

		power_trim_info->flag |= KFREE_FLAG_ON_2G;
		power_trim_info->flag |= KFREE_FLAG_ON;
	}

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8710b power trim flag:0x%02x\n",
	       power_trim_info->flag);

	if (power_trim_info->flag & KFREE_FLAG_ON)
		RF_DBG(dm, DBG_RF_MP,
		       "[kfree] 8710b power_trim_data->bb_gain[0][0]=0x%X\n",
		       power_trim_info->bb_gain[0][0]);
}

void phydm_set_kfree_to_rf_8710b(void *dm_void, u8 e_rf_path, u8 data)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 gain_bmask = (BIT(18) | BIT(17) | BIT(16) | BIT(15));

	odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(19), (data & BIT(0)));
	odm_set_rf_reg(dm, e_rf_path, RF_0x55, gain_bmask, ((data & 0xf) >> 1));

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8710b 0x55[19:14]=0x%X path=%d\n",
	       odm_get_rf_reg(dm, e_rf_path, RF_0x55,
			      (BIT(19) | BIT(18) | BIT(17) | BIT(16) |
			      BIT(15) | BIT(14))), e_rf_path);
}

void phydm_clear_kfree_to_rf_8710b(void *dm_void, u8 e_rf_path, u8 data)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 gain_bmask = (BIT(18) | BIT(17) | BIT(16) | BIT(15) | BIT(14));

	odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(19), (data & BIT(0)));
	odm_set_rf_reg(dm, e_rf_path, RF_0x55, gain_bmask,
		       ((data & 0x1f) >> 1));

	RF_DBG(dm, DBG_RF_MP,
	       "[kfree] 8710b clear power trim 0x55[19:14]=0x%X path=%d\n",
	       odm_get_rf_reg(dm, e_rf_path, RF_0x55,
			      (BIT(19) | BIT(18) | BIT(17) | BIT(16) |
			      BIT(15) | BIT(14))), e_rf_path);
}

void phydm_get_thermal_trim_offset_8192f(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_therm = 0xff;

	odm_efuse_one_byte_read(dm, 0x1EF, &pg_therm, false);

	if (pg_therm != 0xff) {
		pg_therm = pg_therm & 0x1f;
		if ((pg_therm & BIT(0)) == 0)
			power_trim_info->thermal = (-1 * (pg_therm >> 1));
		else
			power_trim_info->thermal = (pg_therm >> 1);

		power_trim_info->flag |= KFREE_FLAG_THERMAL_K_ON;
	}

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8192f thermal trim flag:0x%02x\n",
	       power_trim_info->flag);

	if (power_trim_info->flag & KFREE_FLAG_THERMAL_K_ON)
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8192f thermal:%d\n",
		       power_trim_info->thermal);
}

void phydm_get_power_trim_offset_8192f(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_power1 = 0xff, pg_power2 = 0xff, pg_power3 = 0xff, i, j;

	odm_efuse_one_byte_read(dm, 0x1EE, &pg_power1, false); /*CH4-9*/

	if (pg_power1 != 0xff) {
		/*Path A*/
		odm_efuse_one_byte_read(dm, 0x1EE, &pg_power1, false);
		power_trim_info->bb_gain[1][0] = (pg_power1 & 0xf);
		/*Path B*/
		odm_efuse_one_byte_read(dm, 0x1EE, &pg_power1, false);
		power_trim_info->bb_gain[1][1] = ((pg_power1 & 0xf0) >> 4);

		power_trim_info->flag |= KFREE_FLAG_ON_2G;
		power_trim_info->flag |= KFREE_FLAG_ON;
	}

	odm_efuse_one_byte_read(dm, 0x1EC, &pg_power2, false); /*CH1-3*/

	if (pg_power2 != 0xff) {
		/*Path A*/
		odm_efuse_one_byte_read(dm, 0x1EC, &pg_power2, false);
		power_trim_info->bb_gain[0][0] = (pg_power2 & 0xf);
		/*Path B*/
		odm_efuse_one_byte_read(dm, 0x1EC, &pg_power2, false);
		power_trim_info->bb_gain[0][1] = ((pg_power2 & 0xf0) >> 4);

		power_trim_info->flag |= KFREE_FLAG_ON_2G;
		power_trim_info->flag |= KFREE_FLAG_ON;
	} else {
		power_trim_info->bb_gain[0][0] = (pg_power1 & 0xf);
		power_trim_info->bb_gain[0][1] = ((pg_power1 & 0xf0) >> 4);
	}

	odm_efuse_one_byte_read(dm, 0x1EA, &pg_power3, false); /*CH10-14*/

	if (pg_power3 != 0xff) {
		/*Path A*/
		odm_efuse_one_byte_read(dm, 0x1EA, &pg_power3, false);
		power_trim_info->bb_gain[2][0] = (pg_power3 & 0xf);
		/*Path B*/
		odm_efuse_one_byte_read(dm, 0x1EA, &pg_power3, false);
		power_trim_info->bb_gain[2][1] = ((pg_power3 & 0xf0) >> 4);

		power_trim_info->flag |= KFREE_FLAG_ON_2G;
		power_trim_info->flag |= KFREE_FLAG_ON;
	} else {
		power_trim_info->bb_gain[2][0] = (pg_power1 & 0xf);
		power_trim_info->bb_gain[2][1] = ((pg_power1 & 0xf0) >> 4);
	}

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8192F power trim flag:0x%02x\n",
	       power_trim_info->flag);

	if (!(power_trim_info->flag & KFREE_FLAG_ON))
		return;

	for (i = 0; i < KFREE_CH_NUM; i++) {
		for (j = 0; j < 2; j++)
			RF_DBG(dm, DBG_RF_MP,
			       "[kfree] 8192F PwrTrim->bb_gain[%d][%d]=0x%X\n",
			       i, j, power_trim_info->bb_gain[i][j]);
	}
}

void phydm_set_kfree_to_rf_8192f(void *dm_void, u8 e_rf_path, u8 channel_idx,
				 u8 data)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	/*power_trim based on 55[19:14]*/
	odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(5), 1);
	/*enable 55[14] for 0.5db step*/
	odm_set_rf_reg(dm, e_rf_path, RF_0xf5, BIT(18), 1);
	/*enter power_trim debug mode*/
	odm_set_rf_reg(dm, e_rf_path, RF_0xdf, BIT(7), 1);
	/*write enable*/
	odm_set_rf_reg(dm, e_rf_path, RF_0xef, BIT(7), 1);

	if (e_rf_path == 0) {
		if (channel_idx == 0) {
			odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x70000, 0);
			odm_set_rf_reg(dm, e_rf_path, 0x33, 0x3F, data);

			odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x70000, 1);
			odm_set_rf_reg(dm, e_rf_path, 0x33, 0x3F, data);

		} else if (channel_idx == 1) {
			odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x70000, 2);
			odm_set_rf_reg(dm, e_rf_path, 0x33, 0x3F, data);

			odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x70000, 3);
			odm_set_rf_reg(dm, e_rf_path, 0x33, 0x3F, data);
		} else if (channel_idx == 2) {
			odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x70000, 4);
			odm_set_rf_reg(dm, e_rf_path, 0x33, 0x3F, data);

			odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x70000, 5);
			odm_set_rf_reg(dm, e_rf_path, 0x33, 0x3F, data);
		}
	} else if (e_rf_path == 1) {
		if (channel_idx == 0) {
			odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x70000, 0);
			odm_set_rf_reg(dm, e_rf_path, 0x33, 0x3F, data);

			odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x70000, 1);
			odm_set_rf_reg(dm, e_rf_path, 0x33, 0x3F, data);
		} else if (channel_idx == 1) {
			odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x70000, 2);
			odm_set_rf_reg(dm, e_rf_path, 0x33, 0x3F, data);

			odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x70000, 3);
			odm_set_rf_reg(dm, e_rf_path, 0x33, 0x3F, data);
		} else if (channel_idx == 2) {
			odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x70000, 4);
			odm_set_rf_reg(dm, e_rf_path, 0x33, 0x3F, data);

			odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x70000, 5);
			odm_set_rf_reg(dm, e_rf_path, 0x33, 0x3F, data);
		}
	}

	/*leave power_trim debug mode*/
	odm_set_rf_reg(dm, e_rf_path, RF_0xdf, BIT(7), 0);
	/*write disable*/
	odm_set_rf_reg(dm, e_rf_path, RF_0xef, BIT(7), 0);

	RF_DBG(dm, DBG_RF_MP,
	       "[kfree] 8192F 0x55[19:14]=0x%X path=%d channel=%d\n",
	       odm_get_rf_reg(dm, e_rf_path, RF_0x55,
			      (BIT(19) | BIT(18) | BIT(17) | BIT(16) |
			      BIT(15) | BIT(14))), e_rf_path, channel_idx);
}

#if 0
/*
void phydm_clear_kfree_to_rf_8192f(void *dm_void, u8 e_rf_path, u8 data)
{
	struct dm_struct		*dm = (struct dm_struct *)dm_void;
	struct dm_rf_calibration_struct	*cali_info = &dm->rf_calibrate_info;

	odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(19), (data & BIT(0)));
	odm_set_rf_reg(dm, e_rf_path, RF_0x55, (BIT(18) | BIT(17) | BIT(16) | BIT(15) | BIT(14)), ((data & 0x1f) >> 1));

	RF_DBG(dm, DBG_RF_MP,
		"[kfree] 8192F clear power trim 0x55[19:14]=0x%X path=%d\n",
		odm_get_rf_reg(dm, e_rf_path, RF_0x55, (BIT(19) | BIT(18) | BIT(17) | BIT(16) | BIT(15) | BIT(14))),
		e_rf_path
		);
}
*/
#endif

void phydm_get_thermal_trim_offset_8198f(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_therm = 0;

	odm_efuse_one_byte_read(dm, PPG_THERMAL_OFFSET_98F, &pg_therm, false);

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8198f efuse thermal trim 0x%X=0x%X\n",
		PPG_THERMAL_OFFSET_98F, pg_therm);

	if (pg_therm != 0) {
		pg_therm = pg_therm & 0x1f;
		if ((pg_therm & BIT(0)) == 0)
			power_trim_info->thermal = (-1 * (pg_therm >> 1));
		else
			power_trim_info->thermal = (pg_therm >> 1);

		power_trim_info->flag |= KFREE_FLAG_THERMAL_K_ON;
	}

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8198f thermal trim flag:0x%02x\n",
	       power_trim_info->flag);

	if (power_trim_info->flag & KFREE_FLAG_THERMAL_K_ON)
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8198f thermal:%d\n",
		       power_trim_info->thermal);
}

void phydm_get_power_trim_offset_8198f(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 i, j;
	u8 power_trim[6] = {0};

	odm_efuse_one_byte_read(dm, PPG_2GL_TXAB_98F, &power_trim[0], false);
	odm_efuse_one_byte_read(dm, PPG_2GL_TXCD_98F, &power_trim[1], false);
	odm_efuse_one_byte_read(dm, PPG_2GM_TXAB_98F, &power_trim[2], false);
	odm_efuse_one_byte_read(dm, PPG_2GM_TXCD_98F, &power_trim[3], false);
	odm_efuse_one_byte_read(dm, PPG_2GH_TXAB_98F, &power_trim[4], false);
	odm_efuse_one_byte_read(dm, PPG_2GH_TXCD_98F, &power_trim[5], false);

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8198f efuse Power Trim 0x%X=0x%X 0x%X=0x%X 0x%X=0x%X 0x%X=0x%X 0x%X=0x%X 0x%X=0x%X\n",
		PPG_2GL_TXAB_98F, power_trim[0],
		PPG_2GL_TXCD_98F, power_trim[1],
		PPG_2GM_TXAB_98F, power_trim[2],
		PPG_2GM_TXCD_98F, power_trim[3],
		PPG_2GH_TXAB_98F, power_trim[4],
		PPG_2GH_TXCD_98F, power_trim[5]
		);

	j = 0;
	for (i = 0; i < 6; i++) {
		if (power_trim[i] == 0x0)
			j++;
	}

	if (j == 6) {
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8198f Power Trim no pg\n");
	} else {
		power_trim_info->bb_gain[0][0] = power_trim[0] & 0xf;
		power_trim_info->bb_gain[0][1] = (power_trim[0] & 0xf0) >> 4;

		power_trim_info->bb_gain[0][2] = power_trim[1] & 0xf;
		power_trim_info->bb_gain[0][3] = (power_trim[1] & 0xf0) >> 4;

		power_trim_info->bb_gain[1][0] = power_trim[2] & 0xf;
		power_trim_info->bb_gain[1][1] = (power_trim[2] & 0xf0) >> 4;

		power_trim_info->bb_gain[1][2] = power_trim[3] & 0xf;
		power_trim_info->bb_gain[1][3] = (power_trim[3] & 0xf0) >> 4;

		power_trim_info->bb_gain[2][0] = power_trim[4] & 0xf;
		power_trim_info->bb_gain[2][1] = (power_trim[4] & 0xf0) >> 4;

		power_trim_info->bb_gain[2][2] = power_trim[5] & 0xf;
		power_trim_info->bb_gain[2][3] = (power_trim[5] & 0xf0) >> 4;

		power_trim_info->flag =
			power_trim_info->flag | KFREE_FLAG_ON | KFREE_FLAG_ON_2G;
	}

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8198f power trim flag:0x%02x\n",
	       power_trim_info->flag);

	if (power_trim_info->flag & KFREE_FLAG_ON) {
		for (i = 0; i < KFREE_BAND_NUM; i++) {
			for (j = 0; j < MAX_RF_PATH; j++) {
				RF_DBG(dm, DBG_RF_MP,
				       "[kfree] 8198f pwr_trim->bb_gain[%d][%d]=0x%X\n",
				       i, j, power_trim_info->bb_gain[i][j]);
			}
		}
	}
}

void phydm_get_pa_bias_offset_8198f(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 i, j;
	u8 pa_bias[2] = {0};
	u8 tx_pa_bias[4] = {0};

	odm_efuse_one_byte_read(dm, PPG_PABIAS_2GAB_98F, &pa_bias[0], false);
	odm_efuse_one_byte_read(dm, PPG_PABIAS_2GCD_98F, &pa_bias[1], false);

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8198f efuse Tx PA Bias 0x%X=0x%X 0x%X=0x%X\n",
		PPG_PABIAS_2GAB_98F, pa_bias[0], PPG_PABIAS_2GCD_98F, pa_bias[1]);

	j = 0;
	for (i = 0; i < 2; i++) {
		if (pa_bias[i] == 0x0)
			j++;
	}

	if (j == 2) {
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8198f Tx PA Bias no pg\n");
	} else {
		/*paht ab*/
		tx_pa_bias[0] = pa_bias[0] & 0xf;
		tx_pa_bias[1] = ((pa_bias[0] & 0xf0) >> 4);

		/*paht cd*/
		tx_pa_bias[2] = pa_bias[1] & 0xf;
		tx_pa_bias[3] = ((pa_bias[1] & 0xf0) >> 4);

		for (i = RF_PATH_A; i < 4; i++) {
			if ((tx_pa_bias[i] & 0x1) == 1)
				tx_pa_bias[i] = tx_pa_bias[i] & 0xe;
			else
				tx_pa_bias[i] = tx_pa_bias[i] | 0x1;
		}

		RF_DBG(dm, DBG_RF_MP,
		       "[kfree] 8198f PathA_pa_bias:0x%x PathB_pa_bias:0x%x\n",
		       tx_pa_bias[0], tx_pa_bias[1]);

		RF_DBG(dm, DBG_RF_MP,
		       "[kfree] 8198f PathC_pa_bias:0x%x PathD_pa_bias:0x%x\n",
		       tx_pa_bias[2], tx_pa_bias[3]);

		for (i = RF_PATH_A; i < 4; i++)
			odm_set_rf_reg(dm, i, 0x60, 0x0000f000, tx_pa_bias[i]);

		power_trim_info->pa_bias_flag |= PA_BIAS_FLAG_ON;
	}
}

void phydm_get_set_lna_offset_8198f(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 i, j;
	u8 lna_trim[4] = {0};
	u8 cg[4] = {0}, cs[4] = {0};
	u32 rf_reg;

	odm_efuse_one_byte_read(dm, PPG_LNA_2GA_98F, &lna_trim[0], false);
	odm_efuse_one_byte_read(dm, PPG_LNA_2GB_98F, &lna_trim[1], false);
	odm_efuse_one_byte_read(dm, PPG_LNA_2GC_98F, &lna_trim[2], false);
	odm_efuse_one_byte_read(dm, PPG_LNA_2GD_98F, &lna_trim[3], false);

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8198f efuse LNA Trim 0x%X=0x%X 0x%X=0x%X 0x%X=0x%X 0x%X=0x%X\n",
		PPG_LNA_2GA_98F, lna_trim[0],
		PPG_LNA_2GB_98F, lna_trim[1],
		PPG_LNA_2GC_98F, lna_trim[2],
		PPG_LNA_2GD_98F, lna_trim[3]
		);

	j = 0;
	for (i = 0; i < 4; i++) {
		if (lna_trim[i] == 0x0)
			j++;
	}

	if (j == 4) {
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8198f LNA no pg\n");
	} else {

		for (i = 0; i < 4; i++) {
			cg[i] = (lna_trim[i] & 0xc) >> 2;
			cs[i] = lna_trim[i] & 0x3;
		}

		for (i = RF_PATH_A; i <= RF_PATH_D; i++) {
			RF_DBG(dm, DBG_RF_MP,
				"[kfree] 8198f lna cg[%d]=0x%x cs[%d]=0x%x\n",
				i, cg[i], i, cs[i]);
			odm_set_rf_reg(dm, i, 0xdf, RFREGOFFSETMASK, 0x2);

			if (cg[i] == 0x3) {
				rf_reg = odm_get_rf_reg(dm, i, 0x86, (BIT(19) | BIT(18)));
				rf_reg = rf_reg + 1;
				if (rf_reg >= 0x3)
					rf_reg = 0x3;
				odm_set_rf_reg(dm, i, 0x86, (BIT(19) | BIT(18)), rf_reg);
				RF_DBG(dm, DBG_RF_MP,
					"[kfree] 8198f lna CG set rf 0x86 [19:18]=0x%x path=%d\n", rf_reg, i);
			}
			if (cs[i] == 0x3) {
				rf_reg = odm_get_rf_reg(dm, i, 0x86, (BIT(17) | BIT(16)));
				rf_reg = rf_reg + 1;
				if (rf_reg >= 0x3)
					rf_reg = 0x3;
				odm_set_rf_reg(dm, i, 0x86, (BIT(17) | BIT(16)), rf_reg);
				RF_DBG(dm, DBG_RF_MP,
					"[kfree] 8198f lna CS set rf 0x86 [17:16]=0x%x path=%d\n", rf_reg, i);
			}	
		}

		power_trim_info->lna_flag |= LNA_FLAG_ON;
	}
}


void phydm_set_kfree_to_rf_8198f(void *dm_void, u8 e_rf_path, u8 data)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;
	u32 i;
	s8 pwr_offset[3];

	RF_DBG(dm, DBG_RF_MP,
		   "[kfree] %s:Set kfree to rf 0x33\n", __func__);

	/*power_trim based on 55[19:14]*/
	odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(5), 1);
	/*enable 55[14] for 0.5db step*/
	odm_set_rf_reg(dm, e_rf_path, RF_0xf5, BIT(18), 1);
	/*enter power_trim debug mode*/
	odm_set_rf_reg(dm, e_rf_path, RF_0xdf, BIT(7), 0);
	/*write enable*/
	odm_set_rf_reg(dm, e_rf_path, RF_0xef, BIT(7), 1);

	for (i =0; i < 3; i++)
		pwr_offset[i] = power_trim_info->bb_gain[i][e_rf_path];

	odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x70000, 0);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x3F, pwr_offset[0]);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x70000, 1);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x3F, pwr_offset[0]);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x70000, 2);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x3F, pwr_offset[1]);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x70000, 3);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x3F, pwr_offset[1]);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x70000, 4);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x3F, pwr_offset[2]);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x70000, 5);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x3F, pwr_offset[2]);

	/*leave power_trim debug mode*/
	/*odm_set_rf_reg(dm, e_rf_path, RF_0xdf, BIT(7), 0);*/
	/*write disable*/
	odm_set_rf_reg(dm, e_rf_path, RF_0xef, BIT(7), 0);

}

void phydm_clear_kfree_to_rf_8198f(void *dm_void, u8 e_rf_path, u8 data)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
#if 0

	RF_DBG(dm, DBG_RF_MP,
		   "[kfree] %s:Clear kfree to rf 0x55\n", __func__);

	/*power_trim based on 55[19:14]*/
	odm_set_rf_reg(dm, e_rf_path, RF_0x55, BIT(5), 1);
	/*enable 55[14] for 0.5db step*/
	odm_set_rf_reg(dm, e_rf_path, RF_0xf5, BIT(18), 1);
	/*enter power_trim debug mode*/
	odm_set_rf_reg(dm, e_rf_path, RF_0xdf, BIT(7), 0);
	/*write enable*/
	odm_set_rf_reg(dm, e_rf_path, RF_0xef, BIT(7), 1);

	odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x70000, 0);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x3F, data);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x70000, 1);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x3F, data);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x70000, 2);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x3F, data);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x70000, 3);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x3F, data);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x70000, 4);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x3F, data);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x70000, 5);
	odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x3F, data);

	/*leave power_trim debug mode*/
	odm_set_rf_reg(dm, e_rf_path, RF_0xdf, BIT(7), 0);
	/*enable 55[14] for 0.5db step*/
	odm_set_rf_reg(dm, e_rf_path, RF_0xf5, BIT(18), 0);
	/*write disable*/
	odm_set_rf_reg(dm, e_rf_path, RF_0xef, BIT(7), 0);

	odm_set_rf_reg(dm, e_rf_path, RF_0xdf, BIT(7), 1);
	/*odm_set_rf_reg(dm, e_rf_path, RF_0xf5, BIT(18), 0);*/

#endif

}

void phydm_get_set_thermal_trim_offset_8822c(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_therm = 0xff, thermal[2] = {0};

	odm_efuse_one_byte_read(dm, PPG_THERMAL_A_OFFSET_22C, &pg_therm, false);

	if (pg_therm != 0xff) {
		/*s0*/
		pg_therm = pg_therm & 0x1f;

		thermal[RF_PATH_A] =
			((pg_therm & 0x1) << 3) | ((pg_therm >> 1) & 0x7);

		odm_set_rf_reg(dm, RF_PATH_A, RF_0x43, 0x000f0000, thermal[RF_PATH_A]);

		/*s1*/
		odm_efuse_one_byte_read(dm, PPG_THERMAL_B_OFFSET_22C, &pg_therm, false);

		pg_therm = pg_therm & 0x1f;

		thermal[RF_PATH_B] = ((pg_therm & 0x1) << 3) | ((pg_therm >> 1) & 0x7);

		odm_set_rf_reg(dm, RF_PATH_B, RF_0x43, 0x000f0000, thermal[RF_PATH_B]);

		power_trim_info->flag |= KFREE_FLAG_THERMAL_K_ON;

	}

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8822c thermal trim flag:0x%02x\n",
	       power_trim_info->flag);

	if (power_trim_info->flag & KFREE_FLAG_THERMAL_K_ON)
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8822c thermalA:%d thermalB:%d\n",
			thermal[RF_PATH_A],
			thermal[RF_PATH_B]);	
}

void phydm_set_power_trim_offset_8822c(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;
	u8 e_rf_path;

	for (e_rf_path = RF_PATH_A; e_rf_path < 2; e_rf_path++)
	{
		odm_set_rf_reg(dm, e_rf_path, RF_0xee, BIT(19), 1);

		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x0);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[0][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x1);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[1][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x2);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[2][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x3);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[2][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x4);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[3][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x5);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[4][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x6);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[5][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x7);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[6][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x8);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[7][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x9);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[3][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0xa);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[4][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0xb);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[5][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0xc);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[6][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0xd);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[7][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0xe);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[7][e_rf_path]);

		odm_set_rf_reg(dm, e_rf_path, RF_0xee, BIT(19), 0);
	}
}

void phydm_get_set_power_trim_offset_8822c(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_power = 0xff, i, j;
	u8 pg_power1, pg_power2 , pg_power3, pg_power4, pg_power5;

	odm_efuse_one_byte_read(dm, PPG_2GL_TXAB_22C, &pg_power1, false);
	odm_efuse_one_byte_read(dm, PPG_2GM_TXAB_22C, &pg_power2, false);
	odm_efuse_one_byte_read(dm, PPG_2GH_TXAB_22C, &pg_power3, false);
	odm_efuse_one_byte_read(dm, PPG_5GL1_TXA_22C, &pg_power4, false);
	odm_efuse_one_byte_read(dm, PPG_5GL1_TXB_22C, &pg_power5, false);

	if (pg_power1 != 0xff || pg_power2 != 0xff || pg_power3 != 0xff ||
		pg_power4 != 0xff || pg_power5 != 0xff) {
		odm_efuse_one_byte_read(dm, PPG_2GL_TXAB_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[0][0] = pg_power & 0xf;
		power_trim_info->bb_gain[0][1] = (pg_power & 0xf0) >> 4;

		odm_efuse_one_byte_read(dm, PPG_2GM_TXAB_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[1][0] = pg_power & 0xf;
		power_trim_info->bb_gain[1][1] = (pg_power & 0xf0) >> 4;

		odm_efuse_one_byte_read(dm, PPG_2GH_TXAB_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[2][0] = pg_power & 0xf;
		power_trim_info->bb_gain[2][1] = (pg_power & 0xf0) >> 4;

		odm_efuse_one_byte_read(dm, PPG_5GL1_TXA_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[3][0] = pg_power & 0x1f;
		odm_efuse_one_byte_read(dm, PPG_5GL1_TXB_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[3][1] = pg_power & 0x1f;

		odm_efuse_one_byte_read(dm, PPG_5GL2_TXA_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[4][0] = pg_power & 0x1f;
		odm_efuse_one_byte_read(dm, PPG_5GL2_TXB_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[4][1] = pg_power & 0x1f;

		odm_efuse_one_byte_read(dm, PPG_5GM1_TXA_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[5][0] = pg_power & 0x1f;
		odm_efuse_one_byte_read(dm, PPG_5GM1_TXB_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[5][1] = pg_power & 0x1f;

		odm_efuse_one_byte_read(dm, PPG_5GM2_TXA_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[6][0] = pg_power & 0x1f;
		odm_efuse_one_byte_read(dm, PPG_5GM2_TXB_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[6][1] = pg_power & 0x1f;

		odm_efuse_one_byte_read(dm, PPG_5GH1_TXA_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[7][0] = pg_power & 0x1f;
		odm_efuse_one_byte_read(dm, PPG_5GH1_TXB_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[7][1] = pg_power & 0x1f;

		power_trim_info->flag =
			power_trim_info->flag | KFREE_FLAG_ON |
						KFREE_FLAG_ON_2G |
						KFREE_FLAG_ON_5G;

		phydm_set_power_trim_offset_8822c(dm);
	}

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8822c power trim flag:0x%02x\n",
	       power_trim_info->flag);

	if (power_trim_info->flag & KFREE_FLAG_ON) {
		for (i = 0; i < KFREE_BAND_NUM; i++) {
			for (j = 0; j < 2; j++) {
				RF_DBG(dm, DBG_RF_MP,
				       "[kfree] 8822c pwr_trim->bb_gain[%d][%d]=0x%X\n",
				       i, j, power_trim_info->bb_gain[i][j]);
			}
		}
	}
}

void phydm_get_tssi_trim_offset_8822c(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 i, j;
	u8 pg_power[16] = {0};

	odm_efuse_one_byte_read(dm, TSSI_2GM_TXA_22C, &pg_power[0], false);
	odm_efuse_one_byte_read(dm, TSSI_2GM_TXB_22C, &pg_power[1], false);
	odm_efuse_one_byte_read(dm, TSSI_2GH_TXA_22C, &pg_power[2], false);
	odm_efuse_one_byte_read(dm, TSSI_2GH_TXB_22C, &pg_power[3], false);
	odm_efuse_one_byte_read(dm, TSSI_5GL1_TXA_22C, &pg_power[4], false);
	odm_efuse_one_byte_read(dm, TSSI_5GL1_TXB_22C, &pg_power[5], false);
	odm_efuse_one_byte_read(dm, TSSI_5GL2_TXA_22C, &pg_power[6], false);
	odm_efuse_one_byte_read(dm, TSSI_5GL2_TXB_22C, &pg_power[7], false);
	odm_efuse_one_byte_read(dm, TSSI_5GM1_TXA_22C, &pg_power[8], false);
	odm_efuse_one_byte_read(dm, TSSI_5GM1_TXB_22C, &pg_power[9], false);
	odm_efuse_one_byte_read(dm, TSSI_5GM2_TXA_22C, &pg_power[10], false);
	odm_efuse_one_byte_read(dm, TSSI_5GM2_TXB_22C, &pg_power[11], false);
	odm_efuse_one_byte_read(dm, TSSI_5GH1_TXA_22C, &pg_power[12], false);
	odm_efuse_one_byte_read(dm, TSSI_5GH1_TXB_22C, &pg_power[13], false);
	odm_efuse_one_byte_read(dm, TSSI_5GH2_TXA_22C, &pg_power[14], false);
	odm_efuse_one_byte_read(dm, TSSI_5GH2_TXB_22C, &pg_power[15], false);

	j = 0;
	for (i = 0; i < 16; i++) {
		if (pg_power[i] == 0xff)
			j++;
	}

	if (j == 16) {
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8822c tssi trim no PG\n");
	} else {
		power_trim_info->tssi_trim[0][0] = (s8)pg_power[0];
		power_trim_info->tssi_trim[0][1] = (s8)pg_power[1];
		power_trim_info->tssi_trim[1][0] = (s8)pg_power[0];
		power_trim_info->tssi_trim[1][1] = (s8)pg_power[1];
		power_trim_info->tssi_trim[2][0] = (s8)pg_power[2];
		power_trim_info->tssi_trim[2][1] = (s8)pg_power[3];
		power_trim_info->tssi_trim[3][0] = (s8)pg_power[4];
		power_trim_info->tssi_trim[3][1] = (s8)pg_power[5];
		power_trim_info->tssi_trim[4][0] = (s8)pg_power[6];
		power_trim_info->tssi_trim[4][1] = (s8)pg_power[7];
		power_trim_info->tssi_trim[5][0] = (s8)pg_power[8];
		power_trim_info->tssi_trim[5][1] = (s8)pg_power[9];
		power_trim_info->tssi_trim[6][0] = (s8)pg_power[10];
		power_trim_info->tssi_trim[6][1] = (s8)pg_power[11];
		power_trim_info->tssi_trim[7][0] = (s8)pg_power[12];
		power_trim_info->tssi_trim[7][1] = (s8)pg_power[13];
		power_trim_info->tssi_trim[8][0] = (s8)pg_power[14];
		power_trim_info->tssi_trim[8][1] = (s8)pg_power[15];

		power_trim_info->flag =
			power_trim_info->flag | TSSI_TRIM_FLAG_ON;

		if (power_trim_info->flag & TSSI_TRIM_FLAG_ON) {
			for (i = 0; i < KFREE_BAND_NUM; i++) {
				for (j = 0; j < 2; j++) {
					RF_DBG(dm, DBG_RF_MP,
					       "[kfree] 8822c tssi_trim[%d][%d]=0x%X\n",
					       i, j, power_trim_info->tssi_trim[i][j]);
				}
			}
		}
	}
}

s8 phydm_get_tssi_trim_de_8822c(void *dm_void, u8 path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 channel = *dm->channel, group = 0;

	if (channel >= 1 && channel <= 3)
		group = 0;
	else if (channel >= 4 && channel <= 9)
		group = 1;
	else if (channel >= 10 && channel <= 14)
		group = 2;
	else if (channel >= 36 && channel <= 50)
		group = 3;
	else if (channel >= 52 && channel <= 64)
		group = 4;
	else if (channel >= 100 && channel <= 118)
		group = 5;
	else if (channel >= 120 && channel <= 144)
		group = 6;
	else if (channel >= 149 && channel <= 165)
		group = 7;
	else if (channel >= 167 && channel <= 177)
		group = 8;
	else {
		RF_DBG(dm, DBG_RF_MP, "[kfree] Channel(%d) is not exist in Group\n",
			channel);
		return 0;
	}

	return power_trim_info->tssi_trim[group][path];
}



void phydm_get_set_pa_bias_offset_8822c(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_pa_bias = 0xff;

	RF_DBG(dm, DBG_RF_MP, "======>%s\n", __func__);

	odm_efuse_one_byte_read(dm, PPG_PABIAS_2GA_22C, &pg_pa_bias, false);

	if (pg_pa_bias != 0xff) {
		/*2G s0*/
		odm_efuse_one_byte_read(dm, PPG_PABIAS_2GA_22C,
					&pg_pa_bias, false);
		pg_pa_bias = pg_pa_bias & 0xf;

		RF_DBG(dm, DBG_RF_MP, "[kfree] 2G s0 pa_bias=0x%x\n", pg_pa_bias);

		odm_set_rf_reg(dm, RF_PATH_A, 0x60, 0x0000f000, pg_pa_bias);

		/*2G s1*/
		odm_efuse_one_byte_read(dm, PPG_PABIAS_2GB_22C,
					&pg_pa_bias, false);
		pg_pa_bias = pg_pa_bias & 0xf;

		RF_DBG(dm, DBG_RF_MP, "[kfree] 2G s1 pa_bias=0x%x\n", pg_pa_bias);

		odm_set_rf_reg(dm, RF_PATH_B, 0x60, 0x0000f000, pg_pa_bias);

		/*5G s0*/
		odm_efuse_one_byte_read(dm, PPG_PABIAS_5GA_22C,
					&pg_pa_bias, false);
		pg_pa_bias = pg_pa_bias & 0xf;

		RF_DBG(dm, DBG_RF_MP, "[kfree] 5G s0 pa_bias=0x%x\n", pg_pa_bias);

		odm_set_rf_reg(dm, RF_PATH_A, 0x60, 0x000f0000, pg_pa_bias);

		/*5G s1*/
		odm_efuse_one_byte_read(dm, PPG_PABIAS_5GB_22C,
					&pg_pa_bias, false);
		pg_pa_bias = pg_pa_bias & 0xf;

		RF_DBG(dm, DBG_RF_MP, "[kfree] 5G s1 pa_bias=0x%x\n", pg_pa_bias);

		odm_set_rf_reg(dm, RF_PATH_B, 0x60, 0x000f0000, pg_pa_bias);

		power_trim_info->pa_bias_flag |= PA_BIAS_FLAG_ON;
	} else {
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8822c tx pa bias no pg\n");
	}

}

void phydm_get_set_thermal_trim_offset_8812f(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_therm = 0xff, thermal[2] = {0};

	odm_efuse_one_byte_read(dm, PPG_THERMAL_A_OFFSET_22C, &pg_therm, false);

	if (pg_therm != 0xff && pg_therm != 0x0) {
		/*s0*/
		pg_therm = pg_therm & 0x1f;

		thermal[RF_PATH_A] =
			((pg_therm & 0x1) << 3) | ((pg_therm >> 1) & 0x7);

		odm_set_rf_reg(dm, RF_PATH_A, RF_0x43, 0x000f0000, thermal[RF_PATH_A]);

		/*s1*/
		odm_efuse_one_byte_read(dm, PPG_THERMAL_B_OFFSET_22C, &pg_therm, false);

		pg_therm = pg_therm & 0x1f;

		thermal[RF_PATH_B] = ((pg_therm & 0x1) << 3) | ((pg_therm >> 1) & 0x7);

		odm_set_rf_reg(dm, RF_PATH_B, RF_0x43, 0x000f0000, thermal[RF_PATH_B]);

		power_trim_info->flag |= KFREE_FLAG_THERMAL_K_ON;

	}

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8812f thermal trim flag:0x%02x\n",
	       power_trim_info->flag);

	if (power_trim_info->flag & KFREE_FLAG_THERMAL_K_ON)
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8812f thermalA:%d thermalB:%d\n",
			thermal[RF_PATH_A],
			thermal[RF_PATH_B]);	
}

void phydm_set_power_trim_offset_8812f(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;
	u8 e_rf_path;

	for (e_rf_path = RF_PATH_A; e_rf_path < 2; e_rf_path++)
	{
		odm_set_rf_reg(dm, e_rf_path, RF_0xee, BIT(19), 1);

#if 0
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x0);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[0][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x1);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[1][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x2);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[2][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x3);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[2][e_rf_path]);
#endif
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x4);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[3][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x5);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[4][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x6);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[5][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x7);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[6][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x8);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[7][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x9);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[3][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0xa);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[4][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0xb);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[5][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0xc);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[6][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0xd);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[7][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0xe);
		odm_set_rf_reg(dm, e_rf_path, RF_0x3f, RFREGOFFSETMASK,
			power_trim_info->bb_gain[7][e_rf_path]);

		odm_set_rf_reg(dm, e_rf_path, RF_0xee, BIT(19), 0);
	}
}

void phydm_get_set_power_trim_offset_8812f(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_power = 0xff, i, j;
	u8 pg_power1 = 0, pg_power2 = 0, pg_power3 = 0;
	u8 pg_power4 = 0, pg_power5 = 0;

	odm_efuse_one_byte_read(dm, PPG_5GL1_TXA_22C, &pg_power1, false);
	odm_efuse_one_byte_read(dm, PPG_5GL1_TXB_22C, &pg_power2, false);
	odm_efuse_one_byte_read(dm, PPG_5GL2_TXA_22C, &pg_power3, false);
	odm_efuse_one_byte_read(dm, PPG_5GL2_TXB_22C, &pg_power4, false);
	odm_efuse_one_byte_read(dm, PPG_5GM1_TXA_22C, &pg_power5, false);

	if ((pg_power1 != 0xff || pg_power2 != 0xff || pg_power3 != 0xff ||
		pg_power4 != 0xff || pg_power5 != 0xff) &&
		(pg_power1 != 0x0 || pg_power2 != 0x0 || pg_power3 != 0x0 ||
		pg_power4 != 0x0 || pg_power5 != 0x0)) {
#if 0
		odm_efuse_one_byte_read(dm, PPG_2GL_TXAB_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[0][0] = pg_power & 0xf;
		power_trim_info->bb_gain[0][1] = (pg_power & 0xf0) >> 4;

		odm_efuse_one_byte_read(dm, PPG_2GM_TXAB_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[1][0] = pg_power & 0xf;
		power_trim_info->bb_gain[1][1] = (pg_power & 0xf0) >> 4;

		odm_efuse_one_byte_read(dm, PPG_2GH_TXAB_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[2][0] = pg_power & 0xf;
		power_trim_info->bb_gain[2][1] = (pg_power & 0xf0) >> 4;
#endif
		odm_efuse_one_byte_read(dm, PPG_5GL1_TXA_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[3][0] = pg_power & 0x1f;
		odm_efuse_one_byte_read(dm, PPG_5GL1_TXB_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[3][1] = pg_power & 0x1f;

		odm_efuse_one_byte_read(dm, PPG_5GL2_TXA_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[4][0] = pg_power & 0x1f;
		odm_efuse_one_byte_read(dm, PPG_5GL2_TXB_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[4][1] = pg_power & 0x1f;

		odm_efuse_one_byte_read(dm, PPG_5GM1_TXA_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[5][0] = pg_power & 0x1f;
		odm_efuse_one_byte_read(dm, PPG_5GM1_TXB_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[5][1] = pg_power & 0x1f;

		odm_efuse_one_byte_read(dm, PPG_5GM2_TXA_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[6][0] = pg_power & 0x1f;
		odm_efuse_one_byte_read(dm, PPG_5GM2_TXB_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[6][1] = pg_power & 0x1f;

		odm_efuse_one_byte_read(dm, PPG_5GH1_TXA_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[7][0] = pg_power & 0x1f;
		odm_efuse_one_byte_read(dm, PPG_5GH1_TXB_22C, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[7][1] = pg_power & 0x1f;

		power_trim_info->flag =
			power_trim_info->flag | KFREE_FLAG_ON | KFREE_FLAG_ON_5G;

		phydm_set_power_trim_offset_8812f(dm);
	}

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8812f power trim flag:0x%02x\n",
	       power_trim_info->flag);

	if (power_trim_info->flag & KFREE_FLAG_ON) {
		for (i = 0; i < KFREE_BAND_NUM; i++) {
			for (j = 0; j < 2; j++) {
				RF_DBG(dm, DBG_RF_MP,
				       "[kfree] 8812f pwr_trim->bb_gain[%d][%d]=0x%X\n",
				       i, j, power_trim_info->bb_gain[i][j]);
			}
		}
	}
}

void phydm_get_tssi_trim_offset_8812f(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 i, j ;
	u8 pg_power[16] = {0};

#if 0
	odm_efuse_one_byte_read(dm, TSSI_2GM_TXA_22C, &pg_power[0], false);
	odm_efuse_one_byte_read(dm, TSSI_2GM_TXB_22C, &pg_power[1], false);
	odm_efuse_one_byte_read(dm, TSSI_2GH_TXA_22C, &pg_power[2], false);
	odm_efuse_one_byte_read(dm, TSSI_2GH_TXB_22C, &pg_power[3], false);
#endif
	odm_efuse_one_byte_read(dm, TSSI_5GL1_TXA_22C, &pg_power[4], false);
	odm_efuse_one_byte_read(dm, TSSI_5GL1_TXB_22C, &pg_power[5], false);
	odm_efuse_one_byte_read(dm, TSSI_5GL2_TXA_22C, &pg_power[6], false);
	odm_efuse_one_byte_read(dm, TSSI_5GL2_TXB_22C, &pg_power[7], false);
	odm_efuse_one_byte_read(dm, TSSI_5GM1_TXA_22C, &pg_power[8], false);
	odm_efuse_one_byte_read(dm, TSSI_5GM1_TXB_22C, &pg_power[9], false);
	odm_efuse_one_byte_read(dm, TSSI_5GM2_TXA_22C, &pg_power[10], false);
	odm_efuse_one_byte_read(dm, TSSI_5GM2_TXB_22C, &pg_power[11], false);
	odm_efuse_one_byte_read(dm, TSSI_5GH1_TXA_22C, &pg_power[12], false);
	odm_efuse_one_byte_read(dm, TSSI_5GH1_TXB_22C, &pg_power[13], false);
	odm_efuse_one_byte_read(dm, TSSI_5GH2_TXA_22C, &pg_power[14], false);
	odm_efuse_one_byte_read(dm, TSSI_5GH2_TXB_22C, &pg_power[15], false);

	j = 0;
	for (i = 4; i < 16; i++) {
		if (pg_power[i] == 0xff || pg_power[i] == 0x0)
			j++;
	}

	if (j == 12) {
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8812f tssi trim no PG\n");
	} else {
#if 0
		power_trim_info->tssi_trim[0][0] = (s8)pg_power[0];
		power_trim_info->tssi_trim[0][1] = (s8)pg_power[1];
		power_trim_info->tssi_trim[1][0] = (s8)pg_power[0];
		power_trim_info->tssi_trim[1][1] = (s8)pg_power[1];
		power_trim_info->tssi_trim[2][0] = (s8)pg_power[2];
		power_trim_info->tssi_trim[2][1] = (s8)pg_power[3];
#endif
		power_trim_info->tssi_trim[3][0] = (s8)pg_power[4];
		power_trim_info->tssi_trim[3][1] = (s8)pg_power[5];
		power_trim_info->tssi_trim[4][0] = (s8)pg_power[6];
		power_trim_info->tssi_trim[4][1] = (s8)pg_power[7];
		power_trim_info->tssi_trim[5][0] = (s8)pg_power[8];
		power_trim_info->tssi_trim[5][1] = (s8)pg_power[9];
		power_trim_info->tssi_trim[6][0] = (s8)pg_power[10];
		power_trim_info->tssi_trim[6][1] = (s8)pg_power[11];
		power_trim_info->tssi_trim[7][0] = (s8)pg_power[12];
		power_trim_info->tssi_trim[7][1] = (s8)pg_power[13];
		power_trim_info->tssi_trim[8][0] = (s8)pg_power[14];
		power_trim_info->tssi_trim[8][1] = (s8)pg_power[15];

		power_trim_info->flag =
			power_trim_info->flag | TSSI_TRIM_FLAG_ON;

		if (power_trim_info->flag & TSSI_TRIM_FLAG_ON) {
			for (i = 0; i < KFREE_BAND_NUM; i++) {
				for (j = 0; j < 2; j++) {
					RF_DBG(dm, DBG_RF_MP,
					       "[kfree] 8812f tssi_trim[%d][%d]=0x%X\n",
					       i, j, power_trim_info->tssi_trim[i][j]);
				}
			}
		}
	}
}

s8 phydm_get_tssi_trim_de_8812f(void *dm_void, u8 path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 channel = *dm->channel, group = 0;

	if (channel >= 1 && channel <= 3)
		group = 0;
	else if (channel >= 4 && channel <= 9)
		group = 1;
	else if (channel >= 10 && channel <= 14)
		group = 2;
	else if (channel >= 36 && channel <= 50)
		group = 3;
	else if (channel >= 52 && channel <= 64)
		group = 4;
	else if (channel >= 100 && channel <= 118)
		group = 5;
	else if (channel >= 120 && channel <= 144)
		group = 6;
	else if (channel >= 149 && channel <= 165)
		group = 7;
	else if (channel >= 167 && channel <= 177)
		group = 8;
	else {
		RF_DBG(dm, DBG_RF_MP, "[kfree] Channel(%d) is not exist in Group\n",
			channel);
		return 0;
	}

	return power_trim_info->tssi_trim[group][path];
}

void phydm_get_set_pa_bias_offset_8812f(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_pa_bias = 0xff;

	RF_DBG(dm, DBG_RF_MP, "======>%s\n", __func__);

	odm_efuse_one_byte_read(dm, PPG_PABIAS_5GA_22C, &pg_pa_bias, false);

	if (pg_pa_bias != 0xff && pg_pa_bias != 0x0) {
#if 0
		/*2G s0*/
		odm_efuse_one_byte_read(dm, PPG_PABIAS_2GA_22C,
					&pg_pa_bias, false);
		pg_pa_bias = pg_pa_bias & 0xf;

		RF_DBG(dm, DBG_RF_MP, "[kfree] 2G s0 pa_bias=0x%x\n", pg_pa_bias);

		odm_set_rf_reg(dm, RF_PATH_A, 0x60, 0x0000f000, pg_pa_bias);

		/*2G s1*/
		odm_efuse_one_byte_read(dm, PPG_PABIAS_2GB_22C,
					&pg_pa_bias, false);
		pg_pa_bias = pg_pa_bias & 0xf;

		RF_DBG(dm, DBG_RF_MP, "[kfree] 2G s1 pa_bias=0x%x\n", pg_pa_bias);

		odm_set_rf_reg(dm, RF_PATH_B, 0x60, 0x0000f000, pg_pa_bias);
#endif

		/*5G s0*/
		odm_efuse_one_byte_read(dm, PPG_PABIAS_5GA_22C,
					&pg_pa_bias, false);
		pg_pa_bias = pg_pa_bias & 0xf;

		RF_DBG(dm, DBG_RF_MP, "[kfree] 5G s0 pa_bias=0x%x\n", pg_pa_bias);

		odm_set_rf_reg(dm, RF_PATH_A, 0x60, 0x000f0000, pg_pa_bias);

		/*5G s1*/
		odm_efuse_one_byte_read(dm, PPG_PABIAS_5GB_22C,
					&pg_pa_bias, false);
		pg_pa_bias = pg_pa_bias & 0xf;

		RF_DBG(dm, DBG_RF_MP, "[kfree] 5G s1 pa_bias=0x%x\n", pg_pa_bias);

		odm_set_rf_reg(dm, RF_PATH_B, 0x60, 0x000f0000, pg_pa_bias);

		power_trim_info->pa_bias_flag |= PA_BIAS_FLAG_ON;
	} else {
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8812f tx pa bias no pg\n");
	}

}

void phydm_get_thermal_trim_offset_8195b(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_therm = 0xff;

	odm_efuse_one_byte_read(dm, PPG_THERMAL_OFFSET_95B, &pg_therm, false);

	if (pg_therm != 0xff) {
		pg_therm = pg_therm & 0x1f;
		if ((pg_therm & BIT(0)) == 0)
			power_trim_info->thermal = (-1 * (pg_therm >> 1));
		else
			power_trim_info->thermal = (pg_therm >> 1);

		power_trim_info->flag |= KFREE_FLAG_THERMAL_K_ON;
	}

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8195b thermal trim flag:0x%02x\n",
	       power_trim_info->flag);

	if (power_trim_info->flag & KFREE_FLAG_THERMAL_K_ON)
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8195b thermal:%d\n",
		       power_trim_info->thermal);
}

void phydm_set_power_trim_rf_8195b(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	RF_DBG(dm, DBG_RF_MP,
		   "[kfree] %s:Set kfree to rf 0x33\n", __func__);

	if (power_trim_info->flag & KFREE_FLAG_ON) { 
		odm_set_rf_reg(dm, RF_PATH_A, RF_0xee, BIT(19), 1);

		if (power_trim_info->flag & KFREE_FLAG_ON_2G) {
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0x0);
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, 0x0000003f,
				power_trim_info->bb_gain[0][RF_PATH_A]);
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0x1);
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, 0x0000003f,
				power_trim_info->bb_gain[1][RF_PATH_A]);
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0x2);
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, 0x0000003f,
				power_trim_info->bb_gain[2][RF_PATH_A]);
		}

		if (power_trim_info->flag & KFREE_FLAG_ON_5G) {
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0x4);
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, 0x0000003f,
				power_trim_info->bb_gain[3][RF_PATH_A]);
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0x5);
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, 0x0000003f,
				power_trim_info->bb_gain[4][RF_PATH_A]);
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0x6);
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, 0x0000003f,
				power_trim_info->bb_gain[5][RF_PATH_A]);
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0x7);
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, 0x0000003f,
				power_trim_info->bb_gain[6][RF_PATH_A]);
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0x8);
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, 0x0000003f,
				power_trim_info->bb_gain[7][RF_PATH_A]);
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0xe);
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, 0x0000003f,
				power_trim_info->bb_gain[7][RF_PATH_A]);
		}

		odm_set_rf_reg(dm, RF_PATH_A, RF_0xee, BIT(19), 0);
	}

}

void phydm_get_set_power_trim_offset_8195b(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_power = 0xff, i, j;

	odm_efuse_one_byte_read(dm, PPG_2GL_TXA_95B, &pg_power, false);

	if (pg_power != 0xff) {
		odm_efuse_one_byte_read(dm, PPG_2GL_TXA_95B, &pg_power, false);
		power_trim_info->bb_gain[0][0] = pg_power & 0xf;

		odm_efuse_one_byte_read(dm, PPG_2GM_TXA_95B, &pg_power, false);
		power_trim_info->bb_gain[1][0] = pg_power & 0xf;

		odm_efuse_one_byte_read(dm, PPG_2GH_TXA_95B, &pg_power, false);
		power_trim_info->bb_gain[2][0] = pg_power & 0xf;

		power_trim_info->flag =
			power_trim_info->flag | KFREE_FLAG_ON | KFREE_FLAG_ON_2G;
	}

	pg_power = 0xff;

	odm_efuse_one_byte_read(dm, PPG_5GL1_TXA_95B, &pg_power, false);

	if (pg_power != 0xff) {
		odm_efuse_one_byte_read(dm, PPG_5GL1_TXA_95B, &pg_power, false);
		power_trim_info->bb_gain[3][0] = pg_power & 0x1f;

		odm_efuse_one_byte_read(dm, PPG_5GL2_TXA_95B, &pg_power, false);
		power_trim_info->bb_gain[4][0] = pg_power & 0x1f;

		odm_efuse_one_byte_read(dm, PPG_5GM1_TXA_95B, &pg_power, false);
		power_trim_info->bb_gain[5][0] = pg_power & 0x1f;

		odm_efuse_one_byte_read(dm, PPG_5GM2_TXA_95B, &pg_power, false);
		power_trim_info->bb_gain[6][0] = pg_power & 0x1f;

		odm_efuse_one_byte_read(dm, PPG_5GH1_TXA_95B, &pg_power, false);
		power_trim_info->bb_gain[7][0] = pg_power & 0x1f;

		power_trim_info->flag =
			power_trim_info->flag | KFREE_FLAG_ON | KFREE_FLAG_ON_5G;
	}

	phydm_set_power_trim_rf_8195b(dm);

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8195b power trim flag:0x%02x\n",
	       power_trim_info->flag);

	if (power_trim_info->flag & KFREE_FLAG_ON) {
		for (i = 0; i < KFREE_BAND_NUM; i++) {
			for (j = 0; j < 1; j++) {
				RF_DBG(dm, DBG_RF_MP,
				       "[kfree] 8195b pwr_trim->bb_gain[%d][%d]=0x%X\n",
				       i, j, power_trim_info->bb_gain[i][j]);
			}
		}
	}
}

void phydm_get_set_pa_bias_offset_8195b(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_pa_bias = 0xff;

	RF_DBG(dm, DBG_RF_MP, "======>%s\n", __func__);

	/*2G*/
	odm_efuse_one_byte_read(dm, PPG_PABIAS_2GA_95B, &pg_pa_bias, false);

	if (pg_pa_bias != 0xff) {
		odm_efuse_one_byte_read(dm, PPG_PABIAS_2GA_95B,
					&pg_pa_bias, false);
		pg_pa_bias = pg_pa_bias & 0xf;

		RF_DBG(dm, DBG_RF_MP, "[kfree] 2G pa_bias=0x%x\n", pg_pa_bias);

		odm_set_rf_reg(dm, RF_PATH_A, 0x60, 0x0000f000, pg_pa_bias);
	} else {
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8195b 2G tx pa bias no pg\n");
	}

	/*5G*/
	pg_pa_bias = 0xff;

	odm_efuse_one_byte_read(dm, PPG_PABIAS_5GA_95B, &pg_pa_bias, false);

	if (pg_pa_bias != 0xff) {
		odm_efuse_one_byte_read(dm, PPG_PABIAS_5GA_95B,
					&pg_pa_bias, false);
		pg_pa_bias = pg_pa_bias & 0xf;

		RF_DBG(dm, DBG_RF_MP, "[kfree] 5G pa_bias=0x%x\n", pg_pa_bias);

		odm_set_rf_reg(dm, RF_PATH_A, 0x60, 0x000f0000, pg_pa_bias);

		power_trim_info->pa_bias_flag |= PA_BIAS_FLAG_ON;
	} else {
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8195b 5G tx pa bias no pg\n");
	}
}

void phydm_get_thermal_trim_offset_8721d(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_therm = 0xff;

	odm_efuse_one_byte_read(dm, PPG_THERMAL_OFFSET_8721D, &pg_therm, false);

	if (pg_therm != 0xff) {
		pg_therm = pg_therm & 0x1f;
		if ((pg_therm & BIT(0)) == 0)
			power_trim_info->thermal = (-1 * (pg_therm >> 1));
		else
			power_trim_info->thermal = (pg_therm >> 1);

		power_trim_info->flag |= KFREE_FLAG_THERMAL_K_ON;
	}

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8721d thermal trim flag:0x%02x\n",
	       power_trim_info->flag);

	if (power_trim_info->flag & KFREE_FLAG_THERMAL_K_ON)
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8721d thermal:%d\n",
		       power_trim_info->thermal);
}

void phydm_set_power_trim_rf_8721d(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	RF_DBG(dm, DBG_RF_MP, "[kfree] %s:Set kfree to rf 0x33\n", __func__);

	odm_set_rf_reg(dm, RF_PATH_A, RF_0xee, BIT(19), 1);

	odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0x0);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, 0x0000003f,
		       power_trim_info->bb_gain[0][RF_PATH_A]);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0x1);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, 0x0000003f,
		       power_trim_info->bb_gain[1][RF_PATH_A]);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0x2);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, 0x0000003f,
		       power_trim_info->bb_gain[2][RF_PATH_A]);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0x3);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, 0x0000003f,
		       power_trim_info->bb_gain[2][RF_PATH_A]);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0x4);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, 0x0000003f,
		       power_trim_info->bb_gain[3][RF_PATH_A]);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0x5);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, 0x0000003f,
		       power_trim_info->bb_gain[4][RF_PATH_A]);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0x6);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, 0x0000003f,
		       power_trim_info->bb_gain[5][RF_PATH_A]);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0x7);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, 0x0000003f,
		       power_trim_info->bb_gain[6][RF_PATH_A]);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0x8);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, 0x0000003f,
		       power_trim_info->bb_gain[7][RF_PATH_A]);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0x9);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, RFREGOFFSETMASK,
		       power_trim_info->bb_gain[3][RF_PATH_A]);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0xa);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, RFREGOFFSETMASK,
		       power_trim_info->bb_gain[4][RF_PATH_A]);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0xb);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, RFREGOFFSETMASK,
		       power_trim_info->bb_gain[5][RF_PATH_A]);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0xc);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, RFREGOFFSETMASK,
		       power_trim_info->bb_gain[6][RF_PATH_A]);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0xd);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, RFREGOFFSETMASK,
		       power_trim_info->bb_gain[7][RF_PATH_A]);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0xe);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, RFREGOFFSETMASK,
		       power_trim_info->bb_gain[7][RF_PATH_A]);

	odm_set_rf_reg(dm, RF_PATH_A, RF_0xee, BIT(19), 0);
}

void phydm_get_set_power_trim_offset_8721d(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_power = 0xff, i, j;
	u8 pg_power1, pg_power2, pg_power3, pg_power4, pg_power5, pg_power6;

	odm_efuse_one_byte_read(dm, PPG_2G_TXA_8721D, &pg_power1, false);
	odm_efuse_one_byte_read(dm, PPG_5GL1_TXA_8721D, &pg_power2, false);
	odm_efuse_one_byte_read(dm, PPG_5GL2_TXA_8721D, &pg_power3, false);
	odm_efuse_one_byte_read(dm, PPG_5GM1_TXA_8721D, &pg_power4, false);
	odm_efuse_one_byte_read(dm, PPG_5GM2_TXA_8721D, &pg_power5, false);
	odm_efuse_one_byte_read(dm, PPG_5GH1_TXA_8721D, &pg_power6, false);

	if (pg_power1 != 0xff || pg_power2 != 0xff || pg_power3 != 0xff ||
	    pg_power4 != 0xff || pg_power5 != 0xff || pg_power6 != 0xff) {
		odm_efuse_one_byte_read(dm, PPG_2G_TXA_8721D, &pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[0][0] = pg_power & 0xf;
		power_trim_info->bb_gain[1][0] = pg_power & 0xf;
		power_trim_info->bb_gain[2][0] = pg_power & 0xf;

		odm_efuse_one_byte_read(dm, PPG_5GL1_TXA_8721D,
					&pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[3][0] = pg_power & 0x1f;

		odm_efuse_one_byte_read(dm, PPG_5GL2_TXA_8721D,
					&pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[4][0] = pg_power & 0x1f;

		odm_efuse_one_byte_read(dm, PPG_5GM1_TXA_8721D,
					&pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[5][0] = pg_power & 0x1f;

		odm_efuse_one_byte_read(dm, PPG_5GM2_TXA_8721D,
					&pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[6][0] = pg_power & 0x1f;

		odm_efuse_one_byte_read(dm, PPG_5GH1_TXA_8721D,
					&pg_power, false);
		if (pg_power == 0xff)
			pg_power = 0;
		power_trim_info->bb_gain[7][0] = pg_power & 0x1f;

		power_trim_info->flag =
			power_trim_info->flag | KFREE_FLAG_ON |
						KFREE_FLAG_ON_2G |
						KFREE_FLAG_ON_5G;

		phydm_set_power_trim_rf_8721d(dm);
	}

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8721d power trim flag:0x%02x\n",
	       power_trim_info->flag);

	if (power_trim_info->flag & KFREE_FLAG_ON) {
		for (i = 0; i < KFREE_BAND_NUM; i++) {
			for (j = 0; j < 1; j++) {
				RF_DBG(dm, DBG_RF_MP,
				       "[kfree] 8721d pwr_trim->bb_gain[%d][%d]=0x%X\n",
				       i, j, power_trim_info->bb_gain[i][j]);
			}
		}
	}
}

void phydm_get_set_pa_bias_offset_8721d(void *dm_void)
{
#if 0
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_pa_bias = 0xff;

	RF_DBG(dm, DBG_RF_MP, "======>%s\n", __func__);

	odm_efuse_one_byte_read(dm, PPG_PABIAS_2GA_95B, &pg_pa_bias, false);

	if (pg_pa_bias != 0xff) {
		/*2G*/
		odm_efuse_one_byte_read(dm, PPG_PABIAS_2GA_95B,
					&pg_pa_bias, false);
		pg_pa_bias = pg_pa_bias & 0xf;

		RF_DBG(dm, DBG_RF_MP, "[kfree] 2G pa_bias=0x%x\n", pg_pa_bias);

		odm_set_rf_reg(dm, RF_PATH_A, 0x60, 0x0000f000, pg_pa_bias);

		/*5G*/
		odm_efuse_one_byte_read(dm, PPG_PABIAS_5GA_95B,
					&pg_pa_bias, false);
		pg_pa_bias = pg_pa_bias & 0xf;

		RF_DBG(dm, DBG_RF_MP, "[kfree] 5G pa_bias=0x%x\n", pg_pa_bias);

		odm_set_rf_reg(dm, RF_PATH_A, 0x60, 0x000f0000, pg_pa_bias);

		power_trim_info->pa_bias_flag |= PA_BIAS_FLAG_ON;
	} else {
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8721d tx pa bias no pg\n");
	}
#endif
}

void phydm_get_thermal_trim_offset_8197g(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_therm = 0xff, i;

	odm_efuse_one_byte_read(dm, PPG_THERMAL_A_OFFSET_97G, &pg_therm, false);

	if (pg_therm != 0x0) {
		for (i = 0; i < 2; i++) {
			if (i == 0)
				odm_efuse_one_byte_read(dm, PPG_THERMAL_A_OFFSET_97G, &pg_therm, false);
			else if (i == 1)
				odm_efuse_one_byte_read(dm, PPG_THERMAL_B_OFFSET_97G, &pg_therm, false);

			RF_DBG(dm, DBG_RF_MP, "[kfree] 8197g Efuse thermal S%d:0x%x\n", i, pg_therm);

			pg_therm = pg_therm & 0x1f;
			if ((pg_therm & BIT(0)) == 0)
				power_trim_info->multi_thermal[i] = (-1 * (pg_therm >> 1));
			else
				power_trim_info->multi_thermal[i] = (pg_therm >> 1);
		}

		power_trim_info->flag |= KFREE_FLAG_THERMAL_K_ON;
	}

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8197g thermal trim flag:0x%02x\n",
	       power_trim_info->flag);

	for (i = 0; i < 2; i++) {
		if (power_trim_info->flag & KFREE_FLAG_THERMAL_K_ON)
			RF_DBG(dm, DBG_RF_MP, "[kfree] 8197g thermal S%d:%d\n",
			       i ,power_trim_info->multi_thermal[i]);
	}
}

void phydm_set_power_trim_offset_8197g(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;
	u8 e_rf_path;

	for (e_rf_path = RF_PATH_A; e_rf_path < 2; e_rf_path++)
	{
		odm_set_rf_reg(dm, e_rf_path, RF_0xef, BIT(7), 1);

		odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x1c000, 0);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x3F,
			power_trim_info->bb_gain[0][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x1c000, 1);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x3F,
			power_trim_info->bb_gain[0][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x1c000, 2);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x3F, 
			power_trim_info->bb_gain[1][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x1c000, 3);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x3F, 
			power_trim_info->bb_gain[1][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x1c000, 4);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x3F, 
			power_trim_info->bb_gain[2][e_rf_path]);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x1c000, 5);
		odm_set_rf_reg(dm, e_rf_path, RF_0x33, 0x3F, 
			power_trim_info->bb_gain[2][e_rf_path]);

		odm_set_rf_reg(dm, e_rf_path, RF_0xef, BIT(7), 0);
	}

}

void phydm_get_set_power_trim_offset_8197g(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_power = 0, i, j;

	odm_efuse_one_byte_read(dm, PPG_2GL_TXAB_97G, &pg_power, false);

	if (pg_power != 0) {
		power_trim_info->bb_gain[0][0] = pg_power & 0xf;
		power_trim_info->bb_gain[0][1] = (pg_power & 0xf0) >> 4;

		odm_efuse_one_byte_read(dm, PPG_2GM_TXAB_97G, &pg_power, false);
		power_trim_info->bb_gain[1][0] = pg_power & 0xf;
		power_trim_info->bb_gain[1][1] = (pg_power & 0xf0) >> 4;

		odm_efuse_one_byte_read(dm, PPG_2GH_TXAB_97G, &pg_power, false);
		power_trim_info->bb_gain[2][0] = pg_power & 0xf;
		power_trim_info->bb_gain[2][1] = (pg_power & 0xf0) >> 4;

		phydm_set_power_trim_offset_8197g(dm);

		power_trim_info->flag =
			power_trim_info->flag | KFREE_FLAG_ON | KFREE_FLAG_ON_2G;
	}

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8197g power trim flag:0x%02x\n",
	       power_trim_info->flag);

	if (power_trim_info->flag & KFREE_FLAG_ON) {
		for (i = 0; i < KFREE_BAND_NUM; i++) {
			for (j = 0; j < MAX_RF_PATH; j++) {
				RF_DBG(dm, DBG_RF_MP,
				       "[kfree] 8197g pwr_trim->bb_gain[%d][%d]=0x%X\n",
				       i, j, power_trim_info->bb_gain[i][j]);
			}
		}
	}
}

void phydm_get_tssi_trim_offset_8197g(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 i, j;
	u8 pg_power[4] = {0};

	odm_efuse_one_byte_read(dm, TSSI_2GL_TXA_97G, &pg_power[0], false);
	odm_efuse_one_byte_read(dm, TSSI_2GL_TXB_97G, &pg_power[1], false);
	odm_efuse_one_byte_read(dm, TSSI_2GH_TXA_97G, &pg_power[2], false);
	odm_efuse_one_byte_read(dm, TSSI_2GH_TXB_97G, &pg_power[3], false);

	j = 0;
	for (i = 0; i < 4; i++) {
		if (pg_power[i] == 0x0)
			j++;
	}

	if (j == 4) {
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8197g tssi trim no PG\n");
	} else {
		power_trim_info->tssi_trim[0][0] = (s8)pg_power[0];
		power_trim_info->tssi_trim[0][1] = (s8)pg_power[1];
		power_trim_info->tssi_trim[1][0] = (s8)pg_power[0];
		power_trim_info->tssi_trim[1][1] = (s8)pg_power[1];
		power_trim_info->tssi_trim[2][0] = (s8)pg_power[2];
		power_trim_info->tssi_trim[2][1] = (s8)pg_power[3];

		power_trim_info->flag =
			power_trim_info->flag | TSSI_TRIM_FLAG_ON;

		for (i = 0; i < KFREE_BAND_NUM; i++) {
			for (j = 0; j < MAX_PATH_NUM_8197G; j++) {
				RF_DBG(dm, DBG_RF_MP,
				       "[kfree] 8197g tssi_trim[%d][%d]=0x%X\n",
				       i, j, power_trim_info->tssi_trim[i][j]);
			}
		}
	}
}

s8 phydm_get_tssi_trim_de_8197g(void *dm_void, u8 path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 channel = *dm->channel, group = 0;

	if (channel >= 1 && channel <= 3)
		group = 0;
	else if (channel >= 4 && channel <= 9)
		group = 1;
	else if (channel >= 10 && channel <= 14)
		group = 2;
	else {
		RF_DBG(dm, DBG_RF_MP, "[kfree] Channel(%d) is not exist in Group\n",
			channel);
		return 0;
	}

	return power_trim_info->tssi_trim[group][path];
}

void phydm_get_set_pa_bias_offset_8197g(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_pa_bias = 0xff, i;
	u8 tx_pa_bias[4] = {0};

	odm_efuse_one_byte_read(dm, PPG_PABIAS_2GAB_97G, &pg_pa_bias, false);

	if (pg_pa_bias != 0x0) {
		/*paht ab*/
		odm_efuse_one_byte_read(dm, PPG_PABIAS_2GAB_97G,
					&pg_pa_bias, false);
		tx_pa_bias[0] = pg_pa_bias & 0xf;
		tx_pa_bias[1] = ((pg_pa_bias & 0xf0) >> 4);

		RF_DBG(dm, DBG_RF_MP,
		       "[kfree] 8197g PathA_pa_bias:0x%x PathB_pa_bias:0x%x\n",
		       tx_pa_bias[0], tx_pa_bias[1]);

		for (i = RF_PATH_A; i < 2; i++)
			odm_set_rf_reg(dm, i, 0x60, 0x0000f000, tx_pa_bias[i]);

		power_trim_info->pa_bias_flag |= PA_BIAS_FLAG_ON;
	} else {
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8197g tx pa bias no pg\n");
	}
}

void phydm_get_set_lna_offset_8197g(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_lna[2] = {0}, i, pg_lna_tmp = 0;
	u32 lna_trim_addr[2] = {0x1884, 0x4184};

	odm_efuse_one_byte_read(dm, PPG_LNA_2GA_97G, &pg_lna_tmp, false);

	if (pg_lna_tmp != 0) {
		odm_efuse_one_byte_read(dm, PPG_LNA_2GA_97G,
					&pg_lna[RF_PATH_A], false);
		power_trim_info->lna_trim[RF_PATH_A] = (s8)pg_lna[RF_PATH_A];

		odm_efuse_one_byte_read(dm, PPG_LNA_2GB_97G,
					&pg_lna[RF_PATH_B], false);
		power_trim_info->lna_trim[RF_PATH_B] = (s8)pg_lna[RF_PATH_B];

		for (i = RF_PATH_A; i < 2; i++) {
			if (odm_get_bb_reg(dm, lna_trim_addr[i], 0x00c00000) == 0x2) {
				odm_set_rf_reg(dm, i, 0x88, 0x00000f00, (pg_lna[i] & 0xf));
				RF_DBG(dm, DBG_RF_MP, "[kfree] 8197g lna trim CG 0x%x path=%d\n", (pg_lna[i] & 0xf), i);
			} else if (odm_get_bb_reg(dm, lna_trim_addr[i], 0x00c00000) == 0x3) {
				odm_set_rf_reg(dm, i, 0x88, 0x00000f00, ((pg_lna[i] & 0xf0) >> 4));
				RF_DBG(dm, DBG_RF_MP, "[kfree] 8197g lna trim CS 0x%x path=%d\n", ((pg_lna[i] & 0xf0) >> 4), i);
			}
		}

		power_trim_info->lna_flag |= LNA_FLAG_ON;
	} else {
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8197g lna trim no pg\n");
	}
}

void phydm_set_lna_trim_offset_8197g(void *dm_void, u8 path, u8 cg_cs, u8 enable)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *trim = &dm->power_trim_data;

	u8 i;

	if (enable == 0) {
		for (i = RF_PATH_A; i < 2; i++) {
			odm_set_rf_reg(dm, i, 0x88, 0x00000f00, 0x0);
			RF_DBG(dm, DBG_RF_MP, "[kfree] 8197g diversity lna trim disable\n");
		}
		return;
	}

	/*cg*/
	if (cg_cs == 0) {
		odm_set_rf_reg(dm, path, 0x88, 0x00000f00, (trim->lna_trim[path] & 0xf));
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8197g diversity lna trim CG 0x%x path=%d\n",
			(trim->lna_trim[path] & 0xf), path);
	} else if (cg_cs == 1) {	/*cs*/
		odm_set_rf_reg(dm, path, 0x88, 0x00000f00, ((trim->lna_trim[path] & 0xf0) >> 4));
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8197g diversity lna trim CS 0x%x path=%d\n",
			((trim->lna_trim[path] & 0xf0) >> 4), path);
	}
}


void phydm_get_thermal_trim_offset_8710c(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_therm = 0;

	odm_efuse_one_byte_read(dm, PPG_THERMAL_OFFSET_10C, &pg_therm, false);
	RF_DBG(dm, DBG_RF_MP, "[kfree] 8710c Efuse thermal:0x%x\n", pg_therm);

	if (pg_therm != 0xff) {
		pg_therm = pg_therm & 0x1f;
		if ((pg_therm & BIT(0)) == 0)
			power_trim_info->thermal = (-1 * (pg_therm >> 1));
		else
			power_trim_info->thermal = (pg_therm >> 1);

		power_trim_info->flag |= KFREE_FLAG_THERMAL_K_ON;
	}

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8710c thermal trim flag:0x%02x\n",
	       power_trim_info->flag);

	if (power_trim_info->flag & KFREE_FLAG_THERMAL_K_ON)
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8710c thermal:%d\n",
		       power_trim_info->thermal);
}

void phydm_set_power_trim_offset_8710c(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	odm_set_rf_reg(dm, RF_PATH_A, RF_0xef, BIT(18), 1);

	odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, 0x3f,
		power_trim_info->bb_gain[0][RF_PATH_A]);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 1);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, 0x3f,
		power_trim_info->bb_gain[1][RF_PATH_A]);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 2);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, 0x3f, 
		power_trim_info->bb_gain[2][RF_PATH_A]);

	odm_set_rf_reg(dm, RF_PATH_A, RF_0xef, BIT(18), 0);
}

void phydm_get_set_power_trim_offset_8710c(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_power = 0, i, j;

	odm_efuse_one_byte_read(dm, PPG_2GL_TX_10C, &pg_power, false);

	if (pg_power != 0xff) {
		power_trim_info->bb_gain[0][RF_PATH_A] = pg_power & 0xf;

		odm_efuse_one_byte_read(dm, PPG_2GM_TX_10C, &pg_power, false);
		power_trim_info->bb_gain[1][RF_PATH_A] = pg_power & 0xf;

		odm_efuse_one_byte_read(dm, PPG_2GH_TX_10C, &pg_power, false);
		power_trim_info->bb_gain[2][RF_PATH_A] = pg_power & 0xf;

		phydm_set_power_trim_offset_8710c(dm);

		power_trim_info->flag =
			power_trim_info->flag | KFREE_FLAG_ON | KFREE_FLAG_ON_2G;
	}

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8710c power trim flag:0x%02x\n",
	       power_trim_info->flag);

	if (power_trim_info->flag & KFREE_FLAG_ON) {
		for (i = 0; i < KFREE_BAND_NUM; i++) {
			for (j = 0; j < MAX_RF_PATH; j++) {
				RF_DBG(dm, DBG_RF_MP,
				       "[kfree] 8710c pwr_trim->bb_gain[%d][%d]=0x%X\n",
				       i, j, power_trim_info->bb_gain[i][j]);
			}
		}
	}
}

void phydm_get_set_pa_bias_offset_8710c(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_pa_bias = 0xff;
	u8 tx_pa_bias = 0;

	odm_efuse_one_byte_read(dm, PPG_PABIAS_10C, &pg_pa_bias, false);

	if (pg_pa_bias != 0xff) {
		tx_pa_bias = pg_pa_bias & 0xf;

		RF_DBG(dm, DBG_RF_MP,
		       "[kfree] 8710c PathA_pa_bias:0x%x\n", tx_pa_bias);

		odm_set_rf_reg(dm, RF_PATH_A, 0x60, 0x0000f000, tx_pa_bias);

		power_trim_info->pa_bias_flag |= PA_BIAS_FLAG_ON;
	} else {
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8710c tx pa bias no pg\n");
	}
}

void phydm_set_power_trim_offset_8814b(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;
	u8 e_rf_path;

	for (e_rf_path = RF_PATH_A; e_rf_path < MAX_PATH_NUM_8814B; e_rf_path++)
	{
		if (power_trim_info->flag & KFREE_FLAG_ON) {
			odm_set_rf_reg(dm, e_rf_path, RF_0xee, BIT(19), 1);

			if (power_trim_info->flag & KFREE_FLAG_ON_2G) {
				odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x0);
				odm_set_rf_reg(dm, e_rf_path, RF_0x30, RFREGOFFSETMASK,
					power_trim_info->bb_gain[0][e_rf_path]);
				odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x1);
				odm_set_rf_reg(dm, e_rf_path, RF_0x30, RFREGOFFSETMASK,
					power_trim_info->bb_gain[0][e_rf_path]);
				odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x2);
				odm_set_rf_reg(dm, e_rf_path, RF_0x30, RFREGOFFSETMASK,
					power_trim_info->bb_gain[0][e_rf_path]);
				odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x3);
				odm_set_rf_reg(dm, e_rf_path, RF_0x30, RFREGOFFSETMASK,
					power_trim_info->bb_gain[0][e_rf_path]);
			}

			if (power_trim_info->flag & KFREE_FLAG_ON_5G) {
				odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x4);
				odm_set_rf_reg(dm, e_rf_path, RF_0x30, RFREGOFFSETMASK,
					power_trim_info->bb_gain[3][e_rf_path]);
				odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x5);
				odm_set_rf_reg(dm, e_rf_path, RF_0x30, RFREGOFFSETMASK,
					power_trim_info->bb_gain[4][e_rf_path]);
				odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x6);
				odm_set_rf_reg(dm, e_rf_path, RF_0x30, RFREGOFFSETMASK,
					power_trim_info->bb_gain[5][e_rf_path]);
				odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x7);
				odm_set_rf_reg(dm, e_rf_path, RF_0x30, RFREGOFFSETMASK,
					power_trim_info->bb_gain[6][e_rf_path]);
				odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x8);
				odm_set_rf_reg(dm, e_rf_path, RF_0x30, RFREGOFFSETMASK,
					power_trim_info->bb_gain[7][e_rf_path]);
				odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0x9);
				odm_set_rf_reg(dm, e_rf_path, RF_0x30, RFREGOFFSETMASK,
					power_trim_info->bb_gain[3][e_rf_path]);
				odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0xa);
				odm_set_rf_reg(dm, e_rf_path, RF_0x30, RFREGOFFSETMASK,
					power_trim_info->bb_gain[4][e_rf_path]);
				odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0xb);
				odm_set_rf_reg(dm, e_rf_path, RF_0x30, RFREGOFFSETMASK,
					power_trim_info->bb_gain[5][e_rf_path]);
				odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0xc);
				odm_set_rf_reg(dm, e_rf_path, RF_0x30, RFREGOFFSETMASK,
					power_trim_info->bb_gain[6][e_rf_path]);
				odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0xd);
				odm_set_rf_reg(dm, e_rf_path, RF_0x30, RFREGOFFSETMASK,
					power_trim_info->bb_gain[7][e_rf_path]);
				odm_set_rf_reg(dm, e_rf_path, RF_0x33, RFREGOFFSETMASK, 0xe);
				odm_set_rf_reg(dm, e_rf_path, RF_0x30, RFREGOFFSETMASK,
					power_trim_info->bb_gain[7][e_rf_path]);
			}

			odm_set_rf_reg(dm, e_rf_path, RF_0xee, BIT(19), 0);
		}
	}
}

void phydm_get_set_power_trim_offset_8814b(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 i, j;
	u8 pg_power1, pg_power2;
	u8 pg_power_2g[2] = {0}, pg_power_5g[20] = {0};

	odm_efuse_one_byte_read(dm, PPG_2GL_TXAB_14B, &pg_power_2g[0], false);
	odm_efuse_one_byte_read(dm, PPG_2GL_TXCD_14B, &pg_power_2g[1], false);

	j = 0;
	for (i = 0; i < 2; i++) {
		if (pg_power_2g[i] == 0xff)
			j++;
	}

	if (j == 2) {
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8814b 2G power trim no PG\n");
	} else {
		power_trim_info->bb_gain[0][RF_PATH_A] = pg_power_2g[0] & 0xf;
		power_trim_info->bb_gain[0][RF_PATH_B] = (pg_power_2g[0] & 0xf0) >> 4;

		power_trim_info->bb_gain[0][RF_PATH_C] = pg_power_2g[1] & 0xf;
		power_trim_info->bb_gain[0][RF_PATH_D] = (pg_power_2g[1] & 0xf0) >> 4;

		power_trim_info->flag =
			power_trim_info->flag | KFREE_FLAG_ON | KFREE_FLAG_ON_2G;
	}

	odm_efuse_one_byte_read(dm, PPG_5GL1_TXA_14B, &pg_power_5g[0], false);
	odm_efuse_one_byte_read(dm, PPG_5GL1_TXB_14B, &pg_power_5g[1], false);
	odm_efuse_one_byte_read(dm, PPG_5GL1_TXC_14B, &pg_power_5g[2], false);
	odm_efuse_one_byte_read(dm, PPG_5GL1_TXD_14B, &pg_power_5g[3], false);
	odm_efuse_one_byte_read(dm, PPG_5GL2_TXA_14B, &pg_power_5g[4], false);
	odm_efuse_one_byte_read(dm, PPG_5GL2_TXB_14B, &pg_power_5g[5], false);
	odm_efuse_one_byte_read(dm, PPG_5GL2_TXC_14B, &pg_power_5g[6], false);
	odm_efuse_one_byte_read(dm, PPG_5GL2_TXD_14B, &pg_power_5g[7], false);
	odm_efuse_one_byte_read(dm, PPG_5GM1_TXA_14B, &pg_power_5g[8], false);
	odm_efuse_one_byte_read(dm, PPG_5GM1_TXB_14B, &pg_power_5g[9], false);
	odm_efuse_one_byte_read(dm, PPG_5GM1_TXC_14B, &pg_power_5g[10], false);
	odm_efuse_one_byte_read(dm, PPG_5GM1_TXD_14B, &pg_power_5g[11], false);
	odm_efuse_one_byte_read(dm, PPG_5GM2_TXA_14B, &pg_power_5g[12], false);
	odm_efuse_one_byte_read(dm, PPG_5GM2_TXB_14B, &pg_power_5g[13], false);
	odm_efuse_one_byte_read(dm, PPG_5GM2_TXC_14B, &pg_power_5g[14], false);
	odm_efuse_one_byte_read(dm, PPG_5GM2_TXD_14B, &pg_power_5g[15], false);
	odm_efuse_one_byte_read(dm, PPG_5GH1_TXA_14B, &pg_power_5g[16], false);
	odm_efuse_one_byte_read(dm, PPG_5GH1_TXB_14B, &pg_power_5g[17], false);
	odm_efuse_one_byte_read(dm, PPG_5GH1_TXC_14B, &pg_power_5g[18], false);
	odm_efuse_one_byte_read(dm, PPG_5GH1_TXD_14B, &pg_power_5g[19], false);

	j = 0;
	for (i = 0; i < 20; i++) {
		if (pg_power_5g[i] == 0xff)
			j++;
	}

	if (j == 20) {
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8814b 5G power trim no PG\n");
	} else {
		power_trim_info->bb_gain[3][RF_PATH_A] = pg_power_5g[0] & 0x1f;
		power_trim_info->bb_gain[3][RF_PATH_B] = pg_power_5g[1] & 0x1f;
		power_trim_info->bb_gain[3][RF_PATH_C] = pg_power_5g[2] & 0x1f;
		power_trim_info->bb_gain[3][RF_PATH_D] = pg_power_5g[3] & 0x1f;

		power_trim_info->bb_gain[4][RF_PATH_A] = pg_power_5g[4] & 0x1f;
		power_trim_info->bb_gain[4][RF_PATH_B] = pg_power_5g[5] & 0x1f;
		power_trim_info->bb_gain[4][RF_PATH_C] = pg_power_5g[6] & 0x1f;
		power_trim_info->bb_gain[4][RF_PATH_D] = pg_power_5g[7] & 0x1f;

		power_trim_info->bb_gain[5][RF_PATH_A] = pg_power_5g[8] & 0x1f;
		power_trim_info->bb_gain[5][RF_PATH_B] = pg_power_5g[9] & 0x1f;
		power_trim_info->bb_gain[5][RF_PATH_C] = pg_power_5g[10] & 0x1f;
		power_trim_info->bb_gain[5][RF_PATH_D] = pg_power_5g[11] & 0x1f;

		power_trim_info->bb_gain[6][RF_PATH_A] = pg_power_5g[12] & 0x1f;
		power_trim_info->bb_gain[6][RF_PATH_B] = pg_power_5g[13] & 0x1f;
		power_trim_info->bb_gain[6][RF_PATH_C] = pg_power_5g[14] & 0x1f;
		power_trim_info->bb_gain[6][RF_PATH_D] = pg_power_5g[15] & 0x1f;

		power_trim_info->bb_gain[7][RF_PATH_A] = pg_power_5g[16] & 0x1f;
		power_trim_info->bb_gain[7][RF_PATH_B] = pg_power_5g[17] & 0x1f;
		power_trim_info->bb_gain[7][RF_PATH_C] = pg_power_5g[18] & 0x1f;
		power_trim_info->bb_gain[7][RF_PATH_D] = pg_power_5g[19] & 0x1f;

		power_trim_info->flag =
			power_trim_info->flag | KFREE_FLAG_ON | KFREE_FLAG_ON_5G;

	}

	phydm_set_power_trim_offset_8814b(dm);

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8814b power trim flag:0x%02x\n",
	       power_trim_info->flag);

	if (power_trim_info->flag & KFREE_FLAG_ON) {
		for (i = 0; i < KFREE_BAND_NUM; i++) {
			for (j = 0; j < MAX_PATH_NUM_8814B; j++) {
				RF_DBG(dm, DBG_RF_MP,
				       "[kfree] 8814b pwr_trim->bb_gain[%d][%d]=0x%X\n",
				       i, j, power_trim_info->bb_gain[i][j]);
			}
		}
	}
}

void phydm_get_tssi_trim_offset_8814b(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 i, j;
	u8 tssi_trim_2g[8] = {0}, tssi_trim_5g[24] = {0};

	odm_efuse_one_byte_read(dm, TSSI_2GM_TXA_14B, &tssi_trim_2g[0], false);
	odm_efuse_one_byte_read(dm, TSSI_2GM_TXB_14B, &tssi_trim_2g[1], false);
	odm_efuse_one_byte_read(dm, TSSI_2GM_TXC_14B, &tssi_trim_2g[2], false);
	odm_efuse_one_byte_read(dm, TSSI_2GM_TXD_14B, &tssi_trim_2g[3], false);
	odm_efuse_one_byte_read(dm, TSSI_2GH_TXA_14B, &tssi_trim_2g[4], false);
	odm_efuse_one_byte_read(dm, TSSI_2GH_TXB_14B, &tssi_trim_2g[5], false);
	odm_efuse_one_byte_read(dm, TSSI_2GH_TXC_14B, &tssi_trim_2g[6], false);
	odm_efuse_one_byte_read(dm, TSSI_2GH_TXD_14B, &tssi_trim_2g[7], false);

	j = 0;
	for (i = 0; i < 8; i++) {
		if (tssi_trim_2g[i] == 0xff)
			j++;
	}

	if (j == 8) {
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8814b 2g tssi trim no PG\n");
	} else {
		power_trim_info->tssi_trim[0][RF_PATH_A] = (s8)tssi_trim_2g[0];
		power_trim_info->tssi_trim[0][RF_PATH_B] = (s8)tssi_trim_2g[1];
		power_trim_info->tssi_trim[0][RF_PATH_C] = (s8)tssi_trim_2g[2];
		power_trim_info->tssi_trim[0][RF_PATH_D] = (s8)tssi_trim_2g[3];
		power_trim_info->tssi_trim[1][RF_PATH_A] = (s8)tssi_trim_2g[0];
		power_trim_info->tssi_trim[1][RF_PATH_B] = (s8)tssi_trim_2g[1];
		power_trim_info->tssi_trim[1][RF_PATH_C] = (s8)tssi_trim_2g[2];
		power_trim_info->tssi_trim[1][RF_PATH_D] = (s8)tssi_trim_2g[3];
		power_trim_info->tssi_trim[2][RF_PATH_A] = (s8)tssi_trim_2g[4];
		power_trim_info->tssi_trim[2][RF_PATH_B] = (s8)tssi_trim_2g[5];
		power_trim_info->tssi_trim[2][RF_PATH_C] = (s8)tssi_trim_2g[6];
		power_trim_info->tssi_trim[2][RF_PATH_D] = (s8)tssi_trim_2g[7];

		power_trim_info->flag =
			power_trim_info->flag | TSSI_TRIM_FLAG_ON | KFREE_FLAG_ON_2G;

		for (i = 0; i < KFREE_BAND_NUM; i++) {
			for (j = 0; j < MAX_PATH_NUM_8814B; j++) {
				RF_DBG(dm, DBG_RF_MP,
				       "[kfree] 8814b 2g tssi_trim[%d][%d]=0x%X\n",
				       i, j, power_trim_info->tssi_trim[i][j]);
			}
		}
	}

	odm_efuse_one_byte_read(dm, TSSI_5GL1_TXA_14B, &tssi_trim_5g[0], false);
	odm_efuse_one_byte_read(dm, TSSI_5GL1_TXB_14B, &tssi_trim_5g[1], false);
	odm_efuse_one_byte_read(dm, TSSI_5GL1_TXC_14B, &tssi_trim_5g[2], false);
	odm_efuse_one_byte_read(dm, TSSI_5GL1_TXD_14B, &tssi_trim_5g[3], false);
	odm_efuse_one_byte_read(dm, TSSI_5GL2_TXA_14B, &tssi_trim_5g[4], false);
	odm_efuse_one_byte_read(dm, TSSI_5GL2_TXB_14B, &tssi_trim_5g[5], false);
	odm_efuse_one_byte_read(dm, TSSI_5GL2_TXC_14B, &tssi_trim_5g[6], false);
	odm_efuse_one_byte_read(dm, TSSI_5GL2_TXD_14B, &tssi_trim_5g[7], false);
	odm_efuse_one_byte_read(dm, TSSI_5GM1_TXA_14B, &tssi_trim_5g[8], false);
	odm_efuse_one_byte_read(dm, TSSI_5GM1_TXB_14B, &tssi_trim_5g[9], false);
	odm_efuse_one_byte_read(dm, TSSI_5GM1_TXC_14B, &tssi_trim_5g[10], false);
	odm_efuse_one_byte_read(dm, TSSI_5GM1_TXD_14B, &tssi_trim_5g[11], false);
	odm_efuse_one_byte_read(dm, TSSI_5GM2_TXA_14B, &tssi_trim_5g[12], false);
	odm_efuse_one_byte_read(dm, TSSI_5GM2_TXB_14B, &tssi_trim_5g[13], false);
	odm_efuse_one_byte_read(dm, TSSI_5GM2_TXC_14B, &tssi_trim_5g[14], false);
	odm_efuse_one_byte_read(dm, TSSI_5GM2_TXD_14B, &tssi_trim_5g[15], false);
	odm_efuse_one_byte_read(dm, TSSI_5GH1_TXA_14B, &tssi_trim_5g[16], false);
	odm_efuse_one_byte_read(dm, TSSI_5GH1_TXB_14B, &tssi_trim_5g[17], false);
	odm_efuse_one_byte_read(dm, TSSI_5GH1_TXC_14B, &tssi_trim_5g[18], false);
	odm_efuse_one_byte_read(dm, TSSI_5GH1_TXD_14B, &tssi_trim_5g[19], false);
	odm_efuse_one_byte_read(dm, TSSI_5GH2_TXA_14B, &tssi_trim_5g[20], false);
	odm_efuse_one_byte_read(dm, TSSI_5GH2_TXB_14B, &tssi_trim_5g[21], false);
	odm_efuse_one_byte_read(dm, TSSI_5GH2_TXC_14B, &tssi_trim_5g[22], false);
	odm_efuse_one_byte_read(dm, TSSI_5GH2_TXD_14B, &tssi_trim_5g[23], false);

	j = 0;
	for (i = 0; i < 24; i++) {
		if (tssi_trim_5g[i] == 0xff)
			j++;
	}
	
	if (j == 24) {
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8814b 5g tssi trim no PG\n");
	} else {
		power_trim_info->tssi_trim[3][RF_PATH_A] = (s8)tssi_trim_5g[0];
		power_trim_info->tssi_trim[3][RF_PATH_B] = (s8)tssi_trim_5g[1];
		power_trim_info->tssi_trim[3][RF_PATH_C] = (s8)tssi_trim_5g[2];
		power_trim_info->tssi_trim[3][RF_PATH_D] = (s8)tssi_trim_5g[3];
		power_trim_info->tssi_trim[4][RF_PATH_A] = (s8)tssi_trim_5g[4];
		power_trim_info->tssi_trim[4][RF_PATH_B] = (s8)tssi_trim_5g[5];
		power_trim_info->tssi_trim[4][RF_PATH_C] = (s8)tssi_trim_5g[6];
		power_trim_info->tssi_trim[4][RF_PATH_D] = (s8)tssi_trim_5g[7];
		power_trim_info->tssi_trim[5][RF_PATH_A] = (s8)tssi_trim_5g[8];
		power_trim_info->tssi_trim[5][RF_PATH_B] = (s8)tssi_trim_5g[9];
		power_trim_info->tssi_trim[5][RF_PATH_C] = (s8)tssi_trim_5g[10];
		power_trim_info->tssi_trim[5][RF_PATH_D] = (s8)tssi_trim_5g[11];
		power_trim_info->tssi_trim[6][RF_PATH_A] = (s8)tssi_trim_5g[12];
		power_trim_info->tssi_trim[6][RF_PATH_B] = (s8)tssi_trim_5g[13];
		power_trim_info->tssi_trim[6][RF_PATH_C] = (s8)tssi_trim_5g[14];
		power_trim_info->tssi_trim[6][RF_PATH_D] = (s8)tssi_trim_5g[15];
		power_trim_info->tssi_trim[7][RF_PATH_A] = (s8)tssi_trim_5g[16];
		power_trim_info->tssi_trim[7][RF_PATH_B] = (s8)tssi_trim_5g[17];
		power_trim_info->tssi_trim[7][RF_PATH_C] = (s8)tssi_trim_5g[18];
		power_trim_info->tssi_trim[7][RF_PATH_D] = (s8)tssi_trim_5g[19];
		power_trim_info->tssi_trim[8][RF_PATH_A] = (s8)tssi_trim_5g[20];
		power_trim_info->tssi_trim[8][RF_PATH_B] = (s8)tssi_trim_5g[21];
		power_trim_info->tssi_trim[8][RF_PATH_C] = (s8)tssi_trim_5g[22];
		power_trim_info->tssi_trim[8][RF_PATH_D] = (s8)tssi_trim_5g[23];

		power_trim_info->flag =
			power_trim_info->flag | TSSI_TRIM_FLAG_ON | KFREE_FLAG_ON_5G;

		for (i = 0; i < KFREE_BAND_NUM; i++) {
			for (j = 0; j < MAX_PATH_NUM_8814B; j++) {
				RF_DBG(dm, DBG_RF_MP,
				       "[kfree] 8814b 5g tssi_trim[%d][%d]=0x%X\n",
				       i, j, power_trim_info->tssi_trim[i][j]);
			}
		}
	}
}

s8 phydm_get_tssi_trim_de_8814b(void *dm_void, u8 path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 channel = *dm->channel, group = 0;

	if (channel >= 1 && channel <= 3)
		group = 0;
	else if (channel >= 4 && channel <= 9)
		group = 1;
	else if (channel >= 10 && channel <= 14)
		group = 2;
	else if (channel >= 36 && channel <= 50)
		group = 3;
	else if (channel >= 52 && channel <= 64)
		group = 4;
	else if (channel >= 100 && channel <= 118)
		group = 5;
	else if (channel >= 120 && channel <= 144)
		group = 6;
	else if (channel >= 149 && channel <= 165)
		group = 7;
	else if (channel >= 167 && channel <= 177)
		group = 8;
	else {
		RF_DBG(dm, DBG_RF_MP, "[kfree] Channel(%d) is not exist in Group\n",
			channel);
		return 0;
	}

	return power_trim_info->tssi_trim[group][path];
}

void phydm_set_pabias_bandedge_2g_rf_8814b(void *dm_void)
{
#if 0
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u32 rf_reg_51 = 0, rf_reg_52 = 0, rf_reg_53 = 0, rf_reg_3f = 0;
	u8 i, j;
	s32 pa_bias_tmp, bandedge_tmp, reg_tmp;

#if 0
	/*2.4G bias*/
	/*rf3f == rf53*/
#endif
	for (i = 0; i < MAX_PATH_NUM_8814B; i++) {
		rf_reg_51 = odm_get_rf_reg(dm, i, RF_0x51, RFREGOFFSETMASK);
		rf_reg_52 = odm_get_rf_reg(dm, i, RF_0x52, RFREGOFFSETMASK);
		rf_reg_53 = odm_get_rf_reg(dm, i, RF_0x53, RFREGOFFSETMASK);

		RF_DBG(dm, DBG_RF_MP,
		       "[kfree] 8814b 2g rf(0x51)=0x%X rf(0x52)=0x%X rf(0x53)=0x%X path=%d\n",
		       rf_reg_51, rf_reg_52, rf_reg_53, i);

		/*2.4G bias*/
		rf_reg_3f = rf_reg_53;
		pa_bias_tmp = rf_reg_3f & 0xf;

		reg_tmp = pa_bias_tmp + power_trim_info->pa_bias_trim[0][i];

		RF_DBG(dm, DBG_RF_MP,
		       "[kfree] 8814b 2g pa bias reg_tmp(%d) = pa_bias_tmp(%d) + power_trim_info->pa_bias_trim[0][%d](%d)\n",
		       reg_tmp, pa_bias_tmp, i, power_trim_info->pa_bias_trim[0][i]);

#if 0
		if (reg_tmp < 0) {
			reg_tmp = 0;
			RF_DBG(dm, DBG_RF_MP,
			       "[kfree] 2g pa bias reg_tmp < 0. Set 0 path=%d\n", i);
		} else if (reg_tmp > 7) {
			reg_tmp = 7;
			RF_DBG(dm, DBG_RF_MP,
			       "[kfree] 2g pa bias reg_tmp > 7. Set 7 path=%d\n", i);
		}
#endif

		rf_reg_3f = ((rf_reg_3f & 0xffff0) | reg_tmp);
		rf_reg_3f = ((rf_reg_3f & 0x0ffff) | 0x10000);

		odm_set_rf_reg(dm, i, RF_0xef, BIT(10), 0x1);
		for (j = 0; j <= 0xf; j++) {
			odm_set_rf_reg(dm, i, RF_0x30, RFREGOFFSETMASK, (j << 16));
			odm_set_rf_reg(dm, i, RF_0x3f, RFREGOFFSETMASK, rf_reg_3f);
			RF_DBG(dm, DBG_RF_MP,
			       "[kfree] 8814b 2G pa bias write RF_0x30=0x%05x  RF_0x3f=0x%x path=%d\n",
			       (j << 16), rf_reg_3f, i);
		}
		odm_set_rf_reg(dm, i, RF_0xef, BIT(10), 0x0);

#if 0
		/*2.4G bandedge*/
		/*rf3f =>*/
		/*rf51[3:1] = rf3f[17:15]*/
		/*rf52[2:0] = rf3f[14:12]*/
		/*rf52[18] = rf3f[11]*/
		/*rf51[6:4] = rf3f[10:8]*/
		/*rf51[11:8] = rf3f[7:4]*/
		/*rf51[16:13] = rf3f[3:0]*/
#endif
		/*2.4G bandedge*/
		rf_reg_3f = (((rf_reg_51 & 0xe) >> 1) << 15) |
			    ((rf_reg_52 & 0x7) << 12) |
			    (((rf_reg_52 & 0x40000) >> 18) << 11) |
			    (((rf_reg_51 & 0x70) >> 4) << 8) |
			    (((rf_reg_51 & 0xf00) >> 8) << 4) |
			    ((rf_reg_51 & 0x1e000) >> 13);

		bandedge_tmp = rf_reg_3f & 0xf;

		reg_tmp = bandedge_tmp + power_trim_info->pa_bias_trim[0][i];

		RF_DBG(dm, DBG_RF_MP,
		       "[kfree] 8814b 2g bandedge reg_tmp(%d) = bandedge_tmp(%d) + power_trim_info->pa_bias_trim[0][%d](%d)\n",
		       reg_tmp, bandedge_tmp, i, power_trim_info->pa_bias_trim[0][i]);

#if 0
		if (reg_tmp < 0) {
			reg_tmp = 0;
			RF_DBG(dm, DBG_RF_MP,
			       "[kfree] 2g bandedge reg_tmp < 0. Set 0 path=%d\n", i);
		} else if (reg_tmp > 7) {
			reg_tmp = 7;
			RF_DBG(dm, DBG_RF_MP,
			       "[kfree] 2g bandedge reg_tmp > 7. Set 7 path=%d\n", i);
		}
#endif

		rf_reg_3f = ((rf_reg_3f & 0xffff0) | reg_tmp);

		RF_DBG(dm, DBG_RF_MP,
		       "[kfree] 8814b 2G bandedge RF_0x30=0x%05X  RF_0x3f=0x%x path=%d\n",
		       0x00001, rf_reg_3f, i);
		RF_DBG(dm, DBG_RF_MP,
		       "[kfree] 8814b 2G bandedge RF_0x30=0x%05X  RF_0x3f=0x%x path=%d\n",
		       0x0000b, rf_reg_3f, i);
		RF_DBG(dm, DBG_RF_MP,
		       "[kfree] 8814b 2G bandedge RF_0x30=0x%05X  RF_0x3f=0x%x path=%d\n",
		       0x00023, rf_reg_3f, i);
		RF_DBG(dm, DBG_RF_MP,
		       "[kfree] 8814b 2G bandedge RF_0x30=0x%05X  RF_0x3f=0x%x path=%d\n",
		       0x00029, rf_reg_3f, i);
		RF_DBG(dm, DBG_RF_MP,
		       "[kfree] 8814b 2G bandedge RF_0x30=0x%05X  RF_0x3f=0x%x path=%d\n",
		       0x0002a, rf_reg_3f, i);

		odm_set_rf_reg(dm, i, RF_0xef, BIT(8), 0x1);
		odm_set_rf_reg(dm, i, RF_0x33, RFREGOFFSETMASK, 0x00001);
		odm_set_rf_reg(dm, i, RF_0x3f, RFREGOFFSETMASK, rf_reg_3f);
		odm_set_rf_reg(dm, i, RF_0x33, RFREGOFFSETMASK, 0x0000b);
		odm_set_rf_reg(dm, i, RF_0x3f, RFREGOFFSETMASK, rf_reg_3f);
		odm_set_rf_reg(dm, i, RF_0x33, RFREGOFFSETMASK, 0x00023);
		odm_set_rf_reg(dm, i, RF_0x3f, RFREGOFFSETMASK, rf_reg_3f);
		odm_set_rf_reg(dm, i, RF_0x33, RFREGOFFSETMASK, 0x00029);
		odm_set_rf_reg(dm, i, RF_0x3f, RFREGOFFSETMASK, rf_reg_3f);
		odm_set_rf_reg(dm, i, RF_0x33, RFREGOFFSETMASK, 0x0002a);
		odm_set_rf_reg(dm, i, RF_0x3f, RFREGOFFSETMASK, rf_reg_3f);
		odm_set_rf_reg(dm, i, RF_0xef, BIT(8), 0x0);

	}
#endif
}

void phydm_set_pabias_bandedge_5g_rf_8814b(void *dm_void)
{
#if 0
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u32 rf_reg_18[MAX_PATH_NUM_8814B] = {0},
		rf_reg_61[15][MAX_PATH_NUM_8814B] = {0},
		rf_reg_62[3][MAX_PATH_NUM_8814B] = {0};
	u8 i, j;
	u32 bandedge[15][MAX_PATH_NUM_8814B] = {0},
		pa_bias[3][MAX_PATH_NUM_8814B] = {0};
		
	s32 pa_bias_tmp, reg_tmp;


	for (i = 0; i < MAX_PATH_NUM_8814B; i++) {
		rf_reg_18[i] = odm_get_rf_reg(dm, i, RF_0x18, RFREGOFFSETMASK);

		for (j = 0; j < 3; j++) {
			if (j == 0)
				odm_set_rf_reg(dm, i, RF_0x18, RFREGOFFSETMASK, 0x10d24);
			else if (j == 1)
				odm_set_rf_reg(dm, i, RF_0x18, RFREGOFFSETMASK, 0x30d64);
			else if (j == 2)
				odm_set_rf_reg(dm, i, RF_0x18, RFREGOFFSETMASK, 0x50da9);

			rf_reg_62[j][i] = odm_get_rf_reg(dm, i, 0x62, RFREGOFFSETMASK);

#if 0
			/*5G bias*/
			/*rf62[19:16] == rf30[11:8]*/
			/*rf62[15:12] == rf30[7:4]*/
			/*rf62[11:8] == rf3030[3:0]*/
#endif
			pa_bias[j][i] = (((rf_reg_62[j][i] & 0xf0000) >> 16) << 8) |
					(((rf_reg_62[j][i] & 0xf000) >> 12) << 4) |
					((rf_reg_62[j][i] & 0xf00) >> 8);
		}

		for (j = 0; j < 15; j++) {
			if (j == 0)
				odm_set_rf_reg(dm, i, RF_0x18, RFREGOFFSETMASK, 0x10d24);/*ch36*/
			else if (j == 1)
				odm_set_rf_reg(dm, i, RF_0x18, RFREGOFFSETMASK, 0x11926);/*ch38*/
			else if (j == 2)
				odm_set_rf_reg(dm, i, RF_0x18, RFREGOFFSETMASK, 0x1252a);/*ch42*/
			else if (j == 3)
				odm_set_rf_reg(dm, i, RF_0x18, RFREGOFFSETMASK, 0x1253a);/*ch58*/
			else if (j == 4)
				odm_set_rf_reg(dm, i, RF_0x18, RFREGOFFSETMASK, 0x1193e);/*ch62*/
			else if (j == 5)
				odm_set_rf_reg(dm, i, RF_0x18, RFREGOFFSETMASK, 0x10d40);/*ch64*/
			else if (j == 6)
				odm_set_rf_reg(dm, i, RF_0x18, RFREGOFFSETMASK, 0x30d64);/*ch100*/
			else if (j == 7)
				odm_set_rf_reg(dm, i, RF_0x18, RFREGOFFSETMASK, 0x31966);/*ch102*/
			else if (j == 8)
				odm_set_rf_reg(dm, i, RF_0x18, RFREGOFFSETMASK, 0x3256a);/*ch106*/
			else if (j == 9)
				odm_set_rf_reg(dm, i, RF_0x18, RFREGOFFSETMASK, 0x3257a);/*ch122*/
			else if (j == 10)
				odm_set_rf_reg(dm, i, RF_0x18, RFREGOFFSETMASK, 0x31986);/*ch134*/
			else if (j == 11)
				odm_set_rf_reg(dm, i, RF_0x18, RFREGOFFSETMASK, 0x30d8c);/*ch140*/
			else if (j == 12)
				odm_set_rf_reg(dm, i, RF_0x18, RFREGOFFSETMASK, 0x50d95);/*ch149*/
			else if (j == 13)
				odm_set_rf_reg(dm, i, RF_0x18, RFREGOFFSETMASK, 0x51997);/*ch151*/
			else if (j == 14)
				odm_set_rf_reg(dm, i, RF_0x18, RFREGOFFSETMASK, 0x5259b);/*ch155*/
				

			rf_reg_61[j][i] = odm_get_rf_reg(dm, i, RF_0x61, RFREGOFFSETMASK);
#if 0
			/*5G bandedge*/
			/*rf61[11:8] == rf30[11:8]*/
			/*rf61[7:4] == rf30[7:4]*/
			/*rf61[3:0] == rf3030[3:0]*/
#endif
			bandedge[j][i] = rf_reg_61[j][i] & 0xfff;
		}

		odm_set_rf_reg(dm, i, RF_0x18, RFREGOFFSETMASK, rf_reg_18[i]);
	}

	for (i = 0; i < MAX_PATH_NUM_8814B; i++) {
		for (j = 0; j < 3; j++) {
			RF_DBG(dm, DBG_RF_MP,
			       "[kfree] pa_bias[%d][%d]=0x%x\n", j, i, pa_bias[j][i]);
		}
	}

	for (i = 0; i < MAX_PATH_NUM_8814B; i++) {
		for (j = 0; j < 15; j++) {
			RF_DBG(dm, DBG_RF_MP,
			       "[kfree] bandedge[%d][%d]=0x%x\n", j, i, bandedge[j][i]);
		}
	}

	/*5G bias*/
	for (i = 0; i < MAX_PATH_NUM_8814B; i++) {
		odm_set_rf_reg(dm, i, RF_0xee, BIT(8), 0x1);
		for (j = 0; j <= 0xb; j++) {
			
			if (j >= 0 && j <= 3)
				pa_bias_tmp = pa_bias[0][i] & 0xf;
			else if (j >= 4 && j <= 0x7)
				pa_bias_tmp = pa_bias[1][i] & 0xf;
			else if (j >= 0x8 && j <= 0xb)
				pa_bias_tmp = pa_bias[2][i] & 0xf;

			reg_tmp = pa_bias_tmp + power_trim_info->pa_bias_trim[1][i];

			RF_DBG(dm, DBG_RF_MP,
			       "[kfree] 8814b 5g pa bias reg_tmp(%d) = pa_bias_tmp(%d) + power_trim_info->pa_bias_trim[1][%d](%d)\n",
			       reg_tmp, pa_bias_tmp, i, power_trim_info->pa_bias_trim[1][i]);
#if 0
			if (reg_tmp < 0) {
				reg_tmp = 0;
				RF_DBG(dm, DBG_RF_MP,
				       "[kfree] 5g pa bias reg_tmp < 0. Set 0 path=%d\n", i);
			} else if (reg_tmp > 7) {
				reg_tmp = 7;
				RF_DBG(dm, DBG_RF_MP,
				       "[kfree] 5g pa bias reg_tmp > 7. Set 7 path=%d\n", i);
			}
#endif
			if (j >= 0 && j <= 3)
				reg_tmp = ((pa_bias[0][i] & 0xffff0) | reg_tmp | (j << 12));
			else if (j >= 4 && j <= 0x7)
				reg_tmp = ((pa_bias[1][i] & 0xffff0) | reg_tmp | (j << 12));
			else if (j >= 0x8 && j <= 0xb)
				reg_tmp = ((pa_bias[2][i] & 0xffff0) | reg_tmp | (j << 12));

			RF_DBG(dm, DBG_RF_MP,
				       "[kfree] 8814b write RF_0x30=0x%05x path=%d\n",
				       reg_tmp, i);

			odm_set_rf_reg(dm, i, RF_0x30, RFREGOFFSETMASK, reg_tmp);
		}
		odm_set_rf_reg(dm, i, RF_0xee, BIT(8), 0x0);
	}

	/*5G bandedge*/
	for (i = 0; i < MAX_PATH_NUM_8814B; i++) {
		odm_set_rf_reg(dm, i, RF_0xee, BIT(9), 0x1);
		for (j = 0; j <= 0xe; j++) {
			reg_tmp = bandedge[j][i] + power_trim_info->pa_bias_trim[1][i];

			RF_DBG(dm, DBG_RF_MP,
			       "[kfree] 8814b 5g bandedge reg_tmp(%d)(0x%X) = bandedge_org(%d) + power_trim_info->pa_bias_trim[1][%d](%d)\n",
			       reg_tmp, reg_tmp, bandedge[j][i], i, power_trim_info->pa_bias_trim[1][i]);
#if 0
			if (reg_tmp < 0) {
				reg_tmp = 0;
				RF_DBG(dm, DBG_RF_MP,
				       "[kfree] 5g bandedge reg_tmp < 0. Set 0 path=%d\n", i);
			} else if (reg_tmp > 7) {
				reg_tmp = 7;
				RF_DBG(dm, DBG_RF_MP,
				       "[kfree] 5g bandedge reg_tmp > 7. Set 7 path=%d\n", i);
			}
#endif

			reg_tmp = ((bandedge[j][i] & 0xffff0) | reg_tmp | (j << 12));

			RF_DBG(dm, DBG_RF_MP,
				       "[kfree] 8814b write RF_0x30=0x%05x path=%d\n",
				       reg_tmp, i);

			odm_set_rf_reg(dm, i, RF_0x30, RFREGOFFSETMASK, reg_tmp);
		}
		odm_set_rf_reg(dm, i, RF_0xee, BIT(9), 0x0);
	}

#endif
}


void phydm_get_pa_bias_offset_8814b(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 i, j, k;
	u8 tssi_pa_bias_2g[2] = {0}, tssi_pa_bias_5g[2] = {0};

	odm_efuse_one_byte_read(dm, PPG_PABIAS_2GAC_14B, &tssi_pa_bias_2g[0], false);
	odm_efuse_one_byte_read(dm, PPG_PABIAS_2GBD_14B, &tssi_pa_bias_2g[1], false);

	j = 0;
	for (i = 0; i < 2; i++) {
		if (tssi_pa_bias_2g[i] == 0xff)
			j++;
	}

	if (j == 2) {
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8814b 2g PA Bias K no PG\n");
	} else {
		power_trim_info->pa_bias_trim[0][RF_PATH_A] = tssi_pa_bias_2g[0] & 0xf;
		power_trim_info->pa_bias_trim[0][RF_PATH_C] = (tssi_pa_bias_2g[0] & 0xf0) >> 4;
		power_trim_info->pa_bias_trim[0][RF_PATH_B] = tssi_pa_bias_2g[1] & 0xf;
		power_trim_info->pa_bias_trim[0][RF_PATH_D] = (tssi_pa_bias_2g[1] & 0xf0) >> 4;

		for (k = 0; k < MAX_PATH_NUM_8814B; k++) {
			RF_DBG(dm, DBG_RF_MP, "[kfree] 8814b 2g PA Bias K efuse:0x%x path=%d\n",
				power_trim_info->pa_bias_trim[0][k], k);
			odm_set_rf_reg(dm, k, 0x60, 0x0000f000, power_trim_info->pa_bias_trim[0][k]);
		}

#if 0
		for (k = 0; k < MAX_PATH_NUM_8814B; k++) {
			if ((power_trim_info->pa_bias_trim[0][k] & BIT(0)) == 0)
				power_trim_info->pa_bias_trim[0][k] = (-1 * (power_trim_info->pa_bias_trim[0][k] >> 1));
			else
				power_trim_info->pa_bias_trim[0][k] = (power_trim_info->pa_bias_trim[0][k] >> 1);

			RF_DBG(dm, DBG_RF_MP, "[kfree] 8814b 2g PA Bias K power_trim_info->pa_bias_trim[0][%d]=0x%x\n",
				k, power_trim_info->pa_bias_trim[0][k]);
		}

		phydm_set_pabias_bandedge_2g_rf_8814b(dm);
#endif	
	}
	
	odm_efuse_one_byte_read(dm, PPG_PABIAS_5GAC_14B, &tssi_pa_bias_5g[0], false);
	odm_efuse_one_byte_read(dm, PPG_PABIAS_5GBD_14B, &tssi_pa_bias_5g[1], false);

	j = 0;
	for (i = 0; i < 2; i++) {
		if (tssi_pa_bias_5g[i] == 0xff)
			j++;
	}

	if (j == 2) {
		RF_DBG(dm, DBG_RF_MP, "[kfree] 8814b 5g PA Bias K no PG\n");
	} else {
		power_trim_info->pa_bias_trim[1][RF_PATH_A] = tssi_pa_bias_5g[0] & 0xf;
		power_trim_info->pa_bias_trim[1][RF_PATH_C] = (tssi_pa_bias_5g[0] & 0xf0) >> 4;
		power_trim_info->pa_bias_trim[1][RF_PATH_B] = tssi_pa_bias_5g[1] & 0xf;
		power_trim_info->pa_bias_trim[1][RF_PATH_D] = (tssi_pa_bias_5g[1] & 0xf0) >> 4;

		for (k = 0; k < MAX_PATH_NUM_8814B; k++) {
			RF_DBG(dm, DBG_RF_MP, "[kfree] 8814b 5g PA Bias K efuse:0x%x path=%d\n",
				power_trim_info->pa_bias_trim[1][k], k);

			odm_set_rf_reg(dm, k, 0x60, 0x000f0000, power_trim_info->pa_bias_trim[1][k]);
		}
#if 0
		for (k = 0; k < MAX_PATH_NUM_8814B; k++) {
			if ((power_trim_info->pa_bias_trim[1][k] & BIT(0)) == 0)
				power_trim_info->pa_bias_trim[1][k] = (-1 * (power_trim_info->pa_bias_trim[1][k] >> 1));
			else
				power_trim_info->pa_bias_trim[1][k] = (power_trim_info->pa_bias_trim[1][k] >> 1);

			RF_DBG(dm, DBG_RF_MP, "[kfree] 8814b 5g PA Bias K power_trim_info->pa_bias_trim[1][%d]=0x%x\n",
				k, power_trim_info->pa_bias_trim[1][k]);
 		}

		phydm_set_pabias_bandedge_5g_rf_8814b(dm);
#endif
	}

	
}

void phydm_get_thermal_trim_offset_8814b(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	u8 pg_therm = 0xff, i;

	odm_efuse_one_byte_read(dm, PPG_THERMAL_A_OFFSET_14B, &pg_therm, false);

	if (pg_therm != 0xff) {
		for (i = 0; i < MAX_PATH_NUM_8814B; i++) {
			if (i == 0)
				odm_efuse_one_byte_read(dm, PPG_THERMAL_A_OFFSET_14B, &pg_therm, false);
			else if (i == 1)
				odm_efuse_one_byte_read(dm, PPG_THERMAL_B_OFFSET_14B, &pg_therm, false);
			else if (i == 2)
				odm_efuse_one_byte_read(dm, PPG_THERMAL_C_OFFSET_14B, &pg_therm, false);
			else if (i == 3)
				odm_efuse_one_byte_read(dm, PPG_THERMAL_D_OFFSET_14B, &pg_therm, false);

			RF_DBG(dm, DBG_RF_MP, "[kfree] 8814b Efuse thermal S%d:0x%x\n", i, pg_therm);
			pg_therm = pg_therm & 0x1f;
			if ((pg_therm & BIT(0)) == 0)
				power_trim_info->multi_thermal[i] = (-1 * (pg_therm >> 1));
			else
				power_trim_info->multi_thermal[i] = (pg_therm >> 1);
		}

		power_trim_info->flag |= KFREE_FLAG_THERMAL_K_ON;
	}

	RF_DBG(dm, DBG_RF_MP, "[kfree] 8814b thermal trim flag:0x%02x\n",
	       power_trim_info->flag);

	for (i = 0; i < MAX_RF_PATH; i++) {
		if (power_trim_info->flag & KFREE_FLAG_THERMAL_K_ON)
			RF_DBG(dm, DBG_RF_MP, "[kfree] 8814b thermal S%d:%d\n",
			       i ,power_trim_info->multi_thermal[i]);
	}
}

s8 phydm_get_tssi_trim_de(void *dm_void, u8 path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_RTL8822C)
		return phydm_get_tssi_trim_de_8822c(dm, path);
	else if (dm->support_ic_type & ODM_RTL8812F)
		return phydm_get_tssi_trim_de_8812f(dm, path);
	else if (dm->support_ic_type & ODM_RTL8197G)
		return phydm_get_tssi_trim_de_8197g(dm, path);
	else if (dm->support_ic_type & ODM_RTL8814B)
		return phydm_get_tssi_trim_de_8814b(dm, path);
	else
		return 0;	
}

void phydm_do_new_kfree(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_RTL8822C) {
		phydm_get_set_thermal_trim_offset_8822c(dm);
		phydm_get_set_power_trim_offset_8822c(dm);
		phydm_get_set_pa_bias_offset_8822c(dm);
		phydm_get_tssi_trim_offset_8822c(dm);
	}

	if (dm->support_ic_type & ODM_RTL8812F) {
		phydm_get_set_thermal_trim_offset_8812f(dm);
		phydm_get_set_power_trim_offset_8812f(dm);
		phydm_get_set_pa_bias_offset_8812f(dm);
		phydm_get_tssi_trim_offset_8812f(dm);
	}

	if (dm->support_ic_type & ODM_RTL8195B) {
		phydm_get_thermal_trim_offset_8195b(dm);
		phydm_get_set_power_trim_offset_8195b(dm);
		phydm_get_set_pa_bias_offset_8195b(dm);
	}

	if (dm->support_ic_type & ODM_RTL8721D) {
		phydm_get_thermal_trim_offset_8721d(dm);
		phydm_get_set_power_trim_offset_8721d(dm);
		/*phydm_get_set_pa_bias_offset_8721d(dm);*/
	}

	if (dm->support_ic_type & ODM_RTL8198F) {
		phydm_get_pa_bias_offset_8198f(dm);
		phydm_get_set_lna_offset_8198f(dm);
	}

	if (dm->support_ic_type & ODM_RTL8197G) {
		phydm_get_thermal_trim_offset_8197g(dm);
		phydm_get_set_power_trim_offset_8197g(dm);
		phydm_get_set_pa_bias_offset_8197g(dm);
		phydm_get_tssi_trim_offset_8197g(dm);
		phydm_get_set_lna_offset_8197g(dm);
	}

	if (dm->support_ic_type & ODM_RTL8710C) {
		phydm_get_thermal_trim_offset_8710c(dm);
		phydm_get_set_power_trim_offset_8710c(dm);
		phydm_get_set_pa_bias_offset_8710c(dm);
	}

	if (dm->support_ic_type & ODM_RTL8814B) {
		phydm_get_thermal_trim_offset_8814b(dm);
		phydm_get_set_power_trim_offset_8814b(dm);
		phydm_get_pa_bias_offset_8814b(dm);
		phydm_get_tssi_trim_offset_8814b(dm);
	}
}

void phydm_set_kfree_to_rf(void *dm_void, u8 e_rf_path, u8 data)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_RTL8814A)
		phydm_set_kfree_to_rf_8814a(dm, e_rf_path, data);

	if ((dm->support_ic_type & ODM_RTL8821C) &&
	    (*dm->band_type == ODM_BAND_2_4G))
		phydm_set_kfree_to_rf_8821c(dm, e_rf_path, true, data);
	else if (dm->support_ic_type & ODM_RTL8821C)
		phydm_set_kfree_to_rf_8821c(dm, e_rf_path, false, data);

	if (dm->support_ic_type & ODM_RTL8822B)
		phydm_set_kfree_to_rf_8822b(dm, e_rf_path, data);

	if (dm->support_ic_type & ODM_RTL8710B)
		phydm_set_kfree_to_rf_8710b(dm, e_rf_path, data);

	if (dm->support_ic_type & ODM_RTL8198F)
		phydm_set_kfree_to_rf_8198f(dm, e_rf_path, data);
}

void phydm_clear_kfree_to_rf(void *dm_void, u8 e_rf_path, u8 data)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_RTL8822B)
		phydm_clear_kfree_to_rf_8822b(dm, e_rf_path, 1);

	if (dm->support_ic_type & ODM_RTL8821C)
		phydm_clear_kfree_to_rf_8821c(dm, e_rf_path, 1);

	if (dm->support_ic_type & ODM_RTL8198F)
		phydm_clear_kfree_to_rf_8198f(dm, e_rf_path, 0);
}

void phydm_get_thermal_trim_offset(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	void *adapter = dm->adapter;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	PEFUSE_HAL pEfuseHal = &hal_data->EfuseHal;
	u1Byte eFuseContent[DCMD_EFUSE_MAX_SECTION_NUM * EFUSE_MAX_WORD_UNIT * 2];

	if (HAL_MAC_Dump_EFUSE(&GET_HAL_MAC_INFO((PADAPTER)adapter), EFUSE_WIFI, eFuseContent, pEfuseHal->PhysicalLen_WiFi, HAL_MAC_EFUSE_PHYSICAL, HAL_MAC_EFUSE_PARSE_DRV) != RT_STATUS_SUCCESS)
		RF_DBG(dm, DBG_RF_MP, "[kfree] dump efuse fail !!!\n");
#endif

	if (dm->support_ic_type & ODM_RTL8821C)
		phydm_get_thermal_trim_offset_8821c(dm_void);
	else if (dm->support_ic_type & ODM_RTL8822B)
		phydm_get_thermal_trim_offset_8822b(dm_void);
	else if (dm->support_ic_type & ODM_RTL8710B)
		phydm_get_thermal_trim_offset_8710b(dm_void);
	else if (dm->support_ic_type & ODM_RTL8192F)
		phydm_get_thermal_trim_offset_8192f(dm_void);
	else if (dm->support_ic_type & ODM_RTL8198F)
		phydm_get_thermal_trim_offset_8198f(dm_void);
}

void phydm_get_power_trim_offset(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

#if 0 //(DM_ODM_SUPPORT_TYPE & ODM_WIN)	// 2017 MH DM Should use the same code.s
	void		*adapter = dm->adapter;
	HAL_DATA_TYPE	*hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	PEFUSE_HAL		pEfuseHal = &hal_data->EfuseHal;
	u1Byte			eFuseContent[DCMD_EFUSE_MAX_SECTION_NUM * EFUSE_MAX_WORD_UNIT * 2];

	if (HAL_MAC_Dump_EFUSE(&GET_HAL_MAC_INFO(adapter), EFUSE_WIFI, eFuseContent, pEfuseHal->PhysicalLen_WiFi, HAL_MAC_EFUSE_PHYSICAL, HAL_MAC_EFUSE_PARSE_DRV) != RT_STATUS_SUCCESS)
		RF_DBG(dm, DBG_RF_MP, "[kfree] dump efuse fail !!!\n");
#endif

	if (dm->support_ic_type & ODM_RTL8821C)
		phydm_get_power_trim_offset_8821c(dm_void);
	else if (dm->support_ic_type & ODM_RTL8822B)
		phydm_get_power_trim_offset_8822b(dm_void);
	else if (dm->support_ic_type & ODM_RTL8710B)
		phydm_get_power_trim_offset_8710b(dm_void);
	else if (dm->support_ic_type & ODM_RTL8192F)
		phydm_get_power_trim_offset_8192f(dm_void);
	else if (dm->support_ic_type & ODM_RTL8198F)
		phydm_get_power_trim_offset_8198f(dm_void);
}

void phydm_get_pa_bias_offset(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	void *adapter = dm->adapter;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	PEFUSE_HAL pEfuseHal = &hal_data->EfuseHal;
	u1Byte eFuseContent[DCMD_EFUSE_MAX_SECTION_NUM * EFUSE_MAX_WORD_UNIT * 2];

	if (HAL_MAC_Dump_EFUSE(&GET_HAL_MAC_INFO((PADAPTER)adapter), EFUSE_WIFI, eFuseContent, pEfuseHal->PhysicalLen_WiFi, HAL_MAC_EFUSE_PHYSICAL, HAL_MAC_EFUSE_PARSE_DRV) != RT_STATUS_SUCCESS)
		RF_DBG(dm, DBG_RF_MP, "[kfree] dump efuse fail !!!\n");
#endif

	if (dm->support_ic_type & ODM_RTL8822B)
		phydm_get_pa_bias_offset_8822b(dm_void);
}

s8 phydm_get_thermal_offset(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	if (power_trim_info->flag & KFREE_FLAG_THERMAL_K_ON)
		return power_trim_info->thermal;
	else
		return 0;
}

s8 phydm_get_multi_thermal_offset(void *dm_void, u8 path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;

	if (power_trim_info->flag & KFREE_FLAG_THERMAL_K_ON)
		return power_trim_info->multi_thermal[path];
	else
		return 0;
}

void phydm_do_kfree(void *dm_void, u8 channel_to_sw)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *pwrtrim = &dm->power_trim_data;
	u8 channel_idx = 0, rfpath = 0, max_path = 0, kfree_band_num = 0;
	u8 i, j;
	s8 bb_gain;

	if (dm->support_ic_type & ODM_RTL8814A)
		max_path = 4; /*0~3*/
	else if (dm->support_ic_type &
		 (ODM_RTL8812 | ODM_RTL8822B | ODM_RTL8192F)) {
		max_path = 2; /*0~1*/
		kfree_band_num = KFREE_BAND_NUM;
	} else if (dm->support_ic_type & ODM_RTL8821C) {
		max_path = 1;
		kfree_band_num = KFREE_BAND_NUM;
	} else if (dm->support_ic_type & ODM_RTL8710B) {
		max_path = 1;
		kfree_band_num = 1;
	} else if (dm->support_ic_type & ODM_RTL8198F) {
		max_path = 4;
		kfree_band_num = 3;
	}

	if (dm->support_ic_type &
	    (ODM_RTL8192F | ODM_RTL8822B | ODM_RTL8821C |
	    ODM_RTL8814A | ODM_RTL8710B)) {
		for (i = 0; i < kfree_band_num; i++) {
			for (j = 0; j < max_path; j++)
				RF_DBG(dm, DBG_RF_MP,
				       "[kfree] PwrTrim->gain[%d][%d]=0x%X\n",
				       i, j, pwrtrim->bb_gain[i][j]);
		}
	}
	if (*dm->band_type == ODM_BAND_2_4G &&
	    pwrtrim->flag & KFREE_FLAG_ON_2G) {
		if (!(dm->support_ic_type & ODM_RTL8192F)) {
			if (channel_to_sw >= 1 && channel_to_sw <= 14)
				channel_idx = PHYDM_2G;
			for (rfpath = RF_PATH_A; rfpath < max_path; rfpath++) {
				RF_DBG(dm, DBG_RF_MP,
				       "[kfree] %s:chnl=%d PATH=%d gain:0x%X\n",
				       __func__, channel_to_sw, rfpath,
				       pwrtrim->bb_gain[channel_idx][rfpath]);
				bb_gain = pwrtrim->bb_gain[channel_idx][rfpath];
				phydm_set_kfree_to_rf(dm, rfpath, bb_gain);
			}
		} else if (dm->support_ic_type & ODM_RTL8192F) {
			if (channel_to_sw >= 1 && channel_to_sw <= 3)
				channel_idx = 0;
			if (channel_to_sw >= 4 && channel_to_sw <= 9)
				channel_idx = 1;
			if (channel_to_sw >= 10 && channel_to_sw <= 14)
				channel_idx = 2;
			for (rfpath = RF_PATH_A; rfpath < max_path; rfpath++) {
				RF_DBG(dm, DBG_RF_MP,
				       "[kfree] %s:chnl=%d PATH=%d gain:0x%X\n",
				       __func__, channel_to_sw, rfpath,
				       pwrtrim->bb_gain[channel_idx][rfpath]);
				bb_gain = pwrtrim->bb_gain[channel_idx][rfpath];
				phydm_set_kfree_to_rf_8192f(dm, rfpath,
							    channel_idx,
							    bb_gain);
			}
		}
	} else if (*dm->band_type == ODM_BAND_5G &&
		   pwrtrim->flag & KFREE_FLAG_ON_5G) {
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

		for (rfpath = RF_PATH_A; rfpath < max_path; rfpath++) {
			RF_DBG(dm, DBG_RF_MP,
			       "[kfree] %s: channel=%d PATH=%d bb_gain:0x%X\n",
			       __func__, channel_to_sw, rfpath,
			       pwrtrim->bb_gain[channel_idx][rfpath]);
			bb_gain = pwrtrim->bb_gain[channel_idx][rfpath];
			phydm_set_kfree_to_rf(dm, rfpath, bb_gain);
		}
	} else {
		RF_DBG(dm, DBG_RF_MP, "[kfree] Set default Register\n");
		if (!(dm->support_ic_type & ODM_RTL8192F)) {
			for (rfpath = RF_PATH_A; rfpath < max_path; rfpath++) {
				bb_gain = pwrtrim->bb_gain[channel_idx][rfpath];
				phydm_clear_kfree_to_rf(dm, rfpath, bb_gain);
			}
		}
#if 0
		/*else if(dm->support_ic_type & ODM_RTL8192F){
			if (channel_to_sw >= 1 && channel_to_sw <= 3)
				channel_idx = 0;
			if (channel_to_sw >= 4 && channel_to_sw <= 9)
				channel_idx = 1;
			if (channel_to_sw >= 9 && channel_to_sw <= 14)
				channel_idx = 2;
			for (rfpath = RF_PATH_A;  rfpath < max_path; rfpath++)
				phydm_clear_kfree_to_rf_8192f(dm, rfpath, pwrtrim->bb_gain[channel_idx][rfpath]);
		}*/
#endif
	}
}

void phydm_config_new_kfree(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct dm_rf_calibration_struct *cali_info = &dm->rf_calibrate_info;

	if (cali_info->reg_rf_kfree_enable == 2) {
		RF_DBG(dm, DBG_RF_MP,
		       "[kfree] %s: reg_rf_kfree_enable == 2, Disable\n",
		       __func__);
		return;
	} else if (cali_info->reg_rf_kfree_enable == 1 ||
			cali_info->reg_rf_kfree_enable == 0) {
		RF_DBG(dm, DBG_RF_MP,
		       "[kfree] %s: reg_rf_kfree_enable == true\n", __func__);
	
		phydm_do_new_kfree(dm);
	}
}

void phydm_config_kfree(void *dm_void, u8 channel_to_sw)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct dm_rf_calibration_struct *cali_info = &dm->rf_calibrate_info;
	struct odm_power_trim_data *pwrtrim = &dm->power_trim_data;

	RF_DBG(dm, DBG_RF_MP, "===>[kfree] phy_ConfigKFree()\n");

	if (cali_info->reg_rf_kfree_enable == 2) {
		RF_DBG(dm, DBG_RF_MP,
		       "[kfree] %s: reg_rf_kfree_enable == 2, Disable\n",
		       __func__);
		return;
	} else if (cali_info->reg_rf_kfree_enable == 1 ||
			cali_info->reg_rf_kfree_enable == 0) {
		RF_DBG(dm, DBG_RF_MP,
		       "[kfree] %s: reg_rf_kfree_enable == true\n", __func__);
		/*Make sure the targetval is defined*/
		if (!(pwrtrim->flag & KFREE_FLAG_ON)) {
			RF_DBG(dm, DBG_RF_MP,
			       "[kfree] %s: efuse is 0xff, KFree not work\n",
			       __func__);
			return;
		}
#if 0
		/*if kfree_table[0] == 0xff, means no Kfree*/
#endif
		phydm_do_kfree(dm, channel_to_sw);
	}
	RF_DBG(dm, DBG_RF_MP, "<===[kfree] phy_ConfigKFree()\n");
}

void phydm_set_lna_trim_offset (void *dm_void, u8 path, u8 cg_cs, u8 enable)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_RTL8197G)
		phydm_set_lna_trim_offset_8197g(dm, path, cg_cs, enable);
}

