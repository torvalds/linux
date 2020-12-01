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

void phydm_fahm_racing_release(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u32 value32 = 0;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);
	PHYDM_DBG(dm, DBG_ENV_MNTR, "fahm_racing_release : lv:(%d)->(0)\n",
		  ccx->fahm_set_lv);

	ccx->fahm_ongoing = false;
	ccx->fahm_set_lv = FAHM_RELEASE;

	if (!(ccx->fahm_app == FAHM_BACKGROUND || ccx->fahm_app == FAHM_ACS))
		phydm_pause_func(dm, F00_DIG, PHYDM_RESUME,
				 PHYDM_PAUSE_LEVEL_1, 1, &value32);

	ccx->fahm_app = FAHM_BACKGROUND;
}

u8 phydm_fahm_racing_ctrl(void *dm_void, enum phydm_fahm_level lv)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u8 set_result = PHYDM_SET_SUCCESS;
	/*acquire to control FAHM API*/

	PHYDM_DBG(dm, DBG_ENV_MNTR, "fahm_ongoing=%d, lv:(%d)->(%d)\n",
		  ccx->fahm_ongoing, ccx->fahm_set_lv, lv);
	if (ccx->fahm_ongoing) {
		if (lv <= ccx->fahm_set_lv) {
			set_result = PHYDM_SET_FAIL;
		} else {
			phydm_ccx_hw_restart(dm);
			ccx->fahm_ongoing = false;
		}
	}

	if (set_result)
		ccx->fahm_set_lv = lv;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "fahm racing success=%d\n", set_result);
	return set_result;
}

void phydm_fahm_trigger(void *dm_void)
{ /*@unit (4us)*/
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u32 reg = 0;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	switch (dm->ic_ip_series) {
	case PHYDM_IC_JGR3:
		reg = R_0x1e60;
		break;
	case PHYDM_IC_AC:
		reg = R_0x994;
		break;
	case PHYDM_IC_N:
		reg = R_0x890;
		break;
	default:
		break;
	}

	odm_set_bb_reg(dm, reg, BIT(2), 0);
	odm_set_bb_reg(dm, reg, BIT(2), 1);

	ccx->fahm_trigger_time = dm->phydm_sys_up_time;
	ccx->fahm_rpt_stamp++;
	ccx->fahm_ongoing = true;
}

boolean
phydm_fahm_check_rdy(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	boolean is_ready = false;
	u32 reg = 0, reg_bit = 0;

	switch (dm->ic_ip_series) {
	case PHYDM_IC_JGR3:
		reg = R_0x2d84;
		reg_bit = 31;
		break;
	case PHYDM_IC_AC:
		reg = R_0x1f98;
		reg_bit = 31;
		break;
	case PHYDM_IC_N:
		reg = R_0x9f0;
		reg_bit = 31;
		break;
	default:
		break;
	}

	if (odm_get_bb_reg(dm, reg, BIT(reg_bit)))
		is_ready = true;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "FAHM rdy=%d\n", is_ready);

	return is_ready;
}

u8 phydm_fahm_cal_wgt_avg(void *dm_void, u8 start_i, u8 end_i, u16 r_sum,
			  u16 period)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u8 i = 0;
	u32 pwr_tmp = 0;
	u8 pwr = 0;
	u32 fahm_valid = 0;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	if (r_sum == 0) {
		PHYDM_DBG(dm, DBG_ENV_MNTR,
			  "rpt_sum = 0, don't need to update\n");
		return 0x0;
	} else if (end_i > NHM_RPT_NUM - 1) {
		PHYDM_DBG(dm, DBG_ENV_MNTR,
			  "[WARNING]end_i is larger than 11!!\n");
		return 0x0;
	}

	for (i = start_i; i <= end_i; i++) {
		if (i == 0)
			pwr_tmp += ccx->fahm_result[0] *
				   MAX_2(ccx->fahm_th[0] - 2, 0);
		else if (i == (NHM_RPT_NUM - 1))
			pwr_tmp += ccx->fahm_result[NHM_RPT_NUM - 1] *
				   (ccx->fahm_th[NHM_TH_NUM - 1] + 2);
		else
			pwr_tmp += ccx->fahm_result[i] *
				   (ccx->fahm_th[i - 1] + ccx->fahm_th[i]) >> 1;
	}

	/* protection for the case of minus pwr(RSSI)*/
	pwr = (u8)(NTH_TH_2_RSSI(MAX_2(PHYDM_DIV(pwr_tmp, r_sum), 20)));
	fahm_valid = PHYDM_DIV(r_sum * 100, period);
	PHYDM_DBG(dm, DBG_ENV_MNTR,
		  "valid: ((%d)) percent, pwr(RSSI)=((%d))\n",
		  fahm_valid, pwr);

	return pwr;
}

void phydm_fahm_get_utility(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;

	if (ccx->fahm_rpt_sum >= ccx->fahm_result[0]) {
		ccx->fahm_pwr = phydm_fahm_cal_wgt_avg(dm, 0, NHM_RPT_NUM - 1,
						       ccx->fahm_rpt_sum,
						       ccx->fahm_period);
	} else {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "[warning] fahm_rpt_sum invalid\n");
		ccx->fahm_pwr = 0;
	}

	PHYDM_DBG(dm, DBG_ENV_MNTR, "fahm_pwr=%d\n", ccx->fahm_pwr);
}

boolean
phydm_fahm_get_result(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u32 value32 = 0;
	u16 denom; /*fahm_denominator packet count*/
	u32 reg1 = 0;
	u32 reg2 = 0;
	u8 i = 0;
	u32 fahm_rpt_sum_tmp = 0;

	switch (dm->ic_ip_series) {
	case PHYDM_IC_JGR3:
		reg1 = R_0x2d6c;
		reg2 = R_0x2d84;
		break;
	case PHYDM_IC_AC:
		reg1 = R_0x1f80;
		reg2 = R_0x1f98;
		break;
	case PHYDM_IC_N:
		reg1 = R_0x9d8;
		reg2 = R_0x9f0;
		break;
	default:
		break;
	}

	if (!(phydm_fahm_check_rdy(dm))) {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "Get FAHM report Fail\n");
		phydm_fahm_racing_release(dm);
		return false;
	}

	/*@Get FAHM Denominator*/
	denom = (u16)odm_get_bb_reg(dm, reg2, MASKLWORD);

	if (ccx->fahm_period >= 65530)
		PHYDM_DBG(dm, DBG_ENV_MNTR,
			  "FAHM denominator = %d, valid: %d percent\n", denom,
			  (denom * 100) >> 16);

	/*Get FAHM numerator and sum all fahm_result*/
	for (i = 0; i < 6; i++) {
		value32 = odm_get_bb_reg(dm, reg1 + (i << 2), MASKDWORD);
		ccx->fahm_result[i * 2] = (u16)(value32 & MASKLWORD);
		ccx->fahm_result[i * 2 + 1] = (u16)((value32 & MASKHWORD) >> 16);
		fahm_rpt_sum_tmp = (u32)(fahm_rpt_sum_tmp +
					 ccx->fahm_result[i * 2] +
					 ccx->fahm_result[i * 2 + 1]);
	}

	ccx->fahm_rpt_sum = (u16)fahm_rpt_sum_tmp;

	PHYDM_DBG(dm, DBG_ENV_MNTR,
		  "FAHM_Rpt[%d](H->L)[%d %d %d %d %d %d %d %d %d %d %d %d]\n",
		  ccx->fahm_rpt_stamp, ccx->fahm_result[11],
		  ccx->fahm_result[10], ccx->fahm_result[9],
		  ccx->fahm_result[8], ccx->fahm_result[7], ccx->fahm_result[6],
		  ccx->fahm_result[5], ccx->fahm_result[4], ccx->fahm_result[3],
		  ccx->fahm_result[2], ccx->fahm_result[1],
		  ccx->fahm_result[0]);

	phydm_fahm_racing_release(dm);

	if (fahm_rpt_sum_tmp > 0xffff) {
		PHYDM_DBG(dm, DBG_ENV_MNTR,
			  "[Warning] Invalid FAHM RPT, total=%d\n",
			  fahm_rpt_sum_tmp);
		return false;
	}

	return true;
}

