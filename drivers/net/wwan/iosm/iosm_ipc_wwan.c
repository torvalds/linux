// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-21 Intel Corporation.
 */

#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/if_link.h>
#include <linux/pm_runtime.h>
#include <linux/rtnetlink.h>
#include <linux/wwan.h>
#include <net/pkt_sched.h>

#include "iosm_ipc_chnl_cfg.h"
#include "iosm_ipc_imem_ops.h"
#include "iosm_ipc_wwan.h"

#define IOSM_IP_TYPE_MASK 0xF0
#define IOSM_IP_TYPE_IPV4 0x40
#define IOSM_IP_TYPE_IPV6 0x60

/**
 * struct iosm_netdev_priv - netdev WWAN driver specific private data
 * @ipc_wwan:	Pointer to iosm_wwan struct
 * @netdev:	Pointer to network interface device structure
 * @if_id:	Interface id for device.
 * @ch_id:	IPC channel number for which interface device is created.
 */
struct iosm_netdev_priv {
	struct iosm_wwan *ipc_wwan;
	struct net_device *netdev;
	int if_id;
	int ch_id;
};

/**
 * struct iosm_wwan - This structure contains information about WWAN root device
 *		      and interface to the IPC layer.
 * @ipc_imem:		Pointer to imem data-struct
 * @sub_netlist:	List of active netdevs
 * @dev:		Pointer device structure
 */
struct iosm_wwan {
	struct iosm_imem *ipc_imem;
	struct iosm_netdev_priv __rcu *sub_netlist[IP_MUX_SESSION_END + 1];
	struct device *dev;
};

/* Bring-up the wwan net link */
static int ipc_wwan_link_open(struct net_device *netdev)
{
	struct iosm_netdev_priv *priv = wwan_netdev_drvpriv(netdev);
	struct iosm_wwan *ipc_wwan = priv->ipc_wwan;
	int if_id = priv->if_id;
	int ret = 0;

	if (if_id < IP_MUX_SESSION_START ||
	    if_id >= ARRAY_SIZE(ipc_wwan->sub_netlist))
		return -EINVAL;

	pm_runtime_get_sync(ipc_wwan->ipc_imem->dev);
	/* get channel id */
	priv->ch_id = ipc_imem_sys_wwan_open(ipc_wwan->ipc_imem, if_id);

	if (priv->ch_id < 0) {
		dev_err(ipc_wwan->dev,
			"cannot connect wwan0 & id %d to the IPC mem layer",
			if_id);
		ret = -ENODEV;
		goto err_out;
	}

	/* enable tx path, DL data may follow */
	netif_start_queue(netdev);

	dev_dbg(ipc_wwan->dev, "Channel id %d allocated to if_id %d",
		priv->ch_id, priv->if_id);

err_out:
	pm_runtime_mark_last_busy(ipc_wwan->ipc_imem->dev);
	pm_runtime_put_autosuspend(ipc_wwan->ipc_imem->dev);

	return ret;
}

/* Bring-down the wwan net link */
static int ipc_wwan_link_stop(struct net_device *netdev)
{
	struct iosm_netdev_priv *priv = wwan_netdev_drvpriv(netdev);

	netif_stop_queue(netdev);

	pm_runtime_get_sync(priv->ipc_wwan->ipc_imem->dev);
	ipc_imem_sys_wwan_close(priv->ipc_wwan->ipc_imem, priv->if_id,
				priv->ch_id);
	priv->ch_id = -1;
	pm_runtime_mark_last_busy(priv->ipc_wwan->ipc_imem->dev);
	pm_runtime_put_autosuspend(priv->ipc_wwan->ipc_imem->dev);

	return 0;
}

/* Transmit a packet */
static netdev_tx_t ipc_wwan_link_transmit(struct sk_buff *skb,
					  struct net_device *netdev)
{
	struct iosm_netdev_priv *priv = wwan_netdev_drvpriv(netdev);
	struct iosm_wwan *ipc_wwan = priv->ipc_wwan;
	unsigned int len = skb->len;
	int if_id = priv->if_id;
	int ret;

