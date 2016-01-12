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
#include <linux/etherdevice.h>
#include <linux/qed/qed_chain.h>
#include <linux/qed/qed_if.h>
#include "qed.h"
#include "qed_cxt.h"
#include "qed_dev_api.h"
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_init_ops.h"
#include "qed_int.h"
#include "qed_mcp.h"
#include "qed_reg_addr.h"
#include "qed_sp.h"

/* API common to all protocols */
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
}

void qed_resc_free(struct qed_dev *cdev)
{
	int i;

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
		qed_dmae_info_free(p_hwfn);
	}
}

static int qed_init_qm_info(struct qed_hwfn *p_hwfn)
{
	struct qed_qm_info *qm_info = &p_hwfn->qm_info;
	struct init_qm_port_params *p_qm_port;
	u8 num_vports, i, vport_id, num_ports;
	u16 num_pqs, multi_cos_tcs = 1;

	memset(qm_info, 0, sizeof(*qm_info));

	num_pqs = multi_cos_tcs + 1; /* The '1' is for pure-LB */
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
	qm_info->qm_pq_params = kzalloc(sizeof(*qm_info->qm_pq_params) *
					num_pqs, GFP_ATOMIC);
	if (!qm_info->qm_pq_params)
		goto alloc_err;

	qm_info->qm_vport_params = kzalloc(sizeof(*qm_info->qm_vport_params) *
					   num_vports, GFP_ATOMIC);
	if (!qm_info->qm_vport_params)
		goto alloc_err;

	qm_info->qm_port_params = kzalloc(sizeof(*qm_info->qm_port_params) *
					  MAX_NUM_PORTS, GFP_ATOMIC);
	if (!qm_info->qm_port_params)
		goto alloc_err;

	vport_id = (u8)RESC_START(p_hwfn, QED_VPORT);

	/* First init per-TC PQs */
	for (i = 0; i < multi_cos_tcs; i++) {
		struct init_qm_pq_params *params = &qm_info->qm_pq_params[i];

		params->vport_id = vport_id;
		params->tc_id = p_hwfn->hw_info.non_offload_tc;
		params->wrr_group = 1;
	}

	/* Then init pure-LB PQ */
	qm_info->pure_lb_pq = i;
	qm_info->qm_pq_params[i].vport_id = (u8)RESC_START(p_hwfn, QED_VPORT);
	qm_info->qm_pq_params[i].tc_id = PURE_LB_TC;
	qm_info->qm_pq_params[i].wrr_group = 1;
	i++;

	qm_info->offload_pq = 0;
	qm_info->num_pqs = num_pqs;
	qm_info->num_vports = num_vports;

	/* Initialize qm port parameters */
	num_ports = p_hwfn->cdev->num_ports_in_engines;
	for (i = 0; i < num_ports; i++) {
		p_qm_port = &qm_info->qm_port_params[i];
		p_qm_port->active = 1;
		p_qm_port->num_active_phys_tcs = 4;
		p_qm_port->num_pbf_cmd_lines = PBF_MAX_CMD_LINES / num_ports;
		p_qm_port->num_btb_blocks = BTB_MAX_BLOCKS / num_ports;
	}

	qm_info->max_phys_tcs_per_port = NUM_OF_PHYS_TCS;

	qm_info->start_pq = (u16)RESC_START(p_hwfn, QED_PQ);

	qm_info->start_vport = (u8)RESC_START(p_hwfn, QED_VPORT);

	qm_info->pf_wfq = 0;
	qm_info->pf_rl = 0;
	qm_info->vport_rl_en = 1;

	return 0;

alloc_err:
	DP_NOTICE(p_hwfn, "Failed to allocate memory for QM params\n");
	kfree(qm_info->qm_pq_params);
	kfree(qm_info->qm_vport_params);
	kfree(qm_info->qm_port_params);

	return -ENOMEM;
}

int qed_resc_alloc(struct qed_dev *cdev)
{
	struct qed_consq *p_consq;
	struct qed_eq *p_eq;
	int i, rc = 0;

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
		rc = qed_init_qm_info(p_hwfn);
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
	}
}

