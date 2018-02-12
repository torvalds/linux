// SPDX-License-Identifier: GPL-2.0
/* ldmvsw.c: Sun4v LDOM Virtual Switch Driver.
 *
 * Copyright (C) 2016-2017 Oracle. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/highmem.h>
#include <linux/if_vlan.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/types.h>

#if defined(CONFIG_IPV6)
#include <linux/icmpv6.h>
#endif

#include <net/ip.h>
#include <net/icmp.h>
#include <net/route.h>

#include <asm/vio.h>
#include <asm/ldc.h>

/* This driver makes use of the common code in sunvnet_common.c */
#include "sunvnet_common.h"

/* Length of time before we decide the hardware is hung,
 * and dev->tx_timeout() should be called to fix the problem.
 */
#define VSW_TX_TIMEOUT			(10 * HZ)

/* Static HW Addr used for the network interfaces representing vsw ports */
static u8 vsw_port_hwaddr[ETH_ALEN] = {0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

#define DRV_MODULE_NAME		"ldmvsw"
#define DRV_MODULE_VERSION	"1.2"
#define DRV_MODULE_RELDATE	"March 4, 2017"

static char version[] =
	DRV_MODULE_NAME " " DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")";
MODULE_AUTHOR("Oracle");
MODULE_DESCRIPTION("Sun4v LDOM Virtual Switch Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

/* Ordered from largest major to lowest */
static struct vio_version vsw_versions[] = {
	{ .major = 1, .minor = 8 },
	{ .major = 1, .minor = 7 },
	{ .major = 1, .minor = 6 },
	{ .major = 1, .minor = 0 },
};

static void vsw_get_drvinfo(struct net_device *dev,
			    struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, DRV_MODULE_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_MODULE_VERSION, sizeof(info->version));
}

static u32 vsw_get_msglevel(struct net_device *dev)
{
	struct vnet_port *port = netdev_priv(dev);

	return port->vp->msg_enable;
}

static void vsw_set_msglevel(struct net_device *dev, u32 value)
{
	struct vnet_port *port = netdev_priv(dev);

	port->vp->msg_enable = value;
}

static const struct ethtool_ops vsw_ethtool_ops = {
	.get_drvinfo		= vsw_get_drvinfo,
	.get_msglevel		= vsw_get_msglevel,
	.set_msglevel		= vsw_set_msglevel,
	.get_link		= ethtool_op_get_link,
};

static LIST_HEAD(vnet_list);
static DEFINE_MUTEX(vnet_list_mutex);

/* func arg to vnet_start_xmit_common() to get the proper tx port */
static struct vnet_port *vsw_tx_port_find(struct sk_buff *skb,
					  struct net_device *dev)
{
	struct vnet_port *port = netdev_priv(dev);

	return port;
}

static u16 vsw_select_queue(struct net_device *dev, struct sk_buff *skb,
			    void *accel_priv, select_queue_fallback_t fallback)
{
	struct vnet_port *port = netdev_priv(dev);

	if (!port)
		return 0;

	return port->q_index;
}

/* Wrappers to common functions */
static int vsw_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	return sunvnet_start_xmit_common(skb, dev, vsw_tx_port_find);
}

static void vsw_set_rx_mode(struct net_device *dev)
{
	struct vnet_port *port = netdev_priv(dev);

	return sunvnet_set_rx_mode_common(dev, port->vp);
}

int ldmvsw_open(struct net_device *dev)
{
	struct vnet_port *port = netdev_priv(dev);
	struct vio_driver_state *vio = &port->vio;

	/* reset the channel */
	vio_link_state_change(vio, LDC_EVENT_RESET);
	vnet_port_reset(port);
	vio_port_up(vio);

	return 0;
}
EXPORT_SYMBOL_GPL(ldmvsw_open);

#ifdef CONFIG_NET_POLL_CONTROLLER
static void vsw_poll_controller(struct net_device *dev)
{
	struct vnet_port *port = netdev_priv(dev);

	return sunvnet_poll_controller_common(dev, port->vp);
}
#endif

static const struct net_device_ops vsw_ops = {
	.ndo_open		= ldmvsw_open,
	.ndo_stop		= sunvnet_close_common,
	.ndo_set_rx_mode	= vsw_set_rx_mode,
	.ndo_set_mac_address	= sunvnet_set_mac_addr_common,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_tx_timeout		= sunvnet_tx_timeout_common,
	.ndo_start_xmit		= vsw_start_xmit,
	.ndo_select_queue	= vsw_select_queue,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller    = vsw_poll_controller,
#endif
};

static const char *local_mac_prop = "local-mac-address";
static const char *cfg_handle_prop = "cfg-handle";

