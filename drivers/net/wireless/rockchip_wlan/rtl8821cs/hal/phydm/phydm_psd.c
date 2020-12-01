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

/******************************************************************************
 * include files
 *****************************************************************************/
#include "mp_precomp.h"
#include "phydm_precomp.h"

#ifdef CONFIG_PSD_TOOL
u32 phydm_get_psd_data(void *dm_void, u32 psd_tone_idx, u32 igi)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct psd_info *dm_psd_table = &dm->dm_psd_table;
	u32 psd_report = 0;

	if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		#if(RTL8723F_SUPPORT)
		if (dm->support_ic_type & (ODM_RTL8723F)) {
			odm_set_bb_reg(dm, dm_psd_table->psd_reg, 0x3ff80000, psd_tone_idx & 0x7ff);
			/*PSD trigger start*/
			odm_set_bb_reg(dm, dm_psd_table->psd_reg, BIT(16), 1);
			ODM_delay_us(10 << (dm_psd_table->fft_smp_point >> 7));
			/*PSD trigger stop*/
			odm_set_bb_reg(dm, dm_psd_table->psd_reg, BIT(16), 0);
		}
		#endif
		#if 0
		odm_set_bb_reg(dm, R_0x1e8c, 0x3ff, psd_tone_idx & 0x3ff);
		odm_set_bb_reg(dm, R_0x1e88, BIT(27) | BIT(26),
			       psd_tone_idx >> 10);
		/*PSD trigger start*/
		odm_set_bb_reg(dm, dm_psd_table->psd_reg, BIT(18), 1);
		ODM_delay_us(10 << (dm_psd_table->fft_smp_point >> 7));
		/*PSD trigger stop*/
		odm_set_bb_reg(dm, dm_psd_table->psd_reg, BIT(18), 0);
		#endif
	} else if (dm->support_ic_type & (ODM_RTL8721D |
				ODM_RTL8710C)) {
		odm_set_bb_reg(dm, dm_psd_table->psd_reg, 0xfff, psd_tone_idx);
		odm_set_bb_reg(dm, dm_psd_table->psd_reg, BIT(28), 1);
		/*PSD trigger start*/
		ODM_delay_us(10 << (dm_psd_table->fft_smp_point >> 7));
		odm_set_bb_reg(dm, dm_psd_table->psd_reg, BIT(28), 0);
		/*PSD trigger stop*/
	} else {
		odm_set_bb_reg(dm, dm_psd_table->psd_reg, 0x3ff, psd_tone_idx);
		/*PSD trigger start*/
		odm_set_bb_reg(dm, dm_psd_table->psd_reg, BIT(22), 1);
		ODM_delay_us(10 << (dm_psd_table->fft_smp_point >> 7));
		/*PSD trigger stop*/
		odm_set_bb_reg(dm, dm_psd_table->psd_reg, BIT(22), 0);
	}

	/*Get PSD Report*/
	if (dm->support_ic_type & (ODM_RTL8821C | ODM_RTL8721D |
				ODM_RTL8710C)) {
		psd_report = odm_get_bb_reg(dm, dm_psd_table->psd_report_reg,
					    0xffffff);
		psd_report = psd_report >> 5;
	} else if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		#if(RTL8723F_SUPPORT)
		if (dm->support_ic_type & (ODM_RTL8723F)) {
			psd_report = odm_get_bb_reg(dm, dm_psd_table->psd_report_reg,
					   	 0x1ffffff);
		}
		#endif
		#if 0
		psd_report = odm_get_bb_reg(dm, dm_psd_table->psd_report_reg,
					    0xffffff);
		#endif
	} else {
		psd_report = odm_get_bb_reg(dm, dm_psd_table->psd_report_reg,
					    0xffff);
	}
	psd_report = odm_convert_to_db((u64)psd_report) + igi;

	return psd_report;
}

u8 psd_result_cali_tone_8821[7] = {21, 28, 33, 93, 98, 105, 127};
u8 psd_result_cali_val_8821[7] = {67, 69, 71, 72, 71, 69, 67};

