/*
 * Copyright (c) 2015, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/mutex.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/eswitch.h>

#include "mlx5_core.h"
#include "fs_core.h"
#include "fs_cmd.h"
#include "diag/fs_tracepoint.h"
#include "accel/ipsec.h"
#include "fpga/ipsec.h"

#define INIT_TREE_NODE_ARRAY_SIZE(...)	(sizeof((struct init_tree_node[]){__VA_ARGS__}) /\
					 sizeof(struct init_tree_node))

#define ADD_PRIO(num_prios_val, min_level_val, num_levels_val, caps_val,\
		 ...) {.type = FS_TYPE_PRIO,\
	.min_ft_level = min_level_val,\
	.num_levels = num_levels_val,\
	.num_leaf_prios = num_prios_val,\
	.caps = caps_val,\
	.children = (struct init_tree_node[]) {__VA_ARGS__},\
	.ar_size = INIT_TREE_NODE_ARRAY_SIZE(__VA_ARGS__) \
}

#define ADD_MULTIPLE_PRIO(num_prios_val, num_levels_val, ...)\
	ADD_PRIO(num_prios_val, 0, num_levels_val, {},\
		 __VA_ARGS__)\

#define ADD_NS(...) {.type = FS_TYPE_NAMESPACE,\
	.children = (struct init_tree_node[]) {__VA_ARGS__},\
	.ar_size = INIT_TREE_NODE_ARRAY_SIZE(__VA_ARGS__) \
}

#define INIT_CAPS_ARRAY_SIZE(...) (sizeof((long[]){__VA_ARGS__}) /\
				   sizeof(long))

#define FS_CAP(cap) (__mlx5_bit_off(flow_table_nic_cap, cap))

#define FS_REQUIRED_CAPS(...) {.arr_sz = INIT_CAPS_ARRAY_SIZE(__VA_ARGS__), \
			       .caps = (long[]) {__VA_ARGS__} }

#define FS_CHAINING_CAPS  FS_REQUIRED_CAPS(FS_CAP(flow_table_properties_nic_receive.flow_modify_en), \
					   FS_CAP(flow_table_properties_nic_receive.modify_root), \
					   FS_CAP(flow_table_properties_nic_receive.identified_miss_table_mode), \
					   FS_CAP(flow_table_properties_nic_receive.flow_table_modify))

#define LEFTOVERS_NUM_LEVELS 1
#define LEFTOVERS_NUM_PRIOS 1

#define BY_PASS_PRIO_NUM_LEVELS 1
#define BY_PASS_MIN_LEVEL (ETHTOOL_MIN_LEVEL + MLX5_BY_PASS_NUM_PRIOS +\
			   LEFTOVERS_NUM_PRIOS)

#define ETHTOOL_PRIO_NUM_LEVELS 1
#define ETHTOOL_NUM_PRIOS 11
#define ETHTOOL_MIN_LEVEL (KERNEL_MIN_LEVEL + ETHTOOL_NUM_PRIOS)
/* Vlan, mac, ttc, inner ttc, aRFS */
#define KERNEL_NIC_PRIO_NUM_LEVELS 5
#define KERNEL_NIC_NUM_PRIOS 1
/* One more level for tc */
#define KERNEL_MIN_LEVEL (KERNEL_NIC_PRIO_NUM_LEVELS + 1)

#define KERNEL_NIC_TC_NUM_PRIOS  1
#define KERNEL_NIC_TC_NUM_LEVELS 2

#define ANCHOR_NUM_LEVELS 1
#define ANCHOR_NUM_PRIOS 1
#define ANCHOR_MIN_LEVEL (BY_PASS_MIN_LEVEL + 1)

#define OFFLOADS_MAX_FT 1
#define OFFLOADS_NUM_PRIOS 1
#define OFFLOADS_MIN_LEVEL (ANCHOR_MIN_LEVEL + 1)

#define LAG_PRIO_NUM_LEVELS 1
#define LAG_NUM_PRIOS 1
#define LAG_MIN_LEVEL (OFFLOADS_MIN_LEVEL + 1)

struct node_caps {
	size_t	arr_sz;
	long	*caps;
};

static struct init_tree_node {
	enum fs_node_type	type;
	struct init_tree_node *children;
	int ar_size;
	struct node_caps caps;
	int min_ft_level;
	int num_leaf_prios;
	int prio;
	int num_levels;
} root_fs = {
	.type = FS_TYPE_NAMESPACE,
	.ar_size = 7,
	.children = (struct init_tree_node[]) {
		ADD_PRIO(0, BY_PASS_MIN_LEVEL, 0,
			 FS_CHAINING_CAPS,
			 ADD_NS(ADD_MULTIPLE_PRIO(MLX5_BY_PASS_NUM_PRIOS,
						  BY_PASS_PRIO_NUM_LEVELS))),
		ADD_PRIO(0, LAG_MIN_LEVEL, 0,
			 FS_CHAINING_CAPS,
			 ADD_NS(ADD_MULTIPLE_PRIO(LAG_NUM_PRIOS,
						  LAG_PRIO_NUM_LEVELS))),
		ADD_PRIO(0, OFFLOADS_MIN_LEVEL, 0, {},
			 ADD_NS(ADD_MULTIPLE_PRIO(OFFLOADS_NUM_PRIOS, OFFLOADS_MAX_FT))),
		ADD_PRIO(0, ETHTOOL_MIN_LEVEL, 0,
			 FS_CHAINING_CAPS,
			 ADD_NS(ADD_MULTIPLE_PRIO(ETHTOOL_NUM_PRIOS,
						  ETHTOOL_PRIO_NUM_LEVELS))),
		ADD_PRIO(0, KERNEL_MIN_LEVEL, 0, {},
			 ADD_NS(ADD_MULTIPLE_PRIO(KERNEL_NIC_TC_NUM_PRIOS, KERNEL_NIC_TC_NUM_LEVELS),
				ADD_MULTIPLE_PRIO(KERNEL_NIC_NUM_PRIOS,
						  KERNEL_NIC_PRIO_NUM_LEVELS))),
		ADD_PRIO(0, BY_PASS_MIN_LEVEL, 0,
			 FS_CHAINING_CAPS,
			 ADD_NS(ADD_MULTIPLE_PRIO(LEFTOVERS_NUM_PRIOS, LEFTOVERS_NUM_LEVELS))),
		ADD_PRIO(0, ANCHOR_MIN_LEVEL, 0, {},
			 ADD_NS(ADD_MULTIPLE_PRIO(ANCHOR_NUM_PRIOS, ANCHOR_NUM_LEVELS))),
	}
};

enum fs_i_lock_class {
	FS_LOCK_GRANDPARENT,
	FS_LOCK_PARENT,
	FS_LOCK_CHILD
};

static const struct rhashtable_params rhash_fte = {
	.key_len = FIELD_SIZEOF(struct fs_fte, val),
	.key_offset = offsetof(struct fs_fte, val),
	.head_offset = offsetof(struct fs_fte, hash),
	.automatic_shrinking = true,
	.min_size = 1,
};

static const struct rhashtable_params rhash_fg = {
	.key_len = FIELD_SIZEOF(struct mlx5_flow_group, mask),
	.key_offset = offsetof(struct mlx5_flow_group, mask),
	.head_offset = offsetof(struct mlx5_flow_group, hash),
	.automatic_shrinking = true,
	.min_size = 1,

};

static void del_hw_flow_table(struct fs_node *node);
static void del_hw_flow_group(struct fs_node *node);
static void del_hw_fte(struct fs_node *node);
static void del_sw_flow_table(struct fs_node *node);
static void del_sw_flow_group(struct fs_node *node);
static void del_sw_fte(struct fs_node *node);
static void del_sw_prio(struct fs_node *node);
static void del_sw_ns(struct fs_node *node);
/* Delete rule (destination) is special case that 
 * requires to lock the FTE for all the deletion process.
 */
static void del_sw_hw_rule(struct fs_node *node);
static bool mlx5_flow_dests_cmp(struct mlx5_flow_destination *d1,
				struct mlx5_flow_destination *d2);
static void cleanup_root_ns(struct mlx5_flow_root_namespace *root_ns);
static struct mlx5_flow_rule *
find_flow_rule(struct fs_fte *fte,
	       struct mlx5_flow_destination *dest);

static void tree_init_node(struct fs_node *node,
			   void (*del_hw_func)(struct fs_node *),
			   void (*del_sw_func)(struct fs_node *))
{
	refcount_set(&node->refcount, 1);
	INIT_LIST_HEAD(&node->list);
	INIT_LIST_HEAD(&node->children);
	init_rwsem(&node->lock);
	node->del_hw_func = del_hw_func;
	node->del_sw_func = del_sw_func;
	node->active = false;
}

static void tree_add_node(struct fs_node *node, struct fs_node *parent)
{
	if (parent)
		refcount_inc(&parent->refcount);
	node->parent = parent;

	/* Parent is the root */
	if (!parent)
		node->root = node;
	else
		node->root = parent->root;
}

static int tree_get_node(struct fs_node *node)
{
	return refcount_inc_not_zero(&node->refcount);
}

static void nested_down_read_ref_node(struct fs_node *node,
				      enum fs_i_lock_class class)
{
	if (node) {
		down_read_nested(&node->lock, class);
		refcount_inc(&node->refcount);
	}
}

static void nested_down_write_ref_node(struct fs_node *node,
				       enum fs_i_lock_class class)
{
	if (node) {
		down_write_nested(&node->lock, class);
		refcount_inc(&node->refcount);
	}
}

static void down_write_ref_node(struct fs_node *node)
{
	if (node) {
		down_write(&node->lock);
		refcount_inc(&node->refcount);
	}
}

static void up_read_ref_node(struct fs_node *node)
{
	refcount_dec(&node->refcount);
	up_read(&node->lock);
}

static void up_write_ref_node(struct fs_node *node)
{
	refcount_dec(&node->refcount);
	up_write(&node->lock);
}

static void tree_put_node(struct fs_node *node)
{
	struct fs_node *parent_node = node->parent;

	if (refcount_dec_and_test(&node->refcount)) {
		if (node->del_hw_func)
			node->del_hw_func(node);
		if (parent_node) {
			/* Only root namespace doesn't have parent and we just
			 * need to free its node.
			 */
			down_write_ref_node(parent_node);
			list_del_init(&node->list);
			if (node->del_sw_func)
				node->del_sw_func(node);
			up_write_ref_node(parent_node);
		} else {
			kfree(node);
		}
		node = NULL;
	}
	if (!node && parent_node)
		tree_put_node(parent_node);
}

static int tree_remove_node(struct fs_node *node)
{
	if (refcount_read(&node->refcount) > 1) {
		refcount_dec(&node->refcount);
		return -EEXIST;
	}
	tree_put_node(node);
	return 0;
}

static struct fs_prio *find_prio(struct mlx5_flow_namespace *ns,
				 unsigned int prio)
{
	struct fs_prio *iter_prio;

	fs_for_each_prio(iter_prio, ns) {
		if (iter_prio->prio == prio)
			return iter_prio;
	}

	return NULL;
}

static bool check_last_reserved(const u32 *match_criteria)
{
	char *match_criteria_reserved =
		MLX5_ADDR_OF(fte_match_param, match_criteria, MLX5_FTE_MATCH_PARAM_RESERVED);

	return	!match_criteria_reserved[0] &&
		!memcmp(match_criteria_reserved, match_criteria_reserved + 1,
			MLX5_FLD_SZ_BYTES(fte_match_param,
					  MLX5_FTE_MATCH_PARAM_RESERVED) - 1);
}

