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
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/rtnetlink.h>
#include <linux/net_tstamp.h>
#include <net/rtnetlink.h>
#include <linux/u64_stats_sync.h>

#define DRV_NAME	"dummy"
#define DRV_VERSION	"1.0"

#undef pr_fmt
#define pr_fmt(fmt) DRV_NAME ": " fmt

static int numdummies = 1;
static int num_vfs;

struct vf_data_storage {
	u8	vf_mac[ETH_ALEN];
	u16	pf_vlan; /* When set, guest VLAN config not allowed. */
	u16	pf_qos;
	__be16	vlan_proto;
	u16	min_tx_rate;
	u16	max_tx_rate;
	u8	spoofchk_enabled;
	bool	rss_query_enabled;
	u8	trusted;
	int	link_state;
};

struct dummy_priv {
	struct vf_data_storage	*vfinfo;
};

static int dummy_num_vf(struct device *dev)
{
	return num_vfs;
}

static struct bus_type dummy_bus = {
	.name	= "dummy",
	.num_vf	= dummy_num_vf,
};

static void release_dummy_parent(struct device *dev)
{
}

static struct device dummy_parent = {
	.init_name	= "dummy",
	.bus		= &dummy_bus,
	.release	= release_dummy_parent,
};

/* fake multicast ability */
static void set_multicast_list(struct net_device *dev)
{
}

struct pcpu_dstats {
	u64			tx_packets;
	u64			tx_bytes;
	struct u64_stats_sync	syncp;
};

static void dummy_get_stats64(struct net_device *dev,
			      struct rtnl_link_stats64 *stats)
{
	int i;

	for_each_possible_cpu(i) {
		const struct pcpu_dstats *dstats;
		u64 tbytes, tpackets;
		unsigned int start;

		dstats = per_cpu_ptr(dev->dstats, i);
		do {
			start = u64_stats_fetch_begin_irq(&dstats->syncp);
			tbytes = dstats->tx_bytes;
			tpackets = dstats->tx_packets;
		} while (u64_stats_fetch_retry_irq(&dstats->syncp, start));
		stats->tx_bytes += tbytes;
		stats->tx_packets += tpackets;
	}
}

static netdev_tx_t dummy_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct pcpu_dstats *dstats = this_cpu_ptr(dev->dstats);

	u64_stats_update_begin(&dstats->syncp);
	dstats->tx_packets++;
	dstats->tx_bytes += skb->len;
	u64_stats_update_end(&dstats->syncp);

	skb_tx_timestamp(skb);
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static int dummy_dev_init(struct net_device *dev)
{
	struct dummy_priv *priv = netdev_priv(dev);

	dev->dstats = netdev_alloc_pcpu_stats(struct pcpu_dstats);
	if (!dev->dstats)
		return -ENOMEM;

	priv->vfinfo = NULL;

	if (!num_vfs)
		return 0;

	dev->dev.parent = &dummy_parent;
	priv->vfinfo = kcalloc(num_vfs, sizeof(struct vf_data_storage),
			       GFP_KERNEL);
	if (!priv->vfinfo) {
		free_percpu(dev->dstats);
		return -ENOMEM;
	}

	return 0;
}

static void dummy_dev_uninit(struct net_device *dev)
{
	free_percpu(dev->dstats);
}

static int dummy_change_carrier(struct net_device *dev, bool new_carrier)
{
	if (new_carrier)
		netif_carrier_on(dev);
	else
		netif_carrier_off(dev);
	return 0;
}

static int dummy_set_vf_mac(struct net_device *dev, int vf, u8 *mac)
{
	struct dummy_priv *priv = netdev_priv(dev);

	if (!is_valid_ether_addr(mac) || (vf >= num_vfs))
		return -EINVAL;

	memcpy(priv->vfinfo[vf].vf_mac, mac, ETH_ALEN);

	return 0;
}

static int dummy_set_vf_vlan(struct net_device *dev, int vf,
			     u16 vlan, u8 qos, __be16 vlan_proto)
{
	struct dummy_priv *priv = netdev_priv(dev);

	if ((vf >= num_vfs) || (vlan > 4095) || (qos > 7))
		return -EINVAL;

	priv->vfinfo[vf].pf_vlan = vlan;
	priv->vfinfo[vf].pf_qos = qos;
	priv->vfinfo[vf].vlan_proto = vlan_proto;

	return 0;
}

static int dummy_set_vf_rate(struct net_device *dev, int vf, int min, int max)
{
	struct dummy_priv *priv = netdev_priv(dev);

	if (vf >= num_vfs)
		return -EINVAL;

	priv->vfinfo[vf].min_tx_rate = min;
	priv->vfinfo[vf].max_tx_rate = max;

	return 0;
}

static int dummy_set_vf_spoofchk(struct net_device *dev, int vf, bool val)
{
	struct dummy_priv *priv = netdev_priv(dev);

	if (vf >= num_vfs)
		return -EINVAL;

	priv->vfinfo[vf].spoofchk_enabled = val;

	return 0;
}

static int dummy_set_vf_rss_query_en(struct net_device *dev, int vf, bool val)
{
	struct dummy_priv *priv = netdev_priv(dev);

	if (vf >= num_vfs)
		return -EINVAL;

	priv->vfinfo[vf].rss_query_enabled = val;

	return 0;
}

static int dummy_set_vf_trust(struct net_device *dev, int vf, bool val)
{
	struct dummy_priv *priv = netdev_priv(dev);

	if (vf >= num_vfs)
		return -EINVAL;

	priv->vfinfo[vf].trusted = val;

	return 0;
}

