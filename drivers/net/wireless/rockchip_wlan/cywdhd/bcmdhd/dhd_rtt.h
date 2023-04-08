/*
 * Broadcom Dongle Host Driver (DHD), RTT
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
 * $Id$
 */
#ifndef __DHD_RTT_H__
#define __DHD_RTT_H__

#include "dngl_stats.h"

#define RTT_MAX_TARGET_CNT 50
#define RTT_MAX_FRAME_CNT 25
#define RTT_MAX_RETRY_CNT 10
#define DEFAULT_FTM_CNT 6
#define DEFAULT_RETRY_CNT 6
#define DEFAULT_FTM_FREQ 5180
#define DEFAULT_FTM_CNTR_FREQ0 5210
#define RTT_MAX_GEOFENCE_TARGET_CNT 8

#define TARGET_INFO_SIZE(count) (sizeof(rtt_target_info_t) * count)

#define TARGET_TYPE(target) (target->type)

#define RTT_IS_ENABLED(rtt_status) (rtt_status->status == RTT_ENABLED)
#define RTT_IS_STOPPED(rtt_status) (rtt_status->status == RTT_STOPPED)

#ifndef BIT
#define BIT(x) (1 << (x))
#endif // endif

/* DSSS, CCK and 802.11n rates in [500kbps] units */
#define WL_MAXRATE	108	/* in 500kbps units */
#define WL_RATE_1M	2	/* in 500kbps units */
#define WL_RATE_2M	4	/* in 500kbps units */
#define WL_RATE_5M5	11	/* in 500kbps units */
#define WL_RATE_11M	22	/* in 500kbps units */
#define WL_RATE_6M	12	/* in 500kbps units */
#define WL_RATE_9M	18	/* in 500kbps units */
#define WL_RATE_12M	24	/* in 500kbps units */
#define WL_RATE_18M	36	/* in 500kbps units */
#define WL_RATE_24M	48	/* in 500kbps units */
#define WL_RATE_36M	72	/* in 500kbps units */
#define WL_RATE_48M	96	/* in 500kbps units */
#define WL_RATE_54M	108	/* in 500kbps units */
#define GET_RTTSTATE(dhd) ((rtt_status_info_t *)dhd->rtt_state)

/* RTT Retry Timer Interval */
#define DHD_RTT_RETRY_TIMER_INTERVAL_MS		3000u

#define DHD_RTT_INVALID_TARGET_INDEX		-1

enum rtt_role {
	RTT_INITIATOR = 0,
	RTT_TARGET = 1
};
enum rtt_status {
	RTT_STOPPED = 0,
	RTT_STARTED = 1,
	RTT_ENABLED = 2
};
typedef int64_t wifi_timestamp; /* In microseconds (us) */
typedef int64_t wifi_timespan;
typedef int32 wifi_rssi_rtt;

typedef enum {
	RTT_INVALID,
	RTT_ONE_WAY,
	RTT_TWO_WAY,
	RTT_AUTO
} rtt_type_t;

/* RTT peer type */
typedef enum {
	RTT_PEER_AP         = 0x1,
	RTT_PEER_STA        = 0x2,
	RTT_PEER_P2P_GO     = 0x3,
	RTT_PEER_P2P_CLIENT = 0x4,
	RTT_PEER_NAN        = 0x5,
	RTT_PEER_INVALID    = 0x6
} rtt_peer_type_t;

/* Ranging status */
typedef enum rtt_reason {
	RTT_STATUS_SUCCESS       = 0,
	RTT_STATUS_FAILURE       = 1,           // general failure status
	RTT_STATUS_FAIL_NO_RSP   = 2,           // target STA does not respond to request
	RTT_STATUS_FAIL_REJECTED = 3,           // request rejected. Applies to 2-sided RTT only
	RTT_STATUS_FAIL_NOT_SCHEDULED_YET  = 4,
	RTT_STATUS_FAIL_TM_TIMEOUT         = 5, // timing measurement times out
	RTT_STATUS_FAIL_AP_ON_DIFF_CHANNEL = 6, // Target on different channel, cannot range
	RTT_STATUS_FAIL_NO_CAPABILITY  = 7,     // ranging not supported
	RTT_STATUS_ABORTED             = 8,     // request aborted for unknown reason
	RTT_STATUS_FAIL_INVALID_TS     = 9,     // Invalid T1-T4 timestamp
	RTT_STATUS_FAIL_PROTOCOL       = 10,    // 11mc protocol failed
	RTT_STATUS_FAIL_SCHEDULE       = 11,    // request could not be scheduled
	RTT_STATUS_FAIL_BUSY_TRY_LATER = 12,    // responder cannot collaborate at time of request
	RTT_STATUS_INVALID_REQ         = 13,    // bad request args
	RTT_STATUS_NO_WIFI             = 14,    // WiFi not enabled Responder overrides param info
						// cannot range with new params
	RTT_STATUS_FAIL_FTM_PARAM_OVERRIDE = 15
} rtt_reason_t;

