/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2018 Mellanox Technologies. */

#include <net/vxlan.h>
#include "lib/vxlan.h"
#include "en/tc_tun.h"

static int mlx5e_route_lookup_ipv4(struct mlx5e_priv *priv,
				   struct net_device *mirred_dev,
				   struct net_device **out_dev,
				   struct flowi4 *fl4,
				   struct neighbour **out_n,
				   u8 *out_ttl)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_rep_priv *uplink_rpriv;
	struct rtable *rt;
	struct neighbour *n = NULL;

#if IS_ENABLED(CONFIG_INET)
	int ret;

	rt = ip_route_output_key(dev_net(mirred_dev), fl4);
	ret = PTR_ERR_OR_ZERO(rt);
	if (ret)
		return ret;
#else
	return -EOPNOTSUPP;
#endif
	uplink_rpriv = mlx5_eswitch_get_uplink_priv(esw, REP_ETH);
	/* if the egress device isn't on the same HW e-switch, we use the uplink */
	if (!switchdev_port_same_parent_id(priv->netdev, rt->dst.dev))
		*out_dev = uplink_rpriv->netdev;
	else
		*out_dev = rt->dst.dev;

	if (!(*out_ttl))
		*out_ttl = ip4_dst_hoplimit(&rt->dst);
	n = dst_neigh_lookup(&rt->dst, &fl4->daddr);
	ip_rt_put(rt);
	if (!n)
		return -ENOMEM;

	*out_n = n;
	return 0;
}

static const char *mlx5e_netdev_kind(struct net_device *dev)
{
	if (dev->rtnl_link_ops)
		return dev->rtnl_link_ops->kind;
	else
		return "";
}

static int mlx5e_route_lookup_ipv6(struct mlx5e_priv *priv,
				   struct net_device *mirred_dev,
				   struct net_device **out_dev,
				   struct flowi6 *fl6,
				   struct neighbour **out_n,
				   u8 *out_ttl)
{
	struct neighbour *n = NULL;
	struct dst_entry *dst;

#if IS_ENABLED(CONFIG_INET) && IS_ENABLED(CONFIG_IPV6)
	struct mlx5e_rep_priv *uplink_rpriv;
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	int ret;

	ret = ipv6_stub->ipv6_dst_lookup(dev_net(mirred_dev), NULL, &dst,
					 fl6);
	if (ret < 0)
		return ret;

	if (!(*out_ttl))
		*out_ttl = ip6_dst_hoplimit(dst);

	uplink_rpriv = mlx5_eswitch_get_uplink_priv(esw, REP_ETH);
	/* if the egress device isn't on the same HW e-switch, we use the uplink */
	if (!switchdev_port_same_parent_id(priv->netdev, dst->dev))
		*out_dev = uplink_rpriv->netdev;
	else
		*out_dev = dst->dev;
#else
	return -EOPNOTSUPP;
#endif

	n = dst_neigh_lookup(dst, &fl6->daddr);
	dst_release(dst);
	if (!n)
		return -ENOMEM;

	*out_n = n;
	return 0;
}

static int mlx5e_gen_vxlan_header(char buf[], struct ip_tunnel_key *tun_key)
{
	__be32 tun_id = tunnel_id_to_key32(tun_key->tun_id);
	struct udphdr *udp = (struct udphdr *)(buf);
	struct vxlanhdr *vxh = (struct vxlanhdr *)
			       ((char *)udp + sizeof(struct udphdr));

	udp->dest = tun_key->tp_dst;
	vxh->vx_flags = VXLAN_HF_VNI;
	vxh->vx_vni = vxlan_vni_field(tun_id);

	return 0;
}

static int mlx5e_gen_ip_tunnel_header(char buf[], __u8 *ip_proto,
				      struct mlx5e_encap_entry *e)
{
	int err = 0;
	struct ip_tunnel_key *key = &e->tun_info.key;

	if (e->tunnel_type == MLX5E_TC_TUNNEL_TYPE_VXLAN) {
		*ip_proto = IPPROTO_UDP;
		err = mlx5e_gen_vxlan_header(buf, key);
	} else {
		pr_warn("mlx5: Cannot generate tunnel header for tunnel type (%d)\n"
			, e->tunnel_type);
		err = -EOPNOTSUPP;
	}

	return err;
}

