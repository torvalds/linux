// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023 Intel Corporation */

#include "idpf.h"
#include "idpf_lan_vf_regs.h"

/**
 * idpf_vf_ctlq_reg_init - initialize default mailbox registers
 * @cq: pointer to the array of create control queues
 */
static void idpf_vf_ctlq_reg_init(struct idpf_ctlq_create_info *cq)
{
	int i;

	for (i = 0; i < IDPF_NUM_DFLT_MBX_Q; i++) {
		struct idpf_ctlq_create_info *ccq = cq + i;

		switch (ccq->type) {
		case IDPF_CTLQ_TYPE_MAILBOX_TX:
			/* set head and tail registers in our local struct */
			ccq->reg.head = VF_ATQH;
			ccq->reg.tail = VF_ATQT;
			ccq->reg.len = VF_ATQLEN;
			ccq->reg.bah = VF_ATQBAH;
			ccq->reg.bal = VF_ATQBAL;
			ccq->reg.len_mask = VF_ATQLEN_ATQLEN_M;
			ccq->reg.len_ena_mask = VF_ATQLEN_ATQENABLE_M;
			ccq->reg.head_mask = VF_ATQH_ATQH_M;
			break;
		case IDPF_CTLQ_TYPE_MAILBOX_RX:
			/* set head and tail registers in our local struct */
			ccq->reg.head = VF_ARQH;
			ccq->reg.tail = VF_ARQT;
			ccq->reg.len = VF_ARQLEN;
			ccq->reg.bah = VF_ARQBAH;
			ccq->reg.bal = VF_ARQBAL;
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
 * idpf_vf_reset_reg_init - Initialize reset registers
 * @adapter: Driver specific private structure
 */
static void idpf_vf_reset_reg_init(struct idpf_adapter *adapter)
{
	adapter->reset_reg.rstat = idpf_get_reg_addr(adapter, VFGEN_RSTAT);
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
	/* stub */
}

/**
 * idpf_vf_reg_ops_init - Initialize register API function pointers
 * @adapter: Driver specific private structure
 */
static void idpf_vf_reg_ops_init(struct idpf_adapter *adapter)
{
	adapter->dev_ops.reg_ops.ctlq_reg_init = idpf_vf_ctlq_reg_init;
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
}
