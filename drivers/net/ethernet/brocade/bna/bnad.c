// SPDX-License-Identifier: GPL-2.0-only
/*
 * Linux network driver for QLogic BR-series Converged Network Adapter.
 */
/*
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014-2015 QLogic Corporation
 * All rights reserved
 * www.qlogic.com
 */
#include <linux/bitops.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/in.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/prefetch.h>
#include <linux/module.h>

#include "bnad.h"
#include "bna.h"
#include "cna.h"

static DEFINE_MUTEX(bnad_fwimg_mutex);

/*
 * Module params
 */
static uint bnad_msix_disable;
module_param(bnad_msix_disable, uint, 0444);
MODULE_PARM_DESC(bnad_msix_disable, "Disable MSIX mode");

static uint bnad_ioc_auto_recover = 1;
module_param(bnad_ioc_auto_recover, uint, 0444);
MODULE_PARM_DESC(bnad_ioc_auto_recover, "Enable / Disable auto recovery");

static uint bna_debugfs_enable = 1;
module_param(bna_debugfs_enable, uint, 0644);
MODULE_PARM_DESC(bna_debugfs_enable, "Enables debugfs feature, default=1,"
		 " Range[false:0|true:1]");

/*
 * Global variables
 */
static u32 bnad_rxqs_per_cq = 2;
static atomic_t bna_id;
static const u8 bnad_bcast_addr[] __aligned(2) =
	{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

/*
 * Local MACROS
 */
#define BNAD_GET_MBOX_IRQ(_bnad)				\
	(((_bnad)->cfg_flags & BNAD_CF_MSIX) ?			\
	 ((_bnad)->msix_table[BNAD_MAILBOX_MSIX_INDEX].vector) : \
	 ((_bnad)->pcidev->irq))

#define BNAD_FILL_UNMAPQ_MEM_REQ(_res_info, _num, _size)	\
do {								\
	(_res_info)->res_type = BNA_RES_T_MEM;			\
	(_res_info)->res_u.mem_info.mem_type = BNA_MEM_T_KVA;	\
	(_res_info)->res_u.mem_info.num = (_num);		\
	(_res_info)->res_u.mem_info.len = (_size);		\
} while (0)

/*
 * Reinitialize completions in CQ, once Rx is taken down
 */
static void
bnad_cq_cleanup(struct bnad *bnad, struct bna_ccb *ccb)
{
	struct bna_cq_entry *cmpl;
	int i;

	for (i = 0; i < ccb->q_depth; i++) {
		cmpl = &((struct bna_cq_entry *)ccb->sw_q)[i];
		cmpl->valid = 0;
	}
}

/* Tx Datapath functions */


/* Caller should ensure that the entry at unmap_q[index] is valid */
static u32
bnad_tx_buff_unmap(struct bnad *bnad,
			      struct bnad_tx_unmap *unmap_q,
			      u32 q_depth, u32 index)
{
	struct bnad_tx_unmap *unmap;
	struct sk_buff *skb;
	int vector, nvecs;

	unmap = &unmap_q[index];
	nvecs = unmap->nvecs;

	skb = unmap->skb;
	unmap->skb = NULL;
	unmap->nvecs = 0;
	dma_unmap_single(&bnad->pcidev->dev,
		dma_unmap_addr(&unmap->vectors[0], dma_addr),
		skb_headlen(skb), DMA_TO_DEVICE);
	dma_unmap_addr_set(&unmap->vectors[0], dma_addr, 0);
	nvecs--;

	vector = 0;
	while (nvecs) {
		vector++;
		if (vector == BFI_TX_MAX_VECTORS_PER_WI) {
			vector = 0;
			BNA_QE_INDX_INC(index, q_depth);
			unmap = &unmap_q[index];
		}

		dma_unmap_page(&bnad->pcidev->dev,
			dma_unmap_addr(&unmap->vectors[vector], dma_addr),
			dma_unmap_len(&unmap->vectors[vector], dma_len),
			DMA_TO_DEVICE);
		dma_unmap_addr_set(&unmap->vectors[vector], dma_addr, 0);
		nvecs--;
	}

	BNA_QE_INDX_INC(index, q_depth);

	return index;
}

/*
 * Frees all pending Tx Bufs
 * At this point no activity is expected on the Q,
 * so DMA unmap & freeing is fine.
 */
static void
bnad_txq_cleanup(struct bnad *bnad, struct bna_tcb *tcb)
{
	struct bnad_tx_unmap *unmap_q = tcb->unmap_q;
	struct sk_buff *skb;
	int i;

	for (i = 0; i < tcb->q_depth; i++) {
		skb = unmap_q[i].skb;
		if (!skb)
			continue;
		bnad_tx_buff_unmap(bnad, unmap_q, tcb->q_depth, i);

		dev_kfree_skb_any(skb);
	}
}

/*
 * bnad_txcmpl_process : Frees the Tx bufs on Tx completion
 * Can be called in a) Interrupt context
 *		    b) Sending context
 */
static u32
bnad_txcmpl_process(struct bnad *bnad, struct bna_tcb *tcb)
{
	u32 sent_packets = 0, sent_bytes = 0;
	u32 wis, unmap_wis, hw_cons, cons, q_depth;
	struct bnad_tx_unmap *unmap_q = tcb->unmap_q;
	struct bnad_tx_unmap *unmap;
	struct sk_buff *skb;

	/* Just return if TX is stopped */
	if (!test_bit(BNAD_TXQ_TX_STARTED, &tcb->flags))
		return 0;

	hw_cons = *(tcb->hw_consumer_index);
	rmb();
	cons = tcb->consumer_index;
	q_depth = tcb->q_depth;

	wis = BNA_Q_INDEX_CHANGE(cons, hw_cons, q_depth);
	BUG_ON(!(wis <= BNA_QE_IN_USE_CNT(tcb, tcb->q_depth)));

	while (wis) {
		unmap = &unmap_q[cons];

		skb = unmap->skb;

		sent_packets++;
		sent_bytes += skb->len;

		unmap_wis = BNA_TXQ_WI_NEEDED(unmap->nvecs);
		wis -= unmap_wis;

		cons = bnad_tx_buff_unmap(bnad, unmap_q, q_depth, cons);
		dev_kfree_skb_any(skb);
	}

	/* Update consumer pointers. */
	tcb->consumer_index = hw_cons;

	tcb->txq->tx_packets += sent_packets;
	tcb->txq->tx_bytes += sent_bytes;

	return sent_packets;
}

static u32
bnad_tx_complete(struct bnad *bnad, struct bna_tcb *tcb)
{
	struct net_device *netdev = bnad->netdev;
	u32 sent = 0;

	if (test_and_set_bit(BNAD_TXQ_FREE_SENT, &tcb->flags))
		return 0;

	sent = bnad_txcmpl_process(bnad, tcb);
	if (sent) {
		if (netif_queue_stopped(netdev) &&
		    netif_carrier_ok(netdev) &&
		    BNA_QE_FREE_CNT(tcb, tcb->q_depth) >=
				    BNAD_NETIF_WAKE_THRESHOLD) {
			if (test_bit(BNAD_TXQ_TX_STARTED, &tcb->flags)) {
				netif_wake_queue(netdev);
				BNAD_UPDATE_CTR(bnad, netif_queue_wakeup);
			}
		}
	}

	if (likely(test_bit(BNAD_TXQ_TX_STARTED, &tcb->flags)))
		bna_ib_ack(tcb->i_dbell, sent);

	smp_mb__before_atomic();
	clear_bit(BNAD_TXQ_FREE_SENT, &tcb->flags);

	return sent;
}

/* MSIX Tx Completion Handler */
static irqreturn_t
bnad_msix_tx(int irq, void *data)
{
	struct bna_tcb *tcb = (struct bna_tcb *)data;
	struct bnad *bnad = tcb->bnad;

	bnad_tx_complete(bnad, tcb);

	return IRQ_HANDLED;
}

static inline void
bnad_rxq_alloc_uninit(struct bnad *bnad, struct bna_rcb *rcb)
{
	struct bnad_rx_unmap_q *unmap_q = rcb->unmap_q;

	unmap_q->reuse_pi = -1;
	unmap_q->alloc_order = -1;
	unmap_q->map_size = 0;
	unmap_q->type = BNAD_RXBUF_NONE;
}

/* Default is page-based allocation. Multi-buffer support - TBD */
static int
bnad_rxq_alloc_init(struct bnad *bnad, struct bna_rcb *rcb)
{
	struct bnad_rx_unmap_q *unmap_q = rcb->unmap_q;
	int order;

	bnad_rxq_alloc_uninit(bnad, rcb);

	order = get_order(rcb->rxq->buffer_size);

	unmap_q->type = BNAD_RXBUF_PAGE;

	if (bna_is_small_rxq(rcb->id)) {
		unmap_q->alloc_order = 0;
		unmap_q->map_size = rcb->rxq->buffer_size;
	} else {
		if (rcb->rxq->multi_buffer) {
			unmap_q->alloc_order = 0;
			unmap_q->map_size = rcb->rxq->buffer_size;
			unmap_q->type = BNAD_RXBUF_MULTI_BUFF;
		} else {
			unmap_q->alloc_order = order;
			unmap_q->map_size =
				(rcb->rxq->buffer_size > 2048) ?
				PAGE_SIZE << order : 2048;
		}
	}

	BUG_ON((PAGE_SIZE << order) % unmap_q->map_size);

	return 0;
}

static inline void
bnad_rxq_cleanup_page(struct bnad *bnad, struct bnad_rx_unmap *unmap)
{
	if (!unmap->page)
		return;

	dma_unmap_page(&bnad->pcidev->dev,
			dma_unmap_addr(&unmap->vector, dma_addr),
			unmap->vector.len, DMA_FROM_DEVICE);
	put_page(unmap->page);
	unmap->page = NULL;
	dma_unmap_addr_set(&unmap->vector, dma_addr, 0);
	unmap->vector.len = 0;
}

static inline void
bnad_rxq_cleanup_skb(struct bnad *bnad, struct bnad_rx_unmap *unmap)
{
	if (!unmap->skb)
		return;

	dma_unmap_single(&bnad->pcidev->dev,
			dma_unmap_addr(&unmap->vector, dma_addr),
			unmap->vector.len, DMA_FROM_DEVICE);
	dev_kfree_skb_any(unmap->skb);
	unmap->skb = NULL;
	dma_unmap_addr_set(&unmap->vector, dma_addr, 0);
	unmap->vector.len = 0;
}

static void
bnad_rxq_cleanup(struct bnad *bnad, struct bna_rcb *rcb)
{
	struct bnad_rx_unmap_q *unmap_q = rcb->unmap_q;
	int i;

	for (i = 0; i < rcb->q_depth; i++) {
		struct bnad_rx_unmap *unmap = &unmap_q->unmap[i];

		if (BNAD_RXBUF_IS_SK_BUFF(unmap_q->type))
			bnad_rxq_cleanup_skb(bnad, unmap);
		else
			bnad_rxq_cleanup_page(bnad, unmap);
	}
	bnad_rxq_alloc_uninit(bnad, rcb);
}

static u32
bnad_rxq_refill_page(struct bnad *bnad, struct bna_rcb *rcb, u32 nalloc)
{
	u32 alloced, prod, q_depth;
	struct bnad_rx_unmap_q *unmap_q = rcb->unmap_q;
	struct bnad_rx_unmap *unmap, *prev;
	struct bna_rxq_entry *rxent;
	struct page *page;
	u32 page_offset, alloc_size;
	dma_addr_t dma_addr;

	prod = rcb->producer_index;
	q_depth = rcb->q_depth;

	alloc_size = PAGE_SIZE << unmap_q->alloc_order;
	alloced = 0;

	while (nalloc--) {
		unmap = &unmap_q->unmap[prod];

		if (unmap_q->reuse_pi < 0) {
			page = alloc_pages(GFP_ATOMIC | __GFP_COMP,
					unmap_q->alloc_order);
			page_offset = 0;
		} else {
			prev = &unmap_q->unmap[unmap_q->reuse_pi];
			page = prev->page;
			page_offset = prev->page_offset + unmap_q->map_size;
			get_page(page);
		}

		if (unlikely(!page)) {
			BNAD_UPDATE_CTR(bnad, rxbuf_alloc_failed);
			rcb->rxq->rxbuf_alloc_failed++;
			goto finishing;
		}

		dma_addr = dma_map_page(&bnad->pcidev->dev, page, page_offset,
					unmap_q->map_size, DMA_FROM_DEVICE);
		if (dma_mapping_error(&bnad->pcidev->dev, dma_addr)) {
			put_page(page);
			BNAD_UPDATE_CTR(bnad, rxbuf_map_failed);
			rcb->rxq->rxbuf_map_failed++;
			goto finishing;
		}

		unmap->page = page;
		unmap->page_offset = page_offset;
		dma_unmap_addr_set(&unmap->vector, dma_addr, dma_addr);
		unmap->vector.len = unmap_q->map_size;
		page_offset += unmap_q->map_size;

		if (page_offset < alloc_size)
			unmap_q->reuse_pi = prod;
		else
			unmap_q->reuse_pi = -1;

		rxent = &((struct bna_rxq_entry *)rcb->sw_q)[prod];
		BNA_SET_DMA_ADDR(dma_addr, &rxent->host_addr);
		BNA_QE_INDX_INC(prod, q_depth);
		alloced++;
	}

finishing:
	if (likely(alloced)) {
		rcb->producer_index = prod;
		smp_mb();
		if (likely(test_bit(BNAD_RXQ_POST_OK, &rcb->flags)))
			bna_rxq_prod_indx_doorbell(rcb);
	}

	return alloced;
}

static u32
bnad_rxq_refill_skb(struct bnad *bnad, struct bna_rcb *rcb, u32 nalloc)
{
	u32 alloced, prod, q_depth, buff_sz;
	struct bnad_rx_unmap_q *unmap_q = rcb->unmap_q;
	struct bnad_rx_unmap *unmap;
	struct bna_rxq_entry *rxent;
	struct sk_buff *skb;
	dma_addr_t dma_addr;

	buff_sz = rcb->rxq->buffer_size;
	prod = rcb->producer_index;
	q_depth = rcb->q_depth;

	alloced = 0;
	while (nalloc--) {
		unmap = &unmap_q->unmap[prod];

		skb = netdev_alloc_skb_ip_align(bnad->netdev, buff_sz);

		if (unlikely(!skb)) {
			BNAD_UPDATE_CTR(bnad, rxbuf_alloc_failed);
			rcb->rxq->rxbuf_alloc_failed++;
			goto finishing;
		}

		dma_addr = dma_map_single(&bnad->pcidev->dev, skb->data,
					  buff_sz, DMA_FROM_DEVICE);
		if (dma_mapping_error(&bnad->pcidev->dev, dma_addr)) {
			dev_kfree_skb_any(skb);
			BNAD_UPDATE_CTR(bnad, rxbuf_map_failed);
			rcb->rxq->rxbuf_map_failed++;
			goto finishing;
		}

		unmap->skb = skb;
		dma_unmap_addr_set(&unmap->vector, dma_addr, dma_addr);
		unmap->vector.len = buff_sz;

		rxent = &((struct bna_rxq_entry *)rcb->sw_q)[prod];
		BNA_SET_DMA_ADDR(dma_addr, &rxent->host_addr);
		BNA_QE_INDX_INC(prod, q_depth);
		alloced++;
	}

finishing:
	if (likely(alloced)) {
		rcb->producer_index = prod;
		smp_mb();
		if (likely(test_bit(BNAD_RXQ_POST_OK, &rcb->flags)))
			bna_rxq_prod_indx_doorbell(rcb);
	}

	return alloced;
}

static inline void
bnad_rxq_post(struct bnad *bnad, struct bna_rcb *rcb)
{
	struct bnad_rx_unmap_q *unmap_q = rcb->unmap_q;
	u32 to_alloc;

	to_alloc = BNA_QE_FREE_CNT(rcb, rcb->q_depth);
	if (!(to_alloc >> BNAD_RXQ_REFILL_THRESHOLD_SHIFT))
		return;

	if (BNAD_RXBUF_IS_SK_BUFF(unmap_q->type))
		bnad_rxq_refill_skb(bnad, rcb, to_alloc);
	else
		bnad_rxq_refill_page(bnad, rcb, to_alloc);
}

#define flags_cksum_prot_mask (BNA_CQ_EF_IPV4 | BNA_CQ_EF_L3_CKSUM_OK | \
					BNA_CQ_EF_IPV6 | \
					BNA_CQ_EF_TCP | BNA_CQ_EF_UDP | \
					BNA_CQ_EF_L4_CKSUM_OK)

