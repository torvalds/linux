/* SPDX-License-Identifier: GPL-2.0 */
#ifndef	__HALBTC_OUT_SRC_H__
#define __HALBTC_OUT_SRC_H__


#define		BTC_COEX_OFFLOAD			0
#define		BTC_TMP_BUF_SHORT		20

extern u1Byte	gl_btc_trace_buf[];
#define		BTC_SPRINTF			rsprintf
#define		BTC_TRACE(_MSG_)\
do {\
	if (GLBtcDbgType[COMP_COEX] & BIT(DBG_LOUD)) {\
		RTW_INFO("%s", _MSG_);\
	} \
} while (0)
#define		BT_PrintData(adapter, _MSG_, len, data)	RTW_DBG_DUMP((_MSG_), data, len)


#define		NORMAL_EXEC					FALSE
#define		FORCE_EXEC						TRUE

#define		BTC_RF_OFF					0x0
#define		BTC_RF_ON					0x1

#define		BTC_RF_A					0x0
#define		BTC_RF_B					0x1
#define		BTC_RF_C					0x2
#define		BTC_RF_D					0x3

#define		BTC_SMSP				SINGLEMAC_SINGLEPHY
#define		BTC_DMDP				DUALMAC_DUALPHY
#define		BTC_DMSP				DUALMAC_SINGLEPHY
#define		BTC_MP_UNKNOWN		0xff

#define		BT_COEX_ANT_TYPE_PG			0
#define		BT_COEX_ANT_TYPE_ANTDIV		1
#define		BT_COEX_ANT_TYPE_DETECTED	2

#define		BTC_MIMO_PS_STATIC			0	/* 1ss */
#define		BTC_MIMO_PS_DYNAMIC			1	/* 2ss */

#define		BTC_RATE_DISABLE			0
#define		BTC_RATE_ENABLE				1

/* single Antenna definition */
#define		BTC_ANT_PATH_WIFI			0
#define		BTC_ANT_PATH_BT				1
#define		BTC_ANT_PATH_PTA			2
#define		BTC_ANT_PATH_WIFI5G			3
#define		BTC_ANT_PATH_AUTO			4
/* dual Antenna definition */
#define		BTC_ANT_WIFI_AT_MAIN		0
#define		BTC_ANT_WIFI_AT_AUX			1
#define		BTC_ANT_WIFI_AT_DIVERSITY	2
/* coupler Antenna definition */
#define		BTC_ANT_WIFI_AT_CPL_MAIN	0
#define		BTC_ANT_WIFI_AT_CPL_AUX		1

typedef enum _BTC_POWERSAVE_TYPE {
	BTC_PS_WIFI_NATIVE			= 0,	/* wifi original power save behavior */
	BTC_PS_LPS_ON				= 1,
	BTC_PS_LPS_OFF				= 2,
	BTC_PS_MAX
} BTC_POWERSAVE_TYPE, *PBTC_POWERSAVE_TYPE;

typedef enum _BTC_BT_REG_TYPE {
	BTC_BT_REG_RF						= 0,
	BTC_BT_REG_MODEM					= 1,
	BTC_BT_REG_BLUEWIZE					= 2,
	BTC_BT_REG_VENDOR					= 3,
	BTC_BT_REG_LE						= 4,
	BTC_BT_REG_MAX
} BTC_BT_REG_TYPE, *PBTC_BT_REG_TYPE;

typedef enum _BTC_CHIP_INTERFACE {
	BTC_INTF_UNKNOWN	= 0,
	BTC_INTF_PCI			= 1,
	BTC_INTF_USB			= 2,
	BTC_INTF_SDIO		= 3,
	BTC_INTF_MAX
} BTC_CHIP_INTERFACE, *PBTC_CHIP_INTERFACE;

typedef enum _BTC_CHIP_TYPE {
	BTC_CHIP_UNDEF		= 0,
	BTC_CHIP_CSR_BC4		= 1,
	BTC_CHIP_CSR_BC8		= 2,
	BTC_CHIP_RTL8723A		= 3,
	BTC_CHIP_RTL8821		= 4,
	BTC_CHIP_RTL8723B		= 5,
	BTC_CHIP_MAX
} BTC_CHIP_TYPE, *PBTC_CHIP_TYPE;

/* following is for wifi link status */
#define		WIFI_STA_CONNECTED				BIT0
#define		WIFI_AP_CONNECTED				BIT1
#define		WIFI_HS_CONNECTED				BIT2
#define		WIFI_P2P_GO_CONNECTED			BIT3
#define		WIFI_P2P_GC_CONNECTED			BIT4

/* following is for command line utility */
#define	CL_SPRINTF	rsprintf
#define	CL_PRINTF	DCMD_Printf

struct btc_board_info {
	/* The following is some board information */
	u8				bt_chip_type;
	u8				pg_ant_num;	/* pg ant number */
	u8				btdm_ant_num;	/* ant number for btdm */
	u8				btdm_ant_num_by_ant_det;	/* ant number for btdm after antenna detection */
	u8				btdm_ant_pos;		/* Bryant Add to indicate Antenna Position for (pg_ant_num = 2) && (btdm_ant_num =1)  (DPDT+1Ant case) */
	u8				single_ant_path;	/* current used for 8723b only, 1=>s0,  0=>s1 */
	boolean			tfbga_package;    /* for Antenna detect threshold */
	boolean			btdm_ant_det_finish;
	boolean			btdm_ant_det_already_init_phydm;
	u8				ant_type;
	u8				rfe_type;
	u8				ant_div_cfg;
	boolean			btdm_ant_det_complete_fail;
	u8				ant_det_result;
	boolean			ant_det_result_five_complete;
	u32				antdetval;
};

