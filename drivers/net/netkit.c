// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2023 Isovalent */

#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/etherdevice.h>
#include <linux/filter.h>
#include <linux/netfilter_netdev.h>
#include <linux/bpf_mprog.h>
#include <linux/indirect_call_wrapper.h>

#include <net/netkit.h>
#include <net/dst.h>
#include <net/tcx.h>

#define DRV_NAME "netkit"

struct netkit {
	/* Needed in fast-path */
	struct net_device __rcu *peer;
	struct bpf_mprog_entry __rcu *active;
	enum netkit_action policy;
	struct bpf_mprog_bundle	bundle;

	/* Needed in slow-path */
	enum netkit_mode mode;
	bool primary;
	u32 headroom;
};

struct netkit_link {
	struct bpf_link link;
	struct net_device *dev;
	u32 location;
};

static __always_inline int
netkit_run(const struct bpf_mprog_entry *entry, struct sk_buff *skb,
	   enum netkit_action ret)
{
	const struct bpf_mprog_fp *fp;
	const struct bpf_prog *prog;

	bpf_mprog_foreach_prog(entry, fp, prog) {
		bpf_compute_data_pointers(skb);
		ret = bpf_prog_run(prog, skb);
		if (ret != NETKIT_NEXT)
			break;
	}
	return ret;
}

static void netkit_prep_forward(struct sk_buff *skb, bool xnet)
{
	skb_scrub_packet(skb, xnet);
	skb->priority = 0;
	nf_skip_egress(skb, true);
	skb_reset_mac_header(skb);
}

static struct netkit *netkit_priv(const struct net_device *dev)
{
	return netdev_priv(dev);
}

static netdev_tx_t netkit_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bpf_net_context __bpf_net_ctx, *bpf_net_ctx;
	struct netkit *nk = netkit_priv(dev);
	enum netkit_action ret = READ_ONCE(nk->policy);
	netdev_tx_t ret_dev = NET_XMIT_SUCCESS;
	const struct bpf_mprog_entry *entry;
	struct net_device *peer;
	int len = skb->len;

	bpf_net_ctx = bpf_net_ctx_set(&__bpf_net_ctx);
	rcu_read_lock();
	peer = rcu_dereference(nk->peer);
	if (unlikely(!peer || !(peer->flags & IFF_UP) ||
		     !pskb_may_pull(skb, ETH_HLEN) ||
		     skb_orphan_frags(skb, GFP_ATOMIC)))
		goto drop;
	netkit_prep_forward(skb, !net_eq(dev_net(dev), dev_net(peer)));
	eth_skb_pkt_type(skb, peer);
	skb->dev = peer;
	entry = rcu_dereference(nk->active);
	if (entry)
		ret = netkit_run(entry, skb, ret);
	switch (ret) {
	case NETKIT_NEXT:
	case NETKIT_PASS:
		eth_skb_pull_mac(skb);
		skb_postpull_rcsum(skb, eth_hdr(skb), ETH_HLEN);
		if (likely(__netif_rx(skb) == NET_RX_SUCCESS)) {
			dev_sw_netstats_tx_add(dev, 1, len);
			dev_sw_netstats_rx_add(peer, len);
		} else {
			goto drop_stats;
		}
		break;
	case NETKIT_REDIRECT:
		dev_sw_netstats_tx_add(dev, 1, len);
		skb_do_redirect(skb);
		break;
	case NETKIT_DROP:
	default:
drop:
		kfree_skb(skb);
drop_stats:
		dev_core_stats_tx_dropped_inc(dev);
		ret_dev = NET_XMIT_DROP;
		break;
	}
	rcu_read_unlock();
	bpf_net_ctx_clear(bpf_net_ctx);
	return ret_dev;
}

static int netkit_open(struct net_device *dev)
{
	struct netkit *nk = netkit_priv(dev);
	struct net_device *peer = rtnl_dereference(nk->peer);

	if (!peer)
		return -ENOTCONN;
	if (peer->flags & IFF_UP) {
		netif_carrier_on(dev);
		netif_carrier_on(peer);
	}
	return 0;
}

