// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021 Mellanox Technologies. */

#include <net/fib_notifier.h>
#include "tc_tun_encap.h"
#include "en_tc.h"
#include "tc_tun.h"
#include "rep/tc.h"
#include "diag/en_tc_tracepoint.h"

enum {
	MLX5E_ROUTE_ENTRY_VALID     = BIT(0),
};

struct mlx5e_route_key {
	int ip_version;
	union {
		__be32 v4;
		struct in6_addr v6;
	} endpoint_ip;
};

struct mlx5e_route_entry {
	struct mlx5e_route_key key;
	struct list_head encap_entries;
	struct list_head decap_flows;
	u32 flags;
	struct hlist_node hlist;
	refcount_t refcnt;
	int tunnel_dev_index;
	struct rcu_head rcu;
};

struct mlx5e_tc_tun_encap {
	struct mlx5e_priv *priv;
	struct notifier_block fib_nb;
	spinlock_t route_lock; /* protects route_tbl */
	unsigned long route_tbl_last_update;
	DECLARE_HASHTABLE(route_tbl, 8);
};

static bool mlx5e_route_entry_valid(struct mlx5e_route_entry *r)
{
	return r->flags & MLX5E_ROUTE_ENTRY_VALID;
}

int mlx5e_tc_set_attr_rx_tun(struct mlx5e_tc_flow *flow,
			     struct mlx5_flow_spec *spec)
{
	struct mlx5_esw_flow_attr *esw_attr = flow->attr->esw_attr;
	struct mlx5_rx_tun_attr *tun_attr;
	void *daddr, *saddr;
	u8 ip_version;

	tun_attr = kvzalloc(sizeof(*tun_attr), GFP_KERNEL);
	if (!tun_attr)
		return -ENOMEM;

	esw_attr->rx_tun_attr = tun_attr;
	ip_version = mlx5e_tc_get_ip_version(spec, true);

	if (ip_version == 4) {
		daddr = MLX5_ADDR_OF(fte_match_param, spec->match_value,
				     outer_headers.dst_ipv4_dst_ipv6.ipv4_layout.ipv4);
		saddr = MLX5_ADDR_OF(fte_match_param, spec->match_value,
				     outer_headers.src_ipv4_src_ipv6.ipv4_layout.ipv4);
		tun_attr->dst_ip.v4 = *(__be32 *)daddr;
		tun_attr->src_ip.v4 = *(__be32 *)saddr;
		if (!tun_attr->dst_ip.v4 || !tun_attr->src_ip.v4)
			return 0;
	}
#if IS_ENABLED(CONFIG_INET) && IS_ENABLED(CONFIG_IPV6)
	else if (ip_version == 6) {
		int ipv6_size = MLX5_FLD_SZ_BYTES(ipv6_layout, ipv6);
		struct in6_addr zerov6 = {};

		daddr = MLX5_ADDR_OF(fte_match_param, spec->match_value,
				     outer_headers.dst_ipv4_dst_ipv6.ipv6_layout.ipv6);
		saddr = MLX5_ADDR_OF(fte_match_param, spec->match_value,
				     outer_headers.src_ipv4_src_ipv6.ipv6_layout.ipv6);
		memcpy(&tun_attr->dst_ip.v6, daddr, ipv6_size);
		memcpy(&tun_attr->src_ip.v6, saddr, ipv6_size);
		if (!memcmp(&tun_attr->dst_ip.v6, &zerov6, sizeof(zerov6)) ||
		    !memcmp(&tun_attr->src_ip.v6, &zerov6, sizeof(zerov6)))
			return 0;
	}
#endif
	/* Only set the flag if both src and dst ip addresses exist. They are
	 * required to establish routing.
	 */
	flow_flag_set(flow, TUN_RX);
	return 0;
}

static bool mlx5e_tc_flow_all_encaps_valid(struct mlx5_esw_flow_attr *esw_attr)
{
	bool all_flow_encaps_valid = true;
	int i;

	/* Flow can be associated with multiple encap entries.
	 * Before offloading the flow verify that all of them have
	 * a valid neighbour.
	 */
	for (i = 0; i < MLX5_MAX_FLOW_FWD_VPORTS; i++) {
		if (!(esw_attr->dests[i].flags & MLX5_ESW_DEST_ENCAP))
			continue;
		if (!(esw_attr->dests[i].flags & MLX5_ESW_DEST_ENCAP_VALID)) {
			all_flow_encaps_valid = false;
			break;
		}
	}

	return all_flow_encaps_valid;
}

void mlx5e_tc_encap_flows_add(struct mlx5e_priv *priv,
			      struct mlx5e_encap_entry *e,
			      struct list_head *flow_list)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5_esw_flow_attr *esw_attr;
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_attr *attr;
	struct mlx5_flow_spec *spec;
	struct mlx5e_tc_flow *flow;
	int err;

	if (e->flags & MLX5_ENCAP_ENTRY_NO_ROUTE)
		return;

	e->pkt_reformat = mlx5_packet_reformat_alloc(priv->mdev,
						     e->reformat_type,
						     e->encap_size, e->encap_header,
						     MLX5_FLOW_NAMESPACE_FDB);
	if (IS_ERR(e->pkt_reformat)) {
		mlx5_core_warn(priv->mdev, "Failed to offload cached encapsulation header, %lu\n",
			       PTR_ERR(e->pkt_reformat));
		return;
	}
	e->flags |= MLX5_ENCAP_ENTRY_VALID;
	mlx5e_rep_queue_neigh_stats_work(priv);

	list_for_each_entry(flow, flow_list, tmp_list) {
		if (!mlx5e_is_offloaded_flow(flow))
			continue;
		attr = flow->attr;
		esw_attr = attr->esw_attr;
		spec = &attr->parse_attr->spec;

		esw_attr->dests[flow->tmp_entry_index].pkt_reformat = e->pkt_reformat;
		esw_attr->dests[flow->tmp_entry_index].flags |= MLX5_ESW_DEST_ENCAP_VALID;

		/* Do not offload flows with unresolved neighbors */
		if (!mlx5e_tc_flow_all_encaps_valid(esw_attr))
			continue;
		/* update from slow path rule to encap rule */
		rule = mlx5e_tc_offload_fdb_rules(esw, flow, spec, attr);
		if (IS_ERR(rule)) {
			err = PTR_ERR(rule);
			mlx5_core_warn(priv->mdev, "Failed to update cached encapsulation flow, %d\n",
				       err);
			continue;
		}

		mlx5e_tc_unoffload_from_slow_path(esw, flow);
		flow->rule[0] = rule;
		/* was unset when slow path rule removed */
		flow_flag_set(flow, OFFLOADED);
	}
}

