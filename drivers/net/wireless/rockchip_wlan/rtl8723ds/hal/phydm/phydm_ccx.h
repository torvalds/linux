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

#ifndef __PHYDMCCX_H__
#define __PHYDMCCX_H__

/* 2019.07.17 Disable 1db_mode when nhm Basic-Trigger or watchdog*/
#define CCX_VERSION "3.3"

/* @1 ============================================================
 * 1  Definition
 * 1 ============================================================
 */
#define CCX_EN 1

#define	MAX_ENV_MNTR_TIME	8	/*second*/
#define	IGI_TO_NHM_TH_MULTIPLIER 2
#define	MS_TO_4US_RATIO		250
#define	CCA_CAP			14
#define	CLM_MAX_REPORT_TIME	10
#define	DEVIDER_ERROR		0xffff
#define CLM_PERIOD_MAX		65535
#define NHM_PERIOD_MAX		65534
#define	NHM_TH_NUM		11	/*threshold number of NHM*/
#define	NHM_RPT_NUM		12
#ifdef NHM_DYM_PW_TH_SUPPORT
#define	DYM_PWTH_CCA_CAP	24
#endif

#define	IGI_2_NHM_TH(igi)	((igi) << 1)/*NHM_threshold = IGI * 2*/
#define	NTH_TH_2_RSSI(th)	((th >> 1) - 10)

/*@FAHM*/
#define	FAHM_INCLD_FA		BIT(0)
#define	FAHM_INCLD_CRC_OK	BIT(1)
#define	FAHM_INCLD_CRC_ER	BIT(2)

#define NHM_SUCCESS		BIT(0)
#define CLM_SUCCESS		BIT(1)
#define FAHM_SUCCESS		BIT(2)
#define	ENV_MNTR_FAIL		0xff

/* @1 ============================================================
 * 1 enumrate
 * 1 ============================================================
 */
enum phydm_clm_level {
	CLM_RELEASE		= 0,
	CLM_LV_1		= 1,	/* @Low Priority function */
	CLM_LV_2		= 2,	/* @Middle Priority function */
	CLM_LV_3		= 3,	/* @High priority function (ex: Check hang function) */
	CLM_LV_4		= 4,	/* @Debug function (the highest priority) */
	CLM_MAX_NUM		= 5
};

enum phydm_nhm_level {
	NHM_RELEASE		= 0,
	NHM_LV_1		= 1,	/* @Low Priority function */
	NHM_LV_2		= 2,	/* @Middle Priority function */
	NHM_LV_3		= 3,	/* @High priority function (ex: Check hang function) */
	NHM_LV_4		= 4,	/* @Debug function (the highest priority) */
	NHM_MAX_NUM		= 5
};

enum nhm_divider_opt_all {
	NHM_CNT_ALL		= 0,	/*nhm SUM report <= 255*/
	NHM_VALID		= 1,	/*nhm SUM report = 255*/
	NHM_CNT_INIT
};

enum nhm_setting {
	SET_NHM_SETTING,
	STORE_NHM_SETTING,
	RESTORE_NHM_SETTING
};

enum nhm_option_cca_all {
	NHM_EXCLUDE_CCA		= 0,
	NHM_INCLUDE_CCA		= 1,
	NHM_CCA_INIT
};

enum nhm_option_txon_all {
	NHM_EXCLUDE_TXON	= 0,
	NHM_INCLUDE_TXON	= 1,
	NHM_TXON_INIT
};

enum nhm_application {
	NHM_BACKGROUND		= 0,/*@default*/
	NHM_ACS			= 1,
	IEEE_11K_HIGH		= 2,
	IEEE_11K_LOW		= 3,
	INTEL_XBOX		= 4,
	NHM_DBG			= 5, /*@manual trigger*/
};

enum clm_application {
	CLM_BACKGROUND		= 0,/*@default*/
	CLM_ACS			= 1,
};

enum clm_monitor_mode {
	CLM_DRIVER_MNTR		= 1,
	CLM_FW_MNTR		= 2
};

/* @1 ============================================================
 * 1  structure
 * 1 ============================================================
 */
struct env_trig_rpt {
	u8			nhm_rpt_stamp;
	u8			clm_rpt_stamp;
};


struct env_mntr_rpt {
	u8			nhm_ratio;
	u8			nhm_result[NHM_RPT_NUM];
	u8			clm_ratio;
	u8			nhm_rpt_stamp;
	u8			clm_rpt_stamp;
	u8			nhm_noise_pwr;
};

