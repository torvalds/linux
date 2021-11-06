/*
 * Basic types and constants relating to 802.11ah standard.
 * This is a portion of 802.11ah definition. The rest are in 802.11.h.
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
 */

#ifndef _802_11ah_h_
#define _802_11ah_h_

#include <typedefs.h>

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>

/**
 * TWT IE (sec 9.4.2.200)
 */

/* TWT element - top (Figure 9-589av) */
BWL_PRE_PACKED_STRUCT struct twt_ie_top {
	uint8 id;
	uint8 len;
	uint8 ctrl;		/* Control */
} BWL_POST_PACKED_STRUCT;

typedef struct twt_ie_top twt_ie_top_t;

/* S1G Action IDs */
#define S1G_ACTION_TWT_SETUP	6u
#define S1G_ACTION_TWT_TEARDOWN	7u
#define S1G_ACTION_TWT_INFO	11u

/* S1G Action frame offsets */
#define S1G_AF_CAT_OFF	0u
#define S1G_AF_ACT_OFF	1u

/* TWT Setup */
#define S1G_AF_TWT_SETUP_TOKEN_OFF	2u
#define S1G_AF_TWT_SETUP_TWT_IE_OFF	3u

/* TWT Teardown */
#define S1G_AF_TWT_TEARDOWN_FLOW_OFF	2u

/* TWT Information */
#define S1G_AF_TWT_INFO_OFF	2u

#define TWT_BCAST_WAKE_TIME_OFFSET	10u
#define TWT_BCAST_WAKE_TIME_SHIFT	10u
#define TWT_BCAST_WAKE_TIME_MASK	0x03FFFC00u
#define TWT_BCAST_WAKE_TIME_ZERO_BIT_SZ	10u

/* Control field (Figure 9-589aw) */
#define TWT_CTRL_NDP_PAGING_IND		0x01u	/* NDP Paging Indication */
#define TWT_CTRL_RESP_PM_MODE		0x02u	/* Respondor PM Mode */
#define TWT_CTRL_NEGO_TYPE_IDX		2u
#define TWT_CTRL_NEGO_TYPE_MASK		0x0Cu	/* TWT Negotiation Type */
#define TWT_CTRL_NEGO_TYPE_SHIFT	2u
#define TWT_CTRL_INFO_FRM_DISABLED	0x10u	/* TWT info frame disabled */
#define TWT_CTRL_WAKEDUR_UNIT		0x20u	/* Wake duration unit */

/* TWT Negotiation Type (Table 9-262j1) */
typedef enum twt_ctrl_nego_type {
	TWT_CTRL_NEGO_TYPE_0	= 0,	/* Individual TWT Setup */
	TWT_CTRL_NEGO_TYPE_1	= 1,	/* Wake TBTT Negotiation */
	TWT_CTRL_NEGO_TYPE_2	= 2,	/* Broadcast TWT IE in Beacon */
	TWT_CTRL_NEGO_TYPE_3	= 3,	/* Broadcast TWT memberships */
} twt_ctrl_nego_type_t;

/* Request Type field (Figure 9-589ay) */
#define TWT_REQ_TYPE_REQUEST		0x0001u	/* Request */
#define TWT_REQ_TYPE_SETUP_CMD_MASK	0x000eu	/* Setup Command */
#define TWT_REQ_TYPE_SETUP_CMD_SHIFT	1u
#define TWT_REQ_TYPE_TRIGGER		0x0010u	/* Trigger */
#define TWT_REQ_TYPE_IMPLICIT		0x0020u	/* Implicit */
#define TWT_REQ_TYPE_LAST_BCAST_PARAM	0x0020u	/* Last Broadcast Parameter Set */
#define TWT_REQ_TYPE_FLOW_TYPE		0x0040u	/* Flow Type */
#define TWT_REQ_TYPE_FLOW_ID_MASK	0x0380u	/* Flow Identifier */
#define TWT_REQ_TYPE_FLOW_ID_SHIFT	7u
#define TWT_REQ_TYPE_BTWT_RECOMM_MASK	0x0380u	/* Broadcast TWT Recommendation */
#define TWT_REQ_TYPE_BTWT_RECOMM_SHIFT	7u
#define TWT_REQ_TYPE_WAKE_EXP_MASK	0x7c00u	/* Wake Interval Exponent */
#define TWT_REQ_TYPE_WAKE_EXP_SHIFT	10u
#define TWT_REQ_TYPE_PROTECTION		0x8000u	/* Protection */

/* Setup Command field (Table 9-262k) */
#define TWT_SETUP_CMD_REQUEST_TWT	0u	/* Request TWT */
#define TWT_SETUP_CMD_SUGGEST_TWT	1u	/* Suggest TWT */
#define TWT_SETUP_CMD_DEMAND_TWT	2u	/* Demand TWT */
#define TWT_SETUP_CMD_GROUPING_TWT	3u	/* Grouping TWT */
#define TWT_SETUP_CMD_ACCEPT_TWT	4u	/* Accept TWT */
#define TWT_SETUP_CMD_ALTERNATE_TWT		5u	/* Alternate TWT */
#define TWT_SETUP_CMD_DICTATE_TWT	6u	/* Dictate TWT */
#define TWT_SETUP_CMD_REJECT_TWT	7u	/* Reject TWT */

