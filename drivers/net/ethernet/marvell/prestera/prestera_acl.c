// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2020-2021 Marvell International Ltd. All rights reserved */

#include <linux/rhashtable.h>

#include "prestera_acl.h"
#include "prestera_flow.h"
#include "prestera_hw.h"
#include "prestera.h"

#define ACL_KEYMASK_SIZE	\
	(sizeof(__be32) * __PRESTERA_ACL_RULE_MATCH_TYPE_MAX)

struct prestera_acl {
	struct prestera_switch *sw;
	struct list_head vtcam_list;
	struct list_head rules;
	struct rhashtable ruleset_ht;
	struct rhashtable acl_rule_entry_ht;
	struct idr uid;
};

struct prestera_acl_ruleset_ht_key {
	struct prestera_flow_block *block;
};

struct prestera_acl_rule_entry {
	struct rhash_head ht_node;
	struct prestera_acl_rule_entry_key key;
	u32 hw_id;
	u32 vtcam_id;
	struct {
		struct {
			u8 valid:1;
		} accept, drop, trap;
		struct {
			u32 id;
			struct prestera_counter_block *block;
		} counter;
	};
};

struct prestera_acl_ruleset {
	struct rhash_head ht_node; /* Member of acl HT */
	struct prestera_acl_ruleset_ht_key ht_key;
	struct rhashtable rule_ht;
	struct prestera_acl *acl;
	unsigned long rule_count;
	refcount_t refcount;
	void *keymask;
	u32 vtcam_id;
	u16 pcl_id;
	bool offload;
};

struct prestera_acl_vtcam {
	struct list_head list;
	__be32 keymask[__PRESTERA_ACL_RULE_MATCH_TYPE_MAX];
	refcount_t refcount;
	u32 id;
	bool is_keymask_set;
	u8 lookup;
};

static const struct rhashtable_params prestera_acl_ruleset_ht_params = {
	.key_len = sizeof(struct prestera_acl_ruleset_ht_key),
	.key_offset = offsetof(struct prestera_acl_ruleset, ht_key),
	.head_offset = offsetof(struct prestera_acl_ruleset, ht_node),
	.automatic_shrinking = true,
};

static const struct rhashtable_params prestera_acl_rule_ht_params = {
	.key_len = sizeof(unsigned long),
	.key_offset = offsetof(struct prestera_acl_rule, cookie),
	.head_offset = offsetof(struct prestera_acl_rule, ht_node),
	.automatic_shrinking = true,
};

static const struct rhashtable_params __prestera_acl_rule_entry_ht_params = {
	.key_offset  = offsetof(struct prestera_acl_rule_entry, key),
	.head_offset = offsetof(struct prestera_acl_rule_entry, ht_node),
	.key_len     = sizeof(struct prestera_acl_rule_entry_key),
	.automatic_shrinking = true,
};

static struct prestera_acl_ruleset *
prestera_acl_ruleset_create(struct prestera_acl *acl,
			    struct prestera_flow_block *block)
{
	struct prestera_acl_ruleset *ruleset;
	u32 uid = 0;
	int err;

	ruleset = kzalloc(sizeof(*ruleset), GFP_KERNEL);
	if (!ruleset)
		return ERR_PTR(-ENOMEM);

	ruleset->acl = acl;
	ruleset->ht_key.block = block;
	refcount_set(&ruleset->refcount, 1);

	err = rhashtable_init(&ruleset->rule_ht, &prestera_acl_rule_ht_params);
	if (err)
		goto err_rhashtable_init;

	err = idr_alloc_u32(&acl->uid, NULL, &uid, U8_MAX, GFP_KERNEL);
	if (err)
		goto err_ruleset_create;

	/* make pcl-id based on uid */
	ruleset->pcl_id = (u8)uid;
	err = rhashtable_insert_fast(&acl->ruleset_ht, &ruleset->ht_node,
				     prestera_acl_ruleset_ht_params);
	if (err)
		goto err_ruleset_ht_insert;

