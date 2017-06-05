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
#include <net/devlink.h>
#include <linux/mlx5/device.h>
#include "lib/mpfs.h"

enum {
	SRIOV_NONE,
	SRIOV_LEGACY,
	SRIOV_OFFLOADS
};

#ifdef CONFIG_MLX5_ESWITCH

#define MLX5_MAX_UC_PER_VPORT(dev) \
	(1 << MLX5_CAP_GEN(dev, log_max_current_uc_list))

#define MLX5_MAX_MC_PER_VPORT(dev) \
	(1 << MLX5_CAP_GEN(dev, log_max_current_mc_list))

#define FDB_UPLINK_VPORT 0xffff

#define MLX5_MIN_BW_SHARE 1

#define MLX5_RATE_TO_BW_SHARE(rate, divider, limit) \
	min_t(u32, max_t(u32, (rate) / (divider), MLX5_MIN_BW_SHARE), limit)

struct vport_ingress {
	struct mlx5_flow_table *acl;
	struct mlx5_flow_group *allow_untagged_spoofchk_grp;
	struct mlx5_flow_group *allow_spoofchk_only_grp;
	struct mlx5_flow_group *allow_untagged_only_grp;
	struct mlx5_flow_group *drop_grp;
	struct mlx5_flow_handle  *allow_rule;
	struct mlx5_flow_handle  *drop_rule;
};

struct vport_egress {
	struct mlx5_flow_table *acl;
	struct mlx5_flow_group *allowed_vlans_grp;
	struct mlx5_flow_group *drop_grp;
	struct mlx5_flow_handle  *allowed_vlan;
	struct mlx5_flow_handle  *drop_rule;
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

	struct mlx5_vport_info  info;

	struct {
		bool            enabled;
		u32             esw_tsar_ix;
		u32             bw_share;
	} qos;

	bool                    enabled;
	u16                     enabled_events;
};

struct mlx5_eswitch_fdb {
	void *fdb;
	union {
		struct legacy_fdb {
			struct mlx5_flow_group *addr_grp;
			struct mlx5_flow_group *allmulti_grp;
			struct mlx5_flow_group *promisc_grp;
		} legacy;

		struct offloads_fdb {
			struct mlx5_flow_table *fdb;
			struct mlx5_flow_group *send_to_vport_grp;
			struct mlx5_flow_group *miss_grp;
			struct mlx5_flow_handle *miss_rule;
			int vlan_push_pop_refcount;
		} offloads;
	};
};

struct mlx5_esw_sq {
	struct mlx5_flow_handle	*send_to_vport_rule;
	struct list_head	 list;
};

struct mlx5_eswitch_rep {
	int		       (*load)(struct mlx5_eswitch *esw,
				       struct mlx5_eswitch_rep *rep);
	void		       (*unload)(struct mlx5_eswitch *esw,
					 struct mlx5_eswitch_rep *rep);
	u16		       vport;
	u8		       hw_id[ETH_ALEN];
	struct net_device      *netdev;

	struct mlx5_flow_handle *vport_rx_rule;
	struct list_head       vport_sqs_list;
	u16		       vlan;
	u32		       vlan_refcount;
	bool		       valid;
};

struct mlx5_esw_offload {
	struct mlx5_flow_table *ft_offloads;
	struct mlx5_flow_group *vport_rx_group;
	struct mlx5_eswitch_rep *vport_reps;
	DECLARE_HASHTABLE(encap_tbl, 8);
	DECLARE_HASHTABLE(mod_hdr_tbl, 8);
	u8 inline_mode;
	u64 num_flows;
	u8 encap;
};

/* E-Switch MC FDB table hash node */
struct esw_mc_addr { /* SRIOV only */
	struct l2addr_node     node;
	struct mlx5_flow_handle *uplink_rule; /* Forward to uplink rule */
	u32                    refcnt;
};

struct mlx5_eswitch {
	struct mlx5_core_dev    *dev;
	struct mlx5_eswitch_fdb fdb_table;
	struct hlist_head       mc_table[MLX5_L2_ADDR_HASH_SIZE];
	struct workqueue_struct *work_queue;
	struct mlx5_vport       *vports;
	int                     total_vports;
	int                     enabled_vports;
	/* Synchronize between vport change events
	 * and async SRIOV admin state changes
	 */
	struct mutex            state_lock;
	struct esw_mc_addr	mc_promisc;

	struct {
		bool            enabled;
		u32             root_tsar_id;
	} qos;

	struct mlx5_esw_offload offloads;
	int                     mode;
};

void esw_offloads_cleanup(struct mlx5_eswitch *esw, int nvports);
int esw_offloads_init(struct mlx5_eswitch *esw, int nvports);

/* E-Switch API */
int mlx5_eswitch_init(struct mlx5_core_dev *dev);
void mlx5_eswitch_cleanup(struct mlx5_eswitch *esw);
void mlx5_eswitch_vport_event(struct mlx5_eswitch *esw, struct mlx5_eqe *eqe);
int mlx5_eswitch_enable_sriov(struct mlx5_eswitch *esw, int nvfs, int mode);
void mlx5_eswitch_disable_sriov(struct mlx5_eswitch *esw);
int mlx5_eswitch_set_vport_mac(struct mlx5_eswitch *esw,
			       int vport, u8 mac[ETH_ALEN]);
