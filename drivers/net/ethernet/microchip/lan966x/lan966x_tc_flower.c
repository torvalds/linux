// SPDX-License-Identifier: GPL-2.0+

#include "lan966x_main.h"
#include "vcap_api.h"
#include "vcap_api_client.h"

struct lan966x_tc_flower_parse_usage {
	struct flow_cls_offload *f;
	struct flow_rule *frule;
	struct vcap_rule *vrule;
	unsigned int used_keys;
	u16 l3_proto;
};

static int lan966x_tc_flower_handler_ethaddr_usage(struct lan966x_tc_flower_parse_usage *st)
{
	enum vcap_key_field smac_key = VCAP_KF_L2_SMAC;
	enum vcap_key_field dmac_key = VCAP_KF_L2_DMAC;
	struct flow_match_eth_addrs match;
	struct vcap_u48_key smac, dmac;
	int err = 0;

	flow_rule_match_eth_addrs(st->frule, &match);

	if (!is_zero_ether_addr(match.mask->src)) {
		vcap_netbytes_copy(smac.value, match.key->src, ETH_ALEN);
		vcap_netbytes_copy(smac.mask, match.mask->src, ETH_ALEN);
		err = vcap_rule_add_key_u48(st->vrule, smac_key, &smac);
		if (err)
			goto out;
	}

	if (!is_zero_ether_addr(match.mask->dst)) {
		vcap_netbytes_copy(dmac.value, match.key->dst, ETH_ALEN);
		vcap_netbytes_copy(dmac.mask, match.mask->dst, ETH_ALEN);
		err = vcap_rule_add_key_u48(st->vrule, dmac_key, &dmac);
		if (err)
			goto out;
	}

	st->used_keys |= BIT(FLOW_DISSECTOR_KEY_ETH_ADDRS);

	return err;

out:
	NL_SET_ERR_MSG_MOD(st->f->common.extack, "eth_addr parse error");
	return err;
}

static int
(*lan966x_tc_flower_handlers_usage[])(struct lan966x_tc_flower_parse_usage *st) = {
	[FLOW_DISSECTOR_KEY_ETH_ADDRS] = lan966x_tc_flower_handler_ethaddr_usage,
};

static int lan966x_tc_flower_use_dissectors(struct flow_cls_offload *f,
					    struct vcap_admin *admin,
					    struct vcap_rule *vrule,
					    u16 *l3_proto)
{
	struct lan966x_tc_flower_parse_usage state = {
		.f = f,
		.vrule = vrule,
		.l3_proto = ETH_P_ALL,
	};
	int err = 0;

	state.frule = flow_cls_offload_flow_rule(f);
	for (int i = 0; i < ARRAY_SIZE(lan966x_tc_flower_handlers_usage); ++i) {
		if (!flow_rule_match_key(state.frule, i) ||
		    !lan966x_tc_flower_handlers_usage[i])
			continue;

		err = lan966x_tc_flower_handlers_usage[i](&state);
		if (err)
			return err;
	}

	if (l3_proto)
		*l3_proto = state.l3_proto;

	return err;
}

static int lan966x_tc_flower_action_check(struct vcap_control *vctrl,
					  struct flow_cls_offload *fco,
					  struct vcap_admin *admin)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(fco);
	struct flow_action_entry *actent, *last_actent = NULL;
	struct flow_action *act = &rule->action;
	u64 action_mask = 0;
	int idx;

	if (!flow_action_has_entries(act)) {
		NL_SET_ERR_MSG_MOD(fco->common.extack, "No actions");
		return -EINVAL;
	}

	if (!flow_action_basic_hw_stats_check(act, fco->common.extack))
		return -EOPNOTSUPP;

	flow_action_for_each(idx, actent, act) {
		if (action_mask & BIT(actent->id)) {
			NL_SET_ERR_MSG_MOD(fco->common.extack,
					   "More actions of the same type");
			return -EINVAL;
		}
		action_mask |= BIT(actent->id);
		last_actent = actent; /* Save last action for later check */
	}

	/* Check that last action is a goto */
	if (last_actent->id != FLOW_ACTION_GOTO) {
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "Last action must be 'goto'");
		return -EINVAL;
	}

	/* Check if the goto chain is in the next lookup */
	if (!vcap_is_next_lookup(vctrl, fco->common.chain_index,
				 last_actent->chain_index)) {
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "Invalid goto chain");
		return -EINVAL;
	}

	/* Catch unsupported combinations of actions */
	if (action_mask & BIT(FLOW_ACTION_TRAP) &&
	    action_mask & BIT(FLOW_ACTION_ACCEPT)) {
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "Cannot combine pass and trap action");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int lan966x_tc_flower_add(struct lan966x_port *port,
				 struct flow_cls_offload *f,
				 struct vcap_admin *admin)
{
	struct flow_action_entry *act;
	u16 l3_proto = ETH_P_ALL;
	struct flow_rule *frule;
	struct vcap_rule *vrule;
	int err, idx;

