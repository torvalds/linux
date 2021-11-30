// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

/* The driver transmit and receive code */

#include <linux/prefetch.h>
#include <linux/mm.h>
#include <linux/bpf_trace.h>
#include <net/dsfield.h>
#include <net/xdp.h>
#include "ice_txrx_lib.h"
#include "ice_lib.h"
#include "ice.h"
#include "ice_trace.h"
#include "ice_dcb_lib.h"
#include "ice_xsk.h"
#include "ice_eswitch.h"

#define ICE_RX_HDR_SIZE		256

#define FDIR_DESC_RXDID 0x40
#define ICE_FDIR_CLEAN_DELAY 10

/**
 * ice_prgm_fdir_fltr - Program a Flow Director filter
 * @vsi: VSI to send dummy packet
 * @fdir_desc: flow director descriptor
 * @raw_packet: allocated buffer for flow director
 */
int
ice_prgm_fdir_fltr(struct ice_vsi *vsi, struct ice_fltr_desc *fdir_desc,
		   u8 *raw_packet)
{
	struct ice_tx_buf *tx_buf, *first;
	struct ice_fltr_desc *f_desc;
	struct ice_tx_desc *tx_desc;
	struct ice_tx_ring *tx_ring;
	struct device *dev;
	dma_addr_t dma;
	u32 td_cmd;
	u16 i;

	/* VSI and Tx ring */
	if (!vsi)
		return -ENOENT;
	tx_ring = vsi->tx_rings[0];
	if (!tx_ring || !tx_ring->desc)
		return -ENOENT;
	dev = tx_ring->dev;

	/* we are using two descriptors to add/del a filter and we can wait */
	for (i = ICE_FDIR_CLEAN_DELAY; ICE_DESC_UNUSED(tx_ring) < 2; i--) {
		if (!i)
			return -EAGAIN;
		msleep_interruptible(1);
	}

	dma = dma_map_single(dev, raw_packet, ICE_FDIR_MAX_RAW_PKT_SIZE,
			     DMA_TO_DEVICE);

	if (dma_mapping_error(dev, dma))
		return -EINVAL;

	/* grab the next descriptor */
	i = tx_ring->next_to_use;
	first = &tx_ring->tx_buf[i];
	f_desc = ICE_TX_FDIRDESC(tx_ring, i);
	memcpy(f_desc, fdir_desc, sizeof(*f_desc));

	i++;
	i = (i < tx_ring->count) ? i : 0;
	tx_desc = ICE_TX_DESC(tx_ring, i);
	tx_buf = &tx_ring->tx_buf[i];

	i++;
	tx_ring->next_to_use = (i < tx_ring->count) ? i : 0;

	memset(tx_buf, 0, sizeof(*tx_buf));
	dma_unmap_len_set(tx_buf, len, ICE_FDIR_MAX_RAW_PKT_SIZE);
	dma_unmap_addr_set(tx_buf, dma, dma);

	tx_desc->buf_addr = cpu_to_le64(dma);
	td_cmd = ICE_TXD_LAST_DESC_CMD | ICE_TX_DESC_CMD_DUMMY |
		 ICE_TX_DESC_CMD_RE;

	tx_buf->tx_flags = ICE_TX_FLAGS_DUMMY_PKT;
	tx_buf->raw_buf = raw_packet;

	tx_desc->cmd_type_offset_bsz =
		ice_build_ctob(td_cmd, 0, ICE_FDIR_MAX_RAW_PKT_SIZE, 0);

	/* Force memory write to complete before letting h/w know
	 * there are new descriptors to fetch.
	 */
	wmb();

	/* mark the data descriptor to be watched */
	first->next_to_watch = tx_desc;

	writel(tx_ring->next_to_use, tx_ring->tail);

	return 0;
}

/**
 * ice_unmap_and_free_tx_buf - Release a Tx buffer
 * @ring: the ring that owns the buffer
 * @tx_buf: the buffer to free
 */
static void
ice_unmap_and_free_tx_buf(struct ice_tx_ring *ring, struct ice_tx_buf *tx_buf)
{
	if (tx_buf->skb) {
		if (tx_buf->tx_flags & ICE_TX_FLAGS_DUMMY_PKT)
			devm_kfree(ring->dev, tx_buf->raw_buf);
		else if (ice_ring_is_xdp(ring))
			page_frag_free(tx_buf->raw_buf);
		else
			dev_kfree_skb_any(tx_buf->skb);
		if (dma_unmap_len(tx_buf, len))
			dma_unmap_single(ring->dev,
					 dma_unmap_addr(tx_buf, dma),
					 dma_unmap_len(tx_buf, len),
					 DMA_TO_DEVICE);
	} else if (dma_unmap_len(tx_buf, len)) {
		dma_unmap_page(ring->dev,
			       dma_unmap_addr(tx_buf, dma),
			       dma_unmap_len(tx_buf, len),
			       DMA_TO_DEVICE);
	}

	tx_buf->next_to_watch = NULL;
	tx_buf->skb = NULL;
	dma_unmap_len_set(tx_buf, len, 0);
	/* tx_buf must be completely set up in the transmit path */
}

static struct netdev_queue *txring_txq(const struct ice_tx_ring *ring)
{
	return netdev_get_tx_queue(ring->netdev, ring->q_index);
}

/**
 * ice_clean_tx_ring - Free any empty Tx buffers
 * @tx_ring: ring to be cleaned
 */
void ice_clean_tx_ring(struct ice_tx_ring *tx_ring)
{
	u32 size;
	u16 i;

	if (ice_ring_is_xdp(tx_ring) && tx_ring->xsk_pool) {
		ice_xsk_clean_xdp_ring(tx_ring);
		goto tx_skip_free;
	}

	/* ring already cleared, nothing to do */
	if (!tx_ring->tx_buf)
		return;

	/* Free all the Tx ring sk_buffs */
	for (i = 0; i < tx_ring->count; i++)
		ice_unmap_and_free_tx_buf(tx_ring, &tx_ring->tx_buf[i]);

tx_skip_free:
	memset(tx_ring->tx_buf, 0, sizeof(*tx_ring->tx_buf) * tx_ring->count);

	size = ALIGN(tx_ring->count * sizeof(struct ice_tx_desc),
		     PAGE_SIZE);
	/* Zero out the descriptor ring */
	memset(tx_ring->desc, 0, size);

	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;

	if (!tx_ring->netdev)
		return;

	/* cleanup Tx queue statistics */
	netdev_tx_reset_queue(txring_txq(tx_ring));
}

/**
 * ice_free_tx_ring - Free Tx resources per queue
 * @tx_ring: Tx descriptor ring for a specific queue
 *
 * Free all transmit software resources
 */
void ice_free_tx_ring(struct ice_tx_ring *tx_ring)
{
	u32 size;

	ice_clean_tx_ring(tx_ring);
	devm_kfree(tx_ring->dev, tx_ring->tx_buf);
	tx_ring->tx_buf = NULL;

	if (tx_ring->desc) {
		size = ALIGN(tx_ring->count * sizeof(struct ice_tx_desc),
			     PAGE_SIZE);
		dmam_free_coherent(tx_ring->dev, size,
				   tx_ring->desc, tx_ring->dma);
		tx_ring->desc = NULL;
	}
}

/**
 * ice_clean_tx_irq - Reclaim resources after transmit completes
 * @tx_ring: Tx ring to clean
 * @napi_budget: Used to determine if we are in netpoll
 *
 * Returns true if there's any budget left (e.g. the clean is finished)
 */
static bool ice_clean_tx_irq(struct ice_tx_ring *tx_ring, int napi_budget)
{
	unsigned int total_bytes = 0, total_pkts = 0;
	unsigned int budget = ICE_DFLT_IRQ_WORK;
	struct ice_vsi *vsi = tx_ring->vsi;
	s16 i = tx_ring->next_to_clean;
	struct ice_tx_desc *tx_desc;
	struct ice_tx_buf *tx_buf;

	tx_buf = &tx_ring->tx_buf[i];
	tx_desc = ICE_TX_DESC(tx_ring, i);
	i -= tx_ring->count;

	prefetch(&vsi->state);

	do {
		struct ice_tx_desc *eop_desc = tx_buf->next_to_watch;

		/* if next_to_watch is not set then there is no work pending */
		if (!eop_desc)
			break;

		smp_rmb();	/* prevent any other reads prior to eop_desc */

		ice_trace(clean_tx_irq, tx_ring, tx_desc, tx_buf);
		/* if the descriptor isn't done, no work yet to do */
		if (!(eop_desc->cmd_type_offset_bsz &
		      cpu_to_le64(ICE_TX_DESC_DTYPE_DESC_DONE)))
			break;

		/* clear next_to_watch to prevent false hangs */
		tx_buf->next_to_watch = NULL;

		/* update the statistics for this packet */
		total_bytes += tx_buf->bytecount;
		total_pkts += tx_buf->gso_segs;

		/* free the skb */
		napi_consume_skb(tx_buf->skb, napi_budget);

		/* unmap skb header data */
		dma_unmap_single(tx_ring->dev,
				 dma_unmap_addr(tx_buf, dma),
				 dma_unmap_len(tx_buf, len),
				 DMA_TO_DEVICE);

		/* clear tx_buf data */
		tx_buf->skb = NULL;
		dma_unmap_len_set(tx_buf, len, 0);

		/* unmap remaining buffers */
		while (tx_desc != eop_desc) {
			ice_trace(clean_tx_irq_unmap, tx_ring, tx_desc, tx_buf);
			tx_buf++;
			tx_desc++;
			i++;
			if (unlikely(!i)) {
				i -= tx_ring->count;
				tx_buf = tx_ring->tx_buf;
				tx_desc = ICE_TX_DESC(tx_ring, 0);
			}

			/* unmap any remaining paged data */
			if (dma_unmap_len(tx_buf, len)) {
				dma_unmap_page(tx_ring->dev,
					       dma_unmap_addr(tx_buf, dma),
					       dma_unmap_len(tx_buf, len),
					       DMA_TO_DEVICE);
				dma_unmap_len_set(tx_buf, len, 0);
			}
		}
		ice_trace(clean_tx_irq_unmap_eop, tx_ring, tx_desc, tx_buf);

		/* move us one more past the eop_desc for start of next pkt */
		tx_buf++;
		tx_desc++;
		i++;
		if (unlikely(!i)) {
			i -= tx_ring->count;
			tx_buf = tx_ring->tx_buf;
			tx_desc = ICE_TX_DESC(tx_ring, 0);
		}

		prefetch(tx_desc);

		/* update budget accounting */
		budget--;
	} while (likely(budget));

	i += tx_ring->count;
	tx_ring->next_to_clean = i;

	ice_update_tx_ring_stats(tx_ring, total_pkts, total_bytes);

	netdev_tx_completed_queue(txring_txq(tx_ring), total_pkts,
				  total_bytes);

#define TX_WAKE_THRESHOLD ((s16)(DESC_NEEDED * 2))
	if (unlikely(total_pkts && netif_carrier_ok(tx_ring->netdev) &&
		     (ICE_DESC_UNUSED(tx_ring) >= TX_WAKE_THRESHOLD))) {
		/* Make sure that anybody stopping the queue after this
		 * sees the new next_to_clean.
		 */
		smp_mb();
		if (__netif_subqueue_stopped(tx_ring->netdev,
					     tx_ring->q_index) &&
		    !test_bit(ICE_VSI_DOWN, vsi->state)) {
			netif_wake_subqueue(tx_ring->netdev,
					    tx_ring->q_index);
			++tx_ring->tx_stats.restart_q;
		}
	}

	return !!budget;
}

