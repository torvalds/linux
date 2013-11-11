/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * File: iocmd.h
 *
 * Purpose: Handles the viawget ioctl private interface functions
 *
 * Author: Lyndon Chen
 *
 * Date: May 8, 2002
 *
 */

#ifndef __IOCMD_H__
#define __IOCMD_H__

#include "ttype.h"

/*---------------------  Export Definitions -------------------------*/

//typedef uint32_t u32;
//typedef uint16_t u16;
//typedef uint8_t u8;

// ioctl Command code
#define MAGIC_CODE	                 0x3142
#define IOCTL_CMD_TEST	            (SIOCDEVPRIVATE + 0)
#define IOCTL_CMD_SET			    (SIOCDEVPRIVATE + 1)
#define IOCTL_CMD_HOSTAPD           (SIOCDEVPRIVATE + 2)
#define IOCTL_CMD_WPA               (SIOCDEVPRIVATE + 3)

typedef enum tagWMAC_CMD {
	WLAN_CMD_BSS_SCAN,
	WLAN_CMD_BSS_JOIN,
	WLAN_CMD_DISASSOC,
	WLAN_CMD_SET_WEP,
	WLAN_CMD_GET_LINK,
	WLAN_CMD_GET_LISTLEN,
	WLAN_CMD_GET_LIST,
	WLAN_CMD_GET_MIB,
	WLAN_CMD_GET_STAT,
	WLAN_CMD_STOP_MAC,
	WLAN_CMD_START_MAC,
	WLAN_CMD_AP_START,
	WLAN_CMD_SET_HOSTAPD,
	WLAN_CMD_SET_HOSTAPD_STA,
	WLAN_CMD_SET_802_1X,
	WLAN_CMD_SET_HOST_WEP,
	WLAN_CMD_SET_WPA,
	WLAN_CMD_GET_NODE_CNT,
	WLAN_CMD_ZONETYPE_SET,
	WLAN_CMD_GET_NODE_LIST
} WMAC_CMD, *PWMAC_CMD;

typedef enum tagWZONETYPE {
	ZoneType_USA = 0,
	ZoneType_Japan = 1,
	ZoneType_Europe = 2
} WZONETYPE;

#define ADHOC	0
#define INFRA	1
#define BOTH	2
#define AP	    3

#define ADHOC_STARTED	   1
#define ADHOC_JOINTED	   2

#define PHY80211a       0
#define PHY80211b       1
#define PHY80211g       2

#define SSID_ID                0
#define SSID_MAXLEN            32
#define BSSID_LEN              6
#define WEP_NKEYS              4
#define WEP_KEYMAXLEN          29
#define WEP_40BIT_LEN          5
#define WEP_104BIT_LEN         13
#define WEP_232BIT_LEN         16

// Ioctl interface structure
// Command structure
//
#pragma pack(1)
typedef struct tagSCmdRequest {
	u8	    name[16];
	void	*data;
	u16	    wResult;
	u16     wCmdCode;
} SCmdRequest, *PSCmdRequest;

//
// Scan
//

typedef struct tagSCmdScan {
	u8 ssid[SSID_MAXLEN + 2];
} SCmdScan, *PSCmdScan;

//
// BSS Join
//

typedef struct tagSCmdBSSJoin {
	u16	    wBSSType;
	u16     wBBPType;
	u8	    ssid[SSID_MAXLEN + 2];
	u32	    uChannel;
	bool bPSEnable;
	bool bShareKeyAuth;
} SCmdBSSJoin, *PSCmdBSSJoin;

//
// Zonetype Setting
//

typedef struct tagSCmdZoneTypeSet {
	bool bWrite;
	WZONETYPE  ZoneType;
} SCmdZoneTypeSet, *PSCmdZoneTypeSet;

#ifdef WPA_SM_Transtatus
typedef struct tagSWPAResult {
	char	ifname[100];
	u8 proto;
	u8 key_mgmt;
	u8 eap_type;
	bool authenticated;
} SWPAResult, *PSWPAResult;
#endif

typedef struct tagSCmdStartAP {
	u16	    wBSSType;
	u16     wBBPType;
	u8	    ssid[SSID_MAXLEN + 2];
	u32	    uChannel;
	u32     uBeaconInt;
	bool bShareKeyAuth;
	u8      byBasicRate;
} SCmdStartAP, *PSCmdStartAP;

