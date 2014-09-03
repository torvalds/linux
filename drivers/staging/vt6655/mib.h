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

//
// 802.11 counter
//

typedef struct tagSDot11Counters {
	unsigned long Length;
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
	unsigned long long   TKIPCounterMeasuresInvoked;
	unsigned long long   TKIPReplays;
	unsigned long long   CCMPFormatErrors;
	unsigned long long   CCMPReplays;
	unsigned long long   CCMPDecryptErrors;
	unsigned long long   FourWayHandshakeFailures;
} SDot11Counters, *PSDot11Counters;

//
// MIB2 counter
//
typedef struct tagSMib2Counter {
	long    ifIndex;
	char    ifDescr[256];
	long    ifType;
	long    ifMtu;
	unsigned long ifSpeed;
	unsigned char ifPhysAddress[ETH_ALEN];
	long    ifAdminStatus;
	long    ifOperStatus;
	unsigned long ifLastChange;
	unsigned long ifInOctets;
	unsigned long ifInUcastPkts;
	unsigned long ifInNUcastPkts;
	unsigned long ifInDiscards;
	unsigned long ifInErrors;
	unsigned long ifInUnknownProtos;
	unsigned long ifOutOctets;
	unsigned long ifOutUcastPkts;
	unsigned long ifOutNUcastPkts;
	unsigned long ifOutDiscards;
	unsigned long ifOutErrors;
	unsigned long ifOutQLen;
	unsigned long ifSpecific;
} SMib2Counter, *PSMib2Counter;

// Value in the ifType entry
#define WIRELESSLANIEEE80211b      6

// Value in the ifAdminStatus/ifOperStatus entry
#define UP                  1
#define DOWN                2
#define TESTING             3

//
// RMON counter
//
typedef struct tagSRmonCounter {
	long    etherStatsIndex;
	unsigned long etherStatsDataSource;
	unsigned long etherStatsDropEvents;
	unsigned long etherStatsOctets;
	unsigned long etherStatsPkts;
	unsigned long etherStatsBroadcastPkts;
	unsigned long etherStatsMulticastPkts;
	unsigned long etherStatsCRCAlignErrors;
	unsigned long etherStatsUndersizePkts;
	unsigned long etherStatsOversizePkts;
	unsigned long etherStatsFragments;
	unsigned long etherStatsJabbers;
	unsigned long etherStatsCollisions;
	unsigned long etherStatsPkt64Octets;
	unsigned long etherStatsPkt65to127Octets;
	unsigned long etherStatsPkt128to255Octets;
	unsigned long etherStatsPkt256to511Octets;
	unsigned long etherStatsPkt512to1023Octets;
	unsigned long etherStatsPkt1024to1518Octets;
	unsigned long etherStatsOwners;
	unsigned long etherStatsStatus;
} SRmonCounter, *PSRmonCounter;

//
// Custom counter
//
typedef struct tagSCustomCounters {
	unsigned long Length;

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
	unsigned long Length;

	unsigned long dwIsrTx0OK;
	unsigned long dwIsrAC0TxOK;
	unsigned long dwIsrBeaconTxOK;
	unsigned long dwIsrRx0OK;
	unsigned long dwIsrTBTTInt;
	unsigned long dwIsrSTIMERInt;
	unsigned long dwIsrWatchDog;
	unsigned long dwIsrUnrecoverableError;
	unsigned long dwIsrSoftInterrupt;
	unsigned long dwIsrMIBNearfull;
	unsigned long dwIsrRxNoBuf;

	unsigned long dwIsrUnknown;

	unsigned long dwIsrRx1OK;
	unsigned long dwIsrATIMTxOK;
	unsigned long dwIsrSYNCTxOK;
	unsigned long dwIsrCFPEnd;
	unsigned long dwIsrATIMEnd;
	unsigned long dwIsrSYNCFlushOK;
	unsigned long dwIsrSTIMER1Int;
} SISRCounters, *PSISRCounters;

// Value in the etherStatsStatus entry
#define VALID               1
#define CREATE_REQUEST      2
#define UNDER_CREATION      3
#define INVALID             4

