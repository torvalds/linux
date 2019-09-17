// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2018 Intel Corporation. */

#include <linux/bpf_trace.h>
#include <net/xdp_sock.h>
#include <net/xdp.h>

#include "ixgbe.h"
#include "ixgbe_txrx_common.h"

struct xdp_umem *ixgbe_xsk_umem(struct ixgbe_adapter *adapter,
				struct ixgbe_ring *ring)
{
	bool xdp_on = READ_ONCE(adapter->xdp_prog);
	int qid = ring->ring_idx;

	if (!xdp_on || !test_bit(qid, adapter->af_xdp_zc_qps))
		return NULL;

	return xdp_get_umem_from_qid(adapter->netdev, qid);
}

static int ixgbe_xsk_umem_dma_map(struct ixgbe_adapter *adapter,
				  struct xdp_umem *umem)
{
	struct device *dev = &adapter->pdev->dev;
	unsigned int i, j;
	dma_addr_t dma;

	for (i = 0; i < umem->npgs; i++) {
		dma = dma_map_page_attrs(dev, umem->pgs[i], 0, PAGE_SIZE,
					 DMA_BIDIRECTIONAL, IXGBE_RX_DMA_ATTR);
		if (dma_mapping_error(dev, dma))
			goto out_unmap;

		umem->pages[i].dma = dma;
	}

	return 0;

out_unmap:
	for (j = 0; j < i; j++) {
		dma_unmap_page_attrs(dev, umem->pages[i].dma, PAGE_SIZE,
				     DMA_BIDIRECTIONAL, IXGBE_RX_DMA_ATTR);
		umem->pages[i].dma = 0;
	}

	return -1;
}

static void ixgbe_xsk_umem_dma_unmap(struct ixgbe_adapter *adapter,
				     struct xdp_umem *umem)
{
	struct device *dev = &adapter->pdev->dev;
	unsigned int i;

	for (i = 0; i < umem->npgs; i++) {
		dma_unmap_page_attrs(dev, umem->pages[i].dma, PAGE_SIZE,
				     DMA_BIDIRECTIONAL, IXGBE_RX_DMA_ATTR);

		umem->pages[i].dma = 0;
	}
}

static int ixgbe_xsk_umem_enable(struct ixgbe_adapter *adapter,
				 struct xdp_umem *umem,
				 u16 qid)
{
	struct net_device *netdev = adapter->netdev;
	struct xdp_umem_fq_reuse *reuseq;
	bool if_running;
	int err;

	if (qid >= adapter->num_rx_queues)
		return -EINVAL;

	if (qid >= netdev->real_num_rx_queues ||
	    qid >= netdev->real_num_tx_queues)
		return -EINVAL;

	reuseq = xsk_reuseq_prepare(adapter->rx_ring[0]->count);
	if (!reuseq)
		return -ENOMEM;

	xsk_reuseq_free(xsk_reuseq_swap(umem, reuseq));

	err = ixgbe_xsk_umem_dma_map(adapter, umem);
	if (err)
		return err;

	if_running = netif_running(adapter->netdev) &&
		     ixgbe_enabled_xdp_adapter(adapter);

	if (if_running)
		ixgbe_txrx_ring_disable(adapter, qid);

	set_bit(qid, adapter->af_xdp_zc_qps);

	if (if_running) {
		ixgbe_txrx_ring_enable(adapter, qid);

		/* Kick start the NAPI context so that receiving will start */
		err = ixgbe_xsk_async_xmit(adapter->netdev, qid);
		if (err)
			return err;
	}

	return 0;
}

static int ixgbe_xsk_umem_disable(struct ixgbe_adapter *adapter, u16 qid)
{
	struct xdp_umem *umem;
	bool if_running;

	umem = xdp_get_umem_from_qid(adapter->netdev, qid);
	if (!umem)
		return -EINVAL;

	if_running = netif_running(adapter->netdev) &&
		     ixgbe_enabled_xdp_adapter(adapter);

	if (if_running)
		ixgbe_txrx_ring_disable(adapter, qid);

	clear_bit(qid, adapter->af_xdp_zc_qps);
	ixgbe_xsk_umem_dma_unmap(adapter, umem);

	if (if_running)
		ixgbe_txrx_ring_enable(adapter, qid);

	return 0;
}

