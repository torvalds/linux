/** @file IEEE_types.h
 *
 *  @brief This file contains definitions relating to messages specified in the
 *    IEEE 802.11 spec.
 *
 * Copyright (C) 2014-2017, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

/******************************************************
Change log:
    03/07/2014: Initial version
******************************************************/
#ifndef _IEEE_TYPES_H_
#define _IEEE_TYPES_H_

/*==========================================================================*/
/*                               INCLUDE FILES                              */
/*==========================================================================*/
#include "wltypes.h"

/*==========================================================================*/
/*                            PUBLIC DEFINITIONS                            */
/*==========================================================================*/

#define IS_BROADCAST(macaddr) ((*(UINT16 *)macaddr == 0xffff) &&        \
                               (*(UINT16 *)((UINT8 *)macaddr+2) == 0xffff) && \
                               (*(UINT16 *)((UINT8 *)macaddr+4) == 0xffff))

#define IS_MULTICAST(macaddr) ((*(UINT8*)macaddr & 0x01) == 0x01)

#define IS_GROUP(macaddr)     ((*(UINT8*)macaddr & 0x01) == 0x01)

#define ADDR_NOT_EQUAL(a, b)  (((a)[0] != (b)[0]) || ((a)[1] != (b)[1]) || \
                               ((a)[2] != (b)[2]) || ((a)[3] != (b)[3]) || \
                               ((a)[4] != (b)[4]) || ((a)[5] != (b)[5]))

#define LLC_SNAP_SIZE         6
#define ETHERTYPE_LEN         2

#define IEEE_MSG_TYPE(Hdr_p)    ((Hdr_p)->FrmCtl.Type)
#define IEEE_MSG_SUBTYPE(Hdr_p) ((Hdr_p)->FrmCtl.Subtype)

/*--------------------------------------------------------------*/
/* Reason Codes - these codes are used in management message    */
/* frame bodies to indicate why an action is taking place (such */
/* as a disassociation or deauthentication).                    */
/*--------------------------------------------------------------*/
#define IEEEtypes_REASON_RSVD                      0
#define IEEEtypes_REASON_UNSPEC                    1
#define IEEEtypes_REASON_PRIOR_AUTH_INVALID        2
#define IEEEtypes_REASON_DEAUTH_LEAVING            3
#define IEEEtypes_REASON_DISASSOC_INACTIVE         4
#define IEEEtypes_REASON_DISASSOC_AP_BUSY          5
#define IEEEtypes_REASON_CLASS2_NONAUTH            6
#define IEEEtypes_REASON_CLASS3_NONASSOC           7
#define IEEEtypes_REASON_DISASSOC_STA_HASLEFT      8
#define IEEEtypes_REASON_CANT_ASSOC_NONAUTH        9
/***************802.11h Reasons***************/
#define IEEEtypes_REASON_DISASSOC_BAD_POWERCAP    10
#define IEEEtypes_REASON_DISASSOC_BAD_SUPPCHAN    11
/***************802.11v Reasons***************/
#define IEEEtypes_REASON_BSS_TRANSITION_MGMT      12
/***************WPA Reasons*******************/
#define IEEEtypes_REASON_INVALID_IE               13
#define IEEEtypes_REASON_MIC_FAILURE              14
#define IEEEtypes_REASON_4WAY_HANDSHK_TIMEOUT     15
#define IEEEtypes_REASON_GRP_KEY_UPD_TIMEOUT      16
#define IEEEtypes_REASON_IE_4WAY_DIFF             17
#define IEEEtypes_REASON_INVALID_MCAST_CIPHER     18
#define IEEEtypes_REASON_INVALID_UNICAST_CIPHER   19
#define IEEEtypes_REASON_INVALID_AKMP             20
#define IEEEtypes_REASON_UNSUPT_RSN_VER           21
#define IEEEtypes_REASON_INVALID_RSN_CAP          22
#define IEEEtypes_REASON_8021X_AUTH_FAIL          23
#define IEEEtypes_REASON_CIPHER_POLICY_REJECT     24
/*************** 802.11z(TDLS) Reasons*************/
#define IEEEtypes_REASON_TDLS_TEARDOWN_TDLSPEER_UNREACHABLE 25
#define IEEEtypes_REASON_TDLS_TEARDOWN_UNSPEC     26
/***************802.11e Reasons***************/
#define IEEEtypes_REASON_DISASSOC_UNSPEC_QOS      32
#define IEEEtypes_REASON_DISASSOC_QAP_NO_BNDWDTH  33
#define IEEEtypes_REASON_DISASSOC_FRM_LOSS_BAD_CH 34
#define IEEEtypes_REASON_DISASSOC_QSTA_VIOL_TXOP  35
#define IEEEtypes_REASON_REQ_PEER_LEAVE_QBSS      36
#define IEEEtypes_REASON_REQ_PEER_NO_THANKS       37
#define IEEEtypes_REASON_REQ_PEER_ACM_MISMATCH    38
#define IEEEtypes_REASON_REQ_PEER_TIMEOUT         39
#define IEEEtypes_REASON_PEER_QSTA_NO_SUPP_CIPHER 45
/*********************************************/

/*------------------------------------------------------------*/
/* Status Codes - these codes are used in management message  */
/* frame bodies to indicate the results of an operation (such */
/* as association, reassociation, and authentication).        */
/*------------------------------------------------------------*/
#define IEEEtypes_STATUS_SUCCESS                           0
#define IEEEtypes_STATUS_UNSPEC_FAILURE                    1

/****************BEGIN: 802.11z(TDLS) status codes********/
#define IEEEtypes_STATUS_TDLS_WAKEUPSCHEDULE_REJECTED_BUT_ALT_PROVIDED 2
#define IEEEtypes_STATUS_TDLS_WAKEUPSCHEDULE_REJECTED      3
#define IEEEtypes_STATUS_SECURITY_DISABLED                 5
#define IEEEtypes_STATUS_UNACCEPTABLE_LIFETIME             6
#define IEEEtypes_STATUS_NOT_IN_SAME_BSS                   7
/****************END:   802.11z(TDLS) status codes********/
#define IEEEtypes_STATUS_CAPS_UNSUPPORTED                 10
#define IEEEtypes_STATUS_REASSOC_NO_ASSOC                 11
#define IEEEtypes_STATUS_ASSOC_DENIED_UNSPEC              12
#define IEEEtypes_STATUS_UNSUPPORTED_AUTHALG              13
#define IEEEtypes_STATUS_RX_AUTH_NOSEQ                    14
#define IEEEtypes_STATUS_CHALLENGE_FAIL                   15
#define IEEEtypes_STATUS_AUTH_TIMEOUT                     16
#define IEEEtypes_STATUS_ASSOC_DENIED_BUSY                17
#define IEEEtypes_STATUS_ASSOC_DENIED_RATES               18
#define IEEEtypes_STATUS_ASSOC_DENIED_NOSHORT             19
#define IEEEtypes_STATUS_ASSOC_DENIED_NOPBCC              20
#define IEEEtypes_STATUS_ASSOC_DENIED_NOAGILITY           21
#define IEEEtypes_STATUS_ASSOC_DENIED_SPECMGMT_REQD       22
#define IEEEtypes_STATUS_ASSOC_DENIED_BAD_POWERCAP        23
#define IEEEtypes_STATUS_ASSOC_DENIED_BAD_SUPPCHAN        24
#define IEEEtypes_STATUS_ASSOC_DENIED_NOSHORTSLOTTIME     25
#define IEEEtypes_STATUS_ASSOC_DENIED_NODSSSOFDM          26

#define IEEEtypes_STATUS_R0KH_UNAVAILABLE                 28

#define IEEEtypes_STATUS_TEMP_REJECTION                   30
#define IEEEtypes_STATUS_ROBUST_MGMT_VIOLAION             31
#define IEEEtypes_STATUS_UNSPEC_QOS_FAILURE               32
#define IEEEtypes_STATUS_ASSOC_DENIED_QAP_INSUFF_BNDWDTH  33
#define IEEEtypes_STATUS_ASSOC_DENIED_EXC_FRM_LOSS_BAD_CH 34
#define IEEEtypes_STATUS_ASSOC_DENIED_STA_NO_QOS_SUPP     35

#define IEEEtypes_STATUS_REQ_DECLINED                     37
#define IEEEtypes_STATUS_REQ_FAIL_INVALID_PARAMS          38
#define IEEEtypes_STATUS_FAIL_TS_AP_THINKS_ITS_SMART_THO  39
#define IEEEtypes_STATUS_INVALID_IE                       40
#define IEEEtypes_STATUS_INVALID_GROUP_CIPHER             41
#define IEEEtypes_STATUS_INVALID_PAIRWISE_CIPHER          42
#define IEEEtypes_STATUS_INVALID_AKMP                     43
#define IEEEtypes_STATUS_UNSUPPORTED_RSN_VER              44
#define IEEEtypes_STATUS_INVALID_RSN_CAPABILITIES         45
#define IEEEtypes_STATUS_CIPHER_POLICY_REJECT             46
#define IEEEtypes_STATUS_FAIL_TS_TRY_LATER_AFTER_TS_DELAY 47
#define IEEEtypes_STATUS_DIRECT_LINK_NOT_ALLOWED          48
#define IEEEtypes_STATUS_DEST_STA_NOT_IN_QBSS             49
#define IEEEtypes_STATUS_DEST_STA_NOT_A_QSTA              50
#define IEEEtypes_STATUS_LISTEN_INTERVAL_TOO_LARGE        51
#define IEEEtypes_STATUS_INVALID_FT_ACT_FRAME_COUNT       52
#define IEEEtypes_STATUS_INVALID_PMKID                    53
#define IEEEtypes_STATUS_INVALID_MDIE                     54
#define IEEEtypes_STATUS_INVALID_FTIE                     55
#define IEEEtypes_STATUS_REQ_TCLAS_NOT_SUPPORTED          56
#define IEEEtypes_STATUS_INSF_TCLAS_RSOURCES              57
#define IEEEtypes_STATUS_TS_FAIL_TRANS_SUGGESTED          58
#define IEEEtypes_STATUS_UAPSD_COEX_NOT_SUPPORTED         59
#define IEEEtypes_STATUS_REQ_UAPSD_COEX_MODE_NOT_SUP      60
#define IEEEtypes_STATUS_REQ_INVL_WITH_UAPSD_COEX_NOT_SUP 61
#define IEEEtypes_STATUS_INVALID_CONTENTS_OF_RSNIE        72

/*--------------------------------------------*/
/* Various sizes used in IEEE 802.11 messages */
/*--------------------------------------------*/
#define IEEEtypes_ADDRESS_SIZE         6
#define IEEEtypes_BITMAP_SIZE          251
#define IEEEtypes_CHALLENGE_TEXT_SIZE  128
#define IEEEtypes_CHALLENGE_TEXT_LEN   128
#define IEEEtypes_MAX_DATA_RATES       8
#define IEEEtypes_MAX_DATA_BODY_LEN    2312
#define IEEEtypes_MAX_MGMT_BODY_LEN    2312
#define IEEEtypes_SSID_SIZE            32
#define IEEEtypes_TIME_STAMP_SIZE      8
#define IEEEtypes_MAX_CHANNELS         14
#define IEEEtypes_MAX_BSS_DESCRIPTS    16
#define IEEEtypes_MAX_DATA_RATES_G     14
#define IEEEtypes_COUNTRY_CODE_SIZE    3
#define IEEEtypes_COUNTRY_MAX_TRIPLETS 83

/*---------------------------------------------------------------------*/
/* Define masks used to extract fields from the capability information */
/* structure in a beacon message.                                      */
/*---------------------------------------------------------------------*/
#define IEEEtypes_CAP_INFO_ESS              1
#define IEEEtypes_CAP_INFO_IBSS             2
#define IEEEtypes_CAP_INFO_CF_POLLABLE      4
#define IEEEtypes_CAP_INFO_CF_POLL_RQST     8
#define IEEEtypes_CAP_INFO_PRIVACY          16
#define IEEEtypes_CAP_INFO_SHORT_PREAMB     32
#define IEEEtypes_CAP_INFO_PBCC             64
#define IEEEtypes_CAP_INFO_CHANGE_AGILITY   128
#define IEEEtypes_CAP_INFO_SHORT_SLOT_TIME  0x0400
#define IEEEtypes_CAP_INFO_DSSS_OFDM        0x2000

/*---------------------------*/
/* Miscellaneous definitions */
/*---------------------------*/
#define IEEEtypes_PROTOCOL_VERSION     0

#define IEEEtypes_BASIC_RATE_FLAG      0x80
/* */
/* Used to determine which rates in a list are designated as basic rates */
/* */

#define IEEEtypes_SUPP_RATE_MASK     0x7F

#define IEEE_DATA_RATE_1Mbps    2
#define IEEE_DATA_RATE_2Mbps    4
#define IEEE_DATA_RATE_5_5Mbps  11
#define IEEE_DATA_RATE_11Mbps   22

#define IEEE_DATA_RATE_6Mbps    12
#define IEEE_DATA_RATE_9Mbps    18
#define IEEE_DATA_RATE_12Mbps   24
#define IEEE_DATA_RATE_18Mbps   36
#define IEEE_DATA_RATE_24Mbps   48
#define IEEE_DATA_RATE_36Mbps   72
#define IEEE_DATA_RATE_48Mbps   96
#define IEEE_DATA_RATE_54Mbps   108

/* */
/* Used to mask off the basic rate flag, if one exists, for given */
/* data rates */
/* */

#define IEEEtypes_RATE_MIN 2
/* */
/* The minimum allowable data rate in units of kb/s */
/* */

#define IEEEtypes_RATE_MAX 127
/* */
/* The maximum allowable data rate in units of kb/s */
/* */

#define IEEEtypes_TIME_UNIT 1024
/* */
/* The number of microseconds in 1 time unit, as specified in the */
/* 802.11 spec */
/* */
#define CONVERT_TU_TO_MILLISECOND(x)    (x * 1024 / 1000)
#define CONVERT_TU_TO_MICROSECOND(x)    (x * 1024)

/**
 * 802.11 frame classes
 */
#define IEEEtypes_CLASS1_FRAME 1
#define IEEEtypes_CLASS2_FRAME 2
#define IEEEtypes_CLASS3_FRAME 3

/*
 * Structure of an internet header, naked of options.
 *
 * ip_len and ip_off are sint16, rather than UINT16
 * pragmatically since otherwise unsigned comparisons can result
 * against negative integers quite easily, and fail in subtle ways.
 */
