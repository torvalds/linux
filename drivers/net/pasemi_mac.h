/*
 * Copyright (C) 2006 PA Semi, Inc
 *
 * Driver for the PA6T-1682M onchip 1G/10G Ethernet MACs, soft state and
 * hardware register layouts.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef PASEMI_MAC_H
#define PASEMI_MAC_H

#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/phy.h>

#define MAX_LRO_DESCRIPTORS 8

struct pasemi_mac_txring {
	struct pasemi_dmachan chan; /* Must be first */
	spinlock_t	 lock;
	unsigned int	 size;
	unsigned int	 next_to_fill;
	unsigned int	 next_to_clean;
	struct pasemi_mac_buffer *ring_info;
	struct pasemi_mac *mac;	/* Needed in intr handler */
	struct timer_list clean_timer;
};

struct pasemi_mac_rxring {
	struct pasemi_dmachan chan; /* Must be first */
	spinlock_t	 lock;
	u64		*buffers;	/* RX interface buffer ring */
	dma_addr_t	 buf_dma;
	unsigned int	 size;
	unsigned int	 next_to_fill;
	unsigned int	 next_to_clean;
	struct pasemi_mac_buffer *ring_info;
	struct pasemi_mac *mac;	/* Needed in intr handler */
};

struct pasemi_mac {
	struct net_device *netdev;
	struct pci_dev *pdev;
	struct pci_dev *dma_pdev;
	struct pci_dev *iob_pdev;
	struct phy_device *phydev;
	struct napi_struct napi;

	u8		type;
#define MAC_TYPE_GMAC	1
#define MAC_TYPE_XAUI	2
	u32	dma_if;

	u8		mac_addr[6];

	struct net_lro_mgr	lro_mgr;
	struct net_lro_desc	lro_desc[MAX_LRO_DESCRIPTORS];
	struct timer_list	rxtimer;
	unsigned int		lro_max_aggr;

	struct pasemi_mac_txring *tx;
	struct pasemi_mac_rxring *rx;
	char		tx_irq_name[10];		/* "eth%d tx" */
	char		rx_irq_name[10];		/* "eth%d rx" */
	int	link;
	int	speed;
	int	duplex;

	unsigned int	msg_enable;
	char	phy_id[BUS_ID_SIZE];
};

/* Software status descriptor (ring_info) */
struct pasemi_mac_buffer {
	struct sk_buff *skb;
	dma_addr_t	dma;
};


/* PCI register offsets and formats */


/* MAC CFG register offsets */
enum {
	PAS_MAC_CFG_PCFG = 0x80,
	PAS_MAC_CFG_TXP = 0x98,
	PAS_MAC_IPC_CHNL = 0x208,
};

