/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Header file of Broadcom Dongle Host Driver (DHD)
 * Prefered Network Offload code and Wi-Fi Location Service(WLS) code.
 *
 * Copyright (C) 1999-2019, Broadcom Corporation
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
 * $Id: dhd_pno.h 423669 2013-09-18 13:01:55Z $
 */

#ifndef __DHD_PNO_H__
#define __DHD_PNO_H__

#if defined(PNO_SUPPORT)
#define PNO_TLV_PREFIX			'S'
#define PNO_TLV_VERSION			'1'
#define PNO_TLV_SUBTYPE_LEGACY_PNO '2'
#define PNO_TLV_RESERVED		'0'
#define PNO_BATCHING_SET "SET"
#define PNO_BATCHING_GET "GET"
#define PNO_BATCHING_STOP "STOP"
#define PNO_PARAMS_DELIMETER " "
#define PNO_PARAM_CHANNEL_DELIMETER ","
#define PNO_PARAM_VALUE_DELLIMETER '='
#define PNO_PARAM_SCANFREQ "SCANFREQ"
#define PNO_PARAM_BESTN	"BESTN"
#define PNO_PARAM_MSCAN "MSCAN"
#define PNO_PARAM_CHANNEL "CHANNEL"
#define PNO_PARAM_RTT "RTT"

#define PNO_TLV_TYPE_SSID_IE		'S'
#define PNO_TLV_TYPE_TIME		'T'
#define PNO_TLV_FREQ_REPEAT		'R'
#define PNO_TLV_FREQ_EXPO_MAX		'M'

#define MAXNUM_SSID_PER_ADD	16
#define MAXNUM_PNO_PARAMS 2
#define PNO_TLV_COMMON_LENGTH	1
#define DEFAULT_BATCH_MSCAN 16

#define RESULTS_END_MARKER "----\n"
#define SCAN_END_MARKER "####\n"
#define AP_END_MARKER "====\n"
#define PNO_RSSI_MARGIN_DBM          30

#ifdef GSCAN_SUPPORT

#define GSCAN_MAX_CH_BUCKETS             8
#define GSCAN_MAX_CHANNELS_IN_BUCKET     32
#define GSCAN_MAX_AP_CACHE_PER_SCAN      32
#define GSCAN_MAX_AP_CACHE               320
#define GSCAN_BG_BAND_MASK             (1 << 0)
#define GSCAN_A_BAND_MASK              (1 << 1)
#define GSCAN_DFS_MASK                 (1 << 2)
#define GSCAN_ABG_BAND_MASK            (GSCAN_A_BAND_MASK | GSCAN_BG_BAND_MASK)
#define GSCAN_BAND_MASK                (GSCAN_ABG_BAND_MASK | GSCAN_DFS_MASK)

#define GSCAN_FLUSH_HOTLIST_CFG      (1 << 0)
#define GSCAN_FLUSH_SIGNIFICANT_CFG  (1 << 1)
#define GSCAN_FLUSH_SCAN_CFG         (1 << 2)
#define GSCAN_FLUSH_EPNO_CFG         (1 << 3)
#define GSCAN_FLUSH_ALL_CFG     (GSCAN_FLUSH_SCAN_CFG | \
								GSCAN_FLUSH_SIGNIFICANT_CFG | \
								GSCAN_FLUSH_HOTLIST_CFG  | \
								GSCAN_FLUSH_EPNO_CFG)
#define DHD_EPNO_HIDDEN_SSID          (1 << 0)
#define DHD_EPNO_A_BAND_TRIG          (1 << 1)
#define DHD_EPNO_BG_BAND_TRIG         (1 << 2)
#define DHD_EPNO_STRICT_MATCH         (1 << 3)
#define DHD_PNO_USE_SSID              (DHD_EPNO_HIDDEN_SSID | DHD_EPNO_STRICT_MATCH)

/* Do not change GSCAN_BATCH_RETRIEVAL_COMPLETE */
#define GSCAN_BATCH_RETRIEVAL_COMPLETE      0
#define GSCAN_BATCH_RETRIEVAL_IN_PROGRESS   1
#define GSCAN_BATCH_NO_THR_SET              101
#define GSCAN_LOST_AP_WINDOW_DEFAULT        4
#define GSCAN_MIN_BSSID_TIMEOUT             90
#define GSCAN_BATCH_GET_MAX_WAIT            500
#define CHANNEL_BUCKET_EMPTY_INDEX                      0xFF
#define GSCAN_RETRY_THRESHOLD              3
#define MAX_EPNO_SSID_NUM                   32
#endif /* GSCAN_SUPPORT */

