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

#include "mp_precomp.h"
#include "phydm_precomp.h"

void phydm_ccx_hw_restart(void *dm_void)
			  /*@Will Restart NHM/CLM/FAHM simultaneously*/
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 reg1 = 0;

	if (dm->support_ic_type & ODM_IC_11AC_SERIES)
		reg1 = R_0x994;
	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	else if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		reg1 = R_0x1e60;
	#endif
	else
		reg1 = R_0x890;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);
	/*@disable NHM,CLM, FAHM*/
	odm_set_bb_reg(dm, reg1, 0x7, 0x0);
	odm_set_bb_reg(dm, reg1, BIT(8), 0x0);
	odm_set_bb_reg(dm, reg1, BIT(8), 0x1);
}

#ifdef FAHM_SUPPORT

u16 phydm_hw_divider(void *dm_void, u16 numerator, u16 denumerator)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u16 result = DEVIDER_ERROR;
	u32 tmp_u32 = ((numerator << 16) | denumerator);
	u32 reg_devider_input;
	u32 reg;
	u8 i;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		reg_devider_input = 0x1cbc;
		reg = 0x1f98;
	} else {
		reg_devider_input = 0x980;
		reg = 0x9f0;
	}

	odm_set_bb_reg(dm, reg_devider_input, MASKDWORD, tmp_u32);

	for (i = 0; i < 10; i++) {
		ODM_delay_ms(1);
		if (odm_get_bb_reg(dm, reg, BIT(24))) {
		/*@Chk HW rpt is ready*/

			result = (u16)odm_get_bb_reg(dm, reg, MASKBYTE2);
			break;
		}
	}
	return result;
}

void phydm_fahm_trigger(void *dm_void, u16 tgr_period)
{ /*@unit (4us)*/
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 fahm_reg1;

	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		odm_set_bb_reg(dm, R_0x1cf8, 0xffff00, tgr_period);

		fahm_reg1 = 0x994;
	} else {
		odm_set_bb_reg(dm, R_0x978, 0xff000000, (tgr_period & 0xff));
		odm_set_bb_reg(dm, R_0x97c, 0xff, (tgr_period & 0xff00) >> 8);

		fahm_reg1 = 0x890;
	}

	odm_set_bb_reg(dm, fahm_reg1, BIT(2), 0);
	odm_set_bb_reg(dm, fahm_reg1, BIT(2), 1);
}

void phydm_fahm_set_valid_cnt(void *dm_void, u8 numerator_sel,
			      u8 denominator_sel)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx_info = &dm->dm_ccx_info;
	u32 fahm_reg1;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	if (ccx_info->fahm_nume_sel == numerator_sel &&
	    ccx_info->fahm_denom_sel == denominator_sel) {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "no need to update\n");
		return;
	}

	ccx_info->fahm_nume_sel = numerator_sel;
	ccx_info->fahm_denom_sel = denominator_sel;

	if (dm->support_ic_type & ODM_IC_11AC_SERIES)
		fahm_reg1 = 0x994;
	else
		fahm_reg1 = 0x890;

	odm_set_bb_reg(dm, fahm_reg1, 0xe0, numerator_sel);
	odm_set_bb_reg(dm, fahm_reg1, 0x7000, denominator_sel);
}

void phydm_fahm_get_result(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u16 fahm_cnt[12]; /*packet count*/
	u16 fahm_rpt[12]; /*percentage*/
	u16 denominator; /*@fahm_denominator packet count*/
	u32 reg_rpt, reg_rpt_2;
	u32 reg_tmp;
	boolean is_ready = false;
	u8 i;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		reg_rpt = 0x1f80;
		reg_rpt_2 = 0x1f98;
	} else {
		reg_rpt = 0x9d8;
		reg_rpt_2 = 0x9f0;
	}

	for (i = 0; i < 3; i++) {
		if (odm_get_bb_reg(dm, reg_rpt_2, BIT(31))) {
		/*@Chk HW rpt is ready*/
			is_ready = true;
			break;
		}
		ODM_delay_ms(1);
	}

	if (!is_ready)
		return;

	/*@Get FAHM Denominator*/
	denominator = (u16)odm_get_bb_reg(dm, reg_rpt_2, MASKLWORD);

	PHYDM_DBG(dm, DBG_ENV_MNTR, "Reg[0x%x] fahm_denmrtr = %d\n", reg_rpt_2,
		  denominator);

	/*@Get FAHM nemerator*/
	for (i = 0; i < 6; i++) {
		reg_tmp = odm_get_bb_reg(dm, reg_rpt + (i << 2), MASKDWORD);

		PHYDM_DBG(dm, DBG_ENV_MNTR, "Reg[0x%x] fahm_denmrtr = %d\n",
			  reg_rpt + (i * 4), reg_tmp);

		fahm_cnt[i * 2] = (u16)(reg_tmp & MASKLWORD);
		fahm_cnt[i * 2 + 1] = (u16)((reg_tmp & MASKHWORD) >> 16);
	}

	for (i = 0; i < 12; i++)
		fahm_rpt[i] = phydm_hw_divider(dm, fahm_cnt[i], denominator);

	PHYDM_DBG(dm, DBG_ENV_MNTR,
		  "FAHM_RPT_cnt[10:0]=[%d, %d, %d, %d, %d(IGI), %d, %d, %d, %d, %d, %d, %d]\n",
		  fahm_cnt[11], fahm_cnt[10], fahm_cnt[9],
		  fahm_cnt[8], fahm_cnt[7], fahm_cnt[6],
		  fahm_cnt[5], fahm_cnt[4], fahm_cnt[3],
		  fahm_cnt[2], fahm_cnt[1], fahm_cnt[0]);

	PHYDM_DBG(dm, DBG_ENV_MNTR,
		  "FAHM_RPT[10:0]=[%d, %d, %d, %d, %d(IGI), %d, %d, %d, %d, %d, %d, %d]\n",
		  fahm_rpt[11], fahm_rpt[10], fahm_rpt[9], fahm_rpt[8],
		  fahm_rpt[7], fahm_rpt[6], fahm_rpt[5], fahm_rpt[4],
		  fahm_rpt[3], fahm_rpt[2], fahm_rpt[1], fahm_rpt[0]);
}

void phydm_fahm_set_th_by_igi(void *dm_void, u8 igi)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx_info = &dm->dm_ccx_info;
	u32 val = 0;
	u8 f_th[11]; /*@FAHM Threshold*/
	u8 rssi_th[11]; /*@in RSSI scale*/
	u8 th_gap = 2 * IGI_TO_NHM_TH_MULTIPLIER; /*unit is 0.5dB for FAHM*/
	u8 i;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	if (ccx_info->env_mntr_igi == igi) {
		PHYDM_DBG(dm, DBG_ENV_MNTR,
			  "No need to update FAHM_th, IGI=0x%x\n",
			  ccx_info->env_mntr_igi);
		return;
	}

	ccx_info->env_mntr_igi = igi; /*@bkp IGI*/

	if (igi >= CCA_CAP)
		f_th[0] = (igi - CCA_CAP) * IGI_TO_NHM_TH_MULTIPLIER;
	else
		f_th[0] = 0;

	rssi_th[0] = igi - 10 - CCA_CAP;

	for (i = 1; i <= 10; i++) {
		f_th[i] = f_th[0] + th_gap * i;
		rssi_th[i] = rssi_th[0] + (i << 1);
	}

	PHYDM_DBG(dm, DBG_ENV_MNTR,
		  "FAHM_RSSI_th[10:0]=[%d, %d, %d, (IGI)%d, %d, %d, %d, %d, %d, %d, %d]\n",
		  rssi_th[10], rssi_th[9], rssi_th[8], rssi_th[7], rssi_th[6],
		  rssi_th[5], rssi_th[4], rssi_th[3], rssi_th[2], rssi_th[1],
		  rssi_th[0]);

	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		val = BYTE_2_DWORD(0, f_th[2], f_th[1], f_th[0]);
		odm_set_bb_reg(dm, R_0x1c38, 0xffffff00, val);
		val = BYTE_2_DWORD(0, f_th[5], f_th[4], f_th[3]);
		odm_set_bb_reg(dm, R_0x1c78, 0xffffff00, val);
		val = BYTE_2_DWORD(0, 0, f_th[7], f_th[6]);
		odm_set_bb_reg(dm, R_0x1c7c, 0xffff0000, val);
		val = BYTE_2_DWORD(0, f_th[10], f_th[9], f_th[8]);
		odm_set_bb_reg(dm, R_0x1cb8, 0xffffff00, val);
	} else {
		val = BYTE_2_DWORD(f_th[3], f_th[2], f_th[1], f_th[0]);
		odm_set_bb_reg(dm, R_0x970, MASKDWORD, val);
		val = BYTE_2_DWORD(f_th[7], f_th[6], f_th[5], f_th[4]);
		odm_set_bb_reg(dm, R_0x974, MASKDWORD, val);
		val = BYTE_2_DWORD(0, f_th[10], f_th[9], f_th[8]);
		odm_set_bb_reg(dm, R_0x978, 0xffffff, val);
	}
}

