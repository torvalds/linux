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

#include <linux/refcount.h>
#include <linux/mlx5/fs.h>
#include <linux/rhashtable.h>
#include <linux/llist.h>
#include <steering/fs_dr.h>

#define FDB_TC_MAX_CHAIN 3
#define FDB_FT_CHAIN (FDB_TC_MAX_CHAIN + 1)
#define FDB_TC_SLOW_PATH_CHAIN (FDB_FT_CHAIN + 1)

/* The index of the last real chain (FT) + 1 as chain zero is valid as well */
#define FDB_NUM_CHAINS (FDB_FT_CHAIN + 1)

#define FDB_TC_MAX_PRIO 16
#define FDB_TC_LEVELS_PER_PRIO 2

struct mlx5_flow_definer {
	enum mlx5_flow_namespace_type ns_type;
	u32 id;
};

enum mlx5_flow_resource_owner {
	MLX5_FLOW_RESOURCE_OWNER_FW,
	MLX5_FLOW_RESOURCE_OWNER_SW,
};

struct mlx5_modify_hdr {
	enum mlx5_flow_namespace_type ns_type;
	enum mlx5_flow_resource_owner owner;
	union {
		struct mlx5_fs_dr_action action;
		u32 id;
	};
};

struct mlx5_pkt_reformat {
	enum mlx5_flow_namespace_type ns_type;
	int reformat_type; /* from mlx5_ifc */
	enum mlx5_flow_resource_owner owner;
	union {
		struct mlx5_fs_dr_action action;
		u32 id;
	};
};

/* FS_TYPE_PRIO_CHAINS is a PRIO that will have namespaces only,
 * and those are in parallel to one another when going over them to connect
 * a new flow table. Meaning the last flow table in a TYPE_PRIO prio in one
 * parallel namespace will not automatically connect to the first flow table
 * found in any prio in any next namespace, but skip the entire containing
 * TYPE_PRIO_CHAINS prio.
 *
 * This is used to implement tc chains, each chain of prios is a different
 * namespace inside a containing TYPE_PRIO_CHAINS prio.
 */

enum fs_node_type {
	FS_TYPE_NAMESPACE,
	FS_TYPE_PRIO,
	FS_TYPE_PRIO_CHAINS,
	FS_TYPE_FLOW_TABLE,
	FS_TYPE_FLOW_GROUP,
	FS_TYPE_FLOW_ENTRY,
	FS_TYPE_FLOW_DEST
};

enum fs_flow_table_type {
	FS_FT_NIC_RX          = 0x0,
	FS_FT_NIC_TX          = 0x1,
	FS_FT_ESW_EGRESS_ACL  = 0x2,
	FS_FT_ESW_INGRESS_ACL = 0x3,
	FS_FT_FDB             = 0X4,
	FS_FT_SNIFFER_RX	= 0X5,
	FS_FT_SNIFFER_TX	= 0X6,
	FS_FT_RDMA_RX		= 0X7,
	FS_FT_RDMA_TX		= 0X8,
	FS_FT_PORT_SEL		= 0X9,
	FS_FT_FDB_RX		= 0xa,
	FS_FT_FDB_TX		= 0xb,
	FS_FT_MAX_TYPE = FS_FT_FDB_TX,
};

enum fs_flow_table_op_mod {
	FS_FT_OP_MOD_NORMAL,
	FS_FT_OP_MOD_LAG_DEMUX,
};

enum fs_fte_status {
	FS_FTE_STATUS_EXISTING = 1UL << 0,
};

enum mlx5_flow_steering_mode {
	MLX5_FLOW_STEERING_MODE_DMFS,
	MLX5_FLOW_STEERING_MODE_SMFS
};

enum mlx5_flow_steering_capabilty {
	MLX5_FLOW_STEERING_CAP_VLAN_PUSH_ON_RX = 1UL << 0,
	MLX5_FLOW_STEERING_CAP_VLAN_POP_ON_TX = 1UL << 1,
	MLX5_FLOW_STEERING_CAP_MATCH_RANGES = 1UL << 2,
	MLX5_FLOW_STEERING_CAP_DUPLICATE_MATCH = 1UL << 3,
};

