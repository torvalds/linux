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

#include "mp_precomp.h"
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#if RT_PLATFORM == PLATFORM_MACOSX
#include "phydm_precomp.h"
#else
#include "../phydm_precomp.h"
#endif
#else
#include "../../phydm_precomp.h"
#endif

#if (RTL8821C_SUPPORT == 1)
/*---------------------------Define Local Constant---------------------------*/

static u32 dpk_result[DPK_BACKUP_REG_NUM_8821C];
static boolean txgap_done[3] = {false, false, false};
static boolean overflowflag;
#define dpk_forcein_sram4 1
#define txgap_ref_index 0x0
#define txgap_k_number 0x7

/*---------------------------Define Local Constant---------------------------*/
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
void do_iqk_8821c(void *dm_void, u8 delta_thermal_index, u8 thermal_value,
		  u8 threshold)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct dm_iqk_info *iqk_info = &dm->IQK_info;

	dm->rf_calibrate_info.thermal_value_iqk = thermal_value;
	halrf_segment_iqk_trigger(dm, true, iqk_info->segment_iqk);
}
#else
/*Originally config->do_iqk is hooked phy_iq_calibrate_8821c, but do_iqk_8821c and phy_iq_calibrate_8821c have different arguments*/
void do_iqk_8821c(void *dm_void, u8 delta_thermal_index, u8 thermal_value,
		  u8 threshold)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	/*boolean		is_recovery = (boolean) delta_thermal_index;*/
	halrf_segment_iqk_trigger(dm, true, iqk_info->segment_iqk);
}
#endif
void do_dpk_8821c(void *dm_void, u8 delta_thermal_index, u8 thermal_value,
		  u8 threshold)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	/*boolean		is_recovery = (boolean) delta_thermal_index;*/
	phy_dp_calibrate_8821c(dm, true);
}

boolean
_iqk_check_nctl_done_8821c(struct dm_struct *dm, u8 path, u32 IQK_CMD)
{
	/*this function is only used after the version of nctl8.0*/
	boolean notready = true;
	boolean fail = true;
	u32 delay_count = 0x0;

	while (notready) {
		if ((IQK_CMD & 0x00000f00) >> 8 == 0xc) {
			if (odm_get_rf_reg(dm, path, RF_0x08, RFREGOFFSETMASK) == 0x1a3b5)
				notready = false;
			else
				notready = true;
		} else {
			if (odm_get_rf_reg(dm, path, RF_0x08, RFREGOFFSETMASK) == 0x12345)
				notready = false;
			else
				notready = true;
		}
		if (notready) {
			/*ODM_sleep_ms(1);*/
			ODM_delay_us(10);
			delay_count++;
		} else {
			fail = (boolean)odm_get_bb_reg(dm, R_0x1b08, BIT(26));
			if (fail) {
				RF_DBG(dm, DBG_RF_IQK,
				       "[IQK](1)IQK_CMD =0x%x, Fail, 0x1b08=0x%x, RF08=0x%x, 1b00=0x%x,fail=0x%x, notready=0x%x!!!\n",
				       IQK_CMD, odm_read_4byte(dm, 0x1b08),
				       odm_get_rf_reg(dm, path, RF_0x08,
						      RFREGOFFSETMASK),
				       odm_read_4byte(dm, 0x1b00), fail,
						      notready);
			}
			break;
		}
		if (delay_count >= 50000) {
			RF_DBG(dm, DBG_RF_IQK, "[IQK]S%d IQK timeout!!!\n",
			       path);
			break;
		}
	}
	odm_set_rf_reg(dm, path, RF_0x8, RFREGOFFSETMASK, 0x0);

	if (!fail) {
		RF_DBG(dm, DBG_RF_IQK,
		       "[IQK]IQK_CMD =0x%x, delay_count =0x%x RF0x08=0x%x, 0x1b08=0x%x,RF0xef=0x%x,RF0xdf=0x%x, !!!\n",
		       IQK_CMD, delay_count,
		       odm_get_rf_reg(dm, path, RF_0x8, RFREGOFFSETMASK),
		       odm_read_4byte(dm, 0x1b08),
		       odm_get_rf_reg(dm, path, RF_0xef, RFREGOFFSETMASK),
		       odm_get_rf_reg(dm, path, RF_0xdf, RFREGOFFSETMASK));
	} else {
		RF_DBG(dm, DBG_RF_IQK,
		       "[IQK](2)IQK_CMD =0x%x, Fail, 0x1b08=0x%x, RF08=0x%x!!!\n",
		       IQK_CMD, odm_read_4byte(dm, 0x1b08),
		       odm_get_rf_reg(dm, path, RF_0x08, RFREGOFFSETMASK));
	}
	return fail;
}

void phydm_get_read_counter_8821c(struct dm_struct *dm)
{
	u32 counter = 0x0;

	while (1) {
		if ((odm_get_rf_reg(dm, RF_PATH_A, RF_0x8, RFREGOFFSETMASK) == 0xabcde) || counter > 300)
			break;
		counter++;
		/*ODM_sleep_ms(1);*/
		ODM_delay_us(10);
	};
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x8, RFREGOFFSETMASK, 0x0);
	RF_DBG(dm, DBG_RF_IQK, "[IQK]counter = %d\n", counter);
}

void _iqk_check_coex_status(struct dm_struct *dm, boolean beforeK)
{
	u8 u1b_tmp;
	u16 count = 0;
	u8 h2c_parameter;

	h2c_parameter = 1;

	if (beforeK) {
		u1b_tmp = odm_read_1byte(dm, 0x49c);
		RF_DBG(dm, DBG_RF_IQK,
		       "[IQK]check 0x49c[0] = 0x%x before h2c 0x6d\n", u1b_tmp);

		/*check if BT IQK */
		u1b_tmp = odm_read_1byte(dm, 0x49c);
		while ((u1b_tmp & BIT(1)) && (count < 100)) {
			/*ODM_sleep_ms(10);*/
			ODM_delay_us(10);
			u1b_tmp = odm_read_1byte(dm, 0x49c);
			count++;
			RF_DBG(dm, DBG_RF_IQK,
			       "[IQK]check 0x49c[1]=0x%x, count = %d\n",
			       u1b_tmp, count);
		}
#if 1
		odm_fill_h2c_cmd(dm, ODM_H2C_WIFI_CALIBRATION, 1, &h2c_parameter);

		u1b_tmp = odm_read_1byte(dm, 0x49c);
		RF_DBG(dm, DBG_RF_IQK,
		       "[IQK]check 0x49c[0] = 0x%x after h2c 0x6d\n", u1b_tmp);

		u1b_tmp = odm_read_1byte(dm, 0x49c);
		/*check if WL IQK available form WL FW */
		while ((!(u1b_tmp & BIT(0))) && (count < 100)) {
#if 0
			/*ODM_sleep_ms(10);*/
#endif
			ODM_delay_us(10);
			u1b_tmp = odm_read_1byte(dm, 0x49c);
			count++;
			RF_DBG(dm, DBG_RF_IQK,
			       "[IQK]check 0x49c[1]=0x%x, count = %d\n",
			       u1b_tmp, count);
		}

		if (count >= 10000)
			RF_DBG(dm, DBG_RF_IQK,
			       "[IQK]Polling 0x49c to 1 for WiFi calibration H2C cmd FAIL! count(%d)\n",
			       count);
#endif

	} else {
		odm_set_bb_reg(dm, R_0x49c, BIT(0), 0x0);
	}
}

u32 _iqk_indirect_read_reg(struct dm_struct *dm, u16 reg_addr)
{
	u32 j = 0;

	/*wait for ready bit before access 0x1700*/
	odm_write_4byte(dm, 0x1700, 0x800f0000 | reg_addr);

	do {
		j++;
	} while (((odm_read_1byte(dm, 0x1703) & BIT(5)) == 0) && (j < 30000));

	return odm_read_4byte(dm, 0x1708); /*get read data*/
}

void _iqk_indirect_write_reg(struct dm_struct *dm, u16 reg_addr, u32 bit_mask,
			     u32 reg_value)
{
	u32 val, i = 0, j = 0, bitpos = 0;

	if (bit_mask == 0x0)
		return;
	if (bit_mask == 0xffffffff) {
		odm_write_4byte(dm, 0x1704, reg_value); /*put write data*/

		/*wait for ready bit before access 0x1700*/
		do {
			j++;
		} while (((odm_read_1byte(dm, 0x1703) & BIT(5)) == 0) && (j < 30000));

		odm_write_4byte(dm, 0x1700, 0xc00f0000 | reg_addr);
	} else {
		for (i = 0; i <= 31; i++) {
			if (((bit_mask >> i) & 0x1) == 0x1) {
				bitpos = i;
				break;
			}
		}

		/*read back register value before write*/
		val = _iqk_indirect_read_reg(dm, reg_addr);
		val = (val & (~bit_mask)) | (reg_value << bitpos);

		odm_write_4byte(dm, 0x1704, val); /*put write data*/

		/*wait for ready bit before access 0x1700*/
		do {
			j++;
		} while (((odm_read_1byte(dm, 0x1703) & BIT(5)) == 0) && (j < 30000));

		odm_write_4byte(dm, 0x1700, 0xc00f0000 | reg_addr);
	}
}

void _iqk_set_gnt_wl_high(struct dm_struct *dm)
{
	u32 val = 0;
	u8 state = 0x1, sw_control = 0x1;

	/*GNT_WL = 1*/
	val = (sw_control) ? ((state << 1) | 0x1) : 0;
	_iqk_indirect_write_reg(dm, 0x38, 0x3000, val); /*0x38[13:12]*/
	_iqk_indirect_write_reg(dm, 0x38, 0x0300, val); /*0x38[9:8]*/
}

void _iqk_set_gnt_bt_low(struct dm_struct *dm)
{
	u32 val = 0;
	u8 state = 0x0, sw_control = 0x1;

	/*GNT_BT = 0*/
	val = (sw_control) ? ((state << 1) | 0x1) : 0;
	_iqk_indirect_write_reg(dm, 0x38, 0xc000, val); /*0x38[15:14]*/
	_iqk_indirect_write_reg(dm, 0x38, 0x0c00, val); /*0x38[11:10]*/
}

void _iqk_set_gnt_wl_gnt_bt(struct dm_struct *dm, boolean beforeK)
{
	struct dm_iqk_info *iqk_info = &dm->IQK_info;

	if (beforeK) {
		_iqk_set_gnt_wl_high(dm);
		_iqk_set_gnt_bt_low(dm);
	} else {
		_iqk_indirect_write_reg(dm, 0x38, MASKDWORD, iqk_info->tmp_gntwl);
	}
}

void _iqk_fail_count_8821c(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	u8 i;

	dm->n_iqk_cnt++;
	if (odm_get_rf_reg(dm, RF_PATH_A, RF_0x1bf0, BIT(16)) == 1)
		iqk_info->is_reload = true;
	else
		iqk_info->is_reload = false;

	if (!iqk_info->is_reload) {
		for (i = 0; i < 8; i++) {
			if (odm_get_bb_reg(dm, R_0x1bf0, BIT(i)) == 1)
				dm->n_iqk_fail_cnt++;
		}
	}
	RF_DBG(dm, DBG_RF_IQK, "[IQK]All/Fail = %d %d\n", dm->n_iqk_cnt,
	       dm->n_iqk_fail_cnt);
}

void _iqk_fill_iqk_report_8821c(void *dm_void, u8 channel)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	u32 tmp1 = 0x0, tmp2 = 0x0, tmp3 = 0x0;
	u8 i;

	for (i = 0; i < SS_8821C; i++) {
		tmp1 = tmp1 + ((iqk_info->iqk_fail_report[channel][i][TX_IQK] & 0x1) << i);
		tmp2 = tmp2 + ((iqk_info->iqk_fail_report[channel][i][RX_IQK] & 0x1) << (i + 4));
		tmp3 = tmp3 + ((iqk_info->rxiqk_fail_code[channel][i] & 0x3) << (i * 2 + 8));
	}
	odm_write_4byte(dm, 0x1b00, 0xf8000008);
	odm_set_bb_reg(dm, R_0x1bf0, 0x00ffffff, tmp1 | tmp2 | tmp3);

	for (i = 0; i < SS_8821C; i++)
		odm_write_4byte(dm, 0x1be8 + (i * 4), (iqk_info->rxiqk_agc[channel][(i * 2) + 1] << 16) | iqk_info->rxiqk_agc[channel][i * 2]);
	RF_DBG(dm, DBG_RF_IQK, "[IQK] 0x1be8 = %x\n",
	       odm_read_4byte(dm, 0x1be8));
}

void _iqk_iqk_fail_report_8821c(struct dm_struct *dm)
{
	u32 tmp1bf0 = 0x0;
	u8 i;

	tmp1bf0 = odm_read_4byte(dm, 0x1bf0);

	for (i = 0; i < 4; i++) {
		if (tmp1bf0 & (0x1 << i))
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			RF_DBG(dm, DBG_RF_IQK, "[IQK] please check S%d TXIQK\n",
			       i);
#else
			panic_printk("[IQK] please check S%d TXIQK\n", i);
#endif
		if (tmp1bf0 & (0x1 << (i + 12)))
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			RF_DBG(dm, DBG_RF_IQK, "[IQK] please check S%d RXIQK\n",
			       i);
#else
			panic_printk("[IQK] please check S%d RXIQK\n", i);
#endif
	}
}

void _iqk_backup_mac_bb_8821c(struct dm_struct *dm, u32 *MAC_backup,
			      u32 *BB_backup, u32 *backup_mac_reg,
			      u32 *backup_bb_reg, u8 num_backup_bb_reg)
{
	u32 i;

	for (i = 0; i < MAC_REG_NUM_8821C; i++)
		MAC_backup[i] = odm_read_4byte(dm, backup_mac_reg[i]);

	for (i = 0; i < num_backup_bb_reg; i++)
		BB_backup[i] = odm_read_4byte(dm, backup_bb_reg[i]);
#if 0
	/*	RF_DBG(dm, DBG_RF_IQK, "[IQK]BackupMacBB Success!!!!\n"); */
#endif
}

void _iqk_backup_rf_8821c(struct dm_struct *dm, u32 RF_backup[][SS_8821C],
			  u32 *backup_rf_reg)
{
	u32 i, j;

	for (i = 0; i < RF_REG_NUM_8821C; i++)
		for (j = 0; j < SS_8821C; j++)
			RF_backup[i][j] = odm_get_rf_reg(dm, (u8)j, backup_rf_reg[i], RFREGOFFSETMASK);
#if 0
	/*	RF_DBG(dm, DBG_RF_IQK, "[IQK]BackupRF Success!!!!\n"); */
#endif
}

void _iqk_agc_bnd_int_8821c(struct dm_struct *dm)
{
	/*initialize RX AGC bnd, it must do after bbreset*/
	odm_write_4byte(dm, 0x1b00, 0xf8000008);
	odm_write_4byte(dm, 0x1b00, 0xf80a7008);
	odm_write_4byte(dm, 0x1b00, 0xf8015008);
	odm_write_4byte(dm, 0x1b00, 0xf8000008);
#if 0
	/*RF_DBG(dm, DBG_RF_IQK, "[IQK]init. rx agc bnd\n");*/
#endif
}

void _iqk_bb_reset_8821c(struct dm_struct *dm)
{
	boolean cca_ing = false;
	u32 count = 0;

	odm_set_rf_reg(dm, RF_PATH_A, RF_0x0, RFREGOFFSETMASK, 0x10000);
	odm_set_bb_reg(dm, R_0x8f8,
		       BIT(27) | BIT26 | BIT25 | BIT24 | BIT23 | BIT22 | BIT21 | BIT20, 0x0);

	while (1) {
		odm_write_4byte(dm, 0x8fc, 0x0);
		odm_set_bb_reg(dm, R_0x198c, 0x7, 0x7);
		cca_ing = (boolean)odm_get_bb_reg(dm, R_0xfa0, BIT(3));

		if (count > 30)
			cca_ing = false;

		if (cca_ing) {
			ODM_sleep_ms(1);
			count++;
		} else {
			odm_write_1byte(dm, 0x808, 0x0); /*RX ant off*/
			odm_set_bb_reg(dm, R_0xa04, BIT(27) | BIT26 | BIT25 | BIT24, 0x0); /*CCK RX path off*/

			/*BBreset*/
			odm_set_bb_reg(dm, R_0x0, BIT(16), 0x0);
			odm_set_bb_reg(dm, R_0x0, BIT(16), 0x1);

			if (odm_get_bb_reg(dm, R_0x660, BIT(16)))
				odm_write_4byte(dm, 0x6b4, 0x89000006);
			RF_DBG(dm, DBG_RF_IQK, "[IQK]BBreset!!!!\n");
			break;
		}
	}
}

void _iqk_afe_setting_8821c(struct dm_struct *dm, boolean do_iqk)
{
	if (do_iqk) {
		/*IQK AFE setting RX_WAIT_CCA mode */
		odm_write_4byte(dm, 0xc60, 0x50000000);
		odm_write_4byte(dm, 0xc60, 0x700F0040);

		/*AFE setting*/
		odm_write_4byte(dm, 0xc58, 0xd8000402);
		odm_write_4byte(dm, 0xc5c, 0xd1000120);
		odm_write_4byte(dm, 0xc6c, 0x00000a15);
		_iqk_bb_reset_8821c(dm);
#if 0
		/*		RF_DBG(dm, DBG_RF_IQK, "[IQK]AFE setting for IQK mode!!!!\n"); */
#endif
	} else {
		/*IQK AFE setting RX_WAIT_CCA mode */
		odm_write_4byte(dm, 0xc60, 0x50000000);
		odm_write_4byte(dm, 0xc60, 0x700B8040);

		/*AFE setting*/
		odm_write_4byte(dm, 0xc58, 0xd8020402);
		odm_write_4byte(dm, 0xc5c, 0xde000120);
		odm_write_4byte(dm, 0xc6c, 0x0000122a);
#if 0
		/*		RF_DBG(dm, DBG_RF_IQK, "[IQK]AFE setting for Normal mode!!!!\n"); */
#endif
	}
	/*0x9a4[31]=0: Select da clock*/
	odm_set_bb_reg(dm, R_0x9a4, BIT(31), 0x0);
}

void _iqk_restore_mac_bb_8821c(struct dm_struct *dm, u32 *MAC_backup,
			       u32 *BB_backup, u32 *backup_mac_reg,
			       u32 *backup_bb_reg, u8 num_backup_bb_reg)
{
	u32 i;

	for (i = 0; i < MAC_REG_NUM_8821C; i++)
		odm_write_4byte(dm, backup_mac_reg[i], MAC_backup[i]);
	for (i = 0; i < num_backup_bb_reg; i++)
		odm_write_4byte(dm, backup_bb_reg[i], BB_backup[i]);
#if 0
	/*	RF_DBG(dm, DBG_RF_IQK, "[IQK]RestoreMacBB Success!!!!\n"); */
#endif
}

void _iqk_restore_rf_8821c(struct dm_struct *dm, u32 *backup_rf_reg,
			   u32 RF_backup[][SS_8821C])
{
	u32 i;

	odm_set_rf_reg(dm, RF_PATH_A, RF_0xef, RFREGOFFSETMASK, 0x0);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xee, RFREGOFFSETMASK, 0x0);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xdf, RFREGOFFSETMASK, RF_backup[0][RF_PATH_A] & (~BIT(4)));
#if 0
	/*odm_set_rf_reg(dm, RF_PATH_A, RF_0xde, RFREGOFFSETMASK, RF_backup[1][RF_PATH_A]|BIT(4));*/
#endif
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xde, RFREGOFFSETMASK, RF_backup[1][RF_PATH_A] & (~BIT(4)));

	for (i = 2; i < (RF_REG_NUM_8821C - 1); i++)
		odm_set_rf_reg(dm, RF_PATH_A, backup_rf_reg[i], RFREGOFFSETMASK, RF_backup[i][RF_PATH_A]);

	odm_set_rf_reg(dm, RF_PATH_A, RF_0x1, RFREGOFFSETMASK, (RF_backup[4][RF_PATH_A] & (~BIT(0))));
#if 0
	/*RF_DBG(dm, DBG_RF_IQK, "[IQK]RestoreRF Success!!!!\n"); */
#endif
}

void _iqk_backup_iqk_8821c(struct dm_struct *dm, u8 step, u8 path)
{
	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	u8 i, j, k;
#if 0
	/*u16		iqk_apply[2] = {0xc94, 0xe94};*/
#endif

	switch (step) {
	case 0:
		iqk_info->iqk_channel[1] = iqk_info->iqk_channel[0];
		for (i = 0; i < SS_8821C; i++) {
			iqk_info->lok_idac[1][i] = iqk_info->lok_idac[0][i];
			iqk_info->rxiqk_agc[1][i] = iqk_info->rxiqk_agc[0][i];
			iqk_info->bypass_iqk[1][i] = iqk_info->bypass_iqk[0][i];
			iqk_info->rxiqk_fail_code[1][i] = iqk_info->rxiqk_fail_code[0][i];
			for (j = 0; j < 2; j++) {
				iqk_info->iqk_fail_report[1][i][j] = iqk_info->iqk_fail_report[0][i][j];
				for (k = 0; k < 8; k++) {
					iqk_info->iqk_cfir_real[1][i][j][k] = iqk_info->iqk_cfir_real[0][i][j][k];
					iqk_info->iqk_cfir_imag[1][i][j][k] = iqk_info->iqk_cfir_imag[0][i][j][k];
				}
			}
		}
		for (i = 0; i < SS_8821C; i++) {
			iqk_info->rxiqk_fail_code[0][i] = 0x0;
			iqk_info->rxiqk_agc[0][i] = 0x0;
			for (j = 0; j < 2; j++) {
				iqk_info->iqk_fail_report[0][i][j] = true;
				iqk_info->gs_retry_count[0][i][j] = 0x0;
			}
			for (j = 0; j < 3; j++)
				iqk_info->retry_count[0][i][j] = 0x0;
		}
		/*backup channel*/
		iqk_info->iqk_channel[0] = iqk_info->rf_reg18;
		break;
	case 1: /*LOK backup*/
		iqk_info->lok_idac[0][path] = odm_get_rf_reg(dm, (enum rf_path)path, RF_0x58, RFREGOFFSETMASK);
		break;
	case 2: /*TXIQK backup*/
	case 3: /*RXIQK backup*/
		phydm_get_iqk_cfir(dm, (step - 2), path, false);
		break;
	}
}

