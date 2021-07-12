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
};

/* Must be aligned with enum flow_action_id. */
static struct mlx5e_tc_act *tc_acts_nic[NUM_FLOW_ACTIONS] = {
	&mlx5e_tc_act_accept,
	&mlx5e_tc_act_drop,
	NULL, /* FLOW_ACTION_TRAP, */
	NULL, /* FLOW_ACTION_GOTO, */
	NULL, /* FLOW_ACTION_REDIRECT, */
	NULL, /* FLOW_ACTION_MIRRED, */
	NULL, /* FLOW_ACTION_REDIRECT_INGRESS, */
	NULL, /* FLOW_ACTION_MIRRED_INGRESS, */
	NULL, /* FLOW_ACTION_VLAN_PUSH, */
	NULL, /* FLOW_ACTION_VLAN_POP, */
	NULL, /* FLOW_ACTION_VLAN_MANGLE, */
	NULL, /* FLOW_ACTION_TUNNEL_ENCAP, */
	NULL, /* FLOW_ACTION_TUNNEL_DECAP, */
	NULL, /* FLOW_ACTION_MANGLE, */
	NULL, /* FLOW_ACTION_ADD, */
	NULL, /* FLOW_ACTION_CSUM, */
	&mlx5e_tc_act_mark,
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
