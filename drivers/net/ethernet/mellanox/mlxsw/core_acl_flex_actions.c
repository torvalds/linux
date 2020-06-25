// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/rhashtable.h>
#include <linux/list.h>
#include <linux/idr.h>
#include <linux/refcount.h>
#include <net/flow_offload.h>

#include "item.h"
#include "trap.h"
#include "core_acl_flex_actions.h"

enum mlxsw_afa_set_type {
	MLXSW_AFA_SET_TYPE_NEXT,
	MLXSW_AFA_SET_TYPE_GOTO,
};

/* afa_set_type
 * Type of the record at the end of the action set.
 */
MLXSW_ITEM32(afa, set, type, 0xA0, 28, 4);

/* afa_set_next_action_set_ptr
 * A pointer to the next action set in the KVD Centralized database.
 */
MLXSW_ITEM32(afa, set, next_action_set_ptr, 0xA4, 0, 24);

/* afa_set_goto_g
 * group - When set, the binding is of an ACL group. When cleared,
 * the binding is of an ACL.
 * Must be set to 1 for Spectrum.
 */
MLXSW_ITEM32(afa, set, goto_g, 0xA4, 29, 1);

enum mlxsw_afa_set_goto_binding_cmd {
	/* continue go the next binding point */
	MLXSW_AFA_SET_GOTO_BINDING_CMD_NONE,
	/* jump to the next binding point no return */
	MLXSW_AFA_SET_GOTO_BINDING_CMD_JUMP,
	/* terminate the acl binding */
	MLXSW_AFA_SET_GOTO_BINDING_CMD_TERM = 4,
};

/* afa_set_goto_binding_cmd */
MLXSW_ITEM32(afa, set, goto_binding_cmd, 0xA4, 24, 3);

/* afa_set_goto_next_binding
 * ACL/ACL group identifier. If the g bit is set, this field should hold
 * the acl_group_id, else it should hold the acl_id.
 */
MLXSW_ITEM32(afa, set, goto_next_binding, 0xA4, 0, 16);

/* afa_all_action_type
 * Action Type.
 */
MLXSW_ITEM32(afa, all, action_type, 0x00, 24, 6);

struct mlxsw_afa {
	unsigned int max_acts_per_set;
	const struct mlxsw_afa_ops *ops;
	void *ops_priv;
	struct rhashtable set_ht;
	struct rhashtable fwd_entry_ht;
	struct rhashtable cookie_ht;
	struct idr cookie_idr;
};

#define MLXSW_AFA_SET_LEN 0xA8

struct mlxsw_afa_set_ht_key {
	char enc_actions[MLXSW_AFA_SET_LEN]; /* Encoded set */
	bool is_first;
};

/* Set structure holds one action set record. It contains up to three
 * actions (depends on size of particular actions). The set is either
 * put directly to a rule, or it is stored in KVD linear area.
 * To prevent duplicate entries in KVD linear area, a hashtable is
 * used to track sets that were previously inserted and may be shared.
 */

struct mlxsw_afa_set {
	struct rhash_head ht_node;
	struct mlxsw_afa_set_ht_key ht_key;
	u32 kvdl_index;
	bool shared; /* Inserted in hashtable (doesn't mean that
		      * kvdl_index is valid).
		      */
	unsigned int ref_count;
	struct mlxsw_afa_set *next; /* Pointer to the next set. */
	struct mlxsw_afa_set *prev; /* Pointer to the previous set,
				     * note that set may have multiple
				     * sets from multiple blocks
				     * pointing at it. This is only
				     * usable until commit.
				     */
};

static const struct rhashtable_params mlxsw_afa_set_ht_params = {
	.key_len = sizeof(struct mlxsw_afa_set_ht_key),
	.key_offset = offsetof(struct mlxsw_afa_set, ht_key),
	.head_offset = offsetof(struct mlxsw_afa_set, ht_node),
	.automatic_shrinking = true,
};

struct mlxsw_afa_fwd_entry_ht_key {
	u8 local_port;
};

struct mlxsw_afa_fwd_entry {
	struct rhash_head ht_node;
	struct mlxsw_afa_fwd_entry_ht_key ht_key;
	u32 kvdl_index;
	unsigned int ref_count;
};

static const struct rhashtable_params mlxsw_afa_fwd_entry_ht_params = {
	.key_len = sizeof(struct mlxsw_afa_fwd_entry_ht_key),
	.key_offset = offsetof(struct mlxsw_afa_fwd_entry, ht_key),
	.head_offset = offsetof(struct mlxsw_afa_fwd_entry, ht_node),
	.automatic_shrinking = true,
};

struct mlxsw_afa_cookie {
	struct rhash_head ht_node;
	refcount_t ref_count;
	struct rcu_head rcu;
	u32 cookie_index;
	struct flow_action_cookie fa_cookie;
};

static u32 mlxsw_afa_cookie_hash(const struct flow_action_cookie *fa_cookie,
				 u32 seed)
{
	return jhash2((u32 *) fa_cookie->cookie,
		      fa_cookie->cookie_len / sizeof(u32), seed);
}

static u32 mlxsw_afa_cookie_key_hashfn(const void *data, u32 len, u32 seed)
{
	const struct flow_action_cookie *fa_cookie = data;

	return mlxsw_afa_cookie_hash(fa_cookie, seed);
}

static u32 mlxsw_afa_cookie_obj_hashfn(const void *data, u32 len, u32 seed)
{
	const struct mlxsw_afa_cookie *cookie = data;

	return mlxsw_afa_cookie_hash(&cookie->fa_cookie, seed);
}

static int mlxsw_afa_cookie_obj_cmpfn(struct rhashtable_compare_arg *arg,
				      const void *obj)
{
	const struct flow_action_cookie *fa_cookie = arg->key;
	const struct mlxsw_afa_cookie *cookie = obj;

	if (cookie->fa_cookie.cookie_len == fa_cookie->cookie_len)
		return memcmp(cookie->fa_cookie.cookie, fa_cookie->cookie,
			      fa_cookie->cookie_len);
	return 1;
}

static const struct rhashtable_params mlxsw_afa_cookie_ht_params = {
	.head_offset = offsetof(struct mlxsw_afa_cookie, ht_node),
	.hashfn	= mlxsw_afa_cookie_key_hashfn,
	.obj_hashfn = mlxsw_afa_cookie_obj_hashfn,
	.obj_cmpfn = mlxsw_afa_cookie_obj_cmpfn,
	.automatic_shrinking = true,
};

