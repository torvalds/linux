// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2015-2021 Google, Inc.
 */

#include "gve.h"
#include "gve_adminq.h"
#include "gve_utils.h"
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/vmalloc.h>
#include <linux/skbuff.h>

static inline void gve_tx_put_doorbell(struct gve_priv *priv,
				       struct gve_queue_resources *q_resources,
				       u32 val)
{
	iowrite32be(val, &priv->db_bar2[be32_to_cpu(q_resources->db_index)]);
}

/* gvnic can only transmit from a Registered Segment.
 * We copy skb payloads into the registered segment before writing Tx
 * descriptors and ringing the Tx doorbell.
 *
 * gve_tx_fifo_* manages the Registered Segment as a FIFO - clients must
 * free allocations in the order they were allocated.
 */

static int gve_tx_fifo_init(struct gve_priv *priv, struct gve_tx_fifo *fifo)
{
	fifo->base = vmap(fifo->qpl->pages, fifo->qpl->num_entries, VM_MAP,
			  PAGE_KERNEL);
	if (unlikely(!fifo->base)) {
		netif_err(priv, drv, priv->dev, "Failed to vmap fifo, qpl_id = %d\n",
			  fifo->qpl->id);
		return -ENOMEM;
	}

	fifo->size = fifo->qpl->num_entries * PAGE_SIZE;
	atomic_set(&fifo->available, fifo->size);
	fifo->head = 0;
	return 0;
}

static void gve_tx_fifo_release(struct gve_priv *priv, struct gve_tx_fifo *fifo)
{
	WARN(atomic_read(&fifo->available) != fifo->size,
	     "Releasing non-empty fifo");

	vunmap(fifo->base);
}

static int gve_tx_fifo_pad_alloc_one_frag(struct gve_tx_fifo *fifo,
					  size_t bytes)
{
	return (fifo->head + bytes < fifo->size) ? 0 : fifo->size - fifo->head;
}

static bool gve_tx_fifo_can_alloc(struct gve_tx_fifo *fifo, size_t bytes)
{
	return (atomic_read(&fifo->available) <= bytes) ? false : true;
}

/* gve_tx_alloc_fifo - Allocate fragment(s) from Tx FIFO
 * @fifo: FIFO to allocate from
 * @bytes: Allocation size
 * @iov: Scatter-gather elements to fill with allocation fragment base/len
 *
 * Returns number of valid elements in iov[] or negative on error.
 *
 * Allocations from a given FIFO must be externally synchronized but concurrent
 * allocation and frees are allowed.
 */
static int gve_tx_alloc_fifo(struct gve_tx_fifo *fifo, size_t bytes,
			     struct gve_tx_iovec iov[2])
{
	size_t overflow, padding;
	u32 aligned_head;
	int nfrags = 0;

	if (!bytes)
		return 0;

	/* This check happens before we know how much padding is needed to
	 * align to a cacheline boundary for the payload, but that is fine,
	 * because the FIFO head always start aligned, and the FIFO's boundaries
	 * are aligned, so if there is space for the data, there is space for
	 * the padding to the next alignment.
	 */
	WARN(!gve_tx_fifo_can_alloc(fifo, bytes),
	     "Reached %s when there's not enough space in the fifo", __func__);

	nfrags++;

	iov[0].iov_offset = fifo->head;
	iov[0].iov_len = bytes;
	fifo->head += bytes;

	if (fifo->head > fifo->size) {
		/* If the allocation did not fit in the tail fragment of the
		 * FIFO, also use the head fragment.
		 */
		nfrags++;
		overflow = fifo->head - fifo->size;
		iov[0].iov_len -= overflow;
		iov[1].iov_offset = 0;	/* Start of fifo*/
		iov[1].iov_len = overflow;

		fifo->head = overflow;
	}

