/** @file mlan_ioctl.h
 *
 *  @brief This file declares the IOCTL data structures and APIs.
 *
 *  Copyright (C) 2008-2017, Marvell International Ltd.
 *
 *  This software file (the "File") is distributed by Marvell International
 *  Ltd. under the terms of the GNU General Public License Version 2, June 1991
 *  (the "License").  You may use, redistribute and/or modify this File in
 *  accordance with the terms and conditions of the License, a copy of which
 *  is available by writing to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 *  worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 *  THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 *  ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 *  this warranty disclaimer.
 */

/******************************************************
Change log:
    11/07/2008: initial version
******************************************************/

#ifndef _MLAN_IOCTL_H_
#define _MLAN_IOCTL_H_

/** Enumeration for IOCTL request ID */
enum _mlan_ioctl_req_id {
	/* Scan Group */
	MLAN_IOCTL_SCAN = 0x00010000,
	MLAN_OID_SCAN_NORMAL = 0x00010001,
	MLAN_OID_SCAN_SPECIFIC_SSID = 0x00010002,
	MLAN_OID_SCAN_USER_CONFIG = 0x00010003,
	MLAN_OID_SCAN_CONFIG = 0x00010004,
	MLAN_OID_SCAN_GET_CURRENT_BSS = 0x00010005,
	MLAN_OID_SCAN_CANCEL = 0x00010006,
	MLAN_OID_SCAN_TABLE_FLUSH = 0x0001000A,
	MLAN_OID_SCAN_BGSCAN_CONFIG = 0x0001000B,
	/* BSS Configuration Group */
	MLAN_IOCTL_BSS = 0x00020000,
	MLAN_OID_BSS_START = 0x00020001,
	MLAN_OID_BSS_STOP = 0x00020002,
	MLAN_OID_BSS_MODE = 0x00020003,
	MLAN_OID_BSS_CHANNEL = 0x00020004,
	MLAN_OID_BSS_CHANNEL_LIST = 0x00020005,
	MLAN_OID_BSS_MAC_ADDR = 0x00020006,
	MLAN_OID_BSS_MULTICAST_LIST = 0x00020007,
	MLAN_OID_BSS_FIND_BSS = 0x00020008,
	MLAN_OID_IBSS_BCN_INTERVAL = 0x00020009,
	MLAN_OID_IBSS_ATIM_WINDOW = 0x0002000A,
	MLAN_OID_IBSS_CHANNEL = 0x0002000B,
#ifdef UAP_SUPPORT
	MLAN_OID_UAP_BSS_CONFIG = 0x0002000C,
	MLAN_OID_UAP_DEAUTH_STA = 0x0002000D,
	MLAN_OID_UAP_BSS_RESET = 0x0002000E,
#endif
#if defined(STA_SUPPORT) && defined(UAP_SUPPORT)
	MLAN_OID_BSS_ROLE = 0x0002000F,
#endif
#ifdef WIFI_DIRECT_SUPPORT
	MLAN_OID_WIFI_DIRECT_MODE = 0x00020010,
#endif
#ifdef STA_SUPPORT
	MLAN_OID_BSS_LISTEN_INTERVAL = 0x00020011,
#endif
	MLAN_OID_BSS_REMOVE = 0x00020014,
#ifdef UAP_SUPPORT
	MLAN_OID_UAP_CFG_WMM_PARAM = 0x00020015,
#endif
	MLAN_OID_BSS_11D_CHECK_CHANNEL = 0x00020016,
#ifdef UAP_SUPPORT
	MLAN_OID_UAP_SCAN_CHANNELS = 0x00020018,
	MLAN_OID_UAP_CHANNEL = 0x00020019,
	MLAN_OID_UAP_OPER_CTRL = 0x0002001A,
#endif
#ifdef STA_SUPPORT
	MLAN_OID_BSS_CHAN_INFO = 0x0002001B,
#endif

	/* Radio Configuration Group */
	MLAN_IOCTL_RADIO_CFG = 0x00030000,
	MLAN_OID_RADIO_CTRL = 0x00030001,
	MLAN_OID_BAND_CFG = 0x00030002,
	MLAN_OID_ANT_CFG = 0x00030003,
	MLAN_OID_REMAIN_CHAN_CFG = 0x00030004,

	/* SNMP MIB Group */
	MLAN_IOCTL_SNMP_MIB = 0x00040000,
	MLAN_OID_SNMP_MIB_RTS_THRESHOLD = 0x00040001,
	MLAN_OID_SNMP_MIB_FRAG_THRESHOLD = 0x00040002,
	MLAN_OID_SNMP_MIB_RETRY_COUNT = 0x00040003,
	MLAN_OID_SNMP_MIB_DOT11D = 0x00040004,
#if defined(UAP_SUPPORT)
	MLAN_OID_SNMP_MIB_DOT11H = 0x00040005,
#endif
	MLAN_OID_SNMP_MIB_DTIM_PERIOD = 0x00040006,
	MLAN_OID_SNMP_MIB_SIGNALEXT_ENABLE = 0x00040007,
	MLAN_OID_SNMP_MIB_CTRL_DEAUTH = 0x00040008,

	/* Status Information Group */
	MLAN_IOCTL_GET_INFO = 0x00050000,
	MLAN_OID_GET_STATS = 0x00050001,
	MLAN_OID_GET_SIGNAL = 0x00050002,
	MLAN_OID_GET_FW_INFO = 0x00050003,
	MLAN_OID_GET_VER_EXT = 0x00050004,
	MLAN_OID_GET_BSS_INFO = 0x00050005,
	MLAN_OID_GET_DEBUG_INFO = 0x00050006,
#ifdef UAP_SUPPORT
	MLAN_OID_UAP_STA_LIST = 0x00050007,
#endif
	MLAN_OID_GET_SIGNAL_EXT = 0x00050008,
	MLAN_OID_GET_UAP_STATS_LOG = 0x0005000A,
	/* Security Configuration Group */
	MLAN_IOCTL_SEC_CFG = 0x00060000,
	MLAN_OID_SEC_CFG_AUTH_MODE = 0x00060001,
	MLAN_OID_SEC_CFG_ENCRYPT_MODE = 0x00060002,
	MLAN_OID_SEC_CFG_WPA_ENABLED = 0x00060003,
	MLAN_OID_SEC_CFG_ENCRYPT_KEY = 0x00060004,
	MLAN_OID_SEC_CFG_PASSPHRASE = 0x00060005,
	MLAN_OID_SEC_CFG_EWPA_ENABLED = 0x00060006,
	MLAN_OID_SEC_CFG_ESUPP_MODE = 0x00060007,
	MLAN_OID_SEC_CFG_WAPI_ENABLED = 0x00060009,
	MLAN_OID_SEC_CFG_PORT_CTRL_ENABLED = 0x0006000A,

	/* Rate Group */
	MLAN_IOCTL_RATE = 0x00070000,
	MLAN_OID_RATE_CFG = 0x00070001,
	MLAN_OID_GET_DATA_RATE = 0x00070002,
	MLAN_OID_SUPPORTED_RATES = 0x00070003,

	/* Power Configuration Group */
	MLAN_IOCTL_POWER_CFG = 0x00080000,
	MLAN_OID_POWER_CFG = 0x00080001,
	MLAN_OID_POWER_CFG_EXT = 0x00080002,

	/* Power Management Configuration Group */
	MLAN_IOCTL_PM_CFG = 0x00090000,
	MLAN_OID_PM_CFG_IEEE_PS = 0x00090001,
	MLAN_OID_PM_CFG_HS_CFG = 0x00090002,
	MLAN_OID_PM_CFG_INACTIVITY_TO = 0x00090003,
	MLAN_OID_PM_CFG_DEEP_SLEEP = 0x00090004,
	MLAN_OID_PM_CFG_SLEEP_PD = 0x00090005,
	MLAN_OID_PM_CFG_PS_CFG = 0x00090006,
	MLAN_OID_PM_CFG_FW_WAKEUP_METHOD = 0x00090007,
	MLAN_OID_PM_CFG_SLEEP_PARAMS = 0x00090008,
#ifdef UAP_SUPPORT
	MLAN_OID_PM_CFG_PS_MODE = 0x00090009,
#endif /* UAP_SUPPORT */
	MLAN_OID_PM_INFO = 0x0009000A,
	MLAN_OID_PM_HS_WAKEUP_REASON = 0x0009000B,
	MLAN_OID_PM_MGMT_FILTER = 0x0009000C,
	MLAN_OID_PM_CFG_BCN_TIMEOUT = 0x0009000D,

	/* WMM Configuration Group */
	MLAN_IOCTL_WMM_CFG = 0x000A0000,
	MLAN_OID_WMM_CFG_ENABLE = 0x000A0001,
	MLAN_OID_WMM_CFG_QOS = 0x000A0002,
	MLAN_OID_WMM_CFG_ADDTS = 0x000A0003,
	MLAN_OID_WMM_CFG_DELTS = 0x000A0004,
	MLAN_OID_WMM_CFG_QUEUE_CONFIG = 0x000A0005,
	MLAN_OID_WMM_CFG_QUEUE_STATS = 0x000A0006,
	MLAN_OID_WMM_CFG_QUEUE_STATUS = 0x000A0007,
	MLAN_OID_WMM_CFG_TS_STATUS = 0x000A0008,

	/* WPS Configuration Group */
	MLAN_IOCTL_WPS_CFG = 0x000B0000,
	MLAN_OID_WPS_CFG_SESSION = 0x000B0001,

	/* 802.11n Configuration Group */
	MLAN_IOCTL_11N_CFG = 0x000C0000,
	MLAN_OID_11N_CFG_TX = 0x000C0001,
	MLAN_OID_11N_HTCAP_CFG = 0x000C0002,
	MLAN_OID_11N_CFG_ADDBA_REJECT = 0x000C0003,
	MLAN_OID_11N_CFG_AGGR_PRIO_TBL = 0x000C0004,
	MLAN_OID_11N_CFG_ADDBA_PARAM = 0x000C0005,
	MLAN_OID_11N_CFG_MAX_TX_BUF_SIZE = 0x000C0006,
	MLAN_OID_11N_CFG_AMSDU_AGGR_CTRL = 0x000C0007,
	MLAN_OID_11N_CFG_SUPPORTED_MCS_SET = 0x000C0008,
	MLAN_OID_11N_CFG_TX_BF_CAP = 0x000C0009,
	MLAN_OID_11N_CFG_TX_BF_CFG = 0x000C000A,
	MLAN_OID_11N_CFG_DELBA = 0x000C000C,
	MLAN_OID_11N_CFG_REJECT_ADDBA_REQ = 0x000C000D,
	MLAN_OID_11N_CFG_COEX_RX_WINSIZE = 0x000C000E,
	MLAN_OID_11N_CFG_TX_AGGR_CTRL = 0x000C000F,
	MLAN_OID_11N_CFG_IBSS_AMPDU_PARAM = 0x000C0010,

	/* 802.11d Configuration Group */
	MLAN_IOCTL_11D_CFG = 0x000D0000,
#ifdef STA_SUPPORT
	MLAN_OID_11D_CFG_ENABLE = 0x000D0001,
	MLAN_OID_11D_CLR_CHAN_TABLE = 0x000D0002,
#endif /* STA_SUPPORT */
	MLAN_OID_11D_DOMAIN_INFO = 0x000D0003,

	/* Register Memory Access Group */
	MLAN_IOCTL_REG_MEM = 0x000E0000,
	MLAN_OID_REG_RW = 0x000E0001,
	MLAN_OID_EEPROM_RD = 0x000E0002,
	MLAN_OID_MEM_RW = 0x000E0003,

	/* Multi-Radio Configuration Group */
	MLAN_IOCTL_MFR_CFG = 0x00100000,

	/* 802.11h Configuration Group */
	MLAN_IOCTL_11H_CFG = 0x00110000,
	MLAN_OID_11H_CHANNEL_CHECK = 0x00110001,
	MLAN_OID_11H_LOCAL_POWER_CONSTRAINT = 0x00110002,
#if defined(DFS_TESTING_SUPPORT)
	MLAN_OID_11H_DFS_TESTING = 0x00110003,
#endif
	MLAN_OID_11H_CHAN_REPORT_REQUEST = 0x00110004,
	MLAN_OID_11H_CHAN_SWITCH_COUNT = 0x00110005,
#ifdef DFS_TESTING_SUPPORT
	MLAN_OID_11H_CHAN_NOP_INFO = 0x00110006,
#endif

	MLAN_IOCTL_11K_CFG = 0x00130000,
	MLAN_OID_11K_CFG_ENABLE = 0x00130001,
	MLAN_OID_11K_GET_NLIST = 0x00130002,

	/* Miscellaneous Configuration Group */
	MLAN_IOCTL_MISC_CFG = 0x00200000,
	MLAN_OID_MISC_GEN_IE = 0x00200001,
	MLAN_OID_MISC_REGION = 0x00200002,
	MLAN_OID_MISC_WARM_RESET = 0x00200003,
#if defined(SDIO_MULTI_PORT_TX_AGGR) || defined(SDIO_MULTI_PORT_RX_AGGR)
	MLAN_OID_MISC_SDIO_MPA_CTRL = 0x00200006,
#endif
	MLAN_OID_MISC_HOST_CMD = 0x00200007,
	MLAN_OID_MISC_SYS_CLOCK = 0x00200009,
	MLAN_OID_MISC_SOFT_RESET = 0x0020000A,
	MLAN_OID_MISC_WWS = 0x0020000B,
	MLAN_OID_MISC_ASSOC_RSP = 0x0020000C,
	MLAN_OID_MISC_INIT_SHUTDOWN = 0x0020000D,
	MLAN_OID_MISC_COALESCING_STATUS = 0x0020000E,
	MLAN_OID_MISC_CUSTOM_IE = 0x0020000F,
	MLAN_OID_MISC_TDLS_CONFIG = 0x00200010,
	MLAN_OID_MISC_NET_MONITOR = 0x00200011,
	MLAN_OID_MISC_TX_DATAPAUSE = 0x00200012,
	MLAN_OID_MISC_IP_ADDR = 0x00200013,
	MLAN_OID_MISC_MAC_CONTROL = 0x00200014,
	MLAN_OID_MISC_MEF_CFG = 0x00200015,
	MLAN_OID_MISC_CFP_CODE = 0x00200016,
	MLAN_OID_MISC_COUNTRY_CODE = 0x00200017,
	MLAN_OID_MISC_THERMAL = 0x00200018,
	MLAN_OID_MISC_RX_MGMT_IND = 0x00200019,
	MLAN_OID_MISC_SUBSCRIBE_EVENT = 0x0020001A,
#ifdef DEBUG_LEVEL1
	MLAN_OID_MISC_DRVDBG = 0x0020001B,
#endif
	MLAN_OID_MISC_OTP_USER_DATA = 0x0020001D,
	MLAN_OID_MISC_TXCONTROL = 0x00200020,
#ifdef STA_SUPPORT
	MLAN_OID_MISC_EXT_CAP_CFG = 0x00200021,
#endif
#if defined(STA_SUPPORT)
	MLAN_OID_MISC_PMFCFG = 0x00200022,
#endif
	MLAN_OID_MISC_MULTI_CHAN_CFG = 0x00200023,
	MLAN_OID_MISC_MULTI_CHAN_POLICY = 0x00200024,
#ifdef WIFI_DIRECT_SUPPORT
	MLAN_OID_MISC_WIFI_DIRECT_CONFIG = 0x00200025,
#endif
	MLAN_OID_MISC_TDLS_OPER = 0x00200026,
	MLAN_OID_MISC_GET_TDLS_IES = 0x00200027,
	MLAN_OID_MISC_DFS_REAPTER_MODE = 0x0020002B,
#ifdef RX_PACKET_COALESCE
	MLAN_OID_MISC_RX_PACKET_COALESCE = 0x0020002C,
#endif
	MLAN_OID_MISC_TDLS_CS_CHANNEL = 0x0020002D,
	MLAN_OID_MISC_COALESCE_CFG = 0x0020002E,
	MLAN_OID_MISC_TDLS_IDLE_TIME = 0x0020002F,
	MLAN_OID_MISC_GET_SENSOR_TEMP = 0x00200030,
	MLAN_OID_MISC_GTK_REKEY_OFFLOAD = 0x00200037,
	MLAN_OID_MISC_OPER_CLASS = 0x00200038,
	MLAN_OID_MISC_PMIC_CFG = 0x00200039,
	MLAN_OID_MISC_IND_RST_CFG = 0x00200040,
	MLAN_OID_MISC_ROAM_OFFLOAD = 0x00200042,
	MLAN_OID_MISC_ROAM_OFFLOAD_APLIST = 0x00200043,
	MLAN_OID_MISC_GET_TSF = 0x00200045,
	MLAN_OID_MISC_GET_CHAN_REGION_CFG = 0x00200046,
	MLAN_OID_MISC_OPER_CLASS_CHECK = 0x00200049,
	MLAN_OID_MISC_DRCS_CFG = 0x00200050,

	MLAN_OID_MISC_CWMODE_CTRL = 0x00200051,
	MLAN_OID_MISC_FW_DUMP_EVENT = 0x00200054,
	MLAN_OID_MISC_PER_PKT_CFG = 0x00200055,
	MLAN_OID_MISC_ROBUSTCOEX = 0x00200056,
	MLAN_OID_MISC_GET_CORRELATED_TIME = 0x00200058,
	MLAN_OID_MISC_CFP_INFO = 0x00200060,
	MLAN_OID_MISC_BOOT_SLEEP = 0x00200061,
};

/** Sub command size */
#define MLAN_SUB_COMMAND_SIZE	4

/** Enumeration for the action of IOCTL request */
enum _mlan_act_ioctl {
	MLAN_ACT_SET = 1,
	MLAN_ACT_GET,
	MLAN_ACT_CANCEL,
	MLAN_ACT_CLEAR,
	MLAN_ACT_RESET
};

/** Enumeration for generic enable/disable */
enum _mlan_act_generic {
	MLAN_ACT_DISABLE = 0,
	MLAN_ACT_ENABLE = 1
};

/** Enumeration for scan mode */
enum _mlan_scan_mode {
	MLAN_SCAN_MODE_UNCHANGED = 0,
	MLAN_SCAN_MODE_BSS,
	MLAN_SCAN_MODE_IBSS,
	MLAN_SCAN_MODE_ANY
};

/** Enumeration for scan type */
enum _mlan_scan_type {
	MLAN_SCAN_TYPE_UNCHANGED = 0,
	MLAN_SCAN_TYPE_ACTIVE,
	MLAN_SCAN_TYPE_PASSIVE
};

/** Max number of supported rates */
#define MLAN_SUPPORTED_RATES	32

/** RSSI scan */
#define SCAN_RSSI(RSSI)			(0x100 - ((t_u8)(RSSI)))

/** Max passive scan time for each channel in milliseconds */
#define MRVDRV_MAX_PASSIVE_SCAN_CHAN_TIME   2000

/** Max active scan time for each channel in milliseconds  */
#define MRVDRV_MAX_ACTIVE_SCAN_CHAN_TIME    500

/** Maximum number of probes to send on each channel */
#define MAX_PROBES      4

/** Default number of probes to send on each channel */
#define DEFAULT_PROBES  4

/**
 *  @brief Sub-structure passed in wlan_ioctl_get_scan_table_entry for each BSS
 *
 *  Fixed field information returned for the scan response in the IOCTL
 *    response.
 */
typedef struct _wlan_get_scan_table_fixed {
    /** BSSID of this network */
	t_u8 bssid[MLAN_MAC_ADDR_LENGTH];
    /** Channel this beacon/probe response was detected */
	t_u8 channel;
    /** RSSI for the received packet */
	t_u8 rssi;
    /** TSF value in microseconds from the firmware at packet reception */
	t_u64 network_tsf;
} wlan_get_scan_table_fixed;

/** mlan_802_11_ssid data structure */
typedef struct _mlan_802_11_ssid {
    /** SSID Length */
	t_u32 ssid_len;
    /** SSID information field */
	t_u8 ssid[MLAN_MAX_SSID_LENGTH];
} mlan_802_11_ssid, *pmlan_802_11_ssid;

/** tx status event structure */
typedef MLAN_PACK_START struct _tx_status_event {
    /** packet type */
	t_u8 packet_type;
    /** tx_token_id */
	t_u8 tx_token_id;
    /** 0--success, 1--fail, 2--watchdogtimeout */
	t_u8 status;
	/** t1 time stamp */
	t_u64 t1_tstamp;
	/** t4 time stamp */
	t_u64 t4_tstamp;
	/** t1 error */
	t_u64 t1_error;
	/** t4 error */
	t_u64 t4_error;
	/** egress time */
	t_u64 egress_time;
} MLAN_PACK_END tx_status_event;

/**
 *  Sructure to retrieve the scan table
 */
typedef struct {
    /**
     *  - Zero based scan entry to start retrieval in command request
     *  - Number of scans entries returned in command response
     */
	t_u32 scan_number;
    /**
     * Buffer marker for multiple wlan_ioctl_get_scan_table_entry structures.
     *   Each struct is padded to the nearest 32 bit boundary.
     */
	t_u8 scan_table_entry_buf[1];
} wlan_ioctl_get_scan_table_info;

/**
 *  Structure passed in the wlan_ioctl_get_scan_table_info for each
 *    BSS returned in the WLAN_GET_SCAN_RESP IOCTL
 */
typedef struct _wlan_ioctl_get_scan_table_entry {
    /**
     *  Fixed field length included in the response.
     *
     *  Length value is included so future fixed fields can be added to the
     *   response without breaking backwards compatibility.  Use the length
     *   to find the offset for the bssInfoLength field, not a sizeof() calc.
     */
	t_u32 fixed_field_length;

    /**
     *  Length of the BSS Information (probe resp or beacon) that
     *    follows after the fixed_field_length
     */
	t_u32 bss_info_length;

    /**
     *  Always present, fixed length data fields for the BSS
     */
	wlan_get_scan_table_fixed fixed_fields;

	/*
	 * Probe response or beacon scanned for the BSS.
	 *
	 * Field layout:
	 *  - TSF              8 octets
	 *  - Beacon Interval  2 octets
	 *  - Capability Info  2 octets
	 *
	 *  - IEEE Infomation Elements; variable number & length per 802.11 spec
	 */
	/* t_u8  bss_info_buffer[0]; */
} wlan_ioctl_get_scan_table_entry;

/** Type definition of mlan_scan_time_params */
typedef struct _mlan_scan_time_params {
    /** Scan channel time for specific scan in milliseconds */
	t_u32 specific_scan_time;
    /** Scan channel time for active scan in milliseconds */
	t_u32 active_scan_time;
    /** Scan channel time for passive scan in milliseconds */
	t_u32 passive_scan_time;
} mlan_scan_time_params, *pmlan_scan_time_params;

/** Type definition of mlan_user_scan */
typedef struct _mlan_user_scan {
    /** Length of scan_cfg_buf */
	t_u32 scan_cfg_len;
    /** Buffer of scan config */
	t_u8 scan_cfg_buf[1];
} mlan_user_scan, *pmlan_user_scan;

