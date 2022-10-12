/*
 * Linux cfg80211 Vendor Extension Code
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: wl_cfgvendor.h 814814 2019-04-15 03:31:10Z $
 */

#ifndef _wl_cfgvendor_h_
#define _wl_cfgvendor_h_

#define OUI_BRCM    0x001018
#define OUI_GOOGLE  0x001A11
#define BRCM_VENDOR_SUBCMD_PRIV_STR	1
#define ATTRIBUTE_U32_LEN                  (NLA_HDRLEN  + 4)
#define VENDOR_ID_OVERHEAD                 ATTRIBUTE_U32_LEN
#define VENDOR_SUBCMD_OVERHEAD             ATTRIBUTE_U32_LEN
#define VENDOR_DATA_OVERHEAD               (NLA_HDRLEN)

enum brcm_vendor_attr {
	BRCM_ATTR_DRIVER_CMD		= 0,
	BRCM_ATTR_DRIVER_KEY_PMK	= 1,
	BRCM_ATTR_DRIVER_FEATURE_FLAGS	= 2,
	BRCM_ATTR_DRIVER_MAX		= 3
};

enum brcm_wlan_vendor_features {
	BRCM_WLAN_VENDOR_FEATURE_KEY_MGMT_OFFLOAD	= 0,
	BRCM_WLAN_VENDOR_FEATURES_MAX			= 1
};

#define SCAN_RESULTS_COMPLETE_FLAG_LEN       ATTRIBUTE_U32_LEN
#define SCAN_INDEX_HDR_LEN                   (NLA_HDRLEN)
#define SCAN_ID_HDR_LEN                      ATTRIBUTE_U32_LEN
#define SCAN_FLAGS_HDR_LEN                   ATTRIBUTE_U32_LEN
#define GSCAN_NUM_RESULTS_HDR_LEN            ATTRIBUTE_U32_LEN
#define GSCAN_CH_BUCKET_MASK_HDR_LEN         ATTRIBUTE_U32_LEN
#define GSCAN_RESULTS_HDR_LEN                (NLA_HDRLEN)
#define GSCAN_BATCH_RESULT_HDR_LEN  (SCAN_INDEX_HDR_LEN + SCAN_ID_HDR_LEN + \
									SCAN_FLAGS_HDR_LEN + \
							        GSCAN_NUM_RESULTS_HDR_LEN + \
								GSCAN_CH_BUCKET_MASK_HDR_LEN + \
									GSCAN_RESULTS_HDR_LEN)

#define VENDOR_REPLY_OVERHEAD       (VENDOR_ID_OVERHEAD + \
									VENDOR_SUBCMD_OVERHEAD + \
									VENDOR_DATA_OVERHEAD)

#define GSCAN_ATTR_SET1				10
#define GSCAN_ATTR_SET2				20
#define GSCAN_ATTR_SET3				30
#define GSCAN_ATTR_SET4				40
#define GSCAN_ATTR_SET5				50
#define GSCAN_ATTR_SET6				60
#define GSCAN_ATTR_SET7				70
#define GSCAN_ATTR_SET8				80
#define GSCAN_ATTR_SET9				90
#define GSCAN_ATTR_SET10			100
#define GSCAN_ATTR_SET11			110
#define GSCAN_ATTR_SET12			120
#define GSCAN_ATTR_SET13			130
#define GSCAN_ATTR_SET14			140

#define NAN_SVC_INFO_LEN			255
#define NAN_SID_ENABLE_FLAG_INVALID	0xff
#define NAN_SID_BEACON_COUNT_INVALID	0xff
#define WL_NAN_DW_INTERVAL 512

#define CFG80211_VENDOR_CMD_REPLY_SKB_SZ	100
#define CFG80211_VENDOR_EVT_SKB_SZ			2048

typedef enum {
	/* don't use 0 as a valid subcommand */
	VENDOR_NL80211_SUBCMD_UNSPECIFIED,

	/* define all vendor startup commands between 0x0 and 0x0FFF */
	VENDOR_NL80211_SUBCMD_RANGE_START = 0x0001,
	VENDOR_NL80211_SUBCMD_RANGE_END   = 0x0FFF,

	/* define all GScan related commands between 0x1000 and 0x10FF */
	ANDROID_NL80211_SUBCMD_GSCAN_RANGE_START = 0x1000,
	ANDROID_NL80211_SUBCMD_GSCAN_RANGE_END   = 0x10FF,

	/* define all RTT related commands between 0x1100 and 0x11FF */
	ANDROID_NL80211_SUBCMD_RTT_RANGE_START = 0x1100,
	ANDROID_NL80211_SUBCMD_RTT_RANGE_END   = 0x11FF,

	ANDROID_NL80211_SUBCMD_LSTATS_RANGE_START = 0x1200,
	ANDROID_NL80211_SUBCMD_LSTATS_RANGE_END   = 0x12FF,

	ANDROID_NL80211_SUBCMD_TDLS_RANGE_START = 0x1300,
	ANDROID_NL80211_SUBCMD_TDLS_RANGE_END	= 0x13FF,

	ANDROID_NL80211_SUBCMD_DEBUG_RANGE_START = 0x1400,
	ANDROID_NL80211_SUBCMD_DEBUG_RANGE_END	= 0x14FF,

	/* define all NearbyDiscovery related commands between 0x1500 and 0x15FF */
	ANDROID_NL80211_SUBCMD_NBD_RANGE_START = 0x1500,
	ANDROID_NL80211_SUBCMD_NBD_RANGE_END   = 0x15FF,

	/* define all wifi calling related commands between 0x1600 and 0x16FF */
	ANDROID_NL80211_SUBCMD_WIFI_OFFLOAD_RANGE_START = 0x1600,
	ANDROID_NL80211_SUBCMD_WIFI_OFFLOAD_RANGE_END	= 0x16FF,

	/* define all NAN related commands between 0x1700 and 0x17FF */
	ANDROID_NL80211_SUBCMD_NAN_RANGE_START = 0x1700,
	ANDROID_NL80211_SUBCMD_NAN_RANGE_END   = 0x17FF,

	/* define all packet filter related commands between 0x1800 and 0x18FF */
	ANDROID_NL80211_SUBCMD_PKT_FILTER_RANGE_START = 0x1800,
	ANDROID_NL80211_SUBCMD_PKT_FILTER_RANGE_END   = 0x18FF,

	/* define all tx power related commands between 0x1900 and 0x1910 */
	ANDROID_NL80211_SUBCMD_TX_POWER_RANGE_START = 0x1900,
	ANDROID_NL80211_SUBCMD_TX_POWER_RANGE_END   = 0x1910,

	/* define all thermal mode related commands between 0x1920 and 0x192F */
	ANDROID_NL80211_SUBCMD_MITIGATION_RANGE_START = 0x1920,
	ANDROID_NL80211_SUBCMD_MITIGATION_RANGE_END   = 0x192F,

	/* define all DSCP related commands between 0x2000 and 0x20FF */
	ANDROID_NL80211_SUBCMD_DSCP_RANGE_START =   0x2000,
	ANDROID_NL80211_SUBCMD_DSCP_RANGE_END   =   0x20FF,

	/* define all Channel Avoidance related commands between 0x2100 and 0x211F */
	ANDROID_NL80211_SUBCMD_CHAVOID_RANGE_START =    0x2100,
	ANDROID_NL80211_SUBCMD_CHAVOID_RANGE_END   =    0x211F,

	/* define all OTA Download related commands between 0x2120 and 0x212F */
	ANDROID_NL80211_SUBCMD_OTA_DOWNLOAD_START   = 0x2120,
	ANDROID_NL80211_SUBCMD_OTA_DOWNLOAD_END     = 0x212F,

	/* define all VOIP mode config related commands between 0x2130 and 0x213F */
	ANDROID_NL80211_SUBCMD_VIOP_MODE_START =        0x2130,
	ANDROID_NL80211_SUBCMD_VIOP_MODE_END =          0x213F,

	/* define all TWT related commands between 0x2140 and 0x214F */
	ANDROID_NL80211_SUBCMD_TWT_START =              0x2140,
	ANDROID_NL80211_SUBCMD_TWT_END =                0x214F,

	/* define all Usable Channel related commands between 0x2150 and 0x215F */
	ANDROID_NL80211_SUBCMD_USABLE_CHANNEL_START =   0x2150,
	ANDROID_NL80211_SUBCMD_USABLE_CHANNEL_END =     0x215F,

	/* define all init/deinit related commands between 0x2160 and 0x216F */
	ANDROID_NL80211_SUBCMD_INIT_DEINIT_RANGE_START = 0x2160,
	ANDROID_NL80211_SUBCMD_INIT_DEINIT_RANGE_END   = 0x216F,
	/* This is reserved for future usage */

} ANDROID_VENDOR_SUB_COMMAND;