typedef enum _BTC_DBG_OPCODE {
	BTC_DBG_SET_COEX_NORMAL				= 0x0,
	BTC_DBG_SET_COEX_WIFI_ONLY				= 0x1,
	BTC_DBG_SET_COEX_BT_ONLY				= 0x2,
	BTC_DBG_SET_COEX_DEC_BT_PWR				= 0x3,
	BTC_DBG_SET_COEX_BT_AFH_MAP				= 0x4,
	BTC_DBG_SET_COEX_BT_IGNORE_WLAN_ACT		= 0x5,
	BTC_DBG_SET_COEX_MANUAL_CTRL				= 0x6,
	BTC_DBG_MAX
} BTC_DBG_OPCODE, *PBTC_DBG_OPCODE;

typedef enum _BTC_RSSI_STATE {
	BTC_RSSI_STATE_HIGH						= 0x0,
	BTC_RSSI_STATE_MEDIUM					= 0x1,
	BTC_RSSI_STATE_LOW						= 0x2,
	BTC_RSSI_STATE_STAY_HIGH					= 0x3,
	BTC_RSSI_STATE_STAY_MEDIUM				= 0x4,
	BTC_RSSI_STATE_STAY_LOW					= 0x5,
	BTC_RSSI_MAX
} BTC_RSSI_STATE, *PBTC_RSSI_STATE;
#define	BTC_RSSI_HIGH(_rssi_)	((_rssi_ == BTC_RSSI_STATE_HIGH || _rssi_ == BTC_RSSI_STATE_STAY_HIGH) ? TRUE:FALSE)
#define	BTC_RSSI_MEDIUM(_rssi_)	((_rssi_ == BTC_RSSI_STATE_MEDIUM || _rssi_ == BTC_RSSI_STATE_STAY_MEDIUM) ? TRUE:FALSE)
#define	BTC_RSSI_LOW(_rssi_)	((_rssi_ == BTC_RSSI_STATE_LOW || _rssi_ == BTC_RSSI_STATE_STAY_LOW) ? TRUE:FALSE)

typedef enum _BTC_WIFI_ROLE {
	BTC_ROLE_STATION						= 0x0,
	BTC_ROLE_AP								= 0x1,
	BTC_ROLE_IBSS							= 0x2,
	BTC_ROLE_HS_MODE						= 0x3,
	BTC_ROLE_MAX
} BTC_WIFI_ROLE, *PBTC_WIFI_ROLE;

typedef enum _BTC_WIRELESS_FREQ {
	BTC_FREQ_2_4G					= 0x0,
	BTC_FREQ_5G						= 0x1,
	BTC_FREQ_MAX
} BTC_WIRELESS_FREQ, *PBTC_WIRELESS_FREQ;

typedef enum _BTC_WIFI_BW_MODE {
	BTC_WIFI_BW_LEGACY					= 0x0,
	BTC_WIFI_BW_HT20					= 0x1,
	BTC_WIFI_BW_HT40					= 0x2,
	BTC_WIFI_BW_HT80					= 0x3,
	BTC_WIFI_BW_HT160					= 0x4,
	BTC_WIFI_BW_MAX
} BTC_WIFI_BW_MODE, *PBTC_WIFI_BW_MODE;

typedef enum _BTC_WIFI_TRAFFIC_DIR {
	BTC_WIFI_TRAFFIC_TX					= 0x0,
	BTC_WIFI_TRAFFIC_RX					= 0x1,
	BTC_WIFI_TRAFFIC_MAX
} BTC_WIFI_TRAFFIC_DIR, *PBTC_WIFI_TRAFFIC_DIR;

typedef enum _BTC_WIFI_PNP {
	BTC_WIFI_PNP_WAKE_UP					= 0x0,
	BTC_WIFI_PNP_SLEEP						= 0x1,
	BTC_WIFI_PNP_SLEEP_KEEP_ANT				= 0x2,
	BTC_WIFI_PNP_MAX
} BTC_WIFI_PNP, *PBTC_WIFI_PNP;

typedef enum _BTC_IOT_PEER {
	BTC_IOT_PEER_UNKNOWN = 0,
	BTC_IOT_PEER_REALTEK = 1,
	BTC_IOT_PEER_REALTEK_92SE = 2,
	BTC_IOT_PEER_BROADCOM = 3,
	BTC_IOT_PEER_RALINK = 4,
	BTC_IOT_PEER_ATHEROS = 5,
	BTC_IOT_PEER_CISCO = 6,
	BTC_IOT_PEER_MERU = 7,
	BTC_IOT_PEER_MARVELL = 8,
	BTC_IOT_PEER_REALTEK_SOFTAP = 9, /* peer is RealTek SOFT_AP, by Bohn, 2009.12.17 */
	BTC_IOT_PEER_SELF_SOFTAP = 10, /* Self is SoftAP */
	BTC_IOT_PEER_AIRGO = 11,
	BTC_IOT_PEER_INTEL				= 12,
	BTC_IOT_PEER_RTK_APCLIENT		= 13,
	BTC_IOT_PEER_REALTEK_81XX		= 14,
	BTC_IOT_PEER_REALTEK_WOW		= 15,
	BTC_IOT_PEER_REALTEK_JAGUAR_BCUTAP = 16,
	BTC_IOT_PEER_REALTEK_JAGUAR_CCUTAP = 17,
	BTC_IOT_PEER_MAX,
} BTC_IOT_PEER, *PBTC_IOT_PEER;

