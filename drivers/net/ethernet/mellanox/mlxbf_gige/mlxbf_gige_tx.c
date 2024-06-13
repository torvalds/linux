// SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause

/* Packet transmit logic for Mellanox Gigabit Ethernet driver
 *
 * Copyright (C) 2020-2021 NVIDIA CORPORATION & AFFILIATES
 */

#include <linux/skbuff.h>

#include "mlxbf_gige.h"
#include "mlxbf_gige_regs.h"

/* Transmit Initialization
 * 1) Allocates TX WQE array using coherent DMA mapping
 * 2) Allocates TX completion counter using coherent DMA mapping
 */
int mlxbf_gige_tx_init(struct mlxbf_gige *priv)
{
	size_t size;

	size = MLXBF_GIGE_TX_WQE_SZ * priv->tx_q_entries;
	priv->tx_wqe_base = dma_alloc_coherent(priv->dev, size,
					       &priv->tx_wqe_base_dma,
					       GFP_KERNEL);
	if (!priv->tx_wqe_base)
		return -ENOMEM;

	priv->tx_wqe_next = priv->tx_wqe_base;

	/* Write TX WQE base address into MMIO reg */
	writeq(priv->tx_wqe_base_dma, priv->base + MLXBF_GIGE_TX_WQ_BASE);

	/* Allocate address for TX completion count */
	priv->tx_cc = dma_alloc_coherent(priv->dev, MLXBF_GIGE_TX_CC_SZ,
					 &priv->tx_cc_dma, GFP_KERNEL);
	if (!priv->tx_cc) {
		dma_free_coherent(priv->dev, size,
				  priv->tx_wqe_base, priv->tx_wqe_base_dma);
		return -ENOMEM;
	}

	/* Write TX CC base address into MMIO reg */
	writeq(priv->tx_cc_dma, priv->base + MLXBF_GIGE_TX_CI_UPDATE_ADDRESS);

	writeq(ilog2(priv->tx_q_entries),
	       priv->base + MLXBF_GIGE_TX_WQ_SIZE_LOG2);

	priv->prev_tx_ci = 0;
	priv->tx_pi = 0;

	return 0;
}

/* Transmit Deinitialization
 * This routine will free allocations done by mlxbf_gige_tx_init(),
 * namely the TX WQE array and the TX completion counter
 */
void mlxbf_gige_tx_deinit(struct mlxbf_gige *priv)
{
	u64 *tx_wqe_addr;
	size_t size;
	int i;

	tx_wqe_addr = priv->tx_wqe_base;

	for (i = 0; i < priv->tx_q_entries; i++) {
		if (priv->tx_skb[i]) {
			dma_unmap_single(priv->dev, *tx_wqe_addr,
					 priv->tx_skb[i]->len, DMA_TO_DEVICE);
			dev_kfree_skb(priv->tx_skb[i]);
			priv->tx_skb[i] = NULL;
		}
		tx_wqe_addr += 2;
	}

	size = MLXBF_GIGE_TX_WQE_SZ * priv->tx_q_entries;
	dma_free_coherent(priv->dev, size,
			  priv->tx_wqe_base, priv->tx_wqe_base_dma);

	dma_free_coherent(priv->dev, MLXBF_GIGE_TX_CC_SZ,
			  priv->tx_cc, priv->tx_cc_dma);

	priv->tx_wqe_base = NULL;
	priv->tx_wqe_base_dma = 0;
	priv->tx_cc = NULL;
	priv->tx_cc_dma = 0;
	priv->tx_wqe_next = NULL;
	writeq(0, priv->base + MLXBF_GIGE_TX_WQ_BASE);
	writeq(0, priv->base + MLXBF_GIGE_TX_CI_UPDATE_ADDRESS);
}