/** Type definition of mlan_scan_req */
typedef struct _mlan_scan_req {
    /** BSS mode for scanning */
	t_u32 scan_mode;
    /** Scan type */
	t_u32 scan_type;
    /** SSID */
	mlan_802_11_ssid scan_ssid;
    /** Scan time parameters */
	mlan_scan_time_params scan_time;
    /** Scan config parameters in user scan */
	mlan_user_scan user_scan;
} mlan_scan_req, *pmlan_scan_req;

/** Type defnition of mlan_scan_resp */
typedef struct _mlan_scan_resp {
    /** Number of scan result */
	t_u32 num_in_scan_table;
    /** Scan table */
	t_u8 *pscan_table;
	/* Age in seconds */
	t_u32 age_in_secs;
    /** channel statstics */
	t_u8 *pchan_stats;
    /** Number of records in the chan_stats */
	t_u32 num_in_chan_stats;
} mlan_scan_resp, *pmlan_scan_resp;

#define EXT_SCAN_TYPE_ENH   2
/** Type definition of mlan_scan_cfg */
typedef struct _mlan_scan_cfg {
    /** Scan type */
	t_u32 scan_type;
    /** BSS mode for scanning */
	t_u32 scan_mode;
    /** Scan probe */
	t_u32 scan_probe;
    /** Scan time parameters */
	mlan_scan_time_params scan_time;
    /** Ext_scan:  0 disable, 1: enable, 2: enhance scan*/
	t_u32 ext_scan;
} mlan_scan_cfg, *pmlan_scan_cfg;

/** Type defnition of mlan_ds_scan for MLAN_IOCTL_SCAN */
typedef struct _mlan_ds_scan {
    /** Sub-command */
	t_u32 sub_command;
    /** Scan request/response */
	union {
	/** Scan request */
		mlan_scan_req scan_req;
	/** Scan response */
		mlan_scan_resp scan_resp;
	/** Scan config parameters in user scan */
		mlan_user_scan user_scan;
	/** Scan config parameters */
		mlan_scan_cfg scan_cfg;
	} param;
} mlan_ds_scan, *pmlan_ds_scan;

/*-----------------------------------------------------------------*/
/** BSS Configuration Group */
/*-----------------------------------------------------------------*/
/** Enumeration for BSS mode */
enum _mlan_bss_mode {
	MLAN_BSS_MODE_INFRA = 1,
	MLAN_BSS_MODE_IBSS,
	MLAN_BSS_MODE_AUTO
};

/** Maximum key length */
#define MLAN_MAX_KEY_LENGTH             32
/** Maximum PMK R0 NAME key length */
#define MLAN_MAX_PMKR0_NAME_LENGTH      16

/** Maximum atim window in milliseconds */
#define MLAN_MAX_ATIM_WINDOW		50

/** Minimum beacon interval */
#define MLAN_MIN_BEACON_INTERVAL        20
/** Maximum beacon interval */
#define MLAN_MAX_BEACON_INTERVAL        1000
/** Default beacon interval */
#define MLAN_BEACON_INTERVAL            100

/** Receive all packets */
#define MLAN_PROMISC_MODE       1
/** Receive multicast packets in multicast list */
#define MLAN_MULTICAST_MODE		2
/** Receive all multicast packets */
#define MLAN_ALL_MULTI_MODE		4

/** Maximum size of multicast list */
#define MLAN_MAX_MULTICAST_LIST_SIZE	32

/** mlan_multicast_list data structure for MLAN_OID_BSS_MULTICAST_LIST */
typedef struct _mlan_multicast_list {
    /** Multicast mode */
	t_u32 mode;
    /** Number of multicast addresses in the list */
	t_u32 num_multicast_addr;
    /** Multicast address list */
	mlan_802_11_mac_addr mac_list[MLAN_MAX_MULTICAST_LIST_SIZE];
} mlan_multicast_list, *pmlan_multicast_list;

/** Max channel */
#define MLAN_MAX_CHANNEL    165
/** Maximum number of channels in table */
#define MLAN_MAX_CHANNEL_NUM	128

/** Channel/frequence for MLAN_OID_BSS_CHANNEL */
typedef struct _chan_freq {
    /** Channel Number */
	t_u32 channel;
    /** Frequency of this Channel */
	t_u32 freq;
} chan_freq;

/** mlan_chan_list data structure for MLAN_OID_BSS_CHANNEL_LIST */
typedef struct _mlan_chan_list {
    /** Number of channel */
	t_u32 num_of_chan;
    /** Channel-Frequency table */
	chan_freq cf[MLAN_MAX_CHANNEL_NUM];
} mlan_chan_list;

/* This channel is disabled.*/
#define CHAN_FLAGS_DISABLED         MBIT(0)
/* do not initiate radiation, this includes sending probe requests or beaconing */
#define CHAN_FLAGS_NO_IR            MBIT(1)
/* Radar detection is required on this channel */
#define CHAN_FLAGS_RADAR            MBIT(3)
/* extension channel above this channel is not permitted */
#define CHAN_FLAGS_NO_HT40PLUS      MBIT(4)
/* extension channel below this channel is not permitted */
#define CHAN_FLAGS_NO_HT40MINUS     MBIT(5)
/* OFDM is not allowed on this channel */
#define CHAN_FLAGS_NO_OFDM          MBIT(6)
/** 80Mhz can not used on this channel */
#define CHAN_FLAGS_NO_80MHZ         MBIT(7)
/** 180Mhz can not used on this channel */
#define CHAN_FLAGS_NO_160MHZ        MBIT(8)
/* Only indoor use is permitted on this channel */
#define CHAN_FLAGS_INDOOR_ONLY      MBIT(9)
/* IR operation is allowed on this channel if it's
 * connected concurrently to a BSS on the same channel on
 * the 2 GHz band or to a channel in the same UNII band (on the 5 GHz
 * band), and IEEE80211_CHAN_RADAR is not set */
#define CHAN_FLAGS_IR_CONCURRENT    MBIT(10)
/* 20 MHz operation is not allowed on this channel */
#define CHAN_FLAGS_20MHZ            MBIT(11)
/* 10 MHz operation is not allowed on this channel */
#define CHAN_FLAGS_NO_10MHZ         MBIT(12)

/** mlan_ssid_bssid  data structure for
 *  MLAN_OID_BSS_START and MLAN_OID_BSS_FIND_BSS
 */
typedef struct _mlan_ssid_bssid {
    /** SSID */
	mlan_802_11_ssid ssid;
    /** BSSID */
	mlan_802_11_mac_addr bssid;
    /** index in BSSID list, start from 1 */
	t_u32 idx;
    /** Receive signal strength in dBm */
	t_s32 rssi;
    /**channel*/
	t_u16 channel;
    /**mobility domain value*/
	t_u16 ft_md;
    /**ft capability*/
	t_u8 ft_cap;
    /**band*/
	t_u16 bss_band;
	t_u32 channel_flags;
} mlan_ssid_bssid;

/** Data structure of WMM ECW */
typedef struct _wmm_ecw_t {
#ifdef BIG_ENDIAN_SUPPORT
    /** Maximum Ecw */
	t_u8 ecw_max:4;
    /** Minimum Ecw */
	t_u8 ecw_min:4;
#else
    /** Minimum Ecw */
	t_u8 ecw_min:4;
    /** Maximum Ecw */
	t_u8 ecw_max:4;
#endif				/* BIG_ENDIAN_SUPPORT */
} wmm_ecw_t, *pwmm_ecw_t;

/** Data structure of WMM Aci/Aifsn */
typedef struct _wmm_aci_aifsn_t {
#ifdef BIG_ENDIAN_SUPPORT
    /** Reserved */
	t_u8 reserved:1;
    /** Aci */
	t_u8 aci:2;
    /** Acm */
	t_u8 acm:1;
    /** Aifsn */
	t_u8 aifsn:4;
#else
    /** Aifsn */
	t_u8 aifsn:4;
    /** Acm */
	t_u8 acm:1;
    /** Aci */
	t_u8 aci:2;
    /** Reserved */
	t_u8 reserved:1;
#endif				/* BIG_ENDIAN_SUPPORT */
} wmm_aci_aifsn_t, *pwmm_aci_aifsn_t;

/** Data structure of WMM AC parameters  */
typedef struct _wmm_ac_parameters_t {
	wmm_aci_aifsn_t aci_aifsn;   /**< AciAifSn */
	wmm_ecw_t ecw;		   /**< Ecw */
	t_u16 tx_op_limit;		      /**< Tx op limit */
} wmm_ac_parameters_t, *pwmm_ac_parameters_t;

/** mlan_deauth_param */
typedef struct _mlan_deauth_param {
    /** STA mac addr */
	t_u8 mac_addr[MLAN_MAC_ADDR_LENGTH];
    /** deauth reason */
	t_u16 reason_code;
} mlan_deauth_param;

#ifdef UAP_SUPPORT
/** Maximum packet forward control value */
#define MAX_PKT_FWD_CTRL 15
/** Maximum BEACON period */
#define MAX_BEACON_PERIOD 4000
/** Minimum BEACON period */
#define MIN_BEACON_PERIOD 50
/** Maximum DTIM period */
#define MAX_DTIM_PERIOD 100
/** Minimum DTIM period */
#define MIN_DTIM_PERIOD 1
/** Maximum TX Power Limit */
#define MAX_TX_POWER    20
/** Minimum TX Power Limit */
#define MIN_TX_POWER    0
/** MAX station count */
#define MAX_STA_COUNT   10
/** Maximum RTS threshold */
#define MAX_RTS_THRESHOLD   2347
/** Maximum fragmentation threshold */
#define MAX_FRAG_THRESHOLD 2346
/** Minimum fragmentation threshold */
#define MIN_FRAG_THRESHOLD 256
/** data rate 54 M */
#define DATA_RATE_54M   108
/** Maximum value of bcast_ssid_ctl */
#define MAX_BCAST_SSID_CTL  2
/** antenna A */
#define ANTENNA_MODE_A      0
/** antenna B */
#define ANTENNA_MODE_B      1
/** transmit antenna */
#define TX_ANTENNA          1
/** receive antenna */
#define RX_ANTENNA          0
/** Maximum stage out time */
#define MAX_STAGE_OUT_TIME  864000
/** Minimum stage out time */
#define MIN_STAGE_OUT_TIME  300
/** Maximum Retry Limit */
#define MAX_RETRY_LIMIT         14

/** Maximum group key timer in seconds */
#define MAX_GRP_TIMER           86400

/** Maximum value of 4 byte configuration */
#define MAX_VALID_DWORD         0x7FFFFFFF	/*  (1 << 31) - 1 */

/** default UAP BAND 2.4G */
#define DEFAULT_UAP_BAND		0
/** default UAP channel 6 */
#define DEFAULT_UAP_CHANNEL		6

/** Maximum data rates */
#define MAX_DATA_RATES          14

/** auto data rate */
#define DATA_RATE_AUTO       0

/**filter mode: disable */
#define MAC_FILTER_MODE_DISABLE         0
/**filter mode: block mac address */
#define MAC_FILTER_MODE_ALLOW_MAC       1
/**filter mode: block mac address */
#define MAC_FILTER_MODE_BLOCK_MAC       2
/** Maximum mac filter num */
#define MAX_MAC_FILTER_NUM           16

/* Bitmap for protocol to use */
/** No security */
#define PROTOCOL_NO_SECURITY        0x01
/** Static WEP */
#define PROTOCOL_STATIC_WEP         0x02
/** WPA */
#define PROTOCOL_WPA                0x08
/** WPA2 */
#define PROTOCOL_WPA2               0x20
/** WP2 Mixed */
#define PROTOCOL_WPA2_MIXED         0x28
/** EAP */
#define PROTOCOL_EAP                0x40
/** WAPI */
#define PROTOCOL_WAPI               0x80

/** Key_mgmt_psk */
#define KEY_MGMT_NONE   0x04
/** Key_mgmt_none */
#define KEY_MGMT_PSK    0x02
/** Key_mgmt_eap  */
#define KEY_MGMT_EAP    0x01
/** Key_mgmt_psk_sha256 */
#define KEY_MGMT_PSK_SHA256     0x100

/** TKIP */
#define CIPHER_TKIP                 0x04
/** AES CCMP */
#define CIPHER_AES_CCMP             0x08

/** Valid cipher bitmap */
#define VALID_CIPHER_BITMAP         0x0c

/** Packet forwarding to be done by FW or host */
#define PKT_FWD_FW_BIT  0x01
/** Intra-BSS broadcast packet forwarding allow bit */
#define PKT_FWD_INTRA_BCAST 0x02
/** Intra-BSS unicast packet forwarding allow bit */
#define PKT_FWD_INTRA_UCAST 0x04
/** Inter-BSS unicast packet forwarding allow bit */
#define PKT_FWD_INTER_UCAST 0x08
/** Intra-BSS unicast packet */
#define PKT_INTRA_UCAST     0x01
/** Inter-BSS unicast packet */
#define PKT_INTER_UCAST     0x02
/** Enable Host PKT forwarding */
#define PKT_FWD_ENABLE_BIT  0x01

/** Channel List Entry */
typedef struct _channel_list {
    /** Channel Number */
	t_u8 chan_number;
    /** Band Config */
	Band_Config_t bandcfg;
} scan_chan_list;

/** mac_filter data structure */
typedef struct _mac_filter {
    /** mac filter mode */
	t_u16 filter_mode;
    /** mac adress count */
	t_u16 mac_count;
    /** mac address list */
	mlan_802_11_mac_addr mac_list[MAX_MAC_FILTER_NUM];
} mac_filter;

/** wpa parameter */
typedef struct _wpa_param {
    /** Pairwise cipher WPA */
	t_u8 pairwise_cipher_wpa;
    /** Pairwise cipher WPA2 */
	t_u8 pairwise_cipher_wpa2;
    /** group cipher */
	t_u8 group_cipher;
    /** RSN replay protection */
	t_u8 rsn_protection;
    /** passphrase length */
	t_u32 length;
    /** passphrase */
	t_u8 passphrase[64];
    /**group key rekey time in seconds */
	t_u32 gk_rekey_time;
} wpa_param;

/** wep key */
typedef struct _wep_key {
    /** key index 0-3 */
	t_u8 key_index;
    /** is default */
	t_u8 is_default;
    /** length */
	t_u16 length;
    /** key data */
	t_u8 key[26];
} wep_key;

/** wep param */
typedef struct _wep_param {
    /** key 0 */
	wep_key key0;
    /** key 1 */
	wep_key key1;
    /** key 2 */
	wep_key key2;
    /** key 3 */
	wep_key key3;
} wep_param;

/** Data structure of WMM QoS information */
typedef struct _wmm_qos_info_t {
#ifdef BIG_ENDIAN_SUPPORT
    /** QoS UAPSD */
	t_u8 qos_uapsd:1;
    /** Reserved */
	t_u8 reserved:3;
    /** Parameter set count */
	t_u8 para_set_count:4;
#else
    /** Parameter set count */
	t_u8 para_set_count:4;
    /** Reserved */
	t_u8 reserved:3;
    /** QoS UAPSD */
	t_u8 qos_uapsd:1;
#endif				/* BIG_ENDIAN_SUPPORT */
} wmm_qos_info_t, *pwmm_qos_info_t;

/** Data structure of WMM parameter IE  */
typedef struct _wmm_parameter_t {
    /** OuiType:  00:50:f2:02 */
	t_u8 ouitype[4];
    /** Oui subtype: 01 */
	t_u8 ouisubtype;
    /** version: 01 */
	t_u8 version;
    /** QoS information */
	t_u8 qos_info;
    /** Reserved */
	t_u8 reserved;
    /** AC Parameters Record WMM_AC_BE, WMM_AC_BK, WMM_AC_VI, WMM_AC_VO */
	wmm_ac_parameters_t ac_params[MAX_AC_QUEUES];
} wmm_parameter_t, *pwmm_parameter_t;

/** 5G band */
#define BAND_CONFIG_5G        0x01
/** 2.4 G band */
#define BAND_CONFIG_2G        0x00
/** MAX BG channel */
#define MAX_BG_CHANNEL 14
/** mlan_bss_param
 * Note: For each entry you must enter an invalid value
 * in the MOAL function woal_set_sys_config_invalid_data().
 * Otherwise for a valid data an unwanted TLV will be
 * added to that command.
 */
typedef struct _mlan_uap_bss_param {
    /** AP mac addr */
	mlan_802_11_mac_addr mac_addr;
    /** SSID */
	mlan_802_11_ssid ssid;
    /** Broadcast ssid control */
	t_u8 bcast_ssid_ctl;
    /** Radio control: on/off */
	t_u8 radio_ctl;
    /** dtim period */
	t_u8 dtim_period;
    /** beacon period */
	t_u16 beacon_period;
    /** rates */
	t_u8 rates[MAX_DATA_RATES];
    /** Tx data rate */
	t_u16 tx_data_rate;
    /** Tx beacon rate */
	t_u16 tx_beacon_rate;
    /** multicast/broadcast data rate */
	t_u16 mcbc_data_rate;
    /** Tx power level in dBm */
	t_u8 tx_power_level;
    /** Tx antenna */
	t_u8 tx_antenna;
    /** Rx antenna */
	t_u8 rx_antenna;
    /** packet forward control */
	t_u8 pkt_forward_ctl;
    /** max station count */
	t_u16 max_sta_count;
    /** mac filter */
	mac_filter filter;
    /** station ageout timer in unit of 100ms  */
	t_u32 sta_ageout_timer;
    /** PS station ageout timer in unit of 100ms  */
	t_u32 ps_sta_ageout_timer;
    /** RTS threshold */
	t_u16 rts_threshold;
    /** fragmentation threshold */
	t_u16 frag_threshold;
    /**  retry_limit */
	t_u16 retry_limit;
    /**  pairwise update timeout in milliseconds */
	t_u32 pairwise_update_timeout;
    /** pairwise handshake retries */
	t_u32 pwk_retries;
    /**  groupwise update timeout in milliseconds */
	t_u32 groupwise_update_timeout;
    /** groupwise handshake retries */
	t_u32 gwk_retries;
    /** preamble type */
	t_u8 preamble_type;
    /** band cfg */
	Band_Config_t bandcfg;
    /** channel */
	t_u8 channel;
    /** auth mode */
	t_u16 auth_mode;
    /** encryption protocol */
	t_u16 protocol;
    /** key managment type */
	t_u16 key_mgmt;
    /** wep param */
	wep_param wep_cfg;
    /** wpa param */
	wpa_param wpa_cfg;
    /** Mgmt IE passthru mask */
	t_u32 mgmt_ie_passthru_mask;
	/*
	 * 11n HT Cap  HTCap_t  ht_cap
	 */
    /** HT Capabilities Info field */
	t_u16 ht_cap_info;
    /** A-MPDU Parameters field */
	t_u8 ampdu_param;
    /** Supported MCS Set field */
	t_u8 supported_mcs_set[16];
    /** HT Extended Capabilities field */
	t_u16 ht_ext_cap;
    /** Transmit Beamforming Capabilities field */
	t_u32 tx_bf_cap;
    /** Antenna Selection Capability field */
	t_u8 asel;
    /** Enable 2040 Coex */
	t_u8 enable_2040coex;
    /** key management operation */
	t_u16 key_mgmt_operation;
    /** BSS status */
	t_u16 bss_status;
#ifdef WIFI_DIRECT_SUPPORT
	/* pre shared key */
	t_u8 psk[MLAN_MAX_KEY_LENGTH];
#endif				/* WIFI_DIRECT_SUPPORT */
    /** Number of channels in scan_channel_list */
	t_u32 num_of_chan;
    /** scan channel list in ACS mode */
	scan_chan_list chan_list[MLAN_MAX_CHANNEL];
    /** Wmm parameters */
	wmm_parameter_t wmm_para;

} mlan_uap_bss_param;

/** mlan_uap_scan_channels */
typedef struct _mlan_uap_scan_channels {
    /** flag for remove nop channel*/
	t_u8 remove_nop_channel;
	/** num of removed channel */
	t_u8 num_remvoed_channel;
    /** Number of channels in scan_channel_list */
	t_u32 num_of_chan;
    /** scan channel list in ACS mode */
	scan_chan_list chan_list[MLAN_MAX_CHANNEL];
} mlan_uap_scan_channels;

/** mlan_uap_oper_ctrl */
typedef struct _mlan_uap_oper_ctrl {
    /** control value
      *  0: do nothing,
      *  2: uap stops and restarts automaticaly
     */
	t_u16 ctrl_value;
    /** channel opt
      *  1: uap restart on default 2.4G/channel 6
      *  2: uap restart on the band/channel configured by driver previously
      *  3: uap restart on the band/channel specified by band_cfg and channel
	*/
	t_u16 chan_opt;
    /** band cfg   0
      *  0: 20Mhz  2: 40 Mhz  3: 80Mhz
     */
	t_u8 band_cfg;
    /** channel */
	t_u8 channel;
} mlan_uap_oper_ctrl;

#endif

#ifdef WIFI_DIRECT_SUPPORT
/** mode: disable wifi direct */
#define WIFI_DIRECT_MODE_DISABLE		0
/** mode: listen */
#define WIFI_DIRECT_MODE_LISTEN			1
/** mode: GO */
#define WIFI_DIRECT_MODE_GO             2
/** mode: client */
#define WIFI_DIRECT_MODE_CLIENT			3
/** mode: find */
#define WIFI_DIRECT_MODE_FIND			4
/** mode: stop find */
#define WIFI_DIRECT_MODE_STOP_FIND		5
#endif

/** Type definition of mlan_ds_bss for MLAN_IOCTL_BSS */
typedef struct _mlan_ds_bss {
    /** Sub-command */
	t_u32 sub_command;
    /** BSS parameter */
	union {
	/** SSID-BSSID for MLAN_OID_BSS_START */
		mlan_ssid_bssid ssid_bssid;
	/** BSSID for MLAN_OID_BSS_STOP */
		mlan_802_11_mac_addr bssid;
	/** BSS mode for MLAN_OID_BSS_MODE */
		t_u32 bss_mode;
	/** BSS channel/frequency for MLAN_OID_BSS_CHANNEL */
		chan_freq bss_chan;
	/** BSS channel list for MLAN_OID_BSS_CHANNEL_LIST */
		mlan_chan_list chanlist;
	/** MAC address for MLAN_OID_BSS_MAC_ADDR */
		mlan_802_11_mac_addr mac_addr;
	/** Multicast list for MLAN_OID_BSS_MULTICAST_LIST */
		mlan_multicast_list multicast_list;
	/** Beacon interval for MLAN_OID_IBSS_BCN_INTERVAL */
		t_u32 bcn_interval;
	/** ATIM window for MLAN_OID_IBSS_ATIM_WINDOW */
		t_u32 atim_window;
	/** deauth param for MLAN_OID_BSS_STOP & MLAN_OID_UAP_DEAUTH_STA */
		mlan_deauth_param deauth_param;
#ifdef UAP_SUPPORT
	/** host based uap flag for MLAN_OID_BSS_START */
		t_u8 host_based;
	/** BSS param for AP mode for MLAN_OID_UAP_BSS_CONFIG */
		mlan_uap_bss_param bss_config;
	/** AP Wmm parameters for MLAN_OID_UAP_CFG_WMM_PARAM */
		wmm_parameter_t ap_wmm_para;
	/** ap scan channels for MLAN_OID_UAP_SCAN_CHANNELS*/
		mlan_uap_scan_channels ap_scan_channels;
	/** ap channel for MLAN_OID_UAP_CHANNEL*/
		chan_band_info ap_channel;
	/** ap operation control for MLAN_OID_UAP_OPER_CTRL*/
		mlan_uap_oper_ctrl ap_oper_ctrl;
#endif
#if defined(STA_SUPPORT) && defined(UAP_SUPPORT)
	/** BSS role for MLAN_OID_BSS_ROLE */
		t_u8 bss_role;
#endif
#ifdef WIFI_DIRECT_SUPPORT
	/** wifi direct mode for MLAN_OID_WIFI_DIRECT_MODE */
		t_u16 wfd_mode;
#endif
#ifdef STA_SUPPORT
	/** Listen interval for MLAN_OID_BSS_LISTEN_INTERVAL */
		t_u16 listen_interval;
	/** STA channel info for MLAN_OID_BSS_CHAN_INFO */
		chan_band_info sta_channel;
#endif
	} param;
} mlan_ds_bss, *pmlan_ds_bss;

