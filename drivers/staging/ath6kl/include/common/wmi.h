//------------------------------------------------------------------------------
// Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
//
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//
// Author(s): ="Atheros"
//------------------------------------------------------------------------------

/*
 * This file contains the definitions of the WMI protocol specified in the
 * Wireless Module Interface (WMI).  It includes definitions of all the
 * commands and events. Commands are messages from the host to the WM.
 * Events and Replies are messages from the WM to the host.
 *
 * Ownership of correctness in regards to commands
 * belongs to the host driver and the WMI is not required to validate
 * parameters for value, proper range, or any other checking.
 *
 */

#ifndef _WMI_H_
#define _WMI_H_

#include "wmix.h"
#include "wlan_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HTC_PROTOCOL_VERSION    0x0002
#define HTC_PROTOCOL_REVISION   0x0000

#define WMI_PROTOCOL_VERSION    0x0002
#define WMI_PROTOCOL_REVISION   0x0000

#define ATH_MAC_LEN             6               /* length of mac in bytes */
#define WMI_CMD_MAX_LEN         100
#define WMI_CONTROL_MSG_MAX_LEN     256
#define WMI_OPT_CONTROL_MSG_MAX_LEN 1536
#define IS_ETHERTYPE(_typeOrLen)        ((_typeOrLen) >= 0x0600)
#define RFC1042OUI      {0x00, 0x00, 0x00}

#define IP_ETHERTYPE 0x0800

#define WMI_IMPLICIT_PSTREAM 0xFF
#define WMI_MAX_THINSTREAM 15

#ifdef AR6002_REV2
#define IBSS_MAX_NUM_STA          4
#else
#define IBSS_MAX_NUM_STA          8
#endif

PREPACK struct host_app_area_s {
    u32 wmi_protocol_ver;
} POSTPACK;

/*
 * Data Path
 */
typedef PREPACK struct {
    u8 dstMac[ATH_MAC_LEN];
    u8 srcMac[ATH_MAC_LEN];
    u16 typeOrLen;
} POSTPACK ATH_MAC_HDR;

typedef PREPACK struct {
    u8 dsap;
    u8 ssap;
    u8 cntl;
    u8 orgCode[3];
    u16 etherType;
} POSTPACK ATH_LLC_SNAP_HDR;

typedef enum {
    DATA_MSGTYPE = 0x0,
    CNTL_MSGTYPE,
    SYNC_MSGTYPE,
    OPT_MSGTYPE,
} WMI_MSG_TYPE;


/*
 * Macros for operating on WMI_DATA_HDR (info) field
 */

#define WMI_DATA_HDR_MSG_TYPE_MASK  0x03
#define WMI_DATA_HDR_MSG_TYPE_SHIFT 0
#define WMI_DATA_HDR_UP_MASK        0x07
#define WMI_DATA_HDR_UP_SHIFT       2
/* In AP mode, the same bit (b5) is used to indicate Power save state in
 * the Rx dir and More data bit state in the tx direction.
 */
#define WMI_DATA_HDR_PS_MASK        0x1
#define WMI_DATA_HDR_PS_SHIFT       5

#define WMI_DATA_HDR_MORE_MASK      0x1
#define WMI_DATA_HDR_MORE_SHIFT     5

typedef enum {
    WMI_DATA_HDR_DATA_TYPE_802_3 = 0,
    WMI_DATA_HDR_DATA_TYPE_802_11,
    WMI_DATA_HDR_DATA_TYPE_ACL, /* used to be used for the PAL */
} WMI_DATA_HDR_DATA_TYPE;

#define WMI_DATA_HDR_DATA_TYPE_MASK     0x3
#define WMI_DATA_HDR_DATA_TYPE_SHIFT    6

#define WMI_DATA_HDR_SET_MORE_BIT(h) ((h)->info |= (WMI_DATA_HDR_MORE_MASK << WMI_DATA_HDR_MORE_SHIFT))

#define WMI_DATA_HDR_IS_MSG_TYPE(h, t)  (((h)->info & (WMI_DATA_HDR_MSG_TYPE_MASK)) == (t))
#define WMI_DATA_HDR_SET_MSG_TYPE(h, t) (h)->info = (((h)->info & ~(WMI_DATA_HDR_MSG_TYPE_MASK << WMI_DATA_HDR_MSG_TYPE_SHIFT)) | (t << WMI_DATA_HDR_MSG_TYPE_SHIFT))
#define WMI_DATA_HDR_GET_UP(h)    (((h)->info >> WMI_DATA_HDR_UP_SHIFT) & WMI_DATA_HDR_UP_MASK)
#define WMI_DATA_HDR_SET_UP(h, p) (h)->info = (((h)->info & ~(WMI_DATA_HDR_UP_MASK << WMI_DATA_HDR_UP_SHIFT)) | (p << WMI_DATA_HDR_UP_SHIFT))

#define WMI_DATA_HDR_GET_DATA_TYPE(h)   (((h)->info >> WMI_DATA_HDR_DATA_TYPE_SHIFT) & WMI_DATA_HDR_DATA_TYPE_MASK)
#define WMI_DATA_HDR_SET_DATA_TYPE(h, p) (h)->info = (((h)->info & ~(WMI_DATA_HDR_DATA_TYPE_MASK << WMI_DATA_HDR_DATA_TYPE_SHIFT)) | ((p) << WMI_DATA_HDR_DATA_TYPE_SHIFT))

#define WMI_DATA_HDR_GET_DOT11(h)   (WMI_DATA_HDR_GET_DATA_TYPE((h)) == WMI_DATA_HDR_DATA_TYPE_802_11)
#define WMI_DATA_HDR_SET_DOT11(h, p) WMI_DATA_HDR_SET_DATA_TYPE((h), (p))

/* Macros for operating on WMI_DATA_HDR (info2) field */
#define WMI_DATA_HDR_SEQNO_MASK     0xFFF
#define WMI_DATA_HDR_SEQNO_SHIFT    0

#define WMI_DATA_HDR_AMSDU_MASK     0x1
#define WMI_DATA_HDR_AMSDU_SHIFT    12

#define WMI_DATA_HDR_META_MASK      0x7
#define WMI_DATA_HDR_META_SHIFT     13

#define GET_SEQ_NO(_v)                  ((_v) & WMI_DATA_HDR_SEQNO_MASK)
#define GET_ISMSDU(_v)                  ((_v) & WMI_DATA_HDR_AMSDU_MASK)

#define WMI_DATA_HDR_GET_SEQNO(h)        GET_SEQ_NO((h)->info2 >> WMI_DATA_HDR_SEQNO_SHIFT)
#define WMI_DATA_HDR_SET_SEQNO(h, _v)   ((h)->info2 = ((h)->info2 & ~(WMI_DATA_HDR_SEQNO_MASK << WMI_DATA_HDR_SEQNO_SHIFT)) | (GET_SEQ_NO(_v) << WMI_DATA_HDR_SEQNO_SHIFT))

#define WMI_DATA_HDR_IS_AMSDU(h)        GET_ISMSDU((h)->info2 >> WMI_DATA_HDR_AMSDU_SHIFT)
#define WMI_DATA_HDR_SET_AMSDU(h, _v)   ((h)->info2 = ((h)->info2 & ~(WMI_DATA_HDR_AMSDU_MASK << WMI_DATA_HDR_AMSDU_SHIFT)) | (GET_ISMSDU(_v) << WMI_DATA_HDR_AMSDU_SHIFT))

#define WMI_DATA_HDR_GET_META(h)        (((h)->info2 >> WMI_DATA_HDR_META_SHIFT) & WMI_DATA_HDR_META_MASK)
#define WMI_DATA_HDR_SET_META(h, _v)    ((h)->info2 = ((h)->info2 & ~(WMI_DATA_HDR_META_MASK << WMI_DATA_HDR_META_SHIFT)) | ((_v) << WMI_DATA_HDR_META_SHIFT))

/* Macros for operating on WMI_DATA_HDR (info3) field */
#define WMI_DATA_HDR_DEVID_MASK      0xF
#define WMI_DATA_HDR_DEVID_SHIFT     0
#define GET_DEVID(_v)                ((_v) & WMI_DATA_HDR_DEVID_MASK)

#define WMI_DATA_HDR_GET_DEVID(h) \
	(((h)->info3 >> WMI_DATA_HDR_DEVID_SHIFT) & WMI_DATA_HDR_DEVID_MASK)
#define WMI_DATA_HDR_SET_DEVID(h, _v) \
	((h)->info3 = ((h)->info3 & ~(WMI_DATA_HDR_DEVID_MASK << WMI_DATA_HDR_DEVID_SHIFT)) | (GET_DEVID(_v) << WMI_DATA_HDR_DEVID_SHIFT))

typedef PREPACK struct {
    s8 rssi;
    u8 info;               /* usage of 'info' field(8-bit):
                                     *  b1:b0       - WMI_MSG_TYPE
                                     *  b4:b3:b2    - UP(tid)
                                     *  b5          - Used in AP mode. More-data in tx dir, PS in rx.
                                     *  b7:b6       -  Dot3 header(0),
                                     *                 Dot11 Header(1),
                                     *                 ACL data(2)
                                     */

    u16 info2;              /* usage of 'info2' field(16-bit):
                                     * b11:b0       - seq_no
                                     * b12          - A-MSDU?
                                     * b15:b13      - META_DATA_VERSION 0 - 7
                                     */
    u16 info3;
} POSTPACK WMI_DATA_HDR;

/*
 *  TX META VERSION DEFINITIONS
 */
#define WMI_MAX_TX_META_SZ  (12)
#define WMI_MAX_TX_META_VERSION (7)
#define WMI_META_VERSION_1 (0x01)
#define WMI_META_VERSION_2 (0X02)

#define WMI_ACL_TO_DOT11_HEADROOM   36

#if 0 /* removed to prevent compile errors for WM.. */
typedef PREPACK struct {
/* intentionally empty. Default version is no meta data. */
} POSTPACK WMI_TX_META_V0;
#endif

typedef PREPACK struct {
    u8 pktID;           /* The packet ID to identify the tx request */
    u8 ratePolicyID;    /* The rate policy to be used for the tx of this frame */
} POSTPACK WMI_TX_META_V1;


#define WMI_CSUM_DIR_TX (0x1)
#define TX_CSUM_CALC_FILL (0x1)
typedef PREPACK struct {
    u8 csumStart;       /*Offset from start of the WMI header for csum calculation to begin */
    u8 csumDest;        /*Offset from start of WMI header where final csum goes*/
    u8 csumFlags;    /*number of bytes over which csum is calculated*/
} POSTPACK WMI_TX_META_V2;


/*
 *  RX META VERSION DEFINITIONS
 */
/* if RX meta data is present at all then the meta data field
 *  will consume WMI_MAX_RX_META_SZ bytes of space between the
 *  WMI_DATA_HDR and the payload. How much of the available
 *  Meta data is actually used depends on which meta data
 *  version is active. */
#define WMI_MAX_RX_META_SZ  (12)
#define WMI_MAX_RX_META_VERSION (7)

#define WMI_RX_STATUS_OK            0 /* success */
#define WMI_RX_STATUS_DECRYPT_ERR   1 /* decrypt error */
#define WMI_RX_STATUS_MIC_ERR       2 /* tkip MIC error */
#define WMI_RX_STATUS_ERR           3 /* undefined error */

#define WMI_RX_FLAGS_AGGR           0x0001 /* part of AGGR */
#define WMI_RX_FlAGS_STBC           0x0002 /* used STBC */
#define WMI_RX_FLAGS_SGI            0x0004 /* used SGI */
#define WMI_RX_FLAGS_HT             0x0008 /* is HT packet */
/* the flags field is also used to store the CRYPTO_TYPE of the frame
 * that value is shifted by WMI_RX_FLAGS_CRYPTO_SHIFT */
#define WMI_RX_FLAGS_CRYPTO_SHIFT   4
#define WMI_RX_FLAGS_CRYPTO_MASK    0x1f
#define WMI_RX_META_GET_CRYPTO(flags) (((flags) >> WMI_RX_FLAGS_CRYPTO_SHIFT) & WMI_RX_FLAGS_CRYPTO_MASK)

#if 0 /* removed to prevent compile errors for WM.. */
typedef PREPACK struct {
/* intentionally empty. Default version is no meta data. */
} POSTPACK WMI_RX_META_VERSION_0;
#endif

typedef PREPACK struct {
    u8 status; /* one of WMI_RX_STATUS_... */
    u8 rix;    /* rate index mapped to rate at which this packet was received. */
    u8 rssi;   /* rssi of packet */
    u8 channel;/* rf channel during packet reception */
    u16 flags;  /* a combination of WMI_RX_FLAGS_... */
} POSTPACK WMI_RX_META_V1;

#define RX_CSUM_VALID_FLAG (0x1)
typedef PREPACK struct {
    u16 csum;
    u8 csumFlags;/* bit 0 set -partial csum valid
                             bit 1 set -test mode */
} POSTPACK WMI_RX_META_V2;



#define WMI_GET_DEVICE_ID(info1) ((info1) & 0xF)
/* Macros for operating on WMI_CMD_HDR (info1) field */
#define WMI_CMD_HDR_DEVID_MASK      0xF
#define WMI_CMD_HDR_DEVID_SHIFT     0
#define GET_CMD_DEVID(_v)           ((_v) & WMI_CMD_HDR_DEVID_MASK)

#define WMI_CMD_HDR_GET_DEVID(h) \
	(((h)->info1 >> WMI_CMD_HDR_DEVID_SHIFT) & WMI_CMD_HDR_DEVID_MASK)
#define WMI_CMD_HDR_SET_DEVID(h, _v) \
	((h)->info1 = ((h)->info1 & \
		~(WMI_CMD_HDR_DEVID_MASK << WMI_CMD_HDR_DEVID_SHIFT)) | \
		 (GET_CMD_DEVID(_v) << WMI_CMD_HDR_DEVID_SHIFT))

/*
 * Control Path
 */
typedef PREPACK struct {
    u16 commandId;
/*
 * info1 - 16 bits
 * b03:b00 - id
 * b15:b04 - unused
 */
    u16 info1;

    u16 reserved;      /* For alignment */
} POSTPACK WMI_CMD_HDR;        /* used for commands and events */

/*
 * List of Commnands
 */
