/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2018 Mellanox Technologies. */

#include <net/flow.h>
#include <net/inet_dscp.h>
#include <net/vxlan.h>
#include <net/gre.h>
#include <net/geneve.h>
#include <net/bareudp.h>
#include "en/tc_tun.h"
#include "en/tc_priv.h"
#include "en_tc.h"
#include "rep/tc.h"
#include "rep/neigh.h"
#include "lag/lag.h"
#include "lag/mp.h"

struct mlx5e_tc_tun_route_attr {
	struct net_device *out_dev;
	struct net_device *route_dev;
	union {
		struct flowi4 fl4;
		struct flowi6 fl6;
	} fl;
	struct neighbour *n;
	u8 ttl;
};

#define TC_TUN_ROUTE_ATTR_INIT(name) struct mlx5e_tc_tun_route_attr name = {}

static void mlx5e_tc_tun_route_attr_cleanup(struct mlx5e_tc_tun_route_attr *attr)
{
	if (attr->n)
		neigh_release(attr->n);
	dev_put(attr->route_dev);
}

struct mlx5e_tc_tunnel *mlx5e_get_tc_tun(struct net_device *tunnel_dev)
{
	if (netif_is_vxlan(tunnel_dev))
		return &vxlan_tunnel;
	else if (netif_is_geneve(tunnel_dev))
		return &geneve_tunnel;
	else if (netif_is_gretap(tunnel_dev) ||
		 netif_is_ip6gretap(tunnel_dev))
		return &gre_tunnel;
	else if (netif_is_bareudp(tunnel_dev))
		return &mplsoudp_tunnel;
	else
		return NULL;
}

static int get_route_and_out_devs(struct mlx5e_priv *priv,
				  struct net_device *dev,
				  struct net_device **route_dev,
				  struct net_device **out_dev)
{
	struct net_device *uplink_dev, *uplink_upper, *real_dev;
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	bool dst_is_lag_dev;

	real_dev = is_vlan_dev(dev) ? vlan_dev_real_dev(dev) : dev;
	uplink_dev = mlx5_eswitch_uplink_get_proto_dev(esw, REP_ETH);

	rcu_read_lock();
	uplink_upper = netdev_master_upper_dev_get_rcu(uplink_dev);
	/* mlx5_lag_is_sriov() is a blocking function which can't be called
	 * while holding rcu read lock. Take the net_device for correctness
	 * sake.
	 */
	dev_hold(uplink_upper);
	rcu_read_unlock();

	dst_is_lag_dev = (uplink_upper &&
			  netif_is_lag_master(uplink_upper) &&
			  real_dev == uplink_upper &&
			  mlx5_lag_is_sriov(priv->mdev));
	dev_put(uplink_upper);

	/* if the egress device isn't on the same HW e-switch or
	 * it's a LAG device, use the uplink
	 */
	*route_dev = dev;
	if (!netdev_port_same_parent_id(priv->netdev, real_dev) ||
	    dst_is_lag_dev || is_vlan_dev(*route_dev) ||
	    netif_is_ovs_master(*route_dev))
		*out_dev = uplink_dev;
	else if (mlx5e_eswitch_rep(dev) &&
		 mlx5e_is_valid_eswitch_fwd_dev(priv, dev))
		*out_dev = *route_dev;
	else
		return -EOPNOTSUPP;

	if (!mlx5e_eswitch_uplink_rep(*out_dev))
		return -EOPNOTSUPP;

	if (mlx5e_eswitch_uplink_rep(priv->netdev) && *out_dev != priv->netdev &&
	    !mlx5_lag_is_mpesw(priv->mdev))
		return -EOPNOTSUPP;

	return 0;
}

static int mlx5e_route_lookup_ipv4_get(struct mlx5e_priv *priv,
				       struct net_device *dev,
				       struct mlx5e_tc_tun_route_attr *attr)
{
	struct net_device *route_dev;
	struct net_device *out_dev;
	struct neighbour *n;
	struct rtable *rt;

#if IS_ENABLED(CONFIG_INET)
	struct mlx5_core_dev *mdev = priv->mdev;
	struct net_device *uplink_dev;
	int ret;

	if (mlx5_lag_is_multipath(mdev)) {
		struct mlx5_eswitch *esw = mdev->priv.eswitch;

		uplink_dev = mlx5_eswitch_uplink_get_proto_dev(esw, REP_ETH);
		attr->fl.fl4.flowi4_oif = uplink_dev->ifindex;
	} else {
		struct mlx5e_tc_tunnel *tunnel = mlx5e_get_tc_tun(dev);

		if (tunnel && tunnel->get_remote_ifindex)
			attr->fl.fl4.flowi4_oif = tunnel->get_remote_ifindex(dev);
	}

