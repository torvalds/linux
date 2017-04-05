
#if (BT_SUPPORT == 1 && COEX_SUPPORT == 1)

#if (RTL8821A_SUPPORT == 1)

/* *******************************************
 * The following is for 8821A 1ANT BT Co-exist definition
 * ******************************************* */
#define	BT_AUTO_REPORT_ONLY_8821A_1ANT				1

#define	BT_INFO_8821A_1ANT_B_FTP						BIT(7)
#define	BT_INFO_8821A_1ANT_B_A2DP					BIT(6)
#define	BT_INFO_8821A_1ANT_B_HID						BIT(5)
#define	BT_INFO_8821A_1ANT_B_SCO_BUSY				BIT(4)
#define	BT_INFO_8821A_1ANT_B_ACL_BUSY				BIT(3)
#define	BT_INFO_8821A_1ANT_B_INQ_PAGE				BIT(2)
#define	BT_INFO_8821A_1ANT_B_SCO_ESCO				BIT(1)
#define	BT_INFO_8821A_1ANT_B_CONNECTION				BIT(0)

#define	BT_INFO_8821A_1ANT_A2DP_BASIC_RATE(_BT_INFO_EXT_)	\
		(((_BT_INFO_EXT_&BIT(0))) ? true : false)

#define	BTC_RSSI_COEX_THRESH_TOL_8821A_1ANT		2

enum bt_info_src_8821a_1ant {
	BT_INFO_SRC_8821A_1ANT_WIFI_FW			= 0x0,
	BT_INFO_SRC_8821A_1ANT_BT_RSP				= 0x1,
	BT_INFO_SRC_8821A_1ANT_BT_ACTIVE_SEND		= 0x2,
	BT_INFO_SRC_8821A_1ANT_MAX
};

enum bt_8821a_1ant_bt_status {
	BT_8821A_1ANT_BT_STATUS_NON_CONNECTED_IDLE	= 0x0,
	BT_8821A_1ANT_BT_STATUS_CONNECTED_IDLE		= 0x1,
	BT_8821A_1ANT_BT_STATUS_INQ_PAGE				= 0x2,
	BT_8821A_1ANT_BT_STATUS_ACL_BUSY				= 0x3,
	BT_8821A_1ANT_BT_STATUS_SCO_BUSY				= 0x4,
	BT_8821A_1ANT_BT_STATUS_ACL_SCO_BUSY			= 0x5,
	BT_8821A_1ANT_BT_STATUS_MAX
};

enum bt_8821a_1ant_wifi_status {
	BT_8821A_1ANT_WIFI_STATUS_NON_CONNECTED_IDLE				= 0x0,
	BT_8821A_1ANT_WIFI_STATUS_NON_CONNECTED_ASSO_AUTH_SCAN		= 0x1,
	BT_8821A_1ANT_WIFI_STATUS_CONNECTED_SCAN					= 0x2,
	BT_8821A_1ANT_WIFI_STATUS_CONNECTED_SPECIFIC_PKT				= 0x3,
	BT_8821A_1ANT_WIFI_STATUS_CONNECTED_IDLE					= 0x4,
	BT_8821A_1ANT_WIFI_STATUS_CONNECTED_BUSY					= 0x5,
	BT_8821A_1ANT_WIFI_STATUS_MAX
};

enum bt_8821a_1ant_coex_algo {
	BT_8821A_1ANT_COEX_ALGO_UNDEFINED			= 0x0,
	BT_8821A_1ANT_COEX_ALGO_SCO				= 0x1,
	BT_8821A_1ANT_COEX_ALGO_HID				= 0x2,
	BT_8821A_1ANT_COEX_ALGO_A2DP				= 0x3,
	BT_8821A_1ANT_COEX_ALGO_A2DP_PANHS		= 0x4,
	BT_8821A_1ANT_COEX_ALGO_PANEDR			= 0x5,
	BT_8821A_1ANT_COEX_ALGO_PANHS			= 0x6,
	BT_8821A_1ANT_COEX_ALGO_PANEDR_A2DP		= 0x7,
	BT_8821A_1ANT_COEX_ALGO_PANEDR_HID		= 0x8,
	BT_8821A_1ANT_COEX_ALGO_HID_A2DP_PANEDR	= 0x9,
	BT_8821A_1ANT_COEX_ALGO_HID_A2DP			= 0xa,
	BT_8821A_1ANT_COEX_ALGO_MAX				= 0xb,
};

struct coex_dm_8821a_1ant {
	/* fw mechanism */
	boolean		cur_ignore_wlan_act;
	boolean		pre_ignore_wlan_act;
	u8		pre_ps_tdma;
	u8		cur_ps_tdma;
	u8		ps_tdma_para[5];
	u8		ps_tdma_du_adj_type;
	boolean		auto_tdma_adjust;
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

struct coex_sta_8821a_1ant {
	boolean					bt_disabled;
	boolean					bt_link_exist;
	boolean					sco_exist;
	boolean					a2dp_exist;
	boolean					hid_exist;
	boolean					pan_exist;

