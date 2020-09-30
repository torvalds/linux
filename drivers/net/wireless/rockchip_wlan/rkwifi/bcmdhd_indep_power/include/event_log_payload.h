/* SPDX-License-Identifier: GPL-2.0 */
/*
 * EVENT_LOG System Definitions
 *
 * This file describes the payloads of event log entries that are data buffers
 * rather than formatted string entries. The contents are generally XTLVs.
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
 * $Id: event_log_payload.h 700076 2017-05-17 14:42:22Z $
 */

#ifndef _EVENT_LOG_PAYLOAD_H_
#define _EVENT_LOG_PAYLOAD_H_

#include <typedefs.h>
#include <bcmutils.h>
#include <ethernet.h>
#include <event_log_tag.h>

#define EVENT_LOG_XTLV_ID_STR                   0  /**< XTLV ID for a string */
#define EVENT_LOG_XTLV_ID_TXQ_SUM               1  /**< XTLV ID for txq_summary_t */
#define EVENT_LOG_XTLV_ID_SCBDATA_SUM           2  /**< XTLV ID for cb_subq_summary_t */
#define EVENT_LOG_XTLV_ID_SCBDATA_AMPDU_TX_SUM  3  /**< XTLV ID for scb_ampdu_tx_summary_t */
#define EVENT_LOG_XTLV_ID_BSSCFGDATA_SUM        4  /**< XTLV ID for bsscfg_q_summary_t */
#define EVENT_LOG_XTLV_ID_UCTXSTATUS            5  /**< XTLV ID for ucode TxStatus array */
#define EVENT_LOG_XTLV_ID_TXQ_SUM_V2            6  /**< XTLV ID for txq_summary_v2_t */

/**
 * An XTLV holding a string
 * String is not null terminated, length is the XTLV len.
 */
typedef struct xtlv_string {
	uint16 id;              /* XTLV ID: EVENT_LOG_XTLV_ID_STR */
	uint16 len;             /* XTLV Len (String length) */
	char   str[1];          /* var len array characters */
} xtlv_string_t;

#define XTLV_STRING_FULL_LEN(str_len)     (BCM_XTLV_HDR_SIZE + (str_len) * sizeof(char))

/**
 * Summary for a single TxQ context
 * Two of these will be used per TxQ context---one for the high TxQ, and one for
 * the low txq that contains DMA prepared pkts. The high TxQ is a full multi-precidence
 * queue and also has a BSSCFG map to identify the BSSCFGS associated with the queue context.
 * The low txq counterpart does not populate the BSSCFG map.
 * The excursion queue will have no bsscfgs associated and is the first queue dumped.
 */
typedef struct txq_summary {
	uint16 id;              /* XTLV ID: EVENT_LOG_XTLV_ID_TXQ_SUM */
	uint16 len;             /* XTLV Len */
	uint32 bsscfg_map;      /* bitmap of bsscfg indexes associated with this queue */
	uint32 stopped;         /* flow control bitmap */
	uint8  prec_count;      /* count of precedences/fifos and len of following array */
	uint8  pad;
	uint16 plen[1];         /* var len array of lengths of each prec/fifo in the queue */
} txq_summary_t;

#define TXQ_SUMMARY_LEN                   (OFFSETOF(txq_summary_t, plen))
#define TXQ_SUMMARY_FULL_LEN(num_q)       (TXQ_SUMMARY_LEN + (num_q) * sizeof(uint16))

typedef struct txq_summary_v2 {
	uint16 id;              /* XTLV ID: EVENT_LOG_XTLV_ID_TXQ_SUM_V2 */
	uint16 len;             /* XTLV Len */
	uint32 bsscfg_map;      /* bitmap of bsscfg indexes associated with this queue */
	uint32 stopped;         /* flow control bitmap */
	uint32 hw_stopped;      /* flow control bitmap */
	uint8  prec_count;      /* count of precedences/fifos and len of following array */
	uint8  pad;
	uint16 plen[1];         /* var len array of lengths of each prec/fifo in the queue */
} txq_summary_v2_t;

#define TXQ_SUMMARY_V2_LEN                (OFFSETOF(txq_summary_v2_t, plen))
#define TXQ_SUMMARY_V2_FULL_LEN(num_q)    (TXQ_SUMMARY_V2_LEN + (num_q) * sizeof(uint16))