int ixgbe_xsk_umem_setup(struct ixgbe_adapter *adapter, struct xdp_umem *umem,
			 u16 qid)
{
	return umem ? ixgbe_xsk_umem_enable(adapter, umem, qid) :
		ixgbe_xsk_umem_disable(adapter, qid);
}

static int ixgbe_run_xdp_zc(struct ixgbe_adapter *adapter,
			    struct ixgbe_ring *rx_ring,
			    struct xdp_buff *xdp)
{
	int err, result = IXGBE_XDP_PASS;
	struct bpf_prog *xdp_prog;
	struct xdp_frame *xdpf;
	u32 act;

	rcu_read_lock();
	xdp_prog = READ_ONCE(rx_ring->xdp_prog);
	act = bpf_prog_run_xdp(xdp_prog, xdp);
	xdp->handle += xdp->data - xdp->data_hard_start;
	switch (act) {
	case XDP_PASS:
		break;
	case XDP_TX:
		xdpf = convert_to_xdp_frame(xdp);
		if (unlikely(!xdpf)) {
			result = IXGBE_XDP_CONSUMED;
			break;
		}
		result = ixgbe_xmit_xdp_ring(adapter, xdpf);
		break;
	case XDP_REDIRECT:
		err = xdp_do_redirect(rx_ring->netdev, xdp, xdp_prog);
		result = !err ? IXGBE_XDP_REDIR : IXGBE_XDP_CONSUMED;
		break;
	default:
		bpf_warn_invalid_xdp_action(act);
		/* fallthrough */
	case XDP_ABORTED:
		trace_xdp_exception(rx_ring->netdev, xdp_prog, act);
		/* fallthrough -- handle aborts by dropping packet */
	case XDP_DROP:
		result = IXGBE_XDP_CONSUMED;
		break;
	}
	rcu_read_unlock();
	return result;
}

static struct
ixgbe_rx_buffer *ixgbe_get_rx_buffer_zc(struct ixgbe_ring *rx_ring,
					unsigned int size)
{
	struct ixgbe_rx_buffer *bi;

	bi = &rx_ring->rx_buffer_info[rx_ring->next_to_clean];

	/* we are reusing so sync this buffer for CPU use */
	dma_sync_single_range_for_cpu(rx_ring->dev,
				      bi->dma, 0,
				      size,
				      DMA_BIDIRECTIONAL);

	return bi;
}

static void ixgbe_reuse_rx_buffer_zc(struct ixgbe_ring *rx_ring,
				     struct ixgbe_rx_buffer *obi)
{
	unsigned long mask = (unsigned long)rx_ring->xsk_umem->chunk_mask;
	u64 hr = rx_ring->xsk_umem->headroom + XDP_PACKET_HEADROOM;
	u16 nta = rx_ring->next_to_alloc;
	struct ixgbe_rx_buffer *nbi;

	nbi = &rx_ring->rx_buffer_info[rx_ring->next_to_alloc];
	/* update, and store next to alloc */
	nta++;
	rx_ring->next_to_alloc = (nta < rx_ring->count) ? nta : 0;

	/* transfer page from old buffer to new buffer */
	nbi->dma = obi->dma & mask;
	nbi->dma += hr;

	nbi->addr = (void *)((unsigned long)obi->addr & mask);
	nbi->addr += hr;

	nbi->handle = obi->handle & mask;
	nbi->handle += rx_ring->xsk_umem->headroom;

	obi->addr = NULL;
	obi->skb = NULL;
}

void ixgbe_zca_free(struct zero_copy_allocator *alloc, unsigned long handle)
{
	struct ixgbe_rx_buffer *bi;
	struct ixgbe_ring *rx_ring;
	u64 hr, mask;
	u16 nta;

	rx_ring = container_of(alloc, struct ixgbe_ring, zca);
	hr = rx_ring->xsk_umem->headroom + XDP_PACKET_HEADROOM;
	mask = rx_ring->xsk_umem->chunk_mask;

	nta = rx_ring->next_to_alloc;
	bi = rx_ring->rx_buffer_info;

	nta++;
	rx_ring->next_to_alloc = (nta < rx_ring->count) ? nta : 0;

	handle &= mask;

	bi->dma = xdp_umem_get_dma(rx_ring->xsk_umem, handle);
	bi->dma += hr;

	bi->addr = xdp_umem_get_data(rx_ring->xsk_umem, handle);
	bi->addr += hr;

	bi->handle = (u64)handle + rx_ring->xsk_umem->headroom;
}