void mlx5e_tc_encap_flows_del(struct mlx5e_priv *priv,
			      struct mlx5e_encap_entry *e,
			      struct list_head *flow_list)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5_esw_flow_attr *esw_attr;
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_attr *attr;
	struct mlx5_flow_spec *spec;
	struct mlx5e_tc_flow *flow;
	int err;

	list_for_each_entry(flow, flow_list, tmp_list) {
		if (!mlx5e_is_offloaded_flow(flow))
			continue;
		attr = flow->attr;
		esw_attr = attr->esw_attr;
		spec = &attr->parse_attr->spec;

		/* update from encap rule to slow path rule */
		rule = mlx5e_tc_offload_to_slow_path(esw, flow, spec);
		/* mark the flow's encap dest as non-valid */
		esw_attr->dests[flow->tmp_entry_index].flags &= ~MLX5_ESW_DEST_ENCAP_VALID;

		if (IS_ERR(rule)) {
			err = PTR_ERR(rule);
			mlx5_core_warn(priv->mdev, "Failed to update slow path (encap) flow, %d\n",
				       err);
			continue;
		}

		mlx5e_tc_unoffload_fdb_rules(esw, flow, attr);
		flow->rule[0] = rule;
		/* was unset when fast path rule removed */
		flow_flag_set(flow, OFFLOADED);
	}

	/* we know that the encap is valid */
	e->flags &= ~MLX5_ENCAP_ENTRY_VALID;
	mlx5_packet_reformat_dealloc(priv->mdev, e->pkt_reformat);
}

static void mlx5e_take_tmp_flow(struct mlx5e_tc_flow *flow,
				struct list_head *flow_list,
				int index)
{
	if (IS_ERR(mlx5e_flow_get(flow)))
		return;
	wait_for_completion(&flow->init_done);

	flow->tmp_entry_index = index;
	list_add(&flow->tmp_list, flow_list);
}

/* Takes reference to all flows attached to encap and adds the flows to
 * flow_list using 'tmp_list' list_head in mlx5e_tc_flow.
 */
void mlx5e_take_all_encap_flows(struct mlx5e_encap_entry *e, struct list_head *flow_list)
{
	struct encap_flow_item *efi;
	struct mlx5e_tc_flow *flow;

	list_for_each_entry(efi, &e->flows, list) {
		flow = container_of(efi, struct mlx5e_tc_flow, encaps[efi->index]);
		mlx5e_take_tmp_flow(flow, flow_list, efi->index);
	}
}

/* Takes reference to all flows attached to route and adds the flows to
 * flow_list using 'tmp_list' list_head in mlx5e_tc_flow.
 */
static void mlx5e_take_all_route_decap_flows(struct mlx5e_route_entry *r,
					     struct list_head *flow_list)
{
	struct mlx5e_tc_flow *flow;

	list_for_each_entry(flow, &r->decap_flows, decap_routes)
		mlx5e_take_tmp_flow(flow, flow_list, 0);
}

static struct mlx5e_encap_entry *
mlx5e_get_next_valid_encap(struct mlx5e_neigh_hash_entry *nhe,
			   struct mlx5e_encap_entry *e)
{
	struct mlx5e_encap_entry *next = NULL;

retry:
	rcu_read_lock();

	/* find encap with non-zero reference counter value */
	for (next = e ?
		     list_next_or_null_rcu(&nhe->encap_list,
					   &e->encap_list,
					   struct mlx5e_encap_entry,
					   encap_list) :
		     list_first_or_null_rcu(&nhe->encap_list,
					    struct mlx5e_encap_entry,
					    encap_list);
	     next;
	     next = list_next_or_null_rcu(&nhe->encap_list,
					  &next->encap_list,
					  struct mlx5e_encap_entry,
					  encap_list))
		if (mlx5e_encap_take(next))
			break;

	rcu_read_unlock();

	/* release starting encap */
	if (e)
		mlx5e_encap_put(netdev_priv(e->out_dev), e);
	if (!next)
		return next;

	/* wait for encap to be fully initialized */
	wait_for_completion(&next->res_ready);
	/* continue searching if encap entry is not in valid state after completion */
	if (!(next->flags & MLX5_ENCAP_ENTRY_VALID)) {
		e = next;
		goto retry;
	}

	return next;
}

void mlx5e_tc_update_neigh_used_value(struct mlx5e_neigh_hash_entry *nhe)
{
	struct mlx5e_neigh *m_neigh = &nhe->m_neigh;
	struct mlx5e_encap_entry *e = NULL;
	struct mlx5e_tc_flow *flow;
	struct mlx5_fc *counter;
	struct neigh_table *tbl;
	bool neigh_used = false;
	struct neighbour *n;
	u64 lastuse;

	if (m_neigh->family == AF_INET)
		tbl = &arp_tbl;
#if IS_ENABLED(CONFIG_IPV6)
	else if (m_neigh->family == AF_INET6)
		tbl = ipv6_stub->nd_tbl;
#endif
	else
		return;

	/* mlx5e_get_next_valid_encap() releases previous encap before returning
	 * next one.
	 */
	while ((e = mlx5e_get_next_valid_encap(nhe, e)) != NULL) {
		struct mlx5e_priv *priv = netdev_priv(e->out_dev);
		struct encap_flow_item *efi, *tmp;
		struct mlx5_eswitch *esw;
		LIST_HEAD(flow_list);

		esw = priv->mdev->priv.eswitch;
		mutex_lock(&esw->offloads.encap_tbl_lock);
		list_for_each_entry_safe(efi, tmp, &e->flows, list) {
			flow = container_of(efi, struct mlx5e_tc_flow,
					    encaps[efi->index]);
			if (IS_ERR(mlx5e_flow_get(flow)))
				continue;
			list_add(&flow->tmp_list, &flow_list);

			if (mlx5e_is_offloaded_flow(flow)) {
				counter = mlx5e_tc_get_counter(flow);
				lastuse = mlx5_fc_query_lastuse(counter);
				if (time_after((unsigned long)lastuse, nhe->reported_lastuse)) {
					neigh_used = true;
					break;
				}
			}
		}
		mutex_unlock(&esw->offloads.encap_tbl_lock);

		mlx5e_put_flow_list(priv, &flow_list);
		if (neigh_used) {
			/* release current encap before breaking the loop */
			mlx5e_encap_put(priv, e);
			break;
		}
	}

	trace_mlx5e_tc_update_neigh_used_value(nhe, neigh_used);

	if (neigh_used) {
		nhe->reported_lastuse = jiffies;

		/* find the relevant neigh according to the cached device and
		 * dst ip pair
		 */
		n = neigh_lookup(tbl, &m_neigh->dst_ip, READ_ONCE(nhe->neigh_dev));
		if (!n)
			return;

		neigh_event_send(n, NULL);
		neigh_release(n);
	}
}

static void mlx5e_encap_dealloc(struct mlx5e_priv *priv, struct mlx5e_encap_entry *e)
{
	WARN_ON(!list_empty(&e->flows));

	if (e->compl_result > 0) {
		mlx5e_rep_encap_entry_detach(netdev_priv(e->out_dev), e);

		if (e->flags & MLX5_ENCAP_ENTRY_VALID)
			mlx5_packet_reformat_dealloc(priv->mdev, e->pkt_reformat);
	}

	kfree(e->tun_info);
	kfree(e->encap_header);
	kfree_rcu(e, rcu);
}

static void mlx5e_decap_dealloc(struct mlx5e_priv *priv,
				struct mlx5e_decap_entry *d)
{
	WARN_ON(!list_empty(&d->flows));

	if (!d->compl_result)
		mlx5_packet_reformat_dealloc(priv->mdev, d->pkt_reformat);

	kfree_rcu(d, rcu);
}

void mlx5e_encap_put(struct mlx5e_priv *priv, struct mlx5e_encap_entry *e)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;

	if (!refcount_dec_and_mutex_lock(&e->refcnt, &esw->offloads.encap_tbl_lock))
		return;
	list_del(&e->route_list);
	hash_del_rcu(&e->encap_hlist);
	mutex_unlock(&esw->offloads.encap_tbl_lock);

	mlx5e_encap_dealloc(priv, e);
}