void phydm_fahm_set_th_reg(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u32 val = 0;

	/*Set FAHM threshold*/ /*Unit: PWdB U(8,1)*/
	switch (dm->ic_ip_series) {
	case PHYDM_IC_JGR3:
		val = BYTE_2_DWORD(ccx->fahm_th[3], ccx->fahm_th[2],
				   ccx->fahm_th[1], ccx->fahm_th[0]);
		odm_set_bb_reg(dm, R_0x1e50, MASKDWORD, val);
		val = BYTE_2_DWORD(ccx->fahm_th[7], ccx->fahm_th[6],
				   ccx->fahm_th[5], ccx->fahm_th[4]);
		odm_set_bb_reg(dm, R_0x1e54, MASKDWORD, val);
		val = BYTE_2_DWORD(0, ccx->fahm_th[10], ccx->fahm_th[9],
				   ccx->fahm_th[8]);
		odm_set_bb_reg(dm, R_0x1e58, 0xffffff, val);
		break;
	case PHYDM_IC_AC:
		val = BYTE_2_DWORD(0, ccx->fahm_th[2], ccx->fahm_th[1],
				   ccx->fahm_th[0]);
		odm_set_bb_reg(dm, R_0x1c38, 0xffffff00, val);
		val = BYTE_2_DWORD(0, ccx->fahm_th[5], ccx->fahm_th[4],
				   ccx->fahm_th[3]);
		odm_set_bb_reg(dm, R_0x1c78, 0xffffff00, val);
		val = BYTE_2_DWORD(0, 0, ccx->fahm_th[7], ccx->fahm_th[6]);
		odm_set_bb_reg(dm, R_0x1c7c, 0xffff0000, val);
		val = BYTE_2_DWORD(0, ccx->fahm_th[10], ccx->fahm_th[9],
				   ccx->fahm_th[8]);
		odm_set_bb_reg(dm, R_0x1cb8, 0xffffff00, val);
		break;
	case PHYDM_IC_N:
		val = BYTE_2_DWORD(ccx->fahm_th[3], ccx->fahm_th[2],
				   ccx->fahm_th[1], ccx->fahm_th[0]);
		odm_set_bb_reg(dm, R_0x970, MASKDWORD, val);
		val = BYTE_2_DWORD(ccx->fahm_th[7], ccx->fahm_th[6],
				   ccx->fahm_th[5], ccx->fahm_th[4]);
		odm_set_bb_reg(dm, R_0x974, MASKDWORD, val);
		val = BYTE_2_DWORD(0, ccx->fahm_th[10], ccx->fahm_th[9],
				   ccx->fahm_th[8]);
		odm_set_bb_reg(dm, R_0x978, 0xffffff, val);
		break;
	default:
		break;
	}

	PHYDM_DBG(dm, DBG_ENV_MNTR,
		  "Update FAHM_th[H->L]=[%d %d %d %d %d %d %d %d %d %d %d]\n",
		  ccx->fahm_th[10], ccx->fahm_th[9], ccx->fahm_th[8],
		  ccx->fahm_th[7], ccx->fahm_th[6], ccx->fahm_th[5],
		  ccx->fahm_th[4], ccx->fahm_th[3], ccx->fahm_th[2],
		  ccx->fahm_th[1], ccx->fahm_th[0]);
}

boolean
phydm_fahm_th_update_chk(void *dm_void, enum fahm_application fahm_app,
			 u8 *fahm_th, u32 *igi_new, boolean en_1db_mode,
			 u8 fahm_th0_manual)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	boolean is_update = false;
	u8 igi_curr = phydm_get_igi(dm, BB_PATH_A);
	u8 i = 0;
	u8 th_tmp = igi_curr - CCA_CAP;
	u8 th_step = 2;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "fahm_th_update_chk : App=%d, fahm_igi=0x%x, igi_curr=0x%x\n",
		  fahm_app, ccx->fahm_igi, igi_curr);

	if (igi_curr < 0x10) /* Protect for invalid IGI*/
		return false;

	switch (fahm_app) {
	case FAHM_BACKGROUND: /*Get IGI from driver parameter(cur_ig_value)*/
		if (ccx->fahm_igi != igi_curr || ccx->fahm_app != fahm_app) {
			is_update = true;
			*igi_new = (u32)igi_curr;

			fahm_th[0] = (u8)IGI_2_NHM_TH(th_tmp);

			for (i = 1; i <= 10; i++)
				fahm_th[i] = fahm_th[0] +
					    IGI_2_NHM_TH(th_step * i);

		}
		break;
	case FAHM_ACS:
		if (ccx->fahm_igi != igi_curr || ccx->fahm_app != fahm_app) {
			is_update = true;
			*igi_new = (u32)igi_curr;
			fahm_th[0] = (u8)IGI_2_NHM_TH(igi_curr - CCA_CAP);
			for (i = 1; i <= 10; i++)
				fahm_th[i] = fahm_th[0] + IGI_2_NHM_TH(2 * i);
		}
		break;
	case FAHM_DBG: /*Get IGI from register*/
		igi_curr = phydm_get_igi(dm, BB_PATH_A);
		if (ccx->fahm_igi != igi_curr || ccx->fahm_app != fahm_app) {
			is_update = true;
			*igi_new = (u32)igi_curr;
			if (en_1db_mode) {
				fahm_th[0] = (u8)IGI_2_NHM_TH(fahm_th0_manual +
							      10);
				th_step = 1;
			} else {
				fahm_th[0] = (u8)IGI_2_NHM_TH(igi_curr -
							      CCA_CAP);
			}

			for (i = 1; i <= 10; i++)
				fahm_th[i] = fahm_th[0] +
					     IGI_2_NHM_TH(th_step * i);
		}
		break;
	}

	if (is_update) {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "[Update FAHM_TH] igi_RSSI=%d\n",
			  IGI_2_RSSI(*igi_new));

		for (i = 0; i < NHM_TH_NUM; i++)
			PHYDM_DBG(dm, DBG_ENV_MNTR, "FAHM_th[%d](RSSI) = %d\n",
				  i, NTH_TH_2_RSSI(fahm_th[i]));
	} else {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "No need to update FAHM_TH\n");
	}
	return is_update;
}

void phydm_fahm_set(void *dm_void, enum fahm_opt_fa inclu_fa,
		    enum fahm_opt_crc32_ok inclu_crc32_ok,
		    enum fahm_opt_crc32_err inclu_crc32_err,
		    enum fahm_application app, u16 period, boolean en_1db_mode,
		    u8 th0_manual)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u8 fahm_th[NHM_TH_NUM] = {0};
	u32 igi = 0x20;
	u32 reg1 = 0, reg2 = 0, reg3 = 0;
	u32 val_tmp = 0;

	PHYDM_DBG(dm, DBG_ENV_MNTR,
		  "incld{fa, crc32_ok, crc32_err}={%d, %d, %d}, period=%d\n",
		  inclu_fa, inclu_crc32_ok, inclu_crc32_err, period);

	switch (dm->ic_ip_series) {
	case PHYDM_IC_JGR3:
		reg1 = R_0x1e60;
		reg2 = R_0x1e58;
		reg3 = R_0x1e5c;
		break;
	case PHYDM_IC_AC:
		reg1 = R_0x994;
		reg2 = R_0x1cf8;
		break;
	case PHYDM_IC_N:
		reg1 = R_0x890;
		reg2 = R_0x978;
		reg3 = R_0x97c;
		break;
	default:
		 break;
	}

	/*Set enable fa, ignore crc32 ok, ignore crc32 err*/
	if (inclu_fa != ccx->fahm_incld_fa ||
	    inclu_crc32_ok != ccx->fahm_incld_crc32_ok ||
	    inclu_crc32_err != ccx->fahm_incld_crc32_err) {
		val_tmp = (u32)((inclu_crc32_err << 2) | (inclu_crc32_ok << 1) |
			  inclu_fa);
		odm_set_bb_reg(dm, reg1, 0xe0, val_tmp);
		ccx->fahm_incld_fa = inclu_fa;
		ccx->fahm_incld_crc32_ok = inclu_crc32_ok;
		ccx->fahm_incld_crc32_err = inclu_crc32_err;
	}

	/*Set FAHM period*/
	if (period != ccx->fahm_period) {
		switch (dm->ic_ip_series) {
		case PHYDM_IC_AC:
			odm_set_bb_reg(dm, reg2, 0xffff00, period);
			break;
		case PHYDM_IC_JGR3:
		case PHYDM_IC_N:
			odm_set_bb_reg(dm, reg2, 0xff000000, (period & 0xff));
			odm_set_bb_reg(dm, reg3, 0xff, (period & 0xff00) >> 8);
			break;
		default:
			break;
		}

		PHYDM_DBG(dm, DBG_ENV_MNTR,
			  "Update FAHM period ((%d)) -> ((%d))\n",
			  ccx->fahm_period, period);

		ccx->fahm_period = period;
	}

	/*Set FAHM threshold*/
	if (phydm_fahm_th_update_chk(dm, app, &fahm_th[0], &igi, en_1db_mode,
				     th0_manual)) {
		/*Pause IGI*/
		if (app == FAHM_BACKGROUND || app == FAHM_ACS) {
			PHYDM_DBG(dm, DBG_ENV_MNTR, "DIG Free Run\n");
		} else if (phydm_pause_func(dm, F00_DIG, PHYDM_PAUSE,
					    PHYDM_PAUSE_LEVEL_1, 1, &igi)
					    == PAUSE_FAIL) {
			PHYDM_DBG(dm, DBG_ENV_MNTR, "pause DIG Fail\n");
			return;
		} else {
			PHYDM_DBG(dm, DBG_ENV_MNTR, "pause DIG=0x%x\n", igi);
		}
		ccx->fahm_app = app;
		ccx->fahm_igi = (u8)igi;
		odm_move_memory(dm, &ccx->fahm_th[0], &fahm_th, NHM_TH_NUM);

		/*Set FAHM th*/
		phydm_fahm_set_th_reg(dm);
	}
}

boolean
phydm_fahm_mntr_set(void *dm_void, struct fahm_para_info *para)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u16 fahm_time = 0; /*unit: 4us*/

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	if (para->mntr_time == 0)
		return false;

	if (para->lv >= FAHM_MAX_NUM) {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "Wrong LV=%d\n", para->lv);
		return false;
	}

	if (phydm_fahm_racing_ctrl(dm, para->lv) == PHYDM_SET_FAIL)
		return false;

	if (para->mntr_time >= 262)
		fahm_time = NHM_PERIOD_MAX;
	else
		fahm_time = para->mntr_time * MS_TO_4US_RATIO;

	phydm_fahm_set(dm, para->incld_fa, para->incld_crc32_ok,
		       para->incld_crc32_err, para->app, fahm_time,
		       para->en_1db_mode, para->th0_manual);

	return true;
}

