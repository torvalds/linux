/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Broadcom Dongle Host Driver (DHD), RTT
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
 * $Id: dhd_rtt.c 674349 2017-10-19 07:59:24Z $
 */
#include <typedefs.h>
#include <osl.h>

#include <epivers.h>
#include <bcmutils.h>

#include <bcmendian.h>
#include <linuxver.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/sort.h>
#include <dngl_stats.h>
#include <wlioctl.h>

#include <proto/bcmevent.h>
#include <dhd.h>
#include <dhd_rtt.h>
#include <dhd_dbg.h>
#define GET_RTTSTATE(dhd) ((rtt_status_info_t *)dhd->rtt_state)
static DEFINE_SPINLOCK(noti_list_lock);
#define NULL_CHECK(p, s, err)  \
			do { \
				if (!(p)) { \
					printf("NULL POINTER (%s) : %s\n", __FUNCTION__, (s)); \
					err = BCME_ERROR; \
					return err; \
				} \
			} while (0)

#define RTT_IS_ENABLED(rtt_status) (rtt_status->status == RTT_ENABLED)
#define RTT_IS_STOPPED(rtt_status) (rtt_status->status == RTT_STOPPED)
#define TIMESPEC_TO_US(ts)  (((uint64)(ts).tv_sec * USEC_PER_SEC) + \
							(ts).tv_nsec / NSEC_PER_USEC)

#define FTM_IOC_BUFSZ  2048	/* ioc buffsize for our module (> BCM_XTLV_HDR_SIZE) */
#define FTM_AVAIL_MAX_SLOTS		32
#define FTM_MAX_CONFIGS 10
#define FTM_MAX_PARAMS 10
#define FTM_DEFAULT_SESSION 1
#define FTM_BURST_TIMEOUT_UNIT 250 /* 250 ns */
#define FTM_INVALID -1
#define	FTM_DEFAULT_CNT_20M		12
#define FTM_DEFAULT_CNT_40M		10
#define FTM_DEFAULT_CNT_80M		5

/* convenience macros */
#define FTM_TU2MICRO(_tu) ((uint64)(_tu) << 10)
#define FTM_MICRO2TU(_tu) ((uint64)(_tu) >> 10)
#define FTM_TU2MILLI(_tu) ((uint32)FTM_TU2MICRO(_tu) / 1000)
#define FTM_MICRO2MILLI(_x) ((uint32)(_x) / 1000)
#define FTM_MICRO2SEC(_x) ((uint32)(_x) / 1000000)
#define FTM_INTVL2NSEC(_intvl) ((uint32)ftm_intvl2nsec(_intvl))
#define FTM_INTVL2USEC(_intvl) ((uint32)ftm_intvl2usec(_intvl))
#define FTM_INTVL2MSEC(_intvl) (FTM_INTVL2USEC(_intvl) / 1000)
#define FTM_INTVL2SEC(_intvl) (FTM_INTVL2USEC(_intvl) / 1000000)
#define FTM_USECIN100MILLI(_usec) ((_usec) / 100000)

/* broadcom specific set to have more accurate data */
#define ENABLE_VHT_ACK

struct rtt_noti_callback {
	struct list_head list;
	void *ctx;
	dhd_rtt_compl_noti_fn noti_fn;
};

typedef struct rtt_status_info {
	dhd_pub_t *dhd;
	int8 status;   /* current status for the current entry */
	int8 txchain; /* current device tx chain */
	int8 mpc; /* indicate we change mpc mode */
	int8 cur_idx; /* current entry to do RTT */
	bool all_cancel; /* cancel all request once we got the cancel requet */
	struct capability {
		int32 proto	:8;
		int32 feature	:8;
		int32 preamble	:8;
		int32 bw	:8;
	} rtt_capa; /* rtt capability */
	struct mutex rtt_mutex;
	rtt_config_params_t rtt_config;
	struct work_struct work;
	struct list_head noti_fn_list;
	struct list_head rtt_results_cache; /* store results for RTT */
} rtt_status_info_t;

/* bitmask indicating which command groups; */
typedef enum {
	FTM_SUBCMD_FLAG_METHOD	= 0x01,	/* FTM method command */
	FTM_SUBCMD_FLAG_SESSION	= 0x02,	/* FTM session command */
	FTM_SUBCMD_FLAG_ALL = FTM_SUBCMD_FLAG_METHOD | FTM_SUBCMD_FLAG_SESSION
} ftm_subcmd_flag_t;

/* proxd ftm config-category definition */
typedef enum {
	FTM_CONFIG_CAT_GENERAL = 1,	/* generial configuration */
	FTM_CONFIG_CAT_OPTIONS = 2,	/* 'config options' */
	FTM_CONFIG_CAT_AVAIL = 3,	/* 'config avail' */
} ftm_config_category_t;


typedef struct ftm_subcmd_info {
	int16				version;    /* FTM version (optional) */
	char				*name;		/* cmd-name string as cmdline input */
	wl_proxd_cmd_t		cmdid;		/* cmd-id */
	bcm_xtlv_unpack_cbfn_t *handler;  /* cmd response handler (optional) */
	ftm_subcmd_flag_t	cmdflag; /* CMD flag (optional)  */
} ftm_subcmd_info_t;


typedef struct ftm_config_options_info {
	uint32 flags;		/* wl_proxd_flags_t/wl_proxd_session_flags_t */
	bool enable;
} ftm_config_options_info_t;

typedef struct ftm_config_param_info {
	uint16		tlvid;	/* mapping TLV id for the item */
	union {
		uint32  chanspec;
		struct ether_addr mac_addr;
		wl_proxd_intvl_t data_intvl;
		uint32 data32;
		uint16 data16;
		uint8 data8;
	};
} ftm_config_param_info_t;

/*
* definition for id-string mapping.
* This is used to map an id (can be cmd-id, tlv-id, ....) to a text-string
* for debug-display or cmd-log-display
*/
typedef struct ftm_strmap_entry {
	int32		id;
	char		*text;
} ftm_strmap_entry_t;


typedef struct ftm_status_map_host_entry {
	wl_proxd_status_t proxd_status;
	rtt_reason_t rtt_reason;
} ftm_status_map_host_entry_t;

static int
dhd_rtt_convert_results_to_host(rtt_report_t *rtt_report, uint8 *p_data, uint16 tlvid, uint16 len);

static wifi_rate_t
dhd_rtt_convert_rate_to_host(uint32 ratespec);

static int
dhd_rtt_start(dhd_pub_t *dhd);
static const int burst_duration_idx[]  = {0, 0, 1, 2, 4, 8, 16, 32, 64, 128, 0, 0};

/* ftm status mapping to host status */
static const ftm_status_map_host_entry_t ftm_status_map_info[] = {
	{WL_PROXD_E_INCOMPLETE, RTT_REASON_FAILURE},
	{WL_PROXD_E_OVERRIDDEN, RTT_REASON_FAILURE},
	{WL_PROXD_E_ASAP_FAILED, RTT_REASON_FAILURE},
	{WL_PROXD_E_NOTSTARTED, RTT_REASON_FAIL_NOT_SCHEDULED_YET},
	{WL_PROXD_E_INVALIDAVB, RTT_REASON_FAIL_INVALID_TS},
	{WL_PROXD_E_INCAPABLE, RTT_REASON_FAIL_NO_CAPABILITY},
	{WL_PROXD_E_MISMATCH, RTT_REASON_FAILURE},
	{WL_PROXD_E_DUP_SESSION, RTT_REASON_FAILURE},
	{WL_PROXD_E_REMOTE_FAIL, RTT_REASON_FAILURE},
	{WL_PROXD_E_REMOTE_INCAPABLE, RTT_REASON_FAILURE},
	{WL_PROXD_E_SCHED_FAIL, RTT_REASON_FAIL_SCHEDULE},
	{WL_PROXD_E_PROTO, RTT_REASON_FAIL_PROTOCOL},
	{WL_PROXD_E_EXPIRED, RTT_REASON_FAILURE},
	{WL_PROXD_E_TIMEOUT, RTT_REASON_FAIL_TM_TIMEOUT},
	{WL_PROXD_E_NOACK, RTT_REASON_FAIL_NO_RSP},
	{WL_PROXD_E_DEFERRED, RTT_REASON_FAILURE},
	{WL_PROXD_E_INVALID_SID, RTT_REASON_FAILURE},
	{WL_PROXD_E_REMOTE_CANCEL, RTT_REASON_FAILURE},
	{WL_PROXD_E_CANCELED, RTT_REASON_ABORTED},
	{WL_PROXD_E_INVALID_SESSION, RTT_REASON_FAILURE},
	{WL_PROXD_E_BAD_STATE, RTT_REASON_FAILURE},
	{WL_PROXD_E_ERROR, RTT_REASON_FAILURE},
	{WL_PROXD_E_OK, RTT_REASON_SUCCESS}
};

/* ftm tlv-id mapping */
static const ftm_strmap_entry_t ftm_tlvid_loginfo[] = {
	/* { WL_PROXD_TLV_ID_xxx,				"text for WL_PROXD_TLV_ID_xxx" }, */
	{ WL_PROXD_TLV_ID_NONE,					"none" },
	{ WL_PROXD_TLV_ID_METHOD,				"method" },
	{ WL_PROXD_TLV_ID_FLAGS,				"flags" },
	{ WL_PROXD_TLV_ID_CHANSPEC,				"chanspec" },
	{ WL_PROXD_TLV_ID_TX_POWER,				"tx power" },
	{ WL_PROXD_TLV_ID_RATESPEC,				"ratespec" },
	{ WL_PROXD_TLV_ID_BURST_DURATION,		"burst duration" },
	{ WL_PROXD_TLV_ID_BURST_PERIOD,			"burst period" },
	{ WL_PROXD_TLV_ID_BURST_FTM_SEP,		"burst ftm sep" },
	{ WL_PROXD_TLV_ID_BURST_NUM_FTM,		"burst num ftm" },
	{ WL_PROXD_TLV_ID_NUM_BURST,			"num burst" },
	{ WL_PROXD_TLV_ID_FTM_RETRIES,			"ftm retries" },
	{ WL_PROXD_TLV_ID_BSS_INDEX,			"BSS index" },
	{ WL_PROXD_TLV_ID_BSSID,				"bssid" },
	{ WL_PROXD_TLV_ID_INIT_DELAY,			"burst init delay" },
	{ WL_PROXD_TLV_ID_BURST_TIMEOUT,		"burst timeout" },
	{ WL_PROXD_TLV_ID_EVENT_MASK,			"event mask" },
	{ WL_PROXD_TLV_ID_FLAGS_MASK,			"flags mask" },
	{ WL_PROXD_TLV_ID_PEER_MAC,				"peer addr" },
	{ WL_PROXD_TLV_ID_FTM_REQ,				"ftm req" },
	{ WL_PROXD_TLV_ID_LCI_REQ,				"lci req" },
	{ WL_PROXD_TLV_ID_LCI,					"lci" },
	{ WL_PROXD_TLV_ID_CIVIC_REQ,			"civic req" },
	{ WL_PROXD_TLV_ID_CIVIC,				"civic" },
	{ WL_PROXD_TLV_ID_AVAIL,				"availability" },
	{ WL_PROXD_TLV_ID_SESSION_FLAGS,		"session flags" },
	{ WL_PROXD_TLV_ID_SESSION_FLAGS_MASK,	"session flags mask" },
	{ WL_PROXD_TLV_ID_RX_MAX_BURST,			"rx max bursts" },
	{ WL_PROXD_TLV_ID_RANGING_INFO,			"ranging info" },
	{ WL_PROXD_TLV_ID_RANGING_FLAGS,		"ranging flags" },
	{ WL_PROXD_TLV_ID_RANGING_FLAGS_MASK,	"ranging flags mask" },
	/* output - 512 + x */
	{ WL_PROXD_TLV_ID_STATUS,				"status" },
	{ WL_PROXD_TLV_ID_COUNTERS,				"counters" },
	{ WL_PROXD_TLV_ID_INFO,					"info" },
	{ WL_PROXD_TLV_ID_RTT_RESULT,			"rtt result" },
	{ WL_PROXD_TLV_ID_AOA_RESULT,			"aoa result" },
	{ WL_PROXD_TLV_ID_SESSION_INFO,			"session info" },
	{ WL_PROXD_TLV_ID_SESSION_STATUS,		"session status" },
	{ WL_PROXD_TLV_ID_SESSION_ID_LIST,		"session ids" },
	/* debug tlvs can be added starting 1024 */
	{ WL_PROXD_TLV_ID_DEBUG_MASK,			"debug mask" },
	{ WL_PROXD_TLV_ID_COLLECT,				"collect" },
	{ WL_PROXD_TLV_ID_STRBUF,				"result" }
};