/**
 * ice_setup_tx_ring - Allocate the Tx descriptors
 * @tx_ring: the Tx ring to set up
 *
 * Return 0 on success, negative on error
 */
int ice_setup_tx_ring(struct ice_tx_ring *tx_ring)
{
	struct device *dev = tx_ring->dev;
	u32 size;

	if (!dev)
		return -ENOMEM;

	/* warn if we are about to overwrite the pointer */
	WARN_ON(tx_ring->tx_buf);
	tx_ring->tx_buf =
		devm_kcalloc(dev, sizeof(*tx_ring->tx_buf), tx_ring->count,
			     GFP_KERNEL);
	if (!tx_ring->tx_buf)
		return -ENOMEM;

	/* round up to nearest page */
	size = ALIGN(tx_ring->count * sizeof(struct ice_tx_desc),
		     PAGE_SIZE);
	tx_ring->desc = dmam_alloc_coherent(dev, size, &tx_ring->dma,
					    GFP_KERNEL);
	if (!tx_ring->desc) {
		dev_err(dev, "Unable to allocate memory for the Tx descriptor ring, size=%d\n",
			size);
		goto err;
	}

	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;
	tx_ring->tx_stats.prev_pkt = -1;
	return 0;

err:
	devm_kfree(dev, tx_ring->tx_buf);
	tx_ring->tx_buf = NULL;
	return -ENOMEM;
}

/**
 * ice_clean_rx_ring - Free Rx buffers
 * @rx_ring: ring to be cleaned
 */
void ice_clean_rx_ring(struct ice_rx_ring *rx_ring)
{
	struct device *dev = rx_ring->dev;
	u32 size;
	u16 i;

	/* ring already cleared, nothing to do */
	if (!rx_ring->rx_buf)
		return;

	if (rx_ring->skb) {
		dev_kfree_skb(rx_ring->skb);
		rx_ring->skb = NULL;
	}

	if (rx_ring->xsk_pool) {
		ice_xsk_clean_rx_ring(rx_ring);
		goto rx_skip_free;
	}

	/* Free all the Rx ring sk_buffs */
	for (i = 0; i < rx_ring->count; i++) {
		struct ice_rx_buf *rx_buf = &rx_ring->rx_buf[i];

		if (!rx_buf->page)
			continue;

		/* Invalidate cache lines that may have been written to by
		 * device so that we avoid corrupting memory.
		 */
		dma_sync_single_range_for_cpu(dev, rx_buf->dma,
					      rx_buf->page_offset,
					      rx_ring->rx_buf_len,
					      DMA_FROM_DEVICE);

		/* free resources associated with mapping */
		dma_unmap_page_attrs(dev, rx_buf->dma, ice_rx_pg_size(rx_ring),
				     DMA_FROM_DEVICE, ICE_RX_DMA_ATTR);
		__page_frag_cache_drain(rx_buf->page, rx_buf->pagecnt_bias);

		rx_buf->page = NULL;
		rx_buf->page_offset = 0;
	}

rx_skip_free:
	memset(rx_ring->rx_buf, 0, sizeof(*rx_ring->rx_buf) * rx_ring->count);

	/* Zero out the descriptor ring */
	size = ALIGN(rx_ring->count * sizeof(union ice_32byte_rx_desc),
		     PAGE_SIZE);
	memset(rx_ring->desc, 0, size);

	rx_ring->next_to_alloc = 0;
	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;
}

/**
 * ice_free_rx_ring - Free Rx resources
 * @rx_ring: ring to clean the resources from
 *
 * Free all receive software resources
 */
void ice_free_rx_ring(struct ice_rx_ring *rx_ring)
{
	u32 size;

	ice_clean_rx_ring(rx_ring);
	if (rx_ring->vsi->type == ICE_VSI_PF)
		if (xdp_rxq_info_is_reg(&rx_ring->xdp_rxq))
			xdp_rxq_info_unreg(&rx_ring->xdp_rxq);
	rx_ring->xdp_prog = NULL;
	devm_kfree(rx_ring->dev, rx_ring->rx_buf);
	rx_ring->rx_buf = NULL;

	if (rx_ring->desc) {
		size = ALIGN(rx_ring->count * sizeof(union ice_32byte_rx_desc),
			     PAGE_SIZE);
		dmam_free_coherent(rx_ring->dev, size,
				   rx_ring->desc, rx_ring->dma);
		rx_ring->desc = NULL;
	}
}

/**
 * ice_setup_rx_ring - Allocate the Rx descriptors
 * @rx_ring: the Rx ring to set up
 *
 * Return 0 on success, negative on error
 */
int ice_setup_rx_ring(struct ice_rx_ring *rx_ring)
{
	struct device *dev = rx_ring->dev;
	u32 size;

	if (!dev)
		return -ENOMEM;

	/* warn if we are about to overwrite the pointer */
	WARN_ON(rx_ring->rx_buf);
	rx_ring->rx_buf =
		devm_kcalloc(dev, sizeof(*rx_ring->rx_buf), rx_ring->count,
			     GFP_KERNEL);
	if (!rx_ring->rx_buf)
		return -ENOMEM;

	/* round up to nearest page */
	size = ALIGN(rx_ring->count * sizeof(union ice_32byte_rx_desc),
		     PAGE_SIZE);
	rx_ring->desc = dmam_alloc_coherent(dev, size, &rx_ring->dma,
					    GFP_KERNEL);
	if (!rx_ring->desc) {
		dev_err(dev, "Unable to allocate memory for the Rx descriptor ring, size=%d\n",
			size);
		goto err;
	}

	rx_ring->next_to_use = 0;
	rx_ring->next_to_clean = 0;

	if (ice_is_xdp_ena_vsi(rx_ring->vsi))
		WRITE_ONCE(rx_ring->xdp_prog, rx_ring->vsi->xdp_prog);

	if (rx_ring->vsi->type == ICE_VSI_PF &&
	    !xdp_rxq_info_is_reg(&rx_ring->xdp_rxq))
		if (xdp_rxq_info_reg(&rx_ring->xdp_rxq, rx_ring->netdev,
				     rx_ring->q_index, rx_ring->q_vector->napi.napi_id))
			goto err;
	return 0;

err:
	devm_kfree(dev, rx_ring->rx_buf);
	rx_ring->rx_buf = NULL;
	return -ENOMEM;
}

static unsigned int
ice_rx_frame_truesize(struct ice_rx_ring *rx_ring, unsigned int __maybe_unused size)
{
	unsigned int truesize;

#if (PAGE_SIZE < 8192)
	truesize = ice_rx_pg_size(rx_ring) / 2; /* Must be power-of-2 */
#else
	truesize = rx_ring->rx_offset ?
		SKB_DATA_ALIGN(rx_ring->rx_offset + size) +
		SKB_DATA_ALIGN(sizeof(struct skb_shared_info)) :
		SKB_DATA_ALIGN(size);
#endif
	return truesize;
}

/**
 * ice_run_xdp - Executes an XDP program on initialized xdp_buff
 * @rx_ring: Rx ring
 * @xdp: xdp_buff used as input to the XDP program
 * @xdp_prog: XDP program to run
 * @xdp_ring: ring to be used for XDP_TX action
 *
 * Returns any of ICE_XDP_{PASS, CONSUMED, TX, REDIR}
 */
static int
ice_run_xdp(struct ice_rx_ring *rx_ring, struct xdp_buff *xdp,
	    struct bpf_prog *xdp_prog, struct ice_tx_ring *xdp_ring)
{
	int err;
	u32 act;

	act = bpf_prog_run_xdp(xdp_prog, xdp);
	switch (act) {
	case XDP_PASS:
		return ICE_XDP_PASS;
	case XDP_TX:
		if (static_branch_unlikely(&ice_xdp_locking_key))
			spin_lock(&xdp_ring->tx_lock);
		err = ice_xmit_xdp_ring(xdp->data, xdp->data_end - xdp->data, xdp_ring);
		if (static_branch_unlikely(&ice_xdp_locking_key))
			spin_unlock(&xdp_ring->tx_lock);
		if (err == ICE_XDP_CONSUMED)
			goto out_failure;
		return err;
	case XDP_REDIRECT:
		err = xdp_do_redirect(rx_ring->netdev, xdp, xdp_prog);
		if (err)
			goto out_failure;
		return ICE_XDP_REDIR;
	default:
		bpf_warn_invalid_xdp_action(rx_ring->netdev, xdp_prog, act);
		fallthrough;
	case XDP_ABORTED:
out_failure:
		trace_xdp_exception(rx_ring->netdev, xdp_prog, act);
		fallthrough;
	case XDP_DROP:
		return ICE_XDP_CONSUMED;
	}
}

/**
 * ice_xdp_xmit - submit packets to XDP ring for transmission
 * @dev: netdev
 * @n: number of XDP frames to be transmitted
 * @frames: XDP frames to be transmitted
 * @flags: transmit flags
 *
 * Returns number of frames successfully sent. Failed frames
 * will be free'ed by XDP core.
 * For error cases, a negative errno code is returned and no-frames
 * are transmitted (caller must handle freeing frames).
 */
int
ice_xdp_xmit(struct net_device *dev, int n, struct xdp_frame **frames,
	     u32 flags)
{
	struct ice_netdev_priv *np = netdev_priv(dev);
	unsigned int queue_index = smp_processor_id();
	struct ice_vsi *vsi = np->vsi;
	struct ice_tx_ring *xdp_ring;
	int nxmit = 0, i;

	if (test_bit(ICE_VSI_DOWN, vsi->state))
		return -ENETDOWN;

	if (!ice_is_xdp_ena_vsi(vsi) || queue_index >= vsi->num_xdp_txq)
		return -ENXIO;

	if (unlikely(flags & ~XDP_XMIT_FLAGS_MASK))
		return -EINVAL;

	if (static_branch_unlikely(&ice_xdp_locking_key)) {
		queue_index %= vsi->num_xdp_txq;
		xdp_ring = vsi->xdp_rings[queue_index];
		spin_lock(&xdp_ring->tx_lock);
	} else {
		xdp_ring = vsi->xdp_rings[queue_index];
	}

	for (i = 0; i < n; i++) {
		struct xdp_frame *xdpf = frames[i];
		int err;

		err = ice_xmit_xdp_ring(xdpf->data, xdpf->len, xdp_ring);
		if (err != ICE_XDP_TX)
			break;
		nxmit++;
	}

	if (unlikely(flags & XDP_XMIT_FLUSH))
		ice_xdp_ring_update_tail(xdp_ring);

	if (static_branch_unlikely(&ice_xdp_locking_key))
		spin_unlock(&xdp_ring->tx_lock);

	return nxmit;
}

/**
 * ice_alloc_mapped_page - recycle or make a new page
 * @rx_ring: ring to use
 * @bi: rx_buf struct to modify
 *
 * Returns true if the page was successfully allocated or
 * reused.
 */