static bool check_valid_mask(u8 match_criteria_enable, const u32 *match_criteria)
{
	if (match_criteria_enable & ~(
		(1 << MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_OUTER_HEADERS)   |
		(1 << MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_MISC_PARAMETERS) |
		(1 << MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_INNER_HEADERS) |
		(1 << MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_MISC_PARAMETERS_2)))
		return false;

	if (!(match_criteria_enable &
	      1 << MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_OUTER_HEADERS)) {
		char *fg_type_mask = MLX5_ADDR_OF(fte_match_param,
						  match_criteria, outer_headers);

		if (fg_type_mask[0] ||
		    memcmp(fg_type_mask, fg_type_mask + 1,
			   MLX5_ST_SZ_BYTES(fte_match_set_lyr_2_4) - 1))
			return false;
	}

	if (!(match_criteria_enable &
	      1 << MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_MISC_PARAMETERS)) {
		char *fg_type_mask = MLX5_ADDR_OF(fte_match_param,
						  match_criteria, misc_parameters);

		if (fg_type_mask[0] ||
		    memcmp(fg_type_mask, fg_type_mask + 1,
			   MLX5_ST_SZ_BYTES(fte_match_set_misc) - 1))
			return false;
	}

	if (!(match_criteria_enable &
	      1 << MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_INNER_HEADERS)) {
		char *fg_type_mask = MLX5_ADDR_OF(fte_match_param,
						  match_criteria, inner_headers);

		if (fg_type_mask[0] ||
		    memcmp(fg_type_mask, fg_type_mask + 1,
			   MLX5_ST_SZ_BYTES(fte_match_set_lyr_2_4) - 1))
			return false;
	}

	if (!(match_criteria_enable &
	      1 << MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_MISC_PARAMETERS_2)) {
		char *fg_type_mask = MLX5_ADDR_OF(fte_match_param,
						  match_criteria, misc_parameters_2);

		if (fg_type_mask[0] ||
		    memcmp(fg_type_mask, fg_type_mask + 1,
			   MLX5_ST_SZ_BYTES(fte_match_set_misc2) - 1))
			return false;
	}

	return check_last_reserved(match_criteria);
}

static bool check_valid_spec(const struct mlx5_flow_spec *spec)
{
	int i;

	if (!check_valid_mask(spec->match_criteria_enable, spec->match_criteria)) {
		pr_warn("mlx5_core: Match criteria given mismatches match_criteria_enable\n");
		return false;
	}

	for (i = 0; i < MLX5_ST_SZ_DW_MATCH_PARAM; i++)
		if (spec->match_value[i] & ~spec->match_criteria[i]) {
			pr_warn("mlx5_core: match_value differs from match_criteria\n");
			return false;
		}

	return check_last_reserved(spec->match_value);
}

static struct mlx5_flow_root_namespace *find_root(struct fs_node *node)
{
	struct fs_node *root;
	struct mlx5_flow_namespace *ns;

	root = node->root;

	if (WARN_ON(root->type != FS_TYPE_NAMESPACE)) {
		pr_warn("mlx5: flow steering node is not in tree or garbaged\n");
		return NULL;
	}

	ns = container_of(root, struct mlx5_flow_namespace, node);
	return container_of(ns, struct mlx5_flow_root_namespace, ns);
}

static inline struct mlx5_flow_steering *get_steering(struct fs_node *node)
{
	struct mlx5_flow_root_namespace *root = find_root(node);

	if (root)
		return root->dev->priv.steering;
	return NULL;
}

static inline struct mlx5_core_dev *get_dev(struct fs_node *node)
{
	struct mlx5_flow_root_namespace *root = find_root(node);

	if (root)
		return root->dev;
	return NULL;
}

static void del_sw_ns(struct fs_node *node)
{
	kfree(node);
}

static void del_sw_prio(struct fs_node *node)
{
	kfree(node);
}

static void del_hw_flow_table(struct fs_node *node)
{
	struct mlx5_flow_root_namespace *root;
	struct mlx5_flow_table *ft;
	struct mlx5_core_dev *dev;
	int err;

	fs_get_obj(ft, node);
	dev = get_dev(&ft->node);
	root = find_root(&ft->node);

	if (node->active) {
		err = root->cmds->destroy_flow_table(dev, ft);
		if (err)
			mlx5_core_warn(dev, "flow steering can't destroy ft\n");
	}
}

static void del_sw_flow_table(struct fs_node *node)
{
	struct mlx5_flow_table *ft;
	struct fs_prio *prio;

	fs_get_obj(ft, node);

	rhltable_destroy(&ft->fgs_hash);
	fs_get_obj(prio, ft->node.parent);
	prio->num_ft--;
	kfree(ft);
}

static void del_sw_hw_rule(struct fs_node *node)
{
	struct mlx5_flow_root_namespace *root;
	struct mlx5_flow_rule *rule;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *fg;
	struct fs_fte *fte;
	int modify_mask;
	struct mlx5_core_dev *dev = get_dev(node);
	int err;
	bool update_fte = false;

	fs_get_obj(rule, node);
	fs_get_obj(fte, rule->node.parent);
	fs_get_obj(fg, fte->node.parent);
	fs_get_obj(ft, fg->node.parent);
	trace_mlx5_fs_del_rule(rule);
	if (rule->sw_action == MLX5_FLOW_CONTEXT_ACTION_FWD_NEXT_PRIO) {
		mutex_lock(&rule->dest_attr.ft->lock);
		list_del(&rule->next_ft);
		mutex_unlock(&rule->dest_attr.ft->lock);
	}

	if (rule->dest_attr.type == MLX5_FLOW_DESTINATION_TYPE_COUNTER  &&
	    --fte->dests_size) {
		modify_mask = BIT(MLX5_SET_FTE_MODIFY_ENABLE_MASK_ACTION) |
			      BIT(MLX5_SET_FTE_MODIFY_ENABLE_MASK_FLOW_COUNTERS);
		fte->action.action &= ~MLX5_FLOW_CONTEXT_ACTION_COUNT;
		update_fte = true;
		goto out;
	}

	if ((fte->action.action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST) &&
	    --fte->dests_size) {
		modify_mask = BIT(MLX5_SET_FTE_MODIFY_ENABLE_MASK_DESTINATION_LIST),
		update_fte = true;
	}
out:
	root = find_root(&ft->node);
	if (update_fte && fte->dests_size) {
		err = root->cmds->update_fte(dev, ft, fg->id, modify_mask, fte);
		if (err)
			mlx5_core_warn(dev,
				       "%s can't del rule fg id=%d fte_index=%d\n",
				       __func__, fg->id, fte->index);
	}
	kfree(rule);
}

static void del_hw_fte(struct fs_node *node)
{
	struct mlx5_flow_root_namespace *root;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *fg;
	struct mlx5_core_dev *dev;
	struct fs_fte *fte;
	int err;

	fs_get_obj(fte, node);
	fs_get_obj(fg, fte->node.parent);
	fs_get_obj(ft, fg->node.parent);

	trace_mlx5_fs_del_fte(fte);
	dev = get_dev(&ft->node);
	root = find_root(&ft->node);
	if (node->active) {
		err = root->cmds->delete_fte(dev, ft, fte);
		if (err)
			mlx5_core_warn(dev,
				       "flow steering can't delete fte in index %d of flow group id %d\n",
				       fte->index, fg->id);
	}
}

static void del_sw_fte(struct fs_node *node)
{
	struct mlx5_flow_steering *steering = get_steering(node);
	struct mlx5_flow_group *fg;
	struct fs_fte *fte;
	int err;

	fs_get_obj(fte, node);
	fs_get_obj(fg, fte->node.parent);

	err = rhashtable_remove_fast(&fg->ftes_hash,
				     &fte->hash,
				     rhash_fte);
	WARN_ON(err);
	ida_simple_remove(&fg->fte_allocator, fte->index - fg->start_index);
	kmem_cache_free(steering->ftes_cache, fte);
}

static void del_hw_flow_group(struct fs_node *node)
{
	struct mlx5_flow_root_namespace *root;
	struct mlx5_flow_group *fg;
	struct mlx5_flow_table *ft;
	struct mlx5_core_dev *dev;

	fs_get_obj(fg, node);
	fs_get_obj(ft, fg->node.parent);
	dev = get_dev(&ft->node);
	trace_mlx5_fs_del_fg(fg);

	root = find_root(&ft->node);
	if (fg->node.active && root->cmds->destroy_flow_group(dev, ft, fg->id))
		mlx5_core_warn(dev, "flow steering can't destroy fg %d of ft %d\n",
			       fg->id, ft->id);
}

static void del_sw_flow_group(struct fs_node *node)
{
	struct mlx5_flow_steering *steering = get_steering(node);
	struct mlx5_flow_group *fg;
	struct mlx5_flow_table *ft;
	int err;

	fs_get_obj(fg, node);
	fs_get_obj(ft, fg->node.parent);

	rhashtable_destroy(&fg->ftes_hash);
	ida_destroy(&fg->fte_allocator);
	if (ft->autogroup.active)
		ft->autogroup.num_groups--;
	err = rhltable_remove(&ft->fgs_hash,
			      &fg->hash,
			      rhash_fg);
	WARN_ON(err);
	kmem_cache_free(steering->fgs_cache, fg);
}

static int insert_fte(struct mlx5_flow_group *fg, struct fs_fte *fte)
{
	int index;
	int ret;

	index = ida_simple_get(&fg->fte_allocator, 0, fg->max_ftes, GFP_KERNEL);
	if (index < 0)
		return index;

	fte->index = index + fg->start_index;
	ret = rhashtable_insert_fast(&fg->ftes_hash,
				     &fte->hash,
				     rhash_fte);
	if (ret)
		goto err_ida_remove;

	tree_add_node(&fte->node, &fg->node);
	list_add_tail(&fte->node.list, &fg->node.children);
	return 0;

err_ida_remove:
	ida_simple_remove(&fg->fte_allocator, index);
	return ret;
}

static struct fs_fte *alloc_fte(struct mlx5_flow_table *ft,
				u32 *match_value,
				struct mlx5_flow_act *flow_act)
{
	struct mlx5_flow_steering *steering = get_steering(&ft->node);
	struct fs_fte *fte;

	fte = kmem_cache_zalloc(steering->ftes_cache, GFP_KERNEL);
	if (!fte)
		return ERR_PTR(-ENOMEM);

	memcpy(fte->val, match_value, sizeof(fte->val));
	fte->node.type =  FS_TYPE_FLOW_ENTRY;
	fte->action = *flow_act;

	tree_init_node(&fte->node, del_hw_fte, del_sw_fte);

	return fte;
}

static void dealloc_flow_group(struct mlx5_flow_steering *steering,
			       struct mlx5_flow_group *fg)
{
	rhashtable_destroy(&fg->ftes_hash);
	kmem_cache_free(steering->fgs_cache, fg);
}