/* for 8723b-d cut large current issue */
typedef enum _BTC_WIFI_COEX_STATE {
	BTC_WIFI_STAT_INIT,
	BTC_WIFI_STAT_IQK,
	BTC_WIFI_STAT_NORMAL_OFF,
	BTC_WIFI_STAT_MP_OFF,
	BTC_WIFI_STAT_NORMAL,
	BTC_WIFI_STAT_ANT_DIV,
	BTC_WIFI_STAT_MAX
} BTC_WIFI_COEX_STATE, *PBTC_WIFI_COEX_STATE;

typedef enum _BTC_ANT_TYPE {
	BTC_ANT_TYPE_0,
	BTC_ANT_TYPE_1,
	BTC_ANT_TYPE_2,
	BTC_ANT_TYPE_3,
	BTC_ANT_TYPE_4,
	BTC_ANT_TYPE_MAX
} BTC_ANT_TYPE, *PBTC_ANT_TYPE;

typedef enum _BTC_VENDOR {
	BTC_VENDOR_LENOVO,
	BTC_VENDOR_ASUS,
	BTC_VENDOR_OTHER
} BTC_VENDOR, *PBTC_VENDOR;


/* defined for BFP_BTC_GET */
typedef enum _BTC_GET_TYPE {
	/* type BOOLEAN */
	BTC_GET_BL_HS_OPERATION,
	BTC_GET_BL_HS_CONNECTING,
	BTC_GET_BL_WIFI_FW_READY,
	BTC_GET_BL_WIFI_CONNECTED,
	BTC_GET_BL_WIFI_BUSY,
	BTC_GET_BL_WIFI_SCAN,
	BTC_GET_BL_WIFI_LINK,
	BTC_GET_BL_WIFI_ROAM,
	BTC_GET_BL_WIFI_4_WAY_PROGRESS,
	BTC_GET_BL_WIFI_UNDER_5G,
	BTC_GET_BL_WIFI_AP_MODE_ENABLE,
	BTC_GET_BL_WIFI_ENABLE_ENCRYPTION,
	BTC_GET_BL_WIFI_UNDER_B_MODE,
	BTC_GET_BL_EXT_SWITCH,
	BTC_GET_BL_WIFI_IS_IN_MP_MODE,
	BTC_GET_BL_IS_ASUS_8723B,
	BTC_GET_BL_RF4CE_CONNECTED,

	/* type s4Byte */
	BTC_GET_S4_WIFI_RSSI,
	BTC_GET_S4_HS_RSSI,

	/* type u4Byte */
	BTC_GET_U4_WIFI_BW,
	BTC_GET_U4_WIFI_TRAFFIC_DIRECTION,
	BTC_GET_U4_WIFI_FW_VER,
	BTC_GET_U4_WIFI_LINK_STATUS,
	BTC_GET_U4_BT_PATCH_VER,
	BTC_GET_U4_VENDOR,
	BTC_GET_U4_SUPPORTED_VERSION,
	BTC_GET_U4_SUPPORTED_FEATURE,
	BTC_GET_U4_WIFI_IQK_TOTAL,
	BTC_GET_U4_WIFI_IQK_OK,
	BTC_GET_U4_WIFI_IQK_FAIL,

	/* type u1Byte */
	BTC_GET_U1_WIFI_DOT11_CHNL,
	BTC_GET_U1_WIFI_CENTRAL_CHNL,
	BTC_GET_U1_WIFI_HS_CHNL,
	BTC_GET_U1_WIFI_P2P_CHNL,
	BTC_GET_U1_MAC_PHY_MODE,
	BTC_GET_U1_AP_NUM,
	BTC_GET_U1_ANT_TYPE,
	BTC_GET_U1_IOT_PEER,

	/*===== for 1Ant ======*/
	BTC_GET_U1_LPS_MODE,

	BTC_GET_MAX
} BTC_GET_TYPE, *PBTC_GET_TYPE;

/* defined for BFP_BTC_SET */
typedef enum _BTC_SET_TYPE {
	/* type BOOLEAN */
	BTC_SET_BL_BT_DISABLE,
	BTC_SET_BL_BT_ENABLE_DISABLE_CHANGE,
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
	BTC_SET_U1_AGG_BUF_SIZE,

	/* type trigger some action */
	BTC_SET_ACT_GET_BT_RSSI,
	BTC_SET_ACT_AGGREGATE_CTRL,
	BTC_SET_ACT_ANTPOSREGRISTRY_CTRL,
	/*===== for 1Ant ======*/
	/* type BOOLEAN */

	/* type u1Byte */
	BTC_SET_U1_RSSI_ADJ_VAL_FOR_1ANT_COEX_TYPE,
	BTC_SET_U1_LPS_VAL,
	BTC_SET_U1_RPWM_VAL,
	/* type trigger some action */
	BTC_SET_ACT_LEAVE_LPS,
	BTC_SET_ACT_ENTER_LPS,
	BTC_SET_ACT_NORMAL_LPS,
	BTC_SET_ACT_DISABLE_LOW_POWER,
	BTC_SET_ACT_UPDATE_RAMASK,
	BTC_SET_ACT_SEND_MIMO_PS,
	/* BT Coex related */
	BTC_SET_ACT_CTRL_BT_INFO,
	BTC_SET_ACT_CTRL_BT_COEX,
	BTC_SET_ACT_CTRL_8723B_ANT,
	/*=================*/
	BTC_SET_MAX
} BTC_SET_TYPE, *PBTC_SET_TYPE;