int mlx5e_tc_tun_create_header_ipv4(struct mlx5e_priv *priv,
				    struct net_device *mirred_dev,
				    struct mlx5e_encap_entry *e)
{
	int max_encap_size = MLX5_CAP_ESW(priv->mdev, max_encap_header_size);
	int ipv4_encap_size = ETH_HLEN +
			      sizeof(struct iphdr) +
			      e->tunnel_hlen;
	struct ip_tunnel_key *tun_key = &e->tun_info.key;
	struct net_device *out_dev;
	struct neighbour *n = NULL;
	struct flowi4 fl4 = {};
	char *encap_header;
	struct ethhdr *eth;
	u8 nud_state, ttl;
	struct iphdr *ip;
	int err;

	if (max_encap_size < ipv4_encap_size) {
		mlx5_core_warn(priv->mdev, "encap size %d too big, max supported is %d\n",
			       ipv4_encap_size, max_encap_size);
		return -EOPNOTSUPP;
	}

	encap_header = kzalloc(ipv4_encap_size, GFP_KERNEL);
	if (!encap_header)
		return -ENOMEM;

	/* add the IP fields */
	fl4.flowi4_tos = tun_key->tos;
	fl4.daddr = tun_key->u.ipv4.dst;
	fl4.saddr = tun_key->u.ipv4.src;
	ttl = tun_key->ttl;

	err = mlx5e_route_lookup_ipv4(priv, mirred_dev, &out_dev,
				      &fl4, &n, &ttl);
	if (err)
		goto free_encap;

	/* used by mlx5e_detach_encap to lookup a neigh hash table
	 * entry in the neigh hash table when a user deletes a rule
	 */
	e->m_neigh.dev = n->dev;
	e->m_neigh.family = n->ops->family;
	memcpy(&e->m_neigh.dst_ip, n->primary_key, n->tbl->key_len);
	e->out_dev = out_dev;

	/* It's important to add the neigh to the hash table before checking
	 * the neigh validity state. So if we'll get a notification, in case the
	 * neigh changes it's validity state, we would find the relevant neigh
	 * in the hash.
	 */
	err = mlx5e_rep_encap_entry_attach(netdev_priv(out_dev), e);
	if (err)
		goto free_encap;

	read_lock_bh(&n->lock);
	nud_state = n->nud_state;
	ether_addr_copy(e->h_dest, n->ha);
	read_unlock_bh(&n->lock);

	/* add ethernet header */
	eth = (struct ethhdr *)encap_header;
	ether_addr_copy(eth->h_dest, e->h_dest);
	ether_addr_copy(eth->h_source, out_dev->dev_addr);
	eth->h_proto = htons(ETH_P_IP);

	/* add ip header */
	ip = (struct iphdr *)((char *)eth + sizeof(struct ethhdr));
	ip->tos = tun_key->tos;
	ip->version = 0x4;
	ip->ihl = 0x5;
	ip->ttl = ttl;
	ip->daddr = fl4.daddr;
	ip->saddr = fl4.saddr;

	/* add tunneling protocol header */
	err = mlx5e_gen_ip_tunnel_header((char *)ip + sizeof(struct iphdr),
					 &ip->protocol, e);
	if (err)
		goto destroy_neigh_entry;

	e->encap_size = ipv4_encap_size;
	e->encap_header = encap_header;

	if (!(nud_state & NUD_VALID)) {
		neigh_event_send(n, NULL);
		err = -EAGAIN;
		goto out;
	}

	err = mlx5_packet_reformat_alloc(priv->mdev,
					 e->reformat_type,
					 ipv4_encap_size, encap_header,
					 MLX5_FLOW_NAMESPACE_FDB,
					 &e->encap_id);
	if (err)
		goto destroy_neigh_entry;

	e->flags |= MLX5_ENCAP_ENTRY_VALID;
	mlx5e_rep_queue_neigh_stats_work(netdev_priv(out_dev));
	neigh_release(n);
	return err;

destroy_neigh_entry:
	mlx5e_rep_encap_entry_detach(netdev_priv(e->out_dev), e);
free_encap:
	kfree(encap_header);
out:
	if (n)
		neigh_release(n);
	return err;
}

int mlx5e_tc_tun_create_header_ipv6(struct mlx5e_priv *priv,
				    struct net_device *mirred_dev,
				    struct mlx5e_encap_entry *e)
{
	int max_encap_size = MLX5_CAP_ESW(priv->mdev, max_encap_header_size);
	int ipv6_encap_size = ETH_HLEN +
			      sizeof(struct ipv6hdr) +
			      e->tunnel_hlen;
	struct ip_tunnel_key *tun_key = &e->tun_info.key;
	struct net_device *out_dev;
	struct neighbour *n = NULL;
	struct flowi6 fl6 = {};
	struct ipv6hdr *ip6h;
	char *encap_header;
	struct ethhdr *eth;
	u8 nud_state, ttl;
	int err;

