/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
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
#ifndef	__HALBTC_OUT_SRC_H__
#define __HALBTC_OUT_SRC_H__

#include	"../wifi.h"

#define		NORMAL_EXEC				false
#define		FORCE_EXEC				true

#define		BTC_RF_OFF				0x0
#define		BTC_RF_ON				0x1

#define		BTC_RF_A				RF90_PATH_A
#define		BTC_RF_B				RF90_PATH_B
#define		BTC_RF_C				RF90_PATH_C
#define		BTC_RF_D				RF90_PATH_D

#define		BTC_SMSP				SINGLEMAC_SINGLEPHY
#define		BTC_DMDP				DUALMAC_DUALPHY
#define		BTC_DMSP				DUALMAC_SINGLEPHY
#define		BTC_MP_UNKNOWN				0xff

#define		IN
#define		OUT

#define		BT_TMP_BUF_SIZE				100

#define		BT_COEX_ANT_TYPE_PG			0
#define		BT_COEX_ANT_TYPE_ANTDIV			1
#define		BT_COEX_ANT_TYPE_DETECTED		2

#define		BTC_MIMO_PS_STATIC			0
#define		BTC_MIMO_PS_DYNAMIC			1

#define		BTC_RATE_DISABLE			0
#define		BTC_RATE_ENABLE				1

/* single Antenna definition */
#define		BTC_ANT_PATH_WIFI			0
#define		BTC_ANT_PATH_BT				1
#define		BTC_ANT_PATH_PTA			2
/* dual Antenna definition */
#define		BTC_ANT_WIFI_AT_MAIN			0
#define		BTC_ANT_WIFI_AT_AUX			1
/* coupler Antenna definition */
#define		BTC_ANT_WIFI_AT_CPL_MAIN		0
#define		BTC_ANT_WIFI_AT_CPL_AUX			1

enum btc_bt_reg_type {
	BTC_BT_REG_RF		= 0,
	BTC_BT_REG_MODEM	= 1,
	BTC_BT_REG_BLUEWIZE	= 2,
	BTC_BT_REG_VENDOR	= 3,
	BTC_BT_REG_LE		= 4,
	BTC_BT_REG_MAX
};

enum btc_chip_interface {
	BTC_INTF_UNKNOWN	= 0,
	BTC_INTF_PCI		= 1,
	BTC_INTF_USB		= 2,
	BTC_INTF_SDIO		= 3,
	BTC_INTF_GSPI		= 4,
	BTC_INTF_MAX
};

enum btc_chip_type {
	BTC_CHIP_UNDEF		= 0,
	BTC_CHIP_CSR_BC4	= 1,
	BTC_CHIP_CSR_BC8	= 2,
	BTC_CHIP_RTL8723A	= 3,
	BTC_CHIP_RTL8821	= 4,
	BTC_CHIP_RTL8723B	= 5,
	BTC_CHIP_MAX
};

enum btc_msg_type {
	BTC_MSG_INTERFACE	= 0x0,
	BTC_MSG_ALGORITHM	= 0x1,
	BTC_MSG_MAX
};

/* following is for BTC_MSG_INTERFACE */
#define		INTF_INIT				BIT0
#define		INTF_NOTIFY				BIT2

/* following is for BTC_ALGORITHM */
#define		ALGO_BT_RSSI_STATE			BIT0
#define		ALGO_WIFI_RSSI_STATE			BIT1
#define		ALGO_BT_MONITOR				BIT2
#define		ALGO_TRACE				BIT3
#define		ALGO_TRACE_FW				BIT4
#define		ALGO_TRACE_FW_DETAIL			BIT5
#define		ALGO_TRACE_FW_EXEC			BIT6
#define		ALGO_TRACE_SW				BIT7
#define		ALGO_TRACE_SW_DETAIL			BIT8
#define		ALGO_TRACE_SW_EXEC			BIT9

/* following is for wifi link status */
#define		WIFI_STA_CONNECTED			BIT0
#define		WIFI_AP_CONNECTED			BIT1
#define		WIFI_HS_CONNECTED			BIT2
#define		WIFI_P2P_GO_CONNECTED			BIT3
#define		WIFI_P2P_GC_CONNECTED			BIT4

#define	BTC_RSSI_HIGH(_rssi_)	\
	((_rssi_ == BTC_RSSI_STATE_HIGH ||	\
	  _rssi_ == BTC_RSSI_STATE_STAY_HIGH) ? true : false)
