// SPDX-License-Identifier: GPL-2.0
/* sunvnet.c: Sun LDOM Virtual Network Driver.
 *
 * Copyright (C) 2007, 2008 David S. Miller <davem@davemloft.net>
 * Copyright (C) 2016-2017 Oracle. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/etherdevice.h>
#include <linux/mutex.h>
#include <linux/highmem.h>
#include <linux/if_vlan.h>

#if IS_ENABLED(CONFIG_IPV6)
#include <linux/icmpv6.h>
#endif

#include <net/ip.h>
#include <net/icmp.h>
#include <net/route.h>

#include <asm/vio.h>
#include <asm/ldc.h>

#include "sunvnet_common.h"

/* length of time before we decide the hardware is borked,
 * and dev->tx_timeout() should be called to fix the problem
 */
#define VNET_TX_TIMEOUT			(5 * HZ)

#define DRV_MODULE_NAME		"sunvnet"
#define DRV_MODULE_VERSION	"2.0"
#define DRV_MODULE_RELDATE	"February 3, 2017"

static char version[] =
	DRV_MODULE_NAME " " DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")";
MODULE_AUTHOR("David S. Miller (davem@davemloft.net)");
MODULE_DESCRIPTION("Sun LDOM virtual network driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

/* Ordered from largest major to lowest */
static struct vio_version vnet_versions[] = {
	{ .major = 1, .minor = 8 },
	{ .major = 1, .minor = 7 },
	{ .major = 1, .minor = 6 },
	{ .major = 1, .minor = 0 },
};

static void vnet_get_drvinfo(struct net_device *dev,
			     struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, DRV_MODULE_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_MODULE_VERSION, sizeof(info->version));
}

static u32 vnet_get_msglevel(struct net_device *dev)
{
	struct vnet *vp = netdev_priv(dev);

	return vp->msg_enable;
}

static void vnet_set_msglevel(struct net_device *dev, u32 value)
{
	struct vnet *vp = netdev_priv(dev);

	vp->msg_enable = value;
}

static const struct {
	const char string[ETH_GSTRING_LEN];
} ethtool_stats_keys[] = {
	{ "rx_packets" },
	{ "tx_packets" },
	{ "rx_bytes" },
	{ "tx_bytes" },
	{ "rx_errors" },
	{ "tx_errors" },
	{ "rx_dropped" },
	{ "tx_dropped" },
	{ "multicast" },
	{ "rx_length_errors" },
	{ "rx_frame_errors" },
	{ "rx_missed_errors" },
	{ "tx_carrier_errors" },
	{ "nports" },
};

static int vnet_get_sset_count(struct net_device *dev, int sset)
{
	struct vnet *vp = (struct vnet *)netdev_priv(dev);

	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(ethtool_stats_keys)
			+ (NUM_VNET_PORT_STATS * vp->nports);
	default:
		return -EOPNOTSUPP;
	}
}

static void vnet_get_strings(struct net_device *dev, u32 stringset, u8 *buf)
{
	struct vnet *vp = (struct vnet *)netdev_priv(dev);
	struct vnet_port *port;
	char *p = (char *)buf;

	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(buf, &ethtool_stats_keys, sizeof(ethtool_stats_keys));
		p += sizeof(ethtool_stats_keys);

		rcu_read_lock();
		list_for_each_entry_rcu(port, &vp->port_list, list) {
			snprintf(p, ETH_GSTRING_LEN, "p%u.%s-%pM",
				 port->q_index, port->switch_port ? "s" : "q",
				 port->raddr);
			p += ETH_GSTRING_LEN;
			snprintf(p, ETH_GSTRING_LEN, "p%u.rx_packets",
				 port->q_index);
			p += ETH_GSTRING_LEN;
			snprintf(p, ETH_GSTRING_LEN, "p%u.tx_packets",
				 port->q_index);
			p += ETH_GSTRING_LEN;
			snprintf(p, ETH_GSTRING_LEN, "p%u.rx_bytes",
				 port->q_index);
			p += ETH_GSTRING_LEN;
			snprintf(p, ETH_GSTRING_LEN, "p%u.tx_bytes",
				 port->q_index);
			p += ETH_GSTRING_LEN;
			snprintf(p, ETH_GSTRING_LEN, "p%u.event_up",
				 port->q_index);
			p += ETH_GSTRING_LEN;
			snprintf(p, ETH_GSTRING_LEN, "p%u.event_reset",
				 port->q_index);
			p += ETH_GSTRING_LEN;
		}
		rcu_read_unlock();
		break;
	default:
		WARN_ON(1);
		break;
	}
}

