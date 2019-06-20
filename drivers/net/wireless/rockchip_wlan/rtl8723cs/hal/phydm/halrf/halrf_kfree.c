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

	u8 pg_therm = 0xff;

	odm_efuse_one_byte_read(dm, PPG_THERMAL_OFFSET_98F, &pg_therm, false);

	if (pg_therm != 0xff) {
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

	u8 pg_power = 0xff, i, j;

	odm_efuse_one_byte_read(dm, PPG_2GL_TXAB_98F, &pg_power, false);

	if (pg_power != 0xff) {
		power_trim_info->bb_gain[0][0] = pg_power & 0xf;
		power_trim_info->bb_gain[0][1] = (pg_power & 0xf0) >> 4;

		odm_efuse_one_byte_read(dm, PPG_2GL_TXCD_98F, &pg_power, false);
		power_trim_info->bb_gain[0][2] = pg_power & 0xf;
		power_trim_info->bb_gain[0][3] = (pg_power & 0xf0) >> 4;

		odm_efuse_one_byte_read(dm, PPG_2GM_TXAB_98F, &pg_power, false);
		power_trim_info->bb_gain[1][0] = pg_power & 0xf;
		power_trim_info->bb_gain[1][1] = (pg_power & 0xf0) >> 4;

		odm_efuse_one_byte_read(dm, PPG_2GM_TXCD_98F, &pg_power, false);
		power_trim_info->bb_gain[1][2] = pg_power & 0xf;
		power_trim_info->bb_gain[1][3] = (pg_power & 0xf0) >> 4;

		odm_efuse_one_byte_read(dm, PPG_2GH_TXAB_98F, &pg_power, false);
		power_trim_info->bb_gain[2][0] = pg_power & 0xf;
		power_trim_info->bb_gain[2][1] = (pg_power & 0xf0) >> 4;

		odm_efuse_one_byte_read(dm, PPG_2GH_TXCD_98F, &pg_power, false);
		power_trim_info->bb_gain[2][2] = pg_power & 0xf;
		power_trim_info->bb_gain[2][3] = (pg_power & 0xf0) >> 4;

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

void phydm_set_kfree_to_rf_8198f(void *dm_void, u8 e_rf_path, u8 data)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_power_trim_data *power_trim_info = &dm->power_trim_data;
	u32 band, i;
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

	RF_DBG(dm, DBG_RF_MP,
		   "[kfree] %s:Clear kfree to rf 0x55\n", __func__);
#if 0
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
#else

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

void phydm_get_power_trim_offset_8822c(void *dm_void)
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

void phydm_do_new_kfree(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_RTL8822C) {
		phydm_get_set_thermal_trim_offset_8822c(dm);
		phydm_get_power_trim_offset_8822c(dm);
		phydm_get_set_pa_bias_offset_8822c(dm);
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
