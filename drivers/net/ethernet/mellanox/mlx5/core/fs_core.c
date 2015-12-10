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
