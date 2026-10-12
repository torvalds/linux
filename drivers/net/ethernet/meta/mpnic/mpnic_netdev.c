// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/etherdevice.h>
#include <linux/ipv6.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <net/netdev_lock.h>

#include "mpnic.h"
#include "mpnic_netdev.h"
#include "mpnic_txrx.h"

static int mpnic_open(struct net_device *netdev)
{
	struct mpnic_net *mpn = netdev_priv(netdev);
	int err;

	err = mpnic_alloc_napi_vectors(mpn);
	if (err)
		return err;

	err = mpnic_alloc_resources(mpn);
	if (err)
		goto err_free_napi_vectors;

	err = mpnic_set_netif_queues(mpn);
	if (err)
		goto err_free_resources;

	mpnic_stats_attach_rings(mpn);

	mpnic_enable(mpn);
	mpnic_fill(mpn);
	mpnic_napi_enable(mpn);

	netif_tx_wake_all_queues(netdev);
	netif_carrier_on(netdev);

	schedule_delayed_work(&mpn->mpd->service_task, HZ);

	return 0;

err_free_resources:
	mpnic_free_resources(mpn);
err_free_napi_vectors:
	mpnic_free_napi_vectors(mpn);
	return err;
}

static int mpnic_stop(struct net_device *netdev)
{
	struct mpnic_net *mpn = netdev_priv(netdev);

	cancel_delayed_work(&mpn->mpd->service_task);
	netif_carrier_off(netdev);

	mpnic_napi_disable(mpn);
	netif_tx_disable(netdev);

	mpnic_disable(mpn);
	mpnic_wait_all_queues_idle(mpn->mpd);
	mpnic_flush(mpn);

	mpnic_stats_fold_rings(mpn);

	mpnic_reset_netif_queues(mpn);
	mpnic_free_resources(mpn);
	mpnic_free_napi_vectors(mpn);

	return 0;
}

static void
mpnic_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats64)
{
	struct mpnic_net *mpn = netdev_priv(dev);
	u64 bytes, packets, dropped, errors;
	struct mpnic_queue_stats *stats;
	struct mpnic_base_stats *base;
	unsigned int start, i;

	rcu_read_lock();

	base = rcu_dereference(mpn->stats.base);

	stats64->tx_bytes = base->tx.bytes;
	stats64->tx_packets = base->tx.packets;
	stats64->tx_dropped = base->tx.dropped;

	stats64->rx_bytes = base->rx.bytes;
	stats64->rx_packets = base->rx.packets;
	stats64->rx_dropped = base->rx.dropped;
	stats64->rx_errors = base->rx.errors;

	for (i = 0; i < base->num_tx_queues; i++) {
		struct mpnic_ring *txr = base->txr[i];
		struct mpnic_q_triad *qt;

		qt = container_of(txr, struct mpnic_q_triad, sub0);

		stats = &qt->cmpl.stats;
		do {
			start = u64_stats_fetch_begin(&stats->syncp);
			bytes = u64_stats_read(&stats->tcq.bytes);
			packets = u64_stats_read(&stats->tcq.packets);
		} while (u64_stats_fetch_retry(&stats->syncp, start));

		stats = &txr->stats;
		do {
			start = u64_stats_fetch_begin(&stats->syncp);
			dropped = u64_stats_read(&stats->twq.dropped);
		} while (u64_stats_fetch_retry(&stats->syncp, start));

		stats64->tx_bytes += bytes;
		stats64->tx_packets += packets;
		stats64->tx_dropped += dropped;
	}

	for (i = 0; i < base->num_rx_queues; i++) {
		struct mpnic_ring *rxr = base->rxr[i];

		stats = &rxr->stats;
		do {
			start = u64_stats_fetch_begin(&stats->syncp);
			bytes = u64_stats_read(&stats->rcq.bytes);
			packets = u64_stats_read(&stats->rcq.packets);
			dropped = u64_stats_read(&stats->rcq.dropped);
			errors = u64_stats_read(&stats->rcq.errors);
		} while (u64_stats_fetch_retry(&stats->syncp, start));

		stats64->rx_bytes += bytes;
		stats64->rx_packets += packets;
		stats64->rx_dropped += dropped;
		stats64->rx_errors += errors;
	}

	rcu_read_unlock();
}