static int netkit_close(struct net_device *dev)
{
	struct netkit *nk = netkit_priv(dev);
	struct net_device *peer = rtnl_dereference(nk->peer);

	netif_carrier_off(dev);
	if (peer)
		netif_carrier_off(peer);
	return 0;
}

static int netkit_get_iflink(const struct net_device *dev)
{
	struct netkit *nk = netkit_priv(dev);
	struct net_device *peer;
	int iflink = 0;

	rcu_read_lock();
	peer = rcu_dereference(nk->peer);
	if (peer)
		iflink = READ_ONCE(peer->ifindex);
	rcu_read_unlock();
	return iflink;
}

static void netkit_set_multicast(struct net_device *dev)
{
	/* Nothing to do, we receive whatever gets pushed to us! */
}

static int netkit_set_macaddr(struct net_device *dev, void *sa)
{
	struct netkit *nk = netkit_priv(dev);

	if (nk->mode != NETKIT_L2)
		return -EOPNOTSUPP;

	return eth_mac_addr(dev, sa);
}

static void netkit_set_headroom(struct net_device *dev, int headroom)
{
	struct netkit *nk = netkit_priv(dev), *nk2;
	struct net_device *peer;

	if (headroom < 0)
		headroom = NET_SKB_PAD;

	rcu_read_lock();
	peer = rcu_dereference(nk->peer);
	if (unlikely(!peer))
		goto out;

	nk2 = netkit_priv(peer);
	nk->headroom = headroom;
	headroom = max(nk->headroom, nk2->headroom);

	peer->needed_headroom = headroom;
	dev->needed_headroom = headroom;
out:
	rcu_read_unlock();
}

INDIRECT_CALLABLE_SCOPE struct net_device *netkit_peer_dev(struct net_device *dev)
{
	return rcu_dereference(netkit_priv(dev)->peer);
}

static void netkit_get_stats(struct net_device *dev,
			     struct rtnl_link_stats64 *stats)
{
	dev_fetch_sw_netstats(stats, dev->tstats);
	stats->tx_dropped = DEV_STATS_READ(dev, tx_dropped);
}

static void netkit_uninit(struct net_device *dev);

static const struct net_device_ops netkit_netdev_ops = {
	.ndo_open		= netkit_open,
	.ndo_stop		= netkit_close,
	.ndo_start_xmit		= netkit_xmit,
	.ndo_set_rx_mode	= netkit_set_multicast,
	.ndo_set_rx_headroom	= netkit_set_headroom,
	.ndo_set_mac_address	= netkit_set_macaddr,
	.ndo_get_iflink		= netkit_get_iflink,
	.ndo_get_peer_dev	= netkit_peer_dev,
	.ndo_get_stats64	= netkit_get_stats,
	.ndo_uninit		= netkit_uninit,
	.ndo_features_check	= passthru_features_check,
};

static void netkit_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	strscpy(info->driver, DRV_NAME, sizeof(info->driver));
}

static const struct ethtool_ops netkit_ethtool_ops = {
	.get_drvinfo		= netkit_get_drvinfo,
};

