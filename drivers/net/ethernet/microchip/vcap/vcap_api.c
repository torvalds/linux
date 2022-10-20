// SPDX-License-Identifier: GPL-2.0+
/* Microchip VCAP API
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 */

#include <linux/types.h>

#include "vcap_api.h"
#include "vcap_api_client.h"

#define to_intrule(rule) container_of((rule), struct vcap_rule_internal, data)

/* Private VCAP API rule data */
struct vcap_rule_internal {
	struct vcap_rule data; /* provided by the client */
	struct list_head list; /* for insertion in the vcap admin list of rules */
	struct vcap_admin *admin; /* vcap hw instance */
	struct net_device *ndev;  /* the interface that the rule applies to */
	struct vcap_control *vctrl; /* the client control */
	u32 addr; /* address in the VCAP at insertion */
};

/* Update the keyset for the rule */
int vcap_set_rule_set_keyset(struct vcap_rule *rule,
			     enum vcap_keyfield_set keyset)
{
	/* This will be expanded with more information later */
	rule->keyset = keyset;
	return 0;
}
EXPORT_SYMBOL_GPL(vcap_set_rule_set_keyset);

/* Update the actionset for the rule */
int vcap_set_rule_set_actionset(struct vcap_rule *rule,
				enum vcap_actionfield_set actionset)
{
	/* This will be expanded with more information later */
	rule->actionset = actionset;
	return 0;
}
EXPORT_SYMBOL_GPL(vcap_set_rule_set_actionset);

/* Find a rule with a provided rule id */
static struct vcap_rule_internal *vcap_lookup_rule(struct vcap_control *vctrl,
						   u32 id)
{
	struct vcap_rule_internal *ri;
	struct vcap_admin *admin;

	/* Look for the rule id in all vcaps */
	list_for_each_entry(admin, &vctrl->list, list)
		list_for_each_entry(ri, &admin->rules, list)
			if (ri->data.id == id)
				return ri;
	return NULL;
}

/* Find a rule id with a provided cookie */
int vcap_lookup_rule_by_cookie(struct vcap_control *vctrl, u64 cookie)
{
	struct vcap_rule_internal *ri;
	struct vcap_admin *admin;

	/* Look for the rule id in all vcaps */
	list_for_each_entry(admin, &vctrl->list, list)
		list_for_each_entry(ri, &admin->rules, list)
			if (ri->data.cookie == cookie)
				return ri->data.id;
	return -ENOENT;
}
EXPORT_SYMBOL_GPL(vcap_lookup_rule_by_cookie);

/* Lookup a vcap instance using chain id */
struct vcap_admin *vcap_find_admin(struct vcap_control *vctrl, int cid)
{
	struct vcap_admin *admin;

