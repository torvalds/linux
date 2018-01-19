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

/*---------------------------Define Local Constant---------------------------*/

static bool _iqk_rx_iqk_by_path_8822b(void *, u8);

static inline void phydm_set_iqk_info(struct phy_dm_struct *dm,
				      struct dm_iqk_info *iqk_info, u8 status)
{
	bool KFAIL = true;

	while (1) {
		KFAIL = _iqk_rx_iqk_by_path_8822b(dm, ODM_RF_PATH_A);
		if (status == 0)
			ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
				     "[IQK]S0RXK KFail = 0x%x\n", KFAIL);
		else if (status == 1)
			ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
				     "[IQK]S1RXK KFail = 0x%x\n", KFAIL);
		if (iqk_info->rxiqk_step == 5) {
			dm->rf_calibrate_info.iqk_step++;
			iqk_info->rxiqk_step = 1;
			if (KFAIL && status == 0)
				ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
					     "[IQK]S0RXK fail code: %d!!!\n",
					     iqk_info->rxiqk_fail_code
						     [0][ODM_RF_PATH_A]);
			else if (KFAIL && status == 1)
				ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
					     "[IQK]S1RXK fail code: %d!!!\n",
					     iqk_info->rxiqk_fail_code
						     [0][ODM_RF_PATH_A]);
			break;
		}
	}

	iqk_info->kcount++;
}

static inline void phydm_init_iqk_information(struct dm_iqk_info *iqk_info)
{
	u8 i, j, k, m;

	for (i = 0; i < 2; i++) {
		iqk_info->iqk_channel[i] = 0x0;

		for (j = 0; j < SS_8822B; j++) {
			iqk_info->lok_idac[i][j] = 0x0;
			iqk_info->rxiqk_agc[i][j] = 0x0;
			iqk_info->bypass_iqk[i][j] = 0x0;

			for (k = 0; k < 2; k++) {
				iqk_info->iqk_fail_report[i][j][k] = true;
				for (m = 0; m < 8; m++) {
					iqk_info->iqk_cfir_real[i][j][k][m] =
						0x0;
					iqk_info->iqk_cfir_imag[i][j][k][m] =
						0x0;
				}
			}

			for (k = 0; k < 3; k++)
				iqk_info->retry_count[i][j][k] = 0x0;
		}
	}
}

static inline void phydm_backup_iqk_information(struct dm_iqk_info *iqk_info)
{
	u8 i, j, k;

	iqk_info->iqk_channel[1] = iqk_info->iqk_channel[0];
	for (i = 0; i < 2; i++) {
		iqk_info->lok_idac[1][i] = iqk_info->lok_idac[0][i];
		iqk_info->rxiqk_agc[1][i] = iqk_info->rxiqk_agc[0][i];
		iqk_info->bypass_iqk[1][i] = iqk_info->bypass_iqk[0][i];
		iqk_info->rxiqk_fail_code[1][i] =
			iqk_info->rxiqk_fail_code[0][i];
		for (j = 0; j < 2; j++) {
			iqk_info->iqk_fail_report[1][i][j] =
				iqk_info->iqk_fail_report[0][i][j];
			for (k = 0; k < 8; k++) {
				iqk_info->iqk_cfir_real[1][i][j][k] =
					iqk_info->iqk_cfir_real[0][i][j][k];
				iqk_info->iqk_cfir_imag[1][i][j][k] =
					iqk_info->iqk_cfir_imag[0][i][j][k];
			}
		}
	}

	for (i = 0; i < 4; i++) {
		iqk_info->rxiqk_fail_code[0][i] = 0x0;
		iqk_info->rxiqk_agc[0][i] = 0x0;
		for (j = 0; j < 2; j++) {
			iqk_info->iqk_fail_report[0][i][j] = true;
			iqk_info->gs_retry_count[0][i][j] = 0x0;
		}
		for (j = 0; j < 3; j++)
			iqk_info->retry_count[0][i][j] = 0x0;
	}
}

static inline void phydm_set_iqk_cfir(struct phy_dm_struct *dm,
				      struct dm_iqk_info *iqk_info, u8 path)
{
	u8 idx, i;
	u32 tmp;

	for (idx = 0; idx < 2; idx++) {
		odm_set_bb_reg(dm, 0x1b00, MASKDWORD, 0xf8000008 | path << 1);

		if (idx == 0)
			odm_set_bb_reg(dm, 0x1b0c, BIT(13) | BIT(12), 0x3);
		else
			odm_set_bb_reg(dm, 0x1b0c, BIT(13) | BIT(12), 0x1);

		odm_set_bb_reg(dm, 0x1bd4,
			       BIT(20) | BIT(19) | BIT(18) | BIT(17) | BIT(16),
			       0x10);

		for (i = 0; i < 8; i++) {
			odm_set_bb_reg(dm, 0x1bd8, MASKDWORD,
				       0xe0000001 + (i * 4));
			tmp = odm_get_bb_reg(dm, 0x1bfc, MASKDWORD);
			iqk_info->iqk_cfir_real[0][path][idx][i] =
				(tmp & 0x0fff0000) >> 16;
			iqk_info->iqk_cfir_imag[0][path][idx][i] = tmp & 0xfff;
		}
	}
}

static inline void phydm_get_read_counter(struct phy_dm_struct *dm)
{
	u32 counter = 0x0;

	while (1) {
		if (((odm_read_4byte(dm, 0x1bf0) >> 24) == 0x7f) ||
		    (counter > 300))
			break;

		counter++;
		ODM_delay_ms(1);
	}

	ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION, "[IQK]counter = %d\n", counter);
}

/*---------------------------Define Local Constant---------------------------*/

void do_iqk_8822b(void *dm_void, u8 delta_thermal_index, u8 thermal_value,
		  u8 threshold)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	odm_reset_iqk_result(dm);

	dm->rf_calibrate_info.thermal_value_iqk = thermal_value;

	phy_iq_calibrate_8822b(dm, true);
}

static void _iqk_fill_iqk_report_8822b(void *dm_void, u8 channel)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	u32 tmp1 = 0x0, tmp2 = 0x0, tmp3 = 0x0;
	u8 i;

	for (i = 0; i < SS_8822B; i++) {
		tmp1 = tmp1 +
		       ((iqk_info->iqk_fail_report[channel][i][TX_IQK] & 0x1)
			<< i);
		tmp2 = tmp2 +
		       ((iqk_info->iqk_fail_report[channel][i][RX_IQK] & 0x1)
			<< (i + 4));
		tmp3 = tmp3 + ((iqk_info->rxiqk_fail_code[channel][i] & 0x3)
			       << (i * 2 + 8));
	}
	odm_write_4byte(dm, 0x1b00, 0xf8000008);
	odm_set_bb_reg(dm, 0x1bf0, 0x0000ffff, tmp1 | tmp2 | tmp3);

	for (i = 0; i < 2; i++)
		odm_write_4byte(
			dm, 0x1be8 + (i * 4),
			(iqk_info->rxiqk_agc[channel][(i * 2) + 1] << 16) |
				iqk_info->rxiqk_agc[channel][i * 2]);
}