#define	BTC_RSSI_MEDIUM(_rssi_)	\
	((_rssi_ == BTC_RSSI_STATE_MEDIUM ||	\
	  _rssi_ == BTC_RSSI_STATE_STAY_MEDIUM) ? true : false)
#define	BTC_RSSI_LOW(_rssi_)	\
	((_rssi_ == BTC_RSSI_STATE_LOW ||	\
	  _rssi_ == BTC_RSSI_STATE_STAY_LOW) ? true : false)

enum btc_power_save_type {
	BTC_PS_WIFI_NATIVE = 0,
	BTC_PS_LPS_ON = 1,
	BTC_PS_LPS_OFF = 2,
	BTC_PS_LPS_MAX
};

struct btc_board_info {
	/* The following is some board information */
	u8 bt_chip_type;
	u8 pg_ant_num;	/* pg ant number */
	u8 btdm_ant_num;	/* ant number for btdm */
	u8 btdm_ant_pos;
	u8 single_ant_path; /* current used for 8723b only, 1=>s0,  0=>s1 */
	bool tfbga_package;

	u8 rfe_type;
	u8 ant_div_cfg;
	u8 customer_id;
};

enum btc_dbg_opcode {
	BTC_DBG_SET_COEX_NORMAL = 0x0,
	BTC_DBG_SET_COEX_WIFI_ONLY = 0x1,
	BTC_DBG_SET_COEX_BT_ONLY = 0x2,
	BTC_DBG_MAX
};

enum btc_rssi_state {
	BTC_RSSI_STATE_HIGH = 0x0,
	BTC_RSSI_STATE_MEDIUM = 0x1,
	BTC_RSSI_STATE_LOW = 0x2,
	BTC_RSSI_STATE_STAY_HIGH = 0x3,
	BTC_RSSI_STATE_STAY_MEDIUM = 0x4,
	BTC_RSSI_STATE_STAY_LOW = 0x5,
	BTC_RSSI_MAX
};

enum btc_wifi_role {
	BTC_ROLE_STATION = 0x0,
	BTC_ROLE_AP = 0x1,
	BTC_ROLE_IBSS = 0x2,
	BTC_ROLE_HS_MODE = 0x3,
	BTC_ROLE_MAX
};

enum btc_wireless_freq {
	BTC_FREQ_2_4G = 0x0,
	BTC_FREQ_5G = 0x1,
	BTC_FREQ_MAX
};

enum btc_wifi_bw_mode {
	BTC_WIFI_BW_LEGACY = 0x0,
	BTC_WIFI_BW_HT20 = 0x1,
	BTC_WIFI_BW_HT40 = 0x2,
	BTC_WIFI_BW_HT80 = 0x3,
	BTC_WIFI_BW_MAX
};

enum btc_wifi_traffic_dir {
	BTC_WIFI_TRAFFIC_TX = 0x0,
	BTC_WIFI_TRAFFIC_RX = 0x1,
	BTC_WIFI_TRAFFIC_MAX
};

enum btc_wifi_pnp {
	BTC_WIFI_PNP_WAKE_UP = 0x0,
	BTC_WIFI_PNP_SLEEP = 0x1,
	BTC_WIFI_PNP_MAX
};

enum btc_iot_peer {
	BTC_IOT_PEER_UNKNOWN = 0,
	BTC_IOT_PEER_REALTEK = 1,
	BTC_IOT_PEER_REALTEK_92SE = 2,
	BTC_IOT_PEER_BROADCOM = 3,
	BTC_IOT_PEER_RALINK = 4,
	BTC_IOT_PEER_ATHEROS = 5,
	BTC_IOT_PEER_CISCO = 6,
	BTC_IOT_PEER_MERU = 7,
	BTC_IOT_PEER_MARVELL = 8,
	BTC_IOT_PEER_REALTEK_SOFTAP = 9,
	BTC_IOT_PEER_SELF_SOFTAP = 10, /* Self is SoftAP */
	BTC_IOT_PEER_AIRGO = 11,
	BTC_IOT_PEER_REALTEK_JAGUAR_BCUTAP = 12,
	BTC_IOT_PEER_REALTEK_JAGUAR_CCUTAP = 13,
	BTC_IOT_PEER_MAX,
};

/* for 8723b-d cut large current issue */
enum bt_wifi_coex_state {
	BTC_WIFI_STAT_INIT,
	BTC_WIFI_STAT_IQK,
	BTC_WIFI_STAT_NORMAL_OFF,
	BTC_WIFI_STAT_MP_OFF,
	BTC_WIFI_STAT_NORMAL,
	BTC_WIFI_STAT_ANT_DIV,
	BTC_WIFI_STAT_MAX
};

