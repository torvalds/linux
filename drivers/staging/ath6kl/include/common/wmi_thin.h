//------------------------------------------------------------------------------
// <copyright file="wmi_thin.h" company="Atheros">
//    Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
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
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================

/*
 * This file contains the definitions of the WMI protocol specified in the
 * Wireless Module Interface (WMI).  It includes definitions of all the
 * commands and events. Commands are messages from the host to the WM.
 * Events and Replies are messages from the WM to the host.
 *
 * Ownership of correctness in regards to WMI commands
 * belongs to the host driver and the WM is not required to validate
 * parameters for value, proper range, or any other checking.
 *
 */

#ifndef _WMI_THIN_H_
#define _WMI_THIN_H_

#ifdef __cplusplus
extern "C" {
#endif


typedef enum {
    WMI_THIN_CONFIG_CMDID =  0x8000, // WMI_THIN_RESERVED_START 
    WMI_THIN_SET_MIB_CMDID,
    WMI_THIN_GET_MIB_CMDID,
    WMI_THIN_JOIN_CMDID,
    /* add new CMDID's here */
    WMI_THIN_RESERVED_END_CMDID = 0x8fff // WMI_THIN_RESERVED_END
} WMI_THIN_COMMAND_ID;

typedef enum{
    TEMPLATE_FRM_FIRST = 0,
    TEMPLATE_FRM_PROBE_REQ =TEMPLATE_FRM_FIRST,
    TEMPLATE_FRM_BEACON,
    TEMPLATE_FRM_PROBE_RESP,
    TEMPLATE_FRM_NULL,
    TEMPLATE_FRM_QOS_NULL,
    TEMPLATE_FRM_PSPOLL,
    TEMPLATE_FRM_MAX
}WMI_TEMPLATE_FRM_TYPE;

/* TEMPLATE_FRM_LEN... represent the maximum allowable
 * data lengths (bytes) for each frame type */
#define TEMPLATE_FRM_LEN_PROBE_REQ  (256) /* Symbian dictates a minimum of 256 for these 3 frame types */
#define TEMPLATE_FRM_LEN_BEACON     (256)
#define TEMPLATE_FRM_LEN_PROBE_RESP (256)
#define TEMPLATE_FRM_LEN_NULL       (32)
#define TEMPLATE_FRM_LEN_QOS_NULL   (32)
#define TEMPLATE_FRM_LEN_PSPOLL     (32)
#define TEMPLATE_FRM_LEN_SUM (TEMPLATE_FRM_LEN_PROBE_REQ + TEMPLATE_FRM_LEN_BEACON + TEMPLATE_FRM_LEN_PROBE_RESP + \
            TEMPLATE_FRM_LEN_NULL + TEMPLATE_FRM_LEN_QOS_NULL + TEMPLATE_FRM_LEN_PSPOLL)


/* MAC Header Build Rules */
/*  These values allow the host to configure the 
 *  target code that is responsible for constructing
 *  the MAC header.  In cases where the MAC header
 *  is provided by the host framework, the target
 *  has a diminished responsibility over what fields
 *  it must write. This will vary from framework to framework.
 *  Symbian requires different behavior from MAC80211 which
 *  requires different behavior from MS Native Wifi. */
#define WMI_WRT_VER_TYPE    0x00000001
#define WMI_WRT_DURATION    0x00000002
#define WMI_WRT_DIRECTION   0x00000004
#define WMI_WRT_POWER       0x00000008
#define WMI_WRT_WEP         0x00000010
#define WMI_WRT_MORE        0x00000020
#define WMI_WRT_BSSID       0x00000040
#define WMI_WRT_QOS         0x00000080
#define WMI_WRT_SEQNO       0x00000100
#define WMI_GUARD_TX        0x00000200 /* prevents TX ops that are not allowed for a current state */
#define WMI_WRT_DEFAULT_CONFIG  (WMI_WRT_VER_TYPE | WMI_WRT_DURATION | WMI_WRT_DIRECTION | \
                                 WMI_WRT_POWER | WMI_WRT_MORE | WMI_WRT_WEP | WMI_WRT_BSSID | \
                                 WMI_WRT_QOS | WMI_WRT_SEQNO | WMI_GUARD_TX)

