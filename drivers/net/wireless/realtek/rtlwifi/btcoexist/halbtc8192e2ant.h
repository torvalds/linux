/******************************************************************************
 *
 * Copyright(c) 2012  Realtek Corporation.
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
/*****************************************************************
 *   The following is for 8192E 2Ant BT Co-exist definition
 *****************************************************************/
#define	BT_INFO_8192E_2ANT_B_FTP			BIT7
#define	BT_INFO_8192E_2ANT_B_A2DP			BIT6
#define	BT_INFO_8192E_2ANT_B_HID			BIT5
#define	BT_INFO_8192E_2ANT_B_SCO_BUSY			BIT4
#define	BT_INFO_8192E_2ANT_B_ACL_BUSY			BIT3
#define	BT_INFO_8192E_2ANT_B_INQ_PAGE			BIT2
#define	BT_INFO_8192E_2ANT_B_SCO_ESCO			BIT1
#define	BT_INFO_8192E_2ANT_B_CONNECTION			BIT0

#define BTC_RSSI_COEX_THRESH_TOL_8192E_2ANT		2

enum bt_info_src_8192e_2ant {
	BT_INFO_SRC_8192E_2ANT_WIFI_FW			= 0x0,
	BT_INFO_SRC_8192E_2ANT_BT_RSP			= 0x1,
	BT_INFO_SRC_8192E_2ANT_BT_ACTIVE_SEND		= 0x2,
	BT_INFO_SRC_8192E_2ANT_MAX
};

enum bt_8192e_2ant_bt_status {
	BT_8192E_2ANT_BT_STATUS_NON_CONNECTED_IDLE	= 0x0,
	BT_8192E_2ANT_BT_STATUS_CONNECTED_IDLE		= 0x1,
	BT_8192E_2ANT_BT_STATUS_INQ_PAGE		= 0x2,
	BT_8192E_2ANT_BT_STATUS_ACL_BUSY		= 0x3,
	BT_8192E_2ANT_BT_STATUS_SCO_BUSY		= 0x4,
	BT_8192E_2ANT_BT_STATUS_ACL_SCO_BUSY		= 0x5,
	BT_8192E_2ANT_BT_STATUS_MAX
};

enum bt_8192e_2ant_coex_algo {
	BT_8192E_2ANT_COEX_ALGO_UNDEFINED		= 0x0,
	BT_8192E_2ANT_COEX_ALGO_SCO			= 0x1,
	BT_8192E_2ANT_COEX_ALGO_SCO_PAN			= 0x2,
	BT_8192E_2ANT_COEX_ALGO_HID			= 0x3,
	BT_8192E_2ANT_COEX_ALGO_A2DP			= 0x4,
	BT_8192E_2ANT_COEX_ALGO_A2DP_PANHS		= 0x5,
	BT_8192E_2ANT_COEX_ALGO_PANEDR			= 0x6,
	BT_8192E_2ANT_COEX_ALGO_PANHS			= 0x7,
	BT_8192E_2ANT_COEX_ALGO_PANEDR_A2DP		= 0x8,
	BT_8192E_2ANT_COEX_ALGO_PANEDR_HID		= 0x9,
	BT_8192E_2ANT_COEX_ALGO_HID_A2DP_PANEDR		= 0xa,
	BT_8192E_2ANT_COEX_ALGO_HID_A2DP		= 0xb,
	BT_8192E_2ANT_COEX_ALGO_MAX			= 0xc
};

struct coex_dm_8192e_2ant {
	/* fw mechanism */
	u8 pre_dec_bt_pwr;
	u8 cur_dec_bt_pwr;
	u8 pre_fw_dac_swing_lvl;
	u8 cur_fw_dac_swing_lvl;
	bool cur_ignore_wlan_act;
	bool pre_ignore_wlan_act;
	u8 pre_ps_tdma;
	u8 cur_ps_tdma;
	u8 ps_tdma_para[5];
	u8 tdma_adj_type;
	bool reset_tdma_adjust;
	bool auto_tdma_adjust;
	bool pre_ps_tdma_on;
	bool cur_ps_tdma_on;
	bool pre_bt_auto_report;
	bool cur_bt_auto_report;