typedef MLAN_PACK_START struct {
#if 1				//BYTE_ORDER == LITTLE_ENDIAN
	UINT8 ip_hl:4;		/* header length */
	UINT8 ip_v:4;		/* version */
#endif
	UINT8 ip_tos;		/* type of service */
	sint16 ip_len;		/* total length */
	UINT16 ip_id;		/* identification */
	sint16 ip_off;		/* fragment offset field */
	UINT8 ip_ttl;		/* time to live */
	UINT8 ip_p;		/* protocol */
	UINT16 ip_sum;		/* checksum */
	UINT32 ip_src;		// source ip addr
	UINT32 ip_dst;		// dest ip address
} MLAN_PACK_END ip_hdr_t;
#define    IP_DF 0x4000		/* dont fragment flag */
#define    IP_MF 0x2000		/* more fragments flag */
#define    IP_OFFMASK 0x1fff	/* mask for fragmenting bits */

#define    IP_V4           4
#define    IP_V6           6
#define    IP_PROT_TCP     0x06
#define    IP_PROT_UDP     0x11

typedef MLAN_PACK_START struct {
	UINT8 priority:4;
	UINT8 ip_v:4;
	UINT8 flow_lbl[3];
	UINT16 payload_len;
	UINT8 next_hdr;
	UINT8 hop_limit;
	UINT8 ip_src[16];	/* source ip addr */
	UINT8 ip_dst[16];	/* dest ip address */
} MLAN_PACK_END ipv6_hdr_t;

#define ETHTYPE_ARP         0x0806
#define ETHTYPE_IP          0x0800
#define ARP_OP_REQ          1
#define ARP_OP_RESP         2
#define ETHTYPE_IPV6        0x86DD
#define ICMPV6_TYPE         0x3a

/* See RFC 826 for ARP protocol description. */
typedef MLAN_PACK_START struct {
	UINT16 ar_hrd;		// hardware address space
	UINT16 ar_pro;		// prototcol address space
	UINT8 ar_hln;		// byte length of hardware addr
	UINT8 ar_pln;		// byte length of protocol addr
	UINT16 ar_op;		// ARP opcode
} MLAN_PACK_END arp_hdr_t;

typedef MLAN_PACK_START struct {
	arp_hdr_t hdr;		// arp header
	UINT8 ar_sha[IEEEtypes_ADDRESS_SIZE];	// sender hardware addr
	UINT8 ar_spa[4];	// sender protocol addr
	UINT8 ar_tha[IEEEtypes_ADDRESS_SIZE];	// target hardware addr
	UINT8 ar_tpa[4];	// target protocol addr
} MLAN_PACK_END arp_t;

/* icmp header struct of echo request and echo reply */
typedef MLAN_PACK_START struct {
	UINT8 type;
	UINT8 code;
	UINT16 sum;
	UINT16 id;
	UINT16 seq;
} MLAN_PACK_END icmp_hdr_t;
#define ICMP_ECHO_REQ       8
#define ICMP_ECHO_REPLY     0

typedef MLAN_PACK_START struct {
	UINT8 type;
	UINT8 icode;
	UINT16 csum;
	UINT32 reserved;
	UINT8 target_addr[16];
} MLAN_PACK_END icmpv6_nsol_t;
#define ICMPV6_TYPE_NSOL 0x87
#define ICMPV6_TYPE_NADV 0x88

#define ICMPV6_NADV_FLAG_RTR (1<<31)
#define ICMPV6_NADV_FLAG_SOL (1<<30)
#define ICMPV6_NADV_FLAG_OVR (1<<29)

#define ICMPV6_OPT_TYPE_TLA  (0x2)
#define ICMPV6_OPT_TYPE_TLA_LEN (0x1)

typedef MLAN_PACK_START struct {
	UINT8 type;
	UINT8 icode;
	UINT16 csum;
	UINT32 reserved;
	UINT8 target_addr[16];
	UINT8 icmp_option_type;
	UINT8 icmp_option_length;
	UINT8 macAddr[6];
} MLAN_PACK_END icmpv6_nadv_t;

/*
*****************************************************************************
**
**
**                       802.1x Types
**
**
*****************************************************************************
*/
typedef MLAN_PACK_START enum {
	IEEE_8021X_PACKET_TYPE_EAP_PACKET = 0,
	IEEE_8021X_PACKET_TYPE_EAPOL_START = 1,
	IEEE_8021X_PACKET_TYPE_EAPOL_LOGOFF = 2,
	IEEE_8021X_PACKET_TYPE_EAPOL_KEY = 3,
	IEEE_8021X_PACKET_TYPE_ASF_ALERT = 4,

} MLAN_PACK_END IEEEtypes_8021x_PacketType_e;

typedef MLAN_PACK_START enum {
	IEEE_8021X_CODE_TYPE_REQUEST = 1,
	IEEE_8021X_CODE_TYPE_RESPONSE = 2,
	IEEE_8021X_CODE_TYPE_SUCCESS = 3,
	IEEE_8021X_CODE_TYPE_FAILURE = 4,

} MLAN_PACK_END IEEEtypes_8021x_CodeType_e;

/*
*****************************************************************************
**
**
**                       802.11 PHY Types
**
**
*****************************************************************************
*/
typedef MLAN_PACK_START enum {
	IEEE_PHY_TYPE_UNKNOWN = 0,

	IEEE_PHY_TYPE_FHSS_2_4_GHz = 1,
	IEEE_PHY_TYPE_DSSS_2_4_GHz = 2,
	IEEE_PHY_TYPE_IR_BASEBAND = 3,
	IEEE_PHY_TYPE_OFDM_5GHz = 4,
	IEEE_PHY_TYPE_HRDSSS = 5,
	IEEE_PHY_TYPE_ERP = 6,

} MLAN_PACK_END IEEEtypes_PhyType_e;

/*
*****************************************************************************
**
**
**                         802.11 Message Types
**
**
*****************************************************************************
*/
typedef enum {
	IEEE_TYPE_MANAGEMENT = 0,
	IEEE_TYPE_CONTROL,
	IEEE_TYPE_DATA
} IEEEtypes_MsgType_e;

/*
*****************************************************************************
**
**
**                      802.11 Mangagement SubTypes
**
**
*****************************************************************************
*/
typedef enum {
	IEEE_MSG_ASSOCIATE_RQST = 0,
	IEEE_MSG_ASSOCIATE_RSP,
	IEEE_MSG_REASSOCIATE_RQST,
	IEEE_MSG_REASSOCIATE_RSP,
	IEEE_MSG_PROBE_RQST,
	IEEE_MSG_PROBE_RSP,
	IEEE_MSG_BEACON = 8,
	IEEE_MSG_ATIM,
	IEEE_MSG_DISASSOCIATE,
	IEEE_MSG_AUTHENTICATE,
	IEEE_MSG_DEAUTHENTICATE,
	IEEE_MSG_ACTION
} IEEEtypes_MgmtSubType_e;

/*
*****************************************************************************
**
**
**                    802.11 Control Frame SubTypes
**
**
*****************************************************************************
*/
typedef enum {
	BF_RPRT_POLL = 4,
	NDPA = 5,
	BAR = 8,
	BA = 9,
	PS_POLL = 10,
	RTS = 11,
	CTS = 12,
	ACK = 13,
	CF_END = 14,
	CF_END_CF_ACK = 15,
} IEEEtypes_CtlSubType_e;

/*
*****************************************************************************
**
**
**                    802.11 Data Frame SubTypes
**
**
*****************************************************************************
*/
typedef enum {
	DATA = 0,
	DATA_CF_ACK = 1,
	DATA_CF_POLL = 2,
	DATA_CF_ACK_CF_POLL = 3,
	NULL_DATA = 4,
	CF_ACK = 5,
	CF_POLL = 6,
	CF_ACK_CF_POLL = 7,
	QOS_DATA = 8,
	QOS_DATA_CF_ACK = 9,
	QOS_DATA_CF_POLL = 10,
	QOS_DATA_CF_ACK_CF_POLL = 11,
	QOS_NULL = 12,
	RESERVED_13 = 13,
	QOS_CF_POLL_NO_DATA = 14,
	QOS_CF_ACK_CF_POLL_NO_DATA = 15
} IEEEtypes_DataSubType_e;

/*
*****************************************************************************
**
**
**                    802.11 Action Frame Categories
**
**
*****************************************************************************
*/
typedef MLAN_PACK_START enum {
	IEEE_MGMT_ACTION_CATEGORY_SPECTRUM_MGMT = 0,
	IEEE_MGMT_ACTION_CATEGORY_QOS = 1,
	IEEE_MGMT_ACTION_CATEGORY_DLS = 2,
	IEEE_MGMT_ACTION_CATEGORY_BLOCK_ACK = 3,
	IEEE_MGMT_ACTION_CATEGORY_UNPROTECT_PUBLIC = 4,
	IEEE_MGMT_ACTION_CATEGORY_RADIO_RSRC = 5,
	IEEE_MGMT_ACTION_CATEGORY_FAST_BSS_TRANS = 6,
	IEEE_MGMT_ACTION_CATEGORY_HT = 7,
	IEEE_MGMT_ACTION_CATEGORY_SA_QUERY = 8,
	IEEE_MGMT_ACTION_CATEGORY_PROTECT_PUBLIC = 9,
	IEEE_MGMT_ACTION_CATEGORY_PROTECT_WNM = 10,
	IEEE_MGMT_ACTION_CATEGORY_UNPROTECT_WNM = 11,
	IEEE_MGMT_ACTION_CATEGORY_TDLS = 12,

	IEEE_MGMT_ACTION_CATEGORY_WMM_TSPEC = 17,
	IEEE_MGMT_ACTION_CATEGORY_VHT = 21,
	IEEE_MGMT_ACTION_CATEGORY_PROTECT_VENDOR_SPECIFIC = 126,
	IEEE_MGMT_ACTION_CATEGORY_VENDOR_SPECIFIC = 127
} MLAN_PACK_END IEEEtypes_ActionCategory_e;

/*
** The possible types of commands sent from the SME
*/
typedef enum {
	SME_CMD_NONE,

	SME_CMD_AUTHENTICATE,
	SME_CMD_ASSOCIATE,
	SME_CMD_REASSOCIATE,

	SME_CMD_DEAUTHENTICATE,
	SME_CMD_DISASSOCIATE,

	SME_CMD_START,
	SME_CMD_JOIN,

	SME_CMD_RESET,
	SME_CMD_SCAN,

} IEEEtypes_SmeCmd_e;

/*
** The possible types of Basic Service Sets
*/
typedef enum {
	BSS_INFRASTRUCTURE = 1,
	BSS_INDEPENDENT,
	BSS_ANY,
	BSS_TDLS,
	// Firmware internal BSS types only
	BSS_BT_AMP = 0xF0,
	BSS_LAST = 0xFF
} IEEEtypes_Bss_e;

/*
**  802.11 Element and Subelement IDs
*/
typedef MLAN_PACK_START enum {
	ELEM_ID_SSID = 0,
	ELEM_ID_SUPPORTED_RATES = 1,
	ELEM_ID_FH_PARAM_SET = 2,
	ELEM_ID_DS_PARAM_SET = 3,
	ELEM_ID_CF_PARAM_SET = 4,
	ELEM_ID_TIM = 5,
	ELEM_ID_IBSS_PARAM_SET = 6,
	ELEM_ID_COUNTRY = 7,
	ELEM_ID_HOP_PARAM = 8,
	ELEM_ID_HOP_TABLE = 9,
	ELEM_ID_REQUEST = 10,
	ELEM_ID_BSS_LOAD = 11,
	ELEM_ID_EDCA_PARAM_SET = 12,
	ELEM_ID_TSPEC = 13,
	ELEM_ID_TCLAS = 14,
	ELEM_ID_SCHEDULE = 15,
	ELEM_ID_CHALLENGE_TEXT = 16,

	ELEM_ID_POWER_CONSTRAINT = 32,
	ELEM_ID_POWER_CAPABILITY = 33,
	ELEM_ID_TPC_REQUEST = 34,
	ELEM_ID_TPC_REPORT = 35,
	ELEM_ID_SUPPORTED_CHANNELS = 36,
	ELEM_ID_CHANNEL_SWITCH_ANN = 37,
	ELEM_ID_MEASUREMENT_REQ = 38,
	ELEM_ID_MEASUREMENT_RPT = 39,
	ELEM_ID_QUIET = 40,
	ELEM_ID_IBSS_DFS = 41,
	ELEM_ID_ERP_INFO = 42,
	ELEM_ID_TS_DELAY = 43,
	ELEM_ID_TCLAS_PROCESS = 44,
	ELEM_ID_HT_CAPABILITY = 45,
	ELEM_ID_QOS_CAPABILITY = 46,

	ELEM_ID_RSN = 48,

	ELEM_ID_EXT_SUPPORTED_RATES = 50,
	ELEM_ID_AP_CHANNEL_REPORT = 51,
	ELEM_ID_NEIGHBOR_REPORT = 52,
	ELEM_ID_RCPI = 53,
	ELEM_ID_MOBILITY_DOMAIN = 54,
	ELEM_ID_FAST_BSS_TRANS = 55,
	ELEM_ID_TIMEOUT_INTERVAL = 56,
	ELEM_ID_RIC_DATA = 57,
	ELEM_ID_DSE_REGISTERED_LOC = 58,
	ELEM_ID_SUPPORTED_REGCLASS = 59,
	ELEM_ID_EXT_CHAN_SWITCH_ANN = 60,
	ELEM_ID_HT_INFORMATION = 61,
	ELEM_ID_SECONDARY_CHAN_OFFSET = 62,
	ELEM_ID_BSS_ACCESS_DELAY = 63,
	ELEM_ID_ANTENNA_INFO = 64,
	ELEM_ID_RSNI = 65,
	ELEM_ID_MEAS_PILOT_TX_INFO = 66,
	ELEM_ID_BSS_AVAIL_ADM_CAP = 67,
	ELEM_ID_BSS_AC_ACCESS_DELAY = 68,

	ELEM_ID_RRM_ENABLED_CAP = 70,
	ELEM_ID_MULTI_BSSID = 71,
	ELEM_ID_2040_BSS_COEXISTENCE = 72,
	ELEM_ID_2040_BSS_INTOL_CHRPT = 73,
	ELEM_ID_OBSS_SCAN_PARAM = 74,
	ELEM_ID_RIC_DESCRIPTOR = 75,
	ELEM_ID_MANAGEMENT_MIC = 76,

	ELEM_ID_EVENT_REQUEST = 78,
	ELEM_ID_EVENT_REPORT = 79,
	ELEM_ID_DIAG_REQUEST = 80,
	ELEM_ID_DIAG_REPORT = 81,
	ELEM_ID_LOCATION_PARAM = 82,
	ELEM_ID_NONTRANS_BSSID_CAP = 83,
	ELEM_ID_SSID_LIST = 84,
	ELEM_ID_MBSSID_INDEX = 85,
	ELEM_ID_FMS_DESCRIPTOR = 86,
	ELEM_ID_FMS_REQUEST = 87,
	ELEM_ID_FMS_RESPONSE = 88,
	ELEM_ID_QOS_TRAFFIC_CAP = 89,
	ELEM_ID_BSS_MAX_IDLE_PERIOD = 90,
	ELEM_ID_TFS_REQUEST = 91,
	ELEM_ID_TFS_RESPONSE = 92,
	ELEM_ID_WNM_SLEEP_MODE = 93,
	ELEM_ID_TIM_BCAST_REQUEST = 94,
	ELEM_ID_TIM_BCAST_RESPONSE = 95,
	ELEM_ID_COLLOC_INTF_REPORT = 96,
	ELEM_ID_CHANNEL_USAGE = 97,
	ELEM_ID_TIME_ZONE = 98,
	ELEM_ID_DMS_REQUEST = 99,
	ELEM_ID_DMS_RESPONSE = 100,
	ELEM_ID_LINK_ID = 101,
	ELEM_ID_WAKEUP_SCHEDULE = 102,
	ELEM_ID_TDLS_CS_TIMING = 104,
	ELEM_ID_PTI_CONTROL = 105,
	ELEM_ID_PU_BUFFER_STATUS = 106,

	ELEM_ID_EXT_CAPABILITIES = 127,
	ELEM_ID_VHT_CAPABILITIES = 191,
	ELEM_ID_VHT_OPERATION = 192,
	ELEM_ID_WIDE_BAND_CHAN_SW = 193,
	ELEM_ID_AID = 197,
	ELEM_ID_VHT_OP_MODE_NOTIFICATION = 199,

	ELEM_ID_VENDOR_SPECIFIC = 221,

	/* Subelement IDs */
	SUBELEM_ID_REPORTED_FRAME_BODY = 1,
	SUBELEM_ID_REPORTING_DETAIL = 2,

	SUBELEM_ID_PMK_R1_KEY_HOLDER_ID = 1,
	SUBELEM_ID_GTK = 2,
	SUBELEM_ID_PMK_R0_KEY_HOLDER_ID = 3,
	SUBELEM_ID_IGTK = 4,

	/* Non-IEEE IDs */
	ELEM_ID_WAPI = 68,

} MLAN_PACK_END IEEEtypes_ElementId_e;

