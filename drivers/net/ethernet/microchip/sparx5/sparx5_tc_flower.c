// SPDX-License-Identifier: GPL-2.0+
/* Microchip VCAP API
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 */

#include <net/tc_act/tc_gate.h>
#include <net/tcp.h>

#include "sparx5_tc.h"
#include "vcap_api.h"
#include "vcap_api_client.h"
#include "vcap_tc.h"
#include "sparx5_main.h"
#include "sparx5_vcap_impl.h"

#define SPX5_MAX_RULE_SIZE 13 /* allows X1, X2, X4, X6 and X12 rules */

/* Collect keysets and type ids for multiple rules per size */
struct sparx5_wildcard_rule {
	bool selected;
	u8 value;
	u8 mask;
	enum vcap_keyfield_set keyset;
};

struct sparx5_multiple_rules {
	struct sparx5_wildcard_rule rule[SPX5_MAX_RULE_SIZE];
};

struct sparx5_tc_flower_template {
	struct list_head list; /* for insertion in the list of templates */
	int cid; /* chain id */
	enum vcap_keyfield_set orig; /* keyset used before the template */
	enum vcap_keyfield_set keyset; /* new keyset used by template */
	u16 l3_proto; /* protocol specified in the template */
};

static int
sparx5_tc_flower_es0_tpid(struct vcap_tc_flower_parse_usage *st)
{
	int err = 0;

	switch (st->tpid) {
	case ETH_P_8021Q:
		err = vcap_rule_add_key_u32(st->vrule,
					    VCAP_KF_8021Q_TPID,
					    SPX5_TPID_SEL_8100, ~0);
		break;
	case ETH_P_8021AD:
		err = vcap_rule_add_key_u32(st->vrule,
					    VCAP_KF_8021Q_TPID,
					    SPX5_TPID_SEL_88A8, ~0);
		break;
	default:
		NL_SET_ERR_MSG_MOD(st->fco->common.extack,
				   "Invalid vlan proto");
		err = -EINVAL;
		break;
	}
	return err;
}