/* WMI_THIN_CONFIG_TXCOMPLETE -- Used to configure the params and content for 
 *  TX Complete messages the will come from the Target.  these messages are 
 *  disabled by default but can be enabled using this structure and the 
 *  WMI_THIN_CONFIG_CMDID. */
typedef PREPACK struct {
    u8 version; /* the versioned type of messages to use or 0 to disable */
    u8 countThreshold; /* msg count threshold triggering a tx complete message */
    u16 timeThreshold; /* timeout interval in MSEC triggering a tx complete message */
} POSTPACK WMI_THIN_CONFIG_TXCOMPLETE;

/* WMI_THIN_CONFIG_DECRYPT_ERR -- Used to configure behavior for received frames 
 *  that have decryption errors.  The default behavior is to discard the frame
 *  without notification. Alternately, the MAC Header is forwarded to the host 
 *  with the failed status. */
typedef PREPACK struct {
    u8 enable; /* 1 == send decrypt errors to the host, 0 == don't */
    u8 reserved[3]; /* align padding */
} POSTPACK WMI_THIN_CONFIG_DECRYPT_ERR;

/* WMI_THIN_CONFIG_TX_MAC_RULES -- Used to configure behavior for transmitted
 *  frames that require partial MAC header construction. These rules 
 *  are used by the target to indicate which fields need to be written. */
typedef PREPACK struct {
    u32 rules; /* combination of WMI_WRT_... values */
} POSTPACK WMI_THIN_CONFIG_TX_MAC_RULES;

/* WMI_THIN_CONFIG_RX_FILTER_RULES -- Used to configure behavior for received
 *  frames as to which frames should get forwarded to the host and which
 *  should get processed internally. */
typedef PREPACK struct {
    u32 rules; /* combination of WMI_FILT_... values */
} POSTPACK WMI_THIN_CONFIG_RX_FILTER_RULES;

/* WMI_THIN_CONFIG_CMD -- Used to contain some combination of the above
 *  WMI_THIN_CONFIG_... structures. The actual combination is indicated 
 *  by the value of cfgField. Each bit in this field corresponds to 
 *  one of the above structures. */
typedef PREPACK struct {
#define WMI_THIN_CFG_TXCOMP         0x00000001
#define WMI_THIN_CFG_DECRYPT        0x00000002
#define WMI_THIN_CFG_MAC_RULES      0x00000004
#define WMI_THIN_CFG_FILTER_RULES   0x00000008
    u32 cfgField;   /* combination of WMI_THIN_CFG_... describes contents of config command */
    u16 length;     /* length in bytes of appended sub-commands */
    u8 reserved[2];   /* align padding */
} POSTPACK WMI_THIN_CONFIG_CMD;

