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
#include <net/xdp_sock_drv.h>

static inline void gve_tx_put_doorbell(struct gve_priv *priv,
				       struct gve_queue_resources *q_resources,
				       u32 val)
{
	iowrite32be(val, &priv->db_bar2[be32_to_cpu(q_resources->db_index)]);
}

void gve_xdp_tx_flush(struct gve_priv *priv, u32 xdp_qid)
{
	u32 tx_qid = gve_xdp_tx_queue_id(priv, xdp_qid);
	struct gve_tx_ring *tx = &priv->tx[tx_qid];

	gve_tx_put_doorbell(priv, tx->q_resources, tx->req);
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

static size_t gve_tx_clear_buffer_state(struct gve_tx_buffer_state *info)
{
	size_t space_freed = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(info->iov); i++) {
		space_freed += info->iov[i].iov_len + info->iov[i].iov_padding;
		info->iov[i].iov_len = 0;
		info->iov[i].iov_padding = 0;
	}
	return space_freed;
}

static int gve_clean_xdp_done(struct gve_priv *priv, struct gve_tx_ring *tx,
			      u32 to_do)
{
	struct gve_tx_buffer_state *info;
	u64 pkts = 0, bytes = 0;
	size_t space_freed = 0;
	u32 xsk_complete = 0;
	u32 idx;
	int i;

	for (i = 0; i < to_do; i++) {
		idx = tx->done & tx->mask;
		info = &tx->info[idx];
		tx->done++;

		if (unlikely(!info->xdp.size))
			continue;

		bytes += info->xdp.size;
		pkts++;
		xsk_complete += info->xdp.is_xsk;

		info->xdp.size = 0;
		if (info->xdp_frame) {
			xdp_return_frame(info->xdp_frame);
			info->xdp_frame = NULL;
		}
		space_freed += gve_tx_clear_buffer_state(info);
	}

	gve_tx_free_fifo(&tx->tx_fifo, space_freed);
	if (xsk_complete > 0 && tx->xsk_pool)
		xsk_tx_completed(tx->xsk_pool, xsk_complete);
	u64_stats_update_begin(&tx->statss);
	tx->bytes_done += bytes;
	tx->pkt_done += pkts;
	u64_stats_update_end(&tx->statss);
	return pkts;
}

static int gve_clean_tx_done(struct gve_priv *priv, struct gve_tx_ring *tx,
			     u32 to_do, bool try_to_wake);

void gve_tx_stop_ring_gqi(struct gve_priv *priv, int idx)
{
	int ntfy_idx = gve_tx_idx_to_ntfy(priv, idx);
	struct gve_tx_ring *tx = &priv->tx[idx];

	if (!gve_tx_was_added_to_block(priv, idx))
		return;

	gve_remove_napi(priv, ntfy_idx);
	if (tx->q_num < priv->tx_cfg.num_queues)
		gve_clean_tx_done(priv, tx, priv->tx_desc_cnt, false);
	else
		gve_clean_xdp_done(priv, tx, priv->tx_desc_cnt);
	netdev_tx_reset_queue(tx->netdev_txq);
	gve_tx_remove_from_block(priv, idx);
}

static void gve_tx_free_ring_gqi(struct gve_priv *priv, struct gve_tx_ring *tx,
				 struct gve_tx_alloc_rings_cfg *cfg)
{
	struct device *hdev = &priv->pdev->dev;
	int idx = tx->q_num;
	size_t bytes;
	u32 qpl_id;
	u32 slots;

	slots = tx->mask + 1;
	dma_free_coherent(hdev, sizeof(*tx->q_resources),
			  tx->q_resources, tx->q_resources_bus);
	tx->q_resources = NULL;

	if (tx->tx_fifo.qpl) {
		if (tx->tx_fifo.base)
			gve_tx_fifo_release(priv, &tx->tx_fifo);

		qpl_id = gve_tx_qpl_id(priv, tx->q_num);
		gve_free_queue_page_list(priv, tx->tx_fifo.qpl, qpl_id);
		tx->tx_fifo.qpl = NULL;
	}

	bytes = sizeof(*tx->desc) * slots;
	dma_free_coherent(hdev, bytes, tx->desc, tx->bus);
	tx->desc = NULL;

	vfree(tx->info);
	tx->info = NULL;

	netif_dbg(priv, drv, priv->dev, "freed tx queue %d\n", idx);
}

