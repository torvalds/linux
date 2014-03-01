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
 * File: desc.h
 *
 * Purpose:The header file of descriptor
 *
 * Revision History:
 *
 * Author: Tevin Chen
 *
 * Date: May 21, 1996
 *
 */

#ifndef __DESC_H__
#define __DESC_H__

#include <linux/types.h>
#include <linux/mm.h>

#include "tether.h"

/* max transmit or receive buffer size */
#define CB_MAX_BUF_SIZE     2900U       /* NOTE: must be multiple of 4 */

/* max TX buffer size */
#define CB_MAX_TX_BUF_SIZE        CB_MAX_BUF_SIZE
/* max RX buffer size when not use Multi-RD */
#define CB_MAX_RX_BUF_SIZE_NORMAL CB_MAX_BUF_SIZE

#define CB_BEACON_BUF_SIZE  512U        /* default beacon buffer size */

#define MAX_TOTAL_SIZE_WITH_ALL_HEADERS CB_MAX_BUF_SIZE

#define MAX_INTERRUPT_SIZE              32

#define RX_BLOCKS           64          /* from 0x60 to 0xA0 */
#define TX_BLOCKS           32          /* from 0xA0 to 0xC0 */

#define CB_MAX_RX_DESC      128         /* max # of descriptors */
#define CB_MIN_RX_DESC      16          /* min # of RX descriptors */
#define CB_MAX_TX_DESC      128         /* max # of descriptors */
#define CB_MIN_TX_DESC      16          /* min # of TX descriptors */

#define CB_RD_NUM           64          /* default # of RD */
#define CB_TD_NUM           64          /* default # of TD */

/*
 * bits in the RSR register
 */
#define RSR_ADDRBROAD       0x80
#define RSR_ADDRMULTI       0x40
#define RSR_ADDRUNI         0x00
#define RSR_IVLDTYP         0x20        /* invalid packet type */
#define RSR_IVLDLEN         0x10        /* invalid len (> 2312 byte) */
#define RSR_BSSIDOK         0x08
#define RSR_CRCOK           0x04
#define RSR_BCNSSIDOK       0x02
#define RSR_ADDROK          0x01

/*
 * bits in the new RSR register
 */
#define NEWRSR_DECRYPTOK    0x10
#define NEWRSR_CFPIND       0x08
#define NEWRSR_HWUTSF       0x04
#define NEWRSR_BCNHITAID    0x02
#define NEWRSR_BCNHITAID0   0x01

/*
 * bits in the TSR register
 */
#define TSR_RETRYTMO        0x08
#define TSR_TMO             0x04
#define TSR_ACKDATA         0x02
#define TSR_VALID           0x01

#define CB_PROTOCOL_RESERVED_SECTION    16

/*
 * if retries exceed 15 times, TX will abort, and
 * if TX fifo underflow, TX will fail
 * we should try to resend it
 */
#define CB_MAX_TX_ABORT_RETRY   3

#define FIFOCTL_AUTO_FB_1   0x1000
#define FIFOCTL_AUTO_FB_0   0x0800
#define FIFOCTL_GRPACK      0x0400
#define FIFOCTL_11GA        0x0300
#define FIFOCTL_11GB        0x0200
#define FIFOCTL_11B         0x0100
#define FIFOCTL_11A         0x0000
#define FIFOCTL_RTS         0x0080
#define FIFOCTL_ISDMA0      0x0040
#define FIFOCTL_GENINT      0x0020
#define FIFOCTL_TMOEN       0x0010
#define FIFOCTL_LRETRY      0x0008
#define FIFOCTL_CRCDIS      0x0004
#define FIFOCTL_NEEDACK     0x0002
#define FIFOCTL_LHEAD       0x0001

/* WMAC definition Frag Control */
#define FRAGCTL_AES         0x0300
#define FRAGCTL_TKIP        0x0200
#define FRAGCTL_LEGACY      0x0100
#define FRAGCTL_NONENCRYPT  0x0000
#define FRAGCTL_ENDFRAG     0x0003
#define FRAGCTL_MIDFRAG     0x0002
#define FRAGCTL_STAFRAG     0x0001
#define FRAGCTL_NONFRAG     0x0000

#define TYPE_TXDMA0     0
#define TYPE_AC0DMA     1
#define TYPE_ATIMDMA    2
#define TYPE_SYNCDMA    3
#define TYPE_MAXTD      2

#define TYPE_BEACONDMA  4

#define TYPE_RXDMA0     0
#define TYPE_RXDMA1     1
#define TYPE_MAXRD      2

/* TD_INFO flags control bit */
#define TD_FLAGS_NETIF_SKB 0x01 /* check if need release skb */
#define TD_FLAGS_PRIV_SKB  0x02 /* check if called from private skb(hostap) */
#define TD_FLAGS_PS_RETRY  0x04 /* check if PS STA frame re-transmit */

/*
 * TX FIFO header
 */
typedef struct tagSBEACONCtl {
	u32 BufReady:1;
	u32 TSF:15;
	u32 BufLen:11;
	u32 Reserved:5;
} __attribute__ ((__packed__))
SBEACONCtl;

typedef struct tagSSecretKey {
	u32 dwLowDword;
    u8    byHighByte;
} __attribute__ ((__packed__))
SSecretKey;

typedef struct tagSKeyEntry {
    u8  abyAddrHi[2];
    u16  wKCTL;
    u8  abyAddrLo[4];
	u32 dwKey0[4];
	u32 dwKey1[4];
	u32 dwKey2[4];
	u32 dwKey3[4];
	u32 dwKey4[4];
} __attribute__ ((__packed__))
SKeyEntry;

#endif /* __DESC_H__ */
