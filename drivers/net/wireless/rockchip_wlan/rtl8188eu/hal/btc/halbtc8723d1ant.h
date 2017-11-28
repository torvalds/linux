
#if (BT_SUPPORT == 1 && COEX_SUPPORT == 1)

#if (RTL8723D_SUPPORT == 1)

/* *******************************************
 * The following is for 8723D 1ANT BT Co-exist definition
 * ******************************************* */
#define	BT_8723D_1ANT_COEX_DBG					0
#define	BT_AUTO_REPORT_ONLY_8723D_1ANT				1

#define	BT_INFO_8723D_1ANT_B_FTP						BIT(7)
#define	BT_INFO_8723D_1ANT_B_A2DP					BIT(6)
#define	BT_INFO_8723D_1ANT_B_HID						BIT(5)
#define	BT_INFO_8723D_1ANT_B_SCO_BUSY				BIT(4)
#define	BT_INFO_8723D_1ANT_B_ACL_BUSY				BIT(3)
#define	BT_INFO_8723D_1ANT_B_INQ_PAGE				BIT(2)
#define	BT_INFO_8723D_1ANT_B_SCO_ESCO				BIT(1)
#define	BT_INFO_8723D_1ANT_B_CONNECTION				BIT(0)

#define	BT_INFO_8723D_1ANT_A2DP_BASIC_RATE(_BT_INFO_EXT_)	\
	(((_BT_INFO_EXT_&BIT(0))) ? true : false)

#define	BTC_RSSI_COEX_THRESH_TOL_8723D_1ANT		2

#define  BT_8723D_1ANT_WIFI_NOISY_THRESH							30   /* max: 255 */
#define  BT_8723D_1ANT_DEFAULT_ISOLATION						15	 /*  unit: dB */


/* for Antenna detection */
#define	BT_8723D_1ANT_ANTDET_PSDTHRES_BACKGROUND					50
#define	BT_8723D_1ANT_ANTDET_PSDTHRES_2ANT_BADISOLATION				70
#define	BT_8723D_1ANT_ANTDET_PSDTHRES_2ANT_GOODISOLATION			55
#define	BT_8723D_1ANT_ANTDET_PSDTHRES_1ANT							35
#define	BT_8723D_1ANT_ANTDET_RETRY_INTERVAL							10	/* retry timer if ant det is fail, unit: second */
#define	BT_8723D_1ANT_ANTDET_SWEEPPOINT_DELAY							60000
#define	BT_8723D_1ANT_ANTDET_ENABLE									1
#define	BT_8723D_1ANT_ANTDET_BTTXTIME									100
#define	BT_8723D_1ANT_ANTDET_BTTXCHANNEL								39
#define	BT_8723D_1ANT_ANTDET_PSD_SWWEEPCOUNT						50

#define	BT_8723D_1ANT_LTECOEX_INDIRECTREG_ACCESS_TIMEOUT		30000

enum bt_8723d_1ant_signal_state {
	BT_8723D_1ANT_SIG_STA_SET_TO_LOW		= 0x0,
	BT_8723D_1ANT_SIG_STA_SET_BY_HW		= 0x0,
	BT_8723D_1ANT_SIG_STA_SET_TO_HIGH		= 0x1,
	BT_8723D_1ANT_SIG_STA_MAX
};

enum bt_8723d_1ant_path_ctrl_owner {
	BT_8723D_1ANT_PCO_BTSIDE		= 0x0,
	BT_8723D_1ANT_PCO_WLSIDE	= 0x1,
	BT_8723D_1ANT_PCO_MAX
};

enum bt_8723d_1ant_gnt_ctrl_type {
	BT_8723D_1ANT_GNT_TYPE_CTRL_BY_PTA		= 0x0,
	BT_8723D_1ANT_GNT_TYPE_CTRL_BY_SW		= 0x1,
	BT_8723D_1ANT_GNT_TYPE_MAX
};

enum bt_8723d_1ant_gnt_ctrl_block {
	BT_8723D_1ANT_GNT_BLOCK_RFC_BB		= 0x0,
	BT_8723D_1ANT_GNT_BLOCK_RFC			= 0x1,
	BT_8723D_1ANT_GNT_BLOCK_BB			= 0x2,
	BT_8723D_1ANT_GNT_BLOCK_MAX
};

