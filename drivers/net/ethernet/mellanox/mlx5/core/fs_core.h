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

#ifndef _MLX5_FS_CORE_
#define _MLX5_FS_CORE_

#include <linux/mlx5/fs.h>
#include <linux/rhashtable.h>

enum fs_node_type {
	FS_TYPE_NAMESPACE,
	FS_TYPE_PRIO,
	FS_TYPE_FLOW_TABLE,
	FS_TYPE_FLOW_GROUP,
	FS_TYPE_FLOW_ENTRY,
	FS_TYPE_FLOW_DEST
};

enum fs_flow_table_type {
	FS_FT_NIC_RX          = 0x0,
	FS_FT_ESW_EGRESS_ACL  = 0x2,
	FS_FT_ESW_INGRESS_ACL = 0x3,
	FS_FT_FDB             = 0X4,
	FS_FT_SNIFFER_RX	= 0X5,
	FS_FT_SNIFFER_TX	= 0X6,
};

enum fs_flow_table_op_mod {
	FS_FT_OP_MOD_NORMAL,
	FS_FT_OP_MOD_LAG_DEMUX,
};

enum fs_fte_status {
	FS_FTE_STATUS_EXISTING = 1UL << 0,
};

struct mlx5_flow_steering {
	struct mlx5_core_dev *dev;
	struct mlx5_flow_root_namespace *root_ns;
	struct mlx5_flow_root_namespace *fdb_root_ns;
	struct mlx5_flow_root_namespace *esw_egress_root_ns;
	struct mlx5_flow_root_namespace *esw_ingress_root_ns;
	struct mlx5_flow_root_namespace	*sniffer_tx_root_ns;
	struct mlx5_flow_root_namespace	*sniffer_rx_root_ns;
};

struct fs_node {
	struct list_head	list;
	struct list_head	children;
	enum fs_node_type	type;
	struct fs_node		*parent;
	struct fs_node		*root;
	/* lock the node for writing and traversing */
	struct mutex		lock;
	atomic_t		refcount;
	void			(*remove_func)(struct fs_node *);
};

struct mlx5_flow_rule {
	struct fs_node				node;
	struct mlx5_flow_destination		dest_attr;
	/* next_ft should be accessed under chain_lock and only of
	 * destination type is FWD_NEXT_fT.
	 */
	struct list_head			next_ft;
	u32					sw_action;
};

struct mlx5_flow_handle {
	int num_rules;
	struct mlx5_flow_rule *rule[];
};

/* Type of children is mlx5_flow_group */
struct mlx5_flow_table {
	struct fs_node			node;
	u32				id;
	u16				vport;
	unsigned int			max_fte;
	unsigned int			level;
	enum fs_flow_table_type		type;
	enum fs_flow_table_op_mod	op_mod;
	struct {
		bool			active;
		unsigned int		required_groups;
		unsigned int		num_groups;
	} autogroup;
	/* Protect fwd_rules */
	struct mutex			lock;
	/* FWD rules that point on this flow table */
	struct list_head		fwd_rules;
	u32				flags;
	struct ida			fte_allocator;
	struct rhltable			fgs_hash;
};

struct mlx5_fc_cache {
	u64 packets;
	u64 bytes;
	u64 lastuse;
};

struct mlx5_fc {
	struct rb_node node;
	struct list_head list;

	/* last{packets,bytes} members are used when calculating the delta since
	 * last reading
	 */
	u64 lastpackets;
	u64 lastbytes;

	u32 id;
	bool deleted;
	bool aging;

	struct mlx5_fc_cache cache ____cacheline_aligned_in_smp;
};

#define MLX5_FTE_MATCH_PARAM_RESERVED	reserved_at_600
/* Calculate the fte_match_param length and without the reserved length.
 * Make sure the reserved field is the last.
 */