static int
sparx5_tc_flower_handler_basic_usage(struct vcap_tc_flower_parse_usage *st)
{
	struct flow_match_basic mt;
	int err = 0;

	flow_rule_match_basic(st->frule, &mt);

	if (mt.mask->n_proto) {
		st->l3_proto = be16_to_cpu(mt.key->n_proto);
		if (!sparx5_vcap_is_known_etype(st->admin, st->l3_proto)) {
			err = vcap_rule_add_key_u32(st->vrule, VCAP_KF_ETYPE,
						    st->l3_proto, ~0);
			if (err)
				goto out;
		} else if (st->l3_proto == ETH_P_IP) {
			err = vcap_rule_add_key_bit(st->vrule, VCAP_KF_IP4_IS,
						    VCAP_BIT_1);
			if (err)
				goto out;
		} else if (st->l3_proto == ETH_P_IPV6) {
			err = vcap_rule_add_key_bit(st->vrule, VCAP_KF_IP4_IS,
						    VCAP_BIT_0);
			if (err)
				goto out;
			if (st->admin->vtype == VCAP_TYPE_IS0) {
				err = vcap_rule_add_key_bit(st->vrule,
							    VCAP_KF_IP_SNAP_IS,
							    VCAP_BIT_1);
				if (err)
					goto out;
			}
		}
	}

	if (mt.mask->ip_proto) {
		st->l4_proto = mt.key->ip_proto;
		if (st->l4_proto == IPPROTO_TCP) {
			err = vcap_rule_add_key_bit(st->vrule,
						    VCAP_KF_TCP_IS,
						    VCAP_BIT_1);
			if (err)
				goto out;
		} else if (st->l4_proto == IPPROTO_UDP) {
			err = vcap_rule_add_key_bit(st->vrule,
						    VCAP_KF_TCP_IS,
						    VCAP_BIT_0);
			if (err)
				goto out;
			if (st->admin->vtype == VCAP_TYPE_IS0) {
				err = vcap_rule_add_key_bit(st->vrule,
							    VCAP_KF_TCP_UDP_IS,
							    VCAP_BIT_1);
				if (err)
					goto out;
			}
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
sparx5_tc_flower_handler_control_usage(struct vcap_tc_flower_parse_usage *st)
{
	struct flow_match_control mt;
	u32 value, mask;
	int err = 0;

	flow_rule_match_control(st->frule, &mt);

	if (mt.mask->flags) {
		if (mt.mask->flags & FLOW_DIS_FIRST_FRAG) {
			if (mt.key->flags & FLOW_DIS_FIRST_FRAG) {
				value = 1; /* initial fragment */
				mask = 0x3;
			} else {
				if (mt.mask->flags & FLOW_DIS_IS_FRAGMENT) {
					value = 3; /* follow up fragment */
					mask = 0x3;
				} else {
					value = 0; /* no fragment */
					mask = 0x3;
				}
			}
		} else {
			if (mt.mask->flags & FLOW_DIS_IS_FRAGMENT) {
				value = 3; /* follow up fragment */
				mask = 0x3;
			} else {
				value = 0; /* no fragment */
				mask = 0x3;
			}
		}

		err = vcap_rule_add_key_u32(st->vrule,
					    VCAP_KF_L3_FRAGMENT_TYPE,
					    value, mask);
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
sparx5_tc_flower_handler_cvlan_usage(struct vcap_tc_flower_parse_usage *st)
{
	if (st->admin->vtype != VCAP_TYPE_IS0) {
		NL_SET_ERR_MSG_MOD(st->fco->common.extack,
				   "cvlan not supported in this VCAP");
		return -EINVAL;
	}

	return vcap_tc_flower_handler_cvlan_usage(st);
}

static int
sparx5_tc_flower_handler_vlan_usage(struct vcap_tc_flower_parse_usage *st)
{
	enum vcap_key_field vid_key = VCAP_KF_8021Q_VID_CLS;
	enum vcap_key_field pcp_key = VCAP_KF_8021Q_PCP_CLS;
	int err;

	if (st->admin->vtype == VCAP_TYPE_IS0) {
		vid_key = VCAP_KF_8021Q_VID0;
		pcp_key = VCAP_KF_8021Q_PCP0;
	}

	err = vcap_tc_flower_handler_vlan_usage(st, vid_key, pcp_key);
	if (err)
		return err;

	if (st->admin->vtype == VCAP_TYPE_ES0 && st->tpid)
		err = sparx5_tc_flower_es0_tpid(st);

	return err;
}

static int (*sparx5_tc_flower_usage_handlers[])(struct vcap_tc_flower_parse_usage *st) = {
	[FLOW_DISSECTOR_KEY_ETH_ADDRS] = vcap_tc_flower_handler_ethaddr_usage,
	[FLOW_DISSECTOR_KEY_IPV4_ADDRS] = vcap_tc_flower_handler_ipv4_usage,
	[FLOW_DISSECTOR_KEY_IPV6_ADDRS] = vcap_tc_flower_handler_ipv6_usage,
	[FLOW_DISSECTOR_KEY_CONTROL] = sparx5_tc_flower_handler_control_usage,
	[FLOW_DISSECTOR_KEY_PORTS] = vcap_tc_flower_handler_portnum_usage,
	[FLOW_DISSECTOR_KEY_BASIC] = sparx5_tc_flower_handler_basic_usage,
	[FLOW_DISSECTOR_KEY_CVLAN] = sparx5_tc_flower_handler_cvlan_usage,
	[FLOW_DISSECTOR_KEY_VLAN] = sparx5_tc_flower_handler_vlan_usage,
	[FLOW_DISSECTOR_KEY_TCP] = vcap_tc_flower_handler_tcp_usage,
	[FLOW_DISSECTOR_KEY_ARP] = vcap_tc_flower_handler_arp_usage,
	[FLOW_DISSECTOR_KEY_IP] = vcap_tc_flower_handler_ip_usage,
};

static int sparx5_tc_use_dissectors(struct vcap_tc_flower_parse_usage *st,
				    struct vcap_admin *admin,
				    struct vcap_rule *vrule)
{
	int idx, err = 0;

	for (idx = 0; idx < ARRAY_SIZE(sparx5_tc_flower_usage_handlers); ++idx) {
		if (!flow_rule_match_key(st->frule, idx))
			continue;
		if (!sparx5_tc_flower_usage_handlers[idx])
			continue;
		err = sparx5_tc_flower_usage_handlers[idx](st);
		if (err)
			return err;
	}

	if (st->frule->match.dissector->used_keys ^ st->used_keys) {
		NL_SET_ERR_MSG_MOD(st->fco->common.extack,
				   "Unsupported match item");
		return -ENOENT;
	}

	return err;
}

static int sparx5_tc_flower_action_check(struct vcap_control *vctrl,
					 struct net_device *ndev,
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

	/* Check if last action is a goto
	 * The last chain/lookup does not need to have a goto action
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

	if (action_mask & BIT(FLOW_ACTION_VLAN_PUSH) &&
	    action_mask & BIT(FLOW_ACTION_VLAN_POP)) {
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "Cannot combine vlan push and pop action");
		return -EOPNOTSUPP;
	}

	if (action_mask & BIT(FLOW_ACTION_VLAN_PUSH) &&
	    action_mask & BIT(FLOW_ACTION_VLAN_MANGLE)) {
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "Cannot combine vlan push and modify action");
		return -EOPNOTSUPP;
	}

	if (action_mask & BIT(FLOW_ACTION_VLAN_POP) &&
	    action_mask & BIT(FLOW_ACTION_VLAN_MANGLE)) {
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "Cannot combine vlan pop and modify action");
		return -EOPNOTSUPP;
	}

	return 0;
}

/* Add a rule counter action */
static int sparx5_tc_add_rule_counter(struct vcap_admin *admin,
				      struct vcap_rule *vrule)
{
	int err;

	switch (admin->vtype) {
	case VCAP_TYPE_IS0:
		break;
	case VCAP_TYPE_ES0:
		err = vcap_rule_mod_action_u32(vrule, VCAP_AF_ESDX,
					       vrule->id);
		if (err)
			return err;
		vcap_rule_set_counter_id(vrule, vrule->id);
		break;
	case VCAP_TYPE_IS2:
	case VCAP_TYPE_ES2:
		err = vcap_rule_mod_action_u32(vrule, VCAP_AF_CNT_ID,
					       vrule->id);
		if (err)
			return err;
		vcap_rule_set_counter_id(vrule, vrule->id);
		break;
	default:
		pr_err("%s:%d: vcap type: %d not supported\n",
		       __func__, __LINE__, admin->vtype);
		break;
	}
	return 0;
}

/* Collect all port keysets and apply the first of them, possibly wildcarded */
static int sparx5_tc_select_protocol_keyset(struct net_device *ndev,
					    struct vcap_rule *vrule,
					    struct vcap_admin *admin,
					    u16 l3_proto,
					    struct sparx5_multiple_rules *multi)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct vcap_keyset_list portkeysetlist = {};
	enum vcap_keyfield_set portkeysets[10] = {};
	struct vcap_keyset_list matches = {};
	enum vcap_keyfield_set keysets[10];
	int idx, jdx, err = 0, count = 0;
	struct sparx5_wildcard_rule *mru;
	const struct vcap_set *kinfo;
	struct vcap_control *vctrl;

	vctrl = port->sparx5->vcap_ctrl;

	/* Find the keysets that the rule can use */
	matches.keysets = keysets;
	matches.max = ARRAY_SIZE(keysets);
	if (!vcap_rule_find_keysets(vrule, &matches))
		return -EINVAL;

	/* Find the keysets that the port configuration supports */
	portkeysetlist.max = ARRAY_SIZE(portkeysets);
	portkeysetlist.keysets = portkeysets;
	err = sparx5_vcap_get_port_keyset(ndev,
					  admin, vrule->vcap_chain_id,
					  l3_proto,
					  &portkeysetlist);
	if (err)
		return err;

	/* Find the intersection of the two sets of keyset */
	for (idx = 0; idx < portkeysetlist.cnt; ++idx) {
		kinfo = vcap_keyfieldset(vctrl, admin->vtype,
					 portkeysetlist.keysets[idx]);
		if (!kinfo)
			continue;

		/* Find a port keyset that matches the required keys
		 * If there are multiple keysets then compose a type id mask
		 */
		for (jdx = 0; jdx < matches.cnt; ++jdx) {
			if (portkeysetlist.keysets[idx] != matches.keysets[jdx])
				continue;

			mru = &multi->rule[kinfo->sw_per_item];
			if (!mru->selected) {
				mru->selected = true;
				mru->keyset = portkeysetlist.keysets[idx];
				mru->value = kinfo->type_id;
			}
			mru->value &= kinfo->type_id;
			mru->mask |= kinfo->type_id;
			++count;
		}
	}
	if (count == 0)
		return -EPROTO;

	if (l3_proto == ETH_P_ALL && count < portkeysetlist.cnt)
		return -ENOENT;

	for (idx = 0; idx < SPX5_MAX_RULE_SIZE; ++idx) {
		mru = &multi->rule[idx];
		if (!mru->selected)
			continue;

		/* Align the mask to the combined value */
		mru->mask ^= mru->value;
	}

	/* Set the chosen keyset on the rule and set a wildcarded type if there
	 * are more than one keyset
	 */
	for (idx = 0; idx < SPX5_MAX_RULE_SIZE; ++idx) {
		mru = &multi->rule[idx];
		if (!mru->selected)
			continue;

		vcap_set_rule_set_keyset(vrule, mru->keyset);
		if (count > 1)
			/* Some keysets do not have a type field */
			vcap_rule_mod_key_u32(vrule, VCAP_KF_TYPE,
					      mru->value,
					      ~mru->mask);
		mru->selected = false; /* mark as done */
		break; /* Stop here and add more rules later */
	}
	return err;
}

static int sparx5_tc_add_rule_copy(struct vcap_control *vctrl,
				   struct flow_cls_offload *fco,
				   struct vcap_rule *erule,
				   struct vcap_admin *admin,
				   struct sparx5_wildcard_rule *rule)
{
	enum vcap_key_field keylist[] = {
		VCAP_KF_IF_IGR_PORT_MASK,
		VCAP_KF_IF_IGR_PORT_MASK_SEL,
		VCAP_KF_IF_IGR_PORT_MASK_RNG,
		VCAP_KF_LOOKUP_FIRST_IS,
		VCAP_KF_TYPE,
	};
	struct vcap_rule *vrule;
	int err;

	/* Add an extra rule with a special user and the new keyset */
	erule->user = VCAP_USER_TC_EXTRA;
	vrule = vcap_copy_rule(erule);
	if (IS_ERR(vrule))
		return PTR_ERR(vrule);

	/* Link the new rule to the existing rule with the cookie */
	vrule->cookie = erule->cookie;
	vcap_filter_rule_keys(vrule, keylist, ARRAY_SIZE(keylist), true);
	err = vcap_set_rule_set_keyset(vrule, rule->keyset);
	if (err) {
		pr_err("%s:%d: could not set keyset %s in rule: %u\n",
		       __func__, __LINE__,
		       vcap_keyset_name(vctrl, rule->keyset),
		       vrule->id);
		goto out;
	}

	/* Some keysets do not have a type field, so ignore return value */
	vcap_rule_mod_key_u32(vrule, VCAP_KF_TYPE, rule->value, ~rule->mask);

	err = vcap_set_rule_set_actionset(vrule, erule->actionset);
	if (err)
		goto out;

	err = sparx5_tc_add_rule_counter(admin, vrule);
	if (err)
		goto out;

	err = vcap_val_rule(vrule, ETH_P_ALL);
	if (err) {
		pr_err("%s:%d: could not validate rule: %u\n",
		       __func__, __LINE__, vrule->id);
		vcap_set_tc_exterr(fco, vrule);
		goto out;
	}
	err = vcap_add_rule(vrule);
	if (err) {
		pr_err("%s:%d: could not add rule: %u\n",
		       __func__, __LINE__, vrule->id);
		goto out;
	}
out:
	vcap_free_rule(vrule);
	return err;
}

static int sparx5_tc_add_remaining_rules(struct vcap_control *vctrl,
					 struct flow_cls_offload *fco,
					 struct vcap_rule *erule,
					 struct vcap_admin *admin,
					 struct sparx5_multiple_rules *multi)
{
	int idx, err = 0;

	for (idx = 0; idx < SPX5_MAX_RULE_SIZE; ++idx) {
		if (!multi->rule[idx].selected)
			continue;

		err = sparx5_tc_add_rule_copy(vctrl, fco, erule, admin,
					      &multi->rule[idx]);
		if (err)
			break;
	}
	return err;
}

/* Add the actionset that is the default for the VCAP type */
static int sparx5_tc_set_actionset(struct vcap_admin *admin,
				   struct vcap_rule *vrule)
{
	enum vcap_actionfield_set aset;
	int err = 0;

	switch (admin->vtype) {
	case VCAP_TYPE_IS0:
		aset = VCAP_AFS_CLASSIFICATION;
		break;
	case VCAP_TYPE_IS2:
		aset = VCAP_AFS_BASE_TYPE;
		break;
	case VCAP_TYPE_ES0:
		aset = VCAP_AFS_ES0;
		break;
	case VCAP_TYPE_ES2:
		aset = VCAP_AFS_BASE_TYPE;
		break;
	default:
		pr_err("%s:%d: %s\n", __func__, __LINE__, "Invalid VCAP type");
		return -EINVAL;
	}
	/* Do not overwrite any current actionset */
	if (vrule->actionset == VCAP_AFS_NO_VALUE)
		err = vcap_set_rule_set_actionset(vrule, aset);
	return err;
}

/* Add the VCAP key to match on for a rule target value */
static int sparx5_tc_add_rule_link_target(struct vcap_admin *admin,
					  struct vcap_rule *vrule,
					  int target_cid)
{
	int link_val = target_cid % VCAP_CID_LOOKUP_SIZE;
	int err;

	if (!link_val)
		return 0;

	switch (admin->vtype) {
	case VCAP_TYPE_IS0:
		/* Add NXT_IDX key for chaining rules between IS0 instances */
		err = vcap_rule_add_key_u32(vrule, VCAP_KF_LOOKUP_GEN_IDX_SEL,
					    1, /* enable */
					    ~0);
		if (err)
			return err;
		return vcap_rule_add_key_u32(vrule, VCAP_KF_LOOKUP_GEN_IDX,
					     link_val, /* target */
					     ~0);
	case VCAP_TYPE_IS2:
		/* Add PAG key for chaining rules from IS0 */
		return vcap_rule_add_key_u32(vrule, VCAP_KF_LOOKUP_PAG,
					     link_val, /* target */
					     ~0);
	case VCAP_TYPE_ES0:
	case VCAP_TYPE_ES2:
		/* Add ISDX key for chaining rules from IS0 */
		return vcap_rule_add_key_u32(vrule, VCAP_KF_ISDX_CLS, link_val,
					     ~0);
	default:
		break;
	}
	return 0;
}

/* Add the VCAP action that adds a target value to a rule */
static int sparx5_tc_add_rule_link(struct vcap_control *vctrl,
				   struct vcap_admin *admin,
				   struct vcap_rule *vrule,
				   int from_cid, int to_cid)
{
	struct vcap_admin *to_admin = vcap_find_admin(vctrl, to_cid);
	int diff, err = 0;

	if (!to_admin) {
		pr_err("%s:%d: unsupported chain direction: %d\n",
		       __func__, __LINE__, to_cid);
		return -EINVAL;
	}

	diff = vcap_chain_offset(vctrl, from_cid, to_cid);
	if (!diff)
		return 0;

	if (admin->vtype == VCAP_TYPE_IS0 &&
	    to_admin->vtype == VCAP_TYPE_IS0) {
		/* Between IS0 instances the G_IDX value is used */
		err = vcap_rule_add_action_u32(vrule, VCAP_AF_NXT_IDX, diff);
		if (err)
			goto out;
		err = vcap_rule_add_action_u32(vrule, VCAP_AF_NXT_IDX_CTRL,
					       1); /* Replace */
		if (err)
			goto out;
	} else if (admin->vtype == VCAP_TYPE_IS0 &&
		   to_admin->vtype == VCAP_TYPE_IS2) {
		/* Between IS0 and IS2 the PAG value is used */
		err = vcap_rule_add_action_u32(vrule, VCAP_AF_PAG_VAL, diff);
		if (err)
			goto out;
		err = vcap_rule_add_action_u32(vrule,
					       VCAP_AF_PAG_OVERRIDE_MASK,
					       0xff);
		if (err)
			goto out;
	} else if (admin->vtype == VCAP_TYPE_IS0 &&
		   (to_admin->vtype == VCAP_TYPE_ES0 ||
		    to_admin->vtype == VCAP_TYPE_ES2)) {
		/* Between IS0 and ES0/ES2 the ISDX value is used */
		err = vcap_rule_add_action_u32(vrule, VCAP_AF_ISDX_VAL,
					       diff);
		if (err)
			goto out;
		err = vcap_rule_add_action_bit(vrule,
					       VCAP_AF_ISDX_ADD_REPLACE_SEL,
					       VCAP_BIT_1);
		if (err)
			goto out;
	} else {
		pr_err("%s:%d: unsupported chain destination: %d\n",
		       __func__, __LINE__, to_cid);
		err = -EOPNOTSUPP;
	}
out:
	return err;
}

static int sparx5_tc_flower_parse_act_gate(struct sparx5_psfp_sg *sg,
					   struct flow_action_entry *act,
					   struct netlink_ext_ack *extack)
{
	int i;

	if (act->gate.prio < -1 || act->gate.prio > SPX5_PSFP_SG_MAX_IPV) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid gate priority");
		return -EINVAL;
	}

	if (act->gate.cycletime < SPX5_PSFP_SG_MIN_CYCLE_TIME_NS ||
	    act->gate.cycletime > SPX5_PSFP_SG_MAX_CYCLE_TIME_NS) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid gate cycletime");
		return -EINVAL;
	}

	if (act->gate.cycletimeext > SPX5_PSFP_SG_MAX_CYCLE_TIME_NS) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid gate cycletimeext");
		return -EINVAL;
	}

	if (act->gate.num_entries >= SPX5_PSFP_GCE_CNT) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid number of gate entries");
		return -EINVAL;
	}

	sg->gate_state = true;
	sg->ipv = act->gate.prio;
	sg->num_entries = act->gate.num_entries;
	sg->cycletime = act->gate.cycletime;
	sg->cycletimeext = act->gate.cycletimeext;

	for (i = 0; i < sg->num_entries; i++) {
		sg->gce[i].gate_state = !!act->gate.entries[i].gate_state;
		sg->gce[i].interval = act->gate.entries[i].interval;
		sg->gce[i].ipv = act->gate.entries[i].ipv;
		sg->gce[i].maxoctets = act->gate.entries[i].maxoctets;
	}

	return 0;
}

static int sparx5_tc_flower_parse_act_police(struct sparx5_policer *pol,
					     struct flow_action_entry *act,
					     struct netlink_ext_ack *extack)
{
	pol->type = SPX5_POL_SERVICE;
	pol->rate = div_u64(act->police.rate_bytes_ps, 1000) * 8;
	pol->burst = act->police.burst;
	pol->idx = act->hw_index;

	/* rate is now in kbit */
	if (pol->rate > DIV_ROUND_UP(SPX5_SDLB_GROUP_RATE_MAX, 1000)) {
		NL_SET_ERR_MSG_MOD(extack, "Maximum rate exceeded");
		return -EINVAL;
	}

	if (act->police.exceed.act_id != FLOW_ACTION_DROP) {
		NL_SET_ERR_MSG_MOD(extack, "Offload not supported when exceed action is not drop");
		return -EOPNOTSUPP;
	}

	if (act->police.notexceed.act_id != FLOW_ACTION_PIPE &&
	    act->police.notexceed.act_id != FLOW_ACTION_ACCEPT) {
		NL_SET_ERR_MSG_MOD(extack, "Offload not supported when conform action is not pipe or ok");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int sparx5_tc_flower_psfp_setup(struct sparx5 *sparx5,
				       struct vcap_rule *vrule, int sg_idx,
				       int pol_idx, struct sparx5_psfp_sg *sg,
				       struct sparx5_psfp_fm *fm,
				       struct sparx5_psfp_sf *sf)
{
	u32 psfp_sfid = 0, psfp_fmid = 0, psfp_sgid = 0;
	int ret;

	/* Must always have a stream gate - max sdu (filter option) is evaluated
	 * after frames have passed the gate, so in case of only a policer, we
	 * allocate a stream gate that is always open.
	 */
	if (sg_idx < 0) {
		sg_idx = sparx5_pool_idx_to_id(SPX5_PSFP_SG_OPEN);
		sg->ipv = 0; /* Disabled */
		sg->cycletime = SPX5_PSFP_SG_CYCLE_TIME_DEFAULT;
		sg->num_entries = 1;
		sg->gate_state = 1; /* Open */
		sg->gate_enabled = 1;
		sg->gce[0].gate_state = 1;
		sg->gce[0].interval = SPX5_PSFP_SG_CYCLE_TIME_DEFAULT;
		sg->gce[0].ipv = 0;
		sg->gce[0].maxoctets = 0; /* Disabled */
	}

	ret = sparx5_psfp_sg_add(sparx5, sg_idx, sg, &psfp_sgid);
	if (ret < 0)
		return ret;

	if (pol_idx >= 0) {
		/* Add new flow-meter */
		ret = sparx5_psfp_fm_add(sparx5, pol_idx, fm, &psfp_fmid);
		if (ret < 0)
			return ret;
	}

	/* Map stream filter to stream gate */
	sf->sgid = psfp_sgid;

	/* Add new stream-filter and map it to a steam gate */
	ret = sparx5_psfp_sf_add(sparx5, sf, &psfp_sfid);
	if (ret < 0)
		return ret;

	/* Streams are classified by ISDX - map ISDX 1:1 to sfid for now. */
	sparx5_isdx_conf_set(sparx5, psfp_sfid, psfp_sfid, psfp_fmid);

	ret = vcap_rule_add_action_bit(vrule, VCAP_AF_ISDX_ADD_REPLACE_SEL,
				       VCAP_BIT_1);
	if (ret)
		return ret;

	ret = vcap_rule_add_action_u32(vrule, VCAP_AF_ISDX_VAL, psfp_sfid);
	if (ret)
		return ret;

	return 0;
}

/* Handle the action trap for a VCAP rule */
static int sparx5_tc_action_trap(struct vcap_admin *admin,
				 struct vcap_rule *vrule,
				 struct flow_cls_offload *fco)
{
	int err = 0;

	switch (admin->vtype) {
	case VCAP_TYPE_IS2:
		err = vcap_rule_add_action_bit(vrule,
					       VCAP_AF_CPU_COPY_ENA,
					       VCAP_BIT_1);
		if (err)
			break;
		err = vcap_rule_add_action_u32(vrule,
					       VCAP_AF_CPU_QUEUE_NUM, 0);
		if (err)
			break;
		err = vcap_rule_add_action_u32(vrule,
					       VCAP_AF_MASK_MODE,
					       SPX5_PMM_REPLACE_ALL);
		break;
	case VCAP_TYPE_ES0:
		err = vcap_rule_add_action_u32(vrule,
					       VCAP_AF_FWD_SEL,
					       SPX5_FWSEL_REDIRECT_TO_LOOPBACK);
		break;
	case VCAP_TYPE_ES2:
		err = vcap_rule_add_action_bit(vrule,
					       VCAP_AF_CPU_COPY_ENA,
					       VCAP_BIT_1);
		if (err)
			break;
		err = vcap_rule_add_action_u32(vrule,
					       VCAP_AF_CPU_QUEUE_NUM, 0);
		break;
	default:
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "Trap action not supported in this VCAP");
		err = -EOPNOTSUPP;
		break;
	}
	return err;
}

static int sparx5_tc_action_vlan_pop(struct vcap_admin *admin,
				     struct vcap_rule *vrule,
				     struct flow_cls_offload *fco,
				     u16 tpid)
{
	int err = 0;

	switch (admin->vtype) {
	case VCAP_TYPE_ES0:
		break;
	default:
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "VLAN pop action not supported in this VCAP");
		return -EOPNOTSUPP;
	}

	switch (tpid) {
	case ETH_P_8021Q:
	case ETH_P_8021AD:
		err = vcap_rule_add_action_u32(vrule,
					       VCAP_AF_PUSH_OUTER_TAG,
					       SPX5_OTAG_UNTAG);
		break;
	default:
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "Invalid vlan proto");
		err = -EINVAL;
	}
	return err;
}

