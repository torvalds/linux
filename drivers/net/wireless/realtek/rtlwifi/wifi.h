/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __RTL_WIFI_H__
#define __RTL_WIFI_H__

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/sched.h>
#include <linux/firmware.h>
#include <linux/etherdevice.h>
#include <linux/vmalloc.h>
#include <linux/usb.h>
#include <net/mac80211.h>
#include <linux/completion.h>
#include <linux/bitfield.h>
#include "debug.h"

#define	MASKBYTE0				0xff
#define	MASKBYTE1				0xff00
#define	MASKBYTE2				0xff0000
#define	MASKBYTE3				0xff000000
#define	MASKHWORD				0xffff0000
#define	MASKLWORD				0x0000ffff
#define	MASKDWORD				0xffffffff
#define	MASK12BITS				0xfff
#define	MASKH4BITS				0xf0000000
#define MASKOFDM_D				0xffc00000
#define	MASKCCK					0x3f3f3f3f

#define	MASK4BITS				0x0f
#define	MASK20BITS				0xfffff
#define RFREG_OFFSET_MASK			0xfffff

#define	MASKBYTE0				0xff
#define	MASKBYTE1				0xff00
#define	MASKBYTE2				0xff0000
#define	MASKBYTE3				0xff000000
#define	MASKHWORD				0xffff0000
#define	MASKLWORD				0x0000ffff
#define	MASKDWORD				0xffffffff
#define	MASK12BITS				0xfff
#define	MASKH4BITS				0xf0000000
#define MASKOFDM_D				0xffc00000
#define	MASKCCK					0x3f3f3f3f

#define	MASK4BITS				0x0f
#define	MASK20BITS				0xfffff
#define RFREG_OFFSET_MASK			0xfffff

#define RF_CHANGE_BY_INIT			0
#define RF_CHANGE_BY_IPS			BIT(28)
#define RF_CHANGE_BY_PS				BIT(29)
#define RF_CHANGE_BY_HW				BIT(30)
#define RF_CHANGE_BY_SW				BIT(31)

#define IQK_ADDA_REG_NUM			16
#define IQK_MAC_REG_NUM				4
#define IQK_THRESHOLD				8

#define MAX_KEY_LEN				61
#define KEY_BUF_SIZE				5

/* QoS related. */
/*aci: 0x00	Best Effort*/
/*aci: 0x01	Background*/
/*aci: 0x10	Video*/
/*aci: 0x11	Voice*/
/*Max: define total number.*/
#define AC0_BE					0
#define AC1_BK					1
#define AC2_VI					2
#define AC3_VO					3
#define AC_MAX					4
#define QOS_QUEUE_NUM				4
#define RTL_MAC80211_NUM_QUEUE			5
#define REALTEK_USB_VENQT_MAX_BUF_SIZE		254
#define RTL_USB_MAX_RX_COUNT			100
#define QBSS_LOAD_SIZE				5
#define MAX_WMMELE_LENGTH			64
#define ASPM_L1_LATENCY				7

#define TOTAL_CAM_ENTRY				32

/*slot time for 11g. */
#define RTL_SLOT_TIME_9				9
#define RTL_SLOT_TIME_20			20

/*related to tcp/ip. */
#define SNAP_SIZE		6
#define PROTOC_TYPE_SIZE	2

/*related with 802.11 frame*/
#define MAC80211_3ADDR_LEN			24
#define MAC80211_4ADDR_LEN			30

#define CHANNEL_MAX_NUMBER	(14 + 24 + 21)	/* 14 is the max channel no */
#define CHANNEL_MAX_NUMBER_2G		14
#define CHANNEL_MAX_NUMBER_5G		49 /* Please refer to
					    *"phy_GetChnlGroup8812A" and
					    * "Hal_ReadTxPowerInfo8812A"
					    */
#define CHANNEL_MAX_NUMBER_5G_80M	7
#define CHANNEL_GROUP_MAX	(3 + 9)	/*  ch1~3, 4~9, 10~14 = three groups */
#define MAX_PG_GROUP			13
#define	CHANNEL_GROUP_MAX_2G		3
#define	CHANNEL_GROUP_IDX_5GL		3
#define	CHANNEL_GROUP_IDX_5GM		6
#define	CHANNEL_GROUP_IDX_5GH		9
#define	CHANNEL_GROUP_MAX_5G		9
#define CHANNEL_MAX_NUMBER_2G		14
#define AVG_THERMAL_NUM			8
#define AVG_THERMAL_NUM_88E		4
#define AVG_THERMAL_NUM_8723BE		4
#define MAX_TID_COUNT			9

/* for early mode */
#define FCS_LEN				4
#define EM_HDR_LEN			8

enum rtl8192c_h2c_cmd {
	H2C_AP_OFFLOAD = 0,
	H2C_SETPWRMODE = 1,
	H2C_JOINBSSRPT = 2,
	H2C_RSVDPAGE = 3,
	H2C_RSSI_REPORT = 5,
	H2C_RA_MASK = 6,
	H2C_MACID_PS_MODE = 7,
	H2C_P2P_PS_OFFLOAD = 8,
	H2C_MAC_MODE_SEL = 9,
	H2C_PWRM = 15,
	H2C_P2P_PS_CTW_CMD = 24,
	MAX_H2CCMD
};

enum {
	H2C_BT_PORT_ID = 0x71,
};

enum rtl_c2h_evt_v1 {
	C2H_DBG = 0,
	C2H_LB = 1,
	C2H_TXBF = 2,
	C2H_TX_REPORT = 3,
	C2H_BT_INFO = 9,
	C2H_BT_MP = 11,
	C2H_RA_RPT = 12,

	C2H_FW_SWCHNL = 0x10,
	C2H_IQK_FINISH = 0x11,

	C2H_EXT_V2 = 0xFF,
};

enum rtl_c2h_evt_v2 {
	C2H_V2_CCX_RPT = 0x0F,
};

#define GET_C2H_CMD_ID(c2h)	({u8 *__c2h = c2h; __c2h[0]; })
#define GET_C2H_SEQ(c2h)	({u8 *__c2h = c2h; __c2h[1]; })
#define C2H_DATA_OFFSET		2
#define GET_C2H_DATA_PTR(c2h)	({u8 *__c2h = c2h; &__c2h[C2H_DATA_OFFSET]; })

#define GET_TX_REPORT_SN_V1(c2h)	(c2h[6])
#define GET_TX_REPORT_ST_V1(c2h)	(c2h[0] & 0xC0)
#define GET_TX_REPORT_RETRY_V1(c2h)	(c2h[2] & 0x3F)
#define GET_TX_REPORT_SN_V2(c2h)	(c2h[6])
#define GET_TX_REPORT_ST_V2(c2h)	(c2h[7] & 0xC0)
#define GET_TX_REPORT_RETRY_V2(c2h)	(c2h[8] & 0x3F)

#define MAX_TX_COUNT			4
#define MAX_REGULATION_NUM		4
#define MAX_RF_PATH_NUM			4
#define MAX_RATE_SECTION_NUM		6	/* = MAX_RATE_SECTION */
#define MAX_2_4G_BANDWIDTH_NUM		4
#define MAX_5G_BANDWIDTH_NUM		4
#define	MAX_RF_PATH			4
#define	MAX_CHNL_GROUP_24G		6
#define	MAX_CHNL_GROUP_5G		14

#define TX_PWR_BY_RATE_NUM_BAND		2
#define TX_PWR_BY_RATE_NUM_RF		4
#define TX_PWR_BY_RATE_NUM_SECTION	12
#define TX_PWR_BY_RATE_NUM_RATE		84 /* >= TX_PWR_BY_RATE_NUM_SECTION */
#define MAX_BASE_NUM_IN_PHY_REG_PG_24G	6  /* MAX_RATE_SECTION */
#define MAX_BASE_NUM_IN_PHY_REG_PG_5G	5  /* MAX_RATE_SECTION -1 */

#define BUFDESC_SEG_NUM		1 /* 0:2 seg, 1: 4 seg, 2: 8 seg */

#define DEL_SW_IDX_SZ		30

/* For now, it's just for 8192ee
 * but not OK yet, keep it 0
 */
#define RTL8192EE_SEG_NUM		BUFDESC_SEG_NUM

enum rf_tx_num {
	RF_1TX = 0,
	RF_2TX,
	RF_MAX_TX_NUM,
	RF_TX_NUM_NONIMPLEMENT,
};

#define PACKET_NORMAL			0
#define PACKET_DHCP			1
#define PACKET_ARP			2
#define PACKET_EAPOL			3

#define	MAX_SUPPORT_WOL_PATTERN_NUM	16
#define	RSVD_WOL_PATTERN_NUM		1
#define	WKFMCAM_ADDR_NUM		6
#define	WKFMCAM_SIZE			24

#define	MAX_WOL_BIT_MASK_SIZE		16
/* MIN LEN keeps 13 here */
#define	MIN_WOL_PATTERN_SIZE		13
#define	MAX_WOL_PATTERN_SIZE		128

#define	WAKE_ON_MAGIC_PACKET		BIT(0)
#define	WAKE_ON_PATTERN_MATCH		BIT(1)

#define	WOL_REASON_PTK_UPDATE		BIT(0)
#define	WOL_REASON_GTK_UPDATE		BIT(1)
#define	WOL_REASON_DISASSOC		BIT(2)
#define	WOL_REASON_DEAUTH		BIT(3)
#define	WOL_REASON_AP_LOST		BIT(4)
#define	WOL_REASON_MAGIC_PKT		BIT(5)
#define	WOL_REASON_UNICAST_PKT		BIT(6)
#define	WOL_REASON_PATTERN_PKT		BIT(7)
#define	WOL_REASON_RTD3_SSID_MATCH	BIT(8)
#define	WOL_REASON_REALWOW_V2_WAKEUPPKT	BIT(9)
#define	WOL_REASON_REALWOW_V2_ACKLOST	BIT(10)

struct rtlwifi_firmware_header {
	__le16 signature;
	u8 category;
	u8 function;
	__le16 version;
	u8 subversion;
	u8 rsvd1;
	u8 month;
	u8 date;
	u8 hour;
	u8 minute;
	__le16 ramcodesize;
	__le16 rsvd2;
	__le32 svnindex;
	__le32 rsvd3;
	__le32 rsvd4;
	__le32 rsvd5;
};

struct txpower_info_2g {
	u8 index_cck_base[MAX_RF_PATH][MAX_CHNL_GROUP_24G];
	u8 index_bw40_base[MAX_RF_PATH][MAX_CHNL_GROUP_24G];
	/*If only one tx, only BW20 and OFDM are used.*/
	u8 cck_diff[MAX_RF_PATH][MAX_TX_COUNT];
	u8 ofdm_diff[MAX_RF_PATH][MAX_TX_COUNT];
	u8 bw20_diff[MAX_RF_PATH][MAX_TX_COUNT];
	u8 bw40_diff[MAX_RF_PATH][MAX_TX_COUNT];
	u8 bw80_diff[MAX_RF_PATH][MAX_TX_COUNT];
	u8 bw160_diff[MAX_RF_PATH][MAX_TX_COUNT];
};

struct txpower_info_5g {
	u8 index_bw40_base[MAX_RF_PATH][MAX_CHNL_GROUP_5G];
	/*If only one tx, only BW20, OFDM, BW80 and BW160 are used.*/
	u8 ofdm_diff[MAX_RF_PATH][MAX_TX_COUNT];
	u8 bw20_diff[MAX_RF_PATH][MAX_TX_COUNT];
	u8 bw40_diff[MAX_RF_PATH][MAX_TX_COUNT];
	u8 bw80_diff[MAX_RF_PATH][MAX_TX_COUNT];
	u8 bw160_diff[MAX_RF_PATH][MAX_TX_COUNT];
};

enum rate_section {
	CCK = 0,
	OFDM,
	HT_MCS0_MCS7,
	HT_MCS8_MCS15,
	VHT_1SSMCS0_1SSMCS9,
	VHT_2SSMCS0_2SSMCS9,
	MAX_RATE_SECTION,
};

enum intf_type {
	INTF_PCI = 0,
	INTF_USB = 1,
};

enum radio_path {
	RF90_PATH_A = 0,
	RF90_PATH_B = 1,
	RF90_PATH_C = 2,
	RF90_PATH_D = 3,
};

enum radio_mask {
	RF_MASK_A = BIT(0),
	RF_MASK_B = BIT(1),
	RF_MASK_C = BIT(2),
	RF_MASK_D = BIT(3),
};

enum regulation_txpwr_lmt {
	TXPWR_LMT_FCC = 0,
	TXPWR_LMT_MKK = 1,
	TXPWR_LMT_ETSI = 2,
	TXPWR_LMT_WW = 3,

	TXPWR_LMT_MAX_REGULATION_NUM = 4
};

enum rt_eeprom_type {
	EEPROM_93C46,
	EEPROM_93C56,
	EEPROM_BOOT_EFUSE,
};

enum ttl_status {
	RTL_STATUS_INTERFACE_START = 0,
};

enum hardware_type {
	HARDWARE_TYPE_RTL8192E,
	HARDWARE_TYPE_RTL8192U,
	HARDWARE_TYPE_RTL8192SE,
	HARDWARE_TYPE_RTL8192SU,
	HARDWARE_TYPE_RTL8192CE,
	HARDWARE_TYPE_RTL8192CU,
	HARDWARE_TYPE_RTL8192DE,
	HARDWARE_TYPE_RTL8192DU,
	HARDWARE_TYPE_RTL8723AE,
	HARDWARE_TYPE_RTL8723U,
	HARDWARE_TYPE_RTL8188EE,
	HARDWARE_TYPE_RTL8723BE,
	HARDWARE_TYPE_RTL8192EE,
	HARDWARE_TYPE_RTL8821AE,
	HARDWARE_TYPE_RTL8812AE,
	HARDWARE_TYPE_RTL8822BE,

	/* keep it last */
	HARDWARE_TYPE_NUM
};

#define RTL_HW_TYPE(rtlpriv)	(rtl_hal((struct rtl_priv *)rtlpriv)->hw_type)
#define IS_NEW_GENERATION_IC(rtlpriv)			\
			(RTL_HW_TYPE(rtlpriv) >= HARDWARE_TYPE_RTL8192EE)
#define IS_HARDWARE_TYPE_8192CE(rtlpriv)		\
			(RTL_HW_TYPE(rtlpriv) == HARDWARE_TYPE_RTL8192CE)
#define IS_HARDWARE_TYPE_8812(rtlpriv)			\
			(RTL_HW_TYPE(rtlpriv) == HARDWARE_TYPE_RTL8812AE)
#define IS_HARDWARE_TYPE_8821(rtlpriv)			\
			(RTL_HW_TYPE(rtlpriv) == HARDWARE_TYPE_RTL8821AE)
#define IS_HARDWARE_TYPE_8723A(rtlpriv)			\
			(RTL_HW_TYPE(rtlpriv) == HARDWARE_TYPE_RTL8723AE)
#define IS_HARDWARE_TYPE_8723B(rtlpriv)			\
			(RTL_HW_TYPE(rtlpriv) == HARDWARE_TYPE_RTL8723BE)
#define IS_HARDWARE_TYPE_8192E(rtlpriv)			\
			(RTL_HW_TYPE(rtlpriv) == HARDWARE_TYPE_RTL8192EE)
#define IS_HARDWARE_TYPE_8822B(rtlpriv)			\
			(RTL_HW_TYPE(rtlpriv) == HARDWARE_TYPE_RTL8822BE)

#define RX_HAL_IS_CCK_RATE(rxmcs)			\
	((rxmcs) == DESC_RATE1M ||			\
	 (rxmcs) == DESC_RATE2M ||			\
	 (rxmcs) == DESC_RATE5_5M ||			\
	 (rxmcs) == DESC_RATE11M)

