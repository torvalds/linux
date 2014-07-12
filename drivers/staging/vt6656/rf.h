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

#include "device.h"

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

#define VNT_RF_MAX_POWER    0x3f

int vnt_rf_write_embedded(struct vnt_private *, u32);
int vnt_rf_setpower(struct vnt_private *, u32, u32);
int vnt_rf_set_txpower(struct vnt_private *, u8, u32);
void vnt_rf_rssi_to_dbm(struct vnt_private *, u8, long *);
void vnt_rf_table_download(struct vnt_private *);

#endif /* __RF_H__ */