void _iqk_reload_iqk_setting_8821c(struct dm_struct *dm, u8 channel,
				   u8 reload_idx
				   /*1: reload TX, 2: reload TX, RX*/)
{
	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	u8 i, path, idx;
	u16 iqk_apply[2] = {0xc94, 0xe94};

	for (path = 0; path < SS_8821C; path++) {
#if 0
		if (reload_idx == 2) {
			odm_set_rf_reg(dm, (enum rf_path)path, RF_0xdf, BIT(4), 0x1);
			odm_set_rf_reg(dm, (enum rf_path)path, RF_0x58, RFREGOFFSETMASK, iqk_info->lok_idac[channel][path]);
		}
#endif
		for (idx = 0; idx < reload_idx; idx++) {
			odm_set_bb_reg(dm, R_0x1b00, MASKDWORD, 0xf8000008 | path << 1);
			odm_set_bb_reg(dm, R_0x1b2c, MASKDWORD, 0x7);
			odm_set_bb_reg(dm, R_0x1b38, MASKDWORD, 0x20000000);
			odm_set_bb_reg(dm, R_0x1b3c, MASKDWORD, 0x20000000);
			odm_set_bb_reg(dm, R_0x1bcc, MASKDWORD, 0x00000000);
			if (idx == 0)
				odm_set_bb_reg(dm, R_0x1b0c, BIT(13) | BIT(12), 0x3);
			else
				odm_set_bb_reg(dm, R_0x1b0c, BIT(13) | BIT(12), 0x1);
			odm_set_bb_reg(dm, R_0x1bd4, BIT(20) | BIT(19) | BIT(18) | BIT(17) | BIT(16), 0x10);
			for (i = 0; i < 8; i++) {
				odm_write_4byte(dm, 0x1bd8, ((0xc0000000 >> idx) + 0x3) + (i * 4) + (iqk_info->iqk_cfir_real[channel][path][idx][i] << 9));
				odm_write_4byte(dm, 0x1bd8, ((0xc0000000 >> idx) + 0x1) + (i * 4) + (iqk_info->iqk_cfir_imag[channel][path][idx][i] << 9));
			}
			if (idx == 0)
				odm_set_bb_reg(dm, iqk_apply[path], BIT(0), !(iqk_info->iqk_fail_report[channel][path][idx]));
			else
				odm_set_bb_reg(dm, iqk_apply[path], BIT(10), !(iqk_info->iqk_fail_report[channel][path][idx]));
		}
		odm_set_bb_reg(dm, R_0x1bd8, MASKDWORD, 0x0);
		odm_set_bb_reg(dm, R_0x1b0c, BIT(13) | BIT(12), 0x0);
	}
}

boolean
_iqk_reload_iqk_8821c(struct dm_struct *dm, boolean reset)
{
	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	u8 i;
	iqk_info->is_reload = false;
	odm_set_bb_reg(dm, R_0x1bf0, BIT(16), 0x0); /*clear the reload flag*/

	if (reset) {
		for (i = 0; i < SS_8821C; i++)
			iqk_info->iqk_channel[i] = 0x0;
	} else {
		iqk_info->rf_reg18 = odm_get_rf_reg(dm, RF_PATH_A, RF_0x18, RFREGOFFSETMASK);

		for (i = 0; i < SS_8821C; i++) {
			if (iqk_info->rf_reg18 == iqk_info->iqk_channel[i]) {
				_iqk_reload_iqk_setting_8821c(dm, i, 2);
				_iqk_fill_iqk_report_8821c(dm, i);
				RF_DBG(dm, DBG_RF_IQK,
				       "[IQK]reload IQK result before!!!!\n");
				odm_set_bb_reg(dm, R_0x1bf0, BIT(16), 0x1);
				iqk_info->is_reload = true;
			}
		}
	}

	return iqk_info->is_reload;
}

void _iqk_rfe_setting_8821c(struct dm_struct *dm, boolean ext_pa_on)
{
	if (ext_pa_on) {
		/*RFE setting*/
		odm_write_4byte(dm, 0xcb0, 0x77777777);
		odm_write_4byte(dm, 0xcb4, 0x00007777);
		odm_write_4byte(dm, 0xcbc, 0x0000083B);
#if 0
		/*odm_write_4byte(dm, 0x1990, 0x00000c30);*/
#endif
		RF_DBG(dm, DBG_RF_IQK, "[IQK]external PA on!!!!\n");
	} else {
		/*RFE setting*/
		odm_write_4byte(dm, 0xcb0, 0x77171117);
		odm_write_4byte(dm, 0xcb4, 0x00001177);
		odm_write_4byte(dm, 0xcbc, 0x00000404);
#if 0
		/*odm_write_4byte(dm, 0x1990, 0x00000c30);*/
#endif
		RF_DBG(dm, DBG_RF_IQK, "[IQK]external PA off!!!!\n");
	}
}

void _iqk_rfsetting_8821c(struct dm_struct *dm)
{
	struct dm_iqk_info *iqk_info = &dm->IQK_info;

	u8 path;
	u32 tmp;

	odm_write_4byte(dm, 0x1b00, 0xf8000008);
	odm_write_4byte(dm, 0x1bb8, 0x00000000);

	for (path = 0; path < SS_8821C; path++) {
		/*0xdf:B11 = 1,B4 = 0, B1 = 1*/
		tmp = odm_get_rf_reg(dm, (enum rf_path)path, RF_0xdf, RFREGOFFSETMASK);
		tmp = (tmp & (~BIT(4))) | BIT(1) | BIT(11);
		odm_set_rf_reg(dm, (enum rf_path)path, RF_0xdf, RFREGOFFSETMASK, tmp);

		if (iqk_info->is_btg) {
			tmp = odm_get_rf_reg(dm, RF_PATH_A, RF_0xde, RFREGOFFSETMASK);
			tmp = (tmp & (~BIT(4))) | BIT(15);
#if 0
			/*tmp = tmp|BIT(4)|BIT(15); //manual LOK value  for A-cut*/
#endif
			odm_set_rf_reg(dm, RF_PATH_A, RF_0xde, RFREGOFFSETMASK, tmp);
		}

		if (!iqk_info->is_btg) {
			/*WLAN_AG*/
			/*TX IQK	 mode init*/
			odm_set_rf_reg(dm, (enum rf_path)path, RF_0xef, RFREGOFFSETMASK, 0x80000);
			odm_set_rf_reg(dm, (enum rf_path)path, RF_0x33, RFREGOFFSETMASK, 0x00024);
			odm_set_rf_reg(dm, (enum rf_path)path, RF_0x3e, RFREGOFFSETMASK, 0x0003f);
#if 0
			/*odm_set_rf_reg(dm, (enum rf_path)path, RF_0x3f, RFREGOFFSETMASK, 0x60fde);*/
#endif
			odm_set_rf_reg(dm, (enum rf_path)path, RF_0x3f, RFREGOFFSETMASK, 0xe0fde);
			odm_set_rf_reg(dm, (enum rf_path)path, RF_0xef, RFREGOFFSETMASK, 0x00000);
			if (*dm->band_type == ODM_BAND_5G) {
				odm_set_rf_reg(dm, (enum rf_path)path, RF_0xef, BIT(19), 0x1);
				odm_set_rf_reg(dm, (enum rf_path)path, RF_0x33, RFREGOFFSETMASK, 0x00026);
				odm_set_rf_reg(dm, (enum rf_path)path, RF_0x3e, RFREGOFFSETMASK, 0x00037);
				odm_set_rf_reg(dm, (enum rf_path)path, RF_0x3f, RFREGOFFSETMASK, 0xdefce);
				odm_set_rf_reg(dm, (enum rf_path)path, RF_0xef, BIT(19), 0x0);
			} else {
				odm_set_rf_reg(dm, (enum rf_path)path, RF_0xef, BIT(19), 0x1);
				odm_set_rf_reg(dm, (enum rf_path)path, RF_0x33, RFREGOFFSETMASK, 0x00026);
				odm_set_rf_reg(dm, (enum rf_path)path, RF_0x3e, RFREGOFFSETMASK, 0x00037);
				odm_set_rf_reg(dm, (enum rf_path)path, RF_0x3f, RFREGOFFSETMASK, 0x5efce);
				odm_set_rf_reg(dm, (enum rf_path)path, RF_0xef, BIT(19), 0x0);
			}
		} else {
			/*WLAN_BTG*/
			/*TX IQK	 mode init*/
			odm_set_rf_reg(dm, (enum rf_path)path, RF_0xee, RFREGOFFSETMASK, 0x01000);
			odm_set_rf_reg(dm, (enum rf_path)path, RF_0x33, RFREGOFFSETMASK, 0x00004);
			odm_set_rf_reg(dm, (enum rf_path)path, RF_0x3f, RFREGOFFSETMASK, 0x01ec1);
			odm_set_rf_reg(dm, (enum rf_path)path, RF_0xee, RFREGOFFSETMASK, 0x00000);
		}
	}
}

void _iqk_configure_macbb_8821c(struct dm_struct *dm)
{
	/*MACBB register setting*/
	odm_write_1byte(dm, 0x522, 0x7f);
	odm_set_bb_reg(dm, R_0x1518, BIT(16), 0x1);
	odm_set_bb_reg(dm, R_0x550, BIT(11) | BIT(3), 0x0);
	odm_set_bb_reg(dm, R_0x90c, BIT(15), 0x1); /*0x90c[15]=1: dac_buf reset selection*/

	/*0xc94[0]=1, 0xe94[0]=1: Let tx from IQK*/
	odm_set_bb_reg(dm, R_0xc94, BIT(0), 0x1);
	odm_set_bb_reg(dm, R_0xc94, (BIT(11) | BIT(10)), 0x1);
	/* 3-wire off*/
	odm_write_4byte(dm, 0xc00, 0x00000004);
	/*disable PMAC*/
	odm_set_bb_reg(dm, R_0xb00, BIT(8), 0x0);
#if 0
	/*	RF_DBG(dm, DBG_RF_IQK, "[IQK]Set MACBB setting for IQK!!!!\n");*/
#endif
	/*disable CCK block*/
	odm_set_bb_reg(dm, R_0x808, BIT(28), 0x0);
	/*disable OFDM CCA*/
	odm_set_bb_reg(dm, R_0x838, BIT(3) | BIT(2) | BIT(1), 0x7);

}

void _iqk_lok_setting_8821c(struct dm_struct *dm, u8 path, u8 pad_index)
{
	u32 LOK0x56_2G = 0x50ef3;
	u32 LOK0x56_5G = 0x50ee8;
	u32 LOK0x33 = 0;
	u32 LOK0x78 = 0xbcbba;
	u32 tmp = 0;

	struct dm_iqk_info *iqk_info = &dm->IQK_info;

	LOK0x33 = pad_index;
	/*add delay of MAC send packet*/

	if (*dm->mp_mode)
		odm_set_bb_reg(dm, R_0x810, BIT(7) | BIT(6) | BIT(5) | BIT(4), 0x8);

	if (iqk_info->is_btg) {
		tmp = (LOK0x78 & 0x1c000) >> 14;
		odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);
		odm_write_4byte(dm, 0x1bcc, 0x1b);
		odm_write_1byte(dm, 0x1b23, 0x00);
		odm_write_1byte(dm, 0x1b2b, 0x80);
		/*0x78[11:0] = IDAC value*/
		LOK0x78 = LOK0x78 & (0xe3fff | ((u32)pad_index << 14));
		odm_set_rf_reg(dm, path, RF_0x78, RFREGOFFSETMASK, LOK0x78);
		odm_set_rf_reg(dm, path, RF_0x5c, RFREGOFFSETMASK, 0x05320);
		odm_set_rf_reg(dm, path, RF_0x8f, RFREGOFFSETMASK, 0xac018);
		odm_set_rf_reg(dm, RF_PATH_A, RF_0xee, BIT(4), 0x1);
		odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, BIT(3), 0x0);
		RF_DBG(dm, DBG_RF_IQK, "[IQK] In the BTG\n");
	} else {
		/*tmp = (LOK0x56 & 0xe0) >> 5;*/
		odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);
		odm_write_4byte(dm, 0x1bcc, 0x9);
		odm_write_1byte(dm, 0x1b23, 0x00);

		switch (*dm->band_type) {
		case ODM_BAND_2_4G:
			odm_write_1byte(dm, 0x1b2b, 0x00);
			LOK0x56_2G = LOK0x56_2G & (0xfff1f | ((u32)pad_index << 5));
			odm_set_rf_reg(dm, path, RF_0x56, RFREGOFFSETMASK, LOK0x56_2G);
			odm_set_rf_reg(dm, path, RF_0x8f, RFREGOFFSETMASK, 0xadc18);
			odm_set_rf_reg(dm, RF_PATH_A, RF_0xef, BIT(4), 0x1);
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, BIT(3), 0x0);
			break;
		case ODM_BAND_5G:
			odm_write_1byte(dm, 0x1b2b, 0x00);
			LOK0x56_5G = LOK0x56_5G & (0xfff1f | ((u32)pad_index << 5));
			odm_set_rf_reg(dm, path, RF_0x56, RFREGOFFSETMASK, LOK0x56_5G);
			odm_set_rf_reg(dm, path, RF_0x8f, RFREGOFFSETMASK, 0xadc18);
			odm_set_rf_reg(dm, RF_PATH_A, RF_0xef, BIT(4), 0x1);
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, BIT(3), 0x1);
			break;
		}
	}
	/*for IDAC LUT by PAD idx*/
	odm_set_rf_reg(dm, path, RF_0x33, BIT(2) | BIT(1) | BIT(0), LOK0x33);
#if 0
	/*	RF_DBG(dm, DBG_RF_IQK, "[IQK]Set LOK setting!!!!\n");*/
#endif
}

void _iqk_txk_setting_8821c(struct dm_struct *dm, u8 path)
{
	struct dm_iqk_info *iqk_info = &dm->IQK_info;

	if (iqk_info->is_btg) {
		odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);
		odm_write_4byte(dm, 0x1bcc, 0x1b);
		odm_write_4byte(dm, 0x1b20, 0x00840008);

		/*0x78[11:0] = IDAC value*/
		odm_set_rf_reg(dm, path, RF_0x78, RFREGOFFSETMASK, 0xbcbba);
		odm_set_rf_reg(dm, path, RF_0x5c, RFREGOFFSETMASK, 0x04320);
		odm_set_rf_reg(dm, path, RF_0x8f, RFREGOFFSETMASK, 0xac018);
		odm_write_1byte(dm, 0x1b2b, 0x80);
	} else {
		odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);
		odm_write_4byte(dm, 0x1bcc, 0x9);
		odm_write_4byte(dm, 0x1b20, 0x01440008);

		switch (*dm->band_type) {
		case ODM_BAND_2_4G:
			odm_set_rf_reg(dm, path, RF_0x56, RFREGOFFSETMASK, 0x50EF3);
			odm_set_rf_reg(dm, path, RF_0x8f, RFREGOFFSETMASK, 0xadc18);
			odm_write_1byte(dm, 0x1b2b, 0x00);
			break;
		case ODM_BAND_5G:
#if 0
			/*odm_set_rf_reg(dm, path, RF_0x56, RFREGOFFSETMASK, 0x50EF0);*/
			/*odm_set_rf_reg(dm, path, RF_0x56, RFREGOFFSETMASK, 0x50Ec8);*/
#endif
			odm_set_rf_reg(dm, path, RF_0x56, RFREGOFFSETMASK, 0x5004e);
			odm_set_rf_reg(dm, path, RF_0x8f, RFREGOFFSETMASK, 0xa9c18);
			odm_write_1byte(dm, 0x1b2b, 0x00);
			break;
		}
	}
#if 0
	/*	RF_DBG(dm, DBG_RF_IQK, "[IQK]Set TXK setting!!!!\n");*/
#endif
}

void _iqk_rxk1setting_8821c(struct dm_struct *dm, u8 path)
{
	struct dm_iqk_info *iqk_info = &dm->IQK_info;

	if (iqk_info->is_btg) {
		odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);
		odm_write_1byte(dm, 0x1b2b, 0x80);
		odm_write_4byte(dm, 0x1bcc, 0x09);
		odm_write_4byte(dm, 0x1b20, 0x01450008);
		odm_write_4byte(dm, 0x1b24, 0x01460c88);

		/*0x78[11:0] = IDAC value*/
		odm_set_rf_reg(dm, path, RF_0x78, RFREGOFFSETMASK, 0x8cbba);
		odm_set_rf_reg(dm, path, RF_0x5c, RFREGOFFSETMASK, 0x00320);
		odm_set_rf_reg(dm, path, RF_0x8f, RFREGOFFSETMASK, 0xa8018);
	} else {
		odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);
		switch (*dm->band_type) {
		case ODM_BAND_2_4G:
			odm_write_1byte(dm, 0x1bcc, 0x12);
			odm_write_1byte(dm, 0x1b2b, 0x00);
			odm_write_4byte(dm, 0x1b20, 0x01450008);
			odm_write_4byte(dm, 0x1b24, 0x01461068);

			odm_set_rf_reg(dm, path, RF_0x56, RFREGOFFSETMASK, 0x510f3);
			odm_set_rf_reg(dm, path, RF_0x8f, RFREGOFFSETMASK, 0xa9c00);
			break;
		case ODM_BAND_5G:
			odm_write_1byte(dm, 0x1bcc, 0x9);
			odm_write_1byte(dm, 0x1b2b, 0x00);
			odm_write_4byte(dm, 0x1b20, 0x00450008);
			odm_write_4byte(dm, 0x1b24, 0x00461468);

			odm_set_rf_reg(dm, path, RF_0x56, RFREGOFFSETMASK, 0x510f3);
			odm_set_rf_reg(dm, path, RF_0x8f, RFREGOFFSETMASK, 0xa9c00);
			break;
		}
	}
#if 0
	/*RF_DBG(dm, DBG_RF_IQK, "[IQK]Set RXK setting!!!!\n");*/
#endif
}

static u8 btg_lna[5] = {0x0, 0x4, 0x8, 0xc, 0xf};
static u8 wlg_lna[5] = {0x0, 0x1, 0x2, 0x3, 0x5};
static u8 wla_lna[5] = {0x0, 0x1, 0x3, 0x4, 0x5};

void _iqk_rxk2setting_8821c(struct dm_struct *dm, u8 path, boolean is_gs)
{
	struct dm_iqk_info *iqk_info = &dm->IQK_info;

	if (iqk_info->is_btg) {
		if (is_gs) {
			iqk_info->tmp1bcc = 0x1b;
			iqk_info->lna_idx = 2;
		}
		odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);
		odm_write_1byte(dm, 0x1b2b, 0x80);
		odm_write_4byte(dm, 0x1bcc, iqk_info->tmp1bcc);
		odm_write_4byte(dm, 0x1b20, 0x01450008);
		odm_write_4byte(dm, 0x1b24, (0x01460048 | (btg_lna[iqk_info->lna_idx] << 10)));
		/*0x78[11:0] = IDAC value*/
		odm_set_rf_reg(dm, path, RF_0x78, RFREGOFFSETMASK, 0x8cbba);
		odm_set_rf_reg(dm, path, RF_0x5c, RFREGOFFSETMASK, 0x00320);
		odm_set_rf_reg(dm, path, RF_0x8f, RFREGOFFSETMASK, 0xa8018);
	} else {
		odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);
		switch (*dm->band_type) {
		case ODM_BAND_2_4G:
			if (is_gs) {
				iqk_info->tmp1bcc = 0x12;
				iqk_info->lna_idx = 2;
			}
			odm_write_1byte(dm, 0x1bcc, iqk_info->tmp1bcc);
			odm_write_1byte(dm, 0x1b2b, 0x00);
			odm_write_4byte(dm, 0x1b20, 0x01450008);
			odm_write_4byte(dm, 0x1b24, (0x01460048 | (wlg_lna[iqk_info->lna_idx] << 10)));
			odm_set_rf_reg(dm, path, RF_0x56, RFREGOFFSETMASK, 0x510f3);
			odm_set_rf_reg(dm, path, RF_0x8f, RFREGOFFSETMASK, 0xa9c00);
			break;
		case ODM_BAND_5G:
			if (is_gs) {
				/*iqk_info->tmp1bcc = 0x12;*/
				iqk_info->tmp1bcc = 0x09;
				iqk_info->lna_idx = 2;
			}
			odm_write_1byte(dm, 0x1bcc, iqk_info->tmp1bcc);
			odm_write_1byte(dm, 0x1b2b, 0x00);
			odm_write_4byte(dm, 0x1b20, 0x00450008);
			odm_write_4byte(dm, 0x1b24, (0x01460048 | (wla_lna[iqk_info->lna_idx] << 10)));
#if 0
			/*odm_set_rf_reg(dm, path, RF_0x56, RFREGOFFSETMASK, 0x51000);*/
#endif
			odm_set_rf_reg(dm, path, RF_0x56, RFREGOFFSETMASK, 0x51060);
			odm_set_rf_reg(dm, path, RF_0x8f, RFREGOFFSETMASK, 0xa9c00);
			break;
		}
	}
#if 0
	/*	RF_DBG(dm, DBG_RF_IQK, "[IQK]Set RXK setting!!!!\n");*/
#endif
}

