/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (C) 2022 Microchip Technology Inc. and its subsidiaries.
 * Microchip VCAP API
 */

#ifndef __VCAP_API_CLIENT__
#define __VCAP_API_CLIENT__

#include <linux/types.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <net/flow_offload.h>

#include "vcap_api.h"

/* Client supplied VCAP rule key control part */
struct vcap_client_keyfield_ctrl {
	struct list_head list;  /* For insertion into a rule */
	enum vcap_key_field key;
	enum vcap_field_type type;
};

struct vcap_u1_key {
	u8 value;
	u8 mask;
};

struct vcap_u32_key {
	u32 value;
	u32 mask;
};

struct vcap_u48_key {
	u8 value[6];
	u8 mask[6];
};

struct vcap_u56_key {
	u8 value[7];
	u8 mask[7];
};

struct vcap_u64_key {
	u8 value[8];
	u8 mask[8];
};

struct vcap_u72_key {
	u8 value[9];
	u8 mask[9];
};

struct vcap_u112_key {
	u8 value[14];
	u8 mask[14];
};

struct vcap_u128_key {
	u8 value[16];
	u8 mask[16];
};

/* Client supplied VCAP rule field data */
struct vcap_client_keyfield_data {
	union {
		struct vcap_u1_key u1;
		struct vcap_u32_key u32;
		struct vcap_u48_key u48;
		struct vcap_u56_key u56;
		struct vcap_u64_key u64;
		struct vcap_u72_key u72;
		struct vcap_u112_key u112;
		struct vcap_u128_key u128;
	};
};

/* Client supplied VCAP rule key (value, mask) */
struct vcap_client_keyfield {
	struct vcap_client_keyfield_ctrl ctrl;
	struct vcap_client_keyfield_data data;
};

/* Client supplied VCAP rule action control part */
struct vcap_client_actionfield_ctrl {
	struct list_head list;  /* For insertion into a rule */
	enum vcap_action_field action;
	enum vcap_field_type type;
};

struct vcap_u1_action {
	u8 value;
};

struct vcap_u32_action {
	u32 value;
};

struct vcap_u48_action {
	u8 value[6];
};

struct vcap_u56_action {
	u8 value[7];
};

struct vcap_u64_action {
	u8 value[8];
};

struct vcap_u72_action {
	u8 value[9];
};

struct vcap_u112_action {
	u8 value[14];
};

struct vcap_u128_action {
	u8 value[16];
};

struct vcap_client_actionfield_data {
	union {
		struct vcap_u1_action u1;
		struct vcap_u32_action u32;
		struct vcap_u48_action u48;
		struct vcap_u56_action u56;
		struct vcap_u64_action u64;
		struct vcap_u72_action u72;
		struct vcap_u112_action u112;
		struct vcap_u128_action u128;
	};
};

struct vcap_client_actionfield {
	struct vcap_client_actionfield_ctrl ctrl;
	struct vcap_client_actionfield_data data;
};

enum vcap_bit {
	VCAP_BIT_ANY,
	VCAP_BIT_0,
	VCAP_BIT_1
};

struct vcap_counter {
	u32 value;
	bool sticky;
};

/* Enable/Disable the VCAP instance lookups */
int vcap_enable_lookups(struct vcap_control *vctrl, struct net_device *ndev,
			int from_cid, int to_cid, unsigned long cookie,
			bool enable);

/* VCAP rule operations */
/* Allocate a rule and fill in the basic information */
struct vcap_rule *vcap_alloc_rule(struct vcap_control *vctrl,
				  struct net_device *ndev,
				  int vcap_chain_id,
				  enum vcap_user user,
				  u16 priority,
				  u32 id);
/* Free mem of a rule owned by client */
void vcap_free_rule(struct vcap_rule *rule);
/* Validate a rule before adding it to the VCAP */
int vcap_val_rule(struct vcap_rule *rule, u16 l3_proto);
/* Add rule to a VCAP instance */
int vcap_add_rule(struct vcap_rule *rule);
/* Delete rule in a VCAP instance */
int vcap_del_rule(struct vcap_control *vctrl, struct net_device *ndev, u32 id);
/* Make a full copy of an existing rule with a new rule id */
struct vcap_rule *vcap_copy_rule(struct vcap_rule *rule);
/* Get rule from a VCAP instance */
struct vcap_rule *vcap_get_rule(struct vcap_control *vctrl, u32 id);
/* Update existing rule */
int vcap_mod_rule(struct vcap_rule *rule);

/* Update the keyset for the rule */
int vcap_set_rule_set_keyset(struct vcap_rule *rule,
			     enum vcap_keyfield_set keyset);
/* Update the actionset for the rule */
int vcap_set_rule_set_actionset(struct vcap_rule *rule,
				enum vcap_actionfield_set actionset);
/* Set a rule counter id (for certain VCAPs only) */
void vcap_rule_set_counter_id(struct vcap_rule *rule, u32 counter_id);

