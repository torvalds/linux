/*
 * Copyright (c) 2013 Eugene Krasnikov <k.eugene.e@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _HAL_H_
#define _HAL_H_

/*---------------------------------------------------------------------------
  API VERSIONING INFORMATION

  The RIVA API is versioned as MAJOR.MINOR.VERSION.REVISION
  The MAJOR is incremented for major product/architecture changes
      (and then MINOR/VERSION/REVISION are zeroed)
  The MINOR is incremented for minor product/architecture changes
      (and then VERSION/REVISION are zeroed)
  The VERSION is incremented if a significant API change occurs
      (and then REVISION is zeroed)
  The REVISION is incremented if an insignificant API change occurs
      or if a new API is added
  All values are in the range 0..255 (ie they are 8-bit values)
 ---------------------------------------------------------------------------*/
#define WCN36XX_HAL_VER_MAJOR 1
#define WCN36XX_HAL_VER_MINOR 4
#define WCN36XX_HAL_VER_VERSION 1
#define WCN36XX_HAL_VER_REVISION 2

/* This is to force compiler to use the maximum of an int ( 4 bytes ) */
#define WCN36XX_HAL_MAX_ENUM_SIZE    0x7FFFFFFF
#define WCN36XX_HAL_MSG_TYPE_MAX_ENUM_SIZE    0x7FFF

/* Max no. of transmit categories */
#define STACFG_MAX_TC    8

/* The maximum value of access category */
#define WCN36XX_HAL_MAX_AC  4

#define WCN36XX_HAL_IPV4_ADDR_LEN       4

#define WCN36XX_HAL_STA_INVALID_IDX 0xFF
#define WCN36XX_HAL_BSS_INVALID_IDX 0xFF

/* Default Beacon template size */
#define BEACON_TEMPLATE_SIZE 0x180

/* Minimum PVM size that the FW expects. See comment in smd.c for details. */
#define TIM_MIN_PVM_SIZE 6

/* Param Change Bitmap sent to HAL */
#define PARAM_BCN_INTERVAL_CHANGED                      (1 << 0)
#define PARAM_SHORT_PREAMBLE_CHANGED                 (1 << 1)
#define PARAM_SHORT_SLOT_TIME_CHANGED                 (1 << 2)
#define PARAM_llACOEXIST_CHANGED                            (1 << 3)
#define PARAM_llBCOEXIST_CHANGED                            (1 << 4)
#define PARAM_llGCOEXIST_CHANGED                            (1 << 5)
#define PARAM_HT20MHZCOEXIST_CHANGED                  (1<<6)
#define PARAM_NON_GF_DEVICES_PRESENT_CHANGED (1<<7)
#define PARAM_RIFS_MODE_CHANGED                            (1<<8)
#define PARAM_LSIG_TXOP_FULL_SUPPORT_CHANGED   (1<<9)
#define PARAM_OBSS_MODE_CHANGED                               (1<<10)
#define PARAM_BEACON_UPDATE_MASK \
	(PARAM_BCN_INTERVAL_CHANGED |					\
	 PARAM_SHORT_PREAMBLE_CHANGED |					\
	 PARAM_SHORT_SLOT_TIME_CHANGED |				\
	 PARAM_llACOEXIST_CHANGED |					\
	 PARAM_llBCOEXIST_CHANGED |					\
	 PARAM_llGCOEXIST_CHANGED |					\
	 PARAM_HT20MHZCOEXIST_CHANGED |					\
	 PARAM_NON_GF_DEVICES_PRESENT_CHANGED |				\
	 PARAM_RIFS_MODE_CHANGED |					\
	 PARAM_LSIG_TXOP_FULL_SUPPORT_CHANGED |				\
	 PARAM_OBSS_MODE_CHANGED)

/* dump command response Buffer size */
#define DUMPCMD_RSP_BUFFER 100

/* version string max length (including NULL) */
#define WCN36XX_HAL_VERSION_LENGTH  64

/* How many frames until we start a-mpdu TX session */
#define WCN36XX_AMPDU_START_THRESH	20

#define WCN36XX_MAX_SCAN_SSIDS		9
#define WCN36XX_MAX_SCAN_IE_LEN		500

/* message types for messages exchanged between WDI and HAL */
enum wcn36xx_hal_host_msg_type {
	/* Init/De-Init */
	WCN36XX_HAL_START_REQ = 0,
	WCN36XX_HAL_START_RSP = 1,
	WCN36XX_HAL_STOP_REQ = 2,
	WCN36XX_HAL_STOP_RSP = 3,

	/* Scan */
	WCN36XX_HAL_INIT_SCAN_REQ = 4,
	WCN36XX_HAL_INIT_SCAN_RSP = 5,
	WCN36XX_HAL_START_SCAN_REQ = 6,
	WCN36XX_HAL_START_SCAN_RSP = 7,
	WCN36XX_HAL_END_SCAN_REQ = 8,
	WCN36XX_HAL_END_SCAN_RSP = 9,
	WCN36XX_HAL_FINISH_SCAN_REQ = 10,
	WCN36XX_HAL_FINISH_SCAN_RSP = 11,

	/* HW STA configuration/deconfiguration */
	WCN36XX_HAL_CONFIG_STA_REQ = 12,
	WCN36XX_HAL_CONFIG_STA_RSP = 13,
	WCN36XX_HAL_DELETE_STA_REQ = 14,
	WCN36XX_HAL_DELETE_STA_RSP = 15,
	WCN36XX_HAL_CONFIG_BSS_REQ = 16,
	WCN36XX_HAL_CONFIG_BSS_RSP = 17,
	WCN36XX_HAL_DELETE_BSS_REQ = 18,
	WCN36XX_HAL_DELETE_BSS_RSP = 19,

	/* Infra STA asscoiation */
	WCN36XX_HAL_JOIN_REQ = 20,
	WCN36XX_HAL_JOIN_RSP = 21,
	WCN36XX_HAL_POST_ASSOC_REQ = 22,
	WCN36XX_HAL_POST_ASSOC_RSP = 23,

	/* Security */
	WCN36XX_HAL_SET_BSSKEY_REQ = 24,
	WCN36XX_HAL_SET_BSSKEY_RSP = 25,
	WCN36XX_HAL_SET_STAKEY_REQ = 26,
	WCN36XX_HAL_SET_STAKEY_RSP = 27,
	WCN36XX_HAL_RMV_BSSKEY_REQ = 28,
	WCN36XX_HAL_RMV_BSSKEY_RSP = 29,
	WCN36XX_HAL_RMV_STAKEY_REQ = 30,
	WCN36XX_HAL_RMV_STAKEY_RSP = 31,

	/* Qos Related */
	WCN36XX_HAL_ADD_TS_REQ = 32,
	WCN36XX_HAL_ADD_TS_RSP = 33,
	WCN36XX_HAL_DEL_TS_REQ = 34,
	WCN36XX_HAL_DEL_TS_RSP = 35,
	WCN36XX_HAL_UPD_EDCA_PARAMS_REQ = 36,
	WCN36XX_HAL_UPD_EDCA_PARAMS_RSP = 37,
	WCN36XX_HAL_ADD_BA_REQ = 38,
	WCN36XX_HAL_ADD_BA_RSP = 39,
	WCN36XX_HAL_DEL_BA_REQ = 40,
	WCN36XX_HAL_DEL_BA_RSP = 41,

	WCN36XX_HAL_CH_SWITCH_REQ = 42,
	WCN36XX_HAL_CH_SWITCH_RSP = 43,
	WCN36XX_HAL_SET_LINK_ST_REQ = 44,
	WCN36XX_HAL_SET_LINK_ST_RSP = 45,
	WCN36XX_HAL_GET_STATS_REQ = 46,
	WCN36XX_HAL_GET_STATS_RSP = 47,
	WCN36XX_HAL_UPDATE_CFG_REQ = 48,
	WCN36XX_HAL_UPDATE_CFG_RSP = 49,

	WCN36XX_HAL_MISSED_BEACON_IND = 50,
	WCN36XX_HAL_UNKNOWN_ADDR2_FRAME_RX_IND = 51,
	WCN36XX_HAL_MIC_FAILURE_IND = 52,
	WCN36XX_HAL_FATAL_ERROR_IND = 53,
	WCN36XX_HAL_SET_KEYDONE_MSG = 54,

	/* NV Interface */
	WCN36XX_HAL_DOWNLOAD_NV_REQ = 55,
	WCN36XX_HAL_DOWNLOAD_NV_RSP = 56,

	WCN36XX_HAL_ADD_BA_SESSION_REQ = 57,
	WCN36XX_HAL_ADD_BA_SESSION_RSP = 58,
	WCN36XX_HAL_TRIGGER_BA_REQ = 59,
	WCN36XX_HAL_TRIGGER_BA_RSP = 60,
	WCN36XX_HAL_UPDATE_BEACON_REQ = 61,
	WCN36XX_HAL_UPDATE_BEACON_RSP = 62,
	WCN36XX_HAL_SEND_BEACON_REQ = 63,
	WCN36XX_HAL_SEND_BEACON_RSP = 64,

	WCN36XX_HAL_SET_BCASTKEY_REQ = 65,
	WCN36XX_HAL_SET_BCASTKEY_RSP = 66,
	WCN36XX_HAL_DELETE_STA_CONTEXT_IND = 67,
	WCN36XX_HAL_UPDATE_PROBE_RSP_TEMPLATE_REQ = 68,
	WCN36XX_HAL_UPDATE_PROBE_RSP_TEMPLATE_RSP = 69,

	/* PTT interface support */
	WCN36XX_HAL_PROCESS_PTT_REQ = 70,
	WCN36XX_HAL_PROCESS_PTT_RSP = 71,

	/* BTAMP related events */
	WCN36XX_HAL_SIGNAL_BTAMP_EVENT_REQ = 72,
	WCN36XX_HAL_SIGNAL_BTAMP_EVENT_RSP = 73,
	WCN36XX_HAL_TL_HAL_FLUSH_AC_REQ = 74,
	WCN36XX_HAL_TL_HAL_FLUSH_AC_RSP = 75,

	WCN36XX_HAL_ENTER_IMPS_REQ = 76,
	WCN36XX_HAL_EXIT_IMPS_REQ = 77,
	WCN36XX_HAL_ENTER_BMPS_REQ = 78,
	WCN36XX_HAL_EXIT_BMPS_REQ = 79,
	WCN36XX_HAL_ENTER_UAPSD_REQ = 80,
	WCN36XX_HAL_EXIT_UAPSD_REQ = 81,
	WCN36XX_HAL_UPDATE_UAPSD_PARAM_REQ = 82,
	WCN36XX_HAL_CONFIGURE_RXP_FILTER_REQ = 83,
	WCN36XX_HAL_ADD_BCN_FILTER_REQ = 84,
	WCN36XX_HAL_REM_BCN_FILTER_REQ = 85,
	WCN36XX_HAL_ADD_WOWL_BCAST_PTRN = 86,
	WCN36XX_HAL_DEL_WOWL_BCAST_PTRN = 87,
	WCN36XX_HAL_ENTER_WOWL_REQ = 88,
	WCN36XX_HAL_EXIT_WOWL_REQ = 89,
	WCN36XX_HAL_HOST_OFFLOAD_REQ = 90,
	WCN36XX_HAL_SET_RSSI_THRESH_REQ = 91,
	WCN36XX_HAL_GET_RSSI_REQ = 92,
	WCN36XX_HAL_SET_UAPSD_AC_PARAMS_REQ = 93,
	WCN36XX_HAL_CONFIGURE_APPS_CPU_WAKEUP_STATE_REQ = 94,

	WCN36XX_HAL_ENTER_IMPS_RSP = 95,
	WCN36XX_HAL_EXIT_IMPS_RSP = 96,
	WCN36XX_HAL_ENTER_BMPS_RSP = 97,
	WCN36XX_HAL_EXIT_BMPS_RSP = 98,
	WCN36XX_HAL_ENTER_UAPSD_RSP = 99,
	WCN36XX_HAL_EXIT_UAPSD_RSP = 100,
	WCN36XX_HAL_SET_UAPSD_AC_PARAMS_RSP = 101,
	WCN36XX_HAL_UPDATE_UAPSD_PARAM_RSP = 102,
	WCN36XX_HAL_CONFIGURE_RXP_FILTER_RSP = 103,
	WCN36XX_HAL_ADD_BCN_FILTER_RSP = 104,
	WCN36XX_HAL_REM_BCN_FILTER_RSP = 105,
	WCN36XX_HAL_SET_RSSI_THRESH_RSP = 106,
	WCN36XX_HAL_HOST_OFFLOAD_RSP = 107,
	WCN36XX_HAL_ADD_WOWL_BCAST_PTRN_RSP = 108,
	WCN36XX_HAL_DEL_WOWL_BCAST_PTRN_RSP = 109,
	WCN36XX_HAL_ENTER_WOWL_RSP = 110,
	WCN36XX_HAL_EXIT_WOWL_RSP = 111,
	WCN36XX_HAL_RSSI_NOTIFICATION_IND = 112,
	WCN36XX_HAL_GET_RSSI_RSP = 113,
	WCN36XX_HAL_CONFIGURE_APPS_CPU_WAKEUP_STATE_RSP = 114,

	/* 11k related events */
	WCN36XX_HAL_SET_MAX_TX_POWER_REQ = 115,
	WCN36XX_HAL_SET_MAX_TX_POWER_RSP = 116,

	/* 11R related msgs */
	WCN36XX_HAL_AGGR_ADD_TS_REQ = 117,
	WCN36XX_HAL_AGGR_ADD_TS_RSP = 118,

	/* P2P  WLAN_FEATURE_P2P */
	WCN36XX_HAL_SET_P2P_GONOA_REQ = 119,
	WCN36XX_HAL_SET_P2P_GONOA_RSP = 120,

	/* WLAN Dump commands */
	WCN36XX_HAL_DUMP_COMMAND_REQ = 121,
	WCN36XX_HAL_DUMP_COMMAND_RSP = 122,

	/* OEM_DATA FEATURE SUPPORT */
	WCN36XX_HAL_START_OEM_DATA_REQ = 123,
	WCN36XX_HAL_START_OEM_DATA_RSP = 124,

	/* ADD SELF STA REQ and RSP */
	WCN36XX_HAL_ADD_STA_SELF_REQ = 125,
	WCN36XX_HAL_ADD_STA_SELF_RSP = 126,

	/* DEL SELF STA SUPPORT */
	WCN36XX_HAL_DEL_STA_SELF_REQ = 127,
	WCN36XX_HAL_DEL_STA_SELF_RSP = 128,

	/* Coex Indication */
	WCN36XX_HAL_COEX_IND = 129,

	/* Tx Complete Indication */
	WCN36XX_HAL_OTA_TX_COMPL_IND = 130,

	/* Host Suspend/resume messages */
	WCN36XX_HAL_HOST_SUSPEND_IND = 131,
	WCN36XX_HAL_HOST_RESUME_REQ = 132,
	WCN36XX_HAL_HOST_RESUME_RSP = 133,

	WCN36XX_HAL_SET_TX_POWER_REQ = 134,
	WCN36XX_HAL_SET_TX_POWER_RSP = 135,
	WCN36XX_HAL_GET_TX_POWER_REQ = 136,
	WCN36XX_HAL_GET_TX_POWER_RSP = 137,

	WCN36XX_HAL_P2P_NOA_ATTR_IND = 138,

	WCN36XX_HAL_ENABLE_RADAR_DETECT_REQ = 139,
	WCN36XX_HAL_ENABLE_RADAR_DETECT_RSP = 140,
	WCN36XX_HAL_GET_TPC_REPORT_REQ = 141,
	WCN36XX_HAL_GET_TPC_REPORT_RSP = 142,
	WCN36XX_HAL_RADAR_DETECT_IND = 143,
	WCN36XX_HAL_RADAR_DETECT_INTR_IND = 144,
	WCN36XX_HAL_KEEP_ALIVE_REQ = 145,
	WCN36XX_HAL_KEEP_ALIVE_RSP = 146,

	/* PNO messages */
	WCN36XX_HAL_SET_PREF_NETWORK_REQ = 147,
	WCN36XX_HAL_SET_PREF_NETWORK_RSP = 148,
	WCN36XX_HAL_SET_RSSI_FILTER_REQ = 149,
	WCN36XX_HAL_SET_RSSI_FILTER_RSP = 150,
	WCN36XX_HAL_UPDATE_SCAN_PARAM_REQ = 151,
	WCN36XX_HAL_UPDATE_SCAN_PARAM_RSP = 152,
	WCN36XX_HAL_PREF_NETW_FOUND_IND = 153,

	WCN36XX_HAL_SET_TX_PER_TRACKING_REQ = 154,
	WCN36XX_HAL_SET_TX_PER_TRACKING_RSP = 155,
	WCN36XX_HAL_TX_PER_HIT_IND = 156,

	WCN36XX_HAL_8023_MULTICAST_LIST_REQ = 157,
	WCN36XX_HAL_8023_MULTICAST_LIST_RSP = 158,

	WCN36XX_HAL_SET_PACKET_FILTER_REQ = 159,
	WCN36XX_HAL_SET_PACKET_FILTER_RSP = 160,
	WCN36XX_HAL_PACKET_FILTER_MATCH_COUNT_REQ = 161,
	WCN36XX_HAL_PACKET_FILTER_MATCH_COUNT_RSP = 162,
	WCN36XX_HAL_CLEAR_PACKET_FILTER_REQ = 163,
	WCN36XX_HAL_CLEAR_PACKET_FILTER_RSP = 164,

	/*
	 * This is temp fix. Should be removed once Host and Riva code is
	 * in sync.
	 */
	WCN36XX_HAL_INIT_SCAN_CON_REQ = 165,

	WCN36XX_HAL_SET_POWER_PARAMS_REQ = 166,
	WCN36XX_HAL_SET_POWER_PARAMS_RSP = 167,

	WCN36XX_HAL_TSM_STATS_REQ = 168,
	WCN36XX_HAL_TSM_STATS_RSP = 169,

	/* wake reason indication (WOW) */
	WCN36XX_HAL_WAKE_REASON_IND = 170,

	/* GTK offload support */
	WCN36XX_HAL_GTK_OFFLOAD_REQ = 171,
	WCN36XX_HAL_GTK_OFFLOAD_RSP = 172,
	WCN36XX_HAL_GTK_OFFLOAD_GETINFO_REQ = 173,
	WCN36XX_HAL_GTK_OFFLOAD_GETINFO_RSP = 174,

	WCN36XX_HAL_FEATURE_CAPS_EXCHANGE_REQ = 175,
	WCN36XX_HAL_FEATURE_CAPS_EXCHANGE_RSP = 176,
	WCN36XX_HAL_EXCLUDE_UNENCRYPTED_IND = 177,

	WCN36XX_HAL_SET_THERMAL_MITIGATION_REQ = 178,
	WCN36XX_HAL_SET_THERMAL_MITIGATION_RSP = 179,

	WCN36XX_HAL_UPDATE_VHT_OP_MODE_REQ = 182,
	WCN36XX_HAL_UPDATE_VHT_OP_MODE_RSP = 183,

	WCN36XX_HAL_P2P_NOA_START_IND = 184,

	WCN36XX_HAL_GET_ROAM_RSSI_REQ = 185,
	WCN36XX_HAL_GET_ROAM_RSSI_RSP = 186,

	WCN36XX_HAL_CLASS_B_STATS_IND = 187,
	WCN36XX_HAL_DEL_BA_IND = 188,
	WCN36XX_HAL_DHCP_START_IND = 189,
	WCN36XX_HAL_DHCP_STOP_IND = 190,

	/* Scan Offload(hw) APIs */
	WCN36XX_HAL_START_SCAN_OFFLOAD_REQ = 204,
	WCN36XX_HAL_START_SCAN_OFFLOAD_RSP = 205,
	WCN36XX_HAL_STOP_SCAN_OFFLOAD_REQ = 206,
	WCN36XX_HAL_STOP_SCAN_OFFLOAD_RSP = 207,
	WCN36XX_HAL_UPDATE_CHANNEL_LIST_REQ = 208,
	WCN36XX_HAL_UPDATE_CHANNEL_LIST_RSP = 209,
	WCN36XX_HAL_SCAN_OFFLOAD_IND = 210,

	WCN36XX_HAL_AVOID_FREQ_RANGE_IND = 233,

	WCN36XX_HAL_PRINT_REG_INFO_IND = 259,

	WCN36XX_HAL_MSG_MAX = WCN36XX_HAL_MSG_TYPE_MAX_ENUM_SIZE
};

/* Enumeration for Version */
enum wcn36xx_hal_host_msg_version {
	WCN36XX_HAL_MSG_VERSION0 = 0,
	WCN36XX_HAL_MSG_VERSION1 = 1,
	/* define as 2 bytes data */
	WCN36XX_HAL_MSG_WCNSS_CTRL_VERSION = 0x7FFF,
	WCN36XX_HAL_MSG_VERSION_MAX_FIELD = WCN36XX_HAL_MSG_WCNSS_CTRL_VERSION
};

enum driver_type {
	DRIVER_TYPE_PRODUCTION = 0,
	DRIVER_TYPE_MFG = 1,
	DRIVER_TYPE_DVT = 2,
	DRIVER_TYPE_MAX = WCN36XX_HAL_MAX_ENUM_SIZE
};

enum wcn36xx_hal_stop_type {
	HAL_STOP_TYPE_SYS_RESET,
	HAL_STOP_TYPE_SYS_DEEP_SLEEP,
	HAL_STOP_TYPE_RF_KILL,
	HAL_STOP_TYPE_MAX = WCN36XX_HAL_MAX_ENUM_SIZE
};