#define flags_tcp4 (BNA_CQ_EF_IPV4 | BNA_CQ_EF_L3_CKSUM_OK | \
				BNA_CQ_EF_TCP | BNA_CQ_EF_L4_CKSUM_OK)
#define flags_tcp6 (BNA_CQ_EF_IPV6 | \
				BNA_CQ_EF_TCP | BNA_CQ_EF_L4_CKSUM_OK)
#define flags_udp4 (BNA_CQ_EF_IPV4 | BNA_CQ_EF_L3_CKSUM_OK | \
				BNA_CQ_EF_UDP | BNA_CQ_EF_L4_CKSUM_OK)
#define flags_udp6 (BNA_CQ_EF_IPV6 | \
				BNA_CQ_EF_UDP | BNA_CQ_EF_L4_CKSUM_OK)

static void
bnad_cq_drop_packet(struct bnad *bnad, struct bna_rcb *rcb,
		    u32 sop_ci, u32 nvecs)
{
	struct bnad_rx_unmap_q *unmap_q;
	struct bnad_rx_unmap *unmap;
	u32 ci, vec;

	unmap_q = rcb->unmap_q;
	for (vec = 0, ci = sop_ci; vec < nvecs; vec++) {
		unmap = &unmap_q->unmap[ci];
		BNA_QE_INDX_INC(ci, rcb->q_depth);

		if (BNAD_RXBUF_IS_SK_BUFF(unmap_q->type))
			bnad_rxq_cleanup_skb(bnad, unmap);
		else
			bnad_rxq_cleanup_page(bnad, unmap);
	}
}

static void
bnad_cq_setup_skb_frags(struct bna_ccb *ccb, struct sk_buff *skb, u32 nvecs)
{
	struct bna_rcb *rcb;
	struct bnad *bnad;
	struct bnad_rx_unmap_q *unmap_q;
	struct bna_cq_entry *cq, *cmpl;
	u32 ci, pi, totlen = 0;

	cq = ccb->sw_q;
	pi = ccb->producer_index;
	cmpl = &cq[pi];

	rcb = bna_is_small_rxq(cmpl->rxq_id) ? ccb->rcb[1] : ccb->rcb[0];
	unmap_q = rcb->unmap_q;
	bnad = rcb->bnad;
	ci = rcb->consumer_index;

	/* prefetch header */
	prefetch(page_address(unmap_q->unmap[ci].page) +
		 unmap_q->unmap[ci].page_offset);

	while (nvecs--) {
		struct bnad_rx_unmap *unmap;
		u32 len;

		unmap = &unmap_q->unmap[ci];
		BNA_QE_INDX_INC(ci, rcb->q_depth);

		dma_unmap_page(&bnad->pcidev->dev,
			       dma_unmap_addr(&unmap->vector, dma_addr),
			       unmap->vector.len, DMA_FROM_DEVICE);

		len = ntohs(cmpl->length);
		skb->truesize += unmap->vector.len;
		totlen += len;

		skb_fill_page_desc(skb, skb_shinfo(skb)->nr_frags,
				   unmap->page, unmap->page_offset, len);

		unmap->page = NULL;
		unmap->vector.len = 0;

		BNA_QE_INDX_INC(pi, ccb->q_depth);
		cmpl = &cq[pi];
	}

	skb->len += totlen;
	skb->data_len += totlen;
}

static inline void
bnad_cq_setup_skb(struct bnad *bnad, struct sk_buff *skb,
		  struct bnad_rx_unmap *unmap, u32 len)
{
	prefetch(skb->data);

	dma_unmap_single(&bnad->pcidev->dev,
			dma_unmap_addr(&unmap->vector, dma_addr),
			unmap->vector.len, DMA_FROM_DEVICE);

	skb_put(skb, len);
	skb->protocol = eth_type_trans(skb, bnad->netdev);

	unmap->skb = NULL;
	unmap->vector.len = 0;
}

static u32
bnad_cq_process(struct bnad *bnad, struct bna_ccb *ccb, int budget)
{
	struct bna_cq_entry *cq, *cmpl, *next_cmpl;
	struct bna_rcb *rcb = NULL;
	struct bnad_rx_unmap_q *unmap_q;
	struct bnad_rx_unmap *unmap = NULL;
	struct sk_buff *skb = NULL;
	struct bna_pkt_rate *pkt_rt = &ccb->pkt_rate;
	struct bnad_rx_ctrl *rx_ctrl = ccb->ctrl;
	u32 packets = 0, len = 0, totlen = 0;
	u32 pi, vec, sop_ci = 0, nvecs = 0;
	u32 flags, masked_flags;

	prefetch(bnad->netdev);

	cq = ccb->sw_q;

	while (packets < budget) {
		cmpl = &cq[ccb->producer_index];
		if (!cmpl->valid)
			break;
		/* The 'valid' field is set by the adapter, only after writing
		 * the other fields of completion entry. Hence, do not load
		 * other fields of completion entry *before* the 'valid' is
		 * loaded. Adding the rmb() here prevents the compiler and/or
		 * CPU from reordering the reads which would potentially result
		 * in reading stale values in completion entry.
		 */
		rmb();

		BNA_UPDATE_PKT_CNT(pkt_rt, ntohs(cmpl->length));

		if (bna_is_small_rxq(cmpl->rxq_id))
			rcb = ccb->rcb[1];
		else
			rcb = ccb->rcb[0];

		unmap_q = rcb->unmap_q;

		/* start of packet ci */
		sop_ci = rcb->consumer_index;

		if (BNAD_RXBUF_IS_SK_BUFF(unmap_q->type)) {
			unmap = &unmap_q->unmap[sop_ci];
			skb = unmap->skb;
		} else {
			skb = napi_get_frags(&rx_ctrl->napi);
			if (unlikely(!skb))
				break;
		}
		prefetch(skb);

		flags = ntohl(cmpl->flags);
		len = ntohs(cmpl->length);
		totlen = len;
		nvecs = 1;

		/* Check all the completions for this frame.
		 * busy-wait doesn't help much, break here.
		 */
		if (BNAD_RXBUF_IS_MULTI_BUFF(unmap_q->type) &&
		    (flags & BNA_CQ_EF_EOP) == 0) {
			pi = ccb->producer_index;
			do {
				BNA_QE_INDX_INC(pi, ccb->q_depth);
				next_cmpl = &cq[pi];

				if (!next_cmpl->valid)
					break;
				/* The 'valid' field is set by the adapter, only
				 * after writing the other fields of completion
				 * entry. Hence, do not load other fields of
				 * completion entry *before* the 'valid' is
				 * loaded. Adding the rmb() here prevents the
				 * compiler and/or CPU from reordering the reads
				 * which would potentially result in reading
				 * stale values in completion entry.
				 */
				rmb();

				len = ntohs(next_cmpl->length);
				flags = ntohl(next_cmpl->flags);

				nvecs++;
				totlen += len;
			} while ((flags & BNA_CQ_EF_EOP) == 0);

			if (!next_cmpl->valid)
				break;
		}
		packets++;

		/* TODO: BNA_CQ_EF_LOCAL ? */
		if (unlikely(flags & (BNA_CQ_EF_MAC_ERROR |
						BNA_CQ_EF_FCS_ERROR |
						BNA_CQ_EF_TOO_LONG))) {
			bnad_cq_drop_packet(bnad, rcb, sop_ci, nvecs);
			rcb->rxq->rx_packets_with_error++;

			goto next;
		}

		if (BNAD_RXBUF_IS_SK_BUFF(unmap_q->type))
			bnad_cq_setup_skb(bnad, skb, unmap, len);
		else
			bnad_cq_setup_skb_frags(ccb, skb, nvecs);

		rcb->rxq->rx_packets++;
		rcb->rxq->rx_bytes += totlen;
		ccb->bytes_per_intr += totlen;

		masked_flags = flags & flags_cksum_prot_mask;

		if (likely
		    ((bnad->netdev->features & NETIF_F_RXCSUM) &&
		     ((masked_flags == flags_tcp4) ||
		      (masked_flags == flags_udp4) ||
		      (masked_flags == flags_tcp6) ||
		      (masked_flags == flags_udp6))))
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		else
			skb_checksum_none_assert(skb);

		if ((flags & BNA_CQ_EF_VLAN) &&
		    (bnad->netdev->features & NETIF_F_HW_VLAN_CTAG_RX))
			__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), ntohs(cmpl->vlan_tag));

		if (BNAD_RXBUF_IS_SK_BUFF(unmap_q->type))
			netif_receive_skb(skb);
		else
			napi_gro_frags(&rx_ctrl->napi);

next:
		BNA_QE_INDX_ADD(rcb->consumer_index, nvecs, rcb->q_depth);
		for (vec = 0; vec < nvecs; vec++) {
			cmpl = &cq[ccb->producer_index];
			cmpl->valid = 0;
			BNA_QE_INDX_INC(ccb->producer_index, ccb->q_depth);
		}
	}

	napi_gro_flush(&rx_ctrl->napi, false);
	if (likely(test_bit(BNAD_RXQ_STARTED, &ccb->rcb[0]->flags)))
		bna_ib_ack_disable_irq(ccb->i_dbell, packets);

	bnad_rxq_post(bnad, ccb->rcb[0]);
	if (ccb->rcb[1])
		bnad_rxq_post(bnad, ccb->rcb[1]);

	return packets;
}

static void
bnad_netif_rx_schedule_poll(struct bnad *bnad, struct bna_ccb *ccb)
{
	struct bnad_rx_ctrl *rx_ctrl = (struct bnad_rx_ctrl *)(ccb->ctrl);
	struct napi_struct *napi = &rx_ctrl->napi;

	if (likely(napi_schedule_prep(napi))) {
		__napi_schedule(napi);
		rx_ctrl->rx_schedule++;
	}
}

/* MSIX Rx Path Handler */
static irqreturn_t
bnad_msix_rx(int irq, void *data)
{
	struct bna_ccb *ccb = (struct bna_ccb *)data;

	if (ccb) {
		((struct bnad_rx_ctrl *)ccb->ctrl)->rx_intr_ctr++;
		bnad_netif_rx_schedule_poll(ccb->bnad, ccb);
	}

	return IRQ_HANDLED;
}

/* Interrupt handlers */

/* Mbox Interrupt Handlers */
static irqreturn_t
bnad_msix_mbox_handler(int irq, void *data)
{
	u32 intr_status;
	unsigned long flags;
	struct bnad *bnad = (struct bnad *)data;

	spin_lock_irqsave(&bnad->bna_lock, flags);
	if (unlikely(test_bit(BNAD_RF_MBOX_IRQ_DISABLED, &bnad->run_flags))) {
		spin_unlock_irqrestore(&bnad->bna_lock, flags);
		return IRQ_HANDLED;
	}

	bna_intr_status_get(&bnad->bna, intr_status);

	if (BNA_IS_MBOX_ERR_INTR(&bnad->bna, intr_status))
		bna_mbox_handler(&bnad->bna, intr_status);

	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	return IRQ_HANDLED;
}

static irqreturn_t
bnad_isr(int irq, void *data)
{
	int i, j;
	u32 intr_status;
	unsigned long flags;
	struct bnad *bnad = (struct bnad *)data;
	struct bnad_rx_info *rx_info;
	struct bnad_rx_ctrl *rx_ctrl;
	struct bna_tcb *tcb = NULL;

	spin_lock_irqsave(&bnad->bna_lock, flags);
	if (unlikely(test_bit(BNAD_RF_MBOX_IRQ_DISABLED, &bnad->run_flags))) {
		spin_unlock_irqrestore(&bnad->bna_lock, flags);
		return IRQ_NONE;
	}

	bna_intr_status_get(&bnad->bna, intr_status);

	if (unlikely(!intr_status)) {
		spin_unlock_irqrestore(&bnad->bna_lock, flags);
		return IRQ_NONE;
	}

	if (BNA_IS_MBOX_ERR_INTR(&bnad->bna, intr_status))
		bna_mbox_handler(&bnad->bna, intr_status);

	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	if (!BNA_IS_INTX_DATA_INTR(intr_status))
		return IRQ_HANDLED;

	/* Process data interrupts */
	/* Tx processing */
	for (i = 0; i < bnad->num_tx; i++) {
		for (j = 0; j < bnad->num_txq_per_tx; j++) {
			tcb = bnad->tx_info[i].tcb[j];
			if (tcb && test_bit(BNAD_TXQ_TX_STARTED, &tcb->flags))
				bnad_tx_complete(bnad, bnad->tx_info[i].tcb[j]);
		}
	}
	/* Rx processing */
	for (i = 0; i < bnad->num_rx; i++) {
		rx_info = &bnad->rx_info[i];
		if (!rx_info->rx)
			continue;
		for (j = 0; j < bnad->num_rxp_per_rx; j++) {
			rx_ctrl = &rx_info->rx_ctrl[j];
			if (rx_ctrl->ccb)
				bnad_netif_rx_schedule_poll(bnad,
							    rx_ctrl->ccb);
		}
	}
	return IRQ_HANDLED;
}

/*
 * Called in interrupt / callback context
 * with bna_lock held, so cfg_flags access is OK
 */
static void
bnad_enable_mbox_irq(struct bnad *bnad)
{
	clear_bit(BNAD_RF_MBOX_IRQ_DISABLED, &bnad->run_flags);

	BNAD_UPDATE_CTR(bnad, mbox_intr_enabled);
}

/*
 * Called with bnad->bna_lock held b'cos of
 * bnad->cfg_flags access.
 */
static void
bnad_disable_mbox_irq(struct bnad *bnad)
{
	set_bit(BNAD_RF_MBOX_IRQ_DISABLED, &bnad->run_flags);

	BNAD_UPDATE_CTR(bnad, mbox_intr_disabled);
}

static void
bnad_set_netdev_perm_addr(struct bnad *bnad)
{
	struct net_device *netdev = bnad->netdev;

	ether_addr_copy(netdev->perm_addr, bnad->perm_addr);
	if (is_zero_ether_addr(netdev->dev_addr))
		ether_addr_copy(netdev->dev_addr, bnad->perm_addr);
}

/* Control Path Handlers */

/* Callbacks */
void
bnad_cb_mbox_intr_enable(struct bnad *bnad)
{
	bnad_enable_mbox_irq(bnad);
}

void
bnad_cb_mbox_intr_disable(struct bnad *bnad)
{
	bnad_disable_mbox_irq(bnad);
}

void
bnad_cb_ioceth_ready(struct bnad *bnad)
{
	bnad->bnad_completions.ioc_comp_status = BNA_CB_SUCCESS;
	complete(&bnad->bnad_completions.ioc_comp);
}

void
bnad_cb_ioceth_failed(struct bnad *bnad)
{
	bnad->bnad_completions.ioc_comp_status = BNA_CB_FAIL;
	complete(&bnad->bnad_completions.ioc_comp);
}

void
bnad_cb_ioceth_disabled(struct bnad *bnad)
{
	bnad->bnad_completions.ioc_comp_status = BNA_CB_SUCCESS;
	complete(&bnad->bnad_completions.ioc_comp);
}

static void
bnad_cb_enet_disabled(void *arg)
{
	struct bnad *bnad = (struct bnad *)arg;

	netif_carrier_off(bnad->netdev);
	complete(&bnad->bnad_completions.enet_comp);
}

void
bnad_cb_ethport_link_status(struct bnad *bnad,
			enum bna_link_status link_status)
{
	bool link_up = false;

	link_up = (link_status == BNA_LINK_UP) || (link_status == BNA_CEE_UP);

	if (link_status == BNA_CEE_UP) {
		if (!test_bit(BNAD_RF_CEE_RUNNING, &bnad->run_flags))
			BNAD_UPDATE_CTR(bnad, cee_toggle);
		set_bit(BNAD_RF_CEE_RUNNING, &bnad->run_flags);
	} else {
		if (test_bit(BNAD_RF_CEE_RUNNING, &bnad->run_flags))
			BNAD_UPDATE_CTR(bnad, cee_toggle);
		clear_bit(BNAD_RF_CEE_RUNNING, &bnad->run_flags);
	}

