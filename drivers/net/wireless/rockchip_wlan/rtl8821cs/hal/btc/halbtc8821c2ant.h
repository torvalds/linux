/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/

#if (BT_SUPPORT == 1 && COEX_SUPPORT == 1)

#if (RTL8821C_SUPPORT == 1)

/* *******************************************
 * The following is for 8821C 2Ant BT Co-exist definition
 * ******************************************* */
#define	BT_8821C_2ANT_COEX_DBG				0
#define	BT_AUTO_REPORT_ONLY_8821C_2ANT			1

#define	BT_INFO_8821C_2ANT_B_FTP			BIT(7)
#define	BT_INFO_8821C_2ANT_B_A2DP			BIT(6)
#define	BT_INFO_8821C_2ANT_B_HID			BIT(5)
#define	BT_INFO_8821C_2ANT_B_SCO_BUSY			BIT(4)
#define	BT_INFO_8821C_2ANT_B_ACL_BUSY			BIT(3)
#define	BT_INFO_8821C_2ANT_B_INQ_PAGE			BIT(2)
#define	BT_INFO_8821C_2ANT_B_SCO_ESCO			BIT(1)
#define	BT_INFO_8821C_2ANT_B_CONNECTION			BIT(0)

#define	BTC_RSSI_COEX_THRESH_TOL_8821C_2ANT		2

#define	BT_8821C_2ANT_WIFI_RSSI_COEXSWITCH_THRES1	80
#define	BT_8821C_2ANT_BT_RSSI_COEXSWITCH_THRES1		80
#define	BT_8821C_2ANT_WIFI_RSSI_COEXSWITCH_THRES2	80
#define	BT_8821C_2ANT_BT_RSSI_COEXSWITCH_THRES2		80
#define	BT_8821C_2ANT_DEFAULT_ISOLATION			15
#define   BT_8821C_2ANT_WIFI_MAX_TX_POWER			15
#define   BT_8821C_2ANT_BT_MAX_TX_POWER			3
#define   BT_8821C_2ANT_WIFI_SIR_THRES1			-15
#define   BT_8821C_2ANT_WIFI_SIR_THRES2			-30
#define   BT_8821C_2ANT_BT_SIR_THRES1			-15
#define   BT_8821C_2ANT_BT_SIR_THRES2			-30

enum bt_8821c_2ant_signal_state {
	BT_8821C_2ANT_GNT_SET_TO_LOW	= 0x0,
	BT_8821C_2ANT_GNT_SET_TO_HIGH	= 0x1,
	BT_8821C_2ANT_GNT_SET_BY_HW	= 0x2,
	BT_8821C_2ANT_GNT_SET_MAX
};

enum bt_8821c_2ant_path_ctrl_owner {
	BT_8821C_2ANT_PCO_BTSIDE		= 0x0,
	BT_8821C_2ANT_PCO_WLSIDE		= 0x1,
	BT_8821C_2ANT_PCO_MAX
};

enum bt_8821c_2ant_gnt_ctrl_type {
	BT_8821C_2ANT_GNT_TYPE_CTRL_BY_PTA	= 0x0,
	BT_8821C_2ANT_GNT_TYPE_CTRL_BY_SW	= 0x1,
	BT_8821C_2ANT_GNT_TYPE_MAX
};

enum bt_8821c_2ant_gnt_ctrl_block {
	BT_8821C_2ANT_GNT_BLOCK_RFC_BB		= 0x0,
	BT_8821C_2ANT_GNT_BLOCK_RFC		= 0x1,
	BT_8821C_2ANT_GNT_BLOCK_BB		= 0x2,
	BT_8821C_2ANT_GNT_BLOCK_MAX
};

enum bt_8821c_2ant_lte_coex_table_type {
	BT_8821C_2ANT_CTT_WL_VS_LTE		= 0x0,
	BT_8821C_2ANT_CTT_BT_VS_LTE		= 0x1,
	BT_8821C_2ANT_CTT_MAX
};

