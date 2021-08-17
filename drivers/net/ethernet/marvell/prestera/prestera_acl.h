/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2020 Marvell International Ltd. All rights reserved. */

#ifndef _PRESTERA_ACL_H_
#define _PRESTERA_ACL_H_

enum prestera_acl_rule_match_entry_type {
	PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_ETH_TYPE = 1,
	PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_ETH_DMAC,
	PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_ETH_SMAC,
	PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_IP_PROTO,
	PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_PORT,
	PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_IP_SRC,
	PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_IP_DST,
	PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_L4_PORT_SRC,
	PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_L4_PORT_DST,
	PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_L4_PORT_RANGE_SRC,
	PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_L4_PORT_RANGE_DST,
	PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_VLAN_ID,
	PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_VLAN_TPID,
	PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_ICMP_TYPE,
	PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_ICMP_CODE
};

enum prestera_acl_rule_action {
	PRESTERA_ACL_RULE_ACTION_ACCEPT,
	PRESTERA_ACL_RULE_ACTION_DROP,
	PRESTERA_ACL_RULE_ACTION_TRAP
};

struct prestera_switch;
struct prestera_port;
struct prestera_acl_rule;
struct prestera_acl_ruleset;

struct prestera_flow_block_binding {
	struct list_head list;
	struct prestera_port *port;
	int span_id;
};

struct prestera_flow_block {
	struct list_head binding_list;
	struct prestera_switch *sw;
	struct net *net;
	struct prestera_acl_ruleset *ruleset;
	struct flow_block_cb *block_cb;
};

struct prestera_acl_rule_action_entry {
	struct list_head list;
	enum prestera_acl_rule_action id;
};

struct prestera_acl_rule_match_entry {
	struct list_head list;
	enum prestera_acl_rule_match_entry_type type;
	union {
		struct {
			u8 key;
			u8 mask;
		} u8;
		struct {
			u16 key;
			u16 mask;
		} u16;
		struct {
			u32 key;
			u32 mask;
		} u32;
		struct {
			u64 key;
			u64 mask;
		} u64;
		struct {
			u8 key[ETH_ALEN];
			u8 mask[ETH_ALEN];
		} mac;
	} keymask;
};

int prestera_acl_init(struct prestera_switch *sw);
void prestera_acl_fini(struct prestera_switch *sw);
struct prestera_flow_block *
prestera_acl_block_create(struct prestera_switch *sw, struct net *net);
void prestera_acl_block_destroy(struct prestera_flow_block *block);
struct net *prestera_acl_block_net(struct prestera_flow_block *block);
struct prestera_switch *prestera_acl_block_sw(struct prestera_flow_block *block);
int prestera_acl_block_bind(struct prestera_flow_block *block,
			    struct prestera_port *port);
int prestera_acl_block_unbind(struct prestera_flow_block *block,
			      struct prestera_port *port);
struct prestera_acl_ruleset *
prestera_acl_block_ruleset_get(struct prestera_flow_block *block);
struct prestera_acl_rule *
prestera_acl_rule_create(struct prestera_flow_block *block,
			 unsigned long cookie);
u32 prestera_acl_rule_priority_get(struct prestera_acl_rule *rule);
void prestera_acl_rule_priority_set(struct prestera_acl_rule *rule,
				    u32 priority);
u16 prestera_acl_rule_ruleset_id_get(const struct prestera_acl_rule *rule);
struct list_head *
prestera_acl_rule_action_list_get(struct prestera_acl_rule *rule);
u8 prestera_acl_rule_action_len(struct prestera_acl_rule *rule);
u8 prestera_acl_rule_match_len(struct prestera_acl_rule *rule);
int prestera_acl_rule_action_add(struct prestera_acl_rule *rule,
				 struct prestera_acl_rule_action_entry *entry);
struct list_head *
prestera_acl_rule_match_list_get(struct prestera_acl_rule *rule);
int prestera_acl_rule_match_add(struct prestera_acl_rule *rule,
				struct prestera_acl_rule_match_entry *entry);
void prestera_acl_rule_destroy(struct prestera_acl_rule *rule);
struct prestera_acl_rule *
prestera_acl_rule_lookup(struct prestera_acl_ruleset *ruleset,
			 unsigned long cookie);
int prestera_acl_rule_add(struct prestera_switch *sw,
			  struct prestera_acl_rule *rule);
void prestera_acl_rule_del(struct prestera_switch *sw,
			   struct prestera_acl_rule *rule);
int prestera_acl_rule_get_stats(struct prestera_switch *sw,
				struct prestera_acl_rule *rule,
				u64 *packets, u64 *bytes, u64 *last_use);

#endif /* _PRESTERA_ACL_H_ */
