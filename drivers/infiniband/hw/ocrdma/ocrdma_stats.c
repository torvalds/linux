/* This file is part of the Emulex RoCE Device Driver for
 * RoCE (RDMA over Converged Ethernet) adapters.
 * Copyright (C) 2012-2015 Emulex. All rights reserved.
 * EMULEX and SLI are trademarks of Emulex.
 * www.emulex.com
 *
 * This software is available to you under a choice of one of two licenses.
 * You may choose to be licensed under the terms of the GNU General Public
 * License (GPL) Version 2, available from the file COPYING in the main
 * directory of this source tree, or the BSD license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Contact Information:
 * linux-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

#include <rdma/ib_addr.h>
#include <rdma/ib_pma.h>
#include "ocrdma_stats.h"

static struct dentry *ocrdma_dbgfs_dir;

static int ocrdma_add_stat(char *start, char *pcur,
				char *name, u64 count)
{
	char buff[128] = {0};
	int cpy_len = 0;

	snprintf(buff, 128, "%s: %llu\n", name, count);
	cpy_len = strlen(buff);

	if (pcur + cpy_len > start + OCRDMA_MAX_DBGFS_MEM) {
		pr_err("%s: No space in stats buff\n", __func__);
		return 0;
	}

	memcpy(pcur, buff, cpy_len);
	return cpy_len;
}

bool ocrdma_alloc_stats_resources(struct ocrdma_dev *dev)
{
	struct stats_mem *mem = &dev->stats_mem;

	mutex_init(&dev->stats_lock);
	/* Alloc mbox command mem*/
	mem->size = max_t(u32, sizeof(struct ocrdma_rdma_stats_req),
			sizeof(struct ocrdma_rdma_stats_resp));

	mem->va = dma_alloc_coherent(&dev->nic_info.pdev->dev, mem->size,
				     &mem->pa, GFP_KERNEL);
	if (!mem->va) {
		pr_err("%s: stats mbox allocation failed\n", __func__);
		return false;
	}

	/* Alloc debugfs mem */
	mem->debugfs_mem = kzalloc(OCRDMA_MAX_DBGFS_MEM, GFP_KERNEL);
	if (!mem->debugfs_mem)
		return false;

	return true;
}

void ocrdma_release_stats_resources(struct ocrdma_dev *dev)
{
	struct stats_mem *mem = &dev->stats_mem;

	if (mem->va)
		dma_free_coherent(&dev->nic_info.pdev->dev, mem->size,
				  mem->va, mem->pa);
	mem->va = NULL;
	kfree(mem->debugfs_mem);
}