enum bt_8821c_2ant_lte_break_table_type {
	BT_8821C_2ANT_LBTT_WL_BREAK_LTE		= 0x0,
	BT_8821C_2ANT_LBTT_BT_BREAK_LTE		= 0x1,
	BT_8821C_2ANT_LBTT_LTE_BREAK_WL		= 0x2,
	BT_8821C_2ANT_LBTT_LTE_BREAK_BT		= 0x3,
	BT_8821C_2ANT_LBTT_MAX
};

enum bt_info_src_8821c_2ant {
	BT_8821C_2ANT_INFO_SRC_WIFI_FW		= 0x0,
	BT_8821C_2ANT_INFO_SRC_BT_RSP		= 0x1,
	BT_8821C_2ANT_INFO_SRC_BT_ACT		= 0x2,
	BT_8821C_2ANT_INFO_SRC_MAX
};

enum bt_8821c_2ant_bt_status {
	BT_8821C_2ANT_BSTATUS_NCON_IDLE		= 0x0,
	BT_8821C_2ANT_BSTATUS_CON_IDLE		= 0x1,
	BT_8821C_2ANT_BSTATUS_INQ_PAGE		= 0x2,
	BT_8821C_2ANT_BSTATUS_ACL_BUSY		= 0x3,
	BT_8821C_2ANT_BSTATUS_SCO_BUSY		= 0x4,
	BT_8821C_2ANT_BSTATUS_ACL_SCO_BUSY	= 0x5,
	BT_8821C_2ANT_BSTATUS_MAX
};

enum bt_8821c_2ant_coex_algo {
	BT_8821C_2ANT_COEX_UNDEFINED		= 0x0,
	BT_8821C_2ANT_COEX_SCO			= 0x1,
	BT_8821C_2ANT_COEX_HID			= 0x2,
	BT_8821C_2ANT_COEX_A2DP			= 0x3,
	BT_8821C_2ANT_COEX_A2DP_PANHS		= 0x4,
	BT_8821C_2ANT_COEX_PAN			= 0x5,
	BT_8821C_2ANT_COEX_PANHS		= 0x6,
	BT_8821C_2ANT_COEX_PAN_A2DP		= 0x7,
	BT_8821C_2ANT_COEX_PAN_HID		= 0x8,
	BT_8821C_2ANT_COEX_HID_A2DP_PAN		= 0x9,
	BT_8821C_2ANT_COEX_HID_A2DP		= 0xa,
	BT_8821C_2ANT_COEX_NOPROFILEBUSY	= 0xb,
	BT_8821C_2ANT_COEX_A2DPSINK		= 0xc,
	BT_8821C_2ANT_COEX_MAX
};

enum bt_8821c_2ant_ext_ant_switch_type {
	BT_8821C_2ANT_USE_DPDT		= 0x0,
	BT_8821C_2ANT_USE_SPDT		= 0x1,
	BT_8821C_2ANT_SWITCH_NONE	= 0x2,
	BT_8821C_2ANT_SWITCH_MAX
};

enum bt_8821c_2ant_ext_ant_switch_ctrl_type {
	BT_8821C_2ANT_CTRL_BY_BBSW	= 0x0,
	BT_8821C_2ANT_CTRL_BY_PTA	= 0x1,
	BT_8821C_2ANT_CTRL_BY_ANTDIV	= 0x2,
	BT_8821C_2ANT_CTRL_BY_MAC	= 0x3,
	BT_8821C_2ANT_CTRL_BY_BT	= 0x4,
	BT_8821C_2ANT_CTRL_BY_FW	= 0x5,
	BT_8821C_2ANT_CTRL_MAX
};

enum bt_8821c_2ant_ext_ant_switch_pos_type {
	BT_8821C_2ANT_TO_BT		= 0x0,
	BT_8821C_2ANT_TO_WLG		= 0x1,
	BT_8821C_2ANT_TO_WLA		= 0x2,
	BT_8821C_2ANT_TO_NOCARE		= 0x3,
	BT_8821C_2ANT_TO_MAX
};

