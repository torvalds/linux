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

#include "mlx5_core.h"
#include "fs_core.h"
#include "fs_cmd.h"

#define INIT_TREE_NODE_ARRAY_SIZE(...)	(sizeof((struct init_tree_node[]){__VA_ARGS__}) /\
					 sizeof(struct init_tree_node))

#define ADD_PRIO(num_prios_val, min_level_val, max_ft_val, caps_val,\
		 ...) {.type = FS_TYPE_PRIO,\
	.min_ft_level = min_level_val,\
	.max_ft = max_ft_val,\
	.num_leaf_prios = num_prios_val,\
	.caps = caps_val,\
	.children = (struct init_tree_node[]) {__VA_ARGS__},\
	.ar_size = INIT_TREE_NODE_ARRAY_SIZE(__VA_ARGS__) \
}

#define ADD_MULTIPLE_PRIO(num_prios_val, max_ft_val, ...)\
	ADD_PRIO(num_prios_val, 0, max_ft_val, {},\
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

#define LEFTOVERS_MAX_FT 1
#define LEFTOVERS_NUM_PRIOS 1
#define BY_PASS_PRIO_MAX_FT 1
#define BY_PASS_MIN_LEVEL (KENREL_MIN_LEVEL + MLX5_BY_PASS_NUM_PRIOS +\
			   LEFTOVERS_MAX_FT)

#define KERNEL_MAX_FT 3
#define KERNEL_NUM_PRIOS 2
#define KENREL_MIN_LEVEL 2

#define ANCHOR_MAX_FT 1
#define ANCHOR_NUM_PRIOS 1
#define ANCHOR_MIN_LEVEL (BY_PASS_MIN_LEVEL + 1)
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
	int max_ft;
} root_fs = {
	.type = FS_TYPE_NAMESPACE,
	.ar_size = 4,
	.children = (struct init_tree_node[]) {
		ADD_PRIO(0, BY_PASS_MIN_LEVEL, 0,
			 FS_REQUIRED_CAPS(FS_CAP(flow_table_properties_nic_receive.flow_modify_en),
					  FS_CAP(flow_table_properties_nic_receive.modify_root),
					  FS_CAP(flow_table_properties_nic_receive.identified_miss_table_mode),
					  FS_CAP(flow_table_properties_nic_receive.flow_table_modify)),
			 ADD_NS(ADD_MULTIPLE_PRIO(MLX5_BY_PASS_NUM_PRIOS, BY_PASS_PRIO_MAX_FT))),
		ADD_PRIO(0, KENREL_MIN_LEVEL, 0, {},
			 ADD_NS(ADD_MULTIPLE_PRIO(KERNEL_NUM_PRIOS, KERNEL_MAX_FT))),
		ADD_PRIO(0, BY_PASS_MIN_LEVEL, 0,
			 FS_REQUIRED_CAPS(FS_CAP(flow_table_properties_nic_receive.flow_modify_en),
					  FS_CAP(flow_table_properties_nic_receive.modify_root),
					  FS_CAP(flow_table_properties_nic_receive.identified_miss_table_mode),
					  FS_CAP(flow_table_properties_nic_receive.flow_table_modify)),
			 ADD_NS(ADD_MULTIPLE_PRIO(LEFTOVERS_NUM_PRIOS, LEFTOVERS_MAX_FT))),
		ADD_PRIO(0, ANCHOR_MIN_LEVEL, 0, {},
			 ADD_NS(ADD_MULTIPLE_PRIO(ANCHOR_NUM_PRIOS, ANCHOR_MAX_FT))),
	}
};

enum fs_i_mutex_lock_class {
	FS_MUTEX_GRANDPARENT,
	FS_MUTEX_PARENT,
	FS_MUTEX_CHILD
};

static void del_rule(struct fs_node *node);
static void del_flow_table(struct fs_node *node);
static void del_flow_group(struct fs_node *node);
static void del_fte(struct fs_node *node);

static void tree_init_node(struct fs_node *node,
			   unsigned int refcount,
			   void (*remove_func)(struct fs_node *))
{
	atomic_set(&node->refcount, refcount);
	INIT_LIST_HEAD(&node->list);
	INIT_LIST_HEAD(&node->children);
	mutex_init(&node->lock);
	node->remove_func = remove_func;
}

static void tree_add_node(struct fs_node *node, struct fs_node *parent)
{
	if (parent)
		atomic_inc(&parent->refcount);
	node->parent = parent;

	/* Parent is the root */
	if (!parent)
		node->root = node;
	else
		node->root = parent->root;
}

static void tree_get_node(struct fs_node *node)
{
	atomic_inc(&node->refcount);
}

static void nested_lock_ref_node(struct fs_node *node,
				 enum fs_i_mutex_lock_class class)
{
	if (node) {
		mutex_lock_nested(&node->lock, class);
		atomic_inc(&node->refcount);
	}
}

static void lock_ref_node(struct fs_node *node)
{
	if (node) {
		mutex_lock(&node->lock);
		atomic_inc(&node->refcount);
	}
}

static void unlock_ref_node(struct fs_node *node)
{
	if (node) {
		atomic_dec(&node->refcount);
		mutex_unlock(&node->lock);
	}
}

static void tree_put_node(struct fs_node *node)
{
	struct fs_node *parent_node = node->parent;

	lock_ref_node(parent_node);
	if (atomic_dec_and_test(&node->refcount)) {
		if (parent_node)
			list_del_init(&node->list);
		if (node->remove_func)
			node->remove_func(node);
		kfree(node);
		node = NULL;
	}
	unlock_ref_node(parent_node);
	if (!node && parent_node)
		tree_put_node(parent_node);
}