static struct mlx5_flow_group *alloc_flow_group(struct mlx5_flow_steering *steering,
						u8 match_criteria_enable,
						void *match_criteria,
						int start_index,
						int end_index)
{
	struct mlx5_flow_group *fg;
	int ret;

	fg = kmem_cache_zalloc(steering->fgs_cache, GFP_KERNEL);
	if (!fg)
		return ERR_PTR(-ENOMEM);

	ret = rhashtable_init(&fg->ftes_hash, &rhash_fte);
	if (ret) {
		kmem_cache_free(steering->fgs_cache, fg);
		return ERR_PTR(ret);
}
	ida_init(&fg->fte_allocator);
	fg->mask.match_criteria_enable = match_criteria_enable;
	memcpy(&fg->mask.match_criteria, match_criteria,
	       sizeof(fg->mask.match_criteria));
	fg->node.type =  FS_TYPE_FLOW_GROUP;
	fg->start_index = start_index;
	fg->max_ftes = end_index - start_index + 1;

	return fg;
}

static struct mlx5_flow_group *alloc_insert_flow_group(struct mlx5_flow_table *ft,
						       u8 match_criteria_enable,
						       void *match_criteria,
						       int start_index,
						       int end_index,
						       struct list_head *prev)
{
	struct mlx5_flow_steering *steering = get_steering(&ft->node);
	struct mlx5_flow_group *fg;
	int ret;

	fg = alloc_flow_group(steering, match_criteria_enable, match_criteria,
			      start_index, end_index);
	if (IS_ERR(fg))
		return fg;

	/* initialize refcnt, add to parent list */
	ret = rhltable_insert(&ft->fgs_hash,
			      &fg->hash,
			      rhash_fg);
	if (ret) {
		dealloc_flow_group(steering, fg);
		return ERR_PTR(ret);
	}

	tree_init_node(&fg->node, del_hw_flow_group, del_sw_flow_group);
	tree_add_node(&fg->node, &ft->node);
	/* Add node to group list */
	list_add(&fg->node.list, prev);
	atomic_inc(&ft->node.version);

	return fg;
}

static struct mlx5_flow_table *alloc_flow_table(int level, u16 vport, int max_fte,
						enum fs_flow_table_type table_type,
						enum fs_flow_table_op_mod op_mod,
						u32 flags)
{
	struct mlx5_flow_table *ft;
	int ret;

	ft  = kzalloc(sizeof(*ft), GFP_KERNEL);
	if (!ft)
		return ERR_PTR(-ENOMEM);

	ret = rhltable_init(&ft->fgs_hash, &rhash_fg);
	if (ret) {
		kfree(ft);
		return ERR_PTR(ret);
	}

	ft->level = level;
	ft->node.type = FS_TYPE_FLOW_TABLE;
	ft->op_mod = op_mod;
	ft->type = table_type;
	ft->vport = vport;
	ft->max_fte = max_fte;
	ft->flags = flags;
	INIT_LIST_HEAD(&ft->fwd_rules);
	mutex_init(&ft->lock);

	return ft;
}

/* If reverse is false, then we search for the first flow table in the
 * root sub-tree from start(closest from right), else we search for the
 * last flow table in the root sub-tree till start(closest from left).
 */
static struct mlx5_flow_table *find_closest_ft_recursive(struct fs_node  *root,
							 struct list_head *start,
							 bool reverse)
{
#define list_advance_entry(pos, reverse)		\
	((reverse) ? list_prev_entry(pos, list) : list_next_entry(pos, list))

#define list_for_each_advance_continue(pos, head, reverse)	\
	for (pos = list_advance_entry(pos, reverse);		\
	     &pos->list != (head);				\
	     pos = list_advance_entry(pos, reverse))

	struct fs_node *iter = list_entry(start, struct fs_node, list);
	struct mlx5_flow_table *ft = NULL;

	if (!root)
		return NULL;

	list_for_each_advance_continue(iter, &root->children, reverse) {
		if (iter->type == FS_TYPE_FLOW_TABLE) {
			fs_get_obj(ft, iter);
			return ft;
		}
		ft = find_closest_ft_recursive(iter, &iter->children, reverse);
		if (ft)
			return ft;
	}

	return ft;
}

/* If reverse if false then return the first flow table in next priority of
 * prio in the tree, else return the last flow table in the previous priority
 * of prio in the tree.
 */
static struct mlx5_flow_table *find_closest_ft(struct fs_prio *prio, bool reverse)
{
	struct mlx5_flow_table *ft = NULL;
	struct fs_node *curr_node;
	struct fs_node *parent;

	parent = prio->node.parent;
	curr_node = &prio->node;
	while (!ft && parent) {
		ft = find_closest_ft_recursive(parent, &curr_node->list, reverse);
		curr_node = parent;
		parent = curr_node->parent;
	}
	return ft;
}

/* Assuming all the tree is locked by mutex chain lock */
static struct mlx5_flow_table *find_next_chained_ft(struct fs_prio *prio)
{
	return find_closest_ft(prio, false);
}

/* Assuming all the tree is locked by mutex chain lock */
static struct mlx5_flow_table *find_prev_chained_ft(struct fs_prio *prio)
{
	return find_closest_ft(prio, true);
}

static int connect_fts_in_prio(struct mlx5_core_dev *dev,
			       struct fs_prio *prio,
			       struct mlx5_flow_table *ft)
{
	struct mlx5_flow_root_namespace *root = find_root(&prio->node);
	struct mlx5_flow_table *iter;
	int i = 0;
	int err;

	fs_for_each_ft(iter, prio) {
		i++;
		err = root->cmds->modify_flow_table(dev, iter, ft);
		if (err) {
			mlx5_core_warn(dev, "Failed to modify flow table %d\n",
				       iter->id);
			/* The driver is out of sync with the FW */
			if (i > 1)
				WARN_ON(true);
			return err;
		}
	}
	return 0;
}

/* Connect flow tables from previous priority of prio to ft */
static int connect_prev_fts(struct mlx5_core_dev *dev,
			    struct mlx5_flow_table *ft,
			    struct fs_prio *prio)
{
	struct mlx5_flow_table *prev_ft;

	prev_ft = find_prev_chained_ft(prio);
	if (prev_ft) {
		struct fs_prio *prev_prio;

		fs_get_obj(prev_prio, prev_ft->node.parent);
		return connect_fts_in_prio(dev, prev_prio, ft);
	}
	return 0;
}

static int update_root_ft_create(struct mlx5_flow_table *ft, struct fs_prio
				 *prio)
{
	struct mlx5_flow_root_namespace *root = find_root(&prio->node);
	struct mlx5_ft_underlay_qp *uqp;
	int min_level = INT_MAX;
	int err;
	u32 qpn;

	if (root->root_ft)
		min_level = root->root_ft->level;

	if (ft->level >= min_level)
		return 0;

	if (list_empty(&root->underlay_qpns)) {
		/* Don't set any QPN (zero) in case QPN list is empty */
		qpn = 0;
		err = root->cmds->update_root_ft(root->dev, ft, qpn, false);
	} else {
		list_for_each_entry(uqp, &root->underlay_qpns, list) {
			qpn = uqp->qpn;
			err = root->cmds->update_root_ft(root->dev, ft,
							 qpn, false);
			if (err)
				break;
		}
	}

	if (err)
		mlx5_core_warn(root->dev,
			       "Update root flow table of id(%u) qpn(%d) failed\n",
			       ft->id, qpn);
	else
		root->root_ft = ft;

	return err;
}

static int _mlx5_modify_rule_destination(struct mlx5_flow_rule *rule,
					 struct mlx5_flow_destination *dest)
{
	struct mlx5_flow_root_namespace *root;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *fg;
	struct fs_fte *fte;
	int modify_mask = BIT(MLX5_SET_FTE_MODIFY_ENABLE_MASK_DESTINATION_LIST);
	int err = 0;

	fs_get_obj(fte, rule->node.parent);
	if (!(fte->action.action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST))
		return -EINVAL;
	down_write_ref_node(&fte->node);
	fs_get_obj(fg, fte->node.parent);
	fs_get_obj(ft, fg->node.parent);

	memcpy(&rule->dest_attr, dest, sizeof(*dest));
	root = find_root(&ft->node);
	err = root->cmds->update_fte(get_dev(&ft->node), ft, fg->id,
				     modify_mask, fte);
	up_write_ref_node(&fte->node);

	return err;
}

int mlx5_modify_rule_destination(struct mlx5_flow_handle *handle,
				 struct mlx5_flow_destination *new_dest,
				 struct mlx5_flow_destination *old_dest)
{
	int i;

	if (!old_dest) {
		if (handle->num_rules != 1)
			return -EINVAL;
		return _mlx5_modify_rule_destination(handle->rule[0],
						     new_dest);
	}

	for (i = 0; i < handle->num_rules; i++) {
		if (mlx5_flow_dests_cmp(new_dest, &handle->rule[i]->dest_attr))
			return _mlx5_modify_rule_destination(handle->rule[i],
							     new_dest);
	}

	return -EINVAL;
}

/* Modify/set FWD rules that point on old_next_ft to point on new_next_ft  */
static int connect_fwd_rules(struct mlx5_core_dev *dev,
			     struct mlx5_flow_table *new_next_ft,
			     struct mlx5_flow_table *old_next_ft)
{
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_rule *iter;
	int err = 0;

	/* new_next_ft and old_next_ft could be NULL only
	 * when we create/destroy the anchor flow table.
	 */
	if (!new_next_ft || !old_next_ft)
		return 0;

	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = new_next_ft;

	mutex_lock(&old_next_ft->lock);
	list_splice_init(&old_next_ft->fwd_rules, &new_next_ft->fwd_rules);
	mutex_unlock(&old_next_ft->lock);
	list_for_each_entry(iter, &new_next_ft->fwd_rules, next_ft) {
		err = _mlx5_modify_rule_destination(iter, &dest);
		if (err)
			pr_err("mlx5_core: failed to modify rule to point on flow table %d\n",
			       new_next_ft->id);
	}
	return 0;
}

static int connect_flow_table(struct mlx5_core_dev *dev, struct mlx5_flow_table *ft,
			      struct fs_prio *prio)
{
	struct mlx5_flow_table *next_ft;
	int err = 0;

	/* Connect_prev_fts and update_root_ft_create are mutually exclusive */

	if (list_empty(&prio->node.children)) {
		err = connect_prev_fts(dev, ft, prio);
		if (err)
			return err;

		next_ft = find_next_chained_ft(prio);
		err = connect_fwd_rules(dev, ft, next_ft);
		if (err)
			return err;
	}

	if (MLX5_CAP_FLOWTABLE(dev,
			       flow_table_properties_nic_receive.modify_root))
		err = update_root_ft_create(ft, prio);
	return err;
}

static void list_add_flow_table(struct mlx5_flow_table *ft,
				struct fs_prio *prio)
{
	struct list_head *prev = &prio->node.children;
	struct mlx5_flow_table *iter;

	fs_for_each_ft(iter, prio) {
		if (iter->level > ft->level)
			break;
		prev = &iter->node.list;
	}
	list_add(&ft->node.list, prev);
}

