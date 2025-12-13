// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023 Intel Corporation */

#include "idpf.h"
#include "idpf_lan_pf_regs.h"
#include "idpf_virtchnl.h"
#include "idpf_ptp.h"

#define IDPF_PF_ITR_IDX_SPACING		0x4

/**
 * idpf_ctlq_reg_init - initialize default mailbox registers
 * @adapter: adapter structure
 * @cq: pointer to the array of create control queues
 */
static void idpf_ctlq_reg_init(struct idpf_adapter *adapter,
			       struct idpf_ctlq_create_info *cq)
{
	resource_size_t mbx_start = adapter->dev_ops.static_reg_info[0].start;
	int i;

	for (i = 0; i < IDPF_NUM_DFLT_MBX_Q; i++) {
		struct idpf_ctlq_create_info *ccq = cq + i;

		switch (ccq->type) {
		case IDPF_CTLQ_TYPE_MAILBOX_TX:
			/* set head and tail registers in our local struct */
			ccq->reg.head = PF_FW_ATQH - mbx_start;
			ccq->reg.tail = PF_FW_ATQT - mbx_start;
			ccq->reg.len = PF_FW_ATQLEN - mbx_start;
			ccq->reg.bah = PF_FW_ATQBAH - mbx_start;
			ccq->reg.bal = PF_FW_ATQBAL - mbx_start;
			ccq->reg.len_mask = PF_FW_ATQLEN_ATQLEN_M;
			ccq->reg.len_ena_mask = PF_FW_ATQLEN_ATQENABLE_M;
			ccq->reg.head_mask = PF_FW_ATQH_ATQH_M;
			break;
		case IDPF_CTLQ_TYPE_MAILBOX_RX:
			/* set head and tail registers in our local struct */
			ccq->reg.head = PF_FW_ARQH - mbx_start;
			ccq->reg.tail = PF_FW_ARQT - mbx_start;
			ccq->reg.len = PF_FW_ARQLEN - mbx_start;
			ccq->reg.bah = PF_FW_ARQBAH - mbx_start;
			ccq->reg.bal = PF_FW_ARQBAL - mbx_start;
			ccq->reg.len_mask = PF_FW_ARQLEN_ARQLEN_M;
			ccq->reg.len_ena_mask = PF_FW_ARQLEN_ARQENABLE_M;
			ccq->reg.head_mask = PF_FW_ARQH_ARQH_M;
			break;
		default:
			break;
		}
	}
}

/**
 * idpf_mb_intr_reg_init - Initialize mailbox interrupt register
 * @adapter: adapter structure
 */
static void idpf_mb_intr_reg_init(struct idpf_adapter *adapter)
{
	struct idpf_intr_reg *intr = &adapter->mb_vector.intr_reg;
	u32 dyn_ctl = le32_to_cpu(adapter->caps.mailbox_dyn_ctl);

	intr->dyn_ctl = idpf_get_reg_addr(adapter, dyn_ctl);
	intr->dyn_ctl_intena_m = PF_GLINT_DYN_CTL_INTENA_M;
	intr->dyn_ctl_itridx_m = PF_GLINT_DYN_CTL_ITR_INDX_M;
	intr->icr_ena = idpf_get_reg_addr(adapter, PF_INT_DIR_OICR_ENA);
	intr->icr_ena_ctlq_m = PF_INT_DIR_OICR_ENA_M;
}

/**
 * idpf_intr_reg_init - Initialize interrupt registers
 * @vport: virtual port structure
 */