enum wcn36xx_hal_sys_mode {
	HAL_SYS_MODE_NORMAL,
	HAL_SYS_MODE_LEARN,
	HAL_SYS_MODE_SCAN,
	HAL_SYS_MODE_PROMISC,
	HAL_SYS_MODE_SUSPEND_LINK,
	HAL_SYS_MODE_ROAM_SCAN,
	HAL_SYS_MODE_ROAM_SUSPEND_LINK,
	HAL_SYS_MODE_MAX = WCN36XX_HAL_MAX_ENUM_SIZE
};

enum phy_chan_bond_state {
	/* 20MHz IF bandwidth centered on IF carrier */
	PHY_SINGLE_CHANNEL_CENTERED = 0,

	/* 40MHz IF bandwidth with lower 20MHz supporting the primary channel */
	PHY_DOUBLE_CHANNEL_LOW_PRIMARY = 1,

	/* 40MHz IF bandwidth centered on IF carrier */
	PHY_DOUBLE_CHANNEL_CENTERED = 2,

	/* 40MHz IF bandwidth with higher 20MHz supporting the primary ch */
	PHY_DOUBLE_CHANNEL_HIGH_PRIMARY = 3,

	/* 20/40MHZ offset LOW 40/80MHZ offset CENTERED */
	PHY_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_CENTERED = 4,

	/* 20/40MHZ offset CENTERED 40/80MHZ offset CENTERED */
	PHY_QUADRUPLE_CHANNEL_20MHZ_CENTERED_40MHZ_CENTERED = 5,

	/* 20/40MHZ offset HIGH 40/80MHZ offset CENTERED */
	PHY_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_CENTERED = 6,

	/* 20/40MHZ offset LOW 40/80MHZ offset LOW */
	PHY_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_LOW = 7,

	/* 20/40MHZ offset HIGH 40/80MHZ offset LOW */
	PHY_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_LOW = 8,

	/* 20/40MHZ offset LOW 40/80MHZ offset HIGH */
	PHY_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_HIGH = 9,

	/* 20/40MHZ offset-HIGH 40/80MHZ offset HIGH */
	PHY_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_HIGH = 10,

	PHY_CHANNEL_BONDING_STATE_MAX = WCN36XX_HAL_MAX_ENUM_SIZE
};

/* Spatial Multiplexing(SM) Power Save mode */
enum wcn36xx_hal_ht_mimo_state {
	/* Static SM Power Save mode */
	WCN36XX_HAL_HT_MIMO_PS_STATIC = 0,

	/* Dynamic SM Power Save mode */
	WCN36XX_HAL_HT_MIMO_PS_DYNAMIC = 1,

	/* reserved */
	WCN36XX_HAL_HT_MIMO_PS_NA = 2,

	/* SM Power Save disabled */
	WCN36XX_HAL_HT_MIMO_PS_NO_LIMIT = 3,

	WCN36XX_HAL_HT_MIMO_PS_MAX = WCN36XX_HAL_MAX_ENUM_SIZE
};

/* each station added has a rate mode which specifies the sta attributes */
enum sta_rate_mode {
	STA_TAURUS = 0,
	STA_TITAN,
	STA_POLARIS,
	STA_11b,
	STA_11bg,
	STA_11a,
	STA_11n,
	STA_11ac,
	STA_INVALID_RATE_MODE = WCN36XX_HAL_MAX_ENUM_SIZE
};

/* 1,2,5.5,11 */
#define WCN36XX_HAL_NUM_DSSS_RATES           4

/* 6,9,12,18,24,36,48,54 */
#define WCN36XX_HAL_NUM_OFDM_RATES           8

/* 72,96,108 */
#define WCN36XX_HAL_NUM_POLARIS_RATES       3

#define WCN36XX_HAL_MAC_MAX_SUPPORTED_MCS_SET    16

enum wcn36xx_hal_bss_type {
	WCN36XX_HAL_INFRASTRUCTURE_MODE,

	/* Added for softAP support */
	WCN36XX_HAL_INFRA_AP_MODE,

	WCN36XX_HAL_IBSS_MODE,

	/* Added for BT-AMP support */
	WCN36XX_HAL_BTAMP_STA_MODE,

	/* Added for BT-AMP support */
	WCN36XX_HAL_BTAMP_AP_MODE,

	WCN36XX_HAL_AUTO_MODE,

	WCN36XX_HAL_DONOT_USE_BSS_TYPE = WCN36XX_HAL_MAX_ENUM_SIZE
};

enum wcn36xx_hal_nw_type {
	WCN36XX_HAL_11A_NW_TYPE,
	WCN36XX_HAL_11B_NW_TYPE,
	WCN36XX_HAL_11G_NW_TYPE,
	WCN36XX_HAL_11N_NW_TYPE,
	WCN36XX_HAL_DONOT_USE_NW_TYPE = WCN36XX_HAL_MAX_ENUM_SIZE
};

#define WCN36XX_HAL_MAC_RATESET_EID_MAX            12

enum wcn36xx_hal_ht_operating_mode {
	/* No Protection */
	WCN36XX_HAL_HT_OP_MODE_PURE,

	/* Overlap Legacy device present, protection is optional */
	WCN36XX_HAL_HT_OP_MODE_OVERLAP_LEGACY,

	/* No legacy device, but 20 MHz HT present */
	WCN36XX_HAL_HT_OP_MODE_NO_LEGACY_20MHZ_HT,

	/* Protection is required */
	WCN36XX_HAL_HT_OP_MODE_MIXED,

	WCN36XX_HAL_HT_OP_MODE_MAX = WCN36XX_HAL_MAX_ENUM_SIZE
};

/* Encryption type enum used with peer */
enum ani_ed_type {
	WCN36XX_HAL_ED_NONE,
	WCN36XX_HAL_ED_WEP40,
	WCN36XX_HAL_ED_WEP104,
	WCN36XX_HAL_ED_TKIP,
	WCN36XX_HAL_ED_CCMP,
	WCN36XX_HAL_ED_WPI,
	WCN36XX_HAL_ED_AES_128_CMAC,
	WCN36XX_HAL_ED_NOT_IMPLEMENTED = WCN36XX_HAL_MAX_ENUM_SIZE
};

#define WLAN_MAX_KEY_RSC_LEN                16
#define WLAN_WAPI_KEY_RSC_LEN               16

/* MAX key length when ULA is used */
#define WCN36XX_HAL_MAC_MAX_KEY_LENGTH              32
#define WCN36XX_HAL_MAC_MAX_NUM_OF_DEFAULT_KEYS     4

/*
 * Enum to specify whether key is used for TX only, RX only or both.
 */
enum ani_key_direction {
	WCN36XX_HAL_TX_ONLY,
	WCN36XX_HAL_RX_ONLY,
	WCN36XX_HAL_TX_RX,
	WCN36XX_HAL_TX_DEFAULT,
	WCN36XX_HAL_DONOT_USE_KEY_DIRECTION = WCN36XX_HAL_MAX_ENUM_SIZE
};

enum ani_wep_type {
	WCN36XX_HAL_WEP_STATIC,
	WCN36XX_HAL_WEP_DYNAMIC,
	WCN36XX_HAL_WEP_MAX = WCN36XX_HAL_MAX_ENUM_SIZE
};

enum wcn36xx_hal_link_state {

	WCN36XX_HAL_LINK_IDLE_STATE = 0,
	WCN36XX_HAL_LINK_PREASSOC_STATE = 1,
	WCN36XX_HAL_LINK_POSTASSOC_STATE = 2,
	WCN36XX_HAL_LINK_AP_STATE = 3,
	WCN36XX_HAL_LINK_IBSS_STATE = 4,

	/* BT-AMP Case */
	WCN36XX_HAL_LINK_BTAMP_PREASSOC_STATE = 5,
	WCN36XX_HAL_LINK_BTAMP_POSTASSOC_STATE = 6,
	WCN36XX_HAL_LINK_BTAMP_AP_STATE = 7,
	WCN36XX_HAL_LINK_BTAMP_STA_STATE = 8,

	/* Reserved for HAL Internal Use */
	WCN36XX_HAL_LINK_LEARN_STATE = 9,
	WCN36XX_HAL_LINK_SCAN_STATE = 10,
	WCN36XX_HAL_LINK_FINISH_SCAN_STATE = 11,
	WCN36XX_HAL_LINK_INIT_CAL_STATE = 12,
	WCN36XX_HAL_LINK_FINISH_CAL_STATE = 13,
	WCN36XX_HAL_LINK_LISTEN_STATE = 14,

	WCN36XX_HAL_LINK_MAX = WCN36XX_HAL_MAX_ENUM_SIZE
};

enum wcn36xx_hal_stats_mask {
	HAL_SUMMARY_STATS_INFO = 0x00000001,
	HAL_GLOBAL_CLASS_A_STATS_INFO = 0x00000002,
	HAL_GLOBAL_CLASS_B_STATS_INFO = 0x00000004,
	HAL_GLOBAL_CLASS_C_STATS_INFO = 0x00000008,
	HAL_GLOBAL_CLASS_D_STATS_INFO = 0x00000010,
	HAL_PER_STA_STATS_INFO = 0x00000020
};

/* BT-AMP events type */
enum bt_amp_event_type {
	BTAMP_EVENT_CONNECTION_START,
	BTAMP_EVENT_CONNECTION_STOP,
	BTAMP_EVENT_CONNECTION_TERMINATED,

	/* This and beyond are invalid values */
	BTAMP_EVENT_TYPE_MAX = WCN36XX_HAL_MAX_ENUM_SIZE,
};

/* PE Statistics */
enum pe_stats_mask {
	PE_SUMMARY_STATS_INFO = 0x00000001,
	PE_GLOBAL_CLASS_A_STATS_INFO = 0x00000002,
	PE_GLOBAL_CLASS_B_STATS_INFO = 0x00000004,
	PE_GLOBAL_CLASS_C_STATS_INFO = 0x00000008,
	PE_GLOBAL_CLASS_D_STATS_INFO = 0x00000010,
	PE_PER_STA_STATS_INFO = 0x00000020,

	/* This and beyond are invalid values */
	PE_STATS_TYPE_MAX = WCN36XX_HAL_MAX_ENUM_SIZE
};

/*
 * Configuration Parameter IDs
 */
#define WCN36XX_HAL_CFG_STA_ID				0
#define WCN36XX_HAL_CFG_CURRENT_TX_ANTENNA		1
#define WCN36XX_HAL_CFG_CURRENT_RX_ANTENNA		2
#define WCN36XX_HAL_CFG_LOW_GAIN_OVERRIDE		3
#define WCN36XX_HAL_CFG_POWER_STATE_PER_CHAIN		4
#define WCN36XX_HAL_CFG_CAL_PERIOD			5
#define WCN36XX_HAL_CFG_CAL_CONTROL			6
#define WCN36XX_HAL_CFG_PROXIMITY			7
#define WCN36XX_HAL_CFG_NETWORK_DENSITY			8
#define WCN36XX_HAL_CFG_MAX_MEDIUM_TIME			9
#define WCN36XX_HAL_CFG_MAX_MPDUS_IN_AMPDU		10
#define WCN36XX_HAL_CFG_RTS_THRESHOLD			11
#define WCN36XX_HAL_CFG_SHORT_RETRY_LIMIT		12
#define WCN36XX_HAL_CFG_LONG_RETRY_LIMIT		13
#define WCN36XX_HAL_CFG_FRAGMENTATION_THRESHOLD		14
#define WCN36XX_HAL_CFG_DYNAMIC_THRESHOLD_ZERO		15
#define WCN36XX_HAL_CFG_DYNAMIC_THRESHOLD_ONE		16
#define WCN36XX_HAL_CFG_DYNAMIC_THRESHOLD_TWO		17
#define WCN36XX_HAL_CFG_FIXED_RATE			18
#define WCN36XX_HAL_CFG_RETRYRATE_POLICY		19
#define WCN36XX_HAL_CFG_RETRYRATE_SECONDARY		20
#define WCN36XX_HAL_CFG_RETRYRATE_TERTIARY		21
#define WCN36XX_HAL_CFG_FORCE_POLICY_PROTECTION		22
#define WCN36XX_HAL_CFG_FIXED_RATE_MULTICAST_24GHZ	23
#define WCN36XX_HAL_CFG_FIXED_RATE_MULTICAST_5GHZ	24
#define WCN36XX_HAL_CFG_DEFAULT_RATE_INDEX_24GHZ	25
#define WCN36XX_HAL_CFG_DEFAULT_RATE_INDEX_5GHZ		26
#define WCN36XX_HAL_CFG_MAX_BA_SESSIONS			27
#define WCN36XX_HAL_CFG_PS_DATA_INACTIVITY_TIMEOUT	28
#define WCN36XX_HAL_CFG_PS_ENABLE_BCN_FILTER		29
#define WCN36XX_HAL_CFG_PS_ENABLE_RSSI_MONITOR		30
#define WCN36XX_HAL_CFG_NUM_BEACON_PER_RSSI_AVERAGE	31
#define WCN36XX_HAL_CFG_STATS_PERIOD			32
#define WCN36XX_HAL_CFG_CFP_MAX_DURATION		33
#define WCN36XX_HAL_CFG_FRAME_TRANS_ENABLED		34
#define WCN36XX_HAL_CFG_DTIM_PERIOD			35
#define WCN36XX_HAL_CFG_EDCA_WMM_ACBK			36
#define WCN36XX_HAL_CFG_EDCA_WMM_ACBE			37
#define WCN36XX_HAL_CFG_EDCA_WMM_ACVO			38
#define WCN36XX_HAL_CFG_EDCA_WMM_ACVI			39
#define WCN36XX_HAL_CFG_BA_THRESHOLD_HIGH		40
#define WCN36XX_HAL_CFG_MAX_BA_BUFFERS			41
#define WCN36XX_HAL_CFG_RPE_POLLING_THRESHOLD		42
#define WCN36XX_HAL_CFG_RPE_AGING_THRESHOLD_FOR_AC0_REG	43
#define WCN36XX_HAL_CFG_RPE_AGING_THRESHOLD_FOR_AC1_REG	44
#define WCN36XX_HAL_CFG_RPE_AGING_THRESHOLD_FOR_AC2_REG	45
#define WCN36XX_HAL_CFG_RPE_AGING_THRESHOLD_FOR_AC3_REG	46
#define WCN36XX_HAL_CFG_NO_OF_ONCHIP_REORDER_SESSIONS	47
#define WCN36XX_HAL_CFG_PS_LISTEN_INTERVAL		48
#define WCN36XX_HAL_CFG_PS_HEART_BEAT_THRESHOLD		49
#define WCN36XX_HAL_CFG_PS_NTH_BEACON_FILTER		50
#define WCN36XX_HAL_CFG_PS_MAX_PS_POLL			51
#define WCN36XX_HAL_CFG_PS_MIN_RSSI_THRESHOLD		52
#define WCN36XX_HAL_CFG_PS_RSSI_FILTER_PERIOD		53
#define WCN36XX_HAL_CFG_PS_BROADCAST_FRAME_FILTER_ENABLE 54
#define WCN36XX_HAL_CFG_PS_IGNORE_DTIM			55
#define WCN36XX_HAL_CFG_PS_ENABLE_BCN_EARLY_TERM	56
#define WCN36XX_HAL_CFG_DYNAMIC_PS_POLL_VALUE		57
#define WCN36XX_HAL_CFG_PS_NULLDATA_AP_RESP_TIMEOUT	58
#define WCN36XX_HAL_CFG_TELE_BCN_WAKEUP_EN		59
#define WCN36XX_HAL_CFG_TELE_BCN_TRANS_LI		60
#define WCN36XX_HAL_CFG_TELE_BCN_TRANS_LI_IDLE_BCNS	61
#define WCN36XX_HAL_CFG_TELE_BCN_MAX_LI			62
#define WCN36XX_HAL_CFG_TELE_BCN_MAX_LI_IDLE_BCNS	63
#define WCN36XX_HAL_CFG_TX_PWR_CTRL_ENABLE		64
#define WCN36XX_HAL_CFG_VALID_RADAR_CHANNEL_LIST	65
#define WCN36XX_HAL_CFG_TX_POWER_24_20			66
#define WCN36XX_HAL_CFG_TX_POWER_24_40			67
#define WCN36XX_HAL_CFG_TX_POWER_50_20			68
#define WCN36XX_HAL_CFG_TX_POWER_50_40			69
#define WCN36XX_HAL_CFG_MCAST_BCAST_FILTER_SETTING	70
#define WCN36XX_HAL_CFG_BCN_EARLY_TERM_WAKEUP_INTERVAL	71
#define WCN36XX_HAL_CFG_MAX_TX_POWER_2_4		72
#define WCN36XX_HAL_CFG_MAX_TX_POWER_5			73
#define WCN36XX_HAL_CFG_INFRA_STA_KEEP_ALIVE_PERIOD	74
#define WCN36XX_HAL_CFG_ENABLE_CLOSE_LOOP		75
#define WCN36XX_HAL_CFG_BTC_EXECUTION_MODE		76
#define WCN36XX_HAL_CFG_BTC_DHCP_BT_SLOTS_TO_BLOCK	77
#define WCN36XX_HAL_CFG_BTC_A2DP_DHCP_BT_SUB_INTERVALS	78
#define WCN36XX_HAL_CFG_PS_TX_INACTIVITY_TIMEOUT	79
#define WCN36XX_HAL_CFG_WCNSS_API_VERSION		80
#define WCN36XX_HAL_CFG_AP_KEEPALIVE_TIMEOUT		81
#define WCN36XX_HAL_CFG_GO_KEEPALIVE_TIMEOUT		82
#define WCN36XX_HAL_CFG_ENABLE_MC_ADDR_LIST		83
#define WCN36XX_HAL_CFG_BTC_STATIC_LEN_INQ_BT		84
#define WCN36XX_HAL_CFG_BTC_STATIC_LEN_PAGE_BT		85
#define WCN36XX_HAL_CFG_BTC_STATIC_LEN_CONN_BT		86
#define WCN36XX_HAL_CFG_BTC_STATIC_LEN_LE_BT		87
#define WCN36XX_HAL_CFG_BTC_STATIC_LEN_INQ_WLAN		88
#define WCN36XX_HAL_CFG_BTC_STATIC_LEN_PAGE_WLAN	89
#define WCN36XX_HAL_CFG_BTC_STATIC_LEN_CONN_WLAN	90
#define WCN36XX_HAL_CFG_BTC_STATIC_LEN_LE_WLAN		91
#define WCN36XX_HAL_CFG_BTC_DYN_MAX_LEN_BT		92
#define WCN36XX_HAL_CFG_BTC_DYN_MAX_LEN_WLAN		93
#define WCN36XX_HAL_CFG_BTC_MAX_SCO_BLOCK_PERC		94
#define WCN36XX_HAL_CFG_BTC_DHCP_PROT_ON_A2DP		95
#define WCN36XX_HAL_CFG_BTC_DHCP_PROT_ON_SCO		96
#define WCN36XX_HAL_CFG_ENABLE_UNICAST_FILTER		97
#define WCN36XX_HAL_CFG_MAX_ASSOC_LIMIT			98
#define WCN36XX_HAL_CFG_ENABLE_LPWR_IMG_TRANSITION	99
#define WCN36XX_HAL_CFG_ENABLE_MCC_ADAPTIVE_SCHEDULER	100
#define WCN36XX_HAL_CFG_ENABLE_DETECT_PS_SUPPORT	101
#define WCN36XX_HAL_CFG_AP_LINK_MONITOR_TIMEOUT		102
#define WCN36XX_HAL_CFG_BTC_DWELL_TIME_MULTIPLIER	103
#define WCN36XX_HAL_CFG_ENABLE_TDLS_OXYGEN_MODE		104
#define WCN36XX_HAL_CFG_ENABLE_NAT_KEEP_ALIVE_FILTER	105
#define WCN36XX_HAL_CFG_ENABLE_SAP_OBSS_PROT		106
#define WCN36XX_HAL_CFG_PSPOLL_DATA_RECEP_TIMEOUT	107
#define WCN36XX_HAL_CFG_TDLS_PUAPSD_BUFFER_STA_CAPABLE	108
#define WCN36XX_HAL_CFG_TDLS_PUAPSD_MASK		109
#define WCN36XX_HAL_CFG_TDLS_PUAPSD_INACTIVITY_TIME	110
#define WCN36XX_HAL_CFG_TDLS_PUAPSD_RX_FRAME_THRESHOLD	111
#define WCN36XX_HAL_CFG_ANTENNA_DIVERSITY		112
#define WCN36XX_HAL_CFG_ATH_DISABLE			113
#define WCN36XX_HAL_CFG_FLEXCONNECT_POWER_FACTOR	114
#define WCN36XX_HAL_CFG_ENABLE_ADAPTIVE_RX_DRAIN	115
#define WCN36XX_HAL_CFG_TDLS_OFF_CHANNEL_CAPABLE	116
#define WCN36XX_HAL_CFG_MWS_COEX_V1_WAN_FREQ		117
#define WCN36XX_HAL_CFG_MWS_COEX_V1_WLAN_FREQ		118
#define WCN36XX_HAL_CFG_MWS_COEX_V1_CONFIG		119
#define WCN36XX_HAL_CFG_MWS_COEX_V1_CONFIG2		120
#define WCN36XX_HAL_CFG_MWS_COEX_V2_WAN_FREQ		121
#define WCN36XX_HAL_CFG_MWS_COEX_V2_WLAN_FREQ		122
#define WCN36XX_HAL_CFG_MWS_COEX_V2_CONFIG		123
#define WCN36XX_HAL_CFG_MWS_COEX_V2_CONFIG2		124
#define WCN36XX_HAL_CFG_MWS_COEX_V3_WAN_FREQ		125
#define WCN36XX_HAL_CFG_MWS_COEX_V3_WLAN_FREQ		126
#define WCN36XX_HAL_CFG_MWS_COEX_V3_CONFIG		127
#define WCN36XX_HAL_CFG_MWS_COEX_V3_CONFIG2		128
#define WCN36XX_HAL_CFG_MWS_COEX_V4_WAN_FREQ		129
#define WCN36XX_HAL_CFG_MWS_COEX_V4_WLAN_FREQ		130
#define WCN36XX_HAL_CFG_MWS_COEX_V4_CONFIG		131
#define WCN36XX_HAL_CFG_MWS_COEX_V4_CONFIG2		132
#define WCN36XX_HAL_CFG_MWS_COEX_V5_WAN_FREQ		133
#define WCN36XX_HAL_CFG_MWS_COEX_V5_WLAN_FREQ		134
#define WCN36XX_HAL_CFG_MWS_COEX_V5_CONFIG		135
#define WCN36XX_HAL_CFG_MWS_COEX_V5_CONFIG2		136
#define WCN36XX_HAL_CFG_MWS_COEX_V6_WAN_FREQ		137
#define WCN36XX_HAL_CFG_MWS_COEX_V6_WLAN_FREQ		138
#define WCN36XX_HAL_CFG_MWS_COEX_V6_CONFIG		139
#define WCN36XX_HAL_CFG_MWS_COEX_V6_CONFIG2		140
#define WCN36XX_HAL_CFG_MWS_COEX_V7_WAN_FREQ		141
#define WCN36XX_HAL_CFG_MWS_COEX_V7_WLAN_FREQ		142
#define WCN36XX_HAL_CFG_MWS_COEX_V7_CONFIG		143
#define WCN36XX_HAL_CFG_MWS_COEX_V7_CONFIG2		144
#define WCN36XX_HAL_CFG_MWS_COEX_V8_WAN_FREQ		145
#define WCN36XX_HAL_CFG_MWS_COEX_V8_WLAN_FREQ		146
#define WCN36XX_HAL_CFG_MWS_COEX_V8_CONFIG		147
#define WCN36XX_HAL_CFG_MWS_COEX_V8_CONFIG2		148
#define WCN36XX_HAL_CFG_MWS_COEX_V9_WAN_FREQ		149
#define WCN36XX_HAL_CFG_MWS_COEX_V9_WLAN_FREQ		150
#define WCN36XX_HAL_CFG_MWS_COEX_V9_CONFIG		151
#define WCN36XX_HAL_CFG_MWS_COEX_V9_CONFIG2		152
#define WCN36XX_HAL_CFG_MWS_COEX_V10_WAN_FREQ		153
#define WCN36XX_HAL_CFG_MWS_COEX_V10_WLAN_FREQ		154
#define WCN36XX_HAL_CFG_MWS_COEX_V10_CONFIG		155
#define WCN36XX_HAL_CFG_MWS_COEX_V10_CONFIG2		156
#define WCN36XX_HAL_CFG_MWS_COEX_MODEM_BACKOFF		157
#define WCN36XX_HAL_CFG_MWS_COEX_CONFIG1		158
#define WCN36XX_HAL_CFG_MWS_COEX_CONFIG2		159
#define WCN36XX_HAL_CFG_MWS_COEX_CONFIG3		160
#define WCN36XX_HAL_CFG_MWS_COEX_CONFIG4		161
#define WCN36XX_HAL_CFG_MWS_COEX_CONFIG5		162
#define WCN36XX_HAL_CFG_MWS_COEX_CONFIG6		163
#define WCN36XX_HAL_CFG_SAR_POWER_BACKOFF		164
#define WCN36XX_HAL_CFG_GO_LINK_MONITOR_TIMEOUT		165
#define WCN36XX_HAL_CFG_BTC_STATIC_OPP_WLAN_ACTIVE_WLAN_LEN	166
#define WCN36XX_HAL_CFG_BTC_STATIC_OPP_WLAN_ACTIVE_BT_LEN	167
#define WCN36XX_HAL_CFG_BTC_SAP_STATIC_OPP_ACTIVE_WLAN_LEN	168
#define WCN36XX_HAL_CFG_BTC_SAP_STATIC_OPP_ACTIVE_BT_LEN	169
#define WCN36XX_HAL_CFG_RMC_FIXED_RATE			170
#define WCN36XX_HAL_CFG_ASD_PROBE_INTERVAL		171
#define WCN36XX_HAL_CFG_ASD_TRIGGER_THRESHOLD		172
#define WCN36XX_HAL_CFG_ASD_RTT_RSSI_HYST_THRESHOLD	173
#define WCN36XX_HAL_CFG_BTC_CTS2S_ON_STA_DURING_SCO	174
#define WCN36XX_HAL_CFG_SHORT_PREAMBLE			175
#define WCN36XX_HAL_CFG_SHORT_SLOT_TIME			176
#define WCN36XX_HAL_CFG_DELAYED_BA			177
#define WCN36XX_HAL_CFG_IMMEDIATE_BA			178
#define WCN36XX_HAL_CFG_DOT11_MODE			179
#define WCN36XX_HAL_CFG_HT_CAPS				180
#define WCN36XX_HAL_CFG_AMPDU_PARAMS			181
#define WCN36XX_HAL_CFG_TX_BF_INFO			182
#define WCN36XX_HAL_CFG_ASC_CAP_INFO			183
#define WCN36XX_HAL_CFG_EXT_HT_CAPS			184
#define WCN36XX_HAL_CFG_QOS_ENABLED			185
#define WCN36XX_HAL_CFG_WME_ENABLED			186
#define WCN36XX_HAL_CFG_WSM_ENABLED			187
#define WCN36XX_HAL_CFG_WMM_ENABLED			188
#define WCN36XX_HAL_CFG_UAPSD_PER_AC_BITMASK		189
#define WCN36XX_HAL_CFG_MCS_RATES			190
#define WCN36XX_HAL_CFG_VHT_CAPS			191
#define WCN36XX_HAL_CFG_VHT_RX_SUPP_MCS			192
#define WCN36XX_HAL_CFG_VHT_TX_SUPP_MCS			193
#define WCN36XX_HAL_CFG_RA_FILTER_ENABLE		194
#define WCN36XX_HAL_CFG_RA_RATE_LIMIT_INTERVAL		195
#define WCN36XX_HAL_CFG_BTC_FATAL_HID_NSNIFF_BLK	196
#define WCN36XX_HAL_CFG_BTC_CRITICAL_HID_NSNIFF_BLK	197
#define WCN36XX_HAL_CFG_BTC_DYN_A2DP_TX_QUEUE_THOLD	198
#define WCN36XX_HAL_CFG_BTC_DYN_OPP_TX_QUEUE_THOLD	199
#define WCN36XX_HAL_CFG_LINK_FAIL_TIMEOUT		200
#define WCN36XX_HAL_CFG_MAX_UAPSD_CONSEC_SP		201
#define WCN36XX_HAL_CFG_MAX_UAPSD_CONSEC_RX_CNT		202
#define WCN36XX_HAL_CFG_MAX_UAPSD_CONSEC_TX_CNT		203
#define WCN36XX_HAL_CFG_MAX_UAPSD_CONSEC_RX_CNT_MEAS_WINDOW	204
#define WCN36XX_HAL_CFG_MAX_UAPSD_CONSEC_TX_CNT_MEAS_WINDOW	205
#define WCN36XX_HAL_CFG_MAX_PSPOLL_IN_WMM_UAPSD_PS_MODE	206
#define WCN36XX_HAL_CFG_MAX_UAPSD_INACTIVITY_INTERVALS	207
#define WCN36XX_HAL_CFG_ENABLE_DYNAMIC_WMMPS		208
#define WCN36XX_HAL_CFG_BURST_MODE_BE_TXOP_VALUE	209
#define WCN36XX_HAL_CFG_ENABLE_DYNAMIC_RA_START_RATE	210
#define WCN36XX_HAL_CFG_BTC_FAST_WLAN_CONN_PREF		211
#define WCN36XX_HAL_CFG_ENABLE_RTSCTS_HTVHT		212
#define WCN36XX_HAL_CFG_BTC_STATIC_OPP_WLAN_IDLE_WLAN_LEN	213
#define WCN36XX_HAL_CFG_BTC_STATIC_OPP_WLAN_IDLE_BT_LEN	214
#define WCN36XX_HAL_CFG_LINK_FAIL_TX_CNT		215
#define WCN36XX_HAL_CFG_TOGGLE_ARP_BDRATES		216
#define WCN36XX_HAL_CFG_OPTIMIZE_CA_EVENT		217
#define WCN36XX_HAL_CFG_EXT_SCAN_CONC_MODE		218
#define WCN36XX_HAL_CFG_BAR_WAKEUP_HOST_DISABLE		219
#define WCN36XX_HAL_CFG_SAR_BOFFSET_CORRECTION_ENABLE	220
#define WCN36XX_HAL_CFG_UNITS_OF_BCN_WAIT_TIME		221
#define WCN36XX_HAL_CFG_CONS_BCNMISS_COUNT		222
#define WCN36XX_HAL_CFG_BTC_DISABLE_WLAN_LINK_CRITICAL	223
#define WCN36XX_HAL_CFG_DISABLE_SCAN_DURING_SCO		224
#define WCN36XX_HAL_CFG_TRIGGER_NULLFRAME_BEFORE_HB	225
#define WCN36XX_HAL_CFG_ENABLE_POWERSAVE_OFFLOAD	226
#define WCN36XX_HAL_CFG_MAX_PARAMS			227

