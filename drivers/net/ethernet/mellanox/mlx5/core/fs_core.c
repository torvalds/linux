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
#include <linux/mlx5/vport.h>
#include <linux/mlx5/eswitch.h>
#include <net/devlink.h>

#include "mlx5_core.h"
#include "fs_core.h"
#include "fs_cmd.h"
#include "fs_ft_pool.h"
#include "diag/fs_tracepoint.h"
#include "devlink.h"

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

#define ADD_NS(def_miss_act, ...) {.type = FS_TYPE_NAMESPACE,	\
	.def_miss_action = def_miss_act,\
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

#define FS_CHAINING_CAPS_EGRESS                                                \
	FS_REQUIRED_CAPS(                                                      \
		FS_CAP(flow_table_properties_nic_transmit.flow_modify_en),     \
		FS_CAP(flow_table_properties_nic_transmit.modify_root),        \
		FS_CAP(flow_table_properties_nic_transmit                      \
			       .identified_miss_table_mode),                   \
		FS_CAP(flow_table_properties_nic_transmit.flow_table_modify))

#define FS_CHAINING_CAPS_RDMA_TX                                                \
	FS_REQUIRED_CAPS(                                                       \
		FS_CAP(flow_table_properties_nic_transmit_rdma.flow_modify_en), \
		FS_CAP(flow_table_properties_nic_transmit_rdma.modify_root),    \
		FS_CAP(flow_table_properties_nic_transmit_rdma                  \
			       .identified_miss_table_mode),                    \
		FS_CAP(flow_table_properties_nic_transmit_rdma                  \
			       .flow_table_modify))

#define LEFTOVERS_NUM_LEVELS 1
#define LEFTOVERS_NUM_PRIOS 1

#define RDMA_RX_COUNTERS_PRIO_NUM_LEVELS 1
#define RDMA_TX_COUNTERS_PRIO_NUM_LEVELS 1

#define BY_PASS_PRIO_NUM_LEVELS 1
#define BY_PASS_MIN_LEVEL (ETHTOOL_MIN_LEVEL + MLX5_BY_PASS_NUM_PRIOS +\
			   LEFTOVERS_NUM_PRIOS)

#define KERNEL_RX_MACSEC_NUM_PRIOS  1
#define KERNEL_RX_MACSEC_NUM_LEVELS 2
#define KERNEL_RX_MACSEC_MIN_LEVEL (BY_PASS_MIN_LEVEL + KERNEL_RX_MACSEC_NUM_PRIOS)

#define ETHTOOL_PRIO_NUM_LEVELS 1
#define ETHTOOL_NUM_PRIOS 11
#define ETHTOOL_MIN_LEVEL (KERNEL_MIN_LEVEL + ETHTOOL_NUM_PRIOS)
/* Promiscuous, Vlan, mac, ttc, inner ttc, {UDP/ANY/aRFS/accel/{esp, esp_err}}, IPsec policy,
 * IPsec RoCE policy
 */
#define KERNEL_NIC_PRIO_NUM_LEVELS 9
#define KERNEL_NIC_NUM_PRIOS 1
/* One more level for tc */
#define KERNEL_MIN_LEVEL (KERNEL_NIC_PRIO_NUM_LEVELS + 1)

#define KERNEL_NIC_TC_NUM_PRIOS  1
#define KERNEL_NIC_TC_NUM_LEVELS 3

#define ANCHOR_NUM_LEVELS 1
#define ANCHOR_NUM_PRIOS 1
#define ANCHOR_MIN_LEVEL (BY_PASS_MIN_LEVEL + 1)

#define OFFLOADS_MAX_FT 2
#define OFFLOADS_NUM_PRIOS 2
#define OFFLOADS_MIN_LEVEL (ANCHOR_MIN_LEVEL + OFFLOADS_NUM_PRIOS)

#define LAG_PRIO_NUM_LEVELS 1
#define LAG_NUM_PRIOS 1
#define LAG_MIN_LEVEL (OFFLOADS_MIN_LEVEL + KERNEL_RX_MACSEC_MIN_LEVEL + 1)

#define KERNEL_TX_IPSEC_NUM_PRIOS  1
#define KERNEL_TX_IPSEC_NUM_LEVELS 3
#define KERNEL_TX_IPSEC_MIN_LEVEL        (KERNEL_TX_IPSEC_NUM_LEVELS)

#define KERNEL_TX_MACSEC_NUM_PRIOS  1
#define KERNEL_TX_MACSEC_NUM_LEVELS 2
#define KERNEL_TX_MACSEC_MIN_LEVEL       (KERNEL_TX_IPSEC_MIN_LEVEL + KERNEL_TX_MACSEC_NUM_PRIOS)

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
	enum mlx5_flow_table_miss_action def_miss_action;
} root_fs = {
	.type = FS_TYPE_NAMESPACE,
	.ar_size = 8,
	  .children = (struct init_tree_node[]){
		  ADD_PRIO(0, BY_PASS_MIN_LEVEL, 0, FS_CHAINING_CAPS,
			   ADD_NS(MLX5_FLOW_TABLE_MISS_ACTION_DEF,
				  ADD_MULTIPLE_PRIO(MLX5_BY_PASS_NUM_PRIOS,
						    BY_PASS_PRIO_NUM_LEVELS))),
		  ADD_PRIO(0, KERNEL_RX_MACSEC_MIN_LEVEL, 0, FS_CHAINING_CAPS,
			   ADD_NS(MLX5_FLOW_TABLE_MISS_ACTION_DEF,
				  ADD_MULTIPLE_PRIO(KERNEL_RX_MACSEC_NUM_PRIOS,
						    KERNEL_RX_MACSEC_NUM_LEVELS))),
		  ADD_PRIO(0, LAG_MIN_LEVEL, 0, FS_CHAINING_CAPS,
			   ADD_NS(MLX5_FLOW_TABLE_MISS_ACTION_DEF,
				  ADD_MULTIPLE_PRIO(LAG_NUM_PRIOS,
						    LAG_PRIO_NUM_LEVELS))),
		  ADD_PRIO(0, OFFLOADS_MIN_LEVEL, 0, FS_CHAINING_CAPS,
			   ADD_NS(MLX5_FLOW_TABLE_MISS_ACTION_DEF,
				  ADD_MULTIPLE_PRIO(OFFLOADS_NUM_PRIOS,
						    OFFLOADS_MAX_FT))),
		  ADD_PRIO(0, ETHTOOL_MIN_LEVEL, 0, FS_CHAINING_CAPS,
			   ADD_NS(MLX5_FLOW_TABLE_MISS_ACTION_DEF,
				  ADD_MULTIPLE_PRIO(ETHTOOL_NUM_PRIOS,
						    ETHTOOL_PRIO_NUM_LEVELS))),
		  ADD_PRIO(0, KERNEL_MIN_LEVEL, 0, {},
			   ADD_NS(MLX5_FLOW_TABLE_MISS_ACTION_DEF,
				  ADD_MULTIPLE_PRIO(KERNEL_NIC_TC_NUM_PRIOS,
						    KERNEL_NIC_TC_NUM_LEVELS),
				  ADD_MULTIPLE_PRIO(KERNEL_NIC_NUM_PRIOS,
						    KERNEL_NIC_PRIO_NUM_LEVELS))),
		  ADD_PRIO(0, BY_PASS_MIN_LEVEL, 0, FS_CHAINING_CAPS,
			   ADD_NS(MLX5_FLOW_TABLE_MISS_ACTION_DEF,
				  ADD_MULTIPLE_PRIO(LEFTOVERS_NUM_PRIOS,
						    LEFTOVERS_NUM_LEVELS))),
		  ADD_PRIO(0, ANCHOR_MIN_LEVEL, 0, {},
			   ADD_NS(MLX5_FLOW_TABLE_MISS_ACTION_DEF,
				  ADD_MULTIPLE_PRIO(ANCHOR_NUM_PRIOS,
						    ANCHOR_NUM_LEVELS))),
	}
};

static struct init_tree_node egress_root_fs = {
	.type = FS_TYPE_NAMESPACE,
	.ar_size = 3,
	.children = (struct init_tree_node[]) {
		ADD_PRIO(0, MLX5_BY_PASS_NUM_PRIOS, 0,
			 FS_CHAINING_CAPS_EGRESS,
			 ADD_NS(MLX5_FLOW_TABLE_MISS_ACTION_DEF,
				ADD_MULTIPLE_PRIO(MLX5_BY_PASS_NUM_PRIOS,
						  BY_PASS_PRIO_NUM_LEVELS))),
		ADD_PRIO(0, KERNEL_TX_IPSEC_MIN_LEVEL, 0,
			 FS_CHAINING_CAPS_EGRESS,
			 ADD_NS(MLX5_FLOW_TABLE_MISS_ACTION_DEF,
				ADD_MULTIPLE_PRIO(KERNEL_TX_IPSEC_NUM_PRIOS,
						  KERNEL_TX_IPSEC_NUM_LEVELS))),
		ADD_PRIO(0, KERNEL_TX_MACSEC_MIN_LEVEL, 0,
			 FS_CHAINING_CAPS_EGRESS,
			 ADD_NS(MLX5_FLOW_TABLE_MISS_ACTION_DEF,
				ADD_MULTIPLE_PRIO(KERNEL_TX_MACSEC_NUM_PRIOS,
						  KERNEL_TX_MACSEC_NUM_LEVELS))),
	}
};

enum {
	RDMA_RX_IPSEC_PRIO,
	RDMA_RX_COUNTERS_PRIO,
	RDMA_RX_BYPASS_PRIO,
	RDMA_RX_KERNEL_PRIO,
};

#define RDMA_RX_IPSEC_NUM_PRIOS 1
#define RDMA_RX_IPSEC_NUM_LEVELS 2
#define RDMA_RX_IPSEC_MIN_LEVEL  (RDMA_RX_IPSEC_NUM_LEVELS)

#define RDMA_RX_BYPASS_MIN_LEVEL MLX5_BY_PASS_NUM_REGULAR_PRIOS
#define RDMA_RX_KERNEL_MIN_LEVEL (RDMA_RX_BYPASS_MIN_LEVEL + 1)
#define RDMA_RX_COUNTERS_MIN_LEVEL (RDMA_RX_KERNEL_MIN_LEVEL + 2)

static struct init_tree_node rdma_rx_root_fs = {
	.type = FS_TYPE_NAMESPACE,
	.ar_size = 4,
	.children = (struct init_tree_node[]) {
		[RDMA_RX_IPSEC_PRIO] =
		ADD_PRIO(0, RDMA_RX_IPSEC_MIN_LEVEL, 0,
			 FS_CHAINING_CAPS,
			 ADD_NS(MLX5_FLOW_TABLE_MISS_ACTION_DEF,
				ADD_MULTIPLE_PRIO(RDMA_RX_IPSEC_NUM_PRIOS,
						  RDMA_RX_IPSEC_NUM_LEVELS))),
		[RDMA_RX_COUNTERS_PRIO] =
		ADD_PRIO(0, RDMA_RX_COUNTERS_MIN_LEVEL, 0,
			 FS_CHAINING_CAPS,
			 ADD_NS(MLX5_FLOW_TABLE_MISS_ACTION_DEF,
				ADD_MULTIPLE_PRIO(MLX5_RDMA_RX_NUM_COUNTERS_PRIOS,
						  RDMA_RX_COUNTERS_PRIO_NUM_LEVELS))),
		[RDMA_RX_BYPASS_PRIO] =
		ADD_PRIO(0, RDMA_RX_BYPASS_MIN_LEVEL, 0,
			 FS_CHAINING_CAPS,
			 ADD_NS(MLX5_FLOW_TABLE_MISS_ACTION_DEF,
				ADD_MULTIPLE_PRIO(MLX5_BY_PASS_NUM_REGULAR_PRIOS,
						  BY_PASS_PRIO_NUM_LEVELS))),
		[RDMA_RX_KERNEL_PRIO] =
		ADD_PRIO(0, RDMA_RX_KERNEL_MIN_LEVEL, 0,
			 FS_CHAINING_CAPS,
			 ADD_NS(MLX5_FLOW_TABLE_MISS_ACTION_SWITCH_DOMAIN,
				ADD_MULTIPLE_PRIO(1, 1))),
	}
};