static void netkit_setup(struct net_device *dev)
{
	static const netdev_features_t netkit_features_hw_vlan =
		NETIF_F_HW_VLAN_CTAG_TX |
		NETIF_F_HW_VLAN_CTAG_RX |
		NETIF_F_HW_VLAN_STAG_TX |
		NETIF_F_HW_VLAN_STAG_RX;
	static const netdev_features_t netkit_features =
		netkit_features_hw_vlan |
		NETIF_F_SG |
		NETIF_F_FRAGLIST |
		NETIF_F_HW_CSUM |
		NETIF_F_RXCSUM |
		NETIF_F_SCTP_CRC |
		NETIF_F_HIGHDMA |
		NETIF_F_GSO_SOFTWARE |
		NETIF_F_GSO_ENCAP_ALL;

	ether_setup(dev);
	dev->max_mtu = ETH_MAX_MTU;
	dev->pcpu_stat_type = NETDEV_PCPU_STAT_TSTATS;

	dev->flags |= IFF_NOARP;
	dev->priv_flags &= ~IFF_TX_SKB_SHARING;
	dev->priv_flags |= IFF_LIVE_ADDR_CHANGE;
	dev->priv_flags |= IFF_PHONY_HEADROOM;
	dev->priv_flags |= IFF_NO_QUEUE;
	dev->priv_flags |= IFF_DISABLE_NETPOLL;
	dev->lltx = true;

	dev->ethtool_ops = &netkit_ethtool_ops;
	dev->netdev_ops  = &netkit_netdev_ops;

	dev->features |= netkit_features;
	dev->hw_features = netkit_features;
	dev->hw_enc_features = netkit_features;
	dev->mpls_features = NETIF_F_HW_CSUM | NETIF_F_GSO_SOFTWARE;
	dev->vlan_features = dev->features & ~netkit_features_hw_vlan;

	dev->needs_free_netdev = true;

	netif_set_tso_max_size(dev, GSO_MAX_SIZE);
}

static struct net *netkit_get_link_net(const struct net_device *dev)
{
	struct netkit *nk = netkit_priv(dev);
	struct net_device *peer = rtnl_dereference(nk->peer);

	return peer ? dev_net(peer) : dev_net(dev);
}

static int netkit_check_policy(int policy, struct nlattr *tb,
			       struct netlink_ext_ack *extack)
{
	switch (policy) {
	case NETKIT_PASS:
	case NETKIT_DROP:
		return 0;
	default:
		NL_SET_ERR_MSG_ATTR(extack, tb,
				    "Provided default xmit policy not supported");
		return -EINVAL;
	}
}

static int netkit_check_mode(int mode, struct nlattr *tb,
			     struct netlink_ext_ack *extack)
{
	switch (mode) {
	case NETKIT_L2:
	case NETKIT_L3:
		return 0;
	default:
		NL_SET_ERR_MSG_ATTR(extack, tb,
				    "Provided device mode can only be L2 or L3");
		return -EINVAL;
	}
}

static int netkit_validate(struct nlattr *tb[], struct nlattr *data[],
			   struct netlink_ext_ack *extack)
{
	struct nlattr *attr = tb[IFLA_ADDRESS];

	if (!attr)
		return 0;
	if (nla_len(attr) != ETH_ALEN)
		return -EINVAL;
	if (!is_valid_ether_addr(nla_data(attr)))
		return -EADDRNOTAVAIL;
	return 0;
}

static struct rtnl_link_ops netkit_link_ops;

static int netkit_new_link(struct net *src_net, struct net_device *dev,
			   struct nlattr *tb[], struct nlattr *data[],
			   struct netlink_ext_ack *extack)
{
	struct nlattr *peer_tb[IFLA_MAX + 1], **tbp = tb, *attr;
	enum netkit_action default_prim = NETKIT_PASS;
	enum netkit_action default_peer = NETKIT_PASS;
	enum netkit_mode mode = NETKIT_L3;
	unsigned char ifname_assign_type;
	struct ifinfomsg *ifmp = NULL;
	struct net_device *peer;
	char ifname[IFNAMSIZ];
	struct netkit *nk;
	struct net *net;
	int err;

	if (data) {
		if (data[IFLA_NETKIT_MODE]) {
			attr = data[IFLA_NETKIT_MODE];
			mode = nla_get_u32(attr);
			err = netkit_check_mode(mode, attr, extack);
			if (err < 0)
				return err;
		}
		if (data[IFLA_NETKIT_PEER_INFO]) {
			attr = data[IFLA_NETKIT_PEER_INFO];
			ifmp = nla_data(attr);
			err = rtnl_nla_parse_ifinfomsg(peer_tb, attr, extack);
			if (err < 0)
				return err;
			err = netkit_validate(peer_tb, NULL, extack);
			if (err < 0)
				return err;
			tbp = peer_tb;
		}
		if (data[IFLA_NETKIT_POLICY]) {
			attr = data[IFLA_NETKIT_POLICY];
			default_prim = nla_get_u32(attr);
			err = netkit_check_policy(default_prim, attr, extack);
			if (err < 0)
				return err;
		}
		if (data[IFLA_NETKIT_PEER_POLICY]) {
			attr = data[IFLA_NETKIT_PEER_POLICY];
			default_peer = nla_get_u32(attr);
			err = netkit_check_policy(default_peer, attr, extack);
			if (err < 0)
				return err;
		}
	}