	if (link_up) {
		if (!netif_carrier_ok(bnad->netdev)) {
			uint tx_id, tcb_id;
			netdev_info(bnad->netdev, "link up\n");
			netif_carrier_on(bnad->netdev);
			BNAD_UPDATE_CTR(bnad, link_toggle);
			for (tx_id = 0; tx_id < bnad->num_tx; tx_id++) {
				for (tcb_id = 0; tcb_id < bnad->num_txq_per_tx;
				      tcb_id++) {
					struct bna_tcb *tcb =
					bnad->tx_info[tx_id].tcb[tcb_id];
					u32 txq_id;
					if (!tcb)
						continue;

					txq_id = tcb->id;

					if (test_bit(BNAD_TXQ_TX_STARTED,
						     &tcb->flags)) {
						/*
						 * Force an immediate
						 * Transmit Schedule */
						netif_wake_subqueue(
								bnad->netdev,
								txq_id);
						BNAD_UPDATE_CTR(bnad,
							netif_queue_wakeup);
					} else {
						netif_stop_subqueue(
								bnad->netdev,
								txq_id);
						BNAD_UPDATE_CTR(bnad,
							netif_queue_stop);
					}
				}
			}
		}
	} else {
		if (netif_carrier_ok(bnad->netdev)) {
			netdev_info(bnad->netdev, "link down\n");
			netif_carrier_off(bnad->netdev);
			BNAD_UPDATE_CTR(bnad, link_toggle);
		}
	}
}

static void
bnad_cb_tx_disabled(void *arg, struct bna_tx *tx)
{
	struct bnad *bnad = (struct bnad *)arg;

	complete(&bnad->bnad_completions.tx_comp);
}

static void
bnad_cb_tcb_setup(struct bnad *bnad, struct bna_tcb *tcb)
{
	struct bnad_tx_info *tx_info =
			(struct bnad_tx_info *)tcb->txq->tx->priv;

	tcb->priv = tcb;
	tx_info->tcb[tcb->id] = tcb;
}

static void
bnad_cb_tcb_destroy(struct bnad *bnad, struct bna_tcb *tcb)
{
	struct bnad_tx_info *tx_info =
			(struct bnad_tx_info *)tcb->txq->tx->priv;

	tx_info->tcb[tcb->id] = NULL;
	tcb->priv = NULL;
}

static void
bnad_cb_ccb_setup(struct bnad *bnad, struct bna_ccb *ccb)
{
	struct bnad_rx_info *rx_info =
			(struct bnad_rx_info *)ccb->cq->rx->priv;

	rx_info->rx_ctrl[ccb->id].ccb = ccb;
	ccb->ctrl = &rx_info->rx_ctrl[ccb->id];
}

static void
bnad_cb_ccb_destroy(struct bnad *bnad, struct bna_ccb *ccb)
{
	struct bnad_rx_info *rx_info =
			(struct bnad_rx_info *)ccb->cq->rx->priv;

	rx_info->rx_ctrl[ccb->id].ccb = NULL;
}

static void
bnad_cb_tx_stall(struct bnad *bnad, struct bna_tx *tx)
{
	struct bnad_tx_info *tx_info =
			(struct bnad_tx_info *)tx->priv;
	struct bna_tcb *tcb;
	u32 txq_id;
	int i;

	for (i = 0; i < BNAD_MAX_TXQ_PER_TX; i++) {
		tcb = tx_info->tcb[i];
		if (!tcb)
			continue;
		txq_id = tcb->id;
		clear_bit(BNAD_TXQ_TX_STARTED, &tcb->flags);
		netif_stop_subqueue(bnad->netdev, txq_id);
	}
}

static void
bnad_cb_tx_resume(struct bnad *bnad, struct bna_tx *tx)
{
	struct bnad_tx_info *tx_info = (struct bnad_tx_info *)tx->priv;
	struct bna_tcb *tcb;
	u32 txq_id;
	int i;

	for (i = 0; i < BNAD_MAX_TXQ_PER_TX; i++) {
		tcb = tx_info->tcb[i];
		if (!tcb)
			continue;
		txq_id = tcb->id;

		BUG_ON(test_bit(BNAD_TXQ_TX_STARTED, &tcb->flags));
		set_bit(BNAD_TXQ_TX_STARTED, &tcb->flags);
		BUG_ON(*(tcb->hw_consumer_index) != 0);

		if (netif_carrier_ok(bnad->netdev)) {
			netif_wake_subqueue(bnad->netdev, txq_id);
			BNAD_UPDATE_CTR(bnad, netif_queue_wakeup);
		}
	}

	/*
	 * Workaround for first ioceth enable failure & we
	 * get a 0 MAC address. We try to get the MAC address
	 * again here.
	 */
	if (is_zero_ether_addr(bnad->perm_addr)) {
		bna_enet_perm_mac_get(&bnad->bna.enet, bnad->perm_addr);
		bnad_set_netdev_perm_addr(bnad);
	}
}

/*
 * Free all TxQs buffers and then notify TX_E_CLEANUP_DONE to Tx fsm.
 */
static void
bnad_tx_cleanup(struct delayed_work *work)
{
	struct bnad_tx_info *tx_info =
		container_of(work, struct bnad_tx_info, tx_cleanup_work);
	struct bnad *bnad = NULL;
	struct bna_tcb *tcb;
	unsigned long flags;
	u32 i, pending = 0;

	for (i = 0; i < BNAD_MAX_TXQ_PER_TX; i++) {
		tcb = tx_info->tcb[i];
		if (!tcb)
			continue;

		bnad = tcb->bnad;

		if (test_and_set_bit(BNAD_TXQ_FREE_SENT, &tcb->flags)) {
			pending++;
			continue;
		}

		bnad_txq_cleanup(bnad, tcb);

		smp_mb__before_atomic();
		clear_bit(BNAD_TXQ_FREE_SENT, &tcb->flags);
	}

	if (pending) {
		queue_delayed_work(bnad->work_q, &tx_info->tx_cleanup_work,
			msecs_to_jiffies(1));
		return;
	}

	spin_lock_irqsave(&bnad->bna_lock, flags);
	bna_tx_cleanup_complete(tx_info->tx);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);
}

static void
bnad_cb_tx_cleanup(struct bnad *bnad, struct bna_tx *tx)
{
	struct bnad_tx_info *tx_info = (struct bnad_tx_info *)tx->priv;
	struct bna_tcb *tcb;
	int i;

	for (i = 0; i < BNAD_MAX_TXQ_PER_TX; i++) {
		tcb = tx_info->tcb[i];
		if (!tcb)
			continue;
	}

	queue_delayed_work(bnad->work_q, &tx_info->tx_cleanup_work, 0);
}

static void
bnad_cb_rx_stall(struct bnad *bnad, struct bna_rx *rx)
{
	struct bnad_rx_info *rx_info = (struct bnad_rx_info *)rx->priv;
	struct bna_ccb *ccb;
	struct bnad_rx_ctrl *rx_ctrl;
	int i;

	for (i = 0; i < BNAD_MAX_RXP_PER_RX; i++) {
		rx_ctrl = &rx_info->rx_ctrl[i];
		ccb = rx_ctrl->ccb;
		if (!ccb)
			continue;

		clear_bit(BNAD_RXQ_POST_OK, &ccb->rcb[0]->flags);

		if (ccb->rcb[1])
			clear_bit(BNAD_RXQ_POST_OK, &ccb->rcb[1]->flags);
	}
}

/*
 * Free all RxQs buffers and then notify RX_E_CLEANUP_DONE to Rx fsm.
 */
static void
bnad_rx_cleanup(void *work)
{
	struct bnad_rx_info *rx_info =
		container_of(work, struct bnad_rx_info, rx_cleanup_work);
	struct bnad_rx_ctrl *rx_ctrl;
	struct bnad *bnad = NULL;
	unsigned long flags;
	u32 i;

	for (i = 0; i < BNAD_MAX_RXP_PER_RX; i++) {
		rx_ctrl = &rx_info->rx_ctrl[i];

		if (!rx_ctrl->ccb)
			continue;

		bnad = rx_ctrl->ccb->bnad;

		/*
		 * Wait till the poll handler has exited
		 * and nothing can be scheduled anymore
		 */
		napi_disable(&rx_ctrl->napi);

		bnad_cq_cleanup(bnad, rx_ctrl->ccb);
		bnad_rxq_cleanup(bnad, rx_ctrl->ccb->rcb[0]);
		if (rx_ctrl->ccb->rcb[1])
			bnad_rxq_cleanup(bnad, rx_ctrl->ccb->rcb[1]);
	}

	spin_lock_irqsave(&bnad->bna_lock, flags);
	bna_rx_cleanup_complete(rx_info->rx);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);
}

static void
bnad_cb_rx_cleanup(struct bnad *bnad, struct bna_rx *rx)
{
	struct bnad_rx_info *rx_info = (struct bnad_rx_info *)rx->priv;
	struct bna_ccb *ccb;
	struct bnad_rx_ctrl *rx_ctrl;
	int i;

	for (i = 0; i < BNAD_MAX_RXP_PER_RX; i++) {
		rx_ctrl = &rx_info->rx_ctrl[i];
		ccb = rx_ctrl->ccb;
		if (!ccb)
			continue;

		clear_bit(BNAD_RXQ_STARTED, &ccb->rcb[0]->flags);

		if (ccb->rcb[1])
			clear_bit(BNAD_RXQ_STARTED, &ccb->rcb[1]->flags);
	}

	queue_work(bnad->work_q, &rx_info->rx_cleanup_work);
}

static void
bnad_cb_rx_post(struct bnad *bnad, struct bna_rx *rx)
{
	struct bnad_rx_info *rx_info = (struct bnad_rx_info *)rx->priv;
	struct bna_ccb *ccb;
	struct bna_rcb *rcb;
	struct bnad_rx_ctrl *rx_ctrl;
	int i, j;

	for (i = 0; i < BNAD_MAX_RXP_PER_RX; i++) {
		rx_ctrl = &rx_info->rx_ctrl[i];
		ccb = rx_ctrl->ccb;
		if (!ccb)
			continue;

		napi_enable(&rx_ctrl->napi);

		for (j = 0; j < BNAD_MAX_RXQ_PER_RXP; j++) {
			rcb = ccb->rcb[j];
			if (!rcb)
				continue;

			bnad_rxq_alloc_init(bnad, rcb);
			set_bit(BNAD_RXQ_STARTED, &rcb->flags);
			set_bit(BNAD_RXQ_POST_OK, &rcb->flags);
			bnad_rxq_post(bnad, rcb);
		}
	}
}

static void
bnad_cb_rx_disabled(void *arg, struct bna_rx *rx)
{
	struct bnad *bnad = (struct bnad *)arg;

	complete(&bnad->bnad_completions.rx_comp);
}

static void
bnad_cb_rx_mcast_add(struct bnad *bnad, struct bna_rx *rx)
{
	bnad->bnad_completions.mcast_comp_status = BNA_CB_SUCCESS;
	complete(&bnad->bnad_completions.mcast_comp);
}

void
bnad_cb_stats_get(struct bnad *bnad, enum bna_cb_status status,
		       struct bna_stats *stats)
{
	if (status == BNA_CB_SUCCESS)
		BNAD_UPDATE_CTR(bnad, hw_stats_updates);

	if (!netif_running(bnad->netdev) ||
		!test_bit(BNAD_RF_STATS_TIMER_RUNNING, &bnad->run_flags))
		return;

	mod_timer(&bnad->stats_timer,
		  jiffies + msecs_to_jiffies(BNAD_STATS_TIMER_FREQ));
}

static void
bnad_cb_enet_mtu_set(struct bnad *bnad)
{
	bnad->bnad_completions.mtu_comp_status = BNA_CB_SUCCESS;
	complete(&bnad->bnad_completions.mtu_comp);
}

void
bnad_cb_completion(void *arg, enum bfa_status status)
{
	struct bnad_iocmd_comp *iocmd_comp =
			(struct bnad_iocmd_comp *)arg;

	iocmd_comp->comp_status = (u32) status;
	complete(&iocmd_comp->comp);
}

/* Resource allocation, free functions */

static void
bnad_mem_free(struct bnad *bnad,
	      struct bna_mem_info *mem_info)
{
	int i;
	dma_addr_t dma_pa;

	if (mem_info->mdl == NULL)
		return;

	for (i = 0; i < mem_info->num; i++) {
		if (mem_info->mdl[i].kva != NULL) {
			if (mem_info->mem_type == BNA_MEM_T_DMA) {
				BNA_GET_DMA_ADDR(&(mem_info->mdl[i].dma),
						dma_pa);
				dma_free_coherent(&bnad->pcidev->dev,
						  mem_info->mdl[i].len,
						  mem_info->mdl[i].kva, dma_pa);
			} else
				kfree(mem_info->mdl[i].kva);
		}
	}
	kfree(mem_info->mdl);
	mem_info->mdl = NULL;
}

static int
bnad_mem_alloc(struct bnad *bnad,
	       struct bna_mem_info *mem_info)
{
	int i;
	dma_addr_t dma_pa;

	if ((mem_info->num == 0) || (mem_info->len == 0)) {
		mem_info->mdl = NULL;
		return 0;
	}

	mem_info->mdl = kcalloc(mem_info->num, sizeof(struct bna_mem_descr),
				GFP_KERNEL);
	if (mem_info->mdl == NULL)
		return -ENOMEM;

	if (mem_info->mem_type == BNA_MEM_T_DMA) {
		for (i = 0; i < mem_info->num; i++) {
			mem_info->mdl[i].len = mem_info->len;
			mem_info->mdl[i].kva =
				dma_alloc_coherent(&bnad->pcidev->dev,
						   mem_info->len, &dma_pa,
						   GFP_KERNEL);
			if (mem_info->mdl[i].kva == NULL)
				goto err_return;

			BNA_SET_DMA_ADDR(dma_pa,
					 &(mem_info->mdl[i].dma));
		}
	} else {
		for (i = 0; i < mem_info->num; i++) {
			mem_info->mdl[i].len = mem_info->len;
			mem_info->mdl[i].kva = kzalloc(mem_info->len,
							GFP_KERNEL);
			if (mem_info->mdl[i].kva == NULL)
				goto err_return;
		}
	}

	return 0;

err_return:
	bnad_mem_free(bnad, mem_info);
	return -ENOMEM;
}

/* Free IRQ for Mailbox */
static void
bnad_mbox_irq_free(struct bnad *bnad)
{
	int irq;
	unsigned long flags;

	spin_lock_irqsave(&bnad->bna_lock, flags);
	bnad_disable_mbox_irq(bnad);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	irq = BNAD_GET_MBOX_IRQ(bnad);
	free_irq(irq, bnad);
}

/*
 * Allocates IRQ for Mailbox, but keep it disabled
 * This will be enabled once we get the mbox enable callback
 * from bna
 */
static int
bnad_mbox_irq_alloc(struct bnad *bnad)
{
	int		err = 0;
	unsigned long	irq_flags, flags;
	u32	irq;
	irq_handler_t	irq_handler;

	spin_lock_irqsave(&bnad->bna_lock, flags);
	if (bnad->cfg_flags & BNAD_CF_MSIX) {
		irq_handler = (irq_handler_t)bnad_msix_mbox_handler;
		irq = bnad->msix_table[BNAD_MAILBOX_MSIX_INDEX].vector;
		irq_flags = 0;
	} else {
		irq_handler = (irq_handler_t)bnad_isr;
		irq = bnad->pcidev->irq;
		irq_flags = IRQF_SHARED;
	}

	spin_unlock_irqrestore(&bnad->bna_lock, flags);
	sprintf(bnad->mbox_irq_name, "%s", BNAD_NAME);

	/*
	 * Set the Mbox IRQ disable flag, so that the IRQ handler
	 * called from request_irq() for SHARED IRQs do not execute
	 */
	set_bit(BNAD_RF_MBOX_IRQ_DISABLED, &bnad->run_flags);

	BNAD_UPDATE_CTR(bnad, mbox_intr_disabled);

	err = request_irq(irq, irq_handler, irq_flags,
			  bnad->mbox_irq_name, bnad);

	return err;
}

static void
bnad_txrx_irq_free(struct bnad *bnad, struct bna_intr_info *intr_info)
{
	kfree(intr_info->idl);
	intr_info->idl = NULL;
}

/* Allocates Interrupt Descriptor List for MSIX/INT-X vectors */
static int
bnad_txrx_irq_alloc(struct bnad *bnad, enum bnad_intr_source src,
		    u32 txrx_id, struct bna_intr_info *intr_info)
{
	int i, vector_start = 0;
	u32 cfg_flags;
	unsigned long flags;

	spin_lock_irqsave(&bnad->bna_lock, flags);
	cfg_flags = bnad->cfg_flags;
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	if (cfg_flags & BNAD_CF_MSIX) {
		intr_info->intr_type = BNA_INTR_T_MSIX;
		intr_info->idl = kcalloc(intr_info->num,
					sizeof(struct bna_intr_descr),
					GFP_KERNEL);
		if (!intr_info->idl)
			return -ENOMEM;

		switch (src) {
		case BNAD_INTR_TX:
			vector_start = BNAD_MAILBOX_MSIX_VECTORS + txrx_id;
			break;

		case BNAD_INTR_RX:
			vector_start = BNAD_MAILBOX_MSIX_VECTORS +
					(bnad->num_tx * bnad->num_txq_per_tx) +
					txrx_id;
			break;

		default:
			BUG();
		}

		for (i = 0; i < intr_info->num; i++)
			intr_info->idl[i].vector = vector_start + i;
	} else {
		intr_info->intr_type = BNA_INTR_T_INTX;
		intr_info->num = 1;
		intr_info->idl = kcalloc(intr_info->num,
					sizeof(struct bna_intr_descr),
					GFP_KERNEL);
		if (!intr_info->idl)
			return -ENOMEM;

		switch (src) {
		case BNAD_INTR_TX:
			intr_info->idl[0].vector = BNAD_INTX_TX_IB_BITMASK;
			break;

		case BNAD_INTR_RX:
			intr_info->idl[0].vector = BNAD_INTX_RX_IB_BITMASK;
			break;
		}
	}
	return 0;
}

