/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Fundamental types and constants relating to WFA NAN
 * (Neighbor Awareness Networking)
 *
 * Copyright (C) 1999-2019, Broadcom.
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
 * $Id: nan.h 818571 2019-05-08 04:36:41Z $
 */
#ifndef _NAN_H_
#define _NAN_H_

#include <typedefs.h>
#include <802.11.h>

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>

/* WiFi NAN OUI values */
#define NAN_OUI			"\x50\x6F\x9A"     /* WFA OUI. WiFi-Alliance OUI */
/* For oui_type field identifying the type and version of the NAN IE. */
#define NAN_OUI_TYPE		0x13        /* Type/Version */
#define NAN_AF_OUI_TYPE		0x18        /* Type/Version */
/* IEEE 802.11 vendor specific information element. (Same as P2P_IE_ID.) */
#define NAN_IE_ID		0xdd

/* Same as P2P_PUB_AF_CATEGORY and DOT11_ACTION_CAT_PUBLIC */
#define NAN_PUB_AF_CATEGORY	DOT11_ACTION_CAT_PUBLIC
/* Protected dual public action frame category */
#define NAN_PROT_DUAL_PUB_AF_CATEGORY	DOT11_ACTION_CAT_PDPA
/* IEEE 802.11 Public Action Frame Vendor Specific. (Same as P2P_PUB_AF_ACTION.) */
#define NAN_PUB_AF_ACTION	DOT11_PUB_ACTION_VENDOR_SPEC
/* Number of octents in hash of service name. (Same as P2P_WFDS_HASH_LEN.) */
#define NAN_SVC_HASH_LEN	6
/* Size of fixed length part of nan_pub_act_frame_t before attributes. */
#define NAN_PUB_ACT_FRAME_FIXED_LEN 6
/* Number of octents in master rank value. */
#define NAN_MASTER_RANK_LEN     8
/* NAN public action frame header size */
#define NAN_PUB_ACT_FRAME_HDR_SIZE (OFFSETOF(nan_pub_act_frame_t, data))
/* NAN network ID */
#define NAN_NETWORK_ID		"\x51\x6F\x9A\x01\x00\x00"
/* Service Control Type length */
#define NAN_SVC_CONTROL_TYPE_LEN	2
/* Binding Bitmap length */
#define NAN_BINDING_BITMAP_LEN		2
/* Service Response Filter (SRF) control field masks */
#define NAN_SRF_BLOOM_MASK		0x01
#define NAN_SRF_INCLUDE_MASK		0x02
#define NAN_SRF_INDEX_MASK		0x0C
/* SRF Bloom Filter index shift */
#define NAN_SRF_BLOOM_SHIFT	2
#define NAN_SRF_INCLUDE_SHIFT	1
/* Mask for CRC32 output, used in hash function for NAN bloom filter */
#define NAN_BLOOM_CRC32_MASK	0xFFFF

/* Attribute TLV header size */
#define NAN_ATTR_ID_OFF		0
#define NAN_ATTR_LEN_OFF	1
#define NAN_ATTR_DATA_OFF	3

#define NAN_ATTR_ID_LEN		 1u	/* ID field length */
#define NAN_ATTR_LEN_LEN	 2u	/* Length field length */
#define NAN_ATTR_HDR_LEN	 (NAN_ATTR_ID_LEN + NAN_ATTR_LEN_LEN)
#define NAN_ENTRY_CTRL_LEN       1      /* Entry control field length from FAM attribute */
#define NAN_MAP_ID_LEN           1	/* MAP ID length to signify band */
#define NAN_OPERATING_CLASS_LEN  1	/* operating class field length from NAN FAM */
#define NAN_CHANNEL_NUM_LEN      1	/* channel number field length 1 byte */

/* generic nan attribute total length */
#define NAN_ATTR_TOT_LEN(_nan_attr)	(ltoh16_ua(((const uint8 *)(_nan_attr)) + \
	NAN_ATTR_ID_LEN) + NAN_ATTR_HDR_LEN)

/* NAN slot duration / period */
#define NAN_MIN_TU		16
#define NAN_TU_PER_DW		512
#define NAN_MAX_DW		16
#define NAN_MAX_TU		(NAN_MAX_DW * NAN_TU_PER_DW)

#define NAN_SLOT_DUR_0TU	0
#define NAN_SLOT_DUR_16TU	16
#define NAN_SLOT_DUR_32TU	32
#define NAN_SLOT_DUR_64TU	64
#define NAN_SLOT_DUR_128TU	128
#define NAN_SLOT_DUR_256TU	256
#define NAN_SLOT_DUR_512TU	512
#define NAN_SLOT_DUR_1024TU	1024
#define NAN_SLOT_DUR_2048TU	2048
#define NAN_SLOT_DUR_4096TU	4096
#define NAN_SLOT_DUR_8192TU	8192

#define NAN_SOC_CHAN_2G		6	/* NAN 2.4G discovery channel */
#define NAN_SOC_CHAN_5G_CH149	149	/* NAN 5G discovery channel if upper band allowed */
#define NAN_SOC_CHAN_5G_CH44	44	/* NAN 5G discovery channel if only lower band allowed */

/* size of ndc id */
#define NAN_DATA_NDC_ID_SIZE 6

#define NAN_AVAIL_ENTRY_LEN_RES0 7      /* Avail entry len in FAM attribute for resolution 16TU */
#define NAN_AVAIL_ENTRY_LEN_RES1 5      /* Avail entry len in FAM attribute for resolution 32TU */
#define NAN_AVAIL_ENTRY_LEN_RES2 4      /* Avail entry len in FAM attribute for resolution 64TU */

/* map id field */
#define NAN_MAPID_SPECIFIC_MAP_MASK	0x01 /* apply to specific map */
#define NAN_MAPID_MAPID_MASK		0x1E
#define NAN_MAPID_MAPID_SHIFT		1
#define NAN_MAPID_SPECIFIC_MAP(_mapid)	((_mapid) & NAN_MAPID_SPECIFIC_MAP_MASK)
#define NAN_MAPID_ALL_MAPS(_mapid)	(!NAN_MAPID_SPECIFIC_MAP(_mapid))
#define NAN_MAPID_MAPID(_mapid)		(((_mapid) & NAN_MAPID_MAPID_MASK) \
		>> NAN_MAPID_MAPID_SHIFT)
#define NAN_MAPID_SET_SPECIFIC_MAPID(map_id)	((((map_id) << NAN_MAPID_MAPID_SHIFT) \
		& NAN_MAPID_MAPID_MASK) | NAN_MAPID_SPECIFIC_MAP_MASK)

/* Vendor-specific public action frame for NAN */
typedef BWL_PRE_PACKED_STRUCT struct nan_pub_act_frame_s {
	/* NAN_PUB_AF_CATEGORY 0x04 */
	uint8 category_id;
	/* NAN_PUB_AF_ACTION 0x09 */
	uint8 action_field;
	/* NAN_OUI 0x50-6F-9A */
	uint8 oui[DOT11_OUI_LEN];
	/* NAN_OUI_TYPE 0x13 */
	uint8 oui_type;
	/* One or more NAN Attributes follow */
	uint8 data[];
} BWL_POST_PACKED_STRUCT nan_pub_act_frame_t;

/* NAN attributes as defined in the nan spec */
enum {
	NAN_ATTR_MASTER_IND	= 0,
	NAN_ATTR_CLUSTER	= 1,
	NAN_ATTR_SVC_ID_LIST    = 2,
	NAN_ATTR_SVC_DESCRIPTOR = 3,
	NAN_ATTR_CONN_CAP       = 4,
	NAN_ATTR_INFRA		= 5,
	NAN_ATTR_P2P		= 6,
	NAN_ATTR_IBSS		= 7,
	NAN_ATTR_MESH		= 8,
	NAN_ATTR_FURTHER_NAN_SD = 9,
	NAN_ATTR_FURTHER_AVAIL	= 10,
	NAN_ATTR_COUNTRY_CODE	= 11,
	NAN_ATTR_RANGING	= 12,
	NAN_ATTR_CLUSTER_DISC	= 13,
	/* nan 2.0 */
	NAN_ATTR_SVC_DESC_EXTENSION = 14,
	NAN_ATTR_NAN_DEV_CAP = 15,
	NAN_ATTR_NAN_NDP = 16,
	NAN_ATTR_NAN_NMSG = 17,
	NAN_ATTR_NAN_AVAIL = 18,
	NAN_ATTR_NAN_NDC = 19,
	NAN_ATTR_NAN_NDL = 20,
	NAN_ATTR_NAN_NDL_QOS = 21,
	NAN_ATTR_MCAST_SCHED = 22,
	NAN_ATTR_UNALIGN_SCHED = 23,
	NAN_ATTR_PAGING_UCAST = 24,
	NAN_ATTR_PAGING_MCAST = 25,
	NAN_ATTR_RANGING_INFO = 26,
	NAN_ATTR_RANGING_SETUP = 27,
	NAN_ATTR_FTM_RANGE_REPORT = 28,
	NAN_ATTR_ELEMENT_CONTAINER = 29,
	NAN_ATTR_WLAN_INFRA_EXT = 30,
	NAN_ATTR_EXT_P2P_OPER = 31,
	NAN_ATTR_EXT_IBSS = 32,
	NAN_ATTR_EXT_MESH = 33,
	NAN_ATTR_CIPHER_SUITE_INFO = 34,
	NAN_ATTR_SEC_CTX_ID_INFO = 35,
	NAN_ATTR_SHARED_KEY_DESC = 36,
	NAN_ATTR_MCAST_SCHED_CHANGE = 37,
	NAN_ATTR_MCAST_SCHED_OWNER_CHANGE = 38,
	NAN_ATTR_PUBLIC_AVAILABILITY = 39,
	NAN_ATTR_SUB_SVC_ID_LIST = 40,
	NAN_ATTR_NDPE = 41,
	/* change NAN_ATTR_MAX_ID to max ids + 1, excluding NAN_ATTR_VENDOR_SPECIFIC.
	 * This is used in nan_parse.c
	 */
	NAN_ATTR_MAX_ID		= NAN_ATTR_NDPE + 1,

	NAN_ATTR_VENDOR_SPECIFIC = 221
};

enum wifi_nan_avail_resolution {
	NAN_AVAIL_RES_16_TU = 0,
	NAN_AVAIL_RES_32_TU = 1,
	NAN_AVAIL_RES_64_TU = 2,
	NAN_AVAIL_RES_INVALID = 255
};

typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_ie_s {
	uint8	id;		/* IE ID: NAN_IE_ID 0xDD */
	uint8	len;		/* IE length */
	uint8	oui[DOT11_OUI_LEN]; /* NAN_OUI 50:6F:9A */
	uint8	oui_type;	/* NAN_OUI_TYPE 0x13 */
	uint8	attr[];	/* var len attributes */
} BWL_POST_PACKED_STRUCT wifi_nan_ie_t;

#define NAN_IE_HDR_SIZE	(OFFSETOF(wifi_nan_ie_t, attr))

/* master indication record  */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_master_ind_attr_s {
	uint8	id;
	uint16	len;
	uint8	master_preference;
	uint8	random_factor;
} BWL_POST_PACKED_STRUCT wifi_nan_master_ind_attr_t;

/* cluster attr record  */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_cluster_attr_s {
	uint8	id;
	uint16	len;
	uint8   amr[NAN_MASTER_RANK_LEN];
	uint8   hop_count;
	/* Anchor Master Beacon Transmission Time */
	uint32  ambtt;
} BWL_POST_PACKED_STRUCT wifi_nan_cluster_attr_t;

