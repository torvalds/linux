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

#include "desc.h"

//
// 802.11 counter
//

typedef struct tagSDot11Counters {
	unsigned long long   RTSSuccessCount;
	unsigned long long   RTSFailureCount;
	unsigned long long   ACKFailureCount;
	unsigned long long   FCSErrorCount;
} SDot11Counters, *PSDot11Counters;

//
// Custom counter
//
typedef struct tagSISRCounters {
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
	unsigned long dwIsrSTIMER1Int;
} SISRCounters, *PSISRCounters;

//
// statistic counter
//
typedef struct tagSStatCounter {
	SISRCounters ISRStat;
} SStatCounter, *PSStatCounter;

void STAvUpdateIsrStatCounter(PSStatCounter pStatistic, unsigned long dwIsr);

void STAvUpdate802_11Counter(
	PSDot11Counters p802_11Counter,
	PSStatCounter   pStatistic,
	unsigned long dwCounter
);

#endif // __MIB_H__