	if (ifmp && tbp[IFLA_IFNAME]) {
		nla_strscpy(ifname, tbp[IFLA_IFNAME], IFNAMSIZ);
		ifname_assign_type = NET_NAME_USER;
	} else {
		strscpy(ifname, "nk%d", IFNAMSIZ);
		ifname_assign_type = NET_NAME_ENUM;
	}
	if (mode != NETKIT_L2 &&
	    (tb[IFLA_ADDRESS] || tbp[IFLA_ADDRESS]))
		return -EOPNOTSUPP;

	net = rtnl_link_get_net(src_net, tbp);
	if (IS_ERR(net))
		return PTR_ERR(net);

	peer = rtnl_create_link(net, ifname, ifname_assign_type,
				&netkit_link_ops, tbp, extack);
	if (IS_ERR(peer)) {
		put_net(net);
		return PTR_ERR(peer);
	}

	netif_inherit_tso_max(peer, dev);

	if (mode == NETKIT_L2 && !(ifmp && tbp[IFLA_ADDRESS]))
		eth_hw_addr_random(peer);
	if (ifmp && dev->ifindex)
		peer->ifindex = ifmp->ifi_index;

	nk = netkit_priv(peer);
	nk->primary = false;
	nk->policy = default_peer;
	nk->mode = mode;
	bpf_mprog_bundle_init(&nk->bundle);

	err = register_netdevice(peer);
	put_net(net);
	if (err < 0)
		goto err_register_peer;
	netif_carrier_off(peer);
	if (mode == NETKIT_L2)
		dev_change_flags(peer, peer->flags & ~IFF_NOARP, NULL);

	err = rtnl_configure_link(peer, NULL, 0, NULL);
	if (err < 0)
		goto err_configure_peer;

	if (mode == NETKIT_L2 && !tb[IFLA_ADDRESS])
		eth_hw_addr_random(dev);
	if (tb[IFLA_IFNAME])
		nla_strscpy(dev->name, tb[IFLA_IFNAME], IFNAMSIZ);
	else
		strscpy(dev->name, "nk%d", IFNAMSIZ);

	nk = netkit_priv(dev);
	nk->primary = true;
	nk->policy = default_prim;
	nk->mode = mode;
	bpf_mprog_bundle_init(&nk->bundle);

	err = register_netdevice(dev);
	if (err < 0)
		goto err_configure_peer;
	netif_carrier_off(dev);
	if (mode == NETKIT_L2)
		dev_change_flags(dev, dev->flags & ~IFF_NOARP, NULL);

	rcu_assign_pointer(netkit_priv(dev)->peer, peer);
	rcu_assign_pointer(netkit_priv(peer)->peer, dev);
	return 0;
err_configure_peer:
	unregister_netdevice(peer);
	return err;
err_register_peer:
	free_netdev(peer);
	return err;
}

static struct bpf_mprog_entry *netkit_entry_fetch(struct net_device *dev,
						  bool bundle_fallback)
{
	struct netkit *nk = netkit_priv(dev);
	struct bpf_mprog_entry *entry;

	ASSERT_RTNL();
	entry = rcu_dereference_rtnl(nk->active);
	if (entry)
		return entry;
	if (bundle_fallback)
		return &nk->bundle.a;
	return NULL;
}

static void netkit_entry_update(struct net_device *dev,
				struct bpf_mprog_entry *entry)
{
	struct netkit *nk = netkit_priv(dev);

	ASSERT_RTNL();
	rcu_assign_pointer(nk->active, entry);
}

static void netkit_entry_sync(void)
{
	synchronize_rcu();
}