static const ftm_strmap_entry_t ftm_event_type_loginfo[] = {
	/* wl_proxd_event_type_t,			text-string */
	{ WL_PROXD_EVENT_NONE,				"none" },
	{ WL_PROXD_EVENT_SESSION_CREATE,	"session create" },
	{ WL_PROXD_EVENT_SESSION_START,		"session start" },
	{ WL_PROXD_EVENT_FTM_REQ,			"FTM req" },
	{ WL_PROXD_EVENT_BURST_START,		"burst start" },
	{ WL_PROXD_EVENT_BURST_END,			"burst end" },
	{ WL_PROXD_EVENT_SESSION_END,		"session end" },
	{ WL_PROXD_EVENT_SESSION_RESTART,	"session restart" },
	{ WL_PROXD_EVENT_BURST_RESCHED,		"burst rescheduled" },
	{ WL_PROXD_EVENT_SESSION_DESTROY,	"session destroy" },
	{ WL_PROXD_EVENT_RANGE_REQ,			"range request" },
	{ WL_PROXD_EVENT_FTM_FRAME,			"FTM frame" },
	{ WL_PROXD_EVENT_DELAY,				"delay" },
	{ WL_PROXD_EVENT_VS_INITIATOR_RPT,	"initiator-report " }, /* rx initiator-rpt */
	{ WL_PROXD_EVENT_RANGING,			"ranging " },
};

/*
* session-state --> text string mapping
*/
static const ftm_strmap_entry_t ftm_session_state_value_loginfo[] = {
	/* wl_proxd_session_state_t,			text string */
	{ WL_PROXD_SESSION_STATE_CREATED,		"created" },
	{ WL_PROXD_SESSION_STATE_CONFIGURED,	"configured" },
	{ WL_PROXD_SESSION_STATE_STARTED,		"started" },
	{ WL_PROXD_SESSION_STATE_DELAY,			"delay" },
	{ WL_PROXD_SESSION_STATE_USER_WAIT,		"user-wait" },
	{ WL_PROXD_SESSION_STATE_SCHED_WAIT,	"sched-wait" },
	{ WL_PROXD_SESSION_STATE_BURST,			"burst" },
	{ WL_PROXD_SESSION_STATE_STOPPING,		"stopping" },
	{ WL_PROXD_SESSION_STATE_ENDED,			"ended" },
	{ WL_PROXD_SESSION_STATE_DESTROYING,	"destroying" },
	{ WL_PROXD_SESSION_STATE_NONE,			"none" }
};

/*
* ranging-state --> text string mapping
*/
static const ftm_strmap_entry_t ftm_ranging_state_value_loginfo [] = {
	/* wl_proxd_ranging_state_t,			text string */
		{ WL_PROXD_RANGING_STATE_NONE, "none" },
		{ WL_PROXD_RANGING_STATE_NOTSTARTED, "nonstarted" },
		{ WL_PROXD_RANGING_STATE_INPROGRESS, "inprogress" },
		{ WL_PROXD_RANGING_STATE_DONE, "done" },
};

/*
* status --> text string mapping
*/
static const ftm_strmap_entry_t ftm_status_value_loginfo[] = {
	/* wl_proxd_status_t,			text-string */
	{ WL_PROXD_E_OVERRIDDEN,		"overridden" },
	{ WL_PROXD_E_ASAP_FAILED,		"ASAP failed" },
	{ WL_PROXD_E_NOTSTARTED,		"not started" },
	{ WL_PROXD_E_INVALIDAVB,		"invalid AVB" },
	{ WL_PROXD_E_INCAPABLE,		"incapable" },
	{ WL_PROXD_E_MISMATCH,		"mismatch"},
	{ WL_PROXD_E_DUP_SESSION,		"dup session" },
	{ WL_PROXD_E_REMOTE_FAIL,		"remote fail" },
	{ WL_PROXD_E_REMOTE_INCAPABLE,	"remote incapable" },
	{ WL_PROXD_E_SCHED_FAIL,		"sched failure" },
	{ WL_PROXD_E_PROTO,			"protocol error" },
	{ WL_PROXD_E_EXPIRED,			"expired" },
	{ WL_PROXD_E_TIMEOUT,			"timeout" },
	{ WL_PROXD_E_NOACK,			"no ack" },
	{ WL_PROXD_E_DEFERRED,		"deferred" },
	{ WL_PROXD_E_INVALID_SID,		"invalid session id" },
	{ WL_PROXD_E_REMOTE_CANCEL,		"remote cancel" },
	{ WL_PROXD_E_CANCELED,		"canceled" },
	{ WL_PROXD_E_INVALID_SESSION,	"invalid session" },
	{ WL_PROXD_E_BAD_STATE,		"bad state" },
	{ WL_PROXD_E_ERROR,			"error" },
	{ WL_PROXD_E_OK,				"OK" }
};

/*
* time interval unit --> text string mapping
*/
static const ftm_strmap_entry_t ftm_tmu_value_loginfo[] = {
	/* wl_proxd_tmu_t,			text-string */
	{ WL_PROXD_TMU_TU,			"TU" },
	{ WL_PROXD_TMU_SEC,			"sec" },
	{ WL_PROXD_TMU_MILLI_SEC,	"ms" },
	{ WL_PROXD_TMU_MICRO_SEC,	"us" },
	{ WL_PROXD_TMU_NANO_SEC,	"ns" },
	{ WL_PROXD_TMU_PICO_SEC,	"ps" }
};

#define RSPEC_BW(rspec)         ((rspec) & WL_RSPEC_BW_MASK)
#define RSPEC_IS20MHZ(rspec)	(RSPEC_BW(rspec) == WL_RSPEC_BW_20MHZ)
#define RSPEC_IS40MHZ(rspec)	(RSPEC_BW(rspec) == WL_RSPEC_BW_40MHZ)
#define RSPEC_IS80MHZ(rspec)    (RSPEC_BW(rspec) == WL_RSPEC_BW_80MHZ)
#define RSPEC_IS160MHZ(rspec)   (RSPEC_BW(rspec) == WL_RSPEC_BW_160MHZ)

#define IS_MCS(rspec)     	(((rspec) & WL_RSPEC_ENCODING_MASK) != WL_RSPEC_ENCODE_RATE)
#define IS_STBC(rspec)     	(((((rspec) & WL_RSPEC_ENCODING_MASK) == WL_RSPEC_ENCODE_HT) ||	\
	(((rspec) & WL_RSPEC_ENCODING_MASK) == WL_RSPEC_ENCODE_VHT)) &&	\
	(((rspec) & WL_RSPEC_STBC) == WL_RSPEC_STBC))
#define RSPEC_ISSGI(rspec)      (((rspec) & WL_RSPEC_SGI) != 0)
#define RSPEC_ISLDPC(rspec)     (((rspec) & WL_RSPEC_LDPC) != 0)
#define RSPEC_ISSTBC(rspec)     (((rspec) & WL_RSPEC_STBC) != 0)
#define RSPEC_ISTXBF(rspec)     (((rspec) & WL_RSPEC_TXBF) != 0)
#define RSPEC_ISVHT(rspec)    	(((rspec) & WL_RSPEC_ENCODING_MASK) == WL_RSPEC_ENCODE_VHT)
#define RSPEC_ISHT(rspec)	(((rspec) & WL_RSPEC_ENCODING_MASK) == WL_RSPEC_ENCODE_HT)
#define RSPEC_ISLEGACY(rspec)   (((rspec) & WL_RSPEC_ENCODING_MASK) == WL_RSPEC_ENCODE_RATE)
#define RSPEC2RATE(rspec)	(RSPEC_ISLEGACY(rspec) ? \
				 ((rspec) & RSPEC_RATE_MASK) : rate_rspec2rate(rspec))
/* return rate in unit of 500Kbps -- for internal use in wlc_rate_sel.c */
#define RSPEC2KBPS(rspec)	rate_rspec2rate(rspec)

struct ieee_80211_mcs_rate_info {
	uint8 constellation_bits;
	uint8 coding_q;
	uint8 coding_d;
};

static const struct ieee_80211_mcs_rate_info wl_mcs_info[] = {
	{ 1, 1, 2 }, /* MCS  0: MOD: BPSK,   CR 1/2 */
	{ 2, 1, 2 }, /* MCS  1: MOD: QPSK,   CR 1/2 */
	{ 2, 3, 4 }, /* MCS  2: MOD: QPSK,   CR 3/4 */
	{ 4, 1, 2 }, /* MCS  3: MOD: 16QAM,  CR 1/2 */
	{ 4, 3, 4 }, /* MCS  4: MOD: 16QAM,  CR 3/4 */
	{ 6, 2, 3 }, /* MCS  5: MOD: 64QAM,  CR 2/3 */
	{ 6, 3, 4 }, /* MCS  6: MOD: 64QAM,  CR 3/4 */
	{ 6, 5, 6 }, /* MCS  7: MOD: 64QAM,  CR 5/6 */
	{ 8, 3, 4 }, /* MCS  8: MOD: 256QAM, CR 3/4 */
	{ 8, 5, 6 }  /* MCS  9: MOD: 256QAM, CR 5/6 */
};

/**
 * Returns the rate in [Kbps] units for a caller supplied MCS/bandwidth/Nss/Sgi combination.
 *     'mcs' : a *single* spatial stream MCS (11n or 11ac)
 */
uint
rate_mcs2rate(uint mcs, uint nss, uint bw, int sgi)
{
	const int ksps = 250; /* kilo symbols per sec, 4 us sym */
	const int Nsd_20MHz = 52;
	const int Nsd_40MHz = 108;
	const int Nsd_80MHz = 234;
	const int Nsd_160MHz = 468;
	uint rate;

	if (mcs == 32) {
		/* just return fixed values for mcs32 instead of trying to parametrize */
		rate = (sgi == 0) ? 6000 : 6778;
	} else if (mcs <= 9) {
		/* This calculation works for 11n HT and 11ac VHT if the HT mcs values
		 * are decomposed into a base MCS = MCS % 8, and Nss = 1 + MCS / 8.
		 * That is, HT MCS 23 is a base MCS = 7, Nss = 3
		 */

		/* find the number of complex numbers per symbol */
		if (RSPEC_IS20MHZ(bw)) {
			rate = Nsd_20MHz;
		} else if (RSPEC_IS40MHZ(bw)) {
			rate = Nsd_40MHz;
		} else if (bw == WL_RSPEC_BW_80MHZ) {
			rate = Nsd_80MHz;
		} else if (bw == WL_RSPEC_BW_160MHZ) {
			rate = Nsd_160MHz;
		} else {
			rate = 0;
		}

		/* multiply by bits per number from the constellation in use */
		rate = rate * wl_mcs_info[mcs].constellation_bits;

		/* adjust for the number of spatial streams */
		rate = rate * nss;

		/* adjust for the coding rate given as a quotient and divisor */
		rate = (rate * wl_mcs_info[mcs].coding_q) / wl_mcs_info[mcs].coding_d;

		/* multiply by Kilo symbols per sec to get Kbps */
		rate = rate * ksps;

		/* adjust the symbols per sec for SGI
		 * symbol duration is 4 us without SGI, and 3.6 us with SGI,
		 * so ratio is 10 / 9
		 */
		if (sgi) {
			/* add 4 for rounding of division by 9 */
			rate = ((rate * 10) + 4) / 9;
		}
	} else {
		rate = 0;
	}

	return rate;
} /* wlc_rate_mcs2rate */