struct nhm_para_info {
	enum nhm_option_txon_all	incld_txon;	/*@Include TX on*/
	enum nhm_option_cca_all		incld_cca;	/*@Include CCA*/
	enum nhm_divider_opt_all	div_opt;	/*@divider option*/
	enum nhm_application		nhm_app;
	enum phydm_nhm_level		nhm_lv;
	u16				mntr_time;	/*@0~262 unit ms*/
	boolean				en_1db_mode;
	u8				nhm_th0_manual;	/* for 1-db mode*/
};

struct clm_para_info {
	enum clm_application		clm_app;
	enum phydm_clm_level		clm_lv;
	u16				mntr_time;	/*@0~262 unit ms*/
};

struct ccx_info {
	u32			nhm_trigger_time;
	u32			clm_trigger_time;
	u64			start_time;	/*@monitor for the test duration*/
#ifdef NHM_SUPPORT
	enum nhm_application		nhm_app;
	enum nhm_option_txon_all	nhm_include_txon;
	enum nhm_option_cca_all		nhm_include_cca;
	enum nhm_divider_opt_all 	nhm_divider_opt;
	/*Report*/
	u8			nhm_th[NHM_TH_NUM];
	u8			nhm_result[NHM_RPT_NUM];
	u16			nhm_period;	/* @4us per unit */
	u8			nhm_igi;
	u8			nhm_manual_ctrl;
	u8			nhm_ratio;	/*@1% per nuit, it means the interference igi can't overcome.*/
	u8			nhm_rpt_sum;
	u16			nhm_duration;	/*@Real time of NHM_VALID */
	u8			nhm_set_lv;
	boolean			nhm_ongoing;
	u8			nhm_rpt_stamp;
	u8			nhm_level;
	u8			nhm_level_valid;
#ifdef NHM_DYM_PW_TH_SUPPORT
	boolean			nhm_dym_pw_th_en;
	boolean			dym_pwth_manual_ctrl;
	u8			pw_th_rf20_ori;
	u8			pw_th_rf20_cur;
	u8			nhm_pw_th_max;
	u8			nhm_period_decre;
	u8			nhm_sl_pw_th;
#endif
#endif

#ifdef CLM_SUPPORT
	enum clm_application	clm_app;
	u8			clm_manual_ctrl;
	u8			clm_set_lv;
	boolean			clm_ongoing;
	u16			clm_period;	/* @4us per unit */
	u16			clm_result;
	u8			clm_ratio;
	u32			clm_fw_result_acc;
	u8			clm_fw_result_cnt;
	enum clm_monitor_mode	clm_mntr_mode;
	u8			clm_rpt_stamp;
#endif
#ifdef FAHM_SUPPORT
	boolean			fahm_ongoing;
	u8			env_mntr_igi;
	u8			fahm_nume_sel;	/*@fahm_numerator_sel: select {FA, CRCOK, CRC_fail} */
	u8			fahm_denom_sel;	/*@fahm_denominator_sel: select {FA, CRCOK, CRC_fail} */
	u16			fahm_period;	/*unit: 4us*/
#endif
};

/* @1 ============================================================
 * 1 Function Prototype
 * 1 ============================================================
 */

#ifdef FAHM_SUPPORT
void phydm_fahm_init(void *dm_void);

void phydm_fahm_dbg(void *dm_void, char input[][16], u32 *_used, char *output,
		    u32 *_out_len);
#endif

/*@NHM*/
#ifdef NHM_SUPPORT
void phydm_nhm_dbg(void *dm_void, char input[][16], u32 *_used, char *output,
		   u32 *_out_len);
u8 phydm_get_igi(void *dm_void, enum bb_path path);
#endif

/*@CLM*/
#ifdef CLM_SUPPORT
void phydm_clm_c2h_report_handler(void *dm_void, u8 *cmd_buf, u8 cmd_len);

void phydm_clm_dbg(void *dm_void, char input[][16], u32 *_used, char *output,
		   u32 *_out_len);
#endif

u8 phydm_env_mntr_trigger(void *dm_void, struct nhm_para_info *nhm_para,
			  struct clm_para_info *clm_para,
			  struct env_trig_rpt *rpt);

u8 phydm_env_mntr_result(void *dm_void, struct env_mntr_rpt *rpt);

void phydm_env_mntr_watchdog(void *dm_void);

void phydm_env_monitor_init(void *dm_void);

void phydm_env_mntr_dbg(void *dm_void, char input[][16], u32 *_used,
			char *output, u32 *_out_len);
#endif
