/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/etherdevice.h>
#include <linux/qed/qed_chain.h>
#include <linux/qed/qed_if.h>
#include "qed.h"
#include "qed_cxt.h"
#include "qed_dcbx.h"
#include "qed_dev_api.h"
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_init_ops.h"
#include "qed_int.h"
#include "qed_mcp.h"
#include "qed_reg_addr.h"
#include "qed_sp.h"
#include "qed_sriov.h"
#include "qed_vf.h"

static spinlock_t qm_lock;
static bool qm_lock_init = false;

/* API common to all protocols */
enum BAR_ID {
	BAR_ID_0,       /* used for GRC */
	BAR_ID_1        /* Used for doorbells */
};

static u32 qed_hw_bar_size(struct qed_hwfn	*p_hwfn,
			   enum BAR_ID		bar_id)
{
	u32 bar_reg = (bar_id == BAR_ID_0 ?
		       PGLUE_B_REG_PF_BAR0_SIZE : PGLUE_B_REG_PF_BAR1_SIZE);
	u32 val;

	if (IS_VF(p_hwfn->cdev))
		return 1 << 17;

	val = qed_rd(p_hwfn, p_hwfn->p_main_ptt, bar_reg);
	if (val)
		return 1 << (val + 15);

	/* Old MFW initialized above registered only conditionally */
	if (p_hwfn->cdev->num_hwfns > 1) {
		DP_INFO(p_hwfn,
			"BAR size not configured. Assuming BAR size of 256kB for GRC and 512kB for DB\n");
			return BAR_ID_0 ? 256 * 1024 : 512 * 1024;
	} else {
		DP_INFO(p_hwfn,
			"BAR size not configured. Assuming BAR size of 512kB for GRC and 512kB for DB\n");
			return 512 * 1024;
	}
}

void qed_init_dp(struct qed_dev *cdev,
		 u32 dp_module, u8 dp_level)
{
	u32 i;

	cdev->dp_level = dp_level;
	cdev->dp_module = dp_module;
	for (i = 0; i < MAX_HWFNS_PER_DEVICE; i++) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		p_hwfn->dp_level = dp_level;
		p_hwfn->dp_module = dp_module;
	}
}

void qed_init_struct(struct qed_dev *cdev)
{
	u8 i;

	for (i = 0; i < MAX_HWFNS_PER_DEVICE; i++) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		p_hwfn->cdev = cdev;
		p_hwfn->my_id = i;
		p_hwfn->b_active = false;

		mutex_init(&p_hwfn->dmae_info.mutex);
	}

	/* hwfn 0 is always active */
	cdev->hwfns[0].b_active = true;

	/* set the default cache alignment to 128 */
	cdev->cache_shift = 7;
}

static void qed_qm_info_free(struct qed_hwfn *p_hwfn)
{
	struct qed_qm_info *qm_info = &p_hwfn->qm_info;

	kfree(qm_info->qm_pq_params);
	qm_info->qm_pq_params = NULL;
	kfree(qm_info->qm_vport_params);
	qm_info->qm_vport_params = NULL;
	kfree(qm_info->qm_port_params);
	qm_info->qm_port_params = NULL;
	kfree(qm_info->wfq_data);
	qm_info->wfq_data = NULL;
}

void qed_resc_free(struct qed_dev *cdev)
{
	int i;

	if (IS_VF(cdev))
		return;

	kfree(cdev->fw_data);
	cdev->fw_data = NULL;

	kfree(cdev->reset_stats);

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		kfree(p_hwfn->p_tx_cids);
		p_hwfn->p_tx_cids = NULL;
		kfree(p_hwfn->p_rx_cids);
		p_hwfn->p_rx_cids = NULL;
	}

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		qed_cxt_mngr_free(p_hwfn);
		qed_qm_info_free(p_hwfn);
		qed_spq_free(p_hwfn);
		qed_eq_free(p_hwfn, p_hwfn->p_eq);
		qed_consq_free(p_hwfn, p_hwfn->p_consq);
		qed_int_free(p_hwfn);
		qed_iov_free(p_hwfn);
		qed_dmae_info_free(p_hwfn);
		qed_dcbx_info_free(p_hwfn, p_hwfn->p_dcbx_info);
	}
}

static int qed_init_qm_info(struct qed_hwfn *p_hwfn, bool b_sleepable)
{
	u8 num_vports, vf_offset = 0, i, vport_id, num_ports, curr_queue = 0;
	struct qed_qm_info *qm_info = &p_hwfn->qm_info;
	struct init_qm_port_params *p_qm_port;
	u16 num_pqs, multi_cos_tcs = 1;
	u8 pf_wfq = qm_info->pf_wfq;
	u32 pf_rl = qm_info->pf_rl;
	u16 num_vfs = 0;

#ifdef CONFIG_QED_SRIOV
	if (p_hwfn->cdev->p_iov_info)
		num_vfs = p_hwfn->cdev->p_iov_info->total_vfs;
#endif
	memset(qm_info, 0, sizeof(*qm_info));

	num_pqs = multi_cos_tcs + num_vfs + 1;	/* The '1' is for pure-LB */
	num_vports = (u8)RESC_NUM(p_hwfn, QED_VPORT);

	/* Sanity checking that setup requires legal number of resources */
	if (num_pqs > RESC_NUM(p_hwfn, QED_PQ)) {
		DP_ERR(p_hwfn,
		       "Need too many Physical queues - 0x%04x when only %04x are available\n",
		       num_pqs, RESC_NUM(p_hwfn, QED_PQ));
		return -EINVAL;
	}

	/* PQs will be arranged as follows: First per-TC PQ then pure-LB quete.
	 */
	qm_info->qm_pq_params = kcalloc(num_pqs,
					sizeof(struct init_qm_pq_params),
					b_sleepable ? GFP_KERNEL : GFP_ATOMIC);
	if (!qm_info->qm_pq_params)
		goto alloc_err;

	qm_info->qm_vport_params = kcalloc(num_vports,
					   sizeof(struct init_qm_vport_params),
					   b_sleepable ? GFP_KERNEL
						       : GFP_ATOMIC);
	if (!qm_info->qm_vport_params)
		goto alloc_err;

	qm_info->qm_port_params = kcalloc(MAX_NUM_PORTS,
					  sizeof(struct init_qm_port_params),
					  b_sleepable ? GFP_KERNEL
						      : GFP_ATOMIC);
	if (!qm_info->qm_port_params)
		goto alloc_err;

	qm_info->wfq_data = kcalloc(num_vports, sizeof(struct qed_wfq_data),
				    b_sleepable ? GFP_KERNEL : GFP_ATOMIC);
	if (!qm_info->wfq_data)
		goto alloc_err;

	vport_id = (u8)RESC_START(p_hwfn, QED_VPORT);

	/* First init per-TC PQs */
	for (i = 0; i < multi_cos_tcs; i++) {
		struct init_qm_pq_params *params =
		    &qm_info->qm_pq_params[curr_queue++];

		if (p_hwfn->hw_info.personality == QED_PCI_ETH) {
			params->vport_id = vport_id;
			params->tc_id = p_hwfn->hw_info.non_offload_tc;
			params->wrr_group = 1;
		} else {
			params->vport_id = vport_id;
			params->tc_id = p_hwfn->hw_info.offload_tc;
			params->wrr_group = 1;
		}
	}

	/* Then init pure-LB PQ */
	qm_info->pure_lb_pq = curr_queue;
	qm_info->qm_pq_params[curr_queue].vport_id =
	    (u8) RESC_START(p_hwfn, QED_VPORT);
	qm_info->qm_pq_params[curr_queue].tc_id = PURE_LB_TC;
	qm_info->qm_pq_params[curr_queue].wrr_group = 1;
	curr_queue++;

	qm_info->offload_pq = 0;
	/* Then init per-VF PQs */
	vf_offset = curr_queue;
	for (i = 0; i < num_vfs; i++) {
		/* First vport is used by the PF */
		qm_info->qm_pq_params[curr_queue].vport_id = vport_id + i + 1;
		qm_info->qm_pq_params[curr_queue].tc_id =
		    p_hwfn->hw_info.non_offload_tc;
		qm_info->qm_pq_params[curr_queue].wrr_group = 1;
		qm_info->qm_pq_params[curr_queue].rl_valid = 1;
		curr_queue++;
	}

	qm_info->vf_queues_offset = vf_offset;
	qm_info->num_pqs = num_pqs;
	qm_info->num_vports = num_vports;

	/* Initialize qm port parameters */
	num_ports = p_hwfn->cdev->num_ports_in_engines;
	for (i = 0; i < num_ports; i++) {
		p_qm_port = &qm_info->qm_port_params[i];
		p_qm_port->active = 1;
		if (num_ports == 4)
			p_qm_port->active_phys_tcs = 0x7;
		else
			p_qm_port->active_phys_tcs = 0x9f;
		p_qm_port->num_pbf_cmd_lines = PBF_MAX_CMD_LINES / num_ports;
		p_qm_port->num_btb_blocks = BTB_MAX_BLOCKS / num_ports;
	}

	qm_info->max_phys_tcs_per_port = NUM_OF_PHYS_TCS;

	qm_info->start_pq = (u16)RESC_START(p_hwfn, QED_PQ);

	qm_info->num_vf_pqs = num_vfs;
	qm_info->start_vport = (u8) RESC_START(p_hwfn, QED_VPORT);

	for (i = 0; i < qm_info->num_vports; i++)
		qm_info->qm_vport_params[i].vport_wfq = 1;

	qm_info->vport_rl_en = 1;
	qm_info->vport_wfq_en = 1;
	qm_info->pf_rl = pf_rl;
	qm_info->pf_wfq = pf_wfq;

	return 0;

alloc_err:
	DP_NOTICE(p_hwfn, "Failed to allocate memory for QM params\n");
	qed_qm_info_free(p_hwfn);
	return -ENOMEM;
}

/* This function reconfigures the QM pf on the fly.
 * For this purpose we:
 * 1. reconfigure the QM database
 * 2. set new values to runtime arrat
 * 3. send an sdm_qm_cmd through the rbc interface to stop the QM
 * 4. activate init tool in QM_PF stage
 * 5. send an sdm_qm_cmd through rbc interface to release the QM
 */
int qed_qm_reconf(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	struct qed_qm_info *qm_info = &p_hwfn->qm_info;
	bool b_rc;
	int rc;

	/* qm_info is allocated in qed_init_qm_info() which is already called
	 * from qed_resc_alloc() or previous call of qed_qm_reconf().
	 * The allocated size may change each init, so we free it before next
	 * allocation.
	 */
	qed_qm_info_free(p_hwfn);

	/* initialize qed's qm data structure */
	rc = qed_init_qm_info(p_hwfn, false);
	if (rc)
		return rc;

	/* stop PF's qm queues */
	spin_lock_bh(&qm_lock);
	b_rc = qed_send_qm_stop_cmd(p_hwfn, p_ptt, false, true,
				    qm_info->start_pq, qm_info->num_pqs);
	spin_unlock_bh(&qm_lock);
	if (!b_rc)
		return -EINVAL;

	/* clear the QM_PF runtime phase leftovers from previous init */
	qed_init_clear_rt_data(p_hwfn);

	/* prepare QM portion of runtime array */
	qed_qm_init_pf(p_hwfn);

	/* activate init tool on runtime array */
	rc = qed_init_run(p_hwfn, p_ptt, PHASE_QM_PF, p_hwfn->rel_pf_id,
			  p_hwfn->hw_info.hw_mode);
	if (rc)
		return rc;

	/* start PF's qm queues */
	spin_lock_bh(&qm_lock);
	b_rc = qed_send_qm_stop_cmd(p_hwfn, p_ptt, true, true,
				    qm_info->start_pq, qm_info->num_pqs);
	spin_unlock_bh(&qm_lock);
	if (!b_rc)
		return -EINVAL;

	return 0;
}

