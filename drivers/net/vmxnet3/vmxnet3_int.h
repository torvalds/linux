/*
 * Linux driver for VMware's vmxnet3 ethernet NIC.
 *
 * Copyright (C) 2008-2024, VMware, Inc. All Rights Reserved.
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
 * Maintained by: pv-drivers@vmware.com
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
#include <linux/bpf.h>
#include <net/page_pool/helpers.h>
#include <net/xdp.h>

#include "vmxnet3_defs.h"

#ifdef DEBUG
# define VMXNET3_DRIVER_VERSION_REPORT VMXNET3_DRIVER_VERSION_STRING"-NAPI(debug)"
#else
# define VMXNET3_DRIVER_VERSION_REPORT VMXNET3_DRIVER_VERSION_STRING"-NAPI"
#endif


/*
 * Version numbers
 */
#define VMXNET3_DRIVER_VERSION_STRING   "1.9.0.0-k"

/* Each byte of this 32-bit integer encodes a version number in
 * VMXNET3_DRIVER_VERSION_STRING.
 */
#define VMXNET3_DRIVER_VERSION_NUM      0x01090000

#if defined(CONFIG_PCI_MSI)
	/* RSS only makes sense if MSI-X is supported. */
	#define VMXNET3_RSS
#endif

#define VMXNET3_REV_9		8	/* Vmxnet3 Rev. 9 */
#define VMXNET3_REV_8		7	/* Vmxnet3 Rev. 8 */
#define VMXNET3_REV_7		6	/* Vmxnet3 Rev. 7 */
#define VMXNET3_REV_6		5	/* Vmxnet3 Rev. 6 */
#define VMXNET3_REV_5		4	/* Vmxnet3 Rev. 5 */
#define VMXNET3_REV_4		3	/* Vmxnet3 Rev. 4 */
#define VMXNET3_REV_3		2	/* Vmxnet3 Rev. 3 */
#define VMXNET3_REV_2		1	/* Vmxnet3 Rev. 2 */
#define VMXNET3_REV_1		0	/* Vmxnet3 Rev. 1 */

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
 * Maximum devices supported.
 */
#define MAX_ETHERNET_CARDS		10
#define MAX_PCI_PASSTHRU_DEVICE		6

struct vmxnet3_cmd_ring {
	union Vmxnet3_GenericDesc *base;
	u32		size;
	u32		next2fill;
	u32		next2comp;
	u8		gen;
	u8              isOutOfOrder;
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

struct vmxnet3_tx_ts_ring {
	struct Vmxnet3_TxTSDesc *base;
	dma_addr_t          basePA;
};

#define VMXNET3_MAP_NONE	0
#define VMXNET3_MAP_SINGLE	BIT(0)
#define VMXNET3_MAP_PAGE	BIT(1)
#define VMXNET3_MAP_XDP		BIT(2)

struct vmxnet3_tx_buf_info {
	u32      map_type;
	u16      len;
	u16      sop_idx;
	dma_addr_t  dma_addr;
	union {
		struct sk_buff *skb;
		struct xdp_frame *xdpf;
	};
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

	u64 xdp_xmit;
	u64 xdp_xmit_err;
};

struct vmxnet3_tx_ctx {
	bool   ipv4;
	bool   ipv6;
	u16 mss;
	u32    l4_offset;	/* only valid for pkts requesting tso or csum
				 * offloading. For encap offload, it refers to
				 * inner L4 offset i.e. it includes outer header
				 * encap header and inner eth and ip header size
				 */

	u32	l4_hdr_size;	/* only valid if mss != 0
				 * Refers to inner L4 hdr size for encap
				 * offload
				 */
	u32 copy_size;       /* # of bytes copied into the data ring */
	union Vmxnet3_GenericDesc *sop_txd;
	union Vmxnet3_GenericDesc *eop_txd;
	struct Vmxnet3_TxTSDesc *ts_txd;
};

struct vmxnet3_tx_queue {
	char			name[IFNAMSIZ+8]; /* To identify interrupt */
	struct vmxnet3_adapter		*adapter;
	spinlock_t                      tx_lock;
	struct vmxnet3_cmd_ring         tx_ring;
	struct vmxnet3_tx_buf_info      *buf_info;
	struct vmxnet3_tx_data_ring     data_ring;
	struct vmxnet3_tx_ts_ring       ts_ring;
	struct vmxnet3_comp_ring        comp_ring;
	struct Vmxnet3_TxQueueCtrl      *shared;
	struct vmxnet3_tq_driver_stats  stats;
	bool                            stopped;
	int                             num_stop;  /* # of times the queue is
						    * stopped */
	int				qid;
	u16				txdata_desc_size;
	u16                             tx_ts_desc_size;
	u16                             tsPktCount;
} ____cacheline_aligned;

enum vmxnet3_rx_buf_type {
	VMXNET3_RX_BUF_NONE = 0,
	VMXNET3_RX_BUF_SKB = 1,
	VMXNET3_RX_BUF_PAGE = 2,
	VMXNET3_RX_BUF_XDP = 3,
};

#define VMXNET3_RXD_COMP_PENDING        0
#define VMXNET3_RXD_COMP_DONE           1

struct vmxnet3_rx_buf_info {
	enum vmxnet3_rx_buf_type buf_type;
	u16     len;
	u8      comp_state;
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