enum {
	RDMA_TX_COUNTERS_PRIO,
	RDMA_TX_IPSEC_PRIO,
	RDMA_TX_BYPASS_PRIO,
};

#define RDMA_TX_BYPASS_MIN_LEVEL MLX5_BY_PASS_NUM_PRIOS
#define RDMA_TX_COUNTERS_MIN_LEVEL (RDMA_TX_BYPASS_MIN_LEVEL + 1)

#define RDMA_TX_IPSEC_NUM_PRIOS 1
#define RDMA_TX_IPSEC_PRIO_NUM_LEVELS 1
#define RDMA_TX_IPSEC_MIN_LEVEL  (RDMA_TX_COUNTERS_MIN_LEVEL + RDMA_TX_IPSEC_NUM_PRIOS)

static struct init_tree_node rdma_tx_root_fs = {
	.type = FS_TYPE_NAMESPACE,
	.ar_size = 3,
	.children = (struct init_tree_node[]) {
		[RDMA_TX_COUNTERS_PRIO] =
		ADD_PRIO(0, RDMA_TX_COUNTERS_MIN_LEVEL, 0,
			 FS_CHAINING_CAPS,
			 ADD_NS(MLX5_FLOW_TABLE_MISS_ACTION_DEF,
				ADD_MULTIPLE_PRIO(MLX5_RDMA_TX_NUM_COUNTERS_PRIOS,
						  RDMA_TX_COUNTERS_PRIO_NUM_LEVELS))),
		[RDMA_TX_IPSEC_PRIO] =
		ADD_PRIO(0, RDMA_TX_IPSEC_MIN_LEVEL, 0,
			 FS_CHAINING_CAPS,
			 ADD_NS(MLX5_FLOW_TABLE_MISS_ACTION_DEF,
				ADD_MULTIPLE_PRIO(RDMA_TX_IPSEC_NUM_PRIOS,
						  RDMA_TX_IPSEC_PRIO_NUM_LEVELS))),

		[RDMA_TX_BYPASS_PRIO] =
		ADD_PRIO(0, RDMA_TX_BYPASS_MIN_LEVEL, 0,
			 FS_CHAINING_CAPS_RDMA_TX,
			 ADD_NS(MLX5_FLOW_TABLE_MISS_ACTION_DEF,
				ADD_MULTIPLE_PRIO(RDMA_TX_BYPASS_MIN_LEVEL,
						  BY_PASS_PRIO_NUM_LEVELS))),
	}
};

enum fs_i_lock_class {
	FS_LOCK_GRANDPARENT,
	FS_LOCK_PARENT,
	FS_LOCK_CHILD
};

static const struct rhashtable_params rhash_fte = {
	.key_len = sizeof_field(struct fs_fte, val),
	.key_offset = offsetof(struct fs_fte, val),
	.head_offset = offsetof(struct fs_fte, hash),
	.automatic_shrinking = true,
	.min_size = 1,
};