	/* Re-align to a cacheline boundary */
	aligned_head = L1_CACHE_ALIGN(fifo->head);
	padding = aligned_head - fifo->head;
	iov[nfrags - 1].iov_padding = padding;
	atomic_sub(bytes + padding, &fifo->available);
	fifo->head = aligned_head;

	if (fifo->head == fifo->size)
		fifo->head = 0;

	return nfrags;
}

/* gve_tx_free_fifo - Return space to Tx FIFO
 * @fifo: FIFO to return fragments to
 * @bytes: Bytes to free
 */
static void gve_tx_free_fifo(struct gve_tx_fifo *fifo, size_t bytes)
{
	atomic_add(bytes, &fifo->available);
}

static int gve_clean_tx_done(struct gve_priv *priv, struct gve_tx_ring *tx,
			     u32 to_do, bool try_to_wake);

static void gve_tx_free_ring(struct gve_priv *priv, int idx)
{
	struct gve_tx_ring *tx = &priv->tx[idx];
	struct device *hdev = &priv->pdev->dev;
	size_t bytes;
	u32 slots;

	gve_tx_remove_from_block(priv, idx);
	slots = tx->mask + 1;
	gve_clean_tx_done(priv, tx, priv->tx_desc_cnt, false);
	netdev_tx_reset_queue(tx->netdev_txq);

	dma_free_coherent(hdev, sizeof(*tx->q_resources),
			  tx->q_resources, tx->q_resources_bus);
	tx->q_resources = NULL;

	if (!tx->raw_addressing) {
		gve_tx_fifo_release(priv, &tx->tx_fifo);
		gve_unassign_qpl(priv, tx->tx_fifo.qpl->id);
		tx->tx_fifo.qpl = NULL;
	}

	bytes = sizeof(*tx->desc) * slots;
	dma_free_coherent(hdev, bytes, tx->desc, tx->bus);
	tx->desc = NULL;

	vfree(tx->info);
	tx->info = NULL;

	netif_dbg(priv, drv, priv->dev, "freed tx queue %d\n", idx);
}

static int gve_tx_alloc_ring(struct gve_priv *priv, int idx)
{
	struct gve_tx_ring *tx = &priv->tx[idx];
	struct device *hdev = &priv->pdev->dev;
	u32 slots = priv->tx_desc_cnt;
	size_t bytes;

	/* Make sure everything is zeroed to start */
	memset(tx, 0, sizeof(*tx));
	spin_lock_init(&tx->clean_lock);
	tx->q_num = idx;

	tx->mask = slots - 1;

	/* alloc metadata */
	tx->info = vzalloc(sizeof(*tx->info) * slots);
	if (!tx->info)
		return -ENOMEM;

	/* alloc tx queue */
	bytes = sizeof(*tx->desc) * slots;
	tx->desc = dma_alloc_coherent(hdev, bytes, &tx->bus, GFP_KERNEL);
	if (!tx->desc)
		goto abort_with_info;

	tx->raw_addressing = priv->queue_format == GVE_GQI_RDA_FORMAT;
	tx->dev = &priv->pdev->dev;
	if (!tx->raw_addressing) {
		tx->tx_fifo.qpl = gve_assign_tx_qpl(priv);
		if (!tx->tx_fifo.qpl)
			goto abort_with_desc;
		/* map Tx FIFO */
		if (gve_tx_fifo_init(priv, &tx->tx_fifo))
			goto abort_with_qpl;
	}

	tx->q_resources =
		dma_alloc_coherent(hdev,
				   sizeof(*tx->q_resources),
				   &tx->q_resources_bus,
				   GFP_KERNEL);
	if (!tx->q_resources)
		goto abort_with_fifo;

	netif_dbg(priv, drv, priv->dev, "tx[%d]->bus=%lx\n", idx,
		  (unsigned long)tx->bus);
	tx->netdev_txq = netdev_get_tx_queue(priv->dev, idx);
	gve_tx_add_to_block(priv, idx);

	return 0;

abort_with_fifo:
	if (!tx->raw_addressing)
		gve_tx_fifo_release(priv, &tx->tx_fifo);
abort_with_qpl:
	if (!tx->raw_addressing)
		gve_unassign_qpl(priv, tx->tx_fifo.qpl->id);
abort_with_desc:
	dma_free_coherent(hdev, bytes, tx->desc, tx->bus);
	tx->desc = NULL;
abort_with_info:
	vfree(tx->info);
	tx->info = NULL;
	return -ENOMEM;
}