enum {
	RTT_CAP_ONE_WAY	 = BIT(0),
	/* IEEE802.11mc */
	RTT_CAP_FTM_WAY  = BIT(1)
};

enum {
	RTT_FEATURE_LCI = BIT(0),
	RTT_FEATURE_LCR = BIT(1),
	RTT_FEATURE_PREAMBLE = BIT(2),
	RTT_FEATURE_BW = BIT(3)
};

enum {
	RTT_PREAMBLE_LEGACY = BIT(0),
	RTT_PREAMBLE_HT = BIT(1),
	RTT_PREAMBLE_VHT = BIT(2)
};

enum {
	RTT_BW_5 = BIT(0),
	RTT_BW_10 = BIT(1),
	RTT_BW_20 = BIT(2),
	RTT_BW_40 = BIT(3),
	RTT_BW_80 = BIT(4),
	RTT_BW_160 = BIT(5)
};

enum rtt_rate_bw {
	RTT_RATE_20M,
	RTT_RATE_40M,
	RTT_RATE_80M,
	RTT_RATE_160M
};

typedef enum ranging_type {
	RTT_TYPE_INVALID	=	0,
	RTT_TYPE_LEGACY		=	1,
	RTT_TYPE_NAN_DIRECTED	=	2,
	RTT_TYPE_NAN_GEOFENCE	=	3
} ranging_type_t;

#define FTM_MAX_NUM_BURST_EXP	14
#define HAS_11MC_CAP(cap) (cap & RTT_CAP_FTM_WAY)
#define HAS_ONEWAY_CAP(cap) (cap & RTT_CAP_ONE_WAY)
#define HAS_RTT_CAP(cap) (HAS_ONEWAY_CAP(cap) || HAS_11MC_CAP(cap))

typedef struct wifi_channel_info {
	wifi_channel_width_t width;
	wifi_channel center_freq; /* primary 20 MHz channel */
	wifi_channel center_freq0; /* center freq (MHz) first segment */
	wifi_channel center_freq1; /* center freq (MHz) second segment valid for 80 + 80 */
} wifi_channel_info_t;

typedef struct wifi_rate {
	uint32 preamble :3; /* 0: OFDM, 1: CCK, 2 : HT, 3: VHT, 4..7 reserved */
	uint32 nss		:2; /* 1 : 1x1, 2: 2x2, 3: 3x3, 4: 4x4 */
	uint32 bw		:3; /* 0: 20Mhz, 1: 40Mhz, 2: 80Mhz, 3: 160Mhz */
	/* OFDM/CCK rate code would be as per IEEE std in the unit of 0.5 mb
	* HT/VHT it would be mcs index
	*/
	uint32 rateMcsIdx :8;
	uint32 reserved :16; /* reserved */
	uint32 bitrate;	/* unit of 100 Kbps */
} wifi_rate_t;