static struct mlx5_flow_table *__mlx5_create_flow_table(struct mlx5_flow_namespace *ns,
							struct mlx5_flow_table_attr *ft_attr,
							enum fs_flow_table_op_mod op_mod,
							u16 vport)
{
	struct mlx5_flow_root_namespace *root = find_root(&ns->node);
	struct mlx5_flow_table *next_ft = NULL;
	struct fs_prio *fs_prio = NULL;
	struct mlx5_flow_table *ft;
	int log_table_sz;
	int err;

	if (!root) {
		pr_err("mlx5: flow steering failed to find root of namespace\n");
		return ERR_PTR(-ENODEV);
	}

	mutex_lock(&root->chain_lock);
	fs_prio = find_prio(ns, ft_attr->prio);
	if (!fs_prio) {
		err = -EINVAL;
		goto unlock_root;
	}
	if (ft_attr->level >= fs_prio->num_levels) {
		err = -ENOSPC;
		goto unlock_root;
	}
	/* The level is related to the
	 * priority level range.
	 */
	ft_attr->level += fs_prio->start_level;
	ft = alloc_flow_table(ft_attr->level,
			      vport,
			      ft_attr->max_fte ? roundup_pow_of_two(ft_attr->max_fte) : 0,
			      root->table_type,
			      op_mod, ft_attr->flags);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		goto unlock_root;
	}

	tree_init_node(&ft->node, del_hw_flow_table, del_sw_flow_table);
	log_table_sz = ft->max_fte ? ilog2(ft->max_fte) : 0;
	next_ft = find_next_chained_ft(fs_prio);
	err = root->cmds->create_flow_table(root->dev, ft->vport, ft->op_mod,
					    ft->type, ft->level, log_table_sz,
					    next_ft, &ft->id, ft->flags);
	if (err)
		goto free_ft;

	err = connect_flow_table(root->dev, ft, fs_prio);
	if (err)
		goto destroy_ft;
	ft->node.active = true;
	down_write_ref_node(&fs_prio->node);
	tree_add_node(&ft->node, &fs_prio->node);
	list_add_flow_table(ft, fs_prio);
	fs_prio->num_ft++;
	up_write_ref_node(&fs_prio->node);
	mutex_unlock(&root->chain_lock);
	return ft;
destroy_ft:
	root->cmds->destroy_flow_table(root->dev, ft);
free_ft:
	kfree(ft);
unlock_root:
	mutex_unlock(&root->chain_lock);
	return ERR_PTR(err);
}

struct mlx5_flow_table *mlx5_create_flow_table(struct mlx5_flow_namespace *ns,
					       struct mlx5_flow_table_attr *ft_attr)
{
	return __mlx5_create_flow_table(ns, ft_attr, FS_FT_OP_MOD_NORMAL, 0);
}

struct mlx5_flow_table *mlx5_create_vport_flow_table(struct mlx5_flow_namespace *ns,
						     int prio, int max_fte,
						     u32 level, u16 vport)
{
	struct mlx5_flow_table_attr ft_attr = {};

	ft_attr.max_fte = max_fte;
	ft_attr.level   = level;
	ft_attr.prio    = prio;

	return __mlx5_create_flow_table(ns, &ft_attr, FS_FT_OP_MOD_NORMAL, vport);
}

struct mlx5_flow_table*
mlx5_create_lag_demux_flow_table(struct mlx5_flow_namespace *ns,
				 int prio, u32 level)
{
	struct mlx5_flow_table_attr ft_attr = {};

	ft_attr.level = level;
	ft_attr.prio  = prio;
	return __mlx5_create_flow_table(ns, &ft_attr, FS_FT_OP_MOD_LAG_DEMUX, 0);
}
EXPORT_SYMBOL(mlx5_create_lag_demux_flow_table);

struct mlx5_flow_table*
mlx5_create_auto_grouped_flow_table(struct mlx5_flow_namespace *ns,
				    int prio,
				    int num_flow_table_entries,
				    int max_num_groups,
				    u32 level,
				    u32 flags)
{
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_table *ft;

	if (max_num_groups > num_flow_table_entries)
		return ERR_PTR(-EINVAL);

	ft_attr.max_fte = num_flow_table_entries;
	ft_attr.prio    = prio;
	ft_attr.level   = level;
	ft_attr.flags   = flags;

	ft = mlx5_create_flow_table(ns, &ft_attr);
	if (IS_ERR(ft))
		return ft;

	ft->autogroup.active = true;
	ft->autogroup.required_groups = max_num_groups;

	return ft;
}
EXPORT_SYMBOL(mlx5_create_auto_grouped_flow_table);

struct mlx5_flow_group *mlx5_create_flow_group(struct mlx5_flow_table *ft,
					       u32 *fg_in)
{
	struct mlx5_flow_root_namespace *root = find_root(&ft->node);
	void *match_criteria = MLX5_ADDR_OF(create_flow_group_in,
					    fg_in, match_criteria);
	u8 match_criteria_enable = MLX5_GET(create_flow_group_in,
					    fg_in,
					    match_criteria_enable);
	int start_index = MLX5_GET(create_flow_group_in, fg_in,
				   start_flow_index);
	int end_index = MLX5_GET(create_flow_group_in, fg_in,
				 end_flow_index);
	struct mlx5_core_dev *dev = get_dev(&ft->node);
	struct mlx5_flow_group *fg;
	int err;

	if (!check_valid_mask(match_criteria_enable, match_criteria))
		return ERR_PTR(-EINVAL);

	if (ft->autogroup.active)
		return ERR_PTR(-EPERM);

	down_write_ref_node(&ft->node);
	fg = alloc_insert_flow_group(ft, match_criteria_enable, match_criteria,
				     start_index, end_index,
				     ft->node.children.prev);
	up_write_ref_node(&ft->node);
	if (IS_ERR(fg))
		return fg;

	err = root->cmds->create_flow_group(dev, ft, fg_in, &fg->id);
	if (err) {
		tree_put_node(&fg->node);
		return ERR_PTR(err);
	}
	trace_mlx5_fs_add_fg(fg);
	fg->node.active = true;

	return fg;
}

static struct mlx5_flow_rule *alloc_rule(struct mlx5_flow_destination *dest)
{
	struct mlx5_flow_rule *rule;

	rule = kzalloc(sizeof(*rule), GFP_KERNEL);
	if (!rule)
		return NULL;

	INIT_LIST_HEAD(&rule->next_ft);
	rule->node.type = FS_TYPE_FLOW_DEST;
	if (dest)
		memcpy(&rule->dest_attr, dest, sizeof(*dest));

	return rule;
}

static struct mlx5_flow_handle *alloc_handle(int num_rules)
{
	struct mlx5_flow_handle *handle;

	handle = kzalloc(struct_size(handle, rule, num_rules), GFP_KERNEL);
	if (!handle)
		return NULL;

	handle->num_rules = num_rules;

	return handle;
}

static void destroy_flow_handle(struct fs_fte *fte,
				struct mlx5_flow_handle *handle,
				struct mlx5_flow_destination *dest,
				int i)
{
	for (; --i >= 0;) {
		if (refcount_dec_and_test(&handle->rule[i]->node.refcount)) {
			fte->dests_size--;
			list_del(&handle->rule[i]->node.list);
			kfree(handle->rule[i]);
		}
	}
	kfree(handle);
}

static struct mlx5_flow_handle *
create_flow_handle(struct fs_fte *fte,
		   struct mlx5_flow_destination *dest,
		   int dest_num,
		   int *modify_mask,
		   bool *new_rule)
{
	struct mlx5_flow_handle *handle;
	struct mlx5_flow_rule *rule = NULL;
	static int count = BIT(MLX5_SET_FTE_MODIFY_ENABLE_MASK_FLOW_COUNTERS);
	static int dst = BIT(MLX5_SET_FTE_MODIFY_ENABLE_MASK_DESTINATION_LIST);
	int type;
	int i = 0;

	handle = alloc_handle((dest_num) ? dest_num : 1);
	if (!handle)
		return ERR_PTR(-ENOMEM);

	do {
		if (dest) {
			rule = find_flow_rule(fte, dest + i);
			if (rule) {
				refcount_inc(&rule->node.refcount);
				goto rule_found;
			}
		}

		*new_rule = true;
		rule = alloc_rule(dest + i);
		if (!rule)
			goto free_rules;

		/* Add dest to dests list- we need flow tables to be in the
		 * end of the list for forward to next prio rules.
		 */
		tree_init_node(&rule->node, NULL, del_sw_hw_rule);
		if (dest &&
		    dest[i].type != MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE)
			list_add(&rule->node.list, &fte->node.children);
		else
			list_add_tail(&rule->node.list, &fte->node.children);
		if (dest) {
			fte->dests_size++;

			type = dest[i].type ==
				MLX5_FLOW_DESTINATION_TYPE_COUNTER;
			*modify_mask |= type ? count : dst;
		}
rule_found:
		handle->rule[i] = rule;
	} while (++i < dest_num);

	return handle;

free_rules:
	destroy_flow_handle(fte, handle, dest, i);
	return ERR_PTR(-ENOMEM);
}

/* fte should not be deleted while calling this function */
static struct mlx5_flow_handle *
add_rule_fte(struct fs_fte *fte,
	     struct mlx5_flow_group *fg,
	     struct mlx5_flow_destination *dest,
	     int dest_num,
	     bool update_action)
{
	struct mlx5_flow_root_namespace *root;
	struct mlx5_flow_handle *handle;
	struct mlx5_flow_table *ft;
	int modify_mask = 0;
	int err;
	bool new_rule = false;

	handle = create_flow_handle(fte, dest, dest_num, &modify_mask,
				    &new_rule);
	if (IS_ERR(handle) || !new_rule)
		goto out;

	if (update_action)
		modify_mask |= BIT(MLX5_SET_FTE_MODIFY_ENABLE_MASK_ACTION);

	fs_get_obj(ft, fg->node.parent);
	root = find_root(&fg->node);
	if (!(fte->status & FS_FTE_STATUS_EXISTING))
		err = root->cmds->create_fte(get_dev(&ft->node),
					     ft, fg, fte);
	else
		err = root->cmds->update_fte(get_dev(&ft->node), ft, fg->id,
						     modify_mask, fte);
	if (err)
		goto free_handle;

	fte->node.active = true;
	fte->status |= FS_FTE_STATUS_EXISTING;
	atomic_inc(&fte->node.version);

out:
	return handle;

free_handle:
	destroy_flow_handle(fte, handle, dest, handle->num_rules);
	return ERR_PTR(err);
}

static struct mlx5_flow_group *alloc_auto_flow_group(struct mlx5_flow_table  *ft,
						     struct mlx5_flow_spec *spec)
{
	struct list_head *prev = &ft->node.children;
	struct mlx5_flow_group *fg;
	unsigned int candidate_index = 0;
	unsigned int group_size = 0;

	if (!ft->autogroup.active)
		return ERR_PTR(-ENOENT);

	if (ft->autogroup.num_groups < ft->autogroup.required_groups)
		/* We save place for flow groups in addition to max types */
		group_size = ft->max_fte / (ft->autogroup.required_groups + 1);

	/*  ft->max_fte == ft->autogroup.max_types */
	if (group_size == 0)
		group_size = 1;

	/* sorted by start_index */
	fs_for_each_fg(fg, ft) {
		if (candidate_index + group_size > fg->start_index)
			candidate_index = fg->start_index + fg->max_ftes;
		else
			break;
		prev = &fg->node.list;
	}

	if (candidate_index + group_size > ft->max_fte)
		return ERR_PTR(-ENOSPC);

	fg = alloc_insert_flow_group(ft,
				     spec->match_criteria_enable,
				     spec->match_criteria,
				     candidate_index,
				     candidate_index + group_size - 1,
				     prev);
	if (IS_ERR(fg))
		goto out;

	ft->autogroup.num_groups++;

out:
	return fg;
}