/**
 * Summary for tx datapath of an SCB cubby
 * This is a generic summary structure (one size fits all) with
 * a cubby ID and sub-ID to differentiate SCB cubby types and possible sub-queues.
 */
typedef struct scb_subq_summary {
	uint16 id;             /* XTLV ID: EVENT_LOG_XTLV_ID_SCBDATA_SUM */
	uint16 len;            /* XTLV Len */
	uint32 flags;          /* cubby specficic flags */
	uint8  cubby_id;       /* ID registered for cubby */
	uint8  sub_id;         /* sub ID if a cubby has more than one queue */
	uint8  prec_count;     /* count of precedences/fifos and len of following array */
	uint8  pad;
	uint16 plen[1];        /* var len array of lengths of each prec/fifo in the queue */
} scb_subq_summary_t;

#define SCB_SUBQ_SUMMARY_LEN              (OFFSETOF(scb_subq_summary_t, plen))
#define SCB_SUBQ_SUMMARY_FULL_LEN(num_q)  (SCB_SUBQ_SUMMARY_LEN + (num_q) * sizeof(uint16))

/* scb_subq_summary_t.flags for APPS */
#define SCBDATA_APPS_F_PS               0x00000001
#define SCBDATA_APPS_F_PSPEND           0x00000002
#define SCBDATA_APPS_F_INPVB            0x00000004
#define SCBDATA_APPS_F_APSD_USP         0x00000008
#define SCBDATA_APPS_F_TXBLOCK          0x00000010
#define SCBDATA_APPS_F_APSD_HPKT_TMR    0x00000020
#define SCBDATA_APPS_F_APSD_TX_PEND     0x00000040
#define SCBDATA_APPS_F_INTRANS          0x00000080
#define SCBDATA_APPS_F_OFF_PEND         0x00000100
#define SCBDATA_APPS_F_OFF_BLOCKED      0x00000200
#define SCBDATA_APPS_F_OFF_IN_PROG      0x00000400


/**
 * Summary for tx datapath AMPDU SCB cubby
 * This is a specific data structure to describe the AMPDU datapath state for an SCB
 * used instead of scb_subq_summary_t.
 * Info is for one TID, so one will be dumped per BA TID active for an SCB.
 */
typedef struct scb_ampdu_tx_summary {
	uint16 id;              /* XTLV ID: EVENT_LOG_XTLV_ID_SCBDATA_AMPDU_TX_SUM */
	uint16 len;             /* XTLV Len */
	uint32 flags;           /* misc flags */
	uint8  tid;             /* initiator TID (priority) */
	uint8  ba_state;        /* internal BA state */
	uint8  bar_cnt;         /* number of bars sent with no progress */
	uint8  retry_bar;       /* reason code if bar to be retried at watchdog */
	uint16 barpending_seq;  /* seqnum for bar */
	uint16 bar_ackpending_seq; /* seqnum of bar for which ack is pending */
	uint16 start_seq;       /* seqnum of the first unacknowledged packet */
	uint16 max_seq;         /* max unacknowledged seqnum sent */
	uint32 released_bytes_inflight; /* Number of bytes pending in bytes */
	uint32 released_bytes_target;
} scb_ampdu_tx_summary_t;

/* scb_ampdu_tx_summary.flags defs */
#define SCBDATA_AMPDU_TX_F_BAR_ACKPEND          0x00000001 /* bar_ackpending */

/** XTLV stuct to summarize a BSSCFG's packet queue */
typedef struct bsscfg_q_summary {
	uint16 id;               /* XTLV ID: EVENT_LOG_XTLV_ID_BSSCFGDATA_SUM */
	uint16 len;              /* XTLV Len */
	struct ether_addr BSSID; /* BSSID */
	uint8  bsscfg_idx;       /* bsscfg index */
	uint8  type;             /* bsscfg type enumeration: BSSCFG_TYPE_XXX */
	uint8  subtype;          /* bsscfg subtype enumeration: BSSCFG_SUBTYPE_XXX */
	uint8  prec_count;       /* count of precedences/fifos and len of following array */
	uint16 plen[1];          /* var len array of lengths of each prec/fifo in the queue */
} bsscfg_q_summary_t;

