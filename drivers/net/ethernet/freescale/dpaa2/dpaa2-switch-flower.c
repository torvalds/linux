// SPDX-License-Identifier: GPL-2.0
/*
 * DPAA2 Ethernet Switch flower support
 *
 * Copyright 2021 NXP
 *
 */

#include "dpaa2-switch.h"

static int dpaa2_switch_flower_parse_key(struct flow_cls_offload *cls,
					 struct dpsw_acl_key *acl_key)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct flow_dissector *dissector = rule->match.dissector;
	struct netlink_ext_ack *extack = cls->common.extack;
	struct dpsw_acl_fields *acl_h, *acl_m;

	if (dissector->used_keys &
	    ~(BIT(FLOW_DISSECTOR_KEY_BASIC) |
	      BIT(FLOW_DISSECTOR_KEY_CONTROL) |
	      BIT(FLOW_DISSECTOR_KEY_ETH_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_VLAN) |
	      BIT(FLOW_DISSECTOR_KEY_PORTS) |
	      BIT(FLOW_DISSECTOR_KEY_IP) |
	      BIT(FLOW_DISSECTOR_KEY_IPV6_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_IPV4_ADDRS))) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Unsupported keys used");
		return -EOPNOTSUPP;
	}

	acl_h = &acl_key->match;
	acl_m = &acl_key->mask;

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);
		acl_h->l3_protocol = match.key->ip_proto;
		acl_h->l2_ether_type = be16_to_cpu(match.key->n_proto);
		acl_m->l3_protocol = match.mask->ip_proto;
		acl_m->l2_ether_type = be16_to_cpu(match.mask->n_proto);
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct flow_match_eth_addrs match;

		flow_rule_match_eth_addrs(rule, &match);
		ether_addr_copy(acl_h->l2_dest_mac, &match.key->dst[0]);
		ether_addr_copy(acl_h->l2_source_mac, &match.key->src[0]);
		ether_addr_copy(acl_m->l2_dest_mac, &match.mask->dst[0]);
		ether_addr_copy(acl_m->l2_source_mac, &match.mask->src[0]);
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_match_vlan match;

		flow_rule_match_vlan(rule, &match);
		acl_h->l2_vlan_id = match.key->vlan_id;
		acl_h->l2_tpid = be16_to_cpu(match.key->vlan_tpid);
		acl_h->l2_pcp_dei = match.key->vlan_priority << 1 |
				    match.key->vlan_dei;

		acl_m->l2_vlan_id = match.mask->vlan_id;
		acl_m->l2_tpid = be16_to_cpu(match.mask->vlan_tpid);
		acl_m->l2_pcp_dei = match.mask->vlan_priority << 1 |
				    match.mask->vlan_dei;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV4_ADDRS)) {
		struct flow_match_ipv4_addrs match;

		flow_rule_match_ipv4_addrs(rule, &match);
		acl_h->l3_source_ip = be32_to_cpu(match.key->src);
		acl_h->l3_dest_ip = be32_to_cpu(match.key->dst);
		acl_m->l3_source_ip = be32_to_cpu(match.mask->src);
		acl_m->l3_dest_ip = be32_to_cpu(match.mask->dst);
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_match_ports match;

		flow_rule_match_ports(rule, &match);
		acl_h->l4_source_port = be16_to_cpu(match.key->src);
		acl_h->l4_dest_port = be16_to_cpu(match.key->dst);
		acl_m->l4_source_port = be16_to_cpu(match.mask->src);
		acl_m->l4_dest_port = be16_to_cpu(match.mask->dst);
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IP)) {
		struct flow_match_ip match;

		flow_rule_match_ip(rule, &match);
		if (match.mask->ttl != 0) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Matching on TTL not supported");
			return -EOPNOTSUPP;
		}

		if ((match.mask->tos & 0x3) != 0) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Matching on ECN not supported, only DSCP");
			return -EOPNOTSUPP;
		}

		acl_h->l3_dscp = match.key->tos >> 2;
		acl_m->l3_dscp = match.mask->tos >> 2;
	}

	return 0;
}

