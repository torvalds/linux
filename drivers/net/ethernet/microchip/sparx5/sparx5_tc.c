// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 */

#include <net/pkt_cls.h>

#include "sparx5_tc.h"
#include "sparx5_main.h"
#include "sparx5_qos.h"

static void sparx5_tc_get_layer_and_idx(u32 parent, u32 portno, u32 *layer,
					u32 *idx)
{
	if (parent == TC_H_ROOT) {
		*layer = 2;
		*idx = portno;
	} else {
		u32 queue = TC_H_MIN(parent) - 1;
		*layer = 0;
		*idx = SPX5_HSCH_L0_GET_IDX(portno, queue);
	}
}

static int sparx5_tc_setup_qdisc_mqprio(struct net_device *ndev,
					struct tc_mqprio_qopt_offload *m)
{
	m->qopt.hw = TC_MQPRIO_HW_OFFLOAD_TCS;

	if (m->qopt.num_tc == 0)
		return sparx5_tc_mqprio_del(ndev);
	else
		return sparx5_tc_mqprio_add(ndev, m->qopt.num_tc);
}

static int sparx5_tc_setup_qdisc_tbf(struct net_device *ndev,
				     struct tc_tbf_qopt_offload *qopt)
{
	struct sparx5_port *port = netdev_priv(ndev);
	u32 layer, se_idx;

	sparx5_tc_get_layer_and_idx(qopt->parent, port->portno, &layer,
				    &se_idx);

	switch (qopt->command) {
	case TC_TBF_REPLACE:
		return sparx5_tc_tbf_add(port, &qopt->replace_params, layer,
					 se_idx);
	case TC_TBF_DESTROY:
		return sparx5_tc_tbf_del(port, layer, se_idx);
	case TC_TBF_STATS:
		return -EOPNOTSUPP;
	default:
		return -EOPNOTSUPP;
	}

	return -EOPNOTSUPP;
}

int sparx5_port_setup_tc(struct net_device *ndev, enum tc_setup_type type,
			 void *type_data)
{
	switch (type) {
	case TC_SETUP_QDISC_MQPRIO:
		return sparx5_tc_setup_qdisc_mqprio(ndev, type_data);
	case TC_SETUP_QDISC_TBF:
		return sparx5_tc_setup_qdisc_tbf(ndev, type_data);
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}