#define BSSCFG_Q_SUMMARY_LEN              (OFFSETOF(bsscfg_q_summary_t, plen))
#define BSSCFG_Q_SUMMARY_FULL_LEN(num_q)  (BSSCFG_Q_SUMMARY_LEN + (num_q) * sizeof(uint16))

/**
 * An XTLV holding a TxStats array
 * TxStatus entries are 8 or 16 bytes, size in words (2 or 4) givent in
 * entry_size field.
 * Array is uint32 words
 */
typedef struct xtlv_uc_txs {
	uint16 id;              /* XTLV ID: EVENT_LOG_XTLV_ID_UCTXSTATUS */
	uint16 len;             /* XTLV Len */
	uint8  entry_size;      /* num uint32 words per entry */
	uint8  pad[3];          /* reserved, zero */
	uint32 w[1];            /* var len array of words */
} xtlv_uc_txs_t;

#define XTLV_UCTXSTATUS_LEN                (OFFSETOF(xtlv_uc_txs_t, w))
#define XTLV_UCTXSTATUS_FULL_LEN(words)    (XTLV_UCTXSTATUS_LEN + (words) * sizeof(uint32))

#define SCAN_SUMMARY_VERSION	1
/* Scan flags */
#define SCAN_SUM_CHAN_INFO	0x1
/* Scan_sum flags */
#define BAND5G_SIB_ENAB	0x2
#define BAND2G_SIB_ENAB	0x4
#define PARALLEL_SCAN	0x8
#define SCAN_ABORT	0x10

/* scan_channel_info flags */
#define ACTIVE_SCAN_SCN_SUM	0x2
#define SCAN_SUM_WLC_CORE0	0x4
#define SCAN_SUM_WLC_CORE1	0x8
#define HOME_CHAN	0x10

typedef struct wl_scan_ssid_info
{
	uint8		ssid_len;	/* the length of SSID */
	uint8		ssid[32];	/* SSID string */
} wl_scan_ssid_info_t;

typedef struct wl_scan_channel_info {
	uint16 chanspec;	/* chanspec scanned */
	uint16 reserv;
	uint32 start_time;		/* Scan start time in
				* milliseconds for the chanspec
				* or home_dwell time start
				*/
	uint32 end_time;		/* Scan end time in
				* milliseconds for the chanspec
				* or home_dwell time end
				*/
	uint16 probe_count;	/* No of probes sent out. For future use
				*/
	uint16 scn_res_count;	/* Count of scan_results found per
				* channel. For future use
				*/
} wl_scan_channel_info_t;

typedef struct wl_scan_summary_info {
	uint32 total_chan_num;	/* Total number of channels scanned */
	uint32 scan_start_time;	/* Scan start time in milliseconds */
	uint32 scan_end_time;	/* Scan end time in milliseconds */
	wl_scan_ssid_info_t ssid[1];	/* SSID being scanned in current
				* channel. For future use
				*/
} wl_scan_summary_info_t;

struct wl_scan_summary {
	uint8 version;		/* Version */
	uint8 reserved;
	uint16 len;		/* Length of the data buffer including SSID
				 * list.
				 */
	uint16 sync_id;		/* Scan Sync ID */
	uint16 scan_flags;		/* flags [0] or SCAN_SUM_CHAN_INFO = */
				/* channel_info, if not set */
				/* it is scan_summary_info */
				/* when channel_info is used, */
				/* the following flag bits are overridden: */
				/* flags[1] or ACTIVE_SCAN_SCN_SUM = active channel if set */
				/* passive if not set */
				/* flags[2] or WLC_CORE0 = if set, represents wlc_core0 */
				/* flags[3] or WLC_CORE1 = if set, represents wlc_core1 */
				/* flags[4] or HOME_CHAN = if set, represents home-channel */
				/* flags[5:15] = reserved */
				/* when scan_summary_info is used, */
				/* the following flag bits are used: */
				/* flags[1] or BAND5G_SIB_ENAB = */
				/* allowSIBParallelPassiveScan on 5G band */
				/* flags[2] or BAND2G_SIB_ENAB = */
				/* allowSIBParallelPassiveScan on 2G band */
				/* flags[3] or PARALLEL_SCAN = Parallel scan enabled or not */
				/* flags[4] or SCAN_ABORT = SCAN_ABORTED scenario */
				/* flags[5:15] = reserved */
	union {
		wl_scan_channel_info_t scan_chan_info;	/* scan related information
							* for each channel scanned
							*/
		wl_scan_summary_info_t scan_sum_info;	/* Cumulative scan related
							* information.
							*/
	} u;
};