static const struct net_device_ops mpnic_netdev_ops = {
	.ndo_open		= mpnic_open,
	.ndo_stop		= mpnic_stop,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_start_xmit		= mpnic_xmit_frame,
	.ndo_get_stats64	= mpnic_get_stats64,
};

static void mpnic_get_queue_stats_rx(struct net_device *dev, int idx,
				     struct netdev_queue_stats_rx *rx)
{
	struct mpnic_net *mpn = netdev_priv(dev);
	struct mpnic_ring *rxr = mpn->rx[idx];
	struct mpnic_queue_stats *stats;
	unsigned int start;
	u64 bytes, packets;

	if (!rxr)
		return;

	stats = &rxr->stats;
	do {
		start = u64_stats_fetch_begin(&stats->syncp);
		bytes = u64_stats_read(&stats->rcq.bytes);
		packets = u64_stats_read(&stats->rcq.packets);
	} while (u64_stats_fetch_retry(&stats->syncp, start));

	rx->bytes = bytes;
	rx->packets = packets;
}

static void mpnic_get_queue_stats_tx(struct net_device *dev, int idx,
				     struct netdev_queue_stats_tx *tx)
{
	struct mpnic_net *mpn = netdev_priv(dev);
	struct mpnic_ring *txr = mpn->tx[idx];
	struct mpnic_queue_stats *stats;
	u64 bytes, packets, stop, wake;
	struct mpnic_q_triad *qt;
	unsigned int start;

	if (!txr)
		return;

	qt = container_of(txr, struct mpnic_q_triad, sub0);

	stats = &qt->cmpl.stats;
	do {
		start = u64_stats_fetch_begin(&stats->syncp);
		bytes = u64_stats_read(&stats->tcq.bytes);
		packets = u64_stats_read(&stats->tcq.packets);
		wake = u64_stats_read(&stats->tcq.wake);
	} while (u64_stats_fetch_retry(&stats->syncp, start));

	stats = &txr->stats;
	do {
		start = u64_stats_fetch_begin(&stats->syncp);
		stop = u64_stats_read(&stats->twq.stop);
	} while (u64_stats_fetch_retry(&stats->syncp, start));

	tx->bytes = bytes;
	tx->packets = packets;
	tx->stop = stop;
	tx->wake = wake;
}

static void mpnic_get_base_stats(struct net_device *dev,
				 struct netdev_queue_stats_rx *rx,
				 struct netdev_queue_stats_tx *tx)
{
	struct mpnic_net *mpn = netdev_priv(dev);
	struct mpnic_base_stats *base;

	base = netdev_lock_dereference(mpn->stats.base, dev);

	tx->bytes = base->tx.bytes;
	tx->packets = base->tx.packets;
	tx->stop = base->tx.stop;
	tx->wake = base->tx.wake;

	rx->bytes = base->rx.bytes;
	rx->packets = base->rx.packets;
}

static const struct netdev_stat_ops mpnic_stat_ops = {
	.get_queue_stats_rx	= mpnic_get_queue_stats_rx,
	.get_queue_stats_tx	= mpnic_get_queue_stats_tx,
	.get_base_stats		= mpnic_get_base_stats,
};

/**
 * mpnic_netdev_free - Free the netdev associated with mpnic
 * @mpd: Driver specific structure to free netdev from
 **/
void mpnic_netdev_free(struct mpnic_dev *mpd)
{
	free_netdev(mpd->netdev);
	mpd->netdev = NULL;
}