/* MAC CFG register fields */
#define PAS_MAC_CFG_PCFG_PE		0x80000000
#define PAS_MAC_CFG_PCFG_CE		0x40000000
#define PAS_MAC_CFG_PCFG_BU		0x20000000
#define PAS_MAC_CFG_PCFG_TT		0x10000000
#define PAS_MAC_CFG_PCFG_TSR_M		0x0c000000
#define PAS_MAC_CFG_PCFG_TSR_10M	0x00000000
#define PAS_MAC_CFG_PCFG_TSR_100M	0x04000000
#define PAS_MAC_CFG_PCFG_TSR_1G		0x08000000
#define PAS_MAC_CFG_PCFG_TSR_10G	0x0c000000
#define PAS_MAC_CFG_PCFG_T24		0x02000000
#define PAS_MAC_CFG_PCFG_PR		0x01000000
#define PAS_MAC_CFG_PCFG_CRO_M		0x00ff0000
#define PAS_MAC_CFG_PCFG_CRO_S	16
#define PAS_MAC_CFG_PCFG_IPO_M		0x0000ff00
#define PAS_MAC_CFG_PCFG_IPO_S	8
#define PAS_MAC_CFG_PCFG_S1		0x00000080
#define PAS_MAC_CFG_PCFG_IO_M		0x00000060
#define PAS_MAC_CFG_PCFG_IO_MAC		0x00000000
#define PAS_MAC_CFG_PCFG_IO_OFF		0x00000020
#define PAS_MAC_CFG_PCFG_IO_IND_ETH	0x00000040
#define PAS_MAC_CFG_PCFG_IO_IND_IP	0x00000060
#define PAS_MAC_CFG_PCFG_LP		0x00000010
#define PAS_MAC_CFG_PCFG_TS		0x00000008
#define PAS_MAC_CFG_PCFG_HD		0x00000004
#define PAS_MAC_CFG_PCFG_SPD_M		0x00000003
#define PAS_MAC_CFG_PCFG_SPD_10M	0x00000000
#define PAS_MAC_CFG_PCFG_SPD_100M	0x00000001
#define PAS_MAC_CFG_PCFG_SPD_1G		0x00000002
#define PAS_MAC_CFG_PCFG_SPD_10G	0x00000003
#define PAS_MAC_CFG_TXP_FCF		0x01000000
#define PAS_MAC_CFG_TXP_FCE		0x00800000
#define PAS_MAC_CFG_TXP_FC		0x00400000
#define PAS_MAC_CFG_TXP_FPC_M		0x00300000
#define PAS_MAC_CFG_TXP_FPC_S		20
#define PAS_MAC_CFG_TXP_FPC(x)		(((x) << PAS_MAC_CFG_TXP_FPC_S) & \
					 PAS_MAC_CFG_TXP_FPC_M)
#define PAS_MAC_CFG_TXP_RT		0x00080000
#define PAS_MAC_CFG_TXP_BL		0x00040000
#define PAS_MAC_CFG_TXP_SL_M		0x00030000
#define PAS_MAC_CFG_TXP_SL_S		16
#define PAS_MAC_CFG_TXP_SL(x)		(((x) << PAS_MAC_CFG_TXP_SL_S) & \
					 PAS_MAC_CFG_TXP_SL_M)
#define PAS_MAC_CFG_TXP_COB_M		0x0000f000
#define PAS_MAC_CFG_TXP_COB_S		12
#define PAS_MAC_CFG_TXP_COB(x)		(((x) << PAS_MAC_CFG_TXP_COB_S) & \
					 PAS_MAC_CFG_TXP_COB_M)
#define PAS_MAC_CFG_TXP_TIFT_M		0x00000f00
#define PAS_MAC_CFG_TXP_TIFT_S		8
#define PAS_MAC_CFG_TXP_TIFT(x)		(((x) << PAS_MAC_CFG_TXP_TIFT_S) & \
					 PAS_MAC_CFG_TXP_TIFT_M)
#define PAS_MAC_CFG_TXP_TIFG_M		0x000000ff
#define PAS_MAC_CFG_TXP_TIFG_S		0
#define PAS_MAC_CFG_TXP_TIFG(x)		(((x) << PAS_MAC_CFG_TXP_TIFG_S) & \
					 PAS_MAC_CFG_TXP_TIFG_M)

#define PAS_MAC_IPC_CHNL_DCHNO_M	0x003f0000
#define PAS_MAC_IPC_CHNL_DCHNO_S	16
#define PAS_MAC_IPC_CHNL_DCHNO(x)	(((x) << PAS_MAC_IPC_CHNL_DCHNO_S) & \
					 PAS_MAC_IPC_CHNL_DCHNO_M)
#define PAS_MAC_IPC_CHNL_BCH_M		0x0000003f
#define PAS_MAC_IPC_CHNL_BCH_S		0
#define PAS_MAC_IPC_CHNL_BCH(x)		(((x) << PAS_MAC_IPC_CHNL_BCH_S) & \
					 PAS_MAC_IPC_CHNL_BCH_M)

#endif /* PASEMI_MAC_H */