struct mlxsw_afa *mlxsw_afa_create(unsigned int max_acts_per_set,
				   const struct mlxsw_afa_ops *ops,
				   void *ops_priv)
{
	struct mlxsw_afa *mlxsw_afa;
	int err;

	mlxsw_afa = kzalloc(sizeof(*mlxsw_afa), GFP_KERNEL);
	if (!mlxsw_afa)
		return ERR_PTR(-ENOMEM);
	err = rhashtable_init(&mlxsw_afa->set_ht, &mlxsw_afa_set_ht_params);
	if (err)
		goto err_set_rhashtable_init;
	err = rhashtable_init(&mlxsw_afa->fwd_entry_ht,
			      &mlxsw_afa_fwd_entry_ht_params);
	if (err)
		goto err_fwd_entry_rhashtable_init;
	err = rhashtable_init(&mlxsw_afa->cookie_ht,
			      &mlxsw_afa_cookie_ht_params);
	if (err)
		goto err_cookie_rhashtable_init;
	idr_init(&mlxsw_afa->cookie_idr);
	mlxsw_afa->max_acts_per_set = max_acts_per_set;
	mlxsw_afa->ops = ops;
	mlxsw_afa->ops_priv = ops_priv;
	return mlxsw_afa;

err_cookie_rhashtable_init:
	rhashtable_destroy(&mlxsw_afa->fwd_entry_ht);
err_fwd_entry_rhashtable_init:
	rhashtable_destroy(&mlxsw_afa->set_ht);
err_set_rhashtable_init:
	kfree(mlxsw_afa);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(mlxsw_afa_create);

void mlxsw_afa_destroy(struct mlxsw_afa *mlxsw_afa)
{
	WARN_ON(!idr_is_empty(&mlxsw_afa->cookie_idr));
	idr_destroy(&mlxsw_afa->cookie_idr);
	rhashtable_destroy(&mlxsw_afa->cookie_ht);
	rhashtable_destroy(&mlxsw_afa->fwd_entry_ht);
	rhashtable_destroy(&mlxsw_afa->set_ht);
	kfree(mlxsw_afa);
}
EXPORT_SYMBOL(mlxsw_afa_destroy);

static void mlxsw_afa_set_goto_set(struct mlxsw_afa_set *set,
				   enum mlxsw_afa_set_goto_binding_cmd cmd,
				   u16 group_id)
{
	char *actions = set->ht_key.enc_actions;

	mlxsw_afa_set_type_set(actions, MLXSW_AFA_SET_TYPE_GOTO);
	mlxsw_afa_set_goto_g_set(actions, true);
	mlxsw_afa_set_goto_binding_cmd_set(actions, cmd);
	mlxsw_afa_set_goto_next_binding_set(actions, group_id);
}

static void mlxsw_afa_set_next_set(struct mlxsw_afa_set *set,
				   u32 next_set_kvdl_index)
{
	char *actions = set->ht_key.enc_actions;

	mlxsw_afa_set_type_set(actions, MLXSW_AFA_SET_TYPE_NEXT);
	mlxsw_afa_set_next_action_set_ptr_set(actions, next_set_kvdl_index);
}

static struct mlxsw_afa_set *mlxsw_afa_set_create(bool is_first)
{
	struct mlxsw_afa_set *set;

	set = kzalloc(sizeof(*set), GFP_KERNEL);
	if (!set)
		return NULL;
	/* Need to initialize the set to pass by default */
	mlxsw_afa_set_goto_set(set, MLXSW_AFA_SET_GOTO_BINDING_CMD_TERM, 0);
	set->ht_key.is_first = is_first;
	set->ref_count = 1;
	return set;
}

static void mlxsw_afa_set_destroy(struct mlxsw_afa_set *set)
{
	kfree(set);
}

static int mlxsw_afa_set_share(struct mlxsw_afa *mlxsw_afa,
			       struct mlxsw_afa_set *set)
{
	int err;

	err = rhashtable_insert_fast(&mlxsw_afa->set_ht, &set->ht_node,
				     mlxsw_afa_set_ht_params);
	if (err)
		return err;
	err = mlxsw_afa->ops->kvdl_set_add(mlxsw_afa->ops_priv,
					   &set->kvdl_index,
					   set->ht_key.enc_actions,
					   set->ht_key.is_first);
	if (err)
		goto err_kvdl_set_add;
	set->shared = true;
	set->prev = NULL;
	return 0;

err_kvdl_set_add:
	rhashtable_remove_fast(&mlxsw_afa->set_ht, &set->ht_node,
			       mlxsw_afa_set_ht_params);
	return err;
}

static void mlxsw_afa_set_unshare(struct mlxsw_afa *mlxsw_afa,
				  struct mlxsw_afa_set *set)
{
	mlxsw_afa->ops->kvdl_set_del(mlxsw_afa->ops_priv,
				     set->kvdl_index,
				     set->ht_key.is_first);
	rhashtable_remove_fast(&mlxsw_afa->set_ht, &set->ht_node,
			       mlxsw_afa_set_ht_params);
	set->shared = false;
}

static void mlxsw_afa_set_put(struct mlxsw_afa *mlxsw_afa,
			      struct mlxsw_afa_set *set)
{
	if (--set->ref_count)
		return;
	if (set->shared)
		mlxsw_afa_set_unshare(mlxsw_afa, set);
	mlxsw_afa_set_destroy(set);
}

static struct mlxsw_afa_set *mlxsw_afa_set_get(struct mlxsw_afa *mlxsw_afa,
					       struct mlxsw_afa_set *orig_set)
{
	struct mlxsw_afa_set *set;
	int err;

	/* There is a hashtable of sets maintained. If a set with the exact
	 * same encoding exists, we reuse it. Otherwise, the current set
	 * is shared by making it available to others using the hash table.
	 */
	set = rhashtable_lookup_fast(&mlxsw_afa->set_ht, &orig_set->ht_key,
				     mlxsw_afa_set_ht_params);
	if (set) {
		set->ref_count++;
		mlxsw_afa_set_put(mlxsw_afa, orig_set);
	} else {
		set = orig_set;
		err = mlxsw_afa_set_share(mlxsw_afa, set);
		if (err)
			return ERR_PTR(err);
	}
	return set;
}

/* Block structure holds a list of action sets. One action block
 * represents one chain of actions executed upon match of a rule.
 */

struct mlxsw_afa_block {
	struct mlxsw_afa *afa;
	bool finished;
	struct mlxsw_afa_set *first_set;
	struct mlxsw_afa_set *cur_set;
	unsigned int cur_act_index; /* In current set. */
	struct list_head resource_list; /* List of resources held by actions
					 * in this block.
					 */
};

struct mlxsw_afa_resource {
	struct list_head list;
	void (*destructor)(struct mlxsw_afa_block *block,
			   struct mlxsw_afa_resource *resource);
};

static void mlxsw_afa_resource_add(struct mlxsw_afa_block *block,
				   struct mlxsw_afa_resource *resource)
{
	list_add(&resource->list, &block->resource_list);
}

static void mlxsw_afa_resource_del(struct mlxsw_afa_resource *resource)
{
	list_del(&resource->list);
}

static void mlxsw_afa_resources_destroy(struct mlxsw_afa_block *block)
{
	struct mlxsw_afa_resource *resource, *tmp;

	list_for_each_entry_safe(resource, tmp, &block->resource_list, list) {
		resource->destructor(block, resource);
	}
}

struct mlxsw_afa_block *mlxsw_afa_block_create(struct mlxsw_afa *mlxsw_afa)
{
	struct mlxsw_afa_block *block;

	block = kzalloc(sizeof(*block), GFP_KERNEL);
	if (!block)
		return ERR_PTR(-ENOMEM);
	INIT_LIST_HEAD(&block->resource_list);
	block->afa = mlxsw_afa;

	/* At least one action set is always present, so just create it here */
	block->first_set = mlxsw_afa_set_create(true);
	if (!block->first_set)
		goto err_first_set_create;

	/* In case user instructs to have dummy first set, we leave it
	 * empty here and create another, real, set right away.
	 */
	if (mlxsw_afa->ops->dummy_first_set) {
		block->cur_set = mlxsw_afa_set_create(false);
		if (!block->cur_set)
			goto err_second_set_create;
		block->cur_set->prev = block->first_set;
		block->first_set->next = block->cur_set;
	} else {
		block->cur_set = block->first_set;
	}

	return block;

err_second_set_create:
	mlxsw_afa_set_destroy(block->first_set);
err_first_set_create:
	kfree(block);
	return ERR_PTR(-ENOMEM);
}
EXPORT_SYMBOL(mlxsw_afa_block_create);

void mlxsw_afa_block_destroy(struct mlxsw_afa_block *block)
{
	struct mlxsw_afa_set *set = block->first_set;
	struct mlxsw_afa_set *next_set;

	do {
		next_set = set->next;
		mlxsw_afa_set_put(block->afa, set);
		set = next_set;
	} while (set);
	mlxsw_afa_resources_destroy(block);
	kfree(block);
}
EXPORT_SYMBOL(mlxsw_afa_block_destroy);

int mlxsw_afa_block_commit(struct mlxsw_afa_block *block)
{
	struct mlxsw_afa_set *set = block->cur_set;
	struct mlxsw_afa_set *prev_set;

	block->cur_set = NULL;
	block->finished = true;

	/* Go over all linked sets starting from last
	 * and try to find existing set in the hash table.
	 * In case it is not there, assign a KVD linear index
	 * and insert it.
	 */
	do {
		prev_set = set->prev;
		set = mlxsw_afa_set_get(block->afa, set);
		if (IS_ERR(set))
			/* No rollback is needed since the chain is
			 * in consistent state and mlxsw_afa_block_destroy
			 * will take care of putting it away.
			 */
			return PTR_ERR(set);
		if (prev_set) {
			prev_set->next = set;
			mlxsw_afa_set_next_set(prev_set, set->kvdl_index);
			set = prev_set;
		}
	} while (prev_set);

	block->first_set = set;
	return 0;
}
EXPORT_SYMBOL(mlxsw_afa_block_commit);

char *mlxsw_afa_block_first_set(struct mlxsw_afa_block *block)
{
	return block->first_set->ht_key.enc_actions;
}
EXPORT_SYMBOL(mlxsw_afa_block_first_set);

char *mlxsw_afa_block_cur_set(struct mlxsw_afa_block *block)
{
	return block->cur_set->ht_key.enc_actions;
}
EXPORT_SYMBOL(mlxsw_afa_block_cur_set);

u32 mlxsw_afa_block_first_kvdl_index(struct mlxsw_afa_block *block)
{
	/* First set is never in KVD linear. So the first set
	 * with valid KVD linear index is always the second one.
	 */
	if (WARN_ON(!block->first_set->next))
		return 0;
	return block->first_set->next->kvdl_index;
}
EXPORT_SYMBOL(mlxsw_afa_block_first_kvdl_index);

int mlxsw_afa_block_activity_get(struct mlxsw_afa_block *block, bool *activity)
{
	u32 kvdl_index = mlxsw_afa_block_first_kvdl_index(block);

	return block->afa->ops->kvdl_set_activity_get(block->afa->ops_priv,
						      kvdl_index, activity);
}
EXPORT_SYMBOL(mlxsw_afa_block_activity_get);

int mlxsw_afa_block_continue(struct mlxsw_afa_block *block)
{
	if (block->finished)
		return -EINVAL;
	mlxsw_afa_set_goto_set(block->cur_set,
			       MLXSW_AFA_SET_GOTO_BINDING_CMD_NONE, 0);
	block->finished = true;
	return 0;
}
EXPORT_SYMBOL(mlxsw_afa_block_continue);

int mlxsw_afa_block_jump(struct mlxsw_afa_block *block, u16 group_id)
{
	if (block->finished)
		return -EINVAL;
	mlxsw_afa_set_goto_set(block->cur_set,
			       MLXSW_AFA_SET_GOTO_BINDING_CMD_JUMP, group_id);
	block->finished = true;
	return 0;
}
EXPORT_SYMBOL(mlxsw_afa_block_jump);

int mlxsw_afa_block_terminate(struct mlxsw_afa_block *block)
{
	if (block->finished)
		return -EINVAL;
	mlxsw_afa_set_goto_set(block->cur_set,
			       MLXSW_AFA_SET_GOTO_BINDING_CMD_TERM, 0);
	block->finished = true;
	return 0;
}
EXPORT_SYMBOL(mlxsw_afa_block_terminate);

static struct mlxsw_afa_fwd_entry *
mlxsw_afa_fwd_entry_create(struct mlxsw_afa *mlxsw_afa, u8 local_port)
{
	struct mlxsw_afa_fwd_entry *fwd_entry;
	int err;

	fwd_entry = kzalloc(sizeof(*fwd_entry), GFP_KERNEL);
	if (!fwd_entry)
		return ERR_PTR(-ENOMEM);
	fwd_entry->ht_key.local_port = local_port;
	fwd_entry->ref_count = 1;

	err = rhashtable_insert_fast(&mlxsw_afa->fwd_entry_ht,
				     &fwd_entry->ht_node,
				     mlxsw_afa_fwd_entry_ht_params);
	if (err)
		goto err_rhashtable_insert;

	err = mlxsw_afa->ops->kvdl_fwd_entry_add(mlxsw_afa->ops_priv,
						 &fwd_entry->kvdl_index,
						 local_port);
	if (err)
		goto err_kvdl_fwd_entry_add;
	return fwd_entry;

err_kvdl_fwd_entry_add:
	rhashtable_remove_fast(&mlxsw_afa->fwd_entry_ht, &fwd_entry->ht_node,
			       mlxsw_afa_fwd_entry_ht_params);
err_rhashtable_insert:
	kfree(fwd_entry);
	return ERR_PTR(err);
}

static void mlxsw_afa_fwd_entry_destroy(struct mlxsw_afa *mlxsw_afa,
					struct mlxsw_afa_fwd_entry *fwd_entry)
{
	mlxsw_afa->ops->kvdl_fwd_entry_del(mlxsw_afa->ops_priv,
					   fwd_entry->kvdl_index);
	rhashtable_remove_fast(&mlxsw_afa->fwd_entry_ht, &fwd_entry->ht_node,
			       mlxsw_afa_fwd_entry_ht_params);
	kfree(fwd_entry);
}

static struct mlxsw_afa_fwd_entry *
mlxsw_afa_fwd_entry_get(struct mlxsw_afa *mlxsw_afa, u8 local_port)
{
	struct mlxsw_afa_fwd_entry_ht_key ht_key = {0};
	struct mlxsw_afa_fwd_entry *fwd_entry;

	ht_key.local_port = local_port;
	fwd_entry = rhashtable_lookup_fast(&mlxsw_afa->fwd_entry_ht, &ht_key,
					   mlxsw_afa_fwd_entry_ht_params);
	if (fwd_entry) {
		fwd_entry->ref_count++;
		return fwd_entry;
	}
	return mlxsw_afa_fwd_entry_create(mlxsw_afa, local_port);
}

static void mlxsw_afa_fwd_entry_put(struct mlxsw_afa *mlxsw_afa,
				    struct mlxsw_afa_fwd_entry *fwd_entry)
{
	if (--fwd_entry->ref_count)
		return;
	mlxsw_afa_fwd_entry_destroy(mlxsw_afa, fwd_entry);
}

struct mlxsw_afa_fwd_entry_ref {
	struct mlxsw_afa_resource resource;
	struct mlxsw_afa_fwd_entry *fwd_entry;
};

static void
mlxsw_afa_fwd_entry_ref_destroy(struct mlxsw_afa_block *block,
				struct mlxsw_afa_fwd_entry_ref *fwd_entry_ref)
{
	mlxsw_afa_resource_del(&fwd_entry_ref->resource);
	mlxsw_afa_fwd_entry_put(block->afa, fwd_entry_ref->fwd_entry);
	kfree(fwd_entry_ref);
}

static void
mlxsw_afa_fwd_entry_ref_destructor(struct mlxsw_afa_block *block,
				   struct mlxsw_afa_resource *resource)
{
	struct mlxsw_afa_fwd_entry_ref *fwd_entry_ref;

	fwd_entry_ref = container_of(resource, struct mlxsw_afa_fwd_entry_ref,
				     resource);
	mlxsw_afa_fwd_entry_ref_destroy(block, fwd_entry_ref);
}

static struct mlxsw_afa_fwd_entry_ref *
mlxsw_afa_fwd_entry_ref_create(struct mlxsw_afa_block *block, u8 local_port)
{
	struct mlxsw_afa_fwd_entry_ref *fwd_entry_ref;
	struct mlxsw_afa_fwd_entry *fwd_entry;
	int err;

	fwd_entry_ref = kzalloc(sizeof(*fwd_entry_ref), GFP_KERNEL);
	if (!fwd_entry_ref)
		return ERR_PTR(-ENOMEM);
	fwd_entry = mlxsw_afa_fwd_entry_get(block->afa, local_port);
	if (IS_ERR(fwd_entry)) {
		err = PTR_ERR(fwd_entry);
		goto err_fwd_entry_get;
	}
	fwd_entry_ref->fwd_entry = fwd_entry;
	fwd_entry_ref->resource.destructor = mlxsw_afa_fwd_entry_ref_destructor;
	mlxsw_afa_resource_add(block, &fwd_entry_ref->resource);
	return fwd_entry_ref;

err_fwd_entry_get:
	kfree(fwd_entry_ref);
	return ERR_PTR(err);
}

struct mlxsw_afa_counter {
	struct mlxsw_afa_resource resource;
	u32 counter_index;
};

static void
mlxsw_afa_counter_destroy(struct mlxsw_afa_block *block,
			  struct mlxsw_afa_counter *counter)
{
	mlxsw_afa_resource_del(&counter->resource);
	block->afa->ops->counter_index_put(block->afa->ops_priv,
					   counter->counter_index);
	kfree(counter);
}

static void
mlxsw_afa_counter_destructor(struct mlxsw_afa_block *block,
			     struct mlxsw_afa_resource *resource)
{
	struct mlxsw_afa_counter *counter;

	counter = container_of(resource, struct mlxsw_afa_counter, resource);
	mlxsw_afa_counter_destroy(block, counter);
}

static struct mlxsw_afa_counter *
mlxsw_afa_counter_create(struct mlxsw_afa_block *block)
{
	struct mlxsw_afa_counter *counter;
	int err;

	counter = kzalloc(sizeof(*counter), GFP_KERNEL);
	if (!counter)
		return ERR_PTR(-ENOMEM);

	err = block->afa->ops->counter_index_get(block->afa->ops_priv,
						 &counter->counter_index);
	if (err)
		goto err_counter_index_get;
	counter->resource.destructor = mlxsw_afa_counter_destructor;
	mlxsw_afa_resource_add(block, &counter->resource);
	return counter;

err_counter_index_get:
	kfree(counter);
	return ERR_PTR(err);
}

/* 20 bits is a maximum that hardware can handle in trap with userdef action
 * and carry along with the trapped packet.
 */
#define MLXSW_AFA_COOKIE_INDEX_BITS 20
#define MLXSW_AFA_COOKIE_INDEX_MAX ((1 << MLXSW_AFA_COOKIE_INDEX_BITS) - 1)

static struct mlxsw_afa_cookie *
mlxsw_afa_cookie_create(struct mlxsw_afa *mlxsw_afa,
			const struct flow_action_cookie *fa_cookie)
{
	struct mlxsw_afa_cookie *cookie;
	u32 cookie_index;
	int err;

	cookie = kzalloc(sizeof(*cookie) + fa_cookie->cookie_len, GFP_KERNEL);
	if (!cookie)
		return ERR_PTR(-ENOMEM);
	refcount_set(&cookie->ref_count, 1);
	memcpy(&cookie->fa_cookie, fa_cookie,
	       sizeof(*fa_cookie) + fa_cookie->cookie_len);

	err = rhashtable_insert_fast(&mlxsw_afa->cookie_ht, &cookie->ht_node,
				     mlxsw_afa_cookie_ht_params);
	if (err)
		goto err_rhashtable_insert;

	/* Start cookie indexes with 1. Leave the 0 index unused. Packets
	 * that come from the HW which are not dropped by drop-with-cookie
	 * action are going to pass cookie_index 0 to lookup.
	 */
	cookie_index = 1;
	err = idr_alloc_u32(&mlxsw_afa->cookie_idr, cookie, &cookie_index,
			    MLXSW_AFA_COOKIE_INDEX_MAX, GFP_KERNEL);
	if (err)
		goto err_idr_alloc;
	cookie->cookie_index = cookie_index;
	return cookie;

err_idr_alloc:
	rhashtable_remove_fast(&mlxsw_afa->cookie_ht, &cookie->ht_node,
			       mlxsw_afa_cookie_ht_params);
err_rhashtable_insert:
	kfree(cookie);
	return ERR_PTR(err);
}

static void mlxsw_afa_cookie_destroy(struct mlxsw_afa *mlxsw_afa,
				     struct mlxsw_afa_cookie *cookie)
{
	idr_remove(&mlxsw_afa->cookie_idr, cookie->cookie_index);
	rhashtable_remove_fast(&mlxsw_afa->cookie_ht, &cookie->ht_node,
			       mlxsw_afa_cookie_ht_params);
	kfree_rcu(cookie, rcu);
}

static struct mlxsw_afa_cookie *
mlxsw_afa_cookie_get(struct mlxsw_afa *mlxsw_afa,
		     const struct flow_action_cookie *fa_cookie)
{
	struct mlxsw_afa_cookie *cookie;

	cookie = rhashtable_lookup_fast(&mlxsw_afa->cookie_ht, fa_cookie,
					mlxsw_afa_cookie_ht_params);
	if (cookie) {
		refcount_inc(&cookie->ref_count);
		return cookie;
	}
	return mlxsw_afa_cookie_create(mlxsw_afa, fa_cookie);
}

static void mlxsw_afa_cookie_put(struct mlxsw_afa *mlxsw_afa,
				 struct mlxsw_afa_cookie *cookie)
{
	if (!refcount_dec_and_test(&cookie->ref_count))
		return;
	mlxsw_afa_cookie_destroy(mlxsw_afa, cookie);
}

/* RCU read lock must be held */
const struct flow_action_cookie *
mlxsw_afa_cookie_lookup(struct mlxsw_afa *mlxsw_afa, u32 cookie_index)
{
	struct mlxsw_afa_cookie *cookie;

	/* 0 index means no cookie */
	if (!cookie_index)
		return NULL;
	cookie = idr_find(&mlxsw_afa->cookie_idr, cookie_index);
	if (!cookie)
		return NULL;
	return &cookie->fa_cookie;
}
EXPORT_SYMBOL(mlxsw_afa_cookie_lookup);

struct mlxsw_afa_cookie_ref {
	struct mlxsw_afa_resource resource;
	struct mlxsw_afa_cookie *cookie;
};

static void
mlxsw_afa_cookie_ref_destroy(struct mlxsw_afa_block *block,
			     struct mlxsw_afa_cookie_ref *cookie_ref)
{
	mlxsw_afa_resource_del(&cookie_ref->resource);
	mlxsw_afa_cookie_put(block->afa, cookie_ref->cookie);
	kfree(cookie_ref);
}

static void
mlxsw_afa_cookie_ref_destructor(struct mlxsw_afa_block *block,
				struct mlxsw_afa_resource *resource)
{
	struct mlxsw_afa_cookie_ref *cookie_ref;

	cookie_ref = container_of(resource, struct mlxsw_afa_cookie_ref,
				  resource);
	mlxsw_afa_cookie_ref_destroy(block, cookie_ref);
}

static struct mlxsw_afa_cookie_ref *
mlxsw_afa_cookie_ref_create(struct mlxsw_afa_block *block,
			    const struct flow_action_cookie *fa_cookie)
{
	struct mlxsw_afa_cookie_ref *cookie_ref;
	struct mlxsw_afa_cookie *cookie;
	int err;

	cookie_ref = kzalloc(sizeof(*cookie_ref), GFP_KERNEL);
	if (!cookie_ref)
		return ERR_PTR(-ENOMEM);
	cookie = mlxsw_afa_cookie_get(block->afa, fa_cookie);
	if (IS_ERR(cookie)) {
		err = PTR_ERR(cookie);
		goto err_cookie_get;
	}
	cookie_ref->cookie = cookie;
	cookie_ref->resource.destructor = mlxsw_afa_cookie_ref_destructor;
	mlxsw_afa_resource_add(block, &cookie_ref->resource);
	return cookie_ref;

err_cookie_get:
	kfree(cookie_ref);
	return ERR_PTR(err);
}

#define MLXSW_AFA_ONE_ACTION_LEN 32
#define MLXSW_AFA_PAYLOAD_OFFSET 4

static char *mlxsw_afa_block_append_action(struct mlxsw_afa_block *block,
					   u8 action_code, u8 action_size)
{
	char *oneact;
	char *actions;

	if (block->finished)
		return ERR_PTR(-EINVAL);
	if (block->cur_act_index + action_size >
	    block->afa->max_acts_per_set) {
		struct mlxsw_afa_set *set;

		/* The appended action won't fit into the current action set,
		 * so create a new set.
		 */
		set = mlxsw_afa_set_create(false);
		if (!set)
			return ERR_PTR(-ENOBUFS);
		set->prev = block->cur_set;
		block->cur_act_index = 0;
		block->cur_set->next = set;
		block->cur_set = set;
	}

	actions = block->cur_set->ht_key.enc_actions;
	oneact = actions + block->cur_act_index * MLXSW_AFA_ONE_ACTION_LEN;
	block->cur_act_index += action_size;
	mlxsw_afa_all_action_type_set(oneact, action_code);
	return oneact + MLXSW_AFA_PAYLOAD_OFFSET;
}

/* VLAN Action
 * -----------
 * VLAN action is used for manipulating VLANs. It can be used to implement QinQ,
 * VLAN translation, change of PCP bits of the VLAN tag, push, pop as swap VLANs
 * and more.
 */

#define MLXSW_AFA_VLAN_CODE 0x02
#define MLXSW_AFA_VLAN_SIZE 1

enum mlxsw_afa_vlan_vlan_tag_cmd {
	MLXSW_AFA_VLAN_VLAN_TAG_CMD_NOP,
	MLXSW_AFA_VLAN_VLAN_TAG_CMD_PUSH_TAG,
	MLXSW_AFA_VLAN_VLAN_TAG_CMD_POP_TAG,
};

enum mlxsw_afa_vlan_cmd {
	MLXSW_AFA_VLAN_CMD_NOP,
	MLXSW_AFA_VLAN_CMD_SET_OUTER,
	MLXSW_AFA_VLAN_CMD_SET_INNER,
	MLXSW_AFA_VLAN_CMD_COPY_OUTER_TO_INNER,
	MLXSW_AFA_VLAN_CMD_COPY_INNER_TO_OUTER,
	MLXSW_AFA_VLAN_CMD_SWAP,
};

/* afa_vlan_vlan_tag_cmd
 * Tag command: push, pop, nop VLAN header.
 */
MLXSW_ITEM32(afa, vlan, vlan_tag_cmd, 0x00, 29, 3);

/* afa_vlan_vid_cmd */
MLXSW_ITEM32(afa, vlan, vid_cmd, 0x04, 29, 3);

/* afa_vlan_vid */
MLXSW_ITEM32(afa, vlan, vid, 0x04, 0, 12);

/* afa_vlan_ethertype_cmd */
MLXSW_ITEM32(afa, vlan, ethertype_cmd, 0x08, 29, 3);

/* afa_vlan_ethertype
 * Index to EtherTypes in Switch VLAN EtherType Register (SVER).
 */
MLXSW_ITEM32(afa, vlan, ethertype, 0x08, 24, 3);

/* afa_vlan_pcp_cmd */
MLXSW_ITEM32(afa, vlan, pcp_cmd, 0x08, 13, 3);

/* afa_vlan_pcp */
MLXSW_ITEM32(afa, vlan, pcp, 0x08, 8, 3);

static inline void
mlxsw_afa_vlan_pack(char *payload,
		    enum mlxsw_afa_vlan_vlan_tag_cmd vlan_tag_cmd,
		    enum mlxsw_afa_vlan_cmd vid_cmd, u16 vid,
		    enum mlxsw_afa_vlan_cmd pcp_cmd, u8 pcp,
		    enum mlxsw_afa_vlan_cmd ethertype_cmd, u8 ethertype)
{
	mlxsw_afa_vlan_vlan_tag_cmd_set(payload, vlan_tag_cmd);
	mlxsw_afa_vlan_vid_cmd_set(payload, vid_cmd);
	mlxsw_afa_vlan_vid_set(payload, vid);
	mlxsw_afa_vlan_pcp_cmd_set(payload, pcp_cmd);
	mlxsw_afa_vlan_pcp_set(payload, pcp);
	mlxsw_afa_vlan_ethertype_cmd_set(payload, ethertype_cmd);
	mlxsw_afa_vlan_ethertype_set(payload, ethertype);
}

int mlxsw_afa_block_append_vlan_modify(struct mlxsw_afa_block *block,
				       u16 vid, u8 pcp, u8 et,
				       struct netlink_ext_ack *extack)
{
	char *act = mlxsw_afa_block_append_action(block,
						  MLXSW_AFA_VLAN_CODE,
						  MLXSW_AFA_VLAN_SIZE);

	if (IS_ERR(act)) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot append vlan_modify action");
		return PTR_ERR(act);
	}
	mlxsw_afa_vlan_pack(act, MLXSW_AFA_VLAN_VLAN_TAG_CMD_NOP,
			    MLXSW_AFA_VLAN_CMD_SET_OUTER, vid,
			    MLXSW_AFA_VLAN_CMD_SET_OUTER, pcp,
			    MLXSW_AFA_VLAN_CMD_SET_OUTER, et);
	return 0;
}
EXPORT_SYMBOL(mlxsw_afa_block_append_vlan_modify);