enum bt_8723d_1ant_lte_coex_table_type {
	BT_8723D_1ANT_CTT_WL_VS_LTE			= 0x0,
	BT_8723D_1ANT_CTT_BT_VS_LTE			= 0x1,
	BT_8723D_1ANT_CTT_MAX
};

enum bt_8723d_1ant_lte_break_table_type {
	BT_8723D_1ANT_LBTT_WL_BREAK_LTE			= 0x0,
	BT_8723D_1ANT_LBTT_BT_BREAK_LTE				= 0x1,
	BT_8723D_1ANT_LBTT_LTE_BREAK_WL			= 0x2,
	BT_8723D_1ANT_LBTT_LTE_BREAK_BT				= 0x3,
	BT_8723D_1ANT_LBTT_MAX
};

enum bt_info_src_8723d_1ant {
	BT_INFO_SRC_8723D_1ANT_WIFI_FW			= 0x0,
	BT_INFO_SRC_8723D_1ANT_BT_RSP				= 0x1,
	BT_INFO_SRC_8723D_1ANT_BT_ACTIVE_SEND		= 0x2,
	BT_INFO_SRC_8723D_1ANT_MAX
};

enum bt_8723d_1ant_bt_status {
	BT_8723D_1ANT_BT_STATUS_NON_CONNECTED_IDLE	= 0x0,
	BT_8723D_1ANT_BT_STATUS_CONNECTED_IDLE		= 0x1,
	BT_8723D_1ANT_BT_STATUS_INQ_PAGE				= 0x2,
	BT_8723D_1ANT_BT_STATUS_ACL_BUSY				= 0x3,
	BT_8723D_1ANT_BT_STATUS_SCO_BUSY				= 0x4,
	BT_8723D_1ANT_BT_STATUS_ACL_SCO_BUSY			= 0x5,
	BT_8723D_1ANT_BT_STATUS_MAX
};

enum bt_8723d_1ant_wifi_status {
	BT_8723D_1ANT_WIFI_STATUS_NON_CONNECTED_IDLE				= 0x0,
	BT_8723D_1ANT_WIFI_STATUS_NON_CONNECTED_ASSO_AUTH_SCAN		= 0x1,
	BT_8723D_1ANT_WIFI_STATUS_CONNECTED_SCAN					= 0x2,
	BT_8723D_1ANT_WIFI_STATUS_CONNECTED_SPECIFIC_PKT				= 0x3,
	BT_8723D_1ANT_WIFI_STATUS_CONNECTED_IDLE					= 0x4,
	BT_8723D_1ANT_WIFI_STATUS_CONNECTED_BUSY					= 0x5,
	BT_8723D_1ANT_WIFI_STATUS_MAX
};

enum bt_8723d_1ant_coex_algo {
	BT_8723D_1ANT_COEX_ALGO_UNDEFINED			= 0x0,
	BT_8723D_1ANT_COEX_ALGO_SCO				= 0x1,
	BT_8723D_1ANT_COEX_ALGO_HID				= 0x2,
	BT_8723D_1ANT_COEX_ALGO_A2DP				= 0x3,
	BT_8723D_1ANT_COEX_ALGO_A2DP_PANHS		= 0x4,
	BT_8723D_1ANT_COEX_ALGO_PANEDR			= 0x5,
	BT_8723D_1ANT_COEX_ALGO_PANHS			= 0x6,
	BT_8723D_1ANT_COEX_ALGO_PANEDR_A2DP		= 0x7,
	BT_8723D_1ANT_COEX_ALGO_PANEDR_HID		= 0x8,
	BT_8723D_1ANT_COEX_ALGO_HID_A2DP_PANEDR	= 0x9,
	BT_8723D_1ANT_COEX_ALGO_HID_A2DP			= 0xa,
	BT_8723D_1ANT_COEX_ALGO_MAX				= 0xb,
};

enum bt_8723d_1ant_phase {
	BT_8723D_1ANT_PHASE_COEX_INIT								= 0x0,
	BT_8723D_1ANT_PHASE_WLANONLY_INIT							= 0x1,
	BT_8723D_1ANT_PHASE_WLAN_OFF								= 0x2,
	BT_8723D_1ANT_PHASE_2G_RUNTIME								= 0x3,
	BT_8723D_1ANT_PHASE_5G_RUNTIME								= 0x4,
	BT_8723D_1ANT_PHASE_BTMPMODE								= 0x5,
	BT_8723D_1ANT_PHASE_ANTENNA_DET								= 0x6,
	BT_8723D_1ANT_PHASE_COEX_POWERON							= 0x7,
	BT_8723D_1ANT_PHASE_MAX
};

