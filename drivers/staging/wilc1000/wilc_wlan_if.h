/* ///////////////////////////////////////////////////////////////////////// */
/*  */
/* Copyright (c) Atmel Corporation.  All rights reserved. */
/*  */
/* Module Name:  wilc_wlan_if.h */
/*  */
/*  */
/* ///////////////////////////////////////////////////////////////////////// */

#ifndef WILC_WLAN_IF_H
#define WILC_WLAN_IF_H

#include <linux/netdevice.h>

/********************************************
 *
 *      Host Interface Defines
 *
 ********************************************/

#define HIF_SDIO		(0)
#define HIF_SPI			BIT(0)
#define HIF_SDIO_GPIO_IRQ	BIT(2)

/********************************************
 *
 *      Wlan Interface Defines
 *
 ********************************************/

struct sdio_cmd52 {
	u32 read_write:		1;
	u32 function:		3;
	u32 raw:		1;
	u32 address:		17;
	u32 data:		8;
};

struct sdio_cmd53 {
	u32 read_write:		1;
	u32 function:		3;
	u32 block_mode:		1;
	u32 increment:		1;
	u32 address:		17;
	u32 count:		9;
	u8 *buffer;
	u32 block_size;
};

#define WILC_MAC_INDICATE_STATUS	0x1
#define WILC_MAC_STATUS_INIT		-1
#define WILC_MAC_STATUS_READY		0
#define WILC_MAC_STATUS_CONNECT		1

#define WILC_MAC_INDICATE_SCAN		0x2

struct tx_complete_data {
	int size;
	void *buff;
	u8 *bssid;
	struct sk_buff *skb;
};

typedef void (*wilc_tx_complete_func_t)(void *, int);

#define WILC_TX_ERR_NO_BUF	(-2)

/********************************************
 *
 *      Wlan Configuration ID
 *
 ********************************************/
#define WILC_MULTICAST_TABLE_SIZE	8
#define MAX_SSID_LEN            33
#define MAX_RATES_SUPPORTED     12

typedef enum {
	SUPP_RATES_IE		= 1,
	EXT_SUPP_RATES_IE	= 50,
	HT_CAPABILITY_IE	= 45,
	RSN_IE			= 48,
	WPA_IE			= 221,
	WMM_IE			= 221,
	P2P_IE			= 221,
} BEACON_IE;

typedef enum {
	INFRASTRUCTURE		= 0,
	INDEPENDENT,
	AP,
} BSSTYPE_T;

typedef enum {
	RATE_AUTO		= 0,
	RATE_1MB		= 1,
	RATE_2MB		= 2,
	RATE_5MB		= 5,
	RATE_6MB		= 6,
	RATE_9MB		= 9,
	RATE_11MB		= 11,
	RATE_12MB		= 12,
	RATE_18MB		= 18,
	RATE_24MB		= 24,
	RATE_26MB		= 36,
	RATE_48MB		= 48,
	RATE_54MB		= 54
} TX_RATE_T;

typedef enum {
	B_ONLY_MODE		= 0,    /* 1, 2 M, otherwise 5, 11 M */
	G_ONLY_MODE,			/* 6,12,24 otherwise 9,18,36,48,54 */
	G_MIXED_11B_1_MODE,		/* 1,2,5.5,11 otherwise all on */
	G_MIXED_11B_2_MODE,		/* 1,2,5,11,6,12,24 otherwise all on */
} G_OPERATING_MODE_T;

typedef enum {
	G_SHORT_PREAMBLE	= 0,	/* Short Preamble */
	G_LONG_PREAMBLE		= 1,	/* Long Preamble */
	G_AUTO_PREAMBLE		= 2,	/* Auto Preamble Selection */
} G_PREAMBLE_T;

#define MAC_CONNECTED		1
#define MAC_DISCONNECTED	0

#define SCAN_DONE		TRUE
typedef enum {
	PASSIVE_SCAN		= 0,
	ACTIVE_SCAN		= 1,
} SCANTYPE_T;

typedef enum {
	NO_POWERSAVE		= 0,
	MIN_FAST_PS		= 1,
	MAX_FAST_PS		= 2,
	MIN_PSPOLL_PS		= 3,
	MAX_PSPOLL_PS		= 4
} USER_PS_MODE_T;

