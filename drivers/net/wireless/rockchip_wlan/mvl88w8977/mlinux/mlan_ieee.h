/** @file mlan_ieee.h
 *
 *  @brief This file contains IEEE information element related
 *  definitions used in MLAN and MOAL module.
 *
 *  Copyright (C) 2008-2017, Marvell International Ltd.
 *
 *  This software file (the "File") is distributed by Marvell International
 *  Ltd. under the terms of the GNU General Public License Version 2, June 1991
 *  (the "License").  You may use, redistribute and/or modify this File in
 *  accordance with the terms and conditions of the License, a copy of which
 *  is available by writing to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 *  worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 *  THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 *  ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 *  this warranty disclaimer.
 */

/******************************************************
Change log:
    11/03/2008: initial version
******************************************************/

#ifndef _MLAN_IEEE_H_
#define _MLAN_IEEE_H_

/** FIX IES size in beacon buffer */
#define WLAN_802_11_FIXED_IE_SIZE           12
/** WLAN supported rates */
#define WLAN_SUPPORTED_RATES                14

/** WLAN supported rates extension*/
#define WLAN_SUPPORTED_RATES_EXT            32

/** Enumeration definition*/
/** WLAN_802_11_NETWORK_TYPE */
typedef enum _WLAN_802_11_NETWORK_TYPE {
	Wlan802_11FH,
	Wlan802_11DS,
	/* Defined as upper bound */
	Wlan802_11NetworkTypeMax
} WLAN_802_11_NETWORK_TYPE;

#ifdef BIG_ENDIAN_SUPPORT
/** Frame control: Type Mgmt frame */
#define IEEE80211_FC_MGMT_FRAME_TYPE_MASK    0x3000
/** Frame control: SubType Mgmt frame */
#define IEEE80211_GET_FC_MGMT_FRAME_SUBTYPE(fc) (((fc) & 0xF000) >> 12)
#else
/** Frame control: Type Mgmt frame */
#define IEEE80211_FC_MGMT_FRAME_TYPE_MASK    0x000C
/** Frame control: SubType Mgmt frame */
#define IEEE80211_GET_FC_MGMT_FRAME_SUBTYPE(fc) (((fc) & 0x00F0) >> 4)
#endif

#ifdef PRAGMA_PACK
#pragma pack(push, 1)
#endif

/* Reason codes */
#define  IEEE_80211_REASONCODE_UNSPECIFIED       1

/** IEEE Type definitions  */
typedef MLAN_PACK_START enum _IEEEtypes_ElementId_e {
	SSID = 0,
	SUPPORTED_RATES = 1,

	FH_PARAM_SET = 2,
	DS_PARAM_SET = 3,
	CF_PARAM_SET = 4,

	IBSS_PARAM_SET = 6,

	COUNTRY_INFO = 7,

	POWER_CONSTRAINT = 32,
	POWER_CAPABILITY = 33,
	TPC_REQUEST = 34,
	TPC_REPORT = 35,
	CHANNEL_SWITCH_ANN = 37,
	EXTEND_CHANNEL_SWITCH_ANN = 60,
	QUIET = 40,
	IBSS_DFS = 41,
	SUPPORTED_CHANNELS = 36,
	REGULATORY_CLASS = 59,
	HT_CAPABILITY = 45,
	QOS_INFO = 46,
	HT_OPERATION = 61,
	BSSCO_2040 = 72,
	OVERLAPBSSSCANPARAM = 74,
	EXT_CAPABILITY = 127,
	LINK_ID = 101,
	/*IEEE802.11r */
	MOBILITY_DOMAIN = 54,
	FAST_BSS_TRANSITION = 55,
	TIMEOUT_INTERVAL = 56,
	RIC = 57,
	QOS_MAPPING = 110,

	ERP_INFO = 42,

	EXTENDED_SUPPORTED_RATES = 50,

	VENDOR_SPECIFIC_221 = 221,
	WMM_IE = VENDOR_SPECIFIC_221,

	WPS_IE = VENDOR_SPECIFIC_221,

	WPA_IE = VENDOR_SPECIFIC_221,
	RSN_IE = 48,
	VS_IE = VENDOR_SPECIFIC_221,
	WAPI_IE = 68,
} MLAN_PACK_END IEEEtypes_ElementId_e;

/** IEEE IE header */
typedef MLAN_PACK_START struct _IEEEtypes_Header_t {
    /** Element ID */
	t_u8 element_id;
    /** Length */
	t_u8 len;
} MLAN_PACK_END IEEEtypes_Header_t, *pIEEEtypes_Header_t;

/** Vendor specific IE header */
typedef MLAN_PACK_START struct _IEEEtypes_VendorHeader_t {
    /** Element ID */
	t_u8 element_id;
    /** Length */
	t_u8 len;
    /** OUI */
	t_u8 oui[3];
    /** OUI type */
	t_u8 oui_type;
    /** OUI subtype */
	t_u8 oui_subtype;
    /** Version */
	t_u8 version;
} MLAN_PACK_END IEEEtypes_VendorHeader_t, *pIEEEtypes_VendorHeader_t;

/** Vendor specific IE */
typedef MLAN_PACK_START struct _IEEEtypes_VendorSpecific_t {
    /** Vendor specific IE header */
	IEEEtypes_VendorHeader_t vend_hdr;
    /** IE Max - size of previous fields */
	t_u8 data[IEEE_MAX_IE_SIZE - sizeof(IEEEtypes_VendorHeader_t)];
} MLAN_PACK_END IEEEtypes_VendorSpecific_t, *pIEEEtypes_VendorSpecific_t;

/** IEEE IE */
typedef MLAN_PACK_START struct _IEEEtypes_Generic_t {
    /** Generic IE header */
	IEEEtypes_Header_t ieee_hdr;
    /** IE Max - size of previous fields */
	t_u8 data[IEEE_MAX_IE_SIZE - sizeof(IEEEtypes_Header_t)];
} MLAN_PACK_END IEEEtypes_Generic_t, *pIEEEtypes_Generic_t;

/**ft capability policy*/
typedef MLAN_PACK_START struct _IEEEtypes_FtCapPolicy_t {
#ifdef BIG_ENDIAN_SUPPORT
    /** Reserved */
	t_u8 reserved:6;
    /** RIC support */
	t_u8 ric:1;
    /** FT over the DS capable */
	t_u8 ft_over_ds:1;
#else
    /** FT over the DS capable */
	t_u8 ft_over_ds:1;
    /** RIC support */
	t_u8 ric:1;
    /** Reserved */
	t_u8 reserved:6;
#endif
} MLAN_PACK_END IEEEtypes_FtCapPolicy_t;

/** Mobility domain IE */
typedef MLAN_PACK_START struct _IEEEtypes_MobilityDomain_t {
    /** Generic IE header */
	IEEEtypes_Header_t ieee_hdr;
    /** Mobility Domain ID */
	t_u16 mdid;
    /** FT Capability policy */
	t_u8 ft_cap;
} MLAN_PACK_END IEEEtypes_MobilityDomain_t;

/**FT MIC Control*/
typedef MLAN_PACK_START struct _IEEEtypes_FT_MICControl_t {
	/** reserved */
	t_u8 reserved;
	/** element count */
	t_u8 element_count;
} MLAN_PACK_END IEEEtypes_FT_MICControl_t;

/** FTIE MIC LEN */
#define FTIE_MIC_LEN  16

/**FT IE*/
typedef MLAN_PACK_START struct _IEEEtypes_FastBssTransElement_t {
    /** Generic IE header */
	IEEEtypes_Header_t ieee_hdr;
	/** mic control */
	IEEEtypes_FT_MICControl_t mic_control;
	/** mic */
	t_u8 mic[FTIE_MIC_LEN];
	/** ANonce */
	t_u8 a_nonce[32];
	/** SNonce */
	t_u8 s_nonce[32];
	/** sub element */
	t_u8 sub_element[1];
} MLAN_PACK_END IEEEtypes_FastBssTransElement_t;