/** Type definition of mlan_ds_custom_reg_domain */
typedef struct _mlan_ds_custom_reg_domain {
	t_u8 cfg_len;
	t_u8 cfg_buf[0];
} mlan_ds_custom_reg_domain;
/*-----------------------------------------------------------------*/
/** Radio Control Group */
/*-----------------------------------------------------------------*/
/** Enumeration for band */
enum _mlan_band_def {
	BAND_B = 1,
	BAND_G = 2,
	BAND_A = 4,
	BAND_GN = 8,
	BAND_AN = 16,
};

/** Channel bandwidth */
#define CHANNEL_BW_20MHZ             0
#define CHANNEL_BW_40MHZ_ABOVE       1
#define CHANNEL_BW_40MHZ_BELOW       3

/** RF antenna selection */
#define RF_ANTENNA_MASK(n)	((1<<(n))-1)
/** RF antenna auto select */
#define RF_ANTENNA_AUTO		0xFFFF

/** Type definition of mlan_ds_band_cfg for MLAN_OID_BAND_CFG */
typedef struct _mlan_ds_band_cfg {
    /** Infra band */
	t_u32 config_bands;
    /** Ad-hoc start band */
	t_u32 adhoc_start_band;
    /** Ad-hoc start channel */
	t_u32 adhoc_channel;
    /** Ad-hoc channel bandwidth */
	t_u32 adhoc_chan_bandwidth;
    /** fw supported band */
	t_u32 fw_bands;
} mlan_ds_band_cfg;

/** Type definition of mlan_ds_ant_cfg_1x1 for MLAN_OID_ANT_CFG */
typedef struct _mlan_ds_ant_cfg_1x1 {
    /** Antenna mode */
	t_u32 antenna;
    /** Evaluate time */
	t_u16 evaluate_time;
    /** Current antenna */
	t_u16 current_antenna;
} mlan_ds_ant_cfg_1x1, *pmlan_ds_ant_cfg_1x1;

/** Type definition of mlan_ds_remain_chan for MLAN_OID_REMAIN_CHAN_CFG */
typedef struct _mlan_ds_remain_chan {
    /** remove flag */
	t_u16 remove;
    /** status */
	t_u8 status;
    /** Band cfg */
	Band_Config_t bandcfg;
    /** channel */
	t_u8 channel;
    /** remain time: Unit ms*/
	t_u32 remain_period;
} mlan_ds_remain_chan, *pmlan_ds_remain_chan;

/** Type definition of mlan_ds_radio_cfg for MLAN_IOCTL_RADIO_CFG */
typedef struct _mlan_ds_radio_cfg {
    /** Sub-command */
	t_u32 sub_command;
    /** Radio control parameter */
	union {
	/** Radio on/off for MLAN_OID_RADIO_CTRL */
		t_u32 radio_on_off;
	/** Band info for MLAN_OID_BAND_CFG */
		mlan_ds_band_cfg band_cfg;
	/** Antenna info for MLAN_OID_ANT_CFG */
		mlan_ds_ant_cfg_1x1 ant_cfg_1x1;
	/** remain on channel for MLAN_OID_REMAIN_CHAN_CFG */
		mlan_ds_remain_chan remain_chan;
	} param;
} mlan_ds_radio_cfg, *pmlan_ds_radio_cfg;

enum COALESCE_OPERATION {
	RECV_FILTER_MATCH_TYPE_EQ = 0x80,
	RECV_FILTER_MATCH_TYPE_NE,
};

enum COALESCE_PACKET_TYPE {
	PACKET_TYPE_UNICAST = 1,
	PACKET_TYPE_MULTICAST = 2,
	PACKET_TYPE_BROADCAST = 3
};

#define COALESCE_MAX_RULES	8
#define COALESCE_MAX_BYTESEQ	4	/* non-adjustable */
#define COALESCE_MAX_FILTERS	4
#define MAX_COALESCING_DELAY	100	/* in msecs */
#define MAX_PATTERN_LEN         20
#define MAX_OFFSET_LEN          100

/** filt field param structure */
struct filt_field_param {
	/** Operation */
	t_u8 operation;
	/** Operand len */
	t_u8 operand_len;
	/** offset */
	t_u16 offset;
	/** Operand byte stream */
	t_u8 operand_byte_stream[COALESCE_MAX_BYTESEQ];
};

/** coalesce rule structure */
struct coalesce_rule {
	/** max coalescing delay */
	t_u16 max_coalescing_delay;
	/** number of fields */
	t_u8 num_of_fields;
	/** packet type */
	t_u8 pkt_type;
	struct filt_field_param params[COALESCE_MAX_FILTERS];
};

/** coalesce configuration structure */
typedef struct _mlan_ds_coalesce_cfg {
	t_u16 num_of_rules;
	struct coalesce_rule rule[COALESCE_MAX_RULES];
} mlan_ds_coalesce_cfg;

/*-----------------------------------------------------------------*/
/** SNMP MIB Group */
/*-----------------------------------------------------------------*/
/** Type definition of mlan_ds_snmp_mib for MLAN_IOCTL_SNMP_MIB */
typedef struct _mlan_ds_snmp_mib {
    /** Sub-command */
	t_u32 sub_command;
    /** SNMP MIB parameter */
	union {
	/** RTS threshold for MLAN_OID_SNMP_MIB_RTS_THRESHOLD */
		t_u32 rts_threshold;
	/** Fragment threshold for MLAN_OID_SNMP_MIB_FRAG_THRESHOLD */
		t_u32 frag_threshold;
	/** Retry count for MLAN_OID_SNMP_MIB_RETRY_COUNT */
		t_u32 retry_count;
	/** OID value for MLAN_OID_SNMP_MIB_DOT11D/H */
		t_u32 oid_value;
	/** DTIM period for MLAN_OID_SNMP_MIB_DTIM_PERIOD */
		t_u32 dtim_period;
	/** Singal_ext Enable for MLAN_OID_SNMP_MIB_SIGNALEXT_ENABLE */
		t_u8 signalext_enable;
	/** Control deauth when uap switch channel */
		t_u8 deauthctrl;
	} param;
} mlan_ds_snmp_mib, *pmlan_ds_snmp_mib;

/*-----------------------------------------------------------------*/
/** Status Information Group */
/*-----------------------------------------------------------------*/
/** Enumeration for ad-hoc status */
enum _mlan_adhoc_status {
	ADHOC_IDLE,
	ADHOC_STARTED,
	ADHOC_JOINED,
	ADHOC_COALESCED, ADHOC_STARTING
};

/** Get stats org structure */
typedef struct _mlan_ds_get_stats_org {
    /** Statistics counter */
    /** Multicast transmitted frame count */
	t_u32 mcast_tx_frame;
    /** Failure count */
	t_u32 failed;
    /** Retry count */
	t_u32 retry;
    /** Multi entry count */
	t_u32 multi_retry;
    /** Duplicate frame count */
	t_u32 frame_dup;
    /** RTS success count */
	t_u32 rts_success;
    /** RTS failure count */
	t_u32 rts_failure;
    /** Ack failure count */
	t_u32 ack_failure;
    /** Rx fragmentation count */
	t_u32 rx_frag;
    /** Multicast Tx frame count */
	t_u32 mcast_rx_frame;
    /** FCS error count */
	t_u32 fcs_error;
    /** Tx frame count */
	t_u32 tx_frame;
    /** WEP ICV error count */
	t_u32 wep_icv_error[4];
    /** beacon recv count */
	t_u32 bcn_rcv_cnt;
    /** beacon miss count */
	t_u32 bcn_miss_cnt;
    /** received amsdu count*/
	t_u32 amsdu_rx_cnt;
    /** received msdu count in amsdu*/
	t_u32 msdu_in_rx_amsdu_cnt;
    /** tx amsdu count*/
	t_u32 amsdu_tx_cnt;
    /** tx msdu count in amsdu*/
	t_u32 msdu_in_tx_amsdu_cnt;
} mlan_ds_get_stats_org;

/** Type definition of mlan_ds_get_stats for MLAN_OID_GET_STATS */
typedef struct _mlan_ds_get_stats {
    /** Statistics counter */
    /** Multicast transmitted frame count */
	t_u32 mcast_tx_frame;
    /** Failure count */
	t_u32 failed;
    /** Retry count */
	t_u32 retry;
    /** Multi entry count */
	t_u32 multi_retry;
    /** Duplicate frame count */
	t_u32 frame_dup;
    /** RTS success count */
	t_u32 rts_success;
    /** RTS failure count */
	t_u32 rts_failure;
    /** Ack failure count */
	t_u32 ack_failure;
    /** Rx fragmentation count */
	t_u32 rx_frag;
    /** Multicast Tx frame count */
	t_u32 mcast_rx_frame;
    /** FCS error count */
	t_u32 fcs_error;
    /** Tx frame count */
	t_u32 tx_frame;
    /** WEP ICV error count */
	t_u32 wep_icv_error[4];
    /** beacon recv count */
	t_u32 bcn_rcv_cnt;
    /** beacon miss count */
	t_u32 bcn_miss_cnt;
    /** received amsdu count*/
	t_u32 amsdu_rx_cnt;
    /** received msdu count in amsdu*/
	t_u32 msdu_in_rx_amsdu_cnt;
    /** tx amsdu count*/
	t_u32 amsdu_tx_cnt;
    /** tx msdu count in amsdu*/
	t_u32 msdu_in_tx_amsdu_cnt;

    /** Tx frag count */
	t_u32 tx_frag_cnt;
    /** Qos Tx frag count */
	t_u32 qos_tx_frag_cnt[8];
    /** Qos failed count */
	t_u32 qos_failed_cnt[8];
    /** Qos retry count */
	t_u32 qos_retry_cnt[8];
    /** Qos multi retry count */
	t_u32 qos_multi_retry_cnt[8];
    /** Qos frame dup count */
	t_u32 qos_frm_dup_cnt[8];
    /** Qos rts success count */
	t_u32 qos_rts_suc_cnt[8];
    /** Qos rts failure count */
	t_u32 qos_rts_failure_cnt[8];
    /** Qos ack failure count */
	t_u32 qos_ack_failure_cnt[8];
    /** Qos Rx frag count */
	t_u32 qos_rx_frag_cnt[8];
    /** Qos Tx frame count */
	t_u32 qos_tx_frm_cnt[8];
    /** Qos discarded frame count */
	t_u32 qos_discarded_frm_cnt[8];
    /** Qos mpdus Rx count */
	t_u32 qos_mpdus_rx_cnt[8];
    /** Qos retry rx count */
	t_u32 qos_retries_rx_cnt[8];
    /** CMAC ICV errors count */
	t_u32 cmacicv_errors;
    /** CMAC replays count */
	t_u32 cmac_replays;
    /** mgmt CCMP replays count */
	t_u32 mgmt_ccmp_replays;
    /** TKIP ICV errors count */
	t_u32 tkipicv_errors;
    /** TKIP replays count */
	t_u32 tkip_replays;
    /** CCMP decrypt errors count */
	t_u32 ccmp_decrypt_errors;
    /** CCMP replays count */
	t_u32 ccmp_replays;
    /** Tx amsdu count */
	t_u32 tx_amsdu_cnt;
    /** failed amsdu count */
	t_u32 failed_amsdu_cnt;
    /** retry amsdu count */
	t_u32 retry_amsdu_cnt;
    /** multi-retry amsdu count */
	t_u32 multi_retry_amsdu_cnt;
    /** Tx octets in amsdu count */
	t_u64 tx_octets_in_amsdu_cnt;
    /** amsdu ack failure count */
	t_u32 amsdu_ack_failure_cnt;
    /** Rx amsdu count */
	t_u32 rx_amsdu_cnt;
    /** Rx octets in amsdu count */
	t_u64 rx_octets_in_amsdu_cnt;
    /** Tx ampdu count */
	t_u32 tx_ampdu_cnt;
    /** tx mpdus in ampdu count */
	t_u32 tx_mpdus_in_ampdu_cnt;
    /** tx octets in ampdu count */
	t_u64 tx_octets_in_ampdu_cnt;
    /** ampdu Rx count */
	t_u32 ampdu_rx_cnt;
    /** mpdu in Rx ampdu count */
	t_u32 mpdu_in_rx_ampdu_cnt;
    /** Rx octets ampdu count */
	t_u64 rx_octets_in_ampdu_cnt;
    /** ampdu delimiter CRC error count */
	t_u32 ampdu_delimiter_crc_error_cnt;
} mlan_ds_get_stats, *pmlan_ds_get_stats;

/** Type definition of mlan_ds_get_stats for MLAN_OID_GET_STATS */
typedef struct _mlan_ds_get_correlated_time {
   /** System time */
	t_u64 sys_time;
   /** FW time */
	t_u64 hw_time;
} mlan_ds_get_correlated_time, *pmlan_ds_get_correlated_time;

/** Type definition of mlan_ds_uap_stats for MLAN_OID_GET_STATS */
typedef struct _mlan_ds_uap_stats {
    /** tkip mic failures */
	t_u32 tkip_mic_failures;
    /** ccmp decrypt errors */
	t_u32 ccmp_decrypt_errors;
    /** wep undecryptable count */
	t_u32 wep_undecryptable_count;
    /** wep icv error count */
	t_u32 wep_icv_error_count;
    /** decrypt failure count */
	t_u32 decrypt_failure_count;
    /** dot11 multicast tx count */
	t_u32 mcast_tx_count;
    /** dot11 failed count */
	t_u32 failed_count;
    /** dot11 retry count */
	t_u32 retry_count;
    /** dot11 multi retry count */
	t_u32 multi_retry_count;
    /** dot11 frame duplicate count */
	t_u32 frame_dup_count;
    /** dot11 rts success count */
	t_u32 rts_success_count;
    /** dot11 rts failure count */
	t_u32 rts_failure_count;
    /** dot11 ack failure count */
	t_u32 ack_failure_count;
    /** dot11 rx ragment count */
	t_u32 rx_fragment_count;
    /** dot11 mcast rx frame count */
	t_u32 mcast_rx_frame_count;
    /** dot11 fcs error count */
	t_u32 fcs_error_count;
    /** dot11 tx frame count */
	t_u32 tx_frame_count;
    /** dot11 rsna tkip cm invoked */
	t_u32 rsna_tkip_cm_invoked;
    /** dot11 rsna 4way handshake failures */
	t_u32 rsna_4way_hshk_failures;
} mlan_ds_uap_stats, *pmlan_ds_uap_stats;

/** Mask of last beacon RSSI */
#define BCN_RSSI_LAST_MASK              0x00000001
/** Mask of average beacon RSSI */
#define BCN_RSSI_AVG_MASK               0x00000002
/** Mask of last data RSSI */
#define DATA_RSSI_LAST_MASK             0x00000004
/** Mask of average data RSSI */
#define DATA_RSSI_AVG_MASK              0x00000008
/** Mask of last beacon SNR */
#define BCN_SNR_LAST_MASK               0x00000010
/** Mask of average beacon SNR */
#define BCN_SNR_AVG_MASK                0x00000020
/** Mask of last data SNR */
#define DATA_SNR_LAST_MASK              0x00000040
/** Mask of average data SNR */
#define DATA_SNR_AVG_MASK               0x00000080
/** Mask of last beacon NF */
#define BCN_NF_LAST_MASK                0x00000100
/** Mask of average beacon NF */
#define BCN_NF_AVG_MASK                 0x00000200
/** Mask of last data NF */
#define DATA_NF_LAST_MASK               0x00000400
/** Mask of average data NF */
#define DATA_NF_AVG_MASK                0x00000800
/** Mask of all RSSI_INFO */
#define ALL_RSSI_INFO_MASK              0x00000fff
#define MAX_PATH_NUM                    1
/** path A */
#define PATH_A							0x01
/** path B */
#define PATH_B							0x02
/** path AB */
#define PATH_AB							0x03
/** ALL the path */
#define PATH_ALL						0
/** Type definition of mlan_ds_get_signal for MLAN_OID_GET_SIGNAL */
typedef struct _mlan_ds_get_signal {
    /** Selector of get operation */
	/*
	 * Bit0:  Last Beacon RSSI,  Bit1:  Average Beacon RSSI,
	 * Bit2:  Last Data RSSI,    Bit3:  Average Data RSSI,
	 * Bit4:  Last Beacon SNR,   Bit5:  Average Beacon SNR,
	 * Bit6:  Last Data SNR,     Bit7:  Average Data SNR,
	 * Bit8:  Last Beacon NF,    Bit9:  Average Beacon NF,
	 * Bit10: Last Data NF,      Bit11: Average Data NF
	 *
	 * Bit0: PATH A
	 * Bit1: PATH B
	 */
	t_u16 selector;

    /** RSSI */
    /** RSSI of last beacon */
	t_s16 bcn_rssi_last;
    /** RSSI of beacon average */
	t_s16 bcn_rssi_avg;
    /** RSSI of last data packet */
	t_s16 data_rssi_last;
    /** RSSI of data packet average */
	t_s16 data_rssi_avg;

    /** SNR */
    /** SNR of last beacon */
	t_s16 bcn_snr_last;
    /** SNR of beacon average */
	t_s16 bcn_snr_avg;
    /** SNR of last data packet */
	t_s16 data_snr_last;
    /** SNR of data packet average */
	t_s16 data_snr_avg;

    /** NF */
    /** NF of last beacon */
	t_s16 bcn_nf_last;
    /** NF of beacon average */
	t_s16 bcn_nf_avg;
    /** NF of last data packet */
	t_s16 data_nf_last;
    /** NF of data packet average */
	t_s16 data_nf_avg;
} mlan_ds_get_signal, *pmlan_ds_get_signal;

/** bit for 2.4 G antenna diversity */
#define ANT_DIVERSITY_2G   MBIT(3)
/** bit for 5 G antenna diversity */
#define ANT_DIVERSITY_5G   MBIT(7)

/** mlan_fw_info data structure for MLAN_OID_GET_FW_INFO */
typedef struct _mlan_fw_info {
    /** Firmware version */
	t_u32 fw_ver;
    /** MAC address */
	mlan_802_11_mac_addr mac_addr;
    /** 802.11n device capabilities */
	t_u32 hw_dot_11n_dev_cap;
    /** Device support for MIMO abstraction of MCSs */
	t_u8 hw_dev_mcs_support;
    /** user's MCS setting */
	t_u8 usr_dev_mcs_support;
	/** fw supported band */
	t_u8 fw_bands;
	/** region code */
	t_u16 region_code;
    /** ECSA support */
	t_u8 ecsa_enable;
    /** Get log support */
	t_u8 getlog_enable;
    /** FW support for embedded supplicant */
	t_u8 fw_supplicant_support;
    /** ant info */
	t_u8 antinfo;
    /** max AP associated sta count supported by fw */
	t_u8 max_ap_assoc_sta;
    /** FW support roaming offload */
	t_u8 fw_roaming_support;
} mlan_fw_info, *pmlan_fw_info;

/** Version string buffer length */
#define MLAN_MAX_VER_STR_LEN    128

/** mlan_ver_ext data structure for MLAN_OID_GET_VER_EXT */
typedef struct _mlan_ver_ext {
    /** Selected version string */
	t_u32 version_str_sel;
    /** Version string */
	char version_str[MLAN_MAX_VER_STR_LEN];
} mlan_ver_ext, *pmlan_ver_ext;