	u64 xdp_packets;	/* Total packets processed by XDP. */
	u64 xdp_tx;
	u64 xdp_redirects;
	u64 xdp_drops;
	u64 xdp_aborted;
};

struct vmxnet3_rx_data_ring {
	Vmxnet3_RxDataDesc *base;
	dma_addr_t basePA;
	u16 desc_size;
};

struct vmxnet3_rx_ts_ring {
	struct Vmxnet3_RxTSDesc *base;
	dma_addr_t basePA;
};

struct vmxnet3_rx_queue {
	char			name[IFNAMSIZ + 8]; /* To identify interrupt */
	struct vmxnet3_adapter	  *adapter;
	struct napi_struct        napi;
	struct vmxnet3_cmd_ring   rx_ring[2];
	struct vmxnet3_rx_data_ring data_ring;
	struct vmxnet3_comp_ring  comp_ring;
	struct vmxnet3_rx_ts_ring ts_ring;
	struct vmxnet3_rx_ctx     rx_ctx;
	u32 qid;            /* rqID in RCD for buffer from 1st ring */
	u32 qid2;           /* rqID in RCD for buffer from 2nd ring */
	u32 dataRingQid;    /* rqID in RCD for buffer from data ring */
	struct vmxnet3_rx_buf_info     *buf_info[2];
	struct Vmxnet3_RxQueueCtrl            *shared;
	struct vmxnet3_rq_driver_stats  stats;
	struct page_pool *page_pool;
	struct xdp_rxq_info xdp_rxq;
	u16                             rx_ts_desc_size;
} ____cacheline_aligned;

#define VMXNET3_DEVICE_MAX_TX_QUEUES 32
#define VMXNET3_DEVICE_MAX_RX_QUEUES 32   /* Keep this value as a power of 2 */

#define VMXNET3_DEVICE_DEFAULT_TX_QUEUES 8
#define VMXNET3_DEVICE_DEFAULT_RX_QUEUES 8   /* Keep this value as a power of 2 */

/* Should be less than UPT1_RSS_MAX_IND_TABLE_SIZE */
#define VMXNET3_RSS_IND_TABLE_SIZE (VMXNET3_DEVICE_MAX_RX_QUEUES * 4)

#define VMXNET3_LINUX_MAX_MSIX_VECT     (VMXNET3_DEVICE_MAX_TX_QUEUES + \
					 VMXNET3_DEVICE_MAX_RX_QUEUES + 1)
#define VMXNET3_LINUX_MIN_MSIX_VECT     3 /* 1 for tx, 1 for rx pair and 1 for event */


struct vmxnet3_intr {
	enum vmxnet3_intr_mask_mode  mask_mode;
	enum vmxnet3_intr_type       type;	/* MSI-X, MSI, or INTx? */
	u8  num_intrs;			/* # of intr vectors */
	u8  event_intr_idx;		/* idx of the intr vector for event */
	u8  mod_levels[VMXNET3_LINUX_MAX_MSIX_VECT]; /* moderation level */
	char	event_msi_vector_name[IFNAMSIZ+17];
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
	u8                              version;

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
	dma_addr_t coal_conf_pa;

	/* Wake-on-LAN */
	u32     wol;

	/* Link speed */
	u32     link_speed; /* in mbps */

	u64     tx_timeout_count;

	/* Ring sizes */
	u32 tx_ring_size;
	u32 rx_ring_size;
	u32 rx_ring2_size;

	/* Size of buffer in the data ring */
	u16 txdata_desc_size;
	u16 rxdata_desc_size;

	bool rxdataring_enabled;
	bool default_rss_fields;
	enum Vmxnet3_RSSField rss_fields;