/** take a well formed ratespec_t arg and return phy rate in [Kbps] units */
int
rate_rspec2rate(uint32 rspec)
{
	int rate = -1;

	if (RSPEC_ISLEGACY(rspec)) {
		rate = 500 * (rspec & WL_RSPEC_RATE_MASK);
	} else if (RSPEC_ISHT(rspec)) {
		uint mcs = (rspec & WL_RSPEC_RATE_MASK);

		if (mcs == 32) {
			rate = rate_mcs2rate(mcs, 1, WL_RSPEC_BW_40MHZ, RSPEC_ISSGI(rspec));
		} else {
			uint nss = 1 + (mcs / 8);
			mcs = mcs % 8;
			rate = rate_mcs2rate(mcs, nss, RSPEC_BW(rspec), RSPEC_ISSGI(rspec));
		}
	} else if (RSPEC_ISVHT(rspec)) {
		uint mcs = (rspec & WL_RSPEC_VHT_MCS_MASK);
		uint nss = (rspec & WL_RSPEC_VHT_NSS_MASK) >> WL_RSPEC_VHT_NSS_SHIFT;

		ASSERT(mcs <= 9);
		ASSERT(nss <= 8);

		rate = rate_mcs2rate(mcs, nss, RSPEC_BW(rspec), RSPEC_ISSGI(rspec));
	} else {
		ASSERT(0);
	}

	return (rate == 0) ? -1 : rate;
}

char resp_buf[WLC_IOCTL_SMLEN];

static uint64
ftm_intvl2nsec(const wl_proxd_intvl_t *intvl)
{
	uint64 ret;
	ret = intvl->intvl;
	switch (intvl->tmu) {
	case WL_PROXD_TMU_TU:			ret = FTM_TU2MICRO(ret) * 1000; break;
	case WL_PROXD_TMU_SEC:			ret *= 1000000000; break;
	case WL_PROXD_TMU_MILLI_SEC:	ret *= 1000000; break;
	case WL_PROXD_TMU_MICRO_SEC:	ret *= 1000; break;
	case WL_PROXD_TMU_PICO_SEC:		ret = intvl->intvl / 1000; break;
	case WL_PROXD_TMU_NANO_SEC:		/* fall through */
	default:						break;
	}
	return ret;
}
uint64
ftm_intvl2usec(const wl_proxd_intvl_t *intvl)
{
	uint64 ret;
	ret = intvl->intvl;
	switch (intvl->tmu) {
	case WL_PROXD_TMU_TU:			ret = FTM_TU2MICRO(ret); break;
	case WL_PROXD_TMU_SEC:			ret *= 1000000; break;
	case WL_PROXD_TMU_NANO_SEC:		ret = intvl->intvl / 1000; break;
	case WL_PROXD_TMU_PICO_SEC:		ret = intvl->intvl / 1000000; break;
	case WL_PROXD_TMU_MILLI_SEC:	ret *= 1000; break;
	case WL_PROXD_TMU_MICRO_SEC:	/* fall through */
	default:						break;
	}
	return ret;
}

/*
* lookup 'id' (as a key) from a fw status to host map table
* if found, return the corresponding reason code
*/

static rtt_reason_t
ftm_get_statusmap_info(wl_proxd_status_t id, const ftm_status_map_host_entry_t *p_table,
	uint32 num_entries)
{
	int i;
	const ftm_status_map_host_entry_t *p_entry;
	/* scan thru the table till end */
	p_entry = p_table;
	for (i = 0; i < (int) num_entries; i++)
	{
		if (p_entry->proxd_status == id) {
			return p_entry->rtt_reason;
		}
		p_entry++;		/* next entry */
	}
	return RTT_REASON_FAILURE; /* not found */
}
/*
* lookup 'id' (as a key) from a table
* if found, return the entry pointer, otherwise return NULL
*/
static const ftm_strmap_entry_t*
ftm_get_strmap_info(int32 id, const ftm_strmap_entry_t *p_table, uint32 num_entries)
{
	int i;
	const ftm_strmap_entry_t *p_entry;

	/* scan thru the table till end */
	p_entry = p_table;
	for (i = 0; i < (int) num_entries; i++)
	{
		if (p_entry->id == id)
			return p_entry;
		p_entry++;		/* next entry */
	}
	return NULL;			/* not found */
}

/*
* map enum to a text-string for display, this function is called by the following:
* For debug/trace:
*     ftm_[cmdid|tlvid]_to_str()
* For TLV-output log for 'get' commands
*     ftm_[method|tmu|caps|status|state]_value_to_logstr()
* Input:
*     pTable -- point to a 'enum to string' table.
*/
static const char *
ftm_map_id_to_str(int32 id, const ftm_strmap_entry_t *p_table, uint32 num_entries)
{
	const ftm_strmap_entry_t*p_entry = ftm_get_strmap_info(id, p_table, num_entries);
	if (p_entry)
		return (p_entry->text);

	return "invalid";
}


#ifdef RTT_DEBUG

/* define entry, e.g. { WL_PROXD_CMD_xxx, "WL_PROXD_CMD_xxx" } */
#define DEF_STRMAP_ENTRY(id) { (id), #id }

/* ftm cmd-id mapping */
static const ftm_strmap_entry_t ftm_cmdid_map[] = {
	/* {wl_proxd_cmd_t(WL_PROXD_CMD_xxx), "WL_PROXD_CMD_xxx" }, */
	DEF_STRMAP_ENTRY(WL_PROXD_CMD_NONE),
	DEF_STRMAP_ENTRY(WL_PROXD_CMD_GET_VERSION),
	DEF_STRMAP_ENTRY(WL_PROXD_CMD_ENABLE),
	DEF_STRMAP_ENTRY(WL_PROXD_CMD_DISABLE),
	DEF_STRMAP_ENTRY(WL_PROXD_CMD_CONFIG),
	DEF_STRMAP_ENTRY(WL_PROXD_CMD_START_SESSION),
	DEF_STRMAP_ENTRY(WL_PROXD_CMD_BURST_REQUEST),
	DEF_STRMAP_ENTRY(WL_PROXD_CMD_STOP_SESSION),
	DEF_STRMAP_ENTRY(WL_PROXD_CMD_DELETE_SESSION),
	DEF_STRMAP_ENTRY(WL_PROXD_CMD_GET_RESULT),
	DEF_STRMAP_ENTRY(WL_PROXD_CMD_GET_INFO),
	DEF_STRMAP_ENTRY(WL_PROXD_CMD_GET_STATUS),
	DEF_STRMAP_ENTRY(WL_PROXD_CMD_GET_SESSIONS),
	DEF_STRMAP_ENTRY(WL_PROXD_CMD_GET_COUNTERS),
	DEF_STRMAP_ENTRY(WL_PROXD_CMD_CLEAR_COUNTERS),
	DEF_STRMAP_ENTRY(WL_PROXD_CMD_COLLECT),
	DEF_STRMAP_ENTRY(WL_PROXD_CMD_TUNE),
	DEF_STRMAP_ENTRY(WL_PROXD_CMD_DUMP),
	DEF_STRMAP_ENTRY(WL_PROXD_CMD_START_RANGING),
	DEF_STRMAP_ENTRY(WL_PROXD_CMD_STOP_RANGING),
	DEF_STRMAP_ENTRY(WL_PROXD_CMD_GET_RANGING_INFO),
};

/*
* map a ftm cmd-id to a text-string for display
*/
static const char *
ftm_cmdid_to_str(uint16 cmdid)
{
	return ftm_map_id_to_str((int32) cmdid, &ftm_cmdid_map[0], ARRAYSIZE(ftm_cmdid_map));
}
#endif /* RTT_DEBUG */


/*
* convert BCME_xxx error codes into related error strings
* note, bcmerrorstr() defined in bcmutils is for BCMDRIVER only,
*       this duplicate copy is for WL access and may need to clean up later
*/
static const char *ftm_bcmerrorstrtable[] = BCMERRSTRINGTABLE;
static const char *
ftm_status_value_to_logstr(wl_proxd_status_t status)
{
	static char ftm_msgbuf_status_undef[32];
	const ftm_strmap_entry_t *p_loginfo;
	int bcmerror;

	/* check if within BCME_xxx error range */
	bcmerror = (int) status;
	if (VALID_BCMERROR(bcmerror))
		return ftm_bcmerrorstrtable[-bcmerror];

	/* otherwise, look for 'proxd ftm status' range */
	p_loginfo = ftm_get_strmap_info((int32) status,
		&ftm_status_value_loginfo[0], ARRAYSIZE(ftm_status_value_loginfo));
	if (p_loginfo)
		return p_loginfo->text;

	/* report for 'out of range' FTM-status error code */
	memset(ftm_msgbuf_status_undef, 0, sizeof(ftm_msgbuf_status_undef));
	snprintf(ftm_msgbuf_status_undef, sizeof(ftm_msgbuf_status_undef),
		"Undefined status %d", status);
	return &ftm_msgbuf_status_undef[0];
}

static const char *
ftm_tmu_value_to_logstr(wl_proxd_tmu_t tmu)
{
	return ftm_map_id_to_str((int32)tmu,
		&ftm_tmu_value_loginfo[0], ARRAYSIZE(ftm_tmu_value_loginfo));
}

static const ftm_strmap_entry_t*
ftm_get_event_type_loginfo(wl_proxd_event_type_t	event_type)
{
	/* look up 'event-type' from a predefined table  */
	return ftm_get_strmap_info((int32) event_type,
		ftm_event_type_loginfo, ARRAYSIZE(ftm_event_type_loginfo));
}

static const char *
ftm_session_state_value_to_logstr(wl_proxd_session_state_t state)
{
	return ftm_map_id_to_str((int32)state, &ftm_session_state_value_loginfo[0],
		ARRAYSIZE(ftm_session_state_value_loginfo));
}


/*
* send 'proxd' iovar for all ftm get-related commands
*/
static int
rtt_do_get_ioctl(dhd_pub_t *dhd, wl_proxd_iov_t *p_proxd_iov, uint16 proxd_iovsize,
		ftm_subcmd_info_t *p_subcmd_info)
{

	wl_proxd_iov_t *p_iovresp = (wl_proxd_iov_t *)resp_buf;
	int status;
	int tlvs_len;
	/*  send getbuf proxd iovar */
	status = dhd_getiovar(dhd, 0, "proxd", (char *)p_proxd_iov,
			proxd_iovsize, (char **)&p_iovresp, WLC_IOCTL_SMLEN);
	if (status != BCME_OK) {
		DHD_ERROR(("%s: failed to send getbuf proxd iovar (CMD ID : %d), status=%d\n",
			__FUNCTION__, p_subcmd_info->cmdid, status));
		return status;
	}
	if (p_subcmd_info->cmdid == WL_PROXD_CMD_GET_VERSION) {
		p_subcmd_info->version = ltoh16(p_iovresp->version);
		DHD_RTT(("ftm version: 0x%x\n", ltoh16(p_iovresp->version)));
		goto exit;
	}

	tlvs_len = ltoh16(p_iovresp->len) - WL_PROXD_IOV_HDR_SIZE;
	if (tlvs_len < 0) {
		DHD_ERROR(("%s: alert, p_iovresp->len(%d) should not be smaller than %d\n",
			__FUNCTION__, ltoh16(p_iovresp->len), (int) WL_PROXD_IOV_HDR_SIZE));
		tlvs_len = 0;
	}

	if (tlvs_len > 0 && p_subcmd_info->handler) {
		/* unpack TLVs and invokes the cbfn for processing */
		status = bcm_unpack_xtlv_buf(p_proxd_iov, (uint8 *)p_iovresp->tlvs,
				tlvs_len, BCM_XTLV_OPTION_ALIGN32, p_subcmd_info->handler);
	}
exit:
	return status;
}