enum bt_ant_type {
	BTC_ANT_TYPE_0,
	BTC_ANT_TYPE_1,
	BTC_ANT_TYPE_2,
	BTC_ANT_TYPE_3,
	BTC_ANT_TYPE_4,
	BTC_ANT_TYPE_MAX
};

enum btc_get_type {
	/* type bool */
	BTC_GET_BL_HS_OPERATION,
	BTC_GET_BL_HS_CONNECTING,
	BTC_GET_BL_WIFI_CONNECTED,
	BTC_GET_BL_WIFI_DUAL_BAND_CONNECTED,
	BTC_GET_BL_WIFI_BUSY,
	BTC_GET_BL_WIFI_SCAN,
	BTC_GET_BL_WIFI_LINK,
	BTC_GET_BL_WIFI_DHCP,
	BTC_GET_BL_WIFI_SOFTAP_IDLE,
	BTC_GET_BL_WIFI_SOFTAP_LINKING,
	BTC_GET_BL_WIFI_IN_EARLY_SUSPEND,
	BTC_GET_BL_WIFI_ROAM,
	BTC_GET_BL_WIFI_4_WAY_PROGRESS,
	BTC_GET_BL_WIFI_UNDER_5G,
	BTC_GET_BL_WIFI_AP_MODE_ENABLE,
	BTC_GET_BL_WIFI_ENABLE_ENCRYPTION,
	BTC_GET_BL_WIFI_UNDER_B_MODE,
	BTC_GET_BL_EXT_SWITCH,
	BTC_GET_BL_WIFI_IS_IN_MP_MODE,
	BTC_GET_BL_IS_ASUS_8723B,
	BTC_GET_BL_FW_READY,
	BTC_GET_BL_RF4CE_CONNECTED,

	/* type s4Byte */
	BTC_GET_S4_WIFI_RSSI,
	BTC_GET_S4_HS_RSSI,

	/* type u32 */
	BTC_GET_U4_WIFI_BW,
	BTC_GET_U4_WIFI_TRAFFIC_DIRECTION,
	BTC_GET_U4_WIFI_FW_VER,
	BTC_GET_U4_WIFI_LINK_STATUS,
	BTC_GET_U4_BT_PATCH_VER,
	BTC_GET_U4_VENDOR,
	BTC_GET_U4_SUPPORTED_VERSION,
	BTC_GET_U4_SUPPORTED_FEATURE,
	BTC_GET_U4_BT_DEVICE_INFO,
	BTC_GET_U4_BT_FORBIDDEN_SLOT_VAL,
	BTC_GET_U4_WIFI_IQK_TOTAL,
	BTC_GET_U4_WIFI_IQK_OK,
	BTC_GET_U4_WIFI_IQK_FAIL,

	/* type u1Byte */
	BTC_GET_U1_WIFI_DOT11_CHNL,
	BTC_GET_U1_WIFI_CENTRAL_CHNL,
	BTC_GET_U1_WIFI_HS_CHNL,
	BTC_GET_U1_MAC_PHY_MODE,
	BTC_GET_U1_AP_NUM,
	BTC_GET_U1_ANT_TYPE,
	BTC_GET_U1_IOT_PEER,

	/* for 1Ant */
	BTC_GET_U1_LPS_MODE,
	BTC_GET_BL_BT_SCO_BUSY,

	/* for test mode */
	BTC_GET_DRIVER_TEST_CFG,
	BTC_GET_MAX
};

enum btc_vendor {
	BTC_VENDOR_LENOVO,
	BTC_VENDOR_ASUS,
	BTC_VENDOR_OTHER
};

enum btc_set_type {
	/* type bool */
	BTC_SET_BL_BT_DISABLE,
	BTC_SET_BL_BT_TRAFFIC_BUSY,
	BTC_SET_BL_BT_LIMITED_DIG,
	BTC_SET_BL_FORCE_TO_ROAM,
	BTC_SET_BL_TO_REJ_AP_AGG_PKT,
	BTC_SET_BL_BT_CTRL_AGG_SIZE,
	BTC_SET_BL_INC_SCAN_DEV_NUM,
	BTC_SET_BL_BT_TX_RX_MASK,
	BTC_SET_BL_MIRACAST_PLUS_BT,

	/* type u1Byte */
	BTC_SET_U1_RSSI_ADJ_VAL_FOR_AGC_TABLE_ON,
	BTC_SET_UI_SCAN_SIG_COMPENSATION,
	BTC_SET_U1_AGG_BUF_SIZE,

