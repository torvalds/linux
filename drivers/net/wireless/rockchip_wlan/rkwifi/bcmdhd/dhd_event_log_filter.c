/*
 * Wifi dongle status Filter and Report
 *
 * Copyright (C) 2020, Broadcom.
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
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

/*
 * Filter MODULE and Report MODULE
 */

#include <typedefs.h>
#include <osl.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_dbg.h>
#include <dhd_debug.h>
#include <event_log.h>
#include <event_trace.h>
#include <bcmtlv.h>
#include <bcmwifi_channels.h>
#include <dhd_event_log_filter.h>
#include <wl_cfg80211.h>
#include <dhd_bitpack.h>
#include <dhd_pktlog.h>
#ifdef DHD_STATUS_LOGGING
#include <dhd_statlog.h>
#endif /* DHD_STATUS_LOGGING */

#ifdef IL_BIGENDIAN
#include <bcmendian.h>
#define htod32(i) (bcmswap32(i))
#define htod16(i) (bcmswap16(i))
#define dtoh32(i) (bcmswap32(i))
#define dtoh16(i) (bcmswap16(i))
#define htodchanspec(i) htod16(i)
#define dtohchanspec(i) dtoh16(i)
#else
#define htod32(i) (i)
#define htod16(i) (i)
#define dtoh32(i) (i)
#define dtoh16(i) (i)
#define htodchanspec(i) (i)
#define dtohchanspec(i) (i)
#endif /* IL_BIGENDINAN */

#define DHD_FILTER_ERR_INTERNAL(fmt, ...) DHD_ERROR(("EWPF-" fmt, ##__VA_ARGS__))
#ifdef DHD_REPLACE_LOG_INFO_TO_TRACE
#define DHD_FILTER_TRACE_INTERNAL(fmt, ...) DHD_TRACE(("EWPF-" fmt, ##__VA_ARGS__))
#else
#define DHD_FILTER_TRACE_INTERNAL(fmt, ...) DHD_INFO(("EWPF-" fmt, ##__VA_ARGS__))
#endif /* DHD_REPLACE_LOG_INFO_TO_TRACE */

#define DHD_FILTER_ERR(x) DHD_FILTER_ERR_INTERNAL x
#define DHD_FILTER_TRACE(x) DHD_FILTER_TRACE_INTERNAL x

/* ========= EWP Filter functions ============= */
//#define EWPF_DEBUG
#define EWPF_DEBUG_BUF_LEN	512
#define EWPF_VAL_CNT_PLINE	16

#define EWPF_REPORT_MAX_DATA	32	/* MAX record per slice */

#define EWPF_INVALID	(-1)
#define EWPF_XTLV_INVALID		0

#define EWPF_MAX_IDX_TYPE		4
#define EWPF_IDX_TYPE_SLICE		1
#define EWPF_IDX_TYPE_IFACE		2
#define EWPF_IDX_TYPE_EVENT		3
#define EWPF_IDX_TYPE_KEY_INFO		4

#define EWPF_MAX_SLICE			2	/* MAX slice in dongle */
#define EWPF_SLICE_MAIN			0	/* SLICE ID for 5GHZ */
#define EWPF_SLICE_AUX			1	/* SLICE ID for 2GHZ */

#define EWPF_MAX_IFACE			2	/* MAX IFACE supported, 0: STA */
#define EWPF_MAX_EVENT			1	/* MAX EVENT counter supported */
#define EWPF_MAX_KEY_INFO			1	/* MAX KEY INFO counter supported */

#define EWPF_ARM_TO_MSEC		1
#define EWPF_NO_UNIT_CONV		1
#define EWPF_MSEC_TO_SEC		1000
#define EWPF_USEC_TO_MSEC		1000
#define EWPF_NSEC_TO_MSEC		1000000
#define EWPF_USEC_TO_SEC		1000000
#define EWPF_EPOCH				1000
#define EWPF_NONSEC_TO_SEC		1000000000
#define EWPF_REPORT_YEAR_MUL	10000
#define EWPF_REPORT_MON_MUL		100
#define EWPF_REPORT_HOUR_MUL	10000
#define EWPF_REPORT_MIN_MUL		100
#define EWPF_REPORT_MINUTES		60
#define EWPF_REPORT_YEAR_BASE	1900

#define EWPF_NO_ABS		FALSE
#define EWPF_NEED_ABS		TRUE

#define EWPF_MAX_INFO_TYPE	5
#define EWPF_INFO_VER		0
#define EWPF_INFO_TYPE		1
#define EWPF_INFO_ECNT		2
#define EWPF_INFO_IOVAR		3
#define EWPF_INFO_CPLOG		4
#define EWPF_INFO_DHDSTAT	5

#define EWPF_UPDATE_ARM_CYCLE_OFFSET	1

/* EWPF element of slice type */
typedef struct {
	uint32 armcycle; /* dongle arm cycle for this record */
	union {
		wl_periodic_compact_cntrs_v1_t compact_cntr_v1;
		wl_periodic_compact_cntrs_v2_t compact_cntr_v2;
		wl_periodic_compact_cntrs_v3_t compact_cntr_v3;
	};
	evt_hist_compact_toss_stats_v1_t hist_tx_toss_stat;
	evt_hist_compact_toss_stats_v1_t hist_rx_toss_stat;
	wlc_btc_stats_v4_t btc_stat;
	wl_compact_he_cnt_wlc_v2_t compact_he_cnt;
} EWPF_slc_elem_t;

/* EWPF element for interface type */
typedef struct {
	uint32 armcycle; /* dongle arm cycle for this record */
	wl_if_stats_t if_stat;
	wl_lqm_t lqm;
	wl_if_infra_stats_t infra;
	wl_if_mgt_stats_t mgmt_stat;
	wl_if_state_compact_t if_comp_stat;
	wl_adps_dump_summary_v2_t adps_dump_summary;
	wl_adps_energy_gain_v1_t adps_energy_gain;
	wl_roam_stats_v1_t roam_stat;
} EWPF_ifc_elem_t;

typedef struct {
	uint32 first_armcycle; /* first dongle arm cycle for this record */
	uint32 updated_armcycle; /* last updated dongle arm cycle for this record */
	wl_event_based_statistics_v4_t event_stat;
} EWPF_event_elem_t;

typedef struct {
	uint32 first_armcycle; /* first dongle arm cycle for this record */
	uint32 updated_armcycle; /* last updaated dongle arm cycle for this record */
	key_update_info_v1_t key_update_info;
} EWPF_key_info_elem_t;

typedef struct {
	uint32 first_armcycle; /* first dongle arm cycle for this record */
	uint32 updated_armcycle; /* last updated dongle arm cycle for this record */
	wl_roam_stats_v1_t roam_stat;
} EWPF_roam_stats_event_elem_t;

typedef struct {
	int enabled;			/* enabled/disabled */
	dhd_pub_t *dhdp;
	uint32 tmp_armcycle;	/* global ARM CYCLE for TAG */
	int idx_type;			/* 0 : SLICE, 1: IFACE */
	int xtlv_idx;			/* Slice/Interface index : global for TAG */
	void *s_ring[EWPF_MAX_SLICE];
	void *i_ring[EWPF_MAX_IFACE];
	void *e_ring[EWPF_MAX_EVENT];
	void *k_ring[EWPF_MAX_KEY_INFO];

	/* used by Report module */
	uint8 last_bssid[ETHER_ADDR_LEN];	/* BSSID of last conencted/request */
	int last_channel;
	uint32 last_armcycle;	/* ARM CYCLE prior last connection */
} EWP_filter_t;

/* status gathering functions : XTLV callback functions */
typedef int (*EWPF_filter_cb)(void *ctx, const uint8 *data, uint16 type, uint16 len);
static int evt_xtlv_print_cb(void *ctx, const uint8 *data, uint16 type, uint16 len);
static int evt_xtlv_copy_cb(void *ctx, const uint8 *data, uint16 type, uint16 len);
static int evt_xtlv_idx_cb(void *ctx, const uint8 *data, uint16 type, uint16 len);
static int evt_xtlv_type_cb(void *ctx, const uint8 *data, uint16 type, uint16 len);
static int filter_main_cb(void *ctx, const uint8 *data, uint16 type, uint16 len);
static int evt_xtlv_roam_cb(void *ctx, const uint8 *data, uint16 type, uint16 len);

/* ========= Event Handler functions and its callbacks: ============= */
typedef struct _EWPF_tbl {
	uint16 xtlv_id; /* XTLV ID, to handle */
	EWPF_filter_cb cb_func; /* specific call back function, usually for structre */
	int idx_type;			/* structure specific info: belonged type */
	int	max_idx;			/* structure specific info: ALLOWED MAX IDX */
	uint32 offset; /* offset of structure in EWPF_elem-t, valid if cb is not null */
	uint32 member_length;	/* MAX length of reserved for this structure */
	struct _EWPF_tbl *tbl; /* sub table if XTLV map to XLTV */
} EWPF_tbl_t;

/* Context structre for XTLV callback */
typedef struct {
	dhd_pub_t *dhdp;
	EWPF_tbl_t *tbl;
} EWPF_ctx_t;

#define SLICE_INFO(a) EWPF_IDX_TYPE_SLICE, EWPF_MAX_SLICE, OFFSETOF(EWPF_slc_elem_t, a), \
	sizeof(((EWPF_slc_elem_t *)NULL)->a)
#define IFACE_INFO(a) EWPF_IDX_TYPE_IFACE, EWPF_MAX_IFACE, OFFSETOF(EWPF_ifc_elem_t, a), \
	sizeof(((EWPF_ifc_elem_t *)NULL)->a)
#define EVENT_INFO(a) EWPF_IDX_TYPE_EVENT, EWPF_MAX_EVENT, OFFSETOF(EWPF_event_elem_t, a), \
		sizeof(((EWPF_event_elem_t *)NULL)->a)
#define KEY_INFO(a) EWPF_IDX_TYPE_KEY_INFO, EWPF_MAX_KEY_INFO, OFFSETOF(EWPF_key_info_elem_t, a), \
		sizeof(((EWPF_key_info_elem_t *)NULL)->a)

#define SLICE_U_SIZE(a) sizeof(((EWPF_slc_elem_t *)NULL)->a)
#define SLICE_INFO_UNION(a) EWPF_IDX_TYPE_SLICE, EWPF_MAX_SLICE, OFFSETOF(EWPF_slc_elem_t, a)
#define NONE_INFO(a) 0, 0, a, 0
/* XTLV TBL for WL_SLICESTATS_XTLV_PERIODIC_STATE */
static EWPF_tbl_t EWPF_periodic[] =
{
	{
		WL_STATE_COMPACT_COUNTERS,
		evt_xtlv_copy_cb,
		SLICE_INFO(compact_cntr_v3),
		NULL
	},
	{
		WL_STATE_COMPACT_HE_COUNTERS,
		evt_xtlv_copy_cb,
		SLICE_INFO(compact_he_cnt),
		NULL
	},
	{EWPF_XTLV_INVALID, NULL, NONE_INFO(0), NULL}
};

static EWPF_tbl_t EWPF_if_periodic[] =
{
	{
		WL_STATE_IF_COMPACT_STATE,
		evt_xtlv_copy_cb,
		IFACE_INFO(if_comp_stat),
		NULL
	},
	{
		WL_STATE_IF_ADPS_STATE,
		evt_xtlv_copy_cb,
		IFACE_INFO(adps_dump_summary),
		NULL
	},
	{
		WL_STATE_IF_ADPS_ENERGY_GAIN,
		evt_xtlv_copy_cb,
		IFACE_INFO(adps_energy_gain),
		NULL
	},
	{EWPF_XTLV_INVALID, NULL, NONE_INFO(0), NULL}
};

static EWPF_tbl_t EWPF_roam[] =
{
	{
		WL_IFSTATS_XTLV_ROAM_STATS_EVENT,
		evt_xtlv_print_cb,
		NONE_INFO(0),
		NULL
	},
	{
		WL_IFSTATS_XTLV_ROAM_STATS_PERIODIC,
		evt_xtlv_copy_cb,
		IFACE_INFO(roam_stat),
		NULL
	},
	{EWPF_XTLV_INVALID, NULL, NONE_INFO(0), NULL}
};

/* XTLV TBL for EVENT_LOG_TAG_STATS */
static EWPF_tbl_t EWPF_main[] =
{
	/* MAIN XTLV */
	{
		WL_IFSTATS_XTLV_WL_SLICE,
		evt_xtlv_type_cb,
		NONE_INFO(0),
		EWPF_main
	},
	{
		WL_IFSTATS_XTLV_IF,
		evt_xtlv_type_cb,
		NONE_INFO(0),
		EWPF_main
	},
	/* ID XTLVs */
	{
		WL_IFSTATS_XTLV_SLICE_INDEX,
		evt_xtlv_idx_cb,
		NONE_INFO(0),
		NULL
	},
	{
		WL_IFSTATS_XTLV_IF_INDEX,
		evt_xtlv_idx_cb,
		NONE_INFO(0),
		NULL
	},
	/* NORMAL XTLVS */
	{
		WL_SLICESTATS_XTLV_PERIODIC_STATE,
		NULL,
		NONE_INFO(0),
		EWPF_periodic
	},
	{
		WL_IFSTATS_XTLV_IF_LQM,
		evt_xtlv_copy_cb,
		IFACE_INFO(lqm),
		NULL
	},
	{
		WL_IFSTATS_XTLV_GENERIC,
		evt_xtlv_copy_cb,
		IFACE_INFO(if_stat),
		NULL
	},
	{
		WL_IFSTATS_XTLV_MGT_CNT,
		evt_xtlv_copy_cb,
		IFACE_INFO(mgmt_stat),
		NULL
	},
	{
		WL_IFSTATS_XTLV_IF_PERIODIC_STATE,
		NULL,
		NONE_INFO(0),
		EWPF_if_periodic
	},
	{
		WL_IFSTATS_XTLV_INFRA_SPECIFIC,
		evt_xtlv_copy_cb,
		IFACE_INFO(infra),
		NULL
	},
	{
		WL_SLICESTATS_XTLV_HIST_TX_STATS,
		evt_xtlv_copy_cb,
		SLICE_INFO(hist_tx_toss_stat),
		NULL
	},
	{
		WL_SLICESTATS_XTLV_HIST_RX_STATS,
		evt_xtlv_copy_cb,
		SLICE_INFO(hist_rx_toss_stat),
		NULL
	},
	{
		WL_IFSTATS_XTLV_WL_SLICE_BTCOEX,
		evt_xtlv_copy_cb,
		SLICE_INFO(btc_stat),
		NULL
	},
	{
		WL_IFSTATS_XTLV_IF_EVENT_STATS,
		evt_xtlv_copy_cb,
		EVENT_INFO(event_stat),
		NULL
	},
	{
		WL_IFSTATS_XTLV_IF_EVENT_STATS,
		evt_xtlv_print_cb,
		NONE_INFO(0),
		NULL
	},
	{
		WL_IFSTATS_XTLV_KEY_PLUMB_INFO,
		evt_xtlv_copy_cb,
		KEY_INFO(key_update_info),
		NULL
	},
	{
		WL_IFSTATS_XTLV_ROAM_STATS_EVENT,
		evt_xtlv_roam_cb,
		NONE_INFO(0),
		EWPF_roam
	},
	{
		WL_IFSTATS_XTLV_ROAM_STATS_PERIODIC,
		evt_xtlv_roam_cb,
		IFACE_INFO(roam_stat),
		EWPF_roam
	},

	{EWPF_XTLV_INVALID, NULL, NONE_INFO(0), NULL}
};

#if defined(DHD_EWPR_VER2) && defined(DHD_STATUS_LOGGING)

#define EWP_DHD_STAT_SIZE 2

uint8
dhd_statlog_filter[] =
{
	ST(WLAN_POWER_ON),	/* Wi-Fi Power on */
	ST(WLAN_POWER_OFF),	/* Wi-Fi Power off */
	ST(ASSOC_START),	/* connect to the AP triggered by upper layer */
	ST(AUTH_DONE),		/* complete to authenticate with the AP */
	ST(ASSOC_REQ),		/* send or receive Assoc Req */
	ST(ASSOC_RESP),		/* send or receive Assoc Resp */
	ST(ASSOC_DONE),		/* complete to disconnect to the associated AP */
	ST(DISASSOC_START),	/* disconnect to the associated AP by upper layer */
	ST(DISASSOC_INT_START),	/* initiate the disassoc by DHD */
	ST(DISASSOC_DONE),	/* complete to disconnect to the associated AP */
	ST(DISASSOC),		/* send or receive Disassoc */
	ST(DEAUTH),		/* send or receive Deauth */
	ST(LINKDOWN),		/* receive the link down event */
	ST(REASSOC_START),	/* reassoc the candidate AP */
	ST(REASSOC_INFORM),	/* inform reassoc completion to upper layer */
	ST(REASSOC_DONE),	/* complete to reassoc */
	ST(EAPOL_M1),		/* send or receive the EAPOL M1 */
	ST(EAPOL_M2),		/* send or receive the EAPOL M2 */
	ST(EAPOL_M3),		/* send or receive the EAPOL M3 */
	ST(EAPOL_M4),		/* send or receive the EAPOL M4 */
	ST(EAPOL_GROUPKEY_M1),	/* send or receive the EAPOL Group key handshake M1 */
	ST(EAPOL_GROUPKEY_M2),	/* send or receive the EAPOL Group key handshake M2 */
	ST(EAP_REQ_IDENTITY),	/* send or receive the EAP REQ IDENTITY */
	ST(EAP_RESP_IDENTITY),	/* send or receive the EAP RESP IDENTITY */
	ST(EAP_REQ_TLS),	/* send or receive the EAP REQ TLS */
	ST(EAP_RESP_TLS),	/* send or receive the EAP RESP TLS */
	ST(EAP_REQ_LEAP),	/* send or receive the EAP REQ LEAP */
	ST(EAP_RESP_LEAP),	/* send or receive the EAP RESP LEAP */
	ST(EAP_REQ_TTLS),	/* send or receive the EAP REQ TTLS */
	ST(EAP_RESP_TTLS),	/* send or receive the EAP RESP TTLS */
	ST(EAP_REQ_AKA),	/* send or receive the EAP REQ AKA */
	ST(EAP_RESP_AKA),	/* send or receive the EAP RESP AKA */
	ST(EAP_REQ_PEAP),	/* send or receive the EAP REQ PEAP */
	ST(EAP_RESP_PEAP),	/* send or receive the EAP RESP PEAP */
	ST(EAP_REQ_FAST),	/* send or receive the EAP REQ FAST */
	ST(EAP_RESP_FAST),	/* send or receive the EAP RESP FAST */
	ST(EAP_REQ_PSK),	/* send or receive the EAP REQ PSK */
	ST(EAP_RESP_PSK),	/* send or receive the EAP RESP PSK */
	ST(EAP_REQ_AKAP),	/* send or receive the EAP REQ AKAP */
	ST(EAP_RESP_AKAP),	/* send or receive the EAP RESP AKAP */
	ST(EAP_SUCCESS),	/* send or receive the EAP SUCCESS */
	ST(EAP_FAILURE),	/* send or receive the EAP FAILURE */
	ST(EAPOL_START),	/* send or receive the EAPOL-START */
	ST(WSC_START),		/* send or receive the WSC START */
	ST(WSC_DONE),		/* send or receive the WSC DONE */
	ST(WPS_M1),		/* send or receive the WPS M1 */
	ST(WPS_M2),		/* send or receive the WPS M2 */
	ST(WPS_M3),		/* send or receive the WPS M3 */
	ST(WPS_M4),		/* send or receive the WPS M4 */
	ST(WPS_M5),		/* send or receive the WPS M5 */
	ST(WPS_M6),		/* send or receive the WPS M6 */
	ST(WPS_M7),		/* send or receive the WPS M7 */
	ST(WPS_M8),		/* send or receive the WPS M8 */
	ST(8021X_OTHER),	/* send or receive the other 8021X frames */
	ST(INSTALL_KEY),	/* install the key */
	ST(DELETE_KEY),		/* remove the key */
	ST(INSTALL_PMKSA),	/* install PMKID information */
	ST(INSTALL_OKC_PMK),	/* install PMKID information for OKC */
	ST(DHCP_DISCOVER),	/* send or recv DHCP Discover */
	ST(DHCP_OFFER),		/* send or recv DHCP Offer */
	ST(DHCP_REQUEST),	/* send or recv DHCP Request */
	ST(DHCP_DECLINE),	/* send or recv DHCP Decline */
	ST(DHCP_ACK),		/* send or recv DHCP ACK */
	ST(DHCP_NAK),		/* send or recv DHCP NACK */
	ST(DHCP_RELEASE),	/* send or recv DHCP Release */
	ST(DHCP_INFORM),	/* send or recv DHCP Inform */
	ST(REASSOC_SUCCESS),	/* reassociation success */
	ST(REASSOC_FAILURE),	/* reassociation failure */
	ST(AUTH_TIMEOUT),	/* authentication timeout */
	ST(AUTH_FAIL),		/* authentication failure */
	ST(AUTH_NO_ACK),	/* authentication failure due to no ACK */
	ST(AUTH_OTHERS),	/* authentication failure with other status */
	ST(ASSOC_TIMEOUT),	/* association timeout */
	ST(ASSOC_FAIL),		/* association failure */
	ST(ASSOC_NO_ACK),	/* association failure due to no ACK */
	ST(ASSOC_ABORT),	/* association abort */
	ST(ASSOC_UNSOLICITED),	/* association unsolicited */
	ST(ASSOC_NO_NETWORKS),	/* association failure due to no networks */
	ST(ASSOC_OTHERS),	/* association failure due to no networks */
	ST(REASSOC_DONE_OTHERS)	/* complete to reassoc with other reason */
};
#endif /* DHD_EWPR_VER2 && DHD_STATUS_LOGGING  */