/* Broadcast TWT Recommendation field (Table 9-262k1) */
#define TWT_BCAST_FRAME_RECOMM_0	0u	/* No constrains on frames in Broadcast TWT SP */
#define TWT_BCAST_FRAME_RECOMM_1	1u	/* Do not contain RUs for random access */
#define TWT_BCAST_FRAME_RECOMM_2	2u	/* Can contain RUs for random access */
#define TWT_BCAST_FRAME_RECOMM_3	3u

/* Request Type subfield - 2 octets */
typedef uint16 twt_request_type_t;	/* 16 bit request type */

/* Target Wake Time - 8 octets or 0 octet */
typedef uint64 twt_target_wake_time_t;	/* 64 bit TSF time of TWT Responding STA */
typedef uint16 twt_bcast_wake_time_t;	/* 16 bit Wake Time of Bcast scheduling STA */
typedef uint16 twt_bcast_twt_info_t;	/* 16 bit Broadcast TWT Info subfield */

/* TWT Group Assignment Info - 9 octets (long format) or 3 octets (short format) or 0 octet */
/* Group Assignment Info field - short format - Zero Offset Preset field is 0 */
BWL_PRE_PACKED_STRUCT struct twt_grp_short {
	uint8 grpid_n_0off;	/* Group ID and Zero Offset Present */
	uint16 unit_n_off;	/* TWT Unit and TWT Offset */
} BWL_POST_PACKED_STRUCT;

typedef struct twt_grp_short twt_grp_short_t;

/* Group Assignment Info field - long format - Zero Offset Preset field is 1 */
#define TWT_ZERO_OFF_GRP_LEN 6u
BWL_PRE_PACKED_STRUCT struct twt_grp_long {
	uint8 grpid_n_0off;	/* Group ID and Zero Offset Present */
	uint8 grp_0off[TWT_ZERO_OFF_GRP_LEN];	/* Zero Offset of Group */
	uint16 unit_n_off;	/* Unit and Offset */
} BWL_POST_PACKED_STRUCT;

typedef struct twt_grp_long twt_grp_long_t;

/* TWT Unit and TWT Offset field */
#define TWT_UNIT_MASK		0x000fu		/* TWT Unit */
#define TWT_OFFSET_MASK		0xfff0u		/* TWT Offset */
#define TWT_OFFSET_SHIFT	4u

/* TWT Unit field (table 8-248m) */
#define TWT_UNIT_32us		0u
#define TWT_UNIT_256us		1u
#define TWT_UNIT_1024us		2u
#define TWT_UNIT_8ms192us	3u
#define TWT_UNIT_32ms768us	4u
#define TWT_UNIT_262ms144us	5u
#define TWT_UNIT_1s048576us	6u
#define TWT_UNIT_8s388608us	7u
#define TWT_UNIT_33s554432us	8u
#define TWT_UNIT_268s435456us	9u
#define TWT_UNIT_1073s741824us	10u
#define TWT_UNIT_8589s934592us	11u

/* TWT element - bottom */
BWL_PRE_PACKED_STRUCT struct twt_ie_itwt_bottom {
	uint8 nom_wake_dur;		/* Nominal Minimum Wake Duration */
	uint16 wake_int_mant;		/* TWT Wake Interval Mantissa */
	uint8 channel;			/* TWT Channel */
	/* NDP Paging field */
} BWL_POST_PACKED_STRUCT;

typedef struct twt_ie_itwt_bottom twt_ie_itwt_bottom_t;

/* TWT element - bottom */
BWL_PRE_PACKED_STRUCT struct twt_ie_btwt_bottom {
	uint8 nom_wake_dur;		/* Nominal Minimum Wake Duration */
	uint16 wake_int_mant;		/* TWT Wake Interval Mantissa */
	twt_bcast_twt_info_t btwt_info;	/* Broadcast TWT Info */
	/* NDP Paging field */
} BWL_POST_PACKED_STRUCT;

typedef struct twt_ie_btwt_bottom twt_ie_btwt_bottom_t;

/* TWT IE structure for broadcast TWT */
typedef struct twt_last_bcast_ie {
	twt_ie_top_t top;			/* Element id, len, control fields */
	twt_request_type_t req_type;	/* request type field */
	twt_bcast_wake_time_t twt;	/* twt field */
	twt_ie_btwt_bottom_t btwt_bottom;	/* wake dur, int, BID Info */
} twt_last_bcast_ie_t;

/* Nominal Minimum Wake Duration */
#define TWT_WAKE_DUR_UNIT_256us		256u	/* Nom.Min. Wake Duration is in 256us units */
#define TWT_WAKE_DUR_UNIT_1ms		1024u	/* Nom. Min. Wake Duration is in 1ms units */

/* to be deprecated */
#define TWT_NOM_WAKE_DUR_UNIT	 256u	/* Nominal Minimum Wake Duration is in 256us units */