/* The KDE data types */
typedef enum {
	KDE_DATA_TYPE_RESERVED,
	KDE_DATA_TYPE_GTK = 1,
	KDE_DATA_TYPE_RESERVED2,
	KDE_DATA_TYPE_MACADDR = 3,
	KDE_DATA_TYPE_PMKID = 4,
	KDE_DATA_TYPE_SMK = 5,
	KDE_DATA_TYPE_NONCE = 6,
	KDE_DATA_TYPE_LIFETIME = 7,
	KDE_DATA_TYPE_ERROR = 8,
	KDE_DATA_TYPE_IGTK = 9
} IEEEtypes_KDEDataType_e;

/* The possible power management modes */
typedef enum {
	PWR_MODE_ACTIVE,
	PWR_MODE_PWR_SAVE
} IEEEtypes_PwrMgmtMode_e;

/* The possible types of authentication */
typedef enum {
	AUTH_OPEN_SYSTEM = 0,
	AUTH_SHARED_KEY = 1,
	AUTH_FAST_BSS_TRANSITION = 2,

	AUTH_NETWORK_EAP = 0x80,

	AUTH_NOT_SUPPORTED,

	AUTH_AUTO_OPEN_OR_SHARED = 0xFF
} IEEEtypes_AuthType_e;

/* The possible responses to a request to scan */
typedef enum {
	SCAN_RESULT_SUCCESS,
	SCAN_RESULT_INVALID_PARAMETERS,
	SCAN_RESULT_INTERNAL_ERROR,
	SCAN_RESULT_PARTIAL_RESULTS,

} IEEEtypes_ScanResult_e;

/* The possible responses to a request to join a BSS */
typedef enum {
	JOIN_RESULT_SUCCESS,
	JOIN_RESULT_INTERNAL_ERROR,
	JOIN_RESULT_TIMEOUT
} IEEEtypes_JoinResult_e;

/* The possible results to a request to authenticate */
typedef enum {
	AUTH_RESULT_SUCCESS,
	AUTH_RESULT_INTERNAL_ERROR,
	AUTH_RESULT_TIMEOUT,
	AUTH_RESULT_UNUSED,	/* Do not reuse, maps to refused assoc result */
	AUTH_RESULT_UNHANDLED_MSG,
	AUTH_RESULT_REFUSED,
	AUTH_RESULT_INVALID_PARAMETER
} IEEEtypes_AuthResult_e;

/* The possible results to a request to deauthenticate */
typedef enum {
	DEAUTH_RESULT_SUCCESS,
	DEAUTH_RESULT_INVALID_PARAMETERS,
	DEAUTH_RESULT_TOO_MANY_SIMULTANEOUS_RQSTS,
	DEAUTH_RESULT_TIMEOUT
} IEEEtypes_DeauthResult_e;

/* The possible results to a request to associate */
typedef enum {
	ASSOC_RESULT_SUCCESS,
	ASSOC_RESULT_INTERNAL_ERROR,
	ASSOC_RESULT_TIMEOUT,
	ASSOC_RESULT_REFUSED,
	ASSOC_RESULT_AUTH_UNHANDLED_MSG,
	ASSOC_RESULT_AUTH_REFUSED,
	ASSOC_RESULT_INVALID_PARAMETER,

} IEEEtypes_AssocResult_e;

typedef enum {
	/* Failure enumerations must be non-zero since they map to the
	 **  IEEE status field in the assoc response.  The SUCCESS code is left
	 **  in here as a place holder but is never used.  The remaining enums
	 **  cannot be assigned a value of 0.
	 */
	ASSOC_CMD_SUCCESS = 0,

	ASSOC_CMD_FAILURE_ASSOC,
	ASSOC_CMD_FAILURE_AUTH,
	ASSOC_CMD_FAILURE_JOIN
} IEEEtypes_AssocCmdFailurePoint_e;

typedef enum {
	DISASSOC_RESULT_SUCCESS,
	DISASSOC_RESULT_INVALID_PARAMETERS,
	DISASSOC_RESULT_TIMEOUT,
	DISASSOC_RESULT_REFUSED
} IEEEtypes_DisassocResult_e;
/* */
/* The possible results to a request to disassociate */
/* */

typedef enum {
	PWR_MGMT_RESULT_SUCCESS,
	PWR_MGMT_RESULT_INVALID_PARAMETERS,
	PWR_MGMT_RESULT_NOT_SUPPORTED
} IEEEtypes_PwrMgmtResult_e;
/* */
/* The possible results to a request to change the power management mode */
/* */

typedef enum {
	RESET_RESULT_SUCCESS
} IEEEtypes_ResetResult_e;
/* */
/* The possible results to a request to reset */
/* */

typedef enum {
	START_RESULT_SUCCESS,
	START_RESULT_INVALID_PARAMETERS,
	START_RESULT_BSS_ALREADY_STARTED_OR_JOINED,
	START_RESULT_RESET_REQUIRED_BEFORE_START,
	START_RESULT_NOT_SUPPORTED,
	START_RESULT_ACS_ENABLED
} IEEEtypes_StartResult_e;
/* */
/* The possible results to a request to start */
/* */

typedef enum {
	TPCADAPT_RESULT_SUCCESS,
	TPCADAPT_RESULT_INVALID_PARAMETERS,
	TPCADAPT_RESULT_UNSPECIFIED_FAILURE
} IEEEtypes_TPCAdaptResult_e;

typedef enum {
	STATE_IDLE,
	STATE_SCANNING,
	STATE_JOINING,

	STATE_ASSOCIATING,
	STATE_ASSOCIATED,
	STATE_ROAMING,

	STATE_IBSS_ACTIVE,
	STATE_BSS_ACTIVE,
	STATE_TDLS_SETUP_REQ_RCVD,
	STATE_TDLS_SETUP_REQ_SENT,
	STATE_TDLS_SETUP_RSP_SENT,
	STATE_TDLS_ACTIVE,
} IEEEtypes_MacMgmtStates_e;

/* */
/* The possible states the MAC Management Service Task can be in */
/* */

/*---------------------------------------------------------------------------*/
/*           Types Used In IEEE 802.11 MAC Message Data Structures           */
/*---------------------------------------------------------------------------*/
typedef UINT8 IEEEtypes_Len_t;
/* */
/* Length type */
/* */

typedef UINT8 IEEEtypes_Addr_t;
/* */
/* Address type */
/* */

typedef IEEEtypes_Addr_t IEEEtypes_MacAddr_t[IEEEtypes_ADDRESS_SIZE];
/* */
/* MAC address type */
/* */

typedef UINT8 IEEEtypes_DataRate_t;
/* */
/* Type used to specify the supported data rates */
/* */

typedef UINT8 IEEEtypes_SsId_t[IEEEtypes_SSID_SIZE];
/* */
/* SS ID type */
/* */

/*---------------------------------------------------------------------------*/
/*                 IEEE 802.11 MAC Message Data Structures                   */
/*                                                                           */
/* Each IEEE 802.11 MAC message includes a MAC header, a frame body (which   */
/* can be empty), and a frame check sequence field. This section gives the   */
/* structures that used for the MAC message headers and frame bodies that    */
/* can exist in the three types of MAC messages - 1) Control messages,       */
/* 2) Data messages, and 3) Management messages.                             */
/*---------------------------------------------------------------------------*/
typedef MLAN_PACK_START struct {
	UINT16 ProtocolVersion:2;
	UINT16 Type:2;
	UINT16 Subtype:4;
	UINT16 ToDs:1;
	UINT16 FromDs:1;
	UINT16 MoreFrag:1;
	UINT16 Retry:1;
	UINT16 PwrMgmt:1;
	UINT16 MoreData:1;
	UINT16 Protected:1;
	UINT16 Order:1;

} MLAN_PACK_END IEEEtypes_FrameCtl_t;

typedef MLAN_PACK_START struct {
	UINT16 FragNum:4;
	UINT16 SeqNum:12;

} MLAN_PACK_END IEEEtypes_SeqCtl_t;

typedef struct {
	UINT16 FrmBodyLen;
	IEEEtypes_FrameCtl_t FrmCtl;
	UINT16 DurationId;
	IEEEtypes_MacAddr_t Addr1;
	IEEEtypes_MacAddr_t Addr2;
	IEEEtypes_MacAddr_t Addr3;
	IEEEtypes_SeqCtl_t SeqCtl;
	IEEEtypes_MacAddr_t Addr4;

} IEEEtypes_GenHdr_t;

typedef MLAN_PACK_START struct {
	UINT16 FrmBodyLen;
	IEEEtypes_FrameCtl_t FrmCtl;
	UINT16 Duration;
	IEEEtypes_MacAddr_t DestAddr;
	IEEEtypes_MacAddr_t SrcAddr;
	IEEEtypes_MacAddr_t BssId;
	IEEEtypes_SeqCtl_t SeqCtl;
	IEEEtypes_MacAddr_t Rsrvd;

} MLAN_PACK_END IEEEtypes_MgmtHdr_t;

typedef struct {
	IEEEtypes_GenHdr_t Hdr;
	UINT8 FrmBody[IEEEtypes_MAX_DATA_BODY_LEN];
	UINT32 FCS;

} IEEEtypes_DataFrame_t;

typedef struct {
	UINT8 UserPriority:4;
	UINT8 Management:1;
	UINT8 Reserved:3;

} IEEEtypes_NonceFlags_t;

typedef MLAN_PACK_START struct {
	UINT8 PN0;
	UINT8 PN1;
	UINT8 Reserved1;
	UINT8 Reserved2:5;
	UINT8 ExtIV:1;
	UINT8 KeyId:2;
	UINT8 PN2;
	UINT8 PN3;
	UINT8 PN4;
	UINT8 PN5;

} MLAN_PACK_END IEEEtypes_CcmpHeader_t;

typedef MLAN_PACK_START struct {
	UINT8 TSC1;
	UINT8 WepSeed;
	UINT8 TSC0;
	UINT8 Reserved:5;
	UINT8 ExtIV:1;
	UINT8 KeyId:2;
	UINT8 TSC2;
	UINT8 TSC3;
	UINT8 TSC4;
	UINT8 TSC5;

} MLAN_PACK_END IEEEtypes_TkipHeader_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_FrameCtl_t frameCtl;
	IEEEtypes_MacAddr_t addr1;
	IEEEtypes_MacAddr_t addr2;
	IEEEtypes_MacAddr_t addr3;

} MLAN_PACK_END IEEEtypes_BIP_AAD_t;

/************************************************************************/
/*                      Control Frame Types                             */
/************************************************************************/
typedef MLAN_PACK_START struct {
	UINT16 FrmBodyLen;
	IEEEtypes_FrameCtl_t FrmCtl;
	UINT16 DurationId;
	IEEEtypes_MacAddr_t DestAddr;
	IEEEtypes_MacAddr_t SrcAddr;
	UINT8 Reserved[14];	/* Header MAC HW is 32 bytes */

} MLAN_PACK_END IEEEtypes_CtlHdr_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_CtlHdr_t Hdr;
	UINT32 FCS;

} MLAN_PACK_END IEEEtypes_PsPoll_t;

typedef MLAN_PACK_START struct {
	UINT16 BARAckPolicy:1;
	UINT16 Multi_TID:1;
	UINT16 CompressedBitmap:1;
	UINT16 Reserved:9;
	UINT16 TID:4;

} MLAN_PACK_END IEEEtypes_BARCtl_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_CtlHdr_t Hdr;

	IEEEtypes_BARCtl_t BARCtl;
	IEEEtypes_SeqCtl_t SeqCtl;
	UINT32 FCS;

} MLAN_PACK_END IEEEtypes_BlockAckReq_t;

//NDPA frame components
typedef MLAN_PACK_START struct {
	UINT16 AID:12;
	UINT16 FbType:1;
	UINT16 NcIndex:3;

} MLAN_PACK_END IEEEtypes_StaInfo_t;