/** auth frame body*/
typedef MLAN_PACK_START struct {
    /** auth alg */
	t_u16 auth_alg;
    /** auth transaction */
	t_u16 auth_transaction;
    /** status code */
	t_u16 status_code;
    /** variable */
	t_u8 variable[0];
} MLAN_PACK_END IEEEtypes_Auth_framebody;

/*Category for FT*/
#define FT_CATEGORY 6
/** FT ACTION request */
#define FT_ACTION_REQUEST 1
/** FT ACTION response */
#define FT_ACTION_RESPONSE 2

/*FT response and FT ack*/
typedef MLAN_PACK_START struct {
    /** category */
	t_u8 category;
    /** action */
	t_u8 action;
    /** sta address */
	t_u8 sta_addr[MLAN_MAC_ADDR_LENGTH];
    /** target ap address */
	t_u8 target_ap_addr[MLAN_MAC_ADDR_LENGTH];
    /** status code */
	t_u16 status_code;
    /** varible */
	t_u8 variable[0];
} MLAN_PACK_END IEEEtypes_Ft_action_response;

/**FT request */
typedef MLAN_PACK_START struct {
    /** category */
	t_u8 category;
    /** action */
	t_u8 action;
    /** sta address */
	t_u8 sta_addr[MLAN_MAC_ADDR_LENGTH];
    /** target ap address */
	t_u8 target_ap_addr[MLAN_MAC_ADDR_LENGTH];
    /** varible */
	t_u8 variable[0];
} MLAN_PACK_END IEEEtypes_Ft_action_request;

/*Mgmt frame*/
typedef MLAN_PACK_START struct {
    /** frame control */
	t_u16 frame_control;
    /** duration */
	t_u16 duration;
    /** dest address */
	t_u8 da[MLAN_MAC_ADDR_LENGTH];
    /** source address */
	t_u8 sa[MLAN_MAC_ADDR_LENGTH];
    /** bssid */
	t_u8 bssid[MLAN_MAC_ADDR_LENGTH];
    /** seq control */
	t_u16 seq_ctrl;
    /** address 4 */
	t_u8 addr4[MLAN_MAC_ADDR_LENGTH];
	union {
		IEEEtypes_Auth_framebody auth;
		IEEEtypes_Ft_action_response ft_resp;
		IEEEtypes_Ft_action_request ft_req;
	} u;
} MLAN_PACK_END IEEE80211_MGMT;

/** TLV header */
typedef MLAN_PACK_START struct _TLV_Generic_t {
    /** Type */
	t_u16 type;
    /** Length */
	t_u16 len;
} MLAN_PACK_END TLV_Generic_t, *pTLV_Generic_t;

/** Capability information mask */
#define CAPINFO_MASK \
(~(MBIT(15) | MBIT(14) | MBIT(11) | MBIT(9)))

/** Capability Bit Map*/
#ifdef BIG_ENDIAN_SUPPORT
typedef MLAN_PACK_START struct _IEEEtypes_CapInfo_t {
	t_u8 rsrvd1:2;
	t_u8 dsss_ofdm:1;
	t_u8 radio_measurement:1;
	t_u8 rsvrd2:1;
	t_u8 short_slot_time:1;
	t_u8 rsrvd3:1;
	t_u8 spectrum_mgmt:1;
	t_u8 chan_agility:1;
	t_u8 pbcc:1;
	t_u8 short_preamble:1;
	t_u8 privacy:1;
	t_u8 cf_poll_rqst:1;
	t_u8 cf_pollable:1;
	t_u8 ibss:1;
	t_u8 ess:1;
} MLAN_PACK_END IEEEtypes_CapInfo_t, *pIEEEtypes_CapInfo_t;
#else
typedef MLAN_PACK_START struct _IEEEtypes_CapInfo_t {
    /** Capability Bit Map : ESS */
	t_u8 ess:1;
    /** Capability Bit Map : IBSS */
	t_u8 ibss:1;
    /** Capability Bit Map : CF pollable */
	t_u8 cf_pollable:1;
    /** Capability Bit Map : CF poll request */
	t_u8 cf_poll_rqst:1;
    /** Capability Bit Map : privacy */
	t_u8 privacy:1;
    /** Capability Bit Map : Short preamble */
	t_u8 short_preamble:1;
    /** Capability Bit Map : PBCC */
	t_u8 pbcc:1;
    /** Capability Bit Map : Channel agility */
	t_u8 chan_agility:1;
    /** Capability Bit Map : Spectrum management */
	t_u8 spectrum_mgmt:1;
    /** Capability Bit Map : Reserved */
	t_u8 rsrvd3:1;
    /** Capability Bit Map : Short slot time */
	t_u8 short_slot_time:1;
    /** Capability Bit Map : APSD */
	t_u8 Apsd:1;
    /** Capability Bit Map : Reserved */
	t_u8 rsvrd2:1;
    /** Capability Bit Map : DSS OFDM */
	t_u8 dsss_ofdm:1;
    /** Capability Bit Map : Reserved */
	t_u8 rsrvd1:2;
} MLAN_PACK_END IEEEtypes_CapInfo_t, *pIEEEtypes_CapInfo_t;
#endif /* BIG_ENDIAN_SUPPORT */

/** IEEEtypes_CfParamSet_t */
typedef MLAN_PACK_START struct _IEEEtypes_CfParamSet_t {
    /** CF peremeter : Element ID */
	t_u8 element_id;
    /** CF peremeter : Length */
	t_u8 len;
    /** CF peremeter : Count */
	t_u8 cfp_cnt;
    /** CF peremeter : Period */
	t_u8 cfp_period;
    /** CF peremeter : Maximum duration */
	t_u16 cfp_max_duration;
    /** CF peremeter : Remaining duration */
	t_u16 cfp_duration_remaining;
} MLAN_PACK_END IEEEtypes_CfParamSet_t, *pIEEEtypes_CfParamSet_t;

/** IEEEtypes_IbssParamSet_t */
typedef MLAN_PACK_START struct _IEEEtypes_IbssParamSet_t {
    /** Element ID */
	t_u8 element_id;
    /** Length */
	t_u8 len;
    /** ATIM window value in milliseconds */
	t_u16 atim_window;
} MLAN_PACK_END IEEEtypes_IbssParamSet_t, *pIEEEtypes_IbssParamSet_t;

/** IEEEtypes_SsParamSet_t */
typedef MLAN_PACK_START union _IEEEtypes_SsParamSet_t {
    /** SS parameter : CF parameter set */
	IEEEtypes_CfParamSet_t cf_param_set;
    /** SS parameter : IBSS parameter set */
	IEEEtypes_IbssParamSet_t ibss_param_set;
} MLAN_PACK_END IEEEtypes_SsParamSet_t, *pIEEEtypes_SsParamSet_t;

/** IEEEtypes_FhParamSet_t */
typedef MLAN_PACK_START struct _IEEEtypes_FhParamSet_t {
    /** FH parameter : Element ID */
	t_u8 element_id;
    /** FH parameter : Length */
	t_u8 len;
    /** FH parameter : Dwell time in milliseconds */
	t_u16 dwell_time;
    /** FH parameter : Hop set */
	t_u8 hop_set;
    /** FH parameter : Hop pattern */
	t_u8 hop_pattern;
    /** FH parameter : Hop index */
	t_u8 hop_index;
} MLAN_PACK_END IEEEtypes_FhParamSet_t, *pIEEEtypes_FhParamSet_t;

/** IEEEtypes_DsParamSet_t */
typedef MLAN_PACK_START struct _IEEEtypes_DsParamSet_t {
    /** DS parameter : Element ID */
	t_u8 element_id;
    /** DS parameter : Length */
	t_u8 len;
    /** DS parameter : Current channel */
	t_u8 current_chan;
} MLAN_PACK_END IEEEtypes_DsParamSet_t, *pIEEEtypes_DsParamSet_t;

/** IEEEtypes_PhyParamSet_t */
typedef MLAN_PACK_START union _IEEEtypes_PhyParamSet_t {
    /** FH parameter set */
	IEEEtypes_FhParamSet_t fh_param_set;
    /** DS parameter set */
	IEEEtypes_DsParamSet_t ds_param_set;
} MLAN_PACK_END IEEEtypes_PhyParamSet_t, *pIEEEtypes_PhyParamSet_t;