boolean
phydm_fahm_mntr_chk(void *dm_void, u16 monitor_time /*unit ms*/)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	struct fahm_para_info para = {0};
	boolean fahm_chk_result = false;
	boolean fahm_polling_result = false;
	u32 sys_return_time = 0;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	if (ccx->fahm_manual_ctrl) {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "FAHM in manual ctrl\n");
		return fahm_chk_result;
	}
	sys_return_time = ccx->fahm_trigger_time + MAX_ENV_MNTR_TIME;
	if (ccx->fahm_app != FAHM_BACKGROUND &&
	    (sys_return_time > dm->phydm_sys_up_time)) {
		PHYDM_DBG(dm, DBG_ENV_MNTR,
			  "fahm_app=%d, trigger_time %d, sys_time=%d\n",
			  ccx->fahm_app, ccx->fahm_trigger_time,
			  dm->phydm_sys_up_time);

		return fahm_chk_result;
	}

	/*[FAHM get result & calculate Utility]---------------------------*/
	fahm_polling_result = phydm_fahm_get_result(dm);
	if (fahm_polling_result) {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "Get FAHM_rpt success\n");
		phydm_fahm_get_utility(dm);
	}

	/*[FAHM trigger setting]------------------------------------------*/
	para.incld_fa = FAHM_INCLUDE_FA;
	para.incld_crc32_ok = FAHM_EXCLUDE_CRC32_OK;
	para.incld_crc32_err = FAHM_EXCLUDE_CRC32_ERR;
	para.app = FAHM_BACKGROUND;
	para.lv = FAHM_LV_1;
	para.en_1db_mode = false;
	para.mntr_time = monitor_time;

	fahm_chk_result = phydm_fahm_mntr_set(dm, &para);

	return fahm_chk_result;
}

void phydm_fahm_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u8 denum_sel = 0;
	u32 reg = 0;

	if (!(dm->support_ic_type & PHYDM_IC_SUPPORT_FAHM))
		return;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	ccx->fahm_app = FAHM_BACKGROUND;
	ccx->fahm_igi = 0xff;

	/*Set FAHM threshold*/
	ccx->fahm_ongoing = false;
	ccx->fahm_set_lv = FAHM_RELEASE;

	if (phydm_fahm_th_update_chk(dm, ccx->fahm_app, &ccx->fahm_th[0],
				    (u32 *)&ccx->fahm_igi, false, 0))
		phydm_fahm_set_th_reg(dm);

	ccx->fahm_period = 0;

	ccx->fahm_incld_fa = FAHM_FA_INIT;
	ccx->fahm_incld_crc32_ok = FAHM_CRC32_OK_INIT;
	ccx->fahm_incld_crc32_err = FAHM_CRC32_ERR_INIT;

	ccx->fahm_manual_ctrl = 0;
	ccx->fahm_rpt_stamp = 0;

	switch (dm->ic_ip_series) {
	case PHYDM_IC_JGR3:
		reg = R_0x1e60;
		break;
	case PHYDM_IC_AC:
		reg = R_0x994;
		break;
	case PHYDM_IC_N:
		reg = R_0x890;
		break;
	default:
		break;
	}

	/*enable CCK/OFDM CRC32 check*/
	odm_set_bb_reg(dm, reg, 0x18, 0x3);
	/*denominator:FA/CRC32_OK/CRC32_ERR*/
	odm_set_bb_reg(dm, reg, 0x7000, 0x7);
}

void phydm_fahm_dbg(void *dm_void, char input[][16], u32 *_used, char *output,
		    u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	struct fahm_para_info para = {0};
	char help[] = "-h";
	u32 var1[10] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;
	u16 result_tmp = 0;
	u8 i = 0;

	if (!(dm->support_ic_type & PHYDM_IC_SUPPORT_FAHM))
		return;

	PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "FAHM Basic-Trigger 262ms: {1}\n");

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "FAHM Adv-Trigger: {2} {Include FA} {Include CRC32 ok} {Include CRC32 Err}\n {App:1 for dbg} {LV:1~4} {0~262ms}, 1dB mode :{en} {t[0](RSSI)}\n");

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "FAHM Get Result: {100}\n");
	} else if (var1[0] == 100) { /*Get FAHM results*/
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "IGI=0x%x, rpt_stamp=%d\n", ccx->fahm_igi,
			 ccx->fahm_rpt_stamp);

		if (phydm_fahm_get_result(dm)) {
			for (i = 0; i < NHM_RPT_NUM; i++) {
				result_tmp = ccx->fahm_result[i];
				PDM_SNPF(out_len, used, output + used,
					 out_len - used,
					 "fahm_rpt[%d] = %d (%d percent)\n",
					 i, result_tmp,
					 (((result_tmp * 100) + 32768) >> 16));
			}
			phydm_fahm_get_utility(dm);

			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "fahm_pwr=%d\n", ccx->fahm_pwr);
		} else {
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "Get FAHM_rpt Fail\n");
		}
		ccx->fahm_manual_ctrl = 0;
	} else { /*FAMH trigger*/
		ccx->fahm_manual_ctrl = 1;

		for (i = 1; i < 9; i++)
			PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);

		if (var1[0] == 1) {
			para.incld_fa = FAHM_INCLUDE_FA;
			para.incld_crc32_ok = FAHM_EXCLUDE_CRC32_OK;
			para.incld_crc32_err = FAHM_EXCLUDE_CRC32_ERR;
			para.app = FAHM_DBG;
			para.lv = FAHM_LV_4;
			para.mntr_time = 262;
			para.en_1db_mode = false;
			para.th0_manual = 0;
		} else {
			para.incld_fa = (enum fahm_opt_fa)var1[1];
			para.incld_crc32_ok = (enum fahm_opt_crc32_ok)var1[2];
			para.incld_crc32_err = (enum fahm_opt_crc32_err)var1[3];
			para.app = (enum fahm_application)var1[4];
			para.lv = (enum phydm_fahm_level)var1[5];
			para.mntr_time = (u16)var1[6];
			para.en_1db_mode = (boolean)var1[7];
			para.th0_manual = (u8)var1[8];
		}

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "fa=%d, crc32_ok=%d, crc32_err=%d, app=%d, lv=%d, time=%d ms\n",
			 para.incld_fa, para.incld_crc32_ok,
			 para.incld_crc32_err, para.app, para.lv,
			 para.mntr_time);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "en_1db_mode=%d, th0(for 1db mode)=%d\n",
			 para.en_1db_mode, para.th0_manual);

		if (phydm_fahm_mntr_set(dm, &para))
			phydm_fahm_trigger(dm);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "IGI=0x%x, rpt_stamp=%d\n", ccx->fahm_igi,
			 ccx->fahm_rpt_stamp);

		for (i = 0; i < NHM_TH_NUM; i++)
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "FAHM_th[%d] RSSI = %d\n", i,
				 NTH_TH_2_RSSI(ccx->fahm_th[i]));
	}

	*_used = used;
	*_out_len = out_len;
}

void phydm_fahm_watchdog(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	boolean fahm_chk_ok = false;

	if (!(dm->support_ic_type & PHYDM_IC_SUPPORT_FAHM))
		return;

	fahm_chk_ok = phydm_fahm_mntr_chk(dm, 262);

	if (fahm_chk_ok)
		phydm_fahm_trigger(dm);
}


#endif /*#ifdef FAHM_SUPPORT*/

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

void phydm_nhm_cal_wgt(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u8 i = 0;

	for (i = 0; i < NHM_RPT_NUM; i++) {
		if (i == 0)
			ccx->nhm_wgt[0] = (u8)(MAX_2(ccx->nhm_th[0] - 2, 0));
		else if (i == (NHM_RPT_NUM - 1))
			ccx->nhm_wgt[NHM_RPT_NUM - 1] = (u8)(ccx->nhm_th[NHM_TH_NUM - 1] + 2);
		else
			ccx->nhm_wgt[i] = (u8)((ccx->nhm_th[i - 1] + ccx->nhm_th[i]) >> 1);
	}
}

u8 phydm_nhm_cal_wgt_avg(void *dm_void, u8 start_i, u8 end_i, u8 n_sum)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u8 i = 0;
	u32 noise_tmp = 0;
	u8 noise = 0;
	u32 nhm_valid = 0;

	if (n_sum == 0) {
		PHYDM_DBG(dm, DBG_ENV_MNTR,
			  "n_sum = 0, don't need to update noise\n");
		return 0x0;
	} else if (end_i > NHM_RPT_NUM - 1) {
		PHYDM_DBG(dm, DBG_ENV_MNTR,
			  "[WARNING]end_i is larger than 11!!\n");
		return 0x0;
	}

	for (i = start_i; i <= end_i; i++)
		noise_tmp += ccx->nhm_result[i] * ccx->nhm_wgt[i];

	/* protection for the case of minus noise(RSSI)*/
	noise = (u8)(NTH_TH_2_RSSI(MAX_2(PHYDM_DIV(noise_tmp, n_sum), 20)));
	nhm_valid = (n_sum * 100) >> 8;
	PHYDM_DBG(dm, DBG_ENV_MNTR,
		  "cal wgt_avg : valid: ((%d)) percent, noise(RSSI)=((%d))\n",
		  nhm_valid, noise);

	return noise;
}

