// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "act.h"
#include "en/tc/post_act.h"
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
	&mlx5e_tc_act_redirect_ingress,
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
	&mlx5e_tc_act_ptype,
	NULL, /* FLOW_ACTION_PRIORITY, */
	NULL, /* FLOW_ACTION_WAKE, */
	NULL, /* FLOW_ACTION_QUEUE, */
	&mlx5e_tc_act_sample,
	NULL, /* FLOW_ACTION_POLICE, */
	&mlx5e_tc_act_ct,
	NULL, /* FLOW_ACTION_CT_METADATA, */
	&mlx5e_tc_act_mpls_push,
	&mlx5e_tc_act_mpls_pop,
	NULL, /* FLOW_ACTION_MPLS_MANGLE, */
	NULL, /* FLOW_ACTION_GATE, */
	NULL, /* FLOW_ACTION_PPPOE_PUSH, */
	NULL, /* FLOW_ACTION_JUMP, */
	NULL, /* FLOW_ACTION_PIPE, */
	&mlx5e_tc_act_vlan,
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

void
mlx5e_tc_act_reorder_flow_actions(struct flow_action *flow_action,
				  struct mlx5e_tc_flow_action *flow_action_reorder)
{
	struct flow_action_entry *act;
	int i, j = 0;

	flow_action_for_each(i, act, flow_action) {
		/* Add CT action to be first. */
		if (act->id == FLOW_ACTION_CT)
			flow_action_reorder->entries[j++] = act;
	}

	flow_action_for_each(i, act, flow_action) {
		if (act->id == FLOW_ACTION_CT)
			continue;
		flow_action_reorder->entries[j++] = act;
	}
}

int
mlx5e_tc_act_post_parse(struct mlx5e_tc_act_parse_state *parse_state,
			struct flow_action *flow_action,
			struct mlx5_flow_attr *attr,
			enum mlx5_flow_namespace_type ns_type)
{
	struct flow_action_entry *act;
	struct mlx5e_tc_act *tc_act;
	struct mlx5e_priv *priv;
	int err = 0, i;

	priv = parse_state->flow->priv;

	flow_action_for_each(i, act, flow_action) {
		tc_act = mlx5e_tc_act_get(act->id, ns_type);
		if (!tc_act || !tc_act->post_parse ||
		    !tc_act->can_offload(parse_state, act, i, attr))
			continue;

		err = tc_act->post_parse(parse_state, priv, attr);
		if (err)
			goto out;
	}

out:
	return err;
}

int
mlx5e_tc_act_set_next_post_act(struct mlx5e_tc_flow *flow,
			       struct mlx5_flow_attr *attr,
			       struct mlx5_flow_attr *next_attr)
{
	struct mlx5_core_dev *mdev = flow->priv->mdev;
	struct mlx5e_tc_mod_hdr_acts *mod_acts;
	int err;

	mod_acts = &attr->parse_attr->mod_hdr_acts;

	/* Set handle on current post act rule to next post act rule. */
	err = mlx5e_tc_post_act_set_handle(mdev, next_attr->post_act_handle, mod_acts);
	if (err) {
		mlx5_core_warn(mdev, "Failed setting post action handle");
		return err;
	}

	attr->action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
			MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;

	return 0;
}
