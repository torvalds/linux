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
#include <bcmwifi_rspec.h>

#include <bcmevent.h>
#include <dhd.h>
#include <dhd_linux.h>
#include <dhd_rtt.h>
#include <dhd_dbg.h>
#include <dhd_bus.h>
#include <wldev_common.h>
#ifdef WL_CFG80211
#include <wl_cfg80211.h>
#endif /* WL_CFG80211 */
#ifdef WL_NAN
#include <wl_cfgnan.h>
#endif /* WL_NAN */

static DEFINE_SPINLOCK(noti_list_lock);
#ifndef NULL_CHECK
#define NULL_CHECK(p, s, err)  \
			do { \
				if (!(p)) { \
					printf("NULL POINTER (%s) : %s\n", __FUNCTION__, (s)); \
					err = BCME_ERROR; \
					return err; \
				} \
			} while (0)
#endif // endif

#define TIMESPEC_TO_US(ts)  (((uint64)(ts).tv_sec * USEC_PER_SEC) + \
							(ts).tv_nsec / NSEC_PER_USEC)

#undef DHD_RTT_MEM
#undef DHD_RTT_ERR
#define DHD_RTT_MEM DHD_LOG_MEM
#define DHD_RTT_ERR DHD_ERROR

#define FTM_IOC_BUFSZ  2048	/* ioc buffsize for our module (> BCM_XTLV_HDR_SIZE) */
#define FTM_AVAIL_MAX_SLOTS		32
#define FTM_MAX_CONFIGS 10
#define FTM_MAX_PARAMS 10
#define FTM_DEFAULT_SESSION 1
#define FTM_BURST_TIMEOUT_UNIT 250 /* 250 ns */
#define FTM_INVALID -1
#define	FTM_DEFAULT_CNT_20M		24u
#define FTM_DEFAULT_CNT_40M		16u
#define FTM_DEFAULT_CNT_80M		11u
/* To handle congestion env, set max dur/timeout */
#define FTM_MAX_BURST_DUR_TMO_MS	128u

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
#define CH_MIN_5G_CHANNEL 34

/* CUR ETH became obsolete with this major version onwards */
#define RTT_IOV_CUR_ETH_OBSOLETE 12

/* PROXD TIMEOUT */
#define DHD_RTT_TIMER_INTERVAL_MS	5000u
#define DHD_NAN_RTT_TIMER_INTERVAL_MS	10000u

struct rtt_noti_callback {
	struct list_head list;
	void *ctx;
	dhd_rtt_compl_noti_fn noti_fn;
};

/* bitmask indicating which command groups; */
typedef enum {
	FTM_SUBCMD_FLAG_METHOD	= 0x01,	/* FTM method command */
	FTM_SUBCMD_FLAG_SESSION = 0x02,	/* FTM session command */
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
	uint32 flags;				/* wl_proxd_flags_t/wl_proxd_session_flags_t */
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
		uint32 event_mask;
	};
} ftm_config_param_info_t;

/*
* definition for id-string mapping.
*   This is used to map an id (can be cmd-id, tlv-id, ....) to a text-string
*   for debug-display or cmd-log-display
*/
typedef struct ftm_strmap_entry {
	int32		id;
	char		*text;
} ftm_strmap_entry_t;

typedef struct ftm_status_map_host_entry {
	wl_proxd_status_t proxd_status;
	rtt_reason_t rtt_reason;
} ftm_status_map_host_entry_t;

static uint16
rtt_result_ver(uint16 tlvid, const uint8 *p_data);

static int
dhd_rtt_convert_results_to_host_v1(rtt_result_t *rtt_result, const uint8 *p_data,
	uint16 tlvid, uint16 len);

static int
dhd_rtt_convert_results_to_host_v2(rtt_result_t *rtt_result, const uint8 *p_data,
	uint16 tlvid, uint16 len);

static wifi_rate_t
dhd_rtt_convert_rate_to_host(uint32 ratespec);

#if defined(WL_CFG80211) && defined(RTT_DEBUG)
const char *
ftm_cmdid_to_str(uint16 cmdid);
#endif /* WL_CFG80211 && RTT_DEBUG */

#ifdef WL_CFG80211
static int
dhd_rtt_start(dhd_pub_t *dhd);
static int dhd_rtt_create_failure_result(rtt_status_info_t *rtt_status,
	struct ether_addr *addr);
static void dhd_rtt_handle_rtt_session_end(dhd_pub_t *dhd);
static void dhd_rtt_timeout_work(struct work_struct *work);
#endif /* WL_CFG80211 */
static const int burst_duration_idx[]  = {0, 0, 1, 2, 4, 8, 16, 32, 64, 128, 0, 0};

/* ftm status mapping to host status */
static const ftm_status_map_host_entry_t ftm_status_map_info[] = {
	{WL_PROXD_E_INCOMPLETE, RTT_STATUS_FAILURE},
	{WL_PROXD_E_OVERRIDDEN, RTT_STATUS_FAILURE},
	{WL_PROXD_E_ASAP_FAILED, RTT_STATUS_FAILURE},
	{WL_PROXD_E_NOTSTARTED, RTT_STATUS_FAIL_NOT_SCHEDULED_YET},
	{WL_PROXD_E_INVALIDMEAS, RTT_STATUS_FAIL_INVALID_TS},
	{WL_PROXD_E_INCAPABLE, RTT_STATUS_FAIL_NO_CAPABILITY},
	{WL_PROXD_E_MISMATCH, RTT_STATUS_FAILURE},
	{WL_PROXD_E_DUP_SESSION, RTT_STATUS_FAILURE},
	{WL_PROXD_E_REMOTE_FAIL, RTT_STATUS_FAILURE},
	{WL_PROXD_E_REMOTE_INCAPABLE, RTT_STATUS_FAILURE},
	{WL_PROXD_E_SCHED_FAIL, RTT_STATUS_FAIL_SCHEDULE},
	{WL_PROXD_E_PROTO, RTT_STATUS_FAIL_PROTOCOL},
	{WL_PROXD_E_EXPIRED, RTT_STATUS_FAILURE},
	{WL_PROXD_E_TIMEOUT, RTT_STATUS_FAIL_TM_TIMEOUT},
	{WL_PROXD_E_NOACK, RTT_STATUS_FAIL_NO_RSP},
	{WL_PROXD_E_DEFERRED, RTT_STATUS_FAILURE},
	{WL_PROXD_E_INVALID_SID, RTT_STATUS_FAILURE},
	{WL_PROXD_E_REMOTE_CANCEL, RTT_STATUS_FAILURE},
	{WL_PROXD_E_CANCELED, RTT_STATUS_ABORTED},
	{WL_PROXD_E_INVALID_SESSION, RTT_STATUS_FAILURE},
	{WL_PROXD_E_BAD_STATE, RTT_STATUS_FAILURE},
	{WL_PROXD_E_ERROR, RTT_STATUS_FAILURE},
	{WL_PROXD_E_OK, RTT_STATUS_SUCCESS}
};

static const ftm_strmap_entry_t ftm_event_type_loginfo[] = {
	/* wl_proxd_event_type_t,		text-string */
	{ WL_PROXD_EVENT_NONE,			"none" },
	{ WL_PROXD_EVENT_SESSION_CREATE,	"session create" },
	{ WL_PROXD_EVENT_SESSION_START,		"session start" },
	{ WL_PROXD_EVENT_FTM_REQ,		"FTM req" },
	{ WL_PROXD_EVENT_BURST_START,		"burst start" },
	{ WL_PROXD_EVENT_BURST_END,		"burst end" },
	{ WL_PROXD_EVENT_SESSION_END,		"session end" },
	{ WL_PROXD_EVENT_SESSION_RESTART,	"session restart" },
	{ WL_PROXD_EVENT_BURST_RESCHED,		"burst rescheduled" },
	{ WL_PROXD_EVENT_SESSION_DESTROY,	"session destroy" },
	{ WL_PROXD_EVENT_RANGE_REQ,		"range request" },
	{ WL_PROXD_EVENT_FTM_FRAME,		"FTM frame" },
	{ WL_PROXD_EVENT_DELAY,			"delay" },
	{ WL_PROXD_EVENT_VS_INITIATOR_RPT,	"initiator-report " }, /* rx initiator-rpt */
	{ WL_PROXD_EVENT_RANGING,		"ranging " },
	{ WL_PROXD_EVENT_COLLECT,		"collect" },
	{ WL_PROXD_EVENT_MF_STATS,		"mf_stats" },
};

/*
* session-state --> text string mapping
*/
static const ftm_strmap_entry_t ftm_session_state_value_loginfo[] = {
	/* wl_proxd_session_state_t,		text string */
	{ WL_PROXD_SESSION_STATE_CREATED,	"created" },
	{ WL_PROXD_SESSION_STATE_CONFIGURED,	"configured" },
	{ WL_PROXD_SESSION_STATE_STARTED,	"started" },
	{ WL_PROXD_SESSION_STATE_DELAY,		"delay" },
	{ WL_PROXD_SESSION_STATE_USER_WAIT,	"user-wait" },
	{ WL_PROXD_SESSION_STATE_SCHED_WAIT,	"sched-wait" },
	{ WL_PROXD_SESSION_STATE_BURST,		"burst" },
	{ WL_PROXD_SESSION_STATE_STOPPING,	"stopping" },
	{ WL_PROXD_SESSION_STATE_ENDED,		"ended" },
	{ WL_PROXD_SESSION_STATE_DESTROYING,	"destroying" },
	{ WL_PROXD_SESSION_STATE_NONE,		"none" }
};

/*
* status --> text string mapping
*/
static const ftm_strmap_entry_t ftm_status_value_loginfo[] = {
	/* wl_proxd_status_t,			text-string */
	{ WL_PROXD_E_OVERRIDDEN,		"overridden" },
	{ WL_PROXD_E_ASAP_FAILED,		"ASAP failed" },
	{ WL_PROXD_E_NOTSTARTED,		"not started" },
	{ WL_PROXD_E_INVALIDMEAS,		"invalid measurement" },
	{ WL_PROXD_E_INCAPABLE,			"incapable" },
	{ WL_PROXD_E_MISMATCH,			"mismatch"},
	{ WL_PROXD_E_DUP_SESSION,		"dup session" },
	{ WL_PROXD_E_REMOTE_FAIL,		"remote fail" },
	{ WL_PROXD_E_REMOTE_INCAPABLE,		"remote incapable" },
	{ WL_PROXD_E_SCHED_FAIL,		"sched failure" },
	{ WL_PROXD_E_PROTO,			"protocol error" },
	{ WL_PROXD_E_EXPIRED,			"expired" },
	{ WL_PROXD_E_TIMEOUT,			"timeout" },
	{ WL_PROXD_E_NOACK,			"no ack" },
	{ WL_PROXD_E_DEFERRED,			"deferred" },
	{ WL_PROXD_E_INVALID_SID,		"invalid session id" },
	{ WL_PROXD_E_REMOTE_CANCEL,		"remote cancel" },
	{ WL_PROXD_E_CANCELED,			"canceled" },
	{ WL_PROXD_E_INVALID_SESSION,		"invalid session" },
	{ WL_PROXD_E_BAD_STATE,			"bad state" },
	{ WL_PROXD_E_ERROR,			"error" },
	{ WL_PROXD_E_OK,			"OK" }
};

/*
* time interval unit --> text string mapping
*/
static const ftm_strmap_entry_t ftm_tmu_value_loginfo[] = {
	/* wl_proxd_tmu_t,		text-string */
	{ WL_PROXD_TMU_TU,		"TU" },
	{ WL_PROXD_TMU_SEC,		"sec" },
	{ WL_PROXD_TMU_MILLI_SEC,	"ms" },
	{ WL_PROXD_TMU_MICRO_SEC,	"us" },
	{ WL_PROXD_TMU_NANO_SEC,	"ns" },
	{ WL_PROXD_TMU_PICO_SEC,	"ps" }
};

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
static uint32
rate_rspec2rate(uint32 rspec)
{
	int rate = 0;

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
		if (mcs > 9 || nss > 8) {
			DHD_RTT(("%s: Invalid mcs %d or nss %d\n", __FUNCTION__, mcs, nss));
			goto exit;
		}

		rate = rate_mcs2rate(mcs, nss, RSPEC_BW(rspec), RSPEC_ISSGI(rspec));
	} else {
		DHD_RTT(("%s: wrong rspec:%d\n", __FUNCTION__, rspec));
	}
exit:
	return rate;
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
	return RTT_STATUS_FAILURE; /* not found */
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

#if defined(WL_CFG80211) && defined(RTT_DEBUG)
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
const char *
ftm_cmdid_to_str(uint16 cmdid)
{
	return ftm_map_id_to_str((int32) cmdid, &ftm_cmdid_map[0], ARRAYSIZE(ftm_cmdid_map));
}
#endif /* WL_CFG80211 && RTT_DEBUG */

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