int gve_tx_alloc_rings(struct gve_priv *priv)
{
	int err = 0;
	int i;

	for (i = 0; i < priv->tx_cfg.num_queues; i++) {
		err = gve_tx_alloc_ring(priv, i);
		if (err) {
			netif_err(priv, drv, priv->dev,
				  "Failed to alloc tx ring=%d: err=%d\n",
				  i, err);
			break;
		}
	}
	/* Unallocate if there was an error */
	if (err) {
		int j;

		for (j = 0; j < i; j++)
			gve_tx_free_ring(priv, j);
	}
	return err;
}

void gve_tx_free_rings_gqi(struct gve_priv *priv)
{
	int i;

	for (i = 0; i < priv->tx_cfg.num_queues; i++)
		gve_tx_free_ring(priv, i);
}

/* gve_tx_avail - Calculates the number of slots available in the ring
 * @tx: tx ring to check
 *
 * Returns the number of slots available
 *
 * The capacity of the queue is mask + 1. We don't need to reserve an entry.
 **/
static inline u32 gve_tx_avail(struct gve_tx_ring *tx)
{
	return tx->mask + 1 - (tx->req - tx->done);
}

static inline int gve_skb_fifo_bytes_required(struct gve_tx_ring *tx,
					      struct sk_buff *skb)
{
	int pad_bytes, align_hdr_pad;
	int bytes;
	int hlen;

	hlen = skb_is_gso(skb) ? skb_checksum_start_offset(skb) +
				 tcp_hdrlen(skb) : skb_headlen(skb);

	pad_bytes = gve_tx_fifo_pad_alloc_one_frag(&tx->tx_fifo,
						   hlen);
	/* We need to take into account the header alignment padding. */
	align_hdr_pad = L1_CACHE_ALIGN(hlen) - hlen;
	bytes = align_hdr_pad + pad_bytes + skb->len;

	return bytes;
}

/* The most descriptors we could need is MAX_SKB_FRAGS + 3 : 1 for each skb frag,
 * +1 for the skb linear portion, +1 for when tcp hdr needs to be in separate descriptor,
 * and +1 if the payload wraps to the beginning of the FIFO.
 */
#define MAX_TX_DESC_NEEDED	(MAX_SKB_FRAGS + 3)
static void gve_tx_unmap_buf(struct device *dev, struct gve_tx_buffer_state *info)
{
	if (info->skb) {
		dma_unmap_single(dev, dma_unmap_addr(info, dma),
				 dma_unmap_len(info, len),
				 DMA_TO_DEVICE);
		dma_unmap_len_set(info, len, 0);
	} else {
		dma_unmap_page(dev, dma_unmap_addr(info, dma),
			       dma_unmap_len(info, len),
			       DMA_TO_DEVICE);
		dma_unmap_len_set(info, len, 0);
	}
}

/* Check if sufficient resources (descriptor ring space, FIFO space) are
 * available to transmit the given number of bytes.
 */
static inline bool gve_can_tx(struct gve_tx_ring *tx, int bytes_required)
{
	bool can_alloc = true;

	if (!tx->raw_addressing)
		can_alloc = gve_tx_fifo_can_alloc(&tx->tx_fifo, bytes_required);

	return (gve_tx_avail(tx) >= MAX_TX_DESC_NEEDED && can_alloc);
}

static_assert(NAPI_POLL_WEIGHT >= MAX_TX_DESC_NEEDED);

