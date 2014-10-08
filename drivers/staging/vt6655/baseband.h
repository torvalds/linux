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

/*
 * Registers in the BASEBAND
 */
#define BB_MAX_CONTEXT_SIZE 256

/*
 * Baseband RF pair definition in eeprom (Bits 6..0)
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

#define BBvClearFOE(dwIoBase)				\
	BBbWriteEmbedded(dwIoBase, 0xB1, 0)

#define BBvSetFOE(dwIoBase)				\
	BBbWriteEmbedded(dwIoBase, 0xB1, 0x0C)

unsigned int
BBuGetFrameTime(
	unsigned char byPreambleType,
	unsigned char byPktType,
	unsigned int cbFrameLength,
	unsigned short wRate
);

void vnt_get_phy_field(struct vnt_private *, u32 frame_length,
		       u16 tx_rate, u8 pkt_type, struct vnt_phy_field *);

bool BBbReadEmbedded(void __iomem *dwIoBase, unsigned char byBBAddr, unsigned char *pbyData);
bool BBbWriteEmbedded(void __iomem *dwIoBase, unsigned char byBBAddr, unsigned char byData);

void BBvReadAllRegs(void __iomem *dwIoBase, unsigned char *pbyBBRegs);
void BBvLoopbackOn(struct vnt_private *pDevice);
void BBvLoopbackOff(struct vnt_private *pDevice);
void BBvSetShortSlotTime(struct vnt_private *pDevice);
bool BBbIsRegBitsOn(void __iomem *dwIoBase, unsigned char byBBAddr, unsigned char byTestBits);
bool BBbIsRegBitsOff(void __iomem *dwIoBase, unsigned char byBBAddr, unsigned char byTestBits);
void BBvSetVGAGainOffset(struct vnt_private *pDevice, unsigned char byData);

/* VT3253 Baseband */
bool BBbVT3253Init(struct vnt_private *pDevice);
void BBvSoftwareReset(void __iomem *dwIoBase);
void BBvPowerSaveModeON(void __iomem *dwIoBase);
void BBvPowerSaveModeOFF(void __iomem *dwIoBase);
void BBvSetTxAntennaMode(void __iomem *dwIoBase, unsigned char byAntennaMode);
void BBvSetRxAntennaMode(void __iomem *dwIoBase, unsigned char byAntennaMode);
void BBvSetDeepSleep(void __iomem *dwIoBase, unsigned char byLocalID);
void BBvExitDeepSleep(void __iomem *dwIoBase, unsigned char byLocalID);

/* timer for antenna diversity */

void
TimerSQ3CallBack(
	void *hDeviceContext
);

void
TimerState1CallBack(
	void *hDeviceContext
);

void BBvAntennaDiversity(struct vnt_private *pDevice,
			 unsigned char byRxRate, unsigned char bySQ3);
void
BBvClearAntDivSQ3Value(struct vnt_private *pDevice);

#endif /* __BASEBAND_H__ */