void phydm_fahm_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx_info = &dm->dm_ccx_info;
	u32 fahm_reg1;
	u8 denumerator_sel = 0;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);
	PHYDM_DBG(dm, DBG_ENV_MNTR, "IGI=0x%x\n",
		  dm->dm_dig_table.cur_ig_value);

	if (dm->support_ic_type & ODM_IC_11AC_SERIES)
		fahm_reg1 = 0x994;
	else
		fahm_reg1 = 0x890;

	ccx_info->fahm_period = 65535;

	odm_set_bb_reg(dm, fahm_reg1, 0x6, 3); /*@FAHM HW block enable*/

	denumerator_sel = FAHM_INCLD_FA | FAHM_INCLD_CRC_OK | FAHM_INCLD_CRC_ER;
	phydm_fahm_set_valid_cnt(dm, FAHM_INCLD_FA, denumerator_sel);
	phydm_fahm_set_th_by_igi(dm, dm->dm_dig_table.cur_ig_value);
}

void phydm_fahm_dbg(void *dm_void, char input[][16], u32 *_used, char *output,
		    u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx_info = &dm->dm_ccx_info;
	char help[] = "-h";
	u32 var1[10] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 i;

	for (i = 0; i < 2; i++) {
		if (input[i + 1])
			PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
	}

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{1: trigger, 2:get result}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{3: MNTR mode sel} {1: driver, 2. FW}\n");
		return;
	} else if (var1[0] == 1) { /* Set & trigger CLM */

		phydm_fahm_set_th_by_igi(dm, dm->dm_dig_table.cur_ig_value);
		phydm_fahm_trigger(dm, ccx_info->fahm_period);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Monitor FAHM for %d * 4us\n", ccx_info->fahm_period);

	} else if (var1[0] == 2) { /* @Get CLM results */

		phydm_fahm_get_result(dm);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "FAHM_result=%d us\n", (ccx_info->clm_result << 2));

	} else {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Error\n");
	}

	*_used = used;
	*_out_len = out_len;
}

#endif /*@#ifdef FAHM_SUPPORT*/

#ifdef NHM_SUPPORT

void phydm_nhm_racing_release(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u32 value32 = 0;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);
	PHYDM_DBG(dm, DBG_ENV_MNTR, "lv:(%d)->(0)\n", ccx->nhm_set_lv);

	ccx->nhm_ongoing = false;
	ccx->nhm_set_lv = NHM_RELEASE;

	if (!(ccx->nhm_app == NHM_BACKGROUND || ccx->nhm_app == NHM_ACS)) {
		phydm_pause_func(dm, F00_DIG, PHYDM_RESUME,
				 PHYDM_PAUSE_LEVEL_1, 1, &value32);
	}

	ccx->nhm_app = NHM_BACKGROUND;
}

u8 phydm_nhm_racing_ctrl(void *dm_void, enum phydm_nhm_level nhm_lv)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u8 set_result = PHYDM_SET_SUCCESS;
	/*@acquire to control NHM API*/

	PHYDM_DBG(dm, DBG_ENV_MNTR, "nhm_ongoing=%d, lv:(%d)->(%d)\n",
		  ccx->nhm_ongoing, ccx->nhm_set_lv, nhm_lv);
	if (ccx->nhm_ongoing) {
		if (nhm_lv <= ccx->nhm_set_lv) {
			set_result = PHYDM_SET_FAIL;
		} else {
			phydm_ccx_hw_restart(dm);
			ccx->nhm_ongoing = false;
		}
	}

	if (set_result)
		ccx->nhm_set_lv = nhm_lv;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "nhm racing success=%d\n", set_result);
	return set_result;
}

void phydm_nhm_trigger(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u32 nhm_reg1 = 0;

	if (dm->support_ic_type & ODM_IC_11AC_SERIES)
		nhm_reg1 = R_0x994;
	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	else if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		nhm_reg1 = R_0x1e60;
	#endif
	else
		nhm_reg1 = R_0x890;
	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	/* @Trigger NHM*/
	pdm_set_reg(dm, nhm_reg1, BIT(1), 0);
	pdm_set_reg(dm, nhm_reg1, BIT(1), 1);
	ccx->nhm_trigger_time = dm->phydm_sys_up_time;
	ccx->nhm_rpt_stamp++;
	ccx->nhm_ongoing = true;
}

boolean
phydm_nhm_check_rdy(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	boolean is_ready = false;
	u32 reg1 = 0, reg1_bit = 0;

	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		reg1 = R_0xfb4;
		reg1_bit = 16;
	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	} else if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		reg1 = R_0x2d4c;
		reg1_bit = 16;
	#endif
	} else {
		reg1 = R_0x8b4;
		if (dm->support_ic_type & (ODM_RTL8710B | ODM_RTL8721D |
					ODM_RTL8710C))
			reg1_bit = 25;
		else
			reg1_bit = 17;
	}
	if (odm_get_bb_reg(dm, reg1, BIT(reg1_bit)))
		is_ready = true;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "NHM rdy=%d\n", is_ready);

	return is_ready;
}

u8 phydm_nhm_cal_noise(void *dm_void, u8 start_i, u8 end_i, u8 n_sum)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u8 i = 0;
	u32 noise_tmp = 0;
	u8 noise = 0;
	u32 nhm_valid = 0;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	if (n_sum == 0) {
		PHYDM_DBG(dm, DBG_ENV_MNTR,
			  "n_sum = 0, don't need to update noise\n");
		return 0x0;
	} else if (end_i > NHM_RPT_NUM - 1) {
		PHYDM_DBG(dm, DBG_ENV_MNTR,
			  "[WARNING]end_i is larger than 11!!\n");
		return 0x0;
	}

	for (i = start_i; i <= end_i; i++) {
		if (i == 0)
			noise_tmp += ccx->nhm_result[0] *
				     MAX_2(ccx->nhm_th[0] - 2, 0);
		else if (i == (NHM_RPT_NUM - 1))
			noise_tmp += ccx->nhm_result[NHM_RPT_NUM - 1] *
				     (ccx->nhm_th[NHM_TH_NUM - 1] + 2);
		else
			noise_tmp += ccx->nhm_result[i] *
				     (ccx->nhm_th[i - 1] + ccx->nhm_th[i]) >> 1;
	}

	/* protection for the case of minus noise(RSSI)*/
	noise = (u8)(NTH_TH_2_RSSI(MAX_2(PHYDM_DIV(noise_tmp, n_sum), 20)));
	nhm_valid = (n_sum * 100) >> 8;
	PHYDM_DBG(dm, DBG_ENV_MNTR,
		  "valid: ((%d)) percent, noise(RSSI)=((%d))\n",
		  nhm_valid, noise);

	return noise;
}

void phydm_nhm_get_utility(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u8 nhm_rpt_non_0 = 0;
	u8 nhm_rpt_non_11 = 0;

	if (ccx->nhm_rpt_sum >= ccx->nhm_result[0]) {
		nhm_rpt_non_0 = ccx->nhm_rpt_sum - ccx->nhm_result[0];
		nhm_rpt_non_11 = ccx->nhm_rpt_sum - ccx->nhm_result[11];
		ccx->nhm_ratio = (nhm_rpt_non_0 * 100) >> 8;
		ccx->nhm_level_valid = (nhm_rpt_non_11 * 100) >> 8;
		ccx->nhm_level = phydm_nhm_cal_noise(dm, 0, NHM_RPT_NUM - 2,
						     nhm_rpt_non_11);
	} else {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "[warning] nhm_rpt_sum invalid\n");
		ccx->nhm_ratio = 0;
	}

	PHYDM_DBG(dm, DBG_ENV_MNTR, "nhm_ratio=%d\n", ccx->nhm_ratio);
}