typedef enum {
    WMI_CONNECT_CMDID           = 0x0001,
    WMI_RECONNECT_CMDID,
    WMI_DISCONNECT_CMDID,
    WMI_SYNCHRONIZE_CMDID,
    WMI_CREATE_PSTREAM_CMDID,
    WMI_DELETE_PSTREAM_CMDID,
    WMI_START_SCAN_CMDID,
    WMI_SET_SCAN_PARAMS_CMDID,
    WMI_SET_BSS_FILTER_CMDID,
    WMI_SET_PROBED_SSID_CMDID,               /* 10 */
    WMI_SET_LISTEN_INT_CMDID,
    WMI_SET_BMISS_TIME_CMDID,
    WMI_SET_DISC_TIMEOUT_CMDID,
    WMI_GET_CHANNEL_LIST_CMDID,
    WMI_SET_BEACON_INT_CMDID,
    WMI_GET_STATISTICS_CMDID,
    WMI_SET_CHANNEL_PARAMS_CMDID,
    WMI_SET_POWER_MODE_CMDID,
    WMI_SET_IBSS_PM_CAPS_CMDID,
    WMI_SET_POWER_PARAMS_CMDID,              /* 20 */
    WMI_SET_POWERSAVE_TIMERS_POLICY_CMDID,
    WMI_ADD_CIPHER_KEY_CMDID,
    WMI_DELETE_CIPHER_KEY_CMDID,
    WMI_ADD_KRK_CMDID,
    WMI_DELETE_KRK_CMDID,
    WMI_SET_PMKID_CMDID,
    WMI_SET_TX_PWR_CMDID,
    WMI_GET_TX_PWR_CMDID,
    WMI_SET_ASSOC_INFO_CMDID,
    WMI_ADD_BAD_AP_CMDID,                    /* 30 */
    WMI_DELETE_BAD_AP_CMDID,
    WMI_SET_TKIP_COUNTERMEASURES_CMDID,
    WMI_RSSI_THRESHOLD_PARAMS_CMDID,
    WMI_TARGET_ERROR_REPORT_BITMASK_CMDID,
    WMI_SET_ACCESS_PARAMS_CMDID,
    WMI_SET_RETRY_LIMITS_CMDID,
    WMI_SET_OPT_MODE_CMDID,
    WMI_OPT_TX_FRAME_CMDID,
    WMI_SET_VOICE_PKT_SIZE_CMDID,
    WMI_SET_MAX_SP_LEN_CMDID,                /* 40 */
    WMI_SET_ROAM_CTRL_CMDID,
    WMI_GET_ROAM_TBL_CMDID,
    WMI_GET_ROAM_DATA_CMDID,
    WMI_ENABLE_RM_CMDID,
    WMI_SET_MAX_OFFHOME_DURATION_CMDID,
    WMI_EXTENSION_CMDID,                        /* Non-wireless extensions */
    WMI_SNR_THRESHOLD_PARAMS_CMDID,
    WMI_LQ_THRESHOLD_PARAMS_CMDID,
    WMI_SET_LPREAMBLE_CMDID,
    WMI_SET_RTS_CMDID,                       /* 50 */
    WMI_CLR_RSSI_SNR_CMDID,
    WMI_SET_FIXRATES_CMDID,
    WMI_GET_FIXRATES_CMDID,
    WMI_SET_AUTH_MODE_CMDID,
    WMI_SET_REASSOC_MODE_CMDID,
    WMI_SET_WMM_CMDID,
    WMI_SET_WMM_TXOP_CMDID,
    WMI_TEST_CMDID,
    /* COEX AR6002 only*/
    WMI_SET_BT_STATUS_CMDID,                
    WMI_SET_BT_PARAMS_CMDID,                /* 60 */

    WMI_SET_KEEPALIVE_CMDID,
    WMI_GET_KEEPALIVE_CMDID,
    WMI_SET_APPIE_CMDID,
    WMI_GET_APPIE_CMDID,
    WMI_SET_WSC_STATUS_CMDID,

    /* Wake on Wireless */
    WMI_SET_HOST_SLEEP_MODE_CMDID,
    WMI_SET_WOW_MODE_CMDID,
    WMI_GET_WOW_LIST_CMDID,
    WMI_ADD_WOW_PATTERN_CMDID,
    WMI_DEL_WOW_PATTERN_CMDID,               /* 70 */

    WMI_SET_FRAMERATES_CMDID,
    WMI_SET_AP_PS_CMDID,
    WMI_SET_QOS_SUPP_CMDID,
    /* WMI_THIN_RESERVED_... mark the start and end
     * values for WMI_THIN_RESERVED command IDs. These
     * command IDs can be found in wmi_thin.h */
    WMI_THIN_RESERVED_START = 0x8000,
    WMI_THIN_RESERVED_END = 0x8fff,
    /*
     * Developer commands starts at 0xF000
     */
    WMI_SET_BITRATE_CMDID = 0xF000,
    WMI_GET_BITRATE_CMDID,
    WMI_SET_WHALPARAM_CMDID,


    /*Should add the new command to the tail for compatible with
     * etna.
     */
    WMI_SET_MAC_ADDRESS_CMDID,
    WMI_SET_AKMP_PARAMS_CMDID,
    WMI_SET_PMKID_LIST_CMDID,
    WMI_GET_PMKID_LIST_CMDID,
    WMI_ABORT_SCAN_CMDID,
    WMI_SET_TARGET_EVENT_REPORT_CMDID,

    // Unused
    WMI_UNUSED1,
    WMI_UNUSED2,

    /*
     * AP mode commands
     */
    WMI_AP_HIDDEN_SSID_CMDID,
    WMI_AP_SET_NUM_STA_CMDID,
    WMI_AP_ACL_POLICY_CMDID,
    WMI_AP_ACL_MAC_LIST_CMDID,
    WMI_AP_CONFIG_COMMIT_CMDID,
    WMI_AP_SET_MLME_CMDID,
    WMI_AP_SET_PVB_CMDID,
    WMI_AP_CONN_INACT_CMDID,
    WMI_AP_PROT_SCAN_TIME_CMDID,
    WMI_AP_SET_COUNTRY_CMDID,
    WMI_AP_SET_DTIM_CMDID,
    WMI_AP_MODE_STAT_CMDID,

    WMI_SET_IP_CMDID,
    WMI_SET_PARAMS_CMDID,
    WMI_SET_MCAST_FILTER_CMDID,
    WMI_DEL_MCAST_FILTER_CMDID,

    WMI_ALLOW_AGGR_CMDID,
    WMI_ADDBA_REQ_CMDID,
    WMI_DELBA_REQ_CMDID,
    WMI_SET_HT_CAP_CMDID,
    WMI_SET_HT_OP_CMDID,
    WMI_SET_TX_SELECT_RATES_CMDID,
    WMI_SET_TX_SGI_PARAM_CMDID,
    WMI_SET_RATE_POLICY_CMDID,

    WMI_HCI_CMD_CMDID,
    WMI_RX_FRAME_FORMAT_CMDID,
    WMI_SET_THIN_MODE_CMDID,
    WMI_SET_BT_WLAN_CONN_PRECEDENCE_CMDID,

    WMI_AP_SET_11BG_RATESET_CMDID,
    WMI_SET_PMK_CMDID,
    WMI_MCAST_FILTER_CMDID,
    /* COEX CMDID AR6003*/
    WMI_SET_BTCOEX_FE_ANT_CMDID,
    WMI_SET_BTCOEX_COLOCATED_BT_DEV_CMDID,
    WMI_SET_BTCOEX_SCO_CONFIG_CMDID,
    WMI_SET_BTCOEX_A2DP_CONFIG_CMDID,
    WMI_SET_BTCOEX_ACLCOEX_CONFIG_CMDID,
    WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG_CMDID,
    WMI_SET_BTCOEX_DEBUG_CMDID,
    WMI_SET_BTCOEX_BT_OPERATING_STATUS_CMDID,
    WMI_GET_BTCOEX_STATS_CMDID,
    WMI_GET_BTCOEX_CONFIG_CMDID,

	WMI_SET_DFS_ENABLE_CMDID,   /* F034 */
	WMI_SET_DFS_MINRSSITHRESH_CMDID,
	WMI_SET_DFS_MAXPULSEDUR_CMDID,
	WMI_DFS_RADAR_DETECTED_CMDID,

	/* P2P CMDS */
	WMI_P2P_SET_CONFIG_CMDID,    /* F038 */
	WMI_WPS_SET_CONFIG_CMDID,
	WMI_SET_REQ_DEV_ATTR_CMDID,
	WMI_P2P_FIND_CMDID,
	WMI_P2P_STOP_FIND_CMDID,
	WMI_P2P_GO_NEG_START_CMDID,
	WMI_P2P_LISTEN_CMDID,

	WMI_CONFIG_TX_MAC_RULES_CMDID,    /* F040 */
	WMI_SET_PROMISCUOUS_MODE_CMDID,
	WMI_RX_FRAME_FILTER_CMDID,
	WMI_SET_CHANNEL_CMDID,

	/* WAC commands */
	WMI_ENABLE_WAC_CMDID,
	WMI_WAC_SCAN_REPLY_CMDID,
	WMI_WAC_CTRL_REQ_CMDID,
	WMI_SET_DIV_PARAMS_CMDID,

	WMI_GET_PMK_CMDID,
	WMI_SET_PASSPHRASE_CMDID,
	WMI_SEND_ASSOC_RES_CMDID,
	WMI_SET_ASSOC_REQ_RELAY_CMDID,
	WMI_GET_RFKILL_MODE_CMDID,

	/* ACS command, consists of sub-commands */
	WMI_ACS_CTRL_CMDID,

	/* Ultra low power store / recall commands */
	WMI_STORERECALL_CONFIGURE_CMDID,
	WMI_STORERECALL_RECALL_CMDID,
	WMI_STORERECALL_HOST_READY_CMDID,
	WMI_FORCE_TARGET_ASSERT_CMDID,
	WMI_SET_EXCESS_TX_RETRY_THRES_CMDID,
} WMI_COMMAND_ID;

/*
 * Frame Types
 */
typedef enum {
    WMI_FRAME_BEACON        =   0,
    WMI_FRAME_PROBE_REQ,
    WMI_FRAME_PROBE_RESP,
    WMI_FRAME_ASSOC_REQ,
    WMI_FRAME_ASSOC_RESP,
    WMI_NUM_MGMT_FRAME
} WMI_MGMT_FRAME_TYPE;

/*
 * Connect Command
 */
typedef enum {
    INFRA_NETWORK       = 0x01,
    ADHOC_NETWORK       = 0x02,
    ADHOC_CREATOR       = 0x04,
    AP_NETWORK          = 0x10,
} NETWORK_TYPE;

typedef enum {
    OPEN_AUTH           = 0x01,
    SHARED_AUTH         = 0x02,
    LEAP_AUTH           = 0x04,  /* different from IEEE_AUTH_MODE definitions */
} DOT11_AUTH_MODE;

enum {
	AUTH_IDLE,
	AUTH_OPEN_IN_PROGRESS,
};

typedef enum {
    NONE_AUTH           = 0x01,
    WPA_AUTH            = 0x02,
    WPA2_AUTH           = 0x04,
    WPA_PSK_AUTH        = 0x08,
    WPA2_PSK_AUTH       = 0x10,
    WPA_AUTH_CCKM       = 0x20,
    WPA2_AUTH_CCKM      = 0x40,
} AUTH_MODE;

typedef enum {
    NONE_CRYPT          = 0x01,
    WEP_CRYPT           = 0x02,
    TKIP_CRYPT          = 0x04,
    AES_CRYPT           = 0x08,
#ifdef WAPI_ENABLE
    WAPI_CRYPT          = 0x10,
#endif /*WAPI_ENABLE*/
} CRYPTO_TYPE;

#define WMI_MIN_CRYPTO_TYPE NONE_CRYPT
#define WMI_MAX_CRYPTO_TYPE (AES_CRYPT + 1)

#ifdef WAPI_ENABLE
#undef WMI_MAX_CRYPTO_TYPE
#define WMI_MAX_CRYPTO_TYPE (WAPI_CRYPT + 1)
#endif /* WAPI_ENABLE */

#ifdef WAPI_ENABLE
#define IW_ENCODE_ALG_SM4       0x20
#define IW_AUTH_WAPI_ENABLED    0x20
#endif

#define WMI_MIN_KEY_INDEX   0
#define WMI_MAX_KEY_INDEX   3

#ifdef WAPI_ENABLE
#undef WMI_MAX_KEY_INDEX
#define WMI_MAX_KEY_INDEX   7 /* wapi grpKey 0-3, prwKey 4-7 */
#endif /* WAPI_ENABLE */

#define WMI_MAX_KEY_LEN     32

#define WMI_MAX_SSID_LEN    32

typedef enum {
    CONNECT_ASSOC_POLICY_USER           = 0x0001,
    CONNECT_SEND_REASSOC                = 0x0002,
    CONNECT_IGNORE_WPAx_GROUP_CIPHER    = 0x0004,
    CONNECT_PROFILE_MATCH_DONE          = 0x0008,
    CONNECT_IGNORE_AAC_BEACON           = 0x0010,
    CONNECT_CSA_FOLLOW_BSS              = 0x0020,
    CONNECT_DO_WPA_OFFLOAD              = 0x0040,
    CONNECT_DO_NOT_DEAUTH               = 0x0080,
} WMI_CONNECT_CTRL_FLAGS_BITS;

#define DEFAULT_CONNECT_CTRL_FLAGS    (CONNECT_CSA_FOLLOW_BSS)

typedef PREPACK struct {
    u8 networkType;
    u8 dot11AuthMode;
    u8 authMode;
    u8 pairwiseCryptoType;
    u8 pairwiseCryptoLen;
    u8 groupCryptoType;
    u8 groupCryptoLen;
    u8 ssidLength;
    u8     ssid[WMI_MAX_SSID_LEN];
    u16 channel;
    u8 bssid[ATH_MAC_LEN];
    u32 ctrl_flags;
} POSTPACK WMI_CONNECT_CMD;

/*
 * WMI_RECONNECT_CMDID
 */
typedef PREPACK struct {
    u16 channel;                    /* hint */
    u8 bssid[ATH_MAC_LEN];         /* mandatory if set */
} POSTPACK WMI_RECONNECT_CMD;

#define WMI_PMK_LEN     32
typedef PREPACK struct {
    u8 pmk[WMI_PMK_LEN];
} POSTPACK WMI_SET_PMK_CMD;

/*
 * WMI_SET_EXCESS_TX_RETRY_THRES_CMDID
 */
typedef PREPACK struct {
    u32 threshold;
} POSTPACK WMI_SET_EXCESS_TX_RETRY_THRES_CMD;

/*
 * WMI_ADD_CIPHER_KEY_CMDID
 */
typedef enum {
    PAIRWISE_USAGE      = 0x00,
    GROUP_USAGE         = 0x01,
    TX_USAGE            = 0x02,     /* default Tx Key - Static WEP only */
} KEY_USAGE;

/*
 * Bit Flag
 * Bit 0 - Initialise TSC - default is Initialize
 */
#define KEY_OP_INIT_TSC       0x01
#define KEY_OP_INIT_RSC       0x02
#ifdef WAPI_ENABLE
#define KEY_OP_INIT_WAPIPN    0x10
#endif /* WAPI_ENABLE */

#define KEY_OP_INIT_VAL     0x03     /* Default Initialise the TSC & RSC */
#define KEY_OP_VALID_MASK   0x03

typedef PREPACK struct {
    u8 keyIndex;
    u8 keyType;
    u8 keyUsage;           /* KEY_USAGE */
    u8 keyLength;
    u8 keyRSC[8];          /* key replay sequence counter */
    u8 key[WMI_MAX_KEY_LEN];
    u8 key_op_ctrl;       /* Additional Key Control information */
    u8 key_macaddr[ATH_MAC_LEN];
} POSTPACK WMI_ADD_CIPHER_KEY_CMD;

/*
 * WMI_DELETE_CIPHER_KEY_CMDID
 */
typedef PREPACK struct {
    u8 keyIndex;
} POSTPACK WMI_DELETE_CIPHER_KEY_CMD;

#define WMI_KRK_LEN     16
/*
 * WMI_ADD_KRK_CMDID
 */
typedef PREPACK struct {
    u8 krk[WMI_KRK_LEN];
} POSTPACK WMI_ADD_KRK_CMD;

/*
 * WMI_SET_TKIP_COUNTERMEASURES_CMDID
 */
typedef enum {
    WMI_TKIP_CM_DISABLE = 0x0,
    WMI_TKIP_CM_ENABLE  = 0x1,
} WMI_TKIP_CM_CONTROL;

typedef PREPACK struct {
    u8 cm_en;                     /* WMI_TKIP_CM_CONTROL */
} POSTPACK WMI_SET_TKIP_COUNTERMEASURES_CMD;

/*
 * WMI_SET_PMKID_CMDID
 */

#define WMI_PMKID_LEN 16

typedef enum {
   PMKID_DISABLE = 0,
   PMKID_ENABLE  = 1,
} PMKID_ENABLE_FLG;

typedef PREPACK struct {
    u8 bssid[ATH_MAC_LEN];
    u8 enable;                 /* PMKID_ENABLE_FLG */
    u8 pmkid[WMI_PMKID_LEN];
} POSTPACK WMI_SET_PMKID_CMD;

/*
 * WMI_START_SCAN_CMD
 */
typedef enum {
    WMI_LONG_SCAN  = 0,
    WMI_SHORT_SCAN = 1,
} WMI_SCAN_TYPE;

typedef PREPACK struct {
    u32   forceFgScan;
    u32   isLegacy;        /* For Legacy Cisco AP compatibility */
    u32 homeDwellTime;   /* Maximum duration in the home channel(milliseconds) */
    u32 forceScanInterval;    /* Time interval between scans (milliseconds)*/
    u8 scanType;           /* WMI_SCAN_TYPE */
    u8 numChannels;            /* how many channels follow */
    u16 channelList[1];         /* channels in Mhz */
} POSTPACK WMI_START_SCAN_CMD;

/*
 * WMI_SET_SCAN_PARAMS_CMDID
 */
#define WMI_SHORTSCANRATIO_DEFAULT      3
/* 
 *  Warning: ScanCtrlFlag value of 0xFF is used to disable all flags in WMI_SCAN_PARAMS_CMD 
 *  Do not add any more flags to WMI_SCAN_CTRL_FLAG_BITS
 */
typedef enum {
    CONNECT_SCAN_CTRL_FLAGS = 0x01,    /* set if can scan in the Connect cmd */
    SCAN_CONNECTED_CTRL_FLAGS = 0x02,  /* set if scan for the SSID it is */
                                       /* already connected to */
    ACTIVE_SCAN_CTRL_FLAGS = 0x04,     /* set if enable active scan */
    ROAM_SCAN_CTRL_FLAGS = 0x08,       /* set if enable roam scan when bmiss and lowrssi */
    REPORT_BSSINFO_CTRL_FLAGS = 0x10,   /* set if follows customer BSSINFO reporting rule */
    ENABLE_AUTO_CTRL_FLAGS = 0x20,      /* if disabled, target doesn't
                                          scan after a disconnect event  */
    ENABLE_SCAN_ABORT_EVENT = 0x40      /* Scan complete event with canceled status will be generated when a scan is prempted before it gets completed */
} WMI_SCAN_CTRL_FLAGS_BITS;

