// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022 Gerhard Engleder <gerhard@engleder-embedded.com> */

#include "tsnep.h"

#define ETHER_TYPE_FULL_MASK ((__force __be16)~0)

static void tsnep_enable_rule(struct tsnep_adapter *adapter,
			      struct tsnep_rxnfc_rule *rule)
{
	u8 rx_assign;
	void __iomem *addr;

	rx_assign = TSNEP_RX_ASSIGN_ACTIVE;
	rx_assign |= (rule->queue_index << TSNEP_RX_ASSIGN_QUEUE_SHIFT) &
		     TSNEP_RX_ASSIGN_QUEUE_MASK;

	addr = adapter->addr + TSNEP_RX_ASSIGN_ETHER_TYPE +
	       TSNEP_RX_ASSIGN_ETHER_TYPE_OFFSET * rule->location;
	iowrite16(rule->filter.ether_type, addr);

	/* enable rule after all settings are done */
	addr = adapter->addr + TSNEP_RX_ASSIGN +
	       TSNEP_RX_ASSIGN_OFFSET * rule->location;
	iowrite8(rx_assign, addr);
}

static void tsnep_disable_rule(struct tsnep_adapter *adapter,
			       struct tsnep_rxnfc_rule *rule)
{
	void __iomem *addr;

	addr = adapter->addr + TSNEP_RX_ASSIGN +
	       TSNEP_RX_ASSIGN_OFFSET * rule->location;
	iowrite8(0, addr);
}

static struct tsnep_rxnfc_rule *tsnep_get_rule(struct tsnep_adapter *adapter,
					       int location)
{
	struct tsnep_rxnfc_rule *rule;

	list_for_each_entry(rule, &adapter->rxnfc_rules, list) {
		if (rule->location == location)
			return rule;
		if (rule->location > location)
			break;
	}

	return NULL;
}

static void tsnep_add_rule(struct tsnep_adapter *adapter,
			   struct tsnep_rxnfc_rule *rule)
{
	struct tsnep_rxnfc_rule *pred, *cur;

	tsnep_enable_rule(adapter, rule);

	pred = NULL;
	list_for_each_entry(cur, &adapter->rxnfc_rules, list) {
		if (cur->location >= rule->location)
			break;
		pred = cur;
	}

	list_add(&rule->list, pred ? &pred->list : &adapter->rxnfc_rules);
	adapter->rxnfc_count++;
}

static void tsnep_delete_rule(struct tsnep_adapter *adapter,
			      struct tsnep_rxnfc_rule *rule)
{
	tsnep_disable_rule(adapter, rule);

	list_del(&rule->list);
	adapter->rxnfc_count--;

	kfree(rule);
}

static void tsnep_flush_rules(struct tsnep_adapter *adapter)
{
	struct tsnep_rxnfc_rule *rule, *tmp;

	mutex_lock(&adapter->rxnfc_lock);

	list_for_each_entry_safe(rule, tmp, &adapter->rxnfc_rules, list)
		tsnep_delete_rule(adapter, rule);

	mutex_unlock(&adapter->rxnfc_lock);
}

int tsnep_rxnfc_get_rule(struct tsnep_adapter *adapter,
			 struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp = &cmd->fs;
	struct tsnep_rxnfc_rule *rule = NULL;

	cmd->data = adapter->rxnfc_max;

	mutex_lock(&adapter->rxnfc_lock);

	rule = tsnep_get_rule(adapter, fsp->location);
	if (!rule) {
		mutex_unlock(&adapter->rxnfc_lock);

		return -ENOENT;
	}

	fsp->flow_type = ETHER_FLOW;
	fsp->ring_cookie = rule->queue_index;

	if (rule->filter.type == TSNEP_RXNFC_ETHER_TYPE) {
		fsp->h_u.ether_spec.h_proto = htons(rule->filter.ether_type);
		fsp->m_u.ether_spec.h_proto = ETHER_TYPE_FULL_MASK;
	}

	mutex_unlock(&adapter->rxnfc_lock);

	return 0;
}

int tsnep_rxnfc_get_all(struct tsnep_adapter *adapter,
			struct ethtool_rxnfc *cmd,
			u32 *rule_locs)
{
	struct tsnep_rxnfc_rule *rule;
	int count = 0;

	cmd->data = adapter->rxnfc_max;

	mutex_lock(&adapter->rxnfc_lock);

	list_for_each_entry(rule, &adapter->rxnfc_rules, list) {
		if (count == cmd->rule_cnt) {
			mutex_unlock(&adapter->rxnfc_lock);

			return -EMSGSIZE;
		}

		rule_locs[count] = rule->location;
		count++;
	}

	mutex_unlock(&adapter->rxnfc_lock);

	cmd->rule_cnt = count;

	return 0;
}