/*  container for service ID records  */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_svc_id_attr_s {
	uint8	id;
	uint16	len;
	uint8	svcid[0]; /* 6*len of srvc IDs */
} BWL_POST_PACKED_STRUCT wifi_nan_svc_id_attr_t;

/* service_control bitmap for wifi_nan_svc_descriptor_attr_t below */
#define NAN_SC_PUBLISH 0x0
#define NAN_SC_SUBSCRIBE 0x1
#define NAN_SC_FOLLOWUP 0x2
/* Set to 1 if a Matching Filter field is included in descriptors. */
#define NAN_SC_MATCHING_FILTER_PRESENT 0x4
/* Set to 1 if a Service Response Filter field is included in descriptors. */
#define NAN_SC_SR_FILTER_PRESENT 0x8
/* Set to 1 if a Service Info field is included in descriptors. */
#define NAN_SC_SVC_INFO_PRESENT 0x10
/* range is close proximity only */
#define NAN_SC_RANGE_LIMITED 0x20
/* Set to 1 if binding bitamp is present in descriptors */
#define NAN_SC_BINDING_BITMAP_PRESENT 0x40

/* Service descriptor */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_svc_descriptor_attr_s {
	/* Attribute ID - 0x03. */
	uint8 id;
	/* Length of the following fields in the attribute */
	uint16 len;
	/* Hash of the Service Name */
	uint8 svc_hash[NAN_SVC_HASH_LEN];
	/* Publish or subscribe instance id */
	uint8 instance_id;
	/* Requestor Instance ID */
	uint8 requestor_id;
	/* Service Control Bitmask. Also determines what data follows. */
	uint8 svc_control;
	/* Optional fields follow */
} BWL_POST_PACKED_STRUCT wifi_nan_svc_descriptor_attr_t;

/* IBSS attribute */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_ibss_attr_s {
	/* Attribute ID - 0x07. */
	uint8 id;
	/* Length of the following fields in the attribute */
	uint16 len;
	/* BSSID of the ibss */
	struct ether_addr bssid;
	/*
	 map control:, bits:
	[0-3]: Id for associated further avail map attribute
	[4-5]: avail interval duration: 0:16ms; 1:32ms; 2:64ms; 3:reserved
	[6] : repeat : 0 - applies to next DW, 1: 16 intervals max? wtf?
	[7] : reserved
	*/
	uint8 map_ctrl;
	/* avail. intervals bitmap, var len  */
	uint8 avail_bmp[1];
} BWL_POST_PACKED_STRUCT wifi_nan_ibss_attr_t;

/* Country code attribute  */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_country_code_attr_s {
	/* Attribute ID - 0x0B. */
	uint8 id;
	/* Length of the following fields in the attribute */
	uint16 len;
	/* Condensed Country String first two octets */
	uint8 country_str[2];
} BWL_POST_PACKED_STRUCT wifi_nan_country_code_attr_t;

/* Further Availability MAP attr  */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_favail_attr_s {
	/* Attribute ID - 0x0A. */
	uint8 id;
	/* Length of the following fields in the attribute */
	uint16 len;
	/* MAP id: val [0..15], values[16-255] reserved */
	uint8 map_id;
	/*  availibility entry, var len */
	uint8 avil_entry[1];
} BWL_POST_PACKED_STRUCT wifi_nan_favail_attr_t;

/* Further Availability MAP attr  */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_avail_entry_s {
	/*
	 entry control
	 [0-1]: avail interval duration: 0:16ms; 1:32ms; 2:64ms;
	 [2:7] reserved
	*/
	uint8 entry_ctrl;
	/* operating class: freq band etc IEEE 802.11 */
	uint8 opclass;
	/* channel number */
	uint8 chan;
	/*  avail bmp, var len */
	uint8 avail_bmp[1];
} BWL_POST_PACKED_STRUCT wifi_nan_avail_entry_t;

/* Map control Field */
#define NAN_MAPCTRL_IDMASK	0x7
#define NAN_MAPCTRL_DURSHIFT	4
#define NAN_MAPCTRL_DURMASK	0x30
#define NAN_MAPCTRL_REPEAT	0x40
#define NAN_MAPCTRL_REPEATSHIFT	6

#define NAN_VENDOR_TYPE_RTT	0
#define NAN_VENDOR_TYPE_P2P	1

/* Vendor Specific Attribute - old definition */
/* TODO remove */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_vendor_attr_s {
	uint8	id;			/* 0xDD */
	uint16	len;		/* IE length */
	uint8	oui[DOT11_OUI_LEN]; /* 00-90-4C */
	uint8	type;		/* attribute type */
	uint8	attr[1];	/* var len attributes */
} BWL_POST_PACKED_STRUCT wifi_nan_vendor_attr_t;

#define NAN_VENDOR_HDR_SIZE	(OFFSETOF(wifi_nan_vendor_attr_t, attr))

/* vendor specific attribute */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_vndr_attr_s {
	uint8	id;			/* 0xDD */
	uint16	len;			/* length of following fields */
	uint8	oui[DOT11_OUI_LEN];	/* vendor specific OUI */
	uint8	body[];
} BWL_POST_PACKED_STRUCT wifi_nan_vndr_attr_t;

/* p2p operation attribute */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_p2p_op_attr_s {
	/* Attribute ID - 0x06. */
	uint8 id;
	/* Length of the following fields in the attribute */
	uint16 len;
	/* P2P device role */
	uint8 dev_role;
	/* BSSID of the ibss */
	struct ether_addr p2p_dev_addr;
	/*
	map control:, bits:
	[0-3]: Id for associated further avail map attribute
	[4-5]: avail interval duration: 0:16ms; 1:32ms; 2:64ms; 3:reserved
	[6] : repeat : 0 - applies to next DW, 1: 16 intervals max? wtf?
	[7] : reserved
	*/
	uint8 map_ctrl;
	/* avail. intervals bitmap */
	uint8 avail_bmp[1];
} BWL_POST_PACKED_STRUCT wifi_nan_p2p_op_attr_t;

/* ranging attribute */
#define NAN_RANGING_MAP_CTRL_ID_SHIFT 0
#define NAN_RANGING_MAP_CTRL_ID_MASK 0x0F
#define NAN_RANGING_MAP_CTRL_DUR_SHIFT 4
#define NAN_RANGING_MAP_CTRL_DUR_MASK 0x30
#define NAN_RANGING_MAP_CTRL_REPEAT_SHIFT 6
#define NAN_RANGING_MAP_CTRL_REPEAT_MASK 0x40
#define NAN_RANGING_MAP_CTRL_REPEAT_DW(_ctrl) (((_ctrl) & \
	NAN_RANGING_MAP_CTRL_DUR_MASK) ? 16 : 1)
#define NAN_RANGING_MAP_CTRL(_id, _dur, _repeat) (\
	(((_id) << NAN_RANGING_MAP_CTRL_ID_SHIFT) & \
		 NAN_RANGING_MAP_CTRL_ID_MASK) | \
	(((_dur) << NAN_RANGING_MAP_CTRL_DUR_SHIFT) & \
		NAN_RANGING_MAP_CTRL_DUR_MASK) | \
	(((_repeat) << NAN_RANGING_MAP_CTRL_REPEAT_SHIFT) & \
		 NAN_RANGING_MAP_CTRL_REPEAT_MASK))

enum {
	NAN_RANGING_PROTO_FTM = 0
};

typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_ranging_attr_s {
	uint8 id;			/* 0x0C */
	uint16 len;			/* length that follows */
	struct ether_addr dev_addr;	/* device mac address */

	/*
	map control:, bits:
	[0-3]: Id for associated further avail map attribute
	[4-5]: avail interval duration: 0:16ms; 1:32ms; 2:64ms; 3:reserved
	[6] : repeat : 0 - applies to next DW, 1: 16 intervals max? wtf?
	[7] : reserved
	*/
	uint8 map_ctrl;

	uint8 protocol;					/* FTM = 0 */
	uint32 avail_bmp;				/* avail interval bitmap */
} BWL_POST_PACKED_STRUCT wifi_nan_ranging_attr_t;

typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_ranging_info_attr_s {
	uint8 id;			/* 0x1A */
	uint16 len;			/* length that follows */
	/*
	location info availability bit map
	0: LCI Local Coordinates
	1: Geospatial LCI WGS84
	2: Civi Location
	3: Last Movement Indication
	[4-7]: reserved
	*/
	uint8 lc_info_avail;
	/*
	Last movement indication
	present if bit 3 is set in lc_info_avail
	cluster TSF[29:14] at the last detected platform movement
	*/
	uint16 last_movement;

} BWL_POST_PACKED_STRUCT wifi_nan_ranging_info_attr_t;

typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_ranging_setup_attr_hdr_s {
	uint8 id;			/* 0x1B */
	uint16 len;			/* length that follows */
	uint8 dialog_token;	/* Identify req and resp */
	uint8 type_status;	/* bits 0-3 type, 4-7 status */
	/* reason code
	i. when frm type = response & status = reject
	ii. frm type = termination
	*/
	uint8 reason;
} BWL_POST_PACKED_STRUCT wifi_nan_ranging_setup_attr_hdr_t;

typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_ranging_setup_attr_s {

	wifi_nan_ranging_setup_attr_hdr_t setup_attr_hdr;
	/* Below fields not required when frm type = termination */
	uint8 ranging_ctrl; /* Bit 0: ranging report required or not */
	uint8 ftm_params[3];
	uint8 data[];	/* schedule entry list */
} BWL_POST_PACKED_STRUCT wifi_nan_ranging_setup_attr_t;

#define NAN_RANGE_SETUP_ATTR_OFFSET_TBM_INFO (OFFSETOF(wifi_nan_ranging_setup_attr_t, data))

typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_ranging_report_attr_s {
	uint8 id;			/* 0x1C */
	uint16 len;			/* length that follows */
	/* FTM report format in spec.
	See definition in 9.4.2.22.18 in 802.11mc D5.0
	*/
	uint8 entry_count;
	uint8 data[2]; /* includes pad */
	/*
	dot11_ftm_range_entry_t entries[entry_count];
	uint8 error_count;
	dot11_ftm_error_entry_t errors[error_count];
	 */
} BWL_POST_PACKED_STRUCT wifi_nan_ranging_report_attr_t;

/* Ranging control flags */
#define NAN_RNG_REPORT_REQUIRED		0x01
#define NAN_RNG_FTM_PARAMS_PRESENT	0x02
#define NAN_RNG_SCHED_ENTRY_PRESENT	0X04

/* Location info flags */
#define NAN_RNG_LOCATION_FLAGS_LOCAL_CORD		0x1
#define NAN_RNG_LOCATION_FLAGS_GEO_SPATIAL		0x2
#define NAN_RNG_LOCATION_FLAGS_CIVIC			0x4
#define NAN_RNG_LOCATION_FLAGS_LAST_MVMT		0x8

/* Last movement mask and shift value */
#define NAN_RNG_LOCATION_MASK_LAST_MVT_TSF	0x3FFFC000
#define NAN_RNG_LOCATION_SHIFT_LAST_MVT_TSF	14

/* FTM params shift values  */
#define NAN_FTM_MAX_BURST_DUR_SHIFT	0
#define NAN_FTM_MIN_FTM_DELTA_SHIFT	4
#define NAN_FTM_NUM_FTM_SHIFT		10
#define NAN_FTM_FORMAT_BW_SHIFT		15

/* FTM params mask  */
#define NAN_FTM_MAX_BURST_DUR_MASK	0x00000F
#define NAN_FTM_MIN_FTM_DELTA_MASK	0x00003F
#define NAN_FTM_NUM_FTM_MASK		0x00001F
#define NAN_FTM_FORMAT_BW_MASK		0x00003F

