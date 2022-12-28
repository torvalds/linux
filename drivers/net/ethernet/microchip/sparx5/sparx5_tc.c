// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 */

#include <net/pkt_cls.h>

#include "sparx5_tc.h"
#include "sparx5_main.h"
#include "sparx5_qos.h"

/* tc block handling */
static LIST_HEAD(sparx5_block_cb_list);

static int sparx5_tc_block_cb(enum tc_setup_type type,
			      void *type_data,
			      void *cb_priv, bool ingress)
{
	struct net_device *ndev = cb_priv;

	switch (type) {
	case TC_SETUP_CLSMATCHALL:
		return sparx5_tc_matchall(ndev, type_data, ingress);
	case TC_SETUP_CLSFLOWER:
		return sparx5_tc_flower(ndev, type_data, ingress);
	default:
		return -EOPNOTSUPP;
	}
}

static int sparx5_tc_block_cb_ingress(enum tc_setup_type type,
				      void *type_data,
				      void *cb_priv)
{
	return sparx5_tc_block_cb(type, type_data, cb_priv, true);
}

static int sparx5_tc_block_cb_egress(enum tc_setup_type type,
				     void *type_data,
				     void *cb_priv)
{
	return sparx5_tc_block_cb(type, type_data, cb_priv, false);
}

static int sparx5_tc_setup_block(struct net_device *ndev,
				 struct flow_block_offload *fbo)
{
	flow_setup_cb_t *cb;

	if (fbo->binder_type == FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS)
		cb = sparx5_tc_block_cb_ingress;
	else if (fbo->binder_type == FLOW_BLOCK_BINDER_TYPE_CLSACT_EGRESS)
		cb = sparx5_tc_block_cb_egress;
	else
		return -EOPNOTSUPP;

	return flow_block_cb_setup_simple(fbo, &sparx5_block_cb_list,
					  cb, ndev, ndev, false);
}

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

static int sparx5_tc_setup_qdisc_ets(struct net_device *ndev,
				     struct tc_ets_qopt_offload *qopt)
{
	struct tc_ets_qopt_offload_replace_params *params =
		&qopt->replace_params;
	struct sparx5_port *port = netdev_priv(ndev);
	int i;

	/* Only allow ets on ports  */
	if (qopt->parent != TC_H_ROOT)
		return -EOPNOTSUPP;

	switch (qopt->command) {
	case TC_ETS_REPLACE:

		/* We support eight priorities */
		if (params->bands != SPX5_PRIOS)
			return -EOPNOTSUPP;

		/* Sanity checks */
		for (i = 0; i < SPX5_PRIOS; ++i) {
			/* Priority map is *always* reverse e.g: 7 6 5 .. 0 */
			if (params->priomap[i] != (7 - i))
				return -EOPNOTSUPP;
			/* Throw an error if we receive zero weights by tc */
			if (params->quanta[i] && params->weights[i] == 0) {
				pr_err("Invalid ets configuration; band %d has weight zero",
				       i);
				return -EINVAL;
			}
		}

		return sparx5_tc_ets_add(port, params);
	case TC_ETS_DESTROY:

		return sparx5_tc_ets_del(port);
	case TC_ETS_GRAFT:
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
	case TC_SETUP_BLOCK:
		return sparx5_tc_setup_block(ndev, type_data);
	case TC_SETUP_QDISC_MQPRIO:
		return sparx5_tc_setup_qdisc_mqprio(ndev, type_data);
	case TC_SETUP_QDISC_TBF:
		return sparx5_tc_setup_qdisc_tbf(ndev, type_data);
	case TC_SETUP_QDISC_ETS:
		return sparx5_tc_setup_qdisc_ets(ndev, type_data);
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}
