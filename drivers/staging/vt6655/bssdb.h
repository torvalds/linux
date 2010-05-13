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
 *
 * File: bssdb.h
 *
 * Purpose: Handles the Basic Service Set & Node Database functions
 *
 * Author: Lyndon Chen
 *
 * Date: July 16, 2002
 *
 */

#ifndef __BSSDB_H__
#define __BSSDB_H__

#include <linux/skbuff.h>
#include "80211hdr.h"
#include "80211mgr.h"
#include "card.h"

/*---------------------  Export Definitions -------------------------*/

#define MAX_NODE_NUM             64
#define MAX_BSS_NUM              42
#define LOST_BEACON_COUNT      	 10   // 10 sec, XP defined
#define MAX_PS_TX_BUF            32   // sta max power saving tx buf
#define ADHOC_LOST_BEACON_COUNT  30   // 30 sec, beacon lost for adhoc only
#define MAX_INACTIVE_COUNT       300  // 300 sec, inactive STA node refresh

#define USE_PROTECT_PERIOD       10   // 10 sec, Use protect mode check period
#define ERP_RECOVER_COUNT        30   // 30 sec, ERP support callback check
#define BSS_CLEAR_COUNT           1

#define RSSI_STAT_COUNT          10
#define MAX_CHECK_RSSI_COUNT     8

// STA dwflags
#define WLAN_STA_AUTH            BIT0
#define WLAN_STA_ASSOC           BIT1
#define WLAN_STA_PS              BIT2
#define WLAN_STA_TIM             BIT3
// permanent; do not remove entry on expiration
#define WLAN_STA_PERM            BIT4
// If 802.1X is used, this flag is
// controlling whether STA is authorized to
// send and receive non-IEEE 802.1X frames
#define WLAN_STA_AUTHORIZED      BIT5

#define MAX_RATE            12

#define MAX_WPA_IE_LEN      64


/*---------------------  Export Classes  ----------------------------*/

/*---------------------  Export Variables  --------------------------*/


/*---------------------  Export Types  ------------------------------*/

//
// IEEE 802.11 Structures and definitions
//

typedef enum _NDIS_802_11_NETWORK_TYPE
{
    Ndis802_11FH,
    Ndis802_11DS,
    Ndis802_11OFDM5,
    Ndis802_11OFDM24,
    Ndis802_11NetworkTypeMax    // not a real type, defined as an upper bound
} NDIS_802_11_NETWORK_TYPE, *PNDIS_802_11_NETWORK_TYPE;


typedef struct tagSERPObject {
    BOOL    bERPExist;
    BYTE    byERP;
}ERPObject, *PERPObject;


typedef struct tagSRSNCapObject {
    BOOL    bRSNCapExist;
    WORD    wRSNCap;
}SRSNCapObject, *PSRSNCapObject;

