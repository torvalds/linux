/*
 * Fundamental types and constants relating to WFA MBO
 * (Multiband Operation)
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

#ifndef _MBO_H_
#define _MBO_H_

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>

/* WiFi MBO OUI values */
#define MBO_OUI         WFA_OUI      /* WiFi OUI 50:6F:9A */
/* oui_type field identifying the type and version of the MBO IE. */
#define MBO_OUI_TYPE    WFA_OUI_TYPE_MBO /* OUI Type/Version */
/* IEEE 802.11 vendor specific information element. */
#define MBO_IE_ID          0xdd

/* MBO ATTR related macros */
#define MBO_ATTR_ID_OFF         0
#define MBO_ATTR_LEN_OFF        1
#define MBO_ATTR_DATA_OFF       2

#define MBO_ATTR_ID_LEN          1      /* Attr ID field length */
#define MBO_ATTR_LEN_LEN         1      /* Attr Length field length */
#define MBO_ATTR_HDR_LEN         2      /* ID + 1-byte length field */

/* MBO subelemts related */
#define MBO_SUBELEM_ID          0xdd
#define MBO_SUBELEM_OUI         WFA_OUI

#define MBO_SUBELEM_ID_LEN          1      /* SubElement ID field length */
#define MBO_SUBELEM_LEN_LEN         1      /* SubElement length field length */
#define MBO_SUBELEM_HDR_LEN         6      /* ID + length + OUI + OUY TYPE */

#define MBO_NON_PREF_CHAN_SUBELEM_LEN_LEN(L)  (7 + (L))  /* value of length field */
#define MBO_NON_PREF_CHAN_SUBELEM_TOT_LEN(L) \
	(MBO_SUBELEM_ID_LEN + MBO_SUBELEM_LEN_LEN + MBO_NON_PREF_CHAN_SUBELEM_LEN_LEN(L))
/* MBO attributes as defined in the mbo spec */
enum {
	MBO_ATTR_MBO_AP_CAPABILITY = 1,
	MBO_ATTR_NON_PREF_CHAN_REPORT = 2,
	MBO_ATTR_CELL_DATA_CAP = 3,
	MBO_ATTR_ASSOC_DISALLOWED = 4,
	MBO_ATTR_CELL_DATA_CONN_PREF = 5,
	MBO_ATTR_TRANS_REASON_CODE = 6,
	MBO_ATTR_TRANS_REJ_REASON_CODE = 7,
	MBO_ATTR_ASSOC_RETRY_DELAY = 8
};

typedef BWL_PRE_PACKED_STRUCT struct wifi_mbo_ie_s {
	uint8 id;      /* IE ID: MBO_IE_ID 0xDD */
	uint8 len;     /* IE length */
	uint8 oui[WFA_OUI_LEN]; /* MBO_OUI 50:6F:9A */
	uint8 oui_type;   /* MBO_OUI_TYPE 0x16 */
	uint8 attr[1]; /* var len attributes */
} BWL_POST_PACKED_STRUCT wifi_mbo_ie_t;

#define MBO_IE_HDR_SIZE (OFFSETOF(wifi_mbo_ie_t, attr))
/* oui:3 bytes + oui type:1 byte */
#define MBO_IE_NO_ATTR_LEN  4

/* MBO AP Capability Attribute */
typedef BWL_PRE_PACKED_STRUCT struct wifi_mbo_ap_cap_ind_attr_s {
	/* Attribute ID - 0x01. */
	uint8 id;
	/* Length of the following fields in the attribute */
	uint8 len;
	/* AP capability bitmap */
	uint8 cap_ind;
} BWL_POST_PACKED_STRUCT wifi_mbo_ap_cap_ind_attr_t;

/* MBO AP Capability Indication Field Values */
#define MBO_AP_CAP_IND_CELLULAR_AWARE  0x40

/* Non-preferred Channel Report Attribute */
#define MBO_NON_PREF_CHAN_ATTR_OPCALSS_OFF       2
#define MBO_NON_PREF_CHAN_ATTR_CHANLIST_OFF      3
#define MBO_NON_PREF_CHAN_ATTR_PREF_OFF(L)      \
	(MBO_NON_PREF_CHAN_ATTR_CHANLIST_OFF + (L))

#define MBO_NON_PREF_CHAN_ATTR_OPCALSS_LEN       1
#define MBO_NON_PREF_CHAN_ATTR_PREF_LEN          1
#define MBO_NON_PREF_CHAN_ATTR_REASON_LEN        1

#define MBO_NON_PREF_CHAN_ATTR_LEN(L)  ((L) + 3)
#define MBO_NON_PREF_CHAN_ATTR_TOT_LEN(L)  (MBO_ATTR_HDR_LEN + (L) + 3)

