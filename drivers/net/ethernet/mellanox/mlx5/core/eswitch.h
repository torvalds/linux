/*
 * Copyright (c) 2015, Mellanox Technologies, Ltd.  All rights reserved.
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

#ifndef __MLX5_ESWITCH_H__
#define __MLX5_ESWITCH_H__

#include <linux/if_ether.h>
#include <linux/if_link.h>
#include <linux/atomic.h>
#include <net/devlink.h>
#include <linux/mlx5/device.h>
#include <linux/mlx5/eswitch.h>
#include <linux/mlx5/vport.h>
#include <linux/mlx5/fs.h>
#include "lib/mpfs.h"
#include "lib/fs_chains.h"
#include "en/tc_ct.h"

#ifdef CONFIG_MLX5_ESWITCH

#define ESW_OFFLOADS_DEFAULT_NUM_GROUPS 15

#define MLX5_MAX_UC_PER_VPORT(dev) \
	(1 << MLX5_CAP_GEN(dev, log_max_current_uc_list))

#define MLX5_MAX_MC_PER_VPORT(dev) \
	(1 << MLX5_CAP_GEN(dev, log_max_current_mc_list))

#define MLX5_MIN_BW_SHARE 1

#define MLX5_RATE_TO_BW_SHARE(rate, divider, limit) \
	min_t(u32, max_t(u32, (rate) / (divider), MLX5_MIN_BW_SHARE), limit)

#define mlx5_esw_has_fwd_fdb(dev) \
	MLX5_CAP_ESW_FLOWTABLE(dev, fdb_multi_path_to_table)

#define esw_chains(esw) \
	((esw)->fdb_table.offloads.esw_chains_priv)

struct vport_ingress {
	struct mlx5_flow_table *acl;
	struct mlx5_flow_handle *allow_rule;
	struct {
		struct mlx5_flow_group *allow_spoofchk_only_grp;
		struct mlx5_flow_group *allow_untagged_spoofchk_grp;
		struct mlx5_flow_group *allow_untagged_only_grp;
		struct mlx5_flow_group *drop_grp;
		struct mlx5_flow_handle *drop_rule;
		struct mlx5_fc *drop_counter;
	} legacy;
	struct {
		/* Optional group to add an FTE to do internal priority
		 * tagging on ingress packets.
		 */
		struct mlx5_flow_group *metadata_prio_tag_grp;
		/* Group to add default match-all FTE entry to tag ingress
		 * packet with metadata.
		 */
		struct mlx5_flow_group *metadata_allmatch_grp;
		struct mlx5_modify_hdr *modify_metadata;
		struct mlx5_flow_handle *modify_metadata_rule;
	} offloads;
};

struct vport_egress {
	struct mlx5_flow_table *acl;
	struct mlx5_flow_handle  *allowed_vlan;
	struct mlx5_flow_group *vlan_grp;
	union {
		struct {
			struct mlx5_flow_group *drop_grp;
			struct mlx5_flow_handle *drop_rule;
			struct mlx5_fc *drop_counter;
		} legacy;
		struct {
			struct mlx5_flow_group *fwd_grp;
			struct mlx5_flow_handle *fwd_rule;
		} offloads;
	};
};

struct mlx5_vport_drop_stats {
	u64 rx_dropped;
	u64 tx_dropped;
};

struct mlx5_vport_info {
	u8                      mac[ETH_ALEN];
	u16                     vlan;
	u8                      qos;
	u64                     node_guid;
	int                     link_state;
	u32                     min_rate;
	u32                     max_rate;
	bool                    spoofchk;
	bool                    trusted;
};

/* Vport context events */
enum mlx5_eswitch_vport_event {
	MLX5_VPORT_UC_ADDR_CHANGE = BIT(0),
	MLX5_VPORT_MC_ADDR_CHANGE = BIT(1),
	MLX5_VPORT_PROMISC_CHANGE = BIT(3),
};

struct mlx5_vport {
	struct mlx5_core_dev    *dev;
	int                     vport;
	struct hlist_head       uc_list[MLX5_L2_ADDR_HASH_SIZE];
	struct hlist_head       mc_list[MLX5_L2_ADDR_HASH_SIZE];
	struct mlx5_flow_handle *promisc_rule;
	struct mlx5_flow_handle *allmulti_rule;
	struct work_struct      vport_change_handler;

