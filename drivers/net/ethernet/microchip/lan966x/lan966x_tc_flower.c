// SPDX-License-Identifier: GPL-2.0+

#include "lan966x_main.h"
#include "vcap_api.h"
#include "vcap_api_client.h"
#include "vcap_tc.h"

#define LAN966X_FORCE_UNTAGED	3

static bool lan966x_tc_is_known_etype(struct vcap_tc_flower_parse_usage *st,
				      u16 etype)
{
	switch (st->admin->vtype) {
	case VCAP_TYPE_IS1:
		switch (etype) {
		case ETH_P_ALL:
		case ETH_P_ARP:
		case ETH_P_IP:
		case ETH_P_IPV6:
			return true;
		}
		break;
	case VCAP_TYPE_IS2:
		switch (etype) {
		case ETH_P_ALL:
		case ETH_P_ARP:
		case ETH_P_IP:
		case ETH_P_IPV6:
		case ETH_P_SNAP:
		case ETH_P_802_2:
			return true;
		}
		break;
	case VCAP_TYPE_ES0:
		return true;
	default:
		NL_SET_ERR_MSG_MOD(st->fco->common.extack,
				   "VCAP type not supported");
		return false;
	}

	return false;
}

static int
lan966x_tc_flower_handler_control_usage(struct vcap_tc_flower_parse_usage *st)
{
	struct flow_match_control match;
	int err = 0;

	flow_rule_match_control(st->frule, &match);
	if (match.mask->flags & FLOW_DIS_IS_FRAGMENT) {
		if (match.key->flags & FLOW_DIS_IS_FRAGMENT)
			err = vcap_rule_add_key_bit(st->vrule,
						    VCAP_KF_L3_FRAGMENT,
						    VCAP_BIT_1);
		else
			err = vcap_rule_add_key_bit(st->vrule,
						    VCAP_KF_L3_FRAGMENT,
						    VCAP_BIT_0);
		if (err)
			goto out;
	}

	if (match.mask->flags & FLOW_DIS_FIRST_FRAG) {
		if (match.key->flags & FLOW_DIS_FIRST_FRAG)
			err = vcap_rule_add_key_bit(st->vrule,
						    VCAP_KF_L3_FRAG_OFS_GT0,
						    VCAP_BIT_0);
		else
			err = vcap_rule_add_key_bit(st->vrule,
						    VCAP_KF_L3_FRAG_OFS_GT0,
						    VCAP_BIT_1);
		if (err)
			goto out;
	}

	st->used_keys |= BIT(FLOW_DISSECTOR_KEY_CONTROL);

	return err;

out:
	NL_SET_ERR_MSG_MOD(st->fco->common.extack, "ip_frag parse error");
	return err;
}

