/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/

#ifndef _RTW_CFGVENDOR_H_
#define _RTW_CFGVENDOR_H_

#define OUI_GOOGLE  0x001A11
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

    /* define all Logger related commands between 0x1400 and 0x14FF */
    ANDROID_NL80211_SUBCMD_DEBUG_RANGE_START = 0x1400,
    ANDROID_NL80211_SUBCMD_DEBUG_RANGE_END   = 0x14FF,

    /* define all wifi offload related commands between 0x1600 and 0x16FF */
    ANDROID_NL80211_SUBCMD_WIFI_OFFLOAD_RANGE_START = 0x1600,
    ANDROID_NL80211_SUBCMD_WIFI_OFFLOAD_RANGE_END   = 0x16FF,

    /* define all NAN related commands between 0x1700 and 0x17FF */
    ANDROID_NL80211_SUBCMD_NAN_RANGE_START = 0x1700,
    ANDROID_NL80211_SUBCMD_NAN_RANGE_END   = 0x17FF,

    /* define all Android Packet Filter related commands between 0x1800 and 0x18FF */
    ANDROID_NL80211_SUBCMD_PKT_FILTER_RANGE_START = 0x1800,
    ANDROID_NL80211_SUBCMD_PKT_FILTER_RANGE_END   = 0x18FF,

	/* This is reserved for future usage */

} ANDROID_VENDOR_SUB_COMMAND;

enum rtw_vendor_subcmd {
	GSCAN_SUBCMD_GET_CAPABILITIES = ANDROID_NL80211_SUBCMD_GSCAN_RANGE_START,

    GSCAN_SUBCMD_SET_CONFIG,                            /* 0x1001 */

    GSCAN_SUBCMD_SET_SCAN_CONFIG,                       /* 0x1002 */
    GSCAN_SUBCMD_ENABLE_GSCAN,                          /* 0x1003 */
    GSCAN_SUBCMD_GET_SCAN_RESULTS,                      /* 0x1004 */
    GSCAN_SUBCMD_SCAN_RESULTS,                          /* 0x1005 */

    GSCAN_SUBCMD_SET_HOTLIST,                           /* 0x1006 */

    GSCAN_SUBCMD_SET_SIGNIFICANT_CHANGE_CONFIG,         /* 0x1007 */
    GSCAN_SUBCMD_ENABLE_FULL_SCAN_RESULTS,              /* 0x1008 */
    GSCAN_SUBCMD_GET_CHANNEL_LIST,                       /* 0x1009 */

    WIFI_SUBCMD_GET_FEATURE_SET,                         /* 0x100A */
    WIFI_SUBCMD_GET_FEATURE_SET_MATRIX,                  /* 0x100B */
    WIFI_SUBCMD_SET_PNO_RANDOM_MAC_OUI,                  /* 0x100C */
    WIFI_SUBCMD_NODFS_SET,                               /* 0x100D */
    WIFI_SUBCMD_SET_COUNTRY_CODE,                             /* 0x100E */
    /* Add more sub commands here */
    GSCAN_SUBCMD_SET_EPNO_SSID,                          /* 0x100F */

    WIFI_SUBCMD_SET_SSID_WHITE_LIST,                    /* 0x1010 */
    WIFI_SUBCMD_SET_ROAM_PARAMS,                        /* 0x1011 */
    WIFI_SUBCMD_ENABLE_LAZY_ROAM,                       /* 0x1012 */
    WIFI_SUBCMD_SET_BSSID_PREF,                         /* 0x1013 */
    WIFI_SUBCMD_SET_BSSID_BLACKLIST,                     /* 0x1014 */

    GSCAN_SUBCMD_ANQPO_CONFIG,                          /* 0x1015 */
    WIFI_SUBCMD_SET_RSSI_MONITOR,                       /* 0x1016 */
    WIFI_SUBCMD_CONFIG_ND_OFFLOAD,                      /* 0x1017 */
    /* Add more sub commands here */

    GSCAN_SUBCMD_MAX,