int dpaa2_switch_acl_entry_add(struct dpaa2_switch_acl_tbl *acl_tbl,
			       struct dpaa2_switch_acl_entry *entry)
{
	struct dpsw_acl_entry_cfg *acl_entry_cfg = &entry->cfg;
	struct ethsw_core *ethsw = acl_tbl->ethsw;
	struct dpsw_acl_key *acl_key = &entry->key;
	struct device *dev = ethsw->dev;
	u8 *cmd_buff;
	int err;

	cmd_buff = kzalloc(DPAA2_ETHSW_PORT_ACL_CMD_BUF_SIZE, GFP_KERNEL);
	if (!cmd_buff)
		return -ENOMEM;

	dpsw_acl_prepare_entry_cfg(acl_key, cmd_buff);

	acl_entry_cfg->key_iova = dma_map_single(dev, cmd_buff,
						 DPAA2_ETHSW_PORT_ACL_CMD_BUF_SIZE,
						 DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dev, acl_entry_cfg->key_iova))) {
		dev_err(dev, "DMA mapping failed\n");
		return -EFAULT;
	}

	err = dpsw_acl_add_entry(ethsw->mc_io, 0, ethsw->dpsw_handle,
				 acl_tbl->id, acl_entry_cfg);

	dma_unmap_single(dev, acl_entry_cfg->key_iova, sizeof(cmd_buff),
			 DMA_TO_DEVICE);
	if (err) {
		dev_err(dev, "dpsw_acl_add_entry() failed %d\n", err);
		return err;
	}

	kfree(cmd_buff);

	return 0;
}

static int dpaa2_switch_acl_entry_remove(struct dpaa2_switch_acl_tbl *acl_tbl,
					 struct dpaa2_switch_acl_entry *entry)
{
	struct dpsw_acl_entry_cfg *acl_entry_cfg = &entry->cfg;
	struct dpsw_acl_key *acl_key = &entry->key;
	struct ethsw_core *ethsw = acl_tbl->ethsw;
	struct device *dev = ethsw->dev;
	u8 *cmd_buff;
	int err;

	cmd_buff = kzalloc(DPAA2_ETHSW_PORT_ACL_CMD_BUF_SIZE, GFP_KERNEL);
	if (!cmd_buff)
		return -ENOMEM;

	dpsw_acl_prepare_entry_cfg(acl_key, cmd_buff);

	acl_entry_cfg->key_iova = dma_map_single(dev, cmd_buff,
						 DPAA2_ETHSW_PORT_ACL_CMD_BUF_SIZE,
						 DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dev, acl_entry_cfg->key_iova))) {
		dev_err(dev, "DMA mapping failed\n");
		return -EFAULT;
	}

	err = dpsw_acl_remove_entry(ethsw->mc_io, 0, ethsw->dpsw_handle,
				    acl_tbl->id, acl_entry_cfg);

	dma_unmap_single(dev, acl_entry_cfg->key_iova, sizeof(cmd_buff),
			 DMA_TO_DEVICE);
	if (err) {
		dev_err(dev, "dpsw_acl_remove_entry() failed %d\n", err);
		return err;
	}

	kfree(cmd_buff);

	return 0;
}

static int
dpaa2_switch_acl_entry_add_to_list(struct dpaa2_switch_acl_tbl *acl_tbl,
				   struct dpaa2_switch_acl_entry *entry)
{
	struct dpaa2_switch_acl_entry *tmp;
	struct list_head *pos, *n;
	int index = 0;

	if (list_empty(&acl_tbl->entries)) {
		list_add(&entry->list, &acl_tbl->entries);
		return index;
	}

	list_for_each_safe(pos, n, &acl_tbl->entries) {
		tmp = list_entry(pos, struct dpaa2_switch_acl_entry, list);
		if (entry->prio < tmp->prio)
			break;
		index++;
	}
	list_add(&entry->list, pos->prev);
	return index;
}

static struct dpaa2_switch_acl_entry*
dpaa2_switch_acl_entry_get_by_index(struct dpaa2_switch_acl_tbl *acl_tbl,
				    int index)
{
	struct dpaa2_switch_acl_entry *tmp;
	int i = 0;

	list_for_each_entry(tmp, &acl_tbl->entries, list) {
		if (i == index)
			return tmp;
		++i;
	}

	return NULL;
}