enum andr_vendor_subcmd {
	GSCAN_SUBCMD_GET_CAPABILITIES = ANDROID_NL80211_SUBCMD_GSCAN_RANGE_START,
	GSCAN_SUBCMD_SET_CONFIG,
	GSCAN_SUBCMD_SET_SCAN_CONFIG,
	GSCAN_SUBCMD_ENABLE_GSCAN,
	GSCAN_SUBCMD_GET_SCAN_RESULTS,
	GSCAN_SUBCMD_SCAN_RESULTS,
	GSCAN_SUBCMD_SET_HOTLIST,
	GSCAN_SUBCMD_SET_SIGNIFICANT_CHANGE_CONFIG,
	GSCAN_SUBCMD_ENABLE_FULL_SCAN_RESULTS,
	GSCAN_SUBCMD_GET_CHANNEL_LIST,
	/* ANDR_WIFI_XXX although not related to gscan are defined here */
	ANDR_WIFI_SUBCMD_GET_FEATURE_SET,
	ANDR_WIFI_SUBCMD_GET_FEATURE_SET_MATRIX,
	ANDR_WIFI_SUBCMD_SET_PNO_RANDOM_MAC_OUI,    /*0x100C*/
	ANDR_WIFI_NODFS_CHANNELS,
	ANDR_WIFI_SET_COUNTRY,
	GSCAN_SUBCMD_SET_EPNO_SSID,
	WIFI_SUBCMD_SET_SSID_WHITELIST,
	WIFI_SUBCMD_SET_LAZY_ROAM_PARAMS,
	WIFI_SUBCMD_ENABLE_LAZY_ROAM,
	WIFI_SUBCMD_SET_BSSID_PREF,
	WIFI_SUBCMD_SET_BSSID_BLACKLIST,
	GSCAN_SUBCMD_ANQPO_CONFIG,
	WIFI_SUBCMD_SET_RSSI_MONITOR,
	WIFI_SUBCMD_CONFIG_ND_OFFLOAD,
	WIFI_SUBCMD_CONFIG_TCPACK_SUP,
	WIFI_SUBCMD_FW_ROAM_POLICY,
	WIFI_SUBCMD_ROAM_CAPABILITY,
	WIFI_SUBCMD_SET_LATENCY_MODE,     /*0x101b*/
	WIFI_SUBCMD_SET_MULTISTA_PRIMARY_CONNECTION,
	WIFI_SUBCMD_SET_MULTISTA_USE_CASE,
	WIFI_SUBCMD_SET_DTIM_CONFIG,
	GSCAN_SUBCMD_MAX,

	RTT_SUBCMD_SET_CONFIG = ANDROID_NL80211_SUBCMD_RTT_RANGE_START,
	RTT_SUBCMD_CANCEL_CONFIG,
	RTT_SUBCMD_GETCAPABILITY,
	RTT_SUBCMD_GETAVAILCHANNEL,
	RTT_SUBCMD_SET_RESPONDER,
	RTT_SUBCMD_CANCEL_RESPONDER,
	LSTATS_SUBCMD_GET_INFO = ANDROID_NL80211_SUBCMD_LSTATS_RANGE_START,

	DEBUG_START_LOGGING = ANDROID_NL80211_SUBCMD_DEBUG_RANGE_START,
	DEBUG_TRIGGER_MEM_DUMP,
	DEBUG_GET_MEM_DUMP,
	DEBUG_GET_VER,
	DEBUG_GET_RING_STATUS,
	DEBUG_GET_RING_DATA,
	DEBUG_GET_FEATURE,
	DEBUG_RESET_LOGGING,

	DEBUG_TRIGGER_DRIVER_MEM_DUMP,
	DEBUG_GET_DRIVER_MEM_DUMP,
	DEBUG_START_PKT_FATE_MONITORING,
	DEBUG_GET_TX_PKT_FATES,
	DEBUG_GET_RX_PKT_FATES,
	DEBUG_GET_WAKE_REASON_STATS,
	DEBUG_GET_FILE_DUMP_BUF,
	DEBUG_FILE_DUMP_DONE_IND,
	DEBUG_SET_HAL_START,
	DEBUG_SET_HAL_STOP,
	DEBUG_SET_HAL_PID,

	WIFI_OFFLOAD_SUBCMD_START_MKEEP_ALIVE = ANDROID_NL80211_SUBCMD_WIFI_OFFLOAD_RANGE_START,
	WIFI_OFFLOAD_SUBCMD_STOP_MKEEP_ALIVE,

