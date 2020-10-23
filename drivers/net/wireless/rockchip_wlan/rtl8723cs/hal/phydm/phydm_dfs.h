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

#ifndef __PHYDM_DFS_H__
#define __PHYDM_DFS_H__

#define DFS_VERSION "1.1"

/*@
 * ============================================================
 *  Definition
 * ============================================================
 */

/*@
 * ============================================================
 * 1  structure
 * ============================================================
 */

struct _DFS_STATISTICS {
	u8		mask_idx;
	u8		igi_cur;
	u8		igi_pre;
	u8		st_l2h_cur;
	u16		fa_count_pre;
	u16		fa_inc_hist[5];
	u16		short_pulse_cnt_pre;
	u16		long_pulse_cnt_pre;
	u8		pwdb_th;
	u8		pwdb_th_cur;
	u8		pwdb_scalar_factor;
	u8		peak_th;
	u8		short_pulse_cnt_th;
	u8		long_pulse_cnt_th;
	u8		peak_window;
	u8		three_peak_opt;
	u8		three_peak_th2;
	u8		fa_mask_th;
	u8		st_l2h_max;
	u8		st_l2h_min;
	u8		dfs_polling_time;
	u8		mask_hist_checked : 3;
	boolean		pulse_flag_hist[5];
	boolean		pulse_type_hist[5];
	boolean		radar_det_mask_hist[5];
	boolean		idle_mode;
	boolean		force_TP_mode;
	boolean		dbg_mode;
	boolean		sw_trigger_mode;
	boolean		det_print;
	boolean		det_print2;
	boolean		radar_type;
	boolean		print_hist_rpt;
	boolean		hist_cond_on;
	/*@dfs histogram*/
	boolean		pri_cond1;
	boolean		pri_cond2;
	boolean		pri_cond3;
	boolean		pri_cond4;
	boolean		pri_cond5;
	boolean		pw_cond1;
	boolean		pw_cond2;
	boolean		pw_cond3;
	boolean		pri_type3_4_cond1;	/*@for ETSI*/
	boolean		pri_type3_4_cond2;	/*@for ETSI*/
	boolean		pw_long_cond1;	/*@for long radar*/
	boolean		pw_long_cond2;	/*@for long radar*/
	boolean		pri_long_cond1;	/*@for long radar*/
	boolean		pw_flag;
	boolean		pri_flag;
	boolean		pri_type3_4_flag;	/*@for ETSI*/
	boolean		long_radar_flag;
	u8		pri_hold_sum[6];
	u8		pw_hold_sum[6];
	u8		pri_long_hold_sum[6];
	u8		pw_long_hold_sum[6];
	u8		hist_idx;
	u8		hist_long_idx;
	u8		pw_hold[4][6];
	u8		pri_hold[4][6];
	u8		pw_std;	/*@The std(var) of reasonable num of pw group*/
	u8		pri_std;/*@The std(var) of reasonable num of pri group*/
	/*@dfs histogram threshold*/
	u8		pri_hist_th : 3;
	u8		pri_sum_g1_th : 4;
	u8		pri_sum_g5_th : 4;
	u8		pri_sum_g1_fcc_th : 3;
	u8		pri_sum_g3_fcc_th : 3;
	u8		pri_sum_safe_fcc_th : 7;
	u8		pri_sum_type4_th : 5;
	u8		pri_sum_type6_th : 5;
	u8		pri_sum_safe_th : 6;
	u8		pri_sum_g5_under_g1_th : 3;
	u8		pri_pw_diff_th : 3;
	u8		pri_pw_diff_fcc_th : 4;
	u8		pri_pw_diff_fcc_idle_th : 2;
	u8		pri_pw_diff_w53_th : 4;
	u8		pri_type1_low_fcc_th : 7;
	u8		pri_type1_upp_fcc_th : 7;
	u8		pri_type1_cen_fcc_th : 7;
	u8		pw_g0_th : 4;
	u8		pw_long_lower_20m_th : 4;
	u8		pw_long_lower_th : 3;
	u8		pri_long_upper_th : 6;
	u8		pw_long_sum_upper_th : 7;
	u8		pw_std_th : 4;
	u8		pw_std_idle_th : 4;
	u8		pri_std_th : 4;
	u8		pri_std_idle_th : 4;
	u8		type4_pw_max_cnt : 4;
	u8		type4_safe_pri_sum_th : 3;
};

/*@
 * ============================================================
 * enumeration
 * ============================================================
 */

enum phydm_dfs_region_domain {
	PHYDM_DFS_DOMAIN_UNKNOWN =	0,
	PHYDM_DFS_DOMAIN_FCC =		1,
	PHYDM_DFS_DOMAIN_MKK =		2,
	PHYDM_DFS_DOMAIN_ETSI =		3,
};

/*@
 * ============================================================
 * function prototype
 * ============================================================
 */
#if defined(CONFIG_PHYDM_DFS_MASTER)
void phydm_radar_detect_reset(void *dm_void);
void phydm_radar_detect_disable(void *dm_void);
void phydm_radar_detect_enable(void *dm_void);
boolean phydm_radar_detect(void *dm_void);
void phydm_dfs_histogram_radar_distinguish(void *dm_void);
boolean phydm_dfs_hist_log(void *dm_void, u8 index);
void phydm_dfs_parameter_init(void *dm_void);
void phydm_dfs_hist_dbg(void *dm_void, char input[][16], u32 *_used,
			char *output, u32 *_out_len);
void phydm_dfs_debug(void *dm_void, char input[][16], u32 *_used,
		     char *output, u32 *_out_len);
u8 phydm_dfs_polling_time(void *dm_void);
#endif /* @defined(CONFIG_PHYDM_DFS_MASTER) */

boolean
phydm_dfs_is_meteorology_channel(void *dm_void);

void
phydm_dfs_segment_distinguish(void *dm_void, enum rf_syn syn_path);

void
phydm_dfs_segment_flag_reset(void *dm_void);

boolean
phydm_is_dfs_band(void *dm_void);

boolean
phydm_dfs_master_enabled(void *dm_void);

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
void phydm_dfs_ap_reset_radar_detect_counter_and_flag(void *dm_void);
#endif
#endif

#endif /*@#ifndef __PHYDM_DFS_H__ */
