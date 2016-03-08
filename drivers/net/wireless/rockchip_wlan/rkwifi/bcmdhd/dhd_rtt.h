/*
 * Broadcom Dongle Host Driver (DHD), RTT
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
 * $Id: dhd_rtt.h 558438 2015-05-22 06:05:11Z $
 */
#ifndef __DHD_RTT_H__
#define __DHD_RTT_H__

#include "dngl_stats.h"

#define RTT_MAX_TARGET_CNT	10
#define RTT_MAX_FRAME_CNT	25
#define RTT_MAX_RETRY_CNT	10
#define DEFAULT_FTM_CNT		6
#define DEFAULT_RETRY_CNT	6


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


enum rtt_role {
	RTT_INITIATOR = 0,
	RTT_TARGET = 1
};
enum rtt_status {
	RTT_STOPPED = 0,
	RTT_STARTED = 1
};
typedef int64_t wifi_timestamp; /* In microseconds (us) */
typedef int64_t wifi_timespan;
typedef int wifi_rssi;

typedef enum {
	RTT_INVALID,
	RTT_ONE_WAY,
	RTT_TWO_WAY,
	RTT_AUTO
} rtt_type_t;

typedef enum {
	RTT_PEER_STA,
	RTT_PEER_AP,
	RTT_PEER_P2P,
	RTT_PEER_NAN,
	RTT_PEER_INVALID
} rtt_peer_type_t;

typedef enum rtt_reason {
	RTT_REASON_SUCCESS,
	RTT_REASON_FAILURE,
	RTT_REASON_NO_RSP,
	RTT_REASON_REJECTED,
	RTT_REASON_NOT_SCHEDULED_YET,
	RTT_REASON_TIMEOUT,
	RTT_REASON_AP_ON_DIFF_CH,
	RTT_REASON_AP_NO_CAP,
	RTT_REASON_ABORT
} rtt_reason_t;

typedef enum rtt_capability {
	RTT_CAP_NONE = 0,
	RTT_CAP_ONE_WAY	 = (1 << (0)),
	RTT_CAP_11V_WAY  = (1 << (1)),  /* IEEE802.11v */
	RTT_CAP_11MC_WAY  = (1 << (2)), /* IEEE802.11mc */
	RTT_CAP_VS_WAY = (1 << (3)) /* BRCM vendor specific */
} rtt_capability_t;

typedef struct wifi_channel_info {
	wifi_channel_width_t width;
	wifi_channel center_freq; /* primary 20 MHz channel */
	wifi_channel center_freq0; /* center freq (MHz) first segment */
	wifi_channel center_freq1; /* center freq (MHz) second segment valid for 80 + 80 */
} wifi_channel_info_t;

typedef struct wifi_rate {
	uint32 preamble :3; /* 0: OFDM, 1: CCK, 2 : HT, 3: VHT, 4..7 reserved */
	uint32 nss		:2; /* 0 : 1x1, 1: 2x2, 3: 3x3, 4: 4x4 */
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
	rtt_type_t type; /* rtt_type */
	rtt_peer_type_t peer; /* peer type */
	wifi_channel_info_t channel; /* channel information */
	chanspec_t chanspec; /* chanspec for channel */
	int8	continuous; /* 0 = single shot or 1 = continous raging */
	bool	disable; /* disable for RTT measurement */
	uint32	interval; /* interval of RTT measurement (unit ms) when continuous = true */
	uint32	measure_cnt; /* total number of RTT measurement when continuous */
	uint32	ftm_cnt; /* num of packets in each RTT measurement */
	uint32	retry_cnt; /* num of retries if sampling fails */
} rtt_target_info_t;

typedef struct rtt_result {
	struct list_head list;
	uint16 ver;			/* version */
	rtt_target_info_t *target_info; /* target info */
	uint16 mode;			/* mode: target/initiator */
	uint16 method;			/* method: rssi/TOF/AOA */
	uint8  err_code;		/* error classification */
	uint8  TOF_type;		/* one way or two way TOF */
	wifi_rate_t tx_rate;           /* tx rate */
	struct ether_addr peer_mac;	/* (e.g for tgt:initiator's */
	int32 distance;		/* dst to tgt, units (meter * 16) */
	uint32 meanrtt;			/* mean delta */
	uint32 modertt;			/* Mode delta */
	uint32 medianrtt;		/* median RTT */
	uint32 sdrtt;			/* Standard deviation of RTT */
	int16  avg_rssi;		/* avg rssi across the ftm frames */
	int16  validfrmcnt;		/* Firmware's valid frame counts */
	wifi_timestamp ts; /* the time elapsed from boot time when driver get this result */
	uint16 ftm_cnt;			/*  num of rtd measurments/length in the ftm buffer  */
	ftm_sample_t ftm_buff[1];	/* 1 ... ftm_cnt  */
} rtt_result_t;

typedef struct rtt_report {
	struct ether_addr addr;
	uint num_measurement; /* measurement number in case of continous raging */
	rtt_reason_t status; /* raging status */
	rtt_type_t type; /* rtt type */
	rtt_peer_type_t peer; /* peer type */
	wifi_channel_info_t channel; /* channel information */
	wifi_rssi  rssi; /* avg rssi accroos the ftm frames */
	wifi_rssi  rssi_spread; /* rssi spread in 0.5 db steps e.g. 5 implies 2.5 spread */
	wifi_rate_t tx_rate;           /* tx rate */
	wifi_timespan rtt;	/*  round trip time in nanoseconds */
	wifi_timespan rtt_sd;	/* rtt standard deviation in nanoseconds */
	wifi_timespan rtt_spread; /* difference between max and min rtt times recorded */
	int32 distance; /* distance in cm (optional) */
	int32 distance_sd; /* standard deviation in cm (optional) */
	int32 distance_spread; /* difference between max and min distance recorded (optional) */
	wifi_timestamp ts; /* time of the measurement (in microseconds since boot) */
} rtt_report_t;

/* RTT Capabilities */
typedef struct rtt_capabilities {
	uint8 rtt_one_sided_supported;  /* if 1-sided rtt data collection is supported */
	uint8 rtt_11v_supported;        /* if 11v rtt data collection is supported */
	uint8 rtt_ftm_supported;        /* if ftm rtt data collection is supported */
	uint8 rtt_vs_supported;		/* if vendor specific data collection supported */
} rtt_capabilities_t;

typedef struct rtt_config_params {
	int8 rtt_target_cnt;
	rtt_target_info_t target_info[RTT_MAX_TARGET_CNT];
} rtt_config_params_t;

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

/* export to upper layer */
chanspec_t
dhd_rtt_convert_to_chspec(wifi_channel_info_t channel);

int
dhd_rtt_set_cfg(dhd_pub_t *dhd, rtt_config_params_t *params);

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
dhd_rtt_init(dhd_pub_t *dhd);

int
dhd_rtt_deinit(dhd_pub_t *dhd);
#endif /* __DHD_RTT_H__ */
