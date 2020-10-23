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

#ifndef __PHYDM_LNA_SAT_H__
#define __PHYDM_LNA_SAT_H__
#ifdef PHYDM_LNA_SAT_CHK_SUPPORT
/* @1 ============================================================
 * 1  Definition
 * 1 ============================================================
 */

#define LNA_SAT_VERSION "1.1"

/*@LNA saturation check*/
#define	OFDM_AGC_TAB_0			0
#define	OFDM_AGC_TAB_2			2

#define	DIFF_RSSI_TO_IGI		10
#define	ONE_SEC_MS			1000

#define LNA_CHK_PERIOD			100 /*@ms*/
#define LNA_CHK_CNT			10 /*@checks per callback*/
#define LNA_CHK_DUTY_CYCLE		5 /*@percentage*/

#define	DELTA_STD	2
#define	DELTA_MEAN	2
#define	SNR_STATISTIC_SHIFT	8
#define	SNR_RPT_MAX	256

/* @1 ============================================================
 * 1 enumrate
 * 1 ============================================================
 */

enum lna_sat_timer_state {
	INIT_LNA_SAT_CHK_TIMMER,
	CANCEL_LNA_SAT_CHK_TIMMER,
	RELEASE_LNA_SAT_CHK_TIMMER
};

#ifdef PHYDM_LNA_SAT_CHK_TYPE2
enum lna_sat_chk_type2_status {
	ORI_TABLE_MONITOR,
	ORI_TABLE_TRAINING,
	SAT_TABLE_MONITOR,
	SAT_TABLE_TRAINING,
	SAT_TABLE_TRY_FAIL,
	ORI_TABLE_TRY_FAIL
};

#endif

enum lna_sat_type {
	LNA_SAT_WITH_PEAK_DET	= 1,	/*type1*/
	LNA_SAT_WITH_TRAIN	= 2,	/*type2*/
};

#ifdef PHYDM_HW_SWITCH_AGC_TAB
enum lna_pd_th_level {
	LNA_PD_TH_LEVEL0	= 0,
	LNA_PD_TH_LEVEL1	= 1,
	LNA_PD_TH_LEVEL2	= 2,
	LNA_PD_TH_LEVEL3	= 3
};

enum agc_tab_switch_state {
	AGC_SWH_IDLE,
	AGC_SWH_CCK,
	AGC_SWH_OFDM
};
#endif

/* @1 ============================================================
 * 1  structure
 * 1 ============================================================
 */

struct phydm_lna_sat_t {
#ifdef PHYDM_LNA_SAT_CHK_TYPE1
	u8			chk_cnt;
	u8			chk_duty_cycle;
	u32			chk_period;/*@ms*/
	boolean			is_disable_lna_sat_chk;
	boolean			dis_agc_table_swh;
#endif
#ifdef PHYDM_LNA_SAT_CHK_TYPE2
	u8			force_traget_macid;
	u32			snr_var_thd;
	u32			delta_snr_mean;
	u16			ori_table_try_fail_times;
	u16			cnt_lower_snr_statistic;
	u16			sat_table_monitor_times;
	u16			force_change_period;
	u8			is_snr_detail_en;
	u8			is_force_lna_sat_table;
	u8			lwr_snr_ratio_bit_shift;
	u8			cnt_snr_statistic;
	u16			snr_statistic_sqr[SNR_RPT_MAX];
	u8			snr_statistic[SNR_RPT_MAX];
	u8			is_sm_done;
	u8			is_snr_done;
	u32			cur_snr_var;
	u8			total_bit_shift;
	u8			total_cnt_snr;
	u32			cur_snr_mean;
	u8			cur_snr_var0;
	u32			cur_lower_snr_mean;
	u32			pre_snr_mean;
	u32			pre_snr_var;
	u32			pre_lower_snr_mean;
	u8			nxt_state;
	u8			pre_state;
#endif
	enum lna_sat_type	lna_sat_type;
	u32			sat_cnt_acc_patha;
	u32			sat_cnt_acc_pathb;
#ifdef PHYDM_IC_ABOVE_3SS
	u32			sat_cnt_acc_pathc;
#endif
#ifdef PHYDM_IC_ABOVE_4SS
	u32			sat_cnt_acc_pathd;
#endif
	u32			check_time;
	boolean			pre_sat_status;
	boolean			cur_sat_status;
#ifdef PHYDM_HW_SWITCH_AGC_TAB
	boolean			hw_swh_tab_on;
	enum odm_rf_band	cur_rf_band;
#endif
	struct phydm_timer_list	phydm_lna_sat_chk_timer;
	u32			cur_timer_check_cnt;
	u32			pre_timer_check_cnt;
};

/* @1 ============================================================
 * 1 function prototype
 * 1 ============================================================
 */
void phydm_lna_sat_chk_init(void *dm_void);

u8 phydm_get_ofdm_agc_tab(void *dm_void);

void phydm_lna_sat_chk(void *dm_void);

void phydm_lna_sat_chk_timers(void *dm_void, u8 state);

#ifdef PHYDM_LNA_SAT_CHK_TYPE1
#if (RTL8198F_SUPPORT || RTL8814B_SUPPORT)
void phydm_lna_sat_chk_bb_init(void *dm_void);

void phydm_set_ofdm_agc_tab_path(void *dm_void,
				 u8 tab_sel, enum rf_path path);

u8 phydm_get_ofdm_agc_tab_path(void *dm_void, enum rf_path path);
#endif /*@#if (RTL8198F_SUPPORT || RTL8814B_SUPPORT)*/
#endif

#ifdef PHYDM_LNA_SAT_CHK_TYPE2
void phydm_parsing_snr(void *dm_void, void *pktinfo_void, s8 *rx_snr);
#endif

void phydm_lna_sat_debug(void *dm_void, char input[][16], u32 *_used,
			 char *output, u32 *_out_len);

void phydm_lna_sat_chk_watchdog(void *dm_void);

void phydm_lna_sat_check_init(void *dm_void);

#ifdef PHYDM_HW_SWITCH_AGC_TAB
void phydm_auto_agc_tab_debug(void *dm_void, char input[][16], u32 *_used,
			      char *output, u32 *_out_len);
#endif
#endif /*@#if (PHYDM_LNA_SAT_CHK_SUPPORT == 1)*/
#endif
