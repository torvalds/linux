// SPDX-License-Identifier: GPL-2.0+

#include "lan966x_main.h"

#define DWRR_COST_BIT_WIDTH	BIT(5)

static u32 lan966x_ets_hw_cost(u32 w_min, u32 weight)
{
	u32 res;

	/* Round half up: Multiply with 16 before division,
	 * add 8 and divide result with 16 again
	 */
	res = (((DWRR_COST_BIT_WIDTH << 4) * w_min / weight) + 8) >> 4;
	return max_t(u32, 1, res) - 1;
}

int lan966x_ets_add(struct lan966x_port *port,
		    struct tc_ets_qopt_offload *qopt)
{
	struct tc_ets_qopt_offload_replace_params *params;
	struct lan966x *lan966x = port->lan966x;
	u32 w_min = 100;
	u8 count = 0;
	u32 se_idx;
	u8 i;

	/* Check the input */
	if (qopt->parent != TC_H_ROOT)
		return -EINVAL;

	params = &qopt->replace_params;
	if (params->bands != NUM_PRIO_QUEUES)
		return -EINVAL;

	for (i = 0; i < params->bands; ++i) {
		/* In the switch the DWRR is always on the lowest consecutive
		 * priorities. Due to this, the first priority must map to the
		 * first DWRR band.
		 */
		if (params->priomap[i] != (7 - i))
			return -EINVAL;

		if (params->quanta[i] && params->weights[i] == 0)
			return -EINVAL;
	}

	se_idx = SE_IDX_PORT + port->chip_port;

	/* Find minimum weight */
	for (i = 0; i < params->bands; ++i) {
		if (params->quanta[i] == 0)
			continue;

		w_min = min(w_min, params->weights[i]);
	}

	for (i = 0; i < params->bands; ++i) {
		if (params->quanta[i] == 0)
			continue;

		++count;

		lan_wr(lan966x_ets_hw_cost(w_min, params->weights[i]),
		       lan966x, QSYS_SE_DWRR_CFG(se_idx, 7 - i));
	}

	lan_rmw(QSYS_SE_CFG_SE_DWRR_CNT_SET(count) |
		QSYS_SE_CFG_SE_RR_ENA_SET(0),
		QSYS_SE_CFG_SE_DWRR_CNT |
		QSYS_SE_CFG_SE_RR_ENA,
		lan966x, QSYS_SE_CFG(se_idx));

	return 0;
}

int lan966x_ets_del(struct lan966x_port *port,
		    struct tc_ets_qopt_offload *qopt)
{
	struct lan966x *lan966x = port->lan966x;
	u32 se_idx;
	int i;

	se_idx = SE_IDX_PORT + port->chip_port;

	for (i = 0; i < NUM_PRIO_QUEUES; ++i)
		lan_wr(0, lan966x, QSYS_SE_DWRR_CFG(se_idx, i));

	lan_rmw(QSYS_SE_CFG_SE_DWRR_CNT_SET(0) |
		QSYS_SE_CFG_SE_RR_ENA_SET(0),
		QSYS_SE_CFG_SE_DWRR_CNT |
		QSYS_SE_CFG_SE_RR_ENA,
		lan966x, QSYS_SE_CFG(se_idx));

	return 0;
}