	struct vport_ingress    ingress;
	struct vport_egress     egress;
	u32                     default_metadata;
	u32                     metadata;

	struct mlx5_vport_info  info;

	struct {
		bool            enabled;
		u32             esw_tsar_ix;
		u32             bw_share;
	} qos;

	bool                    enabled;
	enum mlx5_eswitch_vport_event enabled_events;
	struct devlink_port *dl_port;
};

struct mlx5_eswitch_fdb {
	union {
		struct legacy_fdb {
			struct mlx5_flow_table *fdb;
			struct mlx5_flow_group *addr_grp;
			struct mlx5_flow_group *allmulti_grp;
			struct mlx5_flow_group *promisc_grp;
			struct mlx5_flow_table *vepa_fdb;
			struct mlx5_flow_handle *vepa_uplink_rule;
			struct mlx5_flow_handle *vepa_star_rule;
		} legacy;

		struct offloads_fdb {
			struct mlx5_flow_namespace *ns;
			struct mlx5_flow_table *slow_fdb;
			struct mlx5_flow_group *send_to_vport_grp;
			struct mlx5_flow_group *peer_miss_grp;
			struct mlx5_flow_handle **peer_miss_rules;
			struct mlx5_flow_group *miss_grp;
			struct mlx5_flow_handle *miss_rule_uni;
			struct mlx5_flow_handle *miss_rule_multi;
			int vlan_push_pop_refcount;

			struct mlx5_fs_chains *esw_chains_priv;
			struct {
				DECLARE_HASHTABLE(table, 8);
				/* Protects vports.table */
				struct mutex lock;
			} vports;

		} offloads;
	};
	u32 flags;
};

struct mlx5_esw_offload {
	struct mlx5_flow_table *ft_offloads_restore;
	struct mlx5_flow_group *restore_group;
	struct mlx5_modify_hdr *restore_copy_hdr_id;

	struct mlx5_flow_table *ft_offloads;
	struct mlx5_flow_group *vport_rx_group;
	struct mlx5_eswitch_rep *vport_reps;
	struct list_head peer_flows;
	struct mutex peer_mutex;
	struct mutex encap_tbl_lock; /* protects encap_tbl */
	DECLARE_HASHTABLE(encap_tbl, 8);
	struct mutex decap_tbl_lock; /* protects decap_tbl */
	DECLARE_HASHTABLE(decap_tbl, 8);
	struct mod_hdr_tbl mod_hdr;
	DECLARE_HASHTABLE(termtbl_tbl, 8);
	struct mutex termtbl_mutex; /* protects termtbl hash */
	const struct mlx5_eswitch_rep_ops *rep_ops[NUM_REP_TYPES];
	u8 inline_mode;
	atomic64_t num_flows;
	enum devlink_eswitch_encap_mode encap;
	struct ida vport_metadata_ida;
	unsigned int host_number; /* ECPF supports one external host */
};

/* E-Switch MC FDB table hash node */
struct esw_mc_addr { /* SRIOV only */
	struct l2addr_node     node;
	struct mlx5_flow_handle *uplink_rule; /* Forward to uplink rule */
	u32                    refcnt;
};

struct mlx5_host_work {
	struct work_struct	work;
	struct mlx5_eswitch	*esw;
};

struct mlx5_esw_functions {
	struct mlx5_nb		nb;
	u16			num_vfs;
};

enum {
	MLX5_ESWITCH_VPORT_MATCH_METADATA = BIT(0),
	MLX5_ESWITCH_REG_C1_LOOPBACK_ENABLED = BIT(1),
};

struct mlx5_eswitch {
	struct mlx5_core_dev    *dev;
	struct mlx5_nb          nb;
	struct mlx5_eswitch_fdb fdb_table;
	/* legacy data structures */
	struct hlist_head       mc_table[MLX5_L2_ADDR_HASH_SIZE];
	struct esw_mc_addr mc_promisc;
	/* end of legacy */
	struct workqueue_struct *work_queue;
	struct mlx5_vport       *vports;
	u32 flags;
	int                     total_vports;
	int                     enabled_vports;
	/* Synchronize between vport change events
	 * and async SRIOV admin state changes
	 */
	struct mutex            state_lock;

