/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#ifndef _MPNIC_TXRX_H_
#define _MPNIC_TXRX_H_

#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/u64_stats_sync.h>
#include <net/netdev_queues.h>
#include <net/xdp.h>

#include "mpnic.h"

struct mpnic_net;

/* Space we have to have available in a work queue to take a packet:
 *	1 descriptor per page
 *	+ 1 descriptor for the skb head
 *	+ 1 descriptor for the metadata
 *	+ 7 descriptors to keep the tail out of the head's cacheline
 * If we cannot guarantee that we return NETDEV_TX_BUSY.
 */
#define MPNIC_MAX_SKB_DESC		(MAX_SKB_FRAGS + 9)
#define MPNIC_TX_DESC_WAKEUP		(MPNIC_MAX_SKB_DESC * 2)

#define MPNIC_MAX_NAPI_VECTORS		1024u

/* Number of buffer descriptors the driver posts before ringing the
 * doorbell. The device consumes whatever the doorbell points at, this is
 * purely to keep the driver from writing the CSR for every descriptor.
 */
#define MPNIC_BDQ_BATCH_SIZE		64u

#define MPNIC_TXQ_SIZE_DEFAULT		1024
#define MPNIC_HPQ_SIZE_DEFAULT		256
#define MPNIC_PPQ_SIZE_DEFAULT		256
#define MPNIC_RCQ_SIZE_DEFAULT		1024

/* Room the device has to leave in front of and behind every header so the
 * driver can build an skb around it in place. The headroom is padded out
 * so that consecutive headers in one page start 128 B aligned.
 */
#define MPNIC_RX_TROOM \
	SKB_DATA_ALIGN(sizeof(struct skb_shared_info))
#define MPNIC_RX_HROOM \
	(ALIGN(MPNIC_RX_TROOM + XDP_PACKET_HEADROOM, 128) - MPNIC_RX_TROOM)

/* Headers longer than this are split off into the payload queue */
#define MPNIC_RX_MAX_HDR		1536

/* A page is handed out to many packets, each of which takes one reference.
 * Rather than a locked increment per packet the driver takes a batch of
 * references up front and returns whatever is left when the page is done.
 */
#define MPNIC_PAGECNT_BIAS_MAX		(PAGE_SIZE + 1)

#define MPNIC_MAX_JUMBO_FRAME_SIZE	9742

#define MPNIC_RX_HPQ_DROP_THRS \
	DIV_ROUND_UP(MPNIC_RX_HROOM + MPNIC_RX_MAX_HDR + MPNIC_RX_TROOM, \
		     PAGE_SIZE)
#define MPNIC_RX_PPQ_DROP_THRS \
	DIV_ROUND_UP(MPNIC_MAX_JUMBO_FRAME_SIZE, PAGE_SIZE)

/* The page a buffer descriptor queue is currently handing out. Records
 * how many of the references taken on it are still unused.
 */
struct mpnic_pg_ctxt {
	struct page	*page;
	long		pagecnt_bias;
	u32		idx;
};

struct mpnic_pkt_ctxt {
	struct xdp_buff buff;
	bool add_frag_failed;
};

struct mpnic_rcq_state {
	struct mpnic_pkt_ctxt pkt;
	struct mpnic_pg_ctxt hdr;
	struct mpnic_pg_ctxt payld;
};

struct mpnic_queue_stats {
	union {
		struct {
			u64_stats_t dropped;
			u64_stats_t stop;
		} twq;
		struct {
			u64_stats_t packets;
			u64_stats_t bytes;
			u64_stats_t wake;
		} tcq;
		struct {
			u64_stats_t packets;
			u64_stats_t bytes;
			u64_stats_t dropped;
			u64_stats_t alloc_failed;
			u64_stats_t errors;
		} rcq;
		struct {
			u64_stats_t alloc_failed;
		} bdq;
	};
	struct u64_stats_sync syncp;
};

struct mpnic_ring {
	union {
		struct mpnic_rcq_state *state;	/* RCQ */
		struct page **rx_buf;		/* BDQ */
		void **tx_buf;			/* TWQ */
		void *buffer;			/* Generic pointer */
	};

	u32 __iomem *doorbell;		/* Pointer to CSR space for ring */
	__le64 *desc;			/* Descriptor ring memory */
	u16 size_mask;			/* Size of ring in descriptors - 1 */
	u16 q_idx;			/* Hardware queue index */

	u32 head, tail;			/* Head/Tail of ring */

	union {
		/* BDQ only */
		struct page_pool *page_pool;

		/* TWQ only, index of the metadata descriptor of the last
		 * packet placed in the ring without ringing the doorbell,
		 * -1 if the doorbell is in sync with the tail.
		 */
		s32 deferred_meta;
	};

	struct mpnic_queue_stats stats;

	/* Slow path fields follow */
	dma_addr_t dma;			/* Phys addr of descriptor memory */
	size_t size;			/* Size of descriptor ring in memory */
};

/* The device pairs two work queues with one completion queue. On the Rx
 * side they are the header and the payload buffer descriptor queues; on
 * the Tx side only the first one is used for now, the second one becomes
 * the XDP ring.
 */
struct mpnic_q_triad {
	struct xdp_rxq_info xdp_rxq;
	struct mpnic_ring sub0, sub1, cmpl;
};

struct mpnic_napi_vector {
	struct napi_struct napi;
	struct device *dev;		/* Device for DMA unmapping */
	struct mpnic_dev *mpd;

	u16 v_idx;
	u16 txt_count;
	u16 rxt_count;

	char name[IFNAMSIZ + 11];

	struct mpnic_q_triad qt[];
};

netdev_tx_t mpnic_xmit_frame(struct sk_buff *skb, struct net_device *dev);
int mpnic_alloc_napi_vectors(struct mpnic_net *mpn);
void mpnic_free_napi_vectors(struct mpnic_net *mpn);
void mpnic_stats_attach_rings(struct mpnic_net *mpn);
void mpnic_stats_fold_rings(struct mpnic_net *mpn);
int mpnic_alloc_resources(struct mpnic_net *mpn);
void mpnic_free_resources(struct mpnic_net *mpn);
int mpnic_set_netif_queues(struct mpnic_net *mpn);
void mpnic_reset_netif_queues(struct mpnic_net *mpn);
void mpnic_napi_enable(struct mpnic_net *mpn);
void mpnic_napi_disable(struct mpnic_net *mpn);
void mpnic_napi_depletion_check(struct mpnic_net *mpn);
void mpnic_enable(struct mpnic_net *mpn);
void mpnic_disable(struct mpnic_net *mpn);
void mpnic_wait_all_queues_idle(struct mpnic_dev *mpd);
void mpnic_flush(struct mpnic_net *mpn);
void mpnic_fill(struct mpnic_net *mpn);

#endif /* _MPNIC_TXRX_H_ */