	/* Interface IDs from 1 to 8 are for IP data
	 * & from 257 to 261 are for non-IP data
	 */
	if (if_id < IP_MUX_SESSION_START ||
	    if_id >= ARRAY_SIZE(ipc_wwan->sub_netlist))
		return -EINVAL;

	pm_runtime_get(ipc_wwan->ipc_imem->dev);
	/* Send the SKB to device for transmission */
	ret = ipc_imem_sys_wwan_transmit(ipc_wwan->ipc_imem,
					 if_id, priv->ch_id, skb);

	/* Return code of zero is success */
	if (ret == 0) {
		netdev->stats.tx_packets++;
		netdev->stats.tx_bytes += len;
		ret = NETDEV_TX_OK;
	} else if (ret == -EBUSY) {
		ret = NETDEV_TX_BUSY;
		dev_err(ipc_wwan->dev, "unable to push packets");
	} else {
		pm_runtime_mark_last_busy(ipc_wwan->ipc_imem->dev);
		pm_runtime_put_autosuspend(ipc_wwan->ipc_imem->dev);
		goto exit;
	}

	pm_runtime_mark_last_busy(ipc_wwan->ipc_imem->dev);
	pm_runtime_put_autosuspend(ipc_wwan->ipc_imem->dev);

	return ret;

exit:
	/* Log any skb drop */
	if (if_id)
		dev_dbg(ipc_wwan->dev, "skb dropped. IF_ID: %d, ret: %d", if_id,
			ret);

	dev_kfree_skb_any(skb);
	netdev->stats.tx_dropped++;
	return NETDEV_TX_OK;
}

/* Ops structure for wwan net link */
static const struct net_device_ops ipc_inm_ops = {
	.ndo_open = ipc_wwan_link_open,
	.ndo_stop = ipc_wwan_link_stop,
	.ndo_start_xmit = ipc_wwan_link_transmit,
};

/* Setup function for creating new net link */
static void ipc_wwan_setup(struct net_device *iosm_dev)
{
	iosm_dev->header_ops = NULL;
	iosm_dev->hard_header_len = 0;
	iosm_dev->tx_queue_len = DEFAULT_TX_QUEUE_LEN;

	iosm_dev->type = ARPHRD_NONE;
	iosm_dev->mtu = ETH_DATA_LEN;
	iosm_dev->min_mtu = ETH_MIN_MTU;
	iosm_dev->max_mtu = ETH_MAX_MTU;

	iosm_dev->flags = IFF_POINTOPOINT | IFF_NOARP;
	iosm_dev->needs_free_netdev = true;

	iosm_dev->netdev_ops = &ipc_inm_ops;
}

/* Create new wwan net link */
static int ipc_wwan_newlink(void *ctxt, struct net_device *dev,
			    u32 if_id, struct netlink_ext_ack *extack)
{
	struct iosm_wwan *ipc_wwan = ctxt;
	struct iosm_netdev_priv *priv;
	int err;

	if (if_id < IP_MUX_SESSION_START ||
	    if_id >= ARRAY_SIZE(ipc_wwan->sub_netlist))
		return -EINVAL;

	priv = wwan_netdev_drvpriv(dev);
	priv->if_id = if_id;
	priv->netdev = dev;
	priv->ipc_wwan = ipc_wwan;

	if (rcu_access_pointer(ipc_wwan->sub_netlist[if_id]))
		return -EBUSY;

	err = register_netdevice(dev);
	if (err)
		return err;

	rcu_assign_pointer(ipc_wwan->sub_netlist[if_id], priv);
	netif_device_attach(dev);

	return 0;
}

static void ipc_wwan_dellink(void *ctxt, struct net_device *dev,
			     struct list_head *head)
{
	struct iosm_netdev_priv *priv = wwan_netdev_drvpriv(dev);
	struct iosm_wwan *ipc_wwan = ctxt;
	int if_id = priv->if_id;