	rt = ip_route_output_key(dev_net(dev), &attr->fl.fl4);
	if (IS_ERR(rt))
		return PTR_ERR(rt);

	if (rt->rt_type != RTN_UNICAST) {
		ret = -ENETUNREACH;
		goto err_rt_release;
	}

	if (mlx5_lag_is_multipath(mdev) && rt->rt_gw_family != AF_INET) {
		ret = -ENETUNREACH;
		goto err_rt_release;
	}
#else
	return -EOPNOTSUPP;
#endif

	ret = get_route_and_out_devs(priv, rt->dst.dev, &route_dev, &out_dev);
	if (ret < 0)
		goto err_rt_release;
	dev_hold(route_dev);

	if (!attr->ttl)
		attr->ttl = ip4_dst_hoplimit(&rt->dst);
	n = dst_neigh_lookup(&rt->dst, &attr->fl.fl4.daddr);
	if (!n) {
		ret = -ENOMEM;
		goto err_dev_release;
	}

	ip_rt_put(rt);
	attr->route_dev = route_dev;
	attr->out_dev = out_dev;
	attr->n = n;
	return 0;

err_dev_release:
	dev_put(route_dev);
err_rt_release:
	ip_rt_put(rt);
	return ret;
}

static void mlx5e_route_lookup_ipv4_put(struct mlx5e_tc_tun_route_attr *attr)
{
	mlx5e_tc_tun_route_attr_cleanup(attr);
}

static const char *mlx5e_netdev_kind(struct net_device *dev)
{
	if (dev->rtnl_link_ops)
		return dev->rtnl_link_ops->kind;
	else
		return "unknown";
}

static int mlx5e_gen_ip_tunnel_header(char buf[], __u8 *ip_proto,
				      struct mlx5e_encap_entry *e)
{
	if (!e->tunnel) {
		pr_warn("mlx5: Cannot generate tunnel header for this tunnel\n");
		return -EOPNOTSUPP;
	}

	return e->tunnel->generate_ip_tun_hdr(buf, ip_proto, e);
}

static char *gen_eth_tnl_hdr(char *buf, struct net_device *dev,
			     struct mlx5e_encap_entry *e,
			     u16 proto)
{
	struct ethhdr *eth = (struct ethhdr *)buf;
	char *ip;

	ether_addr_copy(eth->h_dest, e->h_dest);
	ether_addr_copy(eth->h_source, dev->dev_addr);
	if (is_vlan_dev(dev)) {
		struct vlan_hdr *vlan = (struct vlan_hdr *)
					((char *)eth + ETH_HLEN);
		ip = (char *)vlan + VLAN_HLEN;
		eth->h_proto = vlan_dev_vlan_proto(dev);
		vlan->h_vlan_TCI = htons(vlan_dev_vlan_id(dev));
		vlan->h_vlan_encapsulated_proto = htons(proto);
	} else {
		eth->h_proto = htons(proto);
		ip = (char *)eth + ETH_HLEN;
	}

	return ip;
}

int mlx5e_tc_tun_create_header_ipv4(struct mlx5e_priv *priv,
				    struct net_device *mirred_dev,
				    struct mlx5e_encap_entry *e)
{
	int max_encap_size = MLX5_CAP_ESW(priv->mdev, max_encap_header_size);
	const struct ip_tunnel_key *tun_key = &e->tun_info->key;
	struct mlx5_pkt_reformat_params reformat_params;
	struct mlx5e_neigh m_neigh = {};
	TC_TUN_ROUTE_ATTR_INIT(attr);
	int ipv4_encap_size;
	char *encap_header;
	struct iphdr *ip;
	u8 nud_state;
	int err;

	/* add the IP fields */
	attr.fl.fl4.flowi4_dscp = inet_dsfield_to_dscp(tun_key->tos);
	attr.fl.fl4.daddr = tun_key->u.ipv4.dst;
	attr.fl.fl4.saddr = tun_key->u.ipv4.src;
	attr.ttl = tun_key->ttl;

	err = mlx5e_route_lookup_ipv4_get(priv, mirred_dev, &attr);
	if (err)
		return err;

	ipv4_encap_size =
		(is_vlan_dev(attr.route_dev) ? VLAN_ETH_HLEN : ETH_HLEN) +
		sizeof(struct iphdr) +
		e->tunnel->calc_hlen(e);

	if (max_encap_size < ipv4_encap_size) {
		mlx5_core_warn(priv->mdev, "encap size %d too big, max supported is %d\n",
			       ipv4_encap_size, max_encap_size);
		err = -EOPNOTSUPP;
		goto release_neigh;
	}