	/* Protects eswitch mode change that occurs via one or more
	 * user commands, i.e. sriov state change, devlink commands.
	 */
	struct rw_semaphore mode_lock;

	struct {
		bool            enabled;
		u32             root_tsar_id;
	} qos;

	struct mlx5_esw_offload offloads;
	int                     mode;
	u16                     manager_vport;
	u16                     first_host_vport;
	struct mlx5_esw_functions esw_funcs;
	struct {
		u32             large_group_num;
	}  params;
};

void esw_offloads_disable(struct mlx5_eswitch *esw);
int esw_offloads_enable(struct mlx5_eswitch *esw);
void esw_offloads_cleanup_reps(struct mlx5_eswitch *esw);
int esw_offloads_init_reps(struct mlx5_eswitch *esw);

u32 mlx5_esw_match_metadata_alloc(struct mlx5_eswitch *esw);
void mlx5_esw_match_metadata_free(struct mlx5_eswitch *esw, u32 metadata);

int mlx5_esw_modify_vport_rate(struct mlx5_eswitch *esw, u16 vport_num,
			       u32 rate_mbps);

/* E-Switch API */
int mlx5_eswitch_init(struct mlx5_core_dev *dev);
void mlx5_eswitch_cleanup(struct mlx5_eswitch *esw);

#define MLX5_ESWITCH_IGNORE_NUM_VFS (-1)
int mlx5_eswitch_enable_locked(struct mlx5_eswitch *esw, int mode, int num_vfs);
int mlx5_eswitch_enable(struct mlx5_eswitch *esw, int num_vfs);
void mlx5_eswitch_disable_locked(struct mlx5_eswitch *esw, bool clear_vf);
void mlx5_eswitch_disable(struct mlx5_eswitch *esw, bool clear_vf);
int mlx5_eswitch_set_vport_mac(struct mlx5_eswitch *esw,
			       u16 vport, const u8 *mac);
int mlx5_eswitch_set_vport_state(struct mlx5_eswitch *esw,
				 u16 vport, int link_state);
int mlx5_eswitch_set_vport_vlan(struct mlx5_eswitch *esw,
				u16 vport, u16 vlan, u8 qos);
int mlx5_eswitch_set_vport_spoofchk(struct mlx5_eswitch *esw,
				    u16 vport, bool spoofchk);
int mlx5_eswitch_set_vport_trust(struct mlx5_eswitch *esw,
				 u16 vport_num, bool setting);
int mlx5_eswitch_set_vport_rate(struct mlx5_eswitch *esw, u16 vport,
				u32 max_rate, u32 min_rate);
int mlx5_eswitch_set_vepa(struct mlx5_eswitch *esw, u8 setting);
int mlx5_eswitch_get_vepa(struct mlx5_eswitch *esw, u8 *setting);
int mlx5_eswitch_get_vport_config(struct mlx5_eswitch *esw,
				  u16 vport, struct ifla_vf_info *ivi);
int mlx5_eswitch_get_vport_stats(struct mlx5_eswitch *esw,
				 u16 vport,
				 struct ifla_vf_stats *vf_stats);
void mlx5_eswitch_del_send_to_vport_rule(struct mlx5_flow_handle *rule);

int mlx5_eswitch_modify_esw_vport_context(struct mlx5_core_dev *dev, u16 vport,
					  bool other_vport, void *in);

struct mlx5_flow_spec;
struct mlx5_esw_flow_attr;
struct mlx5_termtbl_handle;

bool
mlx5_eswitch_termtbl_required(struct mlx5_eswitch *esw,
			      struct mlx5_flow_attr *attr,
			      struct mlx5_flow_act *flow_act,
			      struct mlx5_flow_spec *spec);

struct mlx5_flow_handle *
mlx5_eswitch_add_termtbl_rule(struct mlx5_eswitch *esw,
			      struct mlx5_flow_table *ft,
			      struct mlx5_flow_spec *spec,
			      struct mlx5_esw_flow_attr *attr,
			      struct mlx5_flow_act *flow_act,
			      struct mlx5_flow_destination *dest,
			      int num_dest);