typedef struct rtt_target_info {
	struct ether_addr addr;
	struct ether_addr local_addr;
	rtt_type_t type; /* rtt_type */
	rtt_peer_type_t peer; /* peer type */
	wifi_channel_info_t channel; /* channel information */
	chanspec_t chanspec; /* chanspec for channel */
	bool	disable; /* disable for RTT measurement */
	/*
	* Time interval between bursts (units: 100 ms).
	* Applies to 1-sided and 2-sided RTT multi-burst requests.
	* Range: 0-31, 0: no preference by initiator (2-sided RTT)
	*/
	uint32	burst_period;
	/*
	* Total number of RTT bursts to be executed. It will be
	* specified in the same way as the parameter "Number of
	* Burst Exponent" found in the FTM frame format. It
	* applies to both: 1-sided RTT and 2-sided RTT. Valid
	* values are 0 to 15 as defined in 802.11mc std.
	* 0 means single shot
	* The implication of this parameter on the maximum
	* number of RTT results is the following:
	* for 1-sided RTT: max num of RTT results = (2^num_burst)*(num_frames_per_burst)
	* for 2-sided RTT: max num of RTT results = (2^num_burst)*(num_frames_per_burst - 1)
	*/
	uint16 num_burst;
	/*
	* num of frames per burst.
	* Minimum value = 1, Maximum value = 31
	* For 2-sided this equals the number of FTM frames
	* to be attempted in a single burst. This also
	* equals the number of FTM frames that the
	* initiator will request that the responder send
	* in a single frame
	*/
	uint32 num_frames_per_burst;
	/*
	 * num of frames in each RTT burst
	 * for single side, measurement result num = frame number
	 * for 2 side RTT, measurement result num  = frame number - 1
	 */
	uint32 num_retries_per_ftm; /* retry time for RTT measurment frame */
	/* following fields are only valid for 2 side RTT */
	uint32 num_retries_per_ftmr;
	uint8  LCI_request;
	uint8  LCR_request;
	/*
	* Applies to 1-sided and 2-sided RTT. Valid values will
	* be 2-11 and 15 as specified by the 802.11mc std for
	* the FTM parameter burst duration. In a multi-burst
	* request, if responder overrides with larger value,
	* the initiator will return failure. In a single-burst
	* request if responder overrides with larger value,
	* the initiator will sent TMR_STOP to terminate RTT
	* at the end of the burst_duration it requested.
	*/
	uint32 burst_duration;
	uint32 burst_timeout;
	uint8  preamble; /* 1 - Legacy, 2 - HT, 4 - VHT */
	uint8  bw;  /* 5, 10, 20, 40, 80, 160 */
} rtt_target_info_t;

typedef struct rtt_goefence_target_info {
	bool valid;
	struct ether_addr peer_addr;
} rtt_geofence_target_info_t;

typedef struct rtt_config_params {
	int8 rtt_target_cnt;
	rtt_target_info_t *target_info;
} rtt_config_params_t;

typedef struct rtt_geofence_cfg {
	int8 geofence_target_cnt;
	bool rtt_in_progress;
	bool role_concurr_state;
	int8 cur_target_idx;
	rtt_geofence_target_info_t geofence_target_info[RTT_MAX_GEOFENCE_TARGET_CNT];
} rtt_geofence_cfg_t;

/*
 * Keep Adding more reasons
 * going forward if needed
 */
enum rtt_schedule_reason {
	RTT_SCHED_HOST_TRIGGER		= 1, /* On host command for directed RTT */
	RTT_SCHED_SUB_MATCH		= 2, /* on Sub Match for svc with range req */
	RTT_SCHED_DIR_TRIGGER_FAIL	= 3, /* On failure of Directed RTT Trigger */
	RTT_SCHED_DP_END		= 4, /* ON NDP End event from fw */
	RTT_SCHED_DP_REJECTED		= 5, /* On receving reject dp event from fw */
	RTT_SCHED_RNG_RPT_DIRECTED	= 6, /* On Ranging report for directed RTT */
	RTT_SCHED_RNG_TERM		= 7, /* On Range Term Indicator */
	RTT_SHCED_HOST_DIRECTED_TERM	= 8, /* On host terminating directed RTT sessions */
	RTT_SCHED_RNG_RPT_GEOFENCE	= 9, /* On Ranging report for geofence RTT */
	RTT_SCHED_RTT_RETRY_GEOFENCE	= 10 /* On Geofence Retry */
};

/*
 * Keep Adding more invalid RTT states
 * going forward if needed
 */
enum rtt_invalid_state {
	RTT_STATE_VALID			= 0, /* RTT state is valid */
	RTT_STATE_INV_REASON_NDP_EXIST	= 1 /* RTT state invalid as ndp exists */
};

