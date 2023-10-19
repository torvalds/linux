// SPDX-License-Identifier: GPL-2.0+

#include "lan966x_main.h"

int lan966x_cbs_add(struct lan966x_port *port,
		    struct tc_cbs_qopt_offload *qopt)
{
	struct lan966x *lan966x = port->lan966x;
	u32 cir, cbs;
	u8 se_idx;

	/* Check for invalid values */
	if (qopt->idleslope <= 0 ||
	    qopt->sendslope >= 0 ||
	    qopt->locredit >= qopt->hicredit)
		return -EINVAL;

	se_idx = SE_IDX_QUEUE + port->chip_port * NUM_PRIO_QUEUES + qopt->queue;
	cir = qopt->idleslope;
	cbs = (qopt->idleslope - qopt->sendslope) *
		(qopt->hicredit - qopt->locredit) /
		-qopt->sendslope;

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

	lan_rmw(QSYS_SE_CFG_SE_AVB_ENA_SET(1) |
		QSYS_SE_CFG_SE_FRM_MODE_SET(1),
		QSYS_SE_CFG_SE_AVB_ENA |
		QSYS_SE_CFG_SE_FRM_MODE,
		lan966x, QSYS_SE_CFG(se_idx));

	lan_wr(QSYS_CIR_CFG_CIR_RATE_SET(cir) |
	       QSYS_CIR_CFG_CIR_BURST_SET(cbs),
	       lan966x, QSYS_CIR_CFG(se_idx));

	return 0;
}

int lan966x_cbs_del(struct lan966x_port *port,
		    struct tc_cbs_qopt_offload *qopt)
{
	struct lan966x *lan966x = port->lan966x;
	u8 se_idx;

	se_idx = SE_IDX_QUEUE + port->chip_port * NUM_PRIO_QUEUES + qopt->queue;

	lan_rmw(QSYS_SE_CFG_SE_AVB_ENA_SET(1) |
		QSYS_SE_CFG_SE_FRM_MODE_SET(0),
		QSYS_SE_CFG_SE_AVB_ENA |
		QSYS_SE_CFG_SE_FRM_MODE,
		lan966x, QSYS_SE_CFG(se_idx));

	lan_wr(QSYS_CIR_CFG_CIR_RATE_SET(0) |
	       QSYS_CIR_CFG_CIR_BURST_SET(0),
	       lan966x, QSYS_CIR_CFG(se_idx));

	return 0;
}