enum scan_status {
	/* SCAN ABORT by other scan */
	PNO_STATUS_ABORT,
	/* RTT is presence or not */
	PNO_STATUS_RTT_PRESENCE,
	/* Disable PNO by Driver */
	PNO_STATUS_DISABLE,
	/* NORMAL BATCHING GET */
	PNO_STATUS_NORMAL,
	/* WLC_E_PFN_BEST_BATCHING */
	PNO_STATUS_EVENT,
	PNO_STATUS_MAX
};
#define PNO_STATUS_ABORT_MASK 0x0001
#define PNO_STATUS_RTT_MASK 0x0002
#define PNO_STATUS_DISABLE_MASK 0x0004
#define PNO_STATUS_OOM_MASK 0x0010

enum index_mode {
	INDEX_OF_LEGACY_PARAMS,
	INDEX_OF_BATCH_PARAMS,
	INDEX_OF_HOTLIST_PARAMS,
	/* GSCAN includes hotlist scan and they do not run
	 * independent of each other
	 */
#ifdef GSCAN_SUPPORT
	INDEX_OF_GSCAN_PARAMS = INDEX_OF_HOTLIST_PARAMS,
#endif /* GSCAN_SUPPORT */
	INDEX_MODE_MAX
};
enum dhd_pno_status {
	DHD_PNO_DISABLED,
	DHD_PNO_ENABLED,
	DHD_PNO_SUSPEND
};
typedef struct cmd_tlv {
	char prefix;
	char version;
	char subtype;
	char reserved;
} cmd_tlv_t;
#ifdef GSCAN_SUPPORT
typedef enum {
    WIFI_BAND_UNSPECIFIED,
    WIFI_BAND_BG = 1,                       /* 2.4 GHz                   */
    WIFI_BAND_A = 2,                        /* 5 GHz without DFS         */
    WIFI_BAND_A_DFS = 4,                    /* 5 GHz DFS only            */
    WIFI_BAND_A_WITH_DFS = 6,               /* 5 GHz with DFS            */
    WIFI_BAND_ABG = 3,                      /* 2.4 GHz + 5 GHz; no DFS   */
    WIFI_BAND_ABG_WITH_DFS = 7,             /* 2.4 GHz + 5 GHz with DFS  */
} gscan_wifi_band_t;

typedef enum {
	HOTLIST_LOST,
	HOTLIST_FOUND
} hotlist_type_t;

typedef enum dhd_pno_gscan_cmd_cfg {
	DHD_PNO_BATCH_SCAN_CFG_ID = 0,
	DHD_PNO_GEOFENCE_SCAN_CFG_ID,
	DHD_PNO_SIGNIFICANT_SCAN_CFG_ID,
	DHD_PNO_SCAN_CFG_ID,
	DHD_PNO_GET_CAPABILITIES,
	DHD_PNO_GET_BATCH_RESULTS,
	DHD_PNO_GET_CHANNEL_LIST,
	DHD_PNO_GET_EPNO_SSID_ELEM,
	DHD_PNO_EPNO_CFG_ID,
	DHD_PNO_GET_AUTOJOIN_CAPABILITIES
} dhd_pno_gscan_cmd_cfg_t;