typedef MLAN_PACK_START struct {
	UINT8 Rsvd:2;		//[0:1] Reserved
	UINT8 SoundingSeq:6;	//[2:7]Seq no.
	IEEEtypes_StaInfo_t StaInfo;	//Currently only 1 sta_info support

} MLAN_PACK_END IEEEtypes_NDPAFrameBody_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_CtlHdr_t Hdr;
	IEEEtypes_NDPAFrameBody_t FrameBody;
	UINT32 FCS;

} MLAN_PACK_END IEEEtypes_NDPAFrame_t;
/*-------------------------------------------------*/
/* Management Frame Body Components - Fixed Fields */
/*-------------------------------------------------*/
typedef UINT16 IEEEtypes_AId_t;
/* */
/* Association ID assigned by an AP during the association process */
/* */

typedef UINT16 IEEEtypes_AuthAlg_t;
/* */
/* Number indicating the authentication algorithm used (it can take */
/* on the values given by IEEEtypes_AuthType_e): */
/*    0 = Open system */
/*    1 = Shared key */
/*    All other values reserved */
/* */

typedef UINT16 IEEEtypes_AuthTransSeq_t;
/* */
/* Authentication transaction sequence number that indicates the current */
/* state of progress through a multistep transaction */
/* */

typedef UINT16 IEEEtypes_BcnInterval_t;
/* */
/* Beacon interval that represents the number of time units between */
/* target beacon transmission times */
/* */

typedef UINT8 IEEEtypes_DtimPeriod_t;
/*
 * Interval that represents the number of time units between DTIMs.
 */

typedef MLAN_PACK_START struct {
	UINT16 Ess:1;
	UINT16 Ibss:1;
	UINT16 CfPollable:1;
	UINT16 CfPollRqst:1;
	UINT16 Privacy:1;
	UINT16 ShortPreamble:1;
	UINT16 Pbcc:1;
	UINT16 ChanAgility:1;
	UINT16 SpectrumMgmt:1;
	UINT16 Qos:1;
	UINT16 ShortSlotTime:1;
	UINT16 APSD:1;
	UINT16 RadioMeasurement:1;
	UINT16 DsssOfdm:1;
	UINT16 DelayedBlockAck:1;
	UINT16 ImmediateBlockAck:1;
} MLAN_PACK_END IEEEtypes_CapInfo_t;

typedef UINT16 IEEEtypes_ListenInterval_t;
/* */
/* Listen interval to indicate to an AP how often a STA wakes to listen */
/* to beacon management frames */
/* */

typedef UINT16 IEEEtypes_ReasonCode_t;
/* */
/* Reason code to indicate the reason that an unsolicited notification */
/* management frame of type Disassociation or Deauthentication was */
/* generated */
/* */

typedef UINT16 IEEEtypes_StatusCode_t;
/* */
/* Status code used in a response management frame to indicate the */
/* success or failure of a requested operation */
/* */

typedef UINT8 IEEEtypes_TimeStamp_t[IEEEtypes_TIME_STAMP_SIZE];

/*-------------------------------------------------------*/
/* Management Frame Body Components - Information Fields */
/*-------------------------------------------------------*/

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;
} MLAN_PACK_END IEEEtypes_InfoElementHdr_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	UINT8 Data[255];
} MLAN_PACK_END IEEEtypes_IE_Param_t;

/*
** SSID element that idicates the identity of an ESS or IBSS
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;
	IEEEtypes_SsId_t SsId;
} MLAN_PACK_END IEEEtypes_SsIdElement_t;

/*
** Supported rates element that specifies the rates in the operational
** rate set in the MLME join request and the MLME start request
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;
	IEEEtypes_DataRate_t Rates[IEEEtypes_MAX_DATA_RATES];
} MLAN_PACK_END IEEEtypes_SuppRatesElement_t;

/*
** FH parameter set that conatins the set of parameters necessary to
** allow sychronization for stations using a frequency hopping PHY
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;
	UINT16 DwellTime;
	UINT8 HopSet;
	UINT8 HopPattern;
	UINT8 HopIndex;
} MLAN_PACK_END IEEEtypes_FhParamElement_t;

/*
** DS parameter set that contains information to allow channel number
** identification for stations using a direct sequence spread spectrum PHY
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;
	UINT8 CurrentChan;
} MLAN_PACK_END IEEEtypes_DsParamElement_t;

/*
** CF parameter set that contains the set of parameters necessary to
** support the PCF
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;
	UINT8 CfpCnt;
	UINT8 CfpPeriod;
	UINT16 CfpMaxDuration;
	UINT16 CfpDurationRemaining;
} MLAN_PACK_END IEEEtypes_CfParamElement_t;

/* Since uAP is the only one that holds the TIM statically, we'll
** define a max size for the PVB that accomodates a max AID of 32
** used by the uAP.  Size is 5 to account for broadcast.
*/
#define MAX_TIM_ELEMENT_PVB_LENGTH 5
/*
** TIM, which contains:
** 1) DTIM count - how many beacons (including the current beacon
**    frame) appear before the next DTIM; a count of 0 indicates the
**    current TIM is the DTIM
**
** 2) DTIM period - indicates the number of beacon intervals between
**    successive DTIMs
**
** 3) Bitmap control - contains the traffic indicator bit associated
**    with association ID 0 - this is set to 1 for TIM elements with a
**    a value of 0 in the DTIM count field when one or more broadcast
**    or multicast frames are buffered at the AP. The remaining bits
**    of the field form the bitmap offset
**
** 4) Partial virtual bitmap - indicates which stations have messages
**    buffered at the AP, for which the AP is prepared to deliver at
*    the time the beacon frame is transmitted
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;
	UINT8 DtimCnt;
	UINT8 DtimPeriod;
	UINT8 BitmapCtl;
	UINT8 PartialVirtualBitmap[MAX_TIM_ELEMENT_PVB_LENGTH];
} MLAN_PACK_END IEEEtypes_TimElement_t;

/*
** IBSS parameters necessary to support an IBSS
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;
	UINT16 AtimWindow;
} MLAN_PACK_END IEEEtypes_IbssParamElement_t;

/*
** The challenge text used in authentication exchanges
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;
	UINT8 Text[IEEEtypes_CHALLENGE_TEXT_SIZE];
} MLAN_PACK_END IEEEtypes_ChallengeText_t;

typedef MLAN_PACK_START struct {
	UINT8 ExtCapability;
	UINT32 Capability;

	UINT8 SsIdLength;
	IEEEtypes_SsId_t SsId;

} MLAN_PACK_END IEEEtypes_SsIdL_HidSsId_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	UINT8 OuiType[4];	/* 00:50:f2:05 */

	UINT8 PrimarySsIdExtCapability;

	/* Start optional fields */

	UINT8 SsIdCount;

	/* SsIdCount # of hidden SSIDs, not a fixed size substructure */
	IEEEtypes_SsIdL_HidSsId_t hidSsid[1];

} MLAN_PACK_END IEEEtypes_SsIdLElement_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;
	UINT8 OuiType[4];
	UINT8 Data[1];

} MLAN_PACK_END IEEEtypes_WPS_DataElement_t;

/* This structure is a member of BssConfig_t which is referenced by ROMed code.
** Any increase in the size of this structure will cause the subsequent elements
** of BssConfig_t to move forward, in turn affecting the ROM code. Therefore,
** any changes to this structure should be done taking into account its effect
** on BssConfig_t and ROM code.
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	UINT8 BSS_CoexistSupport:1;	/* bit 0 */
	UINT8 Reserved1:1;	/* bit 1 */
	UINT8 ExtChanSwitching:1;	/* bit 2 */
	UINT8 RejectUnadmFrame:1;	/* bit 3 */
	UINT8 PSMP_Capable:1;	/* bit 4 */
	UINT8 Reserved5:1;	/* bit 5 */
	UINT8 SPSMP_Support:1;	/* bit 6 */
	UINT8 Event:1;		/* bit 7  */
	UINT8 Diagnostics:1;	/* bit 8  */
	UINT8 MulticastDiagnostics:1;	/* bit 9  */
	UINT8 LocationTracking:1;	/* bit 10 */
	UINT8 FMS:1;		/* bit 11 */
	UINT8 ProxyARPService:1;	/* bit 12 */
	UINT8 CollocatedIntf:1;	/* bit 13 */
	UINT8 CivicLocation:1;	/* bit 14 */
	UINT8 GeospatialLocation:1;	/* bit 15 */
	UINT8 TFS:1;		/* bit 16 */
	UINT8 WNM_Sleep:1;	/* bit 17 */
	UINT8 TIM_Broadcast:1;	/* bit 18 */
	UINT8 BSS_Transition:1;	/* bit 19 */
	UINT8 QoSTrafficCap:1;	/* bit 20 */
	UINT8 AC_StationCount:1;	/* bit 21 */
	UINT8 MultipleBSSID:1;	/* bit 22 */
	UINT8 TimingMeasurement:1;	/* bit 23 */
	UINT8 ChannelUsage:1;	/* bit 24 */
	UINT8 SSID_List:1;	/* bit 25 */
	UINT8 DMS:1;		/* bit 26 */
	UINT8 UTC:1;		/* bit 27 */
	UINT8 TDLSPeerUAPSDSupport:1;	/* bit 28 */
	UINT8 TDLSPeerPSMSupport:1;	/* bit 29 */
	UINT8 TDLSChannelSwitching:1;	/* bit 30 */
	UINT8 Reserved31:1;
	UINT8 Reserved32_36:5;
	UINT8 TDLSSupport:1;	/* bit 37 */
	UINT8 TDLSProhibited:1;	/* bit 38 */
	UINT8 TDLSChlSwitchProhib:1;	/* bit 39 */
	UINT8 Reserved40_47:8;
	UINT8 Reserved48_55:8;
	UINT8 Reserved56_60:5;
	UINT8 TDLSWiderBandSupport:1;
	UINT8 OpModeNotification:1;	/* bit 62 */
	UINT8 Reserved63:1;	/* bit 63 */
/* This structure is a member of BssConfig_t which is referenced by ROMed code.
** Any increase in the size of this structure will cause the subsequent elements
** of BssConfig_t to move forward, in turn affecting the ROM code. Therefore,
** any changes to this structure should be done taking into account its effect
** on BssConfig_t and ROM code.
*/
} MLAN_PACK_END IEEEtypes_ExtCapability_t;

/*
** The HT Capability Element
*/

typedef enum {
	STATIC_SM_PS,
	DYNAMIC_SM_PS,
	RESERVED_SM_PS,
	DISABLE_SM_PS
} IEEEtypes_HtCap_SMPS_e;

typedef MLAN_PACK_START struct {
	UINT16 LdpcCoding:1;
	UINT16 SuppChanWidth:1;
	UINT16 MIMOPowerSave:2;
	UINT16 GFPreamble:1;
	UINT16 ShortGI20MHZ:1;
	UINT16 ShortGI40MHZ:1;
	UINT16 TxSTBC:1;
	UINT16 RxSTBC:2;
	UINT16 DelayedBA:1;
	UINT16 MaximalAMSDUSize:1;
	UINT16 DsssCck40MHzMode:1;
	UINT16 Psmp:1;
	UINT16 FortyMHzIntolerant:1;
	UINT16 L_SIG_TXOP_Protection:1;
} MLAN_PACK_END IEEEtypes_HT_Cap_Info_t;

typedef MLAN_PACK_START struct {
	UINT8 MaxRxAMpduFactor:2;
	UINT8 MpduDensity:3;
	UINT8 Reserved_5_7:3;
} MLAN_PACK_END IEEEtypes_HT_Mac_Params_t;

typedef MLAN_PACK_START struct {
	UINT16 Pco:1;
	UINT16 TransitionTime:2;
	UINT16 Reserved:5;

	UINT16 McsFeedback:2;
	UINT16 HtcSupport:1;
	UINT16 RdResponder:1;
	UINT16 Reserved2:4;
} MLAN_PACK_END IEEEtypes_HT_Ext_Cap_t;

typedef MLAN_PACK_START struct {
	UINT32 TxBFCapable:1;	/* B0 */
	UINT32 RxStaggeredSounding:1;
	UINT32 TxStaggeredSounding:1;
	UINT32 RxNDPCap:1;
	UINT32 TxNDPCap:1;
	UINT32 ImplicitTxBF:1;
	UINT32 Calibration:2;
	UINT32 ExplCSITxBF:1;	/* B8 */

	UINT32 ExplUcompSteerMatrix:1;
	UINT32 ExplCompSteerMatrix:1;
	UINT32 ExplBFCSIFeedback:2;
	UINT32 ExplUcompSteerFeedback:2;
	UINT32 ExplCompSteerMatrixFeedback:2;
	UINT32 MinimalGrouping:2;
	UINT32 CSINumMFAntennae:2;	/* B20 */

	UINT32 UcompSteerMatrixBFAntennae:2;
	UINT32 CompSteerMatrixBFAntennae:2;
	UINT32 CSIMaxMaxBeamSupported:2;
	UINT32 ChanEstCapability:2;
	UINT32 Reserved:3;	/* B31 */

} MLAN_PACK_END IEEEtypes_HT_TXBF_Cap_t;

typedef MLAN_PACK_START struct {
	UINT8 ASCapable:1;
	UINT8 ExplCSITxASCapable:1;
	UINT8 IndicesFeedbackTxAS:1;
	UINT8 ExplicitCSIFeedback:1;
	UINT8 AntIndicesFeedback:1;
	UINT8 RxASCapable:1;
	UINT8 TxSoundingPPDUs:1;
	UINT8 Reserved:1;

} MLAN_PACK_END IEEEtypes_HT_AS_Cap_t;

typedef enum {
	HTC_NO_FEEDBACK = 0,
	HTC_CSI,
	HTC_UCOMP_BF,
	HTC_COMP_BF
} IEEEtypes_HTC_CSI_Types_e;

typedef MLAN_PACK_START struct {
	UINT16 Nc:2;
	UINT16 Nr:2;
	UINT16 ChanW:1;
	UINT16 Ng:2;
	UINT16 Coeff_size:2;
	UINT16 Codebook_info:2;
	UINT16 Re_segments:2;
	UINT16 Reserved:2;

	UINT32 SndTimestamp;

} MLAN_PACK_END IEEEtypes_MIMOCtrl_t;

typedef MLAN_PACK_START struct {
	UINT16 Reserved:1;
	UINT16 TRQ:1;
	UINT16 MAI:4;
	UINT16 MFSI:3;
	UINT16 MFB_ASELC:7;

} MLAN_PACK_END IEEEtypes_LinkAdaptCtrl_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_LinkAdaptCtrl_t LA_Ctrl;
	UINT16 CalibPos:2;
	UINT16 CalibSeq:2;
	UINT16 Reserved:2;
	UINT16 CSI:2;
	UINT16 NDPAnnounce:1;
	UINT16 Reserved2:5;
	UINT16 ACConst:1;
	UINT16 RDG_MorePPDU:1;

} MLAN_PACK_END IEEEtypes_HTCtrl_t;

