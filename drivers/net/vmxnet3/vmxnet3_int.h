/*
 * Linux driver for VMware's vmxnet3 ethernet NIC.
 *
 * Copyright (C) 2008-2009, VMware, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Maintained by: Shreyas Bhatewara <pv-drivers@vmware.com>
 *
 */

#ifndef _VMXNET3_INT_H
#define _VMXNET3_INT_H

#include <linux/bitops.h>
#include <linux/ethtool.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/compiler.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/ioport.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/skbuff.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/uaccess.h>
#include <asm/dma.h>
#include <asm/page.h>

#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/in.h>
#include <linux/etherdevice.h>
#include <asm/checksum.h>
#include <linux/if_vlan.h>
#include <linux/if_arp.h>
#include <linux/inetdevice.h>
#include <linux/log2.h>

#include "vmxnet3_defs.h"

#ifdef DEBUG
# define VMXNET3_DRIVER_VERSION_REPORT VMXNET3_DRIVER_VERSION_STRING"-NAPI(debug)"
#else
# define VMXNET3_DRIVER_VERSION_REPORT VMXNET3_DRIVER_VERSION_STRING"-NAPI"
#endif


/*
 * Version numbers
 */
#define VMXNET3_DRIVER_VERSION_STRING   "1.1.18.0-k"

/* a 32-bit int, each byte encode a verion number in VMXNET3_DRIVER_VERSION */
#define VMXNET3_DRIVER_VERSION_NUM      0x01011200

#if defined(CONFIG_PCI_MSI)
	/* RSS only makes sense if MSI-X is supported. */
	#define VMXNET3_RSS
#endif

/*
 * Capabilities
 */

enum {
	VMNET_CAP_SG	        = 0x0001, /* Can do scatter-gather transmits. */
	VMNET_CAP_IP4_CSUM      = 0x0002, /* Can checksum only TCP/UDP over
					   * IPv4 */
	VMNET_CAP_HW_CSUM       = 0x0004, /* Can checksum all packets. */
	VMNET_CAP_HIGH_DMA      = 0x0008, /* Can DMA to high memory. */
	VMNET_CAP_TOE	        = 0x0010, /* Supports TCP/IP offload. */
	VMNET_CAP_TSO	        = 0x0020, /* Supports TCP Segmentation
					   * offload */
	VMNET_CAP_SW_TSO        = 0x0040, /* Supports SW TCP Segmentation */
	VMNET_CAP_VMXNET_APROM  = 0x0080, /* Vmxnet APROM support */
	VMNET_CAP_HW_TX_VLAN    = 0x0100, /* Can we do VLAN tagging in HW */
	VMNET_CAP_HW_RX_VLAN    = 0x0200, /* Can we do VLAN untagging in HW */
	VMNET_CAP_SW_VLAN       = 0x0400, /* VLAN tagging/untagging in SW */
	VMNET_CAP_WAKE_PCKT_RCV = 0x0800, /* Can wake on network packet recv? */
	VMNET_CAP_ENABLE_INT_INLINE = 0x1000,  /* Enable Interrupt Inline */
	VMNET_CAP_ENABLE_HEADER_COPY = 0x2000,  /* copy header for vmkernel */
	VMNET_CAP_TX_CHAIN      = 0x4000, /* Guest can use multiple tx entries
					  * for a pkt */
	VMNET_CAP_RX_CHAIN      = 0x8000, /* pkt can span multiple rx entries */
	VMNET_CAP_LPD           = 0x10000, /* large pkt delivery */
	VMNET_CAP_BPF           = 0x20000, /* BPF Support in VMXNET Virtual HW*/
	VMNET_CAP_SG_SPAN_PAGES = 0x40000, /* Scatter-gather can span multiple*/
					   /* pages transmits */
	VMNET_CAP_IP6_CSUM      = 0x80000, /* Can do IPv6 csum offload. */
	VMNET_CAP_TSO6         = 0x100000, /* TSO seg. offload for IPv6 pkts. */
	VMNET_CAP_TSO256k      = 0x200000, /* Can do TSO seg offload for */
					   /* pkts up to 256kB. */
	VMNET_CAP_UPT          = 0x400000  /* Support UPT */
};