enum scan_operation_backup_opt {
	SCAN_OPT_BACKUP = 0,
	SCAN_OPT_BACKUP_BAND0 = 0,
	SCAN_OPT_BACKUP_BAND1,
	SCAN_OPT_RESTORE,
	SCAN_OPT_MAX
};

/*RF state.*/
enum rf_pwrstate {
	ERFON,
	ERFSLEEP,
	ERFOFF
};

struct bb_reg_def {
	u32 rfintfs;
	u32 rfintfi;
	u32 rfintfo;
	u32 rfintfe;
	u32 rf3wire_offset;
	u32 rflssi_select;
	u32 rftxgain_stage;
	u32 rfhssi_para1;
	u32 rfhssi_para2;
	u32 rfsw_ctrl;
	u32 rfagc_control1;
	u32 rfagc_control2;
	u32 rfrxiq_imbal;
	u32 rfrx_afe;
	u32 rftxiq_imbal;
	u32 rftx_afe;
	u32 rf_rb;		/* rflssi_readback */
	u32 rf_rbpi;		/* rflssi_readbackpi */
};

enum io_type {
	IO_CMD_PAUSE_DM_BY_SCAN = 0,
	IO_CMD_PAUSE_BAND0_DM_BY_SCAN = 0,
	IO_CMD_PAUSE_BAND1_DM_BY_SCAN = 1,
	IO_CMD_RESUME_DM_BY_SCAN = 2,
};

enum hw_variables {
	HW_VAR_ETHER_ADDR = 0x0,
	HW_VAR_MULTICAST_REG = 0x1,
	HW_VAR_BASIC_RATE = 0x2,
	HW_VAR_BSSID = 0x3,
	HW_VAR_MEDIA_STATUS = 0x4,
	HW_VAR_SECURITY_CONF = 0x5,
	HW_VAR_BEACON_INTERVAL = 0x6,
	HW_VAR_ATIM_WINDOW = 0x7,
	HW_VAR_LISTEN_INTERVAL = 0x8,
	HW_VAR_CS_COUNTER = 0x9,
	HW_VAR_DEFAULTKEY0 = 0xa,
	HW_VAR_DEFAULTKEY1 = 0xb,
	HW_VAR_DEFAULTKEY2 = 0xc,
	HW_VAR_DEFAULTKEY3 = 0xd,
	HW_VAR_SIFS = 0xe,
	HW_VAR_R2T_SIFS = 0xf,
	HW_VAR_DIFS = 0x10,
	HW_VAR_EIFS = 0x11,
	HW_VAR_SLOT_TIME = 0x12,
	HW_VAR_ACK_PREAMBLE = 0x13,
	HW_VAR_CW_CONFIG = 0x14,
	HW_VAR_CW_VALUES = 0x15,
	HW_VAR_RATE_FALLBACK_CONTROL = 0x16,
	HW_VAR_CONTENTION_WINDOW = 0x17,
	HW_VAR_RETRY_COUNT = 0x18,
	HW_VAR_TR_SWITCH = 0x19,
	HW_VAR_COMMAND = 0x1a,
	HW_VAR_WPA_CONFIG = 0x1b,
	HW_VAR_AMPDU_MIN_SPACE = 0x1c,
	HW_VAR_SHORTGI_DENSITY = 0x1d,
	HW_VAR_AMPDU_FACTOR = 0x1e,
	HW_VAR_MCS_RATE_AVAILABLE = 0x1f,
	HW_VAR_AC_PARAM = 0x20,
	HW_VAR_ACM_CTRL = 0x21,
	HW_VAR_DIS_REQ_QSIZE = 0x22,
	HW_VAR_CCX_CHNL_LOAD = 0x23,
	HW_VAR_CCX_NOISE_HISTOGRAM = 0x24,
	HW_VAR_CCX_CLM_NHM = 0x25,
	HW_VAR_TXOPLIMIT = 0x26,
	HW_VAR_TURBO_MODE = 0x27,
	HW_VAR_RF_STATE = 0x28,
	HW_VAR_RF_OFF_BY_HW = 0x29,
	HW_VAR_BUS_SPEED = 0x2a,
	HW_VAR_SET_DEV_POWER = 0x2b,

	HW_VAR_RCR = 0x2c,
	HW_VAR_RATR_0 = 0x2d,
	HW_VAR_RRSR = 0x2e,
	HW_VAR_CPU_RST = 0x2f,
	HW_VAR_CHECK_BSSID = 0x30,
	HW_VAR_LBK_MODE = 0x31,
	HW_VAR_AES_11N_FIX = 0x32,
	HW_VAR_USB_RX_AGGR = 0x33,
	HW_VAR_USER_CONTROL_TURBO_MODE = 0x34,
	HW_VAR_RETRY_LIMIT = 0x35,
	HW_VAR_INIT_TX_RATE = 0x36,
	HW_VAR_TX_RATE_REG = 0x37,
	HW_VAR_EFUSE_USAGE = 0x38,
	HW_VAR_EFUSE_BYTES = 0x39,
	HW_VAR_AUTOLOAD_STATUS = 0x3a,
	HW_VAR_RF_2R_DISABLE = 0x3b,
	HW_VAR_SET_RPWM = 0x3c,
	HW_VAR_H2C_FW_PWRMODE = 0x3d,
	HW_VAR_H2C_FW_JOINBSSRPT = 0x3e,
	HW_VAR_H2C_FW_MEDIASTATUSRPT = 0x3f,
	HW_VAR_H2C_FW_P2P_PS_OFFLOAD = 0x40,
	HW_VAR_FW_PSMODE_STATUS = 0x41,
	HW_VAR_INIT_RTS_RATE = 0x42,
	HW_VAR_RESUME_CLK_ON = 0x43,
	HW_VAR_FW_LPS_ACTION = 0x44,
	HW_VAR_1X1_RECV_COMBINE = 0x45,
	HW_VAR_STOP_SEND_BEACON = 0x46,
	HW_VAR_TSF_TIMER = 0x47,
	HW_VAR_IO_CMD = 0x48,

	HW_VAR_RF_RECOVERY = 0x49,
	HW_VAR_H2C_FW_UPDATE_GTK = 0x4a,
	HW_VAR_WF_MASK = 0x4b,
	HW_VAR_WF_CRC = 0x4c,
	HW_VAR_WF_IS_MAC_ADDR = 0x4d,
	HW_VAR_H2C_FW_OFFLOAD = 0x4e,
	HW_VAR_RESET_WFCRC = 0x4f,

	HW_VAR_HANDLE_FW_C2H = 0x50,
	HW_VAR_DL_FW_RSVD_PAGE = 0x51,
	HW_VAR_AID = 0x52,
	HW_VAR_HW_SEQ_ENABLE = 0x53,
	HW_VAR_CORRECT_TSF = 0x54,
	HW_VAR_BCN_VALID = 0x55,
	HW_VAR_FWLPS_RF_ON = 0x56,
	HW_VAR_DUAL_TSF_RST = 0x57,
	HW_VAR_SWITCH_EPHY_WOWLAN = 0x58,
	HW_VAR_INT_MIGRATION = 0x59,
	HW_VAR_INT_AC = 0x5a,
	HW_VAR_RF_TIMING = 0x5b,

	HAL_DEF_WOWLAN = 0x5c,
	HW_VAR_MRC = 0x5d,
	HW_VAR_KEEP_ALIVE = 0x5e,
	HW_VAR_NAV_UPPER = 0x5f,

	HW_VAR_MGT_FILTER = 0x60,
	HW_VAR_CTRL_FILTER = 0x61,
	HW_VAR_DATA_FILTER = 0x62,
};

enum rt_media_status {
	RT_MEDIA_DISCONNECT = 0,
	RT_MEDIA_CONNECT = 1
};

enum rt_oem_id {
	RT_CID_DEFAULT = 0,
	RT_CID_8187_ALPHA0 = 1,
	RT_CID_8187_SERCOMM_PS = 2,
	RT_CID_8187_HW_LED = 3,
	RT_CID_8187_NETGEAR = 4,
	RT_CID_WHQL = 5,
	RT_CID_819X_CAMEO = 6,
	RT_CID_819X_RUNTOP = 7,
	RT_CID_819X_SENAO = 8,
	RT_CID_TOSHIBA = 9,
	RT_CID_819X_NETCORE = 10,
	RT_CID_NETTRONIX = 11,
	RT_CID_DLINK = 12,
	RT_CID_PRONET = 13,
	RT_CID_COREGA = 14,
	RT_CID_819X_ALPHA = 15,
	RT_CID_819X_SITECOM = 16,
	RT_CID_CCX = 17,
	RT_CID_819X_LENOVO = 18,
	RT_CID_819X_QMI = 19,
	RT_CID_819X_EDIMAX_BELKIN = 20,
	RT_CID_819X_SERCOMM_BELKIN = 21,
	RT_CID_819X_CAMEO1 = 22,
	RT_CID_819X_MSI = 23,
	RT_CID_819X_ACER = 24,
	RT_CID_819X_HP = 27,
	RT_CID_819X_CLEVO = 28,
	RT_CID_819X_ARCADYAN_BELKIN = 29,
	RT_CID_819X_SAMSUNG = 30,
	RT_CID_819X_WNC_COREGA = 31,
	RT_CID_819X_FOXCOON = 32,
	RT_CID_819X_DELL = 33,
	RT_CID_819X_PRONETS = 34,
	RT_CID_819X_EDIMAX_ASUS = 35,
	RT_CID_NETGEAR = 36,
	RT_CID_PLANEX = 37,
	RT_CID_CC_C = 38,
	RT_CID_LENOVO_CHINA = 40,
};

enum hw_descs {
	HW_DESC_OWN,
	HW_DESC_RXOWN,
	HW_DESC_TX_NEXTDESC_ADDR,
	HW_DESC_TXBUFF_ADDR,
	HW_DESC_RXBUFF_ADDR,
	HW_DESC_RXPKT_LEN,
	HW_DESC_RXERO,
	HW_DESC_RX_PREPARE,
};

enum prime_sc {
	PRIME_CHNL_OFFSET_DONT_CARE = 0,
	PRIME_CHNL_OFFSET_LOWER = 1,
	PRIME_CHNL_OFFSET_UPPER = 2,
};

enum rf_type {
	RF_1T1R = 0,
	RF_1T2R = 1,
	RF_2T2R = 2,
	RF_2T2R_GREEN = 3,
	RF_2T3R = 4,
	RF_2T4R = 5,
	RF_3T3R = 6,
	RF_3T4R = 7,
	RF_4T4R = 8,
};

enum ht_channel_width {
	HT_CHANNEL_WIDTH_20 = 0,
	HT_CHANNEL_WIDTH_20_40 = 1,
	HT_CHANNEL_WIDTH_80 = 2,
	HT_CHANNEL_WIDTH_MAX,
};

/* Ref: 802.11i sepc D10.0 7.3.2.25.1
 * Cipher Suites Encryption Algorithms
 */
enum rt_enc_alg {
	NO_ENCRYPTION = 0,
	WEP40_ENCRYPTION = 1,
	TKIP_ENCRYPTION = 2,
	RSERVED_ENCRYPTION = 3,
	AESCCMP_ENCRYPTION = 4,
	WEP104_ENCRYPTION = 5,
	AESCMAC_ENCRYPTION = 6,	/*IEEE802.11w */
};

enum rtl_hal_state {
	_HAL_STATE_STOP = 0,
	_HAL_STATE_START = 1,
};

enum rtl_desc_rate {
	DESC_RATE1M = 0x00,
	DESC_RATE2M = 0x01,
	DESC_RATE5_5M = 0x02,
	DESC_RATE11M = 0x03,

	DESC_RATE6M = 0x04,
	DESC_RATE9M = 0x05,
	DESC_RATE12M = 0x06,
	DESC_RATE18M = 0x07,
	DESC_RATE24M = 0x08,
	DESC_RATE36M = 0x09,
	DESC_RATE48M = 0x0a,
	DESC_RATE54M = 0x0b,

	DESC_RATEMCS0 = 0x0c,
	DESC_RATEMCS1 = 0x0d,
	DESC_RATEMCS2 = 0x0e,
	DESC_RATEMCS3 = 0x0f,
	DESC_RATEMCS4 = 0x10,
	DESC_RATEMCS5 = 0x11,
	DESC_RATEMCS6 = 0x12,
	DESC_RATEMCS7 = 0x13,
	DESC_RATEMCS8 = 0x14,
	DESC_RATEMCS9 = 0x15,
	DESC_RATEMCS10 = 0x16,
	DESC_RATEMCS11 = 0x17,
	DESC_RATEMCS12 = 0x18,
	DESC_RATEMCS13 = 0x19,
	DESC_RATEMCS14 = 0x1a,
	DESC_RATEMCS15 = 0x1b,
	DESC_RATEMCS15_SG = 0x1c,
	DESC_RATEMCS32 = 0x20,

	DESC_RATEVHT1SS_MCS0 = 0x2c,
	DESC_RATEVHT1SS_MCS1 = 0x2d,
	DESC_RATEVHT1SS_MCS2 = 0x2e,
	DESC_RATEVHT1SS_MCS3 = 0x2f,
	DESC_RATEVHT1SS_MCS4 = 0x30,
	DESC_RATEVHT1SS_MCS5 = 0x31,
	DESC_RATEVHT1SS_MCS6 = 0x32,
	DESC_RATEVHT1SS_MCS7 = 0x33,
	DESC_RATEVHT1SS_MCS8 = 0x34,
	DESC_RATEVHT1SS_MCS9 = 0x35,
	DESC_RATEVHT2SS_MCS0 = 0x36,
	DESC_RATEVHT2SS_MCS1 = 0x37,
	DESC_RATEVHT2SS_MCS2 = 0x38,
	DESC_RATEVHT2SS_MCS3 = 0x39,
	DESC_RATEVHT2SS_MCS4 = 0x3a,
	DESC_RATEVHT2SS_MCS5 = 0x3b,
	DESC_RATEVHT2SS_MCS6 = 0x3c,
	DESC_RATEVHT2SS_MCS7 = 0x3d,
	DESC_RATEVHT2SS_MCS8 = 0x3e,
	DESC_RATEVHT2SS_MCS9 = 0x3f,
};

enum rtl_var_map {
	/*reg map */
	SYS_ISO_CTRL = 0,
	SYS_FUNC_EN,
	SYS_CLK,
	MAC_RCR_AM,
	MAC_RCR_AB,
	MAC_RCR_ACRC32,
	MAC_RCR_ACF,
	MAC_RCR_AAP,
	MAC_HIMR,
	MAC_HIMRE,
	MAC_HSISR,

	/*efuse map */
	EFUSE_TEST,
	EFUSE_CTRL,
	EFUSE_CLK,
	EFUSE_CLK_CTRL,
	EFUSE_PWC_EV12V,
	EFUSE_FEN_ELDR,
	EFUSE_LOADER_CLK_EN,
	EFUSE_ANA8M,
	EFUSE_HWSET_MAX_SIZE,
	EFUSE_MAX_SECTION_MAP,
	EFUSE_REAL_CONTENT_SIZE,
	EFUSE_OOB_PROTECT_BYTES_LEN,
	EFUSE_ACCESS,

	/*CAM map */
	RWCAM,
	WCAMI,
	RCAMO,
	CAMDBG,
	SECR,
	SEC_CAM_NONE,
	SEC_CAM_WEP40,
	SEC_CAM_TKIP,
	SEC_CAM_AES,
	SEC_CAM_WEP104,