static int sparx5_tc_action_vlan_modify(struct vcap_admin *admin,
					struct vcap_rule *vrule,
					struct flow_cls_offload *fco,
					struct flow_action_entry *act,
					u16 tpid)
{
	int err = 0;

	switch (admin->vtype) {
	case VCAP_TYPE_ES0:
		err = vcap_rule_add_action_u32(vrule,
					       VCAP_AF_PUSH_OUTER_TAG,
					       SPX5_OTAG_TAG_A);
		if (err)
			return err;
		break;
	default:
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "VLAN modify action not supported in this VCAP");
		return -EOPNOTSUPP;
	}

	switch (tpid) {
	case ETH_P_8021Q:
		err = vcap_rule_add_action_u32(vrule,
					       VCAP_AF_TAG_A_TPID_SEL,
					       SPX5_TPID_A_8100);
		break;
	case ETH_P_8021AD:
		err = vcap_rule_add_action_u32(vrule,
					       VCAP_AF_TAG_A_TPID_SEL,
					       SPX5_TPID_A_88A8);
		break;
	default:
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "Invalid vlan proto");
		err = -EINVAL;
	}
	if (err)
		return err;

	err = vcap_rule_add_action_u32(vrule,
				       VCAP_AF_TAG_A_VID_SEL,
				       SPX5_VID_A_VAL);
	if (err)
		return err;

	err = vcap_rule_add_action_u32(vrule,
				       VCAP_AF_VID_A_VAL,
				       act->vlan.vid);
	if (err)
		return err;

	err = vcap_rule_add_action_u32(vrule,
				       VCAP_AF_TAG_A_PCP_SEL,
				       SPX5_PCP_A_VAL);
	if (err)
		return err;

	err = vcap_rule_add_action_u32(vrule,
				       VCAP_AF_PCP_A_VAL,
				       act->vlan.prio);
	if (err)
		return err;

	return vcap_rule_add_action_u32(vrule,
					VCAP_AF_TAG_A_DEI_SEL,
					SPX5_DEI_A_CLASSIFIED);
}