static void mlx5e_decap_put(struct mlx5e_priv *priv, struct mlx5e_decap_entry *d)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;

	if (!refcount_dec_and_mutex_lock(&d->refcnt, &esw->offloads.decap_tbl_lock))
		return;
	hash_del_rcu(&d->hlist);
	mutex_unlock(&esw->offloads.decap_tbl_lock);

	mlx5e_decap_dealloc(priv, d);
}

static void mlx5e_detach_encap_route(struct mlx5e_priv *priv,
				     struct mlx5e_tc_flow *flow,
				     int out_index);

void mlx5e_detach_encap(struct mlx5e_priv *priv,
			struct mlx5e_tc_flow *flow, int out_index)
{
	struct mlx5e_encap_entry *e = flow->encaps[out_index].e;
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;

	if (flow->attr->esw_attr->dests[out_index].flags &
	    MLX5_ESW_DEST_CHAIN_WITH_SRC_PORT_CHANGE)
		mlx5e_detach_encap_route(priv, flow, out_index);

	/* flow wasn't fully initialized */
	if (!e)
		return;

	mutex_lock(&esw->offloads.encap_tbl_lock);
	list_del(&flow->encaps[out_index].list);
	flow->encaps[out_index].e = NULL;
	if (!refcount_dec_and_test(&e->refcnt)) {
		mutex_unlock(&esw->offloads.encap_tbl_lock);
		return;
	}
	list_del(&e->route_list);
	hash_del_rcu(&e->encap_hlist);
	mutex_unlock(&esw->offloads.encap_tbl_lock);

	mlx5e_encap_dealloc(priv, e);
}

void mlx5e_detach_decap(struct mlx5e_priv *priv,
			struct mlx5e_tc_flow *flow)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_decap_entry *d = flow->decap_reformat;

	if (!d)
		return;

	mutex_lock(&esw->offloads.decap_tbl_lock);
	list_del(&flow->l3_to_l2_reformat);
	flow->decap_reformat = NULL;

	if (!refcount_dec_and_test(&d->refcnt)) {
		mutex_unlock(&esw->offloads.decap_tbl_lock);
		return;
	}
	hash_del_rcu(&d->hlist);
	mutex_unlock(&esw->offloads.decap_tbl_lock);

	mlx5e_decap_dealloc(priv, d);
}

struct encap_key {
	const struct ip_tunnel_key *ip_tun_key;
	struct mlx5e_tc_tunnel *tc_tunnel;
};

static int cmp_encap_info(struct encap_key *a,
			  struct encap_key *b)
{
	return memcmp(a->ip_tun_key, b->ip_tun_key, sizeof(*a->ip_tun_key)) ||
		a->tc_tunnel->tunnel_type != b->tc_tunnel->tunnel_type;
}

static int cmp_decap_info(struct mlx5e_decap_key *a,
			  struct mlx5e_decap_key *b)
{
	return memcmp(&a->key, &b->key, sizeof(b->key));
}

static int hash_encap_info(struct encap_key *key)
{
	return jhash(key->ip_tun_key, sizeof(*key->ip_tun_key),
		     key->tc_tunnel->tunnel_type);
}

static int hash_decap_info(struct mlx5e_decap_key *key)
{
	return jhash(&key->key, sizeof(key->key), 0);
}

bool mlx5e_encap_take(struct mlx5e_encap_entry *e)
{
	return refcount_inc_not_zero(&e->refcnt);
}

static bool mlx5e_decap_take(struct mlx5e_decap_entry *e)
{
	return refcount_inc_not_zero(&e->refcnt);
}

static struct mlx5e_encap_entry *
mlx5e_encap_get(struct mlx5e_priv *priv, struct encap_key *key,
		uintptr_t hash_key)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_encap_entry *e;
	struct encap_key e_key;

	hash_for_each_possible_rcu(esw->offloads.encap_tbl, e,
				   encap_hlist, hash_key) {
		e_key.ip_tun_key = &e->tun_info->key;
		e_key.tc_tunnel = e->tunnel;
		if (!cmp_encap_info(&e_key, key) &&
		    mlx5e_encap_take(e))
			return e;
	}

	return NULL;
}

static struct mlx5e_decap_entry *
mlx5e_decap_get(struct mlx5e_priv *priv, struct mlx5e_decap_key *key,
		uintptr_t hash_key)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_decap_key r_key;
	struct mlx5e_decap_entry *e;

	hash_for_each_possible_rcu(esw->offloads.decap_tbl, e,
				   hlist, hash_key) {
		r_key = e->key;
		if (!cmp_decap_info(&r_key, key) &&
		    mlx5e_decap_take(e))
			return e;
	}
	return NULL;
}

struct ip_tunnel_info *mlx5e_dup_tun_info(const struct ip_tunnel_info *tun_info)
{
	size_t tun_size = sizeof(*tun_info) + tun_info->options_len;

	return kmemdup(tun_info, tun_size, GFP_KERNEL);
}

static bool is_duplicated_encap_entry(struct mlx5e_priv *priv,
				      struct mlx5e_tc_flow *flow,
				      int out_index,
				      struct mlx5e_encap_entry *e,
				      struct netlink_ext_ack *extack)
{
	int i;

	for (i = 0; i < out_index; i++) {
		if (flow->encaps[i].e != e)
			continue;
		NL_SET_ERR_MSG_MOD(extack, "can't duplicate encap action");
		netdev_err(priv->netdev, "can't duplicate encap action\n");
		return true;
	}

	return false;
}

static int mlx5e_set_vf_tunnel(struct mlx5_eswitch *esw,
			       struct mlx5_flow_attr *attr,
			       struct mlx5e_tc_mod_hdr_acts *mod_hdr_acts,
			       struct net_device *out_dev,
			       int route_dev_ifindex,
			       int out_index)
{
	struct mlx5_esw_flow_attr *esw_attr = attr->esw_attr;
	struct net_device *route_dev;
	u16 vport_num;
	int err = 0;
	u32 data;

	route_dev = dev_get_by_index(dev_net(out_dev), route_dev_ifindex);

	if (!route_dev || route_dev->netdev_ops != &mlx5e_netdev_ops ||
	    !mlx5e_tc_is_vf_tunnel(out_dev, route_dev))
		goto out;

	err = mlx5e_tc_query_route_vport(out_dev, route_dev, &vport_num);
	if (err)
		goto out;

	attr->dest_chain = 0;
	attr->action |= MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;
	esw_attr->dests[out_index].flags |= MLX5_ESW_DEST_CHAIN_WITH_SRC_PORT_CHANGE;
	data = mlx5_eswitch_get_vport_metadata_for_set(esw_attr->in_mdev->priv.eswitch,
						       vport_num);
	err = mlx5e_tc_match_to_reg_set_and_get_id(esw->dev, mod_hdr_acts,
						   MLX5_FLOW_NAMESPACE_FDB,
						   VPORT_TO_REG, data);
	if (err >= 0) {
		esw_attr->dests[out_index].src_port_rewrite_act_id = err;
		err = 0;
	}

out:
	if (route_dev)
		dev_put(route_dev);
	return err;
}