/* attribute len - (opclass + Pref + Reason) */
#define MBO_NON_PREF_CHAN_ATTR_CHANLIST_LEN(L) ((L) - 3)

/* MBO Non-preferred Channel Report: "Preference" field value */
enum {
	MBO_STA_NON_OPERABLE_BAND_CHAN = 0,
	MBO_STA_NON_PREFERRED_BAND_CHAN = 1,
	MBO_STA_PREFERRED_BAND_CHAN = 255
};

/* MBO Non-preferred Channel Report: "Reason Code" field value */
enum {
	MBO_NON_PREF_CHAN_RC_UNSPECIFIED = 0,
	MBO_NON_PREF_CHAN_RC_BCN_STRENGTH = 1,
	MBO_NON_PREF_CHAN_RC_CO_LOC_INTERFERENCE = 2,
	MBO_NON_PREF_CHAN_RC_IN_DEV_INTERFERENCE = 3
};

/* Cellular Data Capability Attribute */
typedef BWL_PRE_PACKED_STRUCT struct wifi_mbo_cell_data_cap_attr_s {
	/* Attribute ID - 0x03. */
	uint8 id;
	/* Length of the following fields in the attribute */
	uint8 len;
	/* MBO STA's cellular capability */
	uint8 cell_conn;
} BWL_POST_PACKED_STRUCT wifi_mbo_cell_data_cap_attr_t;

/* MBO Cellular Data Capability:  "Cellular Connectivity" field value */
enum {
	MBO_CELL_DATA_CONN_AVAILABLE = 1,
	MBO_CELL_DATA_CONN_NOT_AVAILABLE = 2,
	MBO_CELL_DATA_CONN_NOT_CAPABLE = 3
};

/* Association Disallowed attribute */
typedef BWL_PRE_PACKED_STRUCT struct wifi_mbo_assoc_disallowed_attr_s {
	/* Attribute ID - 0x04. */
	uint8 id;
	/* Length of the following fields in the attribute */
	uint8 len;
	/* Reason of not accepting new association */
	uint8 reason_code;
} BWL_POST_PACKED_STRUCT wifi_mbo_assoc_disallowed_attr_t;

/* Association Disallowed attr Reason code field values */
enum {
	MBO_ASSOC_DISALLOWED_RC_UNSPECIFIED = 1,
	MBO_ASSOC_DISALLOWED_RC_MAX_STA_REACHED = 2,
	MBO_ASSOC_DISALLOWED_RC_AIR_IFACE_OVERLOADED = 3,
	MBO_ASSOC_DISALLOWED_RC_AUTH_SRVR_OVERLOADED = 4,
	MBO_ASSOC_DISALLOWED_RC_INSUFFIC_RSSI = 5,
	MBO_ASSOC_DISALLOWED_RC_INVALID = 0xffff
};

/* Cellular Data Conn Pref attribute */
typedef BWL_PRE_PACKED_STRUCT struct wifi_mbo_cell_data_conn_pref_attr_s {
	/* Attribute ID - 0x05. */
	uint8 id;
	/* Length of the following fields in the attribute */
	uint8 len;
	/* Preference value of cellular connection */
	uint8 cell_pref;
} BWL_POST_PACKED_STRUCT wifi_mbo_cell_data_conn_pref_attr_t;

/* Cellular Data Conn Pref attr: Cellular Pref field values. Per MBO Spec. v1.1 */
enum {
	MBO_CELLULAR_DATA_CONN_EXCLUDED = 0,
	MBO_CELLULAR_DATA_CONN_NOT_PREFERRED = 1,
	MBO_CELLULAR_DATA_CONN_PREFERRED = 255
};

/* Transition Reason Code Attribute */
typedef BWL_PRE_PACKED_STRUCT struct wifi_mbo_trans_reason_code_attr_s {
	/* Attribute ID - 0x06. */
	uint8 id;
	/* Length of the following fields in the attribute */
	uint8 len;
	/* Reason of transition recommendation */
	uint8 trans_reason_code;
} BWL_POST_PACKED_STRUCT wifi_mbo_trans_reason_code_attr_t;

/* Transition Reason Code Attr: trans reason code field values */
enum {
	MBO_TRANS_REASON_UNSPECIFIED = 0,
	MBO_TRANS_REASON_EXCESSV_FRM_LOSS_RATE = 1,
	MBO_TRANS_REASON_EXCESSV_TRAFFIC_DELAY = 2,
	MBO_TRANS_REASON_INSUFF_BW = 3,
	MBO_TRANS_REASON_LOAD_BALANCING = 4,
	MBO_TRANS_REASON_LOW_RSSI = 5,
	MBO_TRANS_REASON_EXCESSV_RETRANS_RCVD = 6,
	MBO_TRANS_REASON_HIGH_INTERFERENCE = 7,
	MBO_TRANS_REASON_GRAY_ZONE = 8,
	MBO_TRANS_REASON_PREMIUM_AP_TRANS = 9
};