/* Channel switch log record structure
 * Host may map the following structure on channel switch event log record
 * received from dongle. Note that all payload entries in event log record are
 * uint32/int32.
 */
typedef struct wl_chansw_event_log_record {
	uint32 time;			/* Time in us */
	uint32 old_chanspec;		/* Old channel spec */
	uint32 new_chanspec;		/* New channel spec */
	uint32 chansw_reason;		/* Reason for channel change */
	int32 dwell_time;
} wl_chansw_event_log_record_t;

/* Sub-block type for EVENT_LOG_TAG_AMPDU_DUMP */
#define WL_AMPDU_STATS_TYPE_RXMCSx1	0	/* RX MCS rate (Nss = 1) */
#define WL_AMPDU_STATS_TYPE_RXMCSx2	1
#define WL_AMPDU_STATS_TYPE_RXMCSx3	2
#define WL_AMPDU_STATS_TYPE_RXMCSx4	3
#define WL_AMPDU_STATS_TYPE_RXVHTx1	4	/* RX VHT rate (Nss = 1) */
#define WL_AMPDU_STATS_TYPE_RXVHTx2	5
#define WL_AMPDU_STATS_TYPE_RXVHTx3	6
#define WL_AMPDU_STATS_TYPE_RXVHTx4	7
#define WL_AMPDU_STATS_TYPE_TXMCSx1	8	/* TX MCS rate (Nss = 1) */
#define WL_AMPDU_STATS_TYPE_TXMCSx2	9
#define WL_AMPDU_STATS_TYPE_TXMCSx3	10
#define WL_AMPDU_STATS_TYPE_TXMCSx4	11
#define WL_AMPDU_STATS_TYPE_TXVHTx1	12	/* TX VHT rate (Nss = 1) */
#define WL_AMPDU_STATS_TYPE_TXVHTx2	13
#define WL_AMPDU_STATS_TYPE_TXVHTx3	14
#define WL_AMPDU_STATS_TYPE_TXVHTx4	15
#define WL_AMPDU_STATS_TYPE_RXMCSSGI	16	/* RX SGI usage (for all MCS rates) */
#define WL_AMPDU_STATS_TYPE_TXMCSSGI	17	/* TX SGI usage (for all MCS rates) */
#define WL_AMPDU_STATS_TYPE_RXVHTSGI	18	/* RX SGI usage (for all VHT rates) */
#define WL_AMPDU_STATS_TYPE_TXVHTSGI	19	/* TX SGI usage (for all VHT rates) */
#define WL_AMPDU_STATS_TYPE_RXMCSPER	20	/* RX PER (for all MCS rates) */
#define WL_AMPDU_STATS_TYPE_TXMCSPER	21	/* TX PER (for all MCS rates) */
#define WL_AMPDU_STATS_TYPE_RXVHTPER	22	/* RX PER (for all VHT rates) */
#define WL_AMPDU_STATS_TYPE_TXVHTPER	23	/* TX PER (for all VHT rates) */
#define WL_AMPDU_STATS_TYPE_RXDENS	24	/* RX AMPDU density */
#define WL_AMPDU_STATS_TYPE_TXDENS	25	/* TX AMPDU density */
#define WL_AMPDU_STATS_TYPE_RXMCSOK	26	/* RX all MCS rates */
#define WL_AMPDU_STATS_TYPE_RXVHTOK	27	/* RX all VHT rates */
#define WL_AMPDU_STATS_TYPE_TXMCSALL	28	/* TX all MCS rates */
#define WL_AMPDU_STATS_TYPE_TXVHTALL	29	/* TX all VHT rates */
#define WL_AMPDU_STATS_TYPE_TXMCSOK	30	/* TX all MCS rates */
#define WL_AMPDU_STATS_TYPE_TXVHTOK	31	/* TX all VHT rates */