typedef enum dhd_pno_mode {
	/* Wi-Fi Legacy PNO Mode */
	DHD_PNO_NONE_MODE   = 0,
	DHD_PNO_LEGACY_MODE = (1 << (0)),
	/* Wi-Fi Android BATCH SCAN Mode */
	DHD_PNO_BATCH_MODE = (1 << (1)),
	/* Wi-Fi Android Hotlist SCAN Mode */
	DHD_PNO_HOTLIST_MODE = (1 << (2)),
	/* Wi-Fi Google Android SCAN Mode */
	DHD_PNO_GSCAN_MODE = (1 << (3))
} dhd_pno_mode_t;
#else
typedef enum dhd_pno_mode {
	/* Wi-Fi Legacy PNO Mode */
	DHD_PNO_NONE_MODE   = 0,
	DHD_PNO_LEGACY_MODE = (1 << (0)),
	/* Wi-Fi Android BATCH SCAN Mode */
	DHD_PNO_BATCH_MODE = (1 << (1)),
	/* Wi-Fi Android Hotlist SCAN Mode */
	DHD_PNO_HOTLIST_MODE = (1 << (2))
} dhd_pno_mode_t;
#endif /* GSCAN_SUPPORT */
struct dhd_pno_ssid {
	bool		hidden;
	int8		rssi_thresh;
	uint8		dummy;
	uint16		SSID_len;
	uchar		SSID[DOT11_MAX_SSID_LEN];
	struct list_head list;
};
struct dhd_pno_bssid {
	struct ether_addr	macaddr;
	/* Bit4: suppress_lost, Bit3: suppress_found */
	uint16			flags;
	struct list_head list;
};
typedef struct dhd_pno_bestnet_entry {
	struct ether_addr BSSID;
	uint8	SSID_len;
	uint8	SSID[DOT11_MAX_SSID_LEN];
	int8	RSSI;
	uint8	channel;
	uint32	timestamp;
	uint16	rtt0; /* distance_cm based on RTT */
	uint16	rtt1; /* distance_cm based on sample standard deviation */
	unsigned long recorded_time;
	struct list_head list;
} dhd_pno_bestnet_entry_t;
#define BESTNET_ENTRY_SIZE (sizeof(dhd_pno_bestnet_entry_t))

typedef struct dhd_pno_bestnet_header {
	struct dhd_pno_bestnet_header *next;
	uint8 reason;
	uint32 tot_cnt;
	uint32 tot_size;
	struct list_head entry_list;
} dhd_pno_best_header_t;
#define BEST_HEADER_SIZE (sizeof(dhd_pno_best_header_t))

typedef struct dhd_pno_scan_results {
	dhd_pno_best_header_t *bestnetheader;
	uint8 cnt_header;
	struct list_head list;
} dhd_pno_scan_results_t;
#define SCAN_RESULTS_SIZE (sizeof(dhd_pno_scan_results_t))

struct dhd_pno_get_batch_info {
	/* info related to get batch */
	char *buf;
	bool batch_started;
	uint32 tot_scan_cnt;
	uint32 expired_tot_scan_cnt;
	uint32 top_node_cnt;
	uint32 bufsize;
	uint32 bytes_written;
	int reason;
	struct list_head scan_results_list;
	struct list_head expired_scan_results_list;
};
struct dhd_pno_legacy_params {
	uint16 scan_fr;
	uint16 chan_list[WL_NUMCHANNELS];
	uint16 nchan;
	int pno_repeat;
	int pno_freq_expo_max;
	int nssid;
	struct list_head ssid_list;
};
struct dhd_pno_batch_params {
	int32 scan_fr;
	uint8 bestn;
	uint8 mscan;
	uint8 band;
	uint16 chan_list[WL_NUMCHANNELS];
	uint16 nchan;
	uint16 rtt;
	struct dhd_pno_get_batch_info get_batch;
};
struct dhd_pno_hotlist_params {
	uint8 band;
	int32 scan_fr;
	uint16 chan_list[WL_NUMCHANNELS];
	uint16 nchan;
	uint16 nbssid;
	struct list_head bssid_list;
};
#ifdef GSCAN_SUPPORT
#define DHD_PNO_REPORT_NO_BATCH      (1 << 2)

typedef struct dhd_pno_gscan_channel_bucket {
	uint16 bucket_freq_multiple;
	/* band = 1 All bg band channels,
	 * band = 2 All a band channels,
	 * band = 0 chan_list channels
	 */
	uint16 band;
	uint8 report_flag;
	uint8 num_channels;
	uint16 repeat;
	uint16 bucket_max_multiple;
	uint16 chan_list[GSCAN_MAX_CHANNELS_IN_BUCKET];
} dhd_pno_gscan_channel_bucket_t;


#define DHD_PNO_AUTH_CODE_OPEN  1 /*  Open   */
#define DHD_PNO_AUTH_CODE_PSK   2 /* WPA_PSK or WPA2PSK */
#define DHD_PNO_AUTH_CODE_EAPOL 4 /* any EAPOL    */