static int sparx5_tc_action_vlan_push(struct vcap_admin *admin,
				      struct vcap_rule *vrule,
				      struct flow_cls_offload *fco,
				      struct flow_action_entry *act,
				      u16 tpid)
{
	u16 act_tpid = be16_to_cpu(act->vlan.proto);
	int err = 0;

	switch (admin->vtype) {
	case VCAP_TYPE_ES0:
		break;
	default:
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "VLAN push action not supported in this VCAP");
		return -EOPNOTSUPP;
	}

	if (tpid == ETH_P_8021AD) {
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "Cannot push on double tagged frames");
		return -EOPNOTSUPP;
	}

	err = sparx5_tc_action_vlan_modify(admin, vrule, fco, act, act_tpid);
	if (err)
		return err;

	switch (act_tpid) {
	case ETH_P_8021Q:
		break;
	case ETH_P_8021AD:
		/* Push classified tag as inner tag */
		err = vcap_rule_add_action_u32(vrule,
					       VCAP_AF_PUSH_INNER_TAG,
					       SPX5_ITAG_PUSH_B_TAG);
		if (err)
			break;
		err = vcap_rule_add_action_u32(vrule,
					       VCAP_AF_TAG_B_TPID_SEL,
					       SPX5_TPID_B_CLASSIFIED);
		break;
	default:
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "Invalid vlan proto");
		err = -EINVAL;
	}
	return err;
}

