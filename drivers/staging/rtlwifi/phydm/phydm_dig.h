/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
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

#define DIG_VERSION "1.32" /* 2016.09.02  YuChen. add CCK PD for 8197F*/

/* Pause DIG & CCKPD */
#define DM_DIG_MAX_PAUSE_TYPE 0x7

enum dig_goupcheck_level {
	DIG_GOUPCHECK_LEVEL_0,
	DIG_GOUPCHECK_LEVEL_1,
	DIG_GOUPCHECK_LEVEL_2

};

struct dig_thres {
	bool is_stop_dig; /* for debug */
	bool is_ignore_dig;
	bool is_psd_in_progress;

	u8 dig_enable_flag;
	u8 dig_ext_port_stage;

	int rssi_low_thresh;
	int rssi_high_thresh;

	u32 fa_low_thresh;
	u32 fa_high_thresh;

	u8 cur_sta_connect_state;
	u8 pre_sta_connect_state;
	u8 cur_multi_sta_connect_state;

	u8 pre_ig_value;
	u8 cur_ig_value;
	u8 backup_ig_value; /* MP DIG */
	u8 bt30_cur_igi;
	u8 igi_backup;

	s8 backoff_val;
	s8 backoff_val_range_max;
	s8 backoff_val_range_min;
	u8 rx_gain_range_max;
	u8 rx_gain_range_min;
	u8 rssi_val_min;

	u8 pre_cck_cca_thres;
	u8 cur_cck_cca_thres;
	u8 pre_cck_pd_state;
	u8 cur_cck_pd_state;
	u8 cck_pd_backup;
	u8 pause_cckpd_level;
	u8 pause_cckpd_value[DM_DIG_MAX_PAUSE_TYPE + 1];

	u8 large_fa_hit;
	u8 large_fa_timeout; /*if (large_fa_hit), monitor "large_fa_timeout"
			      *sec, if timeout, large_fa_hit=0
			      */
	u8 forbidden_igi;
	u32 recover_cnt;

	u8 dig_dynamic_min_0;
	u8 dig_dynamic_min_1;
	bool is_media_connect_0;
	bool is_media_connect_1;

	u32 ant_div_rssi_max;
	u32 rssi_max;

	u8 *is_p2p_in_process;

	u8 pause_dig_level;
	u8 pause_dig_value[DM_DIG_MAX_PAUSE_TYPE + 1];

	u32 cck_fa_ma;
	enum dig_goupcheck_level dig_go_up_check_level;
	u8 aaa_default;

	u8 rf_gain_idx;
	u8 agc_table_idx;
	u8 big_jump_lmt[16];
	u8 enable_adjust_big_jump : 1;
	u8 big_jump_step1 : 3;
	u8 big_jump_step2 : 2;
	u8 big_jump_step3 : 2;
};

struct false_alarm_stat {
	u32 cnt_parity_fail;
	u32 cnt_rate_illegal;
	u32 cnt_crc8_fail;
	u32 cnt_mcs_fail;
	u32 cnt_ofdm_fail;
	u32 cnt_ofdm_fail_pre; /* For RTL8881A */
	u32 cnt_cck_fail;
	u32 cnt_all;
	u32 cnt_all_pre;
	u32 cnt_fast_fsync;
	u32 cnt_sb_search_fail;
	u32 cnt_ofdm_cca;
	u32 cnt_cck_cca;
	u32 cnt_cca_all;
	u32 cnt_bw_usc; /* Gary */
	u32 cnt_bw_lsc; /* Gary */
	u32 cnt_cck_crc32_error;
	u32 cnt_cck_crc32_ok;
	u32 cnt_ofdm_crc32_error;
	u32 cnt_ofdm_crc32_ok;
	u32 cnt_ht_crc32_error;
	u32 cnt_ht_crc32_ok;
	u32 cnt_vht_crc32_error;
	u32 cnt_vht_crc32_ok;
	u32 cnt_crc32_error_all;
	u32 cnt_crc32_ok_all;
	bool cck_block_enable;
	bool ofdm_block_enable;
	u32 dbg_port0;
	bool edcca_flag;
};