/* ========= Module functions : exposed to others ============= */
int
dhd_event_log_filter_init(dhd_pub_t *dhdp, uint8 *buf, uint32 buf_size)
{

	EWP_filter_t *filter;
	int idx;
	uint32 req_size;
	uint32 s_ring_size; /* slice ring */
	uint32 i_ring_size; /* interface ring */
	uint32 e_ring_size; /* event counter ring */
	uint32 k_ring_size; /* key info ring */
	uint8 *buf_ptr = buf;
	EWPF_ctx_t ctx;
	wl_event_based_statistics_v4_t dummy_event_stat;
	key_update_info_v1_t dummy_key_update_info;
#if defined(DHD_EWPR_VER2) && defined(DHD_STATUS_LOGGING)
	stat_bdmask_req_t req;
#endif /* DHD_EWPR_VER2 && DHD_STATUS_LOGGING */

	DHD_FILTER_ERR(("STARTED\n"));

	if (!dhdp || !buf) {
		DHD_FILTER_ERR(("INVALID PTR: dhdp:%p buf:%p\n", dhdp, buf));
		return BCME_ERROR;
	}

	i_ring_size = s_ring_size = e_ring_size = k_ring_size = dhd_ring_get_hdr_size();
	s_ring_size += ((uint32)sizeof(EWPF_slc_elem_t)) * EWPF_REPORT_MAX_DATA;
	i_ring_size += ((uint32)sizeof(EWPF_ifc_elem_t)) * EWPF_REPORT_MAX_DATA;
	e_ring_size += ((uint32)sizeof(EWPF_event_elem_t)) * EWPF_REPORT_MAX_DATA;
	k_ring_size += ((uint32)sizeof(EWPF_key_info_elem_t)) * EWPF_REPORT_MAX_DATA;

	req_size = s_ring_size * EWPF_MAX_SLICE + i_ring_size * EWPF_MAX_IFACE +
		e_ring_size * EWPF_MAX_EVENT + k_ring_size * EWPF_MAX_KEY_INFO;
	req_size += (uint32)sizeof(EWP_filter_t);

	if (buf_size < req_size) {
		DHD_FILTER_ERR(("BUF SIZE IS TO SHORT: req:%d buf_size:%d\n",
			req_size, buf_size));
		return BCME_ERROR;
	}

	BCM_REFERENCE(dhdp);
	filter = (EWP_filter_t *)buf;
	buf_ptr += sizeof(EWP_filter_t);

	/* initialize control block */
	memset(filter, 0, sizeof(EWP_filter_t));

	filter->idx_type = EWPF_INVALID;
	filter->xtlv_idx = EWPF_INVALID;
	filter->tmp_armcycle = 0;

	for (idx = 0; idx < EWPF_MAX_SLICE; idx++) {
		filter->s_ring[idx] = dhd_ring_init(dhdp, buf_ptr, s_ring_size,
			sizeof(EWPF_slc_elem_t), EWPF_REPORT_MAX_DATA,
			DHD_RING_TYPE_FIXED);
		if (!filter->s_ring[idx]) {
			DHD_FILTER_ERR(("FAIL TO INIT SLICE RING: %d\n", idx));
			return BCME_ERROR;
		}
		buf_ptr += s_ring_size;
	}

	for (idx = 0; idx < EWPF_MAX_IFACE; idx++) {
		filter->i_ring[idx] = dhd_ring_init(dhdp, buf_ptr, i_ring_size,
			sizeof(EWPF_ifc_elem_t), EWPF_REPORT_MAX_DATA,
			DHD_RING_TYPE_FIXED);
		if (!filter->i_ring[idx]) {
			DHD_FILTER_ERR(("FAIL TO INIT INTERFACE RING: %d\n", idx));
			return BCME_ERROR;
		}
		buf_ptr += i_ring_size;
	}

	for (idx = 0; idx < EWPF_MAX_EVENT; idx++) {
		filter->e_ring[idx] = dhd_ring_init(dhdp, buf_ptr, e_ring_size,
			sizeof(EWPF_event_elem_t), EWPF_REPORT_MAX_DATA,
			DHD_RING_TYPE_FIXED);
		if (!filter->e_ring[idx]) {
			DHD_FILTER_ERR(("FAIL TO INIT INTERFACE RING: %d\n", idx));
			return BCME_ERROR;
		}
		buf_ptr += e_ring_size;
	}

	for (idx = 0; idx < EWPF_MAX_KEY_INFO; idx++) {
		filter->k_ring[idx] = dhd_ring_init(dhdp, buf_ptr, k_ring_size,
			sizeof(EWPF_key_info_elem_t), EWPF_REPORT_MAX_DATA,
			DHD_RING_TYPE_FIXED);
		if (!filter->k_ring[idx]) {
			DHD_FILTER_ERR(("FAIL TO INIT INTERFACE RING: %d\n", idx));
			return BCME_ERROR;
		}
		buf_ptr += k_ring_size;
	}

	dhdp->event_log_filter = filter;
	filter->dhdp = dhdp;
	filter->enabled = TRUE;

	/*
	 * put dummy element of event based encounters to prevent error
	 * in case of no event happened when data collection is triggered
	 */
	ctx.dhdp = dhdp;
	ctx.tbl = EWPF_main;
	memset(&dummy_event_stat, 0x00, sizeof(dummy_event_stat));
	evt_xtlv_copy_cb(&ctx, (uint8 *)&dummy_event_stat, WL_IFSTATS_XTLV_IF_EVENT_STATS,
		sizeof(wl_event_based_statistics_v4_t));

	memset(&dummy_key_update_info, 0x00, sizeof(dummy_key_update_info));
	evt_xtlv_copy_cb(&ctx, (uint8 *)&dummy_key_update_info, WL_IFSTATS_XTLV_KEY_PLUMB_INFO,
		sizeof(key_update_info_v1_t));

#if defined(DHD_EWPR_VER2) && defined(DHD_STATUS_LOGGING)
	/* create status filter for bigdata logging */
	req.req_buf = dhd_statlog_filter;
	req.req_buf_len = sizeof(dhd_statlog_filter);
	dhd_statlog_generate_bdmask(dhdp, &req);
#endif /* DHD_EWPR_VER2 && DHD_STATUS_LOGGING */

	return BCME_OK;
}

void
dhd_event_log_filter_deinit(dhd_pub_t *dhdp)
{
	EWP_filter_t *filter;
	int idx;

	if (!dhdp) {
		return;
	}

	if (dhdp->event_log_filter) {
		filter = (EWP_filter_t *)dhdp->event_log_filter;
		for (idx = 0; idx < EWPF_MAX_SLICE; idx ++) {
			dhd_ring_deinit(dhdp, filter->s_ring[idx]);
		}
		for (idx = 0; idx < EWPF_MAX_IFACE; idx ++) {
			dhd_ring_deinit(dhdp, filter->i_ring[idx]);
		}
		for (idx = 0; idx < EWPF_MAX_EVENT; idx ++) {
			dhd_ring_deinit(dhdp, filter->e_ring[idx]);
		}
		for (idx = 0; idx < EWPF_MAX_KEY_INFO; idx ++) {
			dhd_ring_deinit(dhdp, filter->k_ring[idx]);
		}
		dhdp->event_log_filter = NULL;
	}
}

void
dhd_event_log_filter_notify_connect_request(dhd_pub_t *dhdp, uint8 *bssid, int channel)
{
	EWP_filter_t *filter;
	void *last_elem;

	if (!dhdp || !dhdp->event_log_filter) {
		return;
	}

	filter = (EWP_filter_t *)dhdp->event_log_filter;
	if (filter->enabled != TRUE) {
		DHD_FILTER_ERR(("EWP Filter is not enabled\n"));
		return;
	}

	memcpy(filter->last_bssid, bssid, ETHER_ADDR_LEN);
	filter->last_channel = channel;

	/* Refer STA interface */
	last_elem = dhd_ring_get_last(filter->i_ring[0]);
	if (last_elem == NULL) {
		filter->last_armcycle = 0;
	} else {
		/* EXCLUDE before connect start */
		filter->last_armcycle = *(uint32 *)last_elem + EWPF_EPOCH + 1;
	}
}

void
dhd_event_log_filter_notify_connect_done(dhd_pub_t *dhdp, uint8 *bssid, int roam)
{
	EWP_filter_t *filter;
	void *last_elem;
	int channel;
	char buf[EWPF_DEBUG_BUF_LEN];
	int ret;
	uint32 armcycle;
	struct channel_info *ci;

	if (!dhdp || !dhdp->event_log_filter) {
		return;
	}

	filter = (EWP_filter_t *)dhdp->event_log_filter;
	if (filter->enabled != TRUE) {
		DHD_FILTER_ERR(("EWP Filter is not enabled\n"));
		return;
	}

	/* GET CHANNEL */
	*(uint32 *)buf = htod32(EWPF_DEBUG_BUF_LEN);
	ret = dhd_wl_ioctl_cmd(dhdp, WLC_GET_CHANNEL, buf, EWPF_DEBUG_BUF_LEN, FALSE, 0);
	if (ret != BCME_OK) {
		DHD_FILTER_ERR(("FAIL TO GET BSS INFO: %d\n", ret));
		return;
	}

	ci = (struct channel_info *)(buf + sizeof(uint32));
	channel = dtoh32(ci->hw_channel);
	DHD_FILTER_TRACE(("CHANNEL:prev %d new:%d\n", filter->last_channel, channel));

	memcpy(filter->last_bssid, bssid, ETHER_ADDR_LEN);
	filter->last_channel = channel;
	if (roam == FALSE) {
		return;
	}

	/* update connect time for roam */
	/* Refer STA interface */
	last_elem = dhd_ring_get_last(filter->i_ring[0]);
	if (last_elem == NULL) {
		armcycle = 0;
	} else {
		/* EXCLUDE before roam done */
		armcycle = *(uint32 *)last_elem + EWPF_EPOCH + 1;
	}

	filter->last_armcycle = armcycle;
}

static int
evt_xtlv_print_cb(void *ctx, const uint8 *data, uint16 type, uint16 len)
{
	EWPF_ctx_t *cur_ctx = (EWPF_ctx_t *)ctx;
	EWP_filter_t *filter = (EWP_filter_t *)cur_ctx->dhdp->event_log_filter;
	uint32 armcycle = 0;
	uint8 bssid[ETHER_ADDR_LEN];
	uint32 initial_assoc_time = 0;
	uint32 prev_roam_time = 0;
	uint32 last_roam_event_type = 0;
	uint32 last_roam_event_status = 0;
	uint32 last_roam_event_reason = 0;
	wl_wips_event_info_t wips_event;
	bzero(&wips_event, sizeof(wips_event));

	DHD_FILTER_TRACE(("%s type:%d %x len:%d %x\n", __FUNCTION__, type, type, len, len));

	/* get current armcycle */
	if (filter) {
		armcycle = filter->tmp_armcycle;
	}
	if (type == WL_IFSTATS_XTLV_IF_EVENT_STATS) {
		wl_event_based_statistics_v1_t *elem;

		elem = (wl_event_based_statistics_v1_t *)(uintptr_t)data;
		if (elem->txdeauthivalclass > 0) {
			memcpy(bssid, &elem->BSSID, ETHER_ADDR_LEN);
			DHD_ERROR(("DHD STA sent DEAUTH frame with invalid class : %d times"
				", BSSID("MACDBG")\n", elem->txdeauthivalclass, MAC2STRDBG(bssid)));
		}
		if (elem->version == WL_EVENT_STATISTICS_VER_2) {
			wl_event_based_statistics_v2_t *elem_v2;

			elem_v2 = (wl_event_based_statistics_v2_t *)(uintptr_t)data;
			memcpy(&wips_event.bssid, &elem_v2->last_deauth, ETHER_ADDR_LEN);
			wips_event.misdeauth = elem_v2->misdeauth;
			wips_event.current_RSSI = elem_v2->cur_rssi;
			wips_event.deauth_RSSI = elem_v2->deauth_rssi;
			wips_event.timestamp = elem_v2->timestamp;
		} else if (elem->version == WL_EVENT_STATISTICS_VER_3) {
			wl_event_based_statistics_v3_t *elem_v3;

			elem_v3 = (wl_event_based_statistics_v3_t *)(uintptr_t)data;
			memcpy(&wips_event.bssid, &elem_v3->last_deauth, ETHER_ADDR_LEN);
			wips_event.misdeauth = elem_v3->misdeauth;
			wips_event.current_RSSI = elem_v3->cur_rssi;
			wips_event.deauth_RSSI = elem_v3->deauth_rssi;
			wips_event.timestamp = elem_v3->timestamp;
			/* roam statistics */
			initial_assoc_time = elem_v3->initial_assoc_time;
			prev_roam_time = elem_v3->prev_roam_time;
			last_roam_event_type = elem_v3->last_roam_event_type;
			last_roam_event_status = elem_v3->last_roam_event_status;
			last_roam_event_reason = elem_v3->last_roam_event_reason;
		} else if (elem->version == WL_EVENT_STATISTICS_VER_4) {
			wl_event_based_statistics_v4_t *elem_v4;

			elem_v4 = (wl_event_based_statistics_v4_t *)(uintptr_t)data;
			memcpy(&wips_event.bssid, &elem_v4->last_deauth, ETHER_ADDR_LEN);
			wips_event.misdeauth = elem_v4->misdeauth;
			wips_event.current_RSSI = elem_v4->cur_rssi;
			wips_event.deauth_RSSI = elem_v4->deauth_rssi;
			wips_event.timestamp = elem_v4->timestamp;
		}
		if (wips_event.misdeauth > 1) {
			DHD_ERROR(("WIPS attack!! cnt=%d, curRSSI=%d, deauthRSSI=%d "
				", time=%d, MAC="MACDBG"\n",
				wips_event.misdeauth, wips_event.current_RSSI,
				wips_event.deauth_RSSI,	wips_event.timestamp,
				MAC2STRDBG(&wips_event.bssid)));
#if defined(WL_CFG80211) && defined(WL_WIPSEVT)
			wl_cfg80211_wips_event_ext(&wips_event);
#endif /* WL_CFG80211 && WL_WIPSEVT */
		}
	} else if (type == WL_IFSTATS_XTLV_ROAM_STATS_EVENT) {
		wl_roam_stats_v1_t *roam_elem;
		roam_elem = (wl_roam_stats_v1_t *)(uintptr_t)data;
		if (roam_elem->version == WL_ROAM_STATS_VER_1) {
			wl_roam_stats_v1_t *roam_elem_v1;

			roam_elem_v1 = (wl_roam_stats_v1_t *)(uintptr_t)data;
			/* roam statistics */
			initial_assoc_time = roam_elem_v1->initial_assoc_time;
			prev_roam_time = roam_elem_v1->prev_roam_time;
			last_roam_event_type = roam_elem_v1->last_roam_event_type;
			last_roam_event_status = roam_elem_v1->last_roam_event_status;
			last_roam_event_reason = roam_elem_v1->last_roam_event_reason;
		}
	} else {
		DHD_FILTER_ERR(("%s TYPE(%d) IS NOT SUPPORTED TO PRINT\n",
			__FUNCTION__, type));
		return BCME_ERROR;
	}
	if (initial_assoc_time > 0 && prev_roam_time > 0) {
		DHD_ERROR(("Last roam event before disconnection : "
			"current armcycle %d, initial assoc time %d, "
			"last event time %d, type %d, status %d, reason %d\n",
			armcycle, initial_assoc_time, prev_roam_time,
			last_roam_event_type, last_roam_event_status,
			last_roam_event_reason));
	}

	return BCME_OK;
}