#define FTM_PARAMS_BURSTTMO_FACTOR 250

/* set to value to uint32 */
#define NAN_FTM_SET_BURST_DUR(ftm, dur) (ftm |= (((dur + 2) & NAN_FTM_MAX_BURST_DUR_MASK) <<\
	NAN_FTM_MAX_BURST_DUR_SHIFT))
#define NAN_FTM_SET_FTM_DELTA(ftm, delta) (ftm |= (((delta/100) & NAN_FTM_MIN_FTM_DELTA_MASK) <<\
	NAN_FTM_MIN_FTM_DELTA_SHIFT))
#define NAN_FTM_SET_NUM_FTM(ftm, delta) (ftm |= ((delta & NAN_FTM_NUM_FTM_MASK) <<\
	NAN_FTM_NUM_FTM_SHIFT))
#define NAN_FTM_SET_FORMAT_BW(ftm, delta) (ftm |= ((delta & NAN_FTM_FORMAT_BW_MASK) <<\
	NAN_FTM_FORMAT_BW_SHIFT))
/* set uint32 to attribute */
#define NAN_FTM_PARAMS_UINT32_TO_ATTR(ftm_u32, ftm_attr) {ftm_attr[0] = ftm_u32 & 0xFF; \
			ftm_attr[1] = (ftm_u32 >> 8) & 0xFF; ftm_attr[2] = (ftm_u32 >> 16) & 0xFF;}

/* get atrribute to uint32 */
#define NAN_FTM_PARAMS_ATTR_TO_UINT32(ftm_p, ftm_u32) (ftm_u32 = ftm_p[0] | ftm_p[1] << 8 | \
	ftm_p[2] << 16)
/* get param values from uint32 */
#define NAN_FTM_GET_BURST_DUR(ftm) (((ftm >> NAN_FTM_MAX_BURST_DUR_SHIFT) &\
	NAN_FTM_MAX_BURST_DUR_MASK))
#define NAN_FTM_GET_BURST_DUR_USEC(_val) ((1 << ((_val)-2)) * FTM_PARAMS_BURSTTMO_FACTOR)
#define NAN_FTM_GET_FTM_DELTA(ftm) (((ftm >> NAN_FTM_MIN_FTM_DELTA_SHIFT) &\
	NAN_FTM_MIN_FTM_DELTA_MASK)*100)
#define NAN_FTM_GET_NUM_FTM(ftm) ((ftm >> NAN_FTM_NUM_FTM_SHIFT) &\
	NAN_FTM_NUM_FTM_MASK)
#define NAN_FTM_GET_FORMAT_BW(ftm) ((ftm >> NAN_FTM_FORMAT_BW_SHIFT) &\
	NAN_FTM_FORMAT_BW_MASK)

#define NAN_CONN_CAPABILITY_WFD		0x0001
#define NAN_CONN_CAPABILITY_WFDS	0x0002
#define NAN_CONN_CAPABILITY_TDLS	0x0004
#define NAN_CONN_CAPABILITY_INFRA	0x0008
#define NAN_CONN_CAPABILITY_IBSS	0x0010
#define NAN_CONN_CAPABILITY_MESH	0x0020

#define NAN_DEFAULT_MAP_ID		0	/* nan default map id */
#define NAN_DEFAULT_MAP_CTRL		0	/* nan default map control */

typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_conn_cap_attr_s {
	/* Attribute ID - 0x04. */
	uint8 id;
	/* Length of the following fields in the attribute */
	uint16	len;
	uint16	conn_cap_bmp;	/* Connection capability bitmap */
} BWL_POST_PACKED_STRUCT wifi_nan_conn_cap_attr_t;

/* NAN Element container Attribute */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_container_attr_s {
	uint8 id;	/* id - 0x20 */
	uint16 len;	/* Total length of following IEs */
	uint8 map_id;	/* map id */
	uint8 data[1];	/* Data pointing to one or more IEs */
} BWL_POST_PACKED_STRUCT wifi_nan_container_attr_t;

/* NAN 2.0 NAN avail attribute */

/* Availability Attribute */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_avail_attr_s {
	uint8 id;	/* id - 0x12 */
	uint16 len;	/* total length */
	uint8 seqid;	/* sequence id */
	uint16 ctrl;	/* attribute control */
	uint8 entry[1];	/* availability entry list */
} BWL_POST_PACKED_STRUCT wifi_nan_avail_attr_t;

/* for processing/building time bitmap info in nan_avail_entry */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_time_bitmap_s {
	uint16 ctrl;		/* Time bitmap control */
	uint8 len;		/* Time bitmap length */
	uint8 bitmap[];	/* Time bitmap */
} BWL_POST_PACKED_STRUCT wifi_nan_time_bitmap_t;

/* Availability Entry format */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_avail_entry_attr_s {
	uint16 len;		/* Length */
	uint16 entry_cntrl;	/* Entry Control */
	uint8 var[];		/* Time bitmap and channel entry list */
} BWL_POST_PACKED_STRUCT wifi_nan_avail_entry_attr_t;

/* FAC Channel Entry  (section 10.7.19.1.5) */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_chan_entry_s {
	uint8 oper_class;		/* Operating Class */
	uint16 chan_bitmap;		/* Channel Bitmap */
	uint8 primary_chan_bmp;		/* Primary Channel Bitmap */
	uint8 aux_chan[0];			/* Auxiliary Channel bitmap */
} BWL_POST_PACKED_STRUCT wifi_nan_chan_entry_t;

/* Channel entry */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_channel_entry_s {
	uint8 opclass;		/* Operating class */
	uint16 chan_bitmap;	/* Channel bitmap */
	uint8 prim_bitmap;	/* Primary channel bitmap */
	uint16 aux_bitmap;	/* Time bitmap length */
} BWL_POST_PACKED_STRUCT wifi_nan_channel_entry_t;

/* Type of  Availability: committed */
#define NAN_ENTRY_CNTRL_TYPE_COMM_AVAIL_MASK	0x1
/* Type of  Availability: potential */
#define NAN_ENTRY_CNTRL_TYPE_POTEN_AVAIL_MASK	0x2
/* Type of  Availability: conditional */
#define NAN_ENTRY_CNTRL_TYPE_COND_AVAIL_MASK	0x4

#define NAN_AVAIL_CTRL_MAP_ID_MASK		0x000F
#define NAN_AVAIL_CTRL_MAP_ID(_ctrl)		((_ctrl) & NAN_AVAIL_CTRL_MAP_ID_MASK)
#define NAN_AVAIL_CTRL_COMM_CHANGED_MASK	0x0010
#define NAN_AVAIL_CTRL_COMM_CHANGED(_ctrl)	((_ctrl) & NAN_AVAIL_CTRL_COMM_CHANGED_MASK)
#define NAN_AVAIL_CTRL_POTEN_CHANGED_MASK	0x0020
#define NAN_AVAIL_CTRL_POTEN_CHANGED(_ctrl)	((_ctrl) & NAN_AVAIL_CTRL_POTEN_CHANGED_MASK)
#define NAN_AVAIL_CTRL_PUBLIC_CHANGED_MASK	0x0040
#define NAN_AVAIL_CTRL_PUBLIC_CHANGED(_ctrl)	((_ctrl) & NAN_AVAIL_CTRL_PUBLIC_CHANGED_MASK)
#define NAN_AVAIL_CTRL_NDC_CHANGED_MASK		0x0080
#define NAN_AVAIL_CTRL_NDC_CHANGED(_ctrl)	((_ctrl) & NAN_AVAIL_CTRL_NDC_CHANGED_MASK)
#define NAN_AVAIL_CTRL_MCAST_CHANGED_MASK	0x0100
#define NAN_AVAIL_CTRL_MCAST_CHANGED(_ctrl)	((_ctrl) & NAN_AVAIL_CTRL_MCAST_CHANGED_MASK)
#define NAN_AVAIL_CTRL_MCAST_CHG_CHANGED_MASK	0x0200
#define NAN_AVAIL_CTRL_MCAST_CHG_CHANGED(_ctrl)	((_ctrl) & NAN_AVAIL_CTRL_MCAST_CHG_CHANGED_MASK)
#define NAN_AVAIL_CTRL_CHANGED_FLAGS_MASK	0x03f0

#define NAN_AVAIL_ENTRY_CTRL_AVAIL_TYPE_MASK 0x07
#define NAN_AVAIL_ENTRY_CTRL_AVAIL_TYPE(_flags) ((_flags) & NAN_AVAIL_ENTRY_CTRL_AVAIL_TYPE_MASK)
#define NAN_AVAIL_ENTRY_CTRL_USAGE_MASK 0x18
#define NAN_AVAIL_ENTRY_CTRL_USAGE_SHIFT 3
#define NAN_AVAIL_ENTRY_CTRL_USAGE(_flags) (((_flags) & NAN_AVAIL_ENTRY_CTRL_USAGE_MASK) \
	>> NAN_AVAIL_ENTRY_CTRL_USAGE_SHIFT)
#define NAN_AVAIL_ENTRY_CTRL_UTIL_MASK 0xE0
#define NAN_AVAIL_ENTRY_CTRL_UTIL_SHIFT 5
#define NAN_AVAIL_ENTRY_CTRL_UTIL(_flags) (((_flags) & NAN_AVAIL_ENTRY_CTRL_UTIL_MASK) \
	>> NAN_AVAIL_ENTRY_CTRL_UTIL_SHIFT)
#define NAN_AVAIL_ENTRY_CTRL_RX_NSS_MASK 0xF00
#define NAN_AVAIL_ENTRY_CTRL_RX_NSS_SHIFT 8
#define NAN_AVAIL_ENTRY_CTRL_RX_NSS(_flags) (((_flags) & NAN_AVAIL_ENTRY_CTRL_RX_NSS_MASK) \
	>> NAN_AVAIL_ENTRY_CTRL_RX_NSS_SHIFT)
#define NAN_AVAIL_ENTRY_CTRL_BITMAP_PRESENT_MASK 0x1000
#define NAN_AVAIL_ENTRY_CTRL_BITMAP_PRESENT_SHIFT 12
#define NAN_AVAIL_ENTRY_CTRL_BITMAP_PRESENT(_flags) (((_flags) & \
	NAN_AVAIL_ENTRY_CTRL_BITMAP_PRESENT_MASK) >> NAN_AVAIL_ENTRY_CTRL_BITMAP_PRESENT_SHIFT)

#define NAN_TIME_BMAP_CTRL_BITDUR_MASK 0x07
#define NAN_TIME_BMAP_CTRL_BITDUR(_flags) ((_flags) & NAN_TIME_BMAP_CTRL_BITDUR_MASK)
#define NAN_TIME_BMAP_CTRL_PERIOD_MASK 0x38
#define NAN_TIME_BMAP_CTRL_PERIOD_SHIFT 3
#define NAN_TIME_BMAP_CTRL_PERIOD(_flags) (((_flags) & NAN_TIME_BMAP_CTRL_PERIOD_MASK) \
	>> NAN_TIME_BMAP_CTRL_PERIOD_SHIFT)
#define NAN_TIME_BMAP_CTRL_OFFSET_MASK 0x7FC0
#define NAN_TIME_BMAP_CTRL_OFFSET_SHIFT 6
#define NAN_TIME_BMAP_CTRL_OFFSET(_flags) (((_flags) & NAN_TIME_BMAP_CTRL_OFFSET_MASK) \
	>> NAN_TIME_BMAP_CTRL_OFFSET_SHIFT)