static const struct rhashtable_params rhash_fg = {
	.key_len = sizeof_field(struct mlx5_flow_group, mask),
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

static void down_write_ref_node(struct fs_node *node, bool locked)
{
	if (node) {
		if (!locked)
			down_write(&node->lock);
		refcount_inc(&node->refcount);
	}
}

static void up_read_ref_node(struct fs_node *node)
{
	refcount_dec(&node->refcount);
	up_read(&node->lock);
}

static void up_write_ref_node(struct fs_node *node, bool locked)
{
	refcount_dec(&node->refcount);
	if (!locked)
		up_write(&node->lock);
}

static void tree_put_node(struct fs_node *node, bool locked)
{
	struct fs_node *parent_node = node->parent;

	if (refcount_dec_and_test(&node->refcount)) {
		if (node->del_hw_func)
			node->del_hw_func(node);
		if (parent_node) {
			down_write_ref_node(parent_node, locked);
			list_del_init(&node->list);
		}
		node->del_sw_func(node);
		if (parent_node)
			up_write_ref_node(parent_node, locked);
		node = NULL;
	}
	if (!node && parent_node)
		tree_put_node(parent_node, locked);
}

static int tree_remove_node(struct fs_node *node, bool locked)
{
	if (refcount_read(&node->refcount) > 1) {
		refcount_dec(&node->refcount);
		return -EEXIST;
	}
	tree_put_node(node, locked);
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

static bool is_fwd_next_action(u32 action)
{
	return action & (MLX5_FLOW_CONTEXT_ACTION_FWD_NEXT_PRIO |
			 MLX5_FLOW_CONTEXT_ACTION_FWD_NEXT_NS);
}

static bool is_fwd_dest_type(enum mlx5_flow_destination_type type)
{
	return type == MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE_NUM ||
		type == MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE ||
		type == MLX5_FLOW_DESTINATION_TYPE_UPLINK ||
		type == MLX5_FLOW_DESTINATION_TYPE_VPORT ||
		type == MLX5_FLOW_DESTINATION_TYPE_FLOW_SAMPLER ||
		type == MLX5_FLOW_DESTINATION_TYPE_TIR ||
		type == MLX5_FLOW_DESTINATION_TYPE_RANGE ||
		type == MLX5_FLOW_DESTINATION_TYPE_TABLE_TYPE;
}

static bool check_valid_spec(const struct mlx5_flow_spec *spec)
{
	int i;

	for (i = 0; i < MLX5_ST_SZ_DW_MATCH_PARAM; i++)
		if (spec->match_value[i] & ~spec->match_criteria[i]) {
			pr_warn("mlx5_core: match_value differs from match_criteria\n");
			return false;
		}

	return true;
}

struct mlx5_flow_root_namespace *find_root(struct fs_node *node)
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
	trace_mlx5_fs_del_ft(ft);

	if (node->active) {
		err = root->cmds->destroy_flow_table(root, ft);
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
	if (ft->node.parent) {
		fs_get_obj(prio, ft->node.parent);
		prio->num_ft--;
	}
	kfree(ft);
}

static void modify_fte(struct fs_fte *fte)
{
	struct mlx5_flow_root_namespace *root;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *fg;
	struct mlx5_core_dev *dev;
	int err;

	fs_get_obj(fg, fte->node.parent);
	fs_get_obj(ft, fg->node.parent);
	dev = get_dev(&fte->node);

	root = find_root(&ft->node);
	err = root->cmds->update_fte(root, ft, fg, fte->modify_mask, fte);
	if (err)
		mlx5_core_warn(dev,
			       "%s can't del rule fg id=%d fte_index=%d\n",
			       __func__, fg->id, fte->index);
	fte->modify_mask = 0;
}

static void del_sw_hw_rule(struct fs_node *node)
{
	struct mlx5_flow_rule *rule;
	struct fs_fte *fte;

	fs_get_obj(rule, node);
	fs_get_obj(fte, rule->node.parent);
	trace_mlx5_fs_del_rule(rule);
	if (is_fwd_next_action(rule->sw_action)) {
		mutex_lock(&rule->dest_attr.ft->lock);
		list_del(&rule->next_ft);
		mutex_unlock(&rule->dest_attr.ft->lock);
	}

	if (rule->dest_attr.type == MLX5_FLOW_DESTINATION_TYPE_COUNTER) {
		--fte->dests_size;
		fte->modify_mask |=
			BIT(MLX5_SET_FTE_MODIFY_ENABLE_MASK_ACTION) |
			BIT(MLX5_SET_FTE_MODIFY_ENABLE_MASK_FLOW_COUNTERS);
		fte->action.action &= ~MLX5_FLOW_CONTEXT_ACTION_COUNT;
		goto out;
	}

	if (rule->dest_attr.type == MLX5_FLOW_DESTINATION_TYPE_PORT) {
		--fte->dests_size;
		fte->modify_mask |= BIT(MLX5_SET_FTE_MODIFY_ENABLE_MASK_ACTION);
		fte->action.action &= ~MLX5_FLOW_CONTEXT_ACTION_ALLOW;
		goto out;
	}

	if (is_fwd_dest_type(rule->dest_attr.type)) {
		--fte->dests_size;
		--fte->fwd_dests;

		if (!fte->fwd_dests)
			fte->action.action &=
				~MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
		fte->modify_mask |=
			BIT(MLX5_SET_FTE_MODIFY_ENABLE_MASK_DESTINATION_LIST);
		goto out;
	}
out:
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
	WARN_ON(fte->dests_size);
	dev = get_dev(&ft->node);
	root = find_root(&ft->node);
	if (node->active) {
		err = root->cmds->delete_fte(root, ft, fte);
		if (err)
			mlx5_core_warn(dev,
				       "flow steering can't delete fte in index %d of flow group id %d\n",
				       fte->index, fg->id);
		node->active = false;
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
	ida_free(&fg->fte_allocator, fte->index - fg->start_index);
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
	if (fg->node.active && root->cmds->destroy_flow_group(root, ft, fg))
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
	if (ft->autogroup.active &&
	    fg->max_ftes == ft->autogroup.group_size &&
	    fg->start_index < ft->autogroup.max_fte)
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

	index = ida_alloc_max(&fg->fte_allocator, fg->max_ftes - 1, GFP_KERNEL);
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
	ida_free(&fg->fte_allocator, index);
	return ret;
}

static struct fs_fte *alloc_fte(struct mlx5_flow_table *ft,
				const struct mlx5_flow_spec *spec,
				struct mlx5_flow_act *flow_act)
{
	struct mlx5_flow_steering *steering = get_steering(&ft->node);
	struct fs_fte *fte;

	fte = kmem_cache_zalloc(steering->ftes_cache, GFP_KERNEL);
	if (!fte)
		return ERR_PTR(-ENOMEM);

	memcpy(fte->val, &spec->match_value, sizeof(fte->val));
	fte->node.type =  FS_TYPE_FLOW_ENTRY;
	fte->action = *flow_act;
	fte->flow_context = spec->flow_context;

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
						const void *match_criteria,
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
						       const void *match_criteria,
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

static struct mlx5_flow_table *alloc_flow_table(int level, u16 vport,
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

static struct fs_node *find_prio_chains_parent(struct fs_node *parent,
					       struct fs_node **child)
{
	struct fs_node *node = NULL;

	while (parent && parent->type != FS_TYPE_PRIO_CHAINS) {
		node = parent;
		parent = parent->parent;
	}

	if (child)
		*child = node;

	return parent;
}

/* If reverse is false then return the first flow table next to the passed node
 * in the tree, else return the last flow table before the node in the tree.
 * If skip is true, skip the flow tables in the same prio_chains prio.
 */
static struct mlx5_flow_table *find_closest_ft(struct fs_node *node, bool reverse,
					       bool skip)
{
	struct fs_node *prio_chains_parent = NULL;
	struct mlx5_flow_table *ft = NULL;
	struct fs_node *curr_node;
	struct fs_node *parent;

	if (skip)
		prio_chains_parent = find_prio_chains_parent(node, NULL);
	parent = node->parent;
	curr_node = node;
	while (!ft && parent) {
		if (parent != prio_chains_parent)
			ft = find_closest_ft_recursive(parent, &curr_node->list,
						       reverse);
		curr_node = parent;
		parent = curr_node->parent;
	}
	return ft;
}

/* Assuming all the tree is locked by mutex chain lock */
static struct mlx5_flow_table *find_next_chained_ft(struct fs_node *node)
{
	return find_closest_ft(node, false, true);
}

/* Assuming all the tree is locked by mutex chain lock */
static struct mlx5_flow_table *find_prev_chained_ft(struct fs_node *node)
{
	return find_closest_ft(node, true, true);
}

static struct mlx5_flow_table *find_next_fwd_ft(struct mlx5_flow_table *ft,
						struct mlx5_flow_act *flow_act)
{
	struct fs_prio *prio;
	bool next_ns;

	next_ns = flow_act->action & MLX5_FLOW_CONTEXT_ACTION_FWD_NEXT_NS;
	fs_get_obj(prio, next_ns ? ft->ns->node.parent : ft->node.parent);

	return find_next_chained_ft(&prio->node);
}

static int connect_fts_in_prio(struct mlx5_core_dev *dev,
			       struct fs_prio *prio,
			       struct mlx5_flow_table *ft)
{
	struct mlx5_flow_root_namespace *root = find_root(&prio->node);
	struct mlx5_flow_table *iter;
	int err;

	fs_for_each_ft(iter, prio) {
		err = root->cmds->modify_flow_table(root, iter, ft);
		if (err) {
			mlx5_core_err(dev,
				      "Failed to modify flow table id %d, type %d, err %d\n",
				      iter->id, iter->type, err);
			/* The driver is out of sync with the FW */
			return err;
		}
	}
	return 0;
}

static struct mlx5_flow_table *find_closet_ft_prio_chains(struct fs_node *node,
							  struct fs_node *parent,
							  struct fs_node **child,
							  bool reverse)
{
	struct mlx5_flow_table *ft;

	ft = find_closest_ft(node, reverse, false);

	if (ft && parent == find_prio_chains_parent(&ft->node, child))
		return ft;

	return NULL;
}

/* Connect flow tables from previous priority of prio to ft */
static int connect_prev_fts(struct mlx5_core_dev *dev,
			    struct mlx5_flow_table *ft,
			    struct fs_prio *prio)
{
	struct fs_node *prio_parent, *parent = NULL, *child, *node;
	struct mlx5_flow_table *prev_ft;
	int err = 0;

	prio_parent = find_prio_chains_parent(&prio->node, &child);

	/* return directly if not under the first sub ns of prio_chains prio */
	if (prio_parent && !list_is_first(&child->list, &prio_parent->children))
		return 0;

	prev_ft = find_prev_chained_ft(&prio->node);
	while (prev_ft) {
		struct fs_prio *prev_prio;

		fs_get_obj(prev_prio, prev_ft->node.parent);
		err = connect_fts_in_prio(dev, prev_prio, ft);
		if (err)
			break;

		if (!parent) {
			parent = find_prio_chains_parent(&prev_prio->node, &child);
			if (!parent)
				break;
		}

		node = child;
		prev_ft = find_closet_ft_prio_chains(node, parent, &child, true);
	}
	return err;
}

static int update_root_ft_create(struct mlx5_flow_table *ft, struct fs_prio
				 *prio)
{
	struct mlx5_flow_root_namespace *root = find_root(&prio->node);
	struct mlx5_ft_underlay_qp *uqp;
	int min_level = INT_MAX;
	int err = 0;
	u32 qpn;

	if (root->root_ft)
		min_level = root->root_ft->level;

	if (ft->level >= min_level)
		return 0;

	if (list_empty(&root->underlay_qpns)) {
		/* Don't set any QPN (zero) in case QPN list is empty */
		qpn = 0;
		err = root->cmds->update_root_ft(root, ft, qpn, false);
	} else {
		list_for_each_entry(uqp, &root->underlay_qpns, list) {
			qpn = uqp->qpn;
			err = root->cmds->update_root_ft(root, ft,
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
	down_write_ref_node(&fte->node, false);
	fs_get_obj(fg, fte->node.parent);
	fs_get_obj(ft, fg->node.parent);

	memcpy(&rule->dest_attr, dest, sizeof(*dest));
	root = find_root(&ft->node);
	err = root->cmds->update_fte(root, ft, fg,
				     modify_mask, fte);
	up_write_ref_node(&fte->node, false);

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
		if (mlx5_flow_dests_cmp(old_dest, &handle->rule[i]->dest_attr))
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
		if ((iter->sw_action & MLX5_FLOW_CONTEXT_ACTION_FWD_NEXT_NS) &&
		    iter->ft->ns == new_next_ft->ns)
			continue;

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
	struct mlx5_flow_table *next_ft, *first_ft;
	int err = 0;

	/* Connect_prev_fts and update_root_ft_create are mutually exclusive */

	first_ft = list_first_entry_or_null(&prio->node.children,
					    struct mlx5_flow_table, node.list);
	if (!first_ft || first_ft->level > ft->level) {
		err = connect_prev_fts(dev, ft, prio);
		if (err)
			return err;

		next_ft = first_ft ? first_ft : find_next_chained_ft(&prio->node);
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
	bool unmanaged = ft_attr->flags & MLX5_FLOW_TABLE_UNMANAGED;
	struct mlx5_flow_table *next_ft;
	struct fs_prio *fs_prio = NULL;
	struct mlx5_flow_table *ft;
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
	if (!unmanaged) {
		/* The level is related to the
		 * priority level range.
		 */
		if (ft_attr->level >= fs_prio->num_levels) {
			err = -ENOSPC;
			goto unlock_root;
		}

		ft_attr->level += fs_prio->start_level;
	}

	/* The level is related to the
	 * priority level range.
	 */
	ft = alloc_flow_table(ft_attr->level,
			      vport,
			      root->table_type,
			      op_mod, ft_attr->flags);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		goto unlock_root;
	}

	tree_init_node(&ft->node, del_hw_flow_table, del_sw_flow_table);
	next_ft = unmanaged ? ft_attr->next_ft :
			      find_next_chained_ft(&fs_prio->node);
	ft->def_miss_action = ns->def_miss_action;
	ft->ns = ns;
	err = root->cmds->create_flow_table(root, ft, ft_attr, next_ft);
	if (err)
		goto free_ft;

	if (!unmanaged) {
		err = connect_flow_table(root->dev, ft, fs_prio);
		if (err)
			goto destroy_ft;
	}

	ft->node.active = true;
	down_write_ref_node(&fs_prio->node, false);
	if (!unmanaged) {
		tree_add_node(&ft->node, &fs_prio->node);
		list_add_flow_table(ft, fs_prio);
	} else {
		ft->node.root = fs_prio->node.root;
	}
	fs_prio->num_ft++;
	up_write_ref_node(&fs_prio->node, false);
	mutex_unlock(&root->chain_lock);
	trace_mlx5_fs_add_ft(ft);
	return ft;
destroy_ft:
	root->cmds->destroy_flow_table(root, ft);
free_ft:
	rhltable_destroy(&ft->fgs_hash);
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
EXPORT_SYMBOL(mlx5_create_flow_table);

u32 mlx5_flow_table_id(struct mlx5_flow_table *ft)
{
	return ft->id;
}
EXPORT_SYMBOL(mlx5_flow_table_id);

struct mlx5_flow_table *
mlx5_create_vport_flow_table(struct mlx5_flow_namespace *ns,
			     struct mlx5_flow_table_attr *ft_attr, u16 vport)
{
	return __mlx5_create_flow_table(ns, ft_attr, FS_FT_OP_MOD_NORMAL, vport);
}

struct mlx5_flow_table*
mlx5_create_lag_demux_flow_table(struct mlx5_flow_namespace *ns,
				 int prio, u32 level)
{
	struct mlx5_flow_table_attr ft_attr = {};

	ft_attr.level = level;
	ft_attr.prio  = prio;
	ft_attr.max_fte = 1;

	return __mlx5_create_flow_table(ns, &ft_attr, FS_FT_OP_MOD_LAG_DEMUX, 0);
}
EXPORT_SYMBOL(mlx5_create_lag_demux_flow_table);

#define MAX_FLOW_GROUP_SIZE BIT(24)
struct mlx5_flow_table*
mlx5_create_auto_grouped_flow_table(struct mlx5_flow_namespace *ns,
				    struct mlx5_flow_table_attr *ft_attr)
{
	int num_reserved_entries = ft_attr->autogroup.num_reserved_entries;
	int max_num_groups = ft_attr->autogroup.max_num_groups;
	struct mlx5_flow_table *ft;
	int autogroups_max_fte;

	ft = mlx5_create_flow_table(ns, ft_attr);
	if (IS_ERR(ft))
		return ft;

	autogroups_max_fte = ft->max_fte - num_reserved_entries;
	if (max_num_groups > autogroups_max_fte)
		goto err_validate;
	if (num_reserved_entries > ft->max_fte)
		goto err_validate;

	/* Align the number of groups according to the largest group size */
	if (autogroups_max_fte / (max_num_groups + 1) > MAX_FLOW_GROUP_SIZE)
		max_num_groups = (autogroups_max_fte / MAX_FLOW_GROUP_SIZE) - 1;

	ft->autogroup.active = true;
	ft->autogroup.required_groups = max_num_groups;
	ft->autogroup.max_fte = autogroups_max_fte;
	/* We save place for flow groups in addition to max types */
	ft->autogroup.group_size = autogroups_max_fte / (max_num_groups + 1);

	return ft;

err_validate:
	mlx5_destroy_flow_table(ft);
	return ERR_PTR(-ENOSPC);
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
	struct mlx5_flow_group *fg;
	int err;

	if (ft->autogroup.active && start_index < ft->autogroup.max_fte)
		return ERR_PTR(-EPERM);

	down_write_ref_node(&ft->node, false);
	fg = alloc_insert_flow_group(ft, match_criteria_enable, match_criteria,
				     start_index, end_index,
				     ft->node.children.prev);
	up_write_ref_node(&ft->node, false);
	if (IS_ERR(fg))
		return fg;

	err = root->cmds->create_flow_group(root, ft, fg_in, fg);
	if (err) {
		tree_put_node(&fg->node, false);
		return ERR_PTR(err);
	}
	trace_mlx5_fs_add_fg(fg);
	fg->node.active = true;

	return fg;
}
EXPORT_SYMBOL(mlx5_create_flow_group);

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
	else
		rule->dest_attr.type = MLX5_FLOW_DESTINATION_TYPE_NONE;

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

			if (is_fwd_dest_type(dest[i].type))
				fte->fwd_dests++;

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
		err = root->cmds->create_fte(root, ft, fg, fte);
	else
		err = root->cmds->update_fte(root, ft, fg, modify_mask, fte);
	if (err)
		goto free_handle;

	fte->node.active = true;
	fte->status |= FS_FTE_STATUS_EXISTING;
	atomic_inc(&fg->node.version);

out:
	return handle;

free_handle:
	destroy_flow_handle(fte, handle, dest, handle->num_rules);
	return ERR_PTR(err);
}

static struct mlx5_flow_group *alloc_auto_flow_group(struct mlx5_flow_table  *ft,
						     const struct mlx5_flow_spec *spec)
{
	struct list_head *prev = &ft->node.children;
	u32 max_fte = ft->autogroup.max_fte;
	unsigned int candidate_index = 0;
	unsigned int group_size = 0;
	struct mlx5_flow_group *fg;

	if (!ft->autogroup.active)
		return ERR_PTR(-ENOENT);

	if (ft->autogroup.num_groups < ft->autogroup.required_groups)
		group_size = ft->autogroup.group_size;

	/*  max_fte == ft->autogroup.max_types */
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

	if (candidate_index + group_size > max_fte)
		return ERR_PTR(-ENOSPC);

	fg = alloc_insert_flow_group(ft,
				     spec->match_criteria_enable,
				     spec->match_criteria,
				     candidate_index,
				     candidate_index + group_size - 1,
				     prev);
	if (IS_ERR(fg))
		goto out;

	if (group_size == ft->autogroup.group_size)
		ft->autogroup.num_groups++;

out:
	return fg;
}

static int create_auto_flow_group(struct mlx5_flow_table *ft,
				  struct mlx5_flow_group *fg)
{
	struct mlx5_flow_root_namespace *root = find_root(&ft->node);
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

	err = root->cmds->create_flow_group(root, ft, in, fg);
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
		if (((d1->type == MLX5_FLOW_DESTINATION_TYPE_VPORT ||
		      d1->type == MLX5_FLOW_DESTINATION_TYPE_UPLINK) &&
		     d1->vport.num == d2->vport.num &&
		     d1->vport.flags == d2->vport.flags &&
		     ((d1->vport.flags & MLX5_FLOW_DEST_VPORT_VHCA_ID) ?
		      (d1->vport.vhca_id == d2->vport.vhca_id) : true) &&
		     ((d1->vport.flags & MLX5_FLOW_DEST_VPORT_REFORMAT_ID) ?
		      (d1->vport.pkt_reformat->id ==
		       d2->vport.pkt_reformat->id) : true)) ||
		    (d1->type == MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE &&
		     d1->ft == d2->ft) ||
		    (d1->type == MLX5_FLOW_DESTINATION_TYPE_TIR &&
		     d1->tir_num == d2->tir_num) ||
		    (d1->type == MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE_NUM &&
		     d1->ft_num == d2->ft_num) ||
		    (d1->type == MLX5_FLOW_DESTINATION_TYPE_FLOW_SAMPLER &&
		     d1->sampler_id == d2->sampler_id) ||
		    (d1->type == MLX5_FLOW_DESTINATION_TYPE_RANGE &&
		     d1->range.field == d2->range.field &&
		     d1->range.hit_ft == d2->range.hit_ft &&
		     d1->range.miss_ft == d2->range.miss_ft &&
		     d1->range.min == d2->range.min &&
		     d1->range.max == d2->range.max))
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

static bool check_conflicting_actions_vlan(const struct mlx5_fs_vlan *vlan0,
					   const struct mlx5_fs_vlan *vlan1)
{
	return vlan0->ethtype != vlan1->ethtype ||
	       vlan0->vid != vlan1->vid ||
	       vlan0->prio != vlan1->prio;
}

static bool check_conflicting_actions(const struct mlx5_flow_act *act1,
				      const struct mlx5_flow_act *act2)
{
	u32 action1 = act1->action;
	u32 action2 = act2->action;
	u32 xored_actions;

	xored_actions = action1 ^ action2;

	/* if one rule only wants to count, it's ok */
	if (action1 == MLX5_FLOW_CONTEXT_ACTION_COUNT ||
	    action2 == MLX5_FLOW_CONTEXT_ACTION_COUNT)
		return false;

	if (xored_actions & (MLX5_FLOW_CONTEXT_ACTION_DROP  |
			     MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT |
			     MLX5_FLOW_CONTEXT_ACTION_DECAP |
			     MLX5_FLOW_CONTEXT_ACTION_MOD_HDR  |
			     MLX5_FLOW_CONTEXT_ACTION_VLAN_POP |
			     MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH |
			     MLX5_FLOW_CONTEXT_ACTION_VLAN_POP_2 |
			     MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH_2))
		return true;

	if (action1 & MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT &&
	    act1->pkt_reformat != act2->pkt_reformat)
		return true;

	if (action1 & MLX5_FLOW_CONTEXT_ACTION_MOD_HDR &&
	    act1->modify_hdr != act2->modify_hdr)
		return true;

	if (action1 & MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH &&
	    check_conflicting_actions_vlan(&act1->vlan[0], &act2->vlan[0]))
		return true;

	if (action1 & MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH_2 &&
	    check_conflicting_actions_vlan(&act1->vlan[1], &act2->vlan[1]))
		return true;

	return false;
}

static int check_conflicting_ftes(struct fs_fte *fte,
				  const struct mlx5_flow_context *flow_context,
				  const struct mlx5_flow_act *flow_act)
{
	if (check_conflicting_actions(flow_act, &fte->action)) {
		mlx5_core_warn(get_dev(&fte->node),
			       "Found two FTEs with conflicting actions\n");
		return -EEXIST;
	}

	if ((flow_context->flags & FLOW_CONTEXT_HAS_TAG) &&
	    fte->flow_context.flow_tag != flow_context->flow_tag) {
		mlx5_core_warn(get_dev(&fte->node),
			       "FTE flow tag %u already exists with different flow tag %u\n",
			       fte->flow_context.flow_tag,
			       flow_context->flow_tag);
		return -EEXIST;
	}

	return 0;
}

static struct mlx5_flow_handle *add_rule_fg(struct mlx5_flow_group *fg,
					    const struct mlx5_flow_spec *spec,
					    struct mlx5_flow_act *flow_act,
					    struct mlx5_flow_destination *dest,
					    int dest_num,
					    struct fs_fte *fte)
{
	struct mlx5_flow_handle *handle;
	int old_action;
	int i;
	int ret;

	ret = check_conflicting_ftes(fte, &spec->flow_context, flow_act);
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

static bool counter_is_valid(u32 action)
{
	return (action & (MLX5_FLOW_CONTEXT_ACTION_DROP |
			  MLX5_FLOW_CONTEXT_ACTION_ALLOW |
			  MLX5_FLOW_CONTEXT_ACTION_FWD_DEST));
}

static bool dest_is_valid(struct mlx5_flow_destination *dest,
			  struct mlx5_flow_act *flow_act,
			  struct mlx5_flow_table *ft)
{
	bool ignore_level = flow_act->flags & FLOW_ACT_IGNORE_FLOW_LEVEL;
	u32 action = flow_act->action;

	if (dest && (dest->type == MLX5_FLOW_DESTINATION_TYPE_COUNTER))
		return counter_is_valid(action);

	if (!(action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST))
		return true;

	if (ignore_level) {
		if (ft->type != FS_FT_FDB &&
		    ft->type != FS_FT_NIC_RX &&
		    ft->type != FS_FT_NIC_TX)
			return false;

		if (dest->type == MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE &&
		    ft->type != dest->ft->type)
			return false;
	}

	if (!dest || ((dest->type ==
	    MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE) &&
	    (dest->ft->level <= ft->level && !ignore_level)))
		return false;
	return true;
}

struct match_list {
	struct list_head	list;
	struct mlx5_flow_group *g;
};

static void free_match_list(struct match_list *head, bool ft_locked)
{
	struct match_list *iter, *match_tmp;

	list_for_each_entry_safe(iter, match_tmp, &head->list,
				 list) {
		tree_put_node(&iter->g->node, ft_locked);
		list_del(&iter->list);
		kfree(iter);
	}
}

static int build_match_list(struct match_list *match_head,
			    struct mlx5_flow_table *ft,
			    const struct mlx5_flow_spec *spec,
			    struct mlx5_flow_group *fg,
			    bool ft_locked)
{
	struct rhlist_head *tmp, *list;
	struct mlx5_flow_group *g;

	rcu_read_lock();
	INIT_LIST_HEAD(&match_head->list);
	/* Collect all fgs which has a matching match_criteria */
	list = rhltable_lookup(&ft->fgs_hash, spec, rhash_fg);
	/* RCU is atomic, we can't execute FW commands here */
	rhl_for_each_entry_rcu(g, tmp, list, hash) {
		struct match_list *curr_match;

		if (fg && fg != g)
			continue;

		if (unlikely(!tree_get_node(&g->node)))
			continue;

		curr_match = kmalloc(sizeof(*curr_match), GFP_ATOMIC);
		if (!curr_match) {
			rcu_read_unlock();
			free_match_list(match_head, ft_locked);
			return -ENOMEM;
		}
		curr_match->g = g;
		list_add_tail(&curr_match->list, &match_head->list);
	}
	rcu_read_unlock();
	return 0;
}

static u64 matched_fgs_get_version(struct list_head *match_head)
{
	struct match_list *iter;
	u64 version = 0;

	list_for_each_entry(iter, match_head, list)
		version += (u64)atomic_read(&iter->g->node.version);
	return version;
}

static struct fs_fte *
lookup_fte_locked(struct mlx5_flow_group *g,
		  const u32 *match_value,
		  bool take_write)
{
	struct fs_fte *fte_tmp;

	if (take_write)
		nested_down_write_ref_node(&g->node, FS_LOCK_PARENT);
	else
		nested_down_read_ref_node(&g->node, FS_LOCK_PARENT);
	fte_tmp = rhashtable_lookup_fast(&g->ftes_hash, match_value,
					 rhash_fte);
	if (!fte_tmp || !tree_get_node(&fte_tmp->node)) {
		fte_tmp = NULL;
		goto out;
	}
	if (!fte_tmp->node.active) {
		tree_put_node(&fte_tmp->node, false);
		fte_tmp = NULL;
		goto out;
	}

	nested_down_write_ref_node(&fte_tmp->node, FS_LOCK_CHILD);
out:
	if (take_write)
		up_write_ref_node(&g->node, false);
	else
		up_read_ref_node(&g->node);
	return fte_tmp;
}

static struct mlx5_flow_handle *
try_add_to_existing_fg(struct mlx5_flow_table *ft,
		       struct list_head *match_head,
		       const struct mlx5_flow_spec *spec,
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
	u64  version = 0;
	int err;

	fte = alloc_fte(ft, spec, flow_act);
	if (IS_ERR(fte))
		return  ERR_PTR(-ENOMEM);

search_again_locked:
	if (flow_act->flags & FLOW_ACT_NO_APPEND)
		goto skip_search;
	version = matched_fgs_get_version(match_head);
	/* Try to find an fte with identical match value and attempt update its
	 * action.
	 */
	list_for_each_entry(iter, match_head, list) {
		struct fs_fte *fte_tmp;

		g = iter->g;
		fte_tmp = lookup_fte_locked(g, spec->match_value, take_write);
		if (!fte_tmp)
			continue;
		rule = add_rule_fg(g, spec, flow_act, dest, dest_num, fte_tmp);
		/* No error check needed here, because insert_fte() is not called */
		up_write_ref_node(&fte_tmp->node, false);
		tree_put_node(&fte_tmp->node, false);
		kmem_cache_free(steering->ftes_cache, fte);
		return rule;
	}

skip_search:
	/* No group with matching fte found, or we skipped the search.
	 * Try to add a new fte to any matching fg.
	 */

	/* Check the ft version, for case that new flow group
	 * was added while the fgs weren't locked
	 */
	if (atomic_read(&ft->node.version) != ft_version) {
		rule = ERR_PTR(-EAGAIN);
		goto out;
	}

	/* Check the fgs version. If version have changed it could be that an
	 * FTE with the same match value was added while the fgs weren't
	 * locked.
	 */
	if (!(flow_act->flags & FLOW_ACT_NO_APPEND) &&
	    version != matched_fgs_get_version(match_head)) {
		take_write = true;
		goto search_again_locked;
	}

	list_for_each_entry(iter, match_head, list) {
		g = iter->g;

		nested_down_write_ref_node(&g->node, FS_LOCK_PARENT);

		if (!g->node.active) {
			up_write_ref_node(&g->node, false);
			continue;
		}

		err = insert_fte(g, fte);
		if (err) {
			up_write_ref_node(&g->node, false);
			if (err == -ENOSPC)
				continue;
			kmem_cache_free(steering->ftes_cache, fte);
			return ERR_PTR(err);
		}

		nested_down_write_ref_node(&fte->node, FS_LOCK_CHILD);
		up_write_ref_node(&g->node, false);
		rule = add_rule_fg(g, spec, flow_act, dest, dest_num, fte);
		up_write_ref_node(&fte->node, false);
		if (IS_ERR(rule))
			tree_put_node(&fte->node, false);
		return rule;
	}
	rule = ERR_PTR(-ENOENT);
out:
	kmem_cache_free(steering->ftes_cache, fte);
	return rule;
}

static struct mlx5_flow_handle *
_mlx5_add_flow_rules(struct mlx5_flow_table *ft,
		     const struct mlx5_flow_spec *spec,
		     struct mlx5_flow_act *flow_act,
		     struct mlx5_flow_destination *dest,
		     int dest_num)

{
	struct mlx5_flow_steering *steering = get_steering(&ft->node);
	struct mlx5_flow_handle *rule;
	struct match_list match_head;
	struct mlx5_flow_group *g;
	bool take_write = false;
	struct fs_fte *fte;
	int version;
	int err;
	int i;

	if (!check_valid_spec(spec))
		return ERR_PTR(-EINVAL);

	if (flow_act->fg && ft->autogroup.active)
		return ERR_PTR(-EINVAL);

	if (dest && dest_num <= 0)
		return ERR_PTR(-EINVAL);

	for (i = 0; i < dest_num; i++) {
		if (!dest_is_valid(&dest[i], flow_act, ft))
			return ERR_PTR(-EINVAL);
	}
	nested_down_read_ref_node(&ft->node, FS_LOCK_GRANDPARENT);
search_again_locked:
	version = atomic_read(&ft->node.version);

	/* Collect all fgs which has a matching match_criteria */
	err = build_match_list(&match_head, ft, spec, flow_act->fg, take_write);
	if (err) {
		if (take_write)
			up_write_ref_node(&ft->node, false);
		else
			up_read_ref_node(&ft->node);
		return ERR_PTR(err);
	}

	if (!take_write)
		up_read_ref_node(&ft->node);

	rule = try_add_to_existing_fg(ft, &match_head.list, spec, flow_act, dest,
				      dest_num, version);
	free_match_list(&match_head, take_write);
	if (!IS_ERR(rule) ||
	    (PTR_ERR(rule) != -ENOENT && PTR_ERR(rule) != -EAGAIN)) {
		if (take_write)
			up_write_ref_node(&ft->node, false);
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
		rule = ERR_CAST(g);
		up_write_ref_node(&ft->node, false);
		return rule;
	}

	fte = alloc_fte(ft, spec, flow_act);
	if (IS_ERR(fte)) {
		up_write_ref_node(&ft->node, false);
		err = PTR_ERR(fte);
		goto err_alloc_fte;
	}

	nested_down_write_ref_node(&g->node, FS_LOCK_PARENT);
	up_write_ref_node(&ft->node, false);

	err = create_auto_flow_group(ft, g);
	if (err)
		goto err_release_fg;

	err = insert_fte(g, fte);
	if (err)
		goto err_release_fg;

	nested_down_write_ref_node(&fte->node, FS_LOCK_CHILD);
	up_write_ref_node(&g->node, false);
	rule = add_rule_fg(g, spec, flow_act, dest, dest_num, fte);
	up_write_ref_node(&fte->node, false);
	if (IS_ERR(rule))
		tree_put_node(&fte->node, false);
	tree_put_node(&g->node, false);
	return rule;

err_release_fg:
	up_write_ref_node(&g->node, false);
	kmem_cache_free(steering->ftes_cache, fte);
err_alloc_fte:
	tree_put_node(&g->node, false);
	return ERR_PTR(err);
}

static bool fwd_next_prio_supported(struct mlx5_flow_table *ft)
{
	return ((ft->type == FS_FT_NIC_RX) &&
		(MLX5_CAP_FLOWTABLE(get_dev(&ft->node), nic_rx_multi_path_tirs)));
}

struct mlx5_flow_handle *
mlx5_add_flow_rules(struct mlx5_flow_table *ft,
		    const struct mlx5_flow_spec *spec,
		    struct mlx5_flow_act *flow_act,
		    struct mlx5_flow_destination *dest,
		    int num_dest)
{
	struct mlx5_flow_root_namespace *root = find_root(&ft->node);
	static const struct mlx5_flow_spec zero_spec = {};
	struct mlx5_flow_destination *gen_dest = NULL;
	struct mlx5_flow_table *next_ft = NULL;
	struct mlx5_flow_handle *handle = NULL;
	u32 sw_action = flow_act->action;
	int i;

	if (!spec)
		spec = &zero_spec;

	if (!is_fwd_next_action(sw_action))
		return _mlx5_add_flow_rules(ft, spec, flow_act, dest, num_dest);

	if (!fwd_next_prio_supported(ft))
		return ERR_PTR(-EOPNOTSUPP);

	mutex_lock(&root->chain_lock);
	next_ft = find_next_fwd_ft(ft, flow_act);
	if (!next_ft) {
		handle = ERR_PTR(-EOPNOTSUPP);
		goto unlock;
	}

	gen_dest = kcalloc(num_dest + 1, sizeof(*dest),
			   GFP_KERNEL);
	if (!gen_dest) {
		handle = ERR_PTR(-ENOMEM);
		goto unlock;
	}
	for (i = 0; i < num_dest; i++)
		gen_dest[i] = dest[i];
	gen_dest[i].type =
		MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	gen_dest[i].ft = next_ft;
	dest = gen_dest;
	num_dest++;
	flow_act->action &= ~(MLX5_FLOW_CONTEXT_ACTION_FWD_NEXT_PRIO |
			      MLX5_FLOW_CONTEXT_ACTION_FWD_NEXT_NS);
	flow_act->action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	handle = _mlx5_add_flow_rules(ft, spec, flow_act, dest, num_dest);
	if (IS_ERR(handle))
		goto unlock;

	if (list_empty(&handle->rule[num_dest - 1]->next_ft)) {
		mutex_lock(&next_ft->lock);
		list_add(&handle->rule[num_dest - 1]->next_ft,
			 &next_ft->fwd_rules);
		mutex_unlock(&next_ft->lock);
		handle->rule[num_dest - 1]->sw_action = sw_action;
		handle->rule[num_dest - 1]->ft = ft;
	}
unlock:
	mutex_unlock(&root->chain_lock);
	kfree(gen_dest);
	return handle;
}
EXPORT_SYMBOL(mlx5_add_flow_rules);

void mlx5_del_flow_rules(struct mlx5_flow_handle *handle)
{
	struct fs_fte *fte;
	int i;

	/* In order to consolidate the HW changes we lock the FTE for other
	 * changes, and increase its refcount, in order not to perform the
	 * "del" functions of the FTE. Will handle them here.
	 * The removal of the rules is done under locked FTE.
	 * After removing all the handle's rules, if there are remaining
	 * rules, it means we just need to modify the FTE in FW, and
	 * unlock/decrease the refcount we increased before.
	 * Otherwise, it means the FTE should be deleted. First delete the
	 * FTE in FW. Then, unlock the FTE, and proceed the tree_put_node of
	 * the FTE, which will handle the last decrease of the refcount, as
	 * well as required handling of its parent.
	 */
	fs_get_obj(fte, handle->rule[0]->node.parent);
	down_write_ref_node(&fte->node, false);
	for (i = handle->num_rules - 1; i >= 0; i--)
		tree_remove_node(&handle->rule[i]->node, true);
	if (list_empty(&fte->node.children)) {
		fte->node.del_hw_func(&fte->node);
		/* Avoid double call to del_hw_fte */
		fte->node.del_hw_func = NULL;
		up_write_ref_node(&fte->node, false);
		tree_put_node(&fte->node, false);
	} else if (fte->dests_size) {
		if (fte->modify_mask)
			modify_fte(fte);
		up_write_ref_node(&fte->node, false);
	} else {
		up_write_ref_node(&fte->node, false);
	}
	kfree(handle);
}
EXPORT_SYMBOL(mlx5_del_flow_rules);

/* Assuming prio->node.children(flow tables) is sorted by level */
static struct mlx5_flow_table *find_next_ft(struct mlx5_flow_table *ft)
{
	struct fs_node *prio_parent, *child;
	struct fs_prio *prio;

	fs_get_obj(prio, ft->node.parent);

	if (!list_is_last(&ft->node.list, &prio->node.children))
		return list_next_entry(ft, node.list);

	prio_parent = find_prio_chains_parent(&prio->node, &child);

	if (prio_parent && list_is_first(&child->list, &prio_parent->children))
		return find_closest_ft(&prio->node, false, false);

	return find_next_chained_ft(&prio->node);
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
		err = root->cmds->update_root_ft(root, new_root_ft,
						 qpn, false);
	} else {
		list_for_each_entry(uqp, &root->underlay_qpns, list) {
			qpn = uqp->qpn;
			err = root->cmds->update_root_ft(root,
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

	next_ft = find_next_ft(ft);
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
	if (!(ft->flags & MLX5_FLOW_TABLE_UNMANAGED))
		err = disconnect_flow_table(ft);
	if (err) {
		mutex_unlock(&root->chain_lock);
		return err;
	}
	if (tree_remove_node(&ft->node, false))
		mlx5_core_warn(get_dev(&ft->node), "Flow table %d wasn't destroyed, refcount > 1\n",
			       ft->id);
	mutex_unlock(&root->chain_lock);

	return err;
}
EXPORT_SYMBOL(mlx5_destroy_flow_table);

void mlx5_destroy_flow_group(struct mlx5_flow_group *fg)
{
	if (tree_remove_node(&fg->node, false))
		mlx5_core_warn(get_dev(&fg->node), "Flow group %d wasn't destroyed, refcount > 1\n",
			       fg->id);
}
EXPORT_SYMBOL(mlx5_destroy_flow_group);

struct mlx5_flow_namespace *mlx5_get_fdb_sub_ns(struct mlx5_core_dev *dev,
						int n)
{
	struct mlx5_flow_steering *steering = dev->priv.steering;

	if (!steering || !steering->fdb_sub_ns)
		return NULL;

	return steering->fdb_sub_ns[n];
}
EXPORT_SYMBOL(mlx5_get_fdb_sub_ns);

static bool is_nic_rx_ns(enum mlx5_flow_namespace_type type)
{
	switch (type) {
	case MLX5_FLOW_NAMESPACE_BYPASS:
	case MLX5_FLOW_NAMESPACE_KERNEL_RX_MACSEC:
	case MLX5_FLOW_NAMESPACE_LAG:
	case MLX5_FLOW_NAMESPACE_OFFLOADS:
	case MLX5_FLOW_NAMESPACE_ETHTOOL:
	case MLX5_FLOW_NAMESPACE_KERNEL:
	case MLX5_FLOW_NAMESPACE_LEFTOVERS:
	case MLX5_FLOW_NAMESPACE_ANCHOR:
		return true;
	default:
		return false;
	}
}

struct mlx5_flow_namespace *mlx5_get_flow_namespace(struct mlx5_core_dev *dev,
						    enum mlx5_flow_namespace_type type)
{
	struct mlx5_flow_steering *steering = dev->priv.steering;
	struct mlx5_flow_root_namespace *root_ns;
	int prio = 0;
	struct fs_prio *fs_prio;
	struct mlx5_flow_namespace *ns;

	if (!steering)
		return NULL;

	switch (type) {
	case MLX5_FLOW_NAMESPACE_FDB:
		if (steering->fdb_root_ns)
			return &steering->fdb_root_ns->ns;
		return NULL;
	case MLX5_FLOW_NAMESPACE_PORT_SEL:
		if (steering->port_sel_root_ns)
			return &steering->port_sel_root_ns->ns;
		return NULL;
	case MLX5_FLOW_NAMESPACE_SNIFFER_RX:
		if (steering->sniffer_rx_root_ns)
			return &steering->sniffer_rx_root_ns->ns;
		return NULL;
	case MLX5_FLOW_NAMESPACE_SNIFFER_TX:
		if (steering->sniffer_tx_root_ns)
			return &steering->sniffer_tx_root_ns->ns;
		return NULL;
	case MLX5_FLOW_NAMESPACE_FDB_BYPASS:
		root_ns = steering->fdb_root_ns;
		prio =  FDB_BYPASS_PATH;
		break;
	case MLX5_FLOW_NAMESPACE_EGRESS:
	case MLX5_FLOW_NAMESPACE_EGRESS_IPSEC:
	case MLX5_FLOW_NAMESPACE_EGRESS_MACSEC:
		root_ns = steering->egress_root_ns;
		prio = type - MLX5_FLOW_NAMESPACE_EGRESS;
		break;
	case MLX5_FLOW_NAMESPACE_RDMA_RX:
		root_ns = steering->rdma_rx_root_ns;
		prio = RDMA_RX_BYPASS_PRIO;
		break;
	case MLX5_FLOW_NAMESPACE_RDMA_RX_KERNEL:
		root_ns = steering->rdma_rx_root_ns;
		prio = RDMA_RX_KERNEL_PRIO;
		break;
	case MLX5_FLOW_NAMESPACE_RDMA_TX:
		root_ns = steering->rdma_tx_root_ns;
		break;
	case MLX5_FLOW_NAMESPACE_RDMA_RX_COUNTERS:
		root_ns = steering->rdma_rx_root_ns;
		prio = RDMA_RX_COUNTERS_PRIO;
		break;
	case MLX5_FLOW_NAMESPACE_RDMA_TX_COUNTERS:
		root_ns = steering->rdma_tx_root_ns;
		prio = RDMA_TX_COUNTERS_PRIO;
		break;
	case MLX5_FLOW_NAMESPACE_RDMA_RX_IPSEC:
		root_ns = steering->rdma_rx_root_ns;
		prio = RDMA_RX_IPSEC_PRIO;
		break;
	case MLX5_FLOW_NAMESPACE_RDMA_TX_IPSEC:
		root_ns = steering->rdma_tx_root_ns;
		prio = RDMA_TX_IPSEC_PRIO;
		break;
	default: /* Must be NIC RX */
		WARN_ON(!is_nic_rx_ns(type));
		root_ns = steering->root_ns;
		prio = type;
		break;
	}

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

	if (!steering)
		return NULL;

	switch (type) {
	case MLX5_FLOW_NAMESPACE_ESW_EGRESS:
		if (vport >= steering->esw_egress_acl_vports)
			return NULL;
		if (steering->esw_egress_root_ns &&
		    steering->esw_egress_root_ns[vport])
			return &steering->esw_egress_root_ns[vport]->ns;
		else
			return NULL;
	case MLX5_FLOW_NAMESPACE_ESW_INGRESS:
		if (vport >= steering->esw_ingress_acl_vports)
			return NULL;
		if (steering->esw_ingress_root_ns &&
		    steering->esw_ingress_root_ns[vport])
			return &steering->esw_ingress_root_ns[vport]->ns;
		else
			return NULL;
	default:
		return NULL;
	}
}

static struct fs_prio *_fs_create_prio(struct mlx5_flow_namespace *ns,
				       unsigned int prio,
				       int num_levels,
				       enum fs_node_type type)
{
	struct fs_prio *fs_prio;

	fs_prio = kzalloc(sizeof(*fs_prio), GFP_KERNEL);
	if (!fs_prio)
		return ERR_PTR(-ENOMEM);

	fs_prio->node.type = type;
	tree_init_node(&fs_prio->node, NULL, del_sw_prio);
	tree_add_node(&fs_prio->node, &ns->node);
	fs_prio->num_levels = num_levels;
	fs_prio->prio = prio;
	list_add_tail(&fs_prio->node.list, &ns->node.children);

	return fs_prio;
}

static struct fs_prio *fs_create_prio_chained(struct mlx5_flow_namespace *ns,
					      unsigned int prio,
					      int num_levels)
{
	return _fs_create_prio(ns, prio, num_levels, FS_TYPE_PRIO_CHAINS);
}

static struct fs_prio *fs_create_prio(struct mlx5_flow_namespace *ns,
				      unsigned int prio, int num_levels)
{
	return _fs_create_prio(ns, prio, num_levels, FS_TYPE_PRIO);
}

static struct mlx5_flow_namespace *fs_init_namespace(struct mlx5_flow_namespace
						     *ns)
{
	ns->node.type = FS_TYPE_NAMESPACE;

	return ns;
}

static struct mlx5_flow_namespace *fs_create_namespace(struct fs_prio *prio,
						       int def_miss_act)
{
	struct mlx5_flow_namespace	*ns;

	ns = kzalloc(sizeof(*ns), GFP_KERNEL);
	if (!ns)
		return ERR_PTR(-ENOMEM);

	fs_init_namespace(ns);
	ns->def_miss_action = def_miss_act;
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
	((be32_to_cpu(*((__be32 *)(dev->caps.hca[MLX5_CAP_FLOW_TABLE]->cur) +	\
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
		fs_ns = fs_create_namespace(fs_prio, init_node->def_miss_action);
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
	int err;
	int i;

	for (i = 0; i < init_node->ar_size; i++) {
		err = init_root_tree_recursive(steering, &init_node->children[i],
					       fs_parent_node,
					       init_node, i);
		if (err)
			return err;
	}
	return 0;
}

static void del_sw_root_ns(struct fs_node *node)
{
	struct mlx5_flow_root_namespace *root_ns;
	struct mlx5_flow_namespace *ns;

	fs_get_obj(ns, node);
	root_ns = container_of(ns, struct mlx5_flow_root_namespace, ns);
	mutex_destroy(&root_ns->chain_lock);
	kfree(node);
}

static struct mlx5_flow_root_namespace
*create_root_ns(struct mlx5_flow_steering *steering,
		enum fs_flow_table_type table_type)
{
	const struct mlx5_flow_cmds *cmds = mlx5_fs_cmd_get_default(table_type);
	struct mlx5_flow_root_namespace *root_ns;
	struct mlx5_flow_namespace *ns;

	/* Create the root namespace */
	root_ns = kzalloc(sizeof(*root_ns), GFP_KERNEL);
	if (!root_ns)
		return NULL;

	root_ns->dev = steering->dev;
	root_ns->table_type = table_type;
	root_ns->cmds = cmds;

	INIT_LIST_HEAD(&root_ns->underlay_qpns);

	ns = &root_ns->ns;
	fs_init_namespace(ns);
	mutex_init(&root_ns->chain_lock);
	tree_init_node(&ns->node, NULL, del_sw_root_ns);
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
	fs_for_each_ns(ns, prio) {
		/* This updates start_level and num_levels of ns's priority descendants */
		acc_level_ns = set_prio_attrs_in_ns(ns, acc_level);

		/* If this a prio with chains, and we can jump from one chain
		 * (namespace) to another, so we accumulate the levels
		 */
		if (prio->node.type == FS_TYPE_PRIO_CHAINS)
			acc_level = acc_level_ns;
	}

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
		tree_put_node(node, false);
		tree_remove_node(node, false);
	}
}

static void cleanup_root_ns(struct mlx5_flow_root_namespace *root_ns)
{
	if (!root_ns)
		return;

	clean_tree(&root_ns->ns.node);
}

static int init_sniffer_tx_root_ns(struct mlx5_flow_steering *steering)
{
	struct fs_prio *prio;

	steering->sniffer_tx_root_ns = create_root_ns(steering, FS_FT_SNIFFER_TX);
	if (!steering->sniffer_tx_root_ns)
		return -ENOMEM;

	/* Create single prio */
	prio = fs_create_prio(&steering->sniffer_tx_root_ns->ns, 0, 1);
	return PTR_ERR_OR_ZERO(prio);
}

static int init_sniffer_rx_root_ns(struct mlx5_flow_steering *steering)
{
	struct fs_prio *prio;

	steering->sniffer_rx_root_ns = create_root_ns(steering, FS_FT_SNIFFER_RX);
	if (!steering->sniffer_rx_root_ns)
		return -ENOMEM;

	/* Create single prio */
	prio = fs_create_prio(&steering->sniffer_rx_root_ns->ns, 0, 1);
	return PTR_ERR_OR_ZERO(prio);
}

#define PORT_SEL_NUM_LEVELS 3
static int init_port_sel_root_ns(struct mlx5_flow_steering *steering)
{
	struct fs_prio *prio;

	steering->port_sel_root_ns = create_root_ns(steering, FS_FT_PORT_SEL);
	if (!steering->port_sel_root_ns)
		return -ENOMEM;

	/* Create single prio */
	prio = fs_create_prio(&steering->port_sel_root_ns->ns, 0,
			      PORT_SEL_NUM_LEVELS);
	return PTR_ERR_OR_ZERO(prio);
}

static int init_rdma_rx_root_ns(struct mlx5_flow_steering *steering)
{
	int err;

	steering->rdma_rx_root_ns = create_root_ns(steering, FS_FT_RDMA_RX);
	if (!steering->rdma_rx_root_ns)
		return -ENOMEM;

	err = init_root_tree(steering, &rdma_rx_root_fs,
			     &steering->rdma_rx_root_ns->ns.node);
	if (err)
		goto out_err;

	set_prio_attrs(steering->rdma_rx_root_ns);

	return 0;

out_err:
	cleanup_root_ns(steering->rdma_rx_root_ns);
	steering->rdma_rx_root_ns = NULL;
	return err;
}

static int init_rdma_tx_root_ns(struct mlx5_flow_steering *steering)
{
	int err;

	steering->rdma_tx_root_ns = create_root_ns(steering, FS_FT_RDMA_TX);
	if (!steering->rdma_tx_root_ns)
		return -ENOMEM;

	err = init_root_tree(steering, &rdma_tx_root_fs,
			     &steering->rdma_tx_root_ns->ns.node);
	if (err)
		goto out_err;

	set_prio_attrs(steering->rdma_tx_root_ns);

	return 0;

out_err:
	cleanup_root_ns(steering->rdma_tx_root_ns);
	steering->rdma_tx_root_ns = NULL;
	return err;
}

/* FT and tc chains are stored in the same array so we can re-use the
 * mlx5_get_fdb_sub_ns() and tc api for FT chains.
 * When creating a new ns for each chain store it in the first available slot.
 * Assume tc chains are created and stored first and only then the FT chain.
 */
static void store_fdb_sub_ns_prio_chain(struct mlx5_flow_steering *steering,
					struct mlx5_flow_namespace *ns)
{
	int chain = 0;

	while (steering->fdb_sub_ns[chain])
		++chain;

	steering->fdb_sub_ns[chain] = ns;
}

static int create_fdb_sub_ns_prio_chain(struct mlx5_flow_steering *steering,
					struct fs_prio *maj_prio)
{
	struct mlx5_flow_namespace *ns;
	struct fs_prio *min_prio;
	int prio;

	ns = fs_create_namespace(maj_prio, MLX5_FLOW_TABLE_MISS_ACTION_DEF);
	if (IS_ERR(ns))
		return PTR_ERR(ns);

	for (prio = 0; prio < FDB_TC_MAX_PRIO; prio++) {
		min_prio = fs_create_prio(ns, prio, FDB_TC_LEVELS_PER_PRIO);
		if (IS_ERR(min_prio))
			return PTR_ERR(min_prio);
	}

	store_fdb_sub_ns_prio_chain(steering, ns);

	return 0;
}

static int create_fdb_chains(struct mlx5_flow_steering *steering,
			     int fs_prio,
			     int chains)
{
	struct fs_prio *maj_prio;
	int levels;
	int chain;
	int err;

	levels = FDB_TC_LEVELS_PER_PRIO * FDB_TC_MAX_PRIO * chains;
	maj_prio = fs_create_prio_chained(&steering->fdb_root_ns->ns,
					  fs_prio,
					  levels);
	if (IS_ERR(maj_prio))
		return PTR_ERR(maj_prio);

	for (chain = 0; chain < chains; chain++) {
		err = create_fdb_sub_ns_prio_chain(steering, maj_prio);
		if (err)
			return err;
	}

	return 0;
}

static int create_fdb_fast_path(struct mlx5_flow_steering *steering)
{
	int err;

	steering->fdb_sub_ns = kcalloc(FDB_NUM_CHAINS,
				       sizeof(*steering->fdb_sub_ns),
				       GFP_KERNEL);
	if (!steering->fdb_sub_ns)
		return -ENOMEM;

	err = create_fdb_chains(steering, FDB_TC_OFFLOAD, FDB_TC_MAX_CHAIN + 1);
	if (err)
		return err;

	err = create_fdb_chains(steering, FDB_FT_OFFLOAD, 1);
	if (err)
		return err;

	return 0;
}

static int create_fdb_bypass(struct mlx5_flow_steering *steering)
{
	struct mlx5_flow_namespace *ns;
	struct fs_prio *prio;
	int i;

	prio = fs_create_prio(&steering->fdb_root_ns->ns, FDB_BYPASS_PATH, 0);
	if (IS_ERR(prio))
		return PTR_ERR(prio);

	ns = fs_create_namespace(prio, MLX5_FLOW_TABLE_MISS_ACTION_DEF);
	if (IS_ERR(ns))
		return PTR_ERR(ns);

	for (i = 0; i < MLX5_BY_PASS_NUM_REGULAR_PRIOS; i++) {
		prio = fs_create_prio(ns, i, 1);
		if (IS_ERR(prio))
			return PTR_ERR(prio);
	}
	return 0;
}

static void cleanup_fdb_root_ns(struct mlx5_flow_steering *steering)
{
	cleanup_root_ns(steering->fdb_root_ns);
	steering->fdb_root_ns = NULL;
	kfree(steering->fdb_sub_ns);
	steering->fdb_sub_ns = NULL;
}

static int init_fdb_root_ns(struct mlx5_flow_steering *steering)
{
	struct fs_prio *maj_prio;
	int err;

	steering->fdb_root_ns = create_root_ns(steering, FS_FT_FDB);
	if (!steering->fdb_root_ns)
		return -ENOMEM;

	err = create_fdb_bypass(steering);
	if (err)
		goto out_err;

	maj_prio = fs_create_prio(&steering->fdb_root_ns->ns, FDB_CRYPTO_INGRESS, 3);
	if (IS_ERR(maj_prio)) {
		err = PTR_ERR(maj_prio);
		goto out_err;
	}

	err = create_fdb_fast_path(steering);
	if (err)
		goto out_err;

	maj_prio = fs_create_prio(&steering->fdb_root_ns->ns, FDB_TC_MISS, 1);
	if (IS_ERR(maj_prio)) {
		err = PTR_ERR(maj_prio);
		goto out_err;
	}

	maj_prio = fs_create_prio(&steering->fdb_root_ns->ns, FDB_BR_OFFLOAD, 4);
	if (IS_ERR(maj_prio)) {
		err = PTR_ERR(maj_prio);
		goto out_err;
	}

	maj_prio = fs_create_prio(&steering->fdb_root_ns->ns, FDB_SLOW_PATH, 1);
	if (IS_ERR(maj_prio)) {
		err = PTR_ERR(maj_prio);
		goto out_err;
	}

	maj_prio = fs_create_prio(&steering->fdb_root_ns->ns, FDB_CRYPTO_EGRESS, 3);
	if (IS_ERR(maj_prio)) {
		err = PTR_ERR(maj_prio);
		goto out_err;
	}

	/* We put this priority last, knowing that nothing will get here
	 * unless explicitly forwarded to. This is possible because the
	 * slow path tables have catch all rules and nothing gets passed
	 * those tables.
	 */
	maj_prio = fs_create_prio(&steering->fdb_root_ns->ns, FDB_PER_VPORT, 1);
	if (IS_ERR(maj_prio)) {
		err = PTR_ERR(maj_prio);
		goto out_err;
	}

	set_prio_attrs(steering->fdb_root_ns);
	return 0;

out_err:
	cleanup_fdb_root_ns(steering);
	return err;
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

int mlx5_fs_egress_acls_init(struct mlx5_core_dev *dev, int total_vports)
{
	struct mlx5_flow_steering *steering = dev->priv.steering;
	int err;
	int i;

	steering->esw_egress_root_ns =
			kcalloc(total_vports,
				sizeof(*steering->esw_egress_root_ns),
				GFP_KERNEL);
	if (!steering->esw_egress_root_ns)
		return -ENOMEM;

	for (i = 0; i < total_vports; i++) {
		err = init_egress_acl_root_ns(steering, i);
		if (err)
			goto cleanup_root_ns;
	}
	steering->esw_egress_acl_vports = total_vports;
	return 0;

cleanup_root_ns:
	for (i--; i >= 0; i--)
		cleanup_root_ns(steering->esw_egress_root_ns[i]);
	kfree(steering->esw_egress_root_ns);
	steering->esw_egress_root_ns = NULL;
	return err;
}

void mlx5_fs_egress_acls_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_flow_steering *steering = dev->priv.steering;
	int i;

	if (!steering->esw_egress_root_ns)
		return;

	for (i = 0; i < steering->esw_egress_acl_vports; i++)
		cleanup_root_ns(steering->esw_egress_root_ns[i]);

	kfree(steering->esw_egress_root_ns);
	steering->esw_egress_root_ns = NULL;
}

int mlx5_fs_ingress_acls_init(struct mlx5_core_dev *dev, int total_vports)
{
	struct mlx5_flow_steering *steering = dev->priv.steering;
	int err;
	int i;

	steering->esw_ingress_root_ns =
			kcalloc(total_vports,
				sizeof(*steering->esw_ingress_root_ns),
				GFP_KERNEL);
	if (!steering->esw_ingress_root_ns)
		return -ENOMEM;

	for (i = 0; i < total_vports; i++) {
		err = init_ingress_acl_root_ns(steering, i);
		if (err)
			goto cleanup_root_ns;
	}
	steering->esw_ingress_acl_vports = total_vports;
	return 0;

cleanup_root_ns:
	for (i--; i >= 0; i--)
		cleanup_root_ns(steering->esw_ingress_root_ns[i]);
	kfree(steering->esw_ingress_root_ns);
	steering->esw_ingress_root_ns = NULL;
	return err;
}

void mlx5_fs_ingress_acls_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_flow_steering *steering = dev->priv.steering;
	int i;

	if (!steering->esw_ingress_root_ns)
		return;

	for (i = 0; i < steering->esw_ingress_acl_vports; i++)
		cleanup_root_ns(steering->esw_ingress_root_ns[i]);

	kfree(steering->esw_ingress_root_ns);
	steering->esw_ingress_root_ns = NULL;
}

u32 mlx5_fs_get_capabilities(struct mlx5_core_dev *dev, enum mlx5_flow_namespace_type type)
{
	struct mlx5_flow_root_namespace *root;
	struct mlx5_flow_namespace *ns;

	ns = mlx5_get_flow_namespace(dev, type);
	if (!ns)
		return 0;

	root = find_root(&ns->node);
	if (!root)
		return 0;

	return root->cmds->get_capabilities(root, root->table_type);
}

static int init_egress_root_ns(struct mlx5_flow_steering *steering)
{
	int err;

	steering->egress_root_ns = create_root_ns(steering,
						  FS_FT_NIC_TX);
	if (!steering->egress_root_ns)
		return -ENOMEM;

	err = init_root_tree(steering, &egress_root_fs,
			     &steering->egress_root_ns->ns.node);
	if (err)
		goto cleanup;
	set_prio_attrs(steering->egress_root_ns);
	return 0;
cleanup:
	cleanup_root_ns(steering->egress_root_ns);
	steering->egress_root_ns = NULL;
	return err;
}

static int mlx5_fs_mode_validate(struct devlink *devlink, u32 id,
				 union devlink_param_value val,
				 struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	char *value = val.vstr;
	int err = 0;

	if (!strcmp(value, "dmfs")) {
		return 0;
	} else if (!strcmp(value, "smfs")) {
		u8 eswitch_mode;
		bool smfs_cap;

		eswitch_mode = mlx5_eswitch_mode(dev);
		smfs_cap = mlx5_fs_dr_is_supported(dev);

		if (!smfs_cap) {
			err = -EOPNOTSUPP;
			NL_SET_ERR_MSG_MOD(extack,
					   "Software managed steering is not supported by current device");
		}

		else if (eswitch_mode == MLX5_ESWITCH_OFFLOADS) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Software managed steering is not supported when eswitch offloads enabled.");
			err = -EOPNOTSUPP;
		}
	} else {
		NL_SET_ERR_MSG_MOD(extack,
				   "Bad parameter: supported values are [\"dmfs\", \"smfs\"]");
		err = -EINVAL;
	}

	return err;
}

static int mlx5_fs_mode_set(struct devlink *devlink, u32 id,
			    struct devlink_param_gset_ctx *ctx)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	enum mlx5_flow_steering_mode mode;

	if (!strcmp(ctx->val.vstr, "smfs"))
		mode = MLX5_FLOW_STEERING_MODE_SMFS;
	else
		mode = MLX5_FLOW_STEERING_MODE_DMFS;
	dev->priv.steering->mode = mode;

	return 0;
}

static int mlx5_fs_mode_get(struct devlink *devlink, u32 id,
			    struct devlink_param_gset_ctx *ctx)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);

	if (dev->priv.steering->mode == MLX5_FLOW_STEERING_MODE_SMFS)
		strcpy(ctx->val.vstr, "smfs");
	else
		strcpy(ctx->val.vstr, "dmfs");
	return 0;
}

static const struct devlink_param mlx5_fs_params[] = {
	DEVLINK_PARAM_DRIVER(MLX5_DEVLINK_PARAM_ID_FLOW_STEERING_MODE,
			     "flow_steering_mode", DEVLINK_PARAM_TYPE_STRING,
			     BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			     mlx5_fs_mode_get, mlx5_fs_mode_set,
			     mlx5_fs_mode_validate),
};

void mlx5_fs_core_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_flow_steering *steering = dev->priv.steering;

	cleanup_root_ns(steering->root_ns);
	cleanup_fdb_root_ns(steering);
	cleanup_root_ns(steering->port_sel_root_ns);
	cleanup_root_ns(steering->sniffer_rx_root_ns);
	cleanup_root_ns(steering->sniffer_tx_root_ns);
	cleanup_root_ns(steering->rdma_rx_root_ns);
	cleanup_root_ns(steering->rdma_tx_root_ns);
	cleanup_root_ns(steering->egress_root_ns);

	devl_params_unregister(priv_to_devlink(dev), mlx5_fs_params,
			       ARRAY_SIZE(mlx5_fs_params));
}

int mlx5_fs_core_init(struct mlx5_core_dev *dev)
{
	struct mlx5_flow_steering *steering = dev->priv.steering;
	int err;

	err = devl_params_register(priv_to_devlink(dev), mlx5_fs_params,
				   ARRAY_SIZE(mlx5_fs_params));
	if (err)
		return err;

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

	if (MLX5_CAP_FLOWTABLE_PORT_SELECTION(dev, ft_support)) {
		err = init_port_sel_root_ns(steering);
		if (err)
			goto err;
	}

	if (MLX5_CAP_FLOWTABLE_RDMA_RX(dev, ft_support) &&
	    MLX5_CAP_FLOWTABLE_RDMA_RX(dev, table_miss_action_domain)) {
		err = init_rdma_rx_root_ns(steering);
		if (err)
			goto err;
	}

	if (MLX5_CAP_FLOWTABLE_RDMA_TX(dev, ft_support)) {
		err = init_rdma_tx_root_ns(steering);
		if (err)
			goto err;
	}

	if (MLX5_CAP_FLOWTABLE_NIC_TX(dev, ft_support)) {
		err = init_egress_root_ns(steering);
		if (err)
			goto err;
	}

	return 0;

err:
	mlx5_fs_core_cleanup(dev);
	return err;
}

void mlx5_fs_core_free(struct mlx5_core_dev *dev)
{
	struct mlx5_flow_steering *steering = dev->priv.steering;

	kmem_cache_destroy(steering->ftes_cache);
	kmem_cache_destroy(steering->fgs_cache);
	kfree(steering);
	mlx5_ft_pool_destroy(dev);
	mlx5_cleanup_fc_stats(dev);
}

int mlx5_fs_core_alloc(struct mlx5_core_dev *dev)
{
	struct mlx5_flow_steering *steering;
	int err = 0;

	err = mlx5_init_fc_stats(dev);
	if (err)
		return err;

	err = mlx5_ft_pool_init(dev);
	if (err)
		goto err;

	steering = kzalloc(sizeof(*steering), GFP_KERNEL);
	if (!steering) {
		err = -ENOMEM;
		goto err;
	}

	steering->dev = dev;
	dev->priv.steering = steering;

	if (mlx5_fs_dr_is_supported(dev))
		steering->mode = MLX5_FLOW_STEERING_MODE_SMFS;
	else
		steering->mode = MLX5_FLOW_STEERING_MODE_DMFS;

	steering->fgs_cache = kmem_cache_create("mlx5_fs_fgs",
						sizeof(struct mlx5_flow_group), 0,
						0, NULL);
	steering->ftes_cache = kmem_cache_create("mlx5_fs_ftes", sizeof(struct fs_fte), 0,
						 0, NULL);
	if (!steering->ftes_cache || !steering->fgs_cache) {
		err = -ENOMEM;
		goto err;
	}

	return 0;

err:
	mlx5_fs_core_free(dev);
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

	err = root->cmds->update_root_ft(root, root->root_ft, underlay_qpn,
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

	err = root->cmds->update_root_ft(root, root->root_ft, underlay_qpn,
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

static struct mlx5_flow_root_namespace
*get_root_namespace(struct mlx5_core_dev *dev, enum mlx5_flow_namespace_type ns_type)
{
	struct mlx5_flow_namespace *ns;

	if (ns_type == MLX5_FLOW_NAMESPACE_ESW_EGRESS ||
	    ns_type == MLX5_FLOW_NAMESPACE_ESW_INGRESS)
		ns = mlx5_get_flow_vport_acl_namespace(dev, ns_type, 0);
	else
		ns = mlx5_get_flow_namespace(dev, ns_type);
	if (!ns)
		return NULL;

	return find_root(&ns->node);
}

struct mlx5_modify_hdr *mlx5_modify_header_alloc(struct mlx5_core_dev *dev,
						 u8 ns_type, u8 num_actions,
						 void *modify_actions)
{
	struct mlx5_flow_root_namespace *root;
	struct mlx5_modify_hdr *modify_hdr;
	int err;

	root = get_root_namespace(dev, ns_type);
	if (!root)
		return ERR_PTR(-EOPNOTSUPP);

	modify_hdr = kzalloc(sizeof(*modify_hdr), GFP_KERNEL);
	if (!modify_hdr)
		return ERR_PTR(-ENOMEM);

	modify_hdr->ns_type = ns_type;
	err = root->cmds->modify_header_alloc(root, ns_type, num_actions,
					      modify_actions, modify_hdr);
	if (err) {
		kfree(modify_hdr);
		return ERR_PTR(err);
	}

	return modify_hdr;
}
EXPORT_SYMBOL(mlx5_modify_header_alloc);

void mlx5_modify_header_dealloc(struct mlx5_core_dev *dev,
				struct mlx5_modify_hdr *modify_hdr)
{
	struct mlx5_flow_root_namespace *root;

	root = get_root_namespace(dev, modify_hdr->ns_type);
	if (WARN_ON(!root))
		return;
	root->cmds->modify_header_dealloc(root, modify_hdr);
	kfree(modify_hdr);
}
EXPORT_SYMBOL(mlx5_modify_header_dealloc);

struct mlx5_pkt_reformat *mlx5_packet_reformat_alloc(struct mlx5_core_dev *dev,
						     struct mlx5_pkt_reformat_params *params,
						     enum mlx5_flow_namespace_type ns_type)
{
	struct mlx5_pkt_reformat *pkt_reformat;
	struct mlx5_flow_root_namespace *root;
	int err;

	root = get_root_namespace(dev, ns_type);
	if (!root)
		return ERR_PTR(-EOPNOTSUPP);

	pkt_reformat = kzalloc(sizeof(*pkt_reformat), GFP_KERNEL);
	if (!pkt_reformat)
		return ERR_PTR(-ENOMEM);

	pkt_reformat->ns_type = ns_type;
	pkt_reformat->reformat_type = params->type;
	err = root->cmds->packet_reformat_alloc(root, params, ns_type,
						pkt_reformat);
	if (err) {
		kfree(pkt_reformat);
		return ERR_PTR(err);
	}

	return pkt_reformat;
}
EXPORT_SYMBOL(mlx5_packet_reformat_alloc);

void mlx5_packet_reformat_dealloc(struct mlx5_core_dev *dev,
				  struct mlx5_pkt_reformat *pkt_reformat)
{
	struct mlx5_flow_root_namespace *root;

	root = get_root_namespace(dev, pkt_reformat->ns_type);
	if (WARN_ON(!root))
		return;
	root->cmds->packet_reformat_dealloc(root, pkt_reformat);
	kfree(pkt_reformat);
}
EXPORT_SYMBOL(mlx5_packet_reformat_dealloc);

int mlx5_get_match_definer_id(struct mlx5_flow_definer *definer)
{
	return definer->id;
}

struct mlx5_flow_definer *
mlx5_create_match_definer(struct mlx5_core_dev *dev,
			  enum mlx5_flow_namespace_type ns_type, u16 format_id,
			  u32 *match_mask)
{
	struct mlx5_flow_root_namespace *root;
	struct mlx5_flow_definer *definer;
	int id;

	root = get_root_namespace(dev, ns_type);
	if (!root)
		return ERR_PTR(-EOPNOTSUPP);

	definer = kzalloc(sizeof(*definer), GFP_KERNEL);
	if (!definer)
		return ERR_PTR(-ENOMEM);

	definer->ns_type = ns_type;
	id = root->cmds->create_match_definer(root, format_id, match_mask);
	if (id < 0) {
		mlx5_core_warn(root->dev, "Failed to create match definer (%d)\n", id);
		kfree(definer);
		return ERR_PTR(id);
	}
	definer->id = id;
	return definer;
}

void mlx5_destroy_match_definer(struct mlx5_core_dev *dev,
				struct mlx5_flow_definer *definer)
{
	struct mlx5_flow_root_namespace *root;

	root = get_root_namespace(dev, definer->ns_type);
	if (WARN_ON(!root))
		return;

	root->cmds->destroy_match_definer(root, definer->id);
	kfree(definer);
}

int mlx5_flow_namespace_set_peer(struct mlx5_flow_root_namespace *ns,
				 struct mlx5_flow_root_namespace *peer_ns,
				 u16 peer_vhca_id)
{
	if (peer_ns && ns->mode != peer_ns->mode) {
		mlx5_core_err(ns->dev,
			      "Can't peer namespace of different steering mode\n");
		return -EINVAL;
	}

	return ns->cmds->set_peer(ns, peer_ns, peer_vhca_id);
}

/* This function should be called only at init stage of the namespace.
 * It is not safe to call this function while steering operations
 * are executed in the namespace.
 */
int mlx5_flow_namespace_set_mode(struct mlx5_flow_namespace *ns,
				 enum mlx5_flow_steering_mode mode)
{
	struct mlx5_flow_root_namespace *root;
	const struct mlx5_flow_cmds *cmds;
	int err;

	root = find_root(&ns->node);
	if (&root->ns != ns)
	/* Can't set cmds to non root namespace */
		return -EINVAL;

	if (root->table_type != FS_FT_FDB)
		return -EOPNOTSUPP;

	if (root->mode == mode)
		return 0;

	if (mode == MLX5_FLOW_STEERING_MODE_SMFS)
		cmds = mlx5_fs_cmd_get_dr_cmds();
	else
		cmds = mlx5_fs_cmd_get_fw_cmds();
	if (!cmds)
		return -EOPNOTSUPP;

	err = cmds->create_ns(root);
	if (err) {
		mlx5_core_err(root->dev, "Failed to create flow namespace (%d)\n",
			      err);
		return err;
	}

	root->cmds->destroy_ns(root);
	root->cmds = cmds;
	root->mode = mode;

	return 0;
}