#ifdef BCM_SDC
static int
evt_get_last_toss_hist(uint8 *ptr, const uint8 *data, uint16 len)
{
	bcm_xtlv_t *bcm_xtlv_desc = (bcm_xtlv_t *)data;
	wl_hist_compact_toss_stats_v2_t *ewp_stats;
	evt_hist_compact_toss_stats_v1_t bidata_stats;
	int16 max_rcidx = EWPF_INVALID, secnd_rcidx = EWPF_INVALID;
	uint16 cur_rnidx = 0, prev_rnidx = 0;
	uint16 max_rccnt = 0, cur_rccnt = 0;
	uint16 idx;

	if (!ptr || !data) {
		return BCME_ERROR;
	}

	if (bcm_xtlv_desc->len != sizeof(wl_hist_compact_toss_stats_v2_t)) {
		DHD_FILTER_ERR(("%s : size is not matched  %d\n", __FUNCTION__,
			bcm_xtlv_desc->len));
		return BCME_ERROR;
	}

	ewp_stats = (wl_hist_compact_toss_stats_v2_t *)(&bcm_xtlv_desc->data[0]);
	if (ewp_stats->htr_type == WL_STATE_HIST_TX_TOSS_REASONS) {
		if (ewp_stats->version != WL_HIST_COMPACT_TOSS_STATS_TX_VER_2) {
			DHD_FILTER_ERR(("%s : unsupported version %d (type: %d)\n",
				__FUNCTION__, ewp_stats->version, ewp_stats->htr_type));
			return BCME_ERROR;
		}
	} else if (ewp_stats->htr_type == WL_STATE_HIST_RX_TOSS_REASONS) {
		if (ewp_stats->version != WL_HIST_COMPACT_TOSS_STATS_RX_VER_2) {
			DHD_FILTER_ERR(("%s : unsupported version %d (type: %d)\n",
				__FUNCTION__, ewp_stats->version, ewp_stats->htr_type));
			return BCME_ERROR;
		}
	} else {
		DHD_FILTER_ERR(("%s : unsupported type %d\n", __FUNCTION__,
			ewp_stats->htr_type));
		return BCME_ERROR;
	}
	/*
	 * htr_rnidx is pointing the next empty slot to be used
	 * Need to get previous index which is valid
	 */
	if (ewp_stats->htr_rnidx > 0) {
		cur_rnidx = ewp_stats->htr_rnidx - 1;
	} else {
		cur_rnidx = WLC_HIST_TOSS_LEN - 1;
	}
	if (cur_rnidx > 0) {
		prev_rnidx = cur_rnidx - 1;
	} else {
		prev_rnidx = WLC_HIST_TOSS_LEN - 1;
	}
	/*
	 * Need to get largest count of toss reasons
	 */
	for (idx = 0; idx < WLC_HIST_TOSS_LEN; idx ++) {
		cur_rccnt = (uint16)((ewp_stats->htr_rc[idx] &
			HIST_TOSS_RC_COUNT_MASK)>>HIST_TOSS_RC_COUNT_POS);
		DHD_FILTER_TRACE(("%s: idx %d htr_rc %04x cur_rccnt %d\n",
			__FUNCTION__, idx, ewp_stats->htr_rc[idx], cur_rccnt));
		if (ewp_stats->htr_rc_ts[idx] && max_rccnt < cur_rccnt) {
			max_rccnt = cur_rccnt;
			secnd_rcidx = max_rcidx;
			max_rcidx = idx;
			DHD_FILTER_TRACE(("%s: max_rcidx updated -"
				"max_rcidx %d secnd_rcidx %d\n",
				__FUNCTION__, max_rcidx, secnd_rcidx));
		}
	}

	memset(&bidata_stats, 0x00, sizeof(bidata_stats));
	bidata_stats.version = ewp_stats->version;
	bidata_stats.htr_type = ewp_stats->htr_type;
	bidata_stats.htr_num = ewp_stats->htr_num;
	bidata_stats.htr_rn_last = ewp_stats->htr_running[cur_rnidx];
	bidata_stats.htr_rn_ts_last = ewp_stats->htr_rn_ts[cur_rnidx];
	bidata_stats.htr_rn_prev = ewp_stats->htr_running[prev_rnidx];
	bidata_stats.htr_rn_ts_prev = ewp_stats->htr_rn_ts[prev_rnidx];
	if (max_rcidx != EWPF_INVALID) {
		bidata_stats.htr_rc_max = ewp_stats->htr_rc[max_rcidx];
		bidata_stats.htr_rc_ts_max = ewp_stats->htr_rc_ts[max_rcidx];
	}
	if (secnd_rcidx != EWPF_INVALID) {
		bidata_stats.htr_rc_secnd = ewp_stats->htr_rc[secnd_rcidx];
		bidata_stats.htr_rc_ts_secnd = ewp_stats->htr_rc_ts[secnd_rcidx];
	}
	DHD_FILTER_TRACE(("%s: ver %d type %d num %d "
		"htr_rn_last %d htr_rn_ts_last %d htr_rn_prev %d htr_rn_ts_prev %d "
		"htr_rc_max %d htr_rc_ts_max %d htr_rc_secnd %d htr_rc_ts_secnd %d\n",
		__FUNCTION__, bidata_stats.version,
		bidata_stats.htr_type, bidata_stats.htr_num,
		bidata_stats.htr_rn_last, bidata_stats.htr_rn_ts_last,
		bidata_stats.htr_rn_prev, bidata_stats.htr_rn_ts_prev,
		bidata_stats.htr_rc_max, bidata_stats.htr_rc_ts_max,
		bidata_stats.htr_rc_secnd, bidata_stats.htr_rc_ts_secnd));

	memcpy(ptr, &bidata_stats, sizeof(bidata_stats));

	return BCME_OK;
}
#endif /* BCM_SDC */

static int
evt_xtlv_copy_cb(void *ctx, const uint8 *data, uint16 type, uint16 len)
{
	EWPF_ctx_t *cur_ctx = (EWPF_ctx_t *)ctx;
	EWP_filter_t *filter = (EWP_filter_t *)cur_ctx->dhdp->event_log_filter;
	uint32 *armcycle;
	EWPF_tbl_t *tbl;
	void *ring;
	void *target;
	uint8 *ptr;
	int tbl_idx;
	uint32 elem_size;

	DHD_FILTER_TRACE(("%s type:%d %x len:%d %x\n", __FUNCTION__, type, type, len, len));

	for (tbl_idx = 0; ; tbl_idx++) {
		if (cur_ctx->tbl[tbl_idx].xtlv_id == EWPF_XTLV_INVALID) {
			DHD_FILTER_ERR(("%s NOT SUPPORTED TYPE(%d)\n", __FUNCTION__, type));
			return BCME_OK;
		}
		if (cur_ctx->tbl[tbl_idx].xtlv_id == type) {
			tbl = &cur_ctx->tbl[tbl_idx];
			break;
		}
	}

	/* Set index type and xtlv_idx for event stats and key plumb info */
	if (type == WL_IFSTATS_XTLV_IF_EVENT_STATS) {
		filter->idx_type = EWPF_IDX_TYPE_EVENT;
		filter->xtlv_idx = 0;
		DHD_FILTER_TRACE(("EVENT XTLV\n"));
	} else if (type == WL_IFSTATS_XTLV_KEY_PLUMB_INFO) {
		filter->idx_type = EWPF_IDX_TYPE_KEY_INFO;
		filter->xtlv_idx = 0;
		DHD_FILTER_TRACE(("KEY INFO XTLV\n"));
	}

	/* Check Validation */
	if (filter->idx_type == EWPF_INVALID ||
		filter->xtlv_idx == EWPF_INVALID ||
		filter->idx_type != tbl->idx_type ||
		filter->xtlv_idx >= tbl->max_idx) {
		DHD_FILTER_ERR(("XTLV VALIDATION FAILED: type:%x xtlv:%x idx:%d\n",
			filter->idx_type, tbl->xtlv_id, filter->xtlv_idx));
		return BCME_OK;
	}

	/* SET RING INFO */
	if (filter->idx_type == EWPF_IDX_TYPE_SLICE) {
		ring = filter->s_ring[filter->xtlv_idx];
		elem_size = sizeof(EWPF_slc_elem_t);
	} else if (filter->idx_type == EWPF_IDX_TYPE_IFACE) {
		ring = filter->i_ring[filter->xtlv_idx];
		elem_size = sizeof(EWPF_ifc_elem_t);
	} else if (filter->idx_type == EWPF_IDX_TYPE_EVENT) {
		DHD_FILTER_TRACE(("%s: EWPF_IDX_TYPE_EVENT FOUND\n",
		__FUNCTION__));
		ring = filter->e_ring[filter->xtlv_idx];
		elem_size = sizeof(EWPF_event_elem_t);
	} else if (filter->idx_type == EWPF_IDX_TYPE_KEY_INFO) {
		DHD_FILTER_TRACE(("%s: EWPF_IDX_TYPE_KEY_INFO FOUND\n",
		__FUNCTION__));
		ring = filter->k_ring[filter->xtlv_idx];
		elem_size = sizeof(EWPF_key_info_elem_t);
	} else {
		DHD_FILTER_TRACE(("%s unsupported idx_type:%d\n",
			__FUNCTION__, filter->idx_type));
		return BCME_OK;
	}

	/* Check armcycle epoch is changed */
	target = dhd_ring_get_last(ring);
	if (target != NULL) {
		armcycle = (uint32 *)target;
		if (*armcycle + EWPF_EPOCH <= filter->tmp_armcycle) {
			/* EPOCH is changed (longer than 1sec) */
			target = NULL;
		} else if (*armcycle - EWPF_EPOCH >= filter->tmp_armcycle) {
			/* dongle is rebooted */
			target = NULL;
		}
	}

	if (target == NULL) {
		/* Get new idx */
		target = dhd_ring_get_empty(ring);
		if (target == NULL) {
			/* no available slot due to oldest slot is locked */
			DHD_FILTER_ERR(("SKIP to logging xltv(%x) due to locking\n", type));
			return BCME_OK;
		}

		/* clean up target */
		armcycle = (uint32 *)target;
		memset(target, 0, elem_size);
		memcpy(armcycle, &filter->tmp_armcycle, sizeof(*armcycle));
	}

#ifdef EWPF_DEBUG
	DHD_FILTER_ERR(("idx:%d write_:%p %u %u\n",
		filter->xtlv_idx, target, *armcycle, filter->tmp_armcycle));
#endif

	/* Additionally put updated armcycle for event based EWP */
	if (filter->idx_type == EWPF_IDX_TYPE_EVENT ||
		filter->idx_type == EWPF_IDX_TYPE_KEY_INFO) {
		DHD_FILTER_TRACE(("%s: updated armcycle for event based EWP\n",
		__FUNCTION__));
		memcpy((uint32 *)(armcycle + EWPF_UPDATE_ARM_CYCLE_OFFSET),
			&filter->tmp_armcycle, sizeof(*armcycle));
	}

	ptr = (uint8 *)target;

#ifdef DHD_EWPR_VER2
	if (tbl->xtlv_id == WL_SLICESTATS_XTLV_HIST_TX_STATS ||
			tbl->xtlv_id == WL_SLICESTATS_XTLV_HIST_RX_STATS) {
#ifdef BCM_SDC
		int err;

		DHD_FILTER_TRACE(("TOSS_REASONS received (%d)\n", tbl->xtlv_id));

		err = evt_get_last_toss_hist(ptr + cur_ctx->tbl[tbl_idx].offset, data, len);
		if (err) {
			DHD_FILTER_ERR(("%s: get toss hist failed\n",
				__FUNCTION__));
			return BCME_ERROR;
		}
#else
		DHD_FILTER_ERR(("%s: Unabled to copy hist TX/RX stats, BCM_SDC must be included\n",
			__FUNCTION__));
#endif /* BCM_SDC */
	} else
#endif /* DHD_EWPR_VER2 */
	{
		/* XXX multiversion shall be use same structure of old version */
		if (len > cur_ctx->tbl[tbl_idx].member_length) {
			DHD_FILTER_TRACE(("data Length is too big to save: (alloc = %d), "
				"(data = %d)\n", cur_ctx->tbl[tbl_idx].member_length, len));
			len = cur_ctx->tbl[tbl_idx].member_length;
		}

		memcpy(ptr + cur_ctx->tbl[tbl_idx].offset, data, len);
	}
	return BCME_OK;
}

static int
evt_xtlv_idx_cb(void *ctx, const uint8 *data, uint16 type, uint16 len)
{
	EWPF_ctx_t *cur_ctx = (EWPF_ctx_t *)ctx;
	EWP_filter_t *filter = (EWP_filter_t *)cur_ctx->dhdp->event_log_filter;

	filter->xtlv_idx = data[0];

	if (filter->idx_type == EWPF_IDX_TYPE_SLICE) {
		if (type != WL_IFSTATS_XTLV_SLICE_INDEX ||
			filter->xtlv_idx >= EWPF_MAX_SLICE) {
			goto idx_fail;
		}
	} else if (filter->idx_type == EWPF_IDX_TYPE_IFACE) {
		if (type != WL_IFSTATS_XTLV_IF_INDEX ||
			filter->xtlv_idx >= EWPF_MAX_IFACE) {
			DHD_FILTER_ERR(("CHANGE IFACE TO 0 in FORCE\n"));
			return BCME_OK;
		}
	} else {
		goto idx_fail;
	}
	return BCME_OK;

idx_fail:
	DHD_FILTER_ERR(("UNEXPECTED IDX XTLV: filter_type:%d input_type%x idx:%d\n",
		filter->idx_type, type, filter->xtlv_idx));
	filter->idx_type = EWPF_INVALID;
	filter->xtlv_idx = EWPF_INVALID;
	return BCME_OK;
}

static int
evt_xtlv_type_cb(void *ctx, const uint8 *data, uint16 type, uint16 len)
{
	EWPF_ctx_t *cur_ctx = (EWPF_ctx_t *)ctx;
	EWP_filter_t *filter = (EWP_filter_t *)cur_ctx->dhdp->event_log_filter;

	if (type == WL_IFSTATS_XTLV_WL_SLICE) {
		filter->idx_type = EWPF_IDX_TYPE_SLICE;
		DHD_FILTER_TRACE(("SLICE XTLV\n"));
	} else if (type == WL_IFSTATS_XTLV_IF) {
		filter->idx_type = EWPF_IDX_TYPE_IFACE;
		DHD_FILTER_TRACE(("IFACE XTLV\n"));
	}

	bcm_unpack_xtlv_buf(ctx, data, len,
		BCM_XTLV_OPTION_ALIGN32, filter_main_cb);
	return BCME_OK;
}

static int
evt_xtlv_roam_cb(void *ctx, const uint8 *data, uint16 type, uint16 len)
{

	EWPF_ctx_t *cur_ctx = (EWPF_ctx_t *)ctx;
	EWPF_tbl_t *new_tbl = EWPF_roam;
	EWPF_ctx_t sub_ctx;
	int idx;

	for (idx = 0; ; idx++) {
		if (new_tbl[idx].xtlv_id == EWPF_XTLV_INVALID) {
			DHD_FILTER_TRACE(("%s NOT SUPPORTED TYPE(%d)\n", __FUNCTION__, type));
			return BCME_OK;
		}
		if (new_tbl[idx].xtlv_id == type) {
			break;
		}
	}

	/* MULTI version may not applied */
	if (len > sizeof(cur_ctx->dhdp->roam_evt)) {
		DHD_FILTER_ERR(("data length is too big :max= %d, cur=%d\n",
				(int)sizeof(cur_ctx->dhdp->roam_evt), len));
		len = sizeof(cur_ctx->dhdp->roam_evt);
	}

	/* save latest roam event to report via get_bss_info */
	(void)memcpy_s((char *)&cur_ctx->dhdp->roam_evt, sizeof(cur_ctx->dhdp->roam_evt),
			data, len);

	sub_ctx.dhdp = cur_ctx->dhdp;
	sub_ctx.tbl = new_tbl;
	new_tbl[idx].cb_func(&sub_ctx, data, type, len);
	return BCME_OK;
}

static int
filter_main_cb(void *ctx, const uint8 *data, uint16 type, uint16 len)
{
	EWPF_ctx_t *cur_ctx = (EWPF_ctx_t *)ctx;
	EWPF_ctx_t sub_ctx;
	int idx;
	int err = BCME_OK;

	DHD_FILTER_TRACE(("%s type:%x len:%d\n", __FUNCTION__, type, len));

	sub_ctx.dhdp = cur_ctx->dhdp;
	for (idx = 0; ; idx++) {
		if (cur_ctx->tbl[idx].xtlv_id == EWPF_XTLV_INVALID) {
			DHD_FILTER_TRACE(("%s NOT SUPPORTED TYPE(%d)\n", __FUNCTION__, type));
			return BCME_OK;
		}
		if (cur_ctx->tbl[idx].xtlv_id == type) {
			/* parse sub xtlv */
			if (cur_ctx->tbl[idx].cb_func == NULL) {
				sub_ctx.tbl = cur_ctx->tbl[idx].tbl;
				err = bcm_unpack_xtlv_buf(&sub_ctx, data, len,
						BCM_XTLV_OPTION_ALIGN32, filter_main_cb);
				return err;
			}

			/* handle for structure/variable */
			err = cur_ctx->tbl[idx].cb_func(ctx, data, type, len);
			if (err != BCME_OK) {
				return err;
			}
		}
	}

	return err;
}

void
dhd_event_log_filter_event_handler(dhd_pub_t *dhdp, prcd_event_log_hdr_t *plog_hdr, uint32 *data)
{
	int err;
	EWP_filter_t *filter;
	EWPF_ctx_t ctx;

	if (!dhdp->event_log_filter) {
		DHD_FILTER_ERR(("NO FILTER MODULE\n"));
		return;
	}

	if (!plog_hdr || !data) {
		/* XXX Validation check is done by caller */
		DHD_FILTER_ERR(("INVALID PARAMETER\n"));
		return;
	}

	filter = (EWP_filter_t *)dhdp->event_log_filter;
	if (filter->enabled != TRUE) {
		DHD_FILTER_ERR(("FITLER IS NOT STARTED\n"));
		return;
	}

	/* get ARMCYCLE */
	filter->tmp_armcycle = plog_hdr->armcycle;
	filter->idx_type = EWPF_INVALID;
	filter->xtlv_idx = EWPF_INVALID;

#ifdef EWPF_DEBUG
	{
		char buf[EWPF_DEBUG_BUF_LEN];
		int idx;

		memset(buf, 0, sizeof(buf));
		DHD_FILTER_ERR(("tag %d(%x) count %d(%x)\n",
			plog_hdr->tag, plog_hdr->tag, plog_hdr->count, plog_hdr->count));
		for (idx = 0; idx < plog_hdr->count; idx++) {
			sprintf(&buf[strlen(buf)], "%08x ", data[idx]);
			if ((idx + 1) % EWPF_VAL_CNT_PLINE == 0) {
				DHD_FILTER_ERR(("%s\n", buf));
				memset(buf, 0, sizeof(buf));
			}
		}
		if (strlen(buf) > 0) {
			DHD_FILTER_ERR(("%s\n", buf));
		}
	}
#endif /* EWPF_DEBUG */

	ctx.dhdp = dhdp;
	ctx.tbl = EWPF_main;
	if ((err = bcm_unpack_xtlv_buf(
		&ctx,
		(const uint8 *)data,
		(plog_hdr->count - 1) * sizeof(uint32),
		BCM_XTLV_OPTION_ALIGN32,
		filter_main_cb))) {
		DHD_FILTER_ERR(("FAIL TO UNPACK XTLV: err(%d)\n", err));
	}
}
/* ========= Private Command(Serialize) ============= */
/* XXX REPORT MODULE will be done after discuss with customer */
/* XXX Current implementation is temporal to verify FILTER MODULE works */
//#define EWPR_DEBUG
#ifdef EWPR_DEBUG
#undef DHD_FILTER_TRACE
#define DHD_FILTER_TRACE DHD_FILTER_ERR
#endif /* EWPR_DEBUG */
#define EWPR_DEBUG_BUF_LEN	512