	encap_header = kzalloc(ipv4_encap_size, GFP_KERNEL);
	if (!encap_header) {
		err = -ENOMEM;
		goto release_neigh;
	}

	m_neigh.family = attr.n->ops->family;
	memcpy(&m_neigh.dst_ip, attr.n->primary_key, attr.n->tbl->key_len);
	e->out_dev = attr.out_dev;
	e->route_dev_ifindex = attr.route_dev->ifindex;

	/* It's important to add the neigh to the hash table before checking
	 * the neigh validity state. So if we'll get a notification, in case the
	 * neigh changes it's validity state, we would find the relevant neigh
	 * in the hash.
	 */
	err = mlx5e_rep_encap_entry_attach(netdev_priv(attr.out_dev), e, &m_neigh, attr.n->dev);
	if (err)
		goto free_encap;

	read_lock_bh(&attr.n->lock);
	nud_state = attr.n->nud_state;
	ether_addr_copy(e->h_dest, attr.n->ha);
	read_unlock_bh(&attr.n->lock);

	/* add ethernet header */
	ip = (struct iphdr *)gen_eth_tnl_hdr(encap_header, attr.route_dev, e,
					     ETH_P_IP);

	/* add ip header */
	ip->tos = tun_key->tos;
	ip->version = 0x4;
	ip->ihl = 0x5;
	ip->ttl = attr.ttl;
	ip->daddr = attr.fl.fl4.daddr;
	ip->saddr = attr.fl.fl4.saddr;

	/* add tunneling protocol header */
	err = mlx5e_gen_ip_tunnel_header((char *)ip + sizeof(struct iphdr),
					 &ip->protocol, e);
	if (err)
		goto destroy_neigh_entry;

	e->encap_size = ipv4_encap_size;
	e->encap_header = encap_header;
	encap_header = NULL;

	if (!(nud_state & NUD_VALID)) {
		neigh_event_send(attr.n, NULL);
		/* the encap entry will be made valid on neigh update event
		 * and not used before that.
		 */
		goto release_neigh;
	}

	memset(&reformat_params, 0, sizeof(reformat_params));
	reformat_params.type = e->reformat_type;
	reformat_params.size = e->encap_size;
	reformat_params.data = e->encap_header;
	e->pkt_reformat = mlx5_packet_reformat_alloc(priv->mdev, &reformat_params,
						     MLX5_FLOW_NAMESPACE_FDB);
	if (IS_ERR(e->pkt_reformat)) {
		err = PTR_ERR(e->pkt_reformat);
		goto destroy_neigh_entry;
	}

	e->flags |= MLX5_ENCAP_ENTRY_VALID;
	mlx5e_rep_queue_neigh_stats_work(netdev_priv(attr.out_dev));
	mlx5e_route_lookup_ipv4_put(&attr);
	return err;

destroy_neigh_entry:
	mlx5e_rep_encap_entry_detach(netdev_priv(e->out_dev), e);
free_encap:
	kfree(encap_header);
release_neigh:
	mlx5e_route_lookup_ipv4_put(&attr);
	return err;
}

int mlx5e_tc_tun_update_header_ipv4(struct mlx5e_priv *priv,
				    struct net_device *mirred_dev,
				    struct mlx5e_encap_entry *e)
{
	int max_encap_size = MLX5_CAP_ESW(priv->mdev, max_encap_header_size);
	const struct ip_tunnel_key *tun_key = &e->tun_info->key;
	struct mlx5_pkt_reformat_params reformat_params;
	TC_TUN_ROUTE_ATTR_INIT(attr);
	int ipv4_encap_size;
	char *encap_header;
	struct iphdr *ip;
	u8 nud_state;
	int err;

	/* add the IP fields */
	attr.fl.fl4.flowi4_dscp = inet_dsfield_to_dscp(tun_key->tos);
	attr.fl.fl4.daddr = tun_key->u.ipv4.dst;
	attr.fl.fl4.saddr = tun_key->u.ipv4.src;
	attr.ttl = tun_key->ttl;

	err = mlx5e_route_lookup_ipv4_get(priv, mirred_dev, &attr);
	if (err)
		return err;

	ipv4_encap_size =
		(is_vlan_dev(attr.route_dev) ? VLAN_ETH_HLEN : ETH_HLEN) +
		sizeof(struct iphdr) +
		e->tunnel->calc_hlen(e);

	if (max_encap_size < ipv4_encap_size) {
		mlx5_core_warn(priv->mdev, "encap size %d too big, max supported is %d\n",
			       ipv4_encap_size, max_encap_size);
		err = -EOPNOTSUPP;
		goto release_neigh;
	}

	encap_header = kzalloc(ipv4_encap_size, GFP_KERNEL);
	if (!encap_header) {
		err = -ENOMEM;
		goto release_neigh;
	}