static wl_proxd_iov_t *
rtt_alloc_getset_buf(wl_proxd_method_t method, wl_proxd_session_id_t session_id,
	wl_proxd_cmd_t cmdid, uint16 tlvs_bufsize, uint16 *p_out_bufsize)
{
	uint16 proxd_iovsize;
	uint16 kflags;
	wl_proxd_tlv_t *p_tlv;
	wl_proxd_iov_t *p_proxd_iov = (wl_proxd_iov_t *) NULL;

	*p_out_bufsize = 0;	/* init */
	kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	/* calculate the whole buffer size, including one reserve-tlv entry in the header */
	proxd_iovsize = sizeof(wl_proxd_iov_t) + tlvs_bufsize;

	p_proxd_iov = kzalloc(proxd_iovsize, kflags);
	if (p_proxd_iov == NULL) {
		DHD_ERROR(("error: failed to allocate %d bytes of memory\n", proxd_iovsize));
		return NULL;
	}

	/* setup proxd-FTM-method iovar header */
	p_proxd_iov->version = htol16(WL_PROXD_API_VERSION);
	p_proxd_iov->len = htol16(proxd_iovsize); /* caller may adjust it based on #of TLVs */
	p_proxd_iov->cmd = htol16(cmdid);
	p_proxd_iov->method = htol16(method);
	p_proxd_iov->sid = htol16(session_id);

	/* initialize the reserved/dummy-TLV in iovar header */
	p_tlv = p_proxd_iov->tlvs;
	p_tlv->id = htol16(WL_PROXD_TLV_ID_NONE);
	p_tlv->len = htol16(0);

	*p_out_bufsize = proxd_iovsize;	/* for caller's reference */

	return p_proxd_iov;
}


static int
dhd_rtt_common_get_handler(dhd_pub_t *dhd, ftm_subcmd_info_t *p_subcmd_info,
		wl_proxd_method_t method,
		wl_proxd_session_id_t session_id)
{
	int status = BCME_OK;
	uint16 proxd_iovsize = 0;
	wl_proxd_iov_t *p_proxd_iov;
#ifdef RTT_DEBUG
	DHD_RTT(("enter %s: method=%d, session_id=%d, cmdid=%d(%s)\n",
		__FUNCTION__, method, session_id, p_subcmd_info->cmdid,
		ftm_cmdid_to_str(p_subcmd_info->cmdid)));
#endif
	/* alloc mem for ioctl headr + reserved 0 bufsize for tlvs (initialize to zero) */
	p_proxd_iov = rtt_alloc_getset_buf(method, session_id, p_subcmd_info->cmdid,
		0, &proxd_iovsize);

	if (p_proxd_iov == NULL)
		return BCME_NOMEM;

	status = rtt_do_get_ioctl(dhd, p_proxd_iov, proxd_iovsize, p_subcmd_info);

	if (status != BCME_OK) {
		DHD_RTT(("%s failed: status=%d\n", __FUNCTION__, status));
	}
	kfree(p_proxd_iov);
	return status;
}

/*
* common handler for set-related proxd method commands which require no TLV as input
*   wl proxd ftm [session-id] <set-subcmd>
* e.g.
*   wl proxd ftm enable -- to enable ftm
*   wl proxd ftm disable -- to disable ftm
*   wl proxd ftm <session-id> start -- to start a specified session
*   wl proxd ftm <session-id> stop  -- to cancel a specified session;
*                                    state is maintained till session is delete.
*   wl proxd ftm <session-id> delete -- to delete a specified session
*   wl proxd ftm [<session-id>] clear-counters -- to clear counters
*   wl proxd ftm <session-id> burst-request -- on initiator: to send burst request;
*                                              on target: send FTM frame
*   wl proxd ftm <session-id> collect
*   wl proxd ftm tune     (TBD)
*/
static int
dhd_rtt_common_set_handler(dhd_pub_t *dhd, const ftm_subcmd_info_t *p_subcmd_info,
	wl_proxd_method_t method, wl_proxd_session_id_t session_id)
{
	uint16 proxd_iovsize;
	wl_proxd_iov_t *p_proxd_iov;
	int ret;

#ifdef RTT_DEBUG
	DHD_ERROR(("enter %s: method=%d, session_id=%d, cmdid=%d(%s)\n",
		__FUNCTION__, method, session_id, p_subcmd_info->cmdid,
		ftm_cmdid_to_str(p_subcmd_info->cmdid)));
#endif
	/* allocate and initialize a temp buffer for 'set proxd' iovar */
	proxd_iovsize = 0;
	p_proxd_iov = rtt_alloc_getset_buf(method, session_id, p_subcmd_info->cmdid,
							0, &proxd_iovsize);		/* no TLV */
	if (p_proxd_iov == NULL)
		return BCME_NOMEM;

	/* no TLV to pack, simply issue a set-proxd iovar */
	ret = dhd_iovar(dhd, 0, "proxd", (char *)p_proxd_iov, proxd_iovsize, NULL, 0, TRUE);
#ifdef RTT_DEBUG
	if (ret != BCME_OK) {
		DHD_ERROR(("error: Proxd IOVAR failed, status=%d\n", ret));
	}
#endif
	/* clean up */
	kfree(p_proxd_iov);
	return ret;
}

static int
rtt_unpack_xtlv_cbfn(void *ctx, uint8 *p_data, uint16 tlvid, uint16 len)
{
	int ret = BCME_OK;
	wl_proxd_ftm_session_status_t *p_data_info;
	switch (tlvid) {
	case WL_PROXD_TLV_ID_RTT_RESULT:
		ret = dhd_rtt_convert_results_to_host((rtt_report_t *)ctx,
				p_data, tlvid, len);
		break;
	case WL_PROXD_TLV_ID_SESSION_STATUS:
		memcpy(ctx, p_data, sizeof(wl_proxd_ftm_session_status_t));
		p_data_info = (wl_proxd_ftm_session_status_t *)ctx;
		p_data_info->state = ltoh16_ua(&p_data_info->state);
		p_data_info->status = ltoh32_ua(&p_data_info->status);
		break;
	default:
		DHD_ERROR(("> Unsupported TLV ID %d\n", tlvid));
		ret = BCME_ERROR;
		break;
	}

	return ret;
}
static int
rtt_handle_config_options(wl_proxd_session_id_t session_id, wl_proxd_tlv_t **p_tlv,
	uint16 *p_buf_space_left, ftm_config_options_info_t *ftm_configs, int ftm_cfg_cnt)
{
	int ret = BCME_OK;
	int cfg_idx = 0;
	uint32 flags = WL_PROXD_FLAG_NONE;
	uint32 flags_mask = WL_PROXD_FLAG_NONE;
	uint32 new_mask;		/* cmdline input */
	ftm_config_options_info_t *p_option_info;
	uint16 type = (session_id == WL_PROXD_SESSION_ID_GLOBAL) ?
			WL_PROXD_TLV_ID_FLAGS_MASK : WL_PROXD_TLV_ID_SESSION_FLAGS_MASK;
	for (cfg_idx = 0; cfg_idx < ftm_cfg_cnt; cfg_idx++) {
		p_option_info = (ftm_configs + cfg_idx);
		if (p_option_info != NULL) {
			new_mask = p_option_info->flags;
			/* update flags mask */
			flags_mask |= new_mask;
			if (p_option_info->enable) {
				flags |= new_mask;	/* set the bit on */
			} else {
				flags &= ~new_mask;	/* set the bit off */
			}
		}
	}
	flags = htol32(flags);
	flags_mask = htol32(flags_mask);
	/* setup flags_mask TLV */
	ret = bcm_pack_xtlv_entry((uint8 **)p_tlv, p_buf_space_left,
		type, sizeof(uint32), &flags_mask, BCM_XTLV_OPTION_ALIGN32);
	if (ret != BCME_OK) {
		DHD_ERROR(("%s : bcm_pack_xltv_entry() for mask flags failed, status=%d\n",
			__FUNCTION__, ret));
		goto exit;
	}

	type = (session_id == WL_PROXD_SESSION_ID_GLOBAL)?
		WL_PROXD_TLV_ID_FLAGS : WL_PROXD_TLV_ID_SESSION_FLAGS;
	/* setup flags TLV */
	ret = bcm_pack_xtlv_entry((uint8 **)p_tlv, p_buf_space_left,
			type, sizeof(uint32), &flags, BCM_XTLV_OPTION_ALIGN32);
		if (ret != BCME_OK) {
//#ifdef RTT_DEBUG
			DHD_RTT(("%s: bcm_pack_xltv_entry() for flags failed, status=%d\n",
				__FUNCTION__, ret));
//#endif
		}
exit:
	return ret;
}

static int
rtt_handle_config_general(wl_proxd_session_id_t session_id, wl_proxd_tlv_t **p_tlv,
	uint16 *p_buf_space_left, ftm_config_param_info_t *ftm_configs, int ftm_cfg_cnt)
{
	int ret = BCME_OK;
	int cfg_idx = 0;
	uint32 chanspec;
	ftm_config_param_info_t *p_config_param_info;
	void		*p_src_data;
	uint16	src_data_size;	/* size of data pointed by p_src_data as 'source' */
	for (cfg_idx = 0; cfg_idx < ftm_cfg_cnt; cfg_idx++) {
		p_config_param_info = (ftm_configs + cfg_idx);
		if (p_config_param_info != NULL) {
			switch (p_config_param_info->tlvid)	{
			case WL_PROXD_TLV_ID_BSS_INDEX:
			case WL_PROXD_TLV_ID_FTM_RETRIES:
			case WL_PROXD_TLV_ID_FTM_REQ_RETRIES:
				p_src_data = &p_config_param_info->data8;
				src_data_size = sizeof(uint8);
				break;
			case WL_PROXD_TLV_ID_BURST_NUM_FTM: /* uint16 */
			case WL_PROXD_TLV_ID_NUM_BURST:
			case WL_PROXD_TLV_ID_RX_MAX_BURST:
				p_src_data = &p_config_param_info->data16;
				src_data_size = sizeof(uint16);
				break;
			case WL_PROXD_TLV_ID_TX_POWER:		/* uint32 */
			case WL_PROXD_TLV_ID_RATESPEC:
			case WL_PROXD_TLV_ID_EVENT_MASK: /* wl_proxd_event_mask_t/uint32 */
			case WL_PROXD_TLV_ID_DEBUG_MASK:
				p_src_data = &p_config_param_info->data32;
				src_data_size = sizeof(uint32);
				break;
			case WL_PROXD_TLV_ID_CHANSPEC:		/* chanspec_t --> 32bit */
				chanspec = p_config_param_info->chanspec;
				p_src_data = (void *) &chanspec;
				src_data_size = sizeof(uint32);
				break;
			case WL_PROXD_TLV_ID_BSSID: /* mac address */
			case WL_PROXD_TLV_ID_PEER_MAC:
				p_src_data = &p_config_param_info->mac_addr;
				src_data_size = sizeof(struct ether_addr);
				break;
			case WL_PROXD_TLV_ID_BURST_DURATION:	/* wl_proxd_intvl_t */
			case WL_PROXD_TLV_ID_BURST_PERIOD:
			case WL_PROXD_TLV_ID_BURST_FTM_SEP:
			case WL_PROXD_TLV_ID_BURST_TIMEOUT:
			case WL_PROXD_TLV_ID_INIT_DELAY:
				p_src_data = &p_config_param_info->data_intvl;
				src_data_size = sizeof(wl_proxd_intvl_t);
				break;
			default:
				ret = BCME_BADARG;
				break;
			}
			if (ret != BCME_OK) {
				DHD_ERROR(("%s bad TLV ID : %d\n",
					__FUNCTION__, p_config_param_info->tlvid));
				break;
			}

			ret = bcm_pack_xtlv_entry((uint8 **) p_tlv, p_buf_space_left,
				p_config_param_info->tlvid, src_data_size, p_src_data,
				BCM_XTLV_OPTION_ALIGN32);
			if (ret != BCME_OK) {
				DHD_ERROR(("%s: bcm_pack_xltv_entry() failed,"
					" status=%d\n", __FUNCTION__, ret));
				break;
			}

		}
	}
	return ret;
}

static int
dhd_rtt_get_version(dhd_pub_t *dhd, int *out_version)
{
	int ret;
	ftm_subcmd_info_t subcmd_info;
	subcmd_info.name = "ver";
	subcmd_info.cmdid = WL_PROXD_CMD_GET_VERSION;
	subcmd_info.handler = NULL;
	ret = dhd_rtt_common_get_handler(dhd, &subcmd_info,
			WL_PROXD_METHOD_FTM, WL_PROXD_SESSION_ID_GLOBAL);
	*out_version = (ret == BCME_OK) ? subcmd_info.version : 0;
	return ret;
}