typedef enum {
	CHIP_WAKEDUP		= 0,
	CHIP_SLEEPING_AUTO      = 1,
	CHIP_SLEEPING_MANUAL	= 2
} CHIP_PS_STATE_T;

typedef enum {
	ACQUIRE_ONLY            = 0,
	ACQUIRE_AND_WAKEUP	= 1,
} BUS_ACQUIRE_T;

typedef enum {
	RELEASE_ONLY		= 0,
	RELEASE_ALLOW_SLEEP	= 1,
} BUS_RELEASE_T;

typedef enum {
	NO_SECURITY		= 0,
	WEP_40			= 0x3,
	WEP_104			= 0x7,
	WPA_AES			= 0x29,
	WPA_TKIP		= 0x49,
	WPA_AES_TKIP		= 0x69,	/* Aes or Tkip */
	WPA2_AES		= 0x31,
	WPA2_TKIP		= 0x51,
	WPA2_AES_TKIP		= 0x71,	/* Aes or Tkip */
} SECURITY_T;

enum AUTHTYPE {
	OPEN_SYSTEM		= 1,
	SHARED_KEY		= 2,
	ANY			= 3,
	IEEE8021		= 5
};

enum SITESURVEY {
	SITE_SURVEY_1CH		= 0,
	SITE_SURVEY_ALL_CH	= 1,
	SITE_SURVEY_OFF		= 2
};

typedef enum {
	NORMAL_ACK		= 0,
	NO_ACK,
} ACK_POLICY_T;

typedef enum {
	DONT_RESET		= 0,
	DO_RESET		= 1,
	NO_REQUEST		= 2,
} RESET_REQ_T;

typedef enum {
	REKEY_DISABLE		= 1,
	REKEY_TIME_BASE,
	REKEY_PKT_BASE,
	REKEY_TIME_PKT_BASE
} RSNA_REKEY_POLICY_T;

typedef enum {
	FILTER_NO		= 0x00,
	FILTER_AP_ONLY		= 0x01,
	FILTER_STA_ONLY		= 0x02
} SCAN_CLASS_FITLER_T;

typedef enum {
	PRI_HIGH_RSSI		= 0x00,
	PRI_LOW_RSSI		= 0x04,
	PRI_DETECT		= 0x08
} SCAN_PRI_T;

typedef enum {
	CH_FILTER_OFF		= 0x00,
	CH_FILTER_ON		= 0x10
} CH_FILTER_T;

typedef enum {
	AUTO_PROT		= 0,	/* Auto */
	NO_PROT,			/* Do not use any protection */
	ERP_PROT,			/* Protect all ERP frame exchanges */
	HT_PROT,			/* Protect all HT frame exchanges  */
	GF_PROT,			/* Protect all GF frame exchanges  */
} N_PROTECTION_MODE_T;

typedef enum {
	G_SELF_CTS_PROT,
	G_RTS_CTS_PROT,
} G_PROTECTION_MODE_T;

typedef enum {
	HT_MIXED_MODE		= 1,
	HT_ONLY_20MHZ_MODE,
	HT_ONLY_20_40MHZ_MODE,
} N_OPERATING_MODE_T;

typedef enum {
	NO_DETECT		= 0,
	DETECT_ONLY		= 1,
	DETECT_PROTECT		= 2,
	DETECT_PROTECT_REPORT	= 3,
} N_OBSS_DETECTION_T;

typedef enum {
	RTS_CTS_NONHT_PROT	= 0,	/* RTS-CTS at non-HT rate */
	FIRST_FRAME_NONHT_PROT,		/* First frame at non-HT rate */
	LSIG_TXOP_PROT,                 /* LSIG TXOP Protection */
	FIRST_FRAME_MIXED_PROT,		/* First frame at Mixed format */
} N_PROTECTION_TYPE_T;

typedef enum {
	STATIC_MODE		= 1,
	DYNAMIC_MODE		= 2,
	MIMO_MODE		= 3,	/* power save disable */
} N_SMPS_MODE_T;

