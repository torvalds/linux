/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2018 Mellanox Technologies. */

#include <net/vxlan.h>
#include <net/gre.h>
#include "lib/vxlan.h"
#include "en/tc_tun.h"

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
	uplink_upper = netdev_master_upper_dev_get(uplink_dev);
	dst_is_lag_dev = (uplink_upper &&
			  netif_is_lag_master(uplink_upper) &&
			  real_dev == uplink_upper &&
			  mlx5_lag_is_sriov(priv->mdev));

	/* if the egress device isn't on the same HW e-switch or
	 * it's a LAG device, use the uplink
	 */
	if (!netdev_port_same_parent_id(priv->netdev, real_dev) ||
	    dst_is_lag_dev) {
		*route_dev = dev;
		*out_dev = uplink_dev;
	} else {
		*route_dev = dev;
		if (is_vlan_dev(*route_dev))
			*out_dev = uplink_dev;
		else if (mlx5e_eswitch_rep(dev))
			*out_dev = *route_dev;
		else
			return -EOPNOTSUPP;
	}

	if (!(mlx5e_eswitch_rep(*out_dev) &&
	      mlx5e_is_uplink_rep(netdev_priv(*out_dev))))
		return -EOPNOTSUPP;

	return 0;
}

static int mlx5e_route_lookup_ipv4(struct mlx5e_priv *priv,
				   struct net_device *mirred_dev,
				   struct net_device **out_dev,
				   struct net_device **route_dev,
				   struct flowi4 *fl4,
				   struct neighbour **out_n,
				   u8 *out_ttl)
{
	struct rtable *rt;
	struct neighbour *n = NULL;

#if IS_ENABLED(CONFIG_INET)
	struct mlx5_core_dev *mdev = priv->mdev;
	struct net_device *uplink_dev;
	int ret;

	if (mlx5_lag_is_multipath(mdev)) {
		struct mlx5_eswitch *esw = mdev->priv.eswitch;

		uplink_dev = mlx5_eswitch_uplink_get_proto_dev(esw, REP_ETH);
		fl4->flowi4_oif = uplink_dev->ifindex;
	}

	rt = ip_route_output_key(dev_net(mirred_dev), fl4);
	ret = PTR_ERR_OR_ZERO(rt);
	if (ret)
		return ret;

	if (mlx5_lag_is_multipath(mdev) && rt->rt_gw_family != AF_INET)
		return -ENETUNREACH;
#else
	return -EOPNOTSUPP;
#endif

	ret = get_route_and_out_devs(priv, rt->dst.dev, route_dev, out_dev);
	if (ret < 0)
		return ret;

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
		return "unknown";
}