enum bt_8723d_1ant_Scoreboard {
	BT_8723D_1ANT_SCOREBOARD_ACTIVE								= BIT(0),
	BT_8723D_1ANT_SCOREBOARD_ONOFF								= BIT(1),
	BT_8723D_1ANT_SCOREBOARD_SCAN								= BIT(2),
	BT_8723D_1ANT_SCOREBOARD_UNDERTEST							= BIT(3),
	BT_8723D_1ANT_SCOREBOARD_WLBUSY								= BIT(6)
};

struct coex_dm_8723d_1ant {
	/* hw setting */
	u8		pre_ant_pos_type;
	u8		cur_ant_pos_type;
	/* fw mechanism */
	boolean		cur_ignore_wlan_act;
	boolean		pre_ignore_wlan_act;
	u8		pre_ps_tdma;
	u8		cur_ps_tdma;
	u8		ps_tdma_para[5];
	u8		ps_tdma_du_adj_type;
	boolean		pre_ps_tdma_on;
	boolean		cur_ps_tdma_on;
	boolean		pre_bt_auto_report;
	boolean		cur_bt_auto_report;
	u8		pre_lps;
	u8		cur_lps;
	u8		pre_rpwm;
	u8		cur_rpwm;

	/* sw mechanism */
	boolean	pre_low_penalty_ra;
	boolean		cur_low_penalty_ra;
	u32		pre_val0x6c0;
	u32		cur_val0x6c0;
	u32		pre_val0x6c4;
	u32		cur_val0x6c4;
	u32		pre_val0x6c8;
	u32		cur_val0x6c8;
	u8		pre_val0x6cc;
	u8		cur_val0x6cc;
	boolean		limited_dig;

	u32		backup_arfr_cnt1;	/* Auto Rate Fallback Retry cnt */
	u32		backup_arfr_cnt2;	/* Auto Rate Fallback Retry cnt */
	u16		backup_retry_limit;
	u8		backup_ampdu_max_time;

	/* algorithm related */
	u8		pre_algorithm;
	u8		cur_algorithm;
	u8		bt_status;
	u8		wifi_chnl_info[3];

	u32		pre_ra_mask;
	u32		cur_ra_mask;
	u8		pre_arfr_type;
	u8		cur_arfr_type;
	u8		pre_retry_limit_type;
	u8		cur_retry_limit_type;
	u8		pre_ampdu_time_type;
	u8		cur_ampdu_time_type;
	u32		arp_cnt;

	u8		error_condition;
};

struct coex_sta_8723d_1ant {
	boolean				bt_disabled;
	boolean				bt_link_exist;
	boolean				sco_exist;
	boolean				a2dp_exist;
	boolean				hid_exist;
	boolean				pan_exist;
	boolean				bt_hi_pri_link_exist;
	u8					num_of_profile;

	boolean				under_lps;
	boolean				under_ips;
	u32					specific_pkt_period_cnt;
	u32					high_priority_tx;
	u32					high_priority_rx;
	u32					low_priority_tx;
	u32					low_priority_rx;
	boolean             is_hiPri_rx_overhead;
	s8					bt_rssi;
	boolean				bt_tx_rx_mask;
	u8					pre_bt_rssi_state;
	u8					pre_wifi_rssi_state[4];
	u8					bt_info_c2h[BT_INFO_SRC_8723D_1ANT_MAX][10];
	u32					bt_info_c2h_cnt[BT_INFO_SRC_8723D_1ANT_MAX];
	boolean				bt_whck_test;
	boolean				c2h_bt_inquiry_page;
	boolean				c2h_bt_remote_name_req;
	boolean				c2h_bt_page;				/* Add for win8.1 page out issue */
	boolean				wifi_is_high_pri_task;		/* Add for win8.1 page out issue */
	u8					bt_retry_cnt;
	u8					bt_info_ext;
	u8					bt_info_ext2;
	u32					pop_event_cnt;
	u8					scan_ap_num;

	u32					crc_ok_cck;
	u32					crc_ok_11g;
	u32					crc_ok_11n;
	u32					crc_ok_11n_vht;

