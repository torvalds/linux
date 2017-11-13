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

#ifndef	__PHYDMDIG_H__
#define	__PHYDMDIG_H__

/*#define DIG_VERSION	"1.4"*/		/* 2017.04.18  YuChen. refine DIG code structure*/
/*#define DIG_VERSION	"2.0"*/		/* 2017.05.09  Dino. Move CCKPD to new files*/
/*#define DIG_VERSION	"2.1"*/		/* 2017.06.01  YuChen. Refine DFS condition*/
#define DIG_VERSION	"2.2"		/* 2017.06.13  YuChen. Remove MP dig*/

#define DIG_HW		0

/*--------------------Define ---------------------------------------*/

/*=== [DIG Boundary] ========================================*/
/*DIG coverage mode*/
#define		DIG_MAX_COVERAGR				0x26
#define		DIG_MIN_COVERAGE				0x1c
#define		DIG_MAX_OF_MIN_COVERAGE		0x22
/*DIG performance mode*/
#if (DIG_HW == 1)
#define		DIG_MAX_BALANCE_MODE		0x32
#else
#define		DIG_MAX_BALANCE_MODE		0x3e
#endif
#define		DIG_MAX_OF_MIN_BALANCE_MODE		0x2a

#define		DIG_MAX_PERFORMANCE_MODE		0x5a
#define		DIG_MAX_OF_MIN_PERFORMANCE_MODE		0x2a	/*from 3E -> 2A, refine by YuChen 2017/04/18*/

#define		DIG_MIN_PERFORMANCE			0x20

/*DIG DFS function*/
#define		DIG_MAX_DFS					0x28
#define		DIG_MIN_DFS					0x20

/*DIG LPS function*/
#define		DIG_MAX_LPS					0x3e
#define		DIG_MIN_LPS					0x20

/*=== [DIG FA Threshold] ======================================*/

/*Normal*/
#define		DM_DIG_FA_TH0					500
#define		DM_DIG_FA_TH1					750

/*LPS*/
#define		DM_DIG_FA_TH0_LPS			4	/* -> 4 lps */
#define		DM_DIG_FA_TH1_LPS			15	/* -> 15 lps */
#define		DM_DIG_FA_TH2_LPS			30	/* -> 30 lps */

#define		RSSI_OFFSET_DIG_LPS			5

/*LNA saturation check*/
#define OFDM_AGC_TAB_0			0
#define	OFDM_AGC_TAB_2			2
#define	DIFF_RSSI_TO_IGI		10
#define	ONE_SEC_MS				1000

/*--------------------Enum-----------------------------------*/
enum dig_goupcheck_level {
	DIG_GOUPCHECK_LEVEL_0,
	DIG_GOUPCHECK_LEVEL_1,
	DIG_GOUPCHECK_LEVEL_2
};

enum phydm_dig_mode {
	PHYDM_DIG_PERFORAMNCE_MODE	= 0,
	PHYDM_DIG_COVERAGE_MODE	= 1,
};

enum lna_sat_timer_state {
	INIT_LNA_SAT_CHK_TIMMER,
	CANCEL_LNA_SAT_CHK_TIMMER,
	RELEASE_LNA_SAT_CHK_TIMMER
};
/*--------------------Define Struct-----------------------------------*/

struct phydm_dig_struct {

	boolean	is_ignore_dig; /*for old pause function*/
	boolean	is_dbg_fa_th;
	u8		dig_mode_decision;
	u8		cur_ig_value;
	u8		rvrt_val;
	u8		igi_backup;
	u8		rx_gain_range_max;	/*dig_dynamic_max*/
	u8		rx_gain_range_min;	/*dig_dynamic_min*/
	u8		dm_dig_max;			/*Absolutly upper bound*/
	u8		dm_dig_min;			/*Absolutly lower bound*/
	u8		dig_max_of_min;		/*Absolutly max of min*/
	boolean	is_media_connect;
	u32		ant_div_rssi_max;
	u8		*is_p2p_in_process;
	u8		pause_lv_bitmap; /*bit-map of pause level*/
	u8		pause_dig_value[PHYDM_PAUSE_MAX_NUM];
	enum dig_goupcheck_level		dig_go_up_check_level;
	u8		aaa_default;
	u16		fa_th[3];
#if (RTL8822B_SUPPORT == 1 || RTL8197F_SUPPORT == 1 || RTL8821C_SUPPORT == 1)
	u8		rf_gain_idx;
	u8		agc_table_idx;
	u8		big_jump_lmt[16];
	u8		enable_adjust_big_jump:1;
	u8		big_jump_step1:3;
	u8		big_jump_step2:2;
	u8		big_jump_step3:2;
#endif
	u8		dig_upcheck_initial_value;
	u8		dig_level0_ratio_reciprocal;
	u8		dig_level1_ratio_reciprocal;
#ifdef PHYDM_TDMA_DIG_SUPPORT
	u8		cur_ig_value_tdma;
	u8		low_ig_value;
	u8		tdma_dig_state;	/*To distinguish which state is now.(L-sate or H-state)*/
	u8		tdma_dig_cnt;	/*for phydm_tdma_dig_timer_check use*/
	u8		pre_tdma_dig_cnt;
	u8		sec_factor;
	u32		cur_timestamp;
	u32		pre_timestamp;
	u32		fa_start_timestamp;
	u32		fa_end_timestamp;
	u32		fa_acc_1sec_timestamp;
#endif	
};