#define NAN_TIME_BMAP_LEN(avail_entry)	\
	(*(uint8 *)(((wifi_nan_avail_entry_attr_t *)avail_entry)->var + 2))

#define NAN_AVAIL_CHAN_LIST_HDR_LEN 1
#define NAN_AVAIL_CHAN_LIST_TYPE_BAND		0x00
#define NAN_AVAIL_CHAN_LIST_TYPE_CHANNEL	0x01
#define NAN_AVAIL_CHAN_LIST_NON_CONTIG_BW	0x02
#define NAN_AVAIL_CHAN_LIST_NUM_ENTRIES_MASK	0xF0
#define NAN_AVAIL_CHAN_LIST_NUM_ENTRIES_SHIFT	4
#define NAN_AVAIL_CHAN_LIST_NUM_ENTRIES(_ctrl) (((_ctrl) & NAN_AVAIL_CHAN_LIST_NUM_ENTRIES_MASK) \
	>> NAN_AVAIL_CHAN_LIST_NUM_ENTRIES_SHIFT)

typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_channel_entry_list_s {
	uint8 chan_info;
	uint8 var[0];
} BWL_POST_PACKED_STRUCT wifi_nan_channel_entry_list_t;

/* define for chan_info */
#define NAN_CHAN_OP_CLASS_MASK 0x01
#define NAN_CHAN_NON_CONT_BW_MASK 0x02
#define NAN_CHAN_RSVD_MASK 0x03
#define NAN_CHAN_NUM_ENTRIES_MASK 0xF0

typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_band_entry_s {
	uint8 band[1];
} BWL_POST_PACKED_STRUCT wifi_nan_band_entry_t;

/* Type of  Availability: committed */
#define NAN_ENTRY_CNTRL_TYPE_COMM_AVAIL	        0x1
/* Type of  Availability: potential */
#define NAN_ENTRY_CNTRL_TYPE_POTEN_AVAIL	0x2
/* Type of  Availability: conditional */
#define NAN_ENTRY_CNTRL_TYPE_COND_AVAIL	        0x4
/* Committed + Potential */
#define NAN_ENTRY_CNTRL_TYPE_COMM_POTEN \
	(NAN_ENTRY_CNTRL_TYPE_COMM_AVAIL | NAN_ENTRY_CNTRL_TYPE_POTEN_AVAIL)
/* Conditional + Potential */
#define NAN_ENTRY_CNTRL_TYPE_COND_POTEN \
	(NAN_ENTRY_CNTRL_TYPE_COND_AVAIL | NAN_ENTRY_CNTRL_TYPE_POTEN_AVAIL)

/* Type of  Availability */
#define NAN_ENTRY_CNTRL_TYPE_OF_AVAIL_MASK	0x07
#define NAN_ENTRY_CNTRL_TYPE_OF_AVAIL_SHIFT	0
/* Usage Preference */
#define NAN_ENTRY_CNTRL_USAGE_PREF_MASK		0x18
#define NAN_ENTRY_CNTRL_USAGE_PREF_SHIFT	3
/* Utilization */
#define NAN_ENTRY_CNTRL_UTIL_MASK		0x1E0
#define NAN_ENTRY_CNTRL_UTIL_SHIFT		5

/* Time Bitmap Control field (section 5.7.18.2.3) */

/* Reserved */
#define NAN_TIME_BMP_CNTRL_RSVD_MASK	0x01
#define NAN_TIME_BMP_CNTRL_RSVD_SHIFT	0
/* Bitmap Len */
#define NAN_TIME_BMP_CNTRL_BMP_LEN_MASK	0x7E
#define NAN_TIME_BMP_CNTRL_BMP_LEN_SHIFT 1
/* Bit Duration */
#define NAN_TIME_BMP_CNTRL_BIT_DUR_MASK	0x380
#define NAN_TIME_BMP_CNTRL_BIT_DUR_SHIFT	7
/* Bitmap Len */
#define NAN_TIME_BMP_CNTRL_PERIOD_MASK	0x1C00
#define NAN_TIME_BMP_CNTRL_PERIOD_SHIFT	10
/* Start Offset */
#define NAN_TIME_BMP_CNTRL_START_OFFSET_MASK	0x3FE000
#define NAN_TIME_BMP_CNTRL_START_OFFSET_SHIFT	13
/* Reserved */
#define NAN_TIME_BMP_CNTRL_RESERVED_MASK	0xC00000
#define NAN_TIME_BMP_CNTRL_RESERVED_SHIFT	22

/* Time Bitmap Control field: Period */
typedef enum
{
	NAN_TIME_BMP_CTRL_PERIOD_128TU = 1,
	NAN_TIME_BMP_CTRL_PERIOD_256TU = 2,
	NAN_TIME_BMP_CTRL_PERIOD_512TU = 3,
	NAN_TIME_BMP_CTRL_PERIOD_1024TU = 4,
	NAN_TIME_BMP_CTRL_PERIOD_2048U = 5,
	NAN_TIME_BMP_CTRL_PERIOD_4096U = 6,
	NAN_TIME_BMP_CTRL_PERIOD_8192U = 7
} nan_time_bmp_ctrl_repeat_interval_t;

enum
{
	NAN_TIME_BMP_BIT_DUR_16TU_IDX = 0,
	NAN_TIME_BMP_BIT_DUR_32TU_IDX = 1,
	NAN_TIME_BMP_BIT_DUR_64TU_IDX = 2,
	NAN_TIME_BMP_BIT_DUR_128TU_IDX = 3
};

enum
{
	NAN_TIME_BMP_BIT_DUR_IDX_0 = 16,
	NAN_TIME_BMP_BIT_DUR_IDX_1 = 32,
	NAN_TIME_BMP_BIT_DUR_IDX_2 = 64,
	NAN_TIME_BMP_BIT_DUR_IDX_3 = 128
};

enum
{
	NAN_TIME_BMP_CTRL_PERIOD_IDX_1 = 128,
	NAN_TIME_BMP_CTRL_PERIOD_IDX_2 = 256,
	NAN_TIME_BMP_CTRL_PERIOD_IDX_3 = 512,
	NAN_TIME_BMP_CTRL_PERIOD_IDX_4 = 1024,
	NAN_TIME_BMP_CTRL_PERIOD_IDX_5 = 2048,
	NAN_TIME_BMP_CTRL_PERIOD_IDX_6 = 4096,
	NAN_TIME_BMP_CTRL_PERIOD_IDX_7 = 8192
};

/* Channel Entries List field */

/* Type */
#define NAN_CHAN_ENTRY_TYPE_MASK	0x01
#define NAN_CHAN_ENTRY_TYPE_SHIFT	0
/* Channel Entry Length Indication */
#define NAN_CHAN_ENTRY_LEN_IND_MASK	0x02
#define NAN_CHAN_ENTRY_LEN_IND_SHIFT	1
/* Reserved */
#define NAN_CHAN_ENTRY_RESERVED_MASK	0x0C
#define NAN_CHAN_ENTRY_RESERVED_SHIFT	2
/* Number of FAC Band or Channel Entries  */
#define NAN_CHAN_ENTRY_NO_OF_CHAN_ENTRY_MASK	0xF0
#define NAN_CHAN_ENTRY_NO_OF_CHAN_ENTRY_SHIFT	4

#define NAN_CHAN_ENTRY_TYPE_BANDS	0
#define NAN_CHAN_ENTRY_TYPE_OPCLASS_CHANS	1

#define NAN_CHAN_ENTRY_BW_LT_80MHZ	0
#define NAN_CHAN_ENTRY_BW_EQ_160MHZ	1

/*
 * NDL Attribute WFA Tech. Spec ver 1.0.r12 (section 10.7.19.2)
 */
#define NDL_ATTR_IM_MAP_ID_LEN		1
#define NDL_ATTR_IM_TIME_BMP_CTRL_LEN	2
#define NDL_ATTR_IM_TIME_BMP_LEN_LEN	1

/*
 * NDL Control field - Table xx
 */
#define NDL_ATTR_CTRL_PEER_ID_PRESENT_MASK		0x01
#define NDL_ATTR_CTRL_PEER_ID_PRESENT_SHIFT		0
#define NDL_ATTR_CTRL_IM_SCHED_PRESENT_MASK		0x02
#define NDL_ATTR_CTRL_IM_SCHED_PRESENT_SHIFT	1
#define NDL_ATTR_CTRL_NDC_ATTR_PRESENT_MASK		0x04
#define NDL_ATTR_CTRL_NDC_ATTR_PRESENT_SHIFT	2
#define NDL_ATTR_CTRL_QOS_ATTR_PRESENT_MASK		0x08
#define NDL_ATTR_CTRL_QOS_ATTR_PRESENT_SHIFT	3
#define NDL_ATTR_CTRL_MAX_IDLE_PER_PRESENT_MASK		0x10	/* max idle period */
#define NDL_ATTR_CTRL_MAX_IDLE_PER_PRESENT_SHIFT	4
#define NDL_ATTR_CTRL_NDL_TYPE_MASK				0x20	/* NDL type */
#define NDL_ATTR_CTRL_NDL_TYPE_SHIFT			5
#define NDL_ATTR_CTRL_NDL_SETUP_REASON_MASK		0xC0	/* NDL Setup Reason */
#define NDL_ATTR_CTRL_NDL_SETUP_REASON_SHIFT	6

/* NDL setup Reason */
#define NDL_ATTR_CTRL_NDL_TYPE_S_NDL	0x0	/* S-NDL */
#define NDL_ATTR_CTRL_NDL_TYPE_P_NDL	0x1	/* P-NDL */

/* NDL setup Reason */
#define NDL_ATTR_CTRL_NDL_SETUP_REASON_NDP_RANG	0x0	/* NDP or Ranging */
#define NDL_ATTR_CTRL_NDL_SETUP_REASON_FSD_GAS	0x1	/* FSD using GAS */

#define NAN_NDL_TYPE_MASK				0x0F
#define NDL_ATTR_TYPE_STATUS_REQUEST	0x00
#define NDL_ATTR_TYPE_STATUS_RESPONSE	0x01
#define NDL_ATTR_TYPE_STATUS_CONFIRM	0x02
#define NDL_ATTR_TYPE_STATUS_CONTINUED	0x00
#define NDL_ATTR_TYPE_STATUS_ACCEPTED	0x10
#define NDL_ATTR_TYPE_STATUS_REJECTED	0x20

#define NAN_NDL_TYPE_CHECK(_ndl, x)	(((_ndl)->type_status & NAN_NDL_TYPE_MASK) == (x))
#define NAN_NDL_REQUEST(_ndl)		(((_ndl)->type_status & NAN_NDL_TYPE_MASK) == \
								NDL_ATTR_TYPE_STATUS_REQUEST)
#define NAN_NDL_RESPONSE(_ndl)		(((_ndl)->type_status & NAN_NDL_TYPE_MASK) == \
								NDL_ATTR_TYPE_STATUS_RESPONSE)
#define NAN_NDL_CONFIRM(_ndl)		(((_ndl)->type_status & NAN_NDL_TYPE_MASK) == \
								NDL_ATTR_TYPE_STATUS_CONFIRM)

#define NAN_NDL_STATUS_SHIFT	4
#define NAN_NDL_STATUS_MASK	0xF0
#define NAN_NDL_CONT(_ndl)	(((_ndl)->type_status & NAN_NDL_STATUS_MASK) == \
								NDL_ATTR_TYPE_STATUS_CONTINUED)
#define NAN_NDL_ACCEPT(_ndl)	(((_ndl)->type_status & NAN_NDL_STATUS_MASK) == \
								NDL_ATTR_TYPE_STATUS_ACCEPTED)