/* NOTE: Should be called for MSIX only
 * Unregisters Tx MSIX vector(s) from the kernel
 */
static void
bnad_tx_msix_unregister(struct bnad *bnad, struct bnad_tx_info *tx_info,
			int num_txqs)
{
	int i;
	int vector_num;

	for (i = 0; i < num_txqs; i++) {
		if (tx_info->tcb[i] == NULL)
			continue;

		vector_num = tx_info->tcb[i]->intr_vector;
		free_irq(bnad->msix_table[vector_num].vector, tx_info->tcb[i]);
	}
}

/* NOTE: Should be called for MSIX only
 * Registers Tx MSIX vector(s) and ISR(s), cookie with the kernel
 */
static int
bnad_tx_msix_register(struct bnad *bnad, struct bnad_tx_info *tx_info,
			u32 tx_id, int num_txqs)
{
	int i;
	int err;
	int vector_num;

	for (i = 0; i < num_txqs; i++) {
		vector_num = tx_info->tcb[i]->intr_vector;
		sprintf(tx_info->tcb[i]->name, "%s TXQ %d", bnad->netdev->name,
				tx_id + tx_info->tcb[i]->id);
		err = request_irq(bnad->msix_table[vector_num].vector,
				  (irq_handler_t)bnad_msix_tx, 0,
				  tx_info->tcb[i]->name,
				  tx_info->tcb[i]);
		if (err)
			goto err_return;
	}

	return 0;

err_return:
	if (i > 0)
		bnad_tx_msix_unregister(bnad, tx_info, (i - 1));
	return -1;
}

/* NOTE: Should be called for MSIX only
 * Unregisters Rx MSIX vector(s) from the kernel
 */
static void
bnad_rx_msix_unregister(struct bnad *bnad, struct bnad_rx_info *rx_info,
			int num_rxps)
{
	int i;
	int vector_num;

	for (i = 0; i < num_rxps; i++) {
		if (rx_info->rx_ctrl[i].ccb == NULL)
			continue;

		vector_num = rx_info->rx_ctrl[i].ccb->intr_vector;
		free_irq(bnad->msix_table[vector_num].vector,
			 rx_info->rx_ctrl[i].ccb);
	}
}

/* NOTE: Should be called for MSIX only
 * Registers Tx MSIX vector(s) and ISR(s), cookie with the kernel
 */
static int
bnad_rx_msix_register(struct bnad *bnad, struct bnad_rx_info *rx_info,
			u32 rx_id, int num_rxps)
{
	int i;
	int err;
	int vector_num;

	for (i = 0; i < num_rxps; i++) {
		vector_num = rx_info->rx_ctrl[i].ccb->intr_vector;
		sprintf(rx_info->rx_ctrl[i].ccb->name, "%s CQ %d",
			bnad->netdev->name,
			rx_id + rx_info->rx_ctrl[i].ccb->id);
		err = request_irq(bnad->msix_table[vector_num].vector,
				  (irq_handler_t)bnad_msix_rx, 0,
				  rx_info->rx_ctrl[i].ccb->name,
				  rx_info->rx_ctrl[i].ccb);
		if (err)
			goto err_return;
	}

	return 0;

err_return:
	if (i > 0)
		bnad_rx_msix_unregister(bnad, rx_info, (i - 1));
	return -1;
}

/* Free Tx object Resources */
static void
bnad_tx_res_free(struct bnad *bnad, struct bna_res_info *res_info)
{
	int i;

	for (i = 0; i < BNA_TX_RES_T_MAX; i++) {
		if (res_info[i].res_type == BNA_RES_T_MEM)
			bnad_mem_free(bnad, &res_info[i].res_u.mem_info);
		else if (res_info[i].res_type == BNA_RES_T_INTR)
			bnad_txrx_irq_free(bnad, &res_info[i].res_u.intr_info);
	}
}

/* Allocates memory and interrupt resources for Tx object */
static int
bnad_tx_res_alloc(struct bnad *bnad, struct bna_res_info *res_info,
		  u32 tx_id)
{
	int i, err = 0;

	for (i = 0; i < BNA_TX_RES_T_MAX; i++) {
		if (res_info[i].res_type == BNA_RES_T_MEM)
			err = bnad_mem_alloc(bnad,
					&res_info[i].res_u.mem_info);
		else if (res_info[i].res_type == BNA_RES_T_INTR)
			err = bnad_txrx_irq_alloc(bnad, BNAD_INTR_TX, tx_id,
					&res_info[i].res_u.intr_info);
		if (err)
			goto err_return;
	}
	return 0;

err_return:
	bnad_tx_res_free(bnad, res_info);
	return err;
}

/* Free Rx object Resources */
static void
bnad_rx_res_free(struct bnad *bnad, struct bna_res_info *res_info)
{
	int i;

	for (i = 0; i < BNA_RX_RES_T_MAX; i++) {
		if (res_info[i].res_type == BNA_RES_T_MEM)
			bnad_mem_free(bnad, &res_info[i].res_u.mem_info);
		else if (res_info[i].res_type == BNA_RES_T_INTR)
			bnad_txrx_irq_free(bnad, &res_info[i].res_u.intr_info);
	}
}

/* Allocates memory and interrupt resources for Rx object */
static int
bnad_rx_res_alloc(struct bnad *bnad, struct bna_res_info *res_info,
		  uint rx_id)
{
	int i, err = 0;

	/* All memory needs to be allocated before setup_ccbs */
	for (i = 0; i < BNA_RX_RES_T_MAX; i++) {
		if (res_info[i].res_type == BNA_RES_T_MEM)
			err = bnad_mem_alloc(bnad,
					&res_info[i].res_u.mem_info);
		else if (res_info[i].res_type == BNA_RES_T_INTR)
			err = bnad_txrx_irq_alloc(bnad, BNAD_INTR_RX, rx_id,
					&res_info[i].res_u.intr_info);
		if (err)
			goto err_return;
	}
	return 0;

err_return:
	bnad_rx_res_free(bnad, res_info);
	return err;
}

/* Timer callbacks */
/* a) IOC timer */
static void
bnad_ioc_timeout(struct timer_list *t)
{
	struct bnad *bnad = from_timer(bnad, t, bna.ioceth.ioc.ioc_timer);
	unsigned long flags;

	spin_lock_irqsave(&bnad->bna_lock, flags);
	bfa_nw_ioc_timeout(&bnad->bna.ioceth.ioc);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);
}

static void
bnad_ioc_hb_check(struct timer_list *t)
{
	struct bnad *bnad = from_timer(bnad, t, bna.ioceth.ioc.hb_timer);
	unsigned long flags;

	spin_lock_irqsave(&bnad->bna_lock, flags);
	bfa_nw_ioc_hb_check(&bnad->bna.ioceth.ioc);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);
}

static void
bnad_iocpf_timeout(struct timer_list *t)
{
	struct bnad *bnad = from_timer(bnad, t, bna.ioceth.ioc.iocpf_timer);
	unsigned long flags;

	spin_lock_irqsave(&bnad->bna_lock, flags);
	bfa_nw_iocpf_timeout(&bnad->bna.ioceth.ioc);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);
}

static void
bnad_iocpf_sem_timeout(struct timer_list *t)
{
	struct bnad *bnad = from_timer(bnad, t, bna.ioceth.ioc.sem_timer);
	unsigned long flags;

	spin_lock_irqsave(&bnad->bna_lock, flags);
	bfa_nw_iocpf_sem_timeout(&bnad->bna.ioceth.ioc);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);
}

/*
 * All timer routines use bnad->bna_lock to protect against
 * the following race, which may occur in case of no locking:
 *	Time	CPU m	CPU n
 *	0       1 = test_bit
 *	1			clear_bit
 *	2			del_timer_sync
 *	3	mod_timer
 */

/* b) Dynamic Interrupt Moderation Timer */
static void
bnad_dim_timeout(struct timer_list *t)
{
	struct bnad *bnad = from_timer(bnad, t, dim_timer);
	struct bnad_rx_info *rx_info;
	struct bnad_rx_ctrl *rx_ctrl;
	int i, j;
	unsigned long flags;

	if (!netif_carrier_ok(bnad->netdev))
		return;

	spin_lock_irqsave(&bnad->bna_lock, flags);
	for (i = 0; i < bnad->num_rx; i++) {
		rx_info = &bnad->rx_info[i];
		if (!rx_info->rx)
			continue;
		for (j = 0; j < bnad->num_rxp_per_rx; j++) {
			rx_ctrl = &rx_info->rx_ctrl[j];
			if (!rx_ctrl->ccb)
				continue;
			bna_rx_dim_update(rx_ctrl->ccb);
		}
	}

	/* Check for BNAD_CF_DIM_ENABLED, does not eleminate a race */
	if (test_bit(BNAD_RF_DIM_TIMER_RUNNING, &bnad->run_flags))
		mod_timer(&bnad->dim_timer,
			  jiffies + msecs_to_jiffies(BNAD_DIM_TIMER_FREQ));
	spin_unlock_irqrestore(&bnad->bna_lock, flags);
}

/* c)  Statistics Timer */
static void
bnad_stats_timeout(struct timer_list *t)
{
	struct bnad *bnad = from_timer(bnad, t, stats_timer);
	unsigned long flags;

	if (!netif_running(bnad->netdev) ||
		!test_bit(BNAD_RF_STATS_TIMER_RUNNING, &bnad->run_flags))
		return;

	spin_lock_irqsave(&bnad->bna_lock, flags);
	bna_hw_stats_get(&bnad->bna);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);
}

/*
 * Set up timer for DIM
 * Called with bnad->bna_lock held
 */
void
bnad_dim_timer_start(struct bnad *bnad)
{
	if (bnad->cfg_flags & BNAD_CF_DIM_ENABLED &&
	    !test_bit(BNAD_RF_DIM_TIMER_RUNNING, &bnad->run_flags)) {
		timer_setup(&bnad->dim_timer, bnad_dim_timeout, 0);
		set_bit(BNAD_RF_DIM_TIMER_RUNNING, &bnad->run_flags);
		mod_timer(&bnad->dim_timer,
			  jiffies + msecs_to_jiffies(BNAD_DIM_TIMER_FREQ));
	}
}

/*
 * Set up timer for statistics
 * Called with mutex_lock(&bnad->conf_mutex) held
 */
static void
bnad_stats_timer_start(struct bnad *bnad)
{
	unsigned long flags;

	spin_lock_irqsave(&bnad->bna_lock, flags);
	if (!test_and_set_bit(BNAD_RF_STATS_TIMER_RUNNING, &bnad->run_flags)) {
		timer_setup(&bnad->stats_timer, bnad_stats_timeout, 0);
		mod_timer(&bnad->stats_timer,
			  jiffies + msecs_to_jiffies(BNAD_STATS_TIMER_FREQ));
	}
	spin_unlock_irqrestore(&bnad->bna_lock, flags);
}

/*
 * Stops the stats timer
 * Called with mutex_lock(&bnad->conf_mutex) held
 */
static void
bnad_stats_timer_stop(struct bnad *bnad)
{
	int to_del = 0;
	unsigned long flags;

	spin_lock_irqsave(&bnad->bna_lock, flags);
	if (test_and_clear_bit(BNAD_RF_STATS_TIMER_RUNNING, &bnad->run_flags))
		to_del = 1;
	spin_unlock_irqrestore(&bnad->bna_lock, flags);
	if (to_del)
		del_timer_sync(&bnad->stats_timer);
}

/* Utilities */

static void
bnad_netdev_mc_list_get(struct net_device *netdev, u8 *mc_list)
{
	int i = 1; /* Index 0 has broadcast address */
	struct netdev_hw_addr *mc_addr;

	netdev_for_each_mc_addr(mc_addr, netdev) {
		ether_addr_copy(&mc_list[i * ETH_ALEN], &mc_addr->addr[0]);
		i++;
	}
}

static int
bnad_napi_poll_rx(struct napi_struct *napi, int budget)
{
	struct bnad_rx_ctrl *rx_ctrl =
		container_of(napi, struct bnad_rx_ctrl, napi);
	struct bnad *bnad = rx_ctrl->bnad;
	int rcvd = 0;

	rx_ctrl->rx_poll_ctr++;

	if (!netif_carrier_ok(bnad->netdev))
		goto poll_exit;

	rcvd = bnad_cq_process(bnad, rx_ctrl->ccb, budget);
	if (rcvd >= budget)
		return rcvd;

poll_exit:
	napi_complete_done(napi, rcvd);

	rx_ctrl->rx_complete++;

	if (rx_ctrl->ccb)
		bnad_enable_rx_irq_unsafe(rx_ctrl->ccb);

	return rcvd;
}

#define BNAD_NAPI_POLL_QUOTA		64
static void
bnad_napi_add(struct bnad *bnad, u32 rx_id)
{
	struct bnad_rx_ctrl *rx_ctrl;
	int i;

	/* Initialize & enable NAPI */
	for (i = 0; i <	bnad->num_rxp_per_rx; i++) {
		rx_ctrl = &bnad->rx_info[rx_id].rx_ctrl[i];
		netif_napi_add(bnad->netdev, &rx_ctrl->napi,
			       bnad_napi_poll_rx, BNAD_NAPI_POLL_QUOTA);
	}
}

static void
bnad_napi_delete(struct bnad *bnad, u32 rx_id)
{
	int i;

	/* First disable and then clean up */
	for (i = 0; i < bnad->num_rxp_per_rx; i++)
		netif_napi_del(&bnad->rx_info[rx_id].rx_ctrl[i].napi);
}

/* Should be held with conf_lock held */
void
bnad_destroy_tx(struct bnad *bnad, u32 tx_id)
{
	struct bnad_tx_info *tx_info = &bnad->tx_info[tx_id];
	struct bna_res_info *res_info = &bnad->tx_res_info[tx_id].res_info[0];
	unsigned long flags;

	if (!tx_info->tx)
		return;

	init_completion(&bnad->bnad_completions.tx_comp);
	spin_lock_irqsave(&bnad->bna_lock, flags);
	bna_tx_disable(tx_info->tx, BNA_HARD_CLEANUP, bnad_cb_tx_disabled);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);
	wait_for_completion(&bnad->bnad_completions.tx_comp);

	if (tx_info->tcb[0]->intr_type == BNA_INTR_T_MSIX)
		bnad_tx_msix_unregister(bnad, tx_info,
			bnad->num_txq_per_tx);

	spin_lock_irqsave(&bnad->bna_lock, flags);
	bna_tx_destroy(tx_info->tx);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	tx_info->tx = NULL;
	tx_info->tx_id = 0;

	bnad_tx_res_free(bnad, res_info);
}

