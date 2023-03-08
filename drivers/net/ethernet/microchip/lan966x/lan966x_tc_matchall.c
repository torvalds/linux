// SPDX-License-Identifier: GPL-2.0+

#include "lan966x_main.h"

static int lan966x_tc_matchall_add(struct lan966x_port *port,
				   struct tc_cls_matchall_offload *f,
				   bool ingress)
{
	struct flow_action_entry *act;

	if (!flow_offload_has_one_action(&f->rule->action)) {
		NL_SET_ERR_MSG_MOD(f->common.extack,
				   "Only once action per filter is supported");
		return -EOPNOTSUPP;
	}

	act = &f->rule->action.entries[0];
	switch (act->id) {
	case FLOW_ACTION_POLICE:
		return lan966x_police_port_add(port, &f->rule->action, act,
					       f->cookie, ingress,
					       f->common.extack);
	case FLOW_ACTION_MIRRED:
		return lan966x_mirror_port_add(port, act, f->cookie,
					       ingress, f->common.extack);
	case FLOW_ACTION_GOTO:
		return lan966x_goto_port_add(port, f->common.chain_index,
					     act->chain_index, f->cookie,
					     f->common.extack);
	default:
		NL_SET_ERR_MSG_MOD(f->common.extack,
				   "Unsupported action");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int lan966x_tc_matchall_del(struct lan966x_port *port,
				   struct tc_cls_matchall_offload *f,
				   bool ingress)
{
	if (f->cookie == port->tc.police_id) {
		return lan966x_police_port_del(port, f->cookie,
					       f->common.extack);
	} else if (f->cookie == port->tc.ingress_mirror_id ||
		   f->cookie == port->tc.egress_mirror_id) {
		return lan966x_mirror_port_del(port, ingress,
					       f->common.extack);
	} else {
		return lan966x_goto_port_del(port, f->cookie, f->common.extack);
	}

	return 0;
}

static int lan966x_tc_matchall_stats(struct lan966x_port *port,
				     struct tc_cls_matchall_offload *f,
				     bool ingress)
{
	if (f->cookie == port->tc.police_id) {
		lan966x_police_port_stats(port, &f->stats);
	} else if (f->cookie == port->tc.ingress_mirror_id ||
		   f->cookie == port->tc.egress_mirror_id) {
		lan966x_mirror_port_stats(port, &f->stats, ingress);
	} else {
		NL_SET_ERR_MSG_MOD(f->common.extack,
				   "Unsupported action");
		return -EOPNOTSUPP;
	}

	return 0;
}

int lan966x_tc_matchall(struct lan966x_port *port,
			struct tc_cls_matchall_offload *f,
			bool ingress)
{
	switch (f->command) {
	case TC_CLSMATCHALL_REPLACE:
		return lan966x_tc_matchall_add(port, f, ingress);
	case TC_CLSMATCHALL_DESTROY:
		return lan966x_tc_matchall_del(port, f, ingress);
	case TC_CLSMATCHALL_STATS:
		return lan966x_tc_matchall_stats(port, f, ingress);
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}