static struct net_device *netkit_dev_fetch(struct net *net, u32 ifindex, u32 which)
{
	struct net_device *dev;
	struct netkit *nk;

	ASSERT_RTNL();

	switch (which) {
	case BPF_NETKIT_PRIMARY:
	case BPF_NETKIT_PEER:
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	dev = __dev_get_by_index(net, ifindex);
	if (!dev)
		return ERR_PTR(-ENODEV);
	if (dev->netdev_ops != &netkit_netdev_ops)
		return ERR_PTR(-ENXIO);

	nk = netkit_priv(dev);
	if (!nk->primary)
		return ERR_PTR(-EACCES);
	if (which == BPF_NETKIT_PEER) {
		dev = rcu_dereference_rtnl(nk->peer);
		if (!dev)
			return ERR_PTR(-ENODEV);
	}
	return dev;
}

int netkit_prog_attach(const union bpf_attr *attr, struct bpf_prog *prog)
{
	struct bpf_mprog_entry *entry, *entry_new;
	struct bpf_prog *replace_prog = NULL;
	struct net_device *dev;
	int ret;

	rtnl_lock();
	dev = netkit_dev_fetch(current->nsproxy->net_ns, attr->target_ifindex,
			       attr->attach_type);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto out;
	}
	entry = netkit_entry_fetch(dev, true);
	if (attr->attach_flags & BPF_F_REPLACE) {
		replace_prog = bpf_prog_get_type(attr->replace_bpf_fd,
						 prog->type);
		if (IS_ERR(replace_prog)) {
			ret = PTR_ERR(replace_prog);
			replace_prog = NULL;
			goto out;
		}
	}
	ret = bpf_mprog_attach(entry, &entry_new, prog, NULL, replace_prog,
			       attr->attach_flags, attr->relative_fd,
			       attr->expected_revision);
	if (!ret) {
		if (entry != entry_new) {
			netkit_entry_update(dev, entry_new);
			netkit_entry_sync();
		}
		bpf_mprog_commit(entry);
	}
out:
	if (replace_prog)
		bpf_prog_put(replace_prog);
	rtnl_unlock();
	return ret;
}

int netkit_prog_detach(const union bpf_attr *attr, struct bpf_prog *prog)
{
	struct bpf_mprog_entry *entry, *entry_new;
	struct net_device *dev;
	int ret;

	rtnl_lock();
	dev = netkit_dev_fetch(current->nsproxy->net_ns, attr->target_ifindex,
			       attr->attach_type);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto out;
	}
	entry = netkit_entry_fetch(dev, false);
	if (!entry) {
		ret = -ENOENT;
		goto out;
	}
	ret = bpf_mprog_detach(entry, &entry_new, prog, NULL, attr->attach_flags,
			       attr->relative_fd, attr->expected_revision);
	if (!ret) {
		if (!bpf_mprog_total(entry_new))
			entry_new = NULL;
		netkit_entry_update(dev, entry_new);
		netkit_entry_sync();
		bpf_mprog_commit(entry);
	}
out:
	rtnl_unlock();
	return ret;
}

int netkit_prog_query(const union bpf_attr *attr, union bpf_attr __user *uattr)
{
	struct net_device *dev;
	int ret;

	rtnl_lock();
	dev = netkit_dev_fetch(current->nsproxy->net_ns,
			       attr->query.target_ifindex,
			       attr->query.attach_type);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto out;
	}
	ret = bpf_mprog_query(attr, uattr, netkit_entry_fetch(dev, false));
out:
	rtnl_unlock();
	return ret;
}

static struct netkit_link *netkit_link(const struct bpf_link *link)
{
	return container_of(link, struct netkit_link, link);
}