static struct vnet *vsw_get_vnet(struct mdesc_handle *hp,
				 u64 port_node,
				 u64 *handle)
{
	struct vnet *vp;
	struct vnet *iter;
	const u64 *local_mac = NULL;
	const u64 *cfghandle = NULL;
	u64 a;

	/* Get the parent virtual-network-switch macaddr and cfghandle */
	mdesc_for_each_arc(a, hp, port_node, MDESC_ARC_TYPE_BACK) {
		u64 target = mdesc_arc_target(hp, a);
		const char *name;

		name = mdesc_get_property(hp, target, "name", NULL);
		if (!name || strcmp(name, "virtual-network-switch"))
			continue;

		local_mac = mdesc_get_property(hp, target,
					       local_mac_prop, NULL);
		cfghandle = mdesc_get_property(hp, target,
					       cfg_handle_prop, NULL);
		break;
	}
	if (!local_mac || !cfghandle)
		return ERR_PTR(-ENODEV);

	/* find or create associated vnet */
	vp = NULL;
	mutex_lock(&vnet_list_mutex);
	list_for_each_entry(iter, &vnet_list, list) {
		if (iter->local_mac == *local_mac) {
			vp = iter;
			break;
		}
	}

	if (!vp) {
		vp = kzalloc(sizeof(*vp), GFP_KERNEL);
		if (unlikely(!vp)) {
			mutex_unlock(&vnet_list_mutex);
			return ERR_PTR(-ENOMEM);
		}

		spin_lock_init(&vp->lock);
		INIT_LIST_HEAD(&vp->port_list);
		INIT_LIST_HEAD(&vp->list);
		vp->local_mac = *local_mac;
		list_add(&vp->list, &vnet_list);
	}

	mutex_unlock(&vnet_list_mutex);

	*handle = (u64)*cfghandle;

	return vp;
}

static struct net_device *vsw_alloc_netdev(u8 hwaddr[],
					   struct vio_dev *vdev,
					   u64 handle,
					   u64 port_id)
{
	struct net_device *dev;
	struct vnet_port *port;
	int i;

	dev = alloc_etherdev_mqs(sizeof(*port), VNET_MAX_TXQS, 1);
	if (!dev)
		return ERR_PTR(-ENOMEM);
	dev->needed_headroom = VNET_PACKET_SKIP + 8;
	dev->needed_tailroom = 8;

	for (i = 0; i < ETH_ALEN; i++) {
		dev->dev_addr[i] = hwaddr[i];
		dev->perm_addr[i] = dev->dev_addr[i];
	}

	sprintf(dev->name, "vif%d.%d", (int)handle, (int)port_id);

	dev->netdev_ops = &vsw_ops;
	dev->ethtool_ops = &vsw_ethtool_ops;
	dev->watchdog_timeo = VSW_TX_TIMEOUT;

	dev->hw_features = NETIF_F_HW_CSUM | NETIF_F_SG;
	dev->features = dev->hw_features;

	/* MTU range: 68 - 65535 */
	dev->min_mtu = ETH_MIN_MTU;
	dev->max_mtu = VNET_MAX_MTU;

	SET_NETDEV_DEV(dev, &vdev->dev);

	return dev;
}

static struct ldc_channel_config vsw_ldc_cfg = {
	.event		= sunvnet_event_common,
	.mtu		= 64,
	.mode		= LDC_MODE_UNRELIABLE,
};

static struct vio_driver_ops vsw_vio_ops = {
	.send_attr		= sunvnet_send_attr_common,
	.handle_attr		= sunvnet_handle_attr_common,
	.handshake_complete	= sunvnet_handshake_complete_common,
};

static const char *remote_macaddr_prop = "remote-mac-address";
static const char *id_prop = "id";

