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

/*
============================================================
 include files
============================================================
*/

#include "mp_precomp.h"
#include "phydm_precomp.h"

#if defined(CONFIG_PHYDM_DFS_MASTER)

boolean phydm_dfs_is_meteorology_channel(void *p_dm_void){

	struct PHY_DM_STRUCT *p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	
	u8 c_channel = *(p_dm->p_channel);
	u8 band_width = *(p_dm->p_band_width);
	
	return ( (band_width == CHANNEL_WIDTH_80 && (c_channel) >= 116 && (c_channel) <= 128) || 
	  (band_width == CHANNEL_WIDTH_40 && (c_channel) >= 116 && (c_channel) <= 128) ||
	  (band_width == CHANNEL_WIDTH_20 && (c_channel) >= 120 && (c_channel) <= 128) );
}

void phydm_radar_detect_reset(void *p_dm_void)
{
	struct PHY_DM_STRUCT *p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	odm_set_bb_reg(p_dm, 0x924, BIT(15), 0);
	odm_set_bb_reg(p_dm, 0x924, BIT(15), 1);
}

void phydm_radar_detect_disable(void *p_dm_void)
{
	struct PHY_DM_STRUCT *p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	odm_set_bb_reg(p_dm, 0x924, BIT(15), 0);
	PHYDM_DBG(p_dm, DBG_DFS, ("\n"));
}

static void phydm_radar_detect_with_dbg_parm(void *p_dm_void)
{
	struct PHY_DM_STRUCT *p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	odm_set_bb_reg(p_dm, 0x918, MASKDWORD, p_dm->radar_detect_reg_918);
	odm_set_bb_reg(p_dm, 0x91c, MASKDWORD, p_dm->radar_detect_reg_91c);
	odm_set_bb_reg(p_dm, 0x920, MASKDWORD, p_dm->radar_detect_reg_920);
	odm_set_bb_reg(p_dm, 0x924, MASKDWORD, p_dm->radar_detect_reg_924);
}