void gve_tx_start_ring_gqi(struct gve_priv *priv, int idx)
{
	int ntfy_idx = gve_tx_idx_to_ntfy(priv, idx);
	struct gve_tx_ring *tx = &priv->tx[idx];

	gve_tx_add_to_block(priv, idx);

	tx->netdev_txq = netdev_get_tx_queue(priv->dev, idx);
	gve_add_napi(priv, ntfy_idx, gve_napi_poll);
}

static int gve_tx_alloc_ring_gqi(struct gve_priv *priv,
				 struct gve_tx_alloc_rings_cfg *cfg,
				 struct gve_tx_ring *tx,
				 int idx)
{
	struct device *hdev = &priv->pdev->dev;
	int qpl_page_cnt;
	u32 qpl_id = 0;
	size_t bytes;

	/* Make sure everything is zeroed to start */
	memset(tx, 0, sizeof(*tx));
	spin_lock_init(&tx->clean_lock);
	spin_lock_init(&tx->xdp_lock);
	tx->q_num = idx;

	tx->mask = cfg->ring_size - 1;

	/* alloc metadata */
	tx->info = vcalloc(cfg->ring_size, sizeof(*tx->info));
	if (!tx->info)
		return -ENOMEM;

	/* alloc tx queue */
	bytes = sizeof(*tx->desc) * cfg->ring_size;
	tx->desc = dma_alloc_coherent(hdev, bytes, &tx->bus, GFP_KERNEL);
	if (!tx->desc)
		goto abort_with_info;

	tx->raw_addressing = cfg->raw_addressing;
	tx->dev = hdev;
	if (!tx->raw_addressing) {
		qpl_id = gve_tx_qpl_id(priv, tx->q_num);
		qpl_page_cnt = priv->tx_pages_per_qpl;

		tx->tx_fifo.qpl = gve_alloc_queue_page_list(priv, qpl_id,
							    qpl_page_cnt);
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

	return 0;

abort_with_fifo:
	if (!tx->raw_addressing)
		gve_tx_fifo_release(priv, &tx->tx_fifo);
abort_with_qpl:
	if (!tx->raw_addressing) {
		gve_free_queue_page_list(priv, tx->tx_fifo.qpl, qpl_id);
		tx->tx_fifo.qpl = NULL;
	}
abort_with_desc:
	dma_free_coherent(hdev, bytes, tx->desc, tx->bus);
	tx->desc = NULL;
abort_with_info:
	vfree(tx->info);
	tx->info = NULL;
	return -ENOMEM;
}

int gve_tx_alloc_rings_gqi(struct gve_priv *priv,
			   struct gve_tx_alloc_rings_cfg *cfg)
{
	struct gve_tx_ring *tx = cfg->tx;
	int total_queues;
	int err = 0;
	int i, j;

	total_queues = cfg->qcfg->num_queues + cfg->num_xdp_rings;
	if (total_queues > cfg->qcfg->max_queues) {
		netif_err(priv, drv, priv->dev,
			  "Cannot alloc more than the max num of Tx rings\n");
		return -EINVAL;
	}

	tx = kvcalloc(cfg->qcfg->max_queues, sizeof(struct gve_tx_ring),
		      GFP_KERNEL);
	if (!tx)
		return -ENOMEM;

	for (i = 0; i < total_queues; i++) {
		err = gve_tx_alloc_ring_gqi(priv, cfg, &tx[i], i);
		if (err) {
			netif_err(priv, drv, priv->dev,
				  "Failed to alloc tx ring=%d: err=%d\n",
				  i, err);
			goto cleanup;
		}
	}

	cfg->tx = tx;
	return 0;

cleanup:
	for (j = 0; j < i; j++)
		gve_tx_free_ring_gqi(priv, &tx[j], cfg);
	kvfree(tx);
	return err;
}

void gve_tx_free_rings_gqi(struct gve_priv *priv,
			   struct gve_tx_alloc_rings_cfg *cfg)
{
	struct gve_tx_ring *tx = cfg->tx;
	int i;

	if (!tx)
		return;

	for (i = 0; i < cfg->qcfg->num_queues + cfg->qcfg->num_xdp_queues; i++)
		gve_tx_free_ring_gqi(priv, &tx[i], cfg);

	kvfree(tx);
	cfg->tx = NULL;
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

	hlen = skb_is_gso(skb) ? skb_checksum_start_offset(skb) + tcp_hdrlen(skb) :
				 min_t(int, GVE_GQ_TX_MIN_PKT_DESC_BYTES, skb->len);

	pad_bytes = gve_tx_fifo_pad_alloc_one_frag(&tx->tx_fifo,
						   hlen);
	/* We need to take into account the header alignment padding. */
	align_hdr_pad = L1_CACHE_ALIGN(hlen) - hlen;
	bytes = align_hdr_pad + pad_bytes + skb->len;

	return bytes;
}

/* The most descriptors we could need is MAX_SKB_FRAGS + 4 :
 * 1 for each skb frag
 * 1 for the skb linear portion
 * 1 for when tcp hdr needs to be in separate descriptor
 * 1 if the payload wraps to the beginning of the FIFO
 * 1 for metadata descriptor
 */
#define MAX_TX_DESC_NEEDED	(MAX_SKB_FRAGS + 4)
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
				 u16 csum_offset, u8 ip_summed, bool is_gso,
				 int l4_hdr_offset, u32 desc_cnt,
				 u16 hlen, u64 addr, u16 pkt_len)
{
	/* l4_hdr_offset and csum_offset are in units of 16-bit words */
	if (is_gso) {
		pkt_desc->pkt.type_flags = GVE_TXD_TSO | GVE_TXF_L4CSUM;
		pkt_desc->pkt.l4_csum_offset = csum_offset >> 1;
		pkt_desc->pkt.l4_hdr_offset = l4_hdr_offset >> 1;
	} else if (likely(ip_summed == CHECKSUM_PARTIAL)) {
		pkt_desc->pkt.type_flags = GVE_TXD_STD | GVE_TXF_L4CSUM;
		pkt_desc->pkt.l4_csum_offset = csum_offset >> 1;
		pkt_desc->pkt.l4_hdr_offset = l4_hdr_offset >> 1;
	} else {
		pkt_desc->pkt.type_flags = GVE_TXD_STD;
		pkt_desc->pkt.l4_csum_offset = 0;
		pkt_desc->pkt.l4_hdr_offset = 0;
	}
	pkt_desc->pkt.desc_cnt = desc_cnt;
	pkt_desc->pkt.len = cpu_to_be16(pkt_len);
	pkt_desc->pkt.seg_len = cpu_to_be16(hlen);
	pkt_desc->pkt.seg_addr = cpu_to_be64(addr);
}