/* Stops the queue if the skb cannot be transmitted. */
static int gve_maybe_stop_tx(struct gve_priv *priv, struct gve_tx_ring *tx,
			     struct sk_buff *skb)
{
	int bytes_required = 0;
	u32 nic_done;
	u32 to_do;
	int ret;

	if (!tx->raw_addressing)
		bytes_required = gve_skb_fifo_bytes_required(tx, skb);

	if (likely(gve_can_tx(tx, bytes_required)))
		return 0;

	ret = -EBUSY;
	spin_lock(&tx->clean_lock);
	nic_done = gve_tx_load_event_counter(priv, tx);
	to_do = nic_done - tx->done;

	/* Only try to clean if there is hope for TX */
	if (to_do + gve_tx_avail(tx) >= MAX_TX_DESC_NEEDED) {
		if (to_do > 0) {
			to_do = min_t(u32, to_do, NAPI_POLL_WEIGHT);
			gve_clean_tx_done(priv, tx, to_do, false);
		}
		if (likely(gve_can_tx(tx, bytes_required)))
			ret = 0;
	}
	if (ret) {
		/* No space, so stop the queue */
		tx->stop_queue++;
		netif_tx_stop_queue(tx->netdev_txq);
	}
	spin_unlock(&tx->clean_lock);

	return ret;
}

static void gve_tx_fill_pkt_desc(union gve_tx_desc *pkt_desc,
				 struct sk_buff *skb, bool is_gso,
				 int l4_hdr_offset, u32 desc_cnt,
				 u16 hlen, u64 addr)
{
	/* l4_hdr_offset and csum_offset are in units of 16-bit words */
	if (is_gso) {
		pkt_desc->pkt.type_flags = GVE_TXD_TSO | GVE_TXF_L4CSUM;
		pkt_desc->pkt.l4_csum_offset = skb->csum_offset >> 1;
		pkt_desc->pkt.l4_hdr_offset = l4_hdr_offset >> 1;
	} else if (likely(skb->ip_summed == CHECKSUM_PARTIAL)) {
		pkt_desc->pkt.type_flags = GVE_TXD_STD | GVE_TXF_L4CSUM;
		pkt_desc->pkt.l4_csum_offset = skb->csum_offset >> 1;
		pkt_desc->pkt.l4_hdr_offset = l4_hdr_offset >> 1;
	} else {
		pkt_desc->pkt.type_flags = GVE_TXD_STD;
		pkt_desc->pkt.l4_csum_offset = 0;
		pkt_desc->pkt.l4_hdr_offset = 0;
	}
	pkt_desc->pkt.desc_cnt = desc_cnt;
	pkt_desc->pkt.len = cpu_to_be16(skb->len);
	pkt_desc->pkt.seg_len = cpu_to_be16(hlen);
	pkt_desc->pkt.seg_addr = cpu_to_be64(addr);
}

static void gve_tx_fill_seg_desc(union gve_tx_desc *seg_desc,
				 struct sk_buff *skb, bool is_gso,
				 u16 len, u64 addr)
{
	seg_desc->seg.type_flags = GVE_TXD_SEG;
	if (is_gso) {
		if (skb_is_gso_v6(skb))
			seg_desc->seg.type_flags |= GVE_TXSF_IPV6;
		seg_desc->seg.l3_offset = skb_network_offset(skb) >> 1;
		seg_desc->seg.mss = cpu_to_be16(skb_shinfo(skb)->gso_size);
	}
	seg_desc->seg.seg_len = cpu_to_be16(len);
	seg_desc->seg.seg_addr = cpu_to_be64(addr);
}

static void gve_dma_sync_for_device(struct device *dev, dma_addr_t *page_buses,
				    u64 iov_offset, u64 iov_len)
{
	u64 last_page = (iov_offset + iov_len - 1) / PAGE_SIZE;
	u64 first_page = iov_offset / PAGE_SIZE;
	u64 page;

	for (page = first_page; page <= last_page; page++)
		dma_sync_single_for_device(dev, page_buses[page], PAGE_SIZE, DMA_TO_DEVICE);
}