boolean
phydm_nhm_get_result(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u32 value32 = 0;
	u8 i = 0;
	u32 nhm_reg1 = 0;
	u16 nhm_rpt_sum_tmp = 0;

	if (dm->support_ic_type & ODM_IC_11AC_SERIES)
		nhm_reg1 = R_0x994;
	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	else if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		nhm_reg1 = R_0x1e60;
	#endif
	else
		nhm_reg1 = R_0x890;
	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	if (!(dm->support_ic_type & (ODM_RTL8822C | ODM_RTL8812F |
				     ODM_RTL8197G)))
		pdm_set_reg(dm, nhm_reg1, BIT(1), 0);

	if (!(phydm_nhm_check_rdy(dm))) {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "Get NHM report Fail\n");
		phydm_nhm_racing_release(dm);
		return false;
	}

	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		value32 = odm_read_4byte(dm, R_0xfa8);
		odm_move_memory(dm, &ccx->nhm_result[0], &value32, 4);

		value32 = odm_read_4byte(dm, R_0xfac);
		odm_move_memory(dm, &ccx->nhm_result[4], &value32, 4);

		value32 = odm_read_4byte(dm, R_0xfb0);
		odm_move_memory(dm, &ccx->nhm_result[8], &value32, 4);

		/*@Get NHM duration*/
		value32 = odm_read_4byte(dm, R_0xfb4);
		ccx->nhm_duration = (u16)(value32 & MASKLWORD);
	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	} else if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		value32 = odm_read_4byte(dm, R_0x2d40);
		odm_move_memory(dm, &ccx->nhm_result[0], &value32, 4);

		value32 = odm_read_4byte(dm, R_0x2d44);
		odm_move_memory(dm, &ccx->nhm_result[4], &value32, 4);

		value32 = odm_read_4byte(dm, R_0x2d48);
		odm_move_memory(dm, &ccx->nhm_result[8], &value32, 4);

		/*@Get NHM duration*/
		value32 = odm_read_4byte(dm, R_0x2d4c);
		ccx->nhm_duration = (u16)(value32 & MASKLWORD);
	#endif
	} else {
		value32 = odm_read_4byte(dm, R_0x8d8);
		odm_move_memory(dm, &ccx->nhm_result[0], &value32, 4);

		value32 = odm_read_4byte(dm, R_0x8dc);
		odm_move_memory(dm, &ccx->nhm_result[4], &value32, 4);

		value32 = odm_get_bb_reg(dm, R_0x8d0, 0xffff0000);
		odm_move_memory(dm, &ccx->nhm_result[8], &value32, 2);

		value32 = odm_read_4byte(dm, R_0x8d4);

		ccx->nhm_result[10] = (u8)((value32 & MASKBYTE2) >> 16);
		ccx->nhm_result[11] = (u8)((value32 & MASKBYTE3) >> 24);

		/*@Get NHM duration*/
		ccx->nhm_duration = (u16)(value32 & MASKLWORD);
	}

	/* sum all nhm_result */
	if (ccx->nhm_period >= 65530) {
		value32 = (ccx->nhm_duration * 100) >> 16;
		PHYDM_DBG(dm, DBG_ENV_MNTR,
			  "NHM valid time = %d, valid: %d percent\n",
			  ccx->nhm_duration, value32);
	}

	for (i = 0; i < NHM_RPT_NUM; i++)
		nhm_rpt_sum_tmp += (u16)ccx->nhm_result[i];

	ccx->nhm_rpt_sum = (u8)nhm_rpt_sum_tmp;

	PHYDM_DBG(dm, DBG_ENV_MNTR,
		  "NHM_Rpt[%d](H->L)[%d %d %d %d %d %d %d %d %d %d %d %d]\n",
		  ccx->nhm_rpt_stamp, ccx->nhm_result[11], ccx->nhm_result[10],
		  ccx->nhm_result[9], ccx->nhm_result[8], ccx->nhm_result[7],
		  ccx->nhm_result[6], ccx->nhm_result[5], ccx->nhm_result[4],
		  ccx->nhm_result[3], ccx->nhm_result[2], ccx->nhm_result[1],
		  ccx->nhm_result[0]);

	phydm_nhm_racing_release(dm);

	if (nhm_rpt_sum_tmp > 255) {
		PHYDM_DBG(dm, DBG_ENV_MNTR,
			  "[Warning] Invalid NHM RPT, total=%d\n",
			  nhm_rpt_sum_tmp);
		return false;
	}

	return true;
}

void phydm_nhm_set_th_reg(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u32 reg1 = 0, reg2 = 0, reg3 = 0, reg4 = 0, reg4_bit = 0;
	u32 val = 0;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		reg1 = R_0x994;
		reg2 = R_0x998;
		reg3 = R_0x99c;
		reg4 = R_0x9a0;
		reg4_bit = MASKBYTE0;
	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	} else if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		reg1 = R_0x1e60;
		reg2 = R_0x1e44;
		reg3 = R_0x1e48;
		reg4 = R_0x1e5c;
		reg4_bit = MASKBYTE2;
	#endif
	} else {
		reg1 = R_0x890;
		reg2 = R_0x898;
		reg3 = R_0x89c;
		reg4 = R_0xe28;
		reg4_bit = MASKBYTE0;
	}

	/*Set NHM threshold*/ /*Unit: PWdB U(8,1)*/
	val = BYTE_2_DWORD(ccx->nhm_th[3], ccx->nhm_th[2],
			   ccx->nhm_th[1], ccx->nhm_th[0]);
	pdm_set_reg(dm, reg2, MASKDWORD, val);
	val = BYTE_2_DWORD(ccx->nhm_th[7], ccx->nhm_th[6],
			   ccx->nhm_th[5], ccx->nhm_th[4]);
	pdm_set_reg(dm, reg3, MASKDWORD, val);
	pdm_set_reg(dm, reg4, reg4_bit, ccx->nhm_th[8]);
	val = BYTE_2_DWORD(0, 0, ccx->nhm_th[10], ccx->nhm_th[9]);
	pdm_set_reg(dm, reg1, 0xffff0000, val);

	PHYDM_DBG(dm, DBG_ENV_MNTR,
		  "Update NHM_th[H->L]=[%d %d %d %d %d %d %d %d %d %d %d]\n",
		  ccx->nhm_th[10], ccx->nhm_th[9], ccx->nhm_th[8],
		  ccx->nhm_th[7], ccx->nhm_th[6], ccx->nhm_th[5],
		  ccx->nhm_th[4], ccx->nhm_th[3], ccx->nhm_th[2],
		  ccx->nhm_th[1], ccx->nhm_th[0]);
}

boolean
phydm_nhm_th_update_chk(void *dm_void, enum nhm_application nhm_app, u8 *nhm_th,
			u32 *igi_new, boolean en_1db_mode, u8 nhm_th0_manual)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	boolean is_update = false;
	u8 igi_curr = phydm_get_igi(dm, BB_PATH_A);
	u8 nhm_igi_th_11k_low[NHM_TH_NUM] = {0x12, 0x15, 0x18, 0x1b, 0x1e,
					     0x23, 0x28, 0x2c, 0x78,
					     0x78, 0x78};
	u8 nhm_igi_th_11k_high[NHM_TH_NUM] = {0x1e, 0x23, 0x28, 0x2d, 0x32,
					      0x37, 0x78, 0x78, 0x78, 0x78,
					      0x78};
	u8 nhm_igi_th_xbox[NHM_TH_NUM] = {0x1a, 0x2c, 0x2e, 0x30, 0x32, 0x34,
					  0x36, 0x38, 0x3a, 0x3c, 0x3d};
	u8 i = 0;
	u8 th_tmp = igi_curr - CCA_CAP;
	u8 th_step = 2;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);
	PHYDM_DBG(dm, DBG_ENV_MNTR, "App=%d, nhm_igi=0x%x, igi_curr=0x%x\n",
		  nhm_app, ccx->nhm_igi, igi_curr);

	if (igi_curr < 0x10) /* Protect for invalid IGI*/
		return false;

	switch (nhm_app) {
	case NHM_BACKGROUND: /* @Get IGI form driver parameter(cur_ig_value)*/
		if (ccx->nhm_igi != igi_curr || ccx->nhm_app != nhm_app) {
			is_update = true;
			*igi_new = (u32)igi_curr;

			#ifdef NHM_DYM_PW_TH_SUPPORT
			if (ccx->nhm_dym_pw_th_en) {
				th_tmp = MAX_2(igi_curr - DYM_PWTH_CCA_CAP, 0);
				th_step = 3;
			}
			#endif

			nhm_th[0] = (u8)IGI_2_NHM_TH(th_tmp);

			for (i = 1; i <= 10; i++)
				nhm_th[i] = nhm_th[0] +
					    IGI_2_NHM_TH(th_step * i);

		}
		break;

	case NHM_ACS:
		if (ccx->nhm_igi != igi_curr || ccx->nhm_app != nhm_app) {
			is_update = true;
			*igi_new = (u32)igi_curr;
			nhm_th[0] = (u8)IGI_2_NHM_TH(igi_curr - CCA_CAP);
			for (i = 1; i <= 10; i++)
				nhm_th[i] = nhm_th[0] + IGI_2_NHM_TH(2 * i);
		}
		break;

	case IEEE_11K_HIGH:
		is_update = true;
		*igi_new = 0x2c;
		for (i = 0; i < NHM_TH_NUM; i++)
			nhm_th[i] = IGI_2_NHM_TH(nhm_igi_th_11k_high[i]);
		break;

	case IEEE_11K_LOW:
		is_update = true;
		*igi_new = 0x20;
		for (i = 0; i < NHM_TH_NUM; i++)
			nhm_th[i] = IGI_2_NHM_TH(nhm_igi_th_11k_low[i]);
		break;

	case INTEL_XBOX:
		is_update = true;
		*igi_new = 0x36;
		for (i = 0; i < NHM_TH_NUM; i++)
			nhm_th[i] = IGI_2_NHM_TH(nhm_igi_th_xbox[i]);
		break;

	case NHM_DBG: /*@Get IGI form register*/
		igi_curr = phydm_get_igi(dm, BB_PATH_A);
		if (ccx->nhm_igi != igi_curr || ccx->nhm_app != nhm_app) {
			is_update = true;
			*igi_new = (u32)igi_curr;
			if (en_1db_mode) {
				nhm_th[0] = (u8)IGI_2_NHM_TH(nhm_th0_manual +
							     10);
				th_step = 1;
			} else {
				nhm_th[0] = (u8)IGI_2_NHM_TH(igi_curr -
							     CCA_CAP);
			}

			for (i = 1; i <= 10; i++)
				nhm_th[i] = nhm_th[0] + IGI_2_NHM_TH(th_step *
					    i);
		}
		break;
	}

	if (is_update) {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "[Update NHM_TH] igi_RSSI=%d\n",
			  IGI_2_RSSI(*igi_new));

		for (i = 0; i < NHM_TH_NUM; i++) {
			PHYDM_DBG(dm, DBG_ENV_MNTR, "NHM_th[%d](RSSI) = %d\n",
				  i, NTH_TH_2_RSSI(nhm_th[i]));
		}
	} else {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "No need to update NHM_TH\n");
	}
	return is_update;
}

