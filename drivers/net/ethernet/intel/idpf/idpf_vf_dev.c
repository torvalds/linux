// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023 Intel Corporation */

#include "idpf.h"
#include "idpf_lan_vf_regs.h"
#include "idpf_virtchnl.h"

#define IDPF_VF_ITR_IDX_SPACING		0x40

/**
 * idpf_vf_ctlq_reg_init - initialize default mailbox registers
 * @adapter: adapter structure
 * @cq: pointer to the array of create control queues
 */
static void idpf_vf_ctlq_reg_init(struct idpf_adapter *adapter,
				  struct idpf_ctlq_create_info *cq)
{
	resource_size_t mbx_start = adapter->dev_ops.static_reg_info[0].start;
	int i;

	for (i = 0; i < IDPF_NUM_DFLT_MBX_Q; i++) {
		struct idpf_ctlq_create_info *ccq = cq + i;

		switch (ccq->type) {
		case IDPF_CTLQ_TYPE_MAILBOX_TX:
			/* set head and tail registers in our local struct */
			ccq->reg.head = VF_ATQH - mbx_start;
			ccq->reg.tail = VF_ATQT - mbx_start;
			ccq->reg.len = VF_ATQLEN - mbx_start;
			ccq->reg.bah = VF_ATQBAH - mbx_start;
			ccq->reg.bal = VF_ATQBAL - mbx_start;
			ccq->reg.len_mask = VF_ATQLEN_ATQLEN_M;
			ccq->reg.len_ena_mask = VF_ATQLEN_ATQENABLE_M;
			ccq->reg.head_mask = VF_ATQH_ATQH_M;
			break;
		case IDPF_CTLQ_TYPE_MAILBOX_RX:
			/* set head and tail registers in our local struct */
			ccq->reg.head = VF_ARQH - mbx_start;
			ccq->reg.tail = VF_ARQT - mbx_start;
			ccq->reg.len = VF_ARQLEN - mbx_start;
			ccq->reg.bah = VF_ARQBAH - mbx_start;
			ccq->reg.bal = VF_ARQBAL - mbx_start;
			ccq->reg.len_mask = VF_ARQLEN_ARQLEN_M;
			ccq->reg.len_ena_mask = VF_ARQLEN_ARQENABLE_M;
			ccq->reg.head_mask = VF_ARQH_ARQH_M;
			break;
		default:
			break;
		}
	}
}

/**
 * idpf_vf_mb_intr_reg_init - Initialize the mailbox register
 * @adapter: adapter structure
 */
static void idpf_vf_mb_intr_reg_init(struct idpf_adapter *adapter)
{
	struct idpf_intr_reg *intr = &adapter->mb_vector.intr_reg;
	u32 dyn_ctl = le32_to_cpu(adapter->caps.mailbox_dyn_ctl);

	intr->dyn_ctl = idpf_get_reg_addr(adapter, dyn_ctl);
	intr->dyn_ctl_intena_m = VF_INT_DYN_CTL0_INTENA_M;
	intr->dyn_ctl_itridx_m = VF_INT_DYN_CTL0_ITR_INDX_M;
	intr->icr_ena = idpf_get_reg_addr(adapter, VF_INT_ICR0_ENA1);
	intr->icr_ena_ctlq_m = VF_INT_ICR0_ENA1_ADMINQ_M;
}

/**
 * idpf_vf_intr_reg_init - Initialize interrupt registers
 * @vport: virtual port structure
 */