static char *ocrdma_resource_stats(struct ocrdma_dev *dev)
{
	char *stats = dev->stats_mem.debugfs_mem, *pcur;
	struct ocrdma_rdma_stats_resp *rdma_stats =
			(struct ocrdma_rdma_stats_resp *)dev->stats_mem.va;
	struct ocrdma_rsrc_stats *rsrc_stats = &rdma_stats->act_rsrc_stats;

	memset(stats, 0, (OCRDMA_MAX_DBGFS_MEM));

	pcur = stats;
	pcur += ocrdma_add_stat(stats, pcur, "active_dpp_pds",
				(u64)rsrc_stats->dpp_pds);
	pcur += ocrdma_add_stat(stats, pcur, "active_non_dpp_pds",
				(u64)rsrc_stats->non_dpp_pds);
	pcur += ocrdma_add_stat(stats, pcur, "active_rc_dpp_qps",
				(u64)rsrc_stats->rc_dpp_qps);
	pcur += ocrdma_add_stat(stats, pcur, "active_uc_dpp_qps",
				(u64)rsrc_stats->uc_dpp_qps);
	pcur += ocrdma_add_stat(stats, pcur, "active_ud_dpp_qps",
				(u64)rsrc_stats->ud_dpp_qps);
	pcur += ocrdma_add_stat(stats, pcur, "active_rc_non_dpp_qps",
				(u64)rsrc_stats->rc_non_dpp_qps);
	pcur += ocrdma_add_stat(stats, pcur, "active_uc_non_dpp_qps",
				(u64)rsrc_stats->uc_non_dpp_qps);
	pcur += ocrdma_add_stat(stats, pcur, "active_ud_non_dpp_qps",
				(u64)rsrc_stats->ud_non_dpp_qps);
	pcur += ocrdma_add_stat(stats, pcur, "active_srqs",
				(u64)rsrc_stats->srqs);
	pcur += ocrdma_add_stat(stats, pcur, "active_rbqs",
				(u64)rsrc_stats->rbqs);
	pcur += ocrdma_add_stat(stats, pcur, "active_64K_nsmr",
				(u64)rsrc_stats->r64K_nsmr);
	pcur += ocrdma_add_stat(stats, pcur, "active_64K_to_2M_nsmr",
				(u64)rsrc_stats->r64K_to_2M_nsmr);
	pcur += ocrdma_add_stat(stats, pcur, "active_2M_to_44M_nsmr",
				(u64)rsrc_stats->r2M_to_44M_nsmr);
	pcur += ocrdma_add_stat(stats, pcur, "active_44M_to_1G_nsmr",
				(u64)rsrc_stats->r44M_to_1G_nsmr);
	pcur += ocrdma_add_stat(stats, pcur, "active_1G_to_4G_nsmr",
				(u64)rsrc_stats->r1G_to_4G_nsmr);
	pcur += ocrdma_add_stat(stats, pcur, "active_nsmr_count_4G_to_32G",
				(u64)rsrc_stats->nsmr_count_4G_to_32G);
	pcur += ocrdma_add_stat(stats, pcur, "active_32G_to_64G_nsmr",
				(u64)rsrc_stats->r32G_to_64G_nsmr);
	pcur += ocrdma_add_stat(stats, pcur, "active_64G_to_128G_nsmr",
				(u64)rsrc_stats->r64G_to_128G_nsmr);
	pcur += ocrdma_add_stat(stats, pcur, "active_128G_to_higher_nsmr",
				(u64)rsrc_stats->r128G_to_higher_nsmr);
	pcur += ocrdma_add_stat(stats, pcur, "active_embedded_nsmr",
				(u64)rsrc_stats->embedded_nsmr);
	pcur += ocrdma_add_stat(stats, pcur, "active_frmr",
				(u64)rsrc_stats->frmr);
	pcur += ocrdma_add_stat(stats, pcur, "active_prefetch_qps",
				(u64)rsrc_stats->prefetch_qps);
	pcur += ocrdma_add_stat(stats, pcur, "active_ondemand_qps",
				(u64)rsrc_stats->ondemand_qps);
	pcur += ocrdma_add_stat(stats, pcur, "active_phy_mr",
				(u64)rsrc_stats->phy_mr);
	pcur += ocrdma_add_stat(stats, pcur, "active_mw",
				(u64)rsrc_stats->mw);

	/* Print the threshold stats */
	rsrc_stats = &rdma_stats->th_rsrc_stats;

	pcur += ocrdma_add_stat(stats, pcur, "threshold_dpp_pds",
				(u64)rsrc_stats->dpp_pds);
	pcur += ocrdma_add_stat(stats, pcur, "threshold_non_dpp_pds",
				(u64)rsrc_stats->non_dpp_pds);
	pcur += ocrdma_add_stat(stats, pcur, "threshold_rc_dpp_qps",
				(u64)rsrc_stats->rc_dpp_qps);
	pcur += ocrdma_add_stat(stats, pcur, "threshold_uc_dpp_qps",
				(u64)rsrc_stats->uc_dpp_qps);
	pcur += ocrdma_add_stat(stats, pcur, "threshold_ud_dpp_qps",
				(u64)rsrc_stats->ud_dpp_qps);
	pcur += ocrdma_add_stat(stats, pcur, "threshold_rc_non_dpp_qps",
				(u64)rsrc_stats->rc_non_dpp_qps);
	pcur += ocrdma_add_stat(stats, pcur, "threshold_uc_non_dpp_qps",
				(u64)rsrc_stats->uc_non_dpp_qps);
	pcur += ocrdma_add_stat(stats, pcur, "threshold_ud_non_dpp_qps",
				(u64)rsrc_stats->ud_non_dpp_qps);
	pcur += ocrdma_add_stat(stats, pcur, "threshold_srqs",
				(u64)rsrc_stats->srqs);
	pcur += ocrdma_add_stat(stats, pcur, "threshold_rbqs",
				(u64)rsrc_stats->rbqs);
	pcur += ocrdma_add_stat(stats, pcur, "threshold_64K_nsmr",
				(u64)rsrc_stats->r64K_nsmr);
	pcur += ocrdma_add_stat(stats, pcur, "threshold_64K_to_2M_nsmr",
				(u64)rsrc_stats->r64K_to_2M_nsmr);
	pcur += ocrdma_add_stat(stats, pcur, "threshold_2M_to_44M_nsmr",
				(u64)rsrc_stats->r2M_to_44M_nsmr);
	pcur += ocrdma_add_stat(stats, pcur, "threshold_44M_to_1G_nsmr",
				(u64)rsrc_stats->r44M_to_1G_nsmr);
	pcur += ocrdma_add_stat(stats, pcur, "threshold_1G_to_4G_nsmr",
				(u64)rsrc_stats->r1G_to_4G_nsmr);
	pcur += ocrdma_add_stat(stats, pcur, "threshold_nsmr_count_4G_to_32G",
				(u64)rsrc_stats->nsmr_count_4G_to_32G);
	pcur += ocrdma_add_stat(stats, pcur, "threshold_32G_to_64G_nsmr",
				(u64)rsrc_stats->r32G_to_64G_nsmr);
	pcur += ocrdma_add_stat(stats, pcur, "threshold_64G_to_128G_nsmr",
				(u64)rsrc_stats->r64G_to_128G_nsmr);
	pcur += ocrdma_add_stat(stats, pcur, "threshold_128G_to_higher_nsmr",
				(u64)rsrc_stats->r128G_to_higher_nsmr);
	pcur += ocrdma_add_stat(stats, pcur, "threshold_embedded_nsmr",
				(u64)rsrc_stats->embedded_nsmr);
	pcur += ocrdma_add_stat(stats, pcur, "threshold_frmr",
				(u64)rsrc_stats->frmr);
	pcur += ocrdma_add_stat(stats, pcur, "threshold_prefetch_qps",
				(u64)rsrc_stats->prefetch_qps);
	pcur += ocrdma_add_stat(stats, pcur, "threshold_ondemand_qps",
				(u64)rsrc_stats->ondemand_qps);
	pcur += ocrdma_add_stat(stats, pcur, "threshold_phy_mr",
				(u64)rsrc_stats->phy_mr);
	pcur += ocrdma_add_stat(stats, pcur, "threshold_mw",
				(u64)rsrc_stats->mw);
	return stats;
}

