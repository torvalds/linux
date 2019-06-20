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

#ifndef __PHYDMDIG_H__
#define __PHYDMDIG_H__

#define DIG_VERSION "2.3"

#define	DIG_HW		0
#define DIG_LIMIT_PERIOD 60 /*@60 sec*/

/*@--------------------Define ---------------------------------------*/

/*@=== [DIG Boundary] ========================================*/
/*@DIG coverage mode*/
#define	DIG_MAX_COVERAGR		0x26
#define	DIG_MIN_COVERAGE		0x1c
#define	DIG_MAX_OF_MIN_COVERAGE		0x22

/*@[DIG Balance mode]*/
#if (DIG_HW == 1)
#define	DIG_MAX_BALANCE_MODE		0x32
#else
#define	DIG_MAX_BALANCE_MODE		0x3e
#endif
#define	DIG_MAX_OF_MIN_BALANCE_MODE	0x2a

/*@[DIG Performance mode]*/
#define	DIG_MAX_PERFORMANCE_MODE	0x5a
#define	DIG_MAX_OF_MIN_PERFORMANCE_MODE	0x40	/*@[WLANBB-871]*/
#define	DIG_MIN_PERFORMANCE		0x20

/*@DIG DFS function*/
#define	DIG_MAX_DFS			0x28
#define	DIG_MIN_DFS			0x20

/*@DIG LPS function*/
#define	DIG_MAX_LPS			0x3e
#define	DIG_MIN_LPS			0x20

#ifdef PHYDM_TDMA_DIG_SUPPORT
#define DIG_NUM_OF_TDMA_STATES	2 /*@L, H state*/
#define DIG_TIMER_MS			250
#define	ONE_SEC_MS			1000
#endif

/*@=== [DIG FA Threshold] ======================================*/

/*Normal*/
#define	DM_DIG_FA_TH0			500
#define	DM_DIG_FA_TH1			750

/*@LPS*/
#define	DM_DIG_FA_TH0_LPS		4	/* @-> 4 lps */
#define	DM_DIG_FA_TH1_LPS		15	/* @-> 15 lps */
#define	DM_DIG_FA_TH2_LPS		30	/* @-> 30 lps */

#define	RSSI_OFFSET_DIG_LPS		5
#define DIG_RECORD_NUM			4

/*@--------------------Enum-----------------------------------*/
enum dig_goupcheck_level {
	DIG_GOUPCHECK_LEVEL_0,
	DIG_GOUPCHECK_LEVEL_1,
	DIG_GOUPCHECK_LEVEL_2
};

enum phydm_dig_mode {
	PHYDM_DIG_PERFORAMNCE_MODE	= 0,
	PHYDM_DIG_COVERAGE_MODE		= 1,
};

#ifdef PHYDM_TDMA_DIG_SUPPORT
enum upd_type {
	ENABLE_TDMA,
	MODE_DECISION
};

enum tdma_opmode {
	MODE_PERFORMANCE = 1,
	MODE_COVERAGE = 2
};

#ifdef IS_USE_NEW_TDMA
enum tdma_dig_timer {
	INIT_TDMA_DIG_TIMMER,
	CANCEL_TDMA_DIG_TIMMER,
	RELEASE_TDMA_DIG_TIMMER
};

enum tdma_dig_state {
	TDMA_DIG_LOW_STATE = 0,
	TDMA_DIG_HIGH_STATE = 1,
	NORMAL_DIG = 2
};
#endif
#endif

/*@--------------------Define Struct-----------------------------------*/
#ifdef CFG_DIG_DAMPING_CHK
struct phydm_dig_recorder_strcut {
	u8		igi_bitmap; /*@Don't add any new parameter before this*/
	u8		igi_history[DIG_RECORD_NUM];
	u32		fa_history[DIG_RECORD_NUM];
	u8		damping_limit_en;
	u8		damping_limit_val; /*@Limit IGI_dyn_min*/
	u32		limit_time;
	u8		limit_rssi;
};
#endif

struct phydm_mcc_dig {
	u8		mcc_rssi_A;
	u8		mcc_rssi_B;
};

struct phydm_dig_struct {
#ifdef CFG_DIG_DAMPING_CHK
	struct phydm_dig_recorder_strcut dig_recorder_t;
	u8		dig_dl_en; /*@damping limit function enable*/
#endif
	boolean		is_dbg_fa_th;
	u8		cur_ig_value;
	u8		rvrt_val;
	u8		igi_backup;
	u8		rx_gain_range_max;	/*@dig_dynamic_max*/
	u8		rx_gain_range_min;	/*@dig_dynamic_min*/
	u8		dm_dig_max;		/*@Absolutly upper bound*/
	u8		dm_dig_min;		/*@Absolutly lower bound*/
	u8		dig_max_of_min;		/*@Absolutly max of min*/
	boolean		is_media_connect;
	u32		ant_div_rssi_max;
	u8		*is_p2p_in_process;
	enum dig_goupcheck_level	go_up_chk_lv;
	u16		fa_th[3];
#if (RTL8822B_SUPPORT || RTL8197F_SUPPORT || RTL8821C_SUPPORT ||\
	RTL8198F_SUPPORT || RTL8192F_SUPPORT || RTL8195B_SUPPORT ||\
	RTL8822C_SUPPORT || RTL8814B_SUPPORT || RTL8721D_SUPPORT ||\
	RTL8812F_SUPPORT || RTL8197G_SUPPORT)
	u8		rf_gain_idx;
	u8		agc_table_idx;
	u8		big_jump_lmt[16];
	u8		enable_adjust_big_jump:1;
	u8		big_jump_step1:3;
	u8		big_jump_step2:2;
	u8		big_jump_step3:2;