/* Init radar detection parameters, called after ch, bw is set */
void phydm_radar_detect_enable(void *p_dm_void)
{
	struct PHY_DM_STRUCT *p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _DFS_STATISTICS	*p_dfs = (struct _DFS_STATISTICS *)phydm_get_structure(p_dm, PHYDM_DFS);
	u8 region_domain = p_dm->dfs_region_domain;
	u8 c_channel = *(p_dm->p_channel);
	u8 band_width = *(p_dm->p_band_width);
	u8 enable = 0;

	PHYDM_DBG(p_dm, DBG_DFS, ("test, region_domain = %d\n", region_domain));
	if (region_domain == PHYDM_DFS_DOMAIN_UNKNOWN) {
		PHYDM_DBG(p_dm, DBG_DFS, ("PHYDM_DFS_DOMAIN_UNKNOWN\n"));
		goto exit;
	}

	if (p_dm->support_ic_type & (ODM_RTL8821 | ODM_RTL8812 | ODM_RTL8881A)) {

		odm_set_bb_reg(p_dm, 0x814, 0x3fffffff, 0x04cc4d10);
		odm_set_bb_reg(p_dm, 0x834, MASKBYTE0, 0x06);

		if (p_dm->radar_detect_dbg_parm_en) {
			phydm_radar_detect_with_dbg_parm(p_dm);
			enable = 1;
			goto exit;
		}

		if (region_domain == PHYDM_DFS_DOMAIN_ETSI) {
			odm_set_bb_reg(p_dm, 0x918, MASKDWORD, 0x1c17ecdf);
			odm_set_bb_reg(p_dm, 0x924, MASKDWORD, 0x01528500);
			odm_set_bb_reg(p_dm, 0x91c, MASKDWORD, 0x0fa21a20);
			odm_set_bb_reg(p_dm, 0x920, MASKDWORD, 0xe0f69204);

		} else if (region_domain == PHYDM_DFS_DOMAIN_MKK) {
			odm_set_bb_reg(p_dm, 0x924, MASKDWORD, 0x01528500);
			odm_set_bb_reg(p_dm, 0x920, MASKDWORD, 0xe0d67234);

			if (c_channel >= 52 && c_channel <= 64) {
				odm_set_bb_reg(p_dm, 0x918, MASKDWORD, 0x1c16ecdf);
				odm_set_bb_reg(p_dm, 0x91c, MASKDWORD, 0x0f141a20);
			} else {
				odm_set_bb_reg(p_dm, 0x918, MASKDWORD, 0x1c16acdf);
				if (band_width == CHANNEL_WIDTH_20)
					odm_set_bb_reg(p_dm, 0x91c, MASKDWORD, 0x64721a20);
				else
					odm_set_bb_reg(p_dm, 0x91c, MASKDWORD, 0x68721a20);
			}

		} else if (region_domain == PHYDM_DFS_DOMAIN_FCC) {
			odm_set_bb_reg(p_dm, 0x918, MASKDWORD, 0x1c16acdf);
			odm_set_bb_reg(p_dm, 0x924, MASKDWORD, 0x01528500);
			odm_set_bb_reg(p_dm, 0x920, MASKDWORD, 0xe0d67231);
			if (band_width == CHANNEL_WIDTH_20)
				odm_set_bb_reg(p_dm, 0x91c, MASKDWORD, 0x64741a20);
			else
				odm_set_bb_reg(p_dm, 0x91c, MASKDWORD, 0x68741a20);

		} else {
			/* not supported */
			PHYDM_DBG(p_dm, DBG_DFS, ("Unsupported dfs_region_domain:%d\n", region_domain));
			goto exit;
		}

	} else if (p_dm->support_ic_type & (ODM_RTL8814A | ODM_RTL8822B | ODM_RTL8821C)) {

		odm_set_bb_reg(p_dm, 0x814, 0x3fffffff, 0x04cc4d10);
		odm_set_bb_reg(p_dm, 0x834, MASKBYTE0, 0x06);

		/* 8822B only, when BW = 20M, DFIR output is 40Mhz, but DFS input is 80MMHz, so it need to upgrade to 80MHz */
		if (p_dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8821C)) {
			if (band_width == CHANNEL_WIDTH_20)
				odm_set_bb_reg(p_dm, 0x1984, BIT(26), 1);
			else
				odm_set_bb_reg(p_dm, 0x1984, BIT(26), 0);
		}

		if (p_dm->radar_detect_dbg_parm_en) {
			phydm_radar_detect_with_dbg_parm(p_dm);
			enable = 1;
			goto exit;
		}

		if (region_domain == PHYDM_DFS_DOMAIN_ETSI) {
			odm_set_bb_reg(p_dm, 0x918, MASKDWORD, 0x1c16acdf);
			odm_set_bb_reg(p_dm, 0x924, MASKDWORD, 0x095a8500);
			odm_set_bb_reg(p_dm, 0x91c, MASKDWORD, 0x0fa21a20);
			odm_set_bb_reg(p_dm, 0x920, MASKDWORD, 0xe0f57204);

		} else if (region_domain == PHYDM_DFS_DOMAIN_MKK) {
			odm_set_bb_reg(p_dm, 0x924, MASKDWORD, 0x095a8500);
			odm_set_bb_reg(p_dm, 0x920, MASKDWORD, 0xe0d67234);

			if (c_channel >= 52 && c_channel <= 64) {
				odm_set_bb_reg(p_dm, 0x918, MASKDWORD, 0x1c16ecdf);
				odm_set_bb_reg(p_dm, 0x91c, MASKDWORD, 0x0f141a20);
			} else {
				odm_set_bb_reg(p_dm, 0x918, MASKDWORD, 0x1c166cdf);
				if (band_width == CHANNEL_WIDTH_20)
					odm_set_bb_reg(p_dm, 0x91c, MASKDWORD, 0x64721a20);
				else
					odm_set_bb_reg(p_dm, 0x91c, MASKDWORD, 0x68721a20);
			}

		} else if (region_domain == PHYDM_DFS_DOMAIN_FCC) {
			odm_set_bb_reg(p_dm, 0x918, MASKDWORD, 0x1c166cdf);
			odm_set_bb_reg(p_dm, 0x924, MASKDWORD, 0x095a8500);
			odm_set_bb_reg(p_dm, 0x920, MASKDWORD, 0xe0d67231);
			if (band_width == CHANNEL_WIDTH_20)
				odm_set_bb_reg(p_dm, 0x91c, MASKDWORD, 0x64741a20);
			else
				odm_set_bb_reg(p_dm, 0x91c, MASKDWORD, 0x68741a20);

		} else {
			/* not supported */
			PHYDM_DBG(p_dm, DBG_DFS, ("Unsupported dfs_region_domain:%d\n", region_domain));
			goto exit;
		}
	} else {
		/* not supported IC type*/
		PHYDM_DBG(p_dm, DBG_DFS, ("Unsupported IC type:%d\n", p_dm->support_ic_type));
		goto exit;
	}

	enable = 1;

	p_dfs->st_l2h_cur = (u8)odm_get_bb_reg(p_dm, 0x91c, 0x000000ff);
	p_dfs->pwdb_th = (u8)odm_get_bb_reg(p_dm, 0x918, 0x00001f00);
	p_dfs->peak_th = (u8)odm_get_bb_reg(p_dm, 0x918, 0x00030000);
	p_dfs->short_pulse_cnt_th = (u8)odm_get_bb_reg(p_dm, 0x920, 0x000f0000);
	p_dfs->long_pulse_cnt_th = (u8)odm_get_bb_reg(p_dm, 0x920, 0x00f00000);
	p_dfs->peak_window = (u8)odm_get_bb_reg(p_dm, 0x920, 0x00000300);
	p_dfs->nb2wb_th = (u8)odm_get_bb_reg(p_dm, 0x920, 0x0000e000);

	phydm_dfs_parameter_init(p_dm);