static char *ocrdma_rx_stats(struct ocrdma_dev *dev)
{
	char *stats = dev->stats_mem.debugfs_mem, *pcur;
	struct ocrdma_rdma_stats_resp *rdma_stats =
		(struct ocrdma_rdma_stats_resp *)dev->stats_mem.va;
	struct ocrdma_rx_stats *rx_stats = &rdma_stats->rx_stats;

	memset(stats, 0, (OCRDMA_MAX_DBGFS_MEM));

	pcur = stats;
	pcur += ocrdma_add_stat
		(stats, pcur, "roce_frame_bytes",
		 convert_to_64bit(rx_stats->roce_frame_bytes_lo,
		 rx_stats->roce_frame_bytes_hi));
	pcur += ocrdma_add_stat(stats, pcur, "roce_frame_icrc_drops",
				(u64)rx_stats->roce_frame_icrc_drops);
	pcur += ocrdma_add_stat(stats, pcur, "roce_frame_payload_len_drops",
				(u64)rx_stats->roce_frame_payload_len_drops);
	pcur += ocrdma_add_stat(stats, pcur, "ud_drops",
				(u64)rx_stats->ud_drops);
	pcur += ocrdma_add_stat(stats, pcur, "qp1_drops",
				(u64)rx_stats->qp1_drops);
	pcur += ocrdma_add_stat(stats, pcur, "psn_error_request_packets",
				(u64)rx_stats->psn_error_request_packets);
	pcur += ocrdma_add_stat(stats, pcur, "psn_error_resp_packets",
				(u64)rx_stats->psn_error_resp_packets);
	pcur += ocrdma_add_stat(stats, pcur, "rnr_nak_timeouts",
				(u64)rx_stats->rnr_nak_timeouts);
	pcur += ocrdma_add_stat(stats, pcur, "rnr_nak_receives",
				(u64)rx_stats->rnr_nak_receives);
	pcur += ocrdma_add_stat(stats, pcur, "roce_frame_rxmt_drops",
				(u64)rx_stats->roce_frame_rxmt_drops);
	pcur += ocrdma_add_stat(stats, pcur, "nak_count_psn_sequence_errors",
				(u64)rx_stats->nak_count_psn_sequence_errors);
	pcur += ocrdma_add_stat(stats, pcur, "rc_drop_count_lookup_errors",
				(u64)rx_stats->rc_drop_count_lookup_errors);
	pcur += ocrdma_add_stat(stats, pcur, "rq_rnr_naks",
				(u64)rx_stats->rq_rnr_naks);
	pcur += ocrdma_add_stat(stats, pcur, "srq_rnr_naks",
				(u64)rx_stats->srq_rnr_naks);
	pcur += ocrdma_add_stat(stats, pcur, "roce_frames",
				convert_to_64bit(rx_stats->roce_frames_lo,
						 rx_stats->roce_frames_hi));

	return stats;
}

static u64 ocrdma_sysfs_rcv_pkts(struct ocrdma_dev *dev)
{
	struct ocrdma_rdma_stats_resp *rdma_stats =
		(struct ocrdma_rdma_stats_resp *)dev->stats_mem.va;
	struct ocrdma_rx_stats *rx_stats = &rdma_stats->rx_stats;

	return convert_to_64bit(rx_stats->roce_frames_lo,
		rx_stats->roce_frames_hi) + (u64)rx_stats->roce_frame_icrc_drops
		+ (u64)rx_stats->roce_frame_payload_len_drops;
}

static u64 ocrdma_sysfs_rcv_data(struct ocrdma_dev *dev)
{
	struct ocrdma_rdma_stats_resp *rdma_stats =
		(struct ocrdma_rdma_stats_resp *)dev->stats_mem.va;
	struct ocrdma_rx_stats *rx_stats = &rdma_stats->rx_stats;

	return (convert_to_64bit(rx_stats->roce_frame_bytes_lo,
		rx_stats->roce_frame_bytes_hi))/4;
}