static int create_auto_flow_group(struct mlx5_flow_table *ft,
				  struct mlx5_flow_group *fg)
{
	struct mlx5_flow_root_namespace *root = find_root(&ft->node);
	struct mlx5_core_dev *dev = get_dev(&ft->node);
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	void *match_criteria_addr;
	u8 src_esw_owner_mask_on;
	void *misc;
	int err;
	u32 *in;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(create_flow_group_in, in, match_criteria_enable,
		 fg->mask.match_criteria_enable);
	MLX5_SET(create_flow_group_in, in, start_flow_index, fg->start_index);
	MLX5_SET(create_flow_group_in, in, end_flow_index,   fg->start_index +
		 fg->max_ftes - 1);

	misc = MLX5_ADDR_OF(fte_match_param, fg->mask.match_criteria,
			    misc_parameters);
	src_esw_owner_mask_on = !!MLX5_GET(fte_match_set_misc, misc,
					 source_eswitch_owner_vhca_id);
	MLX5_SET(create_flow_group_in, in,
		 source_eswitch_owner_vhca_id_valid, src_esw_owner_mask_on);

	match_criteria_addr = MLX5_ADDR_OF(create_flow_group_in,
					   in, match_criteria);
	memcpy(match_criteria_addr, fg->mask.match_criteria,
	       sizeof(fg->mask.match_criteria));

	err = root->cmds->create_flow_group(dev, ft, in, &fg->id);
	if (!err) {
		fg->node.active = true;
		trace_mlx5_fs_add_fg(fg);
	}

	kvfree(in);
	return err;
}

static bool mlx5_flow_dests_cmp(struct mlx5_flow_destination *d1,
				struct mlx5_flow_destination *d2)
{
	if (d1->type == d2->type) {
		if ((d1->type == MLX5_FLOW_DESTINATION_TYPE_VPORT &&
		     d1->vport.num == d2->vport.num) ||
		    (d1->type == MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE &&
		     d1->ft == d2->ft) ||
		    (d1->type == MLX5_FLOW_DESTINATION_TYPE_TIR &&
		     d1->tir_num == d2->tir_num))
			return true;
	}

	return false;
}

static struct mlx5_flow_rule *find_flow_rule(struct fs_fte *fte,
					     struct mlx5_flow_destination *dest)
{
	struct mlx5_flow_rule *rule;

	list_for_each_entry(rule, &fte->node.children, node.list) {
		if (mlx5_flow_dests_cmp(&rule->dest_attr, dest))
			return rule;
	}
	return NULL;
}

static bool check_conflicting_actions(u32 action1, u32 action2)
{
	u32 xored_actions = action1 ^ action2;

	/* if one rule only wants to count, it's ok */
	if (action1 == MLX5_FLOW_CONTEXT_ACTION_COUNT ||
	    action2 == MLX5_FLOW_CONTEXT_ACTION_COUNT)
		return false;

	if (xored_actions & (MLX5_FLOW_CONTEXT_ACTION_DROP  |
			     MLX5_FLOW_CONTEXT_ACTION_ENCAP |
			     MLX5_FLOW_CONTEXT_ACTION_DECAP |
			     MLX5_FLOW_CONTEXT_ACTION_MOD_HDR  |
			     MLX5_FLOW_CONTEXT_ACTION_VLAN_POP |
			     MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH))
		return true;

	return false;
}

static int check_conflicting_ftes(struct fs_fte *fte, const struct mlx5_flow_act *flow_act)
{
	if (check_conflicting_actions(flow_act->action, fte->action.action)) {
		mlx5_core_warn(get_dev(&fte->node),
			       "Found two FTEs with conflicting actions\n");
		return -EEXIST;
	}

	if (flow_act->has_flow_tag &&
	    fte->action.flow_tag != flow_act->flow_tag) {
		mlx5_core_warn(get_dev(&fte->node),
			       "FTE flow tag %u already exists with different flow tag %u\n",
			       fte->action.flow_tag,
			       flow_act->flow_tag);
		return -EEXIST;
	}

	return 0;
}

static struct mlx5_flow_handle *add_rule_fg(struct mlx5_flow_group *fg,
					    u32 *match_value,
					    struct mlx5_flow_act *flow_act,
					    struct mlx5_flow_destination *dest,
					    int dest_num,
					    struct fs_fte *fte)
{
	struct mlx5_flow_handle *handle;
	int old_action;
	int i;
	int ret;

	ret = check_conflicting_ftes(fte, flow_act);
	if (ret)
		return ERR_PTR(ret);

	old_action = fte->action.action;
	fte->action.action |= flow_act->action;
	handle = add_rule_fte(fte, fg, dest, dest_num,
			      old_action != flow_act->action);
	if (IS_ERR(handle)) {
		fte->action.action = old_action;
		return handle;
	}
	trace_mlx5_fs_set_fte(fte, false);

	for (i = 0; i < handle->num_rules; i++) {
		if (refcount_read(&handle->rule[i]->node.refcount) == 1) {
			tree_add_node(&handle->rule[i]->node, &fte->node);
			trace_mlx5_fs_add_rule(handle->rule[i]);
		}
	}
	return handle;
}

struct mlx5_fc *mlx5_flow_rule_counter(struct mlx5_flow_handle *handle)
{
	struct mlx5_flow_rule *dst;
	struct fs_fte *fte;

	fs_get_obj(fte, handle->rule[0]->node.parent);

	fs_for_each_dst(dst, fte) {
		if (dst->dest_attr.type == MLX5_FLOW_DESTINATION_TYPE_COUNTER)
			return dst->dest_attr.counter;
	}

	return NULL;
}

static bool counter_is_valid(struct mlx5_fc *counter, u32 action)
{
	if (!(action & MLX5_FLOW_CONTEXT_ACTION_COUNT))
		return !counter;

	if (!counter)
		return false;

	return (action & (MLX5_FLOW_CONTEXT_ACTION_DROP |
			  MLX5_FLOW_CONTEXT_ACTION_FWD_DEST));
}

static bool dest_is_valid(struct mlx5_flow_destination *dest,
			  u32 action,
			  struct mlx5_flow_table *ft)
{
	if (dest && (dest->type == MLX5_FLOW_DESTINATION_TYPE_COUNTER))
		return counter_is_valid(dest->counter, action);

	if (!(action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST))
		return true;

	if (!dest || ((dest->type ==
	    MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE) &&
	    (dest->ft->level <= ft->level)))
		return false;
	return true;
}

struct match_list {
	struct list_head	list;
	struct mlx5_flow_group *g;
};

struct match_list_head {
	struct list_head  list;
	struct match_list first;
};

static void free_match_list(struct match_list_head *head)
{
	if (!list_empty(&head->list)) {
		struct match_list *iter, *match_tmp;

		list_del(&head->first.list);
		tree_put_node(&head->first.g->node);
		list_for_each_entry_safe(iter, match_tmp, &head->list,
					 list) {
			tree_put_node(&iter->g->node);
			list_del(&iter->list);
			kfree(iter);
		}
	}
}

static int build_match_list(struct match_list_head *match_head,
			    struct mlx5_flow_table *ft,
			    struct mlx5_flow_spec *spec)
{
	struct rhlist_head *tmp, *list;
	struct mlx5_flow_group *g;
	int err = 0;

	rcu_read_lock();
	INIT_LIST_HEAD(&match_head->list);
	/* Collect all fgs which has a matching match_criteria */
	list = rhltable_lookup(&ft->fgs_hash, spec, rhash_fg);
	/* RCU is atomic, we can't execute FW commands here */
	rhl_for_each_entry_rcu(g, tmp, list, hash) {
		struct match_list *curr_match;

		if (likely(list_empty(&match_head->list))) {
			if (!tree_get_node(&g->node))
				continue;
			match_head->first.g = g;
			list_add_tail(&match_head->first.list,
				      &match_head->list);
			continue;
		}

		curr_match = kmalloc(sizeof(*curr_match), GFP_ATOMIC);
		if (!curr_match) {
			free_match_list(match_head);
			err = -ENOMEM;
			goto out;
		}
		if (!tree_get_node(&g->node)) {
			kfree(curr_match);
			continue;
		}
		curr_match->g = g;
		list_add_tail(&curr_match->list, &match_head->list);
	}
out:
	rcu_read_unlock();
	return err;
}

static u64 matched_fgs_get_version(struct list_head *match_head)
{
	struct match_list *iter;
	u64 version = 0;

	list_for_each_entry(iter, match_head, list)
		version += (u64)atomic_read(&iter->g->node.version);
	return version;
}

static struct mlx5_flow_handle *
try_add_to_existing_fg(struct mlx5_flow_table *ft,
		       struct list_head *match_head,
		       struct mlx5_flow_spec *spec,
		       struct mlx5_flow_act *flow_act,
		       struct mlx5_flow_destination *dest,
		       int dest_num,
		       int ft_version)
{
	struct mlx5_flow_steering *steering = get_steering(&ft->node);
	struct mlx5_flow_group *g;
	struct mlx5_flow_handle *rule;
	struct match_list *iter;
	bool take_write = false;
	struct fs_fte *fte;
	u64  version;
	int err;

	fte = alloc_fte(ft, spec->match_value, flow_act);
	if (IS_ERR(fte))
		return  ERR_PTR(-ENOMEM);

	list_for_each_entry(iter, match_head, list) {
		nested_down_read_ref_node(&iter->g->node, FS_LOCK_PARENT);
	}

search_again_locked:
	version = matched_fgs_get_version(match_head);
	/* Try to find a fg that already contains a matching fte */
	list_for_each_entry(iter, match_head, list) {
		struct fs_fte *fte_tmp;

		g = iter->g;
		fte_tmp = rhashtable_lookup_fast(&g->ftes_hash, spec->match_value,
						 rhash_fte);
		if (!fte_tmp || !tree_get_node(&fte_tmp->node))
			continue;

		nested_down_write_ref_node(&fte_tmp->node, FS_LOCK_CHILD);
		if (!take_write) {
			list_for_each_entry(iter, match_head, list)
				up_read_ref_node(&iter->g->node);
		} else {
			list_for_each_entry(iter, match_head, list)
				up_write_ref_node(&iter->g->node);
		}

		rule = add_rule_fg(g, spec->match_value,
				   flow_act, dest, dest_num, fte_tmp);
		up_write_ref_node(&fte_tmp->node);
		tree_put_node(&fte_tmp->node);
		kmem_cache_free(steering->ftes_cache, fte);
		return rule;
	}

	/* No group with matching fte found. Try to add a new fte to any
	 * matching fg.
	 */

	if (!take_write) {
		list_for_each_entry(iter, match_head, list)
			up_read_ref_node(&iter->g->node);
		list_for_each_entry(iter, match_head, list)
			nested_down_write_ref_node(&iter->g->node,
						   FS_LOCK_PARENT);
		take_write = true;
	}

	/* Check the ft version, for case that new flow group
	 * was added while the fgs weren't locked
	 */
	if (atomic_read(&ft->node.version) != ft_version) {
		rule = ERR_PTR(-EAGAIN);
		goto out;
	}

	/* Check the fgs version, for case the new FTE with the
	 * same values was added while the fgs weren't locked
	 */
	if (version != matched_fgs_get_version(match_head))
		goto search_again_locked;

	list_for_each_entry(iter, match_head, list) {
		g = iter->g;

		if (!g->node.active)
			continue;
		err = insert_fte(g, fte);
		if (err) {
			if (err == -ENOSPC)
				continue;
			list_for_each_entry(iter, match_head, list)
				up_write_ref_node(&iter->g->node);
			kmem_cache_free(steering->ftes_cache, fte);
			return ERR_PTR(err);
		}

		nested_down_write_ref_node(&fte->node, FS_LOCK_CHILD);
		list_for_each_entry(iter, match_head, list)
			up_write_ref_node(&iter->g->node);
		rule = add_rule_fg(g, spec->match_value,
				   flow_act, dest, dest_num, fte);
		up_write_ref_node(&fte->node);
		tree_put_node(&fte->node);
		return rule;
	}
	rule = ERR_PTR(-ENOENT);
out:
	list_for_each_entry(iter, match_head, list)
		up_write_ref_node(&iter->g->node);
	kmem_cache_free(steering->ftes_cache, fte);
	return rule;
}

