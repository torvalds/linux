// SPDX-License-Identifier: GPL-2.0-or-later
/* drivers/net/ifb.c:

	The purpose of this driver is to provide a device that allows
	for sharing of resources:

	1) qdiscs/policies that are per device as opposed to system wide.
	ifb allows for a device which can be redirected to thus providing
	an impression of sharing.

	2) Allows for queueing incoming traffic for shaping instead of
	dropping.

	The original concept is based on what is known as the IMQ
	driver initially written by Martin Devera, later rewritten
	by Patrick McHardy and then maintained by Andre Correa.

	You need the tc action  mirror or redirect to feed this device
	packets.


	Authors:	Jamal Hadi Salim (2005)

*/


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <net/pkt_sched.h>
#include <net/net_namespace.h>

#define TX_Q_LIMIT    32
struct ifb_q_private {
	struct net_device	*dev;
	struct tasklet_struct   ifb_tasklet;
	int			tasklet_pending;
	int			txqnum;
	struct sk_buff_head     rq;
	u64			rx_packets;
	u64			rx_bytes;
	struct u64_stats_sync	rsync;

	struct u64_stats_sync	tsync;
	u64			tx_packets;
	u64			tx_bytes;
	struct sk_buff_head     tq;
} ____cacheline_aligned_in_smp;

struct ifb_dev_private {
	struct ifb_q_private *tx_private;
};

static netdev_tx_t ifb_xmit(struct sk_buff *skb, struct net_device *dev);
static int ifb_open(struct net_device *dev);
static int ifb_close(struct net_device *dev);

static void ifb_ri_tasklet(struct tasklet_struct *t)
{
	struct ifb_q_private *txp = from_tasklet(txp, t, ifb_tasklet);
	struct netdev_queue *txq;
	struct sk_buff *skb;

	txq = netdev_get_tx_queue(txp->dev, txp->txqnum);
	skb = skb_peek(&txp->tq);
	if (!skb) {
		if (!__netif_tx_trylock(txq))
			goto resched;
		skb_queue_splice_tail_init(&txp->rq, &txp->tq);
		__netif_tx_unlock(txq);
	}

	while ((skb = __skb_dequeue(&txp->tq)) != NULL) {
		skb->redirected = 0;
		skb->tc_skip_classify = 1;

		u64_stats_update_begin(&txp->tsync);
		txp->tx_packets++;
		txp->tx_bytes += skb->len;
		u64_stats_update_end(&txp->tsync);

		rcu_read_lock();
		skb->dev = dev_get_by_index_rcu(dev_net(txp->dev), skb->skb_iif);
		if (!skb->dev) {
			rcu_read_unlock();
			dev_kfree_skb(skb);
			txp->dev->stats.tx_dropped++;
			if (skb_queue_len(&txp->tq) != 0)
				goto resched;
			break;
		}
		rcu_read_unlock();
		skb->skb_iif = txp->dev->ifindex;

		if (!skb->from_ingress) {
			dev_queue_xmit(skb);
		} else {
			skb_pull_rcsum(skb, skb->mac_len);
			netif_receive_skb(skb);
		}
	}

	if (__netif_tx_trylock(txq)) {
		skb = skb_peek(&txp->rq);
		if (!skb) {
			txp->tasklet_pending = 0;
			if (netif_tx_queue_stopped(txq))
				netif_tx_wake_queue(txq);
		} else {
			__netif_tx_unlock(txq);
			goto resched;
		}
		__netif_tx_unlock(txq);
	} else {
resched:
		txp->tasklet_pending = 1;
		tasklet_schedule(&txp->ifb_tasklet);
	}

}

static void ifb_stats64(struct net_device *dev,
			struct rtnl_link_stats64 *stats)
{
	struct ifb_dev_private *dp = netdev_priv(dev);
	struct ifb_q_private *txp = dp->tx_private;
	unsigned int start;
	u64 packets, bytes;
	int i;

	for (i = 0; i < dev->num_tx_queues; i++,txp++) {
		do {
			start = u64_stats_fetch_begin_irq(&txp->rsync);
			packets = txp->rx_packets;
			bytes = txp->rx_bytes;
		} while (u64_stats_fetch_retry_irq(&txp->rsync, start));
		stats->rx_packets += packets;
		stats->rx_bytes += bytes;

		do {
			start = u64_stats_fetch_begin_irq(&txp->tsync);
			packets = txp->tx_packets;
			bytes = txp->tx_bytes;
		} while (u64_stats_fetch_retry_irq(&txp->tsync, start));
		stats->tx_packets += packets;
		stats->tx_bytes += bytes;
	}
	stats->rx_dropped = dev->stats.rx_dropped;
	stats->tx_dropped = dev->stats.tx_dropped;
}

static int ifb_dev_init(struct net_device *dev)
{
	struct ifb_dev_private *dp = netdev_priv(dev);
	struct ifb_q_private *txp;
	int i;

	txp = kcalloc(dev->num_tx_queues, sizeof(*txp), GFP_KERNEL);
	if (!txp)
		return -ENOMEM;
	dp->tx_private = txp;
	for (i = 0; i < dev->num_tx_queues; i++,txp++) {
		txp->txqnum = i;
		txp->dev = dev;
		__skb_queue_head_init(&txp->rq);
		__skb_queue_head_init(&txp->tq);
		u64_stats_init(&txp->rsync);
		u64_stats_init(&txp->tsync);
		tasklet_setup(&txp->ifb_tasklet, ifb_ri_tasklet);
		netif_tx_start_queue(netdev_get_tx_queue(dev, i));
	}
	return 0;
}