#define FINAL_CLEANUP_CMD_OFFSET        (0)
#define FINAL_CLEANUP_CMD (0x1)
#define FINAL_CLEANUP_VALID_OFFSET      (6)
#define FINAL_CLEANUP_VFPF_ID_SHIFT     (7)
#define FINAL_CLEANUP_COMP (0x2)
#define FINAL_CLEANUP_POLL_CNT          (100)
#define FINAL_CLEANUP_POLL_TIME         (10)
int qed_final_cleanup(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt,
		      u16 id)
{
	u32 command = 0, addr, count = FINAL_CLEANUP_POLL_CNT;
	int rc = -EBUSY;

	addr = GTT_BAR0_MAP_REG_USDM_RAM + USTORM_FLR_FINAL_ACK_OFFSET;

	command |= FINAL_CLEANUP_CMD << FINAL_CLEANUP_CMD_OFFSET;
	command |= 1 << FINAL_CLEANUP_VALID_OFFSET;
	command |= id << FINAL_CLEANUP_VFPF_ID_SHIFT;
	command |= FINAL_CLEANUP_COMP << SDM_OP_GEN_COMP_TYPE_SHIFT;

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

	hw_mode = (1 << MODE_BB_A0);

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
	case SF:
		hw_mode |= 1 << MODE_SF;
		break;
	case MF_OVLAN:
		hw_mode |= 1 << MODE_MF_SD;
		break;
	case MF_NPAR:
		hw_mode |= 1 << MODE_MF_SI;
		break;
	default:
		DP_NOTICE(p_hwfn, "Unsupported MF mode, init as SF\n");
		hw_mode |= 1 << MODE_SF;
	}

	hw_mode |= 1 << MODE_ASIC;

	p_hwfn->hw_info.hw_mode = hw_mode;
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
	int rc = 0;

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

	return rc;
}

static int qed_hw_init_port(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt,
			    int hw_mode)
{
	int rc = 0;

	rc = qed_init_run(p_hwfn, p_ptt, PHASE_PORT, p_hwfn->port_id,
			  hw_mode);
	return rc;
}

static int qed_hw_init_pf(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt,
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
		p_hwfn->qm_info.pf_rl = 100;
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
	rc = qed_final_cleanup(p_hwfn, p_ptt, rel_pf_id);
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

	if (b_hw_start) {
		/* enable interrupts */
		qed_int_igu_enable(p_hwfn, p_ptt, int_mode);

		/* send function start command */
		rc = qed_sp_pf_start(p_hwfn, p_hwfn->cdev->mf_mode);
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
		bool b_hw_start,
		enum qed_int_mode int_mode,
		bool allow_npar_tx_switch,
		const u8 *bin_fw_data)
{
	struct qed_storm_stats *p_stat;
	u32 load_code, param, *p_address;
	int rc, mfw_rc, i;
	u8 fw_vport = 0;

	rc = qed_init_fw_data(cdev, bin_fw_data);
	if (rc != 0)
		return rc;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		rc = qed_fw_vport(p_hwfn, 0, &fw_vport);
		if (rc != 0)
			return rc;

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
					    p_hwfn->hw_info.hw_mode,
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

		p_hwfn->hw_init_done = true;

		/* init PF stats */
		p_stat = &p_hwfn->storm_stats;
		p_stat->mstats.address = BAR0_MAP_REG_MSDM_RAM +
					 MSTORM_QUEUE_STAT_OFFSET(fw_vport);
		p_stat->mstats.len = sizeof(struct eth_mstorm_per_queue_stat);

		p_stat->ustats.address = BAR0_MAP_REG_USDM_RAM +
					 USTORM_QUEUE_STAT_OFFSET(fw_vport);
		p_stat->ustats.len = sizeof(struct eth_ustorm_per_queue_stat);

		p_stat->pstats.address = BAR0_MAP_REG_PSDM_RAM +
					 PSTORM_QUEUE_STAT_OFFSET(fw_vport);
		p_stat->pstats.len = sizeof(struct eth_pstorm_per_queue_stat);

		p_address = &p_stat->tstats.address;
		*p_address = BAR0_MAP_REG_TSDM_RAM +
			     TSTORM_PORT_STAT_OFFSET(MFW_PORT(p_hwfn));
		p_stat->tstats.len = sizeof(struct tstorm_per_port_stat);
	}

	return 0;
}