static int
dhd_rtt_ftm_enable(dhd_pub_t *dhd, bool enable)
{
	ftm_subcmd_info_t subcmd_info;
	subcmd_info.name = (enable)? "enable" : "disable";
	subcmd_info.cmdid = (enable)? WL_PROXD_CMD_ENABLE: WL_PROXD_CMD_DISABLE;
	subcmd_info.handler = NULL;
	return dhd_rtt_common_set_handler(dhd, &subcmd_info,
			WL_PROXD_METHOD_FTM, WL_PROXD_SESSION_ID_GLOBAL);
}

static int
dhd_rtt_start_session(dhd_pub_t *dhd, wl_proxd_session_id_t session_id, bool start)
{
	ftm_subcmd_info_t subcmd_info;
	subcmd_info.name = (start)? "start session" : "stop session";
	subcmd_info.cmdid = (start)? WL_PROXD_CMD_START_SESSION: WL_PROXD_CMD_STOP_SESSION;
	subcmd_info.handler = NULL;
	return dhd_rtt_common_set_handler(dhd, &subcmd_info,
			WL_PROXD_METHOD_FTM, session_id);
}

static int
dhd_rtt_delete_session(dhd_pub_t *dhd, wl_proxd_session_id_t session_id)
{
	ftm_subcmd_info_t subcmd_info;
	subcmd_info.name = "delete session";
	subcmd_info.cmdid = WL_PROXD_CMD_DELETE_SESSION;
	subcmd_info.handler = NULL;
	return dhd_rtt_common_set_handler(dhd, &subcmd_info,
			WL_PROXD_METHOD_FTM, session_id);
}

static int
dhd_rtt_ftm_config(dhd_pub_t *dhd, wl_proxd_session_id_t session_id,
	ftm_config_category_t catagory, void *ftm_configs, int ftm_cfg_cnt)
{
	ftm_subcmd_info_t subcmd_info;
	wl_proxd_tlv_t *p_tlv;
	/* alloc mem for ioctl headr + reserved 0 bufsize for tlvs (initialize to zero) */
	wl_proxd_iov_t *p_proxd_iov;
	uint16 proxd_iovsize = 0;
	uint16 bufsize;
	uint16 buf_space_left;
	uint16 all_tlvsize;
	int ret = BCME_OK;
	subcmd_info.name = "config";
	subcmd_info.cmdid = WL_PROXD_CMD_CONFIG;

	p_proxd_iov = rtt_alloc_getset_buf(WL_PROXD_METHOD_FTM, session_id, subcmd_info.cmdid,
		FTM_IOC_BUFSZ, &proxd_iovsize);

	if (p_proxd_iov == NULL) {
		DHD_ERROR(("%s : failed to allocate the iovar (size :%d)\n",
			__FUNCTION__, FTM_IOC_BUFSZ));
		return BCME_NOMEM;
	}
	/* setup TLVs */
	bufsize = proxd_iovsize - WL_PROXD_IOV_HDR_SIZE; /* adjust available size for TLVs */
	p_tlv = &p_proxd_iov->tlvs[0];
	/* TLV buffer starts with a full size, will decrement for each packed TLV */
	buf_space_left = bufsize;
	if (catagory == FTM_CONFIG_CAT_OPTIONS) {
		ret = rtt_handle_config_options(session_id, &p_tlv, &buf_space_left,
				(ftm_config_options_info_t *)ftm_configs, ftm_cfg_cnt);
	} else if (catagory == FTM_CONFIG_CAT_GENERAL) {
		ret = rtt_handle_config_general(session_id, &p_tlv, &buf_space_left,
				(ftm_config_param_info_t *)ftm_configs, ftm_cfg_cnt);
	}
	if (ret == BCME_OK) {
		/* update the iov header, set len to include all TLVs + header */
		all_tlvsize = (bufsize - buf_space_left);
		p_proxd_iov->len = htol16(all_tlvsize + WL_PROXD_IOV_HDR_SIZE);
		ret = dhd_iovar(dhd, 0, "proxd", (char *)p_proxd_iov,
				all_tlvsize + WL_PROXD_IOV_HDR_SIZE, NULL, 0, TRUE);
		if (ret != BCME_OK) {
			DHD_ERROR(("%s : failed to set config \n", __FUNCTION__));
		}
	}
	/* clean up */
	kfree(p_proxd_iov);
	return ret;
}
chanspec_t
dhd_rtt_convert_to_chspec(wifi_channel_info_t channel)
{
	int bw;
	chanspec_t chanspec = 0;
	uint8 center_chan;
	uint8 primary_chan;
	/* set witdh to 20MHZ for 2.4G HZ */
	if (channel.center_freq >= 2400 && channel.center_freq <= 2500) {
		channel.width = WIFI_CHAN_WIDTH_20;
	}
	switch (channel.width) {
	case WIFI_CHAN_WIDTH_20:
		bw = WL_CHANSPEC_BW_20;
		primary_chan = wf_mhz2channel(channel.center_freq, 0);
		chanspec = wf_channel2chspec(primary_chan, bw);
		break;
	case WIFI_CHAN_WIDTH_40:
		bw = WL_CHANSPEC_BW_40;
		primary_chan = wf_mhz2channel(channel.center_freq, 0);
		chanspec = wf_channel2chspec(primary_chan, bw);
		break;
	case WIFI_CHAN_WIDTH_80:
		bw = WL_CHANSPEC_BW_80;
		primary_chan = wf_mhz2channel(channel.center_freq, 0);
		center_chan = wf_mhz2channel(channel.center_freq0, 0);
		chanspec = wf_chspec_80(center_chan, primary_chan);
		break;
	default:
		DHD_ERROR(("doesn't support this bandwith : %d", channel.width));
		bw = -1;
		break;
	}
	return chanspec;
}

int
dhd_rtt_idx_to_burst_duration(uint idx)
{
	if (idx >= ARRAY_SIZE(burst_duration_idx)) {
		return -1;
	}
	return burst_duration_idx[idx];
}

int
dhd_rtt_set_cfg(dhd_pub_t *dhd, rtt_config_params_t *params)
{
	int err = BCME_OK;
	int idx;
	rtt_status_info_t *rtt_status;
	NULL_CHECK(params, "params is NULL", err);

	NULL_CHECK(dhd, "dhd is NULL", err);
	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", err);
	if (!HAS_11MC_CAP(rtt_status->rtt_capa.proto)) {
		DHD_ERROR(("doesn't support RTT \n"));
		return BCME_ERROR;
	}
	if (rtt_status->status != RTT_STOPPED) {
		DHD_ERROR(("rtt is already started\n"));
		return BCME_BUSY;
	}
	DHD_RTT(("%s enter\n", __FUNCTION__));

	memset(rtt_status->rtt_config.target_info, 0, TARGET_INFO_SIZE(RTT_MAX_TARGET_CNT));
	rtt_status->rtt_config.rtt_target_cnt = params->rtt_target_cnt;
	memcpy(rtt_status->rtt_config.target_info,
		params->target_info, TARGET_INFO_SIZE(params->rtt_target_cnt));
	rtt_status->status = RTT_STARTED;
	/* start to measure RTT from first device */
	/* find next target to trigger RTT */
	for (idx = rtt_status->cur_idx; idx < rtt_status->rtt_config.rtt_target_cnt; idx++) {
		/* skip the disabled device */
		if (rtt_status->rtt_config.target_info[idx].disable) {
			continue;
		} else {
			/* set the idx to cur_idx */
			rtt_status->cur_idx = idx;
			break;
		}
	}
	if (idx < rtt_status->rtt_config.rtt_target_cnt) {
		DHD_RTT(("rtt_status->cur_idx : %d\n", rtt_status->cur_idx));
		schedule_work(&rtt_status->work);
	}
	return err;
}

int
dhd_rtt_stop(dhd_pub_t *dhd, struct ether_addr *mac_list, int mac_cnt)
{
	int err = BCME_OK;
	int i = 0, j = 0;
	rtt_status_info_t *rtt_status;
	rtt_results_header_t *entry, *next;
	rtt_result_t *rtt_result, *next2;
	struct rtt_noti_callback *iter;

	NULL_CHECK(dhd, "dhd is NULL", err);
	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", err);
	if (rtt_status->status == RTT_STOPPED) {
		DHD_ERROR(("rtt is not started\n"));
		return BCME_OK;
	}
	DHD_RTT(("%s enter\n", __FUNCTION__));
	mutex_lock(&rtt_status->rtt_mutex);
	for (i = 0; i < mac_cnt; i++) {
		for (j = 0; j < rtt_status->rtt_config.rtt_target_cnt; j++) {
			if (!bcmp(&mac_list[i], &rtt_status->rtt_config.target_info[j].addr,
				ETHER_ADDR_LEN)) {
				rtt_status->rtt_config.target_info[j].disable = TRUE;
			}
		}
	}
	if (rtt_status->all_cancel) {
		/* cancel all of request */
		rtt_status->status = RTT_STOPPED;
		DHD_RTT(("current RTT process is cancelled\n"));
		/* remove the rtt results in cache */
		if (!list_empty(&rtt_status->rtt_results_cache)) {
			/* Iterate rtt_results_header list */
			list_for_each_entry_safe(entry, next,
				&rtt_status->rtt_results_cache, list) {
				list_del(&entry->list);
				/* Iterate rtt_result list */
				list_for_each_entry_safe(rtt_result, next2,
					&entry->result_list, list) {
					list_del(&rtt_result->list);
					kfree(rtt_result);
				}
				kfree(entry);
			}
		}
		/* send the rtt complete event to wake up the user process */
		list_for_each_entry(iter, &rtt_status->noti_fn_list, list) {
			iter->noti_fn(iter->ctx, &rtt_status->rtt_results_cache);
		}

		/* reinitialize the HEAD */
		INIT_LIST_HEAD(&rtt_status->rtt_results_cache);
		/* clear information for rtt_config */
		rtt_status->rtt_config.rtt_target_cnt = 0;
		memset(rtt_status->rtt_config.target_info, 0,
			TARGET_INFO_SIZE(RTT_MAX_TARGET_CNT));
		rtt_status->cur_idx = 0;
		dhd_rtt_delete_session(dhd, FTM_DEFAULT_SESSION);
		dhd_rtt_ftm_enable(dhd, FALSE);
	}
	mutex_unlock(&rtt_status->rtt_mutex);
	return err;
}