static bool ixgbe_alloc_buffer_zc(struct ixgbe_ring *rx_ring,
				  struct ixgbe_rx_buffer *bi)
{
	struct xdp_umem *umem = rx_ring->xsk_umem;
	void *addr = bi->addr;
	u64 handle, hr;

	if (addr)
		return true;

	if (!xsk_umem_peek_addr(umem, &handle)) {
		rx_ring->rx_stats.alloc_rx_page_failed++;
		return false;
	}

	hr = umem->headroom + XDP_PACKET_HEADROOM;

	bi->dma = xdp_umem_get_dma(umem, handle);
	bi->dma += hr;

	bi->addr = xdp_umem_get_data(umem, handle);
	bi->addr += hr;

	bi->handle = handle + umem->headroom;

	xsk_umem_discard_addr(umem);
	return true;
}

static bool ixgbe_alloc_buffer_slow_zc(struct ixgbe_ring *rx_ring,
				       struct ixgbe_rx_buffer *bi)
{
	struct xdp_umem *umem = rx_ring->xsk_umem;
	u64 handle, hr;

	if (!xsk_umem_peek_addr_rq(umem, &handle)) {
		rx_ring->rx_stats.alloc_rx_page_failed++;
		return false;
	}

	handle &= rx_ring->xsk_umem->chunk_mask;

	hr = umem->headroom + XDP_PACKET_HEADROOM;

	bi->dma = xdp_umem_get_dma(umem, handle);
	bi->dma += hr;

	bi->addr = xdp_umem_get_data(umem, handle);
	bi->addr += hr;

	bi->handle = handle + umem->headroom;

	xsk_umem_discard_addr_rq(umem);
	return true;
}

static __always_inline bool
__ixgbe_alloc_rx_buffers_zc(struct ixgbe_ring *rx_ring, u16 cleaned_count,
			    bool alloc(struct ixgbe_ring *rx_ring,
				       struct ixgbe_rx_buffer *bi))
{
	union ixgbe_adv_rx_desc *rx_desc;
	struct ixgbe_rx_buffer *bi;
	u16 i = rx_ring->next_to_use;
	bool ok = true;

	/* nothing to do */
	if (!cleaned_count)
		return true;

	rx_desc = IXGBE_RX_DESC(rx_ring, i);
	bi = &rx_ring->rx_buffer_info[i];
	i -= rx_ring->count;

	do {
		if (!alloc(rx_ring, bi)) {
			ok = false;
			break;
		}

		/* sync the buffer for use by the device */
		dma_sync_single_range_for_device(rx_ring->dev, bi->dma,
						 bi->page_offset,
						 rx_ring->rx_buf_len,
						 DMA_BIDIRECTIONAL);

		/* Refresh the desc even if buffer_addrs didn't change
		 * because each write-back erases this info.
		 */
		rx_desc->read.pkt_addr = cpu_to_le64(bi->dma);

		rx_desc++;
		bi++;
		i++;
		if (unlikely(!i)) {
			rx_desc = IXGBE_RX_DESC(rx_ring, 0);
			bi = rx_ring->rx_buffer_info;
			i -= rx_ring->count;
		}

		/* clear the length for the next_to_use descriptor */
		rx_desc->wb.upper.length = 0;

		cleaned_count--;
	} while (cleaned_count);

	i += rx_ring->count;

	if (rx_ring->next_to_use != i) {
		rx_ring->next_to_use = i;

		/* update next to alloc since we have filled the ring */
		rx_ring->next_to_alloc = i;

		/* Force memory writes to complete before letting h/w
		 * know there are new descriptors to fetch.  (Only
		 * applicable for weak-ordered memory model archs,
		 * such as IA-64).
		 */
		wmb();
		writel(i, rx_ring->tail);
	}

	return ok;
}

void ixgbe_alloc_rx_buffers_zc(struct ixgbe_ring *rx_ring, u16 count)
{
	__ixgbe_alloc_rx_buffers_zc(rx_ring, count,
				    ixgbe_alloc_buffer_slow_zc);
}

