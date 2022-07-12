/*
 * Fundamental types and constants relating to FILS AUTHENTICATION
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

#ifndef _FILSAUTH_H_
#define _FILSAUTH_H_

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>

/* 11ai D6.0 8.6.8.36 FILS Discovery frame format
	category
	action
	fils_discovery_info_field_t
	fils_rnr_element_t
	fils_indication_element_t
	fils_vendor_specific_element_t
*/

/* 11revmc D4.0 8.4.2.25 Vendor Specific element */
typedef BWL_PRE_PACKED_STRUCT struct fils_vendor_specific_element {
	uint8		elementid;
	uint8		length;
	/* variable len info */
	uint8		orgid_vendorspecific_content[];
} BWL_POST_PACKED_STRUCT fils_vendor_specific_element_t;

#define FILS_VS_ELEM_HDR_LEN	(sizeof(fils_vendor_specific_element_t))

/* 11ai D6.0 8.4.2.178 FILS Indication element */
typedef BWL_PRE_PACKED_STRUCT struct fils_indication_element {
	uint8		elementid;
	uint8		length;
	uint16		fils_info;
	/* variable len info */
	uint8		cache_domain_publickey_id[];
} BWL_POST_PACKED_STRUCT fils_indication_element_t;

#define FILS_INDICATION_ELEM_HDR_LEN	(sizeof(fils_indication_element_t))

#define FILS_INDICATION_IE_TAG_FIXED_LEN		2

#define FI_INFO_CACHE_IND_SUBFIELD_SIZE			2

/* FILS Indication Information field */
#define FI_INFO_PUB_KEY_IDENTS_MASK		(0x0007)
#define FI_INFO_REALM_IDENTS_MASK		(0x0038)
#define FI_INFO_IP_ADDR_CFG_MASK		(0x0040)
#define FI_INFO_CACHE_IDENT_MASK		(0x0080)
#define FI_INFO_HESSID_MASK			(0x0100)
#define FI_INFO_SHRKEY_AUTH_WOPFS_MASK		(0x0200)
#define FI_INFO_SHRKEY_AUTH_WPFS_MASK		(0x0400)
#define FI_INFO_PUBKEY_AUTH_MASK		(0x0800)

#define FI_INFO_CACHE_IDENT(fc)			((fc & FI_INFO_CACHE_IDENT_MASK))
#define FI_INFO_HESSID(fc)			((fc & FI_INFO_HESSID_MASK))
#define FI_INFO_SHRKEY_AUTH_WOPFS(fc)		((fc & FI_INFO_SHRKEY_AUTH_WOPFS_MASK))
#define FI_INFO_SHRKEY_AUTH_WPFS(fc)		((fc & FI_INFO_SHRKEY_AUTH_WPFS_MASK))

/* 11ai D11.0 9.4.2.171.1 TBTT Information field */
typedef BWL_PRE_PACKED_STRUCT struct tbtt_info_field {
	uint8		tbtt_offset;
	uint8		bssid[ETHER_ADDR_LEN];
	uint32		short_ssid;
	uint8		bss_params;	/* BSS paramters subfield */
	uint8		mhz_psd;	/* 20 MHz PSD subfield */
} BWL_POST_PACKED_STRUCT tbtt_info_field_t;

/* TBTT Information Length subfield value */
typedef enum {
	TBTTINFO_LEN_1 = 1, /* Neighbor AP TBTT Offset */
	TBTTINFO_LEN_2 = 2, /* Neighbor AP TBTT Offset and BSS Parameters */
	TBTTINFO_LEN_3 = 3, /* Reserved */
	TBTTINFO_LEN_4 = 4, /* Reserved */
	TBTTINFO_LEN_5 = 5, /* Neighbor AP TBTT Offset and the Short-SSID */
	TBTTINFO_LEN_6 = 6, /* Neighbor AP TBTT Offset, Short-SSID subfield, BSS Parameters */
	TBTTINFO_LEN_7 = 7,	/* Neighbor AP TBTT Offset, BSSID subfields */
	TBTTINFO_LEN_8 = 8, /* Neighbor AP TBTT Offset, BSSID subfield, BSS Parameters */
	TBTTINFO_LEN_9 = 9, /* TBTT Offset, BSSID, BSS Parameters, 20 MHz PSD */
	TBTTINFO_LEN_10 = 10, /* Reserved */
	TBTTINFO_LEN_11 = 11, /* Neighbor AP TBTT Offset, BSSID subfield, short-SSID subfield */
	TBTTINFO_LEN_12 = 12, /* TBTT Offset, BSSID subfield, short-SSID subfield, BSS params */
	TBTTINFO_LEN_13 = 13, /* TBTT Offset, BSSID, short-SSID subfield, BSS params, 20MHz */
	/* 14 - 255 reserved */
} tbttinfo_len_t;