typedef struct rtt_status_info {
	dhd_pub_t	*dhd;
	int8		status;   /* current status for the current entry */
	int8		txchain; /* current device tx chain */
	int		pm; /* to save current value of pm */
	int8		pm_restore; /* flag to reset the old value of pm */
	int8		cur_idx; /* current entry to do RTT */
	bool		all_cancel; /* cancel all request once we got the cancel requet */
	uint32		flags; /* indicate whether device is configured as initiator or target */
	struct capability {
		int32 proto     :8;
		int32 feature   :8;
		int32 preamble  :8;
		int32 bw        :8;
	} rtt_capa; /* rtt capability */
	struct			mutex rtt_mutex;
	struct			mutex geofence_mutex;
	rtt_config_params_t	rtt_config;
	rtt_geofence_cfg_t	geofence_cfg;
	struct work_struct	work;
	struct list_head	noti_fn_list;
	struct list_head	rtt_results_cache; /* store results for RTT */
	int			rtt_sched_reason; /* rtt_schedule_reason: what scheduled RTT */
	struct delayed_work	proxd_timeout; /* Proxd Timeout work */
	struct delayed_work	rtt_retry_timer;   /* Timer for retry RTT after all targets done */
} rtt_status_info_t;

typedef struct rtt_report {
	struct ether_addr addr;
	unsigned int burst_num; /* # of burst inside a multi-burst request */
	unsigned int ftm_num; /* total RTT measurement frames attempted */
	unsigned int success_num; /* total successful RTT measurement frames */
	uint8  num_per_burst_peer; /* max number of FTM number per burst the peer support */
	rtt_reason_t status; /* raging status */
	/* in s, 11mc only, only for RTT_REASON_FAIL_BUSY_TRY_LATER, 1- 31s */
	uint8 retry_after_duration;
	rtt_type_t type; /* rtt type */
	wifi_rssi_rtt  rssi; /* average rssi in 0.5 dB steps e.g. 143 implies -71.5 dB */
	wifi_rssi_rtt  rssi_spread; /* rssi spread in 0.5 db steps e.g. 5 implies 2.5 spread */
	/*
	* 1-sided RTT: TX rate of RTT frame.
	* 2-sided RTT: TX rate of initiator's Ack in response to FTM frame.
	*/
	wifi_rate_t tx_rate;
	/*
	* 1-sided RTT: TX rate of Ack from other side.
	* 2-sided RTT: TX rate of FTM frame coming from responder.
	*/
	wifi_rate_t rx_rate;
	wifi_timespan rtt;	/*  round trip time in 0.1 nanoseconds */
	wifi_timespan rtt_sd;	/* rtt standard deviation in 0.1 nanoseconds */
	wifi_timespan rtt_spread; /* difference between max and min rtt times recorded */
	int distance; /* distance in cm (optional) */
	int distance_sd; /* standard deviation in cm (optional) */
	int distance_spread; /* difference between max and min distance recorded (optional) */
	wifi_timestamp ts; /* time of the measurement (in microseconds since boot) */
	int burst_duration; /* in ms, how long the FW time is to fininish one burst measurement */
	int negotiated_burst_num; /* Number of bursts allowed by the responder */
	bcm_tlv_t *LCI; /* LCI Report */
	bcm_tlv_t *LCR; /* Location Civic Report */
} rtt_report_t;
#define RTT_REPORT_SIZE (sizeof(rtt_report_t))

/* rtt_results_header to maintain rtt result list per mac address */
typedef struct rtt_results_header {
	struct ether_addr peer_mac;
	uint32 result_cnt;
	uint32 result_tot_len; /* sum of report_len of rtt_result */
	struct list_head list;
	struct list_head result_list;
} rtt_results_header_t;
struct rtt_result_detail {
	uint8 num_ota_meas;
	uint32 result_flags;
};
/* rtt_result to link all of rtt_report */
typedef struct rtt_result {
	struct list_head list;
	struct rtt_report report;
	int32 report_len; /* total length of rtt_report */
	struct rtt_result_detail rtt_detail;
	int32 detail_len;
} rtt_result_t;

/* RTT Capabilities */
typedef struct rtt_capabilities {
	uint8 rtt_one_sided_supported;  /* if 1-sided rtt data collection is supported */
	uint8 rtt_ftm_supported;        /* if ftm rtt data collection is supported */
	uint8 lci_support;		/* location configuration information */
	uint8 lcr_support;		/* Civic Location */
	uint8 preamble_support;         /* bit mask indicate what preamble is supported */
	uint8 bw_support;               /* bit mask indicate what BW is supported */
} rtt_capabilities_t;

/* RTT responder information */
typedef struct wifi_rtt_responder {
	wifi_channel_info channel;   /* channel of responder */
	uint8 preamble;             /* preamble supported by responder */
} wifi_rtt_responder_t;