	/*IMR map */
	RTL_IMR_BCNDMAINT6,	/*Beacon DMA Interrupt 6 */
	RTL_IMR_BCNDMAINT5,	/*Beacon DMA Interrupt 5 */
	RTL_IMR_BCNDMAINT4,	/*Beacon DMA Interrupt 4 */
	RTL_IMR_BCNDMAINT3,	/*Beacon DMA Interrupt 3 */
	RTL_IMR_BCNDMAINT2,	/*Beacon DMA Interrupt 2 */
	RTL_IMR_BCNDMAINT1,	/*Beacon DMA Interrupt 1 */
	RTL_IMR_BCNDOK8,	/*Beacon Queue DMA OK Interrup 8 */
	RTL_IMR_BCNDOK7,	/*Beacon Queue DMA OK Interrup 7 */
	RTL_IMR_BCNDOK6,	/*Beacon Queue DMA OK Interrup 6 */
	RTL_IMR_BCNDOK5,	/*Beacon Queue DMA OK Interrup 5 */
	RTL_IMR_BCNDOK4,	/*Beacon Queue DMA OK Interrup 4 */
	RTL_IMR_BCNDOK3,	/*Beacon Queue DMA OK Interrup 3 */
	RTL_IMR_BCNDOK2,	/*Beacon Queue DMA OK Interrup 2 */
	RTL_IMR_BCNDOK1,	/*Beacon Queue DMA OK Interrup 1 */
	RTL_IMR_TIMEOUT2,	/*Timeout interrupt 2 */
	RTL_IMR_TIMEOUT1,	/*Timeout interrupt 1 */
	RTL_IMR_TXFOVW,		/*Transmit FIFO Overflow */
	RTL_IMR_PSTIMEOUT,	/*Power save time out interrupt */
	RTL_IMR_BCNINT,		/*Beacon DMA Interrupt 0 */
	RTL_IMR_RXFOVW,		/*Receive FIFO Overflow */
	RTL_IMR_RDU,		/*Receive Descriptor Unavailable */
	RTL_IMR_ATIMEND,	/*For 92C,ATIM Window End Interrupt */
	RTL_IMR_H2CDOK,		/*H2C Queue DMA OK Interrupt */
	RTL_IMR_BDOK,		/*Beacon Queue DMA OK Interrup */
	RTL_IMR_HIGHDOK,	/*High Queue DMA OK Interrupt */
	RTL_IMR_COMDOK,		/*Command Queue DMA OK Interrupt*/
	RTL_IMR_TBDOK,		/*Transmit Beacon OK interrup */
	RTL_IMR_MGNTDOK,	/*Management Queue DMA OK Interrupt */
	RTL_IMR_TBDER,		/*For 92C,Transmit Beacon Error Interrupt */
	RTL_IMR_BKDOK,		/*AC_BK DMA OK Interrupt */
	RTL_IMR_BEDOK,		/*AC_BE DMA OK Interrupt */
	RTL_IMR_VIDOK,		/*AC_VI DMA OK Interrupt */
	RTL_IMR_VODOK,		/*AC_VO DMA Interrupt */
	RTL_IMR_ROK,		/*Receive DMA OK Interrupt */
	RTL_IMR_HSISR_IND,	/*HSISR Interrupt*/
	RTL_IBSS_INT_MASKS,	/*(RTL_IMR_BCNINT | RTL_IMR_TBDOK |
				 * RTL_IMR_TBDER)
				 */
	RTL_IMR_C2HCMD,		/*fw interrupt*/

	/*CCK Rates, TxHT = 0 */
	RTL_RC_CCK_RATE1M,
	RTL_RC_CCK_RATE2M,
	RTL_RC_CCK_RATE5_5M,
	RTL_RC_CCK_RATE11M,

	/*OFDM Rates, TxHT = 0 */
	RTL_RC_OFDM_RATE6M,
	RTL_RC_OFDM_RATE9M,
	RTL_RC_OFDM_RATE12M,
	RTL_RC_OFDM_RATE18M,
	RTL_RC_OFDM_RATE24M,
	RTL_RC_OFDM_RATE36M,
	RTL_RC_OFDM_RATE48M,
	RTL_RC_OFDM_RATE54M,

	RTL_RC_HT_RATEMCS7,
	RTL_RC_HT_RATEMCS15,

	RTL_RC_VHT_RATE_1SS_MCS7,
	RTL_RC_VHT_RATE_1SS_MCS8,
	RTL_RC_VHT_RATE_1SS_MCS9,
	RTL_RC_VHT_RATE_2SS_MCS7,
	RTL_RC_VHT_RATE_2SS_MCS8,
	RTL_RC_VHT_RATE_2SS_MCS9,

	/*keep it last */
	RTL_VAR_MAP_MAX,
};

/*Firmware PS mode for control LPS.*/
enum _fw_ps_mode {
	FW_PS_ACTIVE_MODE = 0,
	FW_PS_MIN_MODE = 1,
	FW_PS_MAX_MODE = 2,
	FW_PS_DTIM_MODE = 3,
	FW_PS_VOIP_MODE = 4,
	FW_PS_UAPSD_WMM_MODE = 5,
	FW_PS_UAPSD_MODE = 6,
	FW_PS_IBSS_MODE = 7,
	FW_PS_WWLAN_MODE = 8,
	FW_PS_PM_RADIO_OFF = 9,
	FW_PS_PM_CARD_DISABLE = 10,
};

enum rt_psmode {
	EACTIVE,		/*Active/Continuous access. */
	EMAXPS,			/*Max power save mode. */
	EFASTPS,		/*Fast power save mode. */
	EAUTOPS,		/*Auto power save mode. */
};

/*LED related.*/
enum led_ctl_mode {
	LED_CTL_POWER_ON = 1,
	LED_CTL_LINK = 2,
	LED_CTL_NO_LINK = 3,
	LED_CTL_TX = 4,
	LED_CTL_RX = 5,
	LED_CTL_SITE_SURVEY = 6,
	LED_CTL_POWER_OFF = 7,
	LED_CTL_START_TO_LINK = 8,
	LED_CTL_START_WPS = 9,
	LED_CTL_STOP_WPS = 10,
};

enum rtl_led_pin {
	LED_PIN_GPIO0,
	LED_PIN_LED0,
	LED_PIN_LED1,
	LED_PIN_LED2
};

/*QoS related.*/
/*acm implementation method.*/
enum acm_method {
	EACMWAY0_SWANDHW = 0,
	EACMWAY1_HW = 1,
	EACMWAY2_SW = 2,
};

enum macphy_mode {
	SINGLEMAC_SINGLEPHY = 0,
	DUALMAC_DUALPHY,
	DUALMAC_SINGLEPHY,
};

enum band_type {
	BAND_ON_2_4G = 0,
	BAND_ON_5G,
	BAND_ON_BOTH,
	BANDMAX
};

/* aci/aifsn Field.
 * Ref: WMM spec 2.2.2: WME Parameter Element, p.12.
 */
union aci_aifsn {
	u8 char_data;

	struct {
		u8 aifsn:4;
		u8 acm:1;
		u8 aci:2;
		u8 reserved:1;
	} f;			/* Field */
};

/*mlme related.*/
enum wireless_mode {
	WIRELESS_MODE_UNKNOWN = 0x00,
	WIRELESS_MODE_A = 0x01,
	WIRELESS_MODE_B = 0x02,
	WIRELESS_MODE_G = 0x04,
	WIRELESS_MODE_AUTO = 0x08,
	WIRELESS_MODE_N_24G = 0x10,
	WIRELESS_MODE_N_5G = 0x20,
	WIRELESS_MODE_AC_5G = 0x40,
	WIRELESS_MODE_AC_24G  = 0x80,
	WIRELESS_MODE_AC_ONLY = 0x100,
	WIRELESS_MODE_MAX = 0x800
};

#define IS_WIRELESS_MODE_A(wirelessmode)	\
	(wirelessmode == WIRELESS_MODE_A)
#define IS_WIRELESS_MODE_B(wirelessmode)	\
	(wirelessmode == WIRELESS_MODE_B)
#define IS_WIRELESS_MODE_G(wirelessmode)	\
	(wirelessmode == WIRELESS_MODE_G)
#define IS_WIRELESS_MODE_N_24G(wirelessmode)	\
	(wirelessmode == WIRELESS_MODE_N_24G)
#define IS_WIRELESS_MODE_N_5G(wirelessmode)	\
	(wirelessmode == WIRELESS_MODE_N_5G)

enum ratr_table_mode {
	RATR_INX_WIRELESS_NGB = 0,
	RATR_INX_WIRELESS_NG = 1,
	RATR_INX_WIRELESS_NB = 2,
	RATR_INX_WIRELESS_N = 3,
	RATR_INX_WIRELESS_GB = 4,
	RATR_INX_WIRELESS_G = 5,
	RATR_INX_WIRELESS_B = 6,
	RATR_INX_WIRELESS_MC = 7,
	RATR_INX_WIRELESS_A = 8,
	RATR_INX_WIRELESS_AC_5N = 8,
	RATR_INX_WIRELESS_AC_24N = 9,
};

enum ratr_table_mode_new {
	RATEID_IDX_BGN_40M_2SS = 0,
	RATEID_IDX_BGN_40M_1SS = 1,
	RATEID_IDX_BGN_20M_2SS_BN = 2,
	RATEID_IDX_BGN_20M_1SS_BN = 3,
	RATEID_IDX_GN_N2SS = 4,
	RATEID_IDX_GN_N1SS = 5,
	RATEID_IDX_BG = 6,
	RATEID_IDX_G = 7,
	RATEID_IDX_B = 8,
	RATEID_IDX_VHT_2SS = 9,
	RATEID_IDX_VHT_1SS = 10,
	RATEID_IDX_MIX1 = 11,
	RATEID_IDX_MIX2 = 12,
	RATEID_IDX_VHT_3SS = 13,
	RATEID_IDX_BGN_3SS = 14,
};

enum rtl_link_state {
	MAC80211_NOLINK = 0,
	MAC80211_LINKING = 1,
	MAC80211_LINKED = 2,
	MAC80211_LINKED_SCANNING = 3,
};

enum act_category {
	ACT_CAT_QOS = 1,
	ACT_CAT_DLS = 2,
	ACT_CAT_BA = 3,
	ACT_CAT_HT = 7,
	ACT_CAT_WMM = 17,
};

enum ba_action {
	ACT_ADDBAREQ = 0,
	ACT_ADDBARSP = 1,
	ACT_DELBA = 2,
};

enum rt_polarity_ctl {
	RT_POLARITY_LOW_ACT = 0,
	RT_POLARITY_HIGH_ACT = 1,
};

/* After 8188E, we use V2 reason define. 88C/8723A use V1 reason. */
enum fw_wow_reason_v2 {
	FW_WOW_V2_PTK_UPDATE_EVENT = 0x01,
	FW_WOW_V2_GTK_UPDATE_EVENT = 0x02,
	FW_WOW_V2_DISASSOC_EVENT = 0x04,
	FW_WOW_V2_DEAUTH_EVENT = 0x08,
	FW_WOW_V2_FW_DISCONNECT_EVENT = 0x10,
	FW_WOW_V2_MAGIC_PKT_EVENT = 0x21,
	FW_WOW_V2_UNICAST_PKT_EVENT = 0x22,
	FW_WOW_V2_PATTERN_PKT_EVENT = 0x23,
	FW_WOW_V2_RTD3_SSID_MATCH_EVENT = 0x24,
	FW_WOW_V2_REALWOW_V2_WAKEUPPKT = 0x30,
	FW_WOW_V2_REALWOW_V2_ACKLOST = 0x31,
	FW_WOW_V2_REASON_MAX = 0xff,
};

enum wolpattern_type {
	UNICAST_PATTERN = 0,
	MULTICAST_PATTERN = 1,
	BROADCAST_PATTERN = 2,
	DONT_CARE_DA = 3,
	UNKNOWN_TYPE = 4,
};

enum package_type {
	PACKAGE_DEFAULT,
	PACKAGE_QFN68,
	PACKAGE_TFBGA90,
	PACKAGE_TFBGA80,
	PACKAGE_TFBGA79
};

enum rtl_spec_ver {
	RTL_SPEC_NEW_RATEID = BIT(0),	/* use ratr_table_mode_new */
	RTL_SPEC_SUPPORT_VHT = BIT(1),	/* support VHT */
	RTL_SPEC_EXT_C2H = BIT(2),	/* extend FW C2H (e.g. TX REPORT) */
};

enum dm_info_query {
	DM_INFO_FA_OFDM,
	DM_INFO_FA_CCK,
	DM_INFO_FA_TOTAL,
	DM_INFO_CCA_OFDM,
	DM_INFO_CCA_CCK,
	DM_INFO_CCA_ALL,
	DM_INFO_CRC32_OK_VHT,
	DM_INFO_CRC32_OK_HT,
	DM_INFO_CRC32_OK_LEGACY,
	DM_INFO_CRC32_OK_CCK,
	DM_INFO_CRC32_ERROR_VHT,
	DM_INFO_CRC32_ERROR_HT,
	DM_INFO_CRC32_ERROR_LEGACY,
	DM_INFO_CRC32_ERROR_CCK,
	DM_INFO_EDCCA_FLAG,
	DM_INFO_OFDM_ENABLE,
	DM_INFO_CCK_ENABLE,
	DM_INFO_CRC32_OK_HT_AGG,
	DM_INFO_CRC32_ERROR_HT_AGG,
	DM_INFO_DBG_PORT_0,
	DM_INFO_CURR_IGI,
	DM_INFO_RSSI_MIN,
	DM_INFO_RSSI_MAX,
	DM_INFO_CLM_RATIO,
	DM_INFO_NHM_RATIO,
	DM_INFO_IQK_ALL,
	DM_INFO_IQK_OK,
	DM_INFO_IQK_NG,
	DM_INFO_SIZE,
};

enum rx_packet_type {
	NORMAL_RX,
	TX_REPORT1,
	TX_REPORT2,
	HIS_REPORT,
	C2H_PACKET,
};

struct rtlwifi_tx_info {
	int sn;
	unsigned long send_time;
};

static inline struct rtlwifi_tx_info *rtl_tx_skb_cb_info(struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	BUILD_BUG_ON(sizeof(struct rtlwifi_tx_info) >
		     sizeof(info->status.status_driver_data));

	return (struct rtlwifi_tx_info *)(info->status.status_driver_data);
}

struct octet_string {
	u8 *octet;
	u16 length;
};

struct rtl_hdr_3addr {
	__le16 frame_ctl;
	__le16 duration_id;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctl;
	u8 payload[0];
} __packed;

struct rtl_info_element {
	u8 id;
	u8 len;
	u8 data[0];
} __packed;

struct rtl_probe_rsp {
	struct rtl_hdr_3addr header;
	u32 time_stamp[2];
	__le16 beacon_interval;
	__le16 capability;
	/*SSID, supported rates, FH params, DS params,
	 * CF params, IBSS params, TIM (if beacon), RSN
	 */
	struct rtl_info_element info_element[0];
} __packed;

/*LED related.*/
/*ledpin Identify how to implement this SW led.*/
struct rtl_led {
	void *hw;
	enum rtl_led_pin ledpin;
	bool ledon;
};

struct rtl_led_ctl {
	bool led_opendrain;
	struct rtl_led sw_led0;
	struct rtl_led sw_led1;
};

struct rtl_qos_parameters {
	__le16 cw_min;
	__le16 cw_max;
	u8 aifs;
	u8 flag;
	__le16 tx_op;
} __packed;

struct rt_smooth_data {
	u32 elements[100];	/*array to store values */
	u32 index;		/*index to current array to store */
	u32 total_num;		/*num of valid elements */
	u32 total_val;		/*sum of valid elements */
};

