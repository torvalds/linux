/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
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

#include <linux/bits.h>
#include <linux/types.h>
#include <linux/mm.h>

/* max transmit or receive buffer size */
#define CB_MAX_BUF_SIZE     2900U       /* NOTE: must be multiple of 4 */

#define MAX_TOTAL_SIZE_WITH_ALL_HEADERS CB_MAX_BUF_SIZE

#define MAX_INTERRUPT_SIZE              32

#define CB_MAX_RX_DESC      128         /* max # of descriptors */
#define CB_MIN_RX_DESC      16          /* min # of RX descriptors */
#define CB_MAX_TX_DESC      128         /* max # of descriptors */
#define CB_MIN_TX_DESC      16          /* min # of TX descriptors */

/*
 * bits in the RSR register
 */
#define RSR_ADDRBROAD       BIT(7)
#define RSR_ADDRMULTI       BIT(6)
#define RSR_ADDRUNI         0x00
#define RSR_IVLDTYP         BIT(5)	/* invalid packet type */
#define RSR_IVLDLEN         BIT(4)	/* invalid len (> 2312 byte) */
#define RSR_BSSIDOK         BIT(3)
#define RSR_CRCOK           BIT(2)
#define RSR_BCNSSIDOK       BIT(1)
#define RSR_ADDROK          BIT(0)

/*
 * bits in the new RSR register
 */
#define NEWRSR_DECRYPTOK    BIT(4)
#define NEWRSR_CFPIND       BIT(3)
#define NEWRSR_HWUTSF       BIT(2)
#define NEWRSR_BCNHITAID    BIT(1)
#define NEWRSR_BCNHITAID0   BIT(0)

/*
 * bits in the TSR register
 */
#define TSR_RETRYTMO        BIT(3)
#define TSR_TMO             BIT(2)
#define TSR_ACKDATA         BIT(1)
#define TSR_VALID           BIT(0)

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

#endif /* __DESC_H__ */