#define WL_AMPDU_STATS_MAX_CNTS		64

typedef struct {
	uint16	type;		/* AMPDU statistics sub-type */
	uint16	len;		/* Number of 32-bit counters */
	uint32	counters[WL_AMPDU_STATS_MAX_CNTS];
} wl_ampdu_stats_generic_t;

typedef struct {
	uint16	type;		/* AMPDU statistics sub-type */
	uint16	len;		/* Number of 32-bit counters + 2 */
	uint32	total_ampdu;
	uint32	total_mpdu;
	uint32	aggr_dist[WL_AMPDU_STATS_MAX_CNTS + 1];
} wl_ampdu_stats_aggrsz_t;

/* Sub-block type for EVENT_LOG_TAG_MSCHPROFILE */
#define WL_MSCH_PROFILER_START		0	/* start event check */
#define WL_MSCH_PROFILER_EXIT		1	/* exit event check */
#define WL_MSCH_PROFILER_REQ		2	/* request event */
#define WL_MSCH_PROFILER_CALLBACK	3	/* call back event */
#define WL_MSCH_PROFILER_MESSAGE	4	/* message event */
#define WL_MSCH_PROFILER_PROFILE_START	5
#define WL_MSCH_PROFILER_PROFILE_END	6
#define WL_MSCH_PROFILER_REQ_HANDLE	7
#define WL_MSCH_PROFILER_REQ_ENTITY	8
#define WL_MSCH_PROFILER_CHAN_CTXT	9
#define WL_MSCH_PROFILER_EVENT_LOG	10
#define WL_MSCH_PROFILER_REQ_TIMING	11
#define WL_MSCH_PROFILER_TYPE_MASK	0x00ff
#define WL_MSCH_PROFILER_WLINDEX_SHIFT	8
#define WL_MSCH_PROFILER_WLINDEX_MASK	0x0f00
#define WL_MSCH_PROFILER_VER_SHIFT	12
#define WL_MSCH_PROFILER_VER_MASK	0xf000

/* MSCH Event data current verion */
#define WL_MSCH_PROFILER_VER		2

/* msch version history */
#define WL_MSCH_PROFILER_RSDB_VER	1
#define WL_MSCH_PROFILER_REPORT_VER	2

/* msch collect header size */
#define WL_MSCH_PROFILE_HEAD_SIZE	OFFSETOF(msch_collect_tlv_t, value)

/* msch event log header size */
#define WL_MSCH_EVENT_LOG_HEAD_SIZE	OFFSETOF(msch_event_log_profiler_event_data_t, data)

/* MSCH data buffer size */
#define WL_MSCH_PROFILER_BUFFER_SIZE	512

/* request type used in wlc_msch_req_param_t struct */
#define WL_MSCH_RT_BOTH_FIXED	0	/* both start and end time is fixed */
#define WL_MSCH_RT_START_FLEX	1	/* start time is flexible and duration is fixed */
#define WL_MSCH_RT_DUR_FLEX	2	/* start time is fixed and end time is flexible */
#define WL_MSCH_RT_BOTH_FLEX	3	/* Both start and duration is flexible */

/* Flags used in wlc_msch_req_param_t struct */
#define WL_MSCH_REQ_FLAGS_CHAN_CONTIGUOUS  (1 << 0) /* Don't break up channels in chanspec_list */
#define WL_MSCH_REQ_FLAGS_MERGE_CONT_SLOTS (1 << 1)  /* No slot end if slots are continous */
#define WL_MSCH_REQ_FLAGS_PREMTABLE        (1 << 2) /* Req can be pre-empted by PREMT_CURTS req */
#define WL_MSCH_REQ_FLAGS_PREMT_CURTS      (1 << 3) /* Pre-empt request at the end of curts */
#define WL_MSCH_REQ_FLAGS_PREMT_IMMEDIATE  (1 << 4) /* Pre-empt cur_ts immediately */

/* Requested slot Callback states
 * req->pend_slot/cur_slot->flags
 */