/* TWT IE field lengths */
#define TWT_IE_NOM_MIN_TWT_WK_DUR_SZ		1u	/* 1 byte */
#define TWT_IE_TWT_WAKE_INT_MANT_SZ		2u	/* 2 bytes */
#define TWT_IE_BCAST_TWT_INFO_SZ		2u	/* 2 byte */
#define TWT_IE_TWT_CHANNEL_SZ			1u	/* 1 byte */

/* Broadcast TWT info subfield format (figure 9-589ay1) */
#define TWT_BTWT_PERSIST_EXPO_MASK		0x0007u	/* Broadcast TWT Persistence Exponent */
#define TWT_BCAST_TWT_ID_MASK			0x00F8u	/* Broadcast TWT ID */
#define TWT_BCAST_TWT_ID_SHIFT			3u
#define TWT_BTWT_PERSIST_MANT_MASK		0xFF00u	/* Broadcast TWT Persistence Mantissa */
#define TWT_BTWT_PERSIST_MANT_SHIFT		8u

#define TWT_BTWT_PERSIST_INDEFINITE		0xFFu

/* NDP Paging field - 4 octets or 0 octet */
typedef uint32 twt_ndp_paging_t;

#define TWT_NDP_PAGING_PID		0x000001ffu	/* P-ID */
#define TWT_NDP_PAGING_MAX_PERIOD	0x0001fe00u	/* Max NDP Paging Period */
#define TWT_NDP_PAGING_PART_TSF_OFF	0x001e0000u	/* Partial TSF Offset */
#define TWT_NDP_PAGING_ACTION		0x00e00000u	/* Action */
#define TWT_NDP_PAGING_MIN_SLEEP	0x3f000000u	/* Min Sleep Duration */

/* Action field (table 8-248n) */
#define TWT_ACTION_SEND_PSP_TRIG	0u	/* Send a PS-Poll or uplink trigger frame */
#define TWT_ACTION_WAKE_MIN_SLEEP	1u	/* Wake up at the time indicated by
						 * Min Sleep Duration
						 */
#define TWT_ACTION_WAKE_RCV_BCN		2u	/* Wake up to receive the Beacon */
#define TWT_ACTION_WAKE_RCV_DTIM	3u	/* Wake up to receive the DTIM Beacon */
#define TWT_ACTION_WAKE_IND_TIME	4u	/* Wakeup at the time indicated by the sum of
						 * the Min Sleep Duration field and the ASD subfield
						 * in the APDI field of the NDP Paging frame
						 */

/* TWT Teardown for Negotiation type 0 or 1 */
#define TWT_TEARDOWN_FLOW_ID_MASK		0x07u
/* TWT Teardown for Negotiation type 3 */
#define TWT_TEARDOWN_BTWT_ID_MASK		0x1Fu

#define TWT_TEARDOWN_NEGO_TYPE_MASK		0x60u
#define TWT_TEARDOWN_NEGO_TYPE_SHIFT		5u
/* Teardown All TWT indication */
#define TWT_TEARDOWN_ALL_TWT			0x80u

/* TWT Information field byte 0 */
#define TWT_INFO_FLOW_ID_MASK		0x07u
#define TWT_INFO_RESP_REQ		0x08u
#define TWT_INFO_NEXT_TWT_REQ		0x10u
#define TWT_INFO_NEXT_TWT_SIZE_MASK	0x60u
#define TWT_INFO_NEXT_TWT_SIZE_SHIFT	0x5u
#define TWT_INFO_ALL_TWT		0x80u

/* Next TWT Subfield Size field encoding */
#define TWT_INFO_NEXT_TWT_SIZE_0_IDX	0u	/* 0 byte */
#define TWT_INFO_NEXT_TWT_SIZE_32_IDX	1u	/* 4 bytes */
#define TWT_INFO_NEXT_TWT_SIZE_48_IDX	2u	/* 6 bytes */
#define TWT_INFO_NEXT_TWT_SIZE_64_IDX	3u	/* 8 bytes */

/* Next TWT Subfield Size field */
#define TWT_INFO_NEXT_TWT_SIZE_0	0u	/* 0 byte */
#define TWT_INFO_NEXT_TWT_SIZE_32	4u	/* 4 bytes */
#define TWT_INFO_NEXT_TWT_SIZE_48	6u	/* 6 bytes */
#define TWT_INFO_NEXT_TWT_SIZE_64	8u	/* 8 bytes */

/* Old macro definitions - To be removed - Start here */
#define TWT_BCAST_MAX_VALID_FLOW_ID	3u
#define TWT_CTRL_BCAST			0x04u	/* Broadcast */
#define TWT_CTRL_WAKE_TBTT_NEGO		0x08u	/* Wake TBTT Negotiation */
#define TWT_SETUP_CMD_GRPING_TWT	3u	/* Grouping TWT */
#define TWT_SETUP_CMD_ALTER_TWT		5u	/* Alternate TWT */
#define TWT_IE_BCAST_TWT_ID_SZ		1u	/* 1 byte */
#define TWT_INFO_BROADCAST_RESCHED	0x80u

typedef struct twt_ie_itwt_bottom twt_ie_bottom_t;
/* Old macro definitions - To be removed - End here */

/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

#endif /* _802_11ah_h_ */