	return ruleset;

err_ruleset_ht_insert:
	idr_remove(&acl->uid, uid);
err_ruleset_create:
	rhashtable_destroy(&ruleset->rule_ht);
err_rhashtable_init:
	kfree(ruleset);
	return ERR_PTR(err);
}

void prestera_acl_ruleset_keymask_set(struct prestera_acl_ruleset *ruleset,
				      void *keymask)
{
	ruleset->keymask = kmemdup(keymask, ACL_KEYMASK_SIZE, GFP_KERNEL);
}

int prestera_acl_ruleset_offload(struct prestera_acl_ruleset *ruleset)
{
	u32 vtcam_id;
	int err;

	if (ruleset->offload)
		return -EEXIST;

	err = prestera_acl_vtcam_id_get(ruleset->acl, 0,
					ruleset->keymask, &vtcam_id);
	if (err)
		return err;

	ruleset->vtcam_id = vtcam_id;
	ruleset->offload = true;
	return 0;
}

static void prestera_acl_ruleset_destroy(struct prestera_acl_ruleset *ruleset)
{
	struct prestera_acl *acl = ruleset->acl;
	u8 uid = ruleset->pcl_id & PRESTERA_ACL_KEYMASK_PCL_ID_USER;

	rhashtable_remove_fast(&acl->ruleset_ht, &ruleset->ht_node,
			       prestera_acl_ruleset_ht_params);

	if (ruleset->offload)
		WARN_ON(prestera_acl_vtcam_id_put(acl, ruleset->vtcam_id));

	idr_remove(&acl->uid, uid);

	rhashtable_destroy(&ruleset->rule_ht);
	kfree(ruleset->keymask);
	kfree(ruleset);
}

static struct prestera_acl_ruleset *
__prestera_acl_ruleset_lookup(struct prestera_acl *acl,
			      struct prestera_flow_block *block)
{
	struct prestera_acl_ruleset_ht_key ht_key;

	memset(&ht_key, 0, sizeof(ht_key));
	ht_key.block = block;
	return rhashtable_lookup_fast(&acl->ruleset_ht, &ht_key,
				      prestera_acl_ruleset_ht_params);
}

struct prestera_acl_ruleset *
prestera_acl_ruleset_lookup(struct prestera_acl *acl,
			    struct prestera_flow_block *block)
{
	struct prestera_acl_ruleset *ruleset;

	ruleset = __prestera_acl_ruleset_lookup(acl, block);
	if (!ruleset)
		return ERR_PTR(-ENOENT);

	refcount_inc(&ruleset->refcount);
	return ruleset;
}

struct prestera_acl_ruleset *
prestera_acl_ruleset_get(struct prestera_acl *acl,
			 struct prestera_flow_block *block)
{
	struct prestera_acl_ruleset *ruleset;

	ruleset = __prestera_acl_ruleset_lookup(acl, block);
	if (ruleset) {
		refcount_inc(&ruleset->refcount);
		return ruleset;
	}

	return prestera_acl_ruleset_create(acl, block);
}

void prestera_acl_ruleset_put(struct prestera_acl_ruleset *ruleset)
{
	if (!refcount_dec_and_test(&ruleset->refcount))
		return;

	prestera_acl_ruleset_destroy(ruleset);
}

int prestera_acl_ruleset_bind(struct prestera_acl_ruleset *ruleset,
			      struct prestera_port *port)
{
	struct prestera_acl_iface iface = {
		.type = PRESTERA_ACL_IFACE_TYPE_PORT,
		.port = port
	};

	return prestera_hw_vtcam_iface_bind(port->sw, &iface, ruleset->vtcam_id,
					    ruleset->pcl_id);
}

int prestera_acl_ruleset_unbind(struct prestera_acl_ruleset *ruleset,
				struct prestera_port *port)
{
	struct prestera_acl_iface iface = {
		.type = PRESTERA_ACL_IFACE_TYPE_PORT,
		.port = port
	};

	return prestera_hw_vtcam_iface_unbind(port->sw, &iface,
					      ruleset->vtcam_id);
}