#define WL_MSCH_RC_FLAGS_ONCHAN_FIRE		(1 << 0)
#define WL_MSCH_RC_FLAGS_START_FIRE_DONE	(1 << 1)
#define WL_MSCH_RC_FLAGS_END_FIRE_DONE		(1 << 2)
#define WL_MSCH_RC_FLAGS_ONFIRE_DONE		(1 << 3)
#define WL_MSCH_RC_FLAGS_SPLIT_SLOT_START	(1 << 4)
#define WL_MSCH_RC_FLAGS_SPLIT_SLOT_END		(1 << 5)
#define WL_MSCH_RC_FLAGS_PRE_ONFIRE_DONE	(1 << 6)

/* Request entity flags */
#define WL_MSCH_ENTITY_FLAG_MULTI_INSTANCE	(1 << 0)

/* Request Handle flags */
#define WL_MSCH_REQ_HDL_FLAGS_NEW_REQ		(1 << 0) /* req_start callback */

/* MSCH state flags (msch_info->flags) */
#define	WL_MSCH_STATE_IN_TIEMR_CTXT		0x1
#define WL_MSCH_STATE_SCHD_PENDING		0x2

/* MSCH callback type */
#define	WL_MSCH_CT_REQ_START		0x1
#define	WL_MSCH_CT_ON_CHAN		0x2
#define	WL_MSCH_CT_SLOT_START		0x4
#define	WL_MSCH_CT_SLOT_END		0x8
#define	WL_MSCH_CT_SLOT_SKIP		0x10
#define	WL_MSCH_CT_OFF_CHAN		0x20
#define WL_MSCH_CT_OFF_CHAN_DONE	0x40
#define	WL_MSCH_CT_REQ_END		0x80
#define	WL_MSCH_CT_PARTIAL		0x100
#define	WL_MSCH_CT_PRE_ONCHAN		0x200
#define	WL_MSCH_CT_PRE_REQ_START	0x400

/* MSCH command bits */
#define WL_MSCH_CMD_ENABLE_BIT		0x01
#define WL_MSCH_CMD_PROFILE_BIT		0x02
#define WL_MSCH_CMD_CALLBACK_BIT	0x04
#define WL_MSCH_CMD_REGISTER_BIT	0x08
#define WL_MSCH_CMD_ERROR_BIT		0x10
#define WL_MSCH_CMD_DEBUG_BIT		0x20
#define WL_MSCH_CMD_INFOM_BIT		0x40
#define WL_MSCH_CMD_TRACE_BIT		0x80
#define WL_MSCH_CMD_ALL_BITS		0xfe
#define WL_MSCH_CMD_SIZE_MASK		0x00ff0000
#define WL_MSCH_CMD_SIZE_SHIFT		16
#define WL_MSCH_CMD_VER_MASK		0xff000000
#define WL_MSCH_CMD_VER_SHIFT		24

/* maximum channels returned by the get valid channels iovar */
#define WL_MSCH_NUMCHANNELS		64

typedef struct msch_collect_tlv {
	uint16	type;
	uint16	size;
	char	value[1];
} msch_collect_tlv_t;

typedef struct msch_profiler_event_data {
	uint32	time_lo;		/* Request time */
	uint32	time_hi;
} msch_profiler_event_data_t;

typedef struct msch_start_profiler_event_data {
	uint32	time_lo;		/* Request time */
	uint32	time_hi;
	uint32	status;
} msch_start_profiler_event_data_t;

typedef struct msch_message_profiler_event_data {
	uint32	time_lo;		/* Request time */
	uint32	time_hi;
	char	message[1];		/* message */
} msch_message_profiler_event_data_t;

typedef struct msch_event_log_profiler_event_data {
	uint32	time_lo;		/* Request time */
	uint32	time_hi;
	event_log_hdr_t hdr;		/* event log header */
	uint32	data[9];		/* event data */
} msch_event_log_profiler_event_data_t;

