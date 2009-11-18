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
 * File: baseband.h
 *
 * Purpose: Implement functions to access baseband
 *
 * Author: Jerry Chen
 *
 * Date: Jun. 5, 2002
 *
 * Revision History:
 *      06-10-2003 Bryan YC Fan:  Re-write codes to support VT3253 spec.
 *      08-26-2003 Kyle Hsu    :  Add defines of packet type and TX rate.
 */

#ifndef __BASEBAND_H__
#define __BASEBAND_H__

#include "ttype.h"
#include "tether.h"
#include "device.h"

/*---------------------  Export Definitions -------------------------*/

#define PREAMBLE_LONG   0
#define PREAMBLE_SHORT  1

//
// Registers in the BASEBAND
//
#define BB_MAX_CONTEXT_SIZE 256

#define C_SIFS_A      16      // micro sec.
#define C_SIFS_BG     10

#define C_EIFS      80      // micro sec.


#define C_SLOT_SHORT   9      // micro sec.
#define C_SLOT_LONG   20

#define C_CWMIN_A     15       // slot time
#define C_CWMIN_B     31

#define C_CWMAX      1023     // slot time

//0:11A 1:11B 2:11G
#define BB_TYPE_11A    0
#define BB_TYPE_11B    1
#define BB_TYPE_11G    2

//0:11a,1:11b,2:11gb(only CCK in BasicRate),3:11ga(OFDM in Basic Rate)
#define PK_TYPE_11A     0
#define PK_TYPE_11B     1
#define PK_TYPE_11GB    2
#define PK_TYPE_11GA    3

#define TOP_RATE_54M        0x80000000
#define TOP_RATE_48M        0x40000000
#define TOP_RATE_36M        0x20000000
#define TOP_RATE_24M        0x10000000
#define TOP_RATE_18M        0x08000000
#define TOP_RATE_12M        0x04000000
#define TOP_RATE_11M        0x02000000
#define TOP_RATE_9M         0x01000000
#define TOP_RATE_6M         0x00800000
#define TOP_RATE_55M        0x00400000
#define TOP_RATE_2M         0x00200000
#define TOP_RATE_1M         0x00100000


/*---------------------  Export Types  ------------------------------*/

/*---------------------  Export Macros ------------------------------*/

/*---------------------  Export Classes  ----------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

UINT
BBuGetFrameTime(
    IN BYTE byPreambleType,
    IN BYTE byFreqType,
    IN UINT cbFrameLength,
    IN WORD wRate
    );

VOID
BBvCaculateParameter (
    IN  PSDevice pDevice,
    IN  UINT cbFrameLength,
    IN  WORD wRate,
    IN  BYTE byPacketType,
    OUT PWORD pwPhyLen,
    OUT PBYTE pbyPhySrv,
    OUT PBYTE pbyPhySgn
    );

// timer for antenna diversity

VOID
TimerSQ3CallBack (
    IN  HANDLE      hDeviceContext
    );

VOID
TimerSQ3Tmax3CallBack (
    IN  HANDLE      hDeviceContext
    );

VOID BBvAntennaDiversity (PSDevice pDevice, BYTE byRxRate, BYTE bySQ3);
void BBvLoopbackOn (PSDevice pDevice);
void BBvLoopbackOff (PSDevice pDevice);
void BBvSoftwareReset (PSDevice pDevice);

void BBvSetShortSlotTime(PSDevice pDevice);
VOID BBvSetVGAGainOffset(PSDevice pDevice, BYTE byData);
void BBvSetAntennaMode(PSDevice pDevice, BYTE byAntennaMode);
BOOL BBbVT3184Init (PSDevice pDevice);
VOID BBvSetDeepSleep (PSDevice pDevice);
VOID BBvExitDeepSleep (PSDevice pDevice);
VOID BBvUpdatePreEDThreshold(
     IN  PSDevice    pDevice,
     IN  BOOL        bScanning
     );

#endif // __BASEBAND_H__