#ifdef BIG_ENDIAN_SUPPORT
/** Extended Capabilities Data */
typedef struct MLAN_PACK_START _ExtCap_t {
    /** Extended Capabilities value */
	t_u8 rsvdBit79:1;	/* bit 79 */
	t_u8 rsvdBit78:1;	/* bit 78 */
	t_u8 rsvdBit77:1;	/* bit 77 */
	t_u8 rsvdBit76:1;	/* bit 76 */
	t_u8 rsvdBit75:1;	/* bit 75 */
	t_u8 rsvdBit74:1;	/* bit 74 */
	t_u8 rsvdBit73:1;	/* bit 73 */
	t_u8 FILS:1;		/* bit 72 */
	t_u8 FTMI:1;		/* bit 71 */
	t_u8 FTMR:1;		/* bit 70 */
	t_u8 CAQ:1;		/* bit 69 */
	t_u8 rsvdBit68:1;	/* bit 68 */
	t_u8 NCC:1;		/* bit 67 */
	t_u8 rsvdBit66:1;	/* bit 66 */
	t_u8 chanSchedMgnt:1;	/* bit 65 */
	t_u8 MaxAMSDU:2;	/* bit 63-bit 64 */
	t_u8 OperModeNtf:1;	/* bit 62 */
	t_u8 TDLSWildBandwidth:1;	/* bit 61 */
	t_u8 rsvdBit60:1;	/* bit 60 */
	t_u8 rsvdBit59:1;	/* bit 59 */
	t_u8 rsvdBit58:1;	/* bit 58 */
	t_u8 rsvdBit57:1;	/* bit 57 */
	t_u8 rsvdBit56:1;	/* bit 56 */
	t_u8 rsvdBit55:1;	/* bit 55 */
	t_u8 rsvdBit54:1;	/* bit 54 */
	t_u8 rsvdBit53:1;	/* bit 53 */
	t_u8 rsvdBit52:1;	/* bit 52 */
	t_u8 rsvdBit51:1;	/* bit 51 */
	t_u8 rsvdBit50:1;	/* bit 50 */
	t_u8 rsvdBit49:1;	/* bit 49 */
	t_u8 rsvdBit48:1;	/* bit 48 */
	t_u8 rsvdBit47:1;	/* bit 47 */
	t_u8 rsvdBit46:1;	/* bit 46 */
	t_u8 rsvdBit45:1;	/* bit 45 */
	t_u8 rsvdBit44:1;	/* bit 44 */
	t_u8 rsvdBit43:1;	/* bit 43 */
	t_u8 rsvdBit42:1;	/* bit 42 */
	t_u8 rsvdBit41:1;	/* bit 41 */
	t_u8 rsvdBit40:1;	/* bit 40 */
	t_u8 TDLSChlSwitchProhib:1;	/* bit 39 */
	t_u8 TDLSProhibited:1;	/* bit 38 */
	t_u8 TDLSSupport:1;	/* bit 37 */
	t_u8 MSGCF_Capa:1;	/* bit 36 */
	t_u8 Reserved35:1;	/* bit 35 */
	t_u8 SSPN_Interface:1;	/* bit 34 */
	t_u8 EBR:1;		/* bit 33 */
	t_u8 Qos_Map:1;		/* bit 32 */
	t_u8 Interworking:1;	/* bit 31 */
	t_u8 TDLSChannelSwitching:1;	/* bit 30 */
	t_u8 TDLSPeerPSMSupport:1;	/* bit 29 */
	t_u8 TDLSPeerUAPSDSupport:1;	/* bit 28 */
	t_u8 UTC:1;		/* bit 27 */
	t_u8 DMS:1;		/* bit 26 */
	t_u8 SSID_List:1;	/* bit 25 */
	t_u8 ChannelUsage:1;	/* bit 24 */
	t_u8 TimingMeasurement:1;	/* bit 23 */
	t_u8 MultipleBSSID:1;	/* bit 22 */
	t_u8 AC_StationCount:1;	/* bit 21 */
	t_u8 QoSTrafficCap:1;	/* bit 20 */
	t_u8 BSS_Transition:1;	/* bit 19 */
	t_u8 TIM_Broadcast:1;	/* bit 18 */
	t_u8 WNM_Sleep:1;	/* bit 17 */
	t_u8 TFS:1;		/* bit 16 */
	t_u8 GeospatialLocation:1;	/* bit 15 */
	t_u8 CivicLocation:1;	/* bit 14 */
	t_u8 CollocatedIntf:1;	/* bit 13 */
	t_u8 ProxyARPService:1;	/* bit 12 */
	t_u8 FMS:1;		/* bit 11 */
	t_u8 LocationTracking:1;	/* bit 10 */
	t_u8 MulticastDiagnostics:1;	/* bit 9  */
	t_u8 Diagnostics:1;	/* bit 8  */
	t_u8 Event:1;		/* bit 7  */
	t_u8 SPSMP_Support:1;	/* bit 6 */
	t_u8 Reserved5:1;	/* bit 5 */
	t_u8 PSMP_Capable:1;	/* bit 4 */
	t_u8 RejectUnadmFrame:1;	/* bit 3 */
	t_u8 ExtChanSwitching:1;	/* bit 2 */
	t_u8 Reserved1:1;	/* bit 1 */
	t_u8 BSS_CoexistSupport:1;	/* bit 0 */
} MLAN_PACK_END ExtCap_t, *pExtCap_t;
#else
/** Extended Capabilities Data */
typedef struct MLAN_PACK_START _ExtCap_t {
    /** Extended Capabilities value */
	t_u8 BSS_CoexistSupport:1;	/* bit 0 */
	t_u8 Reserved1:1;	/* bit 1 */
	t_u8 ExtChanSwitching:1;	/* bit 2 */
	t_u8 RejectUnadmFrame:1;	/* bit 3 */
	t_u8 PSMP_Capable:1;	/* bit 4 */
	t_u8 Reserved5:1;	/* bit 5 */
	t_u8 SPSMP_Support:1;	/* bit 6 */
	t_u8 Event:1;		/* bit 7  */
	t_u8 Diagnostics:1;	/* bit 8  */
	t_u8 MulticastDiagnostics:1;	/* bit 9  */
	t_u8 LocationTracking:1;	/* bit 10 */
	t_u8 FMS:1;		/* bit 11 */
	t_u8 ProxyARPService:1;	/* bit 12 */
	t_u8 CollocatedIntf:1;	/* bit 13 */
	t_u8 CivicLocation:1;	/* bit 14 */
	t_u8 GeospatialLocation:1;	/* bit 15 */
	t_u8 TFS:1;		/* bit 16 */
	t_u8 WNM_Sleep:1;	/* bit 17 */
	t_u8 TIM_Broadcast:1;	/* bit 18 */
	t_u8 BSS_Transition:1;	/* bit 19 */
	t_u8 QoSTrafficCap:1;	/* bit 20 */
	t_u8 AC_StationCount:1;	/* bit 21 */
	t_u8 MultipleBSSID:1;	/* bit 22 */
	t_u8 TimingMeasurement:1;	/* bit 23 */
	t_u8 ChannelUsage:1;	/* bit 24 */
	t_u8 SSID_List:1;	/* bit 25 */
	t_u8 DMS:1;		/* bit 26 */
	t_u8 UTC:1;		/* bit 27 */
	t_u8 TDLSPeerUAPSDSupport:1;	/* bit 28 */
	t_u8 TDLSPeerPSMSupport:1;	/* bit 29 */
	t_u8 TDLSChannelSwitching:1;	/* bit 30 */
	t_u8 Interworking:1;	/* bit 31 */
	t_u8 Qos_Map:1;		/* bit 32 */
	t_u8 EBR:1;		/* bit 33 */
	t_u8 SSPN_Interface:1;	/* bit 34 */
	t_u8 Reserved35:1;	/* bit 35 */
	t_u8 MSGCF_Capa:1;	/* bit 36 */
	t_u8 TDLSSupport:1;	/* bit 37 */
	t_u8 TDLSProhibited:1;	/* bit 38 */
	t_u8 TDLSChlSwitchProhib:1;	/* bit 39 */
	t_u8 rsvdBit40:1;	/* bit 40 */
	t_u8 rsvdBit41:1;	/* bit 41 */
	t_u8 rsvdBit42:1;	/* bit 42 */
	t_u8 rsvdBit43:1;	/* bit 43 */
	t_u8 rsvdBit44:1;	/* bit 44 */
	t_u8 rsvdBit45:1;	/* bit 45 */
	t_u8 rsvdBit46:1;	/* bit 46 */
	t_u8 rsvdBit47:1;	/* bit 47 */
	t_u8 rsvdBit48:1;	/* bit 48 */
	t_u8 rsvdBit49:1;	/* bit 49 */
	t_u8 rsvdBit50:1;	/* bit 50 */
	t_u8 rsvdBit51:1;	/* bit 51 */
	t_u8 rsvdBit52:1;	/* bit 52 */
	t_u8 rsvdBit53:1;	/* bit 53 */
	t_u8 rsvdBit54:1;	/* bit 54 */
	t_u8 rsvdBit55:1;	/* bit 55 */
	t_u8 rsvdBit56:1;	/* bit 56 */
	t_u8 rsvdBit57:1;	/* bit 57 */
	t_u8 rsvdBit58:1;	/* bit 58 */
	t_u8 rsvdBit59:1;	/* bit 59 */
	t_u8 rsvdBit60:1;	/* bit 60 */
	t_u8 TDLSWildBandwidth:1;	/* bit 61 */
	t_u8 OperModeNtf:1;	/* bit 62 */
	t_u8 MaxAMSDU:2;	/* bit 63-bit 64 */
	t_u8 chanSchedMgnt:1;	/* bit 65 */
	t_u8 rsvdBit66:1;	/* bit 66 */
	t_u8 NCC:1;		/* bit 67 */
	t_u8 rsvdBit68:1;	/* bit 68 */
	t_u8 CAQ:1;		/* bit 69 */
	t_u8 FTMR:1;		/* bit 70 */
	t_u8 FTMI:1;		/* bit 71 */
	t_u8 FILS:1;		/* bit 72 */
	t_u8 rsvdBit73:1;	/* bit 73 */
	t_u8 rsvdBit74:1;	/* bit 74 */
	t_u8 rsvdBit75:1;	/* bit 75 */
	t_u8 rsvdBit76:1;	/* bit 76 */
	t_u8 rsvdBit77:1;	/* bit 77 */
	t_u8 rsvdBit78:1;	/* bit 78 */
	t_u8 rsvdBit79:1;	/* bit 79 */
} MLAN_PACK_END ExtCap_t, *pExtCap_t;
#endif

/** ExtCap : TDLS prohibited */
#define IS_EXTCAP_TDLS_PROHIBITED(ext_cap) (ext_cap.TDLSProhibited)
/** ExtCap : TDLS channel switch prohibited */
#define IS_EXTCAP_TDLS_CHLSWITCHPROHIB(ext_cap) (ext_cap.TDLSChlSwitchProhib)

/** mlan_bss_info data structure for MLAN_OID_GET_BSS_INFO */
typedef struct _mlan_bss_info {
    /** BSS mode */
	t_u32 bss_mode;
    /** SSID */
	mlan_802_11_ssid ssid;
    /** Table index */
	t_u32 scan_table_idx;
    /** Channel */
	t_u32 bss_chan;
    /** Band */
	t_u8 bss_band;
    /** Region code */
	t_u32 region_code;
    /** Connection status */
	t_u32 media_connected;
    /** Radio on */
	t_u32 radio_on;
    /** Max power level in dBm */
	t_s32 max_power_level;
    /** Min power level in dBm */
	t_s32 min_power_level;
    /** Adhoc state */
	t_u32 adhoc_state;
    /** NF of last beacon */
	t_s32 bcn_nf_last;
    /** wep status */
	t_u32 wep_status;
    /** scan block status */
	t_u8 scan_block;
     /** Host Sleep configured flag */
	t_u32 is_hs_configured;
    /** Deep Sleep flag */
	t_u32 is_deep_sleep;
    /** BSSID */
	mlan_802_11_mac_addr bssid;
#ifdef STA_SUPPORT
    /** Capability Info */
	t_u16 capability_info;
    /** Beacon Interval */
	t_u16 beacon_interval;
    /** Listen Interval */
	t_u16 listen_interval;
    /** Association Id  */
	t_u16 assoc_id;
    /** AP/Peer supported rates */
	t_u8 peer_supp_rates[MLAN_SUPPORTED_RATES];
    /** extend capability for AP */
	ExtCap_t ext_cap;
#endif				/* STA_SUPPORT */
    /** Mobility Domain ID */
	t_u16 mdid;
    /** FT Capability policy */
	t_u8 ft_cap;
} mlan_bss_info, *pmlan_bss_info;

/** MAXIMUM number of TID */
#define MAX_NUM_TID     8

/** Max RX Win size */
#define MAX_RX_WINSIZE  64

/** rx_reorder_tbl */
typedef struct {
    /** TID */
	t_u16 tid;
    /** TA */
	t_u8 ta[MLAN_MAC_ADDR_LENGTH];
    /** Start window */
	t_u32 start_win;
    /** Window size */
	t_u32 win_size;
    /** amsdu flag */
	t_u8 amsdu;
    /** buffer status */
	t_u32 buffer[MAX_RX_WINSIZE];
} rx_reorder_tbl;

/** tx_ba_stream_tbl */
typedef struct {
    /** TID */
	t_u16 tid;
    /** RA */
	t_u8 ra[MLAN_MAC_ADDR_LENGTH];
    /** amsdu flag */
	t_u8 amsdu;
} tx_ba_stream_tbl;

/** Debug command number */
#define DBG_CMD_NUM	10

#ifdef SDIO_MULTI_PORT_TX_AGGR
/** sdio mp debug number */
#define SDIO_MP_DBG_NUM                  10
#endif

/** Maximum size of IEEE Information Elements */
#define IEEE_MAX_IE_SIZE      256

/** support up to 8 TDLS peer */
#define MLAN_MAX_TDLS_PEER_SUPPORTED     8
/** TDLS peer info */
typedef struct _tdls_peer_info {
    /** station mac address */
	t_u8 mac_addr[MLAN_MAC_ADDR_LENGTH];
    /** SNR */
	t_s8 snr;
    /** Noise Floor */
	t_s8 nf;
	/** Extended Capabilities IE */
	t_u8 ext_cap[IEEE_MAX_IE_SIZE];
    /** HT Capabilities IE */
	t_u8 ht_cap[IEEE_MAX_IE_SIZE];
} tdls_peer_info;

/** max ralist num */
#define MLAN_MAX_RALIST_NUM  8
/** ralist info */
typedef struct _ralist_info {
    /** RA list buffer */
	t_u8 ra[MLAN_MAC_ADDR_LENGTH];
    /** total packets in RA list */
	t_u16 total_pkts;
    /** tid num */
	t_u8 tid;
	/** tx_pause flag */
	t_u8 tx_pause;
} ralist_info;

/** mlan_debug_info data structure for MLAN_OID_GET_DEBUG_INFO */
typedef struct _mlan_debug_info {
	/* WMM AC_BK count */
	t_u32 wmm_ac_bk;
	/* WMM AC_BE count */
	t_u32 wmm_ac_be;
	/* WMM AC_VI count */
	t_u32 wmm_ac_vi;
	/* WMM AC_VO count */
	t_u32 wmm_ac_vo;
    /** Corresponds to max_tx_buf_size member of mlan_adapter*/
	t_u32 max_tx_buf_size;
     /** Corresponds to tx_buf_size member of mlan_adapter*/
	t_u32 tx_buf_size;
    /** Corresponds to curr_tx_buf_size member of mlan_adapter*/
	t_u32 curr_tx_buf_size;
    /** Tx table num */
	t_u32 tx_tbl_num;
    /** Tx ba stream table */
	tx_ba_stream_tbl tx_tbl[MLAN_MAX_TX_BASTREAM_SUPPORTED];
    /** Rx table num */
	t_u32 rx_tbl_num;
    /** Rx reorder table*/
	rx_reorder_tbl rx_tbl[MLAN_MAX_RX_BASTREAM_SUPPORTED];
    /** TDLS peer number */
	t_u32 tdls_peer_num;
    /** TDLS peer list*/
	tdls_peer_info tdls_peer_list[MLAN_MAX_TDLS_PEER_SUPPORTED];
    /** ralist num */
	t_u32 ralist_num;
    /** ralist info */
	ralist_info ralist[MLAN_MAX_RALIST_NUM];
    /** Corresponds to ps_mode member of mlan_adapter */
	t_u16 ps_mode;
    /** Corresponds to ps_state member of mlan_adapter */
	t_u32 ps_state;
#ifdef STA_SUPPORT
    /** Corresponds to is_deep_sleep member of mlan_adapter */
	t_u8 is_deep_sleep;
#endif /** STA_SUPPORT */
    /** Corresponds to pm_wakeup_card_req member of mlan_adapter */
	t_u8 pm_wakeup_card_req;
    /** Corresponds to pm_wakeup_fw_try member of mlan_adapter */
	t_u32 pm_wakeup_fw_try;
    /** time stamp when host try to wake up firmware */
	t_u32 pm_wakeup_in_secs;
    /** Corresponds to is_hs_configured member of mlan_adapter */
	t_u8 is_hs_configured;
    /** Corresponds to hs_activated member of mlan_adapter */
	t_u8 hs_activated;
    /** Corresponds to pps_uapsd_mode member of mlan_adapter */
	t_u16 pps_uapsd_mode;
    /** Corresponds to sleep_period.period member of mlan_adapter */
	t_u16 sleep_pd;
    /** Corresponds to wmm_qosinfo member of mlan_private */
	t_u8 qos_cfg;
    /** Corresponds to tx_lock_flag member of mlan_adapter */
	t_u8 tx_lock_flag;
    /** Corresponds to port_open member of mlan_private */
	t_u8 port_open;
    /** bypass pkt count */
	t_u16 bypass_pkt_count;
    /** Corresponds to scan_processing member of mlan_adapter */
	t_u32 scan_processing;
    /** Corresponds to mlan_processing member of mlan_adapter */
	t_u32 mlan_processing;
    /** Corresponds to main_lock_flag member of mlan_adapter */
	t_u32 main_lock_flag;
    /** Corresponds to main_process_cnt member of mlan_adapter */
	t_u32 main_process_cnt;
	/** Corresponds to delay_task_flag member of mlan_adapter */
	t_u32 delay_task_flag;
    /** mlan_rx_processing */
	t_u32 mlan_rx_processing;
    /** rx pkts queued */
	t_u32 rx_pkts_queued;
    /** Number of host to card command failures */
	t_u32 num_cmd_host_to_card_failure;
    /** Number of host to card sleep confirm failures */
	t_u32 num_cmd_sleep_cfm_host_to_card_failure;
    /** Number of host to card Tx failures */
	t_u32 num_tx_host_to_card_failure;
    /** Number of allocate buffer failure */
	t_u32 num_alloc_buffer_failure;
	/** Number of pkt dropped */
	t_u32 num_pkt_dropped;
    /** Number of card to host command/event failures */
	t_u32 num_cmdevt_card_to_host_failure;
    /** Number of card to host Rx failures */
	t_u32 num_rx_card_to_host_failure;
    /** Number of interrupt read failures */
	t_u32 num_int_read_failure;
    /** Last interrupt status */
	t_u32 last_int_status;
    /** number of interrupt receive */
	t_u32 num_of_irq;
    /** flag for sdio rx aggr */
	t_u8 sdio_rx_aggr;
    /** FW update port number */
	t_u32 mp_update[SDIO_MP_AGGR_DEF_PKT_LIMIT * 2];
	/** Invalid port update count */
	t_u32 mp_invalid_update;
#ifdef SDIO_MULTI_PORT_TX_AGGR
    /** Number of packets tx aggr */
	t_u32 mpa_tx_count[SDIO_MP_AGGR_DEF_PKT_LIMIT];
    /** no more packets count*/
	t_u32 mpa_sent_last_pkt;
    /** no write_ports count */
	t_u32 mpa_sent_no_ports;
	/** last recv wr_bitmap */
	t_u32 last_recv_wr_bitmap;
    /** last mp_wr_bitmap */
	t_u32 last_mp_wr_bitmap[SDIO_MP_DBG_NUM];
    /** last ports for cmd53 write data */
	t_u32 last_mp_wr_ports[SDIO_MP_DBG_NUM];
	/** last len for cmd53 write data */
	t_u32 last_mp_wr_len[SDIO_MP_DBG_NUM];
    /** last curr_wr_port */
	t_u8 last_curr_wr_port[SDIO_MP_DBG_NUM];
    /** length info for cmd53 write data */
	t_u16 last_mp_wr_info[SDIO_MP_DBG_NUM * SDIO_MP_AGGR_DEF_PKT_LIMIT];
    /** last mp_index */
	t_u8 last_mp_index;
    /** buffer for mp debug */
	t_u8 *mpa_buf;
    /** length info for mp buf size */
	t_u32 mpa_buf_size;
#endif
#ifdef SDIO_MULTI_PORT_RX_AGGR
    /** Number of packets rx aggr */
	t_u32 mpa_rx_count[SDIO_MP_AGGR_DEF_PKT_LIMIT];
#endif
    /** Number of deauthentication events */
	t_u32 num_event_deauth;
    /** Number of disassosiation events */
	t_u32 num_event_disassoc;
    /** Number of link lost events */
	t_u32 num_event_link_lost;
    /** Number of deauthentication commands */
	t_u32 num_cmd_deauth;
    /** Number of association comamnd successes */
	t_u32 num_cmd_assoc_success;
    /** Number of association command failures */
	t_u32 num_cmd_assoc_failure;
    /** Number of Tx timeouts */
	t_u32 num_tx_timeout;
    /** Number of command timeouts */
	t_u32 num_cmd_timeout;
    /** Number of command timeouts */
	t_u32 dbg_num_cmd_timeout;
    /** Timeout command ID */
	t_u16 timeout_cmd_id;
    /** Timeout command action */
	t_u16 timeout_cmd_act;
    /** List of last command IDs */
	t_u16 last_cmd_id[DBG_CMD_NUM];
    /** List of last command actions */
	t_u16 last_cmd_act[DBG_CMD_NUM];
    /** Last command index */
	t_u16 last_cmd_index;
    /** List of last command response IDs */
	t_u16 last_cmd_resp_id[DBG_CMD_NUM];
    /** Last command response index */
	t_u16 last_cmd_resp_index;
    /** List of last events */
	t_u16 last_event[DBG_CMD_NUM];
    /** Last event index */
	t_u16 last_event_index;
    /** Number of no free command node */
	t_u16 num_no_cmd_node;
    /** pending command id */
	t_u16 pending_cmd;
    /** time stamp for dnld last cmd */
	t_u32 dnld_cmd_in_secs;
    /** Corresponds to data_sent member of mlan_adapter */
	t_u8 data_sent;
    /** Corresponds to cmd_sent member of mlan_adapter */
	t_u8 cmd_sent;
    /** SDIO multiple port read bitmap */
	t_u32 mp_rd_bitmap;
    /** SDIO multiple port write bitmap */
	t_u32 mp_wr_bitmap;
    /** Current available port for read */
	t_u8 curr_rd_port;
    /** Current available port for write */
	t_u8 curr_wr_port;
    /** Corresponds to cmdresp_received member of mlan_adapter */
	t_u8 cmd_resp_received;
    /** Corresponds to event_received member of mlan_adapter */
	t_u8 event_received;
    /**  pendig tx pkts */
	t_u32 tx_pkts_queued;
#ifdef UAP_SUPPORT
    /**  pending bridge pkts */
	t_u16 num_bridge_pkts;
    /**  dropped pkts */
	t_u32 num_drop_pkts;
#endif
    /** mlan_adapter pointer */
	t_void *mlan_adapter;
    /** mlan_adapter_size */
	t_u32 mlan_adapter_size;
    /** mlan_priv vector */
	t_void *mlan_priv[MLAN_MAX_BSS_NUM];
    /** mlan_priv_size */
	t_u32 mlan_priv_size[MLAN_MAX_BSS_NUM];
    /** mlan_priv_num */
	t_u8 mlan_priv_num;
} mlan_debug_info, *pmlan_debug_info;

#ifdef UAP_SUPPORT
/** Maximum number of clients supported by AP */
#define MAX_NUM_CLIENTS         MAX_STA_COUNT

/** station info */
typedef struct _sta_info {
    /** STA MAC address */
	t_u8 mac_address[MLAN_MAC_ADDR_LENGTH];
    /** Power mfg status */
	t_u8 power_mfg_status;
    /** RSSI */
	t_s8 rssi;
    /** station bandmode */
	t_u8 bandmode;
} sta_info;

/** mlan_ds_sta_list structure for MLAN_OID_UAP_STA_LIST */
typedef struct _mlan_ds_sta_list {
    /** station count */
	t_u16 sta_count;
    /** station list */
	sta_info info[MAX_NUM_CLIENTS];
} mlan_ds_sta_list, *pmlan_ds_sta_list;
#endif

/** Type definition of mlan_ds_get_info for MLAN_IOCTL_GET_INFO */
typedef struct _mlan_ds_get_info {
    /** Sub-command */
	t_u32 sub_command;

    /** Status information parameter */
	union {
	/** Signal information for MLAN_OID_GET_SIGNAL */
		mlan_ds_get_signal signal;
		/** Signal path id for MLAN_OID_GET_SIGNAL_EXT */
		t_u16 path_id;
	/** Signal information for MLAN_OID_GET_SIGNAL_EXT */
		mlan_ds_get_signal signal_ext[MAX_PATH_NUM];
	/** Statistics information for MLAN_OID_GET_STATS */
		mlan_ds_get_stats stats;
	/** Firmware information for MLAN_OID_GET_FW_INFO */
		mlan_fw_info fw_info;
	/** Extended version information for MLAN_OID_GET_VER_EXT */
		mlan_ver_ext ver_ext;
	/** BSS information for MLAN_OID_GET_BSS_INFO */
		mlan_bss_info bss_info;
	/** Debug information for MLAN_OID_GET_DEBUG_INFO */
		t_u8 debug_info[1];
#ifdef UAP_SUPPORT
	/** UAP Statistics information for MLAN_OID_GET_STATS */
		mlan_ds_uap_stats ustats;
	/** UAP station list for MLAN_OID_UAP_STA_LIST */
		mlan_ds_sta_list sta_list;
#endif
	} param;
} mlan_ds_get_info, *pmlan_ds_get_info;