/** IEEEtypes_ERPInfo_t */
typedef MLAN_PACK_START struct _IEEEtypes_ERPInfo_t {
    /** Element ID */
	t_u8 element_id;
    /** Length */
	t_u8 len;
    /** ERP flags */
	t_u8 erp_flags;
} MLAN_PACK_END IEEEtypes_ERPInfo_t, *pIEEEtypes_ERPInfo_t;

/** IEEEtypes_AId_t */
typedef t_u16 IEEEtypes_AId_t;

/** IEEEtypes_StatusCode_t */
typedef t_u16 IEEEtypes_StatusCode_t;

/** Fixed size in assoc_resp */
#define ASSOC_RESP_FIXED_SIZE      6
/** IEEEtypes_AssocRsp_t */
typedef MLAN_PACK_START struct _IEEEtypes_AssocRsp_t {
    /** Capability information */
	IEEEtypes_CapInfo_t capability;
    /** Association response status code */
	IEEEtypes_StatusCode_t status_code;
    /** Association ID */
	IEEEtypes_AId_t a_id;
    /** IE data buffer */
	t_u8 ie_buffer[1];
} MLAN_PACK_END IEEEtypes_AssocRsp_t, *pIEEEtypes_AssocRsp_t;

/** 802.11 supported rates */
typedef t_u8 WLAN_802_11_RATES[WLAN_SUPPORTED_RATES];

/** cipher TKIP */
#define WPA_CIPHER_TKIP		2
/** cipher AES */
#define WPA_CIPHER_AES_CCM	4
/** AKM: 8021x */
#define RSN_AKM_8021X		1
/** AKM: PSK */
#define RSN_AKM_PSK         2
/** AKM: PSK SHA256 */
#define RSN_AKM_PSK_SHA256	6
#if defined(STA_SUPPORT)
/** Pairwise Cipher Suite length */
#define PAIRWISE_CIPHER_SUITE_LEN    4
/** AKM Suite length */
#define AKM_SUITE_LEN    4
/** MFPC bit in RSN capability */
#define MFPC_BIT    7
/** MFPR bit in RSN capability */
#define MFPR_BIT    6
/** PMF ORing mask */
#define PMF_MASK    0x00c0
#endif

/** wpa_suite_t */
typedef MLAN_PACK_START struct _wpa_suite_t {
    /** OUI */
	t_u8 oui[3];
    /** tyep */
	t_u8 type;
} MLAN_PACK_END wpa_suite, wpa_suite_mcast_t;

/** wpa_suite_ucast_t */
typedef MLAN_PACK_START struct {
	/* count */
	t_u16 count;
    /** wpa_suite list */
	wpa_suite list[1];
} MLAN_PACK_END wpa_suite_ucast_t, wpa_suite_auth_key_mgmt_t;

/** IEEEtypes_Rsn_t */
typedef MLAN_PACK_START struct _IEEEtypes_Rsn_t {
    /** Rsn : Element ID */
	t_u8 element_id;
    /** Rsn : Length */
	t_u8 len;
    /** Rsn : version */
	t_u16 version;
    /** Rsn : group cipher */
	wpa_suite_mcast_t group_cipher;
    /** Rsn : pairwise cipher */
	wpa_suite_ucast_t pairwise_cipher;
} MLAN_PACK_END IEEEtypes_Rsn_t, *pIEEEtypes_Rsn_t;

/** IEEEtypes_Wpa_t */
typedef MLAN_PACK_START struct _IEEEtypes_Wpa_t {
    /** Wpa : Element ID */
	t_u8 element_id;
    /** Wpa : Length */
	t_u8 len;
    /** Wpa : oui */
	t_u8 oui[4];
    /** version */
	t_u16 version;
    /** Wpa : group cipher */
	wpa_suite_mcast_t group_cipher;
    /** Wpa : pairwise cipher */
	wpa_suite_ucast_t pairwise_cipher;
} MLAN_PACK_END IEEEtypes_Wpa_t, *pIEEEtypes_Wpa_t;

/** Data structure of WMM QoS information */
typedef MLAN_PACK_START struct _IEEEtypes_WmmQosInfo_t {
#ifdef BIG_ENDIAN_SUPPORT
    /** QoS UAPSD */
	t_u8 qos_uapsd:1;
    /** Reserved */
	t_u8 reserved:3;
    /** Parameter set count */
	t_u8 para_set_count:4;
#else
    /** Parameter set count */
	t_u8 para_set_count:4;
    /** Reserved */
	t_u8 reserved:3;
    /** QoS UAPSD */
	t_u8 qos_uapsd:1;
#endif				/* BIG_ENDIAN_SUPPORT */
} MLAN_PACK_END IEEEtypes_WmmQosInfo_t, *pIEEEtypes_WmmQosInfo_t;

/** Data structure of WMM Aci/Aifsn */
typedef MLAN_PACK_START struct _IEEEtypes_WmmAciAifsn_t {
#ifdef BIG_ENDIAN_SUPPORT
    /** Reserved */
	t_u8 reserved:1;
    /** Aci */
	t_u8 aci:2;
    /** Acm */
	t_u8 acm:1;
    /** Aifsn */
	t_u8 aifsn:4;
#else
    /** Aifsn */
	t_u8 aifsn:4;
    /** Acm */
	t_u8 acm:1;
    /** Aci */
	t_u8 aci:2;
    /** Reserved */
	t_u8 reserved:1;
#endif				/* BIG_ENDIAN_SUPPORT */
} MLAN_PACK_END IEEEtypes_WmmAciAifsn_t, *pIEEEtypes_WmmAciAifsn_t;

/** Data structure of WMM ECW */
typedef MLAN_PACK_START struct _IEEEtypes_WmmEcw_t {
#ifdef BIG_ENDIAN_SUPPORT
    /** Maximum Ecw */
	t_u8 ecw_max:4;
    /** Minimum Ecw */
	t_u8 ecw_min:4;
#else
    /** Minimum Ecw */
	t_u8 ecw_min:4;
    /** Maximum Ecw */
	t_u8 ecw_max:4;
#endif				/* BIG_ENDIAN_SUPPORT */
} MLAN_PACK_END IEEEtypes_WmmEcw_t, *pIEEEtypes_WmmEcw_t;

/** Data structure of WMM AC parameters  */
typedef MLAN_PACK_START struct _IEEEtypes_WmmAcParameters_t {
	IEEEtypes_WmmAciAifsn_t aci_aifsn;   /**< AciAifSn */
	IEEEtypes_WmmEcw_t ecw;		    /**< Ecw */
	t_u16 tx_op_limit;		      /**< Tx op limit */
} MLAN_PACK_END IEEEtypes_WmmAcParameters_t, *pIEEEtypes_WmmAcParameters_t;

/** Data structure of WMM Info IE  */
typedef MLAN_PACK_START struct _IEEEtypes_WmmInfo_t {

    /**
     * WMM Info IE - Vendor Specific Header:
     *   element_id  [221/0xdd]
     *   Len         [7]
     *   Oui         [00:50:f2]
     *   OuiType     [2]
     *   OuiSubType  [0]
     *   Version     [1]
     */
	IEEEtypes_VendorHeader_t vend_hdr;

    /** QoS information */
	IEEEtypes_WmmQosInfo_t qos_info;

} MLAN_PACK_END IEEEtypes_WmmInfo_t, *pIEEEtypes_WmmInfo_t;

/** Data structure of WMM parameter IE  */
typedef MLAN_PACK_START struct _IEEEtypes_WmmParameter_t {
    /**
     * WMM Parameter IE - Vendor Specific Header:
     *   element_id  [221/0xdd]
     *   Len         [24]
     *   Oui         [00:50:f2]
     *   OuiType     [2]
     *   OuiSubType  [1]
     *   Version     [1]
     */
	IEEEtypes_VendorHeader_t vend_hdr;

    /** QoS information */
	IEEEtypes_WmmQosInfo_t qos_info;
    /** Reserved */
	t_u8 reserved;

    /** AC Parameters Record WMM_AC_BE, WMM_AC_BK, WMM_AC_VI, WMM_AC_VO */
	IEEEtypes_WmmAcParameters_t ac_params[MAX_AC_QUEUES];
} MLAN_PACK_END IEEEtypes_WmmParameter_t, *pIEEEtypes_WmmParameter_t;