	/* type trigger some action */
	BTC_SET_ACT_GET_BT_RSSI,
	BTC_SET_ACT_AGGREGATE_CTRL,
	BTC_SET_ACT_ANTPOSREGRISTRY_CTRL,

	/********* for 1Ant **********/
	/* type bool */
	BTC_SET_BL_BT_SCO_BUSY,
	/* type u1Byte */
	BTC_SET_U1_RSSI_ADJ_VAL_FOR_1ANT_COEX_TYPE,
	BTC_SET_U1_LPS_VAL,
	BTC_SET_U1_RPWM_VAL,
	BTC_SET_U1_1ANT_LPS,
	BTC_SET_U1_1ANT_RPWM,
	/* type trigger some action */
	BTC_SET_ACT_LEAVE_LPS,
	BTC_SET_ACT_ENTER_LPS,
	BTC_SET_ACT_NORMAL_LPS,
	BTC_SET_ACT_INC_FORCE_EXEC_PWR_CMD_CNT,
	BTC_SET_ACT_DISABLE_LOW_POWER,
	BTC_SET_ACT_UPDATE_RAMASK,
	BTC_SET_ACT_SEND_MIMO_PS,
	/* BT Coex related */
	BTC_SET_ACT_CTRL_BT_INFO,
	BTC_SET_ACT_CTRL_BT_COEX,
	BTC_SET_ACT_CTRL_8723B_ANT,
	/***************************/
	BTC_SET_MAX
};

enum btc_dbg_disp_type {
	BTC_DBG_DISP_COEX_STATISTICS = 0x0,
	BTC_DBG_DISP_BT_LINK_INFO = 0x1,
	BTC_DBG_DISP_BT_FW_VER = 0x2,
	BTC_DBG_DISP_FW_PWR_MODE_CMD = 0x3,
	BTC_DBG_DISP_WIFI_STATUS = 0x04,
	BTC_DBG_DISP_MAX
};

enum btc_notify_type_ips {
	BTC_IPS_LEAVE = 0x0,
	BTC_IPS_ENTER = 0x1,
	BTC_IPS_MAX
};

enum btc_notify_type_lps {
	BTC_LPS_DISABLE = 0x0,
	BTC_LPS_ENABLE = 0x1,
	BTC_LPS_MAX
};

enum btc_notify_type_scan {
	BTC_SCAN_FINISH = 0x0,
	BTC_SCAN_START = 0x1,
	BTC_SCAN_MAX
};

enum btc_notify_type_switchband {
	BTC_NOT_SWITCH = 0x0,
	BTC_SWITCH_TO_24G = 0x1,
	BTC_SWITCH_TO_5G = 0x2,
	BTC_SWITCH_TO_24G_NOFORSCAN = 0x3,
	BTC_SWITCH_MAX
};

enum btc_notify_type_associate {
	BTC_ASSOCIATE_FINISH = 0x0,
	BTC_ASSOCIATE_START = 0x1,
	BTC_ASSOCIATE_MAX
};

enum btc_notify_type_media_status {
	BTC_MEDIA_DISCONNECT = 0x0,
	BTC_MEDIA_CONNECT = 0x1,
	BTC_MEDIA_MAX
};

enum btc_notify_type_special_packet {
	BTC_PACKET_UNKNOWN = 0x0,
	BTC_PACKET_DHCP = 0x1,
	BTC_PACKET_ARP = 0x2,
	BTC_PACKET_EAPOL = 0x3,
	BTC_PACKET_MAX
};

enum hci_ext_bt_operation {
	HCI_BT_OP_NONE = 0x0,
	HCI_BT_OP_INQUIRY_START = 0x1,
	HCI_BT_OP_INQUIRY_FINISH = 0x2,
	HCI_BT_OP_PAGING_START = 0x3,
	HCI_BT_OP_PAGING_SUCCESS = 0x4,
	HCI_BT_OP_PAGING_UNSUCCESS = 0x5,
	HCI_BT_OP_PAIRING_START = 0x6,
	HCI_BT_OP_PAIRING_FINISH = 0x7,
	HCI_BT_OP_BT_DEV_ENABLE = 0x8,
	HCI_BT_OP_BT_DEV_DISABLE = 0x9,
	HCI_BT_OP_MAX
};

enum btc_notify_type_stack_operation {
	BTC_STACK_OP_NONE = 0x0,
	BTC_STACK_OP_INQ_PAGE_PAIR_START = 0x1,
	BTC_STACK_OP_INQ_PAGE_PAIR_FINISH = 0x2,
	BTC_STACK_OP_MAX
};