	list_for_each_entry(admin, &vctrl->list, list) {
		if (cid >= admin->first_cid && cid <= admin->last_cid)
			return admin;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(vcap_find_admin);

/* Validate a rule with respect to available port keys */
int vcap_val_rule(struct vcap_rule *rule, u16 l3_proto)
{
	struct vcap_rule_internal *ri = to_intrule(rule);

	/* This validation will be much expanded later */
	if (!ri->admin) {
		ri->data.exterr = VCAP_ERR_NO_ADMIN;
		return -EINVAL;
	}
	if (!ri->ndev) {
		ri->data.exterr = VCAP_ERR_NO_NETDEV;
		return -EINVAL;
	}
	if (ri->data.keyset == VCAP_KFS_NO_VALUE) {
		ri->data.exterr = VCAP_ERR_NO_KEYSET_MATCH;
		return -EINVAL;
	}
	if (ri->data.actionset == VCAP_AFS_NO_VALUE) {
		ri->data.exterr = VCAP_ERR_NO_ACTIONSET_MATCH;
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(vcap_val_rule);

/* Assign a unique rule id and autogenerate one if id == 0 */
static u32 vcap_set_rule_id(struct vcap_rule_internal *ri)
{
	u32 next_id;

	if (ri->data.id != 0)
		return ri->data.id;

	next_id = ri->vctrl->rule_id + 1;

	for (next_id = ri->vctrl->rule_id + 1; next_id < ~0; ++next_id) {
		if (!vcap_lookup_rule(ri->vctrl, next_id)) {
			ri->data.id = next_id;
			ri->vctrl->rule_id = next_id;
			break;
		}
	}
	return ri->data.id;
}

/* Encode and write a validated rule to the VCAP */
int vcap_add_rule(struct vcap_rule *rule)
{
	/* This will later handling the encode and writing of the rule */
	return 0;
}
EXPORT_SYMBOL_GPL(vcap_add_rule);

/* Allocate a new rule with the provided arguments */
struct vcap_rule *vcap_alloc_rule(struct vcap_control *vctrl,
				  struct net_device *ndev, int vcap_chain_id,
				  enum vcap_user user, u16 priority,
				  u32 id)
{
	struct vcap_rule_internal *ri;
	struct vcap_admin *admin;

	if (!ndev)
		return ERR_PTR(-ENODEV);
	/* Get the VCAP instance */
	admin = vcap_find_admin(vctrl, vcap_chain_id);
	if (!admin)
		return ERR_PTR(-ENOENT);
	/* Create a container for the rule and return it */
	ri = kzalloc(sizeof(*ri), GFP_KERNEL);
	if (!ri)
		return ERR_PTR(-ENOMEM);
	ri->data.vcap_chain_id = vcap_chain_id;
	ri->data.user = user;
	ri->data.priority = priority;
	ri->data.id = id;
	ri->data.keyset = VCAP_KFS_NO_VALUE;
	ri->data.actionset = VCAP_AFS_NO_VALUE;
	INIT_LIST_HEAD(&ri->list);
	INIT_LIST_HEAD(&ri->data.keyfields);
	INIT_LIST_HEAD(&ri->data.actionfields);
	ri->ndev = ndev;
	ri->admin = admin; /* refer to the vcap instance */
	ri->vctrl = vctrl; /* refer to the client */
	if (vcap_set_rule_id(ri) == 0)
		goto out_free;
	return (struct vcap_rule *)ri;

out_free:
	kfree(ri);
	return ERR_PTR(-EINVAL);
}
EXPORT_SYMBOL_GPL(vcap_alloc_rule);

/* Free mem of a rule owned by client after the rule as been added to the VCAP */
void vcap_free_rule(struct vcap_rule *rule)
{
	struct vcap_rule_internal *ri = to_intrule(rule);
	struct vcap_client_actionfield *caf, *next_caf;
	struct vcap_client_keyfield *ckf, *next_ckf;

	/* Deallocate the list of keys and actions */
	list_for_each_entry_safe(ckf, next_ckf, &ri->data.keyfields, ctrl.list) {
		list_del(&ckf->ctrl.list);
		kfree(ckf);
	}
	list_for_each_entry_safe(caf, next_caf, &ri->data.actionfields, ctrl.list) {
		list_del(&caf->ctrl.list);
		kfree(caf);
	}
	/* Deallocate the rule */
	kfree(rule);
}
EXPORT_SYMBOL_GPL(vcap_free_rule);

/* Delete rule in a VCAP instance */
int vcap_del_rule(struct vcap_control *vctrl, struct net_device *ndev, u32 id)
{
	struct vcap_rule_internal *ri, *elem;
	struct vcap_admin *admin;

	/* This will later also handle rule moving */
	if (!ndev)
		return -ENODEV;
	/* Look for the rule id in all vcaps */
	ri = vcap_lookup_rule(vctrl, id);
	if (!ri)
		return -EINVAL;
	admin = ri->admin;
	list_del(&ri->list);
	if (list_empty(&admin->rules)) {
		admin->last_used_addr = admin->last_valid_addr;
	} else {
		/* update the address range end marker from the last rule in the list */
		elem = list_last_entry(&admin->rules, struct vcap_rule_internal, list);
		admin->last_used_addr = elem->addr;
	}
	kfree(ri);
	return 0;
}
EXPORT_SYMBOL_GPL(vcap_del_rule);

static void vcap_copy_from_client_keyfield(struct vcap_rule *rule,
					   struct vcap_client_keyfield *field,
					   struct vcap_client_keyfield_data *data)
{
	/* This will be expanded later to handle different vcap memory layouts */
	memcpy(&field->data, data, sizeof(field->data));
}

static int vcap_rule_add_key(struct vcap_rule *rule,
			     enum vcap_key_field key,
			     enum vcap_field_type ftype,
			     struct vcap_client_keyfield_data *data)
{
	struct vcap_client_keyfield *field;

	/* More validation will be added here later */
	field = kzalloc(sizeof(*field), GFP_KERNEL);
	if (!field)
		return -ENOMEM;
	field->ctrl.key = key;
	field->ctrl.type = ftype;
	vcap_copy_from_client_keyfield(rule, field, data);
	list_add_tail(&field->ctrl.list, &rule->keyfields);
	return 0;
}

/* Add a 48 bit key with value and mask to the rule */
int vcap_rule_add_key_u48(struct vcap_rule *rule, enum vcap_key_field key,
			  struct vcap_u48_key *fieldval)
{
	struct vcap_client_keyfield_data data;

	memcpy(&data.u48, fieldval, sizeof(data.u48));
	return vcap_rule_add_key(rule, key, VCAP_FIELD_U48, &data);
}
EXPORT_SYMBOL_GPL(vcap_rule_add_key_u48);

static void vcap_copy_from_client_actionfield(struct vcap_rule *rule,
					      struct vcap_client_actionfield *field,
					      struct vcap_client_actionfield_data *data)
{
	/* This will be expanded later to handle different vcap memory layouts */
	memcpy(&field->data, data, sizeof(field->data));
}

static int vcap_rule_add_action(struct vcap_rule *rule,
				enum vcap_action_field action,
				enum vcap_field_type ftype,
				struct vcap_client_actionfield_data *data)
{
	struct vcap_client_actionfield *field;

	/* More validation will be added here later */
	field = kzalloc(sizeof(*field), GFP_KERNEL);
	if (!field)
		return -ENOMEM;
	field->ctrl.action = action;
	field->ctrl.type = ftype;
	vcap_copy_from_client_actionfield(rule, field, data);
	list_add_tail(&field->ctrl.list, &rule->actionfields);
	return 0;
}

static void vcap_rule_set_action_bitsize(struct vcap_u1_action *u1,
					 enum vcap_bit val)
{
	switch (val) {
	case VCAP_BIT_0:
		u1->value = 0;
		break;
	case VCAP_BIT_1:
		u1->value = 1;
		break;
	case VCAP_BIT_ANY:
		u1->value = 0;
		break;
	}
}

/* Add a bit action with value to the rule */
int vcap_rule_add_action_bit(struct vcap_rule *rule,
			     enum vcap_action_field action,
			     enum vcap_bit val)
{
	struct vcap_client_actionfield_data data;

	vcap_rule_set_action_bitsize(&data.u1, val);
	return vcap_rule_add_action(rule, action, VCAP_FIELD_BIT, &data);
}
EXPORT_SYMBOL_GPL(vcap_rule_add_action_bit);

/* Add a 32 bit action field with value to the rule */
int vcap_rule_add_action_u32(struct vcap_rule *rule,
			     enum vcap_action_field action,
			     u32 value)
{
	struct vcap_client_actionfield_data data;

	data.u32.value = value;
	return vcap_rule_add_action(rule, action, VCAP_FIELD_U32, &data);
}
EXPORT_SYMBOL_GPL(vcap_rule_add_action_u32);

/* Copy to host byte order */
void vcap_netbytes_copy(u8 *dst, u8 *src, int count)
{
	int idx;

	for (idx = 0; idx < count; ++idx, ++dst)
		*dst = src[count - idx - 1];
}
EXPORT_SYMBOL_GPL(vcap_netbytes_copy);

/* Convert validation error code into tc extact error message */
void vcap_set_tc_exterr(struct flow_cls_offload *fco, struct vcap_rule *vrule)
{
	switch (vrule->exterr) {
	case VCAP_ERR_NONE:
		break;
	case VCAP_ERR_NO_ADMIN:
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "Missing VCAP instance");
		break;
	case VCAP_ERR_NO_NETDEV:
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "Missing network interface");
		break;
	case VCAP_ERR_NO_KEYSET_MATCH:
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "No keyset matched the filter keys");
		break;
	case VCAP_ERR_NO_ACTIONSET_MATCH:
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "No actionset matched the filter actions");
		break;
	case VCAP_ERR_NO_PORT_KEYSET_MATCH:
		NL_SET_ERR_MSG_MOD(fco->common.extack,
				   "No port keyset matched the filter keys");
		break;
	}
}
EXPORT_SYMBOL_GPL(vcap_set_tc_exterr);