enum bt_8821c_2ant_phase {
	BT_8821C_2ANT_PHASE_INIT		= 0x0,
	BT_8821C_2ANT_PHASE_WONLY		= 0x1,
	BT_8821C_2ANT_PHASE_WOFF		= 0x2,
	BT_8821C_2ANT_PHASE_2G			= 0x3,
	BT_8821C_2ANT_PHASE_5G			= 0x4,
	BT_8821C_2ANT_PHASE_BTMP		= 0x5,
	BT_8821C_2ANT_PHASE_ANTDET		= 0x6,
	BT_8821C_2ANT_PHASE_POWERON		= 0x7,
	BT_8821C_2ANT_PHASE_MAX
};

enum bt_8821c_2ant_scoreboard {
	BT_8821C_2ANT_SCBD_ACTIVE		= BIT(0),
	BT_8821C_2ANT_SCBD_ONOFF		= BIT(1),
	BT_8821C_2ANT_SCBD_SCAN			= BIT(2),
	BT_8821C_2ANT_SCBD_UNDERTEST		= BIT(3),
	BT_8821C_2ANT_SCBD_RXGAIN		= BIT(4),
	BT_8821C_2ANT_SCBD_WLBUSY		= BIT(6),
	BT_8821C_2ANT_SCBD_TDMA			= BIT(9),
	BT_8821C_2ANT_SCBD_BTCQDDR		= BIT(10),
	BT_8821C_2ANT_SCBD_ALL			= 0xffff
};

enum bt_8821c_2ant_RUNREASON {
	BT_8821C_2ANT_RSN_2GSCANSTART		= 0x0,
	BT_8821C_2ANT_RSN_5GSCANSTART		= 0x1,
	BT_8821C_2ANT_RSN_SCANFINISH		= 0x2,
	BT_8821C_2ANT_RSN_2GSWITCHBAND		= 0x3,
	BT_8821C_2ANT_RSN_5GSWITCHBAND		= 0x4,
	BT_8821C_2ANT_RSN_2GCONSTART		= 0x5,
	BT_8821C_2ANT_RSN_5GCONSTART		= 0x6,
	BT_8821C_2ANT_RSN_2GCONFINISH		= 0x7,
	BT_8821C_2ANT_RSN_5GCONFINISH		= 0x8,
	BT_8821C_2ANT_RSN_2GMEDIA		= 0x9,
	BT_8821C_2ANT_RSN_5GMEDIA		= 0xa,
	BT_8821C_2ANT_RSN_MEDIADISCON		= 0xb,
	BT_8821C_2ANT_RSN_2GSPECIALPKT		= 0xc,
	BT_8821C_2ANT_RSN_5GSPECIALPKT		= 0xd,
	BT_8821C_2ANT_RSN_BTINFO		= 0xe,
	BT_8821C_2ANT_RSN_PERIODICAL		= 0xf,
	BT_8821C_2ANT_RSN_PNP			= 0x10,
	BT_8821C_2ANT_RSN_LPS			= 0x11,
	BT_8821C_2ANT_RSN_MAX
};

enum bt_8821c_2ant_WL_LINK_MODE {
	BT_8821C_2ANT_WLINK_2G1PORT		= 0x0,
	BT_8821C_2ANT_WLINK_2GMPORT		= 0x1,
	BT_8821C_2ANT_WLINK_25GMPORT		= 0x2,
	BT_8821C_2ANT_WLINK_5G			= 0x3,
	BT_8821C_2ANT_WLINK_2GGO		= 0x4,
	BT_8821C_2ANT_WLINK_BTMR		= 0x5,
	BT_8821C_2ANT_WLINK_MAX
};

struct coex_dm_8821c_2ant {
	/* hw setting */
	u32		cur_ant_pos_type;
	/* fw mechanism */

	u8		cur_bt_pwr_lvl;
	u8		cur_wl_pwr_lvl;

