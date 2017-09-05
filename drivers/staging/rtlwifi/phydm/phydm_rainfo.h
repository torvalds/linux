/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
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

/*#define RAINFO_VERSION	"2.0"*/ /*2014.11.04*/
/*#define RAINFO_VERSION	"3.0"*/ /*2015.01.13 Dino*/
/*#define RAINFO_VERSION	"3.1"*/ /*2015.01.14 Dino*/
/*#define RAINFO_VERSION	"3.3"*/ /*2015.07.29 YuChen*/
/*#define RAINFO_VERSION	"3.4"*/ /*2015.12.15 Stanley*/
/*#define RAINFO_VERSION	"4.0"*/ /*2016.03.24 Dino, Add more RA mask
					  *state and Phydm-lize partial ra mask
					  *function
					  */
/*#define RAINFO_VERSION	"4.1"*/ /*2016.04.20 Dino, Add new function to
					  *adjust PCR RA threshold
					  */
/*#define RAINFO_VERSION	"4.2"*/ /*2016.05.17 Dino, Add H2C debug cmd */
#define RAINFO_VERSION "4.3" /*2016.07.11 Dino, Fix RA hang in CCK 1M problem*/

#define FORCED_UPDATE_RAMASK_PERIOD 5

#define H2C_0X42_LENGTH 5
#define H2C_MAX_LENGTH 7

#define RA_FLOOR_UP_GAP 3
#define RA_FLOOR_TABLE_SIZE 7

#define ACTIVE_TP_THRESHOLD 150
#define RA_RETRY_DESCEND_NUM 2
#define RA_RETRY_LIMIT_LOW 4
#define RA_RETRY_LIMIT_HIGH 32

#define RAINFO_BE_RX_STATE BIT(0) /* 1:RX    */ /* ULDL */
#define RAINFO_STBC_STATE BIT(1)
/* #define RAINFO_LDPC_STATE			BIT2 */
#define RAINFO_NOISY_STATE BIT(2) /* set by Noisy_Detection */
#define RAINFO_SHURTCUT_STATE BIT(3)
#define RAINFO_SHURTCUT_FLAG BIT(4)
#define RAINFO_INIT_RSSI_RATE_STATE BIT(5)
#define RAINFO_BF_STATE BIT(6)
#define RAINFO_BE_TX_STATE BIT(7) /* 1:TX */

#define RA_MASK_CCK 0xf
#define RA_MASK_OFDM 0xff0
#define RA_MASK_HT1SS 0xff000
#define RA_MASK_HT2SS 0xff00000
/*#define	RA_MASK_MCS3SS	*/
#define RA_MASK_HT4SS 0xff0
#define RA_MASK_VHT1SS 0x3ff000
#define RA_MASK_VHT2SS 0xffc00000

#define RA_FIRST_MACID 0

#define ap_init_rate_adaptive_state odm_rate_adaptive_state_ap_init

#define DM_RATR_STA_INIT 0
#define DM_RATR_STA_HIGH 1
#define DM_RATR_STA_MIDDLE 2
#define DM_RATR_STA_LOW 3
#define DM_RATR_STA_ULTRA_LOW 4

enum phydm_ra_arfr_num {
	ARFR_0_RATE_ID = 0x9,
	ARFR_1_RATE_ID = 0xa,
	ARFR_2_RATE_ID = 0xb,
	ARFR_3_RATE_ID = 0xc,
	ARFR_4_RATE_ID = 0xd,
	ARFR_5_RATE_ID = 0xe
};

enum phydm_ra_dbg_para {
	RADBG_PCR_TH_OFFSET = 0,
	RADBG_RTY_PENALTY = 1,
	RADBG_N_HIGH = 2,
	RADBG_N_LOW = 3,
	RADBG_TRATE_UP_TABLE = 4,
	RADBG_TRATE_DOWN_TABLE = 5,
	RADBG_TRYING_NECESSARY = 6,
	RADBG_TDROPING_NECESSARY = 7,
	RADBG_RATE_UP_RTY_RATIO = 8,
	RADBG_RATE_DOWN_RTY_RATIO = 9, /* u8 */

	RADBG_DEBUG_MONITOR1 = 0xc,
	RADBG_DEBUG_MONITOR2 = 0xd,
	RADBG_DEBUG_MONITOR3 = 0xe,
	RADBG_DEBUG_MONITOR4 = 0xf,
	RADBG_DEBUG_MONITOR5 = 0x10,
	NUM_RA_PARA
};

enum phydm_wireless_mode {
	PHYDM_WIRELESS_MODE_UNKNOWN = 0x00,
	PHYDM_WIRELESS_MODE_A = 0x01,
	PHYDM_WIRELESS_MODE_B = 0x02,
	PHYDM_WIRELESS_MODE_G = 0x04,
	PHYDM_WIRELESS_MODE_AUTO = 0x08,
	PHYDM_WIRELESS_MODE_N_24G = 0x10,
	PHYDM_WIRELESS_MODE_N_5G = 0x20,
	PHYDM_WIRELESS_MODE_AC_5G = 0x40,
	PHYDM_WIRELESS_MODE_AC_24G = 0x80,
	PHYDM_WIRELESS_MODE_AC_ONLY = 0x100,
	PHYDM_WIRELESS_MODE_MAX = 0x800,
	PHYDM_WIRELESS_MODE_ALL = 0xFFFF
};

