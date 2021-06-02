// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2021 Corigine, Inc. */

#include "conntrack.h"

bool is_pre_ct_flow(struct flow_cls_offload *flow)
{
	struct flow_action_entry *act;
	int i;

	flow_action_for_each(i, act, &flow->rule->action) {
		if (act->id == FLOW_ACTION_CT && !act->ct.action)
			return true;
	}
	return false;
}

bool is_post_ct_flow(struct flow_cls_offload *flow)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(flow);
	struct flow_dissector *dissector = rule->match.dissector;
	struct flow_match_ct ct;

	if (dissector->used_keys & BIT(FLOW_DISSECTOR_KEY_CT)) {
		flow_rule_match_ct(rule, &ct);
		if (ct.key->ct_state & TCA_FLOWER_KEY_CT_FLAGS_ESTABLISHED)
			return true;
	}
	return false;
}

int nfp_fl_ct_handle_pre_ct(struct nfp_flower_priv *priv,
			    struct net_device *netdev,
			    struct flow_cls_offload *flow,
			    struct netlink_ext_ack *extack)
{
	NL_SET_ERR_MSG_MOD(extack, "unsupported offload: Conntrack action not supported");
	return -EOPNOTSUPP;
}

int nfp_fl_ct_handle_post_ct(struct nfp_flower_priv *priv,
			     struct net_device *netdev,
			     struct flow_cls_offload *flow,
			     struct netlink_ext_ack *extack)
{
	NL_SET_ERR_MSG_MOD(extack, "unsupported offload: Conntrack match not supported");
	return -EOPNOTSUPP;
}