static int dummy_get_vf_config(struct net_device *dev,
			       int vf, struct ifla_vf_info *ivi)
{
	struct dummy_priv *priv = netdev_priv(dev);

	if (vf >= num_vfs)
		return -EINVAL;

	ivi->vf = vf;
	memcpy(&ivi->mac, priv->vfinfo[vf].vf_mac, ETH_ALEN);
	ivi->vlan = priv->vfinfo[vf].pf_vlan;
	ivi->qos = priv->vfinfo[vf].pf_qos;
	ivi->spoofchk = priv->vfinfo[vf].spoofchk_enabled;
	ivi->linkstate = priv->vfinfo[vf].link_state;
	ivi->min_tx_rate = priv->vfinfo[vf].min_tx_rate;
	ivi->max_tx_rate = priv->vfinfo[vf].max_tx_rate;
	ivi->rss_query_en = priv->vfinfo[vf].rss_query_enabled;
	ivi->trusted = priv->vfinfo[vf].trusted;
	ivi->vlan_proto = priv->vfinfo[vf].vlan_proto;

	return 0;
}

static int dummy_set_vf_link_state(struct net_device *dev, int vf, int state)
{
	struct dummy_priv *priv = netdev_priv(dev);

	if (vf >= num_vfs)
		return -EINVAL;

	priv->vfinfo[vf].link_state = state;

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
	.ndo_set_vf_mac		= dummy_set_vf_mac,
	.ndo_set_vf_vlan	= dummy_set_vf_vlan,
	.ndo_set_vf_rate	= dummy_set_vf_rate,
	.ndo_set_vf_spoofchk	= dummy_set_vf_spoofchk,
	.ndo_set_vf_trust	= dummy_set_vf_trust,
	.ndo_get_vf_config	= dummy_get_vf_config,
	.ndo_set_vf_link_state	= dummy_set_vf_link_state,
	.ndo_set_vf_rss_query_en = dummy_set_vf_rss_query_en,
};

static void dummy_get_drvinfo(struct net_device *dev,
			      struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
}

static int dummy_get_ts_info(struct net_device *dev,
			      struct ethtool_ts_info *ts_info)
{
	ts_info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
				   SOF_TIMESTAMPING_RX_SOFTWARE |
				   SOF_TIMESTAMPING_SOFTWARE;

	ts_info->phc_index = -1;

	return 0;
};

static const struct ethtool_ops dummy_ethtool_ops = {
	.get_drvinfo            = dummy_get_drvinfo,
	.get_ts_info		= dummy_get_ts_info,
};

static void dummy_free_netdev(struct net_device *dev)
{
	struct dummy_priv *priv = netdev_priv(dev);

	kfree(priv->vfinfo);
}

static void dummy_setup(struct net_device *dev)
{
	ether_setup(dev);

	/* Initialize the device structure. */
	dev->netdev_ops = &dummy_netdev_ops;
	dev->ethtool_ops = &dummy_ethtool_ops;
	dev->needs_free_netdev = true;
	dev->priv_destructor = dummy_free_netdev;

	/* Fill in device structure with ethernet-generic values. */
	dev->flags |= IFF_NOARP;
	dev->flags &= ~IFF_MULTICAST;
	dev->priv_flags |= IFF_LIVE_ADDR_CHANGE | IFF_NO_QUEUE;
	dev->features	|= NETIF_F_SG | NETIF_F_FRAGLIST;
	dev->features	|= NETIF_F_ALL_TSO | NETIF_F_UFO;
	dev->features	|= NETIF_F_HW_CSUM | NETIF_F_HIGHDMA | NETIF_F_LLTX;
	dev->features	|= NETIF_F_GSO_ENCAP_ALL;
	dev->hw_features |= dev->features;
	dev->hw_enc_features |= dev->features;
	eth_hw_addr_random(dev);

	dev->min_mtu = 0;
	dev->max_mtu = ETH_MAX_MTU;
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
	.priv_size	= sizeof(struct dummy_priv),
	.setup		= dummy_setup,
	.validate	= dummy_validate,
};

/* Number of dummy devices to be set up by this module. */
module_param(numdummies, int, 0);
MODULE_PARM_DESC(numdummies, "Number of dummy pseudo devices");

module_param(num_vfs, int, 0);
MODULE_PARM_DESC(num_vfs, "Number of dummy VFs per dummy device");

static int __init dummy_init_one(void)
{
	struct net_device *dev_dummy;
	int err;

	dev_dummy = alloc_netdev(sizeof(struct dummy_priv),
				 "dummy%d", NET_NAME_UNKNOWN, dummy_setup);
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

	if (num_vfs) {
		err = bus_register(&dummy_bus);
		if (err < 0) {
			pr_err("registering dummy bus failed\n");
			return err;
		}

		err = device_register(&dummy_parent);
		if (err < 0) {
			pr_err("registering dummy parent device failed\n");
			bus_unregister(&dummy_bus);
			return err;
		}
	}

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

	if (err && num_vfs) {
		device_unregister(&dummy_parent);
		bus_unregister(&dummy_bus);
	}

	return err;
}

static void __exit dummy_cleanup_module(void)
{
	rtnl_link_unregister(&dummy_link_ops);

	if (num_vfs) {
		device_unregister(&dummy_parent);
		bus_unregister(&dummy_bus);
	}
}

module_init(dummy_init_module);
module_exit(dummy_cleanup_module);
MODULE_LICENSE("GPL");
MODULE_ALIAS_RTNL_LINK(DRV_NAME);
MODULE_VERSION(DRV_VERSION);