/*-----------------------------------------------------------------*/
/** Security Configuration Group */
/*-----------------------------------------------------------------*/
/** Enumeration for authentication mode */
enum _mlan_auth_mode {
	MLAN_AUTH_MODE_OPEN = 0x00,
	MLAN_AUTH_MODE_SHARED = 0x01,
	MLAN_AUTH_MODE_FT = 0x02,
	MLAN_AUTH_MODE_NETWORKEAP = 0x80,
	MLAN_AUTH_MODE_AUTO = 0xFF,
};

/**Enumeration for AssocAgent authentication mode, sync from FW.*/
typedef enum {
	AssocAgentAuth_Open,
	AssocAgentAuth_Shared,
	AssocAgentAuth_FastBss,
	AssocAgentAuth_FastBss_Skip,
	AssocAgentAuth_Network_EAP,
	AssocAgentAuth_Auto,
} AssocAgentAuthType_e;

/** Enumeration for encryption mode */
enum _mlan_encryption_mode {
	MLAN_ENCRYPTION_MODE_NONE = 0,
	MLAN_ENCRYPTION_MODE_WEP40 = 1,
	MLAN_ENCRYPTION_MODE_TKIP = 2,
	MLAN_ENCRYPTION_MODE_CCMP = 3,
	MLAN_ENCRYPTION_MODE_WEP104 = 4,
};

/** Enumeration for PSK */
enum _mlan_psk_type {
	MLAN_PSK_PASSPHRASE = 1,
	MLAN_PSK_PMK,
	MLAN_PSK_CLEAR,
	MLAN_PSK_QUERY,
};

/** The bit to indicate the key is for unicast */
#define MLAN_KEY_INDEX_UNICAST        0x40000000
/** The key index to indicate default key */
#define MLAN_KEY_INDEX_DEFAULT        0x000000ff
/** Maximum key length */
/* #define MLAN_MAX_KEY_LENGTH        32 */
/** Minimum passphrase length */
#define MLAN_MIN_PASSPHRASE_LENGTH    8
/** Maximum passphrase length */
#define MLAN_MAX_PASSPHRASE_LENGTH    63
/** PMK length */
#define MLAN_PMK_HEXSTR_LENGTH        64
/* A few details needed for WEP (Wireless Equivalent Privacy) */
/** 104 bits */
#define MAX_WEP_KEY_SIZE	13
/** 40 bits RC4 - WEP */
#define MIN_WEP_KEY_SIZE	5
/** packet number size */
#define PN_SIZE			16
/** max seq size of wpa/wpa2 key */
#define SEQ_MAX_SIZE        8

/** key flag for tx_seq */
#define KEY_FLAG_TX_SEQ_VALID	0x00000001
/** key flag for rx_seq */
#define KEY_FLAG_RX_SEQ_VALID	0x00000002
/** key flag for group key */
#define KEY_FLAG_GROUP_KEY      0x00000004
/** key flag for tx */
#define KEY_FLAG_SET_TX_KEY     0x00000008
/** key flag for mcast IGTK */
#define KEY_FLAG_AES_MCAST_IGTK 0x00000010
/** key flag for remove key */
#define KEY_FLAG_REMOVE_KEY     0x80000000

/** Type definition of mlan_ds_encrypt_key for MLAN_OID_SEC_CFG_ENCRYPT_KEY */
typedef struct _mlan_ds_encrypt_key {
    /** Key disabled, all other fields will be
     *  ignore when this flag set to MTRUE
     */
	t_u32 key_disable;
    /** key removed flag, when this flag is set
     *  to MTRUE, only key_index will be check
     */
	t_u32 key_remove;
    /** Key index, used as current tx key index
     *  when is_current_wep_key is set to MTRUE
     */
	t_u32 key_index;
    /** Current Tx key flag */
	t_u32 is_current_wep_key;
    /** Key length */
	t_u32 key_len;
    /** Key */
	t_u8 key_material[MLAN_MAX_KEY_LENGTH];
    /** mac address */
	t_u8 mac_addr[MLAN_MAC_ADDR_LENGTH];
    /** wapi key flag */
	t_u32 is_wapi_key;
    /** Initial packet number */
	t_u8 pn[PN_SIZE];
    /** key flags */
	t_u32 key_flags;
} mlan_ds_encrypt_key, *pmlan_ds_encrypt_key;

/** Type definition of mlan_passphrase_t */
typedef struct _mlan_passphrase_t {
    /** Length of passphrase */
	t_u32 passphrase_len;
    /** Passphrase */
	t_u8 passphrase[MLAN_MAX_PASSPHRASE_LENGTH];
} mlan_passphrase_t;

/** Type defnition of mlan_pmk_t */
typedef struct _mlan_pmk_t {
    /** PMK */
	t_u8 pmk[MLAN_MAX_KEY_LENGTH];
	t_u8 pmk_r0[MLAN_MAX_KEY_LENGTH];
	t_u8 pmk_r0_name[MLAN_MAX_PMKR0_NAME_LENGTH];
} mlan_pmk_t;

/** Embedded supplicant RSN type: No RSN */
#define RSN_TYPE_NO_RSN     MBIT(0)
/** Embedded supplicant RSN type: WPA */
#define RSN_TYPE_WPA        MBIT(3)
/** Embedded supplicant RSN type: WPA-NONE */
#define RSN_TYPE_WPANONE    MBIT(4)
/** Embedded supplicant RSN type: WPA2 */
#define RSN_TYPE_WPA2       MBIT(5)
/** Embedded supplicant RSN type: RFU */
#define RSN_TYPE_VALID_BITS (RSN_TYPE_NO_RSN | RSN_TYPE_WPA | RSN_TYPE_WPANONE | RSN_TYPE_WPA2)

/** Embedded supplicant cipher type: TKIP */
#define EMBED_CIPHER_TKIP       MBIT(2)
/** Embedded supplicant cipher type: AES */
#define EMBED_CIPHER_AES        MBIT(3)
/** Embedded supplicant cipher type: RFU */
#define EMBED_CIPHER_VALID_BITS (EMBED_CIPHER_TKIP | EMBED_CIPHER_AES)

/** Type definition of mlan_ds_passphrase for MLAN_OID_SEC_CFG_PASSPHRASE */
typedef struct _mlan_ds_passphrase {
    /** SSID may be used */
	mlan_802_11_ssid ssid;
    /** BSSID may be used */
	mlan_802_11_mac_addr bssid;
    /** Flag for passphrase or pmk used */
	t_u16 psk_type;
    /** Passphrase or PMK */
	union {
	/** Passphrase */
		mlan_passphrase_t passphrase;
	/** PMK */
		mlan_pmk_t pmk;
	} psk;
} mlan_ds_passphrase, *pmlan_ds_passphrase;

/** Type definition of mlan_ds_esupp_mode for MLAN_OID_SEC_CFG_ESUPP_MODE */
typedef struct _mlan_ds_ewpa_mode {
    /** RSN mode */
	t_u32 rsn_mode;
    /** Active pairwise cipher */
	t_u32 act_paircipher;
    /** Active pairwise cipher */
	t_u32 act_groupcipher;
} mlan_ds_esupp_mode, *pmlan_ds_esupp_mode;

/* Security SSID MAX number support by firmware*/
#define MAX_SEC_SSID_NUM 6

/** Type definition of mlan_ds_sec_cfg for MLAN_IOCTL_SEC_CFG */
typedef struct _mlan_ds_sec_cfg {
    /** Sub-command */
	t_u32 sub_command;
    /** Flag to extend some structures to support multiple values.
     ** For example, mlan_ds_passphrase can only contain one value,
     ** if need use mlan_ds_passphrase[N], just set this flag and
     ** use mlan_ds_passphrase[] instead to avoid modify
     ** more already exist code.
     */
	t_u8 multi_passphrase;
    /** Security configuration parameter */
	union {
	/** Authentication mode for MLAN_OID_SEC_CFG_AUTH_MODE */
		t_u32 auth_mode;
	/** Encryption mode for MLAN_OID_SEC_CFG_ENCRYPT_MODE */
		t_u32 encrypt_mode;
	/** WPA enabled flag for MLAN_OID_SEC_CFG_WPA_ENABLED */
		t_u32 wpa_enabled;
	/** WAPI enabled flag for MLAN_OID_SEC_CFG_WAPI_ENABLED */
		t_u32 wapi_enabled;
	/** Port Control enabled flag for MLAN_OID_SEC_CFG_PORT_CTRL */
		t_u32 port_ctrl_enabled;
	/** Encryption key for MLAN_OID_SEC_CFG_ENCRYPT_KEY */
		mlan_ds_encrypt_key encrypt_key;
	/** Passphrase for MLAN_OID_SEC_CFG_PASSPHRASE */
		mlan_ds_passphrase passphrase;
		/** Embedded supplicant WPA enabled flag for
		 *  MLAN_OID_SEC_CFG_EWPA_ENABLED
		 */
		t_u32 ewpa_enabled;
	/** Embedded supplicant mode for MLAN_OID_SEC_CFG_ESUPP_MODE */
		mlan_ds_esupp_mode esupp_mode;
		mlan_ds_passphrase roam_passphrase[MAX_SEC_SSID_NUM];
	} param;
} mlan_ds_sec_cfg, *pmlan_ds_sec_cfg;

/*-----------------------------------------------------------------*/
/** Rate Configuration Group */
/*-----------------------------------------------------------------*/
/** Enumeration for rate type */
enum _mlan_rate_type {
	MLAN_RATE_INDEX,
	MLAN_RATE_VALUE,
	MLAN_RATE_BITMAP
};

/** Enumeration for rate format */
enum _mlan_rate_format {
	MLAN_RATE_FORMAT_LG = 0,
	MLAN_RATE_FORMAT_HT,
	MLAN_RATE_FORMAT_AUTO = 0xFF,
};
/** Max bitmap rates size */
#define MAX_BITMAP_RATES_SIZE   18

/** Type definition of mlan_rate_cfg_t for MLAN_OID_RATE_CFG */
typedef struct _mlan_rate_cfg_t {
    /** Fixed rate: 0, auto rate: 1 */
	t_u32 is_rate_auto;
    /** Rate type. 0: index; 1: value; 2: bitmap */
	t_u32 rate_type;
    /** Rate/MCS index or rate value if fixed rate */
	t_u32 rate;
    /** Rate Bitmap */
	t_u16 bitmap_rates[MAX_BITMAP_RATES_SIZE];
	/* LG rate: 0, HT rate: 1, VHT rate: 2 */
	t_u32 rate_format;
} mlan_rate_cfg_t;

/** HT channel bandwidth */
typedef enum _mlan_ht_bw {
	MLAN_HT_BW20,
	MLAN_HT_BW40,
} mlan_ht_bw;

/** HT guard interval */
typedef enum _mlan_ht_gi {
	MLAN_HT_LGI,
	MLAN_HT_SGI,
} mlan_ht_gi;

/** Band and BSS mode */
typedef struct _mlan_band_data_rate {
    /** Band configuration */
	t_u8 config_bands;
    /** BSS mode (Infra or IBSS) */
	t_u8 bss_mode;
} mlan_band_data_rate;

/** Type definition of mlan_data_rate for MLAN_OID_GET_DATA_RATE */
typedef struct _mlan_data_rate {
    /** Tx data rate */
	t_u32 tx_data_rate;
    /** Rx data rate */
	t_u32 rx_data_rate;

    /** Tx channel bandwidth */
	t_u32 tx_ht_bw;
    /** Tx guard interval */
	t_u32 tx_ht_gi;
    /** Rx channel bandwidth */
	t_u32 rx_ht_bw;
    /** Rx guard interval */
	t_u32 rx_ht_gi;
    /** MCS index */
	t_u32 tx_mcs_index;
	t_u32 rx_mcs_index;
	/* LG rate: 0, HT rate: 1, VHT rate: 2 */
	t_u32 tx_rate_format;
	t_u32 rx_rate_format;
} mlan_data_rate;

/** Type definition of mlan_ds_rate for MLAN_IOCTL_RATE */
typedef struct _mlan_ds_rate {
    /** Sub-command */
	t_u32 sub_command;
    /** Rate configuration parameter */
	union {
	/** Rate configuration for MLAN_OID_RATE_CFG */
		mlan_rate_cfg_t rate_cfg;
	/** Data rate for MLAN_OID_GET_DATA_RATE */
		mlan_data_rate data_rate;
	/** Supported rates for MLAN_OID_SUPPORTED_RATES */
		t_u8 rates[MLAN_SUPPORTED_RATES];
	/** Band/BSS mode for getting supported rates */
		mlan_band_data_rate rate_band_cfg;
	} param;
} mlan_ds_rate, *pmlan_ds_rate;

/*-----------------------------------------------------------------*/
/** Power Configuration Group */
/*-----------------------------------------------------------------*/

/** Type definition of mlan_power_cfg_t for MLAN_OID_POWER_CFG */
typedef struct _mlan_power_cfg_t {
    /** Is power auto */
	t_u32 is_power_auto;
    /** Power level in dBm */
	t_s32 power_level;
} mlan_power_cfg_t;

/** max power table size */
#define MAX_POWER_TABLE_SIZE    128
#define TX_PWR_CFG_AUTO_CTRL_OFF    0xFF
#define MAX_POWER_GROUP     64
/** Type definition of mlan_power group info */
typedef struct mlan_power_group {

    /** rate format (LG: 0, HT: 1, VHT: 2, no auto ctrl: 0xFF) */
	t_u32 rate_format;
    /** bandwidth (LG: 20 MHz, HT: 20/40 MHz, VHT: 80/160/80+80 MHz) */
	t_u8 bandwidth;
    /** LG: first rate index, HT/VHT: first MCS */
	t_u8 first_rate_ind;
    /** LG: last rate index, HT/VHT: last MCS */
	t_u8 last_rate_ind;
    /** minmum tx power (dBm) */
	t_s8 power_min;
    /** maximum tx power (dBm) */
	t_s8 power_max;
    /** tx power step (dB) */
	t_s8 power_step;
} mlan_power_group;

/** Type definition of mlan_power_cfg_ext for MLAN_OID_POWER_CFG_EXT */
typedef struct _mlan_power_cfg_ext {
    /** number of power_groups */
	t_u32 num_pwr_grp;
    /** array of power groups */
	mlan_power_group power_group[MAX_POWER_GROUP];
} mlan_power_cfg_ext;

/** Type definition of mlan_ds_power_cfg for MLAN_IOCTL_POWER_CFG */
typedef struct _mlan_ds_power_cfg {
    /** Sub-command */
	t_u32 sub_command;
    /** Power configuration parameter */
	union {
	/** Power configuration for MLAN_OID_POWER_CFG */
		mlan_power_cfg_t power_cfg;
	/** Extended power configuration for MLAN_OID_POWER_CFG_EXT */
		mlan_power_cfg_ext power_ext;
	} param;
} mlan_ds_power_cfg, *pmlan_ds_power_cfg;

/*-----------------------------------------------------------------*/
/** Power Management Configuration Group */
/*-----------------------------------------------------------------*/
/** Host sleep config conditions : Cancel */
#define HOST_SLEEP_CFG_CANCEL   0xffffffff

/** Host sleep config condition: broadcast data */
#define HOST_SLEEP_COND_BROADCAST_DATA  MBIT(0)
/** Host sleep config condition: unicast data */
#define HOST_SLEEP_COND_UNICAST_DATA    MBIT(1)
/** Host sleep config condition: mac event */
#define HOST_SLEEP_COND_MAC_EVENT       MBIT(2)
/** Host sleep config condition: multicast data */
#define HOST_SLEEP_COND_MULTICAST_DATA  MBIT(3)
/** Host sleep config condition: IPV6 packet */
#define HOST_SLEEP_COND_IPV6_PACKET     MBIT(31)

/** Host sleep config conditions: Default */
#define HOST_SLEEP_DEF_COND     (HOST_SLEEP_COND_BROADCAST_DATA | HOST_SLEEP_COND_UNICAST_DATA | HOST_SLEEP_COND_MAC_EVENT)

/** Host sleep config GPIO : Default */
#define HOST_SLEEP_DEF_GPIO     0xff
/** Host sleep config gap : Default */
#define HOST_SLEEP_DEF_GAP      200
/** Host sleep config min wake holdoff */
#define HOST_SLEEP_DEF_WAKE_HOLDOFF 0;
/** Host sleep config inactivity timeout */
#define HOST_SLEEP_DEF_INACTIVITY_TIMEOUT 10;

/** Type definition of mlan_ds_hs_cfg for MLAN_OID_PM_CFG_HS_CFG */
typedef struct _mlan_ds_hs_cfg {
    /** MTRUE to invoke the HostCmd, MFALSE otherwise */
	t_u32 is_invoke_hostcmd;
    /** Host sleep config condition */
    /** Bit0: broadcast data
     *  Bit1: unicast data
     *  Bit2: mac event
     *  Bit3: multicast data
     */
	t_u32 conditions;
    /** GPIO pin or 0xff for interface */
	t_u32 gpio;
    /** Gap in milliseconds or or 0xff for special
     *  setting when GPIO is used to wakeup host
     */
	t_u32 gap;
    /** Host sleep wake interval */
	t_u32 hs_wake_interval;
    /** Parameter type for indication gpio*/
	t_u8 param_type_ind;
    /** GPIO pin for indication wakeup source */
	t_u32 ind_gpio;
    /** Level on ind_gpio pin for indication normal wakeup source */
	t_u32 level;
    /** Parameter type for extend hscfg*/
	t_u8 param_type_ext;
    /** Events that will be forced ignore*/
	t_u32 event_force_ignore;
    /** Events that will use extend gap to inform host*/
	t_u32 event_use_ext_gap;
    /** Ext gap*/
	t_u8 ext_gap;
    /** GPIO wave level for extend hscfg*/
	t_u8 gpio_wave;
} mlan_ds_hs_cfg, *pmlan_ds_hs_cfg;

#define MAX_MGMT_FRAME_FILTER 2
typedef struct _mlan_mgmt_frame_wakeup {
    /** action - bitmap
     ** On matching rx'd pkt and filter during NON_HOSTSLEEP mode:
     **   Action[1]=0  Discard
     **   Action[1]=1  Allow
     ** Note that default action on non-match is "Allow".
     **
     ** On matching rx'd pkt and filter during HOSTSLEEP mode:
     **   Action[1:0]=00  Discard and Not Wake host
     **   Action[1:0]=01  Discard and Wake host
     **   Action[1:0]=10  Invalid
     ** Note that default action on non-match is "Discard and Not Wake host".
     **/
	t_u32 action;
    /** Frame type(p2p, tdls...)
     ** type=0: invalid
     ** type=1: p2p
     ** type=others: reserved
     **/
	t_u32 type;
    /** Frame mask according to each type
     ** When type=1 for p2p, frame-mask have following define:
     **    Bit      Frame
     **     0       GO Negotiation Request
     **     1       GO Negotiation Response
     **     2       GO Negotiation Confirmation
     **     3       P2P Invitation Request
     **     4       P2P Invitation Response
     **     5       Device Discoverability Request
     **     6       Device Discoverability Response
     **     7       Provision Discovery Request
     **     8       Provision Discovery Response
     **     9       Notice of Absence
     **     10      P2P Presence Request
     **     11      P2P Presence Response
     **     12      GO Discoverability Request
     **     13-31   Reserved
     **
     ** When type=others, frame-mask is reserved.
     **/
	t_u32 frame_mask;
} mlan_mgmt_frame_wakeup, *pmlan_mgmt_frame_wakeup;

/** Enable deep sleep mode */
#define DEEP_SLEEP_ON  1
/** Disable deep sleep mode */
#define DEEP_SLEEP_OFF 0

/** Default idle time in milliseconds for auto deep sleep */
#define DEEP_SLEEP_IDLE_TIME	100

typedef struct _mlan_ds_auto_ds {
    /** auto ds mode, 0 - disable, 1 - enable */
	t_u16 auto_ds;
    /** auto ds idle time in milliseconds */
	t_u16 idletime;
} mlan_ds_auto_ds;

/** Type definition of mlan_ds_inactivity_to
 *  for MLAN_OID_PM_CFG_INACTIVITY_TO
 */
typedef struct _mlan_ds_inactivity_to {
    /** Timeout unit in microsecond, 0 means 1000us (1ms) */
	t_u32 timeout_unit;
    /** Inactivity timeout for unicast data */
	t_u32 unicast_timeout;
    /** Inactivity timeout for multicast data */
	t_u32 mcast_timeout;
    /** Timeout for additional Rx traffic after Null PM1 packet exchange */
	t_u32 ps_entry_timeout;
} mlan_ds_inactivity_to, *pmlan_ds_inactivity_to;

/** Minimum sleep period in milliseconds */
#define MIN_SLEEP_PERIOD    10
/** Maximum sleep period in milliseconds */
#define MAX_SLEEP_PERIOD    60
/** Special setting for UPSD certification tests */
#define SLEEP_PERIOD_RESERVED_FF    0xFF

/** PS null interval disable */
#define PS_NULL_DISABLE         (-1)

/** Local listen interval disable */
#define MRVDRV_LISTEN_INTERVAL_DISABLE   (-1)
/** Minimum listen interval */
#define MRVDRV_MIN_LISTEN_INTERVAL       0

/** Minimum multiple DTIM */
#define MRVDRV_MIN_MULTIPLE_DTIM                0
/** Maximum multiple DTIM */
#define MRVDRV_MAX_MULTIPLE_DTIM                5
/** Ignore multiple DTIM */
#define MRVDRV_IGNORE_MULTIPLE_DTIM             0xfffe
/** Match listen interval to closest DTIM */
#define MRVDRV_MATCH_CLOSEST_DTIM               0xfffd

/** Minimum adhoc awake period */
#define MIN_ADHOC_AWAKE_PD      0
/** Maximum adhoc awake period */
#define MAX_ADHOC_AWAKE_PD      31
/** Special adhoc awake period */
#define SPECIAL_ADHOC_AWAKE_PD  255

/** Minimum beacon miss timeout in milliseconds */
#define MIN_BCN_MISS_TO         0
/** Maximum beacon miss timeout in milliseconds */
#define MAX_BCN_MISS_TO         50
/** Disable beacon miss timeout */
#define DISABLE_BCN_MISS_TO     65535

/** Minimum delay to PS in milliseconds */
#define MIN_DELAY_TO_PS         0
/** Maximum delay to PS in milliseconds */
#define MAX_DELAY_TO_PS         65535
/** Delay to PS unchanged */
#define DELAY_TO_PS_UNCHANGED   (-1)
/** Default delay to PS in milliseconds */
#define DELAY_TO_PS_DEFAULT     1000

/** PS mode: Unchanged */
#define PS_MODE_UNCHANGED       0
/** PS mode: Auto */
#define PS_MODE_AUTO            1
/** PS mode: Poll */
#define PS_MODE_POLL            2
/** PS mode: Null */
#define PS_MODE_NULL            3