/* Specify the starting bitrate, 11B and 11A/G rates can be specified in
 * multiples of 0.5 So for 5.5 mbps => 11. for MCS 0 - 7 rates, Bit 7 should
 * set to 1 and Bit 0-6 represent the MCS index. so for MCS2 => 130.
 * Any invalid non-zero value or unsupported rate will set the start rate
 * to 6 mbps.
 */
#define WCN36XX_HAL_CFG_ENABLE_DYNAMIC_RA_START_RATE	210

/* Message definitons - All the messages below need to be packed */

/* Definition for HAL API Version. */
struct wcnss_wlan_version {
	u8 revision;
	u8 version;
	u8 minor;
	u8 major;
} __packed;

/* Definition for Encryption Keys */
struct wcn36xx_hal_keys {
	u8 id;

	/* 0 for multicast */
	u8 unicast;

	enum ani_key_direction direction;

	/* Usage is unknown */
	u8 rsc[WLAN_MAX_KEY_RSC_LEN];

	/* =1 for authenticator,=0 for supplicant */
	u8 pae_role;

	u16 length;
	u8 key[WCN36XX_HAL_MAC_MAX_KEY_LENGTH];
} __packed;

/*
 * set_sta_key_params Moving here since it is shared by
 * configbss/setstakey msgs
 */
struct wcn36xx_hal_set_sta_key_params {
	/* STA Index */
	u16 sta_index;

	/* Encryption Type used with peer */
	enum ani_ed_type enc_type;

	/* STATIC/DYNAMIC - valid only for WEP */
	enum ani_wep_type wep_type;

	/* Default WEP key, valid only for static WEP, must between 0 and 3. */
	u8 def_wep_idx;

	/* valid only for non-static WEP encyrptions */
	struct wcn36xx_hal_keys key[WCN36XX_HAL_MAC_MAX_NUM_OF_DEFAULT_KEYS];

	/*
	 * Control for Replay Count, 1= Single TID based replay count on Tx
	 * 0 = Per TID based replay count on TX
	 */
	u8 single_tid_rc;

} __packed;

/* 4-byte control message header used by HAL*/
struct wcn36xx_hal_msg_header {
	enum wcn36xx_hal_host_msg_type msg_type:16;
	enum wcn36xx_hal_host_msg_version msg_version:16;
	u32 len;
} __packed;

/* Config format required by HAL for each CFG item*/
struct wcn36xx_hal_cfg {
	/* Cfg Id. The Id required by HAL is exported by HAL
	 * in shared header file between UMAC and HAL.*/
	u16 id;

	/* Length of the Cfg. This parameter is used to go to next cfg
	 * in the TLV format.*/
	u16 len;

	/* Padding bytes for unaligned address's */
	u16 pad_bytes;

	/* Reserve bytes for making cfgVal to align address */
	u16 reserve;

	/* Following the uCfgLen field there should be a 'uCfgLen' bytes
	 * containing the uCfgValue ; u8 uCfgValue[uCfgLen] */
} __packed;

struct wcn36xx_hal_mac_start_parameters {
	/* Drive Type - Production or FTM etc */
	enum driver_type type;

	/* Length of the config buffer */
	u32 len;

	/* Following this there is a TLV formatted buffer of length
	 * "len" bytes containing all config values.
	 * The TLV is expected to be formatted like this:
	 * 0           15            31           31+CFG_LEN-1        length-1
	 * |   CFG_ID   |   CFG_LEN   |   CFG_BODY    |  CFG_ID  |......|
	 */
} __packed;

struct wcn36xx_hal_mac_start_req_msg {
	/* config buffer must start in TLV format just here */
	struct wcn36xx_hal_msg_header header;
	struct wcn36xx_hal_mac_start_parameters params;
} __packed;

struct wcn36xx_hal_mac_start_rsp_params {
	/* success or failure */
	u16 status;

	/* Max number of STA supported by the device */
	u8 stations;

	/* Max number of BSS supported by the device */
	u8 bssids;

	/* API Version */
	struct wcnss_wlan_version version;

	/* CRM build information */
	u8 crm_version[WCN36XX_HAL_VERSION_LENGTH];

	/* hardware/chipset/misc version information */
	u8 wlan_version[WCN36XX_HAL_VERSION_LENGTH];

} __packed;

struct wcn36xx_hal_mac_start_rsp_msg {
	struct wcn36xx_hal_msg_header header;
	struct wcn36xx_hal_mac_start_rsp_params start_rsp_params;
} __packed;

struct wcn36xx_hal_mac_stop_req_params {
	/* The reason for which the device is being stopped */
	enum wcn36xx_hal_stop_type reason;

} __packed;

struct wcn36xx_hal_mac_stop_req_msg {
	struct wcn36xx_hal_msg_header header;
	struct wcn36xx_hal_mac_stop_req_params stop_req_params;
} __packed;

struct wcn36xx_hal_mac_stop_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
} __packed;

struct wcn36xx_hal_update_cfg_req_msg {
	/*
	 * Note: The length specified in tHalUpdateCfgReqMsg messages should be
	 * header.msgLen = sizeof(tHalUpdateCfgReqMsg) + uConfigBufferLen
	 */
	struct wcn36xx_hal_msg_header header;

	/* Length of the config buffer. Allows UMAC to update multiple CFGs */
	u32 len;

	/*
	 * Following this there is a TLV formatted buffer of length
	 * "uConfigBufferLen" bytes containing all config values.
	 * The TLV is expected to be formatted like this:
	 * 0           15            31           31+CFG_LEN-1        length-1
	 * |   CFG_ID   |   CFG_LEN   |   CFG_BODY    |  CFG_ID  |......|
	 */

} __packed;

struct wcn36xx_hal_update_cfg_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;

} __packed;

/* Frame control field format (2 bytes) */
struct wcn36xx_hal_mac_frame_ctl {

#ifndef ANI_LITTLE_BIT_ENDIAN

	u8 subType:4;
	u8 type:2;
	u8 protVer:2;

	u8 order:1;
	u8 wep:1;
	u8 moreData:1;
	u8 powerMgmt:1;
	u8 retry:1;
	u8 moreFrag:1;
	u8 fromDS:1;
	u8 toDS:1;

#else

	u8 protVer:2;
	u8 type:2;
	u8 subType:4;

	u8 toDS:1;
	u8 fromDS:1;
	u8 moreFrag:1;
	u8 retry:1;
	u8 powerMgmt:1;
	u8 moreData:1;
	u8 wep:1;
	u8 order:1;

#endif

};

/* Sequence control field */
struct wcn36xx_hal_mac_seq_ctl {
	u8 fragNum:4;
	u8 seqNumLo:4;
	u8 seqNumHi:8;
};

/* Management header format */
struct wcn36xx_hal_mac_mgmt_hdr {
	struct wcn36xx_hal_mac_frame_ctl fc;
	u8 durationLo;
	u8 durationHi;
	u8 da[6];
	u8 sa[6];
	u8 bssId[6];
	struct wcn36xx_hal_mac_seq_ctl seqControl;
};

/* FIXME: pronto v1 apparently has 4 */
#define WCN36XX_HAL_NUM_BSSID               2

/* Scan Entry to hold active BSS idx's */
struct wcn36xx_hal_scan_entry {
	u8 bss_index[WCN36XX_HAL_NUM_BSSID];
	u8 active_bss_count;
};

struct wcn36xx_hal_init_scan_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* LEARN - AP Role
	   SCAN - STA Role */
	enum wcn36xx_hal_sys_mode mode;

	/* BSSID of the BSS */
	u8 bssid[ETH_ALEN];

	/* Whether BSS needs to be notified */
	u8 notify;

	/* Kind of frame to be used for notifying the BSS (Data Null, QoS
	 * Null, or CTS to Self). Must always be a valid frame type. */
	u8 frame_type;

	/* UMAC has the option of passing the MAC frame to be used for
	 * notifying the BSS. If non-zero, HAL will use the MAC frame
	 * buffer pointed to by macMgmtHdr. If zero, HAL will generate the
	 * appropriate MAC frame based on frameType. */
	u8 frame_len;

	/* Following the framelength there is a MAC frame buffer if
	 * frameLength is non-zero. */
	struct wcn36xx_hal_mac_mgmt_hdr mac_mgmt_hdr;

	/* Entry to hold number of active BSS idx's */
	struct wcn36xx_hal_scan_entry scan_entry;
};

struct wcn36xx_hal_init_scan_con_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* LEARN - AP Role
	   SCAN - STA Role */
	enum wcn36xx_hal_sys_mode mode;

	/* BSSID of the BSS */
	u8 bssid[ETH_ALEN];

	/* Whether BSS needs to be notified */
	u8 notify;

	/* Kind of frame to be used for notifying the BSS (Data Null, QoS
	 * Null, or CTS to Self). Must always be a valid frame type. */
	u8 frame_type;

	/* UMAC has the option of passing the MAC frame to be used for
	 * notifying the BSS. If non-zero, HAL will use the MAC frame
	 * buffer pointed to by macMgmtHdr. If zero, HAL will generate the
	 * appropriate MAC frame based on frameType. */
	u8 frame_length;

	/* Following the framelength there is a MAC frame buffer if
	 * frameLength is non-zero. */
	struct wcn36xx_hal_mac_mgmt_hdr mac_mgmt_hdr;

	/* Entry to hold number of active BSS idx's */
	struct wcn36xx_hal_scan_entry scan_entry;

	/* Single NoA usage in Scanning */
	u8 use_noa;

	/* Indicates the scan duration (in ms) */
	u16 scan_duration;

};

struct wcn36xx_hal_init_scan_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;

} __packed;

struct wcn36xx_hal_start_scan_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* Indicates the channel to scan */
	u8 scan_channel;
} __packed;

struct wcn36xx_hal_start_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;

	u32 start_tsf[2];
	u8 tx_mgmt_power;

} __packed;

struct wcn36xx_hal_end_scan_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* Indicates the channel to stop scanning. Not used really. But
	 * retained for symmetry with "start Scan" message. It can also
	 * help in error check if needed. */
	u8 scan_channel;
} __packed;

struct wcn36xx_hal_end_scan_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
} __packed;

struct wcn36xx_hal_finish_scan_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* Identifies the operational state of the AP/STA
	 * LEARN - AP Role SCAN - STA Role */
	enum wcn36xx_hal_sys_mode mode;

	/* Operating channel to tune to. */
	u8 oper_channel;

	/* Channel Bonding state If 20/40 MHz is operational, this will
	 * indicate the 40 MHz extension channel in combination with the
	 * control channel */
	enum phy_chan_bond_state cb_state;

	/* BSSID of the BSS */
	u8 bssid[ETH_ALEN];

	/* Whether BSS needs to be notified */
	u8 notify;

	/* Kind of frame to be used for notifying the BSS (Data Null, QoS
	 * Null, or CTS to Self). Must always be a valid frame type. */
	u8 frame_type;

	/* UMAC has the option of passing the MAC frame to be used for
	 * notifying the BSS. If non-zero, HAL will use the MAC frame
	 * buffer pointed to by macMgmtHdr. If zero, HAL will generate the
	 * appropriate MAC frame based on frameType. */
	u8 frame_length;

	/* Following the framelength there is a MAC frame buffer if
	 * frameLength is non-zero. */
	struct wcn36xx_hal_mac_mgmt_hdr mac_mgmt_hdr;

	/* Entry to hold number of active BSS idx's */
	struct wcn36xx_hal_scan_entry scan_entry;

} __packed;

struct wcn36xx_hal_finish_scan_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;

} __packed;

enum wcn36xx_hal_scan_type {
	WCN36XX_HAL_SCAN_TYPE_PASSIVE = 0x00,
	WCN36XX_HAL_SCAN_TYPE_ACTIVE = WCN36XX_HAL_MAX_ENUM_SIZE
};

struct wcn36xx_hal_mac_ssid {
	u8 length;
	u8 ssid[32];
} __packed;

