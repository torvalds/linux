/*
** Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/os/linux/include/gl_vendor.h#1
*/

/*! \file   gl_vendor.h
	\brief  This file is for Portable Driver linux gl_vendor support.
*/

/*
** Log: gl_vendor.h
**
** 10 14 2014
** add vendor declaration
**
 *
*/

#ifndef _GL_VENDOR_H
#define _GL_VENDOR_H

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include <linux/can/netlink.h>
#include <net/netlink.h>

#include "gl_os.h"

#include "wlan_lib.h"
#include "gl_wext.h"

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
#define GOOGLE_OUI 0x001A11

typedef enum {
	/* Don't use 0 as a valid subcommand */
	ANDROID_NL80211_SUBCMD_UNSPECIFIED,

	/* Define all vendor startup commands between 0x0 and 0x0FFF */
	ANDROID_NL80211_SUBCMD_WIFI_RANGE_START = 0x0001,
	ANDROID_NL80211_SUBCMD_WIFI_RANGE_END   = 0x0FFF,

	/* Define all GScan related commands between 0x1000 and 0x10FF */
	ANDROID_NL80211_SUBCMD_GSCAN_RANGE_START = 0x1000,
	ANDROID_NL80211_SUBCMD_GSCAN_RANGE_END   = 0x10FF,

	/* Define all RTT related commands between 0x1100 and 0x11FF */
	ANDROID_NL80211_SUBCMD_RTT_RANGE_START = 0x1100,
	ANDROID_NL80211_SUBCMD_RTT_RANGE_END   = 0x11FF,

	ANDROID_NL80211_SUBCMD_LSTATS_RANGE_START = 0x1200,
	ANDROID_NL80211_SUBCMD_LSTATS_RANGE_END   = 0x12FF,

	/* Define all Logger related commands between 0x1400 and 0x14FF */
	ANDROID_NL80211_SUBCMD_DEBUG_RANGE_START = 0x1400,
	ANDROID_NL80211_SUBCMD_DEBUG_RANGE_END   = 0x14FF,

	/* Define all wifi offload related commands between 0x1600 and 0x16FF */
	ANDROID_NL80211_SUBCMD_WIFI_OFFLOAD_RANGE_START = 0x1600,
	ANDROID_NL80211_SUBCMD_WIFI_OFFLOAD_RANGE_END   = 0x16FF,

	/* This is reserved for future usage */

} ANDROID_VENDOR_SUB_COMMAND;

typedef enum {
	WIFI_SUBCMD_GET_CHANNEL_LIST = ANDROID_NL80211_SUBCMD_WIFI_RANGE_START,

	WIFI_SUBCMD_GET_FEATURE_SET,                     /* 0x0001 */
	WIFI_SUBCMD_GET_FEATURE_SET_MATRIX,              /* 0x0002 */
	WIFI_SUBCMD_SET_PNO_RANDOM_MAC_OUI,              /* 0x0003 */
	WIFI_SUBCMD_NODFS_SET,                           /* 0x0004 */
	WIFI_SUBCMD_SET_COUNTRY_CODE,                    /* 0x0005 */
	/* Add more sub commands here */

} WIFI_SUB_COMMAND;

typedef enum {
	GSCAN_SUBCMD_GET_CAPABILITIES = ANDROID_NL80211_SUBCMD_GSCAN_RANGE_START,

	GSCAN_SUBCMD_SET_CONFIG,                          /* 0x1001 */
	GSCAN_SUBCMD_SET_SCAN_CONFIG,                     /* 0x1002 */
	GSCAN_SUBCMD_ENABLE_GSCAN,                        /* 0x1003 */
	GSCAN_SUBCMD_GET_SCAN_RESULTS,                    /* 0x1004 */
	GSCAN_SUBCMD_SCAN_RESULTS,                        /* 0x1005 */

	GSCAN_SUBCMD_SET_HOTLIST,                         /* 0x1006 */

	GSCAN_SUBCMD_SET_SIGNIFICANT_CHANGE_CONFIG,       /* 0x1007 */
	GSCAN_SUBCMD_ENABLE_FULL_SCAN_RESULTS,            /* 0x1008 */
	/* Add more sub commands here */

} GSCAN_SUB_COMMAND;