static int vsw_port_probe(struct vio_dev *vdev, const struct vio_device_id *id)
{
	struct mdesc_handle *hp;
	struct vnet_port *port;
	unsigned long flags;
	struct vnet *vp;
	struct net_device *dev;
	const u64 *rmac;
	int len, i, err;
	const u64 *port_id;
	u64 handle;

	hp = mdesc_grab();

	rmac = mdesc_get_property(hp, vdev->mp, remote_macaddr_prop, &len);
	err = -ENODEV;
	if (!rmac) {
		pr_err("Port lacks %s property\n", remote_macaddr_prop);
		mdesc_release(hp);
		return err;
	}

	port_id = mdesc_get_property(hp, vdev->mp, id_prop, NULL);
	err = -ENODEV;
	if (!port_id) {
		pr_err("Port lacks %s property\n", id_prop);
		mdesc_release(hp);
		return err;
	}

	/* Get (or create) the vnet associated with this port */
	vp = vsw_get_vnet(hp, vdev->mp, &handle);
	if (IS_ERR(vp)) {
		err = PTR_ERR(vp);
		pr_err("Failed to get vnet for vsw-port\n");
		mdesc_release(hp);
		return err;
	}

	mdesc_release(hp);

	dev = vsw_alloc_netdev(vsw_port_hwaddr, vdev, handle, *port_id);
	if (IS_ERR(dev)) {
		err = PTR_ERR(dev);
		pr_err("Failed to alloc netdev for vsw-port\n");
		return err;
	}

	port = netdev_priv(dev);

	INIT_LIST_HEAD(&port->list);

	for (i = 0; i < ETH_ALEN; i++)
		port->raddr[i] = (*rmac >> (5 - i) * 8) & 0xff;

	port->vp = vp;
	port->dev = dev;
	port->switch_port = 1;
	port->tso = false; /* no tso in vsw, misbehaves in bridge */
	port->tsolen = 0;

	/* Mark the port as belonging to ldmvsw which directs the
	 * the common code to use the net_device in the vnet_port
	 * rather than the net_device in the vnet (which is used
	 * by sunvnet). This bit is used by the VNET_PORT_TO_NET_DEVICE
	 * macro.
	 */
	port->vsw = 1;

	err = vio_driver_init(&port->vio, vdev, VDEV_NETWORK,
			      vsw_versions, ARRAY_SIZE(vsw_versions),
			      &vsw_vio_ops, dev->name);
	if (err)
		goto err_out_free_dev;

	err = vio_ldc_alloc(&port->vio, &vsw_ldc_cfg, port);
	if (err)
		goto err_out_free_dev;

	dev_set_drvdata(&vdev->dev, port);

	netif_napi_add(dev, &port->napi, sunvnet_poll_common,
		       NAPI_POLL_WEIGHT);

	spin_lock_irqsave(&vp->lock, flags);
	list_add_rcu(&port->list, &vp->port_list);
	spin_unlock_irqrestore(&vp->lock, flags);

	timer_setup(&port->clean_timer, sunvnet_clean_timer_expire_common, 0);

	err = register_netdev(dev);
	if (err) {
		pr_err("Cannot register net device, aborting\n");
		goto err_out_del_timer;
	}

	spin_lock_irqsave(&vp->lock, flags);
	sunvnet_port_add_txq_common(port);
	spin_unlock_irqrestore(&vp->lock, flags);

	napi_enable(&port->napi);
	vio_port_up(&port->vio);

	/* assure no carrier until we receive an LDC_EVENT_UP,
	 * even if the vsw config script tries to force us up
	 */
	netif_carrier_off(dev);

	netdev_info(dev, "LDOM vsw-port %pM\n", dev->dev_addr);

	pr_info("%s: PORT ( remote-mac %pM%s )\n", dev->name,
		port->raddr, " switch-port");

	return 0;

err_out_del_timer:
	del_timer_sync(&port->clean_timer);
	list_del_rcu(&port->list);
	synchronize_rcu();
	netif_napi_del(&port->napi);
	dev_set_drvdata(&vdev->dev, NULL);
	vio_ldc_free(&port->vio);

err_out_free_dev:
	free_netdev(dev);
	return err;
}

static int vsw_port_remove(struct vio_dev *vdev)
{
	struct vnet_port *port = dev_get_drvdata(&vdev->dev);
	unsigned long flags;

	if (port) {
		del_timer_sync(&port->vio.timer);
		del_timer_sync(&port->clean_timer);

		napi_disable(&port->napi);
		unregister_netdev(port->dev);

		list_del_rcu(&port->list);

		synchronize_rcu();
		spin_lock_irqsave(&port->vp->lock, flags);
		sunvnet_port_rm_txq_common(port);
		spin_unlock_irqrestore(&port->vp->lock, flags);
		netif_napi_del(&port->napi);
		sunvnet_port_free_tx_bufs_common(port);
		vio_ldc_free(&port->vio);

		dev_set_drvdata(&vdev->dev, NULL);

		free_netdev(port->dev);
	}

	return 0;
}

static void vsw_cleanup(void)
{
	struct vnet *vp;

	/* just need to free up the vnet list */
	mutex_lock(&vnet_list_mutex);
	while (!list_empty(&vnet_list)) {
		vp = list_first_entry(&vnet_list, struct vnet, list);
		list_del(&vp->list);
		/* vio_unregister_driver() should have cleaned up port_list */
		if (!list_empty(&vp->port_list))
			pr_err("Ports not removed by VIO subsystem!\n");
		kfree(vp);
	}
	mutex_unlock(&vnet_list_mutex);
}

static const struct vio_device_id vsw_port_match[] = {
	{
		.type = "vsw-port",
	},
	{},
};
MODULE_DEVICE_TABLE(vio, vsw_port_match);

static struct vio_driver vsw_port_driver = {
	.id_table	= vsw_port_match,
	.probe		= vsw_port_probe,
	.remove		= vsw_port_remove,
	.name		= "vsw_port",
};

static int __init vsw_init(void)
{
	pr_info("%s\n", version);
	return vio_register_driver(&vsw_port_driver);
}

static void __exit vsw_exit(void)
{
	vio_unregister_driver(&vsw_port_driver);
	vsw_cleanup();
}

module_init(vsw_init);
module_exit(vsw_exit);