typedef enum _BTC_DBG_DISP_TYPE {
	BTC_DBG_DISP_COEX_STATISTICS				= 0x0,
	BTC_DBG_DISP_BT_LINK_INFO				= 0x1,
	BTC_DBG_DISP_WIFI_STATUS				= 0x2,
	BTC_DBG_DISP_MAX
} BTC_DBG_DISP_TYPE, *PBTC_DBG_DISP_TYPE;

typedef enum _BTC_NOTIFY_TYPE_IPS {
	BTC_IPS_LEAVE							= 0x0,
	BTC_IPS_ENTER							= 0x1,
	BTC_IPS_MAX
} BTC_NOTIFY_TYPE_IPS, *PBTC_NOTIFY_TYPE_IPS;
typedef enum _BTC_NOTIFY_TYPE_LPS {
	BTC_LPS_DISABLE							= 0x0,
	BTC_LPS_ENABLE							= 0x1,
	BTC_LPS_MAX
} BTC_NOTIFY_TYPE_LPS, *PBTC_NOTIFY_TYPE_LPS;
typedef enum _BTC_NOTIFY_TYPE_SCAN {
	BTC_SCAN_FINISH							= 0x0,
	BTC_SCAN_START							= 0x1,
	BTC_SCAN_START_2G						= 0x2,
	BTC_SCAN_MAX
} BTC_NOTIFY_TYPE_SCAN, *PBTC_NOTIFY_TYPE_SCAN;
typedef enum _BTC_NOTIFY_TYPE_SWITCHBAND {
	BTC_NOT_SWITCH							= 0x0,
	BTC_SWITCH_TO_24G						= 0x1,
	BTC_SWITCH_TO_5G						= 0x2,
	BTC_SWITCH_TO_24G_NOFORSCAN				= 0x3,
	BTC_SWITCH_MAX
} BTC_NOTIFY_TYPE_SWITCHBAND, *PBTC_NOTIFY_TYPE_SWITCHBAND;
typedef enum _BTC_NOTIFY_TYPE_ASSOCIATE {
	BTC_ASSOCIATE_FINISH						= 0x0,
	BTC_ASSOCIATE_START						= 0x1,
	BTC_ASSOCIATE_5G_FINISH						= 0x2,
	BTC_ASSOCIATE_5G_START						= 0x3,
	BTC_ASSOCIATE_MAX
} BTC_NOTIFY_TYPE_ASSOCIATE, *PBTC_NOTIFY_TYPE_ASSOCIATE;
typedef enum _BTC_NOTIFY_TYPE_MEDIA_STATUS {
	BTC_MEDIA_DISCONNECT					= 0x0,
	BTC_MEDIA_CONNECT						= 0x1,
	BTC_MEDIA_MAX
} BTC_NOTIFY_TYPE_MEDIA_STATUS, *PBTC_NOTIFY_TYPE_MEDIA_STATUS;
typedef enum _BTC_NOTIFY_TYPE_SPECIFIC_PACKET {
	BTC_PACKET_UNKNOWN					= 0x0,
	BTC_PACKET_DHCP							= 0x1,
	BTC_PACKET_ARP							= 0x2,
	BTC_PACKET_EAPOL						= 0x3,
	BTC_PACKET_MAX
} BTC_NOTIFY_TYPE_SPECIFIC_PACKET, *PBTC_NOTIFY_TYPE_SPECIFIC_PACKET;
typedef enum _BTC_NOTIFY_TYPE_STACK_OPERATION {
	BTC_STACK_OP_NONE					= 0x0,
	BTC_STACK_OP_INQ_PAGE_PAIR_START		= 0x1,
	BTC_STACK_OP_INQ_PAGE_PAIR_FINISH	= 0x2,
	BTC_STACK_OP_MAX
} BTC_NOTIFY_TYPE_STACK_OPERATION, *PBTC_NOTIFY_TYPE_STACK_OPERATION;

/* Bryant Add */
typedef enum _BTC_ANTENNA_POS {
	BTC_ANTENNA_AT_MAIN_PORT				= 0x1,
	BTC_ANTENNA_AT_AUX_PORT				= 0x2,
} BTC_ANTENNA_POS, *PBTC_ANTENNA_POS;

/* Bryant Add */
typedef enum _BTC_BT_OFFON {
	BTC_BT_OFF				= 0x0,
	BTC_BT_ON				= 0x1,
} BTC_BTOFFON, *PBTC_BT_OFFON;

/*==================================================
For following block is for coex offload
==================================================*/
typedef struct _COL_H2C {
	u1Byte	opcode;
	u1Byte	opcode_ver:4;
	u1Byte	req_num:4;
	u1Byte	buf[1];
} COL_H2C, *PCOL_H2C;

#define	COL_C2H_ACK_HDR_LEN	3
typedef struct _COL_C2H_ACK {
	u1Byte	status;
	u1Byte	opcode_ver:4;
	u1Byte	req_num:4;
	u1Byte	ret_len;
	u1Byte	buf[1];
} COL_C2H_ACK, *PCOL_C2H_ACK;

#define	COL_C2H_IND_HDR_LEN	3
typedef struct _COL_C2H_IND {
	u1Byte	type;
	u1Byte	version;
	u1Byte	length;
	u1Byte	data[1];
} COL_C2H_IND, *PCOL_C2H_IND;