//
// statistic counter
//
typedef struct tagSStatCounter {
	// RSR status count
	//
	unsigned long dwRsrFrmAlgnErr;
	unsigned long dwRsrErr;
	unsigned long dwRsrCRCErr;
	unsigned long dwRsrCRCOk;
	unsigned long dwRsrBSSIDOk;
	unsigned long dwRsrADDROk;
	unsigned long dwRsrBCNSSIDOk;
	unsigned long dwRsrLENErr;
	unsigned long dwRsrTYPErr;

	unsigned long dwNewRsrDECRYPTOK;
	unsigned long dwNewRsrCFP;
	unsigned long dwNewRsrUTSF;
	unsigned long dwNewRsrHITAID;
	unsigned long dwNewRsrHITAID0;

	unsigned long dwRsrLong;
	unsigned long dwRsrRunt;

	unsigned long dwRsrRxControl;
	unsigned long dwRsrRxData;
	unsigned long dwRsrRxManage;

	unsigned long dwRsrRxPacket;
	unsigned long dwRsrRxOctet;
	unsigned long dwRsrBroadcast;
	unsigned long dwRsrMulticast;
	unsigned long dwRsrDirected;
	// 64-bit OID
	unsigned long long   ullRsrOK;

	// for some optional OIDs (64 bits) and DMI support
	unsigned long long   ullRxBroadcastBytes;
	unsigned long long   ullRxMulticastBytes;
	unsigned long long   ullRxDirectedBytes;
	unsigned long long   ullRxBroadcastFrames;
	unsigned long long   ullRxMulticastFrames;
	unsigned long long   ullRxDirectedFrames;

	unsigned long dwRsrRxFragment;
	unsigned long dwRsrRxFrmLen64;
	unsigned long dwRsrRxFrmLen65_127;
	unsigned long dwRsrRxFrmLen128_255;
	unsigned long dwRsrRxFrmLen256_511;
	unsigned long dwRsrRxFrmLen512_1023;
	unsigned long dwRsrRxFrmLen1024_1518;

	// TSR status count
	//
	unsigned long dwTsrTotalRetry[TYPE_MAXTD];        // total collision retry count
	unsigned long dwTsrOnceRetry[TYPE_MAXTD];         // this packet only occur one collision
	unsigned long dwTsrMoreThanOnceRetry[TYPE_MAXTD]; // this packet occur more than one collision
	unsigned long dwTsrRetry[TYPE_MAXTD];             // this packet has ever occur collision,
	// that is (dwTsrOnceCollision0 + dwTsrMoreThanOnceCollision0)
	unsigned long dwTsrACKData[TYPE_MAXTD];
	unsigned long dwTsrErr[TYPE_MAXTD];
	unsigned long dwAllTsrOK[TYPE_MAXTD];
	unsigned long dwTsrRetryTimeout[TYPE_MAXTD];
	unsigned long dwTsrTransmitTimeout[TYPE_MAXTD];

	unsigned long dwTsrTxPacket[TYPE_MAXTD];
	unsigned long dwTsrTxOctet[TYPE_MAXTD];
	unsigned long dwTsrBroadcast[TYPE_MAXTD];
	unsigned long dwTsrMulticast[TYPE_MAXTD];
	unsigned long dwTsrDirected[TYPE_MAXTD];

	// RD/TD count
	unsigned long dwCntRxFrmLength;
	unsigned long dwCntTxBufLength;

	unsigned char abyCntRxPattern[16];
	unsigned char abyCntTxPattern[16];

	// Software check....
	unsigned long dwCntRxDataErr;             // rx buffer data software compare CRC err count
	unsigned long dwCntDecryptErr;            // rx buffer data software compare CRC err count
	unsigned long dwCntRxICVErr;              // rx buffer data software compare CRC err count
	unsigned int idxRxErrorDesc[TYPE_MAXRD]; // index for rx data error RD

	// 64-bit OID
	unsigned long long   ullTsrOK[TYPE_MAXTD];

	// for some optional OIDs (64 bits) and DMI support
	unsigned long long   ullTxBroadcastFrames[TYPE_MAXTD];
	unsigned long long   ullTxMulticastFrames[TYPE_MAXTD];
	unsigned long long   ullTxDirectedFrames[TYPE_MAXTD];
	unsigned long long   ullTxBroadcastBytes[TYPE_MAXTD];
	unsigned long long   ullTxMulticastBytes[TYPE_MAXTD];
	unsigned long long   ullTxDirectedBytes[TYPE_MAXTD];

	SISRCounters ISRStat;

	SCustomCounters CustomStat;

#ifdef Calcu_LinkQual
	//Tx count:
	unsigned long TxNoRetryOkCount;
	unsigned long TxRetryOkCount;
	unsigned long TxFailCount;
	//Rx count:
	unsigned long RxOkCnt;
	unsigned long RxFcsErrCnt;
	//statistic
	unsigned long SignalStren;
	unsigned long LinkQuality;
#endif
} SStatCounter, *PSStatCounter;

void STAvClearAllCounter(PSStatCounter pStatistic);

void STAvUpdateIsrStatCounter(PSStatCounter pStatistic, unsigned long dwIsr);

void STAvUpdateRDStatCounter(PSStatCounter pStatistic,
			     unsigned char byRSR, unsigned char byNewRSR, unsigned char byRxRate,
			     unsigned char *pbyBuffer, unsigned int cbFrameLength);

void STAvUpdateRDStatCounterEx(PSStatCounter pStatistic,
			       unsigned char byRSR, unsigned char byNewRsr, unsigned char byRxRate,
			       unsigned char *pbyBuffer, unsigned int cbFrameLength);

void STAvUpdateTDStatCounter(PSStatCounter pStatistic, unsigned char byTSR0, unsigned char byTSR1,
			     unsigned char *pbyBuffer, unsigned int cbFrameLength, unsigned int uIdx);

void STAvUpdateTDStatCounterEx(
	PSStatCounter   pStatistic,
	unsigned char *pbyBuffer,
	unsigned long cbFrameLength
);

void STAvUpdate802_11Counter(
	PSDot11Counters p802_11Counter,
	PSStatCounter   pStatistic,
	unsigned long dwCounter
);

void STAvClear802_11Counter(PSDot11Counters p802_11Counter);

#endif // __MIB_H__