	boolean		cur_ignore_wlan_act;

	u8		cur_ps_tdma;
	u8		ps_tdma_para[5];
	boolean		reset_tdma_adjust;
	boolean		cur_ps_tdma_on;
	boolean		cur_bt_auto_report;

	/* sw mechanism */
	boolean		cur_low_penalty_ra;

	u32		cur_val0x6c0;
	u32		cur_val0x6c4;
	u32		cur_val0x6c8;
	u8		cur_val0x6cc;

	/* algorithm related */
	u8		cur_algorithm;
	u8		bt_status;
	u8		wifi_chnl_info[3];

	u8		cur_lps;
	u8		cur_rpwm;

	u32		arp_cnt;

	u32		cur_ext_ant_switch_status;

	u8		cur_antdiv_type;
	u32		setting_tdma;
};

struct coex_sta_8821c_2ant {
	boolean	bt_disabled;
	boolean	bt_link_exist;
	boolean	sco_exist;
	boolean	a2dp_exist;
	boolean	hid_exist;
	boolean	pan_exist;
	boolean	msft_mr_exist;
	boolean bt_a2dp_active;

	boolean	under_lps;
	boolean	under_ips;
	u32	high_priority_tx;
	u32	high_priority_rx;
	u32	low_priority_tx;
	u32	low_priority_rx;
	boolean bt_ctr_ok;
	boolean	is_hi_pri_rx_overhead;
	u8	bt_rssi;
	u8	pre_bt_rssi_state;
	u8	pre_wifi_rssi_state[4];
	u8	bt_info_c2h[BT_8821C_2ANT_INFO_SRC_MAX][BTC_BTINFO_LENGTH_MAX];
	u32	bt_info_c2h_cnt[BT_8821C_2ANT_INFO_SRC_MAX];
	boolean	bt_whck_test;
	boolean	c2h_bt_inquiry_page;
	boolean bt_inq_page_pre;
	boolean bt_inq_page_remain;
	boolean	c2h_bt_remote_name_req;

	u8	bt_info_lb2;
	u8	bt_info_lb3;
	u8	bt_info_hb0;
	u8	bt_info_hb1;
	u8	bt_info_hb2;
	u8	bt_info_hb3;

	u32	pop_event_cnt;
	u8	scan_ap_num;
	u8	bt_retry_cnt;

	u32	crc_ok_cck;
	u32	crc_ok_11g;
	u32	crc_ok_11n;
	u32	crc_ok_11n_vht;
	u32	crc_err_cck;
	u32	crc_err_11g;
	u32	crc_err_11n;
	u32	crc_err_11n_vht;
	u32	cnt_crcok_max_in_10s;

	boolean	cck_lock;
	boolean	cck_lock_ever;
	boolean	cck_lock_warn;

	u8	coex_table_type;
	boolean	force_lps_ctrl;

	u8	dis_ver_info_cnt;

	u8	a2dp_bit_pool;
	u8	kt_ver;

	boolean	concurrent_rx_mode_on;

	u16	score_board;
	u8	isolation_btween_wb;   /* 0~ 50 */
	u8	wifi_coex_thres;
	u8	bt_coex_thres;
	u8	wifi_coex_thres2;
	u8	bt_coex_thres2;

	u8	num_of_profile;
	boolean	acl_busy;
	boolean	bt_create_connection;

	boolean	wifi_high_pri_task1;
	boolean	wifi_high_pri_task2;

	u32	specific_pkt_period_cnt;
	u32	bt_coex_supported_feature;
	u32	bt_coex_supported_version;

	u8	bt_ble_scan_type;
	u32	bt_ble_scan_para[3];

	boolean	run_time_state;
	boolean	freeze_coexrun_by_btinfo;

	boolean	is_A2DP_3M;
	boolean	voice_over_HOGP;
	boolean	bt_418_hid_exist;
	boolean	bt_ble_hid_exist;
	u8	forbidden_slot;
	u8	hid_busy_num;
	u8	hid_pair_cnt;