#define CAN_SCAN_IN_CONNECT(flags)      (flags & CONNECT_SCAN_CTRL_FLAGS)
#define CAN_SCAN_CONNECTED(flags)       (flags & SCAN_CONNECTED_CTRL_FLAGS)
#define ENABLE_ACTIVE_SCAN(flags)       (flags & ACTIVE_SCAN_CTRL_FLAGS)
#define ENABLE_ROAM_SCAN(flags)         (flags & ROAM_SCAN_CTRL_FLAGS)
#define CONFIG_REPORT_BSSINFO(flags)     (flags & REPORT_BSSINFO_CTRL_FLAGS)
#define IS_AUTO_SCAN_ENABLED(flags)      (flags & ENABLE_AUTO_CTRL_FLAGS)
#define SCAN_ABORT_EVENT_ENABLED(flags) (flags & ENABLE_SCAN_ABORT_EVENT)

#define DEFAULT_SCAN_CTRL_FLAGS         (CONNECT_SCAN_CTRL_FLAGS| SCAN_CONNECTED_CTRL_FLAGS| ACTIVE_SCAN_CTRL_FLAGS| ROAM_SCAN_CTRL_FLAGS | ENABLE_AUTO_CTRL_FLAGS)


typedef PREPACK struct {
    u16 fg_start_period;        /* seconds */
    u16 fg_end_period;          /* seconds */
    u16 bg_period;              /* seconds */
    u16 maxact_chdwell_time;    /* msec */
    u16 pas_chdwell_time;       /* msec */
    u8 shortScanRatio;         /* how many shorts scan for one long */
    u8 scanCtrlFlags;
    u16 minact_chdwell_time;    /* msec */
    u16 maxact_scan_per_ssid;   /* max active scans per ssid */
    u32 max_dfsch_act_time;  /* msecs */
} POSTPACK WMI_SCAN_PARAMS_CMD;

/*
 * WMI_SET_BSS_FILTER_CMDID
 */
typedef enum {
    NONE_BSS_FILTER = 0x0,              /* no beacons forwarded */
    ALL_BSS_FILTER,                     /* all beacons forwarded */
    PROFILE_FILTER,                     /* only beacons matching profile */
    ALL_BUT_PROFILE_FILTER,             /* all but beacons matching profile */
    CURRENT_BSS_FILTER,                 /* only beacons matching current BSS */
    ALL_BUT_BSS_FILTER,                 /* all but beacons matching BSS */
    PROBED_SSID_FILTER,                 /* beacons matching probed ssid */
    LAST_BSS_FILTER,                    /* marker only */
} WMI_BSS_FILTER;

typedef PREPACK struct {
    u8 bssFilter;                      /* see WMI_BSS_FILTER */
    u8 reserved1;                      /* For alignment */
    u16 reserved2;                      /* For alignment */
    u32 ieMask;
} POSTPACK WMI_BSS_FILTER_CMD;

/*
 * WMI_SET_PROBED_SSID_CMDID
 */
#define MAX_PROBED_SSID_INDEX   9

typedef enum {
    DISABLE_SSID_FLAG  = 0,                  /* disables entry */
    SPECIFIC_SSID_FLAG = 0x01,               /* probes specified ssid */
    ANY_SSID_FLAG      = 0x02,               /* probes for any ssid */
} WMI_SSID_FLAG;

typedef PREPACK struct {
    u8 entryIndex;                     /* 0 to MAX_PROBED_SSID_INDEX */
    u8 flag;                           /* WMI_SSID_FLG */
    u8 ssidLength;
    u8 ssid[32];
} POSTPACK WMI_PROBED_SSID_CMD;

/*
 * WMI_SET_LISTEN_INT_CMDID
 * The Listen interval is between 15 and 3000 TUs
 */
#define MIN_LISTEN_INTERVAL 15
#define MAX_LISTEN_INTERVAL 5000
#define MIN_LISTEN_BEACONS 1
#define MAX_LISTEN_BEACONS 50

typedef PREPACK struct {
    u16 listenInterval;
    u16 numBeacons;
} POSTPACK WMI_LISTEN_INT_CMD;

/*
 * WMI_SET_BEACON_INT_CMDID
 */
typedef PREPACK struct {
    u16 beaconInterval;
} POSTPACK WMI_BEACON_INT_CMD;

/*
 * WMI_SET_BMISS_TIME_CMDID
 * valid values are between 1000 and 5000 TUs
 */

#define MIN_BMISS_TIME     1000
#define MAX_BMISS_TIME     5000
#define MIN_BMISS_BEACONS  1
#define MAX_BMISS_BEACONS  50

typedef PREPACK struct {
    u16 bmissTime;
    u16 numBeacons;
} POSTPACK WMI_BMISS_TIME_CMD;

/*
 * WMI_SET_POWER_MODE_CMDID
 */
typedef enum {
    REC_POWER = 0x01,
    MAX_PERF_POWER,
} WMI_POWER_MODE;

typedef PREPACK struct {
    u8 powerMode;      /* WMI_POWER_MODE */
} POSTPACK WMI_POWER_MODE_CMD;

typedef PREPACK struct {
    s8 status;      /* WMI_SET_PARAMS_REPLY */
} POSTPACK WMI_SET_PARAMS_REPLY;

typedef PREPACK struct {
    u32 opcode;
    u32 length;
    char buffer[1];      /* WMI_SET_PARAMS */
} POSTPACK WMI_SET_PARAMS_CMD;

typedef PREPACK struct {
    u8 multicast_mac[ATH_MAC_LEN];      /* WMI_SET_MCAST_FILTER */
} POSTPACK WMI_SET_MCAST_FILTER_CMD;

typedef PREPACK struct {
    u8 enable;      /* WMI_MCAST_FILTER */
} POSTPACK WMI_MCAST_FILTER_CMD;

/*
 * WMI_SET_POWER_PARAMS_CMDID
 */
typedef enum {
    IGNORE_DTIM = 0x01,
    NORMAL_DTIM = 0x02,
    STICK_DTIM  = 0x03,
    AUTO_DTIM   = 0x04,
} WMI_DTIM_POLICY;

/* Policy to determnine whether TX should wakeup WLAN if sleeping */
typedef enum {
    TX_WAKEUP_UPON_SLEEP = 1,
    TX_DONT_WAKEUP_UPON_SLEEP = 2
} WMI_TX_WAKEUP_POLICY_UPON_SLEEP;

/*
 * Policy to determnine whether power save failure event should be sent to
 * host during scanning
 */
typedef enum {
    SEND_POWER_SAVE_FAIL_EVENT_ALWAYS = 1,
    IGNORE_POWER_SAVE_FAIL_EVENT_DURING_SCAN = 2,
} POWER_SAVE_FAIL_EVENT_POLICY;

typedef PREPACK struct {
    u16 idle_period;             /* msec */
    u16 pspoll_number;
    u16 dtim_policy;
    u16 tx_wakeup_policy;
    u16 num_tx_to_wakeup;
    u16 ps_fail_event_policy;
} POSTPACK WMI_POWER_PARAMS_CMD;

/* Adhoc power save types */
typedef enum {
    ADHOC_PS_DISABLE=1,
    ADHOC_PS_ATH=2,
    ADHOC_PS_IEEE=3,
    ADHOC_PS_OTHER=4,
} WMI_ADHOC_PS_TYPE;

typedef PREPACK struct {
    u8 power_saving;
    u8 ttl; /* number of beacon periods */
    u16 atim_windows;          /* msec */
    u16 timeout_value;         /* msec */
} POSTPACK WMI_IBSS_PM_CAPS_CMD;

/* AP power save types */
typedef enum {
    AP_PS_DISABLE=1,
    AP_PS_ATH=2,
} WMI_AP_PS_TYPE;

typedef PREPACK struct {
    u32 idle_time;   /* in msec */
    u32 ps_period;   /* in usec */
    u8 sleep_period; /* in ps periods */
    u8 psType;
} POSTPACK WMI_AP_PS_CMD;

/*
 * WMI_SET_POWERSAVE_TIMERS_POLICY_CMDID
 */
typedef enum {
    IGNORE_TIM_ALL_QUEUES_APSD = 0,
    PROCESS_TIM_ALL_QUEUES_APSD = 1,
    IGNORE_TIM_SIMULATED_APSD = 2,
    PROCESS_TIM_SIMULATED_APSD = 3,
} APSD_TIM_POLICY;

typedef PREPACK struct {
    u16 psPollTimeout;          /* msec */
    u16 triggerTimeout;         /* msec */
    u32 apsdTimPolicy;      /* TIM behavior with  ques APSD enabled. Default is IGNORE_TIM_ALL_QUEUES_APSD */
    u32 simulatedAPSDTimPolicy;      /* TIM behavior with  simulated APSD enabled. Default is PROCESS_TIM_SIMULATED_APSD */
} POSTPACK WMI_POWERSAVE_TIMERS_POLICY_CMD;

/*
 * WMI_SET_VOICE_PKT_SIZE_CMDID
 */
typedef PREPACK struct {
    u16 voicePktSize;
} POSTPACK WMI_SET_VOICE_PKT_SIZE_CMD;

/*
 * WMI_SET_MAX_SP_LEN_CMDID
 */
typedef enum {
    DELIVER_ALL_PKT = 0x0,
    DELIVER_2_PKT = 0x1,
    DELIVER_4_PKT = 0x2,
    DELIVER_6_PKT = 0x3,
} APSD_SP_LEN_TYPE;

typedef PREPACK struct {
    u8 maxSPLen;
} POSTPACK WMI_SET_MAX_SP_LEN_CMD;

/*
 * WMI_SET_DISC_TIMEOUT_CMDID
 */
typedef PREPACK struct {
    u8 disconnectTimeout;          /* seconds */
} POSTPACK WMI_DISC_TIMEOUT_CMD;

typedef enum {
    UPLINK_TRAFFIC = 0,
    DNLINK_TRAFFIC = 1,
    BIDIR_TRAFFIC = 2,
} DIR_TYPE;

typedef enum {
    DISABLE_FOR_THIS_AC = 0,
    ENABLE_FOR_THIS_AC  = 1,
    ENABLE_FOR_ALL_AC   = 2,
} VOICEPS_CAP_TYPE;

typedef enum {
    TRAFFIC_TYPE_APERIODIC = 0,
    TRAFFIC_TYPE_PERIODIC = 1,
}TRAFFIC_TYPE;

/*
 * WMI_SYNCHRONIZE_CMDID
 */
typedef PREPACK struct {
    u8 dataSyncMap;
} POSTPACK WMI_SYNC_CMD;

/*
 * WMI_CREATE_PSTREAM_CMDID
 */
typedef PREPACK struct {
    u32 minServiceInt;           /* in milli-sec */
    u32 maxServiceInt;           /* in milli-sec */
    u32 inactivityInt;           /* in milli-sec */
    u32 suspensionInt;           /* in milli-sec */
    u32 serviceStartTime;
    u32 minDataRate;             /* in bps */
    u32 meanDataRate;            /* in bps */
    u32 peakDataRate;            /* in bps */
    u32 maxBurstSize;
    u32 delayBound;
    u32 minPhyRate;              /* in bps */
    u32 sba;
    u32 mediumTime;
    u16 nominalMSDU;             /* in octects */
    u16 maxMSDU;                 /* in octects */
    u8 trafficClass;
    u8 trafficDirection;        /* DIR_TYPE */
    u8 rxQueueNum;
    u8 trafficType;             /* TRAFFIC_TYPE */
    u8 voicePSCapability;       /* VOICEPS_CAP_TYPE */
    u8 tsid;
    u8 userPriority;            /* 802.1D user priority */
    u8 nominalPHY;              /* nominal phy rate */
} POSTPACK WMI_CREATE_PSTREAM_CMD;

/*
 * WMI_DELETE_PSTREAM_CMDID
 */
typedef PREPACK struct {
    u8 txQueueNumber;
    u8 rxQueueNumber;
    u8 trafficDirection;
    u8 trafficClass;
    u8 tsid;
} POSTPACK WMI_DELETE_PSTREAM_CMD;

/*
 * WMI_SET_CHANNEL_PARAMS_CMDID
 */
typedef enum {
    WMI_11A_MODE  = 0x1,
    WMI_11G_MODE  = 0x2,
    WMI_11AG_MODE = 0x3,
    WMI_11B_MODE  = 0x4,
    WMI_11GONLY_MODE = 0x5,    
} WMI_PHY_MODE;

#define WMI_MAX_CHANNELS        32

typedef PREPACK struct {
    u8 reserved1;
    u8 scanParam;              /* set if enable scan */
    u8 phyMode;                /* see WMI_PHY_MODE */
    u8 numChannels;            /* how many channels follow */
    u16 channelList[1];         /* channels in Mhz */
} POSTPACK WMI_CHANNEL_PARAMS_CMD;


/*
 *  WMI_RSSI_THRESHOLD_PARAMS_CMDID
 *  Setting the polltime to 0 would disable polling.
 *  Threshold values are in the ascending order, and should agree to:
 *  (lowThreshold_lowerVal < lowThreshold_upperVal < highThreshold_lowerVal
 *      < highThreshold_upperVal)
 */

typedef PREPACK struct WMI_RSSI_THRESHOLD_PARAMS{
    u32 pollTime;               /* Polling time as a factor of LI */
    s16 thresholdAbove1_Val;          /* lowest of upper */
    s16 thresholdAbove2_Val;
    s16 thresholdAbove3_Val;
    s16 thresholdAbove4_Val;
    s16 thresholdAbove5_Val;
    s16 thresholdAbove6_Val;          /* highest of upper */
    s16 thresholdBelow1_Val;         /* lowest of bellow */
    s16 thresholdBelow2_Val;
    s16 thresholdBelow3_Val;
    s16 thresholdBelow4_Val;
    s16 thresholdBelow5_Val;
    s16 thresholdBelow6_Val;         /* highest of bellow */
    u8 weight;                  /* "alpha" */
    u8 reserved[3];
} POSTPACK  WMI_RSSI_THRESHOLD_PARAMS_CMD;

/*
 *  WMI_SNR_THRESHOLD_PARAMS_CMDID
 *  Setting the polltime to 0 would disable polling.
 */

typedef PREPACK struct WMI_SNR_THRESHOLD_PARAMS{
    u32 pollTime;               /* Polling time as a factor of LI */
    u8 weight;                  /* "alpha" */
    u8 thresholdAbove1_Val;      /* lowest of uppper*/
    u8 thresholdAbove2_Val;
    u8 thresholdAbove3_Val;
    u8 thresholdAbove4_Val;      /* highest of upper */
    u8 thresholdBelow1_Val;     /* lowest of bellow */
    u8 thresholdBelow2_Val;
    u8 thresholdBelow3_Val;
    u8 thresholdBelow4_Val;     /* highest of bellow */
    u8 reserved[3];
} POSTPACK WMI_SNR_THRESHOLD_PARAMS_CMD;

/*
 *  WMI_LQ_THRESHOLD_PARAMS_CMDID
 */
typedef PREPACK struct WMI_LQ_THRESHOLD_PARAMS {
    u8 enable;
    u8 thresholdAbove1_Val;
    u8 thresholdAbove2_Val;
    u8 thresholdAbove3_Val;
    u8 thresholdAbove4_Val;
    u8 thresholdBelow1_Val;
    u8 thresholdBelow2_Val;
    u8 thresholdBelow3_Val;
    u8 thresholdBelow4_Val;
    u8 reserved[3];
} POSTPACK  WMI_LQ_THRESHOLD_PARAMS_CMD;

typedef enum {
    WMI_LPREAMBLE_DISABLED = 0,
    WMI_LPREAMBLE_ENABLED
} WMI_LPREAMBLE_STATUS;

typedef enum {
    WMI_IGNORE_BARKER_IN_ERP = 0,
    WMI_DONOT_IGNORE_BARKER_IN_ERP
} WMI_PREAMBLE_POLICY;

typedef PREPACK struct {
    u8 status;
    u8 preamblePolicy;
}POSTPACK WMI_SET_LPREAMBLE_CMD;

typedef PREPACK struct {
    u16 threshold;
}POSTPACK WMI_SET_RTS_CMD;

/*
 *  WMI_TARGET_ERROR_REPORT_BITMASK_CMDID
 *  Sets the error reporting event bitmask in target. Target clears it
 *  upon an error. Subsequent errors are counted, but not reported
 *  via event, unless the bitmask is set again.
 */
typedef PREPACK struct {
    u32 bitmask;
} POSTPACK  WMI_TARGET_ERROR_REPORT_BITMASK;

/*
 * WMI_SET_TX_PWR_CMDID
 */
typedef PREPACK struct {
    u8 dbM;                  /* in dbM units */
} POSTPACK WMI_SET_TX_PWR_CMD, WMI_TX_PWR_REPLY;