int qed_resc_alloc(struct qed_dev *cdev)
{
	struct qed_consq *p_consq;
	struct qed_eq *p_eq;
	int i, rc = 0;

	if (IS_VF(cdev))
		return rc;

	cdev->fw_data = kzalloc(sizeof(*cdev->fw_data), GFP_KERNEL);
	if (!cdev->fw_data)
		return -ENOMEM;

	/* Allocate Memory for the Queue->CID mapping */
	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];
		int tx_size = sizeof(struct qed_hw_cid_data) *
				     RESC_NUM(p_hwfn, QED_L2_QUEUE);
		int rx_size = sizeof(struct qed_hw_cid_data) *
				     RESC_NUM(p_hwfn, QED_L2_QUEUE);

		p_hwfn->p_tx_cids = kzalloc(tx_size, GFP_KERNEL);
		if (!p_hwfn->p_tx_cids) {
			DP_NOTICE(p_hwfn,
				  "Failed to allocate memory for Tx Cids\n");
			rc = -ENOMEM;
			goto alloc_err;
		}

		p_hwfn->p_rx_cids = kzalloc(rx_size, GFP_KERNEL);
		if (!p_hwfn->p_rx_cids) {
			DP_NOTICE(p_hwfn,
				  "Failed to allocate memory for Rx Cids\n");
			rc = -ENOMEM;
			goto alloc_err;
		}
	}

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		/* First allocate the context manager structure */
		rc = qed_cxt_mngr_alloc(p_hwfn);
		if (rc)
			goto alloc_err;

		/* Set the HW cid/tid numbers (in the contest manager)
		 * Must be done prior to any further computations.
		 */
		rc = qed_cxt_set_pf_params(p_hwfn);
		if (rc)
			goto alloc_err;

		/* Prepare and process QM requirements */
		rc = qed_init_qm_info(p_hwfn, true);
		if (rc)
			goto alloc_err;

		/* Compute the ILT client partition */
		rc = qed_cxt_cfg_ilt_compute(p_hwfn);
		if (rc)
			goto alloc_err;

		/* CID map / ILT shadow table / T2
		 * The talbes sizes are determined by the computations above
		 */
		rc = qed_cxt_tables_alloc(p_hwfn);
		if (rc)
			goto alloc_err;

		/* SPQ, must follow ILT because initializes SPQ context */
		rc = qed_spq_alloc(p_hwfn);
		if (rc)
			goto alloc_err;

		/* SP status block allocation */
		p_hwfn->p_dpc_ptt = qed_get_reserved_ptt(p_hwfn,
							 RESERVED_PTT_DPC);

		rc = qed_int_alloc(p_hwfn, p_hwfn->p_main_ptt);
		if (rc)
			goto alloc_err;

		rc = qed_iov_alloc(p_hwfn);
		if (rc)
			goto alloc_err;

		/* EQ */
		p_eq = qed_eq_alloc(p_hwfn, 256);
		if (!p_eq) {
			rc = -ENOMEM;
			goto alloc_err;
		}
		p_hwfn->p_eq = p_eq;

		p_consq = qed_consq_alloc(p_hwfn);
		if (!p_consq) {
			rc = -ENOMEM;
			goto alloc_err;
		}
		p_hwfn->p_consq = p_consq;

		/* DMA info initialization */
		rc = qed_dmae_info_alloc(p_hwfn);
		if (rc) {
			DP_NOTICE(p_hwfn,
				  "Failed to allocate memory for dmae_info structure\n");
			goto alloc_err;
		}

		/* DCBX initialization */
		rc = qed_dcbx_info_alloc(p_hwfn);
		if (rc) {
			DP_NOTICE(p_hwfn,
				  "Failed to allocate memory for dcbx structure\n");
			goto alloc_err;
		}
	}

	cdev->reset_stats = kzalloc(sizeof(*cdev->reset_stats), GFP_KERNEL);
	if (!cdev->reset_stats) {
		DP_NOTICE(cdev, "Failed to allocate reset statistics\n");
		rc = -ENOMEM;
		goto alloc_err;
	}

	return 0;

alloc_err:
	qed_resc_free(cdev);
	return rc;
}

void qed_resc_setup(struct qed_dev *cdev)
{
	int i;

	if (IS_VF(cdev))
		return;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		qed_cxt_mngr_setup(p_hwfn);
		qed_spq_setup(p_hwfn);
		qed_eq_setup(p_hwfn, p_hwfn->p_eq);
		qed_consq_setup(p_hwfn, p_hwfn->p_consq);

		/* Read shadow of current MFW mailbox */
		qed_mcp_read_mb(p_hwfn, p_hwfn->p_main_ptt);
		memcpy(p_hwfn->mcp_info->mfw_mb_shadow,
		       p_hwfn->mcp_info->mfw_mb_cur,
		       p_hwfn->mcp_info->mfw_mb_length);

		qed_int_setup(p_hwfn, p_hwfn->p_main_ptt);

		qed_iov_setup(p_hwfn, p_hwfn->p_main_ptt);
	}
}

#define FINAL_CLEANUP_POLL_CNT          (100)
#define FINAL_CLEANUP_POLL_TIME         (10)
int qed_final_cleanup(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt, u16 id, bool is_vf)
{
	u32 command = 0, addr, count = FINAL_CLEANUP_POLL_CNT;
	int rc = -EBUSY;

	addr = GTT_BAR0_MAP_REG_USDM_RAM +
		USTORM_FLR_FINAL_ACK_OFFSET(p_hwfn->rel_pf_id);

	if (is_vf)
		id += 0x10;

	command |= X_FINAL_CLEANUP_AGG_INT <<
		SDM_AGG_INT_COMP_PARAMS_AGG_INT_INDEX_SHIFT;
	command |= 1 << SDM_AGG_INT_COMP_PARAMS_AGG_VECTOR_ENABLE_SHIFT;
	command |= id << SDM_AGG_INT_COMP_PARAMS_AGG_VECTOR_BIT_SHIFT;
	command |= SDM_COMP_TYPE_AGG_INT << SDM_OP_GEN_COMP_TYPE_SHIFT;

	/* Make sure notification is not set before initiating final cleanup */
	if (REG_RD(p_hwfn, addr)) {
		DP_NOTICE(
			p_hwfn,
			"Unexpected; Found final cleanup notification before initiating final cleanup\n");
		REG_WR(p_hwfn, addr, 0);
	}

	DP_VERBOSE(p_hwfn, QED_MSG_IOV,
		   "Sending final cleanup for PFVF[%d] [Command %08x\n]",
		   id, command);

	qed_wr(p_hwfn, p_ptt, XSDM_REG_OPERATION_GEN, command);

	/* Poll until completion */
	while (!REG_RD(p_hwfn, addr) && count--)
		msleep(FINAL_CLEANUP_POLL_TIME);

	if (REG_RD(p_hwfn, addr))
		rc = 0;
	else
		DP_NOTICE(p_hwfn,
			  "Failed to receive FW final cleanup notification\n");

	/* Cleanup afterwards */
	REG_WR(p_hwfn, addr, 0);

	return rc;
}

static void qed_calc_hw_mode(struct qed_hwfn *p_hwfn)
{
	int hw_mode = 0;

	hw_mode = (1 << MODE_BB_B0);

	switch (p_hwfn->cdev->num_ports_in_engines) {
	case 1:
		hw_mode |= 1 << MODE_PORTS_PER_ENG_1;
		break;
	case 2:
		hw_mode |= 1 << MODE_PORTS_PER_ENG_2;
		break;
	case 4:
		hw_mode |= 1 << MODE_PORTS_PER_ENG_4;
		break;
	default:
		DP_NOTICE(p_hwfn, "num_ports_in_engine = %d not supported\n",
			  p_hwfn->cdev->num_ports_in_engines);
		return;
	}

	switch (p_hwfn->cdev->mf_mode) {
	case QED_MF_DEFAULT:
	case QED_MF_NPAR:
		hw_mode |= 1 << MODE_MF_SI;
		break;
	case QED_MF_OVLAN:
		hw_mode |= 1 << MODE_MF_SD;
		break;
	default:
		DP_NOTICE(p_hwfn, "Unsupported MF mode, init as DEFAULT\n");
		hw_mode |= 1 << MODE_MF_SI;
	}

	hw_mode |= 1 << MODE_ASIC;

	if (p_hwfn->cdev->num_hwfns > 1)
		hw_mode |= 1 << MODE_100G;

	p_hwfn->hw_info.hw_mode = hw_mode;

	DP_VERBOSE(p_hwfn, (NETIF_MSG_PROBE | NETIF_MSG_IFUP),
		   "Configuring function for hw_mode: 0x%08x\n",
		   p_hwfn->hw_info.hw_mode);
}

/* Init run time data for all PFs on an engine. */
static void qed_init_cau_rt_data(struct qed_dev *cdev)
{
	u32 offset = CAU_REG_SB_VAR_MEMORY_RT_OFFSET;
	int i, sb_id;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];
		struct qed_igu_info *p_igu_info;
		struct qed_igu_block *p_block;
		struct cau_sb_entry sb_entry;

		p_igu_info = p_hwfn->hw_info.p_igu_info;

		for (sb_id = 0; sb_id < QED_MAPPING_MEMORY_SIZE(cdev);
		     sb_id++) {
			p_block = &p_igu_info->igu_map.igu_blocks[sb_id];
			if (!p_block->is_pf)
				continue;

			qed_init_cau_sb_entry(p_hwfn, &sb_entry,
					      p_block->function_id,
					      0, 0);
			STORE_RT_REG_AGG(p_hwfn, offset + sb_id * 2,
					 sb_entry);
		}
	}
}

