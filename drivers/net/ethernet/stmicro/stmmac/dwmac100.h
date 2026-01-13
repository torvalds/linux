/* SPDX-License-Identifier: GPL-2.0-only */
/*******************************************************************************
  MAC 10/100 Header File

  Copyright (C) 2007-2009  STMicroelectronics Ltd


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
#define MAC_CONTROL_HBD	0x10000000	/* Heartbeat Disable */
#define MAC_CONTROL_PS	0x08000000	/* Port Select */
#define MAC_CONTROL_OM	0x00200000	/* Loopback Operating Mode */
#define MAC_CONTROL_F	0x00100000	/* Full Duplex Mode */
#define MAC_CONTROL_PM	0x00080000	/* Pass All Multicast */
#define MAC_CONTROL_PR	0x00040000	/* Promiscuous Mode */
#define MAC_CONTROL_IF	0x00020000	/* Inverse Filtering */
#define MAC_CONTROL_HO	0x00008000	/* Hash Only Filtering Mode */
#define MAC_CONTROL_HP	0x00002000	/* Hash/Perfect Filtering Mode */

#define MAC_CORE_INIT (MAC_CONTROL_HBD)

/* MAC FLOW CTRL defines */
#define MAC_FLOW_CTRL_PT_MASK	GENMASK(31, 16)	/* Pause Time Mask */
#define MAC_FLOW_CTRL_ENABLE	0x00000002	/* Flow Control Enable */

/*----------------------------------------------------------------------------
 * 				DMA BLOCK defines
 *---------------------------------------------------------------------------*/

/* DMA Bus Mode register defines */
#define DMA_BUS_MODE_PBL_MASK	GENMASK(13, 8)	/* Programmable Burst Len */
#define DMA_BUS_MODE_DEFAULT	0x00000000

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
