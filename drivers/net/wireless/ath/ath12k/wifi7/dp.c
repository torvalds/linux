// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include "../core.h"
#include "../debug.h"
#include "../dp_rx.h"
#include "../dp_tx.h"
#include "hal_desc.h"
#include "../dp_mon.h"
#include "dp_mon.h"
#include "../dp_cmn.h"
#include "dp_rx.h"
#include "dp.h"
#include "dp_tx.h"
#include "hal.h"

static int ath12k_wifi7_dp_service_srng(struct ath12k_dp *dp,
					struct ath12k_ext_irq_grp *irq_grp,
					int budget)
{
	struct napi_struct *napi = &irq_grp->napi;
	int grp_id = irq_grp->grp_id;
	int work_done = 0;
	int i = 0, j;
	int tot_work_done = 0;
	enum dp_monitor_mode monitor_mode;
	u8 ring_mask;

	if (dp->hw_params->ring_mask->tx[grp_id]) {
		i = fls(dp->hw_params->ring_mask->tx[grp_id]) - 1;
		ath12k_wifi7_dp_tx_completion_handler(dp, i);
	}

	if (dp->hw_params->ring_mask->rx_err[grp_id]) {
		work_done = ath12k_wifi7_dp_rx_process_err(dp, napi, budget);
		budget -= work_done;
		tot_work_done += work_done;
		if (budget <= 0)
			goto done;
	}

	if (dp->hw_params->ring_mask->rx_wbm_rel[grp_id]) {
		work_done = ath12k_wifi7_dp_rx_process_wbm_err(dp, napi, budget);
		budget -= work_done;
		tot_work_done += work_done;

		if (budget <= 0)
			goto done;
	}

	if (dp->hw_params->ring_mask->rx[grp_id]) {
		i = fls(dp->hw_params->ring_mask->rx[grp_id]) - 1;
		work_done = ath12k_wifi7_dp_rx_process(dp, i, napi, budget);
		budget -= work_done;
		tot_work_done += work_done;
		if (budget <= 0)
			goto done;
	}

	if (dp->hw_params->ring_mask->rx_mon_status[grp_id]) {
		ring_mask = dp->hw_params->ring_mask->rx_mon_status[grp_id];
		for (i = 0; i < dp->ab->num_radios; i++) {
			for (j = 0; j < dp->hw_params->num_rxdma_per_pdev; j++) {
				int id = i * dp->hw_params->num_rxdma_per_pdev + j;

				if (ring_mask & BIT(id)) {
					work_done =
					ath12k_wifi7_dp_mon_process_ring(dp, id, napi,
									 budget,
									 0);
					budget -= work_done;
					tot_work_done += work_done;
					if (budget <= 0)
						goto done;
				}
			}
		}
	}

	if (dp->hw_params->ring_mask->rx_mon_dest[grp_id]) {
		monitor_mode = ATH12K_DP_RX_MONITOR_MODE;
		ring_mask = dp->hw_params->ring_mask->rx_mon_dest[grp_id];
		for (i = 0; i < dp->ab->num_radios; i++) {
			for (j = 0; j < dp->hw_params->num_rxdma_per_pdev; j++) {
				int id = i * dp->hw_params->num_rxdma_per_pdev + j;

				if (ring_mask & BIT(id)) {
					work_done =
					ath12k_wifi7_dp_mon_process_ring(dp, id, napi,
									 budget,
									 monitor_mode);
					budget -= work_done;
					tot_work_done += work_done;

					if (budget <= 0)
						goto done;
				}
			}
		}
	}

	if (dp->hw_params->ring_mask->tx_mon_dest[grp_id]) {
		monitor_mode = ATH12K_DP_TX_MONITOR_MODE;
		ring_mask = dp->hw_params->ring_mask->tx_mon_dest[grp_id];
		for (i = 0; i < dp->ab->num_radios; i++) {
			for (j = 0; j < dp->hw_params->num_rxdma_per_pdev; j++) {
				int id = i * dp->hw_params->num_rxdma_per_pdev + j;

				if (ring_mask & BIT(id)) {
					work_done =
					ath12k_wifi7_dp_mon_process_ring(dp, id,
									 napi, budget,
									 monitor_mode);
					budget -= work_done;
					tot_work_done += work_done;

					if (budget <= 0)
						goto done;
				}
			}
		}
	}

	if (dp->hw_params->ring_mask->reo_status[grp_id])
		ath12k_wifi7_dp_rx_process_reo_status(dp);

	if (dp->hw_params->ring_mask->host2rxdma[grp_id]) {
		struct dp_rxdma_ring *rx_ring = &dp->rx_refill_buf_ring;
		LIST_HEAD(list);

		ath12k_dp_rx_bufs_replenish(dp, rx_ring, &list, 0);
	}

	/* TODO: Implement handler for other interrupts */

done:
	return tot_work_done;
}

static struct ath12k_dp_arch_ops ath12k_wifi7_dp_arch_ops = {
	.service_srng = ath12k_wifi7_dp_service_srng,
	.tx_get_vdev_bank_config = ath12k_wifi7_dp_tx_get_vdev_bank_config,
	.reo_cmd_send = ath12k_wifi7_dp_reo_cmd_send,
	.setup_pn_check_reo_cmd = ath12k_wifi7_dp_setup_pn_check_reo_cmd,
	.rx_peer_tid_delete = ath12k_wifi7_dp_rx_peer_tid_delete,
	.reo_cache_flush = ath12k_wifi7_dp_reo_cache_flush,
	.rx_link_desc_return = ath12k_wifi7_dp_rx_link_desc_return,
	.rx_frags_cleanup = ath12k_wifi7_dp_rx_frags_cleanup,
	.peer_rx_tid_reo_update = ath12k_wifi7_peer_rx_tid_reo_update,
	.rx_assign_reoq = ath12k_wifi7_dp_rx_assign_reoq,
	.peer_rx_tid_qref_setup = ath12k_wifi7_peer_rx_tid_qref_setup,
	.peer_rx_tid_qref_reset = ath12k_wifi7_peer_rx_tid_qref_reset,
	.rx_tid_delete_handler = ath12k_wifi7_dp_rx_tid_delete_handler,
};

/* TODO: remove export once this file is built with wifi7 ko */
struct ath12k_dp *ath12k_wifi7_dp_device_alloc(struct ath12k_base *ab)
{
	struct ath12k_dp *dp;

	/* TODO: align dp later if cache alignment becomes a bottleneck */
	dp = kzalloc(sizeof(*dp), GFP_KERNEL);
	if (!dp)
		return NULL;

	dp->ab = ab;
	dp->dev = ab->dev;
	dp->hw_params = ab->hw_params;
	dp->hal = &ab->hal;

	dp->ops = &ath12k_wifi7_dp_arch_ops;

	return dp;
}

void ath12k_wifi7_dp_device_free(struct ath12k_dp *dp)
{
	kfree(dp);
}