	RTT_SUBCMD_SET_CONFIG = ANDROID_NL80211_SUBCMD_RTT_RANGE_START,
	RTT_SUBCMD_CANCEL_CONFIG,
	RTT_SUBCMD_GETCAPABILITY,

    APF_SUBCMD_GET_CAPABILITIES = ANDROID_NL80211_SUBCMD_PKT_FILTER_RANGE_START,
    APF_SUBCMD_SET_FILTER,
};

enum gscan_attributes {
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
	GSCAN_ATTRIBUTE_BAND = GSCAN_ATTRIBUTE_BUCKETS_BAND,

	GSCAN_ATTRIBUTE_ENABLE_FEATURE = 20,
	GSCAN_ATTRIBUTE_SCAN_RESULTS_COMPLETE,
	GSCAN_ATTRIBUTE_FLUSH_FEATURE,
	GSCAN_ATTRIBUTE_ENABLE_FULL_SCAN_RESULTS,
	GSCAN_ATTRIBUTE_REPORT_EVENTS,
	/* remaining reserved for additional attributes */
	GSCAN_ATTRIBUTE_NUM_OF_RESULTS = 30,
	GSCAN_ATTRIBUTE_FLUSH_RESULTS,
	GSCAN_ATTRIBUTE_SCAN_RESULTS,                       /* flat array of wifi_scan_result */
	GSCAN_ATTRIBUTE_SCAN_ID,                            /* indicates scan number */
	GSCAN_ATTRIBUTE_SCAN_FLAGS,                         /* indicates if scan was aborted */
	GSCAN_ATTRIBUTE_AP_FLAGS,                           /* flags on significant change event */
	GSCAN_ATTRIBUTE_NUM_CHANNELS,
	GSCAN_ATTRIBUTE_CHANNEL_LIST,

	/* remaining reserved for additional attributes */

	GSCAN_ATTRIBUTE_SSID = 40,
	GSCAN_ATTRIBUTE_BSSID,
	GSCAN_ATTRIBUTE_CHANNEL,
	GSCAN_ATTRIBUTE_RSSI,
	GSCAN_ATTRIBUTE_TIMESTAMP,
	GSCAN_ATTRIBUTE_RTT,
	GSCAN_ATTRIBUTE_RTTSD,

	/* remaining reserved for additional attributes */

	GSCAN_ATTRIBUTE_HOTLIST_BSSIDS = 50,
	GSCAN_ATTRIBUTE_RSSI_LOW,
	GSCAN_ATTRIBUTE_RSSI_HIGH,
	GSCAN_ATTRIBUTE_HOSTLIST_BSSID_ELEM,
	GSCAN_ATTRIBUTE_HOTLIST_FLUSH,