static int mlx5e_update_vf_tunnel(struct mlx5_eswitch *esw,
				  struct mlx5_esw_flow_attr *attr,
				  struct mlx5e_tc_mod_hdr_acts *mod_hdr_acts,
				  struct net_device *out_dev,
				  int route_dev_ifindex,
				  int out_index)
{
	int act_id = attr->dests[out_index].src_port_rewrite_act_id;
	struct net_device *route_dev;
	u16 vport_num;
	int err = 0;
	u32 data;

	route_dev = dev_get_by_index(dev_net(out_dev), route_dev_ifindex);

	if (!route_dev || route_dev->netdev_ops != &mlx5e_netdev_ops ||
	    !mlx5e_tc_is_vf_tunnel(out_dev, route_dev)) {
		err = -ENODEV;
		goto out;
	}

	err = mlx5e_tc_query_route_vport(out_dev, route_dev, &vport_num);
	if (err)
		goto out;

	data = mlx5_eswitch_get_vport_metadata_for_set(attr->in_mdev->priv.eswitch,
						       vport_num);
	mlx5e_tc_match_to_reg_mod_hdr_change(esw->dev, mod_hdr_acts, VPORT_TO_REG, act_id, data);

out:
	if (route_dev)
		dev_put(route_dev);
	return err;
}

static unsigned int mlx5e_route_tbl_get_last_update(struct mlx5e_priv *priv)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5_rep_uplink_priv *uplink_priv;
	struct mlx5e_rep_priv *uplink_rpriv;
	struct mlx5e_tc_tun_encap *encap;
	unsigned int ret;

	uplink_rpriv = mlx5_eswitch_get_uplink_priv(esw, REP_ETH);
	uplink_priv = &uplink_rpriv->uplink_priv;
	encap = uplink_priv->encap;

	spin_lock_bh(&encap->route_lock);
	ret = encap->route_tbl_last_update;
	spin_unlock_bh(&encap->route_lock);
	return ret;
}

static int mlx5e_attach_encap_route(struct mlx5e_priv *priv,
				    struct mlx5e_tc_flow *flow,
				    struct mlx5e_encap_entry *e,
				    bool new_encap_entry,
				    unsigned long tbl_time_before,
				    int out_index);

int mlx5e_attach_encap(struct mlx5e_priv *priv,
		       struct mlx5e_tc_flow *flow,
		       struct net_device *mirred_dev,
		       int out_index,
		       struct netlink_ext_ack *extack,
		       struct net_device **encap_dev,
		       bool *encap_valid)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_tc_flow_parse_attr *parse_attr;
	struct mlx5_flow_attr *attr = flow->attr;
	const struct ip_tunnel_info *tun_info;
	unsigned long tbl_time_before = 0;
	struct encap_key key;
	struct mlx5e_encap_entry *e;
	bool entry_created = false;
	unsigned short family;
	uintptr_t hash_key;
	int err = 0;

	parse_attr = attr->parse_attr;
	tun_info = parse_attr->tun_info[out_index];
	family = ip_tunnel_info_af(tun_info);
	key.ip_tun_key = &tun_info->key;
	key.tc_tunnel = mlx5e_get_tc_tun(mirred_dev);
	if (!key.tc_tunnel) {
		NL_SET_ERR_MSG_MOD(extack, "Unsupported tunnel");
		return -EOPNOTSUPP;
	}

	hash_key = hash_encap_info(&key);

	mutex_lock(&esw->offloads.encap_tbl_lock);
	e = mlx5e_encap_get(priv, &key, hash_key);

	/* must verify if encap is valid or not */
	if (e) {
		/* Check that entry was not already attached to this flow */
		if (is_duplicated_encap_entry(priv, flow, out_index, e, extack)) {
			err = -EOPNOTSUPP;
			goto out_err;
		}

		mutex_unlock(&esw->offloads.encap_tbl_lock);
		wait_for_completion(&e->res_ready);

		/* Protect against concurrent neigh update. */
		mutex_lock(&esw->offloads.encap_tbl_lock);
		if (e->compl_result < 0) {
			err = -EREMOTEIO;
			goto out_err;
		}
		goto attach_flow;
	}

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e) {
		err = -ENOMEM;
		goto out_err;
	}

	refcount_set(&e->refcnt, 1);
	init_completion(&e->res_ready);
	entry_created = true;
	INIT_LIST_HEAD(&e->route_list);

	tun_info = mlx5e_dup_tun_info(tun_info);
	if (!tun_info) {
		err = -ENOMEM;
		goto out_err_init;
	}
	e->tun_info = tun_info;
	err = mlx5e_tc_tun_init_encap_attr(mirred_dev, priv, e, extack);
	if (err)
		goto out_err_init;

	INIT_LIST_HEAD(&e->flows);
	hash_add_rcu(esw->offloads.encap_tbl, &e->encap_hlist, hash_key);
	tbl_time_before = mlx5e_route_tbl_get_last_update(priv);
	mutex_unlock(&esw->offloads.encap_tbl_lock);

	if (family == AF_INET)
		err = mlx5e_tc_tun_create_header_ipv4(priv, mirred_dev, e);
	else if (family == AF_INET6)
		err = mlx5e_tc_tun_create_header_ipv6(priv, mirred_dev, e);

	/* Protect against concurrent neigh update. */
	mutex_lock(&esw->offloads.encap_tbl_lock);
	complete_all(&e->res_ready);
	if (err) {
		e->compl_result = err;
		goto out_err;
	}
	e->compl_result = 1;

attach_flow:
	err = mlx5e_attach_encap_route(priv, flow, e, entry_created, tbl_time_before,
				       out_index);
	if (err)
		goto out_err;

	flow->encaps[out_index].e = e;
	list_add(&flow->encaps[out_index].list, &e->flows);
	flow->encaps[out_index].index = out_index;
	*encap_dev = e->out_dev;
	if (e->flags & MLX5_ENCAP_ENTRY_VALID) {
		attr->esw_attr->dests[out_index].pkt_reformat = e->pkt_reformat;
		attr->esw_attr->dests[out_index].flags |= MLX5_ESW_DEST_ENCAP_VALID;
		*encap_valid = true;
	} else {
		*encap_valid = false;
	}
	mutex_unlock(&esw->offloads.encap_tbl_lock);

	return err;

out_err:
	mutex_unlock(&esw->offloads.encap_tbl_lock);
	if (e)
		mlx5e_encap_put(priv, e);
	return err;

out_err_init:
	mutex_unlock(&esw->offloads.encap_tbl_lock);
	kfree(tun_info);
	kfree(e);
	return err;
}