/** Enumerator for TSPEC direction */
typedef MLAN_PACK_START enum _IEEEtypes_WMM_TSPEC_TS_Info_Direction_e {

	TSPEC_DIR_UPLINK = 0,
	TSPEC_DIR_DOWNLINK = 1,
	/* 2 is a reserved value */
	TSPEC_DIR_BIDIRECT = 3,

} MLAN_PACK_END IEEEtypes_WMM_TSPEC_TS_Info_Direction_e;

/** Enumerator for TSPEC PSB */
typedef MLAN_PACK_START enum _IEEEtypes_WMM_TSPEC_TS_Info_PSB_e {

	TSPEC_PSB_LEGACY = 0,
	TSPEC_PSB_TRIG = 1,

} MLAN_PACK_END IEEEtypes_WMM_TSPEC_TS_Info_PSB_e;

/** Enumerator for TSPEC Ack Policy */
typedef MLAN_PACK_START enum _IEEEtypes_WMM_TSPEC_TS_Info_AckPolicy_e {

	TSPEC_ACKPOLICY_NORMAL = 0,
	TSPEC_ACKPOLICY_NOACK = 1,
	/* 2 is reserved */
	TSPEC_ACKPOLICY_BLOCKACK = 3,

} MLAN_PACK_END IEEEtypes_WMM_TSPEC_TS_Info_AckPolicy_e;

/** Enumerator for TSPEC Trafffice type */
typedef MLAN_PACK_START enum _IEEEtypes_WMM_TSPEC_TS_TRAFFIC_TYPE_e {

	TSPEC_TRAFFIC_APERIODIC = 0,
	TSPEC_TRAFFIC_PERIODIC = 1,

} MLAN_PACK_END IEEEtypes_WMM_TSPEC_TS_TRAFFIC_TYPE_e;

/** Data structure of WMM TSPEC information */
typedef MLAN_PACK_START struct {
#ifdef BIG_ENDIAN_SUPPORT
	t_u8 Reserved17_23:7;	/* ! Reserved */
	t_u8 Schedule:1;
	IEEEtypes_WMM_TSPEC_TS_Info_AckPolicy_e AckPolicy:2;
	t_u8 UserPri:3;		/* ! 802.1d User Priority */
	IEEEtypes_WMM_TSPEC_TS_Info_PSB_e PowerSaveBehavior:1;	/* ! Legacy/Trigg */
	t_u8 Aggregation:1;	/* ! Reserved */
	t_u8 AccessPolicy2:1;	/* ! */
	t_u8 AccessPolicy1:1;	/* ! */
	IEEEtypes_WMM_TSPEC_TS_Info_Direction_e Direction:2;
	t_u8 TID:4;		/* ! Unique identifier */
	IEEEtypes_WMM_TSPEC_TS_TRAFFIC_TYPE_e TrafficType:1;
#else
	IEEEtypes_WMM_TSPEC_TS_TRAFFIC_TYPE_e TrafficType:1;
	t_u8 TID:4;		/* ! Unique identifier */
	IEEEtypes_WMM_TSPEC_TS_Info_Direction_e Direction:2;
	t_u8 AccessPolicy1:1;	/* ! */
	t_u8 AccessPolicy2:1;	/* ! */
	t_u8 Aggregation:1;	/* ! Reserved */
	IEEEtypes_WMM_TSPEC_TS_Info_PSB_e PowerSaveBehavior:1;	/* ! Legacy/Trigg */
	t_u8 UserPri:3;		/* ! 802.1d User Priority */
	IEEEtypes_WMM_TSPEC_TS_Info_AckPolicy_e AckPolicy:2;
	t_u8 Schedule:1;
	t_u8 Reserved17_23:7;	/* ! Reserved */
#endif
} MLAN_PACK_END IEEEtypes_WMM_TSPEC_TS_Info_t;

/** Data structure of WMM TSPEC Nominal Size */
typedef MLAN_PACK_START struct {
#ifdef BIG_ENDIAN_SUPPORT
	t_u16 Fixed:1;		/* ! 1: Fixed size given in Size, 0: Var, size is nominal */
	t_u16 Size:15;		/* ! Nominal size in octets */
#else
	t_u16 Size:15;		/* ! Nominal size in octets */
	t_u16 Fixed:1;		/* ! 1: Fixed size given in Size, 0: Var, size is nominal */
#endif
} MLAN_PACK_END IEEEtypes_WMM_TSPEC_NomMSDUSize_t;

/** Data structure of WMM TSPEC SBWA */
typedef MLAN_PACK_START struct {
#ifdef BIG_ENDIAN_SUPPORT
	t_u16 Whole:3;		/* ! Whole portion */
	t_u16 Fractional:13;	/* ! Fractional portion */
#else
	t_u16 Fractional:13;	/* ! Fractional portion */
	t_u16 Whole:3;		/* ! Whole portion */
#endif
} MLAN_PACK_END IEEEtypes_WMM_TSPEC_SBWA;

/** Data structure of WMM TSPEC Body */
typedef MLAN_PACK_START struct {

    /** TS Information */
	IEEEtypes_WMM_TSPEC_TS_Info_t TSInfo;
    /** NomMSDU size */
	IEEEtypes_WMM_TSPEC_NomMSDUSize_t NomMSDUSize;
    /** MAximum MSDU size */
	t_u16 MaximumMSDUSize;
    /** Minimum Service Interval */
	t_u32 MinServiceInterval;
    /** Maximum Service Interval */
	t_u32 MaxServiceInterval;
    /** Inactivity Interval */
	t_u32 InactivityInterval;
    /** Suspension Interval */
	t_u32 SuspensionInterval;
    /** Service Start Time */
	t_u32 ServiceStartTime;
    /** Minimum Data Rate */
	t_u32 MinimumDataRate;
    /** Mean Data Rate */
	t_u32 MeanDataRate;
    /** Peak Data Rate */
	t_u32 PeakDataRate;
    /** Maximum Burst Size */
	t_u32 MaxBurstSize;
    /** Delay Bound */
	t_u32 DelayBound;
    /** Minimum Phy Rate */
	t_u32 MinPHYRate;
    /** Surplus BA Allowance */
	IEEEtypes_WMM_TSPEC_SBWA SurplusBWAllowance;
    /** Medium Time */
	t_u16 MediumTime;
} MLAN_PACK_END IEEEtypes_WMM_TSPEC_Body_t;

/** Data structure of WMM TSPEC all elements */
typedef MLAN_PACK_START struct {
    /** Element ID */
	t_u8 ElementId;
    /** Length */
	t_u8 Len;
    /** Oui Type */
	t_u8 OuiType[4];	/* 00:50:f2:02 */
    /** Ouisubtype */
	t_u8 OuiSubType;	/* 01 */
    /** Version */
	t_u8 Version;

    /** TspecBody */
	IEEEtypes_WMM_TSPEC_Body_t TspecBody;

} MLAN_PACK_END IEEEtypes_WMM_TSPEC_t;

/** WMM Action Category values */
typedef MLAN_PACK_START enum _IEEEtypes_ActionCategory_e {

	IEEE_MGMT_ACTION_CATEGORY_SPECTRUM_MGMT = 0,
	IEEE_MGMT_ACTION_CATEGORY_QOS = 1,
	IEEE_MGMT_ACTION_CATEGORY_DLS = 2,
	IEEE_MGMT_ACTION_CATEGORY_BLOCK_ACK = 3,
	IEEE_MGMT_ACTION_CATEGORY_PUBLIC = 4,
	IEEE_MGMT_ACTION_CATEGORY_RADIO_RSRC = 5,
	IEEE_MGMT_ACTION_CATEGORY_FAST_BSS_TRANS = 6,
	IEEE_MGMT_ACTION_CATEGORY_HT = 7,

	IEEE_MGMT_ACTION_CATEGORY_WNM = 10,
	IEEE_MGMT_ACTION_CATEGORY_UNPROTECT_WNM = 11,

	IEEE_MGMT_ACTION_CATEGORY_WMM_TSPEC = 17
} MLAN_PACK_END IEEEtypes_ActionCategory_e;

