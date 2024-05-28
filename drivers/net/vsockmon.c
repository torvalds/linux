// SPDX-License-Identifier: GPL-2.0-only
#include <linux/ethtool.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/if_arp.h>
#include <net/rtnetlink.h>
#include <net/sock.h>
#include <net/af_vsock.h>
#include <uapi/linux/vsockmon.h>
#include <linux/virtio_vsock.h>

/* Virtio transport max packet size plus header */
#define DEFAULT_MTU (VIRTIO_VSOCK_MAX_PKT_BUF_SIZE + \
		     sizeof(struct af_vsockmon_hdr))

struct vsockmon {
	struct vsock_tap vt;
};

static int vsockmon_open(struct net_device *dev)
{
	struct vsockmon *vsockmon = netdev_priv(dev);

	vsockmon->vt.dev = dev;
	vsockmon->vt.module = THIS_MODULE;
	return vsock_add_tap(&vsockmon->vt);
}

static int vsockmon_close(struct net_device *dev)
{
	struct vsockmon *vsockmon = netdev_priv(dev);

	return vsock_remove_tap(&vsockmon->vt);
}

static netdev_tx_t vsockmon_xmit(struct sk_buff *skb, struct net_device *dev)
{
	dev_lstats_add(dev, skb->len);

	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

static void
vsockmon_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	dev_lstats_read(dev, &stats->rx_packets, &stats->rx_bytes);
}

static int vsockmon_is_valid_mtu(int new_mtu)
{
	return new_mtu >= (int)sizeof(struct af_vsockmon_hdr);
}

static int vsockmon_change_mtu(struct net_device *dev, int new_mtu)
{
	if (!vsockmon_is_valid_mtu(new_mtu))
		return -EINVAL;

	dev->mtu = new_mtu;
	return 0;
}

static const struct net_device_ops vsockmon_ops = {
	.ndo_open = vsockmon_open,
	.ndo_stop = vsockmon_close,
	.ndo_start_xmit = vsockmon_xmit,
	.ndo_get_stats64 = vsockmon_get_stats64,
	.ndo_change_mtu = vsockmon_change_mtu,
};

static u32 always_on(struct net_device *dev)
{
	return 1;
}

static const struct ethtool_ops vsockmon_ethtool_ops = {
	.get_link = always_on,
};

static void vsockmon_setup(struct net_device *dev)
{
	dev->type = ARPHRD_VSOCKMON;
	dev->priv_flags |= IFF_NO_QUEUE;

	dev->netdev_ops	= &vsockmon_ops;
	dev->ethtool_ops = &vsockmon_ethtool_ops;
	dev->needs_free_netdev = true;

	dev->features = NETIF_F_SG | NETIF_F_FRAGLIST |
			NETIF_F_HIGHDMA | NETIF_F_LLTX;

	dev->flags = IFF_NOARP;

	dev->mtu = DEFAULT_MTU;
	dev->pcpu_stat_type = NETDEV_PCPU_STAT_LSTATS;
}

static struct rtnl_link_ops vsockmon_link_ops __read_mostly = {
	.kind			= "vsockmon",
	.priv_size		= sizeof(struct vsockmon),
	.setup			= vsockmon_setup,
};

static __init int vsockmon_register(void)
{
	return rtnl_link_register(&vsockmon_link_ops);
}

static __exit void vsockmon_unregister(void)
{
	rtnl_link_unregister(&vsockmon_link_ops);
}

module_init(vsockmon_register);
module_exit(vsockmon_unregister);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Gerard Garcia <ggarcia@deic.uab.cat>");
MODULE_DESCRIPTION("Vsock monitoring device. Based on nlmon device.");
MODULE_ALIAS_RTNL_LINK("vsockmon");
