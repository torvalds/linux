/*
 * GENEVE: Generic Network Virtualization Encapsulation
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/hash.h>
#include <net/rtnetlink.h>
#include <net/geneve.h>

#define GENEVE_NETDEV_VER	"0.6"

#define GENEVE_UDP_PORT		6081

#define GENEVE_N_VID		(1u << 24)
#define GENEVE_VID_MASK		(GENEVE_N_VID - 1)

#define VNI_HASH_BITS		10
#define VNI_HASH_SIZE		(1<<VNI_HASH_BITS)

static bool log_ecn_error = true;
module_param(log_ecn_error, bool, 0644);
MODULE_PARM_DESC(log_ecn_error, "Log packets received with corrupted ECN");

/* per-network namespace private data for this module */
struct geneve_net {
	struct list_head  geneve_list;
	struct hlist_head vni_list[VNI_HASH_SIZE];
};

/* Pseudo network device */
struct geneve_dev {
	struct hlist_node  hlist;	/* vni hash table */
	struct net	   *net;	/* netns for packet i/o */
	struct net_device  *dev;	/* netdev for geneve tunnel */
	struct geneve_sock *sock;	/* socket used for geneve tunnel */
	u8                 vni[3];	/* virtual network ID for tunnel */
	u8                 ttl;		/* TTL override */
	u8                 tos;		/* TOS override */
	struct sockaddr_in remote;	/* IPv4 address for link partner */
	struct list_head   next;	/* geneve's per namespace list */
};

static int geneve_net_id;

static inline __u32 geneve_net_vni_hash(u8 vni[3])
{
	__u32 vnid;

	vnid = (vni[0] << 16) | (vni[1] << 8) | vni[2];
	return hash_32(vnid, VNI_HASH_BITS);
}

/* geneve receive/decap routine */
static void geneve_rx(struct geneve_sock *gs, struct sk_buff *skb)
{
	struct genevehdr *gnvh = geneve_hdr(skb);
	struct geneve_dev *dummy, *geneve = NULL;
	struct geneve_net *gn;
	struct iphdr *iph = NULL;
	struct pcpu_sw_netstats *stats;
	struct hlist_head *vni_list_head;
	int err = 0;
	__u32 hash;

	iph = ip_hdr(skb); /* Still outer IP header... */

	gn = gs->rcv_data;

	/* Find the device for this VNI */
	hash = geneve_net_vni_hash(gnvh->vni);
	vni_list_head = &gn->vni_list[hash];
	hlist_for_each_entry_rcu(dummy, vni_list_head, hlist) {
		if (!memcmp(gnvh->vni, dummy->vni, sizeof(dummy->vni)) &&
		    iph->saddr == dummy->remote.sin_addr.s_addr) {
			geneve = dummy;
			break;
		}
	}
	if (!geneve)
		goto drop;

	/* Drop packets w/ critical options,
	 * since we don't support any...
	 */
	if (gnvh->critical)
		goto drop;

	skb_reset_mac_header(skb);
	skb_scrub_packet(skb, !net_eq(geneve->net, dev_net(geneve->dev)));
	skb->protocol = eth_type_trans(skb, geneve->dev);
	skb_postpull_rcsum(skb, eth_hdr(skb), ETH_HLEN);

	/* Ignore packet loops (and multicast echo) */
	if (ether_addr_equal(eth_hdr(skb)->h_source, geneve->dev->dev_addr))
		goto drop;

	skb_reset_network_header(skb);

	iph = ip_hdr(skb); /* Now inner IP header... */
	err = IP_ECN_decapsulate(iph, skb);

	if (unlikely(err)) {
		if (log_ecn_error)
			net_info_ratelimited("non-ECT from %pI4 with TOS=%#x\n",
					     &iph->saddr, iph->tos);
		if (err > 1) {
			++geneve->dev->stats.rx_frame_errors;
			++geneve->dev->stats.rx_errors;
			goto drop;
		}
	}

	stats = this_cpu_ptr(geneve->dev->tstats);
	u64_stats_update_begin(&stats->syncp);
	stats->rx_packets++;
	stats->rx_bytes += skb->len;
	u64_stats_update_end(&stats->syncp);

	netif_rx(skb);

	return;
drop:
	/* Consume bad packet */
	kfree_skb(skb);
}