struct wcn36xx_hal_start_scan_offload_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* BSSIDs hot list */
	u8 num_bssid;
	u8 bssids[4][ETH_ALEN];

	/* Directed probe-requests will be sent for listed SSIDs (max 10)*/
	u8 num_ssid;
	struct wcn36xx_hal_mac_ssid ssids[10];

	/* Report AP with hidden ssid */
	u8 scan_hidden;

	/* Self MAC address */
	u8 mac[ETH_ALEN];

	/* BSS type */
	enum wcn36xx_hal_bss_type bss_type;

	/* Scan type */
	enum wcn36xx_hal_scan_type scan_type;

	/* Minimum scanning time on each channel (ms) */
	u32 min_ch_time;

	/* Maximum scanning time on each channel */
	u32 max_ch_time;

	/* Is a p2p search */
	u8 p2p_search;

	/* Channels to scan */
	u8 num_channel;
	u8 channels[80];

	/* IE field */
	u16 ie_len;
	u8 ie[WCN36XX_MAX_SCAN_IE_LEN];
} __packed;

struct wcn36xx_hal_start_scan_offload_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
} __packed;

enum wcn36xx_hal_scan_offload_ind_type {
	/* Scan has been started */
	WCN36XX_HAL_SCAN_IND_STARTED = 0x01,
	/* Scan has been completed */
	WCN36XX_HAL_SCAN_IND_COMPLETED = 0x02,
	/* Moved to foreign channel */
	WCN36XX_HAL_SCAN_IND_FOREIGN_CHANNEL = 0x08,
	/* scan request has been dequeued */
	WCN36XX_HAL_SCAN_IND_DEQUEUED = 0x10,
	/* preempted by other high priority scan */
	WCN36XX_HAL_SCAN_IND_PREEMPTED = 0x20,
	/* scan start failed */
	WCN36XX_HAL_SCAN_IND_FAILED = 0x40,
	 /*scan restarted */
	WCN36XX_HAL_SCAN_IND_RESTARTED = 0x80,
	WCN36XX_HAL_SCAN_IND_MAX = WCN36XX_HAL_MAX_ENUM_SIZE
};

struct wcn36xx_hal_scan_offload_ind {
	struct wcn36xx_hal_msg_header header;

	u32 type;
	u32 channel_mhz;
	u32 scan_id;
} __packed;

struct wcn36xx_hal_stop_scan_offload_req_msg {
	struct wcn36xx_hal_msg_header header;
} __packed;

struct wcn36xx_hal_stop_scan_offload_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
} __packed;

#define WCN36XX_HAL_CHAN_REG1_MIN_PWR_MASK  0x000000ff
#define WCN36XX_HAL_CHAN_REG1_MAX_PWR_MASK  0x0000ff00
#define WCN36XX_HAL_CHAN_REG1_REG_PWR_MASK  0x00ff0000
#define WCN36XX_HAL_CHAN_REG1_CLASS_ID_MASK 0xff000000
#define WCN36XX_HAL_CHAN_REG2_ANT_GAIN_MASK 0x000000ff
#define WCN36XX_HAL_CHAN_INFO_FLAG_PASSIVE  BIT(7)
#define WCN36XX_HAL_CHAN_INFO_FLAG_DFS      BIT(10)
#define WCN36XX_HAL_CHAN_INFO_FLAG_HT       BIT(11)
#define WCN36XX_HAL_CHAN_INFO_FLAG_VHT      BIT(12)
#define WCN36XX_HAL_CHAN_INFO_PHY_11A       0
#define WCN36XX_HAL_CHAN_INFO_PHY_11BG      1
#define WCN36XX_HAL_DEFAULT_ANT_GAIN        6
#define WCN36XX_HAL_DEFAULT_MIN_POWER       6

struct wcn36xx_hal_channel_param {
	u32 mhz;
	u32 band_center_freq1;
	u32 band_center_freq2;
	u32 channel_info;
	u32 reg_info_1;
	u32 reg_info_2;
} __packed;

struct wcn36xx_hal_update_channel_list_req_msg {
	struct wcn36xx_hal_msg_header header;

	u8 num_channel;
	struct wcn36xx_hal_channel_param channels[80];
} __packed;

enum wcn36xx_hal_rate_index {
	HW_RATE_INDEX_1MBPS	= 0x82,
	HW_RATE_INDEX_2MBPS	= 0x84,
	HW_RATE_INDEX_5_5MBPS	= 0x8B,
	HW_RATE_INDEX_6MBPS	= 0x0C,
	HW_RATE_INDEX_9MBPS	= 0x12,
	HW_RATE_INDEX_11MBPS	= 0x96,
	HW_RATE_INDEX_12MBPS	= 0x18,
	HW_RATE_INDEX_18MBPS	= 0x24,
	HW_RATE_INDEX_24MBPS	= 0x30,
	HW_RATE_INDEX_36MBPS	= 0x48,
	HW_RATE_INDEX_48MBPS	= 0x60,
	HW_RATE_INDEX_54MBPS	= 0x6C
};

struct wcn36xx_hal_supported_rates {
	/*
	 * For Self STA Entry: this represents Self Mode.
	 * For Peer Stations, this represents the mode of the peer.
	 * On Station:
	 *
	 * --this mode is updated when PE adds the Self Entry.
	 *
	 * -- OR when PE sends 'ADD_BSS' message and station context in BSS
	 *    is used to indicate the mode of the AP.
	 *
	 * ON AP:
	 *
	 * -- this mode is updated when PE sends 'ADD_BSS' and Sta entry
	 *     for that BSS is used to indicate the self mode of the AP.
	 *
	 * -- OR when a station is associated, PE sends 'ADD_STA' message
	 *    with this mode updated.
	 */

	enum sta_rate_mode op_rate_mode;

	/* 11b, 11a and aniLegacyRates are IE rates which gives rate in
	 * unit of 500Kbps */
	u16 dsss_rates[WCN36XX_HAL_NUM_DSSS_RATES];
	u16 ofdm_rates[WCN36XX_HAL_NUM_OFDM_RATES];
	u16 legacy_rates[WCN36XX_HAL_NUM_POLARIS_RATES];
	u16 reserved;

	/* Taurus only supports 26 Titan Rates(no ESF/concat Rates will be
	 * supported) First 26 bits are reserved for those Titan rates and
	 * the last 4 bits(bit28-31) for Taurus, 2(bit26-27) bits are
	 * reserved. */
	/* Titan and Taurus Rates */
	u32 enhanced_rate_bitmap;

	/*
	 * 0-76 bits used, remaining reserved
	 * bits 0-15 and 32 should be set.
	 */
	u8 supported_mcs_set[WCN36XX_HAL_MAC_MAX_SUPPORTED_MCS_SET];

	/*
	 * RX Highest Supported Data Rate defines the highest data
	 * rate that the STA is able to receive, in unites of 1Mbps.
	 * This value is derived from "Supported MCS Set field" inside
	 * the HT capability element.
	 */
	u16 rx_highest_data_rate;

} __packed;

struct wcn36xx_hal_config_sta_params {
	/* BSSID of STA */
	u8 bssid[ETH_ALEN];

	/* ASSOC ID, as assigned by UMAC */
	u16 aid;

	/* STA entry Type: 0 - Self, 1 - Other/Peer, 2 - BSSID, 3 - BCAST */
	u8 type;

	/* Short Preamble Supported. */
	u8 short_preamble_supported;

	/* MAC Address of STA */
	u8 mac[ETH_ALEN];

	/* Listen interval of the STA */
	u16 listen_interval;

	/* Support for 11e/WMM */
	u8 wmm_enabled;

	/* 11n HT capable STA */
	u8 ht_capable;

	/* TX Width Set: 0 - 20 MHz only, 1 - 20/40 MHz */
	u8 tx_channel_width_set;

	/* RIFS mode 0 - NA, 1 - Allowed */
	u8 rifs_mode;

	/* L-SIG TXOP Protection mechanism
	   0 - No Support, 1 - Supported
	   SG - there is global field */
	u8 lsig_txop_protection;

	/* Max Ampdu Size supported by STA. TPE programming.
	   0 : 8k , 1 : 16k, 2 : 32k, 3 : 64k */
	u8 max_ampdu_size;

	/* Max Ampdu density. Used by RA.  3 : 0~7 : 2^(11nAMPDUdensity -4) */
	u8 max_ampdu_density;

	/* Max AMSDU size 1 : 3839 bytes, 0 : 7935 bytes */
	u8 max_amsdu_size;

	/* Short GI support for 40Mhz packets */
	u8 sgi_40mhz;

	/* Short GI support for 20Mhz packets */
	u8 sgi_20Mhz;

	/* TODO move this parameter to the end for 3680 */
	/* These rates are the intersection of peer and self capabilities. */
	struct wcn36xx_hal_supported_rates supported_rates;

	/* Robust Management Frame (RMF) enabled/disabled */
	u8 rmf;

	/* The unicast encryption type in the association */
	u32 encrypt_type;

	/* HAL should update the existing STA entry, if this flag is set. UMAC
	   will set this flag in case of RE-ASSOC, where we want to reuse the
	   old STA ID. 0 = Add, 1 = Update */
	u8 action;

	/* U-APSD Flags: 1b per AC.  Encoded as follows:
	   b7 b6 b5 b4 b3 b2 b1 b0 =
	   X  X  X  X  BE BK VI VO */
	u8 uapsd;

	/* Max SP Length */
	u8 max_sp_len;

	/* 11n Green Field preamble support
	   0 - Not supported, 1 - Supported */
	u8 green_field_capable;

	/* MIMO Power Save mode */
	enum wcn36xx_hal_ht_mimo_state mimo_ps;

	/* Delayed BA Support */
	u8 delayed_ba_support;

	/* Max AMPDU duration in 32us */
	u8 max_ampdu_duration;

	/* HT STA should set it to 1 if it is enabled in BSS. HT STA should
	 * set it to 0 if AP does not support it. This indication is sent
	 * to HAL and HAL uses this flag to pickup up appropriate 40Mhz
	 * rates. */
	u8 dsss_cck_mode_40mhz;

	/* Valid STA Idx when action=Update. Set to 0xFF when invalid!
	 * Retained for backward compalibity with existing HAL code */
	u8 sta_index;

	/* BSSID of BSS to which station is associated. Set to 0xFF when
	 * invalid. Retained for backward compalibity with existing HAL
	 * code */
	u8 bssid_index;

	u8 p2p;

	/* TODO add this parameter for 3680. */
	/* Reserved to align next field on a dword boundary */
	/* u8 reserved; */
} __packed;

struct wcn36xx_hal_config_sta_req_msg {
	struct wcn36xx_hal_msg_header header;
	struct wcn36xx_hal_config_sta_params sta_params;
} __packed;

struct wcn36xx_hal_supported_rates_v1 {
	/* For Self STA Entry: this represents Self Mode.
	 * For Peer Stations, this represents the mode of the peer.
	 * On Station:
	 *
	 * --this mode is updated when PE adds the Self Entry.
	 *
	 * -- OR when PE sends 'ADD_BSS' message and station context in BSS
	 *    is used to indicate the mode of the AP.
	 *
	 * ON AP:
	 *
	 * -- this mode is updated when PE sends 'ADD_BSS' and Sta entry
	 *     for that BSS is used to indicate the self mode of the AP.
	 *
	 * -- OR when a station is associated, PE sends 'ADD_STA' message
	 *    with this mode updated.
	 */

	enum sta_rate_mode op_rate_mode;

	/* 11b, 11a and aniLegacyRates are IE rates which gives rate in
	 * unit of 500Kbps
	 */
	u16 dsss_rates[WCN36XX_HAL_NUM_DSSS_RATES];
	u16 ofdm_rates[WCN36XX_HAL_NUM_OFDM_RATES];
	u16 legacy_rates[WCN36XX_HAL_NUM_POLARIS_RATES];
	u16 reserved;

	/* Taurus only supports 26 Titan Rates(no ESF/concat Rates will be
	 * supported) First 26 bits are reserved for those Titan rates and
	 * the last 4 bits(bit28-31) for Taurus, 2(bit26-27) bits are
	 * reserved
	 * Titan and Taurus Rates
	 */
	u32 enhanced_rate_bitmap;

	/* 0-76 bits used, remaining reserved
	 * bits 0-15 and 32 should be set.
	 */
	u8 supported_mcs_set[WCN36XX_HAL_MAC_MAX_SUPPORTED_MCS_SET];

	/* RX Highest Supported Data Rate defines the highest data
	 * rate that the STA is able to receive, in unites of 1Mbps.
	 * This value is derived from "Supported MCS Set field" inside
	 * the HT capability element.
	 */
	u16 rx_highest_data_rate;

	/* Indicates the Maximum MCS that can be received for each spatial
	 * stream.
	 */
	u16 vht_rx_mcs_map;

	/* Indicates the highest VHT data rate that the STA is able to
	 * receive.
	 */
	u16 vht_rx_highest_data_rate;

	/* Indicates the Maximum MCS that can be transmitted for each spatial
	 * stream.
	 */
	u16 vht_tx_mcs_map;

	/* Indicates the highest VHT data rate that the STA is able to
	 * transmit.
	 */
	u16 vht_tx_highest_data_rate;
} __packed;

struct wcn36xx_hal_config_sta_params_v1 {
	/* BSSID of STA */
	u8 bssid[ETH_ALEN];

	/* ASSOC ID, as assigned by UMAC */
	u16 aid;

	/* STA entry Type: 0 - Self, 1 - Other/Peer, 2 - BSSID, 3 - BCAST */
	u8 type;

	/* Short Preamble Supported. */
	u8 short_preamble_supported;

	/* MAC Address of STA */
	u8 mac[ETH_ALEN];

	/* Listen interval of the STA */
	u16 listen_interval;

	/* Support for 11e/WMM */
	u8 wmm_enabled;

	/* 11n HT capable STA */
	u8 ht_capable;

	/* TX Width Set: 0 - 20 MHz only, 1 - 20/40 MHz */
	u8 tx_channel_width_set;

	/* RIFS mode 0 - NA, 1 - Allowed */
	u8 rifs_mode;

	/* L-SIG TXOP Protection mechanism
	   0 - No Support, 1 - Supported
	   SG - there is global field */
	u8 lsig_txop_protection;

	/* Max Ampdu Size supported by STA. TPE programming.
	   0 : 8k , 1 : 16k, 2 : 32k, 3 : 64k */
	u8 max_ampdu_size;

	/* Max Ampdu density. Used by RA.  3 : 0~7 : 2^(11nAMPDUdensity -4) */
	u8 max_ampdu_density;

	/* Max AMSDU size 1 : 3839 bytes, 0 : 7935 bytes */
	u8 max_amsdu_size;

	/* Short GI support for 40Mhz packets */
	u8 sgi_40mhz;

	/* Short GI support for 20Mhz packets */
	u8 sgi_20Mhz;

	/* Robust Management Frame (RMF) enabled/disabled */
	u8 rmf;

	/* The unicast encryption type in the association */
	u32 encrypt_type;

	/* HAL should update the existing STA entry, if this flag is set. UMAC
	   will set this flag in case of RE-ASSOC, where we want to reuse the
	   old STA ID. 0 = Add, 1 = Update */
	u8 action;

	/* U-APSD Flags: 1b per AC.  Encoded as follows:
	   b7 b6 b5 b4 b3 b2 b1 b0 =
	   X  X  X  X  BE BK VI VO */
	u8 uapsd;

	/* Max SP Length */
	u8 max_sp_len;

	/* 11n Green Field preamble support
	   0 - Not supported, 1 - Supported */
	u8 green_field_capable;

	/* MIMO Power Save mode */
	enum wcn36xx_hal_ht_mimo_state mimo_ps;

	/* Delayed BA Support */
	u8 delayed_ba_support;

	/* Max AMPDU duration in 32us */
	u8 max_ampdu_duration;

	/* HT STA should set it to 1 if it is enabled in BSS. HT STA should
	 * set it to 0 if AP does not support it. This indication is sent
	 * to HAL and HAL uses this flag to pickup up appropriate 40Mhz
	 * rates. */
	u8 dsss_cck_mode_40mhz;

	/* Valid STA Idx when action=Update. Set to 0xFF when invalid!
	 * Retained for backward compalibity with existing HAL code */
	u8 sta_index;

	/* BSSID of BSS to which station is associated. Set to 0xFF when
	 * invalid. Retained for backward compalibity with existing HAL
	 * code */
	u8 bssid_index;

	u8 p2p;

	/* Reserved to align next field on a dword boundary */
	u8 ht_ldpc_enabled:1;
	u8 vht_ldpc_enabled:1;
	u8 vht_tx_bf_enabled:1;
	u8 vht_tx_mu_beamformee_capable:1;
	u8 reserved:4;

	/* These rates are the intersection of peer and self capabilities. */
	struct wcn36xx_hal_supported_rates_v1 supported_rates;

	u8 vht_capable;
	u8 vht_tx_channel_width_set;

} __packed;

#define WCN36XX_DIFF_STA_PARAMS_V1_NOVHT 10

struct wcn36xx_hal_config_sta_req_msg_v1 {
	struct wcn36xx_hal_msg_header header;
	struct wcn36xx_hal_config_sta_params_v1 sta_params;
} __packed;

struct config_sta_rsp_params {
	/* success or failure */
	u32 status;

	/* Station index; valid only when 'status' field value SUCCESS */
	u8 sta_index;

	/* BSSID Index of BSS to which the station is associated */
	u8 bssid_index;

	/* DPU Index for PTK */
	u8 dpu_index;

	/* DPU Index for GTK */
	u8 bcast_dpu_index;

	/* DPU Index for IGTK  */
	u8 bcast_mgmt_dpu_idx;

	/* PTK DPU signature */
	u8 uc_ucast_sig;

	/* GTK DPU isignature */
	u8 uc_bcast_sig;

	/* IGTK DPU signature */
	u8 uc_mgmt_sig;

	u8 p2p;

} __packed;

struct wcn36xx_hal_config_sta_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	struct config_sta_rsp_params params;
} __packed;

/* Delete STA Request message */
struct wcn36xx_hal_delete_sta_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* Index of STA to delete */
	u8 sta_index;

} __packed;

/* Delete STA Response message */
struct wcn36xx_hal_delete_sta_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;

	/* Index of STA deleted */
	u8 sta_id;
} __packed;

/* 12 Bytes long because this structure can be used to represent rate and
 * extended rate set IEs. The parser assume this to be at least 12 */
struct wcn36xx_hal_rate_set {
	u8 num_rates;
	u8 rate[WCN36XX_HAL_MAC_RATESET_EID_MAX];
} __packed;

/* access category record */
struct wcn36xx_hal_aci_aifsn {
#ifndef ANI_LITTLE_BIT_ENDIAN
	u8 rsvd:1;
	u8 aci:2;
	u8 acm:1;
	u8 aifsn:4;
#else
	u8 aifsn:4;
	u8 acm:1;
	u8 aci:2;
	u8 rsvd:1;
#endif
} __packed;

/* contention window size */
struct wcn36xx_hal_mac_cw {
#ifndef ANI_LITTLE_BIT_ENDIAN
	u8 max:4;
	u8 min:4;
#else
	u8 min:4;
	u8 max:4;
#endif
} __packed;

struct wcn36xx_hal_edca_param_record {
	struct wcn36xx_hal_aci_aifsn aci;
	struct wcn36xx_hal_mac_cw cw;
	u16 txop_limit;
} __packed;

/* Concurrency role. These are generic IDs that identify the various roles
 *  in the software system. */
enum wcn36xx_hal_con_mode {
	WCN36XX_HAL_STA_MODE = 0,

	/* to support softAp mode . This is misleading.
	   It means AP MODE only. */
	WCN36XX_HAL_STA_SAP_MODE = 1,

	WCN36XX_HAL_P2P_CLIENT_MODE,
	WCN36XX_HAL_P2P_GO_MODE,
	WCN36XX_HAL_MONITOR_MODE,
};

/* This is a bit pattern to be set for each mode
 * bit 0 - sta mode
 * bit 1 - ap mode
 * bit 2 - p2p client mode
 * bit 3 - p2p go mode */
enum wcn36xx_hal_concurrency_mode {
	HAL_STA = 1,
	HAL_SAP = 2,

	/* to support sta, softAp  mode . This means STA+AP mode */
	HAL_STA_SAP = 3,

	HAL_P2P_CLIENT = 4,
	HAL_P2P_GO = 8,
	HAL_MAX_CONCURRENCY_PERSONA = 4
};

struct wcn36xx_hal_config_bss_params {
	/* BSSID */
	u8 bssid[ETH_ALEN];

	/* Self Mac Address */
	u8 self_mac_addr[ETH_ALEN];

	/* BSS type */
	enum wcn36xx_hal_bss_type bss_type;

	/* Operational Mode: AP =0, STA = 1 */
	u8 oper_mode;

	/* Network Type */
	enum wcn36xx_hal_nw_type nw_type;

	/* Used to classify PURE_11G/11G_MIXED to program MTU */
	u8 short_slot_time_supported;

	/* Co-exist with 11a STA */
	u8 lla_coexist;

	/* Co-exist with 11b STA */
	u8 llb_coexist;

	/* Co-exist with 11g STA */
	u8 llg_coexist;

	/* Coexistence with 11n STA */
	u8 ht20_coexist;

	/* Non GF coexist flag */
	u8 lln_non_gf_coexist;

	/* TXOP protection support */
	u8 lsig_tx_op_protection_full_support;

	/* RIFS mode */
	u8 rifs_mode;

	/* Beacon Interval in TU */
	u16 beacon_interval;

	/* DTIM period */
	u8 dtim_period;

	/* TX Width Set: 0 - 20 MHz only, 1 - 20/40 MHz */
	u8 tx_channel_width_set;