static bool
ice_alloc_mapped_page(struct ice_rx_ring *rx_ring, struct ice_rx_buf *bi)
{
	struct page *page = bi->page;
	dma_addr_t dma;

	/* since we are recycling buffers we should seldom need to alloc */
	if (likely(page))
		return true;

	/* alloc new page for storage */
	page = dev_alloc_pages(ice_rx_pg_order(rx_ring));
	if (unlikely(!page)) {
		rx_ring->rx_stats.alloc_page_failed++;
		return false;
	}

	/* map page for use */
	dma = dma_map_page_attrs(rx_ring->dev, page, 0, ice_rx_pg_size(rx_ring),
				 DMA_FROM_DEVICE, ICE_RX_DMA_ATTR);

	/* if mapping failed free memory back to system since
	 * there isn't much point in holding memory we can't use
	 */
	if (dma_mapping_error(rx_ring->dev, dma)) {
		__free_pages(page, ice_rx_pg_order(rx_ring));
		rx_ring->rx_stats.alloc_page_failed++;
		return false;
	}

	bi->dma = dma;
	bi->page = page;
	bi->page_offset = rx_ring->rx_offset;
	page_ref_add(page, USHRT_MAX - 1);
	bi->pagecnt_bias = USHRT_MAX;

	return true;
}

/**
 * ice_alloc_rx_bufs - Replace used receive buffers
 * @rx_ring: ring to place buffers on
 * @cleaned_count: number of buffers to replace
 *
 * Returns false if all allocations were successful, true if any fail. Returning
 * true signals to the caller that we didn't replace cleaned_count buffers and
 * there is more work to do.
 *
 * First, try to clean "cleaned_count" Rx buffers. Then refill the cleaned Rx
 * buffers. Then bump tail at most one time. Grouping like this lets us avoid
 * multiple tail writes per call.
 */
bool ice_alloc_rx_bufs(struct ice_rx_ring *rx_ring, u16 cleaned_count)
{
	union ice_32b_rx_flex_desc *rx_desc;
	u16 ntu = rx_ring->next_to_use;
	struct ice_rx_buf *bi;

	/* do nothing if no valid netdev defined */
	if ((!rx_ring->netdev && rx_ring->vsi->type != ICE_VSI_CTRL) ||
	    !cleaned_count)
		return false;

	/* get the Rx descriptor and buffer based on next_to_use */
	rx_desc = ICE_RX_DESC(rx_ring, ntu);
	bi = &rx_ring->rx_buf[ntu];

	do {
		/* if we fail here, we have work remaining */
		if (!ice_alloc_mapped_page(rx_ring, bi))
			break;

		/* sync the buffer for use by the device */
		dma_sync_single_range_for_device(rx_ring->dev, bi->dma,
						 bi->page_offset,
						 rx_ring->rx_buf_len,
						 DMA_FROM_DEVICE);

		/* Refresh the desc even if buffer_addrs didn't change
		 * because each write-back erases this info.
		 */
		rx_desc->read.pkt_addr = cpu_to_le64(bi->dma + bi->page_offset);

		rx_desc++;
		bi++;
		ntu++;
		if (unlikely(ntu == rx_ring->count)) {
			rx_desc = ICE_RX_DESC(rx_ring, 0);
			bi = rx_ring->rx_buf;
			ntu = 0;
		}

		/* clear the status bits for the next_to_use descriptor */
		rx_desc->wb.status_error0 = 0;

		cleaned_count--;
	} while (cleaned_count);

	if (rx_ring->next_to_use != ntu)
		ice_release_rx_desc(rx_ring, ntu);

	return !!cleaned_count;
}

/**
 * ice_rx_buf_adjust_pg_offset - Prepare Rx buffer for reuse
 * @rx_buf: Rx buffer to adjust
 * @size: Size of adjustment
 *
 * Update the offset within page so that Rx buf will be ready to be reused.
 * For systems with PAGE_SIZE < 8192 this function will flip the page offset
 * so the second half of page assigned to Rx buffer will be used, otherwise
 * the offset is moved by "size" bytes
 */
static void
ice_rx_buf_adjust_pg_offset(struct ice_rx_buf *rx_buf, unsigned int size)
{
#if (PAGE_SIZE < 8192)
	/* flip page offset to other buffer */
	rx_buf->page_offset ^= size;
#else
	/* move offset up to the next cache line */
	rx_buf->page_offset += size;
#endif
}

/**
 * ice_can_reuse_rx_page - Determine if page can be reused for another Rx
 * @rx_buf: buffer containing the page
 * @rx_buf_pgcnt: rx_buf page refcount pre xdp_do_redirect() call
 *
 * If page is reusable, we have a green light for calling ice_reuse_rx_page,
 * which will assign the current buffer to the buffer that next_to_alloc is
 * pointing to; otherwise, the DMA mapping needs to be destroyed and
 * page freed
 */
static bool
ice_can_reuse_rx_page(struct ice_rx_buf *rx_buf, int rx_buf_pgcnt)
{
	unsigned int pagecnt_bias = rx_buf->pagecnt_bias;
	struct page *page = rx_buf->page;

	/* avoid re-using remote and pfmemalloc pages */
	if (!dev_page_is_reusable(page))
		return false;

#if (PAGE_SIZE < 8192)
	/* if we are only owner of page we can reuse it */
	if (unlikely((rx_buf_pgcnt - pagecnt_bias) > 1))
		return false;
#else
#define ICE_LAST_OFFSET \
	(SKB_WITH_OVERHEAD(PAGE_SIZE) - ICE_RXBUF_2048)
	if (rx_buf->page_offset > ICE_LAST_OFFSET)
		return false;
#endif /* PAGE_SIZE < 8192) */

	/* If we have drained the page fragment pool we need to update
	 * the pagecnt_bias and page count so that we fully restock the
	 * number of references the driver holds.
	 */
	if (unlikely(pagecnt_bias == 1)) {
		page_ref_add(page, USHRT_MAX - 1);
		rx_buf->pagecnt_bias = USHRT_MAX;
	}

	return true;
}

/**
 * ice_add_rx_frag - Add contents of Rx buffer to sk_buff as a frag
 * @rx_ring: Rx descriptor ring to transact packets on
 * @rx_buf: buffer containing page to add
 * @skb: sk_buff to place the data into
 * @size: packet length from rx_desc
 *
 * This function will add the data contained in rx_buf->page to the skb.
 * It will just attach the page as a frag to the skb.
 * The function will then update the page offset.
 */
static void
ice_add_rx_frag(struct ice_rx_ring *rx_ring, struct ice_rx_buf *rx_buf,
		struct sk_buff *skb, unsigned int size)
{
#if (PAGE_SIZE >= 8192)
	unsigned int truesize = SKB_DATA_ALIGN(size + rx_ring->rx_offset);
#else
	unsigned int truesize = ice_rx_pg_size(rx_ring) / 2;
#endif

	if (!size)
		return;
	skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags, rx_buf->page,
			rx_buf->page_offset, size, truesize);

	/* page is being used so we must update the page offset */
	ice_rx_buf_adjust_pg_offset(rx_buf, truesize);
}

/**
 * ice_reuse_rx_page - page flip buffer and store it back on the ring
 * @rx_ring: Rx descriptor ring to store buffers on
 * @old_buf: donor buffer to have page reused
 *
 * Synchronizes page for reuse by the adapter
 */
static void
ice_reuse_rx_page(struct ice_rx_ring *rx_ring, struct ice_rx_buf *old_buf)
{
	u16 nta = rx_ring->next_to_alloc;
	struct ice_rx_buf *new_buf;

	new_buf = &rx_ring->rx_buf[nta];

	/* update, and store next to alloc */
	nta++;
	rx_ring->next_to_alloc = (nta < rx_ring->count) ? nta : 0;

	/* Transfer page from old buffer to new buffer.
	 * Move each member individually to avoid possible store
	 * forwarding stalls and unnecessary copy of skb.
	 */
	new_buf->dma = old_buf->dma;
	new_buf->page = old_buf->page;
	new_buf->page_offset = old_buf->page_offset;
	new_buf->pagecnt_bias = old_buf->pagecnt_bias;
}

/**
 * ice_get_rx_buf - Fetch Rx buffer and synchronize data for use
 * @rx_ring: Rx descriptor ring to transact packets on
 * @size: size of buffer to add to skb
 * @rx_buf_pgcnt: rx_buf page refcount
 *
 * This function will pull an Rx buffer from the ring and synchronize it
 * for use by the CPU.
 */
static struct ice_rx_buf *
ice_get_rx_buf(struct ice_rx_ring *rx_ring, const unsigned int size,
	       int *rx_buf_pgcnt)
{
	struct ice_rx_buf *rx_buf;

	rx_buf = &rx_ring->rx_buf[rx_ring->next_to_clean];
	*rx_buf_pgcnt =
#if (PAGE_SIZE < 8192)
		page_count(rx_buf->page);
#else
		0;
#endif
	prefetchw(rx_buf->page);

	if (!size)
		return rx_buf;
	/* we are reusing so sync this buffer for CPU use */
	dma_sync_single_range_for_cpu(rx_ring->dev, rx_buf->dma,
				      rx_buf->page_offset, size,
				      DMA_FROM_DEVICE);

	/* We have pulled a buffer for use, so decrement pagecnt_bias */
	rx_buf->pagecnt_bias--;

	return rx_buf;
}

/**
 * ice_build_skb - Build skb around an existing buffer
 * @rx_ring: Rx descriptor ring to transact packets on
 * @rx_buf: Rx buffer to pull data from
 * @xdp: xdp_buff pointing to the data
 *
 * This function builds an skb around an existing Rx buffer, taking care
 * to set up the skb correctly and avoid any memcpy overhead.
 */
static struct sk_buff *
ice_build_skb(struct ice_rx_ring *rx_ring, struct ice_rx_buf *rx_buf,
	      struct xdp_buff *xdp)
{
	u8 metasize = xdp->data - xdp->data_meta;
#if (PAGE_SIZE < 8192)
	unsigned int truesize = ice_rx_pg_size(rx_ring) / 2;
#else
	unsigned int truesize = SKB_DATA_ALIGN(sizeof(struct skb_shared_info)) +
				SKB_DATA_ALIGN(xdp->data_end -
					       xdp->data_hard_start);
#endif
	struct sk_buff *skb;

	/* Prefetch first cache line of first page. If xdp->data_meta
	 * is unused, this points exactly as xdp->data, otherwise we
	 * likely have a consumer accessing first few bytes of meta
	 * data, and then actual data.
	 */
	net_prefetch(xdp->data_meta);
	/* build an skb around the page buffer */
	skb = build_skb(xdp->data_hard_start, truesize);
	if (unlikely(!skb))
		return NULL;

	/* must to record Rx queue, otherwise OS features such as
	 * symmetric queue won't work
	 */
	skb_record_rx_queue(skb, rx_ring->q_index);

	/* update pointers within the skb to store the data */
	skb_reserve(skb, xdp->data - xdp->data_hard_start);
	__skb_put(skb, xdp->data_end - xdp->data);
	if (metasize)
		skb_metadata_set(skb, metasize);

	/* buffer is used by skb, update page_offset */
	ice_rx_buf_adjust_pg_offset(rx_buf, truesize);

	return skb;
}