void
mlx5_eswitch_termtbl_put(struct mlx5_eswitch *esw,
			 struct mlx5_termtbl_handle *tt);

struct mlx5_flow_handle *
mlx5_eswitch_add_offloaded_rule(struct mlx5_eswitch *esw,
				struct mlx5_flow_spec *spec,
				struct mlx5_flow_attr *attr);
struct mlx5_flow_handle *
mlx5_eswitch_add_fwd_rule(struct mlx5_eswitch *esw,
			  struct mlx5_flow_spec *spec,
			  struct mlx5_flow_attr *attr);
void
mlx5_eswitch_del_offloaded_rule(struct mlx5_eswitch *esw,
				struct mlx5_flow_handle *rule,
				struct mlx5_flow_attr *attr);
void
mlx5_eswitch_del_fwd_rule(struct mlx5_eswitch *esw,
			  struct mlx5_flow_handle *rule,
			  struct mlx5_flow_attr *attr);

struct mlx5_flow_handle *
mlx5_eswitch_create_vport_rx_rule(struct mlx5_eswitch *esw, u16 vport,
				  struct mlx5_flow_destination *dest);

enum {
	SET_VLAN_STRIP	= BIT(0),
	SET_VLAN_INSERT	= BIT(1)
};

enum mlx5_flow_match_level {
	MLX5_MATCH_NONE	= MLX5_INLINE_MODE_NONE,
	MLX5_MATCH_L2	= MLX5_INLINE_MODE_L2,
	MLX5_MATCH_L3	= MLX5_INLINE_MODE_IP,
	MLX5_MATCH_L4	= MLX5_INLINE_MODE_TCP_UDP,
};

/* current maximum for flow based vport multicasting */
#define MLX5_MAX_FLOW_FWD_VPORTS 2

enum {
	MLX5_ESW_DEST_ENCAP         = BIT(0),
	MLX5_ESW_DEST_ENCAP_VALID   = BIT(1),
};

enum {
	MLX5_ESW_ATTR_FLAG_VLAN_HANDLED  = BIT(0),
	MLX5_ESW_ATTR_FLAG_SLOW_PATH     = BIT(1),
	MLX5_ESW_ATTR_FLAG_NO_IN_PORT    = BIT(2),
};

struct mlx5_esw_flow_attr {
	struct mlx5_eswitch_rep *in_rep;
	struct mlx5_core_dev	*in_mdev;
	struct mlx5_core_dev    *counter_dev;

	int split_count;
	int out_count;

	__be16	vlan_proto[MLX5_FS_VLAN_DEPTH];
	u16	vlan_vid[MLX5_FS_VLAN_DEPTH];
	u8	vlan_prio[MLX5_FS_VLAN_DEPTH];
	u8	total_vlan;
	struct {
		u32 flags;
		struct mlx5_eswitch_rep *rep;
		struct mlx5_pkt_reformat *pkt_reformat;
		struct mlx5_core_dev *mdev;
		struct mlx5_termtbl_handle *termtbl;
	} dests[MLX5_MAX_FLOW_FWD_VPORTS];
	struct mlx5_pkt_reformat *decap_pkt_reformat;
};

int mlx5_devlink_eswitch_mode_set(struct devlink *devlink, u16 mode,
				  struct netlink_ext_ack *extack);
int mlx5_devlink_eswitch_mode_get(struct devlink *devlink, u16 *mode);
int mlx5_devlink_eswitch_inline_mode_set(struct devlink *devlink, u8 mode,
					 struct netlink_ext_ack *extack);
int mlx5_devlink_eswitch_inline_mode_get(struct devlink *devlink, u8 *mode);
int mlx5_devlink_eswitch_encap_mode_set(struct devlink *devlink,
					enum devlink_eswitch_encap_mode encap,
					struct netlink_ext_ack *extack);
int mlx5_devlink_eswitch_encap_mode_get(struct devlink *devlink,
					enum devlink_eswitch_encap_mode *encap);
int mlx5_devlink_port_function_hw_addr_get(struct devlink *devlink,
					   struct devlink_port *port,
					   u8 *hw_addr, int *hw_addr_len,
					   struct netlink_ext_ack *extack);