// BSS info(AP)
#pragma pack(1)
typedef struct tagKnownBSS {
    // BSS info
    BOOL            bActive;
    BYTE            abyBSSID[WLAN_BSSID_LEN];
    UINT            uChannel;
    BYTE            abySuppRates[WLAN_IEHDR_LEN + WLAN_RATES_MAXLEN + 1];
    BYTE            abyExtSuppRates[WLAN_IEHDR_LEN + WLAN_RATES_MAXLEN + 1];
    UINT            uRSSI;
    BYTE            bySQ;
    WORD            wBeaconInterval;
    WORD            wCapInfo;
    BYTE            abySSID[WLAN_IEHDR_LEN + WLAN_SSID_MAXLEN + 1];
    BYTE            byRxRate;

//    WORD            wATIMWindow;
    BYTE            byRSSIStatCnt;
    LONG            ldBmMAX;
    LONG            ldBmAverage[RSSI_STAT_COUNT];
    LONG            ldBmAverRange;
    //For any BSSID selection improvment
    BOOL            bSelected;

    //++ WPA informations
    BOOL            bWPAValid;
    BYTE            byGKType;
    BYTE            abyPKType[4];
    WORD            wPKCount;
    BYTE            abyAuthType[4];
    WORD            wAuthCount;
    BYTE            byDefaultK_as_PK;
    BYTE            byReplayIdx;
    //--

    //++ WPA2 informations
    BOOL            bWPA2Valid;
    BYTE            byCSSGK;
    WORD            wCSSPKCount;
    BYTE            abyCSSPK[4];
    WORD            wAKMSSAuthCount;
    BYTE            abyAKMSSAuthType[4];

    //++  wpactl
    BYTE            byWPAIE[MAX_WPA_IE_LEN];
    BYTE            byRSNIE[MAX_WPA_IE_LEN];
    WORD            wWPALen;
    WORD            wRSNLen;

    // Clear count
    UINT            uClearCount;
//    BYTE            abyIEs[WLAN_BEACON_FR_MAXLEN];
    UINT            uIELength;
    QWORD           qwBSSTimestamp;
    QWORD           qwLocalTSF;     // local TSF timer

//    NDIS_802_11_NETWORK_TYPE    NetworkTypeInUse;
    CARD_PHY_TYPE   eNetworkTypeInUse;

    ERPObject       sERP;
    SRSNCapObject   sRSNCapObj;
    BYTE            abyIEs[1024];   // don't move this field !!

}__attribute__ ((__packed__))
KnownBSS , *PKnownBSS;

//2006-1116-01,<Add> by NomadZhao
#pragma pack()

typedef enum tagNODE_STATE {
    NODE_FREE,
    NODE_AGED,
    NODE_KNOWN,
    NODE_AUTH,
    NODE_ASSOC
} NODE_STATE, *PNODE_STATE;


// STA node info
typedef struct tagKnownNodeDB {
    // STA info
    BOOL            bActive;
    BYTE            abyMACAddr[WLAN_ADDR_LEN];
    BYTE            abyCurrSuppRates[WLAN_IEHDR_LEN + WLAN_RATES_MAXLEN];
    BYTE            abyCurrExtSuppRates[WLAN_IEHDR_LEN + WLAN_RATES_MAXLEN];
    WORD            wTxDataRate;
    BOOL            bShortPreamble;
    BOOL            bERPExist;
    BOOL            bShortSlotTime;
    UINT            uInActiveCount;
    WORD            wMaxBasicRate;     //Get from byTopOFDMBasicRate or byTopCCKBasicRate which depends on packetTyp.
    WORD            wMaxSuppRate;      //Records the highest supported rate getting from SuppRates IE and ExtSuppRates IE in Beacon.
    WORD            wSuppRate;
    BYTE            byTopOFDMBasicRate;//Records the highest basic rate in OFDM mode
    BYTE            byTopCCKBasicRate; //Records the highest basic rate in CCK mode

    // For AP mode
    struct sk_buff_head sTxPSQueue;
    WORD            wCapInfo;
    WORD            wListenInterval;
    WORD            wAID;
    NODE_STATE      eNodeState;
    BOOL            bPSEnable;
    BOOL            bRxPSPoll;
    BYTE            byAuthSequence;
    ULONG           ulLastRxJiffer;
    BYTE            bySuppRate;
    DWORD           dwFlags;
    WORD            wEnQueueCnt;

    BOOL            bOnFly;
    ULONGLONG       KeyRSC;
    BYTE            byKeyIndex;
    DWORD           dwKeyIndex;
    BYTE            byCipherSuite;
    DWORD           dwTSC47_16;
    WORD            wTSC15_0;
    UINT            uWepKeyLength;
    BYTE            abyWepKey[WLAN_WEPMAX_KEYLEN];
    //
    // Auto rate fallback vars
    BOOL            bIsInFallback;
    UINT            uAverageRSSI;
    UINT            uRateRecoveryTimeout;
    UINT            uRatePollTimeout;
    UINT            uTxFailures;
    UINT            uTxAttempts;

    UINT            uTxRetry;
    UINT            uFailureRatio;
    UINT            uRetryRatio;
    UINT            uTxOk[MAX_RATE+1];
    UINT            uTxFail[MAX_RATE+1];
    UINT            uTimeCount;

} KnownNodeDB, *PKnownNodeDB;