/* Setup stats when device is created */
static int geneve_init(struct net_device *dev)
{
	dev->tstats = netdev_alloc_pcpu_stats(struct pcpu_sw_netstats);
	if (!dev->tstats)
		return -ENOMEM;

	return 0;
}

static void geneve_uninit(struct net_device *dev)
{
	free_percpu(dev->tstats);
}

static int geneve_open(struct net_device *dev)
{
	struct geneve_dev *geneve = netdev_priv(dev);
	struct net *net = geneve->net;
	struct geneve_net *gn = net_generic(geneve->net, geneve_net_id);
	struct geneve_sock *gs;

	gs = geneve_sock_add(net, htons(GENEVE_UDP_PORT), geneve_rx, gn,
	                     false, false);
	if (IS_ERR(gs))
		return PTR_ERR(gs);

	geneve->sock = gs;

	return 0;
}

static int geneve_stop(struct net_device *dev)
{
	struct geneve_dev *geneve = netdev_priv(dev);
	struct geneve_sock *gs = geneve->sock;

	geneve_sock_release(gs);

	return 0;
}

static netdev_tx_t geneve_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct geneve_dev *geneve = netdev_priv(dev);
	struct geneve_sock *gs = geneve->sock;
	struct rtable *rt = NULL;
	const struct iphdr *iip; /* interior IP header */
	struct flowi4 fl4;
	int err;
	__be16 sport;
	__u8 tos, ttl;

	iip = ip_hdr(skb);

	skb_reset_mac_header(skb);

	/* TODO: port min/max limits should be configurable */
	sport = udp_flow_src_port(dev_net(dev), skb, 0, 0, true);

	tos = geneve->tos;
	if (tos == 1)
		tos = ip_tunnel_get_dsfield(iip, skb);

	memset(&fl4, 0, sizeof(fl4));
	fl4.flowi4_tos = RT_TOS(tos);
	fl4.daddr = geneve->remote.sin_addr.s_addr;
	rt = ip_route_output_key(geneve->net, &fl4);
	if (IS_ERR(rt)) {
		netdev_dbg(dev, "no route to %pI4\n", &fl4.daddr);
		dev->stats.tx_carrier_errors++;
		goto tx_error;
	}
	if (rt->dst.dev == dev) { /* is this necessary? */
		netdev_dbg(dev, "circular route to %pI4\n", &fl4.daddr);
		dev->stats.collisions++;
		goto rt_tx_error;
	}

	tos = ip_tunnel_ecn_encap(tos, iip, skb);

	ttl = geneve->ttl;
	if (!ttl && IN_MULTICAST(ntohl(fl4.daddr)))
		ttl = 1;

	ttl = ttl ? : ip4_dst_hoplimit(&rt->dst);

	/* no need to handle local destination and encap bypass...yet... */

	err = geneve_xmit_skb(gs, rt, skb, fl4.saddr, fl4.daddr,
	                      tos, ttl, 0, sport, htons(GENEVE_UDP_PORT), 0,
	                      geneve->vni, 0, NULL, false,
	                      !net_eq(geneve->net, dev_net(geneve->dev)));
	if (err < 0)
		ip_rt_put(rt);

	iptunnel_xmit_stats(err, &dev->stats, dev->tstats);

	return NETDEV_TX_OK;

rt_tx_error:
	ip_rt_put(rt);
tx_error:
	dev->stats.tx_errors++;
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops geneve_netdev_ops = {
	.ndo_init		= geneve_init,
	.ndo_uninit		= geneve_uninit,
	.ndo_open		= geneve_open,
	.ndo_stop		= geneve_stop,
	.ndo_start_xmit		= geneve_xmit,
	.ndo_get_stats64	= ip_tunnel_get_stats64,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
};

static void geneve_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->version, GENEVE_NETDEV_VER, sizeof(drvinfo->version));
	strlcpy(drvinfo->driver, "geneve", sizeof(drvinfo->driver));
}

static const struct ethtool_ops geneve_ethtool_ops = {
	.get_drvinfo	= geneve_get_drvinfo,
	.get_link	= ethtool_op_get_link,
};

/* Info for udev, that this is a virtual tunnel endpoint */
static struct device_type geneve_type = {
	.name = "geneve",
};