int mlx5_devlink_port_function_hw_addr_set(struct devlink *devlink,
					   struct devlink_port *port,
					   const u8 *hw_addr, int hw_addr_len,
					   struct netlink_ext_ack *extack);

void *mlx5_eswitch_get_uplink_priv(struct mlx5_eswitch *esw, u8 rep_type);

int mlx5_eswitch_add_vlan_action(struct mlx5_eswitch *esw,
				 struct mlx5_flow_attr *attr);
int mlx5_eswitch_del_vlan_action(struct mlx5_eswitch *esw,
				 struct mlx5_flow_attr *attr);
int __mlx5_eswitch_set_vport_vlan(struct mlx5_eswitch *esw,
				  u16 vport, u16 vlan, u8 qos, u8 set_flags);

static inline bool mlx5_esw_qos_enabled(struct mlx5_eswitch *esw)
{
	return esw->qos.enabled;
}

static inline bool mlx5_eswitch_vlan_actions_supported(struct mlx5_core_dev *dev,
						       u8 vlan_depth)
{
	bool ret = MLX5_CAP_ESW_FLOWTABLE_FDB(dev, pop_vlan) &&
		   MLX5_CAP_ESW_FLOWTABLE_FDB(dev, push_vlan);

	if (vlan_depth == 1)
		return ret;

	return  ret && MLX5_CAP_ESW_FLOWTABLE_FDB(dev, pop_vlan_2) &&
		MLX5_CAP_ESW_FLOWTABLE_FDB(dev, push_vlan_2);
}

bool mlx5_esw_lag_prereq(struct mlx5_core_dev *dev0,
			 struct mlx5_core_dev *dev1);
bool mlx5_esw_multipath_prereq(struct mlx5_core_dev *dev0,
			       struct mlx5_core_dev *dev1);

const u32 *mlx5_esw_query_functions(struct mlx5_core_dev *dev);

#define MLX5_DEBUG_ESWITCH_MASK BIT(3)