	e->route_dev_ifindex = attr.route_dev->ifindex;

	read_lock_bh(&attr.n->lock);
	nud_state = attr.n->nud_state;
	ether_addr_copy(e->h_dest, attr.n->ha);
	WRITE_ONCE(e->nhe->neigh_dev, attr.n->dev);
	read_unlock_bh(&attr.n->lock);

	/* add ethernet header */
	ip = (struct iphdr *)gen_eth_tnl_hdr(encap_header, attr.route_dev, e,
					     ETH_P_IP);

	/* add ip header */
	ip->tos = tun_key->tos;
	ip->version = 0x4;
	ip->ihl = 0x5;
	ip->ttl = attr.ttl;
	ip->daddr = attr.fl.fl4.daddr;
	ip->saddr = attr.fl.fl4.saddr;

	/* add tunneling protocol header */
	err = mlx5e_gen_ip_tunnel_header((char *)ip + sizeof(struct iphdr),
					 &ip->protocol, e);
	if (err)
		goto free_encap;

	e->encap_size = ipv4_encap_size;
	kfree(e->encap_header);
	e->encap_header = encap_header;
	encap_header = NULL;

	if (!(nud_state & NUD_VALID)) {
		neigh_event_send(attr.n, NULL);
		/* the encap entry will be made valid on neigh update event
		 * and not used before that.
		 */
		goto release_neigh;
	}

	memset(&reformat_params, 0, sizeof(reformat_params));
	reformat_params.type = e->reformat_type;
	reformat_params.size = e->encap_size;
	reformat_params.data = e->encap_header;
	e->pkt_reformat = mlx5_packet_reformat_alloc(priv->mdev, &reformat_params,
						     MLX5_FLOW_NAMESPACE_FDB);
	if (IS_ERR(e->pkt_reformat)) {
		err = PTR_ERR(e->pkt_reformat);
		goto free_encap;
	}

	e->flags |= MLX5_ENCAP_ENTRY_VALID;
	mlx5e_rep_queue_neigh_stats_work(netdev_priv(attr.out_dev));
	mlx5e_route_lookup_ipv4_put(&attr);
	return err;

free_encap:
	kfree(encap_header);
release_neigh:
	mlx5e_route_lookup_ipv4_put(&attr);
	return err;
}

#if IS_ENABLED(CONFIG_INET) && IS_ENABLED(CONFIG_IPV6)
static int mlx5e_route_lookup_ipv6_get(struct mlx5e_priv *priv,
				       struct net_device *dev,
				       struct mlx5e_tc_tun_route_attr *attr)
{
	struct mlx5e_tc_tunnel *tunnel = mlx5e_get_tc_tun(dev);
	struct net_device *route_dev;
	struct net_device *out_dev;
	struct dst_entry *dst;
	struct neighbour *n;
	int ret;

	if (tunnel && tunnel->get_remote_ifindex)
		attr->fl.fl6.flowi6_oif = tunnel->get_remote_ifindex(dev);
	dst = ipv6_stub->ipv6_dst_lookup_flow(dev_net(dev), NULL, &attr->fl.fl6,
					      NULL);
	if (IS_ERR(dst))
		return PTR_ERR(dst);

	if (!attr->ttl)
		attr->ttl = ip6_dst_hoplimit(dst);

	ret = get_route_and_out_devs(priv, dst->dev, &route_dev, &out_dev);
	if (ret < 0)
		goto err_dst_release;

	dev_hold(route_dev);
	n = dst_neigh_lookup(dst, &attr->fl.fl6.daddr);
	if (!n) {
		ret = -ENOMEM;
		goto err_dev_release;
	}

	dst_release(dst);
	attr->out_dev = out_dev;
	attr->route_dev = route_dev;
	attr->n = n;
	return 0;

err_dev_release:
	dev_put(route_dev);
err_dst_release:
	dst_release(dst);
	return ret;
}

static void mlx5e_route_lookup_ipv6_put(struct mlx5e_tc_tun_route_attr *attr)
{
	mlx5e_tc_tun_route_attr_cleanup(attr);
}

int mlx5e_tc_tun_create_header_ipv6(struct mlx5e_priv *priv,
				    struct net_device *mirred_dev,
				    struct mlx5e_encap_entry *e)
{
	int max_encap_size = MLX5_CAP_ESW(priv->mdev, max_encap_header_size);
	const struct ip_tunnel_key *tun_key = &e->tun_info->key;
	struct mlx5_pkt_reformat_params reformat_params;
	struct mlx5e_neigh m_neigh = {};
	TC_TUN_ROUTE_ATTR_INIT(attr);
	struct ipv6hdr *ip6h;
	int ipv6_encap_size;
	char *encap_header;
	u8 nud_state;
	int err;