/* Initialize the device structure. */
static void geneve_setup(struct net_device *dev)
{
	ether_setup(dev);

	dev->netdev_ops = &geneve_netdev_ops;
	dev->ethtool_ops = &geneve_ethtool_ops;
	dev->destructor = free_netdev;

	SET_NETDEV_DEVTYPE(dev, &geneve_type);

	dev->tx_queue_len = 0;
	dev->features    |= NETIF_F_LLTX;
	dev->features    |= NETIF_F_SG | NETIF_F_HW_CSUM;
	dev->features    |= NETIF_F_RXCSUM;
	dev->features    |= NETIF_F_GSO_SOFTWARE;

	dev->vlan_features = dev->features;
	dev->features    |= NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_STAG_TX;

	dev->hw_features |= NETIF_F_SG | NETIF_F_HW_CSUM | NETIF_F_RXCSUM;
	dev->hw_features |= NETIF_F_GSO_SOFTWARE;
	dev->hw_features |= NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_STAG_TX;

	netif_keep_dst(dev);
	dev->priv_flags |= IFF_LIVE_ADDR_CHANGE;
}

static const struct nla_policy geneve_policy[IFLA_GENEVE_MAX + 1] = {
	[IFLA_GENEVE_ID]		= { .type = NLA_U32 },
	[IFLA_GENEVE_REMOTE]		= { .len = FIELD_SIZEOF(struct iphdr, daddr) },
	[IFLA_GENEVE_TTL]		= { .type = NLA_U8 },
	[IFLA_GENEVE_TOS]		= { .type = NLA_U8 },
};

static int geneve_validate(struct nlattr *tb[], struct nlattr *data[])
{
	if (tb[IFLA_ADDRESS]) {
		if (nla_len(tb[IFLA_ADDRESS]) != ETH_ALEN)
			return -EINVAL;

		if (!is_valid_ether_addr(nla_data(tb[IFLA_ADDRESS])))
			return -EADDRNOTAVAIL;
	}

	if (!data)
		return -EINVAL;

	if (data[IFLA_GENEVE_ID]) {
		__u32 vni =  nla_get_u32(data[IFLA_GENEVE_ID]);

		if (vni >= GENEVE_VID_MASK)
			return -ERANGE;
	}

	return 0;
}

static int geneve_newlink(struct net *net, struct net_device *dev,
			 struct nlattr *tb[], struct nlattr *data[])
{
	struct geneve_net *gn = net_generic(net, geneve_net_id);
	struct geneve_dev *dummy, *geneve = netdev_priv(dev);
	struct hlist_head *vni_list_head;
	struct sockaddr_in remote;	/* IPv4 address for link partner */
	__u32 vni, hash;
	int err;

	if (!data[IFLA_GENEVE_ID] || !data[IFLA_GENEVE_REMOTE])
		return -EINVAL;

	geneve->net = net;
	geneve->dev = dev;

	vni = nla_get_u32(data[IFLA_GENEVE_ID]);
	geneve->vni[0] = (vni & 0x00ff0000) >> 16;
	geneve->vni[1] = (vni & 0x0000ff00) >> 8;
	geneve->vni[2] =  vni & 0x000000ff;

	geneve->remote.sin_addr.s_addr =
		nla_get_in_addr(data[IFLA_GENEVE_REMOTE]);
	if (IN_MULTICAST(ntohl(geneve->remote.sin_addr.s_addr)))
		return -EINVAL;

	remote = geneve->remote;
	hash = geneve_net_vni_hash(geneve->vni);
	vni_list_head = &gn->vni_list[hash];
	hlist_for_each_entry_rcu(dummy, vni_list_head, hlist) {
		if (!memcmp(geneve->vni, dummy->vni, sizeof(dummy->vni)) &&
		    !memcmp(&remote, &dummy->remote, sizeof(dummy->remote)))
			return -EBUSY;
	}

	if (tb[IFLA_ADDRESS] == NULL)
		eth_hw_addr_random(dev);

	err = register_netdevice(dev);
	if (err)
		return err;

	if (data[IFLA_GENEVE_TTL])
		geneve->ttl = nla_get_u8(data[IFLA_GENEVE_TTL]);

	if (data[IFLA_GENEVE_TOS])
		geneve->tos = nla_get_u8(data[IFLA_GENEVE_TOS]);

	list_add(&geneve->next, &gn->geneve_list);

	hlist_add_head_rcu(&geneve->hlist, &gn->vni_list[hash]);

	return 0;
}