static int qed_hw_init_common(struct qed_hwfn *p_hwfn,
			      struct qed_ptt *p_ptt,
			      int hw_mode)
{
	struct qed_qm_info *qm_info = &p_hwfn->qm_info;
	struct qed_qm_common_rt_init_params params;
	struct qed_dev *cdev = p_hwfn->cdev;
	u32 concrete_fid;
	int rc = 0;
	u8 vf_id;

	qed_init_cau_rt_data(cdev);

	/* Program GTT windows */
	qed_gtt_init(p_hwfn);

	if (p_hwfn->mcp_info) {
		if (p_hwfn->mcp_info->func_info.bandwidth_max)
			qm_info->pf_rl_en = 1;
		if (p_hwfn->mcp_info->func_info.bandwidth_min)
			qm_info->pf_wfq_en = 1;
	}

	memset(&params, 0, sizeof(params));
	params.max_ports_per_engine = p_hwfn->cdev->num_ports_in_engines;
	params.max_phys_tcs_per_port = qm_info->max_phys_tcs_per_port;
	params.pf_rl_en = qm_info->pf_rl_en;
	params.pf_wfq_en = qm_info->pf_wfq_en;
	params.vport_rl_en = qm_info->vport_rl_en;
	params.vport_wfq_en = qm_info->vport_wfq_en;
	params.port_params = qm_info->qm_port_params;

	qed_qm_common_rt_init(p_hwfn, &params);

	qed_cxt_hw_init_common(p_hwfn);

	/* Close gate from NIG to BRB/Storm; By default they are open, but
	 * we close them to prevent NIG from passing data to reset blocks.
	 * Should have been done in the ENGINE phase, but init-tool lacks
	 * proper port-pretend capabilities.
	 */
	qed_wr(p_hwfn, p_ptt, NIG_REG_RX_BRB_OUT_EN, 0);
	qed_wr(p_hwfn, p_ptt, NIG_REG_STORM_OUT_EN, 0);
	qed_port_pretend(p_hwfn, p_ptt, p_hwfn->port_id ^ 1);
	qed_wr(p_hwfn, p_ptt, NIG_REG_RX_BRB_OUT_EN, 0);
	qed_wr(p_hwfn, p_ptt, NIG_REG_STORM_OUT_EN, 0);
	qed_port_unpretend(p_hwfn, p_ptt);

	rc = qed_init_run(p_hwfn, p_ptt, PHASE_ENGINE, ANY_PHASE_ID, hw_mode);
	if (rc != 0)
		return rc;

	qed_wr(p_hwfn, p_ptt, PSWRQ2_REG_L2P_VALIDATE_VFID, 0);
	qed_wr(p_hwfn, p_ptt, PGLUE_B_REG_USE_CLIENTID_IN_TAG, 1);

	/* Disable relaxed ordering in the PCI config space */
	qed_wr(p_hwfn, p_ptt, 0x20b4,
	       qed_rd(p_hwfn, p_ptt, 0x20b4) & ~0x10);

	for (vf_id = 0; vf_id < MAX_NUM_VFS_BB; vf_id++) {
		concrete_fid = qed_vfid_to_concrete(p_hwfn, vf_id);
		qed_fid_pretend(p_hwfn, p_ptt, (u16) concrete_fid);
		qed_wr(p_hwfn, p_ptt, CCFC_REG_STRONG_ENABLE_VF, 0x1);
	}
	/* pretend to original PF */
	qed_fid_pretend(p_hwfn, p_ptt, p_hwfn->rel_pf_id);

	return rc;
}

static int qed_hw_init_port(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt,
			    int hw_mode)
{
	int rc = 0;

	rc = qed_init_run(p_hwfn, p_ptt, PHASE_PORT, p_hwfn->port_id, hw_mode);
	if (rc != 0)
		return rc;

	if (hw_mode & (1 << MODE_MF_SI)) {
		u8 pf_id = 0;

		if (!qed_hw_init_first_eth(p_hwfn, p_ptt, &pf_id)) {
			DP_VERBOSE(p_hwfn, NETIF_MSG_IFUP,
				   "PF[%08x] is first eth on engine\n", pf_id);

			/* We should have configured BIT for ppfid, i.e., the
			 * relative function number in the port. But there's a
			 * bug in LLH in BB where the ppfid is actually engine
			 * based, so we need to take this into account.
			 */
			qed_wr(p_hwfn, p_ptt,
			       NIG_REG_LLH_TAGMAC_DEF_PF_VECTOR, 1 << pf_id);
		}

		/* Take the protocol-based hit vector if there is a hit,
		 * otherwise take the other vector.
		 */
		qed_wr(p_hwfn, p_ptt, NIG_REG_LLH_CLS_TYPE_DUALMODE, 0x2);
	}
	return rc;
}

static int qed_hw_init_pf(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt,
			  struct qed_tunn_start_params *p_tunn,
			  int hw_mode,
			  bool b_hw_start,
			  enum qed_int_mode int_mode,
			  bool allow_npar_tx_switch)
{
	u8 rel_pf_id = p_hwfn->rel_pf_id;
	int rc = 0;

	if (p_hwfn->mcp_info) {
		struct qed_mcp_function_info *p_info;

		p_info = &p_hwfn->mcp_info->func_info;
		if (p_info->bandwidth_min)
			p_hwfn->qm_info.pf_wfq = p_info->bandwidth_min;

		/* Update rate limit once we'll actually have a link */
		p_hwfn->qm_info.pf_rl = 100000;
	}

	qed_cxt_hw_init_pf(p_hwfn);

	qed_int_igu_init_rt(p_hwfn);

	/* Set VLAN in NIG if needed */
	if (hw_mode & (1 << MODE_MF_SD)) {
		DP_VERBOSE(p_hwfn, NETIF_MSG_HW, "Configuring LLH_FUNC_TAG\n");
		STORE_RT_REG(p_hwfn, NIG_REG_LLH_FUNC_TAG_EN_RT_OFFSET, 1);
		STORE_RT_REG(p_hwfn, NIG_REG_LLH_FUNC_TAG_VALUE_RT_OFFSET,
			     p_hwfn->hw_info.ovlan);
	}

	/* Enable classification by MAC if needed */
	if (hw_mode & (1 << MODE_MF_SI)) {
		DP_VERBOSE(p_hwfn, NETIF_MSG_HW,
			   "Configuring TAGMAC_CLS_TYPE\n");
		STORE_RT_REG(p_hwfn,
			     NIG_REG_LLH_FUNC_TAGMAC_CLS_TYPE_RT_OFFSET, 1);
	}

	/* Protocl Configuration  */
	STORE_RT_REG(p_hwfn, PRS_REG_SEARCH_TCP_RT_OFFSET, 0);
	STORE_RT_REG(p_hwfn, PRS_REG_SEARCH_FCOE_RT_OFFSET, 0);
	STORE_RT_REG(p_hwfn, PRS_REG_SEARCH_ROCE_RT_OFFSET, 0);

	/* Cleanup chip from previous driver if such remains exist */
	rc = qed_final_cleanup(p_hwfn, p_ptt, rel_pf_id, false);
	if (rc != 0)
		return rc;

	/* PF Init sequence */
	rc = qed_init_run(p_hwfn, p_ptt, PHASE_PF, rel_pf_id, hw_mode);
	if (rc)
		return rc;

	/* QM_PF Init sequence (may be invoked separately e.g. for DCB) */
	rc = qed_init_run(p_hwfn, p_ptt, PHASE_QM_PF, rel_pf_id, hw_mode);
	if (rc)
		return rc;

	/* Pure runtime initializations - directly to the HW  */
	qed_int_igu_init_pure_rt(p_hwfn, p_ptt, true, true);

	if (hw_mode & (1 << MODE_MF_SI)) {
		u8 pf_id = 0;
		u32 val;

		if (!qed_hw_init_first_eth(p_hwfn, p_ptt, &pf_id)) {
			if (p_hwfn->rel_pf_id == pf_id) {
				DP_VERBOSE(p_hwfn, NETIF_MSG_IFUP,
					   "PF[%d] is first ETH on engine\n",
					   pf_id);
				val = 1;
			}
			qed_wr(p_hwfn, p_ptt, PRS_REG_MSG_INFO, val);
		}
	}

	if (b_hw_start) {
		/* enable interrupts */
		qed_int_igu_enable(p_hwfn, p_ptt, int_mode);

		/* send function start command */
		rc = qed_sp_pf_start(p_hwfn, p_tunn, p_hwfn->cdev->mf_mode,
				     allow_npar_tx_switch);
		if (rc)
			DP_NOTICE(p_hwfn, "Function start ramrod failed\n");
	}
	return rc;
}

static int qed_change_pci_hwfn(struct qed_hwfn *p_hwfn,
			       struct qed_ptt *p_ptt,
			       u8 enable)
{
	u32 delay_idx = 0, val, set_val = enable ? 1 : 0;

	/* Change PF in PXP */
	qed_wr(p_hwfn, p_ptt,
	       PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER, set_val);

	/* wait until value is set - try for 1 second every 50us */
	for (delay_idx = 0; delay_idx < 20000; delay_idx++) {
		val = qed_rd(p_hwfn, p_ptt,
			     PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER);
		if (val == set_val)
			break;

		usleep_range(50, 60);
	}

	if (val != set_val) {
		DP_NOTICE(p_hwfn,
			  "PFID_ENABLE_MASTER wasn't changed after a second\n");
		return -EAGAIN;
	}

	return 0;
}

static void qed_reset_mb_shadow(struct qed_hwfn *p_hwfn,
				struct qed_ptt *p_main_ptt)
{
	/* Read shadow of current MFW mailbox */
	qed_mcp_read_mb(p_hwfn, p_main_ptt);
	memcpy(p_hwfn->mcp_info->mfw_mb_shadow,
	       p_hwfn->mcp_info->mfw_mb_cur,
	       p_hwfn->mcp_info->mfw_mb_length);
}

int qed_hw_init(struct qed_dev *cdev,
		struct qed_tunn_start_params *p_tunn,
		bool b_hw_start,
		enum qed_int_mode int_mode,
		bool allow_npar_tx_switch,
		const u8 *bin_fw_data)
{
	u32 load_code, param;
	int rc, mfw_rc, i;

	if ((int_mode == QED_INT_MODE_MSI) && (cdev->num_hwfns > 1)) {
		DP_NOTICE(cdev, "MSI mode is not supported for CMT devices\n");
		return -EINVAL;
	}

	if (IS_PF(cdev)) {
		rc = qed_init_fw_data(cdev, bin_fw_data);
		if (rc != 0)
			return rc;
	}

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		if (IS_VF(cdev)) {
			p_hwfn->b_int_enabled = 1;
			continue;
		}

		/* Enable DMAE in PXP */
		rc = qed_change_pci_hwfn(p_hwfn, p_hwfn->p_main_ptt, true);

		qed_calc_hw_mode(p_hwfn);

		rc = qed_mcp_load_req(p_hwfn, p_hwfn->p_main_ptt,
				      &load_code);
		if (rc) {
			DP_NOTICE(p_hwfn, "Failed sending LOAD_REQ command\n");
			return rc;
		}

		qed_reset_mb_shadow(p_hwfn, p_hwfn->p_main_ptt);

		DP_VERBOSE(p_hwfn, QED_MSG_SP,
			   "Load request was sent. Resp:0x%x, Load code: 0x%x\n",
			   rc, load_code);

		p_hwfn->first_on_engine = (load_code ==
					   FW_MSG_CODE_DRV_LOAD_ENGINE);

		if (!qm_lock_init) {
			spin_lock_init(&qm_lock);
			qm_lock_init = true;
		}

		switch (load_code) {
		case FW_MSG_CODE_DRV_LOAD_ENGINE:
			rc = qed_hw_init_common(p_hwfn, p_hwfn->p_main_ptt,
						p_hwfn->hw_info.hw_mode);
			if (rc)
				break;
		/* Fall into */
		case FW_MSG_CODE_DRV_LOAD_PORT:
			rc = qed_hw_init_port(p_hwfn, p_hwfn->p_main_ptt,
					      p_hwfn->hw_info.hw_mode);
			if (rc)
				break;

		/* Fall into */
		case FW_MSG_CODE_DRV_LOAD_FUNCTION:
			rc = qed_hw_init_pf(p_hwfn, p_hwfn->p_main_ptt,
					    p_tunn, p_hwfn->hw_info.hw_mode,
					    b_hw_start, int_mode,
					    allow_npar_tx_switch);
			break;
		default:
			rc = -EINVAL;
			break;
		}

		if (rc)
			DP_NOTICE(p_hwfn,
				  "init phase failed for loadcode 0x%x (rc %d)\n",
				   load_code, rc);

		/* ACK mfw regardless of success or failure of initialization */
		mfw_rc = qed_mcp_cmd(p_hwfn, p_hwfn->p_main_ptt,
				     DRV_MSG_CODE_LOAD_DONE,
				     0, &load_code, &param);
		if (rc)
			return rc;
		if (mfw_rc) {
			DP_NOTICE(p_hwfn, "Failed sending LOAD_DONE command\n");
			return mfw_rc;
		}

		/* send DCBX attention request command */
		DP_VERBOSE(p_hwfn,
			   QED_MSG_DCB,
			   "sending phony dcbx set command to trigger DCBx attention handling\n");
		mfw_rc = qed_mcp_cmd(p_hwfn, p_hwfn->p_main_ptt,
				     DRV_MSG_CODE_SET_DCBX,
				     1 << DRV_MB_PARAM_DCBX_NOTIFY_SHIFT,
				     &load_code, &param);
		if (mfw_rc) {
			DP_NOTICE(p_hwfn,
				  "Failed to send DCBX attention request\n");
			return mfw_rc;
		}

		p_hwfn->hw_init_done = true;
	}

	return 0;
}