#define EWP_REPORT_ELEM_PRINT_BUF	256
#define EWP_REPORT_NAME_MAX		64

#ifdef DHD_EWPR_VER2
#define EWP_REPORT_VERSION	0x20190514
#define EWP_REPORT_SET_DEFAULT	0x01
#define EWPR_CSDCLIENT_DIFF	10
#define EWPR_INTERVAL	3
#define EWPR_DELTA_CNT	1	/* 3 seconds before */
#define EWPR_ARRAY_CNT	10	/* INTERVAL * ARRAY total 30 seconds to lock */
#define EWPR_DELTA_LAST_POS		6

#define INDEX_STR_SIZE		6
#define	DHD_STAT_STR_SIZE	2
#define REPORT_VERSION_STR_SIZE	8
#define DELIMITER_LEN		1
#else
#define EWP_REPORT_VERSION	0x20170905
#define EWPR_CSDCLIENT_DIFF	4
#define EWPR_INTERVAL	3
#define EWPR_ARRAY_CNT	10	/* INTERVAL * ARRAY total 30 seconds to lock */
#endif /* DHD_EWPR_VER2 */

#define EWPR_DELTA3_POS		3
#define EWPR_DELTA2_POS		2
#define EWPR_DELTA1_POS		1
#define EWPR_NOW_POS		0

#define EWPR_DELTA1_CNT	2	/* 6 seconds before */
#define EWPR_DELTA2_CNT	5	/* 15 seconds before */
#define EWPR_DELTA3_CNT	9	/* 27 seconds before */

#define EWPR_CNT_PER_LINE	5

/* EWP Reporter display format */
#define EWP_DEC	1
#define EWP_HEX	2
#define EWP_BIN	3

/* EWP Filter Data type */
/* BASIC : signed + length */
#define EWP_UINT8	2
#define EWP_UINT16	4
#define EWP_UINT32	8
#define EWP_UINT64	16
#define EWP_INT8	102
#define EWP_INT16	104
#define EWP_INT32	108
#define EWP_BIT		201

/* NON BAISC : need special handling */
#define EWP_NON_BASIC	200
#define EWP_DATE		201
#define EWP_TIME		202
#define EWP_BSSID		203
#define EWP_OUI			204

/* Delimiter between values */
#define KEY_DEL	' '
#define RAW_DEL '_'

/* IOVAR BUF SIZE */
#define EWPR_IOV_BUF_LEN	64

typedef struct {
	void *ring;				/* INPUT ring to lock */
	void **elem_list;		/* OUTPUT elem ptr list for each delta */
	uint32 max_armcycle;	/* IN/OUT arm cycle should be less than this */
	uint32 min_armcycle;	/* IN/OUT arm cycle should be bigger than this */
	uint32 max_period;		/* IN allowed time diff between first and last */
	uint32 delta_cnt;		/* IN finding delta count */
	uint32 *delta_list;		/* IN delta values to find */
} ewpr_lock_param_t;

#define MAX_MULTI_VER	3
typedef struct {
	uint32	version;		/* VERSION for multiple version struct */
	uint32	offset;			/* offset of the member at the version */
} ewpr_MVT_offset_elem_t;	/* elem for multi version type */

typedef struct {
	uint32	version_offset;		/* offset of version */
	ewpr_MVT_offset_elem_t opv[MAX_MULTI_VER];	/* offset per version */
} ewpr_MVT_offset_t;			/* multi_version type */

typedef struct {
	char name[EWP_REPORT_NAME_MAX];
	int ring_type;		/* Ring Type : EWPF_IDX_TYPE_SLICE, EWPF_IDX_TYPE_IFACE */
	int	is_multi_version;		/* is multi version */
	union {
		uint32 offset;			/* Offset from start of element structure */
		ewpr_MVT_offset_t v_info;
	};
	int data_type;				/* Data type : one of EWP Filter Data Type */
	int display_format;			/* Display format : one of EWP Reporter display */
	int display_type;			/* MAX display BYTE : valid for HEX and BIN FORM */
#ifdef DHD_EWPR_VER2
	int info_type;			/* info type : EWPF_INFO_ECNT, EWPF_INFO_IOVAR, ... */
	int display_bit_length;		/* packing bit : valid for BIN FORM */
	int display_array_size;		/* array size */
	int display_method;		/* serial or diff */
	int unit_convert;		/* unit conversion
					 * 0 or 1 : no conversion, put data as is
					 * greater than 1, divide value by unit_convert
					 */
	bool need_abs;			/* need absolute function for negative value */
#endif /* DHD_EWPR_VER2 */
} ewpr_serial_info_t;

/* offset defines */
#define EWPR_CNT_VERSION_OFFSET \
	OFFSETOF(EWPF_slc_elem_t, compact_cntr_v3)

#define EWPR_CNT_V1_OFFSET(a) \
	WL_PERIODIC_COMPACT_CNTRS_VER_1, \
	(OFFSETOF(EWPF_slc_elem_t, compact_cntr_v1) + OFFSETOF(wl_periodic_compact_cntrs_v1_t, a))
#define EWPR_CNT_V2_OFFSET(a) \
	WL_PERIODIC_COMPACT_CNTRS_VER_2, \
	(OFFSETOF(EWPF_slc_elem_t, compact_cntr_v2) + OFFSETOF(wl_periodic_compact_cntrs_v2_t, a))
#define EWPR_CNT_V3_OFFSET(a) \
	WL_PERIODIC_COMPACT_CNTRS_VER_3, \
	(OFFSETOF(EWPF_slc_elem_t, compact_cntr_v3) + OFFSETOF(wl_periodic_compact_cntrs_v3_t, a))
#define EWPR_STAT_OFFSET(a) \
	(OFFSETOF(EWPF_ifc_elem_t, if_stat) + OFFSETOF(wl_if_stats_t, a))
#define EWPR_INFRA_OFFSET(a) \
	(OFFSETOF(EWPF_ifc_elem_t, infra) + OFFSETOF(wl_if_infra_stats_t, a))
#define EWPR_MGMT_OFFSET(a) \
	(OFFSETOF(EWPF_ifc_elem_t, mgmt_stat) + OFFSETOF(wl_if_mgt_stats_t, a))
#define EWPR_LQM_OFFSET(a) \
	(OFFSETOF(EWPF_ifc_elem_t, lqm) + OFFSETOF(wl_lqm_t, a))
#define EWPR_SIGNAL_OFFSET(a) \
	(EWPR_LQM_OFFSET(current_bss) + OFFSETOF(wl_rx_signal_metric_t, a))
#define EWPR_IF_COMP_OFFSET(a) \
	(OFFSETOF(EWPF_ifc_elem_t, if_comp_stat) + OFFSETOF(wl_if_state_compact_t, a))
#define EWPR_EVENT_COUNTER_OFFSET(a) \
	(OFFSETOF(EWPF_event_elem_t, event_stat) + OFFSETOF(wl_event_based_statistics_v4_t, a))
#define EWPR_KEY_INFO_OFFSET(a) \
	(OFFSETOF(EWPF_key_info_elem_t, key_update_info) + OFFSETOF(key_update_info_v1_t, a))
#define EWPR_TX_TOSS_HIST_OFFSET(a) \
	(OFFSETOF(EWPF_slc_elem_t, hist_tx_toss_stat) + \
		OFFSETOF(evt_hist_compact_toss_stats_v1_t, a))
#define EWPR_RX_TOSS_HIST_OFFSET(a) \
	(OFFSETOF(EWPF_slc_elem_t, hist_rx_toss_stat) + \
		OFFSETOF(evt_hist_compact_toss_stats_v1_t, a))
#define EWPR_BTC_STAT_OFFSET(a) \
	(OFFSETOF(EWPF_slc_elem_t, btc_stat) + \
		OFFSETOF(wlc_btc_stats_v4_t, a))
#define EWPR_COMPACT_HE_CNT_OFFSET(a) \
	(OFFSETOF(EWPF_slc_elem_t, compact_he_cnt) + \
		OFFSETOF(wl_compact_he_cnt_wlc_v2_t, a))
#define EWPR_ROAM_STATS_PERIODIC_OFFSET(a) \
	(OFFSETOF(EWPF_ifc_elem_t, roam_stat) + OFFSETOF(wl_roam_stats_v1_t, a))

/* serail info type define */
#define EWPR_SERIAL_CNT(a) {\
	#a, EWPF_IDX_TYPE_SLICE, TRUE, \
	.v_info = { EWPR_CNT_VERSION_OFFSET, \
		{{EWPR_CNT_V1_OFFSET(a)}, \
		{EWPR_CNT_V2_OFFSET(a)}, \
		{EWPR_CNT_V3_OFFSET(a)}}}, \
	EWP_UINT32, EWP_HEX, EWP_UINT32}
#define EWPR_SERIAL_CNT_16(a) {\
	#a, EWPF_IDX_TYPE_SLICE, TRUE, \
	.v_info = { EWPR_CNT_VERSION_OFFSET, \
		{{EWPR_CNT_V1_OFFSET(a)}, \
		{EWPR_CNT_V2_OFFSET(a)}, \
		{EWPR_CNT_V3_OFFSET(a)}}}, \
	EWP_UINT32, EWP_HEX, EWP_UINT16}
#define EWPR_SERIAL_STAT(a) {\
	#a, EWPF_IDX_TYPE_IFACE, FALSE, .offset = EWPR_STAT_OFFSET(a), \
	EWP_UINT64, EWP_HEX, EWP_UINT32}
#define EWPR_SERIAL_INFRA(a) {\
	#a, EWPF_IDX_TYPE_IFACE, FALSE, .offset = EWPR_INFRA_OFFSET(a), \
	EWP_UINT32, EWP_HEX, EWP_UINT16}
#define EWPR_SERIAL_MGMT(a) {\
	#a, EWPF_IDX_TYPE_IFACE, FALSE, .offset = EWPR_MGMT_OFFSET(a), \
	EWP_UINT32, EWP_HEX, EWP_UINT16}
#define EWPR_SERIAL_LQM(a) {\
	#a, EWPF_IDX_TYPE_IFACE, FALSE, .offset = EWPR_LQM_OFFSET(a), \
	EWP_INT32, EWP_DEC, EWP_INT8}
#define EWPR_SERIAL_SIGNAL(a) {\
	#a, EWPF_IDX_TYPE_IFACE, FALSE, .offset = EWPR_SIGNAL_OFFSET(a), \
	EWP_INT32, EWP_DEC, EWP_INT8}
#define EWPR_SERIAL_IFCOMP_8(a) {\
	#a, EWPF_IDX_TYPE_IFACE, FALSE, .offset = EWPR_IF_COMP_OFFSET(a), \
	EWP_INT8, EWP_DEC, EWP_INT8}
#define EWPR_SERIAL_IFCOMP_16(a) {\
	#a, EWPF_IDX_TYPE_IFACE, FALSE, .offset = EWPR_IF_COMP_OFFSET(a), \
	EWP_UINT16, EWP_DEC, EWP_UINT16}
#define EWPR_SERIAL_ARM(a) {\
	"armcycle:" #a, EWPF_IDX_TYPE_##a, FALSE, {0, }, \
	EWP_UINT32, EWP_DEC, EWP_UINT32}
#define EWPR_SERIAL_NONE {"", EWPF_INVALID, FALSE, {0, }, 0, 0, 0}

#ifdef DHD_EWPR_VER2

#define RAW_BUFFER_SIZE		720u
#define BASE64_BUFFER_SIZE	960u /* 33 percent larger than original binary data */
#define EWPR_HEADER_SIZE	39u
#define	EWPR_MAX_STR_SIZE	EWPR_HEADER_SIZE + EWPR_HEADER_SIZE

#define EWPR_DISPLAY_METHOD_SINGLE	0
#define EWPR_DISPLAY_METHOD_DIFF	1

#define MAX_BIT_SIZE	8
#define MAX_BIT_SHIFT	7

#define INDEX_UNSPECIFIED 0u

enum ewpr_context_type {
	EWP_CONTEXT_TYPE_UNWANTED_NETWORK = 0,
	EWP_CONTEXT_TYPE_ASSOC_FAIL = 1,
	EWP_CONTEXT_TYPE_ABNORMAL_DISCONNECT = 2,
	EWP_CONTEXT_TYPE_MAX = 3
};

enum ewpr_unwanted_net_sub_type {
	EWP_UNWANT_NET_SUB_TYPE_UNSPECIFIED = 0,
	EWP_UNWANT_NET_SUB_TYPE_ARP_FAIL = 1,
	EWP_UNWANT_NET_SUB_TYPE_TXBAD = 2,
	EWP_UNWANT_NET_SUB_TYPE_MAX = 3
};

enum ewpr_assoc_fail_sub_type {
	EWP_ASSOC_FAIL_SUB_TYPE_UNSPECIFIED = 0,
	EWP_ASSOC_FAIL_SUB_TYPE_DHCP_FAIL = 1,
	EWP_ASSOC_FAIL_SUB_TYPE_EAP_FAIL = 2,
	EWP_ASSOC_FAIL_SUB_TYPE_EAP_TIMEOUT = 3,
	EWP_ASSOC_FAIL_SUB_TYPE_4WAY_FAIL = 4,
	EWP_ASSOC_FAIL_SUB_TYPE_MAX = 5
};

enum ewpr_abnormal_disconnect_sub_type {
	EWP_ABNRML_DISCONNCET_SUB_TYPE_UNSPECIFIED = 0,
	EWP_ABNRML_DISCONNCET_SUB_TYPE_DISCONNECT_BY_HOST = 1,
	EWP_ABNRML_DISCONNCET_SUB_TYPE_MAX = 2
};

typedef struct {
	uint32 index1;
	uint32 index2;
	uint32 index3;
	ewpr_serial_info_t *table;
} ewpr_serial_context_info_t;

#define EWPR_SINGLE_DEFAULT EWPR_DISPLAY_METHOD_SINGLE, EWPF_NO_UNIT_CONV
#define EWPR_DIFF_DEFAULT EWPR_DISPLAY_METHOD_DIFF, EWPF_NO_UNIT_CONV

#define EWPR_SINGLE_NSEC_TO_MSEC EWPR_DISPLAY_METHOD_SINGLE, EWPF_NSEC_TO_MSEC
#define EWPR_SINGLE_USEC_TO_MSEC EWPR_DISPLAY_METHOD_SINGLE, EWPF_USEC_TO_MSEC
#define EWPR_SINGLE_USEC_TO_SEC EWPR_DISPLAY_METHOD_SINGLE, EWPF_USEC_TO_SEC

/* serail info type define */
#define EWPR_SERIAL_CNT_V3_BIT(a, b, c, d) {\
	#a, EWPF_IDX_TYPE_SLICE, TRUE, \
	.v_info = { EWPR_CNT_VERSION_OFFSET, \
		{{EWPR_CNT_V3_OFFSET(a)}}}, \
	EWP_UINT32, EWP_BIN, EWP_BIT, EWPF_INFO_ECNT, b, c, d}
#define EWPR_SERIAL_STAT_BIT(a, b, c, d) {\
	#a, EWPF_IDX_TYPE_IFACE, FALSE, .offset = EWPR_STAT_OFFSET(a), \
	EWP_UINT64, EWP_BIN, EWP_BIT, EWPF_INFO_ECNT, b, c, d}
#define EWPR_SERIAL_INFRA_BIT(a, b, c, d) {\
	#a, EWPF_IDX_TYPE_IFACE, FALSE, .offset = EWPR_INFRA_OFFSET(a), \
	EWP_UINT32, EWP_BIN, EWP_BIT, EWPF_INFO_ECNT, b, c, d}
#define EWPR_SERIAL_MGMT_BIT(a, b, c, d) {\
	#a, EWPF_IDX_TYPE_IFACE, FALSE, .offset = EWPR_MGMT_OFFSET(a), \
	EWP_UINT32, EWP_BIN, EWP_BIT, EWPF_INFO_ECNT, b, c, d}
#define EWPR_SERIAL_LQM_BIT(a, b, c, d) {\
	#a, EWPF_IDX_TYPE_IFACE, FALSE, .offset = EWPR_LQM_OFFSET(a), \
	EWP_INT32, EWP_BIN, EWP_BIT, EWPF_INFO_ECNT, b, c, d}
#define EWPR_SERIAL_SIGNAL_BIT(a, b, c, d) {\
	#a, EWPF_IDX_TYPE_IFACE, FALSE, .offset = EWPR_SIGNAL_OFFSET(a), \
	EWP_INT32, EWP_BIN, EWP_BIT, EWPF_INFO_ECNT, b, c, d}
#define EWPR_SERIAL_IFCOMP_BIT(a, b, c, d) {\
	#a, EWPF_IDX_TYPE_IFACE, FALSE, .offset = EWPR_IF_COMP_OFFSET(a), \
	EWP_INT8, EWP_BIN, EWP_BIT, EWPF_INFO_ECNT, b, c, d}
#define EWPR_SERIAL_EVENT_COUNTER_16_BIT(a, b, c, d) {\
	#a, EWPF_IDX_TYPE_EVENT, FALSE, .offset = EWPR_EVENT_COUNTER_OFFSET(a), \
	EWP_UINT16, EWP_BIN, EWP_BIT, EWPF_INFO_ECNT, b, c, d}
#define EWPR_SERIAL_EVENT_COUNTER_32_BIT(a, b, c, d) {\
	#a, EWPF_IDX_TYPE_EVENT, FALSE, .offset = EWPR_EVENT_COUNTER_OFFSET(a), \
	EWP_UINT32, EWP_BIN, EWP_BIT, EWPF_INFO_ECNT, b, c, d}
#define EWPR_SERIAL_KEY_INFO_BIT(a, b, c, d) {\
	#a, EWPF_IDX_TYPE_KEY_INFO, FALSE, .offset = EWPR_KEY_INFO_OFFSET(a), \
	EWP_UINT32, EWP_BIN, EWP_BIT, EWPF_INFO_ECNT, b, c, d}
#define EWPR_SERIAL_ROAM_STAT_PERIODIC_16_BIT(a, b, c, d) {\
	#a, EWPF_IDX_TYPE_IFACE, FALSE, .offset = EWPR_ROAM_STATS_PERIODIC_OFFSET(a), \
	EWP_UINT16, EWP_BIN, EWP_BIT, EWPF_INFO_ECNT, b, c, d}
#define EWPR_SERIAL_ROAM_STAT_PERIODIC_32_BIT(a, b, c, d) {\
	#a, EWPF_IDX_TYPE_IFACE, FALSE, .offset = EWPR_ROAM_STATS_PERIODIC_OFFSET(a), \
	EWP_UINT32, EWP_BIN, EWP_BIT, EWPF_INFO_ECNT, b, c, d}