u8 phydm_psd(void *dm_void, u32 igi, u16 start_point, u16 stop_point)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct psd_info *dm_psd_table = &dm->dm_psd_table;
	u32 i = 0, mod_tone_idx = 0;
	u32 t = 0;
	u16 fft_max_half_bw = 0;
	u16 psd_fc_channel = dm_psd_table->psd_fc_channel;
	u8 ag_rf_mode_reg = 0;
	u8 is_5G = 0;
	u32 psd_result_tmp = 0;
	u8 psd_result = 0;
	u8 psd_result_cali_tone[7] = {0};
	u8 psd_result_cali_val[7] = {0};
	u8 noise_idx = 0;
	u8 set_result = 0;
	u32 igi_tmp = 0x6e;

	if (dm->support_ic_type == ODM_RTL8821) {
		odm_move_memory(dm, psd_result_cali_tone,
				psd_result_cali_tone_8821, 7);
		odm_move_memory(dm, psd_result_cali_val,
				psd_result_cali_val_8821, 7);
	}

	dm_psd_table->psd_in_progress = 1;

	PHYDM_DBG(dm, ODM_COMP_API, "PSD Start =>\n");

	/* @[Stop DIG]*/
	/* @IGI target at 0dBm & make it can't CCA*/
	if (phydm_pause_func(dm, F00_DIG, PHYDM_PAUSE, PHYDM_PAUSE_LEVEL_3, 1,
			     &igi_tmp) == PAUSE_FAIL) {
		return PHYDM_SET_FAIL;
	}

	ODM_delay_us(10);

	if (phydm_stop_ic_trx(dm, PHYDM_SET) == PHYDM_SET_FAIL) {
		phydm_pause_func(dm, F00_DIG, PHYDM_RESUME, PHYDM_PAUSE_LEVEL_3,
				 1, &igi_tmp);
		return PHYDM_SET_FAIL;
	}

	/* @[Set IGI]*/
	phydm_write_dig_reg(dm, (u8)igi);

	/* @[Backup RF Reg]*/
	dm_psd_table->rf_0x18_bkp = odm_get_rf_reg(dm, RF_PATH_A, RF_0x18,
						   RFREG_MASK);
	dm_psd_table->rf_0x18_bkp_b = odm_get_rf_reg(dm, RF_PATH_B, RF_0x18,
						     RFREG_MASK);

	if (psd_fc_channel > 14) {
		is_5G = 1;
		if (dm->support_ic_type & (ODM_RTL8822C | ODM_RTL8812F |
					   ODM_RTL8197G)) {
		#if 0
			if (psd_fc_channel < 80)
				ag_rf_mode_reg = 0x1;
			else if (psd_fc_channel >= 80 && psd_fc_channel <= 140)
				ag_rf_mode_reg = 0x3;
			else if (psd_fc_channel > 140)
				ag_rf_mode_reg = 0x5;
		#endif
		} else if (dm->support_ic_type & ODM_RTL8723F) {
				if (psd_fc_channel < 80)
					ag_rf_mode_reg = 0x1;
				else if (psd_fc_channel >= 80 && psd_fc_channel <= 144)
					ag_rf_mode_reg = 0x5;
				else if (psd_fc_channel > 144)
					ag_rf_mode_reg = 0x9;
		} else if (dm->support_ic_type == ODM_RTL8721D) {
			if (psd_fc_channel >= 36 && psd_fc_channel <= 64)
				ag_rf_mode_reg = 0x1;
			else if (psd_fc_channel >= 100 && psd_fc_channel <= 140)
				ag_rf_mode_reg = 0x5;
			else if (psd_fc_channel > 140)
				ag_rf_mode_reg = 0x9;
		} else {
			if (psd_fc_channel >= 36 && psd_fc_channel <= 64)
				ag_rf_mode_reg = 0x1;
			else if (psd_fc_channel >= 100 && psd_fc_channel <= 140)
				ag_rf_mode_reg = 0x3;
			else if (psd_fc_channel > 140)
				ag_rf_mode_reg = 0x5;
		}
	}

	/* Set RF fc*/
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x18, 0xff, psd_fc_channel);
	odm_set_rf_reg(dm, RF_PATH_B, RF_0x18, 0xff, psd_fc_channel);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x18, 0x300, is_5G);
	odm_set_rf_reg(dm, RF_PATH_B, RF_0x18, 0x300, is_5G);
	if (dm->support_ic_type & (ODM_RTL8822C | ODM_RTL8812F |
				   ODM_RTL8197G)) {
		#if 0
		/* @2b'11: 20MHz, 2b'10: 40MHz, 2b'01: 80MHz */
		odm_set_rf_reg(dm, RF_PATH_A, RF_0x18, 0x3000,
			       dm_psd_table->psd_bw_rf_reg);
		odm_set_rf_reg(dm, RF_PATH_B, RF_0x18, 0x3000,
			       dm_psd_table->psd_bw_rf_reg);
		/* Set RF ag fc mode*/
		odm_set_rf_reg(dm, RF_PATH_A, RF_0x18, 0x70000,
			       ag_rf_mode_reg);
		odm_set_rf_reg(dm, RF_PATH_B, RF_0x18, 0x70000,
			       ag_rf_mode_reg);
		#endif
	} else if (dm->support_ic_type & ODM_RTL8723F) {
			/* @2b'11: 20MHz, 2b'10: 40MHz, 2b'01: 80MHz */
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x18, 0x1c00,
				       dm_psd_table->psd_bw_rf_reg);
			odm_set_rf_reg(dm, RF_PATH_B, RF_0x18, 0x1c00,
				       dm_psd_table->psd_bw_rf_reg);
			/* Set RF ag fc mode*/
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x18, 0x30000,	1);
			odm_set_rf_reg(dm, RF_PATH_B, RF_0x18, 0x30000,	1);
			if(ag_rf_mode_reg == 1) {
				odm_set_rf_reg(dm, RF_PATH_A, RF_0x19, 0xc0000, 0);
				odm_set_rf_reg(dm, RF_PATH_B, RF_0x19, 0xc0000, 0);
			}
			else if(ag_rf_mode_reg == 5){
				odm_set_rf_reg(dm, RF_PATH_A, RF_0x19, 0xc0000, 1);
				odm_set_rf_reg(dm, RF_PATH_B, RF_0x19, 0xc0000, 1);
			}
			else {
				odm_set_rf_reg(dm, RF_PATH_A, RF_0x19, 0xc0000, 2);	
				odm_set_rf_reg(dm, RF_PATH_B, RF_0x19, 0xc0000, 2);	
			}
	} else {
		/* @2b'11: 20MHz, 2b'10: 40MHz, 2b'01: 80MHz */
		if (dm->support_ic_type == ODM_RTL8721D) {
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x18, 0x1c00,
				       dm_psd_table->psd_bw_rf_reg);
#if (RTL8710C_SUPPORT == 1)
		} else if (dm->support_ic_type == ODM_RTL8710C) {
			odm_set_rf_reg(dm, RF_PATH_A,
							RF_0x18, 0x1c00,
						dm_psd_table->psd_bw_rf_reg);
#endif
		} else {
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x18, 0xc00,
				       dm_psd_table->psd_bw_rf_reg);
		}
			odm_set_rf_reg(dm, RF_PATH_B, RF_0x18, 0xc00,
				       dm_psd_table->psd_bw_rf_reg);
			/* Set RF ag fc mode*/
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x18, 0xf0000,
				       ag_rf_mode_reg);
			odm_set_rf_reg(dm, RF_PATH_B, RF_0x18, 0xf0000,
				       ag_rf_mode_reg);
	}

	if (dm->support_ic_type & ODM_IC_JGR3_SERIES){
		if (dm->support_ic_type & ODM_RTL8723F) {
		PHYDM_DBG(dm, ODM_COMP_API, "0x1d70=((0x%x))\n",
			  odm_get_bb_reg(dm, R_0x1d70, MASKDWORD));
		PHYDM_DBG(dm, ODM_COMP_API, "RF0x19=((0x%x))\n",
			 odm_get_rf_reg(dm, RF_PATH_A, RF_0x19, RFREG_MASK));
		}
	} else
		PHYDM_DBG(dm, ODM_COMP_API, "0xc50=((0x%x))\n",
			  odm_get_bb_reg(dm, R_0xc50, MASKDWORD));

	PHYDM_DBG(dm, ODM_COMP_API, "RF0x18=((0x%x))\n",
		  odm_get_rf_reg(dm, RF_PATH_A, RF_0x18, RFREG_MASK));

	/* @[Stop 3-wires]*/
	phydm_stop_3_wire(dm, PHYDM_SET);

	ODM_delay_us(10);

	if (stop_point > (dm_psd_table->fft_smp_point - 1))
		stop_point = (dm_psd_table->fft_smp_point - 1);

	if (start_point > (dm_psd_table->fft_smp_point - 1))
		start_point = (dm_psd_table->fft_smp_point - 1);

	if (start_point > stop_point)
		stop_point = start_point;

	for (i = start_point; i <= stop_point; i++) {
		fft_max_half_bw = (dm_psd_table->fft_smp_point) >> 1;

		if (i < fft_max_half_bw)
			mod_tone_idx = i + fft_max_half_bw;
		else
			mod_tone_idx = i - fft_max_half_bw;

		psd_result_tmp = 0;
		for (t = 0; t < dm_psd_table->sw_avg_time; t++)
			psd_result_tmp += phydm_get_psd_data(dm, mod_tone_idx,
							     igi);
		psd_result =
			(u8)((psd_result_tmp / dm_psd_table->sw_avg_time)) -
			dm_psd_table->psd_pwr_common_offset;

		if (dm_psd_table->fft_smp_point == 128 &&
		    dm_psd_table->noise_k_en) {
			if (i > psd_result_cali_tone[noise_idx])
				noise_idx++;

			if (noise_idx > 6)
				noise_idx = 6;

			if (psd_result >= psd_result_cali_val[noise_idx])
				psd_result = psd_result -
					     psd_result_cali_val[noise_idx];
			else
				psd_result = 0;

			dm_psd_table->psd_result[i] = psd_result;
		}

		PHYDM_DBG(dm, ODM_COMP_API, "[%d] N_cali = %d, PSD = %d\n",
			  mod_tone_idx, psd_result_cali_val[noise_idx],
			  psd_result);
	}

	/*@[Start 3-wires]*/
	phydm_stop_3_wire(dm, PHYDM_REVERT);

	ODM_delay_us(10);

	/*@[Revert Reg]*/
	set_result = phydm_stop_ic_trx(dm, PHYDM_REVERT);

	odm_set_rf_reg(dm, RF_PATH_A, RF_0x18, RFREG_MASK,
		       dm_psd_table->rf_0x18_bkp);
	odm_set_rf_reg(dm, RF_PATH_B, RF_0x18, RFREG_MASK,
		       dm_psd_table->rf_0x18_bkp_b);

	PHYDM_DBG(dm, ODM_COMP_API, "PSD finished\n\n");

	phydm_pause_func(dm, F00_DIG, PHYDM_RESUME, PHYDM_PAUSE_LEVEL_3, 1,
			 &igi_tmp);
	dm_psd_table->psd_in_progress = 0;

	return PHYDM_SET_SUCCESS;
}