static void _iqk_backup_mac_bb_8822b(struct phy_dm_struct *dm, u32 *MAC_backup,
				     u32 *BB_backup, u32 *backup_mac_reg,
				     u32 *backup_bb_reg)
{
	u32 i;

	for (i = 0; i < MAC_REG_NUM_8822B; i++)
		MAC_backup[i] = odm_read_4byte(dm, backup_mac_reg[i]);

	for (i = 0; i < BB_REG_NUM_8822B; i++)
		BB_backup[i] = odm_read_4byte(dm, backup_bb_reg[i]);
}

static void _iqk_backup_rf_8822b(struct phy_dm_struct *dm, u32 RF_backup[][2],
				 u32 *backup_rf_reg)
{
	u32 i;

	for (i = 0; i < RF_REG_NUM_8822B; i++) {
		RF_backup[i][ODM_RF_PATH_A] = odm_get_rf_reg(
			dm, ODM_RF_PATH_A, backup_rf_reg[i], RFREGOFFSETMASK);
		RF_backup[i][ODM_RF_PATH_B] = odm_get_rf_reg(
			dm, ODM_RF_PATH_B, backup_rf_reg[i], RFREGOFFSETMASK);
	}
}

static void _iqk_agc_bnd_int_8822b(struct phy_dm_struct *dm)
{
	/*initialize RX AGC bnd, it must do after bbreset*/
	odm_write_4byte(dm, 0x1b00, 0xf8000008);
	odm_write_4byte(dm, 0x1b00, 0xf80a7008);
	odm_write_4byte(dm, 0x1b00, 0xf8015008);
	odm_write_4byte(dm, 0x1b00, 0xf8000008);
}

static void _iqk_bb_reset_8822b(struct phy_dm_struct *dm)
{
	bool cca_ing = false;
	u32 count = 0;

	odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x0, RFREGOFFSETMASK, 0x10000);
	odm_set_rf_reg(dm, ODM_RF_PATH_B, 0x0, RFREGOFFSETMASK, 0x10000);

	while (1) {
		odm_write_4byte(dm, 0x8fc, 0x0);
		odm_set_bb_reg(dm, 0x198c, 0x7, 0x7);
		cca_ing = (bool)odm_get_bb_reg(dm, 0xfa0, BIT(3));

		if (count > 30)
			cca_ing = false;

		if (cca_ing) {
			ODM_delay_ms(1);
			count++;
		} else {
			odm_write_1byte(dm, 0x808, 0x0); /*RX ant off*/
			odm_set_bb_reg(dm, 0xa04,
				       BIT(27) | BIT(26) | BIT(25) | BIT(24),
				       0x0); /*CCK RX path off*/

			/*BBreset*/
			odm_set_bb_reg(dm, 0x0, BIT(16), 0x0);
			odm_set_bb_reg(dm, 0x0, BIT(16), 0x1);

			if (odm_get_bb_reg(dm, 0x660, BIT(16)))
				odm_write_4byte(dm, 0x6b4, 0x89000006);
			break;
		}
	}
}

static void _iqk_afe_setting_8822b(struct phy_dm_struct *dm, bool do_iqk)
{
	if (do_iqk) {
		odm_write_4byte(dm, 0xc60, 0x50000000);
		odm_write_4byte(dm, 0xc60, 0x70070040);
		odm_write_4byte(dm, 0xe60, 0x50000000);
		odm_write_4byte(dm, 0xe60, 0x70070040);

		odm_write_4byte(dm, 0xc58, 0xd8000402);
		odm_write_4byte(dm, 0xc5c, 0xd1000120);
		odm_write_4byte(dm, 0xc6c, 0x00000a15);
		odm_write_4byte(dm, 0xe58, 0xd8000402);
		odm_write_4byte(dm, 0xe5c, 0xd1000120);
		odm_write_4byte(dm, 0xe6c, 0x00000a15);
		_iqk_bb_reset_8822b(dm);
	} else {
		odm_write_4byte(dm, 0xc60, 0x50000000);
		odm_write_4byte(dm, 0xc60, 0x70038040);
		odm_write_4byte(dm, 0xe60, 0x50000000);
		odm_write_4byte(dm, 0xe60, 0x70038040);

		odm_write_4byte(dm, 0xc58, 0xd8020402);
		odm_write_4byte(dm, 0xc5c, 0xde000120);
		odm_write_4byte(dm, 0xc6c, 0x0000122a);
		odm_write_4byte(dm, 0xe58, 0xd8020402);
		odm_write_4byte(dm, 0xe5c, 0xde000120);
		odm_write_4byte(dm, 0xe6c, 0x0000122a);
	}
}

static void _iqk_restore_mac_bb_8822b(struct phy_dm_struct *dm, u32 *MAC_backup,
				      u32 *BB_backup, u32 *backup_mac_reg,
				      u32 *backup_bb_reg)
{
	u32 i;

	for (i = 0; i < MAC_REG_NUM_8822B; i++)
		odm_write_4byte(dm, backup_mac_reg[i], MAC_backup[i]);
	for (i = 0; i < BB_REG_NUM_8822B; i++)
		odm_write_4byte(dm, backup_bb_reg[i], BB_backup[i]);
}

static void _iqk_restore_rf_8822b(struct phy_dm_struct *dm, u32 *backup_rf_reg,
				  u32 RF_backup[][2])
{
	u32 i;

	odm_set_rf_reg(dm, ODM_RF_PATH_A, 0xef, RFREGOFFSETMASK, 0x0);
	odm_set_rf_reg(dm, ODM_RF_PATH_B, 0xef, RFREGOFFSETMASK, 0x0);
	/*0xdf[4]=0*/
	odm_set_rf_reg(dm, ODM_RF_PATH_A, 0xdf, RFREGOFFSETMASK,
		       RF_backup[0][ODM_RF_PATH_A] & (~BIT(4)));
	odm_set_rf_reg(dm, ODM_RF_PATH_B, 0xdf, RFREGOFFSETMASK,
		       RF_backup[0][ODM_RF_PATH_B] & (~BIT(4)));

	for (i = 1; i < RF_REG_NUM_8822B; i++) {
		odm_set_rf_reg(dm, ODM_RF_PATH_A, backup_rf_reg[i],
			       RFREGOFFSETMASK, RF_backup[i][ODM_RF_PATH_A]);
		odm_set_rf_reg(dm, ODM_RF_PATH_B, backup_rf_reg[i],
			       RFREGOFFSETMASK, RF_backup[i][ODM_RF_PATH_B]);
	}
}