	boolean					under_lps;
	boolean					under_ips;
	u32					specific_pkt_period_cnt;
	u32					high_priority_tx;
	u32					high_priority_rx;
	u32					low_priority_tx;
	u32					low_priority_rx;

	u32					crc_ok_cck;
	u32					crc_ok_11g;
	u32					crc_ok_11n;
	u32					crc_ok_11n_vht;

	u32					crc_err_cck;
	u32					crc_err_11g;
	u32					crc_err_11n;
	u32					crc_err_11n_vht;

	u32					bt_coex_supported_version;
	u8					cut_version;
	u8					bt_rssi;
	u8					scan_ap_num;
	boolean					bt_tx_rx_mask;
	u8					pre_bt_rssi_state;
	u8					pre_wifi_rssi_state[4];
	boolean					c2h_bt_info_req_sent;
	u8					bt_info_c2h[BT_INFO_SRC_8821A_1ANT_MAX][10];
	u32					bt_info_c2h_cnt[BT_INFO_SRC_8821A_1ANT_MAX];
	boolean					c2h_bt_inquiry_page;
	boolean					c2h_bt_page;				/* Add for win8.1 page out issue */
	boolean					wifi_is_high_pri_task;		/* Add for win8.1 page out issue */
	u8					bt_retry_cnt;
	u8					bt_info_ext;
	boolean				bt_whck_test;	/* Add for ASUS WHQL TEST that enable wifi test bt */
};

/* *******************************************
 * The following is interface which will notify coex module.
 * ******************************************* */
void ex_halbtc8821a1ant_power_on_setting(IN struct btc_coexist *btcoexist);
void ex_halbtc8821a1ant_init_hw_config(IN struct btc_coexist *btcoexist,
				       IN boolean wifi_only);
void ex_halbtc8821a1ant_init_coex_dm(IN struct btc_coexist *btcoexist);
void ex_halbtc8821a1ant_ips_notify(IN struct btc_coexist *btcoexist,
				   IN u8 type);
void ex_halbtc8821a1ant_lps_notify(IN struct btc_coexist *btcoexist,
				   IN u8 type);
void ex_halbtc8821a1ant_scan_notify(IN struct btc_coexist *btcoexist,
				    IN u8 type);
void ex_halbtc8821a1ant_switchband_notify(IN struct btc_coexist *btcoexist,
		IN u8 type);
void ex_halbtc8821a1ant_connect_notify(IN struct btc_coexist *btcoexist,
				       IN u8 type);
void ex_halbtc8821a1ant_media_status_notify(IN struct btc_coexist *btcoexist,
		IN u8 type);
void ex_halbtc8821a1ant_specific_packet_notify(IN struct btc_coexist *btcoexist,
		IN u8 type);
void ex_halbtc8821a1ant_bt_info_notify(IN struct btc_coexist *btcoexist,
				       IN u8 *tmp_buf, IN u8 length);
void ex_halbtc8821a1ant_halt_notify(IN struct btc_coexist *btcoexist);
void ex_halbtc8821a1ant_pnp_notify(IN struct btc_coexist *btcoexist,
				   IN u8 pnp_state);
void ex_halbtc8821a1ant_periodical(IN struct btc_coexist *btcoexist);
void ex_halbtc8821a1ant_display_coex_info(IN struct btc_coexist *btcoexist);

#else
#define	ex_halbtc8821a1ant_power_on_setting(btcoexist)
#define	ex_halbtc8821a1ant_init_hw_config(btcoexist, wifi_only)
#define	ex_halbtc8821a1ant_init_coex_dm(btcoexist)
#define	ex_halbtc8821a1ant_ips_notify(btcoexist, type)
#define	ex_halbtc8821a1ant_lps_notify(btcoexist, type)
#define	ex_halbtc8821a1ant_scan_notify(btcoexist, type)
#define	ex_halbtc8821a1ant_switchband_notify(btcoexist, type)
#define	ex_halbtc8821a1ant_connect_notify(btcoexist, type)
#define	ex_halbtc8821a1ant_media_status_notify(btcoexist, type)
#define	ex_halbtc8821a1ant_specific_packet_notify(btcoexist, type)
#define	ex_halbtc8821a1ant_bt_info_notify(btcoexist, tmp_buf, length)
#define	ex_halbtc8821a1ant_halt_notify(btcoexist)
#define	ex_halbtc8821a1ant_pnp_notify(btcoexist, pnp_state)
#define	ex_halbtc8821a1ant_periodical(btcoexist)
#define	ex_halbtc8821a1ant_display_coex_info(btcoexist)

#endif

#endif

