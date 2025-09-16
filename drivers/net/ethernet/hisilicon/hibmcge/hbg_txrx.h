/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2024 Hisilicon Limited. */

#ifndef __HBG_TXRX_H
#define __HBG_TXRX_H

#include <linux/etherdevice.h>
#include "hbg_hw.h"

static inline u32 hbg_spec_max_frame_len(struct hbg_priv *priv,
					 enum hbg_dir dir)
{
	return (dir == HBG_DIR_TX) ? priv->dev_specs.max_frame_len :
		priv->dev_specs.rx_buf_size;
}

static inline u32 hbg_get_spec_fifo_max_num(struct hbg_priv *priv,
					    enum hbg_dir dir)
{
	return (dir == HBG_DIR_TX) ? priv->dev_specs.tx_fifo_num :
		priv->dev_specs.rx_fifo_num;
}

static inline bool hbg_fifo_is_full(struct hbg_priv *priv, enum hbg_dir dir)
{
	return hbg_hw_get_fifo_used_num(priv, dir) >=
	       hbg_get_spec_fifo_max_num(priv, dir);
}

static inline u32 hbg_get_queue_used_num(struct hbg_ring *ring)
{
	u32 len = READ_ONCE(ring->len);

	if (!len)
		return 0;

	return (READ_ONCE(ring->ntu) + len - READ_ONCE(ring->ntc)) % len;
}

netdev_tx_t hbg_net_start_xmit(struct sk_buff *skb, struct net_device *netdev);
int hbg_txrx_init(struct hbg_priv *priv);
void hbg_txrx_uninit(struct hbg_priv *priv);

#endif
