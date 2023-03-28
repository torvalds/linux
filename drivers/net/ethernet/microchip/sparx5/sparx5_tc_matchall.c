// SPDX-License-Identifier: GPL-2.0+
/* Microchip VCAP API
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 */

#include "sparx5_tc.h"
#include "vcap_api.h"
#include "vcap_api_client.h"
#include "sparx5_main_regs.h"
#include "sparx5_main.h"
#include "sparx5_vcap_impl.h"

static int sparx5_tc_matchall_replace(struct net_device *ndev,
				      struct tc_cls_matchall_offload *tmo,
				      bool ingress)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct flow_action_entry *action;
	struct sparx5 *sparx5;
	int err;

	if (!flow_offload_has_one_action(&tmo->rule->action)) {
		NL_SET_ERR_MSG_MOD(tmo->common.extack,
				   "Only one action per filter is supported");
		return -EOPNOTSUPP;
	}
	action = &tmo->rule->action.entries[0];

	sparx5 = port->sparx5;
	switch (action->id) {
	case FLOW_ACTION_GOTO:
		err = vcap_enable_lookups(sparx5->vcap_ctrl, ndev,
					  tmo->common.chain_index,
					  action->chain_index, tmo->cookie,
					  true);
		if (err == -EFAULT) {
			NL_SET_ERR_MSG_MOD(tmo->common.extack,
					   "Unsupported goto chain");
			return -EOPNOTSUPP;
		}
		if (err == -EADDRINUSE) {
			NL_SET_ERR_MSG_MOD(tmo->common.extack,
					   "VCAP already enabled");
			return -EOPNOTSUPP;
		}
		if (err == -EADDRNOTAVAIL) {
			NL_SET_ERR_MSG_MOD(tmo->common.extack,
					   "Already matching this chain");
			return -EOPNOTSUPP;
		}
		if (err) {
			NL_SET_ERR_MSG_MOD(tmo->common.extack,
					   "Could not enable VCAP lookups");
			return err;
		}
		break;
	default:
		NL_SET_ERR_MSG_MOD(tmo->common.extack, "Unsupported action");
		return -EOPNOTSUPP;
	}
	return 0;
}

static int sparx5_tc_matchall_destroy(struct net_device *ndev,
				      struct tc_cls_matchall_offload *tmo,
				      bool ingress)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct sparx5 *sparx5;
	int err;

	sparx5 = port->sparx5;
	if (!tmo->rule && tmo->cookie) {
		err = vcap_enable_lookups(sparx5->vcap_ctrl, ndev,
					  0, 0, tmo->cookie, false);
		if (err)
			return err;
		return 0;
	}
	NL_SET_ERR_MSG_MOD(tmo->common.extack, "Unsupported action");
	return -EOPNOTSUPP;
}

int sparx5_tc_matchall(struct net_device *ndev,
		       struct tc_cls_matchall_offload *tmo,
		       bool ingress)
{
	switch (tmo->command) {
	case TC_CLSMATCHALL_REPLACE:
		return sparx5_tc_matchall_replace(ndev, tmo, ingress);
	case TC_CLSMATCHALL_DESTROY:
		return sparx5_tc_matchall_destroy(ndev, tmo, ingress);
	default:
		return -EOPNOTSUPP;
	}
}