boolean
_iqk_check_cal_8821c(struct dm_struct *dm, u32 IQK_CMD)
{
	boolean notready = true, fail = true;
	u32 delay_count = 0x0;

	while (notready) {
		if (odm_read_4byte(dm, 0x1b00) == (IQK_CMD & 0xffffff0f)) {
			fail = (boolean)odm_get_bb_reg(dm, R_0x1b08, BIT(26));
			notready = false;
		} else {
#if 0
			/*ODM_sleep_ms(1);*/
#endif
			ODM_delay_us(10);
			delay_count++;
		}

		if (delay_count >= 50000) {
			fail = true;
			RF_DBG(dm, DBG_RF_IQK, "[IQK]IQK timeout!!!\n");
			break;
		}
	}
#if 0
	/*
	RF_DBG(dm, DBG_RF_IQK,
		     "[IQK]delay count = 0x%x!!!\n", delay_count);
*/
#endif
	return fail;
}

boolean
_iqk_rx_iqk_gain_search_fail_8821c(struct dm_struct *dm, u8 path, u8 step)
{
	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	boolean fail = true;
	u32 IQK_CMD = 0x0, rf_reg0, tmp, rxbb;
	u8 IQMUX[4] = {0x9, 0x12, 0x1b, 0x24}, *plna;
	u8 idx;
	/*u8	lna_setting[5];*/

	if (iqk_info->is_btg)
		plna = btg_lna;
	else if (*dm->band_type == ODM_BAND_2_4G)
		plna = wlg_lna;
	else
		plna = wla_lna;

	for (idx = 0; idx < 4; idx++)
		if (iqk_info->tmp1bcc == IQMUX[idx])
			break;

	odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);
	odm_write_4byte(dm, 0x1bcc, iqk_info->tmp1bcc);

	if (step == RXIQK1)
		RF_DBG(dm, DBG_RF_IQK,
		       "[IQK]============ S%d RXIQK GainSearch ============\n",
		       iqk_info->is_btg);

	if (step == RXIQK1)
		IQK_CMD = 0xf8000208 | (1 << (path + 4));
	else
		IQK_CMD = 0xf8000308 | (1 << (path + 4));

	RF_DBG(dm, DBG_RF_IQK, "[IQK]S%d GS%d_Trigger = 0x%x\n", path, step,
	       IQK_CMD);

	_iqk_set_gnt_wl_gnt_bt(dm, true);
	odm_write_4byte(dm, 0x1b00, IQK_CMD);
	odm_write_4byte(dm, 0x1b00, IQK_CMD + 0x1);
#if 0
	/*ODM_sleep_ms(GS_delay_8821C);*/
#endif
	fail = _iqk_check_cal_8821c(dm, IQK_CMD);
	RF_DBG(dm, DBG_RF_IQK, "[IQK]check 0x49c = %x\n",
	       odm_read_1byte(dm, 0x49c));
	_iqk_set_gnt_wl_gnt_bt(dm, false);

	if (step == RXIQK2) {
		rf_reg0 = odm_get_rf_reg(dm, (enum rf_path)path, RF_0x0, RFREGOFFSETMASK);
		odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);
		RF_DBG(dm, DBG_RF_IQK,
		       "[IQK]S%d ==> RF0x0 = 0x%x, tmp1bcc = 0x%x, idx = %d, 0x1b3c = 0x%x\n",
		       path, rf_reg0, iqk_info->tmp1bcc, idx,
		       odm_read_4byte(dm, 0x1b3c));
		tmp = (rf_reg0 & 0x1fe0) >> 5;
		rxbb = tmp & 0x1f;
#if 1

		if (rxbb == 0x1) {
			if (idx != 3)
				idx++;
			else if (iqk_info->lna_idx != 0x0)
				iqk_info->lna_idx--;
			else
				iqk_info->isbnd = true;
			fail = true;
		} else if (rxbb == 0xa) {
			if (idx != 0)
				idx--;
			else if (iqk_info->lna_idx != 0x4)
				iqk_info->lna_idx++;
			else
				iqk_info->isbnd = true;
			fail = true;
		} else {
			fail = false;
		}

		if (iqk_info->isbnd)
			fail = false;
#endif

#if 0
		if (rxbb == 0x1) {
			if (iqk_info->lna_idx != 0x0)
				iqk_info->lna_idx--;
			else if (idx != 3)
				idx++;
			else
				iqk_info->isbnd = true;
			fail = true;
		} else if (rxbb == 0xa) {
			if (idx != 0)
				idx--;
			else if (iqk_info->lna_idx != 0x7)
				iqk_info->lna_idx++;
			else
				iqk_info->isbnd = true;
			fail = true;
		} else
			fail = false;

		if (iqk_info->isbnd == true)
			fail = false;
#endif
		iqk_info->tmp1bcc = IQMUX[idx];

		if (fail) {
			odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);
			odm_write_4byte(dm, 0x1b24, (odm_read_4byte(dm, 0x1b24) & 0xffffc3ff) | (*(plna + iqk_info->lna_idx) << 10));
		}
	}
	return fail;
}
void _iqk_rxk2setting_by_toneindex_8821c(struct dm_struct *dm, u8 path,
					 boolean is_gs, u8 toneindex)
{
	struct dm_iqk_info *iqk_info = &dm->IQK_info;

	if (iqk_info->is_btg) {
		iqk_info->tmp1bcc = 0x1b;
		iqk_info->lna_idx = 2;
		odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);
		odm_write_1byte(dm, 0x1b2b, 0x80);
		odm_write_4byte(dm, 0x1bcc, iqk_info->tmp1bcc);
		odm_write_4byte(dm, 0x1b20, 0x01450008);
		odm_write_4byte(dm, 0x1b24, (0x01460048 | (btg_lna[iqk_info->lna_idx] << 10)));
		/*0x78[11:0] = IDAC value*/
		odm_set_rf_reg(dm, path, RF_0x78, RFREGOFFSETMASK, 0x8cbba);
		odm_set_rf_reg(dm, path, RF_0x5c, RFREGOFFSETMASK, 0x00320);
		odm_set_rf_reg(dm, path, RF_0x8f, RFREGOFFSETMASK, 0xa8018);
	} else {
		odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);
		switch (*dm->band_type) {
		case ODM_BAND_2_4G:
			iqk_info->tmp1bcc = 0x12;
			iqk_info->lna_idx = 2;
			odm_write_1byte(dm, 0x1bcc, iqk_info->tmp1bcc);
			odm_write_1byte(dm, 0x1b2b, 0x00);
			odm_write_4byte(dm, 0x1b20, 0x01450008);
			odm_write_4byte(dm, 0x1b24, (0x01460048 | (wlg_lna[iqk_info->lna_idx] << 10)));
			odm_set_rf_reg(dm, path, RF_0x56, RFREGOFFSETMASK, 0x510f3);
			odm_set_rf_reg(dm, path, RF_0x8f, RFREGOFFSETMASK, 0xa9c00);
			break;
		case ODM_BAND_5G:
			iqk_info->tmp1bcc = 0x09;
			iqk_info->lna_idx = 2;
			odm_write_1byte(dm, 0x1bcc, iqk_info->tmp1bcc);
			odm_write_1byte(dm, 0x1b2b, 0x00);
			odm_write_4byte(dm, 0x1b20, 0x00450008);
			odm_write_4byte(dm, 0x1b24, (0x01460048 | (wla_lna[iqk_info->lna_idx] << 10)));
#if 0
			/*odm_set_rf_reg(dm, path, RF_0x56, RFREGOFFSETMASK, 0x51000);*/
#endif
			odm_set_rf_reg(dm, path, RF_0x56, RFREGOFFSETMASK, 0x51060);
			odm_set_rf_reg(dm, path, RF_0x8f, RFREGOFFSETMASK, 0xa9c00);
			break;
		}
	}
	odm_write_4byte(dm, 0x1b20, (odm_read_4byte(dm, 0x1b20) & 0x000fffff) | toneindex << 20);
	odm_write_4byte(dm, 0x1b24, (odm_read_4byte(dm, 0x1b24) & 0x000fffff) | toneindex << 20);
}

boolean
_iqk_rx_iqk_gain_search_fail_by_toneindex_8821c(struct dm_struct *dm, u8 path,
						u8 step, u8 tone_index)
{
	boolean fail = true;
	u32 IQK_CMD;
#if 0
	/*u8	lna_setting[5];*/
#endif

	_iqk_rxk2setting_by_toneindex_8821c(dm, path, RXIQK1, tone_index);
	odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);

	IQK_CMD = 0xf8000208 | (1 << (path + 4));

	_iqk_set_gnt_wl_gnt_bt(dm, true);
	odm_write_4byte(dm, 0x1b00, IQK_CMD);
	odm_write_4byte(dm, 0x1b00, IQK_CMD + 0x1);
#if 0
	/*ODM_sleep_ms(GS_delay_8821C);*/
#endif
	fail = _iqk_check_cal_8821c(dm, IQK_CMD);
	_iqk_set_gnt_wl_gnt_bt(dm, false);

	return fail;
}

boolean
_lok_one_shot_8821c(void *dm_void, u8 path, u8 pad_index)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	u8 delay_count = 0;
	boolean LOK_notready = false;
	u32 LOK_temp2 = 0, LOK_temp3 = 0;
	u32 IQK_CMD = 0x0;
#if 0
	/*u8		LOKreg[] = {0x58, 0x78};*/
#endif
	RF_DBG(dm, DBG_RF_IQK, "[IQK]==========S%d LOK ==========\n",
	       iqk_info->is_btg);

	IQK_CMD = 0xf8000008 | (1 << (4 + path));

	RF_DBG(dm, DBG_RF_IQK, "[IQK]LOK_Trigger = 0x%x\n", IQK_CMD);

	_iqk_set_gnt_wl_gnt_bt(dm, true);
	odm_write_4byte(dm, 0x1b00, IQK_CMD);
	odm_write_4byte(dm, 0x1b00, IQK_CMD + 1);
	/*LOK: CMD ID = 0	{0xf8000018, 0xf8000028}*/
	/*LOK: CMD ID = 0	{0xf8000019, 0xf8000029}*/
#if 0
	/*ODM_sleep_ms(LOK_delay_8821C);*/
#endif
	ODM_delay_us(10);

	delay_count = 0;
	LOK_notready = true;

	while (LOK_notready) {
		if (odm_get_rf_reg(dm, path, RF_0x8, RFREGOFFSETMASK) == 0x12345)
			LOK_notready = false;
		else
			LOK_notready = true;

		if (LOK_notready) {
#if 0
			/*ODM_sleep_ms(1);*/
#endif
			ODM_delay_us(10);
			delay_count++;
		}

		if (delay_count >= 50000) {
			RF_DBG(dm, DBG_RF_IQK, "[IQK]S%d LOK timeout!!!\n",
			       path);
			break;
		}
	}
	odm_set_rf_reg(dm, path, RF_0x8, RFREGOFFSETMASK, 0x0);

	_iqk_set_gnt_wl_gnt_bt(dm, false);
	RF_DBG(dm, DBG_RF_IQK, "[IQK]S%d ==> delay_count = 0x%x\n", path,
	       delay_count);

	if (!LOK_notready) {
		LOK_temp2 = odm_get_rf_reg(dm, (enum rf_path)path, RF_0x8, RFREGOFFSETMASK);
		LOK_temp3 = odm_get_rf_reg(dm, (enum rf_path)path, RF_0x58, RFREGOFFSETMASK);

		RF_DBG(dm, DBG_RF_IQK, "[IQK]0x8 = 0x%x, 0x58 = 0x%x\n",
		       LOK_temp2, LOK_temp3);
	} else {
		RF_DBG(dm, DBG_RF_IQK, "[IQK]==>S%d LOK Fail!!!\n", path);
	}

	iqk_info->lok_fail[path] = LOK_notready;

	/*fill IDAC LUT table*/
#if 0
	/*
	for (i = 0; i < 8; i++) {
		odm_set_rf_reg(dm, path, RF_0x33, BIT(2)|BIT(1)|BIT(0), i);
		odm_set_rf_reg(dm, path, RF_0x8, RFREGOFFSETMASK, LOK_temp2);
	}
	*/
#endif
	return LOK_notready;
}

boolean
_iqk_one_shot_8821c(void *dm_void, u8 path, u8 idx)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	u8 delay_count = 0;
	boolean fail = true;
	u32 IQK_CMD = 0x0;
	u16 iqk_apply[2] = {0xc94, 0xe94};

	if (idx == TX_IQK)
		RF_DBG(dm, DBG_RF_IQK,
		       "[IQK]============ S%d WBTXIQK ============\n",
		       iqk_info->is_btg);
	else if (idx == RXIQK1)
		RF_DBG(dm, DBG_RF_IQK,
		       "[IQK]============ S%d WBRXIQK STEP1============\n",
		       iqk_info->is_btg);
	else
		RF_DBG(dm, DBG_RF_IQK,
		       "[IQK]============ S%d WBRXIQK STEP2============\n",
		       iqk_info->is_btg);

	if (idx == TXIQK) {
		IQK_CMD = 0xf8000008 | ((*dm->band_width + 4) << 8) | (1 << (path + 4));
		RF_DBG(dm, DBG_RF_IQK, "[IQK]TXK_Trigger = 0x%x\n", IQK_CMD);
#if 0
		/*{0xf8000418, 0xf800042a} ==> 20 WBTXK (CMD = 4)*/
		/*{0xf8000518, 0xf800052a} ==> 40 WBTXK (CMD = 5)*/
		/*{0xf8000618, 0xf800062a} ==> 80 WBTXK (CMD = 6)*/
#endif
	} else if (idx == RXIQK1) {
		if (*dm->band_width == 2)
			IQK_CMD = 0xf8000808 | (1 << (path + 4));
		else
			IQK_CMD = 0xf8000708 | (1 << (path + 4));
		RF_DBG(dm, DBG_RF_IQK, "[IQK]RXK1_Trigger = 0x%x\n", IQK_CMD);
#if 0
		/*{0xf8000718, 0xf800072a} ==> 20 WBTXK (CMD = 7)*/
		/*{0xf8000718, 0xf800072a} ==> 40 WBTXK (CMD = 7)*/
		/*{0xf8000818, 0xf800082a} ==> 80 WBTXK (CMD = 8)*/
#endif
	} else if (idx == RXIQK2) {
		IQK_CMD = 0xf8000008 | ((*dm->band_width + 9) << 8) | (1 << (path + 4));
		RF_DBG(dm, DBG_RF_IQK, "[IQK]RXK2_Trigger = 0x%x\n", IQK_CMD);
#if 0
		/*{0xf8000918, 0xf800092a} ==> 20 WBRXK (CMD = 9)*/
		/*{0xf8000a18, 0xf8000a2a} ==> 40 WBRXK (CMD = 10)*/
		/*{0xf8000b18, 0xf8000b2a} ==> 80 WBRXK (CMD = 11)*/
#endif
	}

	_iqk_set_gnt_wl_gnt_bt(dm, true);

	odm_write_4byte(dm, 0x1bc8, 0x80000000);
	odm_write_4byte(dm, 0x8f8, 0x41400080);

	if (odm_get_rf_reg(dm, path, RF_0x08, RFREGOFFSETMASK) != 0x0)
		odm_set_rf_reg(dm, path, RF_0x8, RFREGOFFSETMASK, 0x0);

	odm_write_4byte(dm, 0x1b00, IQK_CMD);
	odm_write_4byte(dm, 0x1b00, IQK_CMD + 0x1);

#if 0
	/*ODM_sleep_ms(WBIQK_delay_8821C);*/
#endif

	fail = _iqk_check_nctl_done_8821c(dm, path, IQK_CMD);

	RF_DBG(dm, DBG_RF_IQK, "[IQK]check 0x49c = %x\n",
	       odm_read_1byte(dm, 0x49c));

	_iqk_set_gnt_wl_gnt_bt(dm, false);

	if (dm->debug_components & DBG_RF_IQK) {
		odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);
		RF_DBG(dm, DBG_RF_IQK,
		       "[IQK]S%d ==> 0x1b00 = 0x%x, 0x1b08 = 0x%x\n", path,
		       odm_read_4byte(dm, 0x1b00), odm_read_4byte(dm, 0x1b08));
		RF_DBG(dm, DBG_RF_IQK, "[IQK]S%d ==> delay_count = 0x%x\n",
		       path, delay_count);
		if (idx != TXIQK)
			RF_DBG(dm, DBG_RF_IQK,
			       "[IQK]S%d ==> RF0x0 = 0x%x, RF0x%x = 0x%x\n",
			       path,
			       odm_get_rf_reg(dm, path, RF_0x0,
					      RFREGOFFSETMASK),
			       (iqk_info->is_btg) ? 0x78 : 0x56,
			       (iqk_info->is_btg) ?
			       odm_get_rf_reg(dm, path, RF_0x78,
					      RFREGOFFSETMASK) :
			       odm_get_rf_reg(dm, path, RF_0x56,
					      RFREGOFFSETMASK));
	}

	odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);
	if (idx == TXIQK) {
		if (fail)
			odm_set_bb_reg(dm, iqk_apply[path], BIT(0), 0x0);
		else
			_iqk_backup_iqk_8821c(dm, 0x2, path);
	}
	if (idx == RXIQK2) {
		iqk_info->rxiqk_agc[0][path] =
			(u16)(((odm_get_rf_reg(dm, (enum rf_path)path, RF_0x0, RFREGOFFSETMASK) >> 5) & 0xff) |
			      (iqk_info->tmp1bcc << 8));

		odm_write_4byte(dm, 0x1b38, 0x20000000);

		if (fail)
			odm_set_bb_reg(dm, iqk_apply[path], (BIT(11) | BIT(10)), 0x0);
		else
			_iqk_backup_iqk_8821c(dm, 0x3, path);
	}

	if (idx == TXIQK)
		iqk_info->iqk_fail_report[0][path][TXIQK] = fail;
	else
		iqk_info->iqk_fail_report[0][path][RXIQK] = fail;

	return fail;
}