#define QED_HW_STOP_RETRY_LIMIT (10)
int qed_hw_stop(struct qed_dev *cdev)
{
	int rc = 0, t_rc;
	int i, j;

	for_each_hwfn(cdev, j) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[j];
		struct qed_ptt *p_ptt = p_hwfn->p_main_ptt;

		DP_VERBOSE(p_hwfn, NETIF_MSG_IFDOWN, "Stopping hw/fw\n");

		/* mark the hw as uninitialized... */
		p_hwfn->hw_init_done = false;

		rc = qed_sp_pf_stop(p_hwfn);
		if (rc)
			return rc;

		qed_wr(p_hwfn, p_ptt,
		       NIG_REG_RX_LLH_BRB_GATE_DNTFWD_PERPF, 0x1);

		qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_TCP, 0x0);
		qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_UDP, 0x0);
		qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_FCOE, 0x0);
		qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_ROCE, 0x0);
		qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_OPENFLOW, 0x0);

		qed_wr(p_hwfn, p_ptt, TM_REG_PF_ENABLE_CONN, 0x0);
		qed_wr(p_hwfn, p_ptt, TM_REG_PF_ENABLE_TASK, 0x0);
		for (i = 0; i < QED_HW_STOP_RETRY_LIMIT; i++) {
			if ((!qed_rd(p_hwfn, p_ptt,
				     TM_REG_PF_SCAN_ACTIVE_CONN)) &&
			    (!qed_rd(p_hwfn, p_ptt,
				     TM_REG_PF_SCAN_ACTIVE_TASK)))
				break;

			usleep_range(1000, 2000);
		}
		if (i == QED_HW_STOP_RETRY_LIMIT)
			DP_NOTICE(p_hwfn,
				  "Timers linear scans are not over [Connection %02x Tasks %02x]\n",
				  (u8)qed_rd(p_hwfn, p_ptt,
					     TM_REG_PF_SCAN_ACTIVE_CONN),
				  (u8)qed_rd(p_hwfn, p_ptt,
					     TM_REG_PF_SCAN_ACTIVE_TASK));

		/* Disable Attention Generation */
		qed_int_igu_disable_int(p_hwfn, p_ptt);

		qed_wr(p_hwfn, p_ptt, IGU_REG_LEADING_EDGE_LATCH, 0);
		qed_wr(p_hwfn, p_ptt, IGU_REG_TRAILING_EDGE_LATCH, 0);

		qed_int_igu_init_pure_rt(p_hwfn, p_ptt, false, true);

		/* Need to wait 1ms to guarantee SBs are cleared */
		usleep_range(1000, 2000);
	}

	/* Disable DMAE in PXP - in CMT, this should only be done for
	 * first hw-function, and only after all transactions have
	 * stopped for all active hw-functions.
	 */
	t_rc = qed_change_pci_hwfn(&cdev->hwfns[0],
				   cdev->hwfns[0].p_main_ptt,
				   false);
	if (t_rc != 0)
		rc = t_rc;

	return rc;
}

void qed_hw_stop_fastpath(struct qed_dev *cdev)
{
	int i, j;

	for_each_hwfn(cdev, j) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[j];
		struct qed_ptt *p_ptt   = p_hwfn->p_main_ptt;

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

		qed_wr(p_hwfn, p_ptt, TM_REG_PF_ENABLE_CONN, 0x0);
		qed_wr(p_hwfn, p_ptt, TM_REG_PF_ENABLE_TASK, 0x0);
		for (i = 0; i < QED_HW_STOP_RETRY_LIMIT; i++) {
			if ((!qed_rd(p_hwfn, p_ptt,
				     TM_REG_PF_SCAN_ACTIVE_CONN)) &&
			    (!qed_rd(p_hwfn, p_ptt,
				     TM_REG_PF_SCAN_ACTIVE_TASK)))
				break;

			usleep_range(1000, 2000);
		}
		if (i == QED_HW_STOP_RETRY_LIMIT)
			DP_NOTICE(p_hwfn,
				  "Timers linear scans are not over [Connection %02x Tasks %02x]\n",
				  (u8)qed_rd(p_hwfn, p_ptt,
					     TM_REG_PF_SCAN_ACTIVE_CONN),
				  (u8)qed_rd(p_hwfn, p_ptt,
					     TM_REG_PF_SCAN_ACTIVE_TASK));

		qed_int_igu_init_pure_rt(p_hwfn, p_ptt, false, false);

		/* Need to wait 1ms to guarantee SBs are cleared */
		usleep_range(1000, 2000);
	}
}