#define EWPR_SERIAL_ARM_BIT(a, b, c, d) {\
	#a, EWPF_IDX_TYPE_##a, FALSE, {0, }, \
	EWP_UINT32, EWP_BIN, EWP_BIT, EWPF_INFO_ECNT, b, c, d}
#define EWPR_SERIAL_IOVAR_BIT(a, b) {\
	#a, 0, 0, .offset = 0, \
	EWP_UINT32, EWP_BIN, EWP_BIT, EWPF_INFO_IOVAR, b, 1, EWPR_SINGLE_DEFAULT}
#define EWPR_SERIAL_VERSION_BIT(a, b) {\
	#a, 0, 0, .offset = 0, \
	EWP_UINT32, EWP_BIN, EWP_BIT, EWPF_INFO_VER, b, 1, EWPR_SINGLE_DEFAULT}
#define EWPR_SERIAL_TYPE_BIT(a, b) {\
	#a, 0, 0, .offset = 0, \
	EWP_UINT32, EWP_BIN, EWP_BIT, EWPF_INFO_TYPE, b, 1, EWPR_SINGLE_DEFAULT}
#define EWPR_SERIAL_CPLOG_BIT(a, b, c) {\
	#a, 0, 0, .offset = 0, \
	EWP_UINT32, EWP_BIN, EWP_BIT, EWPF_INFO_CPLOG, b, c, EWPR_SINGLE_DEFAULT}
#define EWPR_SERIAL_DHDSTAT_BIT(a, b, c, d) {\
	#a, 0, 0, .offset = 0, \
	EWP_UINT32, EWP_BIN, EWP_BIT, EWPF_INFO_DHDSTAT, b, c, d}
#define EWPR_SERIAL_TX_TOSS_HIST_BIT(a, b, c, d) {\
	#a, EWPF_IDX_TYPE_SLICE, FALSE, .offset = EWPR_TX_TOSS_HIST_OFFSET(a), \
	EWP_UINT32, EWP_BIN, EWP_BIT, EWPF_INFO_ECNT, b, c, d}
#define EWPR_SERIAL_RX_TOSS_HIST_BIT(a, b, c, d) {\
	#a, EWPF_IDX_TYPE_SLICE, FALSE, .offset = EWPR_RX_TOSS_HIST_OFFSET(a), \
	EWP_UINT32, EWP_BIN, EWP_BIT, EWPF_INFO_ECNT, b, c, d}
#define EWPR_SERIAL_BTC_STAT_16_BIT(a, b, c, d) {\
	#a, EWPF_IDX_TYPE_SLICE, FALSE, .offset = EWPR_BTC_STAT_OFFSET(a), \
	EWP_UINT16, EWP_BIN, EWP_BIT, EWPF_INFO_ECNT, b, c, d}
#define EWPR_SERIAL_BTC_STAT_32_BIT(a, b, c, d) {\
	#a, EWPF_IDX_TYPE_SLICE, FALSE, .offset = EWPR_BTC_STAT_OFFSET(a), \
	EWP_UINT32, EWP_BIN, EWP_BIT, EWPF_INFO_ECNT, b, c, d}
#define EWPR_SERIAL_COMPACT_HE_CNT_BIT(a, b, c, d) {\
	#a, EWPF_IDX_TYPE_SLICE, FALSE, .offset = EWPR_COMPACT_HE_CNT_OFFSET(a), \
	EWP_UINT32, EWP_BIN, EWP_BIT, EWPF_INFO_ECNT, b, c, d}

#define EWPR_SERIAL_NONE_BIT {"", EWPF_INVALID, FALSE, {0, }, 0, 0, 0, 0, 0, 0, 0}

#ifdef EWPR_DEBUG
static void ewpr_print_byte_as_bits(char val);
#endif /* EWPR_DEBUG */

static int32
ewpr_diff_bit_pack(ewpr_serial_info_t *info, char *buf, int buf_len,
	void *_f_op, void *_s_op, int32 bit_offset);
static int32
ewpr_single_bit_pack(ewpr_serial_info_t *info, char *buf, int buf_len,
	void *_ptr, int32 bit_offset);
static int32
ewpr_bit_pack_basic(char *buf, int buf_len, uint32 data, int32 format,
	int32 display_type, int32 display_bit_length, int32 bit_offset);

static char*
ewpr_base64_encode(dhd_pub_t *dhdp, char* input, int32 length);
#endif /* DHD_EWPR_VER2 */

ewpr_serial_info_t
ewpr_serial_CSDCLIENT_key_tbl[] = {
	EWPR_SERIAL_STAT(txframe),
	EWPR_SERIAL_STAT(txerror),
	EWPR_SERIAL_STAT(rxframe),
	EWPR_SERIAL_STAT(rxerror),
	EWPR_SERIAL_STAT(txretrans),
	EWPR_SERIAL_INFRA(rxbeaconmbss),
	EWPR_SERIAL_CNT(txallfrm),
	EWPR_SERIAL_CNT(rxrsptmout),
	EWPR_SERIAL_CNT(rxbadplcp),
	EWPR_SERIAL_CNT(rxcrsglitch),
	EWPR_SERIAL_CNT(rxbadfcs),
	EWPR_SERIAL_CNT_16(rxbeaconmbss),
	EWPR_SERIAL_CNT_16(rxbeaconobss),
	EWPR_SERIAL_NONE
};

ewpr_serial_info_t
ewpr_serial_CSDCLIENT_diff_tbl[] = {
	EWPR_SERIAL_STAT(txframe),
	EWPR_SERIAL_STAT(txerror),
	EWPR_SERIAL_STAT(rxframe),
	EWPR_SERIAL_STAT(rxerror),
	EWPR_SERIAL_STAT(txretrans),
	EWPR_SERIAL_INFRA(rxbeaconmbss),
	EWPR_SERIAL_MGMT(txassocreq),
	EWPR_SERIAL_MGMT(txreassocreq),
	EWPR_SERIAL_MGMT(txdisassoc),
	EWPR_SERIAL_MGMT(rxdisassoc),
	EWPR_SERIAL_MGMT(rxassocrsp),
	EWPR_SERIAL_MGMT(rxreassocrsp),
	EWPR_SERIAL_MGMT(txauth),
	EWPR_SERIAL_MGMT(rxauth),
	EWPR_SERIAL_MGMT(txdeauth),
	EWPR_SERIAL_MGMT(rxdeauth),
	EWPR_SERIAL_MGMT(txaction),
	EWPR_SERIAL_MGMT(rxaction),
	EWPR_SERIAL_CNT(txallfrm),
	EWPR_SERIAL_CNT(rxrsptmout),
	EWPR_SERIAL_CNT(rxbadplcp),
	EWPR_SERIAL_CNT(rxcrsglitch),
	EWPR_SERIAL_CNT(rxbadfcs),
	EWPR_SERIAL_CNT_16(rxbeaconmbss),
	EWPR_SERIAL_CNT_16(rxbeaconobss),
	EWPR_SERIAL_NONE
};

ewpr_serial_info_t
ewpr_serial_CSDCLIENT_array_tbl[] = {
	EWPR_SERIAL_IFCOMP_8(rssi_sum),
	EWPR_SERIAL_IFCOMP_8(snr),
	EWPR_SERIAL_IFCOMP_8(noise_level),
	EWPR_SERIAL_NONE
};

#ifdef EWPR_DEBUG
ewpr_serial_info_t
ewpr_serial_dbg_tbl[] = {
	EWPR_SERIAL_ARM(IFACE),
	EWPR_SERIAL_ARM(SLICE),
	EWPR_SERIAL_NONE
};
#endif /* EWPR_DEBUG */

#ifdef DHD_EWPR_VER2