/* MIB Access Identifiers tailored for Symbian. */
enum {
    MIB_ID_STA_MAC = 1,             // [READONLY]
    MIB_ID_RX_LIFE_TIME,            // [NOT IMPLEMENTED]
    MIB_ID_SLOT_TIME,               // [READ/WRITE]
    MIB_ID_RTS_THRESHOLD,           // [READ/WRITE]
    MIB_ID_CTS_TO_SELF,             // [READ/WRITE]
    MIB_ID_TEMPLATE_FRAME,          // [WRITE ONLY]
    MIB_ID_RXFRAME_FILTER,          // [READ/WRITE]
    MIB_ID_BEACON_FILTER_TABLE,     // [WRITE ONLY]
    MIB_ID_BEACON_FILTER,           // [READ/WRITE]
    MIB_ID_BEACON_LOST_COUNT,       // [WRITE ONLY]
    MIB_ID_RSSI_THRESHOLD,          // [WRITE ONLY]
    MIB_ID_HT_CAP,                  // [NOT IMPLEMENTED]
    MIB_ID_HT_OP,                   // [NOT IMPLEMENTED]
    MIB_ID_HT_2ND_BEACON,           // [NOT IMPLEMENTED]
    MIB_ID_HT_BLOCK_ACK,            // [NOT IMPLEMENTED]
    MIB_ID_PREAMBLE,                // [READ/WRITE]
    /*MIB_ID_GROUP_ADDR_TABLE,*/
    /*MIB_ID_WEP_DEFAULT_KEY_ID */
    /*MIB_ID_TX_POWER */
    /*MIB_ID_ARP_IP_TABLE */
    /*MIB_ID_SLEEP_MODE */
    /*MIB_ID_WAKE_INTERVAL*/
    /*MIB_ID_STAT_TABLE*/
    /*MIB_ID_IBSS_PWR_SAVE*/
    /*MIB_ID_COUNTERS_TABLE*/
    /*MIB_ID_ETHERTYPE_FILTER*/
    /*MIB_ID_BC_UDP_FILTER*/
       
};

typedef PREPACK struct {
    u8 addr[ATH_MAC_LEN];
} POSTPACK WMI_THIN_MIB_STA_MAC;

typedef PREPACK struct {
    u32 time; // units == msec
} POSTPACK WMI_THIN_MIB_RX_LIFE_TIME;

typedef PREPACK struct {
    u8 enable; //1 = on, 0 = off
} POSTPACK WMI_THIN_MIB_CTS_TO_SELF;

typedef PREPACK struct {
    u32 time; // units == usec
} POSTPACK WMI_THIN_MIB_SLOT_TIME;

typedef PREPACK struct {
    u16 length; //units == bytes
} POSTPACK WMI_THIN_MIB_RTS_THRESHOLD;

typedef PREPACK struct {
    u8 type; // type of frame
    u8 rate; // tx rate to be used (one of WMI_BIT_RATE)
    u16 length; // num bytes following this structure as the template data
} POSTPACK WMI_THIN_MIB_TEMPLATE_FRAME;

typedef PREPACK struct {
#define FRAME_FILTER_PROMISCUOUS 0x00000001
#define FRAME_FILTER_BSSID       0x00000002
    u32 filterMask;
} POSTPACK WMI_THIN_MIB_RXFRAME_FILTER;


#define IE_FILTER_TREATMENT_CHANGE 1
#define IE_FILTER_TREATMENT_APPEAR 2

typedef PREPACK struct {
    u8 ie;
    u8 treatment;
} POSTPACK WMI_THIN_MIB_BEACON_FILTER_TABLE;

typedef PREPACK struct {
    u8 ie;
    u8 treatment;
    u8 oui[3];
    u8 type;
    u16 version;
} POSTPACK WMI_THIN_MIB_BEACON_FILTER_TABLE_OUI;

typedef PREPACK struct {
    u16 numElements;
    u8 entrySize; // sizeof(WMI_THIN_MIB_BEACON_FILTER_TABLE) on host cpu may be 2 may be 4
    u8 reserved;
} POSTPACK WMI_THIN_MIB_BEACON_FILTER_TABLE_HEADER; 

typedef PREPACK struct {
    u32 count; /* num beacons between deliveries */
    u8 enable;
    u8 reserved[3];
} POSTPACK WMI_THIN_MIB_BEACON_FILTER;

typedef PREPACK struct {
    u32 count; /* num consec lost beacons after which send event */
} POSTPACK WMI_THIN_MIB_BEACON_LOST_COUNT;

typedef PREPACK struct {
    u8 rssi; /* the low threshold which can trigger an event warning */
    u8 tolerance; /* the range above and below the threshold to prevent event flooding to the host. */
    u8 count; /* the sample count of consecutive frames necessary to trigger an event. */
    u8 reserved[1]; /* padding */
} POSTPACK WMI_THIN_MIB_RSSI_THRESHOLD;