void qed_hw_start_fastpath(struct qed_hwfn *p_hwfn)
{
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
static int qed_hw_hwfn_prepare(struct qed_hwfn *p_hwfn)
{
	int rc;

	/* Allocate PTT pool */
	rc = qed_ptt_pool_alloc(p_hwfn);
	if (rc)
		return rc;

	/* Allocate the main PTT */
	p_hwfn->p_main_ptt = qed_get_reserved_ptt(p_hwfn, RESERVED_PTT_MAIN);

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

	return 0;
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
	u32 *resc_num = p_hwfn->hw_info.resc_num;
	int num_funcs, i;

	num_funcs = IS_MF(p_hwfn) ? MAX_NUM_PFS_BB
				  : p_hwfn->cdev->num_ports_in_engines;

	resc_num[QED_SB] = min_t(u32,
				 (MAX_SB_PER_PATH_BB / num_funcs),
				 qed_int_get_num_sbs(p_hwfn, NULL));
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
	u32 port_cfg_addr, link_temp, val, nvm_cfg_addr;
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

	/* Read Vendor Id / Device Id */
	addr = MCP_REG_SCRATCH + nvm_cfg1_offset +
	       offsetof(struct nvm_cfg1, glob) +
	       offsetof(struct nvm_cfg1_glob, pci_id);
	p_hwfn->hw_info.vendor_id = qed_rd(p_hwfn, p_ptt, addr) &
				    NVM_CFG1_GLOB_VENDOR_ID_MASK;

	addr = MCP_REG_SCRATCH + nvm_cfg1_offset +
	       offsetof(struct nvm_cfg1, glob) +
	       offsetof(struct nvm_cfg1_glob, core_cfg);

	core_cfg = qed_rd(p_hwfn, p_ptt, addr);

	switch ((core_cfg & NVM_CFG1_GLOB_NETWORK_PORT_MODE_MASK) >>
		NVM_CFG1_GLOB_NETWORK_PORT_MODE_OFFSET) {
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_DE_2X40G:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_2X40G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_DE_2X50G:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_2X50G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_DE_1X100G:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_1X100G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_DE_4X10G_F:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_4X10G_F;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_DE_4X10G_E:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_4X10G_E;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_DE_4X20G:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_4X20G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_DE_1X40G:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_1X40G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_DE_2X25G:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_2X25G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_DE_1X25G:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_1X25G;
		break;
	default:
		DP_NOTICE(p_hwfn, "Unknown port mode in 0x%08x\n",
			  core_cfg);
		break;
	}

	addr = MCP_REG_SCRATCH + nvm_cfg1_offset +
	       offsetof(struct nvm_cfg1, func[MCP_PF_ID(p_hwfn)]) +
	       offsetof(struct nvm_cfg1_func, device_id);
	val = qed_rd(p_hwfn, p_ptt, addr);

	if (IS_MF(p_hwfn)) {
		p_hwfn->hw_info.device_id =
			(val & NVM_CFG1_FUNC_MF_VENDOR_DEVICE_ID_MASK) >>
			NVM_CFG1_FUNC_MF_VENDOR_DEVICE_ID_OFFSET;
	} else {
		p_hwfn->hw_info.device_id =
			(val & NVM_CFG1_FUNC_VENDOR_DEVICE_ID_MASK) >>
			NVM_CFG1_FUNC_VENDOR_DEVICE_ID_OFFSET;
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
	case NVM_CFG1_PORT_DRV_LINK_SPEED_100G:
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
		p_hwfn->cdev->mf_mode = MF_OVLAN;
		break;
	case NVM_CFG1_GLOB_MF_MODE_NPAR1_0:
		p_hwfn->cdev->mf_mode = MF_NPAR;
		break;
	case NVM_CFG1_GLOB_MF_MODE_FORCED_SF:
		p_hwfn->cdev->mf_mode = SF;
		break;
	}
	DP_INFO(p_hwfn, "Multi function mode is %08x\n",
		p_hwfn->cdev->mf_mode);

	return qed_mcp_fill_shmem_func_info(p_hwfn, p_ptt);
}

static int
qed_get_hw_info(struct qed_hwfn *p_hwfn,
		struct qed_ptt *p_ptt,
		enum qed_pci_personality personality)
{
	u32 port_mode;
	int rc;

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

	qed_hw_get_resc(p_hwfn);

	return rc;
}

static void qed_get_dev_info(struct qed_dev *cdev)
{
	u32 tmp;

	cdev->chip_num = (u16)qed_rd(cdev->hwfns, cdev->hwfns[0].p_main_ptt,
				     MISCS_REG_CHIP_NUM);
	cdev->chip_rev = (u16)qed_rd(cdev->hwfns, cdev->hwfns[0].p_main_ptt,
				     MISCS_REG_CHIP_REV);
	MASK_FIELD(CHIP_REV, cdev->chip_rev);

	/* Learn number of HW-functions */
	tmp = qed_rd(cdev->hwfns, cdev->hwfns[0].p_main_ptt,
		     MISCS_REG_CMT_ENABLED_FOR_PAIR);

	if (tmp & (1 << cdev->hwfns[0].rel_pf_id)) {
		DP_NOTICE(cdev->hwfns, "device in CMT mode\n");
		cdev->num_hwfns = 2;
	} else {
		cdev->num_hwfns = 1;
	}

	cdev->chip_bond_id = qed_rd(cdev->hwfns, cdev->hwfns[0].p_main_ptt,
				    MISCS_REG_CHIP_TEST_REG) >> 4;
	MASK_FIELD(CHIP_BOND_ID, cdev->chip_bond_id);
	cdev->chip_metal = (u16)qed_rd(cdev->hwfns, cdev->hwfns[0].p_main_ptt,
				       MISCS_REG_CHIP_METAL);
	MASK_FIELD(CHIP_METAL, cdev->chip_metal);

	DP_INFO(cdev->hwfns,
		"Chip details - Num: %04x Rev: %04x Bond id: %04x Metal: %04x\n",
		cdev->chip_num, cdev->chip_rev,
		cdev->chip_bond_id, cdev->chip_metal);
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

	/* Validate that chip access is feasible */
	if (REG_RD(p_hwfn, PXP_PF_ME_OPAQUE_ADDR) == 0xffffffff) {
		DP_ERR(p_hwfn,
		       "Reading the ME register returns all Fs; Preventing further chip access\n");
		return -EINVAL;
	}

	get_function_id(p_hwfn);

	rc = qed_hw_hwfn_prepare(p_hwfn);
	if (rc) {
		DP_NOTICE(p_hwfn, "Failed to prepare hwfn's hw\n");
		goto err0;
	}

	/* First hwfn learns basic information, e.g., number of hwfns */
	if (!p_hwfn->my_id)
		qed_get_dev_info(p_hwfn->cdev);

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
	qed_mcp_free(p_hwfn);
err1:
	qed_hw_hwfn_free(p_hwfn);
err0:
	return rc;
}

static u32 qed_hw_bar_size(struct qed_dev *cdev,
			   u8 bar_id)
{
	u32 size = pci_resource_len(cdev->pdev, (bar_id > 0) ? 2 : 0);

	return size / cdev->num_hwfns;
}

int qed_hw_prepare(struct qed_dev *cdev,
		   int personality)
{
	int rc, i;

	/* Store the precompiled init data ptrs */
	qed_init_iro_array(cdev);

	/* Initialize the first hwfn - will learn number of hwfns */
	rc = qed_hw_prepare_single(&cdev->hwfns[0], cdev->regview,
				   cdev->doorbells, personality);
	if (rc)
		return rc;

	personality = cdev->hwfns[0].hw_info.personality;

	/* Initialize the rest of the hwfns */
	for (i = 1; i < cdev->num_hwfns; i++) {
		void __iomem *p_regview, *p_doorbell;

		p_regview =  cdev->regview +
			     i * qed_hw_bar_size(cdev, 0);
		p_doorbell = cdev->doorbells +
			     i * qed_hw_bar_size(cdev, 1);
		rc = qed_hw_prepare_single(&cdev->hwfns[i], p_regview,
					   p_doorbell, personality);
		if (rc) {
			/* Cleanup previously initialized hwfns */
			while (--i >= 0) {
				qed_init_free(&cdev->hwfns[i]);
				qed_mcp_free(&cdev->hwfns[i]);
				qed_hw_hwfn_free(&cdev->hwfns[i]);
			}
			return rc;
		}
	}

	return 0;
}

void qed_hw_remove(struct qed_dev *cdev)
{
	int i;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		qed_init_free(p_hwfn);
		qed_hw_hwfn_free(p_hwfn);
		qed_mcp_free(p_hwfn);
	}
}