/* TBTT Information field contents */
enum {
	NEIGHBOR_AP_TBTT_SUBFIELD_SHIFT	= 0,	/* Neighbor AP TBTT Offset */
	BSSID_SUBFIELD_SHIFT	= 1,	/* BSSID (optional) */
	SHORT_SSID_SUBFIELD_SHIFT	= 2,	/* Short-SSID (optional) */
	BSS_PARAMETERS_SUBFIELD_SHIFT		= 3,	/* BSS parameters */
	MHZ_SUBFIELD_SHIFT	= 4	/* 20 MHz PSD */
};

/* Length in Octets */
#define NEIGHBOR_AP_TBTT_SUBFIELD_LEN	1
#define BSSID_SUBFIELD_LEN	6
#define SHORT_SSID_SUBFIELD_LEN	4
#define BSS_PARAMETERS_SUBFIELD_LEN	1
#define MHZ_SUBFIELD_LEN	1

#define NEIGHBOR_AP_TBTT_SUBFIELD	(1 << NEIGHBOR_AP_TBTT_SUBFIELD_SHIFT)
#define BSSID_SUBFIELD	(1 << BSSID_SUBFIELD_SHIFT)
#define SHORT_SSID_SUBFIELD	(1 << SHORT_SSID_SUBFIELD_SHIFT)
#define BSS_PARAMETERS_SUBFIELD	(1 << BSS_PARAMETERS_SUBFIELD_SHIFT)
#define MHZ_SUBFIELD	(1 << MHZ_SUBFIELD_SHIFT)

/* BSS paramters subfield 802.11ax D8.0 */
#define TBTT_INFO_BSS_PARAMS_OCT_IDX	0	/* OCT Recommended subfield */
#define TBTT_INFO_BSS_PARAMS_OCT_FSZ	1
#define TBTT_INFO_BSS_PARAMS_SSSID_IDX	1	/* Same SSID subfield */
#define TBTT_INFO_BSS_PARAMS_SSSID_FSZ	1
#define TBTT_INFO_BSS_PARAMS_MBSSID_IDX	2	/* Multiple BSSID subfield */
#define TBTT_INFO_BSS_PARAMS_MBSSID_FSZ	1
#define TBTT_INFO_BSS_PARAMS_TBSSID_IDX	3	/* Transmitted BSSID subfield */
#define TBTT_INFO_BSS_PARAMS_TBSSID_FSZ	1
#define TBTT_INFO_BSS_PARAMS_ESS_COLOCATED_IDX	4	/* ESS With 2.4/5 GHz Co-Located AP */
#define TBTT_INFO_BSS_PARAMS_ESS_COLOCATED_FSZ	1
#define TBTT_INFO_BSS_PARAMS_UNSOLPB_IDX	5	/* Unsolicited Probe Responses Active */
#define TBTT_INFO_BSS_PARAMS_UNSOLPB_FSZ	1
#define TBTT_INFO_BSS_PARAMS_COLOC_AP_IDX	6	/* Co-Located AP subfield */
#define TBTT_INFO_BSS_PARAMS_COLOC_AP_FSZ	1
/* Bit 7 is reserved */

#define TBTT_INFO_FIELD_HDR_LEN	(sizeof(tbtt_info_field_t))