/*============================================
NOTE: for debug message, the following define should match
the strings in coexH2cResultString.
============================================*/
typedef enum _COL_H2C_STATUS {
	/* c2h status */
	COL_STATUS_C2H_OK								= 0x00, /* Wifi received H2C request and check content ok. */
	COL_STATUS_C2H_UNKNOWN							= 0x01,	/* Not handled routine */
	COL_STATUS_C2H_UNKNOWN_OPCODE					= 0x02,	/* Invalid OP code, It means that wifi firmware received an undefiend OP code. */
	COL_STATUS_C2H_OPCODE_VER_MISMATCH			= 0x03, /* Wifi firmware and wifi driver mismatch, need to update wifi driver or wifi or. */
	COL_STATUS_C2H_PARAMETER_ERROR				= 0x04, /* Error paraneter.(ex: parameters = NULL but it should have values) */
	COL_STATUS_C2H_PARAMETER_OUT_OF_RANGE		= 0x05, /* Wifi firmware needs to check the parameters from H2C request and return the status.(ex: ch = 500, it's wrong) */
	/* other COL status start from here */
	COL_STATUS_C2H_REQ_NUM_MISMATCH			, /* c2h req_num mismatch, means this c2h is not we expected. */
	COL_STATUS_H2C_HALMAC_FAIL					, /* HALMAC return fail. */
	COL_STATUS_H2C_TIMTOUT						, /* not received the c2h response from fw */
	COL_STATUS_INVALID_C2H_LEN					, /* invalid coex offload c2h ack length, must >= 3 */
	COL_STATUS_COEX_DATA_OVERFLOW				, /* coex returned length over the c2h ack length. */
	COL_STATUS_MAX
} COL_H2C_STATUS, *PCOL_H2C_STATUS;

#define	COL_MAX_H2C_REQ_NUM		16

#define	COL_H2C_BUF_LEN			20
typedef enum _COL_OPCODE {
	COL_OP_WIFI_STATUS_NOTIFY					= 0x0,
	COL_OP_WIFI_PROGRESS_NOTIFY					= 0x1,
	COL_OP_WIFI_INFO_NOTIFY						= 0x2,
	COL_OP_WIFI_POWER_STATE_NOTIFY				= 0x3,
	COL_OP_SET_CONTROL							= 0x4,
	COL_OP_GET_CONTROL							= 0x5,
	COL_OP_WIFI_OPCODE_MAX
} COL_OPCODE, *PCOL_OPCODE;

typedef enum _COL_IND_TYPE {
	COL_IND_BT_INFO								= 0x0,
	COL_IND_PSTDMA								= 0x1,
	COL_IND_LIMITED_TX_RX						= 0x2,
	COL_IND_COEX_TABLE							= 0x3,
	COL_IND_REQ									= 0x4,
	COL_IND_MAX
} COL_IND_TYPE, *PCOL_IND_TYPE;

typedef struct _COL_SINGLE_H2C_RECORD {
	u1Byte					h2c_buf[COL_H2C_BUF_LEN];	/* the latest sent h2c buffer */
	u4Byte					h2c_len;
	u1Byte					c2h_ack_buf[COL_H2C_BUF_LEN];	/* the latest received c2h buffer */
	u4Byte					c2h_ack_len;
	u4Byte					count;									/* the total number of the sent h2c command */
	u4Byte					status[COL_STATUS_MAX];					/* the c2h status for the sent h2c command */
} COL_SINGLE_H2C_RECORD, *PCOL_SINGLE_H2C_RECORD;

typedef struct _COL_SINGLE_C2H_IND_RECORD {
	u1Byte					ind_buf[COL_H2C_BUF_LEN];	/* the latest received c2h indication buffer */
	u4Byte					ind_len;
	u4Byte					count;									/* the total number of the rcvd c2h indication */
	u4Byte					status[COL_STATUS_MAX];					/* the c2h indication verified status */
} COL_SINGLE_C2H_IND_RECORD, *PCOL_SINGLE_C2H_IND_RECORD;

typedef struct _BTC_OFFLOAD {
	/* H2C command related */
	u1Byte					h2c_req_num;
	u4Byte					cnt_h2c_sent;
	COL_SINGLE_H2C_RECORD	h2c_record[COL_OP_WIFI_OPCODE_MAX];

	/* C2H Ack related */
	u4Byte					cnt_c2h_ack;
	u4Byte					status[COL_STATUS_MAX];
	struct completion		c2h_event[COL_MAX_H2C_REQ_NUM];	/* for req_num = 1~COL_MAX_H2C_REQ_NUM */
	u1Byte					c2h_ack_buf[COL_MAX_H2C_REQ_NUM][COL_H2C_BUF_LEN];
	u1Byte					c2h_ack_len[COL_MAX_H2C_REQ_NUM];

	/* C2H Indication related */
	u4Byte						cnt_c2h_ind;
	COL_SINGLE_C2H_IND_RECORD	c2h_ind_record[COL_IND_MAX];
	u4Byte						c2h_ind_status[COL_STATUS_MAX];
	u1Byte						c2h_ind_buf[COL_H2C_BUF_LEN];
	u1Byte						c2h_ind_len;
} BTC_OFFLOAD, *PBTC_OFFLOAD;
extern BTC_OFFLOAD				gl_coex_offload;
/*==================================================*/

