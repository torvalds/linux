/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * DebugFS code for ST-Ericsson CW1200 mac80211 driver
 *
 * Copyright (c) 2011, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@lockless.no>
 */

#ifndef CW1200_DEBUG_H_INCLUDED
#define CW1200_DEBUG_H_INCLUDED

struct cw1200_debug_priv {
	struct dentry *debugfs_phy;
	int tx;
	int tx_agg;
	int rx;
	int rx_agg;
	int tx_multi;
	int tx_multi_frames;
	int tx_cache_miss;
	int tx_align;
	int tx_ttl;
	int tx_burst;
	int ba_cnt;
	int ba_acc;
	int ba_cnt_rx;
	int ba_acc_rx;
};

int cw1200_debug_init(struct cw1200_common *priv);
void cw1200_debug_release(struct cw1200_common *priv);

static inline void cw1200_debug_txed(struct cw1200_common *priv)
{
	++priv->debug->tx;
}

static inline void cw1200_debug_txed_agg(struct cw1200_common *priv)
{
	++priv->debug->tx_agg;
}

static inline void cw1200_debug_txed_multi(struct cw1200_common *priv,
					   int count)
{
	++priv->debug->tx_multi;
	priv->debug->tx_multi_frames += count;
}

static inline void cw1200_debug_rxed(struct cw1200_common *priv)
{
	++priv->debug->rx;
}

static inline void cw1200_debug_rxed_agg(struct cw1200_common *priv)
{
	++priv->debug->rx_agg;
}

static inline void cw1200_debug_tx_cache_miss(struct cw1200_common *priv)
{
	++priv->debug->tx_cache_miss;
}

static inline void cw1200_debug_tx_align(struct cw1200_common *priv)
{
	++priv->debug->tx_align;
}

static inline void cw1200_debug_tx_ttl(struct cw1200_common *priv)
{
	++priv->debug->tx_ttl;
}

static inline void cw1200_debug_tx_burst(struct cw1200_common *priv)
{
	++priv->debug->tx_burst;
}

static inline void cw1200_debug_ba(struct cw1200_common *priv,
				   int ba_cnt, int ba_acc,
				   int ba_cnt_rx, int ba_acc_rx)
{
	priv->debug->ba_cnt = ba_cnt;
	priv->debug->ba_acc = ba_acc;
	priv->debug->ba_cnt_rx = ba_cnt_rx;
	priv->debug->ba_acc_rx = ba_acc_rx;
}

#endif /* CW1200_DEBUG_H_INCLUDED */