struct false_alarm_statistics {
	u32 cnt_parity_fail;
	u32 cnt_rate_illegal;
	u32 cnt_crc8_fail;
	u32 cnt_mcs_fail;
	u32 cnt_fast_fsync_fail;
	u32 cnt_sb_search_fail;
	u32 cnt_ofdm_fail;
	u32 cnt_cck_fail;
	u32 cnt_all;
	u32 cnt_ofdm_cca;
	u32 cnt_cck_cca;
	u32 cnt_cca_all;
	u32 cnt_bw_usc;
	u32 cnt_bw_lsc;
};

struct init_gain {
	u8 xaagccore1;
	u8 xbagccore1;
	u8 xcagccore1;
	u8 xdagccore1;
	u8 cca;

};

struct wireless_stats {
	u64 txbytesunicast;
	u64 txbytesmulticast;
	u64 txbytesbroadcast;
	u64 rxbytesunicast;

	u64 txbytesunicast_inperiod;
	u64 rxbytesunicast_inperiod;
	u32 txbytesunicast_inperiod_tp;
	u32 rxbytesunicast_inperiod_tp;
	u64 txbytesunicast_last;
	u64 rxbytesunicast_last;

	long rx_snr_db[4];
	/*Correct smoothed ss in Dbm, only used
	 * in driver to report real power now.
	 */
	long recv_signal_power;
	long signal_quality;
	long last_sigstrength_inpercent;

	u32 rssi_calculate_cnt;
	u32 pwdb_all_cnt;

	/* Transformed, in dbm. Beautified signal
	 * strength for UI, not correct.
	 */
	long signal_strength;

	u8 rx_rssi_percentage[4];
	u8 rx_evm_dbm[4];
	u8 rx_evm_percentage[2];

	u16 rx_cfo_short[4];
	u16 rx_cfo_tail[4];

	struct rt_smooth_data ui_rssi;
	struct rt_smooth_data ui_link_quality;
};

struct rate_adaptive {
	u8 rate_adaptive_disabled;
	u8 ratr_state;
	u16 reserve;

	u32 high_rssi_thresh_for_ra;
	u32 high2low_rssi_thresh_for_ra;
	u8 low2high_rssi_thresh_for_ra40m;
	u32 low_rssi_thresh_for_ra40m;
	u8 low2high_rssi_thresh_for_ra20m;
	u32 low_rssi_thresh_for_ra20m;
	u32 upper_rssi_threshold_ratr;
	u32 middleupper_rssi_threshold_ratr;
	u32 middle_rssi_threshold_ratr;
	u32 middlelow_rssi_threshold_ratr;
	u32 low_rssi_threshold_ratr;
	u32 ultralow_rssi_threshold_ratr;
	u32 low_rssi_threshold_ratr_40m;
	u32 low_rssi_threshold_ratr_20m;
	u8 ping_rssi_enable;
	u32 ping_rssi_ratr;
	u32 ping_rssi_thresh_for_ra;
	u32 last_ratr;
	u8 pre_ratr_state;
	u8 ldpc_thres;
	bool use_ldpc;
	bool lower_rts_rate;
	bool is_special_data;
};

struct regd_pair_mapping {
	u16 reg_dmnenum;
	u16 reg_5ghz_ctl;
	u16 reg_2ghz_ctl;
};

struct dynamic_primary_cca {
	u8 pricca_flag;
	u8 intf_flag;
	u8 intf_type;
	u8 dup_rts_flag;
	u8 monitor_flag;
	u8 ch_offset;
	u8 mf_state;
};

struct rtl_regulatory {
	s8 alpha2[2];
	u16 country_code;
	u16 max_power_level;
	u32 tp_scale;
	u16 current_rd;
	u16 current_rd_ext;
	int16_t power_limit;
	struct regd_pair_mapping *regpair;
};

struct rtl_rfkill {
	bool rfkill_state;	/*0 is off, 1 is on */
};

/*for P2P PS**/
#define	P2P_MAX_NOA_NUM		2

enum p2p_role {
	P2P_ROLE_DISABLE = 0,
	P2P_ROLE_DEVICE = 1,
	P2P_ROLE_CLIENT = 2,
	P2P_ROLE_GO = 3
};

enum p2p_ps_state {
	P2P_PS_DISABLE = 0,
	P2P_PS_ENABLE = 1,
	P2P_PS_SCAN = 2,
	P2P_PS_SCAN_DONE = 3,
	P2P_PS_ALLSTASLEEP = 4, /* for P2P GO */
};

enum p2p_ps_mode {
	P2P_PS_NONE = 0,
	P2P_PS_CTWINDOW = 1,
	P2P_PS_NOA	 = 2,
	P2P_PS_MIX = 3, /* CTWindow and NoA */
};

struct rtl_p2p_ps_info {
	enum p2p_ps_mode p2p_ps_mode; /* indicate p2p ps mode */
	enum p2p_ps_state p2p_ps_state; /*  indicate p2p ps state */
	u8 noa_index; /*  Identifies instance of Notice of Absence timing. */
	/*  Client traffic window. A period of time in TU after TBTT. */
	u8 ctwindow;
	u8 opp_ps; /*  opportunistic power save. */
	u8 noa_num; /*  number of NoA descriptor in P2P IE. */
	/*  Count for owner, Type of client. */
	u8 noa_count_type[P2P_MAX_NOA_NUM];
	/*  Max duration for owner, preferred or min acceptable duration
	 * for client.
	 */
	u32 noa_duration[P2P_MAX_NOA_NUM];
	/*  Length of interval for owner, preferred or max acceptable intervali
	 * of client.
	 */
	u32 noa_interval[P2P_MAX_NOA_NUM];
	/*  schedule in terms of the lower 4 bytes of the TSF timer. */
	u32 noa_start_time[P2P_MAX_NOA_NUM];
};

struct p2p_ps_offload_t {
	u8 offload_en:1;
	u8 role:1; /* 1: Owner, 0: Client */
	u8 ctwindow_en:1;
	u8 noa0_en:1;
	u8 noa1_en:1;
	u8 allstasleep:1;
	u8 discovery:1;
	u8 reserved:1;
};

#define IQK_MATRIX_REG_NUM	8
#define IQK_MATRIX_SETTINGS_NUM	(1 + 24 + 21)

struct iqk_matrix_regs {
	bool iqk_done;
	long value[1][IQK_MATRIX_REG_NUM];
};

struct phy_parameters {
	u16 length;
	u32 *pdata;
};

enum hw_param_tab_index {
	PHY_REG_2T,
	PHY_REG_1T,
	PHY_REG_PG,
	RADIOA_2T,
	RADIOB_2T,
	RADIOA_1T,
	RADIOB_1T,
	MAC_REG,
	AGCTAB_2T,
	AGCTAB_1T,
	MAX_TAB
};

struct rtl_phy {
	struct bb_reg_def phyreg_def[4];	/*Radio A/B/C/D */
	struct init_gain initgain_backup;
	enum io_type current_io_type;

	u8 rf_mode;
	u8 rf_type;
	u8 current_chan_bw;
	u8 set_bwmode_inprogress;
	u8 sw_chnl_inprogress;
	u8 sw_chnl_stage;
	u8 sw_chnl_step;
	u8 current_channel;
	u8 h2c_box_num;
	u8 set_io_inprogress;
	u8 lck_inprogress;

	/* record for power tracking */
	s32 reg_e94;
	s32 reg_e9c;
	s32 reg_ea4;
	s32 reg_eac;
	s32 reg_eb4;
	s32 reg_ebc;
	s32 reg_ec4;
	s32 reg_ecc;
	u8 rfpienable;
	u8 reserve_0;
	u16 reserve_1;
	u32 reg_c04, reg_c08, reg_874;
	u32 adda_backup[16];
	u32 iqk_mac_backup[IQK_MAC_REG_NUM];
	u32 iqk_bb_backup[10];
	bool iqk_initialized;

	bool rfpath_rx_enable[MAX_RF_PATH];
	u8 reg_837;
	/* Dual mac */
	bool need_iqk;
	struct iqk_matrix_regs iqk_matrix[IQK_MATRIX_SETTINGS_NUM];

	bool rfpi_enable;
	bool iqk_in_progress;

	u8 pwrgroup_cnt;
	u8 cck_high_power;
	/* this is for 88E & 8723A */
	u32 mcs_txpwrlevel_origoffset[MAX_PG_GROUP][16];
	/* MAX_PG_GROUP groups of pwr diff by rates */
	u32 mcs_offset[MAX_PG_GROUP][16];
	u32 tx_power_by_rate_offset[TX_PWR_BY_RATE_NUM_BAND]
				   [TX_PWR_BY_RATE_NUM_RF]
				   [TX_PWR_BY_RATE_NUM_RF]
				   [TX_PWR_BY_RATE_NUM_RATE];
	u8 txpwr_by_rate_base_24g[TX_PWR_BY_RATE_NUM_RF]
				 [TX_PWR_BY_RATE_NUM_RF]
				 [MAX_BASE_NUM_IN_PHY_REG_PG_24G];
	u8 txpwr_by_rate_base_5g[TX_PWR_BY_RATE_NUM_RF]
				[TX_PWR_BY_RATE_NUM_RF]
				[MAX_BASE_NUM_IN_PHY_REG_PG_5G];
	u8 default_initialgain[4];

	/* the current Tx power level */
	u8 cur_cck_txpwridx;
	u8 cur_ofdm24g_txpwridx;
	u8 cur_bw20_txpwridx;
	u8 cur_bw40_txpwridx;

	s8 txpwr_limit_2_4g[MAX_REGULATION_NUM]
			   [MAX_2_4G_BANDWIDTH_NUM]
			   [MAX_RATE_SECTION_NUM]
			   [CHANNEL_MAX_NUMBER_2G]
			   [MAX_RF_PATH_NUM];
	s8 txpwr_limit_5g[MAX_REGULATION_NUM]
			 [MAX_5G_BANDWIDTH_NUM]
			 [MAX_RATE_SECTION_NUM]
			 [CHANNEL_MAX_NUMBER_5G]
			 [MAX_RF_PATH_NUM];

	u32 rfreg_chnlval[2];
	bool apk_done;
	u32 reg_rf3c[2];	/* pathA / pathB  */

	u32 backup_rf_0x1a;/*92ee*/
	/* bfsync */
	u8 framesync;
	u32 framesync_c34;

	u8 num_total_rfpath;
	struct phy_parameters hwparam_tables[MAX_TAB];
	u16 rf_pathmap;

	u8 hw_rof_enable; /*Enable GPIO[9] as WL RF HW PDn source*/
	enum rt_polarity_ctl polarity_ctl;
};

#define MAX_TID_COUNT				9
#define RTL_AGG_STOP				0
#define RTL_AGG_PROGRESS			1
#define RTL_AGG_START				2
#define RTL_AGG_OPERATIONAL			3
#define RTL_AGG_OFF				0
#define RTL_AGG_ON				1
#define RTL_RX_AGG_START			1
#define RTL_RX_AGG_STOP				0
#define RTL_AGG_EMPTYING_HW_QUEUE_ADDBA		2
#define RTL_AGG_EMPTYING_HW_QUEUE_DELBA		3

struct rtl_ht_agg {
	u16 txq_id;
	u16 wait_for_ba;
	u16 start_idx;
	u64 bitmap;
	u32 rate_n_flags;
	u8 agg_state;
	u8 rx_agg_state;
};

struct rssi_sta {
	long undec_sm_pwdb;
	long undec_sm_cck;
};

struct rtl_tid_data {
	struct rtl_ht_agg agg;
};

struct rtl_sta_info {
	struct list_head list;
	struct rtl_tid_data tids[MAX_TID_COUNT];
	/* just used for ap adhoc or mesh*/
	struct rssi_sta rssi_stat;
	u8 rssi_level;
	u16 wireless_mode;
	u8 ratr_index;
	u8 mimo_ps;
	u8 mac_addr[ETH_ALEN];
} __packed;

struct rtl_priv;
struct rtl_io {
	struct device *dev;
	struct mutex bb_mutex;

	/*PCI MEM map */
	unsigned long pci_mem_end;	/*shared mem end        */
	unsigned long pci_mem_start;	/*shared mem start */

	/*PCI IO map */
	unsigned long pci_base_addr;	/*device I/O address */

	void (*write8_async)(struct rtl_priv *rtlpriv, u32 addr, u8 val);
	void (*write16_async)(struct rtl_priv *rtlpriv, u32 addr, u16 val);
	void (*write32_async)(struct rtl_priv *rtlpriv, u32 addr, u32 val);
	void (*writen_sync)(struct rtl_priv *rtlpriv, u32 addr, void *buf,
			    u16 len);

	u8 (*read8_sync)(struct rtl_priv *rtlpriv, u32 addr);
	u16 (*read16_sync)(struct rtl_priv *rtlpriv, u32 addr);
	u32 (*read32_sync)(struct rtl_priv *rtlpriv, u32 addr);

};

struct rtl_mac {
	u8 mac_addr[ETH_ALEN];
	u8 mac80211_registered;
	u8 beacon_enabled;

	u32 tx_ss_num;
	u32 rx_ss_num;

	struct ieee80211_supported_band bands[NUM_NL80211_BANDS];
	struct ieee80211_hw *hw;
	struct ieee80211_vif *vif;
	enum nl80211_iftype opmode;

	/*Probe Beacon management */
	struct rtl_tid_data tids[MAX_TID_COUNT];
	enum rtl_link_state link_state;

	int n_channels;
	int n_bitrates;

	bool offchan_delay;
	u8 p2p;	/*using p2p role*/
	bool p2p_in_use;

	/*filters */
	u32 rx_conf;
	u16 rx_mgt_filter;
	u16 rx_ctrl_filter;
	u16 rx_data_filter;

	bool act_scanning;
	u8 cnt_after_linked;
	bool skip_scan;

	/* early mode */
	/* skb wait queue */
	struct sk_buff_head skb_waitq[MAX_TID_COUNT];

	u8 ht_stbc_cap;
	u8 ht_cur_stbc;

	/*vht support*/
	u8 vht_enable;
	u8 bw_80;
	u8 vht_cur_ldpc;
	u8 vht_cur_stbc;
	u8 vht_stbc_cap;
	u8 vht_ldpc_cap;

	/*RDG*/
	bool rdg_en;

	/*AP*/
	u8 bssid[ETH_ALEN] __aligned(2);
	u32 vendor;
	u8 mcs[16];	/* 16 bytes mcs for HT rates. */
	u32 basic_rates; /* b/g rates */
	u8 ht_enable;
	u8 sgi_40;
	u8 sgi_20;
	u8 bw_40;
	u16 mode;		/* wireless mode */
	u8 slot_time;
	u8 short_preamble;
	u8 use_cts_protect;
	u8 cur_40_prime_sc;
	u8 cur_40_prime_sc_bk;
	u8 cur_80_prime_sc;
	u64 tsf;
	u8 retry_short;
	u8 retry_long;
	u16 assoc_id;
	bool hiddenssid;

	/*IBSS*/
	int beacon_interval;

	/*AMPDU*/
	u8 min_space_cfg;	/*For Min spacing configurations */
	u8 max_mss_density;
	u8 current_ampdu_factor;
	u8 current_ampdu_density;

	/*QOS & EDCA */
	struct ieee80211_tx_queue_params edca_param[RTL_MAC80211_NUM_QUEUE];
	struct rtl_qos_parameters ac[AC_MAX];

	/* counters */
	u64 last_txok_cnt;
	u64 last_rxok_cnt;
	u32 last_bt_edca_ul;
	u32 last_bt_edca_dl;
};

