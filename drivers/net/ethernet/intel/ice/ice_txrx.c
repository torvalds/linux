// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

/* The driver transmit and receive code */

#include <linux/prefetch.h>
#include <linux/mm.h>
#include "ice.h"

/**
 * ice_unmap_and_free_tx_buf - Release a Tx buffer
 * @ring: the ring that owns the buffer
 * @tx_buf: the buffer to free
 */
static void
ice_unmap_and_free_tx_buf(struct ice_ring *ring, struct ice_tx_buf *tx_buf)
{
	if (tx_buf->skb) {
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

static struct netdev_queue *txring_txq(const struct ice_ring *ring)
{
	return netdev_get_tx_queue(ring->netdev, ring->q_index);
}

/**
 * ice_clean_tx_ring - Free any empty Tx buffers
 * @tx_ring: ring to be cleaned
 */
void ice_clean_tx_ring(struct ice_ring *tx_ring)
{
	unsigned long size;
	u16 i;

	/* ring already cleared, nothing to do */
	if (!tx_ring->tx_buf)
		return;

	/* Free all the Tx ring sk_bufss */
	for (i = 0; i < tx_ring->count; i++)
		ice_unmap_and_free_tx_buf(tx_ring, &tx_ring->tx_buf[i]);

	size = sizeof(struct ice_tx_buf) * tx_ring->count;
	memset(tx_ring->tx_buf, 0, size);

	/* Zero out the descriptor ring */
	memset(tx_ring->desc, 0, tx_ring->size);

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
void ice_free_tx_ring(struct ice_ring *tx_ring)
{
	ice_clean_tx_ring(tx_ring);
	devm_kfree(tx_ring->dev, tx_ring->tx_buf);
	tx_ring->tx_buf = NULL;

	if (tx_ring->desc) {
		dmam_free_coherent(tx_ring->dev, tx_ring->size,
				   tx_ring->desc, tx_ring->dma);
		tx_ring->desc = NULL;
	}
}

/**
 * ice_setup_tx_ring - Allocate the Tx descriptors
 * @tx_ring: the tx ring to set up
 *
 * Return 0 on success, negative on error
 */
int ice_setup_tx_ring(struct ice_ring *tx_ring)
{
	struct device *dev = tx_ring->dev;
	int bi_size;

	if (!dev)
		return -ENOMEM;

	/* warn if we are about to overwrite the pointer */
	WARN_ON(tx_ring->tx_buf);
	bi_size = sizeof(struct ice_tx_buf) * tx_ring->count;
	tx_ring->tx_buf = devm_kzalloc(dev, bi_size, GFP_KERNEL);
	if (!tx_ring->tx_buf)
		return -ENOMEM;

	/* round up to nearest 4K */
	tx_ring->size = tx_ring->count * sizeof(struct ice_tx_desc);
	tx_ring->size = ALIGN(tx_ring->size, 4096);
	tx_ring->desc = dmam_alloc_coherent(dev, tx_ring->size, &tx_ring->dma,
					    GFP_KERNEL);
	if (!tx_ring->desc) {
		dev_err(dev, "Unable to allocate memory for the Tx descriptor ring, size=%d\n",
			tx_ring->size);
		goto err;
	}

	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;
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
void ice_clean_rx_ring(struct ice_ring *rx_ring)
{
	struct device *dev = rx_ring->dev;
	unsigned long size;
	u16 i;

	/* ring already cleared, nothing to do */
	if (!rx_ring->rx_buf)
		return;

	/* Free all the Rx ring sk_buffs */
	for (i = 0; i < rx_ring->count; i++) {
		struct ice_rx_buf *rx_buf = &rx_ring->rx_buf[i];

		if (rx_buf->skb) {
			dev_kfree_skb(rx_buf->skb);
			rx_buf->skb = NULL;
		}
		if (!rx_buf->page)
			continue;

		dma_unmap_page(dev, rx_buf->dma, PAGE_SIZE, DMA_FROM_DEVICE);
		__free_pages(rx_buf->page, 0);

		rx_buf->page = NULL;
		rx_buf->page_offset = 0;
	}

	size = sizeof(struct ice_rx_buf) * rx_ring->count;
	memset(rx_ring->rx_buf, 0, size);

	/* Zero out the descriptor ring */
	memset(rx_ring->desc, 0, rx_ring->size);

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
void ice_free_rx_ring(struct ice_ring *rx_ring)
{
	ice_clean_rx_ring(rx_ring);
	devm_kfree(rx_ring->dev, rx_ring->rx_buf);
	rx_ring->rx_buf = NULL;

	if (rx_ring->desc) {
		dmam_free_coherent(rx_ring->dev, rx_ring->size,
				   rx_ring->desc, rx_ring->dma);
		rx_ring->desc = NULL;
	}
}

/**
 * ice_setup_rx_ring - Allocate the Rx descriptors
 * @rx_ring: the rx ring to set up
 *
 * Return 0 on success, negative on error
 */
int ice_setup_rx_ring(struct ice_ring *rx_ring)
{
	struct device *dev = rx_ring->dev;
	int bi_size;

	if (!dev)
		return -ENOMEM;

	/* warn if we are about to overwrite the pointer */
	WARN_ON(rx_ring->rx_buf);
	bi_size = sizeof(struct ice_rx_buf) * rx_ring->count;
	rx_ring->rx_buf = devm_kzalloc(dev, bi_size, GFP_KERNEL);
	if (!rx_ring->rx_buf)
		return -ENOMEM;

	/* round up to nearest 4K */
	rx_ring->size = rx_ring->count * sizeof(union ice_32byte_rx_desc);
	rx_ring->size = ALIGN(rx_ring->size, 4096);
	rx_ring->desc = dmam_alloc_coherent(dev, rx_ring->size, &rx_ring->dma,
					    GFP_KERNEL);
	if (!rx_ring->desc) {
		dev_err(dev, "Unable to allocate memory for the Rx descriptor ring, size=%d\n",
			rx_ring->size);
		goto err;
	}

	rx_ring->next_to_use = 0;
	rx_ring->next_to_clean = 0;
	return 0;

err:
	devm_kfree(dev, rx_ring->rx_buf);
	rx_ring->rx_buf = NULL;
	return -ENOMEM;
}

/**
 * ice_release_rx_desc - Store the new tail and head values
 * @rx_ring: ring to bump
 * @val: new head index
 */
static void ice_release_rx_desc(struct ice_ring *rx_ring, u32 val)
{
	rx_ring->next_to_use = val;

	/* update next to alloc since we have filled the ring */
	rx_ring->next_to_alloc = val;

	/* Force memory writes to complete before letting h/w
	 * know there are new descriptors to fetch.  (Only
	 * applicable for weak-ordered memory model archs,
	 * such as IA-64).
	 */
	wmb();
	writel(val, rx_ring->tail);
}

/**
 * ice_alloc_mapped_page - recycle or make a new page
 * @rx_ring: ring to use
 * @bi: rx_buf struct to modify
 *
 * Returns true if the page was successfully allocated or
 * reused.
 */
static bool ice_alloc_mapped_page(struct ice_ring *rx_ring,
				  struct ice_rx_buf *bi)
{
	struct page *page = bi->page;
	dma_addr_t dma;

	/* since we are recycling buffers we should seldom need to alloc */
	if (likely(page))
		return true;

	/* alloc new page for storage */
	page = alloc_page(GFP_ATOMIC | __GFP_NOWARN);
	if (unlikely(!page))
		return false;

	/* map page for use */
	dma = dma_map_page(rx_ring->dev, page, 0, PAGE_SIZE, DMA_FROM_DEVICE);

	/* if mapping failed free memory back to system since
	 * there isn't much point in holding memory we can't use
	 */
	if (dma_mapping_error(rx_ring->dev, dma)) {
		__free_pages(page, 0);
		return false;
	}

	bi->dma = dma;
	bi->page = page;
	bi->page_offset = 0;

	return true;
}

/**
 * ice_alloc_rx_bufs - Replace used receive buffers
 * @rx_ring: ring to place buffers on
 * @cleaned_count: number of buffers to replace
 *
 * Returns false if all allocations were successful, true if any fail
 */
bool ice_alloc_rx_bufs(struct ice_ring *rx_ring, u16 cleaned_count)
{
	union ice_32b_rx_flex_desc *rx_desc;
	u16 ntu = rx_ring->next_to_use;
	struct ice_rx_buf *bi;

	/* do nothing if no valid netdev defined */
	if (!rx_ring->netdev || !cleaned_count)
		return false;

	/* get the RX descriptor and buffer based on next_to_use */
	rx_desc = ICE_RX_DESC(rx_ring, ntu);
	bi = &rx_ring->rx_buf[ntu];

	do {
		if (!ice_alloc_mapped_page(rx_ring, bi))
			goto no_bufs;

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

	return false;

no_bufs:
	if (rx_ring->next_to_use != ntu)
		ice_release_rx_desc(rx_ring, ntu);

	/* make sure to come back via polling to try again after
	 * allocation failure
	 */
	return true;
}