/* Transition Rejection Reason Code Attribute */
typedef BWL_PRE_PACKED_STRUCT struct wifi_mbo_trans_rej_reason_code_attr_s {
	/* Attribute ID - 0x07. */
	uint8 id;
	/* Length of the following fields in the attribute */
	uint8 len;
	/* Reason of transition rejection */
	uint8 trans_rej_reason_code;
} BWL_POST_PACKED_STRUCT wifi_mbo_trans_rej_reason_code_attr_t;

/* Transition Rej Reason Code Attr: trans rej reason code field values */
enum {
	MBO_TRANS_REJ_REASON_UNSPECIFIED = 0,
	MBO_TRANS_REJ_REASON_EXSSIV_FRM_LOSS_RATE = 1,
	MBO_TRANS_REJ_REASON_EXSSIV_TRAFFIC_DELAY = 2,
	MBO_TRANS_REJ_REASON_INSUFF_QOS_CAPACITY = 3,
	MBO_TRANS_REJ_REASON_LOW_RSSI = 4,
	MBO_TRANS_REJ_REASON_HIGH_INTERFERENCE = 5,
	MBO_TRANS_REJ_REASON_SERVICE_UNAVAIL = 6
};

/* Assoc Retry Delay Attribute */
typedef BWL_PRE_PACKED_STRUCT struct wifi_mbo_assoc_retry_delay_attr_s {
	/* Attribute ID - 0x08. */
	uint8 id;
	/* Length of the following fields in the attribute */
	uint8 len;
	/* No of Seconds before next assoc attempt */
	uint16 reassoc_delay;
} BWL_POST_PACKED_STRUCT wifi_mbo_assoc_retry_delay_attr_t;

#define MBO_ANQP_OUI_TYPE    0x12     /* OUTI Type/Version */

/* MBO ANQP Element */
typedef BWL_PRE_PACKED_STRUCT struct wifi_mbo_anqp_elem_s {
	/* ID - 56797 */
	uint16 info_id;
	/* Length of the OUI + Vendor Specific content */
	uint16 len;
	/* WFA_OUI 50:6F:9A */
	uint8 oui[WFA_OUI_LEN];
	/* MBO_ANQP_OUI_TYPE 0x12 */
	uint8 oui_type;
	/* MBO ANQP element type */
	uint8 sub_type;
	/* variable len payload */
	uint8 payload[1];
} BWL_POST_PACKED_STRUCT wifi_mbo_anqp_elem_t;

#define MBO_ANQP_ELEM_HDR_SIZE (OFFSETOF(wifi_mbo_anqp_elem_t, payload))

/* oui:3 bytes + oui type:1 byte + sub type:1 byte */
#define MBO_ANQP_ELEM_NO_PAYLOAD_LEN  5

#define MBO_ANQP_VS_ELEM_SIZE (sizeof(wifi_mbo_anqp_elem_t))
#define MBO_ANQP_VS_ELEM_LENGTH (MBO_ANQP_VS_ELEM_SIZE - 4)

/* MBO ANQP Subtype Values */
enum {
	MBO_ANQP_ELEM_MBO_QUERY_LIST = 1,
	MBO_ANQP_ELEM_CELL_DATA_CONN_PREF = 2
};

/* MBO sub-elements */
typedef BWL_PRE_PACKED_STRUCT struct wifi_mbo_cell_cap_subelem_s {
	/* 0xDD */
	uint8 sub_elem_id;
	/* Length of the following fields in sub-element */
	uint8 len;
	/* WFA_OUI 50:6F:9A */
	uint8 oui[WFA_OUI_LEN];
	/* OUI_TYPE 0x03 */
	uint8 oui_type;
	/* STA cellular capability */
	uint8 cell_conn;
} BWL_POST_PACKED_STRUCT wifi_mbo_cell_cap_subelem_t;

typedef BWL_PRE_PACKED_STRUCT struct wifi_mbo_perf_chan_subelem_s {
	/* 0xDD */
	uint8 sub_elem_id;
	/* Length of the following fields in sub-element */
	uint8 len;
	/* WFA_OUI 50:6F:9A */
	uint8 oui[WFA_OUI_LEN];
	/* OUI_TYPE 0x02 */
	uint8 oui_type;
	/* variable length of channel preference data */
	uint8 data[1];
} BWL_POST_PACKED_STRUCT wifi_mbo_pref_chan_subelem_t;

/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

#endif /* __MBO_H__ */
