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
 * File: mib.h
 *
 * Purpose: Implement MIB Data Structure
 *
 * Author: Tevin Chen
 *
 * Date: May 21, 1996
 *
 */

#ifndef __MIB_H__
#define __MIB_H__

#include "ttype.h"
#include "tether.h"
#include "desc.h"

/*---------------------  Export Definitions -------------------------*/


//
// USB counter
//
typedef struct tagSUSBCounter {
    u32 dwCrc;

} SUSBCounter, *PSUSBCounter;



//
// 802.11 counter
//


typedef struct tagSDot11Counters {
  /* unsigned long Length; // Length of structure */
    unsigned long long   TransmittedFragmentCount;
    unsigned long long   MulticastTransmittedFrameCount;
    unsigned long long   FailedCount;
    unsigned long long   RetryCount;
    unsigned long long   MultipleRetryCount;
    unsigned long long   RTSSuccessCount;
    unsigned long long   RTSFailureCount;
    unsigned long long   ACKFailureCount;
    unsigned long long   FrameDuplicateCount;
    unsigned long long   ReceivedFragmentCount;
    unsigned long long   MulticastReceivedFrameCount;
    unsigned long long   FCSErrorCount;
    unsigned long long   TKIPLocalMICFailures;
    unsigned long long   TKIPRemoteMICFailures;
    unsigned long long   TKIPICVErrors;
    unsigned long long   TKIPReplays;
    unsigned long long   CCMPFormatErrors;
    unsigned long long   CCMPReplays;
    unsigned long long   CCMPDecryptErrors;
    unsigned long long   FourWayHandshakeFailures;
  /*
   * unsigned long long   WEPUndecryptableCount;
   * unsigned long long   WEPICVErrorCount;
   * unsigned long long   DecryptSuccessCount;
   * unsigned long long   DecryptFailureCount;
   */
} SDot11Counters, *PSDot11Counters;


//
// MIB2 counter
//
typedef struct tagSMib2Counter {
    signed long    ifIndex;
    char    ifDescr[256];               // max size 255 plus zero ending
                                        // e.g. "interface 1"
    signed long    ifType;
    signed long    ifMtu;
    u32   ifSpeed;
    u8    ifPhysAddress[ETH_ALEN];
    signed long    ifAdminStatus;
    signed long    ifOperStatus;
    u32   ifLastChange;
    u32   ifInOctets;
    u32   ifInUcastPkts;
    u32   ifInNUcastPkts;
    u32   ifInDiscards;
    u32   ifInErrors;
    u32   ifInUnknownProtos;
    u32   ifOutOctets;
    u32   ifOutUcastPkts;
    u32   ifOutNUcastPkts;
    u32   ifOutDiscards;
    u32   ifOutErrors;
    u32   ifOutQLen;
    u32   ifSpecific;
} SMib2Counter, *PSMib2Counter;

// Value in the ifType entry
#define WIRELESSLANIEEE80211b      6           //

// Value in the ifAdminStatus/ifOperStatus entry
#define UP                  1           //
#define DOWN                2           //
#define TESTING             3           //


//
// RMON counter
//
typedef struct tagSRmonCounter {
    signed long    etherStatsIndex;
    u32   etherStatsDataSource;
    u32   etherStatsDropEvents;
    u32   etherStatsOctets;
    u32   etherStatsPkts;
    u32   etherStatsBroadcastPkts;
    u32   etherStatsMulticastPkts;
    u32   etherStatsCRCAlignErrors;
    u32   etherStatsUndersizePkts;
    u32   etherStatsOversizePkts;
    u32   etherStatsFragments;
    u32   etherStatsJabbers;
    u32   etherStatsCollisions;
    u32   etherStatsPkt64Octets;
    u32   etherStatsPkt65to127Octets;
    u32   etherStatsPkt128to255Octets;
    u32   etherStatsPkt256to511Octets;
    u32   etherStatsPkt512to1023Octets;
    u32   etherStatsPkt1024to1518Octets;
    u32   etherStatsOwners;
    u32   etherStatsStatus;
} SRmonCounter, *PSRmonCounter;