	err = lan966x_tc_flower_action_check(port->lan966x->vcap_ctrl, f,
					     admin);
	if (err)
		return err;

	vrule = vcap_alloc_rule(port->lan966x->vcap_ctrl, port->dev,
				f->common.chain_index, VCAP_USER_TC,
				f->common.prio, 0);
	if (IS_ERR(vrule))
		return PTR_ERR(vrule);

	vrule->cookie = f->cookie;
	err = lan966x_tc_flower_use_dissectors(f, admin, vrule, &l3_proto);
	if (err)
		goto out;

	frule = flow_cls_offload_flow_rule(f);

	flow_action_for_each(idx, act, &frule->action) {
		switch (act->id) {
		case FLOW_ACTION_TRAP:
			err = vcap_rule_add_action_bit(vrule,
						       VCAP_AF_CPU_COPY_ENA,
						       VCAP_BIT_1);
			err |= vcap_rule_add_action_u32(vrule,
							VCAP_AF_CPU_QUEUE_NUM,
							0);
			err |= vcap_rule_add_action_u32(vrule, VCAP_AF_MASK_MODE,
							LAN966X_PMM_REPLACE);
			err |= vcap_set_rule_set_actionset(vrule,
							   VCAP_AFS_BASE_TYPE);
			if (err)
				goto out;

			break;
		case FLOW_ACTION_GOTO:
			break;
		default:
			NL_SET_ERR_MSG_MOD(f->common.extack,
					   "Unsupported TC action");
			err = -EOPNOTSUPP;
			goto out;
		}
	}

	err = vcap_val_rule(vrule, l3_proto);
	if (err) {
		vcap_set_tc_exterr(f, vrule);
		goto out;
	}

	err = vcap_add_rule(vrule);
	if (err)
		NL_SET_ERR_MSG_MOD(f->common.extack,
				   "Could not add the filter");
out:
	vcap_free_rule(vrule);
	return err;
}

static int lan966x_tc_flower_del(struct lan966x_port *port,
				 struct flow_cls_offload *f,
				 struct vcap_admin *admin)
{
	struct vcap_control *vctrl;
	int err = -ENOENT, rule_id;

	vctrl = port->lan966x->vcap_ctrl;
	while (true) {
		rule_id = vcap_lookup_rule_by_cookie(vctrl, f->cookie);
		if (rule_id <= 0)
			break;

		err = vcap_del_rule(vctrl, port->dev, rule_id);
		if (err) {
			NL_SET_ERR_MSG_MOD(f->common.extack,
					   "Cannot delete rule");
			break;
		}
	}

	return err;
}

int lan966x_tc_flower(struct lan966x_port *port,
		      struct flow_cls_offload *f)
{
	struct vcap_admin *admin;

	admin = vcap_find_admin(port->lan966x->vcap_ctrl,
				f->common.chain_index);
	if (!admin) {
		NL_SET_ERR_MSG_MOD(f->common.extack, "Invalid chain");
		return -EINVAL;
	}

	switch (f->command) {
	case FLOW_CLS_REPLACE:
		return lan966x_tc_flower_add(port, f, admin);
	case FLOW_CLS_DESTROY:
		return lan966x_tc_flower_del(port, f, admin);
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}