	u32	cnt_remote_name_req;
	u32	cnt_setup_link;
	u32	cnt_reinit;
	u32	cnt_ign_wlan_act;
	u32	cnt_page;
	u32	cnt_role_switch;
	u32	cnt_wl_fw_notify;

	u16	bt_reg_vendor_ac;
	u16	bt_reg_vendor_ae;

	boolean	is_setup_link;
	u8	wl_noisy_level;
	u32	gnt_error_cnt;

	u8	bt_afh_map[10];
	u8	bt_relink_downcount;
	u8	bt_inq_page_downcount;
	boolean	is_tdma_btautoslot;

	boolean	is_esco_mode;
	u8	switch_band_notify_to;

	boolean	is_hid_low_pri_tx_overhead;
	boolean	is_bt_multi_link;
	boolean	is_bt_a2dp_sink;
	boolean	is_set_ps_state_fail;
	u8	cnt_set_ps_state_fail;

	u8	wl_fw_dbg_info[10];
	u8	wl_rx_rate;
	u8	wl_tx_rate;
	u8	wl_rts_rx_rate;
	u8	wl_center_channel;
	u8	wl_tx_macid;
	u8	wl_tx_retry_ratio;

	u16	score_board_WB;
	boolean	is_hid_rcu;
	u8	bt_a2dp_vendor_id;
	u32	bt_a2dp_device_name;
	u32	bt_a2dp_flush_time;
	boolean	is_ble_scan_en;

	boolean	is_bt_opp_exist;
	boolean	gl_wifi_busy;
	u8	connect_ap_period_cnt;

	boolean	is_bt_reenable;
	u8	cnt_bt_reenable;
	boolean	is_wifi_linkscan_process;
	u8	wl_coex_mode;
	u8	wl_pnp_wakeup_downcnt;
	u32	coex_run_cnt;
	boolean	is_no_wl_5ms_extend;

	u16	wl_0x42a_backup;
	u32	wl_0x430_backup;
	u32	wl_0x434_backup;
	u8	wl_0x455_backup;

	boolean	wl_tx_limit_en;
	boolean	wl_ampdu_limit_en;
	boolean	wl_rxagg_limit_en;
	u8	wl_rxagg_size;
	u8	coex_run_reason;

	u8	tdma_timer_base;
	boolean wl_slot_toggle;
	boolean wl_slot_toggle_change; /* if toggle to no-toggle */
	u8	wl_iot_peer;
};

#define  BT_8821C_2ANT_EXT_BAND_SWITCH_USE_DPDT	0
#define  BT_8821C_2ANT_EXT_BAND_SWITCH_USE_SPDT	1

struct rfe_type_8821c_2ant {
	u8		rfe_module_type;
	boolean		ext_ant_switch_exist;
	/* 0:DPDT, 1:SPDT */
	u8		ext_ant_switch_type;
	/*  iF 0: DPDT_P=0, DPDT_N=1 => BTG to Main, WL_A+G to Aux */
	u8		ext_ant_switch_ctrl_polarity;

	boolean		ext_band_switch_exist;
	/* 0:DPDT, 1:SPDT */
	u8		ext_band_switch_type;
	u8		ext_band_switch_ctrl_polarity;

	boolean		ant_at_main_port;

	/*  If TRUE:  WLG at BTG, If FALSE: WLG at WLAG */
	boolean		wlg_locate_at_btg;

	/* If diversity on */
	boolean		ext_ant_switch_diversity;
};

struct wifi_link_info_8821c_2ant {
	u8	num_of_active_port;
	u32	port_connect_status;
	boolean	is_all_under_5g;
	boolean	is_mcc_25g;
	boolean	is_p2p_connected;
	boolean is_connected;
};

/* *******************************************
 * The following is interface which will notify coex module.
 * ******************************************* */