static int netkit_link_prog_attach(struct bpf_link *link, u32 flags,
				   u32 id_or_fd, u64 revision)
{
	struct netkit_link *nkl = netkit_link(link);
	struct bpf_mprog_entry *entry, *entry_new;
	struct net_device *dev = nkl->dev;
	int ret;

	ASSERT_RTNL();
	entry = netkit_entry_fetch(dev, true);
	ret = bpf_mprog_attach(entry, &entry_new, link->prog, link, NULL, flags,
			       id_or_fd, revision);
	if (!ret) {
		if (entry != entry_new) {
			netkit_entry_update(dev, entry_new);
			netkit_entry_sync();
		}
		bpf_mprog_commit(entry);
	}
	return ret;
}

static void netkit_link_release(struct bpf_link *link)
{
	struct netkit_link *nkl = netkit_link(link);
	struct bpf_mprog_entry *entry, *entry_new;
	struct net_device *dev;
	int ret = 0;

	rtnl_lock();
	dev = nkl->dev;
	if (!dev)
		goto out;
	entry = netkit_entry_fetch(dev, false);
	if (!entry) {
		ret = -ENOENT;
		goto out;
	}
	ret = bpf_mprog_detach(entry, &entry_new, link->prog, link, 0, 0, 0);
	if (!ret) {
		if (!bpf_mprog_total(entry_new))
			entry_new = NULL;
		netkit_entry_update(dev, entry_new);
		netkit_entry_sync();
		bpf_mprog_commit(entry);
		nkl->dev = NULL;
	}
out:
	WARN_ON_ONCE(ret);
	rtnl_unlock();
}

static int netkit_link_update(struct bpf_link *link, struct bpf_prog *nprog,
			      struct bpf_prog *oprog)
{
	struct netkit_link *nkl = netkit_link(link);
	struct bpf_mprog_entry *entry, *entry_new;
	struct net_device *dev;
	int ret = 0;

	rtnl_lock();
	dev = nkl->dev;
	if (!dev) {
		ret = -ENOLINK;
		goto out;
	}
	if (oprog && link->prog != oprog) {
		ret = -EPERM;
		goto out;
	}
	oprog = link->prog;
	if (oprog == nprog) {
		bpf_prog_put(nprog);
		goto out;
	}
	entry = netkit_entry_fetch(dev, false);
	if (!entry) {
		ret = -ENOENT;
		goto out;
	}
	ret = bpf_mprog_attach(entry, &entry_new, nprog, link, oprog,
			       BPF_F_REPLACE | BPF_F_ID,
			       link->prog->aux->id, 0);
	if (!ret) {
		WARN_ON_ONCE(entry != entry_new);
		oprog = xchg(&link->prog, nprog);
		bpf_prog_put(oprog);
		bpf_mprog_commit(entry);
	}
out:
	rtnl_unlock();
	return ret;
}

static void netkit_link_dealloc(struct bpf_link *link)
{
	kfree(netkit_link(link));
}

static void netkit_link_fdinfo(const struct bpf_link *link, struct seq_file *seq)
{
	const struct netkit_link *nkl = netkit_link(link);
	u32 ifindex = 0;

	rtnl_lock();
	if (nkl->dev)
		ifindex = nkl->dev->ifindex;
	rtnl_unlock();

	seq_printf(seq, "ifindex:\t%u\n", ifindex);
	seq_printf(seq, "attach_type:\t%u (%s)\n",
		   nkl->location,
		   nkl->location == BPF_NETKIT_PRIMARY ? "primary" : "peer");
}

static int netkit_link_fill_info(const struct bpf_link *link,
				 struct bpf_link_info *info)
{
	const struct netkit_link *nkl = netkit_link(link);
	u32 ifindex = 0;

	rtnl_lock();
	if (nkl->dev)
		ifindex = nkl->dev->ifindex;
	rtnl_unlock();

	info->netkit.ifindex = ifindex;
	info->netkit.attach_type = nkl->location;
	return 0;
}

static int netkit_link_detach(struct bpf_link *link)
{
	netkit_link_release(link);
	return 0;
}

static const struct bpf_link_ops netkit_link_lops = {
	.release	= netkit_link_release,
	.detach		= netkit_link_detach,
	.dealloc	= netkit_link_dealloc,
	.update_prog	= netkit_link_update,
	.show_fdinfo	= netkit_link_fdinfo,
	.fill_link_info	= netkit_link_fill_info,
};