enum {
	BTC_CCK_1,
	BTC_CCK_2,
	BTC_CCK_5_5,
	BTC_CCK_11,
	BTC_OFDM_6,
	BTC_OFDM_9,
	BTC_OFDM_12,
	BTC_OFDM_18,
	BTC_OFDM_24,
	BTC_OFDM_36,
	BTC_OFDM_48,
	BTC_OFDM_54,
	BTC_MCS_0,
	BTC_MCS_1,
	BTC_MCS_2,
	BTC_MCS_3,
	BTC_MCS_4,
	BTC_MCS_5,
	BTC_MCS_6,
	BTC_MCS_7,
	BTC_MCS_8,
	BTC_MCS_9,
	BTC_MCS_10,
	BTC_MCS_11,
	BTC_MCS_12,
	BTC_MCS_13,
	BTC_MCS_14,
	BTC_MCS_15,
	BTC_MCS_16,
	BTC_MCS_17,
	BTC_MCS_18,
	BTC_MCS_19,
	BTC_MCS_20,
	BTC_MCS_21,
	BTC_MCS_22,
	BTC_MCS_23,
	BTC_MCS_24,
	BTC_MCS_25,
	BTC_MCS_26,
	BTC_MCS_27,
	BTC_MCS_28,
	BTC_MCS_29,
	BTC_MCS_30,
	BTC_MCS_31,
	BTC_VHT_1SS_MCS_0,
	BTC_VHT_1SS_MCS_1,
	BTC_VHT_1SS_MCS_2,
	BTC_VHT_1SS_MCS_3,
	BTC_VHT_1SS_MCS_4,
	BTC_VHT_1SS_MCS_5,
	BTC_VHT_1SS_MCS_6,
	BTC_VHT_1SS_MCS_7,
	BTC_VHT_1SS_MCS_8,
	BTC_VHT_1SS_MCS_9,
	BTC_VHT_2SS_MCS_0,
	BTC_VHT_2SS_MCS_1,
	BTC_VHT_2SS_MCS_2,
	BTC_VHT_2SS_MCS_3,
	BTC_VHT_2SS_MCS_4,
	BTC_VHT_2SS_MCS_5,
	BTC_VHT_2SS_MCS_6,
	BTC_VHT_2SS_MCS_7,
	BTC_VHT_2SS_MCS_8,
	BTC_VHT_2SS_MCS_9,
	BTC_VHT_3SS_MCS_0,
	BTC_VHT_3SS_MCS_1,
	BTC_VHT_3SS_MCS_2,
	BTC_VHT_3SS_MCS_3,
	BTC_VHT_3SS_MCS_4,
	BTC_VHT_3SS_MCS_5,
	BTC_VHT_3SS_MCS_6,
	BTC_VHT_3SS_MCS_7,
	BTC_VHT_3SS_MCS_8,
	BTC_VHT_3SS_MCS_9,
	BTC_VHT_4SS_MCS_0,
	BTC_VHT_4SS_MCS_1,
	BTC_VHT_4SS_MCS_2,
	BTC_VHT_4SS_MCS_3,
	BTC_VHT_4SS_MCS_4,
	BTC_VHT_4SS_MCS_5,
	BTC_VHT_4SS_MCS_6,
	BTC_VHT_4SS_MCS_7,
	BTC_VHT_4SS_MCS_8,
	BTC_VHT_4SS_MCS_9,
	BTC_MCS_32,
	BTC_UNKNOWN,
	BTC_PKT_MGNT,
	BTC_PKT_CTRL,
	BTC_PKT_UNKNOWN,
	BTC_PKT_NOT_FOR_ME,
	BTC_RATE_MAX
};

enum {
	BTC_MULTIPORT_SCC,
	BTC_MULTIPORT_MCC_2CHANNEL,
	BTC_MULTIPORT_MCC_2BAND,
	BTC_MULTIPORT_MAX
};

struct btc_bt_info {
	bool bt_disabled;
	u8 rssi_adjust_for_agc_table_on;
	u8 rssi_adjust_for_1ant_coex_type;
	bool pre_bt_ctrl_agg_buf_size;
	bool bt_busy;
	u8 pre_agg_buf_size;
	u8 agg_buf_size;
	bool limited_dig;
	bool pre_reject_agg_pkt;
	bool reject_agg_pkt;
	bool bt_ctrl_buf_size;
	bool increase_scan_dev_num;
	bool miracast_plus_bt;
	bool bt_ctrl_agg_buf_size;
	bool bt_tx_rx_mask;
	u16 bt_hci_ver;
	u16 bt_real_fw_ver;
	u8 bt_fw_ver;

