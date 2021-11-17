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

int dpaa2_switch_acl_entry_add(struct dpaa2_switch_filter_block *filter_block,
			       struct dpaa2_switch_acl_entry *entry)
{
	struct dpsw_acl_entry_cfg *acl_entry_cfg = &entry->cfg;
	struct ethsw_core *ethsw = filter_block->ethsw;
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
				 filter_block->acl_id, acl_entry_cfg);

	dma_unmap_single(dev, acl_entry_cfg->key_iova, sizeof(cmd_buff),
			 DMA_TO_DEVICE);
	if (err) {
		dev_err(dev, "dpsw_acl_add_entry() failed %d\n", err);
		return err;
	}

	kfree(cmd_buff);

	return 0;
}

static int
dpaa2_switch_acl_entry_remove(struct dpaa2_switch_filter_block *block,
			      struct dpaa2_switch_acl_entry *entry)
{
	struct dpsw_acl_entry_cfg *acl_entry_cfg = &entry->cfg;
	struct dpsw_acl_key *acl_key = &entry->key;
	struct ethsw_core *ethsw = block->ethsw;
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
				    block->acl_id, acl_entry_cfg);

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
dpaa2_switch_acl_entry_add_to_list(struct dpaa2_switch_filter_block *block,
				   struct dpaa2_switch_acl_entry *entry)
{
	struct dpaa2_switch_acl_entry *tmp;
	struct list_head *pos, *n;
	int index = 0;

	if (list_empty(&block->acl_entries)) {
		list_add(&entry->list, &block->acl_entries);
		return index;
	}

	list_for_each_safe(pos, n, &block->acl_entries) {
		tmp = list_entry(pos, struct dpaa2_switch_acl_entry, list);
		if (entry->prio < tmp->prio)
			break;
		index++;
	}
	list_add(&entry->list, pos->prev);
	return index;
}

static struct dpaa2_switch_acl_entry*
dpaa2_switch_acl_entry_get_by_index(struct dpaa2_switch_filter_block *block,
				    int index)
{
	struct dpaa2_switch_acl_entry *tmp;
	int i = 0;

	list_for_each_entry(tmp, &block->acl_entries, list) {
		if (i == index)
			return tmp;
		++i;
	}

	return NULL;
}

static int
dpaa2_switch_acl_entry_set_precedence(struct dpaa2_switch_filter_block *block,
				      struct dpaa2_switch_acl_entry *entry,
				      int precedence)
{
	int err;

	err = dpaa2_switch_acl_entry_remove(block, entry);
	if (err)
		return err;

	entry->cfg.precedence = precedence;
	return dpaa2_switch_acl_entry_add(block, entry);
}

static int
dpaa2_switch_acl_tbl_add_entry(struct dpaa2_switch_filter_block *block,
			       struct dpaa2_switch_acl_entry *entry)
{
	struct dpaa2_switch_acl_entry *tmp;
	int index, i, precedence, err;

	/* Add the new ACL entry to the linked list and get its index */
	index = dpaa2_switch_acl_entry_add_to_list(block, entry);

	/* Move up in priority the ACL entries to make space
	 * for the new filter.
	 */
	precedence = DPAA2_ETHSW_PORT_MAX_ACL_ENTRIES - block->num_acl_rules - 1;
	for (i = 0; i < index; i++) {
		tmp = dpaa2_switch_acl_entry_get_by_index(block, i);

		err = dpaa2_switch_acl_entry_set_precedence(block, tmp,
							    precedence);
		if (err)
			return err;

		precedence++;
	}

	/* Add the new entry to hardware */
	entry->cfg.precedence = precedence;
	err = dpaa2_switch_acl_entry_add(block, entry);
	block->num_acl_rules++;

	return err;
}

static struct dpaa2_switch_acl_entry *
dpaa2_switch_acl_tbl_find_entry_by_cookie(struct dpaa2_switch_filter_block *block,
					  unsigned long cookie)
{
	struct dpaa2_switch_acl_entry *tmp, *n;

	list_for_each_entry_safe(tmp, n, &block->acl_entries, list) {
		if (tmp->cookie == cookie)
			return tmp;
	}
	return NULL;
}

static int
dpaa2_switch_acl_entry_get_index(struct dpaa2_switch_filter_block *block,
				 struct dpaa2_switch_acl_entry *entry)
{
	struct dpaa2_switch_acl_entry *tmp, *n;
	int index = 0;

	list_for_each_entry_safe(tmp, n, &block->acl_entries, list) {
		if (tmp->cookie == entry->cookie)
			return index;
		index++;
	}
	return -ENOENT;
}