static int
dhd_rtt_start(dhd_pub_t *dhd)
{
	int err = BCME_OK;
	char eabuf[ETHER_ADDR_STR_LEN];
	char chanbuf[CHANSPEC_STR_LEN];
	int mpc = 0;
	int ftm_cfg_cnt = 0;
	int ftm_param_cnt = 0;
	uint32 rspec = 0;
	ftm_config_options_info_t ftm_configs[FTM_MAX_CONFIGS];
	ftm_config_param_info_t ftm_params[FTM_MAX_PARAMS];
	rtt_target_info_t *rtt_target;
	rtt_status_info_t *rtt_status;
	NULL_CHECK(dhd, "dhd is NULL", err);

	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", err);
	if (rtt_status->cur_idx >= rtt_status->rtt_config.rtt_target_cnt) {
		err = BCME_RANGE;
		DHD_ERROR(("%s : idx %d is out of range\n", __FUNCTION__, rtt_status->cur_idx));
		goto exit;
	}
	if (RTT_IS_STOPPED(rtt_status)) {
		DHD_ERROR(("RTT is stopped\n"));
		goto exit;
	}
	/* turn off mpc in case of non-associted */
	if (!dhd_is_associated(dhd, NULL, NULL)) {
		err = dhd_iovar(dhd, 0, "mpc", (char *)&mpc, sizeof(mpc), NULL, 0, TRUE);
		if (err) {
			DHD_ERROR(("%s : failed to set mpc\n", __FUNCTION__));
			goto exit;
		}
		rtt_status->mpc = 1; /* Either failure or complete, we need to enable mpc */
	}

	mutex_lock(&rtt_status->rtt_mutex);
	/* Get a target information */
	rtt_target = &rtt_status->rtt_config.target_info[rtt_status->cur_idx];
	mutex_unlock(&rtt_status->rtt_mutex);
	DHD_ERROR(("%s enter\n", __FUNCTION__));
	if (!RTT_IS_ENABLED(rtt_status)) {
		/* enable ftm */
		err = dhd_rtt_ftm_enable(dhd, TRUE);
		if (err) {
			DHD_ERROR(("failed to enable FTM (%d)\n", err));
			goto exit;
		}
	}

	/* delete session of index default sesession  */
	err = dhd_rtt_delete_session(dhd, FTM_DEFAULT_SESSION);
	if (err < 0 && err != BCME_NOTFOUND) {
		DHD_ERROR(("failed to delete session of FTM (%d)\n", err));
		goto exit;
	}
	rtt_status->status = RTT_ENABLED;
	memset(ftm_configs, 0, sizeof(ftm_configs));
	memset(ftm_params, 0, sizeof(ftm_params));

	/* configure the session 1 as initiator */
	ftm_configs[ftm_cfg_cnt].enable = TRUE;
	ftm_configs[ftm_cfg_cnt++].flags = WL_PROXD_SESSION_FLAG_INITIATOR;
	dhd_rtt_ftm_config(dhd, FTM_DEFAULT_SESSION, FTM_CONFIG_CAT_OPTIONS,
		ftm_configs, ftm_cfg_cnt);
	/* target's mac address */
	if (!ETHER_ISNULLADDR(rtt_target->addr.octet)) {
		ftm_params[ftm_param_cnt].mac_addr = rtt_target->addr;
		ftm_params[ftm_param_cnt++].tlvid = WL_PROXD_TLV_ID_PEER_MAC;
		DHD_ERROR((">\t target %s\n", bcm_ether_ntoa(&rtt_target->addr, eabuf)));
	}
	/* target's chanspec */
	if (rtt_target->chanspec) {
		ftm_params[ftm_param_cnt].chanspec = htol32((uint32)rtt_target->chanspec);
		ftm_params[ftm_param_cnt++].tlvid = WL_PROXD_TLV_ID_CHANSPEC;
		DHD_ERROR((">\t chanspec : %s\n", wf_chspec_ntoa(rtt_target->chanspec, chanbuf)));
	}
	/* num-burst */
	if (rtt_target->num_burst) {
		ftm_params[ftm_param_cnt].data16 = htol16(rtt_target->num_burst);
		ftm_params[ftm_param_cnt++].tlvid = WL_PROXD_TLV_ID_NUM_BURST;
		DHD_ERROR((">\t num of burst : %d\n", rtt_target->num_burst));
	}
	/* number of frame per burst */
	if (rtt_target->num_frames_per_burst == 0) {
		rtt_target->num_frames_per_burst =
			CHSPEC_IS20(rtt_target->chanspec) ? FTM_DEFAULT_CNT_20M :
			CHSPEC_IS40(rtt_target->chanspec) ? FTM_DEFAULT_CNT_40M :
			FTM_DEFAULT_CNT_80M;
	}
	ftm_params[ftm_param_cnt].data16 = htol16(rtt_target->num_frames_per_burst);
	ftm_params[ftm_param_cnt++].tlvid = WL_PROXD_TLV_ID_BURST_NUM_FTM;
	DHD_ERROR((">\t number of frame per burst : %d\n", rtt_target->num_frames_per_burst));
	/* FTM retry count */
	if (rtt_target->num_retries_per_ftm) {
		ftm_params[ftm_param_cnt].data8 = rtt_target->num_retries_per_ftm;
		ftm_params[ftm_param_cnt++].tlvid = WL_PROXD_TLV_ID_FTM_RETRIES;
		DHD_ERROR((">\t retry count of FTM  : %d\n", rtt_target->num_retries_per_ftm));
	}
	/* FTM Request retry count */
	if (rtt_target->num_retries_per_ftmr) {
		ftm_params[ftm_param_cnt].data8 = rtt_target->num_retries_per_ftmr;
		ftm_params[ftm_param_cnt++].tlvid = WL_PROXD_TLV_ID_FTM_REQ_RETRIES;
		DHD_ERROR((">\t retry count of FTM Req : %d\n", rtt_target->num_retries_per_ftm));
	}
	/* burst-period */
	if (rtt_target->burst_period) {
		ftm_params[ftm_param_cnt].data_intvl.intvl =
			htol32(rtt_target->burst_period); /* ms */
		ftm_params[ftm_param_cnt].data_intvl.tmu = WL_PROXD_TMU_MILLI_SEC;
		ftm_params[ftm_param_cnt++].tlvid = WL_PROXD_TLV_ID_BURST_PERIOD;
		DHD_ERROR((">\t burst period : %d ms\n", rtt_target->burst_period));
	}
	/* burst-duration */
	if (rtt_target->burst_duration) {
		ftm_params[ftm_param_cnt].data_intvl.intvl =
			htol32(rtt_target->burst_period); /* ms */
		ftm_params[ftm_param_cnt].data_intvl.tmu = WL_PROXD_TMU_MILLI_SEC;
		ftm_params[ftm_param_cnt++].tlvid = WL_PROXD_TLV_ID_BURST_DURATION;
		DHD_ERROR((">\t burst duration : %d ms\n",
			rtt_target->burst_duration));
	}
	if (rtt_target->bw && rtt_target->preamble) {
		bool use_default = FALSE;
		int nss;
		int mcs;
		switch (rtt_target->preamble) {
		case RTT_PREAMBLE_LEGACY:
			rspec |= WL_RSPEC_ENCODE_RATE; /* 11abg */
			rspec |= WL_RATE_6M;
			break;
		case RTT_PREAMBLE_HT:
			rspec |= WL_RSPEC_ENCODE_HT; /* 11n HT */
			mcs = 0; /* default MCS 0 */
			rspec |= mcs;
			break;
		case RTT_PREAMBLE_VHT:
			rspec |= WL_RSPEC_ENCODE_VHT; /* 11ac VHT */
			mcs = 0; /* default MCS 0 */
			nss = 1; /* default Nss = 1  */
			rspec |= (nss << WL_RSPEC_VHT_NSS_SHIFT) | mcs;
			break;
		default:
			DHD_ERROR(("doesn't support this preamble : %d\n", rtt_target->preamble));
			use_default = TRUE;
			break;
		}
		switch (rtt_target->bw) {
		case RTT_BW_20:
			rspec |= WL_RSPEC_BW_20MHZ;
			break;
		case RTT_BW_40:
			rspec |= WL_RSPEC_BW_40MHZ;
			break;
		case RTT_BW_80:
			rspec |= WL_RSPEC_BW_80MHZ;
			break;
		default:
			DHD_ERROR(("doesn't support this BW : %d\n", rtt_target->bw));
			use_default = TRUE;
			break;
		}
		if (!use_default) {
			ftm_params[ftm_param_cnt].data32 = htol32(rspec);
			ftm_params[ftm_param_cnt++].tlvid = WL_PROXD_TLV_ID_RATESPEC;
			DHD_ERROR((">\t ratespec : %d\n", rspec));
		}

	}
	dhd_rtt_ftm_config(dhd, FTM_DEFAULT_SESSION, FTM_CONFIG_CAT_GENERAL,
		ftm_params, ftm_param_cnt);

	err = dhd_rtt_start_session(dhd, FTM_DEFAULT_SESSION, TRUE);
	if (err) {
		DHD_ERROR(("failed to start session of FTM : error %d\n", err));
	}
exit:
	if (err) {
		rtt_status->status = RTT_STOPPED;
		/* disable FTM */
		dhd_rtt_ftm_enable(dhd, FALSE);
		if (rtt_status->mpc) {
			/* enable mpc again in case of error */
			mpc = 1;
			rtt_status->mpc = 0;
			err = dhd_iovar(dhd, 0, "mpc", (char *)&mpc, sizeof(mpc),
				NULL, 0, TRUE);
		}
	}
	return err;
}

int
dhd_rtt_register_noti_callback(dhd_pub_t *dhd, void *ctx, dhd_rtt_compl_noti_fn noti_fn)
{
	int err = BCME_OK;
	struct rtt_noti_callback *cb = NULL, *iter;
	rtt_status_info_t *rtt_status;
	NULL_CHECK(dhd, "dhd is NULL", err);
	NULL_CHECK(noti_fn, "noti_fn is NULL", err);

	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", err);
	spin_lock_bh(&noti_list_lock);
	list_for_each_entry(iter, &rtt_status->noti_fn_list, list) {
		if (iter->noti_fn == noti_fn) {
			goto exit;
		}
	}
	cb = kmalloc(sizeof(struct rtt_noti_callback), GFP_ATOMIC);
	if (!cb) {
		err = -ENOMEM;
		goto exit;
	}
	cb->noti_fn = noti_fn;
	cb->ctx = ctx;
	list_add(&cb->list, &rtt_status->noti_fn_list);
exit:
	spin_unlock_bh(&noti_list_lock);
	return err;
}

int
dhd_rtt_unregister_noti_callback(dhd_pub_t *dhd, dhd_rtt_compl_noti_fn noti_fn)
{
	int err = BCME_OK;
	struct rtt_noti_callback *cb = NULL, *iter;
	rtt_status_info_t *rtt_status;
	NULL_CHECK(dhd, "dhd is NULL", err);
	NULL_CHECK(noti_fn, "noti_fn is NULL", err);
	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", err);
	spin_lock_bh(&noti_list_lock);
	list_for_each_entry(iter, &rtt_status->noti_fn_list, list) {
		if (iter->noti_fn == noti_fn) {
			cb = iter;
			list_del(&cb->list);
			break;
		}
	}
	spin_unlock_bh(&noti_list_lock);
	if (cb) {
		kfree(cb);
	}
	return err;
}

static wifi_rate_t
dhd_rtt_convert_rate_to_host(uint32 rspec)
{
	wifi_rate_t host_rate;
	memset(&host_rate, 0, sizeof(wifi_rate_t));
	if ((rspec & WL_RSPEC_ENCODING_MASK) == WL_RSPEC_ENCODE_RATE) {
		host_rate.preamble = 0;
	} else if ((rspec & WL_RSPEC_ENCODING_MASK) == WL_RSPEC_ENCODE_HT) {
		host_rate.preamble = 2;
		host_rate.rateMcsIdx = rspec & WL_RSPEC_RATE_MASK;
	} else if ((rspec & WL_RSPEC_ENCODING_MASK) == WL_RSPEC_ENCODE_VHT) {
		host_rate.preamble = 3;
		host_rate.rateMcsIdx = rspec & WL_RSPEC_VHT_MCS_MASK;
		host_rate.nss = (rspec & WL_RSPEC_VHT_NSS_MASK) >> WL_RSPEC_VHT_NSS_SHIFT;
	}
	host_rate.bw = (rspec & WL_RSPEC_BW_MASK) - 1;
	host_rate.bitrate = rate_rspec2rate(rspec) / 100; /* 100kbps */
	DHD_RTT(("bit rate : %d\n", host_rate.bitrate));
	return host_rate;
}


static int
dhd_rtt_convert_results_to_host(rtt_report_t *rtt_report, uint8 *p_data, uint16 tlvid, uint16 len)
{
	int err = BCME_OK;
	char eabuf[ETHER_ADDR_STR_LEN];
	wl_proxd_rtt_result_t *p_data_info;
	wl_proxd_result_flags_t flags;
	wl_proxd_session_state_t session_state;
	wl_proxd_status_t proxd_status;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0))
	struct timespec64 ts;
#else
	struct timespec ts;
