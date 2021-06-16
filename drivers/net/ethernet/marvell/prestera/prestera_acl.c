// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2020 Marvell International Ltd. All rights reserved */

#include <linux/rhashtable.h>

#include "prestera.h"
#include "prestera_hw.h"
#include "prestera_acl.h"
#include "prestera_span.h"

struct prestera_acl {
	struct prestera_switch *sw;
	struct list_head rules;
};

struct prestera_acl_ruleset {
	struct rhashtable rule_ht;
	struct prestera_switch *sw;
	u16 id;
};

struct prestera_acl_rule {
	struct rhash_head ht_node;
	struct list_head list;
	struct list_head match_list;
	struct list_head action_list;
	struct prestera_flow_block *block;
	unsigned long cookie;
	u32 priority;
	u8 n_actions;
	u8 n_matches;
	u32 id;
};

static const struct rhashtable_params prestera_acl_rule_ht_params = {
	.key_len = sizeof(unsigned long),
	.key_offset = offsetof(struct prestera_acl_rule, cookie),
	.head_offset = offsetof(struct prestera_acl_rule, ht_node),
	.automatic_shrinking = true,
};

static struct prestera_acl_ruleset *
prestera_acl_ruleset_create(struct prestera_switch *sw)
{
	struct prestera_acl_ruleset *ruleset;
	int err;

	ruleset = kzalloc(sizeof(*ruleset), GFP_KERNEL);
	if (!ruleset)
		return ERR_PTR(-ENOMEM);

	err = rhashtable_init(&ruleset->rule_ht, &prestera_acl_rule_ht_params);
	if (err)
		goto err_rhashtable_init;

	err = prestera_hw_acl_ruleset_create(sw, &ruleset->id);
	if (err)
		goto err_ruleset_create;

	ruleset->sw = sw;

	return ruleset;

err_ruleset_create:
	rhashtable_destroy(&ruleset->rule_ht);
err_rhashtable_init:
	kfree(ruleset);
	return ERR_PTR(err);
}

static void prestera_acl_ruleset_destroy(struct prestera_acl_ruleset *ruleset)
{
	prestera_hw_acl_ruleset_del(ruleset->sw, ruleset->id);
	rhashtable_destroy(&ruleset->rule_ht);
	kfree(ruleset);
}

struct prestera_flow_block *
prestera_acl_block_create(struct prestera_switch *sw, struct net *net)
{
	struct prestera_flow_block *block;

	block = kzalloc(sizeof(*block), GFP_KERNEL);
	if (!block)
		return NULL;
	INIT_LIST_HEAD(&block->binding_list);
	block->net = net;
	block->sw = sw;

	block->ruleset = prestera_acl_ruleset_create(sw);
	if (IS_ERR(block->ruleset)) {
		kfree(block);
		return NULL;
	}

	return block;
}

void prestera_acl_block_destroy(struct prestera_flow_block *block)
{
	prestera_acl_ruleset_destroy(block->ruleset);
	WARN_ON(!list_empty(&block->binding_list));
	kfree(block);
}

static struct prestera_flow_block_binding *
prestera_acl_block_lookup(struct prestera_flow_block *block,
			  struct prestera_port *port)
{
	struct prestera_flow_block_binding *binding;

	list_for_each_entry(binding, &block->binding_list, list)
		if (binding->port == port)
			return binding;

	return NULL;
}

int prestera_acl_block_bind(struct prestera_flow_block *block,
			    struct prestera_port *port)
{
	struct prestera_flow_block_binding *binding;
	int err;

	if (WARN_ON(prestera_acl_block_lookup(block, port)))
		return -EEXIST;

	binding = kzalloc(sizeof(*binding), GFP_KERNEL);
	if (!binding)
		return -ENOMEM;
	binding->span_id = PRESTERA_SPAN_INVALID_ID;
	binding->port = port;

	err = prestera_hw_acl_port_bind(port, block->ruleset->id);
	if (err)
		goto err_rules_bind;

	list_add(&binding->list, &block->binding_list);
	return 0;

err_rules_bind:
	kfree(binding);
	return err;
}

int prestera_acl_block_unbind(struct prestera_flow_block *block,
			      struct prestera_port *port)
{
	struct prestera_flow_block_binding *binding;

	binding = prestera_acl_block_lookup(block, port);
	if (!binding)
		return -ENOENT;

	list_del(&binding->list);

	prestera_hw_acl_port_unbind(port, block->ruleset->id);

	kfree(binding);
	return 0;
}

struct prestera_acl_ruleset *
prestera_acl_block_ruleset_get(struct prestera_flow_block *block)
{
	return block->ruleset;
}

u16 prestera_acl_rule_ruleset_id_get(const struct prestera_acl_rule *rule)
{
	return rule->block->ruleset->id;
}

struct net *prestera_acl_block_net(struct prestera_flow_block *block)
{
	return block->net;
}

struct prestera_switch *prestera_acl_block_sw(struct prestera_flow_block *block)
{
	return block->sw;
}

struct prestera_acl_rule *
prestera_acl_rule_lookup(struct prestera_acl_ruleset *ruleset,
			 unsigned long cookie)
{
	return rhashtable_lookup_fast(&ruleset->rule_ht, &cookie,
				      prestera_acl_rule_ht_params);
}

