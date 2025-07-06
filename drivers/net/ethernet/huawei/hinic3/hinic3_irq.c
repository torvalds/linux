// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/netdevice.h>

#include "hinic3_hw_comm.h"
#include "hinic3_hwdev.h"
#include "hinic3_hwif.h"
#include "hinic3_nic_dev.h"
#include "hinic3_rx.h"
#include "hinic3_tx.h"

static int hinic3_poll(struct napi_struct *napi, int budget)
{
	struct hinic3_irq_cfg *irq_cfg =
		container_of(napi, struct hinic3_irq_cfg, napi);
	struct hinic3_nic_dev *nic_dev;
	bool busy = false;
	int work_done;

	nic_dev = netdev_priv(irq_cfg->netdev);

	busy |= hinic3_tx_poll(irq_cfg->txq, budget);

	if (unlikely(!budget))
		return 0;

	work_done = hinic3_rx_poll(irq_cfg->rxq, budget);
	busy |= work_done >= budget;

	if (busy)
		return budget;

	if (likely(napi_complete_done(napi, work_done)))
		hinic3_set_msix_state(nic_dev->hwdev, irq_cfg->msix_entry_idx,
				      HINIC3_MSIX_ENABLE);

	return work_done;
}

void qp_add_napi(struct hinic3_irq_cfg *irq_cfg)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(irq_cfg->netdev);

	netif_queue_set_napi(irq_cfg->netdev, irq_cfg->irq_id,
			     NETDEV_QUEUE_TYPE_RX, &irq_cfg->napi);
	netif_queue_set_napi(irq_cfg->netdev, irq_cfg->irq_id,
			     NETDEV_QUEUE_TYPE_TX, &irq_cfg->napi);
	netif_napi_add(nic_dev->netdev, &irq_cfg->napi, hinic3_poll);
	napi_enable(&irq_cfg->napi);
}

void qp_del_napi(struct hinic3_irq_cfg *irq_cfg)
{
	napi_disable(&irq_cfg->napi);
	netif_queue_set_napi(irq_cfg->netdev, irq_cfg->irq_id,
			     NETDEV_QUEUE_TYPE_RX, NULL);
	netif_queue_set_napi(irq_cfg->netdev, irq_cfg->irq_id,
			     NETDEV_QUEUE_TYPE_TX, NULL);
	netif_stop_subqueue(irq_cfg->netdev, irq_cfg->irq_id);
	netif_napi_del(&irq_cfg->napi);
}