static int tsnep_rxnfc_find_location(struct tsnep_adapter *adapter)
{
	struct tsnep_rxnfc_rule *tmp;
	int location = 0;

	list_for_each_entry(tmp, &adapter->rxnfc_rules, list) {
		if (tmp->location == location)
			location++;
		else
			return location;
	}

	if (location >= adapter->rxnfc_max)
		return -ENOSPC;

	return location;
}

static void tsnep_rxnfc_init_rule(struct tsnep_rxnfc_rule *rule,
				  const struct ethtool_rx_flow_spec *fsp)
{
	INIT_LIST_HEAD(&rule->list);

	rule->queue_index = fsp->ring_cookie;
	rule->location = fsp->location;

	rule->filter.type = TSNEP_RXNFC_ETHER_TYPE;
	rule->filter.ether_type = ntohs(fsp->h_u.ether_spec.h_proto);
}

static int tsnep_rxnfc_check_rule(struct tsnep_adapter *adapter,
				  struct tsnep_rxnfc_rule *rule)
{
	struct net_device *dev = adapter->netdev;
	struct tsnep_rxnfc_rule *tmp;

	list_for_each_entry(tmp, &adapter->rxnfc_rules, list) {
		if (!memcmp(&rule->filter, &tmp->filter, sizeof(rule->filter)) &&
		    tmp->location != rule->location) {
			netdev_dbg(dev, "rule already exists\n");

			return -EEXIST;
		}
	}

	return 0;
}

int tsnep_rxnfc_add_rule(struct tsnep_adapter *adapter,
			 struct ethtool_rxnfc *cmd)
{
	struct net_device *netdev = adapter->netdev;
	struct ethtool_rx_flow_spec *fsp =
		(struct ethtool_rx_flow_spec *)&cmd->fs;
	struct tsnep_rxnfc_rule *rule, *old_rule;
	int retval;

	/* only EtherType is supported */
	if (fsp->flow_type != ETHER_FLOW ||
	    !is_zero_ether_addr(fsp->m_u.ether_spec.h_dest) ||
	    !is_zero_ether_addr(fsp->m_u.ether_spec.h_source) ||
	    fsp->m_u.ether_spec.h_proto != ETHER_TYPE_FULL_MASK) {
		netdev_dbg(netdev, "only ethernet protocol is supported\n");

		return -EOPNOTSUPP;
	}

	if (fsp->ring_cookie >
	    (TSNEP_RX_ASSIGN_QUEUE_MASK >> TSNEP_RX_ASSIGN_QUEUE_SHIFT)) {
		netdev_dbg(netdev, "invalid action\n");

		return -EINVAL;
	}

	if (fsp->location != RX_CLS_LOC_ANY &&
	    fsp->location >= adapter->rxnfc_max) {
		netdev_dbg(netdev, "invalid location\n");

		return -EINVAL;
	}

	rule = kzalloc(sizeof(*rule), GFP_KERNEL);
	if (!rule)
		return -ENOMEM;

	mutex_lock(&adapter->rxnfc_lock);

	if (fsp->location == RX_CLS_LOC_ANY) {
		retval = tsnep_rxnfc_find_location(adapter);
		if (retval < 0)
			goto failed;
		fsp->location = retval;
	}

	tsnep_rxnfc_init_rule(rule, fsp);

	retval = tsnep_rxnfc_check_rule(adapter, rule);
	if (retval)
		goto failed;

	old_rule = tsnep_get_rule(adapter, fsp->location);
	if (old_rule)
		tsnep_delete_rule(adapter, old_rule);

	tsnep_add_rule(adapter, rule);

	mutex_unlock(&adapter->rxnfc_lock);

	return 0;

failed:
	mutex_unlock(&adapter->rxnfc_lock);
	kfree(rule);
	return retval;
}

int tsnep_rxnfc_del_rule(struct tsnep_adapter *adapter,
			 struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp =
		(struct ethtool_rx_flow_spec *)&cmd->fs;
	struct tsnep_rxnfc_rule *rule;

	mutex_lock(&adapter->rxnfc_lock);

	rule = tsnep_get_rule(adapter, fsp->location);
	if (!rule) {
		mutex_unlock(&adapter->rxnfc_lock);

		return -ENOENT;
	}

	tsnep_delete_rule(adapter, rule);

	mutex_unlock(&adapter->rxnfc_lock);

	return 0;
}

int tsnep_rxnfc_init(struct tsnep_adapter *adapter)
{
	int i;

	/* disable all rules */
	for (i = 0; i < adapter->rxnfc_max;
	     i += sizeof(u32) / TSNEP_RX_ASSIGN_OFFSET)
		iowrite32(0, adapter->addr + TSNEP_RX_ASSIGN + i);

	return 0;
}

void tsnep_rxnfc_cleanup(struct tsnep_adapter *adapter)
{
	tsnep_flush_rules(adapter);
}