static void _iqk_backup_iqk_8822b(struct phy_dm_struct *dm, u8 step)
{
	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	u8 path;
	u16 iqk_apply[2] = {0xc94, 0xe94};

	if (step == 0x0) {
		phydm_backup_iqk_information(iqk_info);
	} else {
		iqk_info->iqk_channel[0] = iqk_info->rf_reg18;
		for (path = 0; path < 2; path++) {
			iqk_info->lok_idac[0][path] =
				odm_get_rf_reg(dm, (enum odm_rf_radio_path)path,
					       0x58, RFREGOFFSETMASK);
			iqk_info->bypass_iqk[0][path] =
				odm_get_bb_reg(dm, iqk_apply[path], MASKDWORD);

			phydm_set_iqk_cfir(dm, iqk_info, path);
			odm_set_bb_reg(dm, 0x1bd8, MASKDWORD, 0x0);
			odm_set_bb_reg(dm, 0x1b0c, BIT(13) | BIT(12), 0x0);
		}
	}
}

static void _iqk_reload_iqk_setting_8822b(
	struct phy_dm_struct *dm, u8 channel,
	u8 reload_idx /*1: reload TX, 2: reload LO, TX, RX*/
	)
{
	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	u8 i, path, idx;
	u16 iqk_apply[2] = {0xc94, 0xe94};

	for (path = 0; path < 2; path++) {
		if (reload_idx == 2) {
			odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0xdf,
				       BIT(4), 0x1);
			odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x58,
				       RFREGOFFSETMASK,
				       iqk_info->lok_idac[channel][path]);
		}

		for (idx = 0; idx < reload_idx; idx++) {
			odm_set_bb_reg(dm, 0x1b00, MASKDWORD,
				       0xf8000008 | path << 1);
			odm_set_bb_reg(dm, 0x1b2c, MASKDWORD, 0x7);
			odm_set_bb_reg(dm, 0x1b38, MASKDWORD, 0x20000000);
			odm_set_bb_reg(dm, 0x1b3c, MASKDWORD, 0x20000000);
			odm_set_bb_reg(dm, 0x1bcc, MASKDWORD, 0x00000000);

			if (idx == 0)
				odm_set_bb_reg(dm, 0x1b0c, BIT(13) | BIT(12),
					       0x3);
			else
				odm_set_bb_reg(dm, 0x1b0c, BIT(13) | BIT(12),
					       0x1);

			odm_set_bb_reg(dm, 0x1bd4, BIT(20) | BIT(19) | BIT(18) |
							   BIT(17) | BIT(16),
				       0x10);

			for (i = 0; i < 8; i++) {
				odm_write_4byte(
					dm, 0x1bd8,
					((0xc0000000 >> idx) + 0x3) + (i * 4) +
						(iqk_info->iqk_cfir_real
							 [channel][path][idx][i]
						 << 9));
				odm_write_4byte(
					dm, 0x1bd8,
					((0xc0000000 >> idx) + 0x1) + (i * 4) +
						(iqk_info->iqk_cfir_imag
							 [channel][path][idx][i]
						 << 9));
			}
		}
		odm_set_bb_reg(dm, iqk_apply[path], MASKDWORD,
			       iqk_info->bypass_iqk[channel][path]);

		odm_set_bb_reg(dm, 0x1bd8, MASKDWORD, 0x0);
		odm_set_bb_reg(dm, 0x1b0c, BIT(13) | BIT(12), 0x0);
	}
}

static bool _iqk_reload_iqk_8822b(struct phy_dm_struct *dm, bool reset)
{
	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	u8 i;
	bool reload = false;

	if (reset) {
		for (i = 0; i < 2; i++)
			iqk_info->iqk_channel[i] = 0x0;
	} else {
		iqk_info->rf_reg18 = odm_get_rf_reg(dm, ODM_RF_PATH_A, 0x18,
						    RFREGOFFSETMASK);

		for (i = 0; i < 2; i++) {
			if (iqk_info->rf_reg18 == iqk_info->iqk_channel[i]) {
				_iqk_reload_iqk_setting_8822b(dm, i, 2);
				_iqk_fill_iqk_report_8822b(dm, i);
				ODM_RT_TRACE(
					dm, ODM_COMP_CALIBRATION,
					"[IQK]reload IQK result before!!!!\n");
				reload = true;
			}
		}
	}
	return reload;
}

static void _iqk_rfe_setting_8822b(struct phy_dm_struct *dm, bool ext_pa_on)
{
	if (ext_pa_on) {
		/*RFE setting*/
		odm_write_4byte(dm, 0xcb0, 0x77777777);
		odm_write_4byte(dm, 0xcb4, 0x00007777);
		odm_write_4byte(dm, 0xcbc, 0x0000083B);
		odm_write_4byte(dm, 0xeb0, 0x77777777);
		odm_write_4byte(dm, 0xeb4, 0x00007777);
		odm_write_4byte(dm, 0xebc, 0x0000083B);
		ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
			     "[IQK]external PA on!!!!\n");
	} else {
		/*RFE setting*/
		odm_write_4byte(dm, 0xcb0, 0x77777777);
		odm_write_4byte(dm, 0xcb4, 0x00007777);
		odm_write_4byte(dm, 0xcbc, 0x00000100);
		odm_write_4byte(dm, 0xeb0, 0x77777777);
		odm_write_4byte(dm, 0xeb4, 0x00007777);
		odm_write_4byte(dm, 0xebc, 0x00000100);
	}
}

static void _iqk_rf_setting_8822b(struct phy_dm_struct *dm)
{
	u8 path;
	u32 tmp;

	odm_write_4byte(dm, 0x1b00, 0xf8000008);
	odm_write_4byte(dm, 0x1bb8, 0x00000000);

	for (path = 0; path < 2; path++) {
		/*0xdf:B11 = 1,B4 = 0, B1 = 1*/
		tmp = odm_get_rf_reg(dm, (enum odm_rf_radio_path)path, 0xdf,
				     RFREGOFFSETMASK);
		tmp = (tmp & (~BIT(4))) | BIT(1) | BIT(11);
		odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0xdf,
			       RFREGOFFSETMASK, tmp);

		/*release 0x56 TXBB*/
		odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x65,
			       RFREGOFFSETMASK, 0x09000);

		if (*dm->band_type == ODM_BAND_5G) {
			odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0xef,
				       BIT(19), 0x1);
			odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x33,
				       RFREGOFFSETMASK, 0x00026);
			odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x3e,
				       RFREGOFFSETMASK, 0x00037);
			odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x3f,
				       RFREGOFFSETMASK, 0xdefce);
			odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0xef,
				       BIT(19), 0x0);
		} else {
			odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0xef,
				       BIT(19), 0x1);
			odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x33,
				       RFREGOFFSETMASK, 0x00026);
			odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x3e,
				       RFREGOFFSETMASK, 0x00037);
			odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x3f,
				       RFREGOFFSETMASK, 0x5efce);
			odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0xef,
				       BIT(19), 0x0);
		}
	}
}