#ifdef WL_CFG80211
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
		DHD_RTT_ERR(("%s: failed to send getbuf proxd iovar (CMD ID : %d), status=%d\n",
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
		DHD_RTT_ERR(("%s: alert, p_iovresp->len(%d) should not be smaller than %d\n",
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
	uint32 kflags;
	wl_proxd_tlv_t *p_tlv;
	wl_proxd_iov_t *p_proxd_iov = (wl_proxd_iov_t *) NULL;

	*p_out_bufsize = 0;	/* init */
	kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	/* calculate the whole buffer size, including one reserve-tlv entry in the header */
	proxd_iovsize = sizeof(wl_proxd_iov_t) + tlvs_bufsize;

	p_proxd_iov = kzalloc(proxd_iovsize, kflags);
	if (p_proxd_iov == NULL) {
		DHD_RTT_ERR(("error: failed to allocate %d bytes of memory\n", proxd_iovsize));
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
#endif // endif
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
	DHD_RTT(("enter %s: method=%d, session_id=%d, cmdid=%d(%s)\n",
		__FUNCTION__, method, session_id, p_subcmd_info->cmdid,
		ftm_cmdid_to_str(p_subcmd_info->cmdid)));
#endif // endif

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
		DHD_RTT(("error: IOVAR failed, status=%d\n", ret));
	}
#endif // endif
	/* clean up */
	kfree(p_proxd_iov);

	return ret;
}
#endif /* WL_CFG80211 */

/* gets the length and returns the version
 * of the wl_proxd_collect_event_t version
 */
static uint
rtt_collect_data_event_ver(uint16 len)
{
	if (len > sizeof(wl_proxd_collect_event_data_v3_t)) {
		return WL_PROXD_COLLECT_EVENT_DATA_VERSION_MAX;
	} else if (len == sizeof(wl_proxd_collect_event_data_v3_t)) {
		return WL_PROXD_COLLECT_EVENT_DATA_VERSION_3;
	} else if (len == sizeof(wl_proxd_collect_event_data_v2_t)) {
		return WL_PROXD_COLLECT_EVENT_DATA_VERSION_2;
	} else {
		return WL_PROXD_COLLECT_EVENT_DATA_VERSION_1;
	}
}

static void
rtt_collect_event_data_display(uint8 ver, void *ctx, const uint8 *p_data, uint16 len)
{
	int i;
	wl_proxd_collect_event_data_v1_t *p_collect_data_v1 = NULL;
	wl_proxd_collect_event_data_v2_t *p_collect_data_v2 = NULL;
	wl_proxd_collect_event_data_v3_t *p_collect_data_v3 = NULL;

	if (!ctx || !p_data) {
		return;
	}

	switch (ver) {
	case WL_PROXD_COLLECT_EVENT_DATA_VERSION_1:
		DHD_RTT(("\tVERSION_1\n"));
		memcpy(ctx, p_data, sizeof(wl_proxd_collect_event_data_v1_t));
		p_collect_data_v1 = (wl_proxd_collect_event_data_v1_t *)ctx;
		DHD_RTT(("\tH_RX\n"));
		for (i = 0; i < K_TOF_COLLECT_H_SIZE_20MHZ; i++) {
			p_collect_data_v1->H_RX[i] = ltoh32_ua(&p_collect_data_v1->H_RX[i]);
			DHD_RTT(("\t%u\n", p_collect_data_v1->H_RX[i]));
		}
		DHD_RTT(("\n"));
		DHD_RTT(("\tH_LB\n"));
		for (i = 0; i < K_TOF_COLLECT_H_SIZE_20MHZ; i++) {
			p_collect_data_v1->H_LB[i] = ltoh32_ua(&p_collect_data_v1->H_LB[i]);
			DHD_RTT(("\t%u\n", p_collect_data_v1->H_LB[i]));
		}
		DHD_RTT(("\n"));
		DHD_RTT(("\tri_rr\n"));
		for (i = 0; i < FTM_TPK_RI_RR_LEN; i++) {
			DHD_RTT(("\t%u\n", p_collect_data_v1->ri_rr[i]));
		}
		p_collect_data_v1->phy_err_mask = ltoh32_ua(&p_collect_data_v1->phy_err_mask);
		DHD_RTT(("\tphy_err_mask=0x%x\n", p_collect_data_v1->phy_err_mask));
		break;
	case WL_PROXD_COLLECT_EVENT_DATA_VERSION_2:
		memcpy(ctx, p_data, sizeof(wl_proxd_collect_event_data_v2_t));
		p_collect_data_v2 = (wl_proxd_collect_event_data_v2_t *)ctx;
		DHD_RTT(("\tH_RX\n"));
		for (i = 0; i < K_TOF_COLLECT_H_SIZE_20MHZ; i++) {
			p_collect_data_v2->H_RX[i] = ltoh32_ua(&p_collect_data_v2->H_RX[i]);
			DHD_RTT(("\t%u\n", p_collect_data_v2->H_RX[i]));
		}
		DHD_RTT(("\n"));
		DHD_RTT(("\tH_LB\n"));
		for (i = 0; i < K_TOF_COLLECT_H_SIZE_20MHZ; i++) {
			p_collect_data_v2->H_LB[i] = ltoh32_ua(&p_collect_data_v2->H_LB[i]);
			DHD_RTT(("\t%u\n", p_collect_data_v2->H_LB[i]));
		}
		DHD_RTT(("\n"));
		DHD_RTT(("\tri_rr\n"));
		for (i = 0; i < FTM_TPK_RI_RR_LEN_SECURE_2_0; i++) {
			DHD_RTT(("\t%u\n", p_collect_data_v2->ri_rr[i]));
		}
		p_collect_data_v2->phy_err_mask = ltoh32_ua(&p_collect_data_v2->phy_err_mask);
		DHD_RTT(("\tphy_err_mask=0x%x\n", p_collect_data_v2->phy_err_mask));
		break;
	case WL_PROXD_COLLECT_EVENT_DATA_VERSION_3:
		memcpy(ctx, p_data, sizeof(wl_proxd_collect_event_data_v3_t));
		p_collect_data_v3 = (wl_proxd_collect_event_data_v3_t *)ctx;
		switch (p_collect_data_v3->version) {
		case WL_PROXD_COLLECT_EVENT_DATA_VERSION_3:
			if (p_collect_data_v3->length !=
				(len - OFFSETOF(wl_proxd_collect_event_data_v3_t, H_LB))) {
				DHD_RTT(("\tversion/length mismatch\n"));
				break;
			}
			DHD_RTT(("\tH_RX\n"));
			for (i = 0; i < K_TOF_COLLECT_H_SIZE_20MHZ; i++) {
				p_collect_data_v3->H_RX[i] =
					ltoh32_ua(&p_collect_data_v3->H_RX[i]);
				DHD_RTT(("\t%u\n", p_collect_data_v3->H_RX[i]));
			}
			DHD_RTT(("\n"));
			DHD_RTT(("\tH_LB\n"));
			for (i = 0; i < K_TOF_COLLECT_H_SIZE_20MHZ; i++) {
				p_collect_data_v3->H_LB[i] =
					ltoh32_ua(&p_collect_data_v3->H_LB[i]);
				DHD_RTT(("\t%u\n", p_collect_data_v3->H_LB[i]));
			}
			DHD_RTT(("\n"));
			DHD_RTT(("\tri_rr\n"));
			for (i = 0; i < FTM_TPK_RI_RR_LEN_SECURE_2_0; i++) {
				DHD_RTT(("\t%u\n", p_collect_data_v3->ri_rr[i]));
			}
			p_collect_data_v3->phy_err_mask =
				ltoh32_ua(&p_collect_data_v3->phy_err_mask);
			DHD_RTT(("\tphy_err_mask=0x%x\n", p_collect_data_v3->phy_err_mask));
			break;
		/* future case */
		}
		break;
	}
}

static uint16
rtt_result_ver(uint16 tlvid, const uint8 *p_data)
{
	uint16 ret = BCME_OK;
	const wl_proxd_rtt_result_v2_t *r_v2 = NULL;

	switch (tlvid) {
	case WL_PROXD_TLV_ID_RTT_RESULT:
		BCM_REFERENCE(p_data);
		ret = WL_PROXD_RTT_RESULT_VERSION_1;
		break;
	case WL_PROXD_TLV_ID_RTT_RESULT_V2:
		if (p_data) {
			r_v2 = (const wl_proxd_rtt_result_v2_t *)p_data;
			if (r_v2->version == WL_PROXD_RTT_RESULT_VERSION_2) {
				ret = WL_PROXD_RTT_RESULT_VERSION_2;
			}
		}
		break;
	default:
		DHD_RTT_ERR(("%s: > Unsupported TLV ID %d\n",
			__FUNCTION__, tlvid));
		break;
	}
	return ret;
}

/* pretty hex print a contiguous buffer */
static void
rtt_prhex(const char *msg, const uint8 *buf, uint nbytes)
{
	char line[128], *p;
	int len = sizeof(line);
	int nchar;
	uint i;

	if (msg && (msg[0] != '\0'))
		DHD_RTT(("%s:\n", msg));

	p = line;
	for (i = 0; i < nbytes; i++) {
		if (i % 16 == 0) {
			nchar = snprintf(p, len, "  %04d: ", i);	/* line prefix */
			p += nchar;
			len -= nchar;
		}
		if (len > 0) {
			nchar = snprintf(p, len, "%02x ", buf[i]);
			p += nchar;
			len -= nchar;
		}

		if (i % 16 == 15) {
			DHD_RTT(("%s\n", line));	/* flush line */
			p = line;
			len = sizeof(line);
		}
	}

	/* flush last partial line */
	if (p != line)
		DHD_RTT(("%s\n", line));
}

static int
rtt_unpack_xtlv_cbfn(void *ctx, const uint8 *p_data, uint16 tlvid, uint16 len)
{
	int ret = BCME_OK;
	int i;
	wl_proxd_ftm_session_status_t *p_data_info = NULL;
	uint32 chan_data_entry = 0;
	uint16 expected_rtt_result_ver = 0;

	BCM_REFERENCE(p_data_info);

	switch (tlvid) {
	case WL_PROXD_TLV_ID_RTT_RESULT:
	case WL_PROXD_TLV_ID_RTT_RESULT_V2:
		DHD_RTT(("WL_PROXD_TLV_ID_RTT_RESULT\n"));
		expected_rtt_result_ver = rtt_result_ver(tlvid, p_data);
		switch (expected_rtt_result_ver) {
		case WL_PROXD_RTT_RESULT_VERSION_1:
			ret = dhd_rtt_convert_results_to_host_v1((rtt_result_t *)ctx,
					p_data, tlvid, len);
			break;
		case WL_PROXD_RTT_RESULT_VERSION_2:
			ret = dhd_rtt_convert_results_to_host_v2((rtt_result_t *)ctx,
					p_data, tlvid, len);
			break;
		default:
			DHD_RTT_ERR((" > Unsupported RTT_RESULT version\n"));
			ret = BCME_UNSUPPORTED;
			break;
		}
		break;
	case WL_PROXD_TLV_ID_SESSION_STATUS:
		DHD_RTT(("WL_PROXD_TLV_ID_SESSION_STATUS\n"));
		memcpy(ctx, p_data, sizeof(wl_proxd_ftm_session_status_t));
		p_data_info = (wl_proxd_ftm_session_status_t *)ctx;
		p_data_info->sid = ltoh16_ua(&p_data_info->sid);
		p_data_info->state = ltoh16_ua(&p_data_info->state);
		p_data_info->status = ltoh32_ua(&p_data_info->status);
		p_data_info->burst_num = ltoh16_ua(&p_data_info->burst_num);
		DHD_RTT(("\tsid=%u, state=%d, status=%d, burst_num=%u\n",
			p_data_info->sid, p_data_info->state,
			p_data_info->status, p_data_info->burst_num));

		break;
	case WL_PROXD_TLV_ID_COLLECT_DATA:
		DHD_RTT(("WL_PROXD_TLV_ID_COLLECT_DATA\n"));
		rtt_collect_event_data_display(
			rtt_collect_data_event_ver(len),
			ctx, p_data, len);
		break;
	case WL_PROXD_TLV_ID_COLLECT_CHAN_DATA:
		GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
		DHD_RTT(("WL_PROXD_TLV_ID_COLLECT_CHAN_DATA\n"));
		DHD_RTT(("\tchan est %u\n", (uint32) (len / sizeof(uint32))));
		for (i = 0; i < (len/sizeof(chan_data_entry)); i++) {
			uint32 *p = (uint32*)p_data;
			chan_data_entry = ltoh32_ua(p + i);
			DHD_RTT(("\t%u\n", chan_data_entry));
		}
		GCC_DIAGNOSTIC_POP();
		break;
	case WL_PROXD_TLV_ID_MF_STATS_DATA:
		DHD_RTT(("WL_PROXD_TLV_ID_MF_STATS_DATA\n"));
		DHD_RTT(("\tmf stats len=%u\n", len));
		rtt_prhex("", p_data, len);
		break;
	default:
		DHD_RTT_ERR(("> Unsupported TLV ID %d\n", tlvid));
		ret = BCME_ERROR;
		break;
	}

	return ret;
}

#ifdef WL_CFG80211
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
		type, sizeof(uint32), (uint8 *)&flags_mask, BCM_XTLV_OPTION_ALIGN32);
	if (ret != BCME_OK) {
		DHD_RTT_ERR(("%s : bcm_pack_xltv_entry() for mask flags failed, status=%d\n",
			__FUNCTION__, ret));
		goto exit;
	}

	type = (session_id == WL_PROXD_SESSION_ID_GLOBAL)?
		WL_PROXD_TLV_ID_FLAGS : WL_PROXD_TLV_ID_SESSION_FLAGS;
	/* setup flags TLV */
	ret = bcm_pack_xtlv_entry((uint8 **)p_tlv, p_buf_space_left,
			type, sizeof(uint32), (uint8 *)&flags, BCM_XTLV_OPTION_ALIGN32);
		if (ret != BCME_OK) {
#ifdef RTT_DEBUG
			DHD_RTT(("%s: bcm_pack_xltv_entry() for flags failed, status=%d\n",
				__FUNCTION__, ret));
#endif // endif
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
			case WL_PROXD_TLV_ID_CUR_ETHER_ADDR:
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
				DHD_RTT_ERR(("%s bad TLV ID : %d\n",
					__FUNCTION__, p_config_param_info->tlvid));
				break;
			}

			ret = bcm_pack_xtlv_entry((uint8 **) p_tlv, p_buf_space_left,
				p_config_param_info->tlvid, src_data_size, (uint8 *)p_src_data,
				BCM_XTLV_OPTION_ALIGN32);
			if (ret != BCME_OK) {
				DHD_RTT_ERR(("%s: bcm_pack_xltv_entry() failed,"
					" status=%d\n", __FUNCTION__, ret));
				break;
			}

		}
	}
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
#ifdef WL_NAN
int
dhd_rtt_delete_nan_session(dhd_pub_t *dhd)
{
	struct net_device *dev = dhd_linux_get_primary_netdev(dhd);
	struct wireless_dev *wdev = ndev_to_wdev(dev);
	struct wiphy *wiphy = wdev->wiphy;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	wl_cfgnan_terminate_directed_rtt_sessions(dev, cfg);
	return BCME_OK;
}
#endif /* WL_NAN */
/* API to find out if the given Peer Mac from FTM events
* is nan-peer. Based on this we will handle the SESSION_END
* event. For nan-peer FTM_SESSION_END event is ignored and handled in
* nan-ranging-cancel or nan-ranging-end event.
*/
static bool
dhd_rtt_is_nan_peer(dhd_pub_t *dhd, struct ether_addr *peer_mac)
{
#ifdef WL_NAN
	struct net_device *dev = dhd_linux_get_primary_netdev(dhd);
	struct wireless_dev *wdev = ndev_to_wdev(dev);
	struct wiphy *wiphy = wdev->wiphy;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	nan_ranging_inst_t *ranging_inst = NULL;
	bool ret = FALSE;

	if (cfg->nan_enable == FALSE || ETHER_ISNULLADDR(peer_mac)) {
		goto exit;
	}

	ranging_inst = wl_cfgnan_check_for_ranging(cfg, peer_mac);
	if (ranging_inst) {
		DHD_RTT((" RTT peer is of type NAN\n"));
		ret = TRUE;
		goto exit;
	}
exit:
	return ret;
#else
	return FALSE;
#endif /* WL_NAN */
}

#ifdef WL_NAN
static int
dhd_rtt_nan_start_session(dhd_pub_t *dhd, rtt_target_info_t *rtt_target)
{
	s32 err = BCME_OK;
	struct net_device *dev = dhd_linux_get_primary_netdev(dhd);
	struct wireless_dev *wdev = ndev_to_wdev(dev);
	struct wiphy *wiphy = wdev->wiphy;
	struct bcm_cfg80211 *cfg = wiphy_priv(wiphy);
	wl_nan_ev_rng_rpt_ind_t range_res;
	nan_ranging_inst_t *ranging_inst = NULL;
	rtt_status_info_t *rtt_status = GET_RTTSTATE(dhd);

	NAN_MUTEX_LOCK();

	bzero(&range_res, sizeof(range_res));

	if (!rtt_status) {
		err = BCME_NOTENABLED;
		goto done;
	}

	if (!cfg->nan_enable) { /* If nan is not enabled report error */
		err = BCME_NOTENABLED;
		goto done;
	}

	/* check if new ranging session allowed */
	if (!wl_cfgnan_ranging_allowed(cfg)) {
		/* responder should be in progress because initiator requests are
		* queued in DHD. Since initiator has more proef cancel responder
		* sessions
		*/
		wl_cfgnan_cancel_rng_responders(dev, cfg);
	}

	ranging_inst = wl_cfgnan_get_ranging_inst(cfg,
			&rtt_target->addr, NAN_RANGING_ROLE_INITIATOR);
	if (!ranging_inst) {
		err = BCME_NORESOURCE;
		goto done;
	}

	DHD_RTT(("Trigger nan based range request\n"));
	err = wl_cfgnan_trigger_ranging(bcmcfg_to_prmry_ndev(cfg),
			cfg, ranging_inst, NULL, NAN_RANGE_REQ_CMD, TRUE);
	if (unlikely(err)) {
		goto done;
	}
	ranging_inst->range_type = RTT_TYPE_NAN_DIRECTED;
	ranging_inst->range_role = NAN_RANGING_ROLE_INITIATOR;
	/* schedule proxd timeout */
	schedule_delayed_work(&rtt_status->proxd_timeout,
		msecs_to_jiffies(DHD_NAN_RTT_TIMER_INTERVAL_MS));
done:
	if (err) { /* notify failure RTT event to host */
		DHD_RTT_ERR(("Failed to issue Nan Ranging Request err %d\n", err));
		dhd_rtt_handle_nan_rtt_session_end(dhd, &rtt_target->addr);
		/* try to reset geofence */
		if (ranging_inst) {
			wl_cfgnan_reset_geofence_ranging(cfg, ranging_inst,
				RTT_SCHED_DIR_TRIGGER_FAIL);
		}
	}
	NAN_MUTEX_UNLOCK();
	return err;
}
#endif /* WL_NAN */

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
		DHD_RTT_ERR(("%s : failed to allocate the iovar (size :%d)\n",
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
			DHD_RTT_ERR(("%s : failed to set config\n", __FUNCTION__));
		}
	}
	/* clean up */
	kfree(p_proxd_iov);
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
#endif /* WL_CFG80211 */

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
		DHD_RTT_ERR(("doesn't support this bandwith : %d", channel.width));
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
	rtt_status_info_t *rtt_status = NULL;
	struct net_device *dev = NULL;

	NULL_CHECK(params, "params is NULL", err);
	NULL_CHECK(dhd, "dhd is NULL", err);

	dev = dhd_linux_get_primary_netdev(dhd);
	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", err);
	NULL_CHECK(dev, "dev is NULL", err);

	if (!HAS_11MC_CAP(rtt_status->rtt_capa.proto)) {
		DHD_RTT_ERR(("doesn't support RTT \n"));
		err = BCME_ERROR;
		goto exit;
	}

	DHD_RTT(("%s enter\n", __FUNCTION__));

	if (params->rtt_target_cnt > 0) {
#ifdef WL_NAN
		/* cancel ongoing geofence RTT if there */
		if ((err = wl_cfgnan_suspend_geofence_rng_session(dev,
			NULL, RTT_GEO_SUSPN_HOST_DIR_RTT_TRIG, 0)) != BCME_OK) {
			goto exit;
		}
#endif /* WL_NAN */
	} else {
		err = BCME_BADARG;
		goto exit;
	}

	mutex_lock(&rtt_status->rtt_mutex);
	if (rtt_status->status != RTT_STOPPED) {
		DHD_RTT_ERR(("rtt is already started\n"));
		err = BCME_BUSY;
		goto exit;
	}
	memset(rtt_status->rtt_config.target_info, 0, TARGET_INFO_SIZE(RTT_MAX_TARGET_CNT));
	rtt_status->rtt_config.rtt_target_cnt = params->rtt_target_cnt;
	memcpy(rtt_status->rtt_config.target_info,
		params->target_info, TARGET_INFO_SIZE(params->rtt_target_cnt));
	rtt_status->status = RTT_STARTED;
	DHD_RTT_MEM(("dhd_rtt_set_cfg: RTT Started, target_cnt = %d\n", params->rtt_target_cnt));
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
		rtt_status->rtt_sched_reason = RTT_SCHED_HOST_TRIGGER;
		schedule_work(&rtt_status->work);
	}
