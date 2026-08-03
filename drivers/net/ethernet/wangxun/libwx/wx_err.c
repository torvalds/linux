// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2026 Beijing WangXun Technology Co., Ltd. */
/* Copyright (c) 1999 - 2026 Intel Corporation. */

#include <linux/netdevice.h>
#include <linux/pci.h>

#include "wx_type.h"
#include "wx_lib.h"
#include "wx_err.h"

static void wx_pf_reset_subtask(struct wx *wx)
{
	if (!test_and_clear_bit(WX_FLAG_NEED_DO_RESET, wx->flags))
		return;

	wx_warn(wx, "Reset adapter.\n");
	if (wx->do_reset)
		wx->do_reset(wx->netdev, true);
}

static void wx_reset_task(struct work_struct *work)
{
	struct wx *wx = container_of(work, struct wx, reset_task);

	rtnl_lock();

	if (test_bit(WX_STATE_DOWN, wx->state) ||
	    test_bit(WX_STATE_RESETTING, wx->state))
		goto out;

	wx_pf_reset_subtask(wx);

out:
	rtnl_unlock();
}

void wx_check_err_subtask(struct wx *wx)
{
	if (test_bit(WX_FLAG_NEED_DO_RESET, wx->flags))
		queue_work(wx->reset_wq, &wx->reset_task);
}
EXPORT_SYMBOL(wx_check_err_subtask);

int wx_init_err_task(struct wx *wx)
{
	wx->reset_wq = alloc_workqueue("%s_reset_wq_%x", WQ_UNBOUND | WQ_HIGHPRI,
				       1, wx->driver_name, pci_dev_id(wx->pdev));
	if (!wx->reset_wq) {
		wx_err(wx, "Failed to create wx_reset_wq workqueue\n");
		return -ENOMEM;
	}

	INIT_WORK(&wx->reset_task, wx_reset_task);
	return 0;
}
EXPORT_SYMBOL(wx_init_err_task);

static bool wx_ring_tx_pending(struct wx *wx)
{
	int i;

	for (i = 0; i < wx->num_tx_queues; i++) {
		struct wx_ring *tx_ring = wx->tx_ring[i];

		if (tx_ring->next_to_use != tx_ring->next_to_clean)
			return true;
	}

	return false;
}

static bool wx_vf_tx_pending(struct wx *wx)
{
	struct wx_ring_feature *vmdq = &wx->ring_feature[RING_F_VMDQ];
	u32 q_per_pool = __ALIGN_MASK(1, ~vmdq->mask);
	u32 i, j;

	if (!wx->num_vfs)
		return false;

	for (i = 0; i < wx->num_vfs; i++) {
		for (j = 0; j < q_per_pool; j++) {
			u32 h, t;

			h = rd32(wx, WX_PX_TR_RP_PV(q_per_pool, i, j));
			t = rd32(wx, WX_PX_TR_WP_PV(q_per_pool, i, j));

			if (h != t)
				return true;
		}
	}

	return false;
}

static void wx_watchdog_flush_tx(struct wx *wx)
{
	if (!netif_running(wx->netdev))
		return;
	if (netif_carrier_ok(wx->netdev))
		return;

	if (wx_ring_tx_pending(wx) || wx_vf_tx_pending(wx)) {
		/* We've lost link, so the controller stops DMA,
		 * but we've got queued Tx work that's never going
		 * to get done, so reset controller to flush Tx.
		 * (Do the reset outside of interrupt context).
		 */
		wx_warn(wx, "initiating reset due to lost link with pending Tx work\n");
		set_bit(WX_FLAG_NEED_DO_RESET, wx->flags);
	}
}

static void wx_detect_tx_hang(struct wx *wx)
{
	int i;

	/* If we're down or resetting, just bail */
	if (!netif_running(wx->netdev) ||
	    test_bit(WX_STATE_RESETTING, wx->state))
		return;

	/* Force detection of hung controller */
	if (netif_carrier_ok(wx->netdev)) {
		for (i = 0; i < wx->num_tx_queues; i++)
			set_bit(WX_TX_DETECT_HANG, wx->tx_ring[i]->state);
	}
}

void wx_check_hang_subtask(struct wx *wx)
{
	if (test_bit(WX_STATE_DOWN, wx->state) ||
	    test_bit(WX_STATE_RESETTING, wx->state))
		return;

	wx_watchdog_flush_tx(wx);
	wx_detect_tx_hang(wx);
}
EXPORT_SYMBOL(wx_check_hang_subtask);

static void wx_tx_timeout_reset(struct wx *wx)
{
	if (test_bit(WX_STATE_DOWN, wx->state))
		return;

	set_bit(WX_FLAG_NEED_DO_RESET, wx->flags);
	wx_warn(wx, "initiating reset due to tx timeout\n");
	wx_service_event_schedule(wx);
}

void wx_tx_timeout(struct net_device *netdev, unsigned int __always_unused txqueue)
{
	struct wx *wx = netdev_priv(netdev);

	wx_tx_timeout_reset(wx);
}
EXPORT_SYMBOL(wx_tx_timeout);

void wx_handle_tx_hang(struct wx_ring *tx_ring, unsigned int next)
{
	struct wx *wx = netdev_priv(tx_ring->netdev);

	wx_warn(wx,
		"Detected Tx Unit Hang: Queue %d, TDH %x, TDT %x, ntu %x, ntc %x, ntc.time_stamp %lx, jiffies %lx\n",
		tx_ring->queue_index,
		rd32(wx, WX_PX_TR_RP(tx_ring->reg_idx)),
		rd32(wx, WX_PX_TR_WP(tx_ring->reg_idx)),
		tx_ring->next_to_use, next,
		tx_ring->tx_buffer_info[next].time_stamp, jiffies);

	netif_stop_subqueue(tx_ring->netdev, tx_ring->queue_index);

	wx_tx_timeout_reset(wx);
}
