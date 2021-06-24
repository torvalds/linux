// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2015-2021 Google, Inc.
 */

#include "gve.h"
#include "gve_adminq.h"
#include "gve_utils.h"

void gve_tx_remove_from_block(struct gve_priv *priv, int queue_idx)
{
	struct gve_notify_block *block =
			&priv->ntfy_blocks[gve_tx_idx_to_ntfy(priv, queue_idx)];

	block->tx = NULL;
}

void gve_tx_add_to_block(struct gve_priv *priv, int queue_idx)
{
	int ntfy_idx = gve_tx_idx_to_ntfy(priv, queue_idx);
	struct gve_notify_block *block = &priv->ntfy_blocks[ntfy_idx];
	struct gve_tx_ring *tx = &priv->tx[queue_idx];

	block->tx = tx;
	tx->ntfy_id = ntfy_idx;
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

struct sk_buff *gve_rx_copy(struct net_device *dev, struct napi_struct *napi,
			    struct gve_rx_slot_page_info *page_info, u16 len,
			    u16 pad)
{
	struct sk_buff *skb = napi_alloc_skb(napi, len);
	void *va = page_info->page_address + pad +
		   (page_info->page_offset ? PAGE_SIZE / 2 : 0);

	if (unlikely(!skb))
		return NULL;

	__skb_put(skb, len);

	skb_copy_to_linear_data(skb, va, len);

	skb->protocol = eth_type_trans(skb, dev);

	return skb;
}