exit:
	mutex_unlock(&rtt_status->rtt_mutex);
	return err;
}

#define GEOFENCE_RTT_LOCK(rtt_status) mutex_lock(&(rtt_status)->geofence_mutex)
#define GEOFENCE_RTT_UNLOCK(rtt_status) mutex_unlock(&(rtt_status)->geofence_mutex)

#ifdef WL_NAN
/* sets geofence role concurrency state TRUE/FALSE */
void
dhd_rtt_set_role_concurrency_state(dhd_pub_t *dhd, bool state)
{
	rtt_status_info_t *rtt_status = GET_RTTSTATE(dhd);
	if (!rtt_status) {
		return;
	}
	GEOFENCE_RTT_LOCK(rtt_status);
	rtt_status->geofence_cfg.role_concurr_state = state;
	GEOFENCE_RTT_UNLOCK(rtt_status);
}

/* returns TRUE if geofence role concurrency constraint exists */
bool
dhd_rtt_get_role_concurrency_state(dhd_pub_t *dhd)
{
	rtt_status_info_t *rtt_status = GET_RTTSTATE(dhd);
	if (!rtt_status) {
		return FALSE;
	}
	return rtt_status->geofence_cfg.role_concurr_state;
}

int8
dhd_rtt_get_geofence_target_cnt(dhd_pub_t *dhd)
{
	rtt_status_info_t *rtt_status = GET_RTTSTATE(dhd);
	if (!rtt_status) {
		return 0;
	}
	return rtt_status->geofence_cfg.geofence_target_cnt;
}

/* sets geofence rtt state TRUE/FALSE */
void
dhd_rtt_set_geofence_rtt_state(dhd_pub_t *dhd, bool state)
{
	rtt_status_info_t *rtt_status = GET_RTTSTATE(dhd);
	if (!rtt_status) {
		return;
	}
	GEOFENCE_RTT_LOCK(rtt_status);
	rtt_status->geofence_cfg.rtt_in_progress = state;
	GEOFENCE_RTT_UNLOCK(rtt_status);
}

/* returns TRUE if geofence rtt is in progress */
bool
dhd_rtt_get_geofence_rtt_state(dhd_pub_t *dhd)
{
	rtt_status_info_t *rtt_status = GET_RTTSTATE(dhd);

	if (!rtt_status) {
		return FALSE;
	}

	return rtt_status->geofence_cfg.rtt_in_progress;
}

/* returns geofence RTT target list Head */
rtt_geofence_target_info_t*
dhd_rtt_get_geofence_target_head(dhd_pub_t *dhd)
{
	rtt_status_info_t *rtt_status = GET_RTTSTATE(dhd);
	rtt_geofence_target_info_t* head = NULL;

	if (!rtt_status) {
		return NULL;
	}

	if (rtt_status->geofence_cfg.geofence_target_cnt) {
		head = &rtt_status->geofence_cfg.geofence_target_info[0];
	}

	return head;
}

int8
dhd_rtt_get_geofence_cur_target_idx(dhd_pub_t *dhd)
{
	int8 target_cnt = 0, cur_idx = DHD_RTT_INVALID_TARGET_INDEX;
	rtt_status_info_t *rtt_status = GET_RTTSTATE(dhd);

	if (!rtt_status) {
		goto exit;
	}

	target_cnt = rtt_status->geofence_cfg.geofence_target_cnt;
	if (target_cnt == 0) {
		goto exit;
	}

	cur_idx = rtt_status->geofence_cfg.cur_target_idx;
	ASSERT(cur_idx <= target_cnt);

exit:
	return cur_idx;
}

void
dhd_rtt_move_geofence_cur_target_idx_to_next(dhd_pub_t *dhd)
{
	rtt_status_info_t *rtt_status = GET_RTTSTATE(dhd);

	if (!rtt_status) {
		return;
	}

	if (rtt_status->geofence_cfg.geofence_target_cnt == 0) {
		/* Invalidate current idx if no targets */
		rtt_status->geofence_cfg.cur_target_idx =
			DHD_RTT_INVALID_TARGET_INDEX;
		/* Cancel pending retry timer if any */
		if (delayed_work_pending(&rtt_status->rtt_retry_timer)) {
			cancel_delayed_work(&rtt_status->rtt_retry_timer);
		}
		return;
	}
	rtt_status->geofence_cfg.cur_target_idx++;

	if (rtt_status->geofence_cfg.cur_target_idx >=
		rtt_status->geofence_cfg.geofence_target_cnt) {
		/* Reset once all targets done */
		rtt_status->geofence_cfg.cur_target_idx = 0;
	}
}

/* returns geofence current RTT target */
rtt_geofence_target_info_t*
dhd_rtt_get_geofence_current_target(dhd_pub_t *dhd)
{
	rtt_status_info_t *rtt_status = GET_RTTSTATE(dhd);
	rtt_geofence_target_info_t* cur_target = NULL;
	int cur_idx = 0;

	if (!rtt_status) {
		return NULL;
	}

	cur_idx = dhd_rtt_get_geofence_cur_target_idx(dhd);
	if (cur_idx >= 0) {
		cur_target = &rtt_status->geofence_cfg.geofence_target_info[cur_idx];
	}

	return cur_target;
}

/* returns geofence target from list for the peer */
rtt_geofence_target_info_t*
dhd_rtt_get_geofence_target(dhd_pub_t *dhd, struct ether_addr* peer_addr, int8 *index)
{
	int8 i;
	rtt_status_info_t *rtt_status;
	int target_cnt;
	rtt_geofence_target_info_t *geofence_target_info, *tgt = NULL;

	rtt_status = GET_RTTSTATE(dhd);

	if (!rtt_status) {
		return NULL;
	}

	target_cnt = rtt_status->geofence_cfg.geofence_target_cnt;
	geofence_target_info = rtt_status->geofence_cfg.geofence_target_info;

	/* Loop through to find target */
	for (i = 0; i < target_cnt; i++) {
		if (geofence_target_info[i].valid == FALSE) {
			break;
		}
		if (!memcmp(peer_addr, &geofence_target_info[i].peer_addr,
				ETHER_ADDR_LEN)) {
			*index = i;
			tgt = &geofence_target_info[i];
		}
	}
	if (!tgt) {
		DHD_RTT(("dhd_rtt_get_geofence_target: Target not found in list,"
			" MAC ADDR: "MACDBG" \n", MAC2STRDBG(peer_addr)));
	}
	return tgt;
}

/* add geofence target to the target list */
int
dhd_rtt_add_geofence_target(dhd_pub_t *dhd, rtt_geofence_target_info_t *target)
{
	int err = BCME_OK;
	rtt_status_info_t *rtt_status;
	rtt_geofence_target_info_t  *geofence_target_info;
	int8 geofence_target_cnt, index;

	NULL_CHECK(dhd, "dhd is NULL", err);
	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", err);

	GEOFENCE_RTT_LOCK(rtt_status);

	/* Get the geofence_target via peer addr, index param is dumm here */
	geofence_target_info = dhd_rtt_get_geofence_target(dhd, &target->peer_addr, &index);
	if (geofence_target_info) {
		DHD_RTT(("Duplicate geofencing RTT add request dropped\n"));
		err = BCME_OK;
		goto exit;
	}

	geofence_target_cnt = rtt_status->geofence_cfg.geofence_target_cnt;
	if (geofence_target_cnt >= RTT_MAX_GEOFENCE_TARGET_CNT) {
		DHD_RTT(("Queue full, Geofencing RTT add request dropped\n"));
		err = BCME_NORESOURCE;
		goto exit;
	}

	/* Add Geofence RTT request and increment target count */
	geofence_target_info = rtt_status->geofence_cfg.geofence_target_info;
	/* src and dest buffer len same, pointers of same DS statically allocated */
	(void)memcpy_s(&geofence_target_info[geofence_target_cnt],
		sizeof(geofence_target_info[geofence_target_cnt]), target,
		sizeof(*target));
	geofence_target_info[geofence_target_cnt].valid = TRUE;
	rtt_status->geofence_cfg.geofence_target_cnt++;
	if (rtt_status->geofence_cfg.geofence_target_cnt == 1) {
		/* Adding first target */
		rtt_status->geofence_cfg.cur_target_idx = 0;
	}

exit:
	GEOFENCE_RTT_UNLOCK(rtt_status);
	return err;
}

/* removes geofence target from the target list */
int
dhd_rtt_remove_geofence_target(dhd_pub_t *dhd, struct ether_addr *peer_addr)
{
	int err = BCME_OK;
	rtt_status_info_t *rtt_status;
	rtt_geofence_target_info_t  *geofence_target_info;
	int8 geofence_target_cnt, j, index = 0;

	NULL_CHECK(dhd, "dhd is NULL", err);
	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", err);

	GEOFENCE_RTT_LOCK(rtt_status);

	geofence_target_cnt = dhd_rtt_get_geofence_target_cnt(dhd);
	if (geofence_target_cnt == 0) {
		DHD_RTT(("Queue Empty, Geofencing RTT remove request dropped\n"));
		ASSERT(0);
		goto exit;
	}

	/* Get the geofence_target via peer addr */
	geofence_target_info = dhd_rtt_get_geofence_target(dhd, peer_addr, &index);
	if (geofence_target_info == NULL) {
		DHD_RTT(("Geofencing RTT target not found, remove request dropped\n"));
		err = BCME_NOTFOUND;
		goto exit;
	}

	/* left shift all the valid entries, as we dont keep holes in list */
	for (j = index; (j+1) < geofence_target_cnt; j++) {
		if (geofence_target_info[j].valid == TRUE) {
			/*
			 * src and dest buffer len same, pointers of same DS
			 * statically allocated
			 */
			(void)memcpy_s(&geofence_target_info[j], sizeof(geofence_target_info[j]),
				&geofence_target_info[j + 1],
				sizeof(geofence_target_info[j + 1]));
		} else {
			break;
		}
	}
	rtt_status->geofence_cfg.geofence_target_cnt--;
	if ((rtt_status->geofence_cfg.geofence_target_cnt == 0) ||
		(index == rtt_status->geofence_cfg.cur_target_idx)) {
		/* Move cur_idx to next target */
		dhd_rtt_move_geofence_cur_target_idx_to_next(dhd);
	} else if (index < rtt_status->geofence_cfg.cur_target_idx) {
		/* Decrement cur index if cur target position changed */
		rtt_status->geofence_cfg.cur_target_idx--;
	}

exit:
	GEOFENCE_RTT_UNLOCK(rtt_status);
	return err;
}

/* deletes/empty geofence target list */
int
dhd_rtt_delete_geofence_target_list(dhd_pub_t *dhd)
{
	rtt_status_info_t *rtt_status;

	int err = BCME_OK;

	NULL_CHECK(dhd, "dhd is NULL", err);
	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", err);
	GEOFENCE_RTT_LOCK(rtt_status);
	memset_s(&rtt_status->geofence_cfg, sizeof(rtt_geofence_cfg_t),
		0, sizeof(rtt_geofence_cfg_t));
	GEOFENCE_RTT_UNLOCK(rtt_status);
	return err;
}

int
dhd_rtt_sched_geofencing_target(dhd_pub_t *dhd)
{
	rtt_geofence_target_info_t  *geofence_target_info;
	struct net_device *dev = dhd_linux_get_primary_netdev(dhd);
	int ret = BCME_OK;
	bool geofence_state;
	bool role_concurrency_state;
	u8 rtt_invalid_reason = RTT_STATE_VALID;
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	rtt_status_info_t *rtt_status = GET_RTTSTATE(dhd);

	NAN_MUTEX_LOCK();

	if ((cfg->nan_init_state == FALSE) ||
		(cfg->nan_enable == FALSE)) {
		ret = BCME_NOTENABLED;
		goto done;
	}
	geofence_state = dhd_rtt_get_geofence_rtt_state(dhd);
	role_concurrency_state = dhd_rtt_get_role_concurrency_state(dhd);

	DHD_RTT_ERR(("dhd_rtt_sched_geofencing_target: sched_reason = %d\n",
		rtt_status->rtt_sched_reason));

	if (geofence_state == TRUE || role_concurrency_state == TRUE) {
		ret = BCME_ERROR;
		DHD_RTT_ERR(("geofencing constraint , sched request dropped,"
			" geofence_state = %d, role_concurrency_state = %d\n",
			geofence_state, role_concurrency_state));
		goto done;
	}

	/* Get current geofencing target */
	geofence_target_info = dhd_rtt_get_geofence_current_target(dhd);

	/* call cfg API for trigerring geofencing RTT */
	if (geofence_target_info) {
		/* check for dp/others concurrency */
		rtt_invalid_reason = dhd_rtt_invalid_states(dev,
				&geofence_target_info->peer_addr);
		if (rtt_invalid_reason != RTT_STATE_VALID) {
			ret = BCME_BUSY;
			DHD_RTT_ERR(("DRV State is not valid for RTT, "
				"invalid_state = %d\n", rtt_invalid_reason));
			goto done;
		}

		ret = wl_cfgnan_trigger_geofencing_ranging(dev,
				&geofence_target_info->peer_addr);
		if (ret == BCME_OK) {
			dhd_rtt_set_geofence_rtt_state(dhd, TRUE);
		}
	} else {
		DHD_RTT(("No RTT target to schedule\n"));
		ret = BCME_NOTFOUND;
	}

done:
	NAN_MUTEX_UNLOCK();
	return ret;
}
#endif /* WL_NAN */