/** Type definition of mlan_ds_ps_cfg for MLAN_OID_PM_CFG_PS_CFG */
typedef struct _mlan_ds_ps_cfg {
    /** PS null interval in seconds */
	t_u32 ps_null_interval;
    /** Multiple DTIM interval */
	t_u32 multiple_dtim_interval;
    /** Listen interval */
	t_u32 listen_interval;
    /** Adhoc awake period */
	t_u32 adhoc_awake_period;
    /** Beacon miss timeout in milliseconds */
	t_u32 bcn_miss_timeout;
    /** Delay to PS in milliseconds */
	t_s32 delay_to_ps;
    /** PS mode */
	t_u32 ps_mode;
} mlan_ds_ps_cfg, *pmlan_ds_ps_cfg;

/** Type definition of mlan_ds_sleep_params for MLAN_OID_PM_CFG_SLEEP_PARAMS */
typedef struct _mlan_ds_sleep_params {
    /** Error */
	t_u32 error;
    /** Offset in microseconds */
	t_u32 offset;
    /** Stable time in microseconds */
	t_u32 stable_time;
    /** Calibration control */
	t_u32 cal_control;
    /** External sleep clock */
	t_u32 ext_sleep_clk;
    /** Reserved */
	t_u32 reserved;
} mlan_ds_sleep_params, *pmlan_ds_sleep_params;

/** sleep_param */
typedef struct _ps_sleep_param {
    /** control bitmap */
	t_u32 ctrl_bitmap;
    /** minimum sleep period (micro second) */
	t_u32 min_sleep;
    /** maximum sleep period (micro second) */
	t_u32 max_sleep;
} ps_sleep_param;

/** inactivity sleep_param */
typedef struct _inact_sleep_param {
    /** inactivity timeout (micro second) */
	t_u32 inactivity_to;
    /** miniumu awake period (micro second) */
	t_u32 min_awake;
    /** maximum awake period (micro second) */
	t_u32 max_awake;
} inact_sleep_param;

/** flag for ps mode */
#define PS_FLAG_PS_MODE                 1
/** flag for sleep param */
#define PS_FLAG_SLEEP_PARAM             2
/** flag for inactivity sleep param */
#define PS_FLAG_INACT_SLEEP_PARAM       4

/** Enable Robust Coex mode */
#define ROBUSTCOEX_GPIOCFG_ENABLE            1
/** Disable Robust Coex mode */
#define ROBUSTCOEX_GPIOCFG_DISABLE           0

/** Disable power mode */
#define PS_MODE_DISABLE                      0
/** Enable periodic dtim ps */
#define PS_MODE_PERIODIC_DTIM                1
/** Enable inactivity ps */
#define PS_MODE_INACTIVITY                   2
/** FW wake up method interface */
#define FW_WAKEUP_METHOD_INTERFACE           1
/** FW wake up method gpio */
#define FW_WAKEUP_METHOD_GPIO                2
/** mlan_ds_ps_mgmt */
typedef struct _mlan_ds_ps_mgmt {
    /** flags for valid field */
	t_u16 flags;
    /** power mode */
	t_u16 ps_mode;
    /** sleep param */
	ps_sleep_param sleep_param;
    /** inactivity sleep param */
	inact_sleep_param inact_param;
} mlan_ds_ps_mgmt;

/** mlan_ds_ps_info */
typedef struct _mlan_ds_ps_info {
    /** suspend allowed flag */
	t_u32 is_suspend_allowed;
} mlan_ds_ps_info;

/** Type definition of mlan_ds_wakeup_reason for MLAN_OID_PM_HS_WAKEUP_REASON */
typedef struct _mlan_ds_hs_wakeup_reason {
	t_u16 hs_wakeup_reason;
} mlan_ds_hs_wakeup_reason;

/** Type definition of mlan_fw_wakeup_params for MLAN_OID_PM_CFG_FW_WAKEUP_METHOD */
typedef struct _mlan_fw_wakeup_params {
    /** FW wakeup method */
	t_u16 method;
    /** GPIO pin NO.*/
	t_u8 gpio_pin;
} mlan_fw_wakeup_params, *pmlan_fw_wakeup_params;

/** Type definition of mlan_ds_ps_cfg for MLAN_OID_PM_CFG_PS_CFG */
typedef struct _mlan_ds_bcn_timeout {
    /** Beacon miss timeout period window */
	t_u16 bcn_miss_tmo_window;
    /** Beacon miss timeout period */
	t_u16 bcn_miss_tmo_period;
    /** Beacon reacquire timeout period window */
	t_u16 bcn_rq_tmo_window;
    /** Beacon reacquire timeout period */
	t_u16 bcn_rq_tmo_period;
} mlan_ds_bcn_timeout, *pmlan_ds_bcn_timeout;

/** Type definition of mlan_ds_pm_cfg for MLAN_IOCTL_PM_CFG */
typedef struct _mlan_ds_pm_cfg {
    /** Sub-command */
	t_u32 sub_command;
    /** Power management parameter */
	union {
	/** Power saving mode for MLAN_OID_PM_CFG_IEEE_PS */
		t_u32 ps_mode;
	/** Host Sleep configuration for MLAN_OID_PM_CFG_HS_CFG */
		mlan_ds_hs_cfg hs_cfg;
	/** Deep sleep mode for MLAN_OID_PM_CFG_DEEP_SLEEP */
		mlan_ds_auto_ds auto_deep_sleep;
	/** Inactivity timeout for MLAN_OID_PM_CFG_INACTIVITY_TO */
		mlan_ds_inactivity_to inactivity_to;
	/** Sleep period for MLAN_OID_PM_CFG_SLEEP_PD */
		t_u32 sleep_period;
	/** PS configuration parameters for MLAN_OID_PM_CFG_PS_CFG */
		mlan_ds_ps_cfg ps_cfg;
	/** FW wakeup method for MLAN_OID_PM_CFG_FW_WAKEUP_METHOD */
		mlan_fw_wakeup_params fw_wakeup_params;
	/** PS configuration parameters for MLAN_OID_PM_CFG_SLEEP_PARAMS */
		mlan_ds_sleep_params sleep_params;
	/** PS configuration parameters for MLAN_OID_PM_CFG_PS_MODE */
		mlan_ds_ps_mgmt ps_mgmt;
	/** power info for MLAN_OID_PM_INFO */
		mlan_ds_ps_info ps_info;
	/** hs wakeup reason for MLAN_OID_PM_HS_WAKEUP_REASON */
		mlan_ds_hs_wakeup_reason wakeup_reason;
	/** config manamgement frame for hs wakeup */
		mlan_mgmt_frame_wakeup mgmt_filter[MAX_MGMT_FRAME_FILTER];
	/** Beacon timout parameters for MLAN_OID_PM_CFG_BCN_TIMEOUT */
		mlan_ds_bcn_timeout bcn_timeout;
	} param;
} mlan_ds_pm_cfg, *pmlan_ds_pm_cfg;

#ifdef RX_PACKET_COALESCE
typedef struct {
	mlan_cmd_result_e cmd_result; /**< Firmware execution result */

	t_u32 pkt_threshold;
			 /** Packet threshold */
	t_u16 delay;
		  /** Timeout value in milliseconds */
} wlan_ioctl_rx_pkt_coalesce_config_t;
#endif

/*-----------------------------------------------------------------*/
/** WMM Configuration Group */
/*-----------------------------------------------------------------*/

/** WMM TSpec size */
#define MLAN_WMM_TSPEC_SIZE             63
/** WMM Add TS extra IE bytes */
#define MLAN_WMM_ADDTS_EXTRA_IE_BYTES   256
/** WMM statistics for packets hist bins */
#define MLAN_WMM_STATS_PKTS_HIST_BINS   7
/** Maximum number of AC QOS queues available */
#define MLAN_WMM_MAX_AC_QUEUES          4

/**
 *  @brief IOCTL structure to send an ADDTS request and retrieve the response.
 *
 *  IOCTL structure from the application layer relayed to firmware to
 *    instigate an ADDTS management frame with an appropriate TSPEC IE as well
 *    as any additional IEs appended in the ADDTS Action frame.
 *
 *  @sa woal_wmm_addts_req_ioctl
 */
typedef struct {
	mlan_cmd_result_e cmd_result; /**< Firmware execution result */

	t_u32 timeout_ms;	      /**< Timeout value in milliseconds */
	t_u8 ieee_status_code;	      /**< IEEE status code */

	t_u32 ie_data_len;	      /**< Length of ie block in ie_data */
	t_u8 ie_data[MLAN_WMM_TSPEC_SIZE
				      /**< TSPEC to send in the ADDTS */
		     + MLAN_WMM_ADDTS_EXTRA_IE_BYTES];
						    /**< Extra IE buf*/
} wlan_ioctl_wmm_addts_req_t;

/**
 *  @brief IOCTL structure to send a DELTS request.
 *
 *  IOCTL structure from the application layer relayed to firmware to
 *    instigate an DELTS management frame with an appropriate TSPEC IE.
 *
 *  @sa woal_wmm_delts_req_ioctl
 */
typedef struct {
	mlan_cmd_result_e cmd_result;
				  /**< Firmware execution result */
	t_u8 ieee_reason_code;	  /**< IEEE reason code sent, unused for WMM */
	t_u32 ie_data_len;	  /**< Length of ie block in ie_data */
	t_u8 ie_data[MLAN_WMM_TSPEC_SIZE];
				       /**< TSPEC to send in the DELTS */
} wlan_ioctl_wmm_delts_req_t;

/**
 *  @brief IOCTL structure to configure a specific AC Queue's parameters
 *
 *  IOCTL structure from the application layer relayed to firmware to
 *    get, set, or default the WMM AC queue parameters.
 *
 *  - msdu_lifetime_expiry is ignored if set to 0 on a set command
 *
 *  @sa woal_wmm_queue_config_ioctl
 */
typedef struct {
	mlan_wmm_queue_config_action_e action;/**< Set, Get, or Default */
	mlan_wmm_ac_e access_category;	      /**< WMM_AC_BK(0) to WMM_AC_VO(3) */
	t_u16 msdu_lifetime_expiry;	      /**< lifetime expiry in TUs */
	t_u8 supported_rates[10];	      /**< Not supported yet */
} wlan_ioctl_wmm_queue_config_t;

/**
 *  @brief IOCTL structure to start, stop, and get statistics for a WMM AC
 *
 *  IOCTL structure from the application layer relayed to firmware to
 *    start or stop statistical collection for a given AC.  Also used to
 *    retrieve and clear the collected stats on a given AC.
 *
 *  @sa woal_wmm_queue_stats_ioctl
 */
typedef struct {
    /** Action of Queue Config : Start, Stop, or Get */
	mlan_wmm_queue_stats_action_e action;
    /** User Priority */
	t_u8 user_priority;
    /** Number of successful packets transmitted */
	t_u16 pkt_count;
    /** Packets lost; not included in pkt_count */
	t_u16 pkt_loss;
    /** Average Queue delay in microseconds */
	t_u32 avg_queue_delay;
    /** Average Transmission delay in microseconds */
	t_u32 avg_tx_delay;
    /** Calculated used time in units of 32 microseconds */
	t_u16 used_time;
    /** Calculated policed time in units of 32 microseconds */
	t_u16 policed_time;
    /** Queue Delay Histogram; number of packets per queue delay range
     *
     *  [0] -  0ms <= delay < 5ms
     *  [1] -  5ms <= delay < 10ms
     *  [2] - 10ms <= delay < 20ms
     *  [3] - 20ms <= delay < 30ms
     *  [4] - 30ms <= delay < 40ms
     *  [5] - 40ms <= delay < 50ms
     *  [6] - 50ms <= delay < msduLifetime (TUs)
     */
	t_u16 delay_histogram[MLAN_WMM_STATS_PKTS_HIST_BINS];
} wlan_ioctl_wmm_queue_stats_t,
/** Type definition of mlan_ds_wmm_queue_stats
 *  for MLAN_OID_WMM_CFG_QUEUE_STATS
 */
mlan_ds_wmm_queue_stats, *pmlan_ds_wmm_queue_stats;

/**
 *  @brief IOCTL sub structure for a specific WMM AC Status
 */
typedef struct {
    /** WMM Acm */
	t_u8 wmm_acm;
    /** Flow required flag */
	t_u8 flow_required;
    /** Flow created flag */
	t_u8 flow_created;
    /** Disabled flag */
	t_u8 disabled;
} wlan_ioctl_wmm_queue_status_ac_t;

/**
 *  @brief IOCTL structure to retrieve the WMM AC Queue status
 *
 *  IOCTL structure from the application layer to retrieve:
 *     - ACM bit setting for the AC
 *     - Firmware status (flow required, flow created, flow disabled)
 *
 *  @sa woal_wmm_queue_status_ioctl
 */
typedef struct {
    /** WMM AC queue status */
	wlan_ioctl_wmm_queue_status_ac_t ac_status[MLAN_WMM_MAX_AC_QUEUES];
} wlan_ioctl_wmm_queue_status_t,
/** Type definition of mlan_ds_wmm_queue_status
 *  for MLAN_OID_WMM_CFG_QUEUE_STATUS
 */
mlan_ds_wmm_queue_status, *pmlan_ds_wmm_queue_status;

/** Type definition of mlan_ds_wmm_addts for MLAN_OID_WMM_CFG_ADDTS */
typedef struct _mlan_ds_wmm_addts {
    /** Result of ADDTS request */
	mlan_cmd_result_e result;
    /** Timeout value in milliseconds */
	t_u32 timeout;
    /** IEEE status code */
	t_u32 status_code;
    /** Dialog token */
	t_u8 dialog_tok;
    /** TSPEC data length */
	t_u32 ie_data_len;
    /** TSPEC to send in the ADDTS + buffering for any extra IEs */
	t_u8 ie_data[MLAN_WMM_TSPEC_SIZE + MLAN_WMM_ADDTS_EXTRA_IE_BYTES];
} mlan_ds_wmm_addts, *pmlan_ds_wmm_addts;

/** Type definition of mlan_ds_wmm_delts for MLAN_OID_WMM_CFG_DELTS */
typedef struct _mlan_ds_wmm_delts {
    /** Result of DELTS request */
	mlan_cmd_result_e result;
    /** IEEE status code */
	t_u32 status_code;
    /** TSPEC data length */
	t_u8 ie_data_len;
    /** TSPEC to send in the DELTS */
	t_u8 ie_data[MLAN_WMM_TSPEC_SIZE];
} mlan_ds_wmm_delts, *pmlan_ds_wmm_delts;

/** Type definition of mlan_ds_wmm_queue_config
 *  for MLAN_OID_WMM_CFG_QUEUE_CONFIG
 */
typedef struct _mlan_ds_wmm_queue_config {
    /** Action of Queue Config : Set, Get, or Default */
	mlan_wmm_queue_config_action_e action;
    /** WMM Access Category: WMM_AC_BK(0) to WMM_AC_VO(3) */
	mlan_wmm_ac_e access_category;
    /** Lifetime expiry in TUs */
	t_u16 msdu_lifetime_expiry;
    /** Reserve for future use */
	t_u8 reserved[10];
} mlan_ds_wmm_queue_config, *pmlan_ds_wmm_queue_config;

/** Type definition of mlan_ds_wmm_cfg for MLAN_IOCTL_WMM_CFG */
typedef struct _mlan_ds_wmm_cfg {
    /** Sub-command */
	t_u32 sub_command;
    /** WMM configuration parameter */
	union {
	/** WMM enable for MLAN_OID_WMM_CFG_ENABLE */
		t_u32 wmm_enable;
	/** QoS configuration for MLAN_OID_WMM_CFG_QOS */
		t_u8 qos_cfg;
	/** WMM add TS for MLAN_OID_WMM_CFG_ADDTS */
		mlan_ds_wmm_addts addts;
	/** WMM delete TS for MLAN_OID_WMM_CFG_DELTS */
		mlan_ds_wmm_delts delts;
	/** WMM queue configuration for MLAN_OID_WMM_CFG_QUEUE_CONFIG */
		mlan_ds_wmm_queue_config q_cfg;
	/** AC Parameters Record WMM_AC_BE, WMM_AC_BK, WMM_AC_VI, WMM_AC_VO */
		wmm_ac_parameters_t ac_params[MAX_AC_QUEUES];
	/** WMM queue status for MLAN_OID_WMM_CFG_QUEUE_STATS */
		mlan_ds_wmm_queue_stats q_stats;
	/** WMM queue status for MLAN_OID_WMM_CFG_QUEUE_STATUS */
		mlan_ds_wmm_queue_status q_status;
	/** WMM TS status for MLAN_OID_WMM_CFG_TS_STATUS */
		mlan_ds_wmm_ts_status ts_status;
	} param;
} mlan_ds_wmm_cfg, *pmlan_ds_wmm_cfg;

/*-----------------------------------------------------------------*/
/** WPS Configuration Group */
/*-----------------------------------------------------------------*/
/** Enumeration for WPS session */
enum _mlan_wps_status {
	MLAN_WPS_CFG_SESSION_START = 1,
	MLAN_WPS_CFG_SESSION_END = 0
};

/** Type definition of mlan_ds_wps_cfg for MLAN_IOCTL_WPS_CFG */
typedef struct _mlan_ds_wps_cfg {
    /** Sub-command */
	t_u32 sub_command;
    /** WPS configuration parameter */
	union {
	/** WPS session for MLAN_OID_WPS_CFG_SESSION */
		t_u32 wps_session;
	} param;
} mlan_ds_wps_cfg, *pmlan_ds_wps_cfg;

/*-----------------------------------------------------------------*/
/** 802.11n Configuration Group */
/*-----------------------------------------------------------------*/
/** Maximum MCS */
#define NUM_MCS_FIELD      16

/* Both 2.4G and 5G band selected */
#define BAND_SELECT_BOTH    0
/* Band 2.4G selected */
#define BAND_SELECT_BG      1
/* Band 5G selected */
#define BAND_SELECT_A       2

/** Type definition of mlan_ds_11n_htcap_cfg for MLAN_OID_11N_HTCAP_CFG */
typedef struct _mlan_ds_11n_htcap_cfg {
    /** HT Capability information */
	t_u32 htcap;
    /** Band selection */
	t_u32 misc_cfg;
    /** Hardware HT cap information required */
	t_u32 hw_cap_req;
} mlan_ds_11n_htcap_cfg, *pmlan_ds_11n_htcap_cfg;

/** Type definition of mlan_ds_11n_addba_param
 * for MLAN_OID_11N_CFG_ADDBA_PARAM
 */
typedef struct _mlan_ds_11n_addba_param {
    /** Timeout */
	t_u32 timeout;
    /** Buffer size for ADDBA request */
	t_u32 txwinsize;
    /** Buffer size for ADDBA response */
	t_u32 rxwinsize;
    /** amsdu for ADDBA request */
	t_u8 txamsdu;
    /** amsdu for ADDBA response */
	t_u8 rxamsdu;
} mlan_ds_11n_addba_param, *pmlan_ds_11n_addba_param;

/** Type definition of mlan_ds_11n_tx_cfg for MLAN_OID_11N_CFG_TX */
typedef struct _mlan_ds_11n_tx_cfg {
    /** HTTxCap */
	t_u16 httxcap;
    /** HTTxInfo */
	t_u16 httxinfo;
    /** Band selection */
	t_u32 misc_cfg;
} mlan_ds_11n_tx_cfg, *pmlan_ds_11n_tx_cfg;

/** BF Global Configuration */
#define BF_GLOBAL_CONFIGURATION     0x00
/** Performs NDP sounding for PEER specified */
#define TRIGGER_SOUNDING_FOR_PEER   0x01
/** TX BF interval for channel sounding */
#define SET_GET_BF_PERIODICITY      0x02
/** Tell FW not to perform any sounding for peer */
#define TX_BF_FOR_PEER_ENBL         0x03
/** TX BF SNR threshold for peer */
#define SET_SNR_THR_PEER            0x04
/** TX Sounding*/
#define TX_SOUNDING_CFG             0x05

/* Maximum number of peer MAC and status/SNR tuples */
#define MAX_PEER_MAC_TUPLES         10

/** Any new subcommand structure should be declare here */

/** bf global cfg args */
typedef struct _mlan_bf_global_cfg_args {
    /** Global enable/disable bf */
	t_u8 bf_enbl;
    /** Global enable/disable sounding */
	t_u8 sounding_enbl;
    /** FB Type */
	t_u8 fb_type;
    /** SNR Threshold */
	t_u8 snr_threshold;
    /** Sounding interval in milliseconds */
	t_u16 sounding_interval;
    /** BF mode */
	t_u8 bf_mode;
    /** Reserved */
	t_u8 reserved;
} mlan_bf_global_cfg_args;

/** trigger sounding args */
typedef struct _mlan_trigger_sound_args {
    /** Peer MAC address */
	t_u8 peer_mac[MLAN_MAC_ADDR_LENGTH];
    /** Status */
	t_u8 status;
} mlan_trigger_sound_args;

/** bf periodicity args */
typedef struct _mlan_bf_periodicity_args {
    /** Peer MAC address */
	t_u8 peer_mac[MLAN_MAC_ADDR_LENGTH];
    /** Current Tx BF Interval in milliseconds */
	t_u16 interval;
    /** Status */
	t_u8 status;
} mlan_bf_periodicity_args;

/** tx bf peer args */
typedef struct _mlan_tx_bf_peer_args {
    /** Peer MAC address */
	t_u8 peer_mac[MLAN_MAC_ADDR_LENGTH];
    /** Reserved */
	t_u16 reserved;
    /** Enable/Disable Beamforming */
	t_u8 bf_enbl;
    /** Enable/Disable sounding */
	t_u8 sounding_enbl;
    /** FB Type */
	t_u8 fb_type;
} mlan_tx_bf_peer_args;

/** SNR threshold args */
typedef struct _mlan_snr_thr_args {
    /** Peer MAC address */
	t_u8 peer_mac[MLAN_MAC_ADDR_LENGTH];
    /** SNR for peer */
	t_u8 snr;
} mlan_snr_thr_args;

/** Type definition of mlan_ds_11n_tx_bf_cfg for MLAN_OID_11N_CFG_TX_BF_CFG */
typedef struct _mlan_ds_11n_tx_bf_cfg {
    /** BF Action */
	t_u16 bf_action;
    /** Action */
	t_u16 action;
    /** Number of peers */
	t_u32 no_of_peers;
	union {
		mlan_bf_global_cfg_args bf_global_cfg;
		mlan_trigger_sound_args bf_sound[MAX_PEER_MAC_TUPLES];
		mlan_bf_periodicity_args bf_periodicity[MAX_PEER_MAC_TUPLES];
		mlan_tx_bf_peer_args tx_bf_peer[MAX_PEER_MAC_TUPLES];
		mlan_snr_thr_args bf_snr[MAX_PEER_MAC_TUPLES];
	} body;
} mlan_ds_11n_tx_bf_cfg, *pmlan_ds_11n_tx_bf_cfg;

/** Type definition of mlan_ds_11n_amsdu_aggr_ctrl for
 * MLAN_OID_11N_AMSDU_AGGR_CTRL*/
typedef struct _mlan_ds_11n_amsdu_aggr_ctrl {
    /** Enable/Disable */
	t_u16 enable;
    /** Current AMSDU size valid */
	t_u16 curr_buf_size;
} mlan_ds_11n_amsdu_aggr_ctrl, *pmlan_ds_11n_amsdu_aggr_ctrl;

/** Type definition of mlan_ds_11n_aggr_prio_tbl
 *  for MLAN_OID_11N_CFG_AGGR_PRIO_TBL
 */
