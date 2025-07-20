/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#ifndef _FBNIC_TXRX_H_
#define _FBNIC_TXRX_H_

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/u64_stats_sync.h>
#include <net/xdp.h>

struct fbnic_net;

/* Guarantee we have space needed for storing the buffer
 * To store the buffer we need:
 *	1 descriptor per page
 *	+ 1 descriptor for skb head
 *	+ 2 descriptors for metadata and optional metadata
 *	+ 7 descriptors to keep tail out of the same cacheline as head
 * If we cannot guarantee that then we should return TX_BUSY
 */
#define FBNIC_MAX_SKB_DESC	(MAX_SKB_FRAGS + 10)
#define FBNIC_TX_DESC_WAKEUP	(FBNIC_MAX_SKB_DESC * 2)
#define FBNIC_TX_DESC_MIN	roundup_pow_of_two(FBNIC_TX_DESC_WAKEUP)

/* To receive the worst case packet we need:
 *	1 descriptor for primary metadata
 *	+ 1 descriptor for optional metadata
 *	+ 1 descriptor for headers
 *	+ 4 descriptors for payload
 */
#define FBNIC_MAX_RX_PKT_DESC	7
#define FBNIC_RX_DESC_MIN	roundup_pow_of_two(FBNIC_MAX_RX_PKT_DESC * 2)

#define FBNIC_MAX_TXQS			128u
#define FBNIC_MAX_RXQS			128u

/* These apply to TWQs, TCQ, RCQ */
#define FBNIC_QUEUE_SIZE_MIN		16u
#define FBNIC_QUEUE_SIZE_MAX		SZ_64K

#define FBNIC_TXQ_SIZE_DEFAULT		1024
#define FBNIC_HPQ_SIZE_DEFAULT		256
#define FBNIC_PPQ_SIZE_DEFAULT		256
#define FBNIC_RCQ_SIZE_DEFAULT		1024
#define FBNIC_TX_USECS_DEFAULT		35
#define FBNIC_RX_USECS_DEFAULT		30
#define FBNIC_RX_FRAMES_DEFAULT		0

#define FBNIC_RX_TROOM \
	SKB_DATA_ALIGN(sizeof(struct skb_shared_info))
#define FBNIC_RX_HROOM \
	(ALIGN(FBNIC_RX_TROOM + NET_SKB_PAD, 128) - FBNIC_RX_TROOM)
#define FBNIC_RX_PAD			0
#define FBNIC_RX_MAX_HDR		(1536 - FBNIC_RX_PAD)
#define FBNIC_RX_PAYLD_OFFSET		0
#define FBNIC_RX_PAYLD_PG_CL		0

#define FBNIC_RING_F_DISABLED		BIT(0)
#define FBNIC_RING_F_CTX		BIT(1)
#define FBNIC_RING_F_STATS		BIT(2)	/* Ring's stats may be used */

struct fbnic_pkt_buff {
	struct xdp_buff buff;
	ktime_t hwtstamp;
	u32 data_truesize;
	u16 data_len;
	u16 nr_frags;
};

struct fbnic_queue_stats {
	u64 packets;
	u64 bytes;
	union {
		struct {
			u64 csum_partial;
			u64 lso;
			u64 ts_packets;
			u64 ts_lost;
			u64 stop;
			u64 wake;
		} twq;
		struct {
			u64 alloc_failed;
			u64 csum_complete;
			u64 csum_none;
		} rx;
	};
	u64 dropped;
	struct u64_stats_sync syncp;
};

/* Pagecnt bias is long max to reserve the last bit to catch overflow
 * cases where if we overcharge the bias it will flip over to be negative.
 */
#define PAGECNT_BIAS_MAX	LONG_MAX
struct fbnic_rx_buf {
	struct page *page;
	long pagecnt_bias;
};

struct fbnic_ring {
	/* Pointer to buffer specific info */
	union {
		struct fbnic_pkt_buff *pkt;	/* RCQ */
		struct fbnic_rx_buf *rx_buf;	/* BDQ */
		void **tx_buf;			/* TWQ */
		void *buffer;			/* Generic pointer */
	};

	u32 __iomem *doorbell;		/* Pointer to CSR space for ring */
	__le64 *desc;			/* Descriptor ring memory */
	u16 size_mask;			/* Size of ring in descriptors - 1 */
	u8 q_idx;			/* Logical netdev ring index */
	u8 flags;			/* Ring flags (FBNIC_RING_F_*) */

	u32 head, tail;			/* Head/Tail of ring */

	struct fbnic_queue_stats stats;

	/* Slow path fields follow */
	dma_addr_t dma;			/* Phys addr of descriptor memory */
	size_t size;			/* Size of descriptor ring in memory */
};

struct fbnic_q_triad {
	struct fbnic_ring sub0, sub1, cmpl;
};

struct fbnic_napi_vector {
	struct napi_struct napi;
	struct device *dev;		/* Device for DMA unmapping */
	struct page_pool *page_pool;
	struct fbnic_dev *fbd;

	u16 v_idx;
	u8 txt_count;
	u8 rxt_count;

	struct fbnic_q_triad qt[];
};

#define FBNIC_MAX_TXQS			128u
#define FBNIC_MAX_RXQS			128u

netdev_tx_t fbnic_xmit_frame(struct sk_buff *skb, struct net_device *dev);
netdev_features_t
fbnic_features_check(struct sk_buff *skb, struct net_device *dev,
		     netdev_features_t features);

void fbnic_aggregate_ring_rx_counters(struct fbnic_net *fbn,
				      struct fbnic_ring *rxr);
void fbnic_aggregate_ring_tx_counters(struct fbnic_net *fbn,
				      struct fbnic_ring *txr);

int fbnic_alloc_napi_vectors(struct fbnic_net *fbn);
void fbnic_free_napi_vectors(struct fbnic_net *fbn);
int fbnic_alloc_resources(struct fbnic_net *fbn);
void fbnic_free_resources(struct fbnic_net *fbn);
int fbnic_set_netif_queues(struct fbnic_net *fbn);
void fbnic_reset_netif_queues(struct fbnic_net *fbn);
irqreturn_t fbnic_msix_clean_rings(int irq, void *data);
void fbnic_napi_enable(struct fbnic_net *fbn);
void fbnic_napi_disable(struct fbnic_net *fbn);
void fbnic_enable(struct fbnic_net *fbn);
void fbnic_disable(struct fbnic_net *fbn);
void fbnic_flush(struct fbnic_net *fbn);
void fbnic_fill(struct fbnic_net *fbn);

void fbnic_napi_depletion_check(struct net_device *netdev);
int fbnic_wait_all_queues_idle(struct fbnic_dev *fbd, bool may_fail);

static inline int fbnic_napi_idx(const struct fbnic_napi_vector *nv)
{
	return nv->v_idx - FBNIC_NON_NAPI_VECTORS;
}

#endif /* _FBNIC_TXRX_H_ */