/**
 * ice_construct_skb - Allocate skb and populate it
 * @rx_ring: Rx descriptor ring to transact packets on
 * @rx_buf: Rx buffer to pull data from
 * @xdp: xdp_buff pointing to the data
 *
 * This function allocates an skb. It then populates it with the page
 * data from the current receive descriptor, taking care to set up the
 * skb correctly.
 */
static struct sk_buff *
ice_construct_skb(struct ice_rx_ring *rx_ring, struct ice_rx_buf *rx_buf,
		  struct xdp_buff *xdp)
{
	unsigned int size = xdp->data_end - xdp->data;
	unsigned int headlen;
	struct sk_buff *skb;

	/* prefetch first cache line of first page */
	net_prefetch(xdp->data);

	/* allocate a skb to store the frags */
	skb = __napi_alloc_skb(&rx_ring->q_vector->napi, ICE_RX_HDR_SIZE,
			       GFP_ATOMIC | __GFP_NOWARN);
	if (unlikely(!skb))
		return NULL;

	skb_record_rx_queue(skb, rx_ring->q_index);
	/* Determine available headroom for copy */
	headlen = size;
	if (headlen > ICE_RX_HDR_SIZE)
		headlen = eth_get_headlen(skb->dev, xdp->data, ICE_RX_HDR_SIZE);

	/* align pull length to size of long to optimize memcpy performance */
	memcpy(__skb_put(skb, headlen), xdp->data, ALIGN(headlen,
							 sizeof(long)));

	/* if we exhaust the linear part then add what is left as a frag */
	size -= headlen;
	if (size) {
#if (PAGE_SIZE >= 8192)
		unsigned int truesize = SKB_DATA_ALIGN(size);
#else
		unsigned int truesize = ice_rx_pg_size(rx_ring) / 2;
#endif
		skb_add_rx_frag(skb, 0, rx_buf->page,
				rx_buf->page_offset + headlen, size, truesize);
		/* buffer is used by skb, update page_offset */
		ice_rx_buf_adjust_pg_offset(rx_buf, truesize);
	} else {
		/* buffer is unused, reset bias back to rx_buf; data was copied
		 * onto skb's linear part so there's no need for adjusting
		 * page offset and we can reuse this buffer as-is
		 */
		rx_buf->pagecnt_bias++;
	}

	return skb;
}

/**
 * ice_put_rx_buf - Clean up used buffer and either recycle or free
 * @rx_ring: Rx descriptor ring to transact packets on
 * @rx_buf: Rx buffer to pull data from
 * @rx_buf_pgcnt: Rx buffer page count pre xdp_do_redirect()
 *
 * This function will update next_to_clean and then clean up the contents
 * of the rx_buf. It will either recycle the buffer or unmap it and free
 * the associated resources.
 */
static void
ice_put_rx_buf(struct ice_rx_ring *rx_ring, struct ice_rx_buf *rx_buf,
	       int rx_buf_pgcnt)
{
	u16 ntc = rx_ring->next_to_clean + 1;

	/* fetch, update, and store next to clean */
	ntc = (ntc < rx_ring->count) ? ntc : 0;
	rx_ring->next_to_clean = ntc;

	if (!rx_buf)
		return;

	if (ice_can_reuse_rx_page(rx_buf, rx_buf_pgcnt)) {
		/* hand second half of page back to the ring */
		ice_reuse_rx_page(rx_ring, rx_buf);
	} else {
		/* we are not reusing the buffer so unmap it */
		dma_unmap_page_attrs(rx_ring->dev, rx_buf->dma,
				     ice_rx_pg_size(rx_ring), DMA_FROM_DEVICE,
				     ICE_RX_DMA_ATTR);
		__page_frag_cache_drain(rx_buf->page, rx_buf->pagecnt_bias);
	}

	/* clear contents of buffer_info */
	rx_buf->page = NULL;
}

/**
 * ice_is_non_eop - process handling of non-EOP buffers
 * @rx_ring: Rx ring being processed
 * @rx_desc: Rx descriptor for current buffer
 *
 * If the buffer is an EOP buffer, this function exits returning false,
 * otherwise return true indicating that this is in fact a non-EOP buffer.
 */
static bool
ice_is_non_eop(struct ice_rx_ring *rx_ring, union ice_32b_rx_flex_desc *rx_desc)
{
	/* if we are the last buffer then there is nothing else to do */
#define ICE_RXD_EOF BIT(ICE_RX_FLEX_DESC_STATUS0_EOF_S)
	if (likely(ice_test_staterr(rx_desc, ICE_RXD_EOF)))
		return false;

	rx_ring->rx_stats.non_eop_descs++;

	return true;
}

/**
 * ice_clean_rx_irq - Clean completed descriptors from Rx ring - bounce buf
 * @rx_ring: Rx descriptor ring to transact packets on
 * @budget: Total limit on number of packets to process
 *
 * This function provides a "bounce buffer" approach to Rx interrupt
 * processing. The advantage to this is that on systems that have
 * expensive overhead for IOMMU access this provides a means of avoiding
 * it by maintaining the mapping of the page to the system.
 *
 * Returns amount of work completed
 */
int ice_clean_rx_irq(struct ice_rx_ring *rx_ring, int budget)
{
	unsigned int total_rx_bytes = 0, total_rx_pkts = 0, frame_sz = 0;
	u16 cleaned_count = ICE_DESC_UNUSED(rx_ring);
	unsigned int offset = rx_ring->rx_offset;
	struct ice_tx_ring *xdp_ring = NULL;
	unsigned int xdp_res, xdp_xmit = 0;
	struct sk_buff *skb = rx_ring->skb;
	struct bpf_prog *xdp_prog = NULL;
	struct xdp_buff xdp;
	bool failure;

	/* Frame size depend on rx_ring setup when PAGE_SIZE=4K */
#if (PAGE_SIZE < 8192)
	frame_sz = ice_rx_frame_truesize(rx_ring, 0);
#endif
	xdp_init_buff(&xdp, frame_sz, &rx_ring->xdp_rxq);

	xdp_prog = READ_ONCE(rx_ring->xdp_prog);
	if (xdp_prog)
		xdp_ring = rx_ring->xdp_ring;

	/* start the loop to process Rx packets bounded by 'budget' */
	while (likely(total_rx_pkts < (unsigned int)budget)) {
		union ice_32b_rx_flex_desc *rx_desc;
		struct ice_rx_buf *rx_buf;
		unsigned char *hard_start;
		unsigned int size;
		u16 stat_err_bits;
		int rx_buf_pgcnt;
		u16 vlan_tag = 0;
		u16 rx_ptype;

		/* get the Rx desc from Rx ring based on 'next_to_clean' */
		rx_desc = ICE_RX_DESC(rx_ring, rx_ring->next_to_clean);

		/* status_error_len will always be zero for unused descriptors
		 * because it's cleared in cleanup, and overlaps with hdr_addr
		 * which is always zero because packet split isn't used, if the
		 * hardware wrote DD then it will be non-zero
		 */
		stat_err_bits = BIT(ICE_RX_FLEX_DESC_STATUS0_DD_S);
		if (!ice_test_staterr(rx_desc, stat_err_bits))
			break;

		/* This memory barrier is needed to keep us from reading
		 * any other fields out of the rx_desc until we know the
		 * DD bit is set.
		 */
		dma_rmb();

		ice_trace(clean_rx_irq, rx_ring, rx_desc);
		if (rx_desc->wb.rxdid == FDIR_DESC_RXDID || !rx_ring->netdev) {
			struct ice_vsi *ctrl_vsi = rx_ring->vsi;

			if (rx_desc->wb.rxdid == FDIR_DESC_RXDID &&
			    ctrl_vsi->vf_id != ICE_INVAL_VFID)
				ice_vc_fdir_irq_handler(ctrl_vsi, rx_desc);
			ice_put_rx_buf(rx_ring, NULL, 0);
			cleaned_count++;
			continue;
		}

		size = le16_to_cpu(rx_desc->wb.pkt_len) &
			ICE_RX_FLX_DESC_PKT_LEN_M;

		/* retrieve a buffer from the ring */
		rx_buf = ice_get_rx_buf(rx_ring, size, &rx_buf_pgcnt);

		if (!size) {
			xdp.data = NULL;
			xdp.data_end = NULL;
			xdp.data_hard_start = NULL;
			xdp.data_meta = NULL;
			goto construct_skb;
		}

		hard_start = page_address(rx_buf->page) + rx_buf->page_offset -
			     offset;
		xdp_prepare_buff(&xdp, hard_start, offset, size, true);
#if (PAGE_SIZE > 4096)
		/* At larger PAGE_SIZE, frame_sz depend on len size */
		xdp.frame_sz = ice_rx_frame_truesize(rx_ring, size);
#endif

		if (!xdp_prog)
			goto construct_skb;

		xdp_res = ice_run_xdp(rx_ring, &xdp, xdp_prog, xdp_ring);
		if (!xdp_res)
			goto construct_skb;
		if (xdp_res & (ICE_XDP_TX | ICE_XDP_REDIR)) {
			xdp_xmit |= xdp_res;
			ice_rx_buf_adjust_pg_offset(rx_buf, xdp.frame_sz);
		} else {
			rx_buf->pagecnt_bias++;
		}
		total_rx_bytes += size;
		total_rx_pkts++;

		cleaned_count++;
		ice_put_rx_buf(rx_ring, rx_buf, rx_buf_pgcnt);
		continue;
construct_skb:
		if (skb) {
			ice_add_rx_frag(rx_ring, rx_buf, skb, size);
		} else if (likely(xdp.data)) {
			if (ice_ring_uses_build_skb(rx_ring))
				skb = ice_build_skb(rx_ring, rx_buf, &xdp);
			else
				skb = ice_construct_skb(rx_ring, rx_buf, &xdp);
		}
		/* exit if we failed to retrieve a buffer */
		if (!skb) {
			rx_ring->rx_stats.alloc_buf_failed++;
			if (rx_buf)
				rx_buf->pagecnt_bias++;
			break;
		}

		ice_put_rx_buf(rx_ring, rx_buf, rx_buf_pgcnt);
		cleaned_count++;

		/* skip if it is NOP desc */
		if (ice_is_non_eop(rx_ring, rx_desc))
			continue;

		stat_err_bits = BIT(ICE_RX_FLEX_DESC_STATUS0_RXE_S);
		if (unlikely(ice_test_staterr(rx_desc, stat_err_bits))) {
			dev_kfree_skb_any(skb);
			continue;
		}

		stat_err_bits = BIT(ICE_RX_FLEX_DESC_STATUS0_L2TAG1P_S);
		if (ice_test_staterr(rx_desc, stat_err_bits))
			vlan_tag = le16_to_cpu(rx_desc->wb.l2tag1);

		/* pad the skb if needed, to make a valid ethernet frame */
		if (eth_skb_pad(skb)) {
			skb = NULL;
			continue;
		}

		/* probably a little skewed due to removing CRC */
		total_rx_bytes += skb->len;

		/* populate checksum, VLAN, and protocol */
		rx_ptype = le16_to_cpu(rx_desc->wb.ptype_flex_flags0) &
			ICE_RX_FLEX_DESC_PTYPE_M;

		ice_process_skb_fields(rx_ring, rx_desc, skb, rx_ptype);

		ice_trace(clean_rx_irq_indicate, rx_ring, rx_desc, skb);
		/* send completed skb up the stack */
		ice_receive_skb(rx_ring, skb, vlan_tag);
		skb = NULL;

		/* update budget accounting */
		total_rx_pkts++;
	}

	/* return up to cleaned_count buffers to hardware */
	failure = ice_alloc_rx_bufs(rx_ring, cleaned_count);

	if (xdp_prog)
		ice_finalize_xdp_rx(xdp_ring, xdp_xmit);
	rx_ring->skb = skb;

	ice_update_rx_ring_stats(rx_ring, total_rx_pkts, total_rx_bytes);

	/* guarantee a trip back through this routine if there was a failure */
	return failure ? budget : (int)total_rx_pkts;
}