/** WMM TSPEC operations */
typedef MLAN_PACK_START enum _IEEEtypes_WMM_Tspec_Action_e {

	TSPEC_ACTION_CODE_ADDTS_REQ = 0,
	TSPEC_ACTION_CODE_ADDTS_RSP = 1,
	TSPEC_ACTION_CODE_DELTS = 2,

} MLAN_PACK_END IEEEtypes_WMM_Tspec_Action_e;

/** WMM TSPEC Category Action Base */
typedef MLAN_PACK_START struct {

    /** Category */
	IEEEtypes_ActionCategory_e category;
    /** Action */
	IEEEtypes_WMM_Tspec_Action_e action;
    /** Dialog Token */
	t_u8 dialogToken;

} MLAN_PACK_END IEEEtypes_WMM_Tspec_Action_Base_Tspec_t;

/** WMM TSPEC AddTS request structure */
typedef MLAN_PACK_START struct {

    /** Tspec action */
	IEEEtypes_WMM_Tspec_Action_Base_Tspec_t tspecAct;
    /** Status Code */
	t_u8 statusCode;
    /** tspecIE */
	IEEEtypes_WMM_TSPEC_t tspecIE;

	/* Place holder for additional elements after the TSPEC */
	t_u8 subElem[256];

} MLAN_PACK_END IEEEtypes_Action_WMM_AddTsReq_t;

/** WMM TSPEC AddTS response structure */
typedef MLAN_PACK_START struct {
	IEEEtypes_WMM_Tspec_Action_Base_Tspec_t tspecAct;
	t_u8 statusCode;
	IEEEtypes_WMM_TSPEC_t tspecIE;

	/* Place holder for additional elements after the TSPEC */
	t_u8 subElem[256];

} MLAN_PACK_END IEEEtypes_Action_WMM_AddTsRsp_t;

/** WMM TSPEC DelTS structure */
typedef MLAN_PACK_START struct {
    /** tspec Action */
	IEEEtypes_WMM_Tspec_Action_Base_Tspec_t tspecAct;
    /** Reason Code */
	t_u8 reasonCode;
    /** tspecIE */
	IEEEtypes_WMM_TSPEC_t tspecIE;

} MLAN_PACK_END IEEEtypes_Action_WMM_DelTs_t;

/** union of WMM TSPEC structures */
typedef MLAN_PACK_START union {
    /** tspec Action */
	IEEEtypes_WMM_Tspec_Action_Base_Tspec_t tspecAct;

    /** add TS request */
	IEEEtypes_Action_WMM_AddTsReq_t addTsReq;
    /** add TS response */
	IEEEtypes_Action_WMM_AddTsRsp_t addTsRsp;
    /** Delete TS */
	IEEEtypes_Action_WMM_DelTs_t delTs;

} MLAN_PACK_END IEEEtypes_Action_WMMAC_t;

/** union of WMM TSPEC & Action category */
typedef MLAN_PACK_START union {
    /** Category */
	IEEEtypes_ActionCategory_e category;

    /** wmmAc */
	IEEEtypes_Action_WMMAC_t wmmAc;

} MLAN_PACK_END IEEEtypes_ActionFrame_t;

/** Data structure for subband set */
typedef MLAN_PACK_START struct _IEEEtypes_SubbandSet_t {
    /** First channel */
	t_u8 first_chan;
    /** Number of channels */
	t_u8 no_of_chan;
    /** Maximum Tx power in dBm */
	t_u8 max_tx_pwr;
} MLAN_PACK_END IEEEtypes_SubbandSet_t, *pIEEEtypes_SubbandSet_t;

#ifdef STA_SUPPORT
/** Data structure for Country IE */
typedef MLAN_PACK_START struct _IEEEtypes_CountryInfoSet_t {
    /** Element ID */
	t_u8 element_id;
    /** Length */
	t_u8 len;
    /** Country code */
	t_u8 country_code[COUNTRY_CODE_LEN];
    /** Set of subbands */
	IEEEtypes_SubbandSet_t sub_band[1];
} MLAN_PACK_END IEEEtypes_CountryInfoSet_t, *pIEEEtypes_CountryInfoSet_t;

/** Data structure for Country IE full set */
typedef MLAN_PACK_START struct _IEEEtypes_CountryInfoFullSet_t {
    /** Element ID */
	t_u8 element_id;
    /** Length */
	t_u8 len;
    /** Country code */
	t_u8 country_code[COUNTRY_CODE_LEN];
    /** Set of subbands */
	IEEEtypes_SubbandSet_t sub_band[MRVDRV_MAX_SUBBAND_802_11D];
} MLAN_PACK_END IEEEtypes_CountryInfoFullSet_t,
	*pIEEEtypes_CountryInfoFullSet_t;

#endif /* STA_SUPPORT */

/** Data structure for Link ID */
typedef MLAN_PACK_START struct _IEEEtypes_LinkIDElement_t {
    /** Element ID */
	t_u8 element_id;
    /** Length */
	t_u8 len;
	/** bssid */
	t_u8 bssid[MLAN_MAC_ADDR_LENGTH];
	/** initial sta address */
	t_u8 init_sta[MLAN_MAC_ADDR_LENGTH];
	/** respose sta address */
	t_u8 resp_sta[MLAN_MAC_ADDR_LENGTH];
} MLAN_PACK_END IEEEtypes_LinkIDElement_t, *pIEEEtypes_LinkIDElement_t;

/** HT Capabilities Data */
typedef struct MLAN_PACK_START _HTCap_t {
    /** HT Capabilities Info field */
	t_u16 ht_cap_info;
    /** A-MPDU Parameters field */
	t_u8 ampdu_param;
    /** Supported MCS Set field */
	t_u8 supported_mcs_set[16];
    /** HT Extended Capabilities field */
	t_u16 ht_ext_cap;
    /** Transmit Beamforming Capabilities field */
	t_u32 tx_bf_cap;
    /** Antenna Selection Capability field */
	t_u8 asel;
} MLAN_PACK_END HTCap_t, *pHTCap_t;

/** HT Information Data */
typedef struct MLAN_PACK_START _HTInfo_t {
    /** Primary channel */
	t_u8 pri_chan;
    /** Field 2 */
	t_u8 field2;
    /** Field 3 */
	t_u16 field3;
    /** Field 4 */
	t_u16 field4;
    /** Bitmap indicating MCSs supported by all HT STAs in the BSS */
	t_u8 basic_mcs_set[16];
} MLAN_PACK_END HTInfo_t, *pHTInfo_t;

/** 20/40 BSS Coexistence Data */
typedef struct MLAN_PACK_START _BSSCo2040_t {
    /** 20/40 BSS Coexistence value */
	t_u8 bss_co_2040_value;
} MLAN_PACK_END BSSCo2040_t, *pBSSCo2040_t;

#define MAX_DSCP_EXCEPTION_NUM  21
/** DSCP Range */
typedef struct MLAN_PACK_START _DSCP_Exception_t {
	/* DSCP value 0 to 63 or ff */
	t_u8 dscp_value;
	/* user priority 0-7 */
	t_u8 user_priority;
} MLAN_PACK_END DSCP_Exception_t, *pDSCP_Exception_t;

/** DSCP Range */
typedef struct MLAN_PACK_START _DSCP_Range_t {
	/* DSCP low value */
	t_u8 dscp_low_value;
	/* DSCP high value */
	t_u8 dscp_high_value;
} MLAN_PACK_END DSCP_Range_t, *pDSCP_Range_t;

