/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2020-2021 Marvell International Ltd. All rights reserved. */

#ifndef _PRESTERA_ACL_H_
#define _PRESTERA_ACL_H_

#include <linux/types.h>
#include "prestera_counter.h"

#define PRESTERA_ACL_KEYMASK_PCL_ID		0x3FF
#define PRESTERA_ACL_KEYMASK_PCL_ID_USER			\
	(PRESTERA_ACL_KEYMASK_PCL_ID & 0x00FF)
#define PRESTERA_ACL_KEYMASK_PCL_ID_CHAIN			\
	(PRESTERA_ACL_KEYMASK_PCL_ID & 0xFF00)
#define PRESTERA_ACL_CHAIN_MASK					\
	(PRESTERA_ACL_KEYMASK_PCL_ID >> 8)

#define PRESTERA_ACL_PCL_ID_MAKE(uid, chain_id)			\
	(((uid) & PRESTERA_ACL_KEYMASK_PCL_ID_USER) |		\
	(((chain_id) << 8) & PRESTERA_ACL_KEYMASK_PCL_ID_CHAIN))

#define rule_match_set_n(match_p, type, val_p, size)		\
	memcpy(&(match_p)[PRESTERA_ACL_RULE_MATCH_TYPE_##type],	\
	       val_p, size)
#define rule_match_set(match_p, type, val)			\
	memcpy(&(match_p)[PRESTERA_ACL_RULE_MATCH_TYPE_##type],	\
	       &(val), sizeof(val))

enum prestera_acl_match_type {
	PRESTERA_ACL_RULE_MATCH_TYPE_PCL_ID,
	PRESTERA_ACL_RULE_MATCH_TYPE_ETH_TYPE,
	PRESTERA_ACL_RULE_MATCH_TYPE_ETH_DMAC_0,
	PRESTERA_ACL_RULE_MATCH_TYPE_ETH_DMAC_1,
	PRESTERA_ACL_RULE_MATCH_TYPE_ETH_SMAC_0,
	PRESTERA_ACL_RULE_MATCH_TYPE_ETH_SMAC_1,
	PRESTERA_ACL_RULE_MATCH_TYPE_IP_PROTO,
	PRESTERA_ACL_RULE_MATCH_TYPE_SYS_PORT,
	PRESTERA_ACL_RULE_MATCH_TYPE_SYS_DEV,
	PRESTERA_ACL_RULE_MATCH_TYPE_IP_SRC,
	PRESTERA_ACL_RULE_MATCH_TYPE_IP_DST,
	PRESTERA_ACL_RULE_MATCH_TYPE_L4_PORT_SRC,
	PRESTERA_ACL_RULE_MATCH_TYPE_L4_PORT_DST,
	PRESTERA_ACL_RULE_MATCH_TYPE_L4_PORT_RANGE_SRC,
	PRESTERA_ACL_RULE_MATCH_TYPE_L4_PORT_RANGE_DST,
	PRESTERA_ACL_RULE_MATCH_TYPE_VLAN_ID,
	PRESTERA_ACL_RULE_MATCH_TYPE_VLAN_TPID,
	PRESTERA_ACL_RULE_MATCH_TYPE_ICMP_TYPE,
	PRESTERA_ACL_RULE_MATCH_TYPE_ICMP_CODE,

	__PRESTERA_ACL_RULE_MATCH_TYPE_MAX
};

enum prestera_acl_rule_action {
	PRESTERA_ACL_RULE_ACTION_ACCEPT = 0,
	PRESTERA_ACL_RULE_ACTION_DROP = 1,
	PRESTERA_ACL_RULE_ACTION_TRAP = 2,
	PRESTERA_ACL_RULE_ACTION_JUMP = 5,
	PRESTERA_ACL_RULE_ACTION_COUNT = 7,
	PRESTERA_ACL_RULE_ACTION_POLICE = 8,

	PRESTERA_ACL_RULE_ACTION_MAX
};

enum {
	PRESTERA_ACL_IFACE_TYPE_PORT,
	PRESTERA_ACL_IFACE_TYPE_INDEX
};

struct prestera_acl_match {
	__be32 key[__PRESTERA_ACL_RULE_MATCH_TYPE_MAX];
	__be32 mask[__PRESTERA_ACL_RULE_MATCH_TYPE_MAX];
};

struct prestera_acl_action_jump {
	u32 index;
};

struct prestera_acl_action_police {
	u32 id;
};

struct prestera_acl_action_count {
	u32 id;
};

struct prestera_acl_rule_entry_key {
	u32 prio;
	struct prestera_acl_match match;
};

struct prestera_acl_hw_action_info {
	enum prestera_acl_rule_action id;
	union {
		struct prestera_acl_action_police police;
		struct prestera_acl_action_count count;
		struct prestera_acl_action_jump jump;
	};
};

/* This struct (arg) used only to be passed as parameter for
 * acl_rule_entry_create. Must be flat. Can contain object keys, which will be
 * resolved to object links, before saving to acl_rule_entry struct
 */
struct prestera_acl_rule_entry_arg {
	u32 vtcam_id;
	struct {
		struct {
			u8 valid:1;
		} accept, drop, trap;
		struct {
			struct prestera_acl_action_jump i;
			u8 valid:1;
		} jump;
		struct {
			u8 valid:1;
			u64 rate;
			u64 burst;
			bool ingress;
		} police;
		struct {
			u8 valid:1;
			u32 client;
		} count;
	};
};

struct prestera_acl_rule {
	struct rhash_head ht_node; /* Member of acl HT */
	struct list_head list;
	struct prestera_acl_ruleset *ruleset;
	struct prestera_acl_ruleset *jump_ruleset;
	unsigned long cookie;
	u32 chain_index;
	u32 priority;
	struct prestera_acl_rule_entry_key re_key;
	struct prestera_acl_rule_entry_arg re_arg;
	struct prestera_acl_rule_entry *re;
};

struct prestera_acl_iface {
	union {
		struct prestera_port *port;
		u32 index;
	};
	u8 type;
};

struct prestera_acl;
struct prestera_switch;
struct prestera_flow_block;

int prestera_acl_init(struct prestera_switch *sw);
void prestera_acl_fini(struct prestera_switch *sw);

struct prestera_acl_rule *
prestera_acl_rule_create(struct prestera_acl_ruleset *ruleset,
			 unsigned long cookie, u32 chain_index);
void prestera_acl_rule_priority_set(struct prestera_acl_rule *rule,
				    u32 priority);
void prestera_acl_rule_destroy(struct prestera_acl_rule *rule);
struct prestera_acl_rule *
prestera_acl_rule_lookup(struct prestera_acl_ruleset *ruleset,
			 unsigned long cookie);
int prestera_acl_rule_add(struct prestera_switch *sw,
			  struct prestera_acl_rule *rule);
void prestera_acl_rule_del(struct prestera_switch *sw,
			   struct prestera_acl_rule *rule);
int prestera_acl_rule_get_stats(struct prestera_acl *acl,
				struct prestera_acl_rule *rule,
				u64 *packets, u64 *bytes, u64 *last_use);
struct prestera_acl_rule_entry *
prestera_acl_rule_entry_find(struct prestera_acl *acl,
			     struct prestera_acl_rule_entry_key *key);
void prestera_acl_rule_entry_destroy(struct prestera_acl *acl,
				     struct prestera_acl_rule_entry *e);
struct prestera_acl_rule_entry *
prestera_acl_rule_entry_create(struct prestera_acl *acl,
			       struct prestera_acl_rule_entry_key *key,
			       struct prestera_acl_rule_entry_arg *arg);
struct prestera_acl_ruleset *
prestera_acl_ruleset_get(struct prestera_acl *acl,
			 struct prestera_flow_block *block,
			 u32 chain_index);
struct prestera_acl_ruleset *
prestera_acl_ruleset_lookup(struct prestera_acl *acl,
			    struct prestera_flow_block *block,
			    u32 chain_index);
int prestera_acl_ruleset_keymask_set(struct prestera_acl_ruleset *ruleset,
				     void *keymask);
bool prestera_acl_ruleset_is_offload(struct prestera_acl_ruleset *ruleset);
int prestera_acl_ruleset_offload(struct prestera_acl_ruleset *ruleset);
void prestera_acl_ruleset_put(struct prestera_acl_ruleset *ruleset);
int prestera_acl_ruleset_bind(struct prestera_acl_ruleset *ruleset,
			      struct prestera_port *port);
int prestera_acl_ruleset_unbind(struct prestera_acl_ruleset *ruleset,
				struct prestera_port *port);
u32 prestera_acl_ruleset_index_get(const struct prestera_acl_ruleset *ruleset);
void prestera_acl_ruleset_prio_get(struct prestera_acl_ruleset *ruleset,
				   u32 *prio_min, u32 *prio_max);
void
prestera_acl_rule_keymask_pcl_id_set(struct prestera_acl_rule *rule,
				     u16 pcl_id);

int prestera_acl_vtcam_id_get(struct prestera_acl *acl, u8 lookup, u8 dir,
			      void *keymask, u32 *vtcam_id);
int prestera_acl_vtcam_id_put(struct prestera_acl *acl, u32 vtcam_id);
int prestera_acl_chain_to_client(u32 chain_index, bool ingress, u32 *client);

#endif /* _PRESTERA_ACL_H_ */