	/* sw mechanism */
	bool pre_rf_rx_lpf_shrink;
	bool cur_rf_rx_lpf_shrink;
	u32 bt_rf0x1e_backup;
	bool pre_low_penalty_ra;
	bool cur_low_penalty_ra;
	bool pre_dac_swing_on;
	u32 pre_dac_swing_lvl;
	bool cur_dac_swing_on;
	u32 cur_dac_swing_lvl;
	bool pre_adc_back_off;
	bool cur_adc_back_off;
	bool pre_agc_table_en;
	bool cur_agc_table_en;
	u32 pre_val0x6c0;
	u32 cur_val0x6c0;
	u32 pre_val0x6c4;
	u32 cur_val0x6c4;
	u32 pre_val0x6c8;
	u32 cur_val0x6c8;
	u8 pre_val0x6cc;
	u8 cur_val0x6cc;
	bool limited_dig;

	u32 backup_arfr_cnt1;	/* Auto Rate Fallback Retry cnt */
	u32 backup_arfr_cnt2;	/* Auto Rate Fallback Retry cnt */
	u16 backup_retry_limit;
	u8 backup_ampdu_maxtime;

	/* algorithm related */
	u8 pre_algorithm;
	u8 cur_algorithm;
	u8 bt_status;
	u8 wifi_chnl_info[3];

	u8 pre_ss_type;
	u8 cur_ss_type;

	u32 pre_ra_mask;
	u32 cur_ra_mask;
	u8 cur_ra_mask_type;
	u8 pre_arfr_type;
	u8 cur_arfr_type;
	u8 pre_retry_limit_type;
	u8 cur_retry_limit_type;
	u8 pre_ampdu_time_type;
	u8 cur_ampdu_time_type;
};

struct coex_sta_8192e_2ant {
	bool bt_link_exist;
	bool sco_exist;
	bool a2dp_exist;
	bool hid_exist;
	bool pan_exist;

	bool under_lps;
	bool under_ips;
	u32 high_priority_tx;
	u32 high_priority_rx;
	u32 low_priority_tx;
	u32 low_priority_rx;
	u8 bt_rssi;
	u8 pre_bt_rssi_state;
	u8 pre_wifi_rssi_state[4];
	bool c2h_bt_info_req_sent;
	u8 bt_info_c2h[BT_INFO_SRC_8192E_2ANT_MAX][10];
	u32 bt_info_c2h_cnt[BT_INFO_SRC_8192E_2ANT_MAX];
	bool c2h_bt_inquiry_page;
	u8 bt_retry_cnt;
	u8 bt_info_ext;
};

/****************************************************************
 *    The following is interface which will notify coex module.
 ****************************************************************/
void ex_btc8192e2ant_init_hwconfig(struct btc_coexist *btcoexist);
void ex_btc8192e2ant_init_coex_dm(struct btc_coexist *btcoexist);
void ex_btc8192e2ant_ips_notify(struct btc_coexist *btcoexist, u8 type);
void ex_btc8192e2ant_lps_notify(struct btc_coexist *btcoexist, u8 type);
void ex_btc8192e2ant_scan_notify(struct btc_coexist *btcoexist, u8 type);
void ex_btc8192e2ant_connect_notify(struct btc_coexist *btcoexist, u8 type);
void ex_btc8192e2ant_media_status_notify(struct btc_coexist *btcoexist,
					 u8 type);
void ex_btc8192e2ant_special_packet_notify(struct btc_coexist *btcoexist,
					   u8 type);
void ex_btc8192e2ant_bt_info_notify(struct btc_coexist *btcoexist,
				    u8 *tmpbuf, u8 length);
void ex_btc8192e2ant_stack_operation_notify(struct btc_coexist *btcoexist,
					    u8 type);
void ex_btc8192e2ant_halt_notify(struct btc_coexist *btcoexist);
void ex_btc8192e2ant_periodical(struct btc_coexist *btcoexist);
void ex_btc8192e2ant_display_coex_info(struct btc_coexist *btcoexist);