int mlx5e_attach_decap(struct mlx5e_priv *priv,
		       struct mlx5e_tc_flow *flow,
		       struct netlink_ext_ack *extack)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5_esw_flow_attr *attr = flow->attr->esw_attr;
	struct mlx5e_tc_flow_parse_attr *parse_attr;
	struct mlx5e_decap_entry *d;
	struct mlx5e_decap_key key;
	uintptr_t hash_key;
	int err = 0;

	parse_attr = flow->attr->parse_attr;
	if (sizeof(parse_attr->eth) > MLX5_CAP_ESW(priv->mdev, max_encap_header_size)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "encap header larger than max supported");
		return -EOPNOTSUPP;
	}

	key.key = parse_attr->eth;
	hash_key = hash_decap_info(&key);
	mutex_lock(&esw->offloads.decap_tbl_lock);
	d = mlx5e_decap_get(priv, &key, hash_key);
	if (d) {
		mutex_unlock(&esw->offloads.decap_tbl_lock);
		wait_for_completion(&d->res_ready);
		mutex_lock(&esw->offloads.decap_tbl_lock);
		if (d->compl_result) {
			err = -EREMOTEIO;
			goto out_free;
		}
		goto found;
	}

	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (!d) {
		err = -ENOMEM;
		goto out_err;
	}

	d->key = key;
	refcount_set(&d->refcnt, 1);
	init_completion(&d->res_ready);
	INIT_LIST_HEAD(&d->flows);
	hash_add_rcu(esw->offloads.decap_tbl, &d->hlist, hash_key);
	mutex_unlock(&esw->offloads.decap_tbl_lock);

	d->pkt_reformat = mlx5_packet_reformat_alloc(priv->mdev,
						     MLX5_REFORMAT_TYPE_L3_TUNNEL_TO_L2,
						     sizeof(parse_attr->eth),
						     &parse_attr->eth,
						     MLX5_FLOW_NAMESPACE_FDB);
	if (IS_ERR(d->pkt_reformat)) {
		err = PTR_ERR(d->pkt_reformat);
		d->compl_result = err;
	}
	mutex_lock(&esw->offloads.decap_tbl_lock);
	complete_all(&d->res_ready);
	if (err)
		goto out_free;

found:
	flow->decap_reformat = d;
	attr->decap_pkt_reformat = d->pkt_reformat;
	list_add(&flow->l3_to_l2_reformat, &d->flows);
	mutex_unlock(&esw->offloads.decap_tbl_lock);
	return 0;

out_free:
	mutex_unlock(&esw->offloads.decap_tbl_lock);
	mlx5e_decap_put(priv, d);
	return err;

out_err:
	mutex_unlock(&esw->offloads.decap_tbl_lock);
	return err;
}

static int cmp_route_info(struct mlx5e_route_key *a,
			  struct mlx5e_route_key *b)
{
	if (a->ip_version == 4 && b->ip_version == 4)
		return memcmp(&a->endpoint_ip.v4, &b->endpoint_ip.v4,
			      sizeof(a->endpoint_ip.v4));
	else if (a->ip_version == 6 && b->ip_version == 6)
		return memcmp(&a->endpoint_ip.v6, &b->endpoint_ip.v6,
			      sizeof(a->endpoint_ip.v6));
	return 1;
}

static u32 hash_route_info(struct mlx5e_route_key *key)
{
	if (key->ip_version == 4)
		return jhash(&key->endpoint_ip.v4, sizeof(key->endpoint_ip.v4), 0);
	return jhash(&key->endpoint_ip.v6, sizeof(key->endpoint_ip.v6), 0);
}

static void mlx5e_route_dealloc(struct mlx5e_priv *priv,
				struct mlx5e_route_entry *r)
{
	WARN_ON(!list_empty(&r->decap_flows));
	WARN_ON(!list_empty(&r->encap_entries));

	kfree_rcu(r, rcu);
}

static void mlx5e_route_put(struct mlx5e_priv *priv, struct mlx5e_route_entry *r)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;

	if (!refcount_dec_and_mutex_lock(&r->refcnt, &esw->offloads.encap_tbl_lock))
		return;

	hash_del_rcu(&r->hlist);
	mutex_unlock(&esw->offloads.encap_tbl_lock);

	mlx5e_route_dealloc(priv, r);
}

static void mlx5e_route_put_locked(struct mlx5e_priv *priv, struct mlx5e_route_entry *r)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;

	lockdep_assert_held(&esw->offloads.encap_tbl_lock);

	if (!refcount_dec_and_test(&r->refcnt))
		return;
	hash_del_rcu(&r->hlist);
	mlx5e_route_dealloc(priv, r);
}

static struct mlx5e_route_entry *
mlx5e_route_get(struct mlx5e_tc_tun_encap *encap, struct mlx5e_route_key *key,
		u32 hash_key)
{
	struct mlx5e_route_key r_key;
	struct mlx5e_route_entry *r;

	hash_for_each_possible(encap->route_tbl, r, hlist, hash_key) {
		r_key = r->key;
		if (!cmp_route_info(&r_key, key) &&
		    refcount_inc_not_zero(&r->refcnt))
			return r;
	}
	return NULL;
}

static struct mlx5e_route_entry *
mlx5e_route_get_create(struct mlx5e_priv *priv,
		       struct mlx5e_route_key *key,
		       int tunnel_dev_index,
		       unsigned long *route_tbl_change_time)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5_rep_uplink_priv *uplink_priv;
	struct mlx5e_rep_priv *uplink_rpriv;
	struct mlx5e_tc_tun_encap *encap;
	struct mlx5e_route_entry *r;
	u32 hash_key;

	uplink_rpriv = mlx5_eswitch_get_uplink_priv(esw, REP_ETH);
	uplink_priv = &uplink_rpriv->uplink_priv;
	encap = uplink_priv->encap;

	hash_key = hash_route_info(key);
	spin_lock_bh(&encap->route_lock);
	r = mlx5e_route_get(encap, key, hash_key);
	spin_unlock_bh(&encap->route_lock);
	if (r) {
		if (!mlx5e_route_entry_valid(r)) {
			mlx5e_route_put_locked(priv, r);
			return ERR_PTR(-EINVAL);
		}
		return r;
	}

	r = kzalloc(sizeof(*r), GFP_KERNEL);
	if (!r)
		return ERR_PTR(-ENOMEM);

	r->key = *key;
	r->flags |= MLX5E_ROUTE_ENTRY_VALID;
	r->tunnel_dev_index = tunnel_dev_index;
	refcount_set(&r->refcnt, 1);
	INIT_LIST_HEAD(&r->decap_flows);
	INIT_LIST_HEAD(&r->encap_entries);

	spin_lock_bh(&encap->route_lock);
	*route_tbl_change_time = encap->route_tbl_last_update;
	hash_add(encap->route_tbl, &r->hlist, hash_key);
	spin_unlock_bh(&encap->route_lock);

	return r;
}

static struct mlx5e_route_entry *
mlx5e_route_lookup_for_update(struct mlx5e_tc_tun_encap *encap, struct mlx5e_route_key *key)
{
	u32 hash_key = hash_route_info(key);
	struct mlx5e_route_entry *r;

	spin_lock_bh(&encap->route_lock);
	encap->route_tbl_last_update = jiffies;
	r = mlx5e_route_get(encap, key, hash_key);
	spin_unlock_bh(&encap->route_lock);

	return r;
}

struct mlx5e_tc_fib_event_data {
	struct work_struct work;
	unsigned long event;
	struct mlx5e_route_entry *r;
	struct net_device *ul_dev;
};

static void mlx5e_tc_fib_event_work(struct work_struct *work);
static struct mlx5e_tc_fib_event_data *
mlx5e_tc_init_fib_work(unsigned long event, struct net_device *ul_dev, gfp_t flags)
{
	struct mlx5e_tc_fib_event_data *fib_work;

	fib_work = kzalloc(sizeof(*fib_work), flags);
	if (WARN_ON(!fib_work))
		return NULL;

	INIT_WORK(&fib_work->work, mlx5e_tc_fib_event_work);
	fib_work->event = event;
	fib_work->ul_dev = ul_dev;

	return fib_work;
}