u8 phydm_nhm_cal_nhm_env(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u8 first_idx = 0;
	u8 nhm_env = 0;
	u8 i = 0;

	nhm_env = ccx->nhm_rpt_sum;

	/*search first cluster*/
	for (i = 0; i < NHM_RPT_NUM; i++) {
		if (ccx->nhm_result[i]) {
			first_idx = i;
			break;
		}
	}

	/*exclude first cluster under -80dBm*/
	for (i = 0; i < 4; i++) {
		if (((first_idx + i) < NHM_RPT_NUM) &&
		    (ccx->nhm_wgt[first_idx + i] <= NHM_IC_NOISE_TH))
			nhm_env -= ccx->nhm_result[first_idx + i];
	}

	/*exclude nhm_rpt[0] above -80dBm*/
	if (ccx->nhm_wgt[0] > NHM_IC_NOISE_TH)
		nhm_env -= ccx->nhm_result[0];

	PHYDM_DBG(dm, DBG_ENV_MNTR, "cal nhm_env: first_idx=%d, nhm_env=%d\n",
		  first_idx, nhm_env);

	return nhm_env;
}

void phydm_nhm_get_utility(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u8 nhm_rpt_non_0 = 0;
	u8 nhm_rpt_non_11 = 0;
	u8 nhm_env = 0;

	if (ccx->nhm_rpt_sum >= ccx->nhm_result[0]) {
		phydm_nhm_cal_wgt(dm);

		nhm_rpt_non_0 = ccx->nhm_rpt_sum - ccx->nhm_result[0];
		nhm_rpt_non_11 = ccx->nhm_rpt_sum - ccx->nhm_result[11];
		/*exclude nhm_r[0] above -80dBm or first cluster under -80dBm*/
		nhm_env = phydm_nhm_cal_nhm_env(dm);
		ccx->nhm_ratio = (nhm_rpt_non_0 * 100) >> 8;
		ccx->nhm_env_ratio = (nhm_env * 100) >> 8;
		ccx->nhm_level_valid = (nhm_rpt_non_11 * 100) >> 8;
		ccx->nhm_level = phydm_nhm_cal_wgt_avg(dm, 0, NHM_RPT_NUM - 2,
						     nhm_rpt_non_11);
		ccx->nhm_pwr = phydm_nhm_cal_wgt_avg(dm, 0, NHM_RPT_NUM - 1,
						     ccx->nhm_rpt_sum);
	} else {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "[warning] nhm_rpt_sum invalid\n");
		ccx->nhm_ratio = 0;
		ccx->nhm_env_ratio = 0;
	}

	PHYDM_DBG(dm, DBG_ENV_MNTR,
		  "nhm_ratio=%d, nhm_env_ratio=%d, nhm_level=%d, nhm_pwr=%d\n",
		  ccx->nhm_ratio, ccx->nhm_env_ratio, ccx->nhm_level,
		  ccx->nhm_pwr);
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
	u16 nhm_duration = 0;

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
				     ODM_RTL8197G | ODM_RTL8723F)))
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
		nhm_duration = (u16)(value32 & MASKLWORD);
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
		nhm_duration = (u16)(value32 & MASKLWORD);
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
		nhm_duration = (u16)(value32 & MASKLWORD);
	}

	/* sum all nhm_result */
	if (ccx->nhm_period >= 65530)
		PHYDM_DBG(dm, DBG_ENV_MNTR,
			  "NHM valid time = %d, valid: %d percent\n",
			  nhm_duration, (nhm_duration * 100) >> 16);

	for (i = 0; i < NHM_RPT_NUM; i++)
		nhm_rpt_sum_tmp = (u16)(nhm_rpt_sum_tmp + ccx->nhm_result[i]);

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

boolean
phydm_nhm_mntr_set(void *dm_void, struct nhm_para_info *nhm_para)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u16 nhm_time = 0; /*unit: 4us*/

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	if (nhm_para->mntr_time == 0)
		return false;

	if (nhm_para->nhm_lv >= NHM_MAX_NUM) {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "Wrong LV=%d\n", nhm_para->nhm_lv);
		return false;
	}

	if (phydm_nhm_racing_ctrl(dm, nhm_para->nhm_lv) == PHYDM_SET_FAIL)
		return false;

	if (nhm_para->mntr_time >= 262)
		nhm_time = NHM_PERIOD_MAX;
	else
		nhm_time = nhm_para->mntr_time * MS_TO_4US_RATIO;

	phydm_nhm_set(dm, nhm_para->incld_txon, nhm_para->incld_cca,
		      nhm_para->div_opt, nhm_para->nhm_app, nhm_time,
		      nhm_para->en_1db_mode, nhm_para->nhm_th0_manual);

	return true;
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
			noise = phydm_nhm_cal_wgt_avg(dm, i, i + 3, n_sum);
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
	struct nhm_para_info nhm_para = {0};
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
			 "NHM Adv-Trigger: {2} {Include TXON} {Include CCA}\n{0:Cnt_all, 1:Cnt valid} {App:5 for dbg} {LV:1~4} {0~262ms}, 1dB mode :{en} {t[0](RSSI)}\n");
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
			for (i = 0; i < NHM_RPT_NUM; i++) {
				result_tmp = ccx->nhm_result[i];
				PDM_SNPF(out_len, used, output + used,
					 out_len - used,
					 "nhm_rpt[%d] = %d (%d percent)\n",
					 i, result_tmp,
					 (((result_tmp * 100) + 128) >> 8));
			}
			phydm_nhm_get_utility(dm);

			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "NHM_noise: valid: %d percent, noise(RSSI) = %d\n",
				 ccx->nhm_level_valid, ccx->nhm_level);
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "NHM_pwr: nhm_pwr (RSSI) = %d\n", ccx->nhm_pwr);
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "ratio: nhm_ratio=%d, nhm_env_ratio=%d\n",
				 ccx->nhm_ratio, ccx->nhm_env_ratio);
		} else {
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "Get NHM_rpt Fail\n");
		}
		ccx->nhm_manual_ctrl = 0;
	#ifdef NHM_DYM_PW_TH_SUPPORT
	} else if (var1[0] == 3) { /*NMH dym_pw_th*/
		if (dm->support_ic_type & ODM_RTL8822C) {
			for (i = 1; i < 7; i++) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL,
					     &var1[i]);
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
			PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL,
				     &var1[i]);
		}

		if (var1[0] == 1) {
			nhm_para.incld_txon = NHM_EXCLUDE_TXON;
			nhm_para.incld_cca = NHM_EXCLUDE_CCA;
			nhm_para.div_opt = NHM_CNT_ALL;
			nhm_para.nhm_app = NHM_DBG;
			nhm_para.nhm_lv = NHM_LV_4;
			nhm_para.mntr_time = 262;
			nhm_para.en_1db_mode = false;
			nhm_para.nhm_th0_manual = 0;
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

		if (phydm_nhm_mntr_set(dm, &nhm_para))
			phydm_nhm_trigger(dm);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "IGI=0x%x, rpt_stamp=%d\n", ccx->nhm_igi,
			 ccx->nhm_rpt_stamp);

		for (i = 0; i < NHM_TH_NUM; i++) {
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
				     ODM_RTL8197G | ODM_RTL8723F)))
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

boolean
phydm_clm_mntr_set(void *dm_void, struct clm_para_info *clm_para)
{
	/*@Driver Monitor CLM*/
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u16 clm_period = 0;

	if (clm_para->mntr_time == 0)
		return false;

	if (clm_para->clm_lv >= CLM_MAX_NUM) {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "[WARNING] Wrong LV=%d\n",
			  clm_para->clm_lv);
		return false;
	}

	if (phydm_clm_racing_ctrl(dm, clm_para->clm_lv) == PHYDM_SET_FAIL)
		return false;

	if (clm_para->mntr_time >= 262)
		clm_period = CLM_PERIOD_MAX;
	else
		clm_period = clm_para->mntr_time * MS_TO_4US_RATIO;

	ccx->clm_app = clm_para->clm_app;
	phydm_clm_setting(dm, clm_period);

	return true;
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
		if (phydm_clm_mntr_set(dm, &clm_para))
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

		if (phydm_clm_mntr_set(dm, &clm_para))
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
	u8 trigger_result = 0;
#if (defined(NHM_SUPPORT) && defined(CLM_SUPPORT))
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	boolean nhm_set_ok = false;
	boolean clm_set_ok = false;

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
#endif
	return trigger_result;
}

u8 phydm_env_mntr_result(void *dm_void, struct env_mntr_rpt *rpt)
{
	u8 env_mntr_rpt = 0;
#if (defined(NHM_SUPPORT) && defined(CLM_SUPPORT))
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
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
		rpt->nhm_env_ratio = ccx->nhm_env_ratio;
		rpt->nhm_noise_pwr = ccx->nhm_level;
		rpt->nhm_pwr = ccx->nhm_pwr;
		env_mntr_rpt |= NHM_SUCCESS;

		odm_move_memory(dm, &rpt->nhm_result[0],
				&ccx->nhm_result[0], NHM_RPT_NUM);
	} else {
		rpt->nhm_ratio = ENV_MNTR_FAIL;
		rpt->nhm_env_ratio = ENV_MNTR_FAIL;
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
		  "IGI=0x%x, nhm_ratio=%d, nhm_env_ratio=%d, clm_ratio=%d, nhm_rpt_stamp=%d, clm_rpt_stamp=%d\n\n",
		  ccx->nhm_igi, rpt->nhm_ratio, rpt->nhm_env_ratio,
		  rpt->clm_ratio, rpt->nhm_rpt_stamp, rpt->clm_rpt_stamp);