	NAN_WIFI_SUBCMD_ENABLE = ANDROID_NL80211_SUBCMD_NAN_RANGE_START,	 /* 0x1700 */
	NAN_WIFI_SUBCMD_DISABLE,							/* 0x1701 */
	NAN_WIFI_SUBCMD_REQUEST_PUBLISH,					/* 0x1702 */
	NAN_WIFI_SUBCMD_REQUEST_SUBSCRIBE,					/* 0x1703 */
	NAN_WIFI_SUBCMD_CANCEL_PUBLISH,						/* 0x1704 */
	NAN_WIFI_SUBCMD_CANCEL_SUBSCRIBE,					/* 0x1705 */
	NAN_WIFI_SUBCMD_TRANSMIT,							/* 0x1706 */
	NAN_WIFI_SUBCMD_CONFIG,								/* 0x1707 */
	NAN_WIFI_SUBCMD_TCA,								/* 0x1708 */
	NAN_WIFI_SUBCMD_STATS,								/* 0x1709 */
	NAN_WIFI_SUBCMD_GET_CAPABILITIES,					/* 0x170A */
	NAN_WIFI_SUBCMD_DATA_PATH_IFACE_CREATE,				/* 0x170B */
	NAN_WIFI_SUBCMD_DATA_PATH_IFACE_DELETE,				/* 0x170C */
	NAN_WIFI_SUBCMD_DATA_PATH_REQUEST,					/* 0x170D */
	NAN_WIFI_SUBCMD_DATA_PATH_RESPONSE,					/* 0x170E */
	NAN_WIFI_SUBCMD_DATA_PATH_END,						/* 0x170F */
	NAN_WIFI_SUBCMD_DATA_PATH_SEC_INFO,					/* 0x1710 */
	NAN_WIFI_SUBCMD_VERSION_INFO,						/* 0x1711 */
	NAN_SUBCMD_ENABLE_MERGE,							/* 0x1712 */
	APF_SUBCMD_GET_CAPABILITIES = ANDROID_NL80211_SUBCMD_PKT_FILTER_RANGE_START,
	APF_SUBCMD_SET_FILTER,
	APF_SUBCMD_READ_FILTER,
	WIFI_SUBCMD_TX_POWER_SCENARIO = ANDROID_NL80211_SUBCMD_TX_POWER_RANGE_START, /*0x1900*/

	WIFI_SUBCMD_THERMAL_MITIGATION = ANDROID_NL80211_SUBCMD_MITIGATION_RANGE_START,
	DSCP_SUBCMD_SET_TABLE = ANDROID_NL80211_SUBCMD_DSCP_RANGE_START,
	DSCP_SUBCMD_RESET_TABLE,
	CHAVOID_SUBCMD_SET_CONFIG = ANDROID_NL80211_SUBCMD_CHAVOID_RANGE_START,

	WIFI_SUBCMD_GET_OTA_CURRUNT_INFO = ANDROID_NL80211_SUBCMD_OTA_DOWNLOAD_START,
	WIFI_SUBCMD_OTA_UPDATE,
	WIFI_SUBCMD_CONFIG_VOIP_MODE = ANDROID_NL80211_SUBCMD_VIOP_MODE_START,

	TWT_SUBCMD_GETCAPABILITY    = ANDROID_NL80211_SUBCMD_TWT_START,
	TWT_SUBCMD_SETUP_REQUEST,
	TWT_SUBCMD_TEAR_DOWN_REQUEST,
	TWT_SUBCMD_INFO_FRAME_REQUEST,
	TWT_SUBCMD_GETSTATS,
	TWT_SUBCMD_CLR_STATS,
	WIFI_SUBCMD_USABLE_CHANNEL = ANDROID_NL80211_SUBCMD_USABLE_CHANNEL_START,
	WIFI_SUBCMD_TRIGGER_SSR = ANDROID_NL80211_SUBCMD_INIT_DEINIT_RANGE_START,
	/* Add more sub commands here */
	VENDOR_SUBCMD_MAX
};

enum gscan_attributes {
    GSCAN_ATTRIBUTE_NUM_BUCKETS = GSCAN_ATTR_SET1,
    GSCAN_ATTRIBUTE_BASE_PERIOD,
    GSCAN_ATTRIBUTE_BUCKETS_BAND,
    GSCAN_ATTRIBUTE_BUCKET_ID,
    GSCAN_ATTRIBUTE_BUCKET_PERIOD,
    GSCAN_ATTRIBUTE_BUCKET_NUM_CHANNELS,
    GSCAN_ATTRIBUTE_BUCKET_CHANNELS,
    GSCAN_ATTRIBUTE_NUM_AP_PER_SCAN,
    GSCAN_ATTRIBUTE_REPORT_THRESHOLD,
    GSCAN_ATTRIBUTE_NUM_SCANS_TO_CACHE,
    GSCAN_ATTRIBUTE_BAND = GSCAN_ATTRIBUTE_BUCKETS_BAND,

    GSCAN_ATTRIBUTE_ENABLE_FEATURE = GSCAN_ATTR_SET2,
    GSCAN_ATTRIBUTE_SCAN_RESULTS_COMPLETE,
    GSCAN_ATTRIBUTE_FLUSH_FEATURE,
    GSCAN_ATTRIBUTE_ENABLE_FULL_SCAN_RESULTS,
    GSCAN_ATTRIBUTE_REPORT_EVENTS,
    /* remaining reserved for additional attributes */
    GSCAN_ATTRIBUTE_NUM_OF_RESULTS = GSCAN_ATTR_SET3,
    GSCAN_ATTRIBUTE_FLUSH_RESULTS,
    GSCAN_ATTRIBUTE_SCAN_RESULTS,                       /* flat array of wifi_scan_result */
    GSCAN_ATTRIBUTE_SCAN_ID,                            /* indicates scan number */
    GSCAN_ATTRIBUTE_SCAN_FLAGS,                         /* indicates if scan was aborted */
    GSCAN_ATTRIBUTE_AP_FLAGS,                           /* flags on significant change event */
    GSCAN_ATTRIBUTE_NUM_CHANNELS,
    GSCAN_ATTRIBUTE_CHANNEL_LIST,
    GSCAN_ATTRIBUTE_CH_BUCKET_BITMASK,

	/* remaining reserved for additional attributes */

    GSCAN_ATTRIBUTE_SSID = GSCAN_ATTR_SET4,
    GSCAN_ATTRIBUTE_BSSID,
    GSCAN_ATTRIBUTE_CHANNEL,
    GSCAN_ATTRIBUTE_RSSI,
    GSCAN_ATTRIBUTE_TIMESTAMP,
    GSCAN_ATTRIBUTE_RTT,
    GSCAN_ATTRIBUTE_RTTSD,

    /* remaining reserved for additional attributes */

    GSCAN_ATTRIBUTE_HOTLIST_BSSIDS = GSCAN_ATTR_SET5,
    GSCAN_ATTRIBUTE_RSSI_LOW,
    GSCAN_ATTRIBUTE_RSSI_HIGH,
    GSCAN_ATTRIBUTE_HOSTLIST_BSSID_ELEM,
    GSCAN_ATTRIBUTE_HOTLIST_FLUSH,
    GSCAN_ATTRIBUTE_HOTLIST_BSSID_COUNT,