static int
mlx5e_route_enqueue_update(struct mlx5e_priv *priv,
			   struct mlx5e_route_entry *r,
			   unsigned long event)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_tc_fib_event_data *fib_work;
	struct mlx5e_rep_priv *uplink_rpriv;
	struct net_device *ul_dev;

	uplink_rpriv = mlx5_eswitch_get_uplink_priv(esw, REP_ETH);
	ul_dev = uplink_rpriv->netdev;

	fib_work = mlx5e_tc_init_fib_work(event, ul_dev, GFP_KERNEL);
	if (!fib_work)
		return -ENOMEM;

	dev_hold(ul_dev);
	refcount_inc(&r->refcnt);
	fib_work->r = r;
	queue_work(priv->wq, &fib_work->work);

	return 0;
}

int mlx5e_attach_decap_route(struct mlx5e_priv *priv,
			     struct mlx5e_tc_flow *flow)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	unsigned long tbl_time_before, tbl_time_after;
	struct mlx5e_tc_flow_parse_attr *parse_attr;
	struct mlx5_flow_attr *attr = flow->attr;
	struct mlx5_esw_flow_attr *esw_attr;
	struct mlx5e_route_entry *r;
	struct mlx5e_route_key key;
	int err = 0;

	esw_attr = attr->esw_attr;
	parse_attr = attr->parse_attr;
	mutex_lock(&esw->offloads.encap_tbl_lock);
	if (!esw_attr->rx_tun_attr)
		goto out;

	tbl_time_before = mlx5e_route_tbl_get_last_update(priv);
	tbl_time_after = tbl_time_before;
	err = mlx5e_tc_tun_route_lookup(priv, &parse_attr->spec, attr);
	if (err || !esw_attr->rx_tun_attr->decap_vport)
		goto out;

	key.ip_version = attr->ip_version;
	if (key.ip_version == 4)
		key.endpoint_ip.v4 = esw_attr->rx_tun_attr->dst_ip.v4;
	else
		key.endpoint_ip.v6 = esw_attr->rx_tun_attr->dst_ip.v6;

	r = mlx5e_route_get_create(priv, &key, parse_attr->filter_dev->ifindex,
				   &tbl_time_after);
	if (IS_ERR(r)) {
		err = PTR_ERR(r);
		goto out;
	}
	/* Routing changed concurrently. FIB event handler might have missed new
	 * entry, schedule update.
	 */
	if (tbl_time_before != tbl_time_after) {
		err = mlx5e_route_enqueue_update(priv, r, FIB_EVENT_ENTRY_REPLACE);
		if (err) {
			mlx5e_route_put_locked(priv, r);
			goto out;
		}
	}

	flow->decap_route = r;
	list_add(&flow->decap_routes, &r->decap_flows);
	mutex_unlock(&esw->offloads.encap_tbl_lock);
	return 0;

out:
	mutex_unlock(&esw->offloads.encap_tbl_lock);
	return err;
}

static int mlx5e_attach_encap_route(struct mlx5e_priv *priv,
				    struct mlx5e_tc_flow *flow,
				    struct mlx5e_encap_entry *e,
				    bool new_encap_entry,
				    unsigned long tbl_time_before,
				    int out_index)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	unsigned long tbl_time_after = tbl_time_before;
	struct mlx5e_tc_flow_parse_attr *parse_attr;
	struct mlx5_flow_attr *attr = flow->attr;
	const struct ip_tunnel_info *tun_info;
	struct mlx5_esw_flow_attr *esw_attr;
	struct mlx5e_route_entry *r;
	struct mlx5e_route_key key;
	unsigned short family;
	int err = 0;

	esw_attr = attr->esw_attr;
	parse_attr = attr->parse_attr;
	tun_info = parse_attr->tun_info[out_index];
	family = ip_tunnel_info_af(tun_info);

	if (family == AF_INET) {
		key.endpoint_ip.v4 = tun_info->key.u.ipv4.src;
		key.ip_version = 4;
	} else if (family == AF_INET6) {
		key.endpoint_ip.v6 = tun_info->key.u.ipv6.src;
		key.ip_version = 6;
	}

	err = mlx5e_set_vf_tunnel(esw, attr, &parse_attr->mod_hdr_acts, e->out_dev,
				  e->route_dev_ifindex, out_index);
	if (err || !(esw_attr->dests[out_index].flags &
		     MLX5_ESW_DEST_CHAIN_WITH_SRC_PORT_CHANGE))
		return err;

	r = mlx5e_route_get_create(priv, &key, parse_attr->mirred_ifindex[out_index],
				   &tbl_time_after);
	if (IS_ERR(r))
		return PTR_ERR(r);
	/* Routing changed concurrently. FIB event handler might have missed new
	 * entry, schedule update.
	 */
	if (tbl_time_before != tbl_time_after) {
		err = mlx5e_route_enqueue_update(priv, r, FIB_EVENT_ENTRY_REPLACE);
		if (err) {
			mlx5e_route_put_locked(priv, r);
			return err;
		}
	}

	flow->encap_routes[out_index].r = r;
	if (new_encap_entry)
		list_add(&e->route_list, &r->encap_entries);
	flow->encap_routes[out_index].index = out_index;
	return 0;
}

void mlx5e_detach_decap_route(struct mlx5e_priv *priv,
			      struct mlx5e_tc_flow *flow)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_route_entry *r = flow->decap_route;

	if (!r)
		return;

	mutex_lock(&esw->offloads.encap_tbl_lock);
	list_del(&flow->decap_routes);
	flow->decap_route = NULL;

	if (!refcount_dec_and_test(&r->refcnt)) {
		mutex_unlock(&esw->offloads.encap_tbl_lock);
		return;
	}
	hash_del_rcu(&r->hlist);
	mutex_unlock(&esw->offloads.encap_tbl_lock);

	mlx5e_route_dealloc(priv, r);
}

static void mlx5e_detach_encap_route(struct mlx5e_priv *priv,
				     struct mlx5e_tc_flow *flow,
				     int out_index)
{
	struct mlx5e_route_entry *r = flow->encap_routes[out_index].r;
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_encap_entry *e, *tmp;

	if (!r)
		return;

	mutex_lock(&esw->offloads.encap_tbl_lock);
	flow->encap_routes[out_index].r = NULL;

	if (!refcount_dec_and_test(&r->refcnt)) {
		mutex_unlock(&esw->offloads.encap_tbl_lock);
		return;
	}
	list_for_each_entry_safe(e, tmp, &r->encap_entries, route_list)
		list_del_init(&e->route_list);
	hash_del_rcu(&r->hlist);
	mutex_unlock(&esw->offloads.encap_tbl_lock);

	mlx5e_route_dealloc(priv, r);
}