typedef enum {
	RTT_SUBCMD_SET_CONFIG = ANDROID_NL80211_SUBCMD_RTT_RANGE_START,
	RTT_SUBCMD_CANCEL_CONFIG,
	RTT_SUBCMD_GETCAPABILITY,
} RTT_SUB_COMMAND;

typedef enum {
	LSTATS_SUBCMD_GET_INFO = ANDROID_NL80211_SUBCMD_LSTATS_RANGE_START,
} LSTATS_SUB_COMMAND;

typedef enum {
	GSCAN_EVENT_SIGNIFICANT_CHANGE_RESULTS,
	GSCAN_EVENT_HOTLIST_RESULTS_FOUND,
	GSCAN_EVENT_SCAN_RESULTS_AVAILABLE,
	GSCAN_EVENT_FULL_SCAN_RESULTS,
	RTT_EVENT_COMPLETE,
	GSCAN_EVENT_COMPLETE_SCAN,
	GSCAN_EVENT_HOTLIST_RESULTS_LOST
} WIFI_VENDOR_EVENT;

typedef enum {
	WIFI_ATTRIBUTE_BAND,
	WIFI_ATTRIBUTE_NUM_CHANNELS,
	WIFI_ATTRIBUTE_CHANNEL_LIST,

	WIFI_ATTRIBUTE_NUM_FEATURE_SET,
	WIFI_ATTRIBUTE_FEATURE_SET,
	WIFI_ATTRIBUTE_PNO_RANDOM_MAC_OUI,
	WIFI_ATTRIBUTE_NODFS_VALUE,
	WIFI_ATTRIBUTE_COUNTRY_CODE

} WIFI_ATTRIBUTE;

typedef enum {
	GSCAN_ATTRIBUTE_CAPABILITIES = 1,

	GSCAN_ATTRIBUTE_NUM_BUCKETS = 10,
	GSCAN_ATTRIBUTE_BASE_PERIOD,
	GSCAN_ATTRIBUTE_BUCKETS_BAND,
	GSCAN_ATTRIBUTE_BUCKET_ID,
	GSCAN_ATTRIBUTE_BUCKET_PERIOD,
	GSCAN_ATTRIBUTE_BUCKET_NUM_CHANNELS,
	GSCAN_ATTRIBUTE_BUCKET_CHANNELS,
	GSCAN_ATTRIBUTE_NUM_AP_PER_SCAN,
	GSCAN_ATTRIBUTE_REPORT_THRESHOLD,
	GSCAN_ATTRIBUTE_NUM_SCANS_TO_CACHE,

	GSCAN_ATTRIBUTE_ENABLE_FEATURE = 20,
	GSCAN_ATTRIBUTE_SCAN_RESULTS_COMPLETE,	/* indicates no more results */
	GSCAN_ATTRIBUTE_FLUSH_FEATURE,	/* Flush all the configs */
	GSCAN_ENABLE_FULL_SCAN_RESULTS,
	GSCAN_ATTRIBUTE_REPORT_EVENTS,

	GSCAN_ATTRIBUTE_NUM_OF_RESULTS = 30,
	GSCAN_ATTRIBUTE_FLUSH_RESULTS,
	GSCAN_ATTRIBUTE_SCAN_RESULTS,	/* flat array of wifi_scan_result */
	GSCAN_ATTRIBUTE_SCAN_ID,	/* indicates scan number */
	GSCAN_ATTRIBUTE_SCAN_FLAGS,	/* indicates if scan was aborted */
	GSCAN_ATTRIBUTE_AP_FLAGS,	/* flags on significant change event */

	GSCAN_ATTRIBUTE_SSID = 40,
	GSCAN_ATTRIBUTE_BSSID,
	GSCAN_ATTRIBUTE_CHANNEL,
	GSCAN_ATTRIBUTE_RSSI,
	GSCAN_ATTRIBUTE_TIMESTAMP,
	GSCAN_ATTRIBUTE_RTT,
	GSCAN_ATTRIBUTE_RTTSD,

	GSCAN_ATTRIBUTE_HOTLIST_BSSIDS = 50,
	GSCAN_ATTRIBUTE_RSSI_LOW,
	GSCAN_ATTRIBUTE_RSSI_HIGH,
	GSCAN_ATTRIBUTE_HOTLIST_ELEM,
	GSCAN_ATTRIBUTE_HOTLIST_FLUSH,

	GSCAN_ATTRIBUTE_RSSI_SAMPLE_SIZE = 60,
	GSCAN_ATTRIBUTE_LOST_AP_SAMPLE_SIZE,
	GSCAN_ATTRIBUTE_MIN_BREACHING,
	GSCAN_ATTRIBUTE_NUM_AP,
	GSCAN_ATTRIBUTE_SIGNIFICANT_CHANGE_BSSIDS,
	GSCAN_ATTRIBUTE_SIGNIFICANT_CHANGE_FLUSH

} GSCAN_ATTRIBUTE;

