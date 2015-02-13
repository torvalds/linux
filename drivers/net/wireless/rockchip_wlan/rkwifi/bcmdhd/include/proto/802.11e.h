/*
 * 802.11e protocol header file
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: 802.11e.h 382883 2013-02-04 23:26:09Z $
 */

#ifndef _802_11e_H_
#define _802_11e_H_

#ifndef _TYPEDEFS_H_
#include <typedefs.h>
#endif

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>


/* WME Traffic Specification (TSPEC) element */
#define WME_TSPEC_HDR_LEN           2           /* WME TSPEC header length */
#define WME_TSPEC_BODY_OFF          2           /* WME TSPEC body offset */

#define WME_CATEGORY_CODE_OFFSET	0		/* WME Category code offset */
#define WME_ACTION_CODE_OFFSET		1		/* WME Action code offset */
#define WME_TOKEN_CODE_OFFSET		2		/* WME Token code offset */
#define WME_STATUS_CODE_OFFSET		3		/* WME Status code offset */

BWL_PRE_PACKED_STRUCT struct tsinfo {
	uint8 octets[3];
} BWL_POST_PACKED_STRUCT;

typedef struct tsinfo tsinfo_t;

/* 802.11e TSPEC IE */
typedef BWL_PRE_PACKED_STRUCT struct tspec {
	uint8 oui[DOT11_OUI_LEN];	/* WME_OUI */
	uint8 type;					/* WME_TYPE */
	uint8 subtype;				/* WME_SUBTYPE_TSPEC */
	uint8 version;				/* WME_VERSION */
	tsinfo_t tsinfo;			/* TS Info bit field */
	uint16 nom_msdu_size;		/* (Nominal or fixed) MSDU Size (bytes) */
	uint16 max_msdu_size;		/* Maximum MSDU Size (bytes) */
	uint32 min_srv_interval;	/* Minimum Service Interval (us) */
	uint32 max_srv_interval;	/* Maximum Service Interval (us) */
	uint32 inactivity_interval;	/* Inactivity Interval (us) */
	uint32 suspension_interval; /* Suspension Interval (us) */
	uint32 srv_start_time;		/* Service Start Time (us) */
	uint32 min_data_rate;		/* Minimum Data Rate (bps) */
	uint32 mean_data_rate;		/* Mean Data Rate (bps) */
	uint32 peak_data_rate;		/* Peak Data Rate (bps) */
	uint32 max_burst_size;		/* Maximum Burst Size (bytes) */
	uint32 delay_bound;			/* Delay Bound (us) */
	uint32 min_phy_rate;		/* Minimum PHY Rate (bps) */
	uint16 surplus_bw;			/* Surplus Bandwidth Allowance (range 1.0-8.0) */
	uint16 medium_time;			/* Medium Time (32 us/s periods) */
} BWL_POST_PACKED_STRUCT tspec_t;

#define WME_TSPEC_LEN	(sizeof(tspec_t))		/* not including 2-bytes of header */

/* ts_info */
/* 802.1D priority is duplicated - bits 13-11 AND bits 3-1 */
#define TS_INFO_TID_SHIFT		1	/* TS info. TID shift */
#define TS_INFO_TID_MASK		(0xf << TS_INFO_TID_SHIFT)	/* TS info. TID mask */
#define TS_INFO_CONTENTION_SHIFT	7	/* TS info. contention shift */
#define TS_INFO_CONTENTION_MASK	(0x1 << TS_INFO_CONTENTION_SHIFT) /* TS info. contention mask */
#define TS_INFO_DIRECTION_SHIFT	5	/* TS info. direction shift */
#define TS_INFO_DIRECTION_MASK	(0x3 << TS_INFO_DIRECTION_SHIFT) /* TS info. direction mask */
#define TS_INFO_PSB_SHIFT		2		/* TS info. PSB bit Shift */
#define TS_INFO_PSB_MASK		(1 << TS_INFO_PSB_SHIFT)	/* TS info. PSB mask */
#define TS_INFO_UPLINK			(0 << TS_INFO_DIRECTION_SHIFT)	/* TS info. uplink */
#define TS_INFO_DOWNLINK		(1 << TS_INFO_DIRECTION_SHIFT)	/* TS info. downlink */
#define TS_INFO_BIDIRECTIONAL	(3 << TS_INFO_DIRECTION_SHIFT)	/* TS info. bidirectional */
#define TS_INFO_USER_PRIO_SHIFT	3	/* TS info. user priority shift */
/* TS info. user priority mask */
#define TS_INFO_USER_PRIO_MASK	(0x7 << TS_INFO_USER_PRIO_SHIFT)

