/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
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

/* Baseband RF pair definition in eeprom (Bits 6..0) */
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
#define RF_VT3226D0         0x0c /* RobertYu:20051114 */
#define RF_VT3342A0         0x0d /* RobertYu:20060609 */
#define RF_AL2230S          0x0e

#define RF_EMU              0x80
#define RF_MASK             0x7F

#define VNT_RF_MAX_POWER    0x3f
#define	VNT_RF_REG_LEN      0x17 /* 24 bit length */

int vnt_rf_write_embedded(struct vnt_private *priv, u32 data);
int vnt_rf_setpower(struct vnt_private *priv, u32 rate, u32 channel);
int vnt_rf_set_txpower(struct vnt_private *priv, u8 power, u32 rate);
void vnt_rf_rssi_to_dbm(struct vnt_private *priv, u8 rssi, long *dbm);
void vnt_rf_table_download(struct vnt_private *priv);

#endif /* __RF_H__ */