static struct dpaa2_switch_mirror_entry *
dpaa2_switch_mirror_find_entry_by_cookie(struct dpaa2_switch_filter_block *block,
					 unsigned long cookie)
{
	struct dpaa2_switch_mirror_entry *tmp, *n;

	list_for_each_entry_safe(tmp, n, &block->mirror_entries, list) {
		if (tmp->cookie == cookie)
			return tmp;
	}
	return NULL;
}

static int
dpaa2_switch_acl_tbl_remove_entry(struct dpaa2_switch_filter_block *block,
				  struct dpaa2_switch_acl_entry *entry)
{
	struct dpaa2_switch_acl_entry *tmp;
	int index, i, precedence, err;

	index = dpaa2_switch_acl_entry_get_index(block, entry);

	/* Remove from hardware the ACL entry */
	err = dpaa2_switch_acl_entry_remove(block, entry);
	if (err)
		return err;

	block->num_acl_rules--;

	/* Remove it from the list also */
	list_del(&entry->list);

	/* Move down in priority the entries over the deleted one */
	precedence = entry->cfg.precedence;
	for (i = index - 1; i >= 0; i--) {
		tmp = dpaa2_switch_acl_entry_get_by_index(block, i);
		err = dpaa2_switch_acl_entry_set_precedence(block, tmp,
							    precedence);
		if (err)
			return err;

		precedence--;
	}

	kfree(entry);

	return 0;
}

static int dpaa2_switch_tc_parse_action_acl(struct ethsw_core *ethsw,
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

static int
dpaa2_switch_block_add_mirror(struct dpaa2_switch_filter_block *block,
			      struct dpaa2_switch_mirror_entry *entry,
			      u16 to, struct netlink_ext_ack *extack)
{
	unsigned long block_ports = block->ports;
	struct ethsw_core *ethsw = block->ethsw;
	struct ethsw_port_priv *port_priv;
	unsigned long ports_added = 0;
	u16 vlan = entry->cfg.vlan_id;
	bool mirror_port_enabled;
	int err, port;

	/* Setup the mirroring port */
	mirror_port_enabled = (ethsw->mirror_port != ethsw->sw_attr.num_ifs);
	if (!mirror_port_enabled) {
		err = dpsw_set_reflection_if(ethsw->mc_io, 0,
					     ethsw->dpsw_handle, to);
		if (err)
			return err;
		ethsw->mirror_port = to;
	}

	/* Setup the same egress mirroring configuration on all the switch
	 * ports that share the same filter block.
	 */
	for_each_set_bit(port, &block_ports, ethsw->sw_attr.num_ifs) {
		port_priv = ethsw->ports[port];

		/* We cannot add a per VLAN mirroring rule if the VLAN in
		 * question is not installed on the switch port.
		 */
		if (entry->cfg.filter == DPSW_REFLECTION_FILTER_INGRESS_VLAN &&
		    !(port_priv->vlans[vlan] & ETHSW_VLAN_MEMBER)) {
			NL_SET_ERR_MSG(extack,
				       "VLAN must be installed on the switch port");
			err = -EINVAL;
			goto err_remove_filters;
		}

		err = dpsw_if_add_reflection(ethsw->mc_io, 0,
					     ethsw->dpsw_handle,
					     port, &entry->cfg);
		if (err)
			goto err_remove_filters;

		ports_added |= BIT(port);
	}

	list_add(&entry->list, &block->mirror_entries);

	return 0;

err_remove_filters:
	for_each_set_bit(port, &ports_added, ethsw->sw_attr.num_ifs) {
		dpsw_if_remove_reflection(ethsw->mc_io, 0, ethsw->dpsw_handle,
					  port, &entry->cfg);
	}

	if (!mirror_port_enabled)
		ethsw->mirror_port = ethsw->sw_attr.num_ifs;

	return err;
}

static int
dpaa2_switch_block_remove_mirror(struct dpaa2_switch_filter_block *block,
				 struct dpaa2_switch_mirror_entry *entry)
{
	struct dpsw_reflection_cfg *cfg = &entry->cfg;
	unsigned long block_ports = block->ports;
	struct ethsw_core *ethsw = block->ethsw;
	int port;

	/* Remove this mirroring configuration from all the ports belonging to
	 * the filter block.
	 */
	for_each_set_bit(port, &block_ports, ethsw->sw_attr.num_ifs)
		dpsw_if_remove_reflection(ethsw->mc_io, 0, ethsw->dpsw_handle,
					  port, cfg);

	/* Also remove it from the list of mirror filters */
	list_del(&entry->list);
	kfree(entry);

	/* If this was the last mirror filter, then unset the mirror port */
	if (list_empty(&block->mirror_entries))
		ethsw->mirror_port =  ethsw->sw_attr.num_ifs;

	return 0;
}

static int
dpaa2_switch_cls_flower_replace_acl(struct dpaa2_switch_filter_block *block,
				    struct flow_cls_offload *cls)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct netlink_ext_ack *extack = cls->common.extack;
	struct dpaa2_switch_acl_entry *acl_entry;
	struct ethsw_core *ethsw = block->ethsw;
	struct flow_action_entry *act;
	int err;

	if (dpaa2_switch_acl_tbl_is_full(block)) {
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
	err = dpaa2_switch_tc_parse_action_acl(ethsw, act,
					       &acl_entry->cfg.result, extack);
	if (err)
		goto free_acl_entry;

	acl_entry->prio = cls->common.prio;
	acl_entry->cookie = cls->cookie;

	err = dpaa2_switch_acl_tbl_add_entry(block, acl_entry);
	if (err)
		goto free_acl_entry;

	return 0;

free_acl_entry:
	kfree(acl_entry);

	return err;
}

static int dpaa2_switch_flower_parse_mirror_key(struct flow_cls_offload *cls,
						u16 *vlan)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct flow_dissector *dissector = rule->match.dissector;
	struct netlink_ext_ack *extack = cls->common.extack;

	if (dissector->used_keys &
	    ~(BIT(FLOW_DISSECTOR_KEY_BASIC) |
	      BIT(FLOW_DISSECTOR_KEY_CONTROL) |
	      BIT(FLOW_DISSECTOR_KEY_VLAN))) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Mirroring is supported only per VLAN");
		return -EOPNOTSUPP;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_match_vlan match;

		flow_rule_match_vlan(rule, &match);

		if (match.mask->vlan_priority != 0 ||
		    match.mask->vlan_dei != 0) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Only matching on VLAN ID supported");
			return -EOPNOTSUPP;
		}

		if (match.mask->vlan_id != 0xFFF) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Masked matching not supported");
			return -EOPNOTSUPP;
		}

		*vlan = (u16)match.key->vlan_id;
	}

	return 0;
}