	/* remaining reserved for additional attributes */
	GSCAN_ATTRIBUTE_RSSI_SAMPLE_SIZE = 60,
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

typedef enum rtw_vendor_event {
    RTK_RESERVED1,
    RTK_RESERVED2,
    GSCAN_EVENT_SIGNIFICANT_CHANGE_RESULTS ,
    GSCAN_EVENT_HOTLIST_RESULTS_FOUND,
    GSCAN_EVENT_SCAN_RESULTS_AVAILABLE,
    GSCAN_EVENT_FULL_SCAN_RESULTS,
    RTT_EVENT_COMPLETE,
    GSCAN_EVENT_COMPLETE_SCAN,
    GSCAN_EVENT_HOTLIST_RESULTS_LOST,
    GSCAN_EVENT_EPNO_EVENT,
    GOOGLE_DEBUG_RING_EVENT,
    GOOGLE_DEBUG_MEM_DUMP_EVENT,
    GSCAN_EVENT_ANQPO_HOTSPOT_MATCH,
    GOOGLE_RSSI_MONITOR_EVENT
} rtw_vendor_event_t;

enum andr_wifi_feature_set_attr {
	ANDR_WIFI_ATTRIBUTE_NUM_FEATURE_SET,
	ANDR_WIFI_ATTRIBUTE_FEATURE_SET
};

typedef enum rtw_vendor_gscan_attribute {
	ATTR_START_GSCAN,
	ATTR_STOP_GSCAN,
	ATTR_SET_SCAN_BATCH_CFG_ID, /* set batch scan params */
	ATTR_SET_SCAN_GEOFENCE_CFG_ID, /* set list of bssids to track */
	ATTR_SET_SCAN_SIGNIFICANT_CFG_ID, /* set list of bssids, rssi threshold etc.. */
	ATTR_SET_SCAN_CFG_ID, /* set common scan config params here */
	ATTR_GET_GSCAN_CAPABILITIES_ID,
	/* Add more sub commands here */
	ATTR_GSCAN_MAX
} rtw_vendor_gscan_attribute_t;

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
/* wifi_hal.h */
/* WiFi Common definitions */
typedef unsigned char byte;
typedef int wifi_request_id;
typedef int wifi_channel;                       // indicates channel frequency in MHz
typedef int wifi_rssi;
typedef byte mac_addr[6];
typedef byte oui[3];
typedef int64_t wifi_timestamp;                 // In microseconds (us)
typedef int64_t wifi_timespan;                  // In picoseconds  (ps)

struct wifi_info;
struct wifi_interface_info;
typedef struct wifi_info *wifi_handle;
typedef struct wifi_interface_info *wifi_interface_handle;

/* channel operating width */
typedef enum {
    WIFI_CHAN_WIDTH_20    = 0,
    WIFI_CHAN_WIDTH_40    = 1,
    WIFI_CHAN_WIDTH_80    = 2,
    WIFI_CHAN_WIDTH_160   = 3,
    WIFI_CHAN_WIDTH_80P80 = 4,
    WIFI_CHAN_WIDTH_5     = 5,
    WIFI_CHAN_WIDTH_10    = 6,
    WIFI_CHAN_WIDTH_INVALID = -1
} wifi_channel_width;

typedef int wifi_radio;

typedef struct {
    wifi_channel_width width;
    int center_frequency0;
    int center_frequency1;
    int primary_frequency;
} wifi_channel_spec;

typedef enum {
    WIFI_SUCCESS = 0,
    WIFI_ERROR_NONE = 0,
    WIFI_ERROR_UNKNOWN = -1,
    WIFI_ERROR_UNINITIALIZED = -2,
    WIFI_ERROR_NOT_SUPPORTED = -3,
    WIFI_ERROR_NOT_AVAILABLE = -4,              // Not available right now, but try later
    WIFI_ERROR_INVALID_ARGS = -5,
    WIFI_ERROR_INVALID_REQUEST_ID = -6,
    WIFI_ERROR_TIMED_OUT = -7,
    WIFI_ERROR_TOO_MANY_REQUESTS = -8,          // Too many instances of this request
    WIFI_ERROR_OUT_OF_MEMORY = -9,
    WIFI_ERROR_BUSY = -10,
} wifi_error;

#ifdef CONFIG_RTW_CFGVEDNOR_LLSTATS
#define STATS_MAJOR_VERSION      1
#define STATS_MINOR_VERSION      0
#define STATS_MICRO_VERSION      0

typedef enum {
    WIFI_DISCONNECTED = 0,
    WIFI_AUTHENTICATING = 1,
    WIFI_ASSOCIATING = 2,
    WIFI_ASSOCIATED = 3,
    WIFI_EAPOL_STARTED = 4,   // if done by firmware/driver
    WIFI_EAPOL_COMPLETED = 5, // if done by firmware/driver
} wifi_connection_state;

typedef enum {
    WIFI_ROAMING_IDLE = 0,
    WIFI_ROAMING_ACTIVE = 1,
} wifi_roam_state;

typedef enum {
    WIFI_INTERFACE_STA = 0,
    WIFI_INTERFACE_SOFTAP = 1,
    WIFI_INTERFACE_IBSS = 2,
    WIFI_INTERFACE_P2P_CLIENT = 3,
    WIFI_INTERFACE_P2P_GO = 4,
    WIFI_INTERFACE_NAN = 5,
    WIFI_INTERFACE_MESH = 6,
    WIFI_INTERFACE_UNKNOWN = -1
 } wifi_interface_mode;

#define WIFI_CAPABILITY_QOS          0x00000001     // set for QOS association
#define WIFI_CAPABILITY_PROTECTED    0x00000002     // set for protected association (802.11 beacon frame control protected bit set)
#define WIFI_CAPABILITY_INTERWORKING 0x00000004     // set if 802.11 Extended Capabilities element interworking bit is set
#define WIFI_CAPABILITY_HS20         0x00000008     // set for HS20 association
#define WIFI_CAPABILITY_SSID_UTF8    0x00000010     // set is 802.11 Extended Capabilities element UTF-8 SSID bit is set
#define WIFI_CAPABILITY_COUNTRY      0x00000020     // set is 802.11 Country Element is present

typedef struct {
   wifi_interface_mode mode;     // interface mode
   u8 mac_addr[6];               // interface mac address (self)
   wifi_connection_state state;  // connection state (valid for STA, CLI only)
   wifi_roam_state roaming;      // roaming state
   u32 capabilities;             // WIFI_CAPABILITY_XXX (self)
   u8 ssid[33];                  // null terminated SSID
   u8 bssid[6];                  // bssid
   u8 ap_country_str[3];         // country string advertised by AP
   u8 country_str[3];            // country string for this association
} wifi_interface_link_layer_info;

/* channel information */
typedef struct {
   wifi_channel_width width;   // channel width (20, 40, 80, 80+80, 160)
   wifi_channel center_freq;   // primary 20 MHz channel
   wifi_channel center_freq0;  // center frequency (MHz) first segment
   wifi_channel center_freq1;  // center frequency (MHz) second segment
} wifi_channel_info;

/* wifi rate */
typedef struct {
   u32 preamble   :3;   // 0: OFDM, 1:CCK, 2:HT 3:VHT 4..7 reserved
   u32 nss        :2;   // 0:1x1, 1:2x2, 3:3x3, 4:4x4
   u32 bw         :3;   // 0:20MHz, 1:40Mhz, 2:80Mhz, 3:160Mhz
   u32 rateMcsIdx :8;   // OFDM/CCK rate code would be as per ieee std in the units of 0.5mbps
                        // HT/VHT it would be mcs index
   u32 reserved  :16;   // reserved
   u32 bitrate;         // units of 100 Kbps
} wifi_rate;

/* channel statistics */
typedef struct {
   wifi_channel_info channel;  // channel
   u32 on_time;                // msecs the radio is awake (32 bits number accruing over time)
   u32 cca_busy_time;          // msecs the CCA register is busy (32 bits number accruing over time)
} wifi_channel_stat;

// Max number of tx power levels. The actual number vary per device and is specified by |num_tx_levels|
#define RADIO_STAT_MAX_TX_LEVELS 256

/* radio statistics */
typedef struct {
   wifi_radio radio;                      // wifi radio (if multiple radio supported)
   u32 on_time;                           // msecs the radio is awake (32 bits number accruing over time)
   u32 tx_time;                           // msecs the radio is transmitting (32 bits number accruing over time)
   u32 num_tx_levels;                     // number of radio transmit power levels
   u32* tx_time_per_levels;               // pointer to an array of radio transmit per power levels in
                                          // msecs accured over time
   u32 rx_time;                           // msecs the radio is in active receive (32 bits number accruing over time)
   u32 on_time_scan;                      // msecs the radio is awake due to all scan (32 bits number accruing over time)
   u32 on_time_nbd;                       // msecs the radio is awake due to NAN (32 bits number accruing over time)
   u32 on_time_gscan;                     // msecs the radio is awake due to G?scan (32 bits number accruing over time)
   u32 on_time_roam_scan;                 // msecs the radio is awake due to roam?scan (32 bits number accruing over time)
   u32 on_time_pno_scan;                  // msecs the radio is awake due to PNO scan (32 bits number accruing over time)
   u32 on_time_hs20;                      // msecs the radio is awake due to HS2.0 scans and GAS exchange (32 bits number accruing over time)
   u32 num_channels;                      // number of channels
   wifi_channel_stat channels[];          // channel statistics
} wifi_radio_stat;

/**
 * Packet statistics reporting by firmware is performed on MPDU basi (i.e. counters increase by 1 for each MPDU)
 * As well, "data packet" in associated comments, shall be interpreted as 802.11 data packet,
 * that is, 802.11 frame control subtype == 2 and excluding management and control frames.
 *
 * As an example, in the case of transmission of an MSDU fragmented in 16 MPDUs which are transmitted
 * OTA in a 16 units long a-mpdu, for which a block ack is received with 5 bits set:
 *          tx_mpdu : shall increase by 5
 *          retries : shall increase by 16
 *          tx_ampdu : shall increase by 1
 * data packet counters shall not increase regardless of the number of BAR potentially sent by device for this a-mpdu
 * data packet counters shall not increase regardless of the number of BA received by device for this a-mpdu
 *
 * For each subsequent retransmission of the 11 remaining non ACK'ed mpdus
 * (regardless of the fact that they are transmitted in a-mpdu or not)
 *          retries : shall increase by 1
 *
 * If no subsequent BA or ACK are received from AP, until packet lifetime expires for those 11 packet that were not ACK'ed
 *          mpdu_lost : shall increase by 11
 */

/* per rate statistics */
typedef struct {
   wifi_rate rate;     // rate information
   u32 tx_mpdu;        // number of successfully transmitted data pkts (ACK rcvd)
   u32 rx_mpdu;        // number of received data pkts
   u32 mpdu_lost;      // number of data packet losses (no ACK)
   u32 retries;        // total number of data pkt retries
   u32 retries_short;  // number of short data pkt retries
   u32 retries_long;   // number of long data pkt retries
} wifi_rate_stat;

/* access categories */
typedef enum {
   WIFI_AC_VO  = 0,
   WIFI_AC_VI  = 1,
   WIFI_AC_BE  = 2,
   WIFI_AC_BK  = 3,
   WIFI_AC_MAX = 4,
} wifi_traffic_ac;

/* wifi peer type */
typedef enum
{
   WIFI_PEER_STA,
   WIFI_PEER_AP,
   WIFI_PEER_P2P_GO,
   WIFI_PEER_P2P_CLIENT,
   WIFI_PEER_NAN,
   WIFI_PEER_TDLS,
   WIFI_PEER_INVALID,
} wifi_peer_type;

/* per peer statistics */
typedef struct {
   wifi_peer_type type;           // peer type (AP, TDLS, GO etc.)
   u8 peer_mac_address[6];        // mac address
   u32 capabilities;              // peer WIFI_CAPABILITY_XXX
   u32 num_rate;                  // number of rates
   wifi_rate_stat rate_stats[];   // per rate statistics, number of entries  = num_rate
} wifi_peer_info;

/* Per access category statistics */
typedef struct {
   wifi_traffic_ac ac;             // access category (VI, VO, BE, BK)
   u32 tx_mpdu;                    // number of successfully transmitted unicast data pkts (ACK rcvd)
   u32 rx_mpdu;                    // number of received unicast data packets
   u32 tx_mcast;                   // number of succesfully transmitted multicast data packets
                                   // STA case: implies ACK received from AP for the unicast packet in which mcast pkt was sent
   u32 rx_mcast;                   // number of received multicast data packets
   u32 rx_ampdu;                   // number of received unicast a-mpdus; support of this counter is optional
   u32 tx_ampdu;                   // number of transmitted unicast a-mpdus; support of this counter is optional
   u32 mpdu_lost;                  // number of data pkt losses (no ACK)
   u32 retries;                    // total number of data pkt retries
   u32 retries_short;              // number of short data pkt retries
   u32 retries_long;               // number of long data pkt retries
   u32 contention_time_min;        // data pkt min contention time (usecs)
   u32 contention_time_max;        // data pkt max contention time (usecs)
   u32 contention_time_avg;        // data pkt avg contention time (usecs)
   u32 contention_num_samples;     // num of data pkts used for contention statistics
} wifi_wmm_ac_stat;

/* interface statistics */
typedef struct {
   wifi_interface_handle iface;          // wifi interface
   wifi_interface_link_layer_info info;  // current state of the interface
   u32 beacon_rx;                        // access point beacon received count from connected AP
   u64 average_tsf_offset;               // average beacon offset encountered (beacon_TSF - TBTT)
                                         // The average_tsf_offset field is used so as to calculate the
                                         // typical beacon contention time on the channel as well may be
                                         // used to debug beacon synchronization and related power consumption issue
   u32 leaky_ap_detected;                // indicate that this AP typically leaks packets beyond the driver guard time.
   u32 leaky_ap_avg_num_frames_leaked;  // average number of frame leaked by AP after frame with PM bit set was ACK'ed by AP
   u32 leaky_ap_guard_time;              // guard time currently in force (when implementing IEEE power management based on
                                         // frame control PM bit), How long driver waits before shutting down the radio and
                                         // after receiving an ACK for a data frame with PM bit set)
   u32 mgmt_rx;                          // access point mgmt frames received count from connected AP (including Beacon)
   u32 mgmt_action_rx;                   // action frames received count
   u32 mgmt_action_tx;                   // action frames transmit count
   wifi_rssi rssi_mgmt;                  // access Point Beacon and Management frames RSSI (averaged)
   wifi_rssi rssi_data;                  // access Point Data Frames RSSI (averaged) from connected AP
   wifi_rssi rssi_ack;                   // access Point ACK RSSI (averaged) from connected AP
   wifi_wmm_ac_stat ac[WIFI_AC_MAX];     // per ac data packet statistics
   u32 num_peers;                        // number of peers
   wifi_peer_info peer_info[];           // per peer statistics
} wifi_iface_stat;

/* configuration params */
typedef struct {
   u32 mpdu_size_threshold;             // threshold to classify the pkts as short or long
                                        // packet size < mpdu_size_threshold => short
   u32 aggressive_statistics_gathering; // set for field debug mode. Driver should collect all statistics regardless of performance impact.
} wifi_link_layer_params;

/* callback for reporting link layer stats */
typedef struct {
  void (*on_link_stats_results) (wifi_request_id id, wifi_iface_stat *iface_stat,
         int num_radios, wifi_radio_stat *radio_stat);
} wifi_stats_result_handler;


/* wifi statistics bitmap  */
#define WIFI_STATS_RADIO              0x00000001      // all radio statistics
#define WIFI_STATS_RADIO_CCA          0x00000002      // cca_busy_time (within radio statistics)
#define WIFI_STATS_RADIO_CHANNELS     0x00000004      // all channel statistics (within radio statistics)
#define WIFI_STATS_RADIO_SCAN         0x00000008      // all scan statistics (within radio statistics)
#define WIFI_STATS_IFACE              0x00000010      // all interface statistics
#define WIFI_STATS_IFACE_TXRATE       0x00000020      // all tx rate statistics (within interface statistics)
#define WIFI_STATS_IFACE_AC           0x00000040      // all ac statistics (within interface statistics)
#define WIFI_STATS_IFACE_CONTENTION   0x00000080      // all contention (min, max, avg) statistics (within ac statisctics)

#endif /* CONFIG_RTW_CFGVEDNOR_LLSTATS */


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)) || defined(RTW_VENDOR_EXT_SUPPORT)
extern int rtw_cfgvendor_attach(struct wiphy *wiphy);
extern int rtw_cfgvendor_detach(struct wiphy *wiphy);
extern int rtw_cfgvendor_send_async_event(struct wiphy *wiphy,
	struct net_device *dev, int event_id, const void  *data, int len);
#if defined(GSCAN_SUPPORT) && 0
extern int rtw_cfgvendor_send_hotlist_event(struct wiphy *wiphy,
	struct net_device *dev, void  *data, int len, rtw_vendor_event_t event);
#endif
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)) || defined(RTW_VENDOR_EXT_SUPPORT) */

#endif /* _RTW_CFGVENDOR_H_ */