#endif
	return env_mntr_rpt;
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

	#ifdef FAHM_SUPPORT
	phydm_fahm_watchdog(dm);
	#endif
#endif
}

void phydm_env_monitor_init(void *dm_void)
{
#if (defined(NHM_SUPPORT) && defined(CLM_SUPPORT))
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	phydm_ccx_hw_restart(dm);
	phydm_nhm_init(dm);
	phydm_clm_init(dm);
	#ifdef FAHM_SUPPORT
	phydm_fahm_init(dm);
	#endif
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
			 "Set Result=%d\n nhm_ratio=%d nhm_env_ratio=%d clm_ratio=%d\n nhm_rpt_stamp=%d, clm_rpt_stamp=%d,\n",
			 set_result, rpt.nhm_ratio, rpt.nhm_env_ratio,
			 rpt.clm_ratio, rpt.nhm_rpt_stamp, rpt.clm_rpt_stamp);

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

#ifdef IFS_CLM_SUPPORT
void phydm_ifs_clm_restart(void *dm_void)
			  /*Will Restart IFS CLM simultaneously*/
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 reg1 = 0;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	/*restart IFS_CLM*/
	odm_set_bb_reg(dm, R_0x1ee4, BIT(29), 0x0);
	odm_set_bb_reg(dm, R_0x1ee4, BIT(29), 0x1);
}

void phydm_ifs_clm_racing_release(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);
	PHYDM_DBG(dm, DBG_ENV_MNTR, "ifs clm lv:(%d)->(0)\n",
		  ccx->ifs_clm_set_lv);

	ccx->ifs_clm_ongoing = false;
	ccx->ifs_clm_set_lv = IFS_CLM_RELEASE;
	ccx->ifs_clm_app = IFS_CLM_BACKGROUND;
}

u8 phydm_ifs_clm_racing_ctrl(void *dm_void, enum phydm_ifs_clm_level ifs_clm_lv)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u8 set_result = PHYDM_SET_SUCCESS;
	/*acquire to control IFS CLM API*/

	PHYDM_DBG(dm, DBG_ENV_MNTR, "ifs clm_ongoing=%d, lv:(%d)->(%d)\n",
		  ccx->ifs_clm_ongoing, ccx->ifs_clm_set_lv, ifs_clm_lv);
	if (ccx->ifs_clm_ongoing) {
		if (ifs_clm_lv <= ccx->ifs_clm_set_lv) {
			set_result = PHYDM_SET_FAIL;
		} else {
			phydm_ifs_clm_restart(dm);
			ccx->ifs_clm_ongoing = false;
		}
	}

	if (set_result)
		ccx->ifs_clm_set_lv = ifs_clm_lv;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "ifs clm racing success=%d\n", set_result);
	return set_result;
}

void phydm_ifs_clm_trigger(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	/*Trigger IFS_CLM*/
	pdm_set_reg(dm, R_0x1ee4, BIT(29), 0);
	pdm_set_reg(dm, R_0x1ee4, BIT(29), 1);
	ccx->ifs_clm_trigger_time = dm->phydm_sys_up_time;
	ccx->ifs_clm_rpt_stamp++;
	ccx->ifs_clm_ongoing = true;
}

void phydm_ifs_clm_get_utility(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u32 numerator = 0;
	u16 denominator = 0;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	denominator = ccx->ifs_clm_period;
	numerator = ccx->ifs_clm_tx * 100;
	ccx->ifs_clm_tx_ratio = (u8)PHYDM_DIV(numerator, denominator);
	numerator = ccx->ifs_clm_edcca_excl_cca * 100;
	ccx->ifs_clm_edcca_excl_cca_ratio = (u8)PHYDM_DIV(numerator,
							  denominator);
	numerator = (ccx->ifs_clm_cckfa + ccx->ifs_clm_ofdmfa) * 100;
	ccx->ifs_clm_fa_ratio = (u8)PHYDM_DIV(numerator, denominator);
	numerator = (ccx->ifs_clm_cckcca_excl_fa +
		     ccx->ifs_clm_ofdmcca_excl_fa) * 100;
	ccx->ifs_clm_cca_excl_fa_ratio = (u8)PHYDM_DIV(numerator, denominator);

	PHYDM_DBG(dm, DBG_ENV_MNTR,
		  "Tx_ratio = %d, EDCCA_exclude_CCA_ratio = %d \n",
		  ccx->ifs_clm_tx_ratio, ccx->ifs_clm_edcca_excl_cca_ratio);
	PHYDM_DBG(dm, DBG_ENV_MNTR,
		  "FA_ratio = %d, CCA_exclude_FA_ratio = %d \n",
		  ccx->ifs_clm_fa_ratio, ccx->ifs_clm_cca_excl_fa_ratio);
}

void phydm_ifs_clm_get_result(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u32 value32 = 0;
	u8 i = 0;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	/*Enhance CLM result*/
	value32 = odm_get_bb_reg(dm, R_0x2e60, MASKDWORD);
	ccx->ifs_clm_tx = (u16)(value32 & MASKLWORD);
	ccx->ifs_clm_edcca_excl_cca = (u16)((value32 & MASKHWORD) >> 16);
	value32 = odm_get_bb_reg(dm, R_0x2e64, MASKDWORD);
	ccx->ifs_clm_ofdmfa = (u16)(value32 & MASKLWORD);
	ccx->ifs_clm_ofdmcca_excl_fa = (u16)((value32 & MASKHWORD) >> 16);
	value32 = odm_get_bb_reg(dm, R_0x2e68, MASKDWORD);
	ccx->ifs_clm_cckfa = (u16)(value32 & MASKLWORD);
	ccx->ifs_clm_cckcca_excl_fa = (u16)((value32 & MASKHWORD) >> 16);
	value32 = odm_get_bb_reg(dm, R_0x2e6c, MASKDWORD);
	ccx->ifs_clm_total_cca = (u16)(value32 & MASKLWORD);

	/* IFS result */
	value32 = odm_get_bb_reg(dm, R_0x2e70, MASKDWORD);
	odm_move_memory(dm, &ccx->ifs_clm_his[0], &value32, 4);
	value32 = odm_get_bb_reg(dm, R_0x2e74, MASKDWORD);
	ccx->ifs_clm_avg[0] = (u16)(value32 & MASKLWORD);
	ccx->ifs_clm_avg[1] = (u16)((value32 & MASKHWORD) >> 16);
	value32 = odm_get_bb_reg(dm, R_0x2e78, MASKDWORD);
	ccx->ifs_clm_avg[2] = (u16)(value32 & MASKLWORD);
	ccx->ifs_clm_avg[3] = (u16)((value32 & MASKHWORD) >> 16);
	value32 = odm_get_bb_reg(dm, R_0x2e7c, MASKDWORD);
	ccx->ifs_clm_avg_cca[0] = (u16)(value32 & MASKLWORD);
	ccx->ifs_clm_avg_cca[1] = (u16)((value32 & MASKHWORD) >> 16);
	value32 = odm_get_bb_reg(dm, R_0x2e80, MASKDWORD);
	ccx->ifs_clm_avg_cca[2] = (u16)(value32 & MASKLWORD);
	ccx->ifs_clm_avg_cca[3] = (u16)((value32 & MASKHWORD) >> 16);

	/* Print Result */
	PHYDM_DBG(dm, DBG_ENV_MNTR,
		  "ECLM_Rpt[%d]: \nTx = %d, EDCCA_exclude_CCA = %d \n",
		  ccx->ifs_clm_rpt_stamp, ccx->ifs_clm_tx,
		  ccx->ifs_clm_edcca_excl_cca);
	PHYDM_DBG(dm, DBG_ENV_MNTR,
		  "[FA_cnt] {CCK, OFDM} = {%d, %d}\n",
		  ccx->ifs_clm_cckfa, ccx->ifs_clm_ofdmfa);
	PHYDM_DBG(dm, DBG_ENV_MNTR,
		  "[CCA_exclude_FA_cnt] {CCK, OFDM} = {%d, %d}\n",
		  ccx->ifs_clm_cckcca_excl_fa, ccx->ifs_clm_ofdmcca_excl_fa);
	PHYDM_DBG(dm, DBG_ENV_MNTR, "CCATotal = %d\n", ccx->ifs_clm_total_cca);
	PHYDM_DBG(dm, DBG_ENV_MNTR, "Time:[his, avg, avg_cca]\n");
	for (i = 0; i < IFS_CLM_NUM; i++)
		PHYDM_DBG(dm, DBG_ENV_MNTR,
			  "T%d:[%d, %d, %d]\n", i + 1,
			  ccx->ifs_clm_his[i], ccx->ifs_clm_avg[i],
			  ccx->ifs_clm_avg_cca[i]);

	phydm_ifs_clm_racing_release(dm);

	return;
}