typedef enum {
	RTT_ATTRIBUTE_CAPABILITIES = 1,

	RTT_ATTRIBUTE_TARGET_CNT = 10,
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
	RTT_ATTRIBUTE_RESULT
} RTT_ATTRIBUTE;

typedef enum {
	LSTATS_ATTRIBUTE_STATS = 2,
} LSTATS_ATTRIBUTE;

typedef enum {
	WIFI_BAND_UNSPECIFIED,
	WIFI_BAND_BG = 1,	    /* 2.4 GHz */
	WIFI_BAND_A = 2,	    /* 5 GHz without DFS */
	WIFI_BAND_A_DFS = 4,	    /* 5 GHz DFS only */
	WIFI_BAND_A_WITH_DFS = 6,   /* 5 GHz with DFS */
	WIFI_BAND_ABG = 3,	    /* 2.4 GHz + 5 GHz; no DFS */
	WIFI_BAND_ABG_WITH_DFS = 7, /* 2.4 GHz + 5 GHz with DFS */
} WIFI_BAND;

typedef enum {
	WIFI_SCAN_BUFFER_FULL,
	WIFI_SCAN_COMPLETE,
} WIFI_SCAN_EVENT;

#define GSCAN_MAX_REPORT_THRESHOLD   1024000
#define GSCAN_MAX_CHANNELS                 8
#define GSCAN_MAX_BUCKETS                  8
#define MAX_HOTLIST_APS                   16
#define MAX_SIGNIFICANT_CHANGE_APS        16
#define PSCAN_MAX_SCAN_CACHE_SIZE         16
#define PSCAN_MAX_AP_CACHE_PER_SCAN       16
#define PSCAN_VERSION                      1

#define MAX_BUFFERED_GSCN_RESULTS 5

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
typedef UINT_64 wifi_timestamp;	/* In microseconds (us) */
typedef UINT_64 wifi_timespan;	/* In nanoseconds  (ns) */

typedef UINT_8 mac_addr[6];
typedef UINT_32 wifi_channel;	/* Indicates channel frequency in MHz */
typedef INT_32 wifi_rssi;

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

typedef struct _PARAM_WIFI_GSCAN_GET_RESULT_PARAMS {
	UINT_32 get_num;
	UINT_8 flush;
} PARAM_WIFI_GSCAN_GET_RESULT_PARAMS, *P_PARAM_WIFI_GSCAN_GET_RESULT_PARAMS;

typedef struct _PARAM_WIFI_GSCAN_ACTION_CMD_PARAMS {
	UINT_8 ucPscanAct;
	UINT_8 aucReserved[3];
} PARAM_WIFI_GSCAN_ACTION_CMD_PARAMS, *P_PARAM_WIFI_GSCAN_ACTION_CMD_PARAMS;