void phydm_psd_para_setting(void *dm_void, u8 sw_avg_time, u8 hw_avg_time,
			    u8 i_q_setting, u16 fft_smp_point, u8 ant_sel,
			    u8 psd_input, u8 channel, u8 noise_k_en)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct psd_info *dm_psd_table = &dm->dm_psd_table;
	u8 fft_smp_point_idx = 0;

	dm_psd_table->fft_smp_point = fft_smp_point;

	if (sw_avg_time == 0)
		sw_avg_time = 1;

	dm_psd_table->sw_avg_time = sw_avg_time;
	dm_psd_table->psd_fc_channel = channel;
	dm_psd_table->noise_k_en = noise_k_en;
	if (dm->support_ic_type & ODM_RTL8723F) {
		if (fft_smp_point == 128)
			fft_smp_point_idx = 3;
		else if (fft_smp_point == 256)
			fft_smp_point_idx = 2;
		else if (fft_smp_point == 512)
			fft_smp_point_idx = 1;
		else if (fft_smp_point == 1024)
			fft_smp_point_idx = 0;
	} 
	else {
		if (fft_smp_point == 128)
			fft_smp_point_idx = 0;
		else if (fft_smp_point == 256)
			fft_smp_point_idx = 1;
		else if (fft_smp_point == 512)
			fft_smp_point_idx = 2;
		else if (fft_smp_point == 1024)
			fft_smp_point_idx = 3;
	}
	if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		#if (RTL8723F_SUPPORT)
		odm_set_bb_reg(dm, R_0x1e8c, BIT(12) | BIT(11), hw_avg_time);
		odm_set_bb_reg(dm, R_0x1e8c, BIT(14) | BIT(13),
				       fft_smp_point_idx);
		odm_set_bb_reg(dm, R_0x1e8c, BIT(18) | BIT(17), ant_sel);
		odm_set_bb_reg(dm, R_0x1e88, BIT(25) | BIT(24), psd_input);
		#else
		#if 0
		odm_set_bb_reg(dm, R_0x1e8c, BIT(11) | BIT(10), i_q_setting);
		odm_set_bb_reg(dm, R_0x1e8c, BIT(13) | BIT(12), hw_avg_time);

		if (fft_smp_point == 4096) {
			odm_set_bb_reg(dm, R_0x1e88, BIT(31) | BIT(30), 0x2);
		} else if (fft_smp_point == 2048) {
			odm_set_bb_reg(dm, R_0x1e88, BIT(31) | BIT(30), 0x1);
		} else {
			odm_set_bb_reg(dm, R_0x1e88, BIT(31) | BIT(30), 0x0);
			odm_set_bb_reg(dm, R_0x1e8c, BIT(15) | BIT(14),
				       fft_smp_point_idx);
		}
		odm_set_bb_reg(dm, R_0x1e8c, BIT(17) | BIT(16), ant_sel);
		odm_set_bb_reg(dm, R_0x1e8c, BIT(23) | BIT(22), psd_input);
		#endif
		#endif
	} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		odm_set_bb_reg(dm, R_0x910, BIT(11) | BIT(10), i_q_setting);
		odm_set_bb_reg(dm, R_0x910, BIT(13) | BIT(12), hw_avg_time);
		odm_set_bb_reg(dm, R_0x910, BIT(15) | BIT(14),
			       fft_smp_point_idx);
		odm_set_bb_reg(dm, R_0x910, BIT(17) | BIT(16), ant_sel);
		odm_set_bb_reg(dm, R_0x910, BIT(23), psd_input);
	} else if (dm->support_ic_type & (ODM_RTL8721D | ODM_RTL8710C)) {
		odm_set_bb_reg(dm, R_0x808, BIT(19) | BIT(18), i_q_setting);
		odm_set_bb_reg(dm, R_0x808, BIT(21) | BIT(20), hw_avg_time);
		odm_set_bb_reg(dm, R_0x808, BIT(23) | BIT(22),
			       fft_smp_point_idx);
		odm_set_bb_reg(dm, R_0x804, BIT(5) | BIT(4), ant_sel);
		odm_set_bb_reg(dm, R_0x80c, BIT(23), psd_input);

#if 0
	} else {	/*ODM_IC_11N_SERIES*/
#endif
	}
	/*@bw = (*dm->band_width); //ODM_BW20M */
	/*@channel = *(dm->channel);*/
}

