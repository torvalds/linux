// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/bitfield.h>
#include <linux/iopoll.h>
#include <linux/pci.h>
#include <net/netdev_queues.h>
#include <net/page_pool/helpers.h>

#include "fbnic.h"
#include "fbnic_csr.h"
#include "fbnic_netdev.h"
#include "fbnic_txrx.h"

enum {
	FBNIC_XMIT_CB_TS	= 0x01,
};

struct fbnic_xmit_cb {
	u32 bytecount;
	u8 desc_count;
	u8 flags;
	int hw_head;
};

#define FBNIC_XMIT_CB(__skb) ((struct fbnic_xmit_cb *)((__skb)->cb))

static u32 __iomem *fbnic_ring_csr_base(const struct fbnic_ring *ring)
{
	unsigned long csr_base = (unsigned long)ring->doorbell;

	csr_base &= ~(FBNIC_QUEUE_STRIDE * sizeof(u32) - 1);

	return (u32 __iomem *)csr_base;
}

static u32 fbnic_ring_rd32(struct fbnic_ring *ring, unsigned int csr)
{
	u32 __iomem *csr_base = fbnic_ring_csr_base(ring);

	return readl(csr_base + csr);
}

static void fbnic_ring_wr32(struct fbnic_ring *ring, unsigned int csr, u32 val)
{
	u32 __iomem *csr_base = fbnic_ring_csr_base(ring);

	writel(val, csr_base + csr);
}

/**
 * fbnic_ts40_to_ns() - convert descriptor timestamp to PHC time
 * @fbn: netdev priv of the FB NIC
 * @ts40: timestamp read from a descriptor
 *
 * Return: u64 value of PHC time in nanoseconds
 *
 * Convert truncated 40 bit device timestamp as read from a descriptor
 * to the full PHC time in nanoseconds.
 */
static __maybe_unused u64 fbnic_ts40_to_ns(struct fbnic_net *fbn, u64 ts40)
{
	unsigned int s;
	u64 time_ns;
	s64 offset;
	u8 ts_top;
	u32 high;

	do {
		s = u64_stats_fetch_begin(&fbn->time_seq);
		offset = READ_ONCE(fbn->time_offset);
	} while (u64_stats_fetch_retry(&fbn->time_seq, s));

	high = READ_ONCE(fbn->time_high);

	/* Bits 63..40 from periodic clock reads, 39..0 from ts40 */
	time_ns = (u64)(high >> 8) << 40 | ts40;

	/* Compare bits 32-39 between periodic reads and ts40,
	 * see if HW clock may have wrapped since last read. We are sure
	 * that periodic reads are always at least ~1 minute behind, so
	 * this logic works perfectly fine.
	 */
	ts_top = ts40 >> 32;
	if (ts_top < (u8)high && (u8)high - ts_top > U8_MAX / 2)
		time_ns += 1ULL << 40;

	return time_ns + offset;
}

static unsigned int fbnic_desc_unused(struct fbnic_ring *ring)
{
	return (ring->head - ring->tail - 1) & ring->size_mask;
}

static unsigned int fbnic_desc_used(struct fbnic_ring *ring)
{
	return (ring->tail - ring->head) & ring->size_mask;
}

static struct netdev_queue *txring_txq(const struct net_device *dev,
				       const struct fbnic_ring *ring)
{
	return netdev_get_tx_queue(dev, ring->q_idx);
}

static int fbnic_maybe_stop_tx(const struct net_device *dev,
			       struct fbnic_ring *ring,
			       const unsigned int size)
{
	struct netdev_queue *txq = txring_txq(dev, ring);
	int res;

	res = netif_txq_maybe_stop(txq, fbnic_desc_unused(ring), size,
				   FBNIC_TX_DESC_WAKEUP);

	return !res;
}

static bool fbnic_tx_sent_queue(struct sk_buff *skb, struct fbnic_ring *ring)
{
	struct netdev_queue *dev_queue = txring_txq(skb->dev, ring);
	unsigned int bytecount = FBNIC_XMIT_CB(skb)->bytecount;
	bool xmit_more = netdev_xmit_more();

	/* TBD: Request completion more often if xmit_more becomes large */

	return __netdev_tx_sent_queue(dev_queue, bytecount, xmit_more);
}

static void fbnic_unmap_single_twd(struct device *dev, __le64 *twd)
{
	u64 raw_twd = le64_to_cpu(*twd);
	unsigned int len;
	dma_addr_t dma;

	dma = FIELD_GET(FBNIC_TWD_ADDR_MASK, raw_twd);
	len = FIELD_GET(FBNIC_TWD_LEN_MASK, raw_twd);

	dma_unmap_single(dev, dma, len, DMA_TO_DEVICE);
}

static void fbnic_unmap_page_twd(struct device *dev, __le64 *twd)
{
	u64 raw_twd = le64_to_cpu(*twd);
	unsigned int len;
	dma_addr_t dma;

	dma = FIELD_GET(FBNIC_TWD_ADDR_MASK, raw_twd);
	len = FIELD_GET(FBNIC_TWD_LEN_MASK, raw_twd);

	dma_unmap_page(dev, dma, len, DMA_TO_DEVICE);
}