void phydm_ifs_clm_set_th_reg(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u8 i = 0;
	
	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	/*Set IFS period TH*/
	odm_set_bb_reg(dm, R_0x1ed4, BIT(31), ccx->ifs_clm_th_en[0]);
	odm_set_bb_reg(dm, R_0x1ed8, BIT(31), ccx->ifs_clm_th_en[1]);
	odm_set_bb_reg(dm, R_0x1edc, BIT(31), ccx->ifs_clm_th_en[2]);
	odm_set_bb_reg(dm, R_0x1ee0, BIT(31), ccx->ifs_clm_th_en[3]);
	odm_set_bb_reg(dm, R_0x1ed4, 0x7fff0000, ccx->ifs_clm_th_low[0]);
	odm_set_bb_reg(dm, R_0x1ed8, 0x7fff0000, ccx->ifs_clm_th_low[1]);
	odm_set_bb_reg(dm, R_0x1edc, 0x7fff0000, ccx->ifs_clm_th_low[2]);
	odm_set_bb_reg(dm, R_0x1ee0, 0x7fff0000, ccx->ifs_clm_th_low[3]);
	odm_set_bb_reg(dm, R_0x1ed4, MASKLWORD, ccx->ifs_clm_th_high[0]);
	odm_set_bb_reg(dm, R_0x1ed8, MASKLWORD, ccx->ifs_clm_th_high[1]);
	odm_set_bb_reg(dm, R_0x1edc, MASKLWORD, ccx->ifs_clm_th_high[2]);
	odm_set_bb_reg(dm, R_0x1ee0, MASKLWORD, ccx->ifs_clm_th_high[3]);

	for (i = 0; i < IFS_CLM_NUM; i++)
		PHYDM_DBG(dm, DBG_ENV_MNTR,
			  "Update IFS_CLM_th%d[High Low] : [%d %d]\n", i + 1,
		  	  ccx->ifs_clm_th_high[i], ccx->ifs_clm_th_low[i]);
}

boolean phydm_ifs_clm_th_update_chk(void *dm_void,
				    enum ifs_clm_application ifs_clm_app,
				    boolean *ifs_clm_th_en, u16 *ifs_clm_th_low,
				    u16 *ifs_clm_th_high, s16 th_shift)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	boolean is_update = false;
	u16 ifs_clm_th_low_bg[IFS_CLM_NUM] = {12, 5, 2, 0};
	u16 ifs_clm_th_high_bg[IFS_CLM_NUM] = {64, 12, 5, 2};
	u8 i = 0;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);
	PHYDM_DBG(dm, DBG_ENV_MNTR, "App=%d, th_shift=%d\n", ifs_clm_app,
		  th_shift);

	switch (ifs_clm_app) {
	case IFS_CLM_BACKGROUND:
	case IFS_CLM_ACS:
	case IFS_CLM_HP_TAS:
		if (ccx->ifs_clm_app != ifs_clm_app || th_shift != 0) {
			is_update = true;

			for (i = 0; i < IFS_CLM_NUM; i++) {
				ifs_clm_th_en[i] = true;
				ifs_clm_th_low[i] = ifs_clm_th_low_bg[i];
				ifs_clm_th_high[i] = ifs_clm_th_high_bg[i];
			}
		}
		break;
	case IFS_CLM_DBG:
		if (ccx->ifs_clm_app != ifs_clm_app || th_shift != 0) {
			is_update = true;

			for (i = 0; i < IFS_CLM_NUM; i++) {
				ifs_clm_th_en[i] = true;
				ifs_clm_th_low[i] = MAX_2(ccx->ifs_clm_th_low[i] +
						    th_shift, 0);
				ifs_clm_th_high[i] = MAX_2(ccx->ifs_clm_th_high[i] +
						     th_shift, 0);
			}
		}
		break;
	default:
		break;
	}

	if (is_update)
		PHYDM_DBG(dm, DBG_ENV_MNTR, "[Update IFS_TH]\n");
	else
		PHYDM_DBG(dm, DBG_ENV_MNTR, "No need to update IFS_TH\n");

	return is_update;
}

void phydm_ifs_clm_set(void *dm_void, enum ifs_clm_application ifs_clm_app,
		       u16 period, u8 ctrl_unit, s16 th_shift)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	boolean ifs_clm_th_en[IFS_CLM_NUM] =  {0};
	u16 ifs_clm_th_low[IFS_CLM_NUM] =  {0};
	u16 ifs_clm_th_high[IFS_CLM_NUM] =  {0};

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);
	PHYDM_DBG(dm, DBG_ENV_MNTR, "period=%d, ctrl_unit=%d\n", period,
		  ctrl_unit);

	/*Set Unit*/
	if (ctrl_unit != ccx->ifs_clm_ctrl_unit) {	
		odm_set_bb_reg(dm, R_0x1ee4, 0xc0000000, ctrl_unit);
		PHYDM_DBG(dm, DBG_ENV_MNTR,
			  "Update IFS_CLM unit ((%d)) -> ((%d))\n",
			  ccx->ifs_clm_ctrl_unit, ctrl_unit);
		ccx->ifs_clm_ctrl_unit = ctrl_unit;
	}

	/*Set Duration*/
	if (period != ccx->ifs_clm_period) {
		odm_set_bb_reg(dm, R_0x1eec, 0xc0000000, (period & 0x3));
		odm_set_bb_reg(dm, R_0x1ef0, 0xfe000000, ((period >> 2) &
			       0x7f));
		odm_set_bb_reg(dm, R_0x1ef4, 0xc0000000, ((period >> 9) &
			       0x3));
		odm_set_bb_reg(dm, R_0x1ef8, 0x3e000000, ((period >> 11) &
			       0x1f));
		PHYDM_DBG(dm, DBG_ENV_MNTR,
			  "Update IFS_CLM period ((%d)) -> ((%d))\n",
			  ccx->ifs_clm_period, period);
		ccx->ifs_clm_period = period;
	}

	/*Set IFS CLM threshold*/
	if (phydm_ifs_clm_th_update_chk(dm, ifs_clm_app, &ifs_clm_th_en[0],
					&ifs_clm_th_low[0], &ifs_clm_th_high[0],
					th_shift)) {

		ccx->ifs_clm_app = ifs_clm_app;
		odm_move_memory(dm, &ccx->ifs_clm_th_en[0], &ifs_clm_th_en,
				IFS_CLM_NUM);
		odm_move_memory(dm, &ccx->ifs_clm_th_low[0], &ifs_clm_th_low,
				IFS_CLM_NUM);
		odm_move_memory(dm, &ccx->ifs_clm_th_high[0], &ifs_clm_th_high,
				IFS_CLM_NUM);

		phydm_ifs_clm_set_th_reg(dm);
	}
}

boolean
phydm_ifs_clm_mntr_set(void *dm_void, struct ifs_clm_para_info *ifs_clm_para)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u16 ifs_clm_time = 0; /*unit: 4/8/12/16us*/
	u8 unit = 0;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	if (ifs_clm_para->mntr_time == 0)
		return false;

	if (ifs_clm_para->ifs_clm_lv >= IFS_CLM_MAX_NUM) {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "Wrong LV=%d\n",
			  ifs_clm_para->ifs_clm_lv);
		return false;
	}

	if (phydm_ifs_clm_racing_ctrl(dm, ifs_clm_para->ifs_clm_lv) == PHYDM_SET_FAIL)
		return false;

	if (ifs_clm_para->mntr_time >= 1048) {
		unit = IFS_CLM_16;
		ifs_clm_time = IFS_CLM_PERIOD_MAX; /*65535 * 16us = 1048ms*/
	} else if (ifs_clm_para->mntr_time >= 786) {/*65535 * 12us = 786 ms*/
		unit = IFS_CLM_16;
		ifs_clm_time = PHYDM_DIV(ifs_clm_para->mntr_time * MS_TO_US, 16);
	} else if (ifs_clm_para->mntr_time >= 524) {
		unit = IFS_CLM_12;
		ifs_clm_time = PHYDM_DIV(ifs_clm_para->mntr_time * MS_TO_US, 12);
	} else if (ifs_clm_para->mntr_time >= 262) {
		unit = IFS_CLM_8;
		ifs_clm_time = PHYDM_DIV(ifs_clm_para->mntr_time * MS_TO_US, 8);
	} else {
		unit = IFS_CLM_4;
		ifs_clm_time = PHYDM_DIV(ifs_clm_para->mntr_time * MS_TO_US, 4);
	}

	phydm_ifs_clm_set(dm, ifs_clm_para->ifs_clm_app, ifs_clm_time, unit,
			  ifs_clm_para->th_shift);

	return true;
}

boolean
phydm_ifs_clm_mntr_chk(void *dm_void, u16 monitor_time /*unit ms*/)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	struct ifs_clm_para_info ifs_clm_para = {0};
	boolean ifs_clm_chk_result = false;
	u32 sys_return_time = 0;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	if (ccx->ifs_clm_manual_ctrl) {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "IFS CLM in manual ctrl\n");
		return ifs_clm_chk_result;
	}

	sys_return_time = ccx->ifs_clm_trigger_time + MAX_ENV_MNTR_TIME;
	if (ccx->ifs_clm_app != IFS_CLM_BACKGROUND &&
	    (sys_return_time > dm->phydm_sys_up_time)) {
		PHYDM_DBG(dm, DBG_ENV_MNTR,
			  "ifs_clm_app=%d, trigger_time %d, sys_time=%d\n",
			  ccx->ifs_clm_app, ccx->ifs_clm_trigger_time,
			  dm->phydm_sys_up_time);

		return ifs_clm_chk_result;
	}

	/*[IFS CLM get result ------------------------------------]*/
	phydm_ifs_clm_get_result(dm);
	phydm_ifs_clm_get_utility(dm);

	/*[IFS CLM trigger setting]------------------------------------------*/
	ifs_clm_para.ifs_clm_app = IFS_CLM_BACKGROUND;
	ifs_clm_para.ifs_clm_lv = IFS_CLM_LV_1;
	ifs_clm_para.mntr_time = monitor_time;
	ifs_clm_para.th_shift = 0;

	ifs_clm_chk_result = phydm_ifs_clm_mntr_set(dm, &ifs_clm_para);

	return ifs_clm_chk_result;
}