typedef void (*dhd_rtt_compl_noti_fn)(void *ctx, void *rtt_data);
/* Linux wrapper to call common dhd_rtt_set_cfg */
int
dhd_dev_rtt_set_cfg(struct net_device *dev, void *buf);

int
dhd_dev_rtt_cancel_cfg(struct net_device *dev, struct ether_addr *mac_list, int mac_cnt);

int
dhd_dev_rtt_register_noti_callback(struct net_device *dev, void *ctx,
	dhd_rtt_compl_noti_fn noti_fn);

int
dhd_dev_rtt_unregister_noti_callback(struct net_device *dev, dhd_rtt_compl_noti_fn noti_fn);

int
dhd_dev_rtt_capability(struct net_device *dev, rtt_capabilities_t *capa);

int
dhd_dev_rtt_avail_channel(struct net_device *dev, wifi_channel_info *channel_info);

int
dhd_dev_rtt_enable_responder(struct net_device *dev, wifi_channel_info *channel_info);

int
dhd_dev_rtt_cancel_responder(struct net_device *dev);
/* export to upper layer */
chanspec_t
dhd_rtt_convert_to_chspec(wifi_channel_info_t channel);

int
dhd_rtt_idx_to_burst_duration(uint idx);

int
dhd_rtt_set_cfg(dhd_pub_t *dhd, rtt_config_params_t *params);

void dhd_rtt_set_role_concurrency_state(dhd_pub_t *dhd, bool state);

bool dhd_rtt_get_role_concurrency_state(dhd_pub_t *dhd);

int8 dhd_rtt_get_geofence_target_cnt(dhd_pub_t *dhd);

#ifdef WL_NAN
void dhd_rtt_set_geofence_rtt_state(dhd_pub_t *dhd, bool state);

bool dhd_rtt_get_geofence_rtt_state(dhd_pub_t *dhd);

rtt_geofence_target_info_t*
dhd_rtt_get_geofence_target_head(dhd_pub_t *dhd);

rtt_geofence_target_info_t*
dhd_rtt_get_geofence_current_target(dhd_pub_t *dhd);

rtt_geofence_target_info_t*
dhd_rtt_get_geofence_target(dhd_pub_t *dhd, struct ether_addr* peer_addr,
	int8 *index);

int
dhd_rtt_add_geofence_target(dhd_pub_t *dhd, rtt_geofence_target_info_t  *target);

int
dhd_rtt_remove_geofence_target(dhd_pub_t *dhd, struct ether_addr *peer_addr);

int
dhd_rtt_delete_geofence_target_list(dhd_pub_t *dhd);

int
dhd_rtt_delete_nan_session(dhd_pub_t *dhd);
#endif /* WL_NAN */

uint8
dhd_rtt_invalid_states(struct net_device *ndev, struct ether_addr *peer_addr);

void
dhd_rtt_schedule_rtt_work_thread(dhd_pub_t *dhd, int sched_reason);

int
dhd_rtt_stop(dhd_pub_t *dhd, struct ether_addr *mac_list, int mac_cnt);

int
dhd_rtt_register_noti_callback(dhd_pub_t *dhd, void *ctx, dhd_rtt_compl_noti_fn noti_fn);

int
dhd_rtt_unregister_noti_callback(dhd_pub_t *dhd, dhd_rtt_compl_noti_fn noti_fn);

int
dhd_rtt_event_handler(dhd_pub_t *dhd, wl_event_msg_t *event, void *event_data);

int
dhd_rtt_capability(dhd_pub_t *dhd, rtt_capabilities_t *capa);

int
dhd_rtt_avail_channel(dhd_pub_t *dhd, wifi_channel_info *channel_info);

int
dhd_rtt_enable_responder(dhd_pub_t *dhd, wifi_channel_info *channel_info);

int
dhd_rtt_cancel_responder(dhd_pub_t *dhd);

int
dhd_rtt_init(dhd_pub_t *dhd);

int
dhd_rtt_deinit(dhd_pub_t *dhd);

#ifdef WL_CFG80211
int dhd_rtt_handle_nan_rtt_session_end(dhd_pub_t *dhd,
	struct ether_addr *peer);

void dhd_rtt_move_geofence_cur_target_idx_to_next(dhd_pub_t *dhd);

int8 dhd_rtt_get_geofence_cur_target_idx(dhd_pub_t *dhd);
#endif /* WL_CFG80211 */

#endif /* __DHD_RTT_H__ */