/*
 * PCI vendor and device IDs.
 */
#define PCI_VENDOR_ID_VMWARE            0x15AD
#define PCI_DEVICE_ID_VMWARE_VMXNET3    0x07B0
#define MAX_ETHERNET_CARDS		10
#define MAX_PCI_PASSTHRU_DEVICE		6

struct vmxnet3_cmd_ring {
	union Vmxnet3_GenericDesc *base;
	u32		size;
	u32		next2fill;
	u32		next2comp;
	u8		gen;
	dma_addr_t	basePA;
};

static inline void
vmxnet3_cmd_ring_adv_next2fill(struct vmxnet3_cmd_ring *ring)
{
	ring->next2fill++;
	if (unlikely(ring->next2fill == ring->size)) {
		ring->next2fill = 0;
		VMXNET3_FLIP_RING_GEN(ring->gen);
	}
}

static inline void
vmxnet3_cmd_ring_adv_next2comp(struct vmxnet3_cmd_ring *ring)
{
	VMXNET3_INC_RING_IDX_ONLY(ring->next2comp, ring->size);
}

static inline int
vmxnet3_cmd_ring_desc_avail(struct vmxnet3_cmd_ring *ring)
{
	return (ring->next2comp > ring->next2fill ? 0 : ring->size) +
		ring->next2comp - ring->next2fill - 1;
}

struct vmxnet3_comp_ring {
	union Vmxnet3_GenericDesc *base;
	u32               size;
	u32               next2proc;
	u8                gen;
	u8                intr_idx;
	dma_addr_t           basePA;
};

static inline void
vmxnet3_comp_ring_adv_next2proc(struct vmxnet3_comp_ring *ring)
{
	ring->next2proc++;
	if (unlikely(ring->next2proc == ring->size)) {
		ring->next2proc = 0;
		VMXNET3_FLIP_RING_GEN(ring->gen);
	}
}

struct vmxnet3_tx_data_ring {
	struct Vmxnet3_TxDataDesc *base;
	u32              size;
	dma_addr_t          basePA;
};

enum vmxnet3_buf_map_type {
	VMXNET3_MAP_INVALID = 0,
	VMXNET3_MAP_NONE,
	VMXNET3_MAP_SINGLE,
	VMXNET3_MAP_PAGE,
};

struct vmxnet3_tx_buf_info {
	u32      map_type;
	u16      len;
	u16      sop_idx;
	dma_addr_t  dma_addr;
	struct sk_buff *skb;
};

struct vmxnet3_tq_driver_stats {
	u64 drop_total;     /* # of pkts dropped by the driver, the
				* counters below track droppings due to
				* different reasons
				*/
	u64 drop_too_many_frags;
	u64 drop_oversized_hdr;
	u64 drop_hdr_inspect_err;
	u64 drop_tso;

	u64 tx_ring_full;
	u64 linearized;         /* # of pkts linearized */
	u64 copy_skb_header;    /* # of times we have to copy skb header */
	u64 oversized_hdr;
};

struct vmxnet3_tx_ctx {
	bool   ipv4;
	u16 mss;
	u32 eth_ip_hdr_size; /* only valid for pkts requesting tso or csum
				 * offloading
				 */
	u32 l4_hdr_size;     /* only valid if mss != 0 */
	u32 copy_size;       /* # of bytes copied into the data ring */
	union Vmxnet3_GenericDesc *sop_txd;
	union Vmxnet3_GenericDesc *eop_txd;
};