	/* Operating channel */
	u8 oper_channel;

	/* Extension channel for channel bonding */
	u8 ext_channel;

	/* Reserved to align next field on a dword boundary */
	u8 reserved;

	/* TODO move sta to the end for 3680 */
	/* Context of the station being added in HW
	 *  Add a STA entry for "itself" -
	 *
	 *  On AP  - Add the AP itself in an "STA context"
	 *
	 *  On STA - Add the AP to which this STA is joining in an
	 *  "STA context"
	 */
	struct wcn36xx_hal_config_sta_params sta;
	/* SSID of the BSS */
	struct wcn36xx_hal_mac_ssid ssid;

	/* HAL should update the existing BSS entry, if this flag is set.
	 * UMAC will set this flag in case of reassoc, where we want to
	 * resue the the old BSSID and still return success 0 = Add, 1 =
	 * Update */
	u8 action;

	/* MAC Rate Set */
	struct wcn36xx_hal_rate_set rateset;

	/* Enable/Disable HT capabilities of the BSS */
	u8 ht;

	/* Enable/Disable OBSS protection */
	u8 obss_prot_enabled;

	/* RMF enabled/disabled */
	u8 rmf;

	/* HT Operating Mode operating mode of the 802.11n STA */
	enum wcn36xx_hal_ht_operating_mode ht_oper_mode;

	/* Dual CTS Protection: 0 - Unused, 1 - Used */
	u8 dual_cts_protection;

	/* Probe Response Max retries */
	u8 max_probe_resp_retry_limit;

	/* To Enable Hidden ssid */
	u8 hidden_ssid;

	/* To Enable Disable FW Proxy Probe Resp */
	u8 proxy_probe_resp;

	/* Boolean to indicate if EDCA params are valid. UMAC might not
	 * have valid EDCA params or might not desire to apply EDCA params
	 * during config BSS. 0 implies Not Valid ; Non-Zero implies
	 * valid */
	u8 edca_params_valid;

	/* EDCA Parameters for Best Effort Access Category */
	struct wcn36xx_hal_edca_param_record acbe;

	/* EDCA Parameters forBackground Access Category */
	struct wcn36xx_hal_edca_param_record acbk;

	/* EDCA Parameters for Video Access Category */
	struct wcn36xx_hal_edca_param_record acvi;

	/* EDCA Parameters for Voice Access Category */
	struct wcn36xx_hal_edca_param_record acvo;

	/* Ext Bss Config Msg if set */
	u8 ext_set_sta_key_param_valid;

	/* SetStaKeyParams for ext bss msg */
	struct wcn36xx_hal_set_sta_key_params ext_set_sta_key_param;

	/* Persona for the BSS can be STA,AP,GO,CLIENT value same as enum
	 * wcn36xx_hal_con_mode */
	u8 wcn36xx_hal_persona;

	u8 spectrum_mgt_enable;

	/* HAL fills in the tx power used for mgmt frames in txMgmtPower */
	s8 tx_mgmt_power;

	/* maxTxPower has max power to be used after applying the power
	 * constraint if any */
	s8 max_tx_power;
} __packed;

struct wcn36xx_hal_config_bss_req_msg {
	struct wcn36xx_hal_msg_header header;
	struct wcn36xx_hal_config_bss_params bss_params;
} __packed;

struct wcn36xx_hal_config_bss_params_v1 {
	/* BSSID */
	u8 bssid[ETH_ALEN];

	/* Self Mac Address */
	u8 self_mac_addr[ETH_ALEN];

	/* BSS type */
	enum wcn36xx_hal_bss_type bss_type;

	/* Operational Mode: AP =0, STA = 1 */
	u8 oper_mode;

	/* Network Type */
	enum wcn36xx_hal_nw_type nw_type;

	/* Used to classify PURE_11G/11G_MIXED to program MTU */
	u8 short_slot_time_supported;

	/* Co-exist with 11a STA */
	u8 lla_coexist;

	/* Co-exist with 11b STA */
	u8 llb_coexist;

	/* Co-exist with 11g STA */
	u8 llg_coexist;

	/* Coexistence with 11n STA */
	u8 ht20_coexist;

	/* Non GF coexist flag */
	u8 lln_non_gf_coexist;

	/* TXOP protection support */
	u8 lsig_tx_op_protection_full_support;

	/* RIFS mode */
	u8 rifs_mode;

	/* Beacon Interval in TU */
	u16 beacon_interval;

	/* DTIM period */
	u8 dtim_period;

	/* TX Width Set: 0 - 20 MHz only, 1 - 20/40 MHz */
	u8 tx_channel_width_set;

	/* Operating channel */
	u8 oper_channel;

	/* Extension channel for channel bonding */
	u8 ext_channel;

	/* Reserved to align next field on a dword boundary */
	u8 reserved;

	/* SSID of the BSS */
	struct wcn36xx_hal_mac_ssid ssid;

	/* HAL should update the existing BSS entry, if this flag is set.
	 * UMAC will set this flag in case of reassoc, where we want to
	 * resue the the old BSSID and still return success 0 = Add, 1 =
	 * Update */
	u8 action;

	/* MAC Rate Set */
	struct wcn36xx_hal_rate_set rateset;

	/* Enable/Disable HT capabilities of the BSS */
	u8 ht;

	/* Enable/Disable OBSS protection */
	u8 obss_prot_enabled;

	/* RMF enabled/disabled */
	u8 rmf;

	/* HT Operating Mode operating mode of the 802.11n STA */
	enum wcn36xx_hal_ht_operating_mode ht_oper_mode;

	/* Dual CTS Protection: 0 - Unused, 1 - Used */
	u8 dual_cts_protection;

	/* Probe Response Max retries */
	u8 max_probe_resp_retry_limit;

	/* To Enable Hidden ssid */
	u8 hidden_ssid;

	/* To Enable Disable FW Proxy Probe Resp */
	u8 proxy_probe_resp;

	/* Boolean to indicate if EDCA params are valid. UMAC might not
	 * have valid EDCA params or might not desire to apply EDCA params
	 * during config BSS. 0 implies Not Valid ; Non-Zero implies
	 * valid */
	u8 edca_params_valid;

	/* EDCA Parameters for Best Effort Access Category */
	struct wcn36xx_hal_edca_param_record acbe;

	/* EDCA Parameters forBackground Access Category */
	struct wcn36xx_hal_edca_param_record acbk;

	/* EDCA Parameters for Video Access Category */
	struct wcn36xx_hal_edca_param_record acvi;

	/* EDCA Parameters for Voice Access Category */
	struct wcn36xx_hal_edca_param_record acvo;

	/* Ext Bss Config Msg if set */
	u8 ext_set_sta_key_param_valid;

	/* SetStaKeyParams for ext bss msg */
	struct wcn36xx_hal_set_sta_key_params ext_set_sta_key_param;

	/* Persona for the BSS can be STA,AP,GO,CLIENT value same as enum
	 * wcn36xx_hal_con_mode */
	u8 wcn36xx_hal_persona;

	u8 spectrum_mgt_enable;

	/* HAL fills in the tx power used for mgmt frames in txMgmtPower */
	s8 tx_mgmt_power;

	/* maxTxPower has max power to be used after applying the power
	 * constraint if any */
	s8 max_tx_power;

	/* Context of the station being added in HW
	 *  Add a STA entry for "itself" -
	 *
	 *  On AP  - Add the AP itself in an "STA context"
	 *
	 *  On STA - Add the AP to which this STA is joining in an
	 *  "STA context"
	 */
	struct wcn36xx_hal_config_sta_params_v1 sta;

	u8 vht_capable;
	u8 vht_tx_channel_width_set;

} __packed;

#define WCN36XX_DIFF_BSS_PARAMS_V1_NOVHT (WCN36XX_DIFF_STA_PARAMS_V1_NOVHT + 2)

struct wcn36xx_hal_config_bss_req_msg_v1 {
	struct wcn36xx_hal_msg_header header;
	struct wcn36xx_hal_config_bss_params_v1 bss_params;
} __packed;

struct wcn36xx_hal_config_bss_rsp_params {
	/* Success or Failure */
	u32 status;

	/* BSS index allocated by HAL */
	u8 bss_index;

	/* DPU descriptor index for PTK */
	u8 dpu_desc_index;

	/* PTK DPU signature */
	u8 ucast_dpu_signature;

	/* DPU descriptor index for GTK */
	u8 bcast_dpu_desc_indx;

	/* GTK DPU signature */
	u8 bcast_dpu_signature;

	/* DPU descriptor for IGTK */
	u8 mgmt_dpu_desc_index;

	/* IGTK DPU signature */
	u8 mgmt_dpu_signature;

	/* Station Index for BSS entry */
	u8 bss_sta_index;

	/* Self station index for this BSS */
	u8 bss_self_sta_index;

	/* Bcast station for buffering bcast frames in AP role */
	u8 bss_bcast_sta_idx;

	/* MAC Address of STA(PEER/SELF) in staContext of configBSSReq */
	u8 mac[ETH_ALEN];

	/* HAL fills in the tx power used for mgmt frames in this field. */
	s8 tx_mgmt_power;

} __packed;

struct wcn36xx_hal_config_bss_rsp_msg {
	struct wcn36xx_hal_msg_header header;
	struct wcn36xx_hal_config_bss_rsp_params bss_rsp_params;
} __packed;

struct wcn36xx_hal_delete_bss_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* BSS index to be deleted */
	u8 bss_index;

} __packed;

struct wcn36xx_hal_delete_bss_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* Success or Failure */
	u32 status;

	/* BSS index that has been deleted */
	u8 bss_index;

} __packed;

struct wcn36xx_hal_join_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* Indicates the BSSID to which STA is going to associate */
	u8 bssid[ETH_ALEN];

	/* Indicates the channel to switch to. */
	u8 channel;

	/* Self STA MAC */
	u8 self_sta_mac_addr[ETH_ALEN];

	/* Local power constraint */
	u8 local_power_constraint;

	/* Secondary channel offset */
	enum phy_chan_bond_state secondary_channel_offset;

	/* link State */
	enum wcn36xx_hal_link_state link_state;

	/* Max TX power */
	s8 max_tx_power;
} __packed;

struct wcn36xx_hal_join_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;

	/* HAL fills in the tx power used for mgmt frames in this field */
	u8 tx_mgmt_power;
} __packed;

struct post_assoc_req_msg {
	struct wcn36xx_hal_msg_header header;

	struct wcn36xx_hal_config_sta_params sta_params;
	struct wcn36xx_hal_config_bss_params bss_params;
};

struct post_assoc_rsp_msg {
	struct wcn36xx_hal_msg_header header;
	struct config_sta_rsp_params sta_rsp_params;
	struct wcn36xx_hal_config_bss_rsp_params bss_rsp_params;
};

/* This is used to create a set of WEP keys for a given BSS. */
struct wcn36xx_hal_set_bss_key_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* BSS Index of the BSS */
	u8 bss_idx;

	/* Encryption Type used with peer */
	enum ani_ed_type enc_type;

	/* Number of keys */
	u8 num_keys;

	/* Array of keys. */
	struct wcn36xx_hal_keys keys[WCN36XX_HAL_MAC_MAX_NUM_OF_DEFAULT_KEYS];

	/* Control for Replay Count, 1= Single TID based replay count on Tx
	 * 0 = Per TID based replay count on TX */
	u8 single_tid_rc;
} __packed;

/* tagged version of set bss key */
struct wcn36xx_hal_set_bss_key_req_msg_tagged {
	struct wcn36xx_hal_set_bss_key_req_msg Msg;
	u32 tag;
} __packed;

struct wcn36xx_hal_set_bss_key_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
} __packed;

/*
 * This is used  configure the key information on a given station.
 * When the sec_type is WEP40 or WEP104, the def_wep_idx is used to locate
 * a preconfigured key from a BSS the station associated with; otherwise
 * a new key descriptor is created based on the key field.
 */
struct wcn36xx_hal_set_sta_key_req_msg {
	struct wcn36xx_hal_msg_header header;
	struct wcn36xx_hal_set_sta_key_params set_sta_key_params;
} __packed;

struct wcn36xx_hal_set_sta_key_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
} __packed;

struct wcn36xx_hal_remove_bss_key_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* BSS Index of the BSS */
	u8 bss_idx;

	/* Encryption Type used with peer */
	enum ani_ed_type enc_type;

	/* Key Id */
	u8 key_id;

	/* STATIC/DYNAMIC. Used in Nullifying in Key Descriptors for
	 * Static/Dynamic keys */
	enum ani_wep_type wep_type;
} __packed;

struct wcn36xx_hal_remove_bss_key_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
} __packed;

/*
 * This is used by PE to Remove the key information on a given station.
 */
struct wcn36xx_hal_remove_sta_key_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* STA Index */
	u16 sta_idx;

	/* Encryption Type used with peer */
	enum ani_ed_type enc_type;

	/* Key Id */
	u8 key_id;

	/* Whether to invalidate the Broadcast key or Unicast key. In case
	 * of WEP, the same key is used for both broadcast and unicast. */
	u8 unicast;

} __packed;

struct wcn36xx_hal_remove_sta_key_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/*success or failure */
	u32 status;

} __packed;

#ifdef FEATURE_OEM_DATA_SUPPORT

#ifndef OEM_DATA_REQ_SIZE
#define OEM_DATA_REQ_SIZE 134
#endif

#ifndef OEM_DATA_RSP_SIZE
#define OEM_DATA_RSP_SIZE 1968
#endif

struct start_oem_data_req_msg {
	struct wcn36xx_hal_msg_header header;

	u32 status;
	tSirMacAddr self_mac_addr;
	u8 oem_data_req[OEM_DATA_REQ_SIZE];

};

struct start_oem_data_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	u8 oem_data_rsp[OEM_DATA_RSP_SIZE];
};

#endif

struct wcn36xx_hal_switch_channel_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* Channel number */
	u8 channel_number;

	/* Local power constraint */
	u8 local_power_constraint;

	/* Secondary channel offset */
	enum phy_chan_bond_state secondary_channel_offset;

	/* HAL fills in the tx power used for mgmt frames in this field. */
	u8 tx_mgmt_power;

	/* Max TX power */
	u8 max_tx_power;

	/* Self STA MAC */
	u8 self_sta_mac_addr[ETH_ALEN];

	/* VO WIFI comment: BSSID needed to identify session. As the
	 * request has power constraints, this should be applied only to
	 * that session Since MTU timing and EDCA are sessionized, this
	 * struct needs to be sessionized and bssid needs to be out of the
	 * VOWifi feature flag V IMP: Keep bssId field at the end of this
	 * msg. It is used to mantain backward compatbility by way of
	 * ignoring if using new host/old FW or old host/new FW since it is
	 * at the end of this struct
	 */
	u8 bssid[ETH_ALEN];
} __packed;

struct wcn36xx_hal_switch_channel_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* Status */
	u32 status;

	/* Channel number - same as in request */
	u8 channel_number;

	/* HAL fills in the tx power used for mgmt frames in this field */
	u8 tx_mgmt_power;

	/* BSSID needed to identify session - same as in request */
	u8 bssid[ETH_ALEN];

} __packed;

struct wcn36xx_hal_process_ptt_msg_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* Actual FTM Command body */
	u8 ptt_msg[];
} __packed;

struct wcn36xx_hal_process_ptt_msg_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* FTM Command response status */
	u32 ptt_msg_resp_status;
	/* Actual FTM Command body */
	u8 ptt_msg[];
} __packed;

struct update_edca_params_req_msg {
	struct wcn36xx_hal_msg_header header;

	/*BSS Index */
	u16 bss_index;

	/* Best Effort */
	struct wcn36xx_hal_edca_param_record acbe;

	/* Background */
	struct wcn36xx_hal_edca_param_record acbk;

	/* Video */
	struct wcn36xx_hal_edca_param_record acvi;

	/* Voice */
	struct wcn36xx_hal_edca_param_record acvo;
};

struct update_edca_params_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
};

struct dpu_stats_params {
	/* Index of STA to which the statistics */
	u16 sta_index;

	/* Encryption mode */
	u8 enc_mode;

	/* status */
	u32 status;

	/* Statistics */
	u32 send_blocks;
	u32 recv_blocks;
	u32 replays;
	u8 mic_error_cnt;
	u32 prot_excl_cnt;
	u16 format_err_cnt;
	u16 un_decryptable_cnt;
	u32 decrypt_err_cnt;
	u32 decrypt_ok_cnt;
};

struct wcn36xx_hal_stats_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* Valid STA Idx for per STA stats request */
	u32 sta_id;

	/* Categories of stats requested as specified in eHalStatsMask */
	u32 stats_mask;
};

struct ani_summary_stats_info {
	/* Total number of packets(per AC) that were successfully
	 * transmitted with retries */
	u32 retry_cnt[4];

	/* The number of MSDU packets and MMPDU frames per AC that the
	 * 802.11 station successfully transmitted after more than one
	 * retransmission attempt */
	u32 multiple_retry_cnt[4];

	/* Total number of packets(per AC) that were successfully
	 * transmitted (with and without retries, including multi-cast,
	 * broadcast) */
	u32 tx_frm_cnt[4];

	/* Total number of packets that were successfully received (after
	 * appropriate filter rules including multi-cast, broadcast) */
	u32 rx_frm_cnt;

	/* Total number of duplicate frames received successfully */
	u32 frm_dup_cnt;

	/* Total number packets(per AC) failed to transmit */
	u32 fail_cnt[4];

	/* Total number of RTS/CTS sequence failures for transmission of a
	 * packet */
	u32 rts_fail_cnt;

	/* Total number packets failed transmit because of no ACK from the
	 * remote entity */
	u32 ack_fail_cnt;

	/* Total number of RTS/CTS sequence success for transmission of a
	 * packet */
	u32 rts_succ_cnt;

	/* The sum of the receive error count and dropped-receive-buffer
	 * error count. HAL will provide this as a sum of (FCS error) +
	 * (Fail get BD/PDU in HW) */
	u32 rx_discard_cnt;

	/*
	 * The receive error count. HAL will provide the RxP FCS error
	 * global counter. */
	u32 rx_error_cnt;

	/* The sum of the transmit-directed byte count, transmit-multicast
	 * byte count and transmit-broadcast byte count. HAL will sum TPE
	 * UC/MC/BCAST global counters to provide this. */
	u32 tx_byte_cnt;
};

/* defines tx_rate_flags */
enum tx_rate_info {
	/* Legacy rates */
	HAL_TX_RATE_LEGACY = 0x1,

	/* HT20 rates */
	HAL_TX_RATE_HT20 = 0x2,

	/* HT40 rates */
	HAL_TX_RATE_HT40 = 0x4,

	/* Rate with Short guard interval */
	HAL_TX_RATE_SGI = 0x8,

	/* Rate with Long guard interval */
	HAL_TX_RATE_LGI = 0x10
};

struct ani_global_class_a_stats_info {
	/* The number of MPDU frames received by the 802.11 station for
	 * MSDU packets or MMPDU frames */
	u32 rx_frag_cnt;

	/* The number of MPDU frames received by the 802.11 station for
	 * MSDU packets or MMPDU frames when a promiscuous packet filter
	 * was enabled */
	u32 promiscuous_rx_frag_cnt;

	/* The receiver input sensitivity referenced to a FER of 8% at an
	 * MPDU length of 1024 bytes at the antenna connector. Each element
	 * of the array shall correspond to a supported rate and the order
	 * shall be the same as the supporteRates parameter. */
	u32 rx_input_sensitivity;

	/* The maximum transmit power in dBm upto one decimal. for eg: if
	 * it is 10.5dBm, the value would be 105 */
	u32 max_pwr;

	/* Number of times the receiver failed to synchronize with the
	 * incoming signal after detecting the sync in the preamble of the
	 * transmitted PLCP protocol data unit. */
	u32 sync_fail_cnt;

	/* Legacy transmit rate, in units of 500 kbit/sec, for the most
	 * recently transmitted frame */
	u32 tx_rate;

	/* mcs index for HT20 and HT40 rates */
	u32 mcs_index;

	/* to differentiate between HT20 and HT40 rates; short and long
	 * guard interval */
	u32 tx_rate_flags;
};

struct ani_global_security_stats {
	/* The number of unencrypted received MPDU frames that the MAC
	 * layer discarded when the IEEE 802.11 dot11ExcludeUnencrypted
	 * management information base (MIB) object is enabled */
	u32 rx_wep_unencrypted_frm_cnt;

	/* The number of received MSDU packets that that the 802.11 station
	 * discarded because of MIC failures */
	u32 rx_mic_fail_cnt;

	/* The number of encrypted MPDU frames that the 802.11 station
	 * failed to decrypt because of a TKIP ICV error */
	u32 tkip_icv_err;

	/* The number of received MPDU frames that the 802.11 discarded
	 * because of an invalid AES-CCMP format */
	u32 aes_ccmp_format_err;

	/* The number of received MPDU frames that the 802.11 station
	 * discarded because of the AES-CCMP replay protection procedure */
	u32 aes_ccmp_replay_cnt;

	/* The number of received MPDU frames that the 802.11 station
	 * discarded because of errors detected by the AES-CCMP decryption
	 * algorithm */
	u32 aes_ccmp_decrpt_err;

	/* The number of encrypted MPDU frames received for which a WEP
	 * decryption key was not available on the 802.11 station */
	u32 wep_undecryptable_cnt;

	/* The number of encrypted MPDU frames that the 802.11 station
	 * failed to decrypt because of a WEP ICV error */
	u32 wep_icv_err;

	/* The number of received encrypted packets that the 802.11 station
	 * successfully decrypted */
	u32 rx_decrypt_succ_cnt;