/* Trap Action / Trap With Userdef Action
 * --------------------------------------
 * The Trap action enables trapping / mirroring packets to the CPU
 * as well as discarding packets.
 * The ACL Trap / Discard separates the forward/discard control from CPU
 * trap control. In addition, the Trap / Discard action enables activating
 * SPAN (port mirroring).
 *
 * The Trap with userdef action action has the same functionality as
 * the Trap action with addition of user defined value that can be set
 * and used by higher layer applications.
 */

#define MLXSW_AFA_TRAP_CODE 0x03
#define MLXSW_AFA_TRAP_SIZE 1

#define MLXSW_AFA_TRAPWU_CODE 0x04
#define MLXSW_AFA_TRAPWU_SIZE 2

enum mlxsw_afa_trap_trap_action {
	MLXSW_AFA_TRAP_TRAP_ACTION_NOP = 0,
	MLXSW_AFA_TRAP_TRAP_ACTION_TRAP = 2,
};

/* afa_trap_trap_action
 * Trap Action.
 */
MLXSW_ITEM32(afa, trap, trap_action, 0x00, 24, 4);

enum mlxsw_afa_trap_forward_action {
	MLXSW_AFA_TRAP_FORWARD_ACTION_FORWARD = 1,
	MLXSW_AFA_TRAP_FORWARD_ACTION_DISCARD = 3,
};