//
// Custom counter
//
typedef struct tagSCustomCounters {
    unsigned long       Length;

    unsigned long long   ullTsrAllOK;

    unsigned long long   ullRsr11M;
    unsigned long long   ullRsr5M;
    unsigned long long   ullRsr2M;
    unsigned long long   ullRsr1M;

    unsigned long long   ullRsr11MCRCOk;
    unsigned long long   ullRsr5MCRCOk;
    unsigned long long   ullRsr2MCRCOk;
    unsigned long long   ullRsr1MCRCOk;

    unsigned long long   ullRsr54M;
    unsigned long long   ullRsr48M;
    unsigned long long   ullRsr36M;
    unsigned long long   ullRsr24M;
    unsigned long long   ullRsr18M;
    unsigned long long   ullRsr12M;
    unsigned long long   ullRsr9M;
    unsigned long long   ullRsr6M;

    unsigned long long   ullRsr54MCRCOk;
    unsigned long long   ullRsr48MCRCOk;
    unsigned long long   ullRsr36MCRCOk;
    unsigned long long   ullRsr24MCRCOk;
    unsigned long long   ullRsr18MCRCOk;
    unsigned long long   ullRsr12MCRCOk;
    unsigned long long   ullRsr9MCRCOk;
    unsigned long long   ullRsr6MCRCOk;

} SCustomCounters, *PSCustomCounters;


//
// Custom counter
//
typedef struct tagSISRCounters {
    unsigned long   Length;

    u32   dwIsrTx0OK;
    u32   dwIsrAC0TxOK;
    u32   dwIsrBeaconTxOK;
    u32   dwIsrRx0OK;
    u32   dwIsrTBTTInt;
    u32   dwIsrSTIMERInt;
    u32   dwIsrWatchDog;
    u32   dwIsrUnrecoverableError;
    u32   dwIsrSoftInterrupt;
    u32   dwIsrMIBNearfull;
    u32   dwIsrRxNoBuf;

    u32   dwIsrUnknown;               // unknown interrupt count

    u32   dwIsrRx1OK;
    u32   dwIsrATIMTxOK;
    u32   dwIsrSYNCTxOK;
    u32   dwIsrCFPEnd;
    u32   dwIsrATIMEnd;
    u32   dwIsrSYNCFlushOK;
    u32   dwIsrSTIMER1Int;
    /////////////////////////////////////
} SISRCounters, *PSISRCounters;


// Value in the etherStatsStatus entry
#define VALID               1           //
#define CREATE_REQUEST      2           //
#define UNDER_CREATION      3           //
#define INVALID             4           //


//
// Tx packet information
//
typedef struct tagSTxPktInfo {
    u8    byBroadMultiUni;
    u16    wLength;
    u16    wFIFOCtl;
    u8    abyDestAddr[ETH_ALEN];
} STxPktInfo, *PSTxPktInfo;