/**
 * mpnic_netdev_alloc - Allocate a netdev and associate it with mpnic
 * @mpd: Driver specific structure to associate the netdev with
 *
 * Return: NULL on failure.
 **/
struct net_device *mpnic_netdev_alloc(struct mpnic_dev *mpd)
{
	struct net_device *netdev;
	struct mpnic_net *mpn;
	unsigned int queues;

	netdev = alloc_etherdev_mq(sizeof(*mpn), MPNIC_MAX_RXQS);
	if (!netdev)
		return NULL;

	SET_NETDEV_DEV(netdev, mpd->dev);
	mpd->netdev = netdev;

	netdev->netdev_ops = &mpnic_netdev_ops;
	netdev->stat_ops = &mpnic_stat_ops;
	netdev->request_ops_lock = true;

	mpn = netdev_priv(netdev);
	mpn->netdev = netdev;
	mpn->mpd = mpd;

	mpn->stats.buf[0].txr = mpn->tx;
	mpn->stats.buf[0].rxr = mpn->rx;
	RCU_INIT_POINTER(mpn->stats.base, &mpn->stats.buf[0]);

	mpn->txq_size = MPNIC_TXQ_SIZE_DEFAULT;
	mpn->hpq_size = MPNIC_HPQ_SIZE_DEFAULT;
	mpn->ppq_size = MPNIC_PPQ_SIZE_DEFAULT;
	mpn->rcq_size = MPNIC_RCQ_SIZE_DEFAULT;

	queues = min(netif_get_num_default_rss_queues(),
		     mpd->num_irqs - MPNIC_NON_NAPI_VECTORS);
	mpn->num_tx_queues = queues;
	mpn->num_rx_queues = queues;
	mpn->num_napi = queues;

	netdev->features |= NETIF_F_SG;
	netdev->hw_features |= netdev->features;
	netdev->vlan_features |= netdev->features;

	netdev->min_mtu = IPV6_MIN_MTU;
	netdev->max_mtu = MPNIC_MAX_JUMBO_FRAME_SIZE - ETH_HLEN;

	netif_carrier_off(netdev);
	netif_tx_stop_all_queues(netdev);

	return netdev;
}

static int mpnic_dsn_to_mac_addr(u64 dsn, u8 *addr)
{
	addr[0] = (dsn >> 56) & 0xFF;
	addr[1] = (dsn >> 48) & 0xFF;
	addr[2] = (dsn >> 40) & 0xFF;
	addr[3] = (dsn >> 16) & 0xFF;
	addr[4] = (dsn >> 8) & 0xFF;
	addr[5] = dsn & 0xFF;

	return is_valid_ether_addr(addr) ? 0 : -EINVAL;
}

/**
 * mpnic_netdev_register - Assign the MAC address and register the netdev
 * @netdev: Netdev to register
 *
 * The permanent address is derived from the PCIe device serial number, the
 * same way the firmware and the BMC derive it. A random address would break
 * provisioning, so refuse to spawn the interface if the serial number does
 * not yield a valid one.
 *
 * Return: non-zero on failure.
 **/
int mpnic_netdev_register(struct net_device *netdev)
{
	struct mpnic_net *mpn = netdev_priv(netdev);
	struct mpnic_dev *mpd = mpn->mpd;
	u8 addr[ETH_ALEN];
	int err;

	err = mpnic_dsn_to_mac_addr(mpd->dsn, addr);
	if (err) {
		dev_err(mpd->dev, "MAC addr %pM invalid\n", addr);
		return err;
	}

	ether_addr_copy(netdev->perm_addr, addr);
	eth_hw_addr_set(netdev, addr);

	err = netif_set_real_num_queues(netdev, mpn->num_tx_queues,
					mpn->num_rx_queues);
	if (err)
		return err;

	/* Abort if MMIO has failed. This has to be the last check before
	 * registration, the register accessors can only detach the device
	 * once it has been registered.
	 */
	if (!mpnic_present(mpd))
		return -EIO;

	return register_netdev(netdev);
}
