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

#ifndef __PHYDMRAINFO_H__
#define __PHYDMRAINFO_H__

/* 2019.06.28 Add legacy rate 2 spec rate API*/
#define RAINFO_VERSION "8.5"

#define	FORCED_UPDATE_RAMASK_PERIOD	5

#define	H2C_MAX_LENGTH		7

#define	RA_FLOOR_UP_GAP		3
#define	RA_FLOOR_TABLE_SIZE	7

#define	ACTIVE_TP_THRESHOLD	1
#define	RA_RETRY_DESCEND_NUM	2
#define	RA_RETRY_LIMIT_LOW	4
#define	RA_RETRY_LIMIT_HIGH	32

#define PHYDM_IS_LEGACY_RATE(rate) ((rate <= ODM_RATE54M) ? true : false)
#define PHYDM_IS_CCK_RATE(rate) ((rate <= ODM_RATE11M) ? true : false)

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	#define	FIRST_MACID	1
#else
	#define	FIRST_MACID	0
#endif

/* @1 ============================================================
 * 1 enumrate
 * 1 ============================================================
 */

enum phydm_ra_dbg_para {
	RADBG_PCR_TH_OFFSET	= 0,
	RADBG_RTY_PENALTY	= 1,
	RADBG_N_HIGH		= 2,
	RADBG_N_LOW		= 3,
	RADBG_TRATE_UP_TABLE	= 4,
	RADBG_TRATE_DOWN_TABLE	= 5,
	RADBG_TRYING_NECESSARY	= 6,
	RADBG_TDROPING_NECESSARY = 7,
	RADBG_RATE_UP_RTY_RATIO	= 8,
	RADBG_RATE_DOWN_RTY_RATIO = 9, /* u8 */

	RADBG_DEBUG_MONITOR1	= 0xc,
	RADBG_DEBUG_MONITOR2	= 0xd,
	RADBG_DEBUG_MONITOR3	= 0xe,
	RADBG_DEBUG_MONITOR4	= 0xf,
	RADBG_DEBUG_MONITOR5	= 0x10,
	NUM_RA_PARA
};

enum phydm_wireless_mode {
	PHYDM_WIRELESS_MODE_UNKNOWN	= 0x00,
	PHYDM_WIRELESS_MODE_A		= 0x01,
	PHYDM_WIRELESS_MODE_B		= 0x02,
	PHYDM_WIRELESS_MODE_G		= 0x04,
	PHYDM_WIRELESS_MODE_AUTO	= 0x08,
	PHYDM_WIRELESS_MODE_N_24G	= 0x10,
	PHYDM_WIRELESS_MODE_N_5G	= 0x20,
	PHYDM_WIRELESS_MODE_AC_5G	= 0x40,
	PHYDM_WIRELESS_MODE_AC_24G	= 0x80,
	PHYDM_WIRELESS_MODE_AC_ONLY	= 0x100,
	PHYDM_WIRELESS_MODE_MAX		= 0x800,
	PHYDM_WIRELESS_MODE_ALL		= 0xFFFF
};

enum phydm_rateid_idx {
	PHYDM_BGN_40M_2SS	= 0,
	PHYDM_BGN_40M_1SS	= 1,
	PHYDM_BGN_20M_2SS	= 2,
	PHYDM_BGN_20M_1SS	= 3,
	PHYDM_GN_N2SS		= 4,
	PHYDM_GN_N1SS		= 5,
	PHYDM_BG		= 6,
	PHYDM_G			= 7,
	PHYDM_B_20M		= 8,
	PHYDM_ARFR0_AC_2SS	= 9,
	PHYDM_ARFR1_AC_1SS	= 10,
	PHYDM_ARFR2_AC_2G_1SS	= 11,
	PHYDM_ARFR3_AC_2G_2SS	= 12,
	PHYDM_ARFR4_AC_3SS	= 13,
	PHYDM_ARFR5_N_3SS	= 14,
	PHYDM_ARFR7_N_4SS	= 15,
	PHYDM_ARFR6_AC_4SS	= 16
};

enum phydm_qam_order {
	PHYDM_QAM_CCK	= 0,
	PHYDM_QAM_BPSK	= 1,
	PHYDM_QAM_QPSK	= 2,
	PHYDM_QAM_16QAM	= 3,
	PHYDM_QAM_64QAM	= 4,
	PHYDM_QAM_256QAM = 5
};

#if (RATE_ADAPTIVE_SUPPORT == 1)/* @88E RA */

struct _phydm_txstatistic_ {
	u32	hw_total_tx;
	u32	hw_tx_success;
	u32	hw_tx_rty;
	u32	hw_tx_drop;
};

/* @1 ============================================================
 * 1  structure
 * 1 ============================================================
 */
struct _odm_ra_info_ {
	u8	rate_id;
	u32	rate_mask;
	u32	ra_use_rate;
	u8	rate_sgi;
	u8	rssi_sta_ra;
	u8	pre_rssi_sta_ra;
	u8	sgi_enable;
	u8	decision_rate;
	u8	pre_rate;
	u8	highest_rate;
	u8	lowest_rate;
	u32	nsc_up;
	u32	nsc_down;
	u16	RTY[5];
	u32	TOTAL;
	u16	DROP;
	u8	active;
	u16	rpt_time;
	u8	ra_waiting_counter;
	u8	ra_pending_counter;
	u8	ra_drop_after_down;
#if 1 /* POWER_TRAINING_ACTIVE == 1 */ /* For compile  pass only~! */
	u8	pt_active;	/* on or off */
	u8	pt_try_state;	/* @0 trying state, 1 for decision state */
	u8	pt_stage;	/* @0~6 */
	u8	pt_stop_count;	/* Stop PT counter */
	u8	pt_pre_rate;	/* @if rate change do PT */
	u8	pt_pre_rssi;	/* @if RSSI change 5% do PT */
	u8	pt_mode_ss;	/* @decide whitch rate should do PT */
	u8	ra_stage;	/* @StageRA, decide how many times RA will be done between PT */
	u8	pt_smooth_factor;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_AP) &&	((DEV_BUS_TYPE == RT_USB_INTERFACE) || (DEV_BUS_TYPE == RT_SDIO_INTERFACE))
	u8	rate_down_counter;
	u8	rate_up_counter;
	u8	rate_direction;
	u8	bounding_type;
	u8	bounding_counter;
	u8	bounding_learning_time;
	u8	rate_down_start_time;