void phydm_psd_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct psd_info *dm_psd_table = &dm->dm_psd_table;

	PHYDM_DBG(dm, ODM_COMP_API, "PSD para init\n");

	dm_psd_table->psd_in_progress = false;

	if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		#if (RTL8723F_SUPPORT)
		if (dm->support_ic_type & ODM_RTL8723F) {
			dm_psd_table->psd_reg = R_0x1e8c;
			dm_psd_table->psd_report_reg = R_0x2d90;

			/*@2b'11: 20MHz, 2b'10: 40MHz, 2b'01: 80MHz */
			dm_psd_table->psd_bw_rf_reg = 2;
		}
		#else
		#if 0
		dm_psd_table->psd_reg = R_0x1e8c;
		dm_psd_table->psd_report_reg = R_0x2d90;

		/*@2b'11: 20MHz, 2b'10: 40MHz, 2b'01: 80MHz */
		dm_psd_table->psd_bw_rf_reg = 1;
		#endif

		return;
		#endif
	} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		dm_psd_table->psd_reg = R_0x910;
		dm_psd_table->psd_report_reg = R_0xf44;

		/*@2b'11: 20MHz, 2b'10: 40MHz, 2b'01: 80MHz */
		if (ODM_IC_11AC_2_SERIES)
			dm_psd_table->psd_bw_rf_reg = 1;
		else
			dm_psd_table->psd_bw_rf_reg = 2;
	} else {
		dm_psd_table->psd_reg = R_0x808;
		dm_psd_table->psd_report_reg = R_0x8b4;
		/*@2b'11: 20MHz, 2b'10: 40MHz, 2b'01: 80MHz */
		dm_psd_table->psd_bw_rf_reg = 2;
	}

	dm_psd_table->psd_pwr_common_offset = 0;

	phydm_psd_para_setting(dm, 1, 2, 3, 128, 0, 0, 7, 0);