	attr.ttl = tun_key->ttl;
	attr.fl.fl6.flowlabel = ip6_make_flowinfo(tun_key->tos, tun_key->label);
	attr.fl.fl6.daddr = tun_key->u.ipv6.dst;
	attr.fl.fl6.saddr = tun_key->u.ipv6.src;

	err = mlx5e_route_lookup_ipv6_get(priv, mirred_dev, &attr);
	if (err)
		return err;

	ipv6_encap_size =
		(is_vlan_dev(attr.route_dev) ? VLAN_ETH_HLEN : ETH_HLEN) +
		sizeof(struct ipv6hdr) +
		e->tunnel->calc_hlen(e);

	if (max_encap_size < ipv6_encap_size) {
		mlx5_core_warn(priv->mdev, "encap size %d too big, max supported is %d\n",
			       ipv6_encap_size, max_encap_size);
		err = -EOPNOTSUPP;
		goto release_neigh;
	}

	encap_header = kzalloc(ipv6_encap_size, GFP_KERNEL);
	if (!encap_header) {
		err = -ENOMEM;
		goto release_neigh;
	}

	m_neigh.family = attr.n->ops->family;
	memcpy(&m_neigh.dst_ip, attr.n->primary_key, attr.n->tbl->key_len);
	e->out_dev = attr.out_dev;
	e->route_dev_ifindex = attr.route_dev->ifindex;

	/* It's important to add the neigh to the hash table before checking
	 * the neigh validity state. So if we'll get a notification, in case the
	 * neigh changes it's validity state, we would find the relevant neigh
	 * in the hash.
	 */
	err = mlx5e_rep_encap_entry_attach(netdev_priv(attr.out_dev), e, &m_neigh, attr.n->dev);
	if (err)
		goto free_encap;

	read_lock_bh(&attr.n->lock);
	nud_state = attr.n->nud_state;
	ether_addr_copy(e->h_dest, attr.n->ha);
	read_unlock_bh(&attr.n->lock);

	/* add ethernet header */
	ip6h = (struct ipv6hdr *)gen_eth_tnl_hdr(encap_header, attr.route_dev, e,
						 ETH_P_IPV6);

	/* add ip header */
	ip6_flow_hdr(ip6h, tun_key->tos, 0);
	/* the HW fills up ipv6 payload len */
	ip6h->hop_limit   = attr.ttl;
	ip6h->daddr	  = attr.fl.fl6.daddr;
	ip6h->saddr	  = attr.fl.fl6.saddr;

	/* add tunneling protocol header */
	err = mlx5e_gen_ip_tunnel_header((char *)ip6h + sizeof(struct ipv6hdr),
					 &ip6h->nexthdr, e);
	if (err)
		goto destroy_neigh_entry;

	e->encap_size = ipv6_encap_size;
	e->encap_header = encap_header;
	encap_header = NULL;

	if (!(nud_state & NUD_VALID)) {
		neigh_event_send(attr.n, NULL);
		/* the encap entry will be made valid on neigh update event
		 * and not used before that.
		 */
		goto release_neigh;
	}

	memset(&reformat_params, 0, sizeof(reformat_params));
	reformat_params.type = e->reformat_type;
	reformat_params.size = e->encap_size;
	reformat_params.data = e->encap_header;
	e->pkt_reformat = mlx5_packet_reformat_alloc(priv->mdev, &reformat_params,
						     MLX5_FLOW_NAMESPACE_FDB);
	if (IS_ERR(e->pkt_reformat)) {
		err = PTR_ERR(e->pkt_reformat);
		goto destroy_neigh_entry;
	}

	e->flags |= MLX5_ENCAP_ENTRY_VALID;
	mlx5e_rep_queue_neigh_stats_work(netdev_priv(attr.out_dev));
	mlx5e_route_lookup_ipv6_put(&attr);
	return err;

destroy_neigh_entry:
	mlx5e_rep_encap_entry_detach(netdev_priv(e->out_dev), e);
free_encap:
	kfree(encap_header);
release_neigh:
	mlx5e_route_lookup_ipv6_put(&attr);
	return err;
}

int mlx5e_tc_tun_update_header_ipv6(struct mlx5e_priv *priv,
				    struct net_device *mirred_dev,
				    struct mlx5e_encap_entry *e)
{
	int max_encap_size = MLX5_CAP_ESW(priv->mdev, max_encap_header_size);
	const struct ip_tunnel_key *tun_key = &e->tun_info->key;
	struct mlx5_pkt_reformat_params reformat_params;
	TC_TUN_ROUTE_ATTR_INIT(attr);
	struct ipv6hdr *ip6h;
	int ipv6_encap_size;
	char *encap_header;
	u8 nud_state;
	int err;

