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
 * File: rf.h
 *
 * Purpose:
 *
 * Author: Jerry Chen
 *
 * Date: Feb. 19, 2004
 *
 */

#ifndef __RF_H__
#define __RF_H__

#include "ttype.h"
#include "device.h"

/*---------------------  Export Definitions -------------------------*/
//
// Baseband RF pair definition in eeprom (Bits 6..0)
//
#define RF_RFMD2959         0x01
#define RF_MAXIMAG          0x02
#define RF_AL2230           0x03
#define RF_GCT5103          0x04
#define RF_UW2451           0x05
#define RF_MAXIMG           0x06
#define RF_MAXIM2829        0x07
#define RF_UW2452           0x08
#define RF_VT3226           0x09
#define RF_AIROHA7230       0x0a
#define RF_UW2453           0x0b
#define RF_VT3226D0         0x0c //RobertYu:20051114
#define RF_VT3342A0         0x0d //RobertYu:20060609
#define RF_AL2230S          0x0e

#define RF_EMU              0x80
#define RF_MASK             0x7F



/*---------------------  Export Classes  ----------------------------*/

/*---------------------  Export Variables  --------------------------*/
extern const BYTE RFaby11aChannelIndex[200];
/*---------------------  Export Functions  --------------------------*/

BOOL IFRFbWriteEmbeded(PSDevice pDevice, DWORD dwData);
BOOL RFbSetPower (
    IN  PSDevice  pDevice,
    IN  UINT      uRATE,
    IN  UINT      uCH
    );

BOOL RFbRawSetPower(
    IN  PSDevice  pDevice,
    IN  BYTE      byPwr,
    IN  UINT      uRATE
    );

VOID
RFvRSSITodBm (
    IN  PSDevice pDevice,
    IN  BYTE     byCurrRSSI,
    long *    pldBm
    );

VOID
RFbRFTableDownload (
    IN  PSDevice pDevice
    );

BOOL s_bVT3226D0_11bLoCurrentAdjust(
    IN  PSDevice    pDevice,
    IN  BYTE        byChannel,
    IN  BOOL        b11bMode
    );

#endif // __RF_H__