static void vnet_get_ethtool_stats(struct net_device *dev,
				   struct ethtool_stats *estats, u64 *data)
{
	struct vnet *vp = (struct vnet *)netdev_priv(dev);
	struct vnet_port *port;
	int i = 0;

	data[i++] = dev->stats.rx_packets;
	data[i++] = dev->stats.tx_packets;
	data[i++] = dev->stats.rx_bytes;
	data[i++] = dev->stats.tx_bytes;
	data[i++] = dev->stats.rx_errors;
	data[i++] = dev->stats.tx_errors;
	data[i++] = dev->stats.rx_dropped;
	data[i++] = dev->stats.tx_dropped;
	data[i++] = dev->stats.multicast;
	data[i++] = dev->stats.rx_length_errors;
	data[i++] = dev->stats.rx_frame_errors;
	data[i++] = dev->stats.rx_missed_errors;
	data[i++] = dev->stats.tx_carrier_errors;
	data[i++] = vp->nports;

	rcu_read_lock();
	list_for_each_entry_rcu(port, &vp->port_list, list) {
		data[i++] = port->q_index;
		data[i++] = port->stats.rx_packets;
		data[i++] = port->stats.tx_packets;
		data[i++] = port->stats.rx_bytes;
		data[i++] = port->stats.tx_bytes;
		data[i++] = port->stats.event_up;
		data[i++] = port->stats.event_reset;
	}
	rcu_read_unlock();
}

static const struct ethtool_ops vnet_ethtool_ops = {
	.get_drvinfo		= vnet_get_drvinfo,
	.get_msglevel		= vnet_get_msglevel,
	.set_msglevel		= vnet_set_msglevel,
	.get_link		= ethtool_op_get_link,
	.get_sset_count		= vnet_get_sset_count,
	.get_strings		= vnet_get_strings,
	.get_ethtool_stats	= vnet_get_ethtool_stats,
};

static LIST_HEAD(vnet_list);
static DEFINE_MUTEX(vnet_list_mutex);

static struct vnet_port *__tx_port_find(struct vnet *vp, struct sk_buff *skb)
{
	unsigned int hash = vnet_hashfn(skb->data);
	struct hlist_head *hp = &vp->port_hash[hash];
	struct vnet_port *port;

	hlist_for_each_entry_rcu(port, hp, hash) {
		if (!sunvnet_port_is_up_common(port))
			continue;
		if (ether_addr_equal(port->raddr, skb->data))
			return port;
	}
	list_for_each_entry_rcu(port, &vp->port_list, list) {
		if (!port->switch_port)
			continue;
		if (!sunvnet_port_is_up_common(port))
			continue;
		return port;
	}
	return NULL;
}

/* func arg to vnet_start_xmit_common() to get the proper tx port */
static struct vnet_port *vnet_tx_port_find(struct sk_buff *skb,
					   struct net_device *dev)
{
	struct vnet *vp = netdev_priv(dev);

	return __tx_port_find(vp, skb);
}

static u16 vnet_select_queue(struct net_device *dev, struct sk_buff *skb,
			     struct net_device *sb_dev,
			     select_queue_fallback_t fallback)
{
	struct vnet *vp = netdev_priv(dev);
	struct vnet_port *port = __tx_port_find(vp, skb);

	if (!port)
		return 0;

	return port->q_index;
}

/* Wrappers to common functions */
static netdev_tx_t vnet_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	return sunvnet_start_xmit_common(skb, dev, vnet_tx_port_find);
}