/* 11ai D11.0 9.4.2.171.1 Neighbor AP Information field */
typedef BWL_PRE_PACKED_STRUCT struct neighbor_ap_info_field {
	uint16		tbtt_info_header;
	uint8		op_class;
	uint8		channel;
	/* variable len info */
	uint8		tbtt_info_field[];
} BWL_POST_PACKED_STRUCT neighbor_ap_info_field_t;

#define NEIGHBOR_AP_INFO_FIELD_HDR_LEN	(sizeof(neighbor_ap_info_field_t))

/* 11ai D11.0 9.4.2.171 Reduced Neighbor Report element */
typedef BWL_PRE_PACKED_STRUCT struct fils_rnr_element {
	uint8		elementid;
	uint8		length;
	/* variable len info */
	uint8		neighbor_ap_info[];
} BWL_POST_PACKED_STRUCT fils_rnr_element_t;

#define FILS_RNR_ELEM_HDR_LEN	(sizeof(fils_rnr_element_t))

/* TBTT Info Header macros */
#define TBTT_INFO_HDR_FIELD_TYPE_MASK		(0x001f)
#define TBTT_INFO_HDR_FN_AP_MASK		(0x0004)
#define TBTT_INFO_HDR_COUNT_MASK		(0x00f0)
#define TBTT_INFO_HDR_LENGTH_MASK		(0xff00)

#define TBTT_INFO_HDR_FIELD_TYPE(hdr)\
	((hdr) & TBTT_INFO_HDR_FIELD_TYPE_MASK)
#define TBTT_INFO_HDR_FN_AP(hdr)\
	(((hdr) & TBTT_INFO_HDR_FN_AP_MASK) >> 2)
#define TBTT_INFO_HDR_COUNT(hdr)\
	(((hdr) & TBTT_INFO_HDR_COUNT_MASK) >> 4)
#define TBTT_INFO_HDR_LENGTH(hdr)\
	(((hdr) & TBTT_INFO_HDR_LENGTH_MASK) >> 8)

/* FILS Nonce element */
#define FILS_NONCE_LENGTH 16u

typedef BWL_PRE_PACKED_STRUCT struct fils_nonce_element {
	uint8		elementid;
	uint8		length;
	uint8		element_id_ext;
	uint8		fils_nonce[FILS_NONCE_LENGTH];
} BWL_POST_PACKED_STRUCT fils_nonce_element_t;

/* 11ai 9.4.2.186 FILS Key Delivery element */
#define FILS_KEY_RSC_LENGTH 8u

typedef BWL_PRE_PACKED_STRUCT struct fils_key_delivery_element {
	uint8		elementid;
	uint8		length;
	uint8		element_id_ext;
	uint8		key_rsc[FILS_KEY_RSC_LENGTH];
	uint8		kde_list[];	/* Key Data Elements */
} BWL_POST_PACKED_STRUCT fils_key_delivery_element_t;

/* 8.4.2.175 FILS Session element */
#define FILS_SESSION_LENGTH 8u

typedef BWL_PRE_PACKED_STRUCT struct fils_session_element {
	uint8		elementid;
	uint8		length;
	uint8		element_id_ext;
	uint8		fils_session[FILS_SESSION_LENGTH];
} BWL_POST_PACKED_STRUCT fils_session_element_t;

/* 9.4.2.179 FILS key confirmation element */
#define FILS_KEY_CONFIRMATION_HEADER_LEN 3u

typedef BWL_PRE_PACKED_STRUCT struct fils_key_conf_element {
	uint8		elementid;
	uint8		length;
	uint8		element_id_ext;
	/* variable len info */
	uint8		key_auth[];
} BWL_POST_PACKED_STRUCT fils_key_conf_element_t;

#define FILS_SESSION_ELEM_LEN	(sizeof(fils_session_element_t))

/* 8.4.2.174 FILS Key Confirmation element */
typedef BWL_PRE_PACKED_STRUCT struct fils_key_confirm_element {
	uint8		elementid;
	uint8		length;
	uint8		element_id_ext;
	/* variable len info */
	uint8		keyauth[];
} BWL_POST_PACKED_STRUCT fils_key_confirm_element_t;

#define FILS_CONFIRM_ELEM_HDR_LEN	(sizeof(fils_key_confirm_element_t))

