// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/bitfield.h>
#include <linux/dma-mapping.h>
#include <linux/iopoll.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <net/netdev_lock.h>
#include <net/page_pool/helpers.h>

#include "mpnic.h"
#include "mpnic_netdev.h"
#include "mpnic_txrx.h"

struct mpnic_xmit_cb {
	u32 bytecount;
	u8 desc_count;
};

#define MPNIC_XMIT_CB(__skb) ((struct mpnic_xmit_cb *)((__skb)->cb))
#define MPNIC_TWD_TYPE_MASK(_type) \
	cpu_to_le64(FIELD_PREP(MPNIC_TWD_TYPE, MPNIC_TWD_TYPE_##_type))

/* Leave the interrupt moderation counters alone when arming or masking */
#define MPNIC_TIM_PARAM_CFG_PRESERVE_MASK \
	(MPNIC_TIM_CTL1_UPD_IGN_LONG_EVENT_CNT | \
	 MPNIC_TIM_CTL1_UPD_IGN_LONG_TIME_CNT | \
	 MPNIC_TIM_CTL1_UPD_IGN_SHORT_TIME_CNT)

static void mpnic_nv_irq_disable(struct mpnic_napi_vector *nv)
{
	mpnic_wr64(nv->mpd, MPNIC_TIM_CTL1(nv->qt[0].cmpl.q_idx),
		   MPNIC_TIM_PARAM_CFG_PRESERVE_MASK |
		   MPNIC_TIM_CTL1_MASK_EN | MPNIC_TIM_CTL1_MASK);
}

static void mpnic_nv_irq_rearm(struct mpnic_napi_vector *nv)
{
	/* Rearming a single queue on a given IRQ rearms all the other
	 * queues mapped to the same IRQ.
	 */
	mpnic_wr64(nv->mpd, MPNIC_TIM_CTL1(nv->qt[0].cmpl.q_idx),
		   MPNIC_TIM_PARAM_CFG_PRESERVE_MASK | MPNIC_TIM_CTL1_MASK_EN);
}

static void mpnic_nv_irq_trigger(struct mpnic_napi_vector *nv)
{
	mpnic_wr64(nv->mpd, MPNIC_TIM_CTL1(nv->qt[0].cmpl.q_idx),
		   MPNIC_TIM_PARAM_CFG_PRESERVE_MASK |
		   MPNIC_TIM_CTL1_MASK_EN | MPNIC_TIM_CTL1_TRIGGER);
}

static unsigned int mpnic_desc_unused(struct mpnic_ring *ring)
{
	return (ring->head - ring->tail - 1) & ring->size_mask;
}

static unsigned int mpnic_desc_used(struct mpnic_ring *ring)
{
	return (READ_ONCE(ring->tail) - READ_ONCE(ring->head)) &
	       ring->size_mask;
}

static struct netdev_queue *mpnic_txring_txq(const struct net_device *dev,
					     const struct mpnic_ring *ring)
{
	return netdev_get_tx_queue(dev, ring->q_idx);
}

static bool
mpnic_maybe_stop_tx(const struct net_device *dev, struct mpnic_ring *ring)
{
	int res;

	res = netif_txq_maybe_stop(mpnic_txring_txq(dev, ring),
				   mpnic_desc_unused(ring), MPNIC_MAX_SKB_DESC,
				   MPNIC_TX_DESC_WAKEUP);
	if (!res) {
		u64_stats_update_begin(&ring->stats.syncp);
		u64_stats_inc(&ring->stats.twq.stop);
		u64_stats_update_end(&ring->stats.syncp);
	}

	return !res;
}

static void mpnic_tx_doorbell(struct mpnic_ring *ring, __le64 *meta)
{
	*meta |= cpu_to_le64(MPNIC_TWD_FLAG_REQ_COMPLETION);
	ring->deferred_meta = -1;

	/* Force DMA writes to flush before writing to tail */
	dma_wmb();

	writeq(ring->tail, ring->doorbell);
}

/* Packets handed to us with xmit_more set are left in the ring without a
 * doorbell, and without a completion request, in the expectation that the
 * packet ending the burst will ring for all of them. If that packet gets
 * dropped instead we have to ring here, otherwise the descriptors sit in
 * the ring until the next transmit, which may never come.
 */
static void mpnic_tx_flush_doorbell(struct mpnic_ring *ring)
{
	if (ring->deferred_meta >= 0)
		mpnic_tx_doorbell(ring, &ring->desc[ring->deferred_meta]);
}

static void mpnic_unmap_single_twd(struct device *dev, __le64 *twd)
{
	u64 raw_twd = le64_to_cpu(*twd);

	dma_unmap_single(dev, FIELD_GET(MPNIC_TWD_ADDR, raw_twd),
			 FIELD_GET(MPNIC_TWD_LEN, raw_twd), DMA_TO_DEVICE);
}

static void mpnic_unmap_page_twd(struct device *dev, __le64 *twd)
{
	u64 raw_twd = le64_to_cpu(*twd);

	dma_unmap_page(dev, FIELD_GET(MPNIC_TWD_ADDR, raw_twd),
		       FIELD_GET(MPNIC_TWD_LEN, raw_twd), DMA_TO_DEVICE);
}

static bool
mpnic_tx_map(struct mpnic_ring *ring, struct sk_buff *skb, __le64 *meta)
{
	struct device *dev = skb->dev->dev.parent;
	unsigned int tail = ring->tail, first;
	unsigned int size, data_len;
	skb_frag_t *frag;
	dma_addr_t dma;
	__le64 *twd;

	tail++;
	tail &= ring->size_mask;
	first = tail;

	size = skb_headlen(skb);
	data_len = skb->data_len;

	if (size > FIELD_MAX(MPNIC_TWD_LEN))
		goto err_dma;

	dma = dma_map_single(dev, skb->data, size, DMA_TO_DEVICE);

	for (frag = &skb_shinfo(skb)->frags[0];; frag++) {
		twd = &ring->desc[tail];

		if (dma_mapping_error(dev, dma))
			goto err_dma;

		*twd = cpu_to_le64(FIELD_PREP(MPNIC_TWD_ADDR, dma) |
				   FIELD_PREP(MPNIC_TWD_LEN, size) |
				   FIELD_PREP(MPNIC_TWD_TYPE,
					      MPNIC_TWD_TYPE_AL));

		tail++;
		tail &= ring->size_mask;

		if (!data_len)
			break;

		size = skb_frag_size(frag);
		data_len -= size;

		if (size > FIELD_MAX(MPNIC_TWD_LEN))
			goto err_dma;

		dma = skb_frag_dma_map(dev, frag, 0, size, DMA_TO_DEVICE);
	}

	*twd |= MPNIC_TWD_TYPE_MASK(LAST_AL);

	MPNIC_XMIT_CB(skb)->desc_count = ((twd - meta) + 1) & ring->size_mask;

	skb_tx_timestamp(skb);

	ring->tail = tail;

	/* Verify there is room for another packet */
	mpnic_maybe_stop_tx(skb->dev, ring);

	if (__netdev_tx_sent_queue(mpnic_txring_txq(skb->dev, ring),
				   MPNIC_XMIT_CB(skb)->bytecount,
				   netdev_xmit_more()))
		mpnic_tx_doorbell(ring, meta);
	else
		ring->deferred_meta = meta - ring->desc;

	return false;
err_dma:
	if (net_ratelimit())
		netdev_err(skb->dev, "TX DMA map failed\n");

	while (tail != first) {
		tail--;
		tail &= ring->size_mask;
		twd = &ring->desc[tail];
		if (tail == first)
			mpnic_unmap_single_twd(dev, twd);
		else
			mpnic_unmap_page_twd(dev, twd);
	}

	return true;
}

#define MPNIC_MIN_FRAME_LEN	60

static netdev_tx_t mpnic_xmit_frame_ring(struct sk_buff *skb,
					 struct mpnic_ring *ring)
{
	__le64 *meta = &ring->desc[ring->tail];
	u32 tail = ring->tail;

	if (skb_put_padto(skb, MPNIC_MIN_FRAME_LEN))
		goto err_drop;

	if (mpnic_maybe_stop_tx(skb->dev, ring)) {
		mpnic_tx_flush_doorbell(ring);
		return NETDEV_TX_BUSY;
	}

	ring->tx_buf[tail] = skb;
	*meta = cpu_to_le64(MPNIC_TWD_FLAG_DEST_MAC);

	MPNIC_XMIT_CB(skb)->bytecount = skb->len;
	MPNIC_XMIT_CB(skb)->desc_count = 0;

	if (mpnic_tx_map(ring, skb, meta))
		goto err_free;

	return NETDEV_TX_OK;

err_free:
	dev_kfree_skb_any(skb);
	ring->tx_buf[tail] = NULL;
	ring->tail = tail;
err_drop:
	mpnic_tx_flush_doorbell(ring);

	u64_stats_update_begin(&ring->stats.syncp);
	u64_stats_inc(&ring->stats.twq.dropped);
	u64_stats_update_end(&ring->stats.syncp);

	return NETDEV_TX_OK;
}

netdev_tx_t mpnic_xmit_frame(struct sk_buff *skb, struct net_device *dev)
{
	struct mpnic_net *mpn = netdev_priv(dev);

	return mpnic_xmit_frame_ring(skb, mpn->tx[skb_get_queue_mapping(skb)]);
}

static void mpnic_clean_twq0(struct mpnic_napi_vector *nv, int napi_budget,
			     struct mpnic_q_triad *qt, bool discard,
			     unsigned int hw_head)
{
	u64 total_bytes = 0, total_packets = 0;
	struct mpnic_ring *ring = &qt->sub0;
	struct mpnic_ring *cmpl = &qt->cmpl;
	unsigned int head = ring->head;
	struct netdev_queue *txq;
	unsigned int clean_desc;

	clean_desc = (hw_head - head) & ring->size_mask;

	while (clean_desc) {
		struct sk_buff *skb = ring->tx_buf[head];
		unsigned int desc_cnt;

		desc_cnt = MPNIC_XMIT_CB(skb)->desc_count;
		if (desc_cnt > clean_desc)
			break;

		ring->tx_buf[head] = NULL;

		clean_desc -= desc_cnt;

		/* Step over the metadata descriptor */
		head++;
		head &= ring->size_mask;
		desc_cnt--;

		mpnic_unmap_single_twd(nv->dev, &ring->desc[head]);
		head++;
		head &= ring->size_mask;
		desc_cnt--;

		while (desc_cnt--) {
			mpnic_unmap_page_twd(nv->dev, &ring->desc[head]);
			head++;
			head &= ring->size_mask;
		}

		total_bytes += MPNIC_XMIT_CB(skb)->bytecount;
		total_packets++;

		napi_consume_skb(skb, napi_budget);
	}

	if (!total_bytes)
		return;

	ring->head = head;

	if (discard) {
		local_bh_disable();
		u64_stats_update_begin(&ring->stats.syncp);
		u64_stats_add(&ring->stats.twq.dropped, total_packets);
		u64_stats_update_end(&ring->stats.syncp);
		local_bh_enable();
		return;
	}

	u64_stats_update_begin(&cmpl->stats.syncp);
	u64_stats_add(&cmpl->stats.tcq.bytes, total_bytes);
	u64_stats_add(&cmpl->stats.tcq.packets, total_packets);
	u64_stats_update_end(&cmpl->stats.syncp);

	txq = mpnic_txring_txq(nv->napi.dev, ring);
	if (!netif_txq_completed_wake(txq, total_packets, total_bytes,
				      mpnic_desc_unused(ring),
				      MPNIC_TX_DESC_WAKEUP)) {
		u64_stats_update_begin(&cmpl->stats.syncp);
		u64_stats_inc(&cmpl->stats.tcq.wake);
		u64_stats_update_end(&cmpl->stats.syncp);
	}
}

static void mpnic_commit_cq_head(struct mpnic_ring *cmpl)
{
	u32 head = cmpl->head;

	/* The tail shadows the last value written to the doorbell, so a
	 * completion queue which has not moved costs no MMIO write.
	 */
	if (cmpl->tail != head) {
		cmpl->tail = head;
		writeq(head & cmpl->size_mask, cmpl->doorbell);
	}
}

static void mpnic_clean_tcq(struct mpnic_napi_vector *nv,
			    struct mpnic_q_triad *qt, int napi_budget)
{
	struct mpnic_ring *cmpl = &qt->cmpl;
	__le64 *raw_tcd, done;
	u32 head = cmpl->head;
	s32 head0 = -1;

	done = (head & (cmpl->size_mask + 1)) ? 0 : cpu_to_le64(MPNIC_TCD_DONE);
	raw_tcd = &cmpl->desc[head & cmpl->size_mask];

	/* Walk the completion queue collecting the heads reported by NIC.
	 * Only the first work queue is enabled and no packet asks for a
	 * timestamp, so every completion is a plain head update and the
	 * descriptor type does not have to be decoded.
	 */
	while ((*raw_tcd & cpu_to_le64(MPNIC_TCD_DONE)) == done) {
		u64 tcd;

		dma_rmb();

		tcd = le64_to_cpu(*raw_tcd);
		head0 = FIELD_GET(MPNIC_TCD_TYPE0_HEAD0, tcd);

		raw_tcd++;
		head++;

		if (unlikely(!(head & cmpl->size_mask))) {
			done ^= cpu_to_le64(MPNIC_TCD_DONE);
			raw_tcd = &cmpl->desc[0];
		}
	}

	cmpl->head = head;

	if (head0 >= 0)
		mpnic_clean_twq0(nv, napi_budget, qt, false, head0);
}

static void mpnic_bd_prep(struct mpnic_ring *bdq, u32 idx, struct page *page)
{
	dma_addr_t dma = page_pool_get_dma_addr(page);

	bdq->desc[idx] = cpu_to_le64(FIELD_PREP(MPNIC_BD_DESC_ADDR, dma >> 10) |
				     FIELD_PREP(MPNIC_BD_DESC_ID, idx) |
				     FIELD_PREP(MPNIC_BD_DESC_BUF_SZ_LOG2,
						page_shift(page) - 10));
}

static unsigned int mpnic_bdq_desc_unused(struct mpnic_ring *bdq)
{
	return (ALIGN_DOWN(bdq->head - 1, MPNIC_BDQ_BATCH_SIZE) - bdq->tail) &
	       bdq->size_mask;
}

static unsigned int __mpnic_fill_bdq(struct mpnic_ring *bdq)
{
	unsigned int i = bdq->tail;
	unsigned int count;

	for (count = mpnic_bdq_desc_unused(bdq); count; count--) {
		struct page *page;

		page = page_pool_dev_alloc_pages(bdq->page_pool);
		if (!page)
			break;

		bdq->rx_buf[i] = page;
		mpnic_bd_prep(bdq, i, page);

		i++;
		i &= bdq->size_mask;
	}

	return i;
}

static void __mpnic_bdq_commit_tail(struct mpnic_ring *bdq, unsigned int tail)
{
	if (bdq->tail != tail) {
		WRITE_ONCE(bdq->tail, tail);

		writeq(tail, bdq->doorbell);
	}
}

static void mpnic_fill_qt_bdqs(struct mpnic_q_triad *qt)
{
	unsigned int ppq_i = __mpnic_fill_bdq(&qt->sub1);
	unsigned int hpq_i = __mpnic_fill_bdq(&qt->sub0);

	/* Force DMA writes to flush before writing to tail(s) */
	dma_wmb();

	/* Flush out the completions we are done with */
	mpnic_commit_cq_head(&qt->cmpl);

	__mpnic_bdq_commit_tail(&qt->sub0, hpq_i);
	__mpnic_bdq_commit_tail(&qt->sub1, ppq_i);
}

/* Take one of the references batched on the page at @idx. If the device
 * has moved on to a new page, first drop the unused references left on
 * the previous one.
 */
static struct page *
mpnic_page_pool_get(struct mpnic_pg_ctxt *pg_ctxt, struct mpnic_ring *ring,
		    u32 idx)
{
	struct page *page = pg_ctxt->page;

	if (unlikely(pg_ctxt->idx != idx)) {
		if (pg_ctxt->pagecnt_bias &&
		    !page_pool_unref_page(page, pg_ctxt->pagecnt_bias))
			page_pool_put_unrefed_page(page->pp, page, -1, true);

		page = ring->rx_buf[idx];
		page_pool_fragment_page(page, MPNIC_PAGECNT_BIAS_MAX);

		pg_ctxt->page = page;
		pg_ctxt->pagecnt_bias = MPNIC_PAGECNT_BIAS_MAX;
		pg_ctxt->idx = idx;
	}

	pg_ctxt->pagecnt_bias--;

	return page;
}

static void mpnic_flush_pg_ctxt(struct mpnic_pg_ctxt *ctxt, bool napi)
{
	long pagecnt_bias = ctxt->pagecnt_bias;

	if (pagecnt_bias) {
		struct page *page = ctxt->page;

		if (!page_pool_unref_page(page, pagecnt_bias))
			page_pool_put_unrefed_page(page->pp, page, -1, napi);
	}
}

static unsigned int mpnic_hdr_pg_start(unsigned int pg_off)
{
	/* The headroom of the first header may be larger than
	 * MPNIC_RX_HROOM due to alignment. So account for that by just
	 * making the page offset 0 if we are starting at the first header.
	 */
	if (ALIGN(MPNIC_RX_HROOM, 128) > MPNIC_RX_HROOM &&
	    pg_off == ALIGN(MPNIC_RX_HROOM, 128))
		return 0;

	return pg_off - MPNIC_RX_HROOM;
}

static unsigned int mpnic_hdr_pg_end(unsigned int pg_off, unsigned int len)
{
	/* Determine the end of the buffer by finding the start of the next
	 * and then subtracting the headroom from that frame.
	 */
	pg_off += len + MPNIC_RX_TROOM + MPNIC_RX_HROOM;

	return ALIGN(pg_off, 128) - MPNIC_RX_HROOM;
}

static void
mpnic_pkt_prepare(u64 rcd, struct mpnic_rcq_state *state,
		  struct mpnic_q_triad *qt)
{
	unsigned int pg_off = FIELD_GET(MPNIC_RCD_AL_BUFF_OFF, rcd);
	unsigned int pg_idx = FIELD_GET(MPNIC_RCD_AL_BUFF_ID, rcd);
	unsigned int len = FIELD_GET(MPNIC_RCD_AL_BUFF_LEN, rcd);
	bool fin = FIELD_GET(MPNIC_RCD_AL_PAGE_FIN, rcd);
	unsigned int frame_sz, pg_start, pg_end;
	struct xdp_buff *buff = &state->pkt.buff;
	struct page *page;

	pg_start = mpnic_hdr_pg_start(pg_off);

	page = mpnic_page_pool_get(&state->hdr, &qt->sub0, pg_idx);
	WRITE_ONCE(qt->sub0.head, (pg_idx + 1) & qt->sub0.size_mask);

	/* Short-cut the end calculation if the page is fully consumed */
	pg_end = fin ? page_size(page) : mpnic_hdr_pg_end(pg_off, len);
	frame_sz = pg_end - pg_start;

	page_pool_dma_sync_for_cpu(qt->sub0.page_pool, page, pg_start,
				   frame_sz);

	xdp_init_buff(buff, frame_sz, &qt->xdp_rxq);
	xdp_prepare_buff(buff, page_address(page) + pg_start,
			 pg_off - pg_start, len, true);
	net_prefetch(buff->data);

	state->pkt.add_frag_failed = false;
}

static void
mpnic_add_rx_frag(u64 rcd, struct mpnic_rcq_state *state,
		  struct mpnic_q_triad *qt)
{
	unsigned int pg_off = FIELD_GET(MPNIC_RCD_AL_BUFF_OFF, rcd);
	unsigned int pg_idx = FIELD_GET(MPNIC_RCD_AL_BUFF_ID, rcd);
	unsigned int len = FIELD_GET(MPNIC_RCD_AL_BUFF_LEN, rcd);
	bool fin = FIELD_GET(MPNIC_RCD_AL_PAGE_FIN, rcd);
	struct xdp_buff *buff = &state->pkt.buff;
	unsigned int truesz;
	struct page *page;

	page = mpnic_page_pool_get(&state->payld, &qt->sub1, pg_idx);
	WRITE_ONCE(qt->sub1.head, (pg_idx + 1) & qt->sub1.size_mask);

	truesz = (fin ? page_size(page) : ALIGN(pg_off + len, 128)) - pg_off;

	page_pool_dma_sync_for_cpu(qt->sub1.page_pool, page, pg_off, truesz);

	if (!xdp_buff_add_frag(buff, page_to_netmem(page), pg_off, len,
			       truesz)) {
		state->payld.pagecnt_bias++;
		state->pkt.add_frag_failed = true;
	}
}

static void mpnic_put_pkt_buff(struct mpnic_pkt_ctxt *ctxt, bool napi)
{
	struct xdp_buff *buff = &ctxt->buff;
	struct page *page;

	if (!buff->data_hard_start)
		return;

	if (unlikely(xdp_buff_has_frags(buff))) {
		struct skb_shared_info *shinfo;
		int nr_frags;

		shinfo = xdp_get_shared_info_from_buff(buff);
		nr_frags = shinfo->nr_frags;

		while (nr_frags--) {
			page = skb_frag_page(&shinfo->frags[nr_frags]);
			page_pool_put_full_page(page->pp, page, napi);
		}
	}

	page = virt_to_head_page(buff->data_hard_start);
	page_pool_put_full_page(page->pp, page, napi);
}

static int mpnic_clean_rcq(struct mpnic_napi_vector *nv,
			   struct mpnic_q_triad *qt, int budget)
{
	unsigned int packets = 0, bytes = 0, dropped = 0, errors = 0;
	struct mpnic_ring *rcq = &qt->cmpl;
	struct mpnic_rcq_state *state;
	__le64 *raw_rcd, done;
	u32 head = rcq->head;

	done = (head & (rcq->size_mask + 1)) ? 0 : cpu_to_le64(MPNIC_RCD_DONE);
	raw_rcd = &rcq->desc[head & rcq->size_mask];
	state = rcq->state;

	while (packets < budget) {
		u64 rcd;

		if ((*raw_rcd & cpu_to_le64(MPNIC_RCD_DONE)) != done)
			break;

		dma_rmb();

		rcd = le64_to_cpu(*raw_rcd);

		switch (FIELD_GET(MPNIC_RCD_TYPE, rcd)) {
		case MPNIC_RCD_TYPE_HDR_AL:
			if (FIELD_GET(MPNIC_RCD_HDR_SUBTYPE, rcd) ==
			    MPNIC_RCD_HDR_SUBTYPE_HDR)
				mpnic_pkt_prepare(rcd, state, qt);
			break;
		case MPNIC_RCD_TYPE_PAY_AL:
			mpnic_add_rx_frag(rcd, state, qt);
			break;
		case MPNIC_RCD_TYPE_META: {
			struct sk_buff *skb = NULL;

			if (likely(!(rcd &
				     MPNIC_RCD_META_UNCORRECTABLE_ERR_MASK) &&
				   !state->pkt.add_frag_failed)) {
				bytes += xdp_get_buff_len(&state->pkt.buff);
				skb = xdp_build_skb_from_buff(&state->pkt.buff);
			}

			if (likely(skb)) {
				napi_gro_receive(&nv->napi, skb);
			} else {
				mpnic_put_pkt_buff(&state->pkt, true);
				if (rcd & MPNIC_RCD_META_UNCORRECTABLE_ERR_MASK)
					errors++;
				else
					dropped++;
			}

			state->pkt.buff.data_hard_start = NULL;
			packets++;
			break;
		}
		}

		raw_rcd++;
		head++;

		if (unlikely(!(head & rcq->size_mask))) {
			done ^= cpu_to_le64(MPNIC_RCD_DONE);
			raw_rcd = &rcq->desc[0];
		}
	}

	rcq->head = head;

	u64_stats_update_begin(&rcq->stats.syncp);
	u64_stats_add(&rcq->stats.rcq.packets, packets - errors);
	u64_stats_add(&rcq->stats.rcq.bytes, bytes);
	u64_stats_add(&rcq->stats.rcq.dropped, dropped);
	u64_stats_add(&rcq->stats.rcq.errors, errors);
	u64_stats_update_end(&rcq->stats.syncp);

	/* Allocate buffers, force dma_wmb(), and then start writing tails */
	mpnic_fill_qt_bdqs(qt);

	return packets;
}

static int mpnic_poll(struct napi_struct *napi, int budget)
{
	struct mpnic_napi_vector *nv = container_of(napi,
						    struct mpnic_napi_vector,
						    napi);
	int i, j, work_done = 0;

	for (i = 0; i < nv->txt_count; i++)
		mpnic_clean_tcq(nv, &nv->qt[i], budget);

	if (likely(budget))
		for (j = 0; j < nv->rxt_count; j++, i++)
			work_done += mpnic_clean_rcq(nv, &nv->qt[i],
						     budget - work_done);

	for (i = 0; i < nv->txt_count; i++)
		mpnic_commit_cq_head(&nv->qt[i].cmpl);

	if (work_done >= budget)
		return budget;

	if (likely(napi_complete_done(napi, work_done)))
		mpnic_nv_irq_rearm(nv);

	return work_done;
}

static irqreturn_t mpnic_msix_clean_rings(int __always_unused irq, void *data)
{
	struct mpnic_napi_vector *nv = data;

	napi_schedule_irqoff(&nv->napi);

	return IRQ_HANDLED;
}

static void mpnic_aggregate_ring_twq_counters(struct mpnic_base_stats *base,
					      struct mpnic_ring *twq)
{
	base->tx.dropped += u64_stats_read(&twq->stats.twq.dropped);
	base->tx.stop += u64_stats_read(&twq->stats.twq.stop);
}

static void mpnic_aggregate_ring_tcq_counters(struct mpnic_base_stats *base,
					      struct mpnic_ring *tcq)
{
	base->tx.packets += u64_stats_read(&tcq->stats.tcq.packets);
	base->tx.bytes += u64_stats_read(&tcq->stats.tcq.bytes);
	base->tx.wake += u64_stats_read(&tcq->stats.tcq.wake);
}

static void mpnic_aggregate_ring_rcq_counters(struct mpnic_base_stats *base,
					      struct mpnic_ring *rcq)
{
	base->rx.packets += u64_stats_read(&rcq->stats.rcq.packets);
	base->rx.bytes += u64_stats_read(&rcq->stats.rcq.bytes);
	base->rx.dropped += u64_stats_read(&rcq->stats.rcq.dropped);
	base->rx.errors += u64_stats_read(&rcq->stats.rcq.errors);
}

static struct mpnic_base_stats *mpnic_base_stats_prepare(struct mpnic_net *mpn)
{
	struct mpnic_base_stats *old, *new;

	old = netdev_lock_dereference(mpn->stats.base, mpn->netdev);
	new = &mpn->stats.buf[old == &mpn->stats.buf[0]];
	*new = *old;

	return new;
}

void mpnic_stats_fold_rings(struct mpnic_net *mpn)
{
	struct mpnic_base_stats *new;
	int i, j, t;

	new = mpnic_base_stats_prepare(mpn);
	new->num_tx_queues = 0;
	new->num_rx_queues = 0;

	for (i = 0; i < mpn->num_napi; i++) {
		struct mpnic_napi_vector *nv = mpn->napi[i];

		if (!nv)
			continue;

		for (t = 0; t < nv->txt_count; t++) {
			struct mpnic_q_triad *qt = &nv->qt[t];

			mpnic_aggregate_ring_twq_counters(new, &qt->sub0);
			mpnic_aggregate_ring_tcq_counters(new, &qt->cmpl);
		}

		for (j = 0; j < nv->rxt_count; j++, t++)
			mpnic_aggregate_ring_rcq_counters(new, &nv->qt[t].cmpl);
	}

	rcu_assign_pointer(mpn->stats.base, new);
	synchronize_net();
}

void mpnic_stats_attach_rings(struct mpnic_net *mpn)
{
	struct mpnic_base_stats *new;

	new = mpnic_base_stats_prepare(mpn);
	new->num_tx_queues = mpn->num_tx_queues;
	new->num_rx_queues = mpn->num_rx_queues;
	rcu_assign_pointer(mpn->stats.base, new);
	synchronize_net();
}

static void mpnic_free_napi_vector(struct mpnic_net *mpn,
				   struct mpnic_napi_vector *nv)
{
	int i, j;

	for (i = 0; i < nv->txt_count; i++)
		mpn->tx[nv->qt[i].sub0.q_idx] = NULL;

	for (j = 0; j < nv->rxt_count; j++, i++)
		mpn->rx[nv->qt[i].cmpl.q_idx] = NULL;

	mpnic_free_irq(nv->mpd, nv->v_idx, nv);
	netif_napi_del_locked(&nv->napi);
	mpn->napi[nv->v_idx - MPNIC_NON_NAPI_VECTORS] = NULL;
	kfree(nv);
}

void mpnic_free_napi_vectors(struct mpnic_net *mpn)
{
	int i;

	for (i = 0; i < mpn->num_napi; i++)
		if (mpn->napi[i])
			mpnic_free_napi_vector(mpn, mpn->napi[i]);
}

static void mpnic_ring_init(struct mpnic_ring *ring, u32 __iomem *doorbell,
			    int q_idx)
{
	ring->doorbell = doorbell;
	ring->q_idx = q_idx;
	u64_stats_init(&ring->stats.syncp);
}

static int mpnic_alloc_napi_vector(struct mpnic_dev *mpd,
				   struct mpnic_net *mpn, unsigned int idx)
{
	u32 __iomem *uc_addr = READ_ONCE(mpd->uc_addr0);
	struct mpnic_napi_vector *nv;
	int err;

	/* Doorbells are plain pointers into the register window, they have
	 * no way of noticing that it went away.
	 */
	if (!uc_addr)
		return -EIO;

	nv = kzalloc_flex(*nv, qt, 2);
	if (!nv)
		return -ENOMEM;

	nv->txt_count = 1;
	nv->rxt_count = 1;
	nv->mpd = mpd;
	nv->dev = mpd->dev;
	nv->v_idx = idx + MPNIC_NON_NAPI_VECTORS;

	mpn->napi[idx] = nv;
	netif_napi_add_config_locked(mpn->netdev, &nv->napi, mpnic_poll, idx);
	netif_napi_set_irq_locked(&nv->napi,
				  pci_irq_vector(to_pci_dev(mpd->dev),
						 nv->v_idx));

	snprintf(nv->name, sizeof(nv->name), "%s-TxRx-%u",
		 mpn->netdev->name, idx);

	err = mpnic_request_irq(mpd, nv->v_idx, mpnic_msix_clean_rings, 0,
				nv->name, nv);
	if (err)
		goto err_napi_del;

	mpnic_ring_init(&nv->qt[0].sub0, &uc_addr[MPNIC_TWQ_TAIL(idx, 0)], idx);
	mpnic_ring_init(&nv->qt[0].cmpl, &uc_addr[MPNIC_TCQ_HEAD(idx)], idx);
	mpn->tx[idx] = &nv->qt[0].sub0;

	mpnic_ring_init(&nv->qt[1].sub0, &uc_addr[MPNIC_HPQ_TAIL(idx)], idx);
	mpnic_ring_init(&nv->qt[1].sub1, &uc_addr[MPNIC_PPQ_TAIL(idx)], idx);
	mpnic_ring_init(&nv->qt[1].cmpl, &uc_addr[MPNIC_RCQ_HEAD(idx)], idx);
	mpn->rx[idx] = &nv->qt[1].cmpl;

	return 0;

err_napi_del:
	netif_napi_del_locked(&nv->napi);
	mpn->napi[idx] = NULL;
	kfree(nv);
	return err;
}

int mpnic_alloc_napi_vectors(struct mpnic_net *mpn)
{
	unsigned int i;
	int err;

	for (i = 0; i < mpn->num_napi; i++) {
		err = mpnic_alloc_napi_vector(mpn->mpd, mpn, i);
		if (err)
			goto err_free_vectors;
	}

	return 0;

err_free_vectors:
	mpnic_free_napi_vectors(mpn);

	return err;
}

static void mpnic_free_ring_resources(struct device *dev,
				      struct mpnic_ring *ring)
{
	kvfree(ring->buffer);
	ring->buffer = NULL;

	/* If size is not set there are no descriptors present */
	if (!ring->size)
		return;

	dma_free_coherent(dev, ring->size, ring->desc, ring->dma);
	ring->size_mask = 0;
	ring->size = 0;
}

static int mpnic_alloc_ring_desc(struct mpnic_net *mpn,
				 struct mpnic_ring *ring, u32 count)
{
	struct device *dev = mpn->netdev->dev.parent;
	size_t size;

	size = ALIGN(array_size(sizeof(*ring->desc), count), 4096);

	ring->desc = dma_alloc_coherent(dev, size, &ring->dma,
					GFP_KERNEL | __GFP_NOWARN);
	if (!ring->desc)
		return -ENOMEM;

	ring->size_mask = count - 1;
	ring->size = size;

	return 0;
}

static void mpnic_free_tx_qt_resources(struct mpnic_net *mpn,
				       struct mpnic_q_triad *qt)
{
	struct device *dev = mpn->netdev->dev.parent;

	mpnic_free_ring_resources(dev, &qt->cmpl);
	mpnic_free_ring_resources(dev, &qt->sub0);
}

static int mpnic_alloc_tx_qt_resources(struct mpnic_net *mpn,
				       struct mpnic_q_triad *qt)
{
	int err;

	err = mpnic_alloc_ring_desc(mpn, &qt->sub0, mpn->txq_size);
	if (err)
		return err;

	qt->sub0.tx_buf = kvzalloc_objs(*qt->sub0.tx_buf, mpn->txq_size,
					GFP_KERNEL | __GFP_NOWARN);
	if (!qt->sub0.tx_buf) {
		err = -ENOMEM;
		goto err_free_qt;
	}

	err = mpnic_alloc_ring_desc(mpn, &qt->cmpl, mpn->txq_size);
	if (err)
		goto err_free_qt;

	return 0;

err_free_qt:
	mpnic_free_tx_qt_resources(mpn, qt);
	return err;
}

static int
mpnic_alloc_qt_page_pool(struct mpnic_net *mpn, struct mpnic_napi_vector *nv,
			 struct mpnic_q_triad *qt)
{
	struct page_pool_params pp_params = {
		.flags		= PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV,
		.pool_size	= min(mpn->hpq_size + mpn->ppq_size, 32768u),
		.nid		= NUMA_NO_NODE,
		.dev		= nv->dev,
		.dma_dir	= DMA_FROM_DEVICE,
		.max_len	= PAGE_SIZE,
		.napi		= &nv->napi,
		.netdev		= mpn->netdev,
		.queue_idx	= qt->cmpl.q_idx,
	};
	struct page_pool *pp;

	pp = page_pool_create(&pp_params);
	if (IS_ERR(pp))
		return PTR_ERR(pp);

	qt->sub0.page_pool = pp;
	page_pool_get(pp);
	qt->sub1.page_pool = pp;

	return 0;
}

static void mpnic_free_rx_qt_resources(struct mpnic_net *mpn,
				       struct mpnic_q_triad *qt)
{
	struct device *dev = mpn->netdev->dev.parent;

	mpnic_free_ring_resources(dev, &qt->cmpl);
	mpnic_free_ring_resources(dev, &qt->sub1);
	mpnic_free_ring_resources(dev, &qt->sub0);

	if (xdp_rxq_info_is_reg(&qt->xdp_rxq)) {
		xdp_rxq_info_unreg(&qt->xdp_rxq);
		page_pool_destroy(qt->sub1.page_pool);
		page_pool_destroy(qt->sub0.page_pool);
	}
}

static int mpnic_alloc_rx_qt_resources(struct mpnic_net *mpn,
				       struct mpnic_napi_vector *nv,
				       struct mpnic_q_triad *qt)
{
	int err;

	err = mpnic_alloc_qt_page_pool(mpn, nv, qt);
	if (err)
		return err;

	err = xdp_rxq_info_reg(&qt->xdp_rxq, mpn->netdev, qt->cmpl.q_idx,
			       nv->napi.napi_id);
	if (err)
		goto err_free_page_pool;

	err = xdp_rxq_info_reg_mem_model(&qt->xdp_rxq, MEM_TYPE_PAGE_POOL,
					 qt->sub0.page_pool);
	if (err)
		goto err_unreg_rxq;

	err = mpnic_alloc_ring_desc(mpn, &qt->sub0, mpn->hpq_size);
	if (err)
		goto err_unreg_mm;

	qt->sub0.rx_buf = kvzalloc_objs(*qt->sub0.rx_buf, mpn->hpq_size,
					GFP_KERNEL | __GFP_NOWARN);
	if (!qt->sub0.rx_buf) {
		err = -ENOMEM;
		goto err_free_qt;
	}

	err = mpnic_alloc_ring_desc(mpn, &qt->sub1, mpn->ppq_size);
	if (err)
		goto err_free_qt;

	qt->sub1.rx_buf = kvzalloc_objs(*qt->sub1.rx_buf, mpn->ppq_size,
					GFP_KERNEL | __GFP_NOWARN);
	if (!qt->sub1.rx_buf) {
		err = -ENOMEM;
		goto err_free_qt;
	}

	err = mpnic_alloc_ring_desc(mpn, &qt->cmpl, mpn->rcq_size);
	if (err)
		goto err_free_qt;

	qt->cmpl.state = kvzalloc_obj(*qt->cmpl.state,
				      GFP_KERNEL | __GFP_NOWARN);
	if (!qt->cmpl.state) {
		err = -ENOMEM;
		goto err_free_qt;
	}

	return 0;

err_free_qt:
	mpnic_free_rx_qt_resources(mpn, qt);
	return err;
err_unreg_mm:
	xdp_rxq_info_unreg_mem_model(&qt->xdp_rxq);
err_unreg_rxq:
	xdp_rxq_info_unreg(&qt->xdp_rxq);
err_free_page_pool:
	page_pool_destroy(qt->sub1.page_pool);
	page_pool_destroy(qt->sub0.page_pool);
	return err;
}

static void mpnic_free_nv_resources(struct mpnic_net *mpn,
				    struct mpnic_napi_vector *nv)
{
	int i, j;

	for (i = 0; i < nv->txt_count; i++)
		mpnic_free_tx_qt_resources(mpn, &nv->qt[i]);

	for (j = 0; j < nv->rxt_count; j++, i++)
		mpnic_free_rx_qt_resources(mpn, &nv->qt[i]);
}

static int mpnic_alloc_nv_resources(struct mpnic_net *mpn,
				    struct mpnic_napi_vector *nv)
{
	int i, j, err;

	for (i = 0; i < nv->txt_count; i++) {
		err = mpnic_alloc_tx_qt_resources(mpn, &nv->qt[i]);
		if (err)
			goto err_free_qt_resources;
	}

	for (j = 0; j < nv->rxt_count; j++, i++) {
		err = mpnic_alloc_rx_qt_resources(mpn, nv, &nv->qt[i]);
		if (err)
			goto err_free_qt_resources;
	}

	return 0;

err_free_qt_resources:
	while (i--) {
		if (i < nv->txt_count)
			mpnic_free_tx_qt_resources(mpn, &nv->qt[i]);
		else
			mpnic_free_rx_qt_resources(mpn, &nv->qt[i]);
	}
	return err;
}

void mpnic_free_resources(struct mpnic_net *mpn)
{
	int i;

	for (i = 0; i < mpn->num_napi; i++)
		mpnic_free_nv_resources(mpn, mpn->napi[i]);
}

int mpnic_alloc_resources(struct mpnic_net *mpn)
{
	int i, err;

	for (i = 0; i < mpn->num_napi; i++) {
		err = mpnic_alloc_nv_resources(mpn, mpn->napi[i]);
		if (err)
			goto err_free_resources;
	}

	return 0;

err_free_resources:
	while (i--)
		mpnic_free_nv_resources(mpn, mpn->napi[i]);

	return err;
}

static void mpnic_set_netif_napi(struct mpnic_napi_vector *nv,
				 struct napi_struct *napi)
{
	int i, j;

	for (i = 0; i < nv->txt_count; i++)
		netif_queue_set_napi(nv->napi.dev, nv->qt[i].sub0.q_idx,
				     NETDEV_QUEUE_TYPE_TX, napi);

	for (j = 0; j < nv->rxt_count; j++, i++)
		netif_queue_set_napi(nv->napi.dev, nv->qt[i].cmpl.q_idx,
				     NETDEV_QUEUE_TYPE_RX, napi);
}

int mpnic_set_netif_queues(struct mpnic_net *mpn)
{
	int i, err;

	err = netif_set_real_num_queues(mpn->netdev, mpn->num_tx_queues,
					mpn->num_rx_queues);
	if (err)
		return err;

	for (i = 0; i < mpn->num_napi; i++)
		mpnic_set_netif_napi(mpn->napi[i], &mpn->napi[i]->napi);

	return 0;
}

void mpnic_reset_netif_queues(struct mpnic_net *mpn)
{
	int i;

	for (i = 0; i < mpn->num_napi; i++)
		mpnic_set_netif_napi(mpn->napi[i], NULL);
}

static void mpnic_enable_twq(struct mpnic_dev *mpd, struct mpnic_ring *twq)
{
	u32 log_size = fls(twq->size_mask);
	u32 i = twq->q_idx;

	/* Reset head/tail */
	mpnic_wr64(mpd, MPNIC_TWQ_CTL(i, 0), MPNIC_TWQ_CTL_RESET);
	twq->tail = 0;
	twq->head = 0;
	twq->deferred_meta = -1;

	/* Store descriptor ring address and size */
	mpnic_wr64(mpd, MPNIC_TWQ_BASE_ADDR(i, 0), twq->dma);
	mpnic_wr64(mpd, MPNIC_TWQ_SIZE(i, 0), log_size & MPNIC_TWQ_SIZE_SIZE);

	mpnic_wr64(mpd, MPNIC_TWQ_CTL(i, 0), MPNIC_TWQ_CTL_ENABLE);
}

static void mpnic_enable_tcq(struct mpnic_dev *mpd,
			     struct mpnic_napi_vector *nv,
			     struct mpnic_ring *tcq)
{
	u32 log_size = fls(tcq->size_mask);
	u32 i = tcq->q_idx;

	/* Reset head/tail */
	mpnic_wr64(mpd, MPNIC_TCQ_CTL(i), MPNIC_TCQ_CTL_RESET);
	tcq->tail = 0;
	tcq->head = 0;

	/* Store descriptor ring address and size */
	mpnic_wr64(mpd, MPNIC_TCQ_BASE_ADDR(i), tcq->dma);
	mpnic_wr64(mpd, MPNIC_TCQ_SIZE(i), log_size & MPNIC_TCQ_SIZE_SIZE);

	/* Store interrupt information for the completion queue */
	mpnic_wr64(mpd, MPNIC_TIM_CTL(i), nv->v_idx);
	mpnic_wr64(mpd, MPNIC_TIM_INTR_MASK(i), 0);

	mpnic_wr64(mpd, MPNIC_TCQ_CTL(i), MPNIC_TCQ_CTL_ENABLE);
}

static void mpnic_enable_bdq(struct mpnic_dev *mpd, struct mpnic_ring *hpq,
			     struct mpnic_ring *ppq)
{
	u32 hpq_log_size = fls(hpq->size_mask);
	u32 ppq_log_size = fls(ppq->size_mask);
	u32 i = hpq->q_idx;

	/* Reset head/tail */
	mpnic_wr64(mpd, MPNIC_BDQ_CTL(i), MPNIC_BDQ_CTL_RESET);
	hpq->tail = 0;
	hpq->head = 0;
	ppq->tail = 0;
	ppq->head = 0;

	/* Store descriptor ring addresses and sizes */
	mpnic_wr64(mpd, MPNIC_HPQ_BASE_ADDR(i), hpq->dma);
	mpnic_wr64(mpd, MPNIC_HPQ_SIZE(i), hpq_log_size & MPNIC_HPQ_SIZE_SIZE);
	mpnic_wr64(mpd, MPNIC_PPQ_BASE_ADDR(i), ppq->dma);
	mpnic_wr64(mpd, MPNIC_PPQ_SIZE(i), ppq_log_size & MPNIC_PPQ_SIZE_SIZE);

	mpnic_wr64(mpd, MPNIC_BDQ_CTL(i),
		   MPNIC_BDQ_CTL_ENABLE | MPNIC_BDQ_CTL_ENABLE_PPQ);
}

static void mpnic_set_rde_cfg(struct mpnic_dev *mpd, struct mpnic_ring *rcq)
{
	BUILD_BUG_ON(FIELD_MAX(MPNIC_RDE_CFG_MIN_HEAD_ROOM) < MPNIC_RX_HROOM);
	BUILD_BUG_ON(FIELD_MAX(MPNIC_RDE_CFG_MIN_TAIL_ROOM) < MPNIC_RX_TROOM);

	mpnic_wr64(mpd, MPNIC_RDE_CFG(rcq->q_idx),
		   FIELD_PREP(MPNIC_RDE_CFG_MIN_HEAD_ROOM, MPNIC_RX_HROOM) |
		   FIELD_PREP(MPNIC_RDE_CFG_MIN_TAIL_ROOM, MPNIC_RX_TROOM) |
		   FIELD_PREP(MPNIC_RDE_CFG_MAX_HEADER_BYTES,
			      MPNIC_RX_MAX_HDR));
}

static void mpnic_enable_rcq(struct mpnic_dev *mpd,
			     struct mpnic_napi_vector *nv,
			     struct mpnic_ring *rcq)
{
	u32 log_size = fls(rcq->size_mask);
	u32 i = rcq->q_idx;

	mpnic_set_rde_cfg(mpd, rcq);

	/* Reset head/tail */
	mpnic_wr64(mpd, MPNIC_RCQ_CTL(i), MPNIC_RCQ_CTL_RESET);
	rcq->head = 0;
	rcq->tail = 0;

	/* Store descriptor ring address and size */
	mpnic_wr64(mpd, MPNIC_RCQ_BASE_ADDR(i), rcq->dma);
	mpnic_wr64(mpd, MPNIC_RCQ_SIZE(i), log_size & MPNIC_RCQ_SIZE_SIZE);

	/* Store interrupt information for the completion queue */
	mpnic_wr64(mpd, MPNIC_RIM_CTL(i), nv->v_idx);
	mpnic_wr64(mpd, MPNIC_RIM_INTR_MASK(i), 0);

	mpnic_wr64(mpd, MPNIC_RCQ_CTL(i), MPNIC_RCQ_CTL_ENABLE);
}

void mpnic_enable(struct mpnic_net *mpn)
{
	struct mpnic_dev *mpd = mpn->mpd;
	int i, j, t;

	for (i = 0; i < mpn->num_napi; i++) {
		struct mpnic_napi_vector *nv = mpn->napi[i];

		for (t = 0; t < nv->txt_count; t++) {
			mpnic_enable_twq(mpd, &nv->qt[t].sub0);
			mpnic_enable_tcq(mpd, nv, &nv->qt[t].cmpl);
		}

		for (j = 0; j < nv->rxt_count; j++, t++) {
			mpnic_enable_bdq(mpd, &nv->qt[t].sub0, &nv->qt[t].sub1);
			mpnic_enable_rcq(mpd, nv, &nv->qt[t].cmpl);
		}
	}

	mpnic_wrfl(mpd);
}

static void mpnic_disable_twq(struct mpnic_dev *mpd, struct mpnic_ring *txr)
{
	u64 twq_ctl = mpnic_rd64(mpd, MPNIC_TWQ_CTL(txr->q_idx, 0));

	twq_ctl &= ~MPNIC_TWQ_CTL_ENABLE;
	mpnic_wr64(mpd, MPNIC_TWQ_CTL(txr->q_idx, 0), twq_ctl);
}

static void mpnic_disable_tcq(struct mpnic_dev *mpd, struct mpnic_ring *txr)
{
	mpnic_wr64(mpd, MPNIC_TCQ_CTL(txr->q_idx), 0);
	mpnic_wr64(mpd, MPNIC_TIM_INTR_MASK(txr->q_idx),
		   MPNIC_TIM_INTR_MASK_MASK);
}

static void mpnic_disable_bdq(struct mpnic_dev *mpd, struct mpnic_ring *hpq)
{
	u64 bdq_ctl = mpnic_rd64(mpd, MPNIC_BDQ_CTL(hpq->q_idx));

	bdq_ctl &= ~(MPNIC_BDQ_CTL_ENABLE | MPNIC_BDQ_CTL_ENABLE_PPQ);
	mpnic_wr64(mpd, MPNIC_BDQ_CTL(hpq->q_idx), bdq_ctl);
}

static void mpnic_disable_rcq(struct mpnic_dev *mpd, struct mpnic_ring *rcq)
{
	mpnic_wr64(mpd, MPNIC_RCQ_CTL(rcq->q_idx), 0);
	mpnic_wr64(mpd, MPNIC_RIM_INTR_MASK(rcq->q_idx),
		   MPNIC_RIM_INTR_MASK_MASK);
}

void mpnic_disable(struct mpnic_net *mpn)
{
	struct mpnic_dev *mpd = mpn->mpd;
	int i, j, t;

	for (i = 0; i < mpn->num_napi; i++) {
		struct mpnic_napi_vector *nv = mpn->napi[i];

		for (t = 0; t < nv->txt_count; t++) {
			mpnic_disable_twq(mpd, &nv->qt[t].sub0);
			mpnic_disable_tcq(mpd, &nv->qt[t].cmpl);
		}

		for (j = 0; j < nv->rxt_count; j++, t++) {
			mpnic_disable_bdq(mpd, &nv->qt[t].sub0);
			mpnic_disable_rcq(mpd, &nv->qt[t].cmpl);
		}
	}

	mpnic_wrfl(mpd);
}

struct mpnic_idle_regs {
	u32 reg_base;
	u8 reg_cnt;
	char name[4];
};

static u32 mpnic_non_idle_queues(struct mpnic_dev *mpd,
				 const struct mpnic_idle_regs *regs,
				 unsigned int nregs)
{
	u32 non_idle_bitmap = 0;
	unsigned int i, j;

	for (i = 0; i < nregs; i++) {
		for (j = 0; j < regs[i].reg_cnt; j++) {
			if (mpnic_rd64(mpd, regs[i].reg_base + 2 * j) !=
			    ~0ULL) {
				non_idle_bitmap |= BIT(i);
				break;
			}
		}
	}

	return non_idle_bitmap;
}

static void mpnic_idle_dump(struct mpnic_dev *mpd,
			    const struct mpnic_idle_regs *regs,
			    unsigned int nregs, u32 non_idle_bitmap, int err)
{
	unsigned int i, j;

	dev_err(mpd->dev, "error waiting for queues idle %d\n", err);
	for (i = 0; i < nregs; i++) {
		if (!(non_idle_bitmap & BIT(i)))
			continue;

		dev_err(mpd->dev, "%s block not idle:\n", regs[i].name);
		for (j = 0; j < regs[i].reg_cnt; j++)
			dev_err(mpd->dev, "  0x%04x: %016llx\n",
				regs[i].reg_base + 2 * j,
				mpnic_rd64(mpd, regs[i].reg_base + 2 * j));
	}
}

void mpnic_wait_all_queues_idle(struct mpnic_dev *mpd)
{
	static const struct mpnic_idle_regs queues[] = {
		{ MPNIC_TWQ_IDLE(0), MPNIC_TWQ_IDLE_CNT, "TWQ" },
		{ MPNIC_TQS_IDLE(0), MPNIC_TQS_IDLE_CNT, "TQS" },
		{ MPNIC_TDE_IDLE(0), MPNIC_TDE_IDLE_CNT, "TDE" },
		{ MPNIC_TCQ_IDLE(0), MPNIC_TCQ_IDLE_CNT, "TCQ" },
		{ MPNIC_HPQ_IDLE(0), MPNIC_HPQ_IDLE_CNT, "HPQ" },
		{ MPNIC_PPQ_IDLE(0), MPNIC_PPQ_IDLE_CNT, "PPQ" },
		{ MPNIC_RCQ_IDLE(0), MPNIC_RCQ_IDLE_CNT, "RCQ" },
	};
	u32 non_idle_bitmap;
	int err;

	err = read_poll_timeout(mpnic_non_idle_queues, non_idle_bitmap,
				!non_idle_bitmap, 20, 500000, false, mpd,
				queues, ARRAY_SIZE(queues));
	if (err)
		mpnic_idle_dump(mpd, queues, ARRAY_SIZE(queues),
				non_idle_bitmap, err);
}

static void mpnic_clean_bdq(struct mpnic_ring *bdq)
{
	unsigned int head = bdq->head;

	while (head != bdq->tail) {
		struct page *page = bdq->rx_buf[head];

		page_pool_put_full_page(page->pp, page, false);

		head++;
		head &= bdq->size_mask;
	}

	bdq->head = head;
}

void mpnic_flush(struct mpnic_net *mpn)
{
	int i, j, t;

	for (i = 0; i < mpn->num_napi; i++) {
		struct mpnic_napi_vector *nv = mpn->napi[i];

		for (t = 0; t < nv->txt_count; t++) {
			struct mpnic_q_triad *qt = &nv->qt[t];
			struct netdev_queue *txq;

			/* Clean the work queue of unprocessed work */
			mpnic_clean_twq0(nv, 0, qt, true, qt->sub0.tail);

			txq = netdev_get_tx_queue(mpn->netdev, qt->sub0.q_idx);
			netdev_tx_reset_queue(txq);
		}

		for (j = 0; j < nv->rxt_count; j++, t++) {
			struct mpnic_q_triad *qt = &nv->qt[t];
			struct mpnic_rcq_state *state = qt->cmpl.state;

			/* Release the partially assembled frame and the
			 * pages the queues are still handing out.
			 */
			mpnic_put_pkt_buff(&state->pkt, false);
			mpnic_flush_pg_ctxt(&state->hdr, false);
			mpnic_flush_pg_ctxt(&state->payld, false);
			memset(state, 0, sizeof(*state));

			mpnic_clean_bdq(&qt->sub0);
			mpnic_clean_bdq(&qt->sub1);
		}
	}
}

void mpnic_fill(struct mpnic_net *mpn)
{
	int i, j, t;

	for (i = 0; i < mpn->num_napi; i++) {
		struct mpnic_napi_vector *nv = mpn->napi[i];

		for (j = 0, t = nv->txt_count; j < nv->rxt_count; j++, t++) {
			struct mpnic_q_triad *qt = &nv->qt[t];
			struct mpnic_rcq_state *state = qt->cmpl.state;

			/* Point the page contexts at an index the device
			 * cannot report, so the first buffer coming out of
			 * either queue is not taken for a page we hold.
			 */
			state->hdr.idx = UINT_MAX;
			state->payld.idx = UINT_MAX;

			mpnic_fill_qt_bdqs(qt);
		}
	}
}

void mpnic_napi_disable(struct mpnic_net *mpn)
{
	int i;

	for (i = 0; i < mpn->num_napi; i++) {
		napi_disable_locked(&mpn->napi[i]->napi);

		mpnic_nv_irq_disable(mpn->napi[i]);
	}
}

void mpnic_napi_enable(struct mpnic_net *mpn)
{
	int i;

	for (i = 0; i < mpn->num_napi; i++)
		napi_enable_locked(&mpn->napi[i]->napi);

	/* Force the first interrupt on each vector to guarantee that any
	 * completions posted during bringup are processed.
	 */
	for (i = 0; i < mpn->num_napi; i++)
		mpnic_nv_irq_trigger(mpn->napi[i]);

	mpnic_wrfl(mpn->mpd);
}

void mpnic_napi_depletion_check(struct mpnic_net *mpn)
{
	int i, j, t;

	for (i = 0; i < mpn->num_napi; i++) {
		struct mpnic_napi_vector *nv = mpn->napi[i];

		for (t = nv->txt_count, j = 0; j < nv->rxt_count; j++, t++) {
			/* Check if BDs posted covers a max sized frame
			 *  + 1 BD held by RDE as a spare
			 *  + 1 BD of extra safety margin
			 */
			if (mpnic_desc_used(&nv->qt[t].sub0) <
			    MPNIC_RX_HPQ_DROP_THRS + 2 ||
			    mpnic_desc_used(&nv->qt[t].sub1) <
			    MPNIC_RX_PPQ_DROP_THRS + 2) {
				mpnic_nv_irq_trigger(nv);
				break;
			}
		}
	}

	mpnic_wrfl(mpn->mpd);
}