	struct work_struct work;

	unsigned long  state;    /* VMXNET3_STATE_BIT_xxx */

	int share_intr;

	struct Vmxnet3_CoalesceScheme *coal_conf;
	bool   default_coal_mode;

	dma_addr_t adapter_pa;
	dma_addr_t pm_conf_pa;
	dma_addr_t rss_conf_pa;
	bool   queuesExtEnabled;
	struct Vmxnet3_RingBufferSize     ringBufSize;
	u32    devcap_supported[8];
	u32    ptcap_supported[8];
	u32    dev_caps[8];
	u16    tx_prod_offset;
	u16    rx_prod_offset;
	u16    rx_prod2_offset;
	struct bpf_prog __rcu *xdp_bpf_prog;
	struct Vmxnet3_LatencyConf *latencyConf;
	/* Size of buffer in the ts ring */
	u16     tx_ts_desc_size;
	u16     rx_ts_desc_size;
	u32     disabledOffloads;
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

#define VMXNET3_VERSION_GE_2(adapter) \
	(adapter->version >= VMXNET3_REV_2 + 1)
#define VMXNET3_VERSION_GE_3(adapter) \
	(adapter->version >= VMXNET3_REV_3 + 1)
#define VMXNET3_VERSION_GE_4(adapter) \
	(adapter->version >= VMXNET3_REV_4 + 1)
#define VMXNET3_VERSION_GE_5(adapter) \
	(adapter->version >= VMXNET3_REV_5 + 1)
#define VMXNET3_VERSION_GE_6(adapter) \
	(adapter->version >= VMXNET3_REV_6 + 1)
#define VMXNET3_VERSION_GE_7(adapter) \
	(adapter->version >= VMXNET3_REV_7 + 1)
#define VMXNET3_VERSION_GE_8(adapter) \
	(adapter->version >= VMXNET3_REV_8 + 1)
#define VMXNET3_VERSION_GE_9(adapter) \
	(adapter->version >= VMXNET3_REV_9 + 1)

/* must be a multiple of VMXNET3_RING_SIZE_ALIGN */
#define VMXNET3_DEF_TX_RING_SIZE    512
#define VMXNET3_DEF_RX_RING_SIZE    1024
#define VMXNET3_DEF_RX_RING2_SIZE   512

#define VMXNET3_DEF_RXDATA_DESC_SIZE 128

#define VMXNET3_MAX_ETH_HDR_SIZE    22
#define VMXNET3_MAX_SKB_BUF_SIZE    (3*1024)

#define VMXNET3_GET_RING_IDX(adapter, rqID)		\
	((rqID >= adapter->num_rx_queues &&		\
	 rqID < 2 * adapter->num_rx_queues) ? 1 : 0)	\

#define VMXNET3_RX_DATA_RING(adapter, rqID)		\
	(rqID >= 2 * adapter->num_rx_queues &&		\
	rqID < 3 * adapter->num_rx_queues)		\

#define VMXNET3_COAL_STATIC_DEFAULT_DEPTH	64

#define VMXNET3_COAL_RBC_RATE(usecs) (1000000 / usecs)
#define VMXNET3_COAL_RBC_USECS(rbc_rate) (1000000 / rbc_rate)
#define VMXNET3_RSS_FIELDS_DEFAULT (VMXNET3_RSS_FIELDS_TCPIP4 | \
				    VMXNET3_RSS_FIELDS_TCPIP6)

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
vmxnet3_rq_create_all(struct vmxnet3_adapter *adapter);

void
vmxnet3_adjust_rx_ring_size(struct vmxnet3_adapter *adapter);

netdev_features_t
vmxnet3_fix_features(struct net_device *netdev, netdev_features_t features);

netdev_features_t
vmxnet3_features_check(struct sk_buff *skb,
		       struct net_device *netdev, netdev_features_t features);

int
vmxnet3_set_features(struct net_device *netdev, netdev_features_t features);

int
vmxnet3_create_queues(struct vmxnet3_adapter *adapter,
		      u32 tx_ring_size, u32 rx_ring_size, u32 rx_ring2_size,
		      u16 txdata_desc_size, u16 rxdata_desc_size);

void vmxnet3_set_ethtool_ops(struct net_device *netdev);

void vmxnet3_get_stats64(struct net_device *dev,
			 struct rtnl_link_stats64 *stats);
bool vmxnet3_check_ptcapability(u32 cap_supported, u32 cap);

extern char vmxnet3_driver_name[];
#endif