/*
 * WMI_SET_ASSOC_INFO_CMDID
 *
 * A maximum of 2 private IEs can be sent in the [Re]Assoc request.
 * A 3rd one, the CCX version IE can also be set from the host.
 */
#define WMI_MAX_ASSOC_INFO_TYPE    2
#define WMI_CCX_VER_IE             2 /* ieType to set CCX Version IE */

#define WMI_MAX_ASSOC_INFO_LEN     240

typedef PREPACK struct {
    u8 ieType;
    u8 bufferSize;
    u8 assocInfo[1];       /* up to WMI_MAX_ASSOC_INFO_LEN */
} POSTPACK WMI_SET_ASSOC_INFO_CMD;


/*
 * WMI_GET_TX_PWR_CMDID does not take any parameters
 */

/*
 * WMI_ADD_BAD_AP_CMDID
 */
#define WMI_MAX_BAD_AP_INDEX      1

typedef PREPACK struct {
    u8 badApIndex;         /* 0 to WMI_MAX_BAD_AP_INDEX */
    u8 bssid[ATH_MAC_LEN];
} POSTPACK WMI_ADD_BAD_AP_CMD;

/*
 * WMI_DELETE_BAD_AP_CMDID
 */
typedef PREPACK struct {
    u8 badApIndex;         /* 0 to WMI_MAX_BAD_AP_INDEX */
} POSTPACK WMI_DELETE_BAD_AP_CMD;

/*
 * WMI_SET_ACCESS_PARAMS_CMDID
 */
#define WMI_DEFAULT_TXOP_ACPARAM    0       /* implies one MSDU */
#define WMI_DEFAULT_ECWMIN_ACPARAM  4       /* corresponds to CWmin of 15 */
#define WMI_DEFAULT_ECWMAX_ACPARAM  10      /* corresponds to CWmax of 1023 */
#define WMI_MAX_CW_ACPARAM          15      /* maximum eCWmin or eCWmax */
#define WMI_DEFAULT_AIFSN_ACPARAM   2
#define WMI_MAX_AIFSN_ACPARAM       15
typedef PREPACK struct {
    u16 txop;                      /* in units of 32 usec */
    u8 eCWmin;
    u8 eCWmax;
    u8 aifsn;
    u8 ac;
} POSTPACK WMI_SET_ACCESS_PARAMS_CMD;


/*
 * WMI_SET_RETRY_LIMITS_CMDID
 *
 * This command is used to customize the number of retries the
 * wlan device will perform on a given frame.
 */
#define WMI_MIN_RETRIES 2
#define WMI_MAX_RETRIES 13
typedef enum {
    MGMT_FRAMETYPE    = 0,
    CONTROL_FRAMETYPE = 1,
    DATA_FRAMETYPE    = 2
} WMI_FRAMETYPE;

typedef PREPACK struct {
    u8 frameType;                      /* WMI_FRAMETYPE */
    u8 trafficClass;                   /* applies only to DATA_FRAMETYPE */
    u8 maxRetries;
    u8 enableNotify;
} POSTPACK WMI_SET_RETRY_LIMITS_CMD;

/*
 * WMI_SET_ROAM_CTRL_CMDID
 *
 * This command is used to influence the Roaming behaviour
 * Set the host biases of the BSSs before setting the roam mode as bias
 * based.
 */

/*
 * Different types of Roam Control
 */

typedef enum {
        WMI_FORCE_ROAM          = 1,      /* Roam to the specified BSSID */
        WMI_SET_ROAM_MODE       = 2,      /* default ,progd bias, no roam */
        WMI_SET_HOST_BIAS       = 3,     /* Set the Host Bias */
        WMI_SET_LOWRSSI_SCAN_PARAMS = 4, /* Set lowrssi Scan parameters */
} WMI_ROAM_CTRL_TYPE;

#define WMI_MIN_ROAM_CTRL_TYPE WMI_FORCE_ROAM
#define WMI_MAX_ROAM_CTRL_TYPE WMI_SET_LOWRSSI_SCAN_PARAMS

/*
 * ROAM MODES
 */

typedef enum {
        WMI_DEFAULT_ROAM_MODE   = 1,  /* RSSI based ROAM */
        WMI_HOST_BIAS_ROAM_MODE = 2, /* HOST BIAS based ROAM */
        WMI_LOCK_BSS_MODE  = 3  /* Lock to the Current BSS - no Roam */
} WMI_ROAM_MODE;

/*
 * BSS HOST BIAS INFO
 */

typedef PREPACK struct {
        u8 bssid[ATH_MAC_LEN];
        s8 bias;
} POSTPACK WMI_BSS_BIAS;

typedef PREPACK struct {
        u8 numBss;
        WMI_BSS_BIAS bssBias[1];
} POSTPACK WMI_BSS_BIAS_INFO;

typedef PREPACK struct WMI_LOWRSSI_SCAN_PARAMS {
        u16 lowrssi_scan_period;
        s16 lowrssi_scan_threshold;
        s16 lowrssi_roam_threshold;
        u8 roam_rssi_floor;
        u8 reserved[1];              /* For alignment */
} POSTPACK WMI_LOWRSSI_SCAN_PARAMS;

typedef PREPACK struct {
    PREPACK union {
        u8 bssid[ATH_MAC_LEN]; /* WMI_FORCE_ROAM */
        u8 roamMode;           /* WMI_SET_ROAM_MODE  */
        WMI_BSS_BIAS_INFO bssBiasInfo; /* WMI_SET_HOST_BIAS */
        WMI_LOWRSSI_SCAN_PARAMS lrScanParams;
    } POSTPACK info;
    u8 roamCtrlType ;
} POSTPACK WMI_SET_ROAM_CTRL_CMD;

/*
 * WMI_SET_BT_WLAN_CONN_PRECEDENCE_CMDID
 */
typedef enum {
    BT_WLAN_CONN_PRECDENCE_WLAN=0,  /* Default */
    BT_WLAN_CONN_PRECDENCE_PAL,
} BT_WLAN_CONN_PRECEDENCE;

typedef PREPACK struct {
    u8 precedence;
} POSTPACK WMI_SET_BT_WLAN_CONN_PRECEDENCE;

/*
 * WMI_ENABLE_RM_CMDID
 */
typedef PREPACK struct {
        u32 enable_radio_measurements;
} POSTPACK WMI_ENABLE_RM_CMD;

/*
 * WMI_SET_MAX_OFFHOME_DURATION_CMDID
 */
typedef PREPACK struct {
        u8 max_offhome_duration;
} POSTPACK WMI_SET_MAX_OFFHOME_DURATION_CMD;

typedef PREPACK struct {
    u32 frequency;
    u8 threshold;
} POSTPACK WMI_SET_HB_CHALLENGE_RESP_PARAMS_CMD;
/*---------------------- BTCOEX RELATED -------------------------------------*/
/*----------------------COMMON to AR6002 and AR6003 -------------------------*/
typedef enum {
    BT_STREAM_UNDEF = 0,
    BT_STREAM_SCO,             /* SCO stream */
    BT_STREAM_A2DP,            /* A2DP stream */
    BT_STREAM_SCAN,            /* BT Discovery or Page */
    BT_STREAM_ESCO,
    BT_STREAM_MAX
} BT_STREAM_TYPE;

typedef enum {
    BT_PARAM_SCO_PSPOLL_LATENCY_ONE_FOURTH =1,
    BT_PARAM_SCO_PSPOLL_LATENCY_HALF,
    BT_PARAM_SCO_PSPOLL_LATENCY_THREE_FOURTH,
} BT_PARAMS_SCO_PSPOLL_LATENCY;

typedef enum {
    BT_PARAMS_SCO_STOMP_SCO_NEVER =1,
    BT_PARAMS_SCO_STOMP_SCO_ALWAYS,
    BT_PARAMS_SCO_STOMP_SCO_IN_LOWRSSI,
} BT_PARAMS_SCO_STOMP_RULES;

typedef enum {
    BT_STATUS_UNDEF = 0,
    BT_STATUS_ON,
    BT_STATUS_OFF,
    BT_STATUS_MAX
} BT_STREAM_STATUS;

typedef PREPACK struct {
    u8 streamType;
    u8 status;
} POSTPACK WMI_SET_BT_STATUS_CMD;

typedef enum {
    BT_ANT_TYPE_UNDEF=0,
    BT_ANT_TYPE_DUAL,
    BT_ANT_TYPE_SPLITTER,
    BT_ANT_TYPE_SWITCH,
    BT_ANT_TYPE_HIGH_ISO_DUAL
} BT_ANT_FRONTEND_CONFIG;

typedef enum {
    BT_COLOCATED_DEV_BTS4020=0,
    BT_COLCATED_DEV_CSR ,
    BT_COLOCATED_DEV_VALKYRIE
} BT_COLOCATED_DEV_TYPE;

/*********************** Applicable to AR6002 ONLY ******************************/

typedef enum {
    BT_PARAM_SCO = 1,         /* SCO stream parameters */
    BT_PARAM_A2DP ,
    BT_PARAM_ANTENNA_CONFIG,
    BT_PARAM_COLOCATED_BT_DEVICE,
    BT_PARAM_ACLCOEX,
    BT_PARAM_11A_SEPARATE_ANT,
    BT_PARAM_MAX
} BT_PARAM_TYPE;


#define BT_SCO_ALLOW_CLOSE_RANGE_OPT    (1 << 0)
#define BT_SCO_FORCE_AWAKE_OPT          (1 << 1)
#define BT_SCO_SET_RSSI_OVERRIDE(flags)        ((flags) |= (1 << 2))
#define BT_SCO_GET_RSSI_OVERRIDE(flags)        (((flags) >> 2) & 0x1)
#define BT_SCO_SET_RTS_OVERRIDE(flags)   ((flags) |= (1 << 3))
#define BT_SCO_GET_RTS_OVERRIDE(flags)   (((flags) >> 3) & 0x1)
#define BT_SCO_GET_MIN_LOW_RATE_CNT(flags)     (((flags) >> 8) & 0xFF)
#define BT_SCO_GET_MAX_LOW_RATE_CNT(flags)     (((flags) >> 16) & 0xFF)
#define BT_SCO_SET_MIN_LOW_RATE_CNT(flags,val) (flags) |= (((val) & 0xFF) << 8)
#define BT_SCO_SET_MAX_LOW_RATE_CNT(flags,val) (flags) |= (((val) & 0xFF) << 16)

typedef PREPACK struct {
    u32 numScoCyclesForceTrigger;  /* Number SCO cycles after which
                                           force a pspoll. default = 10 */
    u32 dataResponseTimeout;       /* Timeout Waiting for Downlink pkt
                                           in response for ps-poll,
                                           default = 10 msecs */
    u32 stompScoRules;
    u32 scoOptFlags;               /* SCO Options Flags :
                                            bits:     meaning:
                                             0        Allow Close Range Optimization
                                             1        Force awake during close range
                                             2        If set use host supplied RSSI for OPT
                                             3        If set use host supplied RTS COUNT for OPT
                                             4..7     Unused
                                             8..15    Low Data Rate Min Cnt
                                             16..23   Low Data Rate Max Cnt
                                        */

    u8 stompDutyCyleVal;           /* Sco cycles to limit ps-poll queuing
                                           if stomped */
    u8 stompDutyCyleMaxVal;        /*firm ware increases stomp duty cycle
                                          gradually uptill this value on need basis*/
    u8 psPollLatencyFraction;      /* Fraction of idle
                                           period, within which
                                           additional ps-polls
                                           can be queued */
    u8 noSCOSlots;                 /* Number of SCO Tx/Rx slots.
                                           HVx, EV3, 2EV3 = 2 */
    u8 noIdleSlots;                /* Number of Bluetooth idle slots between
                                           consecutive SCO Tx/Rx slots
                                           HVx, EV3 = 4
                                           2EV3 = 10 */
    u8 scoOptOffRssi;/*RSSI value below which we go to ps poll*/
    u8 scoOptOnRssi; /*RSSI value above which we reenter opt mode*/
    u8 scoOptRtsCount;
} POSTPACK BT_PARAMS_SCO;

#define BT_A2DP_ALLOW_CLOSE_RANGE_OPT  (1 << 0)
#define BT_A2DP_FORCE_AWAKE_OPT        (1 << 1)
#define BT_A2DP_SET_RSSI_OVERRIDE(flags)        ((flags) |= (1 << 2))
#define BT_A2DP_GET_RSSI_OVERRIDE(flags)        (((flags) >> 2) & 0x1)
#define BT_A2DP_SET_RTS_OVERRIDE(flags)   ((flags) |= (1 << 3))
#define BT_A2DP_GET_RTS_OVERRIDE(flags)   (((flags) >> 3) & 0x1)
#define BT_A2DP_GET_MIN_LOW_RATE_CNT(flags)     (((flags) >> 8) & 0xFF)
#define BT_A2DP_GET_MAX_LOW_RATE_CNT(flags)     (((flags) >> 16) & 0xFF)
#define BT_A2DP_SET_MIN_LOW_RATE_CNT(flags,val) (flags) |= (((val) & 0xFF) << 8)
#define BT_A2DP_SET_MAX_LOW_RATE_CNT(flags,val) (flags) |= (((val) & 0xFF) << 16)

typedef PREPACK struct {
    u32 a2dpWlanUsageLimit; /* MAX time firmware uses the medium for
                                    wlan, after it identifies the idle time
                                    default (30 msecs) */
    u32 a2dpBurstCntMin;   /* Minimum number of bluetooth data frames
                                   to replenish Wlan Usage  limit (default 3) */
    u32 a2dpDataRespTimeout;
    u32 a2dpOptFlags;      /* A2DP Option flags:
                                       bits:    meaning:
                                        0       Allow Close Range Optimization
                                        1       Force awake during close range
                                        2        If set use host supplied RSSI for OPT
                                        3        If set use host supplied RTS COUNT for OPT
                                        4..7    Unused
                                        8..15   Low Data Rate Min Cnt
                                        16..23  Low Data Rate Max Cnt
                                 */
    u8 isCoLocatedBtRoleMaster;
    u8 a2dpOptOffRssi;/*RSSI value below which we go to ps poll*/
    u8 a2dpOptOnRssi; /*RSSI value above which we reenter opt mode*/
    u8 a2dpOptRtsCount;
}POSTPACK BT_PARAMS_A2DP;

/* During BT ftp/ BT OPP or any another data based acl profile on bluetooth
   (non a2dp).*/
typedef PREPACK struct {
    u32 aclWlanMediumUsageTime;  /* Wlan usage time during Acl (non-a2dp)
                                       coexistence (default 30 msecs) */
    u32 aclBtMediumUsageTime;   /* Bt usage time during acl coexistence
                                       (default 30 msecs)*/
    u32 aclDataRespTimeout;
    u32 aclDetectTimeout;      /* ACL coexistence enabled if we get
                                       10 Pkts in X msec(default 100 msecs) */
    u32 aclmaxPktCnt;          /* No of ACL pkts to receive before
                                         enabling ACL coex */

}POSTPACK BT_PARAMS_ACLCOEX;

typedef PREPACK struct {
    PREPACK union {
        BT_PARAMS_SCO scoParams;
        BT_PARAMS_A2DP a2dpParams;
        BT_PARAMS_ACLCOEX  aclCoexParams;
        u8 antType;         /* 0 -Disabled (default)
                                     1 - BT_ANT_TYPE_DUAL
                                     2 - BT_ANT_TYPE_SPLITTER
                                     3 - BT_ANT_TYPE_SWITCH */
        u8 coLocatedBtDev;  /* 0 - BT_COLOCATED_DEV_BTS4020 (default)
                                     1 - BT_COLCATED_DEV_CSR
                                     2 - BT_COLOCATED_DEV_VALKYRIe
                                   */
    } POSTPACK info;
    u8 paramType ;
} POSTPACK WMI_SET_BT_PARAMS_CMD;

/************************ END AR6002 BTCOEX *******************************/
/*-----------------------AR6003 BTCOEX -----------------------------------*/

/*  ---------------WMI_SET_BTCOEX_FE_ANT_CMDID --------------------------*/
/* Indicates front end antenna configuration. This command needs to be issued
 * right after initialization and after WMI_SET_BTCOEX_COLOCATED_BT_DEV_CMDID.
 * AR6003 enables coexistence and antenna switching based on the configuration.
 */
typedef enum {
    WMI_BTCOEX_NOT_ENABLED = 0,
    WMI_BTCOEX_FE_ANT_SINGLE =1,
    WMI_BTCOEX_FE_ANT_DUAL=2,
    WMI_BTCOEX_FE_ANT_DUAL_HIGH_ISO=3,
    WMI_BTCOEX_FE_ANT_TYPE_MAX
}WMI_BTCOEX_FE_ANT_TYPE;