#endif
};
#endif


struct ra_table {
	#ifdef MU_EX_MACID
	u8	mu1_rate[MU_EX_MACID];
	#endif
	u8	highest_client_tx_order;
	u16	highest_client_tx_rate_order;
	u8	power_tracking_flag;
	u8	ra_th_ofst; /*RA_threshold_offset*/
	u8	ra_ofst_direc; /*RA_offset_direction*/
	u8	up_ramask_cnt; /*@force update_ra_mask counter*/
	u8	up_ramask_cnt_tmp; /*@Just for debug, should be removed latter*/
	u32	rrsr_val_init; /*0x440*/
	u32	rrsr_val_curr; /*0x440*/
	boolean dynamic_rrsr_en;
#if 0	/*@CONFIG_RA_DYNAMIC_RTY_LIMIT*/
	u8	per_rate_retrylimit_20M[PHY_NUM_RATE_IDX];
	u8	per_rate_retrylimit_40M[PHY_NUM_RATE_IDX];
	u8	retry_descend_num;
	u8	retrylimit_low;
	u8	retrylimit_high;
#endif
	u8	ldpc_thres; /* @if RSSI > ldpc_th => switch from LPDC to BCC */
	void (*record_ra_info)(void *dm_void, u8 macid,
			       struct cmn_sta_info *sta, u64 ra_mask);
};

/* @1 ============================================================
 * 1  Function Prototype
 * 1 ============================================================
 */
boolean phydm_is_cck_rate(void *dm_void, u8 rate);

boolean phydm_is_ofdm_rate(void *dm_void, u8 rate);

boolean phydm_is_ht_rate(void *dm_void, u8 rate);

boolean phydm_is_vht_rate(void *dm_void, u8 rate);

u8 phydm_legacy_rate_2_spec_rate(void *dm_void, u8 rate);

u8 phydm_rate_2_rate_digit(void *dm_void, u8 rate);

u8 phydm_rate_type_2_num_ss(void *dm_void, enum PDM_RATE_TYPE type);

u8 phydm_rate_to_num_ss(void *dm_void, u8 data_rate);

void phydm_h2C_debug(void *dm_void, char input[][16], u32 *_used,
		     char *output, u32 *_out_len);

void phydm_ra_debug(void *dm_void, char input[][16], u32 *_used, char *output,
		    u32 *_out_len);

void odm_c2h_ra_para_report_handler(void *dm_void, u8 *cmd_buf, u8 cmd_len);

void phydm_print_rate(void *dm_void, u8 rate, u32 dbg_component);

void phydm_print_rate_2_buff(void *dm_void, u8 rate, char *buf, u16 buf_size);

void phydm_c2h_ra_report_handler(void *dm_void, u8 *cmd_buf, u8 cmd_len);

u8 phydm_rate_order_compute(void *dm_void, u8 rate_idx);

void phydm_rrsr_set_register(void *dm_void, u32 rrsr_val);

void phydm_ra_info_watchdog(void *dm_void);

void phydm_rrsr_en(void *dm_void, boolean en_rrsr);

void phydm_ra_info_init(void *dm_void);

void phydm_modify_RA_PCR_threshold(void *dm_void, u8 ra_ofst_direc,
				   u8 ra_th_ofst);

u8 phydm_vht_en_mapping(void *dm_void, u32 wireless_mode);

u8 phydm_rate_id_mapping(void *dm_void, u32 wireless_mode, u8 rf_type, u8 bw);
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
void phydm_update_hal_ra_mask(
	void *dm_void,
	u32 wireless_mode,
	u8 rf_type,
	u8 BW,
	u8 mimo_ps_enable,
	u8 disable_cck_rate,
	u32 *ratr_bitmap_msb_in,
	u32 *ratr_bitmap_in,
	u8 tx_rate_level);
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
u8 phydm_get_plcp(void *dm_void, u16 macid);
#endif

void phydm_refresh_rate_adaptive_mask(void *dm_void);

u8 phydm_get_rx_stream_num(void *dm_void, enum rf_type type);

u8 phydm_rssi_lv_dec(void *dm_void, u32 rssi, u8 ratr_state);

void odm_ra_post_action_on_assoc(void *dm);

u8 odm_find_rts_rate(void *dm_void, u8 tx_rate, boolean is_erp_protect);

void phydm_show_sta_info(void *dm_void, char input[][16], u32 *_used,
			 char *output, u32 *_out_len);

u8 phydm_get_rate_from_rssi_lv(void *dm_void, u8 sta_idx);

void phydm_ra_registed(void *dm_void, u8 macid, u8 rssi_from_assoc);

void phydm_ra_offline(void *dm_void, u8 macid);

void phydm_ra_mask_watchdog(void *dm_void);

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void odm_refresh_basic_rate_mask(
	void *dm_void);
#endif
#endif /*@#ifndef __PHYDMRAINFO_H__*/