typedef struct _PARAM_WIFI_GSCAN_CAPABILITIES_STRUCT_T {
	UINT_32 max_scan_cache_size;	/* total space allocated for scan (in bytes) */
	UINT_32 max_scan_buckets;	/* maximum number of channel buckets */
	UINT_32 max_ap_cache_per_scan;	/* maximum number of APs that can be stored per scan */
	UINT_32 max_rssi_sample_size;	/* number of RSSI samples used for averaging RSSI */
	UINT_32 max_scan_reporting_threshold;	/* max possible report_threshold as described */
	/* in wifi_scan_cmd_params */
	UINT_32 max_hotlist_aps;	/* maximum number of entries for hotlist APs */
	UINT_32 max_significant_wifi_change_aps;	/* maximum number of entries for */
	/* significant wifi change APs */
	UINT_32 max_bssid_history_entries;	/* number of BSSID/RSSI entries that device can hold */
} PARAM_WIFI_GSCAN_CAPABILITIES_STRUCT_T, *P_PARAM_WIFI_GSCAN_CAPABILITIES_STRUCT_T;

typedef struct _PARAM_WIFI_GSCAN_CHANNEL_SPEC {
	UINT_32 channel;	/* frequency */
	UINT_32 dwellTimeMs;	/* dwell time hint */
	UINT_32 passive;	/* 0 => active, 1 => passive scan; ignored for DFS */
	/* Add channel class */
} PARAM_WIFI_GSCAN_CHANNEL_SPEC, *P_PARAM_WIFI_GSCAN_CHANNEL_SPEC;

typedef struct _PARAM_WIFI_GSCAN_BUCKET_SPEC {
	UINT_32 bucket;		/* bucket index, 0 based */
	WIFI_BAND band;		/* when UNSPECIFIED, use channel list */
	UINT_32 period;		/* desired period, in millisecond; if this is too */
	/* low, the firmware should choose to generate results as */
	/* fast as it can instead of failing the command */
	/* report_events semantics -
	 *  0 => report only when scan history is % full
	 *  1 => same as 0 + report a scan completion event after scanning this bucket
	 *  2 => same as 1 + forward scan results (beacons/probe responses + IEs) in real time to HAL
	 *  3 => same as 2 + forward scan results (beacons/probe responses + IEs) in real time to
	 supplicant as well (optional) . */
	UINT_8 report_events;

	UINT_32 num_channels;
	PARAM_WIFI_GSCAN_CHANNEL_SPEC channels[GSCAN_MAX_CHANNELS];	/* channels to scan;
									these may include DFS channels */
} PARAM_WIFI_GSCAN_BUCKET_SPEC, *P_PARAM_WIFI_GSCAN_BUCKET_SPEC;

typedef struct _PARAM_WIFI_GSCAN_CMD_PARAMS {
	UINT_32 base_period;	/* base timer period in ms */
	UINT_32 max_ap_per_scan;	/* number of APs to store in each scan in the */
	/* BSSID/RSSI history buffer (keep the highest RSSI APs) */
	UINT_32 report_threshold;	/* in %, when scan buffer is this much full, wake up AP */
	UINT_32 num_scans;
	UINT_32 num_buckets;
	PARAM_WIFI_GSCAN_BUCKET_SPEC buckets[GSCAN_MAX_BUCKETS];
} PARAM_WIFI_GSCAN_CMD_PARAMS, *P_PARAM_WIFI_GSCAN_CMD_PARAMS;

typedef struct _PARAM_WIFI_GSCAN_RESULT {
	wifi_timestamp ts;	/* time since boot (in microsecond) when the result was */
	/* retrieved */
	UINT_8 ssid[32 + 1];	/* null terminated */
	mac_addr bssid;
	wifi_channel channel;	/* channel frequency in MHz */
	wifi_rssi rssi;		/* in db */
	wifi_timespan rtt;	/* in nanoseconds */
	wifi_timespan rtt_sd;	/* standard deviation in rtt */
	UINT_16 beacon_period;	/* period advertised in the beacon */
	UINT_16 capability;	/* capabilities advertised in the beacon */
	UINT_32 ie_length;	/* size of the ie_data blob */
	UINT_8 ie_data[1];	/* blob of all the information elements found in the */
	/* beacon; this data should be a packed list of */
	/* wifi_information_element objects, one after the other. */
	/* other fields */
} PARAM_WIFI_GSCAN_RESULT, *P_PARAM_WIFI_GSCAN_RESULT;