	bool bt_disable_low_pwr;

	/* the following is for 1Ant solution */
	bool bt_ctrl_lps;
	bool bt_pwr_save_mode;
	bool bt_lps_on;
	bool force_to_roam;
	u8 force_exec_pwr_cmd_cnt;
	u8 lps_val;
	u8 rpwm_val;
	u32 ra_mask;

	u32 afh_map_l;
	u32 afh_map_m;
	u16 afh_map_h;
	u32 bt_supported_feature;
	u32 bt_supported_version;
	u32 bt_device_info;
	u32 bt_forb_slot_val;
	u8 bt_ant_det_val;
	u8 bt_ble_scan_type;
	u32 bt_ble_scan_para;
};

struct btc_stack_info {
	bool profile_notified;
	u16 hci_version;	/* stack hci version */
	u8 num_of_link;
	bool bt_link_exist;
	bool sco_exist;
	bool acl_exist;
	bool a2dp_exist;
	bool hid_exist;
	u8 num_of_hid;
	bool pan_exist;
	bool unknown_acl_exist;
	s8 min_bt_rssi;
};

struct btc_statistics {
	u32 cnt_bind;
	u32 cnt_init_hw_config;
	u32 cnt_init_coex_dm;
	u32 cnt_ips_notify;
	u32 cnt_lps_notify;
	u32 cnt_scan_notify;
	u32 cnt_connect_notify;
	u32 cnt_media_status_notify;
	u32 cnt_special_packet_notify;
	u32 cnt_bt_info_notify;
	u32 cnt_periodical;
	u32 cnt_coex_dm_switch;
	u32 cnt_stack_operation_notify;
	u32 cnt_dbg_ctrl;
	u32 cnt_pre_load_firmware;
	u32 cnt_power_on;
};

struct btc_bt_link_info {
	bool bt_link_exist;
	bool bt_hi_pri_link_exist;
	bool sco_exist;
	bool sco_only;
	bool a2dp_exist;
	bool a2dp_only;
	bool hid_exist;
	bool hid_only;
	bool pan_exist;
	bool pan_only;
	bool slave_role;
};

enum btc_antenna_pos {
	BTC_ANTENNA_AT_MAIN_PORT = 0x1,
	BTC_ANTENNA_AT_AUX_PORT = 0x2,
};

enum btc_mp_h2c_op_code {
	BT_OP_GET_BT_VERSION			= 0,
	BT_OP_WRITE_REG_ADDR			= 12,
	BT_OP_WRITE_REG_VALUE			= 13,
	BT_OP_READ_REG				= 17,
	BT_OP_GET_AFH_MAP_L			= 30,
	BT_OP_GET_AFH_MAP_M			= 31,
	BT_OP_GET_AFH_MAP_H			= 32,
	BT_OP_GET_BT_COEX_SUPPORTED_FEATURE	= 42,
	BT_OP_GET_BT_COEX_SUPPORTED_VERSION	= 43,
	BT_OP_GET_BT_ANT_DET_VAL		= 44,
	BT_OP_GET_BT_BLE_SCAN_PARA		= 45,
	BT_OP_GET_BT_BLE_SCAN_TYPE		= 46,
	BT_OP_GET_BT_DEVICE_INFO		= 48,
	BT_OP_GET_BT_FORBIDDEN_SLOT_VAL		= 49,
	BT_OP_MAX
};

enum btc_mp_h2c_req_num {
	/* 4 bits only */
	BT_SEQ_DONT_CARE			= 0,
	BT_SEQ_GET_BT_VERSION			= 0xE,
	BT_SEQ_GET_AFH_MAP_L			= 0x5,
	BT_SEQ_GET_AFH_MAP_M			= 0x6,
	BT_SEQ_GET_AFH_MAP_H			= 0x9,
	BT_SEQ_GET_BT_COEX_SUPPORTED_FEATURE	= 0x7,
	BT_SEQ_GET_BT_COEX_SUPPORTED_VERSION	= 0x8,
	BT_SEQ_GET_BT_ANT_DET_VAL		= 0x2,
	BT_SEQ_GET_BT_BLE_SCAN_PARA		= 0x3,
	BT_SEQ_GET_BT_BLE_SCAN_TYPE		= 0x4,
	BT_SEQ_GET_BT_DEVICE_INFO		= 0xA,
	BT_SEQ_GET_BT_FORB_SLOT_VAL		= 0xB,
};