static int idpf_vf_intr_reg_init(struct idpf_vport *vport)
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
		intr->dyn_ctl_intena_m = VF_INT_DYN_CTLN_INTENA_M;
		intr->dyn_ctl_intena_msk_m = VF_INT_DYN_CTLN_INTENA_MSK_M;
		intr->dyn_ctl_itridx_s = VF_INT_DYN_CTLN_ITR_INDX_S;
		intr->dyn_ctl_intrvl_s = VF_INT_DYN_CTLN_INTERVAL_S;
		intr->dyn_ctl_wb_on_itr_m = VF_INT_DYN_CTLN_WB_ON_ITR_M;
		intr->dyn_ctl_swint_trig_m = VF_INT_DYN_CTLN_SWINT_TRIG_M;
		intr->dyn_ctl_sw_itridx_ena_m =
			VF_INT_DYN_CTLN_SW_ITR_INDX_ENA_M;

		spacing = IDPF_ITR_IDX_SPACING(reg_vals[vec_id].itrn_index_spacing,
					       IDPF_VF_ITR_IDX_SPACING);
		rx_itr = VF_INT_ITRN_ADDR(VIRTCHNL2_ITR_IDX_0,
					  reg_vals[vec_id].itrn_reg,
					  spacing);
		tx_itr = VF_INT_ITRN_ADDR(VIRTCHNL2_ITR_IDX_1,
					  reg_vals[vec_id].itrn_reg,
					  spacing);
		intr->rx_itr = idpf_get_reg_addr(adapter, rx_itr);
		intr->tx_itr = idpf_get_reg_addr(adapter, tx_itr);
	}

	/* Data vector for NOIRQ queues */

	val = reg_vals[vport->q_vector_idxs[i] - IDPF_MBX_Q_VEC].dyn_ctl_reg;
	vport->noirq_dyn_ctl = idpf_get_reg_addr(adapter, val);

	val = VF_INT_DYN_CTLN_WB_ON_ITR_M | VF_INT_DYN_CTLN_INTENA_MSK_M |
	      FIELD_PREP(VF_INT_DYN_CTLN_ITR_INDX_M, IDPF_NO_ITR_UPDATE_IDX);
	vport->noirq_dyn_ctl_ena = val;

free_reg_vals:
	kfree(reg_vals);

	return err;
}

/**
 * idpf_vf_reset_reg_init - Initialize reset registers
 * @adapter: Driver specific private structure
 */
static void idpf_vf_reset_reg_init(struct idpf_adapter *adapter)
{
	adapter->reset_reg.rstat = idpf_get_rstat_reg_addr(adapter, VFGEN_RSTAT);
	adapter->reset_reg.rstat_m = VFGEN_RSTAT_VFR_STATE_M;
}

/**
 * idpf_vf_trigger_reset - trigger reset
 * @adapter: Driver specific private structure
 * @trig_cause: Reason to trigger a reset
 */
static void idpf_vf_trigger_reset(struct idpf_adapter *adapter,
				  enum idpf_flags trig_cause)
{
	/* Do not send VIRTCHNL2_OP_RESET_VF message on driver unload */
	if (trig_cause == IDPF_HR_FUNC_RESET &&
	    !test_bit(IDPF_REMOVE_IN_PROG, adapter->flags))
		idpf_send_mb_msg(adapter, VIRTCHNL2_OP_RESET_VF, 0, NULL, 0);
}

/**
 * idpf_idc_vf_register - register for IDC callbacks
 * @adapter: Driver specific private structure
 *
 * Return: 0 on success or error code on failure.
 */
static int idpf_idc_vf_register(struct idpf_adapter *adapter)
{
	return idpf_idc_init_aux_core_dev(adapter, IIDC_FUNCTION_TYPE_VF);
}

/**
 * idpf_vf_reg_ops_init - Initialize register API function pointers
 * @adapter: Driver specific private structure
 */
static void idpf_vf_reg_ops_init(struct idpf_adapter *adapter)
{
	adapter->dev_ops.reg_ops.ctlq_reg_init = idpf_vf_ctlq_reg_init;
	adapter->dev_ops.reg_ops.intr_reg_init = idpf_vf_intr_reg_init;
	adapter->dev_ops.reg_ops.mb_intr_reg_init = idpf_vf_mb_intr_reg_init;
	adapter->dev_ops.reg_ops.reset_reg_init = idpf_vf_reset_reg_init;
	adapter->dev_ops.reg_ops.trigger_reset = idpf_vf_trigger_reset;
}

/**
 * idpf_vf_dev_ops_init - Initialize device API function pointers
 * @adapter: Driver specific private structure
 */
void idpf_vf_dev_ops_init(struct idpf_adapter *adapter)
{
	idpf_vf_reg_ops_init(adapter);

	adapter->dev_ops.idc_init = idpf_idc_vf_register;

	resource_set_range(&adapter->dev_ops.static_reg_info[0],
			   VF_BASE, IDPF_VF_MBX_REGION_SZ);
	resource_set_range(&adapter->dev_ops.static_reg_info[1],
			   VFGEN_RSTAT, IDPF_VF_RSTAT_REGION_SZ);
}