/* VCAP rule field operations */
int vcap_rule_add_key_bit(struct vcap_rule *rule, enum vcap_key_field key,
			  enum vcap_bit val);
int vcap_rule_add_key_u32(struct vcap_rule *rule, enum vcap_key_field key,
			  u32 value, u32 mask);
int vcap_rule_add_key_u48(struct vcap_rule *rule, enum vcap_key_field key,
			  struct vcap_u48_key *fieldval);
int vcap_rule_add_key_u72(struct vcap_rule *rule, enum vcap_key_field key,
			  struct vcap_u72_key *fieldval);
int vcap_rule_add_key_u128(struct vcap_rule *rule, enum vcap_key_field key,
			   struct vcap_u128_key *fieldval);
int vcap_rule_add_action_bit(struct vcap_rule *rule,
			     enum vcap_action_field action, enum vcap_bit val);
int vcap_rule_add_action_u32(struct vcap_rule *rule,
			     enum vcap_action_field action, u32 value);

/* VCAP rule counter operations */
int vcap_get_rule_count_by_cookie(struct vcap_control *vctrl,
				  struct vcap_counter *ctr, u64 cookie);
int vcap_rule_set_counter(struct vcap_rule *rule, struct vcap_counter *ctr);
int vcap_rule_get_counter(struct vcap_rule *rule, struct vcap_counter *ctr);

/* VCAP lookup operations */
/* Convert a chain id to a VCAP lookup index */
int vcap_chain_id_to_lookup(struct vcap_admin *admin, int cur_cid);
/* Lookup a vcap instance using chain id */
struct vcap_admin *vcap_find_admin(struct vcap_control *vctrl, int cid);
/* Find information on a key field in a rule */
const struct vcap_field *vcap_lookup_keyfield(struct vcap_rule *rule,
					      enum vcap_key_field key);
/* Find a rule id with a provided cookie */
int vcap_lookup_rule_by_cookie(struct vcap_control *vctrl, u64 cookie);
/* Calculate the value used for chaining VCAP rules */
int vcap_chain_offset(struct vcap_control *vctrl, int from_cid, int to_cid);
/* Is the next chain id in the following lookup, possible in another VCAP */
bool vcap_is_next_lookup(struct vcap_control *vctrl, int cur_cid, int next_cid);
/* Is this chain id the last lookup of all VCAPs */
bool vcap_is_last_chain(struct vcap_control *vctrl, int cid, bool ingress);
/* Provide all rules via a callback interface */
int vcap_rule_iter(struct vcap_control *vctrl,
		   int (*callback)(void *, struct vcap_rule *), void *arg);
/* Match a list of keys against the keysets available in a vcap type */
bool vcap_rule_find_keysets(struct vcap_rule *rule,
			    struct vcap_keyset_list *matches);
/* Return the keyset information for the keyset */
const struct vcap_set *vcap_keyfieldset(struct vcap_control *vctrl,
					enum vcap_type vt,
					enum vcap_keyfield_set keyset);
/* Copy to host byte order */
void vcap_netbytes_copy(u8 *dst, u8 *src, int count);

/* Convert validation error code into tc extact error message */
void vcap_set_tc_exterr(struct flow_cls_offload *fco, struct vcap_rule *vrule);

/* Cleanup a VCAP instance */
int vcap_del_rules(struct vcap_control *vctrl, struct vcap_admin *admin);

/* Add a keyset to a keyset list */
bool vcap_keyset_list_add(struct vcap_keyset_list *keysetlist,
			  enum vcap_keyfield_set keyset);
/* Drop keys in a keylist and any keys that are not supported by the keyset */
int vcap_filter_rule_keys(struct vcap_rule *rule,
			  enum vcap_key_field keylist[], int length,
			  bool drop_unsupported);

/* map keyset id to a string with the keyset name */
const char *vcap_keyset_name(struct vcap_control *vctrl,
			     enum vcap_keyfield_set keyset);
/* map key field id to a string with the key name */
const char *vcap_keyfield_name(struct vcap_control *vctrl,
			       enum vcap_key_field key);

/* Modify a 32 bit key field with value and mask in the rule */
int vcap_rule_mod_key_u32(struct vcap_rule *rule, enum vcap_key_field key,
			  u32 value, u32 mask);
/* Modify a 32 bit action field with value in the rule */
int vcap_rule_mod_action_u32(struct vcap_rule *rule,
			     enum vcap_action_field action,
			     u32 value);

/* Get a 32 bit key field value and mask from the rule */
int vcap_rule_get_key_u32(struct vcap_rule *rule, enum vcap_key_field key,
			  u32 *value, u32 *mask);

struct vcap_client_actionfield *
vcap_find_actionfield(struct vcap_rule *rule, enum vcap_action_field act);
#endif /* __VCAP_API_CLIENT__ */