    /* remaining reserved for additional attributes */
    GSCAN_ATTRIBUTE_RSSI_SAMPLE_SIZE = GSCAN_ATTR_SET6,
    GSCAN_ATTRIBUTE_LOST_AP_SAMPLE_SIZE,
    GSCAN_ATTRIBUTE_MIN_BREACHING,
    GSCAN_ATTRIBUTE_SIGNIFICANT_CHANGE_BSSIDS,
    GSCAN_ATTRIBUTE_SIGNIFICANT_CHANGE_FLUSH,

    /* EPNO */
    GSCAN_ATTRIBUTE_EPNO_SSID_LIST = GSCAN_ATTR_SET7,
    GSCAN_ATTRIBUTE_EPNO_SSID,
    GSCAN_ATTRIBUTE_EPNO_SSID_LEN,
    GSCAN_ATTRIBUTE_EPNO_RSSI,
    GSCAN_ATTRIBUTE_EPNO_FLAGS,
    GSCAN_ATTRIBUTE_EPNO_AUTH,
    GSCAN_ATTRIBUTE_EPNO_SSID_NUM,
    GSCAN_ATTRIBUTE_EPNO_FLUSH,

    /* Roam SSID Whitelist and BSSID pref */
    GSCAN_ATTRIBUTE_WHITELIST_SSID = GSCAN_ATTR_SET8,
    GSCAN_ATTRIBUTE_NUM_WL_SSID,
    GSCAN_ATTRIBUTE_WL_SSID_LEN,
    GSCAN_ATTRIBUTE_WL_SSID_FLUSH,
    GSCAN_ATTRIBUTE_WHITELIST_SSID_ELEM,
    GSCAN_ATTRIBUTE_NUM_BSSID,
    GSCAN_ATTRIBUTE_BSSID_PREF_LIST,
    GSCAN_ATTRIBUTE_BSSID_PREF_FLUSH,
    GSCAN_ATTRIBUTE_BSSID_PREF,
    GSCAN_ATTRIBUTE_RSSI_MODIFIER,

    /* Roam cfg */
    GSCAN_ATTRIBUTE_A_BAND_BOOST_THRESHOLD = GSCAN_ATTR_SET9,
    GSCAN_ATTRIBUTE_A_BAND_PENALTY_THRESHOLD,
    GSCAN_ATTRIBUTE_A_BAND_BOOST_FACTOR,
    GSCAN_ATTRIBUTE_A_BAND_PENALTY_FACTOR,
    GSCAN_ATTRIBUTE_A_BAND_MAX_BOOST,
    GSCAN_ATTRIBUTE_LAZY_ROAM_HYSTERESIS,
    GSCAN_ATTRIBUTE_ALERT_ROAM_RSSI_TRIGGER,
    GSCAN_ATTRIBUTE_LAZY_ROAM_ENABLE,

    /* BSSID blacklist */
    GSCAN_ATTRIBUTE_BSSID_BLACKLIST_FLUSH = GSCAN_ATTR_SET10,
    GSCAN_ATTRIBUTE_BLACKLIST_BSSID,

    GSCAN_ATTRIBUTE_ANQPO_HS_LIST = GSCAN_ATTR_SET11,
    GSCAN_ATTRIBUTE_ANQPO_HS_LIST_SIZE,
    GSCAN_ATTRIBUTE_ANQPO_HS_NETWORK_ID,
    GSCAN_ATTRIBUTE_ANQPO_HS_NAI_REALM,
    GSCAN_ATTRIBUTE_ANQPO_HS_ROAM_CONSORTIUM_ID,
    GSCAN_ATTRIBUTE_ANQPO_HS_PLMN,

    /* Adaptive scan attributes */
    GSCAN_ATTRIBUTE_BUCKET_STEP_COUNT = GSCAN_ATTR_SET12,
    GSCAN_ATTRIBUTE_BUCKET_MAX_PERIOD,

    /* ePNO cfg */
    GSCAN_ATTRIBUTE_EPNO_5G_RSSI_THR = GSCAN_ATTR_SET13,
    GSCAN_ATTRIBUTE_EPNO_2G_RSSI_THR,
    GSCAN_ATTRIBUTE_EPNO_INIT_SCORE_MAX,
    GSCAN_ATTRIBUTE_EPNO_CUR_CONN_BONUS,
    GSCAN_ATTRIBUTE_EPNO_SAME_NETWORK_BONUS,
    GSCAN_ATTRIBUTE_EPNO_SECURE_BONUS,
    GSCAN_ATTRIBUTE_EPNO_5G_BONUS,

    /* Android O Roaming features */
    GSCAN_ATTRIBUTE_ROAM_STATE_SET = GSCAN_ATTR_SET14,

    GSCAN_ATTRIBUTE_MAX
};

enum gscan_bucket_attributes {
	GSCAN_ATTRIBUTE_CH_BUCKET_1,
	GSCAN_ATTRIBUTE_CH_BUCKET_2,
	GSCAN_ATTRIBUTE_CH_BUCKET_3,
	GSCAN_ATTRIBUTE_CH_BUCKET_4,
	GSCAN_ATTRIBUTE_CH_BUCKET_5,
	GSCAN_ATTRIBUTE_CH_BUCKET_6,
	GSCAN_ATTRIBUTE_CH_BUCKET_7
};

enum gscan_ch_attributes {
	GSCAN_ATTRIBUTE_CH_ID_1,
	GSCAN_ATTRIBUTE_CH_ID_2,
	GSCAN_ATTRIBUTE_CH_ID_3,
	GSCAN_ATTRIBUTE_CH_ID_4,
	GSCAN_ATTRIBUTE_CH_ID_5,
	GSCAN_ATTRIBUTE_CH_ID_6,
	GSCAN_ATTRIBUTE_CH_ID_7
};

enum rtt_attributes {
	RTT_ATTRIBUTE_TARGET_CNT,
	RTT_ATTRIBUTE_TARGET_INFO,
	RTT_ATTRIBUTE_TARGET_MAC,
	RTT_ATTRIBUTE_TARGET_TYPE,
	RTT_ATTRIBUTE_TARGET_PEER,
	RTT_ATTRIBUTE_TARGET_CHAN,
	RTT_ATTRIBUTE_TARGET_PERIOD,
	RTT_ATTRIBUTE_TARGET_NUM_BURST,
	RTT_ATTRIBUTE_TARGET_NUM_FTM_BURST,
	RTT_ATTRIBUTE_TARGET_NUM_RETRY_FTM,
	RTT_ATTRIBUTE_TARGET_NUM_RETRY_FTMR,
	RTT_ATTRIBUTE_TARGET_LCI,
	RTT_ATTRIBUTE_TARGET_LCR,
	RTT_ATTRIBUTE_TARGET_BURST_DURATION,
	RTT_ATTRIBUTE_TARGET_PREAMBLE,
	RTT_ATTRIBUTE_TARGET_BW,
	RTT_ATTRIBUTE_RESULTS_COMPLETE = 30,
	RTT_ATTRIBUTE_RESULTS_PER_TARGET,
	RTT_ATTRIBUTE_RESULT_CNT,
	RTT_ATTRIBUTE_RESULT,
	RTT_ATTRIBUTE_RESULT_DETAIL
};

