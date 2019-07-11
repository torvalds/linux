/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2012  Realtek Corporation.*/

/**********************************************************************
 * The following is for 8723B 1ANT BT Co-exist definition
 **********************************************************************/
#define	BT_INFO_8723B_1ANT_B_FTP			BIT7
#define	BT_INFO_8723B_1ANT_B_A2DP			BIT6
#define	BT_INFO_8723B_1ANT_B_HID			BIT5
#define	BT_INFO_8723B_1ANT_B_SCO_BUSY			BIT4
#define	BT_INFO_8723B_1ANT_B_ACL_BUSY			BIT3
#define	BT_INFO_8723B_1ANT_B_INQ_PAGE			BIT2
#define	BT_INFO_8723B_1ANT_B_SCO_ESCO			BIT1
#define	BT_INFO_8723B_1ANT_B_CONNECTION			BIT0

#define	BT_INFO_8723B_1ANT_A2DP_BASIC_RATE(_BT_INFO_EXT_)	\
		(((_BT_INFO_EXT_&BIT0)) ? true : false)

#define	BTC_RSSI_COEX_THRESH_TOL_8723B_1ANT		2

#define BT_8723B_1ANT_WIFI_NOISY_THRESH			50

enum _BT_INFO_SRC_8723B_1ANT {
	BT_INFO_SRC_8723B_1ANT_WIFI_FW			= 0x0,
	BT_INFO_SRC_8723B_1ANT_BT_RSP			= 0x1,
	BT_INFO_SRC_8723B_1ANT_BT_ACTIVE_SEND		= 0x2,
	BT_INFO_SRC_8723B_1ANT_MAX
};

enum _BT_8723B_1ANT_BT_STATUS {
	BT_8723B_1ANT_BT_STATUS_NON_CONNECTED_IDLE	= 0x0,
	BT_8723B_1ANT_BT_STATUS_CONNECTED_IDLE		= 0x1,
	BT_8723B_1ANT_BT_STATUS_INQ_PAGE		= 0x2,
	BT_8723B_1ANT_BT_STATUS_ACL_BUSY		= 0x3,
	BT_8723B_1ANT_BT_STATUS_SCO_BUSY		= 0x4,
	BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY		= 0x5,
	BT_8723B_1ANT_BT_STATUS_MAX
};

enum _BT_8723B_1ANT_WIFI_STATUS {
	BT_8723B_1ANT_WIFI_STATUS_NON_CONNECTED_IDLE			= 0x0,
	BT_8723B_1ANT_WIFI_STATUS_NON_CONNECTED_ASSO_AUTH_SCAN		= 0x1,
	BT_8723B_1ANT_WIFI_STATUS_CONNECTED_SCAN			= 0x2,
	BT_8723B_1ANT_WIFI_STATUS_CONNECTED_SPECIAL_PKT			= 0x3,
	BT_8723B_1ANT_WIFI_STATUS_CONNECTED_IDLE			= 0x4,
	BT_8723B_1ANT_WIFI_STATUS_CONNECTED_BUSY			= 0x5,
	BT_8723B_1ANT_WIFI_STATUS_MAX
};

enum _BT_8723B_1ANT_COEX_ALGO {
	BT_8723B_1ANT_COEX_ALGO_UNDEFINED		= 0x0,
	BT_8723B_1ANT_COEX_ALGO_SCO			= 0x1,
	BT_8723B_1ANT_COEX_ALGO_HID			= 0x2,
	BT_8723B_1ANT_COEX_ALGO_A2DP			= 0x3,
	BT_8723B_1ANT_COEX_ALGO_A2DP_PANHS		= 0x4,
	BT_8723B_1ANT_COEX_ALGO_PANEDR			= 0x5,
	BT_8723B_1ANT_COEX_ALGO_PANHS			= 0x6,
	BT_8723B_1ANT_COEX_ALGO_PANEDR_A2DP		= 0x7,
	BT_8723B_1ANT_COEX_ALGO_PANEDR_HID		= 0x8,
	BT_8723B_1ANT_COEX_ALGO_HID_A2DP_PANEDR		= 0x9,
	BT_8723B_1ANT_COEX_ALGO_HID_A2DP		= 0xa,
	BT_8723B_1ANT_COEX_ALGO_MAX			= 0xb,
};

struct coex_dm_8723b_1ant {
	/* hw setting */
	u8 pre_ant_pos_type;
	u8 cur_ant_pos_type;
	/* fw mechanism */
	bool cur_ignore_wlan_act;
	bool pre_ignore_wlan_act;
	u8 pre_ps_tdma;
	u8 cur_ps_tdma;
	u8 ps_tdma_para[5];
	u8 ps_tdma_du_adj_type;
	bool auto_tdma_adjust;
	bool pre_ps_tdma_on;
	bool cur_ps_tdma_on;
	bool pre_bt_auto_report;
	bool cur_bt_auto_report;
	u8 pre_lps;
	u8 cur_lps;
	u8 pre_rpwm;
	u8 cur_rpwm;