static int prestera_acl_ruleset_block_bind(struct prestera_acl_ruleset *ruleset,
					   struct prestera_flow_block *block)
{
	struct prestera_flow_block_binding *binding;
	int err;

	block->ruleset_zero = ruleset;
	list_for_each_entry(binding, &block->binding_list, list) {
		err = prestera_acl_ruleset_bind(ruleset, binding->port);
		if (err)
			goto rollback;
	}
	return 0;

rollback:
	list_for_each_entry_continue_reverse(binding, &block->binding_list,
					     list)
		err = prestera_acl_ruleset_unbind(ruleset, binding->port);
	block->ruleset_zero = NULL;

	return err;
}

static void
prestera_acl_ruleset_block_unbind(struct prestera_acl_ruleset *ruleset,
				  struct prestera_flow_block *block)
{
	struct prestera_flow_block_binding *binding;

	list_for_each_entry(binding, &block->binding_list, list)
		prestera_acl_ruleset_unbind(ruleset, binding->port);
	block->ruleset_zero = NULL;
}

void
prestera_acl_rule_keymask_pcl_id_set(struct prestera_acl_rule *rule, u16 pcl_id)
{
	struct prestera_acl_match *r_match = &rule->re_key.match;
	__be16 pcl_id_mask = htons(PRESTERA_ACL_KEYMASK_PCL_ID);
	__be16 pcl_id_key = htons(pcl_id);

	rule_match_set(r_match->key, PCL_ID, pcl_id_key);
	rule_match_set(r_match->mask, PCL_ID, pcl_id_mask);
}

struct prestera_acl_rule *
prestera_acl_rule_lookup(struct prestera_acl_ruleset *ruleset,
			 unsigned long cookie)
{
	return rhashtable_lookup_fast(&ruleset->rule_ht, &cookie,
				      prestera_acl_rule_ht_params);
}

bool prestera_acl_ruleset_is_offload(struct prestera_acl_ruleset *ruleset)
{
	return ruleset->offload;
}

struct prestera_acl_rule *
prestera_acl_rule_create(struct prestera_acl_ruleset *ruleset,
			 unsigned long cookie)
{
	struct prestera_acl_rule *rule;

	rule = kzalloc(sizeof(*rule), GFP_KERNEL);
	if (!rule)
		return ERR_PTR(-ENOMEM);

	rule->ruleset = ruleset;
	rule->cookie = cookie;

	refcount_inc(&ruleset->refcount);

	return rule;
}

void prestera_acl_rule_priority_set(struct prestera_acl_rule *rule,
				    u32 priority)
{
	rule->priority = priority;
}

void prestera_acl_rule_destroy(struct prestera_acl_rule *rule)
{
	prestera_acl_ruleset_put(rule->ruleset);
	kfree(rule);
}

int prestera_acl_rule_add(struct prestera_switch *sw,
			  struct prestera_acl_rule *rule)
{
	int err;
	struct prestera_acl_ruleset *ruleset = rule->ruleset;
	struct prestera_flow_block *block = ruleset->ht_key.block;

	/* try to add rule to hash table first */
	err = rhashtable_insert_fast(&ruleset->rule_ht, &rule->ht_node,
				     prestera_acl_rule_ht_params);
	if (err)
		goto err_ht_insert;

	prestera_acl_rule_keymask_pcl_id_set(rule, ruleset->pcl_id);
	rule->re_arg.vtcam_id = ruleset->vtcam_id;
	rule->re_key.prio = rule->priority;

	/* setup counter */
	rule->re_arg.count.valid = true;
	rule->re_arg.count.client = PRESTERA_HW_COUNTER_CLIENT_LOOKUP_0;

	rule->re = prestera_acl_rule_entry_find(sw->acl, &rule->re_key);
	err = WARN_ON(rule->re) ? -EEXIST : 0;
	if (err)
		goto err_rule_add;

	rule->re = prestera_acl_rule_entry_create(sw->acl, &rule->re_key,
						  &rule->re_arg);
	err = !rule->re ? -EINVAL : 0;
	if (err)
		goto err_rule_add;

