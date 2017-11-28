
#if (BT_SUPPORT == 1 && COEX_SUPPORT == 1)

#if (RTL8821A_SUPPORT == 1)

/* *******************************************
 * The following is for 8821A 2Ant BT Co-exist definition
 * ******************************************* */
#define	BT_AUTO_REPORT_ONLY_8821A_2ANT				1


#define	BT_INFO_8821A_2ANT_B_FTP						BIT(7)
#define	BT_INFO_8821A_2ANT_B_A2DP					BIT(6)
#define	BT_INFO_8821A_2ANT_B_HID						BIT(5)
#define	BT_INFO_8821A_2ANT_B_SCO_BUSY				BIT(4)
#define	BT_INFO_8821A_2ANT_B_ACL_BUSY				BIT(3)
#define	BT_INFO_8821A_2ANT_B_INQ_PAGE				BIT(2)
#define	BT_INFO_8821A_2ANT_B_SCO_ESCO				BIT(1)
#define	BT_INFO_8821A_2ANT_B_CONNECTION				BIT(0)

#define		BTC_RSSI_COEX_THRESH_TOL_8821A_2ANT		2


#define	BT_8821A_2ANT_WIFI_RSSI_COEXSWITCH_THRES				42  /* WiFi RSSI Threshold for 2-Ant TDMA/1-Ant PS-TDMA translation */
#define	BT_8821A_2ANT_BT_RSSI_COEXSWITCH_THRES				46 /* BT RSSI Threshold for 2-Ant TDMA/1-Ant PS-TDMA translation */

enum bt_info_src_8821a_2ant {
	BT_INFO_SRC_8821A_2ANT_WIFI_FW			= 0x0,
	BT_INFO_SRC_8821A_2ANT_BT_RSP				= 0x1,
	BT_INFO_SRC_8821A_2ANT_BT_ACTIVE_SEND		= 0x2,
	BT_INFO_SRC_8821A_2ANT_MAX
};

enum bt_8821a_2ant_bt_status {
	BT_8821A_2ANT_BT_STATUS_NON_CONNECTED_IDLE	= 0x0,
	BT_8821A_2ANT_BT_STATUS_CONNECTED_IDLE		= 0x1,
	BT_8821A_2ANT_BT_STATUS_INQ_PAGE				= 0x2,
	BT_8821A_2ANT_BT_STATUS_ACL_BUSY				= 0x3,
	BT_8821A_2ANT_BT_STATUS_SCO_BUSY				= 0x4,
	BT_8821A_2ANT_BT_STATUS_ACL_SCO_BUSY			= 0x5,
	BT_8821A_2ANT_BT_STATUS_MAX
};

enum bt_8821a_2ant_coex_algo {
	BT_8821A_2ANT_COEX_ALGO_UNDEFINED			= 0x0,
	BT_8821A_2ANT_COEX_ALGO_SCO				= 0x1,
	BT_8821A_2ANT_COEX_ALGO_HID				= 0x2,
	BT_8821A_2ANT_COEX_ALGO_A2DP				= 0x3,
	BT_8821A_2ANT_COEX_ALGO_A2DP_PANHS		= 0x4,
	BT_8821A_2ANT_COEX_ALGO_PANEDR			= 0x5,
	BT_8821A_2ANT_COEX_ALGO_PANHS			= 0x6,
	BT_8821A_2ANT_COEX_ALGO_PANEDR_A2DP		= 0x7,
	BT_8821A_2ANT_COEX_ALGO_PANEDR_HID		= 0x8,
	BT_8821A_2ANT_COEX_ALGO_HID_A2DP_PANEDR	= 0x9,
	BT_8821A_2ANT_COEX_ALGO_HID_A2DP			= 0xa,
	BT_8821A_2ANT_COEX_ALGO_MAX				= 0xb,
};

struct coex_dm_8821a_2ant {
	/* fw mechanism */
	u8		pre_bt_dec_pwr_lvl;
	u8		cur_bt_dec_pwr_lvl;
	u8		pre_fw_dac_swing_lvl;
	u8		cur_fw_dac_swing_lvl;
	boolean		cur_ignore_wlan_act;
	boolean		pre_ignore_wlan_act;
	u8		pre_ps_tdma;
	u8		cur_ps_tdma;
	u8		ps_tdma_para[5];
	u8		ps_tdma_du_adj_type;
	boolean		reset_tdma_adjust;
	boolean		auto_tdma_adjust;
	boolean		pre_ps_tdma_on;
	boolean		cur_ps_tdma_on;
	boolean		pre_bt_auto_report;
	boolean		cur_bt_auto_report;

	/* sw mechanism */
	boolean		pre_rf_rx_lpf_shrink;
	boolean		cur_rf_rx_lpf_shrink;
	u32		bt_rf_0x1e_backup;
	boolean	pre_low_penalty_ra;
	boolean		cur_low_penalty_ra;
	boolean		pre_dac_swing_on;
	u32		pre_dac_swing_lvl;
	boolean		cur_dac_swing_on;
	u32		cur_dac_swing_lvl;
	boolean		pre_adc_back_off;
	boolean		cur_adc_back_off;
	boolean	pre_agc_table_en;
	boolean		cur_agc_table_en;
	u32		pre_val0x6c0;
	u32		cur_val0x6c0;
	u32		pre_val0x6c4;
	u32		cur_val0x6c4;
	u32		pre_val0x6c8;
	u32		cur_val0x6c8;
	u8		pre_val0x6cc;
	u8		cur_val0x6cc;
	boolean		limited_dig;