static const struct net_device_ops ifb_netdev_ops = {
	.ndo_open	= ifb_open,
	.ndo_stop	= ifb_close,
	.ndo_get_stats64 = ifb_stats64,
	.ndo_start_xmit	= ifb_xmit,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_init	= ifb_dev_init,
};

#define IFB_FEATURES (NETIF_F_HW_CSUM | NETIF_F_SG  | NETIF_F_FRAGLIST	| \
		      NETIF_F_GSO_SOFTWARE | NETIF_F_GSO_ENCAP_ALL	| \
		      NETIF_F_HIGHDMA | NETIF_F_HW_VLAN_CTAG_TX		| \
		      NETIF_F_HW_VLAN_STAG_TX)

static void ifb_dev_free(struct net_device *dev)
{
	struct ifb_dev_private *dp = netdev_priv(dev);
	struct ifb_q_private *txp = dp->tx_private;
	int i;

	for (i = 0; i < dev->num_tx_queues; i++,txp++) {
		tasklet_kill(&txp->ifb_tasklet);
		__skb_queue_purge(&txp->rq);
		__skb_queue_purge(&txp->tq);
	}
	kfree(dp->tx_private);
}

static void ifb_setup(struct net_device *dev)
{
	/* Initialize the device structure. */
	dev->netdev_ops = &ifb_netdev_ops;

	/* Fill in device structure with ethernet-generic values. */
	ether_setup(dev);
	dev->tx_queue_len = TX_Q_LIMIT;

	dev->features |= IFB_FEATURES;
	dev->hw_features |= dev->features;
	dev->hw_enc_features |= dev->features;
	dev->vlan_features |= IFB_FEATURES & ~(NETIF_F_HW_VLAN_CTAG_TX |
					       NETIF_F_HW_VLAN_STAG_TX);

	dev->flags |= IFF_NOARP;
	dev->flags &= ~IFF_MULTICAST;
	dev->priv_flags &= ~IFF_TX_SKB_SHARING;
	netif_keep_dst(dev);
	eth_hw_addr_random(dev);
	dev->needs_free_netdev = true;
	dev->priv_destructor = ifb_dev_free;

	dev->min_mtu = 0;
	dev->max_mtu = 0;
}

static netdev_tx_t ifb_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ifb_dev_private *dp = netdev_priv(dev);
	struct ifb_q_private *txp = dp->tx_private + skb_get_queue_mapping(skb);

	u64_stats_update_begin(&txp->rsync);
	txp->rx_packets++;
	txp->rx_bytes += skb->len;
	u64_stats_update_end(&txp->rsync);

	if (!skb->redirected || !skb->skb_iif) {
		dev_kfree_skb(skb);
		dev->stats.rx_dropped++;
		return NETDEV_TX_OK;
	}

	if (skb_queue_len(&txp->rq) >= dev->tx_queue_len)
		netif_tx_stop_queue(netdev_get_tx_queue(dev, txp->txqnum));

	__skb_queue_tail(&txp->rq, skb);
	if (!txp->tasklet_pending) {
		txp->tasklet_pending = 1;
		tasklet_schedule(&txp->ifb_tasklet);
	}

	return NETDEV_TX_OK;
}

static int ifb_close(struct net_device *dev)
{
	netif_tx_stop_all_queues(dev);
	return 0;
}

static int ifb_open(struct net_device *dev)
{
	netif_tx_start_all_queues(dev);
	return 0;
}

static int ifb_validate(struct nlattr *tb[], struct nlattr *data[],
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

static struct rtnl_link_ops ifb_link_ops __read_mostly = {
	.kind		= "ifb",
	.priv_size	= sizeof(struct ifb_dev_private),
	.setup		= ifb_setup,
	.validate	= ifb_validate,
};

/* Number of ifb devices to be set up by this module.
 * Note that these legacy devices have one queue.
 * Prefer something like : ip link add ifb10 numtxqueues 8 type ifb
 */
static int numifbs = 2;
module_param(numifbs, int, 0);
MODULE_PARM_DESC(numifbs, "Number of ifb devices");

static int __init ifb_init_one(int index)
{
	struct net_device *dev_ifb;
	int err;

	dev_ifb = alloc_netdev(sizeof(struct ifb_dev_private), "ifb%d",
			       NET_NAME_UNKNOWN, ifb_setup);

	if (!dev_ifb)
		return -ENOMEM;

	dev_ifb->rtnl_link_ops = &ifb_link_ops;
	err = register_netdevice(dev_ifb);
	if (err < 0)
		goto err;

	return 0;

err:
	free_netdev(dev_ifb);
	return err;
}

static int __init ifb_init_module(void)
{
	int i, err;

	down_write(&pernet_ops_rwsem);
	rtnl_lock();
	err = __rtnl_link_register(&ifb_link_ops);
	if (err < 0)
		goto out;

	for (i = 0; i < numifbs && !err; i++) {
		err = ifb_init_one(i);
		cond_resched();
	}
	if (err)
		__rtnl_link_unregister(&ifb_link_ops);

out:
	rtnl_unlock();
	up_write(&pernet_ops_rwsem);

	return err;
}

static void __exit ifb_cleanup_module(void)
{
	rtnl_link_unregister(&ifb_link_ops);
}

module_init(ifb_init_module);
module_exit(ifb_cleanup_module);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jamal Hadi Salim");
MODULE_ALIAS_RTNL_LINK("ifb");