typedef struct msch_req_param_profiler_event_data {
	uint16  flags;			/* Describe various request properties */
	uint8	req_type;		/* Describe start and end time flexiblilty */
	uint8	priority;		/* Define the request priority */
	uint32	start_time_l;		/* Requested start time offset in us unit */
	uint32	start_time_h;
	uint32	duration;		/* Requested duration in us unit */
	uint32	interval;		/* Requested periodic interval in us unit,
					 * 0 means non-periodic
					 */
	union {
		uint32	dur_flex;	/* MSCH_REG_DUR_FLEX, min_dur = duration - dur_flex */
		struct {
			uint32 min_dur;	/* min duration for traffic, maps to home_time */
			uint32 max_away_dur; /* max acceptable away dur, maps to home_away_time */
			uint32 hi_prio_time_l;
			uint32 hi_prio_time_h;
			uint32 hi_prio_interval; /* repeated high priority interval */
		} bf;
	} flex;
} msch_req_param_profiler_event_data_t;

typedef struct msch_req_timing_profiler_event_data {
	uint32 p_req_timing;
	uint32 p_prev;
	uint32 p_next;
	uint16 flags;
	uint16 timeslot_ptr;
	uint32 fire_time_l;
	uint32 fire_time_h;
	uint32 pre_start_time_l;
	uint32 pre_start_time_h;
	uint32 start_time_l;
	uint32 start_time_h;
	uint32 end_time_l;
	uint32 end_time_h;
	uint32 p_timeslot;
} msch_req_timing_profiler_event_data_t;

typedef struct msch_chan_ctxt_profiler_event_data {
	uint32 p_chan_ctxt;
	uint32 p_prev;
	uint32 p_next;
	uint16 chanspec;
	uint16 bf_sch_pending;
	uint32 bf_link_prev;
	uint32 bf_link_next;
	uint32 onchan_time_l;
	uint32 onchan_time_h;
	uint32 actual_onchan_dur_l;
	uint32 actual_onchan_dur_h;
	uint32 pend_onchan_dur_l;
	uint32 pend_onchan_dur_h;
	uint16 req_entity_list_cnt;
	uint16 req_entity_list_ptr;
	uint16 bf_entity_list_cnt;
	uint16 bf_entity_list_ptr;
	uint32 bf_skipped_count;
} msch_chan_ctxt_profiler_event_data_t;

typedef struct msch_req_entity_profiler_event_data {
	uint32 p_req_entity;
	uint32 req_hdl_link_prev;
	uint32 req_hdl_link_next;
	uint32 chan_ctxt_link_prev;
	uint32 chan_ctxt_link_next;
	uint32 rt_specific_link_prev;
	uint32 rt_specific_link_next;
	uint32 start_fixed_link_prev;
	uint32 start_fixed_link_next;
	uint32 both_flex_list_prev;
	uint32 both_flex_list_next;
	uint16 chanspec;
	uint16 priority;
	uint16 cur_slot_ptr;
	uint16 pend_slot_ptr;
	uint16 pad;
	uint16 chan_ctxt_ptr;
	uint32 p_chan_ctxt;
	uint32 p_req_hdl;
	uint32 bf_last_serv_time_l;
	uint32 bf_last_serv_time_h;
	uint16 onchan_chn_idx;
	uint16 cur_chn_idx;
	uint32 flags;
	uint32 actual_start_time_l;
	uint32 actual_start_time_h;
	uint32 curts_fire_time_l;
	uint32 curts_fire_time_h;
} msch_req_entity_profiler_event_data_t;

typedef struct msch_req_handle_profiler_event_data {
	uint32 p_req_handle;
	uint32 p_prev;
	uint32 p_next;
	uint32 cb_func;
	uint32 cb_ctxt;
	uint16 req_param_ptr;
	uint16 req_entity_list_cnt;
	uint16 req_entity_list_ptr;
	uint16 chan_cnt;
	uint32 flags;
	uint16 chanspec_list;
	uint16 chanspec_cnt;
	uint16 chan_idx;
	uint16 last_chan_idx;
	uint32 req_time_l;
	uint32 req_time_h;
} msch_req_handle_profiler_event_data_t;