	/* algorithm related */
	u8		pre_algorithm;
	u8		cur_algorithm;
	u8		bt_status;
	u8		wifi_chnl_info[3];

	boolean		need_recover0x948;
	u32		backup0x948;

	u8		pre_lps;
	u8		cur_lps;
	u8		pre_rpwm;
	u8		cur_rpwm;
};

struct coex_sta_8821a_2ant {
	boolean					bt_disabled;
	boolean					bt_link_exist;
	boolean					sco_exist;
	boolean					a2dp_exist;
	boolean					hid_exist;
	boolean					pan_exist;

	boolean					under_lps;
	boolean					under_ips;
	u32					high_priority_tx;
	u32					high_priority_rx;
	u32					low_priority_tx;
	u32					low_priority_rx;
	u8					bt_rssi;
	boolean					bt_tx_rx_mask;
	u8					pre_bt_rssi_state;
	u8					pre_wifi_rssi_state[4];
	boolean					c2h_bt_info_req_sent;
	u8					bt_info_c2h[BT_INFO_SRC_8821A_2ANT_MAX][10];
	u32					bt_info_c2h_cnt[BT_INFO_SRC_8821A_2ANT_MAX];
	boolean				bt_whck_test;
	boolean					c2h_bt_inquiry_page;
	u8					bt_retry_cnt;
	u8					bt_info_ext;
	u8					scan_ap_num;
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
	u8					coex_table_type;
	boolean					force_lps_on;

	u8					dis_ver_info_cnt;
};

/* *******************************************
 * The following is interface which will notify coex module.
 * ******************************************* */
void ex_halbtc8821a2ant_power_on_setting(IN struct btc_coexist *btcoexist);
void ex_halbtc8821a2ant_pre_load_firmware(IN struct btc_coexist *btcoexist);
void ex_halbtc8821a2ant_init_hw_config(IN struct btc_coexist *btcoexist,
				       IN boolean wifi_only);
void ex_halbtc8821a2ant_init_coex_dm(IN struct btc_coexist *btcoexist);
void ex_halbtc8821a2ant_ips_notify(IN struct btc_coexist *btcoexist,
				   IN u8 type);
void ex_halbtc8821a2ant_lps_notify(IN struct btc_coexist *btcoexist,
				   IN u8 type);
void ex_halbtc8821a2ant_scan_notify(IN struct btc_coexist *btcoexist,
				    IN u8 type);
void ex_halbtc8821a2ant_switchband_notify(IN struct btc_coexist *btcoexist,
		IN u8 type);
void ex_halbtc8821a2ant_connect_notify(IN struct btc_coexist *btcoexist,
				       IN u8 type);
void ex_halbtc8821a2ant_media_status_notify(IN struct btc_coexist *btcoexist,
		IN u8 type);
void ex_halbtc8821a2ant_specific_packet_notify(IN struct btc_coexist *btcoexist,
		IN u8 type);
void ex_halbtc8821a2ant_bt_info_notify(IN struct btc_coexist *btcoexist,
				       IN u8 *tmp_buf, IN u8 length);
void ex_halbtc8821a2ant_halt_notify(IN struct btc_coexist *btcoexist);
void ex_halbtc8821a2ant_pnp_notify(IN struct btc_coexist *btcoexist,
				   IN u8 pnp_state);
void ex_halbtc8821a2ant_periodical(IN struct btc_coexist *btcoexist);
void ex_halbtc8821a2ant_display_coex_info(IN struct btc_coexist *btcoexist);

#else
#define	ex_halbtc8821a2ant_power_on_setting(btcoexist)
#define	ex_halbtc8821a2ant_pre_load_firmware(btcoexist)
#define	ex_halbtc8821a2ant_init_hw_config(btcoexist, wifi_only)
#define	ex_halbtc8821a2ant_init_coex_dm(btcoexist)
#define	ex_halbtc8821a2ant_ips_notify(btcoexist, type)
#define	ex_halbtc8821a2ant_lps_notify(btcoexist, type)
#define	ex_halbtc8821a2ant_scan_notify(btcoexist, type)
#define	ex_halbtc8821a2ant_switchband_notify(btcoexist, type)
#define	ex_halbtc8821a2ant_connect_notify(btcoexist, type)
#define	ex_halbtc8821a2ant_media_status_notify(btcoexist, type)
#define	ex_halbtc8821a2ant_specific_packet_notify(btcoexist, type)
#define	ex_halbtc8821a2ant_bt_info_notify(btcoexist, tmp_buf, length)
#define	ex_halbtc8821a2ant_halt_notify(btcoexist)
#define	ex_halbtc8821a2ant_pnp_notify(btcoexist, pnp_state)
#define	ex_halbtc8821a2ant_periodical(btcoexist)
#define	ex_halbtc8821a2ant_display_coex_info(btcoexist)
#endif

#endif