static int tree_remove_node(struct fs_node *node)
{
	if (atomic_read(&node->refcount) > 1) {
		atomic_dec(&node->refcount);
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

static unsigned int find_next_free_level(struct fs_prio *prio)
{
	if (!list_empty(&prio->node.children)) {
		struct mlx5_flow_table *ft;

		ft = list_last_entry(&prio->node.children,
				     struct mlx5_flow_table,
				     node.list);
		return ft->level + 1;
	}
	return prio->start_level;
}

static bool masked_memcmp(void *mask, void *val1, void *val2, size_t size)
{
	unsigned int i;

	for (i = 0; i < size; i++, mask++, val1++, val2++)
		if ((*((u8 *)val1) & (*(u8 *)mask)) !=
		    ((*(u8 *)val2) & (*(u8 *)mask)))
			return false;

	return true;
}

static bool compare_match_value(struct mlx5_flow_group_mask *mask,
				void *fte_param1, void *fte_param2)
{
	if (mask->match_criteria_enable &
	    1 << MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_OUTER_HEADERS) {
		void *fte_match1 = MLX5_ADDR_OF(fte_match_param,
						fte_param1, outer_headers);
		void *fte_match2 = MLX5_ADDR_OF(fte_match_param,
						fte_param2, outer_headers);
		void *fte_mask = MLX5_ADDR_OF(fte_match_param,
					      mask->match_criteria, outer_headers);

		if (!masked_memcmp(fte_mask, fte_match1, fte_match2,
				   MLX5_ST_SZ_BYTES(fte_match_set_lyr_2_4)))
			return false;
	}

	if (mask->match_criteria_enable &
	    1 << MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_MISC_PARAMETERS) {
		void *fte_match1 = MLX5_ADDR_OF(fte_match_param,
						fte_param1, misc_parameters);
		void *fte_match2 = MLX5_ADDR_OF(fte_match_param,
						fte_param2, misc_parameters);
		void *fte_mask = MLX5_ADDR_OF(fte_match_param,
					  mask->match_criteria, misc_parameters);

		if (!masked_memcmp(fte_mask, fte_match1, fte_match2,
				   MLX5_ST_SZ_BYTES(fte_match_set_misc)))
			return false;
	}

	if (mask->match_criteria_enable &
	    1 << MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_INNER_HEADERS) {
		void *fte_match1 = MLX5_ADDR_OF(fte_match_param,
						fte_param1, inner_headers);
		void *fte_match2 = MLX5_ADDR_OF(fte_match_param,
						fte_param2, inner_headers);
		void *fte_mask = MLX5_ADDR_OF(fte_match_param,
					  mask->match_criteria, inner_headers);

		if (!masked_memcmp(fte_mask, fte_match1, fte_match2,
				   MLX5_ST_SZ_BYTES(fte_match_set_lyr_2_4)))
			return false;
	}
	return true;
}

static bool compare_match_criteria(u8 match_criteria_enable1,
				   u8 match_criteria_enable2,
				   void *mask1, void *mask2)
{
	return match_criteria_enable1 == match_criteria_enable2 &&
		!memcmp(mask1, mask2, MLX5_ST_SZ_BYTES(fte_match_param));
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

static inline struct mlx5_core_dev *get_dev(struct fs_node *node)
{
	struct mlx5_flow_root_namespace *root = find_root(node);

	if (root)
		return root->dev;
	return NULL;
}

static void del_flow_table(struct fs_node *node)
{
	struct mlx5_flow_table *ft;
	struct mlx5_core_dev *dev;
	struct fs_prio *prio;
	int err;

	fs_get_obj(ft, node);
	dev = get_dev(&ft->node);

	err = mlx5_cmd_destroy_flow_table(dev, ft);
	if (err)
		pr_warn("flow steering can't destroy ft\n");
	fs_get_obj(prio, ft->node.parent);
	prio->num_ft--;
}

static void del_rule(struct fs_node *node)
{
	struct mlx5_flow_rule *rule;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *fg;
	struct fs_fte *fte;
	u32	*match_value;
	struct mlx5_core_dev *dev = get_dev(node);
	int match_len = MLX5_ST_SZ_BYTES(fte_match_param);
	int err;

	match_value = mlx5_vzalloc(match_len);
	if (!match_value) {
		pr_warn("failed to allocate inbox\n");
		return;
	}

	fs_get_obj(rule, node);
	fs_get_obj(fte, rule->node.parent);
	fs_get_obj(fg, fte->node.parent);
	memcpy(match_value, fte->val, sizeof(fte->val));
	fs_get_obj(ft, fg->node.parent);
	list_del(&rule->node.list);
	if (rule->sw_action == MLX5_FLOW_CONTEXT_ACTION_FWD_NEXT_PRIO) {
		mutex_lock(&rule->dest_attr.ft->lock);
		list_del(&rule->next_ft);
		mutex_unlock(&rule->dest_attr.ft->lock);
	}
	if ((fte->action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST) &&
	    --fte->dests_size) {
		err = mlx5_cmd_update_fte(dev, ft,
					  fg->id, fte);
		if (err)
			pr_warn("%s can't del rule fg id=%d fte_index=%d\n",
				__func__, fg->id, fte->index);
	}
	kvfree(match_value);
}

static void del_fte(struct fs_node *node)
{
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *fg;
	struct mlx5_core_dev *dev;
	struct fs_fte *fte;
	int err;

	fs_get_obj(fte, node);
	fs_get_obj(fg, fte->node.parent);
	fs_get_obj(ft, fg->node.parent);

	dev = get_dev(&ft->node);
	err = mlx5_cmd_delete_fte(dev, ft,
				  fte->index);
	if (err)
		pr_warn("flow steering can't delete fte in index %d of flow group id %d\n",
			fte->index, fg->id);

	fte->status = 0;
	fg->num_ftes--;
}

static void del_flow_group(struct fs_node *node)
{
	struct mlx5_flow_group *fg;
	struct mlx5_flow_table *ft;
	struct mlx5_core_dev *dev;

	fs_get_obj(fg, node);
	fs_get_obj(ft, fg->node.parent);
	dev = get_dev(&ft->node);

	if (mlx5_cmd_destroy_flow_group(dev, ft, fg->id))
		pr_warn("flow steering can't destroy fg %d of ft %d\n",
			fg->id, ft->id);
}

static struct fs_fte *alloc_fte(u8 action,
				u32 flow_tag,
				u32 *match_value,
				unsigned int index)
{
	struct fs_fte *fte;

	fte = kzalloc(sizeof(*fte), GFP_KERNEL);
	if (!fte)
		return ERR_PTR(-ENOMEM);

	memcpy(fte->val, match_value, sizeof(fte->val));
	fte->node.type =  FS_TYPE_FLOW_ENTRY;
	fte->flow_tag = flow_tag;
	fte->index = index;
	fte->action = action;

	return fte;
}

static struct mlx5_flow_group *alloc_flow_group(u32 *create_fg_in)
{
	struct mlx5_flow_group *fg;
	void *match_criteria = MLX5_ADDR_OF(create_flow_group_in,
					    create_fg_in, match_criteria);
	u8 match_criteria_enable = MLX5_GET(create_flow_group_in,
					    create_fg_in,
					    match_criteria_enable);
	fg = kzalloc(sizeof(*fg), GFP_KERNEL);
	if (!fg)
		return ERR_PTR(-ENOMEM);

	fg->mask.match_criteria_enable = match_criteria_enable;
	memcpy(&fg->mask.match_criteria, match_criteria,
	       sizeof(fg->mask.match_criteria));
	fg->node.type =  FS_TYPE_FLOW_GROUP;
	fg->start_index = MLX5_GET(create_flow_group_in, create_fg_in,
				   start_flow_index);
	fg->max_ftes = MLX5_GET(create_flow_group_in, create_fg_in,
				end_flow_index) - fg->start_index + 1;
	return fg;
}

static struct mlx5_flow_table *alloc_flow_table(int level, int max_fte,
						enum fs_flow_table_type table_type)
{
	struct mlx5_flow_table *ft;

	ft  = kzalloc(sizeof(*ft), GFP_KERNEL);
	if (!ft)
		return NULL;

	ft->level = level;
	ft->node.type = FS_TYPE_FLOW_TABLE;
	ft->type = table_type;
	ft->max_fte = max_fte;
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
	struct mlx5_flow_table *iter;
	int i = 0;
	int err;

	fs_for_each_ft(iter, prio) {
		i++;
		err = mlx5_cmd_modify_flow_table(dev,
						 iter,
						 ft);
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
	int min_level = INT_MAX;
	int err;

	if (root->root_ft)
		min_level = root->root_ft->level;

	if (ft->level >= min_level)
		return 0;

	err = mlx5_cmd_update_root_ft(root->dev, ft);
	if (err)
		mlx5_core_warn(root->dev, "Update root flow table of id=%u failed\n",
			       ft->id);
	else
		root->root_ft = ft;

	return err;
}

static int mlx5_modify_rule_destination(struct mlx5_flow_rule *rule,
					struct mlx5_flow_destination *dest)
{
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *fg;
	struct fs_fte *fte;
	int err = 0;

	fs_get_obj(fte, rule->node.parent);
	if (!(fte->action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST))
		return -EINVAL;
	lock_ref_node(&fte->node);
	fs_get_obj(fg, fte->node.parent);
	fs_get_obj(ft, fg->node.parent);

	memcpy(&rule->dest_attr, dest, sizeof(*dest));
	err = mlx5_cmd_update_fte(get_dev(&ft->node),
				  ft, fg->id, fte);
	unlock_ref_node(&fte->node);

	return err;
}

/* Modify/set FWD rules that point on old_next_ft to point on new_next_ft  */
static int connect_fwd_rules(struct mlx5_core_dev *dev,
			     struct mlx5_flow_table *new_next_ft,
			     struct mlx5_flow_table *old_next_ft)
{
	struct mlx5_flow_destination dest;
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
		err = mlx5_modify_rule_destination(iter, &dest);
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

struct mlx5_flow_table *mlx5_create_flow_table(struct mlx5_flow_namespace *ns,
					       int prio,
					       int max_fte)
{
	struct mlx5_flow_table *next_ft = NULL;
	struct mlx5_flow_table *ft;
	int err;
	int log_table_sz;
	struct mlx5_flow_root_namespace *root =
		find_root(&ns->node);
	struct fs_prio *fs_prio = NULL;

	if (!root) {
		pr_err("mlx5: flow steering failed to find root of namespace\n");
		return ERR_PTR(-ENODEV);
	}

	mutex_lock(&root->chain_lock);
	fs_prio = find_prio(ns, prio);
	if (!fs_prio) {
		err = -EINVAL;
		goto unlock_root;
	}
	if (fs_prio->num_ft == fs_prio->max_ft) {
		err = -ENOSPC;
		goto unlock_root;
	}

	ft = alloc_flow_table(find_next_free_level(fs_prio),
			      roundup_pow_of_two(max_fte),
			      root->table_type);
	if (!ft) {
		err = -ENOMEM;
		goto unlock_root;
	}

	tree_init_node(&ft->node, 1, del_flow_table);
	log_table_sz = ilog2(ft->max_fte);
	next_ft = find_next_chained_ft(fs_prio);
	err = mlx5_cmd_create_flow_table(root->dev, ft->type, ft->level,
					 log_table_sz, next_ft, &ft->id);
	if (err)
		goto free_ft;

	err = connect_flow_table(root->dev, ft, fs_prio);
	if (err)
		goto destroy_ft;
	lock_ref_node(&fs_prio->node);
	tree_add_node(&ft->node, &fs_prio->node);
	list_add_tail(&ft->node.list, &fs_prio->node.children);
	fs_prio->num_ft++;
	unlock_ref_node(&fs_prio->node);
	mutex_unlock(&root->chain_lock);
	return ft;
destroy_ft:
	mlx5_cmd_destroy_flow_table(root->dev, ft);
free_ft:
	kfree(ft);
unlock_root:
	mutex_unlock(&root->chain_lock);
	return ERR_PTR(err);
}

struct mlx5_flow_table *mlx5_create_auto_grouped_flow_table(struct mlx5_flow_namespace *ns,
							    int prio,
							    int num_flow_table_entries,
							    int max_num_groups)
{
	struct mlx5_flow_table *ft;

	if (max_num_groups > num_flow_table_entries)
		return ERR_PTR(-EINVAL);

	ft = mlx5_create_flow_table(ns, prio, num_flow_table_entries);
	if (IS_ERR(ft))
		return ft;

	ft->autogroup.active = true;
	ft->autogroup.required_groups = max_num_groups;

	return ft;
}
EXPORT_SYMBOL(mlx5_create_auto_grouped_flow_table);

/* Flow table should be locked */
static struct mlx5_flow_group *create_flow_group_common(struct mlx5_flow_table *ft,
							u32 *fg_in,
							struct list_head
							*prev_fg,
							bool is_auto_fg)
{
	struct mlx5_flow_group *fg;
	struct mlx5_core_dev *dev = get_dev(&ft->node);
	int err;

	if (!dev)
		return ERR_PTR(-ENODEV);

	fg = alloc_flow_group(fg_in);
	if (IS_ERR(fg))
		return fg;

	err = mlx5_cmd_create_flow_group(dev, ft, fg_in, &fg->id);
	if (err) {
		kfree(fg);
		return ERR_PTR(err);
	}

	if (ft->autogroup.active)
		ft->autogroup.num_groups++;
	/* Add node to tree */
	tree_init_node(&fg->node, !is_auto_fg, del_flow_group);
	tree_add_node(&fg->node, &ft->node);
	/* Add node to group list */
	list_add(&fg->node.list, ft->node.children.prev);

	return fg;
}

struct mlx5_flow_group *mlx5_create_flow_group(struct mlx5_flow_table *ft,
					       u32 *fg_in)
{
	struct mlx5_flow_group *fg;

	if (ft->autogroup.active)
		return ERR_PTR(-EPERM);

	lock_ref_node(&ft->node);
	fg = create_flow_group_common(ft, fg_in, &ft->node.children, false);
	unlock_ref_node(&ft->node);

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

/* fte should not be deleted while calling this function */
static struct mlx5_flow_rule *add_rule_fte(struct fs_fte *fte,
					   struct mlx5_flow_group *fg,
					   struct mlx5_flow_destination *dest)
{
	struct mlx5_flow_table *ft;
	struct mlx5_flow_rule *rule;
	int err;

	rule = alloc_rule(dest);
	if (!rule)
		return ERR_PTR(-ENOMEM);

	fs_get_obj(ft, fg->node.parent);
	/* Add dest to dests list- we need flow tables to be in the
	 * end of the list for forward to next prio rules.
	 */
	tree_init_node(&rule->node, 1, del_rule);
	if (dest && dest->type != MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE)
		list_add(&rule->node.list, &fte->node.children);
	else
		list_add_tail(&rule->node.list, &fte->node.children);
	if (dest)
		fte->dests_size++;
	if (fte->dests_size == 1 || !dest)
		err = mlx5_cmd_create_fte(get_dev(&ft->node),
					  ft, fg->id, fte);
	else
		err = mlx5_cmd_update_fte(get_dev(&ft->node),
					  ft, fg->id, fte);
	if (err)
		goto free_rule;

	fte->status |= FS_FTE_STATUS_EXISTING;

	return rule;

free_rule:
	list_del(&rule->node.list);
	kfree(rule);
	if (dest)
		fte->dests_size--;
	return ERR_PTR(err);
}

/* Assumed fg is locked */
static unsigned int get_free_fte_index(struct mlx5_flow_group *fg,
				       struct list_head **prev)
{
	struct fs_fte *fte;
	unsigned int start = fg->start_index;

	if (prev)
		*prev = &fg->node.children;

	/* assumed list is sorted by index */
	fs_for_each_fte(fte, fg) {
		if (fte->index != start)
			return start;
		start++;
		if (prev)
			*prev = &fte->node.list;
	}

	return start;
}

/* prev is output, prev->next = new_fte */
static struct fs_fte *create_fte(struct mlx5_flow_group *fg,
				 u32 *match_value,
				 u8 action,
				 u32 flow_tag,
				 struct list_head **prev)
{
	struct fs_fte *fte;
	int index;

	index = get_free_fte_index(fg, prev);
	fte = alloc_fte(action, flow_tag, match_value, index);
	if (IS_ERR(fte))
		return fte;

	return fte;
}

static struct mlx5_flow_group *create_autogroup(struct mlx5_flow_table *ft,
						u8 match_criteria_enable,
						u32 *match_criteria)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct list_head *prev = &ft->node.children;
	unsigned int candidate_index = 0;
	struct mlx5_flow_group *fg;
	void *match_criteria_addr;
	unsigned int group_size = 0;
	u32 *in;

	if (!ft->autogroup.active)
		return ERR_PTR(-ENOENT);

	in = mlx5_vzalloc(inlen);
	if (!in)
		return ERR_PTR(-ENOMEM);

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

	if (candidate_index + group_size > ft->max_fte) {
		fg = ERR_PTR(-ENOSPC);
		goto out;
	}

	MLX5_SET(create_flow_group_in, in, match_criteria_enable,
		 match_criteria_enable);
	MLX5_SET(create_flow_group_in, in, start_flow_index, candidate_index);
	MLX5_SET(create_flow_group_in, in, end_flow_index,   candidate_index +
		 group_size - 1);
	match_criteria_addr = MLX5_ADDR_OF(create_flow_group_in,
					   in, match_criteria);
	memcpy(match_criteria_addr, match_criteria,
	       MLX5_ST_SZ_BYTES(fte_match_param));

	fg = create_flow_group_common(ft, in, prev, true);
out:
	kvfree(in);
	return fg;
}

static struct mlx5_flow_rule *find_flow_rule(struct fs_fte *fte,
					     struct mlx5_flow_destination *dest)
{
	struct mlx5_flow_rule *rule;

	list_for_each_entry(rule, &fte->node.children, node.list) {
		if (rule->dest_attr.type == dest->type) {
			if ((dest->type == MLX5_FLOW_DESTINATION_TYPE_VPORT &&
			     dest->vport_num == rule->dest_attr.vport_num) ||
			    (dest->type == MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE &&
			     dest->ft == rule->dest_attr.ft) ||
			    (dest->type == MLX5_FLOW_DESTINATION_TYPE_TIR &&
			     dest->tir_num == rule->dest_attr.tir_num))
				return rule;
		}
	}
	return NULL;
}

static struct mlx5_flow_rule *add_rule_fg(struct mlx5_flow_group *fg,
					  u32 *match_value,
					  u8 action,
					  u32 flow_tag,
					  struct mlx5_flow_destination *dest)
{
	struct fs_fte *fte;
	struct mlx5_flow_rule *rule;
	struct mlx5_flow_table *ft;
	struct list_head *prev;

	nested_lock_ref_node(&fg->node, FS_MUTEX_PARENT);
	fs_for_each_fte(fte, fg) {
		nested_lock_ref_node(&fte->node, FS_MUTEX_CHILD);
		if (compare_match_value(&fg->mask, match_value, &fte->val) &&
		    action == fte->action && flow_tag == fte->flow_tag) {
			rule = find_flow_rule(fte, dest);
			if (rule) {
				atomic_inc(&rule->node.refcount);
				unlock_ref_node(&fte->node);
				unlock_ref_node(&fg->node);
				return rule;
			}
			rule = add_rule_fte(fte, fg, dest);
			unlock_ref_node(&fte->node);
			if (IS_ERR(rule))
				goto unlock_fg;
			else
				goto add_rule;
		}
		unlock_ref_node(&fte->node);
	}
	fs_get_obj(ft, fg->node.parent);
	if (fg->num_ftes >= fg->max_ftes) {
		rule = ERR_PTR(-ENOSPC);
		goto unlock_fg;
	}

	fte = create_fte(fg, match_value, action, flow_tag, &prev);
	if (IS_ERR(fte)) {
		rule = (void *)fte;
		goto unlock_fg;
	}
	tree_init_node(&fte->node, 0, del_fte);
	rule = add_rule_fte(fte, fg, dest);
	if (IS_ERR(rule)) {
		kfree(fte);
		goto unlock_fg;
	}

	fg->num_ftes++;

	tree_add_node(&fte->node, &fg->node);
	list_add(&fte->node.list, prev);
add_rule:
	tree_add_node(&rule->node, &fte->node);
unlock_fg:
	unlock_ref_node(&fg->node);
	return rule;
}

static struct mlx5_flow_rule *
_mlx5_add_flow_rule(struct mlx5_flow_table *ft,
		    u8 match_criteria_enable,
		    u32 *match_criteria,
		    u32 *match_value,
		    u32 action,
		    u32 flow_tag,
		    struct mlx5_flow_destination *dest)
{
	struct mlx5_flow_group *g;
	struct mlx5_flow_rule *rule;

	if ((action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST) && !dest)
		return ERR_PTR(-EINVAL);

	nested_lock_ref_node(&ft->node, FS_MUTEX_GRANDPARENT);
	fs_for_each_fg(g, ft)
		if (compare_match_criteria(g->mask.match_criteria_enable,
					   match_criteria_enable,
					   g->mask.match_criteria,
					   match_criteria)) {
			rule = add_rule_fg(g, match_value,
					   action, flow_tag, dest);
			if (!IS_ERR(rule) || PTR_ERR(rule) != -ENOSPC)
				goto unlock;
		}

	g = create_autogroup(ft, match_criteria_enable, match_criteria);
	if (IS_ERR(g)) {
		rule = (void *)g;
		goto unlock;
	}

	rule = add_rule_fg(g, match_value,
			   action, flow_tag, dest);
	if (IS_ERR(rule)) {
		/* Remove assumes refcount > 0 and autogroup creates a group
		 * with a refcount = 0.
		 */
		unlock_ref_node(&ft->node);
		tree_get_node(&g->node);
		tree_remove_node(&g->node);
		return rule;
	}
unlock:
	unlock_ref_node(&ft->node);
	return rule;
}

static bool fwd_next_prio_supported(struct mlx5_flow_table *ft)
{
	return ((ft->type == FS_FT_NIC_RX) &&
		(MLX5_CAP_FLOWTABLE(get_dev(&ft->node), nic_rx_multi_path_tirs)));
}

struct mlx5_flow_rule *
mlx5_add_flow_rule(struct mlx5_flow_table *ft,
		   u8 match_criteria_enable,
		   u32 *match_criteria,
		   u32 *match_value,
		   u32 action,
		   u32 flow_tag,
		   struct mlx5_flow_destination *dest)
{
	struct mlx5_flow_root_namespace *root = find_root(&ft->node);
	struct mlx5_flow_destination gen_dest;
	struct mlx5_flow_table *next_ft = NULL;
	struct mlx5_flow_rule *rule = NULL;
	u32 sw_action = action;
	struct fs_prio *prio;

	fs_get_obj(prio, ft->node.parent);
	if (action == MLX5_FLOW_CONTEXT_ACTION_FWD_NEXT_PRIO) {
		if (!fwd_next_prio_supported(ft))
			return ERR_PTR(-EOPNOTSUPP);
		if (dest)
			return ERR_PTR(-EINVAL);
		mutex_lock(&root->chain_lock);
		next_ft = find_next_chained_ft(prio);
		if (next_ft) {
			gen_dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
			gen_dest.ft = next_ft;
			dest = &gen_dest;
			action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
		} else {
			mutex_unlock(&root->chain_lock);
			return ERR_PTR(-EOPNOTSUPP);
		}
	}

	rule =	_mlx5_add_flow_rule(ft, match_criteria_enable, match_criteria,
				    match_value, action, flow_tag, dest);

	if (sw_action == MLX5_FLOW_CONTEXT_ACTION_FWD_NEXT_PRIO) {
		if (!IS_ERR_OR_NULL(rule) &&
		    (list_empty(&rule->next_ft))) {
			mutex_lock(&next_ft->lock);
			list_add(&rule->next_ft, &next_ft->fwd_rules);
			mutex_unlock(&next_ft->lock);
			rule->sw_action = MLX5_FLOW_CONTEXT_ACTION_FWD_NEXT_PRIO;
		}
		mutex_unlock(&root->chain_lock);
	}
	return rule;
}
EXPORT_SYMBOL(mlx5_add_flow_rule);

void mlx5_del_flow_rule(struct mlx5_flow_rule *rule)
{
	tree_remove_node(&rule->node);
}
EXPORT_SYMBOL(mlx5_del_flow_rule);

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
	struct mlx5_flow_table *new_root_ft = NULL;

	if (root->root_ft != ft)
		return 0;

	new_root_ft = find_next_ft(ft);
	if (new_root_ft) {
		int err = mlx5_cmd_update_root_ft(root->dev, new_root_ft);

		if (err) {
			mlx5_core_warn(root->dev, "Update root flow table of id=%u failed\n",
				       ft->id);
			return err;
		}
		root->root_ft = new_root_ft;
	}
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
	struct mlx5_flow_root_namespace *root_ns = dev->priv.root_ns;
	int prio;
	struct fs_prio *fs_prio;
	struct mlx5_flow_namespace *ns;

	if (!root_ns)
		return NULL;

	switch (type) {
	case MLX5_FLOW_NAMESPACE_BYPASS:
	case MLX5_FLOW_NAMESPACE_KERNEL:
	case MLX5_FLOW_NAMESPACE_LEFTOVERS:
	case MLX5_FLOW_NAMESPACE_ANCHOR:
		prio = type;
		break;
	case MLX5_FLOW_NAMESPACE_FDB:
		if (dev->priv.fdb_root_ns)
			return &dev->priv.fdb_root_ns->ns;
		else
			return NULL;
	default:
		return NULL;
	}

	fs_prio = find_prio(&root_ns->ns, prio);
	if (!fs_prio)
		return NULL;

	ns = list_first_entry(&fs_prio->node.children,
			      typeof(*ns),
			      node.list);

	return ns;
}
EXPORT_SYMBOL(mlx5_get_flow_namespace);

static struct fs_prio *fs_create_prio(struct mlx5_flow_namespace *ns,
				      unsigned prio, int max_ft)
{
	struct fs_prio *fs_prio;

	fs_prio = kzalloc(sizeof(*fs_prio), GFP_KERNEL);
	if (!fs_prio)
		return ERR_PTR(-ENOMEM);

	fs_prio->node.type = FS_TYPE_PRIO;
	tree_init_node(&fs_prio->node, 1, NULL);
	tree_add_node(&fs_prio->node, &ns->node);
	fs_prio->max_ft = max_ft;
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
	tree_init_node(&ns->node, 1, NULL);
	tree_add_node(&ns->node, &prio->node);
	list_add_tail(&ns->node.list, &prio->node.children);

	return ns;
}

static int create_leaf_prios(struct mlx5_flow_namespace *ns, struct init_tree_node
			     *prio_metadata)
{
	struct fs_prio *fs_prio;
	int i;

	for (i = 0; i < prio_metadata->num_leaf_prios; i++) {
		fs_prio = fs_create_prio(ns, i, prio_metadata->max_ft);
		if (IS_ERR(fs_prio))
			return PTR_ERR(fs_prio);
	}
	return 0;
}

#define FLOW_TABLE_BIT_SZ 1
#define GET_FLOW_TABLE_CAP(dev, offset) \
	((be32_to_cpu(*((__be32 *)(dev->hca_caps_cur[MLX5_CAP_FLOW_TABLE]) +	\
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

static int init_root_tree_recursive(struct mlx5_core_dev *dev,
				    struct init_tree_node *init_node,
				    struct fs_node *fs_parent_node,
				    struct init_tree_node *init_parent_node,
				    int index)
{
	int max_ft_level = MLX5_CAP_FLOWTABLE(dev,
					      flow_table_properties_nic_receive.
					      max_ft_level);
	struct mlx5_flow_namespace *fs_ns;
	struct fs_prio *fs_prio;
	struct fs_node *base;
	int i;
	int err;

	if (init_node->type == FS_TYPE_PRIO) {
		if ((init_node->min_ft_level > max_ft_level) ||
		    !has_required_caps(dev, &init_node->caps))
			return 0;

		fs_get_obj(fs_ns, fs_parent_node);
		if (init_node->num_leaf_prios)
			return create_leaf_prios(fs_ns, init_node);
		fs_prio = fs_create_prio(fs_ns, index, init_node->max_ft);
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
	for (i = 0; i < init_node->ar_size; i++) {
		err = init_root_tree_recursive(dev, &init_node->children[i],
					       base, init_node, i);
		if (err)
			return err;
	}

	return 0;
}

static int init_root_tree(struct mlx5_core_dev *dev,
			  struct init_tree_node *init_node,
			  struct fs_node *fs_parent_node)
{
	int i;
	struct mlx5_flow_namespace *fs_ns;
	int err;

	fs_get_obj(fs_ns, fs_parent_node);
	for (i = 0; i < init_node->ar_size; i++) {
		err = init_root_tree_recursive(dev, &init_node->children[i],
					       &fs_ns->node,
					       init_node, i);
		if (err)
			return err;
	}
	return 0;
}

static struct mlx5_flow_root_namespace *create_root_ns(struct mlx5_core_dev *dev,
						       enum fs_flow_table_type
						       table_type)
{
	struct mlx5_flow_root_namespace *root_ns;
	struct mlx5_flow_namespace *ns;

	/* Create the root namespace */
	root_ns = mlx5_vzalloc(sizeof(*root_ns));
	if (!root_ns)
		return NULL;

	root_ns->dev = dev;
	root_ns->table_type = table_type;

	ns = &root_ns->ns;
	fs_init_namespace(ns);
	mutex_init(&root_ns->chain_lock);
	tree_init_node(&ns->node, 1, NULL);
	tree_add_node(&ns->node, NULL);

	return root_ns;
}

static void set_prio_attrs_in_prio(struct fs_prio *prio, int acc_level);

static int set_prio_attrs_in_ns(struct mlx5_flow_namespace *ns, int acc_level)
{
	struct fs_prio *prio;

	fs_for_each_prio(prio, ns) {
		 /* This updates prio start_level and max_ft */
		set_prio_attrs_in_prio(prio, acc_level);
		acc_level += prio->max_ft;
	}
	return acc_level;
}

static void set_prio_attrs_in_prio(struct fs_prio *prio, int acc_level)
{
	struct mlx5_flow_namespace *ns;
	int acc_level_ns = acc_level;

	prio->start_level = acc_level;
	fs_for_each_ns(ns, prio)
		/* This updates start_level and max_ft of ns's priority descendants */
		acc_level_ns = set_prio_attrs_in_ns(ns, acc_level);
	if (!prio->max_ft)
		prio->max_ft = acc_level_ns - prio->start_level;
	WARN_ON(prio->max_ft < acc_level_ns - prio->start_level);
}

static void set_prio_attrs(struct mlx5_flow_root_namespace *root_ns)
{
	struct mlx5_flow_namespace *ns = &root_ns->ns;
	struct fs_prio *prio;
	int start_level = 0;

	fs_for_each_prio(prio, ns) {
		set_prio_attrs_in_prio(prio, start_level);
		start_level += prio->max_ft;
	}
}

#define ANCHOR_PRIO 0
#define ANCHOR_SIZE 1
static int create_anchor_flow_table(struct mlx5_core_dev
							*dev)
{
	struct mlx5_flow_namespace *ns = NULL;
	struct mlx5_flow_table *ft;

	ns = mlx5_get_flow_namespace(dev, MLX5_FLOW_NAMESPACE_ANCHOR);
	if (!ns)
		return -EINVAL;
	ft = mlx5_create_flow_table(ns, ANCHOR_PRIO, ANCHOR_SIZE);
	if (IS_ERR(ft)) {
		mlx5_core_err(dev, "Failed to create last anchor flow table");
		return PTR_ERR(ft);
	}
	return 0;
}

static int init_root_ns(struct mlx5_core_dev *dev)
{

	dev->priv.root_ns = create_root_ns(dev, FS_FT_NIC_RX);
	if (IS_ERR_OR_NULL(dev->priv.root_ns))
		goto cleanup;

	if (init_root_tree(dev, &root_fs, &dev->priv.root_ns->ns.node))
		goto cleanup;

	set_prio_attrs(dev->priv.root_ns);

	if (create_anchor_flow_table(dev))
		goto cleanup;

	return 0;

cleanup:
	mlx5_cleanup_fs(dev);
	return -ENOMEM;
}

static void cleanup_single_prio_root_ns(struct mlx5_core_dev *dev,
					struct mlx5_flow_root_namespace *root_ns)
{
	struct fs_node *prio;

	if (!root_ns)
		return;

	if (!list_empty(&root_ns->ns.node.children)) {
		prio = list_first_entry(&root_ns->ns.node.children,
					struct fs_node,
				 list);
		if (tree_remove_node(prio))
			mlx5_core_warn(dev,
				       "Flow steering priority wasn't destroyed, refcount > 1\n");
	}
	if (tree_remove_node(&root_ns->ns.node))
		mlx5_core_warn(dev,
			       "Flow steering namespace wasn't destroyed, refcount > 1\n");
	root_ns = NULL;
}

static void destroy_flow_tables(struct fs_prio *prio)
{
	struct mlx5_flow_table *iter;
	struct mlx5_flow_table *tmp;

	fs_for_each_ft_safe(iter, tmp, prio)
		mlx5_destroy_flow_table(iter);
}

static void cleanup_root_ns(struct mlx5_core_dev *dev)
{
	struct mlx5_flow_root_namespace *root_ns = dev->priv.root_ns;
	struct fs_prio *iter_prio;

	if (!MLX5_CAP_GEN(dev, nic_flow_table))
		return;

	if (!root_ns)
		return;

	/* stage 1 */
	fs_for_each_prio(iter_prio, &root_ns->ns) {
		struct fs_node *node;
		struct mlx5_flow_namespace *iter_ns;

		fs_for_each_ns_or_ft(node, iter_prio) {
			if (node->type == FS_TYPE_FLOW_TABLE)
				continue;
			fs_get_obj(iter_ns, node);
			while (!list_empty(&iter_ns->node.children)) {
				struct fs_prio *obj_iter_prio2;
				struct fs_node *iter_prio2 =
					list_first_entry(&iter_ns->node.children,
							 struct fs_node,
							 list);

				fs_get_obj(obj_iter_prio2, iter_prio2);
				destroy_flow_tables(obj_iter_prio2);
				if (tree_remove_node(iter_prio2)) {
					mlx5_core_warn(dev,
						       "Priority %d wasn't destroyed, refcount > 1\n",
						       obj_iter_prio2->prio);
					return;
				}
			}
		}
	}

	/* stage 2 */
	fs_for_each_prio(iter_prio, &root_ns->ns) {
		while (!list_empty(&iter_prio->node.children)) {
			struct fs_node *iter_ns =
				list_first_entry(&iter_prio->node.children,
						 struct fs_node,
						 list);
			if (tree_remove_node(iter_ns)) {
				mlx5_core_warn(dev,
					       "Namespace wasn't destroyed, refcount > 1\n");
				return;
			}
		}
	}

	/* stage 3 */
	while (!list_empty(&root_ns->ns.node.children)) {
		struct fs_prio *obj_prio_node;
		struct fs_node *prio_node =
			list_first_entry(&root_ns->ns.node.children,
					 struct fs_node,
					 list);

		fs_get_obj(obj_prio_node, prio_node);
		if (tree_remove_node(prio_node)) {
			mlx5_core_warn(dev,
				       "Priority %d wasn't destroyed, refcount > 1\n",
				       obj_prio_node->prio);
			return;
		}
	}

	if (tree_remove_node(&root_ns->ns.node)) {
		mlx5_core_warn(dev,
			       "root namespace wasn't destroyed, refcount > 1\n");
		return;
	}

	dev->priv.root_ns = NULL;
}

void mlx5_cleanup_fs(struct mlx5_core_dev *dev)
{
	cleanup_root_ns(dev);
	cleanup_single_prio_root_ns(dev, dev->priv.fdb_root_ns);
}

static int init_fdb_root_ns(struct mlx5_core_dev *dev)
{
	struct fs_prio *prio;

	dev->priv.fdb_root_ns = create_root_ns(dev, FS_FT_FDB);
	if (!dev->priv.fdb_root_ns)
		return -ENOMEM;

	/* Create single prio */
	prio = fs_create_prio(&dev->priv.fdb_root_ns->ns, 0, 1);
	if (IS_ERR(prio)) {
		cleanup_single_prio_root_ns(dev, dev->priv.fdb_root_ns);
		return PTR_ERR(prio);
	} else {
		return 0;
	}
}

int mlx5_init_fs(struct mlx5_core_dev *dev)
{
	int err = 0;

	if (MLX5_CAP_GEN(dev, nic_flow_table)) {
		err = init_root_ns(dev);
		if (err)
			return err;
	}
	if (MLX5_CAP_GEN(dev, eswitch_flow_table)) {
		err = init_fdb_root_ns(dev);
		if (err)
			cleanup_root_ns(dev);
	}

	return err;
}