static int netkit_link_init(struct netkit_link *nkl,
			    struct bpf_link_primer *link_primer,
			    const union bpf_attr *attr,
			    struct net_device *dev,
			    struct bpf_prog *prog)
{
	bpf_link_init(&nkl->link, BPF_LINK_TYPE_NETKIT,
		      &netkit_link_lops, prog);
	nkl->location = attr->link_create.attach_type;
	nkl->dev = dev;
	return bpf_link_prime(&nkl->link, link_primer);
}

int netkit_link_attach(const union bpf_attr *attr, struct bpf_prog *prog)
{
	struct bpf_link_primer link_primer;
	struct netkit_link *nkl;
	struct net_device *dev;
	int ret;

	rtnl_lock();
	dev = netkit_dev_fetch(current->nsproxy->net_ns,
			       attr->link_create.target_ifindex,
			       attr->link_create.attach_type);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto out;
	}
	nkl = kzalloc(sizeof(*nkl), GFP_KERNEL_ACCOUNT);
	if (!nkl) {
		ret = -ENOMEM;
		goto out;
	}
	ret = netkit_link_init(nkl, &link_primer, attr, dev, prog);
	if (ret) {
		kfree(nkl);
		goto out;
	}
	ret = netkit_link_prog_attach(&nkl->link,
				      attr->link_create.flags,
				      attr->link_create.netkit.relative_fd,
				      attr->link_create.netkit.expected_revision);
	if (ret) {
		nkl->dev = NULL;
		bpf_link_cleanup(&link_primer);
		goto out;
	}
	ret = bpf_link_settle(&link_primer);
out:
	rtnl_unlock();
	return ret;
}

static void netkit_release_all(struct net_device *dev)
{
	struct bpf_mprog_entry *entry;
	struct bpf_tuple tuple = {};
	struct bpf_mprog_fp *fp;
	struct bpf_mprog_cp *cp;

	entry = netkit_entry_fetch(dev, false);
	if (!entry)
		return;
	netkit_entry_update(dev, NULL);
	netkit_entry_sync();
	bpf_mprog_foreach_tuple(entry, fp, cp, tuple) {
		if (tuple.link)
			netkit_link(tuple.link)->dev = NULL;
		else
			bpf_prog_put(tuple.prog);
	}
}

static void netkit_uninit(struct net_device *dev)
{
	netkit_release_all(dev);
}

static void netkit_del_link(struct net_device *dev, struct list_head *head)
{
	struct netkit *nk = netkit_priv(dev);
	struct net_device *peer = rtnl_dereference(nk->peer);

	RCU_INIT_POINTER(nk->peer, NULL);
	unregister_netdevice_queue(dev, head);
	if (peer) {
		nk = netkit_priv(peer);
		RCU_INIT_POINTER(nk->peer, NULL);
		unregister_netdevice_queue(peer, head);
	}
}

static int netkit_change_link(struct net_device *dev, struct nlattr *tb[],
			      struct nlattr *data[],
			      struct netlink_ext_ack *extack)
{
	struct netkit *nk = netkit_priv(dev);
	struct net_device *peer = rtnl_dereference(nk->peer);
	enum netkit_action policy;
	struct nlattr *attr;
	int err;

	if (!nk->primary) {
		NL_SET_ERR_MSG(extack,
			       "netkit link settings can be changed only through the primary device");
		return -EACCES;
	}

	if (data[IFLA_NETKIT_MODE]) {
		NL_SET_ERR_MSG_ATTR(extack, data[IFLA_NETKIT_MODE],
				    "netkit link operating mode cannot be changed after device creation");
		return -EACCES;
	}

	if (data[IFLA_NETKIT_PEER_INFO]) {
		NL_SET_ERR_MSG_ATTR(extack, data[IFLA_NETKIT_PEER_INFO],
				    "netkit peer info cannot be changed after device creation");
		return -EINVAL;
	}

