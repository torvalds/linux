// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2018 Intel Corporation. */

#include <linux/bpf_trace.h>
#include <net/xdp_sock.h>
#include <net/xdp.h>

#include "i40e.h"
#include "i40e_txrx_common.h"
#include "i40e_xsk.h"

/**
 * i40e_xsk_umem_dma_map - DMA maps all UMEM memory for the netdev
 * @vsi: Current VSI
 * @umem: UMEM to DMA map
 *
 * Returns 0 on success, <0 on failure
 **/
static int i40e_xsk_umem_dma_map(struct i40e_vsi *vsi, struct xdp_umem *umem)
{
	struct i40e_pf *pf = vsi->back;
	struct device *dev;
	unsigned int i, j;
	dma_addr_t dma;

	dev = &pf->pdev->dev;
	for (i = 0; i < umem->npgs; i++) {
		dma = dma_map_page_attrs(dev, umem->pgs[i], 0, PAGE_SIZE,
					 DMA_BIDIRECTIONAL, I40E_RX_DMA_ATTR);
		if (dma_mapping_error(dev, dma))
			goto out_unmap;

		umem->pages[i].dma = dma;
	}

	return 0;

out_unmap:
	for (j = 0; j < i; j++) {
		dma_unmap_page_attrs(dev, umem->pages[i].dma, PAGE_SIZE,
				     DMA_BIDIRECTIONAL, I40E_RX_DMA_ATTR);
		umem->pages[i].dma = 0;
	}

	return -1;
}

/**
 * i40e_xsk_umem_dma_unmap - DMA unmaps all UMEM memory for the netdev
 * @vsi: Current VSI
 * @umem: UMEM to DMA map
 **/
static void i40e_xsk_umem_dma_unmap(struct i40e_vsi *vsi, struct xdp_umem *umem)
{
	struct i40e_pf *pf = vsi->back;
	struct device *dev;
	unsigned int i;

	dev = &pf->pdev->dev;

	for (i = 0; i < umem->npgs; i++) {
		dma_unmap_page_attrs(dev, umem->pages[i].dma, PAGE_SIZE,
				     DMA_BIDIRECTIONAL, I40E_RX_DMA_ATTR);

		umem->pages[i].dma = 0;
	}
}

/**
 * i40e_xsk_umem_enable - Enable/associate a UMEM to a certain ring/qid
 * @vsi: Current VSI
 * @umem: UMEM
 * @qid: Rx ring to associate UMEM to
 *
 * Returns 0 on success, <0 on failure
 **/
static int i40e_xsk_umem_enable(struct i40e_vsi *vsi, struct xdp_umem *umem,
				u16 qid)
{
	struct net_device *netdev = vsi->netdev;
	struct xdp_umem_fq_reuse *reuseq;
	bool if_running;
	int err;

	if (vsi->type != I40E_VSI_MAIN)
		return -EINVAL;

	if (qid >= vsi->num_queue_pairs)
		return -EINVAL;

	if (qid >= netdev->real_num_rx_queues ||
	    qid >= netdev->real_num_tx_queues)
		return -EINVAL;

	reuseq = xsk_reuseq_prepare(vsi->rx_rings[0]->count);
	if (!reuseq)
		return -ENOMEM;

	xsk_reuseq_free(xsk_reuseq_swap(umem, reuseq));

	err = i40e_xsk_umem_dma_map(vsi, umem);
	if (err)
		return err;

	set_bit(qid, vsi->af_xdp_zc_qps);

	if_running = netif_running(vsi->netdev) && i40e_enabled_xdp_vsi(vsi);

	if (if_running) {
		err = i40e_queue_pair_disable(vsi, qid);
		if (err)
			return err;

		err = i40e_queue_pair_enable(vsi, qid);
		if (err)
			return err;

		/* Kick start the NAPI context so that receiving will start */
		err = i40e_xsk_async_xmit(vsi->netdev, qid);
		if (err)
			return err;
	}

	return 0;
}