void phydm_nhm_set(void *dm_void, enum nhm_option_txon_all include_tx,
		   enum nhm_option_cca_all include_cca,
		   enum nhm_divider_opt_all divi_opt,
		   enum nhm_application nhm_app, u16 period,
		   boolean en_1db_mode, u8 nhm_th0_manual)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u8 nhm_th[NHM_TH_NUM] = {0};
	u32 igi = 0x20;
	u32 reg1 = 0, reg2 = 0;
	u32 val_tmp = 0;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	PHYDM_DBG(dm, DBG_ENV_MNTR,
		  "incld{tx, cca}={%d, %d}, divi_opt=%d, period=%d\n",
		  include_tx, include_cca, divi_opt, period);

	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		reg1 = R_0x994;
		reg2 = R_0x990;
	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	} else if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		reg1 = R_0x1e60;
		reg2 = R_0x1e40;
	#endif
	} else {
		reg1 = R_0x890;
		reg2 = R_0x894;
	}

	/*Set disable_ignore_cca, disable_ignore_txon, ccx_en*/
	if (include_tx != ccx->nhm_include_txon ||
	    include_cca != ccx->nhm_include_cca ||
	    divi_opt != ccx->nhm_divider_opt) {
	    /* some old ic is not supported on NHM divider option */
		if (dm->support_ic_type & (ODM_RTL8188E | ODM_RTL8723B |
		    ODM_RTL8195A | ODM_RTL8192E)) {
			val_tmp = (u32)((include_tx << 2) |
				  (include_cca << 1) | 1);
			pdm_set_reg(dm, reg1, 0x700, val_tmp);
		} else {
			val_tmp = (u32)BIT_2_BYTE(divi_opt, include_tx,
				  include_cca, 1);
			pdm_set_reg(dm, reg1, 0xf00, val_tmp);
		}
		ccx->nhm_include_txon = include_tx;
		ccx->nhm_include_cca = include_cca;
		ccx->nhm_divider_opt = divi_opt;
	}

	/*Set NHM period*/
	if (period != ccx->nhm_period) {
		pdm_set_reg(dm, reg2, MASKHWORD, period);
		PHYDM_DBG(dm, DBG_ENV_MNTR,
			  "Update NHM period ((%d)) -> ((%d))\n",
			  ccx->nhm_period, period);

		ccx->nhm_period = period;
	}

	/*Set NHM threshold*/
	if (phydm_nhm_th_update_chk(dm, nhm_app, &nhm_th[0], &igi,
				    en_1db_mode, nhm_th0_manual)) {
		/*Pause IGI*/
		if (nhm_app == NHM_BACKGROUND || nhm_app == NHM_ACS) {
			PHYDM_DBG(dm, DBG_ENV_MNTR, "DIG Free Run\n");
		} else if (phydm_pause_func(dm, F00_DIG, PHYDM_PAUSE,
					    PHYDM_PAUSE_LEVEL_1, 1, &igi)
					    == PAUSE_FAIL) {
			PHYDM_DBG(dm, DBG_ENV_MNTR, "pause DIG Fail\n");
			return;
		} else {
			PHYDM_DBG(dm, DBG_ENV_MNTR, "pause DIG=0x%x\n", igi);
		}
		ccx->nhm_app = nhm_app;
		ccx->nhm_igi = (u8)igi;
		odm_move_memory(dm, &ccx->nhm_th[0], &nhm_th, NHM_TH_NUM);

		/*Set NHM th*/
		phydm_nhm_set_th_reg(dm);
	}
}

u8 phydm_nhm_mntr_set(void *dm_void, struct nhm_para_info *nhm_para)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u16 nhm_time = 0; /*unit: 4us*/

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	if (nhm_para->mntr_time == 0)
		return PHYDM_SET_FAIL;

	if (nhm_para->nhm_lv >= NHM_MAX_NUM) {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "Wrong LV=%d\n", nhm_para->nhm_lv);
		return PHYDM_SET_FAIL;
	}

	if (phydm_nhm_racing_ctrl(dm, nhm_para->nhm_lv) == PHYDM_SET_FAIL)
		return PHYDM_SET_FAIL;

	if (nhm_para->mntr_time >= 262)
		nhm_time = NHM_PERIOD_MAX;
	else
		nhm_time = nhm_para->mntr_time * MS_TO_4US_RATIO;

	phydm_nhm_set(dm, nhm_para->incld_txon, nhm_para->incld_cca,
		      nhm_para->div_opt, nhm_para->nhm_app, nhm_time,
		      nhm_para->en_1db_mode, nhm_para->nhm_th0_manual);

	return PHYDM_SET_SUCCESS;
}

#ifdef NHM_DYM_PW_TH_SUPPORT
void
phydm_nhm_restore_pw_th(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;

	odm_set_bb_reg(dm, R_0x82c, 0x3f, ccx->pw_th_rf20_ori);
}

void
phydm_nhm_set_pw_th(void *dm_void, u8 noise, boolean chk_succ)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	boolean not_update = false;
	u8 pw_th_rf20_new = 0;
	u8 pw_th_u_bnd = 0;
	s8 noise_diff = 0;
	u8 point_mean = 15;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	if (*dm->band_width != CHANNEL_WIDTH_20 ||
	    *dm->band_type == ODM_BAND_5G) {
		PHYDM_DBG(dm, DBG_ENV_MNTR,  "bandwidth=((%d)), band=((%d))\n",
			  *dm->band_width, *dm->band_type);
		phydm_nhm_restore_pw_th(dm);
		return;
	}

	if (chk_succ) {
		noise_diff = noise - (ccx->nhm_igi - 10);
		pw_th_u_bnd = (u8)(noise_diff + 32 + point_mean);

		pw_th_u_bnd = MIN_2(pw_th_u_bnd, ccx->nhm_pw_th_max);

		PHYDM_DBG(dm, DBG_ENV_MNTR,
			  "noise_diff=((%d)), max=((%d)), pw_th_u_bnd=((%d))\n",
			  noise_diff, ccx->nhm_pw_th_max, pw_th_u_bnd);

		if (pw_th_u_bnd > ccx->pw_th_rf20_cur) {
			pw_th_rf20_new = ccx->pw_th_rf20_cur + 1;
		} else if (pw_th_u_bnd < ccx->pw_th_rf20_cur) {
			if (ccx->pw_th_rf20_cur > ccx->pw_th_rf20_ori)
				pw_th_rf20_new = ccx->pw_th_rf20_cur - 1;
			else /*ccx->pw_th_rf20_cur == ccx->pw_th_ori*/
				not_update = true;
		} else {/*pw_th_u_bnd == ccx->pw_th_rf20_cur*/
			not_update = true;
		}
	} else {
		if (ccx->pw_th_rf20_cur > ccx->pw_th_rf20_ori)
			pw_th_rf20_new = ccx->pw_th_rf20_cur - 1;
		else /*ccx->pw_th_rf20_cur == ccx->pw_th_ori*/
			not_update = true;
	}

	PHYDM_DBG(dm, DBG_ENV_MNTR, "pw_th_cur=((%d)), pw_th_new=((%d))\n",
		  ccx->pw_th_rf20_cur, pw_th_rf20_new);

	if (!not_update) {
		odm_set_bb_reg(dm, R_0x82c, 0x3f, pw_th_rf20_new);
		ccx->pw_th_rf20_cur = pw_th_rf20_new;
	}
}

