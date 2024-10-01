// SPDX-License-Identifier: GPL-2.0-only
#include <linux/ethtool.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/netlink.h>
#include <net/net_namespace.h>
#include <linux/if_arp.h>
#include <net/rtnetlink.h>

static netdev_tx_t nlmon_xmit(struct sk_buff *skb, struct net_device *dev)
{
	dev_lstats_add(dev, skb->len);

	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

struct nlmon {
	struct netlink_tap nt;
};

static int nlmon_open(struct net_device *dev)
{
	struct nlmon *nlmon = netdev_priv(dev);

	nlmon->nt.dev = dev;
	nlmon->nt.module = THIS_MODULE;
	return netlink_add_tap(&nlmon->nt);
}

static int nlmon_close(struct net_device *dev)
{
	struct nlmon *nlmon = netdev_priv(dev);

	return netlink_remove_tap(&nlmon->nt);
}

static void
nlmon_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	dev_lstats_read(dev, &stats->rx_packets, &stats->rx_bytes);
}

static u32 always_on(struct net_device *dev)
{
	return 1;
}

static const struct ethtool_ops nlmon_ethtool_ops = {
	.get_link = always_on,
};

static const struct net_device_ops nlmon_ops = {
	.ndo_open = nlmon_open,
	.ndo_stop = nlmon_close,
	.ndo_start_xmit = nlmon_xmit,
	.ndo_get_stats64 = nlmon_get_stats64,
};

static void nlmon_setup(struct net_device *dev)
{
	dev->type = ARPHRD_NETLINK;
	dev->priv_flags |= IFF_NO_QUEUE;
	dev->lltx = true;

	dev->netdev_ops	= &nlmon_ops;
	dev->ethtool_ops = &nlmon_ethtool_ops;
	dev->needs_free_netdev = true;

	dev->features = NETIF_F_SG | NETIF_F_FRAGLIST | NETIF_F_HIGHDMA;
	dev->flags = IFF_NOARP;
	dev->pcpu_stat_type = NETDEV_PCPU_STAT_LSTATS;

	/* That's rather a softlimit here, which, of course,
	 * can be altered. Not a real MTU, but what is to be
	 * expected in most cases.
	 */
	dev->mtu = NLMSG_GOODSIZE;
	dev->min_mtu = sizeof(struct nlmsghdr);
}

static int nlmon_validate(struct nlattr *tb[], struct nlattr *data[],
			  struct netlink_ext_ack *extack)
{
	if (tb[IFLA_ADDRESS])
		return -EINVAL;
	return 0;
}

static struct rtnl_link_ops nlmon_link_ops __read_mostly = {
	.kind			= "nlmon",
	.priv_size		= sizeof(struct nlmon),
	.setup			= nlmon_setup,
	.validate		= nlmon_validate,
};

static __init int nlmon_register(void)
{
	return rtnl_link_register(&nlmon_link_ops);
}

static __exit void nlmon_unregister(void)
{
	rtnl_link_unregister(&nlmon_link_ops);
}

module_init(nlmon_register);
module_exit(nlmon_unregister);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Daniel Borkmann <dborkman@redhat.com>");
MODULE_AUTHOR("Mathieu Geli <geli@enseirb.fr>");
MODULE_DESCRIPTION("Netlink monitoring device");
MODULE_ALIAS_RTNL_LINK("nlmon");