static void _iqk_configure_macbb_8822b(struct phy_dm_struct *dm)
{
	/*MACBB register setting*/
	odm_write_1byte(dm, 0x522, 0x7f);
	odm_set_bb_reg(dm, 0x550, BIT(11) | BIT(3), 0x0);
	odm_set_bb_reg(dm, 0x90c, BIT(15),
		       0x1); /*0x90c[15]=1: dac_buf reset selection*/
	odm_set_bb_reg(dm, 0x9a4, BIT(31),
		       0x0); /*0x9a4[31]=0: Select da clock*/
	/*0xc94[0]=1, 0xe94[0]=1: let tx through iqk*/
	odm_set_bb_reg(dm, 0xc94, BIT(0), 0x1);
	odm_set_bb_reg(dm, 0xe94, BIT(0), 0x1);
	/* 3-wire off*/
	odm_write_4byte(dm, 0xc00, 0x00000004);
	odm_write_4byte(dm, 0xe00, 0x00000004);
}

static void _iqk_lok_setting_8822b(struct phy_dm_struct *dm, u8 path)
{
	odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);
	odm_write_4byte(dm, 0x1bcc, 0x9);
	odm_write_1byte(dm, 0x1b23, 0x00);

	switch (*dm->band_type) {
	case ODM_BAND_2_4G:
		odm_write_1byte(dm, 0x1b2b, 0x00);
		odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x56,
			       RFREGOFFSETMASK, 0x50df2);
		odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x8f,
			       RFREGOFFSETMASK, 0xadc00);
		/* WE_LUT_TX_LOK*/
		odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0xef, BIT(4),
			       0x1);
		odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x33,
			       BIT(1) | BIT(0), 0x0);
		break;
	case ODM_BAND_5G:
		odm_write_1byte(dm, 0x1b2b, 0x80);
		odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x56,
			       RFREGOFFSETMASK, 0x5086c);
		odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x8f,
			       RFREGOFFSETMASK, 0xa9c00);
		/* WE_LUT_TX_LOK*/
		odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0xef, BIT(4),
			       0x1);
		odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x33,
			       BIT(1) | BIT(0), 0x1);
		break;
	}
}

static void _iqk_txk_setting_8822b(struct phy_dm_struct *dm, u8 path)
{
	odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);
	odm_write_4byte(dm, 0x1bcc, 0x9);
	odm_write_4byte(dm, 0x1b20, 0x01440008);

	if (path == 0x0)
		odm_write_4byte(dm, 0x1b00, 0xf800000a);
	else
		odm_write_4byte(dm, 0x1b00, 0xf8000008);
	odm_write_4byte(dm, 0x1bcc, 0x3f);

	switch (*dm->band_type) {
	case ODM_BAND_2_4G:
		odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x56,
			       RFREGOFFSETMASK, 0x50df2);
		odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x8f,
			       RFREGOFFSETMASK, 0xadc00);
		odm_write_1byte(dm, 0x1b2b, 0x00);
		break;
	case ODM_BAND_5G:
		odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x56,
			       RFREGOFFSETMASK, 0x500ef);
		odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x8f,
			       RFREGOFFSETMASK, 0xa9c00);
		odm_write_1byte(dm, 0x1b2b, 0x80);
		break;
	}
}

static void _iqk_rxk1_setting_8822b(struct phy_dm_struct *dm, u8 path)
{
	odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);

	switch (*dm->band_type) {
	case ODM_BAND_2_4G:
		odm_write_1byte(dm, 0x1bcc, 0x9);
		odm_write_1byte(dm, 0x1b2b, 0x00);
		odm_write_4byte(dm, 0x1b20, 0x01450008);
		odm_write_4byte(dm, 0x1b24, 0x01460c88);
		odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x56,
			       RFREGOFFSETMASK, 0x510e0);
		odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x8f,
			       RFREGOFFSETMASK, 0xacc00);
		break;
	case ODM_BAND_5G:
		odm_write_1byte(dm, 0x1bcc, 0x09);
		odm_write_1byte(dm, 0x1b2b, 0x80);
		odm_write_4byte(dm, 0x1b20, 0x00850008);
		odm_write_4byte(dm, 0x1b24, 0x00460048);
		odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x56,
			       RFREGOFFSETMASK, 0x510e0);
		odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x8f,
			       RFREGOFFSETMASK, 0xadc00);
		break;
	}
}

static void _iqk_rxk2_setting_8822b(struct phy_dm_struct *dm, u8 path,
				    bool is_gs)
{
	struct dm_iqk_info *iqk_info = &dm->IQK_info;

	odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);

	switch (*dm->band_type) {
	case ODM_BAND_2_4G:
		if (is_gs)
			iqk_info->tmp1bcc = 0x12;
		odm_write_1byte(dm, 0x1bcc, iqk_info->tmp1bcc);
		odm_write_1byte(dm, 0x1b2b, 0x00);
		odm_write_4byte(dm, 0x1b20, 0x01450008);
		odm_write_4byte(dm, 0x1b24, 0x01460848);
		odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x56,
			       RFREGOFFSETMASK, 0x510e0);
		odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x8f,
			       RFREGOFFSETMASK, 0xa9c00);
		break;
	case ODM_BAND_5G:
		if (is_gs) {
			if (path == ODM_RF_PATH_A)
				iqk_info->tmp1bcc = 0x12;
			else
				iqk_info->tmp1bcc = 0x09;
		}
		odm_write_1byte(dm, 0x1bcc, iqk_info->tmp1bcc);
		odm_write_1byte(dm, 0x1b2b, 0x80);
		odm_write_4byte(dm, 0x1b20, 0x00850008);
		odm_write_4byte(dm, 0x1b24, 0x00460848);
		odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x56,
			       RFREGOFFSETMASK, 0x51060);
		odm_set_rf_reg(dm, (enum odm_rf_radio_path)path, 0x8f,
			       RFREGOFFSETMASK, 0xa9c00);
		break;
	}
}

static bool _iqk_check_cal_8822b(struct phy_dm_struct *dm, u32 IQK_CMD)
{
	bool notready = true, fail = true;
	u32 delay_count = 0x0;

	while (notready) {
		if (odm_read_4byte(dm, 0x1b00) == (IQK_CMD & 0xffffff0f)) {
			fail = (bool)odm_get_bb_reg(dm, 0x1b08, BIT(26));
			notready = false;
		} else {
			ODM_delay_ms(1);
			delay_count++;
		}

		if (delay_count >= 50) {
			fail = true;
			ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
				     "[IQK]IQK timeout!!!\n");
			break;
		}
	}
	ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION, "[IQK]delay count = 0x%x!!!\n",
		     delay_count);
	return fail;
}