ewpr_serial_info_t
ewpr_serial_bit_unwanted_network_default_tbl[] = {
	EWPR_SERIAL_VERSION_BIT(version, 32),
	EWPR_SERIAL_TYPE_BIT(type, 5),
	EWPR_SERIAL_DHDSTAT_BIT(dhdstat_last_ts, 32, 1, EWPR_SINGLE_USEC_TO_SEC),
	EWPR_SERIAL_DHDSTAT_BIT(dhdstat_last, 8, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_DHDSTAT_BIT(dhdstat_prev_ts, 32, 1, EWPR_SINGLE_USEC_TO_SEC),
	EWPR_SERIAL_DHDSTAT_BIT(dhdstat_prev, 8, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_IOVAR_BIT(auth, 8),
	EWPR_SERIAL_IOVAR_BIT(wsec, 8),
	EWPR_SERIAL_IOVAR_BIT(mfp, 1),
	EWPR_SERIAL_IOVAR_BIT(bip, 8),
	EWPR_SERIAL_ARM_BIT(IFACE, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_STAT_BIT(txframe, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_STAT_BIT(txerror, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_STAT_BIT(rxframe, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_STAT_BIT(rxerror, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_STAT_BIT(txretrans, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(txassocreq, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(txreassocreq, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(txdisassoc, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(rxdisassoc, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(rxassocrsp, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(rxreassocrsp, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(txauth, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(rxauth, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(txdeauth, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(rxdeauth, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(txaction, 7, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(rxaction, 7, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(txallfrm, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxrsptmout, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxbadplcp, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxcrsglitch, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxbadfcs, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxbeaconmbss, 5, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxbeaconobss, 12, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(lqcm_report, 19, 6, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(tx_toss_cnt, 18, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rx_toss_cnt, 18, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxretry, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxdup, 15, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(chswitch_cnt, 8, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(pm_dur, 12, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxholes, 15, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(bt_dcsn_map, 16, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(bt_dcsn_cnt, 12, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(bt_a2dp_hiwat_cnt, 12, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(bt_datadelay_cnt, 12, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(bt_crtpri_cnt, 12, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(bt_pri_cnt, 12, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(a2dpbuf5cnt, 12, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(a2dpbuf6cnt, 12, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(a2dpbuf7cnt, 12, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_32_BIT(bt_gnt_dur, 12, 3, EWPR_SINGLE_USEC_TO_MSEC),
	EWPR_SERIAL_IFCOMP_BIT(rssi_sum, 7, 6, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_IFCOMP_BIT(snr, 7, 6, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_IFCOMP_BIT(noise_level, 7, 6, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_32_BIT(initial_assoc_time, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_32_BIT(prev_roam_time, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_32_BIT(last_roam_event_type, 8, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_32_BIT(last_roam_event_status, 6, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_32_BIT(last_roam_event_reason, 6, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_16_BIT(roam_success_cnt, 10, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_16_BIT(roam_fail_cnt, 10, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_16_BIT(roam_attempt_cnt, 11, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_16_BIT(max_roam_target_cnt, 5, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_16_BIT(min_roam_target_cnt, 5, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_16_BIT(max_cached_ch_cnt, 5, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_16_BIT(min_cached_ch_cnt, 5, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_16_BIT(partial_roam_scan_cnt, 11, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_16_BIT(full_roam_scan_cnt, 11, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_COMPACT_HE_CNT_BIT(he_rxtrig_myaid, 10, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_COMPACT_HE_CNT_BIT(he_colormiss_cnt, 10, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_COMPACT_HE_CNT_BIT(he_rxmsta_back, 10, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_COMPACT_HE_CNT_BIT(he_txtbppdu, 10, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_COMPACT_HE_CNT_BIT(he_null_tbppdu, 10, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_KEY_INFO_BIT(timestamp, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_KEY_INFO_BIT(algo, 6, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_KEY_INFO_BIT(key_flags, 16, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_TX_TOSS_HIST_BIT(htr_rn_ts_last, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_TX_TOSS_HIST_BIT(htr_rn_last, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_TX_TOSS_HIST_BIT(htr_rn_ts_prev, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_TX_TOSS_HIST_BIT(htr_rn_prev, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_TX_TOSS_HIST_BIT(htr_rc_ts_max, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_TX_TOSS_HIST_BIT(htr_rc_max, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_TX_TOSS_HIST_BIT(htr_rc_ts_secnd, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_TX_TOSS_HIST_BIT(htr_rc_secnd, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_RX_TOSS_HIST_BIT(htr_rn_ts_last, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_RX_TOSS_HIST_BIT(htr_rn_last, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_RX_TOSS_HIST_BIT(htr_rn_ts_prev, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_RX_TOSS_HIST_BIT(htr_rn_prev, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_RX_TOSS_HIST_BIT(htr_rc_ts_max, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_RX_TOSS_HIST_BIT(htr_rc_max, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_RX_TOSS_HIST_BIT(htr_rc_ts_secnd, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_RX_TOSS_HIST_BIT(htr_rc_secnd, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_CPLOG_BIT(packtlog, 22, 70),
	EWPR_SERIAL_NONE
};

ewpr_serial_info_t
ewpr_serial_bit_assoc_fail_default_tbl[] = {
	EWPR_SERIAL_VERSION_BIT(version, 32),
	EWPR_SERIAL_TYPE_BIT(type, 5),
	EWPR_SERIAL_DHDSTAT_BIT(dhdstat_last_ts, 32, 1, EWPR_SINGLE_USEC_TO_SEC),
	EWPR_SERIAL_DHDSTAT_BIT(dhdstat_last, 8, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_DHDSTAT_BIT(dhdstat_prev_ts, 32, 1, EWPR_SINGLE_USEC_TO_SEC),
	EWPR_SERIAL_DHDSTAT_BIT(dhdstat_prev, 8, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_IOVAR_BIT(auth, 8),
	EWPR_SERIAL_IOVAR_BIT(wsec, 8),
	EWPR_SERIAL_IOVAR_BIT(mfp, 1),
	EWPR_SERIAL_IOVAR_BIT(bip, 8),
	EWPR_SERIAL_ARM_BIT(IFACE, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_STAT_BIT(txframe, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_STAT_BIT(txerror, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_STAT_BIT(rxframe, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_STAT_BIT(rxerror, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_STAT_BIT(txretrans, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(txassocreq, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(txreassocreq, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(txdisassoc, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(rxdisassoc, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(rxassocrsp, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(rxreassocrsp, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(txauth, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(rxauth, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(txdeauth, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(rxdeauth, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(txaction, 7, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(rxaction, 7, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(txallfrm, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxrsptmout, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxbadplcp, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxcrsglitch, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxbadfcs, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxbeaconmbss, 5, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxbeaconobss, 12, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(lqcm_report, 19, 6, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(tx_toss_cnt, 18, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rx_toss_cnt, 18, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxretry, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxdup, 15, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(chswitch_cnt, 8, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(pm_dur, 12, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxholes, 15, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(bt_dcsn_map, 16, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(bt_dcsn_cnt, 12, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(bt_a2dp_hiwat_cnt, 12, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(bt_datadelay_cnt, 12, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(bt_crtpri_cnt, 12, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(bt_pri_cnt, 12, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(a2dpbuf5cnt, 12, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(a2dpbuf6cnt, 12, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(a2dpbuf7cnt, 12, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_32_BIT(bt_gnt_dur, 12, 3, EWPR_SINGLE_USEC_TO_MSEC),
	EWPR_SERIAL_IFCOMP_BIT(rssi_sum, 7, 6, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_IFCOMP_BIT(snr, 7, 6, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_IFCOMP_BIT(noise_level, 7, 6, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_COMPACT_HE_CNT_BIT(he_rxtrig_myaid, 10, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_COMPACT_HE_CNT_BIT(he_colormiss_cnt, 10, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_COMPACT_HE_CNT_BIT(he_rxmsta_back, 10, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_COMPACT_HE_CNT_BIT(he_txtbppdu, 10, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_COMPACT_HE_CNT_BIT(he_null_tbppdu, 10, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_KEY_INFO_BIT(timestamp, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_KEY_INFO_BIT(algo, 6, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_KEY_INFO_BIT(key_flags, 16, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_TX_TOSS_HIST_BIT(htr_rn_ts_last, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_TX_TOSS_HIST_BIT(htr_rn_last, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_TX_TOSS_HIST_BIT(htr_rn_ts_prev, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_TX_TOSS_HIST_BIT(htr_rn_prev, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_TX_TOSS_HIST_BIT(htr_rc_ts_max, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_TX_TOSS_HIST_BIT(htr_rc_max, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_TX_TOSS_HIST_BIT(htr_rc_ts_secnd, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_TX_TOSS_HIST_BIT(htr_rc_secnd, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_RX_TOSS_HIST_BIT(htr_rn_ts_last, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_RX_TOSS_HIST_BIT(htr_rn_last, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_RX_TOSS_HIST_BIT(htr_rn_ts_prev, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_RX_TOSS_HIST_BIT(htr_rn_prev, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_RX_TOSS_HIST_BIT(htr_rc_ts_max, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_RX_TOSS_HIST_BIT(htr_rc_max, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_RX_TOSS_HIST_BIT(htr_rc_ts_secnd, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_RX_TOSS_HIST_BIT(htr_rc_secnd, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_CPLOG_BIT(packtlog, 22, 70),
	EWPR_SERIAL_NONE
};

ewpr_serial_info_t
ewpr_serial_bit_abnormal_disconnect_default_tbl[] = {
	EWPR_SERIAL_VERSION_BIT(version, 32),
	EWPR_SERIAL_TYPE_BIT(type, 5),
	EWPR_SERIAL_DHDSTAT_BIT(dhdstat_last_ts, 32, 1, EWPR_SINGLE_USEC_TO_SEC),
	EWPR_SERIAL_DHDSTAT_BIT(dhdstat_last, 8, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_DHDSTAT_BIT(dhdstat_prev_ts, 32, 1, EWPR_SINGLE_USEC_TO_SEC),
	EWPR_SERIAL_DHDSTAT_BIT(dhdstat_prev, 8, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_IOVAR_BIT(auth, 8),
	EWPR_SERIAL_IOVAR_BIT(wsec, 8),
	EWPR_SERIAL_IOVAR_BIT(mfp, 1),
	EWPR_SERIAL_IOVAR_BIT(bip, 8),
	EWPR_SERIAL_ARM_BIT(IFACE, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_STAT_BIT(txframe, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_STAT_BIT(txerror, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_STAT_BIT(rxframe, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_STAT_BIT(rxerror, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_STAT_BIT(txretrans, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(txassocreq, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(txreassocreq, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(txdisassoc, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(rxdisassoc, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(rxassocrsp, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(rxreassocrsp, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(txauth, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(rxauth, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(txdeauth, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(rxdeauth, 4, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(txaction, 7, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_MGMT_BIT(rxaction, 7, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(txallfrm, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxrsptmout, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxbadplcp, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxcrsglitch, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxbadfcs, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxbeaconmbss, 5, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxbeaconobss, 12, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(lqcm_report, 19, 6, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(tx_toss_cnt, 18, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rx_toss_cnt, 18, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxretry, 17, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxdup, 15, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(chswitch_cnt, 8, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(pm_dur, 12, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_CNT_V3_BIT(rxholes, 15, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(bt_dcsn_map, 16, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(bt_dcsn_cnt, 12, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(bt_a2dp_hiwat_cnt, 12, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(bt_datadelay_cnt, 12, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(bt_crtpri_cnt, 12, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(bt_pri_cnt, 12, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(a2dpbuf5cnt, 12, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(a2dpbuf6cnt, 12, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_16_BIT(a2dpbuf7cnt, 12, 3, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_BTC_STAT_32_BIT(bt_gnt_dur, 12, 3, EWPR_SINGLE_USEC_TO_MSEC),
	EWPR_SERIAL_IFCOMP_BIT(rssi_sum, 7, 6, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_IFCOMP_BIT(snr, 7, 6, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_IFCOMP_BIT(noise_level, 7, 6, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_32_BIT(initial_assoc_time, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_32_BIT(prev_roam_time, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_32_BIT(last_roam_event_type, 8, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_32_BIT(last_roam_event_status, 6, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_32_BIT(last_roam_event_reason, 6, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_16_BIT(roam_success_cnt, 10, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_16_BIT(roam_fail_cnt, 10, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_16_BIT(roam_attempt_cnt, 11, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_16_BIT(max_roam_target_cnt, 5, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_16_BIT(min_roam_target_cnt, 5, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_16_BIT(max_cached_ch_cnt, 5, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_16_BIT(min_cached_ch_cnt, 5, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_16_BIT(partial_roam_scan_cnt, 11, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_ROAM_STAT_PERIODIC_16_BIT(full_roam_scan_cnt, 11, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_COMPACT_HE_CNT_BIT(he_rxtrig_myaid, 10, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_COMPACT_HE_CNT_BIT(he_colormiss_cnt, 10, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_COMPACT_HE_CNT_BIT(he_rxmsta_back, 10, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_COMPACT_HE_CNT_BIT(he_txtbppdu, 10, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_COMPACT_HE_CNT_BIT(he_null_tbppdu, 10, 6, EWPR_DIFF_DEFAULT),
	EWPR_SERIAL_KEY_INFO_BIT(timestamp, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_KEY_INFO_BIT(algo, 6, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_KEY_INFO_BIT(key_flags, 16, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_TX_TOSS_HIST_BIT(htr_rn_ts_last, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_TX_TOSS_HIST_BIT(htr_rn_last, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_TX_TOSS_HIST_BIT(htr_rn_ts_prev, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_TX_TOSS_HIST_BIT(htr_rn_prev, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_TX_TOSS_HIST_BIT(htr_rc_ts_max, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_TX_TOSS_HIST_BIT(htr_rc_max, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_TX_TOSS_HIST_BIT(htr_rc_ts_secnd, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_TX_TOSS_HIST_BIT(htr_rc_secnd, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_RX_TOSS_HIST_BIT(htr_rn_ts_last, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_RX_TOSS_HIST_BIT(htr_rn_last, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_RX_TOSS_HIST_BIT(htr_rn_ts_prev, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_RX_TOSS_HIST_BIT(htr_rn_prev, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_RX_TOSS_HIST_BIT(htr_rc_ts_max, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_RX_TOSS_HIST_BIT(htr_rc_max, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_RX_TOSS_HIST_BIT(htr_rc_ts_secnd, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_RX_TOSS_HIST_BIT(htr_rc_secnd, 32, 1, EWPR_SINGLE_DEFAULT),
	EWPR_SERIAL_CPLOG_BIT(packtlog, 22, 70),
	EWPR_SERIAL_NONE
};

ewpr_serial_context_info_t ewpr_serial_context_info[] = {
	{EWP_CONTEXT_TYPE_UNWANTED_NETWORK, EWP_UNWANT_NET_SUB_TYPE_UNSPECIFIED,
	INDEX_UNSPECIFIED, &ewpr_serial_bit_unwanted_network_default_tbl[0]},
	{EWP_CONTEXT_TYPE_UNWANTED_NETWORK, EWP_UNWANT_NET_SUB_TYPE_ARP_FAIL,
	INDEX_UNSPECIFIED, &ewpr_serial_bit_unwanted_network_default_tbl[0]},
	{EWP_CONTEXT_TYPE_UNWANTED_NETWORK, EWP_UNWANT_NET_SUB_TYPE_TXBAD,
	INDEX_UNSPECIFIED, &ewpr_serial_bit_unwanted_network_default_tbl[0]},
	{EWP_CONTEXT_TYPE_ASSOC_FAIL, EWP_ASSOC_FAIL_SUB_TYPE_UNSPECIFIED,
	INDEX_UNSPECIFIED, &ewpr_serial_bit_assoc_fail_default_tbl[0]},
	{EWP_CONTEXT_TYPE_ASSOC_FAIL, EWP_ASSOC_FAIL_SUB_TYPE_DHCP_FAIL,
	INDEX_UNSPECIFIED, &ewpr_serial_bit_assoc_fail_default_tbl[0]},
	{EWP_CONTEXT_TYPE_ASSOC_FAIL, EWP_ASSOC_FAIL_SUB_TYPE_EAP_FAIL,
	INDEX_UNSPECIFIED, &ewpr_serial_bit_assoc_fail_default_tbl[0]},
	{EWP_CONTEXT_TYPE_ASSOC_FAIL, EWP_ASSOC_FAIL_SUB_TYPE_EAP_TIMEOUT,
	INDEX_UNSPECIFIED, &ewpr_serial_bit_assoc_fail_default_tbl[0]},
	{EWP_CONTEXT_TYPE_ASSOC_FAIL, EWP_ASSOC_FAIL_SUB_TYPE_4WAY_FAIL,
	INDEX_UNSPECIFIED, &ewpr_serial_bit_assoc_fail_default_tbl[0]},
	{EWP_CONTEXT_TYPE_ABNORMAL_DISCONNECT, EWP_ABNRML_DISCONNCET_SUB_TYPE_UNSPECIFIED,
	INDEX_UNSPECIFIED, &ewpr_serial_bit_abnormal_disconnect_default_tbl[0]},
	{EWP_CONTEXT_TYPE_ABNORMAL_DISCONNECT, EWP_ABNRML_DISCONNCET_SUB_TYPE_DISCONNECT_BY_HOST,
	INDEX_UNSPECIFIED, &ewpr_serial_bit_abnormal_disconnect_default_tbl[0]},
	{EWP_CONTEXT_TYPE_MAX, INDEX_UNSPECIFIED, INDEX_UNSPECIFIED, NULL}
};
#endif /* DHD_EWPR_VER2 */

int ewpr_set_period_lock(ewpr_lock_param_t *param);
int ewpr_diff_serial(ewpr_serial_info_t *info, char *buf,
	int buf_len, void *_f_op, void *_s_op, char del);
int ewpr_single_serial(ewpr_serial_info_t *info, char *buf, int buf_len, void *ptr, char del);

int
ewpr_serial_basic(char *buf, int buf_len, uint32 data, int format, int display_type, char del)
{
	if (format == EWP_HEX) {
		switch (display_type) {
			case EWP_INT8:
			case EWP_UINT8:
				return scnprintf(buf, buf_len, "%c%02x", del, data & 0xff);
			case EWP_INT16:
			case EWP_UINT16:
				return scnprintf(buf, buf_len, "%c%04x", del, data & 0xffff);
			case EWP_INT32:
			case EWP_UINT32:
				return scnprintf(buf, buf_len, "%c%08x", del, data & 0xffffffff);
			default:
				DHD_FILTER_ERR(("INVALID TYPE for Serial:%d", display_type));
				return 0;
		}
	}

	if (format == EWP_DEC) {
		int32 sdata = (int32) data;
		switch (display_type) {
			case EWP_INT8:
			case EWP_UINT8:
				return scnprintf(buf, buf_len, "%c%04d", del, sdata);
			case EWP_INT16:
			case EWP_UINT16:
				return scnprintf(buf, buf_len, "%c%06d", del, sdata);
			case EWP_INT32:
			case EWP_UINT32:
				return scnprintf(buf, buf_len, "%c%011d", del, sdata);
			default:
				DHD_FILTER_ERR(("INVALID TYPE for Serial:%d", display_type));
				return 0;
		}
	}

	if (format == EWP_BIN) {
		int32 sdata = (int32) data;
		switch (display_type) {
			case EWP_BIT:
				return scnprintf(buf, buf_len, "%c%011d", del, sdata);
			default:
				DHD_FILTER_ERR(("INVALID TYPE for Serial:%d", display_type));
				return 0;
		}
	}

	DHD_FILTER_ERR(("INVALID FORMAT for Serial:%d", format));
	return 0;
}

static int
ewpr_get_multi_offset(uint16 looking_version, ewpr_serial_info_t *info)
{
	int idx;
	ewpr_MVT_offset_elem_t *opv;

	DHD_FILTER_TRACE(("FINDING MULTI OFFSET: type = %s version = %d\n",
		info->name, looking_version));
	for (idx = 0; idx < MAX_MULTI_VER; idx ++) {
		opv = &(info->v_info.opv[idx]);

		/* END OF MULTI VERSION */
		if (opv->version == 0) {
			break;
		}
		if (looking_version == opv->version) {
			return opv->offset;
		}
	}
	/* return first version if no version is found */
	return info->v_info.opv[0].offset;
}
int
ewpr_single_serial(ewpr_serial_info_t *info, char *buf, int buf_len, void *_ptr, char del)
{
	uint32 sval = 0;
	char *ptr = (char *)_ptr;
	uint32 offset = EWPF_INVALID;
	uint16	version;

	if (info->is_multi_version == TRUE) {
		version = *(uint16 *)((char *)_ptr + info->v_info.version_offset);
		offset = ewpr_get_multi_offset(version, info);
	} else {
		offset = info->offset;
	}

	if (offset == EWPF_INVALID) {
		DHD_FILTER_ERR(("INVALID TYPE to OFFSET:%s\n", info->name));
		return 0;
	}

	ptr += offset;

	switch (info->data_type) {
		case EWP_INT8:
			sval = *(int8 *)ptr;
			break;
		case EWP_UINT8:
			sval = *(uint8 *)ptr;
			break;
		case EWP_INT16:
			sval = *(int16 *)ptr;
			break;
		case EWP_UINT16:
			sval = *(uint16 *)ptr;
			break;
		case EWP_INT32:
			sval = *(int32 *)ptr;
			break;
		case EWP_UINT32:
			sval = *(uint32 *)ptr;
			break;
		/* XXX UINT64 is used only for debug */
#ifdef EWPR_DEBUG
		case EWP_UINT64:
			sval = (uint32)(*(uint64 *)ptr);
			break;
#endif /* EWPR_DEBUG */
		case EWP_BIT:
		default:
			DHD_FILTER_ERR(("INVALID TYPE for Single Serial:%d", info->data_type));
			return 0;
	}

	return ewpr_serial_basic(buf, buf_len, sval, info->display_format, info->display_type, del);
}

int
ewpr_diff_serial(ewpr_serial_info_t *info,
	char *buf, int buf_len, void *_f_op, void *_s_op, char del)
{
	char *f_op = (char *)_f_op;
	char *s_op = (char *)_s_op;
	uint32 diff;
	uint32 offset = EWPF_INVALID;
	uint16	version;

	if (info->is_multi_version == TRUE) {
		version = *(uint16 *)(f_op + info->v_info.version_offset);
		offset = ewpr_get_multi_offset(version, info);
	} else {
		offset = info->offset;
	}

	if (offset == EWPF_INVALID) {
		DHD_FILTER_ERR(("INVALID TYPE to OFFSET:%s\n", info->name));
		return 0;
	}

	f_op = f_op + offset;
	s_op = s_op + offset;

	switch (info->data_type) {
		case EWP_INT8:
		case EWP_UINT8:
			diff = *(uint8 *)f_op - *(uint8 *)s_op;
			break;
		case EWP_INT16:
		case EWP_UINT16:
			diff = *(uint16 *)f_op - *(uint16 *)s_op;
			break;
		case EWP_INT32:
		case EWP_UINT32:
			diff = *(uint32 *)f_op - *(uint32 *)s_op;
			break;
		case EWP_UINT64:
			diff = (uint32)(*(uint64 *)f_op - *(uint64 *)s_op);
			break;
		case EWP_BIT:
		default:
			DHD_FILTER_ERR(("INVALID TYPE to DIFF:%d", info->data_type));
			return 0;
	}

	return ewpr_serial_basic(buf, buf_len, diff, info->display_format, info->display_type, del);
}

#ifdef EWPR_DEBUG
void
ewpr_debug_dump(ewpr_serial_info_t *tbl, void **ring)
{
	void *elem;
	int idx, idx2;
	ewpr_serial_info_t *info;
	char buf[EWPR_DEBUG_BUF_LEN];
	uint32 bytes_written;
	int lock_cnt;

	for (idx = 0; strlen(tbl[idx].name) != 0; idx++) {
		info = &tbl[idx];
#ifdef DHD_EWPR_VER2
		if (info->info_type != EWPF_INFO_ECNT) {
			DHD_FILTER_ERR(("%s: unable to dump value\n", info->name));
			break;
		}
#endif /* DHD_EWPR_VER2 */
		memset(buf, 0, sizeof(buf));
		lock_cnt = dhd_ring_lock_get_count(ring[info->ring_type - 1]);
		elem = dhd_ring_lock_get_first(ring[info->ring_type - 1]);
		bytes_written = scnprintf(buf, EWPR_DEBUG_BUF_LEN, "%s:", info->name);
		for (idx2 = 0; elem && (idx2 < lock_cnt); idx2++) {
			bytes_written += ewpr_single_serial(info, &buf[bytes_written],
				EWPR_DEBUG_BUF_LEN - bytes_written, elem, KEY_DEL);
			elem = dhd_ring_get_next(ring[info->ring_type - 1], elem);
		}
		DHD_FILTER_ERR(("%s\n", buf));
	}
}
#endif /* EWPR_DEBUG */

uint32
dhd_event_log_filter_serialize(dhd_pub_t *dhdp, char *in_buf, uint32 tot_len, int type)
{
	EWP_filter_t *filter = (EWP_filter_t *)dhdp->event_log_filter;
	void *ring[EWPF_MAX_IDX_TYPE];
	char *ret_buf = in_buf;
	int slice_id;
	int iface_id;
	int idx, idx2;
	uint32 bytes_written = 0;
	void *elem[EWPF_MAX_IDX_TYPE][EWPR_CSDCLIENT_DIFF];
	void **elem_list;
	int lock_cnt, lock_cnt2;
	char *last_print;
	void *arr_elem;
	uint32 delta_list[EWPR_CSDCLIENT_DIFF];
	ewpr_lock_param_t lock_param;
	int print_name = FALSE;
	char cookie_str[DEBUG_DUMP_TIME_BUF_LEN];
	char iov_buf[EWPR_IOV_BUF_LEN];

	if (type != 0) {
		DHD_FILTER_ERR(("NOT SUPPORTED TYPE: %d\n", type));
		return 0;
	}

	iface_id = 0; /* STA INTERFACE ONLY */
	if (filter->last_channel <= CH_MAX_2G_CHANNEL) {
		slice_id = EWPF_SLICE_AUX;
	} else {
		slice_id = EWPF_SLICE_MAIN;
	}
	ring[EWPF_IDX_TYPE_SLICE - 1] = filter->s_ring[slice_id];
	ring[EWPF_IDX_TYPE_IFACE - 1] = filter->i_ring[iface_id];

	/* Configure common LOCK parameter */
	lock_param.max_armcycle = (uint32)EWPF_INVALID;
	lock_param.min_armcycle = filter->last_armcycle;
	lock_param.max_period = (EWPR_ARRAY_CNT - 1)* EWPR_INTERVAL;
	lock_param.max_period *= EWPF_MSEC_TO_SEC * EWPF_ARM_TO_MSEC;
	lock_param.delta_cnt = ARRAYSIZE(delta_list);
	lock_param.delta_list = delta_list;

	delta_list[EWPR_DELTA3_POS] = EWPR_DELTA3_CNT;
	delta_list[EWPR_DELTA2_POS] = EWPR_DELTA2_CNT;
	delta_list[EWPR_DELTA1_POS] = EWPR_DELTA1_CNT;
	delta_list[EWPR_NOW_POS] = 0;
	lock_param.ring = ring[EWPF_IDX_TYPE_IFACE -1];
	lock_param.elem_list = elem[EWPF_IDX_TYPE_IFACE -1];
	lock_cnt = ewpr_set_period_lock(&lock_param);
	if (lock_cnt <= 0) {
		DHD_FILTER_ERR(("FAIL TO GET IFACE LOCK: %d\n", iface_id));
		bytes_written = 0;
		goto finished;
	}

	delta_list[EWPR_DELTA3_POS] = EWPR_DELTA3_CNT;
	delta_list[EWPR_DELTA2_POS] = EWPR_DELTA2_CNT;
	delta_list[EWPR_DELTA1_POS] = EWPR_DELTA1_CNT;
	delta_list[EWPR_NOW_POS] = 0;
	lock_param.ring = ring[EWPF_IDX_TYPE_SLICE -1];
	lock_param.elem_list = elem[EWPF_IDX_TYPE_SLICE -1];
	lock_cnt2 = ewpr_set_period_lock(&lock_param);
	if (lock_cnt2 <= 0) {
		DHD_FILTER_ERR(("FAIL TO GET SLICE LOCK: %d\n", slice_id));
		goto finished;
	}

	if (lock_cnt != lock_cnt2) {
		DHD_FILTER_ERR(("Lock Count is Diff: iface:%d slice:%d\n", lock_cnt, lock_cnt2));
		lock_cnt = MIN(lock_cnt, lock_cnt2);
	}

#ifdef EWPR_DEBUG
	print_name = TRUE;
	ewpr_debug_dump(ewpr_serial_dbg_tbl, ring);
	ewpr_debug_dump(ewpr_serial_CSDCLIENT_diff_tbl, ring);
	ewpr_debug_dump(ewpr_serial_CSDCLIENT_array_tbl, ring);
#endif /* EWPR_DEBUG */

	memset(ret_buf, 0, tot_len);
	memset(cookie_str, 0, DEBUG_DUMP_TIME_BUF_LEN);
	bytes_written = 0;
	last_print = ret_buf;

	/* XXX Counters BIG DATA not matched to file yet */
	get_debug_dump_time(cookie_str);
#ifdef DHD_LOG_DUMP
	dhd_logdump_cookie_save(dhdp, cookie_str, "ECNT");
#endif

	/* KEY DATA */
	bytes_written += scnprintf(&ret_buf[bytes_written],
		tot_len - bytes_written, "%08x", EWP_REPORT_VERSION);
	bytes_written += scnprintf(&ret_buf[bytes_written],
		tot_len - bytes_written, "%c%s", KEY_DEL, cookie_str);
	DHD_FILTER_ERR(("%d: %s\n", bytes_written, last_print));
	last_print = &ret_buf[bytes_written];

	for (idx = 0; strlen(ewpr_serial_CSDCLIENT_key_tbl[idx].name) != 0; idx++) {
		ewpr_serial_info_t *info = &ewpr_serial_CSDCLIENT_key_tbl[idx];
		elem_list = elem[info->ring_type - 1];
		if (print_name) {
			bytes_written += scnprintf(&ret_buf[bytes_written],
				tot_len - bytes_written, " %s:", info->name);
		}
		bytes_written += ewpr_diff_serial(info, &ret_buf[bytes_written],
			tot_len - bytes_written,
			elem_list[EWPR_NOW_POS],
			elem_list[EWPR_DELTA1_POS],
			KEY_DEL);
		if ((idx + 1) % EWPR_CNT_PER_LINE == 0) {
			DHD_FILTER_ERR(("%d:%s\n", bytes_written, last_print));
			last_print = &ret_buf[bytes_written];
		}
	}

	/* RAW DATA */
	/* XXX FIRST data shall use space:KEY delimiter */
	bytes_written += scnprintf(&ret_buf[bytes_written],
		tot_len - bytes_written, "%c%08x", KEY_DEL, EWP_REPORT_VERSION);
	bytes_written += scnprintf(&ret_buf[bytes_written],
		tot_len - bytes_written, "%c%s", RAW_DEL, cookie_str);

	for (idx = 0; strlen(ewpr_serial_CSDCLIENT_diff_tbl[idx].name) != 0; idx++) {
		ewpr_serial_info_t *info = &ewpr_serial_CSDCLIENT_diff_tbl[idx];
		elem_list = elem[info->ring_type - 1];
		if (print_name) {
			bytes_written += scnprintf(&ret_buf[bytes_written],
				tot_len - bytes_written, " %s:", info->name);
		}
		bytes_written += ewpr_diff_serial(info, &ret_buf[bytes_written],
			tot_len - bytes_written,
			elem_list[EWPR_NOW_POS],
			elem_list[EWPR_DELTA1_POS],
			RAW_DEL);
		bytes_written += ewpr_diff_serial(info, &ret_buf[bytes_written],
			tot_len - bytes_written,
			elem_list[EWPR_DELTA1_POS],
			elem_list[EWPR_DELTA2_POS],
			RAW_DEL);
		if ((idx + 1) % EWPR_CNT_PER_LINE == 0) {
			DHD_FILTER_ERR(("%d:%s\n", bytes_written, last_print));
			last_print = &ret_buf[bytes_written];
		}
	}

	/* FILL BSS SPECIFIC DATA LATER */
	if (dhd_iovar(dhdp, 0, "auth", NULL, 0, iov_buf, ARRAYSIZE(iov_buf), FALSE) < 0) {
		DHD_FILTER_ERR(("fail to get auth\n"));
		*(uint32 *)iov_buf = EWPF_INVALID;

	}
	bytes_written += scnprintf(&ret_buf[bytes_written],
			tot_len - bytes_written, "%c%08x", RAW_DEL, *(uint32 *)iov_buf);

	if (dhd_iovar(dhdp, 0, "wsec", NULL, 0, iov_buf, ARRAYSIZE(iov_buf), FALSE) < 0) {
		DHD_FILTER_ERR(("fail to get wsec\n"));
		*(uint32 *)iov_buf = EWPF_INVALID;

	}
	bytes_written += scnprintf(&ret_buf[bytes_written],
			tot_len - bytes_written, "%c%08x", RAW_DEL, *(uint32 *)iov_buf);

	if (dhd_iovar(dhdp, 0, "mfp", NULL, 0, iov_buf, ARRAYSIZE(iov_buf), FALSE) < 0) {
		DHD_FILTER_ERR(("fail to get mfp\n"));
		*(uint8 *)iov_buf = EWPF_INVALID;

	}
	bytes_written += scnprintf(&ret_buf[bytes_written],
			tot_len - bytes_written, "%c%02x", RAW_DEL, *(uint8 *)iov_buf);

	if (dhd_iovar(dhdp, 0, "bip", NULL, 0, iov_buf, ARRAYSIZE(iov_buf), FALSE) < 0) {
		DHD_FILTER_ERR(("fail to get bip\n"));
		*(uint8 *)iov_buf = EWPF_INVALID;
	}
	bytes_written += scnprintf(&ret_buf[bytes_written],
			tot_len - bytes_written, "%c%02x", RAW_DEL, *(uint8 *)iov_buf);

	for (idx = 0; strlen(ewpr_serial_CSDCLIENT_array_tbl[idx].name) != 0; idx++) {
		ewpr_serial_info_t *info = &ewpr_serial_CSDCLIENT_array_tbl[idx];
		if (print_name) {
			bytes_written += scnprintf(&ret_buf[bytes_written],
				tot_len - bytes_written, " %s:", info->name);
		}
		for (idx2 = 0; idx2 < EWPR_ARRAY_CNT - lock_cnt; idx2++) {
			bytes_written += ewpr_serial_basic(&ret_buf[bytes_written],
				tot_len - bytes_written, 0,
				info->display_format, info->display_type, RAW_DEL);
		}
		arr_elem = elem[info->ring_type - 1][EWPR_DELTA3_POS];
		for (; idx2 < EWPR_ARRAY_CNT; idx2++) {
			if (arr_elem == NULL) {
				DHD_FILTER_ERR(("ARR IS NULL : %d %p \n",
					idx2, elem[info->ring_type - 1][EWPR_DELTA3_POS]));
				break;
			}
			bytes_written += ewpr_single_serial(info, &ret_buf[bytes_written],
				tot_len - bytes_written, arr_elem, RAW_DEL);
			arr_elem = dhd_ring_get_next(ring[info->ring_type - 1], arr_elem);
		}
		DHD_FILTER_ERR(("%d:%s\n", bytes_written, last_print));
		last_print = &ret_buf[bytes_written];
	}

finished:
	DHD_FILTER_ERR(("RET LEN:%d\n", (int)strlen(ret_buf)));
	dhd_ring_lock_free(ring[EWPF_IDX_TYPE_SLICE - 1]);
	dhd_ring_lock_free(ring[EWPF_IDX_TYPE_IFACE - 1]);
	return bytes_written;
}

int
ewpr_set_period_lock(ewpr_lock_param_t *param)
{
	void *last;
	void *first;
	void *cur;
	int lock_cnt;
	int idx2;
	int delta_idx;
	uint32 last_armcycle;
	uint32 first_armcycle;
	uint32 cur_armcycle = 0;
	void *ring = param->ring;

	/* GET LATEST PTR */
	last = dhd_ring_get_last(ring);
	while (TRUE) {
		if (last == NULL) {
			DHD_FILTER_ERR(("NO LAST\n"));
			return -1;
		}
		last_armcycle = *(uint32 *)last;
		if (last_armcycle <= param->max_armcycle ||
			last_armcycle + EWPF_EPOCH >= param->max_armcycle) {
			break;
		}
		last = dhd_ring_get_prev(ring, last);
	}

	if (last_armcycle != param->max_armcycle) {
		DHD_FILTER_TRACE(("MAX ARMCYCLE IS CHANGEd new:%u prev:%u\n",
			last_armcycle, param->max_armcycle));
		param->max_armcycle = last_armcycle;
	}

	if (last_armcycle < param->min_armcycle) {
		param->min_armcycle = 0;
	}

	/* GET FIRST PTR */
	first_armcycle = last_armcycle;
	first = last;
	while (TRUE) {
		cur = dhd_ring_get_prev(ring, first);
		if (cur == NULL) {
			break;
		}
		cur_armcycle = *(uint32 *)cur;
		if (cur_armcycle >= first_armcycle) {
			DHD_FILTER_TRACE(("case 1: %u %u\n", first_armcycle, cur_armcycle));
			/* dongle is rebooted */
			break;
		}
		if (cur_armcycle + EWPF_EPOCH < param->min_armcycle) {
			DHD_FILTER_TRACE(("case 2: %u %u\n", param->min_armcycle, cur_armcycle));
			/* Reach Limitation */
			break;
		}
		if (cur_armcycle + param->max_period + EWPF_EPOCH < last_armcycle) {
			DHD_FILTER_TRACE(("case 3: %u %u\n", param->max_period, cur_armcycle));
			/* exceed max period */
			break;
		}
		first = cur;
		first_armcycle = cur_armcycle;
	}

	if (first_armcycle != param->min_armcycle) {
		DHD_FILTER_TRACE(("MIN ARMCYCLE IS CHANGEd new:%u prev:%u %u\n",
			first_armcycle, param->min_armcycle, cur_armcycle));
		param->min_armcycle = first_armcycle;
	}

	DHD_FILTER_TRACE(("ARM CYCLE of first(%u), last(%u)\n", first_armcycle, last_armcycle));

	dhd_ring_lock(ring, first, last);

	lock_cnt = dhd_ring_lock_get_count(ring);
	if (lock_cnt <= 0) {
		DHD_FILTER_ERR((" NO VALID RECORD : %d\n", lock_cnt));
		return -1;
	}
	DHD_FILTER_TRACE(("Lock Count:%d\n", lock_cnt));

	/* Validate delta position */
	for (idx2 = 0; idx2 < param->delta_cnt - 1; idx2++) {
		if (param->delta_list[idx2] >= param->delta_list[idx2 + 1]) {
			DHD_FILTER_ERR(("INVALID DELTA at %d\n", idx2 + 1));
			param->delta_list[idx2 + 1] = param->delta_list[idx2];
		}
	}

	delta_idx = 0;
	for (idx2 = 0; idx2 < lock_cnt && delta_idx < param->delta_cnt; idx2++) {
		if (idx2 == 0) {
			cur = dhd_ring_lock_get_last(ring);
		} else {
			cur = dhd_ring_get_prev(ring, cur);
		}

		if (idx2 >= param->delta_list[delta_idx]) {
			param->elem_list[delta_idx] = cur;
			delta_idx ++;
		}
	}

	/* COPY last elem to rest of the list */
	delta_idx--;
	for (idx2 = delta_idx + 1; idx2 < param->delta_cnt; idx2++) {
		param->elem_list[idx2] = cur;
	}
	return lock_cnt;
}

#ifdef DHD_EWPR_VER2
static int
ewpr_single_bit_pack(ewpr_serial_info_t * info, char * buf, int buf_len,
	void * _ptr, int bit_offset)
{
	int32 sval = 0;
	char *ptr = (char *)_ptr;
	uint32 offset = EWPF_INVALID;
	uint16	version;
	bool is_signed = FALSE;

	if (info->is_multi_version == TRUE) {
		version = *(uint16 *)((char *)_ptr + info->v_info.version_offset);
		offset = ewpr_get_multi_offset(version, info);
	} else {
		offset = info->offset;
	}

	if (offset == EWPF_INVALID) {
		DHD_FILTER_ERR(("INVALID TYPE to OFFSET:%s\n", info->name));
		return 0;
	}

	ptr += offset;

	switch (info->data_type) {
		case EWP_INT8:
			sval = *(int8 *)ptr;
			is_signed = TRUE;
			break;
		case EWP_UINT8:
			sval = *(uint8 *)ptr;
			break;
		case EWP_INT16:
			sval = *(int16 *)ptr;
			is_signed = TRUE;
			break;
		case EWP_UINT16:
			sval = *(uint16 *)ptr;
			break;
		case EWP_INT32:
			sval = *(int32 *)ptr;
			is_signed = TRUE;
			break;
		case EWP_UINT32:
			sval = *(uint32 *)ptr;
			break;
#ifdef EWPR_DEBUG
		case EWP_UINT64:
			sval = (int32)(*(uint64 *)ptr);
			break;
#endif /* EWPR_DEBUG */
		default:
			DHD_FILTER_ERR(("INVALID TYPE for Single Serial:%d", info->data_type));
			return 0;
	}

	/* convert negative value to positive before bit packing */
	if (is_signed) {
		if (sval < 0) {
			DHD_FILTER_TRACE(("convert to positive value %d\n", sval));
			sval = ABS(sval);
		}
	}

	if (info->unit_convert > 1) {
		DHD_FILTER_TRACE(("convert unit %d / %d\n", sval, info->unit_convert));
		sval = sval / info->unit_convert;
	}

	if (is_signed) {
		DHD_FILTER_TRACE(("%s : signed value : %d, bit length: %d",
			info->name, sval, info->display_bit_length));
	} else {
		DHD_FILTER_TRACE(("%s : unsigned value : %u, bit length: %d",
			info->name, sval, info->display_bit_length));
	}

	return ewpr_bit_pack_basic(buf, buf_len, sval, info->display_format,
			info->display_type, info->display_bit_length, bit_offset);
}

static int
ewpr_diff_bit_pack(ewpr_serial_info_t *info, char *buf, int buf_len,
	void *_f_op, void *_s_op, int bit_offset)
{
	char *f_op = (char *)_f_op;
	char *s_op = (char *)_s_op;
	int32 diff;
	uint32 offset = EWPF_INVALID;
	uint16	version;

	if (info->is_multi_version == TRUE) {
		version = *(uint16 *)(f_op + info->v_info.version_offset);
		offset = ewpr_get_multi_offset(version, info);
	} else {
		offset = info->offset;
	}

	if (offset == EWPF_INVALID) {
		DHD_FILTER_ERR(("INVALID TYPE to OFFSET:%s\n", info->name));
		return 0;
	}

	f_op = f_op + offset;
	s_op = s_op + offset;

	switch (info->data_type) {
		case EWP_INT8:
		case EWP_UINT8:
			diff = *(uint8 *)f_op - *(uint8 *)s_op;
			break;
		case EWP_INT16:
		case EWP_UINT16:
			diff = *(uint16 *)f_op - *(uint16 *)s_op;
			break;
		case EWP_INT32:
		case EWP_UINT32:
			diff = *(uint32 *)f_op - *(uint32 *)s_op;
			break;
		case EWP_UINT64:
			diff = (uint32)(*(uint64 *)f_op - *(uint64 *)s_op);
			break;
		default:
			DHD_FILTER_ERR(("INVALID TYPE to DIFF:%d", info->data_type));
			return 0;
	}

	if (diff < 0) {
		DHD_FILTER_TRACE(("convert to positive value %d\n", diff));
		diff = ABS(diff);
	}

	if (info->unit_convert > 1) {
		DHD_FILTER_TRACE(("convert unit %d / %d\n", diff, info->unit_convert));
		diff = diff / info->unit_convert;
	}

	DHD_FILTER_TRACE(("%s : value : %d, bit length: %d",
		info->name, diff, info->display_bit_length));
	return ewpr_bit_pack_basic(buf, buf_len, diff, info->display_format,
		info->display_type, info->display_bit_length, bit_offset);
}

static int
ewpr_bit_pack_basic(char *buf, int buf_len, uint32 data, int format, int display_type,
	int display_bit_length, int bit_offset)
{
	if (format == EWP_BIN) {
		uint32 sdata = (uint32) data;
		switch (display_type) {
			case EWP_BIT:
				/* call bit packing */
				return dhd_bit_pack(buf, buf_len, bit_offset,
					sdata, display_bit_length);
			default:
				DHD_FILTER_ERR(("INVALID TYPE for Serial:%d", display_type));
				return 0;
		}
	}

	DHD_FILTER_ERR(("INVALID FORMAT for Serial:%d", format));
	return 0;
}

static ewpr_serial_info_t*
ewpr_find_context_info(int index1, int index2, int index3)
{
	int idx = 0;
	ewpr_serial_info_t *context_info = NULL;

	for (idx = 0; ewpr_serial_context_info[idx].table != NULL; idx++) {
		if (index1 == ewpr_serial_context_info[idx].index1 &&
				index2 == ewpr_serial_context_info[idx].index2 &&
				index3 == ewpr_serial_context_info[idx].index3) {
			context_info = ewpr_serial_context_info[idx].table;
			break;
		}
	}

	if (context_info == NULL) {
		DHD_FILTER_ERR(("unable to find context info for index number: %02x:%02x:%02x\n",
			index1, index2, index3));
		return NULL;
	}

	return context_info;
}

static int
ewpr_find_context_type(ewpr_serial_info_t* context_info)
{
	int idx = 0;
	int context_type = BCME_ERROR;

	/* index2, index3 are reserved */

	for (idx = 0; ewpr_serial_context_info[idx].table != NULL; idx++) {
		if (context_info == ewpr_serial_context_info[idx].table) {
			context_type = idx;
			break;
		}
	}

	return context_type;
}

static uint32
ewpr_scnprintf(char *buf, uint32 buf_len, uint32 input_len, char *data_type, char *fmt, ...)
{
	va_list args;

	if (buf_len < input_len) {
		DHD_FILTER_ERR(("%s: input length(%d) is larger than "
			"remain buffer length(%d)\n", data_type,
			input_len, buf_len));
	}
	va_start(args, fmt);
	buf_len = vscnprintf(buf, buf_len, fmt, args);
	va_end(args);

	return buf_len;
}

uint32
dhd_event_log_filter_serialize_bit(dhd_pub_t *dhdp, char *in_buf, uint32 tot_len,
	int index1, int index2, int index3)
{
	EWP_filter_t *filter = (EWP_filter_t *)dhdp->event_log_filter;
	void *ring[EWPF_MAX_IDX_TYPE];
	char *ret_buf = in_buf;
	int slice_id;
	int iface_id;
	int event_id;
	int key_info_id;
	int idx;
	int idx2;
	uint32 bytes_written = 0;
	int bits_written = 0;
	void *elem[EWPF_MAX_IDX_TYPE][EWPR_CSDCLIENT_DIFF];
	void **elem_list;
	int lock_cnt, lock_cnt2;
	uint32 delta_list[EWPR_CSDCLIENT_DIFF];
	ewpr_lock_param_t lock_param;
	char cookie_str[DEBUG_DUMP_TIME_BUF_LEN];
	char iov_buf[EWPR_IOV_BUF_LEN];
	char *raw_buf = NULL;
	char *raw_encode_buf = NULL;
	int raw_buf_size;
	int ret = 0;
	ewpr_serial_info_t *context_info = NULL;
	int context_type;
#ifdef DHD_STATUS_LOGGING
	uint32 conv_cnt = 0;
#endif /* DHD_STATUS_LOGGING */

#ifdef DHD_STATUS_LOGGING
	stat_elem_t dhd_stat[EWP_DHD_STAT_SIZE];
	stat_query_t query;

	memset(&dhd_stat[0], 0x00, sizeof(stat_elem_t) * EWP_DHD_STAT_SIZE);
#endif /* DHD_STATUS_LOGGING */

	context_info = ewpr_find_context_info(index1, index2, index3);
	if (!context_info) {
		return bytes_written;
	}

	if (tot_len < EWPR_MAX_STR_SIZE) {
		DHD_FILTER_ERR(("%s: insufficient buffer size %d\n",
			__FUNCTION__, tot_len));
		return bytes_written;
	}

	iface_id = 0; /* STA INTERFACE ONLY */
	event_id = 0; /* COMMON ID */
	key_info_id = 0; /* COMMON ID */
	if (filter->last_channel <= CH_MAX_2G_CHANNEL) {
		slice_id = EWPF_SLICE_AUX;
	} else {
		slice_id = EWPF_SLICE_MAIN;
	}
	ring[EWPF_IDX_TYPE_SLICE - 1] = filter->s_ring[slice_id];
	ring[EWPF_IDX_TYPE_IFACE - 1] = filter->i_ring[iface_id];
	ring[EWPF_IDX_TYPE_EVENT - 1] = filter->e_ring[event_id];
	ring[EWPF_IDX_TYPE_KEY_INFO - 1] = filter->k_ring[key_info_id];

	/* Configure common LOCK parameter */
	lock_param.max_armcycle = (uint32)EWPF_INVALID;
	lock_param.min_armcycle = filter->last_armcycle;
	lock_param.max_period = (EWPR_ARRAY_CNT - 1)* EWPR_INTERVAL;
	lock_param.max_period *= EWPF_MSEC_TO_SEC * EWPF_ARM_TO_MSEC;
	lock_param.delta_cnt = ARRAYSIZE(delta_list);
	lock_param.delta_list = delta_list;

	for (idx = 0; idx < EWPR_CSDCLIENT_DIFF; idx++) {
		delta_list[idx] = idx * EWPR_DELTA_CNT;
	}

	lock_param.ring = ring[EWPF_IDX_TYPE_IFACE -1];
	lock_param.elem_list = elem[EWPF_IDX_TYPE_IFACE -1];
	lock_cnt = ewpr_set_period_lock(&lock_param);
	if (lock_cnt <= 0) {
		DHD_FILTER_ERR(("FAIL TO GET IFACE LOCK: %d\n", iface_id));
		bytes_written = 0;
		goto finished;
	}

	for (idx = 0; idx < EWPR_CSDCLIENT_DIFF; idx++) {
		delta_list[idx] = idx * EWPR_DELTA_CNT;
	}

	lock_param.ring = ring[EWPF_IDX_TYPE_SLICE -1];
	lock_param.elem_list = elem[EWPF_IDX_TYPE_SLICE -1];
	lock_cnt2 = ewpr_set_period_lock(&lock_param);
	if (lock_cnt2 <= 0) {
		DHD_FILTER_ERR(("FAIL TO GET SLICE LOCK: %d\n", slice_id));
		goto finished;
	}

	if (lock_cnt != lock_cnt2) {
		DHD_FILTER_ERR(("Lock Count is Diff: iface:%d slice:%d\n", lock_cnt, lock_cnt2));
		lock_cnt = MIN(lock_cnt, lock_cnt2);
	}

	for (idx = 0; idx < EWPR_CSDCLIENT_DIFF; idx++) {
		delta_list[idx] = idx * EWPR_DELTA_CNT;
	}

	lock_param.ring = ring[EWPF_IDX_TYPE_EVENT -1];
	lock_param.elem_list = elem[EWPF_IDX_TYPE_EVENT -1];
	lock_cnt = ewpr_set_period_lock(&lock_param);
	if (lock_cnt <= 0) {
		DHD_FILTER_ERR(("FAIL TO GET EVENT ECNT LOCK: %d\n", iface_id));
		bytes_written = 0;
		goto finished;
	}

	for (idx = 0; idx < EWPR_CSDCLIENT_DIFF; idx++) {
		delta_list[idx] = idx * EWPR_DELTA_CNT;
	}

	lock_param.ring = ring[EWPF_IDX_TYPE_KEY_INFO -1];
	lock_param.elem_list = elem[EWPF_IDX_TYPE_KEY_INFO -1];
	lock_cnt = ewpr_set_period_lock(&lock_param);
	if (lock_cnt <= 0) {
		DHD_FILTER_ERR(("FAIL TO GET KEY INFO LOCK: %d\n", iface_id));
		bytes_written = 0;
		goto finished;
	}

#ifdef EWPR_DEBUG
	ewpr_debug_dump(context_info, ring);
#endif /* EWPR_DEBUG */

	memset(ret_buf, 0, tot_len);
	memset(cookie_str, 0, DEBUG_DUMP_TIME_BUF_LEN);
	bytes_written = 0;
	bits_written = 0;

	/* XXX Counters BIG DATA not matched to file yet */
	get_debug_dump_time(cookie_str);
#ifdef DHD_LOG_DUMP
	dhd_logdump_cookie_save(dhdp, cookie_str, "ECNT");
#endif /* DHD_LOG_DUMP */

	/* KEY DATA */
	bytes_written += ewpr_scnprintf(&ret_buf[bytes_written],
		tot_len - bytes_written, REPORT_VERSION_STR_SIZE,
		"report version", "%08x", EWP_REPORT_VERSION);

	bytes_written += ewpr_scnprintf(&ret_buf[bytes_written],
		tot_len - bytes_written, DELIMITER_LEN + strlen(cookie_str),
		"cookie string", "%c%s", KEY_DEL, cookie_str);

	bytes_written += ewpr_scnprintf(&ret_buf[bytes_written],
		tot_len - bytes_written, DELIMITER_LEN + INDEX_STR_SIZE,
		"host trigger index", "%c%02x%02x%02x", KEY_DEL, index1, index2, index3);

#ifdef DHD_STATUS_LOGGING
	DHD_FILTER_TRACE(("print dhd_stat size:%d * %d, size of filter list: %d\n",
		(uint32)sizeof(dhd_stat[0]), EWP_DHD_STAT_SIZE,
		(uint32)sizeof(dhd_statlog_filter)));
	query.req_buf = NULL;
	query.req_buf_len = 0;
	query.resp_buf = (char *)dhd_stat;
	query.resp_buf_len = DHD_STATLOG_RING_SIZE(EWP_DHD_STAT_SIZE);
	query.req_num = EWP_DHD_STAT_SIZE;
	ret = dhd_statlog_get_latest_info(dhdp, (void *)&query);
	if (ret < 0) {
		DHD_FILTER_ERR(("fail to get dhd statlog - %d\n", ret));
	}
#ifdef EWPR_DEBUG
	for (idx = 0; idx < EWP_DHD_STAT_SIZE; idx++) {
		DHD_FILTER_TRACE(("DHD status index: %d, timestamp: %llu, stat: %d\n",
			idx, dhd_stat[idx].ts, dhd_stat[idx].stat));
	}
#endif
	bytes_written += ewpr_scnprintf(&ret_buf[bytes_written],
		tot_len - bytes_written, DELIMITER_LEN + DHD_STAT_STR_SIZE,
		"current dhd status", "%c%02x", KEY_DEL, dhd_stat[0].stat);

	bytes_written += ewpr_scnprintf(&ret_buf[bytes_written],
		tot_len - bytes_written, DELIMITER_LEN + DHD_STAT_STR_SIZE,
		"previous dhd status", "%c%02x", KEY_DEL, dhd_stat[1].stat);
#else
	/* reserved for dhd status information */
	bytes_written += ewpr_scnprintf(&ret_buf[bytes_written],
		tot_len - bytes_written, DELIMITER_LEN + DHD_STAT_STR_SIZE,
		"current dhd status", "%c%02x", KEY_DEL, 0x00);

	bytes_written += ewpr_scnprintf(&ret_buf[bytes_written],
		tot_len - bytes_written, DELIMITER_LEN + DHD_STAT_STR_SIZE,
		"previous dhd status", "%c%02x", KEY_DEL, 0x00);
#endif /* DHD_STATUS_LOGGING */

	/* RAW DATA */
	raw_buf = MALLOCZ(dhdp->osh, RAW_BUFFER_SIZE);

	for (idx = 0; strlen(context_info[idx].name) != 0; idx++) {
		ewpr_serial_info_t *info = &context_info[idx];
		elem_list = elem[info->ring_type - 1];
		DHD_FILTER_TRACE(("%s : array_size: %d\n", info->name, info->display_array_size));
		switch (info->info_type) {
			case EWPF_INFO_VER:
				DHD_FILTER_TRACE(("write %s - value: %d\n", info->name,
					EWP_REPORT_VERSION));
				bits_written = dhd_bit_pack(raw_buf, RAW_BUFFER_SIZE, bits_written,
					EWP_REPORT_VERSION, info->display_bit_length);
				break;
			case EWPF_INFO_TYPE:
				context_type = ewpr_find_context_type(context_info);
				if (context_type < 0) {
					DHD_FILTER_ERR(("fail to get context_type - %d\n",
						context_type));
					break;
				}
				DHD_FILTER_TRACE(("write %s - value: %d\n", info->name,
					(uint32)context_type));
				bits_written = dhd_bit_pack(raw_buf, RAW_BUFFER_SIZE, bits_written,
					(uint32)context_type, info->display_bit_length);
				break;
			case EWPF_INFO_DHDSTAT:
				if (strcmp("dhdstat_last_ts", info->name) == 0) {
#ifdef DHD_STATUS_LOGGING
					if (info->unit_convert > 1) {
						conv_cnt = dhd_stat[0].ts_tz / info->unit_convert;
					} else {
						conv_cnt = dhd_stat[0].ts_tz;
					}
					DHD_FILTER_TRACE(("DHD status last timestamp:"
						" %llu, %u", dhd_stat[0].ts_tz,
						conv_cnt));
					bits_written = dhd_bit_pack(raw_buf, RAW_BUFFER_SIZE,
						bits_written, conv_cnt,
						info->display_bit_length);
#else
					DHD_FILTER_TRACE(("No DHD status log timestamp\n"));
					bits_written = dhd_bit_pack(raw_buf, RAW_BUFFER_SIZE,
						bits_written, 0x00, info->display_bit_length);
#endif /* DHD_STATUS_LOGGING */
				} else if (strcmp("dhdstat_last", info->name) == 0) {
#ifdef DHD_STATUS_LOGGING
					DHD_FILTER_TRACE(("DHD status last stat: %d(0x%02x)",
						dhd_stat[0].stat, dhd_stat[0].stat));
					bits_written = dhd_bit_pack(raw_buf, RAW_BUFFER_SIZE,
						bits_written, (uint32)dhd_stat[0].stat,
						info->display_bit_length);
#else
					DHD_FILTER_TRACE(("No DHD status log value\n"));
					bits_written = dhd_bit_pack(raw_buf, RAW_BUFFER_SIZE,
						bits_written, 0x00, info->display_bit_length);
#endif /* DHD_STATUS_LOGGING */
				} else if (strcmp("dhdstat_prev_ts", info->name) == 0) {
#ifdef DHD_STATUS_LOGGING
					if (info->unit_convert > 1) {
						conv_cnt = dhd_stat[1].ts_tz / info->unit_convert;
					} else {
						conv_cnt = dhd_stat[1].ts_tz;
					}
					DHD_FILTER_TRACE(("DHD status prev timestamp:"
						" %llu, %u", dhd_stat[1].ts_tz,
						conv_cnt));
					bits_written = dhd_bit_pack(raw_buf, RAW_BUFFER_SIZE,
						bits_written, conv_cnt,
						info->display_bit_length);
#else
					DHD_FILTER_TRACE(("No DHD status log timestamp\n"));
					bits_written = dhd_bit_pack(raw_buf, RAW_BUFFER_SIZE,
						bits_written, 0x00, info->display_bit_length);
#endif /* DHD_STATUS_LOGGING */
				} else if (strcmp("dhdstat_prev", info->name) == 0) {
#ifdef DHD_STATUS_LOGGING
					DHD_FILTER_TRACE(("DHD status prev stat: %d(0x%02x)",
						dhd_stat[1].stat, dhd_stat[1].stat));
					bits_written = dhd_bit_pack(raw_buf, RAW_BUFFER_SIZE,
						bits_written, (uint32)dhd_stat[1].stat,
						info->display_bit_length);
#else
					DHD_FILTER_TRACE(("No DHD status log value\n"));
					bits_written = dhd_bit_pack(raw_buf, RAW_BUFFER_SIZE,
						bits_written, 0x00, info->display_bit_length);
#endif /* DHD_STATUS_LOGGING */
				} else {
					DHD_FILTER_ERR(("unknown dhdstat name - %s\n",
						info->name));
				}
				break;
			case EWPF_INFO_ECNT:
				for (idx2 = 0; idx2 < info->display_array_size; idx2++) {
					if (info->display_method == EWPR_DISPLAY_METHOD_SINGLE) {
						bits_written = ewpr_single_bit_pack(info, raw_buf,
							RAW_BUFFER_SIZE, elem_list[idx2],
							bits_written);
					} else {
						bits_written = ewpr_diff_bit_pack(info, raw_buf,
							RAW_BUFFER_SIZE, elem_list[idx2],
							elem_list[idx2+1], bits_written);
					}
				}
				break;
			case EWPF_INFO_IOVAR:
				if (dhd_iovar(dhdp, 0, info->name, NULL, 0, iov_buf,
						ARRAYSIZE(iov_buf), FALSE) < 0) {
					DHD_FILTER_ERR(("fail to get auth\n"));
					*(uint32 *)iov_buf = EWPF_INVALID;
				}
				DHD_FILTER_TRACE(("write %s - value: %d\n", info->name,
					*(uint8 *)iov_buf));
				bits_written = dhd_bit_pack(raw_buf, RAW_BUFFER_SIZE,
					bits_written, *(uint8 *)iov_buf,
					info->display_bit_length);
				break;
			case EWPF_INFO_CPLOG:
				DHD_FILTER_TRACE(("write compact packt log\n"));
				ret = 0;
#if defined(DHD_PKT_LOGGING) && defined(DHD_COMPACT_PKT_LOG)
				ret = dhd_cpkt_log_proc(dhdp, raw_buf, RAW_BUFFER_SIZE,
					bits_written, info->display_array_size);
#endif /* DHD_PKT_LOGGING && DHD_COMPACT_PKT_LOG */
				if (ret < 0) {
					DHD_FILTER_ERR(("fail to get compact packet log - %d\n",
						ret));
					break;
				}
				/* update bit offset */
				DHD_FILTER_TRACE(("%d bits written\n", ret));
				if (ret > 0) {
					bits_written = ret;
				}
				break;
			default:
				DHD_FILTER_ERR(("unsupported info type\n"));
				break;
		}
		DHD_FILTER_TRACE(("%d bits written\n", bits_written));
	}

	/* encode data */
	raw_buf_size = BYTE_SIZE(bits_written);
	raw_encode_buf = ewpr_base64_encode(dhdp, raw_buf, raw_buf_size);

#ifdef EWPR_DEBUG
	DHD_FILTER_ERR(("raw_buf:\n"));
	for (idx = 0; idx < raw_buf_size; idx++) {
		ewpr_print_byte_as_bits(raw_buf[idx]);
	}
#endif /* EWPR_DEBUG */
	DHD_FILTER_TRACE(("base64 encoding result:\n"));
	DHD_FILTER_TRACE(("%s", raw_encode_buf));

	bytes_written += ewpr_scnprintf(&ret_buf[bytes_written],
		tot_len - bytes_written, DELIMITER_LEN + strlen(raw_encode_buf),
		"base64 encoded raw data", "%c%s", KEY_DEL, raw_encode_buf);

finished:
	DHD_FILTER_ERR(("RET LEN:%d\n", (int)strlen(ret_buf)));
	DHD_FILTER_TRACE(("ret_buf: %s", ret_buf));

	dhd_ring_lock_free(ring[EWPF_IDX_TYPE_SLICE - 1]);
	dhd_ring_lock_free(ring[EWPF_IDX_TYPE_IFACE - 1]);
	dhd_ring_lock_free(ring[EWPF_IDX_TYPE_EVENT - 1]);
	dhd_ring_lock_free(ring[EWPF_IDX_TYPE_KEY_INFO - 1]);

	MFREE(dhdp->osh, raw_buf, RAW_BUFFER_SIZE);
	MFREE(dhdp->osh, raw_encode_buf, BASE64_BUFFER_SIZE);
	return bytes_written;
}

#ifdef EWPR_DEBUG
static void
ewpr_print_byte_as_bits(char val)
{
	int32 idx;
	char buf[EWPR_DEBUG_BUF_LEN];
	for (idx = 0; idx < MAX_BIT_SIZE; idx++) {
		scnprintf(&buf[idx], EWPR_DEBUG_BUF_LEN-idx, "%c",
			(val & (1 << (MAX_BIT_SHIFT-idx))) ? '1' : '0');
	}
	buf[MAX_BIT_SIZE] = 0x0;
	DHD_FILTER_ERR(("%s\n", buf));
}
#endif /* EWPR_DEBUG */

static char*
ewpr_base64_encode(dhd_pub_t *dhdp, char* input, int32 length)
{
	/* set up a destination buffer large enough to hold the encoded data */
	char *output = MALLOCZ(dhdp->osh, BASE64_BUFFER_SIZE);
	int32 cnt = 0;

	if (length > RAW_BUFFER_SIZE) {
		DHD_FILTER_ERR(("%s: input data size is too big, size is limited to %d\n",
			__FUNCTION__, RAW_BUFFER_SIZE));
		length = RAW_BUFFER_SIZE;
	}

	cnt = dhd_base64_encode(input, length, output, BASE64_BUFFER_SIZE);
	if (cnt == 0) {
		DHD_FILTER_ERR(("%s: base64 encoding error\n", __FUNCTION__));
	}
	return output;
}
#endif /* DHD_EWPR_VER2 */

#ifdef WLADPS_ENERGY_GAIN
#define ADPS_GAIN_ENERGY_CONV_UNIT	100000	/* energy unit(10^-2) * dur unit(10^-3) */
static int
dhd_calculate_adps_energy_gain(wl_adps_energy_gain_v1_t *data)
{
	int i;
	int energy_gain = 0;

	/* energy unit: (uAh * 10^-2)/sec */
	int pm0_idle_energy[MAX_BANDS] =
		{ADPS_GAIN_2G_PM0_IDLE, ADPS_GAIN_5G_PM0_IDLE};
	int txpspoll_energy[MAX_BANDS] =
		{ADPS_GAIN_2G_TX_PSPOLL, ADPS_GAIN_5G_TX_PSPOLL};

	if (data->version == 0 || data->length != sizeof(*data)) {
		DHD_FILTER_ERR(("%s - invalid adps_energy_gain data\n", __FUNCTION__));
		return BCME_ERROR;
	}

	/* dur unit: mSec */
	for (i = 0; i < MAX_BANDS; i++) {
		energy_gain += (data->gain_data[i].pm_dur_gain * pm0_idle_energy[i]);
		energy_gain -= (data->gain_data[i].step0_dur * txpspoll_energy[i]);
	}
	energy_gain /= ADPS_GAIN_ENERGY_CONV_UNIT;

	if (energy_gain < 0) {
		energy_gain = 0;
	}

	return energy_gain;
}

int dhd_event_log_filter_adps_energy_gain(dhd_pub_t *dhdp)
{
	int ret;

	void *last_elem;
	EWP_filter_t *filter;
	EWPF_ifc_elem_t *ifc_elem;

	if (!dhdp || !dhdp->event_log_filter) {
		DHD_FILTER_ERR(("%s - dhdp or event_log_filter is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	filter = (EWP_filter_t *)dhdp->event_log_filter;

	if (filter->enabled != TRUE) {
		DHD_FILTER_ERR(("%s - EWP Filter is not enabled\n", __FUNCTION__));
		return BCME_UNSUPPORTED;
	}

	/* Refer to STA interface */
	last_elem = dhd_ring_get_last(filter->i_ring[0]);
	if (last_elem == NULL) {
		DHD_FILTER_ERR(("%s - last_elem is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	ifc_elem = (EWPF_ifc_elem_t *)last_elem;
	ret = dhd_calculate_adps_energy_gain(&ifc_elem->adps_energy_gain);

	return ret;
}
#endif /* WLADPS_ENERGY_GAIN */