static void vnet_set_rx_mode(struct net_device *dev)
{
	struct vnet *vp = netdev_priv(dev);

	return sunvnet_set_rx_mode_common(dev, vp);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void vnet_poll_controller(struct net_device *dev)
{
	struct vnet *vp = netdev_priv(dev);

	return sunvnet_poll_controller_common(dev, vp);
}
#endif

static const struct net_device_ops vnet_ops = {
	.ndo_open		= sunvnet_open_common,
	.ndo_stop		= sunvnet_close_common,
	.ndo_set_rx_mode	= vnet_set_rx_mode,
	.ndo_set_mac_address	= sunvnet_set_mac_addr_common,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_tx_timeout		= sunvnet_tx_timeout_common,
	.ndo_start_xmit		= vnet_start_xmit,
	.ndo_select_queue	= vnet_select_queue,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= vnet_poll_controller,
#endif
};

static struct vnet *vnet_new(const u64 *local_mac,
			     struct vio_dev *vdev)
{
	struct net_device *dev;
	struct vnet *vp;
	int err, i;

	dev = alloc_etherdev_mqs(sizeof(*vp), VNET_MAX_TXQS, 1);
	if (!dev)
		return ERR_PTR(-ENOMEM);
	dev->needed_headroom = VNET_PACKET_SKIP + 8;
	dev->needed_tailroom = 8;

	for (i = 0; i < ETH_ALEN; i++)
		dev->dev_addr[i] = (*local_mac >> (5 - i) * 8) & 0xff;

	vp = netdev_priv(dev);

	spin_lock_init(&vp->lock);
	vp->dev = dev;

	INIT_LIST_HEAD(&vp->port_list);
	for (i = 0; i < VNET_PORT_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&vp->port_hash[i]);
	INIT_LIST_HEAD(&vp->list);
	vp->local_mac = *local_mac;

	dev->netdev_ops = &vnet_ops;
	dev->ethtool_ops = &vnet_ethtool_ops;
	dev->watchdog_timeo = VNET_TX_TIMEOUT;

	dev->hw_features = NETIF_F_TSO | NETIF_F_GSO | NETIF_F_ALL_TSO |
			   NETIF_F_HW_CSUM | NETIF_F_SG;
	dev->features = dev->hw_features;

	/* MTU range: 68 - 65535 */
	dev->min_mtu = ETH_MIN_MTU;
	dev->max_mtu = VNET_MAX_MTU;

	SET_NETDEV_DEV(dev, &vdev->dev);

	err = register_netdev(dev);
	if (err) {
		pr_err("Cannot register net device, aborting\n");
		goto err_out_free_dev;
	}

	netdev_info(dev, "Sun LDOM vnet %pM\n", dev->dev_addr);

	list_add(&vp->list, &vnet_list);

	return vp;

err_out_free_dev:
	free_netdev(dev);

	return ERR_PTR(err);
}

static struct vnet *vnet_find_or_create(const u64 *local_mac,
					struct vio_dev *vdev)
{
	struct vnet *iter, *vp;

	mutex_lock(&vnet_list_mutex);
	vp = NULL;
	list_for_each_entry(iter, &vnet_list, list) {
		if (iter->local_mac == *local_mac) {
			vp = iter;
			break;
		}
	}
	if (!vp)
		vp = vnet_new(local_mac, vdev);
	mutex_unlock(&vnet_list_mutex);

	return vp;
}

static void vnet_cleanup(void)
{
	struct vnet *vp;
	struct net_device *dev;

	mutex_lock(&vnet_list_mutex);
	while (!list_empty(&vnet_list)) {
		vp = list_first_entry(&vnet_list, struct vnet, list);
		list_del(&vp->list);
		dev = vp->dev;
		/* vio_unregister_driver() should have cleaned up port_list */
		BUG_ON(!list_empty(&vp->port_list));
		unregister_netdev(dev);
		free_netdev(dev);
	}
	mutex_unlock(&vnet_list_mutex);
}

static const char *local_mac_prop = "local-mac-address";

static struct vnet *vnet_find_parent(struct mdesc_handle *hp,
				     u64 port_node,
				     struct vio_dev *vdev)
{
	const u64 *local_mac = NULL;
	u64 a;

	mdesc_for_each_arc(a, hp, port_node, MDESC_ARC_TYPE_BACK) {
		u64 target = mdesc_arc_target(hp, a);
		const char *name;

		name = mdesc_get_property(hp, target, "name", NULL);
		if (!name || strcmp(name, "network"))
			continue;

		local_mac = mdesc_get_property(hp, target,
					       local_mac_prop, NULL);
		if (local_mac)
			break;
	}
	if (!local_mac)
		return ERR_PTR(-ENODEV);

	return vnet_find_or_create(local_mac, vdev);
}

static struct ldc_channel_config vnet_ldc_cfg = {
	.event		= sunvnet_event_common,
	.mtu		= 64,
	.mode		= LDC_MODE_UNRELIABLE,
};

static struct vio_driver_ops vnet_vio_ops = {
	.send_attr		= sunvnet_send_attr_common,
	.handle_attr		= sunvnet_handle_attr_common,
	.handshake_complete	= sunvnet_handshake_complete_common,
};

const char *remote_macaddr_prop = "remote-mac-address";

static int vnet_port_probe(struct vio_dev *vdev, const struct vio_device_id *id)
{
	struct mdesc_handle *hp;
	struct vnet_port *port;
	unsigned long flags;
	struct vnet *vp;
	const u64 *rmac;
	int len, i, err, switch_port;

	hp = mdesc_grab();

	vp = vnet_find_parent(hp, vdev->mp, vdev);
	if (IS_ERR(vp)) {
		pr_err("Cannot find port parent vnet\n");
		err = PTR_ERR(vp);
		goto err_out_put_mdesc;
	}

	rmac = mdesc_get_property(hp, vdev->mp, remote_macaddr_prop, &len);
	err = -ENODEV;
	if (!rmac) {
		pr_err("Port lacks %s property\n", remote_macaddr_prop);
		goto err_out_put_mdesc;
	}

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	err = -ENOMEM;
	if (!port)
		goto err_out_put_mdesc;

	for (i = 0; i < ETH_ALEN; i++)
		port->raddr[i] = (*rmac >> (5 - i) * 8) & 0xff;

	port->vp = vp;

	err = vio_driver_init(&port->vio, vdev, VDEV_NETWORK,
			      vnet_versions, ARRAY_SIZE(vnet_versions),
			      &vnet_vio_ops, vp->dev->name);
	if (err)
		goto err_out_free_port;

	err = vio_ldc_alloc(&port->vio, &vnet_ldc_cfg, port);
	if (err)
		goto err_out_free_port;

	netif_napi_add(port->vp->dev, &port->napi, sunvnet_poll_common,
		       NAPI_POLL_WEIGHT);

	INIT_HLIST_NODE(&port->hash);
	INIT_LIST_HEAD(&port->list);

	switch_port = 0;
	if (mdesc_get_property(hp, vdev->mp, "switch-port", NULL))
		switch_port = 1;
	port->switch_port = switch_port;
	port->tso = true;
	port->tsolen = 0;

	spin_lock_irqsave(&vp->lock, flags);
	if (switch_port)
		list_add_rcu(&port->list, &vp->port_list);
	else
		list_add_tail_rcu(&port->list, &vp->port_list);
	hlist_add_head_rcu(&port->hash,
			   &vp->port_hash[vnet_hashfn(port->raddr)]);
	sunvnet_port_add_txq_common(port);
	spin_unlock_irqrestore(&vp->lock, flags);

	dev_set_drvdata(&vdev->dev, port);

	pr_info("%s: PORT ( remote-mac %pM%s )\n",
		vp->dev->name, port->raddr, switch_port ? " switch-port" : "");

	timer_setup(&port->clean_timer, sunvnet_clean_timer_expire_common, 0);

	napi_enable(&port->napi);
	vio_port_up(&port->vio);

	mdesc_release(hp);

	return 0;

err_out_free_port:
	kfree(port);

err_out_put_mdesc:
	mdesc_release(hp);
	return err;
}

static int vnet_port_remove(struct vio_dev *vdev)
{
	struct vnet_port *port = dev_get_drvdata(&vdev->dev);

	if (port) {
		del_timer_sync(&port->vio.timer);

		napi_disable(&port->napi);

		list_del_rcu(&port->list);
		hlist_del_rcu(&port->hash);

		synchronize_rcu();
		del_timer_sync(&port->clean_timer);
		sunvnet_port_rm_txq_common(port);
		netif_napi_del(&port->napi);
		sunvnet_port_free_tx_bufs_common(port);
		vio_ldc_free(&port->vio);

		dev_set_drvdata(&vdev->dev, NULL);

		kfree(port);
	}
	return 0;
}

static const struct vio_device_id vnet_port_match[] = {
	{
		.type = "vnet-port",
	},
	{},
};
MODULE_DEVICE_TABLE(vio, vnet_port_match);

static struct vio_driver vnet_port_driver = {
	.id_table	= vnet_port_match,
	.probe		= vnet_port_probe,
	.remove		= vnet_port_remove,
	.name		= "vnet_port",
};

static int __init vnet_init(void)
{
	pr_info("%s\n", version);
	return vio_register_driver(&vnet_port_driver);
}

static void __exit vnet_exit(void)
{
	vio_unregister_driver(&vnet_port_driver);
	vnet_cleanup();
}

module_init(vnet_init);
module_exit(vnet_exit);