static bool ixgbe_alloc_rx_buffers_fast_zc(struct ixgbe_ring *rx_ring,
					   u16 count)
{
	return __ixgbe_alloc_rx_buffers_zc(rx_ring, count,
					   ixgbe_alloc_buffer_zc);
}

static struct sk_buff *ixgbe_construct_skb_zc(struct ixgbe_ring *rx_ring,
					      struct ixgbe_rx_buffer *bi,
					      struct xdp_buff *xdp)
{
	unsigned int metasize = xdp->data - xdp->data_meta;
	unsigned int datasize = xdp->data_end - xdp->data;
	struct sk_buff *skb;

	/* allocate a skb to store the frags */
	skb = __napi_alloc_skb(&rx_ring->q_vector->napi,
			       xdp->data_end - xdp->data_hard_start,
			       GFP_ATOMIC | __GFP_NOWARN);
	if (unlikely(!skb))
		return NULL;

	skb_reserve(skb, xdp->data - xdp->data_hard_start);
	memcpy(__skb_put(skb, datasize), xdp->data, datasize);
	if (metasize)
		skb_metadata_set(skb, metasize);

	ixgbe_reuse_rx_buffer_zc(rx_ring, bi);
	return skb;
}

static void ixgbe_inc_ntc(struct ixgbe_ring *rx_ring)
{
	u32 ntc = rx_ring->next_to_clean + 1;

	ntc = (ntc < rx_ring->count) ? ntc : 0;
	rx_ring->next_to_clean = ntc;
	prefetch(IXGBE_RX_DESC(rx_ring, ntc));
}

int ixgbe_clean_rx_irq_zc(struct ixgbe_q_vector *q_vector,
			  struct ixgbe_ring *rx_ring,
			  const int budget)
{
	unsigned int total_rx_bytes = 0, total_rx_packets = 0;
	struct ixgbe_adapter *adapter = q_vector->adapter;
	u16 cleaned_count = ixgbe_desc_unused(rx_ring);
	unsigned int xdp_res, xdp_xmit = 0;
	bool failure = false;
	struct sk_buff *skb;
	struct xdp_buff xdp;

	xdp.rxq = &rx_ring->xdp_rxq;

	while (likely(total_rx_packets < budget)) {
		union ixgbe_adv_rx_desc *rx_desc;
		struct ixgbe_rx_buffer *bi;
		unsigned int size;

		/* return some buffers to hardware, one at a time is too slow */
		if (cleaned_count >= IXGBE_RX_BUFFER_WRITE) {
			failure = failure ||
				  !ixgbe_alloc_rx_buffers_fast_zc(rx_ring,
								 cleaned_count);
			cleaned_count = 0;
		}

		rx_desc = IXGBE_RX_DESC(rx_ring, rx_ring->next_to_clean);
		size = le16_to_cpu(rx_desc->wb.upper.length);
		if (!size)
			break;

		/* This memory barrier is needed to keep us from reading
		 * any other fields out of the rx_desc until we know the
		 * descriptor has been written back
		 */
		dma_rmb();

		bi = ixgbe_get_rx_buffer_zc(rx_ring, size);

		if (unlikely(!ixgbe_test_staterr(rx_desc,
						 IXGBE_RXD_STAT_EOP))) {
			struct ixgbe_rx_buffer *next_bi;

			ixgbe_reuse_rx_buffer_zc(rx_ring, bi);
			ixgbe_inc_ntc(rx_ring);
			next_bi =
			       &rx_ring->rx_buffer_info[rx_ring->next_to_clean];
			next_bi->skb = ERR_PTR(-EINVAL);
			continue;
		}

		if (unlikely(bi->skb)) {
			ixgbe_reuse_rx_buffer_zc(rx_ring, bi);
			ixgbe_inc_ntc(rx_ring);
			continue;
		}

		xdp.data = bi->addr;
		xdp.data_meta = xdp.data;
		xdp.data_hard_start = xdp.data - XDP_PACKET_HEADROOM;
		xdp.data_end = xdp.data + size;
		xdp.handle = bi->handle;

		xdp_res = ixgbe_run_xdp_zc(adapter, rx_ring, &xdp);

		if (xdp_res) {
			if (xdp_res & (IXGBE_XDP_TX | IXGBE_XDP_REDIR)) {
				xdp_xmit |= xdp_res;
				bi->addr = NULL;
				bi->skb = NULL;
			} else {
				ixgbe_reuse_rx_buffer_zc(rx_ring, bi);
			}
			total_rx_packets++;
			total_rx_bytes += size;

			cleaned_count++;
			ixgbe_inc_ntc(rx_ring);
			continue;
		}

		/* XDP_PASS path */
		skb = ixgbe_construct_skb_zc(rx_ring, bi, &xdp);
		if (!skb) {
			rx_ring->rx_stats.alloc_rx_buff_failed++;
			break;
		}

		cleaned_count++;
		ixgbe_inc_ntc(rx_ring);

		if (eth_skb_pad(skb))
			continue;

		total_rx_bytes += skb->len;
		total_rx_packets++;

		ixgbe_process_skb_fields(rx_ring, rx_desc, skb);
		ixgbe_rx_skb(q_vector, skb);
	}

	if (xdp_xmit & IXGBE_XDP_REDIR)
		xdp_do_flush_map();

	if (xdp_xmit & IXGBE_XDP_TX) {
		struct ixgbe_ring *ring = adapter->xdp_ring[smp_processor_id()];

		/* Force memory writes to complete before letting h/w
		 * know there are new descriptors to fetch.
		 */
		wmb();
		writel(ring->next_to_use, ring->tail);
	}

	u64_stats_update_begin(&rx_ring->syncp);
	rx_ring->stats.packets += total_rx_packets;
	rx_ring->stats.bytes += total_rx_bytes;
	u64_stats_update_end(&rx_ring->syncp);
	q_vector->rx.total_packets += total_rx_packets;
	q_vector->rx.total_bytes += total_rx_bytes;

	return failure ? budget : (int)total_rx_packets;
}