#if 0
	/*phydm_psd(dm, 0x3c, 0, 127);*/ /* target at -50dBm */
#endif
}

void phydm_psd_debug(void *dm_void, char input[][16], u32 *_used,
		     char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	char help[] = "-h";
	u32 var1[10] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;
	u8 i = 0;

	if ((strcmp(input[1], help) == 0)) {
		#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
		#if (RTL8723F_SUPPORT)
				if (dm->support_ic_type & ODM_RTL8723F)
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "{0} {sw_avg} {hw_avg 0:3} {1:I,2:Q,3:IQ} {fft_point: 128*(1:4)}\n{path_sel 0~3} {0:ADC, 1:rxdata_fir_in, 2:rx_nbi_nf_stage2} {CH} {noise_k}\n\n");
		#endif
		#if 0
		if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "{0} {sw_avg} {hw_avg 0:3} {1:I,2:Q,3:IQ} {fft_point: 128*(1:4) 2048 4096}\n{path_sel 0~3} {0:ADC, 1:rxdata_fir_in, 2:rx_nbi_nf_stage2} {CH} {noise_k}\n\n");
		else
		#endif
		#endif
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "{0} {sw_avg} {hw_avg 0:3} {1:I,2:Q,3:IQ} {fft_point: 128*(1:4)} {path_sel 0~3} {0:ADC, 1:RXIQC} {CH} {noise_k}\n");

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{1} {IGI(hex)} {start_point} {stop_point}\n");
		goto out;
	}

	PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

	if (var1[0] == 0) {
		for (i = 1; i < 10; i++) {
			PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL,
				     &var1[i]);
		}
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "sw_avg_time=((%d)), hw_avg_time=((%d)), IQ=((%d)), fft=((%d)), path=((%d)), input =((%d)) ch=((%d)), noise_k=((%d))\n",
			 var1[1], var1[2], var1[3], var1[4], var1[5],
			 var1[6], (u8)var1[7], (u8)var1[8]);
		phydm_psd_para_setting(dm, (u8)var1[1], (u8)var1[2],
				       (u8)var1[3], (u16)var1[4],
				       (u8)var1[5], (u8)var1[6],
				       (u8)var1[7], (u8)var1[8]);

	} else if (var1[0] == 1) {
		PHYDM_SSCANF(input[2], DCMD_HEX, &var1[1]);
		PHYDM_SSCANF(input[3], DCMD_DECIMAL, &var1[2]);
		PHYDM_SSCANF(input[4], DCMD_DECIMAL, &var1[3]);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "IGI=((0x%x)), start_point=((%d)), stop_point=((%d))\n",
			 var1[1], var1[2], var1[3]);
		dm->debug_components |= ODM_COMP_API;
		if (phydm_psd(dm, var1[1], (u16)var1[2], (u16)var1[3]) ==
		    PHYDM_SET_FAIL)
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "PSD_SET_FAIL\n");
		dm->debug_components &= ~(ODM_COMP_API);
	}

out:
	*_used = used;
	*_out_len = out_len;
}

u8 phydm_get_psd_result_table(void *dm_void, int index)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct psd_info *dm_psd_table = &dm->dm_psd_table;
	u8 result = 0;

	if (index < 128)
		result = dm_psd_table->psd_result[index];

	return result;
}

#endif