static void geneve_dellink(struct net_device *dev, struct list_head *head)
{
	struct geneve_dev *geneve = netdev_priv(dev);

	if (!hlist_unhashed(&geneve->hlist))
		hlist_del_rcu(&geneve->hlist);

	list_del(&geneve->next);
	unregister_netdevice_queue(dev, head);
}

static size_t geneve_get_size(const struct net_device *dev)
{
	return nla_total_size(sizeof(__u32)) +	/* IFLA_GENEVE_ID */
		nla_total_size(sizeof(struct in_addr)) + /* IFLA_GENEVE_REMOTE */
		nla_total_size(sizeof(__u8)) +  /* IFLA_GENEVE_TTL */
		nla_total_size(sizeof(__u8)) +  /* IFLA_GENEVE_TOS */
		0;
}

static int geneve_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct geneve_dev *geneve = netdev_priv(dev);
	__u32 vni;

	vni = (geneve->vni[0] << 16) | (geneve->vni[1] << 8) | geneve->vni[2];
	if (nla_put_u32(skb, IFLA_GENEVE_ID, vni))
		goto nla_put_failure;

	if (nla_put_in_addr(skb, IFLA_GENEVE_REMOTE,
			    geneve->remote.sin_addr.s_addr))
		goto nla_put_failure;

	if (nla_put_u8(skb, IFLA_GENEVE_TTL, geneve->ttl) ||
	    nla_put_u8(skb, IFLA_GENEVE_TOS, geneve->tos))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static struct rtnl_link_ops geneve_link_ops __read_mostly = {
	.kind		= "geneve",
	.maxtype	= IFLA_GENEVE_MAX,
	.policy		= geneve_policy,
	.priv_size	= sizeof(struct geneve_dev),
	.setup		= geneve_setup,
	.validate	= geneve_validate,
	.newlink	= geneve_newlink,
	.dellink	= geneve_dellink,
	.get_size	= geneve_get_size,
	.fill_info	= geneve_fill_info,
};

static __net_init int geneve_init_net(struct net *net)
{
	struct geneve_net *gn = net_generic(net, geneve_net_id);
	unsigned int h;

	INIT_LIST_HEAD(&gn->geneve_list);

	for (h = 0; h < VNI_HASH_SIZE; ++h)
		INIT_HLIST_HEAD(&gn->vni_list[h]);

	return 0;
}

static void __net_exit geneve_exit_net(struct net *net)
{
	struct geneve_net *gn = net_generic(net, geneve_net_id);
	struct geneve_dev *geneve, *next;
	struct net_device *dev, *aux;
	LIST_HEAD(list);

	rtnl_lock();

	/* gather any geneve devices that were moved into this ns */
	for_each_netdev_safe(net, dev, aux)
		if (dev->rtnl_link_ops == &geneve_link_ops)
			unregister_netdevice_queue(dev, &list);

	/* now gather any other geneve devices that were created in this ns */
	list_for_each_entry_safe(geneve, next, &gn->geneve_list, next) {
		/* If geneve->dev is in the same netns, it was already added
		 * to the list by the previous loop.
		 */
		if (!net_eq(dev_net(geneve->dev), net))
			unregister_netdevice_queue(geneve->dev, &list);
	}

	/* unregister the devices gathered above */
	unregister_netdevice_many(&list);
	rtnl_unlock();
}

static struct pernet_operations geneve_net_ops = {
	.init = geneve_init_net,
	.exit = geneve_exit_net,
	.id   = &geneve_net_id,
	.size = sizeof(struct geneve_net),
};

static int __init geneve_init_module(void)
{
	int rc;

	rc = register_pernet_subsys(&geneve_net_ops);
	if (rc)
		goto out1;

	rc = rtnl_link_register(&geneve_link_ops);
	if (rc)
		goto out2;

	return 0;
out2:
	unregister_pernet_subsys(&geneve_net_ops);
out1:
	return rc;
}
late_initcall(geneve_init_module);

static void __exit geneve_cleanup_module(void)
{
	rtnl_link_unregister(&geneve_link_ops);
	unregister_pernet_subsys(&geneve_net_ops);
}
module_exit(geneve_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_VERSION(GENEVE_NETDEV_VER);
MODULE_AUTHOR("John W. Linville <linville@tuxdriver.com>");
MODULE_DESCRIPTION("Interface driver for GENEVE encapsulated traffic");
MODULE_ALIAS_RTNL_LINK("geneve");
