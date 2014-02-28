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

#define		BTC_ANT_PATH_WIFI			0
#define		BTC_ANT_PATH_BT				1
#define		BTC_ANT_PATH_PTA			2

enum btc_chip_interface {
	BTC_INTF_UNKNOWN	= 0,
	BTC_INTF_PCI		= 1,
	BTC_INTF_USB		= 2,
	BTC_INTF_SDIO		= 3,
	BTC_INTF_GSPI		= 4,
	BTC_INTF_MAX
};

enum BTC_CHIP_TYPE {
	BTC_CHIP_UNDEF		= 0,
	BTC_CHIP_CSR_BC4	= 1,
	BTC_CHIP_CSR_BC8	= 2,
	BTC_CHIP_RTL8723A	= 3,
	BTC_CHIP_RTL8821	= 4,
	BTC_CHIP_RTL8723B	= 5,
	BTC_CHIP_MAX
};

enum BTC_MSG_TYPE {
	BTC_MSG_INTERFACE	= 0x0,
	BTC_MSG_ALGORITHM	= 0x1,
	BTC_MSG_MAX
};
extern u32 btc_dbg_type[];

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

#define		BT_COEX_ANT_TYPE_PG			0
#define		BT_COEX_ANT_TYPE_ANTDIV			1
#define		BT_COEX_ANT_TYPE_DETECTED		2
#define		BTC_MIMO_PS_STATIC			0
#define		BTC_MIMO_PS_DYNAMIC			1
#define		BTC_RATE_DISABLE			0
#define		BTC_RATE_ENABLE				1
#define		BTC_ANT_PATH_WIFI			0
#define		BTC_ANT_PATH_BT				1
#define		BTC_ANT_PATH_PTA			2


#define	CL_SPRINTF	snprintf
#define	CL_PRINTF	printk