static int gve_tx_add_skb_copy(struct gve_priv *priv, struct gve_tx_ring *tx, struct sk_buff *skb)
{
	int pad_bytes, hlen, hdr_nfrags, payload_nfrags, l4_hdr_offset;
	union gve_tx_desc *pkt_desc, *seg_desc;
	struct gve_tx_buffer_state *info;
	bool is_gso = skb_is_gso(skb);
	u32 idx = tx->req & tx->mask;
	int payload_iov = 2;
	int copy_offset;
	u32 next_idx;
	int i;

	info = &tx->info[idx];
	pkt_desc = &tx->desc[idx];

	l4_hdr_offset = skb_checksum_start_offset(skb);
	/* If the skb is gso, then we want the tcp header in the first segment
	 * otherwise we want the linear portion of the skb (which will contain
	 * the checksum because skb->csum_start and skb->csum_offset are given
	 * relative to skb->head) in the first segment.
	 */
	hlen = is_gso ? l4_hdr_offset + tcp_hdrlen(skb) :
			skb_headlen(skb);

	info->skb =  skb;
	/* We don't want to split the header, so if necessary, pad to the end
	 * of the fifo and then put the header at the beginning of the fifo.
	 */
	pad_bytes = gve_tx_fifo_pad_alloc_one_frag(&tx->tx_fifo, hlen);
	hdr_nfrags = gve_tx_alloc_fifo(&tx->tx_fifo, hlen + pad_bytes,
				       &info->iov[0]);
	WARN(!hdr_nfrags, "hdr_nfrags should never be 0!");
	payload_nfrags = gve_tx_alloc_fifo(&tx->tx_fifo, skb->len - hlen,
					   &info->iov[payload_iov]);

	gve_tx_fill_pkt_desc(pkt_desc, skb, is_gso, l4_hdr_offset,
			     1 + payload_nfrags, hlen,
			     info->iov[hdr_nfrags - 1].iov_offset);

	skb_copy_bits(skb, 0,
		      tx->tx_fifo.base + info->iov[hdr_nfrags - 1].iov_offset,
		      hlen);
	gve_dma_sync_for_device(&priv->pdev->dev, tx->tx_fifo.qpl->page_buses,
				info->iov[hdr_nfrags - 1].iov_offset,
				info->iov[hdr_nfrags - 1].iov_len);
	copy_offset = hlen;

	for (i = payload_iov; i < payload_nfrags + payload_iov; i++) {
		next_idx = (tx->req + 1 + i - payload_iov) & tx->mask;
		seg_desc = &tx->desc[next_idx];

		gve_tx_fill_seg_desc(seg_desc, skb, is_gso,
				     info->iov[i].iov_len,
				     info->iov[i].iov_offset);

		skb_copy_bits(skb, copy_offset,
			      tx->tx_fifo.base + info->iov[i].iov_offset,
			      info->iov[i].iov_len);
		gve_dma_sync_for_device(&priv->pdev->dev, tx->tx_fifo.qpl->page_buses,
					info->iov[i].iov_offset,
					info->iov[i].iov_len);
		copy_offset += info->iov[i].iov_len;
	}

	return 1 + payload_nfrags;
}

static int gve_tx_add_skb_no_copy(struct gve_priv *priv, struct gve_tx_ring *tx,
				  struct sk_buff *skb)
{
	const struct skb_shared_info *shinfo = skb_shinfo(skb);
	int hlen, payload_nfrags, l4_hdr_offset;
	union gve_tx_desc *pkt_desc, *seg_desc;
	struct gve_tx_buffer_state *info;
	bool is_gso = skb_is_gso(skb);
	u32 idx = tx->req & tx->mask;
	u64 addr;
	u32 len;
	int i;

	info = &tx->info[idx];
	pkt_desc = &tx->desc[idx];