void phydm_ifs_clm_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);

	ccx->ifs_clm_app = IFS_CLM_BACKGROUND;

	/*Set IFS threshold*/
	ccx->ifs_clm_ongoing = false;
	ccx->ifs_clm_set_lv = IFS_CLM_RELEASE;

	if (phydm_ifs_clm_th_update_chk(dm, ccx->ifs_clm_app,
					&ccx->ifs_clm_th_en[0],
					&ccx->ifs_clm_th_low[0],
					&ccx->ifs_clm_th_high[0], 0xffff))
		phydm_ifs_clm_set_th_reg(dm);

	ccx->ifs_clm_period = 0;
	ccx->ifs_clm_ctrl_unit = IFS_CLM_INIT;
	ccx->ifs_clm_manual_ctrl = 0;
	ccx->ifs_clm_rpt_stamp = 0;
}

void phydm_ifs_clm_dbg(void *dm_void, char input[][16], u32 *_used,
		       char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	struct ifs_clm_para_info ifs_clm_para;
	char help[] = "-h";
	u32 var1[10] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;
	u8 result_tmp = 0;
	u8 i = 0;
	u16 th_shift = 0;

	if (!(dm->support_ic_type & PHYDM_IC_SUPPORT_IFS_CLM))
		return;

	for (i = 0; i < 5; i++) {
		if (input[i + 1])
			PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL,
				     &var1[i]);
	}

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "IFS_CLM Basic-Trigger 960ms: {1}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "IFS_CLM Adv-Trigger: {2} {App:3 for dbg} {LV:1~4} {0~2096ms} {th_shift}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "IFS_CLM Get Result: {100}\n");
	} else if (var1[0] == 100) { /*Get IFS_CLM results*/
		phydm_ifs_clm_get_result(dm);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			  "ECLM_Rpt[%d]: \nTx = %d \nEDCCA_exclude_CCA = %d\n",
			  ccx->ifs_clm_rpt_stamp, ccx->ifs_clm_tx,
			  ccx->ifs_clm_edcca_excl_cca);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			  "[FA_cnt] {CCK, OFDM} = {%d, %d}\n",
			  ccx->ifs_clm_cckfa, ccx->ifs_clm_ofdmfa);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			  "[CCA_exclude_FA_cnt] {CCK, OFDM} = {%d, %d}\n",
			  ccx->ifs_clm_cckcca_excl_fa,
			  ccx->ifs_clm_ofdmcca_excl_fa);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "CCATotal = %d\n", ccx->ifs_clm_total_cca);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Time:[his, avg, avg_cca]\n");
		for (i = 0; i < IFS_CLM_NUM; i++)
			PDM_SNPF(out_len, used, output + used, out_len - used,
				  "T%d:[%d, %d, %d]\n", i + 1,
				  ccx->ifs_clm_his[i], ccx->ifs_clm_avg[i],
				  ccx->ifs_clm_avg_cca[i]);

		phydm_ifs_clm_get_utility(dm);

		ccx->ifs_clm_manual_ctrl = 0;
	} else { /*IFS_CLM trigger*/
		ccx->ifs_clm_manual_ctrl = 1;

		if (var1[0] == 1) {
			ifs_clm_para.ifs_clm_app = IFS_CLM_DBG;
			ifs_clm_para.ifs_clm_lv = IFS_CLM_LV_4;
			ifs_clm_para.mntr_time = 960;
			ifs_clm_para.th_shift = 0;
		} else {
			ifs_clm_para.ifs_clm_app = (enum ifs_clm_application)var1[1];
			ifs_clm_para.ifs_clm_lv = (enum phydm_ifs_clm_level)var1[2];
			ifs_clm_para.mntr_time = (u16)var1[3];
			ifs_clm_para.th_shift = (s16)var1[4];
		}

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "app=%d, lv=%d, time=%d ms, th_shift=%s%d\n",
			 ifs_clm_para.ifs_clm_app, ifs_clm_para.ifs_clm_lv,
			 ifs_clm_para.mntr_time,
			 (ifs_clm_para.th_shift > 0) ? "+" : "-",
			 ifs_clm_para.th_shift);

		if (phydm_ifs_clm_mntr_set(dm, &ifs_clm_para) == PHYDM_SET_SUCCESS)
			phydm_ifs_clm_trigger(dm);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "rpt_stamp=%d\n", ccx->ifs_clm_rpt_stamp);
		for (i = 0; i < IFS_CLM_NUM; i++)
			PDM_SNPF(out_len, used, output + used, out_len - used,
				  "IFS_CLM_th%d[High Low] : [%d %d]\n", i + 1,
			  	  ccx->ifs_clm_th_high[i],
			  	  ccx->ifs_clm_th_low[i]);
	}

	*_used = used;
	*_out_len = out_len;
}
#endif

u8 phydm_enhance_mntr_trigger(void *dm_void, struct nhm_para_info *nhm_para,
			     struct clm_para_info *clm_para,
			     struct fahm_para_info *fahm_para,
			     struct ifs_clm_para_info *ifs_clm_para,
			     struct enhance_mntr_trig_rpt *trig_rpt)
{
	u8 trigger_result = 0;
#if (defined(NHM_SUPPORT) && defined(CLM_SUPPORT) && defined(FAHM_SUPPORT) && defined(IFS_CLM_SUPPORT))
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	boolean nhm_set_ok = false;
	boolean clm_set_ok = false;
	boolean fahm_set_ok = false;
	boolean ifs_clm_set_ok = false;

	if (!(dm->support_ic_type & PHYDM_IC_SUPPORT_FAHM) ||
	    !(dm->support_ic_type & PHYDM_IC_SUPPORT_IFS_CLM))
		return trigger_result;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s] ======>\n", __func__);

	nhm_set_ok = phydm_nhm_mntr_set(dm, nhm_para);

	if (ccx->clm_mntr_mode == CLM_DRIVER_MNTR) {
		clm_set_ok = phydm_clm_mntr_set(dm, clm_para);
	} else if (ccx->clm_mntr_mode == CLM_FW_MNTR) {
		phydm_clm_h2c(dm, CLM_PERIOD_MAX, true);
		trigger_result |= CLM_SUCCESS;
	}

	fahm_set_ok = phydm_fahm_mntr_set(dm, fahm_para);

	ifs_clm_set_ok = phydm_ifs_clm_mntr_set(dm, ifs_clm_para);

	if (nhm_set_ok) {
		phydm_nhm_trigger(dm);
		trigger_result |= NHM_SUCCESS;
	}

	if (clm_set_ok) {
		phydm_clm_trigger(dm);
		trigger_result |= CLM_SUCCESS;
	}

	if (fahm_set_ok) {
		phydm_fahm_trigger(dm);
		trigger_result |= FAHM_SUCCESS;
	}

	if (ifs_clm_set_ok) {
		phydm_ifs_clm_trigger(dm);
		trigger_result |= IFS_CLM_SUCCESS;
	}

	/*monitor for the test duration*/
	ccx->start_time = odm_get_current_time(dm);

	trig_rpt->nhm_rpt_stamp = ccx->nhm_rpt_stamp;
	trig_rpt->clm_rpt_stamp = ccx->clm_rpt_stamp;
	trig_rpt->fahm_rpt_stamp = ccx->fahm_rpt_stamp;
	trig_rpt->ifs_clm_rpt_stamp = ccx->ifs_clm_rpt_stamp;

	PHYDM_DBG(dm, DBG_ENV_MNTR,
		  "rpt_stamp{NHM, CLM, FAHM, IFS_CLM}={%d, %d, %d, %d}\n\n",
		  trig_rpt->nhm_rpt_stamp, trig_rpt->clm_rpt_stamp,
		  trig_rpt->fahm_rpt_stamp, trig_rpt->ifs_clm_rpt_stamp);

#endif
	return trigger_result;
}