#define DHD_EPNO_DEFAULT_INDEX     0xFFFFFFFF

typedef struct dhd_epno_params {
	uint8 ssid[DOT11_MAX_SSID_LEN];
	uint8 ssid_len;
	int8 rssi_thresh;
	uint8 flags;
	uint8 auth;
	/* index required only for visble ssid */
	uint32 index;
	struct list_head list;
} dhd_epno_params_t;

typedef struct dhd_epno_results {
	uint8 ssid[DOT11_MAX_SSID_LEN];
	uint8 ssid_len;
	int8 rssi;
	uint16 channel;
	uint16 flags;
	struct ether_addr bssid;
} dhd_epno_results_t;

typedef struct dhd_pno_swc_evt_param {
	uint16 results_rxed_so_far;
	wl_pfn_significant_net_t *change_array;
} dhd_pno_swc_evt_param_t;

typedef struct wifi_gscan_result {
	uint64 ts;                           /* Time of discovery           */
	char ssid[DOT11_MAX_SSID_LEN+1];     /* null terminated             */
	struct ether_addr	macaddr;         /* BSSID                      */
	uint32 channel;                      /* channel frequency in MHz    */
	int32 rssi;                          /* in db                       */
	uint64 rtt;                          /* in nanoseconds              */
	uint64 rtt_sd;                       /* standard deviation in rtt   */
	uint16 beacon_period;                /* units are Kusec             */
	uint16 capability;                   /* Capability information       */
	uint32 ie_length;                    /* byte length of Information Elements */
	char  ie_data[1];                    /* IE data to follow       */
} wifi_gscan_result_t;

typedef struct gscan_results_cache {
	struct gscan_results_cache *next;
	uint8  scan_id;
	uint8  flag;
	uint8  tot_count;
	uint8  tot_consumed;
	wifi_gscan_result_t results[1];
} gscan_results_cache_t;

typedef struct dhd_pno_gscan_capabilities {
	int max_scan_cache_size;
	int max_scan_buckets;
	int max_ap_cache_per_scan;
	int max_rssi_sample_size;
	int max_scan_reporting_threshold;
	int max_hotlist_aps;
	int max_significant_wifi_change_aps;
	int max_epno_ssid_crc32;
	int max_epno_hidden_ssid;
	int max_white_list_ssid;
} dhd_pno_gscan_capabilities_t;

struct dhd_pno_gscan_params {
	int32 scan_fr;
	uint8 bestn;
	uint8 mscan;
	uint8 buffer_threshold;
	uint8 swc_nbssid_threshold;
	uint8 swc_rssi_window_size;
	uint8 lost_ap_window;
	uint8 nchannel_buckets;
	uint8 reason;
	uint8 get_batch_flag;
	uint8 send_all_results_flag;
	uint16 max_ch_bucket_freq;
	gscan_results_cache_t *gscan_batch_cache;
	gscan_results_cache_t *gscan_hotlist_found;
	gscan_results_cache_t *gscan_hotlist_lost;
	uint16 nbssid_significant_change;
	uint16 nbssid_hotlist;
	uint16 num_epno_ssid;
	uint8 num_visible_epno_ssid;
	/* To keep track of visble ssid index
	 * across multiple FW configs i.e. config
	 * w/o clear in between
	 */
	uint8 ssid_ext_last_used_index;
	struct dhd_pno_swc_evt_param param_significant;
	struct dhd_pno_gscan_channel_bucket channel_bucket[GSCAN_MAX_CH_BUCKETS];
	struct list_head hotlist_bssid_list;
	struct list_head significant_bssid_list;
	struct list_head epno_ssid_list;
	uint32 scan_id;
};

typedef struct gscan_scan_params {
	int32 scan_fr;
	uint16 nchannel_buckets;
	struct dhd_pno_gscan_channel_bucket channel_bucket[GSCAN_MAX_CH_BUCKETS];
} gscan_scan_params_t;

typedef struct gscan_batch_params {
	uint8 bestn;
	uint8 mscan;
	uint8 buffer_threshold;
} gscan_batch_params_t;

