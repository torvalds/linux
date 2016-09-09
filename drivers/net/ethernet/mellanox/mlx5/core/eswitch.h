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

#define MLX5_MAX_UC_PER_VPORT(dev) \
	(1 << MLX5_CAP_GEN(dev, log_max_current_uc_list))

#define MLX5_MAX_MC_PER_VPORT(dev) \
	(1 << MLX5_CAP_GEN(dev, log_max_current_mc_list))

#define MLX5_L2_ADDR_HASH_SIZE (BIT(BITS_PER_BYTE))
#define MLX5_L2_ADDR_HASH(addr) (addr[5])

#define FDB_UPLINK_VPORT 0xffff

/* L2 -mac address based- hash helpers */
struct l2addr_node {
	struct hlist_node hlist;
	u8                addr[ETH_ALEN];
};

#define for_each_l2hash_node(hn, tmp, hash, i) \
	for (i = 0; i < MLX5_L2_ADDR_HASH_SIZE; i++) \
		hlist_for_each_entry_safe(hn, tmp, &hash[i], hlist)

#define l2addr_hash_find(hash, mac, type) ({                \
	int ix = MLX5_L2_ADDR_HASH(mac);                    \
	bool found = false;                                 \
	type *ptr = NULL;                                   \
							    \
	hlist_for_each_entry(ptr, &hash[ix], node.hlist)    \
		if (ether_addr_equal(ptr->node.addr, mac)) {\
			found = true;                       \
			break;                              \
		}                                           \
	if (!found)                                         \
		ptr = NULL;                                 \
	ptr;                                                \
})

#define l2addr_hash_add(hash, mac, type, gfp) ({            \
	int ix = MLX5_L2_ADDR_HASH(mac);                    \
	type *ptr = NULL;                                   \
							    \
	ptr = kzalloc(sizeof(type), gfp);                   \
	if (ptr) {                                          \
		ether_addr_copy(ptr->node.addr, mac);       \
		hlist_add_head(&ptr->node.hlist, &hash[ix]);\
	}                                                   \
	ptr;                                                \
})

#define l2addr_hash_del(ptr) ({                             \
	hlist_del(&ptr->node.hlist);                        \
	kfree(ptr);                                         \
})

struct vport_ingress {
	struct mlx5_flow_table *acl;
	struct mlx5_flow_group *allow_untagged_spoofchk_grp;
	struct mlx5_flow_group *allow_spoofchk_only_grp;
	struct mlx5_flow_group *allow_untagged_only_grp;
	struct mlx5_flow_group *drop_grp;
	struct mlx5_flow_rule  *allow_rule;
	struct mlx5_flow_rule  *drop_rule;
};

struct vport_egress {
	struct mlx5_flow_table *acl;
	struct mlx5_flow_group *allowed_vlans_grp;
	struct mlx5_flow_group *drop_grp;
	struct mlx5_flow_rule  *allowed_vlan;
	struct mlx5_flow_rule  *drop_rule;
};

struct mlx5_vport_info {
	u8                      mac[ETH_ALEN];
	u16                     vlan;
	u8                      qos;
	u64                     node_guid;
	int                     link_state;
	bool                    spoofchk;
	bool                    trusted;
};

struct mlx5_vport {
	struct mlx5_core_dev    *dev;
	int                     vport;
	struct hlist_head       uc_list[MLX5_L2_ADDR_HASH_SIZE];
	struct hlist_head       mc_list[MLX5_L2_ADDR_HASH_SIZE];
	struct mlx5_flow_rule   *promisc_rule;
	struct mlx5_flow_rule   *allmulti_rule;
	struct work_struct      vport_change_handler;

	struct vport_ingress    ingress;
	struct vport_egress     egress;

	struct mlx5_vport_info  info;

	bool                    enabled;
	u16                     enabled_events;
};