static char *ocrdma_tx_stats(struct ocrdma_dev *dev)
{
	char *stats = dev->stats_mem.debugfs_mem, *pcur;
	struct ocrdma_rdma_stats_resp *rdma_stats =
		(struct ocrdma_rdma_stats_resp *)dev->stats_mem.va;
	struct ocrdma_tx_stats *tx_stats = &rdma_stats->tx_stats;

	memset(stats, 0, (OCRDMA_MAX_DBGFS_MEM));

	pcur = stats;
	pcur += ocrdma_add_stat(stats, pcur, "send_pkts",
				convert_to_64bit(tx_stats->send_pkts_lo,
						 tx_stats->send_pkts_hi));
	pcur += ocrdma_add_stat(stats, pcur, "write_pkts",
				convert_to_64bit(tx_stats->write_pkts_lo,
						 tx_stats->write_pkts_hi));
	pcur += ocrdma_add_stat(stats, pcur, "read_pkts",
				convert_to_64bit(tx_stats->read_pkts_lo,
						 tx_stats->read_pkts_hi));
	pcur += ocrdma_add_stat(stats, pcur, "read_rsp_pkts",
				convert_to_64bit(tx_stats->read_rsp_pkts_lo,
						 tx_stats->read_rsp_pkts_hi));
	pcur += ocrdma_add_stat(stats, pcur, "ack_pkts",
				convert_to_64bit(tx_stats->ack_pkts_lo,
						 tx_stats->ack_pkts_hi));
	pcur += ocrdma_add_stat(stats, pcur, "send_bytes",
				convert_to_64bit(tx_stats->send_bytes_lo,
						 tx_stats->send_bytes_hi));
	pcur += ocrdma_add_stat(stats, pcur, "write_bytes",
				convert_to_64bit(tx_stats->write_bytes_lo,
						 tx_stats->write_bytes_hi));
	pcur += ocrdma_add_stat(stats, pcur, "read_req_bytes",
				convert_to_64bit(tx_stats->read_req_bytes_lo,
						 tx_stats->read_req_bytes_hi));
	pcur += ocrdma_add_stat(stats, pcur, "read_rsp_bytes",
				convert_to_64bit(tx_stats->read_rsp_bytes_lo,
						 tx_stats->read_rsp_bytes_hi));
	pcur += ocrdma_add_stat(stats, pcur, "ack_timeouts",
				(u64)tx_stats->ack_timeouts);

	return stats;
}

static u64 ocrdma_sysfs_xmit_pkts(struct ocrdma_dev *dev)
{
	struct ocrdma_rdma_stats_resp *rdma_stats =
		(struct ocrdma_rdma_stats_resp *)dev->stats_mem.va;
	struct ocrdma_tx_stats *tx_stats = &rdma_stats->tx_stats;

	return (convert_to_64bit(tx_stats->send_pkts_lo,
				 tx_stats->send_pkts_hi) +
	convert_to_64bit(tx_stats->write_pkts_lo, tx_stats->write_pkts_hi) +
	convert_to_64bit(tx_stats->read_pkts_lo, tx_stats->read_pkts_hi) +
	convert_to_64bit(tx_stats->read_rsp_pkts_lo,
			 tx_stats->read_rsp_pkts_hi) +
	convert_to_64bit(tx_stats->ack_pkts_lo, tx_stats->ack_pkts_hi));
}

static u64 ocrdma_sysfs_xmit_data(struct ocrdma_dev *dev)
{
	struct ocrdma_rdma_stats_resp *rdma_stats =
		(struct ocrdma_rdma_stats_resp *)dev->stats_mem.va;
	struct ocrdma_tx_stats *tx_stats = &rdma_stats->tx_stats;

	return (convert_to_64bit(tx_stats->send_bytes_lo,
				 tx_stats->send_bytes_hi) +
		convert_to_64bit(tx_stats->write_bytes_lo,
				 tx_stats->write_bytes_hi) +
		convert_to_64bit(tx_stats->read_req_bytes_lo,
				 tx_stats->read_req_bytes_hi) +
		convert_to_64bit(tx_stats->read_rsp_bytes_lo,
				 tx_stats->read_rsp_bytes_hi))/4;
}

static char *ocrdma_wqe_stats(struct ocrdma_dev *dev)
{
	char *stats = dev->stats_mem.debugfs_mem, *pcur;
	struct ocrdma_rdma_stats_resp *rdma_stats =
		(struct ocrdma_rdma_stats_resp *)dev->stats_mem.va;
	struct ocrdma_wqe_stats	*wqe_stats = &rdma_stats->wqe_stats;

	memset(stats, 0, (OCRDMA_MAX_DBGFS_MEM));

	pcur = stats;
	pcur += ocrdma_add_stat(stats, pcur, "large_send_rc_wqes",
		convert_to_64bit(wqe_stats->large_send_rc_wqes_lo,
				 wqe_stats->large_send_rc_wqes_hi));
	pcur += ocrdma_add_stat(stats, pcur, "large_write_rc_wqes",
		convert_to_64bit(wqe_stats->large_write_rc_wqes_lo,
				 wqe_stats->large_write_rc_wqes_hi));
	pcur += ocrdma_add_stat(stats, pcur, "read_wqes",
				convert_to_64bit(wqe_stats->read_wqes_lo,
						 wqe_stats->read_wqes_hi));
	pcur += ocrdma_add_stat(stats, pcur, "frmr_wqes",
				convert_to_64bit(wqe_stats->frmr_wqes_lo,
						 wqe_stats->frmr_wqes_hi));
	pcur += ocrdma_add_stat(stats, pcur, "mw_bind_wqes",
				convert_to_64bit(wqe_stats->mw_bind_wqes_lo,
						 wqe_stats->mw_bind_wqes_hi));
	pcur += ocrdma_add_stat(stats, pcur, "invalidate_wqes",
		convert_to_64bit(wqe_stats->invalidate_wqes_lo,
				 wqe_stats->invalidate_wqes_hi));
	pcur += ocrdma_add_stat(stats, pcur, "dpp_wqe_drops",
				(u64)wqe_stats->dpp_wqe_drops);
	return stats;
}