/* Significant wifi change*/
/*typedef struct _PARAM_WIFI_CHANGE_RESULT{
	mac_addr bssid;                     // BSSID
	wifi_channel channel;               // channel frequency in MHz
	UINT_32 num_rssi;                       // number of rssi samples
	wifi_rssi rssi[8];                   // RSSI history in db
} PARAM_WIFI_CHANGE_RESULT, *P_PARAM_WIFI_CHANGE_RESULT;*/

typedef struct _PARAM_WIFI_CHANGE_RESULT {
	UINT_16 flags;
	UINT_16 channel;
	mac_addr bssid;		/* BSSID */
	INT_8 rssi[8];		/* RSSI history in db */
} PARAM_WIFI_CHANGE_RESULT, *P_PARAM_WIFI_CHANGE_RESULT;

typedef struct _PARAM_AP_THRESHOLD {
	mac_addr bssid;		/* AP BSSID */
	wifi_rssi low;		/* low threshold */
	wifi_rssi high;		/* high threshold */
	wifi_channel channel;	/* channel hint */
} PARAM_AP_THRESHOLD, *P_PARAM_AP_THRESHOLD;

typedef struct _PARAM_WIFI_BSSID_HOTLIST {
	UINT_32 lost_ap_sample_size;
	UINT_32 num_ap;		/* number of hotlist APs */
	PARAM_AP_THRESHOLD ap[MAX_HOTLIST_APS];	/* hotlist APs */
} PARAM_WIFI_BSSID_HOTLIST, *P_PARAM_WIFI_BSSID_HOTLIST;

typedef struct _PARAM_WIFI_SIGNIFICANT_CHANGE {
	UINT_16 rssi_sample_size;	/* number of samples for averaging RSSI */
	UINT_16 lost_ap_sample_size;	/* number of samples to confirm AP loss */
	UINT_16 min_breaching;	/* number of APs breaching threshold */
	UINT_16 num_ap;		/* max 64 */
	PARAM_AP_THRESHOLD ap[MAX_SIGNIFICANT_CHANGE_APS];
} PARAM_WIFI_SIGNIFICANT_CHANGE, *P_PARAM_WIFI_SIGNIFICANT_CHANGE;

/* RTT Capabilities */
typedef struct _PARAM_WIFI_RTT_CAPABILITIES {
	UINT_8 rtt_one_sided_supported;  /* if 1-sided rtt data collection is supported */
	UINT_8 rtt_ftm_supported;        /* if ftm rtt data collection is supported */
	UINT_8 lci_support;              /* if initiator supports LCI request. Applies to 2-sided RTT */
	UINT_8 lcr_support;              /* if initiator supports LCR request. Applies to 2-sided RTT */
	UINT_8 preamble_support;         /* bit mask indicates what preamble is supported by initiator */
	UINT_8 bw_support;               /* bit mask indicates what BW is supported by initiator */
} PARAM_WIFI_RTT_CAPABILITIES, *P_PARAM_WIFI_RTT_CAPABILITIES;

/* channel operating width */
typedef enum {
	WIFI_CHAN_WIDTH_20 = 0,
	WIFI_CHAN_WIDTH_40 = 1,
	WIFI_CHAN_WIDTH_80 = 2,
	WIFI_CHAN_WIDTH_160 = 3,
	WIFI_CHAN_WIDTH_80P80 = 4,
	WIFI_CHAN_WIDTH_5 = 5,
	WIFI_CHAN_WIDTH_10 = 6,
	WIFI_CHAN_WIDTH_INVALID = -1
} WIFI_CHANNEL_WIDTH;