enum wifi_rssi_monitor_attr {
#ifdef ANDROID12_SUPPORT
	RSSI_MONITOR_ATTRIBUTE_INVALID,
#endif // endif
	RSSI_MONITOR_ATTRIBUTE_MAX_RSSI,
	RSSI_MONITOR_ATTRIBUTE_MIN_RSSI,
	RSSI_MONITOR_ATTRIBUTE_START,
	RSSI_MONITOR_ATTRIBUTE_MAX
};

enum wifi_sae_key_attr {
	BRCM_SAE_KEY_ATTR_PEER_MAC,
	BRCM_SAE_KEY_ATTR_PMK,
	BRCM_SAE_KEY_ATTR_PMKID
};

enum debug_attributes {
#ifdef ANDROID12_SUPPORT
	DEBUG_ATTRIBUTE_INVALID,
#endif // endif
	DEBUG_ATTRIBUTE_GET_DRIVER,
	DEBUG_ATTRIBUTE_GET_FW,
	DEBUG_ATTRIBUTE_RING_ID,
	DEBUG_ATTRIBUTE_RING_NAME,
	DEBUG_ATTRIBUTE_RING_FLAGS,
	DEBUG_ATTRIBUTE_LOG_LEVEL,
	DEBUG_ATTRIBUTE_LOG_TIME_INTVAL,
	DEBUG_ATTRIBUTE_LOG_MIN_DATA_SIZE,
	DEBUG_ATTRIBUTE_FW_DUMP_LEN,
	DEBUG_ATTRIBUTE_FW_DUMP_DATA,
	DEBUG_ATTRIBUTE_FW_ERR_CODE,
	DEBUG_ATTRIBUTE_RING_DATA,
	DEBUG_ATTRIBUTE_RING_STATUS,
	DEBUG_ATTRIBUTE_RING_NUM,
	DEBUG_ATTRIBUTE_DRIVER_DUMP_LEN,
	DEBUG_ATTRIBUTE_DRIVER_DUMP_DATA,
	DEBUG_ATTRIBUTE_PKT_FATE_NUM,
	DEBUG_ATTRIBUTE_PKT_FATE_DATA,
	DEBUG_ATTRIBUTE_HANG_REASON,
	/* Add new attributes just above this */
	DEBUG_ATTRIBUTE_MAX
};

typedef enum {
	DUMP_LEN_ATTR_INVALID,
	DUMP_LEN_ATTR_MEMDUMP,
	DUMP_LEN_ATTR_SSSR_C0_D11_BEFORE,
	DUMP_LEN_ATTR_SSSR_C0_D11_AFTER,
	DUMP_LEN_ATTR_SSSR_C1_D11_BEFORE,
	DUMP_LEN_ATTR_SSSR_C1_D11_AFTER,
	DUMP_LEN_ATTR_SSSR_DIG_BEFORE,
	DUMP_LEN_ATTR_SSSR_DIG_AFTER,
	DUMP_LEN_ATTR_TIMESTAMP,
	DUMP_LEN_ATTR_GENERAL_LOG,
	DUMP_LEN_ATTR_ECNTRS,
	DUMP_LEN_ATTR_SPECIAL_LOG,
	DUMP_LEN_ATTR_DHD_DUMP,
	DUMP_LEN_ATTR_EXT_TRAP,
	DUMP_LEN_ATTR_HEALTH_CHK,
	DUMP_LEN_ATTR_PRESERVE_LOG,
	DUMP_LEN_ATTR_COOKIE,
	DUMP_LEN_ATTR_FLOWRING_DUMP,
	DUMP_LEN_ATTR_PKTLOG,
	DUMP_FILENAME_ATTR_DEBUG_DUMP,
	DUMP_FILENAME_ATTR_MEM_DUMP,
	DUMP_FILENAME_ATTR_SSSR_CORE_0_BEFORE_DUMP,
	DUMP_FILENAME_ATTR_SSSR_CORE_0_AFTER_DUMP,
	DUMP_FILENAME_ATTR_SSSR_CORE_1_BEFORE_DUMP,
	DUMP_FILENAME_ATTR_SSSR_CORE_1_AFTER_DUMP,
	DUMP_FILENAME_ATTR_SSSR_DIG_BEFORE_DUMP,
	DUMP_FILENAME_ATTR_SSSR_DIG_AFTER_DUMP,
	DUMP_FILENAME_ATTR_PKTLOG_DUMP,
	DUMP_LEN_ATTR_STATUS_LOG,
	DUMP_LEN_ATTR_AXI_ERROR,
	DUMP_FILENAME_ATTR_AXI_ERROR_DUMP,
	DUMP_LEN_ATTR_RTT_LOG
} EWP_DUMP_EVENT_ATTRIBUTE;

/* Attributes associated with DEBUG_GET_DUMP_BUF */
typedef enum {
	DUMP_BUF_ATTR_INVALID,
	DUMP_BUF_ATTR_MEMDUMP,
	DUMP_BUF_ATTR_SSSR_C0_D11_BEFORE,
	DUMP_BUF_ATTR_SSSR_C0_D11_AFTER,
	DUMP_BUF_ATTR_SSSR_C1_D11_BEFORE,
	DUMP_BUF_ATTR_SSSR_C1_D11_AFTER,
	DUMP_BUF_ATTR_SSSR_DIG_BEFORE,
	DUMP_BUF_ATTR_SSSR_DIG_AFTER,
	DUMP_BUF_ATTR_TIMESTAMP,
	DUMP_BUF_ATTR_GENERAL_LOG,
	DUMP_BUF_ATTR_ECNTRS,
	DUMP_BUF_ATTR_SPECIAL_LOG,
	DUMP_BUF_ATTR_DHD_DUMP,
	DUMP_BUF_ATTR_EXT_TRAP,
	DUMP_BUF_ATTR_HEALTH_CHK,
	DUMP_BUF_ATTR_PRESERVE_LOG,
	DUMP_BUF_ATTR_COOKIE,
	DUMP_BUF_ATTR_FLOWRING_DUMP,
	DUMP_BUF_ATTR_PKTLOG,
	DUMP_BUF_ATTR_STATUS_LOG,
	DUMP_BUF_ATTR_AXI_ERROR,
	DUMP_BUF_ATTR_RTT_LOG
} EWP_DUMP_CMD_ATTRIBUTE;

enum mkeep_alive_attributes {
#ifdef ANDROID12_SUPPORT
	MKEEP_ALIVE_ATTRIBUTE_INVALID,
#endif // endif
	MKEEP_ALIVE_ATTRIBUTE_ID,
	MKEEP_ALIVE_ATTRIBUTE_IP_PKT,
	MKEEP_ALIVE_ATTRIBUTE_IP_PKT_LEN,
	MKEEP_ALIVE_ATTRIBUTE_SRC_MAC_ADDR,
	MKEEP_ALIVE_ATTRIBUTE_DST_MAC_ADDR,
	MKEEP_ALIVE_ATTRIBUTE_PERIOD_MSEC,
	MKEEP_ALIVE_ATTRIBUTE_ETHER_TYPE,
	MKEEP_ALIVE_ATTRIBUTE_MAX
};