boolean
_iqk_rxiqkbystep_8821c(void *dm_void, u8 path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	boolean KFAIL = true, gonext;

#if 1
	switch (iqk_info->rxiqk_step) {
	case 1: /*gain search_RXK1*/
		_iqk_rxk1setting_8821c(dm, path);
		gonext = false;
		while (1) {
			KFAIL = _iqk_rx_iqk_gain_search_fail_8821c(dm, path, RXIQK1);
			if (KFAIL && iqk_info->gs_retry_count[0][path][0] < 2) {
				iqk_info->gs_retry_count[0][path][0]++;
			} else if (KFAIL) {
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
		halrf_iqk_xym_read(dm, 0x0, 0x2);
		break;
	case 2: /*gain search_RXK2*/
		_iqk_rxk2setting_8821c(dm, path, true);
		iqk_info->isbnd = false;
		while (1) {
			KFAIL = _iqk_rx_iqk_gain_search_fail_8821c(dm, path, RXIQK2);
			if (KFAIL && iqk_info->gs_retry_count[0][path][1] < rxiqk_gs_limit) {
				iqk_info->gs_retry_count[0][path][1]++;
			} else {
				iqk_info->rxiqk_step++;
				break;
			}
		}
		halrf_iqk_xym_read(dm, 0x0, 0x3);
		break;
	case 3: /*RXK1*/
		_iqk_rxk1setting_8821c(dm, path);
		gonext = false;
		while (1) {
			KFAIL = _iqk_one_shot_8821c(dm, path, RXIQK1);
			if (KFAIL && iqk_info->retry_count[0][path][RXIQK1] < 2) {
				iqk_info->retry_count[0][path][RXIQK1]++;
			} else if (KFAIL) {
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
		halrf_iqk_xym_read(dm, 0x0, 0x4);
		break;
	case 4: /*RXK2*/
		_iqk_rxk2setting_8821c(dm, path, false);
		gonext = false;
		while (1) {
			KFAIL = _iqk_one_shot_8821c(dm, path, RXIQK2);
			if (KFAIL && iqk_info->retry_count[0][path][RXIQK2] < 2) {
				iqk_info->retry_count[0][path][RXIQK2]++;
			} else if (KFAIL) {
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
		halrf_iqk_xym_read(dm, 0x0, 0x0);
		break;
	}
	return KFAIL;
#endif
}
void _iqk_iqk_by_path_8821c(void *dm_void, boolean segment_iqk)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	boolean KFAIL = true;
	u8 i, kcount_limit;

#if 0
	/*	RF_DBG(dm, DBG_RF_IQK, "[IQK]iqk_step = 0x%x\n", dm->rf_calibrate_info.iqk_step); */
#endif
	if (*dm->band_width == 2)
		kcount_limit = kcount_limit_80m;
	else
		kcount_limit = kcount_limit_others;

	while (1) {
		switch (dm->rf_calibrate_info.iqk_step) {
		case 1: /*S0 LOK*/
			for (i = 0; i < 8; i++) { /* the LOK Cal in the each PAD stage*/
				_iqk_lok_setting_8821c(dm, RF_PATH_A, i);
				_lok_one_shot_8821c(dm, RF_PATH_A, i);
			}
			dm->rf_calibrate_info.iqk_step++;
			break;
		case 2: /*S0 TXIQK*/
			_iqk_txk_setting_8821c(dm, RF_PATH_A);
			KFAIL = _iqk_one_shot_8821c(dm, RF_PATH_A, TXIQK);
			iqk_info->kcount++;
			RF_DBG(dm, DBG_RF_IQK, "[IQK]KFail = 0x%x\n", KFAIL);
			if (KFAIL && iqk_info->retry_count[0][RF_PATH_A][TXIQK] < 3)
				iqk_info->retry_count[0][RF_PATH_A][TXIQK]++;
			else
				dm->rf_calibrate_info.iqk_step++;
			halrf_iqk_xym_read(dm, 0x0, 0x1);
			break;
		case 3: /*S0 RXIQK*/
			while (1) {
				KFAIL = _iqk_rxiqkbystep_8821c(dm, RF_PATH_A);
				RF_DBG(dm, DBG_RF_IQK,
				       "[IQK]S0RXK KFail = 0x%x\n", KFAIL);
				if (iqk_info->rxiqk_step == 5) {
					dm->rf_calibrate_info.iqk_step++;
					iqk_info->rxiqk_step = 1;
					if (KFAIL)
						RF_DBG(dm, DBG_RF_IQK, "[IQK]S0RXK fail code: %d!!!\n", iqk_info->rxiqk_fail_code[0][RF_PATH_A]);
					break;
				}
			}
			iqk_info->kcount++;
			break;
		}

		if (dm->rf_calibrate_info.iqk_step == 4) {
			RF_DBG(dm, DBG_RF_IQK,
			       "[IQK]==========LOK summary ==========\n");
			RF_DBG(dm, DBG_RF_IQK, "[IQK]PathA_LOK_notready = %d\n",
			       iqk_info->lok_fail[RF_PATH_A]);
			RF_DBG(dm, DBG_RF_IQK,
			       "[IQK]==========IQK summary ==========\n");
			RF_DBG(dm, DBG_RF_IQK, "[IQK]PathA_TXIQK_fail = %d\n",
			       iqk_info->iqk_fail_report[0][RF_PATH_A][TXIQK]);
			RF_DBG(dm, DBG_RF_IQK, "[IQK]PathA_RXIQK_fail = %d\n",
			       iqk_info->iqk_fail_report[0][RF_PATH_A][RXIQK]);
			RF_DBG(dm, DBG_RF_IQK, "[IQK]PathA_TXIQK_retry = %d\n",
			       iqk_info->retry_count[0][RF_PATH_A][TXIQK]);
			RF_DBG(dm, DBG_RF_IQK,
			       "[IQK]PathA_RXK1_retry = %d, PathA_RXK2_retry = %d\n",
			       iqk_info->retry_count[0][RF_PATH_A][RXIQK1],
			       iqk_info->retry_count[0][RF_PATH_A][RXIQK2]);
			RF_DBG(dm, DBG_RF_IQK,
			       "[IQK]PathA_GS1_retry = %d, PathA_GS2_retry = %d\n",
			       iqk_info->gs_retry_count[0][RF_PATH_A][0],
			       iqk_info->gs_retry_count[0][RF_PATH_A][1]);

			for (i = 0; i < SS_8821C; i++) {
				odm_write_4byte(dm, 0x1b00, 0xf8000008 | i << 1);
				odm_write_4byte(dm, 0x1b2c, 0x7);
				odm_write_4byte(dm, 0x1bcc, 0x0);
				odm_write_4byte(dm, 0x1b38, 0x20000000);
			}
			break;
		}
		RF_DBG(dm, DBG_RF_IQK, "[IQK]segmentIQK = %d, Kcount = %d\n",
		       segment_iqk, iqk_info->kcount);
		if (segment_iqk && iqk_info->kcount == kcount_limit)
			break;
	}
}

void _iqk_start_iqk_8821c(struct dm_struct *dm, boolean segment_iqk)
{
	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	u32 tmp;

	odm_write_4byte(dm, 0x1b00, 0xf8000008);
	odm_write_4byte(dm, 0x1bb8, 0x00000000);
	/*GNT_WL = 1*/
	if (iqk_info->is_btg) {
		tmp = odm_get_rf_reg(dm, RF_PATH_A, RF_0x1, RFREGOFFSETMASK);
		tmp = (tmp & (~BIT(3))) | BIT(0) | BIT(2) | BIT(5);
		odm_set_rf_reg(dm, RF_PATH_A, RF_0x1, RFREGOFFSETMASK, tmp);
	} else {
		tmp = odm_get_rf_reg(dm, RF_PATH_A, RF_0x1, RFREGOFFSETMASK);
		tmp = ((tmp & (~BIT(3))) & (~BIT(5))) | BIT(0) | BIT(2);
		odm_set_rf_reg(dm, RF_PATH_A, RF_0x1, RFREGOFFSETMASK, tmp);
		RF_DBG(dm, DBG_RF_IQK, "[IQK]==> RF0x1 = 0x%x\n",
		       odm_get_rf_reg(dm, RF_PATH_A, RF_0x1, RFREGOFFSETMASK));
	}
	_iqk_iqk_by_path_8821c(dm, segment_iqk);
}

void _iq_calibrate_8821c_init(struct dm_struct *dm)
{
	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	u8 i, j, k, m;
	static boolean firstrun = true;

	if (firstrun) {
		firstrun = false;
		RF_DBG(dm, DBG_RF_IQK,
		       "[IQK]=====>PHY_IQCalibrate_8821C_Init\n");

		for (i = 0; i < SS_8821C; i++) {
			for (j = 0; j < 2; j++) {
				iqk_info->lok_fail[i] = true;
				iqk_info->iqk_fail[j][i] = true;
				iqk_info->iqc_matrix[j][i] = 0x20000000;
			}
		}

		for (i = 0; i < 2; i++) {
			iqk_info->iqk_channel[i] = 0x0;

			for (j = 0; j < SS_8821C; j++) {
				iqk_info->lok_idac[i][j] = 0x0;
				iqk_info->rxiqk_agc[i][j] = 0x0;
				iqk_info->bypass_iqk[i][j] = 0x0;

				for (k = 0; k < 2; k++) {
					iqk_info->iqk_fail_report[i][j][k] = true;
					for (m = 0; m < 8; m++) {
						iqk_info->iqk_cfir_real[i][j][k][m] = 0x0;
						iqk_info->iqk_cfir_imag[i][j][k][m] = 0x0;
					}
				}

				for (k = 0; k < 3; k++)
					iqk_info->retry_count[i][j][k] = 0x0;
			}
		}
	}
}

u8 _txgapk_txpower_compare_8821c(struct dm_struct *dm, u8 path, u32 pw1,
				 u32 pw2, u32 *pwr_table)
{
	u8 pwr_delta;
	u32 temp = 0x0;
	temp = (u32)(pw1 / (pw2 / 1000));

	if (temp < pwr_table[0]) /*<-3.5 dB*/
		pwr_delta = 0x0;
	else if (temp < pwr_table[1])
		pwr_delta = 0x1;
	else if (temp < pwr_table[2])
		pwr_delta = 0x2;
	else if (temp < pwr_table[3])
		pwr_delta = 0x3;
	else if (temp < pwr_table[4])
		pwr_delta = 0x4;
	else if (temp < pwr_table[5])
		pwr_delta = 0x5;
	else if (temp < pwr_table[6])
		pwr_delta = 0x6;
	else if (temp < pwr_table[7])
		pwr_delta = 0x7;
	else if (temp < pwr_table[8])
		pwr_delta = 0x8;
	else if (temp < pwr_table[9])
		pwr_delta = 0x9;
	else if (temp < pwr_table[0xa])
		pwr_delta = 0xa;
	else if (temp < pwr_table[0xb])
		pwr_delta = 0xb;
	else if (temp < pwr_table[0xc])
		pwr_delta = 0xc;
	else if (temp < pwr_table[0xd])
		pwr_delta = 0xd;
	else if (temp < pwr_table[0xe])
		pwr_delta = 0xe;
	else if (temp < pwr_table[0xf])
		pwr_delta = 0xf;
	else if (temp < pwr_table[0x10])
		pwr_delta = 0x10;
	else if (temp < pwr_table[0x11])
		pwr_delta = 0x11;
	else if (temp < pwr_table[0x12])
		pwr_delta = 0x12;
	else if (temp < pwr_table[0x13])
		pwr_delta = 0x13;
	else if (temp < pwr_table[0x14])
		pwr_delta = 0x14;
	else if (temp < pwr_table[0x15])
		pwr_delta = 0x15;
	else if (temp < pwr_table[0x16])
		pwr_delta = 0x16;
	else if (temp < pwr_table[0x17])
		pwr_delta = 0x17;
	else if (temp < pwr_table[0x18])
		pwr_delta = 0x18;
	else if (temp < pwr_table[0x19])
		pwr_delta = 0x19;
	else
		pwr_delta = 0x1a;

	RF_DBG(dm, DBG_RF_IQK, "[TXGAPK] temp =%d, pwr_delta =%x\n", temp,
	       pwr_delta);

	return pwr_delta;
}

u32 _txgapk_txgap_compenstion_8821c(struct dm_struct *dm, u8 path,
				    u32 txgain_0x56, u8 pwr_delta)
{
	u32 new_txgain_0x56;

	/*	0	1	2	3	4	5	6	7	8	9	10	11	12	13	14 */
	/*	-3.5	-3 -2.5  -2  -1.5  -1  -0.5    0   0.5	  1	1.5   2   2.5	  3    3.5 (dB)*/
#if 0
	/* 	pwr_table[0xf]={89,100,112,125,141,158,177,199,223,251,281,316,354,398,446}*/
#endif

	switch (pwr_delta) {
	case 0x1a:
	case 0x19:
	case 0x18:
	case 0x17:
	case 0x16:
	case 0x15:
	case 0x14:
#if 0
	/*
						if ((txgain_0x56 & 0x1f)<=0x1d)
							new_txgain_0x56 = txgain_0x56 + 0x2; //< -1.5 ~ -2.5dB
						else if ((txgain_0x56 & 0x1f)<=  0x1e)
							new_txgain_0x56 = txgain_0x56 + 0x1;
						else
							new_txgain_0x56 = txgain_0x56;
						break;
*/
#endif
	case 0x13:
	case 0x12:
		if ((txgain_0x56 & 0x1f) <= 0x1e)
			new_txgain_0x56 = txgain_0x56 + 0x1;/*< -0.5 ~ -1.5dB*/
		else
			new_txgain_0x56 = txgain_0x56;
		break;
	case 0x11:
	case 0x10:
	case 0xf:
	case 0xe:
	case 0xd:
	case 0xc:
		new_txgain_0x56 = txgain_0x56; /*<  -0.5~0.5dB*/
		break;
	case 0xb:
	case 0xa:
	case 0x9:
	case 0x8:
	case 0x7:
		if ((txgain_0x56 & 0x1f) >= 0x01)
			new_txgain_0x56 = txgain_0x56 - 0x1; /* >0.5~1.5dB */
		else
			new_txgain_0x56 = txgain_0x56;
		break;
	case 0x6:
	case 0x5:
	case 0x4:
	case 0x3:
#if 0
	/*
						if ((txgain_0x56 & 0x1f)>= 0x02)
							new_txgain_0x56 = txgain_0x56 - 0x2; //>1.5~2.5dB
						else if ((txgain_0x56 & 0x1f)>= 0x01)
							new_txgain_0x56 = txgain_0x56 - 0x1;
						else
							new_txgain_0x56 = txgain_0x56;
						break;
*/
#endif
	case 0x2:
	case 0x1:
	case 0x0:
#if 0
	/*
						if ((txgain_0x56 & 0x1f)>= 0x03)
							new_txgain_0x56 = txgain_0x56 - 0x3; //>2.5~3.5dB
						else if ((txgain_0x56 & 0x1f)>= 0x02)
							new_txgain_0x56 = txgain_0x56 - 0x2;
						else if ((txgain_0x56 & 0x1f)>= 0x01)
							new_txgain_0x56 = txgain_0x56 - 0x1;
						else
							new_txgain_0x56 = txgain_0x56;
						break;
*/
#endif
	default:
		new_txgain_0x56 = txgain_0x56;
		break;
	}
	if ((new_txgain_0x56 & 0x1f) < 0x02)
		new_txgain_0x56 = (new_txgain_0x56 & 0xffffc) + 0x2;
	if ((new_txgain_0x56 & 0x1f) > 0x1d)
		new_txgain_0x56 = (new_txgain_0x56 | 0x3) - 0x2;

	return new_txgain_0x56;
}

void _txgapk_backup_8821c(struct dm_struct *dm, u32 *backup_txgap,
			  u32 *backup_txgap_reg, u8 txgapk_reg_num)
{
	u32 i;

	for (i = 0; i < txgapk_reg_num; i++)
		backup_txgap[i] = odm_read_4byte(dm, backup_txgap_reg[i]);
}
u32 _txgapk_get_rf_tx_index_8821c(struct dm_struct *dm, u8 path,
				  u32 txgain_index)
{
	u32 rf_backup_reg00, rf_backup_regdf, rf_reg56;

	rf_backup_reg00 = odm_get_rf_reg(dm, RF_PATH_A, RF_0x00, RFREGOFFSETMASK);
	rf_backup_regdf = odm_get_rf_reg(dm, RF_PATH_A, RF_0xdf, RFREGOFFSETMASK);

	odm_set_rf_reg(dm, RF_PATH_A, RF_0xdf, RFREGOFFSETMASK, 0x08009);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x00, RFREGOFFSETMASK, 0x20000 + txgain_index);
	/*ODM_sleep_us(10);*/

	ODM_delay_us(10);
	rf_reg56 = odm_get_rf_reg(dm, RF_PATH_A, RF_0x56, RFREGOFFSETMASK);

	RF_DBG(dm, DBG_RF_IQK,
	       "[TXGAPK](2) txgain_index =0x%x, rf_reg56=0x%x\n", txgain_index,
	       rf_reg56);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x00, RFREGOFFSETMASK, rf_backup_reg00);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xdf, RFREGOFFSETMASK, rf_backup_regdf);
	return rf_reg56;
}

void _txgapk_restore_8821c(struct dm_struct *dm, u32 *backup_txgap,
			   u32 *backup_txgap_reg, u8 txgapk_reg_num)
{
	u32 i;

	for (i = 0; i < txgapk_reg_num; i++)
		odm_write_4byte(dm, backup_txgap_reg[i], backup_txgap[i]);
}

void _txgapk_setting_8821c(struct dm_struct *dm, u8 path)
{
	struct dm_iqk_info *iqk_info = &dm->IQK_info;

	/*RF*/
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xef, RFREGOFFSETMASK, 0x80000);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0x00024);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x3e, RFREGOFFSETMASK, 0x0003F);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, RFREGOFFSETMASK, 0xCBFCE);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xef, RFREGOFFSETMASK, 0x00000);
	if (iqk_info->is_btg) {
	} else {
		switch (*dm->band_type) {
		case ODM_BAND_2_4G:
			break;
		case ODM_BAND_5G:
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x8f, RFREGOFFSETMASK, 0xA9C00);
			odm_set_rf_reg(dm, RF_PATH_A, RF_0xdf, RFREGOFFSETMASK, 0x00809);
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x00, RFREGOFFSETMASK, 0x4001c);

			odm_write_4byte(dm, 0x1bcc, 0x00000009); /*try the iqk swing*/
			odm_write_4byte(dm, 0x1b20, 0x01040008);
			odm_write_4byte(dm, 0x1b24, 0x01040848);

			odm_write_4byte(dm, 0x1b14, 0x00001000);
			odm_write_4byte(dm, 0x1b1c, 0x82193d31);

			odm_write_1byte(dm, 0x1b22, 0x04); /*sync with RF0x33[3:0]*/
			odm_write_1byte(dm, 0x1b26, 0x04); /*sync with RF0x33[3:0]*/
			odm_write_1byte(dm, 0x1b2c, 0x03);
			odm_write_4byte(dm, 0x1b38, 0x20000000);
			odm_write_4byte(dm, 0x1b3c, 0x20000000);
			odm_write_4byte(dm, 0xc00, 0x4);

			RF_DBG(dm, DBG_RF_IQK,
			       "[IQK](1) txgap calibration setting!!!\n");

			break;
		}
	}
}

u32 _txgapk_one_shot_8821c(struct dm_struct *dm_void, u8 path, u32 reg0x56)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	boolean txgap_k_notready = true;
	u8 delay_count = 0x0;
	u32 txgap_k_tmp1 = 0x1, txgap_k_tmp2 = 0x2;
	u8 offset;
	u32 reg_1bb8;
	u32 rx_dsp_power;

	reg_1bb8 = odm_read_4byte(dm, 0x1bb8);
	/*clear the flag*/
	odm_write_1byte(dm, 0x1bd6, 0x0b);
	odm_set_bb_reg(dm, R_0x1bfc, BIT(1), 0x0);
	txgap_k_notready = true;
	delay_count = 0x0;
	/* get tx gain*/
	odm_write_1byte(dm, 0x1b2b, 0x00);
	odm_write_1byte(dm, 0x1bb8, 0x00);
	odm_set_rf_reg(dm, path, RF_0xdf, RFREGOFFSETMASK, 0x00802);
	odm_set_rf_reg(dm, path, RF_0x8f, RFREGOFFSETMASK, 0xa9c00);
	odm_set_rf_reg(dm, path, RF_0x56, RFREGOFFSETMASK, reg0x56);
	odm_write_4byte(dm, 0x1bb8, 0x00100000);
#if 0
	/*ODM_sleep_us(10);*/
#endif

	ODM_delay_us(10);
	/* one-shot-1*/
	odm_write_4byte(dm, 0x1b34, 0x1);
	odm_write_4byte(dm, 0x1b34, 0x0);

#if 1
	while (txgap_k_notready) {
		odm_write_1byte(dm, 0x1bd6, 0x0b);
		if ((boolean)odm_get_bb_reg(dm, R_0x1bfc, BIT(1)))
			txgap_k_notready = false;
		else
			txgap_k_notready = true;

		if (txgap_k_notready) {
#if 0
			/*ODM_sleep_us(100);*/
#endif
			ODM_delay_us(100);
			delay_count++;
		}

		if (delay_count >= 20) {
			RF_DBG(dm, DBG_RF_IQK,
			       "[TXGAPK] (3)txgapktimeout,delay_count=0x%x !!!\n",
			       delay_count);
			txgap_k_notready = false;
			break;
		}
	}
#else
	ODM_sleep_ms(1);
	if ((boolean)odm_get_bb_reg(dm, R_0x1bfc, BIT(1)))
		txgap_k_notready = false;
	else
		txgap_k_notready = true;

#endif

	if (!txgap_k_notready) {
		odm_write_1byte(dm, 0x1bd6, 0x5);
		txgap_k_tmp1 = odm_read_4byte(dm, 0x1bfc) >> 27;
		odm_write_1byte(dm, 0x1bd6, 0xe);
		txgap_k_tmp2 = odm_read_4byte(dm, 0x1bfc);

		RF_DBG(dm, DBG_RF_IQK,
		       "[TXGAPK] reg0x56 =0x%x, txgapK_tmp1 =0x%x, txgapK_tmp2 =0x%x!!!\n",
		       reg0x56, txgap_k_tmp1, txgap_k_tmp2);

		if (txgap_k_tmp1 == 0)
			offset = 0x0;
		else if (txgap_k_tmp1 < 2)
			offset = 0x1;
		else if (txgap_k_tmp1 < 4)
			offset = 0x2;
		else
			offset = 0x3;

		if (txgap_k_tmp1 == 0x0) {
			rx_dsp_power = txgap_k_tmp2;
		} else {
			txgap_k_tmp1 = txgap_k_tmp1 << (32 - offset);
			txgap_k_tmp2 = txgap_k_tmp2 >> offset;
			rx_dsp_power = txgap_k_tmp1 + txgap_k_tmp2;
			overflowflag = true;
			RF_DBG(dm, DBG_RF_IQK,
			       "[TXGAPK](3) (1)overflowflag = true, txgapK_tmp1 =0x%x, txgapK_tmp2 =0x%x!!!\n",
			       txgap_k_tmp1, txgap_k_tmp2);
		}
	} else {
		RF_DBG(dm, DBG_RF_IQK, "[TXGAPK](3) txgapK Fail!!!\n");
	}

	odm_write_4byte(dm, 0x1bb8, reg_1bb8);

	return rx_dsp_power;
}