/**
 * i40e_xsk_umem_disable - Disassociate a UMEM from a certain ring/qid
 * @vsi: Current VSI
 * @qid: Rx ring to associate UMEM to
 *
 * Returns 0 on success, <0 on failure
 **/
static int i40e_xsk_umem_disable(struct i40e_vsi *vsi, u16 qid)
{
	struct net_device *netdev = vsi->netdev;
	struct xdp_umem *umem;
	bool if_running;
	int err;

	umem = xdp_get_umem_from_qid(netdev, qid);
	if (!umem)
		return -EINVAL;

	if_running = netif_running(vsi->netdev) && i40e_enabled_xdp_vsi(vsi);

	if (if_running) {
		err = i40e_queue_pair_disable(vsi, qid);
		if (err)
			return err;
	}

	clear_bit(qid, vsi->af_xdp_zc_qps);
	i40e_xsk_umem_dma_unmap(vsi, umem);

	if (if_running) {
		err = i40e_queue_pair_enable(vsi, qid);
		if (err)
			return err;
	}

	return 0;
}

/**
 * i40e_xsk_umem_setup - Enable/disassociate a UMEM to/from a ring/qid
 * @vsi: Current VSI
 * @umem: UMEM to enable/associate to a ring, or NULL to disable
 * @qid: Rx ring to (dis)associate UMEM (from)to
 *
 * This function enables or disables a UMEM to a certain ring.
 *
 * Returns 0 on success, <0 on failure
 **/
int i40e_xsk_umem_setup(struct i40e_vsi *vsi, struct xdp_umem *umem,
			u16 qid)
{
	return umem ? i40e_xsk_umem_enable(vsi, umem, qid) :
		i40e_xsk_umem_disable(vsi, qid);
}

/**
 * i40e_run_xdp_zc - Executes an XDP program on an xdp_buff
 * @rx_ring: Rx ring
 * @xdp: xdp_buff used as input to the XDP program
 *
 * This function enables or disables a UMEM to a certain ring.
 *
 * Returns any of I40E_XDP_{PASS, CONSUMED, TX, REDIR}
 **/
static int i40e_run_xdp_zc(struct i40e_ring *rx_ring, struct xdp_buff *xdp)
{
	int err, result = I40E_XDP_PASS;
	struct i40e_ring *xdp_ring;
	struct bpf_prog *xdp_prog;
	u32 act;

	rcu_read_lock();
	/* NB! xdp_prog will always be !NULL, due to the fact that
	 * this path is enabled by setting an XDP program.
	 */
	xdp_prog = READ_ONCE(rx_ring->xdp_prog);
	act = bpf_prog_run_xdp(xdp_prog, xdp);
	xdp->handle += xdp->data - xdp->data_hard_start;
	switch (act) {
	case XDP_PASS:
		break;
	case XDP_TX:
		xdp_ring = rx_ring->vsi->xdp_rings[rx_ring->queue_index];
		result = i40e_xmit_xdp_tx_ring(xdp, xdp_ring);
		break;
	case XDP_REDIRECT:
		err = xdp_do_redirect(rx_ring->netdev, xdp, xdp_prog);
		result = !err ? I40E_XDP_REDIR : I40E_XDP_CONSUMED;
		break;
	default:
		bpf_warn_invalid_xdp_action(act);
	case XDP_ABORTED:
		trace_xdp_exception(rx_ring->netdev, xdp_prog, act);
		/* fallthrough -- handle aborts by dropping packet */
	case XDP_DROP:
		result = I40E_XDP_CONSUMED;
		break;
	}
	rcu_read_unlock();
	return result;
}

/**
 * i40e_alloc_buffer_zc - Allocates an i40e_rx_buffer
 * @rx_ring: Rx ring
 * @bi: Rx buffer to populate
 *
 * This function allocates an Rx buffer. The buffer can come from fill
 * queue, or via the recycle queue (next_to_alloc).
 *
 * Returns true for a successful allocation, false otherwise
 **/