static void __ice_update_sample(struct ice_q_vector *q_vector,
				struct ice_ring_container *rc,
				struct dim_sample *sample,
				bool is_tx)
{
	u64 packets = 0, bytes = 0;

	if (is_tx) {
		struct ice_tx_ring *tx_ring;

		ice_for_each_tx_ring(tx_ring, *rc) {
			packets += tx_ring->stats.pkts;
			bytes += tx_ring->stats.bytes;
		}
	} else {
		struct ice_rx_ring *rx_ring;

		ice_for_each_rx_ring(rx_ring, *rc) {
			packets += rx_ring->stats.pkts;
			bytes += rx_ring->stats.bytes;
		}
	}

	dim_update_sample(q_vector->total_events, packets, bytes, sample);
	sample->comp_ctr = 0;

	/* if dim settings get stale, like when not updated for 1
	 * second or longer, force it to start again. This addresses the
	 * frequent case of an idle queue being switched to by the
	 * scheduler. The 1,000 here means 1,000 milliseconds.
	 */
	if (ktime_ms_delta(sample->time, rc->dim.start_sample.time) >= 1000)
		rc->dim.state = DIM_START_MEASURE;
}

/**
 * ice_net_dim - Update net DIM algorithm
 * @q_vector: the vector associated with the interrupt
 *
 * Create a DIM sample and notify net_dim() so that it can possibly decide
 * a new ITR value based on incoming packets, bytes, and interrupts.
 *
 * This function is a no-op if the ring is not configured to dynamic ITR.
 */
static void ice_net_dim(struct ice_q_vector *q_vector)
{
	struct ice_ring_container *tx = &q_vector->tx;
	struct ice_ring_container *rx = &q_vector->rx;

	if (ITR_IS_DYNAMIC(tx)) {
		struct dim_sample dim_sample;

		__ice_update_sample(q_vector, tx, &dim_sample, true);
		net_dim(&tx->dim, dim_sample);
	}

	if (ITR_IS_DYNAMIC(rx)) {
		struct dim_sample dim_sample;

		__ice_update_sample(q_vector, rx, &dim_sample, false);
		net_dim(&rx->dim, dim_sample);
	}
}

/**
 * ice_buildreg_itr - build value for writing to the GLINT_DYN_CTL register
 * @itr_idx: interrupt throttling index
 * @itr: interrupt throttling value in usecs
 */
static u32 ice_buildreg_itr(u16 itr_idx, u16 itr)
{
	/* The ITR value is reported in microseconds, and the register value is
	 * recorded in 2 microsecond units. For this reason we only need to
	 * shift by the GLINT_DYN_CTL_INTERVAL_S - ICE_ITR_GRAN_S to apply this
	 * granularity as a shift instead of division. The mask makes sure the
	 * ITR value is never odd so we don't accidentally write into the field
	 * prior to the ITR field.
	 */
	itr &= ICE_ITR_MASK;

	return GLINT_DYN_CTL_INTENA_M | GLINT_DYN_CTL_CLEARPBA_M |
		(itr_idx << GLINT_DYN_CTL_ITR_INDX_S) |
		(itr << (GLINT_DYN_CTL_INTERVAL_S - ICE_ITR_GRAN_S));
}

/**
 * ice_enable_interrupt - re-enable MSI-X interrupt
 * @q_vector: the vector associated with the interrupt to enable
 *
 * If the VSI is down, the interrupt will not be re-enabled. Also,
 * when enabling the interrupt always reset the wb_on_itr to false
 * and trigger a software interrupt to clean out internal state.
 */
static void ice_enable_interrupt(struct ice_q_vector *q_vector)
{
	struct ice_vsi *vsi = q_vector->vsi;
	bool wb_en = q_vector->wb_on_itr;
	u32 itr_val;

	if (test_bit(ICE_DOWN, vsi->state))
		return;

	/* trigger an ITR delayed software interrupt when exiting busy poll, to
	 * make sure to catch any pending cleanups that might have been missed
	 * due to interrupt state transition. If busy poll or poll isn't
	 * enabled, then don't update ITR, and just enable the interrupt.
	 */
	if (!wb_en) {
		itr_val = ice_buildreg_itr(ICE_ITR_NONE, 0);
	} else {
		q_vector->wb_on_itr = false;

		/* do two things here with a single write. Set up the third ITR
		 * index to be used for software interrupt moderation, and then
		 * trigger a software interrupt with a rate limit of 20K on
		 * software interrupts, this will help avoid high interrupt
		 * loads due to frequently polling and exiting polling.
		 */
		itr_val = ice_buildreg_itr(ICE_IDX_ITR2, ICE_ITR_20K);
		itr_val |= GLINT_DYN_CTL_SWINT_TRIG_M |
			   ICE_IDX_ITR2 << GLINT_DYN_CTL_SW_ITR_INDX_S |
			   GLINT_DYN_CTL_SW_ITR_INDX_ENA_M;
	}
	wr32(&vsi->back->hw, GLINT_DYN_CTL(q_vector->reg_idx), itr_val);
}

/**
 * ice_set_wb_on_itr - set WB_ON_ITR for this q_vector
 * @q_vector: q_vector to set WB_ON_ITR on
 *
 * We need to tell hardware to write-back completed descriptors even when
 * interrupts are disabled. Descriptors will be written back on cache line
 * boundaries without WB_ON_ITR enabled, but if we don't enable WB_ON_ITR
 * descriptors may not be written back if they don't fill a cache line until
 * the next interrupt.
 *
 * This sets the write-back frequency to whatever was set previously for the
 * ITR indices. Also, set the INTENA_MSK bit to make sure hardware knows we
 * aren't meddling with the INTENA_M bit.
 */
static void ice_set_wb_on_itr(struct ice_q_vector *q_vector)
{
	struct ice_vsi *vsi = q_vector->vsi;

	/* already in wb_on_itr mode no need to change it */
	if (q_vector->wb_on_itr)
		return;

	/* use previously set ITR values for all of the ITR indices by
	 * specifying ICE_ITR_NONE, which will vary in adaptive (AIM) mode and
	 * be static in non-adaptive mode (user configured)
	 */
	wr32(&vsi->back->hw, GLINT_DYN_CTL(q_vector->reg_idx),
	     ((ICE_ITR_NONE << GLINT_DYN_CTL_ITR_INDX_S) &
	      GLINT_DYN_CTL_ITR_INDX_M) | GLINT_DYN_CTL_INTENA_MSK_M |
	     GLINT_DYN_CTL_WB_ON_ITR_M);

	q_vector->wb_on_itr = true;
}

/**
 * ice_napi_poll - NAPI polling Rx/Tx cleanup routine
 * @napi: napi struct with our devices info in it
 * @budget: amount of work driver is allowed to do this pass, in packets
 *
 * This function will clean all queues associated with a q_vector.
 *
 * Returns the amount of work done
 */
int ice_napi_poll(struct napi_struct *napi, int budget)
{
	struct ice_q_vector *q_vector =
				container_of(napi, struct ice_q_vector, napi);
	struct ice_tx_ring *tx_ring;
	struct ice_rx_ring *rx_ring;
	bool clean_complete = true;
	int budget_per_ring;
	int work_done = 0;

	/* Since the actual Tx work is minimal, we can give the Tx a larger
	 * budget and be more aggressive about cleaning up the Tx descriptors.
	 */
	ice_for_each_tx_ring(tx_ring, q_vector->tx) {
		bool wd;

		if (tx_ring->xsk_pool)
			wd = ice_clean_tx_irq_zc(tx_ring, budget);
		else if (ice_ring_is_xdp(tx_ring))
			wd = true;
		else
			wd = ice_clean_tx_irq(tx_ring, budget);

		if (!wd)
			clean_complete = false;
	}

	/* Handle case where we are called by netpoll with a budget of 0 */
	if (unlikely(budget <= 0))
		return budget;

	/* normally we have 1 Rx ring per q_vector */
	if (unlikely(q_vector->num_ring_rx > 1))
		/* We attempt to distribute budget to each Rx queue fairly, but
		 * don't allow the budget to go below 1 because that would exit
		 * polling early.
		 */
		budget_per_ring = max_t(int, budget / q_vector->num_ring_rx, 1);
	else
		/* Max of 1 Rx ring in this q_vector so give it the budget */
		budget_per_ring = budget;

	ice_for_each_rx_ring(rx_ring, q_vector->rx) {
		int cleaned;

		/* A dedicated path for zero-copy allows making a single
		 * comparison in the irq context instead of many inside the
		 * ice_clean_rx_irq function and makes the codebase cleaner.
		 */
		cleaned = rx_ring->xsk_pool ?
			  ice_clean_rx_irq_zc(rx_ring, budget_per_ring) :
			  ice_clean_rx_irq(rx_ring, budget_per_ring);
		work_done += cleaned;
		/* if we clean as many as budgeted, we must not be done */
		if (cleaned >= budget_per_ring)
			clean_complete = false;
	}

	/* If work not completed, return budget and polling will return */
	if (!clean_complete) {
		/* Set the writeback on ITR so partial completions of
		 * cache-lines will still continue even if we're polling.
		 */
		ice_set_wb_on_itr(q_vector);
		return budget;
	}

	/* Exit the polling mode, but don't re-enable interrupts if stack might
	 * poll us due to busy-polling
	 */
	if (likely(napi_complete_done(napi, work_done))) {
		ice_net_dim(q_vector);
		ice_enable_interrupt(q_vector);
	} else {
		ice_set_wb_on_itr(q_vector);
	}

	return min_t(int, work_done, budget - 1);
}

/**
 * __ice_maybe_stop_tx - 2nd level check for Tx stop conditions
 * @tx_ring: the ring to be checked
 * @size: the size buffer we want to assure is available
 *
 * Returns -EBUSY if a stop is needed, else 0
 */
static int __ice_maybe_stop_tx(struct ice_tx_ring *tx_ring, unsigned int size)
{
	netif_stop_subqueue(tx_ring->netdev, tx_ring->q_index);
	/* Memory barrier before checking head and tail */
	smp_mb();

	/* Check again in a case another CPU has just made room available. */
	if (likely(ICE_DESC_UNUSED(tx_ring) < size))
		return -EBUSY;

	/* A reprieve! - use start_subqueue because it doesn't call schedule */
	netif_start_subqueue(tx_ring->netdev, tx_ring->q_index);
	++tx_ring->tx_stats.restart_q;
	return 0;
}

/**
 * ice_maybe_stop_tx - 1st level check for Tx stop conditions
 * @tx_ring: the ring to be checked
 * @size:    the size buffer we want to assure is available
 *
 * Returns 0 if stop is not needed
 */