struct btdm_8723 {
	bool all_off;
	bool agc_table_en;
	bool adc_back_off_on;
	bool b2_ant_hid_en;
	bool low_penalty_rate_adaptive;
	bool rf_rx_lpf_shrink;
	bool reject_aggre_pkt;
	bool tra_tdma_on;
	u8 tra_tdma_nav;
	u8 tra_tdma_ant;
	bool tdma_on;
	u8 tdma_ant;
	u8 tdma_nav;
	u8 tdma_dac_swing;
	u8 fw_dac_swing_lvl;
	bool ps_tdma_on;
	u8 ps_tdma_byte[5];
	bool pta_on;
	u32 val_0x6c0;
	u32 val_0x6c8;
	u32 val_0x6cc;
	bool sw_dac_swing_on;
	u32 sw_dac_swing_lvl;
	u32 wlan_act_hi;
	u32 wlan_act_lo;
	u32 bt_retry_index;
	bool dec_bt_pwr;
	bool ignore_wlan_act;
};

struct bt_coexist_8723 {
	u32 high_priority_tx;
	u32 high_priority_rx;
	u32 low_priority_tx;
	u32 low_priority_rx;
	u8 c2h_bt_info;
	bool c2h_bt_info_req_sent;
	bool c2h_bt_inquiry_page;
	u32 bt_inq_page_start_time;
	u8 bt_retry_cnt;
	u8 c2h_bt_info_original;
	u8 bt_inquiry_page_cnt;
	struct btdm_8723 btdm;
};

struct rtl_hal {
	struct ieee80211_hw *hw;
	bool driver_is_goingto_unload;
	bool up_first_time;
	bool first_init;
	bool being_init_adapter;
	bool bbrf_ready;
	bool mac_func_enable;
	bool pre_edcca_enable;
	struct bt_coexist_8723 hal_coex_8723;

	enum intf_type interface;
	u16 hw_type;		/*92c or 92d or 92s and so on */
	u8 ic_class;
	u8 oem_id;
	u32 version;		/*version of chip */
	u8 state;		/*stop 0, start 1 */
	u8 board_type;
	u8 package_type;
	u8 external_pa;

	u8 pa_mode;
	u8 pa_type_2g;
	u8 pa_type_5g;
	u8 lna_type_2g;
	u8 lna_type_5g;
	u8 external_pa_2g;
	u8 external_lna_2g;
	u8 external_pa_5g;
	u8 external_lna_5g;
	u8 type_glna;
	u8 type_gpa;
	u8 type_alna;
	u8 type_apa;
	u8 rfe_type;

	/*firmware */
	u32 fwsize;
	u8 *pfirmware;
	u16 fw_version;
	u16 fw_subversion;
	bool h2c_setinprogress;
	u8 last_hmeboxnum;
	bool fw_ready;
	/*Reserve page start offset except beacon in TxQ. */
	u8 fw_rsvdpage_startoffset;
	u8 h2c_txcmd_seq;
	u8 current_ra_rate;

	/* FW Cmd IO related */
	u16 fwcmd_iomap;
	u32 fwcmd_ioparam;
	bool set_fwcmd_inprogress;
	u8 current_fwcmd_io;

	struct p2p_ps_offload_t p2p_ps_offload;
	bool fw_clk_change_in_progress;
	bool allow_sw_to_change_hwclc;
	u8 fw_ps_state;
	/**/
	bool driver_going2unload;

	/*AMPDU init min space*/
	u8 minspace_cfg;	/*For Min spacing configurations */

	/* Dual mac */
	enum macphy_mode macphymode;
	enum band_type current_bandtype;	/* 0:2.4G, 1:5G */
	enum band_type current_bandtypebackup;
	enum band_type bandset;
	/* dual MAC 0--Mac0 1--Mac1 */
	u32 interfaceindex;
	/* just for DualMac S3S4 */
	u8 macphyctl_reg;
	bool earlymode_enable;
	u8 max_earlymode_num;
	/* Dual mac*/
	bool during_mac0init_radiob;
	bool during_mac1init_radioa;
	bool reloadtxpowerindex;
	/* True if IMR or IQK  have done
	 * for 2.4G in scan progress
	 */
	bool load_imrandiqk_setting_for2g;

	bool disable_amsdu_8k;
	bool master_of_dmsp;
	bool slave_of_dmsp;

	u16 rx_tag;/*for 92ee*/
	u8 rts_en;

	/*for wowlan*/
	bool wow_enable;
	bool enter_pnp_sleep;
	bool wake_from_pnp_sleep;
	bool wow_enabled;
	time64_t last_suspend_sec;
	u32 wowlan_fwsize;
	u8 *wowlan_firmware;

	u8 hw_rof_enable; /*Enable GPIO[9] as WL RF HW PDn source*/

	bool real_wow_v2_enable;
	bool re_init_llt_table;
};

struct rtl_security {
	/*default 0 */
	bool use_sw_sec;

	bool being_setkey;
	bool use_defaultkey;
	/*Encryption Algorithm for Unicast Packet */
	enum rt_enc_alg pairwise_enc_algorithm;
	/*Encryption Algorithm for Brocast/Multicast */
	enum rt_enc_alg group_enc_algorithm;
	/*Cam Entry Bitmap */
	u32 hwsec_cam_bitmap;
	u8 hwsec_cam_sta_addr[TOTAL_CAM_ENTRY][ETH_ALEN];
	/*local Key buffer, indx 0 is for
	 * pairwise key 1-4 is for agoup key.
	 */
	u8 key_buf[KEY_BUF_SIZE][MAX_KEY_LEN];
	u8 key_len[KEY_BUF_SIZE];

	/*The pointer of Pairwise Key,
	 * it always points to KeyBuf[4]
	 */
	u8 *pairwise_key;
};

#define ASSOCIATE_ENTRY_NUM	33

struct fast_ant_training {
	u8	bssid[6];
	u8	antsel_rx_keep_0;
	u8	antsel_rx_keep_1;
	u8	antsel_rx_keep_2;
	u32	ant_sum[7];
	u32	ant_cnt[7];
	u32	ant_ave[7];
	u8	fat_state;
	u32	train_idx;
	u8	antsel_a[ASSOCIATE_ENTRY_NUM];
	u8	antsel_b[ASSOCIATE_ENTRY_NUM];
	u8	antsel_c[ASSOCIATE_ENTRY_NUM];
	u32	main_ant_sum[ASSOCIATE_ENTRY_NUM];
	u32	aux_ant_sum[ASSOCIATE_ENTRY_NUM];
	u32	main_ant_cnt[ASSOCIATE_ENTRY_NUM];
	u32	aux_ant_cnt[ASSOCIATE_ENTRY_NUM];
	u8	rx_idle_ant;
	bool	becomelinked;
};

struct dm_phy_dbg_info {
	s8 rx_snrdb[4];
	u64 num_qry_phy_status;
	u64 num_qry_phy_status_cck;
	u64 num_qry_phy_status_ofdm;
	u16 num_qry_beacon_pkt;
	u16 num_non_be_pkt;
	s32 rx_evm[4];
};

struct rtl_dm {
	/*PHY status for Dynamic Management */
	long entry_min_undec_sm_pwdb;
	long undec_sm_cck;
	long undec_sm_pwdb;	/*out dm */
	long entry_max_undec_sm_pwdb;
	s32 ofdm_pkt_cnt;
	bool dm_initialgain_enable;
	bool dynamic_txpower_enable;
	bool current_turbo_edca;
	bool is_any_nonbepkts;	/*out dm */
	bool is_cur_rdlstate;
	bool txpower_trackinginit;
	bool disable_framebursting;
	bool cck_inch14;
	bool txpower_tracking;
	bool useramask;
	bool rfpath_rxenable[4];
	bool inform_fw_driverctrldm;
	bool current_mrc_switch;
	u8 txpowercount;
	u8 powerindex_backup[6];

	u8 thermalvalue_rxgain;
	u8 thermalvalue_iqk;
	u8 thermalvalue_lck;
	u8 thermalvalue;
	u8 last_dtp_lvl;
	u8 thermalvalue_avg[AVG_THERMAL_NUM];
	u8 thermalvalue_avg_index;
	u8 tm_trigger;
	bool done_txpower;
	u8 dynamic_txhighpower_lvl;	/*Tx high power level */
	u8 dm_flag;		/*Indicate each dynamic mechanism's status. */
	u8 dm_flag_tmp;
	u8 dm_type;
	u8 dm_rssi_sel;
	u8 txpower_track_control;
	bool interrupt_migration;
	bool disable_tx_int;
	s8 ofdm_index[MAX_RF_PATH];
	u8 default_ofdm_index;
	u8 default_cck_index;
	s8 cck_index;
	s8 delta_power_index[MAX_RF_PATH];
	s8 delta_power_index_last[MAX_RF_PATH];
	s8 power_index_offset[MAX_RF_PATH];
	s8 absolute_ofdm_swing_idx[MAX_RF_PATH];
	s8 remnant_ofdm_swing_idx[MAX_RF_PATH];
	s8 remnant_cck_idx;
	bool modify_txagc_flag_path_a;
	bool modify_txagc_flag_path_b;

	bool one_entry_only;
	struct dm_phy_dbg_info dbginfo;

	/* Dynamic ATC switch */
	bool atc_status;
	bool large_cfo_hit;
	bool is_freeze;
	int cfo_tail[2];
	int cfo_ave_pre;
	int crystal_cap;
	u8 cfo_threshold;
	u32 packet_count;
	u32 packet_count_pre;
	u8 tx_rate;

	/*88e tx power tracking*/
	u8	swing_idx_ofdm[MAX_RF_PATH];
	u8	swing_idx_ofdm_cur;
	u8	swing_idx_ofdm_base[MAX_RF_PATH];
	bool	swing_flag_ofdm;
	u8	swing_idx_cck;
	u8	swing_idx_cck_cur;
	u8	swing_idx_cck_base;
	bool	swing_flag_cck;

	s8	swing_diff_2g;
	s8	swing_diff_5g;

	/* DMSP */
	bool supp_phymode_switch;

	/* DulMac */
	struct fast_ant_training fat_table;

	u8	resp_tx_path;
	u8	path_sel;
	u32	patha_sum;
	u32	pathb_sum;
	u32	patha_cnt;
	u32	pathb_cnt;

	u8 pre_channel;
	u8 *p_channel;
	u8 linked_interval;

	u64 last_tx_ok_cnt;
	u64 last_rx_ok_cnt;
};

#define	EFUSE_MAX_LOGICAL_SIZE			512

struct rtl_efuse {
	const struct rtl_efuse_ops *efuse_ops;
	bool autoload_ok;
	bool bootfromefuse;
	u16 max_physical_size;

	u8 efuse_map[2][EFUSE_MAX_LOGICAL_SIZE];
	u16 efuse_usedbytes;
	u8 efuse_usedpercentage;

	u8 autoload_failflag;
	u8 autoload_status;

	short epromtype;
	u16 eeprom_vid;
	u16 eeprom_did;
	u16 eeprom_svid;
	u16 eeprom_smid;
	u8 eeprom_oemid;
	u16 eeprom_channelplan;
	u8 eeprom_version;
	u8 board_type;
	u8 external_pa;

	u8 dev_addr[6];
	u8 wowlan_enable;
	u8 antenna_div_cfg;
	u8 antenna_div_type;

	bool txpwr_fromeprom;
	u8 eeprom_crystalcap;
	u8 eeprom_tssi[2];
	u8 eeprom_tssi_5g[3][2]; /* for 5GL/5GM/5GH band. */
	u8 eeprom_pwrlimit_ht20[CHANNEL_GROUP_MAX];
	u8 eeprom_pwrlimit_ht40[CHANNEL_GROUP_MAX];
	u8 eeprom_chnlarea_txpwr_cck[MAX_RF_PATH][CHANNEL_GROUP_MAX_2G];
	u8 eeprom_chnlarea_txpwr_ht40_1s[MAX_RF_PATH][CHANNEL_GROUP_MAX];
	u8 eprom_chnl_txpwr_ht40_2sdf[MAX_RF_PATH][CHANNEL_GROUP_MAX];

	u8 internal_pa_5g[2];	/* pathA / pathB */
	u8 eeprom_c9;
	u8 eeprom_cc;

	/*For power group */
	u8 eeprom_pwrgroup[2][3];
	u8 pwrgroup_ht20[2][CHANNEL_MAX_NUMBER];
	u8 pwrgroup_ht40[2][CHANNEL_MAX_NUMBER];

	u8 txpwrlevel_cck[MAX_RF_PATH][CHANNEL_MAX_NUMBER_2G];
	/*For HT 40MHZ pwr */
	u8 txpwrlevel_ht40_1s[MAX_RF_PATH][CHANNEL_MAX_NUMBER];
	/*For HT 40MHZ pwr */
	u8 txpwrlevel_ht40_2s[MAX_RF_PATH][CHANNEL_MAX_NUMBER];

	/*--------------------------------------------------------*
	 * 8192CE\8192SE\8192DE\8723AE use the following 4 arrays,
	 * other ICs (8188EE\8723BE\8192EE\8812AE...)
	 * define new arrays in Windows code.
	 * BUT, in linux code, we use the same array for all ICs.
	 *
	 * The Correspondance relation between two arrays is:
	 * txpwr_cckdiff[][] == CCK_24G_Diff[][]
	 * txpwr_ht20diff[][] == BW20_24G_Diff[][]
	 * txpwr_ht40diff[][] == BW40_24G_Diff[][]
	 * txpwr_legacyhtdiff[][] == OFDM_24G_Diff[][]
	 *
	 * Sizes of these arrays are decided by the larger ones.
	 */
	s8 txpwr_cckdiff[MAX_RF_PATH][CHANNEL_MAX_NUMBER];
	s8 txpwr_ht20diff[MAX_RF_PATH][CHANNEL_MAX_NUMBER];
	s8 txpwr_ht40diff[MAX_RF_PATH][CHANNEL_MAX_NUMBER];
	s8 txpwr_legacyhtdiff[MAX_RF_PATH][CHANNEL_MAX_NUMBER];

	u8 txpwr_5g_bw40base[MAX_RF_PATH][CHANNEL_MAX_NUMBER];
	u8 txpwr_5g_bw80base[MAX_RF_PATH][CHANNEL_MAX_NUMBER_5G_80M];
	s8 txpwr_5g_ofdmdiff[MAX_RF_PATH][MAX_TX_COUNT];
	s8 txpwr_5g_bw20diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8 txpwr_5g_bw40diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8 txpwr_5g_bw80diff[MAX_RF_PATH][MAX_TX_COUNT];

	u8 txpwr_safetyflag;			/* Band edge enable flag */
	u16 eeprom_txpowerdiff;
	u8 legacy_httxpowerdiff;	/* Legacy to HT rate power diff */
	u8 antenna_txpwdiff[3];

	u8 eeprom_regulatory;
	u8 eeprom_thermalmeter;
	u8 thermalmeter[2]; /*ThermalMeter, index 0 for RFIC0, 1 for RFIC1 */
	u16 tssi_13dbm;
	u8 crystalcap;		/* CrystalCap. */
	u8 delta_iqk;
	u8 delta_lck;

	u8 legacy_ht_txpowerdiff;	/*Legacy to HT rate power diff */
	bool apk_thermalmeterignore;

	bool b1x1_recvcombine;
	bool b1ss_support;

	/*channel plan */
	u8 channel_plan;
};

struct rtl_efuse_ops {
	int (*efuse_onebyte_read)(struct ieee80211_hw *hw, u16 addr, u8 *data);
	void (*efuse_logical_map_read)(struct ieee80211_hw *hw, u8 type,
				       u16 offset, u32 *value);
};

struct rtl_tx_report {
	atomic_t sn;
	u16 last_sent_sn;
	unsigned long last_sent_time;
	u16 last_recv_sn;
	struct sk_buff_head queue;
};

struct rtl_ps_ctl {
	bool pwrdomain_protect;
	bool in_powersavemode;
	bool rfchange_inprogress;
	bool swrf_processing;
	bool hwradiooff;
	/* just for PCIE ASPM
	 * If it supports ASPM, Offset[560h] = 0x40,
	 * otherwise Offset[560h] = 0x00.
	 */
	bool support_aspm;
	bool support_backdoor;