	/* bind the block (all ports) to chain index 0 */
	if (!ruleset->rule_count) {
		err = prestera_acl_ruleset_block_bind(ruleset, block);
		if (err)
			goto err_acl_block_bind;
	}

	list_add_tail(&rule->list, &sw->acl->rules);
	ruleset->rule_count++;
	return 0;

err_acl_block_bind:
	prestera_acl_rule_entry_destroy(sw->acl, rule->re);
err_rule_add:
	rule->re = NULL;
	rhashtable_remove_fast(&ruleset->rule_ht, &rule->ht_node,
			       prestera_acl_rule_ht_params);
err_ht_insert:
	return err;
}

void prestera_acl_rule_del(struct prestera_switch *sw,
			   struct prestera_acl_rule *rule)
{
	struct prestera_acl_ruleset *ruleset = rule->ruleset;
	struct prestera_flow_block *block = ruleset->ht_key.block;

	rhashtable_remove_fast(&ruleset->rule_ht, &rule->ht_node,
			       prestera_acl_rule_ht_params);
	ruleset->rule_count--;
	list_del(&rule->list);

	prestera_acl_rule_entry_destroy(sw->acl, rule->re);

	/* unbind block (all ports) */
	if (!ruleset->rule_count)
		prestera_acl_ruleset_block_unbind(ruleset, block);
}

int prestera_acl_rule_get_stats(struct prestera_acl *acl,
				struct prestera_acl_rule *rule,
				u64 *packets, u64 *bytes, u64 *last_use)
{
	u64 current_packets;
	u64 current_bytes;
	int err;

	err = prestera_counter_stats_get(acl->sw->counter,
					 rule->re->counter.block,
					 rule->re->counter.id,
					 &current_packets, &current_bytes);
	if (err)
		return err;

	*packets = current_packets;
	*bytes = current_bytes;
	*last_use = jiffies;

	return 0;
}

struct prestera_acl_rule_entry *
prestera_acl_rule_entry_find(struct prestera_acl *acl,
			     struct prestera_acl_rule_entry_key *key)
{
	return rhashtable_lookup_fast(&acl->acl_rule_entry_ht, key,
				      __prestera_acl_rule_entry_ht_params);
}

static int __prestera_acl_rule_entry2hw_del(struct prestera_switch *sw,
					    struct prestera_acl_rule_entry *e)
{
	return prestera_hw_vtcam_rule_del(sw, e->vtcam_id, e->hw_id);
}

static int __prestera_acl_rule_entry2hw_add(struct prestera_switch *sw,
					    struct prestera_acl_rule_entry *e)
{
	struct prestera_acl_hw_action_info act_hw[PRESTERA_ACL_RULE_ACTION_MAX];
	int act_num;

	memset(&act_hw, 0, sizeof(act_hw));
	act_num = 0;

	/* accept */
	if (e->accept.valid) {
		act_hw[act_num].id = PRESTERA_ACL_RULE_ACTION_ACCEPT;
		act_num++;
	}
	/* drop */
	if (e->drop.valid) {
		act_hw[act_num].id = PRESTERA_ACL_RULE_ACTION_DROP;
		act_num++;
	}
	/* trap */
	if (e->trap.valid) {
		act_hw[act_num].id = PRESTERA_ACL_RULE_ACTION_TRAP;
		act_num++;
	}
	/* counter */
	if (e->counter.block) {
		act_hw[act_num].id = PRESTERA_ACL_RULE_ACTION_COUNT;
		act_hw[act_num].count.id = e->counter.id;
		act_num++;
	}

	return prestera_hw_vtcam_rule_add(sw, e->vtcam_id, e->key.prio,
					  e->key.match.key, e->key.match.mask,
					  act_hw, act_num, &e->hw_id);
}

static void
__prestera_acl_rule_entry_act_destruct(struct prestera_switch *sw,
				       struct prestera_acl_rule_entry *e)
{
	/* counter */
	prestera_counter_put(sw->counter, e->counter.block, e->counter.id);
}