#define HTC_NDP_ANNOUNCE    BIT24

#define IEEEtypes_MCS_BITMAP_SIZE       16
typedef UINT8 IEEEtypes_Supported_MCS_Bitmap_t[IEEEtypes_MCS_BITMAP_SIZE];

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	IEEEtypes_HT_Cap_Info_t HtCapInfo;
	IEEEtypes_HT_Mac_Params_t HtMacParams;
	IEEEtypes_Supported_MCS_Bitmap_t SupportedMcsBitmap;
	IEEEtypes_HT_Ext_Cap_t ExtHTCaps;
	IEEEtypes_HT_TXBF_Cap_t TxBFCaps;
	IEEEtypes_HT_AS_Cap_t ASCaps;

} MLAN_PACK_END IEEEtypes_HT_Capability_t;

/*
** The HT Information Element
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	UINT8 PrimaryChan;
    /*-----------------------------------------------*/
	UINT8 SecChanOffset:2;
	UINT8 ChanWidth:1;
	UINT8 RifsMode:1;
	UINT8 CtrlLedAccessOnly:1;
	UINT8 SrvIntrvlGran:3;
    /*-----------------------------------------------*/
	UINT16 HtProtection:2;
	UINT16 NonGfHtStaPresent:1;
	UINT16 Reserved1:1;
	UINT16 ObssNonHtStaPresent:1;
	UINT16 Reserved2:11;
    /*-----------------------------------------------*/
	UINT16 Reserved3:6;
	UINT16 DualBeacon:1;
	UINT16 DualCtsProtection:1;
	UINT16 StbcBeacon:1;
	UINT16 L_SIGTxopProtectionAllowed:1;
	UINT16 PcoActive:1;
	UINT16 PcoPhase:1;
	UINT16 Reserved4:4;
    /*-----------------------------------------------*/
	IEEEtypes_Supported_MCS_Bitmap_t BasicMcsSetBitmap;

} MLAN_PACK_END IEEEtypes_HT_Information_t;

/* secondary channel offset        */
/* 20/40 BSS Coexistence           */
typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	UINT8 InfoResuest:1;
	UINT8 FortyMHzIntolerant:1;
	UINT8 Width20MHzRequest:1;
	UINT8 Reserved:5;

} MLAN_PACK_END IEEEtypes_20N40_BSS_Coexist_t;

/* overlapping BSS Scan Parameters */
typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	UINT16 OBSSScanPassiveDwell;
	UINT16 OBSSScanActiveDwel;
	UINT16 BSSChanWidthTriggerScanInt;
	UINT16 OBSSScanPassiveTotal;
	UINT16 OBSSScanActiveTotal;
	UINT16 BSSWidthChanTransDelay;
	UINT16 OBSSScanActiveThreshold;

} MLAN_PACK_END IEEEtypes_OBSS_ScanParam_t;

typedef MLAN_PACK_START struct {
	UINT8 htSupported;
	IEEEtypes_HT_Capability_t htCap;
	IEEEtypes_HT_Information_t htInfo;
	UINT8 secChanOffset;
	IEEEtypes_20N40_BSS_Coexist_t coexist;
	IEEEtypes_OBSS_ScanParam_t scanParam;

} MLAN_PACK_END HtEntry_t;

#define DOT11AC_VHTCAP_MAX_MPDU_LEN_0     3895
#define DOT11AC_VHTCAP_MAX_MPDU_LEN_1     7991
#define DOT11AC_VHTCAP_MAX_MPDU_LEN_2     11454
#define DOT11AC_VHTCAP_MAX_MPDU_LEN_3     3895	//reserved value

    /* IE definitions based on draft 1.4.
     */
typedef MLAN_PACK_START struct {
	UINT32 MaxMpduLen:2;
	UINT32 SuppChanWidthSet:2;
	UINT32 RxLDPC:1;
	UINT32 ShortGI_80:1;
	UINT32 ShortGI_160:1;
	UINT32 TxSTBC:1;
    /*------------------------*/
	UINT32 RxSTBC:3;
	UINT32 SUBFerCap:1;
	UINT32 SUBFeeCap:1;
	UINT32 NumBFerAnt:3;
    /*------------------------*/
	UINT32 NumSoundingDim:3;
	UINT32 MUBFerCap:1;
	UINT32 MUBFeeCap:1;
	UINT32 VHTTxOPPs:1;
	UINT32 HTC_VHT_Cap:1;
	UINT32 MaxAMPDULenExp:3;
	UINT32 VHTLinkAdaptCap:2;
	UINT32 Reserved:4;

} MLAN_PACK_END IEEEtypes_VHT_Cap_Info_t;

typedef MLAN_PACK_START struct {
	UINT16 RxMCSMap;
	UINT16 RxHighestDataRate:13;
	UINT16 Reserved1:3;

	UINT16 TxMCSMap;
	UINT16 TxHighestDataRate:13;
	UINT16 Reserved2:3;

} MLAN_PACK_END IEEEtypes_VHT_Supp_MCS_Set_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	IEEEtypes_VHT_Cap_Info_t VhtCap;
	IEEEtypes_VHT_Supp_MCS_Set_t VhtSuppMcsSet;

} MLAN_PACK_END IEEEtypes_VHT_Capability_t;

typedef MLAN_PACK_START struct {
	UINT8 ChanWidth;
	UINT8 ChanCenterFreq1;
	UINT8 ChanCenterFreq2;

} MLAN_PACK_END IEEEtypes_VHT_Op_Info_t;

/*
** The VHT Operation Element
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	IEEEtypes_VHT_Op_Info_t VhtOpInfo;
	UINT16 VhtBasicMcsSet;

} MLAN_PACK_END IEEEtypes_VHT_Operation_t;

typedef MLAN_PACK_START struct {
	UINT8 chanWidth:2;
	UINT8 reserved:2;
	UINT8 rxNss:3;
	UINT8 rxNssType:1;

} MLAN_PACK_END IEEEtypes_VHT_OpMode_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	IEEEtypes_VHT_OpMode_t VhtOpMode;
} MLAN_PACK_END IEEEtypes_VHT_OpModeNotification_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;
    /** WideBW has same structure as VHT_OP_INFO*/
	IEEEtypes_VHT_Op_Info_t wideBwCs;
} MLAN_PACK_END IEEEtypes_WIde_BW_CS_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;
	IEEEtypes_AId_t Aid;
} MLAN_PACK_END IEEEtypes_AIDElement_t;

/*
*****************************************************************************
**
**
**                     802.11k RRM definitions
**
**
*****************************************************************************
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	/* First byte */
	UINT8 LinkMeas:1;
	UINT8 NborRpt:1;
	UINT8 ParallelMeas:1;
	UINT8 RepeatMeas:1;
	UINT8 BcnPassiveMeas:1;
	UINT8 BcnActiveMeas:1;
	UINT8 BcnTableMeas:1;
	UINT8 BcnMeasRptCond:1;

	/* Second byte */
	UINT8 FrameMeas:1;
	UINT8 ChanLoadMeas:1;
	UINT8 NoiseHistMeas:1;
	UINT8 StatsMeas:1;
	UINT8 LciMeas:1;
	UINT8 LciAzimuth:1;
	UINT8 TxStreamMeas:1;
	UINT8 TrigTxStreamMeas:1;

	/* Third byte */
	UINT8 ApChanRpt:1;
	UINT8 RrmMib:1;
	UINT8 OpChanMaxMeas:3;
	UINT8 NonOpChanMaxMeas:3;

	/* Fourth byte */
	UINT8 MeasPilot:3;
	UINT8 MeasPilotTxInfo:1;
	UINT8 NborRptTsfOffset:1;
	UINT8 RcpiMeas:1;
	UINT8 RsniMeas:1;
	UINT8 BssAvgAccessDelay:1;

	/* Fifth byte */
	UINT8 BssAvailAdmCap:1;
	UINT8 AntennaInfo:1;
	UINT8 Reserved:6;

} MLAN_PACK_END IEEEtypes_RrmEnabledCapabilities_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	UINT8 RegulatoryClass;

	UINT8 ChannelList[1];

} MLAN_PACK_END IEEEtypes_ApChanRptElement_t;

typedef enum {
	RPT_DTL_NO_FIX_OR_IE = 0,
	RPT_DTL_ALL_FIX_AND_REQ_IES = 1,
	RPT_DTL_ALL_FIX_AND_ALL_IES = 2,

} IEEEtypes_ReportDetailLevel_e;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	MLAN_PACK_START IEEEtypes_ReportDetailLevel_e MLAN_PACK_END
		RptDetailLevel;

} MLAN_PACK_END IEEEtypes_ReportingDetailElement_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	IEEEtypes_ElementId_e IeList[1];

} MLAN_PACK_END IEEEtypes_RequestElement_t;

/*
*****************************************************************************
**
**
**               802.11d and 802.11j Country IE definitions
**
**
*****************************************************************************
*/

/** Regulatory Triplet component in the country IE */
typedef MLAN_PACK_START struct {
	UINT8 RegulatoryExtensionId;
	UINT8 RegulatoryClass;
	UINT8 CoverageClass;

} MLAN_PACK_END IEEEtypes_RegulatoryTriplet_t;

/** Subband Triplet component in the country IE */
typedef MLAN_PACK_START struct {
	UINT8 FirstChan;
	UINT8 NumChans;
	UINT8 MaxTxPower;

} MLAN_PACK_END IEEEtypes_SubbandTriplet_t;

/* Country Info Triplet union comprised of subband and potentially regulatory
 *   triplets
 */
typedef MLAN_PACK_START union {
	UINT8 TripletIdentifier;
	IEEEtypes_SubbandTriplet_t Subband;
	IEEEtypes_RegulatoryTriplet_t Regulatory;

} MLAN_PACK_END IEEEtypes_CountryInfoTriplet_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;
	UINT8 CountryCode[IEEEtypes_COUNTRY_CODE_SIZE];
	IEEEtypes_CountryInfoTriplet_t Triplets[IEEEtypes_COUNTRY_MAX_TRIPLETS];

} MLAN_PACK_END IEEEtypes_CountryInfoElement_t;

/*
*****************************************************************************
**
**
**                      802.11h TPC definitions
**
**
*****************************************************************************
*/

/*
** Power Constraint IE - for 802.11h TPC. Specifies a Local
**  Power Constraint in a channel.
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;
	UINT8 localConstraint;
} MLAN_PACK_END IEEEtypes_PowerConstraintElement_t;

/*
** Power Capability IE - for 802.11h TPC. Specifies the
**   min and max power the station is capable of transmitting with.
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;
	UINT8 minTxPwr;
	UINT8 maxTxPwr;
} MLAN_PACK_END IEEEtypes_PowerCapabilityElement_t;

/*
** 802.11h TPC Request IE - used for requesting a peer station
**   to send Tx power and Link Margin.
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;
} MLAN_PACK_END IEEEtypes_TPCRequestElement_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;
	UINT8 TxPwr;
	UINT8 LinkMargin;
} MLAN_PACK_END IEEEtypes_TPCReportElement_t;

#define WIFI_TPCRPT_OUI_SUBTYPE  0x08

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	UINT8 OuiType[4];	/* 00:50:f2:08 */
	UINT8 OuiSubType;

	UINT8 TxPwr;
	UINT8 LinkMargin;

} MLAN_PACK_END IEEEtypes_WiFi_TPCReportElement_t;

/*
*****************************************************************************
**
**
**                     802.11h DFS definitions
**
**
*****************************************************************************
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	UINT8 ChannelSwitchMode;
	UINT8 ChannelNumber;
	UINT8 ChannelSwitchCount;
} MLAN_PACK_END IEEEtypes_ChannelSwitchElement_t;

typedef MLAN_PACK_START struct {
	UINT8 BSS:1;
	UINT8 OFDM_Preamble:1;
	UINT8 Unidentified:1;
	UINT8 Radar:1;
	UINT8 Unmeasured:1;
	UINT8 Reserved:3;
} MLAN_PACK_END IEEEtypes_DFS_Map_t;

typedef MLAN_PACK_START struct {
	UINT8 ChannelNumber;
	IEEEtypes_DFS_Map_t DFS_Map;
} MLAN_PACK_END IEEEtypes_ChannelMap_t;

#define MAX_NUMBER_OF_DFS_CHANNELS 25
typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	IEEEtypes_MacAddr_t DFS_Owner;
	UINT8 DFS_Recovery_Interval;

	/* For the channels in .11a */
	IEEEtypes_ChannelMap_t Channel_Map[MAX_NUMBER_OF_DFS_CHANNELS];
} MLAN_PACK_END IEEEtypes_IbssDfsElement_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	UINT8 QuietCount;
	UINT8 QuietPeriod;
	UINT16 QuietDuration;
	UINT16 QuietOffset;
} MLAN_PACK_END IEEEtypes_QuietElement_t;

typedef MLAN_PACK_START struct {
	UINT8 FirstChannelNumber;
	UINT8 NumberOfChannels;
} MLAN_PACK_END ChannelsTuple_t;

#define MAX_CHANNEL_TUPLES 20
typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	ChannelsTuple_t ChannelTuple[MAX_CHANNEL_TUPLES];
} MLAN_PACK_END IEEEtypes_SupportedChannelsElement_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	UINT8 currRegClass;
	UINT8 suppRegClass;
} MLAN_PACK_END IEEEtypes_SupportedRegClasses_t;

/*
*****************************************************************************
**
**
**                        802.11y definitions
**
**
*****************************************************************************
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	UINT8 ChannelSwitchMode;
	UINT8 RegClass;
	UINT8 ChannelNumber;
	UINT8 ChannelSwitchCount;
} MLAN_PACK_END IEEEtypes_ExtChannelSwitchElement_t;

/*
*****************************************************************************
**
**
**                        802.11e definitions
**
**
*****************************************************************************
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	UINT16 StaCount;
	UINT8 ChnlUtil;
	UINT16 AdmCap;
} MLAN_PACK_END IEEEtypes_BSSLoadElement_t;

/*
*****************************************************************************
**
**
**                     802.11i and WPA definitions
**
**
*****************************************************************************
*/
typedef enum {
	IEEEtypes_RSN_AUTH_KEY_SUITE_RSVD = 0,
	IEEEtypes_RSN_AUTH_KEY_SUITE_8021X = 1,
	IEEEtypes_RSN_AUTH_KEY_SUITE_PSK = 2,
	IEEEtypes_AKM_SUITE_FT_1X = 3,
	IEEEtypes_AKM_SUITE_FT_PSK = 4,
	IEEEtypes_AKM_SUITE_1X_SHA256 = 5,
	IEEEtypes_AKM_SUITE_PSK_SHA256 = 6
} IEEEtypes_RSN_Auth_Key_Suite;