typedef enum wl_vendor_event {
	BRCM_VENDOR_EVENT_UNSPEC		= 0,
	BRCM_VENDOR_EVENT_PRIV_STR		= 1,
	GOOGLE_GSCAN_SIGNIFICANT_EVENT		= 2,
	GOOGLE_GSCAN_GEOFENCE_FOUND_EVENT	= 3,
	GOOGLE_GSCAN_BATCH_SCAN_EVENT		= 4,
	GOOGLE_SCAN_FULL_RESULTS_EVENT		= 5,
	GOOGLE_RTT_COMPLETE_EVENT		= 6,
	GOOGLE_SCAN_COMPLETE_EVENT		= 7,
	GOOGLE_GSCAN_GEOFENCE_LOST_EVENT	= 8,
	GOOGLE_SCAN_EPNO_EVENT			= 9,
	GOOGLE_DEBUG_RING_EVENT			= 10,
	GOOGLE_FW_DUMP_EVENT			= 11,
	GOOGLE_PNO_HOTSPOT_FOUND_EVENT		= 12,
	GOOGLE_RSSI_MONITOR_EVENT		= 13,
	GOOGLE_MKEEP_ALIVE_EVENT		= 14,

	/*
	 * BRCM specific events should be placed after
	 * the Generic events so that enums don't mismatch
	 * between the DHD and HAL
	 */
	GOOGLE_NAN_EVENT_ENABLED		= 15,
	GOOGLE_NAN_EVENT_DISABLED		= 16,
	GOOGLE_NAN_EVENT_SUBSCRIBE_MATCH	= 17,
	GOOGLE_NAN_EVENT_REPLIED		= 18,
	GOOGLE_NAN_EVENT_PUBLISH_TERMINATED	= 19,
	GOOGLE_NAN_EVENT_SUBSCRIBE_TERMINATED	= 20,
	GOOGLE_NAN_EVENT_DE_EVENT		= 21,
	GOOGLE_NAN_EVENT_FOLLOWUP		= 22,
	GOOGLE_NAN_EVENT_TRANSMIT_FOLLOWUP_IND	= 23,
	GOOGLE_NAN_EVENT_DATA_REQUEST		= 24,
	GOOGLE_NAN_EVENT_DATA_CONFIRMATION	= 25,
	GOOGLE_NAN_EVENT_DATA_END		= 26,
	GOOGLE_NAN_EVENT_BEACON			= 27,
	GOOGLE_NAN_EVENT_SDF			= 28,
	GOOGLE_NAN_EVENT_TCA			= 29,
	GOOGLE_NAN_EVENT_SUBSCRIBE_UNMATCH	= 30,
	GOOGLE_NAN_EVENT_UNKNOWN		= 31,
	GOOGLE_ROAM_EVENT_START			= 32,
	BRCM_VENDOR_EVENT_HANGED                = 33,
	BRCM_VENDOR_EVENT_SAE_KEY               = 34,
	BRCM_VENDOR_EVENT_BEACON_RECV           = 35,
	BRCM_VENDOR_EVENT_PORT_AUTHORIZED       = 36,
	GOOGLE_FILE_DUMP_EVENT			= 37,
	BRCM_VENDOR_EVENT_CU			= 38,
	BRCM_VENDOR_EVENT_WIPS			= 39,
	NAN_ASYNC_RESPONSE_DISABLED		= 40,
	BRCM_VENDOR_EVENT_RCC_INFO		= 41,
	BRCM_VENDOR_EVENT_ACS			= 42,
	BRCM_VENDOR_EVENT_OVERTEMP		= 43,
	BRCM_VENDOR_EVENT_LAST
} wl_vendor_event_t;

enum andr_wifi_attr {
#ifdef ANDROID12_SUPPORT
	ANDR_WIFI_ATTRIBUTE_INVALID,
#endif // endif
	ANDR_WIFI_ATTRIBUTE_NUM_FEATURE_SET,
	ANDR_WIFI_ATTRIBUTE_FEATURE_SET,
	ANDR_WIFI_ATTRIBUTE_PNO_RANDOM_MAC_OUI,
	ANDR_WIFI_ATTRIBUTE_NODFS_SET,
	ANDR_WIFI_ATTRIBUTE_COUNTRY,
	ANDR_WIFI_ATTRIBUTE_ND_OFFLOAD_VALUE,
	ANDR_WIFI_ATTRIBUTE_TCPACK_SUP_VALUE,
	ANDR_WIFI_ATTRIBUTE_LATENCY_MODE,
	ANDR_WIFI_ATTRIBUTE_RANDOM_MAC,
	ANDR_WIFI_ATTRIBUTE_TX_POWER_SCENARIO,
	ANDR_WIFI_ATTRIBUTE_THERMAL_MITIGATION,
	ANDR_WIFI_ATTRIBUTE_THERMAL_COMPLETION_WINDOW,
	ANDR_WIFI_ATTRIBUTE_VOIP_MODE,
	ANDR_WIFI_ATTRIBUTE_DTIM_MULTIPLIER,
	//Add more attributes here
	ANDR_WIFI_ATTRIBUTE_MAX
};
enum apf_attributes {
	APF_ATTRIBUTE_VERSION,
	APF_ATTRIBUTE_MAX_LEN,
	APF_ATTRIBUTE_PROGRAM,
	APF_ATTRIBUTE_PROGRAM_LEN
};

typedef enum wl_vendor_gscan_attribute {
	ATTR_START_GSCAN,
	ATTR_STOP_GSCAN,
	ATTR_SET_SCAN_BATCH_CFG_ID, /* set batch scan params */
	ATTR_SET_SCAN_GEOFENCE_CFG_ID, /* set list of bssids to track */
	ATTR_SET_SCAN_SIGNIFICANT_CFG_ID, /* set list of bssids, rssi threshold etc.. */
	ATTR_SET_SCAN_CFG_ID, /* set common scan config params here */
	ATTR_GET_GSCAN_CAPABILITIES_ID,
    /* Add more sub commands here */
	ATTR_GSCAN_MAX
} wl_vendor_gscan_attribute_t;

typedef enum gscan_batch_attribute {
	ATTR_GSCAN_BATCH_BESTN,
	ATTR_GSCAN_BATCH_MSCAN,
	ATTR_GSCAN_BATCH_BUFFER_THRESHOLD
} gscan_batch_attribute_t;

typedef enum gscan_geofence_attribute {
	ATTR_GSCAN_NUM_HOTLIST_BSSID,
	ATTR_GSCAN_HOTLIST_BSSID
} gscan_geofence_attribute_t;