/* Function that returns status of TX ring:
 *          0: TX ring is full, i.e. there are no
 *             available un-used entries in TX ring.
 *   non-null: TX ring is not full, i.e. there are
 *             some available entries in TX ring.
 *             The non-null value is a measure of
 *             how many TX entries are available, but
 *             it is not the exact number of available
 *             entries (see below).
 *
 * The algorithm makes the assumption that if
 * (prev_tx_ci == tx_pi) then the TX ring is empty.
 * An empty ring actually has (tx_q_entries-1)
 * entries, which allows the algorithm to differentiate
 * the case of an empty ring vs. a full ring.
 */
static u16 mlxbf_gige_tx_buffs_avail(struct mlxbf_gige *priv)
{
	unsigned long flags;
	u16 avail;

	spin_lock_irqsave(&priv->lock, flags);

	if (priv->prev_tx_ci == priv->tx_pi)
		avail = priv->tx_q_entries - 1;
	else
		avail = ((priv->tx_q_entries + priv->prev_tx_ci - priv->tx_pi)
			  % priv->tx_q_entries) - 1;

	spin_unlock_irqrestore(&priv->lock, flags);

	return avail;
}

bool mlxbf_gige_handle_tx_complete(struct mlxbf_gige *priv)
{
	struct net_device_stats *stats;
	u16 tx_wqe_index;
	u64 *tx_wqe_addr;
	u64 tx_status;
	u16 tx_ci;

	tx_status = readq(priv->base + MLXBF_GIGE_TX_STATUS);
	if (tx_status & MLXBF_GIGE_TX_STATUS_DATA_FIFO_FULL)
		priv->stats.tx_fifo_full++;
	tx_ci = readq(priv->base + MLXBF_GIGE_TX_CONSUMER_INDEX);
	stats = &priv->netdev->stats;

	/* Transmit completion logic needs to loop until the completion
	 * index (in SW) equals TX consumer index (from HW).  These
	 * parameters are unsigned 16-bit values and the wrap case needs
	 * to be supported, that is TX consumer index wrapped from 0xFFFF
	 * to 0 while TX completion index is still < 0xFFFF.
	 */
	for (; priv->prev_tx_ci != tx_ci; priv->prev_tx_ci++) {
		tx_wqe_index = priv->prev_tx_ci % priv->tx_q_entries;
		/* Each TX WQE is 16 bytes. The 8 MSB store the 2KB TX
		 * buffer address and the 8 LSB contain information
		 * about the TX WQE.
		 */
		tx_wqe_addr = priv->tx_wqe_base +
			       (tx_wqe_index * MLXBF_GIGE_TX_WQE_SZ_QWORDS);

		stats->tx_packets++;
		stats->tx_bytes += MLXBF_GIGE_TX_WQE_PKT_LEN(tx_wqe_addr);

		dma_unmap_single(priv->dev, *tx_wqe_addr,
				 priv->tx_skb[tx_wqe_index]->len, DMA_TO_DEVICE);
		dev_consume_skb_any(priv->tx_skb[tx_wqe_index]);
		priv->tx_skb[tx_wqe_index] = NULL;

		/* Ensure completion of updates across all cores */
		mb();
	}

	/* Since the TX ring was likely just drained, check if TX queue
	 * had previously been stopped and now that there are TX buffers
	 * available the TX queue can be awakened.
	 */
	if (netif_queue_stopped(priv->netdev) &&
	    mlxbf_gige_tx_buffs_avail(priv))
		netif_wake_queue(priv->netdev);

	return true;
}

/* Function to advance the tx_wqe_next pointer to next TX WQE */
void mlxbf_gige_update_tx_wqe_next(struct mlxbf_gige *priv)
{
	/* Advance tx_wqe_next pointer */
	priv->tx_wqe_next += MLXBF_GIGE_TX_WQE_SZ_QWORDS;

	/* Check if 'next' pointer is beyond end of TX ring */
	/* If so, set 'next' back to 'base' pointer of ring */
	if (priv->tx_wqe_next == (priv->tx_wqe_base +
				  (priv->tx_q_entries * MLXBF_GIGE_TX_WQE_SZ_QWORDS)))
		priv->tx_wqe_next = priv->tx_wqe_base;
}

