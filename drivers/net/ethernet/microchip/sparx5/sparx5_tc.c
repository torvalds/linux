// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 */

#include <net/pkt_cls.h>

#include "sparx5_tc.h"
#include "sparx5_main.h"
#include "sparx5_qos.h"

static int sparx5_tc_setup_qdisc_mqprio(struct net_device *ndev,
					struct tc_mqprio_qopt_offload *m)
{
	m->qopt.hw = TC_MQPRIO_HW_OFFLOAD_TCS;

	if (m->qopt.num_tc == 0)
		return sparx5_tc_mqprio_del(ndev);
	else
		return sparx5_tc_mqprio_add(ndev, m->qopt.num_tc);
}

int sparx5_port_setup_tc(struct net_device *ndev, enum tc_setup_type type,
			 void *type_data)
{
	switch (type) {
	case TC_SETUP_QDISC_MQPRIO:
		return sparx5_tc_setup_qdisc_mqprio(ndev, type_data);
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}