int qed_chain_alloc(struct qed_dev *cdev,
		    enum qed_chain_use_mode intended_use,
		    enum qed_chain_mode mode,
		    u16 num_elems,
		    size_t elem_size,
		    struct qed_chain *p_chain)
{
	dma_addr_t p_pbl_phys = 0;
	void *p_pbl_virt = NULL;
	dma_addr_t p_phys = 0;
	void *p_virt = NULL;
	u16 page_cnt = 0;
	size_t size;

	if (mode == QED_CHAIN_MODE_SINGLE)
		page_cnt = 1;
	else
		page_cnt = QED_CHAIN_PAGE_CNT(num_elems, elem_size, mode);

	size = page_cnt * QED_CHAIN_PAGE_SIZE;
	p_virt = dma_alloc_coherent(&cdev->pdev->dev,
				    size, &p_phys, GFP_KERNEL);
	if (!p_virt) {
		DP_NOTICE(cdev, "Failed to allocate chain mem\n");
		goto nomem;
	}

	if (mode == QED_CHAIN_MODE_PBL) {
		size = page_cnt * QED_CHAIN_PBL_ENTRY_SIZE;
		p_pbl_virt = dma_alloc_coherent(&cdev->pdev->dev,
						size, &p_pbl_phys,
						GFP_KERNEL);
		if (!p_pbl_virt) {
			DP_NOTICE(cdev, "Failed to allocate chain pbl mem\n");
			goto nomem;
		}

		qed_chain_pbl_init(p_chain, p_virt, p_phys, page_cnt,
				   (u8)elem_size, intended_use,
				   p_pbl_phys, p_pbl_virt);
	} else {
		qed_chain_init(p_chain, p_virt, p_phys, page_cnt,
			       (u8)elem_size, intended_use, mode);
	}