struct bssid_t {
	struct ether_addr	macaddr;
	int16 rssi_reporting_threshold;  /* 0 -> no reporting threshold */
};

typedef struct gscan_hotlist_scan_params {
	uint16 lost_ap_window; /* number of scans to declare LOST */
	uint16 nbssid;   /* number of bssids  */
	struct bssid_t bssid[1];  /* n bssids to follow */
} gscan_hotlist_scan_params_t;

/* SWC (Significant WiFi Change) params */
typedef struct gscan_swc_params {
	/* Rssi averaging window size */
	uint8 rssi_window;
	/* Number of scans that the AP has to be absent before
	 * being declared LOST
	 */
	uint8 lost_ap_window;
	/* if x  Aps have a significant change generate an event. */
	uint8 swc_threshold;
	uint8 nbssid;
	wl_pfn_significant_bssid_t bssid_elem_list[1];
} gscan_swc_params_t;

typedef struct dhd_pno_significant_bssid {
	struct ether_addr BSSID;
	int8 rssi_low_threshold;
	int8 rssi_high_threshold;
	struct list_head list;
} dhd_pno_significant_bssid_t;
#endif /* GSCAN_SUPPORT */
typedef union dhd_pno_params {
	struct dhd_pno_legacy_params params_legacy;
	struct dhd_pno_batch_params params_batch;
	struct dhd_pno_hotlist_params params_hotlist;
#ifdef GSCAN_SUPPORT
	struct dhd_pno_gscan_params params_gscan;
#endif /* GSCAN_SUPPORT */
} dhd_pno_params_t;
typedef struct dhd_pno_status_info {
	dhd_pub_t *dhd;
	struct work_struct work;
	struct mutex pno_mutex;
#ifdef GSCAN_SUPPORT
	wait_queue_head_t batch_get_wait;
#endif /* GSCAN_SUPPORT */
	struct completion get_batch_done;
	bool wls_supported; /* wifi location service supported or not */
	enum dhd_pno_status pno_status;
	enum dhd_pno_mode pno_mode;
	dhd_pno_params_t pno_params_arr[INDEX_MODE_MAX];
	struct list_head head_list;
} dhd_pno_status_info_t;

/* wrapper functions */
extern int
dhd_dev_pno_enable(struct net_device *dev, int enable);

extern int
dhd_dev_pno_stop_for_ssid(struct net_device *dev);

extern int
dhd_dev_pno_set_for_ssid(struct net_device *dev, wlc_ssid_ext_t* ssids_local, int nssid,
	uint16 scan_fr, int pno_repeat, int pno_freq_expo_max, uint16 *channel_list, int nchan);

extern int
dhd_dev_pno_set_for_batch(struct net_device *dev,
	struct dhd_pno_batch_params *batch_params);

extern int
dhd_dev_pno_get_for_batch(struct net_device *dev, char *buf, int bufsize);

extern int
dhd_dev_pno_stop_for_batch(struct net_device *dev);

extern int
dhd_dev_pno_set_for_hotlist(struct net_device *dev, wl_pfn_bssid_t *p_pfn_bssid,
	struct dhd_pno_hotlist_params *hotlist_params);
extern bool dhd_dev_is_legacy_pno_enabled(struct net_device *dev);
#ifdef GSCAN_SUPPORT
extern int
dhd_dev_pno_set_cfg_gscan(struct net_device *dev, dhd_pno_gscan_cmd_cfg_t type,
              void *buf, uint8 flush);
extern void *
dhd_dev_pno_get_gscan(struct net_device *dev, dhd_pno_gscan_cmd_cfg_t type, void *info,
        uint32 *len);
int dhd_dev_pno_lock_access_batch_results(struct net_device *dev);
void dhd_dev_pno_unlock_access_batch_results(struct net_device *dev);
extern int dhd_dev_pno_run_gscan(struct net_device *dev, bool run, bool flush);
extern int dhd_dev_pno_enable_full_scan_result(struct net_device *dev, bool real_time);
extern void * dhd_dev_swc_scan_event(struct net_device *dev, const void  *data,
              int *send_evt_bytes);
int dhd_retreive_batch_scan_results(dhd_pub_t *dhd);
extern void * dhd_dev_hotlist_scan_event(struct net_device *dev,
            const void  *data, int *send_evt_bytes, hotlist_type_t type);