/*---------------------  Export Functions  --------------------------*/



PKnownBSS
BSSpSearchBSSList(
    void *hDeviceContext,
    PBYTE pbyDesireBSSID,
    PBYTE pbyDesireSSID,
    CARD_PHY_TYPE ePhyType
    );

PKnownBSS
BSSpAddrIsInBSSList(
    void *hDeviceContext,
    PBYTE abyBSSID,
    PWLAN_IE_SSID pSSID
    );

void
BSSvClearBSSList(
    void *hDeviceContext,
    BOOL bKeepCurrBSSID
    );

BOOL
BSSbInsertToBSSList(
    void *hDeviceContext,
    PBYTE abyBSSIDAddr,
    QWORD qwTimestamp,
    WORD wBeaconInterval,
    WORD wCapInfo,
    BYTE byCurrChannel,
    PWLAN_IE_SSID pSSID,
    PWLAN_IE_SUPP_RATES pSuppRates,
    PWLAN_IE_SUPP_RATES pExtSuppRates,
    PERPObject psERP,
    PWLAN_IE_RSN pRSN,
    PWLAN_IE_RSN_EXT pRSNWPA,
    PWLAN_IE_COUNTRY pIE_Country,
    PWLAN_IE_QUIET pIE_Quiet,
    UINT uIELength,
    PBYTE pbyIEs,
    void *pRxPacketContext
    );


BOOL
BSSbUpdateToBSSList(
    void *hDeviceContext,
    QWORD qwTimestamp,
    WORD wBeaconInterval,
    WORD wCapInfo,
    BYTE byCurrChannel,
    BOOL bChannelHit,
    PWLAN_IE_SSID pSSID,
    PWLAN_IE_SUPP_RATES pSuppRates,
    PWLAN_IE_SUPP_RATES pExtSuppRates,
    PERPObject psERP,
    PWLAN_IE_RSN pRSN,
    PWLAN_IE_RSN_EXT pRSNWPA,
    PWLAN_IE_COUNTRY pIE_Country,
    PWLAN_IE_QUIET pIE_Quiet,
    PKnownBSS pBSSList,
    UINT uIELength,
    PBYTE pbyIEs,
    void *pRxPacketContext
    );


BOOL
BSSDBbIsSTAInNodeDB(
    void *hDeviceContext,
    PBYTE abyDstAddr,
    PUINT puNodeIndex
    );

void
BSSvCreateOneNode(
    void *hDeviceContext,
    PUINT puNodeIndex
    );

void
BSSvUpdateAPNode(
    void *hDeviceContext,
    PWORD pwCapInfo,
    PWLAN_IE_SUPP_RATES pItemRates,
    PWLAN_IE_SUPP_RATES pExtSuppRates
    );


void
BSSvSecondCallBack(
    void *hDeviceContext
    );


void
BSSvUpdateNodeTxCounter(
    void *hDeviceContext,
    BYTE        byTsr0,
    BYTE        byTsr1,
    PBYTE       pbyBuffer,
    UINT        uFIFOHeaderSize
    );

void
BSSvRemoveOneNode(
    void *hDeviceContext,
    UINT uNodeIndex
    );

void
BSSvAddMulticastNode(
    void *hDeviceContext
    );


void
BSSvClearNodeDBTable(
    void *hDeviceContext,
    UINT uStartIndex
    );

void
BSSvClearAnyBSSJoinRecord(
    void *hDeviceContext
    );

#endif //__BSSDB_H__