	return 0;

nomem:
	dma_free_coherent(&cdev->pdev->dev,
			  page_cnt * QED_CHAIN_PAGE_SIZE,
			  p_virt, p_phys);
	dma_free_coherent(&cdev->pdev->dev,
			  page_cnt * QED_CHAIN_PBL_ENTRY_SIZE,
			  p_pbl_virt, p_pbl_phys);

	return -ENOMEM;
}

void qed_chain_free(struct qed_dev *cdev,
		    struct qed_chain *p_chain)
{
	size_t size;

	if (!p_chain->p_virt_addr)
		return;

	if (p_chain->mode == QED_CHAIN_MODE_PBL) {
		size = p_chain->page_cnt * QED_CHAIN_PBL_ENTRY_SIZE;
		dma_free_coherent(&cdev->pdev->dev, size,
				  p_chain->pbl.p_virt_table,
				  p_chain->pbl.p_phys_table);
	}

	size = p_chain->page_cnt * QED_CHAIN_PAGE_SIZE;
	dma_free_coherent(&cdev->pdev->dev, size,
			  p_chain->p_virt_addr,
			  p_chain->p_phys_addr);
}

static void __qed_get_vport_stats(struct qed_dev *cdev,
				  struct qed_eth_stats  *stats)
{
	int i, j;