exit:
	if (enable) {
		phydm_radar_detect_reset(p_dm);
		PHYDM_DBG(p_dm, DBG_DFS, ("on cch:%u, bw:%u\n", c_channel, band_width));
	} else
		phydm_radar_detect_disable(p_dm);
}

void phydm_dfs_parameter_init(void *p_dm_void)
{

	struct PHY_DM_STRUCT *p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _DFS_STATISTICS	*p_dfs = (struct _DFS_STATISTICS *)phydm_get_structure(p_dm, PHYDM_DFS);

	u8 i;
	
	p_dfs->fa_mask_th = 30;
	p_dfs->det_print = 1;
	p_dfs->det_print2 = 0;
	p_dfs->st_l2h_min = 0x20;
	p_dfs->st_l2h_max = 0x4e;
	p_dfs->pwdb_scalar_factor = 12;
	p_dfs->pwdb_th = 8;
	for (i = 0 ; i < 5 ; i++) {
		p_dfs->pulse_flag_hist[i] = 0;
		p_dfs->radar_det_mask_hist[i] = 0;
		p_dfs->fa_inc_hist[i] = 0;
	}

}

void phydm_dfs_dynamic_setting(
	void *p_dm_void
){
	struct PHY_DM_STRUCT *p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _DFS_STATISTICS	*p_dfs = (struct _DFS_STATISTICS *)phydm_get_structure(p_dm, PHYDM_DFS);

	u8 peak_th_cur=0, short_pulse_cnt_th_cur=0, long_pulse_cnt_th_cur=0, three_peak_opt_cur=0, three_peak_th2_cur=0;
	u8 peak_window_cur=0, nb2wb_th_cur=0;
	u8 region_domain = p_dm->dfs_region_domain;
	u8 c_channel = *(p_dm->p_channel);
	
	if (p_dm->rx_tp <= 2) {
		p_dfs->idle_mode = 1;
		if(p_dfs->force_TP_mode)
			p_dfs->idle_mode = 0;
	} else{
		p_dfs->idle_mode = 0;
	}

	if ((p_dfs->idle_mode == 1)) { /*idle (no traffic)*/
		peak_th_cur = 3;
		short_pulse_cnt_th_cur = 6;
		long_pulse_cnt_th_cur = 13;
		peak_window_cur = 2;
		nb2wb_th_cur = 6;
		three_peak_opt_cur = 1;
		three_peak_th2_cur = 2;
		if (region_domain == PHYDM_DFS_DOMAIN_MKK) {
			if ((c_channel >= 52) && (c_channel <= 64)) {
				short_pulse_cnt_th_cur = 14;
				long_pulse_cnt_th_cur = 15;
				nb2wb_th_cur = 3;
				three_peak_th2_cur = 0;                
			} else {
				short_pulse_cnt_th_cur = 6;
				nb2wb_th_cur = 3;
				three_peak_th2_cur = 0;
				long_pulse_cnt_th_cur = 10;
			}
		} else if (region_domain == PHYDM_DFS_DOMAIN_FCC) {
			three_peak_th2_cur = 0;
		} else if (region_domain == PHYDM_DFS_DOMAIN_ETSI) {
			long_pulse_cnt_th_cur = 15;
			if (phydm_dfs_is_meteorology_channel(p_dm)) {/*need to add check cac end condition*/
				peak_th_cur = 2;
				nb2wb_th_cur = 3;
				three_peak_opt_cur = 1;
				three_peak_th2_cur = 0;	
				short_pulse_cnt_th_cur = 7;
			} else {
				three_peak_opt_cur = 1;
				three_peak_th2_cur = 0;	
				short_pulse_cnt_th_cur = 7;
				nb2wb_th_cur = 3;
			}
		} else	/*default: FCC*/
			three_peak_th2_cur = 0;

	} else { /*in service (with TP)*/
		peak_th_cur = 2;
		short_pulse_cnt_th_cur = 6;
		long_pulse_cnt_th_cur = 9;
		peak_window_cur = 2;
		nb2wb_th_cur = 3;
		three_peak_opt_cur = 1;
		three_peak_th2_cur = 2;
		if(region_domain == PHYDM_DFS_DOMAIN_MKK){
			if ((c_channel >= 52) && (c_channel <= 64)) {
				long_pulse_cnt_th_cur = 15;
				short_pulse_cnt_th_cur = 5; /*for high duty cycle*/
				three_peak_th2_cur = 0;			
			}
			else {
				three_peak_opt_cur = 0;
				three_peak_th2_cur = 0;
				long_pulse_cnt_th_cur = 8;
			}
		}		
		else if(region_domain == PHYDM_DFS_DOMAIN_FCC){
		}
		else if(region_domain == PHYDM_DFS_DOMAIN_ETSI){
			long_pulse_cnt_th_cur = 15;
			short_pulse_cnt_th_cur = 5;
			three_peak_opt_cur = 0;
		}
		else{
		}
	}

}


