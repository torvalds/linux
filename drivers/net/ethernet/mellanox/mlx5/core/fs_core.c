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

static void nested_lock_ref_node(struct fs_node *node)
{
	if (node) {
		mutex_lock_nested(&node->lock, SINGLE_DEPTH_NESTING);
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
	if (atomic_read(&node->refcount) > 1)
		return -EPERM;
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
	fte->dests_size--;
	if (fte->dests_size) {
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

	return ft;
}

static struct mlx5_flow_table *mlx5_create_flow_table(struct mlx5_flow_namespace *ns,
						      int prio,
						      int max_fte)
{
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

	fs_prio = find_prio(ns, prio);
	if (!fs_prio)
		return ERR_PTR(-EINVAL);

	lock_ref_node(&fs_prio->node);
	if (fs_prio->num_ft == fs_prio->max_ft) {
		err = -ENOSPC;
		goto unlock_prio;
	}

	ft = alloc_flow_table(find_next_free_level(fs_prio),
			      roundup_pow_of_two(max_fte),
			      root->table_type);
	if (!ft) {
		err = -ENOMEM;
		goto unlock_prio;
	}

	tree_init_node(&ft->node, 1, del_flow_table);
	log_table_sz = ilog2(ft->max_fte);
	err = mlx5_cmd_create_flow_table(root->dev, ft->type, ft->level,
					 log_table_sz, &ft->id);
	if (err)
		goto free_ft;

	tree_add_node(&ft->node, &fs_prio->node);
	list_add_tail(&ft->node.list, &fs_prio->node.children);
	fs_prio->num_ft++;
	unlock_ref_node(&fs_prio->node);

	return ft;

free_ft:
	kfree(ft);
unlock_prio:
	unlock_ref_node(&fs_prio->node);
	return ERR_PTR(err);
}

static struct mlx5_flow_group *mlx5_create_flow_group(struct mlx5_flow_table *ft,
						      u32 *fg_in)
{
	struct mlx5_flow_group *fg;
	struct mlx5_core_dev *dev = get_dev(&ft->node);
	int err;

	if (!dev)
		return ERR_PTR(-ENODEV);

	fg = alloc_flow_group(fg_in);
	if (IS_ERR(fg))
		return fg;

	lock_ref_node(&ft->node);
	err = mlx5_cmd_create_flow_group(dev, ft, fg_in, &fg->id);
	if (err) {
		kfree(fg);
		unlock_ref_node(&ft->node);
		return ERR_PTR(err);
	}
	/* Add node to tree */
	tree_init_node(&fg->node, 1, del_flow_group);
	tree_add_node(&fg->node, &ft->node);
	/* Add node to group list */
	list_add(&fg->node.list, ft->node.children.prev);
	unlock_ref_node(&ft->node);

	return fg;
}

static struct mlx5_flow_rule *alloc_rule(struct mlx5_flow_destination *dest)
{
	struct mlx5_flow_rule *rule;

	rule = kzalloc(sizeof(*rule), GFP_KERNEL);
	if (!rule)
		return NULL;

	rule->node.type = FS_TYPE_FLOW_DEST;
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
	/* Add dest to dests list- added as first element after the head */
	tree_init_node(&rule->node, 1, del_rule);
	list_add_tail(&rule->node.list, &fte->node.children);
	fte->dests_size++;
	if (fte->dests_size == 1)
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

/* Assuming parent fg(flow table) is locked */
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

	lock_ref_node(&fg->node);
	fs_for_each_fte(fte, fg) {
		nested_lock_ref_node(&fte->node);
		if (compare_match_value(&fg->mask, match_value, &fte->val) &&
		    action == fte->action && flow_tag == fte->flow_tag) {
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
mlx5_add_flow_rule(struct mlx5_flow_table *ft,
		   u8 match_criteria_enable,
		   u32 *match_criteria,
		   u32 *match_value,
		   u32 action,
		   u32 flow_tag,
		   struct mlx5_flow_destination *dest)
{
	struct mlx5_flow_group *g;
	struct mlx5_flow_rule *rule = ERR_PTR(-EINVAL);

	tree_get_node(&ft->node);
	lock_ref_node(&ft->node);
	fs_for_each_fg(g, ft)
		if (compare_match_criteria(g->mask.match_criteria_enable,
					   match_criteria_enable,
					   g->mask.match_criteria,
					   match_criteria)) {
			unlock_ref_node(&ft->node);
			rule = add_rule_fg(g, match_value,
					   action, flow_tag, dest);
			goto put;
		}
	unlock_ref_node(&ft->node);
put:
	tree_put_node(&ft->node);
	return rule;
}

static void mlx5_del_flow_rule(struct mlx5_flow_rule *rule)
{
	tree_remove_node(&rule->node);
}

static int mlx5_destroy_flow_table(struct mlx5_flow_table *ft)
{
	if (tree_remove_node(&ft->node))
		mlx5_core_warn(get_dev(&ft->node), "Flow table %d wasn't destroyed, refcount > 1\n",
			       ft->id);

	return 0;
}

static void mlx5_destroy_flow_group(struct mlx5_flow_group *fg)
{
	if (tree_remove_node(&fg->node))
		mlx5_core_warn(get_dev(&fg->node), "Flow group %d wasn't destroyed, refcount > 1\n",
			       fg->id);
}