/* Should be held with conf_lock held */
int
bnad_setup_tx(struct bnad *bnad, u32 tx_id)
{
	int err;
	struct bnad_tx_info *tx_info = &bnad->tx_info[tx_id];
	struct bna_res_info *res_info = &bnad->tx_res_info[tx_id].res_info[0];
	struct bna_intr_info *intr_info =
			&res_info[BNA_TX_RES_INTR_T_TXCMPL].res_u.intr_info;
	struct bna_tx_config *tx_config = &bnad->tx_config[tx_id];
	static const struct bna_tx_event_cbfn tx_cbfn = {
		.tcb_setup_cbfn = bnad_cb_tcb_setup,
		.tcb_destroy_cbfn = bnad_cb_tcb_destroy,
		.tx_stall_cbfn = bnad_cb_tx_stall,
		.tx_resume_cbfn = bnad_cb_tx_resume,
		.tx_cleanup_cbfn = bnad_cb_tx_cleanup,
	};

	struct bna_tx *tx;
	unsigned long flags;

	tx_info->tx_id = tx_id;

	/* Initialize the Tx object configuration */
	tx_config->num_txq = bnad->num_txq_per_tx;
	tx_config->txq_depth = bnad->txq_depth;
	tx_config->tx_type = BNA_TX_T_REGULAR;
	tx_config->coalescing_timeo = bnad->tx_coalescing_timeo;

	/* Get BNA's resource requirement for one tx object */
	spin_lock_irqsave(&bnad->bna_lock, flags);
	bna_tx_res_req(bnad->num_txq_per_tx,
		bnad->txq_depth, res_info);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	/* Fill Unmap Q memory requirements */
	BNAD_FILL_UNMAPQ_MEM_REQ(&res_info[BNA_TX_RES_MEM_T_UNMAPQ],
			bnad->num_txq_per_tx, (sizeof(struct bnad_tx_unmap) *
			bnad->txq_depth));

	/* Allocate resources */
	err = bnad_tx_res_alloc(bnad, res_info, tx_id);
	if (err)
		return err;

	/* Ask BNA to create one Tx object, supplying required resources */
	spin_lock_irqsave(&bnad->bna_lock, flags);
	tx = bna_tx_create(&bnad->bna, bnad, tx_config, &tx_cbfn, res_info,
			tx_info);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);
	if (!tx) {
		err = -ENOMEM;
		goto err_return;
	}
	tx_info->tx = tx;

	INIT_DELAYED_WORK(&tx_info->tx_cleanup_work,
			(work_func_t)bnad_tx_cleanup);

	/* Register ISR for the Tx object */
	if (intr_info->intr_type == BNA_INTR_T_MSIX) {
		err = bnad_tx_msix_register(bnad, tx_info,
			tx_id, bnad->num_txq_per_tx);
		if (err)
			goto cleanup_tx;
	}

	spin_lock_irqsave(&bnad->bna_lock, flags);
	bna_tx_enable(tx);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	return 0;

cleanup_tx:
	spin_lock_irqsave(&bnad->bna_lock, flags);
	bna_tx_destroy(tx_info->tx);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);
	tx_info->tx = NULL;
	tx_info->tx_id = 0;
err_return:
	bnad_tx_res_free(bnad, res_info);
	return err;
}

/* Setup the rx config for bna_rx_create */
/* bnad decides the configuration */
static void
bnad_init_rx_config(struct bnad *bnad, struct bna_rx_config *rx_config)
{
	memset(rx_config, 0, sizeof(*rx_config));
	rx_config->rx_type = BNA_RX_T_REGULAR;
	rx_config->num_paths = bnad->num_rxp_per_rx;
	rx_config->coalescing_timeo = bnad->rx_coalescing_timeo;

	if (bnad->num_rxp_per_rx > 1) {
		rx_config->rss_status = BNA_STATUS_T_ENABLED;
		rx_config->rss_config.hash_type =
				(BFI_ENET_RSS_IPV6 |
				 BFI_ENET_RSS_IPV6_TCP |
				 BFI_ENET_RSS_IPV4 |
				 BFI_ENET_RSS_IPV4_TCP);
		rx_config->rss_config.hash_mask =
				bnad->num_rxp_per_rx - 1;
		netdev_rss_key_fill(rx_config->rss_config.toeplitz_hash_key,
			sizeof(rx_config->rss_config.toeplitz_hash_key));
	} else {
		rx_config->rss_status = BNA_STATUS_T_DISABLED;
		memset(&rx_config->rss_config, 0,
		       sizeof(rx_config->rss_config));
	}

	rx_config->frame_size = BNAD_FRAME_SIZE(bnad->netdev->mtu);
	rx_config->q0_multi_buf = BNA_STATUS_T_DISABLED;

	/* BNA_RXP_SINGLE - one data-buffer queue
	 * BNA_RXP_SLR - one small-buffer and one large-buffer queues
	 * BNA_RXP_HDS - one header-buffer and one data-buffer queues
	 */
	/* TODO: configurable param for queue type */
	rx_config->rxp_type = BNA_RXP_SLR;

	if (BNAD_PCI_DEV_IS_CAT2(bnad) &&
	    rx_config->frame_size > 4096) {
		/* though size_routing_enable is set in SLR,
		 * small packets may get routed to same rxq.
		 * set buf_size to 2048 instead of PAGE_SIZE.
		 */
		rx_config->q0_buf_size = 2048;
		/* this should be in multiples of 2 */
		rx_config->q0_num_vecs = 4;
		rx_config->q0_depth = bnad->rxq_depth * rx_config->q0_num_vecs;
		rx_config->q0_multi_buf = BNA_STATUS_T_ENABLED;
	} else {
		rx_config->q0_buf_size = rx_config->frame_size;
		rx_config->q0_num_vecs = 1;
		rx_config->q0_depth = bnad->rxq_depth;
	}

	/* initialize for q1 for BNA_RXP_SLR/BNA_RXP_HDS */
	if (rx_config->rxp_type == BNA_RXP_SLR) {
		rx_config->q1_depth = bnad->rxq_depth;
		rx_config->q1_buf_size = BFI_SMALL_RXBUF_SIZE;
	}

	rx_config->vlan_strip_status =
		(bnad->netdev->features & NETIF_F_HW_VLAN_CTAG_RX) ?
		BNA_STATUS_T_ENABLED : BNA_STATUS_T_DISABLED;
}

static void
bnad_rx_ctrl_init(struct bnad *bnad, u32 rx_id)
{
	struct bnad_rx_info *rx_info = &bnad->rx_info[rx_id];
	int i;

	for (i = 0; i < bnad->num_rxp_per_rx; i++)
		rx_info->rx_ctrl[i].bnad = bnad;
}

/* Called with mutex_lock(&bnad->conf_mutex) held */
static u32
bnad_reinit_rx(struct bnad *bnad)
{
	struct net_device *netdev = bnad->netdev;
	u32 err = 0, current_err = 0;
	u32 rx_id = 0, count = 0;
	unsigned long flags;

	/* destroy and create new rx objects */
	for (rx_id = 0; rx_id < bnad->num_rx; rx_id++) {
		if (!bnad->rx_info[rx_id].rx)
			continue;
		bnad_destroy_rx(bnad, rx_id);
	}

	spin_lock_irqsave(&bnad->bna_lock, flags);
	bna_enet_mtu_set(&bnad->bna.enet,
			 BNAD_FRAME_SIZE(bnad->netdev->mtu), NULL);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	for (rx_id = 0; rx_id < bnad->num_rx; rx_id++) {
		count++;
		current_err = bnad_setup_rx(bnad, rx_id);
		if (current_err && !err) {
			err = current_err;
			netdev_err(netdev, "RXQ:%u setup failed\n", rx_id);
		}
	}

	/* restore rx configuration */
	if (bnad->rx_info[0].rx && !err) {
		bnad_restore_vlans(bnad, 0);
		bnad_enable_default_bcast(bnad);
		spin_lock_irqsave(&bnad->bna_lock, flags);
		bnad_mac_addr_set_locked(bnad, netdev->dev_addr);
		spin_unlock_irqrestore(&bnad->bna_lock, flags);
		bnad_set_rx_mode(netdev);
	}

	return count;
}

/* Called with bnad_conf_lock() held */
void
bnad_destroy_rx(struct bnad *bnad, u32 rx_id)
{
	struct bnad_rx_info *rx_info = &bnad->rx_info[rx_id];
	struct bna_rx_config *rx_config = &bnad->rx_config[rx_id];
	struct bna_res_info *res_info = &bnad->rx_res_info[rx_id].res_info[0];
	unsigned long flags;
	int to_del = 0;

	if (!rx_info->rx)
		return;

	if (0 == rx_id) {
		spin_lock_irqsave(&bnad->bna_lock, flags);
		if (bnad->cfg_flags & BNAD_CF_DIM_ENABLED &&
		    test_bit(BNAD_RF_DIM_TIMER_RUNNING, &bnad->run_flags)) {
			clear_bit(BNAD_RF_DIM_TIMER_RUNNING, &bnad->run_flags);
			to_del = 1;
		}
		spin_unlock_irqrestore(&bnad->bna_lock, flags);
		if (to_del)
			del_timer_sync(&bnad->dim_timer);
	}

	init_completion(&bnad->bnad_completions.rx_comp);
	spin_lock_irqsave(&bnad->bna_lock, flags);
	bna_rx_disable(rx_info->rx, BNA_HARD_CLEANUP, bnad_cb_rx_disabled);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);
	wait_for_completion(&bnad->bnad_completions.rx_comp);

	if (rx_info->rx_ctrl[0].ccb->intr_type == BNA_INTR_T_MSIX)
		bnad_rx_msix_unregister(bnad, rx_info, rx_config->num_paths);

	bnad_napi_delete(bnad, rx_id);

	spin_lock_irqsave(&bnad->bna_lock, flags);
	bna_rx_destroy(rx_info->rx);

	rx_info->rx = NULL;
	rx_info->rx_id = 0;
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	bnad_rx_res_free(bnad, res_info);
}

/* Called with mutex_lock(&bnad->conf_mutex) held */
int
bnad_setup_rx(struct bnad *bnad, u32 rx_id)
{
	int err;
	struct bnad_rx_info *rx_info = &bnad->rx_info[rx_id];
	struct bna_res_info *res_info = &bnad->rx_res_info[rx_id].res_info[0];
	struct bna_intr_info *intr_info =
			&res_info[BNA_RX_RES_T_INTR].res_u.intr_info;
	struct bna_rx_config *rx_config = &bnad->rx_config[rx_id];
	static const struct bna_rx_event_cbfn rx_cbfn = {
		.rcb_setup_cbfn = NULL,
		.rcb_destroy_cbfn = NULL,
		.ccb_setup_cbfn = bnad_cb_ccb_setup,
		.ccb_destroy_cbfn = bnad_cb_ccb_destroy,
		.rx_stall_cbfn = bnad_cb_rx_stall,
		.rx_cleanup_cbfn = bnad_cb_rx_cleanup,
		.rx_post_cbfn = bnad_cb_rx_post,
	};
	struct bna_rx *rx;
	unsigned long flags;

	rx_info->rx_id = rx_id;

	/* Initialize the Rx object configuration */
	bnad_init_rx_config(bnad, rx_config);

	/* Get BNA's resource requirement for one Rx object */
	spin_lock_irqsave(&bnad->bna_lock, flags);
	bna_rx_res_req(rx_config, res_info);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	/* Fill Unmap Q memory requirements */
	BNAD_FILL_UNMAPQ_MEM_REQ(&res_info[BNA_RX_RES_MEM_T_UNMAPDQ],
				 rx_config->num_paths,
			(rx_config->q0_depth *
			 sizeof(struct bnad_rx_unmap)) +
			 sizeof(struct bnad_rx_unmap_q));

	if (rx_config->rxp_type != BNA_RXP_SINGLE) {
		BNAD_FILL_UNMAPQ_MEM_REQ(&res_info[BNA_RX_RES_MEM_T_UNMAPHQ],
					 rx_config->num_paths,
				(rx_config->q1_depth *
				 sizeof(struct bnad_rx_unmap) +
				 sizeof(struct bnad_rx_unmap_q)));
	}
	/* Allocate resource */
	err = bnad_rx_res_alloc(bnad, res_info, rx_id);
	if (err)
		return err;

	bnad_rx_ctrl_init(bnad, rx_id);

	/* Ask BNA to create one Rx object, supplying required resources */
	spin_lock_irqsave(&bnad->bna_lock, flags);
	rx = bna_rx_create(&bnad->bna, bnad, rx_config, &rx_cbfn, res_info,
			rx_info);
	if (!rx) {
		err = -ENOMEM;
		spin_unlock_irqrestore(&bnad->bna_lock, flags);
		goto err_return;
	}
	rx_info->rx = rx;
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	INIT_WORK(&rx_info->rx_cleanup_work,
			(work_func_t)(bnad_rx_cleanup));

	/*
	 * Init NAPI, so that state is set to NAPI_STATE_SCHED,
	 * so that IRQ handler cannot schedule NAPI at this point.
	 */
	bnad_napi_add(bnad, rx_id);

	/* Register ISR for the Rx object */
	if (intr_info->intr_type == BNA_INTR_T_MSIX) {
		err = bnad_rx_msix_register(bnad, rx_info, rx_id,
						rx_config->num_paths);
		if (err)
			goto err_return;
	}

	spin_lock_irqsave(&bnad->bna_lock, flags);
	if (0 == rx_id) {
		/* Set up Dynamic Interrupt Moderation Vector */
		if (bnad->cfg_flags & BNAD_CF_DIM_ENABLED)
			bna_rx_dim_reconfig(&bnad->bna, bna_napi_dim_vector);

		/* Enable VLAN filtering only on the default Rx */
		bna_rx_vlanfilter_enable(rx);

		/* Start the DIM timer */
		bnad_dim_timer_start(bnad);
	}

	bna_rx_enable(rx);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	return 0;

err_return:
	bnad_destroy_rx(bnad, rx_id);
	return err;
}

/* Called with conf_lock & bnad->bna_lock held */
void
bnad_tx_coalescing_timeo_set(struct bnad *bnad)
{
	struct bnad_tx_info *tx_info;

	tx_info = &bnad->tx_info[0];
	if (!tx_info->tx)
		return;

	bna_tx_coalescing_timeo_set(tx_info->tx, bnad->tx_coalescing_timeo);
}

/* Called with conf_lock & bnad->bna_lock held */
void
bnad_rx_coalescing_timeo_set(struct bnad *bnad)
{
	struct bnad_rx_info *rx_info;
	int	i;

	for (i = 0; i < bnad->num_rx; i++) {
		rx_info = &bnad->rx_info[i];
		if (!rx_info->rx)
			continue;
		bna_rx_coalescing_timeo_set(rx_info->rx,
				bnad->rx_coalescing_timeo);
	}
}

/*
 * Called with bnad->bna_lock held
 */
int
bnad_mac_addr_set_locked(struct bnad *bnad, const u8 *mac_addr)
{
	int ret;

	if (!is_valid_ether_addr(mac_addr))
		return -EADDRNOTAVAIL;

	/* If datapath is down, pretend everything went through */
	if (!bnad->rx_info[0].rx)
		return 0;

	ret = bna_rx_ucast_set(bnad->rx_info[0].rx, mac_addr);
	if (ret != BNA_CB_SUCCESS)
		return -EADDRNOTAVAIL;

	return 0;
}

/* Should be called with conf_lock held */
int
bnad_enable_default_bcast(struct bnad *bnad)
{
	struct bnad_rx_info *rx_info = &bnad->rx_info[0];
	int ret;
	unsigned long flags;

	init_completion(&bnad->bnad_completions.mcast_comp);

	spin_lock_irqsave(&bnad->bna_lock, flags);
	ret = bna_rx_mcast_add(rx_info->rx, bnad_bcast_addr,
			       bnad_cb_rx_mcast_add);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	if (ret == BNA_CB_SUCCESS)
		wait_for_completion(&bnad->bnad_completions.mcast_comp);
	else
		return -ENODEV;

	if (bnad->bnad_completions.mcast_comp_status != BNA_CB_SUCCESS)
		return -ENODEV;

	return 0;
}

/* Called with mutex_lock(&bnad->conf_mutex) held */
void
bnad_restore_vlans(struct bnad *bnad, u32 rx_id)
{
	u16 vid;
	unsigned long flags;

	for_each_set_bit(vid, bnad->active_vlans, VLAN_N_VID) {
		spin_lock_irqsave(&bnad->bna_lock, flags);
		bna_rx_vlan_add(bnad->rx_info[rx_id].rx, vid);
		spin_unlock_irqrestore(&bnad->bna_lock, flags);
	}
}

/* Statistics utilities */
void
bnad_netdev_qstats_fill(struct bnad *bnad, struct rtnl_link_stats64 *stats)
{
	int i, j;

	for (i = 0; i < bnad->num_rx; i++) {
		for (j = 0; j < bnad->num_rxp_per_rx; j++) {
			if (bnad->rx_info[i].rx_ctrl[j].ccb) {
				stats->rx_packets += bnad->rx_info[i].
				rx_ctrl[j].ccb->rcb[0]->rxq->rx_packets;
				stats->rx_bytes += bnad->rx_info[i].
					rx_ctrl[j].ccb->rcb[0]->rxq->rx_bytes;
				if (bnad->rx_info[i].rx_ctrl[j].ccb->rcb[1] &&
					bnad->rx_info[i].rx_ctrl[j].ccb->
					rcb[1]->rxq) {
					stats->rx_packets +=
						bnad->rx_info[i].rx_ctrl[j].
						ccb->rcb[1]->rxq->rx_packets;
					stats->rx_bytes +=
						bnad->rx_info[i].rx_ctrl[j].
						ccb->rcb[1]->rxq->rx_bytes;
				}
			}
		}
	}
	for (i = 0; i < bnad->num_tx; i++) {
		for (j = 0; j < bnad->num_txq_per_tx; j++) {
			if (bnad->tx_info[i].tcb[j]) {
				stats->tx_packets +=
				bnad->tx_info[i].tcb[j]->txq->tx_packets;
				stats->tx_bytes +=
					bnad->tx_info[i].tcb[j]->txq->tx_bytes;
			}
		}
	}
}