	/* The number of encrypted packets that the 802.11 station failed
	 * to decrypt */
	u32 rx_decrypt_fail_cnt;
};

struct ani_global_class_b_stats_info {
	struct ani_global_security_stats uc_stats;
	struct ani_global_security_stats mc_bc_stats;
};

struct ani_global_class_c_stats_info {
	/* This counter shall be incremented for a received A-MSDU frame
	 * with the stations MAC address in the address 1 field or an
	 * A-MSDU frame with a group address in the address 1 field */
	u32 rx_amsdu_cnt;

	/* This counter shall be incremented when the MAC receives an AMPDU
	 * from the PHY */
	u32 rx_ampdu_cnt;

	/* This counter shall be incremented when a Frame is transmitted
	 * only on the primary channel */
	u32 tx_20_frm_cnt;

	/* This counter shall be incremented when a Frame is received only
	 * on the primary channel */
	u32 rx_20_frm_cnt;

	/* This counter shall be incremented by the number of MPDUs
	 * received in the A-MPDU when an A-MPDU is received */
	u32 rx_mpdu_in_ampdu_cnt;

	/* This counter shall be incremented when an MPDU delimiter has a
	 * CRC error when this is the first CRC error in the received AMPDU
	 * or when the previous delimiter has been decoded correctly */
	u32 ampdu_delimiter_crc_err;
};

struct ani_per_sta_stats_info {
	/* The number of MPDU frames that the 802.11 station transmitted
	 * and acknowledged through a received 802.11 ACK frame */
	u32 tx_frag_cnt[4];

	/* This counter shall be incremented when an A-MPDU is transmitted */
	u32 tx_ampdu_cnt;

	/* This counter shall increment by the number of MPDUs in the AMPDU
	 * when an A-MPDU is transmitted */
	u32 tx_mpdu_in_ampdu_cnt;
};

struct wcn36xx_hal_stats_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* Success or Failure */
	u32 status;

	/* STA Idx */
	u32 sta_index;

	/* Categories of STATS being returned as per eHalStatsMask */
	u32 stats_mask;

	/* message type is same as the request type */
	u16 msg_type;

	/* length of the entire request, includes the pStatsBuf length too */
	u16 msg_len;
};

struct wcn36xx_hal_set_link_state_req_msg {
	struct wcn36xx_hal_msg_header header;

	u8 bssid[ETH_ALEN];
	enum wcn36xx_hal_link_state state;
	u8 self_mac_addr[ETH_ALEN];

} __packed;

struct set_link_state_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
};

/* TSPEC Params */
struct wcn36xx_hal_ts_info_tfc {
#ifndef ANI_LITTLE_BIT_ENDIAN
	u16 ackPolicy:2;
	u16 userPrio:3;
	u16 psb:1;
	u16 aggregation:1;
	u16 accessPolicy:2;
	u16 direction:2;
	u16 tsid:4;
	u16 trafficType:1;
#else
	u16 trafficType:1;
	u16 tsid:4;
	u16 direction:2;
	u16 accessPolicy:2;
	u16 aggregation:1;
	u16 psb:1;
	u16 userPrio:3;
	u16 ackPolicy:2;
#endif
};

/* Flag to schedule the traffic type */
struct wcn36xx_hal_ts_info_sch {
#ifndef ANI_LITTLE_BIT_ENDIAN
	u8 rsvd:7;
	u8 schedule:1;
#else
	u8 schedule:1;
	u8 rsvd:7;
#endif
};

/* Traffic and scheduling info */
struct wcn36xx_hal_ts_info {
	struct wcn36xx_hal_ts_info_tfc traffic;
	struct wcn36xx_hal_ts_info_sch schedule;
};

/* Information elements */
struct wcn36xx_hal_tspec_ie {
	u8 type;
	u8 length;
	struct wcn36xx_hal_ts_info ts_info;
	u16 nom_msdu_size;
	u16 max_msdu_size;
	u32 min_svc_interval;
	u32 max_svc_interval;
	u32 inact_interval;
	u32 suspend_interval;
	u32 svc_start_time;
	u32 min_data_rate;
	u32 mean_data_rate;
	u32 peak_data_rate;
	u32 max_burst_sz;
	u32 delay_bound;
	u32 min_phy_rate;
	u16 surplus_bw;
	u16 medium_time;
};

struct add_ts_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* Station Index */
	u16 sta_index;

	/* TSPEC handler uniquely identifying a TSPEC for a STA in a BSS */
	u16 tspec_index;

	/* To program TPE with required parameters */
	struct wcn36xx_hal_tspec_ie tspec;

	/* U-APSD Flags: 1b per AC.  Encoded as follows:
	   b7 b6 b5 b4 b3 b2 b1 b0 =
	   X  X  X  X  BE BK VI VO */
	u8 uapsd;

	/* These parameters are for all the access categories */

	/* Service Interval */
	u32 service_interval[WCN36XX_HAL_MAX_AC];

	/* Suspend Interval */
	u32 suspend_interval[WCN36XX_HAL_MAX_AC];

	/* Delay Interval */
	u32 delay_interval[WCN36XX_HAL_MAX_AC];
};

struct add_rs_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
};

struct del_ts_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* Station Index */
	u16 sta_index;

	/* TSPEC identifier uniquely identifying a TSPEC for a STA in a BSS */
	u16 tspec_index;

	/* To lookup station id using the mac address */
	u8 bssid[ETH_ALEN];
};

struct del_ts_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
};

/* End of TSpec Parameters */

/* Start of BLOCK ACK related Parameters */

struct wcn36xx_hal_add_ba_session_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* Station Index */
	u16 sta_index;

	/* Peer MAC Address */
	u8 mac_addr[ETH_ALEN];

	/* ADDBA Action Frame dialog token
	   HAL will not interpret this object */
	u8 dialog_token;

	/* TID for which the BA is being setup
	   This identifies the TC or TS of interest */
	u8 tid;

	/* 0 - Delayed BA (Not supported)
	   1 - Immediate BA */
	u8 policy;

	/* Indicates the number of buffers for this TID (baTID)
	   NOTE - This is the requested buffer size. When this
	   is processed by HAL and subsequently by HDD, it is
	   possible that HDD may change this buffer size. Any
	   change in the buffer size should be noted by PE and
	   advertized appropriately in the ADDBA response */
	u16 buffer_size;

	/* BA timeout in TU's 0 means no timeout will occur */
	u16 timeout;

	/* b0..b3 - Fragment Number - Always set to 0
	   b4..b15 - Starting Sequence Number of first MSDU
	   for which this BA is setup */
	u16 ssn;

	/* ADDBA direction
	   1 - Originator
	   0 - Recipient */
	u8 direction;
} __packed;

struct wcn36xx_hal_add_ba_session_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;

	/* Dialog token */
	u8 dialog_token;

	/* TID for which the BA session has been setup */
	u8 ba_tid;

	/* BA Buffer Size allocated for the current BA session */
	u8 ba_buffer_size;

	u8 ba_session_id;

	/* Reordering Window buffer */
	u8 win_size;

	/* Station Index to id the sta */
	u8 sta_index;

	/* Starting Sequence Number */
	u16 ssn;
} __packed;

struct wcn36xx_hal_add_ba_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* Session Id */
	u8 session_id;

	/* Reorder Window Size */
	u8 win_size;
/* Old FW 1.2.2.4 does not support this*/
#ifdef FEATURE_ON_CHIP_REORDERING
	u8 reordering_done_on_chip;
#endif
} __packed;

struct wcn36xx_hal_add_ba_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;

	/* Dialog token */
	u8 dialog_token;
} __packed;

struct add_ba_info {
	u16 ba_enable:1;
	u16 starting_seq_num:12;
	u16 reserved:3;
};

struct wcn36xx_hal_trigger_ba_rsp_candidate {
	u8 sta_addr[ETH_ALEN];
	struct add_ba_info ba_info[STACFG_MAX_TC];
} __packed;

struct wcn36xx_hal_trigger_ba_req_candidate {
	u8 sta_index;
	u8 tid_bitmap;
} __packed;

struct wcn36xx_hal_trigger_ba_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* Session Id */
	u8 session_id;

	/* baCandidateCnt is followed by trigger BA
	 * Candidate List(tTriggerBaCandidate)
	 */
	u16 candidate_cnt;

} __packed;

struct wcn36xx_hal_trigger_ba_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* TO SUPPORT BT-AMP */
	u8 bssid[ETH_ALEN];

	/* success or failure */
	u32 status;

	/* baCandidateCnt is followed by trigger BA
	 * Rsp Candidate List(tTriggerRspBaCandidate)
	 */
	u16 candidate_cnt;
} __packed;

struct wcn36xx_hal_del_ba_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* Station Index */
	u16 sta_index;

	/* TID for which the BA session is being deleted */
	u8 tid;

	/* DELBA direction
	   1 - Originator
	   0 - Recipient */
	u8 direction;
} __packed;

struct wcn36xx_hal_del_ba_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
} __packed;

struct tsm_stats_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* Traffic Id */
	u8 tid;

	u8 bssid[ETH_ALEN];
};

struct tsm_stats_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/*success or failure */
	u32 status;

	/* Uplink Packet Queue delay */
	u16 uplink_pkt_queue_delay;

	/* Uplink Packet Queue delay histogram */
	u16 uplink_pkt_queue_delay_hist[4];

	/* Uplink Packet Transmit delay */
	u32 uplink_pkt_tx_delay;

	/* Uplink Packet loss */
	u16 uplink_pkt_loss;

	/* Uplink Packet count */
	u16 uplink_pkt_count;

	/* Roaming count */
	u8 roaming_count;

	/* Roaming Delay */
	u16 roaming_delay;
};

struct set_key_done_msg {
	struct wcn36xx_hal_msg_header header;

	/*bssid of the keys */
	u8 bssidx;
	u8 enc_type;
};

struct wcn36xx_hal_nv_img_download_req_msg {
	/* Note: The length specified in wcn36xx_hal_nv_img_download_req_msg
	 * messages should be
	 * header.len = sizeof(wcn36xx_hal_nv_img_download_req_msg) +
	 * nv_img_buffer_size */
	struct wcn36xx_hal_msg_header header;

	/* Fragment sequence number of the NV Image. Note that NV Image
	 * might not fit into one message due to size limitation of the SMD
	 * channel FIFO. UMAC can hence choose to chop the NV blob into
	 * multiple fragments starting with seqeunce number 0, 1, 2 etc.
	 * The last fragment MUST be indicated by marking the
	 * isLastFragment field to 1. Note that all the NV blobs would be
	 * concatenated together by HAL without any padding bytes in
	 * between.*/
	u16 frag_number;

	/* Is this the last fragment? When set to 1 it indicates that no
	 * more fragments will be sent by UMAC and HAL can concatenate all
	 * the NV blobs rcvd & proceed with the parsing. HAL would generate
	 * a WCN36XX_HAL_DOWNLOAD_NV_RSP to the WCN36XX_HAL_DOWNLOAD_NV_REQ
	 * after it receives each fragment */
	u16 last_fragment;

	/* NV Image size (number of bytes) */
	u32 nv_img_buffer_size;

	/* Following the 'nv_img_buffer_size', there should be
	 * nv_img_buffer_size bytes of NV Image i.e.
	 * u8[nv_img_buffer_size] */
} __packed;

struct wcn36xx_hal_nv_img_download_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* Success or Failure. HAL would generate a
	 * WCN36XX_HAL_DOWNLOAD_NV_RSP after each fragment */
	u32 status;
} __packed;

struct wcn36xx_hal_nv_store_ind {
	/* Note: The length specified in tHalNvStoreInd messages should be
	 * header.msgLen = sizeof(tHalNvStoreInd) + nvBlobSize */
	struct wcn36xx_hal_msg_header header;

	/* NV Item */
	u32 table_id;

	/* Size of NV Blob */
	u32 nv_blob_size;

	/* Following the 'nvBlobSize', there should be nvBlobSize bytes of
	 * NV blob i.e. u8[nvBlobSize] */
};

/* End of Block Ack Related Parameters */

#define WCN36XX_HAL_CIPHER_SEQ_CTR_SIZE 6

/* Definition for MIC failure indication MAC reports this each time a MIC
 * failure occures on Rx TKIP packet
 */
struct mic_failure_ind_msg {
	struct wcn36xx_hal_msg_header header;

	u8 bssid[ETH_ALEN];

	/* address used to compute MIC */
	u8 src_addr[ETH_ALEN];

	/* transmitter address */
	u8 ta_addr[ETH_ALEN];

	u8 dst_addr[ETH_ALEN];

	u8 multicast;

	/* first byte of IV */
	u8 iv1;

	/* second byte of IV */
	u8 key_id;

	/* sequence number */
	u8 tsc[WCN36XX_HAL_CIPHER_SEQ_CTR_SIZE];

	/* receive address */
	u8 rx_addr[ETH_ALEN];
};

struct update_vht_op_mode_req_msg {
	struct wcn36xx_hal_msg_header header;

	u16 op_mode;
	u16 sta_id;
};

struct update_vht_op_mode_params_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	u32 status;
};

struct update_beacon_req_msg {
	struct wcn36xx_hal_msg_header header;

	u8 bss_index;

	/* shortPreamble mode. HAL should update all the STA rates when it
	 * receives this message */
	u8 short_preamble;

	/* short Slot time. */
	u8 short_slot_time;

	/* Beacon Interval */
	u16 beacon_interval;

	/* Protection related */
	u8 lla_coexist;
	u8 llb_coexist;
	u8 llg_coexist;
	u8 ht20_coexist;
	u8 lln_non_gf_coexist;
	u8 lsig_tx_op_protection_full_support;
	u8 rifs_mode;

	u16 param_change_bitmap;
};

struct update_beacon_rsp_msg {
	struct wcn36xx_hal_msg_header header;
	u32 status;
};

struct wcn36xx_hal_send_beacon_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* length of the template + 6. Only qcom knows why */
	u32 beacon_length6;

	/* length of the template. */
	u32 beacon_length;

	/* Beacon data. */
	u8 beacon[BEACON_TEMPLATE_SIZE - sizeof(u32)];

	u8 bssid[ETH_ALEN];

	/* TIM IE offset from the beginning of the template. */
	u32 tim_ie_offset;

	/* P2P IE offset from the begining of the template */
	u16 p2p_ie_offset;
} __packed;

struct send_beacon_rsp_msg {
	struct wcn36xx_hal_msg_header header;
	u32 status;
} __packed;

struct enable_radar_req_msg {
	struct wcn36xx_hal_msg_header header;

	u8 bssid[ETH_ALEN];
	u8 channel;
};

struct enable_radar_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* Link Parameters */
	u8 bssid[ETH_ALEN];

	/* success or failure */
	u32 status;
};

struct radar_detect_intr_ind_msg {
	struct wcn36xx_hal_msg_header header;

	u8 radar_det_channel;
};

struct radar_detect_ind_msg {
	struct wcn36xx_hal_msg_header header;

	/* channel number in which the RADAR detected */
	u8 channel_number;

	/* RADAR pulse width in usecond */
	u16 radar_pulse_width;

	/* Number of RADAR pulses */
	u16 num_radar_pulse;
};

struct wcn36xx_hal_get_tpc_report_req_msg {
	struct wcn36xx_hal_msg_header header;

	u8 sta[ETH_ALEN];
	u8 dialog_token;
	u8 txpower;
};

struct wcn36xx_hal_get_tpc_report_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
};

struct wcn36xx_hal_send_probe_resp_req_msg {
	struct wcn36xx_hal_msg_header header;

	u8 probe_resp_template[BEACON_TEMPLATE_SIZE];
	u32 probe_resp_template_len;
	u32 proxy_probe_req_valid_ie_bmap[8];
	u8 bssid[ETH_ALEN];
};

struct send_probe_resp_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
};

struct send_unknown_frame_rx_ind_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
};

struct wcn36xx_hal_delete_sta_context_ind_msg {
	struct wcn36xx_hal_msg_header header;

	u16 aid;
	u16 sta_id;

	/* TO SUPPORT BT-AMP */
	u8 bssid[ETH_ALEN];

	/* HAL copies bssid from the sta table. */
	u8 addr2[ETH_ALEN];

	/* To unify the keepalive / unknown A2 / tim-based disa */
	u16 reason_code;
} __packed;

struct indicate_del_sta {
	struct wcn36xx_hal_msg_header header;
	u8 aid;
	u8 sta_index;
	u8 bss_index;
	u8 reason_code;
	u32 status;
};

struct bt_amp_event_msg {
	struct wcn36xx_hal_msg_header header;

	enum bt_amp_event_type btAmpEventType;
};

struct bt_amp_event_rsp {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
};

struct tl_hal_flush_ac_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* Station Index. originates from HAL */
	u8 sta_id;

	/* TID for which the transmit queue is being flushed */
	u8 tid;
};

struct tl_hal_flush_ac_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* Station Index. originates from HAL */
	u8 sta_id;

	/* TID for which the transmit queue is being flushed */
	u8 tid;

	/* success or failure */
	u32 status;
};

struct wcn36xx_hal_enter_imps_req_msg {
	struct wcn36xx_hal_msg_header header;
} __packed;

struct wcn36xx_hal_exit_imps_req_msg {
	struct wcn36xx_hal_msg_header header;
} __packed;

struct wcn36xx_hal_enter_bmps_req_msg {
	struct wcn36xx_hal_msg_header header;

	u8 bss_index;

	/* TBTT value derived from the last beacon */
#ifndef BUILD_QWPTTSTATIC
	u64 tbtt;
#endif
	u8 dtim_count;

	/* DTIM period given to HAL during association may not be valid, if
	 * association is based on ProbeRsp instead of beacon. */
	u8 dtim_period;

	/* For CCX and 11R Roaming */
	u32 rssi_filter_period;

	u32 num_beacon_per_rssi_average;
	u8 rssi_filter_enable;
} __packed;

struct wcn36xx_hal_exit_bmps_req_msg {
	struct wcn36xx_hal_msg_header header;

	u8 send_data_null;
	u8 bss_index;
} __packed;

struct wcn36xx_hal_missed_beacon_ind_msg {
	struct wcn36xx_hal_msg_header header;

	u8 bss_index;
} __packed;

/* Beacon Filtering data structures */

/* The above structure would be followed by multiple of below mentioned
 * structure
 */
struct beacon_filter_ie {
	u8 element_id;
	u8 check_ie_presence;
	u8 offset;
	u8 value;
	u8 bitmask;
	u8 ref;
};

struct wcn36xx_hal_add_bcn_filter_req_msg {
	struct wcn36xx_hal_msg_header header;

	u16 capability_info;
	u16 capability_mask;
	u16 beacon_interval;
	u16 ie_num;
	u8 bss_index;
	u8 reserved;
};

struct wcn36xx_hal_rem_bcn_filter_req {
	struct wcn36xx_hal_msg_header header;

	u8 ie_Count;
	u8 rem_ie_id[1];
};

#define WCN36XX_HAL_IPV4_ARP_REPLY_OFFLOAD                  0
#define WCN36XX_HAL_IPV6_NEIGHBOR_DISCOVERY_OFFLOAD         1
#define WCN36XX_HAL_IPV6_NS_OFFLOAD                         2
#define WCN36XX_HAL_IPV6_ADDR_LEN                           16
#define WCN36XX_HAL_OFFLOAD_DISABLE                         0
#define WCN36XX_HAL_OFFLOAD_ENABLE                          1
#define WCN36XX_HAL_OFFLOAD_BCAST_FILTER_ENABLE             0x2
#define WCN36XX_HAL_OFFLOAD_MCAST_FILTER_ENABLE             0x4
#define WCN36XX_HAL_OFFLOAD_NS_AND_MCAST_FILTER_ENABLE	\
	(WCN36XX_HAL_OFFLOAD_ENABLE | WCN36XX_HAL_OFFLOAD_MCAST_FILTER_ENABLE)
#define WCN36XX_HAL_OFFLOAD_ARP_AND_BCAST_FILTER_ENABLE	\
	(WCN36XX_HAL_OFFLOAD_ENABLE | WCN36XX_HAL_OFFLOAD_BCAST_FILTER_ENABLE)
#define WCN36XX_HAL_IPV6_OFFLOAD_ADDR_MAX		0x02

struct wcn36xx_hal_ns_offload_params {
	u8 src_ipv6_addr[WCN36XX_HAL_IPV6_ADDR_LEN];
	u8 self_ipv6_addr[WCN36XX_HAL_IPV6_ADDR_LEN];

	/* Only support 2 possible Network Advertisement IPv6 address */
	u8 target_ipv6_addr1[WCN36XX_HAL_IPV6_ADDR_LEN];
	u8 target_ipv6_addr2[WCN36XX_HAL_IPV6_ADDR_LEN];

	u8 self_addr[ETH_ALEN];
	u8 src_ipv6_addr_valid:1;
	u8 target_ipv6_addr1_valid:1;
	u8 target_ipv6_addr2_valid:1;
	u8 reserved1:5;

	/* make it DWORD aligned */
	u8 reserved2;

	/* slot index for this offload */
	u32 slot_index;
	u8 bss_index;
} __packed;

struct wcn36xx_hal_host_offload_req {
	u8 offload_type;

	/* enable or disable */
	u8 enable;

	union {
		u8 host_ipv4_addr[4];
		u8 host_ipv6_addr[WCN36XX_HAL_IPV6_ADDR_LEN];
	} u;
} __packed;

struct wcn36xx_hal_host_offload_req_msg {
	struct wcn36xx_hal_msg_header header;
	struct wcn36xx_hal_host_offload_req host_offload_params;
	struct wcn36xx_hal_ns_offload_params ns_offload_params;
} __packed;