typedef u1Byte
(*BFP_BTC_R1)(
	IN	PVOID			pBtcContext,
	IN	u4Byte			RegAddr
	);
typedef u2Byte
(*BFP_BTC_R2)(
	IN	PVOID			pBtcContext,
	IN	u4Byte			RegAddr
	);
typedef u4Byte
(*BFP_BTC_R4)(
	IN	PVOID			pBtcContext,
	IN	u4Byte			RegAddr
	);
typedef VOID
(*BFP_BTC_W1)(
	IN	PVOID			pBtcContext,
	IN	u4Byte			RegAddr,
	IN	u1Byte			Data
	);
typedef VOID
(*BFP_BTC_W1_BIT_MASK)(
	IN	PVOID			pBtcContext,
	IN	u4Byte			regAddr,
	IN	u1Byte			bitMask,
	IN	u1Byte			data1b
	);
typedef VOID
(*BFP_BTC_W2)(
	IN	PVOID			pBtcContext,
	IN	u4Byte			RegAddr,
	IN	u2Byte			Data
	);
typedef VOID
(*BFP_BTC_W4)(
	IN	PVOID			pBtcContext,
	IN	u4Byte			RegAddr,
	IN	u4Byte			Data
	);
typedef VOID
(*BFP_BTC_LOCAL_REG_W1)(
	IN	PVOID			pBtcContext,
	IN	u4Byte			RegAddr,
	IN	u1Byte			Data
	);
typedef VOID
(*BFP_BTC_SET_BB_REG)(
	IN	PVOID			pBtcContext,
	IN	u4Byte			RegAddr,
	IN	u4Byte			BitMask,
	IN	u4Byte			Data
	);
typedef u4Byte
(*BFP_BTC_GET_BB_REG)(
	IN	PVOID			pBtcContext,
	IN	u4Byte			RegAddr,
	IN	u4Byte			BitMask
	);
typedef VOID
(*BFP_BTC_SET_RF_REG)(
	IN	PVOID			pBtcContext,
	IN	u1Byte			eRFPath,
	IN	u4Byte			RegAddr,
	IN	u4Byte			BitMask,
	IN	u4Byte			Data
	);
typedef u4Byte
(*BFP_BTC_GET_RF_REG)(
	IN	PVOID			pBtcContext,
	IN	u1Byte			eRFPath,
	IN	u4Byte			RegAddr,
	IN	u4Byte			BitMask
	);
typedef VOID
(*BFP_BTC_FILL_H2C)(
	IN	PVOID			pBtcContext,
	IN	u1Byte			elementId,
	IN	u4Byte			cmdLen,
	IN	pu1Byte			pCmdBuffer
	);

typedef	BOOLEAN
(*BFP_BTC_GET)(
	IN	PVOID			pBtCoexist,
	IN	u1Byte			getType,
	OUT	PVOID			pOutBuf
	);

typedef	BOOLEAN
(*BFP_BTC_SET)(
	IN	PVOID			pBtCoexist,
	IN	u1Byte			setType,
	OUT	PVOID			pInBuf
	);
typedef u2Byte
(*BFP_BTC_SET_BT_REG)(
	IN	PVOID			pBtcContext,
	IN	u1Byte			regType,
	IN	u4Byte			offset,
	IN	u4Byte			value
	);
typedef BOOLEAN
(*BFP_BTC_SET_BT_ANT_DETECTION)(
	IN	PVOID			pBtcContext,
	IN	u1Byte			txTime,
	IN	u1Byte			btChnl
	);

typedef BOOLEAN
(*BFP_BTC_SET_BT_TRX_MASK)(
	IN	PVOID			pBtcContext,
	IN	u1Byte			bt_trx_mask
	);

typedef u4Byte
(*BFP_BTC_GET_BT_REG)(
	IN	PVOID			pBtcContext,
	IN	u1Byte			regType,
	IN	u4Byte			offset
	);
typedef VOID
(*BFP_BTC_DISP_DBG_MSG)(
	IN	PVOID			pBtCoexist,
	IN	u1Byte			dispType
	);

typedef COL_H2C_STATUS
(*BFP_BTC_COEX_H2C_PROCESS)(
	IN	PVOID			pBtCoexist,
	IN	u1Byte			opcode,
	IN	u1Byte			opcode_ver,
	IN	pu1Byte			ph2c_par,
	IN	u1Byte			h2c_par_len
	);

typedef u4Byte
(*BFP_BTC_GET_BT_COEX_SUPPORTED_FEATURE)(
	IN	PVOID			pBtcContext
	);

typedef u4Byte
(*BFP_BTC_GET_BT_COEX_SUPPORTED_VERSION)(
	IN	PVOID			pBtcContext
	);

typedef u4Byte
(*BFP_BTC_GET_PHYDM_VERSION)(
	IN	PVOID			pBtcContext
	);

typedef VOID
(*BTC_PHYDM_MODIFY_RA_PCR_THRESHLOD)(
	IN	PVOID		pDM_Odm,
	IN	u1Byte		RA_offset_direction,
	IN	u1Byte		RA_threshold_offset
	);

typedef u4Byte
(*BTC_PHYDM_CMNINFOQUERY)(
	IN		PVOID	pDM_Odm,
	IN		u1Byte	info_type
	);

typedef u1Byte
(*BFP_BTC_GET_ANT_DET_VAL_FROM_BT)(
	IN	PVOID			pBtcContext
	);

typedef u1Byte
(*BFP_BTC_GET_BLE_SCAN_TYPE_FROM_BT)(
	IN	PVOID			pBtcContext
	);