int mlx5_eswitch_set_vport_state(struct mlx5_eswitch *esw,
				 int vport, int link_state);
int mlx5_eswitch_set_vport_vlan(struct mlx5_eswitch *esw,
				int vport, u16 vlan, u8 qos);
int mlx5_eswitch_set_vport_spoofchk(struct mlx5_eswitch *esw,
				    int vport, bool spoofchk);
int mlx5_eswitch_set_vport_trust(struct mlx5_eswitch *esw,
				 int vport_num, bool setting);
int mlx5_eswitch_set_vport_rate(struct mlx5_eswitch *esw, int vport,
				u32 max_rate, u32 min_rate);
int mlx5_eswitch_get_vport_config(struct mlx5_eswitch *esw,
				  int vport, struct ifla_vf_info *ivi);
int mlx5_eswitch_get_vport_stats(struct mlx5_eswitch *esw,
				 int vport,
				 struct ifla_vf_stats *vf_stats);

struct mlx5_flow_spec;
struct mlx5_esw_flow_attr;

struct mlx5_flow_handle *
mlx5_eswitch_add_offloaded_rule(struct mlx5_eswitch *esw,
				struct mlx5_flow_spec *spec,
				struct mlx5_esw_flow_attr *attr);
void
mlx5_eswitch_del_offloaded_rule(struct mlx5_eswitch *esw,
				struct mlx5_flow_handle *rule,
				struct mlx5_esw_flow_attr *attr);

struct mlx5_flow_handle *
mlx5_eswitch_create_vport_rx_rule(struct mlx5_eswitch *esw, int vport, u32 tirn);

enum {
	SET_VLAN_STRIP	= BIT(0),
	SET_VLAN_INSERT	= BIT(1)
};

#define MLX5_FLOW_CONTEXT_ACTION_VLAN_POP  0x4000
#define MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH 0x8000

struct mlx5_esw_flow_attr {
	struct mlx5_eswitch_rep *in_rep;
	struct mlx5_eswitch_rep *out_rep;

	int	action;
	u16	vlan;
	bool	vlan_handled;
	u32	encap_id;
	u32	mod_hdr_id;
	struct mlx5e_tc_flow_parse_attr *parse_attr;
};

int mlx5_eswitch_sqs2vport_start(struct mlx5_eswitch *esw,
				 struct mlx5_eswitch_rep *rep,
				 u16 *sqns_array, int sqns_num);
void mlx5_eswitch_sqs2vport_stop(struct mlx5_eswitch *esw,
				 struct mlx5_eswitch_rep *rep);

int mlx5_devlink_eswitch_mode_set(struct devlink *devlink, u16 mode);
int mlx5_devlink_eswitch_mode_get(struct devlink *devlink, u16 *mode);
int mlx5_devlink_eswitch_inline_mode_set(struct devlink *devlink, u8 mode);
int mlx5_devlink_eswitch_inline_mode_get(struct devlink *devlink, u8 *mode);
int mlx5_eswitch_inline_mode_get(struct mlx5_eswitch *esw, int nvfs, u8 *mode);
int mlx5_devlink_eswitch_encap_mode_set(struct devlink *devlink, u8 encap);
int mlx5_devlink_eswitch_encap_mode_get(struct devlink *devlink, u8 *encap);
void mlx5_eswitch_register_vport_rep(struct mlx5_eswitch *esw,
				     int vport_index,
				     struct mlx5_eswitch_rep *rep);
void mlx5_eswitch_unregister_vport_rep(struct mlx5_eswitch *esw,
				       int vport_index);
struct net_device *mlx5_eswitch_get_uplink_netdev(struct mlx5_eswitch *esw);

int mlx5_eswitch_add_vlan_action(struct mlx5_eswitch *esw,
				 struct mlx5_esw_flow_attr *attr);
int mlx5_eswitch_del_vlan_action(struct mlx5_eswitch *esw,
				 struct mlx5_esw_flow_attr *attr);
int __mlx5_eswitch_set_vport_vlan(struct mlx5_eswitch *esw,
				  int vport, u16 vlan, u8 qos, u8 set_flags);

#define MLX5_DEBUG_ESWITCH_MASK BIT(3)

#define esw_info(dev, format, ...)				\
	pr_info("(%s): E-Switch: " format, (dev)->priv.name, ##__VA_ARGS__)

#define esw_warn(dev, format, ...)				\
	pr_warn("(%s): E-Switch: " format, (dev)->priv.name, ##__VA_ARGS__)

#define esw_debug(dev, format, ...)				\
	mlx5_core_dbg_mask(dev, MLX5_DEBUG_ESWITCH_MASK, format, ##__VA_ARGS__)
#else  /* CONFIG_MLX5_ESWITCH */
/* eswitch API stubs */
static inline int  mlx5_eswitch_init(struct mlx5_core_dev *dev) { return 0; }
static inline void mlx5_eswitch_cleanup(struct mlx5_eswitch *esw) {}
static inline void mlx5_eswitch_vport_event(struct mlx5_eswitch *esw, struct mlx5_eqe *eqe) {}
static inline int  mlx5_eswitch_enable_sriov(struct mlx5_eswitch *esw, int nvfs, int mode) { return 0; }
static inline void mlx5_eswitch_disable_sriov(struct mlx5_eswitch *esw) {}
#endif /* CONFIG_MLX5_ESWITCH */

#endif /* __MLX5_ESWITCH_H__ */