struct prestera_acl_rule *
prestera_acl_rule_create(struct prestera_flow_block *block,
			 unsigned long cookie)
{
	struct prestera_acl_rule *rule;

	rule = kzalloc(sizeof(*rule), GFP_KERNEL);
	if (!rule)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&rule->match_list);
	INIT_LIST_HEAD(&rule->action_list);
	rule->cookie = cookie;
	rule->block = block;

	return rule;
}

struct list_head *
prestera_acl_rule_match_list_get(struct prestera_acl_rule *rule)
{
	return &rule->match_list;
}

struct list_head *
prestera_acl_rule_action_list_get(struct prestera_acl_rule *rule)
{
	return &rule->action_list;
}

int prestera_acl_rule_action_add(struct prestera_acl_rule *rule,
				 struct prestera_acl_rule_action_entry *entry)
{
	struct prestera_acl_rule_action_entry *a_entry;

	a_entry = kmalloc(sizeof(*a_entry), GFP_KERNEL);
	if (!a_entry)
		return -ENOMEM;

	memcpy(a_entry, entry, sizeof(*entry));
	list_add(&a_entry->list, &rule->action_list);

	rule->n_actions++;
	return 0;
}

u8 prestera_acl_rule_action_len(struct prestera_acl_rule *rule)
{
	return rule->n_actions;
}

u32 prestera_acl_rule_priority_get(struct prestera_acl_rule *rule)
{
	return rule->priority;
}

void prestera_acl_rule_priority_set(struct prestera_acl_rule *rule,
				    u32 priority)
{
	rule->priority = priority;
}

int prestera_acl_rule_match_add(struct prestera_acl_rule *rule,
				struct prestera_acl_rule_match_entry *entry)
{
	struct prestera_acl_rule_match_entry *m_entry;

	m_entry = kmalloc(sizeof(*m_entry), GFP_KERNEL);
	if (!m_entry)
		return -ENOMEM;

	memcpy(m_entry, entry, sizeof(*entry));
	list_add(&m_entry->list, &rule->match_list);

	rule->n_matches++;
	return 0;
}

u8 prestera_acl_rule_match_len(struct prestera_acl_rule *rule)
{
	return rule->n_matches;
}

void prestera_acl_rule_destroy(struct prestera_acl_rule *rule)
{
	struct prestera_acl_rule_action_entry *a_entry;
	struct prestera_acl_rule_match_entry *m_entry;
	struct list_head *pos, *n;

	list_for_each_safe(pos, n, &rule->match_list) {
		m_entry = list_entry(pos, typeof(*m_entry), list);
		list_del(pos);
		kfree(m_entry);
	}

	list_for_each_safe(pos, n, &rule->action_list) {
		a_entry = list_entry(pos, typeof(*a_entry), list);
		list_del(pos);
		kfree(a_entry);
	}

	kfree(rule);
}

int prestera_acl_rule_add(struct prestera_switch *sw,
			  struct prestera_acl_rule *rule)
{
	u32 rule_id;
	int err;

	/* try to add rule to hash table first */
	err = rhashtable_insert_fast(&rule->block->ruleset->rule_ht,
				     &rule->ht_node,
				     prestera_acl_rule_ht_params);
	if (err)
		return err;

	/* add rule to hw */
	err = prestera_hw_acl_rule_add(sw, rule, &rule_id);
	if (err)
		goto err_rule_add;

	rule->id = rule_id;

	list_add_tail(&rule->list, &sw->acl->rules);

	return 0;

err_rule_add:
	rhashtable_remove_fast(&rule->block->ruleset->rule_ht, &rule->ht_node,
			       prestera_acl_rule_ht_params);
	return err;
}

void prestera_acl_rule_del(struct prestera_switch *sw,
			   struct prestera_acl_rule *rule)
{
	rhashtable_remove_fast(&rule->block->ruleset->rule_ht, &rule->ht_node,
			       prestera_acl_rule_ht_params);
	list_del(&rule->list);
	prestera_hw_acl_rule_del(sw, rule->id);
}

int prestera_acl_rule_get_stats(struct prestera_switch *sw,
				struct prestera_acl_rule *rule,
				u64 *packets, u64 *bytes, u64 *last_use)
{
	u64 current_packets;
	u64 current_bytes;
	int err;

	err = prestera_hw_acl_rule_stats_get(sw, rule->id, &current_packets,
					     &current_bytes);
	if (err)
		return err;

	*packets = current_packets;
	*bytes = current_bytes;
	*last_use = jiffies;

	return 0;
}

int prestera_acl_init(struct prestera_switch *sw)
{
	struct prestera_acl *acl;

	acl = kzalloc(sizeof(*acl), GFP_KERNEL);
	if (!acl)
		return -ENOMEM;

	INIT_LIST_HEAD(&acl->rules);
	sw->acl = acl;
	acl->sw = sw;

	return 0;
}

void prestera_acl_fini(struct prestera_switch *sw)
{
	struct prestera_acl *acl = sw->acl;

	WARN_ON(!list_empty(&acl->rules));
	kfree(acl);
}