/* afa_trap_forward_action
 * Forward Action.
 */
MLXSW_ITEM32(afa, trap, forward_action, 0x00, 0, 4);

/* afa_trap_trap_id
 * Trap ID to configure.
 */
MLXSW_ITEM32(afa, trap, trap_id, 0x04, 0, 9);

/* afa_trap_mirror_agent
 * Mirror agent.
 */
MLXSW_ITEM32(afa, trap, mirror_agent, 0x08, 29, 3);

/* afa_trap_mirror_enable
 * Mirror enable.
 */
MLXSW_ITEM32(afa, trap, mirror_enable, 0x08, 24, 1);

/* user_def_val
 * Value for the SW usage. Can be used to pass information of which
 * rule has caused a trap. This may be overwritten by later traps.
 * This field does a set on the packet's user_def_val only if this
 * is the first trap_id or if the trap_id has replaced the previous
 * packet's trap_id.
 */
MLXSW_ITEM32(afa, trap, user_def_val, 0x0C, 0, 20);

static inline void
mlxsw_afa_trap_pack(char *payload,
		    enum mlxsw_afa_trap_trap_action trap_action,
		    enum mlxsw_afa_trap_forward_action forward_action,
		    u16 trap_id)
{
	mlxsw_afa_trap_trap_action_set(payload, trap_action);
	mlxsw_afa_trap_forward_action_set(payload, forward_action);
	mlxsw_afa_trap_trap_id_set(payload, trap_id);
}

