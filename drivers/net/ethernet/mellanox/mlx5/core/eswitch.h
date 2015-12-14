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
#include <linux/mlx5/device.h>

#define MLX5_MAX_UC_PER_VPORT(dev) \
	(1 << MLX5_CAP_GEN(dev, log_max_current_uc_list))

#define MLX5_MAX_MC_PER_VPORT(dev) \
	(1 << MLX5_CAP_GEN(dev, log_max_current_mc_list))

#define MLX5_L2_ADDR_HASH_SIZE (BIT(BITS_PER_BYTE))
#define MLX5_L2_ADDR_HASH(addr) (addr[5])

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

struct mlx5_vport {
	struct mlx5_core_dev    *dev;
	int                     vport;
	struct hlist_head       uc_list[MLX5_L2_ADDR_HASH_SIZE];
	struct hlist_head       mc_list[MLX5_L2_ADDR_HASH_SIZE];
	struct work_struct      vport_change_handler;

	/* This spinlock protects access to vport data, between
	 * "esw_vport_disable" and ongoing interrupt "mlx5_eswitch_vport_event"
	 * once vport marked as disabled new interrupts are discarded.
	 */
	spinlock_t              lock; /* vport events sync */
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
	struct mlx5_flow_group *addr_grp;
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
};

/* E-Switch API */
int mlx5_eswitch_init(struct mlx5_core_dev *dev);
void mlx5_eswitch_cleanup(struct mlx5_eswitch *esw);
void mlx5_eswitch_vport_event(struct mlx5_eswitch *esw, struct mlx5_eqe *eqe);
int mlx5_eswitch_enable_sriov(struct mlx5_eswitch *esw, int nvfs);
void mlx5_eswitch_disable_sriov(struct mlx5_eswitch *esw);
int mlx5_eswitch_set_vport_mac(struct mlx5_eswitch *esw,
			       int vport, u8 mac[ETH_ALEN]);
int mlx5_eswitch_set_vport_state(struct mlx5_eswitch *esw,
				 int vport, int link_state);
int mlx5_eswitch_set_vport_vlan(struct mlx5_eswitch *esw,
				int vport, u16 vlan, u8 qos);
int mlx5_eswitch_get_vport_config(struct mlx5_eswitch *esw,
				  int vport, struct ifla_vf_info *ivi);
int mlx5_eswitch_get_vport_stats(struct mlx5_eswitch *esw,
				 int vport,
				 struct ifla_vf_stats *vf_stats);

#endif /* __MLX5_ESWITCH_H__ */