static struct mlx5_flow_handle *
_mlx5_add_flow_rules(struct mlx5_flow_table *ft,
		     struct mlx5_flow_spec *spec,
		     struct mlx5_flow_act *flow_act,
		     struct mlx5_flow_destination *dest,
		     int dest_num)

{
	struct mlx5_flow_steering *steering = get_steering(&ft->node);
	struct mlx5_flow_group *g;
	struct mlx5_flow_handle *rule;
	struct match_list_head match_head;
	bool take_write = false;
	struct fs_fte *fte;
	int version;
	int err;
	int i;

	if (!check_valid_spec(spec))
		return ERR_PTR(-EINVAL);

	for (i = 0; i < dest_num; i++) {
		if (!dest_is_valid(&dest[i], flow_act->action, ft))
			return ERR_PTR(-EINVAL);
	}
	nested_down_read_ref_node(&ft->node, FS_LOCK_GRANDPARENT);
search_again_locked:
	version = atomic_read(&ft->node.version);

	/* Collect all fgs which has a matching match_criteria */
	err = build_match_list(&match_head, ft, spec);
	if (err) {
		if (take_write)
			up_write_ref_node(&ft->node);
		return ERR_PTR(err);
	}

	if (!take_write)
		up_read_ref_node(&ft->node);

	rule = try_add_to_existing_fg(ft, &match_head.list, spec, flow_act, dest,
				      dest_num, version);
	free_match_list(&match_head);
	if (!IS_ERR(rule) ||
	    (PTR_ERR(rule) != -ENOENT && PTR_ERR(rule) != -EAGAIN)) {
		if (take_write)
			up_write_ref_node(&ft->node);
		return rule;
	}

	if (!take_write) {
		nested_down_write_ref_node(&ft->node, FS_LOCK_GRANDPARENT);
		take_write = true;
	}

	if (PTR_ERR(rule) == -EAGAIN ||
	    version != atomic_read(&ft->node.version))
		goto search_again_locked;

	g = alloc_auto_flow_group(ft, spec);
	if (IS_ERR(g)) {
		rule = (void *)g;
		up_write_ref_node(&ft->node);
		return rule;
	}

	nested_down_write_ref_node(&g->node, FS_LOCK_PARENT);
	up_write_ref_node(&ft->node);

	err = create_auto_flow_group(ft, g);
	if (err)
		goto err_release_fg;

	fte = alloc_fte(ft, spec->match_value, flow_act);
	if (IS_ERR(fte)) {
		err = PTR_ERR(fte);
		goto err_release_fg;
	}

	err = insert_fte(g, fte);
	if (err) {
		kmem_cache_free(steering->ftes_cache, fte);
		goto err_release_fg;
	}

	nested_down_write_ref_node(&fte->node, FS_LOCK_CHILD);
	up_write_ref_node(&g->node);
	rule = add_rule_fg(g, spec->match_value, flow_act, dest,
			   dest_num, fte);
	up_write_ref_node(&fte->node);
	tree_put_node(&fte->node);
	tree_put_node(&g->node);
	return rule;

err_release_fg:
	up_write_ref_node(&g->node);
	tree_put_node(&g->node);
	return ERR_PTR(err);
}

static bool fwd_next_prio_supported(struct mlx5_flow_table *ft)
{
	return ((ft->type == FS_FT_NIC_RX) &&
		(MLX5_CAP_FLOWTABLE(get_dev(&ft->node), nic_rx_multi_path_tirs)));
}

struct mlx5_flow_handle *
mlx5_add_flow_rules(struct mlx5_flow_table *ft,
		    struct mlx5_flow_spec *spec,
		    struct mlx5_flow_act *flow_act,
		    struct mlx5_flow_destination *dest,
		    int dest_num)
{
	struct mlx5_flow_root_namespace *root = find_root(&ft->node);
	struct mlx5_flow_destination gen_dest = {};
	struct mlx5_flow_table *next_ft = NULL;
	struct mlx5_flow_handle *handle = NULL;
	u32 sw_action = flow_act->action;
	struct fs_prio *prio;

	fs_get_obj(prio, ft->node.parent);
	if (flow_act->action == MLX5_FLOW_CONTEXT_ACTION_FWD_NEXT_PRIO) {
		if (!fwd_next_prio_supported(ft))
			return ERR_PTR(-EOPNOTSUPP);
		if (dest_num)
			return ERR_PTR(-EINVAL);
		mutex_lock(&root->chain_lock);
		next_ft = find_next_chained_ft(prio);
		if (next_ft) {
			gen_dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
			gen_dest.ft = next_ft;
			dest = &gen_dest;
			dest_num = 1;
			flow_act->action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
		} else {
			mutex_unlock(&root->chain_lock);
			return ERR_PTR(-EOPNOTSUPP);
		}
	}

	handle = _mlx5_add_flow_rules(ft, spec, flow_act, dest, dest_num);

	if (sw_action == MLX5_FLOW_CONTEXT_ACTION_FWD_NEXT_PRIO) {
		if (!IS_ERR_OR_NULL(handle) &&
		    (list_empty(&handle->rule[0]->next_ft))) {
			mutex_lock(&next_ft->lock);
			list_add(&handle->rule[0]->next_ft,
				 &next_ft->fwd_rules);
			mutex_unlock(&next_ft->lock);
			handle->rule[0]->sw_action = MLX5_FLOW_CONTEXT_ACTION_FWD_NEXT_PRIO;
		}
		mutex_unlock(&root->chain_lock);
	}
	return handle;
}
EXPORT_SYMBOL(mlx5_add_flow_rules);

void mlx5_del_flow_rules(struct mlx5_flow_handle *handle)
{
	int i;

	for (i = handle->num_rules - 1; i >= 0; i--)
		tree_remove_node(&handle->rule[i]->node);
	kfree(handle);
}
EXPORT_SYMBOL(mlx5_del_flow_rules);

/* Assuming prio->node.children(flow tables) is sorted by level */
static struct mlx5_flow_table *find_next_ft(struct mlx5_flow_table *ft)
{
	struct fs_prio *prio;

	fs_get_obj(prio, ft->node.parent);

	if (!list_is_last(&ft->node.list, &prio->node.children))
		return list_next_entry(ft, node.list);
	return find_next_chained_ft(prio);
}

static int update_root_ft_destroy(struct mlx5_flow_table *ft)
{
	struct mlx5_flow_root_namespace *root = find_root(&ft->node);
	struct mlx5_ft_underlay_qp *uqp;
	struct mlx5_flow_table *new_root_ft = NULL;
	int err = 0;
	u32 qpn;

	if (root->root_ft != ft)
		return 0;

	new_root_ft = find_next_ft(ft);
	if (!new_root_ft) {
		root->root_ft = NULL;
		return 0;
	}

	if (list_empty(&root->underlay_qpns)) {
		/* Don't set any QPN (zero) in case QPN list is empty */
		qpn = 0;
		err = root->cmds->update_root_ft(root->dev, new_root_ft,
						 qpn, false);
	} else {
		list_for_each_entry(uqp, &root->underlay_qpns, list) {
			qpn = uqp->qpn;
			err = root->cmds->update_root_ft(root->dev,
							 new_root_ft, qpn,
							 false);
			if (err)
				break;
		}
	}

	if (err)
		mlx5_core_warn(root->dev,
			       "Update root flow table of id(%u) qpn(%d) failed\n",
			       ft->id, qpn);
	else
		root->root_ft = new_root_ft;

	return 0;
}

/* Connect flow table from previous priority to
 * the next flow table.
 */
static int disconnect_flow_table(struct mlx5_flow_table *ft)
{
	struct mlx5_core_dev *dev = get_dev(&ft->node);
	struct mlx5_flow_table *next_ft;
	struct fs_prio *prio;
	int err = 0;

	err = update_root_ft_destroy(ft);
	if (err)
		return err;

	fs_get_obj(prio, ft->node.parent);
	if  (!(list_first_entry(&prio->node.children,
				struct mlx5_flow_table,
				node.list) == ft))
		return 0;

	next_ft = find_next_chained_ft(prio);
	err = connect_fwd_rules(dev, next_ft, ft);
	if (err)
		return err;

	err = connect_prev_fts(dev, next_ft, prio);
	if (err)
		mlx5_core_warn(dev, "Failed to disconnect flow table %d\n",
			       ft->id);
	return err;
}

int mlx5_destroy_flow_table(struct mlx5_flow_table *ft)
{
	struct mlx5_flow_root_namespace *root = find_root(&ft->node);
	int err = 0;

	mutex_lock(&root->chain_lock);
	err = disconnect_flow_table(ft);
	if (err) {
		mutex_unlock(&root->chain_lock);
		return err;
	}
	if (tree_remove_node(&ft->node))
		mlx5_core_warn(get_dev(&ft->node), "Flow table %d wasn't destroyed, refcount > 1\n",
			       ft->id);
	mutex_unlock(&root->chain_lock);

	return err;
}
EXPORT_SYMBOL(mlx5_destroy_flow_table);

void mlx5_destroy_flow_group(struct mlx5_flow_group *fg)
{
	if (tree_remove_node(&fg->node))
		mlx5_core_warn(get_dev(&fg->node), "Flow group %d wasn't destroyed, refcount > 1\n",
			       fg->id);
}

struct mlx5_flow_namespace *mlx5_get_flow_namespace(struct mlx5_core_dev *dev,
						    enum mlx5_flow_namespace_type type)
{
	struct mlx5_flow_steering *steering = dev->priv.steering;
	struct mlx5_flow_root_namespace *root_ns;
	int prio;
	struct fs_prio *fs_prio;
	struct mlx5_flow_namespace *ns;

	if (!steering)
		return NULL;

	switch (type) {
	case MLX5_FLOW_NAMESPACE_BYPASS:
	case MLX5_FLOW_NAMESPACE_LAG:
	case MLX5_FLOW_NAMESPACE_OFFLOADS:
	case MLX5_FLOW_NAMESPACE_ETHTOOL:
	case MLX5_FLOW_NAMESPACE_KERNEL:
	case MLX5_FLOW_NAMESPACE_LEFTOVERS:
	case MLX5_FLOW_NAMESPACE_ANCHOR:
		prio = type;
		break;
	case MLX5_FLOW_NAMESPACE_FDB:
		if (steering->fdb_root_ns)
			return &steering->fdb_root_ns->ns;
		else
			return NULL;
	case MLX5_FLOW_NAMESPACE_SNIFFER_RX:
		if (steering->sniffer_rx_root_ns)
			return &steering->sniffer_rx_root_ns->ns;
		else
			return NULL;
	case MLX5_FLOW_NAMESPACE_SNIFFER_TX:
		if (steering->sniffer_tx_root_ns)
			return &steering->sniffer_tx_root_ns->ns;
		else
			return NULL;
	case MLX5_FLOW_NAMESPACE_EGRESS:
		if (steering->egress_root_ns)
			return &steering->egress_root_ns->ns;
		else
			return NULL;
	default:
		return NULL;
	}

