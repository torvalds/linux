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
#include <linux/mutex.h>
#include "eswitch.h"
#include "en.h"
#include "lib/port_tun.h"

#ifdef CONFIG_MLX5_ESWITCH
extern const struct mlx5e_rx_handlers mlx5e_rx_handlers_rep;

struct mlx5e_neigh_update_table {
	struct rhashtable       neigh_ht;
	/* Save the neigh hash entries in a list in addition to the hash table
	 * (neigh_ht). In order to iterate easily over the neigh entries.
	 * Used for stats query.
	 */
	struct list_head	neigh_list;
	/* protect lookup/remove operations */
	struct mutex		encap_lock;
	struct notifier_block   netevent_nb;
	struct delayed_work     neigh_stats_work;
	unsigned long           min_interval; /* jiffies */
};

struct mlx5_tc_ct_priv;
struct mlx5e_rep_bond;
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
	 */
	struct list_head	    tc_indr_block_priv_list;

	struct mlx5_tun_entropy tun_entropy;

	/* protects unready_flows */
	struct mutex                unready_flows_lock;
	struct list_head            unready_flows;
	struct work_struct          reoffload_flows_work;

	/* maps tun_info to a unique id*/
	struct mapping_ctx *tunnel_mapping;
	/* maps tun_enc_opts to a unique id*/
	struct mapping_ctx *tunnel_enc_opts_mapping;

	struct mlx5_tc_ct_priv *ct_priv;

	/* support eswitch vports bonding */
	struct mlx5e_rep_bond *bond;
};

struct mlx5e_rep_priv {
	struct mlx5_eswitch_rep *rep;
	struct mlx5e_neigh_update_table neigh_update;
	struct net_device      *netdev;
	struct mlx5_flow_table *root_ft;
	struct mlx5_flow_handle *vport_rx_rule;
	struct list_head       vport_sqs_list;
	struct mlx5_rep_uplink_priv uplink_priv; /* valid for uplink rep */
	struct rtnl_link_stats64 prev_vf_vport_stats;
};

static inline
struct mlx5e_rep_priv *mlx5e_rep_to_rep_priv(struct mlx5_eswitch_rep *rep)
{
	return rep->rep_data[REP_ETH].priv;
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
	struct mlx5e_priv *priv;

	/* Save the neigh hash entry in a list on the representor in
	 * addition to the hash table. In order to iterate easily over the
	 * neighbour entries. Used for stats query.
	 */
	struct list_head neigh_list;

	/* protects encap list */
	spinlock_t encap_list_lock;
	/* encap list sharing the same neigh */
	struct list_head encap_list;

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

	struct rcu_head rcu;
};

enum {
	/* set when the encap entry is successfully offloaded into HW */
	MLX5_ENCAP_ENTRY_VALID     = BIT(0),
	MLX5_REFORMAT_DECAP        = BIT(1),
};

struct mlx5e_decap_key {
	struct ethhdr key;
};

struct mlx5e_decap_entry {
	struct mlx5e_decap_key key;
	struct list_head flows;
	struct hlist_node hlist;
	refcount_t refcnt;
	struct completion res_ready;
	int compl_result;
	struct mlx5_pkt_reformat *pkt_reformat;
	struct rcu_head rcu;
};

struct mlx5e_encap_entry {
	/* attached neigh hash entry */
	struct mlx5e_neigh_hash_entry *nhe;
	/* neigh hash entry list of encaps sharing the same neigh */
	struct list_head encap_list;
	struct mlx5e_neigh m_neigh;
	/* a node of the eswitch encap hash table which keeping all the encap
	 * entries
	 */
	struct hlist_node encap_hlist;
	struct list_head flows;
	struct mlx5_pkt_reformat *pkt_reformat;
	const struct ip_tunnel_info *tun_info;
	unsigned char h_dest[ETH_ALEN];	/* destination eth addr	*/

	struct net_device *out_dev;
	int route_dev_ifindex;
	struct mlx5e_tc_tunnel *tunnel;
	int reformat_type;
	u8 flags;
	char *encap_header;
	int encap_size;
	refcount_t refcnt;
	struct completion res_ready;
	int compl_result;
	struct rcu_head rcu;
};

struct mlx5e_rep_sq {
	struct mlx5_flow_handle	*send_to_vport_rule;
	struct list_head	 list;
};

void mlx5e_rep_register_vport_reps(struct mlx5_core_dev *mdev);
void mlx5e_rep_unregister_vport_reps(struct mlx5_core_dev *mdev);
int mlx5e_rep_bond_init(struct mlx5e_rep_priv *rpriv);
void mlx5e_rep_bond_cleanup(struct mlx5e_rep_priv *rpriv);
int mlx5e_rep_bond_enslave(struct mlx5_eswitch *esw, struct net_device *netdev,
			   struct net_device *lag_dev);
void mlx5e_rep_bond_unslave(struct mlx5_eswitch *esw,
			    const struct net_device *netdev,
			    const struct net_device *lag_dev);
int mlx5e_rep_bond_update(struct mlx5e_priv *priv, bool cleanup);

bool mlx5e_is_uplink_rep(struct mlx5e_priv *priv);
int mlx5e_add_sqs_fwd_rules(struct mlx5e_priv *priv);
void mlx5e_remove_sqs_fwd_rules(struct mlx5e_priv *priv);

void mlx5e_rep_queue_neigh_stats_work(struct mlx5e_priv *priv);

bool mlx5e_eswitch_vf_rep(struct net_device *netdev);
bool mlx5e_eswitch_uplink_rep(struct net_device *netdev);
static inline bool mlx5e_eswitch_rep(struct net_device *netdev)
{
	return mlx5e_eswitch_vf_rep(netdev) ||
	       mlx5e_eswitch_uplink_rep(netdev);
}

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