/* Packet Types. */
#define WCN36XX_HAL_KEEP_ALIVE_NULL_PKT              1
#define WCN36XX_HAL_KEEP_ALIVE_UNSOLICIT_ARP_RSP     2

/* Enable or disable keep alive */
#define WCN36XX_HAL_KEEP_ALIVE_DISABLE   0
#define WCN36XX_HAL_KEEP_ALIVE_ENABLE    1
#define WCN36XX_KEEP_ALIVE_TIME_PERIOD	 30 /* unit: s */

/* Keep Alive request. */
struct wcn36xx_hal_keep_alive_req_msg {
	struct wcn36xx_hal_msg_header header;

	u8 packet_type;
	u32 time_period;
	u8 host_ipv4_addr[WCN36XX_HAL_IPV4_ADDR_LEN];
	u8 dest_ipv4_addr[WCN36XX_HAL_IPV4_ADDR_LEN];
	u8 dest_addr[ETH_ALEN];
	u8 bss_index;
} __packed;

struct wcn36xx_hal_rssi_threshold_req_msg {
	struct wcn36xx_hal_msg_header header;

	s8 threshold1:8;
	s8 threshold2:8;
	s8 threshold3:8;
	u8 thres1_pos_notify:1;
	u8 thres1_neg_notify:1;
	u8 thres2_pos_notify:1;
	u8 thres2_neg_notify:1;
	u8 thres3_pos_notify:1;
	u8 thres3_neg_notify:1;
	u8 reserved10:2;
};

struct wcn36xx_hal_enter_uapsd_req_msg {
	struct wcn36xx_hal_msg_header header;

	u8 bk_delivery:1;
	u8 be_delivery:1;
	u8 vi_delivery:1;
	u8 vo_delivery:1;
	u8 bk_trigger:1;
	u8 be_trigger:1;
	u8 vi_trigger:1;
	u8 vo_trigger:1;
	u8 bss_index;
};

struct wcn36xx_hal_exit_uapsd_req_msg {
	struct wcn36xx_hal_msg_header header;
	u8 bss_index;
};

#define WCN36XX_HAL_WOWL_BCAST_PATTERN_MAX_SIZE 128
#define WCN36XX_HAL_WOWL_BCAST_MAX_NUM_PATTERNS 16

struct wcn36xx_hal_wowl_add_bcast_ptrn_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* Pattern ID */
	u8 id;

	/* Pattern byte offset from beginning of the 802.11 packet to start
	 * of the wake-up pattern */
	u8 byte_Offset;

	/* Non-Zero Pattern size */
	u8 size;

	/* Pattern */
	u8 pattern[WCN36XX_HAL_WOWL_BCAST_PATTERN_MAX_SIZE];

	/* Non-zero pattern mask size */
	u8 mask_size;

	/* Pattern mask */
	u8 mask[WCN36XX_HAL_WOWL_BCAST_PATTERN_MAX_SIZE];

	/* Extra pattern */
	u8 extra[WCN36XX_HAL_WOWL_BCAST_PATTERN_MAX_SIZE];

	/* Extra pattern mask */
	u8 mask_extra[WCN36XX_HAL_WOWL_BCAST_PATTERN_MAX_SIZE];

	u8 bss_index;
};

struct wcn36xx_hal_wow_del_bcast_ptrn_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* Pattern ID of the wakeup pattern to be deleted */
	u8 id;
	u8 bss_index;
};

struct wcn36xx_hal_wowl_enter_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* Enables/disables magic packet filtering */
	u8 magic_packet_enable;

	/* Magic pattern */
	u8 magic_pattern[ETH_ALEN];

	/* Enables/disables packet pattern filtering in firmware. Enabling
	 * this flag enables broadcast pattern matching in Firmware. If
	 * unicast pattern matching is also desired,
	 * ucUcastPatternFilteringEnable flag must be set tot true as well
	 */
	u8 pattern_filtering_enable;

	/* Enables/disables unicast packet pattern filtering. This flag
	 * specifies whether we want to do pattern match on unicast packets
	 * as well and not just broadcast packets. This flag has no effect
	 * if the ucPatternFilteringEnable (main controlling flag) is set
	 * to false
	 */
	u8 ucast_pattern_filtering_enable;

	/* This configuration is valid only when magicPktEnable=1. It
	 * requests hardware to wake up when it receives the Channel Switch
	 * Action Frame.
	 */
	u8 wow_channel_switch_receive;

	/* This configuration is valid only when magicPktEnable=1. It
	 * requests hardware to wake up when it receives the
	 * Deauthentication Frame.
	 */
	u8 wow_deauth_receive;

	/* This configuration is valid only when magicPktEnable=1. It
	 * requests hardware to wake up when it receives the Disassociation
	 * Frame.
	 */
	u8 wow_disassoc_receive;

	/* This configuration is valid only when magicPktEnable=1. It
	 * requests hardware to wake up when it has missed consecutive
	 * beacons. This is a hardware register configuration (NOT a
	 * firmware configuration).
	 */
	u8 wow_max_missed_beacons;

	/* This configuration is valid only when magicPktEnable=1. This is
	 * a timeout value in units of microsec. It requests hardware to
	 * unconditionally wake up after it has stayed in WoWLAN mode for
	 * some time. Set 0 to disable this feature.
	 */
	u8 wow_max_sleep;

	/* This configuration directs the WoW packet filtering to look for
	 * EAP-ID requests embedded in EAPOL frames and use this as a wake
	 * source.
	 */
	u8 wow_eap_id_request_enable;

	/* This configuration directs the WoW packet filtering to look for
	 * EAPOL-4WAY requests and use this as a wake source.
	 */
	u8 wow_eapol_4way_enable;

	/* This configuration allows a host wakeup on an network scan
	 * offload match.
	 */
	u8 wow_net_scan_offload_match;

	/* This configuration allows a host wakeup on any GTK rekeying
	 * error.
	 */
	u8 wow_gtk_rekey_error;

	/* This configuration allows a host wakeup on BSS connection loss.
	 */
	u8 wow_bss_connection_loss;

	u8 bss_index;
};

struct wcn36xx_hal_wowl_exit_req_msg {
	struct wcn36xx_hal_msg_header header;

	u8 bss_index;
};

struct wcn36xx_hal_get_rssi_req_msg {
	struct wcn36xx_hal_msg_header header;
};

struct wcn36xx_hal_get_roam_rssi_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* Valid STA Idx for per STA stats request */
	u32 sta_id;
};

struct wcn36xx_hal_set_uapsd_ac_params_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* STA index */
	u8 sta_idx;

	/* Access Category */
	u8 ac;

	/* User Priority */
	u8 up;

	/* Service Interval */
	u32 service_interval;

	/* Suspend Interval */
	u32 suspend_interval;

	/* Delay Interval */
	u32 delay_interval;
};

struct wcn36xx_hal_configure_rxp_filter_req_msg {
	struct wcn36xx_hal_msg_header header;

	u8 set_mcst_bcst_filter_setting;
	u8 set_mcst_bcst_filter;
};

struct wcn36xx_hal_enter_imps_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
};

struct wcn36xx_hal_exit_imps_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
};

struct wcn36xx_hal_enter_bmps_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;

	u8 bss_index;
} __packed;

struct wcn36xx_hal_exit_bmps_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;

	u8 bss_index;
} __packed;

struct wcn36xx_hal_enter_uapsd_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;

	u8 bss_index;
};

struct wcn36xx_hal_exit_uapsd_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;

	u8 bss_index;
};

struct wcn36xx_hal_rssi_notification_ind_msg {
	struct wcn36xx_hal_msg_header header;

	u32 rssi_thres1_pos_cross:1;
	u32 rssi_thres1_neg_cross:1;
	u32 rssi_thres2_pos_cross:1;
	u32 rssi_thres2_neg_cross:1;
	u32 rssi_thres3_pos_cross:1;
	u32 rssi_thres3_neg_cross:1;
	u32 avg_rssi:8;
	u32 reserved:18;

};

struct wcn36xx_hal_get_rssio_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
	s8 rssi;

};

struct wcn36xx_hal_get_roam_rssi_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;

	u8 sta_id;
	s8 rssi;
};

struct wcn36xx_hal_wowl_enter_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
	u8 bss_index;
};

struct wcn36xx_hal_wowl_exit_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
	u8 bss_index;
};

struct wcn36xx_hal_add_bcn_filter_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
};

struct wcn36xx_hal_rem_bcn_filter_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
};

struct wcn36xx_hal_add_wowl_bcast_ptrn_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
	u8 bss_index;
};

struct wcn36xx_hal_del_wowl_bcast_ptrn_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
	u8 bss_index;
};

struct wcn36xx_hal_host_offload_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
};

struct wcn36xx_hal_keep_alive_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
};

struct wcn36xx_hal_set_rssi_thresh_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
};

struct wcn36xx_hal_set_uapsd_ac_params_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
};

struct wcn36xx_hal_configure_rxp_filter_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
};

struct set_max_tx_pwr_req {
	struct wcn36xx_hal_msg_header header;

	/* BSSID is needed to identify which session issued this request.
	 * As the request has power constraints, this should be applied
	 * only to that session */
	u8 bssid[ETH_ALEN];

	u8 self_addr[ETH_ALEN];

	/* In request, power == MaxTx power to be used. */
	u8 power;
};

struct set_max_tx_pwr_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* power == tx power used for management frames */
	u8 power;

	/* success or failure */
	u32 status;
};

struct set_tx_pwr_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* TX Power in milli watts */
	u32 tx_power;

	u8 bss_index;
};

struct set_tx_pwr_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
};

struct get_tx_pwr_req_msg {
	struct wcn36xx_hal_msg_header header;

	u8 sta_id;
};

struct get_tx_pwr_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;

	/* TX Power in milli watts */
	u32 tx_power;
};

struct set_p2p_gonoa_req_msg {
	struct wcn36xx_hal_msg_header header;

	u8 opp_ps;
	u32 ct_window;
	u8 count;
	u32 duration;
	u32 interval;
	u32 single_noa_duration;
	u8 ps_selection;
};

struct set_p2p_gonoa_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
};

struct wcn36xx_hal_add_sta_self_req {
	struct wcn36xx_hal_msg_header header;

	u8 self_addr[ETH_ALEN];
	u32 status;
} __packed;

struct wcn36xx_hal_add_sta_self_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;

	/* Self STA Index */
	u8 self_sta_index;

	/* DPU Index (IGTK, PTK, GTK all same) */
	u8 dpu_index;

	/* DPU Signature */
	u8 dpu_signature;
} __packed;

struct wcn36xx_hal_del_sta_self_req_msg {
	struct wcn36xx_hal_msg_header header;

	u8 self_addr[ETH_ALEN];
} __packed;

struct wcn36xx_hal_del_sta_self_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/*success or failure */
	u32 status;

	u8 self_addr[ETH_ALEN];
} __packed;

struct aggr_add_ts_req {
	struct wcn36xx_hal_msg_header header;

	/* Station Index */
	u16 sta_idx;

	/* TSPEC handler uniquely identifying a TSPEC for a STA in a BSS.
	 * This will carry the bitmap with the bit positions representing
	 * different AC.s */
	u16 tspec_index;

	/* Tspec info per AC To program TPE with required parameters */
	struct wcn36xx_hal_tspec_ie tspec[WCN36XX_HAL_MAX_AC];

	/* U-APSD Flags: 1b per AC.  Encoded as follows:
	   b7 b6 b5 b4 b3 b2 b1 b0 =
	   X  X  X  X  BE BK VI VO */
	u8 uapsd;

	/* These parameters are for all the access categories */

	/* Service Interval */
	u32 service_interval[WCN36XX_HAL_MAX_AC];

	/* Suspend Interval */
	u32 suspend_interval[WCN36XX_HAL_MAX_AC];

	/* Delay Interval */
	u32 delay_interval[WCN36XX_HAL_MAX_AC];
};

struct aggr_add_ts_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status0;

	/* FIXME PRIMA for future use for 11R */
	u32 status1;
};

struct wcn36xx_hal_configure_apps_cpu_wakeup_state_req_msg {
	struct wcn36xx_hal_msg_header header;

	u8 is_apps_cpu_awake;
};

struct wcn36xx_hal_configure_apps_cpu_wakeup_state_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
};

struct wcn36xx_hal_dump_cmd_req_msg {
	struct wcn36xx_hal_msg_header header;

	u32 arg1;
	u32 arg2;
	u32 arg3;
	u32 arg4;
	u32 arg5;
} __packed;

struct wcn36xx_hal_dump_cmd_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;

	/* Length of the responce message */
	u32 rsp_length;

	/* FIXME: Currently considering the the responce will be less than
	 * 100bytes */
	u8 rsp_buffer[DUMPCMD_RSP_BUFFER];
} __packed;

#define WLAN_COEX_IND_DATA_SIZE (4)
#define WLAN_COEX_IND_TYPE_DISABLE_HB_MONITOR (0)
#define WLAN_COEX_IND_TYPE_ENABLE_HB_MONITOR (1)

struct coex_ind_msg {
	struct wcn36xx_hal_msg_header header;

	/* Coex Indication Type */
	u32 type;

	/* Coex Indication Data */
	u32 data[WLAN_COEX_IND_DATA_SIZE];
};

struct wcn36xx_hal_tx_compl_ind_msg {
	struct wcn36xx_hal_msg_header header;

	/* Tx Complete Indication Success or Failure */
	u32 status;
};

struct wcn36xx_hal_wlan_host_suspend_ind_msg {
	struct wcn36xx_hal_msg_header header;

	u32 configured_mcst_bcst_filter_setting;
	u32 active_session_count;
};

struct wcn36xx_hal_wlan_exclude_unencrpted_ind_msg {
	struct wcn36xx_hal_msg_header header;

	u8 dot11_exclude_unencrypted;
	u8 bssid[ETH_ALEN];
};

struct noa_attr_ind_msg {
	struct wcn36xx_hal_msg_header header;

	u8 index;
	u8 opp_ps_flag;
	u16 ctwin;

	u16 noa1_interval_count;
	u16 bss_index;
	u32 noa1_duration;
	u32 noa1_interval;
	u32 noa1_starttime;

	u16 noa2_interval_count;
	u16 reserved2;
	u32 noa2_duration;
	u32 noa2_interval;
	u32 noa2_start_time;

	u32 status;
};

struct noa_start_ind_msg {
	struct wcn36xx_hal_msg_header header;

	u32 status;
	u32 bss_index;
};

struct wcn36xx_hal_wlan_host_resume_req_msg {
	struct wcn36xx_hal_msg_header header;

	u8 configured_mcst_bcst_filter_setting;
};

struct wcn36xx_hal_host_resume_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
};

struct wcn36xx_hal_del_ba_ind_msg {
	struct wcn36xx_hal_msg_header header;

	u16 sta_idx;

	/* Peer MAC Address, whose BA session has timed out */
	u8 peer_addr[ETH_ALEN];

	/* TID for which a BA session timeout is being triggered */
	u8 ba_tid;

	/* DELBA direction
	 * 1 - Originator
	 * 0 - Recipient
	 */
	u8 direction;

	u32 reason_code;

	/* TO SUPPORT BT-AMP */
	u8 bssid[ETH_ALEN];
};

/* PNO Messages */

/* Max number of channels that a network can be found on */
#define WCN36XX_HAL_PNO_MAX_NETW_CHANNELS  26

/* Max number of channels that a network can be found on */
#define WCN36XX_HAL_PNO_MAX_NETW_CHANNELS_EX  60

/* Maximum numbers of networks supported by PNO */
#define WCN36XX_HAL_PNO_MAX_SUPP_NETWORKS  16

/* The number of scan time intervals that can be programmed into PNO */
#define WCN36XX_HAL_PNO_MAX_SCAN_TIMERS    10

/* Maximum size of the probe template */
#define WCN36XX_HAL_PNO_MAX_PROBE_SIZE     450

/* Type of PNO enabling:
 *
 * Immediate - scanning will start immediately and PNO procedure will be
 * repeated based on timer
 *
 * Suspend - scanning will start at suspend
 *
 * Resume - scanning will start on system resume
 */
enum pno_mode {
	PNO_MODE_IMMEDIATE,
	PNO_MODE_ON_SUSPEND,
	PNO_MODE_ON_RESUME,
	PNO_MODE_MAX = WCN36XX_HAL_MAX_ENUM_SIZE
};

/* Authentication type */
enum auth_type {
	AUTH_TYPE_ANY = 0,
	AUTH_TYPE_OPEN_SYSTEM = 1,

	/* Upper layer authentication types */
	AUTH_TYPE_WPA = 2,
	AUTH_TYPE_WPA_PSK = 3,

	AUTH_TYPE_RSN = 4,
	AUTH_TYPE_RSN_PSK = 5,
	AUTH_TYPE_FT_RSN = 6,
	AUTH_TYPE_FT_RSN_PSK = 7,
	AUTH_TYPE_WAPI_WAI_CERTIFICATE = 8,
	AUTH_TYPE_WAPI_WAI_PSK = 9,

	AUTH_TYPE_MAX = WCN36XX_HAL_MAX_ENUM_SIZE
};

/* Encryption type */
enum ed_type {
	ED_ANY = 0,
	ED_NONE = 1,
	ED_WEP = 2,
	ED_TKIP = 3,
	ED_CCMP = 4,
	ED_WPI = 5,

	ED_TYPE_MAX = WCN36XX_HAL_MAX_ENUM_SIZE
};

/* SSID broadcast  type */
enum ssid_bcast_type {
	BCAST_UNKNOWN = 0,
	BCAST_NORMAL = 1,
	BCAST_HIDDEN = 2,

	BCAST_TYPE_MAX = WCN36XX_HAL_MAX_ENUM_SIZE
};

/* The network description for which PNO will have to look for */
struct network_type {
	/* SSID of the BSS */
	struct wcn36xx_hal_mac_ssid ssid;

	/* Authentication type for the network */
	enum auth_type authentication;

	/* Encryption type for the network */
	enum ed_type encryption;

	/* Indicate the channel on which the Network can be found 0 - if
	 * all channels */
	u8 channel_count;
	u8 channels[WCN36XX_HAL_PNO_MAX_NETW_CHANNELS];

	/* Indicates the RSSI threshold for the network to be considered */
	u8 rssi_threshold;
};

struct scan_timer {
	/* How much it should wait */
	u32 value;

	/* How many times it should repeat that wait value 0 - keep using
	 * this timer until PNO is disabled */
	u32 repeat;

	/* e.g: 2 3 4 0 - it will wait 2s between consecutive scans for 3
	 * times - after that it will wait 4s between consecutive scans
	 * until disabled */
};

/* The network parameters to be sent to the PNO algorithm */
struct scan_timers_type {
	/* set to 0 if you wish for PNO to use its default telescopic timer */
	u8 count;

	/* A set value represents the amount of time that PNO will wait
	 * between two consecutive scan procedures If the desired is for a
	 * uniform timer that fires always at the exact same interval - one
	 * single value is to be set If there is a desire for a more
	 * complex - telescopic like timer multiple values can be set -
	 * once PNO reaches the end of the array it will continue scanning
	 * at intervals presented by the last value */
	struct scan_timer values[WCN36XX_HAL_PNO_MAX_SCAN_TIMERS];
};

/* Preferred network list request */
struct set_pref_netw_list_req {
	struct wcn36xx_hal_msg_header header;

	/* Enable PNO */
	u32 enable;

	/* Immediate,  On Suspend,   On Resume */
	enum pno_mode mode;

	/* Number of networks sent for PNO */
	u32 networks_count;

	/* The networks that PNO needs to look for */
	struct network_type networks[WCN36XX_HAL_PNO_MAX_SUPP_NETWORKS];

	/* The scan timers required for PNO */
	struct scan_timers_type scan_timers;

	/* Probe template for 2.4GHz band */
	u16 band_24g_probe_size;
	u8 band_24g_probe_template[WCN36XX_HAL_PNO_MAX_PROBE_SIZE];

	/* Probe template for 5GHz band */
	u16 band_5g_probe_size;
	u8 band_5g_probe_template[WCN36XX_HAL_PNO_MAX_PROBE_SIZE];
};

/* The network description for which PNO will have to look for */
struct network_type_new {
	/* SSID of the BSS */
	struct wcn36xx_hal_mac_ssid ssid;

	/* Authentication type for the network */
	enum auth_type authentication;

	/* Encryption type for the network */
	enum ed_type encryption;

	/* SSID broadcast type, normal, hidden or unknown */
	enum ssid_bcast_type bcast_network_type;

	/* Indicate the channel on which the Network can be found 0 - if
	 * all channels */
	u8 channel_count;
	u8 channels[WCN36XX_HAL_PNO_MAX_NETW_CHANNELS];

	/* Indicates the RSSI threshold for the network to be considered */
	u8 rssi_threshold;
};

/* Preferred network list request new */
struct set_pref_netw_list_req_new {
	struct wcn36xx_hal_msg_header header;

	/* Enable PNO */
	u32 enable;

	/* Immediate,  On Suspend,   On Resume */
	enum pno_mode mode;

	/* Number of networks sent for PNO */
	u32 networks_count;

	/* The networks that PNO needs to look for */
	struct network_type_new networks[WCN36XX_HAL_PNO_MAX_SUPP_NETWORKS];

	/* The scan timers required for PNO */
	struct scan_timers_type scan_timers;

	/* Probe template for 2.4GHz band */
	u16 band_24g_probe_size;
	u8 band_24g_probe_template[WCN36XX_HAL_PNO_MAX_PROBE_SIZE];