void _phy_txgapk_calibrate_8821c(void *dm_void, u8 path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 txgain[txgap_k_number] = {0, 0, 0, 0, 0, 0, 0};
	u32 txgain_rf56[txgap_k_number] = {0, 0, 0, 0, 0, 0, 0};
	u8 add_base, txgapk_num = 0x0, psd_delta = 0x0;
	u8 i, bandselect = 0x0;
	u32 backup_txgap_reg[11] = {0x1b14, 0x1b1c, 0x1b20, 0x1b24, 0x1b28, 0x1b2c, 0x1b38, 0x1b3c, 0x1bd6, 0x1bb8, 0x1bcc};
	u32 backup_txgap[11];
	u8 gain_gap_index[txgap_k_number] = {0x13, 0x10, 0xd, 0xa, 0x7, 0x4, 0x1};
	u32 txgainindex[txgap_k_number] = {0x26, 0x25, 0x24, 0x23, 0x22, 0x21, 0x20};
	u32 tmp1, tmp2, tmp3, tmp4, tmp5;
	u8 skip_low_power_index = 0x3;

	s8 psd_single_tone_offset_1db[3][3] = {{-1, 0, 0}, {-2, -1, -1}, {-1, -1, -1} }; /*5G, L,M,H, index 32,26,20*/
	static u32 pwr_table_1db[27] = {590, 630, 668, 707, 749, 794, 841, 891, 944, 1000, 1040, 1096, 1148, 1202, 1258, 1318,
					1380, 1412, 1513, 1584, 1678, 1778, 1888, 1995, 2113, 2387, 2391};
	static u32 pwr_table_3db[27] = {944, 1000, 1059, 1122, 1185, 1258, 1333, 1412, 1496, 1584, 1659, 1737, 1819, 1905, 1995, 2089,
					2187, 2290, 2398, 2511, 2660, 2818, 2985, 3162, 3349, 3548, 3758};
	boolean txgapkdone = false;
	u8 txgap_changed = 0x0;
	u8 ref_val = 0x0;

	overflowflag = 0;
	RF_DBG(dm, DBG_RF_IQK,
	       "[TXGAPK] (1) *dm->band_width = %x, *dm->channel =%d, *dm->band_type=%x\n",
	       *dm->band_width, *dm->channel, *dm->band_type);

	if (!(*dm->mp_mode))
		return;

	if (!(*dm->band_type == ODM_BAND_5G))
		return;

	if (*dm->band_width == 0) {
#if 0
		/*		return;*/
#endif
		if (*dm->channel < 64) {
			bandselect = 0x0;
			add_base = 0x0;
		} else if (*dm->channel < 153) {
			bandselect = 0x1;
			add_base = 0x40;
#if 0
		/*else if (*dm->channel ==153){*/
#endif
		} else {
			bandselect = 0x2;
			add_base = 0x80;
		}
	} else if (*dm->band_width == 1) {
		if (*dm->channel < 102) {
			bandselect = 0x0;
			add_base = 0x0;
		} else if (*dm->channel < 151) {
			bandselect = 0x1;
			add_base = 0x40;
#if 0
		/*else if (*dm->channel ==151){*/
#endif
		} else {
			bandselect = 0x2;
			add_base = 0x80;
		}
	} else if (*dm->band_width == 2) {
		/*		return;*/
		if (*dm->channel < 106) {
			bandselect = 0x0;
			add_base = 0x0;
		} else if (*dm->channel < 155) {
			bandselect = 0x1;
			add_base = 0x40;
#if 0
		/*else if (*dm->channel ==155){*/
#endif
		} else {
			bandselect = 0x2;
			add_base = 0x80;
		}
	} else {
		return;
	}

	if (txgap_done[bandselect])
		return;

	/*Step 1*/
	_txgapk_backup_8821c(dm, backup_txgap, backup_txgap_reg, 0xb);
	/*step 2*/
	phydm_clear_kfree_to_rf(dm, RF_PATH_A, 1);

	for (i = 0; i < txgap_k_number; i++) {
		txgain_rf56[i] = _txgapk_get_rf_tx_index_8821c(dm, RF_PATH_A, gain_gap_index[i]);
		txgain[i] = txgain_rf56[i];
	}

	for (i = 0; i < txgap_k_number; i++) {
		RF_DBG(dm, DBG_RF_IQK, "[TXGAPK] start txgain1[%x]=0x%x\n", i,
		       txgain_rf56[i]);
	}

	_txgapk_setting_8821c(dm, RF_PATH_A);

	/*step 3*/

	/*1st*/
	while (!txgapkdone) {
		txgapk_num++;
		if (txgapk_num > 2)
			break;

		for (i = 0; i < txgap_k_number - 1 - skip_low_power_index; i++) {
			tmp1 = (txgain[i] & 0x000000e0) >> 5;
			tmp2 = (txgain[i + 1] & 0x000000e0) >> 5;
			tmp5 = txgain[i + 1];
#if 0
			//if (tmp1 != tmp2){
#endif
			if (true) {
				tmp3 = _txgapk_one_shot_8821c(dm, RF_PATH_A, txgain[i] - 2);
				tmp4 = _txgapk_one_shot_8821c(dm, RF_PATH_A, txgain[i + 1]);
				if (overflowflag)
					overflowflag = false;
#if 0
					//tmp4 = tmp4>>1;
#endif

				psd_delta =
					_txgapk_txpower_compare_8821c(dm, RF_PATH_A, tmp3, tmp4, pwr_table_1db);

				psd_delta = psd_delta + psd_single_tone_offset_1db[bandselect][i];
				RF_DBG(dm, DBG_RF_IQK,
				       "[TXGAPK] new psd_detla = %x\n",
				       psd_delta);

				if (psd_delta <= 0xb)
					txgap_changed++;
				else if (psd_delta <= 0x11)
					;
				else
					txgap_changed++;

				txgain[i + 1] =
					_txgapk_txgap_compenstion_8821c(dm, RF_PATH_A, txgain[i + 1], psd_delta);
			} else {
				RF_DBG(dm, DBG_RF_IQK,
				       "[TXGAPK]skip i=%d, txgain[%x]=0x%x\n",
				       i, i + 1, txgain[i + 1]);
			}
		}

		/*2nd*/

		if (txgap_changed > 0x0) {
			RF_DBG(dm, DBG_RF_IQK, "[TXGAPK] do 3dB check\n");

			for (i = 0; i < txgap_k_number - 1 - skip_low_power_index; i++) {
				tmp1 = (txgain[i] & 0x000000e0) >> 5;
				tmp2 = (txgain[i + 1] & 0x000000e0) >> 5;

				if (tmp1 != tmp2) {
					if (i == 0)
						tmp3 = _txgapk_one_shot_8821c(dm, RF_PATH_A, txgain[i] - 2);
					else
						tmp3 = _txgapk_one_shot_8821c(dm, RF_PATH_A, txgain[i]);

					tmp4 = _txgapk_one_shot_8821c(dm, RF_PATH_A, txgain[i + 1]);

					if (overflowflag) {
						overflowflag = false;
						RF_DBG(dm, DBG_RF_IQK, "[IQK] tmp3= %x, tmp4= %x\n", tmp3, tmp4);
					}
					if (i == 0)
						psd_delta =
							_txgapk_txpower_compare_8821c(dm, RF_PATH_A, tmp3, tmp4, pwr_table_1db);

					else
						psd_delta =
							_txgapk_txpower_compare_8821c(dm, RF_PATH_A, tmp3, tmp4, pwr_table_3db);

					if (psd_delta <= 0xa)
						ref_val++;
					else if (psd_delta <= 0x12)
						;
					else
						ref_val++;
				} else {
					RF_DBG(dm, DBG_RF_IQK, "[TXGAPK]skip i=%d, txgain[%x]=0x%x\n", i, i + 1, txgain[i + 1]);
				}
			}

			if (ref_val == 0x0) {
				txgapkdone = true;
				txgap_changed = 0x0;
			} else {
				/* restore default rf 0x56 */
				for (i = 0; i < txgap_k_number; i++)
					txgain[i] = txgain_rf56[i];
			}

		} else {
			txgapkdone = true;
		}
	}

	/*step 7*/
	_txgapk_restore_8821c(dm, backup_txgap, backup_txgap_reg, 0xb);

	/*step 8*/
	RF_DBG(dm, DBG_RF_IQK,
	       "[TXGAPK]txgapkdone =%x, txgap_changed=0x%x, ref_val =0x%x\n",
	       txgapkdone, txgap_changed, ref_val);

	if (txgapkdone) {
		odm_set_rf_reg(dm, RF_PATH_A, RF_0xef, RFREGOFFSETMASK, 0x00800);

		for (i = 0; i < txgap_k_number; i++) {
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, txgainindex[i] + add_base);
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, RFREGOFFSETMASK, txgain[i]);
		}
		odm_set_rf_reg(dm, RF_PATH_A, RF_0xef, RFREGOFFSETMASK, 0x00000);
		txgap_done[bandselect] = true;
		txgapkdone = false;
	}
}

#if 0
/*
void
_DPK_BackupReg_8821C(
	struct dm_struct*	dm,
	static u32*	DPK_backup,
	u32*		backup_dpk_reg
	)
{

	u32 i;

	for (i = 0; i < DPK_BACKUP_REG_NUM_8821C; i++)
		DPK_backup[i] = odm_read_4byte(dm, backup_dpk_reg[i]);

}
void
_DPK_Restore_8821C(
	struct dm_struct*		dm,
	static  u32*		DPK_backup,
	u32*		backup_dpk_reg
	)
{
	u32 i;
	for (i = 0; i < DPK_BACKUP_REG_NUM_8821C; i++)
		odm_write_4byte(dm, backup_dpk_reg[i], DPK_backup[i]);
}

*/
#endif
void _dpk_toggle_rxagc(struct dm_struct *dm, boolean reset)
{
	/*toggle RXAGC  workaround method*/
	u32 tmp1;
	tmp1 = odm_read_4byte(dm, 0xc50);
	odm_set_bb_reg(dm, R_0xc50, BIT(3) | BIT(2) | BIT(1) | BIT(0), 0x0);
#if 0
	/* 	 ODM_sleep_ms(2);*/
#endif
	ODM_delay_us(10);
	odm_set_bb_reg(dm, R_0xc50, BIT(3) | BIT(2) | BIT(1) | BIT(0), 0x2);
#if 0
	/*	 ODM_sleep_ms(2);*/
#endif
	ODM_delay_us(10);
	odm_set_bb_reg(dm, R_0xc50, BIT(3) | BIT(2) | BIT(1) | BIT(0), 0x0);
	odm_write_4byte(dm, 0xc50, tmp1);
}

void _dpk_set_gain_scaling(struct dm_struct *dm, u8 path)
{
	u32 tmp1, tmp2, tmp3, tmp4, reg_1bfc;
	u32 lut_i = 0x0, lut_q = 0x0, lut_pw = 0x0, lut_pw_avg = 0x0;
	u16 gain_scaling = 0x0;

	tmp1 = odm_read_4byte(dm, 0x1b00);
	tmp2 = odm_read_4byte(dm, 0x1b08);
	tmp3 = odm_read_4byte(dm, 0x1bd4);
	tmp4 = odm_read_4byte(dm, 0x1bdc);

	odm_write_4byte(dm, 0x1b00, 0xf8000008);
	odm_write_4byte(dm, 0x1b08, 0x00000080);
	odm_write_4byte(dm, 0x1bd4, 0x00040001);
	odm_write_4byte(dm, 0x1bdc, 0xc0000081);

	reg_1bfc = odm_read_4byte(dm, 0x1bfc);
	lut_i = (reg_1bfc & 0x003ff800) >> 11;
	lut_q = (reg_1bfc & 0x00007ff);

	if ((lut_i & 0x400) == 0x400)
		lut_i = 0x800 - lut_i;
	if ((lut_q & 0x400) == 0x400)
		lut_q = 0x800 - lut_q;

	lut_pw = lut_i * lut_i + lut_q * lut_q;
	lut_pw_avg = (u32)(lut_i + lut_q) >> 1;
	gain_scaling = (u16)(0x800000 / lut_pw_avg);

	odm_set_bb_reg(dm, R_0x1b98, 0x0000ffff, gain_scaling);
	odm_set_bb_reg(dm, R_0x1b98, 0xffff0000, gain_scaling);

	odm_write_4byte(dm, 0x1b00, tmp1);
	odm_write_4byte(dm, 0x1b08, tmp2);
	odm_write_4byte(dm, 0x1bd4, tmp3);
	odm_write_4byte(dm, 0x1bdc, tmp4);

	RF_DBG(dm, DBG_RF_IQK,
	       "[IQK] reg_1bfc =0x%x, lut_pw =0x%x, lut_i = 0x%x, lut_q = 0x%x, lut_pw_avg = 0x%x, gain_scaling = 0x%x, 0x1b98 =0x%x!!!\n",
	       reg_1bfc, lut_pw, lut_i, lut_q, lut_pw_avg, gain_scaling,
	       odm_read_4byte(dm, 0x1b98));
}

void _dpk_set_dpk_pa_scan(struct dm_struct *dm, u8 path)
{
	u32 tmp1, tmp2, reg_1bfc;
	u32 pa_scan_i = 0x0, pa_scan_q = 0x0, pa_scan_pw = 0x0;
	u32 gainloss_back = 0x0;
#if 0
	/*	boolean pa_scan_search_fail = false;*/
#endif
	tmp1 = odm_read_4byte(dm, 0x1bcf);
	tmp2 = odm_read_4byte(dm, 0x1bd4);

	odm_write_4byte(dm, 0x1bcf, 0x11);
	odm_write_4byte(dm, 0x1bd4, 0x00060000);

	reg_1bfc = odm_read_4byte(dm, 0x1bfc);
	odm_write_4byte(dm, 0x1bcf, 0x15);
	pa_scan_i = (reg_1bfc & 0xffff0000) >> 16;
	pa_scan_q = (reg_1bfc & 0x0000ffff);

	if ((pa_scan_i & 0x8000) == 0x8000)
		pa_scan_i = 0x10000 - pa_scan_i;
	if ((pa_scan_q & 0x8000) == 0x8000)
		pa_scan_q = 0x10000 - pa_scan_q;

	pa_scan_pw = pa_scan_i * pa_scan_i + pa_scan_q * pa_scan_q;

	/*estimated pa_scan_pw*/
#if 0
	/*0dB => (512^2) * 10^(0/10) = 262144*/
	/*1dB => (512^2) * 10^(1/10) = 330019*/
	/*2dB => (512^2) * 10^(2/10) = 415470*/
	/*3dB => (512^2) * 10^(3/10) = 523046*/
	/*4dB => (512^2) * 10^(4/10) = 658475*/
	/*5dB => (512^2) * 10^(5/10) = 828972*/
	/*6dB => (512^2) * 10^(6/10) = 1043614*/
	/*7dB => (512^2) * 10^(7/10) = 1313832*/
	/*8dB => (512^2) * 10^(0/10) = 1654016*/
	/*9dB => (512^2) * 10^(1/10) = 2082283*/
	/*10dB => (512^2) * 10^(2/10) = 2621440*/
	/*11dB => (512^2) * 10^(3/10) = 3300197*/
	/*12dB => (512^2) * 10^(4/10) = 4154702*/
	/*13dB => (512^2) * 10^(5/10) = 5230460*/
	/*14dB => (512^2) * 10^(6/10) = 6584759*/
	/*15dB => (512^2) * 10^(7/10) = 8289721*/
#endif
	if (pa_scan_pw >= 0x7e7db9)
		pa_scan_pw = 0x0f;
	else if (pa_scan_pw >= 0x6479b7)
		pa_scan_pw = 0x0e;
	else if (pa_scan_pw >= 0x4fcf7c)
		pa_scan_pw = 0x0d;
	else if (pa_scan_pw >= 0x3f654e)
		pa_scan_pw = 0x0c;
	else if (pa_scan_pw >= 0x325b65)
		pa_scan_pw = 0x0b;
	else if (pa_scan_pw >= 0x280000)
		pa_scan_pw = 0x0a;
	else if (pa_scan_pw >= 0x1fc5eb)
		pa_scan_pw = 0x09;
	else if (pa_scan_pw >= 0x193d00)
		pa_scan_pw = 0x8;
	else if (pa_scan_pw >= 0x140c28)
		pa_scan_pw = 0x7;
	else if (pa_scan_pw >= 0xefc9e)
		pa_scan_pw = 0x6;
	else if (pa_scan_pw >= 0xca62c)
		pa_scan_pw = 0x5;
	else if (pa_scan_pw > 0xa0c2b)
		pa_scan_pw = 0x4;
	else if (pa_scan_pw > 0x7fb26)
		pa_scan_pw = 0x3;
	else if (pa_scan_pw > 0x656ee)
		pa_scan_pw = 0x2;
	else if (pa_scan_pw > 0x50923)
		pa_scan_pw = 0x1;
	else /*262144 >= pa_scan_pw*/
		pa_scan_pw = 0x0;

	odm_write_4byte(dm, 0x1bd4, 0x00060001);
	gainloss_back = (odm_read_4byte(dm, 0x1bfc) & 0x0000000f);

	if (gainloss_back <= 0xa)
		gainloss_back = 0xa - gainloss_back;

	if (gainloss_back > pa_scan_pw + 0x8)
		odm_set_rf_reg(dm, path, RF_0x8f, BIT(14) | BIT(13), 0x11);
	else if ((pa_scan_pw + 0x8 - gainloss_back) >= 0x6)
		odm_set_rf_reg(dm, path, RF_0x8f, BIT(14) | BIT(13), 0x00);
	else
#if 0
		/*if (0x6 >= (pa_scan_pw + 0x8 - gainloss_back)> 0x0 )*/
#endif
		odm_set_rf_reg(dm, path, RF_0x8f, BIT(14) | BIT(13), 0x01);

	RF_DBG(dm, DBG_RF_IQK,
	       "[IQK] reg_1bfc =0x%x, pa_scan_pw =0x%x, gainloss_back = 0x%x, pa_scan_i = 0x%x, pa_scan_q = 0x%x, RF0x8f = 0x%x, !!!\n",
	       reg_1bfc, pa_scan_pw, gainloss_back, pa_scan_i, pa_scan_q,
	       odm_get_rf_reg(dm, path, RF_0x8f, RFREGOFFSETMASK));

	odm_write_4byte(dm, 0x1bcf, tmp1);
	odm_write_4byte(dm, 0x1bd4, tmp2);
}

void _dpk_disable_bb_dynamic_pwr_threshold(struct dm_struct *dm, boolean flag)
{
	if (flag) /*disable BB dynamic pwr threshold hold*/
		odm_set_bb_reg(dm, R_0x1c74, BIT(31) | BIT(30) | BIT(29) | BIT(28), 0x0);
	else
		odm_set_bb_reg(dm, R_0x1c74, BIT(31) | BIT(30) | BIT(29) | BIT(28), 0x2);
	RF_DBG(dm, DBG_RF_IQK, "[DPK]\nset 0x1c74 = 0x%x\n",
	       odm_read_4byte(dm, 0x1c74));
}

void _dpk_set_dpk_sram_to_10_8821c(struct dm_struct *dm, u8 path)
{
	u32 tmp1, tmp2;
	u8 i;
	tmp1 = odm_read_4byte(dm, 0x1b00);
	tmp2 = odm_read_4byte(dm, 0x1b08);

	for (i = 0; i < 64; i++)
		odm_write_4byte(dm, 0x1bdc, 0xd0000001 + (i * 2) + 1);

	for (i = 0; i < 64; i++)
		odm_write_4byte(dm, 0x1bdc, 0x90000080 + (i * 2) + 1);

#if 0
	/*
	RF_DBG(dm, DBG_RF_IQK,
		"[DPK]return  txagc = 0x%x , 1bfc = 0x%x,rf00=0x%x\n",
		tmp4, odm_read_4byte(dm, 0x1bfc),
		odm_get_rf_reg(dm, path, RF_0x00, RFREGOFFSETMASK));
*/
#endif
	odm_write_4byte(dm, 0x1bdc, 0x0);
	odm_write_4byte(dm, 0x1b00, tmp1);
	odm_write_4byte(dm, 0x1b08, tmp2);
}

void _dpk_set_bbtxagc_8821c(struct dm_struct *dm, u8 path)
{
	u8 hw_rate, tmp;

	for (hw_rate = 0; hw_rate < 0x53; hw_rate++) {
		phydm_write_txagc_1byte_8821c(dm, 0x30, (enum rf_path)0x0, hw_rate);

		tmp = config_phydm_read_txagc_8821c(dm, (enum rf_path)0x0, hw_rate);
#if 0
		/*
	  RF_DBG(dm, DBG_RF_IQK,
		  "hw_rate =0x%x, tmp =0x%x \n", hw_rate, tmp);
*/
#endif
	}
}

u8 _dpk_get_txagcindpk_8821c(struct dm_struct *dm, u8 path)
{
	u32 tmp1, tmp2, tmp3;
	u8 tmp4;
	tmp1 = odm_read_4byte(dm, 0x1bcc);
	odm_set_bb_reg(dm, R_0x1bcc, BIT(26), 0x01);
	tmp2 = odm_read_4byte(dm, 0x1bd4);
	odm_set_bb_reg(dm, R_0x1bd4, BIT(20) | BIT(19) | BIT(18) | BIT(17) | BIT(16), 0x0a);
	tmp3 = odm_read_4byte(dm, 0x1bfc);
	tmp4 = ((u8)odm_get_bb_reg(dm, R_0x1bfc, MASKDWORD)) >> 2;
#if 0
	/*
	RF_DBG(dm, DBG_RF_IQK,
		"[DPK]return  txagc = 0x%x , 1bfc = 0x%x,rf00=0x%x\n",
		tmp4, odm_read_4byte(dm, 0x1bfc),
		odm_get_rf_reg(dm, path, RF_0x00, RFREGOFFSETMASK));
*/
#endif
	odm_write_4byte(dm, 0x1bcc, tmp1);
	odm_write_4byte(dm, 0x1bd4, tmp2);

	return tmp4;
}

void _dpk_ampmcurce_8821c(struct dm_struct *dm, u8 path)
{
	u8 i;
	RF_DBG(dm, DBG_RF_IQK, "[DPK] 0x1bcc = 0x%x, 0x1bb8 =0x%x\n",
	       odm_read_4byte(dm, 0x1bcc), odm_read_4byte(dm, 0x1bb8));

	odm_write_4byte(dm, 0x1bcc, 0x118f8800);
	for (i = 0; i < 8; i++) {
		odm_write_4byte(dm, 0x1b90, 0x0101e018 + i);
		odm_write_4byte(dm, 0x1bd4, 0x00060000);
		RF_DBG(dm, DBG_RF_IQK, "0x%x\n", odm_read_4byte(dm, 0x1bfc));
		odm_write_4byte(dm, 0x1bd4, 0x00070000);
		RF_DBG(dm, DBG_RF_IQK, "0x%x\n", odm_read_4byte(dm, 0x1bfc));
		odm_write_4byte(dm, 0x1bd4, 0x00080000);
		RF_DBG(dm, DBG_RF_IQK, "0x%x\n", odm_read_4byte(dm, 0x1bfc));
		odm_write_4byte(dm, 0x1bd4, 0x00090000);
		RF_DBG(dm, DBG_RF_IQK, "0x%x\n", odm_read_4byte(dm, 0x1bfc));
	}

	odm_write_4byte(dm, 0x1b90, 0x0001e018);
}
void _dpk_readsram_8821c(struct dm_struct *dm, u8 path)
{
	/* dbg message*/
	u8 i;
	odm_write_4byte(dm, 0x1b00, 0xf8000008);
	odm_write_4byte(dm, 0x1b08, 0x00000080);
	odm_write_4byte(dm, 0x1bd4, 0x00040001);

	RF_DBG(dm, DBG_RF_IQK, "[DPK] SRAM value!!!\n");

	for (i = 0; i < 64; i++) {
#if 0
		/*odm_write_4byte(dm, 0x1b90, 0x0101e018+i);*/
#endif
		odm_write_4byte(dm, 0x1bdc, 0xc0000081 + i * 2);

		RF_DBG(dm, DBG_RF_IQK, "0x%x\n", odm_read_4byte(dm, 0x1bfc));
	}
	odm_write_4byte(dm, 0x1bd4, 0x00050001);
	for (i = 0; i < 64; i++) {
#if 0
		/*odm_write_4byte(dm, 0x1b90, 0x0101e018+i);*/
#endif
		odm_write_4byte(dm, 0x1bdc, 0xc0000081 + i * 2);

		RF_DBG(dm, DBG_RF_IQK, "0x%x\n", odm_read_4byte(dm, 0x1bfc));
	}
#if 0
	/*ODM_sleep_ms(200);*/
#endif
	ODM_delay_ms(200);
#if 0
	/*odm_write_4byte(dm, 0x1b08, 0x00000080);*/
#endif
	odm_write_4byte(dm, 0x1bd4, 0xA0001);
	odm_write_4byte(dm, 0x1bdc, 0x00000000);
}