void ixgbe_xsk_clean_rx_ring(struct ixgbe_ring *rx_ring)
{
	u16 i = rx_ring->next_to_clean;
	struct ixgbe_rx_buffer *bi = &rx_ring->rx_buffer_info[i];

	while (i != rx_ring->next_to_alloc) {
		xsk_umem_fq_reuse(rx_ring->xsk_umem, bi->handle);
		i++;
		bi++;
		if (i == rx_ring->count) {
			i = 0;
			bi = rx_ring->rx_buffer_info;
		}
	}
}

static bool ixgbe_xmit_zc(struct ixgbe_ring *xdp_ring, unsigned int budget)
{
	union ixgbe_adv_tx_desc *tx_desc = NULL;
	struct ixgbe_tx_buffer *tx_bi;
	bool work_done = true;
	struct xdp_desc desc;
	dma_addr_t dma;
	u32 cmd_type;

	while (budget-- > 0) {
		if (unlikely(!ixgbe_desc_unused(xdp_ring)) ||
		    !netif_carrier_ok(xdp_ring->netdev)) {
			work_done = false;
			break;
		}

		if (!xsk_umem_consume_tx(xdp_ring->xsk_umem, &desc))
			break;

		dma = xdp_umem_get_dma(xdp_ring->xsk_umem, desc.addr);

		dma_sync_single_for_device(xdp_ring->dev, dma, desc.len,
					   DMA_BIDIRECTIONAL);

		tx_bi = &xdp_ring->tx_buffer_info[xdp_ring->next_to_use];
		tx_bi->bytecount = desc.len;
		tx_bi->xdpf = NULL;
		tx_bi->gso_segs = 1;

		tx_desc = IXGBE_TX_DESC(xdp_ring, xdp_ring->next_to_use);
		tx_desc->read.buffer_addr = cpu_to_le64(dma);

		/* put descriptor type bits */
		cmd_type = IXGBE_ADVTXD_DTYP_DATA |
			   IXGBE_ADVTXD_DCMD_DEXT |
			   IXGBE_ADVTXD_DCMD_IFCS;
		cmd_type |= desc.len | IXGBE_TXD_CMD;
		tx_desc->read.cmd_type_len = cpu_to_le32(cmd_type);
		tx_desc->read.olinfo_status =
			cpu_to_le32(desc.len << IXGBE_ADVTXD_PAYLEN_SHIFT);

		xdp_ring->next_to_use++;
		if (xdp_ring->next_to_use == xdp_ring->count)
			xdp_ring->next_to_use = 0;
	}

	if (tx_desc) {
		ixgbe_xdp_ring_update_tail(xdp_ring);
		xsk_umem_consume_tx_done(xdp_ring->xsk_umem);
	}

	return !!budget && work_done;
}