static bool _iqk_rx_iqk_gain_search_fail_8822b(struct phy_dm_struct *dm,
					       u8 path, u8 step)
{
	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	bool fail = true;
	u32 IQK_CMD = 0x0, rf_reg0, tmp, bb_idx;
	u8 IQMUX[4] = {0x9, 0x12, 0x1b, 0x24};
	u8 idx;

	for (idx = 0; idx < 4; idx++)
		if (iqk_info->tmp1bcc == IQMUX[idx])
			break;

	odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);
	odm_write_4byte(dm, 0x1bcc, iqk_info->tmp1bcc);

	if (step == RXIQK1)
		ODM_RT_TRACE(
			dm, ODM_COMP_CALIBRATION,
			"[IQK]============ S%d RXIQK GainSearch ============\n",
			path);

	if (step == RXIQK1)
		IQK_CMD = 0xf8000208 | (1 << (path + 4));
	else
		IQK_CMD = 0xf8000308 | (1 << (path + 4));

	ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION, "[IQK]S%d GS%d_Trigger = 0x%x\n",
		     path, step, IQK_CMD);

	odm_write_4byte(dm, 0x1b00, IQK_CMD);
	odm_write_4byte(dm, 0x1b00, IQK_CMD + 0x1);
	ODM_delay_ms(GS_delay_8822B);
	fail = _iqk_check_cal_8822b(dm, IQK_CMD);

	if (step == RXIQK2) {
		rf_reg0 = odm_get_rf_reg(dm, (enum odm_rf_radio_path)path, 0x0,
					 RFREGOFFSETMASK);
		odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);
		ODM_RT_TRACE(
			dm, ODM_COMP_CALIBRATION,
			"[IQK]S%d ==> RF0x0 = 0x%x, tmp1bcc = 0x%x, idx = %d, 0x1b3c = 0x%x\n",
			path, rf_reg0, iqk_info->tmp1bcc, idx,
			odm_read_4byte(dm, 0x1b3c));
		tmp = (rf_reg0 & 0x1fe0) >> 5;
		iqk_info->lna_idx = tmp >> 5;
		bb_idx = tmp & 0x1f;
		if (bb_idx == 0x1) {
			if (iqk_info->lna_idx != 0x0)
				iqk_info->lna_idx--;
			else if (idx != 3)
				idx++;
			else
				iqk_info->isbnd = true;
			fail = true;
		} else if (bb_idx == 0xa) {
			if (idx != 0)
				idx--;
			else if (iqk_info->lna_idx != 0x7)
				iqk_info->lna_idx++;
			else
				iqk_info->isbnd = true;
			fail = true;
		} else {
			fail = false;
		}

		if (iqk_info->isbnd)
			fail = false;

		iqk_info->tmp1bcc = IQMUX[idx];

		if (fail) {
			odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);
			odm_write_4byte(
				dm, 0x1b24,
				(odm_read_4byte(dm, 0x1b24) & 0xffffe3ff) |
					(iqk_info->lna_idx << 10));
		}
	}

	return fail;
}

static bool _lok_one_shot_8822b(void *dm_void, u8 path)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	u8 delay_count = 0;
	bool LOK_notready = false;
	u32 LOK_temp = 0;
	u32 IQK_CMD = 0x0;

	ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
		     "[IQK]==========S%d LOK ==========\n", path);

	IQK_CMD = 0xf8000008 | (1 << (4 + path));

	ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION, "[IQK]LOK_Trigger = 0x%x\n",
		     IQK_CMD);

	odm_write_4byte(dm, 0x1b00, IQK_CMD);
	odm_write_4byte(dm, 0x1b00, IQK_CMD + 1);
	/*LOK: CMD ID = 0	{0xf8000018, 0xf8000028}*/
	/*LOK: CMD ID = 0	{0xf8000019, 0xf8000029}*/
	ODM_delay_ms(LOK_delay_8822B);

	delay_count = 0;
	LOK_notready = true;

	while (LOK_notready) {
		if (odm_read_4byte(dm, 0x1b00) == (IQK_CMD & 0xffffff0f))
			LOK_notready = false;
		else
			LOK_notready = true;

		if (LOK_notready) {
			ODM_delay_ms(1);
			delay_count++;
		}

		if (delay_count >= 50) {
			ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
				     "[IQK]S%d LOK timeout!!!\n", path);
			break;
		}
	}

	ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
		     "[IQK]S%d ==> delay_count = 0x%x\n", path, delay_count);
	if (ODM_COMP_CALIBRATION) {
		if (!LOK_notready) {
			LOK_temp =
				odm_get_rf_reg(dm, (enum odm_rf_radio_path)path,
					       0x58, RFREGOFFSETMASK);
			ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
				     "[IQK]0x58 = 0x%x\n", LOK_temp);
		} else {
			ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
				     "[IQK]==>S%d LOK Fail!!!\n", path);
		}
	}
	iqk_info->lok_fail[path] = LOK_notready;
	return LOK_notready;
}