void prestera_acl_rule_entry_destroy(struct prestera_acl *acl,
				     struct prestera_acl_rule_entry *e)
{
	int ret;

	rhashtable_remove_fast(&acl->acl_rule_entry_ht, &e->ht_node,
			       __prestera_acl_rule_entry_ht_params);

	ret = __prestera_acl_rule_entry2hw_del(acl->sw, e);
	WARN_ON(ret && ret != -ENODEV);

	__prestera_acl_rule_entry_act_destruct(acl->sw, e);
	kfree(e);
}

static int
__prestera_acl_rule_entry_act_construct(struct prestera_switch *sw,
					struct prestera_acl_rule_entry *e,
					struct prestera_acl_rule_entry_arg *arg)
{
	/* accept */
	e->accept.valid = arg->accept.valid;
	/* drop */
	e->drop.valid = arg->drop.valid;
	/* trap */
	e->trap.valid = arg->trap.valid;
	/* counter */
	if (arg->count.valid) {
		int err;

		err = prestera_counter_get(sw->counter, arg->count.client,
					   &e->counter.block,
					   &e->counter.id);
		if (err)
			goto err_out;
	}

	return 0;

err_out:
	__prestera_acl_rule_entry_act_destruct(sw, e);
	return -EINVAL;
}

struct prestera_acl_rule_entry *
prestera_acl_rule_entry_create(struct prestera_acl *acl,
			       struct prestera_acl_rule_entry_key *key,
			       struct prestera_acl_rule_entry_arg *arg)
{
	struct prestera_acl_rule_entry *e;
	int err;

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e)
		goto err_kzalloc;

	memcpy(&e->key, key, sizeof(*key));
	e->vtcam_id = arg->vtcam_id;
	err = __prestera_acl_rule_entry_act_construct(acl->sw, e, arg);
	if (err)
		goto err_act_construct;

	err = __prestera_acl_rule_entry2hw_add(acl->sw, e);
	if (err)
		goto err_hw_add;

	err = rhashtable_insert_fast(&acl->acl_rule_entry_ht, &e->ht_node,
				     __prestera_acl_rule_entry_ht_params);
	if (err)
		goto err_ht_insert;

	return e;

err_ht_insert:
	WARN_ON(__prestera_acl_rule_entry2hw_del(acl->sw, e));
err_hw_add:
	__prestera_acl_rule_entry_act_destruct(acl->sw, e);
err_act_construct:
	kfree(e);
err_kzalloc:
	return NULL;
}

static int __prestera_acl_vtcam_id_try_fit(struct prestera_acl *acl, u8 lookup,
					   void *keymask, u32 *vtcam_id)
{
	struct prestera_acl_vtcam *vtcam;
	int i;

	list_for_each_entry(vtcam, &acl->vtcam_list, list) {
		if (lookup != vtcam->lookup)
			continue;

		if (!keymask && !vtcam->is_keymask_set)
			goto vtcam_found;

		if (!(keymask && vtcam->is_keymask_set))
			continue;

		/* try to fit with vtcam keymask */
		for (i = 0; i < __PRESTERA_ACL_RULE_MATCH_TYPE_MAX; i++) {
			__be32 __keymask = ((__be32 *)keymask)[i];

			if (!__keymask)
				/* vtcam keymask in not interested */
				continue;

			if (__keymask & ~vtcam->keymask[i])
				/* keymask does not fit the vtcam keymask */
				break;
		}

		if (i == __PRESTERA_ACL_RULE_MATCH_TYPE_MAX)
			/* keymask fits vtcam keymask, return it */
			goto vtcam_found;
	}

	/* nothing is found */
	return -ENOENT;

vtcam_found:
	refcount_inc(&vtcam->refcount);
	*vtcam_id = vtcam->id;
	return 0;
}