#define FBNIC_TWD_TYPE(_type) \
	cpu_to_le64(FIELD_PREP(FBNIC_TWD_TYPE_MASK, FBNIC_TWD_TYPE_##_type))

static bool fbnic_tx_tstamp(struct sk_buff *skb)
{
	struct fbnic_net *fbn;

	if (!unlikely(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP))
		return false;

	fbn = netdev_priv(skb->dev);
	if (fbn->hwtstamp_config.tx_type == HWTSTAMP_TX_OFF)
		return false;

	skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
	FBNIC_XMIT_CB(skb)->flags |= FBNIC_XMIT_CB_TS;
	FBNIC_XMIT_CB(skb)->hw_head = -1;

	return true;
}

static bool
fbnic_tx_offloads(struct fbnic_ring *ring, struct sk_buff *skb, __le64 *meta)
{
	unsigned int l2len, i3len;

	if (fbnic_tx_tstamp(skb))
		*meta |= cpu_to_le64(FBNIC_TWD_FLAG_REQ_TS);

	if (unlikely(skb->ip_summed != CHECKSUM_PARTIAL))
		return false;

	l2len = skb_mac_header_len(skb);
	i3len = skb_checksum_start(skb) - skb_network_header(skb);

	*meta |= cpu_to_le64(FIELD_PREP(FBNIC_TWD_CSUM_OFFSET_MASK,
					skb->csum_offset / 2));

	*meta |= cpu_to_le64(FBNIC_TWD_FLAG_REQ_CSO);

	*meta |= cpu_to_le64(FIELD_PREP(FBNIC_TWD_L2_HLEN_MASK, l2len / 2) |
			     FIELD_PREP(FBNIC_TWD_L3_IHLEN_MASK, i3len / 2));
	return false;
}

static void
fbnic_rx_csum(u64 rcd, struct sk_buff *skb, struct fbnic_ring *rcq)
{
	skb_checksum_none_assert(skb);

	if (unlikely(!(skb->dev->features & NETIF_F_RXCSUM)))
		return;

	if (FIELD_GET(FBNIC_RCD_META_L4_CSUM_UNNECESSARY, rcd)) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	} else {
		u16 csum = FIELD_GET(FBNIC_RCD_META_L2_CSUM_MASK, rcd);

		skb->ip_summed = CHECKSUM_COMPLETE;
		skb->csum = (__force __wsum)csum;
	}
}

static bool
fbnic_tx_map(struct fbnic_ring *ring, struct sk_buff *skb, __le64 *meta)
{
	struct device *dev = skb->dev->dev.parent;
	unsigned int tail = ring->tail, first;
	unsigned int size, data_len;
	skb_frag_t *frag;
	dma_addr_t dma;
	__le64 *twd;

	ring->tx_buf[tail] = skb;

	tail++;
	tail &= ring->size_mask;
	first = tail;

	size = skb_headlen(skb);
	data_len = skb->data_len;

	if (size > FIELD_MAX(FBNIC_TWD_LEN_MASK))
		goto dma_error;

	dma = dma_map_single(dev, skb->data, size, DMA_TO_DEVICE);

	for (frag = &skb_shinfo(skb)->frags[0];; frag++) {
		twd = &ring->desc[tail];

		if (dma_mapping_error(dev, dma))
			goto dma_error;

		*twd = cpu_to_le64(FIELD_PREP(FBNIC_TWD_ADDR_MASK, dma) |
				   FIELD_PREP(FBNIC_TWD_LEN_MASK, size) |
				   FIELD_PREP(FBNIC_TWD_TYPE_MASK,
					      FBNIC_TWD_TYPE_AL));

		tail++;
		tail &= ring->size_mask;

		if (!data_len)
			break;

		size = skb_frag_size(frag);
		data_len -= size;

		if (size > FIELD_MAX(FBNIC_TWD_LEN_MASK))
			goto dma_error;

		dma = skb_frag_dma_map(dev, frag, 0, size, DMA_TO_DEVICE);
	}

	*twd |= FBNIC_TWD_TYPE(LAST_AL);

	FBNIC_XMIT_CB(skb)->desc_count = ((twd - meta) + 1) & ring->size_mask;

	ring->tail = tail;

	/* Record SW timestamp */
	skb_tx_timestamp(skb);

	/* Verify there is room for another packet */
	fbnic_maybe_stop_tx(skb->dev, ring, FBNIC_MAX_SKB_DESC);

	if (fbnic_tx_sent_queue(skb, ring)) {
		*meta |= cpu_to_le64(FBNIC_TWD_FLAG_REQ_COMPLETION);

		/* Force DMA writes to flush before writing to tail */
		dma_wmb();

		writel(tail, ring->doorbell);
	}

	return false;
dma_error:
	if (net_ratelimit())
		netdev_err(skb->dev, "TX DMA map failed\n");

	while (tail != first) {
		tail--;
		tail &= ring->size_mask;
		twd = &ring->desc[tail];
		if (tail == first)
			fbnic_unmap_single_twd(dev, twd);
		else
			fbnic_unmap_page_twd(dev, twd);
	}

	return true;
}

#define FBNIC_MIN_FRAME_LEN	60

static netdev_tx_t
fbnic_xmit_frame_ring(struct sk_buff *skb, struct fbnic_ring *ring)
{
	__le64 *meta = &ring->desc[ring->tail];
	u16 desc_needed;

	if (skb_put_padto(skb, FBNIC_MIN_FRAME_LEN))
		goto err_count;

	/* Need: 1 descriptor per page,
	 *       + 1 desc for skb_head,
	 *       + 2 desc for metadata and timestamp metadata
	 *       + 7 desc gap to keep tail from touching head
	 * otherwise try next time
	 */
	desc_needed = skb_shinfo(skb)->nr_frags + 10;
	if (fbnic_maybe_stop_tx(skb->dev, ring, desc_needed))
		return NETDEV_TX_BUSY;

	*meta = cpu_to_le64(FBNIC_TWD_FLAG_DEST_MAC);

	/* Write all members within DWORD to condense this into 2 4B writes */
	FBNIC_XMIT_CB(skb)->bytecount = skb->len;
	FBNIC_XMIT_CB(skb)->desc_count = 0;

	if (fbnic_tx_offloads(ring, skb, meta))
		goto err_free;

	if (fbnic_tx_map(ring, skb, meta))
		goto err_free;

	return NETDEV_TX_OK;

err_free:
	dev_kfree_skb_any(skb);
err_count:
	u64_stats_update_begin(&ring->stats.syncp);
	ring->stats.dropped++;
	u64_stats_update_end(&ring->stats.syncp);
	return NETDEV_TX_OK;
}

netdev_tx_t fbnic_xmit_frame(struct sk_buff *skb, struct net_device *dev)
{
	struct fbnic_net *fbn = netdev_priv(dev);
	unsigned int q_map = skb->queue_mapping;

	return fbnic_xmit_frame_ring(skb, fbn->tx[q_map]);
}

netdev_features_t
fbnic_features_check(struct sk_buff *skb, struct net_device *dev,
		     netdev_features_t features)
{
	unsigned int l2len, l3len;

	if (unlikely(skb->ip_summed != CHECKSUM_PARTIAL))
		return features;

	l2len = skb_mac_header_len(skb);
	l3len = skb_checksum_start(skb) - skb_network_header(skb);

	/* Check header lengths are multiple of 2.
	 * In case of 6in6 we support longer headers (IHLEN + OHLEN)
	 * but keep things simple for now, 512B is plenty.
	 */
	if ((l2len | l3len | skb->csum_offset) % 2 ||
	    !FIELD_FIT(FBNIC_TWD_L2_HLEN_MASK, l2len / 2) ||
	    !FIELD_FIT(FBNIC_TWD_L3_IHLEN_MASK, l3len / 2) ||
	    !FIELD_FIT(FBNIC_TWD_CSUM_OFFSET_MASK, skb->csum_offset / 2))
		return features & ~NETIF_F_CSUM_MASK;

	return features;
}

static void fbnic_clean_twq0(struct fbnic_napi_vector *nv, int napi_budget,
			     struct fbnic_ring *ring, bool discard,
			     unsigned int hw_head)
{
	u64 total_bytes = 0, total_packets = 0, ts_lost = 0;
	unsigned int head = ring->head;
	struct netdev_queue *txq;
	unsigned int clean_desc;

	clean_desc = (hw_head - head) & ring->size_mask;

	while (clean_desc) {
		struct sk_buff *skb = ring->tx_buf[head];
		unsigned int desc_cnt;

		desc_cnt = FBNIC_XMIT_CB(skb)->desc_count;
		if (desc_cnt > clean_desc)
			break;

		if (unlikely(FBNIC_XMIT_CB(skb)->flags & FBNIC_XMIT_CB_TS)) {
			FBNIC_XMIT_CB(skb)->hw_head = hw_head;
			if (likely(!discard))
				break;
			ts_lost++;
		}

		ring->tx_buf[head] = NULL;

		clean_desc -= desc_cnt;

		while (!(ring->desc[head] & FBNIC_TWD_TYPE(AL))) {
			head++;
			head &= ring->size_mask;
			desc_cnt--;
		}

		fbnic_unmap_single_twd(nv->dev, &ring->desc[head]);
		head++;
		head &= ring->size_mask;
		desc_cnt--;

		while (desc_cnt--) {
			fbnic_unmap_page_twd(nv->dev, &ring->desc[head]);
			head++;
			head &= ring->size_mask;
		}

		total_bytes += FBNIC_XMIT_CB(skb)->bytecount;
		total_packets += 1;

		napi_consume_skb(skb, napi_budget);
	}

	if (!total_bytes)
		return;

	ring->head = head;

	txq = txring_txq(nv->napi.dev, ring);

	if (unlikely(discard)) {
		u64_stats_update_begin(&ring->stats.syncp);
		ring->stats.dropped += total_packets;
		ring->stats.ts_lost += ts_lost;
		u64_stats_update_end(&ring->stats.syncp);

		netdev_tx_completed_queue(txq, total_packets, total_bytes);
		return;
	}

	u64_stats_update_begin(&ring->stats.syncp);
	ring->stats.bytes += total_bytes;
	ring->stats.packets += total_packets;
	u64_stats_update_end(&ring->stats.syncp);

	netif_txq_completed_wake(txq, total_packets, total_bytes,
				 fbnic_desc_unused(ring),
				 FBNIC_TX_DESC_WAKEUP);
}

static void fbnic_clean_tsq(struct fbnic_napi_vector *nv,
			    struct fbnic_ring *ring,
			    u64 tcd, int *ts_head, int *head0)
{
	struct skb_shared_hwtstamps hwtstamp;
	struct fbnic_net *fbn;
	struct sk_buff *skb;
	int head;
	u64 ns;

	head = (*ts_head < 0) ? ring->head : *ts_head;

	do {
		unsigned int desc_cnt;

		if (head == ring->tail) {
			if (unlikely(net_ratelimit()))
				netdev_err(nv->napi.dev,
					   "Tx timestamp without matching packet\n");
			return;
		}

		skb = ring->tx_buf[head];
		desc_cnt = FBNIC_XMIT_CB(skb)->desc_count;

		head += desc_cnt;
		head &= ring->size_mask;
	} while (!(FBNIC_XMIT_CB(skb)->flags & FBNIC_XMIT_CB_TS));

	fbn = netdev_priv(nv->napi.dev);
	ns = fbnic_ts40_to_ns(fbn, FIELD_GET(FBNIC_TCD_TYPE1_TS_MASK, tcd));

	memset(&hwtstamp, 0, sizeof(hwtstamp));
	hwtstamp.hwtstamp = ns_to_ktime(ns);

	*ts_head = head;

	FBNIC_XMIT_CB(skb)->flags &= ~FBNIC_XMIT_CB_TS;
	if (*head0 < 0) {
		head = FBNIC_XMIT_CB(skb)->hw_head;
		if (head >= 0)
			*head0 = head;
	}

	skb_tstamp_tx(skb, &hwtstamp);
	u64_stats_update_begin(&ring->stats.syncp);
	ring->stats.ts_packets++;
	u64_stats_update_end(&ring->stats.syncp);
}

static void fbnic_page_pool_init(struct fbnic_ring *ring, unsigned int idx,
				 struct page *page)
{
	struct fbnic_rx_buf *rx_buf = &ring->rx_buf[idx];

	page_pool_fragment_page(page, PAGECNT_BIAS_MAX);
	rx_buf->pagecnt_bias = PAGECNT_BIAS_MAX;
	rx_buf->page = page;
}

static struct page *fbnic_page_pool_get(struct fbnic_ring *ring,
					unsigned int idx)
{
	struct fbnic_rx_buf *rx_buf = &ring->rx_buf[idx];

	rx_buf->pagecnt_bias--;

	return rx_buf->page;
}

static void fbnic_page_pool_drain(struct fbnic_ring *ring, unsigned int idx,
				  struct fbnic_napi_vector *nv, int budget)
{
	struct fbnic_rx_buf *rx_buf = &ring->rx_buf[idx];
	struct page *page = rx_buf->page;

	if (!page_pool_unref_page(page, rx_buf->pagecnt_bias))
		page_pool_put_unrefed_page(nv->page_pool, page, -1, !!budget);

	rx_buf->page = NULL;
}

static void fbnic_clean_twq(struct fbnic_napi_vector *nv, int napi_budget,
			    struct fbnic_q_triad *qt, s32 ts_head, s32 head0)
{
	if (head0 >= 0)
		fbnic_clean_twq0(nv, napi_budget, &qt->sub0, false, head0);
	else if (ts_head >= 0)
		fbnic_clean_twq0(nv, napi_budget, &qt->sub0, false, ts_head);
}

static void
fbnic_clean_tcq(struct fbnic_napi_vector *nv, struct fbnic_q_triad *qt,
		int napi_budget)
{
	struct fbnic_ring *cmpl = &qt->cmpl;
	s32 head0 = -1, ts_head = -1;
	__le64 *raw_tcd, done;
	u32 head = cmpl->head;

	done = (head & (cmpl->size_mask + 1)) ? 0 : cpu_to_le64(FBNIC_TCD_DONE);
	raw_tcd = &cmpl->desc[head & cmpl->size_mask];

	/* Walk the completion queue collecting the heads reported by NIC */
	while ((*raw_tcd & cpu_to_le64(FBNIC_TCD_DONE)) == done) {
		u64 tcd;

		dma_rmb();

		tcd = le64_to_cpu(*raw_tcd);

		switch (FIELD_GET(FBNIC_TCD_TYPE_MASK, tcd)) {
		case FBNIC_TCD_TYPE_0:
			if (!(tcd & FBNIC_TCD_TWQ1))
				head0 = FIELD_GET(FBNIC_TCD_TYPE0_HEAD0_MASK,
						  tcd);
			/* Currently all err status bits are related to
			 * timestamps and as those have yet to be added
			 * they are skipped for now.
			 */
			break;
		case FBNIC_TCD_TYPE_1:
			if (WARN_ON_ONCE(tcd & FBNIC_TCD_TWQ1))
				break;

			fbnic_clean_tsq(nv, &qt->sub0, tcd, &ts_head, &head0);
			break;
		default:
			break;
		}

		raw_tcd++;
		head++;
		if (!(head & cmpl->size_mask)) {
			done ^= cpu_to_le64(FBNIC_TCD_DONE);
			raw_tcd = &cmpl->desc[0];
		}
	}

	/* Record the current head/tail of the queue */
	if (cmpl->head != head) {
		cmpl->head = head;
		writel(head & cmpl->size_mask, cmpl->doorbell);
	}

	/* Unmap and free processed buffers */
	fbnic_clean_twq(nv, napi_budget, qt, ts_head, head0);
}

static void fbnic_clean_bdq(struct fbnic_napi_vector *nv, int napi_budget,
			    struct fbnic_ring *ring, unsigned int hw_head)
{
	unsigned int head = ring->head;

	if (head == hw_head)
		return;

	do {
		fbnic_page_pool_drain(ring, head, nv, napi_budget);

		head++;
		head &= ring->size_mask;
	} while (head != hw_head);

	ring->head = head;
}

static void fbnic_bd_prep(struct fbnic_ring *bdq, u16 id, struct page *page)
{
	__le64 *bdq_desc = &bdq->desc[id * FBNIC_BD_FRAG_COUNT];
	dma_addr_t dma = page_pool_get_dma_addr(page);
	u64 bd, i = FBNIC_BD_FRAG_COUNT;

	bd = (FBNIC_BD_PAGE_ADDR_MASK & dma) |
	     FIELD_PREP(FBNIC_BD_PAGE_ID_MASK, id);

	/* In the case that a page size is larger than 4K we will map a
	 * single page to multiple fragments. The fragments will be
	 * FBNIC_BD_FRAG_COUNT in size and the lower n bits will be use
	 * to indicate the individual fragment IDs.
	 */
	do {
		*bdq_desc = cpu_to_le64(bd);
		bd += FIELD_PREP(FBNIC_BD_DESC_ADDR_MASK, 1) |
		      FIELD_PREP(FBNIC_BD_DESC_ID_MASK, 1);
	} while (--i);
}

static void fbnic_fill_bdq(struct fbnic_napi_vector *nv, struct fbnic_ring *bdq)
{
	unsigned int count = fbnic_desc_unused(bdq);
	unsigned int i = bdq->tail;

	if (!count)
		return;

	do {
		struct page *page;

		page = page_pool_dev_alloc_pages(nv->page_pool);
		if (!page)
			break;

		fbnic_page_pool_init(bdq, i, page);
		fbnic_bd_prep(bdq, i, page);

		i++;
		i &= bdq->size_mask;

		count--;
	} while (count);

	if (bdq->tail != i) {
		bdq->tail = i;

		/* Force DMA writes to flush before writing to tail */
		dma_wmb();

		writel(i, bdq->doorbell);
	}
}

static unsigned int fbnic_hdr_pg_start(unsigned int pg_off)
{
	/* The headroom of the first header may be larger than FBNIC_RX_HROOM
	 * due to alignment. So account for that by just making the page
	 * offset 0 if we are starting at the first header.
	 */
	if (ALIGN(FBNIC_RX_HROOM, 128) > FBNIC_RX_HROOM &&
	    pg_off == ALIGN(FBNIC_RX_HROOM, 128))
		return 0;

	return pg_off - FBNIC_RX_HROOM;
}

static unsigned int fbnic_hdr_pg_end(unsigned int pg_off, unsigned int len)
{
	/* Determine the end of the buffer by finding the start of the next
	 * and then subtracting the headroom from that frame.
	 */
	pg_off += len + FBNIC_RX_TROOM + FBNIC_RX_HROOM;

	return ALIGN(pg_off, 128) - FBNIC_RX_HROOM;
}

static void fbnic_pkt_prepare(struct fbnic_napi_vector *nv, u64 rcd,
			      struct fbnic_pkt_buff *pkt,
			      struct fbnic_q_triad *qt)
{
	unsigned int hdr_pg_idx = FIELD_GET(FBNIC_RCD_AL_BUFF_PAGE_MASK, rcd);
	unsigned int hdr_pg_off = FIELD_GET(FBNIC_RCD_AL_BUFF_OFF_MASK, rcd);
	struct page *page = fbnic_page_pool_get(&qt->sub0, hdr_pg_idx);
	unsigned int len = FIELD_GET(FBNIC_RCD_AL_BUFF_LEN_MASK, rcd);
	unsigned int frame_sz, hdr_pg_start, hdr_pg_end, headroom;
	unsigned char *hdr_start;

	/* data_hard_start should always be NULL when this is called */
	WARN_ON_ONCE(pkt->buff.data_hard_start);

	/* Short-cut the end calculation if we know page is fully consumed */
	hdr_pg_end = FIELD_GET(FBNIC_RCD_AL_PAGE_FIN, rcd) ?
		     FBNIC_BD_FRAG_SIZE : fbnic_hdr_pg_end(hdr_pg_off, len);
	hdr_pg_start = fbnic_hdr_pg_start(hdr_pg_off);

	headroom = hdr_pg_off - hdr_pg_start + FBNIC_RX_PAD;
	frame_sz = hdr_pg_end - hdr_pg_start;
	xdp_init_buff(&pkt->buff, frame_sz, NULL);
	hdr_pg_start += (FBNIC_RCD_AL_BUFF_FRAG_MASK & rcd) *
			FBNIC_BD_FRAG_SIZE;

	/* Sync DMA buffer */
	dma_sync_single_range_for_cpu(nv->dev, page_pool_get_dma_addr(page),
				      hdr_pg_start, frame_sz,
				      DMA_BIDIRECTIONAL);

	/* Build frame around buffer */
	hdr_start = page_address(page) + hdr_pg_start;

	xdp_prepare_buff(&pkt->buff, hdr_start, headroom,
			 len - FBNIC_RX_PAD, true);

	pkt->data_truesize = 0;
	pkt->data_len = 0;
	pkt->nr_frags = 0;
}

static void fbnic_add_rx_frag(struct fbnic_napi_vector *nv, u64 rcd,
			      struct fbnic_pkt_buff *pkt,
			      struct fbnic_q_triad *qt)
{
	unsigned int pg_idx = FIELD_GET(FBNIC_RCD_AL_BUFF_PAGE_MASK, rcd);
	unsigned int pg_off = FIELD_GET(FBNIC_RCD_AL_BUFF_OFF_MASK, rcd);
	unsigned int len = FIELD_GET(FBNIC_RCD_AL_BUFF_LEN_MASK, rcd);
	struct page *page = fbnic_page_pool_get(&qt->sub1, pg_idx);
	struct skb_shared_info *shinfo;
	unsigned int truesize;

	truesize = FIELD_GET(FBNIC_RCD_AL_PAGE_FIN, rcd) ?
		   FBNIC_BD_FRAG_SIZE - pg_off : ALIGN(len, 128);

	pg_off += (FBNIC_RCD_AL_BUFF_FRAG_MASK & rcd) *
		  FBNIC_BD_FRAG_SIZE;

	/* Sync DMA buffer */
	dma_sync_single_range_for_cpu(nv->dev, page_pool_get_dma_addr(page),
				      pg_off, truesize, DMA_BIDIRECTIONAL);

	/* Add page to xdp shared info */
	shinfo = xdp_get_shared_info_from_buff(&pkt->buff);

	/* We use gso_segs to store truesize */
	pkt->data_truesize += truesize;

	__skb_fill_page_desc_noacc(shinfo, pkt->nr_frags++, page, pg_off, len);

	/* Store data_len in gso_size */
	pkt->data_len += len;
}

static void fbnic_put_pkt_buff(struct fbnic_napi_vector *nv,
			       struct fbnic_pkt_buff *pkt, int budget)
{
	struct skb_shared_info *shinfo;
	struct page *page;
	int nr_frags;

	if (!pkt->buff.data_hard_start)
		return;

	shinfo = xdp_get_shared_info_from_buff(&pkt->buff);
	nr_frags = pkt->nr_frags;

	while (nr_frags--) {
		page = skb_frag_page(&shinfo->frags[nr_frags]);
		page_pool_put_full_page(nv->page_pool, page, !!budget);
	}

	page = virt_to_page(pkt->buff.data_hard_start);
	page_pool_put_full_page(nv->page_pool, page, !!budget);
}

static struct sk_buff *fbnic_build_skb(struct fbnic_napi_vector *nv,
				       struct fbnic_pkt_buff *pkt)
{
	unsigned int nr_frags = pkt->nr_frags;
	struct skb_shared_info *shinfo;
	unsigned int truesize;
	struct sk_buff *skb;

	truesize = xdp_data_hard_end(&pkt->buff) + FBNIC_RX_TROOM -
		   pkt->buff.data_hard_start;

	/* Build frame around buffer */
	skb = napi_build_skb(pkt->buff.data_hard_start, truesize);
	if (unlikely(!skb))
		return NULL;

	/* Push data pointer to start of data, put tail to end of data */
	skb_reserve(skb, pkt->buff.data - pkt->buff.data_hard_start);
	__skb_put(skb, pkt->buff.data_end - pkt->buff.data);

	/* Add tracking for metadata at the start of the frame */
	skb_metadata_set(skb, pkt->buff.data - pkt->buff.data_meta);

	/* Add Rx frags */
	if (nr_frags) {
		/* Verify that shared info didn't move */
		shinfo = xdp_get_shared_info_from_buff(&pkt->buff);
		WARN_ON(skb_shinfo(skb) != shinfo);

		skb->truesize += pkt->data_truesize;
		skb->data_len += pkt->data_len;
		shinfo->nr_frags = nr_frags;
		skb->len += pkt->data_len;
	}

	skb_mark_for_recycle(skb);

	/* Set MAC header specific fields */
	skb->protocol = eth_type_trans(skb, nv->napi.dev);

	/* Add timestamp if present */
	if (pkt->hwtstamp)
		skb_hwtstamps(skb)->hwtstamp = pkt->hwtstamp;

	return skb;
}

static enum pkt_hash_types fbnic_skb_hash_type(u64 rcd)
{
	return (FBNIC_RCD_META_L4_TYPE_MASK & rcd) ? PKT_HASH_TYPE_L4 :
	       (FBNIC_RCD_META_L3_TYPE_MASK & rcd) ? PKT_HASH_TYPE_L3 :
						     PKT_HASH_TYPE_L2;
}

static void fbnic_rx_tstamp(struct fbnic_napi_vector *nv, u64 rcd,
			    struct fbnic_pkt_buff *pkt)
{
	struct fbnic_net *fbn;
	u64 ns, ts;

	if (!FIELD_GET(FBNIC_RCD_OPT_META_TS, rcd))
		return;

	fbn = netdev_priv(nv->napi.dev);
	ts = FIELD_GET(FBNIC_RCD_OPT_META_TS_MASK, rcd);
	ns = fbnic_ts40_to_ns(fbn, ts);

	/* Add timestamp to shared info */
	pkt->hwtstamp = ns_to_ktime(ns);
}

static void fbnic_populate_skb_fields(struct fbnic_napi_vector *nv,
				      u64 rcd, struct sk_buff *skb,
				      struct fbnic_q_triad *qt)
{
	struct net_device *netdev = nv->napi.dev;
	struct fbnic_ring *rcq = &qt->cmpl;

	fbnic_rx_csum(rcd, skb, rcq);

	if (netdev->features & NETIF_F_RXHASH)
		skb_set_hash(skb,
			     FIELD_GET(FBNIC_RCD_META_RSS_HASH_MASK, rcd),
			     fbnic_skb_hash_type(rcd));

	skb_record_rx_queue(skb, rcq->q_idx);
}

static bool fbnic_rcd_metadata_err(u64 rcd)
{
	return !!(FBNIC_RCD_META_UNCORRECTABLE_ERR_MASK & rcd);
}

static int fbnic_clean_rcq(struct fbnic_napi_vector *nv,
			   struct fbnic_q_triad *qt, int budget)
{
	unsigned int packets = 0, bytes = 0, dropped = 0;
	struct fbnic_ring *rcq = &qt->cmpl;
	struct fbnic_pkt_buff *pkt;
	s32 head0 = -1, head1 = -1;
	__le64 *raw_rcd, done;
	u32 head = rcq->head;

	done = (head & (rcq->size_mask + 1)) ? cpu_to_le64(FBNIC_RCD_DONE) : 0;
	raw_rcd = &rcq->desc[head & rcq->size_mask];
	pkt = rcq->pkt;

	/* Walk the completion queue collecting the heads reported by NIC */
	while (likely(packets < budget)) {
		struct sk_buff *skb = ERR_PTR(-EINVAL);
		u64 rcd;

		if ((*raw_rcd & cpu_to_le64(FBNIC_RCD_DONE)) == done)
			break;

		dma_rmb();

		rcd = le64_to_cpu(*raw_rcd);

		switch (FIELD_GET(FBNIC_RCD_TYPE_MASK, rcd)) {
		case FBNIC_RCD_TYPE_HDR_AL:
			head0 = FIELD_GET(FBNIC_RCD_AL_BUFF_PAGE_MASK, rcd);
			fbnic_pkt_prepare(nv, rcd, pkt, qt);

			break;
		case FBNIC_RCD_TYPE_PAY_AL:
			head1 = FIELD_GET(FBNIC_RCD_AL_BUFF_PAGE_MASK, rcd);
			fbnic_add_rx_frag(nv, rcd, pkt, qt);

			break;
		case FBNIC_RCD_TYPE_OPT_META:
			/* Only type 0 is currently supported */
			if (FIELD_GET(FBNIC_RCD_OPT_META_TYPE_MASK, rcd))
				break;

			fbnic_rx_tstamp(nv, rcd, pkt);

			/* We currently ignore the action table index */
			break;
		case FBNIC_RCD_TYPE_META:
			if (likely(!fbnic_rcd_metadata_err(rcd)))
				skb = fbnic_build_skb(nv, pkt);

			/* Populate skb and invalidate XDP */
			if (!IS_ERR_OR_NULL(skb)) {
				fbnic_populate_skb_fields(nv, rcd, skb, qt);

				packets++;
				bytes += skb->len;

				napi_gro_receive(&nv->napi, skb);
			} else {
				dropped++;
				fbnic_put_pkt_buff(nv, pkt, 1);
			}

			pkt->buff.data_hard_start = NULL;

			break;
		}

		raw_rcd++;
		head++;
		if (!(head & rcq->size_mask)) {
			done ^= cpu_to_le64(FBNIC_RCD_DONE);
			raw_rcd = &rcq->desc[0];
		}
	}

	u64_stats_update_begin(&rcq->stats.syncp);
	rcq->stats.packets += packets;
	rcq->stats.bytes += bytes;
	/* Re-add ethernet header length (removed in fbnic_build_skb) */
	rcq->stats.bytes += ETH_HLEN * packets;
	rcq->stats.dropped += dropped;
	u64_stats_update_end(&rcq->stats.syncp);

	/* Unmap and free processed buffers */
	if (head0 >= 0)
		fbnic_clean_bdq(nv, budget, &qt->sub0, head0);
	fbnic_fill_bdq(nv, &qt->sub0);

	if (head1 >= 0)
		fbnic_clean_bdq(nv, budget, &qt->sub1, head1);
	fbnic_fill_bdq(nv, &qt->sub1);

	/* Record the current head/tail of the queue */
	if (rcq->head != head) {
		rcq->head = head;
		writel(head & rcq->size_mask, rcq->doorbell);
	}

	return packets;
}

static void fbnic_nv_irq_disable(struct fbnic_napi_vector *nv)
{
	struct fbnic_dev *fbd = nv->fbd;
	u32 v_idx = nv->v_idx;

	fbnic_wr32(fbd, FBNIC_INTR_MASK_SET(v_idx / 32), 1 << (v_idx % 32));
}

static void fbnic_nv_irq_rearm(struct fbnic_napi_vector *nv)
{
	struct fbnic_dev *fbd = nv->fbd;
	u32 v_idx = nv->v_idx;

	fbnic_wr32(fbd, FBNIC_INTR_CQ_REARM(v_idx),
		   FBNIC_INTR_CQ_REARM_INTR_UNMASK);
}

static int fbnic_poll(struct napi_struct *napi, int budget)
{
	struct fbnic_napi_vector *nv = container_of(napi,
						    struct fbnic_napi_vector,
						    napi);
	int i, j, work_done = 0;

	for (i = 0; i < nv->txt_count; i++)
		fbnic_clean_tcq(nv, &nv->qt[i], budget);

	for (j = 0; j < nv->rxt_count; j++, i++)
		work_done += fbnic_clean_rcq(nv, &nv->qt[i], budget);

	if (work_done >= budget)
		return budget;

	if (likely(napi_complete_done(napi, work_done)))
		fbnic_nv_irq_rearm(nv);

	return 0;
}

static irqreturn_t fbnic_msix_clean_rings(int __always_unused irq, void *data)
{
	struct fbnic_napi_vector *nv = data;

	napi_schedule_irqoff(&nv->napi);

	return IRQ_HANDLED;
}

static void fbnic_aggregate_ring_rx_counters(struct fbnic_net *fbn,
					     struct fbnic_ring *rxr)
{
	struct fbnic_queue_stats *stats = &rxr->stats;

	/* Capture stats from queues before dissasociating them */
	fbn->rx_stats.bytes += stats->bytes;
	fbn->rx_stats.packets += stats->packets;
	fbn->rx_stats.dropped += stats->dropped;
}

static void fbnic_aggregate_ring_tx_counters(struct fbnic_net *fbn,
					     struct fbnic_ring *txr)
{
	struct fbnic_queue_stats *stats = &txr->stats;

	/* Capture stats from queues before dissasociating them */
	fbn->tx_stats.bytes += stats->bytes;
	fbn->tx_stats.packets += stats->packets;
	fbn->tx_stats.dropped += stats->dropped;
	fbn->tx_stats.ts_lost += stats->ts_lost;
	fbn->tx_stats.ts_packets += stats->ts_packets;
}

static void fbnic_remove_tx_ring(struct fbnic_net *fbn,
				 struct fbnic_ring *txr)
{
	if (!(txr->flags & FBNIC_RING_F_STATS))
		return;

	fbnic_aggregate_ring_tx_counters(fbn, txr);

	/* Remove pointer to the Tx ring */
	WARN_ON(fbn->tx[txr->q_idx] && fbn->tx[txr->q_idx] != txr);
	fbn->tx[txr->q_idx] = NULL;
}

static void fbnic_remove_rx_ring(struct fbnic_net *fbn,
				 struct fbnic_ring *rxr)
{
	if (!(rxr->flags & FBNIC_RING_F_STATS))
		return;

	fbnic_aggregate_ring_rx_counters(fbn, rxr);

	/* Remove pointer to the Rx ring */
	WARN_ON(fbn->rx[rxr->q_idx] && fbn->rx[rxr->q_idx] != rxr);
	fbn->rx[rxr->q_idx] = NULL;
}

static void fbnic_free_napi_vector(struct fbnic_net *fbn,
				   struct fbnic_napi_vector *nv)
{
	struct fbnic_dev *fbd = nv->fbd;
	u32 v_idx = nv->v_idx;
	int i, j;

	for (i = 0; i < nv->txt_count; i++) {
		fbnic_remove_tx_ring(fbn, &nv->qt[i].sub0);
		fbnic_remove_tx_ring(fbn, &nv->qt[i].cmpl);
	}

	for (j = 0; j < nv->rxt_count; j++, i++) {
		fbnic_remove_rx_ring(fbn, &nv->qt[i].sub0);
		fbnic_remove_rx_ring(fbn, &nv->qt[i].sub1);
		fbnic_remove_rx_ring(fbn, &nv->qt[i].cmpl);
	}

	fbnic_free_irq(fbd, v_idx, nv);
	page_pool_destroy(nv->page_pool);
	netif_napi_del(&nv->napi);
	list_del(&nv->napis);
	kfree(nv);
}

void fbnic_free_napi_vectors(struct fbnic_net *fbn)
{
	struct fbnic_napi_vector *nv, *temp;

	list_for_each_entry_safe(nv, temp, &fbn->napis, napis)
		fbnic_free_napi_vector(fbn, nv);
}

static void fbnic_name_napi_vector(struct fbnic_napi_vector *nv)
{
	unsigned char *dev_name = nv->napi.dev->name;

	if (!nv->rxt_count)
		snprintf(nv->name, sizeof(nv->name), "%s-Tx-%u", dev_name,
			 nv->v_idx - FBNIC_NON_NAPI_VECTORS);
	else
		snprintf(nv->name, sizeof(nv->name), "%s-TxRx-%u", dev_name,
			 nv->v_idx - FBNIC_NON_NAPI_VECTORS);
}

#define FBNIC_PAGE_POOL_FLAGS \
	(PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV)

static int fbnic_alloc_nv_page_pool(struct fbnic_net *fbn,
				    struct fbnic_napi_vector *nv)
{
	struct page_pool_params pp_params = {
		.order = 0,
		.flags = FBNIC_PAGE_POOL_FLAGS,
		.pool_size = (fbn->hpq_size + fbn->ppq_size) * nv->rxt_count,
		.nid = NUMA_NO_NODE,
		.dev = nv->dev,
		.dma_dir = DMA_BIDIRECTIONAL,
		.offset = 0,
		.max_len = PAGE_SIZE
	};
	struct page_pool *pp;

	/* Page pool cannot exceed a size of 32768. This doesn't limit the
	 * pages on the ring but the number we can have cached waiting on
	 * the next use.
	 *
	 * TBD: Can this be reduced further? Would a multiple of
	 * NAPI_POLL_WEIGHT possibly make more sense? The question is how
	 * may pages do we need to hold in reserve to get the best return
	 * without hogging too much system memory.
	 */
	if (pp_params.pool_size > 32768)
		pp_params.pool_size = 32768;

	pp = page_pool_create(&pp_params);
	if (IS_ERR(pp))
		return PTR_ERR(pp);

	nv->page_pool = pp;

	return 0;
}

static void fbnic_ring_init(struct fbnic_ring *ring, u32 __iomem *doorbell,
			    int q_idx, u8 flags)
{
	u64_stats_init(&ring->stats.syncp);
	ring->doorbell = doorbell;
	ring->q_idx = q_idx;
	ring->flags = flags;
}

static int fbnic_alloc_napi_vector(struct fbnic_dev *fbd, struct fbnic_net *fbn,
				   unsigned int v_count, unsigned int v_idx,
				   unsigned int txq_count, unsigned int txq_idx,
				   unsigned int rxq_count, unsigned int rxq_idx)
{
	int txt_count = txq_count, rxt_count = rxq_count;
	u32 __iomem *uc_addr = fbd->uc_addr0;
	struct fbnic_napi_vector *nv;
	struct fbnic_q_triad *qt;
	int qt_count, err;
	u32 __iomem *db;

	qt_count = txt_count + rxq_count;
	if (!qt_count)
		return -EINVAL;

	/* If MMIO has already failed there are no rings to initialize */
	if (!uc_addr)
		return -EIO;

	/* Allocate NAPI vector and queue triads */
	nv = kzalloc(struct_size(nv, qt, qt_count), GFP_KERNEL);
	if (!nv)
		return -ENOMEM;

	/* Record queue triad counts */
	nv->txt_count = txt_count;
	nv->rxt_count = rxt_count;

	/* Provide pointer back to fbnic and MSI-X vectors */
	nv->fbd = fbd;
	nv->v_idx = v_idx;

	/* Tie napi to netdev */
	list_add(&nv->napis, &fbn->napis);
	netif_napi_add(fbn->netdev, &nv->napi, fbnic_poll);

	/* Record IRQ to NAPI struct */
	netif_napi_set_irq(&nv->napi,
			   pci_irq_vector(to_pci_dev(fbd->dev), nv->v_idx));

	/* Tie nv back to PCIe dev */
	nv->dev = fbd->dev;

	/* Allocate page pool */
	if (rxq_count) {
		err = fbnic_alloc_nv_page_pool(fbn, nv);
		if (err)
			goto napi_del;
	}

	/* Initialize vector name */
	fbnic_name_napi_vector(nv);

	/* Request the IRQ for napi vector */
	err = fbnic_request_irq(fbd, v_idx, &fbnic_msix_clean_rings,
				IRQF_SHARED, nv->name, nv);
	if (err)
		goto pp_destroy;

	/* Initialize queue triads */
	qt = nv->qt;

	while (txt_count) {
		/* Configure Tx queue */
		db = &uc_addr[FBNIC_QUEUE(txq_idx) + FBNIC_QUEUE_TWQ0_TAIL];

		/* Assign Tx queue to netdev if applicable */
		if (txq_count > 0) {
			u8 flags = FBNIC_RING_F_CTX | FBNIC_RING_F_STATS;

			fbnic_ring_init(&qt->sub0, db, txq_idx, flags);
			fbn->tx[txq_idx] = &qt->sub0;
			txq_count--;
		} else {
			fbnic_ring_init(&qt->sub0, db, 0,
					FBNIC_RING_F_DISABLED);
		}

		/* Configure Tx completion queue */
		db = &uc_addr[FBNIC_QUEUE(txq_idx) + FBNIC_QUEUE_TCQ_HEAD];
		fbnic_ring_init(&qt->cmpl, db, 0, 0);

		/* Update Tx queue index */
		txt_count--;
		txq_idx += v_count;

		/* Move to next queue triad */
		qt++;
	}

	while (rxt_count) {
		/* Configure header queue */
		db = &uc_addr[FBNIC_QUEUE(rxq_idx) + FBNIC_QUEUE_BDQ_HPQ_TAIL];
		fbnic_ring_init(&qt->sub0, db, 0, FBNIC_RING_F_CTX);

		/* Configure payload queue */
		db = &uc_addr[FBNIC_QUEUE(rxq_idx) + FBNIC_QUEUE_BDQ_PPQ_TAIL];
		fbnic_ring_init(&qt->sub1, db, 0, FBNIC_RING_F_CTX);

		/* Configure Rx completion queue */
		db = &uc_addr[FBNIC_QUEUE(rxq_idx) + FBNIC_QUEUE_RCQ_HEAD];
		fbnic_ring_init(&qt->cmpl, db, rxq_idx, FBNIC_RING_F_STATS);
		fbn->rx[rxq_idx] = &qt->cmpl;

		/* Update Rx queue index */
		rxt_count--;
		rxq_idx += v_count;

		/* Move to next queue triad */
		qt++;
	}

	return 0;

pp_destroy:
	page_pool_destroy(nv->page_pool);
napi_del:
	netif_napi_del(&nv->napi);
	list_del(&nv->napis);
	kfree(nv);
	return err;
}

int fbnic_alloc_napi_vectors(struct fbnic_net *fbn)
{
	unsigned int txq_idx = 0, rxq_idx = 0, v_idx = FBNIC_NON_NAPI_VECTORS;
	unsigned int num_tx = fbn->num_tx_queues;
	unsigned int num_rx = fbn->num_rx_queues;
	unsigned int num_napi = fbn->num_napi;
	struct fbnic_dev *fbd = fbn->fbd;
	int err;

	/* Allocate 1 Tx queue per napi vector */
	if (num_napi < FBNIC_MAX_TXQS && num_napi == num_tx + num_rx) {
		while (num_tx) {
			err = fbnic_alloc_napi_vector(fbd, fbn,
						      num_napi, v_idx,
						      1, txq_idx, 0, 0);
			if (err)
				goto free_vectors;

			/* Update counts and index */
			num_tx--;
			txq_idx++;

			v_idx++;
		}
	}

	/* Allocate Tx/Rx queue pairs per vector, or allocate remaining Rx */
	while (num_rx | num_tx) {
		int tqpv = DIV_ROUND_UP(num_tx, num_napi - txq_idx);
		int rqpv = DIV_ROUND_UP(num_rx, num_napi - rxq_idx);

		err = fbnic_alloc_napi_vector(fbd, fbn, num_napi, v_idx,
					      tqpv, txq_idx, rqpv, rxq_idx);
		if (err)
			goto free_vectors;

		/* Update counts and index */
		num_tx -= tqpv;
		txq_idx++;

		num_rx -= rqpv;
		rxq_idx++;

		v_idx++;
	}

	return 0;

free_vectors:
	fbnic_free_napi_vectors(fbn);

	return -ENOMEM;
}

static void fbnic_free_ring_resources(struct device *dev,
				      struct fbnic_ring *ring)
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

static int fbnic_alloc_tx_ring_desc(struct fbnic_net *fbn,
				    struct fbnic_ring *txr)
{
	struct device *dev = fbn->netdev->dev.parent;
	size_t size;

	/* Round size up to nearest 4K */
	size = ALIGN(array_size(sizeof(*txr->desc), fbn->txq_size), 4096);

	txr->desc = dma_alloc_coherent(dev, size, &txr->dma,
				       GFP_KERNEL | __GFP_NOWARN);
	if (!txr->desc)
		return -ENOMEM;

	/* txq_size should be a power of 2, so mask is just that -1 */
	txr->size_mask = fbn->txq_size - 1;
	txr->size = size;

	return 0;
}

static int fbnic_alloc_tx_ring_buffer(struct fbnic_ring *txr)
{
	size_t size = array_size(sizeof(*txr->tx_buf), txr->size_mask + 1);

	txr->tx_buf = kvzalloc(size, GFP_KERNEL | __GFP_NOWARN);

	return txr->tx_buf ? 0 : -ENOMEM;
}

static int fbnic_alloc_tx_ring_resources(struct fbnic_net *fbn,
					 struct fbnic_ring *txr)
{
	struct device *dev = fbn->netdev->dev.parent;
	int err;

	if (txr->flags & FBNIC_RING_F_DISABLED)
		return 0;

	err = fbnic_alloc_tx_ring_desc(fbn, txr);
	if (err)
		return err;

	if (!(txr->flags & FBNIC_RING_F_CTX))
		return 0;

	err = fbnic_alloc_tx_ring_buffer(txr);
	if (err)
		goto free_desc;

	return 0;

free_desc:
	fbnic_free_ring_resources(dev, txr);
	return err;
}

static int fbnic_alloc_rx_ring_desc(struct fbnic_net *fbn,
				    struct fbnic_ring *rxr)
{
	struct device *dev = fbn->netdev->dev.parent;
	size_t desc_size = sizeof(*rxr->desc);
	u32 rxq_size;
	size_t size;

	switch (rxr->doorbell - fbnic_ring_csr_base(rxr)) {
	case FBNIC_QUEUE_BDQ_HPQ_TAIL:
		rxq_size = fbn->hpq_size / FBNIC_BD_FRAG_COUNT;
		desc_size *= FBNIC_BD_FRAG_COUNT;
		break;
	case FBNIC_QUEUE_BDQ_PPQ_TAIL:
		rxq_size = fbn->ppq_size / FBNIC_BD_FRAG_COUNT;
		desc_size *= FBNIC_BD_FRAG_COUNT;
		break;
	case FBNIC_QUEUE_RCQ_HEAD:
		rxq_size = fbn->rcq_size;
		break;
	default:
		return -EINVAL;
	}

	/* Round size up to nearest 4K */
	size = ALIGN(array_size(desc_size, rxq_size), 4096);

	rxr->desc = dma_alloc_coherent(dev, size, &rxr->dma,
				       GFP_KERNEL | __GFP_NOWARN);
	if (!rxr->desc)
		return -ENOMEM;

	/* rxq_size should be a power of 2, so mask is just that -1 */
	rxr->size_mask = rxq_size - 1;
	rxr->size = size;

	return 0;
}

static int fbnic_alloc_rx_ring_buffer(struct fbnic_ring *rxr)
{
	size_t size = array_size(sizeof(*rxr->rx_buf), rxr->size_mask + 1);

	if (rxr->flags & FBNIC_RING_F_CTX)
		size = sizeof(*rxr->rx_buf) * (rxr->size_mask + 1);
	else
		size = sizeof(*rxr->pkt);

	rxr->rx_buf = kvzalloc(size, GFP_KERNEL | __GFP_NOWARN);

	return rxr->rx_buf ? 0 : -ENOMEM;
}

static int fbnic_alloc_rx_ring_resources(struct fbnic_net *fbn,
					 struct fbnic_ring *rxr)
{
	struct device *dev = fbn->netdev->dev.parent;
	int err;

	err = fbnic_alloc_rx_ring_desc(fbn, rxr);
	if (err)
		return err;

	err = fbnic_alloc_rx_ring_buffer(rxr);
	if (err)
		goto free_desc;

	return 0;

free_desc:
	fbnic_free_ring_resources(dev, rxr);
	return err;
}

static void fbnic_free_qt_resources(struct fbnic_net *fbn,
				    struct fbnic_q_triad *qt)
{
	struct device *dev = fbn->netdev->dev.parent;

	fbnic_free_ring_resources(dev, &qt->cmpl);
	fbnic_free_ring_resources(dev, &qt->sub1);
	fbnic_free_ring_resources(dev, &qt->sub0);
}

static int fbnic_alloc_tx_qt_resources(struct fbnic_net *fbn,
				       struct fbnic_q_triad *qt)
{
	struct device *dev = fbn->netdev->dev.parent;
	int err;

	err = fbnic_alloc_tx_ring_resources(fbn, &qt->sub0);
	if (err)
		return err;

	err = fbnic_alloc_tx_ring_resources(fbn, &qt->cmpl);
	if (err)
		goto free_sub1;

	return 0;

free_sub1:
	fbnic_free_ring_resources(dev, &qt->sub0);
	return err;
}

static int fbnic_alloc_rx_qt_resources(struct fbnic_net *fbn,
				       struct fbnic_q_triad *qt)
{
	struct device *dev = fbn->netdev->dev.parent;
	int err;

	err = fbnic_alloc_rx_ring_resources(fbn, &qt->sub0);
	if (err)
		return err;

	err = fbnic_alloc_rx_ring_resources(fbn, &qt->sub1);
	if (err)
		goto free_sub0;

	err = fbnic_alloc_rx_ring_resources(fbn, &qt->cmpl);
	if (err)
		goto free_sub1;

	return 0;

free_sub1:
	fbnic_free_ring_resources(dev, &qt->sub1);
free_sub0:
	fbnic_free_ring_resources(dev, &qt->sub0);
	return err;
}

static void fbnic_free_nv_resources(struct fbnic_net *fbn,
				    struct fbnic_napi_vector *nv)
{
	int i, j;

	/* Free Tx Resources  */
	for (i = 0; i < nv->txt_count; i++)
		fbnic_free_qt_resources(fbn, &nv->qt[i]);

	for (j = 0; j < nv->rxt_count; j++, i++)
		fbnic_free_qt_resources(fbn, &nv->qt[i]);
}

static int fbnic_alloc_nv_resources(struct fbnic_net *fbn,
				    struct fbnic_napi_vector *nv)
{
	int i, j, err;

	/* Allocate Tx Resources */
	for (i = 0; i < nv->txt_count; i++) {
		err = fbnic_alloc_tx_qt_resources(fbn, &nv->qt[i]);
		if (err)
			goto free_resources;
	}

	/* Allocate Rx Resources */
	for (j = 0; j < nv->rxt_count; j++, i++) {
		err = fbnic_alloc_rx_qt_resources(fbn, &nv->qt[i]);
		if (err)
			goto free_resources;
	}

	return 0;

free_resources:
	while (i--)
		fbnic_free_qt_resources(fbn, &nv->qt[i]);
	return err;
}

void fbnic_free_resources(struct fbnic_net *fbn)
{
	struct fbnic_napi_vector *nv;

	list_for_each_entry(nv, &fbn->napis, napis)
		fbnic_free_nv_resources(fbn, nv);
}

int fbnic_alloc_resources(struct fbnic_net *fbn)
{
	struct fbnic_napi_vector *nv;
	int err = -ENODEV;

	list_for_each_entry(nv, &fbn->napis, napis) {
		err = fbnic_alloc_nv_resources(fbn, nv);
		if (err)
			goto free_resources;
	}

	return 0;

free_resources:
	list_for_each_entry_continue_reverse(nv, &fbn->napis, napis)
		fbnic_free_nv_resources(fbn, nv);

	return err;
}

static void fbnic_disable_twq0(struct fbnic_ring *txr)
{
	u32 twq_ctl = fbnic_ring_rd32(txr, FBNIC_QUEUE_TWQ0_CTL);

	twq_ctl &= ~FBNIC_QUEUE_TWQ_CTL_ENABLE;

	fbnic_ring_wr32(txr, FBNIC_QUEUE_TWQ0_CTL, twq_ctl);
}

static void fbnic_disable_tcq(struct fbnic_ring *txr)
{
	fbnic_ring_wr32(txr, FBNIC_QUEUE_TCQ_CTL, 0);
	fbnic_ring_wr32(txr, FBNIC_QUEUE_TIM_MASK, FBNIC_QUEUE_TIM_MASK_MASK);
}

static void fbnic_disable_bdq(struct fbnic_ring *hpq, struct fbnic_ring *ppq)
{
	u32 bdq_ctl = fbnic_ring_rd32(hpq, FBNIC_QUEUE_BDQ_CTL);

	bdq_ctl &= ~FBNIC_QUEUE_BDQ_CTL_ENABLE;

	fbnic_ring_wr32(hpq, FBNIC_QUEUE_BDQ_CTL, bdq_ctl);
}

static void fbnic_disable_rcq(struct fbnic_ring *rxr)
{
	fbnic_ring_wr32(rxr, FBNIC_QUEUE_RCQ_CTL, 0);
	fbnic_ring_wr32(rxr, FBNIC_QUEUE_RIM_MASK, FBNIC_QUEUE_RIM_MASK_MASK);
}

void fbnic_napi_disable(struct fbnic_net *fbn)
{
	struct fbnic_napi_vector *nv;

	list_for_each_entry(nv, &fbn->napis, napis) {
		napi_disable(&nv->napi);

		fbnic_nv_irq_disable(nv);
	}
}

void fbnic_disable(struct fbnic_net *fbn)
{
	struct fbnic_dev *fbd = fbn->fbd;
	struct fbnic_napi_vector *nv;
	int i, j;

	list_for_each_entry(nv, &fbn->napis, napis) {
		/* Disable Tx queue triads */
		for (i = 0; i < nv->txt_count; i++) {
			struct fbnic_q_triad *qt = &nv->qt[i];

			fbnic_disable_twq0(&qt->sub0);
			fbnic_disable_tcq(&qt->cmpl);
		}

		/* Disable Rx queue triads */
		for (j = 0; j < nv->rxt_count; j++, i++) {
			struct fbnic_q_triad *qt = &nv->qt[i];

			fbnic_disable_bdq(&qt->sub0, &qt->sub1);
			fbnic_disable_rcq(&qt->cmpl);
		}
	}

	fbnic_wrfl(fbd);
}

static void fbnic_tx_flush(struct fbnic_dev *fbd)
{
	netdev_warn(fbd->netdev, "triggering Tx flush\n");

	fbnic_rmw32(fbd, FBNIC_TMI_DROP_CTRL, FBNIC_TMI_DROP_CTRL_EN,
		    FBNIC_TMI_DROP_CTRL_EN);
}

static void fbnic_tx_flush_off(struct fbnic_dev *fbd)
{
	fbnic_rmw32(fbd, FBNIC_TMI_DROP_CTRL, FBNIC_TMI_DROP_CTRL_EN, 0);
}

struct fbnic_idle_regs {
	u32 reg_base;
	u8 reg_cnt;
};

static bool fbnic_all_idle(struct fbnic_dev *fbd,
			   const struct fbnic_idle_regs *regs,
			   unsigned int nregs)
{
	unsigned int i, j;

	for (i = 0; i < nregs; i++) {
		for (j = 0; j < regs[i].reg_cnt; j++) {
			if (fbnic_rd32(fbd, regs[i].reg_base + j) != ~0U)
				return false;
		}
	}
	return true;
}

static void fbnic_idle_dump(struct fbnic_dev *fbd,
			    const struct fbnic_idle_regs *regs,
			    unsigned int nregs, const char *dir, int err)
{
	unsigned int i, j;

	netdev_err(fbd->netdev, "error waiting for %s idle %d\n", dir, err);
	for (i = 0; i < nregs; i++)
		for (j = 0; j < regs[i].reg_cnt; j++)
			netdev_err(fbd->netdev, "0x%04x: %08x\n",
				   regs[i].reg_base + j,
				   fbnic_rd32(fbd, regs[i].reg_base + j));
}

int fbnic_wait_all_queues_idle(struct fbnic_dev *fbd, bool may_fail)
{
	static const struct fbnic_idle_regs tx[] = {
		{ FBNIC_QM_TWQ_IDLE(0),	FBNIC_QM_TWQ_IDLE_CNT, },
		{ FBNIC_QM_TQS_IDLE(0),	FBNIC_QM_TQS_IDLE_CNT, },
		{ FBNIC_QM_TDE_IDLE(0),	FBNIC_QM_TDE_IDLE_CNT, },
		{ FBNIC_QM_TCQ_IDLE(0),	FBNIC_QM_TCQ_IDLE_CNT, },
	}, rx[] = {
		{ FBNIC_QM_HPQ_IDLE(0),	FBNIC_QM_HPQ_IDLE_CNT, },
		{ FBNIC_QM_PPQ_IDLE(0),	FBNIC_QM_PPQ_IDLE_CNT, },
		{ FBNIC_QM_RCQ_IDLE(0),	FBNIC_QM_RCQ_IDLE_CNT, },
	};
	bool idle;
	int err;

	err = read_poll_timeout_atomic(fbnic_all_idle, idle, idle, 2, 500000,
				       false, fbd, tx, ARRAY_SIZE(tx));
	if (err == -ETIMEDOUT) {
		fbnic_tx_flush(fbd);
		err = read_poll_timeout_atomic(fbnic_all_idle, idle, idle,
					       2, 500000, false,
					       fbd, tx, ARRAY_SIZE(tx));
		fbnic_tx_flush_off(fbd);
	}
	if (err) {
		fbnic_idle_dump(fbd, tx, ARRAY_SIZE(tx), "Tx", err);
		if (may_fail)
			return err;
	}

	err = read_poll_timeout_atomic(fbnic_all_idle, idle, idle, 2, 500000,
				       false, fbd, rx, ARRAY_SIZE(rx));
	if (err)
		fbnic_idle_dump(fbd, rx, ARRAY_SIZE(rx), "Rx", err);
	return err;
}

void fbnic_flush(struct fbnic_net *fbn)
{
	struct fbnic_napi_vector *nv;

	list_for_each_entry(nv, &fbn->napis, napis) {
		int i, j;

		/* Flush any processed Tx Queue Triads and drop the rest */
		for (i = 0; i < nv->txt_count; i++) {
			struct fbnic_q_triad *qt = &nv->qt[i];
			struct netdev_queue *tx_queue;

			/* Clean the work queues of unprocessed work */
			fbnic_clean_twq0(nv, 0, &qt->sub0, true, qt->sub0.tail);

			/* Reset completion queue descriptor ring */
			memset(qt->cmpl.desc, 0, qt->cmpl.size);

			/* Nothing else to do if Tx queue is disabled */
			if (qt->sub0.flags & FBNIC_RING_F_DISABLED)
				continue;

			/* Reset BQL associated with Tx queue */
			tx_queue = netdev_get_tx_queue(nv->napi.dev,
						       qt->sub0.q_idx);
			netdev_tx_reset_queue(tx_queue);

			/* Disassociate Tx queue from NAPI */
			netif_queue_set_napi(nv->napi.dev, qt->sub0.q_idx,
					     NETDEV_QUEUE_TYPE_TX, NULL);
		}

		/* Flush any processed Rx Queue Triads and drop the rest */
		for (j = 0; j < nv->rxt_count; j++, i++) {
			struct fbnic_q_triad *qt = &nv->qt[i];

			/* Clean the work queues of unprocessed work */
			fbnic_clean_bdq(nv, 0, &qt->sub0, qt->sub0.tail);
			fbnic_clean_bdq(nv, 0, &qt->sub1, qt->sub1.tail);

			/* Reset completion queue descriptor ring */
			memset(qt->cmpl.desc, 0, qt->cmpl.size);

			fbnic_put_pkt_buff(nv, qt->cmpl.pkt, 0);
			qt->cmpl.pkt->buff.data_hard_start = NULL;

			/* Disassociate Rx queue from NAPI */
			netif_queue_set_napi(nv->napi.dev, qt->cmpl.q_idx,
					     NETDEV_QUEUE_TYPE_RX, NULL);
		}
	}
}

void fbnic_fill(struct fbnic_net *fbn)
{
	struct fbnic_napi_vector *nv;

	list_for_each_entry(nv, &fbn->napis, napis) {
		int i, j;

		/* Configure NAPI mapping for Tx */
		for (i = 0; i < nv->txt_count; i++) {
			struct fbnic_q_triad *qt = &nv->qt[i];

			/* Nothing to do if Tx queue is disabled */
			if (qt->sub0.flags & FBNIC_RING_F_DISABLED)
				continue;

			/* Associate Tx queue with NAPI */
			netif_queue_set_napi(nv->napi.dev, qt->sub0.q_idx,
					     NETDEV_QUEUE_TYPE_TX, &nv->napi);
		}

		/* Configure NAPI mapping and populate pages
		 * in the BDQ rings to use for Rx
		 */
		for (j = 0; j < nv->rxt_count; j++, i++) {
			struct fbnic_q_triad *qt = &nv->qt[i];

			/* Associate Rx queue with NAPI */
			netif_queue_set_napi(nv->napi.dev, qt->cmpl.q_idx,
					     NETDEV_QUEUE_TYPE_RX, &nv->napi);

			/* Populate the header and payload BDQs */
			fbnic_fill_bdq(nv, &qt->sub0);
			fbnic_fill_bdq(nv, &qt->sub1);
		}
	}
}

static void fbnic_enable_twq0(struct fbnic_ring *twq)
{
	u32 log_size = fls(twq->size_mask);

	if (!twq->size_mask)
		return;

	/* Reset head/tail */
	fbnic_ring_wr32(twq, FBNIC_QUEUE_TWQ0_CTL, FBNIC_QUEUE_TWQ_CTL_RESET);
	twq->tail = 0;
	twq->head = 0;

	/* Store descriptor ring address and size */
	fbnic_ring_wr32(twq, FBNIC_QUEUE_TWQ0_BAL, lower_32_bits(twq->dma));
	fbnic_ring_wr32(twq, FBNIC_QUEUE_TWQ0_BAH, upper_32_bits(twq->dma));

	/* Write lower 4 bits of log size as 64K ring size is 0 */
	fbnic_ring_wr32(twq, FBNIC_QUEUE_TWQ0_SIZE, log_size & 0xf);

	fbnic_ring_wr32(twq, FBNIC_QUEUE_TWQ0_CTL, FBNIC_QUEUE_TWQ_CTL_ENABLE);
}

static void fbnic_enable_tcq(struct fbnic_napi_vector *nv,
			     struct fbnic_ring *tcq)
{
	u32 log_size = fls(tcq->size_mask);

	if (!tcq->size_mask)
		return;

	/* Reset head/tail */
	fbnic_ring_wr32(tcq, FBNIC_QUEUE_TCQ_CTL, FBNIC_QUEUE_TCQ_CTL_RESET);
	tcq->tail = 0;
	tcq->head = 0;

	/* Store descriptor ring address and size */
	fbnic_ring_wr32(tcq, FBNIC_QUEUE_TCQ_BAL, lower_32_bits(tcq->dma));
	fbnic_ring_wr32(tcq, FBNIC_QUEUE_TCQ_BAH, upper_32_bits(tcq->dma));

	/* Write lower 4 bits of log size as 64K ring size is 0 */
	fbnic_ring_wr32(tcq, FBNIC_QUEUE_TCQ_SIZE, log_size & 0xf);

	/* Store interrupt information for the completion queue */
	fbnic_ring_wr32(tcq, FBNIC_QUEUE_TIM_CTL, nv->v_idx);
	fbnic_ring_wr32(tcq, FBNIC_QUEUE_TIM_THRESHOLD, tcq->size_mask / 2);
	fbnic_ring_wr32(tcq, FBNIC_QUEUE_TIM_MASK, 0);

	/* Enable queue */
	fbnic_ring_wr32(tcq, FBNIC_QUEUE_TCQ_CTL, FBNIC_QUEUE_TCQ_CTL_ENABLE);
}

static void fbnic_enable_bdq(struct fbnic_ring *hpq, struct fbnic_ring *ppq)
{
	u32 bdq_ctl = FBNIC_QUEUE_BDQ_CTL_ENABLE;
	u32 log_size;

	/* Reset head/tail */
	fbnic_ring_wr32(hpq, FBNIC_QUEUE_BDQ_CTL, FBNIC_QUEUE_BDQ_CTL_RESET);
	ppq->tail = 0;
	ppq->head = 0;
	hpq->tail = 0;
	hpq->head = 0;

	log_size = fls(hpq->size_mask);

	/* Store descriptor ring address and size */
	fbnic_ring_wr32(hpq, FBNIC_QUEUE_BDQ_HPQ_BAL, lower_32_bits(hpq->dma));
	fbnic_ring_wr32(hpq, FBNIC_QUEUE_BDQ_HPQ_BAH, upper_32_bits(hpq->dma));

	/* Write lower 4 bits of log size as 64K ring size is 0 */
	fbnic_ring_wr32(hpq, FBNIC_QUEUE_BDQ_HPQ_SIZE, log_size & 0xf);

	if (!ppq->size_mask)
		goto write_ctl;

	log_size = fls(ppq->size_mask);

	/* Add enabling of PPQ to BDQ control */
	bdq_ctl |= FBNIC_QUEUE_BDQ_CTL_PPQ_ENABLE;

	/* Store descriptor ring address and size */
	fbnic_ring_wr32(ppq, FBNIC_QUEUE_BDQ_PPQ_BAL, lower_32_bits(ppq->dma));
	fbnic_ring_wr32(ppq, FBNIC_QUEUE_BDQ_PPQ_BAH, upper_32_bits(ppq->dma));
	fbnic_ring_wr32(ppq, FBNIC_QUEUE_BDQ_PPQ_SIZE, log_size & 0xf);

write_ctl:
	fbnic_ring_wr32(hpq, FBNIC_QUEUE_BDQ_CTL, bdq_ctl);
}

static void fbnic_config_drop_mode_rcq(struct fbnic_napi_vector *nv,
				       struct fbnic_ring *rcq)
{
	u32 drop_mode, rcq_ctl;

	drop_mode = FBNIC_QUEUE_RDE_CTL0_DROP_IMMEDIATE;

	/* Specify packet layout */
	rcq_ctl = FIELD_PREP(FBNIC_QUEUE_RDE_CTL0_DROP_MODE_MASK, drop_mode) |
	    FIELD_PREP(FBNIC_QUEUE_RDE_CTL0_MIN_HROOM_MASK, FBNIC_RX_HROOM) |
	    FIELD_PREP(FBNIC_QUEUE_RDE_CTL0_MIN_TROOM_MASK, FBNIC_RX_TROOM);

	fbnic_ring_wr32(rcq, FBNIC_QUEUE_RDE_CTL0, rcq_ctl);
}

static void fbnic_enable_rcq(struct fbnic_napi_vector *nv,
			     struct fbnic_ring *rcq)
{
	u32 log_size = fls(rcq->size_mask);
	u32 rcq_ctl;

	fbnic_config_drop_mode_rcq(nv, rcq);

	rcq_ctl = FIELD_PREP(FBNIC_QUEUE_RDE_CTL1_PADLEN_MASK, FBNIC_RX_PAD) |
		   FIELD_PREP(FBNIC_QUEUE_RDE_CTL1_MAX_HDR_MASK,
			      FBNIC_RX_MAX_HDR) |
		   FIELD_PREP(FBNIC_QUEUE_RDE_CTL1_PAYLD_OFF_MASK,
			      FBNIC_RX_PAYLD_OFFSET) |
		   FIELD_PREP(FBNIC_QUEUE_RDE_CTL1_PAYLD_PG_CL_MASK,
			      FBNIC_RX_PAYLD_PG_CL);
	fbnic_ring_wr32(rcq, FBNIC_QUEUE_RDE_CTL1, rcq_ctl);

	/* Reset head/tail */
	fbnic_ring_wr32(rcq, FBNIC_QUEUE_RCQ_CTL, FBNIC_QUEUE_RCQ_CTL_RESET);
	rcq->head = 0;
	rcq->tail = 0;

	/* Store descriptor ring address and size */
	fbnic_ring_wr32(rcq, FBNIC_QUEUE_RCQ_BAL, lower_32_bits(rcq->dma));
	fbnic_ring_wr32(rcq, FBNIC_QUEUE_RCQ_BAH, upper_32_bits(rcq->dma));

	/* Write lower 4 bits of log size as 64K ring size is 0 */
	fbnic_ring_wr32(rcq, FBNIC_QUEUE_RCQ_SIZE, log_size & 0xf);

	/* Store interrupt information for the completion queue */
	fbnic_ring_wr32(rcq, FBNIC_QUEUE_RIM_CTL, nv->v_idx);
	fbnic_ring_wr32(rcq, FBNIC_QUEUE_RIM_THRESHOLD, rcq->size_mask / 2);
	fbnic_ring_wr32(rcq, FBNIC_QUEUE_RIM_MASK, 0);

	/* Enable queue */
	fbnic_ring_wr32(rcq, FBNIC_QUEUE_RCQ_CTL, FBNIC_QUEUE_RCQ_CTL_ENABLE);
}

void fbnic_enable(struct fbnic_net *fbn)
{
	struct fbnic_dev *fbd = fbn->fbd;
	struct fbnic_napi_vector *nv;
	int i, j;

	list_for_each_entry(nv, &fbn->napis, napis) {
		/* Setup Tx Queue Triads */
		for (i = 0; i < nv->txt_count; i++) {
			struct fbnic_q_triad *qt = &nv->qt[i];

			fbnic_enable_twq0(&qt->sub0);
			fbnic_enable_tcq(nv, &qt->cmpl);
		}

		/* Setup Rx Queue Triads */
		for (j = 0; j < nv->rxt_count; j++, i++) {
			struct fbnic_q_triad *qt = &nv->qt[i];

			fbnic_enable_bdq(&qt->sub0, &qt->sub1);
			fbnic_config_drop_mode_rcq(nv, &qt->cmpl);
			fbnic_enable_rcq(nv, &qt->cmpl);
		}
	}

	fbnic_wrfl(fbd);
}

static void fbnic_nv_irq_enable(struct fbnic_napi_vector *nv)
{
	struct fbnic_dev *fbd = nv->fbd;
	u32 val;

	val = FBNIC_INTR_CQ_REARM_INTR_UNMASK;

	fbnic_wr32(fbd, FBNIC_INTR_CQ_REARM(nv->v_idx), val);
}

void fbnic_napi_enable(struct fbnic_net *fbn)
{
	u32 irqs[FBNIC_MAX_MSIX_VECS / 32] = {};
	struct fbnic_dev *fbd = fbn->fbd;
	struct fbnic_napi_vector *nv;
	int i;

	list_for_each_entry(nv, &fbn->napis, napis) {
		napi_enable(&nv->napi);

		fbnic_nv_irq_enable(nv);

		/* Record bit used for NAPI IRQs so we can
		 * set the mask appropriately
		 */
		irqs[nv->v_idx / 32] |= BIT(nv->v_idx % 32);
	}

	/* Force the first interrupt on the device to guarantee
	 * that any packets that may have been enqueued during the
	 * bringup are processed.
	 */
	for (i = 0; i < ARRAY_SIZE(irqs); i++) {
		if (!irqs[i])
			continue;
		fbnic_wr32(fbd, FBNIC_INTR_SET(i), irqs[i]);
	}

	fbnic_wrfl(fbd);
}

void fbnic_napi_depletion_check(struct net_device *netdev)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	u32 irqs[FBNIC_MAX_MSIX_VECS / 32] = {};
	struct fbnic_dev *fbd = fbn->fbd;
	struct fbnic_napi_vector *nv;
	int i, j;

	list_for_each_entry(nv, &fbn->napis, napis) {
		/* Find RQs which are completely out of pages */
		for (i = nv->txt_count, j = 0; j < nv->rxt_count; j++, i++) {
			/* Assume 4 pages is always enough to fit a packet
			 * and therefore generate a completion and an IRQ.
			 */
			if (fbnic_desc_used(&nv->qt[i].sub0) < 4 ||
			    fbnic_desc_used(&nv->qt[i].sub1) < 4)
				irqs[nv->v_idx / 32] |= BIT(nv->v_idx % 32);
		}
	}

	for (i = 0; i < ARRAY_SIZE(irqs); i++) {
		if (!irqs[i])
			continue;
		fbnic_wr32(fbd, FBNIC_INTR_MASK_CLEAR(i), irqs[i]);
		fbnic_wr32(fbd, FBNIC_INTR_SET(i), irqs[i]);
	}

	fbnic_wrfl(fbd);
}