/* Cipher Suite Selector */
typedef enum {
	IEEEtypes_RSN_CIPHER_SUITE_NONE = 0,
	IEEEtypes_RSN_CIPHER_SUITE_WEP40,
	IEEEtypes_RSN_CIPHER_SUITE_TKIP,
	IEEEtypes_RSN_CIPHER_SUITE_WRAP,
	IEEEtypes_RSN_CIPHER_SUITE_CCMP,
	IEEEtypes_RSN_CIPHER_SUITE_WEP104
} IEEEtypes_RSN_Cipher_Suite;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	UINT8 OuiType[4];	/* 00:50:f2:01 */
	UINT16 Ver;
	UINT8 GrpKeyCipher[4];
	UINT16 PwsKeyCnt;
	UINT8 PwsKeyCipherList[4];
	UINT16 AuthKeyCnt;
	UINT8 AuthKeyList[4];
} MLAN_PACK_END IEEEtypes_WPAElement_t;

typedef MLAN_PACK_START struct {
	UINT8 PreAuth:1;	/* B0 */
	UINT8 NoPairwise:1;
	UINT8 PtksaReplayCtr:2;
	UINT8 GtksaReplayCtr:2;
	UINT8 MFPR:1;
	UINT8 MFPC:1;

	UINT8 Reserved_8:1;	/* B8 */
	UINT8 PeerkeyEnabled:1;
	UINT8 SppAmsduCap:1;
	UINT8 SppAmsduReq:1;
	UINT8 PBAC:1;
	UINT8 Reserved_13_15:3;
} MLAN_PACK_END IEEEtypes_RSNCapability_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	/* All elements after Ver field are optional per the spec.
	 **
	 ** - AuthKeyList and PwsKeyCipherList can have multiple elements so
	 **   static parsing of this structure is not possible.
	 **
	 ** - RsnCap, PMKIDCnt/List, and GrpMgmtCipher are often not included
	 **
	 ** - If any optional element is included, all preceding elements must
	 **   also be included.  Once an optional element is not inserted, the
	 **   IE construction ends. (i.e. RsnCap must be included if PMKIDCnt/List
	 **   is needed).
	 */

	UINT16 Ver;
	UINT8 GrpKeyCipher[4];
	UINT16 PwsKeyCnt;
	UINT8 PwsKeyCipherList[4];
	UINT16 AuthKeyCnt;
	UINT8 AuthKeyList[4];

	IEEEtypes_RSNCapability_t RsnCap;

	UINT16 PMKIDCnt;
	UINT8 PMKIDList[16];

	UINT8 GrpMgmtCipher[4];
} MLAN_PACK_END IEEEtypes_RSNElement_t;

/*
*****************************************************************************
**
**
**                        802.11r definitions
**
**
*****************************************************************************
*/

typedef MLAN_PACK_START struct {
	UINT8 FtOverBss:1;
	UINT8 FtOverAir:1;

	UINT8 Reserved:6;

} MLAN_PACK_END IEEEtypes_FT_CapabilityPolicy_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	UINT16 MobilityDomainId;

	IEEEtypes_FT_CapabilityPolicy_t FtCapPol;

} MLAN_PACK_END IEEEtypes_MobilityDomainElement_t;

typedef MLAN_PACK_START struct {
	UINT8 KeyId:2;

	UINT8 Reserved1:6;
	UINT8 Reserved2:8;

} MLAN_PACK_END IEEEtypes_GtkKeyInfo_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	IEEEtypes_GtkKeyInfo_t KeyInfo;
	UINT8 KeyLen;
	UINT8 RSC[8];

	UINT8 Key[32];		/* Variable length from 5 to 32 */

} IEEEtypes_GtkElement_t;

typedef MLAN_PACK_START struct {
	UINT8 Reserved;
	UINT8 InfoElementCount;

} MLAN_PACK_END IEEEtypes_FT_MICControl_t;

#define FTIE_MIC_LEN  16
typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	IEEEtypes_FT_MICControl_t MicControl;

	UINT8 MIC[FTIE_MIC_LEN];
	UINT8 ANonce[32];
	UINT8 SNonce[32];

	UINT8 SubElem[1];

} MLAN_PACK_END IEEEtypes_FastBssTransElement_t;

typedef MLAN_PACK_START enum {
	TI_TYPE_RESERVED = 0,
	TI_TYPE_REASSOC_DEADLINE_TUS = 1,
	TI_TYPE_KEY_LIFETIME_SECS = 2,
	TI_TYPE_ASSOCIATION_COMEBACK_TIME = 3,

} IEEEtypes_TimeoutInterval_e;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	IEEEtypes_TimeoutInterval_e TimeoutIntervalType;
	UINT32 TimeoutInterval;

} MLAN_PACK_END IEEEtypes_TimeoutIntervalElement_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	UINT8 Identifier;
	UINT8 DescCount;

	IEEEtypes_StatusCode_t StatusCode;

} MLAN_PACK_END IEEEtypes_RICDataElement_t;

typedef MLAN_PACK_START enum {
	RIC_RESOURCE_BLOCK_ACK = 1,

} MLAN_PACK_END IEEEtypes_RICResource_e;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	IEEEtypes_RICResource_e ResourceType;

	UINT8 Data[1];

} MLAN_PACK_END IEEEtypes_RICDescElement_t;

/*
*****************************************************************************
**
**
**                        802.11w definitions
**
**
*****************************************************************************
*/
#define MMIE_MIC_LEN  8
typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	UINT8 KeyId[2];
	UINT8 IPN[6];

	UINT8 MIC[MMIE_MIC_LEN];

} MLAN_PACK_END IEEEtypes_ManagementMICElement_t;

/*
*****************************************************************************
**
**
**                        Management Frame Bodies
**
**
*****************************************************************************
*/

/*
** The parameter set relevant to the PHY
*/
typedef MLAN_PACK_START union {
	IEEEtypes_FhParamElement_t FhParamSet;
	IEEEtypes_DsParamElement_t DsParamSet;

} MLAN_PACK_END IEEEtypes_PhyParamElement_t;

/*
** Service set parameters - for a BSS supporting, PCF, the
** CF parameter set is used; for an independent BSS, the IBSS
** parameter set is used.
*/
typedef MLAN_PACK_START union {
	IEEEtypes_CfParamElement_t CfParamSet;
	IEEEtypes_IbssParamElement_t IbssParamSet;
} MLAN_PACK_END IEEEtypes_SsParamElement_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;
	IEEEtypes_DataRate_t Rates[IEEEtypes_MAX_DATA_RATES];
} MLAN_PACK_END IEEEtypes_ExtSuppRatesElement_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	MLAN_PACK_START union {
		MLAN_PACK_START struct {
			UINT8 NonERPPresent:1;
			UINT8 UseProtection:1;
			UINT8 BarkerPreamble:1;
			UINT8 Reserved:5;
		} bf;
		UINT8 erpInfo;
	} u;
} MLAN_PACK_END IEEEtypes_ERPInfoElement_t;

#define IEEEtypes_BCN_FIXED_FIELD_SZ (sizeof(IEEEtypes_TimeStamp_t)     \
                                      + sizeof(IEEEtypes_BcnInterval_t) \
                                      + sizeof(IEEEtypes_CapInfo_t))

typedef MLAN_PACK_START struct {
	IEEEtypes_TimeStamp_t TimeStamp;
	IEEEtypes_BcnInterval_t BcnInterval;
	IEEEtypes_CapInfo_t CapInfo;
	IEEEtypes_SsIdElement_t SsId;
	IEEEtypes_SuppRatesElement_t SuppRates;
	IEEEtypes_PhyParamElement_t PhyParamSet;
	IEEEtypes_SsParamElement_t SsParamSet;
	IEEEtypes_TimElement_t Tim;
	IEEEtypes_ERPInfoElement_t ERPInfo;
	IEEEtypes_ExtSuppRatesElement_t ExtSuppRates;
} MLAN_PACK_END IEEEtypes_Bcn_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ReasonCode_t ReasonCode;
} MLAN_PACK_END IEEEtypes_DisAssoc_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_CapInfo_t CapInfo;
	IEEEtypes_ListenInterval_t ListenInterval;
	UINT8 IEBuffer[1];
} MLAN_PACK_END IEEEtypes_AssocRqst_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_CapInfo_t CapInfo;
	IEEEtypes_StatusCode_t StatusCode;
	IEEEtypes_AId_t AId;
} MLAN_PACK_END IEEEtypes_AssocRsp_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_CapInfo_t CapInfo;
	IEEEtypes_ListenInterval_t ListenInterval;
	IEEEtypes_MacAddr_t CurrentApAddr;
	UINT8 IEBuffer[1];
} MLAN_PACK_END IEEEtypes_ReAssocRqst_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_CapInfo_t CapInfo;
	IEEEtypes_StatusCode_t StatusCode;
	IEEEtypes_AId_t AId;
} MLAN_PACK_END IEEEtypes_ReAssocRsp_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_SsIdElement_t SsId;
	IEEEtypes_SuppRatesElement_t SuppRates;
	IEEEtypes_ExtSuppRatesElement_t ExtSuppRates;
} MLAN_PACK_END IEEEtypes_ProbeRqst_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_TimeStamp_t TimeStamp;
	IEEEtypes_BcnInterval_t BcnInterval;
	IEEEtypes_CapInfo_t CapInfo;
	IEEEtypes_SsIdElement_t SsId;
	IEEEtypes_SuppRatesElement_t SuppRates;
	IEEEtypes_PhyParamElement_t PhyParamSet;
	IEEEtypes_SsParamElement_t SsParamSet;
	IEEEtypes_TimElement_t Tim;
	IEEEtypes_ERPInfoElement_t ERPInfo;
	IEEEtypes_ExtSuppRatesElement_t ExtSuppRates;
} MLAN_PACK_END IEEEtypes_ProbeRsp_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_AuthAlg_t AuthAlg;
	IEEEtypes_AuthTransSeq_t AuthTransSeq;
	IEEEtypes_StatusCode_t StatusCode;
	IEEEtypes_ChallengeText_t ChallengeText;
} MLAN_PACK_END IEEEtypes_Auth_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ReasonCode_t ReasonCode;
} MLAN_PACK_END IEEEtypes_Deauth_t;

/*---------------------------------------------------------------------------*/
/*              IEEE 802.11 MLME SAP Interface Data Structures               */
/*                                                                           */
/* According to IEEE 802.11, services are provided by the MLME to the SME.   */
/* In the current architecture, the services are provided to the SME by the  */
/* MAC Management Service Task. This section describes data structures       */
/* needed for these services.                                                */
/*---------------------------------------------------------------------------*/

/*
** BSS Description Set
**
**  A description of a BSS, providing the following:
**  BssId:        The ID of the BSS
**  SsId:         The SSID of the BSS
**  BssType:      The type of the BSS (INFRASTRUCTURE or INDEPENDENT)
**  BcnPeriod:    The beacon period (in time units)
**  DtimPeriod:   The DTIM period (in beacon periods)
**  Tstamp:       Timestamp of a received frame from the BSS; this is an 8
**                   byte string from a probe response or beacon
**  StartTs:      The value of a station's timing synchronization function
**                   at the start of reception of the first octet of the
**                   timestamp field of a received frame (probe response or
**                   beacon) from a BSS; this is an 8 byte string
**  PhyParamSet:  The parameter set relevant to the PHY (empty if not
**                   needed by the PHY)
**  SsParamSet:   The service set parameters. These can consist of either
**                   the parameter set for CF periods or for an IBSS.
**  Cap:          The advertised capabilities of the BSS
**  DataRates:    The set of data rates that must be supported by all
**                   stations (the BSS basic rate set)
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_MacAddr_t BssId;
	IEEEtypes_SsId_t SsId;
	IEEEtypes_Bss_e BssType;
	IEEEtypes_BcnInterval_t BcnPeriod;
	IEEEtypes_DtimPeriod_t DtimPeriod;
	IEEEtypes_TimeStamp_t Tstamp;
	IEEEtypes_TimeStamp_t StartTs;
	IEEEtypes_PhyParamElement_t PhyParamSet;
	IEEEtypes_SsParamElement_t SsParamSet;
	IEEEtypes_CapInfo_t Cap;
	IEEEtypes_DataRate_t DataRates[IEEEtypes_MAX_DATA_RATES_G];
	/*
	 ** DO NOT ADD ANY FIELDS TO THIS STRUCTURE.  It is used in the middle of
	 **   the adhoc join command.  Any changes will shift the binary layout
	 **   of the following fields in the command from the driver
	 */
} MLAN_PACK_END IEEEtypes_BssDesc_t;

typedef enum {
	Band_2_4_GHz = 0,
	Band_5_GHz = 1,
	Band_4_GHz = 2,

} ChanBand_e;

#define NUM_CHAN_BAND_ENUMS  3

typedef enum {
	ChanWidth_20_MHz = 0,
	ChanWidth_10_MHz = 1,
	ChanWidth_40_MHz = 2,
	ChanWidth_80_MHz = 3,

} ChanWidth_e;

typedef enum {
	SECONDARY_CHAN_NONE = 0,
	SECONDARY_CHAN_ABOVE = 1,
	SECONDARY_CHAN_BELOW = 3,
	//reserved 2, 4~255
} Chan2Offset_e;

typedef enum {
	MANUAL_MODE = 0,
	ACS_MODE = 1,
} ScanMode_e;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;
	UINT8 chan2Offset;
} MLAN_PACK_END IEEEtypes_SecondaryChannelOffset_t;

/* This struct is used in ROM and existing fields should not be changed. */
typedef MLAN_PACK_START struct {
	ChanBand_e chanBand:2;
	ChanWidth_e chanWidth:2;
	Chan2Offset_e chan2Offset:2;
	ScanMode_e scanMode:2;
} MLAN_PACK_END BandConfig_t;

#define MASK_SCAN_MODE(pBandConfig) (*(UINT8*)(pBandConfig) &= 0x3f)

#define BC_2_4_GHZ  {Band_2_4_GHz, ChanWidth_20_MHz, SECONDARY_CHAN_NONE, MANUAL_MODE}
#define BC_5_GHZ    {Band_5_GHz,   ChanWidth_20_MHz, SECONDARY_CHAN_NONE, MANUAL_MODE}

