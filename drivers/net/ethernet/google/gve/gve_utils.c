// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2015-2021 Google, Inc.
 */

#include "gve.h"
#include "gve_adminq.h"
#include "gve_utils.h"

bool gve_tx_was_added_to_block(struct gve_priv *priv, int queue_idx)
{
	struct gve_notify_block *block =
			&priv->ntfy_blocks[gve_tx_idx_to_ntfy(priv, queue_idx)];

	return block->tx != NULL;
}

void gve_tx_remove_from_block(struct gve_priv *priv, int queue_idx)
{
	struct gve_notify_block *block =
			&priv->ntfy_blocks[gve_tx_idx_to_ntfy(priv, queue_idx)];

	block->tx = NULL;
}

void gve_tx_add_to_block(struct gve_priv *priv, int queue_idx)
{
	unsigned int active_cpus = min_t(int, priv->num_ntfy_blks / 2,
					 num_online_cpus());
	int ntfy_idx = gve_tx_idx_to_ntfy(priv, queue_idx);
	struct gve_notify_block *block = &priv->ntfy_blocks[ntfy_idx];
	struct gve_tx_ring *tx = &priv->tx[queue_idx];

	block->tx = tx;
	tx->ntfy_id = ntfy_idx;
	netif_set_xps_queue(priv->dev, get_cpu_mask(ntfy_idx % active_cpus),
			    queue_idx);
}

bool gve_rx_was_added_to_block(struct gve_priv *priv, int queue_idx)
{
	struct gve_notify_block *block =
			&priv->ntfy_blocks[gve_rx_idx_to_ntfy(priv, queue_idx)];

	return block->rx != NULL;
}

void gve_rx_remove_from_block(struct gve_priv *priv, int queue_idx)
{
	struct gve_notify_block *block =
			&priv->ntfy_blocks[gve_rx_idx_to_ntfy(priv, queue_idx)];

	block->rx = NULL;
}

void gve_rx_add_to_block(struct gve_priv *priv, int queue_idx)
{
	u32 ntfy_idx = gve_rx_idx_to_ntfy(priv, queue_idx);
	struct gve_notify_block *block = &priv->ntfy_blocks[ntfy_idx];
	struct gve_rx_ring *rx = &priv->rx[queue_idx];

	block->rx = rx;
	rx->ntfy_id = ntfy_idx;
}

struct sk_buff *gve_rx_copy_data(struct net_device *dev, struct napi_struct *napi,
				 u8 *data, u16 len)
{
	struct sk_buff *skb;

	skb = napi_alloc_skb(napi, len);
	if (unlikely(!skb))
		return NULL;

	__skb_put(skb, len);
	skb_copy_to_linear_data_offset(skb, 0, data, len);
	skb->protocol = eth_type_trans(skb, dev);

	return skb;
}

struct sk_buff *gve_rx_copy(struct net_device *dev, struct napi_struct *napi,
			    struct gve_rx_slot_page_info *page_info, u16 len)
{
	void *va = page_info->page_address + page_info->page_offset +
		page_info->pad;

	return gve_rx_copy_data(dev, napi, va, len);
}

void gve_dec_pagecnt_bias(struct gve_rx_slot_page_info *page_info)
{
	page_info->pagecnt_bias--;
	if (page_info->pagecnt_bias == 0) {
		int pagecount = page_count(page_info->page);

		/* If we have run out of bias - set it back up to INT_MAX
		 * minus the existing refs.
		 */
		page_info->pagecnt_bias = INT_MAX - pagecount;

		/* Set pagecount back up to max. */
		page_ref_add(page_info->page, INT_MAX - pagecount);
	}
}

void gve_add_napi(struct gve_priv *priv, int ntfy_idx,
		  int (*gve_poll)(struct napi_struct *, int))
{
	struct gve_notify_block *block = &priv->ntfy_blocks[ntfy_idx];

	netif_napi_add_locked(priv->dev, &block->napi, gve_poll);
	netif_napi_set_irq_locked(&block->napi, block->irq);
}

void gve_remove_napi(struct gve_priv *priv, int ntfy_idx)
{
	struct gve_notify_block *block = &priv->ntfy_blocks[ntfy_idx];

	netif_napi_del_locked(&block->napi);
}