/*
 * Must be called with the bna_lock held.
 */
void
bnad_netdev_hwstats_fill(struct bnad *bnad, struct rtnl_link_stats64 *stats)
{
	struct bfi_enet_stats_mac *mac_stats;
	u32 bmap;
	int i;

	mac_stats = &bnad->stats.bna_stats->hw_stats.mac_stats;
	stats->rx_errors =
		mac_stats->rx_fcs_error + mac_stats->rx_alignment_error +
		mac_stats->rx_frame_length_error + mac_stats->rx_code_error +
		mac_stats->rx_undersize;
	stats->tx_errors = mac_stats->tx_fcs_error +
					mac_stats->tx_undersize;
	stats->rx_dropped = mac_stats->rx_drop;
	stats->tx_dropped = mac_stats->tx_drop;
	stats->multicast = mac_stats->rx_multicast;
	stats->collisions = mac_stats->tx_total_collision;

	stats->rx_length_errors = mac_stats->rx_frame_length_error;

	/* receive ring buffer overflow  ?? */

	stats->rx_crc_errors = mac_stats->rx_fcs_error;
	stats->rx_frame_errors = mac_stats->rx_alignment_error;
	/* recv'r fifo overrun */
	bmap = bna_rx_rid_mask(&bnad->bna);
	for (i = 0; bmap; i++) {
		if (bmap & 1) {
			stats->rx_fifo_errors +=
				bnad->stats.bna_stats->
					hw_stats.rxf_stats[i].frame_drops;
			break;
		}
		bmap >>= 1;
	}
}

static void
bnad_mbox_irq_sync(struct bnad *bnad)
{
	u32 irq;
	unsigned long flags;

	spin_lock_irqsave(&bnad->bna_lock, flags);
	if (bnad->cfg_flags & BNAD_CF_MSIX)
		irq = bnad->msix_table[BNAD_MAILBOX_MSIX_INDEX].vector;
	else
		irq = bnad->pcidev->irq;
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	synchronize_irq(irq);
}

/* Utility used by bnad_start_xmit, for doing TSO */
static int
bnad_tso_prepare(struct bnad *bnad, struct sk_buff *skb)
{
	int err;

	err = skb_cow_head(skb, 0);
	if (err < 0) {
		BNAD_UPDATE_CTR(bnad, tso_err);
		return err;
	}

	/*
	 * For TSO, the TCP checksum field is seeded with pseudo-header sum
	 * excluding the length field.
	 */
	if (vlan_get_protocol(skb) == htons(ETH_P_IP)) {
		struct iphdr *iph = ip_hdr(skb);

		/* Do we really need these? */
		iph->tot_len = 0;
		iph->check = 0;

		tcp_hdr(skb)->check =
			~csum_tcpudp_magic(iph->saddr, iph->daddr, 0,
					   IPPROTO_TCP, 0);
		BNAD_UPDATE_CTR(bnad, tso4);
	} else {
		struct ipv6hdr *ipv6h = ipv6_hdr(skb);

		ipv6h->payload_len = 0;
		tcp_hdr(skb)->check =
			~csum_ipv6_magic(&ipv6h->saddr, &ipv6h->daddr, 0,
					 IPPROTO_TCP, 0);
		BNAD_UPDATE_CTR(bnad, tso6);
	}

	return 0;
}

/*
 * Initialize Q numbers depending on Rx Paths
 * Called with bnad->bna_lock held, because of cfg_flags
 * access.
 */
static void
bnad_q_num_init(struct bnad *bnad)
{
	int rxps;

	rxps = min((uint)num_online_cpus(),
			(uint)(BNAD_MAX_RX * BNAD_MAX_RXP_PER_RX));

	if (!(bnad->cfg_flags & BNAD_CF_MSIX))
		rxps = 1;	/* INTx */

	bnad->num_rx = 1;
	bnad->num_tx = 1;
	bnad->num_rxp_per_rx = rxps;
	bnad->num_txq_per_tx = BNAD_TXQ_NUM;
}

/*
 * Adjusts the Q numbers, given a number of msix vectors
 * Give preference to RSS as opposed to Tx priority Queues,
 * in such a case, just use 1 Tx Q
 * Called with bnad->bna_lock held b'cos of cfg_flags access
 */
static void
bnad_q_num_adjust(struct bnad *bnad, int msix_vectors, int temp)
{
	bnad->num_txq_per_tx = 1;
	if ((msix_vectors >= (bnad->num_tx * bnad->num_txq_per_tx)  +
	     bnad_rxqs_per_cq + BNAD_MAILBOX_MSIX_VECTORS) &&
	    (bnad->cfg_flags & BNAD_CF_MSIX)) {
		bnad->num_rxp_per_rx = msix_vectors -
			(bnad->num_tx * bnad->num_txq_per_tx) -
			BNAD_MAILBOX_MSIX_VECTORS;
	} else
		bnad->num_rxp_per_rx = 1;
}

/* Enable / disable ioceth */
static int
bnad_ioceth_disable(struct bnad *bnad)
{
	unsigned long flags;
	int err = 0;

	spin_lock_irqsave(&bnad->bna_lock, flags);
	init_completion(&bnad->bnad_completions.ioc_comp);
	bna_ioceth_disable(&bnad->bna.ioceth, BNA_HARD_CLEANUP);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	wait_for_completion_timeout(&bnad->bnad_completions.ioc_comp,
		msecs_to_jiffies(BNAD_IOCETH_TIMEOUT));

	err = bnad->bnad_completions.ioc_comp_status;
	return err;
}

static int
bnad_ioceth_enable(struct bnad *bnad)
{
	int err = 0;
	unsigned long flags;

	spin_lock_irqsave(&bnad->bna_lock, flags);
	init_completion(&bnad->bnad_completions.ioc_comp);
	bnad->bnad_completions.ioc_comp_status = BNA_CB_WAITING;
	bna_ioceth_enable(&bnad->bna.ioceth);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	wait_for_completion_timeout(&bnad->bnad_completions.ioc_comp,
		msecs_to_jiffies(BNAD_IOCETH_TIMEOUT));

	err = bnad->bnad_completions.ioc_comp_status;

	return err;
}

/* Free BNA resources */
static void
bnad_res_free(struct bnad *bnad, struct bna_res_info *res_info,
		u32 res_val_max)
{
	int i;

	for (i = 0; i < res_val_max; i++)
		bnad_mem_free(bnad, &res_info[i].res_u.mem_info);
}

/* Allocates memory and interrupt resources for BNA */
static int
bnad_res_alloc(struct bnad *bnad, struct bna_res_info *res_info,
		u32 res_val_max)
{
	int i, err;

	for (i = 0; i < res_val_max; i++) {
		err = bnad_mem_alloc(bnad, &res_info[i].res_u.mem_info);
		if (err)
			goto err_return;
	}
	return 0;

err_return:
	bnad_res_free(bnad, res_info, res_val_max);
	return err;
}

/* Interrupt enable / disable */
static void
bnad_enable_msix(struct bnad *bnad)
{
	int i, ret;
	unsigned long flags;

	spin_lock_irqsave(&bnad->bna_lock, flags);
	if (!(bnad->cfg_flags & BNAD_CF_MSIX)) {
		spin_unlock_irqrestore(&bnad->bna_lock, flags);
		return;
	}
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	if (bnad->msix_table)
		return;

	bnad->msix_table =
		kcalloc(bnad->msix_num, sizeof(struct msix_entry), GFP_KERNEL);

	if (!bnad->msix_table)
		goto intx_mode;

	for (i = 0; i < bnad->msix_num; i++)
		bnad->msix_table[i].entry = i;

	ret = pci_enable_msix_range(bnad->pcidev, bnad->msix_table,
				    1, bnad->msix_num);
	if (ret < 0) {
		goto intx_mode;
	} else if (ret < bnad->msix_num) {
		dev_warn(&bnad->pcidev->dev,
			 "%d MSI-X vectors allocated < %d requested\n",
			 ret, bnad->msix_num);

		spin_lock_irqsave(&bnad->bna_lock, flags);
		/* ret = #of vectors that we got */
		bnad_q_num_adjust(bnad, (ret - BNAD_MAILBOX_MSIX_VECTORS) / 2,
			(ret - BNAD_MAILBOX_MSIX_VECTORS) / 2);
		spin_unlock_irqrestore(&bnad->bna_lock, flags);

		bnad->msix_num = BNAD_NUM_TXQ + BNAD_NUM_RXP +
			 BNAD_MAILBOX_MSIX_VECTORS;

		if (bnad->msix_num > ret) {
			pci_disable_msix(bnad->pcidev);
			goto intx_mode;
		}
	}

	pci_intx(bnad->pcidev, 0);

	return;

intx_mode:
	dev_warn(&bnad->pcidev->dev,
		 "MSI-X enable failed - operating in INTx mode\n");

	kfree(bnad->msix_table);
	bnad->msix_table = NULL;
	bnad->msix_num = 0;
	spin_lock_irqsave(&bnad->bna_lock, flags);
	bnad->cfg_flags &= ~BNAD_CF_MSIX;
	bnad_q_num_init(bnad);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);
}

static void
bnad_disable_msix(struct bnad *bnad)
{
	u32 cfg_flags;
	unsigned long flags;

	spin_lock_irqsave(&bnad->bna_lock, flags);
	cfg_flags = bnad->cfg_flags;
	if (bnad->cfg_flags & BNAD_CF_MSIX)
		bnad->cfg_flags &= ~BNAD_CF_MSIX;
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	if (cfg_flags & BNAD_CF_MSIX) {
		pci_disable_msix(bnad->pcidev);
		kfree(bnad->msix_table);
		bnad->msix_table = NULL;
	}
}

/* Netdev entry points */
static int
bnad_open(struct net_device *netdev)
{
	int err;
	struct bnad *bnad = netdev_priv(netdev);
	struct bna_pause_config pause_config;
	unsigned long flags;

	mutex_lock(&bnad->conf_mutex);

	/* Tx */
	err = bnad_setup_tx(bnad, 0);
	if (err)
		goto err_return;

	/* Rx */
	err = bnad_setup_rx(bnad, 0);
	if (err)
		goto cleanup_tx;

	/* Port */
	pause_config.tx_pause = 0;
	pause_config.rx_pause = 0;

	spin_lock_irqsave(&bnad->bna_lock, flags);
	bna_enet_mtu_set(&bnad->bna.enet,
			 BNAD_FRAME_SIZE(bnad->netdev->mtu), NULL);
	bna_enet_pause_config(&bnad->bna.enet, &pause_config);
	bna_enet_enable(&bnad->bna.enet);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	/* Enable broadcast */
	bnad_enable_default_bcast(bnad);

	/* Restore VLANs, if any */
	bnad_restore_vlans(bnad, 0);

	/* Set the UCAST address */
	spin_lock_irqsave(&bnad->bna_lock, flags);
	bnad_mac_addr_set_locked(bnad, netdev->dev_addr);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	/* Start the stats timer */
	bnad_stats_timer_start(bnad);

	mutex_unlock(&bnad->conf_mutex);

	return 0;

cleanup_tx:
	bnad_destroy_tx(bnad, 0);

err_return:
	mutex_unlock(&bnad->conf_mutex);
	return err;
}

static int
bnad_stop(struct net_device *netdev)
{
	struct bnad *bnad = netdev_priv(netdev);
	unsigned long flags;

	mutex_lock(&bnad->conf_mutex);

	/* Stop the stats timer */
	bnad_stats_timer_stop(bnad);

	init_completion(&bnad->bnad_completions.enet_comp);

	spin_lock_irqsave(&bnad->bna_lock, flags);
	bna_enet_disable(&bnad->bna.enet, BNA_HARD_CLEANUP,
			bnad_cb_enet_disabled);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	wait_for_completion(&bnad->bnad_completions.enet_comp);

	bnad_destroy_tx(bnad, 0);
	bnad_destroy_rx(bnad, 0);

	/* Synchronize mailbox IRQ */
	bnad_mbox_irq_sync(bnad);

	mutex_unlock(&bnad->conf_mutex);

	return 0;
}

/* TX */
/* Returns 0 for success */
static int
bnad_txq_wi_prepare(struct bnad *bnad, struct bna_tcb *tcb,
		    struct sk_buff *skb, struct bna_txq_entry *txqent)
{
	u16 flags = 0;
	u32 gso_size;
	u16 vlan_tag = 0;

	if (skb_vlan_tag_present(skb)) {
		vlan_tag = (u16)skb_vlan_tag_get(skb);
		flags |= (BNA_TXQ_WI_CF_INS_PRIO | BNA_TXQ_WI_CF_INS_VLAN);
	}
	if (test_bit(BNAD_RF_CEE_RUNNING, &bnad->run_flags)) {
		vlan_tag = ((tcb->priority & 0x7) << VLAN_PRIO_SHIFT)
				| (vlan_tag & 0x1fff);
		flags |= (BNA_TXQ_WI_CF_INS_PRIO | BNA_TXQ_WI_CF_INS_VLAN);
	}
	txqent->hdr.wi.vlan_tag = htons(vlan_tag);

	if (skb_is_gso(skb)) {
		gso_size = skb_shinfo(skb)->gso_size;
		if (unlikely(gso_size > bnad->netdev->mtu)) {
			BNAD_UPDATE_CTR(bnad, tx_skb_mss_too_long);
			return -EINVAL;
		}
		if (unlikely((gso_size + skb_transport_offset(skb) +
			      tcp_hdrlen(skb)) >= skb->len)) {
			txqent->hdr.wi.opcode = htons(BNA_TXQ_WI_SEND);
			txqent->hdr.wi.lso_mss = 0;
			BNAD_UPDATE_CTR(bnad, tx_skb_tso_too_short);
		} else {
			txqent->hdr.wi.opcode = htons(BNA_TXQ_WI_SEND_LSO);
			txqent->hdr.wi.lso_mss = htons(gso_size);
		}

		if (bnad_tso_prepare(bnad, skb)) {
			BNAD_UPDATE_CTR(bnad, tx_skb_tso_prepare);
			return -EINVAL;
		}

		flags |= (BNA_TXQ_WI_CF_IP_CKSUM | BNA_TXQ_WI_CF_TCP_CKSUM);
		txqent->hdr.wi.l4_hdr_size_n_offset =
			htons(BNA_TXQ_WI_L4_HDR_N_OFFSET(
			tcp_hdrlen(skb) >> 2, skb_transport_offset(skb)));
	} else  {
		txqent->hdr.wi.opcode =	htons(BNA_TXQ_WI_SEND);
		txqent->hdr.wi.lso_mss = 0;

		if (unlikely(skb->len > (bnad->netdev->mtu + VLAN_ETH_HLEN))) {
			BNAD_UPDATE_CTR(bnad, tx_skb_non_tso_too_long);
			return -EINVAL;
		}

		if (skb->ip_summed == CHECKSUM_PARTIAL) {
			__be16 net_proto = vlan_get_protocol(skb);
			u8 proto = 0;

			if (net_proto == htons(ETH_P_IP))
				proto = ip_hdr(skb)->protocol;
#ifdef NETIF_F_IPV6_CSUM
			else if (net_proto == htons(ETH_P_IPV6)) {
				/* nexthdr may not be TCP immediately. */
				proto = ipv6_hdr(skb)->nexthdr;
			}
#endif
			if (proto == IPPROTO_TCP) {
				flags |= BNA_TXQ_WI_CF_TCP_CKSUM;
				txqent->hdr.wi.l4_hdr_size_n_offset =
					htons(BNA_TXQ_WI_L4_HDR_N_OFFSET
					      (0, skb_transport_offset(skb)));

				BNAD_UPDATE_CTR(bnad, tcpcsum_offload);

				if (unlikely(skb_headlen(skb) <
					    skb_transport_offset(skb) +
				    tcp_hdrlen(skb))) {
					BNAD_UPDATE_CTR(bnad, tx_skb_tcp_hdr);
					return -EINVAL;
				}
			} else if (proto == IPPROTO_UDP) {
				flags |= BNA_TXQ_WI_CF_UDP_CKSUM;
				txqent->hdr.wi.l4_hdr_size_n_offset =
					htons(BNA_TXQ_WI_L4_HDR_N_OFFSET
					      (0, skb_transport_offset(skb)));

				BNAD_UPDATE_CTR(bnad, udpcsum_offload);
				if (unlikely(skb_headlen(skb) <
					    skb_transport_offset(skb) +
				    sizeof(struct udphdr))) {
					BNAD_UPDATE_CTR(bnad, tx_skb_udp_hdr);
					return -EINVAL;
				}
			} else {

				BNAD_UPDATE_CTR(bnad, tx_skb_csum_err);
				return -EINVAL;
			}
		} else
			txqent->hdr.wi.l4_hdr_size_n_offset = 0;
	}