#endif
	u8		upcheck_init_val;
	u8		lv0_ratio_reciprocal;
	u8		lv1_ratio_reciprocal;
#ifdef PHYDM_TDMA_DIG_SUPPORT
	u8		cur_ig_value_tdma;
	u8		low_ig_value;
	u8		tdma_dig_state;	/*@To distinguish which state is now.(L-sate or H-state)*/
	u8		tdma_dig_cnt;	/*@for phydm_tdma_dig_timer_check use*/
	u8		pre_tdma_dig_cnt;
	u8		sec_factor;
	u32		cur_timestamp;
	u32		pre_timestamp;
	u32		fa_start_timestamp;
	u32		fa_end_timestamp;
	u32		fa_acc_1sec_timestamp;
#ifdef IS_USE_NEW_TDMA
	u8		tdma_dig_block_cnt;/*@for 1 second dump indicator use*/
			/*@dynamic upper bound for L/H state*/
	u8		tdma_rx_gain_max[DIG_NUM_OF_TDMA_STATES];
			/*@dynamic lower bound for L/H state*/
	u8		tdma_rx_gain_min[DIG_NUM_OF_TDMA_STATES];
			/*To distinguish current state(L-sate or H-state)*/
#endif
#endif
};

struct phydm_fa_struct {
	u32		cnt_parity_fail;
	u32		cnt_rate_illegal;
	u32		cnt_crc8_fail;
	u32		cnt_crc8_fail_vht;
	u32		cnt_mcs_fail;
	u32		cnt_mcs_fail_vht;
	u32		cnt_ofdm_fail;
	u32		cnt_ofdm_fail_pre;	/* @For RTL8881A */
	u32		cnt_cck_fail;
	u32		cnt_all;
	u32		cnt_all_accumulated;
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
	u32		time_fa_all;
	boolean		cck_block_enable;
	boolean		ofdm_block_enable;
	u32		dbg_port0;
	boolean		edcca_flag;
};

#ifdef PHYDM_TDMA_DIG_SUPPORT
struct phydm_fa_acc_struct {
	u32		cnt_parity_fail;
	u32		cnt_rate_illegal;
	u32		cnt_crc8_fail;
	u32		cnt_mcs_fail;
	u32		cnt_ofdm_fail;
	u32		cnt_ofdm_fail_pre;	/*@For RTL8881A*/
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

#endif	/*@#ifdef PHYDM_TDMA_DIG_SUPPORT*/

/*@--------------------Function declaration-----------------------------*/
void phydm_write_dig_reg(void *dm_void, u8 igi);

void odm_write_dig(void *dm_void, u8 current_igi);

u8 phydm_get_igi(void *dm_void, enum bb_path path);

void phydm_set_dig_val(void *dm_void, u32 *val_buf, u8 val_len);

void odm_pause_dig(void *dm_void, enum phydm_pause_type pause_type,
		   enum phydm_pause_level pause_level, u8 igi_value);

void phydm_dig_init(void *dm_void);

void phydm_dig(void *dm_void);

void phydm_dig_lps_32k(void *dm_void);

void phydm_dig_by_rssi_lps(void *dm_void);

void phydm_false_alarm_counter_statistics(void *dm_void);

#ifdef PHYDM_TDMA_DIG_SUPPORT
void phydm_set_tdma_dig_timer(void *dm_void);

void phydm_tdma_dig_timer_check(void *dm_void);

void phydm_tdma_dig(void *dm_void);

void phydm_tdma_false_alarm_counter_check(void *dm_void);

void phydm_tdma_dig_add_interrupt_mask_handler(void *dm_void);

void phydm_false_alarm_counter_reset(void *dm_void);

void phydm_false_alarm_counter_acc(void *dm_void, boolean rssi_dump_en);

void phydm_false_alarm_counter_acc_reset(void *dm_void);

void phydm_tdma_dig_para_upd(void *dm_void, enum upd_type type, u8 input);

#ifdef IS_USE_NEW_TDMA
void phydm_tdma_dig_timers(void *dm_void, u8 state);

void phydm_tdma_dig_cbk(void *dm_void);

void phydm_tdma_dig_workitem_callback(void *dm_void);

void phydm_tdma_fa_cnt_chk(void *dm_void);

void phydm_tdma_low_dig(void *dm_void);

void phydm_tdma_high_dig(void *dm_void);

void phydm_fa_cnt_acc(void *dm_void, boolean rssi_dump_en,
		      u8 cur_tdma_dig_state);
#endif /*@#ifdef IS_USE_NEW_TDMA*/
#endif /*@#ifdef PHYDM_TDMA_DIG_SUPPORT*/

void phydm_set_ofdm_agc_tab(void *dm_void, u8 tab_sel);

void phydm_dig_debug(void *dm_void, char input[][16], u32 *_used, char *output,
		     u32 *_out_len);

#ifdef CONFIG_MCC_DM
void phydm_mcc_igi_cal(void *dm_void);
#endif


#endif
