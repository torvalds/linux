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

static void qp_add_napi(struct hinic3_irq_cfg *irq_cfg)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(irq_cfg->netdev);

	netif_napi_add(nic_dev->netdev, &irq_cfg->napi, hinic3_poll);
	netif_queue_set_napi(irq_cfg->netdev, irq_cfg->irq_id,
			     NETDEV_QUEUE_TYPE_RX, &irq_cfg->napi);
	netif_queue_set_napi(irq_cfg->netdev, irq_cfg->irq_id,
			     NETDEV_QUEUE_TYPE_TX, &irq_cfg->napi);
	napi_enable(&irq_cfg->napi);
}

static void qp_del_napi(struct hinic3_irq_cfg *irq_cfg)
{
	napi_disable(&irq_cfg->napi);
	netif_queue_set_napi(irq_cfg->netdev, irq_cfg->irq_id,
			     NETDEV_QUEUE_TYPE_RX, NULL);
	netif_queue_set_napi(irq_cfg->netdev, irq_cfg->irq_id,
			     NETDEV_QUEUE_TYPE_TX, NULL);
	netif_stop_subqueue(irq_cfg->netdev, irq_cfg->irq_id);
	netif_napi_del(&irq_cfg->napi);
}

static irqreturn_t qp_irq(int irq, void *data)
{
	struct hinic3_irq_cfg *irq_cfg = data;
	struct hinic3_nic_dev *nic_dev;

	nic_dev = netdev_priv(irq_cfg->netdev);
	hinic3_msix_intr_clear_resend_bit(nic_dev->hwdev,
					  irq_cfg->msix_entry_idx, 1);

	napi_schedule(&irq_cfg->napi);

	return IRQ_HANDLED;
}

static int hinic3_request_irq(struct hinic3_irq_cfg *irq_cfg, u16 q_id)
{
	struct hinic3_interrupt_info info = {};
	struct hinic3_nic_dev *nic_dev;
	struct net_device *netdev;
	int err;

	netdev = irq_cfg->netdev;
	nic_dev = netdev_priv(netdev);
	qp_add_napi(irq_cfg);

	info.msix_index = irq_cfg->msix_entry_idx;
	info.interrupt_coalesc_set = 1;
	info.pending_limit = nic_dev->intr_coalesce[q_id].pending_limit;
	info.coalesc_timer_cfg =
		nic_dev->intr_coalesce[q_id].coalesce_timer_cfg;
	info.resend_timer_cfg = nic_dev->intr_coalesce[q_id].resend_timer_cfg;
	err = hinic3_set_interrupt_cfg_direct(nic_dev->hwdev, &info);
	if (err) {
		netdev_err(netdev, "Failed to set RX interrupt coalescing attribute.\n");
		qp_del_napi(irq_cfg);
		return err;
	}

	err = request_irq(irq_cfg->irq_id, qp_irq, 0, irq_cfg->irq_name,
			  irq_cfg);
	if (err) {
		qp_del_napi(irq_cfg);
		return err;
	}

	irq_set_affinity_hint(irq_cfg->irq_id, &irq_cfg->affinity_mask);

	return 0;
}

static void hinic3_release_irq(struct hinic3_irq_cfg *irq_cfg)
{
	irq_set_affinity_hint(irq_cfg->irq_id, NULL);
	free_irq(irq_cfg->irq_id, irq_cfg);
}

int hinic3_qps_irq_init(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	struct pci_dev *pdev = nic_dev->pdev;
	struct hinic3_irq_cfg *irq_cfg;
	struct msix_entry *msix_entry;
	u32 local_cpu;
	u16 q_id;
	int err;

	for (q_id = 0; q_id < nic_dev->q_params.num_qps; q_id++) {
		msix_entry = &nic_dev->qps_msix_entries[q_id];
		irq_cfg = &nic_dev->q_params.irq_cfg[q_id];

		irq_cfg->irq_id = msix_entry->vector;
		irq_cfg->msix_entry_idx = msix_entry->entry;
		irq_cfg->netdev = netdev;
		irq_cfg->txq = &nic_dev->txqs[q_id];
		irq_cfg->rxq = &nic_dev->rxqs[q_id];
		nic_dev->rxqs[q_id].irq_cfg = irq_cfg;

		local_cpu = cpumask_local_spread(q_id, dev_to_node(&pdev->dev));
		cpumask_set_cpu(local_cpu, &irq_cfg->affinity_mask);

		snprintf(irq_cfg->irq_name, sizeof(irq_cfg->irq_name),
			 "%s_qp%u", netdev->name, q_id);

		err = hinic3_request_irq(irq_cfg, q_id);
		if (err) {
			netdev_err(netdev, "Failed to request Rx irq\n");
			goto err_release_irqs;
		}

		hinic3_set_msix_auto_mask_state(nic_dev->hwdev,
						irq_cfg->msix_entry_idx,
						HINIC3_SET_MSIX_AUTO_MASK);
		hinic3_set_msix_state(nic_dev->hwdev, irq_cfg->msix_entry_idx,
				      HINIC3_MSIX_ENABLE);
	}

	return 0;

err_release_irqs:
	while (q_id > 0) {
		q_id--;
		irq_cfg = &nic_dev->q_params.irq_cfg[q_id];
		qp_del_napi(irq_cfg);
		hinic3_set_msix_state(nic_dev->hwdev, irq_cfg->msix_entry_idx,
				      HINIC3_MSIX_DISABLE);
		hinic3_set_msix_auto_mask_state(nic_dev->hwdev,
						irq_cfg->msix_entry_idx,
						HINIC3_CLR_MSIX_AUTO_MASK);
		hinic3_release_irq(irq_cfg);
	}

	return err;
}

void hinic3_qps_irq_uninit(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic3_irq_cfg *irq_cfg;
	u16 q_id;

	for (q_id = 0; q_id < nic_dev->q_params.num_qps; q_id++) {
		irq_cfg = &nic_dev->q_params.irq_cfg[q_id];
		qp_del_napi(irq_cfg);
		hinic3_set_msix_state(nic_dev->hwdev, irq_cfg->msix_entry_idx,
				      HINIC3_MSIX_DISABLE);
		hinic3_set_msix_auto_mask_state(nic_dev->hwdev,
						irq_cfg->msix_entry_idx,
						HINIC3_CLR_MSIX_AUTO_MASK);
		hinic3_release_irq(irq_cfg);
	}
}
