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

#define GVE_TX_IRQ_RATELIMIT_US_DQO 50
#define GVE_RX_IRQ_RATELIMIT_US_DQO 20

netdev_tx_t gve_tx_dqo(struct sk_buff *skb, struct net_device *dev);
bool gve_tx_poll_dqo(struct gve_notify_block *block, bool do_clean);
int gve_rx_poll_dqo(struct gve_notify_block *block, int budget);

static inline void
gve_write_irq_doorbell_dqo(const struct gve_priv *priv,
			   const struct gve_notify_block *block, u32 val)
{
	u32 index = be32_to_cpu(block->irq_db_index);

	iowrite32(val, &priv->db_bar2[index]);
}

#endif /* _GVE_DQO_H_ */