#ifdef WL_CFG80211
#ifdef WL_NAN
static void
dhd_rtt_retry(dhd_pub_t *dhd)
{
	struct net_device *dev = dhd_linux_get_primary_netdev(dhd);
	struct bcm_cfg80211 *cfg = wl_get_cfg(dev);
	rtt_geofence_target_info_t  *geofence_target = NULL;
	nan_ranging_inst_t *ranging_inst = NULL;

	geofence_target = dhd_rtt_get_geofence_current_target(dhd);
	if (!geofence_target) {
		DHD_RTT(("dhd_rtt_retry: geofence target null\n"));
		goto exit;
	}
	ranging_inst = wl_cfgnan_get_ranging_inst(cfg,
		&geofence_target->peer_addr, NAN_RANGING_ROLE_INITIATOR);
	if (!ranging_inst) {
		DHD_RTT(("dhd_rtt_retry: ranging instance null\n"));
		goto exit;
	}
	wl_cfgnan_reset_geofence_ranging(cfg,
		ranging_inst, RTT_SCHED_RTT_RETRY_GEOFENCE);

exit:
	return;
}

static void
dhd_rtt_retry_work(struct work_struct *work)
{
	rtt_status_info_t *rtt_status = NULL;
	dhd_pub_t *dhd = NULL;
	struct net_device *dev = NULL;
	struct bcm_cfg80211 *cfg = NULL;

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif // endif
	rtt_status = container_of(work, rtt_status_info_t, proxd_timeout.work);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif // endif

	dhd = rtt_status->dhd;
	if (dhd == NULL) {
		DHD_RTT_ERR(("%s : dhd is NULL\n", __FUNCTION__));
		goto exit;
	}
	dev = dhd_linux_get_primary_netdev(dhd);
	cfg = wl_get_cfg(dev);

	NAN_MUTEX_LOCK();
	mutex_lock(&rtt_status->rtt_mutex);
	(void) dhd_rtt_retry(dhd);
	mutex_unlock(&rtt_status->rtt_mutex);
	NAN_MUTEX_UNLOCK();

exit:
	return;
}
#endif /* WL_NAN */

/*
 * Return zero (0)
 * for valid RTT state
 * means if RTT is applicable
 */
uint8
dhd_rtt_invalid_states(struct net_device *ndev, struct ether_addr *peer_addr)
{
	uint8 invalid_reason = RTT_STATE_VALID;
	struct bcm_cfg80211 *cfg = wl_get_cfg(ndev);

	UNUSED_PARAMETER(cfg);
	UNUSED_PARAMETER(invalid_reason);

	/* Make sure peer addr is not NULL in caller */
	ASSERT(peer_addr);
	/*
	 * Keep adding prohibited drv states here
	 * Only generic conditions which block
	 * All RTTs like NDP connection
	 */

#ifdef WL_NAN
	if (wl_cfgnan_data_dp_exists_with_peer(cfg, peer_addr)) {
		invalid_reason = RTT_STATE_INV_REASON_NDP_EXIST;
		DHD_RTT(("NDP in progress/connected, RTT prohibited\n"));
		goto exit;
	}
#endif /* WL_NAN */

	/* Remove below #defines once more exit calls come */
#ifdef WL_NAN
exit:
#endif /* WL_NAN */
	return invalid_reason;
}
#endif /* WL_CFG80211 */

void
dhd_rtt_schedule_rtt_work_thread(dhd_pub_t *dhd, int sched_reason)
{
	rtt_status_info_t *rtt_status = GET_RTTSTATE(dhd);
	if (rtt_status == NULL) {
		ASSERT(0);
	} else {
		rtt_status->rtt_sched_reason = sched_reason;
		schedule_work(&rtt_status->work);
	}
	return;
}

int
dhd_rtt_stop(dhd_pub_t *dhd, struct ether_addr *mac_list, int mac_cnt)
{
	int err = BCME_OK;
#ifdef WL_CFG80211
	int i = 0, j = 0;
	rtt_status_info_t *rtt_status;
	rtt_results_header_t *entry, *next;
	rtt_result_t *rtt_result, *next2;
	struct rtt_noti_callback *iter;

	NULL_CHECK(dhd, "dhd is NULL", err);
	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", err);
	if (rtt_status->status == RTT_STOPPED) {
		DHD_RTT_ERR(("rtt is not started\n"));
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
			GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
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
			GCC_DIAGNOSTIC_POP();
		}
		/* send the rtt complete event to wake up the user process */
		GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
		list_for_each_entry(iter, &rtt_status->noti_fn_list, list) {
			GCC_DIAGNOSTIC_POP();
			iter->noti_fn(iter->ctx, &rtt_status->rtt_results_cache);
		}
		/* reinitialize the HEAD */
		INIT_LIST_HEAD(&rtt_status->rtt_results_cache);
		/* clear information for rtt_config */
		rtt_status->rtt_config.rtt_target_cnt = 0;
		memset(rtt_status->rtt_config.target_info, 0,
			TARGET_INFO_SIZE(RTT_MAX_TARGET_CNT));
		rtt_status->cur_idx = 0;
		/* Cancel pending proxd timeout work if any */
		if (delayed_work_pending(&rtt_status->proxd_timeout)) {
			cancel_delayed_work(&rtt_status->proxd_timeout);
		}
		dhd_rtt_delete_session(dhd, FTM_DEFAULT_SESSION);
#ifdef WL_NAN
		dhd_rtt_delete_nan_session(dhd);
#endif /* WL_NAN */
		dhd_rtt_ftm_enable(dhd, FALSE);
	}
	mutex_unlock(&rtt_status->rtt_mutex);
#endif /* WL_CFG80211 */
	return err;
}

#ifdef WL_CFG80211
static void
dhd_rtt_timeout(dhd_pub_t *dhd)
{
	rtt_status_info_t *rtt_status;
#ifndef DHD_DUMP_ON_RTT_TIMEOUT
	rtt_target_info_t *rtt_target = NULL;
	rtt_target_info_t *rtt_target_info = NULL;
#ifdef WL_NAN
	nan_ranging_inst_t *ranging_inst = NULL;
	int ret = BCME_OK;
	uint32 status;
	struct net_device *ndev = dhd_linux_get_primary_netdev(dhd);
	struct bcm_cfg80211 *cfg =  wiphy_priv(ndev->ieee80211_ptr->wiphy);
#endif /* WL_NAN */
#endif /* !DHD_DUMP_ON_RTT_TIMEOUT */

	rtt_status = GET_RTTSTATE(dhd);
	if (!rtt_status) {
		DHD_RTT_ERR(("Proxd timer expired but no RTT status\n"));
		goto exit;
	}

	if (RTT_IS_STOPPED(rtt_status)) {
		DHD_RTT_ERR(("Proxd timer expired but no RTT Request\n"));
		goto exit;
	}

#ifdef DHD_DUMP_ON_RTT_TIMEOUT
	/* Dump, and Panic depending on memdump.info */
	if (dhd_query_bus_erros(dhd)) {
		goto exit;
	}
#ifdef DHD_FW_COREDUMP
	if (dhd->memdump_enabled) {
		/* Behave based on user memdump info */
		dhd->memdump_type = DUMP_TYPE_PROXD_TIMEOUT;
		dhd_bus_mem_dump(dhd);
	}
#endif /* DHD_FW_COREDUMP */
#else /* DHD_DUMP_ON_RTT_TIMEOUT */
	/* Cancel RTT for target and proceed to next target */
	rtt_target_info = rtt_status->rtt_config.target_info;
	if ((!rtt_target_info) ||
		(rtt_status->cur_idx >= rtt_status->rtt_config.rtt_target_cnt)) {
		goto exit;
	}
	rtt_target = &rtt_target_info[rtt_status->cur_idx];
	WL_ERR(("Proxd timer expired for Target: "MACDBG" \n", MAC2STRDBG(&rtt_target->addr)));
#ifdef WL_NAN
	if (rtt_target->peer == RTT_PEER_NAN) {
		ranging_inst = wl_cfgnan_check_for_ranging(cfg, &rtt_target->addr);
		if (!ranging_inst) {
			goto exit;
		}
		ret =  wl_cfgnan_cancel_ranging(ndev, cfg, ranging_inst->range_id,
				NAN_RNG_TERM_FLAG_IMMEDIATE, &status);
		if (unlikely(ret) || unlikely(status)) {
			WL_ERR(("%s:nan range cancel failed ret = %d status = %d\n",
				__FUNCTION__, ret, status));
		}
	} else
#endif /* WL_NAN */
	{
		/* For Legacy RTT */
		dhd_rtt_delete_session(dhd, FTM_DEFAULT_SESSION);
	}
	dhd_rtt_create_failure_result(rtt_status, &rtt_target->addr);
	dhd_rtt_handle_rtt_session_end(dhd);
#endif /* DHD_DUMP_ON_RTT_TIMEOUT */
exit:
	return;
}

static void
dhd_rtt_timeout_work(struct work_struct *work)
{
	rtt_status_info_t *rtt_status = NULL;
	dhd_pub_t *dhd = NULL;

#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif // endif
	rtt_status = container_of(work, rtt_status_info_t, proxd_timeout.work);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif // endif

	dhd = rtt_status->dhd;
	if (dhd == NULL) {
		DHD_RTT_ERR(("%s : dhd is NULL\n", __FUNCTION__));
		return;
	}
	mutex_lock(&rtt_status->rtt_mutex);
	(void) dhd_rtt_timeout(dhd);
	mutex_unlock(&rtt_status->rtt_mutex);
}

static int
dhd_rtt_start(dhd_pub_t *dhd)
{
	int err = BCME_OK;
	int err_at = 0;
	char eabuf[ETHER_ADDR_STR_LEN];
	char chanbuf[CHANSPEC_STR_LEN];
	int pm = PM_OFF;
	int ftm_cfg_cnt = 0;
	int ftm_param_cnt = 0;
	uint32 rspec = 0;
	ftm_config_options_info_t ftm_configs[FTM_MAX_CONFIGS];
	ftm_config_param_info_t ftm_params[FTM_MAX_PARAMS];
	rtt_target_info_t *rtt_target;
	rtt_status_info_t *rtt_status;
	struct net_device *dev = dhd_linux_get_primary_netdev(dhd);
	u8 ioctl_buf[WLC_IOCTL_SMLEN];
	u8 rtt_invalid_reason = RTT_STATE_VALID;
	int rtt_sched_type = RTT_TYPE_INVALID;

	NULL_CHECK(dhd, "dhd is NULL", err);

	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", err);

	DHD_RTT(("Enter %s\n", __FUNCTION__));

	if (RTT_IS_STOPPED(rtt_status)) {
		DHD_RTT(("No Directed RTT target to process, check for geofence\n"));
		goto geofence;
	}

	if (rtt_status->cur_idx >= rtt_status->rtt_config.rtt_target_cnt) {
		err = BCME_RANGE;
		err_at = 1;
		DHD_RTT(("%s : idx %d is out of range\n", __FUNCTION__, rtt_status->cur_idx));
		if (rtt_status->flags == WL_PROXD_SESSION_FLAG_TARGET) {
			DHD_RTT_ERR(("STA is set as Target/Responder \n"));
			err = BCME_ERROR;
			err_at = 1;
		}
		goto exit;
	}

	rtt_status->pm = PM_OFF;
	err = wldev_ioctl_get(dev, WLC_GET_PM, &rtt_status->pm, sizeof(rtt_status->pm));
	if (err) {
		DHD_RTT_ERR(("Failed to get the PM value\n"));
	} else {
		err = wldev_ioctl_set(dev, WLC_SET_PM, &pm, sizeof(pm));
		if (err) {
			DHD_RTT_ERR(("Failed to set the PM\n"));
			rtt_status->pm_restore = FALSE;
		} else {
			rtt_status->pm_restore = TRUE;
		}
	}

	mutex_lock(&rtt_status->rtt_mutex);
	/* Get a target information */
	rtt_target = &rtt_status->rtt_config.target_info[rtt_status->cur_idx];
	mutex_unlock(&rtt_status->rtt_mutex);
	DHD_RTT(("%s enter\n", __FUNCTION__));

	if (ETHER_ISNULLADDR(rtt_target->addr.octet)) {
		err = BCME_BADADDR;
		err_at = 2;
		DHD_RTT(("RTT Target addr is NULL\n"));
		goto exit;
	}

	/* check for dp/others concurrency */
	rtt_invalid_reason = dhd_rtt_invalid_states(dev, &rtt_target->addr);
	if (rtt_invalid_reason != RTT_STATE_VALID) {
		err = BCME_BUSY;
		err_at = 3;
		DHD_RTT(("DRV State is not valid for RTT\n"));
		goto exit;
	}

#ifdef WL_NAN
	if (rtt_target->peer == RTT_PEER_NAN) {
		rtt_sched_type = RTT_TYPE_NAN_DIRECTED;
		rtt_status->status = RTT_ENABLED;
		/* Ignore return value..failure taken care inside the API */
		dhd_rtt_nan_start_session(dhd, rtt_target);
		goto exit;
	}
#endif /* WL_NAN */
	if (!RTT_IS_ENABLED(rtt_status)) {
		/* enable ftm */
		err = dhd_rtt_ftm_enable(dhd, TRUE);
		if (err) {
			DHD_RTT_ERR(("failed to enable FTM (%d)\n", err));
			err_at = 5;
			goto exit;
		}
	}

	/* delete session of index default sesession  */
	err = dhd_rtt_delete_session(dhd, FTM_DEFAULT_SESSION);
	if (err < 0 && err != BCME_NOTFOUND) {
		DHD_RTT_ERR(("failed to delete session of FTM (%d)\n", err));
		err_at = 6;
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

	memset(ioctl_buf, 0, WLC_IOCTL_SMLEN);

	/* Rand Mac for newer version in place of cur_eth */
	if (dhd->wlc_ver_major < RTT_IOV_CUR_ETH_OBSOLETE) {
		err = wldev_iovar_getbuf(dev, "cur_etheraddr", NULL, 0,
				ioctl_buf, WLC_IOCTL_SMLEN, NULL);
		if (err) {
			DHD_RTT_ERR(("WLC_GET_CUR_ETHERADDR failed, error %d\n", err));
			err_at = 7;
			goto exit;
		}
		memcpy(rtt_target->local_addr.octet, ioctl_buf, ETHER_ADDR_LEN);

		/* local mac address */
		if (!ETHER_ISNULLADDR(rtt_target->local_addr.octet)) {
			ftm_params[ftm_param_cnt].mac_addr = rtt_target->local_addr;
			ftm_params[ftm_param_cnt++].tlvid = WL_PROXD_TLV_ID_CUR_ETHER_ADDR;
			bcm_ether_ntoa(&rtt_target->local_addr, eabuf);
			DHD_RTT((">\t local %s\n", eabuf));
		}
	}
	/* target's mac address */
	if (!ETHER_ISNULLADDR(rtt_target->addr.octet)) {
		ftm_params[ftm_param_cnt].mac_addr = rtt_target->addr;
		ftm_params[ftm_param_cnt++].tlvid = WL_PROXD_TLV_ID_PEER_MAC;
		bcm_ether_ntoa(&rtt_target->addr, eabuf);
		DHD_RTT((">\t target %s\n", eabuf));
	}
	/* target's chanspec */
	if (rtt_target->chanspec) {
		ftm_params[ftm_param_cnt].chanspec = htol32((uint32)rtt_target->chanspec);
		ftm_params[ftm_param_cnt++].tlvid = WL_PROXD_TLV_ID_CHANSPEC;
		wf_chspec_ntoa(rtt_target->chanspec, chanbuf);
		DHD_RTT((">\t chanspec : %s\n", chanbuf));
	}
	/* num-burst */
	if (rtt_target->num_burst) {
		ftm_params[ftm_param_cnt].data16 = htol16(rtt_target->num_burst);
		ftm_params[ftm_param_cnt++].tlvid = WL_PROXD_TLV_ID_NUM_BURST;
		DHD_RTT((">\t num of burst : %d\n", rtt_target->num_burst));
	}
	/* number of frame per burst */
	rtt_target->num_frames_per_burst = FTM_DEFAULT_CNT_80M;
	if (CHSPEC_IS80(rtt_target->chanspec)) {
		rtt_target->num_frames_per_burst = FTM_DEFAULT_CNT_80M;
	} else if (CHSPEC_IS40(rtt_target->chanspec)) {
		rtt_target->num_frames_per_burst = FTM_DEFAULT_CNT_40M;
	} else if (CHSPEC_IS20(rtt_target->chanspec)) {
		rtt_target->num_frames_per_burst = FTM_DEFAULT_CNT_20M;
	}
	ftm_params[ftm_param_cnt].data16 = htol16(rtt_target->num_frames_per_burst);
	ftm_params[ftm_param_cnt++].tlvid = WL_PROXD_TLV_ID_BURST_NUM_FTM;
	DHD_RTT((">\t number of frame per burst : %d\n", rtt_target->num_frames_per_burst));

	/* FTM retry count */
	if (rtt_target->num_retries_per_ftm) {
		ftm_params[ftm_param_cnt].data8 = rtt_target->num_retries_per_ftm;
		ftm_params[ftm_param_cnt++].tlvid = WL_PROXD_TLV_ID_FTM_RETRIES;
		DHD_RTT((">\t retry count of FTM  : %d\n", rtt_target->num_retries_per_ftm));
	}
	/* FTM Request retry count */
	if (rtt_target->num_retries_per_ftmr) {
		ftm_params[ftm_param_cnt].data8 = rtt_target->num_retries_per_ftmr;
		ftm_params[ftm_param_cnt++].tlvid = WL_PROXD_TLV_ID_FTM_REQ_RETRIES;
		DHD_RTT((">\t retry count of FTM Req : %d\n", rtt_target->num_retries_per_ftmr));
	}
	/* burst-period */
	if (rtt_target->burst_period) {
		ftm_params[ftm_param_cnt].data_intvl.intvl =
			htol32(rtt_target->burst_period); /* ms */
		ftm_params[ftm_param_cnt].data_intvl.tmu = WL_PROXD_TMU_MILLI_SEC;
		ftm_params[ftm_param_cnt++].tlvid = WL_PROXD_TLV_ID_BURST_PERIOD;
		DHD_RTT((">\t burst period : %d ms\n", rtt_target->burst_period));
	}
	/* Setting both duration and timeout to MAX duration
	 * to handle the congestion environments.
	 * Hence ignoring the user config.
	 */
	/* burst-duration */
	rtt_target->burst_duration = FTM_MAX_BURST_DUR_TMO_MS;
	if (rtt_target->burst_duration) {
		ftm_params[ftm_param_cnt].data_intvl.intvl =
			htol32(rtt_target->burst_duration); /* ms */
		ftm_params[ftm_param_cnt].data_intvl.tmu = WL_PROXD_TMU_MILLI_SEC;
		ftm_params[ftm_param_cnt++].tlvid = WL_PROXD_TLV_ID_BURST_DURATION;
		DHD_RTT((">\t burst duration : %d ms\n",
			rtt_target->burst_duration));
	}
	/* burst-timeout */
	rtt_target->burst_timeout = FTM_MAX_BURST_DUR_TMO_MS;
	if (rtt_target->burst_timeout) {
		ftm_params[ftm_param_cnt].data_intvl.intvl =
			htol32(rtt_target->burst_timeout); /* ms */
		ftm_params[ftm_param_cnt].data_intvl.tmu = WL_PROXD_TMU_MILLI_SEC;
		ftm_params[ftm_param_cnt++].tlvid = WL_PROXD_TLV_ID_BURST_TIMEOUT;
		DHD_RTT((">\t burst timeout : %d ms\n",
			rtt_target->burst_timeout));
	}
	/* event_mask..applicable for only Legacy RTT.
	* For nan-rtt config happens from firmware
	*/
	ftm_params[ftm_param_cnt].event_mask = ((1 << WL_PROXD_EVENT_BURST_END) |
		(1 << WL_PROXD_EVENT_SESSION_END));
	ftm_params[ftm_param_cnt++].tlvid = WL_PROXD_TLV_ID_EVENT_MASK;

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
				DHD_RTT(("doesn't support this preamble : %d\n",
					rtt_target->preamble));
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
				DHD_RTT(("doesn't support this BW : %d\n", rtt_target->bw));
				use_default = TRUE;
				break;
		}
		if (!use_default) {
			ftm_params[ftm_param_cnt].data32 = htol32(rspec);
			ftm_params[ftm_param_cnt++].tlvid = WL_PROXD_TLV_ID_RATESPEC;
			DHD_RTT((">\t ratespec : %d\n", rspec));
		}

	}
	dhd_set_rand_mac_oui(dhd);
	dhd_rtt_ftm_config(dhd, FTM_DEFAULT_SESSION, FTM_CONFIG_CAT_GENERAL,
			ftm_params, ftm_param_cnt);

	rtt_sched_type = RTT_TYPE_LEGACY;
	err = dhd_rtt_start_session(dhd, FTM_DEFAULT_SESSION, TRUE);
	if (err) {
		DHD_RTT_ERR(("failed to start session of FTM : error %d\n", err));
		err_at = 8;
	} else {
		/* schedule proxd timeout */
		schedule_delayed_work(&rtt_status->proxd_timeout,
			msecs_to_jiffies(DHD_NAN_RTT_TIMER_INTERVAL_MS));

	}

	goto exit;