/* 11ai D6.0 8.6.8.36 FILS Discovery frame format */
typedef BWL_PRE_PACKED_STRUCT struct fils_discovery_info_field {
	uint16		framecontrol;
	uint32		timestamp[2];
	uint16		bcninterval;
	/* variable len info */
	uint8		disc_info[];
} BWL_POST_PACKED_STRUCT fils_discovery_info_field_t;

#define FD_INFO_FIELD_HDR_LEN	(sizeof(fils_discovery_info_field_t))

#define FD_INFO_CAP_SUBFIELD_SIZE			2
#define FD_INFO_LENGTH_FIELD_SIZE			2

/* FILS Discovery Information field */
#define FD_INFO_SSID_LENGTH_MASK			(0x001f)
#define FD_INFO_CAP_IND_MASK				(0x0020)
#define FD_INFO_SHORT_SSID_IND_MASK			(0x0040)
#define FD_INFO_APCSN_IND_MASK				(0x0080)
#define FD_INFO_ANO_IND_MASK				(0x0100)
#define FD_INFO_CH_CENTER_FR_IND_MASK			(0x0200)
#define FD_INFO_PRIMARY_CH_IND_MASK			(0x0400)
#define FD_INFO_RSN_IND_MASK				(0x0800)
#define FD_INFO_LENGTH_IND_MASK				(0x1000)
#define FD_INFO_MD_IND_MASK				(0x2000)

#define FD_INFO_SET_SSID_LENGTH(fc, len)	(fc |= ((uint16)(len) & FD_INFO_SSID_LENGTH_MASK))
#define FD_INFO_SET_CAP_PRESENT(fc)			(fc |= FD_INFO_CAP_IND_MASK)
#define FD_INFO_SET_SHORT_SSID_PRESENT(fc)		(fc |= FD_INFO_SHORT_SSID_IND_MASK)
#define FD_INFO_SET_APCSN_PRESENT(fc)			((fc |= FD_INFO_APCSN_IND_MASK)
#define FD_INFO_SET_ANO_PRESENT(fc)			(fc |= FD_INFO_ANO_IND_MASK)
#define FD_INFO_SET_CH_CENTER_FR_PRESENT(fc)		(fc |= FD_INFO_CH_CENTER_FR_IND_MASK)
#define FD_INFO_SET_PRIMARY_CH_PRESENT(fc)		(fc |= FD_INFO_PRIMARY_CH_IND_MASK)
#define FD_INFO_SET_RSN_PRESENT(fc)			(fc |= FD_INFO_RSN_IND_MASK)
#define FD_INFO_SET_LENGTH_PRESENT(fc)			(fc |= FD_INFO_LENGTH_IND_MASK)
#define FD_INFO_SET_MD_PRESENT(fc)			(fc |= FD_INFO_MD_IND_MASK)

#define FD_INFO_SSID_LENGTH(fc)				((fc & FD_INFO_SSID_LENGTH_MASK))
#define FD_INFO_IS_CAP_PRESENT(fc)			((fc & FD_INFO_CAP_IND_MASK) >> 5)
#define FD_INFO_IS_SHORT_SSID_PRESENT(fc)		((fc & FD_INFO_SHORT_SSID_IND_MASK) >> 6)
#define FD_INFO_IS_APCSN_PRESENT(fc)			((fc & FD_INFO_APCSN_IND_MASK) >> 7)
#define FD_INFO_IS_ANO_PRESENT(fc)			((fc & FD_INFO_ANO_IND_MASK) >> 8)
#define FD_INFO_IS_CH_CENTER_FR_PRESENT(fc)		((fc & FD_INFO_CH_CENTER_FR_IND_MASK) >> 9)
#define FD_INFO_IS_PRIMARY_CH_PRESENT(fc)		((fc & FD_INFO_PRIMARY_CH_IND_MASK) >> 10)
#define FD_INFO_IS_RSN_PRESENT(fc)			((fc & FD_INFO_RSN_IND_MASK) >> 11)
#define FD_INFO_IS_LENGTH_PRESENT(fc)			((fc & FD_INFO_LENGTH_IND_MASK) >> 12)
#define FD_INFO_IS_MD_PRESENT(fc)			((fc & FD_INFO_MD_IND_MASK) >> 13)