	if (max_encap_size < ipv6_encap_size) {
		mlx5_core_warn(priv->mdev, "encap size %d too big, max supported is %d\n",
			       ipv6_encap_size, max_encap_size);
		return -EOPNOTSUPP;
	}

	encap_header = kzalloc(ipv6_encap_size, GFP_KERNEL);
	if (!encap_header)
		return -ENOMEM;

	ttl = tun_key->ttl;

	fl6.flowlabel = ip6_make_flowinfo(RT_TOS(tun_key->tos), tun_key->label);
	fl6.daddr = tun_key->u.ipv6.dst;
	fl6.saddr = tun_key->u.ipv6.src;

	err = mlx5e_route_lookup_ipv6(priv, mirred_dev, &out_dev,
				      &fl6, &n, &ttl);
	if (err)
		goto free_encap;

	/* used by mlx5e_detach_encap to lookup a neigh hash table
	 * entry in the neigh hash table when a user deletes a rule
	 */
	e->m_neigh.dev = n->dev;
	e->m_neigh.family = n->ops->family;
	memcpy(&e->m_neigh.dst_ip, n->primary_key, n->tbl->key_len);
	e->out_dev = out_dev;

	/* It's importent to add the neigh to the hash table before checking
	 * the neigh validity state. So if we'll get a notification, in case the
	 * neigh changes it's validity state, we would find the relevant neigh
	 * in the hash.
	 */
	err = mlx5e_rep_encap_entry_attach(netdev_priv(out_dev), e);
	if (err)
		goto free_encap;

	read_lock_bh(&n->lock);
	nud_state = n->nud_state;
	ether_addr_copy(e->h_dest, n->ha);
	read_unlock_bh(&n->lock);

	/* add ethernet header */
	eth = (struct ethhdr *)encap_header;
	ether_addr_copy(eth->h_dest, e->h_dest);
	ether_addr_copy(eth->h_source, out_dev->dev_addr);
	eth->h_proto = htons(ETH_P_IPV6);

	/* add ip header */
	ip6h = (struct ipv6hdr *)((char *)eth + sizeof(struct ethhdr));
	ip6_flow_hdr(ip6h, tun_key->tos, 0);
	/* the HW fills up ipv6 payload len */
	ip6h->hop_limit   = ttl;
	ip6h->daddr	  = fl6.daddr;
	ip6h->saddr	  = fl6.saddr;

	/* add tunneling protocol header */
	err = mlx5e_gen_ip_tunnel_header((char *)ip6h + sizeof(struct ipv6hdr),
					 &ip6h->nexthdr, e);
	if (err)
		goto destroy_neigh_entry;

	e->encap_size = ipv6_encap_size;
	e->encap_header = encap_header;

	if (!(nud_state & NUD_VALID)) {
		neigh_event_send(n, NULL);
		err = -EAGAIN;
		goto out;
	}

	err = mlx5_packet_reformat_alloc(priv->mdev,
					 e->reformat_type,
					 ipv6_encap_size, encap_header,
					 MLX5_FLOW_NAMESPACE_FDB,
					 &e->encap_id);
	if (err)
		goto destroy_neigh_entry;

	e->flags |= MLX5_ENCAP_ENTRY_VALID;
	mlx5e_rep_queue_neigh_stats_work(netdev_priv(out_dev));
	neigh_release(n);
	return err;

destroy_neigh_entry:
	mlx5e_rep_encap_entry_detach(netdev_priv(e->out_dev), e);
free_encap:
	kfree(encap_header);
out:
	if (n)
		neigh_release(n);
	return err;
}

int mlx5e_tc_tun_get_type(struct net_device *tunnel_dev)
{
	if (netif_is_vxlan(tunnel_dev))
		return MLX5E_TC_TUNNEL_TYPE_VXLAN;
	else
		return MLX5E_TC_TUNNEL_TYPE_UNKNOWN;
}

bool mlx5e_tc_tun_device_to_offload(struct mlx5e_priv *priv,
				    struct net_device *netdev)
{
	int tunnel_type = mlx5e_tc_tun_get_type(netdev);

	if (tunnel_type == MLX5E_TC_TUNNEL_TYPE_VXLAN &&
	    MLX5_CAP_ESW(priv->mdev, vxlan_encap_decap))
		return true;
	else
		return false;
}

int mlx5e_tc_tun_init_encap_attr(struct net_device *tunnel_dev,
				 struct mlx5e_priv *priv,
				 struct mlx5e_encap_entry *e,
				 struct netlink_ext_ack *extack)
{
	e->tunnel_type = mlx5e_tc_tun_get_type(tunnel_dev);