boolean
phydm_radar_detect_dm_check(
	void *p_dm_void
){
	struct PHY_DM_STRUCT *p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _DFS_STATISTICS	*p_dfs = (struct _DFS_STATISTICS *)phydm_get_structure(p_dm, PHYDM_DFS);
	u8 region_domain = p_dm->dfs_region_domain, index = 0;

	u16 i = 0, k = 0, fa_count_cur = 0, fa_count_inc = 0, total_fa_in_hist = 0, pre_post_now_acc_fa_in_hist = 0, max_fa_in_hist = 0, vht_crc_ok_cnt_cur = 0;
	u16 vht_crc_ok_cnt_inc = 0, ht_crc_ok_cnt_cur = 0, ht_crc_ok_cnt_inc = 0, leg_crc_ok_cnt_cur = 0, leg_crc_ok_cnt_inc = 0;
	u16 total_crc_ok_cnt_inc = 0, short_pulse_cnt_cur = 0, short_pulse_cnt_inc = 0, long_pulse_cnt_cur = 0, long_pulse_cnt_inc = 0, total_pulse_count_inc = 0;
	u32 regf98_value = 0, reg918_value = 0, reg91c_value = 0, reg920_value = 0, reg924_value = 0;
	boolean tri_short_pulse = 0, tri_long_pulse = 0, radar_type = 0, fault_flag_det = 0, fault_flag_psd = 0, fa_flag = 0, radar_detected = 0;
	u8 st_l2h_new = 0, fa_mask_th = 0, sum = 0;
	u8 c_channel = *(p_dm->p_channel);
		
	/*Get FA count during past 100ms*/
	fa_count_cur = (u16)odm_get_bb_reg(p_dm, 0xf48, 0x0000ffff);
	
	if (p_dfs->fa_count_pre == 0)
		fa_count_inc = 0;
	else if (fa_count_cur >= p_dfs->fa_count_pre)
		fa_count_inc = fa_count_cur - p_dfs->fa_count_pre;
	else
		fa_count_inc = fa_count_cur;
	p_dfs->fa_count_pre = fa_count_cur;

	p_dfs->fa_inc_hist[p_dfs->mask_idx] = fa_count_inc;
	
	for (i=0; i<5; i++) {		
		total_fa_in_hist = total_fa_in_hist + p_dfs->fa_inc_hist[i];		
		if (p_dfs->fa_inc_hist[i] > max_fa_in_hist)			
			max_fa_in_hist = p_dfs->fa_inc_hist[i];	
	}	
	if (p_dfs->mask_idx >= 2)		
		index = p_dfs->mask_idx - 2;
	else		
		index = 5 + p_dfs->mask_idx - 2;	
	if (index == 0)		
		pre_post_now_acc_fa_in_hist = p_dfs->fa_inc_hist[index] + p_dfs->fa_inc_hist[index+1] + p_dfs->fa_inc_hist[4];	
	else if (index == 4)		
		pre_post_now_acc_fa_in_hist = p_dfs->fa_inc_hist[index] + p_dfs->fa_inc_hist[0] + p_dfs->fa_inc_hist[index-1];	
	else		
		pre_post_now_acc_fa_in_hist = p_dfs->fa_inc_hist[index] + p_dfs->fa_inc_hist[index+1] + p_dfs->fa_inc_hist[index-1];
		
	/*Get VHT CRC32 ok count during past 100ms*/
	vht_crc_ok_cnt_cur = (u16)odm_get_bb_reg(p_dm, 0xf0c, 0x00003fff);
	if (vht_crc_ok_cnt_cur >= p_dfs->vht_crc_ok_cnt_pre)
		vht_crc_ok_cnt_inc = vht_crc_ok_cnt_cur - p_dfs->vht_crc_ok_cnt_pre;
	else
		vht_crc_ok_cnt_inc = vht_crc_ok_cnt_cur;
	p_dfs->vht_crc_ok_cnt_pre = vht_crc_ok_cnt_cur;

	/*Get HT CRC32 ok count during past 100ms*/
	ht_crc_ok_cnt_cur = (u16)odm_get_bb_reg(p_dm, 0xf10, 0x00003fff);
	if (ht_crc_ok_cnt_cur >= p_dfs->ht_crc_ok_cnt_pre)
		ht_crc_ok_cnt_inc = ht_crc_ok_cnt_cur - p_dfs->ht_crc_ok_cnt_pre;
	else
		ht_crc_ok_cnt_inc = ht_crc_ok_cnt_cur;
	p_dfs->ht_crc_ok_cnt_pre = ht_crc_ok_cnt_cur;

	/*Get Legacy CRC32 ok count during past 100ms*/
	leg_crc_ok_cnt_cur = (u16)odm_get_bb_reg(p_dm, 0xf14, 0x00003fff);
	if (leg_crc_ok_cnt_cur >= p_dfs->leg_crc_ok_cnt_pre)
		leg_crc_ok_cnt_inc = leg_crc_ok_cnt_cur - p_dfs->leg_crc_ok_cnt_pre;
	else
		leg_crc_ok_cnt_inc = leg_crc_ok_cnt_cur;
	p_dfs->leg_crc_ok_cnt_pre = leg_crc_ok_cnt_cur;

	if ((vht_crc_ok_cnt_cur == 0x3fff) ||
		(ht_crc_ok_cnt_cur == 0x3fff) ||
		(leg_crc_ok_cnt_cur == 0x3fff)) {
		odm_set_bb_reg(p_dm, 0xb58, BIT(0), 1);
		odm_set_bb_reg(p_dm, 0xb58, BIT(0), 0);
	}

	total_crc_ok_cnt_inc = vht_crc_ok_cnt_inc + ht_crc_ok_cnt_inc + leg_crc_ok_cnt_inc;

	/*Get short pulse count, need carefully handle the counter overflow*/
	regf98_value = odm_get_bb_reg(p_dm, 0xf98, 0xffffffff);
	short_pulse_cnt_cur = (u16)(regf98_value & 0x000000ff);
	if (short_pulse_cnt_cur >= p_dfs->short_pulse_cnt_pre)
		short_pulse_cnt_inc = short_pulse_cnt_cur - p_dfs->short_pulse_cnt_pre;
	else
		short_pulse_cnt_inc = short_pulse_cnt_cur;
	p_dfs->short_pulse_cnt_pre = short_pulse_cnt_cur;

	/*Get long pulse count, need carefully handle the counter overflow*/
	long_pulse_cnt_cur = (u16)((regf98_value & 0x0000ff00) >> 8);
	if (long_pulse_cnt_cur >= p_dfs->long_pulse_cnt_pre)
		long_pulse_cnt_inc = long_pulse_cnt_cur - p_dfs->long_pulse_cnt_pre;
	else
		long_pulse_cnt_inc = long_pulse_cnt_cur;
	p_dfs->long_pulse_cnt_pre = long_pulse_cnt_cur;

	total_pulse_count_inc = short_pulse_cnt_inc + long_pulse_cnt_inc;

	if (p_dfs->det_print){
		PHYDM_DBG(p_dm, DBG_DFS, ("=====================================================================\n"));
		PHYDM_DBG(p_dm, DBG_DFS, ("Total_CRC_OK_cnt_inc[%d] VHT_CRC_ok_cnt_inc[%d] HT_CRC_ok_cnt_inc[%d] LEG_CRC_ok_cnt_inc[%d] FA_count_inc[%d]\n",
			total_crc_ok_cnt_inc, vht_crc_ok_cnt_inc, ht_crc_ok_cnt_inc, leg_crc_ok_cnt_inc, fa_count_inc));
		PHYDM_DBG(p_dm, DBG_DFS, ("Init_Gain[%x] 0x91c[%x] 0xf98[%08x] short_pulse_cnt_inc[%d] long_pulse_cnt_inc[%d]\n",
			p_dfs->igi_cur, p_dfs->st_l2h_cur, regf98_value, short_pulse_cnt_inc, long_pulse_cnt_inc));
		PHYDM_DBG(p_dm, DBG_DFS, ("Throughput: %dMbps\n", p_dm->rx_tp));
		reg918_value = odm_get_bb_reg(p_dm, 0x918, 0xffffffff);
		reg91c_value = odm_get_bb_reg(p_dm, 0x91c, 0xffffffff);
		reg920_value = odm_get_bb_reg(p_dm, 0x920, 0xffffffff);
		reg924_value = odm_get_bb_reg(p_dm, 0x924, 0xffffffff);
		PHYDM_DBG(p_dm, DBG_DFS, ("0x918[%08x] 0x91c[%08x] 0x920[%08x] 0x924[%08x]\n", reg918_value, reg91c_value, reg920_value, reg924_value));
		PHYDM_DBG(p_dm, DBG_DFS, ("dfs_regdomain = %d, dbg_mode = %d, idle_mode = %d\n", region_domain, p_dfs->dbg_mode, p_dfs->idle_mode));
	}
	tri_short_pulse = (regf98_value & BIT(17))? 1 : 0;
	tri_long_pulse = (regf98_value & BIT(19))? 1 : 0;

	if(tri_short_pulse)
		radar_type = 0;
	else if(tri_long_pulse)
		radar_type = 1;

	if (tri_short_pulse) {
		odm_set_bb_reg(p_dm, 0x924, BIT(15), 0);
		odm_set_bb_reg(p_dm, 0x924, BIT(15), 1);
	}
	if (tri_long_pulse) {
		odm_set_bb_reg(p_dm, 0x924, BIT(15), 0);
		odm_set_bb_reg(p_dm, 0x924, BIT(15), 1);
		if (region_domain == PHYDM_DFS_DOMAIN_MKK) {	
			if ((c_channel >= 52) && (c_channel <= 64)) {
				tri_long_pulse = 0;
			}
		}
		if (region_domain == PHYDM_DFS_DOMAIN_ETSI) {
			tri_long_pulse = 0;
		}
	}

	st_l2h_new = p_dfs->st_l2h_cur;
	p_dfs->pulse_flag_hist[p_dfs->mask_idx] = tri_short_pulse | tri_long_pulse;

	/* PSD(not ready) */

	fault_flag_det = 0;
	fault_flag_psd = 0;
	fa_flag = 0;
	if(region_domain == PHYDM_DFS_DOMAIN_ETSI){
		fa_mask_th = p_dfs->fa_mask_th + 20;		
	}
	else{
		fa_mask_th = p_dfs->fa_mask_th;		
	}
	if (max_fa_in_hist >= fa_mask_th || total_fa_in_hist >= fa_mask_th || pre_post_now_acc_fa_in_hist >= fa_mask_th || (p_dfs->igi_cur >= 0x30)){		
		st_l2h_new = p_dfs->st_l2h_max;
		p_dfs->radar_det_mask_hist[index] = 1;		
		if (p_dfs->pulse_flag_hist[index] == 1){			
			p_dfs->pulse_flag_hist[index] = 0;			
			if (p_dfs->det_print2){
				PHYDM_DBG(p_dm, DBG_DFS, ("Radar is masked : FA mask\n"));
			}
		}
		fa_flag = 1;
	}

	if (p_dfs->det_print) {
		PHYDM_DBG(p_dm, DBG_DFS, ("mask_idx: %d\n", p_dfs->mask_idx));
		PHYDM_DBG(p_dm, DBG_DFS, ("radar_det_mask_hist: "));
		for (i=0; i<5; i++)
			PHYDM_DBG(p_dm, DBG_DFS, ("%d ", p_dfs->radar_det_mask_hist[i]));
		PHYDM_DBG(p_dm, DBG_DFS, ("pulse_flag_hist: "));
		for (i=0; i<5; i++)
			PHYDM_DBG(p_dm, DBG_DFS, ("%d ", p_dfs->pulse_flag_hist[i]));
		PHYDM_DBG(p_dm, DBG_DFS, ("fa_inc_hist: "));
		for (i=0; i<5; i++)			
			PHYDM_DBG(p_dm, DBG_DFS, ("%d ", p_dfs->fa_inc_hist[i]));
		PHYDM_DBG(p_dm, DBG_DFS,
			("\nmax_fa_in_hist: %d pre_post_now_acc_fa_in_hist: %d ", max_fa_in_hist, pre_post_now_acc_fa_in_hist));
	}

	sum = 0;
	for (k=0; k<5; k++) {
		if (p_dfs->radar_det_mask_hist[k] == 1)
			sum++;
	}

	if (p_dfs->mask_hist_checked <= 5)
		p_dfs->mask_hist_checked++;

	if ((p_dfs->mask_hist_checked >= 5) && p_dfs->pulse_flag_hist[index])
	{
		if (sum <= 2) 
		{
			radar_detected = 1 ;
			PHYDM_DBG(p_dm, DBG_DFS, ("Detected type %d radar signal!\n", radar_type));
		}
		else {
			fault_flag_det = 1;
			if (p_dfs->det_print2){
				PHYDM_DBG(p_dm, DBG_DFS, ("Radar is masked : mask_hist large than thd\n"));
			}
		}
	}

	p_dfs->mask_idx++;
	if (p_dfs->mask_idx == 5)
		p_dfs->mask_idx = 0;

	if ((fault_flag_det == 0) && (fault_flag_psd == 0) && (fa_flag ==0)) {		
		if (p_dfs->igi_cur < 0x30) {
			st_l2h_new = p_dfs->st_l2h_min;
		}
	}
	
	if ((st_l2h_new != p_dfs->st_l2h_cur)) {
		if (st_l2h_new < p_dfs->st_l2h_min) {			
			p_dfs->st_l2h_cur = p_dfs->st_l2h_min;			
		}
		else if (st_l2h_new > p_dfs->st_l2h_max)
			p_dfs->st_l2h_cur = p_dfs->st_l2h_max;
		else
			p_dfs->st_l2h_cur = st_l2h_new;
		odm_set_bb_reg(p_dm, 0x91c, 0xff, p_dfs->st_l2h_cur);

		p_dfs->pwdb_th = ((int)p_dfs->st_l2h_cur - (int)p_dfs->igi_cur)/2 + p_dfs->pwdb_scalar_factor;
		p_dfs->pwdb_th = MAX_2(p_dfs->pwdb_th, (int)p_dfs->pwdb_th); /*limit the pwdb value to absoulte lower bound 8*/
		p_dfs->pwdb_th = MIN_2(p_dfs->pwdb_th, 0x1f);    /*limit the pwdb value to absoulte upper bound 0x1f*/
		odm_set_bb_reg(p_dm, 0x918, 0x00001f00, p_dfs->pwdb_th);
	}

	if (p_dfs->det_print2) {
		PHYDM_DBG(p_dm, DBG_DFS,
			("fault_flag_det[%d], fault_flag_psd[%d], DFS_detected [%d]\n", fault_flag_det, fault_flag_psd, radar_detected));
	}

	return radar_detected;

}