enum phydm_rateid_idx {
	PHYDM_BGN_40M_2SS = 0,
	PHYDM_BGN_40M_1SS = 1,
	PHYDM_BGN_20M_2SS = 2,
	PHYDM_BGN_20M_1SS = 3,
	PHYDM_GN_N2SS = 4,
	PHYDM_GN_N1SS = 5,
	PHYDM_BG = 6,
	PHYDM_G = 7,
	PHYDM_B_20M = 8,
	PHYDM_ARFR0_AC_2SS = 9,
	PHYDM_ARFR1_AC_1SS = 10,
	PHYDM_ARFR2_AC_2G_1SS = 11,
	PHYDM_ARFR3_AC_2G_2SS = 12,
	PHYDM_ARFR4_AC_3SS = 13,
	PHYDM_ARFR5_N_3SS = 14
};

enum phydm_rf_type_def {
	PHYDM_RF_1T1R = 0,
	PHYDM_RF_1T2R,
	PHYDM_RF_2T2R,
	PHYDM_RF_2T2R_GREEN,
	PHYDM_RF_2T3R,
	PHYDM_RF_2T4R,
	PHYDM_RF_3T3R,
	PHYDM_RF_3T4R,
	PHYDM_RF_4T4R,
	PHYDM_RF_MAX_TYPE
};

enum phydm_bw {
	PHYDM_BW_20 = 0,
	PHYDM_BW_40,
	PHYDM_BW_80,
	PHYDM_BW_80_80,
	PHYDM_BW_160,
	PHYDM_BW_10,
	PHYDM_BW_5
};

struct ra_table {
	u8 firstconnect;

	u8 link_tx_rate[ODM_ASSOCIATE_ENTRY_NUM];
	u8 highest_client_tx_order;
	u16 highest_client_tx_rate_order;
	u8 power_tracking_flag;
	u8 RA_threshold_offset;
	u8 RA_offset_direction;
	u8 force_update_ra_mask_count;
};

struct odm_rate_adaptive {
	/* dm_type_by_fw/dm_type_by_driver */
	u8 type;
	/* if RSSI > high_rssi_thresh	=> ratr_state is DM_RATR_STA_HIGH */
	u8 high_rssi_thresh;
	/* if RSSI <= low_rssi_thresh	=> ratr_state is DM_RATR_STA_LOW */
	u8 low_rssi_thresh;
	/* Cur RSSI level, DM_RATR_STA_HIGH/DM_RATR_STA_MIDDLE/DM_RATR_STA_LOW*/
	u8 ratr_state;

	/* if RSSI > ldpc_thres => switch from LPDC to BCC */
	u8 ldpc_thres;
	bool is_lower_rts_rate;

	bool is_use_ldpc;
};

void phydm_h2C_debug(void *dm_void, u32 *const dm_value, u32 *_used,
		     char *output, u32 *_out_len);

void phydm_RA_debug_PCR(void *dm_void, u32 *const dm_value, u32 *_used,
			char *output, u32 *_out_len);

void odm_c2h_ra_para_report_handler(void *dm_void, u8 *cmd_buf, u8 cmd_len);

void odm_ra_para_adjust(void *dm_void);

void phydm_ra_dynamic_retry_count(void *dm_void);

void phydm_ra_dynamic_retry_limit(void *dm_void);

void phydm_ra_dynamic_rate_id_on_assoc(void *dm_void, u8 wireless_mode,
				       u8 init_rate_id);

void phydm_print_rate(void *dm_void, u8 rate, u32 dbg_component);

void phydm_c2h_ra_report_handler(void *dm_void, u8 *cmd_buf, u8 cmd_len);

u8 phydm_rate_order_compute(void *dm_void, u8 rate_idx);

void phydm_ra_info_watchdog(void *dm_void);

void phydm_ra_info_init(void *dm_void);

void odm_rssi_monitor_init(void *dm_void);

void phydm_modify_RA_PCR_threshold(void *dm_void, u8 RA_offset_direction,
				   u8 RA_threshold_offset);

void odm_rssi_monitor_check(void *dm_void);

void phydm_init_ra_info(void *dm_void);

u8 phydm_vht_en_mapping(void *dm_void, u32 wireless_mode);

u8 phydm_rate_id_mapping(void *dm_void, u32 wireless_mode, u8 rf_type, u8 bw);

void phydm_update_hal_ra_mask(void *dm_void, u32 wireless_mode, u8 rf_type,
			      u8 BW, u8 mimo_ps_enable, u8 disable_cck_rate,
			      u32 *ratr_bitmap_msb_in, u32 *ratr_bitmap_in,
			      u8 tx_rate_level);

void odm_rate_adaptive_mask_init(void *dm_void);

void odm_refresh_rate_adaptive_mask(void *dm_void);

void odm_refresh_rate_adaptive_mask_mp(void *dm_void);

void odm_refresh_rate_adaptive_mask_ce(void *dm_void);

void odm_refresh_rate_adaptive_mask_apadsl(void *dm_void);

u8 phydm_RA_level_decision(void *dm_void, u32 rssi, u8 ratr_state);

bool odm_ra_state_check(void *dm_void, s32 RSSI, bool is_force_update,
			u8 *ra_tr_state);

void odm_refresh_basic_rate_mask(void *dm_void);
void odm_ra_post_action_on_assoc(void *dm);

u8 odm_find_rts_rate(void *dm_void, u8 tx_rate, bool is_erp_protect);

void odm_update_noisy_state(void *dm_void, bool is_noisy_state_from_c2h);

void phydm_update_pwr_track(void *dm_void, u8 rate);

#endif /*#ifndef	__ODMRAINFO_H__*/