static int
dpaa2_switch_cls_flower_replace_mirror(struct dpaa2_switch_filter_block *block,
				       struct flow_cls_offload *cls)
{
	struct netlink_ext_ack *extack = cls->common.extack;
	struct dpaa2_switch_mirror_entry *mirror_entry;
	struct ethsw_core *ethsw = block->ethsw;
	struct dpaa2_switch_mirror_entry *tmp;
	struct flow_action_entry *cls_act;
	struct list_head *pos, *n;
	bool mirror_port_enabled;
	u16 if_id, vlan;
	int err;

	mirror_port_enabled = (ethsw->mirror_port != ethsw->sw_attr.num_ifs);
	cls_act = &cls->rule->action.entries[0];

	/* Offload rules only when the destination is a DPAA2 switch port */
	if (!dpaa2_switch_port_dev_check(cls_act->dev)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Destination not a DPAA2 switch port");
		return -EOPNOTSUPP;
	}
	if_id = dpaa2_switch_get_index(ethsw, cls_act->dev);

	/* We have a single mirror port but can configure egress mirroring on
	 * all the other switch ports. We need to allow mirroring rules only
	 * when the destination port is the same.
	 */
	if (mirror_port_enabled && ethsw->mirror_port != if_id) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Multiple mirror ports not supported");
		return -EBUSY;
	}

	/* Parse the key */
	err = dpaa2_switch_flower_parse_mirror_key(cls, &vlan);
	if (err)
		return err;

	/* Make sure that we don't already have a mirror rule with the same
	 * configuration.
	 */
	list_for_each_safe(pos, n, &block->mirror_entries) {
		tmp = list_entry(pos, struct dpaa2_switch_mirror_entry, list);

		if (tmp->cfg.filter == DPSW_REFLECTION_FILTER_INGRESS_VLAN &&
		    tmp->cfg.vlan_id == vlan) {
			NL_SET_ERR_MSG_MOD(extack,
					   "VLAN mirror filter already installed");
			return -EBUSY;
		}
	}

	mirror_entry = kzalloc(sizeof(*mirror_entry), GFP_KERNEL);
	if (!mirror_entry)
		return -ENOMEM;

	mirror_entry->cfg.filter = DPSW_REFLECTION_FILTER_INGRESS_VLAN;
	mirror_entry->cfg.vlan_id = vlan;
	mirror_entry->cookie = cls->cookie;

	return dpaa2_switch_block_add_mirror(block, mirror_entry, if_id,
					     extack);
}