#define NAN_NDL_REJECT(_ndl)	(((_ndl)->type_status & NAN_NDL_STATUS_MASK) == \
								NDL_ATTR_TYPE_STATUS_REJECTED)
#define NAN_NDL_FRM_STATUS(_ndl) \
	(((_ndl)->type_status & NAN_NDL_STATUS_MASK) >> NAN_NDL_STATUS_SHIFT)

#define NDL_ATTR_CTRL_NONE				0
#define NDL_ATTR_CTRL_PEER_ID_PRESENT	(1 << NDL_ATTR_CTRL_PEER_ID_PRESENT_SHIFT)
#define NDL_ATTR_CTRL_IMSCHED_PRESENT	(1 << NDL_ATTR_CTRL_IM_SCHED_PRESENT_SHIFT)
#define NDL_ATTR_CTRL_NDC_PRESENT		(1 << NDL_ATTR_CTRL_NDC_ATTR_PRESENT_SHIFT)
#define NDL_ATTR_CTRL_NDL_QOS_PRESENT	(1 << NDL_ATTR_CTRL_QOS_ATTR_PRESENT_SHIFT)
#define NDL_ATTR_CTRL_MAX_IDLE_PER_PRESENT	(1 << NDL_ATTR_CTRL_MAX_IDLE_PER_PRESENT_SHIFT)

#define NA_NDL_IS_IMMUT_PRESENT(ndl) (((ndl)->ndl_ctrl) & NDL_ATTR_CTRL_IMSCHED_PRESENT)
#define NA_NDL_IS_PEER_ID_PRESENT(ndl) (((ndl)->ndl_ctrl) & NDL_ATTR_CTRL_PEER_ID_PRESENT)
#define NA_NDL_IS_MAX_IDLE_PER_PRESENT(ndl) (((ndl)->ndl_ctrl) & NDL_ATTR_CTRL_MAX_IDLE_PER_PRESENT)

#define NDL_ATTR_PEERID_LEN				1
#define NDL_ATTR_MAX_IDLE_PERIOD_LEN	2

typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_ndl_attr_s {
	uint8 id;		/* NAN_ATTR_NAN_NDL = 0x17 */
	uint16 len;		/* Length of the fields in the attribute */
	uint8 dialog_token;	/* Identify req and resp */
	uint8 type_status;		/* Bits[3-0] type subfield, Bits[7-4] status subfield */
	uint8 reason;		/* Identifies reject reason */
	uint8 ndl_ctrl;		/* NDL control field */
	uint8 var[];		/* Optional fields follow */
} BWL_POST_PACKED_STRUCT wifi_nan_ndl_attr_t;

/*
 * NDL QoS Attribute  WFA Tech. Spec ver r26
 */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_ndl_qos_attr_s {
	uint8 id;		/* NAN_ATTR_NAN_NDL_QOS = 24 */
	uint16 len;		/* Length of the attribute field following */
	uint8 min_slots;	/* Min. number of FAW slots needed per DW interval */
	uint16 max_latency;  /* Max interval between non-cont FAW */
} BWL_POST_PACKED_STRUCT wifi_nan_ndl_qos_attr_t;

/* no preference to min time slots */
#define NAN_NDL_QOS_MIN_SLOT_NO_PREF	0
/* no preference to no. of slots between two non-contiguous slots */
#define NAN_NDL_QOS_MAX_LAT_NO_PREF		0xFFFF

/* Device Capability Attribute */

typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_dev_cap_s {
	uint8 id;		/* 0x0F */
	uint16 len;		/* Length */
	uint8 map_id;		/* map id */
	uint16 commit_dw_info;	/* Committed DW Info */
	uint8 bands_supported;	/* Supported Bands */
	uint8 op_mode;		/* Operation Mode */
	uint8 num_antennas;	/* Bit 0-3 tx, 4-7 rx */
	uint16 chan_switch_time;	/* Max channel switch time in us */
	uint8 capabilities;	/* DFS Master, Extended key id etc */
} BWL_POST_PACKED_STRUCT wifi_nan_dev_cap_t;

/* map id related */

/* all maps */
#define NAN_DEV_CAP_ALL_MAPS_FLAG_MASK	0x1	/* nan default map control */
#define NAN_DEV_CAP_ALL_MAPS_FLAG_SHIFT	0
/* map id */
#define NAN_DEV_CAP_MAPID_MASK	0x1E
#define NAN_DEV_CAP_MAPID_SHIFT	1

/* Awake DW Info field format */

/* 2.4GHz DW */
#define NAN_DEV_CAP_AWAKE_DW_2G_MASK	0x07
/* 5GHz DW */
#define NAN_DEV_CAP_AWAKE_DW_5G_MASK	0x38
/* Reserved */
#define NAN_DEV_CAP_AWAKE_DW_RSVD_MASK	0xC0

/* bit shift for dev cap */
#define NAN_DEV_CAP_AWAKE_DW_2G_SHIFT	0
#define NAN_DEV_CAP_AWAKE_DW_5G_SHIFT	3

/* Device Capability Attribute Format */

/* Committed DW Info field format */
/* 2.4GHz DW */
#define NAN_DEV_CAP_COMMIT_DW_2G_MASK	0x07
#define NAN_DEV_CAP_COMMIT_DW_2G_OVERWRITE_MASK	0x3C0
/* 5GHz DW */
#define NAN_DEV_CAP_COMMIT_DW_5G_MASK	0x38
#define NAN_DEV_CAP_COMMIT_DW_5G_OVERWRITE_MASK 0x3C00
/* Reserved */
#define NAN_DEV_CAP_COMMIT_DW_RSVD_MASK	0xC000
/* Committed DW bit shift for dev cap */
#define NAN_DEV_CAP_COMMIT_DW_2G_SHIFT	0
#define NAN_DEV_CAP_COMMIT_DW_5G_SHIFT	3
#define NAN_DEV_CAP_COMMIT_DW_2G_OVERWRITE_SHIFT	6
#define NAN_DEV_CAP_COMMIT_DW_5G_OVERWRITE_SHIFT	10
/* Operation Mode */
#define NAN_DEV_CAP_OP_PHY_MODE_HT_ONLY		0x00
#define NAN_DEV_CAP_OP_PHY_MODE_VHT		0x01
#define NAN_DEV_CAP_OP_PHY_MODE_VHT_8080	0x02
#define NAN_DEV_CAP_OP_PHY_MODE_VHT_160		0x04
#define NAN_DEV_CAP_OP_PAGING_NDL		0x08

#define NAN_DEV_CAP_OP_MODE_VHT_MASK		0x01
#define NAN_DEV_CAP_OP_MODE_VHT_SHIFT		0
#define NAN_DEV_CAP_OP_MODE_VHT8080_MASK	0x02
#define NAN_DEV_CAP_OP_MODE_VHT8080_SHIFT	1
#define NAN_DEV_CAP_OP_MODE_VHT160_MASK		0x04
#define NAN_DEV_CAP_OP_MODE_VHT160_SHIFT	2
#define NAN_DEV_CAP_OP_MODE_PAGING_NDL_MASK	0x08
#define NAN_DEV_CAP_OP_MODE_PAGING_NDL_SHIFT	3

#define NAN_DEV_CAP_RX_ANT_SHIFT		4
#define NAN_DEV_CAP_TX_ANT_MASK			0x0F
#define NAN_DEV_CAP_RX_ANT_MASK			0xF0
#define NAN_DEV_CAP_TX_ANT(_ant)		((_ant) & NAN_DEV_CAP_TX_ANT_MASK)
#define NAN_DEV_CAP_RX_ANT(_ant)		(((_ant) & NAN_DEV_CAP_RX_ANT_MASK) \
		>> NAN_DEV_CAP_RX_ANT_SHIFT)

/* Device capabilities */

/* DFS master capability */
#define NAN_DEV_CAP_DFS_MASTER_MASK		0x01
#define NAN_DEV_CAP_DFS_MASTER_SHIFT	0
/* extended iv cap */
#define NAN_DEV_CAP_EXT_KEYID_MASK		0x02
#define NAN_DEV_CAP_EXT_KEYID_SHIFT		1
/* NDPE attribute support */
#define	NAN_DEV_CAP_NDPE_ATTR_SUPPORT_MASK	0x08
#define NAN_DEV_CAP_NDPE_ATTR_SUPPORT(_cap)	((_cap) & NAN_DEV_CAP_NDPE_ATTR_SUPPORT_MASK)

/* Band IDs */
enum {
	NAN_BAND_ID_TVWS		= 0,
	NAN_BAND_ID_SIG			= 1,	/* Sub 1 GHz */
	NAN_BAND_ID_2G			= 2,	/* 2.4 GHz */
	NAN_BAND_ID_3G			= 3,	/* 3.6 GHz */
	NAN_BAND_ID_5G			= 4,	/* 4.9 & 5 GHz */
	NAN_BAND_ID_60G			= 5
};
typedef uint8 nan_band_id_t;

/* NAN supported band in device capability */
#define NAN_DEV_CAP_SUPPORTED_BANDS_2G	(1 << NAN_BAND_ID_2G)
#define NAN_DEV_CAP_SUPPORTED_BANDS_5G	(1 << NAN_BAND_ID_5G)

/*
 * Unaligned schedule attribute section 10.7.19.6 spec. ver r15
 */
#define NAN_ULW_ATTR_CTRL_SCHED_ID_MASK 0x000F
#define NAN_ULW_ATTR_CTRL_SCHED_ID_SHIFT 0
#define NAN_ULW_ATTR_CTRL_SEQ_ID_MASK 0xFF00
#define NAN_ULW_ATTR_CTRL_SEQ_ID_SHIFT 8

#define NAN_ULW_OVWR_ALL_MASK 0x01
#define NAN_ULW_OVWR_ALL_SHIFT 0
#define NAN_ULW_OVWR_MAP_ID_MASK 0x1E
#define NAN_ULW_OVWR_MAP_ID_SHIFT 1

#define NAN_ULW_CTRL_TYPE_MASK 0x03
#define NAN_ULW_CTRL_TYPE_SHIFT 0
#define NAN_ULW_CTRL_TYPE(ctrl)	(ctrl & NAN_ULW_CTRL_TYPE_MASK)
#define NAN_ULW_CTRL_CHAN_AVAIL_MASK 0x04
#define NAN_ULW_CTRL_CHAN_AVAIL_SHIFT 2
#define NAN_ULW_CTRL_CHAN_AVAIL(ctrl)	((ctrl & NAN_ULW_CTRL_CHAN_AVAIL_MASK) \
	>> NAN_ULW_CTRL_CHAN_AVAIL_SHIFT)
#define NAN_ULW_CTRL_RX_NSS_MASK 0x78
#define NAN_ULW_CTRL_RX_NSS_SHIFT 3

#define NAN_ULW_CTRL_TYPE_BAND		0
#define NAN_ULW_CTRL_TYPE_CHAN_NOAUX	1
#define NAN_ULW_CTRL_TYPE_CHAN_AUX	2

#define NAN_ULW_CNT_DOWN_NO_EXPIRE	0xFF	/* ULWs doen't end until next sched update */
#define NAN_ULW_CNT_DOWN_CANCEL		0x0		/* cancel remaining ulws */

typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_ulw_attr_s {
	uint8 id;
	uint16 len;
	uint16 ctrl;
	uint32 start; /* low 32 bits of tsf */
	uint32 dur;
	uint32 period;
	uint8 count_down;
	uint8 overwrite;
	/*
	 * ulw[0] == optional field ULW control when present.
	 * band ID or channel follows
	 */
	uint8 ulw_entry[];
} BWL_POST_PACKED_STRUCT wifi_nan_ulw_attr_t;