/* Remove rule keys that may prevent templates from matching a keyset */
static void sparx5_tc_flower_simplify_rule(struct vcap_admin *admin,
					   struct vcap_rule *vrule,
					   u16 l3_proto)
{
	switch (admin->vtype) {
	case VCAP_TYPE_IS0:
		vcap_rule_rem_key(vrule, VCAP_KF_ETYPE);
		switch (l3_proto) {
		case ETH_P_IP:
			break;
		case ETH_P_IPV6:
			vcap_rule_rem_key(vrule, VCAP_KF_IP_SNAP_IS);
			break;
		default:
			break;
		}
		break;
	case VCAP_TYPE_ES2:
		switch (l3_proto) {
		case ETH_P_IP:
			if (vrule->keyset == VCAP_KFS_IP4_OTHER)
				vcap_rule_rem_key(vrule, VCAP_KF_TCP_IS);
			break;
		case ETH_P_IPV6:
			if (vrule->keyset == VCAP_KFS_IP6_STD)
				vcap_rule_rem_key(vrule, VCAP_KF_TCP_IS);
			vcap_rule_rem_key(vrule, VCAP_KF_IP4_IS);
			break;
		default:
			break;
		}
		break;
	case VCAP_TYPE_IS2:
		switch (l3_proto) {
		case ETH_P_IP:
		case ETH_P_IPV6:
			vcap_rule_rem_key(vrule, VCAP_KF_IP4_IS);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static bool sparx5_tc_flower_use_template(struct net_device *ndev,
					  struct flow_cls_offload *fco,
					  struct vcap_admin *admin,
					  struct vcap_rule *vrule)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct sparx5_tc_flower_template *ftp;

	list_for_each_entry(ftp, &port->tc_templates, list) {
		if (ftp->cid != fco->common.chain_index)
			continue;

		vcap_set_rule_set_keyset(vrule, ftp->keyset);
		sparx5_tc_flower_simplify_rule(admin, vrule, ftp->l3_proto);
		return true;
	}
	return false;
}

static int sparx5_tc_flower_replace(struct net_device *ndev,
				    struct flow_cls_offload *fco,
				    struct vcap_admin *admin,
				    bool ingress)
{
	struct sparx5_psfp_sf sf = { .max_sdu = SPX5_PSFP_SF_MAX_SDU };
	struct netlink_ext_ack *extack = fco->common.extack;
	int err, idx, tc_sg_idx = -1, tc_pol_idx = -1;
	struct vcap_tc_flower_parse_usage state = {
		.fco = fco,
		.l3_proto = ETH_P_ALL,
		.admin = admin,
	};
	struct sparx5_port *port = netdev_priv(ndev);
	struct sparx5_multiple_rules multi = {};
	struct sparx5 *sparx5 = port->sparx5;
	struct sparx5_psfp_sg sg = { 0 };
	struct sparx5_psfp_fm fm = { 0 };
	struct flow_action_entry *act;
	struct vcap_control *vctrl;
	struct flow_rule *frule;
	struct vcap_rule *vrule;

	vctrl = port->sparx5->vcap_ctrl;

	err = sparx5_tc_flower_action_check(vctrl, ndev, fco, ingress);
	if (err)
		return err;

	vrule = vcap_alloc_rule(vctrl, ndev, fco->common.chain_index, VCAP_USER_TC,
				fco->common.prio, 0);
	if (IS_ERR(vrule))
		return PTR_ERR(vrule);

	vrule->cookie = fco->cookie;

	state.vrule = vrule;
	state.frule = flow_cls_offload_flow_rule(fco);
	err = sparx5_tc_use_dissectors(&state, admin, vrule);
	if (err)
		goto out;

	err = sparx5_tc_add_rule_counter(admin, vrule);
	if (err)
		goto out;

	err = sparx5_tc_add_rule_link_target(admin, vrule,
					     fco->common.chain_index);
	if (err)
		goto out;

	frule = flow_cls_offload_flow_rule(fco);
	flow_action_for_each(idx, act, &frule->action) {
		switch (act->id) {
		case FLOW_ACTION_GATE: {
			err = sparx5_tc_flower_parse_act_gate(&sg, act, extack);
			if (err < 0)
				goto out;

			tc_sg_idx = act->hw_index;

			break;
		}
		case FLOW_ACTION_POLICE: {
			err = sparx5_tc_flower_parse_act_police(&fm.pol, act,
								extack);
			if (err < 0)
				goto out;

			tc_pol_idx = fm.pol.idx;
			sf.max_sdu = act->police.mtu;

			break;
		}
		case FLOW_ACTION_TRAP:
			err = sparx5_tc_action_trap(admin, vrule, fco);
			if (err)
				goto out;
			break;
		case FLOW_ACTION_ACCEPT:
			err = sparx5_tc_set_actionset(admin, vrule);
			if (err)
				goto out;
			break;
		case FLOW_ACTION_GOTO:
			err = sparx5_tc_set_actionset(admin, vrule);
			if (err)
				goto out;
			sparx5_tc_add_rule_link(vctrl, admin, vrule,
						fco->common.chain_index,
						act->chain_index);
			break;
		case FLOW_ACTION_VLAN_POP:
			err = sparx5_tc_action_vlan_pop(admin, vrule, fco,
							state.tpid);
			if (err)
				goto out;
			break;
		case FLOW_ACTION_VLAN_PUSH:
			err = sparx5_tc_action_vlan_push(admin, vrule, fco,
							 act, state.tpid);
			if (err)
				goto out;
			break;
		case FLOW_ACTION_VLAN_MANGLE:
			err = sparx5_tc_action_vlan_modify(admin, vrule, fco,
							   act, state.tpid);
			if (err)
				goto out;
			break;
		default:
			NL_SET_ERR_MSG_MOD(fco->common.extack,
					   "Unsupported TC action");
			err = -EOPNOTSUPP;
			goto out;
		}
	}

	/* Setup PSFP */
	if (tc_sg_idx >= 0 || tc_pol_idx >= 0) {
		err = sparx5_tc_flower_psfp_setup(sparx5, vrule, tc_sg_idx,
						  tc_pol_idx, &sg, &fm, &sf);
		if (err)
			goto out;
	}

	if (!sparx5_tc_flower_use_template(ndev, fco, admin, vrule)) {
		err = sparx5_tc_select_protocol_keyset(ndev, vrule, admin,
						       state.l3_proto, &multi);
		if (err) {
			NL_SET_ERR_MSG_MOD(fco->common.extack,
					   "No matching port keyset for filter protocol and keys");
			goto out;
		}
	}

	/* provide the l3 protocol to guide the keyset selection */
	err = vcap_val_rule(vrule, state.l3_proto);
	if (err) {
		vcap_set_tc_exterr(fco, vrule);
		goto out;
	}
	err = vcap_add_rule(vrule);
	if (err)
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "Could not add the filter");

	if (state.l3_proto == ETH_P_ALL)
		err = sparx5_tc_add_remaining_rules(vctrl, fco, vrule, admin,
						    &multi);

out:
	vcap_free_rule(vrule);
	return err;
}

static void sparx5_tc_free_psfp_resources(struct sparx5 *sparx5,
					  struct vcap_rule *vrule)
{
	struct vcap_client_actionfield *afield;
	u32 isdx, sfid, sgid, fmid;

	/* Check if VCAP_AF_ISDX_VAL action is set for this rule - and if
	 * it is used for stream and/or flow-meter classification.
	 */
	afield = vcap_find_actionfield(vrule, VCAP_AF_ISDX_VAL);
	if (!afield)
		return;

	isdx = afield->data.u32.value;
	sfid = sparx5_psfp_isdx_get_sf(sparx5, isdx);

	if (!sfid)
		return;

	fmid = sparx5_psfp_isdx_get_fm(sparx5, isdx);
	sgid = sparx5_psfp_sf_get_sg(sparx5, sfid);

	if (fmid && sparx5_psfp_fm_del(sparx5, fmid) < 0)
		pr_err("%s:%d Could not delete invalid fmid: %d", __func__,
		       __LINE__, fmid);

	if (sgid && sparx5_psfp_sg_del(sparx5, sgid) < 0)
		pr_err("%s:%d Could not delete invalid sgid: %d", __func__,
		       __LINE__, sgid);

	if (sparx5_psfp_sf_del(sparx5, sfid) < 0)
		pr_err("%s:%d Could not delete invalid sfid: %d", __func__,
		       __LINE__, sfid);

	sparx5_isdx_conf_set(sparx5, isdx, 0, 0);
}

static int sparx5_tc_free_rule_resources(struct net_device *ndev,
					 struct vcap_control *vctrl,
					 int rule_id)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct sparx5 *sparx5 = port->sparx5;
	struct vcap_rule *vrule;
	int ret = 0;

	vrule = vcap_get_rule(vctrl, rule_id);
	if (!vrule || IS_ERR(vrule))
		return -EINVAL;

	sparx5_tc_free_psfp_resources(sparx5, vrule);

	vcap_free_rule(vrule);
	return ret;
}