static bool _iqk_one_shot_8822b(void *dm_void, u8 path, u8 idx)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	u8 delay_count = 0;
	bool notready = true, fail = true;
	u32 IQK_CMD = 0x0;
	u16 iqk_apply[2] = {0xc94, 0xe94};

	if (idx == TXIQK)
		ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
			     "[IQK]============ S%d WBTXIQK ============\n",
			     path);
	else if (idx == RXIQK1)
		ODM_RT_TRACE(
			dm, ODM_COMP_CALIBRATION,
			"[IQK]============ S%d WBRXIQK STEP1============\n",
			path);
	else
		ODM_RT_TRACE(
			dm, ODM_COMP_CALIBRATION,
			"[IQK]============ S%d WBRXIQK STEP2============\n",
			path);

	if (idx == TXIQK) {
		IQK_CMD = 0xf8000008 | ((*dm->band_width + 4) << 8) |
			  (1 << (path + 4));
		ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
			     "[IQK]TXK_Trigger = 0x%x\n", IQK_CMD);
		/*{0xf8000418, 0xf800042a} ==> 20 WBTXK (CMD = 4)*/
		/*{0xf8000518, 0xf800052a} ==> 40 WBTXK (CMD = 5)*/
		/*{0xf8000618, 0xf800062a} ==> 80 WBTXK (CMD = 6)*/
	} else if (idx == RXIQK1) {
		if (*dm->band_width == 2)
			IQK_CMD = 0xf8000808 | (1 << (path + 4));
		else
			IQK_CMD = 0xf8000708 | (1 << (path + 4));
		ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
			     "[IQK]RXK1_Trigger = 0x%x\n", IQK_CMD);
		/*{0xf8000718, 0xf800072a} ==> 20 WBTXK (CMD = 7)*/
		/*{0xf8000718, 0xf800072a} ==> 40 WBTXK (CMD = 7)*/
		/*{0xf8000818, 0xf800082a} ==> 80 WBTXK (CMD = 8)*/
	} else if (idx == RXIQK2) {
		IQK_CMD = 0xf8000008 | ((*dm->band_width + 9) << 8) |
			  (1 << (path + 4));
		ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
			     "[IQK]RXK2_Trigger = 0x%x\n", IQK_CMD);
		/*{0xf8000918, 0xf800092a} ==> 20 WBRXK (CMD = 9)*/
		/*{0xf8000a18, 0xf8000a2a} ==> 40 WBRXK (CMD = 10)*/
		/*{0xf8000b18, 0xf8000b2a} ==> 80 WBRXK (CMD = 11)*/
		odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);
		odm_write_4byte(dm, 0x1b24,
				(odm_read_4byte(dm, 0x1b24) & 0xffffe3ff) |
					((iqk_info->lna_idx & 0x7) << 10));
	}
	odm_write_4byte(dm, 0x1b00, IQK_CMD);
	odm_write_4byte(dm, 0x1b00, IQK_CMD + 0x1);
	ODM_delay_ms(WBIQK_delay_8822B);

	while (notready) {
		if (odm_read_4byte(dm, 0x1b00) == (IQK_CMD & 0xffffff0f))
			notready = false;
		else
			notready = true;

		if (notready) {
			ODM_delay_ms(1);
			delay_count++;
		} else {
			fail = (bool)odm_get_bb_reg(dm, 0x1b08, BIT(26));
			break;
		}

		if (delay_count >= 50) {
			ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
				     "[IQK]S%d IQK timeout!!!\n", path);
			break;
		}
	}

	if (dm->debug_components & ODM_COMP_CALIBRATION) {
		odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);
		ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
			     "[IQK]S%d ==> 0x1b00 = 0x%x, 0x1b08 = 0x%x\n",
			     path, odm_read_4byte(dm, 0x1b00),
			     odm_read_4byte(dm, 0x1b08));
		ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
			     "[IQK]S%d ==> delay_count = 0x%x\n", path,
			     delay_count);
		if (idx != TXIQK)
			ODM_RT_TRACE(
				dm, ODM_COMP_CALIBRATION,
				"[IQK]S%d ==> RF0x0 = 0x%x, RF0x56 = 0x%x\n",
				path,
				odm_get_rf_reg(dm, (enum odm_rf_radio_path)path,
					       0x0, RFREGOFFSETMASK),
				odm_get_rf_reg(dm, (enum odm_rf_radio_path)path,
					       0x56, RFREGOFFSETMASK));
	}

	odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);

	if (idx == TXIQK)
		if (fail)
			odm_set_bb_reg(dm, iqk_apply[path], BIT(0), 0x0);

	if (idx == RXIQK2) {
		iqk_info->rxiqk_agc[0][path] =
			(u16)(((odm_get_rf_reg(dm, (enum odm_rf_radio_path)path,
					       0x0, RFREGOFFSETMASK) >>
				5) &
			       0xff) |
			      (iqk_info->tmp1bcc << 8));

		odm_write_4byte(dm, 0x1b38, 0x20000000);

		if (!fail)
			odm_set_bb_reg(dm, iqk_apply[path], (BIT(11) | BIT(10)),
				       0x1);
		else
			odm_set_bb_reg(dm, iqk_apply[path], (BIT(11) | BIT(10)),
				       0x0);
	}

	if (idx == TXIQK)
		iqk_info->iqk_fail_report[0][path][TXIQK] = fail;
	else
		iqk_info->iqk_fail_report[0][path][RXIQK] = fail;

	return fail;
}

static bool _iqk_rx_iqk_by_path_8822b(void *dm_void, u8 path)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	bool KFAIL = true, gonext;

	switch (iqk_info->rxiqk_step) {
	case 1: /*gain search_RXK1*/
		_iqk_rxk1_setting_8822b(dm, path);
		gonext = false;
		while (1) {
			KFAIL = _iqk_rx_iqk_gain_search_fail_8822b(dm, path,
								   RXIQK1);
			if (KFAIL &&
			    (iqk_info->gs_retry_count[0][path][GSRXK1] < 2))
				iqk_info->gs_retry_count[0][path][GSRXK1]++;
			else if (KFAIL) {
				iqk_info->rxiqk_fail_code[0][path] = 0;
				iqk_info->rxiqk_step = 5;
				gonext = true;
			} else {
				iqk_info->rxiqk_step++;
				gonext = true;
			}
			if (gonext)
				break;
		}
		break;
	case 2: /*gain search_RXK2*/
		_iqk_rxk2_setting_8822b(dm, path, true);
		iqk_info->isbnd = false;
		while (1) {
			KFAIL = _iqk_rx_iqk_gain_search_fail_8822b(dm, path,
								   RXIQK2);
			if (KFAIL &&
			    (iqk_info->gs_retry_count[0][path][GSRXK2] <
			     rxiqk_gs_limit)) {
				iqk_info->gs_retry_count[0][path][GSRXK2]++;
			} else {
				iqk_info->rxiqk_step++;
				break;
			}
		}
		break;
	case 3: /*RXK1*/
		_iqk_rxk1_setting_8822b(dm, path);
		gonext = false;
		while (1) {
			KFAIL = _iqk_one_shot_8822b(dm, path, RXIQK1);
			if (KFAIL &&
			    (iqk_info->retry_count[0][path][RXIQK1] < 2))
				iqk_info->retry_count[0][path][RXIQK1]++;
			else if (KFAIL) {
				iqk_info->rxiqk_fail_code[0][path] = 1;
				iqk_info->rxiqk_step = 5;
				gonext = true;
			} else {
				iqk_info->rxiqk_step++;
				gonext = true;
			}
			if (gonext)
				break;
		}
		break;
	case 4: /*RXK2*/
		_iqk_rxk2_setting_8822b(dm, path, false);
		gonext = false;
		while (1) {
			KFAIL = _iqk_one_shot_8822b(dm, path, RXIQK2);
			if (KFAIL &&
			    (iqk_info->retry_count[0][path][RXIQK2] < 2))
				iqk_info->retry_count[0][path][RXIQK2]++;
			else if (KFAIL) {
				iqk_info->rxiqk_fail_code[0][path] = 2;
				iqk_info->rxiqk_step = 5;
				gonext = true;
			} else {
				iqk_info->rxiqk_step++;
				gonext = true;
			}
			if (gonext)
				break;
		}
		break;
	}
	return KFAIL;
}

