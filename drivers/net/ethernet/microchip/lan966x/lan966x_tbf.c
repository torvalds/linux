// SPDX-License-Identifier: GPL-2.0+

#include "lan966x_main.h"

int lan966x_tbf_add(struct lan966x_port *port,
		    struct tc_tbf_qopt_offload *qopt)
{
	struct lan966x *lan966x = port->lan966x;
	bool root = qopt->parent == TC_H_ROOT;
	u32 queue = 0;
	u32 cir, cbs;
	u32 se_idx;

	if (!root) {
		queue = TC_H_MIN(qopt->parent) - 1;
		if (queue >= NUM_PRIO_QUEUES)
			return -EOPNOTSUPP;
	}

	if (root)
		se_idx = SE_IDX_PORT + port->chip_port;
	else
		se_idx = SE_IDX_QUEUE + port->chip_port * NUM_PRIO_QUEUES + queue;

	cir = div_u64(qopt->replace_params.rate.rate_bytes_ps, 1000) * 8;
	cbs = qopt->replace_params.max_size;

	/* Rate unit is 100 kbps */
	cir = DIV_ROUND_UP(cir, 100);
	/* Avoid using zero rate */
	cir = cir ?: 1;
	/* Burst unit is 4kB */
	cbs = DIV_ROUND_UP(cbs, 4096);
	/* Avoid using zero burst */
	cbs = cbs ?: 1;

	/* Check that actually the result can be written */
	if (cir > GENMASK(15, 0) ||
	    cbs > GENMASK(6, 0))
		return -EINVAL;

	lan_rmw(QSYS_SE_CFG_SE_AVB_ENA_SET(0) |
		QSYS_SE_CFG_SE_FRM_MODE_SET(1),
		QSYS_SE_CFG_SE_AVB_ENA |
		QSYS_SE_CFG_SE_FRM_MODE,
		lan966x, QSYS_SE_CFG(se_idx));

	lan_wr(QSYS_CIR_CFG_CIR_RATE_SET(cir) |
	       QSYS_CIR_CFG_CIR_BURST_SET(cbs),
	       lan966x, QSYS_CIR_CFG(se_idx));

	return 0;
}

int lan966x_tbf_del(struct lan966x_port *port,
		    struct tc_tbf_qopt_offload *qopt)
{
	struct lan966x *lan966x = port->lan966x;
	bool root = qopt->parent == TC_H_ROOT;
	u32 queue = 0;
	u32 se_idx;

	if (!root) {
		queue = TC_H_MIN(qopt->parent) - 1;
		if (queue >= NUM_PRIO_QUEUES)
			return -EOPNOTSUPP;
	}

	if (root)
		se_idx = SE_IDX_PORT + port->chip_port;
	else
		se_idx = SE_IDX_QUEUE + port->chip_port * NUM_PRIO_QUEUES + queue;

	lan_rmw(QSYS_SE_CFG_SE_AVB_ENA_SET(0) |
		QSYS_SE_CFG_SE_FRM_MODE_SET(0),
		QSYS_SE_CFG_SE_AVB_ENA |
		QSYS_SE_CFG_SE_FRM_MODE,
		lan966x, QSYS_SE_CFG(se_idx));

	lan_wr(QSYS_CIR_CFG_CIR_RATE_SET(0) |
	       QSYS_CIR_CFG_CIR_BURST_SET(0),
	       lan966x, QSYS_CIR_CFG(se_idx));

	return 0;
}
