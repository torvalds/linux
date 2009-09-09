/*
 * Copyright (c) 1996, 2003 VIA Networking, Inc. All rights reserved.
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

#if !defined(__TTYPE_H__)
#include "ttype.h"
#endif


/*---------------------  Export Definitions -------------------------*/

#if !defined(DEF)
#define DEF
#endif

//typedef int BOOL;
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

} WMAC_CMD, DEF* PWMAC_CMD;

	typedef enum tagWZONETYPE {
  ZoneType_USA=0,
  ZoneType_Japan=1,
  ZoneType_Europe=2
}WZONETYPE;

#define ADHOC	0
#define INFRA	1
#define BOTH	2
#define AP	    3

#define ADHOC_STARTED	   1
#define ADHOC_JOINTED	   2


#define PHY80211a 	    0
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
	U8 	    name[16];
	void	*data;
	U16	    wResult;
	U16     wCmdCode;
} SCmdRequest, *PSCmdRequest;


//
// Scan
//

typedef struct tagSCmdScan {

    U8	    ssid[SSID_MAXLEN + 2];

} SCmdScan, *PSCmdScan;


//
// BSS Join
//

typedef struct tagSCmdBSSJoin {

    U16	    wBSSType;
    U16     wBBPType;
    U8	    ssid[SSID_MAXLEN + 2];
    U32	    uChannel;
    BOOL    bPSEnable;
    BOOL    bShareKeyAuth;

} SCmdBSSJoin, *PSCmdBSSJoin;

typedef struct tagSCmdZoneTypeSet {

 BOOL       bWrite;
 WZONETYPE  ZoneType;

} SCmdZoneTypeSet, *PSCmdZoneTypeSet;

#ifdef WPA_SM_Transtatus
typedef struct tagSWPAResult {
         char	ifname[100];
         U8		proto;
         U8   key_mgmt;
         U8   eap_type;
         BOOL authenticated;
} SWPAResult, *PSWPAResult;
#endif


typedef struct tagSCmdStartAP {

    U16	    wBSSType;
    U16     wBBPType;
    U8	    ssid[SSID_MAXLEN + 2];
    U32 	uChannel;
    U32     uBeaconInt;
    BOOL    bShareKeyAuth;
    U8      byBasicRate;

} SCmdStartAP, *PSCmdStartAP;


typedef struct tagSCmdSetWEP {

    BOOL    bEnableWep;
    U8      byKeyIndex;
    U8      abyWepKey[WEP_NKEYS][WEP_KEYMAXLEN];
    BOOL    bWepKeyAvailable[WEP_NKEYS];
    U32     auWepKeyLength[WEP_NKEYS];

} SCmdSetWEP, *PSCmdSetWEP;



typedef struct tagSBSSIDItem {

	U32	    uChannel;
    U8      abyBSSID[BSSID_LEN];
    U8      abySSID[SSID_MAXLEN + 1];
    //2006-1116-01,<Modify> by NomadZhao
    //U16	    wBeaconInterval;
    //U16	    wCapInfo;
    //U8      byNetType;
    U8      byNetType;
    U16	    wBeaconInterval;
    U16	    wCapInfo;        // for address of byNetType at align 4

    BOOL    bWEPOn;
    U32     uRSSI;

} SBSSIDItem;


typedef struct tagSBSSIDList {

	U32		    uItem;
	SBSSIDItem	sBSSIDList[0];
} SBSSIDList, *PSBSSIDList;


typedef struct tagSCmdLinkStatus {

    BOOL    bLink;
	U16	    wBSSType;
	U8      byState;
    U8      abyBSSID[BSSID_LEN];
    U8      abySSID[SSID_MAXLEN + 2];
    U32     uChannel;
    U32     uLinkRate;

} SCmdLinkStatus, *PSCmdLinkStatus;

//
// 802.11 counter
//
typedef struct tagSDot11MIBCount {
    U32 TransmittedFragmentCount;
    U32 MulticastTransmittedFrameCount;
    U32 FailedCount;
    U32 RetryCount;
    U32 MultipleRetryCount;
    U32 RTSSuccessCount;
    U32 RTSFailureCount;
    U32 ACKFailureCount;
    U32 FrameDuplicateCount;
    U32 ReceivedFragmentCount;
    U32 MulticastReceivedFrameCount;
    U32 FCSErrorCount;
} SDot11MIBCount, DEF* PSDot11MIBCount;