	root_ns = steering->root_ns;
	if (!root_ns)
		return NULL;

	fs_prio = find_prio(&root_ns->ns, prio);
	if (!fs_prio)
		return NULL;

	ns = list_first_entry(&fs_prio->node.children,
			      typeof(*ns),
			      node.list);

	return ns;
}
EXPORT_SYMBOL(mlx5_get_flow_namespace);

struct mlx5_flow_namespace *mlx5_get_flow_vport_acl_namespace(struct mlx5_core_dev *dev,
							      enum mlx5_flow_namespace_type type,
							      int vport)
{
	struct mlx5_flow_steering *steering = dev->priv.steering;

	if (!steering || vport >= MLX5_TOTAL_VPORTS(dev))
		return NULL;

	switch (type) {
	case MLX5_FLOW_NAMESPACE_ESW_EGRESS:
		if (steering->esw_egress_root_ns &&
		    steering->esw_egress_root_ns[vport])
			return &steering->esw_egress_root_ns[vport]->ns;
		else
			return NULL;
	case MLX5_FLOW_NAMESPACE_ESW_INGRESS:
		if (steering->esw_ingress_root_ns &&
		    steering->esw_ingress_root_ns[vport])
			return &steering->esw_ingress_root_ns[vport]->ns;
		else
			return NULL;
	default:
		return NULL;
	}
}

static struct fs_prio *fs_create_prio(struct mlx5_flow_namespace *ns,
				      unsigned int prio, int num_levels)
{
	struct fs_prio *fs_prio;

	fs_prio = kzalloc(sizeof(*fs_prio), GFP_KERNEL);
	if (!fs_prio)
		return ERR_PTR(-ENOMEM);

	fs_prio->node.type = FS_TYPE_PRIO;
	tree_init_node(&fs_prio->node, NULL, del_sw_prio);
	tree_add_node(&fs_prio->node, &ns->node);
	fs_prio->num_levels = num_levels;
	fs_prio->prio = prio;
	list_add_tail(&fs_prio->node.list, &ns->node.children);

	return fs_prio;
}

static struct mlx5_flow_namespace *fs_init_namespace(struct mlx5_flow_namespace
						     *ns)
{
	ns->node.type = FS_TYPE_NAMESPACE;

	return ns;
}

static struct mlx5_flow_namespace *fs_create_namespace(struct fs_prio *prio)
{
	struct mlx5_flow_namespace	*ns;

	ns = kzalloc(sizeof(*ns), GFP_KERNEL);
	if (!ns)
		return ERR_PTR(-ENOMEM);

	fs_init_namespace(ns);
	tree_init_node(&ns->node, NULL, del_sw_ns);
	tree_add_node(&ns->node, &prio->node);
	list_add_tail(&ns->node.list, &prio->node.children);

	return ns;
}

static int create_leaf_prios(struct mlx5_flow_namespace *ns, int prio,
			     struct init_tree_node *prio_metadata)
{
	struct fs_prio *fs_prio;
	int i;

	for (i = 0; i < prio_metadata->num_leaf_prios; i++) {
		fs_prio = fs_create_prio(ns, prio++, prio_metadata->num_levels);
		if (IS_ERR(fs_prio))
			return PTR_ERR(fs_prio);
	}
	return 0;
}

#define FLOW_TABLE_BIT_SZ 1
#define GET_FLOW_TABLE_CAP(dev, offset) \
	((be32_to_cpu(*((__be32 *)(dev->caps.hca_cur[MLX5_CAP_FLOW_TABLE]) +	\
			offset / 32)) >>					\
	  (32 - FLOW_TABLE_BIT_SZ - (offset & 0x1f))) & FLOW_TABLE_BIT_SZ)
static bool has_required_caps(struct mlx5_core_dev *dev, struct node_caps *caps)
{
	int i;

	for (i = 0; i < caps->arr_sz; i++) {
		if (!GET_FLOW_TABLE_CAP(dev, caps->caps[i]))
			return false;
	}
	return true;
}

static int init_root_tree_recursive(struct mlx5_flow_steering *steering,
				    struct init_tree_node *init_node,
				    struct fs_node *fs_parent_node,
				    struct init_tree_node *init_parent_node,
				    int prio)
{
	int max_ft_level = MLX5_CAP_FLOWTABLE(steering->dev,
					      flow_table_properties_nic_receive.
					      max_ft_level);
	struct mlx5_flow_namespace *fs_ns;
	struct fs_prio *fs_prio;
	struct fs_node *base;
	int i;
	int err;

	if (init_node->type == FS_TYPE_PRIO) {
		if ((init_node->min_ft_level > max_ft_level) ||
		    !has_required_caps(steering->dev, &init_node->caps))
			return 0;

		fs_get_obj(fs_ns, fs_parent_node);
		if (init_node->num_leaf_prios)
			return create_leaf_prios(fs_ns, prio, init_node);
		fs_prio = fs_create_prio(fs_ns, prio, init_node->num_levels);
		if (IS_ERR(fs_prio))
			return PTR_ERR(fs_prio);
		base = &fs_prio->node;
	} else if (init_node->type == FS_TYPE_NAMESPACE) {
		fs_get_obj(fs_prio, fs_parent_node);
		fs_ns = fs_create_namespace(fs_prio);
		if (IS_ERR(fs_ns))
			return PTR_ERR(fs_ns);
		base = &fs_ns->node;
	} else {
		return -EINVAL;
	}
	prio = 0;
	for (i = 0; i < init_node->ar_size; i++) {
		err = init_root_tree_recursive(steering, &init_node->children[i],
					       base, init_node, prio);
		if (err)
			return err;
		if (init_node->children[i].type == FS_TYPE_PRIO &&
		    init_node->children[i].num_leaf_prios) {
			prio += init_node->children[i].num_leaf_prios;
		}
	}

	return 0;
}

static int init_root_tree(struct mlx5_flow_steering *steering,
			  struct init_tree_node *init_node,
			  struct fs_node *fs_parent_node)
{
	int i;
	struct mlx5_flow_namespace *fs_ns;
	int err;

	fs_get_obj(fs_ns, fs_parent_node);
	for (i = 0; i < init_node->ar_size; i++) {
		err = init_root_tree_recursive(steering, &init_node->children[i],
					       &fs_ns->node,
					       init_node, i);
		if (err)
			return err;
	}
	return 0;
}

static struct mlx5_flow_root_namespace
*create_root_ns(struct mlx5_flow_steering *steering,
		enum fs_flow_table_type table_type)
{
	const struct mlx5_flow_cmds *cmds = mlx5_fs_cmd_get_default(table_type);
	struct mlx5_flow_root_namespace *root_ns;
	struct mlx5_flow_namespace *ns;

	if (mlx5_accel_ipsec_device_caps(steering->dev) & MLX5_ACCEL_IPSEC_CAP_DEVICE &&
	    (table_type == FS_FT_NIC_RX || table_type == FS_FT_NIC_TX))
		cmds = mlx5_fs_cmd_get_default_ipsec_fpga_cmds(table_type);

	/* Create the root namespace */
	root_ns = kvzalloc(sizeof(*root_ns), GFP_KERNEL);
	if (!root_ns)
		return NULL;

	root_ns->dev = steering->dev;
	root_ns->table_type = table_type;
	root_ns->cmds = cmds;

	INIT_LIST_HEAD(&root_ns->underlay_qpns);

	ns = &root_ns->ns;
	fs_init_namespace(ns);
	mutex_init(&root_ns->chain_lock);
	tree_init_node(&ns->node, NULL, NULL);
	tree_add_node(&ns->node, NULL);

	return root_ns;
}

static void set_prio_attrs_in_prio(struct fs_prio *prio, int acc_level);

static int set_prio_attrs_in_ns(struct mlx5_flow_namespace *ns, int acc_level)
{
	struct fs_prio *prio;

	fs_for_each_prio(prio, ns) {
		 /* This updates prio start_level and num_levels */
		set_prio_attrs_in_prio(prio, acc_level);
		acc_level += prio->num_levels;
	}
	return acc_level;
}

static void set_prio_attrs_in_prio(struct fs_prio *prio, int acc_level)
{
	struct mlx5_flow_namespace *ns;
	int acc_level_ns = acc_level;

	prio->start_level = acc_level;
	fs_for_each_ns(ns, prio)
		/* This updates start_level and num_levels of ns's priority descendants */
		acc_level_ns = set_prio_attrs_in_ns(ns, acc_level);
	if (!prio->num_levels)
		prio->num_levels = acc_level_ns - prio->start_level;
	WARN_ON(prio->num_levels < acc_level_ns - prio->start_level);
}

static void set_prio_attrs(struct mlx5_flow_root_namespace *root_ns)
{
	struct mlx5_flow_namespace *ns = &root_ns->ns;
	struct fs_prio *prio;
	int start_level = 0;

	fs_for_each_prio(prio, ns) {
		set_prio_attrs_in_prio(prio, start_level);
		start_level += prio->num_levels;
	}
}

#define ANCHOR_PRIO 0
#define ANCHOR_SIZE 1
#define ANCHOR_LEVEL 0
static int create_anchor_flow_table(struct mlx5_flow_steering *steering)
{
	struct mlx5_flow_namespace *ns = NULL;
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_table *ft;

	ns = mlx5_get_flow_namespace(steering->dev, MLX5_FLOW_NAMESPACE_ANCHOR);
	if (WARN_ON(!ns))
		return -EINVAL;

	ft_attr.max_fte = ANCHOR_SIZE;
	ft_attr.level   = ANCHOR_LEVEL;
	ft_attr.prio    = ANCHOR_PRIO;

	ft = mlx5_create_flow_table(ns, &ft_attr);
	if (IS_ERR(ft)) {
		mlx5_core_err(steering->dev, "Failed to create last anchor flow table");
		return PTR_ERR(ft);
	}
	return 0;
}

static int init_root_ns(struct mlx5_flow_steering *steering)
{
	int err;

	steering->root_ns = create_root_ns(steering, FS_FT_NIC_RX);
	if (!steering->root_ns)
		return -ENOMEM;

	err = init_root_tree(steering, &root_fs, &steering->root_ns->ns.node);
	if (err)
		goto out_err;

	set_prio_attrs(steering->root_ns);
	err = create_anchor_flow_table(steering);
	if (err)
		goto out_err;

	return 0;

out_err:
	cleanup_root_ns(steering->root_ns);
	steering->root_ns = NULL;
	return err;
}

static void clean_tree(struct fs_node *node)
{
	if (node) {
		struct fs_node *iter;
		struct fs_node *temp;

		tree_get_node(node);
		list_for_each_entry_safe(iter, temp, &node->children, list)
			clean_tree(iter);
		tree_put_node(node);
		tree_remove_node(node);
	}
}

static void cleanup_root_ns(struct mlx5_flow_root_namespace *root_ns)
{
	if (!root_ns)
		return;

	clean_tree(&root_ns->ns.node);
}

static void cleanup_egress_acls_root_ns(struct mlx5_core_dev *dev)
{
	struct mlx5_flow_steering *steering = dev->priv.steering;
	int i;

	if (!steering->esw_egress_root_ns)
		return;

	for (i = 0; i < MLX5_TOTAL_VPORTS(dev); i++)
		cleanup_root_ns(steering->esw_egress_root_ns[i]);

	kfree(steering->esw_egress_root_ns);
}