typedef PREPACK struct {
	u8 btcoexFeAntType; /* 1 - WMI_BTCOEX_FE_ANT_SINGLE for single antenna front end
                                2 - WMI_BTCOEX_FE_ANT_DUAL for dual antenna front end
                                    (for isolations less 35dB, for higher isolation there
                                    is not need to pass this command).
                                    (not implemented)
                              */
}POSTPACK WMI_SET_BTCOEX_FE_ANT_CMD;

/* -------------WMI_SET_BTCOEX_COLOCATED_BT_DEV_CMDID ----------------*/
/* Indicate the bluetooth chip to the firmware. Firmware can have different algorithm based
 * bluetooth chip type.Based on bluetooth device, different coexistence protocol would be used.
 */
typedef PREPACK struct {
	u8 btcoexCoLocatedBTdev; /*1 - Qcom BT (3 -wire PTA)
                                    2 - CSR BT  (3 wire PTA)
                                    3 - Atheros 3001 BT (3 wire PTA)
                                    4 - STE bluetooth (4-wire ePTA)
                                    5 - Atheros 3002 BT (4-wire MCI)
                                    defaults= 3 (Atheros 3001 BT )
                                    */
}POSTPACK WMI_SET_BTCOEX_COLOCATED_BT_DEV_CMD;

/* -------------WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG_CMDID ------------*/
/* Configuration parameters during bluetooth inquiry and page. Page configuration
 * is applicable only on interfaces which can distinguish page (applicable only for ePTA -
 * STE bluetooth).
 * Bluetooth inquiry start and end is indicated via WMI_SET_BTCOEX_BT_OPERATING_STATUS_CMDID.
 * During this the station will be  power-save mode.
 */
typedef PREPACK struct {
	u32 btInquiryDataFetchFrequency;/* The frequency of querying the AP for data
                                            (via pspoll) is configured by this parameter.
                                            "default = 10 ms" */

	u32 protectBmissDurPostBtInquiry;/* The firmware will continue to be in inquiry state
                                             for configured duration, after inquiry completion
                                             . This is to ensure other bluetooth transactions
                                             (RDP, SDP profiles, link key exchange ...etc)
                                             goes through smoothly without wifi stomping.
                                             default = 10 secs*/

	u32 maxpageStomp;                 /*Applicable only for STE-BT interface. Currently not
                                             used */
	u32 btInquiryPageFlag;           /* Not used */
}POSTPACK WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG_CMD;

/*---------------------WMI_SET_BTCOEX_SCO_CONFIG_CMDID ---------------*/
/* Configure  SCO parameters. These parameters would be used whenever firmware is indicated
 * of (e)SCO profile on bluetooth ( via WMI_SET_BTCOEX_BT_OPERATING_STATUS_CMDID).
 * Configration of BTCOEX_SCO_CONFIG data structure are common configuration and applies
 * ps-poll mode and opt mode.
 * Ps-poll Mode - Station is in power-save and retrieves downlink data between sco gaps.
 * Opt Mode - station is in awake state and access point can send data to station any time.
 * BTCOEX_PSPOLLMODE_SCO_CONFIG - Configuration applied only during ps-poll mode.
 * BTCOEX_OPTMODE_SCO_CONFIG - Configuration applied only during opt mode.
 */
#define WMI_SCO_CONFIG_FLAG_ALLOW_OPTIMIZATION   (1 << 0)
#define WMI_SCO_CONFIG_FLAG_IS_EDR_CAPABLE       (1 << 1)
#define WMI_SCO_CONFIG_FLAG_IS_BT_MASTER         (1 << 2)
#define WMI_SCO_CONFIG_FLAG_FW_DETECT_OF_PER     (1 << 3)
typedef PREPACK struct {
	u32 scoSlots;					/* Number of SCO Tx/Rx slots.
										   HVx, EV3, 2EV3 = 2 */
	u32 scoIdleSlots;				/* Number of Bluetooth idle slots between
										   consecutive SCO Tx/Rx slots
										   HVx, EV3 = 4
										   2EV3 = 10
                                         */
	u32 scoFlags;				   /* SCO Options Flags :
										  bits:	   meaning:
 										  0   Allow Close Range Optimization
 										  1   Is EDR capable or Not
 										  2   IS Co-located Bt role Master
                                          3   Firmware determines the periodicity of SCO.
							  			 */

    u32 linkId;                      /* applicable to STE-BT - not used */
}POSTPACK BTCOEX_SCO_CONFIG;

typedef PREPACK struct {
	u32 scoCyclesForceTrigger;	/* Number SCO cycles after which
											force a pspoll. default = 10 */
    u32 scoDataResponseTimeout;	 /* Timeout Waiting for Downlink pkt
											in response for ps-poll,
											default = 20 msecs */

	u32 scoStompDutyCyleVal;		 /* not implemented */

	u32 scoStompDutyCyleMaxVal;     /*Not implemented */

	u32 scoPsPollLatencyFraction; 	 /* Fraction of idle
											period, within which
											additional ps-polls can be queued
                                            1 - 1/4 of idle duration
                                            2 - 1/2 of idle duration
                                            3 - 3/4 of idle duration
                                            default =2 (1/2)
                                           */
}POSTPACK BTCOEX_PSPOLLMODE_SCO_CONFIG;

typedef PREPACK struct {
	u32 scoStompCntIn100ms;/*max number of SCO stomp in 100ms allowed in
                                   opt mode. If exceeds the configured value,
                                   switch to ps-poll mode
                                  default = 3 */

	u32 scoContStompMax;   /* max number of continuous stomp allowed in opt mode.
                                   if exceeded switch to pspoll mode
                                    default = 3 */

	u32 scoMinlowRateMbps; /* Low rate threshold */

	u32 scoLowRateCnt;     /* number of low rate pkts (< scoMinlowRateMbps) allowed in 100 ms.
                                   If exceeded switch/stay to ps-poll mode, lower stay in opt mode.
                                   default = 36
                                 */

	u32 scoHighPktRatio;   /*(Total Rx pkts in 100 ms + 1)/
                                  ((Total tx pkts in 100 ms - No of high rate pkts in 100 ms) + 1) in 100 ms,
                                  if exceeded switch/stay in opt mode and if lower switch/stay in  pspoll mode.
                                  default = 5 (80% of high rates)
                                 */

	u32 scoMaxAggrSize;    /* Max number of Rx subframes allowed in this mode. (Firmware re-negogiates
                                   max number of aggregates if it was negogiated to higher value
                                   default = 1
                                   Recommended value Basic rate headsets = 1, EDR (2-EV3)  =4.
                                 */
}POSTPACK BTCOEX_OPTMODE_SCO_CONFIG;

typedef PREPACK struct {
    u32 scanInterval;
    u32 maxScanStompCnt;
}POSTPACK BTCOEX_WLANSCAN_SCO_CONFIG;

typedef PREPACK struct {
	BTCOEX_SCO_CONFIG scoConfig;
	BTCOEX_PSPOLLMODE_SCO_CONFIG scoPspollConfig;
	BTCOEX_OPTMODE_SCO_CONFIG scoOptModeConfig;
	BTCOEX_WLANSCAN_SCO_CONFIG scoWlanScanConfig;
}POSTPACK WMI_SET_BTCOEX_SCO_CONFIG_CMD;

/* ------------------WMI_SET_BTCOEX_A2DP_CONFIG_CMDID -------------------*/
/* Configure A2DP profile parameters. These parameters would be used whenver firmware is indicated
 * of A2DP profile on bluetooth ( via WMI_SET_BTCOEX_BT_OPERATING_STATUS_CMDID).
 * Configuration of BTCOEX_A2DP_CONFIG data structure are common configuration and applies to
 * ps-poll mode and opt mode.
 * Ps-poll Mode - Station is in power-save and retrieves downlink data between a2dp data bursts.
 * Opt Mode - station is in power save during a2dp bursts and awake in the gaps.
 * BTCOEX_PSPOLLMODE_A2DP_CONFIG - Configuration applied only during ps-poll mode.
 * BTCOEX_OPTMODE_A2DP_CONFIG - Configuration applied only during opt mode.
 */

#define WMI_A2DP_CONFIG_FLAG_ALLOW_OPTIMIZATION    (1 << 0)
#define WMI_A2DP_CONFIG_FLAG_IS_EDR_CAPABLE        (1 << 1)
#define WMI_A2DP_CONFIG_FLAG_IS_BT_ROLE_MASTER     (1 << 2)
#define WMI_A2DP_CONFIG_FLAG_IS_A2DP_HIGH_PRI      (1 << 3)
#define WMI_A2DP_CONFIG_FLAG_FIND_BT_ROLE          (1 << 4)

typedef PREPACK struct {
    u32 a2dpFlags;      /* A2DP Option flags:
		                        bits:    meaning:
               		            0       Allow Close Range Optimization
       	                     	1       IS EDR capable
       	                     	2       IS Co-located Bt role Master
                                3       a2dp traffic is high priority
                                4       Fw detect the role of bluetooth.
                             */
	u32 linkId;         /* Applicable only to STE-BT - not used */

}POSTPACK BTCOEX_A2DP_CONFIG;

typedef PREPACK struct {
    u32 a2dpWlanMaxDur; /* MAX time firmware uses the medium for
                      			wlan, after it identifies the idle time
                                default (30 msecs) */

    u32 a2dpMinBurstCnt;   /* Minimum number of bluetooth data frames
                  				to replenish Wlan Usage  limit (default 3) */

    u32 a2dpDataRespTimeout; /* Max duration firmware waits for downlink
                                     by stomping on  bluetooth
                                     after ps-poll is acknowledged.
                                     default = 20 ms
                                   */
}POSTPACK BTCOEX_PSPOLLMODE_A2DP_CONFIG;

typedef PREPACK struct {
	u32 a2dpMinlowRateMbps;  /* Low rate threshold */

	u32 a2dpLowRateCnt;    /* number of low rate pkts (< a2dpMinlowRateMbps) allowed in 100 ms.
                                   If exceeded switch/stay to ps-poll mode, lower stay in opt mode.
                                   default = 36
                                 */

	u32 a2dpHighPktRatio;   /*(Total Rx pkts in 100 ms + 1)/
                                  ((Total tx pkts in 100 ms - No of high rate pkts in 100 ms) + 1) in 100 ms,
                                  if exceeded switch/stay in opt mode and if lower switch/stay in  pspoll mode.
                                  default = 5 (80% of high rates)
                                 */

	u32 a2dpMaxAggrSize;    /* Max number of Rx subframes allowed in this mode. (Firmware re-negogiates
                                   max number of aggregates if it was negogiated to higher value
                                   default = 1
                                  Recommended value Basic rate headsets = 1, EDR (2-EV3)  =8.
                                 */
	u32 a2dpPktStompCnt;    /*number of a2dp pkts that can be stomped per burst.
                                   default = 6*/

}POSTPACK BTCOEX_OPTMODE_A2DP_CONFIG;

typedef PREPACK struct {
	BTCOEX_A2DP_CONFIG a2dpConfig;
	BTCOEX_PSPOLLMODE_A2DP_CONFIG a2dppspollConfig;
	BTCOEX_OPTMODE_A2DP_CONFIG a2dpOptConfig;
}POSTPACK WMI_SET_BTCOEX_A2DP_CONFIG_CMD;

/*------------ WMI_SET_BTCOEX_ACLCOEX_CONFIG_CMDID---------------------*/
/* Configure non-A2dp ACL profile parameters.The starts of ACL profile can either be
 * indicated via WMI_SET_BTCOEX_BT_OPERATING_STATUS_CMDID orenabled via firmware detection
 *  which is configured via "aclCoexFlags".
 * Configration of BTCOEX_ACLCOEX_CONFIG data structure are common configuration and applies
 * ps-poll mode and opt mode.
 * Ps-poll Mode - Station is in power-save and retrieves downlink data during wlan medium.
 * Opt Mode - station is in power save during bluetooth medium time and awake during wlan duration.
 *             (Not implemented yet)
 *
 * BTCOEX_PSPOLLMODE_ACLCOEX_CONFIG - Configuration applied only during ps-poll mode.
 * BTCOEX_OPTMODE_ACLCOEX_CONFIG - Configuration applied only during opt mode.
 */

#define WMI_ACLCOEX_FLAGS_ALLOW_OPTIMIZATION   (1 << 0)
#define WMI_ACLCOEX_FLAGS_DISABLE_FW_DETECTION (1 << 1)

typedef PREPACK struct {
    u32 aclWlanMediumDur; 	    /* Wlan usage time during Acl (non-a2dp)
                     					coexistence (default 30 msecs)
                                    */

    u32 aclBtMediumDur; 	   /* Bt usage time during acl coexistence
					                     (default 30 msecs)
                                   */

	u32 aclDetectTimeout;	   /* BT activity observation time limit.
									  In this time duration, number of bt pkts are counted.
									  If the Cnt reaches "aclPktCntLowerLimit" value
									  for "aclIterToEnableCoex" iteration continuously,
									  firmware gets into ACL coexistence mode.
									  Similarly, if bt traffic count during ACL coexistence
									  has not reached "aclPktCntLowerLimit" continuously
									  for "aclIterToEnableCoex", then ACL coexistence is
									  disabled.
    								  -default 100 msecs
                                    */

	 u32 aclPktCntLowerLimit;   /* Acl Pkt Cnt to be received in duration of
										"aclDetectTimeout" for
										"aclIterForEnDis" times to enabling ACL coex.
                                        Similar logic is used to disable acl coexistence.
                                        (If "aclPktCntLowerLimit"  cnt of acl pkts
                                         are not seen by the for "aclIterForEnDis"
                                         then acl coexistence is disabled).
                                        default = 10
                                   */

	 u32 aclIterForEnDis;      /* number of Iteration of "aclPktCntLowerLimit" for Enabling and
                                       Disabling Acl Coexistence.
                                       default = 3
                                     */

	 u32 aclPktCntUpperLimit; /* This is upperBound limit, if there is more than
									  "aclPktCntUpperLimit" seen in "aclDetectTimeout",
									  ACL coexistence is enabled right away.
									  - default 15*/

	u32 aclCoexFlags;			/* A2DP Option flags:
		  	                          bits:    meaning:
       		                          0       Allow Close Range Optimization
                    		          1       disable Firmware detection
                                      (Currently supported configuration is aclCoexFlags =0)
                      			 	*/
	u32 linkId;                /* Applicable only for STE-BT - not used */

}POSTPACK BTCOEX_ACLCOEX_CONFIG;

typedef PREPACK struct {
    u32 aclDataRespTimeout;   /* Max duration firmware waits for downlink
                                      by stomping on  bluetooth
                                      after ps-poll is acknowledged.
                                     default = 20 ms */

}POSTPACK BTCOEX_PSPOLLMODE_ACLCOEX_CONFIG;


/* Not implemented yet*/
typedef PREPACK struct {
	u32 aclCoexMinlowRateMbps;
	u32 aclCoexLowRateCnt;
	u32 aclCoexHighPktRatio;
	u32 aclCoexMaxAggrSize;
	u32 aclPktStompCnt;
}POSTPACK BTCOEX_OPTMODE_ACLCOEX_CONFIG;

typedef PREPACK struct {
	BTCOEX_ACLCOEX_CONFIG aclCoexConfig;
	BTCOEX_PSPOLLMODE_ACLCOEX_CONFIG aclCoexPspollConfig;
	BTCOEX_OPTMODE_ACLCOEX_CONFIG aclCoexOptConfig;
}POSTPACK WMI_SET_BTCOEX_ACLCOEX_CONFIG_CMD;

/* -----------WMI_SET_BTCOEX_BT_OPERATING_STATUS_CMDID ------------------*/
typedef enum {
	WMI_BTCOEX_BT_PROFILE_SCO =1,
	WMI_BTCOEX_BT_PROFILE_A2DP,
	WMI_BTCOEX_BT_PROFILE_INQUIRY_PAGE,
	WMI_BTCOEX_BT_PROFILE_ACLCOEX,
}WMI_BTCOEX_BT_PROFILE;

typedef PREPACK struct {
	u32 btProfileType;
	u32 btOperatingStatus;
	u32 btLinkId;
}WMI_SET_BTCOEX_BT_OPERATING_STATUS_CMD;

/*--------------------- WMI_SET_BTCOEX_DEBUG_CMDID ---------------------*/
/* Used for firmware development and debugging */
typedef PREPACK struct {
	u32 btcoexDbgParam1;
	u32 btcoexDbgParam2;
	u32 btcoexDbgParam3;
	u32 btcoexDbgParam4;
	u32 btcoexDbgParam5;
}WMI_SET_BTCOEX_DEBUG_CMD;

/*---------------------WMI_GET_BTCOEX_CONFIG_CMDID --------------------- */
/* Command to firmware to get configuration parameters of the bt profile
 * reported via WMI_BTCOEX_CONFIG_EVENTID */