void * dhd_dev_process_full_gscan_result(struct net_device *dev,
            const void  *data, uint32 len, int *send_evt_bytes);
extern int dhd_dev_gscan_batch_cache_cleanup(struct net_device *dev);
extern void dhd_dev_gscan_hotlist_cache_cleanup(struct net_device *dev, hotlist_type_t type);
extern int dhd_dev_wait_batch_results_complete(struct net_device *dev);
extern void * dhd_dev_process_epno_result(struct net_device *dev,
                        const void  *data, uint32 event, int *send_evt_bytes);
#endif /* GSCAN_SUPPORT */
/* dhd pno fuctions */
extern int dhd_pno_stop_for_ssid(dhd_pub_t *dhd);
extern int dhd_pno_enable(dhd_pub_t *dhd, int enable);
extern int dhd_pno_set_for_ssid(dhd_pub_t *dhd, wlc_ssid_ext_t* ssid_list, int nssid,
	uint16  scan_fr, int pno_repeat, int pno_freq_expo_max, uint16 *channel_list, int nchan);

extern int dhd_pno_set_for_batch(dhd_pub_t *dhd, struct dhd_pno_batch_params *batch_params);

extern int dhd_pno_get_for_batch(dhd_pub_t *dhd, char *buf, int bufsize, int reason);


extern int dhd_pno_stop_for_batch(dhd_pub_t *dhd);

extern int dhd_pno_set_for_hotlist(dhd_pub_t *dhd, wl_pfn_bssid_t *p_pfn_bssid,
	struct dhd_pno_hotlist_params *hotlist_params);

extern int dhd_pno_stop_for_hotlist(dhd_pub_t *dhd);

extern int dhd_pno_event_handler(dhd_pub_t *dhd, wl_event_msg_t *event, void *event_data);
extern int dhd_pno_init(dhd_pub_t *dhd);
extern int dhd_pno_deinit(dhd_pub_t *dhd);
extern bool dhd_is_pno_supported(dhd_pub_t *dhd);
extern int dhd_pno_set_mac_oui(dhd_pub_t *dhd, uint8 *oui);
extern bool dhd_is_legacy_pno_enabled(dhd_pub_t *dhd);
#ifdef GSCAN_SUPPORT
extern int dhd_pno_set_cfg_gscan(dhd_pub_t *dhd, dhd_pno_gscan_cmd_cfg_t type,
                       void *buf, uint8 flush);
extern void * dhd_pno_get_gscan(dhd_pub_t *dhd, dhd_pno_gscan_cmd_cfg_t type, void *info,
                       uint32 *len);
extern int dhd_pno_lock_batch_results(dhd_pub_t *dhd);
extern void dhd_pno_unlock_batch_results(dhd_pub_t *dhd);
extern int dhd_pno_initiate_gscan_request(dhd_pub_t *dhd, bool run, bool flush);
extern int dhd_pno_enable_full_scan_result(dhd_pub_t *dhd, bool real_time_flag);
extern int dhd_pno_cfg_gscan(dhd_pub_t *dhd, dhd_pno_gscan_cmd_cfg_t type, void *buf);
extern int dhd_dev_retrieve_batch_scan(struct net_device *dev);
extern void *dhd_handle_swc_evt(dhd_pub_t *dhd, const void *event_data, int *send_evt_bytes);
extern void *dhd_handle_hotlist_scan_evt(dhd_pub_t *dhd, const void *event_data,
                       int *send_evt_bytes, hotlist_type_t type);
extern void *dhd_process_full_gscan_result(dhd_pub_t *dhd, const void *event_data,
                       uint32 len, int *send_evt_bytes);
extern int dhd_gscan_batch_cache_cleanup(dhd_pub_t *dhd);
extern void dhd_gscan_hotlist_cache_cleanup(dhd_pub_t *dhd, hotlist_type_t type);
extern int dhd_wait_batch_results_complete(dhd_pub_t *dhd);
extern void * dhd_pno_process_epno_result(dhd_pub_t *dhd, const void *data,
         uint32 event, int *size);
#endif /* GSCAN_SUPPORT */
#endif 

#endif /* __DHD_PNO_H__ */
