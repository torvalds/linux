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

/* Update the keyset for the rule */
int vcap_set_rule_set_keyset(struct vcap_rule *rule,
			     enum vcap_keyfield_set keyset);
/* Update the actionset for the rule */
int vcap_set_rule_set_actionset(struct vcap_rule *rule,
				enum vcap_actionfield_set actionset);

/* VCAP rule field operations */
int vcap_rule_add_key_bit(struct vcap_rule *rule, enum vcap_key_field key,
			  enum vcap_bit val);
int vcap_rule_add_key_u32(struct vcap_rule *rule, enum vcap_key_field key,
			  u32 value, u32 mask);
int vcap_rule_add_key_u48(struct vcap_rule *rule, enum vcap_key_field key,
			  struct vcap_u48_key *fieldval);
int vcap_rule_add_key_u72(struct vcap_rule *rule, enum vcap_key_field key,
			  struct vcap_u72_key *fieldval);
int vcap_rule_add_action_bit(struct vcap_rule *rule,
			     enum vcap_action_field action, enum vcap_bit val);
int vcap_rule_add_action_u32(struct vcap_rule *rule,
			     enum vcap_action_field action, u32 value);

/* VCAP lookup operations */
/* Lookup a vcap instance using chain id */
struct vcap_admin *vcap_find_admin(struct vcap_control *vctrl, int cid);
/* Find information on a key field in a rule */
const struct vcap_field *vcap_lookup_keyfield(struct vcap_rule *rule,
					      enum vcap_key_field key);
/* Find a rule id with a provided cookie */
int vcap_lookup_rule_by_cookie(struct vcap_control *vctrl, u64 cookie);

/* Copy to host byte order */
void vcap_netbytes_copy(u8 *dst, u8 *src, int count);

/* Convert validation error code into tc extact error message */
void vcap_set_tc_exterr(struct flow_cls_offload *fco, struct vcap_rule *vrule);

/* Cleanup a VCAP instance */
int vcap_del_rules(struct vcap_control *vctrl, struct vcap_admin *admin);

#endif /* __VCAP_API_CLIENT__ */