/* FILS Discovery Capability subfield */
#define FD_CAP_ESS_MASK					(0x0001)
#define FD_CAP_PRIVACY_MASK				(0x0002)
#define FD_CAP_BSS_CH_WIDTH_MASK			(0x001c)
#define FD_CAP_MAX_NSS_MASK				(0x00e0)
#define FD_CAP_MULTI_BSS_MASK				(0x0200)
#define FD_CAP_PHY_INDEX_MASK				(0x1c00)
#define FD_CAP_FILS_MIN_RATE_MASK			(0xe000)

#define FD_CAP_ESS(cap)					((cap & FD_CAP_ESS_MASK))
#define FD_CAP_PRIVACY(cap)				((cap & FD_CAP_PRIVACY_MASK) >> 1)
#define FD_CAP_BSS_CH_WIDTH(cap)			((cap & FD_CAP_BSS_CH_WIDTH_MASK) >> 2)
#define FD_CAP_MAX_NSS(cap)				((cap & FD_CAP_MAX_NSS_MASK) >> 5)
#define FD_CAP_MULTI_BSS(cap)				((cap & FD_CAP_MULTI_BSS_MASK) >> 9)
#define FD_CAP_PHY_INDEX(cap)				((cap & FD_CAP_PHY_INDEX_MASK) >> 10)
#define FD_CAP_FILS_MIN_RATE(cap)			((cap & FD_CAP_FILS_MIN_RATE_MASK) >> 13)

#define FD_CAP_SET_ESS(cap)				((cap |= FD_CAP_ESS_MASK))
#define FD_CAP_SET_PRIVACY(cap)				((cap & FD_CAP_PRIVACY_MASK) >> 1)
#define FD_CAP_SET_BSS_CH_WIDTH(cap)			((cap & FD_CAP_BSS_CH_WIDTH_MASK) >> 2)
#define FD_CAP_SET_MAX_NSS(cap)				((cap & FD_CAP_MAX_NSS_MASK) >> 5)
#define FD_CAP_SET_MULTI_BSS(cap)			((cap & FD_CAP_MULTI_BSS_MASK) >> 9)
#define FD_CAP_SET_PHY_INDEX(cap)			((cap & FD_CAP_PHY_INDEX_MASK) >> 10)
#define FD_CAP_SET_FILS_MIN_RATE(cap)			((cap & FD_CAP_FILS_MIN_RATE_MASK) >> 13)

/* 11ai D6.0 8.4.2.173 FILS Request Parameters element */
typedef BWL_PRE_PACKED_STRUCT struct fils_request_parameters_element {
	uint8		elementid;
	uint8		length;
	uint8		element_id_ext;
	uint8		params_bitmap;
	/* variable len info */
	uint8		params_fields[];
} BWL_POST_PACKED_STRUCT fils_request_parameters_element_t;

#define FILS_PARAM_MAX_CHANNEL_TIME		(1 << 2)

/* 11ai 9.4.2.184 FILS HLP Container element */
typedef BWL_PRE_PACKED_STRUCT struct fils_hlp_container_element {
	uint8		elementid;
	uint8		length;
	uint8		element_id_ext;
	uint8		dest_addr[ETHER_ADDR_LEN];
	uint8		src_addr[ETHER_ADDR_LEN];
	/* variable len hlp packet */
	uint8		hlp[];
} BWL_POST_PACKED_STRUCT fils_hlp_container_element_t;

/* 11ai 9.4.2.184 FILS Wrapped Data element */
typedef BWL_PRE_PACKED_STRUCT struct fils_wrapped_data_element {
	uint8		elementid;
	uint8		length;
	uint8		element_id_ext;
	/* variable len wrapped data packet */
	uint8		wrapped_data[];
} BWL_POST_PACKED_STRUCT fils_wrapped_data_element_t;

#define FILS_HLP_CONTAINER_ELEM_LEN	(sizeof(fils_hlp_container_element_t))

/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

#endif /* __FILSAUTH_H__ */