static inline void
mlxsw_afa_trapwu_pack(char *payload,
		      enum mlxsw_afa_trap_trap_action trap_action,
		      enum mlxsw_afa_trap_forward_action forward_action,
		      u16 trap_id, u32 user_def_val)
{
	mlxsw_afa_trap_pack(payload, trap_action, forward_action, trap_id);
	mlxsw_afa_trap_user_def_val_set(payload, user_def_val);
}

static inline void
mlxsw_afa_trap_mirror_pack(char *payload, bool mirror_enable,
			   u8 mirror_agent)
{
	mlxsw_afa_trap_mirror_enable_set(payload, mirror_enable);
	mlxsw_afa_trap_mirror_agent_set(payload, mirror_agent);
}

static int mlxsw_afa_block_append_drop_plain(struct mlxsw_afa_block *block,
					     bool ingress)
{
	char *act = mlxsw_afa_block_append_action(block, MLXSW_AFA_TRAP_CODE,
						  MLXSW_AFA_TRAP_SIZE);

	if (IS_ERR(act))
		return PTR_ERR(act);
	mlxsw_afa_trap_pack(act, MLXSW_AFA_TRAP_TRAP_ACTION_TRAP,
			    MLXSW_AFA_TRAP_FORWARD_ACTION_DISCARD,
			    ingress ? MLXSW_TRAP_ID_DISCARD_INGRESS_ACL :
				      MLXSW_TRAP_ID_DISCARD_EGRESS_ACL);
	return 0;
}

