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

#ifndef __PHYDM_DFS_H__
#define __PHYDM_DFS_H__

#define DFS_VERSION	"1.1"

/* ============================================================
  Definition
 ============================================================
*/

/*
============================================================
1  structure
 ============================================================
*/

struct _DFS_STATISTICS {
	u8			mask_idx;
	u8			igi_cur;
	u8			igi_pre;
	u8			st_l2h_cur;
	u16			fa_count_pre;
	u16			fa_inc_hist[5];	
	u16			vht_crc_ok_cnt_pre;
	u16			ht_crc_ok_cnt_pre;
	u16			leg_crc_ok_cnt_pre;
	u16			short_pulse_cnt_pre;
	u16			long_pulse_cnt_pre;
	u8			pwdb_th;
	u8			pwdb_th_cur;
	u8			pwdb_scalar_factor;	
	u8			peak_th;
	u8			short_pulse_cnt_th;
	u8			long_pulse_cnt_th;
	u8			peak_window;
	u8			nb2wb_th;
	u8			fa_mask_th;
	u8			det_flag_offset;
	u8			st_l2h_max;
	u8			st_l2h_min;
	u8			mask_hist_checked;
	boolean		pulse_flag_hist[5];
	boolean		radar_det_mask_hist[5];
	boolean		idle_mode;
	boolean		force_TP_mode;
	boolean		dbg_mode;
	boolean		det_print;
	boolean		det_print2;
};


/* ============================================================
  enumeration
 ============================================================
*/

enum phydm_dfs_region_domain {
	PHYDM_DFS_DOMAIN_UNKNOWN = 0,
	PHYDM_DFS_DOMAIN_FCC = 1,
	PHYDM_DFS_DOMAIN_MKK = 2,
	PHYDM_DFS_DOMAIN_ETSI = 3,
};

/*
============================================================
  function prototype
============================================================
*/
#if defined(CONFIG_PHYDM_DFS_MASTER)
void phydm_radar_detect_reset(void *p_dm_void);
void phydm_radar_detect_disable(void *p_dm_void);
void phydm_radar_detect_enable(void *p_dm_void);
boolean phydm_radar_detect(void *p_dm_void);
void phydm_dfs_parameter_init(void *p_dm_void);
void phydm_dfs_debug(void *p_dm_void, u32 *const argv, u32 *_used, char *output, u32 *_out_len);
#endif /* defined(CONFIG_PHYDM_DFS_MASTER) */

boolean 
phydm_dfs_is_meteorology_channel(
	void		*p_dm_void
);

boolean
phydm_is_dfs_band(
	void		*p_dm_void
);

boolean
phydm_dfs_master_enabled(
	void		*p_dm_void
);

#endif /*#ifndef __PHYDM_DFS_H__ */