struct mlx5_l2_table {
	struct hlist_head l2_hash[MLX5_L2_ADDR_HASH_SIZE];
	u32                  size;
	unsigned long        *bitmap;
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
			struct mlx5_flow_rule  *miss_rule;
		} offloads;
	};
};

enum {
	SRIOV_NONE,
	SRIOV_LEGACY,
	SRIOV_OFFLOADS
};

struct mlx5_esw_sq {
	struct mlx5_flow_rule	*send_to_vport_rule;
	struct list_head	 list;
};

struct mlx5_eswitch_rep {
	int		       (*load)(struct mlx5_eswitch *esw,
				       struct mlx5_eswitch_rep *rep);
	void		       (*unload)(struct mlx5_eswitch *esw,
					 struct mlx5_eswitch_rep *rep);
	u16		       vport;
	struct mlx5_flow_rule *vport_rx_rule;
	void		      *priv_data;
	struct list_head       vport_sqs_list;
	bool		       valid;
	u8		       hw_id[ETH_ALEN];
};

struct mlx5_esw_offload {
	struct mlx5_flow_table *ft_offloads;
	struct mlx5_flow_group *vport_rx_group;
	struct mlx5_eswitch_rep *vport_reps;
};

struct mlx5_eswitch {
	struct mlx5_core_dev    *dev;
	struct mlx5_l2_table    l2_table;
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
	struct esw_mc_addr      *mc_promisc;
	struct mlx5_esw_offload offloads;
	int                     mode;
};

/* E-Switch API */
int mlx5_eswitch_init(struct mlx5_core_dev *dev);
void mlx5_eswitch_cleanup(struct mlx5_eswitch *esw);
void mlx5_eswitch_attach(struct mlx5_eswitch *esw);
void mlx5_eswitch_detach(struct mlx5_eswitch *esw);
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
int mlx5_eswitch_get_vport_config(struct mlx5_eswitch *esw,
				  int vport, struct ifla_vf_info *ivi);
int mlx5_eswitch_get_vport_stats(struct mlx5_eswitch *esw,
				 int vport,
				 struct ifla_vf_stats *vf_stats);

struct mlx5_flow_spec;

struct mlx5_flow_rule *
mlx5_eswitch_add_offloaded_rule(struct mlx5_eswitch *esw,
				struct mlx5_flow_spec *spec,
				u32 action, u32 src_vport, u32 dst_vport);
struct mlx5_flow_rule *
mlx5_eswitch_create_vport_rx_rule(struct mlx5_eswitch *esw, int vport, u32 tirn);

int mlx5_eswitch_sqs2vport_start(struct mlx5_eswitch *esw,
				 struct mlx5_eswitch_rep *rep,
				 u16 *sqns_array, int sqns_num);
void mlx5_eswitch_sqs2vport_stop(struct mlx5_eswitch *esw,
				 struct mlx5_eswitch_rep *rep);

int mlx5_devlink_eswitch_mode_set(struct devlink *devlink, u16 mode);
int mlx5_devlink_eswitch_mode_get(struct devlink *devlink, u16 *mode);
void mlx5_eswitch_register_vport_rep(struct mlx5_eswitch *esw,
				     struct mlx5_eswitch_rep *rep);
void mlx5_eswitch_unregister_vport_rep(struct mlx5_eswitch *esw,
				       int vport);

#define MLX5_DEBUG_ESWITCH_MASK BIT(3)

#define esw_info(dev, format, ...)				\
	pr_info("(%s): E-Switch: " format, (dev)->priv.name, ##__VA_ARGS__)

#define esw_warn(dev, format, ...)				\
	pr_warn("(%s): E-Switch: " format, (dev)->priv.name, ##__VA_ARGS__)

#define esw_debug(dev, format, ...)				\
	mlx5_core_dbg_mask(dev, MLX5_DEBUG_ESWITCH_MASK, format, ##__VA_ARGS__)
#endif /* __MLX5_ESWITCH_H__ */