	if (e->tunnel_type == MLX5E_TC_TUNNEL_TYPE_VXLAN) {
		int dst_port =  be16_to_cpu(e->tun_info.key.tp_dst);

		if (!mlx5_vxlan_lookup_port(priv->mdev->vxlan, dst_port)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "vxlan udp dport was not registered with the HW");
			netdev_warn(priv->netdev,
				    "%d isn't an offloaded vxlan udp dport\n",
				    dst_port);
			return -EOPNOTSUPP;
		}
		e->reformat_type = MLX5_REFORMAT_TYPE_L2_TO_VXLAN;
		e->tunnel_hlen = VXLAN_HLEN;
	} else {
		e->reformat_type = -1;
		e->tunnel_hlen = -1;
		return -EOPNOTSUPP;
	}
	return 0;
}

static int mlx5e_tc_tun_parse_vxlan(struct mlx5e_priv *priv,
				    struct mlx5_flow_spec *spec,
				    struct tc_cls_flower_offload *f,
				    void *headers_c,
				    void *headers_v)
{
	struct netlink_ext_ack *extack = f->common.extack;
	struct flow_dissector_key_ports *key =
		skb_flow_dissector_target(f->dissector,
					  FLOW_DISSECTOR_KEY_ENC_PORTS,
					  f->key);
	struct flow_dissector_key_ports *mask =
		skb_flow_dissector_target(f->dissector,
					  FLOW_DISSECTOR_KEY_ENC_PORTS,
					  f->mask);
	void *misc_c = MLX5_ADDR_OF(fte_match_param,
				    spec->match_criteria,
				    misc_parameters);
	void *misc_v = MLX5_ADDR_OF(fte_match_param,
				    spec->match_value,
				    misc_parameters);

	/* Full udp dst port must be given */
	if (!dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_ENC_PORTS) ||
	    memchr_inv(&mask->dst, 0xff, sizeof(mask->dst))) {
		NL_SET_ERR_MSG_MOD(extack,
				   "VXLAN decap filter must include enc_dst_port condition");
		netdev_warn(priv->netdev,
			    "VXLAN decap filter must include enc_dst_port condition\n");
		return -EOPNOTSUPP;
	}

	/* udp dst port must be knonwn as a VXLAN port */
	if (!mlx5_vxlan_lookup_port(priv->mdev->vxlan, be16_to_cpu(key->dst))) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Matched UDP port is not registered as a VXLAN port");
		netdev_warn(priv->netdev,
			    "UDP port %d is not registered as a VXLAN port\n",
			    be16_to_cpu(key->dst));
		return -EOPNOTSUPP;
	}

	/* dst UDP port is valid here */
	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, ip_protocol);
	MLX5_SET(fte_match_set_lyr_2_4, headers_v, ip_protocol, IPPROTO_UDP);

	MLX5_SET(fte_match_set_lyr_2_4, headers_c, udp_dport, ntohs(mask->dst));
	MLX5_SET(fte_match_set_lyr_2_4, headers_v, udp_dport, ntohs(key->dst));

	MLX5_SET(fte_match_set_lyr_2_4, headers_c, udp_sport, ntohs(mask->src));
	MLX5_SET(fte_match_set_lyr_2_4, headers_v, udp_sport, ntohs(key->src));

	/* match on VNI */
	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_ENC_KEYID)) {
		struct flow_dissector_key_keyid *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ENC_KEYID,
						  f->key);
		struct flow_dissector_key_keyid *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ENC_KEYID,
						  f->mask);
		MLX5_SET(fte_match_set_misc, misc_c, vxlan_vni,
			 be32_to_cpu(mask->keyid));
		MLX5_SET(fte_match_set_misc, misc_v, vxlan_vni,
			 be32_to_cpu(key->keyid));
	}
	return 0;
}

int mlx5e_tc_tun_parse(struct net_device *filter_dev,
		       struct mlx5e_priv *priv,
		       struct mlx5_flow_spec *spec,
		       struct tc_cls_flower_offload *f,
		       void *headers_c,
		       void *headers_v)
{
	int tunnel_type;
	int err = 0;

	tunnel_type = mlx5e_tc_tun_get_type(filter_dev);
	if (tunnel_type == MLX5E_TC_TUNNEL_TYPE_VXLAN) {
		err = mlx5e_tc_tun_parse_vxlan(priv, spec, f,
					       headers_c, headers_v);
	} else {
		netdev_warn(priv->netdev,
			    "decapsulation offload is not supported for %s net device (%d)\n",
			    mlx5e_netdev_kind(filter_dev), tunnel_type);
		return -EOPNOTSUPP;
	}
	return err;
}