int prestera_acl_vtcam_id_get(struct prestera_acl *acl, u8 lookup,
			      void *keymask, u32 *vtcam_id)
{
	struct prestera_acl_vtcam *vtcam;
	u32 new_vtcam_id;
	int err;

	/* find the vtcam that suits keymask. We do not expect to have
	 * a big number of vtcams, so, the list type for vtcam list is
	 * fine for now
	 */
	list_for_each_entry(vtcam, &acl->vtcam_list, list) {
		if (lookup != vtcam->lookup)
			continue;

		if (!keymask && !vtcam->is_keymask_set) {
			refcount_inc(&vtcam->refcount);
			goto vtcam_found;
		}

		if (keymask && vtcam->is_keymask_set &&
		    !memcmp(keymask, vtcam->keymask, sizeof(vtcam->keymask))) {
			refcount_inc(&vtcam->refcount);
			goto vtcam_found;
		}
	}

	/* vtcam not found, try to create new one */
	vtcam = kzalloc(sizeof(*vtcam), GFP_KERNEL);
	if (!vtcam)
		return -ENOMEM;

	err = prestera_hw_vtcam_create(acl->sw, lookup, keymask, &new_vtcam_id,
				       PRESTERA_HW_VTCAM_DIR_INGRESS);
	if (err) {
		kfree(vtcam);

		/* cannot create new, try to fit into existing vtcam */
		if (__prestera_acl_vtcam_id_try_fit(acl, lookup,
						    keymask, &new_vtcam_id))
			return err;

		*vtcam_id = new_vtcam_id;
		return 0;
	}

	vtcam->id = new_vtcam_id;
	vtcam->lookup = lookup;
	if (keymask) {
		memcpy(vtcam->keymask, keymask, sizeof(vtcam->keymask));
		vtcam->is_keymask_set = true;
	}
	refcount_set(&vtcam->refcount, 1);
	list_add_rcu(&vtcam->list, &acl->vtcam_list);

vtcam_found:
	*vtcam_id = vtcam->id;
	return 0;
}

int prestera_acl_vtcam_id_put(struct prestera_acl *acl, u32 vtcam_id)
{
	struct prestera_acl_vtcam *vtcam;
	int err;

	list_for_each_entry(vtcam, &acl->vtcam_list, list) {
		if (vtcam_id != vtcam->id)
			continue;

		if (!refcount_dec_and_test(&vtcam->refcount))
			return 0;

		err = prestera_hw_vtcam_destroy(acl->sw, vtcam->id);
		if (err && err != -ENODEV) {
			refcount_set(&vtcam->refcount, 1);
			return err;
		}

		list_del(&vtcam->list);
		kfree(vtcam);
		return 0;
	}

	return -ENOENT;
}

int prestera_acl_init(struct prestera_switch *sw)
{
	struct prestera_acl *acl;
	int err;

	acl = kzalloc(sizeof(*acl), GFP_KERNEL);
	if (!acl)
		return -ENOMEM;

	acl->sw = sw;
	INIT_LIST_HEAD(&acl->rules);
	INIT_LIST_HEAD(&acl->vtcam_list);
	idr_init(&acl->uid);

	err = rhashtable_init(&acl->acl_rule_entry_ht,
			      &__prestera_acl_rule_entry_ht_params);
	if (err)
		goto err_acl_rule_entry_ht_init;

	err = rhashtable_init(&acl->ruleset_ht,
			      &prestera_acl_ruleset_ht_params);
	if (err)
		goto err_ruleset_ht_init;

	sw->acl = acl;

	return 0;

err_ruleset_ht_init:
	rhashtable_destroy(&acl->acl_rule_entry_ht);
err_acl_rule_entry_ht_init:
	kfree(acl);
	return err;
}

void prestera_acl_fini(struct prestera_switch *sw)
{
	struct prestera_acl *acl = sw->acl;

	WARN_ON(!idr_is_empty(&acl->uid));
	idr_destroy(&acl->uid);

	WARN_ON(!list_empty(&acl->vtcam_list));
	WARN_ON(!list_empty(&acl->rules));

	rhashtable_destroy(&acl->ruleset_ht);
	rhashtable_destroy(&acl->acl_rule_entry_ht);

	kfree(acl);
}