typedef PREPACK struct {
	u32 btProfileType; /* 1 - SCO
                               2 - A2DP
                               3 - INQUIRY_PAGE
                               4 - ACLCOEX
                            */
	u32 linkId;    /* not used */
}WMI_GET_BTCOEX_CONFIG_CMD;

/*------------------WMI_REPORT_BTCOEX_CONFIG_EVENTID------------------- */
/* Event from firmware to host, sent in response to WMI_GET_BTCOEX_CONFIG_CMDID
 * */
typedef PREPACK struct {
	u32 btProfileType;
	u32 linkId; /* not used */
	PREPACK union {
		WMI_SET_BTCOEX_SCO_CONFIG_CMD scoConfigCmd;
		WMI_SET_BTCOEX_A2DP_CONFIG_CMD a2dpConfigCmd;
		WMI_SET_BTCOEX_ACLCOEX_CONFIG_CMD aclcoexConfig;
        WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG_CMD btinquiryPageConfigCmd;
    } POSTPACK info;
} POSTPACK WMI_BTCOEX_CONFIG_EVENT;

/*------------- WMI_REPORT_BTCOEX_BTCOEX_STATS_EVENTID--------------------*/
/* Used for firmware development and debugging*/
typedef PREPACK struct {
	u32 highRatePktCnt;
	u32 firstBmissCnt;
	u32 psPollFailureCnt;
	u32 nullFrameFailureCnt;
	u32 optModeTransitionCnt;
}BTCOEX_GENERAL_STATS;

typedef PREPACK struct {
	u32 scoStompCntAvg;
	u32 scoStompIn100ms;
	u32 scoMaxContStomp;
	u32 scoAvgNoRetries;
	u32 scoMaxNoRetriesIn100ms;
}BTCOEX_SCO_STATS;

typedef PREPACK struct {
	u32 a2dpBurstCnt;
	u32 a2dpMaxBurstCnt;
	u32 a2dpAvgIdletimeIn100ms;
	u32 a2dpAvgStompCnt;
}BTCOEX_A2DP_STATS;

typedef PREPACK struct {
	u32 aclPktCntInBtTime;
	u32 aclStompCntInWlanTime;
	u32 aclPktCntIn100ms;
}BTCOEX_ACLCOEX_STATS;

typedef PREPACK struct {
	BTCOEX_GENERAL_STATS coexStats;
	BTCOEX_SCO_STATS scoStats;
	BTCOEX_A2DP_STATS a2dpStats;
	BTCOEX_ACLCOEX_STATS aclCoexStats;
}WMI_BTCOEX_STATS_EVENT;


/*--------------------------END OF BTCOEX -------------------------------------*/
typedef PREPACK struct {
    u32 sleepState;
}WMI_REPORT_SLEEP_STATE_EVENT;

typedef enum {
    WMI_REPORT_SLEEP_STATUS_IS_DEEP_SLEEP =0,
    WMI_REPORT_SLEEP_STATUS_IS_AWAKE
} WMI_REPORT_SLEEP_STATUS;
typedef enum {
    DISCONN_EVT_IN_RECONN = 0,  /* default */
    NO_DISCONN_EVT_IN_RECONN
} TARGET_EVENT_REPORT_CONFIG;

typedef PREPACK struct {
    u32 evtConfig;
} POSTPACK WMI_SET_TARGET_EVENT_REPORT_CMD;


typedef PREPACK struct {
    u16 cmd_buf_sz;     /* HCI cmd buffer size */
    u8 buf[1];         /* Absolute HCI cmd */
} POSTPACK WMI_HCI_CMD;

/*
 * Command Replies
 */

/*
 * WMI_GET_CHANNEL_LIST_CMDID reply
 */
typedef PREPACK struct {
    u8 reserved1;
    u8 numChannels;            /* number of channels in reply */
    u16 channelList[1];         /* channel in Mhz */
} POSTPACK WMI_CHANNEL_LIST_REPLY;

typedef enum {
    A_SUCCEEDED = 0,
    A_FAILED_DELETE_STREAM_DOESNOT_EXIST=250,
    A_SUCCEEDED_MODIFY_STREAM=251,
    A_FAILED_INVALID_STREAM = 252,
    A_FAILED_MAX_THINSTREAMS = 253,
    A_FAILED_CREATE_REMOVE_PSTREAM_FIRST = 254,
} PSTREAM_REPLY_STATUS;

typedef PREPACK struct {
    u8 status;                 /* PSTREAM_REPLY_STATUS */
    u8 txQueueNumber;
    u8 rxQueueNumber;
    u8 trafficClass;
    u8 trafficDirection;       /* DIR_TYPE */
} POSTPACK WMI_CRE_PRIORITY_STREAM_REPLY;

typedef PREPACK struct {
    u8 status;                 /* PSTREAM_REPLY_STATUS */
    u8 txQueueNumber;
    u8 rxQueueNumber;
    u8 trafficDirection;       /* DIR_TYPE */
    u8 trafficClass;
} POSTPACK WMI_DEL_PRIORITY_STREAM_REPLY;

/*
 * List of Events (target to host)
 */
typedef enum {
    WMI_READY_EVENTID           = 0x1001,
    WMI_CONNECT_EVENTID,
    WMI_DISCONNECT_EVENTID,
    WMI_BSSINFO_EVENTID,
    WMI_CMDERROR_EVENTID,
    WMI_REGDOMAIN_EVENTID,
    WMI_PSTREAM_TIMEOUT_EVENTID,
    WMI_NEIGHBOR_REPORT_EVENTID,
    WMI_TKIP_MICERR_EVENTID,
    WMI_SCAN_COMPLETE_EVENTID,           /* 0x100a */
    WMI_REPORT_STATISTICS_EVENTID,
    WMI_RSSI_THRESHOLD_EVENTID,
    WMI_ERROR_REPORT_EVENTID,
    WMI_OPT_RX_FRAME_EVENTID,
    WMI_REPORT_ROAM_TBL_EVENTID,
    WMI_EXTENSION_EVENTID,
    WMI_CAC_EVENTID,
    WMI_SNR_THRESHOLD_EVENTID,
    WMI_LQ_THRESHOLD_EVENTID,
    WMI_TX_RETRY_ERR_EVENTID,            /* 0x1014 */
    WMI_REPORT_ROAM_DATA_EVENTID,
    WMI_TEST_EVENTID,
    WMI_APLIST_EVENTID,
    WMI_GET_WOW_LIST_EVENTID,
    WMI_GET_PMKID_LIST_EVENTID,
    WMI_CHANNEL_CHANGE_EVENTID,
    WMI_PEER_NODE_EVENTID,
    WMI_PSPOLL_EVENTID,
    WMI_DTIMEXPIRY_EVENTID,
    WMI_WLAN_VERSION_EVENTID,
    WMI_SET_PARAMS_REPLY_EVENTID,
    WMI_ADDBA_REQ_EVENTID,              /*0x1020 */
    WMI_ADDBA_RESP_EVENTID,
    WMI_DELBA_REQ_EVENTID,
    WMI_TX_COMPLETE_EVENTID,
    WMI_HCI_EVENT_EVENTID,
    WMI_ACL_DATA_EVENTID,
    WMI_REPORT_SLEEP_STATE_EVENTID,
#ifdef WAPI_ENABLE
    WMI_WAPI_REKEY_EVENTID,
#endif
    WMI_REPORT_BTCOEX_STATS_EVENTID,
    WMI_REPORT_BTCOEX_CONFIG_EVENTID,
	WMI_GET_PMK_EVENTID,

	/* DFS Events */
	WMI_DFS_HOST_ATTACH_EVENTID,
	WMI_DFS_HOST_INIT_EVENTID,
	WMI_DFS_RESET_DELAYLINES_EVENTID,
	WMI_DFS_RESET_RADARQ_EVENTID,
	WMI_DFS_RESET_AR_EVENTID,
	WMI_DFS_RESET_ARQ_EVENTID,
	WMI_DFS_SET_DUR_MULTIPLIER_EVENTID,
	WMI_DFS_SET_BANGRADAR_EVENTID,
	WMI_DFS_SET_DEBUGLEVEL_EVENTID,
	WMI_DFS_PHYERR_EVENTID,
	/* CCX Evants */
	WMI_CCX_RM_STATUS_EVENTID,

	/* P2P Events */
	WMI_P2P_GO_NEG_RESULT_EVENTID,

	WMI_WAC_SCAN_DONE_EVENTID,
	WMI_WAC_REPORT_BSS_EVENTID,
	WMI_WAC_START_WPS_EVENTID,
	WMI_WAC_CTRL_REQ_REPLY_EVENTID,

	/* RFKILL Events */
	WMI_RFKILL_STATE_CHANGE_EVENTID,
	WMI_RFKILL_GET_MODE_CMD_EVENTID,
	WMI_THIN_RESERVED_START_EVENTID = 0x8000,

	/*
	 * Events in this range are reserved for thinmode
	 * See wmi_thin.h for actual definitions
	 */
	WMI_THIN_RESERVED_END_EVENTID = 0x8fff,

	WMI_SET_CHANNEL_EVENTID,
	WMI_ASSOC_REQ_EVENTID,

	/* generic ACS event */
	WMI_ACS_EVENTID,
	WMI_REPORT_WMM_PARAMS_EVENTID
} WMI_EVENT_ID;


typedef enum {
    WMI_11A_CAPABILITY   = 1,
    WMI_11G_CAPABILITY   = 2,
    WMI_11AG_CAPABILITY  = 3,
    WMI_11NA_CAPABILITY  = 4,
    WMI_11NG_CAPABILITY  = 5,
    WMI_11NAG_CAPABILITY = 6,
    // END CAPABILITY
    WMI_11N_CAPABILITY_OFFSET = (WMI_11NA_CAPABILITY - WMI_11A_CAPABILITY),
} WMI_PHY_CAPABILITY;

typedef PREPACK struct {
    u8 macaddr[ATH_MAC_LEN];
    u8 phyCapability;              /* WMI_PHY_CAPABILITY */
} POSTPACK WMI_READY_EVENT_1;

typedef PREPACK struct {
    u32 sw_version;
    u32 abi_version;
    u8 macaddr[ATH_MAC_LEN];
    u8 phyCapability;              /* WMI_PHY_CAPABILITY */
} POSTPACK WMI_READY_EVENT_2;

#if defined(ATH_TARGET)
#ifdef AR6002_REV2
#define WMI_READY_EVENT WMI_READY_EVENT_1  /* AR6002_REV2 target code */
#else
#define WMI_READY_EVENT WMI_READY_EVENT_2  /* AR6001, AR6002_REV4, AR6002_REV5 */
#endif
#else
#define WMI_READY_EVENT WMI_READY_EVENT_2 /* host code */
#endif


/*
 * Connect Event
 */
typedef PREPACK struct {
    u16 channel;
    u8 bssid[ATH_MAC_LEN];
    u16 listenInterval;
    u16 beaconInterval;
    u32 networkType;
    u8 beaconIeLen;
    u8 assocReqLen;
    u8 assocRespLen;
    u8 assocInfo[1];
} POSTPACK WMI_CONNECT_EVENT;

/*
 * Disconnect Event
 */
typedef enum {
    NO_NETWORK_AVAIL   = 0x01,
    LOST_LINK          = 0x02,     /* bmiss */
    DISCONNECT_CMD     = 0x03,
    BSS_DISCONNECTED   = 0x04,
    AUTH_FAILED        = 0x05,
    ASSOC_FAILED       = 0x06,
    NO_RESOURCES_AVAIL = 0x07,
    CSERV_DISCONNECT   = 0x08,
    INVALID_PROFILE    = 0x0a,
    DOT11H_CHANNEL_SWITCH = 0x0b,
    PROFILE_MISMATCH   = 0x0c,
    CONNECTION_EVICTED = 0x0d,
    IBSS_MERGE         = 0xe,
} WMI_DISCONNECT_REASON;

typedef PREPACK struct {
    u16 protocolReasonStatus;  /* reason code, see 802.11 spec. */
    u8 bssid[ATH_MAC_LEN];    /* set if known */
    u8 disconnectReason ;      /* see WMI_DISCONNECT_REASON */
    u8 assocRespLen;
    u8 assocInfo[1];
} POSTPACK WMI_DISCONNECT_EVENT;

/*
 * BSS Info Event.
 * Mechanism used to inform host of the presence and characteristic of
 * wireless networks present.  Consists of bss info header followed by
 * the beacon or probe-response frame body.  The 802.11 header is not included.
 */
typedef enum {
    BEACON_FTYPE = 0x1,
    PROBERESP_FTYPE,
    ACTION_MGMT_FTYPE,
    PROBEREQ_FTYPE,
} WMI_BI_FTYPE;

enum {
    BSS_ELEMID_CHANSWITCH = 0x01,
    BSS_ELEMID_ATHEROS = 0x02,
};

typedef PREPACK struct {
    u16 channel;
    u8 frameType;          /* see WMI_BI_FTYPE */
    u8 snr;
    s16 rssi;
    u8 bssid[ATH_MAC_LEN];
    u32 ieMask;
} POSTPACK WMI_BSS_INFO_HDR;

/*
 * BSS INFO HDR version 2.0
 * With 6 bytes HTC header and 6 bytes of WMI header
 * WMI_BSS_INFO_HDR cannot be accommodated in the removed 802.11 management
 * header space.
 * - Reduce the ieMask to 2 bytes as only two bit flags are used
 * - Remove rssi and compute it on the host. rssi = snr - 95
 */
typedef PREPACK struct {
    u16 channel;
    u8 frameType;          /* see WMI_BI_FTYPE */
    u8 snr;
    u8 bssid[ATH_MAC_LEN];
    u16 ieMask;
} POSTPACK WMI_BSS_INFO_HDR2;

/*
 * Command Error Event
 */
typedef enum {
    INVALID_PARAM  = 0x01,
    ILLEGAL_STATE  = 0x02,
    INTERNAL_ERROR = 0x03,
} WMI_ERROR_CODE;

typedef PREPACK struct {
    u16 commandId;
    u8 errorCode;
} POSTPACK WMI_CMD_ERROR_EVENT;

/*
 * New Regulatory Domain Event
 */
typedef PREPACK struct {
    u32 regDomain;
} POSTPACK WMI_REG_DOMAIN_EVENT;

typedef PREPACK struct {
    u8 txQueueNumber;
    u8 rxQueueNumber;
    u8 trafficDirection;
    u8 trafficClass;
} POSTPACK WMI_PSTREAM_TIMEOUT_EVENT;

typedef PREPACK struct {
    u8 reserve1;
    u8 reserve2;
    u8 reserve3;
    u8 trafficClass;
} POSTPACK WMI_ACM_REJECT_EVENT;

/*
 * The WMI_NEIGHBOR_REPORT Event is generated by the target to inform
 * the host of BSS's it has found that matches the current profile.
 * It can be used by the host to cache PMKs and/to initiate pre-authentication
 * if the BSS supports it.  The first bssid is always the current associated
 * BSS.
 * The bssid and bssFlags information repeats according to the number
 * or APs reported.
 */
typedef enum {
    WMI_DEFAULT_BSS_FLAGS   = 0x00,
    WMI_PREAUTH_CAPABLE_BSS = 0x01,
    WMI_PMKID_VALID_BSS     = 0x02,
} WMI_BSS_FLAGS;

typedef PREPACK struct {
    u8 bssid[ATH_MAC_LEN];
    u8 bssFlags;            /* see WMI_BSS_FLAGS */
} POSTPACK WMI_NEIGHBOR_INFO;

typedef PREPACK struct {
    s8 numberOfAps;
    WMI_NEIGHBOR_INFO neighbor[1];
} POSTPACK WMI_NEIGHBOR_REPORT_EVENT;

/*
 * TKIP MIC Error Event
 */
typedef PREPACK struct {
    u8 keyid;
    u8 ismcast;
} POSTPACK WMI_TKIP_MICERR_EVENT;

/*
 * WMI_SCAN_COMPLETE_EVENTID - no parameters (old), staus parameter (new)
 */
typedef PREPACK struct {
    s32 status;
} POSTPACK WMI_SCAN_COMPLETE_EVENT;

#define MAX_OPT_DATA_LEN 1400

/*
 * WMI_SET_ADHOC_BSSID_CMDID
 */
typedef PREPACK struct {
    u8 bssid[ATH_MAC_LEN];
} POSTPACK WMI_SET_ADHOC_BSSID_CMD;

/*
 * WMI_SET_OPT_MODE_CMDID
 */
typedef enum {
    SPECIAL_OFF,
    SPECIAL_ON,
} OPT_MODE_TYPE;

typedef PREPACK struct {
    u8 optMode;
} POSTPACK WMI_SET_OPT_MODE_CMD;

/*
 * WMI_TX_OPT_FRAME_CMDID
 */