	memset(stats, 0, sizeof(*stats));

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];
		struct eth_mstorm_per_queue_stat mstats;
		struct eth_ustorm_per_queue_stat ustats;
		struct eth_pstorm_per_queue_stat pstats;
		struct tstorm_per_port_stat tstats;
		struct port_stats port_stats;
		struct qed_ptt *p_ptt = qed_ptt_acquire(p_hwfn);

		if (!p_ptt) {
			DP_ERR(p_hwfn, "Failed to acquire ptt\n");
			continue;
		}

		memset(&mstats, 0, sizeof(mstats));
		qed_memcpy_from(p_hwfn, p_ptt, &mstats,
				p_hwfn->storm_stats.mstats.address,
				p_hwfn->storm_stats.mstats.len);

		memset(&ustats, 0, sizeof(ustats));
		qed_memcpy_from(p_hwfn, p_ptt, &ustats,
				p_hwfn->storm_stats.ustats.address,
				p_hwfn->storm_stats.ustats.len);

		memset(&pstats, 0, sizeof(pstats));
		qed_memcpy_from(p_hwfn, p_ptt, &pstats,
				p_hwfn->storm_stats.pstats.address,
				p_hwfn->storm_stats.pstats.len);

		memset(&tstats, 0, sizeof(tstats));
		qed_memcpy_from(p_hwfn, p_ptt, &tstats,
				p_hwfn->storm_stats.tstats.address,
				p_hwfn->storm_stats.tstats.len);

		memset(&port_stats, 0, sizeof(port_stats));

		if (p_hwfn->mcp_info)
			qed_memcpy_from(p_hwfn, p_ptt, &port_stats,
					p_hwfn->mcp_info->port_addr +
					offsetof(struct public_port, stats),
					sizeof(port_stats));
		qed_ptt_release(p_hwfn, p_ptt);

		stats->no_buff_discards +=
			HILO_64_REGPAIR(mstats.no_buff_discard);
		stats->packet_too_big_discard +=
			HILO_64_REGPAIR(mstats.packet_too_big_discard);
		stats->ttl0_discard +=
			HILO_64_REGPAIR(mstats.ttl0_discard);
		stats->tpa_coalesced_pkts +=
			HILO_64_REGPAIR(mstats.tpa_coalesced_pkts);
		stats->tpa_coalesced_events +=
			HILO_64_REGPAIR(mstats.tpa_coalesced_events);
		stats->tpa_aborts_num +=
			HILO_64_REGPAIR(mstats.tpa_aborts_num);
		stats->tpa_coalesced_bytes +=
			HILO_64_REGPAIR(mstats.tpa_coalesced_bytes);

		stats->rx_ucast_bytes +=
			HILO_64_REGPAIR(ustats.rcv_ucast_bytes);
		stats->rx_mcast_bytes +=
			HILO_64_REGPAIR(ustats.rcv_mcast_bytes);
		stats->rx_bcast_bytes +=
			HILO_64_REGPAIR(ustats.rcv_bcast_bytes);
		stats->rx_ucast_pkts +=
			HILO_64_REGPAIR(ustats.rcv_ucast_pkts);
		stats->rx_mcast_pkts +=
			HILO_64_REGPAIR(ustats.rcv_mcast_pkts);
		stats->rx_bcast_pkts +=
			HILO_64_REGPAIR(ustats.rcv_bcast_pkts);

		stats->mftag_filter_discards +=
			HILO_64_REGPAIR(tstats.mftag_filter_discard);
		stats->mac_filter_discards +=
			HILO_64_REGPAIR(tstats.eth_mac_filter_discard);

		stats->tx_ucast_bytes +=
			HILO_64_REGPAIR(pstats.sent_ucast_bytes);
		stats->tx_mcast_bytes +=
			HILO_64_REGPAIR(pstats.sent_mcast_bytes);
		stats->tx_bcast_bytes +=
			HILO_64_REGPAIR(pstats.sent_bcast_bytes);
		stats->tx_ucast_pkts +=
			HILO_64_REGPAIR(pstats.sent_ucast_pkts);
		stats->tx_mcast_pkts +=
			HILO_64_REGPAIR(pstats.sent_mcast_pkts);
		stats->tx_bcast_pkts +=
			HILO_64_REGPAIR(pstats.sent_bcast_pkts);
		stats->tx_err_drop_pkts +=
			HILO_64_REGPAIR(pstats.error_drop_pkts);
		stats->rx_64_byte_packets       += port_stats.pmm.r64;
		stats->rx_127_byte_packets      += port_stats.pmm.r127;
		stats->rx_255_byte_packets      += port_stats.pmm.r255;
		stats->rx_511_byte_packets      += port_stats.pmm.r511;
		stats->rx_1023_byte_packets     += port_stats.pmm.r1023;
		stats->rx_1518_byte_packets     += port_stats.pmm.r1518;
		stats->rx_1522_byte_packets     += port_stats.pmm.r1522;
		stats->rx_2047_byte_packets     += port_stats.pmm.r2047;
		stats->rx_4095_byte_packets     += port_stats.pmm.r4095;
		stats->rx_9216_byte_packets     += port_stats.pmm.r9216;
		stats->rx_16383_byte_packets    += port_stats.pmm.r16383;
		stats->rx_crc_errors	    += port_stats.pmm.rfcs;
		stats->rx_mac_crtl_frames       += port_stats.pmm.rxcf;
		stats->rx_pause_frames	  += port_stats.pmm.rxpf;
		stats->rx_pfc_frames	    += port_stats.pmm.rxpp;
		stats->rx_align_errors	  += port_stats.pmm.raln;
		stats->rx_carrier_errors	+= port_stats.pmm.rfcr;
		stats->rx_oversize_packets      += port_stats.pmm.rovr;
		stats->rx_jabbers	       += port_stats.pmm.rjbr;
		stats->rx_undersize_packets     += port_stats.pmm.rund;
		stats->rx_fragments	     += port_stats.pmm.rfrg;
		stats->tx_64_byte_packets       += port_stats.pmm.t64;
		stats->tx_65_to_127_byte_packets += port_stats.pmm.t127;
		stats->tx_128_to_255_byte_packets += port_stats.pmm.t255;
		stats->tx_256_to_511_byte_packets  += port_stats.pmm.t511;
		stats->tx_512_to_1023_byte_packets += port_stats.pmm.t1023;
		stats->tx_1024_to_1518_byte_packets += port_stats.pmm.t1518;
		stats->tx_1519_to_2047_byte_packets += port_stats.pmm.t2047;
		stats->tx_2048_to_4095_byte_packets += port_stats.pmm.t4095;
		stats->tx_4096_to_9216_byte_packets += port_stats.pmm.t9216;
		stats->tx_9217_to_16383_byte_packets += port_stats.pmm.t16383;
		stats->tx_pause_frames	  += port_stats.pmm.txpf;
		stats->tx_pfc_frames	    += port_stats.pmm.txpp;
		stats->tx_lpi_entry_count       += port_stats.pmm.tlpiec;
		stats->tx_total_collisions      += port_stats.pmm.tncl;
		stats->rx_mac_bytes	     += port_stats.pmm.rbyte;
		stats->rx_mac_uc_packets	+= port_stats.pmm.rxuca;
		stats->rx_mac_mc_packets	+= port_stats.pmm.rxmca;
		stats->rx_mac_bc_packets	+= port_stats.pmm.rxbca;
		stats->rx_mac_frames_ok	 += port_stats.pmm.rxpok;
		stats->tx_mac_bytes	     += port_stats.pmm.tbyte;
		stats->tx_mac_uc_packets	+= port_stats.pmm.txuca;
		stats->tx_mac_mc_packets	+= port_stats.pmm.txmca;
		stats->tx_mac_bc_packets	+= port_stats.pmm.txbca;
		stats->tx_mac_ctrl_frames       += port_stats.pmm.txcf;

		for (j = 0; j < 8; j++) {
			stats->brb_truncates += port_stats.brb.brb_truncate[j];
			stats->brb_discards += port_stats.brb.brb_discard[j];
		}
	}
}