static int sparx5_tc_flower_destroy(struct net_device *ndev,
				    struct flow_cls_offload *fco,
				    struct vcap_admin *admin)
{
	struct sparx5_port *port = netdev_priv(ndev);
	int err = -ENOENT, count = 0, rule_id;
	struct vcap_control *vctrl;

	vctrl = port->sparx5->vcap_ctrl;
	while (true) {
		rule_id = vcap_lookup_rule_by_cookie(vctrl, fco->cookie);
		if (rule_id <= 0)
			break;
		if (count == 0) {
			/* Resources are attached to the first rule of
			 * a set of rules. Only works if the rules are
			 * in the correct order.
			 */
			err = sparx5_tc_free_rule_resources(ndev, vctrl,
							    rule_id);
			if (err)
				pr_err("%s:%d: could not free resources %d\n",
				       __func__, __LINE__, rule_id);
		}
		err = vcap_del_rule(vctrl, ndev, rule_id);
		if (err) {
			pr_err("%s:%d: could not delete rule %d\n",
			       __func__, __LINE__, rule_id);
			break;
		}
	}
	return err;
}

static int sparx5_tc_flower_stats(struct net_device *ndev,
				  struct flow_cls_offload *fco,
				  struct vcap_admin *admin)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct vcap_counter ctr = {};
	struct vcap_control *vctrl;
	ulong lastused = 0;
	int err;

	vctrl = port->sparx5->vcap_ctrl;
	err = vcap_get_rule_count_by_cookie(vctrl, &ctr, fco->cookie);
	if (err)
		return err;
	flow_stats_update(&fco->stats, 0x0, ctr.value, 0, lastused,
			  FLOW_ACTION_HW_STATS_IMMEDIATE);
	return err;
}