	l4_hdr_offset = skb_checksum_start_offset(skb);
	/* If the skb is gso, then we want only up to the tcp header in the first segment
	 * to efficiently replicate on each segment otherwise we want the linear portion
	 * of the skb (which will contain the checksum because skb->csum_start and
	 * skb->csum_offset are given relative to skb->head) in the first segment.
	 */
	hlen = is_gso ? l4_hdr_offset + tcp_hdrlen(skb) : skb_headlen(skb);
	len = skb_headlen(skb);

	info->skb =  skb;

	addr = dma_map_single(tx->dev, skb->data, len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(tx->dev, addr))) {
		tx->dma_mapping_error++;
		goto drop;
	}
	dma_unmap_len_set(info, len, len);
	dma_unmap_addr_set(info, dma, addr);

	payload_nfrags = shinfo->nr_frags;
	if (hlen < len) {
		/* For gso the rest of the linear portion of the skb needs to
		 * be in its own descriptor.
		 */
		payload_nfrags++;
		gve_tx_fill_pkt_desc(pkt_desc, skb, is_gso, l4_hdr_offset,
				     1 + payload_nfrags, hlen, addr);

		len -= hlen;
		addr += hlen;
		idx = (tx->req + 1) & tx->mask;
		seg_desc = &tx->desc[idx];
		gve_tx_fill_seg_desc(seg_desc, skb, is_gso, len, addr);
	} else {
		gve_tx_fill_pkt_desc(pkt_desc, skb, is_gso, l4_hdr_offset,
				     1 + payload_nfrags, hlen, addr);
	}

	for (i = 0; i < shinfo->nr_frags; i++) {
		const skb_frag_t *frag = &shinfo->frags[i];

		idx = (idx + 1) & tx->mask;
		seg_desc = &tx->desc[idx];
		len = skb_frag_size(frag);
		addr = skb_frag_dma_map(tx->dev, frag, 0, len, DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(tx->dev, addr))) {
			tx->dma_mapping_error++;
			goto unmap_drop;
		}
		tx->info[idx].skb = NULL;
		dma_unmap_len_set(&tx->info[idx], len, len);
		dma_unmap_addr_set(&tx->info[idx], dma, addr);

		gve_tx_fill_seg_desc(seg_desc, skb, is_gso, len, addr);
	}

	return 1 + payload_nfrags;

unmap_drop:
	i += (payload_nfrags == shinfo->nr_frags ? 1 : 2);
	while (i--) {
		idx--;
		gve_tx_unmap_buf(tx->dev, &tx->info[idx & tx->mask]);
	}
drop:
	tx->dropped_pkt++;
	return 0;
}

netdev_tx_t gve_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct gve_priv *priv = netdev_priv(dev);
	struct gve_tx_ring *tx;
	int nsegs;

	WARN(skb_get_queue_mapping(skb) >= priv->tx_cfg.num_queues,
	     "skb queue index out of range");
	tx = &priv->tx[skb_get_queue_mapping(skb)];
	if (unlikely(gve_maybe_stop_tx(priv, tx, skb))) {
		/* We need to ring the txq doorbell -- we have stopped the Tx
		 * queue for want of resources, but prior calls to gve_tx()
		 * may have added descriptors without ringing the doorbell.
		 */

		gve_tx_put_doorbell(priv, tx->q_resources, tx->req);
		return NETDEV_TX_BUSY;
	}
	if (tx->raw_addressing)
		nsegs = gve_tx_add_skb_no_copy(priv, tx, skb);
	else
		nsegs = gve_tx_add_skb_copy(priv, tx, skb);

	/* If the packet is getting sent, we need to update the skb */
	if (nsegs) {
		netdev_tx_sent_queue(tx->netdev_txq, skb->len);
		skb_tx_timestamp(skb);
		tx->req += nsegs;
	} else {
		dev_kfree_skb_any(skb);
	}

	if (!netif_xmit_stopped(tx->netdev_txq) && netdev_xmit_more())
		return NETDEV_TX_OK;

	/* Give packets to NIC. Even if this packet failed to send the doorbell
	 * might need to be rung because of xmit_more.
	 */
	gve_tx_put_doorbell(priv, tx->q_resources, tx->req);
	return NETDEV_TX_OK;
}

