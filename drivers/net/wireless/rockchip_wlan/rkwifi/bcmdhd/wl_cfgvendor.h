/*
 * Linux cfg80211 Vendor Extension Code
 *
 * Copyright (C) 1999-2016, Broadcom Corporation
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
 * $Id: wl_cfgvendor.h 605796 2015-12-11 13:45:36Z $
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

#define SCAN_RESULTS_COMPLETE_FLAG_LEN       ATTRIBUTE_U32_LEN
#define SCAN_INDEX_HDR_LEN                   (NLA_HDRLEN)
#define SCAN_ID_HDR_LEN                      ATTRIBUTE_U32_LEN
#define SCAN_FLAGS_HDR_LEN                   ATTRIBUTE_U32_LEN
#define GSCAN_NUM_RESULTS_HDR_LEN            ATTRIBUTE_U32_LEN
#define GSCAN_RESULTS_HDR_LEN                (NLA_HDRLEN)
#define GSCAN_BATCH_RESULT_HDR_LEN  (SCAN_INDEX_HDR_LEN + SCAN_ID_HDR_LEN + \
									SCAN_FLAGS_HDR_LEN + \
							        GSCAN_NUM_RESULTS_HDR_LEN + \
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

typedef enum {
	/* don't use 0 as a valid subcommand */
	VENDOR_NL80211_SUBCMD_UNSPECIFIED,

	/* define all vendor startup commands between 0x0 and 0x0FFF */
	VENDOR_NL80211_SUBCMD_RANGE_START = 0x0001,
	VENDOR_NL80211_SUBCMD_RANGE_END   = 0x0FFF,

	/* define all GScan related commands between 0x1000 and 0x10FF */
	ANDROID_NL80211_SUBCMD_GSCAN_RANGE_START = 0x1000,
	ANDROID_NL80211_SUBCMD_GSCAN_RANGE_END   = 0x10FF,

	/* define all NearbyDiscovery related commands between 0x1100 and 0x11FF */
	ANDROID_NL80211_SUBCMD_NBD_RANGE_START = 0x1100,
	ANDROID_NL80211_SUBCMD_NBD_RANGE_END   = 0x11FF,

	/* define all RTT related commands between 0x1100 and 0x11FF */
	ANDROID_NL80211_SUBCMD_RTT_RANGE_START = 0x1100,
	ANDROID_NL80211_SUBCMD_RTT_RANGE_END   = 0x11FF,

	ANDROID_NL80211_SUBCMD_LSTATS_RANGE_START = 0x1200,
	ANDROID_NL80211_SUBCMD_LSTATS_RANGE_END   = 0x12FF,

	ANDROID_NL80211_SUBCMD_TDLS_RANGE_START = 0x1300,
	ANDROID_NL80211_SUBCMD_TDLS_RANGE_END	= 0x13FF,
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
	ANDR_WIFI_PNO_RANDOM_MAC_OUI,
	ANDR_WIFI_NODFS_CHANNELS,
	RTT_SUBCMD_SET_CONFIG = ANDROID_NL80211_SUBCMD_RTT_RANGE_START,
	RTT_SUBCMD_CANCEL_CONFIG,
	RTT_SUBCMD_GETCAPABILITY,

	LSTATS_SUBCMD_GET_INFO = ANDROID_NL80211_SUBCMD_LSTATS_RANGE_START,
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

    /* remaining reserved for additional attributes */
    GSCAN_ATTRIBUTE_RSSI_SAMPLE_SIZE = GSCAN_ATTR_SET6,
    GSCAN_ATTRIBUTE_LOST_AP_SAMPLE_SIZE,
    GSCAN_ATTRIBUTE_MIN_BREACHING,
    GSCAN_ATTRIBUTE_SIGNIFICANT_CHANGE_BSSIDS,
    GSCAN_ATTRIBUTE_SIGNIFICANT_CHANGE_FLUSH,
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
	RTT_ATTRIBUTE_TARGET_MODE,
	RTT_ATTRIBUTE_TARGET_INTERVAL,
	RTT_ATTRIBUTE_TARGET_NUM_MEASUREMENT,
	RTT_ATTRIBUTE_TARGET_NUM_PKT,
	RTT_ATTRIBUTE_TARGET_NUM_RETRY
};

typedef enum wl_vendor_event {
	BRCM_VENDOR_EVENT_UNSPEC,
	BRCM_VENDOR_EVENT_PRIV_STR,
	GOOGLE_GSCAN_SIGNIFICANT_EVENT,
	GOOGLE_GSCAN_GEOFENCE_FOUND_EVENT,
	GOOGLE_GSCAN_BATCH_SCAN_EVENT,
	GOOGLE_SCAN_FULL_RESULTS_EVENT,
	GOOGLE_RTT_COMPLETE_EVENT,
	GOOGLE_SCAN_COMPLETE_EVENT,
	GOOGLE_GSCAN_GEOFENCE_LOST_EVENT,
	BRCM_VENDOR_EVENT_IDSUP_STATUS
} wl_vendor_event_t;

enum andr_wifi_attr {
	ANDR_WIFI_ATTRIBUTE_NUM_FEATURE_SET,
	ANDR_WIFI_ATTRIBUTE_FEATURE_SET,
	ANDR_WIFI_ATTRIBUTE_PNO_RANDOM_MAC_OUI,
	ANDR_WIFI_ATTRIBUTE_NODFS_SET
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
	WIFI_SCAN_BUFFER_FULL,
	WIFI_SCAN_COMPLETE
} gscan_complete_event_t;

/* Capture the BRCM_VENDOR_SUBCMD_PRIV_STRINGS* here */
#define BRCM_VENDOR_SCMD_CAPA	"cap"

#if defined(WL_VENDOR_EXT_SUPPORT) || defined(CONFIG_BCMDHD_VENDOR_EXT)
extern int wl_cfgvendor_attach(struct wiphy *wiphy);
extern int wl_cfgvendor_detach(struct wiphy *wiphy);
extern int wl_cfgvendor_send_async_event(struct wiphy *wiphy,
                  struct net_device *dev, int event_id, const void  *data, int len);
extern int wl_cfgvendor_send_hotlist_event(struct wiphy *wiphy,
                struct net_device *dev, void  *data, int len, wl_vendor_event_t event);
#else
static INLINE int cfgvendor_attach(struct wiphy *wiphy) { return 0; }
static INLINE int cfgvendor_detach(struct wiphy *wiphy) { return 0; }
#endif /* defined(WL_VENDOR_EXT_SUPPORT) */

#endif /* _wl_cfgvendor_h_ */