boolean phydm_radar_detect(void *p_dm_void)
{
	struct PHY_DM_STRUCT *p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _DFS_STATISTICS	*p_dfs = (struct _DFS_STATISTICS *)phydm_get_structure(p_dm, PHYDM_DFS);
	boolean enable_DFS = false;
	boolean radar_detected = false;

	p_dfs->igi_cur = (u8)odm_get_bb_reg(p_dm, 0xc50, 0x0000007f);

	p_dfs->st_l2h_cur = (u8)odm_get_bb_reg(p_dm, 0x91c, 0x000000ff);

	/* dynamic pwdb calibration */
	if (p_dfs->igi_pre != p_dfs->igi_cur) {
		p_dfs->pwdb_th = ((int)p_dfs->st_l2h_cur - (int)p_dfs->igi_cur)/2 + p_dfs->pwdb_scalar_factor;
		p_dfs->pwdb_th = MAX_2(p_dfs->pwdb_th_cur, (int)p_dfs->pwdb_th); /* limit the pwdb value to absoulte lower bound 0xa */
		p_dfs->pwdb_th = MIN_2(p_dfs->pwdb_th_cur, 0x1f);    /* limit the pwdb value to absoulte upper bound 0x1f */
		odm_set_bb_reg(p_dm,  0x918, 0x00001f00, p_dfs->pwdb_th);
	}

	p_dfs->igi_pre = p_dfs->igi_cur;

	phydm_dfs_dynamic_setting(p_dm);
	radar_detected = phydm_radar_detect_dm_check(p_dm);

	if (odm_get_bb_reg(p_dm, 0x924, BIT(15)))
		enable_DFS = true;

	if (enable_DFS && radar_detected) {
		PHYDM_DBG(p_dm, DBG_DFS, ("Radar detect: enable_DFS:%d, radar_detected:%d\n", enable_DFS, radar_detected));
		phydm_radar_detect_reset(p_dm);
                if (p_dfs->dbg_mode == 1){
			PHYDM_DBG(p_dm, DBG_DFS, ("Radar is detected in DFS dbg mode.\n"));
			radar_detected = 0;
		}
	}

	return enable_DFS && radar_detected;
}