void qed_get_vport_stats(struct qed_dev *cdev,
			 struct qed_eth_stats *stats)
{
	u32 i;

	if (!cdev) {
		memset(stats, 0, sizeof(*stats));
		return;
	}

	__qed_get_vport_stats(cdev, stats);

	if (!cdev->reset_stats)
		return;

	/* Reduce the statistics baseline */
	for (i = 0; i < sizeof(struct qed_eth_stats) / sizeof(u64); i++)
		((u64 *)stats)[i] -= ((u64 *)cdev->reset_stats)[i];
}

/* zeroes V-PORT specific portion of stats (Port stats remains untouched) */
void qed_reset_vport_stats(struct qed_dev *cdev)
{
	int i;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];
		struct eth_mstorm_per_queue_stat mstats;
		struct eth_ustorm_per_queue_stat ustats;
		struct eth_pstorm_per_queue_stat pstats;
		struct qed_ptt *p_ptt = qed_ptt_acquire(p_hwfn);

		if (!p_ptt) {
			DP_ERR(p_hwfn, "Failed to acquire ptt\n");
			continue;
		}

		memset(&mstats, 0, sizeof(mstats));
		qed_memcpy_to(p_hwfn, p_ptt,
			      p_hwfn->storm_stats.mstats.address,
			      &mstats,
			      p_hwfn->storm_stats.mstats.len);

		memset(&ustats, 0, sizeof(ustats));
		qed_memcpy_to(p_hwfn, p_ptt,
			      p_hwfn->storm_stats.ustats.address,
			      &ustats,
			      p_hwfn->storm_stats.ustats.len);

		memset(&pstats, 0, sizeof(pstats));
		qed_memcpy_to(p_hwfn, p_ptt,
			      p_hwfn->storm_stats.pstats.address,
			      &pstats,
			      p_hwfn->storm_stats.pstats.len);

		qed_ptt_release(p_hwfn, p_ptt);
	}

	/* PORT statistics are not necessarily reset, so we need to
	 * read and create a baseline for future statistics.
	 */
	if (!cdev->reset_stats)
		DP_INFO(cdev, "Reset stats not allocated\n");
	else
		__qed_get_vport_stats(cdev, cdev->reset_stats);
}

int qed_fw_l2_queue(struct qed_hwfn *p_hwfn,
		    u16 src_id, u16 *dst_id)
{
	if (src_id >= RESC_NUM(p_hwfn, QED_L2_QUEUE)) {
		u16 min, max;

		min = (u16)RESC_START(p_hwfn, QED_L2_QUEUE);
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