static int sparx5_tc_flower_template_create(struct net_device *ndev,
					    struct flow_cls_offload *fco,
					    struct vcap_admin *admin)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct vcap_tc_flower_parse_usage state = {
		.fco = fco,
		.l3_proto = ETH_P_ALL,
		.admin = admin,
	};
	struct sparx5_tc_flower_template *ftp;
	struct vcap_keyset_list kslist = {};
	enum vcap_keyfield_set keysets[10];
	struct vcap_control *vctrl;
	struct vcap_rule *vrule;
	int count, err;

	if (admin->vtype == VCAP_TYPE_ES0) {
		pr_err("%s:%d: %s\n", __func__, __LINE__,
		       "VCAP does not support templates");
		return -EINVAL;
	}

	count = vcap_admin_rule_count(admin, fco->common.chain_index);
	if (count > 0) {
		pr_err("%s:%d: %s\n", __func__, __LINE__,
		       "Filters are already present");
		return -EBUSY;
	}

	ftp = kzalloc(sizeof(*ftp), GFP_KERNEL);
	if (!ftp)
		return -ENOMEM;

	ftp->cid = fco->common.chain_index;
	ftp->orig = VCAP_KFS_NO_VALUE;
	ftp->keyset = VCAP_KFS_NO_VALUE;

	vctrl = port->sparx5->vcap_ctrl;
	vrule = vcap_alloc_rule(vctrl, ndev, fco->common.chain_index,
				VCAP_USER_TC, fco->common.prio, 0);
	if (IS_ERR(vrule)) {
		err = PTR_ERR(vrule);
		goto err_rule;
	}

	state.vrule = vrule;
	state.frule = flow_cls_offload_flow_rule(fco);
	err = sparx5_tc_use_dissectors(&state, admin, vrule);
	if (err) {
		pr_err("%s:%d: key error: %d\n", __func__, __LINE__, err);
		goto out;
	}

	ftp->l3_proto = state.l3_proto;

	sparx5_tc_flower_simplify_rule(admin, vrule, state.l3_proto);

	/* Find the keysets that the rule can use */
	kslist.keysets = keysets;
	kslist.max = ARRAY_SIZE(keysets);
	if (!vcap_rule_find_keysets(vrule, &kslist)) {
		pr_err("%s:%d: %s\n", __func__, __LINE__,
		       "Could not find a suitable keyset");
		err = -ENOENT;
		goto out;
	}

	ftp->keyset = vcap_select_min_rule_keyset(vctrl, admin->vtype, &kslist);
	kslist.cnt = 0;
	sparx5_vcap_set_port_keyset(ndev, admin, fco->common.chain_index,
				    state.l3_proto,
				    ftp->keyset,
				    &kslist);

	if (kslist.cnt > 0)
		ftp->orig = kslist.keysets[0];

	/* Store new template */
	list_add_tail(&ftp->list, &port->tc_templates);
	vcap_free_rule(vrule);
	return 0;