	/* sw mechanism */
	bool pre_low_penalty_ra;
	bool cur_low_penalty_ra;
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
	u8 backup_ampdu_max_time;

	/* algorithm related */
	u8 pre_algorithm;
	u8 cur_algorithm;
	u8 bt_status;
	u8 wifi_chnl_info[3];

	u32 prera_mask;
	u32 curra_mask;
	u8 pre_arfr_type;
	u8 cur_arfr_type;
	u8 pre_retry_limit_type;
	u8 cur_retry_limit_type;
	u8 pre_ampdu_time_type;
	u8 cur_ampdu_time_type;
	u32	arp_cnt;

	u8 error_condition;
};

struct coex_sta_8723b_1ant {
	bool bt_disabled;
	bool bt_link_exist;
	bool sco_exist;
	bool a2dp_exist;
	bool hid_exist;
	bool pan_exist;
	bool bt_hi_pri_link_exist;
	u8 num_of_profile;
	bool bt_abnormal_scan;

	bool under_lps;
	bool under_ips;
	u32 special_pkt_period_cnt;
	u32 high_priority_tx;
	u32 high_priority_rx;
	u32 low_priority_tx;
	u32 low_priority_rx;
	u8 bt_rssi;
	u8 pre_bt_rssi_state;
	u8 pre_wifi_rssi_state[4];
	bool bt_tx_rx_mask;
	bool c2h_bt_info_req_sent;
	u8 bt_info_c2h[BT_INFO_SRC_8723B_1ANT_MAX][10];
	u32 bt_info_c2h_cnt[BT_INFO_SRC_8723B_1ANT_MAX];
	bool bt_whck_test;
	bool c2h_bt_inquiry_page;
	bool c2h_bt_remote_name_req;
	bool wifi_is_high_pri_task;
	u8 bt_retry_cnt;
	u8 bt_info_ext;
	u8 scan_ap_num;
	bool cck_ever_lock;
	u8 coex_table_type;
	bool force_lps_on;
	u32 pop_event_cnt;

	u32 crc_ok_cck;
	u32 crc_ok_11g;
	u32 crc_ok_11n;
	u32 crc_ok_11n_agg;

	u32 crc_err_cck;
	u32 crc_err_11g;
	u32 crc_err_11n;
	u32 crc_err_11n_agg;

	bool cck_lock;
	bool pre_ccklock;

	u32 wrong_profile_notification;

	u8 a2dp_bit_pool;
	u8 cut_version;
};

/*************************************************************************
 * The following is interface which will notify coex module.
 *************************************************************************/
void ex_btc8723b1ant_power_on_setting(struct btc_coexist *btcoexist);
void ex_btc8723b1ant_init_hwconfig(struct btc_coexist *btcoexist,
				   bool wifi_only);
void ex_btc8723b1ant_init_coex_dm(struct btc_coexist *btcoexist);
void ex_btc8723b1ant_ips_notify(struct btc_coexist *btcoexist, u8 type);
void ex_btc8723b1ant_lps_notify(struct btc_coexist *btcoexist, u8 type);
void ex_btc8723b1ant_scan_notify(struct btc_coexist *btcoexist, u8 type);
void ex_btc8723b1ant_connect_notify(struct btc_coexist *btcoexist, u8 type);
void ex_btc8723b1ant_media_status_notify(struct btc_coexist *btcoexist,
					 u8 type);
void ex_btc8723b1ant_special_packet_notify(struct btc_coexist *btcoexist,
					   u8 type);
void ex_btc8723b1ant_bt_info_notify(struct btc_coexist *btcoexist,
				    u8 *tmpbuf, u8 length);
void ex_btc8723b1ant_rf_status_notify(struct btc_coexist *btcoexist,
				      u8 type);
void ex_btc8723b1ant_halt_notify(struct btc_coexist *btcoexist);
void ex_btc8723b1ant_pnp_notify(struct btc_coexist *btcoexist, u8 pnpstate);
void ex_btc8723b1ant_coex_dm_reset(struct btc_coexist *btcoexist);
void ex_btc8723b1ant_periodical(struct btc_coexist *btcoexist);
void ex_btc8723b1ant_display_coex_info(struct btc_coexist *btcoexist,
				       struct seq_file *m);
void ex_btc8723b1ant_pnp_notify(struct btc_coexist *btcoexist, u8 pnp_state);