static void mlx5e_invalidate_encap(struct mlx5e_priv *priv,
				   struct mlx5e_encap_entry *e,
				   struct list_head *encap_flows)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_tc_flow *flow;

	list_for_each_entry(flow, encap_flows, tmp_list) {
		struct mlx5_flow_attr *attr = flow->attr;
		struct mlx5_esw_flow_attr *esw_attr;

		if (!mlx5e_is_offloaded_flow(flow))
			continue;
		esw_attr = attr->esw_attr;

		if (flow_flag_test(flow, SLOW))
			mlx5e_tc_unoffload_from_slow_path(esw, flow);
		else
			mlx5e_tc_unoffload_fdb_rules(esw, flow, flow->attr);
		mlx5_modify_header_dealloc(priv->mdev, attr->modify_hdr);
		attr->modify_hdr = NULL;

		esw_attr->dests[flow->tmp_entry_index].flags &=
			~MLX5_ESW_DEST_ENCAP_VALID;
		esw_attr->dests[flow->tmp_entry_index].pkt_reformat = NULL;
	}

	e->flags |= MLX5_ENCAP_ENTRY_NO_ROUTE;
	if (e->flags & MLX5_ENCAP_ENTRY_VALID) {
		e->flags &= ~MLX5_ENCAP_ENTRY_VALID;
		mlx5_packet_reformat_dealloc(priv->mdev, e->pkt_reformat);
		e->pkt_reformat = NULL;
	}
}

static void mlx5e_reoffload_encap(struct mlx5e_priv *priv,
				  struct net_device *tunnel_dev,
				  struct mlx5e_encap_entry *e,
				  struct list_head *encap_flows)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_tc_flow *flow;
	int err;

	err = ip_tunnel_info_af(e->tun_info) == AF_INET ?
		mlx5e_tc_tun_update_header_ipv4(priv, tunnel_dev, e) :
		mlx5e_tc_tun_update_header_ipv6(priv, tunnel_dev, e);
	if (err)
		mlx5_core_warn(priv->mdev, "Failed to update encap header, %d", err);
	e->flags &= ~MLX5_ENCAP_ENTRY_NO_ROUTE;

	list_for_each_entry(flow, encap_flows, tmp_list) {
		struct mlx5e_tc_flow_parse_attr *parse_attr;
		struct mlx5_flow_attr *attr = flow->attr;
		struct mlx5_esw_flow_attr *esw_attr;
		struct mlx5_flow_handle *rule;
		struct mlx5_flow_spec *spec;

		if (flow_flag_test(flow, FAILED))
			continue;

		esw_attr = attr->esw_attr;
		parse_attr = attr->parse_attr;
		spec = &parse_attr->spec;

		err = mlx5e_update_vf_tunnel(esw, esw_attr, &parse_attr->mod_hdr_acts,
					     e->out_dev, e->route_dev_ifindex,
					     flow->tmp_entry_index);
		if (err) {
			mlx5_core_warn(priv->mdev, "Failed to update VF tunnel err=%d", err);
			continue;
		}

		err = mlx5e_tc_add_flow_mod_hdr(priv, parse_attr, flow);
		if (err) {
			mlx5_core_warn(priv->mdev, "Failed to update flow mod_hdr err=%d",
				       err);
			continue;
		}

		if (e->flags & MLX5_ENCAP_ENTRY_VALID) {
			esw_attr->dests[flow->tmp_entry_index].pkt_reformat = e->pkt_reformat;
			esw_attr->dests[flow->tmp_entry_index].flags |= MLX5_ESW_DEST_ENCAP_VALID;
			if (!mlx5e_tc_flow_all_encaps_valid(esw_attr))
				goto offload_to_slow_path;
			/* update from slow path rule to encap rule */
			rule = mlx5e_tc_offload_fdb_rules(esw, flow, spec, attr);
			if (IS_ERR(rule)) {
				err = PTR_ERR(rule);
				mlx5_core_warn(priv->mdev, "Failed to update cached encapsulation flow, %d\n",
					       err);
			} else {
				flow->rule[0] = rule;
			}
		} else {
offload_to_slow_path:
			rule = mlx5e_tc_offload_to_slow_path(esw, flow, spec);
			/* mark the flow's encap dest as non-valid */
			esw_attr->dests[flow->tmp_entry_index].flags &=
				~MLX5_ESW_DEST_ENCAP_VALID;

			if (IS_ERR(rule)) {
				err = PTR_ERR(rule);
				mlx5_core_warn(priv->mdev, "Failed to update slow path (encap) flow, %d\n",
					       err);
			} else {
				flow->rule[0] = rule;
			}
		}
		flow_flag_set(flow, OFFLOADED);
	}
}

static int mlx5e_update_route_encaps(struct mlx5e_priv *priv,
				     struct mlx5e_route_entry *r,
				     struct list_head *flow_list,
				     bool replace)
{
	struct net_device *tunnel_dev;
	struct mlx5e_encap_entry *e;

	tunnel_dev = __dev_get_by_index(dev_net(priv->netdev), r->tunnel_dev_index);
	if (!tunnel_dev)
		return -ENODEV;

	list_for_each_entry(e, &r->encap_entries, route_list) {
		LIST_HEAD(encap_flows);

		mlx5e_take_all_encap_flows(e, &encap_flows);
		if (list_empty(&encap_flows))
			continue;

		if (mlx5e_route_entry_valid(r))
			mlx5e_invalidate_encap(priv, e, &encap_flows);

		if (!replace) {
			list_splice(&encap_flows, flow_list);
			continue;
		}

		mlx5e_reoffload_encap(priv, tunnel_dev, e, &encap_flows);
		list_splice(&encap_flows, flow_list);
	}

	return 0;
}

static void mlx5e_unoffload_flow_list(struct mlx5e_priv *priv,
				      struct list_head *flow_list)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_tc_flow *flow;

	list_for_each_entry(flow, flow_list, tmp_list)
		if (mlx5e_is_offloaded_flow(flow))
			mlx5e_tc_unoffload_fdb_rules(esw, flow, flow->attr);
}

static void mlx5e_reoffload_decap(struct mlx5e_priv *priv,
				  struct list_head *decap_flows)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_tc_flow *flow;

	list_for_each_entry(flow, decap_flows, tmp_list) {
		struct mlx5e_tc_flow_parse_attr *parse_attr;
		struct mlx5_flow_attr *attr = flow->attr;
		struct mlx5_flow_handle *rule;
		struct mlx5_flow_spec *spec;
		int err;

		if (flow_flag_test(flow, FAILED))
			continue;

		parse_attr = attr->parse_attr;
		spec = &parse_attr->spec;
		err = mlx5e_tc_tun_route_lookup(priv, spec, attr);
		if (err) {
			mlx5_core_warn(priv->mdev, "Failed to lookup route for flow, %d\n",
				       err);
			continue;
		}

		rule = mlx5e_tc_offload_fdb_rules(esw, flow, spec, attr);
		if (IS_ERR(rule)) {
			err = PTR_ERR(rule);
			mlx5_core_warn(priv->mdev, "Failed to update cached decap flow, %d\n",
				       err);
		} else {
			flow->rule[0] = rule;
			flow_flag_set(flow, OFFLOADED);
		}
	}
}

static int mlx5e_update_route_decap_flows(struct mlx5e_priv *priv,
					  struct mlx5e_route_entry *r,
					  struct list_head *flow_list,
					  bool replace)
{
	struct net_device *tunnel_dev;
	LIST_HEAD(decap_flows);

	tunnel_dev = __dev_get_by_index(dev_net(priv->netdev), r->tunnel_dev_index);
	if (!tunnel_dev)
		return -ENODEV;

	mlx5e_take_all_route_decap_flows(r, &decap_flows);
	if (mlx5e_route_entry_valid(r))
		mlx5e_unoffload_flow_list(priv, &decap_flows);
	if (replace)
		mlx5e_reoffload_decap(priv, &decap_flows);

	list_splice(&decap_flows, flow_list);

	return 0;
}