static char *ocrdma_db_errstats(struct ocrdma_dev *dev)
{
	char *stats = dev->stats_mem.debugfs_mem, *pcur;
	struct ocrdma_rdma_stats_resp *rdma_stats =
		(struct ocrdma_rdma_stats_resp *)dev->stats_mem.va;
	struct ocrdma_db_err_stats *db_err_stats = &rdma_stats->db_err_stats;

	memset(stats, 0, (OCRDMA_MAX_DBGFS_MEM));

	pcur = stats;
	pcur += ocrdma_add_stat(stats, pcur, "sq_doorbell_errors",
				(u64)db_err_stats->sq_doorbell_errors);
	pcur += ocrdma_add_stat(stats, pcur, "cq_doorbell_errors",
				(u64)db_err_stats->cq_doorbell_errors);
	pcur += ocrdma_add_stat(stats, pcur, "rq_srq_doorbell_errors",
				(u64)db_err_stats->rq_srq_doorbell_errors);
	pcur += ocrdma_add_stat(stats, pcur, "cq_overflow_errors",
				(u64)db_err_stats->cq_overflow_errors);
	return stats;
}

static char *ocrdma_rxqp_errstats(struct ocrdma_dev *dev)
{
	char *stats = dev->stats_mem.debugfs_mem, *pcur;
	struct ocrdma_rdma_stats_resp *rdma_stats =
		(struct ocrdma_rdma_stats_resp *)dev->stats_mem.va;
	struct ocrdma_rx_qp_err_stats *rx_qp_err_stats =
		 &rdma_stats->rx_qp_err_stats;

	memset(stats, 0, (OCRDMA_MAX_DBGFS_MEM));

	pcur = stats;
	pcur += ocrdma_add_stat(stats, pcur, "nak_invalid_requst_errors",
			(u64)rx_qp_err_stats->nak_invalid_requst_errors);
	pcur += ocrdma_add_stat(stats, pcur, "nak_remote_operation_errors",
			(u64)rx_qp_err_stats->nak_remote_operation_errors);
	pcur += ocrdma_add_stat(stats, pcur, "nak_count_remote_access_errors",
			(u64)rx_qp_err_stats->nak_count_remote_access_errors);
	pcur += ocrdma_add_stat(stats, pcur, "local_length_errors",
			(u64)rx_qp_err_stats->local_length_errors);
	pcur += ocrdma_add_stat(stats, pcur, "local_protection_errors",
			(u64)rx_qp_err_stats->local_protection_errors);
	pcur += ocrdma_add_stat(stats, pcur, "local_qp_operation_errors",
			(u64)rx_qp_err_stats->local_qp_operation_errors);
	return stats;
}

static char *ocrdma_txqp_errstats(struct ocrdma_dev *dev)
{
	char *stats = dev->stats_mem.debugfs_mem, *pcur;
	struct ocrdma_rdma_stats_resp *rdma_stats =
		(struct ocrdma_rdma_stats_resp *)dev->stats_mem.va;
	struct ocrdma_tx_qp_err_stats *tx_qp_err_stats =
		&rdma_stats->tx_qp_err_stats;

	memset(stats, 0, (OCRDMA_MAX_DBGFS_MEM));

	pcur = stats;
	pcur += ocrdma_add_stat(stats, pcur, "local_length_errors",
			(u64)tx_qp_err_stats->local_length_errors);
	pcur += ocrdma_add_stat(stats, pcur, "local_protection_errors",
			(u64)tx_qp_err_stats->local_protection_errors);
	pcur += ocrdma_add_stat(stats, pcur, "local_qp_operation_errors",
			(u64)tx_qp_err_stats->local_qp_operation_errors);
	pcur += ocrdma_add_stat(stats, pcur, "retry_count_exceeded_errors",
			(u64)tx_qp_err_stats->retry_count_exceeded_errors);
	pcur += ocrdma_add_stat(stats, pcur, "rnr_retry_count_exceeded_errors",
			(u64)tx_qp_err_stats->rnr_retry_count_exceeded_errors);
	return stats;
}

static char *ocrdma_tx_dbg_stats(struct ocrdma_dev *dev)
{
	int i;
	char *pstats = dev->stats_mem.debugfs_mem;
	struct ocrdma_rdma_stats_resp *rdma_stats =
		(struct ocrdma_rdma_stats_resp *)dev->stats_mem.va;
	struct ocrdma_tx_dbg_stats *tx_dbg_stats =
		&rdma_stats->tx_dbg_stats;

	memset(pstats, 0, (OCRDMA_MAX_DBGFS_MEM));

	for (i = 0; i < 100; i++)
		pstats += snprintf(pstats, 80, "DW[%d] = 0x%x\n", i,
				 tx_dbg_stats->data[i]);

	return dev->stats_mem.debugfs_mem;
}

static char *ocrdma_rx_dbg_stats(struct ocrdma_dev *dev)
{
	int i;
	char *pstats = dev->stats_mem.debugfs_mem;
	struct ocrdma_rdma_stats_resp *rdma_stats =
		(struct ocrdma_rdma_stats_resp *)dev->stats_mem.va;
	struct ocrdma_rx_dbg_stats *rx_dbg_stats =
		&rdma_stats->rx_dbg_stats;

	memset(pstats, 0, (OCRDMA_MAX_DBGFS_MEM));

	for (i = 0; i < 200; i++)
		pstats += snprintf(pstats, 80, "DW[%d] = 0x%x\n", i,
				 rx_dbg_stats->data[i]);

	return dev->stats_mem.debugfs_mem;
}