#define GVE_TX_START_THRESH	PAGE_SIZE

static int gve_clean_tx_done(struct gve_priv *priv, struct gve_tx_ring *tx,
			     u32 to_do, bool try_to_wake)
{
	struct gve_tx_buffer_state *info;
	u64 pkts = 0, bytes = 0;
	size_t space_freed = 0;
	struct sk_buff *skb;
	int i, j;
	u32 idx;

	for (j = 0; j < to_do; j++) {
		idx = tx->done & tx->mask;
		netif_info(priv, tx_done, priv->dev,
			   "[%d] %s: idx=%d (req=%u done=%u)\n",
			   tx->q_num, __func__, idx, tx->req, tx->done);
		info = &tx->info[idx];
		skb = info->skb;

		/* Unmap the buffer */
		if (tx->raw_addressing)
			gve_tx_unmap_buf(tx->dev, info);
		tx->done++;
		/* Mark as free */
		if (skb) {
			info->skb = NULL;
			bytes += skb->len;
			pkts++;
			dev_consume_skb_any(skb);
			if (tx->raw_addressing)
				continue;
			/* FIFO free */
			for (i = 0; i < ARRAY_SIZE(info->iov); i++) {
				space_freed += info->iov[i].iov_len + info->iov[i].iov_padding;
				info->iov[i].iov_len = 0;
				info->iov[i].iov_padding = 0;
			}
		}
	}

	if (!tx->raw_addressing)
		gve_tx_free_fifo(&tx->tx_fifo, space_freed);
	u64_stats_update_begin(&tx->statss);
	tx->bytes_done += bytes;
	tx->pkt_done += pkts;
	u64_stats_update_end(&tx->statss);
	netdev_tx_completed_queue(tx->netdev_txq, pkts, bytes);

	/* start the queue if we've stopped it */
#ifndef CONFIG_BQL
	/* Make sure that the doorbells are synced */
	smp_mb();
#endif
	if (try_to_wake && netif_tx_queue_stopped(tx->netdev_txq) &&
	    likely(gve_can_tx(tx, GVE_TX_START_THRESH))) {
		tx->wake_queue++;
		netif_tx_wake_queue(tx->netdev_txq);
	}

	return pkts;
}

u32 gve_tx_load_event_counter(struct gve_priv *priv,
			      struct gve_tx_ring *tx)
{
	u32 counter_index = be32_to_cpu(tx->q_resources->counter_index);
	__be32 counter = READ_ONCE(priv->counter_array[counter_index]);

	return be32_to_cpu(counter);
}

bool gve_tx_poll(struct gve_notify_block *block, int budget)
{
	struct gve_priv *priv = block->priv;
	struct gve_tx_ring *tx = block->tx;
	u32 nic_done;
	u32 to_do;

	/* If budget is 0, do all the work */
	if (budget == 0)
		budget = INT_MAX;

	/* In TX path, it may try to clean completed pkts in order to xmit,
	 * to avoid cleaning conflict, use spin_lock(), it yields better
	 * concurrency between xmit/clean than netif's lock.
	 */
	spin_lock(&tx->clean_lock);
	/* Find out how much work there is to be done */
	nic_done = gve_tx_load_event_counter(priv, tx);
	to_do = min_t(u32, (nic_done - tx->done), budget);
	gve_clean_tx_done(priv, tx, to_do, true);
	spin_unlock(&tx->clean_lock);
	/* If we still have work we want to repoll */
	return nic_done != tx->done;
}

bool gve_tx_clean_pending(struct gve_priv *priv, struct gve_tx_ring *tx)
{
	u32 nic_done = gve_tx_load_event_counter(priv, tx);

	return nic_done != tx->done;
}