#define MLX5_ST_SZ_DW_MATCH_PARAM					    \
	((MLX5_BYTE_OFF(fte_match_param, MLX5_FTE_MATCH_PARAM_RESERVED) / sizeof(u32)) + \
	 BUILD_BUG_ON_ZERO(MLX5_ST_SZ_BYTES(fte_match_param) !=		     \
			   MLX5_FLD_SZ_BYTES(fte_match_param,		     \
					     MLX5_FTE_MATCH_PARAM_RESERVED) +\
			   MLX5_BYTE_OFF(fte_match_param,		     \
					 MLX5_FTE_MATCH_PARAM_RESERVED)))

/* Type of children is mlx5_flow_rule */
struct fs_fte {
	struct fs_node			node;
	u32				val[MLX5_ST_SZ_DW_MATCH_PARAM];
	u32				dests_size;
	u32				flow_tag;
	u32				index;
	u32				action;
	u32				encap_id;
	u32				modify_id;
	enum fs_fte_status		status;
	struct mlx5_fc			*counter;
	struct rhash_head		hash;
};

/* Type of children is mlx5_flow_table/namespace */
struct fs_prio {
	struct fs_node			node;
	unsigned int			num_levels;
	unsigned int			start_level;
	unsigned int			prio;
	unsigned int			num_ft;
};

/* Type of children is fs_prio */
struct mlx5_flow_namespace {
	/* parent == NULL => root ns */
	struct	fs_node			node;
};

struct mlx5_flow_group_mask {
	u8	match_criteria_enable;
	u32	match_criteria[MLX5_ST_SZ_DW_MATCH_PARAM];
};

/* Type of children is fs_fte */
struct mlx5_flow_group {
	struct fs_node			node;
	struct mlx5_flow_group_mask	mask;
	u32				start_index;
	u32				max_ftes;
	u32				id;
	struct rhashtable		ftes_hash;
	struct rhlist_head		hash;
};

struct mlx5_flow_root_namespace {
	struct mlx5_flow_namespace	ns;
	enum   fs_flow_table_type	table_type;
	struct mlx5_core_dev		*dev;
	struct mlx5_flow_table		*root_ft;
	/* Should be held when chaining flow tables */
	struct mutex			chain_lock;
	u32				underlay_qpn;
};

int mlx5_init_fc_stats(struct mlx5_core_dev *dev);
void mlx5_cleanup_fc_stats(struct mlx5_core_dev *dev);
void mlx5_fc_queue_stats_work(struct mlx5_core_dev *dev,
			      struct delayed_work *dwork,
			      unsigned long delay);
void mlx5_fc_update_sampling_interval(struct mlx5_core_dev *dev,
				      unsigned long interval);

int mlx5_init_fs(struct mlx5_core_dev *dev);
void mlx5_cleanup_fs(struct mlx5_core_dev *dev);

#define fs_get_obj(v, _node)  {v = container_of((_node), typeof(*v), node); }

#define fs_list_for_each_entry(pos, root)		\
	list_for_each_entry(pos, root, node.list)

#define fs_list_for_each_entry_safe(pos, tmp, root)		\
	list_for_each_entry_safe(pos, tmp, root, node.list)

#define fs_for_each_ns_or_ft_reverse(pos, prio)				\
	list_for_each_entry_reverse(pos, &(prio)->node.children, list)

#define fs_for_each_ns_or_ft(pos, prio)					\
	list_for_each_entry(pos, (&(prio)->node.children), list)

#define fs_for_each_prio(pos, ns)			\
	fs_list_for_each_entry(pos, &(ns)->node.children)

#define fs_for_each_ns(pos, prio)			\
	fs_list_for_each_entry(pos, &(prio)->node.children)

#define fs_for_each_ft(pos, prio)			\
	fs_list_for_each_entry(pos, &(prio)->node.children)

#define fs_for_each_ft_safe(pos, tmp, prio)			\
	fs_list_for_each_entry_safe(pos, tmp, &(prio)->node.children)

#define fs_for_each_fg(pos, ft)			\
	fs_list_for_each_entry(pos, &(ft)->node.children)

#define fs_for_each_fte(pos, fg)			\
	fs_list_for_each_entry(pos, &(fg)->node.children)

#define fs_for_each_dst(pos, fte)			\
	fs_list_for_each_entry(pos, &(fte)->node.children)

#endif