typedef MLAN_PACK_START struct {
	BandConfig_t bandConfig;
	UINT8 chanNum;

} MLAN_PACK_END ChanBandInfo_t;

typedef MLAN_PACK_START struct {
	UINT8 passiveScan:1;
	UINT8 disableChanFilt:1;
	UINT8 multiDomainScan:1;
	UINT8 rspTimeoutEn:1;
	UINT8 hiddenSsidReport:1;
	UINT8 firstScanCmd:1;
	UINT8 reserved_6_7:2;

} MLAN_PACK_END ChanScanMode_t;

typedef MLAN_PACK_START struct {
	BandConfig_t bandConfig;
	UINT8 chanNum;
	ChanScanMode_t scanMode;
	UINT16 minScanTime;	// unused - not removed since shared with
	// host
	UINT16 maxScanTime;

} MLAN_PACK_END channelInfo_t;

/*
** Join request message from the SME to establish synchronization with
** a BSS
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_BssDesc_t BssDesc;
#ifdef OLD_DFS
	IEEEtypes_QuietElement_t Quiet_Element;
	IEEEtypes_IbssDfsElement_t IBSS_DFS_Element;
#endif
	channelInfo_t JoinChannel;

	UINT8 *pIeBuf;
	UINT16 ieBufLen;
} MLAN_PACK_END IEEEtypes_JoinCmd_t;

/*
** Join confirm message sent from the MLME as a result of a join request;
** it reports the result of the join
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_JoinResult_e Result;
} MLAN_PACK_END IEEEtypes_JoinCfrm_t;

/*
** Authenticate request message sent from the SME to establish
** authentication with a specified peer MAC entity
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_MacAddr_t PeerStaAddr;
	IEEEtypes_AuthType_e AuthType;
	IEEEtypes_AuthTransSeq_t AuthTransSeq;
} MLAN_PACK_END IEEEtypes_AuthCmd_t;

/*
** Authenticate confirm message sent from the MLME as a result of an
** authenticate request; it reports the result of the authentication
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_MacAddr_t PeerStaAddr;
	IEEEtypes_AuthType_e AuthType;
	IEEEtypes_AuthResult_e Result;
} MLAN_PACK_END IEEEtypes_AuthCfrm_t;

/*
** Authenticate indication message sent from the MLME to report
** authentication with a peer MAC entity that resulted from an
** authentication procedure that was initiated by that MAC entity
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_MacAddr_t PeerStaAddr;
	IEEEtypes_AuthType_e AuthType;
} MLAN_PACK_END IEEEtypes_AuthInd_t;

/*
** Deauthenticate request message sent from the SME to invalidate
** authentication with a specified peer MAC entity
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_MacAddr_t PeerStaAddr;
	channelInfo_t DeauthChanInfo;
	IEEEtypes_ReasonCode_t Reason;
} MLAN_PACK_END IEEEtypes_DeauthCmd_t;

/*
** Deauthenticate confirm message sent from the MLME as a result of a
** deauthenticate request message; it reports the result of the
** deauthentication
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_MacAddr_t PeerStaAddr;
	IEEEtypes_DeauthResult_e Result;
} MLAN_PACK_END IEEEtypes_DeauthCfrm_t;

/*
** Deauthentication indication message sent from the MLME to report
** invalidation of an authentication with a peer MAC entity; the message
** is generated as a result of an invalidation of the authentication
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_MacAddr_t PeerStaAddr;
	IEEEtypes_ReasonCode_t Reason;
} MLAN_PACK_END IEEEtypes_DeauthInd_t;

/*
** Internal Association command constructed from the TLV based host
**   command struct
*/
typedef struct {
	IEEEtypes_MacAddr_t PeerStaAddr;
	IEEEtypes_CapInfo_t CapInfo;
	IEEEtypes_ListenInterval_t ListenInterval;
	IEEEtypes_SsId_t SsId;
	IEEEtypes_DataRate_t RateSet[IEEEtypes_MAX_DATA_RATES_G];

	UINT8 *pIeBuf;
	UINT16 ieBufLen;

} IEEEtypes_AssocCmd_t;

/*
** Association confirm message sent from the MLME as a result of an
**   association request message; it reports the result of the assoication
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_AssocResult_e Result;
	IEEEtypes_AssocCmdFailurePoint_e FailurePoint;
} MLAN_PACK_END IEEEtypes_AssocCfrm_t;

/*
** Disassociate request message sent from the SME to establish
** disassociation with an AP
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_MacAddr_t PeerStaAddr;
	IEEEtypes_ReasonCode_t Reason;
} MLAN_PACK_END IEEEtypes_DisassocCmd_t;

/*
** Disassociate confirm message sent from the MLME as a result of a
** disassociate request message; it reports the result of the
** disassociation
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_DisassocResult_e Result;
} MLAN_PACK_END IEEEtypes_DisassocCfrm_t;

/*
** Disassociate indication message sent from the MLME to report the
** invalidation of an association relationship with a peer MAC entity;
** the message is generated as a result of an invalidation of an
** association relationship
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_MacAddr_t PeerStaAddr;
	IEEEtypes_ReasonCode_t Reason;
} MLAN_PACK_END IEEEtypes_DisassocInd_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;
	IEEEtypes_MacAddr_t MacAddr;
} MLAN_PACK_END IEEEtypes_BssId_t;

/*
** Start request message sent from the SME to start a new BSS; the BSS
**   may be either an infrastructure BSS (with the MAC entity acting as the
**   AP) or an independent BSS (with the MAC entity acting as the first
**   station in the IBSS)
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_SsId_t SsId;
	IEEEtypes_Bss_e BssType;
	IEEEtypes_BcnInterval_t BcnPeriod;
	IEEEtypes_DtimPeriod_t DtimPeriod;
	IEEEtypes_SsParamElement_t SsParamSet;
	IEEEtypes_PhyParamElement_t PhyParamSet;
	UINT16 Reserved;
	IEEEtypes_CapInfo_t CapInfo;
	IEEEtypes_DataRate_t OpRateSet[IEEEtypes_MAX_DATA_RATES_G];
#ifdef OLD_DFS
	IEEEtypes_QuietElement_t Quiet_Element;
	IEEEtypes_IbssDfsElement_t IBSS_DFS_Element;
#endif
	channelInfo_t JoinChannel;
	UINT8 *pIeBuf;
	UINT16 ieBufLen;
	IEEEtypes_BssId_t BssId;
} MLAN_PACK_END IEEEtypes_StartCmd_t;

/*
** Start confirm message sent from the MLME as a result of a start request
**   message; it reports the results of the BSS creation procedure
*/
typedef MLAN_PACK_START struct {
	IEEEtypes_StartResult_e Result;
} MLAN_PACK_END IEEEtypes_StartCfrm_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_MacAddr_t PeerStaAddr;
} MLAN_PACK_END IEEEtypes_TPCAdaptCmd_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_TPCAdaptResult_e Result;
} MLAN_PACK_END IEEEtypes_TPCAdaptCfrm_t;

typedef struct {
	IEEEtypes_GenHdr_t Hdr;
	UINT8 Body[8];
} IEEEtypes_Frame_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_MacAddr_t da;
	IEEEtypes_MacAddr_t sa;
	UINT16 type;
} MLAN_PACK_END ether_hdr_t;

typedef MLAN_PACK_START struct {
	ether_hdr_t Hdr;
	UINT8 Body[1600 - 14];
} MLAN_PACK_END IEEEtypes_8023_Frame_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElemId;
	IEEEtypes_Len_t Len;
	UINT8 Oui[4];
	UINT8 Data[1];
} MLAN_PACK_END IEEEtypes_WPSElement_t;

/*---------------------------------*/
/* IEEE 802.11 Management Messages */
/*---------------------------------*/

#define WMM_STATS_PKTS_HIST_BINS 7

typedef MLAN_PACK_START enum {
	AckPolicy_ImmediateAck = 0,
	AckPolicy_NoAck = 1,
	AckPolicy_ExplicitAck = 2,
	AckPolicy_BlockAck = 3,

} MLAN_PACK_END IEEEtypes_AckPolicy_e;

typedef MLAN_PACK_START struct {
	UINT16 userPriority:3;
	UINT16 reserved1:1;
	UINT16 eosp:1;
	IEEEtypes_AckPolicy_e ackPolicy:2;
	UINT16 amsdu:1;
	UINT16 reserved2:8;

} MLAN_PACK_END IEEEtypes_QosCtl_t;

typedef MLAN_PACK_START struct {
	UINT8 Version;
	UINT8 SourceIpAddr[4];
	UINT8 DestIpAddr[4];
	UINT8 SourcePort[2];
	UINT8 DestPort[2];
	UINT8 DSCP;
	UINT8 Protocol;
	UINT8 Reserved;

} MLAN_PACK_END IEEEtypes_TCLAS_IPv4_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;

	UINT8 UserPriority;

	UINT8 ClassifierType;
	UINT8 ClassifierMask;

	MLAN_PACK_START union {
		IEEEtypes_TCLAS_IPv4_t ipv4;

	} MLAN_PACK_END classifier;

} MLAN_PACK_END IEEEtypes_TCLAS_t;

typedef enum {
	AC_BE = 0x0,
	AC_BK,
	AC_VI,
	AC_VO,

	AC_MAX_TYPES
} IEEEtypes_WMM_AC_e;

#define WMM_MAX_TIDS              8
#define WMM_MAX_RX_PN_SUPPORTED   4

typedef MLAN_PACK_START struct {
	UINT8 Aifsn:4;
	UINT8 Acm:1;
	UINT8 Aci:2;
	UINT8 Rsvd1:1;

} MLAN_PACK_END IEEEtypes_WMM_AC_ACI_AIFSN_t;

typedef MLAN_PACK_START struct {
	UINT8 EcwMin:4;
	UINT8 EcwMax:4;

} MLAN_PACK_END IEEEtypes_ECW_Min_Max_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_WMM_AC_ACI_AIFSN_t AciAifsn;
	IEEEtypes_ECW_Min_Max_t EcwMinMax;
	UINT16 TxopLimit;

} MLAN_PACK_END IEEEtypes_WMM_AC_Parameters_t;

typedef MLAN_PACK_START struct {
	UINT8 ParamSetCount:4;
	UINT8 Reserved1:3;
	UINT8 QosInfoUAPSD:1;

} MLAN_PACK_END IEEEtypes_QAP_QOS_Info_t;

typedef MLAN_PACK_START struct {
	UINT8 AC_VO:1;
	UINT8 AC_VI:1;
	UINT8 AC_BK:1;
	UINT8 AC_BE:1;
	UINT8 QAck:1;
	UINT8 MaxSP:2;
	UINT8 MoreDataAck:1;

} MLAN_PACK_END IEEEtypes_QSTA_QOS_Info_t;

typedef MLAN_PACK_START union {
	IEEEtypes_QAP_QOS_Info_t QAp;
	IEEEtypes_QSTA_QOS_Info_t QSta;

} MLAN_PACK_END IEEEtypes_QOS_Info_t;

//added for TDLS
typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;
	IEEEtypes_QOS_Info_t QosInfo;
} MLAN_PACK_END IEEEtypes_QOS_Capability_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;
	UINT8 OuiType[4];	/* 00:50:f2:02 */
	UINT8 OuiSubType;	/* 01 */
	UINT8 Version;

	IEEEtypes_QOS_Info_t QosInfo;
	UINT8 Reserved1;
	IEEEtypes_WMM_AC_Parameters_t AcParams[AC_MAX_TYPES];

} MLAN_PACK_END IEEEtypes_WMM_ParamElement_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;
	UINT8 OuiType[4];	/* 00:50:f2:02 */
	UINT8 OuiSubType;	/* 00 */
	UINT8 Version;

	IEEEtypes_QOS_Info_t QosInfo;

} MLAN_PACK_END IEEEtypes_WMM_InfoElement_t;

typedef MLAN_PACK_START enum {
	TSPEC_DIR_UPLINK = 0,
	TSPEC_DIR_DOWNLINK = 1,
	/* 2 is a reserved value */
	TSPEC_DIR_BIDIRECT = 3,

} MLAN_PACK_END IEEEtypes_WMM_TSPEC_TS_Info_Direction_e;

typedef MLAN_PACK_START enum {
	TSPEC_PSB_LEGACY = 0,
	TSPEC_PSB_TRIG = 1,

} MLAN_PACK_END IEEEtypes_WMM_TSPEC_TS_Info_PSB_e;

typedef MLAN_PACK_START enum {
	/* 0 is reserved */
	TSPEC_ACCESS_EDCA = 1,
	TSPEC_ACCESS_HCCA = 2,
	TSPEC_ACCESS_HEMM = 3,

} MLAN_PACK_END IEEEtypes_WMM_TSPEC_TS_Info_AccessPolicy_e;

typedef MLAN_PACK_START enum {
	TSPEC_ACKPOLICY_NORMAL = 0,
	TSPEC_ACKPOLICY_NOACK = 1,
	/* 2 is reserved */
	TSPEC_ACKPOLICY_BLOCKACK = 3,

} MLAN_PACK_END IEEEtypes_WMM_TSPEC_TS_Info_AckPolicy_e;

typedef MLAN_PACK_START enum {
	TSPEC_TRAFFIC_APERIODIC = 0,
	TSPEC_TRAFFIC_PERIODIC = 1,

} MLAN_PACK_END IEEEtypes_WMM_TSPEC_TS_TRAFFIC_TYPE_e;

typedef MLAN_PACK_START struct {
	IEEEtypes_WMM_TSPEC_TS_TRAFFIC_TYPE_e TrafficType:1;
	UINT8 TID:4;		//! Unique identifier
	IEEEtypes_WMM_TSPEC_TS_Info_Direction_e Direction:2;
	UINT8 acp_1:1;

	UINT8 acp_2:1;
	UINT8 Aggregation:1;
	IEEEtypes_WMM_TSPEC_TS_Info_PSB_e PSB:1;	//! Legacy/Trigg
	UINT8 UserPriority:3;	//! 802.1d User Priority
	IEEEtypes_WMM_TSPEC_TS_Info_AckPolicy_e AckPolicy:2;

	UINT8 tsinfo_0:8;
} MLAN_PACK_END IEEEtypes_WMM_TSPEC_TS_Info_t;

typedef MLAN_PACK_START struct {
	UINT16 Size:15;		//! Nominal size in octets
	UINT16 Fixed:1;		//! 1: Fixed size given in Size, 0: Var, size is nominal

} MLAN_PACK_END IEEEtypes_WMM_TSPEC_NomMSDUSize_t;

typedef MLAN_PACK_START struct {
	UINT16 Fractional:13;	//! Fractional portion
	UINT16 Whole:3;		//! Whole portion

} MLAN_PACK_END IEEEtypes_WMM_TSPEC_SBWA;