#define esw_info(__dev, format, ...)			\
	dev_info((__dev)->device, "E-Switch: " format, ##__VA_ARGS__)

#define esw_warn(__dev, format, ...)			\
	dev_warn((__dev)->device, "E-Switch: " format, ##__VA_ARGS__)

#define esw_debug(dev, format, ...)				\
	mlx5_core_dbg_mask(dev, MLX5_DEBUG_ESWITCH_MASK, format, ##__VA_ARGS__)

/* The returned number is valid only when the dev is eswitch manager. */
static inline u16 mlx5_eswitch_manager_vport(struct mlx5_core_dev *dev)
{
	return mlx5_core_is_ecpf_esw_manager(dev) ?
		MLX5_VPORT_ECPF : MLX5_VPORT_PF;
}

static inline bool
mlx5_esw_is_manager_vport(const struct mlx5_eswitch *esw, u16 vport_num)
{
	return esw->manager_vport == vport_num;
}

static inline u16 mlx5_eswitch_first_host_vport_num(struct mlx5_core_dev *dev)
{
	return mlx5_core_is_ecpf_esw_manager(dev) ?
		MLX5_VPORT_PF : MLX5_VPORT_FIRST_VF;
}

static inline bool mlx5_eswitch_is_funcs_handler(const struct mlx5_core_dev *dev)
{
	return mlx5_core_is_ecpf_esw_manager(dev);
}

static inline int mlx5_eswitch_uplink_idx(struct mlx5_eswitch *esw)
{
	/* Uplink always locate at the last element of the array.*/
	return esw->total_vports - 1;
}

static inline int mlx5_eswitch_ecpf_idx(struct mlx5_eswitch *esw)
{
	return esw->total_vports - 2;
}

static inline int mlx5_eswitch_vport_num_to_index(struct mlx5_eswitch *esw,
						  u16 vport_num)
{
	if (vport_num == MLX5_VPORT_ECPF) {
		if (!mlx5_ecpf_vport_exists(esw->dev))
			esw_warn(esw->dev, "ECPF vport doesn't exist!\n");
		return mlx5_eswitch_ecpf_idx(esw);
	}

	if (vport_num == MLX5_VPORT_UPLINK)
		return mlx5_eswitch_uplink_idx(esw);

	return vport_num;
}

static inline u16 mlx5_eswitch_index_to_vport_num(struct mlx5_eswitch *esw,
						  int index)
{
	if (index == mlx5_eswitch_ecpf_idx(esw) &&
	    mlx5_ecpf_vport_exists(esw->dev))
		return MLX5_VPORT_ECPF;

	if (index == mlx5_eswitch_uplink_idx(esw))
		return MLX5_VPORT_UPLINK;

	return index;
}

static inline unsigned int
mlx5_esw_vport_to_devlink_port_index(const struct mlx5_core_dev *dev,
				     u16 vport_num)
{
	return (MLX5_CAP_GEN(dev, vhca_id) << 16) | vport_num;
}

static inline u16
mlx5_esw_devlink_port_index_to_vport_num(unsigned int dl_port_index)
{
	return dl_port_index & 0xffff;
}

/* TODO: This mlx5e_tc function shouldn't be called by eswitch */
void mlx5e_tc_clean_fdb_peer_flows(struct mlx5_eswitch *esw);

/* The vport getter/iterator are only valid after esw->total_vports
 * and vport->vport are initialized in mlx5_eswitch_init.
 */
#define mlx5_esw_for_all_vports(esw, i, vport)		\
	for ((i) = MLX5_VPORT_PF;			\
	     (vport) = &(esw)->vports[i],		\
	     (i) < (esw)->total_vports; (i)++)

#define mlx5_esw_for_all_vports_reverse(esw, i, vport)	\
	for ((i) = (esw)->total_vports - 1;		\
	     (vport) = &(esw)->vports[i],		\
	     (i) >= MLX5_VPORT_PF; (i)--)

#define mlx5_esw_for_each_vf_vport(esw, i, vport, nvfs)	\
	for ((i) = MLX5_VPORT_FIRST_VF;			\
	     (vport) = &(esw)->vports[(i)],		\
	     (i) <= (nvfs); (i)++)

#define mlx5_esw_for_each_vf_vport_reverse(esw, i, vport, nvfs)	\
	for ((i) = (nvfs);					\
	     (vport) = &(esw)->vports[(i)],			\
	     (i) >= MLX5_VPORT_FIRST_VF; (i)--)

/* The rep getter/iterator are only valid after esw->total_vports
 * and vport->vport are initialized in mlx5_eswitch_init.
 */
#define mlx5_esw_for_all_reps(esw, i, rep)			\
	for ((i) = MLX5_VPORT_PF;				\
	     (rep) = &(esw)->offloads.vport_reps[i],		\
	     (i) < (esw)->total_vports; (i)++)

#define mlx5_esw_for_each_vf_rep(esw, i, rep, nvfs)		\
	for ((i) = MLX5_VPORT_FIRST_VF;				\
	     (rep) = &(esw)->offloads.vport_reps[i],		\
	     (i) <= (nvfs); (i)++)

#define mlx5_esw_for_each_vf_rep_reverse(esw, i, rep, nvfs)	\
	for ((i) = (nvfs);					\
	     (rep) = &(esw)->offloads.vport_reps[i],		\
	     (i) >= MLX5_VPORT_FIRST_VF; (i)--)

#define mlx5_esw_for_each_vf_vport_num(esw, vport, nvfs)	\
	for ((vport) = MLX5_VPORT_FIRST_VF; (vport) <= (nvfs); (vport)++)

#define mlx5_esw_for_each_vf_vport_num_reverse(esw, vport, nvfs)	\
	for ((vport) = (nvfs); (vport) >= MLX5_VPORT_FIRST_VF; (vport)--)

/* Includes host PF (vport 0) if it's not esw manager. */
#define mlx5_esw_for_each_host_func_rep(esw, i, rep, nvfs)	\
	for ((i) = (esw)->first_host_vport;			\
	     (rep) = &(esw)->offloads.vport_reps[i],		\
	     (i) <= (nvfs); (i)++)

#define mlx5_esw_for_each_host_func_rep_reverse(esw, i, rep, nvfs)	\
	for ((i) = (nvfs);						\
	     (rep) = &(esw)->offloads.vport_reps[i],			\
	     (i) >= (esw)->first_host_vport; (i)--)

#define mlx5_esw_for_each_host_func_vport(esw, vport, nvfs)	\
	for ((vport) = (esw)->first_host_vport;			\
	     (vport) <= (nvfs); (vport)++)

#define mlx5_esw_for_each_host_func_vport_reverse(esw, vport, nvfs)	\
	for ((vport) = (nvfs);						\
	     (vport) >= (esw)->first_host_vport; (vport)--)

struct mlx5_eswitch *mlx5_devlink_eswitch_get(struct devlink *devlink);
struct mlx5_vport *__must_check
mlx5_eswitch_get_vport(struct mlx5_eswitch *esw, u16 vport_num);

bool mlx5_eswitch_is_vf_vport(const struct mlx5_eswitch *esw, u16 vport_num);

int mlx5_esw_funcs_changed_handler(struct notifier_block *nb, unsigned long type, void *data);

int
mlx5_eswitch_enable_pf_vf_vports(struct mlx5_eswitch *esw,
				 enum mlx5_eswitch_vport_event enabled_events);
void mlx5_eswitch_disable_pf_vf_vports(struct mlx5_eswitch *esw);

int
esw_vport_create_offloads_acl_tables(struct mlx5_eswitch *esw,
				     struct mlx5_vport *vport);
void
esw_vport_destroy_offloads_acl_tables(struct mlx5_eswitch *esw,
				      struct mlx5_vport *vport);

int mlx5_esw_vport_tbl_get(struct mlx5_eswitch *esw);
void mlx5_esw_vport_tbl_put(struct mlx5_eswitch *esw);

struct mlx5_flow_handle *
esw_add_restore_rule(struct mlx5_eswitch *esw, u32 tag);
u32
esw_get_max_restore_tag(struct mlx5_eswitch *esw);

int esw_offloads_load_rep(struct mlx5_eswitch *esw, u16 vport_num);
void esw_offloads_unload_rep(struct mlx5_eswitch *esw, u16 vport_num);

int mlx5_eswitch_load_vport(struct mlx5_eswitch *esw, u16 vport_num,
			    enum mlx5_eswitch_vport_event enabled_events);
void mlx5_eswitch_unload_vport(struct mlx5_eswitch *esw, u16 vport_num);

int mlx5_eswitch_load_vf_vports(struct mlx5_eswitch *esw, u16 num_vfs,
				enum mlx5_eswitch_vport_event enabled_events);
void mlx5_eswitch_unload_vf_vports(struct mlx5_eswitch *esw, u16 num_vfs);

int mlx5_esw_offloads_devlink_port_register(struct mlx5_eswitch *esw, u16 vport_num);
void mlx5_esw_offloads_devlink_port_unregister(struct mlx5_eswitch *esw, u16 vport_num);
struct devlink_port *mlx5_esw_offloads_devlink_port(struct mlx5_eswitch *esw, u16 vport_num);
#else  /* CONFIG_MLX5_ESWITCH */
/* eswitch API stubs */
static inline int  mlx5_eswitch_init(struct mlx5_core_dev *dev) { return 0; }
static inline void mlx5_eswitch_cleanup(struct mlx5_eswitch *esw) {}
static inline int mlx5_eswitch_enable(struct mlx5_eswitch *esw, int num_vfs) { return 0; }
static inline void mlx5_eswitch_disable(struct mlx5_eswitch *esw, bool clear_vf) {}
static inline bool mlx5_esw_lag_prereq(struct mlx5_core_dev *dev0, struct mlx5_core_dev *dev1) { return true; }
static inline bool mlx5_eswitch_is_funcs_handler(struct mlx5_core_dev *dev) { return false; }
static inline
int mlx5_eswitch_set_vport_state(struct mlx5_eswitch *esw, u16 vport, int link_state) { return 0; }
static inline const u32 *mlx5_esw_query_functions(struct mlx5_core_dev *dev)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline struct mlx5_flow_handle *
esw_add_restore_rule(struct mlx5_eswitch *esw, u32 tag)
{
	return ERR_PTR(-EOPNOTSUPP);
}
#endif /* CONFIG_MLX5_ESWITCH */

#endif /* __MLX5_ESWITCH_H__ */