typedef struct _mlan_ds_11n_aggr_prio_tbl {
    /** ampdu priority table */
	t_u8 ampdu[MAX_NUM_TID];
    /** amsdu priority table */
	t_u8 amsdu[MAX_NUM_TID];
} mlan_ds_11n_aggr_prio_tbl, *pmlan_ds_11n_aggr_prio_tbl;

/** DelBA All TIDs */
#define DELBA_ALL_TIDS  0xff
/** DelBA Tx */
#define DELBA_TX        MBIT(0)
/** DelBA Rx */
#define DELBA_RX        MBIT(1)

/** Type definition of mlan_ds_11n_delba for MLAN_OID_11N_CFG_DELBA */
typedef struct _mlan_ds_11n_delba {
    /** TID */
	t_u8 tid;
    /** Peer MAC address */
	t_u8 peer_mac_addr[MLAN_MAC_ADDR_LENGTH];
    /** Direction (Tx: bit 0, Rx: bit 1) */
	t_u8 direction;
} mlan_ds_11n_delba, *pmlan_ds_11n_delba;

/** Type definition of mlan_ds_delba for MLAN_OID_11N_CFG_REJECT_ADDBA_REQ */
typedef struct _mlan_ds_reject_addba_req {
    /** Bit0    : host sleep activated
     *  Bit1    : auto reconnect enabled
     *  Others  : reserved
     */
	t_u32 conditions;
} mlan_ds_reject_addba_req, *pmlan_ds_reject_addba_req;

/** Type definition of mlan_ds_ibss_ampdu_param */
typedef struct _mlan_ds_ibss_ampdu_param {
    /** ampdu priority table */
	t_u8 ampdu[MAX_NUM_TID];
    /** rx amdpdu setting */
	t_u8 addba_reject[MAX_NUM_TID];
} mlan_ds_ibss_ampdu_param, *pmlan_ds_ibss_ampdu_param;

/** Type definition of mlan_ds_11n_cfg for MLAN_IOCTL_11N_CFG */
typedef struct _mlan_ds_11n_cfg {
    /** Sub-command */
	t_u32 sub_command;
    /** 802.11n configuration parameter */
	union {
	/** Tx param for 11n for MLAN_OID_11N_CFG_TX */
		mlan_ds_11n_tx_cfg tx_cfg;
	/** Aggr priority table for MLAN_OID_11N_CFG_AGGR_PRIO_TBL */
		mlan_ds_11n_aggr_prio_tbl aggr_prio_tbl;
	/** Add BA param for MLAN_OID_11N_CFG_ADDBA_PARAM */
		mlan_ds_11n_addba_param addba_param;
	/** Add BA Reject paramters for MLAN_OID_11N_CFG_ADDBA_REJECT */
		t_u8 addba_reject[MAX_NUM_TID];
	/** Tx buf size for MLAN_OID_11N_CFG_MAX_TX_BUF_SIZE */
		t_u32 tx_buf_size;
	/** HT cap info configuration for MLAN_OID_11N_HTCAP_CFG */
		mlan_ds_11n_htcap_cfg htcap_cfg;
	/** Tx param for 11n for MLAN_OID_11N_AMSDU_AGGR_CTRL */
		mlan_ds_11n_amsdu_aggr_ctrl amsdu_aggr_ctrl;
	/** Supported MCS Set field */
		t_u8 supported_mcs_set[NUM_MCS_FIELD];
	/** Transmit Beamforming Capabilities field */
		t_u32 tx_bf_cap;
	/** Transmit Beamforming configuration */
		mlan_ds_11n_tx_bf_cfg tx_bf;
	/** DelBA for MLAN_OID_11N_CFG_DELBA */
		mlan_ds_11n_delba del_ba;
	/** Reject Addba Req for MLAN_OID_11N_CFG_REJECT_ADDBA_REQ */
		mlan_ds_reject_addba_req reject_addba_req;
	/** Control coex RX window size configuration */
		t_u32 coex_rx_winsize;
	/** Control TX AMPDU configuration */
		t_u32 txaggrctrl;
	/** aggrprirotity table for MLAN_OID_11N_CFG_IBSS_AMPDU_PARAM */
		mlan_ds_ibss_ampdu_param ibss_ampdu;
	} param;
} mlan_ds_11n_cfg, *pmlan_ds_11n_cfg;

/** Country code length */
#define COUNTRY_CODE_LEN                        3

/*-----------------------------------------------------------------*/
/** 802.11d Configuration Group */
/*-----------------------------------------------------------------*/
/** Maximum subbands for 11d */
#define MRVDRV_MAX_SUBBAND_802_11D              83

#ifdef STA_SUPPORT
/** Data structure for subband set */
typedef struct _mlan_ds_subband_set_t {
    /** First channel */
	t_u8 first_chan;
    /** Number of channels */
	t_u8 no_of_chan;
    /** Maximum Tx power in dBm */
	t_u8 max_tx_pwr;
} mlan_ds_subband_set_t;

/** Domain regulatory information */
typedef struct _mlan_ds_11d_domain_info {
    /** Country Code */
	t_u8 country_code[COUNTRY_CODE_LEN];
    /** Band that channels in sub_band belong to */
	t_u8 band;
    /** No. of subband in below */
	t_u8 no_of_sub_band;
    /** Subband data to send/last sent */
	mlan_ds_subband_set_t sub_band[MRVDRV_MAX_SUBBAND_802_11D];
} mlan_ds_11d_domain_info;
#endif

/** Type definition of mlan_ds_11d_cfg for MLAN_IOCTL_11D_CFG */
typedef struct _mlan_ds_11d_cfg {
    /** Sub-command */
	t_u32 sub_command;
    /** 802.11d configuration parameter */
	union {
#ifdef STA_SUPPORT
	/** Enable for MLAN_OID_11D_CFG_ENABLE */
		t_u32 enable_11d;
	/** Domain info for MLAN_OID_11D_DOMAIN_INFO */
		mlan_ds_11d_domain_info domain_info;
#endif				/* STA_SUPPORT */
#ifdef UAP_SUPPORT
	/** tlv data for MLAN_OID_11D_DOMAIN_INFO */
		t_u8 domain_tlv[MAX_IE_SIZE];
#endif				/* UAP_SUPPORT */
	} param;
} mlan_ds_11d_cfg, *pmlan_ds_11d_cfg;

typedef struct _mlan_ds_11k_cfg {
    /** Sub-command */
	t_u32 sub_command;
	union {
		t_u32 enable_11k;
	} param;
} mlan_ds_11k_cfg;

/*-----------------------------------------------------------------*/
/** Register Memory Access Group */
/*-----------------------------------------------------------------*/
/** Enumeration for CSU target device type */
enum _mlan_csu_target_type {
	MLAN_CSU_TARGET_CAU = 1,
	MLAN_CSU_TARGET_PSU,
};

/** Enumeration for register type */
enum _mlan_reg_type {
	MLAN_REG_MAC = 1,
	MLAN_REG_BBP,
	MLAN_REG_RF,
	MLAN_REG_CAU = 5,
	MLAN_REG_PSU = 6,
};

/** Type definition of mlan_ds_reg_rw for MLAN_OID_REG_RW */
typedef struct _mlan_ds_reg_rw {
    /** Register type */
	t_u32 type;
    /** Offset */
	t_u32 offset;
    /** Value */
	t_u32 value;
} mlan_ds_reg_rw;

/** Maximum EEPROM data */
#define MAX_EEPROM_DATA 256

/** Type definition of mlan_ds_read_eeprom for MLAN_OID_EEPROM_RD */
typedef struct _mlan_ds_read_eeprom {
    /** Multiples of 4 */
	t_u16 offset;
    /** Number of bytes */
	t_u16 byte_count;
    /** Value */
	t_u8 value[MAX_EEPROM_DATA];
} mlan_ds_read_eeprom;

/** Type definition of mlan_ds_mem_rw for MLAN_OID_MEM_RW */
typedef struct _mlan_ds_mem_rw {
    /** Address */
	t_u32 addr;
    /** Value */
	t_u32 value;
} mlan_ds_mem_rw;

/** Type definition of mlan_ds_reg_mem for MLAN_IOCTL_REG_MEM */
typedef struct _mlan_ds_reg_mem {
    /** Sub-command */
	t_u32 sub_command;
    /** Register memory access parameter */
	union {
	/** Register access for MLAN_OID_REG_RW */
		mlan_ds_reg_rw reg_rw;
	/** EEPROM access for MLAN_OID_EEPROM_RD */
		mlan_ds_read_eeprom rd_eeprom;
	/** Memory access for MLAN_OID_MEM_RW */
		mlan_ds_mem_rw mem_rw;
	} param;
} mlan_ds_reg_mem, *pmlan_ds_reg_mem;

/*-----------------------------------------------------------------*/
/** Multi-Radio Configuration Group */
/*-----------------------------------------------------------------*/

/*-----------------------------------------------------------------*/
/** 802.11h Configuration Group */
/*-----------------------------------------------------------------*/
#if defined(DFS_TESTING_SUPPORT)
/** Type definition of mlan_ds_11h_dfs_testing for MLAN_OID_11H_DFS_TESTING */
typedef struct _mlan_ds_11h_dfs_testing {
    /** User-configured CAC period in milliseconds, 0 to use default */
	t_u32 usr_cac_period_msec;
    /** User-configured NOP period in seconds, 0 to use default */
	t_u16 usr_nop_period_sec;
    /** User-configured skip channel change, 0 to disable */
	t_u8 usr_no_chan_change;
    /** User-configured fixed channel to change to, 0 to use random channel */
	t_u8 usr_fixed_new_chan;
} mlan_ds_11h_dfs_testing, *pmlan_ds_11h_dfs_testing;

/** Type definition of mlan_ds_11h_dfs_testing for MLAN_OID_11H_CHAN_NOP_INFO */
typedef struct _mlan_ds_11h_chan_nop_info {
    /** current channel */
	t_u8 curr_chan;
    /** channel_width */
	t_u8 chan_width;
    /** flag for chan under nop */
	t_bool chan_under_nop;
    /** chan_ban_info for new channel */
	chan_band_info new_chan;
} mlan_ds_11h_chan_nop_info;
#endif

typedef struct _mlan_ds_11h_chan_rep_req {
	t_u16 startFreq;
	Band_Config_t bandcfg;
	t_u8 chanNum;
	t_u32 millisec_dwell_time;
				/**< Channel dwell time in milliseconds */
	t_u8 host_based;
} mlan_ds_11h_chan_rep_req;

/** Type definition of mlan_ds_11h_cfg for MLAN_IOCTL_11H_CFG */
typedef struct _mlan_ds_11h_cfg {
    /** Sub-command */
	t_u32 sub_command;
	union {
	/** Local power constraint for MLAN_OID_11H_LOCAL_POWER_CONSTRAINT */
		t_s8 usr_local_power_constraint;
#if defined(DFS_TESTING_SUPPORT)
	/** User-configuation for MLAN_OID_11H_DFS_TESTING */
		mlan_ds_11h_dfs_testing dfs_testing;
	/** channel NOP information for MLAN_OID_11H_CHAN_NOP_INFO */
		mlan_ds_11h_chan_nop_info ch_nop_info;
#endif
		mlan_ds_11h_chan_rep_req chan_rpt_req;
		t_s8 cs_count;
	} param;
} mlan_ds_11h_cfg, *pmlan_ds_11h_cfg;

/*-----------------------------------------------------------------*/
/** Miscellaneous Configuration Group */
/*-----------------------------------------------------------------*/

/** CMD buffer size */
#define MLAN_SIZE_OF_CMD_BUFFER 2048

/** LDO Internal */
#define LDO_INTERNAL            0
/** LDO External */
#define LDO_EXTERNAL            1

/** Enumeration for IE type */
enum _mlan_ie_type {
	MLAN_IE_TYPE_GEN_IE = 0,
#ifdef STA_SUPPORT
	MLAN_IE_TYPE_ARP_FILTER,
#endif /* STA_SUPPORT */
};

/** Type definition of mlan_ds_misc_gen_ie for MLAN_OID_MISC_GEN_IE */
typedef struct _mlan_ds_misc_gen_ie {
    /** IE type */
	t_u32 type;
    /** IE length */
	t_u32 len;
    /** IE buffer */
	t_u8 ie_data[MAX_IE_SIZE];
} mlan_ds_misc_gen_ie;

#if defined(SDIO_MULTI_PORT_TX_AGGR) || defined(SDIO_MULTI_PORT_RX_AGGR)
/** Type definition of mlan_ds_misc_sdio_mpa_ctrl
 *  for MLAN_OID_MISC_SDIO_MPA_CTRL
 */
typedef struct _mlan_ds_misc_sdio_mpa_ctrl {
    /** SDIO MP-A TX enable/disable */
	t_u16 tx_enable;
    /** SDIO MP-A RX enable/disable */
	t_u16 rx_enable;
    /** SDIO MP-A TX buf size */
	t_u16 tx_buf_size;
    /** SDIO MP-A RX buf size */
	t_u16 rx_buf_size;
    /** SDIO MP-A TX Max Ports */
	t_u16 tx_max_ports;
    /** SDIO MP-A RX Max Ports */
	t_u16 rx_max_ports;
} mlan_ds_misc_sdio_mpa_ctrl;
#endif

/** Type definition of mlan_ds_misc_cmd for MLAN_OID_MISC_HOST_CMD */
typedef struct _mlan_ds_misc_cmd {
    /** Command length */
	t_u32 len;
    /** Command buffer */
	t_u8 cmd[MLAN_SIZE_OF_CMD_BUFFER];
} mlan_ds_misc_cmd;

/** Maximum number of system clocks */
#define MLAN_MAX_CLK_NUM        16

/** Clock type : Configurable */
#define MLAN_CLK_CONFIGURABLE   0
/** Clock type : Supported */
#define MLAN_CLK_SUPPORTED      1

/** Type definition of mlan_ds_misc_sys_clock for MLAN_OID_MISC_SYS_CLOCK */
typedef struct _mlan_ds_misc_sys_clock {
    /** Current system clock */
	t_u16 cur_sys_clk;
    /** Clock type */
	t_u16 sys_clk_type;
    /** Number of clocks */
	t_u16 sys_clk_num;
    /** System clocks */
	t_u16 sys_clk[MLAN_MAX_CLK_NUM];
} mlan_ds_misc_sys_clock;

/** Maximum response buffer length */
#define ASSOC_RSP_BUF_SIZE 500

/** Type definition of mlan_ds_misc_assoc_rsp for MLAN_OID_MISC_ASSOC_RSP */
typedef struct _mlan_ds_misc_assoc_rsp {
    /** Associate response buffer */
	t_u8 assoc_resp_buf[ASSOC_RSP_BUF_SIZE];
    /** Response buffer length */
	t_u32 assoc_resp_len;
} mlan_ds_misc_assoc_rsp;

/** Enumeration for function init/shutdown */
enum _mlan_func_cmd {
	MLAN_FUNC_INIT = 1,
	MLAN_FUNC_SHUTDOWN,
};

/** Enumeration for Coalescing status */
enum _mlan_coal_status {
	MLAN_MISC_COALESCING_ENABLE = 1,
	MLAN_MISC_COALESCING_DISABLE = 0
};

/** Net monitor filter: management frame */
#define MLAN_NETMON_MANAGEMENT_FRAME    MBIT(0)
/** Net monitor filter: control frame */
#define MLAN_NETMON_CONTROL_FRAME       MBIT(1)
/** Net monitor filter: data frame */
#define MLAN_NETMON_DATA_FRAME          MBIT(2)

typedef struct _mlan_ds_misc_net_monitor {
    /** Enable/disable network monitor */
	t_u32 enable_net_mon;
    /** Set net monitor filer flag */
	t_u32 filter_flag;
   /** Radio type */
	t_u32 band;
    /** Channel */
	t_u32 channel;
    /** Secondary channel bandwidth */
	t_u32 chan_bandwidth;
} mlan_ds_misc_net_monitor;

/** Type definition of mlan_ds_misc_tx_datapause
 * for MLAN_OID_MISC_TX_DATAPAUSE
 */
typedef struct _mlan_ds_misc_tx_datapause {
    /** Tx data pause flag */
	t_u16 tx_pause;
    /** Max number of Tx buffers for all PS clients */
	t_u16 tx_buf_cnt;
} mlan_ds_misc_tx_datapause;

/** IP address length */
#define IPADDR_LEN                  (16)
/** Max number of ip */
#define MAX_IPADDR                  (4)
/** IP address type - NONE*/
#define IPADDR_TYPE_NONE            (0)
/** IP address type - IPv4*/
#define IPADDR_TYPE_IPV4            (1)
/** IP operation remove */
#define MLAN_IPADDR_OP_IP_REMOVE    (0)
/** IP operation ARP filter */
#define MLAN_IPADDR_OP_ARP_FILTER   MBIT(0)
/** IP operation ARP response */
#define MLAN_IPADDR_OP_AUTO_ARP_RESP    MBIT(1)

/** Type definition of mlan_ds_misc_ipaddr_cfg for MLAN_OID_MISC_IP_ADDR */
typedef struct _mlan_ds_misc_ipaddr_cfg {
    /** Operation code */
	t_u32 op_code;
    /** IP address type */
	t_u32 ip_addr_type;
    /** Number of IP */
	t_u32 ip_addr_num;
    /** IP address */
	t_u8 ip_addr[MAX_IPADDR][IPADDR_LEN];
} mlan_ds_misc_ipaddr_cfg;

/* MEF configuration disable */
#define MEF_CFG_DISABLE             0
/* MEF configuration Rx filter enable */
#define MEF_CFG_RX_FILTER_ENABLE    1
/* MEF configuration auto ARP response */
#define MEF_CFG_AUTO_ARP_RESP       2
/* MEF configuration host command */
#define MEF_CFG_HOSTCMD             0xFFFF

/** Type definition of mlan_ds_misc_mef_cfg for MLAN_OID_MISC_MEF_CFG */
typedef struct _mlan_ds_misc_mef_cfg {
    /** Sub-ID for operation */
	t_u32 sub_id;
    /** Parameter according to sub-ID */
	union {
	/** MEF command buffer for MEF_CFG_HOSTCMD */
		mlan_ds_misc_cmd cmd_buf;
	} param;
} mlan_ds_misc_mef_cfg;

/** Type definition of mlan_ds_misc_cfp_code for MLAN_OID_MISC_CFP_CODE */
typedef struct _mlan_ds_misc_cfp_code {
    /** CFP table code for 2.4GHz */
	t_u32 cfp_code_bg;
    /** CFP table code for 5GHz */
	t_u32 cfp_code_a;
} mlan_ds_misc_cfp_code;

/** Type definition of mlan_ds_misc_country_code
 *  for MLAN_OID_MISC_COUNTRY_CODE
 */
typedef struct _mlan_ds_misc_country_code {
    /** Country Code */
	t_u8 country_code[COUNTRY_CODE_LEN];
} mlan_ds_misc_country_code;

/** Type definition of mlan_ds_host_clock  */
typedef struct _mlan_ds_host_clock {
    /** host time in secs */
	t_u64 time;
	/** fw time */
	t_u64 fw_time;
    /** host-bbu clock delta */
	t_u64 host_bbu_clk_delta;
} mlan_ds_host_clock;

/** action for set */
#define SUBSCRIBE_EVT_ACT_BITWISE_SET         0x0002
/** action for clear */
#define SUBSCRIBE_EVT_ACT_BITWISE_CLR         0x0003
/** BITMAP for subscribe event rssi low */
#define SUBSCRIBE_EVT_RSSI_LOW          MBIT(0)
/** BITMAP for subscribe event snr low */
#define SUBSCRIBE_EVT_SNR_LOW           MBIT(1)
/** BITMAP for subscribe event max fail */
#define SUBSCRIBE_EVT_MAX_FAIL          MBIT(2)
/** BITMAP for subscribe event beacon missed */
#define SUBSCRIBE_EVT_BEACON_MISSED     MBIT(3)
/** BITMAP for subscribe event rssi high */
#define SUBSCRIBE_EVT_RSSI_HIGH         MBIT(4)
/** BITMAP for subscribe event snr high */
#define SUBSCRIBE_EVT_SNR_HIGH          MBIT(5)
/** BITMAP for subscribe event data rssi low */
#define SUBSCRIBE_EVT_DATA_RSSI_LOW     MBIT(6)
/** BITMAP for subscribe event data snr low */
#define SUBSCRIBE_EVT_DATA_SNR_LOW      MBIT(7)
/** BITMAP for subscribe event data rssi high */
#define SUBSCRIBE_EVT_DATA_RSSI_HIGH    MBIT(8)
/** BITMAP for subscribe event data snr high */
#define SUBSCRIBE_EVT_DATA_SNR_HIGH     MBIT(9)
/** BITMAP for subscribe event link quality */
#define SUBSCRIBE_EVT_LINK_QUALITY      MBIT(10)
/** BITMAP for subscribe event pre_beacon_lost */
#define SUBSCRIBE_EVT_PRE_BEACON_LOST   MBIT(11)
/** default PRE_BEACON_MISS_COUNT */
#define DEFAULT_PRE_BEACON_MISS         30

/** Type definition of mlan_ds_subscribe_evt for MLAN_OID_MISC_CFP_CODE */
typedef struct _mlan_ds_subscribe_evt {
    /** evt action */
	t_u16 evt_action;
    /** bitmap for subscribe event */
	t_u16 evt_bitmap;
    /** Absolute value of RSSI threshold value (dBm) */
	t_u8 low_rssi;
    /** 0--report once, 1--report everytime happen,
      * N -- report only happend > N consecutive times
     */
	t_u8 low_rssi_freq;
    /** SNR threshold value (dB) */
	t_u8 low_snr;
    /** 0--report once, 1--report everytime happen,
     *  N -- report only happend > N consecutive times
     */
	t_u8 low_snr_freq;
    /** Failure count threshold */
	t_u8 failure_count;
    /** 0--report once, 1--report everytime happen,
     *  N -- report only happend > N consecutive times
     */
	t_u8 failure_count_freq;
    /** num of missed beacons */
	t_u8 beacon_miss;
    /** 0--report once, 1--report everytime happen,
     *  N -- report only happend > N consecutive times
     */
	t_u8 beacon_miss_freq;
    /** Absolute value of RSSI threshold value (dBm) */
	t_u8 high_rssi;
    /** 0--report once, 1--report everytime happen,
     *  N -- report only happend > N consecutive times
     */
	t_u8 high_rssi_freq;
    /** SNR threshold value (dB) */
	t_u8 high_snr;
    /** 0--report once, 1--report everytime happen,
     *  N -- report only happend > N consecutive times
     */
	t_u8 high_snr_freq;
    /** Absolute value of data RSSI threshold value (dBm) */
	t_u8 data_low_rssi;
    /** 0--report once, 1--report everytime happen,
     *  N -- report only happend > N consecutive times
     */
	t_u8 data_low_rssi_freq;
    /** Absolute value of data SNR threshold value (dBm) */
	t_u8 data_low_snr;
    /** 0--report once, 1--report everytime happen,
     *  N -- report only happend > N consecutive times
     */
	t_u8 data_low_snr_freq;
    /** Absolute value of data RSSI threshold value (dBm) */
	t_u8 data_high_rssi;
    /** 0--report once, 1--report everytime happen,
     *  N -- report only happend > N consecutive times
     */
	t_u8 data_high_rssi_freq;
    /** Absolute value of data SNR threshold value (dBm) */
	t_u8 data_high_snr;
    /** 0--report once, 1--report everytime happen,
     *  N -- report only happend > N consecutive times
     */
	t_u8 data_high_snr_freq;
	/* Link SNR threshold (dB) */
	t_u16 link_snr;
	/* Link SNR frequency */
	t_u16 link_snr_freq;
	/* Second minimum rate value as per the rate table below */
	t_u16 link_rate;
	/* Second minimum rate frequency */
	t_u16 link_rate_freq;
	/* Tx latency value (us) */
	t_u16 link_tx_latency;
	/* Tx latency frequency */
	t_u16 link_tx_lantency_freq;
	/* Number of pre missed beacons */
	t_u8 pre_beacon_miss;
} mlan_ds_subscribe_evt;

