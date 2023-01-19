// SPDX-License-Identifier: GPL-2.0-only
/* dummy.c: a dummy net driver

	The purpose of this driver is to provide a device to point a
	route through, but not to actually transmit packets.

	Why?  If you have a machine whose only connection is an occasional
	PPP/SLIP/PLIP link, you can only connect to your own hostname
	when the link is up.  Otherwise you have to use localhost.
	This isn't very consistent.

	One solution is to set up a dummy link using PPP/SLIP/PLIP,
	but this seems (to me) too much overhead for too little gain.
	This driver provides a small alternative. Thus you can do

	[when not running slip]
		ifconfig dummy slip.addr.ess.here up
	[to go to slip]
		ifconfig dummy down
		dip whatever

	This was written by looking at Donald Becker's skeleton driver
	and the loopback driver.  I then threw away anything that didn't
	apply!	Thanks to Alan Cox for the key clue on what to do with
	misguided packets.

			Nick Holloway, 27th May 1994
	[I tweaked this explanation a little but that's all]
			Alan Cox, 30th May 1994
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/rtnetlink.h>
#include <linux/net_tstamp.h>
#include <net/rtnetlink.h>
#include <linux/u64_stats_sync.h>

#define DRV_NAME	"dummy"

static int numdummies = 1;

/* fake multicast ability */
static void set_multicast_list(struct net_device *dev)
{
}

static void dummy_get_stats64(struct net_device *dev,
			      struct rtnl_link_stats64 *stats)
{
	dev_lstats_read(dev, &stats->tx_packets, &stats->tx_bytes);
}

static netdev_tx_t dummy_xmit(struct sk_buff *skb, struct net_device *dev)
{
	dev_lstats_add(dev, skb->len);

	skb_tx_timestamp(skb);
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static int dummy_dev_init(struct net_device *dev)
{
	dev->lstats = netdev_alloc_pcpu_stats(struct pcpu_lstats);
	if (!dev->lstats)
		return -ENOMEM;

	return 0;
}

static void dummy_dev_uninit(struct net_device *dev)
{
	free_percpu(dev->lstats);
}

static int dummy_change_carrier(struct net_device *dev, bool new_carrier)
{
	if (new_carrier)
		netif_carrier_on(dev);
	else
		netif_carrier_off(dev);
	return 0;
}

static const struct net_device_ops dummy_netdev_ops = {
	.ndo_init		= dummy_dev_init,
	.ndo_uninit		= dummy_dev_uninit,
	.ndo_start_xmit		= dummy_xmit,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_rx_mode	= set_multicast_list,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_get_stats64	= dummy_get_stats64,
	.ndo_change_carrier	= dummy_change_carrier,
};

static const struct ethtool_ops dummy_ethtool_ops = {
	.get_ts_info		= ethtool_op_get_ts_info,
};

static void dummy_setup(struct net_device *dev)
{
	ether_setup(dev);

	/* Initialize the device structure. */
	dev->netdev_ops = &dummy_netdev_ops;
	dev->ethtool_ops = &dummy_ethtool_ops;
	dev->needs_free_netdev = true;

	/* Fill in device structure with ethernet-generic values. */
	dev->flags |= IFF_NOARP;
	dev->flags &= ~IFF_MULTICAST;
	dev->priv_flags |= IFF_LIVE_ADDR_CHANGE | IFF_NO_QUEUE;
	dev->features	|= NETIF_F_SG | NETIF_F_FRAGLIST;
	dev->features	|= NETIF_F_GSO_SOFTWARE;
	dev->features	|= NETIF_F_HW_CSUM | NETIF_F_HIGHDMA | NETIF_F_LLTX;
	dev->features	|= NETIF_F_GSO_ENCAP_ALL;
	dev->hw_features |= dev->features;
	dev->hw_enc_features |= dev->features;
	eth_hw_addr_random(dev);

	dev->min_mtu = 0;
	dev->max_mtu = 0;
}

static int dummy_validate(struct nlattr *tb[], struct nlattr *data[],
			  struct netlink_ext_ack *extack)
{
	if (tb[IFLA_ADDRESS]) {
		if (nla_len(tb[IFLA_ADDRESS]) != ETH_ALEN)
			return -EINVAL;
		if (!is_valid_ether_addr(nla_data(tb[IFLA_ADDRESS])))
			return -EADDRNOTAVAIL;
	}
	return 0;
}

static struct rtnl_link_ops dummy_link_ops __read_mostly = {
	.kind		= DRV_NAME,
	.setup		= dummy_setup,
	.validate	= dummy_validate,
};

/* Number of dummy devices to be set up by this module. */
module_param(numdummies, int, 0);
MODULE_PARM_DESC(numdummies, "Number of dummy pseudo devices");

static int __init dummy_init_one(void)
{
	struct net_device *dev_dummy;
	int err;

	dev_dummy = alloc_netdev(0, "dummy%d", NET_NAME_ENUM, dummy_setup);
	if (!dev_dummy)
		return -ENOMEM;

	dev_dummy->rtnl_link_ops = &dummy_link_ops;
	err = register_netdevice(dev_dummy);
	if (err < 0)
		goto err;
	return 0;

err:
	free_netdev(dev_dummy);
	return err;
}

static int __init dummy_init_module(void)
{
	int i, err = 0;

	down_write(&pernet_ops_rwsem);
	rtnl_lock();
	err = __rtnl_link_register(&dummy_link_ops);
	if (err < 0)
		goto out;

	for (i = 0; i < numdummies && !err; i++) {
		err = dummy_init_one();
		cond_resched();
	}
	if (err < 0)
		__rtnl_link_unregister(&dummy_link_ops);

out:
	rtnl_unlock();
	up_write(&pernet_ops_rwsem);

	return err;
}

static void __exit dummy_cleanup_module(void)
{
	rtnl_link_unregister(&dummy_link_ops);
}

module_init(dummy_init_module);
module_exit(dummy_cleanup_module);
MODULE_LICENSE("GPL");
MODULE_ALIAS_RTNL_LINK(DRV_NAME);