	attr.ttl = tun_key->ttl;

	attr.fl.fl6.flowlabel = ip6_make_flowinfo(tun_key->tos, tun_key->label);
	attr.fl.fl6.daddr = tun_key->u.ipv6.dst;
	attr.fl.fl6.saddr = tun_key->u.ipv6.src;

	err = mlx5e_route_lookup_ipv6_get(priv, mirred_dev, &attr);
	if (err)
		return err;

	ipv6_encap_size =
		(is_vlan_dev(attr.route_dev) ? VLAN_ETH_HLEN : ETH_HLEN) +
		sizeof(struct ipv6hdr) +
		e->tunnel->calc_hlen(e);

	if (max_encap_size < ipv6_encap_size) {
		mlx5_core_warn(priv->mdev, "encap size %d too big, max supported is %d\n",
			       ipv6_encap_size, max_encap_size);
		err = -EOPNOTSUPP;
		goto release_neigh;
	}

	encap_header = kzalloc(ipv6_encap_size, GFP_KERNEL);
	if (!encap_header) {
		err = -ENOMEM;
		goto release_neigh;
	}

	e->route_dev_ifindex = attr.route_dev->ifindex;

	read_lock_bh(&attr.n->lock);
	nud_state = attr.n->nud_state;
	ether_addr_copy(e->h_dest, attr.n->ha);
	WRITE_ONCE(e->nhe->neigh_dev, attr.n->dev);
	read_unlock_bh(&attr.n->lock);

	/* add ethernet header */
	ip6h = (struct ipv6hdr *)gen_eth_tnl_hdr(encap_header, attr.route_dev, e,
						 ETH_P_IPV6);

	/* add ip header */
	ip6_flow_hdr(ip6h, tun_key->tos, 0);
	/* the HW fills up ipv6 payload len */
	ip6h->hop_limit   = attr.ttl;
	ip6h->daddr	  = attr.fl.fl6.daddr;
	ip6h->saddr	  = attr.fl.fl6.saddr;

	/* add tunneling protocol header */
	err = mlx5e_gen_ip_tunnel_header((char *)ip6h + sizeof(struct ipv6hdr),
					 &ip6h->nexthdr, e);
	if (err)
		goto free_encap;

	e->encap_size = ipv6_encap_size;
	kfree(e->encap_header);
	e->encap_header = encap_header;
	encap_header = NULL;

	if (!(nud_state & NUD_VALID)) {
		neigh_event_send(attr.n, NULL);
		/* the encap entry will be made valid on neigh update event
		 * and not used before that.
		 */
		goto release_neigh;
	}

	memset(&reformat_params, 0, sizeof(reformat_params));
	reformat_params.type = e->reformat_type;
	reformat_params.size = e->encap_size;
	reformat_params.data = e->encap_header;
	e->pkt_reformat = mlx5_packet_reformat_alloc(priv->mdev, &reformat_params,
						     MLX5_FLOW_NAMESPACE_FDB);
	if (IS_ERR(e->pkt_reformat)) {
		err = PTR_ERR(e->pkt_reformat);
		goto free_encap;
	}

	e->flags |= MLX5_ENCAP_ENTRY_VALID;
	mlx5e_rep_queue_neigh_stats_work(netdev_priv(attr.out_dev));
	mlx5e_route_lookup_ipv6_put(&attr);
	return err;

free_encap:
	kfree(encap_header);
release_neigh:
	mlx5e_route_lookup_ipv6_put(&attr);
	return err;
}
#endif

int mlx5e_tc_tun_route_lookup(struct mlx5e_priv *priv,
			      struct mlx5_flow_spec *spec,
			      struct mlx5_flow_attr *flow_attr,
			      struct net_device *filter_dev)
{
	struct mlx5_esw_flow_attr *esw_attr = flow_attr->esw_attr;
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_tc_int_port *int_port;
	TC_TUN_ROUTE_ATTR_INIT(attr);
	u16 vport_num;
	int err = 0;

	if (flow_attr->tun_ip_version == 4) {
		/* Addresses are swapped for decap */
		attr.fl.fl4.saddr = esw_attr->rx_tun_attr->dst_ip.v4;
		attr.fl.fl4.daddr = esw_attr->rx_tun_attr->src_ip.v4;
		err = mlx5e_route_lookup_ipv4_get(priv, filter_dev, &attr);
	}
#if IS_ENABLED(CONFIG_INET) && IS_ENABLED(CONFIG_IPV6)
	else if (flow_attr->tun_ip_version == 6) {
		/* Addresses are swapped for decap */
		attr.fl.fl6.saddr = esw_attr->rx_tun_attr->dst_ip.v6;
		attr.fl.fl6.daddr = esw_attr->rx_tun_attr->src_ip.v6;
		err = mlx5e_route_lookup_ipv6_get(priv, filter_dev, &attr);
	}
#endif
	else
		return 0;