	if (WARN_ON(if_id < IP_MUX_SESSION_START ||
		    if_id >= ARRAY_SIZE(ipc_wwan->sub_netlist)))
		return;

	if (WARN_ON(rcu_access_pointer(ipc_wwan->sub_netlist[if_id]) != priv))
		return;

	RCU_INIT_POINTER(ipc_wwan->sub_netlist[if_id], NULL);
	/* unregistering includes synchronize_net() */
	unregister_netdevice_queue(dev, head);
}

static const struct wwan_ops iosm_wwan_ops = {
	.priv_size = sizeof(struct iosm_netdev_priv),
	.setup = ipc_wwan_setup,
	.newlink = ipc_wwan_newlink,
	.dellink = ipc_wwan_dellink,
};

int ipc_wwan_receive(struct iosm_wwan *ipc_wwan, struct sk_buff *skb_arg,
		     bool dss, int if_id)
{
	struct sk_buff *skb = skb_arg;
	struct net_device_stats *stats;
	struct iosm_netdev_priv *priv;
	int ret;

	if ((skb->data[0] & IOSM_IP_TYPE_MASK) == IOSM_IP_TYPE_IPV4)
		skb->protocol = htons(ETH_P_IP);
	else if ((skb->data[0] & IOSM_IP_TYPE_MASK) ==
		 IOSM_IP_TYPE_IPV6)
		skb->protocol = htons(ETH_P_IPV6);

	skb->pkt_type = PACKET_HOST;

	if (if_id < IP_MUX_SESSION_START ||
	    if_id > IP_MUX_SESSION_END) {
		ret = -EINVAL;
		goto free;
	}

	rcu_read_lock();
	priv = rcu_dereference(ipc_wwan->sub_netlist[if_id]);
	if (!priv) {
		ret = -EINVAL;
		goto unlock;
	}
	skb->dev = priv->netdev;
	stats = &priv->netdev->stats;
	stats->rx_packets++;
	stats->rx_bytes += skb->len;

	ret = netif_rx(skb);
	skb = NULL;
unlock:
	rcu_read_unlock();
free:
	dev_kfree_skb(skb);
	return ret;
}

void ipc_wwan_tx_flowctrl(struct iosm_wwan *ipc_wwan, int if_id, bool on)
{
	struct net_device *netdev;
	struct iosm_netdev_priv *priv;
	bool is_tx_blk;

	rcu_read_lock();
	priv = rcu_dereference(ipc_wwan->sub_netlist[if_id]);
	if (!priv) {
		rcu_read_unlock();
		return;
	}

	netdev = priv->netdev;

	is_tx_blk = netif_queue_stopped(netdev);

	if (on)
		dev_dbg(ipc_wwan->dev, "session id[%d]: flowctrl enable",
			if_id);

	if (on && !is_tx_blk)
		netif_stop_queue(netdev);
	else if (!on && is_tx_blk)
		netif_wake_queue(netdev);
	rcu_read_unlock();
}

struct iosm_wwan *ipc_wwan_init(struct iosm_imem *ipc_imem, struct device *dev)
{
	struct iosm_wwan *ipc_wwan;

	ipc_wwan = kzalloc(sizeof(*ipc_wwan), GFP_KERNEL);
	if (!ipc_wwan)
		return NULL;

	ipc_wwan->dev = dev;
	ipc_wwan->ipc_imem = ipc_imem;

	/* WWAN core will create a netdev for the default IP MUX channel */
	if (wwan_register_ops(ipc_wwan->dev, &iosm_wwan_ops, ipc_wwan,
			      IP_MUX_SESSION_DEFAULT)) {
		kfree(ipc_wwan);
		return NULL;
	}

	return ipc_wwan;
}

void ipc_wwan_deinit(struct iosm_wwan *ipc_wwan)
{
	/* This call will remove all child netdev(s) */
	wwan_unregister_ops(ipc_wwan->dev);

	kfree(ipc_wwan);
}