#define MAX_RATE            12
//
// statistic counter
//
typedef struct tagSStatCounter {
    //
    // ISR status count
    //

    SISRCounters ISRStat;

    // RSR status count
    //
    u32   dwRsrFrmAlgnErr;
    u32   dwRsrErr;
    u32   dwRsrCRCErr;
    u32   dwRsrCRCOk;
    u32   dwRsrBSSIDOk;
    u32   dwRsrADDROk;
    u32   dwRsrBCNSSIDOk;
    u32   dwRsrLENErr;
    u32   dwRsrTYPErr;

    u32   dwNewRsrDECRYPTOK;
    u32   dwNewRsrCFP;
    u32   dwNewRsrUTSF;
    u32   dwNewRsrHITAID;
    u32   dwNewRsrHITAID0;

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
    unsigned long long   ullRsrOK;

    // for some optional OIDs (64 bits) and DMI support
    unsigned long long   ullRxBroadcastBytes;
    unsigned long long   ullRxMulticastBytes;
    unsigned long long   ullRxDirectedBytes;
    unsigned long long   ullRxBroadcastFrames;
    unsigned long long   ullRxMulticastFrames;
    unsigned long long   ullRxDirectedFrames;

    u32   dwRsrRxFragment;
    u32   dwRsrRxFrmLen64;
    u32   dwRsrRxFrmLen65_127;
    u32   dwRsrRxFrmLen128_255;
    u32   dwRsrRxFrmLen256_511;
    u32   dwRsrRxFrmLen512_1023;
    u32   dwRsrRxFrmLen1024_1518;

    // TSR status count
    //
    u32   dwTsrTotalRetry;        // total collision retry count
    u32   dwTsrOnceRetry;         // this packet only occur one collision
    u32   dwTsrMoreThanOnceRetry; // this packet occur more than one collision
    u32   dwTsrRetry;             // this packet has ever occur collision,
                                         // that is (dwTsrOnceCollision0 + dwTsrMoreThanOnceCollision0)
    u32   dwTsrACKData;
    u32   dwTsrErr;
    u32   dwAllTsrOK;
    u32   dwTsrRetryTimeout;
    u32   dwTsrTransmitTimeout;

    u32   dwTsrTxPacket;
    u32   dwTsrTxOctet;
    u32   dwTsrBroadcast;
    u32   dwTsrMulticast;
    u32   dwTsrDirected;

    // RD/TD count
    u32   dwCntRxFrmLength;
    u32   dwCntTxBufLength;

    u8    abyCntRxPattern[16];
    u8    abyCntTxPattern[16];



    // Software check....
    u32   dwCntRxDataErr;             // rx buffer data software compare CRC err count
    u32   dwCntDecryptErr;            // rx buffer data software compare CRC err count
    u32   dwCntRxICVErr;              // rx buffer data software compare CRC err count


    // 64-bit OID
    unsigned long long   ullTsrOK;

    // for some optional OIDs (64 bits) and DMI support
    unsigned long long   ullTxBroadcastFrames;
    unsigned long long   ullTxMulticastFrames;
    unsigned long long   ullTxDirectedFrames;
    unsigned long long   ullTxBroadcastBytes;
    unsigned long long   ullTxMulticastBytes;
    unsigned long long   ullTxDirectedBytes;

    // for autorate
    u32   dwTxOk[MAX_RATE+1];
    u32   dwTxFail[MAX_RATE+1];
    u32   dwTxRetryCount[8];

    STxPktInfo  abyTxPktInfo[16];

    SUSBCounter USB_EP0Stat;
    SUSBCounter USB_BulkInStat;
    SUSBCounter USB_BulkOutStat;
    SUSBCounter USB_InterruptStat;

    SCustomCounters CustomStat;

       //Tx count:
  unsigned long TxNoRetryOkCount;         /* success tx no retry ! */
  unsigned long TxRetryOkCount;           /* success tx but retry ! */
  unsigned long TxFailCount;              /* fail tx ? */
      //Rx count:
  unsigned long RxOkCnt;                  /* success rx ! */
  unsigned long RxFcsErrCnt;              /* fail rx ? */
      //statistic
    unsigned long SignalStren;
    unsigned long LinkQuality;

} SStatCounter, *PSStatCounter;

/*---------------------  Export Classes  ----------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

void STAvClearAllCounter(PSStatCounter pStatistic);

void STAvUpdateIsrStatCounter(PSStatCounter pStatistic,
			      u8 byIsr0,
			      u8 byIsr1);

void STAvUpdateRDStatCounter(PSStatCounter pStatistic,
			     u8 byRSR, u8 byNewRSR, u8 byRxSts,
			     u8 byRxRate, u8 * pbyBuffer,
			     unsigned int cbFrameLength);

void STAvUpdateRDStatCounterEx(PSStatCounter pStatistic,
			       u8 byRSR, u8 byNewRSR, u8 byRxSts,
			       u8 byRxRate, u8 * pbyBuffer,
			       unsigned int cbFrameLength);

void STAvUpdateTDStatCounter(PSStatCounter pStatistic, u8 byPktNum,
			     u8 byRate, u8 byTSR);

void
STAvUpdate802_11Counter(
    PSDot11Counters         p802_11Counter,
    PSStatCounter           pStatistic,
    u8                    byRTSSuccess,
    u8                    byRTSFail,
    u8                    byACKFail,
    u8                    byFCSErr
    );

void STAvClear802_11Counter(PSDot11Counters p802_11Counter);
void STAvUpdateUSBCounter(PSUSBCounter pUsbCounter, int ntStatus);

#endif /* __MIB_H__ */