	if (err)
		return err;

	if (attr.route_dev->netdev_ops == &mlx5e_netdev_ops &&
	    mlx5e_tc_is_vf_tunnel(attr.out_dev, attr.route_dev)) {
		err = mlx5e_tc_query_route_vport(attr.out_dev, attr.route_dev, &vport_num);
		if (err)
			goto out;

		esw_attr->rx_tun_attr->decap_vport = vport_num;
	} else if (netif_is_ovs_master(attr.route_dev) && mlx5e_tc_int_port_supported(esw)) {
		int_port = mlx5e_tc_int_port_get(mlx5e_get_int_port_priv(priv),
						 attr.route_dev->ifindex,
						 MLX5E_TC_INT_PORT_INGRESS);
		if (IS_ERR(int_port)) {
			err = PTR_ERR(int_port);
			goto out;
		}
		esw_attr->int_port = int_port;
	}

out:
	if (flow_attr->tun_ip_version == 4)
		mlx5e_route_lookup_ipv4_put(&attr);
#if IS_ENABLED(CONFIG_INET) && IS_ENABLED(CONFIG_IPV6)
	else if (flow_attr->tun_ip_version == 6)
		mlx5e_route_lookup_ipv6_put(&attr);
#endif
	return err;
}

bool mlx5e_tc_tun_device_to_offload(struct mlx5e_priv *priv,
				    struct net_device *netdev)
{
	struct mlx5e_tc_tunnel *tunnel = mlx5e_get_tc_tun(netdev);

	if (tunnel && tunnel->can_offload(priv))
		return true;
	else
		return false;
}

int mlx5e_tc_tun_init_encap_attr(struct net_device *tunnel_dev,
				 struct mlx5e_priv *priv,
				 struct mlx5e_encap_entry *e,
				 struct netlink_ext_ack *extack)
{
	struct mlx5e_tc_tunnel *tunnel = mlx5e_get_tc_tun(tunnel_dev);

	if (!tunnel) {
		e->reformat_type = -1;
		return -EOPNOTSUPP;
	}

	return tunnel->init_encap_attr(tunnel_dev, priv, e, extack);
}