geofence:
#ifdef WL_NAN
	/* sched geofencing rtt */
	rtt_sched_type = RTT_TYPE_NAN_GEOFENCE;
	if ((err = dhd_rtt_sched_geofencing_target(dhd)) != BCME_OK) {
		DHD_RTT_ERR(("geofencing sched failed, err = %d\n", err));
		err_at = 9;
	}
#endif /* WL_NAN */

exit:
	if (err) {
		/* RTT Failed */
		DHD_RTT_ERR(("dhd_rtt_start: Failed & RTT_STOPPED, err = %d,"
			" err_at = %d, rtt_sched_type = %d, rtt_invalid_reason = %d\n"
			" sched_reason = %d",
			err, err_at, rtt_sched_type, rtt_invalid_reason,
			rtt_status->rtt_sched_reason));
		rtt_status->status = RTT_STOPPED;
		/* disable FTM */
		dhd_rtt_ftm_enable(dhd, FALSE);
		if (rtt_status->pm_restore) {
			pm = PM_FAST;
			DHD_RTT_ERR(("pm_restore =%d func =%s \n",
				rtt_status->pm_restore, __FUNCTION__));
			err = wldev_ioctl_set(dev, WLC_SET_PM, &pm, sizeof(pm));
			if (err) {
				DHD_RTT_ERR(("Failed to set PM \n"));
			} else {
				rtt_status->pm_restore = FALSE;
			}
		}
	}
	return err;
}
#endif /* WL_CFG80211 */

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
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	list_for_each_entry(iter, &rtt_status->noti_fn_list, list) {
		GCC_DIAGNOSTIC_POP();
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
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	list_for_each_entry(iter, &rtt_status->noti_fn_list, list) {
		GCC_DIAGNOSTIC_POP();
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
	uint32 bandwidth;
	memset(&host_rate, 0, sizeof(wifi_rate_t));
	if (RSPEC_ISLEGACY(rspec)) {
		host_rate.preamble = 0;
	} else if (RSPEC_ISHT(rspec)) {
		host_rate.preamble = 2;
		host_rate.rateMcsIdx = rspec & WL_RSPEC_RATE_MASK;
	} else if (RSPEC_ISVHT(rspec)) {
		host_rate.preamble = 3;
		host_rate.rateMcsIdx = rspec & WL_RSPEC_VHT_MCS_MASK;
		host_rate.nss = (rspec & WL_RSPEC_VHT_NSS_MASK) >> WL_RSPEC_VHT_NSS_SHIFT;
	}

	bandwidth = RSPEC_BW(rspec);
	switch (bandwidth) {
	case WL_RSPEC_BW_20MHZ:
		host_rate.bw = RTT_RATE_20M;
		break;
	case WL_RSPEC_BW_40MHZ:
		host_rate.bw = RTT_RATE_40M;
		break;
	case WL_RSPEC_BW_80MHZ:
		host_rate.bw = RTT_RATE_80M;
		break;
	case WL_RSPEC_BW_160MHZ:
		host_rate.bw = RTT_RATE_160M;
		break;
	default:
		host_rate.bw = RTT_RATE_20M;
		break;
	}

	host_rate.bitrate = rate_rspec2rate(rspec) / 100; /* 100kbps */
	DHD_RTT(("bit rate : %d\n", host_rate.bitrate));
	return host_rate;
}

#define FTM_FRAME_TYPES	{"SETUP", "TRIGGER", "TIMESTAMP"}
static int
dhd_rtt_convert_results_to_host_v1(rtt_result_t *rtt_result, const uint8 *p_data,
	uint16 tlvid, uint16 len)
{
	int i;
	int err = BCME_OK;
	char eabuf[ETHER_ADDR_STR_LEN];
	wl_proxd_result_flags_t flags;
	wl_proxd_session_state_t session_state;
	wl_proxd_status_t proxd_status;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0))
	struct timespec64 ts;
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39))
	struct timespec ts;
#endif /* LINUX_VER >= 2.6.39 */
	uint32 ratespec;
	uint32 avg_dist;
	const wl_proxd_rtt_result_v1_t *p_data_info = NULL;
	const wl_proxd_rtt_sample_v1_t *p_sample_avg = NULL;
	const wl_proxd_rtt_sample_v1_t *p_sample = NULL;
	wl_proxd_intvl_t rtt;
	wl_proxd_intvl_t p_time;
	uint16 num_rtt = 0, snr = 0, bitflips = 0;
	wl_proxd_phy_error_t tof_phy_error = 0;
	wl_proxd_phy_error_t tof_phy_tgt_error = 0;
	wl_proxd_snr_t tof_target_snr = 0;
	wl_proxd_bitflips_t tof_target_bitflips = 0;
	int16 rssi = 0;
	int32 dist = 0;
	uint8 num_ftm = 0;
	char *ftm_frame_types[] = FTM_FRAME_TYPES;
	rtt_report_t *rtt_report = &(rtt_result->report);

	BCM_REFERENCE(ftm_frame_types);
	BCM_REFERENCE(dist);
	BCM_REFERENCE(rssi);
	BCM_REFERENCE(tof_target_bitflips);
	BCM_REFERENCE(tof_target_snr);
	BCM_REFERENCE(tof_phy_tgt_error);
	BCM_REFERENCE(tof_phy_error);
	BCM_REFERENCE(bitflips);
	BCM_REFERENCE(snr);
	BCM_REFERENCE(session_state);
	BCM_REFERENCE(ftm_session_state_value_to_logstr);

	NULL_CHECK(rtt_report, "rtt_report is NULL", err);
	NULL_CHECK(p_data, "p_data is NULL", err);
	DHD_RTT(("%s enter\n", __FUNCTION__));
	p_data_info = (const wl_proxd_rtt_result_v1_t *) p_data;
	/* unpack and format 'flags' for display */
	flags = ltoh16_ua(&p_data_info->flags);

	/* session state and status */
	session_state = ltoh16_ua(&p_data_info->state);
	proxd_status = ltoh32_ua(&p_data_info->status);
	bcm_ether_ntoa((&(p_data_info->peer)), eabuf);
	ftm_status_value_to_logstr(proxd_status);
	DHD_RTT((">\tTarget(%s) session state=%d(%s), status=%d(%s)\n",
		eabuf,
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
	p_sample_avg = &p_data_info->avg_rtt;
	ftm_tmu_value_to_logstr(ltoh16_ua(&p_sample_avg->rtt.tmu));
	DHD_RTT((">\tavg_rtt sample: rssi=%d rtt=%d%s std_deviation =%d.%d ratespec=0x%08x\n",
		(int16) ltoh16_ua(&p_sample_avg->rssi),
		ltoh32_ua(&p_sample_avg->rtt.intvl),
		ftm_tmu_value_to_logstr(ltoh16_ua(&p_sample_avg->rtt.tmu)),
		ltoh16_ua(&p_data_info->sd_rtt)/10, ltoh16_ua(&p_data_info->sd_rtt)%10,
		ltoh32_ua(&p_sample_avg->ratespec)));

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
	rtt_report->rssi = ABS((wl_proxd_rssi_t)ltoh16_ua(&p_data_info->avg_rtt.rssi)) * 2;

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
	rtt_report->rtt = (wifi_timespan)FTM_INTVL2NSEC(&rtt) * 1000; /* nano -> pico seconds */
	rtt_report->rtt_sd = ltoh16_ua(&p_data_info->sd_rtt); /* nano -> 0.1 nano */
	DHD_RTT(("rtt_report->rtt : %llu\n", rtt_report->rtt));
	DHD_RTT(("rtt_report->rssi : %d (0.5db)\n", rtt_report->rssi));

	/* average distance */
	if (avg_dist != FTM_INVALID) {
		rtt_report->distance = (avg_dist >> 8) * 1000; /* meter -> mm */
		rtt_report->distance += (avg_dist & 0xff) * 1000 / 256;
	} else {
		rtt_report->distance = FTM_INVALID;
	}
	/* time stamp */
	/* get the time elapsed from boot time */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39))
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0))
	ktime_get_boottime_ts64(&ts);
#else
	get_monotonic_boottime(&ts);
#endif
	rtt_report->ts = (uint64)TIMESPEC_TO_US(ts);
#endif /* LINUX_VER >= 2.6.39 */

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

	/* display detail if available */
	num_rtt = ltoh16_ua(&p_data_info->num_rtt);
	if (num_rtt > 0) {
		DHD_RTT((">\tnum rtt: %d samples\n", num_rtt));
		p_sample = &p_data_info->rtt[0];
		for (i = 0; i < num_rtt; i++) {
			snr = 0;
			bitflips = 0;
			tof_phy_error = 0;
			tof_phy_tgt_error = 0;
			tof_target_snr = 0;
			tof_target_bitflips = 0;
			rssi = 0;
			dist = 0;
			num_ftm = p_data_info->num_ftm;
			/* FTM frames 1,4,7,11 have valid snr, rssi and bitflips */
			if ((i % num_ftm) == 1) {
				rssi = (wl_proxd_rssi_t) ltoh16_ua(&p_sample->rssi);
				snr = (wl_proxd_snr_t) ltoh16_ua(&p_sample->snr);
				bitflips = (wl_proxd_bitflips_t) ltoh16_ua(&p_sample->bitflips);
				tof_phy_error =
					(wl_proxd_phy_error_t)
					ltoh32_ua(&p_sample->tof_phy_error);
				tof_phy_tgt_error =
					(wl_proxd_phy_error_t)
					ltoh32_ua(&p_sample->tof_tgt_phy_error);
				tof_target_snr =
					(wl_proxd_snr_t)
					ltoh16_ua(&p_sample->tof_tgt_snr);
				tof_target_bitflips =
					(wl_proxd_bitflips_t)
					ltoh16_ua(&p_sample->tof_tgt_bitflips);
				dist = ltoh32_ua(&p_sample->distance);
			} else {
				rssi = -1;
				snr = 0;
				bitflips = 0;
				dist = 0;
				tof_target_bitflips = 0;
				tof_target_snr = 0;
				tof_phy_tgt_error = 0;
			}
			DHD_RTT((">\t sample[%d]: id=%d rssi=%d snr=0x%x bitflips=%d"
				" tof_phy_error %x tof_phy_tgt_error %x target_snr=0x%x"
				" target_bitflips=%d dist=%d rtt=%d%s status %s"
				" Type %s coreid=%d\n",
				i, p_sample->id, rssi, snr,
				bitflips, tof_phy_error, tof_phy_tgt_error,
				tof_target_snr,
				tof_target_bitflips, dist,
				ltoh32_ua(&p_sample->rtt.intvl),
				ftm_tmu_value_to_logstr(ltoh16_ua(&p_sample->rtt.tmu)),
				ftm_status_value_to_logstr(ltoh32_ua(&p_sample->status)),
				ftm_frame_types[i % num_ftm], p_sample->coreid));
			p_sample++;
		}
	}
	return err;
}