	u32					crc_err_cck;
	u32					crc_err_11g;
	u32					crc_err_11n;
	u32					crc_err_11n_vht;

	boolean				cck_lock;
	boolean				pre_ccklock;
	boolean				cck_ever_lock;
	u8					coex_table_type;

	boolean				force_lps_on;

	boolean				concurrent_rx_mode_on;

	u16					score_board;
	u8					isolation_btween_wb;   /* 0~ 50 */

	u8					a2dp_bit_pool;
	u8					cut_version;
	boolean				acl_busy;
	boolean				bt_create_connection;

	u32					bt_coex_supported_feature;
	u32					bt_coex_supported_version;

	u8					bt_ble_scan_type;
	u32					bt_ble_scan_para[3];

	boolean				run_time_state;
	boolean				freeze_coexrun_by_btinfo;

	boolean				is_A2DP_3M;
	boolean				voice_over_HOGP;
	u8                  bt_info;
	boolean				is_autoslot;
	u8					forbidden_slot;
	u8					hid_busy_num;
	u8					hid_pair_cnt;

	u32					cnt_RemoteNameReq;
	u32					cnt_setupLink;
	u32					cnt_ReInit;
	u32					cnt_IgnWlanAct;
	u32					cnt_Page;
	u32					cnt_RoleSwitch;

	u16					bt_reg_vendor_ac;
	u16					bt_reg_vendor_ae;

	boolean				is_setupLink;
	u8					wl_noisy_level;
	u32                 gnt_error_cnt;

	u8					bt_afh_map[10];
	u8					bt_relink_downcount;
	boolean				is_tdma_btautoslot;
	boolean				is_tdma_btautoslot_hang;
};

#define  BT_8723D_1ANT_ANTDET_PSD_POINTS			256	/* MAX:1024 */
#define  BT_8723D_1ANT_ANTDET_PSD_AVGNUM			1	/* MAX:3 */
#define	BT_8723D_1ANT_ANTDET_BUF_LEN				16

struct psdscan_sta_8723d_1ant {

	u32			ant_det_bt_le_channel;  /* BT LE Channel ex:2412 */
	u32			ant_det_bt_tx_time;
	u32			ant_det_pre_psdscan_peak_val;
	boolean			ant_det_is_ant_det_available;
	u32			ant_det_psd_scan_peak_val;
	boolean			ant_det_is_btreply_available;
	u32			ant_det_psd_scan_peak_freq;

	u8			ant_det_result;
	u8			ant_det_peak_val[BT_8723D_1ANT_ANTDET_BUF_LEN];
	u8			ant_det_peak_freq[BT_8723D_1ANT_ANTDET_BUF_LEN];
	u32			ant_det_try_count;
	u32			ant_det_fail_count;
	u32			ant_det_inteval_count;
	u32			ant_det_thres_offset;

	u32			real_cent_freq;
	s32			real_offset;
	u32			real_span;

	u32			psd_band_width;  /* unit: Hz */
	u32			psd_point;		/* 128/256/512/1024 */
	u32			psd_report[1024];  /* unit:dB (20logx), 0~255 */
	u32			psd_report_max_hold[1024];  /* unit:dB (20logx), 0~255 */
	u32			psd_start_point;
	u32			psd_stop_point;
	u32			psd_max_value_point;
	u32			psd_max_value;
	u32			psd_max_value2;
	u32			psd_avg_value;   /* filter loop_max_value that below BT_8723D_1ANT_ANTDET_PSDTHRES_1ANT, and average the rest*/
	u32			psd_loop_max_value[BT_8723D_1ANT_ANTDET_PSD_SWWEEPCOUNT];  /*max value in each loop */
	u32			psd_start_base;
	u32			psd_avg_num;	/* 1/8/16/32 */
	u32			psd_gen_count;
	boolean			is_AntDet_running;
	boolean			is_psd_show_max_only;
};

/* *******************************************
 * The following is interface which will notify coex module.
 * ******************************************* */
void ex_halbtc8723d1ant_power_on_setting(IN struct btc_coexist *btcoexist);
void ex_halbtc8723d1ant_pre_load_firmware(IN struct btc_coexist *btcoexist);
void ex_halbtc8723d1ant_init_hw_config(IN struct btc_coexist *btcoexist,
				       IN boolean wifi_only);