struct mlx5_flow_steering {
	struct mlx5_core_dev *dev;
	enum   mlx5_flow_steering_mode	mode;
	struct kmem_cache		*fgs_cache;
	struct kmem_cache               *ftes_cache;
	struct mlx5_flow_root_namespace *root_ns;
	struct mlx5_flow_root_namespace *fdb_root_ns;
	struct mlx5_flow_namespace	**fdb_sub_ns;
	struct mlx5_flow_root_namespace **esw_egress_root_ns;
	struct mlx5_flow_root_namespace **esw_ingress_root_ns;
	struct mlx5_flow_root_namespace	*sniffer_tx_root_ns;
	struct mlx5_flow_root_namespace	*sniffer_rx_root_ns;
	struct mlx5_flow_root_namespace	*rdma_rx_root_ns;
	struct mlx5_flow_root_namespace	*rdma_tx_root_ns;
	struct mlx5_flow_root_namespace	*egress_root_ns;
	struct mlx5_flow_root_namespace	*port_sel_root_ns;
	int esw_egress_acl_vports;
	int esw_ingress_acl_vports;
};

struct fs_node {
	struct list_head	list;
	struct list_head	children;
	enum fs_node_type	type;
	struct fs_node		*parent;
	struct fs_node		*root;
	/* lock the node for writing and traversing */
	struct rw_semaphore	lock;
	refcount_t		refcount;
	bool			active;
	void			(*del_hw_func)(struct fs_node *);
	void			(*del_sw_func)(struct fs_node *);
	atomic_t		version;
};

struct mlx5_flow_rule {
	struct fs_node				node;
	struct mlx5_flow_table			*ft;
	struct mlx5_flow_destination		dest_attr;
	/* next_ft should be accessed under chain_lock and only of
	 * destination type is FWD_NEXT_fT.
	 */
	struct list_head			next_ft;
	u32					sw_action;
};

struct mlx5_flow_handle {
	int num_rules;
	struct mlx5_flow_rule *rule[] __counted_by(num_rules);
};

/* Type of children is mlx5_flow_group */
struct mlx5_flow_table {
	struct fs_node			node;
	struct mlx5_fs_dr_table		fs_dr_table;
	u32				id;
	u16				vport;
	unsigned int			max_fte;
	unsigned int			level;
	enum fs_flow_table_type		type;
	enum fs_flow_table_op_mod	op_mod;
	struct {
		bool			active;
		unsigned int		required_groups;
		unsigned int		group_size;
		unsigned int		num_groups;
		unsigned int		max_fte;
	} autogroup;
	/* Protect fwd_rules */
	struct mutex			lock;
	/* FWD rules that point on this flow table */
	struct list_head		fwd_rules;
	u32				flags;
	struct rhltable			fgs_hash;
	enum mlx5_flow_table_miss_action def_miss_action;
	struct mlx5_flow_namespace	*ns;
};

struct mlx5_ft_underlay_qp {
	struct list_head list;
	u32 qpn;
};

#define MLX5_FTE_MATCH_PARAM_RESERVED	reserved_at_e00
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

struct fs_fte_action {
	int				modify_mask;
	u32				dests_size;
	u32				fwd_dests;
	struct mlx5_flow_context	flow_context;
	struct mlx5_flow_act		action;
};

struct fs_fte_dup {
	struct list_head children;
	struct fs_fte_action act_dests;
};

/* Type of children is mlx5_flow_rule */
struct fs_fte {
	struct fs_node			node;
	struct mlx5_fs_dr_rule		fs_dr_rule;
	u32				val[MLX5_ST_SZ_DW_MATCH_PARAM];
	struct fs_fte_action		act_dests;
	struct fs_fte_dup		*dup;
	u32				index;
	enum fs_fte_status		status;
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
	enum mlx5_flow_table_miss_action def_miss_action;
};

struct mlx5_flow_group_mask {
	u8	match_criteria_enable;
	u32	match_criteria[MLX5_ST_SZ_DW_MATCH_PARAM];
};

/* Type of children is fs_fte */
struct mlx5_flow_group {
	struct fs_node			node;
	struct mlx5_fs_dr_matcher	fs_dr_matcher;
	struct mlx5_flow_group_mask	mask;
	u32				start_index;
	u32				max_ftes;
	struct ida			fte_allocator;
	u32				id;
	struct rhashtable		ftes_hash;
	struct rhlist_head		hash;
};