static int
dhd_rtt_convert_results_to_host_v2(rtt_result_t *rtt_result, const uint8 *p_data,
	uint16 tlvid, uint16 len)
{
	int i;
	int err = BCME_OK;
	char eabuf[ETHER_ADDR_STR_LEN];
	wl_proxd_result_flags_t flags;
	wl_proxd_session_state_t session_state;
	wl_proxd_status_t proxd_status;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0))
	struct timespec64 ts;
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39))
	struct timespec ts;
#endif /* LINUX_VER >= 2.6.39 */
	uint32 ratespec;
	uint32 avg_dist;
	const wl_proxd_rtt_result_v2_t *p_data_info = NULL;
	const wl_proxd_rtt_sample_v2_t *p_sample_avg = NULL;
	const wl_proxd_rtt_sample_v2_t *p_sample = NULL;
	uint16 num_rtt = 0;
	wl_proxd_intvl_t rtt;
	wl_proxd_intvl_t p_time;
	uint16 snr = 0, bitflips = 0;
	wl_proxd_phy_error_t tof_phy_error = 0;
	wl_proxd_phy_error_t tof_phy_tgt_error = 0;
	wl_proxd_snr_t tof_target_snr = 0;
	wl_proxd_bitflips_t tof_target_bitflips = 0;
	int16 rssi = 0;
	int32 dist = 0;
	uint32 chanspec = 0;
	uint8 num_ftm = 0;
	char *ftm_frame_types[] =  FTM_FRAME_TYPES;
	rtt_report_t *rtt_report = &(rtt_result->report);

	BCM_REFERENCE(ftm_frame_types);
	BCM_REFERENCE(dist);
	BCM_REFERENCE(rssi);
	BCM_REFERENCE(tof_target_bitflips);
	BCM_REFERENCE(tof_target_snr);
	BCM_REFERENCE(tof_phy_tgt_error);
	BCM_REFERENCE(tof_phy_error);
	BCM_REFERENCE(bitflips);
	BCM_REFERENCE(snr);
	BCM_REFERENCE(chanspec);
	BCM_REFERENCE(session_state);
	BCM_REFERENCE(ftm_session_state_value_to_logstr);

	NULL_CHECK(rtt_report, "rtt_report is NULL", err);
	NULL_CHECK(p_data, "p_data is NULL", err);
	DHD_RTT(("%s enter\n", __FUNCTION__));
	p_data_info = (const wl_proxd_rtt_result_v2_t *) p_data;
	/* unpack and format 'flags' for display */
	flags = ltoh16_ua(&p_data_info->flags);
	/* session state and status */
	session_state = ltoh16_ua(&p_data_info->state);
	proxd_status = ltoh32_ua(&p_data_info->status);
	bcm_ether_ntoa((&(p_data_info->peer)), eabuf);

	if (proxd_status != BCME_OK) {
		DHD_RTT_ERR((">\tTarget(%s) session state=%d(%s), status=%d(%s) "
			"num_meas_ota %d num_valid_rtt %d result_flags %x\n",
			eabuf, session_state,
			ftm_session_state_value_to_logstr(session_state),
			proxd_status, ftm_status_value_to_logstr(proxd_status),
			p_data_info->num_meas, p_data_info->num_valid_rtt,
			p_data_info->flags));
	} else {
		DHD_RTT((">\tTarget(%s) session state=%d(%s), status=%d(%s)\n",
		eabuf, session_state,
		ftm_session_state_value_to_logstr(session_state),
		proxd_status, ftm_status_value_to_logstr(proxd_status)));
	}
	/* show avg_dist (1/256m units), burst_num */
	avg_dist = ltoh32_ua(&p_data_info->avg_dist);
	if (avg_dist == 0xffffffff) {	/* report 'failure' case */
		DHD_RTT((">\tavg_dist=-1m, burst_num=%d, valid_measure_cnt=%d\n",
		ltoh16_ua(&p_data_info->burst_num),
		p_data_info->num_valid_rtt)); /* in a session */
		avg_dist = FTM_INVALID;
	} else {
		DHD_RTT((">\tavg_dist=%d.%04dm, burst_num=%d, valid_measure_cnt=%d num_ftm=%d "
			"num_meas_ota=%d, result_flags=%x\n", avg_dist >> 8, /* 1/256m units */
			((avg_dist & 0xff) * 625) >> 4,
			ltoh16_ua(&p_data_info->burst_num),
			p_data_info->num_valid_rtt,
			p_data_info->num_ftm, p_data_info->num_meas,
			p_data_info->flags)); /* in a session */
	}
	rtt_result->rtt_detail.num_ota_meas = p_data_info->num_meas;
	rtt_result->rtt_detail.result_flags = p_data_info->flags;
	/* show 'avg_rtt' sample */
	/* in v2, avg_rtt is the first element of the variable rtt[] */
	p_sample_avg = &p_data_info->rtt[0];
	ftm_tmu_value_to_logstr(ltoh16_ua(&p_sample_avg->rtt.tmu));
	DHD_RTT((">\tavg_rtt sample: rssi=%d rtt=%d%s std_deviation =%d.%d"
		"ratespec=0x%08x chanspec=0x%08x\n",
		(int16) ltoh16_ua(&p_sample_avg->rssi),
		ltoh32_ua(&p_sample_avg->rtt.intvl),
		ftm_tmu_value_to_logstr(ltoh16_ua(&p_sample_avg->rtt.tmu)),
		ltoh16_ua(&p_data_info->sd_rtt)/10, ltoh16_ua(&p_data_info->sd_rtt)%10,
		ltoh32_ua(&p_sample_avg->ratespec),
		ltoh32_ua(&p_sample_avg->chanspec)));

	/* set peer address */
	rtt_report->addr = p_data_info->peer;

	/* burst num */
	rtt_report->burst_num = ltoh16_ua(&p_data_info->burst_num);

	/* success num */
	rtt_report->success_num = p_data_info->num_valid_rtt;

	/* num-ftm configured */
	rtt_report->ftm_num = p_data_info->num_ftm;

	/* actual number of FTM supported by peer */
	rtt_report->num_per_burst_peer = p_data_info->num_ftm;
	rtt_report->negotiated_burst_num = p_data_info->num_ftm;

	/* status */
	rtt_report->status = ftm_get_statusmap_info(proxd_status,
			&ftm_status_map_info[0], ARRAYSIZE(ftm_status_map_info));

	/* Framework expects status as SUCCESS else all results will be
	* set to zero even if we have partial valid result.
	* So setting status as SUCCESS if we have a valid_rtt
	* On burst timeout we stop burst with "timeout" reason and
	* on msch end we set status as "cancel"
	*/
	if ((proxd_status == WL_PROXD_E_TIMEOUT ||
		proxd_status == WL_PROXD_E_CANCELED) &&
		rtt_report->success_num) {
		rtt_report->status = RTT_STATUS_SUCCESS;
	}

	/* rssi (0.5db) */
	rtt_report->rssi = ABS((wl_proxd_rssi_t)ltoh16_ua(&p_sample_avg->rssi)) * 2;

	/* rx rate */
	ratespec = ltoh32_ua(&p_sample_avg->ratespec);
	rtt_report->rx_rate = dhd_rtt_convert_rate_to_host(ratespec);

	/* tx rate */
	if (flags & WL_PROXD_RESULT_FLAG_VHTACK) {
		rtt_report->tx_rate = dhd_rtt_convert_rate_to_host(0x2010010);
	} else {
		rtt_report->tx_rate = dhd_rtt_convert_rate_to_host(0xc);
	}

	/* rtt_sd */
	rtt.tmu = ltoh16_ua(&p_sample_avg->rtt.tmu);
	rtt.intvl = ltoh32_ua(&p_sample_avg->rtt.intvl);
	rtt_report->rtt = (wifi_timespan)FTM_INTVL2NSEC(&rtt) * 1000; /* nano -> pico seconds */
	rtt_report->rtt_sd = ltoh16_ua(&p_data_info->sd_rtt); /* nano -> 0.1 nano */
	DHD_RTT(("rtt_report->rtt : %llu\n", rtt_report->rtt));
	DHD_RTT(("rtt_report->rssi : %d (0.5db)\n", rtt_report->rssi));

	/* average distance */
	if (avg_dist != FTM_INVALID) {
		rtt_report->distance = (avg_dist >> 8) * 1000; /* meter -> mm */
		rtt_report->distance += (avg_dist & 0xff) * 1000 / 256;
		/* rtt_sd is in 0.1 ns.
		* host needs distance_sd in milli mtrs
		* (0.1 * rtt_sd/2 * 10^-9) * C * 1000
		*/
		rtt_report->distance_sd = rtt_report->rtt_sd * 15; /* mm */
	} else {
		rtt_report->distance = FTM_INVALID;
	}
	/* time stamp */
	/* get the time elapsed from boot time */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39))
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0))
	ktime_get_boottime_ts64(&ts);
#else
	get_monotonic_boottime(&ts);
#endif
	rtt_report->ts = (uint64)TIMESPEC_TO_US(ts);
#endif /* LINUX_VER >= 2.6.39 */

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
	/* display detail if available */
	num_rtt = ltoh16_ua(&p_data_info->num_rtt);
	if (num_rtt > 0) {
		DHD_RTT((">\tnum rtt: %d samples\n", num_rtt));
		p_sample = &p_data_info->rtt[1];
		for (i = 0; i < num_rtt; i++) {
			snr = 0;
			bitflips = 0;
			tof_phy_error = 0;
			tof_phy_tgt_error = 0;
			tof_target_snr = 0;
			tof_target_bitflips = 0;
			rssi = 0;
			dist = 0;
			num_ftm = p_data_info->num_ftm;
			/* FTM frames 1,4,7,11 have valid snr, rssi and bitflips */
			if ((i % num_ftm) == 1) {
				rssi = (wl_proxd_rssi_t) ltoh16_ua(&p_sample->rssi);
				snr = (wl_proxd_snr_t) ltoh16_ua(&p_sample->snr);
				bitflips = (wl_proxd_bitflips_t) ltoh16_ua(&p_sample->bitflips);
				tof_phy_error =
					(wl_proxd_phy_error_t)
					ltoh32_ua(&p_sample->tof_phy_error);
				tof_phy_tgt_error =
					(wl_proxd_phy_error_t)
					ltoh32_ua(&p_sample->tof_tgt_phy_error);
				tof_target_snr =
					(wl_proxd_snr_t)
					ltoh16_ua(&p_sample->tof_tgt_snr);
				tof_target_bitflips =
					(wl_proxd_bitflips_t)
					ltoh16_ua(&p_sample->tof_tgt_bitflips);
				dist = ltoh32_ua(&p_sample->distance);
				chanspec = ltoh32_ua(&p_sample->chanspec);
			} else {
				rssi = -1;
				snr = 0;
				bitflips = 0;
				dist = 0;
				tof_target_bitflips = 0;
				tof_target_snr = 0;
				tof_phy_tgt_error = 0;
			}
			DHD_RTT((">\t sample[%d]: id=%d rssi=%d snr=0x%x bitflips=%d"
				" tof_phy_error %x tof_phy_tgt_error %x target_snr=0x%x"
				" target_bitflips=%d dist=%d rtt=%d%s status %s Type %s"
				" coreid=%d chanspec=0x%08x\n",
				i, p_sample->id, rssi, snr,
				bitflips, tof_phy_error, tof_phy_tgt_error,
				tof_target_snr,
				tof_target_bitflips, dist,
				ltoh32_ua(&p_sample->rtt.intvl),
				ftm_tmu_value_to_logstr(ltoh16_ua(&p_sample->rtt.tmu)),
				ftm_status_value_to_logstr(ltoh32_ua(&p_sample->status)),
				ftm_frame_types[i % num_ftm], p_sample->coreid,
				chanspec));
			p_sample++;
		}
	}
	return err;
}
#ifdef WL_CFG80211
/* Common API for handling Session End.
* This API will flush out the results for a peer MAC.
*
* @For legacy FTM session, this API will be called
* when legacy FTM_SESSION_END event is received.
* @For legacy Nan-RTT , this API will be called when
* we are cancelling the nan-ranging session or on
* nan-ranging-end event.
*/
static void
dhd_rtt_handle_rtt_session_end(dhd_pub_t *dhd)
{

	int idx;
	struct rtt_noti_callback *iter;
	rtt_results_header_t *entry, *next;
	rtt_result_t *next2;
	rtt_result_t *rtt_result;
	rtt_status_info_t *rtt_status = GET_RTTSTATE(dhd);

	/* Cancel pending proxd timeout work if any */
	if (delayed_work_pending(&rtt_status->proxd_timeout)) {
		cancel_delayed_work(&rtt_status->proxd_timeout);
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
		DHD_INFO(("restart to measure rtt\n"));
		schedule_work(&rtt_status->work);
	} else {
		DHD_RTT(("RTT_STOPPED\n"));
		rtt_status->status = RTT_STOPPED;
		/* notify the completed information to others */
		GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
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
		GCC_DIAGNOSTIC_POP();
		/* reinitialize the HEAD */
		INIT_LIST_HEAD(&rtt_status->rtt_results_cache);
		/* clear information for rtt_config */
		rtt_status->rtt_config.rtt_target_cnt = 0;
		memset_s(rtt_status->rtt_config.target_info, TARGET_INFO_SIZE(RTT_MAX_TARGET_CNT),
			0, TARGET_INFO_SIZE(RTT_MAX_TARGET_CNT));
		rtt_status->cur_idx = 0;
	}
}
#endif /* WL_CFG80211 */

#ifdef WL_CFG80211
static int
dhd_rtt_create_failure_result(rtt_status_info_t *rtt_status,
	struct ether_addr *addr)
{
	rtt_results_header_t *rtt_results_header = NULL;
	rtt_target_info_t *rtt_target_info;
	int ret = BCME_OK;
	rtt_result_t *rtt_result;

	/* allocate new header for rtt_results */
	rtt_results_header = (rtt_results_header_t *)MALLOCZ(rtt_status->dhd->osh,
		sizeof(rtt_results_header_t));
	if (!rtt_results_header) {
		ret = -ENOMEM;
		goto exit;
	}
	rtt_target_info = &rtt_status->rtt_config.target_info[rtt_status->cur_idx];
	/* Initialize the head of list for rtt result */
	INIT_LIST_HEAD(&rtt_results_header->result_list);
	/* same src and dest len */
	(void)memcpy_s(&rtt_results_header->peer_mac,
		ETHER_ADDR_LEN, addr, ETHER_ADDR_LEN);
	list_add_tail(&rtt_results_header->list, &rtt_status->rtt_results_cache);

	/* allocate rtt_results for new results */
	rtt_result = (rtt_result_t *)MALLOCZ(rtt_status->dhd->osh,
		sizeof(rtt_result_t));
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
	rtt_result->report.status = RTT_STATUS_FAIL_NO_RSP;
	/* same src and dest len */
	(void)memcpy_s(&rtt_result->report.addr, ETHER_ADDR_LEN,
		&rtt_target_info->addr, ETHER_ADDR_LEN);
	rtt_result->report.distance = FTM_INVALID;
	list_add_tail(&rtt_result->list, &rtt_results_header->result_list);
	rtt_results_header->result_cnt++;
	rtt_results_header->result_tot_len += rtt_result->report_len;
exit:
	return ret;
}

static bool
dhd_rtt_get_report_header(rtt_status_info_t *rtt_status,
	rtt_results_header_t **rtt_results_header, struct ether_addr *addr)
{
	rtt_results_header_t *entry;
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	/* find a rtt_report_header for this mac address */
	list_for_each_entry(entry, &rtt_status->rtt_results_cache, list) {
		GCC_DIAGNOSTIC_POP();
		if (!memcmp(&entry->peer_mac, addr, ETHER_ADDR_LEN))  {
			/* found a rtt_report_header for peer_mac in the list */
			if (rtt_results_header) {
				*rtt_results_header = entry;
			}
			return TRUE;
		}
	}
	return FALSE;
}