u8 phydm_enhance_mntr_result(void *dm_void, struct enhance_mntr_rpt *rpt)
{
	u8 enhance_mntr_rpt = 0;
#if (defined(NHM_SUPPORT) && defined(CLM_SUPPORT) && defined(FAHM_SUPPORT) && defined(IFS_CLM_SUPPORT))
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u64 progressing_time = 0;
	u32 val_tmp = 0;

	if (!(dm->support_ic_type & PHYDM_IC_SUPPORT_FAHM) ||
	    !(dm->support_ic_type & PHYDM_IC_SUPPORT_IFS_CLM))
		return enhance_mntr_rpt;

	/*monitor for the test duration*/
	progressing_time = odm_get_progressing_time(dm, ccx->start_time);
	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s] ======>\n", __func__);
	PHYDM_DBG(dm, DBG_ENV_MNTR, "enhance_mntr_time=%lld\n",
		  progressing_time);

	/*Get NHM result*/
	if (phydm_nhm_get_result(dm)) {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "Get NHM_rpt success\n");
		phydm_nhm_get_utility(dm);
		rpt->nhm_ratio = ccx->nhm_ratio;
		rpt->nhm_env_ratio = ccx->nhm_env_ratio;
		rpt->nhm_noise_pwr = ccx->nhm_level;
		rpt->nhm_pwr = ccx->nhm_pwr;
		enhance_mntr_rpt |= NHM_SUCCESS;

		odm_move_memory(dm, &rpt->nhm_result[0],
				&ccx->nhm_result[0], NHM_RPT_NUM);
	} else {
		rpt->nhm_ratio = ENV_MNTR_FAIL;
		rpt->nhm_env_ratio = ENV_MNTR_FAIL;
	}

	PHYDM_DBG(dm, DBG_ENV_MNTR,
		  "[NHM]rpt_stamp=%d, IGI=0x%x, ratio=%d, env_ratio=%d, noise_pwr=%d, pwr=%d\n",
		  rpt->nhm_rpt_stamp, ccx->nhm_igi, rpt->nhm_ratio,
		  rpt->nhm_env_ratio, rpt->nhm_noise_pwr, rpt->nhm_pwr);

	/*Get CLM result*/
	if (ccx->clm_mntr_mode == CLM_DRIVER_MNTR) {
		if (phydm_clm_get_result(dm)) {
			PHYDM_DBG(dm, DBG_ENV_MNTR, "Get CLM_rpt success\n");
			phydm_clm_get_utility(dm);
			enhance_mntr_rpt |= CLM_SUCCESS;
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
		enhance_mntr_rpt |= CLM_SUCCESS;
	}

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[CLM]rpt_stamp=%d, ratio=%d\n",
		  rpt->clm_rpt_stamp, rpt->clm_ratio);

	/*Get FAHM result*/
	if (phydm_fahm_get_result(dm)) {
		PHYDM_DBG(dm, DBG_ENV_MNTR, "Get FAHM_rpt success\n");
		phydm_fahm_get_utility(dm);
		rpt->fahm_pwr = ccx->fahm_pwr;
		enhance_mntr_rpt |= FAHM_SUCCESS;

		odm_move_memory(dm, &rpt->fahm_result[0],
				&ccx->fahm_result[0], NHM_RPT_NUM * 2);
	} else {
		rpt->fahm_pwr = 0;
	}

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[FAHM]rpt_stamp=%d, IGI=0x%x, pwr=%d\n",
		  rpt->fahm_rpt_stamp, ccx->fahm_igi, rpt->fahm_pwr);

	/*Get IFS_CLM result*/
	phydm_ifs_clm_get_result(dm);
	phydm_ifs_clm_get_utility(dm);
	rpt->ifs_clm_tx_ratio = ccx->ifs_clm_tx_ratio;
	rpt->ifs_clm_edcca_excl_cca_ratio = ccx->ifs_clm_edcca_excl_cca_ratio;
	rpt->ifs_clm_fa_ratio = ccx->ifs_clm_fa_ratio;
	rpt->ifs_clm_cca_excl_fa_ratio = ccx->ifs_clm_cca_excl_fa_ratio;
	rpt->ifs_clm_rpt_stamp = ccx->ifs_clm_rpt_stamp;
	enhance_mntr_rpt |= IFS_CLM_SUCCESS;

	PHYDM_DBG(dm, DBG_ENV_MNTR,
		  "[IFS_CLM]rpt_stamp = %d, Tx_ratio = %d, EDCCA_exclude_CCA_ratio = %d\n",
		  ccx->ifs_clm_rpt_stamp, ccx->ifs_clm_tx_ratio,
		  ccx->ifs_clm_edcca_excl_cca_ratio);	

	PHYDM_DBG(dm, DBG_ENV_MNTR,
		  "FA_ratio = %d, CCA_exclude_FA_ratio = %d\n",
		  ccx->ifs_clm_fa_ratio, ccx->ifs_clm_cca_excl_fa_ratio);

	rpt->nhm_rpt_stamp = ccx->nhm_rpt_stamp;
	rpt->clm_rpt_stamp = ccx->clm_rpt_stamp;
	rpt->fahm_rpt_stamp = ccx->fahm_rpt_stamp;
	rpt->ifs_clm_rpt_stamp = ccx->ifs_clm_rpt_stamp;
#endif
	return enhance_mntr_rpt;
}

void phydm_enhance_mntr_watchdog(void *dm_void)
{
#ifdef IFS_CLM_SUPPORT
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct ccx_info *ccx = &dm->dm_ccx_info;
	boolean ifs_clm_chk_ok = false;

	if (!(dm->support_ability & ODM_BB_ENV_MONITOR))
		return;

	if (!(dm->support_ic_type & PHYDM_IC_SUPPORT_IFS_CLM))
		return;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);
	ifs_clm_chk_ok = phydm_ifs_clm_mntr_chk(dm, 960); /*monitor 960ms*/

	if (ifs_clm_chk_ok)
		phydm_ifs_clm_trigger(dm);
#endif
}

void phydm_enhance_monitor_init(void *dm_void)
{
#ifdef IFS_CLM_SUPPORT
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (!(dm->support_ic_type & PHYDM_IC_SUPPORT_IFS_CLM))
		return;

	PHYDM_DBG(dm, DBG_ENV_MNTR, "[%s]===>\n", __func__);
	phydm_ifs_clm_restart(dm);
	phydm_ifs_clm_init(dm);
#endif
}

void phydm_enhance_mntr_dbg(void *dm_void, char input[][16], u32 *_used,
			    char *output, u32 *_out_len)
{
#if (defined(NHM_SUPPORT) && defined(CLM_SUPPORT) && defined(FAHM_SUPPORT) && defined(IFS_CLM_SUPPORT))
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	char help[] = "-h";
	u32 var1[10] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;
	struct nhm_para_info nhm_para = {0};
	struct clm_para_info clm_para = {0};
	struct fahm_para_info fahm_para = {0};
	struct ifs_clm_para_info ifs_clm_para = {0};
	struct enhance_mntr_rpt rpt = {0};
	struct enhance_mntr_trig_rpt trig_rpt = {0};
	struct ccx_info *ccx = &dm->dm_ccx_info;
	u8 set_result = 0;
	u8 i = 0;

	if (!(dm->support_ic_type & PHYDM_IC_SUPPORT_FAHM) ||
	    !(dm->support_ic_type & PHYDM_IC_SUPPORT_IFS_CLM))
		return;

	PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Basic-Trigger 960ms for ifs_clm, 262ms for others: {1}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Get Result: {100}\n");
	} else if (var1[0] == 100) { /* Get results */
		set_result = phydm_enhance_mntr_result(dm, &rpt);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Set Result=%d, rpt_stamp{NHM, CLM, FAHM, IFS_CLM}={%d, %d, %d, %d}\n",
			 set_result, rpt.nhm_rpt_stamp, rpt.clm_rpt_stamp,
			 rpt.fahm_rpt_stamp, rpt.ifs_clm_rpt_stamp);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "nhm_IGI=0x%x, nhm_ratio=%d, ,nhm_env_ratio=%d, noise_pwr=%d, pwr=%d\n",
			 ccx->nhm_igi, rpt.nhm_ratio, rpt.nhm_env_ratio,
			 rpt.nhm_noise_pwr, rpt.nhm_pwr);

		for (i = 0; i <= 11; i++) {
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "nhm_rpt[%d] = %d (%d percent)\n", i,
				 rpt.nhm_result[i],
				 (((rpt.nhm_result[i] * 100) + 128) >> 8));
		}

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "clm_ratio=%d, fahm_IGI=0x%x, fahm_pwr=%d\n",
			 rpt.clm_ratio, ccx->fahm_igi, rpt.fahm_pwr);

		for (i = 0; i <= 11; i++) {
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "fahm_rpt[%d] = %d (%d percent)\n", i,
				 rpt.fahm_result[i],
				 (((rpt.fahm_result[i] * 100) + 32768) >> 16));
		}

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "ifs_clm_Tx_ratio = %d, ifs_clm_EDCCA_exclude_CCA_ratio = %d \n",
			 rpt.ifs_clm_tx_ratio,
			 rpt.ifs_clm_edcca_excl_cca_ratio);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "ifs_clm_FA_ratio = %d, ifs_clm_CCA_exclude_FA_ratio = %d \n",
			 rpt.ifs_clm_fa_ratio, rpt.ifs_clm_cca_excl_fa_ratio);
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

		/*fahm para*/
		fahm_para.incld_fa = FAHM_INCLUDE_FA;
		fahm_para.incld_crc32_ok = FAHM_EXCLUDE_CRC32_OK;
		fahm_para.incld_crc32_err = FAHM_EXCLUDE_CRC32_ERR;
		fahm_para.app = FAHM_ACS;
		fahm_para.lv = FAHM_LV_2;
		fahm_para.mntr_time = 262;
		fahm_para.en_1db_mode = false;

		ifs_clm_para.ifs_clm_app = IFS_CLM_ACS;
		ifs_clm_para.ifs_clm_lv = IFS_CLM_LV_2;
		ifs_clm_para.mntr_time = 960;
		ifs_clm_para.th_shift = 0;

		set_result = phydm_enhance_mntr_trigger(dm, &nhm_para,
							&clm_para, &fahm_para,
							&ifs_clm_para,
							&trig_rpt);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Set Result=%d, rpt_stamp{NHM, CLM, FAHM, IFS_CLM}={%d, %d ,%d, %d}\n",
			 set_result, trig_rpt.nhm_rpt_stamp,
			 trig_rpt.clm_rpt_stamp, trig_rpt.fahm_rpt_stamp,
			 trig_rpt.ifs_clm_rpt_stamp);
	}
	*_used = used;
	*_out_len = out_len;
#endif
}