/* NAN2 Management Frame (section 5.6) */

/* Public action frame for NAN2 */
typedef BWL_PRE_PACKED_STRUCT struct nan2_pub_act_frame_s {
	/* NAN_PUB_AF_CATEGORY 0x04 */
	uint8 category_id;
	/* NAN_PUB_AF_ACTION 0x09 */
	uint8 action_field;
	/* NAN_OUI 0x50-6F-9A */
	uint8 oui[DOT11_OUI_LEN];
	/* NAN_OUI_TYPE TBD */
	uint8 oui_type;
	/* NAN_OUI_SUB_TYPE TBD */
	uint8 oui_sub_type;
	/* One or more NAN Attributes follow */
	uint8 data[];
} BWL_POST_PACKED_STRUCT nan2_pub_act_frame_t;

#define NAN2_PUB_ACT_FRM_SIZE	(OFFSETOF(nan2_pub_act_frame_t, data))

/* NAN Action Frame Subtypes */
/* Subtype-0 is Reserved */
#define NAN_MGMT_FRM_SUBTYPE_RESERVED		0
#define NAN_MGMT_FRM_SUBTYPE_INVALID		0
/* NAN Ranging Request */
#define NAN_MGMT_FRM_SUBTYPE_RANGING_REQ	1
/* NAN Ranging Response */
#define NAN_MGMT_FRM_SUBTYPE_RANGING_RESP	2
/* NAN Ranging Termination */
#define NAN_MGMT_FRM_SUBTYPE_RANGING_TERM	3
/* NAN Ranging Report */
#define NAN_MGMT_FRM_SUBTYPE_RANGING_RPT	4
/* NDP Request */
#define NAN_MGMT_FRM_SUBTYPE_NDP_REQ		5
/* NDP Response */
#define NAN_MGMT_FRM_SUBTYPE_NDP_RESP		6
/* NDP Confirm */
#define NAN_MGMT_FRM_SUBTYPE_NDP_CONFIRM	7
/* NDP Key Installment */
#define NAN_MGMT_FRM_SUBTYPE_NDP_KEY_INST	8
/* NDP Termination */
#define NAN_MGMT_FRM_SUBTYPE_NDP_END		9
/* Schedule Request */
#define NAN_MGMT_FRM_SUBTYPE_SCHED_REQ		10
/* Schedule Response */
#define NAN_MGMT_FRM_SUBTYPE_SCHED_RESP		11
/* Schedule Confirm */
#define NAN_MGMT_FRM_SUBTYPE_SCHED_CONF		12
/* Schedule Update */
#define NAN_MGMT_FRM_SUBTYPE_SCHED_UPD		13

#define NAN_SCHEDULE_AF(_naf_subtype) \
	((_naf_subtype >= NAN_MGMT_FRM_SUBTYPE_SCHED_REQ) && \
	(_naf_subtype <= NAN_MGMT_FRM_SUBTYPE_SCHED_UPD))

/* Reason code defines */
#define NAN_REASON_RESERVED			0x0
#define NAN_REASON_UNSPECIFIED			0x1
#define NAN_REASON_RESOURCE_LIMIT		0x2
#define NAN_REASON_INVALID_PARAMS		0x3
#define NAN_REASON_FTM_PARAM_INCAP		0x4
#define NAN_REASON_NO_MOVEMENT			0x5
#define NAN_REASON_INVALID_AVAIL		0x6
#define NAN_REASON_IMMUT_UNACCEPT		0x7
#define NAN_REASON_SEC_POLICY			0x8
#define NAN_REASON_QOS_UNACCEPT			0x9
#define NAN_REASON_NDP_REJECT			0xa
#define NAN_REASON_NDL_UNACCEPTABLE		0xb

/* nan 2.0 qos (not attribute) */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_ndp_qos_s {
	uint8 tid;		/* traffic identifier */
	uint16 pkt_size;	/* service data pkt size */
	uint8 data_rate;	/* mean data rate */
	uint8 svc_interval;	/* max service interval */
} BWL_POST_PACKED_STRUCT wifi_nan_ndp_qos_t;

/* NDP control bitmap defines */
#define NAN_NDP_CTRL_CONFIRM_REQUIRED		0x01
#define NAN_NDP_CTRL_SECURTIY_PRESENT		0x04
#define NAN_NDP_CTRL_PUB_ID_PRESENT		0x08
#define NAN_NDP_CTRL_RESP_NDI_PRESENT		0x10
#define NAN_NDP_CTRL_SPEC_INFO_PRESENT		0x20
#define NAN_NDP_CTRL_RESERVED			0xA0

/* Used for both NDP Attribute and NDPE Attribute, since the structures are identical */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_ndp_attr_s {
	uint8 id;		/* NDP: 0x10, NDPE: 0x29 */
	uint16 len;		/* length */
	uint8 dialog_token;	/* dialog token */
	uint8 type_status;	/* bits 0-3 type, 4-7 status */
	uint8 reason;		/* reason code */
	struct ether_addr init_ndi;	/* ndp initiator's data interface address */
	uint8 ndp_id;		/* ndp identifier (created by initiator */
	uint8 control;		/* ndp control field */
	uint8 var[];		/* Optional fields follow */
} BWL_POST_PACKED_STRUCT wifi_nan_ndp_attr_t;
/* NDP attribute type and status macros */
#define NAN_NDP_TYPE_MASK	0x0F
#define NAN_NDP_TYPE_REQUEST	0x0
#define NAN_NDP_TYPE_RESPONSE	0x1
#define NAN_NDP_TYPE_CONFIRM	0x2
#define NAN_NDP_TYPE_SECURITY	0x3
#define NAN_NDP_TYPE_TERMINATE	0x4
#define NAN_NDP_REQUEST(_ndp)	(((_ndp)->type_status & NAN_NDP_TYPE_MASK) == NAN_NDP_TYPE_REQUEST)
#define NAN_NDP_RESPONSE(_ndp)	(((_ndp)->type_status & NAN_NDP_TYPE_MASK) == NAN_NDP_TYPE_RESPONSE)
#define NAN_NDP_CONFIRM(_ndp)	(((_ndp)->type_status & NAN_NDP_TYPE_MASK) == NAN_NDP_TYPE_CONFIRM)
#define NAN_NDP_SECURITY_INST(_ndp)	(((_ndp)->type_status & NAN_NDP_TYPE_MASK) == \
									NAN_NDP_TYPE_SECURITY)
#define NAN_NDP_TERMINATE(_ndp) (((_ndp)->type_status & NAN_NDP_TYPE_MASK) == \
									NAN_NDP_TYPE_TERMINATE)
#define NAN_NDP_STATUS_SHIFT	4
#define NAN_NDP_STATUS_MASK	0xF0
#define NAN_NDP_STATUS_CONT	(0 << NAN_NDP_STATUS_SHIFT)
#define NAN_NDP_STATUS_ACCEPT	(1 << NAN_NDP_STATUS_SHIFT)
#define NAN_NDP_STATUS_REJECT	(2 << NAN_NDP_STATUS_SHIFT)
#define NAN_NDP_CONT(_ndp)	(((_ndp)->type_status & NAN_NDP_STATUS_MASK) == NAN_NDP_STATUS_CONT)
#define NAN_NDP_ACCEPT(_ndp)	(((_ndp)->type_status & NAN_NDP_STATUS_MASK) == \
									NAN_NDP_STATUS_ACCEPT)
#define NAN_NDP_REJECT(_ndp)	(((_ndp)->type_status & NAN_NDP_STATUS_MASK) == \
									NAN_NDP_STATUS_REJECT)

#define NAN_NDP_FRM_STATUS(_ndp) \
	(((_ndp)->type_status & NAN_NDP_STATUS_MASK) >> NAN_NDP_STATUS_SHIFT)

/* NDP Setup Status */
#define NAN_NDP_SETUP_STATUS_OK		1
#define NAN_NDP_SETUP_STATUS_FAIL	0
#define NAN_NDP_SETUP_STATUS_REJECT	2

/* NDPE TLV list */
#define NDPE_TLV_TYPE_IPV6		0x00
#define NDPE_TLV_TYPE_SVC_INFO		0x01
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_ndpe_tlv_s {
	uint8 type;		/* Operating Class */
	uint16 length;		/* Channel Bitmap */
	uint8 data[];
} BWL_POST_PACKED_STRUCT wifi_nan_ndpe_tlv_t;

/* Rng setup attribute type and status macros */
#define NAN_RNG_TYPE_MASK	0x0F
#define NAN_RNG_TYPE_REQUEST	0x0
#define NAN_RNG_TYPE_RESPONSE	0x1
#define NAN_RNG_TYPE_TERMINATE	0x2

#define NAN_RNG_STATUS_SHIFT	4
#define NAN_RNG_STATUS_MASK	0xF0
#define NAN_RNG_STATUS_ACCEPT	(0 << NAN_RNG_STATUS_SHIFT)
#define NAN_RNG_STATUS_REJECT	(1 << NAN_RNG_STATUS_SHIFT)

#define NAN_RNG_ACCEPT(_rsua)	(((_rsua)->type_status & NAN_RNG_STATUS_MASK) == \
									NAN_RNG_STATUS_ACCEPT)
#define NAN_RNG_REJECT(_rsua)	(((_rsua)->type_status & NAN_RNG_STATUS_MASK) == \
									NAN_RNG_STATUS_REJECT)

/* schedule entry */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_sched_entry_s {
	uint8 map_id;		/* map id */
	uint16 tbmp_ctrl;	/* time bitmap control */
	uint8 tbmp_len;		/* time bitmap len */
	uint8 tbmp[];		/* time bitmap - Optional */
} BWL_POST_PACKED_STRUCT wifi_nan_sched_entry_t;

#define NAN_SCHED_ENTRY_MAPID_MASK	0x0F
#define NAN_SCHED_ENTRY_MIN_SIZE	OFFSETOF(wifi_nan_sched_entry_t, tbmp)
#define NAN_SCHED_ENTRY_SIZE(_entry)	(NAN_SCHED_ENTRY_MIN_SIZE + (_entry)->tbmp_len)

/* for dev cap, element container etc. */
#define NAN_DEV_ELE_MAPID_CTRL_MASK		0x1
#define NAN_DEV_ELE_MAPID_CTRL_SHIFT	0
#define NAN_DEV_ELE_MAPID_MASK		0x1E
#define NAN_DEV_ELE_MAPID_SHIFT		1

#define NAN_DEV_ELE_MAPID_CTRL_SET(_mapid_field, value) \
	do {(_mapid_field) &= ~NAN_DEV_ELE_MAPID_CTRL_MASK; \
		(_mapid_field) |= ((value << NAN_DEV_ELE_MAPID_CTRL_SHIFT) & \
		NAN_DEV_ELE_MAPID_CTRL_MASK); \
	} while (0);

#define NAN_DEV_ELE_MAPID_CTRL_GET(_mapid_field) \
	(((_mapid_field) & NAN_DEV_ELE_MAPID_CTRL_MASK) >> \
	NAN_DEV_ELE_MAPID_CTRL_SHIFT)

#define NAN_DEV_ELE_MAPID_SET(_mapid_field, value) \
	do {(_mapid_field) &= ~NAN_DEV_ELE_MAPID_MASK; \
		(_mapid_field) |= ((value << NAN_DEV_ELE_MAPID_SHIFT) & \
		NAN_DEV_ELE_MAPID_MASK); \
	} while (0);

