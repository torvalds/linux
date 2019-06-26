/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
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

#include "device.h"

#define PREAMBLE_LONG   0
#define PREAMBLE_SHORT  1

/*
 * Registers in the BASEBAND
 */
#define BB_MAX_CONTEXT_SIZE 256

#define C_SIFS_A      16      /* usec */
#define C_SIFS_BG     10

#define C_EIFS      80      /* usec */

#define C_SLOT_SHORT   9      /* usec */
#define C_SLOT_LONG   20

#define C_CWMIN_A     15       /* slot time */
#define C_CWMIN_B     31

#define C_CWMAX      1023     /* slot time */

/* 0:11A 1:11B 2:11G */
#define BB_TYPE_11A    0
#define BB_TYPE_11B    1
#define BB_TYPE_11G    2

/* 0:11a, 1:11b, 2:11gb (only CCK in BasicRate), 3:11ga (OFDM in BasicRate) */
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

/* Length, Service, and Signal fields of Phy for Tx */
struct vnt_phy_field {
	u8 signal;
	u8 service;
	__le16 len;
} __packed;

unsigned int vnt_get_frame_time(u8 preamble_type, u8 pkt_type,
				unsigned int frame_length, u16 tx_rate);

void vnt_get_phy_field(struct vnt_private *priv, u32 frame_length,
		       u16 tx_rate, u8 pkt_type, struct vnt_phy_field *phy);

void vnt_set_short_slot_time(struct vnt_private *priv);
void vnt_set_vga_gain_offset(struct vnt_private *priv, u8 data);
void vnt_set_antenna_mode(struct vnt_private *priv, u8 antenna_mode);
int vnt_vt3184_init(struct vnt_private *priv);
void vnt_set_deep_sleep(struct vnt_private *priv);
void vnt_exit_deep_sleep(struct vnt_private *priv);
void vnt_update_pre_ed_threshold(struct vnt_private *priv, int scanning);

#endif /* __BASEBAND_H__ */