void ex_halbtc8723d1ant_init_coex_dm(IN struct btc_coexist *btcoexist);
void ex_halbtc8723d1ant_ips_notify(IN struct btc_coexist *btcoexist,
				   IN u8 type);
void ex_halbtc8723d1ant_lps_notify(IN struct btc_coexist *btcoexist,
				   IN u8 type);
void ex_halbtc8723d1ant_scan_notify(IN struct btc_coexist *btcoexist,
				    IN u8 type);
void ex_halbtc8723d1ant_connect_notify(IN struct btc_coexist *btcoexist,
				       IN u8 type);
void ex_halbtc8723d1ant_media_status_notify(IN struct btc_coexist *btcoexist,
		IN u8 type);
void ex_halbtc8723d1ant_specific_packet_notify(IN struct btc_coexist *btcoexist,
		IN u8 type);
void ex_halbtc8723d1ant_bt_info_notify(IN struct btc_coexist *btcoexist,
				       IN u8 *tmp_buf, IN u8 length);
void ex_halbtc8723d1ant_rf_status_notify(IN struct btc_coexist *btcoexist,
		IN u8 type);
void ex_halbtc8723d1ant_halt_notify(IN struct btc_coexist *btcoexist);
void ex_halbtc8723d1ant_pnp_notify(IN struct btc_coexist *btcoexist,
				   IN u8 pnp_state);
void ex_halbtc8723d1ant_coex_dm_reset(IN struct btc_coexist *btcoexist);
void ex_halbtc8723d1ant_periodical(IN struct btc_coexist *btcoexist);
void ex_halbtc8723d1ant_set_antenna_notify(IN struct btc_coexist *btcoexist,
		IN u8 type);
void ex_halbtc8723d1ant_display_coex_info(IN struct btc_coexist *btcoexist);
void ex_halbtc8723d1ant_antenna_detection(IN struct btc_coexist *btcoexist,
		IN u32 cent_freq, IN u32 offset, IN u32 span, IN u32 seconds);
void ex_halbtc8723d1ant_antenna_isolation(IN struct btc_coexist *btcoexist,
		IN u32 cent_freq, IN u32 offset, IN u32 span, IN u32 seconds);

void ex_halbtc8723d1ant_psd_scan(IN struct btc_coexist *btcoexist,
		 IN u32 cent_freq, IN u32 offset, IN u32 span, IN u32 seconds);
void ex_halbtc8723d1ant_display_ant_detection(IN struct btc_coexist *btcoexist);

#else
#define	ex_halbtc8723d1ant_power_on_setting(btcoexist)
#define	ex_halbtc8723d1ant_pre_load_firmware(btcoexist)
#define	ex_halbtc8723d1ant_init_hw_config(btcoexist, wifi_only)
#define	ex_halbtc8723d1ant_init_coex_dm(btcoexist)
#define	ex_halbtc8723d1ant_ips_notify(btcoexist, type)
#define	ex_halbtc8723d1ant_lps_notify(btcoexist, type)
#define	ex_halbtc8723d1ant_scan_notify(btcoexist, type)
#define	ex_halbtc8723d1ant_connect_notify(btcoexist, type)
#define	ex_halbtc8723d1ant_media_status_notify(btcoexist, type)
#define	ex_halbtc8723d1ant_specific_packet_notify(btcoexist, type)
#define	ex_halbtc8723d1ant_bt_info_notify(btcoexist, tmp_buf, length)
#define	ex_halbtc8723d1ant_rf_status_notify(btcoexist, type)
#define	ex_halbtc8723d1ant_halt_notify(btcoexist)
#define	ex_halbtc8723d1ant_pnp_notify(btcoexist, pnp_state)
#define	ex_halbtc8723d1ant_coex_dm_reset(btcoexist)
#define	ex_halbtc8723d1ant_periodical(btcoexist)
#define	ex_halbtc8723d1ant_display_coex_info(btcoexist)
#define	ex_halbtc8723d1ant_set_antenna_notify(btcoexist, type)
#define	ex_halbtc8723d1ant_antenna_detection(btcoexist, cent_freq, offset, span, seconds)
#define	ex_halbtc8723d1ant_antenna_isolation(btcoexist, cent_freq, offset, span, seconds)
#define	ex_halbtc8723d1ant_psd_scan(btcoexist, cent_freq, offset, span, seconds)
#define	ex_halbtc8723d1ant_display_ant_detection(btcoexist)
#endif

#endif