	/* Probe template for 5GHz band */
	u16 band_5g_probe_size;
	u8 band_5g_probe_template[WCN36XX_HAL_PNO_MAX_PROBE_SIZE];
};

/* Preferred network list response */
struct set_pref_netw_list_resp {
	struct wcn36xx_hal_msg_header header;

	/* status of the request - just to indicate that PNO has
	 * acknowledged the request and will start scanning */
	u32 status;
};

/* Preferred network found indication */
struct pref_netw_found_ind {

	struct wcn36xx_hal_msg_header header;

	/* Network that was found with the highest RSSI */
	struct wcn36xx_hal_mac_ssid ssid;

	/* Indicates the RSSI */
	u8 rssi;
};

/* RSSI Filter request */
struct set_rssi_filter_req {
	struct wcn36xx_hal_msg_header header;

	/* RSSI Threshold */
	u8 rssi_threshold;
};

/* Set RSSI filter resp */
struct set_rssi_filter_resp {
	struct wcn36xx_hal_msg_header header;

	/* status of the request */
	u32 status;
};

/* Update scan params - sent from host to PNO to be used during PNO
 * scanningx */
struct wcn36xx_hal_update_scan_params_req {

	struct wcn36xx_hal_msg_header header;

	/* Host setting for 11d */
	u8 dot11d_enabled;

	/* Lets PNO know that host has determined the regulatory domain */
	u8 dot11d_resolved;

	/* Channels on which PNO is allowed to scan */
	u8 channel_count;
	u8 channels[WCN36XX_HAL_PNO_MAX_NETW_CHANNELS];

	/* Minimum channel time */
	u16 active_min_ch_time;

	/* Maximum channel time */
	u16 active_max_ch_time;

	/* Minimum channel time */
	u16 passive_min_ch_time;

	/* Maximum channel time */
	u16 passive_max_ch_time;

	/* Cb State */
	enum phy_chan_bond_state state;
} __packed;

/* Update scan params - sent from host to PNO to be used during PNO
 * scanningx */
struct wcn36xx_hal_update_scan_params_req_ex {

	struct wcn36xx_hal_msg_header header;

	/* Host setting for 11d */
	u8 dot11d_enabled;

	/* Lets PNO know that host has determined the regulatory domain */
	u8 dot11d_resolved;

	/* Channels on which PNO is allowed to scan */
	u8 channel_count;
	u8 channels[WCN36XX_HAL_PNO_MAX_NETW_CHANNELS_EX];

	/* Minimum channel time */
	u16 active_min_ch_time;

	/* Maximum channel time */
	u16 active_max_ch_time;

	/* Minimum channel time */
	u16 passive_min_ch_time;

	/* Maximum channel time */
	u16 passive_max_ch_time;

	/* Cb State */
	enum phy_chan_bond_state state;
} __packed;

/* Update scan params - sent from host to PNO to be used during PNO
 * scanningx */
struct wcn36xx_hal_update_scan_params_resp {

	struct wcn36xx_hal_msg_header header;

	/* status of the request */
	u32 status;
} __packed;

struct wcn36xx_hal_set_tx_per_tracking_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* 0: disable, 1:enable */
	u8 tx_per_tracking_enable;

	/* Check period, unit is sec. */
	u8 tx_per_tracking_period;

	/* (Fail TX packet)/(Total TX packet) ratio, the unit is 10%. */
	u8 tx_per_tracking_ratio;

	/* A watermark of check number, once the tx packet exceed this
	 * number, we do the check, default is 5 */
	u32 tx_per_tracking_watermark;
};

struct wcn36xx_hal_set_tx_per_tracking_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;

};

struct tx_per_hit_ind_msg {
	struct wcn36xx_hal_msg_header header;
};

/* Packet Filtering Definitions Begin */
#define    WCN36XX_HAL_PROTOCOL_DATA_LEN                  8
#define    WCN36XX_HAL_MAX_NUM_MULTICAST_ADDRESS        240
#define    WCN36XX_HAL_MAX_NUM_FILTERS                   20
#define    WCN36XX_HAL_MAX_CMP_PER_FILTER                10

enum wcn36xx_hal_receive_packet_filter_type {
	HAL_RCV_FILTER_TYPE_INVALID,
	HAL_RCV_FILTER_TYPE_FILTER_PKT,
	HAL_RCV_FILTER_TYPE_BUFFER_PKT,
	HAL_RCV_FILTER_TYPE_MAX_ENUM_SIZE
};

enum wcn36xx_hal_rcv_pkt_flt_protocol_type {
	HAL_FILTER_PROTO_TYPE_INVALID,
	HAL_FILTER_PROTO_TYPE_MAC,
	HAL_FILTER_PROTO_TYPE_ARP,
	HAL_FILTER_PROTO_TYPE_IPV4,
	HAL_FILTER_PROTO_TYPE_IPV6,
	HAL_FILTER_PROTO_TYPE_UDP,
	HAL_FILTER_PROTO_TYPE_MAX
};

enum wcn36xx_hal_rcv_pkt_flt_cmp_flag_type {
	HAL_FILTER_CMP_TYPE_INVALID,
	HAL_FILTER_CMP_TYPE_EQUAL,
	HAL_FILTER_CMP_TYPE_MASK_EQUAL,
	HAL_FILTER_CMP_TYPE_NOT_EQUAL,
	HAL_FILTER_CMP_TYPE_MAX
};

struct wcn36xx_hal_rcv_pkt_filter_params {
	u8 protocol_layer;
	u8 cmp_flag;

	/* Length of the data to compare */
	u16 data_length;

	/* from start of the respective frame header */
	u8 data_offset;

	/* Reserved field */
	u8 reserved;

	/* Data to compare */
	u8 compare_data[WCN36XX_HAL_PROTOCOL_DATA_LEN];

	/* Mask to be applied on the received packet data before compare */
	u8 data_mask[WCN36XX_HAL_PROTOCOL_DATA_LEN];
};

struct wcn36xx_hal_sessionized_rcv_pkt_filter_cfg_type {
	u8 id;
	u8 type;
	u8 params_count;
	u32 coleasce_time;
	u8 bss_index;
	struct wcn36xx_hal_rcv_pkt_filter_params params[1];
};

struct wcn36xx_hal_set_rcv_pkt_filter_req_msg {
	struct wcn36xx_hal_msg_header header;

	u8 id;
	u8 type;
	u8 params_count;
	u32 coalesce_time;
	struct wcn36xx_hal_rcv_pkt_filter_params params[1];
};

struct wcn36xx_hal_rcv_flt_mc_addr_list_type {
	/* from start of the respective frame header */
	u8 data_offset;

	u32 mc_addr_count;
	u8 mc_addr[WCN36XX_HAL_MAX_NUM_MULTICAST_ADDRESS][ETH_ALEN];
	u8 bss_index;
} __packed;

struct wcn36xx_hal_set_pkt_filter_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;

	u8 bss_index;
};

struct wcn36xx_hal_rcv_flt_pkt_match_cnt_req_msg {
	struct wcn36xx_hal_msg_header header;

	u8 bss_index;
};

struct wcn36xx_hal_rcv_flt_pkt_match_cnt {
	u8 id;
	u32 match_cnt;
};

struct wcn36xx_hal_rcv_flt_pkt_match_cnt_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* Success or Failure */
	u32 status;

	u32 match_count;
	struct wcn36xx_hal_rcv_flt_pkt_match_cnt
		matches[WCN36XX_HAL_MAX_NUM_FILTERS];
	u8 bss_index;
};

struct wcn36xx_hal_rcv_flt_pkt_clear_param {
	/* only valid for response message */
	u32 status;
	u8 id;
	u8 bss_index;
};

struct wcn36xx_hal_rcv_flt_pkt_clear_req_msg {
	struct wcn36xx_hal_msg_header header;
	struct wcn36xx_hal_rcv_flt_pkt_clear_param param;
};

struct wcn36xx_hal_rcv_flt_pkt_clear_rsp_msg {
	struct wcn36xx_hal_msg_header header;
	struct wcn36xx_hal_rcv_flt_pkt_clear_param param;
};

struct wcn36xx_hal_rcv_flt_pkt_set_mc_list_req_msg {
	struct wcn36xx_hal_msg_header header;
	struct wcn36xx_hal_rcv_flt_mc_addr_list_type mc_addr_list;
} __packed;

struct wcn36xx_hal_rcv_flt_pkt_set_mc_list_rsp_msg {
	struct wcn36xx_hal_msg_header header;
	u32 status;
	u8 bss_index;
};

/* Packet Filtering Definitions End */

struct wcn36xx_hal_set_power_params_req_msg {
	struct wcn36xx_hal_msg_header header;

	/*  Ignore DTIM */
	u32 ignore_dtim;

	/* DTIM Period */
	u32 dtim_period;

	/* Listen Interval */
	u32 listen_interval;

	/* Broadcast Multicast Filter  */
	u32 bcast_mcast_filter;

	/* Beacon Early Termination */
	u32 enable_bet;

	/* Beacon Early Termination Interval */
	u32 bet_interval;
} __packed;

struct wcn36xx_hal_set_power_params_resp {

	struct wcn36xx_hal_msg_header header;

	/* status of the request */
	u32 status;
} __packed;

/* Capability bitmap exchange definitions and macros starts */

enum place_holder_in_cap_bitmap {
	MCC = 0,
	P2P = 1,
	DOT11AC = 2,
	SLM_SESSIONIZATION = 3,
	DOT11AC_OPMODE = 4,
	SAP32STA = 5,
	TDLS = 6,
	P2P_GO_NOA_DECOUPLE_INIT_SCAN = 7,
	WLANACTIVE_OFFLOAD = 8,
	BEACON_OFFLOAD = 9,
	SCAN_OFFLOAD = 10,
	ROAM_OFFLOAD = 11,
	BCN_MISS_OFFLOAD = 12,
	STA_POWERSAVE = 13,
	STA_ADVANCED_PWRSAVE = 14,
	AP_UAPSD = 15,
	AP_DFS = 16,
	BLOCKACK = 17,
	PHY_ERR = 18,
	BCN_FILTER = 19,
	RTT = 20,
	RATECTRL = 21,
	WOW = 22,
	WLAN_ROAM_SCAN_OFFLOAD = 23,
	SPECULATIVE_PS_POLL = 24,
	SCAN_SCH = 25,
	IBSS_HEARTBEAT_OFFLOAD = 26,
	WLAN_SCAN_OFFLOAD = 27,
	WLAN_PERIODIC_TX_PTRN = 28,
	ADVANCE_TDLS = 29,
	BATCH_SCAN = 30,
	FW_IN_TX_PATH = 31,
	EXTENDED_NSOFFLOAD_SLOT = 32,
	CH_SWITCH_V1 = 33,
	HT40_OBSS_SCAN = 34,
	UPDATE_CHANNEL_LIST = 35,
	WLAN_MCADDR_FLT = 36,
	WLAN_CH144 = 37,
	NAN = 38,
	TDLS_SCAN_COEXISTENCE = 39,
	LINK_LAYER_STATS_MEAS = 40,
	MU_MIMO = 41,
	EXTENDED_SCAN = 42,
	DYNAMIC_WMM_PS = 43,
	MAC_SPOOFED_SCAN = 44,
	BMU_ERROR_GENERIC_RECOVERY = 45,
	DISA = 46,
	FW_STATS = 47,
	WPS_PRBRSP_TMPL = 48,
	BCN_IE_FLT_DELTA = 49,
	TDLS_OFF_CHANNEL = 51,
	RTT3 = 52,
	MGMT_FRAME_LOGGING = 53,
	ENHANCED_TXBD_COMPLETION = 54,
	LOGGING_ENHANCEMENT = 55,
	EXT_SCAN_ENHANCED = 56,
	MEMORY_DUMP_SUPPORTED = 57,
	PER_PKT_STATS_SUPPORTED = 58,
	EXT_LL_STAT = 60,
	WIFI_CONFIG = 61,
	ANTENNA_DIVERSITY_SELECTION = 62,

	MAX_FEATURE_SUPPORTED = 128,
};

#define WCN36XX_HAL_CAPS_SIZE 4

struct wcn36xx_hal_feat_caps_msg {

	struct wcn36xx_hal_msg_header header;

	u32 feat_caps[WCN36XX_HAL_CAPS_SIZE];
} __packed;

/* status codes to help debug rekey failures */
enum gtk_rekey_status {
	WCN36XX_HAL_GTK_REKEY_STATUS_SUCCESS = 0,

	/* rekey detected, but not handled */
	WCN36XX_HAL_GTK_REKEY_STATUS_NOT_HANDLED = 1,

	/* MIC check error on M1 */
	WCN36XX_HAL_GTK_REKEY_STATUS_MIC_ERROR = 2,

	/* decryption error on M1  */
	WCN36XX_HAL_GTK_REKEY_STATUS_DECRYPT_ERROR = 3,

	/* M1 replay detected */
	WCN36XX_HAL_GTK_REKEY_STATUS_REPLAY_ERROR = 4,

	/* missing GTK key descriptor in M1 */
	WCN36XX_HAL_GTK_REKEY_STATUS_MISSING_KDE = 5,

	/* missing iGTK key descriptor in M1 */
	WCN36XX_HAL_GTK_REKEY_STATUS_MISSING_IGTK_KDE = 6,

	/* key installation error */
	WCN36XX_HAL_GTK_REKEY_STATUS_INSTALL_ERROR = 7,

	/* iGTK key installation error */
	WCN36XX_HAL_GTK_REKEY_STATUS_IGTK_INSTALL_ERROR = 8,

	/* GTK rekey M2 response TX error */
	WCN36XX_HAL_GTK_REKEY_STATUS_RESP_TX_ERROR = 9,

	/* non-specific general error */
	WCN36XX_HAL_GTK_REKEY_STATUS_GEN_ERROR = 255
};

/* wake reason types */
enum wake_reason_type {
	WCN36XX_HAL_WAKE_REASON_NONE = 0,

	/* magic packet match */
	WCN36XX_HAL_WAKE_REASON_MAGIC_PACKET = 1,

	/* host defined pattern match */
	WCN36XX_HAL_WAKE_REASON_PATTERN_MATCH = 2,

	/* EAP-ID frame detected */
	WCN36XX_HAL_WAKE_REASON_EAPID_PACKET = 3,

	/* start of EAPOL 4-way handshake detected */
	WCN36XX_HAL_WAKE_REASON_EAPOL4WAY_PACKET = 4,

	/* network scan offload match */
	WCN36XX_HAL_WAKE_REASON_NETSCAN_OFFL_MATCH = 5,

	/* GTK rekey status wakeup (see status) */
	WCN36XX_HAL_WAKE_REASON_GTK_REKEY_STATUS = 6,

	/* BSS connection lost */
	WCN36XX_HAL_WAKE_REASON_BSS_CONN_LOST = 7,
};

/*
  Wake Packet which is saved at tWakeReasonParams.DataStart
  This data is sent for any wake reasons that involve a packet-based wakeup :

  WCN36XX_HAL_WAKE_REASON_TYPE_MAGIC_PACKET
  WCN36XX_HAL_WAKE_REASON_TYPE_PATTERN_MATCH
  WCN36XX_HAL_WAKE_REASON_TYPE_EAPID_PACKET
  WCN36XX_HAL_WAKE_REASON_TYPE_EAPOL4WAY_PACKET
  WCN36XX_HAL_WAKE_REASON_TYPE_GTK_REKEY_STATUS

  The information is provided to the host for auditing and debug purposes

*/

/* Wake reason indication */
struct wcn36xx_hal_wake_reason_ind {
	struct wcn36xx_hal_msg_header header;

	/* see tWakeReasonType */
	u32 reason;

	/* argument specific to the reason type */
	u32 reason_arg;

	/* length of optional data stored in this message, in case HAL
	 * truncates the data (i.e. data packets) this length will be less
	 * than the actual length */
	u32 stored_data_len;

	/* actual length of data */
	u32 actual_data_len;

	/* variable length start of data (length == storedDataLen) see
	 * specific wake type */
	u8 data_start[1];

	u32 bss_index:8;
	u32 reserved:24;
};

#define WCN36XX_HAL_GTK_KEK_BYTES 16
#define WCN36XX_HAL_GTK_KCK_BYTES 16

#define WCN36XX_HAL_GTK_OFFLOAD_FLAGS_DISABLE (1 << 0)

#define GTK_SET_BSS_KEY_TAG  0x1234AA55

struct wcn36xx_hal_gtk_offload_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* optional flags */
	u32 flags;

	/* Key confirmation key */
	u8 kck[WCN36XX_HAL_GTK_KCK_BYTES];

	/* key encryption key */
	u8 kek[WCN36XX_HAL_GTK_KEK_BYTES];

	/* replay counter */
	u64 key_replay_counter;

	u8 bss_index;
} __packed;

struct wcn36xx_hal_gtk_offload_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;

	u8 bss_index;
};

struct wcn36xx_hal_gtk_offload_get_info_req_msg {
	struct wcn36xx_hal_msg_header header;
	u8 bss_index;
} __packed;

struct wcn36xx_hal_gtk_offload_get_info_rsp_msg {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;

	/* last rekey status when the rekey was offloaded */
	u32 last_rekey_status;

	/* current replay counter value */
	u64 key_replay_counter;

	/* total rekey attempts */
	u32 total_rekey_count;

	/* successful GTK rekeys */
	u32 gtk_rekey_count;

	/* successful iGTK rekeys */
	u32 igtk_rekey_count;

	u8 bss_index;
} __packed;

struct dhcp_info {
	/* Indicates the device mode which indicates about the DHCP activity */
	u8 device_mode;

	u8 addr[ETH_ALEN];
};

struct dhcp_ind_status {
	struct wcn36xx_hal_msg_header header;

	/* success or failure */
	u32 status;
};

/*
 *   Thermal Mitigation mode of operation.
 *
 *  WCN36XX_HAL_THERMAL_MITIGATION_MODE_0 - Based on AMPDU disabling aggregation
 *
 *  WCN36XX_HAL_THERMAL_MITIGATION_MODE_1 - Based on AMPDU disabling aggregation
 *  and reducing transmit power
 *
 *  WCN36XX_HAL_THERMAL_MITIGATION_MODE_2 - Not supported */
enum wcn36xx_hal_thermal_mitigation_mode_type {
	HAL_THERMAL_MITIGATION_MODE_INVALID = -1,
	HAL_THERMAL_MITIGATION_MODE_0,
	HAL_THERMAL_MITIGATION_MODE_1,
	HAL_THERMAL_MITIGATION_MODE_2,
	HAL_THERMAL_MITIGATION_MODE_MAX = WCN36XX_HAL_MAX_ENUM_SIZE,
};


/*
 *   Thermal Mitigation level.
 * Note the levels are incremental i.e WCN36XX_HAL_THERMAL_MITIGATION_LEVEL_2 =
 * WCN36XX_HAL_THERMAL_MITIGATION_LEVEL_0 +
 * WCN36XX_HAL_THERMAL_MITIGATION_LEVEL_1
 *
 * WCN36XX_HAL_THERMAL_MITIGATION_LEVEL_0 - lowest level of thermal mitigation.
 * This level indicates normal mode of operation
 *
 * WCN36XX_HAL_THERMAL_MITIGATION_LEVEL_1 - 1st level of thermal mitigation
 *
 * WCN36XX_HAL_THERMAL_MITIGATION_LEVEL_2 - 2nd level of thermal mitigation
 *
 * WCN36XX_HAL_THERMAL_MITIGATION_LEVEL_3 - 3rd level of thermal mitigation
 *
 * WCN36XX_HAL_THERMAL_MITIGATION_LEVEL_4 - 4th level of thermal mitigation
 */
enum wcn36xx_hal_thermal_mitigation_level_type {
	HAL_THERMAL_MITIGATION_LEVEL_INVALID = -1,
	HAL_THERMAL_MITIGATION_LEVEL_0,
	HAL_THERMAL_MITIGATION_LEVEL_1,
	HAL_THERMAL_MITIGATION_LEVEL_2,
	HAL_THERMAL_MITIGATION_LEVEL_3,
	HAL_THERMAL_MITIGATION_LEVEL_4,
	HAL_THERMAL_MITIGATION_LEVEL_MAX = WCN36XX_HAL_MAX_ENUM_SIZE,
};


/* WCN36XX_HAL_SET_THERMAL_MITIGATION_REQ */
struct set_thermal_mitigation_req_msg {
	struct wcn36xx_hal_msg_header header;

	/* Thermal Mitigation Operation Mode */
	enum wcn36xx_hal_thermal_mitigation_mode_type mode;

	/* Thermal Mitigation Level */
	enum wcn36xx_hal_thermal_mitigation_level_type level;
};

struct set_thermal_mitigation_resp {

	struct wcn36xx_hal_msg_header header;

	/* status of the request */
	u32 status;
};

/* Per STA Class B Statistics. Class B statistics are STA TX/RX stats
 * provided to FW from Host via periodic messages */
struct stats_class_b_ind {
	struct wcn36xx_hal_msg_header header;

	/* Duration over which this stats was collected */
	u32 duration;

	/* Per STA Stats */

	/* TX stats */
	u32 tx_bytes_pushed;
	u32 tx_packets_pushed;

	/* RX stats */
	u32 rx_bytes_rcvd;
	u32 rx_packets_rcvd;
	u32 rx_time_total;
};

/* WCN36XX_HAL_PRINT_REG_INFO_IND */
struct wcn36xx_hal_print_reg_info_ind {
	struct wcn36xx_hal_msg_header header;

	u32 count;
	u32 scenario;
	u32 reason;

	struct {
		u32 addr;
		u32 value;
	} regs[];
} __packed;

#endif /* _HAL_H_ */
