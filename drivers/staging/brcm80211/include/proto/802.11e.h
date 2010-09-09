/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _802_11e_H_
#define _802_11e_H_

#ifndef _TYPEDEFS_H_
#include <typedefs.h>
#endif

#include <packed_section_start.h>

#define WME_TSPEC_HDR_LEN           2
#define WME_TSPEC_BODY_OFF          2

#define WME_CATEGORY_CODE_OFFSET	0
#define WME_ACTION_CODE_OFFSET		1
#define WME_TOKEN_CODE_OFFSET		2
#define WME_STATUS_CODE_OFFSET		3

BWL_PRE_PACKED_STRUCT struct tsinfo {
	uint8 octets[3];
} BWL_POST_PACKED_STRUCT;

typedef struct tsinfo tsinfo_t;

typedef BWL_PRE_PACKED_STRUCT struct tspec {
	uint8 oui[DOT11_OUI_LEN];
	uint8 type;
	uint8 subtype;
	uint8 version;
	tsinfo_t tsinfo;
	uint16 nom_msdu_size;
	uint16 max_msdu_size;
	uint32 min_srv_interval;
	uint32 max_srv_interval;
	uint32 inactivity_interval;
	uint32 suspension_interval;
	uint32 srv_start_time;
	uint32 min_data_rate;
	uint32 mean_data_rate;
	uint32 peak_data_rate;
	uint32 max_burst_size;
	uint32 delay_bound;
	uint32 min_phy_rate;
	uint16 surplus_bw;
	uint16 medium_time;
} BWL_POST_PACKED_STRUCT tspec_t;

#define WME_TSPEC_LEN	(sizeof(tspec_t))

#define TS_INFO_TID_SHIFT		1
#define TS_INFO_TID_MASK		(0xf << TS_INFO_TID_SHIFT)
#define TS_INFO_CONTENTION_SHIFT	7
#define TS_INFO_CONTENTION_MASK		(0x1 << TS_INFO_CONTENTION_SHIFT)
#define TS_INFO_DIRECTION_SHIFT		5
#define TS_INFO_DIRECTION_MASK		(0x3 << TS_INFO_DIRECTION_SHIFT)
#define TS_INFO_PSB_SHIFT		2
#define TS_INFO_PSB_MASK		(1 << TS_INFO_PSB_SHIFT)
#define TS_INFO_UPLINK			(0 << TS_INFO_DIRECTION_SHIFT)
#define TS_INFO_DOWNLINK		(1 << TS_INFO_DIRECTION_SHIFT)
#define TS_INFO_BIDIRECTIONAL		(3 << TS_INFO_DIRECTION_SHIFT)
#define TS_INFO_USER_PRIO_SHIFT		3
#define TS_INFO_USER_PRIO_MASK		(0x7 << TS_INFO_USER_PRIO_SHIFT)

#define WLC_CAC_GET_TID(pt)	((((pt).octets[0]) & \
	TS_INFO_TID_MASK) >> TS_INFO_TID_SHIFT)
#define WLC_CAC_GET_DIR(pt)	((((pt).octets[0]) & \
	TS_INFO_DIRECTION_MASK) >> TS_INFO_DIRECTION_SHIFT)
#define WLC_CAC_GET_PSB(pt)	((((pt).octets[1]) & \
	TS_INFO_PSB_MASK) >> TS_INFO_PSB_SHIFT)
#define WLC_CAC_GET_USER_PRIO(pt)	((((pt).octets[1]) & \
	TS_INFO_USER_PRIO_MASK) >> TS_INFO_USER_PRIO_SHIFT)

#define WLC_CAC_SET_TID(pt, id)	((((pt).octets[0]) & (~TS_INFO_TID_MASK)) | \
	((id) << TS_INFO_TID_SHIFT))
#define WLC_CAC_SET_USER_PRIO(pt, prio)	((((pt).octets[0]) & \
	(~TS_INFO_USER_PRIO_MASK)) | ((prio) << TS_INFO_USER_PRIO_SHIFT))

#define QBSS_LOAD_IE_LEN		5
#define QBSS_LOAD_AAC_OFF		3

#define CAC_ADDTS_RESP_TIMEOUT		300

#define DOT11E_STATUS_ADMISSION_ACCEPTED	0
#define DOT11E_STATUS_ADDTS_INVALID_PARAM	1
#define DOT11E_STATUS_ADDTS_REFUSED_NSBW	3
#define DOT11E_STATUS_ADDTS_REFUSED_AWHILE	47

#define DOT11E_STATUS_QSTA_LEAVE_QBSS		36
#define DOT11E_STATUS_END_TS			37
#define DOT11E_STATUS_UNKNOWN_TS		38
#define DOT11E_STATUS_QSTA_REQ_TIMEOUT		39

#include <packed_section_end.h>

#endif				/* _802_11e_H_ */