static int
mlxsw_afa_block_append_drop_with_cookie(struct mlxsw_afa_block *block,
					bool ingress,
					const struct flow_action_cookie *fa_cookie,
					struct netlink_ext_ack *extack)
{
	struct mlxsw_afa_cookie_ref *cookie_ref;
	u32 cookie_index;
	char *act;
	int err;

	cookie_ref = mlxsw_afa_cookie_ref_create(block, fa_cookie);
	if (IS_ERR(cookie_ref)) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot create cookie for drop action");
		return PTR_ERR(cookie_ref);
	}
	cookie_index = cookie_ref->cookie->cookie_index;

	act = mlxsw_afa_block_append_action(block, MLXSW_AFA_TRAPWU_CODE,
					    MLXSW_AFA_TRAPWU_SIZE);
	if (IS_ERR(act)) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot append drop with cookie action");
		err = PTR_ERR(act);
		goto err_append_action;
	}
	mlxsw_afa_trapwu_pack(act, MLXSW_AFA_TRAP_TRAP_ACTION_TRAP,
			      MLXSW_AFA_TRAP_FORWARD_ACTION_DISCARD,
			      ingress ? MLXSW_TRAP_ID_DISCARD_INGRESS_ACL :
					MLXSW_TRAP_ID_DISCARD_EGRESS_ACL,
			      cookie_index);
	return 0;

err_append_action:
	mlxsw_afa_cookie_ref_destroy(block, cookie_ref);
	return err;
}

int mlxsw_afa_block_append_drop(struct mlxsw_afa_block *block, bool ingress,
				const struct flow_action_cookie *fa_cookie,
				struct netlink_ext_ack *extack)
{
	return fa_cookie ?
	       mlxsw_afa_block_append_drop_with_cookie(block, ingress,
						       fa_cookie, extack) :
	       mlxsw_afa_block_append_drop_plain(block, ingress);
}
EXPORT_SYMBOL(mlxsw_afa_block_append_drop);

int mlxsw_afa_block_append_trap(struct mlxsw_afa_block *block, u16 trap_id)
{
	char *act = mlxsw_afa_block_append_action(block, MLXSW_AFA_TRAP_CODE,
						  MLXSW_AFA_TRAP_SIZE);

	if (IS_ERR(act))
		return PTR_ERR(act);
	mlxsw_afa_trap_pack(act, MLXSW_AFA_TRAP_TRAP_ACTION_TRAP,
			    MLXSW_AFA_TRAP_FORWARD_ACTION_DISCARD, trap_id);
	return 0;
}
EXPORT_SYMBOL(mlxsw_afa_block_append_trap);

int mlxsw_afa_block_append_trap_and_forward(struct mlxsw_afa_block *block,
					    u16 trap_id)
{
	char *act = mlxsw_afa_block_append_action(block, MLXSW_AFA_TRAP_CODE,
						  MLXSW_AFA_TRAP_SIZE);

	if (IS_ERR(act))
		return PTR_ERR(act);
	mlxsw_afa_trap_pack(act, MLXSW_AFA_TRAP_TRAP_ACTION_TRAP,
			    MLXSW_AFA_TRAP_FORWARD_ACTION_FORWARD, trap_id);
	return 0;
}
EXPORT_SYMBOL(mlxsw_afa_block_append_trap_and_forward);

struct mlxsw_afa_mirror {
	struct mlxsw_afa_resource resource;
	int span_id;
	u8 local_in_port;
	bool ingress;
};

static void
mlxsw_afa_mirror_destroy(struct mlxsw_afa_block *block,
			 struct mlxsw_afa_mirror *mirror)
{
	mlxsw_afa_resource_del(&mirror->resource);
	block->afa->ops->mirror_del(block->afa->ops_priv,
				    mirror->local_in_port,
				    mirror->span_id,
				    mirror->ingress);
	kfree(mirror);
}

static void
mlxsw_afa_mirror_destructor(struct mlxsw_afa_block *block,
			    struct mlxsw_afa_resource *resource)
{
	struct mlxsw_afa_mirror *mirror;

	mirror = container_of(resource, struct mlxsw_afa_mirror, resource);
	mlxsw_afa_mirror_destroy(block, mirror);
}

static struct mlxsw_afa_mirror *
mlxsw_afa_mirror_create(struct mlxsw_afa_block *block, u8 local_in_port,
			const struct net_device *out_dev, bool ingress)
{
	struct mlxsw_afa_mirror *mirror;
	int err;

	mirror = kzalloc(sizeof(*mirror), GFP_KERNEL);
	if (!mirror)
		return ERR_PTR(-ENOMEM);

	err = block->afa->ops->mirror_add(block->afa->ops_priv,
					  local_in_port, out_dev,
					  ingress, &mirror->span_id);
	if (err)
		goto err_mirror_add;

	mirror->ingress = ingress;
	mirror->local_in_port = local_in_port;
	mirror->resource.destructor = mlxsw_afa_mirror_destructor;
	mlxsw_afa_resource_add(block, &mirror->resource);
	return mirror;

err_mirror_add:
	kfree(mirror);
	return ERR_PTR(err);
}

static int
mlxsw_afa_block_append_allocated_mirror(struct mlxsw_afa_block *block,
					u8 mirror_agent)
{
	char *act = mlxsw_afa_block_append_action(block,
						  MLXSW_AFA_TRAP_CODE,
						  MLXSW_AFA_TRAP_SIZE);
	if (IS_ERR(act))
		return PTR_ERR(act);
	mlxsw_afa_trap_pack(act, MLXSW_AFA_TRAP_TRAP_ACTION_NOP,
			    MLXSW_AFA_TRAP_FORWARD_ACTION_FORWARD, 0);
	mlxsw_afa_trap_mirror_pack(act, true, mirror_agent);
	return 0;
}

int
mlxsw_afa_block_append_mirror(struct mlxsw_afa_block *block, u8 local_in_port,
			      const struct net_device *out_dev, bool ingress,
			      struct netlink_ext_ack *extack)
{
	struct mlxsw_afa_mirror *mirror;
	int err;

	mirror = mlxsw_afa_mirror_create(block, local_in_port, out_dev,
					 ingress);
	if (IS_ERR(mirror)) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot create mirror action");
		return PTR_ERR(mirror);
	}
	err = mlxsw_afa_block_append_allocated_mirror(block, mirror->span_id);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot append mirror action");
		goto err_append_allocated_mirror;
	}

	return 0;

err_append_allocated_mirror:
	mlxsw_afa_mirror_destroy(block, mirror);
	return err;
}
EXPORT_SYMBOL(mlxsw_afa_block_append_mirror);

/* QoS Action
 * ----------
 * The QOS_ACTION is used for manipulating the QoS attributes of a packet. It
 * can be used to change the DCSP, ECN, Color and Switch Priority of the packet.
 * Note that PCP field can be changed using the VLAN action.
 */

#define MLXSW_AFA_QOS_CODE 0x06
#define MLXSW_AFA_QOS_SIZE 1

enum mlxsw_afa_qos_ecn_cmd {
	/* Do nothing */
	MLXSW_AFA_QOS_ECN_CMD_NOP,
	/* Set ECN to afa_qos_ecn */
	MLXSW_AFA_QOS_ECN_CMD_SET,
};

/* afa_qos_ecn_cmd
 */
MLXSW_ITEM32(afa, qos, ecn_cmd, 0x04, 29, 3);

/* afa_qos_ecn
 * ECN value.
 */
MLXSW_ITEM32(afa, qos, ecn, 0x04, 24, 2);

enum mlxsw_afa_qos_dscp_cmd {
	/* Do nothing */
	MLXSW_AFA_QOS_DSCP_CMD_NOP,
	/* Set DSCP 3 LSB bits according to dscp[2:0] */
	MLXSW_AFA_QOS_DSCP_CMD_SET_3LSB,
	/* Set DSCP 3 MSB bits according to dscp[5:3] */
	MLXSW_AFA_QOS_DSCP_CMD_SET_3MSB,
	/* Set DSCP 6 bits according to dscp[5:0] */
	MLXSW_AFA_QOS_DSCP_CMD_SET_ALL,
};