static int
dpaa2_switch_acl_entry_set_precedence(struct dpaa2_switch_acl_tbl *acl_tbl,
				      struct dpaa2_switch_acl_entry *entry,
				      int precedence)
{
	int err;

	err = dpaa2_switch_acl_entry_remove(acl_tbl, entry);
	if (err)
		return err;

	entry->cfg.precedence = precedence;
	return dpaa2_switch_acl_entry_add(acl_tbl, entry);
}

static int dpaa2_switch_acl_tbl_add_entry(struct dpaa2_switch_acl_tbl *acl_tbl,
					  struct dpaa2_switch_acl_entry *entry)
{
	struct dpaa2_switch_acl_entry *tmp;
	int index, i, precedence, err;

	/* Add the new ACL entry to the linked list and get its index */
	index = dpaa2_switch_acl_entry_add_to_list(acl_tbl, entry);

	/* Move up in priority the ACL entries to make space
	 * for the new filter.
	 */
	precedence = DPAA2_ETHSW_PORT_MAX_ACL_ENTRIES - acl_tbl->num_rules - 1;
	for (i = 0; i < index; i++) {
		tmp = dpaa2_switch_acl_entry_get_by_index(acl_tbl, i);

		err = dpaa2_switch_acl_entry_set_precedence(acl_tbl, tmp,
							    precedence);
		if (err)
			return err;

		precedence++;
	}

	/* Add the new entry to hardware */
	entry->cfg.precedence = precedence;
	err = dpaa2_switch_acl_entry_add(acl_tbl, entry);
	acl_tbl->num_rules++;

	return err;
}

static struct dpaa2_switch_acl_entry *
dpaa2_switch_acl_tbl_find_entry_by_cookie(struct dpaa2_switch_acl_tbl *acl_tbl,
					  unsigned long cookie)
{
	struct dpaa2_switch_acl_entry *tmp, *n;

	list_for_each_entry_safe(tmp, n, &acl_tbl->entries, list) {
		if (tmp->cookie == cookie)
			return tmp;
	}
	return NULL;
}

static int
dpaa2_switch_acl_entry_get_index(struct dpaa2_switch_acl_tbl *acl_tbl,
				 struct dpaa2_switch_acl_entry *entry)
{
	struct dpaa2_switch_acl_entry *tmp, *n;
	int index = 0;

	list_for_each_entry_safe(tmp, n, &acl_tbl->entries, list) {
		if (tmp->cookie == entry->cookie)
			return index;
		index++;
	}
	return -ENOENT;
}

static int
dpaa2_switch_acl_tbl_remove_entry(struct dpaa2_switch_acl_tbl *acl_tbl,
				  struct dpaa2_switch_acl_entry *entry)
{
	struct dpaa2_switch_acl_entry *tmp;
	int index, i, precedence, err;

	index = dpaa2_switch_acl_entry_get_index(acl_tbl, entry);

	/* Remove from hardware the ACL entry */
	err = dpaa2_switch_acl_entry_remove(acl_tbl, entry);
	if (err)
		return err;

	acl_tbl->num_rules--;

	/* Remove it from the list also */
	list_del(&entry->list);

	/* Move down in priority the entries over the deleted one */
	precedence = entry->cfg.precedence;
	for (i = index - 1; i >= 0; i--) {
		tmp = dpaa2_switch_acl_entry_get_by_index(acl_tbl, i);
		err = dpaa2_switch_acl_entry_set_precedence(acl_tbl, tmp,
							    precedence);
		if (err)
			return err;

		precedence--;
	}

	kfree(entry);

	return 0;
}

static int dpaa2_switch_tc_parse_action(struct ethsw_core *ethsw,
					struct flow_action_entry *cls_act,
					struct dpsw_acl_result *dpsw_act,
					struct netlink_ext_ack *extack)
{
	int err = 0;

	switch (cls_act->id) {
	case FLOW_ACTION_TRAP:
		dpsw_act->action = DPSW_ACL_ACTION_REDIRECT_TO_CTRL_IF;
		break;
	case FLOW_ACTION_REDIRECT:
		if (!dpaa2_switch_port_dev_check(cls_act->dev)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Destination not a DPAA2 switch port");
			return -EOPNOTSUPP;
		}

		dpsw_act->if_id = dpaa2_switch_get_index(ethsw, cls_act->dev);
		dpsw_act->action = DPSW_ACL_ACTION_REDIRECT;
		break;
	case FLOW_ACTION_DROP:
		dpsw_act->action = DPSW_ACL_ACTION_DROP;
		break;
	default:
		NL_SET_ERR_MSG_MOD(extack,
				   "Action not supported");
		err = -EOPNOTSUPP;
		goto out;
	}

out:
	return err;
}