void _dpk_restore_8821c(struct dm_struct *dm, u8 path)
{
	odm_write_4byte(dm, 0xc60, 0x700B8040);
	odm_write_4byte(dm, 0xc60, 0x700B8040);
	odm_write_4byte(dm, 0xc60, 0x70146040);
	odm_write_4byte(dm, 0xc60, 0x70246040);
	odm_write_4byte(dm, 0xc60, 0x70346040);
	odm_write_4byte(dm, 0xc60, 0x70446040);
	odm_write_4byte(dm, 0xc60, 0x705B2040);
	odm_write_4byte(dm, 0xc60, 0x70646040);
	odm_write_4byte(dm, 0xc60, 0x707B8040);
	odm_write_4byte(dm, 0xc60, 0x708B8040);
	odm_write_4byte(dm, 0xc60, 0x709B8040);
	odm_write_4byte(dm, 0xc60, 0x70aB8040);
	odm_write_4byte(dm, 0xc60, 0x70bB6040);
	odm_write_4byte(dm, 0xc60, 0x70c06040);
	odm_write_4byte(dm, 0xc60, 0x70d06040);
	odm_write_4byte(dm, 0xc60, 0x70eF6040);
	odm_write_4byte(dm, 0xc60, 0x70f06040);

	odm_write_4byte(dm, 0xc58, 0xd8020402);
	odm_write_4byte(dm, 0xc5c, 0xde000120);
	odm_write_4byte(dm, 0xc6c, 0x0000122a);

	odm_write_4byte(dm, 0x808, 0x24028211);
#if 0
	/*odm_write_4byte(dm, 0x810, 0x211042A5);*/
#endif
	odm_write_4byte(dm, 0x90c, 0x13000000);
	odm_write_4byte(dm, 0x9a4, 0x80000088);
	odm_write_4byte(dm, 0xc94, 0x01000101);
	odm_write_4byte(dm, 0x1904, 0x00238000);
	odm_write_4byte(dm, 0x1904, 0x00228000);
	odm_write_4byte(dm, 0xC00, 0x00000007);
}

void _dpk_clear_sram_8821c(struct dm_struct *dm, u8 path)
{
	u8 i;

	/* write pwsf*/
	/*S3*/
	odm_write_4byte(dm, 0x1bdc, 0x40caffe1);
	odm_write_4byte(dm, 0x1bdc, 0x4080a1e3);
	odm_write_4byte(dm, 0x1bdc, 0x405165e5);
	odm_write_4byte(dm, 0x1bdc, 0x403340e7);
	odm_write_4byte(dm, 0x1bdc, 0x402028e9);
	odm_write_4byte(dm, 0x1bdc, 0x401419eb);
	odm_write_4byte(dm, 0x1bdc, 0x400d10ed);
	odm_write_4byte(dm, 0x1bdc, 0x40080aef);

	odm_write_4byte(dm, 0x1bdc, 0x400506f1);
	odm_write_4byte(dm, 0x1bdc, 0x400304f3);
	odm_write_4byte(dm, 0x1bdc, 0x400203f5);
	odm_write_4byte(dm, 0x1bdc, 0x400102f7);
	odm_write_4byte(dm, 0x1bdc, 0x400101f9);
	odm_write_4byte(dm, 0x1bdc, 0x400101fb);
	odm_write_4byte(dm, 0x1bdc, 0x400101fd);
	odm_write_4byte(dm, 0x1bdc, 0x400101ff);
	/*S0*/
	odm_write_4byte(dm, 0x1bdc, 0x40caff81);
	odm_write_4byte(dm, 0x1bdc, 0x4080a183);
	odm_write_4byte(dm, 0x1bdc, 0x40516585);
	odm_write_4byte(dm, 0x1bdc, 0x40334087);
	odm_write_4byte(dm, 0x1bdc, 0x40202889);
	odm_write_4byte(dm, 0x1bdc, 0x4014198b);
	odm_write_4byte(dm, 0x1bdc, 0x400d108d);
	odm_write_4byte(dm, 0x1bdc, 0x40080a8f);
	odm_write_4byte(dm, 0x1bdc, 0x40050691);
	odm_write_4byte(dm, 0x1bdc, 0x40030493);
	odm_write_4byte(dm, 0x1bdc, 0x40020395);
	odm_write_4byte(dm, 0x1bdc, 0x40010297);
	odm_write_4byte(dm, 0x1bdc, 0x40010199);
	odm_write_4byte(dm, 0x1bdc, 0x4001019b);
	odm_write_4byte(dm, 0x1bdc, 0x4001019d);
	odm_write_4byte(dm, 0x1bdc, 0x4001019f);

	odm_write_4byte(dm, 0x1bdc, 0x00000000);

	/*clear sram even*/
	for (i = 0; i < 0x40; i++)
		odm_write_4byte(dm, 0x1bdc, 0xd0000000 + ((i * 2) + 1));
	/*clear sram odd*/
	for (i = 0; i < 0x40; i++)
		odm_write_4byte(dm, 0x1bdc, 0x90000080 + ((i * 2) + 1));

	odm_write_4byte(dm, 0x1bdc, 0x0);
	RF_DBG(dm, DBG_RF_IQK, "[DPK]==========write pwsf and clear sram/n");
}

void _dpk_setting_8821c(struct dm_struct *dm, u8 path)
{
	RF_DBG(dm, DBG_RF_IQK,
	       "[DPK]==========Start the DPD setting Initilaize/n");
	/*AFE setting*/
	odm_write_4byte(dm, 0xc60, 0x50000000);
	odm_write_4byte(dm, 0xc60, 0x700F0040);
	odm_write_4byte(dm, 0xc5c, 0xd1000120);
	odm_write_4byte(dm, 0xc58, 0xd8000402);
	odm_write_4byte(dm, 0xc6c, 0x00000a15);
	odm_write_4byte(dm, 0xc00, 0x00000004);
#if 0
	/*_iqk_bb_reset_8821c(dm);*/
#endif
	odm_write_4byte(dm, 0xe5c, 0xD1000120);
	odm_write_4byte(dm, 0xc6c, 0x00000A15);
	odm_write_4byte(dm, 0xe6c, 0x00000A15);
	odm_write_4byte(dm, 0x808, 0x2D028200);
#if 0
	/*odm_write_4byte(dm, 0x810, 0x211042A5);*/
#endif
	odm_write_4byte(dm, 0x8f4, 0x00d80fb1);
	odm_write_4byte(dm, 0x90c, 0x0B00C000);
	odm_write_4byte(dm, 0x9a4, 0x00000080);
	odm_write_4byte(dm, 0xc94, 0x01000101);
	odm_write_4byte(dm, 0xe94, 0x01000101);
	odm_write_4byte(dm, 0xe5c, 0xD1000120);
	odm_write_4byte(dm, 0xc6c, 0x00000A15);
	odm_write_4byte(dm, 0xe6c, 0x00000A15);
	odm_write_4byte(dm, 0x1904, 0x00020000);
	/*path A*/
	/*RF*/
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xef, RFREGOFFSETMASK, 0x80000);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, RFREGOFFSETMASK, 0x00024);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x3e, RFREGOFFSETMASK, 0x0003F);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x3f, RFREGOFFSETMASK, 0xCBFCE);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xef, RFREGOFFSETMASK, 0x00000);
	/*AGC boundary selection*/
	odm_write_4byte(dm, 0x1bbc, 0x0001abf6);
	odm_write_4byte(dm, 0x1b90, 0x0001e018);
	odm_write_4byte(dm, 0x1bb8, 0x000fffff);
	odm_write_4byte(dm, 0x1bc8, 0x000c55aa);
#if 0
	/*odm_write_4byte(dm, 0x1bcc, 0x11978200);*/
#endif
	odm_write_4byte(dm, 0x1bcc, 0x11978800);
#if 0
	/*odm_write_4byte(dm, 0xcb0, 0x77775747);*/
	/*odm_write_4byte(dm, 0xcb4, 0x100000f7);*/
	/*odm_write_4byte(dm, 0xcb4, 0x10000007);*/
	/*odm_write_4byte(dm, 0xcbc, 0x0);*/
#endif
}

void _dpk_dynamic_bias_8821c(struct dm_struct *dm, u8 path, u8 dynamicbias)
{
	u32 tmp;
	tmp = odm_get_rf_reg(dm, RF_PATH_A, RF_0xdf, RFREGOFFSETMASK);
	tmp = tmp | BIT(8);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xdf, RFREGOFFSETMASK, tmp);
	if ((*dm->band_type == ODM_BAND_5G) && (*dm->band_width == 1))
		odm_set_rf_reg(dm, path, RF_0x61, BIT(7) | BIT(6) | BIT(5) | BIT(4), dynamicbias);
	if ((*dm->band_type == ODM_BAND_5G) && (*dm->band_width == 2))
		odm_set_rf_reg(dm, path, RF_0x61, BIT(7) | BIT(6) | BIT(5) | BIT(4), dynamicbias);
	tmp = tmp & (~BIT(8));
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xdf, RFREGOFFSETMASK, tmp);
	RF_DBG(dm, DBG_RF_IQK, "[DPK]Set DynamicBias 0xdf=0x%x, 0x61=0x%x\n",
	       odm_get_rf_reg(dm, RF_PATH_A, RF_0xdf, RFREGOFFSETMASK),
	       odm_get_rf_reg(dm, RF_PATH_A, RF_0x61, RFREGOFFSETMASK));
}

void _dpk_boundary_selection_8821c(struct dm_struct *dm, u8 path)
{
	u8 tmp_pad, compared_pad, compared_txbb;
	u32 rf_backup_reg00;
	u8 i = 0;
	u8 j = 1;
	u32 boundaryselect = 0;
	struct dm_iqk_info *iqk_info = &dm->IQK_info;

	RF_DBG(dm, DBG_RF_IQK, "[DPK]Start the DPD boundary selection\n");
	rf_backup_reg00 = odm_get_rf_reg(dm, (enum rf_path)path, RF_0x00, RFREGOFFSETMASK);
	tmp_pad = 0;
	compared_pad = 0;
	boundaryselect = 0;
#if dpk_forcein_sram4
	for (i = 0x1f; i > 0x0; i--) { /*i=tx index*/
		odm_set_rf_reg(dm, RF_PATH_A, RF_0x00, RFREGOFFSETMASK, 0x20000 + i);

		if (iqk_info->is_btg) {
			compared_pad = (u8)((0x1c000 & odm_get_rf_reg(dm, (enum rf_path)path, RF_0x78, RFREGOFFSETMASK)) >> 14);
			compared_txbb = (u8)((0x07C00 & odm_get_rf_reg(dm, (enum rf_path)path, RF_0x5c, RFREGOFFSETMASK)) >> 10);
		} else {
			compared_pad = (u8)((0xe0 & odm_get_rf_reg(dm, (enum rf_path)path, RF_0x56, RFREGOFFSETMASK)) >> 5);
			compared_txbb = (u8)((0x1f & odm_get_rf_reg(dm, (enum rf_path)path, RF_0x56, RFREGOFFSETMASK)));
		}
		if (i == 0x1f) {
			/*boundaryselect = compared_txbb;*/
			boundaryselect = 0x1f;
			tmp_pad = compared_pad;
		}
		if (compared_pad < tmp_pad) {
			boundaryselect = boundaryselect + (i << (j * 5));
			tmp_pad = compared_pad;
			j++;
		}

		if (j >= 4)
			break;
	}

#else
	boundaryselect = 0x0;
#endif
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x00, RFREGOFFSETMASK, rf_backup_reg00);
	odm_write_4byte(dm, 0x1bbc, boundaryselect);
}

u8 _dpk_get_dpk_tx_agc_8821c(struct dm_struct *dm, u8 path)
{
	u8 tx_agc_init_value = 0x1f; /* DPK TXAGC value*/
	u32 rf_reg00 = 0x0;
	u8 gainloss = 0x1;
	u8 best_tx_agc, txagcindpk = 0x0;
	u8 tmp;
	boolean fail = true;
	u32 IQK_CMD = 0xf8000d18;

	/* rf_reg00 = 0x40000 + tx_agc_init_value;  set TXAGC value */
	if (*dm->band_type == ODM_BAND_5G) {
		tx_agc_init_value = 0x1c;
		rf_reg00 = 0x40000 + tx_agc_init_value; /* set TXAGC value*/
#if 0
		/*rf_reg00 = 0x54000 + tx_agc_init_value;*/ /* set TXAGC value*/
#endif
		odm_write_4byte(dm, 0x1bc8, 0x000c55aa);
		odm_set_rf_reg(dm, RF_PATH_A, RF_0x8f, RFREGOFFSETMASK, 0xa9c00);
	} else {
		tx_agc_init_value = 0x17;
		rf_reg00 = 0x44000 + tx_agc_init_value; /* set TXAGC value*/
#if 0
		/*rf_reg00 = 0x54000 + tx_agc_init_value; */ /* set TXAGC value*/
#endif
		odm_write_4byte(dm, 0x1bc8, 0x000c44aa);
		odm_set_rf_reg(dm, RF_PATH_A, RF_0x8f, RFREGOFFSETMASK, 0xaec00);
	}
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x00, RFREGOFFSETMASK, rf_reg00);
	odm_set_bb_reg(dm, R_0x1b8c, BIT(15) | BIT(14) | BIT(13), gainloss);
	odm_set_bb_reg(dm, R_0x1bc8, BIT(31), 0x1);
	odm_set_bb_reg(dm, R_0x8f8, BIT(25) | BIT(24) | BIT(23) | BIT(22), 0x5);

	_dpk_set_bbtxagc_8821c(dm, RF_PATH_A);
	txagcindpk = _dpk_get_txagcindpk_8821c(dm, RF_PATH_A);

	if (odm_get_rf_reg(dm, path, RF_0x08, RFREGOFFSETMASK) != 0x0)
		odm_set_rf_reg(dm, path, RF_0x8, RFREGOFFSETMASK, 0x0);
	;
#if 0
	/*ODM_sleep_ms(1);*/
#endif
	ODM_delay_us(10);
	odm_write_4byte(dm, 0x1b00, IQK_CMD);
	odm_write_4byte(dm, 0x1b00, IQK_CMD + 1);

	fail = _iqk_check_nctl_done_8821c(dm, path, IQK_CMD);

	odm_write_4byte(dm, 0x1b90, 0x0001e018);
#if 0
	/*
	RF_DBG(dm, DBG_RF_IQK,
		"[DPK]rf_reg00 =0x%x, 0x8F =0x%x, txagcindpk =0x%x\n",
		odm_get_rf_reg(dm, path, RF_0x00, RFREGOFFSETMASK),
		odm_get_rf_reg(dm, path, RF_0x8f, RFREGOFFSETMASK),txagcindpk);
*/
#endif

	odm_write_4byte(dm, 0x1bd4, 0x60001);
	tmp = (u8)odm_read_4byte(dm, 0x1bfc);
	best_tx_agc = tx_agc_init_value - (0xa - tmp);
#if 0
/*
	RF_DBG(dm, DBG_RF_IQK,
		"[DPK](2), 0x1b8c =0x%x, rf_reg00 = 0x%x, 0x1b00 = 0x%x, 0x1bfc = 0x%x, 0x1bd4 = 0x%x,best_tx_agc =0x%x, 0x1bfc[7:0] =0x%x\n",
		odm_read_4byte(dm, 0x1b8c),
		odm_get_rf_reg(dm, path, RF_0x00, RFREGOFFSETMASK),
		odm_read_4byte(dm, 0x1b00),
		odm_read_4byte(dm, 0x1bfc), odm_read_4byte(dm, 0x1bd4),
		best_tx_agc, tmp);
*/
#endif
/* dbg message*/
#if 0

	RF_DBG(dm, DBG_RF_IQK, "[DPK] 0x1bcc = 0x%x, 0x1bb8 =0x%x\n",
	       odm_read_4byte(dm, 0x1bcc), odm_read_4byte(dm, 0x1bb8));

	odm_write_4byte(dm, 0x1bcc, 0x118f8800);
	for (i = 0 ; i < 8; i++) {
		odm_write_4byte(dm, 0x1b90, 0x0101e018 + i);
		odm_write_4byte(dm, 0x1bd4, 0x00060000);
		RF_DBG(dm, DBG_RF_IQK, "0x%x\n", odm_read_4byte(dm, 0x1bfc));
		odm_write_4byte(dm, 0x1bd4, 0x00070000);
		RF_DBG(dm, DBG_RF_IQK, "0x%x\n", odm_read_4byte(dm, 0x1bfc));
		odm_write_4byte(dm, 0x1bd4, 0x00080000);
		RF_DBG(dm, DBG_RF_IQK, "0x%x\n", odm_read_4byte(dm, 0x1bfc));
		odm_write_4byte(dm, 0x1bd4, 0x00090000);
		RF_DBG(dm, DBG_RF_IQK, "0x%x\n", odm_read_4byte(dm, 0x1bfc));
	}

	odm_write_4byte(dm, 0x1b90, 0x0001e018);

#endif
	return best_tx_agc;
}

boolean
_dpk_enable_dpk_8821c(struct dm_struct *dm, u8 path, u8 best_tx_agc)
{
	u32 rf_reg00 = 0x0;
	boolean fail = true;
	u32 IQK_CMD = 0xf8000e18;

	if (*dm->band_type == ODM_BAND_5G)
		rf_reg00 = 0x40000 + best_tx_agc; /* set TXAGC value*/
	else
		rf_reg00 = 0x44000 + best_tx_agc; /* set TXAGC value*/
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x00, RFREGOFFSETMASK, rf_reg00);
	_dpk_set_dpk_pa_scan(dm, RF_PATH_A);

	/*ODM_sleep_ms(1);*/
	ODM_delay_us(10);
	odm_set_bb_reg(dm, R_0x1bc8, BIT(31), 0x1);
	odm_write_4byte(dm, 0x8f8, 0x41400080);

	if (odm_get_rf_reg(dm, path, RF_0x08, RFREGOFFSETMASK) != 0x0)
		odm_set_rf_reg(dm, path, RF_0x8, RFREGOFFSETMASK, 0x0);

	/*ODM_sleep_ms(1);*/
	ODM_delay_us(10);
	odm_write_4byte(dm, 0x1b00, IQK_CMD);
	odm_write_4byte(dm, 0x1b00, IQK_CMD + 1);

	odm_write_4byte(dm, 0x1b90, 0x0001e018);
	odm_write_4byte(dm, 0x1bd4, 0xA0001);
	fail = _iqk_check_nctl_done_8821c(dm, path, IQK_CMD);
#if 0
/*
	RF_DBG(dm, DBG_RF_IQK,
		"[DPK] (3) 0x1b0b = 0x%x, 0x1bc8 = 0x%x, rf_reg00 = 0x%x, ,0x1bfc = 0x%x, 0x1b90=0x%x, 0x1b94=0x%x\n",
		odm_read_1byte(dm, 0x1b0b), odm_read_4byte(dm, 0x1bc8), odm_get_rf_reg(dm, path, RF_0x00, RFREGOFFSETMASK),
		odm_read_4byte(dm, 0x1bfc), odm_read_4byte(dm, 0x1b90), odm_read_4byte(dm, 0x1b94));
*/
#endif
#if 0 /* dbg message*/
	u8 delay_count = 0x0;
    u8 i;
	u32 tmp;
	odm_write_4byte(dm, 0x1b00, 0xf8000008);
	odm_write_4byte(dm, 0x1b08, 0x00000080);
	odm_write_4byte(dm, 0x1bd4, 0x00040001);

	RF_DBG(dm, DBG_RF_IQK, "[DPK] SRAM value!!!\n");

	for (i = 0 ; i < 64; i++) {
		/*odm_write_4byte(dm, 0x1b90, 0x0101e018+i);*/
		odm_write_4byte(dm, 0x1bdc, 0xc0000081 + i * 2);

		RF_DBG(dm, DBG_RF_IQK, "0x%x\n", odm_read_4byte(dm, 0x1bfc));
	}
	odm_write_4byte(dm, 0x1bd4, 0x00050001);
	for (i = 0 ; i < 64; i++) {
		/*odm_write_4byte(dm, 0x1b90, 0x0101e018+i);*/
		odm_write_4byte(dm, 0x1bdc, 0xc0000081 + i * 2);

		RF_DBG(dm, DBG_RF_IQK, "0x%x\n", odm_read_4byte(dm, 0x1bfc));
	}

	/*odm_write_4byte(dm, 0x1b08, 0x00000080);*/
	odm_write_4byte(dm, 0x1bd4, 0xA0001);
	odm_write_4byte(dm, 0x1bdc, 0x00000000);
#endif
	return fail;
}