/* afa_qos_dscp_cmd
 * DSCP command.
 */
MLXSW_ITEM32(afa, qos, dscp_cmd, 0x04, 14, 2);

/* afa_qos_dscp
 * DSCP value.
 */
MLXSW_ITEM32(afa, qos, dscp, 0x04, 0, 6);

enum mlxsw_afa_qos_switch_prio_cmd {
	/* Do nothing */
	MLXSW_AFA_QOS_SWITCH_PRIO_CMD_NOP,
	/* Set Switch Priority to afa_qos_switch_prio */
	MLXSW_AFA_QOS_SWITCH_PRIO_CMD_SET,
};

/* afa_qos_switch_prio_cmd
 */
MLXSW_ITEM32(afa, qos, switch_prio_cmd, 0x08, 14, 2);

/* afa_qos_switch_prio
 * Switch Priority.
 */
MLXSW_ITEM32(afa, qos, switch_prio, 0x08, 0, 4);

enum mlxsw_afa_qos_dscp_rw {
	MLXSW_AFA_QOS_DSCP_RW_PRESERVE,
	MLXSW_AFA_QOS_DSCP_RW_SET,
	MLXSW_AFA_QOS_DSCP_RW_CLEAR,
};

/* afa_qos_dscp_rw
 * DSCP Re-write Enable. Controlling the rewrite_enable for DSCP.
 */
MLXSW_ITEM32(afa, qos, dscp_rw, 0x0C, 30, 2);

static inline void
mlxsw_afa_qos_ecn_pack(char *payload,
		       enum mlxsw_afa_qos_ecn_cmd ecn_cmd, u8 ecn)
{
	mlxsw_afa_qos_ecn_cmd_set(payload, ecn_cmd);
	mlxsw_afa_qos_ecn_set(payload, ecn);
}

static inline void
mlxsw_afa_qos_dscp_pack(char *payload,
			enum mlxsw_afa_qos_dscp_cmd dscp_cmd, u8 dscp)
{
	mlxsw_afa_qos_dscp_cmd_set(payload, dscp_cmd);
	mlxsw_afa_qos_dscp_set(payload, dscp);
}

static inline void
mlxsw_afa_qos_switch_prio_pack(char *payload,
			       enum mlxsw_afa_qos_switch_prio_cmd prio_cmd,
			       u8 prio)
{
	mlxsw_afa_qos_switch_prio_cmd_set(payload, prio_cmd);
	mlxsw_afa_qos_switch_prio_set(payload, prio);
}

static int __mlxsw_afa_block_append_qos_dsfield(struct mlxsw_afa_block *block,
						bool set_dscp, u8 dscp,
						bool set_ecn, u8 ecn,
						struct netlink_ext_ack *extack)
{
	char *act = mlxsw_afa_block_append_action(block,
						  MLXSW_AFA_QOS_CODE,
						  MLXSW_AFA_QOS_SIZE);

	if (IS_ERR(act)) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot append QOS action");
		return PTR_ERR(act);
	}

	if (set_ecn)
		mlxsw_afa_qos_ecn_pack(act, MLXSW_AFA_QOS_ECN_CMD_SET, ecn);
	if (set_dscp) {
		mlxsw_afa_qos_dscp_pack(act, MLXSW_AFA_QOS_DSCP_CMD_SET_ALL,
					dscp);
		mlxsw_afa_qos_dscp_rw_set(act, MLXSW_AFA_QOS_DSCP_RW_CLEAR);
	}

	return 0;
}

int mlxsw_afa_block_append_qos_dsfield(struct mlxsw_afa_block *block,
				       u8 dsfield,
				       struct netlink_ext_ack *extack)
{
	return __mlxsw_afa_block_append_qos_dsfield(block,
						    true, dsfield >> 2,
						    true, dsfield & 0x03,
						    extack);
}
EXPORT_SYMBOL(mlxsw_afa_block_append_qos_dsfield);

int mlxsw_afa_block_append_qos_dscp(struct mlxsw_afa_block *block,
				    u8 dscp, struct netlink_ext_ack *extack)
{
	return __mlxsw_afa_block_append_qos_dsfield(block,
						    true, dscp,
						    false, 0,
						    extack);
}
EXPORT_SYMBOL(mlxsw_afa_block_append_qos_dscp);

int mlxsw_afa_block_append_qos_ecn(struct mlxsw_afa_block *block,
				   u8 ecn, struct netlink_ext_ack *extack)
{
	return __mlxsw_afa_block_append_qos_dsfield(block,
						    false, 0,
						    true, ecn,
						    extack);
}
EXPORT_SYMBOL(mlxsw_afa_block_append_qos_ecn);

int mlxsw_afa_block_append_qos_switch_prio(struct mlxsw_afa_block *block,
					   u8 prio,
					   struct netlink_ext_ack *extack)
{
	char *act = mlxsw_afa_block_append_action(block,
						  MLXSW_AFA_QOS_CODE,
						  MLXSW_AFA_QOS_SIZE);

	if (IS_ERR(act)) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot append QOS action");
		return PTR_ERR(act);
	}
	mlxsw_afa_qos_switch_prio_pack(act, MLXSW_AFA_QOS_SWITCH_PRIO_CMD_SET,
				       prio);
	return 0;
}
EXPORT_SYMBOL(mlxsw_afa_block_append_qos_switch_prio);

/* Forwarding Action
 * -----------------
 * Forwarding Action can be used to implement Policy Based Switching (PBS)
 * as well as OpenFlow related "Output" action.
 */

#define MLXSW_AFA_FORWARD_CODE 0x07
#define MLXSW_AFA_FORWARD_SIZE 1

enum mlxsw_afa_forward_type {
	/* PBS, Policy Based Switching */
	MLXSW_AFA_FORWARD_TYPE_PBS,
	/* Output, OpenFlow output type */
	MLXSW_AFA_FORWARD_TYPE_OUTPUT,
};

/* afa_forward_type */
MLXSW_ITEM32(afa, forward, type, 0x00, 24, 2);

/* afa_forward_pbs_ptr
 * A pointer to the PBS entry configured by PPBS register.
 * Reserved when in_port is set.
 */
MLXSW_ITEM32(afa, forward, pbs_ptr, 0x08, 0, 24);

/* afa_forward_in_port
 * Packet is forwarded back to the ingress port.
 */
MLXSW_ITEM32(afa, forward, in_port, 0x0C, 0, 1);

static inline void
mlxsw_afa_forward_pack(char *payload, enum mlxsw_afa_forward_type type,
		       u32 pbs_ptr, bool in_port)
{
	mlxsw_afa_forward_type_set(payload, type);
	mlxsw_afa_forward_pbs_ptr_set(payload, pbs_ptr);
	mlxsw_afa_forward_in_port_set(payload, in_port);
}

int mlxsw_afa_block_append_fwd(struct mlxsw_afa_block *block,
			       u8 local_port, bool in_port,
			       struct netlink_ext_ack *extack)
{
	struct mlxsw_afa_fwd_entry_ref *fwd_entry_ref;
	u32 kvdl_index;
	char *act;
	int err;

	if (in_port) {
		NL_SET_ERR_MSG_MOD(extack, "Forwarding to ingress port is not supported");
		return -EOPNOTSUPP;
	}
	fwd_entry_ref = mlxsw_afa_fwd_entry_ref_create(block, local_port);
	if (IS_ERR(fwd_entry_ref)) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot create forward action");
		return PTR_ERR(fwd_entry_ref);
	}
	kvdl_index = fwd_entry_ref->fwd_entry->kvdl_index;

	act = mlxsw_afa_block_append_action(block, MLXSW_AFA_FORWARD_CODE,
					    MLXSW_AFA_FORWARD_SIZE);
	if (IS_ERR(act)) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot append forward action");
		err = PTR_ERR(act);
		goto err_append_action;
	}
	mlxsw_afa_forward_pack(act, MLXSW_AFA_FORWARD_TYPE_PBS,
			       kvdl_index, in_port);
	return 0;

err_append_action:
	mlxsw_afa_fwd_entry_ref_destroy(block, fwd_entry_ref);
	return err;
}
EXPORT_SYMBOL(mlxsw_afa_block_append_fwd);