typedef u4Byte
(*BFP_BTC_GET_BLE_SCAN_PARA_FROM_BT)(
	IN	PVOID			pBtcContext,
	IN  u1Byte			scanType
	);

struct  btc_bt_info {
	boolean					bt_disabled;
	boolean				bt_enable_disable_change;
	u8					rssi_adjust_for_agc_table_on;
	u8					rssi_adjust_for_1ant_coex_type;
	boolean					pre_bt_ctrl_agg_buf_size;
	boolean					bt_ctrl_agg_buf_size;
	boolean					pre_reject_agg_pkt;
	boolean					reject_agg_pkt;
	boolean					increase_scan_dev_num;
	boolean					bt_tx_rx_mask;
	u8					pre_agg_buf_size;
	u8					agg_buf_size;
	boolean					bt_busy;
	boolean					limited_dig;
	u16					bt_hci_ver;
	u16					bt_real_fw_ver;
	u8					bt_fw_ver;
	u32					get_bt_fw_ver_cnt;
	u32					bt_get_fw_ver;
	boolean					miracast_plus_bt;

	boolean					bt_disable_low_pwr;

	boolean					bt_ctrl_lps;
	boolean					bt_lps_on;
	boolean					force_to_roam;	/* for 1Ant solution */
	u8					lps_val;
	u8					rpwm_val;
	u32					ra_mask;
};

struct btc_stack_info {
	boolean					profile_notified;
	u16					hci_version;	/* stack hci version */
	u8					num_of_link;
	boolean					bt_link_exist;
	boolean					sco_exist;
	boolean					acl_exist;
	boolean					a2dp_exist;
	boolean					hid_exist;
	u8					num_of_hid;
	boolean					pan_exist;
	boolean					unknown_acl_exist;
	s8					min_bt_rssi;
};

struct btc_bt_link_info {
	boolean					bt_link_exist;
	boolean					bt_hi_pri_link_exist;
	boolean					sco_exist;
	boolean					sco_only;
	boolean					a2dp_exist;
	boolean					a2dp_only;
	boolean					hid_exist;
	boolean					hid_only;
	boolean					pan_exist;
	boolean					pan_only;
	boolean					slave_role;
	boolean					acl_busy;
};

#ifdef CONFIG_RF4CE_COEXIST
struct btc_rf4ce_info {
	u8					link_state;
	u8					voice_state;
};
#endif

struct btc_statistics {
	u32					cnt_bind;
	u32					cnt_power_on;
	u32					cnt_pre_load_firmware;
	u32					cnt_init_hw_config;
	u32					cnt_init_coex_dm;
	u32					cnt_ips_notify;
	u32					cnt_lps_notify;
	u32					cnt_scan_notify;
	u32					cnt_connect_notify;
	u32					cnt_media_status_notify;
	u32					cnt_specific_packet_notify;
	u32					cnt_bt_info_notify;
	u32					cnt_rf_status_notify;
	u32					cnt_periodical;
	u32					cnt_coex_dm_switch;
	u32					cnt_stack_operation_notify;
	u32					cnt_dbg_ctrl;
};

struct btc_coexist {
	BOOLEAN				bBinded;		/*make sure only one adapter can bind the data context*/
	PVOID				Adapter;		/*default adapter*/
	struct  btc_board_info		board_info;
	struct  btc_bt_info			bt_info;		/*some bt info referenced by non-bt module*/
	struct  btc_stack_info		stack_info;
	struct  btc_bt_link_info		bt_link_info;

#ifdef CONFIG_RF4CE_COEXIST
	struct  btc_rf4ce_info		rf4ce_info;
#endif

	BTC_CHIP_INTERFACE		chip_interface;
	PVOID					odm_priv;

	BOOLEAN					initilized;
	BOOLEAN					stop_coex_dm;
	BOOLEAN					manual_control;
	BOOLEAN					bdontenterLPS;
	pu1Byte					cli_buf;
	struct btc_statistics		statistics;
	u1Byte				pwrModeVal[10];

	/* function pointers */
	/* io related */
	BFP_BTC_R1			btc_read_1byte;
	BFP_BTC_W1			btc_write_1byte;
	BFP_BTC_W1_BIT_MASK	btc_write_1byte_bitmask;
	BFP_BTC_R2			btc_read_2byte;
	BFP_BTC_W2			btc_write_2byte;
	BFP_BTC_R4			btc_read_4byte;
	BFP_BTC_W4			btc_write_4byte;
	BFP_BTC_LOCAL_REG_W1	btc_write_local_reg_1byte;
	/* read/write bb related */
	BFP_BTC_SET_BB_REG	btc_set_bb_reg;
	BFP_BTC_GET_BB_REG	btc_get_bb_reg;

	/* read/write rf related */
	BFP_BTC_SET_RF_REG	btc_set_rf_reg;
	BFP_BTC_GET_RF_REG	btc_get_rf_reg;

	/* fill h2c related */
	BFP_BTC_FILL_H2C		btc_fill_h2c;
	/* other */
	BFP_BTC_DISP_DBG_MSG	btc_disp_dbg_msg;
	/* normal get/set related */
	BFP_BTC_GET			btc_get;
	BFP_BTC_SET			btc_set;

	BFP_BTC_GET_BT_REG	btc_get_bt_reg;
	BFP_BTC_SET_BT_REG	btc_set_bt_reg;

	BFP_BTC_SET_BT_ANT_DETECTION	btc_set_bt_ant_detection;