struct btc_coexist {
	/* make sure only one adapter can bind the data context  */
	bool binded;
	/* default adapter */
	void *adapter;
	struct btc_board_info board_info;
	/* some bt info referenced by non-bt module */
	struct btc_bt_info bt_info;
	struct btc_stack_info stack_info;
	enum btc_chip_interface	chip_interface;
	struct btc_bt_link_info bt_link_info;

	/* boolean variables to replace BT_AUTO_REPORT_ONLY_XXXXY_ZANT
	 * configuration parameters
	 */
	bool auto_report_1ant;
	bool auto_report_2ant;
	bool dbg_mode_1ant;
	bool dbg_mode_2ant;
	bool initilized;
	bool stop_coex_dm;
	bool manual_control;
	struct btc_statistics statistics;
	u8 pwr_mode_val[10];

	struct completion bt_mp_comp;

	/* function pointers - io related */
	u8 (*btc_read_1byte)(void *btc_context, u32 reg_addr);
	void (*btc_write_1byte)(void *btc_context, u32 reg_addr, u32 data);
	void (*btc_write_1byte_bitmask)(void *btc_context, u32 reg_addr,
					u32 bit_mask, u8 data1b);
	u16 (*btc_read_2byte)(void *btc_context, u32 reg_addr);
	void (*btc_write_2byte)(void *btc_context, u32 reg_addr, u16 data);
	u32 (*btc_read_4byte)(void *btc_context, u32 reg_addr);
	void (*btc_write_4byte)(void *btc_context, u32 reg_addr, u32 data);

	void (*btc_write_local_reg_1byte)(void *btc_context, u32 reg_addr,
					  u8 data);
	void (*btc_set_bb_reg)(void *btc_context, u32 reg_addr,
			       u32 bit_mask, u32 data);
	u32 (*btc_get_bb_reg)(void *btc_context, u32 reg_addr,
			      u32 bit_mask);
	void (*btc_set_rf_reg)(void *btc_context, u8 rf_path, u32 reg_addr,
			       u32 bit_mask, u32 data);
	u32 (*btc_get_rf_reg)(void *btc_context, u8 rf_path,
			      u32 reg_addr, u32 bit_mask);

	void (*btc_fill_h2c)(void *btc_context, u8 element_id,
			     u32 cmd_len, u8 *cmd_buffer);

	void (*btc_disp_dbg_msg)(void *btcoexist, u8 disp_type,
				 struct seq_file *m);

	bool (*btc_get)(void *btcoexist, u8 get_type, void *out_buf);
	bool (*btc_set)(void *btcoexist, u8 set_type, void *in_buf);

	void (*btc_set_bt_reg)(void *btc_context, u8 reg_type, u32 offset,
			       u32 value);
	u32 (*btc_get_bt_coex_supported_feature)(void *btcoexist);
	u32 (*btc_get_bt_coex_supported_version)(void *btcoexist);
	u8 (*btc_get_ant_det_val_from_bt)(void *btcoexist);
	u8 (*btc_get_ble_scan_type_from_bt)(void *btcoexist);
	u32 (*btc_get_ble_scan_para_from_bt)(void *btcoexist, u8 scan_type);
	bool (*btc_get_bt_afh_map_from_bt)(void *btcoexist, u8 map_type,
					   u8 *afh_map);
};

bool halbtc_is_wifi_uplink(struct rtl_priv *adapter);

#define rtl_btc_coexist(rtlpriv)				\
	((struct btc_coexist *)((rtlpriv)->btcoexist.btc_context))
#define rtl_btc_wifi_only(rtlpriv)				\
	((struct wifi_only_cfg *)((rtlpriv)->btcoexist.wifi_only_context))

struct wifi_only_cfg;

bool exhalbtc_initlize_variables(struct rtl_priv *rtlpriv);
bool exhalbtc_initlize_variables_wifi_only(struct rtl_priv *rtlpriv);
bool exhalbtc_bind_bt_coex_withadapter(void *adapter);
void exhalbtc_power_on_setting(struct btc_coexist *btcoexist);
void exhalbtc_pre_load_firmware(struct btc_coexist *btcoexist);
void exhalbtc_init_hw_config(struct btc_coexist *btcoexist, bool wifi_only);
void exhalbtc_init_hw_config_wifi_only(struct wifi_only_cfg *wifionly_cfg);
void exhalbtc_init_coex_dm(struct btc_coexist *btcoexist);
void exhalbtc_ips_notify(struct btc_coexist *btcoexist, u8 type);
void exhalbtc_lps_notify(struct btc_coexist *btcoexist, u8 type);
void exhalbtc_scan_notify(struct btc_coexist *btcoexist, u8 type);
void exhalbtc_scan_notify_wifi_only(struct wifi_only_cfg *wifionly_cfg,
				    u8 is_5g);