typedef enum {
    OPT_PROBE_REQ   = 0x01,
    OPT_PROBE_RESP  = 0x02,
    OPT_CPPP_START  = 0x03,
    OPT_CPPP_STOP   = 0x04,
} WMI_OPT_FTYPE;

typedef PREPACK struct {
    u16 optIEDataLen;
    u8 frmType;
    u8 dstAddr[ATH_MAC_LEN];
    u8 bssid[ATH_MAC_LEN];
    u8 reserved;               /* For alignment */
    u8 optIEData[1];
} POSTPACK WMI_OPT_TX_FRAME_CMD;

/*
 * Special frame receive Event.
 * Mechanism used to inform host of the receiption of the special frames.
 * Consists of special frame info header followed by special frame body.
 * The 802.11 header is not included.
 */
typedef PREPACK struct {
    u16 channel;
    u8 frameType;          /* see WMI_OPT_FTYPE */
    s8 snr;
    u8 srcAddr[ATH_MAC_LEN];
    u8 bssid[ATH_MAC_LEN];
} POSTPACK WMI_OPT_RX_INFO_HDR;

/*
 * Reporting statistics.
 */
typedef PREPACK struct {
    u32 tx_packets;
    u32 tx_bytes;
    u32 tx_unicast_pkts;
    u32 tx_unicast_bytes;
    u32 tx_multicast_pkts;
    u32 tx_multicast_bytes;
    u32 tx_broadcast_pkts;
    u32 tx_broadcast_bytes;
    u32 tx_rts_success_cnt;
    u32 tx_packet_per_ac[4];
    u32 tx_errors_per_ac[4];

    u32 tx_errors;
    u32 tx_failed_cnt;
    u32 tx_retry_cnt;
    u32 tx_mult_retry_cnt;
    u32 tx_rts_fail_cnt;
    s32 tx_unicast_rate;
}POSTPACK tx_stats_t;

typedef PREPACK struct {
    u32 rx_packets;
    u32 rx_bytes;
    u32 rx_unicast_pkts;
    u32 rx_unicast_bytes;
    u32 rx_multicast_pkts;
    u32 rx_multicast_bytes;
    u32 rx_broadcast_pkts;
    u32 rx_broadcast_bytes;
    u32 rx_fragment_pkt;

    u32 rx_errors;
    u32 rx_crcerr;
    u32 rx_key_cache_miss;
    u32 rx_decrypt_err;
    u32 rx_duplicate_frames;
    s32 rx_unicast_rate;
}POSTPACK rx_stats_t;

typedef PREPACK struct {
    u32 tkip_local_mic_failure;
    u32 tkip_counter_measures_invoked;
    u32 tkip_replays;
    u32 tkip_format_errors;
    u32 ccmp_format_errors;
    u32 ccmp_replays;
}POSTPACK tkip_ccmp_stats_t;

typedef PREPACK struct {
    u32 power_save_failure_cnt;
    u16 stop_tx_failure_cnt;
    u16 atim_tx_failure_cnt;
    u16 atim_rx_failure_cnt;
    u16 bcn_rx_failure_cnt;
}POSTPACK pm_stats_t;

typedef PREPACK struct {
    u32 cs_bmiss_cnt;
    u32 cs_lowRssi_cnt;
    u16 cs_connect_cnt;
    u16 cs_disconnect_cnt;
    s16 cs_aveBeacon_rssi;
    u16 cs_roam_count;
    s16 cs_rssi;
    u8 cs_snr;
    u8 cs_aveBeacon_snr;
    u8 cs_lastRoam_msec;
} POSTPACK cserv_stats_t;

typedef PREPACK struct {
    tx_stats_t          tx_stats;
    rx_stats_t          rx_stats;
    tkip_ccmp_stats_t   tkipCcmpStats;
}POSTPACK wlan_net_stats_t;

typedef PREPACK struct {
    u32 arp_received;
    u32 arp_matched;
    u32 arp_replied;
} POSTPACK arp_stats_t;

typedef PREPACK struct {
    u32 wow_num_pkts_dropped;
    u16 wow_num_events_discarded;
    u8 wow_num_host_pkt_wakeups;
    u8 wow_num_host_event_wakeups;
} POSTPACK wlan_wow_stats_t;

typedef PREPACK struct {
    u32 lqVal;
    s32 noise_floor_calibation;
    pm_stats_t          pmStats;
    wlan_net_stats_t    txrxStats;
    wlan_wow_stats_t    wowStats;
    arp_stats_t         arpStats;
    cserv_stats_t       cservStats;
} POSTPACK WMI_TARGET_STATS;

/*
 * WMI_RSSI_THRESHOLD_EVENTID.
 * Indicate the RSSI events to host. Events are indicated when we breach a
 * thresold value.
 */
typedef enum{
    WMI_RSSI_THRESHOLD1_ABOVE = 0,
    WMI_RSSI_THRESHOLD2_ABOVE,
    WMI_RSSI_THRESHOLD3_ABOVE,
    WMI_RSSI_THRESHOLD4_ABOVE,
    WMI_RSSI_THRESHOLD5_ABOVE,
    WMI_RSSI_THRESHOLD6_ABOVE,
    WMI_RSSI_THRESHOLD1_BELOW,
    WMI_RSSI_THRESHOLD2_BELOW,
    WMI_RSSI_THRESHOLD3_BELOW,
    WMI_RSSI_THRESHOLD4_BELOW,
    WMI_RSSI_THRESHOLD5_BELOW,
    WMI_RSSI_THRESHOLD6_BELOW
}WMI_RSSI_THRESHOLD_VAL;

typedef PREPACK struct {
    s16 rssi;
    u8 range;
}POSTPACK WMI_RSSI_THRESHOLD_EVENT;

/*
 *  WMI_ERROR_REPORT_EVENTID
 */
typedef enum{
    WMI_TARGET_PM_ERR_FAIL      = 0x00000001,
    WMI_TARGET_KEY_NOT_FOUND    = 0x00000002,
    WMI_TARGET_DECRYPTION_ERR   = 0x00000004,
    WMI_TARGET_BMISS            = 0x00000008,
    WMI_PSDISABLE_NODE_JOIN     = 0x00000010,
    WMI_TARGET_COM_ERR          = 0x00000020,
    WMI_TARGET_FATAL_ERR        = 0x00000040
} WMI_TARGET_ERROR_VAL;

typedef PREPACK struct {
    u32 errorVal;
}POSTPACK  WMI_TARGET_ERROR_REPORT_EVENT;

typedef PREPACK struct {
    u8 retrys;
}POSTPACK  WMI_TX_RETRY_ERR_EVENT;

typedef enum{
    WMI_SNR_THRESHOLD1_ABOVE = 1,
    WMI_SNR_THRESHOLD1_BELOW,
    WMI_SNR_THRESHOLD2_ABOVE,
    WMI_SNR_THRESHOLD2_BELOW,
    WMI_SNR_THRESHOLD3_ABOVE,
    WMI_SNR_THRESHOLD3_BELOW,
    WMI_SNR_THRESHOLD4_ABOVE,
    WMI_SNR_THRESHOLD4_BELOW
} WMI_SNR_THRESHOLD_VAL;

typedef PREPACK struct {
    u8 range;  /* WMI_SNR_THRESHOLD_VAL */
    u8 snr;
}POSTPACK  WMI_SNR_THRESHOLD_EVENT;

typedef enum{
    WMI_LQ_THRESHOLD1_ABOVE = 1,
    WMI_LQ_THRESHOLD1_BELOW,
    WMI_LQ_THRESHOLD2_ABOVE,
    WMI_LQ_THRESHOLD2_BELOW,
    WMI_LQ_THRESHOLD3_ABOVE,
    WMI_LQ_THRESHOLD3_BELOW,
    WMI_LQ_THRESHOLD4_ABOVE,
    WMI_LQ_THRESHOLD4_BELOW
} WMI_LQ_THRESHOLD_VAL;

typedef PREPACK struct {
    s32 lq;
    u8 range;  /* WMI_LQ_THRESHOLD_VAL */
}POSTPACK  WMI_LQ_THRESHOLD_EVENT;
/*
 * WMI_REPORT_ROAM_TBL_EVENTID
 */
#define MAX_ROAM_TBL_CAND   5

typedef PREPACK struct {
    s32 roam_util;
    u8 bssid[ATH_MAC_LEN];
    s8 rssi;
    s8 rssidt;
    s8 last_rssi;
    s8 util;
    s8 bias;
    u8 reserved; /* For alignment */
} POSTPACK WMI_BSS_ROAM_INFO;


typedef PREPACK struct {
    u16 roamMode;
    u16 numEntries;
    WMI_BSS_ROAM_INFO bssRoamInfo[1];
} POSTPACK WMI_TARGET_ROAM_TBL;

/*
 * WMI_HCI_EVENT_EVENTID
 */
typedef PREPACK struct {
    u16 evt_buf_sz;     /* HCI event buffer size */
    u8 buf[1];         /* HCI  event */
} POSTPACK WMI_HCI_EVENT;

/*
 *  WMI_CAC_EVENTID
 */
typedef enum {
    CAC_INDICATION_ADMISSION = 0x00,
    CAC_INDICATION_ADMISSION_RESP = 0x01,
    CAC_INDICATION_DELETE = 0x02,
    CAC_INDICATION_NO_RESP = 0x03,
}CAC_INDICATION;

#define WMM_TSPEC_IE_LEN   63

typedef PREPACK struct {
    u8 ac;
    u8 cac_indication;
    u8 statusCode;
    u8 tspecSuggestion[WMM_TSPEC_IE_LEN];
}POSTPACK  WMI_CAC_EVENT;

/*
 * WMI_APLIST_EVENTID
 */

typedef enum {
    APLIST_VER1 = 1,
} APLIST_VER;

typedef PREPACK struct {
    u8 bssid[ATH_MAC_LEN];
    u16 channel;
} POSTPACK  WMI_AP_INFO_V1;

typedef PREPACK union {
    WMI_AP_INFO_V1  apInfoV1;
} POSTPACK WMI_AP_INFO;

typedef PREPACK struct {
    u8 apListVer;
    u8 numAP;
    WMI_AP_INFO apList[1];
} POSTPACK WMI_APLIST_EVENT;

/*
 * developer commands
 */

/*
 * WMI_SET_BITRATE_CMDID
 *
 * Get bit rate cmd uses same definition as set bit rate cmd
 */
typedef enum {
    RATE_AUTO   = -1,
    RATE_1Mb    = 0,
    RATE_2Mb    = 1,
    RATE_5_5Mb  = 2,
    RATE_11Mb   = 3,
    RATE_6Mb    = 4,
    RATE_9Mb    = 5,
    RATE_12Mb   = 6,
    RATE_18Mb   = 7,
    RATE_24Mb   = 8,
    RATE_36Mb   = 9,
    RATE_48Mb   = 10,
    RATE_54Mb   = 11,
    RATE_MCS_0_20 = 12,
    RATE_MCS_1_20 = 13,
    RATE_MCS_2_20 = 14,
    RATE_MCS_3_20 = 15,
    RATE_MCS_4_20 = 16,
    RATE_MCS_5_20 = 17,
    RATE_MCS_6_20 = 18,
    RATE_MCS_7_20 = 19,
    RATE_MCS_0_40 = 20,
    RATE_MCS_1_40 = 21,
    RATE_MCS_2_40 = 22,
    RATE_MCS_3_40 = 23,
    RATE_MCS_4_40 = 24,
    RATE_MCS_5_40 = 25,
    RATE_MCS_6_40 = 26,
    RATE_MCS_7_40 = 27,
} WMI_BIT_RATE;

typedef PREPACK struct {
    s8 rateIndex;          /* see WMI_BIT_RATE */
    s8 mgmtRateIndex;
    s8 ctlRateIndex;
} POSTPACK WMI_BIT_RATE_CMD;


typedef PREPACK struct {
    s8 rateIndex;          /* see WMI_BIT_RATE */
} POSTPACK  WMI_BIT_RATE_REPLY;


/*
 * WMI_SET_FIXRATES_CMDID
 *
 * Get fix rates cmd uses same definition as set fix rates cmd
 */
#define FIX_RATE_1Mb            ((u32)0x1)
#define FIX_RATE_2Mb            ((u32)0x2)
#define FIX_RATE_5_5Mb          ((u32)0x4)
#define FIX_RATE_11Mb           ((u32)0x8)
#define FIX_RATE_6Mb            ((u32)0x10)
#define FIX_RATE_9Mb            ((u32)0x20)
#define FIX_RATE_12Mb           ((u32)0x40)
#define FIX_RATE_18Mb           ((u32)0x80)
#define FIX_RATE_24Mb           ((u32)0x100)
#define FIX_RATE_36Mb           ((u32)0x200)
#define FIX_RATE_48Mb           ((u32)0x400)
#define FIX_RATE_54Mb           ((u32)0x800)
#define FIX_RATE_MCS_0_20       ((u32)0x1000)
#define FIX_RATE_MCS_1_20       ((u32)0x2000)
#define FIX_RATE_MCS_2_20       ((u32)0x4000)
#define FIX_RATE_MCS_3_20       ((u32)0x8000)
#define FIX_RATE_MCS_4_20       ((u32)0x10000)
#define FIX_RATE_MCS_5_20       ((u32)0x20000)
#define FIX_RATE_MCS_6_20       ((u32)0x40000)
#define FIX_RATE_MCS_7_20       ((u32)0x80000)
#define FIX_RATE_MCS_0_40       ((u32)0x100000)
#define FIX_RATE_MCS_1_40       ((u32)0x200000)
#define FIX_RATE_MCS_2_40       ((u32)0x400000)
#define FIX_RATE_MCS_3_40       ((u32)0x800000)
#define FIX_RATE_MCS_4_40       ((u32)0x1000000)
#define FIX_RATE_MCS_5_40       ((u32)0x2000000)
#define FIX_RATE_MCS_6_40       ((u32)0x4000000)
#define FIX_RATE_MCS_7_40       ((u32)0x8000000)

typedef PREPACK struct {
    u32 fixRateMask;          /* see WMI_BIT_RATE */
} POSTPACK WMI_FIX_RATES_CMD, WMI_FIX_RATES_REPLY;

typedef PREPACK struct {
    u8 bEnableMask;
    u8 frameType;               /*type and subtype*/
    u32 frameRateMask;          /* see WMI_BIT_RATE */
} POSTPACK WMI_FRAME_RATES_CMD, WMI_FRAME_RATES_REPLY;

/*
 * WMI_SET_RECONNECT_AUTH_MODE_CMDID
 *
 * Set authentication mode
 */
typedef enum {
    RECONN_DO_AUTH = 0x00,
    RECONN_NOT_AUTH = 0x01
} WMI_AUTH_MODE;

typedef PREPACK struct {
    u8 mode;
} POSTPACK WMI_SET_AUTH_MODE_CMD;

/*
 * WMI_SET_REASSOC_MODE_CMDID
 *
 * Set authentication mode
 */
typedef enum {
    REASSOC_DO_DISASSOC = 0x00,
    REASSOC_DONOT_DISASSOC = 0x01
} WMI_REASSOC_MODE;

typedef PREPACK struct {
    u8 mode;
}POSTPACK WMI_SET_REASSOC_MODE_CMD;

typedef enum {
    ROAM_DATA_TIME = 1,            /* Get The Roam Time Data */
} ROAM_DATA_TYPE;

typedef PREPACK struct {
    u32 disassoc_time;
    u32 no_txrx_time;
    u32 assoc_time;
    u32 allow_txrx_time;
    u8 disassoc_bssid[ATH_MAC_LEN];
    s8 disassoc_bss_rssi;
    u8 assoc_bssid[ATH_MAC_LEN];
    s8 assoc_bss_rssi;
} POSTPACK WMI_TARGET_ROAM_TIME;

typedef PREPACK struct {
    PREPACK union {
        WMI_TARGET_ROAM_TIME roamTime;
    } POSTPACK u;
    u8 roamDataType ;
} POSTPACK WMI_TARGET_ROAM_DATA;

typedef enum {
    WMI_WMM_DISABLED = 0,
    WMI_WMM_ENABLED
} WMI_WMM_STATUS;

typedef PREPACK struct {
    u8 status;
}POSTPACK WMI_SET_WMM_CMD;

typedef PREPACK struct {
    u8 status;
}POSTPACK WMI_SET_QOS_SUPP_CMD;

typedef enum {
    WMI_TXOP_DISABLED = 0,
    WMI_TXOP_ENABLED
} WMI_TXOP_CFG;

typedef PREPACK struct {
    u8 txopEnable;
}POSTPACK WMI_SET_WMM_TXOP_CMD;

typedef PREPACK struct {
    u8 keepaliveInterval;
} POSTPACK WMI_SET_KEEPALIVE_CMD;