/* Macro to get/set bit(s) field in TSINFO */
#define WLC_CAC_GET_TID(pt)	((((pt).octets[0]) & TS_INFO_TID_MASK) >> TS_INFO_TID_SHIFT)
#define WLC_CAC_GET_DIR(pt)	((((pt).octets[0]) & \
	TS_INFO_DIRECTION_MASK) >> TS_INFO_DIRECTION_SHIFT)
#define WLC_CAC_GET_PSB(pt)	((((pt).octets[1]) & TS_INFO_PSB_MASK) >> TS_INFO_PSB_SHIFT)
#define WLC_CAC_GET_USER_PRIO(pt)	((((pt).octets[1]) & \
	TS_INFO_USER_PRIO_MASK) >> TS_INFO_USER_PRIO_SHIFT)

#define WLC_CAC_SET_TID(pt, id)	((((pt).octets[0]) & (~TS_INFO_TID_MASK)) | \
	((id) << TS_INFO_TID_SHIFT))
#define WLC_CAC_SET_USER_PRIO(pt, prio)	((((pt).octets[0]) & (~TS_INFO_USER_PRIO_MASK)) | \
	((prio) << TS_INFO_USER_PRIO_SHIFT))

/* 802.11e QBSS Load IE */
#define QBSS_LOAD_IE_LEN		5	/* QBSS Load IE length */
#define QBSS_LOAD_AAC_OFF		3	/* AAC offset in IE */

#define CAC_ADDTS_RESP_TIMEOUT		1000	/* default ADDTS response timeout in ms */
						/* DEFVAL dot11ADDTSResponseTimeout = 1s */

/* 802.11e ADDTS status code */
#define DOT11E_STATUS_ADMISSION_ACCEPTED	0	/* TSPEC Admission accepted status */
#define DOT11E_STATUS_ADDTS_INVALID_PARAM	1	/* TSPEC invalid parameter status */
#define DOT11E_STATUS_ADDTS_REFUSED_NSBW	3	/* ADDTS refused (non-sufficient BW) */
#define DOT11E_STATUS_ADDTS_REFUSED_AWHILE	47	/* ADDTS refused but could retry later */
#ifdef BCMCCX
#define CCX_STATUS_ASSOC_DENIED_UNKNOWN    0xc8	/* unspecified QoS related failure */
#define CCX_STATUS_ASSOC_DENIED_AP_POLICY  0xc9	/* TSPEC refused due to AP policy */
#define CCX_STATUS_ASSOC_DENIED_NO_BW	   0xca	/* Assoc denied due to AP insufficient BW */
#define CCX_STATUS_ASSOC_DENIED_BAD_PARAM  0xcb	/* one or more TSPEC with invalid parameter */
#endif	/* BCMCCX */

/* 802.11e DELTS status code */
#define DOT11E_STATUS_QSTA_LEAVE_QBSS		36	/* STA leave QBSS */
#define DOT11E_STATUS_END_TS				37	/* END TS */
#define DOT11E_STATUS_UNKNOWN_TS			38	/* UNKNOWN TS */
#define DOT11E_STATUS_QSTA_REQ_TIMEOUT		39	/* STA ADDTS request timeout */


/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

#endif /* _802_11e_CAC_H_ */