static int mlx5e_route_lookup_ipv6(struct mlx5e_priv *priv,
				   struct net_device *mirred_dev,
				   struct net_device **out_dev,
				   struct net_device **route_dev,
				   struct flowi6 *fl6,
				   struct neighbour **out_n,
				   u8 *out_ttl)
{
	struct neighbour *n = NULL;
	struct dst_entry *dst;

#if IS_ENABLED(CONFIG_INET) && IS_ENABLED(CONFIG_IPV6)
	int ret;

	ret = ipv6_stub->ipv6_dst_lookup(dev_net(mirred_dev), NULL, &dst,
					 fl6);
	if (ret < 0)
		return ret;

	if (!(*out_ttl))
		*out_ttl = ip6_dst_hoplimit(dst);

	ret = get_route_and_out_devs(priv, dst->dev, route_dev, out_dev);
	if (ret < 0)
		return ret;
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

static int mlx5e_gen_gre_header(char buf[], struct ip_tunnel_key *tun_key)
{
	__be32 tun_id = tunnel_id_to_key32(tun_key->tun_id);
	int hdr_len;
	struct gre_base_hdr *greh = (struct gre_base_hdr *)(buf);

	/* the HW does not calculate GRE csum or sequences */
	if (tun_key->tun_flags & (TUNNEL_CSUM | TUNNEL_SEQ))
		return -EOPNOTSUPP;

	greh->protocol = htons(ETH_P_TEB);

	/* GRE key */
	hdr_len = gre_calc_hlen(tun_key->tun_flags);
	greh->flags = gre_tnl_flags_to_gre_flags(tun_key->tun_flags);
	if (tun_key->tun_flags & TUNNEL_KEY) {
		__be32 *ptr = (__be32 *)(((u8 *)greh) + hdr_len - 4);

		*ptr = tun_id;
	}

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
	} else if  (e->tunnel_type == MLX5E_TC_TUNNEL_TYPE_GRETAP) {
		*ip_proto = IPPROTO_GRE;
		err = mlx5e_gen_gre_header(buf, key);
	} else {
		pr_warn("mlx5: Cannot generate tunnel header for tunnel type (%d)\n"
			, e->tunnel_type);
		err = -EOPNOTSUPP;
	}

	return err;
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
	struct ip_tunnel_key *tun_key = &e->tun_info.key;
	struct net_device *out_dev, *route_dev;
	struct neighbour *n = NULL;
	struct flowi4 fl4 = {};
	int ipv4_encap_size;
	char *encap_header;
	u8 nud_state, ttl;
	struct iphdr *ip;
	int err;

	/* add the IP fields */
	fl4.flowi4_tos = tun_key->tos;
	fl4.daddr = tun_key->u.ipv4.dst;
	fl4.saddr = tun_key->u.ipv4.src;
	ttl = tun_key->ttl;

	err = mlx5e_route_lookup_ipv4(priv, mirred_dev, &out_dev, &route_dev,
				      &fl4, &n, &ttl);
	if (err)
		return err;

	ipv4_encap_size =
		(is_vlan_dev(route_dev) ? VLAN_ETH_HLEN : ETH_HLEN) +
		sizeof(struct iphdr) +
		e->tunnel_hlen;

	if (max_encap_size < ipv4_encap_size) {
		mlx5_core_warn(priv->mdev, "encap size %d too big, max supported is %d\n",
			       ipv4_encap_size, max_encap_size);
		return -EOPNOTSUPP;
	}

	encap_header = kzalloc(ipv4_encap_size, GFP_KERNEL);
	if (!encap_header)
		return -ENOMEM;

	/* used by mlx5e_detach_encap to lookup a neigh hash table
	 * entry in the neigh hash table when a user deletes a rule
	 */
	e->m_neigh.dev = n->dev;
	e->m_neigh.family = n->ops->family;
	memcpy(&e->m_neigh.dst_ip, n->primary_key, n->tbl->key_len);
	e->out_dev = out_dev;
	e->route_dev = route_dev;

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
	ip = (struct iphdr *)gen_eth_tnl_hdr(encap_header, route_dev, e,
					     ETH_P_IP);

	/* add ip header */
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
		/* the encap entry will be made valid on neigh update event
		 * and not used before that.
		 */
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
	struct ip_tunnel_key *tun_key = &e->tun_info.key;
	struct net_device *out_dev, *route_dev;
	struct neighbour *n = NULL;
	struct flowi6 fl6 = {};
	struct ipv6hdr *ip6h;
	int ipv6_encap_size;
	char *encap_header;
	u8 nud_state, ttl;
	int err;

	ttl = tun_key->ttl;

	fl6.flowlabel = ip6_make_flowinfo(RT_TOS(tun_key->tos), tun_key->label);
	fl6.daddr = tun_key->u.ipv6.dst;
	fl6.saddr = tun_key->u.ipv6.src;

	err = mlx5e_route_lookup_ipv6(priv, mirred_dev, &out_dev, &route_dev,
				      &fl6, &n, &ttl);
	if (err)
		return err;

	ipv6_encap_size =
		(is_vlan_dev(route_dev) ? VLAN_ETH_HLEN : ETH_HLEN) +
		sizeof(struct ipv6hdr) +
		e->tunnel_hlen;

	if (max_encap_size < ipv6_encap_size) {
		mlx5_core_warn(priv->mdev, "encap size %d too big, max supported is %d\n",
			       ipv6_encap_size, max_encap_size);
		return -EOPNOTSUPP;
	}

	encap_header = kzalloc(ipv6_encap_size, GFP_KERNEL);
	if (!encap_header)
		return -ENOMEM;

	/* used by mlx5e_detach_encap to lookup a neigh hash table
	 * entry in the neigh hash table when a user deletes a rule
	 */
	e->m_neigh.dev = n->dev;
	e->m_neigh.family = n->ops->family;
	memcpy(&e->m_neigh.dst_ip, n->primary_key, n->tbl->key_len);
	e->out_dev = out_dev;
	e->route_dev = route_dev;

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
	ip6h = (struct ipv6hdr *)gen_eth_tnl_hdr(encap_header, route_dev, e,
						 ETH_P_IPV6);

	/* add ip header */
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
		/* the encap entry will be made valid on neigh update event
		 * and not used before that.
		 */
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
	else if (netif_is_gretap(tunnel_dev) ||
		 netif_is_ip6gretap(tunnel_dev))
		return MLX5E_TC_TUNNEL_TYPE_GRETAP;
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
	else if (tunnel_type == MLX5E_TC_TUNNEL_TYPE_GRETAP &&
		 MLX5_CAP_ESW(priv->mdev, nvgre_encap_decap))
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
	} else if (e->tunnel_type == MLX5E_TC_TUNNEL_TYPE_GRETAP) {
		e->reformat_type = MLX5_REFORMAT_TYPE_L2_TO_NVGRE;
		e->tunnel_hlen = gre_calc_hlen(e->tun_info.key.tun_flags);
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
	struct flow_rule *rule = tc_cls_flower_offload_flow_rule(f);
	struct netlink_ext_ack *extack = f->common.extack;
	void *misc_c = MLX5_ADDR_OF(fte_match_param,
				    spec->match_criteria,
				    misc_parameters);
	void *misc_v = MLX5_ADDR_OF(fte_match_param,
				    spec->match_value,
				    misc_parameters);
	struct flow_match_ports enc_ports;

	flow_rule_match_enc_ports(rule, &enc_ports);

	/* Full udp dst port must be given */
	if (!flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_PORTS) ||
	    memchr_inv(&enc_ports.mask->dst, 0xff, sizeof(enc_ports.mask->dst))) {
		NL_SET_ERR_MSG_MOD(extack,
				   "VXLAN decap filter must include enc_dst_port condition");
		netdev_warn(priv->netdev,
			    "VXLAN decap filter must include enc_dst_port condition\n");
		return -EOPNOTSUPP;
	}

	/* udp dst port must be knonwn as a VXLAN port */
	if (!mlx5_vxlan_lookup_port(priv->mdev->vxlan, be16_to_cpu(enc_ports.key->dst))) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Matched UDP port is not registered as a VXLAN port");
		netdev_warn(priv->netdev,
			    "UDP port %d is not registered as a VXLAN port\n",
			    be16_to_cpu(enc_ports.key->dst));
		return -EOPNOTSUPP;
	}

	/* dst UDP port is valid here */
	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, ip_protocol);
	MLX5_SET(fte_match_set_lyr_2_4, headers_v, ip_protocol, IPPROTO_UDP);

	MLX5_SET(fte_match_set_lyr_2_4, headers_c, udp_dport,
		 ntohs(enc_ports.mask->dst));
	MLX5_SET(fte_match_set_lyr_2_4, headers_v, udp_dport,
		 ntohs(enc_ports.key->dst));

	MLX5_SET(fte_match_set_lyr_2_4, headers_c, udp_sport,
		 ntohs(enc_ports.mask->src));
	MLX5_SET(fte_match_set_lyr_2_4, headers_v, udp_sport,
		 ntohs(enc_ports.key->src));

	/* match on VNI */
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_KEYID)) {
		struct flow_match_enc_keyid enc_keyid;

		flow_rule_match_enc_keyid(rule, &enc_keyid);

		MLX5_SET(fte_match_set_misc, misc_c, vxlan_vni,
			 be32_to_cpu(enc_keyid.mask->keyid));
		MLX5_SET(fte_match_set_misc, misc_v, vxlan_vni,
			 be32_to_cpu(enc_keyid.key->keyid));
	}
	return 0;
}