static int ice_maybe_stop_tx(struct ice_tx_ring *tx_ring, unsigned int size)
{
	if (likely(ICE_DESC_UNUSED(tx_ring) >= size))
		return 0;

	return __ice_maybe_stop_tx(tx_ring, size);
}

/**
 * ice_tx_map - Build the Tx descriptor
 * @tx_ring: ring to send buffer on
 * @first: first buffer info buffer to use
 * @off: pointer to struct that holds offload parameters
 *
 * This function loops over the skb data pointed to by *first
 * and gets a physical address for each memory location and programs
 * it and the length into the transmit descriptor.
 */
static void
ice_tx_map(struct ice_tx_ring *tx_ring, struct ice_tx_buf *first,
	   struct ice_tx_offload_params *off)
{
	u64 td_offset, td_tag, td_cmd;
	u16 i = tx_ring->next_to_use;
	unsigned int data_len, size;
	struct ice_tx_desc *tx_desc;
	struct ice_tx_buf *tx_buf;
	struct sk_buff *skb;
	skb_frag_t *frag;
	dma_addr_t dma;

	td_tag = off->td_l2tag1;
	td_cmd = off->td_cmd;
	td_offset = off->td_offset;
	skb = first->skb;

	data_len = skb->data_len;
	size = skb_headlen(skb);

	tx_desc = ICE_TX_DESC(tx_ring, i);

	if (first->tx_flags & ICE_TX_FLAGS_HW_VLAN) {
		td_cmd |= (u64)ICE_TX_DESC_CMD_IL2TAG1;
		td_tag = (first->tx_flags & ICE_TX_FLAGS_VLAN_M) >>
			  ICE_TX_FLAGS_VLAN_S;
	}

	dma = dma_map_single(tx_ring->dev, skb->data, size, DMA_TO_DEVICE);

	tx_buf = first;

	for (frag = &skb_shinfo(skb)->frags[0];; frag++) {
		unsigned int max_data = ICE_MAX_DATA_PER_TXD_ALIGNED;

		if (dma_mapping_error(tx_ring->dev, dma))
			goto dma_error;

		/* record length, and DMA address */
		dma_unmap_len_set(tx_buf, len, size);
		dma_unmap_addr_set(tx_buf, dma, dma);

		/* align size to end of page */
		max_data += -dma & (ICE_MAX_READ_REQ_SIZE - 1);
		tx_desc->buf_addr = cpu_to_le64(dma);

		/* account for data chunks larger than the hardware
		 * can handle
		 */
		while (unlikely(size > ICE_MAX_DATA_PER_TXD)) {
			tx_desc->cmd_type_offset_bsz =
				ice_build_ctob(td_cmd, td_offset, max_data,
					       td_tag);

			tx_desc++;
			i++;

			if (i == tx_ring->count) {
				tx_desc = ICE_TX_DESC(tx_ring, 0);
				i = 0;
			}

			dma += max_data;
			size -= max_data;

			max_data = ICE_MAX_DATA_PER_TXD_ALIGNED;
			tx_desc->buf_addr = cpu_to_le64(dma);
		}

		if (likely(!data_len))
			break;

		tx_desc->cmd_type_offset_bsz = ice_build_ctob(td_cmd, td_offset,
							      size, td_tag);

		tx_desc++;
		i++;

		if (i == tx_ring->count) {
			tx_desc = ICE_TX_DESC(tx_ring, 0);
			i = 0;
		}

		size = skb_frag_size(frag);
		data_len -= size;

		dma = skb_frag_dma_map(tx_ring->dev, frag, 0, size,
				       DMA_TO_DEVICE);

		tx_buf = &tx_ring->tx_buf[i];
	}

	/* record bytecount for BQL */
	netdev_tx_sent_queue(txring_txq(tx_ring), first->bytecount);

	/* record SW timestamp if HW timestamp is not available */
	skb_tx_timestamp(first->skb);

	i++;
	if (i == tx_ring->count)
		i = 0;

	/* write last descriptor with RS and EOP bits */
	td_cmd |= (u64)ICE_TXD_LAST_DESC_CMD;
	tx_desc->cmd_type_offset_bsz =
			ice_build_ctob(td_cmd, td_offset, size, td_tag);

	/* Force memory writes to complete before letting h/w know there
	 * are new descriptors to fetch.
	 *
	 * We also use this memory barrier to make certain all of the
	 * status bits have been updated before next_to_watch is written.
	 */
	wmb();

	/* set next_to_watch value indicating a packet is present */
	first->next_to_watch = tx_desc;

	tx_ring->next_to_use = i;

	ice_maybe_stop_tx(tx_ring, DESC_NEEDED);

	/* notify HW of packet */
	if (netif_xmit_stopped(txring_txq(tx_ring)) || !netdev_xmit_more())
		writel(i, tx_ring->tail);

	return;

dma_error:
	/* clear DMA mappings for failed tx_buf map */
	for (;;) {
		tx_buf = &tx_ring->tx_buf[i];
		ice_unmap_and_free_tx_buf(tx_ring, tx_buf);
		if (tx_buf == first)
			break;
		if (i == 0)
			i = tx_ring->count;
		i--;
	}

	tx_ring->next_to_use = i;
}

/**
 * ice_tx_csum - Enable Tx checksum offloads
 * @first: pointer to the first descriptor
 * @off: pointer to struct that holds offload parameters
 *
 * Returns 0 or error (negative) if checksum offload can't happen, 1 otherwise.
 */
static
int ice_tx_csum(struct ice_tx_buf *first, struct ice_tx_offload_params *off)
{
	u32 l4_len = 0, l3_len = 0, l2_len = 0;
	struct sk_buff *skb = first->skb;
	union {
		struct iphdr *v4;
		struct ipv6hdr *v6;
		unsigned char *hdr;
	} ip;
	union {
		struct tcphdr *tcp;
		unsigned char *hdr;
	} l4;
	__be16 frag_off, protocol;
	unsigned char *exthdr;
	u32 offset, cmd = 0;
	u8 l4_proto = 0;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return 0;

	ip.hdr = skb_network_header(skb);
	l4.hdr = skb_transport_header(skb);

	/* compute outer L2 header size */
	l2_len = ip.hdr - skb->data;
	offset = (l2_len / 2) << ICE_TX_DESC_LEN_MACLEN_S;

	protocol = vlan_get_protocol(skb);

	if (protocol == htons(ETH_P_IP))
		first->tx_flags |= ICE_TX_FLAGS_IPV4;
	else if (protocol == htons(ETH_P_IPV6))
		first->tx_flags |= ICE_TX_FLAGS_IPV6;

	if (skb->encapsulation) {
		bool gso_ena = false;
		u32 tunnel = 0;

		/* define outer network header type */
		if (first->tx_flags & ICE_TX_FLAGS_IPV4) {
			tunnel |= (first->tx_flags & ICE_TX_FLAGS_TSO) ?
				  ICE_TX_CTX_EIPT_IPV4 :
				  ICE_TX_CTX_EIPT_IPV4_NO_CSUM;
			l4_proto = ip.v4->protocol;
		} else if (first->tx_flags & ICE_TX_FLAGS_IPV6) {
			int ret;

			tunnel |= ICE_TX_CTX_EIPT_IPV6;
			exthdr = ip.hdr + sizeof(*ip.v6);
			l4_proto = ip.v6->nexthdr;
			ret = ipv6_skip_exthdr(skb, exthdr - skb->data,
					       &l4_proto, &frag_off);
			if (ret < 0)
				return -1;
		}

		/* define outer transport */
		switch (l4_proto) {
		case IPPROTO_UDP:
			tunnel |= ICE_TXD_CTX_UDP_TUNNELING;
			first->tx_flags |= ICE_TX_FLAGS_TUNNEL;
			break;
		case IPPROTO_GRE:
			tunnel |= ICE_TXD_CTX_GRE_TUNNELING;
			first->tx_flags |= ICE_TX_FLAGS_TUNNEL;
			break;
		case IPPROTO_IPIP:
		case IPPROTO_IPV6:
			first->tx_flags |= ICE_TX_FLAGS_TUNNEL;
			l4.hdr = skb_inner_network_header(skb);
			break;
		default:
			if (first->tx_flags & ICE_TX_FLAGS_TSO)
				return -1;

			skb_checksum_help(skb);
			return 0;
		}

		/* compute outer L3 header size */
		tunnel |= ((l4.hdr - ip.hdr) / 4) <<
			  ICE_TXD_CTX_QW0_EIPLEN_S;

		/* switch IP header pointer from outer to inner header */
		ip.hdr = skb_inner_network_header(skb);

		/* compute tunnel header size */
		tunnel |= ((ip.hdr - l4.hdr) / 2) <<
			   ICE_TXD_CTX_QW0_NATLEN_S;

		gso_ena = skb_shinfo(skb)->gso_type & SKB_GSO_PARTIAL;
		/* indicate if we need to offload outer UDP header */
		if ((first->tx_flags & ICE_TX_FLAGS_TSO) && !gso_ena &&
		    (skb_shinfo(skb)->gso_type & SKB_GSO_UDP_TUNNEL_CSUM))
			tunnel |= ICE_TXD_CTX_QW0_L4T_CS_M;

		/* record tunnel offload values */
		off->cd_tunnel_params |= tunnel;

		/* set DTYP=1 to indicate that it's an Tx context descriptor
		 * in IPsec tunnel mode with Tx offloads in Quad word 1
		 */
		off->cd_qw1 |= (u64)ICE_TX_DESC_DTYPE_CTX;

		/* switch L4 header pointer from outer to inner */
		l4.hdr = skb_inner_transport_header(skb);
		l4_proto = 0;

		/* reset type as we transition from outer to inner headers */
		first->tx_flags &= ~(ICE_TX_FLAGS_IPV4 | ICE_TX_FLAGS_IPV6);
		if (ip.v4->version == 4)
			first->tx_flags |= ICE_TX_FLAGS_IPV4;
		if (ip.v6->version == 6)
			first->tx_flags |= ICE_TX_FLAGS_IPV6;
	}

	/* Enable IP checksum offloads */
	if (first->tx_flags & ICE_TX_FLAGS_IPV4) {
		l4_proto = ip.v4->protocol;
		/* the stack computes the IP header already, the only time we
		 * need the hardware to recompute it is in the case of TSO.
		 */
		if (first->tx_flags & ICE_TX_FLAGS_TSO)
			cmd |= ICE_TX_DESC_CMD_IIPT_IPV4_CSUM;
		else
			cmd |= ICE_TX_DESC_CMD_IIPT_IPV4;

	} else if (first->tx_flags & ICE_TX_FLAGS_IPV6) {
		cmd |= ICE_TX_DESC_CMD_IIPT_IPV6;
		exthdr = ip.hdr + sizeof(*ip.v6);
		l4_proto = ip.v6->nexthdr;
		if (l4.hdr != exthdr)
			ipv6_skip_exthdr(skb, exthdr - skb->data, &l4_proto,
					 &frag_off);
	} else {
		return -1;
	}

	/* compute inner L3 header size */
	l3_len = l4.hdr - ip.hdr;
	offset |= (l3_len / 4) << ICE_TX_DESC_LEN_IPLEN_S;

	/* Enable L4 checksum offloads */
	switch (l4_proto) {
	case IPPROTO_TCP:
		/* enable checksum offloads */
		cmd |= ICE_TX_DESC_CMD_L4T_EOFT_TCP;
		l4_len = l4.tcp->doff;
		offset |= l4_len << ICE_TX_DESC_LEN_L4_LEN_S;
		break;
	case IPPROTO_UDP:
		/* enable UDP checksum offload */
		cmd |= ICE_TX_DESC_CMD_L4T_EOFT_UDP;
		l4_len = (sizeof(struct udphdr) >> 2);
		offset |= l4_len << ICE_TX_DESC_LEN_L4_LEN_S;
		break;
	case IPPROTO_SCTP:
		/* enable SCTP checksum offload */
		cmd |= ICE_TX_DESC_CMD_L4T_EOFT_SCTP;
		l4_len = sizeof(struct sctphdr) >> 2;
		offset |= l4_len << ICE_TX_DESC_LEN_L4_LEN_S;
		break;

	default:
		if (first->tx_flags & ICE_TX_FLAGS_TSO)
			return -1;
		skb_checksum_help(skb);
		return 0;
	}

	off->td_cmd |= cmd;
	off->td_offset |= offset;
	return 1;
}