int dpaa2_switch_cls_flower_replace(struct dpaa2_switch_filter_block *block,
				    struct flow_cls_offload *cls)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct netlink_ext_ack *extack = cls->common.extack;
	struct flow_action_entry *act;

	if (!flow_offload_has_one_action(&rule->action)) {
		NL_SET_ERR_MSG(extack, "Only singular actions are supported");
		return -EOPNOTSUPP;
	}

	act = &rule->action.entries[0];
	switch (act->id) {
	case FLOW_ACTION_REDIRECT:
	case FLOW_ACTION_TRAP:
	case FLOW_ACTION_DROP:
		return dpaa2_switch_cls_flower_replace_acl(block, cls);
	case FLOW_ACTION_MIRRED:
		return dpaa2_switch_cls_flower_replace_mirror(block, cls);
	default:
		NL_SET_ERR_MSG_MOD(extack, "Action not supported");
		return -EOPNOTSUPP;
	}
}

int dpaa2_switch_cls_flower_destroy(struct dpaa2_switch_filter_block *block,
				    struct flow_cls_offload *cls)
{
	struct dpaa2_switch_mirror_entry *mirror_entry;
	struct dpaa2_switch_acl_entry *acl_entry;

	/* If this filter is a an ACL one, remove it */
	acl_entry = dpaa2_switch_acl_tbl_find_entry_by_cookie(block,
							      cls->cookie);
	if (acl_entry)
		return dpaa2_switch_acl_tbl_remove_entry(block, acl_entry);

	/* If not, then it has to be a mirror */
	mirror_entry = dpaa2_switch_mirror_find_entry_by_cookie(block,
								cls->cookie);
	if (mirror_entry)
		return dpaa2_switch_block_remove_mirror(block,
							mirror_entry);

	return 0;
}

static int
dpaa2_switch_cls_matchall_replace_acl(struct dpaa2_switch_filter_block *block,
				      struct tc_cls_matchall_offload *cls)
{
	struct netlink_ext_ack *extack = cls->common.extack;
	struct ethsw_core *ethsw = block->ethsw;
	struct dpaa2_switch_acl_entry *acl_entry;
	struct flow_action_entry *act;
	int err;

	if (dpaa2_switch_acl_tbl_is_full(block)) {
		NL_SET_ERR_MSG(extack, "Maximum filter capacity reached");
		return -ENOMEM;
	}

	acl_entry = kzalloc(sizeof(*acl_entry), GFP_KERNEL);
	if (!acl_entry)
		return -ENOMEM;

	act = &cls->rule->action.entries[0];
	err = dpaa2_switch_tc_parse_action_acl(ethsw, act,
					       &acl_entry->cfg.result, extack);
	if (err)
		goto free_acl_entry;

	acl_entry->prio = cls->common.prio;
	acl_entry->cookie = cls->cookie;

	err = dpaa2_switch_acl_tbl_add_entry(block, acl_entry);
	if (err)
		goto free_acl_entry;

	return 0;

free_acl_entry:
	kfree(acl_entry);

	return err;
}

static int
dpaa2_switch_cls_matchall_replace_mirror(struct dpaa2_switch_filter_block *block,
					 struct tc_cls_matchall_offload *cls)
{
	struct netlink_ext_ack *extack = cls->common.extack;
	struct dpaa2_switch_mirror_entry *mirror_entry;
	struct ethsw_core *ethsw = block->ethsw;
	struct dpaa2_switch_mirror_entry *tmp;
	struct flow_action_entry *cls_act;
	struct list_head *pos, *n;
	bool mirror_port_enabled;
	u16 if_id;

	mirror_port_enabled = (ethsw->mirror_port != ethsw->sw_attr.num_ifs);
	cls_act = &cls->rule->action.entries[0];

	/* Offload rules only when the destination is a DPAA2 switch port */
	if (!dpaa2_switch_port_dev_check(cls_act->dev)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Destination not a DPAA2 switch port");
		return -EOPNOTSUPP;
	}
	if_id = dpaa2_switch_get_index(ethsw, cls_act->dev);