int
dhd_rtt_handle_nan_rtt_session_end(dhd_pub_t *dhd, struct ether_addr *peer)
{
	bool is_new = TRUE;
	rtt_status_info_t *rtt_status = GET_RTTSTATE(dhd);
	mutex_lock(&rtt_status->rtt_mutex);
	is_new = !dhd_rtt_get_report_header(rtt_status, NULL, peer);

	if (is_new) { /* no FTM result..create failure result */
		dhd_rtt_create_failure_result(rtt_status, peer);
	}
	dhd_rtt_handle_rtt_session_end(dhd);
	mutex_unlock(&rtt_status->rtt_mutex);
	return BCME_OK;
}
#endif /* WL_CFG80211 */

static bool
dhd_rtt_is_valid_measurement(rtt_result_t *rtt_result)
{
	bool ret = FALSE;

	if (rtt_result && (rtt_result->report.success_num != 0)) {
		ret = TRUE;
	}
	return ret;
}

static int
dhd_rtt_parse_result_event(wl_proxd_event_t *proxd_ev_data,
	int tlvs_len, rtt_result_t *rtt_result)
{
	int ret = BCME_OK;

	/* unpack TLVs and invokes the cbfn to print the event content TLVs */
	ret = bcm_unpack_xtlv_buf((void *) rtt_result,
			(uint8 *)&proxd_ev_data->tlvs[0], tlvs_len,
			BCM_XTLV_OPTION_ALIGN32, rtt_unpack_xtlv_cbfn);
	if (ret != BCME_OK) {
		DHD_RTT_ERR(("%s : Failed to unpack xtlv for an event\n",
			__FUNCTION__));
		goto exit;
	}
	/* fill out the results from the configuration param */
	rtt_result->report.type = RTT_TWO_WAY;
	DHD_RTT(("report->ftm_num : %d\n", rtt_result->report.ftm_num));
	rtt_result->report_len = RTT_REPORT_SIZE;
	rtt_result->detail_len = sizeof(rtt_result->rtt_detail);

exit:
	return ret;

}

static int
dhd_rtt_handle_directed_rtt_burst_end(dhd_pub_t *dhd, struct ether_addr *peer_addr,
        wl_proxd_event_t *proxd_ev_data, int tlvs_len, rtt_result_t *rtt_result, bool is_nan)
{
	rtt_status_info_t *rtt_status;
	rtt_results_header_t *rtt_results_header = NULL;
	bool is_new = TRUE;
	int ret = BCME_OK;
	int err_at = 0;

	rtt_status = GET_RTTSTATE(dhd);
	is_new = !dhd_rtt_get_report_header(rtt_status,
		&rtt_results_header, peer_addr);

	if (tlvs_len > 0) {
		if (is_new) {
			/* allocate new header for rtt_results */
			rtt_results_header = (rtt_results_header_t *)MALLOCZ(rtt_status->dhd->osh,
				sizeof(rtt_results_header_t));
			if (!rtt_results_header) {
				ret = BCME_NORESOURCE;
				err_at = 1;
				goto exit;
			}
			/* Initialize the head of list for rtt result */
			INIT_LIST_HEAD(&rtt_results_header->result_list);
			/* same src and header len */
			(void)memcpy_s(&rtt_results_header->peer_mac, ETHER_ADDR_LEN,
				peer_addr, ETHER_ADDR_LEN);
			list_add_tail(&rtt_results_header->list, &rtt_status->rtt_results_cache);
		}

		ret = dhd_rtt_parse_result_event(proxd_ev_data, tlvs_len, rtt_result);
		if ((ret == BCME_OK) && ((!is_nan) ||
			dhd_rtt_is_valid_measurement(rtt_result))) {
			/*
			 * Add to list, if non-nan RTT (legacy) or
			 * valid measurement in nan rtt case
			 */
			list_add_tail(&rtt_result->list, &rtt_results_header->result_list);
			rtt_results_header->result_cnt++;
			rtt_results_header->result_tot_len += rtt_result->report_len +
				rtt_result->detail_len;
		} else {
			err_at = 2;
			if (ret == BCME_OK) {
				/* Case for nan rtt invalid measurement */
				ret = BCME_ERROR;
				err_at = 3;
			}
			goto exit;
		}
	} else {
		ret = BCME_ERROR;
		err_at = 4;
		goto exit;
	}

exit:
	if (ret != BCME_OK) {
		DHD_RTT_ERR(("dhd_rtt_handle_directed_rtt_burst_end: failed, "
			" ret = %d, err_at = %d\n", ret, err_at));
		if (rtt_results_header) {
			list_del(&rtt_results_header->list);
			kfree(rtt_results_header);
			rtt_results_header = NULL;
		}
	}
	return ret;
}

#ifdef WL_NAN
static	void
dhd_rtt_nan_range_report(struct bcm_cfg80211 *cfg,
		rtt_result_t *rtt_result)
{
	wl_nan_ev_rng_rpt_ind_t range_res;
	nan_ranging_inst_t *rng_inst = NULL;
	dhd_pub_t *dhd = (struct dhd_pub *)(cfg->pub);
	rtt_status_info_t *rtt_status = GET_RTTSTATE(dhd);

	UNUSED_PARAMETER(range_res);

	if (!dhd_rtt_is_valid_measurement(rtt_result)) {
		/* Drop Invalid Measurements for NAN RTT report */
		DHD_RTT(("dhd_rtt_nan_range_report: Drop Invalid Measurements\n"));
		return;
	}
	bzero(&range_res, sizeof(range_res));
	range_res.indication = 0;
	range_res.dist_mm = rtt_result->report.distance;
	/* same src and header len, ignoring ret val here */
	(void)memcpy_s(&range_res.peer_m_addr, ETHER_ADDR_LEN,
		&rtt_result->report.addr, ETHER_ADDR_LEN);
	wl_cfgnan_process_range_report(cfg, &range_res);
	/*
	 * suspend geofence ranging for this target
	 * and move to next target
	 * after valid measurement for the target
	 */
	rng_inst = wl_cfgnan_check_for_ranging(cfg, &range_res.peer_m_addr);
	if (rng_inst) {
		wl_cfgnan_suspend_geofence_rng_session(bcmcfg_to_prmry_ndev(cfg),
			&rng_inst->peer_addr, RTT_GEO_SUSPN_RANGE_RES_REPORTED, 0);
		GEOFENCE_RTT_LOCK(rtt_status);
		dhd_rtt_move_geofence_cur_target_idx_to_next(dhd);
		GEOFENCE_RTT_UNLOCK(rtt_status);
		wl_cfgnan_reset_geofence_ranging(cfg,
			rng_inst, RTT_SCHED_RNG_RPT_GEOFENCE);
	}
}

static int
dhd_rtt_handle_nan_burst_end(dhd_pub_t *dhd, struct ether_addr *peer_addr,
	wl_proxd_event_t *proxd_ev_data, int tlvs_len)
{
	struct net_device *ndev = NULL;
	struct bcm_cfg80211 *cfg = NULL;
	nan_ranging_inst_t *rng_inst = NULL;
	rtt_status_info_t *rtt_status = NULL;
	rtt_result_t *rtt_result = NULL;
	bool is_geofence = FALSE;
	int ret = BCME_OK;

	ndev = dhd_linux_get_primary_netdev(dhd);
	cfg =  wiphy_priv(ndev->ieee80211_ptr->wiphy);

	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", ret);
	NAN_MUTEX_LOCK();
	mutex_lock(&rtt_status->rtt_mutex);

	if ((cfg->nan_enable == FALSE) ||
		ETHER_ISNULLADDR(peer_addr)) {
		DHD_RTT_ERR(("Received Burst End with NULL ether addr, "
			"or nan disable, nan_enable = %d\n", cfg->nan_enable));
		ret = BCME_UNSUPPORTED;
		goto exit;
	}

	rng_inst = wl_cfgnan_check_for_ranging(cfg, peer_addr);
	if (rng_inst) {
		is_geofence = (rng_inst->range_type
			== RTT_TYPE_NAN_GEOFENCE);
	} else {
		DHD_RTT_ERR(("Received Burst End without Ranging Instance\n"));
		ret = BCME_ERROR;
		goto exit;
	}

	/* allocate rtt_results for new results */
	rtt_result = (rtt_result_t *)MALLOCZ(dhd->osh, sizeof(rtt_result_t));
	if (!rtt_result) {
		ret = BCME_NORESOURCE;
		goto exit;
	}

	if (is_geofence) {
		ret = dhd_rtt_parse_result_event(proxd_ev_data, tlvs_len, rtt_result);
		if (ret != BCME_OK) {
			DHD_RTT_ERR(("avilog: dhd_rtt_handle_nan_burst_end: "
				"dhd_rtt_parse_result_event failed\n"));
			goto exit;
		}
	} else {
		if (RTT_IS_STOPPED(rtt_status)) {
			/* Ignore the Proxd event */
			DHD_RTT((" event handler rtt is stopped \n"));
			if (rtt_status->flags == WL_PROXD_SESSION_FLAG_TARGET) {
				DHD_RTT(("Device is target/Responder. Recv the event. \n"));
			} else {
				ret = BCME_UNSUPPORTED;
				goto exit;
			}
		}
		ret = dhd_rtt_handle_directed_rtt_burst_end(dhd, peer_addr,
			proxd_ev_data, tlvs_len, rtt_result, TRUE);
		if (ret != BCME_OK) {
			goto exit;
		}

	}

exit:
	mutex_unlock(&rtt_status->rtt_mutex);
	if (ret == BCME_OK) {
		dhd_rtt_nan_range_report(cfg, rtt_result);
	}
	if (rtt_result &&
		((ret != BCME_OK) || is_geofence)) {
		kfree(rtt_result);
		rtt_result = NULL;
	}
	NAN_MUTEX_UNLOCK();
	return ret;
}
#endif /* WL_NAN */

int
dhd_rtt_event_handler(dhd_pub_t *dhd, wl_event_msg_t *event, void *event_data)
{
	int ret = BCME_OK;
	int tlvs_len;
	uint16 version;
	wl_proxd_event_t *p_event;
	wl_proxd_event_type_t event_type;
	wl_proxd_ftm_session_status_t session_status;
	const ftm_strmap_entry_t *p_loginfo;
	rtt_result_t *rtt_result;
#ifdef WL_CFG80211
	rtt_status_info_t *rtt_status;
	rtt_results_header_t *rtt_results_header = NULL;
	bool is_new = TRUE;
#endif /* WL_CFG80211 */

	DHD_RTT(("Enter %s \n", __FUNCTION__));
	NULL_CHECK(dhd, "dhd is NULL", ret);

	if (ntoh32_ua((void *)&event->datalen) < OFFSETOF(wl_proxd_event_t, tlvs)) {
		DHD_RTT(("%s: wrong datalen:%d\n", __FUNCTION__,
			ntoh32_ua((void *)&event->datalen)));
		return -EINVAL;
	}
	event_type = ntoh32_ua((void *)&event->event_type);
	if (event_type != WLC_E_PROXD) {
		DHD_RTT_ERR((" failed event \n"));
		return -EINVAL;
	}

	if (!event_data) {
		DHD_RTT_ERR(("%s: event_data:NULL\n", __FUNCTION__));
		return -EINVAL;
	}
	p_event = (wl_proxd_event_t *) event_data;
	version = ltoh16(p_event->version);
	if (version < WL_PROXD_API_VERSION) {
		DHD_RTT_ERR(("ignore non-ftm event version = 0x%0x < WL_PROXD_API_VERSION (0x%x)\n",
			version, WL_PROXD_API_VERSION));
		return ret;
	}

	event_type = (wl_proxd_event_type_t) ltoh16(p_event->type);

	DHD_RTT(("event_type=0x%x, ntoh16()=0x%x, ltoh16()=0x%x\n",
		p_event->type, ntoh16(p_event->type), ltoh16(p_event->type)));
	p_loginfo = ftm_get_event_type_loginfo(event_type);
	if (p_loginfo == NULL) {
		DHD_RTT_ERR(("receive an invalid FTM event %d\n", event_type));
		ret = -EINVAL;
		return ret;	/* ignore this event */
	}
	/* get TLVs len, skip over event header */
	if (ltoh16(p_event->len) < OFFSETOF(wl_proxd_event_t, tlvs)) {
		DHD_RTT_ERR(("invalid FTM event length:%d\n", ltoh16(p_event->len)));
		ret = -EINVAL;
		return ret;
	}
	tlvs_len = ltoh16(p_event->len) - OFFSETOF(wl_proxd_event_t, tlvs);
	DHD_RTT(("receive '%s' event: version=0x%x len=%d method=%d sid=%d tlvs_len=%d\n",
		p_loginfo->text,
		version,
		ltoh16(p_event->len),
		ltoh16(p_event->method),
		ltoh16(p_event->sid),
		tlvs_len));
#ifdef WL_CFG80211
#ifdef WL_NAN
	if ((event_type == WL_PROXD_EVENT_BURST_END) &&
			dhd_rtt_is_nan_peer(dhd, &event->addr)) {
		DHD_RTT(("WL_PROXD_EVENT_BURST_END for NAN RTT\n"));
		ret = dhd_rtt_handle_nan_burst_end(dhd, &event->addr, p_event, tlvs_len);
		return ret;
	}
#endif /* WL_NAN */

	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", ret);
	mutex_lock(&rtt_status->rtt_mutex);

	if (RTT_IS_STOPPED(rtt_status)) {
		/* Ignore the Proxd event */
		DHD_RTT((" event handler rtt is stopped \n"));
		if (rtt_status->flags == WL_PROXD_SESSION_FLAG_TARGET) {
			DHD_RTT(("Device is target/Responder. Recv the event. \n"));
		} else {
			ret = BCME_NOTREADY;
			goto exit;
		}
	}
#endif /* WL_CFG80211 */

#ifdef WL_CFG80211
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	is_new = !dhd_rtt_get_report_header(rtt_status,
		&rtt_results_header, &event->addr);
	GCC_DIAGNOSTIC_POP();
#endif /* WL_CFG80211 */
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
		DHD_RTT(("WL_PROXD_EVENT_BURST_END for Legacy RTT\n"));
		/* allocate rtt_results for new legacy rtt results */
		rtt_result = (rtt_result_t *)MALLOCZ(dhd->osh, sizeof(rtt_result_t));
		if (!rtt_result) {
			ret = -ENOMEM;
			goto exit;
		}
		ret = dhd_rtt_handle_directed_rtt_burst_end(dhd, &event->addr,
			p_event, tlvs_len, rtt_result, FALSE);
		if (rtt_result && (ret != BCME_OK)) {
			kfree(rtt_result);
			rtt_result = NULL;
			goto exit;
		}
		break;
	case WL_PROXD_EVENT_SESSION_END:
		DHD_RTT(("WL_PROXD_EVENT_SESSION_END\n"));
		if (dhd_rtt_is_nan_peer(dhd, &event->addr)) {
			/*
			 * Nothing to do for session end for nan peer
			 * All taken care in burst end and nan rng rep
			 */
			break;
		}
#ifdef WL_CFG80211
		if (!RTT_IS_ENABLED(rtt_status)) {
			DHD_RTT(("Ignore the session end evt\n"));
			goto exit;
		}
#endif /* WL_CFG80211 */
		if (tlvs_len > 0) {
			/* unpack TLVs and invokes the cbfn to print the event content TLVs */
			ret = bcm_unpack_xtlv_buf((void *) &session_status,
				(uint8 *)&p_event->tlvs[0], tlvs_len,
				BCM_XTLV_OPTION_ALIGN32, rtt_unpack_xtlv_cbfn);
			if (ret != BCME_OK) {
				DHD_RTT_ERR(("%s : Failed to unpack xtlv for an event\n",
					__FUNCTION__));
				goto exit;
			}
		}
#ifdef WL_CFG80211
		/* In case of no result for the peer device, make fake result for error case */
		if (is_new) {
			dhd_rtt_create_failure_result(rtt_status, &event->addr);
		}
		DHD_RTT(("\n Not Nan peer..proceed to notify result and restart\n"));
		dhd_rtt_handle_rtt_session_end(dhd);
#endif /* WL_CFG80211 */
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
	case WL_PROXD_EVENT_COLLECT:
		DHD_RTT(("WL_PROXD_EVENT_COLLECT\n"));
		if (tlvs_len > 0) {
			void *buffer = NULL;
			if (!(buffer = (void *)MALLOCZ(dhd->osh, tlvs_len))) {
				ret = -ENOMEM;
				goto exit;
			}
			/* unpack TLVs and invokes the cbfn to print the event content TLVs */
			ret = bcm_unpack_xtlv_buf(buffer,
				(uint8 *)&p_event->tlvs[0], tlvs_len,
				BCM_XTLV_OPTION_NONE, rtt_unpack_xtlv_cbfn);
			kfree(buffer);
			if (ret != BCME_OK) {
				DHD_RTT_ERR(("%s : Failed to unpack xtlv for event %d\n",
					__FUNCTION__, event_type));
				goto exit;
			}
		}
		break;
	case WL_PROXD_EVENT_MF_STATS:
		DHD_RTT(("WL_PROXD_EVENT_MF_STATS\n"));
		if (tlvs_len > 0) {
			void *buffer = NULL;
			if (!(buffer = (void *)MALLOCZ(dhd->osh, tlvs_len))) {
				ret = -ENOMEM;
				goto exit;
			}
			/* unpack TLVs and invokes the cbfn to print the event content TLVs */
			ret = bcm_unpack_xtlv_buf(buffer,
				(uint8 *)&p_event->tlvs[0], tlvs_len,
				BCM_XTLV_OPTION_NONE, rtt_unpack_xtlv_cbfn);
			kfree(buffer);
			if (ret != BCME_OK) {
				DHD_RTT_ERR(("%s : Failed to unpack xtlv for event %d\n",
					__FUNCTION__, event_type));
				goto exit;
			}
		}
		break;

	default:
		DHD_RTT_ERR(("WLC_E_PROXD: not supported EVENT Type:%d\n", event_type));
		break;
	}