/**
 * ice_tx_prepare_vlan_flags - prepare generic Tx VLAN tagging flags for HW
 * @tx_ring: ring to send buffer on
 * @first: pointer to struct ice_tx_buf
 *
 * Checks the skb and set up correspondingly several generic transmit flags
 * related to VLAN tagging for the HW, such as VLAN, DCB, etc.
 */
static void
ice_tx_prepare_vlan_flags(struct ice_tx_ring *tx_ring, struct ice_tx_buf *first)
{
	struct sk_buff *skb = first->skb;

	/* nothing left to do, software offloaded VLAN */
	if (!skb_vlan_tag_present(skb) && eth_type_vlan(skb->protocol))
		return;

	/* currently, we always assume 802.1Q for VLAN insertion as VLAN
	 * insertion for 802.1AD is not supported
	 */
	if (skb_vlan_tag_present(skb)) {
		first->tx_flags |= skb_vlan_tag_get(skb) << ICE_TX_FLAGS_VLAN_S;
		first->tx_flags |= ICE_TX_FLAGS_HW_VLAN;
	}

	ice_tx_prepare_vlan_flags_dcb(tx_ring, first);
}

/**
 * ice_tso - computes mss and TSO length to prepare for TSO
 * @first: pointer to struct ice_tx_buf
 * @off: pointer to struct that holds offload parameters
 *
 * Returns 0 or error (negative) if TSO can't happen, 1 otherwise.
 */
static
int ice_tso(struct ice_tx_buf *first, struct ice_tx_offload_params *off)
{
	struct sk_buff *skb = first->skb;
	union {
		struct iphdr *v4;
		struct ipv6hdr *v6;
		unsigned char *hdr;
	} ip;
	union {
		struct tcphdr *tcp;
		struct udphdr *udp;
		unsigned char *hdr;
	} l4;
	u64 cd_mss, cd_tso_len;
	u32 paylen;
	u8 l4_start;
	int err;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return 0;

	if (!skb_is_gso(skb))
		return 0;

	err = skb_cow_head(skb, 0);
	if (err < 0)
		return err;

	/* cppcheck-suppress unreadVariable */
	ip.hdr = skb_network_header(skb);
	l4.hdr = skb_transport_header(skb);

	/* initialize outer IP header fields */
	if (ip.v4->version == 4) {
		ip.v4->tot_len = 0;
		ip.v4->check = 0;
	} else {
		ip.v6->payload_len = 0;
	}

	if (skb_shinfo(skb)->gso_type & (SKB_GSO_GRE |
					 SKB_GSO_GRE_CSUM |
					 SKB_GSO_IPXIP4 |
					 SKB_GSO_IPXIP6 |
					 SKB_GSO_UDP_TUNNEL |
					 SKB_GSO_UDP_TUNNEL_CSUM)) {
		if (!(skb_shinfo(skb)->gso_type & SKB_GSO_PARTIAL) &&
		    (skb_shinfo(skb)->gso_type & SKB_GSO_UDP_TUNNEL_CSUM)) {
			l4.udp->len = 0;

			/* determine offset of outer transport header */
			l4_start = (u8)(l4.hdr - skb->data);

			/* remove payload length from outer checksum */
			paylen = skb->len - l4_start;
			csum_replace_by_diff(&l4.udp->check,
					     (__force __wsum)htonl(paylen));
		}

		/* reset pointers to inner headers */

		/* cppcheck-suppress unreadVariable */
		ip.hdr = skb_inner_network_header(skb);
		l4.hdr = skb_inner_transport_header(skb);

		/* initialize inner IP header fields */
		if (ip.v4->version == 4) {
			ip.v4->tot_len = 0;
			ip.v4->check = 0;
		} else {
			ip.v6->payload_len = 0;
		}
	}

	/* determine offset of transport header */
	l4_start = (u8)(l4.hdr - skb->data);

	/* remove payload length from checksum */
	paylen = skb->len - l4_start;

	if (skb_shinfo(skb)->gso_type & SKB_GSO_UDP_L4) {
		csum_replace_by_diff(&l4.udp->check,
				     (__force __wsum)htonl(paylen));
		/* compute length of UDP segmentation header */
		off->header_len = (u8)sizeof(l4.udp) + l4_start;
	} else {
		csum_replace_by_diff(&l4.tcp->check,
				     (__force __wsum)htonl(paylen));
		/* compute length of TCP segmentation header */
		off->header_len = (u8)((l4.tcp->doff * 4) + l4_start);
	}

	/* update gso_segs and bytecount */
	first->gso_segs = skb_shinfo(skb)->gso_segs;
	first->bytecount += (first->gso_segs - 1) * off->header_len;

	cd_tso_len = skb->len - off->header_len;
	cd_mss = skb_shinfo(skb)->gso_size;

	/* record cdesc_qw1 with TSO parameters */
	off->cd_qw1 |= (u64)(ICE_TX_DESC_DTYPE_CTX |
			     (ICE_TX_CTX_DESC_TSO << ICE_TXD_CTX_QW1_CMD_S) |
			     (cd_tso_len << ICE_TXD_CTX_QW1_TSO_LEN_S) |
			     (cd_mss << ICE_TXD_CTX_QW1_MSS_S));
	first->tx_flags |= ICE_TX_FLAGS_TSO;
	return 1;
}

/**
 * ice_txd_use_count  - estimate the number of descriptors needed for Tx
 * @size: transmit request size in bytes
 *
 * Due to hardware alignment restrictions (4K alignment), we need to
 * assume that we can have no more than 12K of data per descriptor, even
 * though each descriptor can take up to 16K - 1 bytes of aligned memory.
 * Thus, we need to divide by 12K. But division is slow! Instead,
 * we decompose the operation into shifts and one relatively cheap
 * multiply operation.
 *
 * To divide by 12K, we first divide by 4K, then divide by 3:
 *     To divide by 4K, shift right by 12 bits
 *     To divide by 3, multiply by 85, then divide by 256
 *     (Divide by 256 is done by shifting right by 8 bits)
 * Finally, we add one to round up. Because 256 isn't an exact multiple of
 * 3, we'll underestimate near each multiple of 12K. This is actually more
 * accurate as we have 4K - 1 of wiggle room that we can fit into the last
 * segment. For our purposes this is accurate out to 1M which is orders of
 * magnitude greater than our largest possible GSO size.
 *
 * This would then be implemented as:
 *     return (((size >> 12) * 85) >> 8) + ICE_DESCS_FOR_SKB_DATA_PTR;
 *
 * Since multiplication and division are commutative, we can reorder
 * operations into:
 *     return ((size * 85) >> 20) + ICE_DESCS_FOR_SKB_DATA_PTR;
 */
static unsigned int ice_txd_use_count(unsigned int size)
{
	return ((size * 85) >> 20) + ICE_DESCS_FOR_SKB_DATA_PTR;
}

/**
 * ice_xmit_desc_count - calculate number of Tx descriptors needed
 * @skb: send buffer
 *
 * Returns number of data descriptors needed for this skb.
 */
static unsigned int ice_xmit_desc_count(struct sk_buff *skb)
{
	const skb_frag_t *frag = &skb_shinfo(skb)->frags[0];
	unsigned int nr_frags = skb_shinfo(skb)->nr_frags;
	unsigned int count = 0, size = skb_headlen(skb);

	for (;;) {
		count += ice_txd_use_count(size);

		if (!nr_frags--)
			break;

		size = skb_frag_size(frag++);
	}

	return count;
}

/**
 * __ice_chk_linearize - Check if there are more than 8 buffers per packet
 * @skb: send buffer
 *
 * Note: This HW can't DMA more than 8 buffers to build a packet on the wire
 * and so we need to figure out the cases where we need to linearize the skb.
 *
 * For TSO we need to count the TSO header and segment payload separately.
 * As such we need to check cases where we have 7 fragments or more as we
 * can potentially require 9 DMA transactions, 1 for the TSO header, 1 for
 * the segment payload in the first descriptor, and another 7 for the
 * fragments.
 */
static bool __ice_chk_linearize(struct sk_buff *skb)
{
	const skb_frag_t *frag, *stale;
	int nr_frags, sum;

	/* no need to check if number of frags is less than 7 */
	nr_frags = skb_shinfo(skb)->nr_frags;
	if (nr_frags < (ICE_MAX_BUF_TXD - 1))
		return false;

	/* We need to walk through the list and validate that each group
	 * of 6 fragments totals at least gso_size.
	 */
	nr_frags -= ICE_MAX_BUF_TXD - 2;
	frag = &skb_shinfo(skb)->frags[0];

	/* Initialize size to the negative value of gso_size minus 1. We
	 * use this as the worst case scenario in which the frag ahead
	 * of us only provides one byte which is why we are limited to 6
	 * descriptors for a single transmit as the header and previous
	 * fragment are already consuming 2 descriptors.
	 */
	sum = 1 - skb_shinfo(skb)->gso_size;

	/* Add size of frags 0 through 4 to create our initial sum */
	sum += skb_frag_size(frag++);
	sum += skb_frag_size(frag++);
	sum += skb_frag_size(frag++);
	sum += skb_frag_size(frag++);
	sum += skb_frag_size(frag++);

	/* Walk through fragments adding latest fragment, testing it, and
	 * then removing stale fragments from the sum.
	 */
	for (stale = &skb_shinfo(skb)->frags[0];; stale++) {
		int stale_size = skb_frag_size(stale);

		sum += skb_frag_size(frag++);

		/* The stale fragment may present us with a smaller
		 * descriptor than the actual fragment size. To account
		 * for that we need to remove all the data on the front and
		 * figure out what the remainder would be in the last
		 * descriptor associated with the fragment.
		 */
		if (stale_size > ICE_MAX_DATA_PER_TXD) {
			int align_pad = -(skb_frag_off(stale)) &
					(ICE_MAX_READ_REQ_SIZE - 1);

			sum -= align_pad;
			stale_size -= align_pad;

			do {
				sum -= ICE_MAX_DATA_PER_TXD_ALIGNED;
				stale_size -= ICE_MAX_DATA_PER_TXD_ALIGNED;
			} while (stale_size > ICE_MAX_DATA_PER_TXD);
		}

		/* if sum is negative we failed to make sufficient progress */
		if (sum < 0)
			return true;

		if (!nr_frags--)
			break;

		sum -= stale_size;
	}

	return false;
}