static int idpf_intr_reg_init(struct idpf_vport *vport)
{
	struct idpf_adapter *adapter = vport->adapter;
	int num_vecs = vport->num_q_vectors;
	struct idpf_vec_regs *reg_vals;
	int num_regs, i, err = 0;
	u32 rx_itr, tx_itr, val;
	u16 total_vecs;

	total_vecs = idpf_get_reserved_vecs(vport->adapter);
	reg_vals = kcalloc(total_vecs, sizeof(struct idpf_vec_regs),
			   GFP_KERNEL);
	if (!reg_vals)
		return -ENOMEM;

	num_regs = idpf_get_reg_intr_vecs(vport, reg_vals);
	if (num_regs < num_vecs) {
		err = -EINVAL;
		goto free_reg_vals;
	}

	for (i = 0; i < num_vecs; i++) {
		struct idpf_q_vector *q_vector = &vport->q_vectors[i];
		u16 vec_id = vport->q_vector_idxs[i] - IDPF_MBX_Q_VEC;
		struct idpf_intr_reg *intr = &q_vector->intr_reg;
		u32 spacing;

		intr->dyn_ctl = idpf_get_reg_addr(adapter,
						  reg_vals[vec_id].dyn_ctl_reg);
		intr->dyn_ctl_intena_m = PF_GLINT_DYN_CTL_INTENA_M;
		intr->dyn_ctl_intena_msk_m = PF_GLINT_DYN_CTL_INTENA_MSK_M;
		intr->dyn_ctl_itridx_s = PF_GLINT_DYN_CTL_ITR_INDX_S;
		intr->dyn_ctl_intrvl_s = PF_GLINT_DYN_CTL_INTERVAL_S;
		intr->dyn_ctl_wb_on_itr_m = PF_GLINT_DYN_CTL_WB_ON_ITR_M;
		intr->dyn_ctl_swint_trig_m = PF_GLINT_DYN_CTL_SWINT_TRIG_M;
		intr->dyn_ctl_sw_itridx_ena_m =
			PF_GLINT_DYN_CTL_SW_ITR_INDX_ENA_M;

		spacing = IDPF_ITR_IDX_SPACING(reg_vals[vec_id].itrn_index_spacing,
					       IDPF_PF_ITR_IDX_SPACING);
		rx_itr = PF_GLINT_ITR_ADDR(VIRTCHNL2_ITR_IDX_0,
					   reg_vals[vec_id].itrn_reg,
					   spacing);
		tx_itr = PF_GLINT_ITR_ADDR(VIRTCHNL2_ITR_IDX_1,
					   reg_vals[vec_id].itrn_reg,
					   spacing);
		intr->rx_itr = idpf_get_reg_addr(adapter, rx_itr);
		intr->tx_itr = idpf_get_reg_addr(adapter, tx_itr);
	}

	/* Data vector for NOIRQ queues */

	val = reg_vals[vport->q_vector_idxs[i] - IDPF_MBX_Q_VEC].dyn_ctl_reg;
	vport->noirq_dyn_ctl = idpf_get_reg_addr(adapter, val);

	val = PF_GLINT_DYN_CTL_WB_ON_ITR_M | PF_GLINT_DYN_CTL_INTENA_MSK_M |
	      FIELD_PREP(PF_GLINT_DYN_CTL_ITR_INDX_M, IDPF_NO_ITR_UPDATE_IDX);
	vport->noirq_dyn_ctl_ena = val;

free_reg_vals:
	kfree(reg_vals);

	return err;
}

/**
 * idpf_reset_reg_init - Initialize reset registers
 * @adapter: Driver specific private structure
 */
static void idpf_reset_reg_init(struct idpf_adapter *adapter)
{
	adapter->reset_reg.rstat = idpf_get_rstat_reg_addr(adapter, PFGEN_RSTAT);
	adapter->reset_reg.rstat_m = PFGEN_RSTAT_PFR_STATE_M;
}

/**
 * idpf_trigger_reset - trigger reset
 * @adapter: Driver specific private structure
 * @trig_cause: Reason to trigger a reset
 */
static void idpf_trigger_reset(struct idpf_adapter *adapter,
			       enum idpf_flags __always_unused trig_cause)
{
	u32 reset_reg;

	reset_reg = readl(idpf_get_rstat_reg_addr(adapter, PFGEN_CTRL));
	writel(reset_reg | PFGEN_CTRL_PFSWR,
	       idpf_get_rstat_reg_addr(adapter, PFGEN_CTRL));
}

/**
 * idpf_ptp_reg_init - Initialize required registers
 * @adapter: Driver specific private structure
 *
 * Set the bits required for enabling shtime and cmd execution
 */
static void idpf_ptp_reg_init(const struct idpf_adapter *adapter)
{
	adapter->ptp->cmd.shtime_enable_mask = PF_GLTSYN_CMD_SYNC_SHTIME_EN_M;
	adapter->ptp->cmd.exec_cmd_mask = PF_GLTSYN_CMD_SYNC_EXEC_CMD_M;
}

/**
 * idpf_idc_register - register for IDC callbacks
 * @adapter: Driver specific private structure
 *
 * Return: 0 on success or error code on failure.
 */
static int idpf_idc_register(struct idpf_adapter *adapter)
{
	return idpf_idc_init_aux_core_dev(adapter, IIDC_FUNCTION_TYPE_PF);
}

/**
 * idpf_reg_ops_init - Initialize register API function pointers
 * @adapter: Driver specific private structure
 */
static void idpf_reg_ops_init(struct idpf_adapter *adapter)
{
	adapter->dev_ops.reg_ops.ctlq_reg_init = idpf_ctlq_reg_init;
	adapter->dev_ops.reg_ops.intr_reg_init = idpf_intr_reg_init;
	adapter->dev_ops.reg_ops.mb_intr_reg_init = idpf_mb_intr_reg_init;
	adapter->dev_ops.reg_ops.reset_reg_init = idpf_reset_reg_init;
	adapter->dev_ops.reg_ops.trigger_reset = idpf_trigger_reset;
	adapter->dev_ops.reg_ops.ptp_reg_init = idpf_ptp_reg_init;
}

/**
 * idpf_dev_ops_init - Initialize device API function pointers
 * @adapter: Driver specific private structure
 */
void idpf_dev_ops_init(struct idpf_adapter *adapter)
{
	idpf_reg_ops_init(adapter);

	adapter->dev_ops.idc_init = idpf_idc_register;

	resource_set_range(&adapter->dev_ops.static_reg_info[0],
			   PF_FW_BASE, IDPF_PF_MBX_REGION_SZ);
	resource_set_range(&adapter->dev_ops.static_reg_info[1],
			   PFGEN_RTRIG, IDPF_PF_RSTAT_REGION_SZ);
}