#define	BTC_PRINT(dbgtype, dbgflag, printstr, ...)		\
	do {							\
		if (unlikely(btc_dbg_type[dbgtype] & dbgflag)) {\
			printk(printstr, ##__VA_ARGS__);	\
		}						\
	} while (0)

#define	BTC_PRINT_F(dbgtype, dbgflag, printstr, ...)		\
	do {							\
		if (unlikely(btc_dbg_type[dbgtype] & dbgflag)) {\
			pr_info("%s: ", __func__);	\
			printk(printstr, ##__VA_ARGS__);	\
		}						\
	} while (0)

#define	BTC_PRINT_ADDR(dbgtype, dbgflag, printstr, _ptr)	\
	do {							\
		if (unlikely(btc_dbg_type[dbgtype] & dbgflag)) {	\
			int __i;				\
			u8 *__ptr = (u8 *)_ptr;			\
			printk printstr;			\
			for (__i = 0; __i < 6; __i++)		\
				printk("%02X%s", __ptr[__i], (__i == 5) ? \
				       "" : "-");		\
			pr_info("\n");				\
		}						\
	} while (0)

#define BTC_PRINT_DATA(dbgtype, dbgflag, _titlestring, _hexdata, _hexdatalen) \
	do {								\
		if (unlikely(btc_dbg_type[dbgtype] & dbgflag))	{	\
			int __i;					\
			u8 *__ptr = (u8 *)_hexdata;			\
			printk(_titlestring);				\
			for (__i = 0; __i < (int)_hexdatalen; __i++) {	\
				printk("%02X%s", __ptr[__i], (((__i + 1) % 4) \
							== 0) ? "  " : " ");\
				if (((__i + 1) % 16) == 0)		\
					printk("\n");			\
			}						\
			pr_debug("\n");					\
		}							\
	} while (0)

#define BTC_ANT_PATH_WIFI	0
#define BTC_ANT_PATH_BT		1
#define BTC_ANT_PATH_PTA	2

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
	bool bt_exist;
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

enum btc_wifi_bw_mode {
	BTC_WIFI_BW_LEGACY = 0x0,
	BTC_WIFI_BW_HT20 = 0x1,
	BTC_WIFI_BW_HT40 = 0x2,
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


enum btc_get_type {
	/* type bool */
	BTC_GET_BL_HS_OPERATION,
	BTC_GET_BL_HS_CONNECTING,
	BTC_GET_BL_WIFI_CONNECTED,
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

	/* type s4Byte */
	BTC_GET_S4_WIFI_RSSI,
	BTC_GET_S4_HS_RSSI,

	/* type u32 */
	BTC_GET_U4_WIFI_BW,
	BTC_GET_U4_WIFI_TRAFFIC_DIRECTION,
	BTC_GET_U4_WIFI_FW_VER,
	BTC_GET_U4_BT_PATCH_VER,

	/* type u1Byte */
	BTC_GET_U1_WIFI_DOT11_CHNL,
	BTC_GET_U1_WIFI_CENTRAL_CHNL,
	BTC_GET_U1_WIFI_HS_CHNL,
	BTC_GET_U1_MAC_PHY_MODE,

	/* for 1Ant */
	BTC_GET_U1_LPS_MODE,
	BTC_GET_BL_BT_SCO_BUSY,

	/* for test mode */
	BTC_GET_DRIVER_TEST_CFG,
	BTC_GET_MAX
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

	/* type u1Byte */
	BTC_SET_U1_RSSI_ADJ_VAL_FOR_AGC_TABLE_ON,
	BTC_SET_U1_RSSI_ADJ_VAL_FOR_1ANT_COEX_TYPE,
	BTC_SET_UI_SCAN_SIG_COMPENSATION,
	BTC_SET_U1_AGG_BUF_SIZE,

	/* type trigger some action */
	BTC_SET_ACT_GET_BT_RSSI,
	BTC_SET_ACT_AGGREGATE_CTRL,

	/********* for 1Ant **********/
	/* type bool */
	BTC_SET_BL_BT_SCO_BUSY,
	/* type u1Byte */
	BTC_SET_U1_1ANT_LPS,
	BTC_SET_U1_1ANT_RPWM,
	/* type trigger some action */
	BTC_SET_ACT_LEAVE_LPS,
	BTC_SET_ACT_ENTER_LPS,
	BTC_SET_ACT_NORMAL_LPS,
	BTC_SET_ACT_INC_FORCE_EXEC_PWR_CMD_CNT,
	BTC_SET_ACT_DISABLE_LOW_POWER,
	BTC_SET_ACT_UPDATE_ra_mask,
	BTC_SET_ACT_SEND_MIMO_PS,
	/* BT Coex related */
	BTC_SET_ACT_CTRL_BT_INFO,
	BTC_SET_ACT_CTRL_BT_COEX,
	/***************************/
	BTC_SET_MAX
};

enum btc_dbg_disp_type {
	BTC_DBG_DISP_COEX_STATISTICS = 0x0,
	BTC_DBG_DISP_BT_LINK_INFO = 0x1,
	BTC_DBG_DISP_BT_FW_VER = 0x2,
	BTC_DBG_DISP_FW_PWR_MODE_CMD = 0x3,
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

enum btc_notify_type_stack_operation {
	BTC_STACK_OP_NONE = 0x0,
	BTC_STACK_OP_INQ_PAGE_PAIR_START = 0x1,
	BTC_STACK_OP_INQ_PAGE_PAIR_FINISH = 0x2,
	BTC_STACK_OP_MAX
};


typedef u8 (*bfp_btc_r1)(void *btc_context, u32 reg_addr);

typedef u16 (*bfp_btc_r2)(void *btc_context, u32 reg_addr);

typedef u32 (*bfp_btc_r4)(void *btc_context, u32 reg_addr);

typedef void (*bfp_btc_w1)(void *btc_context, u32 reg_addr, u8 data);

typedef void (*bfp_btc_w1_bit_mak)(void *btc_context, u32 reg_addr,
				   u32 bit_mask, u8 data1b);

typedef void (*bfp_btc_w2)(void *btc_context, u32 reg_addr, u16 data);

typedef void (*bfp_btc_w4)(void *btc_context, u32 reg_addr, u32 data);

typedef void (*bfp_btc_wr_1byte_bit_mask)(void *btc_context, u32 reg_addr,
					  u8 bit_mask, u8 data);

typedef void (*bfp_btc_set_bb_reg)(void *btc_context, u32 reg_addr,
				   u32 bit_mask, u32 data);

typedef u32 (*bfp_btc_get_bb_reg)(void *btc_context, u32 reg_addr,
				  u32 bit_mask);

typedef void (*bfp_btc_set_rf_reg)(void *btc_context, u8 rf_path, u32 reg_addr,
				   u32 bit_mask, u32 data);

typedef u32 (*bfp_btc_get_rf_reg)(void *btc_context, u8 rf_path,
				  u32 reg_addr, u32 bit_mask);

typedef void (*bfp_btc_fill_h2c)(void *btc_context, u8 element_id,
				 u32 cmd_len, u8 *cmd_buffer);

typedef	bool (*bfp_btc_get)(void *btcoexist, u8 get_type, void *out_buf);

typedef	bool (*bfp_btc_set)(void *btcoexist, u8 set_type, void *in_buf);

typedef void (*bfp_btc_disp_dbg_msg)(void *btcoexist, u8 disp_type);

struct btc_bt_info {
	bool bt_disabled;
	u8 rssi_adjust_for_agc_table_on;
	u8 rssi_adjust_for_1ant_coex_type;
	bool bt_busy;
	u8 agg_buf_size;
	bool limited_dig;
	bool reject_agg_pkt;
	bool b_bt_ctrl_buf_size;
	bool increase_scan_dev_num;
	u16 bt_hci_ver;
	u16 bt_real_fw_ver;
	u8 bt_fw_ver;

	/* the following is for 1Ant solution */
	bool bt_ctrl_lps;
	bool bt_pwr_save_mode;
	bool bt_lps_on;
	bool force_to_roam;
	u8 force_exec_pwr_cmd_cnt;
	u8 lps_1ant;
	u8 rpwm_1ant;
	u32 ra_mask;
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
	char min_bt_rssi;
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
	u32 cnt_stack_operation_notify;
	u32 cnt_dbg_ctrl;
};

struct btc_bt_link_info {
	bool bt_link_exist;
	bool sco_exist;
	bool sco_only;
	bool a2dp_exist;
	bool a2dp_only;
	bool hid_exist;
	bool hid_only;
	bool pan_exist;
	bool pan_only;
};

enum btc_antenna_pos {
	BTC_ANTENNA_AT_MAIN_PORT = 0x1,
	BTC_ANTENNA_AT_AUX_PORT = 0x2,
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

	bool initilized;
	bool stop_coex_dm;
	bool manual_control;
	u8 *cli_buf;
	struct btc_statistics statistics;
	u8 pwr_mode_val[10];

	/* function pointers - io related */
	bfp_btc_r1 btc_read_1byte;
	bfp_btc_w1 btc_write_1byte;
	bfp_btc_w1_bit_mak btc_write_1byte_bitmask;
	bfp_btc_r2 btc_read_2byte;
	bfp_btc_w2 btc_write_2byte;
	bfp_btc_r4 btc_read_4byte;
	bfp_btc_w4 btc_write_4byte;

	bfp_btc_set_bb_reg btc_set_bb_reg;
	bfp_btc_get_bb_reg btc_get_bb_reg;


	bfp_btc_set_rf_reg btc_set_rf_reg;
	bfp_btc_get_rf_reg btc_get_rf_reg;

	bfp_btc_fill_h2c btc_fill_h2c;

	bfp_btc_disp_dbg_msg btc_disp_dbg_msg;

	bfp_btc_get btc_get;
	bfp_btc_set btc_set;
};

bool halbtc_is_wifi_uplink(struct rtl_priv *adapter);

extern struct btc_coexist gl_bt_coexist;

bool exhalbtc_initlize_variables(struct rtl_priv *adapter);
void exhalbtc_init_hw_config(struct btc_coexist *btcoexist);
void exhalbtc_init_coex_dm(struct btc_coexist *btcoexist);
void exhalbtc_ips_notify(struct btc_coexist *btcoexist, u8 type);
void exhalbtc_lps_notify(struct btc_coexist *btcoexist, u8 type);
void exhalbtc_scan_notify(struct btc_coexist *btcoexist, u8 type);
void exhalbtc_connect_notify(struct btc_coexist *btcoexist, u8 action);
void exhalbtc_mediastatus_notify(struct btc_coexist *btcoexist,
				 enum _RT_MEDIA_STATUS media_status);
void exhalbtc_special_packet_notify(struct btc_coexist *btcoexist, u8 pkt_type);
void exhalbtc_bt_info_notify(struct btc_coexist *btcoexist, u8 *tmp_buf,
			     u8 length);
void exhalbtc_stack_operation_notify(struct btc_coexist *btcoexist, u8 type);
void exhalbtc_halt_notify(struct btc_coexist *btcoexist);
void exhalbtc_pnp_notify(struct btc_coexist *btcoexist, u8 pnp_state);
void exhalbtc_periodical(struct btc_coexist *btcoexist);
void exhalbtc_dbg_control(struct btc_coexist *btcoexist, u8 code, u8 len,
			  u8 *data);
void exhalbtc_stack_update_profile_info(void);
void exhalbtc_set_hci_version(u16 hci_version);
void exhalbtc_set_bt_patch_version(u16 bt_hci_version, u16 bt_patch_version);
void exhalbtc_update_min_bt_rssi(char bt_rssi);
void exhalbtc_set_bt_exist(bool bt_exist);
void exhalbtc_set_chip_type(u8 chip_type);
void exhalbtc_set_ant_num(u8 type, u8 ant_num);
void exhalbtc_display_bt_coex_info(struct btc_coexist *btcoexist);
void exhalbtc_signal_compensation(struct btc_coexist *btcoexist,
				  u8 *rssi_wifi, u8 *rssi_bt);
void exhalbtc_lps_leave(struct btc_coexist *btcoexist);
void exhalbtc_low_wifi_traffic_notify(struct btc_coexist *btcoexist);

#endif