/**
 * ice_chk_linearize - Check if there are more than 8 fragments per packet
 * @skb:      send buffer
 * @count:    number of buffers used
 *
 * Note: Our HW can't scatter-gather more than 8 fragments to build
 * a packet on the wire and so we need to figure out the cases where we
 * need to linearize the skb.
 */
static bool ice_chk_linearize(struct sk_buff *skb, unsigned int count)
{
	/* Both TSO and single send will work if count is less than 8 */
	if (likely(count < ICE_MAX_BUF_TXD))
		return false;

	if (skb_is_gso(skb))
		return __ice_chk_linearize(skb);

	/* we can support up to 8 data buffers for a single send */
	return count != ICE_MAX_BUF_TXD;
}

/**
 * ice_tstamp - set up context descriptor for hardware timestamp
 * @tx_ring: pointer to the Tx ring to send buffer on
 * @skb: pointer to the SKB we're sending
 * @first: Tx buffer
 * @off: Tx offload parameters
 */
static void
ice_tstamp(struct ice_tx_ring *tx_ring, struct sk_buff *skb,
	   struct ice_tx_buf *first, struct ice_tx_offload_params *off)
{
	s8 idx;

	/* only timestamp the outbound packet if the user has requested it */
	if (likely(!(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)))
		return;

	if (!tx_ring->ptp_tx)
		return;

	/* Tx timestamps cannot be sampled when doing TSO */
	if (first->tx_flags & ICE_TX_FLAGS_TSO)
		return;

	/* Grab an open timestamp slot */
	idx = ice_ptp_request_ts(tx_ring->tx_tstamps, skb);
	if (idx < 0)
		return;

	off->cd_qw1 |= (u64)(ICE_TX_DESC_DTYPE_CTX |
			     (ICE_TX_CTX_DESC_TSYN << ICE_TXD_CTX_QW1_CMD_S) |
			     ((u64)idx << ICE_TXD_CTX_QW1_TSO_LEN_S));
	first->tx_flags |= ICE_TX_FLAGS_TSYN;
}

/**
 * ice_xmit_frame_ring - Sends buffer on Tx ring
 * @skb: send buffer
 * @tx_ring: ring to send buffer on
 *
 * Returns NETDEV_TX_OK if sent, else an error code
 */
static netdev_tx_t
ice_xmit_frame_ring(struct sk_buff *skb, struct ice_tx_ring *tx_ring)
{
	struct ice_tx_offload_params offload = { 0 };
	struct ice_vsi *vsi = tx_ring->vsi;
	struct ice_tx_buf *first;
	struct ethhdr *eth;
	unsigned int count;
	int tso, csum;

	ice_trace(xmit_frame_ring, tx_ring, skb);

	count = ice_xmit_desc_count(skb);
	if (ice_chk_linearize(skb, count)) {
		if (__skb_linearize(skb))
			goto out_drop;
		count = ice_txd_use_count(skb->len);
		tx_ring->tx_stats.tx_linearize++;
	}

	/* need: 1 descriptor per page * PAGE_SIZE/ICE_MAX_DATA_PER_TXD,
	 *       + 1 desc for skb_head_len/ICE_MAX_DATA_PER_TXD,
	 *       + 4 desc gap to avoid the cache line where head is,
	 *       + 1 desc for context descriptor,
	 * otherwise try next time
	 */
	if (ice_maybe_stop_tx(tx_ring, count + ICE_DESCS_PER_CACHE_LINE +
			      ICE_DESCS_FOR_CTX_DESC)) {
		tx_ring->tx_stats.tx_busy++;
		return NETDEV_TX_BUSY;
	}

	offload.tx_ring = tx_ring;

	/* record the location of the first descriptor for this packet */
	first = &tx_ring->tx_buf[tx_ring->next_to_use];
	first->skb = skb;
	first->bytecount = max_t(unsigned int, skb->len, ETH_ZLEN);
	first->gso_segs = 1;
	first->tx_flags = 0;

	/* prepare the VLAN tagging flags for Tx */
	ice_tx_prepare_vlan_flags(tx_ring, first);

	/* set up TSO offload */
	tso = ice_tso(first, &offload);
	if (tso < 0)
		goto out_drop;

	/* always set up Tx checksum offload */
	csum = ice_tx_csum(first, &offload);
	if (csum < 0)
		goto out_drop;

	/* allow CONTROL frames egress from main VSI if FW LLDP disabled */
	eth = (struct ethhdr *)skb_mac_header(skb);
	if (unlikely((skb->priority == TC_PRIO_CONTROL ||
		      eth->h_proto == htons(ETH_P_LLDP)) &&
		     vsi->type == ICE_VSI_PF &&
		     vsi->port_info->qos_cfg.is_sw_lldp))
		offload.cd_qw1 |= (u64)(ICE_TX_DESC_DTYPE_CTX |
					ICE_TX_CTX_DESC_SWTCH_UPLINK <<
					ICE_TXD_CTX_QW1_CMD_S);

	ice_tstamp(tx_ring, skb, first, &offload);
	if (ice_is_switchdev_running(vsi->back))
		ice_eswitch_set_target_vsi(skb, &offload);

	if (offload.cd_qw1 & ICE_TX_DESC_DTYPE_CTX) {
		struct ice_tx_ctx_desc *cdesc;
		u16 i = tx_ring->next_to_use;

		/* grab the next descriptor */
		cdesc = ICE_TX_CTX_DESC(tx_ring, i);
		i++;
		tx_ring->next_to_use = (i < tx_ring->count) ? i : 0;

		/* setup context descriptor */
		cdesc->tunneling_params = cpu_to_le32(offload.cd_tunnel_params);
		cdesc->l2tag2 = cpu_to_le16(offload.cd_l2tag2);
		cdesc->rsvd = cpu_to_le16(0);
		cdesc->qw1 = cpu_to_le64(offload.cd_qw1);
	}

	ice_tx_map(tx_ring, first, &offload);
	return NETDEV_TX_OK;

out_drop:
	ice_trace(xmit_frame_ring_drop, tx_ring, skb);
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

/**
 * ice_start_xmit - Selects the correct VSI and Tx queue to send buffer
 * @skb: send buffer
 * @netdev: network interface device structure
 *
 * Returns NETDEV_TX_OK if sent, else an error code
 */
netdev_tx_t ice_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_vsi *vsi = np->vsi;
	struct ice_tx_ring *tx_ring;

	tx_ring = vsi->tx_rings[skb->queue_mapping];

	/* hardware can't handle really short frames, hardware padding works
	 * beyond this point
	 */
	if (skb_put_padto(skb, ICE_MIN_TX_LEN))
		return NETDEV_TX_OK;

	return ice_xmit_frame_ring(skb, tx_ring);
}

/**
 * ice_get_dscp_up - return the UP/TC value for a SKB
 * @dcbcfg: DCB config that contains DSCP to UP/TC mapping
 * @skb: SKB to query for info to determine UP/TC
 *
 * This function is to only be called when the PF is in L3 DSCP PFC mode
 */
static u8 ice_get_dscp_up(struct ice_dcbx_cfg *dcbcfg, struct sk_buff *skb)
{
	u8 dscp = 0;

	if (skb->protocol == htons(ETH_P_IP))
		dscp = ipv4_get_dsfield(ip_hdr(skb)) >> 2;
	else if (skb->protocol == htons(ETH_P_IPV6))
		dscp = ipv6_get_dsfield(ipv6_hdr(skb)) >> 2;

	return dcbcfg->dscp_map[dscp];
}

u16
ice_select_queue(struct net_device *netdev, struct sk_buff *skb,
		 struct net_device *sb_dev)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);
	struct ice_dcbx_cfg *dcbcfg;

	dcbcfg = &pf->hw.port_info->qos_cfg.local_dcbx_cfg;
	if (dcbcfg->pfc_mode == ICE_QOS_MODE_DSCP)
		skb->priority = ice_get_dscp_up(dcbcfg, skb);

	return netdev_pick_tx(netdev, skb, sb_dev);
}

/**
 * ice_clean_ctrl_tx_irq - interrupt handler for flow director Tx queue
 * @tx_ring: tx_ring to clean
 */
void ice_clean_ctrl_tx_irq(struct ice_tx_ring *tx_ring)
{
	struct ice_vsi *vsi = tx_ring->vsi;
	s16 i = tx_ring->next_to_clean;
	int budget = ICE_DFLT_IRQ_WORK;
	struct ice_tx_desc *tx_desc;
	struct ice_tx_buf *tx_buf;

	tx_buf = &tx_ring->tx_buf[i];
	tx_desc = ICE_TX_DESC(tx_ring, i);
	i -= tx_ring->count;

	do {
		struct ice_tx_desc *eop_desc = tx_buf->next_to_watch;

		/* if next_to_watch is not set then there is no pending work */
		if (!eop_desc)
			break;

		/* prevent any other reads prior to eop_desc */
		smp_rmb();

		/* if the descriptor isn't done, no work to do */
		if (!(eop_desc->cmd_type_offset_bsz &
		      cpu_to_le64(ICE_TX_DESC_DTYPE_DESC_DONE)))
			break;

		/* clear next_to_watch to prevent false hangs */
		tx_buf->next_to_watch = NULL;
		tx_desc->buf_addr = 0;
		tx_desc->cmd_type_offset_bsz = 0;

		/* move past filter desc */
		tx_buf++;
		tx_desc++;
		i++;
		if (unlikely(!i)) {
			i -= tx_ring->count;
			tx_buf = tx_ring->tx_buf;
			tx_desc = ICE_TX_DESC(tx_ring, 0);
		}

		/* unmap the data header */
		if (dma_unmap_len(tx_buf, len))
			dma_unmap_single(tx_ring->dev,
					 dma_unmap_addr(tx_buf, dma),
					 dma_unmap_len(tx_buf, len),
					 DMA_TO_DEVICE);
		if (tx_buf->tx_flags & ICE_TX_FLAGS_DUMMY_PKT)
			devm_kfree(tx_ring->dev, tx_buf->raw_buf);

		/* clear next_to_watch to prevent false hangs */
		tx_buf->raw_buf = NULL;
		tx_buf->tx_flags = 0;
		tx_buf->next_to_watch = NULL;
		dma_unmap_len_set(tx_buf, len, 0);
		tx_desc->buf_addr = 0;
		tx_desc->cmd_type_offset_bsz = 0;

		/* move past eop_desc for start of next FD desc */
		tx_buf++;
		tx_desc++;
		i++;
		if (unlikely(!i)) {
			i -= tx_ring->count;
			tx_buf = tx_ring->tx_buf;
			tx_desc = ICE_TX_DESC(tx_ring, 0);
		}

		budget--;
	} while (likely(budget));

	i += tx_ring->count;
	tx_ring->next_to_clean = i;

	/* re-enable interrupt if needed */
	ice_irq_dynamic_ena(&vsi->back->hw, vsi, vsi->q_vectors[0]);
}