typedef struct msch_profiler_profiler_event_data {
	uint32 time_lo;			/* Request time */
	uint32 time_hi;
	uint32 free_req_hdl_list;
	uint32 free_req_entity_list;
	uint32 free_chan_ctxt_list;
	uint32 free_chanspec_list;
	uint16 cur_msch_timeslot_ptr;
	uint16 next_timeslot_ptr;
	uint32 p_cur_msch_timeslot;
	uint32 p_next_timeslot;
	uint32 cur_armed_timeslot;
	uint32 flags;
	uint32 ts_id;
	uint32 service_interval;
	uint32 max_lo_prio_interval;
	uint16 flex_list_cnt;
	uint16 msch_chanspec_alloc_cnt;
	uint16 msch_req_entity_alloc_cnt;
	uint16 msch_req_hdl_alloc_cnt;
	uint16 msch_chan_ctxt_alloc_cnt;
	uint16 msch_timeslot_alloc_cnt;
	uint16 msch_req_hdl_list_cnt;
	uint16 msch_req_hdl_list_ptr;
	uint16 msch_chan_ctxt_list_cnt;
	uint16 msch_chan_ctxt_list_ptr;
	uint16 msch_req_timing_list_cnt;
	uint16 msch_req_timing_list_ptr;
	uint16 msch_start_fixed_list_cnt;
	uint16 msch_start_fixed_list_ptr;
	uint16 msch_both_flex_req_entity_list_cnt;
	uint16 msch_both_flex_req_entity_list_ptr;
	uint16 msch_start_flex_list_cnt;
	uint16 msch_start_flex_list_ptr;
	uint16 msch_both_flex_list_cnt;
	uint16 msch_both_flex_list_ptr;
	uint32 slotskip_flag;
} msch_profiler_profiler_event_data_t;

typedef struct msch_req_profiler_event_data {
	uint32 time_lo;			/* Request time */
	uint32 time_hi;
	uint16 chanspec_cnt;
	uint16 chanspec_ptr;
	uint16 req_param_ptr;
	uint16 pad;
} msch_req_profiler_event_data_t;

typedef struct msch_callback_profiler_event_data {
	uint32 time_lo;			/* Request time */
	uint32 time_hi;
	uint16 type;			/* callback type */
	uint16 chanspec;		/* actual chanspec, may different with requested one */
	uint32 start_time_l;		/* time slot start time low 32bit */
	uint32 start_time_h;		/* time slot start time high 32bit */
	uint32 end_time_l;		/* time slot end time low 32 bit */
	uint32 end_time_h;		/* time slot end time high 32 bit */
	uint32 timeslot_id;		/* unique time slot id */
	uint32 p_req_hdl;
	uint32 onchan_idx;		/* Current channel index */
	uint32 cur_chan_seq_start_time_l; /* start time of current sequence */
	uint32 cur_chan_seq_start_time_h;
} msch_callback_profiler_event_data_t;

typedef struct msch_timeslot_profiler_event_data {
	uint32 p_timeslot;
	uint32 timeslot_id;
	uint32 pre_start_time_l;
	uint32 pre_start_time_h;
	uint32 end_time_l;
	uint32 end_time_h;
	uint32 sch_dur_l;
	uint32 sch_dur_h;
	uint32 p_chan_ctxt;
	uint32 fire_time_l;
	uint32 fire_time_h;
	uint32 state;
} msch_timeslot_profiler_event_data_t;

typedef struct msch_register_params	{
	uint16 wlc_index;		/* Optional wlc index */
	uint16 flags;			/* Describe various request properties */
	uint32 req_type;		/* Describe start and end time flexiblilty */
	uint16 id;			/* register id */
	uint16 priority;		/* Define the request priority */
	uint32 start_time;		/* Requested start time offset in ms unit */
	uint32 duration;		/* Requested duration in ms unit */
	uint32 interval;		/* Requested periodic interval in ms unit,
					 * 0 means non-periodic
					 */
	uint32 dur_flex;		/* MSCH_REG_DUR_FLEX, min_dur = duration - dur_flex */
	uint32 min_dur;			/* min duration for traffic, maps to home_time */
	uint32 max_away_dur;		/* max acceptable away dur, maps to home_away_time */
	uint32 hi_prio_time;
	uint32 hi_prio_interval;	/* repeated high priority interval */
	uint32 chanspec_cnt;
	uint16 chanspec_list[WL_MSCH_NUMCHANNELS];
} msch_register_params_t;

#endif /* _EVENT_LOG_PAYLOAD_H_ */
