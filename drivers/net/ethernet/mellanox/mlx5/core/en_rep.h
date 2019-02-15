/*
 * Copyright (c) 2017, Mellanox Technologies. All rights reserved.
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

#ifndef __MLX5E_REP_H__
#define __MLX5E_REP_H__

#include <net/ip_tunnels.h>
#include <linux/rhashtable.h>
#include "eswitch.h"
#include "en.h"

#ifdef CONFIG_MLX5_ESWITCH
struct mlx5e_neigh_update_table {
	struct rhashtable       neigh_ht;
	/* Save the neigh hash entries in a list in addition to the hash table
	 * (neigh_ht). In order to iterate easily over the neigh entries.
	 * Used for stats query.
	 */
	struct list_head	neigh_list;
	/* protect lookup/remove operations */
	spinlock_t              encap_lock;
	struct notifier_block   netevent_nb;
	struct delayed_work     neigh_stats_work;
	unsigned long           min_interval; /* jiffies */
};

struct mlx5_rep_uplink_priv {
	/* Filters DB - instantiated by the uplink representor and shared by
	 * the uplink's VFs
	 */
	struct rhashtable  tc_ht;

	/* indirect block callbacks are invoked on bind/unbind events
	 * on registered higher level devices (e.g. tunnel devices)
	 *
	 * tc_indr_block_cb_priv_list is used to lookup indirect callback
	 * private data
	 *
	 * netdevice_nb is the netdev events notifier - used to register
	 * tunnel devices for block events
	 *
	 */
	struct list_head	    tc_indr_block_priv_list;
	struct notifier_block	    netdevice_nb;
};

struct mlx5e_rep_priv {
	struct mlx5_eswitch_rep *rep;
	struct mlx5e_neigh_update_table neigh_update;
	struct net_device      *netdev;
	struct mlx5_flow_handle *vport_rx_rule;
	struct list_head       vport_sqs_list;
	struct mlx5_rep_uplink_priv uplink_priv; /* valid for uplink rep */
};

static inline
struct mlx5e_rep_priv *mlx5e_rep_to_rep_priv(struct mlx5_eswitch_rep *rep)
{
	return (struct mlx5e_rep_priv *)rep->rep_if[REP_ETH].priv;
}

struct mlx5e_neigh {
	struct net_device *dev;
	union {
		__be32	v4;
		struct in6_addr v6;
	} dst_ip;
	int family;
};

struct mlx5e_neigh_hash_entry {
	struct rhash_head rhash_node;
	struct mlx5e_neigh m_neigh;

	/* Save the neigh hash entry in a list on the representor in
	 * addition to the hash table. In order to iterate easily over the
	 * neighbour entries. Used for stats query.
	 */
	struct list_head neigh_list;

	/* encap list sharing the same neigh */
	struct list_head encap_list;

	/* valid only when the neigh reference is taken during
	 * neigh_update_work workqueue callback.
	 */
	struct neighbour *n;
	struct work_struct neigh_update_work;

	/* neigh hash entry can be deleted only when the refcount is zero.
	 * refcount is needed to avoid neigh hash entry removal by TC, while
	 * it's used by the neigh notification call.
	 */
	refcount_t refcnt;

	/* Save the last reported time offloaded trafic pass over one of the
	 * neigh hash entry flows. Use it to periodically update the neigh
	 * 'used' value and avoid neigh deleting by the kernel.
	 */
	unsigned long reported_lastuse;
};

enum {
	/* set when the encap entry is successfully offloaded into HW */
	MLX5_ENCAP_ENTRY_VALID     = BIT(0),
};

struct mlx5e_encap_entry {
	/* neigh hash entry list of encaps sharing the same neigh */
	struct list_head encap_list;
	struct mlx5e_neigh m_neigh;
	/* a node of the eswitch encap hash table which keeping all the encap
	 * entries
	 */
	struct hlist_node encap_hlist;
	struct list_head flows;
	u32 encap_id;
	struct ip_tunnel_info tun_info;
	unsigned char h_dest[ETH_ALEN];	/* destination eth addr	*/

	struct net_device *out_dev;
	struct net_device *route_dev;
	int tunnel_type;
	int tunnel_hlen;
	int reformat_type;
	u8 flags;
	char *encap_header;
	int encap_size;
};

struct mlx5e_rep_sq {
	struct mlx5_flow_handle	*send_to_vport_rule;
	struct list_head	 list;
};

void *mlx5e_alloc_nic_rep_priv(struct mlx5_core_dev *mdev);
void mlx5e_rep_register_vport_reps(struct mlx5_core_dev *mdev);
void mlx5e_rep_unregister_vport_reps(struct mlx5_core_dev *mdev);
bool mlx5e_is_uplink_rep(struct mlx5e_priv *priv);
int mlx5e_add_sqs_fwd_rules(struct mlx5e_priv *priv);
void mlx5e_remove_sqs_fwd_rules(struct mlx5e_priv *priv);

void mlx5e_handle_rx_cqe_rep(struct mlx5e_rq *rq, struct mlx5_cqe64 *cqe);

int mlx5e_rep_encap_entry_attach(struct mlx5e_priv *priv,
				 struct mlx5e_encap_entry *e);
void mlx5e_rep_encap_entry_detach(struct mlx5e_priv *priv,
				  struct mlx5e_encap_entry *e);

void mlx5e_rep_queue_neigh_stats_work(struct mlx5e_priv *priv);

bool mlx5e_eswitch_rep(struct net_device *netdev);

#else /* CONFIG_MLX5_ESWITCH */
static inline bool mlx5e_is_uplink_rep(struct mlx5e_priv *priv) { return false; }
static inline int mlx5e_add_sqs_fwd_rules(struct mlx5e_priv *priv) { return 0; }
static inline void mlx5e_remove_sqs_fwd_rules(struct mlx5e_priv *priv) {}
#endif

static inline bool mlx5e_is_vport_rep(struct mlx5e_priv *priv)
{
	return (MLX5_ESWITCH_MANAGER(priv->mdev) && priv->ppriv);
}
#endif /* __MLX5E_REP_H__ */