static void _iqk_iqk_by_path_8822b(void *dm_void, bool segment_iqk)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	bool KFAIL = true;
	u8 i, kcount_limit;

	if (*dm->band_width == 2)
		kcount_limit = kcount_limit_80m;
	else
		kcount_limit = kcount_limit_others;

	while (1) {
		switch (dm->rf_calibrate_info.iqk_step) {
		case 1: /*S0 LOK*/
			_iqk_lok_setting_8822b(dm, ODM_RF_PATH_A);
			_lok_one_shot_8822b(dm, ODM_RF_PATH_A);
			dm->rf_calibrate_info.iqk_step++;
			break;
		case 2: /*S1 LOK*/
			_iqk_lok_setting_8822b(dm, ODM_RF_PATH_B);
			_lok_one_shot_8822b(dm, ODM_RF_PATH_B);
			dm->rf_calibrate_info.iqk_step++;
			break;
		case 3: /*S0 TXIQK*/
			_iqk_txk_setting_8822b(dm, ODM_RF_PATH_A);
			KFAIL = _iqk_one_shot_8822b(dm, ODM_RF_PATH_A, TXIQK);
			iqk_info->kcount++;
			ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
				     "[IQK]S0TXK KFail = 0x%x\n", KFAIL);

			if (KFAIL &&
			    (iqk_info->retry_count[0][ODM_RF_PATH_A][TXIQK] <
			     3))
				iqk_info->retry_count[0][ODM_RF_PATH_A]
						     [TXIQK]++;
			else
				dm->rf_calibrate_info.iqk_step++;
			break;
		case 4: /*S1 TXIQK*/
			_iqk_txk_setting_8822b(dm, ODM_RF_PATH_B);
			KFAIL = _iqk_one_shot_8822b(dm, ODM_RF_PATH_B, TXIQK);
			iqk_info->kcount++;
			ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
				     "[IQK]S1TXK KFail = 0x%x\n", KFAIL);
			if (KFAIL &&
			    iqk_info->retry_count[0][ODM_RF_PATH_B][TXIQK] < 3)
				iqk_info->retry_count[0][ODM_RF_PATH_B]
						     [TXIQK]++;
			else
				dm->rf_calibrate_info.iqk_step++;
			break;
		case 5: /*S0 RXIQK*/
			phydm_set_iqk_info(dm, iqk_info, 0);
			break;
		case 6: /*S1 RXIQK*/
			phydm_set_iqk_info(dm, iqk_info, 1);
			break;
		}

		if (dm->rf_calibrate_info.iqk_step == 7) {
			ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
				     "[IQK]==========LOK summary ==========\n");
			ODM_RT_TRACE(
				dm, ODM_COMP_CALIBRATION,
				"[IQK]PathA_LOK_notready = %d, PathB_LOK1_notready = %d\n",
				iqk_info->lok_fail[ODM_RF_PATH_A],
				iqk_info->lok_fail[ODM_RF_PATH_B]);
			ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
				     "[IQK]==========IQK summary ==========\n");
			ODM_RT_TRACE(
				dm, ODM_COMP_CALIBRATION,
				"[IQK]PathA_TXIQK_fail = %d, PathB_TXIQK_fail = %d\n",
				iqk_info->iqk_fail_report[0][ODM_RF_PATH_A]
							 [TXIQK],
				iqk_info->iqk_fail_report[0][ODM_RF_PATH_B]
							 [TXIQK]);
			ODM_RT_TRACE(
				dm, ODM_COMP_CALIBRATION,
				"[IQK]PathA_RXIQK_fail = %d, PathB_RXIQK_fail = %d\n",
				iqk_info->iqk_fail_report[0][ODM_RF_PATH_A]
							 [RXIQK],
				iqk_info->iqk_fail_report[0][ODM_RF_PATH_B]
							 [RXIQK]);
			ODM_RT_TRACE(
				dm, ODM_COMP_CALIBRATION,
				"[IQK]PathA_TXIQK_retry = %d, PathB_TXIQK_retry = %d\n",
				iqk_info->retry_count[0][ODM_RF_PATH_A][TXIQK],
				iqk_info->retry_count[0][ODM_RF_PATH_B][TXIQK]);
			ODM_RT_TRACE(
				dm, ODM_COMP_CALIBRATION,
				"[IQK]PathA_RXK1_retry = %d, PathA_RXK2_retry = %d, PathB_RXK1_retry = %d, PathB_RXK2_retry = %d\n",
				iqk_info->retry_count[0][ODM_RF_PATH_A][RXIQK1],
				iqk_info->retry_count[0][ODM_RF_PATH_A][RXIQK2],
				iqk_info->retry_count[0][ODM_RF_PATH_B][RXIQK1],
				iqk_info->retry_count[0][ODM_RF_PATH_B]
						     [RXIQK2]);
			ODM_RT_TRACE(
				dm, ODM_COMP_CALIBRATION,
				"[IQK]PathA_GS1_retry = %d, PathA_GS2_retry = %d, PathB_GS1_retry = %d, PathB_GS2_retry = %d\n",
				iqk_info->gs_retry_count[0][ODM_RF_PATH_A]
							[GSRXK1],
				iqk_info->gs_retry_count[0][ODM_RF_PATH_A]
							[GSRXK2],
				iqk_info->gs_retry_count[0][ODM_RF_PATH_B]
							[GSRXK1],
				iqk_info->gs_retry_count[0][ODM_RF_PATH_B]
							[GSRXK2]);
			for (i = 0; i < 2; i++) {
				odm_write_4byte(dm, 0x1b00,
						0xf8000008 | i << 1);
				odm_write_4byte(dm, 0x1b2c, 0x7);
				odm_write_4byte(dm, 0x1bcc, 0x0);
			}
			break;
		}

		if (segment_iqk && (iqk_info->kcount == kcount_limit))
			break;
	}
}

static void _iqk_start_iqk_8822b(struct phy_dm_struct *dm, bool segment_iqk)
{
	u32 tmp;

	/*GNT_WL = 1*/
	tmp = odm_get_rf_reg(dm, ODM_RF_PATH_A, 0x1, RFREGOFFSETMASK);
	tmp = tmp | BIT(5) | BIT(0);
	odm_set_rf_reg(dm, ODM_RF_PATH_A, 0x1, RFREGOFFSETMASK, tmp);

	tmp = odm_get_rf_reg(dm, ODM_RF_PATH_B, 0x1, RFREGOFFSETMASK);
	tmp = tmp | BIT(5) | BIT(0);
	odm_set_rf_reg(dm, ODM_RF_PATH_B, 0x1, RFREGOFFSETMASK, tmp);

	_iqk_iqk_by_path_8822b(dm, segment_iqk);
}

static void _iq_calibrate_8822b_init(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	u8 i, j;

	if (iqk_info->iqk_times == 0) {
		ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
			     "[IQK]=====>PHY_IQCalibrate_8822B_Init\n");

		for (i = 0; i < SS_8822B; i++) {
			for (j = 0; j < 2; j++) {
				iqk_info->lok_fail[i] = true;
				iqk_info->iqk_fail[j][i] = true;
				iqk_info->iqc_matrix[j][i] = 0x20000000;
			}
		}

		phydm_init_iqk_information(iqk_info);
	}
}