/** Overlapping BSS Scan Parameters Data */
typedef struct MLAN_PACK_START _OverlapBSSScanParam_t {
    /** OBSS Scan Passive Dwell in milliseconds */
	t_u16 obss_scan_passive_dwell;
    /** OBSS Scan Active Dwell in milliseconds */
	t_u16 obss_scan_active_dwell;
    /** BSS Channel Width Trigger Scan Interval in seconds */
	t_u16 bss_chan_width_trigger_scan_int;
    /** OBSS Scan Passive Total Per Channel */
	t_u16 obss_scan_passive_total;
    /** OBSS Scan Active Total Per Channel */
	t_u16 obss_scan_active_total;
    /** BSS Width Channel Transition Delay Factor */
	t_u16 bss_width_chan_trans_delay;
    /** OBSS Scan Activity Threshold */
	t_u16 obss_scan_active_threshold;
} MLAN_PACK_END OBSSScanParam_t, *pOBSSScanParam_t;

/** HT Capabilities IE */
typedef MLAN_PACK_START struct _IEEEtypes_HTCap_t {
    /** Generic IE header */
	IEEEtypes_Header_t ieee_hdr;
    /** HTCap struct */
	HTCap_t ht_cap;
} MLAN_PACK_END IEEEtypes_HTCap_t, *pIEEEtypes_HTCap_t;

/** HT Information IE */
typedef MLAN_PACK_START struct _IEEEtypes_HTInfo_t {
    /** Generic IE header */
	IEEEtypes_Header_t ieee_hdr;
    /** HTInfo struct */
	HTInfo_t ht_info;
} MLAN_PACK_END IEEEtypes_HTInfo_t, *pIEEEtypes_HTInfo_t;

/** 20/40 BSS Coexistence IE */
typedef MLAN_PACK_START struct _IEEEtypes_2040BSSCo_t {
    /** Generic IE header */
	IEEEtypes_Header_t ieee_hdr;
    /** BSSCo2040_t struct */
	BSSCo2040_t bss_co_2040;
} MLAN_PACK_END IEEEtypes_2040BSSCo_t, *pIEEEtypes_2040BSSCo_t;

/** Extended Capabilities IE */
typedef MLAN_PACK_START struct _IEEEtypes_ExtCap_t {
    /** Generic IE header */
	IEEEtypes_Header_t ieee_hdr;
    /** ExtCap_t struct */
	ExtCap_t ext_cap;
} MLAN_PACK_END IEEEtypes_ExtCap_t, *pIEEEtypes_ExtCap_t;

/** Overlapping BSS Scan Parameters IE */
typedef MLAN_PACK_START struct _IEEEtypes_OverlapBSSScanParam_t {
    /** Generic IE header */
	IEEEtypes_Header_t ieee_hdr;
    /** OBSSScanParam_t struct */
	OBSSScanParam_t obss_scan_param;
} MLAN_PACK_END IEEEtypes_OverlapBSSScanParam_t,
	*pIEEEtypes_OverlapBSSScanParam_t;

/** Maximum number of subbands in the IEEEtypes_SupportedChannels_t structure */
#define WLAN_11H_MAX_SUBBANDS  5

/** Maximum number of DFS channels configured in IEEEtypes_IBSS_DFS_t */
#define WLAN_11H_MAX_IBSS_DFS_CHANNELS 25

/**  IEEE Power Constraint element (7.3.2.15) */
typedef MLAN_PACK_START struct {
	t_u8 element_id;    /**< IEEE Element ID = 32 */
	t_u8 len;	    /**< Element length after id and len */
	t_u8 local_constraint;
			    /**< Local power constraint applied to 11d
                             chan info */
} MLAN_PACK_END IEEEtypes_PowerConstraint_t;

/**  IEEE Power Capability element (7.3.2.16) */
typedef MLAN_PACK_START struct {
	t_u8 element_id;	    /**< IEEE Element ID = 33 */
	t_u8 len;		    /**< Element length after id and len */
	t_s8 min_tx_power_capability;
				    /**< Minimum Transmit power (dBm) */
	t_s8 max_tx_power_capability;
				    /**< Maximum Transmit power (dBm) */
} MLAN_PACK_END IEEEtypes_PowerCapability_t;

/**  IEEE TPC Report element (7.3.2.18) */
typedef MLAN_PACK_START struct {
	t_u8 element_id;/**< IEEE Element ID = 35 */
	t_u8 len;	/**< Element length after id and len */
	t_s8 tx_power;	/**< Max power used to transmit the TPC Report frame (dBm) */
	t_s8 link_margin;
			/**< Link margin when TPC Request received (dB) */
} MLAN_PACK_END IEEEtypes_TPCReport_t;

/*  IEEE Supported Channel sub-band description (7.3.2.19) */
/**
 *  Sub-band description used in the supported channels element.
 */
typedef MLAN_PACK_START struct {
	t_u8 start_chan;/**< Starting channel in the subband */
	t_u8 num_chans;	/**< Number of channels in the subband */

} MLAN_PACK_END IEEEtypes_SupportChan_Subband_t;

/*  IEEE Supported Channel element (7.3.2.19) */
/**
 *  Sent in association requests. Details the sub-bands and number
 *    of channels supported in each subband
 */
typedef MLAN_PACK_START struct {
	t_u8 element_id;/**< IEEE Element ID = 36 */
	t_u8 len;	/**< Element length after id and len */

    /** Configured sub-bands information in the element */
	IEEEtypes_SupportChan_Subband_t subband[WLAN_11H_MAX_SUBBANDS];

} MLAN_PACK_END IEEEtypes_SupportedChannels_t;

/*  IEEE Channel Switch Announcement Element (7.3.2.20) */
/**
 *  Provided in beacons and probe responses.  Used to advertise when
 *    and to which channel it is changing to.  Only starting STAs in
 *    an IBSS and APs are allowed to originate a chan switch element.
 */
typedef MLAN_PACK_START struct {
	t_u8 element_id;	/**< IEEE Element ID = 37 */
	t_u8 len;		/**< Element length after id and len */
	t_u8 chan_switch_mode;	/**< STA should not transmit any frames if 1 */
	t_u8 new_channel_num;	/**< Channel # that AP/IBSS is moving to */
	t_u8 chan_switch_count;	/**< # of TBTTs before channel switch */

} MLAN_PACK_END IEEEtypes_ChanSwitchAnn_t;

/*  IEEE Quiet Period Element (7.3.2.23) */
/**
 *  Provided in beacons and probe responses.  Indicates times during
 *    which the STA should not be transmitting data.  Only starting STAs in
 *    an IBSS and APs are allowed to originate a quiet element.
 */
typedef MLAN_PACK_START struct {
	t_u8 element_id;    /**< IEEE Element ID = 40 */
	t_u8 len;	    /**< Element length after id and len */
	t_u8 quiet_count;   /**< Number of TBTTs until beacon with the quiet period */
	t_u8 quiet_period;  /**< Regular quiet period, # of TBTTS between periods */
	t_u16 quiet_duration;
			    /**< Duration of the quiet period in TUs */
	t_u16 quiet_offset; /**< Offset in TUs from the TBTT for the quiet period */

} MLAN_PACK_END IEEEtypes_Quiet_t;

/**
***  @brief Map octet of the basic measurement report (7.3.2.22.1)
**/
typedef MLAN_PACK_START struct {
#ifdef BIG_ENDIAN_SUPPORT
    /**< Reserved */
	t_u8 rsvd5_7:3;
    /**< Channel is unmeasured */
	t_u8 unmeasured:1;
    /**< Radar detected on channel */
	t_u8 radar:1;
    /**< Unidentified signal found on channel */
	t_u8 unidentified_sig:1;
    /**< OFDM preamble detected on channel */
	t_u8 ofdm_preamble:1;
    /**< At least one valid MPDU received on channel */
	t_u8 bss:1;
#else
    /**< At least one valid MPDU received on channel */
	t_u8 bss:1;
    /**< OFDM preamble detected on channel */
	t_u8 ofdm_preamble:1;
    /**< Unidentified signal found on channel */
	t_u8 unidentified_sig:1;
    /**< Radar detected on channel */
	t_u8 radar:1;
    /**< Channel is unmeasured */
	t_u8 unmeasured:1;
    /**< Reserved */
	t_u8 rsvd5_7:3;
#endif				/* BIG_ENDIAN_SUPPORT */

} MLAN_PACK_END MeasRptBasicMap_t;