/* channel information */
typedef struct {
	WIFI_CHANNEL_WIDTH width;	/* channel width (20, 40, 80, 80+80, 160) */
	UINT_32 center_freq;	/* primary 20 MHz channel */
	UINT_32 center_freq0;	/* center frequency (MHz) first segment */
	UINT_32 center_freq1;	/* center frequency (MHz) second segment */
} WIFI_CHANNEL_INFO;

/* channel statistics */
typedef struct {
	WIFI_CHANNEL_INFO channel;	/* channel */
	UINT_32 on_time;	/* msecs the radio is awake (32 bits number accruing over time) */
	UINT_32 cca_busy_time;	/* msecs the CCA register is busy (32 bits number accruing over time) */
} WIFI_CHANNEL_STAT;

/* radio statistics */
typedef struct {
	UINT_32 radio;		/* wifi radio (if multiple radio supported) */
	UINT_32 on_time;	/* msecs the radio is awake (32 bits number accruing over time) */
	UINT_32 tx_time;	/* msecs the radio is transmitting (32 bits number accruing over time) */
	UINT_32 rx_time;	/* msecs the radio is in active receive (32 bits number accruing over time) */
	UINT_32 on_time_scan;	/* msecs the radio is awake due to all scan (32 bits number accruing over time) */
	UINT_32 on_time_nbd;	/* msecs the radio is awake due to NAN (32 bits number accruing over time) */
	UINT_32 on_time_gscan;	/* msecs the radio is awake due to G?scan (32 bits number accruing over time) */
	UINT_32 on_time_roam_scan;	/* msecs the radio is awake due to roam?scan
					(32 bits number accruing over time) */
	UINT_32 on_time_pno_scan;	/* msecs the radio is awake due to PNO scan
					(32 bits number accruing over time) */
	UINT_32 on_time_hs20;	/* msecs the radio is awake due to HS2.0 scans and GAS exchange
					32 bits number accruing over time) */
	UINT_32 num_channels;	/* number of channels */
	WIFI_CHANNEL_STAT channels[];	/* channel statistics */
} WIFI_RADIO_STAT;

/* wifi rate */
typedef struct {
	UINT_32 preamble:3;	/* 0: OFDM, 1:CCK, 2:HT 3:VHT 4..7 reserved */
	UINT_32 nss:2;		/* 0:1x1, 1:2x2, 3:3x3, 4:4x4 */
	UINT_32 bw:3;		/* 0:20MHz, 1:40Mhz, 2:80Mhz, 3:160Mhz */
	UINT_32 rateMcsIdx:8;	/* OFDM/CCK rate code would be as per ieee std in the units of 0.5mbps */
	/* HT/VHT it would be mcs index */
	UINT_32 reserved:16;	/* reserved */
	UINT_32 bitrate;	/* units of 100 Kbps */
} WIFI_RATE;

/* per rate statistics */
typedef struct {
	WIFI_RATE rate;		/* rate information */
	UINT_32 tx_mpdu;	/* number of successfully transmitted data pkts (ACK rcvd) */
	UINT_32 rx_mpdu;	/* number of received data pkts */
	UINT_32 mpdu_lost;	/* number of data packet losses (no ACK) */
	UINT_32 retries;	/* total number of data pkt retries */
	UINT_32 retries_short;	/* number of short data pkt retries */
	UINT_32 retries_long;	/* number of long data pkt retries */
} WIFI_RATE_STAT;

/*wifi_interface_link_layer_info*/
typedef enum {
	WIFI_DISCONNECTED = 0,
	WIFI_AUTHENTICATING = 1,
	WIFI_ASSOCIATING = 2,
	WIFI_ASSOCIATED = 3,
	WIFI_EAPOL_STARTED = 4,	/* if done by firmware/driver */
	WIFI_EAPOL_COMPLETED = 5,	/* if done by firmware/driver */
} WIFI_CONNECTION_STATE;