int dpaa2_switch_cls_flower_replace(struct dpaa2_switch_acl_tbl *acl_tbl,
				    struct flow_cls_offload *cls)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct netlink_ext_ack *extack = cls->common.extack;
	struct ethsw_core *ethsw = acl_tbl->ethsw;
	struct dpaa2_switch_acl_entry *acl_entry;
	struct flow_action_entry *act;
	int err;

	if (!flow_offload_has_one_action(&rule->action)) {
		NL_SET_ERR_MSG(extack, "Only singular actions are supported");
		return -EOPNOTSUPP;
	}

	if (dpaa2_switch_acl_tbl_is_full(acl_tbl)) {
		NL_SET_ERR_MSG(extack, "Maximum filter capacity reached");
		return -ENOMEM;
	}

	acl_entry = kzalloc(sizeof(*acl_entry), GFP_KERNEL);
	if (!acl_entry)
		return -ENOMEM;

	err = dpaa2_switch_flower_parse_key(cls, &acl_entry->key);
	if (err)
		goto free_acl_entry;

	act = &rule->action.entries[0];
	err = dpaa2_switch_tc_parse_action(ethsw, act,
					   &acl_entry->cfg.result, extack);
	if (err)
		goto free_acl_entry;

	acl_entry->prio = cls->common.prio;
	acl_entry->cookie = cls->cookie;

	err = dpaa2_switch_acl_tbl_add_entry(acl_tbl, acl_entry);
	if (err)
		goto free_acl_entry;

	return 0;

free_acl_entry:
	kfree(acl_entry);

	return err;
}

int dpaa2_switch_cls_flower_destroy(struct dpaa2_switch_acl_tbl *acl_tbl,
				    struct flow_cls_offload *cls)
{
	struct dpaa2_switch_acl_entry *entry;

	entry = dpaa2_switch_acl_tbl_find_entry_by_cookie(acl_tbl, cls->cookie);
	if (!entry)
		return 0;

	return dpaa2_switch_acl_tbl_remove_entry(acl_tbl, entry);
}

int dpaa2_switch_cls_matchall_replace(struct dpaa2_switch_acl_tbl *acl_tbl,
				      struct tc_cls_matchall_offload *cls)
{
	struct netlink_ext_ack *extack = cls->common.extack;
	struct ethsw_core *ethsw = acl_tbl->ethsw;
	struct dpaa2_switch_acl_entry *acl_entry;
	struct flow_action_entry *act;
	int err;

	if (!flow_offload_has_one_action(&cls->rule->action)) {
		NL_SET_ERR_MSG(extack, "Only singular actions are supported");
		return -EOPNOTSUPP;
	}

	if (dpaa2_switch_acl_tbl_is_full(acl_tbl)) {
		NL_SET_ERR_MSG(extack, "Maximum filter capacity reached");
		return -ENOMEM;
	}

	acl_entry = kzalloc(sizeof(*acl_entry), GFP_KERNEL);
	if (!acl_entry)
		return -ENOMEM;

	act = &cls->rule->action.entries[0];
	err = dpaa2_switch_tc_parse_action(ethsw, act,
					   &acl_entry->cfg.result, extack);
	if (err)
		goto free_acl_entry;

	acl_entry->prio = cls->common.prio;
	acl_entry->cookie = cls->cookie;

	err = dpaa2_switch_acl_tbl_add_entry(acl_tbl, acl_entry);
	if (err)
		goto free_acl_entry;

	return 0;

free_acl_entry:
	kfree(acl_entry);

	return err;
}

int dpaa2_switch_cls_matchall_destroy(struct dpaa2_switch_acl_tbl *acl_tbl,
				      struct tc_cls_matchall_offload *cls)
{
	struct dpaa2_switch_acl_entry *entry;

	entry = dpaa2_switch_acl_tbl_find_entry_by_cookie(acl_tbl, cls->cookie);
	if (!entry)
		return 0;

	return  dpaa2_switch_acl_tbl_remove_entry(acl_tbl, entry);
}