	txqent->hdr.wi.flags = htons(flags);
	txqent->hdr.wi.frame_length = htonl(skb->len);

	return 0;
}

/*
 * bnad_start_xmit : Netdev entry point for Transmit
 *		     Called under lock held by net_device
 */
static netdev_tx_t
bnad_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct bnad *bnad = netdev_priv(netdev);
	u32 txq_id = 0;
	struct bna_tcb *tcb = NULL;
	struct bnad_tx_unmap *unmap_q, *unmap, *head_unmap;
	u32		prod, q_depth, vect_id;
	u32		wis, vectors, len;
	int		i;
	dma_addr_t		dma_addr;
	struct bna_txq_entry *txqent;

	len = skb_headlen(skb);

	/* Sanity checks for the skb */

	if (unlikely(skb->len <= ETH_HLEN)) {
		dev_kfree_skb_any(skb);
		BNAD_UPDATE_CTR(bnad, tx_skb_too_short);
		return NETDEV_TX_OK;
	}
	if (unlikely(len > BFI_TX_MAX_DATA_PER_VECTOR)) {
		dev_kfree_skb_any(skb);
		BNAD_UPDATE_CTR(bnad, tx_skb_headlen_zero);
		return NETDEV_TX_OK;
	}
	if (unlikely(len == 0)) {
		dev_kfree_skb_any(skb);
		BNAD_UPDATE_CTR(bnad, tx_skb_headlen_zero);
		return NETDEV_TX_OK;
	}

	tcb = bnad->tx_info[0].tcb[txq_id];

	/*
	 * Takes care of the Tx that is scheduled between clearing the flag
	 * and the netif_tx_stop_all_queues() call.
	 */
	if (unlikely(!tcb || !test_bit(BNAD_TXQ_TX_STARTED, &tcb->flags))) {
		dev_kfree_skb_any(skb);
		BNAD_UPDATE_CTR(bnad, tx_skb_stopping);
		return NETDEV_TX_OK;
	}

	q_depth = tcb->q_depth;
	prod = tcb->producer_index;
	unmap_q = tcb->unmap_q;

	vectors = 1 + skb_shinfo(skb)->nr_frags;
	wis = BNA_TXQ_WI_NEEDED(vectors);	/* 4 vectors per work item */

	if (unlikely(vectors > BFI_TX_MAX_VECTORS_PER_PKT)) {
		dev_kfree_skb_any(skb);
		BNAD_UPDATE_CTR(bnad, tx_skb_max_vectors);
		return NETDEV_TX_OK;
	}

	/* Check for available TxQ resources */
	if (unlikely(wis > BNA_QE_FREE_CNT(tcb, q_depth))) {
		if ((*tcb->hw_consumer_index != tcb->consumer_index) &&
		    !test_and_set_bit(BNAD_TXQ_FREE_SENT, &tcb->flags)) {
			u32 sent;
			sent = bnad_txcmpl_process(bnad, tcb);
			if (likely(test_bit(BNAD_TXQ_TX_STARTED, &tcb->flags)))
				bna_ib_ack(tcb->i_dbell, sent);
			smp_mb__before_atomic();
			clear_bit(BNAD_TXQ_FREE_SENT, &tcb->flags);
		} else {
			netif_stop_queue(netdev);
			BNAD_UPDATE_CTR(bnad, netif_queue_stop);
		}

		smp_mb();
		/*
		 * Check again to deal with race condition between
		 * netif_stop_queue here, and netif_wake_queue in
		 * interrupt handler which is not inside netif tx lock.
		 */
		if (likely(wis > BNA_QE_FREE_CNT(tcb, q_depth))) {
			BNAD_UPDATE_CTR(bnad, netif_queue_stop);
			return NETDEV_TX_BUSY;
		} else {
			netif_wake_queue(netdev);
			BNAD_UPDATE_CTR(bnad, netif_queue_wakeup);
		}
	}

	txqent = &((struct bna_txq_entry *)tcb->sw_q)[prod];
	head_unmap = &unmap_q[prod];

	/* Program the opcode, flags, frame_len, num_vectors in WI */
	if (bnad_txq_wi_prepare(bnad, tcb, skb, txqent)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}
	txqent->hdr.wi.reserved = 0;
	txqent->hdr.wi.num_vectors = vectors;

	head_unmap->skb = skb;
	head_unmap->nvecs = 0;

	/* Program the vectors */
	unmap = head_unmap;
	dma_addr = dma_map_single(&bnad->pcidev->dev, skb->data,
				  len, DMA_TO_DEVICE);
	if (dma_mapping_error(&bnad->pcidev->dev, dma_addr)) {
		dev_kfree_skb_any(skb);
		BNAD_UPDATE_CTR(bnad, tx_skb_map_failed);
		return NETDEV_TX_OK;
	}
	BNA_SET_DMA_ADDR(dma_addr, &txqent->vector[0].host_addr);
	txqent->vector[0].length = htons(len);
	dma_unmap_addr_set(&unmap->vectors[0], dma_addr, dma_addr);
	head_unmap->nvecs++;

	for (i = 0, vect_id = 0; i < vectors - 1; i++) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		u32		size = skb_frag_size(frag);

		if (unlikely(size == 0)) {
			/* Undo the changes starting at tcb->producer_index */
			bnad_tx_buff_unmap(bnad, unmap_q, q_depth,
				tcb->producer_index);
			dev_kfree_skb_any(skb);
			BNAD_UPDATE_CTR(bnad, tx_skb_frag_zero);
			return NETDEV_TX_OK;
		}

		len += size;

		vect_id++;
		if (vect_id == BFI_TX_MAX_VECTORS_PER_WI) {
			vect_id = 0;
			BNA_QE_INDX_INC(prod, q_depth);
			txqent = &((struct bna_txq_entry *)tcb->sw_q)[prod];
			txqent->hdr.wi_ext.opcode = htons(BNA_TXQ_WI_EXTENSION);
			unmap = &unmap_q[prod];
		}

		dma_addr = skb_frag_dma_map(&bnad->pcidev->dev, frag,
					    0, size, DMA_TO_DEVICE);
		if (dma_mapping_error(&bnad->pcidev->dev, dma_addr)) {
			/* Undo the changes starting at tcb->producer_index */
			bnad_tx_buff_unmap(bnad, unmap_q, q_depth,
					   tcb->producer_index);
			dev_kfree_skb_any(skb);
			BNAD_UPDATE_CTR(bnad, tx_skb_map_failed);
			return NETDEV_TX_OK;
		}

		dma_unmap_len_set(&unmap->vectors[vect_id], dma_len, size);
		BNA_SET_DMA_ADDR(dma_addr, &txqent->vector[vect_id].host_addr);
		txqent->vector[vect_id].length = htons(size);
		dma_unmap_addr_set(&unmap->vectors[vect_id], dma_addr,
				   dma_addr);
		head_unmap->nvecs++;
	}

	if (unlikely(len != skb->len)) {
		/* Undo the changes starting at tcb->producer_index */
		bnad_tx_buff_unmap(bnad, unmap_q, q_depth, tcb->producer_index);
		dev_kfree_skb_any(skb);
		BNAD_UPDATE_CTR(bnad, tx_skb_len_mismatch);
		return NETDEV_TX_OK;
	}

	BNA_QE_INDX_INC(prod, q_depth);
	tcb->producer_index = prod;

	wmb();

	if (unlikely(!test_bit(BNAD_TXQ_TX_STARTED, &tcb->flags)))
		return NETDEV_TX_OK;

	skb_tx_timestamp(skb);

	bna_txq_prod_indx_doorbell(tcb);

	return NETDEV_TX_OK;
}

/*
 * Used spin_lock to synchronize reading of stats structures, which
 * is written by BNA under the same lock.
 */
static void
bnad_get_stats64(struct net_device *netdev, struct rtnl_link_stats64 *stats)
{
	struct bnad *bnad = netdev_priv(netdev);
	unsigned long flags;

	spin_lock_irqsave(&bnad->bna_lock, flags);

	bnad_netdev_qstats_fill(bnad, stats);
	bnad_netdev_hwstats_fill(bnad, stats);

	spin_unlock_irqrestore(&bnad->bna_lock, flags);
}

static void
bnad_set_rx_ucast_fltr(struct bnad *bnad)
{
	struct net_device *netdev = bnad->netdev;
	int uc_count = netdev_uc_count(netdev);
	enum bna_cb_status ret;
	u8 *mac_list;
	struct netdev_hw_addr *ha;
	int entry;

	if (netdev_uc_empty(bnad->netdev)) {
		bna_rx_ucast_listset(bnad->rx_info[0].rx, 0, NULL);
		return;
	}

	if (uc_count > bna_attr(&bnad->bna)->num_ucmac)
		goto mode_default;

	mac_list = kcalloc(ETH_ALEN, uc_count, GFP_ATOMIC);
	if (mac_list == NULL)
		goto mode_default;

	entry = 0;
	netdev_for_each_uc_addr(ha, netdev) {
		ether_addr_copy(&mac_list[entry * ETH_ALEN], &ha->addr[0]);
		entry++;
	}

	ret = bna_rx_ucast_listset(bnad->rx_info[0].rx, entry, mac_list);
	kfree(mac_list);

	if (ret != BNA_CB_SUCCESS)
		goto mode_default;

	return;

	/* ucast packets not in UCAM are routed to default function */
mode_default:
	bnad->cfg_flags |= BNAD_CF_DEFAULT;
	bna_rx_ucast_listset(bnad->rx_info[0].rx, 0, NULL);
}

static void
bnad_set_rx_mcast_fltr(struct bnad *bnad)
{
	struct net_device *netdev = bnad->netdev;
	int mc_count = netdev_mc_count(netdev);
	enum bna_cb_status ret;
	u8 *mac_list;

	if (netdev->flags & IFF_ALLMULTI)
		goto mode_allmulti;

	if (netdev_mc_empty(netdev))
		return;

	if (mc_count > bna_attr(&bnad->bna)->num_mcmac)
		goto mode_allmulti;

	mac_list = kcalloc(mc_count + 1, ETH_ALEN, GFP_ATOMIC);

	if (mac_list == NULL)
		goto mode_allmulti;

	ether_addr_copy(&mac_list[0], &bnad_bcast_addr[0]);

	/* copy rest of the MCAST addresses */
	bnad_netdev_mc_list_get(netdev, mac_list);
	ret = bna_rx_mcast_listset(bnad->rx_info[0].rx, mc_count + 1, mac_list);
	kfree(mac_list);

	if (ret != BNA_CB_SUCCESS)
		goto mode_allmulti;

	return;

mode_allmulti:
	bnad->cfg_flags |= BNAD_CF_ALLMULTI;
	bna_rx_mcast_delall(bnad->rx_info[0].rx);
}

void
bnad_set_rx_mode(struct net_device *netdev)
{
	struct bnad *bnad = netdev_priv(netdev);
	enum bna_rxmode new_mode, mode_mask;
	unsigned long flags;

	spin_lock_irqsave(&bnad->bna_lock, flags);

	if (bnad->rx_info[0].rx == NULL) {
		spin_unlock_irqrestore(&bnad->bna_lock, flags);
		return;
	}

	/* clear bnad flags to update it with new settings */
	bnad->cfg_flags &= ~(BNAD_CF_PROMISC | BNAD_CF_DEFAULT |
			BNAD_CF_ALLMULTI);

	new_mode = 0;
	if (netdev->flags & IFF_PROMISC) {
		new_mode |= BNAD_RXMODE_PROMISC_DEFAULT;
		bnad->cfg_flags |= BNAD_CF_PROMISC;
	} else {
		bnad_set_rx_mcast_fltr(bnad);

		if (bnad->cfg_flags & BNAD_CF_ALLMULTI)
			new_mode |= BNA_RXMODE_ALLMULTI;

		bnad_set_rx_ucast_fltr(bnad);

		if (bnad->cfg_flags & BNAD_CF_DEFAULT)
			new_mode |= BNA_RXMODE_DEFAULT;
	}

	mode_mask = BNA_RXMODE_PROMISC | BNA_RXMODE_DEFAULT |
			BNA_RXMODE_ALLMULTI;
	bna_rx_mode_set(bnad->rx_info[0].rx, new_mode, mode_mask);

	spin_unlock_irqrestore(&bnad->bna_lock, flags);
}

/*
 * bna_lock is used to sync writes to netdev->addr
 * conf_lock cannot be used since this call may be made
 * in a non-blocking context.
 */
static int
bnad_set_mac_address(struct net_device *netdev, void *addr)
{
	int err;
	struct bnad *bnad = netdev_priv(netdev);
	struct sockaddr *sa = (struct sockaddr *)addr;
	unsigned long flags;

	spin_lock_irqsave(&bnad->bna_lock, flags);

	err = bnad_mac_addr_set_locked(bnad, sa->sa_data);
	if (!err)
		ether_addr_copy(netdev->dev_addr, sa->sa_data);

	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	return err;
}

static int
bnad_mtu_set(struct bnad *bnad, int frame_size)
{
	unsigned long flags;

	init_completion(&bnad->bnad_completions.mtu_comp);

	spin_lock_irqsave(&bnad->bna_lock, flags);
	bna_enet_mtu_set(&bnad->bna.enet, frame_size, bnad_cb_enet_mtu_set);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	wait_for_completion(&bnad->bnad_completions.mtu_comp);

	return bnad->bnad_completions.mtu_comp_status;
}

static int
bnad_change_mtu(struct net_device *netdev, int new_mtu)
{
	int err, mtu;
	struct bnad *bnad = netdev_priv(netdev);
	u32 rx_count = 0, frame, new_frame;

	mutex_lock(&bnad->conf_mutex);

	mtu = netdev->mtu;
	netdev->mtu = new_mtu;

	frame = BNAD_FRAME_SIZE(mtu);
	new_frame = BNAD_FRAME_SIZE(new_mtu);

	/* check if multi-buffer needs to be enabled */
	if (BNAD_PCI_DEV_IS_CAT2(bnad) &&
	    netif_running(bnad->netdev)) {
		/* only when transition is over 4K */
		if ((frame <= 4096 && new_frame > 4096) ||
		    (frame > 4096 && new_frame <= 4096))
			rx_count = bnad_reinit_rx(bnad);
	}

	/* rx_count > 0 - new rx created
	 *	- Linux set err = 0 and return
	 */
	err = bnad_mtu_set(bnad, new_frame);
	if (err)
		err = -EBUSY;

	mutex_unlock(&bnad->conf_mutex);
	return err;
}

static int
bnad_vlan_rx_add_vid(struct net_device *netdev, __be16 proto, u16 vid)
{
	struct bnad *bnad = netdev_priv(netdev);
	unsigned long flags;

	if (!bnad->rx_info[0].rx)
		return 0;

	mutex_lock(&bnad->conf_mutex);

	spin_lock_irqsave(&bnad->bna_lock, flags);
	bna_rx_vlan_add(bnad->rx_info[0].rx, vid);
	set_bit(vid, bnad->active_vlans);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	mutex_unlock(&bnad->conf_mutex);

	return 0;
}

static int
bnad_vlan_rx_kill_vid(struct net_device *netdev, __be16 proto, u16 vid)
{
	struct bnad *bnad = netdev_priv(netdev);
	unsigned long flags;

	if (!bnad->rx_info[0].rx)
		return 0;

	mutex_lock(&bnad->conf_mutex);

	spin_lock_irqsave(&bnad->bna_lock, flags);
	clear_bit(vid, bnad->active_vlans);
	bna_rx_vlan_del(bnad->rx_info[0].rx, vid);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	mutex_unlock(&bnad->conf_mutex);

	return 0;
}

