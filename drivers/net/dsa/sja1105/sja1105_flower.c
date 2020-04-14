// SPDX-License-Identifier: GPL-2.0
/* Copyright 2020, NXP Semiconductors
 */
#include "sja1105.h"

static struct sja1105_rule *sja1105_rule_find(struct sja1105_private *priv,
					      unsigned long cookie)
{
	struct sja1105_rule *rule;

	list_for_each_entry(rule, &priv->flow_block.rules, list)
		if (rule->cookie == cookie)
			return rule;

	return NULL;
}

static int sja1105_find_free_l2_policer(struct sja1105_private *priv)
{
	int i;

	for (i = 0; i < SJA1105_NUM_L2_POLICERS; i++)
		if (!priv->flow_block.l2_policer_used[i])
			return i;

	return -1;
}

static int sja1105_setup_bcast_policer(struct sja1105_private *priv,
				       struct netlink_ext_ack *extack,
				       unsigned long cookie, int port,
				       u64 rate_bytes_per_sec,
				       s64 burst)
{
	struct sja1105_rule *rule = sja1105_rule_find(priv, cookie);
	struct sja1105_l2_policing_entry *policing;
	bool new_rule = false;
	unsigned long p;
	int rc;

	if (!rule) {
		rule = kzalloc(sizeof(*rule), GFP_KERNEL);
		if (!rule)
			return -ENOMEM;

		rule->cookie = cookie;
		rule->type = SJA1105_RULE_BCAST_POLICER;
		rule->bcast_pol.sharindx = sja1105_find_free_l2_policer(priv);
		new_rule = true;
	}

	if (rule->bcast_pol.sharindx == -1) {
		NL_SET_ERR_MSG_MOD(extack, "No more L2 policers free");
		rc = -ENOSPC;
		goto out;
	}

	policing = priv->static_config.tables[BLK_IDX_L2_POLICING].entries;

	if (policing[(SJA1105_NUM_PORTS * SJA1105_NUM_TC) + port].sharindx != port) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Port already has a broadcast policer");
		rc = -EEXIST;
		goto out;
	}

	rule->port_mask |= BIT(port);

	/* Make the broadcast policers of all ports attached to this block
	 * point to the newly allocated policer
	 */
	for_each_set_bit(p, &rule->port_mask, SJA1105_NUM_PORTS) {
		int bcast = (SJA1105_NUM_PORTS * SJA1105_NUM_TC) + p;

		policing[bcast].sharindx = rule->bcast_pol.sharindx;
	}

	policing[rule->bcast_pol.sharindx].rate = div_u64(rate_bytes_per_sec *
							  512, 1000000);
	policing[rule->bcast_pol.sharindx].smax = div_u64(rate_bytes_per_sec *
							  PSCHED_NS2TICKS(burst),
							  PSCHED_TICKS_PER_SEC);
	/* TODO: support per-flow MTU */
	policing[rule->bcast_pol.sharindx].maxlen = VLAN_ETH_FRAME_LEN +
						    ETH_FCS_LEN;

	rc = sja1105_static_config_reload(priv, SJA1105_BEST_EFFORT_POLICING);

out:
	if (rc == 0 && new_rule) {
		priv->flow_block.l2_policer_used[rule->bcast_pol.sharindx] = true;
		list_add(&rule->list, &priv->flow_block.rules);
	} else if (new_rule) {
		kfree(rule);
	}

	return rc;
}

static int sja1105_setup_tc_policer(struct sja1105_private *priv,
				    struct netlink_ext_ack *extack,
				    unsigned long cookie, int port, int tc,
				    u64 rate_bytes_per_sec,
				    s64 burst)
{
	struct sja1105_rule *rule = sja1105_rule_find(priv, cookie);
	struct sja1105_l2_policing_entry *policing;
	bool new_rule = false;
	unsigned long p;
	int rc;

	if (!rule) {
		rule = kzalloc(sizeof(*rule), GFP_KERNEL);
		if (!rule)
			return -ENOMEM;

		rule->cookie = cookie;
		rule->type = SJA1105_RULE_TC_POLICER;
		rule->tc_pol.sharindx = sja1105_find_free_l2_policer(priv);
		rule->tc_pol.tc = tc;
		new_rule = true;
	}

	if (rule->tc_pol.sharindx == -1) {
		NL_SET_ERR_MSG_MOD(extack, "No more L2 policers free");
		rc = -ENOSPC;
		goto out;
	}

	policing = priv->static_config.tables[BLK_IDX_L2_POLICING].entries;

	if (policing[(port * SJA1105_NUM_TC) + tc].sharindx != port) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Port-TC pair already has an L2 policer");
		rc = -EEXIST;
		goto out;
	}

	rule->port_mask |= BIT(port);

	/* Make the policers for traffic class @tc of all ports attached to
	 * this block point to the newly allocated policer
	 */
	for_each_set_bit(p, &rule->port_mask, SJA1105_NUM_PORTS) {
		int index = (p * SJA1105_NUM_TC) + tc;

		policing[index].sharindx = rule->tc_pol.sharindx;
	}

	policing[rule->tc_pol.sharindx].rate = div_u64(rate_bytes_per_sec *
						       512, 1000000);
	policing[rule->tc_pol.sharindx].smax = div_u64(rate_bytes_per_sec *
						       PSCHED_NS2TICKS(burst),
						       PSCHED_TICKS_PER_SEC);
	/* TODO: support per-flow MTU */
	policing[rule->tc_pol.sharindx].maxlen = VLAN_ETH_FRAME_LEN +
						 ETH_FCS_LEN;

	rc = sja1105_static_config_reload(priv, SJA1105_BEST_EFFORT_POLICING);