	/*for LPS */
	enum rt_psmode dot11_psmode;	/*Power save mode configured. */
	bool swctrl_lps;
	bool leisure_ps;
	bool fwctrl_lps;
	u8 fwctrl_psmode;
	/*For Fw control LPS mode */
	u8 reg_fwctrl_lps;
	/*Record Fw PS mode status. */
	bool fw_current_inpsmode;
	u8 reg_max_lps_awakeintvl;
	bool report_linked;
	bool low_power_enable;/*for 32k*/

	/*for IPS */
	bool inactiveps;

	u32 rfoff_reason;

	/*RF OFF Level */
	u32 cur_ps_level;
	u32 reg_rfps_level;

	/*just for PCIE ASPM */
	u8 const_amdpci_aspm;
	bool pwrdown_mode;

	enum rf_pwrstate inactive_pwrstate;
	enum rf_pwrstate rfpwr_state;	/*cur power state */

	/* for SW LPS*/
	bool sw_ps_enabled;
	bool state;
	bool state_inap;
	bool multi_buffered;
	u16 nullfunc_seq;
	unsigned int dtim_counter;
	unsigned int sleep_ms;
	unsigned long last_sleep_jiffies;
	unsigned long last_awake_jiffies;
	unsigned long last_delaylps_stamp_jiffies;
	unsigned long last_dtim;
	unsigned long last_beacon;
	unsigned long last_action;
	unsigned long last_slept;

	/*For P2P PS */
	struct rtl_p2p_ps_info p2p_ps_info;
	u8 pwr_mode;
	u8 smart_ps;

	/* wake up on line */
	u8 wo_wlan_mode;
	u8 arp_offload_enable;
	u8 gtk_offload_enable;
	/* Used for WOL, indicates the reason for waking event.*/
	u32 wakeup_reason;
};

struct rtl_stats {
	u8 psaddr[ETH_ALEN];
	u32 mac_time[2];
	s8 rssi;
	u8 signal;
	u8 noise;
	u8 rate;		/* hw desc rate */
	u8 received_channel;
	u8 control;
	u8 mask;
	u8 freq;
	u16 len;
	u64 tsf;
	u32 beacon_time;
	u8 nic_type;
	u16 length;
	u8 signalquality;	/*in 0-100 index. */
	/* Real power in dBm for this packet,
	 * no beautification and aggregation.
	 */
	s32 recvsignalpower;
	s8 rxpower;		/*in dBm Translate from PWdB */
	u8 signalstrength;	/*in 0-100 index. */
	u16 hwerror:1;
	u16 crc:1;
	u16 icv:1;
	u16 shortpreamble:1;
	u16 antenna:1;
	u16 decrypted:1;
	u16 wakeup:1;
	u32 timestamp_low;
	u32 timestamp_high;
	bool shift;

	u8 rx_drvinfo_size;
	u8 rx_bufshift;
	bool isampdu;
	bool isfirst_ampdu;
	bool rx_is40mhzpacket;
	u8 rx_packet_bw;
	u32 rx_pwdb_all;
	u8 rx_mimo_signalstrength[4];	/*in 0~100 index */
	s8 rx_mimo_signalquality[4];
	u8 rx_mimo_evm_dbm[4];
	u16 cfo_short[4];		/* per-path's Cfo_short */
	u16 cfo_tail[4];

	s8 rx_mimo_sig_qual[4];
	u8 rx_pwr[4]; /* per-path's pwdb */
	u8 rx_snr[4]; /* per-path's SNR */
	u8 bandwidth;
	u8 bt_coex_pwr_adjust;
	bool packet_matchbssid;
	bool is_cck;
	bool is_ht;
	bool packet_toself;
	bool packet_beacon;	/*for rssi */
	s8 cck_adc_pwdb[4];	/*for rx path selection */

	bool is_vht;
	bool is_short_gi;
	u8 vht_nss;

	u8 packet_report_type;

	u32 macid;
	u32 bt_rx_rssi_percentage;
	u32 macid_valid_entry[2];
};

struct rt_link_detect {
	/* count for roaming */
	u32 bcn_rx_inperiod;
	u32 roam_times;

	u32 num_tx_in4period[4];
	u32 num_rx_in4period[4];

	u32 num_tx_inperiod;
	u32 num_rx_inperiod;

	bool busytraffic;
	bool tx_busy_traffic;
	bool rx_busy_traffic;
	bool higher_busytraffic;
	bool higher_busyrxtraffic;

	u32 tidtx_in4period[MAX_TID_COUNT][4];
	u32 tidtx_inperiod[MAX_TID_COUNT];
	bool higher_busytxtraffic[MAX_TID_COUNT];
};

struct rtl_tcb_desc {
	u8 packet_bw:2;
	u8 multicast:1;
	u8 broadcast:1;

	u8 rts_stbc:1;
	u8 rts_enable:1;
	u8 cts_enable:1;
	u8 rts_use_shortpreamble:1;
	u8 rts_use_shortgi:1;
	u8 rts_sc:1;
	u8 rts_bw:1;
	u8 rts_rate;

	u8 use_shortgi:1;
	u8 use_shortpreamble:1;
	u8 use_driver_rate:1;
	u8 disable_ratefallback:1;

	u8 use_spe_rpt:1;

	u8 ratr_index;
	u8 mac_id;
	u8 hw_rate;

	u8 last_inipkt:1;
	u8 cmd_or_init:1;
	u8 queue_index;

	/* early mode */
	u8 empkt_num;
	/* The max value by HW */
	u32 empkt_len[10];
	bool tx_enable_sw_calc_duration;
};

struct rtl_wow_pattern {
	u8 type;
	u16 crc;
	u32 mask[4];
};

/* struct to store contents of interrupt vectors */
struct rtl_int {
	u32 inta;
	u32 intb;
	u32 intc;
	u32 intd;
};

struct rtl_hal_ops {
	int (*init_sw_vars)(struct ieee80211_hw *hw);
	void (*deinit_sw_vars)(struct ieee80211_hw *hw);
	void (*read_chip_version)(struct ieee80211_hw *hw);
	void (*read_eeprom_info)(struct ieee80211_hw *hw);
	void (*interrupt_recognized)(struct ieee80211_hw *hw,
				     struct rtl_int *intvec);
	int (*hw_init)(struct ieee80211_hw *hw);
	void (*hw_disable)(struct ieee80211_hw *hw);
	void (*hw_suspend)(struct ieee80211_hw *hw);
	void (*hw_resume)(struct ieee80211_hw *hw);
	void (*enable_interrupt)(struct ieee80211_hw *hw);
	void (*disable_interrupt)(struct ieee80211_hw *hw);
	int (*set_network_type)(struct ieee80211_hw *hw,
				enum nl80211_iftype type);
	void (*set_chk_bssid)(struct ieee80211_hw *hw,
			      bool check_bssid);
	void (*set_bw_mode)(struct ieee80211_hw *hw,
			    enum nl80211_channel_type ch_type);
	 u8 (*switch_channel)(struct ieee80211_hw *hw);
	void (*set_qos)(struct ieee80211_hw *hw, int aci);
	void (*set_bcn_reg)(struct ieee80211_hw *hw);
	void (*set_bcn_intv)(struct ieee80211_hw *hw);
	void (*update_interrupt_mask)(struct ieee80211_hw *hw,
				      u32 add_msr, u32 rm_msr);
	void (*get_hw_reg)(struct ieee80211_hw *hw, u8 variable, u8 *val);
	void (*set_hw_reg)(struct ieee80211_hw *hw, u8 variable, u8 *val);
	void (*update_rate_tbl)(struct ieee80211_hw *hw,
				struct ieee80211_sta *sta, u8 rssi_leve,
				bool update_bw);
	void (*pre_fill_tx_bd_desc)(struct ieee80211_hw *hw, u8 *tx_bd_desc,
				    u8 *desc, u8 queue_index,
				    struct sk_buff *skb, dma_addr_t addr);
	void (*update_rate_mask)(struct ieee80211_hw *hw, u8 rssi_level);
	u16 (*rx_desc_buff_remained_cnt)(struct ieee80211_hw *hw,
					 u8 queue_index);
	void (*rx_check_dma_ok)(struct ieee80211_hw *hw, u8 *header_desc,
				u8 queue_index);
	void (*fill_tx_desc)(struct ieee80211_hw *hw,
			     struct ieee80211_hdr *hdr, u8 *pdesc_tx,
			     u8 *pbd_desc_tx,
			     struct ieee80211_tx_info *info,
			     struct ieee80211_sta *sta,
			     struct sk_buff *skb, u8 hw_queue,
			     struct rtl_tcb_desc *ptcb_desc);
	void (*fill_fake_txdesc)(struct ieee80211_hw *hw, u8 *pdesc,
				 u32 buffer_len, bool bsspspoll);
	void (*fill_tx_cmddesc)(struct ieee80211_hw *hw, u8 *pdesc,
				bool firstseg, bool lastseg,
				struct sk_buff *skb);
	void (*fill_tx_special_desc)(struct ieee80211_hw *hw,
				     u8 *pdesc, u8 *pbd_desc,
				     struct sk_buff *skb, u8 hw_queue);
	bool (*query_rx_desc)(struct ieee80211_hw *hw,
			      struct rtl_stats *stats,
			      struct ieee80211_rx_status *rx_status,
			      u8 *pdesc, struct sk_buff *skb);
	void (*set_channel_access)(struct ieee80211_hw *hw);
	bool (*radio_onoff_checking)(struct ieee80211_hw *hw, u8 *valid);
	void (*dm_watchdog)(struct ieee80211_hw *hw);
	void (*scan_operation_backup)(struct ieee80211_hw *hw, u8 operation);
	bool (*set_rf_power_state)(struct ieee80211_hw *hw,
				   enum rf_pwrstate rfpwr_state);
	void (*led_control)(struct ieee80211_hw *hw,
			    enum led_ctl_mode ledaction);
	void (*set_desc)(struct ieee80211_hw *hw, u8 *pdesc, bool istx,
			 u8 desc_name, u8 *val);
	u64 (*get_desc)(struct ieee80211_hw *hw, u8 *pdesc, bool istx,
			u8 desc_name);
	bool (*is_tx_desc_closed)(struct ieee80211_hw *hw,
				  u8 hw_queue, u16 index);
	void (*tx_polling)(struct ieee80211_hw *hw, u8 hw_queue);
	void (*enable_hw_sec)(struct ieee80211_hw *hw);
	void (*set_key)(struct ieee80211_hw *hw, u32 key_index,
			u8 *macaddr, bool is_group, u8 enc_algo,
			bool is_wepkey, bool clear_all);
	void (*init_sw_leds)(struct ieee80211_hw *hw);
	void (*deinit_sw_leds)(struct ieee80211_hw *hw);
	u32 (*get_bbreg)(struct ieee80211_hw *hw, u32 regaddr, u32 bitmask);
	void (*set_bbreg)(struct ieee80211_hw *hw, u32 regaddr, u32 bitmask,
			  u32 data);
	u32 (*get_rfreg)(struct ieee80211_hw *hw, enum radio_path rfpath,
			 u32 regaddr, u32 bitmask);
	void (*set_rfreg)(struct ieee80211_hw *hw, enum radio_path rfpath,
			  u32 regaddr, u32 bitmask, u32 data);
	void (*linked_set_reg)(struct ieee80211_hw *hw);
	void (*chk_switch_dmdp)(struct ieee80211_hw *hw);
	void (*dualmac_easy_concurrent)(struct ieee80211_hw *hw);
	void (*dualmac_switch_to_dmdp)(struct ieee80211_hw *hw);
	bool (*phy_rf6052_config)(struct ieee80211_hw *hw);
	void (*phy_rf6052_set_cck_txpower)(struct ieee80211_hw *hw,
					   u8 *powerlevel);
	void (*phy_rf6052_set_ofdm_txpower)(struct ieee80211_hw *hw,
					    u8 *ppowerlevel, u8 channel);
	bool (*config_bb_with_headerfile)(struct ieee80211_hw *hw,
					  u8 configtype);
	bool (*config_bb_with_pgheaderfile)(struct ieee80211_hw *hw,
					    u8 configtype);
	void (*phy_lc_calibrate)(struct ieee80211_hw *hw, bool is2t);
	void (*phy_set_bw_mode_callback)(struct ieee80211_hw *hw);
	void (*dm_dynamic_txpower)(struct ieee80211_hw *hw);
	void (*c2h_command_handle)(struct ieee80211_hw *hw);
	void (*bt_wifi_media_status_notify)(struct ieee80211_hw *hw,
					    bool mstate);
	void (*bt_coex_off_before_lps)(struct ieee80211_hw *hw);
	void (*fill_h2c_cmd)(struct ieee80211_hw *hw, u8 element_id,
			     u32 cmd_len, u8 *p_cmdbuffer);
	void (*set_default_port_id_cmd)(struct ieee80211_hw *hw);
	bool (*get_btc_status)(void);
	bool (*is_fw_header)(struct rtlwifi_firmware_header *hdr);
	void (*add_wowlan_pattern)(struct ieee80211_hw *hw,
				   struct rtl_wow_pattern *rtl_pattern,
				   u8 index);
	u16 (*get_available_desc)(struct ieee80211_hw *hw, u8 q_idx);
	void (*c2h_ra_report_handler)(struct ieee80211_hw *hw,
				      u8 *cmd_buf, u8 cmd_len);
};

struct rtl_intf_ops {
	/*com */
	void (*read_efuse_byte)(struct ieee80211_hw *hw, u16 _offset, u8 *pbuf);
	int (*adapter_start)(struct ieee80211_hw *hw);
	void (*adapter_stop)(struct ieee80211_hw *hw);
	bool (*check_buddy_priv)(struct ieee80211_hw *hw,
				 struct rtl_priv **buddy_priv);

	int (*adapter_tx)(struct ieee80211_hw *hw,
			  struct ieee80211_sta *sta,
			  struct sk_buff *skb,
			  struct rtl_tcb_desc *ptcb_desc);
	void (*flush)(struct ieee80211_hw *hw, u32 queues, bool drop);
	int (*reset_trx_ring)(struct ieee80211_hw *hw);
	bool (*waitq_insert)(struct ieee80211_hw *hw,
			     struct ieee80211_sta *sta,
			     struct sk_buff *skb);

	/*pci */
	void (*disable_aspm)(struct ieee80211_hw *hw);
	void (*enable_aspm)(struct ieee80211_hw *hw);

	/*usb */
};

struct rtl_mod_params {
	/* default: 0,0 */
	u64 debug_mask;
	/* default: 0 = using hardware encryption */
	bool sw_crypto;

	/* default: 0 = DBG_EMERG (0)*/
	int debug_level;

	/* default: 1 = using no linked power save */
	bool inactiveps;

	/* default: 1 = using linked sw power save */
	bool swctrl_lps;

	/* default: 1 = using linked fw power save */
	bool fwctrl_lps;

	/* default: 0 = not using MSI interrupts mode
	 * submodules should set their own default value
	 */
	bool msi_support;

	/* default: 0 = dma 32 */
	bool dma64;

	/* default: 1 = enable aspm */
	int aspm_support;

	/* default 0: 1 means disable */
	bool disable_watchdog;

	/* default 0: 1 means do not disable interrupts */
	bool int_clear;

	/* select antenna */
	int ant_sel;
};

struct rtl_hal_usbint_cfg {
	/* data - rx */
	u32 in_ep_num;
	u32 rx_urb_num;
	u32 rx_max_size;

	/* op - rx */
	void (*usb_rx_hdl)(struct ieee80211_hw *, struct sk_buff *);
	void (*usb_rx_segregate_hdl)(struct ieee80211_hw *, struct sk_buff *,
				     struct sk_buff_head *);