static char *ocrdma_driver_dbg_stats(struct ocrdma_dev *dev)
{
	char *stats = dev->stats_mem.debugfs_mem, *pcur;


	memset(stats, 0, (OCRDMA_MAX_DBGFS_MEM));

	pcur = stats;
	pcur += ocrdma_add_stat(stats, pcur, "async_cq_err",
				(u64)(dev->async_err_stats
				[OCRDMA_CQ_ERROR].counter));
	pcur += ocrdma_add_stat(stats, pcur, "async_cq_overrun_err",
				(u64)dev->async_err_stats
				[OCRDMA_CQ_OVERRUN_ERROR].counter);
	pcur += ocrdma_add_stat(stats, pcur, "async_cq_qpcat_err",
				(u64)dev->async_err_stats
				[OCRDMA_CQ_QPCAT_ERROR].counter);
	pcur += ocrdma_add_stat(stats, pcur, "async_qp_access_err",
				(u64)dev->async_err_stats
				[OCRDMA_QP_ACCESS_ERROR].counter);
	pcur += ocrdma_add_stat(stats, pcur, "async_qp_commm_est_evt",
				(u64)dev->async_err_stats
				[OCRDMA_QP_COMM_EST_EVENT].counter);
	pcur += ocrdma_add_stat(stats, pcur, "async_sq_drained_evt",
				(u64)dev->async_err_stats
				[OCRDMA_SQ_DRAINED_EVENT].counter);
	pcur += ocrdma_add_stat(stats, pcur, "async_dev_fatal_evt",
				(u64)dev->async_err_stats
				[OCRDMA_DEVICE_FATAL_EVENT].counter);
	pcur += ocrdma_add_stat(stats, pcur, "async_srqcat_err",
				(u64)dev->async_err_stats
				[OCRDMA_SRQCAT_ERROR].counter);
	pcur += ocrdma_add_stat(stats, pcur, "async_srq_limit_evt",
				(u64)dev->async_err_stats
				[OCRDMA_SRQ_LIMIT_EVENT].counter);
	pcur += ocrdma_add_stat(stats, pcur, "async_qp_last_wqe_evt",
				(u64)dev->async_err_stats
				[OCRDMA_QP_LAST_WQE_EVENT].counter);

	pcur += ocrdma_add_stat(stats, pcur, "cqe_loc_len_err",
				(u64)dev->cqe_err_stats
				[OCRDMA_CQE_LOC_LEN_ERR].counter);
	pcur += ocrdma_add_stat(stats, pcur, "cqe_loc_qp_op_err",
				(u64)dev->cqe_err_stats
				[OCRDMA_CQE_LOC_QP_OP_ERR].counter);
	pcur += ocrdma_add_stat(stats, pcur, "cqe_loc_eec_op_err",
				(u64)dev->cqe_err_stats
				[OCRDMA_CQE_LOC_EEC_OP_ERR].counter);
	pcur += ocrdma_add_stat(stats, pcur, "cqe_loc_prot_err",
				(u64)dev->cqe_err_stats
				[OCRDMA_CQE_LOC_PROT_ERR].counter);
	pcur += ocrdma_add_stat(stats, pcur, "cqe_wr_flush_err",
				(u64)dev->cqe_err_stats
				[OCRDMA_CQE_WR_FLUSH_ERR].counter);
	pcur += ocrdma_add_stat(stats, pcur, "cqe_mw_bind_err",
				(u64)dev->cqe_err_stats
				[OCRDMA_CQE_MW_BIND_ERR].counter);
	pcur += ocrdma_add_stat(stats, pcur, "cqe_bad_resp_err",
				(u64)dev->cqe_err_stats
				[OCRDMA_CQE_BAD_RESP_ERR].counter);
	pcur += ocrdma_add_stat(stats, pcur, "cqe_loc_access_err",
				(u64)dev->cqe_err_stats
				[OCRDMA_CQE_LOC_ACCESS_ERR].counter);
	pcur += ocrdma_add_stat(stats, pcur, "cqe_rem_inv_req_err",
				(u64)dev->cqe_err_stats
				[OCRDMA_CQE_REM_INV_REQ_ERR].counter);
	pcur += ocrdma_add_stat(stats, pcur, "cqe_rem_access_err",
				(u64)dev->cqe_err_stats
				[OCRDMA_CQE_REM_ACCESS_ERR].counter);
	pcur += ocrdma_add_stat(stats, pcur, "cqe_rem_op_err",
				(u64)dev->cqe_err_stats
				[OCRDMA_CQE_REM_OP_ERR].counter);
	pcur += ocrdma_add_stat(stats, pcur, "cqe_retry_exc_err",
				(u64)dev->cqe_err_stats
				[OCRDMA_CQE_RETRY_EXC_ERR].counter);
	pcur += ocrdma_add_stat(stats, pcur, "cqe_rnr_retry_exc_err",
				(u64)dev->cqe_err_stats
				[OCRDMA_CQE_RNR_RETRY_EXC_ERR].counter);
	pcur += ocrdma_add_stat(stats, pcur, "cqe_loc_rdd_viol_err",
				(u64)dev->cqe_err_stats
				[OCRDMA_CQE_LOC_RDD_VIOL_ERR].counter);
	pcur += ocrdma_add_stat(stats, pcur, "cqe_rem_inv_rd_req_err",
				(u64)dev->cqe_err_stats
				[OCRDMA_CQE_REM_INV_RD_REQ_ERR].counter);
	pcur += ocrdma_add_stat(stats, pcur, "cqe_rem_abort_err",
				(u64)dev->cqe_err_stats
				[OCRDMA_CQE_REM_ABORT_ERR].counter);
	pcur += ocrdma_add_stat(stats, pcur, "cqe_inv_eecn_err",
				(u64)dev->cqe_err_stats
				[OCRDMA_CQE_INV_EECN_ERR].counter);
	pcur += ocrdma_add_stat(stats, pcur, "cqe_inv_eec_state_err",
				(u64)dev->cqe_err_stats
				[OCRDMA_CQE_INV_EEC_STATE_ERR].counter);
	pcur += ocrdma_add_stat(stats, pcur, "cqe_fatal_err",
				(u64)dev->cqe_err_stats
				[OCRDMA_CQE_FATAL_ERR].counter);
	pcur += ocrdma_add_stat(stats, pcur, "cqe_resp_timeout_err",
				(u64)dev->cqe_err_stats
				[OCRDMA_CQE_RESP_TIMEOUT_ERR].counter);
	pcur += ocrdma_add_stat(stats, pcur, "cqe_general_err",
				(u64)dev->cqe_err_stats
				[OCRDMA_CQE_GENERAL_ERR].counter);
	return stats;
}