void
phydm_nhm_dym_pw_th(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u8 i = 0;
	u8 n_sum = 0;
	u8 noise = 0;
	boolean chk_succ = false;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	for (i = 0; i < NHM_RPT_NUM - 3; i++) {
		n_sum = ccx->nhm_result[i] + ccx->nhm_result[i + 1] +
			ccx->nhm_result[i + 2] + ccx->nhm_result[i + 3];
		if (n_sum >= ccx->nhm_sl_pw_th) {
			PHYDM_DBG(dm, DBG_ENV_MNTR, "Do sl[%d:%d]\n", i, i + 3);
			chk_succ = true;
			noise = phydm_nhm_cal_noise(dm, i, i + 3, n_sum);
			break;
		}
	}

	if (!chk_succ)
		PHYDM_DBG(dm, DBG_ENV_MNTR, "SL method failed!\n");

	phydm_nhm_set_pw_th(dm, noise, chk_succ);
}

boolean
phydm_nhm_dym_pw_th_en(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	struct phydm_iot_center	*iot_table = &dm->iot_table;

	if (!(dm->support_ic_type & ODM_RTL8822C))
		return false;

	if (ccx->dym_pwth_manual_ctrl)
		return true;

	if (dm->iot_table.phydm_patch_id == 0x100f0401 ||
	    iot_table->patch_id_100f0401) {
		return true;
	} else if (ccx->nhm_dym_pw_th_en) {
		phydm_nhm_restore_pw_th(dm);
		return false;
	} else {
		return false;
	}
}
#endif

/*Environment Monitor*/
boolean
phydm_nhm_mntr_chk(void *dm_void, u16 monitor_time /*unit ms*/)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	struct nhm_para_info nhm_para = {0};
	boolean nhm_chk_result = false;
	boolean nhm_polling_result = false;
	u32 sys_return_time = 0;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	if (ccx->nhm_manual_ctrl) {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "NHM in manual ctrl\n");
		return nhm_chk_result;
	}
	sys_return_time = ccx->nhm_trigger_time + MAX_ENV_MNTR_TIME;
	if (ccx->nhm_app != NHM_BACKGROUND &&
	    (sys_return_time > dm->phydm_sys_up_time)) {
		PHYDM_DBG(dm, DBG_ENV_MNTR,
			  "nhm_app=%d, trigger_time %d, sys_time=%d\n",
			  ccx->nhm_app, ccx->nhm_trigger_time,
			  dm->phydm_sys_up_time);

		return nhm_chk_result;
	}

	/*[NHM get result & calculate Utility----------------------------*/
	nhm_polling_result = phydm_nhm_get_result(dm);
	if (nhm_polling_result) {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "Get NHM_rpt success\n");
		phydm_nhm_get_utility(dm);
	}

	#ifdef NHM_DYM_PW_TH_SUPPORT
	ccx->nhm_dym_pw_th_en = phydm_nhm_dym_pw_th_en(dm);
	if (ccx->nhm_dym_pw_th_en) {
		if (nhm_polling_result)
			phydm_nhm_dym_pw_th(dm);
		else
			phydm_nhm_set_pw_th(dm, 0x0, false);
	}
	#endif

	/*[NHM trigger setting]------------------------------------------*/
	nhm_para.incld_txon = NHM_EXCLUDE_TXON;
	nhm_para.incld_cca = NHM_EXCLUDE_CCA;
	nhm_para.div_opt = NHM_CNT_ALL;
	nhm_para.nhm_app = NHM_BACKGROUND;
	nhm_para.nhm_lv = NHM_LV_1;
	nhm_para.en_1db_mode = false;
	nhm_para.mntr_time = monitor_time;

	#ifdef NHM_DYM_PW_TH_SUPPORT
	if (ccx->nhm_dym_pw_th_en) {
		nhm_para.div_opt = NHM_VALID;
		nhm_para.mntr_time = monitor_time >> ccx->nhm_period_decre;
	}
	#endif

	nhm_chk_result = phydm_nhm_mntr_set(dm, &nhm_para);

	return nhm_chk_result;
}

void phydm_nhm_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);
	PHYDM_DBG(dm, DBG_ENV_MNTR, "cur_igi=0x%x\n",
		  dm->dm_dig_table.cur_ig_value);

	ccx->nhm_app = NHM_BACKGROUND;
	ccx->nhm_igi = 0xff;

	/*Set NHM threshold*/
	ccx->nhm_ongoing = false;
	ccx->nhm_set_lv = NHM_RELEASE;

	if (phydm_nhm_th_update_chk(dm, ccx->nhm_app, &ccx->nhm_th[0],
				    (u32 *)&ccx->nhm_igi, false, 0))
		phydm_nhm_set_th_reg(dm);

	ccx->nhm_period = 0;

	ccx->nhm_include_cca = NHM_CCA_INIT;
	ccx->nhm_include_txon = NHM_TXON_INIT;
	ccx->nhm_divider_opt = NHM_CNT_INIT;

	ccx->nhm_manual_ctrl = 0;
	ccx->nhm_rpt_stamp = 0;

	#ifdef NHM_DYM_PW_TH_SUPPORT
	if (dm->support_ic_type & ODM_RTL8822C) {
		ccx->nhm_dym_pw_th_en = false;
		ccx->pw_th_rf20_ori = (u8)odm_get_bb_reg(dm, R_0x82c, 0x3f);
		ccx->pw_th_rf20_cur = ccx->pw_th_rf20_ori;
		ccx->nhm_pw_th_max = 63;
		ccx->nhm_sl_pw_th = 100; /*39%*/
		ccx->nhm_period_decre = 1;
		ccx->dym_pwth_manual_ctrl = false;
	}
	#endif
}

