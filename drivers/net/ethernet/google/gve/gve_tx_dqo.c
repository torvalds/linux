// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2015-2021 Google, Inc.
 */

#include "gve.h"
#include "gve_adminq.h"
#include "gve_utils.h"
#include "gve_dqo.h"
#include <linux/tcp.h>
#include <linux/slab.h>
#include <linux/skbuff.h>

/* gve_tx_free_desc - Cleans up all pending tx requests and buffers.
 */
static void gve_tx_clean_pending_packets(struct gve_tx_ring *tx)
{
	int i;

	for (i = 0; i < tx->dqo.num_pending_packets; i++) {
		struct gve_tx_pending_packet_dqo *cur_state =
			&tx->dqo.pending_packets[i];
		int j;

		for (j = 0; j < cur_state->num_bufs; j++) {
			struct gve_tx_dma_buf *buf = &cur_state->bufs[j];

			if (j == 0) {
				dma_unmap_single(tx->dev,
						 dma_unmap_addr(buf, dma),
						 dma_unmap_len(buf, len),
						 DMA_TO_DEVICE);
			} else {
				dma_unmap_page(tx->dev,
					       dma_unmap_addr(buf, dma),
					       dma_unmap_len(buf, len),
					       DMA_TO_DEVICE);
			}
		}
		if (cur_state->skb) {
			dev_consume_skb_any(cur_state->skb);
			cur_state->skb = NULL;
		}
	}
}

static void gve_tx_free_ring_dqo(struct gve_priv *priv, int idx)
{
	struct gve_tx_ring *tx = &priv->tx[idx];
	struct device *hdev = &priv->pdev->dev;
	size_t bytes;

	gve_tx_remove_from_block(priv, idx);

	if (tx->q_resources) {
		dma_free_coherent(hdev, sizeof(*tx->q_resources),
				  tx->q_resources, tx->q_resources_bus);
		tx->q_resources = NULL;
	}

	if (tx->dqo.compl_ring) {
		bytes = sizeof(tx->dqo.compl_ring[0]) *
			(tx->dqo.complq_mask + 1);
		dma_free_coherent(hdev, bytes, tx->dqo.compl_ring,
				  tx->complq_bus_dqo);
		tx->dqo.compl_ring = NULL;
	}

	if (tx->dqo.tx_ring) {
		bytes = sizeof(tx->dqo.tx_ring[0]) * (tx->mask + 1);
		dma_free_coherent(hdev, bytes, tx->dqo.tx_ring, tx->bus);
		tx->dqo.tx_ring = NULL;
	}

	kvfree(tx->dqo.pending_packets);
	tx->dqo.pending_packets = NULL;

	netif_dbg(priv, drv, priv->dev, "freed tx queue %d\n", idx);
}

static int gve_tx_alloc_ring_dqo(struct gve_priv *priv, int idx)
{
	struct gve_tx_ring *tx = &priv->tx[idx];
	struct device *hdev = &priv->pdev->dev;
	int num_pending_packets;
	size_t bytes;
	int i;

	memset(tx, 0, sizeof(*tx));
	tx->q_num = idx;
	tx->dev = &priv->pdev->dev;
	tx->netdev_txq = netdev_get_tx_queue(priv->dev, idx);
	atomic_set_release(&tx->dqo_compl.hw_tx_head, 0);

	/* Queue sizes must be a power of 2 */
	tx->mask = priv->tx_desc_cnt - 1;
	tx->dqo.complq_mask = priv->options_dqo_rda.tx_comp_ring_entries - 1;

	/* The max number of pending packets determines the maximum number of
	 * descriptors which maybe written to the completion queue.
	 *
	 * We must set the number small enough to make sure we never overrun the
	 * completion queue.
	 */
	num_pending_packets = tx->dqo.complq_mask + 1;

	/* Reserve space for descriptor completions, which will be reported at
	 * most every GVE_TX_MIN_RE_INTERVAL packets.
	 */
	num_pending_packets -=
		(tx->dqo.complq_mask + 1) / GVE_TX_MIN_RE_INTERVAL;

	/* Each packet may have at most 2 buffer completions if it receives both
	 * a miss and reinjection completion.
	 */
	num_pending_packets /= 2;

	tx->dqo.num_pending_packets = min_t(int, num_pending_packets, S16_MAX);
	tx->dqo.pending_packets = kvcalloc(tx->dqo.num_pending_packets,
					   sizeof(tx->dqo.pending_packets[0]),
					   GFP_KERNEL);
	if (!tx->dqo.pending_packets)
		goto err;

	/* Set up linked list of pending packets */
	for (i = 0; i < tx->dqo.num_pending_packets - 1; i++)
		tx->dqo.pending_packets[i].next = i + 1;

	tx->dqo.pending_packets[tx->dqo.num_pending_packets - 1].next = -1;
	atomic_set_release(&tx->dqo_compl.free_pending_packets, -1);
	tx->dqo_compl.miss_completions.head = -1;
	tx->dqo_compl.miss_completions.tail = -1;
	tx->dqo_compl.timed_out_completions.head = -1;
	tx->dqo_compl.timed_out_completions.tail = -1;

	bytes = sizeof(tx->dqo.tx_ring[0]) * (tx->mask + 1);
	tx->dqo.tx_ring = dma_alloc_coherent(hdev, bytes, &tx->bus, GFP_KERNEL);
	if (!tx->dqo.tx_ring)
		goto err;

	bytes = sizeof(tx->dqo.compl_ring[0]) * (tx->dqo.complq_mask + 1);
	tx->dqo.compl_ring = dma_alloc_coherent(hdev, bytes,
						&tx->complq_bus_dqo,
						GFP_KERNEL);
	if (!tx->dqo.compl_ring)
		goto err;

	tx->q_resources = dma_alloc_coherent(hdev, sizeof(*tx->q_resources),
					     &tx->q_resources_bus, GFP_KERNEL);
	if (!tx->q_resources)
		goto err;

	gve_tx_add_to_block(priv, idx);

	return 0;

err:
	gve_tx_free_ring_dqo(priv, idx);
	return -ENOMEM;
}

int gve_tx_alloc_rings_dqo(struct gve_priv *priv)
{
	int err = 0;
	int i;

	for (i = 0; i < priv->tx_cfg.num_queues; i++) {
		err = gve_tx_alloc_ring_dqo(priv, i);
		if (err) {
			netif_err(priv, drv, priv->dev,
				  "Failed to alloc tx ring=%d: err=%d\n",
				  i, err);
			goto err;
		}
	}

	return 0;

err:
	for (i--; i >= 0; i--)
		gve_tx_free_ring_dqo(priv, i);

	return err;
}

void gve_tx_free_rings_dqo(struct gve_priv *priv)
{
	int i;

	for (i = 0; i < priv->tx_cfg.num_queues; i++) {
		struct gve_tx_ring *tx = &priv->tx[i];

		gve_clean_tx_done_dqo(priv, tx, /*napi=*/NULL);
		netdev_tx_reset_queue(tx->netdev_txq);
		gve_tx_clean_pending_packets(tx);

		gve_tx_free_ring_dqo(priv, i);
	}
}

netdev_tx_t gve_tx_dqo(struct sk_buff *skb, struct net_device *dev)
{
	return NETDEV_TX_OK;
}

int gve_clean_tx_done_dqo(struct gve_priv *priv, struct gve_tx_ring *tx,
			  struct napi_struct *napi)
{
	return 0;
}

bool gve_tx_poll_dqo(struct gve_notify_block *block, bool do_clean)
{
	return false;
}