#define QED_HW_STOP_RETRY_LIMIT (10)
static inline void qed_hw_timers_stop(struct qed_dev *cdev,
				      struct qed_hwfn *p_hwfn,
				      struct qed_ptt *p_ptt)
{
	int i;

	/* close timers */
	qed_wr(p_hwfn, p_ptt, TM_REG_PF_ENABLE_CONN, 0x0);
	qed_wr(p_hwfn, p_ptt, TM_REG_PF_ENABLE_TASK, 0x0);

	for (i = 0; i < QED_HW_STOP_RETRY_LIMIT; i++) {
		if ((!qed_rd(p_hwfn, p_ptt,
			     TM_REG_PF_SCAN_ACTIVE_CONN)) &&
		    (!qed_rd(p_hwfn, p_ptt,
			     TM_REG_PF_SCAN_ACTIVE_TASK)))
			break;

		/* Dependent on number of connection/tasks, possibly
		 * 1ms sleep is required between polls
		 */
		usleep_range(1000, 2000);
	}

	if (i < QED_HW_STOP_RETRY_LIMIT)
		return;

	DP_NOTICE(p_hwfn,
		  "Timers linear scans are not over [Connection %02x Tasks %02x]\n",
		  (u8)qed_rd(p_hwfn, p_ptt, TM_REG_PF_SCAN_ACTIVE_CONN),
		  (u8)qed_rd(p_hwfn, p_ptt, TM_REG_PF_SCAN_ACTIVE_TASK));
}

void qed_hw_timers_stop_all(struct qed_dev *cdev)
{
	int j;

	for_each_hwfn(cdev, j) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[j];
		struct qed_ptt *p_ptt = p_hwfn->p_main_ptt;

		qed_hw_timers_stop(cdev, p_hwfn, p_ptt);
	}
}

int qed_hw_stop(struct qed_dev *cdev)
{
	int rc = 0, t_rc;
	int j;

	for_each_hwfn(cdev, j) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[j];
		struct qed_ptt *p_ptt = p_hwfn->p_main_ptt;

		DP_VERBOSE(p_hwfn, NETIF_MSG_IFDOWN, "Stopping hw/fw\n");

		if (IS_VF(cdev)) {
			qed_vf_pf_int_cleanup(p_hwfn);
			continue;
		}

		/* mark the hw as uninitialized... */
		p_hwfn->hw_init_done = false;

		rc = qed_sp_pf_stop(p_hwfn);
		if (rc)
			DP_NOTICE(p_hwfn,
				  "Failed to close PF against FW. Continue to stop HW to prevent illegal host access by the device\n");

		qed_wr(p_hwfn, p_ptt,
		       NIG_REG_RX_LLH_BRB_GATE_DNTFWD_PERPF, 0x1);

		qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_TCP, 0x0);
		qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_UDP, 0x0);
		qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_FCOE, 0x0);
		qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_ROCE, 0x0);
		qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_OPENFLOW, 0x0);

		qed_hw_timers_stop(cdev, p_hwfn, p_ptt);

		/* Disable Attention Generation */
		qed_int_igu_disable_int(p_hwfn, p_ptt);

		qed_wr(p_hwfn, p_ptt, IGU_REG_LEADING_EDGE_LATCH, 0);
		qed_wr(p_hwfn, p_ptt, IGU_REG_TRAILING_EDGE_LATCH, 0);

		qed_int_igu_init_pure_rt(p_hwfn, p_ptt, false, true);

		/* Need to wait 1ms to guarantee SBs are cleared */
		usleep_range(1000, 2000);
	}

	if (IS_PF(cdev)) {
		/* Disable DMAE in PXP - in CMT, this should only be done for
		 * first hw-function, and only after all transactions have
		 * stopped for all active hw-functions.
		 */
		t_rc = qed_change_pci_hwfn(&cdev->hwfns[0],
					   cdev->hwfns[0].p_main_ptt, false);
		if (t_rc != 0)
			rc = t_rc;
	}

	return rc;
}

void qed_hw_stop_fastpath(struct qed_dev *cdev)
{
	int j;

	for_each_hwfn(cdev, j) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[j];
		struct qed_ptt *p_ptt = p_hwfn->p_main_ptt;

		if (IS_VF(cdev)) {
			qed_vf_pf_int_cleanup(p_hwfn);
			continue;
		}

		DP_VERBOSE(p_hwfn,
			   NETIF_MSG_IFDOWN,
			   "Shutting down the fastpath\n");

		qed_wr(p_hwfn, p_ptt,
		       NIG_REG_RX_LLH_BRB_GATE_DNTFWD_PERPF, 0x1);

		qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_TCP, 0x0);
		qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_UDP, 0x0);
		qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_FCOE, 0x0);
		qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_ROCE, 0x0);
		qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_OPENFLOW, 0x0);

		qed_int_igu_init_pure_rt(p_hwfn, p_ptt, false, false);

		/* Need to wait 1ms to guarantee SBs are cleared */
		usleep_range(1000, 2000);
	}
}

void qed_hw_start_fastpath(struct qed_hwfn *p_hwfn)
{
	if (IS_VF(p_hwfn->cdev))
		return;

	/* Re-open incoming traffic */
	qed_wr(p_hwfn, p_hwfn->p_main_ptt,
	       NIG_REG_RX_LLH_BRB_GATE_DNTFWD_PERPF, 0x0);
}

static int qed_reg_assert(struct qed_hwfn *hwfn,
			  struct qed_ptt *ptt, u32 reg,
			  bool expected)
{
	u32 assert_val = qed_rd(hwfn, ptt, reg);

	if (assert_val != expected) {
		DP_NOTICE(hwfn, "Value at address 0x%x != 0x%08x\n",
			  reg, expected);
		return -EINVAL;
	}

	return 0;
}

int qed_hw_reset(struct qed_dev *cdev)
{
	int rc = 0;
	u32 unload_resp, unload_param;
	int i;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		if (IS_VF(cdev)) {
			rc = qed_vf_pf_reset(p_hwfn);
			if (rc)
				return rc;
			continue;
		}

		DP_VERBOSE(p_hwfn, NETIF_MSG_IFDOWN, "Resetting hw/fw\n");

		/* Check for incorrect states */
		qed_reg_assert(p_hwfn, p_hwfn->p_main_ptt,
			       QM_REG_USG_CNT_PF_TX, 0);
		qed_reg_assert(p_hwfn, p_hwfn->p_main_ptt,
			       QM_REG_USG_CNT_PF_OTHER, 0);

		/* Disable PF in HW blocks */
		qed_wr(p_hwfn, p_hwfn->p_main_ptt, DORQ_REG_PF_DB_ENABLE, 0);
		qed_wr(p_hwfn, p_hwfn->p_main_ptt, QM_REG_PF_EN, 0);
		qed_wr(p_hwfn, p_hwfn->p_main_ptt,
		       TCFC_REG_STRONG_ENABLE_PF, 0);
		qed_wr(p_hwfn, p_hwfn->p_main_ptt,
		       CCFC_REG_STRONG_ENABLE_PF, 0);

		/* Send unload command to MCP */
		rc = qed_mcp_cmd(p_hwfn, p_hwfn->p_main_ptt,
				 DRV_MSG_CODE_UNLOAD_REQ,
				 DRV_MB_PARAM_UNLOAD_WOL_MCP,
				 &unload_resp, &unload_param);
		if (rc) {
			DP_NOTICE(p_hwfn, "qed_hw_reset: UNLOAD_REQ failed\n");
			unload_resp = FW_MSG_CODE_DRV_UNLOAD_ENGINE;
		}

		rc = qed_mcp_cmd(p_hwfn, p_hwfn->p_main_ptt,
				 DRV_MSG_CODE_UNLOAD_DONE,
				 0, &unload_resp, &unload_param);
		if (rc) {
			DP_NOTICE(p_hwfn, "qed_hw_reset: UNLOAD_DONE failed\n");
			return rc;
		}
	}

	return rc;
}

/* Free hwfn memory and resources acquired in hw_hwfn_prepare */
static void qed_hw_hwfn_free(struct qed_hwfn *p_hwfn)
{
	qed_ptt_pool_free(p_hwfn);
	kfree(p_hwfn->hw_info.p_igu_info);
}

/* Setup bar access */
static void qed_hw_hwfn_prepare(struct qed_hwfn *p_hwfn)
{
	/* clear indirect access */
	qed_wr(p_hwfn, p_hwfn->p_main_ptt, PGLUE_B_REG_PGL_ADDR_88_F0, 0);
	qed_wr(p_hwfn, p_hwfn->p_main_ptt, PGLUE_B_REG_PGL_ADDR_8C_F0, 0);
	qed_wr(p_hwfn, p_hwfn->p_main_ptt, PGLUE_B_REG_PGL_ADDR_90_F0, 0);
	qed_wr(p_hwfn, p_hwfn->p_main_ptt, PGLUE_B_REG_PGL_ADDR_94_F0, 0);

	/* Clean Previous errors if such exist */
	qed_wr(p_hwfn, p_hwfn->p_main_ptt,
	       PGLUE_B_REG_WAS_ERROR_PF_31_0_CLR,
	       1 << p_hwfn->abs_pf_id);

	/* enable internal target-read */
	qed_wr(p_hwfn, p_hwfn->p_main_ptt,
	       PGLUE_B_REG_INTERNAL_PFID_ENABLE_TARGET_READ, 1);
}

static void get_function_id(struct qed_hwfn *p_hwfn)
{
	/* ME Register */
	p_hwfn->hw_info.opaque_fid = (u16)REG_RD(p_hwfn, PXP_PF_ME_OPAQUE_ADDR);

	p_hwfn->hw_info.concrete_fid = REG_RD(p_hwfn, PXP_PF_ME_CONCRETE_ADDR);

	p_hwfn->abs_pf_id = (p_hwfn->hw_info.concrete_fid >> 16) & 0xf;
	p_hwfn->rel_pf_id = GET_FIELD(p_hwfn->hw_info.concrete_fid,
				      PXP_CONCRETE_FID_PFID);
	p_hwfn->port_id = GET_FIELD(p_hwfn->hw_info.concrete_fid,
				    PXP_CONCRETE_FID_PORT);
}

static void qed_hw_set_feat(struct qed_hwfn *p_hwfn)
{
	u32 *feat_num = p_hwfn->hw_info.feat_num;
	int num_features = 1;

	feat_num[QED_PF_L2_QUE] = min_t(u32, RESC_NUM(p_hwfn, QED_SB) /
						num_features,
					RESC_NUM(p_hwfn, QED_L2_QUEUE));
	DP_VERBOSE(p_hwfn, NETIF_MSG_PROBE,
		   "#PF_L2_QUEUES=%d #SBS=%d num_features=%d\n",
		   feat_num[QED_PF_L2_QUE], RESC_NUM(p_hwfn, QED_SB),
		   num_features);
}