//
// statistic counter
//
typedef struct tagSStatMIBCount {
    //
    // ISR status count
    //
    U32   dwIsrTx0OK;
    U32   dwIsrTx1OK;
    U32   dwIsrBeaconTxOK;
    U32   dwIsrRxOK;
    U32   dwIsrTBTTInt;
    U32   dwIsrSTIMERInt;
    U32   dwIsrUnrecoverableError;
    U32   dwIsrSoftInterrupt;
    U32   dwIsrRxNoBuf;
    /////////////////////////////////////

    U32   dwIsrUnknown;               // unknown interrupt count

    // RSR status count
    //
    U32   dwRsrFrmAlgnErr;
    U32   dwRsrErr;
    U32   dwRsrCRCErr;
    U32   dwRsrCRCOk;
    U32   dwRsrBSSIDOk;
    U32   dwRsrADDROk;
    U32   dwRsrICVOk;
    U32   dwNewRsrShortPreamble;
    U32   dwRsrLong;
    U32   dwRsrRunt;

    U32   dwRsrRxControl;
    U32   dwRsrRxData;
    U32   dwRsrRxManage;

    U32   dwRsrRxPacket;
    U32   dwRsrRxOctet;
    U32   dwRsrBroadcast;
    U32   dwRsrMulticast;
    U32   dwRsrDirected;
    // 64-bit OID
    U32   ullRsrOK;

    // for some optional OIDs (64 bits) and DMI support
    U32   ullRxBroadcastBytes;
    U32   ullRxMulticastBytes;
    U32   ullRxDirectedBytes;
    U32   ullRxBroadcastFrames;
    U32   ullRxMulticastFrames;
    U32   ullRxDirectedFrames;

    U32   dwRsrRxFragment;
    U32   dwRsrRxFrmLen64;
    U32   dwRsrRxFrmLen65_127;
    U32   dwRsrRxFrmLen128_255;
    U32   dwRsrRxFrmLen256_511;
    U32   dwRsrRxFrmLen512_1023;
    U32   dwRsrRxFrmLen1024_1518;

    // TSR0,1 status count
    //
    U32   dwTsrTotalRetry[2];        // total collision retry count
    U32   dwTsrOnceRetry[2];         // this packet only occur one collision
    U32   dwTsrMoreThanOnceRetry[2]; // this packet occur more than one collision
    U32   dwTsrRetry[2];             // this packet has ever occur collision,
                                       // that is (dwTsrOnceCollision0 + dwTsrMoreThanOnceCollision0)
    U32   dwTsrACKData[2];
    U32   dwTsrErr[2];
    U32   dwAllTsrOK[2];
    U32   dwTsrRetryTimeout[2];
    U32   dwTsrTransmitTimeout[2];

    U32   dwTsrTxPacket[2];
    U32   dwTsrTxOctet[2];
    U32   dwTsrBroadcast[2];
    U32   dwTsrMulticast[2];
    U32   dwTsrDirected[2];

    // RD/TD count
    U32   dwCntRxFrmLength;
    U32   dwCntTxBufLength;

    U8    abyCntRxPattern[16];
    U8    abyCntTxPattern[16];

    // Software check....
    U32   dwCntRxDataErr;             // rx buffer data software compare CRC err count
    U32   dwCntDecryptErr;            // rx buffer data software compare CRC err count
    U32   dwCntRxICVErr;              // rx buffer data software compare CRC err count
    U32    idxRxErrorDesc;             // index for rx data error RD

    // 64-bit OID
    U32   ullTsrOK[2];

    // for some optional OIDs (64 bits) and DMI support
    U32   ullTxBroadcastFrames[2];
    U32   ullTxMulticastFrames[2];
    U32   ullTxDirectedFrames[2];
    U32   ullTxBroadcastBytes[2];
    U32   ullTxMulticastBytes[2];
    U32   ullTxDirectedBytes[2];
} SStatMIBCount, DEF* PSStatMIBCount;


typedef struct tagSNodeItem {
    // STA info
    U16            wAID;
    U8             abyMACAddr[6];
    U16            wTxDataRate;
    U16            wInActiveCount;
    U16            wEnQueueCnt;
    U16            wFlags;
    BOOL           bPWBitOn;
    U8             byKeyIndex;
    U16            wWepKeyLength;
    U8            abyWepKey[WEP_KEYMAXLEN];
    // Auto rate fallback vars
    BOOL           bIsInFallback;
    U32            uTxFailures;
    U32            uTxAttempts;
    U16            wFailureRatio;

} SNodeItem;


typedef struct tagSNodeList {

	U32		    uItem;
	SNodeItem	sNodeList[0];

} SNodeList, *PSNodeList;



typedef struct tagSCmdValue {

    U32     dwValue;

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


#define VIAWGET_HOSTAPD_GENERIC_ELEMENT_HDR_LEN \
((int) (&((struct viawget_hostapd_param *) 0)->u.generic_elem.data))

// Maximum length for algorithm names (-1 for nul termination) used in ioctl()



struct viawget_hostapd_param {
	U32 cmd;
	U8 sta_addr[6];
	union {
		struct {
			U16 aid;
			U16 capability;
			U8 tx_supp_rates;
		} add_sta;
		struct {
			U32 inactive_sec;
		} get_info_sta;
		struct {
			U8 alg;
			U32 flags;
			U32 err;
			U8 idx;
			U8 seq[8];
			U16 key_len;
			U8 key[0];
		} crypt;
		struct {
			U32 flags_and;
			U32 flags_or;
		} set_flags_sta;
		struct {
			U16 rid;
			U16 len;
			U8 data[0];
		} rid;
		struct {
			U8 len;
			U8 data[0];
		} generic_elem;
		struct {
			U16 cmd;
			U16 reason_code;
		} mlme;
		struct {
			U8 ssid_len;
			U8 ssid[32];
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