/* Policing and Counting Action
 * ----------------------------
 * Policing and Counting action is used for binding policer and counter
 * to ACL rules.
 */

#define MLXSW_AFA_POLCNT_CODE 0x08
#define MLXSW_AFA_POLCNT_SIZE 1

enum mlxsw_afa_polcnt_counter_set_type {
	/* No count */
	MLXSW_AFA_POLCNT_COUNTER_SET_TYPE_NO_COUNT = 0x00,
	/* Count packets and bytes */
	MLXSW_AFA_POLCNT_COUNTER_SET_TYPE_PACKETS_BYTES = 0x03,
	/* Count only packets */
	MLXSW_AFA_POLCNT_COUNTER_SET_TYPE_PACKETS = 0x05,
};

/* afa_polcnt_counter_set_type
 * Counter set type for flow counters.
 */
MLXSW_ITEM32(afa, polcnt, counter_set_type, 0x04, 24, 8);

/* afa_polcnt_counter_index
 * Counter index for flow counters.
 */
MLXSW_ITEM32(afa, polcnt, counter_index, 0x04, 0, 24);

static inline void
mlxsw_afa_polcnt_pack(char *payload,
		      enum mlxsw_afa_polcnt_counter_set_type set_type,
		      u32 counter_index)
{
	mlxsw_afa_polcnt_counter_set_type_set(payload, set_type);
	mlxsw_afa_polcnt_counter_index_set(payload, counter_index);
}

int mlxsw_afa_block_append_allocated_counter(struct mlxsw_afa_block *block,
					     u32 counter_index)
{
	char *act = mlxsw_afa_block_append_action(block, MLXSW_AFA_POLCNT_CODE,
						  MLXSW_AFA_POLCNT_SIZE);
	if (IS_ERR(act))
		return PTR_ERR(act);
	mlxsw_afa_polcnt_pack(act, MLXSW_AFA_POLCNT_COUNTER_SET_TYPE_PACKETS_BYTES,
			      counter_index);
	return 0;
}
EXPORT_SYMBOL(mlxsw_afa_block_append_allocated_counter);

int mlxsw_afa_block_append_counter(struct mlxsw_afa_block *block,
				   u32 *p_counter_index,
				   struct netlink_ext_ack *extack)
{
	struct mlxsw_afa_counter *counter;
	u32 counter_index;
	int err;

	counter = mlxsw_afa_counter_create(block);
	if (IS_ERR(counter)) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot create count action");
		return PTR_ERR(counter);
	}
	counter_index = counter->counter_index;

	err = mlxsw_afa_block_append_allocated_counter(block, counter_index);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot append count action");
		goto err_append_allocated_counter;
	}
	if (p_counter_index)
		*p_counter_index = counter_index;
	return 0;

err_append_allocated_counter:
	mlxsw_afa_counter_destroy(block, counter);
	return err;
}
EXPORT_SYMBOL(mlxsw_afa_block_append_counter);

/* Virtual Router and Forwarding Domain Action
 * -------------------------------------------
 * Virtual Switch action is used for manipulate the Virtual Router (VR),
 * MPLS label space and the Forwarding Identifier (FID).
 */

#define MLXSW_AFA_VIRFWD_CODE 0x0E
#define MLXSW_AFA_VIRFWD_SIZE 1

enum mlxsw_afa_virfwd_fid_cmd {
	/* Do nothing */
	MLXSW_AFA_VIRFWD_FID_CMD_NOOP,
	/* Set the Forwarding Identifier (FID) to fid */
	MLXSW_AFA_VIRFWD_FID_CMD_SET,
};

/* afa_virfwd_fid_cmd */
MLXSW_ITEM32(afa, virfwd, fid_cmd, 0x08, 29, 3);

/* afa_virfwd_fid
 * The FID value.
 */
MLXSW_ITEM32(afa, virfwd, fid, 0x08, 0, 16);

static inline void mlxsw_afa_virfwd_pack(char *payload,
					 enum mlxsw_afa_virfwd_fid_cmd fid_cmd,
					 u16 fid)
{
	mlxsw_afa_virfwd_fid_cmd_set(payload, fid_cmd);
	mlxsw_afa_virfwd_fid_set(payload, fid);
}

int mlxsw_afa_block_append_fid_set(struct mlxsw_afa_block *block, u16 fid,
				   struct netlink_ext_ack *extack)
{
	char *act = mlxsw_afa_block_append_action(block,
						  MLXSW_AFA_VIRFWD_CODE,
						  MLXSW_AFA_VIRFWD_SIZE);
	if (IS_ERR(act)) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot append fid_set action");
		return PTR_ERR(act);
	}
	mlxsw_afa_virfwd_pack(act, MLXSW_AFA_VIRFWD_FID_CMD_SET, fid);
	return 0;
}
EXPORT_SYMBOL(mlxsw_afa_block_append_fid_set);

/* MC Routing Action
 * -----------------
 * The Multicast router action. Can be used by RMFT_V2 - Router Multicast
 * Forwarding Table Version 2 Register.
 */

#define MLXSW_AFA_MCROUTER_CODE 0x10
#define MLXSW_AFA_MCROUTER_SIZE 2

enum mlxsw_afa_mcrouter_rpf_action {
	MLXSW_AFA_MCROUTER_RPF_ACTION_NOP,
	MLXSW_AFA_MCROUTER_RPF_ACTION_TRAP,
	MLXSW_AFA_MCROUTER_RPF_ACTION_DISCARD_ERROR,
};

/* afa_mcrouter_rpf_action */
MLXSW_ITEM32(afa, mcrouter, rpf_action, 0x00, 28, 3);

/* afa_mcrouter_expected_irif */
MLXSW_ITEM32(afa, mcrouter, expected_irif, 0x00, 0, 16);

/* afa_mcrouter_min_mtu */
MLXSW_ITEM32(afa, mcrouter, min_mtu, 0x08, 0, 16);

enum mlxsw_afa_mrouter_vrmid {
	MLXSW_AFA_MCROUTER_VRMID_INVALID,
	MLXSW_AFA_MCROUTER_VRMID_VALID
};

/* afa_mcrouter_vrmid
 * Valid RMID: rigr_rmid_index is used as RMID
 */
MLXSW_ITEM32(afa, mcrouter, vrmid, 0x0C, 31, 1);

/* afa_mcrouter_rigr_rmid_index
 * When the vrmid field is set to invalid, the field is used as pointer to
 * Router Interface Group (RIGR) Table in the KVD linear.
 * When the vrmid is set to valid, the field is used as RMID index, ranged
 * from 0 to max_mid - 1. The index is to the Port Group Table.
 */
MLXSW_ITEM32(afa, mcrouter, rigr_rmid_index, 0x0C, 0, 24);

static inline void
mlxsw_afa_mcrouter_pack(char *payload,
			enum mlxsw_afa_mcrouter_rpf_action rpf_action,
			u16 expected_irif, u16 min_mtu,
			enum mlxsw_afa_mrouter_vrmid vrmid, u32 rigr_rmid_index)

{
	mlxsw_afa_mcrouter_rpf_action_set(payload, rpf_action);
	mlxsw_afa_mcrouter_expected_irif_set(payload, expected_irif);
	mlxsw_afa_mcrouter_min_mtu_set(payload, min_mtu);
	mlxsw_afa_mcrouter_vrmid_set(payload, vrmid);
	mlxsw_afa_mcrouter_rigr_rmid_index_set(payload, rigr_rmid_index);
}

int mlxsw_afa_block_append_mcrouter(struct mlxsw_afa_block *block,
				    u16 expected_irif, u16 min_mtu,
				    bool rmid_valid, u32 kvdl_index)
{
	char *act = mlxsw_afa_block_append_action(block,
						  MLXSW_AFA_MCROUTER_CODE,
						  MLXSW_AFA_MCROUTER_SIZE);
	if (IS_ERR(act))
		return PTR_ERR(act);
	mlxsw_afa_mcrouter_pack(act, MLXSW_AFA_MCROUTER_RPF_ACTION_TRAP,
				expected_irif, min_mtu, rmid_valid, kvdl_index);
	return 0;
}
EXPORT_SYMBOL(mlxsw_afa_block_append_mcrouter);