void exhalbtc_connect_notify(struct btc_coexist *btcoexist, u8 action);
void exhalbtc_mediastatus_notify(struct btc_coexist *btcoexist,
				 enum rt_media_status media_status);
void exhalbtc_special_packet_notify(struct btc_coexist *btcoexist, u8 pkt_type);
void exhalbtc_bt_info_notify(struct btc_coexist *btcoexist, u8 *tmp_buf,
			     u8 length);
void exhalbtc_rf_status_notify(struct btc_coexist *btcoexist, u8 type);
void exhalbtc_stack_operation_notify(struct btc_coexist *btcoexist, u8 type);
void exhalbtc_halt_notify(struct btc_coexist *btcoexist);
void exhalbtc_pnp_notify(struct btc_coexist *btcoexist, u8 pnp_state);
void exhalbtc_coex_dm_switch(struct btc_coexist *btcoexist);
void exhalbtc_periodical(struct btc_coexist *btcoexist);
void exhalbtc_dbg_control(struct btc_coexist *btcoexist, u8 code, u8 len,
			  u8 *data);
void exhalbtc_antenna_detection(struct btc_coexist *btcoexist, u32 cent_freq,
				u32 offset, u32 span, u32 seconds);
void exhalbtc_stack_update_profile_info(void);
void exhalbtc_set_hci_version(struct btc_coexist *btcoexist, u16 hci_version);
void exhalbtc_set_bt_patch_version(struct btc_coexist *btcoexist,
				   u16 bt_hci_version, u16 bt_patch_version);
void exhalbtc_update_min_bt_rssi(struct btc_coexist *btcoexist, s8 bt_rssi);
void exhalbtc_set_bt_exist(struct btc_coexist *btcoexist, bool bt_exist);
void exhalbtc_set_chip_type(struct btc_coexist *btcoexist, u8 chip_type);
void exhalbtc_set_ant_num(struct rtl_priv *rtlpriv, u8 type, u8 ant_num);
void exhalbtc_display_bt_coex_info(struct btc_coexist *btcoexist,
				   struct seq_file *m);
void exhalbtc_switch_band_notify(struct btc_coexist *btcoexist, u8 type);
void exhalbtc_switch_band_notify_wifi_only(struct wifi_only_cfg *wifionly_cfg,
					   u8 is_5g);
void exhalbtc_signal_compensation(struct btc_coexist *btcoexist,
				  u8 *rssi_wifi, u8 *rssi_bt);
void exhalbtc_lps_leave(struct btc_coexist *btcoexist);
void exhalbtc_low_wifi_traffic_notify(struct btc_coexist *btcoexist);
void exhalbtc_set_single_ant_path(struct btc_coexist *btcoexist,
				  u8 single_ant_path);
void halbtc_send_wifi_port_id_cmd(void *bt_context);
void halbtc_set_default_port_id_cmd(void *bt_context);

/* The following are used by wifi_only case */
enum wifionly_chip_interface {
	WIFIONLY_INTF_UNKNOWN	= 0,
	WIFIONLY_INTF_PCI		= 1,
	WIFIONLY_INTF_USB		= 2,
	WIFIONLY_INTF_SDIO		= 3,
	WIFIONLY_INTF_MAX
};

enum wifionly_customer_id {
	CUSTOMER_NORMAL			= 0,
	CUSTOMER_HP_1			= 1,
};

struct wifi_only_haldata {
	u16		customer_id;
	u8		efuse_pg_antnum;
	u8		efuse_pg_antpath;
	u8		rfe_type;
	u8		ant_div_cfg;
};

struct wifi_only_cfg {
	void				*adapter;
	struct wifi_only_haldata	haldata_info;
	enum wifionly_chip_interface	chip_interface;
};

static inline
void halwifionly_phy_set_bb_reg(struct wifi_only_cfg *wifi_conly_cfg,
				u32 regaddr, u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = (struct rtl_priv *)wifi_conly_cfg->adapter;

	rtl_set_bbreg(rtlpriv->hw, regaddr, bitmask, data);
}

#endif