typedef PREPACK struct {
    u32 configured;
    u8 keepaliveInterval;
} POSTPACK WMI_GET_KEEPALIVE_CMD;

/*
 * Add Application specified IE to a management frame
 */
#define WMI_MAX_IE_LEN  255

typedef PREPACK struct {
    u8 mgmtFrmType;  /* one of WMI_MGMT_FRAME_TYPE */
    u8 ieLen;    /* Length  of the IE that should be added to the MGMT frame */
    u8 ieInfo[1];
} POSTPACK WMI_SET_APPIE_CMD;

/*
 * Notify the WSC registration status to the target
 */
#define WSC_REG_ACTIVE     1
#define WSC_REG_INACTIVE   0
/* Generic Hal Interface for setting hal paramters. */
/* Add new Set HAL Param cmdIds here for newer params */
typedef enum {
   WHAL_SETCABTO_CMDID = 1,
}WHAL_CMDID;

typedef PREPACK struct {
    u8 cabTimeOut;
} POSTPACK WHAL_SETCABTO_PARAM;

typedef PREPACK struct {
    u8 whalCmdId;
    u8 data[1];
} POSTPACK WHAL_PARAMCMD;


#define WOW_MAX_FILTER_LISTS 1 /*4*/
#define WOW_MAX_FILTERS_PER_LIST 4
#define WOW_PATTERN_SIZE 64
#define WOW_MASK_SIZE 64

#define MAC_MAX_FILTERS_PER_LIST 4

typedef PREPACK struct {
    u8 wow_valid_filter;
    u8 wow_filter_id;
    u8 wow_filter_size;
    u8 wow_filter_offset;
    u8 wow_filter_mask[WOW_MASK_SIZE];
    u8 wow_filter_pattern[WOW_PATTERN_SIZE];
} POSTPACK WOW_FILTER;


typedef PREPACK struct {
    u8 wow_valid_list;
    u8 wow_list_id;
    u8 wow_num_filters;
    u8 wow_total_list_size;
    WOW_FILTER list[WOW_MAX_FILTERS_PER_LIST];
} POSTPACK WOW_FILTER_LIST;

typedef PREPACK struct {
    u8 valid_filter;
    u8 mac_addr[ATH_MAC_LEN];
} POSTPACK MAC_FILTER;


typedef PREPACK struct {
    u8 total_list_size;
    u8 enable;
    MAC_FILTER list[MAC_MAX_FILTERS_PER_LIST];
} POSTPACK MAC_FILTER_LIST;

#define MAX_IP_ADDRS  2
typedef PREPACK struct {
    u32 ips[MAX_IP_ADDRS];  /* IP in Network Byte Order */
} POSTPACK WMI_SET_IP_CMD;

typedef PREPACK struct {
    u32 awake;
    u32 asleep;
} POSTPACK WMI_SET_HOST_SLEEP_MODE_CMD;

typedef enum {
    WOW_FILTER_SSID = 0x1
} WMI_WOW_FILTER;

typedef PREPACK struct {
    u32 enable_wow;
    WMI_WOW_FILTER filter;
    u16 hostReqDelay;
} POSTPACK WMI_SET_WOW_MODE_CMD;

typedef PREPACK struct {
    u8 filter_list_id;
} POSTPACK WMI_GET_WOW_LIST_CMD;

/*
 * WMI_GET_WOW_LIST_CMD reply
 */
typedef PREPACK struct {
    u8 num_filters;     /* number of patterns in reply */
    u8 this_filter_num; /*  this is filter # x of total num_filters */
    u8 wow_mode;
    u8 host_mode;
    WOW_FILTER  wow_filters[1];
} POSTPACK WMI_GET_WOW_LIST_REPLY;

typedef PREPACK struct {
    u8 filter_list_id;
    u8 filter_size;
    u8 filter_offset;
    u8 filter[1];
} POSTPACK WMI_ADD_WOW_PATTERN_CMD;

typedef PREPACK struct {
    u16 filter_list_id;
    u16 filter_id;
} POSTPACK WMI_DEL_WOW_PATTERN_CMD;

typedef PREPACK struct {
    u8 macaddr[ATH_MAC_LEN];
} POSTPACK WMI_SET_MAC_ADDRESS_CMD;

/*
 * WMI_SET_AKMP_PARAMS_CMD
 */

#define WMI_AKMP_MULTI_PMKID_EN   0x000001

typedef PREPACK struct {
    u32 akmpInfo;
} POSTPACK WMI_SET_AKMP_PARAMS_CMD;

typedef PREPACK struct {
    u8 pmkid[WMI_PMKID_LEN];
} POSTPACK WMI_PMKID;

/*
 * WMI_SET_PMKID_LIST_CMD
 */
#define WMI_MAX_PMKID_CACHE   8

typedef PREPACK struct {
    u32 numPMKID;
    WMI_PMKID   pmkidList[WMI_MAX_PMKID_CACHE];
} POSTPACK WMI_SET_PMKID_LIST_CMD;

/*
 * WMI_GET_PMKID_LIST_CMD  Reply
 * Following the Number of PMKIDs is the list of PMKIDs
 */
typedef PREPACK struct {
    u32 numPMKID;
    u8 bssidList[ATH_MAC_LEN][1];
    WMI_PMKID   pmkidList[1];
} POSTPACK WMI_PMKID_LIST_REPLY;

typedef PREPACK struct {
    u16 oldChannel;
    u32 newChannel;
} POSTPACK WMI_CHANNEL_CHANGE_EVENT;

typedef PREPACK struct {
    u32 version;
} POSTPACK WMI_WLAN_VERSION_EVENT;


/* WMI_ADDBA_REQ_EVENTID */
typedef PREPACK struct {
    u8 tid;
    u8 win_sz;
    u16 st_seq_no;
    u8 status;         /* f/w response for ADDBA Req; OK(0) or failure(!=0) */
} POSTPACK WMI_ADDBA_REQ_EVENT;

/* WMI_ADDBA_RESP_EVENTID */
typedef PREPACK struct {
    u8 tid;
    u8 status;         /* OK(0), failure (!=0) */
    u16 amsdu_sz;       /* Three values: Not supported(0), 3839, 8k */
} POSTPACK WMI_ADDBA_RESP_EVENT;

/* WMI_DELBA_EVENTID
 * f/w received a DELBA for peer and processed it.
 * Host is notified of this
 */
typedef PREPACK struct {
    u8 tid;
    u8 is_peer_initiator;
    u16 reason_code;
} POSTPACK WMI_DELBA_EVENT;


#ifdef WAPI_ENABLE
#define WAPI_REKEY_UCAST    1
#define WAPI_REKEY_MCAST    2
typedef PREPACK struct {
    u8 type;
    u8 macAddr[ATH_MAC_LEN];
} POSTPACK WMI_WAPIREKEY_EVENT;
#endif


/* WMI_ALLOW_AGGR_CMDID
 * Configures tid's to allow ADDBA negotiations
 * on each tid, in each direction
 */
typedef PREPACK struct {
    u16 tx_allow_aggr;  /* 16-bit mask to allow uplink ADDBA negotiation - bit position indicates tid*/
    u16 rx_allow_aggr;  /* 16-bit mask to allow donwlink ADDBA negotiation - bit position indicates tid*/
} POSTPACK WMI_ALLOW_AGGR_CMD;

/* WMI_ADDBA_REQ_CMDID
 * f/w starts performing ADDBA negotiations with peer
 * on the given tid
 */
typedef PREPACK struct {
    u8 tid;
} POSTPACK WMI_ADDBA_REQ_CMD;

/* WMI_DELBA_REQ_CMDID
 * f/w would teardown BA with peer.
 * is_send_initiator indicates if it's or tx or rx side
 */
typedef PREPACK struct {
    u8 tid;
    u8 is_sender_initiator;

} POSTPACK WMI_DELBA_REQ_CMD;

#define PEER_NODE_JOIN_EVENT 0x00
#define PEER_NODE_LEAVE_EVENT 0x01
#define PEER_FIRST_NODE_JOIN_EVENT 0x10
#define PEER_LAST_NODE_LEAVE_EVENT 0x11
typedef PREPACK struct {
    u8 eventCode;
    u8 peerMacAddr[ATH_MAC_LEN];
} POSTPACK WMI_PEER_NODE_EVENT;

#define IEEE80211_FRAME_TYPE_MGT          0x00
#define IEEE80211_FRAME_TYPE_CTL          0x04

/*
 * Transmit complete event data structure(s)
 */


typedef PREPACK struct {
#define TX_COMPLETE_STATUS_SUCCESS 0
#define TX_COMPLETE_STATUS_RETRIES 1
#define TX_COMPLETE_STATUS_NOLINK  2
#define TX_COMPLETE_STATUS_TIMEOUT 3
#define TX_COMPLETE_STATUS_OTHER   4

    u8 status; /* one of TX_COMPLETE_STATUS_... */
    u8 pktID; /* packet ID to identify parent packet */
    u8 rateIdx; /* rate index on successful transmission */
    u8 ackFailures; /* number of ACK failures in tx attempt */
#if 0 /* optional params currently omitted. */
    u32 queueDelay; // usec delay measured Tx Start time - host delivery time
    u32 mediaDelay; // usec delay measured ACK rx time - host delivery time
#endif
} POSTPACK TX_COMPLETE_MSG_V1; /* version 1 of tx complete msg */

typedef PREPACK struct {
    u8 numMessages; /* number of tx comp msgs following this struct */
    u8 msgLen; /* length in bytes for each individual msg following this struct */
    u8 msgType; /* version of tx complete msg data following this struct */
    u8 reserved; /* individual messages follow this header */
} POSTPACK WMI_TX_COMPLETE_EVENT;

#define WMI_TXCOMPLETE_VERSION_1 (0x01)


/*
 * ------- AP Mode definitions --------------
 */

/*
 * !!! Warning !!!
 * -Changing the following values needs compilation of both driver and firmware
 */
#ifdef AR6002_REV2
#define AP_MAX_NUM_STA          4
#else
#define AP_MAX_NUM_STA          8
#endif
#define AP_ACL_SIZE             10
#define IEEE80211_MAX_IE        256
#define MCAST_AID               0xFF /* Spl. AID used to set DTIM flag in the beacons */
#define DEF_AP_COUNTRY_CODE     "US "
#define DEF_AP_WMODE_G          WMI_11G_MODE
#define DEF_AP_WMODE_AG         WMI_11AG_MODE
#define DEF_AP_DTIM             5
#define DEF_BEACON_INTERVAL     100

/* AP mode disconnect reasons */
#define AP_DISCONNECT_STA_LEFT      101
#define AP_DISCONNECT_FROM_HOST     102
#define AP_DISCONNECT_COMM_TIMEOUT  103

/*
 * Used with WMI_AP_HIDDEN_SSID_CMDID
 */
#define HIDDEN_SSID_FALSE   0
#define HIDDEN_SSID_TRUE    1
typedef PREPACK struct {
    u8 hidden_ssid;
} POSTPACK WMI_AP_HIDDEN_SSID_CMD;

/*
 * Used with WMI_AP_ACL_POLICY_CMDID
 */
#define AP_ACL_DISABLE          0x00
#define AP_ACL_ALLOW_MAC        0x01
#define AP_ACL_DENY_MAC         0x02
#define AP_ACL_RETAIN_LIST_MASK 0x80
typedef PREPACK struct {
    u8 policy;
} POSTPACK WMI_AP_ACL_POLICY_CMD;

/*
 * Used with WMI_AP_ACL_MAC_LIST_CMDID
 */
#define ADD_MAC_ADDR    1
#define DEL_MAC_ADDR    2
typedef PREPACK struct {
    u8 action;
    u8 index;
    u8 mac[ATH_MAC_LEN];
    u8 wildcard;
} POSTPACK WMI_AP_ACL_MAC_CMD;

typedef PREPACK struct {
    u16 index;
    u8 acl_mac[AP_ACL_SIZE][ATH_MAC_LEN];
    u8 wildcard[AP_ACL_SIZE];
    u8 policy;
} POSTPACK WMI_AP_ACL;

/*
 * Used with WMI_AP_SET_NUM_STA_CMDID
 */
typedef PREPACK struct {
    u8 num_sta;
} POSTPACK WMI_AP_SET_NUM_STA_CMD;

/*
 * Used with WMI_AP_SET_MLME_CMDID
 */
typedef PREPACK struct {
    u8 mac[ATH_MAC_LEN];
    u16 reason;              /* 802.11 reason code */
    u8 cmd;                 /* operation to perform */
#define WMI_AP_MLME_ASSOC       1   /* associate station */
#define WMI_AP_DISASSOC         2   /* disassociate station */
#define WMI_AP_DEAUTH           3   /* deauthenticate station */
#define WMI_AP_MLME_AUTHORIZE   4   /* authorize station */
#define WMI_AP_MLME_UNAUTHORIZE 5   /* unauthorize station */
} POSTPACK WMI_AP_SET_MLME_CMD;

typedef PREPACK struct {
    u32 period;
} POSTPACK WMI_AP_CONN_INACT_CMD;

typedef PREPACK struct {
    u32 period_min;
    u32 dwell_ms;
} POSTPACK WMI_AP_PROT_SCAN_TIME_CMD;

typedef PREPACK struct {
    u32 flag;
    u16 aid;
} POSTPACK WMI_AP_SET_PVB_CMD;

#define WMI_DISABLE_REGULATORY_CODE "FF"

typedef PREPACK struct {
    u8 countryCode[3];
} POSTPACK WMI_AP_SET_COUNTRY_CMD;

typedef PREPACK struct {
    u8 dtim;
} POSTPACK WMI_AP_SET_DTIM_CMD;

typedef PREPACK struct {
    u8 band; /* specifies which band to apply these values */
    u8 enable; /* allows 11n to be disabled on a per band basis */
    u8 chan_width_40M_supported;
    u8 short_GI_20MHz;
    u8 short_GI_40MHz;
    u8 intolerance_40MHz;
    u8 max_ampdu_len_exp;
} POSTPACK WMI_SET_HT_CAP_CMD;

typedef PREPACK struct {
    u8 sta_chan_width;
} POSTPACK WMI_SET_HT_OP_CMD;

typedef PREPACK struct {
    u32 rateMasks[8];
} POSTPACK WMI_SET_TX_SELECT_RATES_CMD;

typedef PREPACK struct {
    u32 sgiMask;
    u8 sgiPERThreshold;
} POSTPACK WMI_SET_TX_SGI_PARAM_CMD;

#define DEFAULT_SGI_MASK 0x08080000
#define DEFAULT_SGI_PER 10

typedef PREPACK struct {
    u32 rateField; /* 1 bit per rate corresponding to index */
    u8 id;
    u8 shortTrys;
    u8 longTrys;
    u8 reserved; /* padding */
} POSTPACK WMI_SET_RATE_POLICY_CMD;

typedef PREPACK struct {
    u8 metaVersion; /* version of meta data for rx packets <0 = default> (0-7 = valid) */
    u8 dot11Hdr; /* 1 == leave .11 header intact , 0 == replace .11 header with .3 <default> */
    u8 defragOnHost; /* 1 == defragmentation is performed by host, 0 == performed by target <default> */
    u8 reserved[1]; /* alignment */
} POSTPACK WMI_RX_FRAME_FORMAT_CMD;


typedef PREPACK struct {
    u8 enable;     /* 1 == device operates in thin mode , 0 == normal mode <default> */
    u8 reserved[3];
} POSTPACK WMI_SET_THIN_MODE_CMD;

/* AP mode events */
/* WMI_PS_POLL_EVENT */
typedef PREPACK struct {
    u16 aid;
} POSTPACK WMI_PSPOLL_EVENT;

typedef PREPACK struct {
    u32 tx_bytes;
    u32 tx_pkts;
    u32 tx_error;
    u32 tx_discard;
    u32 rx_bytes;
    u32 rx_pkts;
    u32 rx_error;
    u32 rx_discard;
    u32 aid;
} POSTPACK WMI_PER_STA_STAT;

#define AP_GET_STATS    0
#define AP_CLEAR_STATS  1

typedef PREPACK struct {
    u32 action;
    WMI_PER_STA_STAT    sta[AP_MAX_NUM_STA+1];
} POSTPACK WMI_AP_MODE_STAT;
#define WMI_AP_MODE_STAT_SIZE(numSta) (sizeof(u32) + ((numSta + 1) * sizeof(WMI_PER_STA_STAT)))

#define AP_11BG_RATESET1        1
#define AP_11BG_RATESET2        2
#define DEF_AP_11BG_RATESET     AP_11BG_RATESET1
typedef PREPACK struct {
    u8 rateset;
} POSTPACK WMI_AP_SET_11BG_RATESET_CMD;
/*
 * End of AP mode definitions
 */

#ifdef __cplusplus
}
#endif

#endif /* _WMI_H_ */