typedef enum {
	DISABLE_SELF_CTS,
	ENABLE_SELF_CTS,
	DISABLE_TX_ABORT,
	ENABLE_TX_ABORT,
	HW_TRIGGER_ABORT,
	SW_TRIGGER_ABORT,
} TX_ABORT_OPTION_T;

enum wid_type {
	WID_CHAR		= 0,
	WID_SHORT		= 1,
	WID_INT			= 2,
	WID_STR			= 3,
	WID_BIN_DATA		= 4,
	WID_BIN			= 5,
	WID_IP			= 6,
	WID_ADR			= 7,
	WID_UNDEF		= 8,
	WID_TYPE_FORCE_32BIT	= 0xFFFFFFFF
};

struct wid {
	u16 id;
	enum wid_type type;
	s32 size;
	s8 *val;
};

typedef enum {
	WID_NIL				= 0xffff,

	/*
	 *  BSS Type
	 *  -----------------------------------------------------------
	 *  Configuration : Infrastructure   Independent   Access Point
	 *  Values to set :         0               1            2
	 *  -----------------------------------------------------------
	 */
	WID_BSS_TYPE			= 0x0000,

	/*
	 *  Transmit Rate
	 *  -----------------------------------------------------------
	 *  Configuration : 1  2  5.5  11  6  9  12  18  24  36  48  54
	 *  Values to set : 1  2    5  11  6  9  12  18  24  36  48  54
	 *  -----------------------------------------------------------
	 */
	WID_CURRENT_TX_RATE		= 0x0001,

	/*
	 *  Channel
	 *  -----------------------------------------------------------
	 *  Configuration(g) : 1  2  3  4  5  6  7  8  9 10 11 12 13 14
	 *  Values to set    : 1  2  3  4  5  6  7  8  9 10 11 12 13 14
	 *  -----------------------------------------------------------
	 */
	WID_CURRENT_CHANNEL		= 0x0002,

	/*
	 *  Preamble
	 *  -----------------------------------------------------------
	 *  Configuration :    short    long      Auto
	 *  Values to set :       0       1         2
	 *  -----------------------------------------------------------
	 */
	WID_PREAMBLE			= 0x0003,

	/*
	 * 11g operating mode (ignored if 11g not present)
	 *  -----------------------------------------------------------
	 *  Configuration :   HighPerf  Compat(RSet #1) Compat(RSet #2)
	 *  Values to set :          1               2               3
	 *  -----------------------------------------------------------
	 */
	WID_11G_OPERATING_MODE		= 0x0004,

	/*
	 *  Mac status (response only)
	 *  -----------------------------------------------------------
	 *  Configuration :   disconnect  connect
	 *  Values to get :          0       1
	 *  -----------------------------------------------------------
	 */
	WID_STATUS			= 0x0005,

	/*
	 *  Scan type
	 *  -----------------------------------------------------------
	 *  Configuration :   Passive Scanning   Active Scanning
	 *  Values to set :                  0                 1
	 *  -----------------------------------------------------------
	 */
	WID_SCAN_TYPE			= 0x0007,

	/*
	 *  Key Id (WEP default key Id)
	 *  -----------------------------------------------------------
	 *  Configuration :   Any value between 0 to 3
	 *  Values to set :   Same value. Default is 0
	 *  -----------------------------------------------------------
	 */
	WID_KEY_ID			= 0x0009,

	/*
	 *  QoS Enable
	 *  -----------------------------------------------------------
	 *  Configuration :   QoS Disable   WMM Enable
	 *  Values to set :   0             1
	 *  -----------------------------------------------------------
	 */
	WID_QOS_ENABLE			= 0x000A,

	/*
	 *  Power Management
	 *  -----------------------------------------------------------
	 *  Configuration : NO_POWERSAVE MIN_POWERSAVE MAX_POWERSAVE
	 *  Values to set : 0            1             2
	 *  -----------------------------------------------------------
	 */
	WID_POWER_MANAGEMENT		= 0x000B,

	/*
	 *  WEP/802 11I Configuration
	 *  -----------------------------------------------------------
	 *  Configuration:Disable WP40 WP104 WPA-AES WPA-TKIP RSN-AES RSN-TKIP
	 *  Values (0x)  :   00     03   07     29       49       31      51
	 *  Configuration:WPA-AES+TKIP RSN-AES+TKIP
	 *  Values (0x)  :      69        71
	 *  -----------------------------------------------------------
	 */
	WID_11I_MODE			= 0x000C,

	/*
	 *  WEP Configuration: Used in BSS STA mode only when WEP is enabled
	 *  -----------------------------------------------------------
	 *  Configuration : Open System Shared Key Any Type | 802.1x Auth
	 *  Values (0x)   :    01             02         03 |    BIT2
	 *  -----------------------------------------------------------
	 */
	WID_AUTH_TYPE			= 0x000D,

	/*
	 *  Site Survey Type
	 *  -----------------------------------------------------------
	 *  Configuration       :  Values to set
	 *  Survey 1 Channel    :  0
	 *  survey all Channels :  1
	 *  Disable Site Survey :  2
	 *  -----------------------------------------------------------
	 */
	WID_SITE_SURVEY			= 0x000E,

	/*
	 *  Listen Interval
	 *  -----------------------------------------------------------
	 *  Configuration :   Any value between 1 to 255
	 *  Values to set :   Same value. Default is 3
	 *  -----------------------------------------------------------
	 */
	WID_LISTEN_INTERVAL		= 0x000F,

	/*
	 *  DTIM Period
	 *  -----------------------------------------------------------
	 *  Configuration :   Any value between 1 to 255
	 *  Values to set :   Same value. Default is 3
	 *  -----------------------------------------------------------
	 */
	WID_DTIM_PERIOD			= 0x0010,

	/*
	 *  ACK Policy
	 *  -----------------------------------------------------------
	 *  Configuration :   Normal Ack            No Ack
	 *  Values to set :       0                   1
	 *  -----------------------------------------------------------
	 */
	WID_ACK_POLICY			= 0x0011,

	/*
	 *  Reset MAC (Set only)
	 *  -----------------------------------------------------------
	 *  Configuration :   Don't Reset	Reset	No Request
	 *  Values to set :       0               1	    2
	 *  -----------------------------------------------------------
	 */
	WID_RESET			= 0x0012,

	/*
	 *  Broadcast SSID Option: Setting this will adhere to "" SSID element
	 *  -----------------------------------------------------------
	 *  Configuration :   Enable             Disable
	 *  Values to set :   1                  0
	 *  -----------------------------------------------------------
	 */
	WID_BCAST_SSID			= 0x0015,

	/*
	 *  Disconnect (Station)
	 *  -----------------------------------------------------------
	 *  Configuration :   Association ID
	 *  Values to set :   Association ID
	 *  -----------------------------------------------------------
	 */
	WID_DISCONNECT			= 0x0016,

	/*
	 *  11a Tx Power Level
	 *  -----------------------------------------------------------
	 *  Configuration : Sets TX Power (Higher the value greater the power)
	 *  Values to set : Any value between 0 and 63 (inclusive Default 48)
	 *  -----------------------------------------------------------
	 */
	WID_TX_POWER_LEVEL_11A		= 0x0018,

	/*
	 *  Group Key Update Policy Selection
	 *  -----------------------------------------------------------
	 *  Configuration : Disabled timeBased packetBased timePacketBased
	 *  Values to set :   1            2          3              4
	 *  -----------------------------------------------------------
	 */
	WID_REKEY_POLICY		= 0x0019,

	/*
	 *  Allow Short Slot
	 *  -----------------------------------------------------------
	 *  Configuration : Disallow Short Slot      Allow Short Slot
	 *          (Enable Only Long Slot) (Enable Short Slot if applicable)
	 *  Values to set :    0         1
	 *  -----------------------------------------------------------
	 */
	WID_SHORT_SLOT_ALLOWED		= 0x001A,

	WID_PHY_ACTIVE_REG		= 0x001B,

	/*
	 *  11b Tx Power Level
	 *  -----------------------------------------------------------
	 *  Configuration : Sets TX Power (Higher the value greater the power)
	 *  Values to set : Any value between 0 and 63 (inclusive Default 48)
	 *  -----------------------------------------------------------
	 */
	WID_TX_POWER_LEVEL_11B		= 0x001D,

	/*
	 *  Scan Request
	 *  -----------------------------------------------------------
	 *  Configuration : Request default scan
	 *  Values to set : 0
	 *  -----------------------------------------------------------
	 */
	WID_START_SCAN_REQ		= 0x001E,

	/*
	 *  Rssi (get only)
	 *  -----------------------------------------------------------
	 *  Configuration :
	 *  Values to get : Rssi value
	 *  -----------------------------------------------------------
	 */
	WID_RSSI			= 0x001F,

	/*
	 * Join Request
	 *  -----------------------------------------------------------
	 *  Configuration : Request to join
	 *  Values to set : index of scan result
	 *  -----------------------------------------------------------
	 */
	WID_JOIN_REQ			= 0x0020,

	WID_LINKSPEED			= 0x0026,

	/*
	 *  Enable User Control of TX Power
	 *  -----------------------------------------------------------
	 *  Configuration : Disable                  Enable
	 *  Values to set :    0                       1
	 *  -----------------------------------------------------------
	 */
	WID_USER_CONTROL_ON_TX_POWER	= 0x0027,

	WID_MEMORY_ACCESS_8BIT		= 0x0029,

	/*
	 *  Enable Auto RX Sensitivity feature
	 *  -----------------------------------------------------------
	 *  Configuration : Disable                  Enable
	 *  Values to set :    0                       1
	 *  -----------------------------------------------------------
	 */
	WID_AUTO_RX_SENSITIVITY		= 0x0032,

	/*
	 *  Receive Buffer Based Ack
	 *  -----------------------------------------------------------
	 *  Configuration : Disable                  Enable
	 *  Values to set :    0                       1
	 *  -----------------------------------------------------------
	 */
	WID_DATAFLOW_CONTROL		= 0x0033,

	/*
	 *  Scan Filter
	 *  -----------------------------------------------------------
	 *  Configuration : Class       No filter   AP only   Station Only
	 *  Values to set :                0           1           2
	 *  Configuration : Priority    High Rssi   Low Rssi     Detect
	 *  Values to set :                0          0x4         0x0
	 *  Configuration : Channel     filter off  filter on
	 *  Values to set :                0          0x10
	 *  -----------------------------------------------------------
	 */
	WID_SCAN_FILTER			= 0x0036,

	/*
	 *  Link Loss Threshold (measure in the beacon period)
	 *  -----------------------------------------------------------
	 *  Configuration : Any value between 10 and 254(Set to 255 disable)
	 *  Values to set : Same value. Default is 10
	 *  -----------------------------------------------------------
	 */
	WID_LINK_LOSS_THRESHOLD		= 0x0037,

	WID_ABORT_RUNNING_SCAN		= 0x003E,

	/* NMAC Character WID list */
	WID_WPS_START			= 0x0043,

	/*
	 *  Protection mode for MAC
	 *  -----------------------------------------------------------
	 *  Configuration :  Auto  No protection  ERP    HT    GF
	 *  Values to set :  0     1              2      3     4
	 *  -----------------------------------------------------------
	 */
	WID_11N_PROT_MECH		= 0x0080,

	/*
	 *  ERP Protection type for MAC
	 *  -----------------------------------------------------------
	 *  Configuration :  Self-CTS   RTS-CTS
	 *  Values to set :  0          1
	 *  -----------------------------------------------------------
	 */
	WID_11N_ERP_PROT_TYPE		= 0x0081,

	/*
	 *  HT Option Enable
	 *  -----------------------------------------------------------
	 *  Configuration :   HT Enable          HT Disable
	 *  Values to set :   1                  0
	 *  -----------------------------------------------------------
	 */
	WID_11N_ENABLE			= 0x0082,

	/*
	 *  11n Operating mode (Note that 11g operating mode will also be
	 *  used in addition to this, if this is set to HT Mixed mode)
	 *  -----------------------------------------------------------
	 *   Configuration :  HT Mixed  HT Only-20MHz   HT Only-20/40MHz
	 *  Values to set :     1         2               3
	 *  -----------------------------------------------------------
	 */
	WID_11N_OPERATING_MODE		= 0x0083,

	/*
	 *  11n OBSS non-HT STA Detection flag
	 *  -----------------------------------------------------------
	 *  Configuration :  Do not detect
	 *  Values to set :  0
	 *  Configuration :  Detect, do not protect or report
	 *  Values to set :  1
	 *  Configuration :  Detect, protect and do not report
	 *  Values to set :  2
	 *  Configuration :  Detect, protect and report to other BSS
	 *  Values to set :  3
	 *  -----------------------------------------------------------
	 */
	WID_11N_OBSS_NONHT_DETECTION	= 0x0084,

	/*
	 *  11n HT Protection Type
	 *  -----------------------------------------------------------
	 *  Configuration :  RTS-CTS   First Frame Exchange at non-HT-rate
	 *  Values to set :  0         1
	 *  Configuration :  LSIG TXOP First Frame Exchange in Mixed Fmt
	 *  Values to set :  2         3
	 *  -----------------------------------------------------------
	 */
	WID_11N_HT_PROT_TYPE		= 0x0085,

	/*
	 *  11n RIFS Protection Enable Flag
	 *  -----------------------------------------------------------
	 *  Configuration :  Disable    Enable
	 *  Values to set :  0          1
	 *  -----------------------------------------------------------
	 */
	WID_11N_RIFS_PROT_ENABLE	= 0x0086,

	/*
	 *  SMPS Mode
	 *  -----------------------------------------------------------
	 *  Configuration :  Static   Dynamic   MIMO (Power Save Disabled)
	 *  Values to set :  1        2         3
	 *  -----------------------------------------------------------
	 */
	WID_11N_SMPS_MODE		= 0x0087,

	/*
	 *  Current transmit MCS
	 *  -----------------------------------------------------------
	 *  Configuration :  MCS Index for data rate
	 *  Values to set :  0 to 7
	 *  -----------------------------------------------------------
	 */
	WID_11N_CURRENT_TX_MCS		= 0x0088,

	WID_11N_PRINT_STATS		= 0x0089,

	/*
	 *  11n Short GI Enable Flag
	 *  -----------------------------------------------------------
	 *  Configuration :  Disable    Enable
	 *  Values to set :  0          1
	 *  -----------------------------------------------------------
	 */
	WID_11N_SHORT_GI_ENABLE		= 0x008D,

	/*
	 *  11n RIFS Enable Flag
	 *  -----------------------------------------------------------
	 *  Configuration :  Disable    Enable
	 *  Values to set :  0          1
	 *  -----------------------------------------------------------
	 */
	WID_RIFS_MODE			= 0x0094,

	/*
	 *  TX Abort Feature
	 *  -----------------------------------------------------------
	 *  Configuration :  Disable Self CTS    Enable Self CTS
	 *  Values to set :             0                      1
	 *  Configuration :  Disable TX Abort    Enable TX Abort
	 *  Values to set :             2                      3
	 *  Configuration :  Enable HW TX Abort Enable SW TX Abort
	 *  Values to set :             4                      5
	 *  -----------------------------------------------------------
	 */
	WID_TX_ABORT_CONFIG		= 0x00A1,

	WID_REG_TSSI_11B_VALUE		= 0x00A6,
	WID_REG_TSSI_11G_VALUE		= 0x00A7,
	WID_REG_TSSI_11N_VALUE		= 0x00A8,
	WID_TX_CALIBRATION		= 0x00A9,
	WID_DSCR_TSSI_11B_VALUE		= 0x00AA,
	WID_DSCR_TSSI_11G_VALUE		= 0x00AB,
	WID_DSCR_TSSI_11N_VALUE		= 0x00AC,

	/*
	 *  Immediate Block-Ack Support
	 *  -----------------------------------------------------------
	 *  Configuration : Disable                  Enable
	 *  Values to set :    0                       1
	 *  -----------------------------------------------------------
	 */
	WID_11N_IMMEDIATE_BA_ENABLED	= 0x00AF,

	/*
	 *  TXOP Disable Flag
	 *  -----------------------------------------------------------
	 *  Configuration : Disable                  Enable
	 *  Values to set :    1                        0
	 *  -----------------------------------------------------------
	 */
	WID_11N_TXOP_PROT_DISABLE	= 0x00B0,

	WID_TX_POWER_LEVEL_11N		= 0x00B1,

	/* Custom Character WID list */
	WID_PC_TEST_MODE		= 0x00C8,
	/* SCAN Complete notification WID*/
	WID_SCAN_COMPLETE		= 0x00C9,

	WID_DEL_BEACON			= 0x00CA,

	WID_LOGTerminal_Switch		= 0x00CD,
	WID_TX_POWER			= 0x00CE,
	/*  EMAC Short WID list */
	/*  RTS Threshold */
	/*
	 *  -----------------------------------------------------------
	 *  Configuration :   Any value between 256 to 2347
	 *  Values to set :   Same value. Default is 2347
	 *  -----------------------------------------------------------
	 */
	WID_RTS_THRESHOLD		= 0x1000,

	/*
	 *  Fragmentation Threshold
	 *  -----------------------------------------------------------
	 *  Configuration :   Any value between 256 to 2346
	 *  Values to set :   Same value. Default is 2346
	 *  -----------------------------------------------------------
	 */
	WID_FRAG_THRESHOLD		= 0x1001,

	WID_SHORT_RETRY_LIMIT		= 0x1002,
	WID_LONG_RETRY_LIMIT		= 0x1003,
	WID_BEACON_INTERVAL		= 0x1006,
	WID_MEMORY_ACCESS_16BIT		= 0x1008,
	WID_RX_SENSE			= 0x100B,
	WID_ACTIVE_SCAN_TIME		= 0x100C,
	WID_PASSIVE_SCAN_TIME		= 0x100D,

	WID_SITE_SURVEY_SCAN_TIME	= 0x100E,
	WID_JOIN_START_TIMEOUT		= 0x100F,
	WID_AUTH_TIMEOUT		= 0x1010,
	WID_ASOC_TIMEOUT		= 0x1011,
	WID_11I_PROTOCOL_TIMEOUT	= 0x1012,
	WID_EAPOL_RESPONSE_TIMEOUT	= 0x1013,

	/* NMAC Short WID list */
	WID_11N_SIG_QUAL_VAL		= 0x1085,
	WID_CCA_THRESHOLD		= 0x1087,

	/* Custom Short WID list */

	/* EMAC Integer WID list */
	WID_FAILED_COUNT		= 0x2000,
	WID_RETRY_COUNT			= 0x2001,
	WID_MULTIPLE_RETRY_COUNT	= 0x2002,
	WID_FRAME_DUPLICATE_COUNT	= 0x2003,
	WID_ACK_FAILURE_COUNT		= 0x2004,
	WID_RECEIVED_FRAGMENT_COUNT	= 0x2005,
	WID_MCAST_RECEIVED_FRAME_COUNT	= 0x2006,
	WID_FCS_ERROR_COUNT		= 0x2007,
	WID_SUCCESS_FRAME_COUNT		= 0x2008,
	WID_HUT_TX_COUNT		= 0x200A,
	WID_TX_FRAGMENT_COUNT		= 0x200B,
	WID_TX_MULTICAST_FRAME_COUNT	= 0x200C,
	WID_RTS_SUCCESS_COUNT		= 0x200D,
	WID_RTS_FAILURE_COUNT		= 0x200E,
	WID_WEP_UNDECRYPTABLE_COUNT	= 0x200F,
	WID_REKEY_PERIOD		= 0x2010,
	WID_REKEY_PACKET_COUNT		= 0x2011,
	WID_1X_SERV_ADDR		= 0x2012,
	WID_STACK_IP_ADDR		= 0x2013,
	WID_STACK_NETMASK_ADDR		= 0x2014,
	WID_HW_RX_COUNT			= 0x2015,
	WID_MEMORY_ADDRESS		= 0x201E,
	WID_MEMORY_ACCESS_32BIT		= 0x201F,
	WID_RF_REG_VAL			= 0x2021,

	/* NMAC Integer WID list */
	WID_11N_PHY_ACTIVE_REG_VAL	= 0x2080,

	/* Custom Integer WID list */
	WID_GET_INACTIVE_TIME		= 0x2084,
	WID_SET_OPERATION_MODE		= 0X2086,
	/* EMAC String WID list */
	WID_SSID			= 0x3000,
	WID_FIRMWARE_VERSION		= 0x3001,
	WID_OPERATIONAL_RATE_SET	= 0x3002,
	WID_BSSID			= 0x3003,
	WID_WEP_KEY_VALUE		= 0x3004,
	WID_11I_PSK			= 0x3008,
	WID_11E_P_ACTION_REQ		= 0x3009,
	WID_1X_KEY			= 0x300A,
	WID_HARDWARE_VERSION		= 0x300B,
	WID_MAC_ADDR			= 0x300C,
	WID_HUT_DEST_ADDR		= 0x300D,
	WID_PHY_VERSION			= 0x300F,
	WID_SUPP_USERNAME		= 0x3010,
	WID_SUPP_PASSWORD		= 0x3011,
	WID_SITE_SURVEY_RESULTS		= 0x3012,
	WID_RX_POWER_LEVEL		= 0x3013,
	WID_DEL_ALL_RX_BA		= 0x3014,
	WID_SET_STA_MAC_INACTIVE_TIME	= 0x3017,
	WID_ADD_WEP_KEY			= 0x3019,
	WID_REMOVE_WEP_KEY		= 0x301A,
	WID_ADD_PTK			= 0x301B,
	WID_ADD_RX_GTK			= 0x301C,
	WID_ADD_TX_GTK			= 0x301D,
	WID_REMOVE_KEY			= 0x301E,
	WID_ASSOC_REQ_INFO		= 0x301F,
	WID_ASSOC_RES_INFO		= 0x3020,
	WID_MANUFACTURER		= 0x3026, /*Added for CAPI tool */
	WID_MODEL_NAME			= 0x3027, /*Added for CAPI tool */
	WID_MODEL_NUM			= 0x3028, /*Added for CAPI tool */
	WID_DEVICE_NAME			= 0x3029, /*Added for CAPI tool */
	WID_SET_DRV_HANDLER		= 0x3079,

	/* NMAC String WID list */
	WID_11N_P_ACTION_REQ		= 0x3080,
	WID_HUT_TEST_ID			= 0x3081,
	WID_PMKID_INFO			= 0x3082,
	WID_FIRMWARE_INFO		= 0x3083,
	WID_REGISTER_FRAME		= 0x3084,
	WID_DEL_ALL_STA			= 0x3085,
	WID_REMAIN_ON_CHAN		= 0x3996,
	WID_SSID_PROBE_REQ		= 0x3997,
	WID_JOIN_REQ_EXTENDED		= 0x3998,

	WID_IP_ADDRESS			= 0x3999,

	/* Custom String WID list */

	/* EMAC Binary WID list */
	WID_UAPSD_CONFIG		= 0x4001,
	WID_UAPSD_STATUS		= 0x4002,
	WID_WMM_AP_AC_PARAMS		= 0x4003,
	WID_WMM_STA_AC_PARAMS		= 0x4004,
	WID_NETWORK_INFO		= 0x4005,
	WID_STA_JOIN_INFO		= 0x4006,
	WID_CONNECTED_STA_LIST		= 0x4007,

	/* NMAC Binary WID list */
	WID_11N_AUTORATE_TABLE		= 0x4080,

	WID_SCAN_CHANNEL_LIST		= 0x4084,

	WID_INFO_ELEMENT_PROBE		= 0x4085,
	WID_INFO_ELEMENT_ASSOCIATE	= 0x4086,
	WID_ADD_STA			= 0X4087,
	WID_REMOVE_STA			= 0X4088,
	WID_EDIT_STA			= 0X4089,
	WID_ADD_BEACON			= 0x408a,

	WID_SETUP_MULTICAST_FILTER	= 0x408b,

	/* Miscellaneous WIDs */
	WID_ALL				= 0x7FFE,
	WID_MAX				= 0xFFFF
} WID_T;

struct wilc;
int wilc_wlan_init(struct net_device *dev);
u32 wilc_get_chipid(struct wilc *wilc, bool update);

#endif