static int
lan966x_tc_flower_handler_basic_usage(struct vcap_tc_flower_parse_usage *st)
{
	struct flow_match_basic match;
	int err = 0;

	flow_rule_match_basic(st->frule, &match);
	if (match.mask->n_proto) {
		st->l3_proto = be16_to_cpu(match.key->n_proto);
		if (!lan966x_tc_is_known_etype(st, st->l3_proto)) {
			err = vcap_rule_add_key_u32(st->vrule, VCAP_KF_ETYPE,
						    st->l3_proto, ~0);
			if (err)
				goto out;
		} else if (st->l3_proto == ETH_P_IP) {
			err = vcap_rule_add_key_bit(st->vrule, VCAP_KF_IP4_IS,
						    VCAP_BIT_1);
			if (err)
				goto out;
		} else if (st->l3_proto == ETH_P_IPV6 &&
			   st->admin->vtype == VCAP_TYPE_IS1) {
			/* Don't set any keys in this case */
		} else if (st->l3_proto == ETH_P_SNAP &&
			   st->admin->vtype == VCAP_TYPE_IS1) {
			err = vcap_rule_add_key_bit(st->vrule,
						    VCAP_KF_ETYPE_LEN_IS,
						    VCAP_BIT_0);
			if (err)
				goto out;

			err = vcap_rule_add_key_bit(st->vrule,
						    VCAP_KF_IP_SNAP_IS,
						    VCAP_BIT_1);
			if (err)
				goto out;
		} else if (st->admin->vtype == VCAP_TYPE_IS1) {
			err = vcap_rule_add_key_bit(st->vrule,
						    VCAP_KF_ETYPE_LEN_IS,
						    VCAP_BIT_1);
			if (err)
				goto out;

			err = vcap_rule_add_key_u32(st->vrule, VCAP_KF_ETYPE,
						    st->l3_proto, ~0);
			if (err)
				goto out;
		}
	}
	if (match.mask->ip_proto) {
		st->l4_proto = match.key->ip_proto;

		if (st->l4_proto == IPPROTO_TCP) {
			if (st->admin->vtype == VCAP_TYPE_IS1) {
				err = vcap_rule_add_key_bit(st->vrule,
							    VCAP_KF_TCP_UDP_IS,
							    VCAP_BIT_1);
				if (err)
					goto out;
			}

			err = vcap_rule_add_key_bit(st->vrule,
						    VCAP_KF_TCP_IS,
						    VCAP_BIT_1);
			if (err)
				goto out;
		} else if (st->l4_proto == IPPROTO_UDP) {
			if (st->admin->vtype == VCAP_TYPE_IS1) {
				err = vcap_rule_add_key_bit(st->vrule,
							    VCAP_KF_TCP_UDP_IS,
							    VCAP_BIT_1);
				if (err)
					goto out;
			}

			err = vcap_rule_add_key_bit(st->vrule,
						    VCAP_KF_TCP_IS,
						    VCAP_BIT_0);
			if (err)
				goto out;
		} else {
			err = vcap_rule_add_key_u32(st->vrule,
						    VCAP_KF_L3_IP_PROTO,
						    st->l4_proto, ~0);
			if (err)
				goto out;
		}
	}

	st->used_keys |= BIT(FLOW_DISSECTOR_KEY_BASIC);
	return err;
out:
	NL_SET_ERR_MSG_MOD(st->fco->common.extack, "ip_proto parse error");
	return err;
}

static int
lan966x_tc_flower_handler_cvlan_usage(struct vcap_tc_flower_parse_usage *st)
{
	if (st->admin->vtype != VCAP_TYPE_IS1) {
		NL_SET_ERR_MSG_MOD(st->fco->common.extack,
				   "cvlan not supported in this VCAP");
		return -EINVAL;
	}

	return vcap_tc_flower_handler_cvlan_usage(st);
}

static int
lan966x_tc_flower_handler_vlan_usage(struct vcap_tc_flower_parse_usage *st)
{
	enum vcap_key_field vid_key = VCAP_KF_8021Q_VID_CLS;
	enum vcap_key_field pcp_key = VCAP_KF_8021Q_PCP_CLS;

	if (st->admin->vtype == VCAP_TYPE_IS1) {
		vid_key = VCAP_KF_8021Q_VID0;
		pcp_key = VCAP_KF_8021Q_PCP0;
	}

	return vcap_tc_flower_handler_vlan_usage(st, vid_key, pcp_key);
}

static int
(*lan966x_tc_flower_handlers_usage[])(struct vcap_tc_flower_parse_usage *st) = {
	[FLOW_DISSECTOR_KEY_ETH_ADDRS] = vcap_tc_flower_handler_ethaddr_usage,
	[FLOW_DISSECTOR_KEY_IPV4_ADDRS] = vcap_tc_flower_handler_ipv4_usage,
	[FLOW_DISSECTOR_KEY_IPV6_ADDRS] = vcap_tc_flower_handler_ipv6_usage,
	[FLOW_DISSECTOR_KEY_CONTROL] = lan966x_tc_flower_handler_control_usage,
	[FLOW_DISSECTOR_KEY_PORTS] = vcap_tc_flower_handler_portnum_usage,
	[FLOW_DISSECTOR_KEY_BASIC] = lan966x_tc_flower_handler_basic_usage,
	[FLOW_DISSECTOR_KEY_CVLAN] = lan966x_tc_flower_handler_cvlan_usage,
	[FLOW_DISSECTOR_KEY_VLAN] = lan966x_tc_flower_handler_vlan_usage,
	[FLOW_DISSECTOR_KEY_TCP] = vcap_tc_flower_handler_tcp_usage,
	[FLOW_DISSECTOR_KEY_ARP] = vcap_tc_flower_handler_arp_usage,
	[FLOW_DISSECTOR_KEY_IP] = vcap_tc_flower_handler_ip_usage,
};