void
phydm_dfs_debug(
	void		*p_dm_void,
	u32		*const argv,
	u32		*_used,
	char		*output,
	u32		*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _DFS_STATISTICS	*p_dfs = (struct _DFS_STATISTICS *)phydm_get_structure(p_dm_odm, PHYDM_DFS);
	u32 used = *_used;
	u32 out_len = *_out_len;

	p_dfs->dbg_mode = (boolean)argv[0];
	p_dfs->force_TP_mode = (boolean)argv[1];
	p_dfs->det_print = (boolean)argv[2];
	p_dfs->det_print2 = (boolean)argv[3];

	PHYDM_SNPRINTF((output + used, out_len - used, "dbg_mode: %d, force_TP_mode: %d, det_print: %d, det_print2: %d\n", p_dfs->dbg_mode, p_dfs->force_TP_mode, p_dfs->det_print, p_dfs->det_print2));
	
	/*switch (argv[0]) {
	case 1:
#if defined(CONFIG_PHYDM_DFS_MASTER)
		 set dbg parameters for radar detection instead of the default value 
		if (argv[1] == 1) {
			p_dm_odm->radar_detect_reg_918 = argv[2];
			p_dm_odm->radar_detect_reg_91c = argv[3];
			p_dm_odm->radar_detect_reg_920 = argv[4];
			p_dm_odm->radar_detect_reg_924 = argv[5];
			p_dm_odm->radar_detect_dbg_parm_en = 1;

			PHYDM_SNPRINTF((output + used, out_len - used, "Radar detection with dbg parameter\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "reg918:0x%08X\n", p_dm_odm->radar_detect_reg_918));
			PHYDM_SNPRINTF((output + used, out_len - used, "reg91c:0x%08X\n", p_dm_odm->radar_detect_reg_91c));
			PHYDM_SNPRINTF((output + used, out_len - used, "reg920:0x%08X\n", p_dm_odm->radar_detect_reg_920));
			PHYDM_SNPRINTF((output + used, out_len - used, "reg924:0x%08X\n", p_dm_odm->radar_detect_reg_924));
		} else {
			p_dm_odm->radar_detect_dbg_parm_en = 0;
			PHYDM_SNPRINTF((output + used, out_len - used, "Radar detection with default parameter\n"));
		}
		phydm_radar_detect_enable(p_dm_odm);
#endif  defined(CONFIG_PHYDM_DFS_MASTER) 

		break;
	default:
		break;
	}*/
}



#endif /* defined(CONFIG_PHYDM_DFS_MASTER) */

boolean
phydm_is_dfs_band(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT *p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (((*p_dm->p_channel >= 52) && (*p_dm->p_channel <= 64)) ||
		((*p_dm->p_channel >= 100) && (*p_dm->p_channel <= 140)))
		return true;
	else
		return false;
}

boolean
phydm_dfs_master_enabled(
	void		*p_dm_void
)
{
#ifdef CONFIG_PHYDM_DFS_MASTER
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	return *p_dm->dfs_master_enabled ? true : false;
#else
	return false;
#endif
}