static int bnad_set_features(struct net_device *dev, netdev_features_t features)
{
	struct bnad *bnad = netdev_priv(dev);
	netdev_features_t changed = features ^ dev->features;

	if ((changed & NETIF_F_HW_VLAN_CTAG_RX) && netif_running(dev)) {
		unsigned long flags;

		spin_lock_irqsave(&bnad->bna_lock, flags);

		if (features & NETIF_F_HW_VLAN_CTAG_RX)
			bna_rx_vlan_strip_enable(bnad->rx_info[0].rx);
		else
			bna_rx_vlan_strip_disable(bnad->rx_info[0].rx);

		spin_unlock_irqrestore(&bnad->bna_lock, flags);
	}

	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void
bnad_netpoll(struct net_device *netdev)
{
	struct bnad *bnad = netdev_priv(netdev);
	struct bnad_rx_info *rx_info;
	struct bnad_rx_ctrl *rx_ctrl;
	u32 curr_mask;
	int i, j;

	if (!(bnad->cfg_flags & BNAD_CF_MSIX)) {
		bna_intx_disable(&bnad->bna, curr_mask);
		bnad_isr(bnad->pcidev->irq, netdev);
		bna_intx_enable(&bnad->bna, curr_mask);
	} else {
		/*
		 * Tx processing may happen in sending context, so no need
		 * to explicitly process completions here
		 */

		/* Rx processing */
		for (i = 0; i < bnad->num_rx; i++) {
			rx_info = &bnad->rx_info[i];
			if (!rx_info->rx)
				continue;
			for (j = 0; j < bnad->num_rxp_per_rx; j++) {
				rx_ctrl = &rx_info->rx_ctrl[j];
				if (rx_ctrl->ccb)
					bnad_netif_rx_schedule_poll(bnad,
							    rx_ctrl->ccb);
			}
		}
	}
}
#endif

static const struct net_device_ops bnad_netdev_ops = {
	.ndo_open		= bnad_open,
	.ndo_stop		= bnad_stop,
	.ndo_start_xmit		= bnad_start_xmit,
	.ndo_get_stats64	= bnad_get_stats64,
	.ndo_set_rx_mode	= bnad_set_rx_mode,
	.ndo_validate_addr      = eth_validate_addr,
	.ndo_set_mac_address    = bnad_set_mac_address,
	.ndo_change_mtu		= bnad_change_mtu,
	.ndo_vlan_rx_add_vid    = bnad_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid   = bnad_vlan_rx_kill_vid,
	.ndo_set_features	= bnad_set_features,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller    = bnad_netpoll
#endif
};

static void
bnad_netdev_init(struct bnad *bnad, bool using_dac)
{
	struct net_device *netdev = bnad->netdev;

	netdev->hw_features = NETIF_F_SG | NETIF_F_RXCSUM |
		NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
		NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_HW_VLAN_CTAG_TX |
		NETIF_F_HW_VLAN_CTAG_RX;

	netdev->vlan_features = NETIF_F_SG | NETIF_F_HIGHDMA |
		NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
		NETIF_F_TSO | NETIF_F_TSO6;

	netdev->features |= netdev->hw_features | NETIF_F_HW_VLAN_CTAG_FILTER;

	if (using_dac)
		netdev->features |= NETIF_F_HIGHDMA;

	netdev->mem_start = bnad->mmio_start;
	netdev->mem_end = bnad->mmio_start + bnad->mmio_len - 1;

	/* MTU range: 46 - 9000 */
	netdev->min_mtu = ETH_ZLEN - ETH_HLEN;
	netdev->max_mtu = BNAD_JUMBO_MTU;

	netdev->netdev_ops = &bnad_netdev_ops;
	bnad_set_ethtool_ops(netdev);
}

/*
 * 1. Initialize the bnad structure
 * 2. Setup netdev pointer in pci_dev
 * 3. Initialize no. of TxQ & CQs & MSIX vectors
 * 4. Initialize work queue.
 */
static int
bnad_init(struct bnad *bnad,
	  struct pci_dev *pdev, struct net_device *netdev)
{
	unsigned long flags;

	SET_NETDEV_DEV(netdev, &pdev->dev);
	pci_set_drvdata(pdev, netdev);

	bnad->netdev = netdev;
	bnad->pcidev = pdev;
	bnad->mmio_start = pci_resource_start(pdev, 0);
	bnad->mmio_len = pci_resource_len(pdev, 0);
	bnad->bar0 = ioremap(bnad->mmio_start, bnad->mmio_len);
	if (!bnad->bar0) {
		dev_err(&pdev->dev, "ioremap for bar0 failed\n");
		return -ENOMEM;
	}
	dev_info(&pdev->dev, "bar0 mapped to %p, len %llu\n", bnad->bar0,
		 (unsigned long long) bnad->mmio_len);

	spin_lock_irqsave(&bnad->bna_lock, flags);
	if (!bnad_msix_disable)
		bnad->cfg_flags = BNAD_CF_MSIX;

	bnad->cfg_flags |= BNAD_CF_DIM_ENABLED;

	bnad_q_num_init(bnad);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	bnad->msix_num = (bnad->num_tx * bnad->num_txq_per_tx) +
		(bnad->num_rx * bnad->num_rxp_per_rx) +
			 BNAD_MAILBOX_MSIX_VECTORS;

	bnad->txq_depth = BNAD_TXQ_DEPTH;
	bnad->rxq_depth = BNAD_RXQ_DEPTH;

	bnad->tx_coalescing_timeo = BFI_TX_COALESCING_TIMEO;
	bnad->rx_coalescing_timeo = BFI_RX_COALESCING_TIMEO;

	sprintf(bnad->wq_name, "%s_wq_%d", BNAD_NAME, bnad->id);
	bnad->work_q = create_singlethread_workqueue(bnad->wq_name);
	if (!bnad->work_q) {
		iounmap(bnad->bar0);
		return -ENOMEM;
	}

	return 0;
}

/*
 * Must be called after bnad_pci_uninit()
 * so that iounmap() and pci_set_drvdata(NULL)
 * happens only after PCI uninitialization.
 */
static void
bnad_uninit(struct bnad *bnad)
{
	if (bnad->work_q) {
		flush_workqueue(bnad->work_q);
		destroy_workqueue(bnad->work_q);
		bnad->work_q = NULL;
	}

	if (bnad->bar0)
		iounmap(bnad->bar0);
}

/*
 * Initialize locks
	a) Per ioceth mutes used for serializing configuration
	   changes from OS interface
	b) spin lock used to protect bna state machine
 */
static void
bnad_lock_init(struct bnad *bnad)
{
	spin_lock_init(&bnad->bna_lock);
	mutex_init(&bnad->conf_mutex);
}

static void
bnad_lock_uninit(struct bnad *bnad)
{
	mutex_destroy(&bnad->conf_mutex);
}

/* PCI Initialization */
static int
bnad_pci_init(struct bnad *bnad,
	      struct pci_dev *pdev, bool *using_dac)
{
	int err;

	err = pci_enable_device(pdev);
	if (err)
		return err;
	err = pci_request_regions(pdev, BNAD_NAME);
	if (err)
		goto disable_device;
	if (!dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64))) {
		*using_dac = true;
	} else {
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (err)
			goto release_regions;
		*using_dac = false;
	}
	pci_set_master(pdev);
	return 0;

release_regions:
	pci_release_regions(pdev);
disable_device:
	pci_disable_device(pdev);

	return err;
}

static void
bnad_pci_uninit(struct pci_dev *pdev)
{
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static int
bnad_pci_probe(struct pci_dev *pdev,
		const struct pci_device_id *pcidev_id)
{
	bool	using_dac;
	int	err;
	struct bnad *bnad;
	struct bna *bna;
	struct net_device *netdev;
	struct bfa_pcidev pcidev_info;
	unsigned long flags;

	mutex_lock(&bnad_fwimg_mutex);
	if (!cna_get_firmware_buf(pdev)) {
		mutex_unlock(&bnad_fwimg_mutex);
		dev_err(&pdev->dev, "failed to load firmware image!\n");
		return -ENODEV;
	}
	mutex_unlock(&bnad_fwimg_mutex);

	/*
	 * Allocates sizeof(struct net_device + struct bnad)
	 * bnad = netdev->priv
	 */
	netdev = alloc_etherdev(sizeof(struct bnad));
	if (!netdev) {
		err = -ENOMEM;
		return err;
	}
	bnad = netdev_priv(netdev);
	bnad_lock_init(bnad);
	bnad->id = atomic_inc_return(&bna_id) - 1;

	mutex_lock(&bnad->conf_mutex);
	/*
	 * PCI initialization
	 *	Output : using_dac = 1 for 64 bit DMA
	 *			   = 0 for 32 bit DMA
	 */
	using_dac = false;
	err = bnad_pci_init(bnad, pdev, &using_dac);
	if (err)
		goto unlock_mutex;

	/*
	 * Initialize bnad structure
	 * Setup relation between pci_dev & netdev
	 */
	err = bnad_init(bnad, pdev, netdev);
	if (err)
		goto pci_uninit;

	/* Initialize netdev structure, set up ethtool ops */
	bnad_netdev_init(bnad, using_dac);

	/* Set link to down state */
	netif_carrier_off(netdev);

	/* Setup the debugfs node for this bfad */
	if (bna_debugfs_enable)
		bnad_debugfs_init(bnad);

	/* Get resource requirement form bna */
	spin_lock_irqsave(&bnad->bna_lock, flags);
	bna_res_req(&bnad->res_info[0]);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	/* Allocate resources from bna */
	err = bnad_res_alloc(bnad, &bnad->res_info[0], BNA_RES_T_MAX);
	if (err)
		goto drv_uninit;

	bna = &bnad->bna;

	/* Setup pcidev_info for bna_init() */
	pcidev_info.pci_slot = PCI_SLOT(bnad->pcidev->devfn);
	pcidev_info.pci_func = PCI_FUNC(bnad->pcidev->devfn);
	pcidev_info.device_id = bnad->pcidev->device;
	pcidev_info.pci_bar_kva = bnad->bar0;

	spin_lock_irqsave(&bnad->bna_lock, flags);
	bna_init(bna, bnad, &pcidev_info, &bnad->res_info[0]);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	bnad->stats.bna_stats = &bna->stats;

	bnad_enable_msix(bnad);
	err = bnad_mbox_irq_alloc(bnad);
	if (err)
		goto res_free;

	/* Set up timers */
	timer_setup(&bnad->bna.ioceth.ioc.ioc_timer, bnad_ioc_timeout, 0);
	timer_setup(&bnad->bna.ioceth.ioc.hb_timer, bnad_ioc_hb_check, 0);
	timer_setup(&bnad->bna.ioceth.ioc.iocpf_timer, bnad_iocpf_timeout, 0);
	timer_setup(&bnad->bna.ioceth.ioc.sem_timer, bnad_iocpf_sem_timeout,
		    0);

	/*
	 * Start the chip
	 * If the call back comes with error, we bail out.
	 * This is a catastrophic error.
	 */
	err = bnad_ioceth_enable(bnad);
	if (err) {
		dev_err(&pdev->dev, "initialization failed err=%d\n", err);
		goto probe_success;
	}

	spin_lock_irqsave(&bnad->bna_lock, flags);
	if (bna_num_txq_set(bna, BNAD_NUM_TXQ + 1) ||
		bna_num_rxp_set(bna, BNAD_NUM_RXP + 1)) {
		bnad_q_num_adjust(bnad, bna_attr(bna)->num_txq - 1,
			bna_attr(bna)->num_rxp - 1);
		if (bna_num_txq_set(bna, BNAD_NUM_TXQ + 1) ||
			bna_num_rxp_set(bna, BNAD_NUM_RXP + 1))
			err = -EIO;
	}
	spin_unlock_irqrestore(&bnad->bna_lock, flags);
	if (err)
		goto disable_ioceth;

	spin_lock_irqsave(&bnad->bna_lock, flags);
	bna_mod_res_req(&bnad->bna, &bnad->mod_res_info[0]);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	err = bnad_res_alloc(bnad, &bnad->mod_res_info[0], BNA_MOD_RES_T_MAX);
	if (err) {
		err = -EIO;
		goto disable_ioceth;
	}

	spin_lock_irqsave(&bnad->bna_lock, flags);
	bna_mod_init(&bnad->bna, &bnad->mod_res_info[0]);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	/* Get the burnt-in mac */
	spin_lock_irqsave(&bnad->bna_lock, flags);
	bna_enet_perm_mac_get(&bna->enet, bnad->perm_addr);
	bnad_set_netdev_perm_addr(bnad);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	mutex_unlock(&bnad->conf_mutex);

	/* Finally, reguister with net_device layer */
	err = register_netdev(netdev);
	if (err) {
		dev_err(&pdev->dev, "registering net device failed\n");
		goto probe_uninit;
	}
	set_bit(BNAD_RF_NETDEV_REGISTERED, &bnad->run_flags);

	return 0;

probe_success:
	mutex_unlock(&bnad->conf_mutex);
	return 0;

probe_uninit:
	mutex_lock(&bnad->conf_mutex);
	bnad_res_free(bnad, &bnad->mod_res_info[0], BNA_MOD_RES_T_MAX);
disable_ioceth:
	bnad_ioceth_disable(bnad);
	del_timer_sync(&bnad->bna.ioceth.ioc.ioc_timer);
	del_timer_sync(&bnad->bna.ioceth.ioc.sem_timer);
	del_timer_sync(&bnad->bna.ioceth.ioc.hb_timer);
	spin_lock_irqsave(&bnad->bna_lock, flags);
	bna_uninit(bna);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);
	bnad_mbox_irq_free(bnad);
	bnad_disable_msix(bnad);
res_free:
	bnad_res_free(bnad, &bnad->res_info[0], BNA_RES_T_MAX);
drv_uninit:
	/* Remove the debugfs node for this bnad */
	kfree(bnad->regdata);
	bnad_debugfs_uninit(bnad);
	bnad_uninit(bnad);
pci_uninit:
	bnad_pci_uninit(pdev);
unlock_mutex:
	mutex_unlock(&bnad->conf_mutex);
	bnad_lock_uninit(bnad);
	free_netdev(netdev);
	return err;
}

static void
bnad_pci_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct bnad *bnad;
	struct bna *bna;
	unsigned long flags;

	if (!netdev)
		return;

	bnad = netdev_priv(netdev);
	bna = &bnad->bna;

	if (test_and_clear_bit(BNAD_RF_NETDEV_REGISTERED, &bnad->run_flags))
		unregister_netdev(netdev);

	mutex_lock(&bnad->conf_mutex);
	bnad_ioceth_disable(bnad);
	del_timer_sync(&bnad->bna.ioceth.ioc.ioc_timer);
	del_timer_sync(&bnad->bna.ioceth.ioc.sem_timer);
	del_timer_sync(&bnad->bna.ioceth.ioc.hb_timer);
	spin_lock_irqsave(&bnad->bna_lock, flags);
	bna_uninit(bna);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	bnad_res_free(bnad, &bnad->mod_res_info[0], BNA_MOD_RES_T_MAX);
	bnad_res_free(bnad, &bnad->res_info[0], BNA_RES_T_MAX);
	bnad_mbox_irq_free(bnad);
	bnad_disable_msix(bnad);
	bnad_pci_uninit(pdev);
	mutex_unlock(&bnad->conf_mutex);
	bnad_lock_uninit(bnad);
	/* Remove the debugfs node for this bnad */
	kfree(bnad->regdata);
	bnad_debugfs_uninit(bnad);
	bnad_uninit(bnad);
	free_netdev(netdev);
}

static const struct pci_device_id bnad_pci_id_table[] = {
	{
		PCI_DEVICE(PCI_VENDOR_ID_BROCADE,
			PCI_DEVICE_ID_BROCADE_CT),
		.class = PCI_CLASS_NETWORK_ETHERNET << 8,
		.class_mask =  0xffff00
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_BROCADE,
			BFA_PCI_DEVICE_ID_CT2),
		.class = PCI_CLASS_NETWORK_ETHERNET << 8,
		.class_mask =  0xffff00
	},
	{0,  },
};

MODULE_DEVICE_TABLE(pci, bnad_pci_id_table);

static struct pci_driver bnad_pci_driver = {
	.name = BNAD_NAME,
	.id_table = bnad_pci_id_table,
	.probe = bnad_pci_probe,
	.remove = bnad_pci_remove,
};

static int __init
bnad_module_init(void)
{
	int err;

	pr_info("bna: QLogic BR-series 10G Ethernet driver - version: %s\n",
		BNAD_VERSION);

	bfa_nw_ioc_auto_recover(bnad_ioc_auto_recover);

	err = pci_register_driver(&bnad_pci_driver);
	if (err < 0) {
		pr_err("bna: PCI driver registration failed err=%d\n", err);
		return err;
	}

	return 0;
}

static void __exit
bnad_module_exit(void)
{
	pci_unregister_driver(&bnad_pci_driver);
	release_firmware(bfi_fw);
}

module_init(bnad_module_init);
module_exit(bnad_module_exit);

MODULE_AUTHOR("Brocade");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("QLogic BR-series 10G PCIe Ethernet driver");
MODULE_VERSION(BNAD_VERSION);
MODULE_FIRMWARE(CNA_FW_FILE_CT);
MODULE_FIRMWARE(CNA_FW_FILE_CT2);