static void ocrdma_update_stats(struct ocrdma_dev *dev)
{
	ulong now = jiffies, secs;
	int status;
	struct ocrdma_rdma_stats_resp *rdma_stats =
		      (struct ocrdma_rdma_stats_resp *)dev->stats_mem.va;
	struct ocrdma_rsrc_stats *rsrc_stats = &rdma_stats->act_rsrc_stats;

	secs = jiffies_to_msecs(now - dev->last_stats_time) / 1000U;
	if (secs) {
		/* update */
		status = ocrdma_mbx_rdma_stats(dev, false);
		if (status)
			pr_err("%s: stats mbox failed with status = %d\n",
			       __func__, status);
		/* Update PD counters from PD resource manager */
		if (dev->pd_mgr->pd_prealloc_valid) {
			rsrc_stats->dpp_pds = dev->pd_mgr->pd_dpp_count;
			rsrc_stats->non_dpp_pds = dev->pd_mgr->pd_norm_count;
			/* Threshold stata*/
			rsrc_stats = &rdma_stats->th_rsrc_stats;
			rsrc_stats->dpp_pds = dev->pd_mgr->pd_dpp_thrsh;
			rsrc_stats->non_dpp_pds = dev->pd_mgr->pd_norm_thrsh;
		}
		dev->last_stats_time = jiffies;
	}
}

static ssize_t ocrdma_dbgfs_ops_write(struct file *filp,
					const char __user *buffer,
					size_t count, loff_t *ppos)
{
	char tmp_str[32];
	long reset;
	int status;
	struct ocrdma_stats *pstats = filp->private_data;
	struct ocrdma_dev *dev = pstats->dev;

	if (*ppos != 0 || count == 0 || count > sizeof(tmp_str))
		goto err;

	if (copy_from_user(tmp_str, buffer, count))
		goto err;

	tmp_str[count-1] = '\0';
	if (kstrtol(tmp_str, 10, &reset))
		goto err;

	switch (pstats->type) {
	case OCRDMA_RESET_STATS:
		if (reset) {
			status = ocrdma_mbx_rdma_stats(dev, true);
			if (status) {
				pr_err("Failed to reset stats = %d\n", status);
				goto err;
			}
		}
		break;
	default:
		goto err;
	}

	return count;
err:
	return -EFAULT;
}

int ocrdma_pma_counters(struct ocrdma_dev *dev,
			struct ib_mad *out_mad)
{
	struct ib_pma_portcounters *pma_cnt;

	memset(out_mad->data, 0, sizeof out_mad->data);
	pma_cnt = (void *)(out_mad->data + 40);
	ocrdma_update_stats(dev);

	pma_cnt->port_xmit_data    = cpu_to_be32(ocrdma_sysfs_xmit_data(dev));
	pma_cnt->port_rcv_data     = cpu_to_be32(ocrdma_sysfs_rcv_data(dev));
	pma_cnt->port_xmit_packets = cpu_to_be32(ocrdma_sysfs_xmit_pkts(dev));
	pma_cnt->port_rcv_packets  = cpu_to_be32(ocrdma_sysfs_rcv_pkts(dev));
	return 0;
}

static ssize_t ocrdma_dbgfs_ops_read(struct file *filp, char __user *buffer,
					size_t usr_buf_len, loff_t *ppos)
{
	struct ocrdma_stats *pstats = filp->private_data;
	struct ocrdma_dev *dev = pstats->dev;
	ssize_t status = 0;
	char *data = NULL;

	/* No partial reads */
	if (*ppos != 0)
		return 0;

	mutex_lock(&dev->stats_lock);

	ocrdma_update_stats(dev);

	switch (pstats->type) {
	case OCRDMA_RSRC_STATS:
		data = ocrdma_resource_stats(dev);
		break;
	case OCRDMA_RXSTATS:
		data = ocrdma_rx_stats(dev);
		break;
	case OCRDMA_WQESTATS:
		data = ocrdma_wqe_stats(dev);
		break;
	case OCRDMA_TXSTATS:
		data = ocrdma_tx_stats(dev);
		break;
	case OCRDMA_DB_ERRSTATS:
		data = ocrdma_db_errstats(dev);
		break;
	case OCRDMA_RXQP_ERRSTATS:
		data = ocrdma_rxqp_errstats(dev);
		break;
	case OCRDMA_TXQP_ERRSTATS:
		data = ocrdma_txqp_errstats(dev);
		break;
	case OCRDMA_TX_DBG_STATS:
		data = ocrdma_tx_dbg_stats(dev);
		break;
	case OCRDMA_RX_DBG_STATS:
		data = ocrdma_rx_dbg_stats(dev);
		break;
	case OCRDMA_DRV_STATS:
		data = ocrdma_driver_dbg_stats(dev);
		break;

	default:
		status = -EFAULT;
		goto exit;
	}

	if (usr_buf_len < strlen(data)) {
		status = -ENOSPC;
		goto exit;
	}

	status = simple_read_from_buffer(buffer, usr_buf_len, ppos, data,
					 strlen(data));
exit:
	mutex_unlock(&dev->stats_lock);
	return status;
}