static void qed_hw_get_resc(struct qed_hwfn *p_hwfn)
{
	u32 *resc_start = p_hwfn->hw_info.resc_start;
	u8 num_funcs = p_hwfn->num_funcs_on_engine;
	u32 *resc_num = p_hwfn->hw_info.resc_num;
	struct qed_sb_cnt_info sb_cnt_info;
	int i, max_vf_vlan_filters;

	memset(&sb_cnt_info, 0, sizeof(sb_cnt_info));

#ifdef CONFIG_QED_SRIOV
	max_vf_vlan_filters = QED_ETH_MAX_VF_NUM_VLAN_FILTERS;
#else
	max_vf_vlan_filters = 0;
#endif

	qed_int_get_num_sbs(p_hwfn, &sb_cnt_info);

	resc_num[QED_SB] = min_t(u32,
				 (MAX_SB_PER_PATH_BB / num_funcs),
				 sb_cnt_info.sb_cnt);
	resc_num[QED_L2_QUEUE] = MAX_NUM_L2_QUEUES_BB / num_funcs;
	resc_num[QED_VPORT] = MAX_NUM_VPORTS_BB / num_funcs;
	resc_num[QED_RSS_ENG] = ETH_RSS_ENGINE_NUM_BB / num_funcs;
	resc_num[QED_PQ] = MAX_QM_TX_QUEUES_BB / num_funcs;
	resc_num[QED_RL] = 8;
	resc_num[QED_MAC] = ETH_NUM_MAC_FILTERS / num_funcs;
	resc_num[QED_VLAN] = (ETH_NUM_VLAN_FILTERS - 1 /*For vlan0*/) /
			     num_funcs;
	resc_num[QED_ILT] = 950;

	for (i = 0; i < QED_MAX_RESC; i++)
		resc_start[i] = resc_num[i] * p_hwfn->rel_pf_id;

	qed_hw_set_feat(p_hwfn);

	DP_VERBOSE(p_hwfn, NETIF_MSG_PROBE,
		   "The numbers for each resource are:\n"
		   "SB = %d start = %d\n"
		   "L2_QUEUE = %d start = %d\n"
		   "VPORT = %d start = %d\n"
		   "PQ = %d start = %d\n"
		   "RL = %d start = %d\n"
		   "MAC = %d start = %d\n"
		   "VLAN = %d start = %d\n"
		   "ILT = %d start = %d\n",
		   p_hwfn->hw_info.resc_num[QED_SB],
		   p_hwfn->hw_info.resc_start[QED_SB],
		   p_hwfn->hw_info.resc_num[QED_L2_QUEUE],
		   p_hwfn->hw_info.resc_start[QED_L2_QUEUE],
		   p_hwfn->hw_info.resc_num[QED_VPORT],
		   p_hwfn->hw_info.resc_start[QED_VPORT],
		   p_hwfn->hw_info.resc_num[QED_PQ],
		   p_hwfn->hw_info.resc_start[QED_PQ],
		   p_hwfn->hw_info.resc_num[QED_RL],
		   p_hwfn->hw_info.resc_start[QED_RL],
		   p_hwfn->hw_info.resc_num[QED_MAC],
		   p_hwfn->hw_info.resc_start[QED_MAC],
		   p_hwfn->hw_info.resc_num[QED_VLAN],
		   p_hwfn->hw_info.resc_start[QED_VLAN],
		   p_hwfn->hw_info.resc_num[QED_ILT],
		   p_hwfn->hw_info.resc_start[QED_ILT]);
}

static int qed_hw_get_nvm_info(struct qed_hwfn *p_hwfn,
			       struct qed_ptt *p_ptt)
{
	u32 nvm_cfg1_offset, mf_mode, addr, generic_cont0, core_cfg;
	u32 port_cfg_addr, link_temp, nvm_cfg_addr, device_capabilities;
	struct qed_mcp_link_params *link;

	/* Read global nvm_cfg address */
	nvm_cfg_addr = qed_rd(p_hwfn, p_ptt, MISC_REG_GEN_PURP_CR0);

	/* Verify MCP has initialized it */
	if (!nvm_cfg_addr) {
		DP_NOTICE(p_hwfn, "Shared memory not initialized\n");
		return -EINVAL;
	}

	/* Read nvm_cfg1  (Notice this is just offset, and not offsize (TBD) */
	nvm_cfg1_offset = qed_rd(p_hwfn, p_ptt, nvm_cfg_addr + 4);

	addr = MCP_REG_SCRATCH + nvm_cfg1_offset +
	       offsetof(struct nvm_cfg1, glob) +
	       offsetof(struct nvm_cfg1_glob, core_cfg);

	core_cfg = qed_rd(p_hwfn, p_ptt, addr);

	switch ((core_cfg & NVM_CFG1_GLOB_NETWORK_PORT_MODE_MASK) >>
		NVM_CFG1_GLOB_NETWORK_PORT_MODE_OFFSET) {
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_BB_2X40G:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_2X40G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_2X50G:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_2X50G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_BB_1X100G:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_1X100G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_4X10G_F:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_4X10G_F;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_BB_4X10G_E:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_4X10G_E;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_BB_4X20G:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_4X20G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_1X40G:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_1X40G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_2X25G:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_2X25G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_1X25G:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_1X25G;
		break;
	default:
		DP_NOTICE(p_hwfn, "Unknown port mode in 0x%08x\n",
			  core_cfg);
		break;
	}

	/* Read default link configuration */
	link = &p_hwfn->mcp_info->link_input;
	port_cfg_addr = MCP_REG_SCRATCH + nvm_cfg1_offset +
			offsetof(struct nvm_cfg1, port[MFW_PORT(p_hwfn)]);
	link_temp = qed_rd(p_hwfn, p_ptt,
			   port_cfg_addr +
			   offsetof(struct nvm_cfg1_port, speed_cap_mask));
	link->speed.advertised_speeds =
		link_temp & NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_MASK;

	p_hwfn->mcp_info->link_capabilities.speed_capabilities =
						link->speed.advertised_speeds;

	link_temp = qed_rd(p_hwfn, p_ptt,
			   port_cfg_addr +
			   offsetof(struct nvm_cfg1_port, link_settings));
	switch ((link_temp & NVM_CFG1_PORT_DRV_LINK_SPEED_MASK) >>
		NVM_CFG1_PORT_DRV_LINK_SPEED_OFFSET) {
	case NVM_CFG1_PORT_DRV_LINK_SPEED_AUTONEG:
		link->speed.autoneg = true;
		break;
	case NVM_CFG1_PORT_DRV_LINK_SPEED_1G:
		link->speed.forced_speed = 1000;
		break;
	case NVM_CFG1_PORT_DRV_LINK_SPEED_10G:
		link->speed.forced_speed = 10000;
		break;
	case NVM_CFG1_PORT_DRV_LINK_SPEED_25G:
		link->speed.forced_speed = 25000;
		break;
	case NVM_CFG1_PORT_DRV_LINK_SPEED_40G:
		link->speed.forced_speed = 40000;
		break;
	case NVM_CFG1_PORT_DRV_LINK_SPEED_50G:
		link->speed.forced_speed = 50000;
		break;
	case NVM_CFG1_PORT_DRV_LINK_SPEED_BB_100G:
		link->speed.forced_speed = 100000;
		break;
	default:
		DP_NOTICE(p_hwfn, "Unknown Speed in 0x%08x\n",
			  link_temp);
	}

	link_temp &= NVM_CFG1_PORT_DRV_FLOW_CONTROL_MASK;
	link_temp >>= NVM_CFG1_PORT_DRV_FLOW_CONTROL_OFFSET;
	link->pause.autoneg = !!(link_temp &
				 NVM_CFG1_PORT_DRV_FLOW_CONTROL_AUTONEG);
	link->pause.forced_rx = !!(link_temp &
				   NVM_CFG1_PORT_DRV_FLOW_CONTROL_RX);
	link->pause.forced_tx = !!(link_temp &
				   NVM_CFG1_PORT_DRV_FLOW_CONTROL_TX);
	link->loopback_mode = 0;

	DP_VERBOSE(p_hwfn, NETIF_MSG_LINK,
		   "Read default link: Speed 0x%08x, Adv. Speed 0x%08x, AN: 0x%02x, PAUSE AN: 0x%02x\n",
		   link->speed.forced_speed, link->speed.advertised_speeds,
		   link->speed.autoneg, link->pause.autoneg);

	/* Read Multi-function information from shmem */
	addr = MCP_REG_SCRATCH + nvm_cfg1_offset +
	       offsetof(struct nvm_cfg1, glob) +
	       offsetof(struct nvm_cfg1_glob, generic_cont0);

	generic_cont0 = qed_rd(p_hwfn, p_ptt, addr);

	mf_mode = (generic_cont0 & NVM_CFG1_GLOB_MF_MODE_MASK) >>
		  NVM_CFG1_GLOB_MF_MODE_OFFSET;

	switch (mf_mode) {
	case NVM_CFG1_GLOB_MF_MODE_MF_ALLOWED:
		p_hwfn->cdev->mf_mode = QED_MF_OVLAN;
		break;
	case NVM_CFG1_GLOB_MF_MODE_NPAR1_0:
		p_hwfn->cdev->mf_mode = QED_MF_NPAR;
		break;
	case NVM_CFG1_GLOB_MF_MODE_DEFAULT:
		p_hwfn->cdev->mf_mode = QED_MF_DEFAULT;
		break;
	}
	DP_INFO(p_hwfn, "Multi function mode is %08x\n",
		p_hwfn->cdev->mf_mode);

	/* Read Multi-function information from shmem */
	addr = MCP_REG_SCRATCH + nvm_cfg1_offset +
		offsetof(struct nvm_cfg1, glob) +
		offsetof(struct nvm_cfg1_glob, device_capabilities);

	device_capabilities = qed_rd(p_hwfn, p_ptt, addr);
	if (device_capabilities & NVM_CFG1_GLOB_DEVICE_CAPABILITIES_ETHERNET)
		__set_bit(QED_DEV_CAP_ETH,
			  &p_hwfn->hw_info.device_capabilities);
	if (device_capabilities & NVM_CFG1_GLOB_DEVICE_CAPABILITIES_ISCSI)
		__set_bit(QED_DEV_CAP_ISCSI,
			  &p_hwfn->hw_info.device_capabilities);
	if (device_capabilities & NVM_CFG1_GLOB_DEVICE_CAPABILITIES_ROCE)
		__set_bit(QED_DEV_CAP_ROCE,
			  &p_hwfn->hw_info.device_capabilities);

	return qed_mcp_fill_shmem_func_info(p_hwfn, p_ptt);
}

static void qed_get_num_funcs(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	u32 reg_function_hide, tmp, eng_mask;
	u8 num_funcs;

	num_funcs = MAX_NUM_PFS_BB;

	/* Bit 0 of MISCS_REG_FUNCTION_HIDE indicates whether the bypass values
	 * in the other bits are selected.
	 * Bits 1-15 are for functions 1-15, respectively, and their value is
	 * '0' only for enabled functions (function 0 always exists and
	 * enabled).
	 * In case of CMT, only the "even" functions are enabled, and thus the
	 * number of functions for both hwfns is learnt from the same bits.
	 */
	reg_function_hide = qed_rd(p_hwfn, p_ptt, MISCS_REG_FUNCTION_HIDE);

	if (reg_function_hide & 0x1) {
		if (QED_PATH_ID(p_hwfn) && p_hwfn->cdev->num_hwfns == 1) {
			num_funcs = 0;
			eng_mask = 0xaaaa;
		} else {
			num_funcs = 1;
			eng_mask = 0x5554;
		}

		/* Get the number of the enabled functions on the engine */
		tmp = (reg_function_hide ^ 0xffffffff) & eng_mask;
		while (tmp) {
			if (tmp & 0x1)
				num_funcs++;
			tmp >>= 0x1;
		}
	}

	p_hwfn->num_funcs_on_engine = num_funcs;

	DP_VERBOSE(p_hwfn,
		   NETIF_MSG_PROBE,
		   "PF [rel_id %d, abs_id %d] within the %d enabled functions on the engine\n",
		   p_hwfn->rel_pf_id,
		   p_hwfn->abs_pf_id,
		   p_hwfn->num_funcs_on_engine);
}