typedef enum {
	WIFI_ROAMING_IDLE = 0,
	WIFI_ROAMING_ACTIVE = 1,
} WIFI_ROAM_STATE;

typedef enum {
	WIFI_INTERFACE_STA = 0,
	WIFI_INTERFACE_SOFTAP = 1,
	WIFI_INTERFACE_IBSS = 2,
	WIFI_INTERFACE_P2P_CLIENT = 3,
	WIFI_INTERFACE_P2P_GO = 4,
	WIFI_INTERFACE_NAN = 5,
	WIFI_INTERFACE_MESH = 6,
	WIFI_INTERFACE_UNKNOWN = -1
} WIFI_INTERFACE_MODE;

typedef struct {
	WIFI_INTERFACE_MODE mode;	/* interface mode */
	u8 mac_addr[6];		/* interface mac address (self) */
	WIFI_CONNECTION_STATE state;	/* connection state (valid for STA, CLI only) */
	WIFI_ROAM_STATE roaming;	/* roaming state */
	u32 capabilities;	/* WIFI_CAPABILITY_XXX (self) */
	u8 ssid[33];		/* null terminated SSID */
	u8 bssid[6];		/* bssid */
	u8 ap_country_str[3];	/* country string advertised by AP */
	u8 country_str[3];	/* country string for this association */
} WIFI_INTERFACE_LINK_LAYER_INFO;

/* access categories */
typedef enum {
	WIFI_AC_VO = 0,
	WIFI_AC_VI = 1,
	WIFI_AC_BE = 2,
	WIFI_AC_BK = 3,
	WIFI_AC_MAX = 4,
} WIFI_TRAFFIC_AC;

/* wifi peer type */
typedef enum {
	WIFI_PEER_STA,
	WIFI_PEER_AP,
	WIFI_PEER_P2P_GO,
	WIFI_PEER_P2P_CLIENT,
	WIFI_PEER_NAN,
	WIFI_PEER_TDLS,
	WIFI_PEER_INVALID,
} WIFI_PEER_TYPE;

/* per peer statistics */
typedef struct {
	WIFI_PEER_TYPE type;	/* peer type (AP, TDLS, GO etc.) */
	UINT_8 peer_mac_address[6];	/* mac address */
	UINT_32 capabilities;	/* peer WIFI_CAPABILITY_XXX */
	UINT_32 num_rate;	/* number of rates */
	WIFI_RATE_STAT rate_stats[];	/* per rate statistics, number of entries  = num_rate */
} WIFI_PEER_INFO;

/* per access category statistics */
typedef struct {
	WIFI_TRAFFIC_AC ac;	/* access category (VI, VO, BE, BK) */
	UINT_32 tx_mpdu;	/* number of successfully transmitted unicast data pkts (ACK rcvd) */
	UINT_32 rx_mpdu;	/* number of received unicast mpdus */
	UINT_32 tx_mcast;	/* number of successfully transmitted multicast data packets */
	/* STA case: implies ACK received from AP for the unicast packet in which mcast pkt was sent */
	UINT_32 rx_mcast;	/* number of received multicast data packets */
	UINT_32 rx_ampdu;	/* number of received unicast a-mpdus */
	UINT_32 tx_ampdu;	/* number of transmitted unicast a-mpdus */
	UINT_32 mpdu_lost;	/* number of data pkt losses (no ACK) */
	UINT_32 retries;	/* total number of data pkt retries */
	UINT_32 retries_short;	/* number of short data pkt retries */
	UINT_32 retries_long;	/* number of long data pkt retries */
	UINT_32 contention_time_min;	/* data pkt min contention time (usecs) */
	UINT_32 contention_time_max;	/* data pkt max contention time (usecs) */
	UINT_32 contention_time_avg;	/* data pkt avg contention time (usecs) */
	UINT_32 contention_num_samples;	/* num of data pkts used for contention statistics */
} WIFI_WMM_AC_STAT;