#define NAN_DEV_ELE_MAPID_GET(_mapid_field) \
	(((_mapid_field) & NAN_DEV_ELE_MAPID_MASK) >> \
	NAN_DEV_ELE_MAPID_SHIFT)

/* schedule entry map id handling */
#define NAN_SCHED_ENTRY_MAPID_MASK		0x0F
#define NAN_SCHED_ENTRY_MAPID_SHIFT		0

#define NAN_SCHED_ENTRY_MAPID_SET(_mapid_field, value) \
	do {(_mapid_field) &= ~NAN_SCHED_ENTRY_MAPID_MASK; \
		(_mapid_field) |= ((value << NAN_SCHED_ENTRY_MAPID_SHIFT) & \
		NAN_SCHED_ENTRY_MAPID_MASK); \
	} while (0);

#define NAN_SCHED_ENTRY_MAPID_GET(_mapid_field) \
	(((_mapid_field) & NAN_SCHED_ENTRY_MAPID_MASK) >> \
	NAN_SCHED_ENTRY_MAPID_SHIFT)

/* NDC attribute */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_ndc_attr_s {
	uint8 id;
	uint16 len;
	uint8 ndc_id[NAN_DATA_NDC_ID_SIZE];
	uint8 attr_cntrl;
	uint8 var[];
} BWL_POST_PACKED_STRUCT wifi_nan_ndc_attr_t;

/* Attribute control subfield of NDC attr */
/* Proposed NDC */
#define NAN_NDC_ATTR_PROPOSED_NDC_MASK	0x1
#define NAN_NDC_ATTR_PROPOSED_NDC_SHIFT 0

/* get & set */
#define NAN_NDC_GET_PROPOSED_FLAG(_attr)	\
	(((_attr)->attr_cntrl & NAN_NDC_ATTR_PROPOSED_NDC_MASK) >>	\
	NAN_NDC_ATTR_PROPOSED_NDC_SHIFT)
#define NAN_NDC_SET_PROPOSED_FLAG(_attr, value) \
	do {((_attr)->attr_cntrl &= ~NAN_NDC_ATTR_PROPOSED_NDC_MASK); \
		((_attr)->attr_cntrl |=	\
		(((value) << NAN_NDC_ATTR_PROPOSED_NDC_SHIFT) & NAN_NDC_ATTR_PROPOSED_NDC_MASK)); \
	} while (0)

/* Service descriptor extension attribute */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_svc_desc_ext_attr_s {
	/* Attribute ID - 0x11 */
	uint8 id;
	/* Length of the following fields in the attribute */
	uint16 len;
	/* Instance id of associated service descriptor attribute */
	uint8 instance_id;
	/* SDE control field */
	uint16 control;
	/* range limit, svc upd indicator etc. */
	uint8 var[];
} BWL_POST_PACKED_STRUCT wifi_nan_svc_desc_ext_attr_t;

#define NAN_SDE_ATTR_MIN_LEN OFFSETOF(wifi_nan_svc_desc_ext_attr_t, var)
#define	NAN_SDE_ATTR_RANGE_LEN			4
#define	NAN_SDE_ATTR_SUI_LEN			1
#define	NAN_SDE_ATTR_INFO_LEN_PARAM_LEN		2
#define	NAN_SDE_ATTR_RANGE_INGRESS_LEN		2
#define	NAN_SDE_ATTR_RANGE_EGRESS_LEN		2
#define NAN_SDE_ATTR_CTRL_LEN			2
/* max length of variable length field (matching filter, service response filter,
 * or service info) in service descriptor attribute
 */
#define NAN_DISC_SDA_FIELD_MAX_LEN	255

/* SDEA control field bit definitions and access macros */
#define NAN_SDE_CF_FSD_REQUIRED		(1 << 0)
#define NAN_SDE_CF_FSD_GAS		(1 << 1)
#define NAN_SDE_CF_DP_REQUIRED		(1 << 2)
#define NAN_SDE_CF_DP_TYPE		(1 << 3)
#define NAN_SDE_CF_MULTICAST_TYPE	(1 << 4)
#define NAN_SDE_CF_QOS_REQUIRED		(1 << 5)
#define NAN_SDE_CF_SECURITY_REQUIRED	(1 << 6)
#define NAN_SDE_CF_RANGING_REQUIRED	(1 << 7)
#define NAN_SDE_CF_RANGE_PRESENT	(1 << 8)
#define NAN_SDE_CF_SVC_UPD_IND_PRESENT	(1 << 9)
/* Using Reserved Bits as per Spec */
#define NAN_SDE_CF_LIFE_CNT_PUB_RX      (1 << 15)
#define NAN_SDE_FSD_REQUIRED(_sde)	((_sde)->control & NAN_SDE_CF_FSD_REQUIRED)
#define NAN_SDE_FSD_GAS(_sde)		((_sde)->control & NAN_SDE_CF_FSD_GAS)
#define NAN_SDE_DP_REQUIRED(_sde)	((_sde)->control & NAN_SDE_CF_DP_REQUIRED)
#define NAN_SDE_DP_MULTICAST(_sde)	((_sde)->control & NAN_SDE_CF_DP_TYPE)
#define NAN_SDE_MULTICAST_M_TO_M(_sde)	((_sde)->control & NAN_SDE_CF_MULTICAST_TYPE)
#define NAN_SDE_QOS_REQUIRED(_sde)	((_sde)->control & NAN_SDE_CF_QOS_REQUIRED)
#define NAN_SDE_SECURITY_REQUIRED(_sde)	((_sde)->control & NAN_SDE_CF_SECURITY_REQUIRED)
#define NAN_SDE_RANGING_REQUIRED(_sde)	((_sde)->control & NAN_SDE_CF_RANGING_REQUIRED)
#define NAN_SDE_RANGE_PRESENT(_sde)	((_sde)->control & NAN_SDE_CF_RANGE_PRESENT)
#define NAN_SDE_SVC_UPD_IND_PRESENT(_sde)	((_sde)->control & NAN_SDE_CF_SVC_UPD_IND_PRESENT)
#define NAN_SDE_LIFE_COUNT_FOR_PUB_RX(_sde)     (_sde & NAN_SDE_CF_LIFE_CNT_PUB_RX)

/* nan2 security */

/*
 * Cipher suite information Attribute.
 * WFA Tech. Spec ver 1.0.r21 (section 10.7.24.2)
 */
#define NAN_SEC_CIPHER_SUITE_CAP_REPLAY_4     0
#define NAN_SEC_CIPHER_SUITE_CAP_REPLAY_16    (1 << 0)

/* enum security algo.
*/
enum nan_sec_csid {
	NAN_SEC_ALGO_NONE = 0,
	NAN_SEC_ALGO_NCS_SK_CCM_128 = 1,     /* CCMP 128 */
	NAN_SEC_ALGO_NCS_SK_GCM_256 = 2,     /* GCMP 256 */
	NAN_SEC_ALGO_LAST = 3
};
typedef int8 nan_sec_csid_e;

/* nan2 cipher suite attribute field */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_sec_cipher_suite_field_s {
	uint8 cipher_suite_id;
	uint8 inst_id;	/* Instance Id */
} BWL_POST_PACKED_STRUCT wifi_nan_sec_cipher_suite_field_t;

/* nan2 cipher suite information attribute field */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_sec_cipher_suite_info_attr_s {
	uint8 attr_id;	/* 0x22 - NAN_ATTR_CIPHER_SUITE_INFO */
	uint16 len;
	uint8 capabilities;
	uint8 var[];	/* cipher suite list */
} BWL_POST_PACKED_STRUCT wifi_nan_sec_cipher_suite_info_attr_t;

/*
 * Security context identifier attribute
 * WFA Tech. Spec ver 1.0.r21 (section 10.7.24.4)
 */

#define NAN_SEC_CTX_ID_TYPE_PMKID   (1 << 0)

/* nan2 security context identifier attribute field */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_sec_ctx_id_field_s {
	uint16 sec_ctx_id_type_len;	/* length of security ctx identifier */
	uint8 sec_ctx_id_type;
	uint8 inst_id;	/* Instance Id */
	uint8 var[];	/* security ctx identifier */
} BWL_POST_PACKED_STRUCT wifi_nan_sec_ctx_id_field_t;

/* nan2 security context identifier info attribute field */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_sec_ctx_id_info_attr_s {
	uint8 attr_id;	/* 0x23 - NAN_ATTR_SEC_CTX_ID_INFO */
	uint16 len;
	uint8 var[];	/* security context identifier  list */
} BWL_POST_PACKED_STRUCT wifi_nan_sec_ctx_id_info_attr_t;

/*
 * Nan shared key descriptor attribute
 * WFA Tech. Spec ver 23
 */

#define NAN_SEC_NCSSK_DESC_REPLAY_CNT_LEN	8
#define NAN_SEC_NCSSK_DESC_KEY_NONCE_LEN	32

/* nan shared key descriptor attr field */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_sec_ncssk_key_desc_attr_s {
	uint8 attr_id;	/* 0x24 - NAN_ATTR_SHARED_KEY_DESC */
	uint16 len;
	uint8 inst_id;  /* Publish service instance ID */
	uint8 desc_type;
	uint16 key_info;
	uint16 key_len;
	uint8 key_replay_cntr[NAN_SEC_NCSSK_DESC_REPLAY_CNT_LEN];
	uint8 key_nonce[NAN_SEC_NCSSK_DESC_KEY_NONCE_LEN];
	uint8 reserved[32];	/* EAPOL IV + Key RSC + Rsvd fields in EAPOL Key */
	uint8 mic[];  /* mic + key data len + key data */
} BWL_POST_PACKED_STRUCT wifi_nan_sec_ncssk_key_desc_attr_t;

/* Key Info fields */
#define NAN_SEC_NCSSK_DESC_MASK			0x7
#define NAN_SEC_NCSSK_DESC_SHIFT		0
#define NAN_SEC_NCSSK_DESC_KEY_TYPE_MASK	0x8
#define NAN_SEC_NCSSK_DESC_KEY_TYPE_SHIFT	3
#define NAN_SEC_NCSSK_DESC_KEY_INSTALL_MASK	0x40
#define NAN_SEC_NCSSK_DESC_KEY_INSTALL_SHIFT	6
#define NAN_SEC_NCSSK_DESC_KEY_ACK_MASK		0x80
#define NAN_SEC_NCSSK_DESC_KEY_ACK_SHIFT	7
#define NAN_SEC_NCSSK_DESC_KEY_MIC_MASK		0x100
#define NAN_SEC_NCSSK_DESC_KEY_MIC_SHIFT	8
#define NAN_SEC_NCSSK_DESC_KEY_SEC_MASK		0x200
#define NAN_SEC_NCSSK_DESC_KEY_SEC_SHIFT	9
#define NAN_SEC_NCSSK_DESC_KEY_ERR_MASK		0x400
#define NAN_SEC_NCSSK_DESC_KEY_ERR_SHIFT	10
#define NAN_SEC_NCSSK_DESC_KEY_REQ_MASK		0x800
#define NAN_SEC_NCSSK_DESC_KEY_REQ_SHIFT	11
#define NAN_SEC_NCSSK_DESC_KEY_ENC_KEY_MASK	0x1000
#define NAN_SEC_NCSSK_DESC_KEY_ENC_KEY_SHIFT	12
#define NAN_SEC_NCSSK_DESC_KEY_SMK_MSG_MASK	0x2000
#define NAN_SEC_NCSSK_DESC_KEY_SMK_MSG_SHIFT	13