boolean
_dpk_enable_dpd_8821c(struct dm_struct *dm, u8 path, u8 best_tx_agc)
{
	boolean fail = true;
	u8 offset = 0x0;
	u32 IQK_CMD = 0xf8000f18;
	u8 external_pswf_gain;
	boolean gain_scaling_enable = false;

	odm_set_bb_reg(dm, R_0x1bc8, BIT(31), 0x1);
	odm_write_4byte(dm, 0x8f8, 0x41400080);
	if (odm_get_rf_reg(dm, path, RF_0x08, RFREGOFFSETMASK) != 0x0)
		odm_set_rf_reg(dm, path, RF_0x8, RFREGOFFSETMASK, 0x0);
	/*ODM_sleep_ms(1);*/
	ODM_delay_us(10);
	odm_write_4byte(dm, 0x1b00, IQK_CMD);
	odm_write_4byte(dm, 0x1b00, IQK_CMD + 1);
	fail = _iqk_check_nctl_done_8821c(dm, path, IQK_CMD);

	odm_write_4byte(dm, 0x1b90, 0x0001e018);
	odm_write_4byte(dm, 0x1bd4, 0xA0001);

	if (!fail) {
		odm_write_4byte(dm, 0x1bcf, 0x19);
		odm_write_4byte(dm, 0x1bdc, 0x0);
		/*add 2db extrnal for compensate performnace, the reason is unknown*/
		external_pswf_gain = 0x2;
		best_tx_agc = best_tx_agc + external_pswf_gain;

		if (best_tx_agc >= 0x19)
			offset = best_tx_agc - 0x19;
		else
			offset = 0x20 - (0x19 - best_tx_agc);
		odm_set_bb_reg(dm, R_0x1bd0, BIT(12) | BIT(11) | BIT(10) | BIT(9) | BIT(8), offset);
		if (gain_scaling_enable)
			_dpk_set_gain_scaling(dm, RF_PATH_A);
		else
			odm_write_4byte(dm, 0x1b98, 0x4c004c00);

	} else {
		RF_DBG(dm, DBG_RF_IQK,
		       "[DPK](4)0x1b08 =%x, 0x1bc8 = 0x%x,0x1bfc = 0x%x, ,0x1bd0 = 0x%x, offset =%x, 1bcc =%x\n",
		       odm_read_4byte(dm, 0x1b08), odm_read_4byte(dm, 0x1bc8),
		       odm_read_1byte(dm, 0x1bfc), odm_read_4byte(dm, 0x1bd0),
		       offset, odm_read_4byte(dm, 0x1bcc));
	}

	return fail;
}

void _phy_dpd_calibrate_8821c(struct dm_struct *dm, boolean reset)
{
	u32 backup_dpdbb[3];
	u8 best_tx_agc = 0x1c;
	u32 MAC_backup[MAC_REG_NUM_8821C], RF_backup[RF_REG_NUM_8821C][1];
	u32 backup_mac_reg[MAC_REG_NUM_8821C] = {0x520, 0x550, 0x1518};
	u32 BB_backup[DPK_BB_REG_NUM_8821C];
	u32 backup_bb_reg[DPK_BB_REG_NUM_8821C] = {0x808, 0x90c, 0xc00, 0xcb0, 0xcb4, 0xcbc, 0x1990, 0x9a4, 0xa04, 0xc58, 0xc5c, 0xe58, 0xe5c, 0xc6c, 0xe6c, 0x90c, 0xc94, 0xe94, 0x1904, 0xcb0, 0xcb4, 0xcbc, 0xc00};
	u32 backup_rf_reg[RF_REG_NUM_8821C] = {0xdf, 0xde, 0x8f, 0x0, 0x1};
	u8 i;
	u32 backup_dpk_reg[3] = {0x1bd0, 0x1b98, 0x1bbc};

	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	iqk_info->is_btg = (boolean)odm_get_bb_reg(dm, R_0xcb8, BIT(16));
	if (!(*dm->mp_mode))
		if (_iqk_reload_iqk_8821c(dm, reset))
			return;
	if (!(*dm->band_type == ODM_BAND_5G))
		return;

	RF_DBG(dm, DBG_RF_IQK, "[DPK]==========DPK strat!!!!!==========\n");
	RF_DBG(dm, DBG_RF_IQK,
	       "[DPK]band_type = %s, band_width = %d, ExtPA2G = %d, ext_pa_5g = %d\n",
	       (*dm->band_type == ODM_BAND_5G) ? "5G" : "2G", *dm->band_width,
	       dm->ext_pa, dm->ext_pa_5g);
	_iqk_backup_mac_bb_8821c(dm, MAC_backup, BB_backup, backup_mac_reg, backup_bb_reg, DPK_BB_REG_NUM_8821C);
	_iqk_afe_setting_8821c(dm, true);
	_iqk_backup_rf_8821c(dm, RF_backup, backup_rf_reg);

	if (iqk_info->is_btg) {
	} else {
		if (*dm->band_type == ODM_BAND_2_4G)
			odm_set_bb_reg(dm, R_0xcb8, BIT(8), 0x1);
		else
			odm_set_bb_reg(dm, R_0xcb8, BIT(8), 0x0);
	}

	/*backup 0x1b2c, 1b38,0x1b3c*/
	backup_dpdbb[0] = odm_read_4byte(dm, 0x1b2c);
	backup_dpdbb[1] = odm_read_4byte(dm, 0x1b38);
	backup_dpdbb[2] = odm_read_4byte(dm, 0x1b3c);
	RF_DBG(dm, DBG_RF_IQK, "[DPK]In DPD Process(1), Backup\n");

	/*PDK Init Register setting*/
	_dpk_clear_sram_8821c(dm, RF_PATH_A);
	_dpk_setting_8821c(dm, RF_PATH_A);
	_dpk_boundary_selection_8821c(dm, RF_PATH_A);
	odm_set_bb_reg(dm, R_0x1bc8, BIT(31), 0x1);
	odm_set_bb_reg(dm, R_0x8f8, BIT(25) | BIT(24) | BIT(23) | BIT(22), 0x5);
	/* Get the best TXAGC*/

	best_tx_agc = _dpk_get_dpk_tx_agc_8821c(dm, RF_PATH_A);
#if 0
	/*ODM_sleep_ms(2);*/
#endif

	ODM_delay_us(10);
	RF_DBG(dm, DBG_RF_IQK, "[DPK]In DPD Process(2), Best TXAGC = 0x%x\n",
	       best_tx_agc);

	if (_dpk_enable_dpk_8821c(dm, RF_PATH_A, best_tx_agc)) {
		RF_DBG(dm, DBG_RF_IQK,
		       "[DPK]In DPD Process(3), DPK process is Fail\n");
	}
#if 0
	/*ODM_sleep_ms(2);*/
#endif
	ODM_delay_us(10);
	if (_dpk_enable_dpd_8821c(dm, RF_PATH_A, best_tx_agc)) {
		RF_DBG(dm, DBG_RF_IQK,
		       "[DPK]In DPD Process(4), DPD process is Fail\n");
	}
	/* restore IQK */
	iqk_info->rf_reg18 = odm_get_rf_reg(dm, RF_PATH_A, RF_0x18, RFREGOFFSETMASK);
	_iqk_reload_iqk_setting_8821c(dm, 0, 2);
	_iqk_fill_iqk_report_8821c(dm, 0);
#if 0
	/*
	RF_DBG(dm, DBG_RF_IQK,
		"[DPK]reload IQK result before, iqk_info->rf_reg18=0x%x, iqk_info->iqk_channel[0]=0x%x, iqk_info->iqk_channel[1]=0x%x!!!!\n",
		iqk_info->rf_reg18, iqk_info->iqk_channel[0], iqk_info->iqk_channel[1]);
      */
#endif

	/* Restore setup */
#if 0
	/*_dpk_readsram_8821c(dm, RF_PATH_A);*/
#endif
	_dpk_restore_8821c(dm, RF_PATH_A);
	odm_set_bb_reg(dm, R_0x8f8, BIT(25) | BIT(24) | BIT(23) | BIT(22), 0x5);
	odm_set_bb_reg(dm, R_0x1bd4, BIT(20) | BIT(19) | BIT(18) | BIT(17) | BIT(16), 0x0);
	odm_set_bb_reg(dm, R_0x1b00, BIT(2) | BIT(1), 0x0);
	odm_set_bb_reg(dm, R_0x1b08, BIT(6) | BIT(5), 0x2);

	odm_write_4byte(dm, 0x1b2c, backup_dpdbb[0]);
	odm_write_4byte(dm, 0x1b38, backup_dpdbb[1]);
	odm_write_4byte(dm, 0x1b3c, backup_dpdbb[2]);
	/*enable DPK*/
	odm_set_bb_reg(dm, R_0x1b2c, BIT(7) | BIT(6) | BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0), 0x5);
/*enable boundary condition*/
#if dpk_forcein_sram4 /* disable : froce in sram4*/
	odm_set_bb_reg(dm, R_0x1bcc, BIT(27), 0x1);
#endif
	odm_write_4byte(dm, 0x1bcc, 0x11868800);
	RF_DBG(dm, DBG_RF_IQK, "[DPK]In DPD Process(5), Restore\n");
	_iqk_restore_mac_bb_8821c(dm, MAC_backup, BB_backup, backup_mac_reg, backup_bb_reg, DPK_BB_REG_NUM_8821C);
	_iqk_afe_setting_8821c(dm, false);
	_iqk_restore_rf_8821c(dm, backup_rf_reg, RF_backup);
	/*toggle dynamic_pwr_threshold*/

	/* backup the DPK current result*/
	for (i = 0; i < DPK_BACKUP_REG_NUM_8821C; i++)
		dpk_result[i] = odm_read_4byte(dm, backup_dpk_reg[i]);

	RF_DBG(dm, DBG_RF_IQK,
	       "[DPK]the DPD calibration Process Finish (6), 0x1bd0 = 0x%x, 0x1b98 = 0x%x, 0x1bbc0= 0x%x\n",
	       dpk_result[0], dpk_result[1], dpk_result[2]);
}

u32 _iqk_tximr_selfcheck_8821c(void *dm_void, u8 tone_index, u8 path)
{
	u32 tx_ini_power_H[2], tx_ini_power_L[2];
	u32 tmp1, tmp2, tmp3, tmp4, tmp5;
	u32 IQK_CMD;
	u32 tximr = 0x0;
	u8 i;

	struct dm_struct *dm = (struct dm_struct *)dm_void;
	/*backup*/
	odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);
	odm_write_4byte(dm, 0x1bc8, 0x80000000);
	odm_write_4byte(dm, 0x8f8, 0x41400080);
	tmp1 = odm_read_4byte(dm, 0x1b0c);
	tmp2 = odm_read_4byte(dm, 0x1b14);
	tmp3 = odm_read_4byte(dm, 0x1b1c);
	tmp4 = odm_read_4byte(dm, 0x1b20);
	tmp5 = odm_read_4byte(dm, 0x1b24);
	/*setup*/
	odm_write_4byte(dm, 0x1b0c, 0x00003000);
	odm_write_4byte(dm, 0x1b1c, 0xA2193C32);
	odm_write_1byte(dm, 0x1b15, 0x00);
	odm_write_4byte(dm, 0x1b20, (u32)(tone_index << 20 | 0x00040008));
	odm_write_4byte(dm, 0x1b24, (u32)(tone_index << 20 | 0x00060008));
	odm_write_4byte(dm, 0x1b2c, 0x07);
	odm_write_4byte(dm, 0x1b38, 0x20000000);
	odm_write_4byte(dm, 0x1b3c, 0x20000000);
	/* ======derive pwr1========*/
	for (i = 0; i < 2; i++) {
		if (i == 0)
			odm_write_4byte(dm, 0x1bcc, 0x0f);
		else
			odm_write_4byte(dm, 0x1bcc, 0x09);
		/* One Shot*/
		IQK_CMD = 0x00000800;
		odm_write_4byte(dm, 0x1b34, IQK_CMD + 1);
		odm_write_4byte(dm, 0x1b34, IQK_CMD);
		ODM_delay_us(10);
		odm_write_4byte(dm, 0x1bd4, 0x00040001);
		tx_ini_power_H[i] = odm_read_4byte(dm, 0x1bfc);
		odm_write_4byte(dm, 0x1bd4, 0x000C0001);
		tx_ini_power_L[i] = odm_read_4byte(dm, 0x1bfc);
	}
	/*restore*/
	odm_write_4byte(dm, 0x1b0c, tmp1);
	odm_write_4byte(dm, 0x1b14, tmp2);
	odm_write_4byte(dm, 0x1b1c, tmp3);
	odm_write_4byte(dm, 0x1b20, tmp4);
	odm_write_4byte(dm, 0x1b24, tmp5);

	if (tx_ini_power_H[1] == tx_ini_power_H[0])
		tximr = (3 * (halrf_psd_log2base(tx_ini_power_L[0] << 2) - halrf_psd_log2base(tx_ini_power_L[1]))) / 100;
	else
		tximr = 0;
	return tximr;
}

u32 _iqk_rximr_selfcheck_8821c(void *dm_void, u8 tone_index, u8 path,
			       u32 tmp1b38)
{
	u32 rx_ini_power_H[2], rx_ini_power_L[2]; /*[0]: psd tone; [1]: image tone*/
	u32 tmp1, tmp2, tmp3, tmp4, tmp5;
	u32 IQK_CMD, tmp1bcc;
	u8 i;
	u32 rximr = 0x0;

	struct dm_struct *dm = (struct dm_struct *)dm_void;

	/*backup*/
	odm_write_4byte(dm, 0x1b00, 0xf8000008 | path << 1);
	tmp1 = odm_read_4byte(dm, 0x1b0c);
	tmp2 = odm_read_4byte(dm, 0x1b14);
	tmp3 = odm_read_4byte(dm, 0x1b1c);
	tmp4 = odm_read_4byte(dm, 0x1b20);
	tmp5 = odm_read_4byte(dm, 0x1b24);

	tmp1bcc = (odm_read_4byte(dm, 0x1be8) & 0x0000ff00) >> 8;
	odm_write_4byte(dm, 0x1b0c, 0x00001000);
	odm_write_1byte(dm, 0x1b15, 0x00);
	odm_write_4byte(dm, 0x1b1c, 0x82193d31);
	odm_write_4byte(dm, 0x1b20, (u32)(tone_index << 20 | 0x00040008));
	odm_write_4byte(dm, 0x1b24, (u32)(tone_index << 20 | 0x00060048));
	odm_write_4byte(dm, 0x1b2c, 0x07);
#if 0
	//odm_write_4byte(dm, 0x1b38, iqk_info->rxk1_tmp1b38[path][(tone_index&0xff0)>>4]);
#endif
	odm_write_4byte(dm, 0x1b38, tmp1b38);

	odm_write_4byte(dm, 0x1b3c, 0x20000000);
	odm_write_4byte(dm, 0x1bcc, tmp1bcc);
	for (i = 0; i < 2; i++) {
		if (i == 0)
			odm_write_4byte(dm, 0x1b1c, 0x82193d31);
		else
			odm_write_4byte(dm, 0x1b1c, 0xA2193d31);
		IQK_CMD = 0x00000800;
		odm_write_4byte(dm, 0x1b34, IQK_CMD + 1);
		odm_write_4byte(dm, 0x1b34, IQK_CMD);
		ODM_delay_us(2000);
		odm_write_1byte(dm, 0x1bd6, 0xb);
#if 0
		/*if ((boolean)odm_get_bb_reg(dm, R_0x1bfc, BIT(1))){*/
#endif
		if (1) {
			odm_write_1byte(dm, 0x1bd6, 0x5);
			rx_ini_power_H[i] = odm_read_4byte(dm, 0x1bfc);
			odm_write_1byte(dm, 0x1bd6, 0xe);
			rx_ini_power_L[i] = odm_read_4byte(dm, 0x1bfc);
		} else {
			rx_ini_power_H[i] = 0x0;
			rx_ini_power_L[i] = 0x0;
		}
	}
	/*restore*/
	odm_write_4byte(dm, 0x1b0c, tmp1);
	odm_write_4byte(dm, 0x1b14, tmp2);
	odm_write_4byte(dm, 0x1b1c, tmp3);
	odm_write_4byte(dm, 0x1b20, tmp4);
	odm_write_4byte(dm, 0x1b24, tmp5);

	for (i = 0; i < 2; i++)
		rx_ini_power_H[i] = (rx_ini_power_H[i] & 0xf8000000) >> 27;

	if (rx_ini_power_H[0] != rx_ini_power_H[1])
		switch (rx_ini_power_H[0]) {
		case 1:
			rx_ini_power_L[0] = (u32)((rx_ini_power_L[0] >> 1) | 0x80000000);
			rx_ini_power_L[1] = (u32)rx_ini_power_L[1] >> 1;
			break;
		case 2:
			rx_ini_power_L[0] = (u32)((rx_ini_power_L[0] >> 2) | 0x80000000);
			rx_ini_power_L[1] = (u32)rx_ini_power_L[1] >> 2;
			break;
		case 3:
			rx_ini_power_L[0] = (u32)((rx_ini_power_L[0] >> 2) | 0xc0000000);
			rx_ini_power_L[1] = (u32)rx_ini_power_L[1] >> 2;
			break;
		case 4:
			rx_ini_power_L[0] = (u32)((rx_ini_power_L[0] >> 3) | 0x80000000);
			rx_ini_power_L[1] = (u32)rx_ini_power_L[1] >> 3;
			break;
		case 5:
			rx_ini_power_L[0] = (u32)((rx_ini_power_L[0] >> 3) | 0xa0000000);
			rx_ini_power_L[1] = (u32)rx_ini_power_L[1] >> 3;
			break;
		case 6:
			rx_ini_power_L[0] = (u32)((rx_ini_power_L[0] >> 3) | 0xc0000000);
			rx_ini_power_L[1] = (u32)rx_ini_power_L[1] >> 3;
			break;
		case 7:
			rx_ini_power_L[0] = (u32)((rx_ini_power_L[0] >> 3) | 0xe0000000);
			rx_ini_power_L[1] = (u32)rx_ini_power_L[1] >> 3;
			break;
		default:
			break;
		}
	rximr = (u32)(3 * ((halrf_psd_log2base(rx_ini_power_L[0] / 100) - halrf_psd_log2base(rx_ini_power_L[1] / 100))) / 100);
#if 0
	/*
		RF_DBG(dm, DBG_RF_IQK, "%-20s: 0x%x, 0x%x, 0x%x, 0x%x,0x%x, tone_index=%x, rximr= %d\n",
		(path == 0) ? "PATH A RXIMR ": "PATH B RXIMR",
		rx_ini_power_H[0], rx_ini_power_L[0], rx_ini_power_H[1], rx_ini_power_L[1], tmp1bcc, tone_index, rximr);
*/
#endif
	return rximr;
}

void _iqk_start_imr_test_8821c(void *dm_void, u8 path)
{
	u8 imr_limit, i, tone_index;
	u32 tmp;
	boolean KFAIL;
	u32 rxk1_tmp1b38[2][15];
	u32 imr_result[2];

	struct dm_struct *dm = (struct dm_struct *)dm_void;

	/*TX IMR*/
	if (*dm->band_width == 2)
		imr_limit = 0xe;
	else if (*dm->band_width == 1)
		imr_limit = 0x7;
	else
		imr_limit = 0x3;

	_iqk_txk_setting_8821c(dm, RF_PATH_A);
	KFAIL = _iqk_one_shot_8821c(dm, RF_PATH_A, TXIQK);
	for (i = 0x0; i <= imr_limit; i++) {
		tone_index = (u8)(0x08 | i << 4);
		imr_result[RF_PATH_A] = _iqk_tximr_selfcheck_8821c(dm, tone_index, RF_PATH_A);
		RF_DBG(dm, DBG_RF_IQK, "[IQK]toneindex = %x, TXIMR = %d\n",
		       tone_index, imr_result[RF_PATH_A]);
	}
	RF_DBG(dm, DBG_RF_IQK, "\n");
	/*RX IMR*/
	/*get the rxk1 tone index 0x1b38 setting*/
	_iqk_rxk1setting_8821c(dm, path);
	tmp = odm_read_4byte(dm, 0x1b1c);
	for (path = 0; path < SS_8821C; path++) {
		for (i = 0; i <= imr_limit; i++) {
			tone_index = (u8)(0x08 | i << 4);
			KFAIL = _iqk_rx_iqk_gain_search_fail_by_toneindex_8821c(dm, path, RXIQK1, tone_index);
			if (!KFAIL) {
				odm_write_4byte(dm, 0x1b1c, 0xa2193c32);
				odm_write_4byte(dm, 0x1b14, 0xe5);
				odm_write_4byte(dm, 0x1b14, 0x0);
				rxk1_tmp1b38[path][i] = odm_read_4byte(dm, 0x1b38);
			} else {
				rxk1_tmp1b38[path][i] = 0x0;
			}
		}
	}
	_iqk_rxk2setting_8821c(dm, path, true);
	for (path = 0; path < SS_8821C; path++) {
		for (i = 0x0; i <= imr_limit; i++) {
			tone_index = (u8)(0x08 | i << 4);
			imr_result[RF_PATH_A] = _iqk_rximr_selfcheck_8821c(dm, tone_index, RF_PATH_A, rxk1_tmp1b38[path][i]);
			RF_DBG(dm, DBG_RF_IQK,
			       "[IQK]toneindex = %x, RXIMR = %d\n", tone_index,
			       imr_result[RF_PATH_A]);
		}
	}
	odm_write_4byte(dm, 0x1b1c, tmp);
	odm_write_4byte(dm, 0x1b38, 0x20000000);
}