int mlx5e_tc_tun_parse(struct net_device *filter_dev,
		       struct mlx5e_priv *priv,
		       struct mlx5_flow_spec *spec,
		       struct flow_cls_offload *f,
		       u8 *match_level)
{
	struct mlx5e_tc_tunnel *tunnel = mlx5e_get_tc_tun(filter_dev);
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	void *headers_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				       outer_headers);
	void *headers_v = MLX5_ADDR_OF(fte_match_param, spec->match_value,
				       outer_headers);
	struct netlink_ext_ack *extack = f->common.extack;
	int err = 0;

	if (!tunnel) {
		netdev_warn(priv->netdev,
			    "decapsulation offload is not supported for %s net device\n",
			    mlx5e_netdev_kind(filter_dev));
		err = -EOPNOTSUPP;
		goto out;
	}

	*match_level = tunnel->match_level;

	if (tunnel->parse_udp_ports) {
		err = tunnel->parse_udp_ports(priv, spec, f,
					      headers_c, headers_v);
		if (err)
			goto out;
	}

	if (tunnel->parse_tunnel) {
		err = tunnel->parse_tunnel(priv, spec, f,
					   headers_c, headers_v);
		if (err)
			goto out;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_CONTROL)) {
		struct flow_dissector_key_basic key_basic = {};
		struct flow_dissector_key_basic mask_basic = {
			.n_proto = htons(0xFFFF),
		};
		struct flow_match_basic match_basic = {
			.key = &key_basic, .mask = &mask_basic,
		};
		struct flow_match_control match;
		u16 addr_type;

		flow_rule_match_enc_control(rule, &match);
		addr_type = match.key->addr_type;

		if (flow_rule_has_enc_control_flags(match.mask->flags,
						    extack)) {
			err = -EOPNOTSUPP;
			goto out;
		}

		/* For tunnel addr_type used same key id`s as for non-tunnel */
		if (addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS) {
			struct flow_match_ipv4_addrs match;

			flow_rule_match_enc_ipv4_addrs(rule, &match);
			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 src_ipv4_src_ipv6.ipv4_layout.ipv4,
				 ntohl(match.mask->src));
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 src_ipv4_src_ipv6.ipv4_layout.ipv4,
				 ntohl(match.key->src));

			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 dst_ipv4_dst_ipv6.ipv4_layout.ipv4,
				 ntohl(match.mask->dst));
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 dst_ipv4_dst_ipv6.ipv4_layout.ipv4,
				 ntohl(match.key->dst));

			key_basic.n_proto = htons(ETH_P_IP);
			mlx5e_tc_set_ethertype(priv->mdev, &match_basic, true,
					       headers_c, headers_v);
		} else if (addr_type == FLOW_DISSECTOR_KEY_IPV6_ADDRS) {
			struct flow_match_ipv6_addrs match;

			flow_rule_match_enc_ipv6_addrs(rule, &match);
			memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
					    src_ipv4_src_ipv6.ipv6_layout.ipv6),
			       &match.mask->src, MLX5_FLD_SZ_BYTES(ipv6_layout,
								   ipv6));
			memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
					    src_ipv4_src_ipv6.ipv6_layout.ipv6),
			       &match.key->src, MLX5_FLD_SZ_BYTES(ipv6_layout,
								  ipv6));

			memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
					    dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
			       &match.mask->dst, MLX5_FLD_SZ_BYTES(ipv6_layout,
								   ipv6));
			memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
					    dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
			       &match.key->dst, MLX5_FLD_SZ_BYTES(ipv6_layout,
								  ipv6));

			key_basic.n_proto = htons(ETH_P_IPV6);
			mlx5e_tc_set_ethertype(priv->mdev, &match_basic, true,
					       headers_c, headers_v);
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_IP)) {
		struct flow_match_ip match;

		flow_rule_match_enc_ip(rule, &match);
		MLX5_SET(fte_match_set_lyr_2_4, headers_c, ip_ecn,
			 match.mask->tos & 0x3);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ip_ecn,
			 match.key->tos & 0x3);

		MLX5_SET(fte_match_set_lyr_2_4, headers_c, ip_dscp,
			 match.mask->tos >> 2);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ip_dscp,
			 match.key->tos  >> 2);

		MLX5_SET(fte_match_set_lyr_2_4, headers_c, ttl_hoplimit,
			 match.mask->ttl);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ttl_hoplimit,
			 match.key->ttl);

		if (match.mask->ttl &&
		    !MLX5_CAP_ESW_FLOWTABLE_FDB
			(priv->mdev,
			 ft_field_support.outer_ipv4_ttl)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Matching on TTL is not supported");
			err = -EOPNOTSUPP;
			goto out;
		}
	}

	/* let software handle IP fragments */
	MLX5_SET(fte_match_set_lyr_2_4, headers_c, frag, 1);
	MLX5_SET(fte_match_set_lyr_2_4, headers_v, frag, 0);

	return 0;

out:
	return err;
}

int mlx5e_tc_tun_parse_udp_ports(struct mlx5e_priv *priv,
				 struct mlx5_flow_spec *spec,
				 struct flow_cls_offload *f,
				 void *headers_c,
				 void *headers_v)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	struct netlink_ext_ack *extack = f->common.extack;
	struct flow_match_ports enc_ports;

	/* Full udp dst port must be given */

	if (!flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_PORTS)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "UDP tunnel decap filter must include enc_dst_port condition");
		netdev_warn(priv->netdev,
			    "UDP tunnel decap filter must include enc_dst_port condition\n");
		return -EOPNOTSUPP;
	}

	flow_rule_match_enc_ports(rule, &enc_ports);

	if (memchr_inv(&enc_ports.mask->dst, 0xff,
		       sizeof(enc_ports.mask->dst))) {
		NL_SET_ERR_MSG_MOD(extack,
				   "UDP tunnel decap filter must match enc_dst_port fully");
		netdev_warn(priv->netdev,
			    "UDP tunnel decap filter must match enc_dst_port fully\n");
		return -EOPNOTSUPP;
	}

	/* match on UDP protocol and dst port number */

	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, ip_protocol);
	MLX5_SET(fte_match_set_lyr_2_4, headers_v, ip_protocol, IPPROTO_UDP);

	MLX5_SET(fte_match_set_lyr_2_4, headers_c, udp_dport,
		 ntohs(enc_ports.mask->dst));
	MLX5_SET(fte_match_set_lyr_2_4, headers_v, udp_dport,
		 ntohs(enc_ports.key->dst));

	/* UDP src port on outer header is generated by HW,
	 * so it is probably a bad idea to request matching it.
	 * Nonetheless, it is allowed.
	 */

	MLX5_SET(fte_match_set_lyr_2_4, headers_c, udp_sport,
		 ntohs(enc_ports.mask->src));
	MLX5_SET(fte_match_set_lyr_2_4, headers_v, udp_sport,
		 ntohs(enc_ports.key->src));

	return 0;
}