typedef struct tagSCmdSetWEP {
	bool bEnableWep;
	u8      byKeyIndex;
	u8      abyWepKey[WEP_NKEYS][WEP_KEYMAXLEN];
	bool bWepKeyAvailable[WEP_NKEYS];
	u32     auWepKeyLength[WEP_NKEYS];
} SCmdSetWEP, *PSCmdSetWEP;

typedef struct tagSBSSIDItem {
	u32	    uChannel;
	u8      abyBSSID[BSSID_LEN];
	u8      abySSID[SSID_MAXLEN + 1];
	//2006-1116-01,<Modify> by NomadZhao
	//u16	    wBeaconInterval;
	//u16	    wCapInfo;
	//u8      byNetType;
	u8      byNetType;
	u16	    wBeaconInterval;
	u16	    wCapInfo;        // for address of byNetType at align 4

	bool bWEPOn;
	u32     uRSSI;
} SBSSIDItem;

typedef struct tagSBSSIDList {
	u32		    uItem;
	SBSSIDItem	sBSSIDList[0];
} SBSSIDList, *PSBSSIDList;

typedef struct tagSCmdLinkStatus {
	bool bLink;
	u16   wBSSType;
	u8      byState;
	u8      abyBSSID[BSSID_LEN];
	u8      abySSID[SSID_MAXLEN + 2];
	u32     uChannel;
	u32     uLinkRate;
} SCmdLinkStatus, *PSCmdLinkStatus;

//
// 802.11 counter
//
typedef struct tagSDot11MIBCount {
	u32 TransmittedFragmentCount;
	u32 MulticastTransmittedFrameCount;
	u32 FailedCount;
	u32 RetryCount;
	u32 MultipleRetryCount;
	u32 RTSSuccessCount;
	u32 RTSFailureCount;
	u32 ACKFailureCount;
	u32 FrameDuplicateCount;
	u32 ReceivedFragmentCount;
	u32 MulticastReceivedFrameCount;
	u32 FCSErrorCount;
} SDot11MIBCount, *PSDot11MIBCount;

//
// statistic counter
//
typedef struct tagSStatMIBCount {
	//
	// ISR status count
	//
	u32   dwIsrTx0OK;
	u32   dwIsrTx1OK;
	u32   dwIsrBeaconTxOK;
	u32   dwIsrRxOK;
	u32   dwIsrTBTTInt;
	u32   dwIsrSTIMERInt;
	u32   dwIsrUnrecoverableError;
	u32   dwIsrSoftInterrupt;
	u32   dwIsrRxNoBuf;
	/////////////////////////////////////

	u32   dwIsrUnknown;               // unknown interrupt count

	// RSR status count
	//
	u32   dwRsrFrmAlgnErr;
	u32   dwRsrErr;
	u32   dwRsrCRCErr;
	u32   dwRsrCRCOk;
	u32   dwRsrBSSIDOk;
	u32   dwRsrADDROk;
	u32   dwRsrICVOk;
	u32   dwNewRsrShortPreamble;
	u32   dwRsrLong;
	u32   dwRsrRunt;

	u32   dwRsrRxControl;
	u32   dwRsrRxData;
	u32   dwRsrRxManage;

	u32   dwRsrRxPacket;
	u32   dwRsrRxOctet;
	u32   dwRsrBroadcast;
	u32   dwRsrMulticast;
	u32   dwRsrDirected;
	// 64-bit OID
	u32   ullRsrOK;

	// for some optional OIDs (64 bits) and DMI support
	u32   ullRxBroadcastBytes;
	u32   ullRxMulticastBytes;
	u32   ullRxDirectedBytes;
	u32   ullRxBroadcastFrames;
	u32   ullRxMulticastFrames;
	u32   ullRxDirectedFrames;

	u32   dwRsrRxFragment;
	u32   dwRsrRxFrmLen64;
	u32   dwRsrRxFrmLen65_127;
	u32   dwRsrRxFrmLen128_255;
	u32   dwRsrRxFrmLen256_511;
	u32   dwRsrRxFrmLen512_1023;
	u32   dwRsrRxFrmLen1024_1518;

	// TSR0,1 status count
	//
	u32   dwTsrTotalRetry[2];        // total collision retry count
	u32   dwTsrOnceRetry[2];         // this packet only occur one collision
	u32   dwTsrMoreThanOnceRetry[2]; // this packet occur more than one collision
	u32   dwTsrRetry[2];             // this packet has ever occur collision,
	// that is (dwTsrOnceCollision0 + dwTsrMoreThanOnceCollision0)
	u32   dwTsrACKData[2];
	u32   dwTsrErr[2];
	u32   dwAllTsrOK[2];
	u32   dwTsrRetryTimeout[2];
	u32   dwTsrTransmitTimeout[2];

	u32   dwTsrTxPacket[2];
	u32   dwTsrTxOctet[2];
	u32   dwTsrBroadcast[2];
	u32   dwTsrMulticast[2];
	u32   dwTsrDirected[2];

	// RD/TD count
	u32   dwCntRxFrmLength;
	u32   dwCntTxBufLength;

	u8    abyCntRxPattern[16];
	u8    abyCntTxPattern[16];

	// Software check....
	u32   dwCntRxDataErr;             // rx buffer data software compare CRC err count
	u32   dwCntDecryptErr;            // rx buffer data software compare CRC err count
	u32   dwCntRxICVErr;              // rx buffer data software compare CRC err count
	u32    idxRxErrorDesc;             // index for rx data error RD

	// 64-bit OID
	u32   ullTsrOK[2];

	// for some optional OIDs (64 bits) and DMI support
	u32   ullTxBroadcastFrames[2];
	u32   ullTxMulticastFrames[2];
	u32   ullTxDirectedFrames[2];
	u32   ullTxBroadcastBytes[2];
	u32   ullTxMulticastBytes[2];
	u32   ullTxDirectedBytes[2];
} SStatMIBCount, *PSStatMIBCount;