typedef PREPACK struct {
    u32 cap;
    u32 rxRateField;
    u32 beamForming;
    u8 addr[ATH_MAC_LEN];
    u8 enable;
    u8 stbc;
    u8 maxAMPDU;
    u8 msduSpacing;
    u8 mcsFeedback;
    u8 antennaSelCap;
} POSTPACK WMI_THIN_MIB_HT_CAP;

typedef PREPACK struct {
    u32 infoField;
    u32 basicRateField;
    u8 protection;
    u8 secondChanneloffset;
    u8 channelWidth;
    u8 reserved;
} POSTPACK WMI_THIN_MIB_HT_OP;

typedef PREPACK struct {
#define SECOND_BEACON_PRIMARY   1
#define SECOND_BEACON_EITHER    2
#define SECOND_BEACON_SECONDARY 3
    u8 cfg;
    u8 reserved[3]; /* padding */
} POSTPACK WMI_THIN_MIB_HT_2ND_BEACON;

typedef PREPACK struct {
    u8 txTIDField;
    u8 rxTIDField;
    u8 reserved[2]; /* padding */
} POSTPACK WMI_THIN_MIB_HT_BLOCK_ACK;

typedef PREPACK struct {
    u8 enableLong; // 1 == long preamble, 0 == short preamble
    u8 reserved[3];
} POSTPACK WMI_THIN_MIB_PREAMBLE;

typedef PREPACK struct {    
    u16 length;     /* the length in bytes of the appended MIB data */
    u8 mibID;      /* the ID of the MIB element being set */
    u8 reserved; /* align padding */
} POSTPACK WMI_THIN_SET_MIB_CMD;

typedef PREPACK struct {    
    u8 mibID;      /* the ID of the MIB element being set */
    u8 reserved[3]; /* align padding */
} POSTPACK WMI_THIN_GET_MIB_CMD;

typedef PREPACK struct {
    u32 basicRateMask; /* bit mask of basic rates */
    u32 beaconIntval; /* TUs */
    u16 atimWindow; /* TUs */
    u16 channel; /* frequency in Mhz */
    u8 networkType; /* INFRA_NETWORK | ADHOC_NETWORK */
    u8 ssidLength; /* 0 - 32 */
    u8 probe;      /* != 0 : issue probe req at start */
    u8 reserved;   /* alignment */
    u8     ssid[WMI_MAX_SSID_LEN];    
    u8 bssid[ATH_MAC_LEN];
} POSTPACK WMI_THIN_JOIN_CMD;

typedef PREPACK struct {
    u16 dtim; /* dtim interval in num beacons */
    u16 aid; /* 80211 AID from Assoc resp */
} POSTPACK WMI_THIN_POST_ASSOC_CMD;

typedef enum {
    WMI_THIN_EVENTID_RESERVED_START           = 0x8000,
    WMI_THIN_GET_MIB_EVENTID,
    WMI_THIN_JOIN_EVENTID,
    
    /* Add new THIN EVENTID's here */
    WMI_THIN_EVENTID_RESERVED_END           = 0x8fff    
} WMI_THIN_EVENT_ID;

/* Possible values for WMI_THIN_JOIN_EVENT.result */
typedef enum {
    WMI_THIN_JOIN_RES_SUCCESS = 0, // device has joined the network
    WMI_THIN_JOIN_RES_FAIL, // device failed for unspecified reason
    WMI_THIN_JOIN_RES_TIMEOUT, // device failed due to no beacon rx in time limit
    WMI_THIN_JOIN_RES_BAD_PARAM, // device failed due to bad cmd param.
}WMI_THIN_JOIN_RESULT;

typedef PREPACK struct {
    u8 result; /* the result of the join cmd. one of WMI_THIN_JOIN_RESULT */
    u8 reserved[3]; /* alignment */
} POSTPACK WMI_THIN_JOIN_EVENT;

#ifdef __cplusplus
}
#endif

#endif /* _WMI_THIN_H_ */