	/* tx */
	void (*usb_tx_cleanup)(struct ieee80211_hw *, struct sk_buff *);
	int (*usb_tx_post_hdl)(struct ieee80211_hw *, struct urb *,
			       struct sk_buff *);
	struct sk_buff *(*usb_tx_aggregate_hdl)(struct ieee80211_hw *,
						struct sk_buff_head *);

	/* endpoint mapping */
	int (*usb_endpoint_mapping)(struct ieee80211_hw *hw);
	u16 (*usb_mq_to_hwq)(__le16 fc, u16 mac80211_queue_index);
};

struct rtl_hal_cfg {
	u8 bar_id;
	bool write_readback;
	char *name;
	char *alt_fw_name;
	struct rtl_hal_ops *ops;
	struct rtl_mod_params *mod_params;
	struct rtl_hal_usbint_cfg *usb_interface_cfg;
	enum rtl_spec_ver spec_ver;

	/*this map used for some registers or vars
	 * defined int HAL but used in MAIN
	 */
	u32 maps[RTL_VAR_MAP_MAX];

};

struct rtl_locks {
	/* mutex */
	struct mutex conf_mutex;
	struct mutex ips_mutex;	/* mutex for enter/leave IPS */
	struct mutex lps_mutex;	/* mutex for enter/leave LPS */

	/*spin lock */
	spinlock_t irq_th_lock;
	spinlock_t h2c_lock;
	spinlock_t rf_ps_lock;
	spinlock_t rf_lock;
	spinlock_t waitq_lock;
	spinlock_t entry_list_lock;
	spinlock_t usb_lock;
	spinlock_t c2hcmd_lock;
	spinlock_t scan_list_lock; /* lock for the scan list */

	/*FW clock change */
	spinlock_t fw_ps_lock;

	/*Dual mac*/
	spinlock_t cck_and_rw_pagea_lock;

	spinlock_t iqk_lock;
};

struct rtl_works {
	struct ieee80211_hw *hw;

	/*timer */
	struct timer_list watchdog_timer;
	struct timer_list dualmac_easyconcurrent_retrytimer;
	struct timer_list fw_clockoff_timer;
	struct timer_list fast_antenna_training_timer;
	/*task */
	struct tasklet_struct irq_tasklet;
	struct tasklet_struct irq_prepare_bcn_tasklet;

	/*work queue */
	struct workqueue_struct *rtl_wq;
	struct delayed_work watchdog_wq;
	struct delayed_work ips_nic_off_wq;
	struct delayed_work c2hcmd_wq;

	/* For SW LPS */
	struct delayed_work ps_work;
	struct delayed_work ps_rfon_wq;
	struct delayed_work fwevt_wq;

	struct work_struct lps_change_work;
	struct work_struct fill_h2c_cmd;
};

struct rtl_debug {
	/* add for debug */
	struct dentry *debugfs_dir;
	char debugfs_name[20];
};

#define MIMO_PS_STATIC			0
#define MIMO_PS_DYNAMIC			1
#define MIMO_PS_NOLIMIT			3

struct rtl_dualmac_easy_concurrent_ctl {
	enum band_type currentbandtype_backfordmdp;
	bool close_bbandrf_for_dmsp;
	bool change_to_dmdp;
	bool change_to_dmsp;
	bool switch_in_process;
};

struct rtl_dmsp_ctl {
	bool activescan_for_slaveofdmsp;
	bool scan_for_anothermac_fordmsp;
	bool scan_for_itself_fordmsp;
	bool writedig_for_anothermacofdmsp;
	u32 curdigvalue_for_anothermacofdmsp;
	bool changecckpdstate_for_anothermacofdmsp;
	u8 curcckpdstate_for_anothermacofdmsp;
	bool changetxhighpowerlvl_for_anothermacofdmsp;
	u8 curtxhighlvl_for_anothermacofdmsp;
	long rssivalmin_for_anothermacofdmsp;
};

struct ps_t {
	u8 pre_ccastate;
	u8 cur_ccasate;
	u8 pre_rfstate;
	u8 cur_rfstate;
	u8 initialize;
	long rssi_val_min;
};

struct dig_t {
	u32 rssi_lowthresh;
	u32 rssi_highthresh;
	u32 fa_lowthresh;
	u32 fa_highthresh;
	long last_min_undec_pwdb_for_dm;
	long rssi_highpower_lowthresh;
	long rssi_highpower_highthresh;
	u32 recover_cnt;
	u32 pre_igvalue;
	u32 cur_igvalue;
	long rssi_val;
	u8 dig_enable_flag;
	u8 dig_ext_port_stage;
	u8 dig_algorithm;
	u8 dig_twoport_algorithm;
	u8 dig_dbgmode;
	u8 dig_slgorithm_switch;
	u8 cursta_cstate;
	u8 presta_cstate;
	u8 curmultista_cstate;
	u8 stop_dig;
	s8 back_val;
	s8 back_range_max;
	s8 back_range_min;
	u8 rx_gain_max;
	u8 rx_gain_min;
	u8 min_undec_pwdb_for_dm;
	u8 rssi_val_min;
	u8 pre_cck_cca_thres;
	u8 cur_cck_cca_thres;
	u8 pre_cck_pd_state;
	u8 cur_cck_pd_state;
	u8 pre_cck_fa_state;
	u8 cur_cck_fa_state;
	u8 pre_ccastate;
	u8 cur_ccasate;
	u8 large_fa_hit;
	u8 forbidden_igi;
	u8 dig_state;
	u8 dig_highpwrstate;
	u8 cur_sta_cstate;
	u8 pre_sta_cstate;
	u8 cur_ap_cstate;
	u8 pre_ap_cstate;
	u8 cur_pd_thstate;
	u8 pre_pd_thstate;
	u8 cur_cs_ratiostate;
	u8 pre_cs_ratiostate;
	u8 backoff_enable_flag;
	s8 backoffval_range_max;
	s8 backoffval_range_min;
	u8 dig_min_0;
	u8 dig_min_1;
	u8 bt30_cur_igi;
	bool media_connect_0;
	bool media_connect_1;

	u32 antdiv_rssi_max;
	u32 rssi_max;
};

struct rtl_global_var {
	/* from this list we can get
	 * other adapter's rtl_priv
	 */
	struct list_head glb_priv_list;
	spinlock_t glb_list_lock;
};

#define IN_4WAY_TIMEOUT_TIME	(30 * MSEC_PER_SEC)	/* 30 seconds */

struct rtl_btc_info {
	u8 bt_type;
	u8 btcoexist;
	u8 ant_num;
	u8 single_ant_path;

	u8 ap_num;
	bool in_4way;
	unsigned long in_4way_ts;
};

struct bt_coexist_info {
	struct rtl_btc_ops *btc_ops;
	struct rtl_btc_info btc_info;
	/* btc context */
	void *btc_context;
	void *wifi_only_context;
	/* EEPROM BT info. */
	u8 eeprom_bt_coexist;
	u8 eeprom_bt_type;
	u8 eeprom_bt_ant_num;
	u8 eeprom_bt_ant_isol;
	u8 eeprom_bt_radio_shared;

	u8 bt_coexistence;
	u8 bt_ant_num;
	u8 bt_coexist_type;
	u8 bt_state;
	u8 bt_cur_state;	/* 0:on, 1:off */
	u8 bt_ant_isolation;	/* 0:good, 1:bad */
	u8 bt_pape_ctrl;	/* 0:SW, 1:SW/HW dynamic */
	u8 bt_service;
	u8 bt_radio_shared_type;
	u8 bt_rfreg_origin_1e;
	u8 bt_rfreg_origin_1f;
	u8 bt_rssi_state;
	u32 ratio_tx;
	u32 ratio_pri;
	u32 bt_edca_ul;
	u32 bt_edca_dl;

	bool init_set;
	bool bt_busy_traffic;
	bool bt_traffic_mode_set;
	bool bt_non_traffic_mode_set;

	bool fw_coexist_all_off;
	bool sw_coexist_all_off;
	bool hw_coexist_all_off;
	u32 cstate;
	u32 previous_state;
	u32 cstate_h;
	u32 previous_state_h;

	u8 bt_pre_rssi_state;
	u8 bt_pre_rssi_state1;

	u8 reg_bt_iso;
	u8 reg_bt_sco;
	bool balance_on;
	u8 bt_active_zero_cnt;
	bool cur_bt_disabled;
	bool pre_bt_disabled;

	u8 bt_profile_case;
	u8 bt_profile_action;
	bool bt_busy;
	bool hold_for_bt_operation;
	u8 lps_counter;
};

struct rtl_btc_ops {
	void (*btc_init_variables)(struct rtl_priv *rtlpriv);
	void (*btc_init_variables_wifi_only)(struct rtl_priv *rtlpriv);
	void (*btc_deinit_variables)(struct rtl_priv *rtlpriv);
	void (*btc_init_hal_vars)(struct rtl_priv *rtlpriv);
	void (*btc_power_on_setting)(struct rtl_priv *rtlpriv);
	void (*btc_init_hw_config)(struct rtl_priv *rtlpriv);
	void (*btc_init_hw_config_wifi_only)(struct rtl_priv *rtlpriv);
	void (*btc_ips_notify)(struct rtl_priv *rtlpriv, u8 type);
	void (*btc_lps_notify)(struct rtl_priv *rtlpriv, u8 type);
	void (*btc_scan_notify)(struct rtl_priv *rtlpriv, u8 scantype);
	void (*btc_scan_notify_wifi_only)(struct rtl_priv *rtlpriv,
					  u8 scantype);
	void (*btc_connect_notify)(struct rtl_priv *rtlpriv, u8 action);
	void (*btc_mediastatus_notify)(struct rtl_priv *rtlpriv,
				       enum rt_media_status mstatus);
	void (*btc_periodical)(struct rtl_priv *rtlpriv);
	void (*btc_halt_notify)(struct rtl_priv *rtlpriv);
	void (*btc_btinfo_notify)(struct rtl_priv *rtlpriv,
				  u8 *tmp_buf, u8 length);
	void (*btc_btmpinfo_notify)(struct rtl_priv *rtlpriv,
				    u8 *tmp_buf, u8 length);
	bool (*btc_is_limited_dig)(struct rtl_priv *rtlpriv);
	bool (*btc_is_disable_edca_turbo)(struct rtl_priv *rtlpriv);
	bool (*btc_is_bt_disabled)(struct rtl_priv *rtlpriv);
	void (*btc_special_packet_notify)(struct rtl_priv *rtlpriv,
					  u8 pkt_type);
	void (*btc_switch_band_notify)(struct rtl_priv *rtlpriv, u8 type,
				       bool scanning);
	void (*btc_switch_band_notify_wifi_only)(struct rtl_priv *rtlpriv,
						 u8 type, bool scanning);
	void (*btc_display_bt_coex_info)(struct rtl_priv *rtlpriv,
					 struct seq_file *m);
	void (*btc_record_pwr_mode)(struct rtl_priv *rtlpriv, u8 *buf, u8 len);
	u8   (*btc_get_lps_val)(struct rtl_priv *rtlpriv);
	u8   (*btc_get_rpwm_val)(struct rtl_priv *rtlpriv);
	bool (*btc_is_bt_ctrl_lps)(struct rtl_priv *rtlpriv);
	void (*btc_get_ampdu_cfg)(struct rtl_priv *rtlpriv, u8 *reject_agg,
				  u8 *ctrl_agg_size, u8 *agg_size);
	bool (*btc_is_bt_lps_on)(struct rtl_priv *rtlpriv);
};

struct proxim {
	bool proxim_on;

	void *proximity_priv;
	int (*proxim_rx)(struct ieee80211_hw *hw, struct rtl_stats *status,
			 struct sk_buff *skb);
	u8  (*proxim_get_var)(struct ieee80211_hw *hw, u8 type);
};

struct rtl_c2hcmd {
	struct list_head list;
	u8 tag;
	u8 len;
	u8 *val;
};

struct rtl_bssid_entry {
	struct list_head list;
	u8 bssid[ETH_ALEN];
	u32 age;
};

struct rtl_scan_list {
	int num;
	struct list_head list;	/* sort by age */
};

struct rtl_priv {
	struct ieee80211_hw *hw;
	struct completion firmware_loading_complete;
	struct list_head list;
	struct rtl_priv *buddy_priv;
	struct rtl_global_var *glb_var;
	struct rtl_dualmac_easy_concurrent_ctl easy_concurrent_ctl;
	struct rtl_dmsp_ctl dmsp_ctl;
	struct rtl_locks locks;
	struct rtl_works works;
	struct rtl_mac mac80211;
	struct rtl_hal rtlhal;
	struct rtl_regulatory regd;
	struct rtl_rfkill rfkill;
	struct rtl_io io;
	struct rtl_phy phy;
	struct rtl_dm dm;
	struct rtl_security sec;
	struct rtl_efuse efuse;
	struct rtl_led_ctl ledctl;
	struct rtl_tx_report tx_report;
	struct rtl_scan_list scan_list;

	struct rtl_ps_ctl psc;
	struct rate_adaptive ra;
	struct dynamic_primary_cca primarycca;
	struct wireless_stats stats;
	struct rt_link_detect link_info;
	struct false_alarm_statistics falsealm_cnt;

	struct rtl_rate_priv *rate_priv;

	/* sta entry list for ap adhoc or mesh */
	struct list_head entry_list;

	/* c2hcmd list for kthread level access */
	struct sk_buff_head c2hcmd_queue;

	struct rtl_debug dbg;
	int max_fw_size;

	/* hal_cfg : for diff cards
	 * intf_ops : for diff interrface usb/pcie
	 */
	struct rtl_hal_cfg *cfg;
	const struct rtl_intf_ops *intf_ops;

	/* this var will be set by set_bit,
	 * and was used to indicate status of
	 * interface or hardware
	 */
	unsigned long status;

	/* tables for dm */
	struct dig_t dm_digtable;
	struct ps_t dm_pstable;

	u32 reg_874;
	u32 reg_c70;
	u32 reg_85c;
	u32 reg_a74;
	bool reg_init;	/* true if regs saved */
	bool bt_operation_on;
	__le32 *usb_data;
	int usb_data_index;
	bool initialized;
	bool enter_ps;	/* true when entering PS */
	u8 rate_mask[5];

	/* intel Proximity, should be alloc mem
	 * in intel Proximity module and can only
	 * be used in intel Proximity mode
	 */
	struct proxim proximity;

	/*for bt coexist use*/
	struct bt_coexist_info btcoexist;

	/* separate 92ee from other ICs,
	 * 92ee use new trx flow.
	 */
	bool use_new_trx_flow;

#ifdef CONFIG_PM
	struct wiphy_wowlan_support wowlan;
#endif
	/* This must be the last item so
	 * that it points to the data allocated
	 * beyond  this structure like:
	 * rtl_pci_priv or rtl_usb_priv
	 */
	u8 priv[0] __aligned(sizeof(void *));
};

#define rtl_priv(hw)		(((struct rtl_priv *)(hw)->priv))
#define rtl_mac(rtlpriv)	(&((rtlpriv)->mac80211))
#define rtl_hal(rtlpriv)	(&((rtlpriv)->rtlhal))
#define rtl_efuse(rtlpriv)	(&((rtlpriv)->efuse))
#define rtl_psc(rtlpriv)	(&((rtlpriv)->psc))

/* Bluetooth Co-existence Related */

enum bt_ant_num {
	ANT_X2 = 0,
	ANT_X1 = 1,
};

enum bt_ant_path {
	ANT_MAIN = 0,
	ANT_AUX = 1,
};

enum bt_co_type {
	BT_2WIRE = 0,
	BT_ISSC_3WIRE = 1,
	BT_ACCEL = 2,
	BT_CSR_BC4 = 3,
	BT_CSR_BC8 = 4,
	BT_RTL8756 = 5,
	BT_RTL8723A = 6,
	BT_RTL8821A = 7,
	BT_RTL8723B = 8,
	BT_RTL8192E = 9,
	BT_RTL8812A = 11,
};