	if (data[IFLA_NETKIT_POLICY]) {
		attr = data[IFLA_NETKIT_POLICY];
		policy = nla_get_u32(attr);
		err = netkit_check_policy(policy, attr, extack);
		if (err)
			return err;
		WRITE_ONCE(nk->policy, policy);
	}

	if (data[IFLA_NETKIT_PEER_POLICY]) {
		err = -EOPNOTSUPP;
		attr = data[IFLA_NETKIT_PEER_POLICY];
		policy = nla_get_u32(attr);
		if (peer)
			err = netkit_check_policy(policy, attr, extack);
		if (err)
			return err;
		nk = netkit_priv(peer);
		WRITE_ONCE(nk->policy, policy);
	}

	return 0;
}

static size_t netkit_get_size(const struct net_device *dev)
{
	return nla_total_size(sizeof(u32)) + /* IFLA_NETKIT_POLICY */
	       nla_total_size(sizeof(u32)) + /* IFLA_NETKIT_PEER_POLICY */
	       nla_total_size(sizeof(u8))  + /* IFLA_NETKIT_PRIMARY */
	       nla_total_size(sizeof(u32)) + /* IFLA_NETKIT_MODE */
	       0;
}

static int netkit_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct netkit *nk = netkit_priv(dev);
	struct net_device *peer = rtnl_dereference(nk->peer);

	if (nla_put_u8(skb, IFLA_NETKIT_PRIMARY, nk->primary))
		return -EMSGSIZE;
	if (nla_put_u32(skb, IFLA_NETKIT_POLICY, nk->policy))
		return -EMSGSIZE;
	if (nla_put_u32(skb, IFLA_NETKIT_MODE, nk->mode))
		return -EMSGSIZE;

	if (peer) {
		nk = netkit_priv(peer);
		if (nla_put_u32(skb, IFLA_NETKIT_PEER_POLICY, nk->policy))
			return -EMSGSIZE;
	}

	return 0;
}

static const struct nla_policy netkit_policy[IFLA_NETKIT_MAX + 1] = {
	[IFLA_NETKIT_PEER_INFO]		= { .len = sizeof(struct ifinfomsg) },
	[IFLA_NETKIT_POLICY]		= { .type = NLA_U32 },
	[IFLA_NETKIT_MODE]		= { .type = NLA_U32 },
	[IFLA_NETKIT_PEER_POLICY]	= { .type = NLA_U32 },
	[IFLA_NETKIT_PRIMARY]		= { .type = NLA_REJECT,
					    .reject_message = "Primary attribute is read-only" },
};

static struct rtnl_link_ops netkit_link_ops = {
	.kind		= DRV_NAME,
	.priv_size	= sizeof(struct netkit),
	.setup		= netkit_setup,
	.newlink	= netkit_new_link,
	.dellink	= netkit_del_link,
	.changelink	= netkit_change_link,
	.get_link_net	= netkit_get_link_net,
	.get_size	= netkit_get_size,
	.fill_info	= netkit_fill_info,
	.policy		= netkit_policy,
	.validate	= netkit_validate,
	.maxtype	= IFLA_NETKIT_MAX,
};

static __init int netkit_init(void)
{
	BUILD_BUG_ON((int)NETKIT_NEXT != (int)TCX_NEXT ||
		     (int)NETKIT_PASS != (int)TCX_PASS ||
		     (int)NETKIT_DROP != (int)TCX_DROP ||
		     (int)NETKIT_REDIRECT != (int)TCX_REDIRECT);

	return rtnl_link_register(&netkit_link_ops);
}

static __exit void netkit_exit(void)
{
	rtnl_link_unregister(&netkit_link_ops);
}

module_init(netkit_init);
module_exit(netkit_exit);

MODULE_DESCRIPTION("BPF-programmable network device");
MODULE_AUTHOR("Daniel Borkmann <daniel@iogearbox.net>");
MODULE_AUTHOR("Nikolay Aleksandrov <razor@blackwall.org>");
MODULE_LICENSE("GPL");
MODULE_ALIAS_RTNL_LINK(DRV_NAME);