void _phy_iq_calibrate_8821c(struct dm_struct *dm, boolean reset,
			     boolean segment_iqk, boolean do_imr_test)
{
	u32 MAC_backup[MAC_REG_NUM_8821C], BB_backup[BB_REG_NUM_8821C];
	u32 RF_backup[RF_REG_NUM_8821C][1];
	u32 backup_mac_reg[MAC_REG_NUM_8821C] = {0x520, 0x550, 0x1518};
	u32 backup_bb_reg[BB_REG_NUM_8821C] = {0x808, 0x90c, 0xc00, 0xcb0,
						0xcb4, 0xcbc, 0x1990,
						0x9a4, 0xa04, 0x838};
	u32 backup_rf_reg[RF_REG_NUM_8821C] = {0xdf, 0xde, 0x8f, 0x0, 0x1};
	boolean is_mp = false;
	int i = 0;

	struct dm_iqk_info *iqk_info = &dm->IQK_info;

	if (*dm->mp_mode)
		is_mp = true;
	else if (dm->is_linked)
		segment_iqk = false;
	iqk_info->is_btg = (boolean)odm_get_bb_reg(dm, R_0xcb8, BIT(16));

	if (!is_mp)
		if (_iqk_reload_iqk_8821c(dm, reset))
			return;
	if (!do_imr_test)
		RF_DBG(dm, DBG_RF_IQK,
		       "[IQK]==========IQK strat!!!!!==========\n");
	RF_DBG(dm, DBG_RF_IQK,
	       "[IQK]band_type = %s, band_width = %d, ExtPA2G = %d, ext_pa_5g = %d\n",
	       (*dm->band_type == ODM_BAND_5G) ? "5G" : "2G", *dm->band_width,
	       dm->ext_pa, dm->ext_pa_5g);
	RF_DBG(dm, DBG_RF_IQK, "[IQK]Interface = %d, Cv = %x\n",
	       dm->support_interface, dm->cut_version);

	iqk_info->tmp_gntwl = _iqk_indirect_read_reg(dm, 0x38);
	iqk_info->iqk_times++;
	iqk_info->kcount = 0;
	dm->rf_calibrate_info.iqk_total_progressing_time = 0;
	dm->rf_calibrate_info.iqk_step = 1;
	iqk_info->rxiqk_step = 1;
	iqk_info->is_reload = false;

	_iqk_backup_iqk_8821c(dm, 0x0, 0x0);
	_iqk_backup_mac_bb_8821c(dm, MAC_backup, BB_backup, backup_mac_reg, backup_bb_reg, BB_REG_NUM_8821C);
	_iqk_backup_rf_8821c(dm, RF_backup, backup_rf_reg);

	while (1) {
		if (!is_mp)
			dm->rf_calibrate_info.iqk_start_time = odm_get_current_time(dm);

		_iqk_configure_macbb_8821c(dm);
		_iqk_afe_setting_8821c(dm, true);
		_iqk_rfe_setting_8821c(dm, false);
		_iqk_agc_bnd_int_8821c(dm);
		_iqk_rfsetting_8821c(dm);
		if (do_imr_test) {
			_iqk_start_imr_test_8821c(dm, 0x0);
			dm->rf_calibrate_info.iqk_step = 4;
		} else {
			_iqk_start_iqk_8821c(dm, segment_iqk);
		}
		_iqk_afe_setting_8821c(dm, false);
		_iqk_restore_mac_bb_8821c(dm, MAC_backup, BB_backup, backup_mac_reg, backup_bb_reg, BB_REG_NUM_8821C);
		_iqk_restore_rf_8821c(dm, backup_rf_reg, RF_backup);

		if (!is_mp) {
			dm->rf_calibrate_info.iqk_progressing_time = odm_get_progressing_time(dm, dm->rf_calibrate_info.iqk_start_time);
			dm->rf_calibrate_info.iqk_total_progressing_time += odm_get_progressing_time(dm, dm->rf_calibrate_info.iqk_start_time);
			RF_DBG(dm, DBG_RF_IQK,
			       "[IQK]IQK progressing_time = %lld ms\n",
			       dm->rf_calibrate_info.iqk_progressing_time);
		}

		if (dm->rf_calibrate_info.iqk_step == 4)
			break;

		iqk_info->kcount = 0;
		RF_DBG(dm, DBG_RF_IQK, "[IQK]delay 50ms!!!\n");
#if 0
		/*ODM_sleep_ms(50);*/
#endif
		for (i = 0; i<1000; i++)
			ODM_delay_us(10);
	}

	if (!do_imr_test) {
		if (segment_iqk)
			_iqk_reload_iqk_setting_8821c(dm, 0x0, 0x1);
		_iqk_fill_iqk_report_8821c(dm, 0);
		if (!is_mp)
			RF_DBG(dm, DBG_RF_IQK,
			       "[IQK]Total IQK progressing_time = %lld ms\n",
			       dm->rf_calibrate_info.iqk_total_progressing_time)
			       ;
		RF_DBG(dm, DBG_RF_IQK,
		       "[IQK]==========IQK end!!!!!==========\n");
		RF_DBG(dm, DBG_RF_IQK, "[IQK]check 0x49c = %x\n",
		       odm_read_1byte(dm, 0x49c));
	}
}

void _phy_iq_calibrate_by_fw_8821c(void *dm_void, u8 clear, u8 segment_iqk)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	enum hal_status status = HAL_STATUS_FAILURE;

	if (*dm->mp_mode)
		clear = 0x1;
	else if (dm->is_linked)
		segment_iqk = 0x1;

	status = odm_iq_calibrate_by_fw(dm, clear, segment_iqk);

	if (status == HAL_STATUS_SUCCESS)
		RF_DBG(dm, DBG_RF_IQK, "[IQK]FWIQK  OK!!!\n");
	else
		RF_DBG(dm, DBG_RF_IQK, "[IQK]FWIQK fail!!!\n");
}

/*********debug message start**************/

void _phy_iqk_XYM_Read_Using_0x1b38_8821c(struct dm_struct *dm, u8 path)
{
	u32 tmp = 0x0;
	u32 tmp2;
	u8 i;

	tmp = odm_read_4byte(dm, 0x1b1c);
	odm_write_4byte(dm, 0x1b1c, 0xA2193C32);
	RF_DBG(dm, DBG_RF_IQK, "\n");
	for (i = 0; i < 0xa; i++) {
		odm_write_4byte(dm, 0x1b14, 0xe6 + i);
		odm_write_4byte(dm, 0x1b14, 0x0);
		tmp2 = odm_read_4byte(dm, 0x1b38);
		RF_DBG(dm, DBG_RF_IQK, "%x\n", tmp2);
	}
	odm_write_4byte(dm, 0x1b1c, tmp);
	RF_DBG(dm, DBG_RF_IQK, "\n");
	odm_write_4byte(dm, 0x1b38, 0x20000000);
}

void _phy_iqk_debug_inner_lpbk_psd_8821c(struct dm_struct *dm, u8 path)
{
	s16 tx_x;
	s16 tx_y;
	u32 temp = 0x0;
	u32 psd_pwr = 0x0;
	u32 tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp8, tmp9, tmp10;

	tmp1 = odm_read_4byte(dm, 0x1b20);
	tmp2 = odm_read_4byte(dm, 0x1b24);
	tmp3 = odm_read_4byte(dm, 0x1b15);
	tmp4 = odm_read_4byte(dm, 0x1b18);
	tmp5 = odm_read_4byte(dm, 0x1b1c);
	tmp6 = odm_read_4byte(dm, 0x1b28);
	tmp7 = odm_read_4byte(dm, 0x1b90);
	tmp8 = odm_read_4byte(dm, 0x1bcc);
	tmp9 = odm_read_4byte(dm, 0x1b2c);
	tmp10 = odm_read_4byte(dm, 0x1b30);
	odm_write_4byte(dm, 0x1b20, 0x03840008);
	odm_write_4byte(dm, 0x1b24, 0x03860008);
	odm_write_1byte(dm, 0x1b15, 0x00);
	odm_write_4byte(dm, 0x1b18, 0x00010101);
	odm_write_4byte(dm, 0x1b1c, 0x02effcb2);
	odm_write_4byte(dm, 0x1b28, 0x00060c00);
	odm_write_4byte(dm, 0x1b90, 0x00080003);
	odm_write_4byte(dm, 0x1bcc, 0x00000009);
	odm_write_4byte(dm, 0x1b2c, 0x20000003);
	odm_write_4byte(dm, 0x1b30, 0x20000000);
	RF_DBG(dm, DBG_RF_IQK, "\n");
	for (tx_x = 507; tx_x <= 532; tx_x++) {
		for (tx_y = 0; tx_y <= 10 + 20; tx_y++) {
			if (tx_y < 0)
				temp = (tx_x << 20) | (tx_y + 2048) << 8;
			else
				temp = (tx_x << 20) | (tx_y << 8);
			odm_write_4byte(dm, 0x1b38, temp);
			odm_write_4byte(dm, 0x1b3c, 0x20000000);
			odm_write_4byte(dm, 0x1b34, 0x00000801);
			odm_write_4byte(dm, 0x1b34, 0x00000800);
			ODM_delay_us(10);
			/*PSD_bef_K*/
			odm_write_4byte(dm, 0x1bd4, 0x000c0001);
			psd_pwr = odm_read_4byte(dm, 0x1bfc);
			RF_DBG(dm, DBG_RF_IQK, "%d ", psd_pwr);
		}
	}
	RF_DBG(dm, DBG_RF_IQK, "\n");
	odm_write_4byte(dm, 0x1b20, tmp1);
	odm_write_4byte(dm, 0x1b24, tmp2);
	odm_write_4byte(dm, 0x1b15, tmp3);
	odm_write_4byte(dm, 0x1b18, tmp4);
	odm_write_4byte(dm, 0x1b1c, tmp5);
	odm_write_4byte(dm, 0x1b28, tmp6);
	odm_write_4byte(dm, 0x1b90, tmp7);
	odm_write_4byte(dm, 0x1bcc, tmp8);
	odm_write_4byte(dm, 0x1b2c, tmp9);
	odm_write_4byte(dm, 0x1b30, tmp10);
	odm_write_4byte(dm, 0x1b38, 0x20000000);
}

void _iqk_readsram_8821c(struct dm_struct *dm, u8 path)
{
	u32 tmp1bd4, tmp1bd8, tmp;
	u8 i;

	tmp1bd4 = odm_read_4byte(dm, 0x1bd4);
	tmp1bd8 = odm_read_4byte(dm, 0x1bd8);
	odm_write_4byte(dm, 0x1bd4, 0x00010001);
	for (i = 0; i < 0x80; i++) {
		odm_write_4byte(dm, 0x1bd8, 0xa0000101 + (u32)(i << 1));
		tmp = (u32)odm_read_4byte(dm, 0x1bfc) & 0x3ff;
		if (i < 0x40)
			RF_DBG(dm, DBG_RF_IQK, "adc_i[%d] = %x\n", i, tmp);
		else
			RF_DBG(dm, DBG_RF_IQK, "adc_q[%d] = %x\n", i, tmp);
	}
}

void do_imr_test_8821c(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	RF_DBG(dm, DBG_RF_IQK,
	       "[IQK]  ************IMR Test *****************\n");
	_phy_iq_calibrate_8821c(dm, false, false, true);
	RF_DBG(dm, DBG_RF_IQK,
	       "[IQK]  **********End IMR Test *******************\n");
}

/*********debug message end***************/

/*IQK version:0x23, NCTL:0x8*/
/*1. modify the iqk counters for coex.*/

void phy_iq_calibrate_8821c(void *dm_void, boolean clear, boolean segment_iqk)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _hal_rf_ *rf = &dm->rf_table;

	if (!(*dm->mp_mode))
		_iqk_check_coex_status(dm, true);

	RF_DBG(dm, DBG_RF_IQK, "[IQK]fw_ver= 0x%x\n", rf->fw_ver);
	if (*dm->mp_mode)
		halrf_iqk_hwtx_check(dm, true);
	/*FW IQK*/
	if (dm->fw_offload_ability & PHYDM_RF_IQK_OFFLOAD) {
		_phy_iq_calibrate_by_fw_8821c(dm, clear, segment_iqk);
		phydm_get_read_counter_8821c(dm);
		RF_DBG(dm, DBG_RF_IQK, "[IQK]0x38= 0x%x\n",
		       _iqk_indirect_read_reg(dm, 0x38));
	} else {
		_iq_calibrate_8821c_init(dm);
		_phy_iq_calibrate_8821c(dm, clear, segment_iqk, false);
	}
	_iqk_fail_count_8821c(dm);

	if (*dm->mp_mode)
		halrf_iqk_hwtx_check(dm, false);
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	_iqk_iqk_fail_report_8821c(dm);
#endif
	halrf_iqk_dbg(dm);

	if (!(*dm->mp_mode))
		_iqk_check_coex_status(dm, false);
	RF_DBG(dm, DBG_RF_IQK, "[IQK]final 0x49c = %x\n",
	       odm_read_1byte(dm, 0x49c));
}

void phy_dp_calibrate_8821c(void *dm_void, boolean clear)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _hal_rf_ *rf = &dm->rf_table;

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	if (odm_check_power_status(dm) == false)
		return;
#endif

#if (MP_DRIVER)
	if ((dm->mp_mode != NULL) && (rf->is_con_tx != NULL) && (rf->is_single_tone != NULL) && (rf->is_carrier_suppresion != NULL))
		if (*(dm->mp_mode) && ((*(rf->is_con_tx) || *(rf->is_single_tone) || *(rf->is_carrier_suppresion))))
			return;
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	if (!(rf->rf_supportability & HAL_RF_DPK))
		return;
#endif

#if DISABLE_BB_RF
	return;
#endif

	RF_DBG(dm, DBG_RF_IQK, "[DPK] In PHY, dm->dpk_en == %x\n", rf->dpk_en);

	/*if dpk is not enable*/
	if (rf->dpk_en == 0x0)
		return;

	/*start*/
	if (!dm->rf_calibrate_info.is_iqk_in_progress) {
		odm_acquire_spin_lock(dm, RT_IQK_SPINLOCK);
		dm->rf_calibrate_info.is_iqk_in_progress = true;
		odm_release_spin_lock(dm, RT_IQK_SPINLOCK);
		if (*dm->mp_mode)
			dm->rf_calibrate_info.iqk_start_time = odm_get_current_time(dm);

		if (*dm->mp_mode) {
			/*do DPK*/
			_phy_dpd_calibrate_8821c(dm, clear);
			_dpk_toggle_rxagc(dm, clear);
		}

		if (*dm->mp_mode) {
			dm->rf_calibrate_info.iqk_progressing_time = odm_get_progressing_time(dm, dm->rf_calibrate_info.iqk_start_time);
			RF_DBG(dm, DBG_RF_IQK,
			       "[DPK]DPK progressing_time = %lld ms\n",
			       dm->rf_calibrate_info.iqk_progressing_time);
		}
		odm_acquire_spin_lock(dm, RT_IQK_SPINLOCK);
		dm->rf_calibrate_info.is_iqk_in_progress = false;
		odm_release_spin_lock(dm, RT_IQK_SPINLOCK);
	} else {
		RF_DBG(dm, DBG_RF_IQK,
		       "[DPK]== Return the DPK CMD, because the DPK in Progress ==\n");
	}
}
void phy_txtap_calibrate_8821c(void *dm_void, boolean clear)
{
	u32 MAC_backup[MAC_REG_NUM_8821C], BB_backup[BB_REG_NUM_8821C], RF_backup[RF_REG_NUM_8821C][1];
	u32 backup_mac_reg[MAC_REG_NUM_8821C] = {0x520, 0x550, 0x1518};
	u32 backup_bb_reg[BB_REG_NUM_8821C] = {0x808, 0x90c, 0xc00, 0xcb0, 0xcb4, 0xcbc, 0x1990, 0x9a4, 0xa04};
	u32 backup_rf_reg[RF_REG_NUM_8821C] = {0xdf, 0xde, 0x8f, 0x0, 0x1};

	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct dm_iqk_info *iqk_info = &dm->IQK_info;

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct _ADAPTER *adapter = dm->adapter;

#if (MP_DRIVER == 1)
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PMPT_CONTEXT p_mpt_ctx = &(adapter->MptCtx);
#else
#ifdef CONFIG_MP_INCLUDED
	PMPT_CONTEXT p_mpt_ctx = &(adapter->mppriv.mpt_ctx);
#endif
#endif
#endif

	struct _hal_rf_ *rf = &dm->rf_table;

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	if (!(rf->rf_supportability & HAL_RF_IQK))
		return;
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	if (odm_check_power_status(adapter) == false)
		return;
#endif

#if MP_DRIVER == 1
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	if (p_mpt_ctx->bSingleTone || p_mpt_ctx->bCarrierSuppression)
		return;
#else
#ifdef CONFIG_MP_INCLUDED
	if (p_mpt_ctx->is_single_tone || p_mpt_ctx->is_carrier_suppression)
		return;
#endif
#endif
#endif
#endif

	if (!(*dm->mp_mode))
		_iqk_check_coex_status(dm, true);

	if ((dm->rf_table.rf_supportability & HAL_RF_TXGAPK))
		if (iqk_info->lok_fail[RF_PATH_A] == 0 &&
		    iqk_info->iqk_fail_report[0][RF_PATH_A][TXIQK] == 0 &&
		    iqk_info->iqk_fail_report[0][RF_PATH_A][RXIQK] == 0) {
			_iqk_backup_iqk_8821c(dm, 0x0, 0x0);
			_iqk_backup_mac_bb_8821c(dm, MAC_backup, BB_backup, backup_mac_reg, backup_bb_reg, BB_REG_NUM_8821C);
			_iqk_backup_rf_8821c(dm, RF_backup, backup_rf_reg);

			_iqk_configure_macbb_8821c(dm);
			_iqk_afe_setting_8821c(dm, true);
			_iqk_rfe_setting_8821c(dm, false);
			_iqk_agc_bnd_int_8821c(dm);
			_iqk_rfsetting_8821c(dm);

			_phy_txgapk_calibrate_8821c(dm, RF_PATH_A);

			_iqk_afe_setting_8821c(dm, false);
			_iqk_restore_mac_bb_8821c(dm, MAC_backup, BB_backup, backup_mac_reg, backup_bb_reg, BB_REG_NUM_8821C);
			_iqk_restore_rf_8821c(dm, backup_rf_reg, RF_backup);
		}
}
void dpk_temperature_compensate_8821c(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _hal_rf_ *rf = &dm->rf_table;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	void *adapter = dm->adapter;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	u8 pgthermal = hal_data->eeprom_thermal_meter;
#else
	struct rtl8192cd_priv *priv = dm->priv;
	u8 pgthermal = (u8)priv->pmib->dot11RFEntry.ther;

#endif
	static u8 dpk_tm_trigger;
	u8 thermal_value = 0, delta_dpk, i = 0;
	u8 thermal_value_avg_count = 0;
	u8 thermal_value_avg_times = 2;
	u32 thermal_value_avg = 0;
	u8 tmp, abs_temperature;

	/*if dpk is not enable*/
	if (rf->dpk_en == 0x0)
		return;
	/*if ap mode, disable dpk*/
	if (DM_ODM_SUPPORT_TYPE & ODM_AP)
		return;
	if (!dpk_tm_trigger) {
		odm_set_rf_reg(dm, RF_PATH_A, RF_0x42, BIT(17) | BIT(16), 0x03);
#if 0
		/*RF_DBG(dm, DBG_RF_IQK, "[DPK] (1) Trigger Thermal Meter!!\n");*/
#endif
		dpk_tm_trigger = 1;
		return;
	}

	/* Initialize */
	dpk_tm_trigger = 0;
#if 0
	/*RF_DBG(dm, DBG_RF_IQK, "[DPK] (2) calculate the thermal !!\n");
	*/
#endif

	/* calculate average thermal meter */
	thermal_value = (u8)odm_get_rf_reg(dm, RF_PATH_A, RF_0x42, 0xfc00); /*0x42: RF Reg[15:10] 88E*/
	RF_DBG(dm, DBG_RF_IQK, "[DPK] (3) current Thermal Meter = %d\n",
	       thermal_value);

	dm->rf_calibrate_info.thermal_value_dpk = thermal_value;
	dm->rf_calibrate_info.thermal_value_avg[dm->rf_calibrate_info.thermal_value_avg_index] = thermal_value;
	dm->rf_calibrate_info.thermal_value_avg_index++;
	if (dm->rf_calibrate_info.thermal_value_avg_index == thermal_value_avg_times)
		dm->rf_calibrate_info.thermal_value_avg_index = 0;
	for (i = 0; i < thermal_value_avg_times; i++) {
		if (dm->rf_calibrate_info.thermal_value_avg[i]) {
			thermal_value_avg += dm->rf_calibrate_info.thermal_value_avg[i];
			thermal_value_avg_count++;
		}
	}
	if (thermal_value_avg_count) /*Calculate Average thermal_value after average enough times*/
		thermal_value = (u8)(thermal_value_avg / thermal_value_avg_count);
	/* compensate the DPK */
	delta_dpk = (thermal_value > pgthermal) ? (thermal_value - pgthermal) : (pgthermal - thermal_value);
	tmp = (u8)((dpk_result[0] & 0x00001f00) >> 8);
	RF_DBG(dm, DBG_RF_IQK,
	       "[DPK] (5)delta_dpk = %d, eeprom_thermal_meter = %d, tmp=%d\n",
	       delta_dpk, pgthermal, tmp);

	if (thermal_value > pgthermal) {
		abs_temperature = thermal_value - pgthermal;
		if (abs_temperature >= 20)
			tmp = tmp + 4;
		else if (abs_temperature >= 15)
			tmp = tmp + 3;
		else if (abs_temperature >= 10)
			tmp = tmp + 2;
		else if (abs_temperature >= 5)
			tmp = tmp + 1;
	} else { /*low temperature*/
		abs_temperature = pgthermal - thermal_value;
		if (abs_temperature >= 20)
			tmp = tmp - 4;
		else if (abs_temperature >= 15)
			tmp = tmp - 3;
		else if (abs_temperature >= 10)
			tmp = tmp - 2;
		else if (abs_temperature >= 5)
			tmp = tmp - 1;
	}

	odm_set_bb_reg(dm, R_0x1bd0, BIT(12) | BIT(11) | BIT(10) | BIT(9) | BIT(8), tmp);
	RF_DBG(dm, DBG_RF_IQK,
	       "[DPK] (6)delta_dpk = %d, eeprom_thermal_meter = %d, new tmp=%d, 0x1bd0=0x%x\n",
	       delta_dpk, pgthermal, tmp, odm_read_4byte(dm, 0x1bd0));
}

#endif
