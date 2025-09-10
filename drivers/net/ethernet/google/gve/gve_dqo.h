/* SPDX-License-Identifier: (GPL-2.0 OR MIT)
 * Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2015-2021 Google, Inc.
 */

#ifndef _GVE_DQO_H_
#define _GVE_DQO_H_

#include "gve_adminq.h"

#define GVE_ITR_ENABLE_BIT_DQO BIT(0)
#define GVE_ITR_CLEAR_PBA_BIT_DQO BIT(1)
#define GVE_ITR_NO_UPDATE_DQO (3 << 3)

#define GVE_ITR_INTERVAL_DQO_SHIFT 5
#define GVE_ITR_INTERVAL_DQO_MASK ((1 << 12) - 1)

#define GVE_TX_IRQ_RATELIMIT_US_DQO 50
#define GVE_RX_IRQ_RATELIMIT_US_DQO 20
#define GVE_MAX_ITR_INTERVAL_DQO (GVE_ITR_INTERVAL_DQO_MASK * 2)

/* Timeout in seconds to wait for a reinjection completion after receiving
 * its corresponding miss completion.
 */
#define GVE_REINJECT_COMPL_TIMEOUT 1

/* Timeout in seconds to deallocate the completion tag for a packet that was
 * prematurely freed for not receiving a valid completion. This should be large
 * enough to rule out the possibility of receiving the corresponding valid
 * completion after this interval.
 */
#define GVE_DEALLOCATE_COMPL_TIMEOUT 60

netdev_tx_t gve_tx_dqo(struct sk_buff *skb, struct net_device *dev);
netdev_features_t gve_features_check_dqo(struct sk_buff *skb,
					 struct net_device *dev,
					 netdev_features_t features);
bool gve_tx_poll_dqo(struct gve_notify_block *block, bool do_clean);
bool gve_xdp_poll_dqo(struct gve_notify_block *block);
bool gve_xsk_tx_poll_dqo(struct gve_notify_block *block, int budget);
int gve_rx_poll_dqo(struct gve_notify_block *block, int budget);
int gve_tx_alloc_rings_dqo(struct gve_priv *priv,
			   struct gve_tx_alloc_rings_cfg *cfg);
void gve_tx_free_rings_dqo(struct gve_priv *priv,
			   struct gve_tx_alloc_rings_cfg *cfg);
void gve_tx_start_ring_dqo(struct gve_priv *priv, int idx);
void gve_tx_stop_ring_dqo(struct gve_priv *priv, int idx);
int gve_rx_alloc_ring_dqo(struct gve_priv *priv,
			  struct gve_rx_alloc_rings_cfg *cfg,
			  struct gve_rx_ring *rx,
			  int idx);
void gve_rx_free_ring_dqo(struct gve_priv *priv, struct gve_rx_ring *rx,
			  struct gve_rx_alloc_rings_cfg *cfg);
int gve_rx_alloc_rings_dqo(struct gve_priv *priv,
			   struct gve_rx_alloc_rings_cfg *cfg);
void gve_rx_free_rings_dqo(struct gve_priv *priv,
			   struct gve_rx_alloc_rings_cfg *cfg);
void gve_rx_start_ring_dqo(struct gve_priv *priv, int idx);
void gve_rx_stop_ring_dqo(struct gve_priv *priv, int idx);
int gve_clean_tx_done_dqo(struct gve_priv *priv, struct gve_tx_ring *tx,
			  struct napi_struct *napi);
void gve_rx_post_buffers_dqo(struct gve_rx_ring *rx);
void gve_rx_write_doorbell_dqo(const struct gve_priv *priv, int queue_idx);
void gve_xdp_tx_flush_dqo(struct gve_priv *priv, u32 xdp_qid);

static inline void
gve_tx_put_doorbell_dqo(const struct gve_priv *priv,
			const struct gve_queue_resources *q_resources, u32 val)
{
	u64 index;

	index = be32_to_cpu(q_resources->db_index);
	iowrite32(val, &priv->db_bar2[index]);
}

/* Builds register value to write to DQO IRQ doorbell to enable with specified
 * ITR interval.
 */
static inline u32 gve_setup_itr_interval_dqo(u32 interval_us)
{
	u32 result = GVE_ITR_ENABLE_BIT_DQO;

	/* Interval has 2us granularity. */
	interval_us >>= 1;

	interval_us &= GVE_ITR_INTERVAL_DQO_MASK;
	result |= (interval_us << GVE_ITR_INTERVAL_DQO_SHIFT);

	return result;
}

static inline void
gve_write_irq_doorbell_dqo(const struct gve_priv *priv,
			   const struct gve_notify_block *block, u32 val)
{
	u32 index = be32_to_cpu(*block->irq_db_index);

	iowrite32(val, &priv->db_bar2[index]);
}

/* Sets interrupt throttling interval and enables interrupt
 * by writing to IRQ doorbell.
 */
static inline void
gve_set_itr_coalesce_usecs_dqo(struct gve_priv *priv,
			       struct gve_notify_block *block,
			       u32 usecs)
{
	gve_write_irq_doorbell_dqo(priv, block,
				   gve_setup_itr_interval_dqo(usecs));
}

int gve_napi_poll_dqo(struct napi_struct *napi, int budget);
#endif /* _GVE_DQO_H_ */