/*  IEEE DFS Channel Map field (7.3.2.24) */
/**
 *  Used to list supported channels and provide a octet "map" field which
 *    contains a basic measurement report for that channel in the
 *    IEEEtypes_IBSS_DFS_t element
 */
typedef MLAN_PACK_START struct {
	t_u8 channel_number;	/**< Channel number */
	MeasRptBasicMap_t rpt_map;
				/**< Basic measurement report for the channel */

} MLAN_PACK_END IEEEtypes_ChannelMap_t;

/*  IEEE IBSS DFS Element (7.3.2.24) */
/**
 *  IBSS DFS element included in ad hoc beacons and probe responses.
 *    Provides information regarding the IBSS DFS Owner as well as the
 *    originating STAs supported channels and basic measurement results.
 */
typedef MLAN_PACK_START struct {
	t_u8 element_id;		    /**< IEEE Element ID = 41 */
	t_u8 len;			    /**< Element length after id and len */
	t_u8 dfs_owner[MLAN_MAC_ADDR_LENGTH];
					    /**< DFS Owner STA Address */
	t_u8 dfs_recovery_interval;	    /**< DFS Recovery time in TBTTs */

    /** Variable length map field, one Map entry for each supported channel */
	IEEEtypes_ChannelMap_t channel_map[WLAN_11H_MAX_IBSS_DFS_CHANNELS];

} MLAN_PACK_END IEEEtypes_IBSS_DFS_t;

/* 802.11h BSS information kept for each BSSID received in scan results */
/**
 * IEEE BSS information needed from scan results for later processing in
 *    join commands
 */
typedef struct {
	t_u8 sensed_11h;
		      /**< Capability bit set or 11h IE found in this BSS */

	IEEEtypes_PowerConstraint_t power_constraint;
						  /**< Power Constraint IE */
	IEEEtypes_PowerCapability_t power_capability;
						  /**< Power Capability IE */
	IEEEtypes_TPCReport_t tpc_report;	  /**< TPC Report IE */
	IEEEtypes_ChanSwitchAnn_t chan_switch_ann;/**< Channel Switch Announcement IE */
	IEEEtypes_Quiet_t quiet;		  /**< Quiet IE */
	IEEEtypes_IBSS_DFS_t ibss_dfs;		  /**< IBSS DFS Element IE */

} wlan_11h_bss_info_t;

/** Ethernet packet type for TDLS */
#define MLAN_ETHER_PKT_TYPE_TDLS_ACTION (0x890D)

/*802.11z  TDLS action frame type and strcuct */
typedef MLAN_PACK_START struct {
	/*link indentifier ie =101 */
	t_u8 element_id;
	/*len = 18 */
	t_u8 len;
   /** bssid */
	t_u8 bssid[MLAN_MAC_ADDR_LENGTH];
   /** init sta mac address */
	t_u8 init_sta[MLAN_MAC_ADDR_LENGTH];
   /** resp sta mac address */
	t_u8 resp_sta[MLAN_MAC_ADDR_LENGTH];
} MLAN_PACK_END IEEEtypes_tdls_linkie;

/** action code for tdls setup request */
#define TDLS_SETUP_REQUEST 0
/** action code for tdls setup response */
#define TDLS_SETUP_RESPONSE 1
/** action code for tdls setup confirm */
#define TDLS_SETUP_CONFIRM 2
/** action code for tdls tear down */
#define TDLS_TEARDOWN 3
/** action code for tdls traffic indication */
#define TDLS_PEER_TRAFFIC_INDICATION 4
/** action code for tdls channel switch request */
#define TDLS_CHANNEL_SWITCH_REQUEST 5
/** action code for tdls channel switch response */
#define TDLS_CHANNEL_SWITCH_RESPONSE 6
/** action code for tdls psm request */
#define TDLS_PEER_PSM_REQUEST 7
/** action code for tdls psm response */
#define TDLS_PEER_PSM_RESPONSE 8
/** action code for tdls traffic response */
#define TDLS_PEER_TRAFFIC_RESPONSE 9
/** action code for tdls discovery request */
#define TDLS_DISCOVERY_REQUEST 10
/** action code for TDLS discovery response */
#define TDLS_DISCOVERY_RESPONSE 14
/** category public */
#define CATEGORY_PUBLIC         4

/** action code for 20/40 BSS Coexsitence Management frame */
#define BSS_20_40_COEX 0

#ifdef STA_SUPPORT
/** Macro for maximum size of scan response buffer */
#define MAX_SCAN_RSP_BUF (16 * 1024)

/** Maximum number of channels that can be sent in user scan config */
#define WLAN_USER_SCAN_CHAN_MAX             50

/** Maximum length of SSID list */
#define MRVDRV_MAX_SSID_LIST_LENGTH         10

/** Scan all the channels in specified band */
#define BAND_SPECIFIED    0x80

/**
 *  IOCTL SSID List sub-structure sent in wlan_ioctl_user_scan_cfg
 *
 *  Used to specify SSID specific filters as well as SSID pattern matching
 *    filters for scan result processing in firmware.
 */
typedef MLAN_PACK_START struct _wlan_user_scan_ssid {
    /** SSID */
	t_u8 ssid[MLAN_MAX_SSID_LENGTH + 1];
    /** Maximum length of SSID */
	t_u8 max_len;
} MLAN_PACK_END wlan_user_scan_ssid;

/**
 *  @brief IOCTL channel sub-structure sent in wlan_ioctl_user_scan_cfg
 *
 *  Multiple instances of this structure are included in the IOCTL command
 *   to configure a instance of a scan on the specific channel.
 */
typedef MLAN_PACK_START struct _wlan_user_scan_chan {
    /** Channel Number to scan */
	t_u8 chan_number;
    /** Radio type: 'B/G' Band = 0, 'A' Band = 1 */
	t_u8 radio_type;
    /** Scan type: Active = 1, Passive = 2 */
	t_u8 scan_type;
    /** Reserved */
	t_u8 reserved;
    /** Scan duration in milliseconds; if 0 default used */
	t_u32 scan_time;
} MLAN_PACK_END wlan_user_scan_chan;

/** channel statictics */
typedef MLAN_PACK_START struct _ChanStatistics_t {
    /** channle number */
	t_u8 chan_num;
	/** band info */
	Band_Config_t bandcfg;
	/** flags */
	t_u8 flags;
	/** noise */
	t_s8 noise;
	/** total network */
	t_u16 total_networks;
	/** scan duration */
	t_u16 cca_scan_duration;
	/** busy duration */
	t_u16 cca_busy_duration;
} MLAN_PACK_END ChanStatistics_t;

/** Enhance ext scan type defination */
typedef enum _MLAN_EXT_SCAN_TYPE {
	EXT_SCAN_DEFAULT,
	EXT_SCAN_ENHANCE,
	EXT_SCAN_CANCEL,
} MLAN_EXT_SCAN_TYPE;

/**
 *  Input structure to configure an immediate scan cmd to firmware
 *
 *  Specifies a number of parameters to be used in general for the scan
 *    as well as a channel list (wlan_user_scan_chan) for each scan period
 *    desired.
 */