/** Max OTP user data length */
#define MAX_OTP_USER_DATA_LEN	252

/** Type definition of mlan_ds_misc_otp_user_data
 * for MLAN_OID_MISC_OTP_USER_DATA
 */
typedef struct _mlan_ds_misc_otp_user_data {
    /** Reserved */
	t_u16 reserved;
    /** OTP user data length */
	t_u16 user_data_length;
    /** User data buffer */
	t_u8 user_data[MAX_OTP_USER_DATA_LEN];
} mlan_ds_misc_otp_user_data;

#ifdef WIFI_DIRECT_SUPPORT
/** flag for NOA */
#define WIFI_DIRECT_NOA         1
/** flag for OPP_PS */
#define WIFI_DIRECT_OPP_PS      2
/** Type definition of mlan_ds_wifi_direct_config
 *  for MLAN_OID_MISC_WIFI_DIRECT_CONFIG
 */
typedef struct _mlan_ds_wifi_direct_config {
    /** flags for NOA/OPP_PS */
	t_u8 flags;
     /** NoA enable/disable */
	t_u8 noa_enable;
    /** index */
	t_u16 index;
    /** NoA count */
	t_u8 noa_count;
    /** NoA duration */
	t_u32 noa_duration;
    /** NoA interval */
	t_u32 noa_interval;
    /** opp ps enable/disable */
	t_u8 opp_ps_enable;
    /** CT window value */
	t_u8 ct_window;
} mlan_ds_wifi_direct_config;
#endif

#if defined(STA_SUPPORT)
/** mlan_ds_misc_pmfcfg structure */
typedef struct _mlan_ds_misc_pmfcfg {
    /** Management Frame Protection Capable */
	t_u8 mfpc;
    /** Management Frame Protection Required */
	t_u8 mfpr;
} mlan_ds_misc_pmfcfg;
#endif

/** mlan_ds_multi_chan_cfg structure */
typedef MLAN_PACK_START struct _mlan_ds_multi_chan_cfg {
    /** Channel Time */
	t_u32 channel_time;
    /** Buffer Weight */
	t_u8 buffer_weight;
    /** tlv len */
	t_u16 tlv_len;
    /** TLV buffer */
	t_u8 tlv_buf[0];
} MLAN_PACK_END mlan_ds_multi_chan_cfg;

/** mlan_ds_drcs_cfg structure */
typedef MLAN_PACK_START struct _mlan_ds_drcs_cfg {
    /** Channel Index*/
	t_u16 chan_idx;
    /** Channel time (in TU) for chan_idx */
	t_u8 chantime;
    /** Channel swith time (in TU) for chan_idx */
	t_u8 switchtime;
    /** Undoze time (in TU) for chan_idx */
	t_u8 undozetime;
    /** Rx traffic control scheme when channel switch*/
    /** only valid for GC/STA interface*/
	t_u8 mode;
} MLAN_PACK_END mlan_ds_drcs_cfg;

#define MAX_SSID_NUM 16
#define MAX_AP_LIST 8
#define RETRY_UNLIMITED_TIME 0xFF

#define FW_ROAM_ENABLE                        MBIT(0)
#define FW_ROAM_TRIGGER_COND                  MBIT(1)
#define FW_ROAM_BSSID                         MBIT(2)
#define FW_ROAM_SSID                          MBIT(3)
#define FW_ROAM_RETRY_COUNT                   MBIT(4)
#define FW_ROAM_RSSI_PARA                     MBIT(5)
#define FW_ROAM_BAND_RSSI                     MBIT(6)
#define FW_ROAM_BGSCAN_PARAM                  MBIT(7)
#define FW_ROAM_EES_PARAM                     MBIT(8)
#define FW_ROAM_BCN_MISS_THRESHOLD            MBIT(9)
#define FW_ROAM_PRE_BCN_MISS_THRESHOLD        MBIT(10)
#define FW_ROAM_BLACKLIST                     MBIT(11)
#define FW_ROAM_REPEAT_CNT                    MBIT(12)

/*Roam offload configuration for auto reconnection when suspend and resume*/
typedef enum _roam_offload_config_mode {
	ROAM_OFFLOAD_ENABLE = 1,
	ROAM_OFFLOAD_SUSPEND_CFG,
	ROAM_OFFLOAD_RESUME_CFG,
	ROAM_OFFLOAD_PARAM_CFG,
} roam_offload_config_mode;

typedef enum _roam_offload_set_mode {
	ROAM_OFFLOAD_DISABLE = 0,
	ROAM_OFFLOAD_WITH_APLIST,
	ROAM_OFFLOAD_WITHOUT_APLIST,
	ROAM_OFFLOAD_WITH_BSSID,
	ROAM_OFFLOAD_WITH_SSID,
	AUTO_RECONNECT,
} roam_offload_set_mode;

typedef enum _roam_offload_trigger_mode {
	NO_TRIGGER = 0x00,
	RSSI_LOW_TRIGGER = 0x01,
	PRE_BEACON_LOST_TRIGGER = 0x02,
	LINK_LOST_TRIGGER = 0x04,
	DEAUTH_WITH_EXT_AP_TRIGGER = 0x08,
} roam_offload_trigger_mode;

/** mlan_ds_misc_ees_cfg structure */
typedef MLAN_PACK_START struct _mlan_ds_misc_ees_cfg {
	/* EES mode */
	t_u16 ees_mode;
	/* EES report condition */
	t_u16 ees_rpt_condition;
	/* High scan period(milliseconds) */
	t_u16 high_scan_period;
	/* High scan count */
	t_u16 high_scan_count;
	/* Middle scan period(milliseconds) */
	t_u16 mid_scan_period;
	/* Middle scan count */
	t_u16 mid_scan_count;
	/* Low scan period(milliseconds) */
	t_u16 low_scan_period;
	/* Low scan count */
	t_u16 low_scan_count;
} MLAN_PACK_END mlan_ds_misc_ees_cfg;

/** mlan_ds_misc_bgscan_cfg structure */
typedef MLAN_PACK_START struct _mlan_ds_misc_bgscan_cfg {
	/* BSS Type  0x1-bss independent, 0x2-bss infrastructure, 0x3-bss any */
	t_u8 bss_type;
	/* Number of channels scanned for each scan */
	t_u8 channels_per_scan;
	/* Interval between consective scans */
	t_u32 scan_interval;
	/* Conditons to trigger report to host */
	t_u32 bg_rpt_condition;
} MLAN_PACK_END mlan_ds_misc_bgscan_cfg;

/** mlan_ds_misc_band_rssi structure */
typedef MLAN_PACK_START struct _mlan_ds_misc_band_rssi {
	/* RSSI hysteresis */
	t_u8 rssi_hysteresis;
	/*  Preferred channel band for fw roaming
	 *  0:2.4G band; 1: 5G band; 2:4G band; 0xFF:band not set(invalid)
	 */
	t_u8 band_preferred;
} MLAN_PACK_END mlan_ds_misc_band_rssi;

/** mlan_ds_misc_ssid_list structure */
typedef MLAN_PACK_START struct _mlan_ds_misc_ssid_list {
	/* SSID number */
	t_u8 ssid_num;
	/* SSID for fw roaming/auto_reconnect */
	mlan_802_11_ssid ssids[MAX_SSID_NUM];
} MLAN_PACK_END mlan_ds_misc_ssid_list;

/** mlan_ds_misc_roam_offload_aplist structure */
typedef MLAN_PACK_START struct _mlan_ds_misc_roam_offload_aplist {
    /** Number of AP**/
	t_u8 ap_num;
    /** AP mac addrs**/
	t_u8 ap_mac[MAX_AP_LIST][MLAN_MAC_ADDR_LENGTH];
} MLAN_PACK_END mlan_ds_misc_roam_offload_aplist;

/** _mlan_ds_misc_roam_offload_para_rssi structure */
typedef MLAN_PACK_START struct _mlan_ds_misc_roam_offload_para_rssi {
    /** Setting flag**/
	t_u8 set_flag;
    /** Max value of RSSI threshold**/
	t_u8 max_rssi;
    /** Min value of RSSI threshold**/
	t_u8 min_rssi;
    /** Adjusting step value of RSSI threshold**/
	t_u8 step_rssi;
} MLAN_PACK_END mlan_ds_misc_roam_offload_para_rssi;

/** mlan_ds_misc_roam_offload structure */
typedef MLAN_PACK_START struct _mlan_ds_misc_roam_offload {
    /** Enable roam offload**/
	t_u8 enable;
    /** User set passphrase**/
	t_u8 userset_passphrase;
	/* Condition to trigger roaming
	 * Bit0 : RSSI low trigger
	 * Bit1 : Pre-beacon lost trigger
	 * Bit2 : Link Lost trigger
	 * Bit3 : Deauth by ext-AP trigger
	 * Bit4 ~ Bit15 : Reserved
	 * value 0 : no trigger
	 * value 0xff : invalid
	 */
	t_u16 trigger_condition;
    /** AP list**/
	mlan_ds_misc_roam_offload_aplist aplist;
	/*Roam offload configuration mode for auto connection when suspend and resume */
	roam_offload_config_mode config_mode;
    /** Retry count**/
	t_u8 retry_count;
    /** RSSI para**/
	mlan_ds_misc_roam_offload_para_rssi para_rssi;
    /** BSSID of reconnection**/
	mlan_802_11_mac_addr bssid_reconnect;
	/* SSID List(White list) */
	mlan_ds_misc_ssid_list ssid_list;
	/* Black list(BSSID list) */
	mlan_ds_misc_roam_offload_aplist black_list;
	/* BAND and RSSI_HYSTERESIS set flag */
	t_u8 band_rssi_flag;
	mlan_ds_misc_band_rssi band_rssi;

	/* BGSCAN params set flag */
	t_u8 bgscan_set_flag;
	mlan_ds_misc_bgscan_cfg bgscan_cfg;

	/* EES mode params set flag */
	t_u8 ees_param_set_flag;
	mlan_ds_misc_ees_cfg ees_cfg;

	/* Beacon miss threshold */
	t_u8 bcn_miss_threshold;

	/* Beacon miss threshold */
	t_u8 pre_bcn_miss_threshold;

	/* Scan repeat count */
	t_u16 repeat_count;
} MLAN_PACK_END mlan_ds_misc_roam_offload;

/**Action ID for TDLS disable link*/
#define WLAN_TDLS_DISABLE_LINK           0x00
/**Action ID for TDLS enable link*/
#define WLAN_TDLS_ENABLE_LINK            0x01
/**Action ID for TDLS create link*/
#define WLAN_TDLS_CREATE_LINK            0x02
/**Action ID for TDLS config link*/
#define WLAN_TDLS_CONFIG_LINK            0x03
/*reason code*/
#define MLAN_REASON_TDLS_TEARDOWN_UNSPECIFIED 26
/** TDLS operation buffer */
typedef struct _mlan_ds_misc_tdls_oper {
    /** TDLS Action */
	t_u16 tdls_action;
    /** TDLS peer address */
	t_u8 peer_mac[MLAN_MAC_ADDR_LENGTH];
    /** peer capability */
	t_u16 capability;
	/** peer qos info */
	t_u8 qos_info;
	/** peer extend capability */
	t_u8 *ext_capab;
	/** extend capability len */
	t_u8 ext_capab_len;
	/** support rates */
	t_u8 *supported_rates;
	/** supported rates len */
	t_u8 supported_rates_len;
	/** peer ht_cap */
	t_u8 *ht_capa;
} mlan_ds_misc_tdls_oper;

/** flag for TDLS extcap */
#define TDLS_IE_FLAGS_EXTCAP     0x0001
/** flag for TDLS HTCAP */
#define TDLS_IE_FLAGS_HTCAP      0x0002
/** flag for TDLS HTINFO */
#define TDLS_IE_FLAGS_HTINFO     0x0004
/** flag for TDLS Supported channels and regulatory class IE*/
#define TDLS_IE_FLAGS_SUPP_CS_IE        0x0040
/** flag for TDLS Qos info */
#define TDLS_IE_FLAGS_QOS_INFO        0x0080
/** flag for TDLS SETUP */
#define TDLS_IE_FLAGS_SETUP        0x0100

/** TDLS ie buffer */
typedef struct _mlan_ds_misc_tdls_ies {
    /** TDLS peer address */
	t_u8 peer_mac[MLAN_MAC_ADDR_LENGTH];
    /** flags for request IEs */
	t_u16 flags;
    /** Qos info */
	t_u8 QosInfo;
    /** Extended Capabilities IE */
	t_u8 ext_cap[IEEE_MAX_IE_SIZE];
    /** HT Capabilities IE */
	t_u8 ht_cap[IEEE_MAX_IE_SIZE];
    /** HT Information IE */
	t_u8 ht_info[IEEE_MAX_IE_SIZE];
    /** supported channels */
	t_u8 supp_chan[IEEE_MAX_IE_SIZE];
    /** supported regulatory class */
	t_u8 regulatory_class[IEEE_MAX_IE_SIZE];
} mlan_ds_misc_tdls_ies;

#ifdef RX_PACKET_COALESCE
typedef struct _mlan_ds_misc_rx_packet_coalesce {
	/** packet threshold */
	t_u32 packet_threshold;
	/** timeout value */
	t_u16 delay;
} mlan_ds_misc_rx_packet_coalesce;
#endif

/** mlan_ds_misc_dfs_repeater structure */
typedef struct _mlan_ds_misc_dfs_repeater {
	/** Set or Get */
	t_u16 action;
	/** 1 on or 0 off */
	t_u16 mode;
} mlan_ds_misc_dfs_repeater;

#define WOWLAN_MAX_PATTERN_LEN		20
#define WOWLAN_MAX_OFFSET_LEN		50
#define MAX_NUM_FILTERS              10

/** Temperature Sensor structure */
typedef struct _mlan_ds_sensor_temp {
    /** Temperature */
	t_u32 temperature;
} mlan_ds_sensor_temp;

#define MLAN_KCK_LEN   16
#define MLAN_KEK_LEN   16
#define MLAN_REPLAY_CTR_LEN 8
/** mlan_ds_misc_gtk_rekey_data */
typedef struct _mlan_ds_misc_gtk_rekey_data {
    /** key encryption key */
	t_u8 kek[MLAN_KEK_LEN];
    /** key confirmation key */
	t_u8 kck[MLAN_KCK_LEN];
    /** replay counter */
	t_u8 replay_ctr[MLAN_REPLAY_CTR_LEN];
} mlan_ds_misc_gtk_rekey_data;
typedef struct _mlan_ds_bw_chan_oper {
	/* bandwidth 20:20M 40:40M 80:80M */
	t_u8 bandwidth;
	/* channel number */
	t_u8 channel;
	/* Non-global operating class */
	t_u8 oper_class;
} mlan_ds_bw_chan_oper;

typedef struct _mlan_ds_ind_rst_cfg {
	/** Set or Get */
	t_u16 action;
	/** oob mode enable/ disable */
	t_u8 ir_mode;
    /** gpio pin */
	t_u8 gpio_pin;
} mlan_ds_ind_rst_cfg;

/** mlan_ds_cw_mode_ctrl structure */
typedef MLAN_PACK_START struct _mlan_ds_cw_mode_ctrl {
    /** Mode of Operation 0: Disable 1: Tx Continuous Packet 2: Tx Continuous Wave */
	t_u8 mode;
	/*channel */
	t_u8 channel;
	/* channel info */
	t_u8 chanInfo;
    /** Tx Power level in dBm */
	t_u16 txPower;
    /** Packet Length */
	t_u16 pktLength;
    /** bit rate Info */
	t_u32 rateInfo;
} MLAN_PACK_END mlan_ds_cw_mode_ctrl;

#define RX_PKT_INFO MBIT(1)
/** Struct for per-packet configuration */
typedef struct _mlan_per_pkt_cfg {
   /** Type ID*/
	t_u16 type;
   /** Length of payload*/
	t_u16 len;
   /**  Tx/Rx per-packet control */
	t_u8 tx_rx_control;
   /** Number of ethernet types in ether_type array */
	t_u8 proto_type_num;
   /** Array of ether_type for per-packet control */
	t_u16 ether_type[0];
} mlan_per_pkt_cfg;

/** Type definition of mlan_ds_misc_robustcoex_params for MLAN_IOCTL_MISC_CFG */
typedef struct _mlan_ds_misc_robustcoex_params {
	t_u16 method;
    /** enable/disable robustcoex gpio cfg */
	t_u8 enable;
    /** Number of GPIO */
	t_u8 gpio_num;
    /** Polarity of GPIO */
	t_u8 gpio_polarity;
} mlan_ds_misc_robustcoex_params;

/** Type definition of mlan_ds_misc_cfg for MLAN_IOCTL_MISC_CFG */
typedef struct _mlan_ds_misc_cfg {
    /** Sub-command */
	t_u32 sub_command;
    /** Miscellaneous configuration parameter */
	union {
	/** Generic IE for MLAN_OID_MISC_GEN_IE */
		mlan_ds_misc_gen_ie gen_ie;
	/** Region code for MLAN_OID_MISC_REGION */
		t_u32 region_code;
#if defined(SDIO_MULTI_PORT_TX_AGGR) || defined(SDIO_MULTI_PORT_RX_AGGR)
	/** SDIO MP-A Ctrl command for MLAN_OID_MISC_SDIO_MPA_CTRL */
		mlan_ds_misc_sdio_mpa_ctrl mpa_ctrl;
#endif
	/** Hostcmd for MLAN_OID_MISC_HOST_CMD */
		mlan_ds_misc_cmd hostcmd;
	/** System clock for MLAN_OID_MISC_SYS_CLOCK */
		mlan_ds_misc_sys_clock sys_clock;
	/** WWS set/get for MLAN_OID_MISC_WWS */
		t_u32 wws_cfg;
	/** Get associate response for MLAN_OID_MISC_ASSOC_RSP */
		mlan_ds_misc_assoc_rsp assoc_resp;
	/** Function init/shutdown for MLAN_OID_MISC_INIT_SHUTDOWN */
		t_u32 func_init_shutdown;
	/** Coalescing status for MLAN_OID_MISC_COALESCING_STATUS */
		t_u16 coalescing_status;
	/** Custom IE for MLAN_OID_MISC_CUSTOM_IE */
		mlan_ds_misc_custom_ie cust_ie;
		t_u16 tdls_idle_time;
	/** TDLS configuration for MLAN_OID_MISC_TDLS_CONFIG */
		mlan_ds_misc_tdls_config tdls_config;
	/** TDLS operation for MLAN_OID_MISC_TDLS_OPER */
		mlan_ds_misc_tdls_oper tdls_oper;
	/** TDLS ies for  MLAN_OID_MISC_GET_TDLS_IES */
		mlan_ds_misc_tdls_ies tdls_ies;
	/**tdls cs off channel*/
		t_u8 tdls_cs_channel;
	/** Net monitor for MLAN_OID_MISC_NET_MONITOR */
		mlan_ds_misc_net_monitor net_mon;
	/** Tx data pause for MLAN_OID_MISC_TX_DATAPAUSE */
		mlan_ds_misc_tx_datapause tx_datapause;
	/** IP address configuration */
		mlan_ds_misc_ipaddr_cfg ipaddr_cfg;
	/** MAC control for MLAN_OID_MISC_MAC_CONTROL */
		t_u32 mac_ctrl;
	/** MEF configuration for MLAN_OID_MISC_MEF_CFG */
		mlan_ds_misc_mef_cfg mef_cfg;
	/** CFP code for MLAN_OID_MISC_CFP_CODE */
		mlan_ds_misc_cfp_code cfp_code;
	/** Country code for MLAN_OID_MISC_COUNTRY_CODE */
		mlan_ds_misc_country_code country_code;
	/** Thermal reading for MLAN_OID_MISC_THERMAL */
		t_u32 thermal;
	/** Mgmt subtype mask for MLAN_OID_MISC_RX_MGMT_IND */
		t_u32 mgmt_subtype_mask;
	/** subscribe event for MLAN_OID_MISC_SUBSCRIBE_EVENT */
		mlan_ds_subscribe_evt subscribe_event;
#ifdef DEBUG_LEVEL1
	/** Driver debug bit masks */
		t_u32 drvdbg;
#endif
#ifdef STA_SUPPORT
		ExtCap_t ext_cap;
#endif
		mlan_ds_misc_otp_user_data otp_user_data;
	/** Tx control */
		t_u32 tx_control;
#if defined(STA_SUPPORT)
		mlan_ds_misc_pmfcfg pmfcfg;
#endif
	/** Multi-channel config for MLAN_OID_MISC_MULTI_CHAN_CFG */
		mlan_ds_multi_chan_cfg multi_chan_cfg;
	/** Multi-channel policy for MLAN_OID_MISC_MULTI_CHAN_POLICY */
		t_u16 multi_chan_policy;
	/** channel drcs time slicing config for MLAN_OID_MISC_DRCS_CFG */
		mlan_ds_drcs_cfg drcs_cfg[2];
#ifdef WIFI_DIRECT_SUPPORT
		mlan_ds_wifi_direct_config p2p_config;
#endif
		mlan_ds_coalesce_cfg coalesce_cfg;
		mlan_ds_misc_dfs_repeater dfs_repeater;
#ifdef RX_PACKET_COALESCE
		mlan_ds_misc_rx_packet_coalesce rx_coalesce;
#endif
	/** FW reload flag */
		t_u8 fw_reload;
	/** Sensor temperature */
		mlan_ds_sensor_temp sensor_temp;
	/** GTK rekey data */
		mlan_ds_misc_gtk_rekey_data gtk_rekey;
	/** Bandwidth Channel operation */
		mlan_ds_bw_chan_oper bw_chan_oper;
    /** Independent Reset Configuration */
		mlan_ds_ind_rst_cfg ind_rst_cfg;
    /** Roam offload */
		mlan_ds_misc_roam_offload roam_offload;
    /** misc tsf */
		t_u64 misc_tsf;
    /** Custom regulatory domain */
		mlan_ds_custom_reg_domain custom_reg_domain;
    /** cwmmode */
		mlan_ds_cw_mode_ctrl cwmode;
   /**  Tx/Rx per-packet control */
		t_u8 txrx_pkt_ctrl;
		mlan_ds_misc_robustcoex_params robustcoexparams;
		mlan_ds_host_clock host_clock;
    /** boot sleep enable or disable */
		t_u16 boot_sleep;
	} param;
} mlan_ds_misc_cfg, *pmlan_ds_misc_cfg;

#endif /* !_MLAN_IOCTL_H_ */