	BFP_BTC_COEX_H2C_PROCESS	btc_coex_h2c_process;
	BFP_BTC_SET_BT_TRX_MASK		btc_set_bt_trx_mask;
	BFP_BTC_GET_BT_COEX_SUPPORTED_FEATURE btc_get_bt_coex_supported_feature;
	BFP_BTC_GET_BT_COEX_SUPPORTED_VERSION btc_get_bt_coex_supported_version;
	BFP_BTC_GET_PHYDM_VERSION		btc_get_bt_phydm_version;
	BTC_PHYDM_MODIFY_RA_PCR_THRESHLOD	btc_phydm_modify_RA_PCR_threshold;
	BTC_PHYDM_CMNINFOQUERY				btc_phydm_query_PHY_counter;
	BFP_BTC_GET_ANT_DET_VAL_FROM_BT		btc_get_ant_det_val_from_bt;
	BFP_BTC_GET_BLE_SCAN_TYPE_FROM_BT	btc_get_ble_scan_type_from_bt;
	BFP_BTC_GET_BLE_SCAN_PARA_FROM_BT	btc_get_ble_scan_para_from_bt;
};
typedef struct btc_coexist *PBTC_COEXIST;

extern struct btc_coexist	GLBtCoexist;

BOOLEAN
EXhalbtcoutsrc_InitlizeVariables(
	IN	PVOID		Adapter
	);
VOID
EXhalbtcoutsrc_PowerOnSetting(
	IN	PBTC_COEXIST		pBtCoexist
	);
VOID
EXhalbtcoutsrc_PreLoadFirmware(
	IN	PBTC_COEXIST		pBtCoexist
	);
VOID
EXhalbtcoutsrc_InitHwConfig(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN				bWifiOnly
	);
VOID
EXhalbtcoutsrc_InitCoexDm(
	IN	PBTC_COEXIST		pBtCoexist
	);
VOID
EXhalbtcoutsrc_IpsNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	);
VOID
EXhalbtcoutsrc_LpsNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	);
VOID
EXhalbtcoutsrc_ScanNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	);
VOID
EXhalbtcoutsrc_SetAntennaPathNotify(
	IN	PBTC_COEXIST	pBtCoexist,
	IN	u1Byte			type
	);
VOID
EXhalbtcoutsrc_ConnectNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			action
	);
VOID
EXhalbtcoutsrc_MediaStatusNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	RT_MEDIA_STATUS	mediaStatus
	);
VOID
EXhalbtcoutsrc_SpecificPacketNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			pktType
	);
VOID
EXhalbtcoutsrc_BtInfoNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	pu1Byte			tmpBuf,
	IN	u1Byte			length
	);
VOID
EXhalbtcoutsrc_RfStatusNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte				type
	);
VOID
EXhalbtcoutsrc_StackOperationNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	);
VOID
EXhalbtcoutsrc_HaltNotify(
	IN	PBTC_COEXIST		pBtCoexist
	);
VOID
EXhalbtcoutsrc_PnpNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			pnpState
	);
VOID
EXhalbtcoutsrc_CoexDmSwitch(
	IN	PBTC_COEXIST		pBtCoexist
	);
VOID
EXhalbtcoutsrc_Periodical(
	IN	PBTC_COEXIST		pBtCoexist
	);
VOID
EXhalbtcoutsrc_DbgControl(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u1Byte				opCode,
	IN	u1Byte				opLen,
	IN	pu1Byte				pData
	);
VOID
EXhalbtcoutsrc_AntennaDetection(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u4Byte					centFreq,
	IN	u4Byte					offset,
	IN	u4Byte					span,
	IN	u4Byte					seconds
	);
VOID
EXhalbtcoutsrc_StackUpdateProfileInfo(
	VOID
	);
VOID
EXhalbtcoutsrc_SetHciVersion(
	IN	u2Byte	hciVersion
	);
VOID
EXhalbtcoutsrc_SetBtPatchVersion(
	IN	u2Byte	btHciVersion,
	IN	u2Byte	btPatchVersion
	);
VOID
EXhalbtcoutsrc_UpdateMinBtRssi(
	IN	s1Byte	btRssi
	);
#if 0
VOID
EXhalbtcoutsrc_SetBtExist(
	IN	BOOLEAN		bBtExist
	);
#endif
VOID
EXhalbtcoutsrc_SetChipType(
	IN	u1Byte		chipType
	);
VOID
EXhalbtcoutsrc_SetAntNum(
	IN	u1Byte		type,
	IN	u1Byte		antNum
	);
VOID
EXhalbtcoutsrc_SetSingleAntPath(
	IN	u1Byte		singleAntPath
	);
VOID
EXhalbtcoutsrc_DisplayBtCoexInfo(
	IN	PBTC_COEXIST		pBtCoexist
	);
VOID
EXhalbtcoutsrc_DisplayAntDetection(
	IN	PBTC_COEXIST		pBtCoexist
	);

#define	MASKBYTE0		0xff
#define	MASKBYTE1		0xff00
#define	MASKBYTE2		0xff0000
#define	MASKBYTE3		0xff000000
#define	MASKHWORD	0xffff0000
#define	MASKLWORD		0x0000ffff
#define	MASKDWORD	0xffffffff
#define	MASK12BITS		0xfff
#define	MASKH4BITS		0xf0000000
#define	MASKOFDM_D	0xffc00000
#define	MASKCCK		0x3f3f3f3f

#endif