static int mlx5e_tc_tun_parse_gretap(struct mlx5e_priv *priv,
				     struct mlx5_flow_spec *spec,
				     struct tc_cls_flower_offload *f,
				     void *outer_headers_c,
				     void *outer_headers_v)
{
	void *misc_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				    misc_parameters);
	void *misc_v = MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    misc_parameters);
	struct flow_rule *rule = tc_cls_flower_offload_flow_rule(f);

	if (!MLX5_CAP_ESW(priv->mdev, nvgre_encap_decap)) {
		NL_SET_ERR_MSG_MOD(f->common.extack,
				   "GRE HW offloading is not supported");
		netdev_warn(priv->netdev, "GRE HW offloading is not supported\n");
		return -EOPNOTSUPP;
	}

	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, ip_protocol);
	MLX5_SET(fte_match_set_lyr_2_4, outer_headers_v,
		 ip_protocol, IPPROTO_GRE);

	/* gre protocol*/
	MLX5_SET_TO_ONES(fte_match_set_misc, misc_c, gre_protocol);
	MLX5_SET(fte_match_set_misc, misc_v, gre_protocol, ETH_P_TEB);

	/* gre key */
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_KEYID)) {
		struct flow_match_enc_keyid enc_keyid;

		flow_rule_match_enc_keyid(rule, &enc_keyid);
		MLX5_SET(fte_match_set_misc, misc_c,
			 gre_key.key, be32_to_cpu(enc_keyid.mask->keyid));
		MLX5_SET(fte_match_set_misc, misc_v,
			 gre_key.key, be32_to_cpu(enc_keyid.key->keyid));
	}

	return 0;
}

int mlx5e_tc_tun_parse(struct net_device *filter_dev,
		       struct mlx5e_priv *priv,
		       struct mlx5_flow_spec *spec,
		       struct tc_cls_flower_offload *f,
		       void *headers_c,
		       void *headers_v, u8 *match_level)
{
	int tunnel_type;
	int err = 0;

	tunnel_type = mlx5e_tc_tun_get_type(filter_dev);
	if (tunnel_type == MLX5E_TC_TUNNEL_TYPE_VXLAN) {
		*match_level = MLX5_MATCH_L4;
		err = mlx5e_tc_tun_parse_vxlan(priv, spec, f,
					       headers_c, headers_v);
	} else if (tunnel_type == MLX5E_TC_TUNNEL_TYPE_GRETAP) {
		*match_level = MLX5_MATCH_L3;
		err = mlx5e_tc_tun_parse_gretap(priv, spec, f,
						headers_c, headers_v);
	} else {
		netdev_warn(priv->netdev,
			    "decapsulation offload is not supported for %s (kind: \"%s\")\n",
			    netdev_name(filter_dev),
			    mlx5e_netdev_kind(filter_dev));

		return -EOPNOTSUPP;
	}
	return err;
}