#endif
	uint32 ratespec;
	uint32 avg_dist;
	wl_proxd_rtt_sample_t *p_sample;
	wl_proxd_intvl_t rtt;
	wl_proxd_intvl_t p_time;

	NULL_CHECK(rtt_report, "rtt_report is NULL", err);
	NULL_CHECK(p_data, "p_data is NULL", err);
	DHD_RTT(("%s enter\n", __FUNCTION__));
	p_data_info = (wl_proxd_rtt_result_t *) p_data;
	/* unpack and format 'flags' for display */
	flags = ltoh16_ua(&p_data_info->flags);

	/* session state and status */
	session_state = ltoh16_ua(&p_data_info->state);
	proxd_status = ltoh32_ua(&p_data_info->status);
	DHD_RTT((">\tTarget(%s) session state=%d(%s), status=%d(%s)\n",
		bcm_ether_ntoa((&(p_data_info->peer)), eabuf),
		session_state,
		ftm_session_state_value_to_logstr(session_state),
		proxd_status,
		ftm_status_value_to_logstr(proxd_status)));

	/* show avg_dist (1/256m units), burst_num */
	avg_dist = ltoh32_ua(&p_data_info->avg_dist);
	if (avg_dist == 0xffffffff) {	/* report 'failure' case */
		DHD_RTT((">\tavg_dist=-1m, burst_num=%d, valid_measure_cnt=%d\n",
		ltoh16_ua(&p_data_info->burst_num),
		p_data_info->num_valid_rtt)); /* in a session */
		avg_dist = FTM_INVALID;
	}
	else {
		DHD_RTT((">\tavg_dist=%d.%04dm, burst_num=%d, valid_measure_cnt=%d num_ftm=%d\n",
			avg_dist >> 8, /* 1/256m units */
			((avg_dist & 0xff) * 625) >> 4,
			ltoh16_ua(&p_data_info->burst_num),
			p_data_info->num_valid_rtt,
			p_data_info->num_ftm)); /* in a session */
	}
	/* show 'avg_rtt' sample */
	p_sample = &p_data_info->avg_rtt;
	DHD_RTT((">\tavg_rtt sample: rssi=%d rtt=%d%s std_deviation =%d.%d ratespec=0x%08x\n",
		(int16) ltoh16_ua(&p_sample->rssi),
		ltoh32_ua(&p_sample->rtt.intvl),
		ftm_tmu_value_to_logstr(ltoh16_ua(&p_sample->rtt.tmu)),
		ltoh16_ua(&p_data_info->sd_rtt)/10, ltoh16_ua(&p_data_info->sd_rtt)%10,
		ltoh32_ua(&p_sample->ratespec)));

	/* set peer address */
	rtt_report->addr = p_data_info->peer;
	/* burst num */
	rtt_report->burst_num = ltoh16_ua(&p_data_info->burst_num);
	/* success num */
	rtt_report->success_num = p_data_info->num_valid_rtt;
	/* actual number of FTM supported by peer */
	rtt_report->num_per_burst_peer = p_data_info->num_ftm;
	rtt_report->negotiated_burst_num = p_data_info->num_ftm;
	/* status */
	rtt_report->status = ftm_get_statusmap_info(proxd_status,
			&ftm_status_map_info[0], ARRAYSIZE(ftm_status_map_info));
	/* rssi (0.5db) */
	rtt_report->rssi = (int16)ltoh16_ua(&p_data_info->avg_rtt.rssi) * 2;
	/* rx rate */
	ratespec = ltoh32_ua(&p_data_info->avg_rtt.ratespec);
	rtt_report->rx_rate = dhd_rtt_convert_rate_to_host(ratespec);
	/* tx rate */
	if (flags & WL_PROXD_RESULT_FLAG_VHTACK) {
		rtt_report->tx_rate = dhd_rtt_convert_rate_to_host(0x2010010);
	} else {
		rtt_report->tx_rate = dhd_rtt_convert_rate_to_host(0xc);
	}
	/* rtt_sd */
	rtt.tmu = ltoh16_ua(&p_data_info->avg_rtt.rtt.tmu);
	rtt.intvl = ltoh32_ua(&p_data_info->avg_rtt.rtt.intvl);
	rtt_report->rtt = FTM_INTVL2NSEC(&rtt) * 10; /* nano -> 0.1 nano */
	rtt_report->rtt_sd = ltoh16_ua(&p_data_info->sd_rtt); /* nano -> 0.1 nano */
	DHD_RTT(("rtt_report->rtt : %llu\n", rtt_report->rtt));
	DHD_RTT(("rtt_report->rssi : %d (0.5db)\n", rtt_report->rssi));

	/* average distance */
	if (avg_dist != FTM_INVALID) {
		rtt_report->distance = (avg_dist >> 8) * 100; /* meter -> cm */
		rtt_report->distance += (avg_dist & 0xff) * 100 / 256;
	} else {
		rtt_report->distance = FTM_INVALID;
	}
	/* time stamp */
	/* get the time elapsed from boot time */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0))
	ktime_get_boottime_ts64(&ts);
#else
	get_monotonic_boottime(&ts);
#endif
	rtt_report->ts = (uint64)TIMESPEC_TO_US(ts);

	if (proxd_status == WL_PROXD_E_REMOTE_FAIL) {
		/* retry time  after failure */
		p_time.intvl = ltoh32_ua(&p_data_info->u.retry_after.intvl);
		p_time.tmu = ltoh16_ua(&p_data_info->u.retry_after.tmu);
		rtt_report->retry_after_duration = FTM_INTVL2SEC(&p_time); /* s -> s */
		DHD_RTT((">\tretry_after: %d%s\n",
			ltoh32_ua(&p_data_info->u.retry_after.intvl),
			ftm_tmu_value_to_logstr(ltoh16_ua(&p_data_info->u.retry_after.tmu))));
	} else {
		/* burst duration */
		p_time.intvl = ltoh32_ua(&p_data_info->u.retry_after.intvl);
		p_time.tmu = ltoh16_ua(&p_data_info->u.retry_after.tmu);
		rtt_report->burst_duration =  FTM_INTVL2MSEC(&p_time); /* s -> ms */
		DHD_RTT((">\tburst_duration: %d%s\n",
			ltoh32_ua(&p_data_info->u.burst_duration.intvl),
			ftm_tmu_value_to_logstr(ltoh16_ua(&p_data_info->u.burst_duration.tmu))));
		DHD_RTT(("rtt_report->burst_duration : %d\n", rtt_report->burst_duration));
	}
	return err;
}

int
dhd_rtt_event_handler(dhd_pub_t *dhd, wl_event_msg_t *event, void *event_data)
{
	int ret = BCME_OK;
	int tlvs_len;
	int idx;
	uint16 version;
	wl_proxd_event_t *p_event;
	wl_proxd_event_type_t event_type;
	wl_proxd_ftm_session_status_t session_status;
	const ftm_strmap_entry_t *p_loginfo;
	rtt_status_info_t *rtt_status;
	rtt_target_info_t *rtt_target_info;
	struct rtt_noti_callback *iter;
	rtt_results_header_t *entry, *next, *rtt_results_header = NULL;
	rtt_result_t *rtt_result, *next2;
	gfp_t kflags;
	bool is_new = TRUE;

	NULL_CHECK(dhd, "dhd is NULL", ret);
	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", ret);

	if (ntoh32_ua((void *)&event->datalen) < OFFSETOF(wl_proxd_event_t, tlvs)) {
		DHD_RTT(("%s: wrong datalen:%d\n", __FUNCTION__,
			ntoh32_ua((void *)&event->datalen)));
		return -EINVAL;
	}
	event_type = ntoh32_ua((void *)&event->event_type);
	if (event_type != WLC_E_PROXD) {
		DHD_ERROR((" failed event \n"));
		return -EINVAL;
	}

	if (!event_data) {
		DHD_ERROR(("%s: event_data:NULL\n", __FUNCTION__));
		return -EINVAL;
	}

	if (RTT_IS_STOPPED(rtt_status)) {
		/* Ignore the Proxd event */
		return ret;
	}
	p_event = (wl_proxd_event_t *) event_data;
	version = ltoh16(p_event->version);
	if (version < WL_PROXD_API_VERSION) {
		DHD_ERROR(("ignore non-ftm event version = 0x%0x < WL_PROXD_API_VERSION (0x%x)\n",
			version, WL_PROXD_API_VERSION));
		return ret;
	}
	if (!in_atomic()) {
		mutex_lock(&rtt_status->rtt_mutex);
	}
	event_type = (wl_proxd_event_type_t) ltoh16(p_event->type);

	kflags = in_softirq()? GFP_ATOMIC : GFP_KERNEL;

	DHD_RTT(("event_type=0x%x, ntoh16()=0x%x, ltoh16()=0x%x\n",
		p_event->type, ntoh16(p_event->type), ltoh16(p_event->type)));
	p_loginfo = ftm_get_event_type_loginfo(event_type);
	if (p_loginfo == NULL) {
		DHD_ERROR(("receive an invalid FTM event %d\n", event_type));
		ret = -EINVAL;
		goto exit;	/* ignore this event */
	}
	/* get TLVs len, skip over event header */
	if (ltoh16(p_event->len) < OFFSETOF(wl_proxd_event_t, tlvs)) {
		DHD_ERROR(("invalid FTM event length:%d\n", ltoh16(p_event->len)));
		ret = -EINVAL;
		goto exit;
	}
	tlvs_len = ltoh16(p_event->len) - OFFSETOF(wl_proxd_event_t, tlvs);
	DHD_RTT(("receive '%s' event: version=0x%x len=%d method=%d sid=%d tlvs_len=%d\n",
		p_loginfo->text,
		version,
		ltoh16(p_event->len),
		ltoh16(p_event->method),
		ltoh16(p_event->sid),
		tlvs_len));
	rtt_target_info = &rtt_status->rtt_config.target_info[rtt_status->cur_idx];
	/* find a rtt_report_header for this mac address */
	list_for_each_entry(entry, &rtt_status->rtt_results_cache, list) {
		 if (!memcmp(&entry->peer_mac, &event->addr, ETHER_ADDR_LEN))  {
			/* found a rtt_report_header for peer_mac in the list */
			is_new = FALSE;
			rtt_results_header = entry;
			break;
		 }
	}

	switch (event_type) {
	case WL_PROXD_EVENT_SESSION_CREATE:
		DHD_RTT(("WL_PROXD_EVENT_SESSION_CREATE\n"));
		break;
	case WL_PROXD_EVENT_SESSION_START:
		DHD_RTT(("WL_PROXD_EVENT_SESSION_START\n"));
		break;
	case WL_PROXD_EVENT_BURST_START:
		DHD_RTT(("WL_PROXD_EVENT_BURST_START\n"));
		break;
	case WL_PROXD_EVENT_BURST_END:
		DHD_RTT(("WL_PROXD_EVENT_BURST_END\n"));
		if (is_new) {
			/* allocate new header for rtt_results */
			rtt_results_header = kzalloc(sizeof(rtt_results_header_t), kflags);
			if (!rtt_results_header) {
				ret = -ENOMEM;
				goto exit;
			}
			/* Initialize the head of list for rtt result */
			INIT_LIST_HEAD(&rtt_results_header->result_list);
			rtt_results_header->peer_mac = event->addr;
			list_add_tail(&rtt_results_header->list, &rtt_status->rtt_results_cache);
		}
		if (tlvs_len > 0) {
			/* allocate rtt_results for new results */
			rtt_result = kzalloc(sizeof(rtt_result_t), kflags);
			if (!rtt_result) {
				ret = -ENOMEM;
				goto exit;
			}
			/* unpack TLVs and invokes the cbfn to print the event content TLVs */
			ret = bcm_unpack_xtlv_buf((void *) &(rtt_result->report),
				(uint8 *)&p_event->tlvs[0], tlvs_len,
				BCM_XTLV_OPTION_ALIGN32, rtt_unpack_xtlv_cbfn);
			if (ret != BCME_OK) {
				DHD_ERROR(("%s : Failed to unpack xtlv for an event\n",
					__FUNCTION__));
				goto exit;
			}
			/* fill out the results from the configuration param */
			rtt_result->report.ftm_num = rtt_target_info->num_frames_per_burst;
			rtt_result->report.type = RTT_TWO_WAY;
			DHD_RTT(("report->ftm_num : %d\n", rtt_result->report.ftm_num));
			rtt_result->report_len = RTT_REPORT_SIZE;

			list_add_tail(&rtt_result->list, &rtt_results_header->result_list);
			rtt_results_header->result_cnt++;
			rtt_results_header->result_tot_len += rtt_result->report_len;
		}
		break;
	case WL_PROXD_EVENT_SESSION_END:
			DHD_RTT(("WL_PROXD_EVENT_SESSION_END\n"));
		if (!RTT_IS_ENABLED(rtt_status)) {
			DHD_RTT(("Ignore the session end evt\n"));
			goto exit;
		}
		if (tlvs_len > 0) {
			/* unpack TLVs and invokes the cbfn to print the event content TLVs */
			ret = bcm_unpack_xtlv_buf((void *) &session_status,
				(uint8 *)&p_event->tlvs[0], tlvs_len,
				BCM_XTLV_OPTION_ALIGN32, rtt_unpack_xtlv_cbfn);
			if (ret != BCME_OK) {
				DHD_ERROR(("%s : Failed to unpack xtlv for an event\n",
					__FUNCTION__));
				goto exit;
			}
		}
		/* In case of no result for the peer device, make fake result for error case */
		if (is_new) {
			/* allocate new header for rtt_results */
			rtt_results_header = kzalloc(sizeof(rtt_results_header_t), GFP_KERNEL);
			if (!rtt_results_header) {
				ret = -ENOMEM;
				goto exit;
			}
			/* Initialize the head of list for rtt result */
			INIT_LIST_HEAD(&rtt_results_header->result_list);
			rtt_results_header->peer_mac = event->addr;
			list_add_tail(&rtt_results_header->list, &rtt_status->rtt_results_cache);

			/* allocate rtt_results for new results */
			rtt_result = kzalloc(sizeof(rtt_result_t), kflags);
			if (!rtt_result) {
				ret = -ENOMEM;
				kfree(rtt_results_header);
				goto exit;
			}
			/* fill out the results from the configuration param */
			rtt_result->report.ftm_num = rtt_target_info->num_frames_per_burst;
			rtt_result->report.type = RTT_TWO_WAY;
			DHD_RTT(("report->ftm_num : %d\n", rtt_result->report.ftm_num));
			rtt_result->report_len = RTT_REPORT_SIZE;
			rtt_result->report.status = RTT_REASON_FAIL_NO_RSP;
			rtt_result->report.addr = rtt_target_info->addr;
			rtt_result->report.distance = FTM_INVALID;
			list_add_tail(&rtt_result->list, &rtt_results_header->result_list);
			rtt_results_header->result_cnt++;
			rtt_results_header->result_tot_len += rtt_result->report_len;
		}
		/* find next target to trigger RTT */
		for (idx = (rtt_status->cur_idx + 1);
			idx < rtt_status->rtt_config.rtt_target_cnt; idx++) {
			/* skip the disabled device */
			if (rtt_status->rtt_config.target_info[idx].disable) {
				continue;
			} else {
				/* set the idx to cur_idx */
				rtt_status->cur_idx = idx;
				break;
			}
		}
		if (idx < rtt_status->rtt_config.rtt_target_cnt) {
			/* restart to measure RTT from next device */
			schedule_work(&rtt_status->work);
		} else {
			DHD_RTT(("RTT_STOPPED\n"));
			rtt_status->status = RTT_STOPPED;
			/* to turn on mpc mode */
			schedule_work(&rtt_status->work);
			/* notify the completed information to others */
			list_for_each_entry(iter, &rtt_status->noti_fn_list, list) {
				iter->noti_fn(iter->ctx, &rtt_status->rtt_results_cache);
			}
			/* remove the rtt results in cache */
			if (!list_empty(&rtt_status->rtt_results_cache)) {
				/* Iterate rtt_results_header list */
				list_for_each_entry_safe(entry, next,
					&rtt_status->rtt_results_cache, list) {
					list_del(&entry->list);
					/* Iterate rtt_result list */
					list_for_each_entry_safe(rtt_result, next2,
						&entry->result_list, list) {
						list_del(&rtt_result->list);
						kfree(rtt_result);
					}
					kfree(entry);
				}
			}

			/* reinitialize the HEAD */
			INIT_LIST_HEAD(&rtt_status->rtt_results_cache);
			/* clear information for rtt_config */
			rtt_status->rtt_config.rtt_target_cnt = 0;
			memset(rtt_status->rtt_config.target_info, 0,
				TARGET_INFO_SIZE(RTT_MAX_TARGET_CNT));
			rtt_status->cur_idx = 0;
		}
		break;
	case WL_PROXD_EVENT_SESSION_RESTART:
		DHD_RTT(("WL_PROXD_EVENT_SESSION_RESTART\n"));
		break;
	case WL_PROXD_EVENT_BURST_RESCHED:
		DHD_RTT(("WL_PROXD_EVENT_BURST_RESCHED\n"));
		break;
	case WL_PROXD_EVENT_SESSION_DESTROY:
		DHD_RTT(("WL_PROXD_EVENT_SESSION_DESTROY\n"));
		break;
	case WL_PROXD_EVENT_FTM_FRAME:
		DHD_RTT(("WL_PROXD_EVENT_FTM_FRAME\n"));
		break;
	case WL_PROXD_EVENT_DELAY:
		DHD_RTT(("WL_PROXD_EVENT_DELAY\n"));
		break;
	case WL_PROXD_EVENT_VS_INITIATOR_RPT:
		DHD_RTT(("WL_PROXD_EVENT_VS_INITIATOR_RPT\n "));
		break;
	case WL_PROXD_EVENT_RANGING:
		DHD_RTT(("WL_PROXD_EVENT_RANGING\n"));
		break;

	default:
		DHD_ERROR(("WLC_E_PROXD: not supported EVENT Type:%d\n", event_type));
		break;
	}
exit:
	if (!in_atomic()) {
		mutex_unlock(&rtt_status->rtt_mutex);
	}

	return ret;
}