struct phydm_fa_struct {
	u32		cnt_parity_fail;
	u32		cnt_rate_illegal;
	u32		cnt_crc8_fail;
	u32		cnt_mcs_fail;
	u32		cnt_ofdm_fail;
	u32		cnt_ofdm_fail_pre;	/* For RTL8881A */
	u32		cnt_cck_fail;
	u32		cnt_all;
	u32		cnt_all_pre;
	u32		cnt_fast_fsync;
	u32		cnt_sb_search_fail;
	u32		cnt_ofdm_cca;
	u32		cnt_cck_cca;
	u32		cnt_cca_all;
	u32		cnt_bw_usc;
	u32		cnt_bw_lsc;
	u32		cnt_cck_crc32_error;
	u32		cnt_cck_crc32_ok;
	u32		cnt_ofdm_crc32_error;
	u32		cnt_ofdm_crc32_ok;
	u32		cnt_ht_crc32_error;
	u32		cnt_ht_crc32_ok;
	u32		cnt_ht_crc32_error_agg;
	u32		cnt_ht_crc32_ok_agg;
	u32		cnt_vht_crc32_error;
	u32		cnt_vht_crc32_ok;
	u32		cnt_crc32_error_all;
	u32		cnt_crc32_ok_all;
	boolean	cck_block_enable;
	boolean	ofdm_block_enable;
	u32		dbg_port0;
	boolean	edcca_flag;
};

#ifdef PHYDM_TDMA_DIG_SUPPORT
struct phydm_fa_acc_struct {
	u32		cnt_parity_fail;
	u32		cnt_rate_illegal;
	u32		cnt_crc8_fail;
	u32		cnt_mcs_fail;
	u32		cnt_ofdm_fail;
	u32		cnt_ofdm_fail_pre;	/*For RTL8881A*/
	u32		cnt_cck_fail;
	u32		cnt_all;
	u32		cnt_all_pre;
	u32		cnt_fast_fsync;
	u32		cnt_sb_search_fail;
	u32		cnt_ofdm_cca;
	u32		cnt_cck_cca;
	u32		cnt_cca_all;
	u32		cnt_cck_crc32_error;
	u32		cnt_cck_crc32_ok;
	u32		cnt_ofdm_crc32_error;
	u32		cnt_ofdm_crc32_ok;
	u32		cnt_ht_crc32_error;
	u32		cnt_ht_crc32_ok;
	u32		cnt_vht_crc32_error;
	u32		cnt_vht_crc32_ok;
	u32		cnt_crc32_error_all;
	u32		cnt_crc32_ok_all;
	u32		cnt_all_1sec;
	u32		cnt_cca_all_1sec;
	u32		cnt_cck_fail_1sec;
};

#endif	/*#ifdef PHYDM_TDMA_DIG_SUPPORT*/

struct phydm_lna_sat_info_struct {
	u32			sat_cnt_acc_patha;
	u32			sat_cnt_acc_pathb;
	u32			check_time;
	boolean		pre_sat_status;
	boolean		cur_sat_status;
	struct timer_list	phydm_lna_sat_chk_timer;
	u32			cur_timer_check_cnt;
	u32			pre_timer_check_cnt;
};

/*--------------------Function declaration-----------------------------*/
void
odm_write_dig(
	void					*p_dm_void,
	u8					current_igi
);

void
phydm_set_dig_val(
	void			*p_dm_void,
	u32			*val_buf,
	u8			val_len
);

void
odm_pause_dig(
	void					*p_dm_void,
	enum phydm_pause_type		pause_type,
	enum phydm_pause_level		pause_level,
	u8					igi_value
);

void
phydm_dig_init(
	void					*p_dm_void
);

void
phydm_dig(
	void					*p_dm_void
);

void
phydm_dig_lps_32k(
	void		*p_dm_void
);

void
phydm_dig_by_rssi_lps(
	void					*p_dm_void
);

void
odm_false_alarm_counter_statistics(
	void					*p_dm_void
);

#ifdef PHYDM_TDMA_DIG_SUPPORT
void
phydm_set_tdma_dig_timer(
	void					*p_dm_void
);

void
phydm_tdma_dig_timer_check(
	void					*p_dm_void
);

void
phydm_tdma_dig(
	void		*p_dm_void
);

void
phydm_tdma_false_alarm_counter_check(
	void		*p_dm_void
);

void
phydm_tdma_dig_add_interrupt_mask_handler(
	void		*p_dm_void
);

void
phydm_false_alarm_counter_reset(
	void		*p_dm_void
);

void
phydm_false_alarm_counter_acc(
	void		*p_dm_void,
	boolean		rssi_dump_en
	);

void
phydm_false_alarm_counter_acc_reset(
	void		*p_dm_void
	);

#endif	/*#ifdef PHYDM_TDMA_DIG_SUPPORT*/

void
phydm_set_ofdm_agc_tab(
	void	*p_dm_void,
	u8		tab_sel
);

#ifdef PHYDM_LNA_SAT_CHK_SUPPORT
u8
phydm_get_ofdm_agc_tab(
	void	*p_dm_void
);

void
phydm_lna_sat_chk(
	void		*p_dm_void
);

void
phydm_lna_sat_chk_timers(
	void		*p_dm_void,
	u8			state
);

void
phydm_lna_sat_chk_watchdog(
	void		*p_dm_void
);

#endif	/*#if (PHYDM_LNA_SAT_CHK_SUPPORT == 1)*/

void
phydm_dig_debug(
	void		*p_dm_void,
	char		input[][16],
	u32		*_used,
	char		*output,
	u32		*_out_len,
	u32		input_num
);

#endif