out:
	if (rc == 0 && new_rule) {
		priv->flow_block.l2_policer_used[rule->tc_pol.sharindx] = true;
		list_add(&rule->list, &priv->flow_block.rules);
	} else if (new_rule) {
		kfree(rule);
	}

	return rc;
}

static int sja1105_flower_parse_policer(struct sja1105_private *priv, int port,
					struct netlink_ext_ack *extack,
					struct flow_cls_offload *cls,
					u64 rate_bytes_per_sec,
					s64 burst)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct flow_dissector *dissector = rule->match.dissector;

	if (dissector->used_keys &
	    ~(BIT(FLOW_DISSECTOR_KEY_BASIC) |
	      BIT(FLOW_DISSECTOR_KEY_CONTROL) |
	      BIT(FLOW_DISSECTOR_KEY_VLAN) |
	      BIT(FLOW_DISSECTOR_KEY_ETH_ADDRS))) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Unsupported keys used");
		return -EOPNOTSUPP;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);
		if (match.key->n_proto) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Matching on protocol not supported");
			return -EOPNOTSUPP;
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		u8 bcast[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
		u8 null[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
		struct flow_match_eth_addrs match;

		flow_rule_match_eth_addrs(rule, &match);

		if (!ether_addr_equal_masked(match.key->src, null,
					     match.mask->src)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Matching on source MAC not supported");
			return -EOPNOTSUPP;
		}

		if (!ether_addr_equal_masked(match.key->dst, bcast,
					     match.mask->dst)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Only matching on broadcast DMAC is supported");
			return -EOPNOTSUPP;
		}

		return sja1105_setup_bcast_policer(priv, extack, cls->cookie,
						   port, rate_bytes_per_sec,
						   burst);
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_match_vlan match;

		flow_rule_match_vlan(rule, &match);

		if (match.key->vlan_id & match.mask->vlan_id) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Matching on VID is not supported");
			return -EOPNOTSUPP;
		}

		if (match.mask->vlan_priority != 0x7) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Masked matching on PCP is not supported");
			return -EOPNOTSUPP;
		}

		return sja1105_setup_tc_policer(priv, extack, cls->cookie, port,
						match.key->vlan_priority,
						rate_bytes_per_sec,
						burst);
	}

	NL_SET_ERR_MSG_MOD(extack, "Not matching on any known key");
	return -EOPNOTSUPP;
}

int sja1105_cls_flower_add(struct dsa_switch *ds, int port,
			   struct flow_cls_offload *cls, bool ingress)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct netlink_ext_ack *extack = cls->common.extack;
	struct sja1105_private *priv = ds->priv;
	const struct flow_action_entry *act;
	int rc = -EOPNOTSUPP, i;

	flow_action_for_each(i, act, &rule->action) {
		switch (act->id) {
		case FLOW_ACTION_POLICE:
			rc = sja1105_flower_parse_policer(priv, port, extack, cls,
							  act->police.rate_bytes_ps,
							  act->police.burst);
			break;
		default:
			NL_SET_ERR_MSG_MOD(extack,
					   "Action not supported");
			break;
		}
	}

	return rc;
}

int sja1105_cls_flower_del(struct dsa_switch *ds, int port,
			   struct flow_cls_offload *cls, bool ingress)
{
	struct sja1105_private *priv = ds->priv;
	struct sja1105_rule *rule = sja1105_rule_find(priv, cls->cookie);
	struct sja1105_l2_policing_entry *policing;
	int old_sharindx;

	if (!rule)
		return 0;

	policing = priv->static_config.tables[BLK_IDX_L2_POLICING].entries;

	if (rule->type == SJA1105_RULE_BCAST_POLICER) {
		int bcast = (SJA1105_NUM_PORTS * SJA1105_NUM_TC) + port;

		old_sharindx = policing[bcast].sharindx;
		policing[bcast].sharindx = port;
	} else if (rule->type == SJA1105_RULE_TC_POLICER) {
		int index = (port * SJA1105_NUM_TC) + rule->tc_pol.tc;

		old_sharindx = policing[index].sharindx;
		policing[index].sharindx = port;
	} else {
		return -EINVAL;
	}

	rule->port_mask &= ~BIT(port);
	if (!rule->port_mask) {
		priv->flow_block.l2_policer_used[old_sharindx] = false;
		list_del(&rule->list);
		kfree(rule);
	}

	return sja1105_static_config_reload(priv, SJA1105_BEST_EFFORT_POLICING);
}

void sja1105_flower_setup(struct dsa_switch *ds)
{
	struct sja1105_private *priv = ds->priv;
	int port;

	INIT_LIST_HEAD(&priv->flow_block.rules);

	for (port = 0; port < SJA1105_NUM_PORTS; port++)
		priv->flow_block.l2_policer_used[port] = true;
}

void sja1105_flower_teardown(struct dsa_switch *ds)
{
	struct sja1105_private *priv = ds->priv;
	struct sja1105_rule *rule;
	struct list_head *pos, *n;

	list_for_each_safe(pos, n, &priv->flow_block.rules) {
		rule = list_entry(pos, struct sja1105_rule, list);
		list_del(&rule->list);
		kfree(rule);
	}
}