static int
qed_get_hw_info(struct qed_hwfn *p_hwfn,
		struct qed_ptt *p_ptt,
		enum qed_pci_personality personality)
{
	u32 port_mode;
	int rc;

	/* Since all information is common, only first hwfns should do this */
	if (IS_LEAD_HWFN(p_hwfn)) {
		rc = qed_iov_hw_info(p_hwfn);
		if (rc)
			return rc;
	}

	/* Read the port mode */
	port_mode = qed_rd(p_hwfn, p_ptt,
			   CNIG_REG_NW_PORT_MODE_BB_B0);

	if (port_mode < 3) {
		p_hwfn->cdev->num_ports_in_engines = 1;
	} else if (port_mode <= 5) {
		p_hwfn->cdev->num_ports_in_engines = 2;
	} else {
		DP_NOTICE(p_hwfn, "PORT MODE: %d not supported\n",
			  p_hwfn->cdev->num_ports_in_engines);

		/* Default num_ports_in_engines to something */
		p_hwfn->cdev->num_ports_in_engines = 1;
	}

	qed_hw_get_nvm_info(p_hwfn, p_ptt);

	rc = qed_int_igu_read_cam(p_hwfn, p_ptt);
	if (rc)
		return rc;

	if (qed_mcp_is_init(p_hwfn))
		ether_addr_copy(p_hwfn->hw_info.hw_mac_addr,
				p_hwfn->mcp_info->func_info.mac);
	else
		eth_random_addr(p_hwfn->hw_info.hw_mac_addr);

	if (qed_mcp_is_init(p_hwfn)) {
		if (p_hwfn->mcp_info->func_info.ovlan != QED_MCP_VLAN_UNSET)
			p_hwfn->hw_info.ovlan =
				p_hwfn->mcp_info->func_info.ovlan;

		qed_mcp_cmd_port_init(p_hwfn, p_ptt);
	}

	if (qed_mcp_is_init(p_hwfn)) {
		enum qed_pci_personality protocol;

		protocol = p_hwfn->mcp_info->func_info.protocol;
		p_hwfn->hw_info.personality = protocol;
	}

	qed_get_num_funcs(p_hwfn, p_ptt);

	qed_hw_get_resc(p_hwfn);

	return rc;
}

static int qed_get_dev_info(struct qed_dev *cdev)
{
	struct qed_hwfn *p_hwfn = QED_LEADING_HWFN(cdev);
	u32 tmp;

	/* Read Vendor Id / Device Id */
	pci_read_config_word(cdev->pdev, PCI_VENDOR_ID,
			     &cdev->vendor_id);
	pci_read_config_word(cdev->pdev, PCI_DEVICE_ID,
			     &cdev->device_id);
	cdev->chip_num = (u16)qed_rd(p_hwfn, p_hwfn->p_main_ptt,
				     MISCS_REG_CHIP_NUM);
	cdev->chip_rev = (u16)qed_rd(p_hwfn, p_hwfn->p_main_ptt,
				     MISCS_REG_CHIP_REV);
	MASK_FIELD(CHIP_REV, cdev->chip_rev);

	cdev->type = QED_DEV_TYPE_BB;
	/* Learn number of HW-functions */
	tmp = qed_rd(p_hwfn, p_hwfn->p_main_ptt,
		     MISCS_REG_CMT_ENABLED_FOR_PAIR);

	if (tmp & (1 << p_hwfn->rel_pf_id)) {
		DP_NOTICE(cdev->hwfns, "device in CMT mode\n");
		cdev->num_hwfns = 2;
	} else {
		cdev->num_hwfns = 1;
	}

	cdev->chip_bond_id = qed_rd(p_hwfn, p_hwfn->p_main_ptt,
				    MISCS_REG_CHIP_TEST_REG) >> 4;
	MASK_FIELD(CHIP_BOND_ID, cdev->chip_bond_id);
	cdev->chip_metal = (u16)qed_rd(p_hwfn, p_hwfn->p_main_ptt,
				       MISCS_REG_CHIP_METAL);
	MASK_FIELD(CHIP_METAL, cdev->chip_metal);

	DP_INFO(cdev->hwfns,
		"Chip details - Num: %04x Rev: %04x Bond id: %04x Metal: %04x\n",
		cdev->chip_num, cdev->chip_rev,
		cdev->chip_bond_id, cdev->chip_metal);

	if (QED_IS_BB(cdev) && CHIP_REV_IS_A0(cdev)) {
		DP_NOTICE(cdev->hwfns,
			  "The chip type/rev (BB A0) is not supported!\n");
		return -EINVAL;
	}

	return 0;
}

static int qed_hw_prepare_single(struct qed_hwfn *p_hwfn,
				 void __iomem *p_regview,
				 void __iomem *p_doorbells,
				 enum qed_pci_personality personality)
{
	int rc = 0;

	/* Split PCI bars evenly between hwfns */
	p_hwfn->regview = p_regview;
	p_hwfn->doorbells = p_doorbells;

	if (IS_VF(p_hwfn->cdev))
		return qed_vf_hw_prepare(p_hwfn);

	/* Validate that chip access is feasible */
	if (REG_RD(p_hwfn, PXP_PF_ME_OPAQUE_ADDR) == 0xffffffff) {
		DP_ERR(p_hwfn,
		       "Reading the ME register returns all Fs; Preventing further chip access\n");
		return -EINVAL;
	}

	get_function_id(p_hwfn);

	/* Allocate PTT pool */
	rc = qed_ptt_pool_alloc(p_hwfn);
	if (rc) {
		DP_NOTICE(p_hwfn, "Failed to prepare hwfn's hw\n");
		goto err0;
	}

	/* Allocate the main PTT */
	p_hwfn->p_main_ptt = qed_get_reserved_ptt(p_hwfn, RESERVED_PTT_MAIN);

	/* First hwfn learns basic information, e.g., number of hwfns */
	if (!p_hwfn->my_id) {
		rc = qed_get_dev_info(p_hwfn->cdev);
		if (rc != 0)
			goto err1;
	}

	qed_hw_hwfn_prepare(p_hwfn);

	/* Initialize MCP structure */
	rc = qed_mcp_cmd_init(p_hwfn, p_hwfn->p_main_ptt);
	if (rc) {
		DP_NOTICE(p_hwfn, "Failed initializing mcp command\n");
		goto err1;
	}

	/* Read the device configuration information from the HW and SHMEM */
	rc = qed_get_hw_info(p_hwfn, p_hwfn->p_main_ptt, personality);
	if (rc) {
		DP_NOTICE(p_hwfn, "Failed to get HW information\n");
		goto err2;
	}

	/* Allocate the init RT array and initialize the init-ops engine */
	rc = qed_init_alloc(p_hwfn);
	if (rc) {
		DP_NOTICE(p_hwfn, "Failed to allocate the init array\n");
		goto err2;
	}

	return rc;
err2:
	if (IS_LEAD_HWFN(p_hwfn))
		qed_iov_free_hw_info(p_hwfn->cdev);
	qed_mcp_free(p_hwfn);
err1:
	qed_hw_hwfn_free(p_hwfn);
err0:
	return rc;
}

int qed_hw_prepare(struct qed_dev *cdev,
		   int personality)
{
	struct qed_hwfn *p_hwfn = QED_LEADING_HWFN(cdev);
	int rc;

	/* Store the precompiled init data ptrs */
	if (IS_PF(cdev))
		qed_init_iro_array(cdev);

	/* Initialize the first hwfn - will learn number of hwfns */
	rc = qed_hw_prepare_single(p_hwfn,
				   cdev->regview,
				   cdev->doorbells, personality);
	if (rc)
		return rc;

	personality = p_hwfn->hw_info.personality;

	/* Initialize the rest of the hwfns */
	if (cdev->num_hwfns > 1) {
		void __iomem *p_regview, *p_doorbell;
		u8 __iomem *addr;

		/* adjust bar offset for second engine */
		addr = cdev->regview + qed_hw_bar_size(p_hwfn, BAR_ID_0) / 2;
		p_regview = addr;

		/* adjust doorbell bar offset for second engine */
		addr = cdev->doorbells + qed_hw_bar_size(p_hwfn, BAR_ID_1) / 2;
		p_doorbell = addr;

		/* prepare second hw function */
		rc = qed_hw_prepare_single(&cdev->hwfns[1], p_regview,
					   p_doorbell, personality);

		/* in case of error, need to free the previously
		 * initiliazed hwfn 0.
		 */
		if (rc) {
			if (IS_PF(cdev)) {
				qed_init_free(p_hwfn);
				qed_mcp_free(p_hwfn);
				qed_hw_hwfn_free(p_hwfn);
			}
		}
	}

	return rc;
}

void qed_hw_remove(struct qed_dev *cdev)
{
	int i;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		if (IS_VF(cdev)) {
			qed_vf_pf_release(p_hwfn);
			continue;
		}

		qed_init_free(p_hwfn);
		qed_hw_hwfn_free(p_hwfn);
		qed_mcp_free(p_hwfn);
	}

	qed_iov_free_hw_info(cdev);
}

static void qed_chain_free_next_ptr(struct qed_dev *cdev,
				    struct qed_chain *p_chain)
{
	void *p_virt = p_chain->p_virt_addr, *p_virt_next = NULL;
	dma_addr_t p_phys = p_chain->p_phys_addr, p_phys_next = 0;
	struct qed_chain_next *p_next;
	u32 size, i;

	if (!p_virt)
		return;

	size = p_chain->elem_size * p_chain->usable_per_page;

	for (i = 0; i < p_chain->page_cnt; i++) {
		if (!p_virt)
			break;

		p_next = (struct qed_chain_next *)((u8 *)p_virt + size);
		p_virt_next = p_next->next_virt;
		p_phys_next = HILO_DMA_REGPAIR(p_next->next_phys);

		dma_free_coherent(&cdev->pdev->dev,
				  QED_CHAIN_PAGE_SIZE, p_virt, p_phys);

		p_virt = p_virt_next;
		p_phys = p_phys_next;
	}
}

static void qed_chain_free_single(struct qed_dev *cdev,
				  struct qed_chain *p_chain)
{
	if (!p_chain->p_virt_addr)
		return;

	dma_free_coherent(&cdev->pdev->dev,
			  QED_CHAIN_PAGE_SIZE,
			  p_chain->p_virt_addr, p_chain->p_phys_addr);
}