netdev_tx_t mlxbf_gige_start_xmit(struct sk_buff *skb,
				  struct net_device *netdev)
{
	struct mlxbf_gige *priv = netdev_priv(netdev);
	long buff_addr, start_dma_page, end_dma_page;
	struct sk_buff *tx_skb;
	dma_addr_t tx_buf_dma;
	unsigned long flags;
	u64 *tx_wqe_addr;
	u64 word2;

	/* If needed, linearize TX SKB as hardware DMA expects this */
	if (skb->len > MLXBF_GIGE_DEFAULT_BUF_SZ || skb_linearize(skb)) {
		dev_kfree_skb(skb);
		netdev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	buff_addr = (long)skb->data;
	start_dma_page = buff_addr >> MLXBF_GIGE_DMA_PAGE_SHIFT;
	end_dma_page   = (buff_addr + skb->len - 1) >> MLXBF_GIGE_DMA_PAGE_SHIFT;

	/* Verify that payload pointer and data length of SKB to be
	 * transmitted does not violate the hardware DMA limitation.
	 */
	if (start_dma_page != end_dma_page) {
		/* DMA operation would fail as-is, alloc new aligned SKB */
		tx_skb = mlxbf_gige_alloc_skb(priv, skb->len,
					      &tx_buf_dma, DMA_TO_DEVICE);
		if (!tx_skb) {
			/* Free original skb, could not alloc new aligned SKB */
			dev_kfree_skb(skb);
			netdev->stats.tx_dropped++;
			return NETDEV_TX_OK;
		}

		skb_put_data(tx_skb, skb->data, skb->len);

		/* Free the original SKB */
		dev_kfree_skb(skb);
	} else {
		tx_skb = skb;
		tx_buf_dma = dma_map_single(priv->dev, skb->data,
					    skb->len, DMA_TO_DEVICE);
		if (dma_mapping_error(priv->dev, tx_buf_dma)) {
			dev_kfree_skb(skb);
			netdev->stats.tx_dropped++;
			return NETDEV_TX_OK;
		}
	}

	/* Get address of TX WQE */
	tx_wqe_addr = priv->tx_wqe_next;

	mlxbf_gige_update_tx_wqe_next(priv);

	/* Put PA of buffer address into first 64-bit word of TX WQE */
	*tx_wqe_addr = tx_buf_dma;

	/* Set TX WQE pkt_len appropriately
	 * NOTE: GigE silicon will automatically pad up to
	 *       minimum packet length if needed.
	 */
	word2 = tx_skb->len & MLXBF_GIGE_TX_WQE_PKT_LEN_MASK;

	/* Write entire 2nd word of TX WQE */
	*(tx_wqe_addr + 1) = word2;

	spin_lock_irqsave(&priv->lock, flags);
	priv->tx_skb[priv->tx_pi % priv->tx_q_entries] = tx_skb;
	priv->tx_pi++;
	spin_unlock_irqrestore(&priv->lock, flags);

	if (!netdev_xmit_more()) {
		/* Create memory barrier before write to TX PI */
		wmb();
		writeq(priv->tx_pi, priv->base + MLXBF_GIGE_TX_PRODUCER_INDEX);
	}

	/* Check if the last TX entry was just used */
	if (!mlxbf_gige_tx_buffs_avail(priv)) {
		/* TX ring is full, inform stack */
		netif_stop_queue(netdev);

		/* Since there is no separate "TX complete" interrupt, need
		 * to explicitly schedule NAPI poll.  This will trigger logic
		 * which processes TX completions, and will hopefully drain
		 * the TX ring allowing the TX queue to be awakened.
		 */
		napi_schedule(&priv->napi);
	}

	return NETDEV_TX_OK;
}