exit:
#ifdef WL_CFG80211
	mutex_unlock(&rtt_status->rtt_mutex);
#endif /* WL_CFG80211 */

	return ret;
}

#ifdef WL_CFG80211
static void
dhd_rtt_work(struct work_struct *work)
{
	rtt_status_info_t *rtt_status;
	dhd_pub_t *dhd;

	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	rtt_status = container_of(work, rtt_status_info_t, work);
	GCC_DIAGNOSTIC_POP();

	dhd = rtt_status->dhd;
	if (dhd == NULL) {
		DHD_RTT_ERR(("%s : dhd is NULL\n", __FUNCTION__));
		return;
	}
	(void) dhd_rtt_start(dhd);
}
#endif /* WL_CFG80211 */

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

	/* set rtt capabilities */
	if (rtt_status->rtt_capa.proto & RTT_CAP_ONE_WAY)
		capa->rtt_one_sided_supported = 1;
	if (rtt_status->rtt_capa.proto & RTT_CAP_FTM_WAY)
		capa->rtt_ftm_supported = 1;

	if (rtt_status->rtt_capa.feature & RTT_FEATURE_LCI)
		capa->lci_support = 1;
	if (rtt_status->rtt_capa.feature & RTT_FEATURE_LCR)
		capa->lcr_support = 1;
	if (rtt_status->rtt_capa.feature & RTT_FEATURE_PREAMBLE)
		capa->preamble_support = 1;
	if (rtt_status->rtt_capa.feature & RTT_FEATURE_BW)
		capa->bw_support = 1;

	/* bit mask */
	capa->preamble_support = rtt_status->rtt_capa.preamble;
	capa->bw_support = rtt_status->rtt_capa.bw;

	return err;
}

#ifdef WL_CFG80211
int
dhd_rtt_avail_channel(dhd_pub_t *dhd, wifi_channel_info *channel_info)
{
	u32 chanspec = 0;
	int err = BCME_OK;
	chanspec_t c = 0;
	u32 channel;
	struct net_device *dev = dhd_linux_get_primary_netdev(dhd);

	if ((err = wldev_iovar_getint(dev, "chanspec",
		(s32 *)&chanspec)) == BCME_OK) {
		c = (chanspec_t)dtoh32(chanspec);
		c = wl_chspec_driver_to_host(c);
		channel  = wf_chspec_ctlchan(c);
		DHD_RTT((" control channel is %d \n", channel));
		if (CHSPEC_IS20(c)) {
			channel_info->width = WIFI_CHAN_WIDTH_20;
			DHD_RTT((" band is 20 \n"));
		} else if (CHSPEC_IS40(c)) {
			channel_info->width = WIFI_CHAN_WIDTH_40;
			DHD_RTT(("band is 40 \n"));
		} else {
			channel_info->width = WIFI_CHAN_WIDTH_80;
			DHD_RTT(("band is 80 \n"));
		}
		if (CHSPEC_IS2G(c) && (channel >= CH_MIN_2G_CHANNEL) &&
			(channel <= CH_MAX_2G_CHANNEL)) {
			channel_info->center_freq =
				ieee80211_channel_to_frequency(channel, IEEE80211_BAND_2GHZ);
		} else if (CHSPEC_IS5G(c) && channel >= CH_MIN_5G_CHANNEL) {
			channel_info->center_freq =
				ieee80211_channel_to_frequency(channel, IEEE80211_BAND_5GHZ);
		}
		if ((channel_info->width == WIFI_CHAN_WIDTH_80) ||
			(channel_info->width == WIFI_CHAN_WIDTH_40)) {
			channel = CHSPEC_CHANNEL(c);
			channel_info->center_freq0 =
				ieee80211_channel_to_frequency(channel, IEEE80211_BAND_5GHZ);
		}
	} else {
		DHD_RTT_ERR(("Failed to get the chanspec \n"));
	}
	return err;
}

int
dhd_rtt_enable_responder(dhd_pub_t *dhd, wifi_channel_info *channel_info)
{
	int err = BCME_OK;
	char chanbuf[CHANSPEC_STR_LEN];
	int pm = PM_OFF;
	int ftm_cfg_cnt = 0;
	chanspec_t chanspec;
	wifi_channel_info_t channel;
	struct net_device *dev = dhd_linux_get_primary_netdev(dhd);
	ftm_config_options_info_t ftm_configs[FTM_MAX_CONFIGS];
	ftm_config_param_info_t ftm_params[FTM_MAX_PARAMS];
	rtt_status_info_t *rtt_status;

	memset(&channel, 0, sizeof(channel));
	BCM_REFERENCE(chanbuf);
	NULL_CHECK(dhd, "dhd is NULL", err);
	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", err);
	if (RTT_IS_STOPPED(rtt_status)) {
		DHD_RTT(("STA responder/Target. \n"));
	}
	DHD_RTT(("Enter %s \n", __FUNCTION__));
	if (!dhd_is_associated(dhd, 0, NULL)) {
		if (channel_info) {
			channel.width = channel_info->width;
			channel.center_freq = channel_info->center_freq;
			channel.center_freq0 = channel_info->center_freq;
		}
		else {
			channel.width = WIFI_CHAN_WIDTH_80;
			channel.center_freq = DEFAULT_FTM_FREQ;
			channel.center_freq0 = DEFAULT_FTM_CNTR_FREQ0;
		}
		chanspec =  dhd_rtt_convert_to_chspec(channel);
		DHD_RTT(("chanspec/channel set as %s for rtt.\n",
			wf_chspec_ntoa(chanspec, chanbuf)));
		err = wldev_iovar_setint(dev, "chanspec", chanspec);
		if (err) {
			DHD_RTT_ERR(("Failed to set the chanspec \n"));
		}
	}
	rtt_status->pm = PM_OFF;
	err = wldev_ioctl_get(dev, WLC_GET_PM, &rtt_status->pm, sizeof(rtt_status->pm));
	DHD_RTT(("Current PM value read %d\n", rtt_status->pm));
	if (err) {
		DHD_RTT_ERR(("Failed to get the PM value \n"));
	} else {
		err = wldev_ioctl_set(dev, WLC_SET_PM, &pm, sizeof(pm));
		if (err) {
			DHD_RTT_ERR(("Failed to set the PM \n"));
			rtt_status->pm_restore = FALSE;
		} else {
			rtt_status->pm_restore = TRUE;
		}
	}
	if (!RTT_IS_ENABLED(rtt_status)) {
		err = dhd_rtt_ftm_enable(dhd, TRUE);
		if (err) {
			DHD_RTT_ERR(("Failed to enable FTM (%d)\n", err));
			goto exit;
		}
		DHD_RTT(("FTM enabled \n"));
	}
	rtt_status->status = RTT_ENABLED;
	DHD_RTT(("Responder enabled \n"));
	memset(ftm_configs, 0, sizeof(ftm_configs));
	memset(ftm_params, 0, sizeof(ftm_params));
	ftm_configs[ftm_cfg_cnt].enable = TRUE;
	ftm_configs[ftm_cfg_cnt++].flags = WL_PROXD_SESSION_FLAG_TARGET;
	rtt_status->flags = WL_PROXD_SESSION_FLAG_TARGET;
	DHD_RTT(("Set the device as responder \n"));
	err = dhd_rtt_ftm_config(dhd, FTM_DEFAULT_SESSION, FTM_CONFIG_CAT_OPTIONS,
		ftm_configs, ftm_cfg_cnt);
exit:
	if (err) {
		rtt_status->status = RTT_STOPPED;
		DHD_RTT_ERR(("rtt is stopped  %s \n", __FUNCTION__));
		dhd_rtt_ftm_enable(dhd, FALSE);
		DHD_RTT(("restoring the PM value \n"));
		if (rtt_status->pm_restore) {
			pm = PM_FAST;
			err = wldev_ioctl_set(dev, WLC_SET_PM, &pm, sizeof(pm));
			if (err) {
				DHD_RTT_ERR(("Failed to restore PM \n"));
			} else {
				rtt_status->pm_restore = FALSE;
			}
		}
	}
	return err;
}

int
dhd_rtt_cancel_responder(dhd_pub_t *dhd)
{
	int err = BCME_OK;
	rtt_status_info_t *rtt_status;
	int pm = 0;
	struct net_device *dev = dhd_linux_get_primary_netdev(dhd);

	NULL_CHECK(dhd, "dhd is NULL", err);
	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", err);
	DHD_RTT(("Enter %s \n", __FUNCTION__));
	err = dhd_rtt_ftm_enable(dhd, FALSE);
	if (err) {
		DHD_RTT_ERR(("failed to disable FTM (%d)\n", err));
	}
	rtt_status->status = RTT_STOPPED;
	if (rtt_status->pm_restore) {
		pm = PM_FAST;
		DHD_RTT(("pm_restore =%d \n", rtt_status->pm_restore));
		err = wldev_ioctl_set(dev, WLC_SET_PM, &pm, sizeof(pm));
		if (err) {
			DHD_RTT_ERR(("Failed to restore PM \n"));
		} else {
			rtt_status->pm_restore = FALSE;
		}
	}
	return err;
}
#endif /* WL_CFG80211 */

int
dhd_rtt_init(dhd_pub_t *dhd)
{
	int err = BCME_OK;
#ifdef WL_CFG80211
	int ret;
	int32 drv_up = 1;
	int32 version;
	rtt_status_info_t *rtt_status;
	ftm_config_param_info_t ftm_params[FTM_MAX_PARAMS];
	int ftm_param_cnt = 0;

	NULL_CHECK(dhd, "dhd is NULL", err);
	if (dhd->rtt_state) {
		return err;
	}
	dhd->rtt_state = (rtt_status_info_t *)MALLOCZ(dhd->osh,
		sizeof(rtt_status_info_t));
	if (dhd->rtt_state == NULL) {
		err = BCME_NOMEM;
		DHD_RTT_ERR(("%s : failed to create rtt_state\n", __FUNCTION__));
		return err;
	}
	bzero(dhd->rtt_state, sizeof(rtt_status_info_t));
	rtt_status = GET_RTTSTATE(dhd);
	rtt_status->rtt_config.target_info =
			(rtt_target_info_t *)MALLOCZ(dhd->osh,
			TARGET_INFO_SIZE(RTT_MAX_TARGET_CNT));
	if (rtt_status->rtt_config.target_info == NULL) {
		DHD_RTT_ERR(("%s failed to allocate the target info for %d\n",
			__FUNCTION__, RTT_MAX_TARGET_CNT));
		err = BCME_NOMEM;
		goto exit;
	}
	rtt_status->dhd = dhd;
	/* need to do WLC_UP  */
	dhd_wl_ioctl_cmd(dhd, WLC_UP, (char *)&drv_up, sizeof(int32), TRUE, 0);

	ret = dhd_rtt_get_version(dhd, &version);
	if (ret == BCME_OK && (version == WL_PROXD_API_VERSION)) {
		DHD_RTT_ERR(("%s : FTM is supported\n", __FUNCTION__));
		/* rtt_status->rtt_capa.proto |= RTT_CAP_ONE_WAY; */
		rtt_status->rtt_capa.proto |= RTT_CAP_FTM_WAY;

		/* indicate to set tx rate */
		rtt_status->rtt_capa.feature |= RTT_FEATURE_LCI;
		rtt_status->rtt_capa.feature |= RTT_FEATURE_LCR;
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
			DHD_RTT_ERR(("%s : FTM is not supported\n", __FUNCTION__));
		} else {
			DHD_RTT_ERR(("%s : FTM version mismatch between HOST (%d) and FW (%d)\n",
				__FUNCTION__, WL_PROXD_API_VERSION, version));
		}
	}
	/* cancel all of RTT request once we got the cancel request */
	rtt_status->all_cancel = TRUE;
	mutex_init(&rtt_status->rtt_mutex);
	mutex_init(&rtt_status->geofence_mutex);
	INIT_LIST_HEAD(&rtt_status->noti_fn_list);
	INIT_LIST_HEAD(&rtt_status->rtt_results_cache);
	INIT_WORK(&rtt_status->work, dhd_rtt_work);
	/* initialize proxd timer */
	INIT_DELAYED_WORK(&rtt_status->proxd_timeout, dhd_rtt_timeout_work);
#ifdef WL_NAN
	/* initialize proxd retry timer */
	INIT_DELAYED_WORK(&rtt_status->rtt_retry_timer, dhd_rtt_retry_work);
	/* initialize non zero params of geofenne cfg */
	rtt_status->geofence_cfg.cur_target_idx = DHD_RTT_INVALID_TARGET_INDEX;
#endif /* WL_NAN */
	/* Global proxd config */
	ftm_params[ftm_param_cnt].event_mask = ((1 << WL_PROXD_EVENT_BURST_END) |
		(1 << WL_PROXD_EVENT_SESSION_END));
	ftm_params[ftm_param_cnt++].tlvid = WL_PROXD_TLV_ID_EVENT_MASK;
	dhd_rtt_ftm_config(dhd, 0, FTM_CONFIG_CAT_GENERAL,
			ftm_params, ftm_param_cnt);
exit:
	if (err < 0) {
		kfree(rtt_status->rtt_config.target_info);
		kfree(dhd->rtt_state);
	}
#endif /* WL_CFG80211 */
	return err;

}

int
dhd_rtt_deinit(dhd_pub_t *dhd)
{
	int err = BCME_OK;
#ifdef WL_CFG80211
	rtt_status_info_t *rtt_status;
	rtt_results_header_t *rtt_header, *next;
	rtt_result_t *rtt_result, *next2;
	struct rtt_noti_callback *iter, *iter2;
	NULL_CHECK(dhd, "dhd is NULL", err);
	rtt_status = GET_RTTSTATE(dhd);
	NULL_CHECK(rtt_status, "rtt_status is NULL", err);
	rtt_status->status = RTT_STOPPED;
	DHD_RTT(("rtt is stopped %s \n", __FUNCTION__));
	/* clear evt callback list */
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
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
	GCC_DIAGNOSTIC_POP();

	kfree(rtt_status->rtt_config.target_info);
	kfree(dhd->rtt_state);
	dhd->rtt_state = NULL;
#endif /* WL_CFG80211 */
	return err;
}