enum bt_cur_state {
	BT_OFF = 0,
	BT_ON = 1,
};

enum bt_service_type {
	BT_SCO = 0,
	BT_A2DP = 1,
	BT_HID = 2,
	BT_HID_IDLE = 3,
	BT_SCAN = 4,
	BT_IDLE = 5,
	BT_OTHER_ACTION = 6,
	BT_BUSY = 7,
	BT_OTHERBUSY = 8,
	BT_PAN = 9,
};

enum bt_radio_shared {
	BT_RADIO_SHARED = 0,
	BT_RADIO_INDIVIDUAL = 1,
};

/****************************************
 *	mem access macro define start
 *	Call endian free function when
 *	1. Read/write packet content.
 *	2. Before write integer to IO.
 *	3. After read integer from IO.
 ****************************************/
/* Convert little data endian to host ordering */
#define EF1BYTE(_val)		\
	((u8)(_val))
#define EF2BYTE(_val)		\
	(le16_to_cpu(_val))
#define EF4BYTE(_val)		\
	(le32_to_cpu(_val))

/* Read data from memory */
#define READEF1BYTE(_ptr)      \
	EF1BYTE(*((u8 *)(_ptr)))
/* Read le16 data from memory and convert to host ordering */
#define READEF2BYTE(_ptr)      \
	EF2BYTE(*(_ptr))
#define READEF4BYTE(_ptr)      \
	EF4BYTE(*(_ptr))

/* Create a bit mask
 * Examples:
 * BIT_LEN_MASK_32(0) => 0x00000000
 * BIT_LEN_MASK_32(1) => 0x00000001
 * BIT_LEN_MASK_32(2) => 0x00000003
 * BIT_LEN_MASK_32(32) => 0xFFFFFFFF
 */
#define BIT_LEN_MASK_32(__bitlen)	 \
	(0xFFFFFFFF >> (32 - (__bitlen)))
#define BIT_LEN_MASK_16(__bitlen)	 \
	(0xFFFF >> (16 - (__bitlen)))
#define BIT_LEN_MASK_8(__bitlen) \
	(0xFF >> (8 - (__bitlen)))

/* Create an offset bit mask
 * Examples:
 * BIT_OFFSET_LEN_MASK_32(0, 2) => 0x00000003
 * BIT_OFFSET_LEN_MASK_32(16, 2) => 0x00030000
 */
#define BIT_OFFSET_LEN_MASK_32(__bitoffset, __bitlen) \
	(BIT_LEN_MASK_32(__bitlen) << (__bitoffset))
#define BIT_OFFSET_LEN_MASK_16(__bitoffset, __bitlen) \
	(BIT_LEN_MASK_16(__bitlen) << (__bitoffset))
#define BIT_OFFSET_LEN_MASK_8(__bitoffset, __bitlen) \
	(BIT_LEN_MASK_8(__bitlen) << (__bitoffset))

/*Description:
 * Return 4-byte value in host byte ordering from
 * 4-byte pointer in little-endian system.
 */
#define LE_P4BYTE_TO_HOST_4BYTE(__pstart) \
	(EF4BYTE(*((__le32 *)(__pstart))))
#define LE_P2BYTE_TO_HOST_2BYTE(__pstart) \
	(EF2BYTE(*((__le16 *)(__pstart))))
#define LE_P1BYTE_TO_HOST_1BYTE(__pstart) \
	(EF1BYTE(*((u8 *)(__pstart))))

/*Description:
 * Translate subfield (continuous bits in little-endian) of 4-byte
 * value to host byte ordering.
 */
#define LE_BITS_TO_4BYTE(__pstart, __bitoffset, __bitlen) \
	( \
		(LE_P4BYTE_TO_HOST_4BYTE(__pstart) >> (__bitoffset))  & \
		BIT_LEN_MASK_32(__bitlen) \
	)
#define LE_BITS_TO_2BYTE(__pstart, __bitoffset, __bitlen) \
	( \
		(LE_P2BYTE_TO_HOST_2BYTE(__pstart) >> (__bitoffset)) & \
		BIT_LEN_MASK_16(__bitlen) \
	)
#define LE_BITS_TO_1BYTE(__pstart, __bitoffset, __bitlen) \
	( \
		(LE_P1BYTE_TO_HOST_1BYTE(__pstart) >> (__bitoffset)) & \
		BIT_LEN_MASK_8(__bitlen) \
	)

/* Description:
 * Mask subfield (continuous bits in little-endian) of 4-byte value
 * and return the result in 4-byte value in host byte ordering.
 */
#define LE_BITS_CLEARED_TO_4BYTE(__pstart, __bitoffset, __bitlen) \
	( \
		LE_P4BYTE_TO_HOST_4BYTE(__pstart)  & \
		(~BIT_OFFSET_LEN_MASK_32(__bitoffset, __bitlen)) \
	)
#define LE_BITS_CLEARED_TO_2BYTE(__pstart, __bitoffset, __bitlen) \
	( \
		LE_P2BYTE_TO_HOST_2BYTE(__pstart) & \
		(~BIT_OFFSET_LEN_MASK_16(__bitoffset, __bitlen)) \
	)
#define LE_BITS_CLEARED_TO_1BYTE(__pstart, __bitoffset, __bitlen) \
	( \
		LE_P1BYTE_TO_HOST_1BYTE(__pstart) & \
		(~BIT_OFFSET_LEN_MASK_8(__bitoffset, __bitlen)) \
	)

/* Description:
 * Set subfield of little-endian 4-byte value to specified value.
 */
#define SET_BITS_TO_LE_4BYTE(__pstart, __bitoffset, __bitlen, __val) \
	*((__le32 *)(__pstart)) = \
	cpu_to_le32( \
		LE_BITS_CLEARED_TO_4BYTE(__pstart, __bitoffset, __bitlen) | \
		((((u32)__val) & BIT_LEN_MASK_32(__bitlen)) << (__bitoffset)) \
	)
#define SET_BITS_TO_LE_2BYTE(__pstart, __bitoffset, __bitlen, __val) \
	*((__le16 *)(__pstart)) = \
	cpu_to_le16( \
		LE_BITS_CLEARED_TO_2BYTE(__pstart, __bitoffset, __bitlen) | \
		((((u16)__val) & BIT_LEN_MASK_16(__bitlen)) << (__bitoffset)) \
	)
#define SET_BITS_TO_LE_1BYTE(__pstart, __bitoffset, __bitlen, __val) \
	*((u8 *)(__pstart)) = EF1BYTE \
	( \
		LE_BITS_CLEARED_TO_1BYTE(__pstart, __bitoffset, __bitlen) | \
		((((u8)__val) & BIT_LEN_MASK_8(__bitlen)) << (__bitoffset)) \
	)

#define	N_BYTE_ALIGMENT(__value, __aligment) ((__aligment == 1) ? \
	(__value) : (((__value + __aligment - 1) / __aligment) * __aligment))

/* mem access macro define end */

#define byte(x, n) ((x >> (8 * n)) & 0xff)

#define packet_get_type(_packet) (EF1BYTE((_packet).octet[0]) & 0xFC)
#define RTL_WATCH_DOG_TIME	2000
#define MSECS(t)		msecs_to_jiffies(t)
#define WLAN_FC_GET_VERS(fc)	(le16_to_cpu(fc) & IEEE80211_FCTL_VERS)
#define WLAN_FC_GET_TYPE(fc)	(le16_to_cpu(fc) & IEEE80211_FCTL_FTYPE)
#define WLAN_FC_GET_STYPE(fc)	(le16_to_cpu(fc) & IEEE80211_FCTL_STYPE)
#define WLAN_FC_MORE_DATA(fc)	(le16_to_cpu(fc) & IEEE80211_FCTL_MOREDATA)
#define rtl_dm(rtlpriv)		(&((rtlpriv)->dm))

#define	RT_RF_OFF_LEVL_ASPM		BIT(0)	/*PCI ASPM */
#define	RT_RF_OFF_LEVL_CLK_REQ		BIT(1)	/*PCI clock request */
#define	RT_RF_OFF_LEVL_PCI_D3		BIT(2)	/*PCI D3 mode */
/*NIC halt, re-initialize hw parameters*/
#define	RT_RF_OFF_LEVL_HALT_NIC		BIT(3)
#define	RT_RF_OFF_LEVL_FREE_FW		BIT(4)	/*FW free, re-download the FW */
#define	RT_RF_OFF_LEVL_FW_32K		BIT(5)	/*FW in 32k */
/*Always enable ASPM and Clock Req in initialization.*/
#define	RT_RF_PS_LEVEL_ALWAYS_ASPM	BIT(6)
/* no matter RFOFF or SLEEP we set PS_ASPM_LEVL*/
#define	RT_PS_LEVEL_ASPM		BIT(7)
/*When LPS is on, disable 2R if no packet is received or transmittd.*/
#define	RT_RF_LPS_DISALBE_2R		BIT(30)
#define	RT_RF_LPS_LEVEL_ASPM		BIT(31)	/*LPS with ASPM */
#define	RT_IN_PS_LEVEL(ppsc, _ps_flg)		\
	((ppsc->cur_ps_level & _ps_flg) ? true : false)
#define	RT_CLEAR_PS_LEVEL(ppsc, _ps_flg)	\
	(ppsc->cur_ps_level &= (~(_ps_flg)))
#define	RT_SET_PS_LEVEL(ppsc, _ps_flg)		\
	(ppsc->cur_ps_level |= _ps_flg)

#define container_of_dwork_rtl(x, y, z) \
	container_of(to_delayed_work(x), y, z)

#define FILL_OCTET_STRING(_os, _octet, _len)	\
		(_os).octet = (u8 *)(_octet);		\
		(_os).length = (_len);

#define CP_MACADDR(des, src)	\
	((des)[0] = (src)[0], (des)[1] = (src)[1],\
	(des)[2] = (src)[2], (des)[3] = (src)[3],\
	(des)[4] = (src)[4], (des)[5] = (src)[5])

#define	LDPC_HT_ENABLE_RX			BIT(0)
#define	LDPC_HT_ENABLE_TX			BIT(1)
#define	LDPC_HT_TEST_TX_ENABLE			BIT(2)
#define	LDPC_HT_CAP_TX				BIT(3)

#define	STBC_HT_ENABLE_RX			BIT(0)
#define	STBC_HT_ENABLE_TX			BIT(1)
#define	STBC_HT_TEST_TX_ENABLE			BIT(2)
#define	STBC_HT_CAP_TX				BIT(3)

#define	LDPC_VHT_ENABLE_RX			BIT(0)
#define	LDPC_VHT_ENABLE_TX			BIT(1)
#define	LDPC_VHT_TEST_TX_ENABLE			BIT(2)
#define	LDPC_VHT_CAP_TX				BIT(3)

#define	STBC_VHT_ENABLE_RX			BIT(0)
#define	STBC_VHT_ENABLE_TX			BIT(1)
#define	STBC_VHT_TEST_TX_ENABLE			BIT(2)
#define	STBC_VHT_CAP_TX				BIT(3)

extern u8 channel5g[CHANNEL_MAX_NUMBER_5G];

extern u8 channel5g_80m[CHANNEL_MAX_NUMBER_5G_80M];

static inline u8 rtl_read_byte(struct rtl_priv *rtlpriv, u32 addr)
{
	return rtlpriv->io.read8_sync(rtlpriv, addr);
}

static inline u16 rtl_read_word(struct rtl_priv *rtlpriv, u32 addr)
{
	return rtlpriv->io.read16_sync(rtlpriv, addr);
}

static inline u32 rtl_read_dword(struct rtl_priv *rtlpriv, u32 addr)
{
	return rtlpriv->io.read32_sync(rtlpriv, addr);
}

static inline void rtl_write_byte(struct rtl_priv *rtlpriv, u32 addr, u8 val8)
{
	rtlpriv->io.write8_async(rtlpriv, addr, val8);

	if (rtlpriv->cfg->write_readback)
		rtlpriv->io.read8_sync(rtlpriv, addr);
}

static inline void rtl_write_byte_with_val32(struct ieee80211_hw *hw,
					     u32 addr, u32 val8)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, addr, (u8)val8);
}

static inline void rtl_write_word(struct rtl_priv *rtlpriv, u32 addr, u16 val16)
{
	rtlpriv->io.write16_async(rtlpriv, addr, val16);

	if (rtlpriv->cfg->write_readback)
		rtlpriv->io.read16_sync(rtlpriv, addr);
}

static inline void rtl_write_dword(struct rtl_priv *rtlpriv,
				   u32 addr, u32 val32)
{
	rtlpriv->io.write32_async(rtlpriv, addr, val32);

	if (rtlpriv->cfg->write_readback)
		rtlpriv->io.read32_sync(rtlpriv, addr);
}

static inline u32 rtl_get_bbreg(struct ieee80211_hw *hw,
				u32 regaddr, u32 bitmask)
{
	struct rtl_priv *rtlpriv = hw->priv;

	return rtlpriv->cfg->ops->get_bbreg(hw, regaddr, bitmask);
}

static inline void rtl_set_bbreg(struct ieee80211_hw *hw, u32 regaddr,
				 u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = hw->priv;

	rtlpriv->cfg->ops->set_bbreg(hw, regaddr, bitmask, data);
}

static inline void rtl_set_bbreg_with_dwmask(struct ieee80211_hw *hw,
					     u32 regaddr, u32 data)
{
	rtl_set_bbreg(hw, regaddr, 0xffffffff, data);
}

static inline u32 rtl_get_rfreg(struct ieee80211_hw *hw,
				enum radio_path rfpath, u32 regaddr,
				u32 bitmask)
{
	struct rtl_priv *rtlpriv = hw->priv;

	return rtlpriv->cfg->ops->get_rfreg(hw, rfpath, regaddr, bitmask);
}

static inline void rtl_set_rfreg(struct ieee80211_hw *hw,
				 enum radio_path rfpath, u32 regaddr,
				 u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = hw->priv;

	rtlpriv->cfg->ops->set_rfreg(hw, rfpath, regaddr, bitmask, data);
}

static inline bool is_hal_stop(struct rtl_hal *rtlhal)
{
	return (_HAL_STATE_STOP == rtlhal->state);
}

static inline void set_hal_start(struct rtl_hal *rtlhal)
{
	rtlhal->state = _HAL_STATE_START;
}

static inline void set_hal_stop(struct rtl_hal *rtlhal)
{
	rtlhal->state = _HAL_STATE_STOP;
}

static inline u8 get_rf_type(struct rtl_phy *rtlphy)
{
	return rtlphy->rf_type;
}

static inline struct ieee80211_hdr *rtl_get_hdr(struct sk_buff *skb)
{
	return (struct ieee80211_hdr *)(skb->data);
}

static inline __le16 rtl_get_fc(struct sk_buff *skb)
{
	return rtl_get_hdr(skb)->frame_control;
}

static inline u16 rtl_get_tid_h(struct ieee80211_hdr *hdr)
{
	return (ieee80211_get_qos_ctl(hdr))[0] & IEEE80211_QOS_CTL_TID_MASK;
}

static inline u16 rtl_get_tid(struct sk_buff *skb)
{
	return rtl_get_tid_h(rtl_get_hdr(skb));
}

static inline struct ieee80211_sta *get_sta(struct ieee80211_hw *hw,
					    struct ieee80211_vif *vif,
					    const u8 *bssid)
{
	return ieee80211_find_sta(vif, bssid);
}

static inline struct ieee80211_sta *rtl_find_sta(struct ieee80211_hw *hw,
						 u8 *mac_addr)
{
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));

	return ieee80211_find_sta(mac->vif, mac_addr);
}

#endif