static int lan966x_tc_flower_use_dissectors(struct flow_cls_offload *f,
					    struct vcap_admin *admin,
					    struct vcap_rule *vrule,
					    u16 *l3_proto)
{
	struct vcap_tc_flower_parse_usage state = {
		.fco = f,
		.vrule = vrule,
		.l3_proto = ETH_P_ALL,
		.admin = admin,
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
					  struct net_device *dev,
					  struct flow_cls_offload *fco,
					  bool ingress)
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

	/* Check that last action is a goto
	 * The last chain/lookup does not need to have goto action
	 */
	if (last_actent->id == FLOW_ACTION_GOTO) {
		/* Check if the destination chain is in one of the VCAPs */
		if (!vcap_is_next_lookup(vctrl, fco->common.chain_index,
					 last_actent->chain_index)) {
			NL_SET_ERR_MSG_MOD(fco->common.extack,
					   "Invalid goto chain");
			return -EINVAL;
		}
	} else if (!vcap_is_last_chain(vctrl, fco->common.chain_index,
				       ingress)) {
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "Last action must be 'goto'");
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

/* Add the actionset that is the default for the VCAP type */
static int lan966x_tc_set_actionset(struct vcap_admin *admin,
				    struct vcap_rule *vrule)
{
	enum vcap_actionfield_set aset;
	int err = 0;

	switch (admin->vtype) {
	case VCAP_TYPE_IS1:
		aset = VCAP_AFS_S1;
		break;
	case VCAP_TYPE_IS2:
		aset = VCAP_AFS_BASE_TYPE;
		break;
	case VCAP_TYPE_ES0:
		aset = VCAP_AFS_VID;
		break;
	default:
		return -EINVAL;
	}

	/* Do not overwrite any current actionset */
	if (vrule->actionset == VCAP_AFS_NO_VALUE)
		err = vcap_set_rule_set_actionset(vrule, aset);

	return err;
}

static int lan966x_tc_add_rule_link_target(struct vcap_admin *admin,
					   struct vcap_rule *vrule,
					   int target_cid)
{
	int link_val = target_cid % VCAP_CID_LOOKUP_SIZE;
	int err;

	if (!link_val)
		return 0;

	switch (admin->vtype) {
	case VCAP_TYPE_IS1:
		/* Choose IS1 specific NXT_IDX key (for chaining rules from IS1) */
		err = vcap_rule_add_key_u32(vrule, VCAP_KF_LOOKUP_GEN_IDX_SEL,
					    1, ~0);
		if (err)
			return err;

		return vcap_rule_add_key_u32(vrule, VCAP_KF_LOOKUP_GEN_IDX,
					     link_val, ~0);
	case VCAP_TYPE_IS2:
		/* Add IS2 specific PAG key (for chaining rules from IS1) */
		return vcap_rule_add_key_u32(vrule, VCAP_KF_LOOKUP_PAG,
					     link_val, ~0);
	case VCAP_TYPE_ES0:
		/* Add ES0 specific ISDX key (for chaining rules from IS1) */
		return vcap_rule_add_key_u32(vrule, VCAP_KF_ISDX_CLS,
					     link_val, ~0);
	default:
		break;
	}
	return 0;
}

static int lan966x_tc_add_rule_link(struct vcap_control *vctrl,
				    struct vcap_admin *admin,
				    struct vcap_rule *vrule,
				    struct flow_cls_offload *f,
				    int to_cid)
{
	struct vcap_admin *to_admin = vcap_find_admin(vctrl, to_cid);
	int diff, err = 0;

	if (!to_admin) {
		NL_SET_ERR_MSG_MOD(f->common.extack,
				   "Unknown destination chain");
		return -EINVAL;
	}

	diff = vcap_chain_offset(vctrl, f->common.chain_index, to_cid);
	if (!diff)
		return 0;

	/* Between IS1 and IS2 the PAG value is used */
	if (admin->vtype == VCAP_TYPE_IS1 && to_admin->vtype == VCAP_TYPE_IS2) {
		/* This works for IS1->IS2 */
		err = vcap_rule_add_action_u32(vrule, VCAP_AF_PAG_VAL, diff);
		if (err)
			return err;

		err = vcap_rule_add_action_u32(vrule, VCAP_AF_PAG_OVERRIDE_MASK,
					       0xff);
		if (err)
			return err;
	} else if (admin->vtype == VCAP_TYPE_IS1 &&
		   to_admin->vtype == VCAP_TYPE_ES0) {
		/* This works for IS1->ES0 */
		err = vcap_rule_add_action_u32(vrule, VCAP_AF_ISDX_ADD_VAL,
					       diff);
		if (err)
			return err;

		err = vcap_rule_add_action_bit(vrule, VCAP_AF_ISDX_REPLACE_ENA,
					       VCAP_BIT_1);
		if (err)
			return err;
	} else {
		NL_SET_ERR_MSG_MOD(f->common.extack,
				   "Unsupported chain destination");
		return -EOPNOTSUPP;
	}

	return err;
}

static int lan966x_tc_add_rule_counter(struct vcap_admin *admin,
				       struct vcap_rule *vrule)
{
	int err = 0;

	switch (admin->vtype) {
	case VCAP_TYPE_ES0:
		err = vcap_rule_mod_action_u32(vrule, VCAP_AF_ESDX,
					       vrule->id);
		break;
	default:
		break;
	}

	return err;
}

static int lan966x_tc_flower_add(struct lan966x_port *port,
				 struct flow_cls_offload *f,
				 struct vcap_admin *admin,
				 bool ingress)
{
	struct flow_action_entry *act;
	u16 l3_proto = ETH_P_ALL;
	struct flow_rule *frule;
	struct vcap_rule *vrule;
	int err, idx;

	err = lan966x_tc_flower_action_check(port->lan966x->vcap_ctrl,
					     port->dev, f, ingress);
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

	err = lan966x_tc_add_rule_link_target(admin, vrule,
					      f->common.chain_index);
	if (err)
		goto out;

	frule = flow_cls_offload_flow_rule(f);

	flow_action_for_each(idx, act, &frule->action) {
		switch (act->id) {
		case FLOW_ACTION_TRAP:
			if (admin->vtype != VCAP_TYPE_IS2) {
				NL_SET_ERR_MSG_MOD(f->common.extack,
						   "Trap action not supported in this VCAP");
				err = -EOPNOTSUPP;
				goto out;
			}

			err = vcap_rule_add_action_bit(vrule,
						       VCAP_AF_CPU_COPY_ENA,
						       VCAP_BIT_1);
			err |= vcap_rule_add_action_u32(vrule,
							VCAP_AF_CPU_QUEUE_NUM,
							0);
			err |= vcap_rule_add_action_u32(vrule, VCAP_AF_MASK_MODE,
							LAN966X_PMM_REPLACE);
			if (err)
				goto out;

			break;
		case FLOW_ACTION_GOTO:
			err = lan966x_tc_set_actionset(admin, vrule);
			if (err)
				goto out;

			err = lan966x_tc_add_rule_link(port->lan966x->vcap_ctrl,
						       admin, vrule,
						       f, act->chain_index);
			if (err)
				goto out;

			break;
		case FLOW_ACTION_VLAN_POP:
			if (admin->vtype != VCAP_TYPE_ES0) {
				NL_SET_ERR_MSG_MOD(f->common.extack,
						   "Cannot use vlan pop on non es0");
				err = -EOPNOTSUPP;
				goto out;
			}

			/* Force untag */
			err = vcap_rule_add_action_u32(vrule, VCAP_AF_PUSH_OUTER_TAG,
						       LAN966X_FORCE_UNTAGED);
			if (err)
				goto out;

			break;
		default:
			NL_SET_ERR_MSG_MOD(f->common.extack,
					   "Unsupported TC action");
			err = -EOPNOTSUPP;
			goto out;
		}
	}

	err = lan966x_tc_add_rule_counter(admin, vrule);
	if (err) {
		vcap_set_tc_exterr(f, vrule);
		goto out;
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

static int lan966x_tc_flower_stats(struct lan966x_port *port,
				   struct flow_cls_offload *f,
				   struct vcap_admin *admin)
{
	struct vcap_counter count = {};
	int err;

	err = vcap_get_rule_count_by_cookie(port->lan966x->vcap_ctrl,
					    &count, f->cookie);
	if (err)
		return err;

	flow_stats_update(&f->stats, 0x0, count.value, 0, 0,
			  FLOW_ACTION_HW_STATS_IMMEDIATE);

	return err;
}

int lan966x_tc_flower(struct lan966x_port *port,
		      struct flow_cls_offload *f,
		      bool ingress)
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
		return lan966x_tc_flower_add(port, f, admin, ingress);
	case FLOW_CLS_DESTROY:
		return lan966x_tc_flower_del(port, f, admin);
	case FLOW_CLS_STATS:
		return lan966x_tc_flower_stats(port, f, admin);
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}