void phydm_nhm_dbg(void *dm_void, char input[][16], u32 *_used, char *output,
		   u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	struct nhm_para_info nhm_para;
	char help[] = "-h";
	u32 var1[10] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;
	u8 result_tmp = 0;
	u8 i = 0;

	PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "NHM Basic-Trigger 262ms: {1}\n");

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "NHM Adv-Trigger: {2} {Include TXON} {Include CCA}\n{0:Cnt_all, 1:Cnt valid} {App} {LV:1~4} {0~262ms}, 1dB mode :{en} {t[0](RSSI)}\n");
		#ifdef NHM_DYM_PW_TH_SUPPORT
		if (dm->support_ic_type & ODM_RTL8822C) {
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "NHM dym_pw_th: {3} {0:off}\n");
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "NHM dym_pw_th: {3} {1:on} {max} {period_decre} {sl_th}\n");
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "NHM dym_pw_th: {3} {2:fast on}\n");
		}
		#endif

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "NHM Get Result: {100}\n");
	} else if (var1[0] == 100) { /*Get NHM results*/

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "IGI=0x%x, rpt_stamp=%d\n", ccx->nhm_igi,
			 ccx->nhm_rpt_stamp);

		if (phydm_nhm_get_result(dm)) {
			for (i = 0; i <= 11; i++) {
				result_tmp = ccx->nhm_result[i];
				PDM_SNPF(out_len, used, output + used,
					 out_len - used,
					 "nhm_rpt[%d] = %d (%d percent)\n",
					 i, result_tmp,
					 (((result_tmp * 100) + 128) >> 8));
			}
			phydm_nhm_get_utility(dm);

			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "[NHM] valid: %d percent, noise(RSSI) = %d\n",
				 ccx->nhm_level_valid, ccx->nhm_level);
		} else {
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "Get NHM_rpt Fail\n");
		}
		ccx->nhm_manual_ctrl = 0;
	#ifdef NHM_DYM_PW_TH_SUPPORT
	} else if (var1[0] == 3) { /*NMH dym_pw_th*/
		if (dm->support_ic_type & ODM_RTL8822C) {
			for (i = 1; i < 7; i++) {
				if (input[i + 1]) {
					PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL,
						     &var1[i]);
				}
			}

			if (var1[1] == 1) {
				ccx->nhm_dym_pw_th_en = true;
				ccx->nhm_pw_th_max = (u8)var1[2];
				ccx->nhm_period_decre = (u8)var1[3];
				ccx->nhm_sl_pw_th = (u8)var1[4];
				ccx->dym_pwth_manual_ctrl = true;
			} else if (var1[1] == 2) {
				ccx->nhm_dym_pw_th_en = true;
				ccx->nhm_pw_th_max = 63;
				ccx->nhm_period_decre = 1;
				ccx->nhm_sl_pw_th = 100;
				ccx->dym_pwth_manual_ctrl = true;
			} else {
				ccx->nhm_dym_pw_th_en = false;
				phydm_nhm_restore_pw_th(dm);
				ccx->dym_pwth_manual_ctrl = false;
			}
		}
	#endif
	} else { /*NMH trigger*/
		ccx->nhm_manual_ctrl = 1;

		for (i = 1; i < 9; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL,
					     &var1[i]);
			}
		}

		if (var1[0] == 1) {
			nhm_para.incld_txon = NHM_EXCLUDE_TXON;
			nhm_para.incld_cca = NHM_EXCLUDE_CCA;
			nhm_para.div_opt = NHM_CNT_ALL;
			nhm_para.nhm_app = NHM_DBG;
			nhm_para.nhm_lv = NHM_LV_4;
			nhm_para.mntr_time = 262;
			nhm_para.en_1db_mode = false;
		} else {
			nhm_para.incld_txon = (enum nhm_option_txon_all)var1[1];
			nhm_para.incld_cca = (enum nhm_option_cca_all)var1[2];
			nhm_para.div_opt = (enum nhm_divider_opt_all)var1[3];
			nhm_para.nhm_app = (enum nhm_application)var1[4];
			nhm_para.nhm_lv = (enum phydm_nhm_level)var1[5];
			nhm_para.mntr_time = (u16)var1[6];
			nhm_para.en_1db_mode = (boolean)var1[7];
			nhm_para.nhm_th0_manual = (u8)var1[8];

			/*some old ic is not supported on NHM divider option */
			if (dm->support_ic_type & (ODM_RTL8188E | ODM_RTL8723B |
			    ODM_RTL8195A | ODM_RTL8192E)) {
				nhm_para.div_opt = NHM_CNT_ALL;
			}
		}

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "txon=%d, cca=%d, dev=%d, app=%d, lv=%d, time=%d ms\n",
			 nhm_para.incld_txon, nhm_para.incld_cca,
			 nhm_para.div_opt, nhm_para.nhm_app,
			 nhm_para.nhm_lv, nhm_para.mntr_time);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "en_1db_mode=%d, th0(for 1db mode)=%d\n",
			 nhm_para.en_1db_mode, nhm_para.nhm_th0_manual);

		if (phydm_nhm_mntr_set(dm, &nhm_para) == PHYDM_SET_SUCCESS)
			phydm_nhm_trigger(dm);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "IGI=0x%x, rpt_stamp=%d\n", ccx->nhm_igi,
			 ccx->nhm_rpt_stamp);

		for (i = 0; i <= 10; i++) {
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "NHM_th[%d] RSSI = %d\n", i,
				 NTH_TH_2_RSSI(ccx->nhm_th[i]));
		}
	}

	*_used = used;
	*_out_len = out_len;
}

#endif /*@#ifdef NHM_SUPPORT*/

#ifdef CLM_SUPPORT

void phydm_clm_racing_release(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);
	PHYDM_DBG(dm, DBG_ENV_MNTR, "lv:(%d)->(0)\n", ccx->clm_set_lv);

	ccx->clm_ongoing = false;
	ccx->clm_set_lv = CLM_RELEASE;
	ccx->clm_app = CLM_BACKGROUND;
}

u8 phydm_clm_racing_ctrl(void *dm_void, enum phydm_clm_level clm_lv)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u8 set_result = PHYDM_SET_SUCCESS;
	/*@acquire to control CLM API*/

	PHYDM_DBG(dm, DBG_ENV_MNTR, "clm_ongoing=%d, lv:(%d)->(%d)\n",
		  ccx->clm_ongoing, ccx->clm_set_lv, clm_lv);
	if (ccx->clm_ongoing) {
		if (clm_lv <= ccx->clm_set_lv) {
			set_result = PHYDM_SET_FAIL;
		} else {
			phydm_ccx_hw_restart(dm);
			ccx->clm_ongoing = false;
		}
	}

	if (set_result)
		ccx->clm_set_lv = clm_lv;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "clm racing success=%d\n", set_result);
	return set_result;
}

void phydm_clm_c2h_report_handler(void *dm_void, u8 *cmd_buf, u8 cmd_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx_info = &dm->dm_ccx_info;
	u8 clm_report = cmd_buf[0];
	/*@u8 clm_report_idx = cmd_buf[1];*/

	if (cmd_len >= 12)
		return;

	ccx_info->clm_fw_result_acc += clm_report;
	ccx_info->clm_fw_result_cnt++;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%d] clm_report= %d\n",
		  ccx_info->clm_fw_result_cnt, clm_report);
}

void phydm_clm_h2c(void *dm_void, u16 obs_time, u8 fw_clm_en)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 h2c_val[H2C_MAX_LENGTH] = {0};
	u8 i = 0;
	u8 obs_time_idx = 0;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s] ======>\n", __func__);
	PHYDM_DBG(dm, DBG_ENV_MNTR, "obs_time_index=%d *4 us\n", obs_time);

	for (i = 1; i <= 16; i++) {
		if (obs_time & BIT(16 - i)) {
			obs_time_idx = 16 - i;
			break;
		}
	}
#if 0
	obs_time = (2 ^ 16 - 1)~(2 ^ 15)  => obs_time_idx = 15  (65535 ~32768)
	obs_time = (2 ^ 15 - 1)~(2 ^ 14)  => obs_time_idx = 14
	...
	...
	...
	obs_time = (2 ^ 1 - 1)~(2 ^ 0)  => obs_time_idx = 0

#endif

	h2c_val[0] = obs_time_idx | (((fw_clm_en) ? 1 : 0) << 7);
	h2c_val[1] = CLM_MAX_REPORT_TIME;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "PHYDM h2c[0x4d]=0x%x %x %x %x %x %x %x\n",
		  h2c_val[6], h2c_val[5], h2c_val[4], h2c_val[3], h2c_val[2],
		  h2c_val[1], h2c_val[0]);

	odm_fill_h2c_cmd(dm, PHYDM_H2C_FW_CLM_MNTR, H2C_MAX_LENGTH, h2c_val);
}

void phydm_clm_setting(void *dm_void, u16 clm_period /*@4us sample 1 time*/)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;

	if (ccx->clm_period != clm_period) {
		if (dm->support_ic_type & ODM_IC_11AC_SERIES)
			odm_set_bb_reg(dm, R_0x990, MASKLWORD, clm_period);
		#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
		else if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
			odm_set_bb_reg(dm, R_0x1e40, MASKLWORD, clm_period);
		#endif
		else if (dm->support_ic_type & ODM_IC_11N_SERIES)
			odm_set_bb_reg(dm, R_0x894, MASKLWORD, clm_period);

		ccx->clm_period = clm_period;
		PHYDM_DBG(dm, DBG_ENV_MNTR,
			  "Update CLM period ((%d)) -> ((%d))\n",
			  ccx->clm_period, clm_period);
	}

	PHYDM_DBG(dm, DBG_ENV_MNTR, "Set CLM period=%d * 4us\n",
		  ccx->clm_period);
}

void phydm_clm_trigger(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u32 reg1 = 0;

	if (dm->support_ic_type & ODM_IC_11AC_SERIES)
		reg1 = R_0x994;
	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	else if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		reg1 = R_0x1e60;
	#endif
	else
		reg1 = R_0x890;
	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	odm_set_bb_reg(dm, reg1, BIT(0), 0x0);
	odm_set_bb_reg(dm, reg1, BIT(0), 0x1);

	ccx->clm_trigger_time = dm->phydm_sys_up_time;
	ccx->clm_rpt_stamp++;
	ccx->clm_ongoing = true;
}

boolean
phydm_clm_check_rdy(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	boolean is_ready = false;
	u32 reg1 = 0, reg1_bit = 0;

	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		reg1 = R_0xfa4;
		reg1_bit = 16;
	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	} else if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		reg1 = R_0x2d88;
		reg1_bit = 16;
	#endif
	} else if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		if (dm->support_ic_type & (ODM_RTL8710B | ODM_RTL8721D |
					ODM_RTL8710C)) {
			reg1 = R_0x8b4;
			reg1_bit = 24;
		} else {
			reg1 = R_0x8b4;
			reg1_bit = 16;
		}
	}
	if (odm_get_bb_reg(dm, reg1, BIT(reg1_bit)))
		is_ready = true;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "CLM rdy=%d\n", is_ready);

	return is_ready;
}

void phydm_clm_get_utility(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u32 clm_result_tmp;

	if (ccx->clm_period == 0) {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "[warning] clm_period = 0\n");
		ccx->clm_ratio = 0;
	} else if (ccx->clm_period >= 65530) {
		clm_result_tmp = (u32)(ccx->clm_result * 100);
		ccx->clm_ratio = (u8)((clm_result_tmp + (1 << 15)) >> 16);
	} else {
		clm_result_tmp = (u32)(ccx->clm_result * 100);
		ccx->clm_ratio = (u8)(clm_result_tmp / (u32)ccx->clm_period);
	}
}