static void gve_tx_fill_mtd_desc(union gve_tx_desc *mtd_desc,
				 struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(mtd_desc->mtd) != sizeof(mtd_desc->pkt));

	mtd_desc->mtd.type_flags = GVE_TXD_MTD | GVE_MTD_SUBTYPE_PATH;
	mtd_desc->mtd.path_state = GVE_MTD_PATH_STATE_DEFAULT |
				   GVE_MTD_PATH_HASH_L4;
	mtd_desc->mtd.path_hash = cpu_to_be32(skb->hash);
	mtd_desc->mtd.reserved0 = 0;
	mtd_desc->mtd.reserved1 = 0;
}

static void gve_tx_fill_seg_desc(union gve_tx_desc *seg_desc,
				 u16 l3_offset, u16 gso_size,
				 bool is_gso_v6, bool is_gso,
				 u16 len, u64 addr)
{
	seg_desc->seg.type_flags = GVE_TXD_SEG;
	if (is_gso) {
		if (is_gso_v6)
			seg_desc->seg.type_flags |= GVE_TXSF_IPV6;
		seg_desc->seg.l3_offset = l3_offset >> 1;
		seg_desc->seg.mss = cpu_to_be16(gso_size);
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
	int mtd_desc_nr = !!skb->l4_hash;
	bool is_gso = skb_is_gso(skb);
	u32 idx = tx->req & tx->mask;
	int payload_iov = 2;
	int copy_offset;
	u32 next_idx;
	int i;

	info = &tx->info[idx];
	pkt_desc = &tx->desc[idx];

	l4_hdr_offset = skb_checksum_start_offset(skb);
	/* If the skb is gso, then we want the tcp header alone in the first segment
	 * otherwise we want the minimum required by the gVNIC spec.
	 */
	hlen = is_gso ? l4_hdr_offset + tcp_hdrlen(skb) :
			min_t(int, GVE_GQ_TX_MIN_PKT_DESC_BYTES, skb->len);

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

	gve_tx_fill_pkt_desc(pkt_desc, skb->csum_offset, skb->ip_summed,
			     is_gso, l4_hdr_offset,
			     1 + mtd_desc_nr + payload_nfrags, hlen,
			     info->iov[hdr_nfrags - 1].iov_offset, skb->len);

	skb_copy_bits(skb, 0,
		      tx->tx_fifo.base + info->iov[hdr_nfrags - 1].iov_offset,
		      hlen);
	gve_dma_sync_for_device(&priv->pdev->dev, tx->tx_fifo.qpl->page_buses,
				info->iov[hdr_nfrags - 1].iov_offset,
				info->iov[hdr_nfrags - 1].iov_len);
	copy_offset = hlen;

	if (mtd_desc_nr) {
		next_idx = (tx->req + 1) & tx->mask;
		gve_tx_fill_mtd_desc(&tx->desc[next_idx], skb);
	}

	for (i = payload_iov; i < payload_nfrags + payload_iov; i++) {
		next_idx = (tx->req + 1 + mtd_desc_nr + i - payload_iov) & tx->mask;
		seg_desc = &tx->desc[next_idx];

		gve_tx_fill_seg_desc(seg_desc, skb_network_offset(skb),
				     skb_shinfo(skb)->gso_size,
				     skb_is_gso_v6(skb), is_gso,
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

	return 1 + mtd_desc_nr + payload_nfrags;
}

static int gve_tx_add_skb_no_copy(struct gve_priv *priv, struct gve_tx_ring *tx,
				  struct sk_buff *skb)
{
	const struct skb_shared_info *shinfo = skb_shinfo(skb);
	int hlen, num_descriptors, l4_hdr_offset;
	union gve_tx_desc *pkt_desc, *mtd_desc, *seg_desc;
	struct gve_tx_buffer_state *info;
	int mtd_desc_nr = !!skb->l4_hash;
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

	num_descriptors = 1 + shinfo->nr_frags;
	if (hlen < len)
		num_descriptors++;
	if (mtd_desc_nr)
		num_descriptors++;

	gve_tx_fill_pkt_desc(pkt_desc, skb->csum_offset, skb->ip_summed,
			     is_gso, l4_hdr_offset,
			     num_descriptors, hlen, addr, skb->len);

	if (mtd_desc_nr) {
		idx = (idx + 1) & tx->mask;
		mtd_desc = &tx->desc[idx];
		gve_tx_fill_mtd_desc(mtd_desc, skb);
	}

	if (hlen < len) {
		/* For gso the rest of the linear portion of the skb needs to
		 * be in its own descriptor.
		 */
		len -= hlen;
		addr += hlen;
		idx = (idx + 1) & tx->mask;
		seg_desc = &tx->desc[idx];
		gve_tx_fill_seg_desc(seg_desc, skb_network_offset(skb),
				     skb_shinfo(skb)->gso_size,
				     skb_is_gso_v6(skb), is_gso, len, addr);
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

		gve_tx_fill_seg_desc(seg_desc, skb_network_offset(skb),
				     skb_shinfo(skb)->gso_size,
				     skb_is_gso_v6(skb), is_gso, len, addr);
	}

	return num_descriptors;

unmap_drop:
	i += num_descriptors - shinfo->nr_frags;
	while (i--) {
		/* Skip metadata descriptor, if set */
		if (i == 1 && mtd_desc_nr == 1)
			continue;
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

static int gve_tx_fill_xdp(struct gve_priv *priv, struct gve_tx_ring *tx,
			   void *data, int len, void *frame_p, bool is_xsk)
{
	int pad, nfrags, ndescs, iovi, offset;
	struct gve_tx_buffer_state *info;
	u32 reqi = tx->req;

	pad = gve_tx_fifo_pad_alloc_one_frag(&tx->tx_fifo, len);
	if (pad >= GVE_GQ_TX_MIN_PKT_DESC_BYTES)
		pad = 0;
	info = &tx->info[reqi & tx->mask];
	info->xdp_frame = frame_p;
	info->xdp.size = len;
	info->xdp.is_xsk = is_xsk;

	nfrags = gve_tx_alloc_fifo(&tx->tx_fifo, pad + len,
				   &info->iov[0]);
	iovi = pad > 0;
	ndescs = nfrags - iovi;
	offset = 0;

	while (iovi < nfrags) {
		if (!offset)
			gve_tx_fill_pkt_desc(&tx->desc[reqi & tx->mask], 0,
					     CHECKSUM_NONE, false, 0, ndescs,
					     info->iov[iovi].iov_len,
					     info->iov[iovi].iov_offset, len);
		else
			gve_tx_fill_seg_desc(&tx->desc[reqi & tx->mask],
					     0, 0, false, false,
					     info->iov[iovi].iov_len,
					     info->iov[iovi].iov_offset);

		memcpy(tx->tx_fifo.base + info->iov[iovi].iov_offset,
		       data + offset, info->iov[iovi].iov_len);
		gve_dma_sync_for_device(&priv->pdev->dev,
					tx->tx_fifo.qpl->page_buses,
					info->iov[iovi].iov_offset,
					info->iov[iovi].iov_len);
		offset += info->iov[iovi].iov_len;
		iovi++;
		reqi++;
	}

	return ndescs;
}

int gve_xdp_xmit(struct net_device *dev, int n, struct xdp_frame **frames,
		 u32 flags)
{
	struct gve_priv *priv = netdev_priv(dev);
	struct gve_tx_ring *tx;
	int i, err = 0, qid;

	if (unlikely(flags & ~XDP_XMIT_FLAGS_MASK) || !priv->xdp_prog)
		return -EINVAL;

	if (!gve_get_napi_enabled(priv))
		return -ENETDOWN;

	qid = gve_xdp_tx_queue_id(priv,
				  smp_processor_id() % priv->tx_cfg.num_xdp_queues);

	tx = &priv->tx[qid];

	spin_lock(&tx->xdp_lock);
	for (i = 0; i < n; i++) {
		err = gve_xdp_xmit_one(priv, tx, frames[i]->data,
				       frames[i]->len, frames[i]);
		if (err)
			break;
	}

	if (flags & XDP_XMIT_FLUSH)
		gve_tx_put_doorbell(priv, tx->q_resources, tx->req);

	spin_unlock(&tx->xdp_lock);

	u64_stats_update_begin(&tx->statss);
	tx->xdp_xmit += n;
	tx->xdp_xmit_errors += n - i;
	u64_stats_update_end(&tx->statss);

	return i ? i : err;
}

int gve_xdp_xmit_one(struct gve_priv *priv, struct gve_tx_ring *tx,
		     void *data, int len, void *frame_p)
{
	int nsegs;

	if (!gve_can_tx(tx, len + GVE_GQ_TX_MIN_PKT_DESC_BYTES - 1))
		return -EBUSY;

	nsegs = gve_tx_fill_xdp(priv, tx, data, len, frame_p, false);
	tx->req += nsegs;

	return 0;
}

#define GVE_TX_START_THRESH	4096

static int gve_clean_tx_done(struct gve_priv *priv, struct gve_tx_ring *tx,
			     u32 to_do, bool try_to_wake)
{
	struct gve_tx_buffer_state *info;
	u64 pkts = 0, bytes = 0;
	size_t space_freed = 0;
	struct sk_buff *skb;
	u32 idx;
	int j;

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
			space_freed += gve_tx_clear_buffer_state(info);
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

static int gve_xsk_tx(struct gve_priv *priv, struct gve_tx_ring *tx,
		      int budget)
{
	struct xdp_desc desc;
	int sent = 0, nsegs;
	void *data;

	spin_lock(&tx->xdp_lock);
	while (sent < budget) {
		if (!gve_can_tx(tx, GVE_TX_START_THRESH) ||
		    !xsk_tx_peek_desc(tx->xsk_pool, &desc))
			goto out;

		data = xsk_buff_raw_get_data(tx->xsk_pool, desc.addr);
		nsegs = gve_tx_fill_xdp(priv, tx, data, desc.len, NULL, true);
		tx->req += nsegs;
		sent++;
	}
out:
	if (sent > 0) {
		gve_tx_put_doorbell(priv, tx->q_resources, tx->req);
		xsk_tx_release(tx->xsk_pool);
	}
	spin_unlock(&tx->xdp_lock);
	return sent;
}

int gve_xsk_tx_poll(struct gve_notify_block *rx_block, int budget)
{
	struct gve_rx_ring *rx = rx_block->rx;
	struct gve_priv *priv = rx->gve;
	struct gve_tx_ring *tx;
	int sent = 0;

	tx = &priv->tx[gve_xdp_tx_queue_id(priv, rx->q_num)];
	if (tx->xsk_pool) {
		sent = gve_xsk_tx(priv, tx, budget);

		u64_stats_update_begin(&tx->statss);
		tx->xdp_xsk_sent += sent;
		u64_stats_update_end(&tx->statss);
		if (xsk_uses_need_wakeup(tx->xsk_pool))
			xsk_set_tx_need_wakeup(tx->xsk_pool);
	}

	return sent;
}

bool gve_xdp_poll(struct gve_notify_block *block, int budget)
{
	struct gve_priv *priv = block->priv;
	struct gve_tx_ring *tx = block->tx;
	u32 nic_done;
	u32 to_do;

	/* Find out how much work there is to be done */
	nic_done = gve_tx_load_event_counter(priv, tx);
	to_do = min_t(u32, (nic_done - tx->done), budget);
	gve_clean_xdp_done(priv, tx, to_do);

	/* If we still have work we want to repoll */
	return nic_done != tx->done;
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