static void
dhd_rtt_work(struct work_struct *work)
{
	rtt_status_info_t *rtt_status;
	dhd_pub_t *dhd;
	rtt_status = container_of(work, rtt_status_info_t, work);
	if (rtt_status == NULL) {
		DHD_ERROR(("%s : rtt_status is NULL\n", __FUNCTION__));
		return;
	}
	dhd = rtt_status->dhd;
	if (dhd == NULL) {
		DHD_ERROR(("%s : dhd is NULL\n", __FUNCTION__));
		return;
	}
	(void) dhd_rtt_start(dhd);
}

int
dhd_rtt_capability(dhd_pub_t *dhd, rtt_capabilities_t *capa)
{
	rtt_status_info_t *rtt_status;
	int err = BCME_OK;
	NULL_CHECK(dhd, "dhd is NULL", err);
	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", err);
	NULL_CHECK(capa, "capa is NULL", err);
	bzero(capa, sizeof(rtt_capabilities_t));
	switch (rtt_status->rtt_capa.proto) {
	case RTT_CAP_ONE_WAY:
		capa->rtt_one_sided_supported = 1;
		break;
	case RTT_CAP_FTM_WAY:
		capa->rtt_ftm_supported = 1;
		break;
	}

	switch (rtt_status->rtt_capa.feature) {
	case RTT_FEATURE_LCI:
		capa->lci_support = 1;
		break;
	case RTT_FEATURE_LCR:
		capa->lcr_support = 1;
		break;
	case RTT_FEATURE_PREAMBLE:
		capa->preamble_support = 1;
		break;
	case RTT_FEATURE_BW:
		capa->bw_support = 1;
		break;
	}
	/* bit mask */
	capa->preamble_support = rtt_status->rtt_capa.preamble;
	capa->bw_support = rtt_status->rtt_capa.bw;

	return err;
}

int
dhd_rtt_init(dhd_pub_t *dhd)
{
	int err = BCME_OK, ret;
	int32 up = 1;
	int32 version;
	rtt_status_info_t *rtt_status;
	NULL_CHECK(dhd, "dhd is NULL", err);
	if (dhd->rtt_state) {
		return err;
	}
	dhd->rtt_state = kzalloc(sizeof(rtt_status_info_t), GFP_KERNEL);
	if (dhd->rtt_state == NULL) {
		err = BCME_NOMEM;
		DHD_ERROR(("%s : failed to create rtt_state\n", __FUNCTION__));
		return err;
	}
	bzero(dhd->rtt_state, sizeof(rtt_status_info_t));
	rtt_status = GET_RTTSTATE(dhd);
	rtt_status->rtt_config.target_info =
			kzalloc(TARGET_INFO_SIZE(RTT_MAX_TARGET_CNT), GFP_KERNEL);
	if (rtt_status->rtt_config.target_info == NULL) {
		DHD_ERROR(("%s failed to allocate the target info for %d\n",
			__FUNCTION__, RTT_MAX_TARGET_CNT));
		err = BCME_NOMEM;
		goto exit;
	}
	rtt_status->dhd = dhd;
	/* need to do WLC_UP  */
	dhd_wl_ioctl_cmd(dhd, WLC_UP, (char *)&up, sizeof(int32), TRUE, 0);

	ret = dhd_rtt_get_version(dhd, &version);
	if (ret == BCME_OK && (version == WL_PROXD_API_VERSION)) {
		DHD_ERROR(("%s : FTM is supported\n", __FUNCTION__));
		/* rtt_status->rtt_capa.proto |= RTT_CAP_ONE_WAY; */
		rtt_status->rtt_capa.proto |= RTT_CAP_FTM_WAY;

		/* indicate to set tx rate */
		rtt_status->rtt_capa.feature |= RTT_FEATURE_PREAMBLE;
		rtt_status->rtt_capa.preamble |= RTT_PREAMBLE_VHT;
		rtt_status->rtt_capa.preamble |= RTT_PREAMBLE_HT;

		/* indicate to set bandwith */
		rtt_status->rtt_capa.feature |= RTT_FEATURE_BW;
		rtt_status->rtt_capa.bw |= RTT_BW_20;
		rtt_status->rtt_capa.bw |= RTT_BW_40;
		rtt_status->rtt_capa.bw |= RTT_BW_80;
	} else {
		if ((ret != BCME_OK) || (version == 0)) {
			DHD_ERROR(("%s : FTM is not supported\n", __FUNCTION__));
		} else {
			DHD_ERROR(("%s : FTM version mismatch between HOST (%d) and FW (%d)\n",
				__FUNCTION__, WL_PROXD_API_VERSION, version));
		}
	}
	/* cancel all of RTT request once we got the cancel request */
	rtt_status->all_cancel = TRUE;
	mutex_init(&rtt_status->rtt_mutex);
	INIT_LIST_HEAD(&rtt_status->noti_fn_list);
	INIT_LIST_HEAD(&rtt_status->rtt_results_cache);
	INIT_WORK(&rtt_status->work, dhd_rtt_work);
exit:
	if (err < 0) {
		kfree(rtt_status->rtt_config.target_info);
		kfree(dhd->rtt_state);
	}
	return err;
}

int
dhd_rtt_deinit(dhd_pub_t *dhd)
{
	int err = BCME_OK;
	rtt_status_info_t *rtt_status;
	rtt_results_header_t *rtt_header, *next;
	rtt_result_t *rtt_result, *next2;
	struct rtt_noti_callback *iter, *iter2;
	NULL_CHECK(dhd, "dhd is NULL", err);
	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", err);
	rtt_status->status = RTT_STOPPED;
	/* clear evt callback list */
	if (!list_empty(&rtt_status->noti_fn_list)) {
		list_for_each_entry_safe(iter, iter2, &rtt_status->noti_fn_list, list) {
			list_del(&iter->list);
			kfree(iter);
		}
	}
	/* remove the rtt results */
	if (!list_empty(&rtt_status->rtt_results_cache)) {
		list_for_each_entry_safe(rtt_header, next, &rtt_status->rtt_results_cache, list) {
			list_del(&rtt_header->list);
			list_for_each_entry_safe(rtt_result, next2,
				&rtt_header->result_list, list) {
				list_del(&rtt_result->list);
				kfree(rtt_result);
			}
			kfree(rtt_header);
		}
	}
	kfree(rtt_status->rtt_config.target_info);
	kfree(dhd->rtt_state);
	dhd->rtt_state = NULL;
	return err;
}