boolean
phydm_clm_get_result(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx_info = &dm->dm_ccx_info;
	u32 reg1 = 0;
	u32 val = 0;

	if (dm->support_ic_type & ODM_IC_11AC_SERIES)
		reg1 = R_0x994;
	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	else if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		reg1 = R_0x1e60;
	#endif
	else
		reg1 = R_0x890;
	if (!(dm->support_ic_type & (ODM_RTL8822C | ODM_RTL8812F |
				     ODM_RTL8197G)))
		odm_set_bb_reg(dm, reg1, BIT(0), 0x0);
	if (!(phydm_clm_check_rdy(dm))) {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "Get CLM report Fail\n");
		phydm_clm_racing_release(dm);
		return false;
	}

	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		val = odm_get_bb_reg(dm, R_0xfa4, MASKLWORD);
		ccx_info->clm_result = (u16)val;
	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	} else if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		val = odm_get_bb_reg(dm, R_0x2d88, MASKLWORD);
		ccx_info->clm_result = (u16)val;
	#endif
	} else if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		val = odm_get_bb_reg(dm, R_0x8d0, MASKLWORD);
		ccx_info->clm_result = (u16)val;
	}

	PHYDM_DBG(dm, DBG_ENV_MNTR, "CLM result = %d *4 us\n",
		  ccx_info->clm_result);
	phydm_clm_racing_release(dm);
	return true;
}

void phydm_clm_mntr_fw(void *dm_void, u16 monitor_time /*unit ms*/)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u32 val = 0;

	/*@[Get CLM report]*/
	if (ccx->clm_fw_result_cnt != 0) {
		val = ccx->clm_fw_result_acc / ccx->clm_fw_result_cnt;
		ccx->clm_ratio = (u8)val;
	} else {
		ccx->clm_ratio = 0;
	}

	PHYDM_DBG(dm, DBG_ENV_MNTR,
		  "clm_fw_result_acc=%d, clm_fw_result_cnt=%d\n",
		  ccx->clm_fw_result_acc, ccx->clm_fw_result_cnt);

	ccx->clm_fw_result_acc = 0;
	ccx->clm_fw_result_cnt = 0;

	/*@[CLM trigger]*/
	if (monitor_time >= 262)
		ccx->clm_period = 65535;
	else
		ccx->clm_period = monitor_time * MS_TO_4US_RATIO;

	phydm_clm_h2c(dm, ccx->clm_period, true);
}

u8 phydm_clm_mntr_set(void *dm_void, struct clm_para_info *clm_para)
{
	/*@Driver Monitor CLM*/
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u16 clm_period = 0;

	if (clm_para->mntr_time == 0)
		return PHYDM_SET_FAIL;

	if (clm_para->clm_lv >= CLM_MAX_NUM) {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "[WARNING] Wrong LV=%d\n",
			  clm_para->clm_lv);
		return PHYDM_SET_FAIL;
	}

	if (phydm_clm_racing_ctrl(dm, clm_para->clm_lv) == PHYDM_SET_FAIL)
		return PHYDM_SET_FAIL;

	if (clm_para->mntr_time >= 262)
		clm_period = CLM_PERIOD_MAX;
	else
		clm_period = clm_para->mntr_time * MS_TO_4US_RATIO;

	ccx->clm_app = clm_para->clm_app;
	phydm_clm_setting(dm, clm_period);

	return PHYDM_SET_SUCCESS;
}

boolean
phydm_clm_mntr_chk(void *dm_void, u16 monitor_time /*unit ms*/)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	struct clm_para_info clm_para = {0};
	boolean clm_chk_result = false;
	u32 sys_return_time = 0;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s] ======>\n", __func__);
	if (ccx->clm_manual_ctrl) {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "CLM in manual ctrl\n");
		return clm_chk_result;
	}

	sys_return_time = ccx->clm_trigger_time + MAX_ENV_MNTR_TIME;

	if (ccx->clm_app != CLM_BACKGROUND &&
	    sys_return_time > dm->phydm_sys_up_time) {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "trigger_time %d, sys_time=%d\n",
			  ccx->clm_trigger_time, dm->phydm_sys_up_time);

		return clm_chk_result;
	}

	clm_para.clm_app = CLM_BACKGROUND;
	clm_para.clm_lv = CLM_LV_1;
	clm_para.mntr_time = monitor_time;
	if (ccx->clm_mntr_mode == CLM_DRIVER_MNTR) {
		/*@[Get CLM report]*/
		if (phydm_clm_get_result(dm)) {
			PHYDM_DBG(dm, DBG_ENV_MNTR, "Get CLM_rpt success\n");
			phydm_clm_get_utility(dm);
		}

		/*@[CLM trigger]----------------------------------------------*/
		if (phydm_clm_mntr_set(dm, &clm_para) == PHYDM_SET_SUCCESS)
			clm_chk_result = true;
	} else {
		phydm_clm_mntr_fw(dm, monitor_time);
	}

	PHYDM_DBG(dm, DBG_ENV_MNTR, "clm_ratio=%d\n", ccx->clm_ratio);

	/*@PHYDM_DBG(dm, DBG_ENV_MNTR, "clm_chk_result=%d\n",clm_chk_result);*/

	return clm_chk_result;
}

void phydm_set_clm_mntr_mode(void *dm_void, enum clm_monitor_mode mode)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx_info = &dm->dm_ccx_info;

	if (ccx_info->clm_mntr_mode != mode) {
		ccx_info->clm_mntr_mode = mode;
		phydm_ccx_hw_restart(dm);

		if (mode == CLM_DRIVER_MNTR)
			phydm_clm_h2c(dm, CLM_PERIOD_MAX, 0);
	}
}

void phydm_clm_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);
	ccx->clm_ongoing = false;
	ccx->clm_manual_ctrl = 0;
	ccx->clm_mntr_mode = CLM_DRIVER_MNTR;
	ccx->clm_period = 0;
	ccx->clm_rpt_stamp = 0;
	phydm_clm_setting(dm, 65535);
}

void phydm_clm_dbg(void *dm_void, char input[][16], u32 *_used, char *output,
		   u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	char help[] = "-h";
	u32 var1[10] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;
	struct clm_para_info clm_para = {0};
	u32 i;

	for (i = 0; i < 4; i++) {
		if (input[i + 1])
			PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
	}

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "CLM Driver Basic-Trigger 262ms: {1}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "CLM Driver Adv-Trigger: {2} {app} {LV} {0~262ms}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "CLM FW Trigger: {3} {1:drv, 2:fw}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "CLM Get Result: {100}\n");
	} else if (var1[0] == 100) { /* @Get CLM results */

		if (phydm_clm_get_result(dm))
			phydm_clm_get_utility(dm);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "clm_rpt_stamp=%d\n", ccx->clm_rpt_stamp);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "clm_ratio:((%d percent)) = (%d us/ %d us)\n",
			 ccx->clm_ratio, ccx->clm_result << 2,
			 ccx->clm_period << 2);

		ccx->clm_manual_ctrl = 0;
	} else if (var1[0] == 3) {
		phydm_set_clm_mntr_mode(dm, (enum clm_monitor_mode)var1[1]);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "CLM mode: %s mode\n",
			 ((ccx->clm_mntr_mode == CLM_FW_MNTR) ? "FW" : "Drv"));
	} else { /* Set & trigger CLM */
		ccx->clm_manual_ctrl = 1;

		if (var1[0] == 1) {
			clm_para.clm_app = CLM_BACKGROUND;
			clm_para.clm_lv = CLM_LV_4;
			clm_para.mntr_time = 262;
			ccx->clm_mntr_mode = CLM_DRIVER_MNTR;
		} else if (var1[0] == 2) {
			clm_para.clm_app = (enum clm_application)var1[1];
			clm_para.clm_lv = (enum phydm_clm_level)var1[2];
			ccx->clm_mntr_mode = CLM_DRIVER_MNTR;
			clm_para.mntr_time = (u16)var1[3];
		}

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "app=%d, lv=%d, mode=%s, time=%d ms\n",
			 clm_para.clm_app, clm_para.clm_lv,
			 ((ccx->clm_mntr_mode == CLM_FW_MNTR) ? "FW" :
			 "driver"), clm_para.mntr_time);

		if (phydm_clm_mntr_set(dm, &clm_para) == PHYDM_SET_SUCCESS)
			phydm_clm_trigger(dm);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "clm_rpt_stamp=%d\n", ccx->clm_rpt_stamp);
	}

	*_used = used;
	*_out_len = out_len;
}