struct mlx5_flow_root_namespace {
	struct mlx5_flow_namespace	ns;
	enum   mlx5_flow_steering_mode	mode;
	struct mlx5_fs_dr_domain	fs_dr_domain;
	enum   fs_flow_table_type	table_type;
	struct mlx5_core_dev		*dev;
	struct mlx5_flow_table		*root_ft;
	/* Should be held when chaining flow tables */
	struct mutex			chain_lock;
	struct list_head		underlay_qpns;
	const struct mlx5_flow_cmds	*cmds;
};

int mlx5_init_fc_stats(struct mlx5_core_dev *dev);
void mlx5_cleanup_fc_stats(struct mlx5_core_dev *dev);
void mlx5_fc_queue_stats_work(struct mlx5_core_dev *dev,
			      struct delayed_work *dwork,
			      unsigned long delay);
void mlx5_fc_update_sampling_interval(struct mlx5_core_dev *dev,
				      unsigned long interval);

const struct mlx5_flow_cmds *mlx5_fs_cmd_get_fw_cmds(void);

int mlx5_flow_namespace_set_peer(struct mlx5_flow_root_namespace *ns,
				 struct mlx5_flow_root_namespace *peer_ns,
				 u16 peer_vhca_id);

int mlx5_flow_namespace_set_mode(struct mlx5_flow_namespace *ns,
				 enum mlx5_flow_steering_mode mode);

int mlx5_fs_core_alloc(struct mlx5_core_dev *dev);
void mlx5_fs_core_free(struct mlx5_core_dev *dev);
int mlx5_fs_core_init(struct mlx5_core_dev *dev);
void mlx5_fs_core_cleanup(struct mlx5_core_dev *dev);

int mlx5_fs_egress_acls_init(struct mlx5_core_dev *dev, int total_vports);
void mlx5_fs_egress_acls_cleanup(struct mlx5_core_dev *dev);
int mlx5_fs_ingress_acls_init(struct mlx5_core_dev *dev, int total_vports);
void mlx5_fs_ingress_acls_cleanup(struct mlx5_core_dev *dev);

u32 mlx5_fs_get_capabilities(struct mlx5_core_dev *dev, enum mlx5_flow_namespace_type type);

struct mlx5_flow_root_namespace *find_root(struct fs_node *node);

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

#define MLX5_CAP_FLOWTABLE_TYPE(mdev, cap, type) (		\
	(type == FS_FT_NIC_RX) ? MLX5_CAP_FLOWTABLE_NIC_RX(mdev, cap) :		\
	(type == FS_FT_NIC_TX) ? MLX5_CAP_FLOWTABLE_NIC_TX(mdev, cap) :		\
	(type == FS_FT_ESW_EGRESS_ACL) ? MLX5_CAP_ESW_EGRESS_ACL(mdev, cap) :		\
	(type == FS_FT_ESW_INGRESS_ACL) ? MLX5_CAP_ESW_INGRESS_ACL(mdev, cap) :		\
	(type == FS_FT_FDB) ? MLX5_CAP_ESW_FLOWTABLE_FDB(mdev, cap) :		\
	(type == FS_FT_SNIFFER_RX) ? MLX5_CAP_FLOWTABLE_SNIFFER_RX(mdev, cap) :		\
	(type == FS_FT_SNIFFER_TX) ? MLX5_CAP_FLOWTABLE_SNIFFER_TX(mdev, cap) :		\
	(type == FS_FT_RDMA_RX) ? MLX5_CAP_FLOWTABLE_RDMA_RX(mdev, cap) :		\
	(type == FS_FT_RDMA_TX) ? MLX5_CAP_FLOWTABLE_RDMA_TX(mdev, cap) :      \
	(type == FS_FT_PORT_SEL) ? MLX5_CAP_FLOWTABLE_PORT_SELECTION(mdev, cap) :      \
	(type == FS_FT_FDB_RX) ? MLX5_CAP_ESW_FLOWTABLE_FDB(mdev, cap) :      \
	(type == FS_FT_FDB_TX) ? MLX5_CAP_ESW_FLOWTABLE_FDB(mdev, cap) :      \
	(BUILD_BUG_ON_ZERO(FS_FT_FDB_TX != FS_FT_MAX_TYPE))\
	)

#endif
