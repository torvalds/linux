// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023 Intel Corporation */

#include "idpf.h"
#include "idpf_lan_pf_regs.h"

/**
 * idpf_ctlq_reg_init - initialize default mailbox registers
 * @cq: pointer to the array of create control queues
 */
static void idpf_ctlq_reg_init(struct idpf_ctlq_create_info *cq)
{
	int i;

	for (i = 0; i < IDPF_NUM_DFLT_MBX_Q; i++) {
		struct idpf_ctlq_create_info *ccq = cq + i;

		switch (ccq->type) {
		case IDPF_CTLQ_TYPE_MAILBOX_TX:
			/* set head and tail registers in our local struct */
			ccq->reg.head = PF_FW_ATQH;
			ccq->reg.tail = PF_FW_ATQT;
			ccq->reg.len = PF_FW_ATQLEN;
			ccq->reg.bah = PF_FW_ATQBAH;
			ccq->reg.bal = PF_FW_ATQBAL;
			ccq->reg.len_mask = PF_FW_ATQLEN_ATQLEN_M;
			ccq->reg.len_ena_mask = PF_FW_ATQLEN_ATQENABLE_M;
			ccq->reg.head_mask = PF_FW_ATQH_ATQH_M;
			break;
		case IDPF_CTLQ_TYPE_MAILBOX_RX:
			/* set head and tail registers in our local struct */
			ccq->reg.head = PF_FW_ARQH;
			ccq->reg.tail = PF_FW_ARQT;
			ccq->reg.len = PF_FW_ARQLEN;
			ccq->reg.bah = PF_FW_ARQBAH;
			ccq->reg.bal = PF_FW_ARQBAL;
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
 * idpf_reset_reg_init - Initialize reset registers
 * @adapter: Driver specific private structure
 */
static void idpf_reset_reg_init(struct idpf_adapter *adapter)
{
	adapter->reset_reg.rstat = idpf_get_reg_addr(adapter, PFGEN_RSTAT);
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

	reset_reg = readl(idpf_get_reg_addr(adapter, PFGEN_CTRL));
	writel(reset_reg | PFGEN_CTRL_PFSWR,
	       idpf_get_reg_addr(adapter, PFGEN_CTRL));
}

/**
 * idpf_reg_ops_init - Initialize register API function pointers
 * @adapter: Driver specific private structure
 */
static void idpf_reg_ops_init(struct idpf_adapter *adapter)
{
	adapter->dev_ops.reg_ops.ctlq_reg_init = idpf_ctlq_reg_init;
	adapter->dev_ops.reg_ops.reset_reg_init = idpf_reset_reg_init;
	adapter->dev_ops.reg_ops.trigger_reset = idpf_trigger_reset;
}

/**
 * idpf_dev_ops_init - Initialize device API function pointers
 * @adapter: Driver specific private structure
 */
void idpf_dev_ops_init(struct idpf_adapter *adapter)
{
	idpf_reg_ops_init(adapter);
}
