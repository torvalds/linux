/*******************************************************************************
  MAC 10/100 Header File

  Copyright (C) 2007-2009  STMicroelectronics Ltd

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#ifndef __DWMAC100_H__
#define __DWMAC100_H__

#include <linux/phy.h>
#include "common.h"

/*----------------------------------------------------------------------------
 *	 			MAC BLOCK defines
 *---------------------------------------------------------------------------*/
/* MAC CSR offset */
#define MAC_CONTROL	0x00000000	/* MAC Control */
#define MAC_ADDR_HIGH	0x00000004	/* MAC Address High */
#define MAC_ADDR_LOW	0x00000008	/* MAC Address Low */
#define MAC_HASH_HIGH	0x0000000c	/* Multicast Hash Table High */
#define MAC_HASH_LOW	0x00000010	/* Multicast Hash Table Low */
#define MAC_MII_ADDR	0x00000014	/* MII Address */
#define MAC_MII_DATA	0x00000018	/* MII Data */
#define MAC_FLOW_CTRL	0x0000001c	/* Flow Control */
#define MAC_VLAN1	0x00000020	/* VLAN1 Tag */
#define MAC_VLAN2	0x00000024	/* VLAN2 Tag */

/* MAC CTRL defines */
#define MAC_CONTROL_RA	0x80000000	/* Receive All Mode */
#define MAC_CONTROL_BLE	0x40000000	/* Endian Mode */
#define MAC_CONTROL_HBD	0x10000000	/* Heartbeat Disable */
#define MAC_CONTROL_PS	0x08000000	/* Port Select */
#define MAC_CONTROL_DRO	0x00800000	/* Disable Receive Own */
#define MAC_CONTROL_EXT_LOOPBACK 0x00400000	/* Reserved (ext loopback?) */
#define MAC_CONTROL_OM	0x00200000	/* Loopback Operating Mode */
#define MAC_CONTROL_F	0x00100000	/* Full Duplex Mode */
#define MAC_CONTROL_PM	0x00080000	/* Pass All Multicast */
#define MAC_CONTROL_PR	0x00040000	/* Promiscuous Mode */
#define MAC_CONTROL_IF	0x00020000	/* Inverse Filtering */
#define MAC_CONTROL_PB	0x00010000	/* Pass Bad Frames */
#define MAC_CONTROL_HO	0x00008000	/* Hash Only Filtering Mode */
#define MAC_CONTROL_HP	0x00002000	/* Hash/Perfect Filtering Mode */
#define MAC_CONTROL_LCC	0x00001000	/* Late Collision Control */
#define MAC_CONTROL_DBF	0x00000800	/* Disable Broadcast Frames */
#define MAC_CONTROL_DRTY	0x00000400	/* Disable Retry */
#define MAC_CONTROL_ASTP	0x00000100	/* Automatic Pad Stripping */
#define MAC_CONTROL_BOLMT_10	0x00000000	/* Back Off Limit 10 */
#define MAC_CONTROL_BOLMT_8	0x00000040	/* Back Off Limit 8 */
#define MAC_CONTROL_BOLMT_4	0x00000080	/* Back Off Limit 4 */
#define MAC_CONTROL_BOLMT_1	0x000000c0	/* Back Off Limit 1 */
#define MAC_CONTROL_DC		0x00000020	/* Deferral Check */
#define MAC_CONTROL_TE		0x00000008	/* Transmitter Enable */
#define MAC_CONTROL_RE		0x00000004	/* Receiver Enable */

#define MAC_CORE_INIT (MAC_CONTROL_HBD | MAC_CONTROL_ASTP)

/* MAC FLOW CTRL defines */
#define MAC_FLOW_CTRL_PT_MASK	0xffff0000	/* Pause Time Mask */
#define MAC_FLOW_CTRL_PT_SHIFT	16
#define MAC_FLOW_CTRL_PASS	0x00000004	/* Pass Control Frames */
#define MAC_FLOW_CTRL_ENABLE	0x00000002	/* Flow Control Enable */
#define MAC_FLOW_CTRL_PAUSE	0x00000001	/* Flow Control Busy ... */

/* MII ADDR  defines */
#define MAC_MII_ADDR_WRITE	0x00000002	/* MII Write */
#define MAC_MII_ADDR_BUSY	0x00000001	/* MII Busy */

/*----------------------------------------------------------------------------
 * 				DMA BLOCK defines
 *---------------------------------------------------------------------------*/

/* DMA Bus Mode register defines */
#define DMA_BUS_MODE_DBO	0x00100000	/* Descriptor Byte Ordering */
#define DMA_BUS_MODE_BLE	0x00000080	/* Big Endian/Little Endian */
#define DMA_BUS_MODE_PBL_MASK	0x00003f00	/* Programmable Burst Len */
#define DMA_BUS_MODE_PBL_SHIFT	8
#define DMA_BUS_MODE_DSL_MASK	0x0000007c	/* Descriptor Skip Length */
#define DMA_BUS_MODE_DSL_SHIFT	2	/*   (in DWORDS)      */
#define DMA_BUS_MODE_BAR_BUS	0x00000002	/* Bar-Bus Arbitration */
#define DMA_BUS_MODE_DEFAULT	0x00000000

/* DMA Control register defines */
#define DMA_CONTROL_SF		0x00200000	/* Store And Forward */

/* Transmit Threshold Control */
enum ttc_control {
	DMA_CONTROL_TTC_DEFAULT = 0x00000000,	/* Threshold is 32 DWORDS */
	DMA_CONTROL_TTC_64 = 0x00004000,	/* Threshold is 64 DWORDS */
	DMA_CONTROL_TTC_128 = 0x00008000,	/* Threshold is 128 DWORDS */
	DMA_CONTROL_TTC_256 = 0x0000c000,	/* Threshold is 256 DWORDS */
	DMA_CONTROL_TTC_18 = 0x00400000,	/* Threshold is 18 DWORDS */
	DMA_CONTROL_TTC_24 = 0x00404000,	/* Threshold is 24 DWORDS */
	DMA_CONTROL_TTC_32 = 0x00408000,	/* Threshold is 32 DWORDS */
	DMA_CONTROL_TTC_40 = 0x0040c000,	/* Threshold is 40 DWORDS */
	DMA_CONTROL_SE = 0x00000008,	/* Stop On Empty */
	DMA_CONTROL_OSF = 0x00000004,	/* Operate On 2nd Frame */
};

/* STMAC110 DMA Missed Frame Counter register defines */
#define DMA_MISSED_FRAME_OVE	0x10000000	/* FIFO Overflow Overflow */
#define DMA_MISSED_FRAME_OVE_CNTR 0x0ffe0000	/* Overflow Frame Counter */
#define DMA_MISSED_FRAME_OVE_M	0x00010000	/* Missed Frame Overflow */
#define DMA_MISSED_FRAME_M_CNTR	0x0000ffff	/* Missed Frame Couinter */

extern const struct stmmac_dma_ops dwmac100_dma_ops;

#endif /* __DWMAC100_H__ */