static const struct file_operations ocrdma_dbg_ops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = ocrdma_dbgfs_ops_read,
	.write = ocrdma_dbgfs_ops_write,
};

void ocrdma_add_port_stats(struct ocrdma_dev *dev)
{
	const struct pci_dev *pdev = dev->nic_info.pdev;

	if (!ocrdma_dbgfs_dir)
		return;

	/* Create post stats base dir */
	dev->dir = debugfs_create_dir(pci_name(pdev), ocrdma_dbgfs_dir);
	if (!dev->dir)
		goto err;

	dev->rsrc_stats.type = OCRDMA_RSRC_STATS;
	dev->rsrc_stats.dev = dev;
	if (!debugfs_create_file("resource_stats", S_IRUSR, dev->dir,
				 &dev->rsrc_stats, &ocrdma_dbg_ops))
		goto err;

	dev->rx_stats.type = OCRDMA_RXSTATS;
	dev->rx_stats.dev = dev;
	if (!debugfs_create_file("rx_stats", S_IRUSR, dev->dir,
				 &dev->rx_stats, &ocrdma_dbg_ops))
		goto err;

	dev->wqe_stats.type = OCRDMA_WQESTATS;
	dev->wqe_stats.dev = dev;
	if (!debugfs_create_file("wqe_stats", S_IRUSR, dev->dir,
				 &dev->wqe_stats, &ocrdma_dbg_ops))
		goto err;

	dev->tx_stats.type = OCRDMA_TXSTATS;
	dev->tx_stats.dev = dev;
	if (!debugfs_create_file("tx_stats", S_IRUSR, dev->dir,
				 &dev->tx_stats, &ocrdma_dbg_ops))
		goto err;

	dev->db_err_stats.type = OCRDMA_DB_ERRSTATS;
	dev->db_err_stats.dev = dev;
	if (!debugfs_create_file("db_err_stats", S_IRUSR, dev->dir,
				 &dev->db_err_stats, &ocrdma_dbg_ops))
		goto err;


	dev->tx_qp_err_stats.type = OCRDMA_TXQP_ERRSTATS;
	dev->tx_qp_err_stats.dev = dev;
	if (!debugfs_create_file("tx_qp_err_stats", S_IRUSR, dev->dir,
				 &dev->tx_qp_err_stats, &ocrdma_dbg_ops))
		goto err;

	dev->rx_qp_err_stats.type = OCRDMA_RXQP_ERRSTATS;
	dev->rx_qp_err_stats.dev = dev;
	if (!debugfs_create_file("rx_qp_err_stats", S_IRUSR, dev->dir,
				 &dev->rx_qp_err_stats, &ocrdma_dbg_ops))
		goto err;


	dev->tx_dbg_stats.type = OCRDMA_TX_DBG_STATS;
	dev->tx_dbg_stats.dev = dev;
	if (!debugfs_create_file("tx_dbg_stats", S_IRUSR, dev->dir,
				 &dev->tx_dbg_stats, &ocrdma_dbg_ops))
		goto err;

	dev->rx_dbg_stats.type = OCRDMA_RX_DBG_STATS;
	dev->rx_dbg_stats.dev = dev;
	if (!debugfs_create_file("rx_dbg_stats", S_IRUSR, dev->dir,
				 &dev->rx_dbg_stats, &ocrdma_dbg_ops))
		goto err;

	dev->driver_stats.type = OCRDMA_DRV_STATS;
	dev->driver_stats.dev = dev;
	if (!debugfs_create_file("driver_dbg_stats", S_IRUSR, dev->dir,
					&dev->driver_stats, &ocrdma_dbg_ops))
		goto err;

	dev->reset_stats.type = OCRDMA_RESET_STATS;
	dev->reset_stats.dev = dev;
	if (!debugfs_create_file("reset_stats", 0200, dev->dir,
				&dev->reset_stats, &ocrdma_dbg_ops))
		goto err;


	return;
err:
	debugfs_remove_recursive(dev->dir);
	dev->dir = NULL;
}

void ocrdma_rem_port_stats(struct ocrdma_dev *dev)
{
	if (!dev->dir)
		return;
	debugfs_remove_recursive(dev->dir);
}

void ocrdma_init_debugfs(void)
{
	/* Create base dir in debugfs root dir */
	ocrdma_dbgfs_dir = debugfs_create_dir("ocrdma", NULL);
}

void ocrdma_rem_debugfs(void)
{
	debugfs_remove_recursive(ocrdma_dbgfs_dir);
}