static void qed_chain_free_pbl(struct qed_dev *cdev, struct qed_chain *p_chain)
{
	void **pp_virt_addr_tbl = p_chain->pbl.pp_virt_addr_tbl;
	u32 page_cnt = p_chain->page_cnt, i, pbl_size;
	u8 *p_pbl_virt = p_chain->pbl.p_virt_table;

	if (!pp_virt_addr_tbl)
		return;

	if (!p_chain->pbl.p_virt_table)
		goto out;

	for (i = 0; i < page_cnt; i++) {
		if (!pp_virt_addr_tbl[i])
			break;

		dma_free_coherent(&cdev->pdev->dev,
				  QED_CHAIN_PAGE_SIZE,
				  pp_virt_addr_tbl[i],
				  *(dma_addr_t *)p_pbl_virt);

		p_pbl_virt += QED_CHAIN_PBL_ENTRY_SIZE;
	}

	pbl_size = page_cnt * QED_CHAIN_PBL_ENTRY_SIZE;
	dma_free_coherent(&cdev->pdev->dev,
			  pbl_size,
			  p_chain->pbl.p_virt_table, p_chain->pbl.p_phys_table);
out:
	vfree(p_chain->pbl.pp_virt_addr_tbl);
}

void qed_chain_free(struct qed_dev *cdev, struct qed_chain *p_chain)
{
	switch (p_chain->mode) {
	case QED_CHAIN_MODE_NEXT_PTR:
		qed_chain_free_next_ptr(cdev, p_chain);
		break;
	case QED_CHAIN_MODE_SINGLE:
		qed_chain_free_single(cdev, p_chain);
		break;
	case QED_CHAIN_MODE_PBL:
		qed_chain_free_pbl(cdev, p_chain);
		break;
	}
}

static int
qed_chain_alloc_sanity_check(struct qed_dev *cdev,
			     enum qed_chain_cnt_type cnt_type,
			     size_t elem_size, u32 page_cnt)
{
	u64 chain_size = ELEMS_PER_PAGE(elem_size) * page_cnt;

	/* The actual chain size can be larger than the maximal possible value
	 * after rounding up the requested elements number to pages, and after
	 * taking into acount the unusuable elements (next-ptr elements).
	 * The size of a "u16" chain can be (U16_MAX + 1) since the chain
	 * size/capacity fields are of a u32 type.
	 */
	if ((cnt_type == QED_CHAIN_CNT_TYPE_U16 &&
	     chain_size > 0x10000) ||
	    (cnt_type == QED_CHAIN_CNT_TYPE_U32 &&
	     chain_size > 0x100000000ULL)) {
		DP_NOTICE(cdev,
			  "The actual chain size (0x%llx) is larger than the maximal possible value\n",
			  chain_size);
		return -EINVAL;
	}

	return 0;
}

static int
qed_chain_alloc_next_ptr(struct qed_dev *cdev, struct qed_chain *p_chain)
{
	void *p_virt = NULL, *p_virt_prev = NULL;
	dma_addr_t p_phys = 0;
	u32 i;

	for (i = 0; i < p_chain->page_cnt; i++) {
		p_virt = dma_alloc_coherent(&cdev->pdev->dev,
					    QED_CHAIN_PAGE_SIZE,
					    &p_phys, GFP_KERNEL);
		if (!p_virt) {
			DP_NOTICE(cdev, "Failed to allocate chain memory\n");
			return -ENOMEM;
		}

		if (i == 0) {
			qed_chain_init_mem(p_chain, p_virt, p_phys);
			qed_chain_reset(p_chain);
		} else {
			qed_chain_init_next_ptr_elem(p_chain, p_virt_prev,
						     p_virt, p_phys);
		}

		p_virt_prev = p_virt;
	}
	/* Last page's next element should point to the beginning of the
	 * chain.
	 */
	qed_chain_init_next_ptr_elem(p_chain, p_virt_prev,
				     p_chain->p_virt_addr,
				     p_chain->p_phys_addr);

	return 0;
}

static int
qed_chain_alloc_single(struct qed_dev *cdev, struct qed_chain *p_chain)
{
	dma_addr_t p_phys = 0;
	void *p_virt = NULL;

	p_virt = dma_alloc_coherent(&cdev->pdev->dev,
				    QED_CHAIN_PAGE_SIZE, &p_phys, GFP_KERNEL);
	if (!p_virt) {
		DP_NOTICE(cdev, "Failed to allocate chain memory\n");
		return -ENOMEM;
	}

	qed_chain_init_mem(p_chain, p_virt, p_phys);
	qed_chain_reset(p_chain);

	return 0;
}

static int qed_chain_alloc_pbl(struct qed_dev *cdev, struct qed_chain *p_chain)
{
	u32 page_cnt = p_chain->page_cnt, size, i;
	dma_addr_t p_phys = 0, p_pbl_phys = 0;
	void **pp_virt_addr_tbl = NULL;
	u8 *p_pbl_virt = NULL;
	void *p_virt = NULL;

	size = page_cnt * sizeof(*pp_virt_addr_tbl);
	pp_virt_addr_tbl = vmalloc(size);
	if (!pp_virt_addr_tbl) {
		DP_NOTICE(cdev,
			  "Failed to allocate memory for the chain virtual addresses table\n");
		return -ENOMEM;
	}
	memset(pp_virt_addr_tbl, 0, size);

	/* The allocation of the PBL table is done with its full size, since it
	 * is expected to be successive.
	 * qed_chain_init_pbl_mem() is called even in a case of an allocation
	 * failure, since pp_virt_addr_tbl was previously allocated, and it
	 * should be saved to allow its freeing during the error flow.
	 */
	size = page_cnt * QED_CHAIN_PBL_ENTRY_SIZE;
	p_pbl_virt = dma_alloc_coherent(&cdev->pdev->dev,
					size, &p_pbl_phys, GFP_KERNEL);
	qed_chain_init_pbl_mem(p_chain, p_pbl_virt, p_pbl_phys,
			       pp_virt_addr_tbl);
	if (!p_pbl_virt) {
		DP_NOTICE(cdev, "Failed to allocate chain pbl memory\n");
		return -ENOMEM;
	}

	for (i = 0; i < page_cnt; i++) {
		p_virt = dma_alloc_coherent(&cdev->pdev->dev,
					    QED_CHAIN_PAGE_SIZE,
					    &p_phys, GFP_KERNEL);
		if (!p_virt) {
			DP_NOTICE(cdev, "Failed to allocate chain memory\n");
			return -ENOMEM;
		}

		if (i == 0) {
			qed_chain_init_mem(p_chain, p_virt, p_phys);
			qed_chain_reset(p_chain);
		}

		/* Fill the PBL table with the physical address of the page */
		*(dma_addr_t *)p_pbl_virt = p_phys;
		/* Keep the virtual address of the page */
		p_chain->pbl.pp_virt_addr_tbl[i] = p_virt;

		p_pbl_virt += QED_CHAIN_PBL_ENTRY_SIZE;
	}

	return 0;
}

int qed_chain_alloc(struct qed_dev *cdev,
		    enum qed_chain_use_mode intended_use,
		    enum qed_chain_mode mode,
		    enum qed_chain_cnt_type cnt_type,
		    u32 num_elems, size_t elem_size, struct qed_chain *p_chain)
{
	u32 page_cnt;
	int rc = 0;

	if (mode == QED_CHAIN_MODE_SINGLE)
		page_cnt = 1;
	else
		page_cnt = QED_CHAIN_PAGE_CNT(num_elems, elem_size, mode);

	rc = qed_chain_alloc_sanity_check(cdev, cnt_type, elem_size, page_cnt);
	if (rc) {
		DP_NOTICE(cdev,
			  "Cannot allocate a chain with the given arguments:\n"
			  "[use_mode %d, mode %d, cnt_type %d, num_elems %d, elem_size %zu]\n",
			  intended_use, mode, cnt_type, num_elems, elem_size);
		return rc;
	}

	qed_chain_init_params(p_chain, page_cnt, (u8) elem_size, intended_use,
			      mode, cnt_type);

	switch (mode) {
	case QED_CHAIN_MODE_NEXT_PTR:
		rc = qed_chain_alloc_next_ptr(cdev, p_chain);
		break;
	case QED_CHAIN_MODE_SINGLE:
		rc = qed_chain_alloc_single(cdev, p_chain);
		break;
	case QED_CHAIN_MODE_PBL:
		rc = qed_chain_alloc_pbl(cdev, p_chain);
		break;
	}
	if (rc)
		goto nomem;

	return 0;

nomem:
	qed_chain_free(cdev, p_chain);
	return rc;
}

int qed_fw_l2_queue(struct qed_hwfn *p_hwfn, u16 src_id, u16 *dst_id)
{
	if (src_id >= RESC_NUM(p_hwfn, QED_L2_QUEUE)) {
		u16 min, max;

		min = (u16) RESC_START(p_hwfn, QED_L2_QUEUE);
		max = min + RESC_NUM(p_hwfn, QED_L2_QUEUE);
		DP_NOTICE(p_hwfn,
			  "l2_queue id [%d] is not valid, available indices [%d - %d]\n",
			  src_id, min, max);

		return -EINVAL;
	}

	*dst_id = RESC_START(p_hwfn, QED_L2_QUEUE) + src_id;

	return 0;
}

int qed_fw_vport(struct qed_hwfn *p_hwfn,
		 u8 src_id, u8 *dst_id)
{
	if (src_id >= RESC_NUM(p_hwfn, QED_VPORT)) {
		u8 min, max;

		min = (u8)RESC_START(p_hwfn, QED_VPORT);
		max = min + RESC_NUM(p_hwfn, QED_VPORT);
		DP_NOTICE(p_hwfn,
			  "vport id [%d] is not valid, available indices [%d - %d]\n",
			  src_id, min, max);

		return -EINVAL;
	}

	*dst_id = RESC_START(p_hwfn, QED_VPORT) + src_id;

	return 0;
}

int qed_fw_rss_eng(struct qed_hwfn *p_hwfn,
		   u8 src_id, u8 *dst_id)
{
	if (src_id >= RESC_NUM(p_hwfn, QED_RSS_ENG)) {
		u8 min, max;

		min = (u8)RESC_START(p_hwfn, QED_RSS_ENG);
		max = min + RESC_NUM(p_hwfn, QED_RSS_ENG);
		DP_NOTICE(p_hwfn,
			  "rss_eng id [%d] is not valid, available indices [%d - %d]\n",
			  src_id, min, max);

		return -EINVAL;
	}

	*dst_id = RESC_START(p_hwfn, QED_RSS_ENG) + src_id;

	return 0;
}

/* Calculate final WFQ values for all vports and configure them.
 * After this configuration each vport will have
 * approx min rate =  min_pf_rate * (vport_wfq / QED_WFQ_UNIT)
 */
static void qed_configure_wfq_for_all_vports(struct qed_hwfn *p_hwfn,
					     struct qed_ptt *p_ptt,
					     u32 min_pf_rate)
{
	struct init_qm_vport_params *vport_params;
	int i;

	vport_params = p_hwfn->qm_info.qm_vport_params;

	for (i = 0; i < p_hwfn->qm_info.num_vports; i++) {
		u32 wfq_speed = p_hwfn->qm_info.wfq_data[i].min_speed;

		vport_params[i].vport_wfq = (wfq_speed * QED_WFQ_UNIT) /
						min_pf_rate;
		qed_init_vport_wfq(p_hwfn, p_ptt,
				   vport_params[i].first_tx_pq_id,
				   vport_params[i].vport_wfq);
	}
}

static void qed_init_wfq_default_param(struct qed_hwfn *p_hwfn,
				       u32 min_pf_rate)

{
	int i;

	for (i = 0; i < p_hwfn->qm_info.num_vports; i++)
		p_hwfn->qm_info.qm_vport_params[i].vport_wfq = 1;
}