static void mlx5e_tc_fib_event_work(struct work_struct *work)
{
	struct mlx5e_tc_fib_event_data *event_data =
		container_of(work, struct mlx5e_tc_fib_event_data, work);
	struct net_device *ul_dev = event_data->ul_dev;
	struct mlx5e_priv *priv = netdev_priv(ul_dev);
	struct mlx5e_route_entry *r = event_data->r;
	struct mlx5_eswitch *esw;
	LIST_HEAD(flow_list);
	bool replace;
	int err;

	/* sync with concurrent neigh updates */
	rtnl_lock();
	esw = priv->mdev->priv.eswitch;
	mutex_lock(&esw->offloads.encap_tbl_lock);
	replace = event_data->event == FIB_EVENT_ENTRY_REPLACE;

	if (!mlx5e_route_entry_valid(r) && !replace)
		goto out;

	err = mlx5e_update_route_encaps(priv, r, &flow_list, replace);
	if (err)
		mlx5_core_warn(priv->mdev, "Failed to update route encaps, %d\n",
			       err);

	err = mlx5e_update_route_decap_flows(priv, r, &flow_list, replace);
	if (err)
		mlx5_core_warn(priv->mdev, "Failed to update route decap flows, %d\n",
			       err);

	if (replace)
		r->flags |= MLX5E_ROUTE_ENTRY_VALID;
out:
	mutex_unlock(&esw->offloads.encap_tbl_lock);
	rtnl_unlock();

	mlx5e_put_flow_list(priv, &flow_list);
	mlx5e_route_put(priv, event_data->r);
	dev_put(event_data->ul_dev);
	kfree(event_data);
}

static struct mlx5e_tc_fib_event_data *
mlx5e_init_fib_work_ipv4(struct mlx5e_priv *priv,
			 struct net_device *ul_dev,
			 struct mlx5e_tc_tun_encap *encap,
			 unsigned long event,
			 struct fib_notifier_info *info)
{
	struct fib_entry_notifier_info *fen_info;
	struct mlx5e_tc_fib_event_data *fib_work;
	struct mlx5e_route_entry *r;
	struct mlx5e_route_key key;
	struct net_device *fib_dev;

	fen_info = container_of(info, struct fib_entry_notifier_info, info);
	fib_dev = fib_info_nh(fen_info->fi, 0)->fib_nh_dev;
	if (fib_dev->netdev_ops != &mlx5e_netdev_ops ||
	    fen_info->dst_len != 32)
		return NULL;

	fib_work = mlx5e_tc_init_fib_work(event, ul_dev, GFP_ATOMIC);
	if (!fib_work)
		return ERR_PTR(-ENOMEM);

	key.endpoint_ip.v4 = htonl(fen_info->dst);
	key.ip_version = 4;

	/* Can't fail after this point because releasing reference to r
	 * requires obtaining sleeping mutex which we can't do in atomic
	 * context.
	 */
	r = mlx5e_route_lookup_for_update(encap, &key);
	if (!r)
		goto out;
	fib_work->r = r;
	dev_hold(ul_dev);

	return fib_work;

out:
	kfree(fib_work);
	return NULL;
}

static struct mlx5e_tc_fib_event_data *
mlx5e_init_fib_work_ipv6(struct mlx5e_priv *priv,
			 struct net_device *ul_dev,
			 struct mlx5e_tc_tun_encap *encap,
			 unsigned long event,
			 struct fib_notifier_info *info)
{
	struct fib6_entry_notifier_info *fen_info;
	struct mlx5e_tc_fib_event_data *fib_work;
	struct mlx5e_route_entry *r;
	struct mlx5e_route_key key;
	struct net_device *fib_dev;

	fen_info = container_of(info, struct fib6_entry_notifier_info, info);
	fib_dev = fib6_info_nh_dev(fen_info->rt);
	if (fib_dev->netdev_ops != &mlx5e_netdev_ops ||
	    fen_info->rt->fib6_dst.plen != 128)
		return NULL;

	fib_work = mlx5e_tc_init_fib_work(event, ul_dev, GFP_ATOMIC);
	if (!fib_work)
		return ERR_PTR(-ENOMEM);

	memcpy(&key.endpoint_ip.v6, &fen_info->rt->fib6_dst.addr,
	       sizeof(fen_info->rt->fib6_dst.addr));
	key.ip_version = 6;

	/* Can't fail after this point because releasing reference to r
	 * requires obtaining sleeping mutex which we can't do in atomic
	 * context.
	 */
	r = mlx5e_route_lookup_for_update(encap, &key);
	if (!r)
		goto out;
	fib_work->r = r;
	dev_hold(ul_dev);

	return fib_work;

out:
	kfree(fib_work);
	return NULL;
}

static int mlx5e_tc_tun_fib_event(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct mlx5e_tc_fib_event_data *fib_work;
	struct fib_notifier_info *info = ptr;
	struct mlx5e_tc_tun_encap *encap;
	struct net_device *ul_dev;
	struct mlx5e_priv *priv;

	encap = container_of(nb, struct mlx5e_tc_tun_encap, fib_nb);
	priv = encap->priv;
	ul_dev = priv->netdev;
	priv = netdev_priv(ul_dev);

	switch (event) {
	case FIB_EVENT_ENTRY_REPLACE:
	case FIB_EVENT_ENTRY_DEL:
		if (info->family == AF_INET)
			fib_work = mlx5e_init_fib_work_ipv4(priv, ul_dev, encap, event, info);
		else if (info->family == AF_INET6)
			fib_work = mlx5e_init_fib_work_ipv6(priv, ul_dev, encap, event, info);
		else
			return NOTIFY_DONE;

		if (!IS_ERR_OR_NULL(fib_work)) {
			queue_work(priv->wq, &fib_work->work);
		} else if (IS_ERR(fib_work)) {
			NL_SET_ERR_MSG_MOD(info->extack, "Failed to init fib work");
			mlx5_core_warn(priv->mdev, "Failed to init fib work, %ld\n",
				       PTR_ERR(fib_work));
		}

		break;
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_DONE;
}

struct mlx5e_tc_tun_encap *mlx5e_tc_tun_init(struct mlx5e_priv *priv)
{
	struct mlx5e_tc_tun_encap *encap;
	int err;

	encap = kvzalloc(sizeof(*encap), GFP_KERNEL);
	if (!encap)
		return ERR_PTR(-ENOMEM);

	encap->priv = priv;
	encap->fib_nb.notifier_call = mlx5e_tc_tun_fib_event;
	spin_lock_init(&encap->route_lock);
	hash_init(encap->route_tbl);
	err = register_fib_notifier(dev_net(priv->netdev), &encap->fib_nb,
				    NULL, NULL);
	if (err) {
		kvfree(encap);
		return ERR_PTR(err);
	}

	return encap;
}

void mlx5e_tc_tun_cleanup(struct mlx5e_tc_tun_encap *encap)
{
	if (!encap)
		return;

	unregister_fib_notifier(dev_net(encap->priv->netdev), &encap->fib_nb);
	flush_workqueue(encap->priv->wq); /* flush fib event works */
	kvfree(encap);
}