typedef enum gscan_complete_event {
	WIFI_SCAN_COMPLETE,
	WIFI_SCAN_THRESHOLD_NUM_SCANS,
	WIFI_SCAN_BUFFER_THR_BREACHED
} gscan_complete_event_t;

#ifdef DHD_WAKE_STATUS
enum wake_stat_attributes {
#ifdef ANDROID12_SUPPORT
	WAKE_STAT_ATTRIBUTE_INVALID,
#endif // endif
	WAKE_STAT_ATTRIBUTE_TOTAL_CMD_EVENT,
	WAKE_STAT_ATTRIBUTE_CMD_EVENT_WAKE,
	WAKE_STAT_ATTRIBUTE_CMD_EVENT_COUNT,
	WAKE_STAT_ATTRIBUTE_CMD_EVENT_COUNT_USED,
	WAKE_STAT_ATTRIBUTE_TOTAL_DRIVER_FW,
	WAKE_STAT_ATTRIBUTE_DRIVER_FW_WAKE,
	WAKE_STAT_ATTRIBUTE_DRIVER_FW_COUNT,
	WAKE_STAT_ATTRIBUTE_DRIVER_FW_COUNT_USED,
	WAKE_STAT_ATTRIBUTE_TOTAL_RX_DATA_WAKE,
	WAKE_STAT_ATTRIBUTE_RX_UNICAST_COUNT,
	WAKE_STAT_ATTRIBUTE_RX_MULTICAST_COUNT,
	WAKE_STAT_ATTRIBUTE_RX_BROADCAST_COUNT,
	WAKE_STAT_ATTRIBUTE_RX_ICMP_PKT,
	WAKE_STAT_ATTRIBUTE_RX_ICMP6_PKT,
	WAKE_STAT_ATTRIBUTE_RX_ICMP6_RA,
	WAKE_STAT_ATTRIBUTE_RX_ICMP6_NA,
	WAKE_STAT_ATTRIBUTE_RX_ICMP6_NS,
	WAKE_STAT_ATTRIBUTE_IPV4_RX_MULTICAST_ADD_CNT,
	WAKE_STAT_ATTRIBUTE_IPV6_RX_MULTICAST_ADD_CNT,
	WAKE_STAT_ATTRIBUTE_OTHER_RX_MULTICAST_ADD_CNT,
	WAKE_STAT_ATTRIBUTE_RX_MULTICAST_PKT_INFO,
	WAKE_STAT_ATTRIBUTE_MAX
};

typedef struct rx_data_cnt_details_t {
	int rx_unicast_cnt;		/* Total rx unicast packet which woke up host */
	int rx_multicast_cnt;   /* Total rx multicast packet which woke up host */
	int rx_broadcast_cnt;   /* Total rx broadcast packet which woke up host */
} RX_DATA_WAKE_CNT_DETAILS;

typedef struct rx_wake_pkt_type_classification_t {
	int icmp_pkt;   /* wake icmp packet count */
	int icmp6_pkt;  /* wake icmp6 packet count */
	int icmp6_ra;   /* wake icmp6 RA packet count */
	int icmp6_na;   /* wake icmp6 NA packet count */
	int icmp6_ns;   /* wake icmp6 NS packet count */
} RX_WAKE_PKT_TYPE_CLASSFICATION;

typedef struct rx_multicast_cnt_t {
	int ipv4_rx_multicast_addr_cnt; /* Rx wake packet was ipv4 multicast */
	int ipv6_rx_multicast_addr_cnt; /* Rx wake packet was ipv6 multicast */
	int other_rx_multicast_addr_cnt; /* Rx wake packet was non-ipv4 and non-ipv6 */
} RX_MULTICAST_WAKE_DATA_CNT;

typedef struct wlan_driver_wake_reason_cnt_t {
	int total_cmd_event_wake;    /* Total count of cmd event wakes */
	int *cmd_event_wake_cnt;     /* Individual wake count array, each index a reason */
	int cmd_event_wake_cnt_sz;   /* Max number of cmd event wake reasons */
	int cmd_event_wake_cnt_used; /* Number of cmd event wake reasons specific to the driver */
	int total_driver_fw_local_wake;    /* Total count of drive/fw wakes, for local reasons */
	int *driver_fw_local_wake_cnt;     /* Individual wake count array, each index a reason */
	int driver_fw_local_wake_cnt_sz;   /* Max number of local driver/fw wake reasons */
	/* Number of local driver/fw wake reasons specific to the driver */
	int driver_fw_local_wake_cnt_used;
	int total_rx_data_wake;     /* total data rx packets, that woke up host */
	RX_DATA_WAKE_CNT_DETAILS rx_wake_details;
	RX_WAKE_PKT_TYPE_CLASSFICATION rx_wake_pkt_classification_info;
	RX_MULTICAST_WAKE_DATA_CNT rx_multicast_wake_pkt_info;
} WLAN_DRIVER_WAKE_REASON_CNT;
#endif /* DHD_WAKE_STATUS */

typedef enum {
	SET_HAL_START_ATTRIBUTE_DEINIT = 0x0001,
	SET_HAL_START_ATTRIBUTE_PRE_INIT = 0x0002,
	SET_HAL_START_ATTRIBUTE_EVENT_SOCK_PID = 0x0003,
	SET_HAL_START_ATTRIBUTE_MAX
} SET_HAL_START_ATTRIBUTE;

typedef enum {
    CHAVOID_ATTRIBUTE_INVALID   = 0,
    CHAVOID_ATTRIBUTE_CNT       = 1,
    CHAVOID_ATTRIBUTE_CONFIG    = 2,
    CHAVOID_ATTRIBUTE_BAND      = 3,
    CHAVOID_ATTRIBUTE_CHANNEL   = 4,
    CHAVOID_ATTRIBUTE_PWRCAP    = 5,
    CHAVOID_ATTRIBUTE_MANDATORY = 6,
    CHAVOID_ATTRIBUTE_MAX
} CHAVOID_ATTRIBUTE;

typedef enum {
    USABLECHAN_ATTRIBUTE_INVALID    = 0,
    USABLECHAN_ATTRIBUTE_BAND       = 1,
    USABLECHAN_ATTRIBUTE_IFACE      = 2,
    USABLECHAN_ATTRIBUTE_FILTER     = 3,
    USABLECHAN_ATTRIBUTE_MAX_SIZE   = 4,
    USABLECHAN_ATTRIBUTE_SIZE       = 5,
    USABLECHAN_ATTRIBUTE_CHANNELS   = 6,
    USABLECHAN_ATTRIBUTE_MAX
} USABLECHAN_ATTRIBUTE;