/* Key Info get & set macros */
#define NAN_SEC_NCSSK_KEY_DESC_VER_GET(_key_info)	\
	(((_key_info) & NAN_SEC_NCSSK_DESC_MASK) >> NAN_SEC_NCSSK_DESC_SHIFT)
#define NAN_SEC_NCSSK_KEY_DESC_VER_SET(_val, _key_info)	\
	do {(_key_info) &= ~NAN_SEC_NCSSK_DESC_MASK; \
		(_key_info) |= (((_val) << NAN_SEC_NCSSK_DESC_SHIFT) & \
		NAN_SEC_NCSSK_DESC_MASK);} while (0)
#define NAN_SEC_NCSSK_DESC_KEY_TYPE_GET(_key_info)	\
	(((_key_info) & NAN_SEC_NCSSK_DESC_KEY_TYPE_MASK) >> NAN_SEC_NCSSK_DESC_KEY_TYPE_SHIFT)
#define NAN_SEC_NCSSK_DESC_KEY_TYPE_SET(_val, _key_info)	\
	do {(_key_info) &= ~NAN_SEC_NCSSK_DESC_KEY_TYPE_MASK; \
		(_key_info) |= (((_val) << NAN_SEC_NCSSK_DESC_KEY_TYPE_SHIFT) & \
		NAN_SEC_NCSSK_DESC_KEY_TYPE_MASK);} while (0)
#define NAN_SEC_NCSSK_DESC_KEY_INSTALL_GET(_key_info)	\
	(((_key_info) & NAN_SEC_NCSSK_DESC_KEY_INSTALL_MASK) >> \
	NAN_SEC_NCSSK_DESC_KEY_INSTALL_SHIFT)
#define NAN_SEC_NCSSK_DESC_KEY_INSTALL_SET(_val, _key_info)	\
	do {(_key_info) &= ~NAN_SEC_NCSSK_DESC_KEY_INSTALL_MASK; \
		(_key_info) |= (((_val) << NAN_SEC_NCSSK_DESC_KEY_INSTALL_SHIFT) & \
		NAN_SEC_NCSSK_DESC_KEY_INSTALL_MASK);} while (0)
#define NAN_SEC_NCSSK_DESC_KEY_ACK_GET(_key_info)	\
	(((_key_info) & NAN_SEC_NCSSK_DESC_KEY_ACK_MASK) >> NAN_SEC_NCSSK_DESC_KEY_ACK_SHIFT)
#define NAN_SEC_NCSSK_DESC_KEY_ACK_SET(_val, _key_info)	\
	do {(_key_info) &= ~NAN_SEC_NCSSK_DESC_KEY_ACK_MASK; \
		(_key_info) |= (((_val) << NAN_SEC_NCSSK_DESC_KEY_ACK_SHIFT) & \
		NAN_SEC_NCSSK_DESC_KEY_ACK_MASK);} while (0)
#define NAN_SEC_NCSSK_DESC_KEY_MIC_GET(_key_info)	\
	(((_key_info) & NAN_SEC_NCSSK_DESC_KEY_MIC_MASK) >> NAN_SEC_NCSSK_DESC_KEY_MIC_SHIFT)
#define NAN_SEC_NCSSK_DESC_KEY_MIC_SET(_val, _key_info)	\
	do {(_key_info) &= ~NAN_SEC_NCSSK_DESC_KEY_MIC_MASK; \
		(_key_info) |= (((_val) << NAN_SEC_NCSSK_DESC_KEY_MIC_SHIFT) & \
		NAN_SEC_NCSSK_DESC_KEY_MIC_MASK);} while (0)
#define NAN_SEC_NCSSK_DESC_KEY_SEC_GET(_key_info)	\
	(((_key_info) & NAN_SEC_NCSSK_DESC_KEY_SEC_MASK) >> NAN_SEC_NCSSK_DESC_KEY_SEC_SHIFT)
#define NAN_SEC_NCSSK_DESC_KEY_SEC_SET(_val, _key_info)	\
	do {(_key_info) &= ~NAN_SEC_NCSSK_DESC_KEY_SEC_MASK; \
		(_key_info) |= (((_val) << NAN_SEC_NCSSK_DESC_KEY_SEC_SHIFT) & \
		NAN_SEC_NCSSK_DESC_KEY_SEC_MASK);} while (0)
#define NAN_SEC_NCSSK_DESC_KEY_ERR_GET(_key_info)	\
	(((_key_info) & NAN_SEC_NCSSK_DESC_KEY_ERR_MASK) >> NAN_SEC_NCSSK_DESC_KEY_ERR_SHIFT)
#define NAN_SEC_NCSSK_DESC_KEY_ERR_SET(_val, _key_info)	\
	do {(_key_info) &= ~NAN_SEC_NCSSK_DESC_KEY_ERR_MASK; \
		(_key_info) |= (((_val) << NAN_SEC_NCSSK_DESC_KEY_ERR_SHIFT) & \
		NAN_SEC_NCSSK_DESC_KEY_ERR_MASK);} while (0)
#define NAN_SEC_NCSSK_DESC_KEY_REQ_GET(_key_info)	\
	(((_key_info) & NAN_SEC_NCSSK_DESC_KEY_REQ_MASK) >> NAN_SEC_NCSSK_DESC_KEY_REQ_SHIFT)
#define NAN_SEC_NCSSK_DESC_KEY_REQ_SET(_val, _key_info)	\
	do {(_key_info) &= ~NAN_SEC_NCSSK_DESC_KEY_REQ_MASK; \
		(_key_info) |= (((_val) << NAN_SEC_NCSSK_DESC_KEY_REQ_SHIFT) & \
		NAN_SEC_NCSSK_DESC_KEY_REQ_MASK);} while (0)
#define NAN_SEC_NCSSK_DESC_KEY_ENC_KEY_GET(_key_info)	\
	(((_key_info) & NAN_SEC_NCSSK_DESC_KEY_ENC_KEY_MASK) >> \
	NAN_SEC_NCSSK_DESC_KEY_ENC_KEY_SHIFT)
#define NAN_SEC_NCSSK_DESC_KEY_ENC_KEY_SET(_val, _key_info)	\
	do {(_key_info) &= ~NAN_SEC_NCSSK_DESC_KEY_ENC_KEY_MASK; \
		(_key_info) |= (((_val) << NAN_SEC_NCSSK_DESC_KEY_ENC_KEY_SHIFT) & \
		NAN_SEC_NCSSK_DESC_KEY_ENC_KEY_MASK);} while (0)
#define NAN_SEC_NCSSK_DESC_KEY_SMK_MSG_GET(_key_info)	\
	(((_key_info) & NAN_SEC_NCSSK_DESC_KEY_SMK_MSG_MASK) >> \
	NAN_SEC_NCSSK_DESC_KEY_SMK_MSG_SHIFT)
#define NAN_SEC_NCSSK_DESC_KEY_SMK_MSG_SET(_val, _key_info)	\
	do {(_key_info) &= ~NAN_SEC_NCSSK_DESC_KEY_SMK_MSG_MASK; \
		(_key_info) |= (((_val) << NAN_SEC_NCSSK_DESC_KEY_SMK_MSG_SHIFT) & \
		NAN_SEC_NCSSK_DESC_KEY_SMK_MSG_MASK);} while (0)

#define NAN_SEC_NCSSK_IEEE80211_KDESC_TYPE	2	/* IEEE 802.11 Key Descriptor Type */
#define NAN_SEC_NCSSK_KEY_DESC_VER			0	/* NCSSK-128/256 */
#define NAN_SEC_NCSSK_KEY_TYPE_PAIRWISE		1	/* Pairwise */
#define NAN_SEC_NCSSK_LIFETIME_KDE			7	/* Lifetime KDE type */

/* TODO include MTK related attributes */

/* NAN Multicast service group(NMSG) definitions */
/* Length of NMSG_ID -- (NDI * 2^16 + pub_id * 2^8 + Random_factor) */
#define NAN_NMSG_ID_LEN                         8

#define NAN_NMSG_TYPE_MASK			0x0F
#define NMSG_ATTR_TYPE_STATUS_REQUEST		0x00
#define NMSG_ATTR_TYPE_STATUS_RESPONSE		0x01
#define NMSG_ATTR_TYPE_STATUS_CONFIRM		0x02
#define NMSG_ATTR_TYPE_STATUS_SEC_INSTALL	0x03
#define NMSG_ATTR_TYPE_STATUS_TERMINATE		0x04
#define NMSG_ATTR_TYPE_STATUS_IMPLICIT_ENROL	0x05

#define NMSG_ATTR_TYPE_STATUS_CONTINUED	        0x00
#define NMSG_ATTR_TYPE_STATUS_ACCEPTED	        0x10
#define NMSG_ATTR_TYPE_STATUS_REJECTED	        0x20

#define NMSG_CTRL_PUB_ID_PRESENT                0x0001
#define NMSG_CTRL_NMSG_ID_PRESENT               0x0002
#define NMSG_CTRL_SECURITY_PRESENT              0x0004
#define NMSG_CTRL_MANY_TO_MANY_PRESENT          0x0008
#define NMSG_CTRL_SVC_INFO_PRESENT              0x0010

/* NMSG attribute */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_nmsg_attr_s {
	uint8 id; /* Attribute ID - 0x11 */
	uint16 len; /* Length including pubid, NMSGID and svc info */
	uint8 dialog_token;
	uint8 type_status; /* Type and Status field byte */
	uint8 reason_code;
	uint8 mc_id; /* Multicast id similar to NDPID */
	uint8 nmsg_ctrl; /* NMSG control field */
	/* Optional publish id, NMSGID and svc info are included in var[] */
	uint8 var[0];
} BWL_POST_PACKED_STRUCT wifi_nan_nmsg_attr_t;

#define NMSG_ATTR_MCAST_SCHED_MAP_ID_MASK     0x1E
#define NMSG_ATTR_MCAST_SCHED_MAP_ID_SHIFT    1
#define NMSG_ATTR_MCAST_SCHED_TIME_MAP_MASK   0x20
#define NMSG_ATTR_MCAST_SCHED_TIME_MAP_SHIFT  5

/* NAN Multicast Schedule atribute structure */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_mcast_sched_attr_s {
	uint8 id; /* 0x16 */
	uint16 len;
	uint8 nmsg_id[NAN_NMSG_ID_LEN];
	uint8 attr_cntrl;
	uint8 sched_own[ETHER_ADDR_LEN];
	uint8 var[]; /* multicast sched entry list (schedule_entry_list) */
} BWL_POST_PACKED_STRUCT wifi_nan_mcast_sched_attr_t;

/* FAC Channel Entry  (section 10.7.19.1.5) */
typedef BWL_PRE_PACKED_STRUCT struct wifi_nan_fac_chan_entry_s {
	uint8 oper_class;		/* Operating Class */
	uint16 chan_bitmap;		/* Channel Bitmap */
	uint8 primary_chan_bmp;		/* Primary Channel Bitmap */
	uint16 aux_chan;			/* Auxiliary Channel bitmap */
} BWL_POST_PACKED_STRUCT wifi_nan_fac_chan_entry_t;

/* TODO move this from nan.h */
#define NAN_ALL_NAN_MGMT_FRAMES (NAN_FRM_SCHED_AF | \
	NAN_FRM_NDP_AF | NAN_FRM_NDL_AF | \
	NAN_FRM_DISC_BCN | NAN_FRM_SYNC_BCN | \
	NAN_FRM_SVC_DISC | NAN_FRM_RNG_REQ_AF | \
	NAN_FRM_RNG_RESP_AF | NAN_FRM_RNG_REPORT_AF | \
	NAN_FRM_RNG_TERM_AF)

/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

#endif /* _NAN_H_ */
