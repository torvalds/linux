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
 */

#ifndef __BASEBAND_H__
#define __BASEBAND_H__

#include "ttype.h"
#include "tether.h"
#include "device.h"

/*---------------------  Export Definitions -------------------------*/

//
// Registers in the BASEBAND
//
#define BB_MAX_CONTEXT_SIZE 256


//
// Baseband RF pair definition in eeprom (Bits 6..0)
//

/*
#define RATE_1M         0
#define RATE_2M         1
#define RATE_5M         2
#define RATE_11M        3
#define RATE_6M         4
#define RATE_9M         5
#define RATE_12M        6
#define RATE_18M        7
#define RATE_24M        8
#define RATE_36M        9
#define RATE_48M       10
#define RATE_54M       11
#define RATE_AUTO      12
#define MAX_RATE       12


//0:11A 1:11B 2:11G
#define BB_TYPE_11A    0
#define BB_TYPE_11B    1
#define BB_TYPE_11G    2

//0:11a,1:11b,2:11gb(only CCK in BasicRate),3:11ga(OFDM in Basic Rate)
#define PK_TYPE_11A     0
#define PK_TYPE_11B     1
#define PK_TYPE_11GB    2
#define PK_TYPE_11GA    3
*/


#define PREAMBLE_LONG   0
#define PREAMBLE_SHORT  1


#define F5G             0
#define F2_4G           1

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

#define BBvClearFOE(dwIoBase)                               \
{                                                           \
    BBbWriteEmbeded(dwIoBase, 0xB1, 0);                     \
}

#define BBvSetFOE(dwIoBase)                                 \
{                                                           \
    BBbWriteEmbeded(dwIoBase, 0xB1, 0x0C);                  \
}


/*---------------------  Export Classes  ----------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

unsigned int
BBuGetFrameTime(
    unsigned char byPreambleType,
    unsigned char byPktType,
    unsigned int cbFrameLength,
    unsigned short wRate
    );

void
BBvCaculateParameter (
    PSDevice pDevice,
    unsigned int cbFrameLength,
    unsigned short wRate,
    unsigned char byPacketType,
    unsigned short *pwPhyLen,
    unsigned char *pbyPhySrv,
    unsigned char *pbyPhySgn
    );

bool BBbReadEmbeded(unsigned long dwIoBase, unsigned char byBBAddr, unsigned char *pbyData);
bool BBbWriteEmbeded(unsigned long dwIoBase, unsigned char byBBAddr, unsigned char byData);

void BBvReadAllRegs(unsigned long dwIoBase, unsigned char *pbyBBRegs);
void BBvLoopbackOn(PSDevice pDevice);
void BBvLoopbackOff(PSDevice pDevice);
void BBvSetShortSlotTime(PSDevice pDevice);
bool BBbIsRegBitsOn(unsigned long dwIoBase, unsigned char byBBAddr, unsigned char byTestBits);
bool BBbIsRegBitsOff(unsigned long dwIoBase, unsigned char byBBAddr, unsigned char byTestBits);
void BBvSetVGAGainOffset(PSDevice pDevice, unsigned char byData);

// VT3253 Baseband
bool BBbVT3253Init(PSDevice pDevice);
void BBvSoftwareReset(unsigned long dwIoBase);
void BBvPowerSaveModeON(unsigned long dwIoBase);
void BBvPowerSaveModeOFF(unsigned long dwIoBase);
void BBvSetTxAntennaMode(unsigned long dwIoBase, unsigned char byAntennaMode);
void BBvSetRxAntennaMode(unsigned long dwIoBase, unsigned char byAntennaMode);
void BBvSetDeepSleep(unsigned long dwIoBase, unsigned char byLocalID);
void BBvExitDeepSleep(unsigned long dwIoBase, unsigned char byLocalID);

// timer for antenna diversity

void
TimerSQ3CallBack (
    void *hDeviceContext
    );

void
TimerState1CallBack(
    void *hDeviceContext
    );

void BBvAntennaDiversity(PSDevice pDevice, unsigned char byRxRate, unsigned char bySQ3);
void
BBvClearAntDivSQ3Value (PSDevice pDevice);

#endif // __BASEBAND_H__