void ex_halbtc8821c2ant_power_on_setting(struct btc_coexist *btc);
void ex_halbtc8821c2ant_pre_load_firmware(struct btc_coexist *btc);
void ex_halbtc8821c2ant_init_hw_config(struct btc_coexist *btc,
				       boolean wifi_only);
void ex_halbtc8821c2ant_init_coex_dm(struct btc_coexist *btc);
void ex_halbtc8821c2ant_ips_notify(struct btc_coexist *btc,
				   u8 type);
void ex_halbtc8821c2ant_lps_notify(struct btc_coexist *btc,
				   u8 type);
void ex_halbtc8821c2ant_scan_notify(struct btc_coexist *btc,
				    u8 type);
void ex_halbtc8821c2ant_switchband_notify(struct btc_coexist *btc,
					  u8 type);
void ex_halbtc8821c2ant_connect_notify(struct btc_coexist *btc,
				       u8 type);
void ex_halbtc8821c2ant_media_status_notify(struct btc_coexist *btc,
					    u8 type);
void ex_halbtc8821c2ant_specific_packet_notify(struct btc_coexist *btc,
					       u8 type);
void ex_halbtc8821c2ant_bt_info_notify(struct btc_coexist *btc,
				       u8 *tmp_buf,  u8 length);
void ex_halbtc8821c2ant_wl_fwdbginfo_notify(struct btc_coexist *btc,
					    u8 *tmp_buf, u8 length);
void ex_halbtc8821c2ant_rx_rate_change_notify(struct btc_coexist *btc,
					      BOOLEAN is_data_frame,
					      u8 btc_rate_id);
void ex_halbtc8821c2ant_tx_rate_change_notify(struct btc_coexist *btc,
					      u8 tx_rate,
					      u8 tx_retry_ratio, u8 macid);
void ex_halbtc8821c2ant_rf_status_notify(struct btc_coexist *btc,
					 u8 type);
void ex_halbtc8821c2ant_halt_notify(struct btc_coexist *btc);
void ex_halbtc8821c2ant_pnp_notify(struct btc_coexist *btc,
				   u8 pnp_state);
void ex_halbtc8821c2ant_periodical(struct btc_coexist *btc);
void ex_halbtc8821c2ant_display_simple_coex_info(struct btc_coexist *btc);
void ex_halbtc8821c2ant_display_coex_info(struct btc_coexist *btc);

#else
#define ex_halbtc8821c2ant_power_on_setting(btc)
#define ex_halbtc8821c2ant_pre_load_firmware(btc)
#define ex_halbtc8821c2ant_init_hw_config(btc, wifi_only)
#define ex_halbtc8821c2ant_init_coex_dm(btc)
#define ex_halbtc8821c2ant_ips_notify(btc, type)
#define ex_halbtc8821c2ant_lps_notify(btc, type)
#define ex_halbtc8821c2ant_scan_notify(btc, type)
#define ex_halbtc8821c2ant_switchband_notify(btc, type)
#define ex_halbtc8821c2ant_connect_notify(btc, type)
#define ex_halbtc8821c2ant_media_status_notify(btc, type)
#define ex_halbtc8821c2ant_specific_packet_notify(btc, type)
#define ex_halbtc8821c2ant_bt_info_notify(btc, tmp_buf, length)
#define ex_halbtc8821c2ant_wl_fwdbginfo_notify(btc, tmp_buf, length)
#define ex_halbtc8821c2ant_rx_rate_change_notify(btc, is_data_frame,     \
						 btc_rate_id)
#define ex_halbtc8821c2ant_tx_rate_change_notify(btcoexist, tx_rate,     \
						tx_retry_ratio, macid)
#define ex_halbtc8821c2ant_rf_status_notify(btc, type)
#define ex_halbtc8821c2ant_halt_notify(btc)
#define ex_halbtc8821c2ant_pnp_notify(btc, pnp_state)
#define ex_halbtc8821c2ant_periodical(btc)
#define ex_halbtc8821c2ant_display_simple_coex_info(btc)
#define ex_halbtc8821c2ant_display_coex_info(btc)
#endif

#endif