static void qed_disable_wfq_for_all_vports(struct qed_hwfn *p_hwfn,
					   struct qed_ptt *p_ptt,
					   u32 min_pf_rate)
{
	struct init_qm_vport_params *vport_params;
	int i;

	vport_params = p_hwfn->qm_info.qm_vport_params;

	for (i = 0; i < p_hwfn->qm_info.num_vports; i++) {
		qed_init_wfq_default_param(p_hwfn, min_pf_rate);
		qed_init_vport_wfq(p_hwfn, p_ptt,
				   vport_params[i].first_tx_pq_id,
				   vport_params[i].vport_wfq);
	}
}

/* This function performs several validations for WFQ
 * configuration and required min rate for a given vport
 * 1. req_rate must be greater than one percent of min_pf_rate.
 * 2. req_rate should not cause other vports [not configured for WFQ explicitly]
 *    rates to get less than one percent of min_pf_rate.
 * 3. total_req_min_rate [all vports min rate sum] shouldn't exceed min_pf_rate.
 */
static int qed_init_wfq_param(struct qed_hwfn *p_hwfn,
			      u16 vport_id, u32 req_rate,
			      u32 min_pf_rate)
{
	u32 total_req_min_rate = 0, total_left_rate = 0, left_rate_per_vp = 0;
	int non_requested_count = 0, req_count = 0, i, num_vports;

	num_vports = p_hwfn->qm_info.num_vports;

	/* Accounting for the vports which are configured for WFQ explicitly */
	for (i = 0; i < num_vports; i++) {
		u32 tmp_speed;

		if ((i != vport_id) &&
		    p_hwfn->qm_info.wfq_data[i].configured) {
			req_count++;
			tmp_speed = p_hwfn->qm_info.wfq_data[i].min_speed;
			total_req_min_rate += tmp_speed;
		}
	}

	/* Include current vport data as well */
	req_count++;
	total_req_min_rate += req_rate;
	non_requested_count = num_vports - req_count;

	if (req_rate < min_pf_rate / QED_WFQ_UNIT) {
		DP_VERBOSE(p_hwfn, NETIF_MSG_LINK,
			   "Vport [%d] - Requested rate[%d Mbps] is less than one percent of configured PF min rate[%d Mbps]\n",
			   vport_id, req_rate, min_pf_rate);
		return -EINVAL;
	}

	if (num_vports > QED_WFQ_UNIT) {
		DP_VERBOSE(p_hwfn, NETIF_MSG_LINK,
			   "Number of vports is greater than %d\n",
			   QED_WFQ_UNIT);
		return -EINVAL;
	}

	if (total_req_min_rate > min_pf_rate) {
		DP_VERBOSE(p_hwfn, NETIF_MSG_LINK,
			   "Total requested min rate for all vports[%d Mbps] is greater than configured PF min rate[%d Mbps]\n",
			   total_req_min_rate, min_pf_rate);
		return -EINVAL;
	}

	total_left_rate	= min_pf_rate - total_req_min_rate;

	left_rate_per_vp = total_left_rate / non_requested_count;
	if (left_rate_per_vp <  min_pf_rate / QED_WFQ_UNIT) {
		DP_VERBOSE(p_hwfn, NETIF_MSG_LINK,
			   "Non WFQ configured vports rate [%d Mbps] is less than one percent of configured PF min rate[%d Mbps]\n",
			   left_rate_per_vp, min_pf_rate);
		return -EINVAL;
	}

	p_hwfn->qm_info.wfq_data[vport_id].min_speed = req_rate;
	p_hwfn->qm_info.wfq_data[vport_id].configured = true;

	for (i = 0; i < num_vports; i++) {
		if (p_hwfn->qm_info.wfq_data[i].configured)
			continue;

		p_hwfn->qm_info.wfq_data[i].min_speed = left_rate_per_vp;
	}

	return 0;
}

static int __qed_configure_vport_wfq(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt, u16 vp_id, u32 rate)
{
	struct qed_mcp_link_state *p_link;
	int rc = 0;

	p_link = &p_hwfn->cdev->hwfns[0].mcp_info->link_output;

	if (!p_link->min_pf_rate) {
		p_hwfn->qm_info.wfq_data[vp_id].min_speed = rate;
		p_hwfn->qm_info.wfq_data[vp_id].configured = true;
		return rc;
	}

	rc = qed_init_wfq_param(p_hwfn, vp_id, rate, p_link->min_pf_rate);

	if (rc == 0)
		qed_configure_wfq_for_all_vports(p_hwfn, p_ptt,
						 p_link->min_pf_rate);
	else
		DP_NOTICE(p_hwfn,
			  "Validation failed while configuring min rate\n");

	return rc;
}

static int __qed_configure_vp_wfq_on_link_change(struct qed_hwfn *p_hwfn,
						 struct qed_ptt *p_ptt,
						 u32 min_pf_rate)
{
	bool use_wfq = false;
	int rc = 0;
	u16 i;

	/* Validate all pre configured vports for wfq */
	for (i = 0; i < p_hwfn->qm_info.num_vports; i++) {
		u32 rate;

		if (!p_hwfn->qm_info.wfq_data[i].configured)
			continue;

		rate = p_hwfn->qm_info.wfq_data[i].min_speed;
		use_wfq = true;

		rc = qed_init_wfq_param(p_hwfn, i, rate, min_pf_rate);
		if (rc) {
			DP_NOTICE(p_hwfn,
				  "WFQ validation failed while configuring min rate\n");
			break;
		}
	}

	if (!rc && use_wfq)
		qed_configure_wfq_for_all_vports(p_hwfn, p_ptt, min_pf_rate);
	else
		qed_disable_wfq_for_all_vports(p_hwfn, p_ptt, min_pf_rate);

	return rc;
}

/* Main API for qed clients to configure vport min rate.
 * vp_id - vport id in PF Range[0 - (total_num_vports_per_pf - 1)]
 * rate - Speed in Mbps needs to be assigned to a given vport.
 */
int qed_configure_vport_wfq(struct qed_dev *cdev, u16 vp_id, u32 rate)
{
	int i, rc = -EINVAL;

	/* Currently not supported; Might change in future */
	if (cdev->num_hwfns > 1) {
		DP_NOTICE(cdev,
			  "WFQ configuration is not supported for this device\n");
		return rc;
	}

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];
		struct qed_ptt *p_ptt;

		p_ptt = qed_ptt_acquire(p_hwfn);
		if (!p_ptt)
			return -EBUSY;

		rc = __qed_configure_vport_wfq(p_hwfn, p_ptt, vp_id, rate);

		if (!rc) {
			qed_ptt_release(p_hwfn, p_ptt);
			return rc;
		}

		qed_ptt_release(p_hwfn, p_ptt);
	}

	return rc;
}

/* API to configure WFQ from mcp link change */
void qed_configure_vp_wfq_on_link_change(struct qed_dev *cdev, u32 min_pf_rate)
{
	int i;

	if (cdev->num_hwfns > 1) {
		DP_VERBOSE(cdev,
			   NETIF_MSG_LINK,
			   "WFQ configuration is not supported for this device\n");
		return;
	}

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		__qed_configure_vp_wfq_on_link_change(p_hwfn,
						      p_hwfn->p_dpc_ptt,
						      min_pf_rate);
	}
}

int __qed_configure_pf_max_bandwidth(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     struct qed_mcp_link_state *p_link,
				     u8 max_bw)
{
	int rc = 0;

	p_hwfn->mcp_info->func_info.bandwidth_max = max_bw;

	if (!p_link->line_speed && (max_bw != 100))
		return rc;

	p_link->speed = (p_link->line_speed * max_bw) / 100;
	p_hwfn->qm_info.pf_rl = p_link->speed;

	/* Since the limiter also affects Tx-switched traffic, we don't want it
	 * to limit such traffic in case there's no actual limit.
	 * In that case, set limit to imaginary high boundary.
	 */
	if (max_bw == 100)
		p_hwfn->qm_info.pf_rl = 100000;

	rc = qed_init_pf_rl(p_hwfn, p_ptt, p_hwfn->rel_pf_id,
			    p_hwfn->qm_info.pf_rl);

	DP_VERBOSE(p_hwfn, NETIF_MSG_LINK,
		   "Configured MAX bandwidth to be %08x Mb/sec\n",
		   p_link->speed);

	return rc;
}

/* Main API to configure PF max bandwidth where bw range is [1 - 100] */
int qed_configure_pf_max_bandwidth(struct qed_dev *cdev, u8 max_bw)
{
	int i, rc = -EINVAL;

	if (max_bw < 1 || max_bw > 100) {
		DP_NOTICE(cdev, "PF max bw valid range is [1-100]\n");
		return rc;
	}

	for_each_hwfn(cdev, i) {
		struct qed_hwfn	*p_hwfn = &cdev->hwfns[i];
		struct qed_hwfn *p_lead = QED_LEADING_HWFN(cdev);
		struct qed_mcp_link_state *p_link;
		struct qed_ptt *p_ptt;

		p_link = &p_lead->mcp_info->link_output;

		p_ptt = qed_ptt_acquire(p_hwfn);
		if (!p_ptt)
			return -EBUSY;

		rc = __qed_configure_pf_max_bandwidth(p_hwfn, p_ptt,
						      p_link, max_bw);

		qed_ptt_release(p_hwfn, p_ptt);

		if (rc)
			break;
	}

	return rc;
}

int __qed_configure_pf_min_bandwidth(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     struct qed_mcp_link_state *p_link,
				     u8 min_bw)
{
	int rc = 0;

	p_hwfn->mcp_info->func_info.bandwidth_min = min_bw;
	p_hwfn->qm_info.pf_wfq = min_bw;

	if (!p_link->line_speed)
		return rc;

	p_link->min_pf_rate = (p_link->line_speed * min_bw) / 100;

	rc = qed_init_pf_wfq(p_hwfn, p_ptt, p_hwfn->rel_pf_id, min_bw);

	DP_VERBOSE(p_hwfn, NETIF_MSG_LINK,
		   "Configured MIN bandwidth to be %d Mb/sec\n",
		   p_link->min_pf_rate);

	return rc;
}

/* Main API to configure PF min bandwidth where bw range is [1-100] */
int qed_configure_pf_min_bandwidth(struct qed_dev *cdev, u8 min_bw)
{
	int i, rc = -EINVAL;

	if (min_bw < 1 || min_bw > 100) {
		DP_NOTICE(cdev, "PF min bw valid range is [1-100]\n");
		return rc;
	}

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];
		struct qed_hwfn *p_lead = QED_LEADING_HWFN(cdev);
		struct qed_mcp_link_state *p_link;
		struct qed_ptt *p_ptt;

		p_link = &p_lead->mcp_info->link_output;

		p_ptt = qed_ptt_acquire(p_hwfn);
		if (!p_ptt)
			return -EBUSY;

		rc = __qed_configure_pf_min_bandwidth(p_hwfn, p_ptt,
						      p_link, min_bw);
		if (rc) {
			qed_ptt_release(p_hwfn, p_ptt);
			return rc;
		}

		if (p_link->min_pf_rate) {
			u32 min_rate = p_link->min_pf_rate;

			rc = __qed_configure_vp_wfq_on_link_change(p_hwfn,
								   p_ptt,
								   min_rate);
		}

		qed_ptt_release(p_hwfn, p_ptt);
	}

	return rc;
}

void qed_clean_wfq_db(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	struct qed_mcp_link_state *p_link;

	p_link = &p_hwfn->mcp_info->link_output;

	if (p_link->min_pf_rate)
		qed_disable_wfq_for_all_vports(p_hwfn, p_ptt,
					       p_link->min_pf_rate);

	memset(p_hwfn->qm_info.wfq_data, 0,
	       sizeof(*p_hwfn->qm_info.wfq_data) * p_hwfn->qm_info.num_vports);
}