typedef MLAN_PACK_START struct {
	IEEEtypes_WMM_TSPEC_TS_Info_t TSInfo;
	IEEEtypes_WMM_TSPEC_NomMSDUSize_t NomMSDUSize;
	UINT16 MaximumMSDUSize;
	UINT32 MinServiceInterval;
	UINT32 MaxServiceInterval;
	UINT32 InactivityInterval;
	UINT32 SuspensionInterval;
	UINT32 ServiceStartTime;
	UINT32 MinimumDataRate;
	UINT32 MeanDataRate;
	UINT32 PeakDataRate;
	UINT32 MaxBurstSize;
	UINT32 DelayBound;
	UINT32 MinPHYRate;
	IEEEtypes_WMM_TSPEC_SBWA SurplusBWAllowance;
	UINT16 MediumTime;
} MLAN_PACK_END IEEEtypes_WMM_TSPEC_Body_t;

typedef MLAN_PACK_START struct {
	UINT8 ElementId;
	UINT8 Len;
	UINT8 OuiType[4];	/* 00:50:f2:02 */
	UINT8 OuiSubType;	/* 01 */
	UINT8 Version;

	IEEEtypes_WMM_TSPEC_Body_t TspecBody;

} MLAN_PACK_END IEEEtypes_WMM_TSPEC_t;

typedef MLAN_PACK_START enum {
	TSPEC_ACTION_CODE_ADDTS_REQ = 0,
	TSPEC_ACTION_CODE_ADDTS_RSP = 1,
	TSPEC_ACTION_CODE_DELTS = 2,

} MLAN_PACK_END IEEEtypes_WMM_Tspec_Action_e;

typedef MLAN_PACK_START struct {
	IEEEtypes_ActionCategory_e category;
	IEEEtypes_WMM_Tspec_Action_e action;
	UINT8 dialogToken;

} MLAN_PACK_END IEEEtypes_WMM_Tspec_Action_Base_Tspec_t;

/* Allocate enough space for a V4 TCLASS + a small CCX or VendSpec IE */
#define WMM_TSPEC_EXTRA_IE_BUF_MAX  (sizeof(IEEEtypes_TCLAS_t) + 6)

typedef MLAN_PACK_START struct {
	IEEEtypes_WMM_Tspec_Action_Base_Tspec_t tspecAct;
	UINT8 statusCode;
	IEEEtypes_WMM_TSPEC_t tspecIE;

	/* Place holder for additional elements after the TSPEC */
	UINT8 subElem[WMM_TSPEC_EXTRA_IE_BUF_MAX];

} MLAN_PACK_END IEEEtypes_Action_WMM_AddTsReq_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_WMM_Tspec_Action_Base_Tspec_t tspecAct;
	UINT8 statusCode;
	IEEEtypes_WMM_TSPEC_t tspecIE;

	/* Place holder for additional elements after the TSPEC */
	UINT8 subElem[256];

} MLAN_PACK_END IEEEtypes_Action_WMM_AddTsRsp_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_WMM_Tspec_Action_Base_Tspec_t tspecAct;
	UINT8 reasonCode;
	IEEEtypes_WMM_TSPEC_t tspecIE;

} MLAN_PACK_END IEEEtypes_Action_WMM_DelTs_t;

typedef MLAN_PACK_START union {
	IEEEtypes_WMM_Tspec_Action_Base_Tspec_t tspecAct;

	IEEEtypes_Action_WMM_AddTsReq_t addTsReq;
	IEEEtypes_Action_WMM_AddTsRsp_t addTsRsp;
	IEEEtypes_Action_WMM_DelTs_t delTs;

} MLAN_PACK_END IEEEtypes_Action_WMMAC_t;

typedef MLAN_PACK_START struct {
	IEEEtypes_MgmtHdr_t Hdr;
	MLAN_PACK_START union {
		IEEEtypes_Bcn_t Bcn;
		IEEEtypes_DisAssoc_t DisAssoc;
		IEEEtypes_AssocRqst_t AssocRqst;
		IEEEtypes_AssocRsp_t AssocRsp;
		IEEEtypes_ReAssocRqst_t ReAssocRqst;
		IEEEtypes_ReAssocRsp_t ReAssocRsp;
		IEEEtypes_ProbeRqst_t ProbeRqst;
		IEEEtypes_ProbeRsp_t ProbeRsp;
		IEEEtypes_Auth_t Auth;
		IEEEtypes_Deauth_t Deauth;
		UINT8 BodyStart;
	} MLAN_PACK_END Body;

	UINT32 FCS;
} MLAN_PACK_END dot11MgtFrame_t;

#define FCS_SIZE                    (4)

#define IV_SIZE                     (4)
#define EIV_SIZE                    (4)
#define MIC_SIZE                    (8)
#define MIC_KEY_SIZE                (8)
#define ICV_SIZE                    (4)
#define EXT_IV                      (0x20)

#define IEEE80211_HEADER_SIZE       (24)
#define QOS_CTRL_SIZE               (2)
#define IEEE80211_QOSHEADER_SIZE    (IEEE80211_HEADER_SIZE+QOS_CTRL_SIZE)

#define MACHDR_n_FCS_SIZE       (IEEE80211_HEADER_SIZE+FCS_SIZE)
#define QOS_MACHDR_n_FCS_SIZE   (IEEE80211_QOSHEADER_SIZE+FCS_SIZE)

#define WEPOVERHEAD             (IV_SIZE+ICV_SIZE)

#define IEEE80211_SIFS_11b      (10)	/* us */
#define IEEE80211_SIFS_11g      (IEEE80211_SIFS_11b)	/* us */
#define IEEE80211_SIFS_11a      (16)	/* us */

#define IEEE80211b_SHORT_PREAM  (96)	/* us */
#define IEEE80211b_LONG_PREAM   (192)	/* us */

#define SIGNAL_EXTENSION        (6)	/* us */

#define TPREAMBLE               (16)	/* us */
#define TSIGNAL                 (4)	/* us */
#define IEEE80211g_PREAM        (TPREAMBLE+TSIGNAL)	/* us */
#define IEEE80211a_PREAM        (IEEE80211g_PREAM )	/* us */
#define IEEE80211n_PREAM        (40)	/* us */
#define IEEE80211ac_PREAM       (40)    /* us */	//TBD

#define TSYM                    (4)	/* us */

#define IEEE80211_BSS_CLOCK_PPM (100)	// ppm

enum {
	OTHER = 0x00,
	FCC = 0x10,
	IC = 0x20,
	ETSI = 0x30,
	SPAIN = 0x31,
	FRANCE = 0x32,
	JAPAN = 0x40,
	JAPAN1 = 0x41,
	CHINA = 0x50
};

typedef enum {
	RegDomain_Null = 0x00,

	RegDomain_FCC = 0x01,
	RegDomain_ETSI = 0x02,
	RegDomain_MIC = 0x03,

	RegDomain_Other = 0xFF,

} RegDomain_e;

#define IEEE_PHY_RATE_CODE_1Mbps    (10)	// it's the same as IEEE_PHY_RATE_CODE_12Mbps
#define IEEE_PHY_RATE_CODE_2Mbps    (20)
#define IEEE_PHY_RATE_CODE_5_5Mbps  (55)
#define IEEE_PHY_RATE_CODE_11Mbps   (110)
#define IEEE_PHY_RATE_CODE_22Mbps   (220)
#define IEEE_PHY_RATE_CODE_6Mbps    (0x0B)
#define IEEE_PHY_RATE_CODE_9Mbps    (0x0F)
#define IEEE_PHY_RATE_CODE_12Mbps   (0x0A)	// it's the same as IEEE_PHY_RATE_CODE_1Mbps
#define IEEE_PHY_RATE_CODE_18Mbps   (0x0E)
#define IEEE_PHY_RATE_CODE_24Mbps   (0x09)
#define IEEE_PHY_RATE_CODE_36Mbps   (0x0D)
#define IEEE_PHY_RATE_CODE_48Mbps   (0x08)
#define IEEE_PHY_RATE_CODE_54Mbps   (0x0C)
#define IEEE_PHY_RATE_CODE_72Mbps   (0x07)

#define IEEE_PHY_RATE_CODE_1Mbps    (10)	// it's the same as IEEE_PHY_RATE_CODE_12Mbps
#define IEEE_PHY_RATE_CODE_2Mbps    (20)
#define IEEE_PHY_RATE_CODE_5_5Mbps  (55)
#define IEEE_PHY_RATE_CODE_11Mbps   (110)
#define IEEE_PHY_RATE_CODE_22Mbps   (220)

#define IEEE_PHY_RATE_CODE_6Mbps    (0x0B)
#define IEEE_PHY_RATE_CODE_9Mbps    (0x0F)
#define IEEE_PHY_RATE_CODE_12Mbps   (0x0A)	// it's the same as IEEE_PHY_RATE_CODE_1Mbps
#define IEEE_PHY_RATE_CODE_18Mbps   (0x0E)
#define IEEE_PHY_RATE_CODE_24Mbps   (0x09)
#define IEEE_PHY_RATE_CODE_36Mbps   (0x0D)
#define IEEE_PHY_RATE_CODE_48Mbps   (0x08)
#define IEEE_PHY_RATE_CODE_54Mbps   (0x0C)
#define IEEE_PHY_RATE_CODE_72Mbps   (0x07)

#define IEEE_PHY_RATE_CODE_MCS0     (0x00)
#define IEEE_PHY_RATE_CODE_MCS1     (0x01)
#define IEEE_PHY_RATE_CODE_MCS2     (0x02)
#define IEEE_PHY_RATE_CODE_MCS3     (0x03)
#define IEEE_PHY_RATE_CODE_MCS4     (0x04)
#define IEEE_PHY_RATE_CODE_MCS5     (0x05)
#define IEEE_PHY_RATE_CODE_MCS6     (0x06)
#define IEEE_PHY_RATE_CODE_MCS7     (0x07)

#define IEEE_PHY_RATE_CODE_MCS8     (0x08)
#define IEEE_PHY_RATE_CODE_MCS9     (0x09)
#define IEEE_PHY_RATE_CODE_MCS10    (0x0A)
#define IEEE_PHY_RATE_CODE_MCS11    (0x0B)
#define IEEE_PHY_RATE_CODE_MCS12    (0x0C)
#define IEEE_PHY_RATE_CODE_MCS13    (0x0D)
#define IEEE_PHY_RATE_CODE_MCS14    (0x0E)
#define IEEE_PHY_RATE_CODE_MCS15    (0x0F)

#define IEEE_PHY_RATE_CODE_MCS32    (0x20)

typedef enum {
	MOD_CLASS_INFRA = 1,
	MOD_CLASS_FHSS = 2,
	MOD_CLASS_HR_DSSS = 3,
	MOD_CLASS_ERP_PBCC = 4,
	MOD_CLASS_DSSS_OFDM = 5,
	MOD_CLASS_ERP_OFDM = 6,
	MOD_CLASS_OFDM = 7,
	MOD_CLASS_HT = 8,
	MOD_CLASS_VHT = 9,
} MOD_CLASS_e;

typedef enum {
	DSSS_1Mbps = 0 + (MOD_CLASS_HR_DSSS << 8),
	DSSS_2Mbps,
	DSSS_5d5Mbps,
	DSSS_11Mbps,
} HR_DSSS_e;

typedef enum {
	OFDM_6Mbps = 0 + (MOD_CLASS_OFDM << 8),
	OFDM_9Mbps,
	OFDM_12Mbps,
	OFDM_18Mbps,
	OFDM_24Mbps,
	OFDM_36Mbps,
	OFDM_48Mbps,
	OFDM_54Mbps,
} OFDM_e;

typedef enum {
	// 1x1 EM
	MCS_0 = 0 + (MOD_CLASS_HT << 8),
	MCS_1,
	MCS_2,
	MCS_3,
	MCS_4,
	MCS_5,
	MCS_6,
	MCS_7,

	// 2x2 EM
	MCS_8,
	MCS_9,
	MCS_10,
	MCS_11,
	MCS_12,
	MCS_13,
	MCS_14,
	MCS_15,

	// 3x3 EM
	MCS_16,
	MCS_17,
	MCS_18,
	MCS_19,
	MCS_20,
	MCS_21,
	MCS_22,
	MCS_23,

	// 4x4 EM
	MCS_24,
	MCS_25,
	MCS_26,
	MCS_27,
	MCS_28,
	MCS_29,
	MCS_30,
	MCS_31,

	// 1x1 EM 40MHz only
	MCS_32,

	// 2x2 UEM
	MCS_33,
	MCS_34,
	MCS_35,
	MCS_36,
	MCS_37,
	MCS_38,

	// 3x3 UEM
	MCS_39,
	MCS_40,
	MCS_41,
	MCS_42,
	MCS_43,
	MCS_44,
	MCS_45,
	MCS_46,
	MCS_47,
	MCS_48,
	MCS_49,
	MCS_50,
	MCS_51,
	MCS_52,

	// 4x4 UEM
	MCS_53,
	MCS_54,
	MCS_55,
	MCS_56,
	MCS_57,
	MCS_58,
	MCS_59,
	MCS_60,
	MCS_61,
	MCS_62,
	MCS_63,
	MCS_64,
	MCS_65,
	MCS_66,
	MCS_67,
	MCS_68,
	MCS_69,
	MCS_70,
	MCS_71,
	MCS_72,
	MCS_73,
	MCS_74,
	MCS_75,
	MCS_76,

} MCS_e;

typedef union {
	UINT16 u16;
	HR_DSSS_e dsss;
	OFDM_e ofdm;
	MCS_e mcs;
} RATE_CODE_u;

#define RATECODE_to_RATEID(x)     (x & 0xff)

#define IEEEtypes_STATUS_INVALID_INFO_ELEMENT    40
#define IEEEtypes_STATUS_INVALID_AKMP            43
#define IEEEtypes_STATUS_CIPHER_POLICY_REJECT    46
#define IEEEtypes_STATUS_INVALID_MCAST_CIPHER    47
#define IEEEtypes_STATUS_INVALID_UCAST_CIPHER    48
#define IEEEtypes_STATUS_UNSUPPORT_WAPI_VERSION  49
#define IEEEtypes_STATUS_INVALID_WAPI_CAPS       50

typedef MLAN_PACK_START struct {
	IEEEtypes_ElementId_e ElementId;
	IEEEtypes_Len_t Len;
	UINT8 Data[1];
} MLAN_PACK_END IEEEtypes_WAPIElement_t;

#endif /* _IEEE_TYPES_H_ */