struct vmxnet3_tx_queue {
	char			name[IFNAMSIZ+8]; /* To identify interrupt */
	struct vmxnet3_adapter		*adapter;
	spinlock_t                      tx_lock;
	struct vmxnet3_cmd_ring         tx_ring;
	struct vmxnet3_tx_buf_info      *buf_info;
	struct vmxnet3_tx_data_ring     data_ring;
	struct vmxnet3_comp_ring        comp_ring;
	struct Vmxnet3_TxQueueCtrl      *shared;
	struct vmxnet3_tq_driver_stats  stats;
	bool                            stopped;
	int                             num_stop;  /* # of times the queue is
						    * stopped */
	int				qid;
} __attribute__((__aligned__(SMP_CACHE_BYTES)));

enum vmxnet3_rx_buf_type {
	VMXNET3_RX_BUF_NONE = 0,
	VMXNET3_RX_BUF_SKB = 1,
	VMXNET3_RX_BUF_PAGE = 2
};

struct vmxnet3_rx_buf_info {
	enum vmxnet3_rx_buf_type buf_type;
	u16     len;
	union {
		struct sk_buff *skb;
		struct page    *page;
	};
	dma_addr_t dma_addr;
};

struct vmxnet3_rx_ctx {
	struct sk_buff *skb;
	u32 sop_idx;
};

struct vmxnet3_rq_driver_stats {
	u64 drop_total;
	u64 drop_err;
	u64 drop_fcs;
	u64 rx_buf_alloc_failure;
};

struct vmxnet3_rx_queue {
	char			name[IFNAMSIZ + 8]; /* To identify interrupt */
	struct vmxnet3_adapter	  *adapter;
	struct napi_struct        napi;
	struct vmxnet3_cmd_ring   rx_ring[2];
	struct vmxnet3_comp_ring  comp_ring;
	struct vmxnet3_rx_ctx     rx_ctx;
	u32 qid;            /* rqID in RCD for buffer from 1st ring */
	u32 qid2;           /* rqID in RCD for buffer from 2nd ring */
	u32 uncommitted[2]; /* # of buffers allocated since last RXPROD
				* update */
	struct vmxnet3_rx_buf_info     *buf_info[2];
	struct Vmxnet3_RxQueueCtrl            *shared;
	struct vmxnet3_rq_driver_stats  stats;
} __attribute__((__aligned__(SMP_CACHE_BYTES)));

#define VMXNET3_DEVICE_MAX_TX_QUEUES 8
#define VMXNET3_DEVICE_MAX_RX_QUEUES 8   /* Keep this value as a power of 2 */

/* Should be less than UPT1_RSS_MAX_IND_TABLE_SIZE */
#define VMXNET3_RSS_IND_TABLE_SIZE (VMXNET3_DEVICE_MAX_RX_QUEUES * 4)

#define VMXNET3_LINUX_MAX_MSIX_VECT     (VMXNET3_DEVICE_MAX_TX_QUEUES + \
					 VMXNET3_DEVICE_MAX_RX_QUEUES + 1)
#define VMXNET3_LINUX_MIN_MSIX_VECT     2 /* 1 for tx-rx pair and 1 for event */


struct vmxnet3_intr {
	enum vmxnet3_intr_mask_mode  mask_mode;
	enum vmxnet3_intr_type       type;	/* MSI-X, MSI, or INTx? */
	u8  num_intrs;			/* # of intr vectors */
	u8  event_intr_idx;		/* idx of the intr vector for event */
	u8  mod_levels[VMXNET3_LINUX_MAX_MSIX_VECT]; /* moderation level */
	char	event_msi_vector_name[IFNAMSIZ+11];
#ifdef CONFIG_PCI_MSI
	struct msix_entry msix_entries[VMXNET3_LINUX_MAX_MSIX_VECT];
#endif
};

/* Interrupt sharing schemes, share_intr */
#define VMXNET3_INTR_BUDDYSHARE 0    /* Corresponding tx,rx queues share irq */
#define VMXNET3_INTR_TXSHARE 1	     /* All tx queues share one irq */
#define VMXNET3_INTR_DONTSHARE 2     /* each queue has its own irq */