typedef enum {
        /**
        * Usage:
        * - This will be sent down for make before break use-case.
        * - Platform is trying to speculatively connect to a second network and evaluate it without
        *   disrupting the primary connection.
        *
        * Requirements for Firmware:
        * - Do not reduce the number of tx/rx chains of primary connection.
        * - If using MCC, should set the MCC duty cycle of the primary connection to be higher than
        *   the secondary connection (maybe 70/30 split).
        * - Should pick the best BSSID for the secondary STA (disregard the chip mode)
        *   independent of the primary STA:
        * - Don't optimize for DBS vs MCC/SCC
        * - Should not impact the primary connections bssid selection:
        * - Don't downgrade chains of the existing primary connection.
        * - Don't optimize for DBS vs MCC/SCC.
        */
        WIFI_DUAL_STA_TRANSIENT_PREFER_PRIMARY = 0,
        /**
        * Usage:
        * - This will be sent down for any app requested peer to peer connections.
        * - In this case, both the connections needs to be allocated equal resources.
        * - For the peer to peer use case, BSSID for the secondary connection will be chosen by the
        *   framework.
        *
        * Requirements for Firmware:
        * - Can choose MCC or DBS mode depending on the MCC efficiency and HW capability.
        * - If using MCC, set the MCC duty cycle of the primary connection to be equal to the
        *   secondary connection.
        * - Prefer BSSID candidates which will help provide the best "overall" performance for
        *   both the connections.
        */
        WIFI_DUAL_STA_NON_TRANSIENT_UNBIASED = 1
} wifi_multi_sta_use_case;

typedef enum {
    MULTISTA_ATTRIBUTE_PRIM_CONN_IFACE,
    MULTISTA_ATTRIBUTE_USE_CASE,
    MULTISTA_ATTRIBUTE_MAX
} MULTISTA_ATTRIBUTE;

#ifdef WL_WIPSEVT
#define BRCM_VENDOR_WIPS_EVENT_BUF_LEN	128
typedef enum wl_vendor_wips_attr_type {
	WIPS_ATTR_DEAUTH_CNT = 1,
	WPPS_ATTR_DEAUTH_BSSID
} wl_vendor_wips_attr_type_t;
#endif /* WL_WIPSEVT  */

/* Chipset roaming capabilities */
typedef struct wifi_roaming_capabilities {
	u32 max_blacklist_size;
	u32 max_whitelist_size;
} wifi_roaming_capabilities_t;

/* sync-up return code with wifi_hal.h in wifi_hal layer. */
typedef enum {
	WIFI_SUCCESS = 0,
	WIFI_ERROR_NONE = 0,
	WIFI_ERROR_UNKNOWN = -1,
	WIFI_ERROR_UNINITIALIZED = -2,
	WIFI_ERROR_NOT_SUPPORTED = -3,
	WIFI_ERROR_NOT_AVAILABLE = -4,              /* Not available right now, but try later */
	WIFI_ERROR_INVALID_ARGS = -5,
	WIFI_ERROR_INVALID_REQUEST_ID = -6,
	WIFI_ERROR_TIMED_OUT = -7,
	WIFI_ERROR_TOO_MANY_REQUESTS = -8,          /* Too many instances of this request */
	WIFI_ERROR_OUT_OF_MEMORY = -9,
	WIFI_ERROR_BUSY = -10
} wifi_error;

/* Capture the BRCM_VENDOR_SUBCMD_PRIV_STRINGS* here */
#define BRCM_VENDOR_SCMD_CAPA	"cap"
#define MEMDUMP_PATH_LEN	128

#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 13, 0)) || defined(WL_VENDOR_EXT_SUPPORT)
extern int wl_cfgvendor_attach(struct wiphy *wiphy, dhd_pub_t *dhd);
extern int wl_cfgvendor_detach(struct wiphy *wiphy);
extern int wl_cfgvendor_send_async_event(struct wiphy *wiphy,
                  struct net_device *dev, int event_id, const void  *data, int len);
extern int wl_cfgvendor_send_hotlist_event(struct wiphy *wiphy,
                struct net_device *dev, void  *data, int len, wl_vendor_event_t event);
#else
static INLINE int wl_cfgvendor_attach(struct wiphy *wiphy,
		dhd_pub_t *dhd) { UNUSED_PARAMETER(wiphy); UNUSED_PARAMETER(dhd); return 0; }
static INLINE int wl_cfgvendor_detach(struct wiphy *wiphy) { UNUSED_PARAMETER(wiphy); return 0; }
static INLINE int wl_cfgvendor_send_async_event(struct wiphy *wiphy,
                  struct net_device *dev, int event_id, const void  *data, int len)
{ return 0; }
static INLINE int wl_cfgvendor_send_hotlist_event(struct wiphy *wiphy,
	struct net_device *dev, void  *data, int len, wl_vendor_event_t event)
{ return 0; }
#endif /*  (LINUX_VERSION_CODE > KERNEL_VERSION(3, 13, 0)) || defined(WL_VENDOR_EXT_SUPPORT) */

#if defined(WL_SUPP_EVENT) && ((LINUX_VERSION_CODE > KERNEL_VERSION(3, 13, 0)) || \
	defined(WL_VENDOR_EXT_SUPPORT))
extern int wl_cfgvendor_send_supp_eventstring(const char *func, const char *fmt, ...);
int wl_cfgvendor_notify_supp_event_str(const char *evt_name, const char *fmt, ...);
#define SUPP_LOG_LEN 256
#define PRINT_SUPP_LOG(fmt, ...) \
	 wl_cfgvendor_send_supp_eventstring(__func__, fmt, ##__VA_ARGS__);
#define SUPP_LOG(args)  PRINT_SUPP_LOG  args;
#define SUPP_EVT_LOG(evt_name, fmt, ...) \
    wl_cfgvendor_notify_supp_event_str(evt_name, fmt, ##__VA_ARGS__);
#define SUPP_EVENT(args) SUPP_EVT_LOG args
#else
#define SUPP_LOG(x)
#define SUPP_EVENT(x)
#endif /* WL_SUPP_EVENT && (kernel > (3, 13, 0)) || WL_VENDOR_EXT_SUPPORT */

#define COMPAT_ASSIGN_VALUE(normal_structure, member, value) \
	normal_structure.member = value;

#if (defined(CONFIG_ARCH_MSM) && defined(SUPPORT_WDEV_CFG80211_VENDOR_EVENT_ALLOC)) || \
	LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
#define CFG80211_VENDOR_EVENT_ALLOC(wiphy, wdev, len, type, kflags) \
	cfg80211_vendor_event_alloc(wiphy, wdev, len, type, kflags);
#else
#define CFG80211_VENDOR_EVENT_ALLOC(wiphy, wdev, len, type, kflags) \
	cfg80211_vendor_event_alloc(wiphy, len, type, kflags);
#endif /* (defined(CONFIG_ARCH_MSM) && defined(SUPPORT_WDEV_CFG80211_VENDOR_EVENT_ALLOC)) || */
	/* LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0) */

#ifdef WL_CFGVENDOR_SEND_HANG_EVENT
void wl_cfgvendor_send_hang_event(struct net_device *dev, u16 reason);
void wl_copy_hang_info_if_falure(struct net_device *dev, u16 reason, s32 ret);
#endif /* WL_CFGVENDOR_SEND_HANG_EVENT */
#endif /* _wl_cfgvendor_h_ */