static void cleanup_ingress_acls_root_ns(struct mlx5_core_dev *dev)
{
	struct mlx5_flow_steering *steering = dev->priv.steering;
	int i;

	if (!steering->esw_ingress_root_ns)
		return;

	for (i = 0; i < MLX5_TOTAL_VPORTS(dev); i++)
		cleanup_root_ns(steering->esw_ingress_root_ns[i]);

	kfree(steering->esw_ingress_root_ns);
}

void mlx5_cleanup_fs(struct mlx5_core_dev *dev)
{
	struct mlx5_flow_steering *steering = dev->priv.steering;

	cleanup_root_ns(steering->root_ns);
	cleanup_egress_acls_root_ns(dev);
	cleanup_ingress_acls_root_ns(dev);
	cleanup_root_ns(steering->fdb_root_ns);
	cleanup_root_ns(steering->sniffer_rx_root_ns);
	cleanup_root_ns(steering->sniffer_tx_root_ns);
	cleanup_root_ns(steering->egress_root_ns);
	mlx5_cleanup_fc_stats(dev);
	kmem_cache_destroy(steering->ftes_cache);
	kmem_cache_destroy(steering->fgs_cache);
	kfree(steering);
}

static int init_sniffer_tx_root_ns(struct mlx5_flow_steering *steering)
{
	struct fs_prio *prio;

	steering->sniffer_tx_root_ns = create_root_ns(steering, FS_FT_SNIFFER_TX);
	if (!steering->sniffer_tx_root_ns)
		return -ENOMEM;

	/* Create single prio */
	prio = fs_create_prio(&steering->sniffer_tx_root_ns->ns, 0, 1);
	if (IS_ERR(prio)) {
		cleanup_root_ns(steering->sniffer_tx_root_ns);
		return PTR_ERR(prio);
	}
	return 0;
}

static int init_sniffer_rx_root_ns(struct mlx5_flow_steering *steering)
{
	struct fs_prio *prio;

	steering->sniffer_rx_root_ns = create_root_ns(steering, FS_FT_SNIFFER_RX);
	if (!steering->sniffer_rx_root_ns)
		return -ENOMEM;

	/* Create single prio */
	prio = fs_create_prio(&steering->sniffer_rx_root_ns->ns, 0, 1);
	if (IS_ERR(prio)) {
		cleanup_root_ns(steering->sniffer_rx_root_ns);
		return PTR_ERR(prio);
	}
	return 0;
}

static int init_fdb_root_ns(struct mlx5_flow_steering *steering)
{
	struct fs_prio *prio;

	steering->fdb_root_ns = create_root_ns(steering, FS_FT_FDB);
	if (!steering->fdb_root_ns)
		return -ENOMEM;

	prio = fs_create_prio(&steering->fdb_root_ns->ns, 0, 2);
	if (IS_ERR(prio))
		goto out_err;

	prio = fs_create_prio(&steering->fdb_root_ns->ns, 1, 1);
	if (IS_ERR(prio))
		goto out_err;

	set_prio_attrs(steering->fdb_root_ns);
	return 0;

out_err:
	cleanup_root_ns(steering->fdb_root_ns);
	steering->fdb_root_ns = NULL;
	return PTR_ERR(prio);
}

static int init_egress_acl_root_ns(struct mlx5_flow_steering *steering, int vport)
{
	struct fs_prio *prio;

	steering->esw_egress_root_ns[vport] = create_root_ns(steering, FS_FT_ESW_EGRESS_ACL);
	if (!steering->esw_egress_root_ns[vport])
		return -ENOMEM;

	/* create 1 prio*/
	prio = fs_create_prio(&steering->esw_egress_root_ns[vport]->ns, 0, 1);
	return PTR_ERR_OR_ZERO(prio);
}

static int init_ingress_acl_root_ns(struct mlx5_flow_steering *steering, int vport)
{
	struct fs_prio *prio;

	steering->esw_ingress_root_ns[vport] = create_root_ns(steering, FS_FT_ESW_INGRESS_ACL);
	if (!steering->esw_ingress_root_ns[vport])
		return -ENOMEM;

	/* create 1 prio*/
	prio = fs_create_prio(&steering->esw_ingress_root_ns[vport]->ns, 0, 1);
	return PTR_ERR_OR_ZERO(prio);
}

static int init_egress_acls_root_ns(struct mlx5_core_dev *dev)
{
	struct mlx5_flow_steering *steering = dev->priv.steering;
	int err;
	int i;

	steering->esw_egress_root_ns = kcalloc(MLX5_TOTAL_VPORTS(dev),
					       sizeof(*steering->esw_egress_root_ns),
					       GFP_KERNEL);
	if (!steering->esw_egress_root_ns)
		return -ENOMEM;

	for (i = 0; i < MLX5_TOTAL_VPORTS(dev); i++) {
		err = init_egress_acl_root_ns(steering, i);
		if (err)
			goto cleanup_root_ns;
	}

	return 0;

cleanup_root_ns:
	for (i--; i >= 0; i--)
		cleanup_root_ns(steering->esw_egress_root_ns[i]);
	kfree(steering->esw_egress_root_ns);
	return err;
}

static int init_ingress_acls_root_ns(struct mlx5_core_dev *dev)
{
	struct mlx5_flow_steering *steering = dev->priv.steering;
	int err;
	int i;

	steering->esw_ingress_root_ns = kcalloc(MLX5_TOTAL_VPORTS(dev),
						sizeof(*steering->esw_ingress_root_ns),
						GFP_KERNEL);
	if (!steering->esw_ingress_root_ns)
		return -ENOMEM;

	for (i = 0; i < MLX5_TOTAL_VPORTS(dev); i++) {
		err = init_ingress_acl_root_ns(steering, i);
		if (err)
			goto cleanup_root_ns;
	}

	return 0;

cleanup_root_ns:
	for (i--; i >= 0; i--)
		cleanup_root_ns(steering->esw_ingress_root_ns[i]);
	kfree(steering->esw_ingress_root_ns);
	return err;
}

static int init_egress_root_ns(struct mlx5_flow_steering *steering)
{
	struct fs_prio *prio;

	steering->egress_root_ns = create_root_ns(steering,
						  FS_FT_NIC_TX);
	if (!steering->egress_root_ns)
		return -ENOMEM;

	/* create 1 prio*/
	prio = fs_create_prio(&steering->egress_root_ns->ns, 0, 1);
	return PTR_ERR_OR_ZERO(prio);
}

int mlx5_init_fs(struct mlx5_core_dev *dev)
{
	struct mlx5_flow_steering *steering;
	int err = 0;

	err = mlx5_init_fc_stats(dev);
	if (err)
		return err;

	steering = kzalloc(sizeof(*steering), GFP_KERNEL);
	if (!steering)
		return -ENOMEM;
	steering->dev = dev;
	dev->priv.steering = steering;

	steering->fgs_cache = kmem_cache_create("mlx5_fs_fgs",
						sizeof(struct mlx5_flow_group), 0,
						0, NULL);
	steering->ftes_cache = kmem_cache_create("mlx5_fs_ftes", sizeof(struct fs_fte), 0,
						 0, NULL);
	if (!steering->ftes_cache || !steering->fgs_cache) {
		err = -ENOMEM;
		goto err;
	}

	if ((((MLX5_CAP_GEN(dev, port_type) == MLX5_CAP_PORT_TYPE_ETH) &&
	      (MLX5_CAP_GEN(dev, nic_flow_table))) ||
	     ((MLX5_CAP_GEN(dev, port_type) == MLX5_CAP_PORT_TYPE_IB) &&
	      MLX5_CAP_GEN(dev, ipoib_enhanced_offloads))) &&
	    MLX5_CAP_FLOWTABLE_NIC_RX(dev, ft_support)) {
		err = init_root_ns(steering);
		if (err)
			goto err;
	}

	if (MLX5_ESWITCH_MANAGER(dev)) {
		if (MLX5_CAP_ESW_FLOWTABLE_FDB(dev, ft_support)) {
			err = init_fdb_root_ns(steering);
			if (err)
				goto err;
		}
		if (MLX5_CAP_ESW_EGRESS_ACL(dev, ft_support)) {
			err = init_egress_acls_root_ns(dev);
			if (err)
				goto err;
		}
		if (MLX5_CAP_ESW_INGRESS_ACL(dev, ft_support)) {
			err = init_ingress_acls_root_ns(dev);
			if (err)
				goto err;
		}
	}

	if (MLX5_CAP_FLOWTABLE_SNIFFER_RX(dev, ft_support)) {
		err = init_sniffer_rx_root_ns(steering);
		if (err)
			goto err;
	}

	if (MLX5_CAP_FLOWTABLE_SNIFFER_TX(dev, ft_support)) {
		err = init_sniffer_tx_root_ns(steering);
		if (err)
			goto err;
	}

	if (MLX5_IPSEC_DEV(dev)) {
		err = init_egress_root_ns(steering);
		if (err)
			goto err;
	}

	return 0;
err:
	mlx5_cleanup_fs(dev);
	return err;
}

int mlx5_fs_add_rx_underlay_qpn(struct mlx5_core_dev *dev, u32 underlay_qpn)
{
	struct mlx5_flow_root_namespace *root = dev->priv.steering->root_ns;
	struct mlx5_ft_underlay_qp *new_uqp;
	int err = 0;

	new_uqp = kzalloc(sizeof(*new_uqp), GFP_KERNEL);
	if (!new_uqp)
		return -ENOMEM;

	mutex_lock(&root->chain_lock);

	if (!root->root_ft) {
		err = -EINVAL;
		goto update_ft_fail;
	}

	err = root->cmds->update_root_ft(dev, root->root_ft, underlay_qpn,
					 false);
	if (err) {
		mlx5_core_warn(dev, "Failed adding underlay QPN (%u) to root FT err(%d)\n",
			       underlay_qpn, err);
		goto update_ft_fail;
	}

	new_uqp->qpn = underlay_qpn;
	list_add_tail(&new_uqp->list, &root->underlay_qpns);

	mutex_unlock(&root->chain_lock);

	return 0;

update_ft_fail:
	mutex_unlock(&root->chain_lock);
	kfree(new_uqp);
	return err;
}
EXPORT_SYMBOL(mlx5_fs_add_rx_underlay_qpn);

int mlx5_fs_remove_rx_underlay_qpn(struct mlx5_core_dev *dev, u32 underlay_qpn)
{
	struct mlx5_flow_root_namespace *root = dev->priv.steering->root_ns;
	struct mlx5_ft_underlay_qp *uqp;
	bool found = false;
	int err = 0;

	mutex_lock(&root->chain_lock);
	list_for_each_entry(uqp, &root->underlay_qpns, list) {
		if (uqp->qpn == underlay_qpn) {
			found = true;
			break;
		}
	}

	if (!found) {
		mlx5_core_warn(dev, "Failed finding underlay qp (%u) in qpn list\n",
			       underlay_qpn);
		err = -EINVAL;
		goto out;
	}

	err = root->cmds->update_root_ft(dev, root->root_ft, underlay_qpn,
					 true);
	if (err)
		mlx5_core_warn(dev, "Failed removing underlay QPN (%u) from root FT err(%d)\n",
			       underlay_qpn, err);

	list_del(&uqp->list);
	mutex_unlock(&root->chain_lock);
	kfree(uqp);

	return 0;

out:
	mutex_unlock(&root->chain_lock);
	return err;
}
EXPORT_SYMBOL(mlx5_fs_remove_rx_underlay_qpn);