/* interface statistics */
typedef struct {
	/* wifi_interface_handle iface;          // wifi interface */
	WIFI_INTERFACE_LINK_LAYER_INFO info;	/* current state of the interface */
	UINT_32 beacon_rx;	/* access point beacon received count from connected AP */
	UINT_32 mgmt_rx;	/* access point mgmt frames received count from connected AP (including Beacon) */
	UINT_32 mgmt_action_rx;	/* action frames received count */
	UINT_32 mgmt_action_tx;	/* action frames transmit count */
	wifi_rssi rssi_mgmt;	/* access Point Beacon and Management frames RSSI (averaged) */
	wifi_rssi rssi_data;	/* access Point Data Frames RSSI (averaged) from connected AP */
	wifi_rssi rssi_ack;	/* access Point ACK RSSI (averaged) from connected AP */
	WIFI_WMM_AC_STAT ac[WIFI_AC_MAX];	/* per ac data packet statistics */
	UINT_32 num_peers;	/* number of peers */
	WIFI_PEER_INFO peer_info[];	/* per peer statistics */
} WIFI_IFACE_STAT;

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
int mtk_cfg80211_vendor_get_channel_list(struct wiphy *wiphy, struct wireless_dev *wdev,
					 const void *data, int data_len);

int mtk_cfg80211_vendor_set_country_code(struct wiphy *wiphy, struct wireless_dev *wdev,
					 const void *data, int data_len);

int mtk_cfg80211_vendor_get_gscan_capabilities(struct wiphy *wiphy, struct wireless_dev *wdev,
					 const void *data, int data_len);

int mtk_cfg80211_vendor_set_config(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int data_len);

int mtk_cfg80211_vendor_set_scan_config(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int data_len);

int mtk_cfg80211_vendor_set_significant_change(struct wiphy *wiphy, struct wireless_dev *wdev,
					 const void *data, int data_len);

int mtk_cfg80211_vendor_set_hotlist(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int data_len);

int mtk_cfg80211_vendor_enable_scan(struct wiphy *wiphy, struct wireless_dev *wdev, const void *data, int data_len);

int mtk_cfg80211_vendor_enable_full_scan_results(struct wiphy *wiphy, struct wireless_dev *wdev,
					 const void *data, int data_len);

int mtk_cfg80211_vendor_get_scan_results(struct wiphy *wiphy, struct wireless_dev *wdev,
					 const void *data, int data_len);

int mtk_cfg80211_vendor_get_rtt_capabilities(struct wiphy *wiphy, struct wireless_dev *wdev,
					 const void *data, int data_len);

int mtk_cfg80211_vendor_llstats_get_info(struct wiphy *wiphy, struct wireless_dev *wdev,
					 const void *data, int data_len);

int mtk_cfg80211_vendor_event_complete_scan(struct wiphy *wiphy, struct wireless_dev *wdev, WIFI_SCAN_EVENT complete);

int mtk_cfg80211_vendor_event_scan_results_available(struct wiphy *wiphy, struct wireless_dev *wdev, UINT_32 num);

int mtk_cfg80211_vendor_event_full_scan_results(struct wiphy *wiphy, struct wireless_dev *wdev,
					 P_PARAM_WIFI_GSCAN_RESULT pdata, UINT_32 data_len);

int mtk_cfg80211_vendor_event_significant_change_results(struct wiphy *wiphy, struct wireless_dev *wdev,
					 P_PARAM_WIFI_CHANGE_RESULT pdata, UINT_32 data_len);

int mtk_cfg80211_vendor_event_hotlist_ap_found(struct wiphy *wiphy, struct wireless_dev *wdev,
					 P_PARAM_WIFI_GSCAN_RESULT pdata, UINT_32 data_len);

int mtk_cfg80211_vendor_event_hotlist_ap_lost(struct wiphy *wiphy, struct wireless_dev *wdev,
					 P_PARAM_WIFI_GSCAN_RESULT pdata, UINT_32 data_len);

#endif /* _GL_VENDOR_H */