typedef struct tagSNodeItem {
	// STA info
	u16            wAID;
	u8             abyMACAddr[6];
	u16            wTxDataRate;
	u16            wInActiveCount;
	u16            wEnQueueCnt;
	u16            wFlags;
	bool bPWBitOn;
	u8             byKeyIndex;
	u16            wWepKeyLength;
	u8            abyWepKey[WEP_KEYMAXLEN];
	// Auto rate fallback vars
	bool bIsInFallback;
	u32            uTxFailures;
	u32            uTxAttempts;
	u16            wFailureRatio;
} SNodeItem;

typedef struct tagSNodeList {
	u32		    uItem;
	SNodeItem	sNodeList[0];
} SNodeList, *PSNodeList;

typedef struct tagSCmdValue {
	u32 dwValue;
} SCmdValue,  *PSCmdValue;

//
// hostapd & viawget ioctl related
//

// VIAGWET_IOCTL_HOSTAPD ioctl() cmd:
enum {
	VIAWGET_HOSTAPD_FLUSH = 1,
	VIAWGET_HOSTAPD_ADD_STA = 2,
	VIAWGET_HOSTAPD_REMOVE_STA = 3,
	VIAWGET_HOSTAPD_GET_INFO_STA = 4,
	VIAWGET_HOSTAPD_SET_ENCRYPTION = 5,
	VIAWGET_HOSTAPD_GET_ENCRYPTION = 6,
	VIAWGET_HOSTAPD_SET_FLAGS_STA = 7,
	VIAWGET_HOSTAPD_SET_ASSOC_AP_ADDR = 8,
	VIAWGET_HOSTAPD_SET_GENERIC_ELEMENT = 9,
	VIAWGET_HOSTAPD_MLME = 10,
	VIAWGET_HOSTAPD_SCAN_REQ = 11,
	VIAWGET_HOSTAPD_STA_CLEAR_STATS = 12,
};

#define VIAWGET_HOSTAPD_GENERIC_ELEMENT_HDR_LEN				\
	((int)(&((struct viawget_hostapd_param *)0)->u.generic_elem.data))

// Maximum length for algorithm names (-1 for nul termination) used in ioctl()

struct viawget_hostapd_param {
	u32 cmd;
	u8 sta_addr[6];
	union {
		struct {
			u16 aid;
			u16 capability;
			u8 tx_supp_rates;
		} add_sta;
		struct {
			u32 inactive_sec;
		} get_info_sta;
		struct {
			u8 alg;
			u32 flags;
			u32 err;
			u8 idx;
			u8 seq[8];
			u16 key_len;
			u8 key[0];
		} crypt;
		struct {
			u32 flags_and;
			u32 flags_or;
		} set_flags_sta;
		struct {
			u16 rid;
			u16 len;
			u8 data[0];
		} rid;
		struct {
			u8 len;
			u8 data[0];
		} generic_elem;
		struct {
			u16 cmd;
			u16 reason_code;
		} mlme;
		struct {
			u8 ssid_len;
			u8 ssid[32];
		} scan_req;
	} u;
};

//2006-1116-01,<Add> by NomadZhao
#pragma pack()

/*---------------------  Export Classes  ----------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Types  ------------------------------*/

/*---------------------  Export Functions  --------------------------*/

#endif //__IOCMD_H__
