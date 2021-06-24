// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2015-2021 Google, Inc.
 */

#include "gve.h"
#include "gve_dqo.h"
#include "gve_adminq.h"
#include "gve_utils.h"
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <net/ip6_checksum.h>
#include <net/ipv6.h>
#include <net/tcp.h>

static void gve_free_page_dqo(struct gve_priv *priv,
			      struct gve_rx_buf_state_dqo *bs)
{
}

static void gve_rx_free_ring_dqo(struct gve_priv *priv, int idx)
{
	struct gve_rx_ring *rx = &priv->rx[idx];
	struct device *hdev = &priv->pdev->dev;
	size_t completion_queue_slots;
	size_t buffer_queue_slots;
	size_t size;
	int i;

	completion_queue_slots = rx->dqo.complq.mask + 1;
	buffer_queue_slots = rx->dqo.bufq.mask + 1;

	gve_rx_remove_from_block(priv, idx);

	if (rx->q_resources) {
		dma_free_coherent(hdev, sizeof(*rx->q_resources),
				  rx->q_resources, rx->q_resources_bus);
		rx->q_resources = NULL;
	}

	for (i = 0; i < rx->dqo.num_buf_states; i++) {
		struct gve_rx_buf_state_dqo *bs = &rx->dqo.buf_states[i];

		if (bs->page_info.page)
			gve_free_page_dqo(priv, bs);
	}

	if (rx->dqo.bufq.desc_ring) {
		size = sizeof(rx->dqo.bufq.desc_ring[0]) * buffer_queue_slots;
		dma_free_coherent(hdev, size, rx->dqo.bufq.desc_ring,
				  rx->dqo.bufq.bus);
		rx->dqo.bufq.desc_ring = NULL;
	}

	if (rx->dqo.complq.desc_ring) {
		size = sizeof(rx->dqo.complq.desc_ring[0]) *
			completion_queue_slots;
		dma_free_coherent(hdev, size, rx->dqo.complq.desc_ring,
				  rx->dqo.complq.bus);
		rx->dqo.complq.desc_ring = NULL;
	}

	kvfree(rx->dqo.buf_states);
	rx->dqo.buf_states = NULL;

	netif_dbg(priv, drv, priv->dev, "freed rx ring %d\n", idx);
}

static int gve_rx_alloc_ring_dqo(struct gve_priv *priv, int idx)
{
	struct gve_rx_ring *rx = &priv->rx[idx];
	struct device *hdev = &priv->pdev->dev;
	size_t size;
	int i;

	const u32 buffer_queue_slots =
		priv->options_dqo_rda.rx_buff_ring_entries;
	const u32 completion_queue_slots = priv->rx_desc_cnt;

	netif_dbg(priv, drv, priv->dev, "allocating rx ring DQO\n");

	memset(rx, 0, sizeof(*rx));
	rx->gve = priv;
	rx->q_num = idx;
	rx->dqo.bufq.mask = buffer_queue_slots - 1;
	rx->dqo.complq.num_free_slots = completion_queue_slots;
	rx->dqo.complq.mask = completion_queue_slots - 1;
	rx->skb_head = NULL;
	rx->skb_tail = NULL;

	rx->dqo.num_buf_states = min_t(s16, S16_MAX, buffer_queue_slots * 4);
	rx->dqo.buf_states = kvcalloc(rx->dqo.num_buf_states,
				      sizeof(rx->dqo.buf_states[0]),
				      GFP_KERNEL);
	if (!rx->dqo.buf_states)
		return -ENOMEM;

	/* Set up linked list of buffer IDs */
	for (i = 0; i < rx->dqo.num_buf_states - 1; i++)
		rx->dqo.buf_states[i].next = i + 1;

	rx->dqo.buf_states[rx->dqo.num_buf_states - 1].next = -1;
	rx->dqo.recycled_buf_states.head = -1;
	rx->dqo.recycled_buf_states.tail = -1;
	rx->dqo.used_buf_states.head = -1;
	rx->dqo.used_buf_states.tail = -1;

	/* Allocate RX completion queue */
	size = sizeof(rx->dqo.complq.desc_ring[0]) *
		completion_queue_slots;
	rx->dqo.complq.desc_ring =
		dma_alloc_coherent(hdev, size, &rx->dqo.complq.bus, GFP_KERNEL);
	if (!rx->dqo.complq.desc_ring)
		goto err;

	/* Allocate RX buffer queue */
	size = sizeof(rx->dqo.bufq.desc_ring[0]) * buffer_queue_slots;
	rx->dqo.bufq.desc_ring =
		dma_alloc_coherent(hdev, size, &rx->dqo.bufq.bus, GFP_KERNEL);
	if (!rx->dqo.bufq.desc_ring)
		goto err;

	rx->q_resources = dma_alloc_coherent(hdev, sizeof(*rx->q_resources),
					     &rx->q_resources_bus, GFP_KERNEL);
	if (!rx->q_resources)
		goto err;

	gve_rx_add_to_block(priv, idx);

	return 0;

err:
	gve_rx_free_ring_dqo(priv, idx);
	return -ENOMEM;
}

int gve_rx_alloc_rings_dqo(struct gve_priv *priv)
{
	int err = 0;
	int i;

	for (i = 0; i < priv->rx_cfg.num_queues; i++) {
		err = gve_rx_alloc_ring_dqo(priv, i);
		if (err) {
			netif_err(priv, drv, priv->dev,
				  "Failed to alloc rx ring=%d: err=%d\n",
				  i, err);
			goto err;
		}
	}

	return 0;

err:
	for (i--; i >= 0; i--)
		gve_rx_free_ring_dqo(priv, i);

	return err;
}

void gve_rx_free_rings_dqo(struct gve_priv *priv)
{
	int i;

	for (i = 0; i < priv->rx_cfg.num_queues; i++)
		gve_rx_free_ring_dqo(priv, i);
}

void gve_rx_post_buffers_dqo(struct gve_rx_ring *rx)
{
}

int gve_rx_poll_dqo(struct gve_notify_block *block, int budget)
{
	u32 work_done = 0;

	return work_done;
}