static void _phy_iq_calibrate_8822b(struct phy_dm_struct *dm, bool reset)
{
	u32 MAC_backup[MAC_REG_NUM_8822B], BB_backup[BB_REG_NUM_8822B],
		RF_backup[RF_REG_NUM_8822B][SS_8822B];
	u32 backup_mac_reg[MAC_REG_NUM_8822B] = {0x520, 0x550};
	u32 backup_bb_reg[BB_REG_NUM_8822B] = {
		0x808, 0x90c, 0xc00, 0xcb0,  0xcb4, 0xcbc, 0xe00,
		0xeb0, 0xeb4, 0xebc, 0x1990, 0x9a4, 0xa04};
	u32 backup_rf_reg[RF_REG_NUM_8822B] = {0xdf, 0x8f, 0x65, 0x0, 0x1};
	bool segment_iqk = false, is_mp = false;

	struct dm_iqk_info *iqk_info = &dm->IQK_info;

	if (dm->mp_mode)
		is_mp = true;
	else if (dm->is_linked)
		segment_iqk = true;

	if (!is_mp)
		if (_iqk_reload_iqk_8822b(dm, reset))
			return;

	ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
		     "[IQK]==========IQK strat!!!!!==========\n");

	ODM_RT_TRACE(
		dm, ODM_COMP_CALIBRATION,
		"[IQK]band_type = %s, band_width = %d, ExtPA2G = %d, ext_pa_5g = %d\n",
		(*dm->band_type == ODM_BAND_5G) ? "5G" : "2G", *dm->band_width,
		dm->ext_pa, dm->ext_pa_5g);
	ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
		     "[IQK]Interface = %d, cut_version = %x\n",
		     dm->support_interface, dm->cut_version);

	iqk_info->iqk_times++;

	iqk_info->kcount = 0;
	dm->rf_calibrate_info.iqk_total_progressing_time = 0;
	dm->rf_calibrate_info.iqk_step = 1;
	iqk_info->rxiqk_step = 1;

	_iqk_backup_iqk_8822b(dm, 0);
	_iqk_backup_mac_bb_8822b(dm, MAC_backup, BB_backup, backup_mac_reg,
				 backup_bb_reg);
	_iqk_backup_rf_8822b(dm, RF_backup, backup_rf_reg);

	while (1) {
		if (!is_mp)
			dm->rf_calibrate_info.iqk_start_time =
				odm_get_current_time(dm);

		_iqk_configure_macbb_8822b(dm);
		_iqk_afe_setting_8822b(dm, true);
		_iqk_rfe_setting_8822b(dm, false);
		_iqk_agc_bnd_int_8822b(dm);
		_iqk_rf_setting_8822b(dm);

		_iqk_start_iqk_8822b(dm, segment_iqk);

		_iqk_afe_setting_8822b(dm, false);
		_iqk_restore_mac_bb_8822b(dm, MAC_backup, BB_backup,
					  backup_mac_reg, backup_bb_reg);
		_iqk_restore_rf_8822b(dm, backup_rf_reg, RF_backup);

		if (!is_mp) {
			dm->rf_calibrate_info.iqk_progressing_time =
				odm_get_progressing_time(
					dm,
					dm->rf_calibrate_info.iqk_start_time);
			dm->rf_calibrate_info.iqk_total_progressing_time +=
				odm_get_progressing_time(
					dm,
					dm->rf_calibrate_info.iqk_start_time);
			ODM_RT_TRACE(
				dm, ODM_COMP_CALIBRATION,
				"[IQK]IQK progressing_time = %lld ms\n",
				dm->rf_calibrate_info.iqk_progressing_time);
		}

		if (dm->rf_calibrate_info.iqk_step == 7)
			break;

		iqk_info->kcount = 0;
		ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION, "[IQK]delay 50ms!!!\n");
		ODM_delay_ms(50);
	};

	_iqk_backup_iqk_8822b(dm, 1);
	_iqk_fill_iqk_report_8822b(dm, 0);

	if (!is_mp)
		ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
			     "[IQK]Total IQK progressing_time = %lld ms\n",
			     dm->rf_calibrate_info.iqk_total_progressing_time);

	ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
		     "[IQK]==========IQK end!!!!!==========\n");
}

static void _phy_iq_calibrate_by_fw_8822b(void *dm_void, u8 clear) {}

/*IQK version:v3.3, NCTL v0.6*/
/*1.The new gainsearch method for RXIQK*/
/*2.The new format of IQK report register: 0x1be8/0x1bec*/
/*3. add the option of segment IQK*/
void phy_iq_calibrate_8822b(void *dm_void, bool clear)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	dm->iqk_fw_offload = 0;

	/*FW IQK*/
	if (dm->iqk_fw_offload) {
		if (!dm->rf_calibrate_info.is_iqk_in_progress) {
			odm_acquire_spin_lock(dm, RT_IQK_SPINLOCK);
			dm->rf_calibrate_info.is_iqk_in_progress = true;
			odm_release_spin_lock(dm, RT_IQK_SPINLOCK);

			dm->rf_calibrate_info.iqk_start_time =
				odm_get_current_time(dm);

			odm_write_4byte(dm, 0x1b00, 0xf8000008);
			odm_set_bb_reg(dm, 0x1bf0, 0xff000000, 0xff);
			ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
				     "[IQK]0x1bf0 = 0x%x\n",
				     odm_read_4byte(dm, 0x1bf0));

			_phy_iq_calibrate_by_fw_8822b(dm, clear);
			phydm_get_read_counter(dm);

			dm->rf_calibrate_info.iqk_progressing_time =
				odm_get_progressing_time(
					dm,
					dm->rf_calibrate_info.iqk_start_time);

			ODM_RT_TRACE(
				dm, ODM_COMP_CALIBRATION,
				"[IQK]IQK progressing_time = %lld ms\n",
				dm->rf_calibrate_info.iqk_progressing_time);

			odm_acquire_spin_lock(dm, RT_IQK_SPINLOCK);
			dm->rf_calibrate_info.is_iqk_in_progress = false;
			odm_release_spin_lock(dm, RT_IQK_SPINLOCK);
		} else {
			ODM_RT_TRACE(
				dm, ODM_COMP_CALIBRATION,
				"== Return the IQK CMD, because the IQK in Progress ==\n");
		}

	} else {
		_iq_calibrate_8822b_init(dm_void);

		if (!dm->rf_calibrate_info.is_iqk_in_progress) {
			odm_acquire_spin_lock(dm, RT_IQK_SPINLOCK);
			dm->rf_calibrate_info.is_iqk_in_progress = true;
			odm_release_spin_lock(dm, RT_IQK_SPINLOCK);
			if (dm->mp_mode)
				dm->rf_calibrate_info.iqk_start_time =
					odm_get_current_time(dm);

			_phy_iq_calibrate_8822b(dm, clear);
			if (dm->mp_mode) {
				dm->rf_calibrate_info.iqk_progressing_time =
					odm_get_progressing_time(
						dm, dm->rf_calibrate_info
							    .iqk_start_time);
				ODM_RT_TRACE(
					dm, ODM_COMP_CALIBRATION,
					"[IQK]IQK progressing_time = %lld ms\n",
					dm->rf_calibrate_info
						.iqk_progressing_time);
			}
			odm_acquire_spin_lock(dm, RT_IQK_SPINLOCK);
			dm->rf_calibrate_info.is_iqk_in_progress = false;
			odm_release_spin_lock(dm, RT_IQK_SPINLOCK);
		} else {
			ODM_RT_TRACE(
				dm, ODM_COMP_CALIBRATION,
				"[IQK]== Return the IQK CMD, because the IQK in Progress ==\n");
		}
	}
}