static bool i40e_alloc_buffer_zc(struct i40e_ring *rx_ring,
				 struct i40e_rx_buffer *bi)
{
	struct xdp_umem *umem = rx_ring->xsk_umem;
	void *addr = bi->addr;
	u64 handle, hr;

	if (addr) {
		rx_ring->rx_stats.page_reuse_count++;
		return true;
	}

	if (!xsk_umem_peek_addr(umem, &handle)) {
		rx_ring->rx_stats.alloc_page_failed++;
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

/**
 * i40e_alloc_buffer_slow_zc - Allocates an i40e_rx_buffer
 * @rx_ring: Rx ring
 * @bi: Rx buffer to populate
 *
 * This function allocates an Rx buffer. The buffer can come from fill
 * queue, or via the reuse queue.
 *
 * Returns true for a successful allocation, false otherwise
 **/
static bool i40e_alloc_buffer_slow_zc(struct i40e_ring *rx_ring,
				      struct i40e_rx_buffer *bi)
{
	struct xdp_umem *umem = rx_ring->xsk_umem;
	u64 handle, hr;

	if (!xsk_umem_peek_addr_rq(umem, &handle)) {
		rx_ring->rx_stats.alloc_page_failed++;
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
__i40e_alloc_rx_buffers_zc(struct i40e_ring *rx_ring, u16 count,
			   bool alloc(struct i40e_ring *rx_ring,
				      struct i40e_rx_buffer *bi))
{
	u16 ntu = rx_ring->next_to_use;
	union i40e_rx_desc *rx_desc;
	struct i40e_rx_buffer *bi;
	bool ok = true;

	rx_desc = I40E_RX_DESC(rx_ring, ntu);
	bi = &rx_ring->rx_bi[ntu];
	do {
		if (!alloc(rx_ring, bi)) {
			ok = false;
			goto no_buffers;
		}

		dma_sync_single_range_for_device(rx_ring->dev, bi->dma, 0,
						 rx_ring->rx_buf_len,
						 DMA_BIDIRECTIONAL);

		rx_desc->read.pkt_addr = cpu_to_le64(bi->dma);

		rx_desc++;
		bi++;
		ntu++;

		if (unlikely(ntu == rx_ring->count)) {
			rx_desc = I40E_RX_DESC(rx_ring, 0);
			bi = rx_ring->rx_bi;
			ntu = 0;
		}

		rx_desc->wb.qword1.status_error_len = 0;
		count--;
	} while (count);

no_buffers:
	if (rx_ring->next_to_use != ntu)
		i40e_release_rx_desc(rx_ring, ntu);

	return ok;
}

/**
 * i40e_alloc_rx_buffers_zc - Allocates a number of Rx buffers
 * @rx_ring: Rx ring
 * @count: The number of buffers to allocate
 *
 * This function allocates a number of Rx buffers from the reuse queue
 * or fill ring and places them on the Rx ring.
 *
 * Returns true for a successful allocation, false otherwise
 **/
bool i40e_alloc_rx_buffers_zc(struct i40e_ring *rx_ring, u16 count)
{
	return __i40e_alloc_rx_buffers_zc(rx_ring, count,
					  i40e_alloc_buffer_slow_zc);
}

/**
 * i40e_alloc_rx_buffers_fast_zc - Allocates a number of Rx buffers
 * @rx_ring: Rx ring
 * @count: The number of buffers to allocate
 *
 * This function allocates a number of Rx buffers from the fill ring
 * or the internal recycle mechanism and places them on the Rx ring.
 *
 * Returns true for a successful allocation, false otherwise
 **/
static bool i40e_alloc_rx_buffers_fast_zc(struct i40e_ring *rx_ring, u16 count)
{
	return __i40e_alloc_rx_buffers_zc(rx_ring, count,
					  i40e_alloc_buffer_zc);
}

/**
 * i40e_get_rx_buffer_zc - Return the current Rx buffer
 * @rx_ring: Rx ring
 * @size: The size of the rx buffer (read from descriptor)
 *
 * This function returns the current, received Rx buffer, and also
 * does DMA synchronization.  the Rx ring.
 *
 * Returns the received Rx buffer
 **/
static struct i40e_rx_buffer *i40e_get_rx_buffer_zc(struct i40e_ring *rx_ring,
						    const unsigned int size)
{
	struct i40e_rx_buffer *bi;

	bi = &rx_ring->rx_bi[rx_ring->next_to_clean];

	/* we are reusing so sync this buffer for CPU use */
	dma_sync_single_range_for_cpu(rx_ring->dev,
				      bi->dma, 0,
				      size,
				      DMA_BIDIRECTIONAL);

	return bi;
}

/**
 * i40e_reuse_rx_buffer_zc - Recycle an Rx buffer
 * @rx_ring: Rx ring
 * @old_bi: The Rx buffer to recycle
 *
 * This function recycles a finished Rx buffer, and places it on the
 * recycle queue (next_to_alloc).
 **/
static void i40e_reuse_rx_buffer_zc(struct i40e_ring *rx_ring,
				    struct i40e_rx_buffer *old_bi)
{
	struct i40e_rx_buffer *new_bi = &rx_ring->rx_bi[rx_ring->next_to_alloc];
	unsigned long mask = (unsigned long)rx_ring->xsk_umem->chunk_mask;
	u64 hr = rx_ring->xsk_umem->headroom + XDP_PACKET_HEADROOM;
	u16 nta = rx_ring->next_to_alloc;

	/* update, and store next to alloc */
	nta++;
	rx_ring->next_to_alloc = (nta < rx_ring->count) ? nta : 0;

	/* transfer page from old buffer to new buffer */
	new_bi->dma = old_bi->dma & mask;
	new_bi->dma += hr;

	new_bi->addr = (void *)((unsigned long)old_bi->addr & mask);
	new_bi->addr += hr;

	new_bi->handle = old_bi->handle & mask;
	new_bi->handle += rx_ring->xsk_umem->headroom;

	old_bi->addr = NULL;
}

/**
 * i40e_zca_free - Free callback for MEM_TYPE_ZERO_COPY allocations
 * @alloc: Zero-copy allocator
 * @handle: Buffer handle
 **/
void i40e_zca_free(struct zero_copy_allocator *alloc, unsigned long handle)
{
	struct i40e_rx_buffer *bi;
	struct i40e_ring *rx_ring;
	u64 hr, mask;
	u16 nta;

	rx_ring = container_of(alloc, struct i40e_ring, zca);
	hr = rx_ring->xsk_umem->headroom + XDP_PACKET_HEADROOM;
	mask = rx_ring->xsk_umem->chunk_mask;

	nta = rx_ring->next_to_alloc;
	bi = &rx_ring->rx_bi[nta];

	nta++;
	rx_ring->next_to_alloc = (nta < rx_ring->count) ? nta : 0;

	handle &= mask;

	bi->dma = xdp_umem_get_dma(rx_ring->xsk_umem, handle);
	bi->dma += hr;

	bi->addr = xdp_umem_get_data(rx_ring->xsk_umem, handle);
	bi->addr += hr;

	bi->handle = (u64)handle + rx_ring->xsk_umem->headroom;
}

/**
 * i40e_construct_skb_zc - Create skbufff from zero-copy Rx buffer
 * @rx_ring: Rx ring
 * @bi: Rx buffer
 * @xdp: xdp_buff
 *
 * This functions allocates a new skb from a zero-copy Rx buffer.
 *
 * Returns the skb, or NULL on failure.
 **/
static struct sk_buff *i40e_construct_skb_zc(struct i40e_ring *rx_ring,
					     struct i40e_rx_buffer *bi,
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

	i40e_reuse_rx_buffer_zc(rx_ring, bi);
	return skb;
}

/**
 * i40e_inc_ntc: Advance the next_to_clean index
 * @rx_ring: Rx ring
 **/
static void i40e_inc_ntc(struct i40e_ring *rx_ring)
{
	u32 ntc = rx_ring->next_to_clean + 1;

	ntc = (ntc < rx_ring->count) ? ntc : 0;
	rx_ring->next_to_clean = ntc;
	prefetch(I40E_RX_DESC(rx_ring, ntc));
}

/**
 * i40e_clean_rx_irq_zc - Consumes Rx packets from the hardware ring
 * @rx_ring: Rx ring
 * @budget: NAPI budget
 *
 * Returns amount of work completed
 **/
int i40e_clean_rx_irq_zc(struct i40e_ring *rx_ring, int budget)
{
	unsigned int total_rx_bytes = 0, total_rx_packets = 0;
	u16 cleaned_count = I40E_DESC_UNUSED(rx_ring);
	unsigned int xdp_res, xdp_xmit = 0;
	bool failure = false;
	struct sk_buff *skb;
	struct xdp_buff xdp;

	xdp.rxq = &rx_ring->xdp_rxq;

	while (likely(total_rx_packets < (unsigned int)budget)) {
		struct i40e_rx_buffer *bi;
		union i40e_rx_desc *rx_desc;
		unsigned int size;
		u64 qword;

		if (cleaned_count >= I40E_RX_BUFFER_WRITE) {
			failure = failure ||
				  !i40e_alloc_rx_buffers_fast_zc(rx_ring,
								 cleaned_count);
			cleaned_count = 0;
		}

		rx_desc = I40E_RX_DESC(rx_ring, rx_ring->next_to_clean);
		qword = le64_to_cpu(rx_desc->wb.qword1.status_error_len);

		/* This memory barrier is needed to keep us from reading
		 * any other fields out of the rx_desc until we have
		 * verified the descriptor has been written back.
		 */
		dma_rmb();

		bi = i40e_clean_programming_status(rx_ring, rx_desc,
						   qword);
		if (unlikely(bi)) {
			i40e_reuse_rx_buffer_zc(rx_ring, bi);
			cleaned_count++;
			continue;
		}

		size = (qword & I40E_RXD_QW1_LENGTH_PBUF_MASK) >>
		       I40E_RXD_QW1_LENGTH_PBUF_SHIFT;
		if (!size)
			break;

		bi = i40e_get_rx_buffer_zc(rx_ring, size);
		xdp.data = bi->addr;
		xdp.data_meta = xdp.data;
		xdp.data_hard_start = xdp.data - XDP_PACKET_HEADROOM;
		xdp.data_end = xdp.data + size;
		xdp.handle = bi->handle;

		xdp_res = i40e_run_xdp_zc(rx_ring, &xdp);
		if (xdp_res) {
			if (xdp_res & (I40E_XDP_TX | I40E_XDP_REDIR)) {
				xdp_xmit |= xdp_res;
				bi->addr = NULL;
			} else {
				i40e_reuse_rx_buffer_zc(rx_ring, bi);
			}

			total_rx_bytes += size;
			total_rx_packets++;

			cleaned_count++;
			i40e_inc_ntc(rx_ring);
			continue;
		}

		/* XDP_PASS path */

		/* NB! We are not checking for errors using
		 * i40e_test_staterr with
		 * BIT(I40E_RXD_QW1_ERROR_SHIFT). This is due to that
		 * SBP is *not* set in PRT_SBPVSI (default not set).
		 */
		skb = i40e_construct_skb_zc(rx_ring, bi, &xdp);
		if (!skb) {
			rx_ring->rx_stats.alloc_buff_failed++;
			break;
		}

		cleaned_count++;
		i40e_inc_ntc(rx_ring);

		if (eth_skb_pad(skb))
			continue;

		total_rx_bytes += skb->len;
		total_rx_packets++;

		i40e_process_skb_fields(rx_ring, rx_desc, skb);
		napi_gro_receive(&rx_ring->q_vector->napi, skb);
	}

	i40e_finalize_xdp_rx(rx_ring, xdp_xmit);
	i40e_update_rx_stats(rx_ring, total_rx_bytes, total_rx_packets);
	return failure ? budget : (int)total_rx_packets;
}

/**
 * i40e_xmit_zc - Performs zero-copy Tx AF_XDP
 * @xdp_ring: XDP Tx ring
 * @budget: NAPI budget
 *
 * Returns true if the work is finished.
 **/
static bool i40e_xmit_zc(struct i40e_ring *xdp_ring, unsigned int budget)
{
	struct i40e_tx_desc *tx_desc = NULL;
	struct i40e_tx_buffer *tx_bi;
	bool work_done = true;
	dma_addr_t dma;
	u32 len;

	while (budget-- > 0) {
		if (!unlikely(I40E_DESC_UNUSED(xdp_ring))) {
			xdp_ring->tx_stats.tx_busy++;
			work_done = false;
			break;
		}

		if (!xsk_umem_consume_tx(xdp_ring->xsk_umem, &dma, &len))
			break;

		dma_sync_single_for_device(xdp_ring->dev, dma, len,
					   DMA_BIDIRECTIONAL);

		tx_bi = &xdp_ring->tx_bi[xdp_ring->next_to_use];
		tx_bi->bytecount = len;

		tx_desc = I40E_TX_DESC(xdp_ring, xdp_ring->next_to_use);
		tx_desc->buffer_addr = cpu_to_le64(dma);
		tx_desc->cmd_type_offset_bsz =
			build_ctob(I40E_TX_DESC_CMD_ICRC
				   | I40E_TX_DESC_CMD_EOP,
				   0, len, 0);

		xdp_ring->next_to_use++;
		if (xdp_ring->next_to_use == xdp_ring->count)
			xdp_ring->next_to_use = 0;
	}

	if (tx_desc) {
		/* Request an interrupt for the last frame and bump tail ptr. */
		tx_desc->cmd_type_offset_bsz |= (I40E_TX_DESC_CMD_RS <<
						 I40E_TXD_QW1_CMD_SHIFT);
		i40e_xdp_ring_update_tail(xdp_ring);

		xsk_umem_consume_tx_done(xdp_ring->xsk_umem);
	}

	return !!budget && work_done;
}

/**
 * i40e_clean_xdp_tx_buffer - Frees and unmaps an XDP Tx entry
 * @tx_ring: XDP Tx ring
 * @tx_bi: Tx buffer info to clean
 **/
static void i40e_clean_xdp_tx_buffer(struct i40e_ring *tx_ring,
				     struct i40e_tx_buffer *tx_bi)
{
	xdp_return_frame(tx_bi->xdpf);
	dma_unmap_single(tx_ring->dev,
			 dma_unmap_addr(tx_bi, dma),
			 dma_unmap_len(tx_bi, len), DMA_TO_DEVICE);
	dma_unmap_len_set(tx_bi, len, 0);
}

/**
 * i40e_clean_xdp_tx_irq - Completes AF_XDP entries, and cleans XDP entries
 * @tx_ring: XDP Tx ring
 * @tx_bi: Tx buffer info to clean
 *
 * Returns true if cleanup/tranmission is done.
 **/
bool i40e_clean_xdp_tx_irq(struct i40e_vsi *vsi,
			   struct i40e_ring *tx_ring, int napi_budget)
{
	unsigned int ntc, total_bytes = 0, budget = vsi->work_limit;
	u32 i, completed_frames, frames_ready, xsk_frames = 0;
	struct xdp_umem *umem = tx_ring->xsk_umem;
	u32 head_idx = i40e_get_head(tx_ring);
	bool work_done = true, xmit_done;
	struct i40e_tx_buffer *tx_bi;

	if (head_idx < tx_ring->next_to_clean)
		head_idx += tx_ring->count;
	frames_ready = head_idx - tx_ring->next_to_clean;

	if (frames_ready == 0) {
		goto out_xmit;
	} else if (frames_ready > budget) {
		completed_frames = budget;
		work_done = false;
	} else {
		completed_frames = frames_ready;
	}

	ntc = tx_ring->next_to_clean;

	for (i = 0; i < completed_frames; i++) {
		tx_bi = &tx_ring->tx_bi[ntc];

		if (tx_bi->xdpf)
			i40e_clean_xdp_tx_buffer(tx_ring, tx_bi);
		else
			xsk_frames++;

		tx_bi->xdpf = NULL;
		total_bytes += tx_bi->bytecount;

		if (++ntc >= tx_ring->count)
			ntc = 0;
	}

	tx_ring->next_to_clean += completed_frames;
	if (unlikely(tx_ring->next_to_clean >= tx_ring->count))
		tx_ring->next_to_clean -= tx_ring->count;

	if (xsk_frames)
		xsk_umem_complete_tx(umem, xsk_frames);

	i40e_arm_wb(tx_ring, vsi, budget);
	i40e_update_tx_stats(tx_ring, completed_frames, total_bytes);

out_xmit:
	xmit_done = i40e_xmit_zc(tx_ring, budget);

	return work_done && xmit_done;
}

/**
 * i40e_xsk_async_xmit - Implements the ndo_xsk_async_xmit
 * @dev: the netdevice
 * @queue_id: queue id to wake up
 *
 * Returns <0 for errors, 0 otherwise.
 **/
int i40e_xsk_async_xmit(struct net_device *dev, u32 queue_id)
{
	struct i40e_netdev_priv *np = netdev_priv(dev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_ring *ring;

	if (test_bit(__I40E_VSI_DOWN, vsi->state))
		return -ENETDOWN;

	if (!i40e_enabled_xdp_vsi(vsi))
		return -ENXIO;

	if (queue_id >= vsi->num_queue_pairs)
		return -ENXIO;

	if (!vsi->xdp_rings[queue_id]->xsk_umem)
		return -ENXIO;

	ring = vsi->xdp_rings[queue_id];

	/* The idea here is that if NAPI is running, mark a miss, so
	 * it will run again. If not, trigger an interrupt and
	 * schedule the NAPI from interrupt context. If NAPI would be
	 * scheduled here, the interrupt affinity would not be
	 * honored.
	 */
	if (!napi_if_scheduled_mark_missed(&ring->q_vector->napi))
		i40e_force_wb(vsi, ring->q_vector);

	return 0;
}

void i40e_xsk_clean_rx_ring(struct i40e_ring *rx_ring)
{
	u16 i;

	for (i = 0; i < rx_ring->count; i++) {
		struct i40e_rx_buffer *rx_bi = &rx_ring->rx_bi[i];

		if (!rx_bi->addr)
			continue;

		xsk_umem_fq_reuse(rx_ring->xsk_umem, rx_bi->handle);
		rx_bi->addr = NULL;
	}
}

/**
 * i40e_xsk_clean_xdp_ring - Clean the XDP Tx ring on shutdown
 * @xdp_ring: XDP Tx ring
 **/
void i40e_xsk_clean_tx_ring(struct i40e_ring *tx_ring)
{
	u16 ntc = tx_ring->next_to_clean, ntu = tx_ring->next_to_use;
	struct xdp_umem *umem = tx_ring->xsk_umem;
	struct i40e_tx_buffer *tx_bi;
	u32 xsk_frames = 0;

	while (ntc != ntu) {
		tx_bi = &tx_ring->tx_bi[ntc];

		if (tx_bi->xdpf)
			i40e_clean_xdp_tx_buffer(tx_ring, tx_bi);
		else
			xsk_frames++;

		tx_bi->xdpf = NULL;

		ntc++;
		if (ntc >= tx_ring->count)
			ntc = 0;
	}

	if (xsk_frames)
		xsk_umem_complete_tx(umem, xsk_frames);
}

/**
 * i40e_xsk_any_rx_ring_enabled - Checks if Rx rings have AF_XDP UMEM attached
 * @vsi: vsi
 *
 * Returns true if any of the Rx rings has an AF_XDP UMEM attached
 **/
bool i40e_xsk_any_rx_ring_enabled(struct i40e_vsi *vsi)
{
	struct net_device *netdev = vsi->netdev;
	int i;

	for (i = 0; i < vsi->num_queue_pairs; i++) {
		if (xdp_get_umem_from_qid(netdev, i))
			return true;
	}

	return false;
}