#define VMXNET3_STATE_BIT_RESETTING   0
#define VMXNET3_STATE_BIT_QUIESCED    1
struct vmxnet3_adapter {
	struct vmxnet3_tx_queue		tx_queue[VMXNET3_DEVICE_MAX_TX_QUEUES];
	struct vmxnet3_rx_queue		rx_queue[VMXNET3_DEVICE_MAX_RX_QUEUES];
	unsigned long			active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
	struct vmxnet3_intr		intr;
	spinlock_t			cmd_lock;
	struct Vmxnet3_DriverShared	*shared;
	struct Vmxnet3_PMConf		*pm_conf;
	struct Vmxnet3_TxQueueDesc	*tqd_start;     /* all tx queue desc */
	struct Vmxnet3_RxQueueDesc	*rqd_start;	/* all rx queue desc */
	struct net_device		*netdev;
	struct pci_dev			*pdev;

	u8			__iomem *hw_addr0; /* for BAR 0 */
	u8			__iomem *hw_addr1; /* for BAR 1 */

#ifdef VMXNET3_RSS
	struct UPT1_RSSConf		*rss_conf;
	bool				rss;
#endif
	u32				num_rx_queues;
	u32				num_tx_queues;

	/* rx buffer related */
	unsigned			skb_buf_size;
	int		rx_buf_per_pkt;  /* only apply to the 1st ring */
	dma_addr_t			shared_pa;
	dma_addr_t queue_desc_pa;

	/* Wake-on-LAN */
	u32     wol;

	/* Link speed */
	u32     link_speed; /* in mbps */

	u64     tx_timeout_count;
	struct work_struct work;

	unsigned long  state;    /* VMXNET3_STATE_BIT_xxx */

	int dev_number;
	int share_intr;
};

#define VMXNET3_WRITE_BAR0_REG(adapter, reg, val)  \
	writel((val), (adapter)->hw_addr0 + (reg))
#define VMXNET3_READ_BAR0_REG(adapter, reg)        \
	readl((adapter)->hw_addr0 + (reg))

#define VMXNET3_WRITE_BAR1_REG(adapter, reg, val)  \
	writel((val), (adapter)->hw_addr1 + (reg))
#define VMXNET3_READ_BAR1_REG(adapter, reg)        \
	readl((adapter)->hw_addr1 + (reg))

#define VMXNET3_WAKE_QUEUE_THRESHOLD(tq)  (5)
#define VMXNET3_RX_ALLOC_THRESHOLD(rq, ring_idx, adapter) \
	((rq)->rx_ring[ring_idx].size >> 3)

#define VMXNET3_GET_ADDR_LO(dma)   ((u32)(dma))
#define VMXNET3_GET_ADDR_HI(dma)   ((u32)(((u64)(dma)) >> 32))

/* must be a multiple of VMXNET3_RING_SIZE_ALIGN */
#define VMXNET3_DEF_TX_RING_SIZE    512
#define VMXNET3_DEF_RX_RING_SIZE    256

#define VMXNET3_MAX_ETH_HDR_SIZE    22
#define VMXNET3_MAX_SKB_BUF_SIZE    (3*1024)

int
vmxnet3_quiesce_dev(struct vmxnet3_adapter *adapter);

int
vmxnet3_activate_dev(struct vmxnet3_adapter *adapter);

void
vmxnet3_force_close(struct vmxnet3_adapter *adapter);

void
vmxnet3_reset_dev(struct vmxnet3_adapter *adapter);

void
vmxnet3_tq_destroy_all(struct vmxnet3_adapter *adapter);

void
vmxnet3_rq_destroy_all(struct vmxnet3_adapter *adapter);

int
vmxnet3_set_features(struct net_device *netdev, u32 features);

int
vmxnet3_create_queues(struct vmxnet3_adapter *adapter,
		      u32 tx_ring_size, u32 rx_ring_size, u32 rx_ring2_size);

extern void vmxnet3_set_ethtool_ops(struct net_device *netdev);

extern struct rtnl_link_stats64 *
vmxnet3_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats);

extern char vmxnet3_driver_name[];
#endif