out:
	vcap_free_rule(vrule);
err_rule:
	kfree(ftp);
	return err;
}

static int sparx5_tc_flower_template_destroy(struct net_device *ndev,
					     struct flow_cls_offload *fco,
					     struct vcap_admin *admin)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct sparx5_tc_flower_template *ftp, *tmp;
	int err = -ENOENT;

	/* Rules using the template are removed by the tc framework */
	list_for_each_entry_safe(ftp, tmp, &port->tc_templates, list) {
		if (ftp->cid != fco->common.chain_index)
			continue;

		sparx5_vcap_set_port_keyset(ndev, admin,
					    fco->common.chain_index,
					    ftp->l3_proto, ftp->orig,
					    NULL);
		list_del(&ftp->list);
		kfree(ftp);
		break;
	}
	return err;
}

int sparx5_tc_flower(struct net_device *ndev, struct flow_cls_offload *fco,
		     bool ingress)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct vcap_control *vctrl;
	struct vcap_admin *admin;
	int err = -EINVAL;

	/* Get vcap instance from the chain id */
	vctrl = port->sparx5->vcap_ctrl;
	admin = vcap_find_admin(vctrl, fco->common.chain_index);
	if (!admin) {
		NL_SET_ERR_MSG_MOD(fco->common.extack, "Invalid chain");
		return err;
	}

	switch (fco->command) {
	case FLOW_CLS_REPLACE:
		return sparx5_tc_flower_replace(ndev, fco, admin, ingress);
	case FLOW_CLS_DESTROY:
		return sparx5_tc_flower_destroy(ndev, fco, admin);
	case FLOW_CLS_STATS:
		return sparx5_tc_flower_stats(ndev, fco, admin);
	case FLOW_CLS_TMPLT_CREATE:
		return sparx5_tc_flower_template_create(ndev, fco, admin);
	case FLOW_CLS_TMPLT_DESTROY:
		return sparx5_tc_flower_template_destroy(ndev, fco, admin);
	default:
		return -EOPNOTSUPP;
	}
}