typedef MLAN_PACK_START struct {
    /**
     *  Flag set to keep the previous scan table intact
     *
     *  If set, the scan results will accumulate, replacing any previous
     *   matched entries for a BSS with the new scan data
     */
	t_u8 keep_previous_scan;
    /**
     *  BSS mode to be sent in the firmware command
     *
     *  Field can be used to restrict the types of networks returned in the
     *    scan.  Valid settings are:
     *
     *   - MLAN_SCAN_MODE_BSS  (infrastructure)
     *   - MLAN_SCAN_MODE_IBSS (adhoc)
     *   - MLAN_SCAN_MODE_ANY  (unrestricted, adhoc and infrastructure)
     */
	t_u8 bss_mode;
    /**
     *  Configure the number of probe requests for active chan scans
     */
	t_u8 num_probes;
    /**
     *  @brief ssid filter flag
     */
	t_u8 ssid_filter;
    /**
     *  @brief BSSID filter sent in the firmware command to limit the results
     */
	t_u8 specific_bssid[MLAN_MAC_ADDR_LENGTH];
    /**
     *  SSID filter list used in the to limit the scan results
     */
	wlan_user_scan_ssid ssid_list[MRVDRV_MAX_SSID_LIST_LENGTH];
    /**
     *  Variable number (fixed maximum) of channels to scan up
     */
	wlan_user_scan_chan chan_list[WLAN_USER_SCAN_CHAN_MAX];
    /** scan channel gap */
	t_u16 scan_chan_gap;
    /** scan type: 0 legacy, 1: enhance scan*/
	t_u8 ext_scan_type;
    /** flag to filer only probe response */
	t_u8 proberesp_only;
} MLAN_PACK_END wlan_user_scan_cfg;

/** Default scan interval in millisecond*/
#define DEFAULT_BGSCAN_INTERVAL 30000

/** action get all, except pps/uapsd config */
#define BG_SCAN_ACT_GET		0x0000
/** action set all, except pps/uapsd config */
#define BG_SCAN_ACT_SET             0x0001
/** action get pps/uapsd config */
#define BG_SCAN_ACT_GET_PPS_UAPSD   0x0100
/** action set pps/uapsd config */
#define BG_SCAN_ACT_SET_PPS_UAPSD   0x0101
/** action set all */
#define BG_SCAN_ACT_SET_ALL         0xff01
/** ssid match */
#define BG_SCAN_SSID_MATCH			0x0001
/** ssid match and RSSI exceeded */
#define BG_SCAN_SSID_RSSI_MATCH		0x0004
/**wait for all channel scan to complete to report scan result*/
#define BG_SCAN_WAIT_ALL_CHAN_DONE  0x80000000
/** Maximum number of channels that can be sent in bg scan config */
#define WLAN_BG_SCAN_CHAN_MAX       38

/**
 *  Input structure to configure bs scan cmd to firmware
 */
typedef MLAN_PACK_START struct {
    /** action */
	t_u16 action;
    /** enable/disable */
	t_u8 enable;
    /**  BSS type:
      *   MLAN_SCAN_MODE_BSS  (infrastructure)
      *   MLAN_SCAN_MODE_IBSS (adhoc)
      *   MLAN_SCAN_MODE_ANY  (unrestricted, adhoc and infrastructure)
      */
	t_u8 bss_type;
    /** number of channel scanned during each scan */
	t_u8 chan_per_scan;
    /** interval between consecutive scan */
	t_u32 scan_interval;
    /** bit 0: ssid match bit 1: ssid match and SNR exceeded
      *  bit 2: ssid match and RSSI exceeded
      *  bit 31: wait for all channel scan to complete to report scan result
      */
	t_u32 report_condition;
	/*  Configure the number of probe requests for active chan scans */
	t_u8 num_probes;
    /** RSSI threshold */
	t_u8 rssi_threshold;
    /** SNR threshold */
	t_u8 snr_threshold;
    /** repeat count */
	t_u16 repeat_count;
    /** start later flag */
	t_u16 start_later;
    /** SSID filter list used in the to limit the scan results */
	wlan_user_scan_ssid ssid_list[MRVDRV_MAX_SSID_LIST_LENGTH];
    /** Variable number (fixed maximum) of channels to scan up */
	wlan_user_scan_chan chan_list[WLAN_BG_SCAN_CHAN_MAX];
    /** scan channel gap */
	t_u16 scan_chan_gap;
} MLAN_PACK_END wlan_bgscan_cfg;
#endif /* STA_SUPPORT */

#ifdef PRAGMA_PACK
#pragma pack(pop)
#endif

/** BSSDescriptor_t
 *    Structure used to store information for beacon/probe response
 */
typedef struct _BSSDescriptor_t {
    /** MAC address */
	mlan_802_11_mac_addr mac_address;

    /** SSID */
	mlan_802_11_ssid ssid;

    /** WEP encryption requirement */
	t_u32 privacy;

    /** Receive signal strength in dBm */
	t_s32 rssi;

    /** Channel */
	t_u32 channel;

    /** Freq */
	t_u32 freq;

    /** Beacon period */
	t_u16 beacon_period;

    /** ATIM window */
	t_u32 atim_window;

    /** ERP flags */
	t_u8 erp_flags;

    /** Type of network in use */
	WLAN_802_11_NETWORK_TYPE network_type_use;

    /** Network infrastructure mode */
	t_u32 bss_mode;

    /** Network supported rates */
	WLAN_802_11_RATES supported_rates;

    /** Supported data rates */
	t_u8 data_rates[WLAN_SUPPORTED_RATES];

    /** Current channel bandwidth
     *  0 : 20MHZ
     *  1 : 40MHZ
     *  2 : 80MHZ
     *  3 : 160MHZ
     */
	t_u8 curr_bandwidth;

    /** Network band.
     * BAND_B(0x01): 'b' band
     * BAND_G(0x02): 'g' band
     * BAND_A(0X04): 'a' band
     */
	t_u16 bss_band;

    /** TSF timestamp from the current firmware TSF */
	t_u64 network_tsf;

    /** TSF value included in the beacon/probe response */
	t_u8 time_stamp[8];

    /** PHY parameter set */
	IEEEtypes_PhyParamSet_t phy_param_set;

    /** SS parameter set */
	IEEEtypes_SsParamSet_t ss_param_set;

    /** Capability information */
	IEEEtypes_CapInfo_t cap_info;

    /** WMM IE */
	IEEEtypes_WmmParameter_t wmm_ie;

    /** 802.11h BSS information */
	wlan_11h_bss_info_t wlan_11h_bss_info;

    /** Indicate disabling 11n when associate with AP */
	t_u8 disable_11n;
    /** 802.11n BSS information */
    /** HT Capabilities IE */
	IEEEtypes_HTCap_t *pht_cap;
    /** HT Capabilities Offset */
	t_u16 ht_cap_offset;
    /** HT Information IE */
	IEEEtypes_HTInfo_t *pht_info;
    /** HT Information Offset */
	t_u16 ht_info_offset;
    /** 20/40 BSS Coexistence IE */
	IEEEtypes_2040BSSCo_t *pbss_co_2040;
    /** 20/40 BSS Coexistence Offset */
	t_u16 bss_co_2040_offset;
    /** Extended Capabilities IE */
	IEEEtypes_ExtCap_t *pext_cap;
    /** Extended Capabilities Offset */
	t_u16 ext_cap_offset;
    /** Overlapping BSS Scan Parameters IE */
	IEEEtypes_OverlapBSSScanParam_t *poverlap_bss_scan_param;
    /** Overlapping BSS Scan Parameters Offset */
	t_u16 overlap_bss_offset;

#ifdef STA_SUPPORT
    /** Country information set */
	IEEEtypes_CountryInfoFullSet_t country_info;
#endif				/* STA_SUPPORT */

    /** WPA IE */
	IEEEtypes_VendorSpecific_t *pwpa_ie;
    /** WPA IE offset in the beacon buffer */
	t_u16 wpa_offset;
    /** RSN IE */
	IEEEtypes_Generic_t *prsn_ie;
    /** RSN IE offset in the beacon buffer */
	t_u16 rsn_offset;
#ifdef STA_SUPPORT
    /** WAPI IE */
	IEEEtypes_Generic_t *pwapi_ie;
    /** WAPI IE offset in the beacon buffer */
	t_u16 wapi_offset;
#endif
	/* Mobility domain IE */
	IEEEtypes_MobilityDomain_t *pmd_ie;
	/** Mobility domain IE offset in the beacon buffer */
	t_u16 md_offset;

    /** Pointer to the returned scan response */
	t_u8 *pbeacon_buf;
    /** Length of the stored scan response */
	t_u32 beacon_buf_size;
    /** Max allocated size for updated scan response */
	t_u32 beacon_buf_size_max;

} BSSDescriptor_t, *pBSSDescriptor_t;

#endif /* !_MLAN_IEEE_H_ */
