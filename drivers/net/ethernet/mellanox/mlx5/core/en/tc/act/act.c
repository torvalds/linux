// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "act.h"
#include "en/tc_priv.h"
#include "mlx5_core.h"

/* Must be aligned with enum flow_action_id. */
static struct mlx5e_tc_act *tc_acts_fdb[NUM_FLOW_ACTIONS] = {
	&mlx5e_tc_act_accept,
	&mlx5e_tc_act_drop,
	&mlx5e_tc_act_trap,
	&mlx5e_tc_act_goto,
	&mlx5e_tc_act_mirred,
	&mlx5e_tc_act_mirred,
	NULL, /* FLOW_ACTION_REDIRECT_INGRESS, */
	NULL, /* FLOW_ACTION_MIRRED_INGRESS, */
	&mlx5e_tc_act_vlan,
	&mlx5e_tc_act_vlan,
	&mlx5e_tc_act_vlan_mangle,
	&mlx5e_tc_act_tun_encap,
	&mlx5e_tc_act_tun_decap,
	&mlx5e_tc_act_pedit,
	&mlx5e_tc_act_pedit,
	&mlx5e_tc_act_csum,
	NULL, /* FLOW_ACTION_MARK, */
	NULL, /* FLOW_ACTION_PTYPE, */
	NULL, /* FLOW_ACTION_PRIORITY, */
	NULL, /* FLOW_ACTION_WAKE, */
	NULL, /* FLOW_ACTION_QUEUE, */
	NULL, /* FLOW_ACTION_SAMPLE, */
	NULL, /* FLOW_ACTION_POLICE, */
	&mlx5e_tc_act_ct,
	NULL, /* FLOW_ACTION_CT_METADATA, */
	&mlx5e_tc_act_mpls_push,
	&mlx5e_tc_act_mpls_pop,
};

/* Must be aligned with enum flow_action_id. */
static struct mlx5e_tc_act *tc_acts_nic[NUM_FLOW_ACTIONS] = {
	&mlx5e_tc_act_accept,
	&mlx5e_tc_act_drop,
	NULL, /* FLOW_ACTION_TRAP, */
	&mlx5e_tc_act_goto,
	&mlx5e_tc_act_mirred_nic,
	NULL, /* FLOW_ACTION_MIRRED, */
	NULL, /* FLOW_ACTION_REDIRECT_INGRESS, */
	NULL, /* FLOW_ACTION_MIRRED_INGRESS, */
	NULL, /* FLOW_ACTION_VLAN_PUSH, */
	NULL, /* FLOW_ACTION_VLAN_POP, */
	NULL, /* FLOW_ACTION_VLAN_MANGLE, */
	NULL, /* FLOW_ACTION_TUNNEL_ENCAP, */
	NULL, /* FLOW_ACTION_TUNNEL_DECAP, */
	&mlx5e_tc_act_pedit,
	&mlx5e_tc_act_pedit,
	&mlx5e_tc_act_csum,
	&mlx5e_tc_act_mark,
	NULL, /* FLOW_ACTION_PTYPE, */
	NULL, /* FLOW_ACTION_PRIORITY, */
	NULL, /* FLOW_ACTION_WAKE, */
	NULL, /* FLOW_ACTION_QUEUE, */
	NULL, /* FLOW_ACTION_SAMPLE, */
	NULL, /* FLOW_ACTION_POLICE, */
	&mlx5e_tc_act_ct,
};

/**
 * mlx5e_tc_act_get() - Get an action parser for an action id.
 * @act_id: Flow action id.
 * @ns_type: flow namespace type.
 */
struct mlx5e_tc_act *
mlx5e_tc_act_get(enum flow_action_id act_id,
		 enum mlx5_flow_namespace_type ns_type)
{
	struct mlx5e_tc_act **tc_acts;

	tc_acts = ns_type == MLX5_FLOW_NAMESPACE_FDB ? tc_acts_fdb : tc_acts_nic;

	return tc_acts[act_id];
}

/**
 * mlx5e_tc_act_init_parse_state() - Init a new parse_state.
 * @parse_state: Parsing state.
 * @flow:        mlx5e tc flow being handled.
 * @flow_action: flow action to parse.
 * @extack:      to set an error msg.
 *
 * The same parse_state should be passed to action parsers
 * for tracking the current parsing state.
 */
void
mlx5e_tc_act_init_parse_state(struct mlx5e_tc_act_parse_state *parse_state,
			      struct mlx5e_tc_flow *flow,
			      struct flow_action *flow_action,
			      struct netlink_ext_ack *extack)
{
	memset(parse_state, 0, sizeof(*parse_state));
	parse_state->flow = flow;
	parse_state->num_actions = flow_action->num_entries;
	parse_state->extack = extack;
}