#endif /*@#ifdef CLM_SUPPORT*/

u8 phydm_env_mntr_trigger(void *dm_void, struct nhm_para_info *nhm_para,
			  struct clm_para_info *clm_para,
			  struct env_trig_rpt *trig_rpt)
{
#if (defined(NHM_SUPPORT) && defined(CLM_SUPPORT))
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	boolean nhm_set_ok = false;
	boolean clm_set_ok = false;
	u8 trigger_result = 0;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s] ======>\n", __func__);

	/*@[NHM]*/
	nhm_set_ok = phydm_nhm_mntr_set(dm, nhm_para);

	/*@[CLM]*/
	if (ccx->clm_mntr_mode == CLM_DRIVER_MNTR) {
		clm_set_ok = phydm_clm_mntr_set(dm, clm_para);
	} else if (ccx->clm_mntr_mode == CLM_FW_MNTR) {
		phydm_clm_h2c(dm, CLM_PERIOD_MAX, true);
		trigger_result |= CLM_SUCCESS;
	}

	if (nhm_set_ok) {
		phydm_nhm_trigger(dm);
		trigger_result |= NHM_SUCCESS;
	}

	if (clm_set_ok) {
		phydm_clm_trigger(dm);
		trigger_result |= CLM_SUCCESS;
	}

	/*@monitor for the test duration*/
	ccx->start_time = odm_get_current_time(dm);

	trig_rpt->nhm_rpt_stamp = ccx->nhm_rpt_stamp;
	trig_rpt->clm_rpt_stamp = ccx->clm_rpt_stamp;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "nhm_rpt_stamp=%d, clm_rpt_stamp=%d,\n\n",
		  trig_rpt->nhm_rpt_stamp, trig_rpt->clm_rpt_stamp);

	return trigger_result;
#endif
}

u8 phydm_env_mntr_result(void *dm_void, struct env_mntr_rpt *rpt)
{
#if (defined(NHM_SUPPORT) && defined(CLM_SUPPORT))
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u8 env_mntr_rpt = 0;
	u64 progressing_time = 0;
	u32 val_tmp = 0;

	/*@monitor for the test duration*/
	progressing_time = odm_get_progressing_time(dm, ccx->start_time);
	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s] ======>\n", __func__);
	PHYDM_DBG(dm, DBG_ENV_MNTR, "env_time=%lld\n", progressing_time);

	/*@Get NHM result*/
	if (phydm_nhm_get_result(dm)) {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "Get NHM_rpt success\n");
		phydm_nhm_get_utility(dm);
		rpt->nhm_ratio = ccx->nhm_ratio;
		rpt->nhm_noise_pwr = ccx->nhm_level;
		env_mntr_rpt |= NHM_SUCCESS;

		odm_move_memory(dm, &rpt->nhm_result[0],
				&ccx->nhm_result[0], NHM_RPT_NUM);
	} else {
		rpt->nhm_ratio = ENV_MNTR_FAIL;
	}

	/*@Get CLM result*/
	if (ccx->clm_mntr_mode == CLM_DRIVER_MNTR) {
		if (phydm_clm_get_result(dm)) {
			PHYDM_DBG(dm, DBG_ENV_MNTR, "Get CLM_rpt success\n");
			phydm_clm_get_utility(dm);
			env_mntr_rpt |= CLM_SUCCESS;
			rpt->clm_ratio = ccx->clm_ratio;
		} else {
			rpt->clm_ratio = ENV_MNTR_FAIL;
		}

	} else {
		if (ccx->clm_fw_result_cnt != 0) {
			val_tmp = ccx->clm_fw_result_acc
			/ ccx->clm_fw_result_cnt;
			ccx->clm_ratio = (u8)val_tmp;
		} else {
			ccx->clm_ratio = 0;
		}

		rpt->clm_ratio = ccx->clm_ratio;
		PHYDM_DBG(dm, DBG_ENV_MNTR,
			  "clm_fw_result_acc=%d, clm_fw_result_cnt=%d\n",
			  ccx->clm_fw_result_acc, ccx->clm_fw_result_cnt);

		ccx->clm_fw_result_acc = 0;
		ccx->clm_fw_result_cnt = 0;
		env_mntr_rpt |= CLM_SUCCESS;
	}

	rpt->nhm_rpt_stamp = ccx->nhm_rpt_stamp;
	rpt->clm_rpt_stamp = ccx->clm_rpt_stamp;

	PHYDM_DBG(dm, DBG_ENV_MNTR,
		  "IGI=0x%x, nhm_ratio=%d, clm_ratio=%d, nhm_rpt_stamp=%d, clm_rpt_stamp=%d\n\n",
		  ccx->nhm_igi, rpt->nhm_ratio, rpt->clm_ratio,
		  rpt->nhm_rpt_stamp, rpt->clm_rpt_stamp);

	return env_mntr_rpt;
#endif
}

/*@Environment Monitor*/
void phydm_env_mntr_watchdog(void *dm_void)
{
#if (defined(NHM_SUPPORT) && defined(CLM_SUPPORT))
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	boolean nhm_chk_ok = false;
	boolean clm_chk_ok = false;

	if (!(dm->support_ability & ODM_BB_ENV_MONITOR))
		return;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);
	nhm_chk_ok = phydm_nhm_mntr_chk(dm, 262); /*@monitor 262ms*/
	clm_chk_ok = phydm_clm_mntr_chk(dm, 262); /*@monitor 262ms*/

	/*@PHYDM_DBG(dm, DBG_ENV_MNTR, "nhm_chk_ok %d\n\n",nhm_chk_ok);*/
	/*@PHYDM_DBG(dm, DBG_ENV_MNTR, "clm_chk_ok %d\n\n",clm_chk_ok);*/

	if (nhm_chk_ok)
		phydm_nhm_trigger(dm);

	if (clm_chk_ok)
		phydm_clm_trigger(dm);

	PHYDM_DBG(dm, DBG_ENV_MNTR,
		  "Summary: nhm_ratio=((%d)) clm_ratio=((%d))\n\n",
		  ccx->nhm_ratio, ccx->clm_ratio);
#endif
}

void phydm_env_monitor_init(void *dm_void)
{
#if (defined(NHM_SUPPORT) && defined(CLM_SUPPORT))
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (!(dm->support_ability & ODM_BB_ENV_MONITOR))
		return;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);
	phydm_ccx_hw_restart(dm);
	phydm_nhm_init(dm);
	phydm_clm_init(dm);
#endif
}

void phydm_env_mntr_dbg(void *dm_void, char input[][16], u32 *_used,
			char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	char help[] = "-h";
	u32 var1[10] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;
	struct clm_para_info clm_para = {0};
	struct nhm_para_info nhm_para = {0};
	struct env_mntr_rpt rpt = {0};
	struct env_trig_rpt trig_rpt = {0};
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u8 set_result = 0;
	u8 i = 0;

	PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Basic-Trigger 262ms: {1}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Get Result: {100}\n");
	} else if (var1[0] == 100) { /* Get results */
		set_result = phydm_env_mntr_result(dm, &rpt);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Set Result=%d\n nhm_ratio=%d clm_ratio=%d\n nhm_rpt_stamp=%d, clm_rpt_stamp=%d,\n",
			 set_result, rpt.nhm_ratio, rpt.clm_ratio,
			 rpt.nhm_rpt_stamp, rpt.clm_rpt_stamp);

		for (i = 0; i <= 11; i++) {
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "nhm_rpt[%d] = %d (%d percent)\n", i,
				 rpt.nhm_result[i],
				 (((rpt.nhm_result[i] * 100) + 128) >> 8));
		}
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "[NHM] valid: %d percent, noise(RSSI) = %d\n",
			 ccx->nhm_level_valid, ccx->nhm_level);
	} else { /* Set & trigger*/
		/*nhm para*/
		nhm_para.incld_txon = NHM_EXCLUDE_TXON;
		nhm_para.incld_cca = NHM_EXCLUDE_CCA;
		nhm_para.div_opt = NHM_CNT_ALL;
		nhm_para.nhm_app = NHM_ACS;
		nhm_para.nhm_lv = NHM_LV_2;
		nhm_para.mntr_time = 262;
		nhm_para.en_1db_mode = false;

		/*clm para*/
		clm_para.clm_app = CLM_ACS;
		clm_para.clm_lv = CLM_LV_2;
		clm_para.mntr_time = 262;

		set_result = phydm_env_mntr_trigger(dm, &nhm_para,
						    &clm_para, &trig_rpt);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Set Result=%d, nhm_rpt_stamp=%d, clm_rpt_stamp=%d\n",
			 set_result, trig_rpt.nhm_rpt_stamp,
			 trig_rpt.clm_rpt_stamp);
	}

	*_used = used;
	*_out_len = out_len;
}