static void ixgbe_clean_xdp_tx_buffer(struct ixgbe_ring *tx_ring,
				      struct ixgbe_tx_buffer *tx_bi)
{
	xdp_return_frame(tx_bi->xdpf);
	dma_unmap_single(tx_ring->dev,
			 dma_unmap_addr(tx_bi, dma),
			 dma_unmap_len(tx_bi, len), DMA_TO_DEVICE);
	dma_unmap_len_set(tx_bi, len, 0);
}

bool ixgbe_clean_xdp_tx_irq(struct ixgbe_q_vector *q_vector,
			    struct ixgbe_ring *tx_ring, int napi_budget)
{
	u16 ntc = tx_ring->next_to_clean, ntu = tx_ring->next_to_use;
	unsigned int total_packets = 0, total_bytes = 0;
	struct xdp_umem *umem = tx_ring->xsk_umem;
	union ixgbe_adv_tx_desc *tx_desc;
	struct ixgbe_tx_buffer *tx_bi;
	u32 xsk_frames = 0;

	tx_bi = &tx_ring->tx_buffer_info[ntc];
	tx_desc = IXGBE_TX_DESC(tx_ring, ntc);

	while (ntc != ntu) {
		if (!(tx_desc->wb.status & cpu_to_le32(IXGBE_TXD_STAT_DD)))
			break;

		total_bytes += tx_bi->bytecount;
		total_packets += tx_bi->gso_segs;

		if (tx_bi->xdpf)
			ixgbe_clean_xdp_tx_buffer(tx_ring, tx_bi);
		else
			xsk_frames++;

		tx_bi->xdpf = NULL;

		tx_bi++;
		tx_desc++;
		ntc++;
		if (unlikely(ntc == tx_ring->count)) {
			ntc = 0;
			tx_bi = tx_ring->tx_buffer_info;
			tx_desc = IXGBE_TX_DESC(tx_ring, 0);
		}

		/* issue prefetch for next Tx descriptor */
		prefetch(tx_desc);
	}

	tx_ring->next_to_clean = ntc;

	u64_stats_update_begin(&tx_ring->syncp);
	tx_ring->stats.bytes += total_bytes;
	tx_ring->stats.packets += total_packets;
	u64_stats_update_end(&tx_ring->syncp);
	q_vector->tx.total_bytes += total_bytes;
	q_vector->tx.total_packets += total_packets;

	if (xsk_frames)
		xsk_umem_complete_tx(umem, xsk_frames);

	return ixgbe_xmit_zc(tx_ring, q_vector->tx.work_limit);
}

int ixgbe_xsk_async_xmit(struct net_device *dev, u32 qid)
{
	struct ixgbe_adapter *adapter = netdev_priv(dev);
	struct ixgbe_ring *ring;

	if (test_bit(__IXGBE_DOWN, &adapter->state))
		return -ENETDOWN;

	if (!READ_ONCE(adapter->xdp_prog))
		return -ENXIO;

	if (qid >= adapter->num_xdp_queues)
		return -ENXIO;

	if (!adapter->xdp_ring[qid]->xsk_umem)
		return -ENXIO;

	ring = adapter->xdp_ring[qid];
	if (!napi_if_scheduled_mark_missed(&ring->q_vector->napi)) {
		u64 eics = BIT_ULL(ring->q_vector->v_idx);

		ixgbe_irq_rearm_queues(adapter, eics);
	}

	return 0;
}

void ixgbe_xsk_clean_tx_ring(struct ixgbe_ring *tx_ring)
{
	u16 ntc = tx_ring->next_to_clean, ntu = tx_ring->next_to_use;
	struct xdp_umem *umem = tx_ring->xsk_umem;
	struct ixgbe_tx_buffer *tx_bi;
	u32 xsk_frames = 0;

	while (ntc != ntu) {
		tx_bi = &tx_ring->tx_buffer_info[ntc];

		if (tx_bi->xdpf)
			ixgbe_clean_xdp_tx_buffer(tx_ring, tx_bi);
		else
			xsk_frames++;

		tx_bi->xdpf = NULL;

		ntc++;
		if (ntc == tx_ring->count)
			ntc = 0;
	}

	if (xsk_frames)
		xsk_umem_complete_tx(umem, xsk_frames);
}