enum dm_dig_op {
	DIG_TYPE_THRESH_HIGH = 0,
	DIG_TYPE_THRESH_LOW = 1,
	DIG_TYPE_BACKOFF = 2,
	DIG_TYPE_RX_GAIN_MIN = 3,
	DIG_TYPE_RX_GAIN_MAX = 4,
	DIG_TYPE_ENABLE = 5,
	DIG_TYPE_DISABLE = 6,
	DIG_OP_TYPE_MAX
};

enum phydm_pause_type { PHYDM_PAUSE = BIT(0), PHYDM_RESUME = BIT(1) };

enum phydm_pause_level {
	/* number of pause level can't exceed DM_DIG_MAX_PAUSE_TYPE */
	PHYDM_PAUSE_LEVEL_0 = 0,
	PHYDM_PAUSE_LEVEL_1 = 1,
	PHYDM_PAUSE_LEVEL_2 = 2,
	PHYDM_PAUSE_LEVEL_3 = 3,
	PHYDM_PAUSE_LEVEL_4 = 4,
	PHYDM_PAUSE_LEVEL_5 = 5,
	PHYDM_PAUSE_LEVEL_6 = 6,
	PHYDM_PAUSE_LEVEL_7 = DM_DIG_MAX_PAUSE_TYPE /* maximum level */
};

#define DM_DIG_THRESH_HIGH 40
#define DM_DIG_THRESH_LOW 35

#define DM_FALSEALARM_THRESH_LOW 400
#define DM_FALSEALARM_THRESH_HIGH 1000

#define DM_DIG_MAX_NIC 0x3e
#define DM_DIG_MIN_NIC 0x20
#define DM_DIG_MAX_OF_MIN_NIC 0x3e

#define DM_DIG_MAX_AP 0x3e
#define DM_DIG_MIN_AP 0x20
#define DM_DIG_MAX_OF_MIN 0x2A /* 0x32 */
#define DM_DIG_MIN_AP_DFS 0x20

#define DM_DIG_MAX_NIC_HP 0x46
#define DM_DIG_MIN_NIC_HP 0x2e

#define DM_DIG_MAX_AP_HP 0x42
#define DM_DIG_MIN_AP_HP 0x30

/* vivi 92c&92d has different definition, 20110504
 * this is for 92c
 */
#define DM_DIG_FA_TH0 0x200 /* 0x20 */

#define DM_DIG_FA_TH1 0x300
#define DM_DIG_FA_TH2 0x400
/* this is for 92d */
#define DM_DIG_FA_TH0_92D 0x100
#define DM_DIG_FA_TH1_92D 0x400
#define DM_DIG_FA_TH2_92D 0x600

#define DM_DIG_BACKOFF_MAX 12
#define DM_DIG_BACKOFF_MIN -4
#define DM_DIG_BACKOFF_DEFAULT 10

#define DM_DIG_FA_TH0_LPS 4 /* -> 4 in lps */
#define DM_DIG_FA_TH1_LPS 15 /* -> 15 lps */
#define DM_DIG_FA_TH2_LPS 30 /* -> 30 lps */
#define RSSI_OFFSET_DIG 0x05
#define LARGE_FA_TIMEOUT 60

void odm_change_dynamic_init_gain_thresh(void *dm_void, u32 dm_type,
					 u32 dm_value);

void odm_write_dig(void *dm_void, u8 current_igi);

void odm_pause_dig(void *dm_void, enum phydm_pause_type pause_type,
		   enum phydm_pause_level pause_level, u8 igi_value);

void odm_dig_init(void *dm_void);

void odm_DIG(void *dm_void);

void odm_dig_by_rssi_lps(void *dm_void);

void odm_false_alarm_counter_statistics(void *dm_void);

void odm_pause_cck_packet_detection(void *dm_void,
				    enum phydm_pause_type pause_type,
				    enum phydm_pause_level pause_level,
				    u8 cck_pd_threshold);

void odm_cck_packet_detection_thresh(void *dm_void);

void odm_write_cck_cca_thres(void *dm_void, u8 cur_cck_cca_thres);

bool phydm_dig_go_up_check(void *dm_void);

#endif