	/* We have a single mirror port but can configure egress mirroring on
	 * all the other switch ports. We need to allow mirroring rules only
	 * when the destination port is the same.
	 */
	if (mirror_port_enabled && ethsw->mirror_port != if_id) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Multiple mirror ports not supported");
		return -EBUSY;
	}

	/* Make sure that we don't already have a mirror rule with the same
	 * configuration. One matchall rule per block is the maximum.
	 */
	list_for_each_safe(pos, n, &block->mirror_entries) {
		tmp = list_entry(pos, struct dpaa2_switch_mirror_entry, list);

		if (tmp->cfg.filter == DPSW_REFLECTION_FILTER_INGRESS_ALL) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Matchall mirror filter already installed");
			return -EBUSY;
		}
	}

	mirror_entry = kzalloc(sizeof(*mirror_entry), GFP_KERNEL);
	if (!mirror_entry)
		return -ENOMEM;

	mirror_entry->cfg.filter = DPSW_REFLECTION_FILTER_INGRESS_ALL;
	mirror_entry->cookie = cls->cookie;

	return dpaa2_switch_block_add_mirror(block, mirror_entry, if_id,
					     extack);
}

int dpaa2_switch_cls_matchall_replace(struct dpaa2_switch_filter_block *block,
				      struct tc_cls_matchall_offload *cls)
{
	struct netlink_ext_ack *extack = cls->common.extack;
	struct flow_action_entry *act;

	if (!flow_offload_has_one_action(&cls->rule->action)) {
		NL_SET_ERR_MSG(extack, "Only singular actions are supported");
		return -EOPNOTSUPP;
	}

	act = &cls->rule->action.entries[0];
	switch (act->id) {
	case FLOW_ACTION_REDIRECT:
	case FLOW_ACTION_TRAP:
	case FLOW_ACTION_DROP:
		return dpaa2_switch_cls_matchall_replace_acl(block, cls);
	case FLOW_ACTION_MIRRED:
		return dpaa2_switch_cls_matchall_replace_mirror(block, cls);
	default:
		NL_SET_ERR_MSG_MOD(extack, "Action not supported");
		return -EOPNOTSUPP;
	}
}

int dpaa2_switch_block_offload_mirror(struct dpaa2_switch_filter_block *block,
				      struct ethsw_port_priv *port_priv)
{
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct dpaa2_switch_mirror_entry *tmp;
	int err;

	list_for_each_entry(tmp, &block->mirror_entries, list) {
		err = dpsw_if_add_reflection(ethsw->mc_io, 0,
					     ethsw->dpsw_handle,
					     port_priv->idx, &tmp->cfg);
		if (err)
			goto unwind_add;
	}

	return 0;

unwind_add:
	list_for_each_entry(tmp, &block->mirror_entries, list)
		dpsw_if_remove_reflection(ethsw->mc_io, 0,
					  ethsw->dpsw_handle,
					  port_priv->idx, &tmp->cfg);

	return err;
}

int dpaa2_switch_block_unoffload_mirror(struct dpaa2_switch_filter_block *block,
					struct ethsw_port_priv *port_priv)
{
	struct ethsw_core *ethsw = port_priv->ethsw_data;
	struct dpaa2_switch_mirror_entry *tmp;
	int err;

	list_for_each_entry(tmp, &block->mirror_entries, list) {
		err = dpsw_if_remove_reflection(ethsw->mc_io, 0,
						ethsw->dpsw_handle,
						port_priv->idx, &tmp->cfg);
		if (err)
			goto unwind_remove;
	}

	return 0;

unwind_remove:
	list_for_each_entry(tmp, &block->mirror_entries, list)
		dpsw_if_add_reflection(ethsw->mc_io, 0, ethsw->dpsw_handle,
				       port_priv->idx, &tmp->cfg);

	return err;
}

int dpaa2_switch_cls_matchall_destroy(struct dpaa2_switch_filter_block *block,
				      struct tc_cls_matchall_offload *cls)
{
	struct dpaa2_switch_mirror_entry *mirror_entry;
	struct dpaa2_switch_acl_entry *acl_entry;

	/* If this filter is a an ACL one, remove it */
	acl_entry = dpaa2_switch_acl_tbl_find_entry_by_cookie(block,
							      cls->cookie);
	if (acl_entry)
		return dpaa2_switch_acl_tbl_remove_entry(block,
							 acl_entry);

	/* If not, then it has to be a mirror */
	mirror_entry = dpaa2_switch_mirror_find_entry_by_cookie(block,
								cls->cookie);
	if (mirror_entry)
		return dpaa2_switch_block_remove_mirror(block,
							mirror_entry);

	return 0;
}
