// SPDX-License-Identifier: GPL-2.0-or-later
/* MHI Network driver - Network over MHI bus
 *
 * Copyright (C) 2020 Linaro Ltd <loic.poulain@linaro.org>
 */

#include <linux/if_arp.h>
#include <linux/mhi.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/u64_stats_sync.h>

#define MHI_NET_MIN_MTU		ETH_MIN_MTU
#define MHI_NET_MAX_MTU		0xffff
#define MHI_NET_DEFAULT_MTU	0x4000

struct mhi_net_stats {
	u64_stats_t rx_packets;
	u64_stats_t rx_bytes;
	u64_stats_t rx_errors;
	u64_stats_t rx_dropped;
	u64_stats_t tx_packets;
	u64_stats_t tx_bytes;
	u64_stats_t tx_errors;
	u64_stats_t tx_dropped;
	atomic_t rx_queued;
	struct u64_stats_sync tx_syncp;
	struct u64_stats_sync rx_syncp;
};

struct mhi_net_dev {
	struct mhi_device *mdev;
	struct net_device *ndev;
	struct delayed_work rx_refill;
	struct mhi_net_stats stats;
	u32 rx_queue_sz;
};

static int mhi_ndo_open(struct net_device *ndev)
{
	struct mhi_net_dev *mhi_netdev = netdev_priv(ndev);

	/* Feed the rx buffer pool */
	schedule_delayed_work(&mhi_netdev->rx_refill, 0);

	/* Carrier is established via out-of-band channel (e.g. qmi) */
	netif_carrier_on(ndev);

	netif_start_queue(ndev);

	return 0;
}

static int mhi_ndo_stop(struct net_device *ndev)
{
	struct mhi_net_dev *mhi_netdev = netdev_priv(ndev);

	netif_stop_queue(ndev);
	netif_carrier_off(ndev);
	cancel_delayed_work_sync(&mhi_netdev->rx_refill);

	return 0;
}

static int mhi_ndo_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct mhi_net_dev *mhi_netdev = netdev_priv(ndev);
	struct mhi_device *mdev = mhi_netdev->mdev;
	int err;

	err = mhi_queue_skb(mdev, DMA_TO_DEVICE, skb, skb->len, MHI_EOT);
	if (unlikely(err)) {
		net_err_ratelimited("%s: Failed to queue TX buf (%d)\n",
				    ndev->name, err);

		u64_stats_update_begin(&mhi_netdev->stats.tx_syncp);
		u64_stats_inc(&mhi_netdev->stats.tx_dropped);
		u64_stats_update_end(&mhi_netdev->stats.tx_syncp);

		/* drop the packet */
		dev_kfree_skb_any(skb);
	}

	if (mhi_queue_is_full(mdev, DMA_TO_DEVICE))
		netif_stop_queue(ndev);

	return NETDEV_TX_OK;
}

static void mhi_ndo_get_stats64(struct net_device *ndev,
				struct rtnl_link_stats64 *stats)
{
	struct mhi_net_dev *mhi_netdev = netdev_priv(ndev);
	unsigned int start;

	do {
		start = u64_stats_fetch_begin_irq(&mhi_netdev->stats.rx_syncp);
		stats->rx_packets = u64_stats_read(&mhi_netdev->stats.rx_packets);
		stats->rx_bytes = u64_stats_read(&mhi_netdev->stats.rx_bytes);
		stats->rx_errors = u64_stats_read(&mhi_netdev->stats.rx_errors);
		stats->rx_dropped = u64_stats_read(&mhi_netdev->stats.rx_dropped);
	} while (u64_stats_fetch_retry_irq(&mhi_netdev->stats.rx_syncp, start));

	do {
		start = u64_stats_fetch_begin_irq(&mhi_netdev->stats.tx_syncp);
		stats->tx_packets = u64_stats_read(&mhi_netdev->stats.tx_packets);
		stats->tx_bytes = u64_stats_read(&mhi_netdev->stats.tx_bytes);
		stats->tx_errors = u64_stats_read(&mhi_netdev->stats.tx_errors);
		stats->tx_dropped = u64_stats_read(&mhi_netdev->stats.tx_dropped);
	} while (u64_stats_fetch_retry_irq(&mhi_netdev->stats.tx_syncp, start));
}

static const struct net_device_ops mhi_netdev_ops = {
	.ndo_open               = mhi_ndo_open,
	.ndo_stop               = mhi_ndo_stop,
	.ndo_start_xmit         = mhi_ndo_xmit,
	.ndo_get_stats64	= mhi_ndo_get_stats64,
};

static void mhi_net_setup(struct net_device *ndev)
{
	ndev->header_ops = NULL;  /* No header */
	ndev->type = ARPHRD_NONE; /* QMAP... */
	ndev->hard_header_len = 0;
	ndev->addr_len = 0;
	ndev->flags = IFF_POINTOPOINT | IFF_NOARP;
	ndev->netdev_ops = &mhi_netdev_ops;
	ndev->mtu = MHI_NET_DEFAULT_MTU;
	ndev->min_mtu = MHI_NET_MIN_MTU;
	ndev->max_mtu = MHI_NET_MAX_MTU;
	ndev->tx_queue_len = 1000;
}

static void mhi_net_dl_callback(struct mhi_device *mhi_dev,
				struct mhi_result *mhi_res)
{
	struct mhi_net_dev *mhi_netdev = dev_get_drvdata(&mhi_dev->dev);
	struct sk_buff *skb = mhi_res->buf_addr;
	int remaining;

	remaining = atomic_dec_return(&mhi_netdev->stats.rx_queued);

	if (unlikely(mhi_res->transaction_status)) {
		dev_kfree_skb_any(skb);

		/* MHI layer stopping/resetting the DL channel */
		if (mhi_res->transaction_status == -ENOTCONN)
			return;

		u64_stats_update_begin(&mhi_netdev->stats.rx_syncp);
		u64_stats_inc(&mhi_netdev->stats.rx_errors);
		u64_stats_update_end(&mhi_netdev->stats.rx_syncp);
	} else {
		u64_stats_update_begin(&mhi_netdev->stats.rx_syncp);
		u64_stats_inc(&mhi_netdev->stats.rx_packets);
		u64_stats_add(&mhi_netdev->stats.rx_bytes, mhi_res->bytes_xferd);
		u64_stats_update_end(&mhi_netdev->stats.rx_syncp);

		skb->protocol = htons(ETH_P_MAP);
		skb_put(skb, mhi_res->bytes_xferd);
		netif_rx(skb);
	}

	/* Refill if RX buffers queue becomes low */
	if (remaining <= mhi_netdev->rx_queue_sz / 2)
		schedule_delayed_work(&mhi_netdev->rx_refill, 0);
}

static void mhi_net_ul_callback(struct mhi_device *mhi_dev,
				struct mhi_result *mhi_res)
{
	struct mhi_net_dev *mhi_netdev = dev_get_drvdata(&mhi_dev->dev);
	struct net_device *ndev = mhi_netdev->ndev;
	struct mhi_device *mdev = mhi_netdev->mdev;
	struct sk_buff *skb = mhi_res->buf_addr;

	/* Hardware has consumed the buffer, so free the skb (which is not
	 * freed by the MHI stack) and perform accounting.
	 */
	dev_consume_skb_any(skb);

	u64_stats_update_begin(&mhi_netdev->stats.tx_syncp);
	if (unlikely(mhi_res->transaction_status)) {

		/* MHI layer stopping/resetting the UL channel */
		if (mhi_res->transaction_status == -ENOTCONN) {
			u64_stats_update_end(&mhi_netdev->stats.tx_syncp);
			return;
		}

		u64_stats_inc(&mhi_netdev->stats.tx_errors);
	} else {
		u64_stats_inc(&mhi_netdev->stats.tx_packets);
		u64_stats_add(&mhi_netdev->stats.tx_bytes, mhi_res->bytes_xferd);
	}
	u64_stats_update_end(&mhi_netdev->stats.tx_syncp);

	if (netif_queue_stopped(ndev) && !mhi_queue_is_full(mdev, DMA_TO_DEVICE))
		netif_wake_queue(ndev);
}

static void mhi_net_rx_refill_work(struct work_struct *work)
{
	struct mhi_net_dev *mhi_netdev = container_of(work, struct mhi_net_dev,
						      rx_refill.work);
	struct net_device *ndev = mhi_netdev->ndev;
	struct mhi_device *mdev = mhi_netdev->mdev;
	int size = READ_ONCE(ndev->mtu);
	struct sk_buff *skb;
	int err;

	while (atomic_read(&mhi_netdev->stats.rx_queued) < mhi_netdev->rx_queue_sz) {
		skb = netdev_alloc_skb(ndev, size);
		if (unlikely(!skb))
			break;

		err = mhi_queue_skb(mdev, DMA_FROM_DEVICE, skb, size, MHI_EOT);
		if (unlikely(err)) {
			net_err_ratelimited("%s: Failed to queue RX buf (%d)\n",
					    ndev->name, err);
			kfree_skb(skb);
			break;
		}

		atomic_inc(&mhi_netdev->stats.rx_queued);

		/* Do not hog the CPU if rx buffers are consumed faster than
		 * queued (unlikely).
		 */
		cond_resched();
	}

	/* If we're still starved of rx buffers, reschedule later */
	if (unlikely(!atomic_read(&mhi_netdev->stats.rx_queued)))
		schedule_delayed_work(&mhi_netdev->rx_refill, HZ / 2);
}

static int mhi_net_probe(struct mhi_device *mhi_dev,
			 const struct mhi_device_id *id)
{
	const char *netname = (char *)id->driver_data;
	struct device *dev = &mhi_dev->dev;
	struct mhi_net_dev *mhi_netdev;
	struct net_device *ndev;
	int err;

	ndev = alloc_netdev(sizeof(*mhi_netdev), netname, NET_NAME_PREDICTABLE,
			    mhi_net_setup);
	if (!ndev)
		return -ENOMEM;

	mhi_netdev = netdev_priv(ndev);
	dev_set_drvdata(dev, mhi_netdev);
	mhi_netdev->ndev = ndev;
	mhi_netdev->mdev = mhi_dev;
	SET_NETDEV_DEV(ndev, &mhi_dev->dev);

	/* All MHI net channels have 128 ring elements (at least for now) */
	mhi_netdev->rx_queue_sz = 128;

	INIT_DELAYED_WORK(&mhi_netdev->rx_refill, mhi_net_rx_refill_work);
	u64_stats_init(&mhi_netdev->stats.rx_syncp);
	u64_stats_init(&mhi_netdev->stats.tx_syncp);

	/* Start MHI channels */
	err = mhi_prepare_for_transfer(mhi_dev);
	if (err)
		goto out_err;

	err = register_netdev(ndev);
	if (err)
		goto out_err;

	return 0;

out_err:
	free_netdev(ndev);
	return err;
}

static void mhi_net_remove(struct mhi_device *mhi_dev)
{
	struct mhi_net_dev *mhi_netdev = dev_get_drvdata(&mhi_dev->dev);

	unregister_netdev(mhi_netdev->ndev);

	mhi_unprepare_from_transfer(mhi_netdev->mdev);

	free_netdev(mhi_netdev->ndev);
}

static const struct mhi_device_id mhi_net_id_table[] = {
	{ .chan = "IP_HW0", .driver_data = (kernel_ulong_t)"mhi_hwip%d" },
	{ .chan = "IP_SW0", .driver_data = (kernel_ulong_t)"mhi_swip%d" },
	{}
};
MODULE_DEVICE_TABLE(mhi, mhi_net_id_table);

static struct mhi_driver mhi_net_driver = {
	.probe = mhi_net_probe,
	.remove = mhi_net_remove,
	.dl_xfer_cb = mhi_net_dl_callback,
	.ul_xfer_cb = mhi_net_ul_callback,
	.id_table = mhi_net_id_table,
	.driver = {
		.name = "mhi_net",
		.owner = THIS_MODULE,
	},
};

module_mhi_driver(mhi_net_driver);

MODULE_AUTHOR("Loic Poulain <loic.poulain@linaro.org>");
MODULE_DESCRIPTION("Network over MHI");
MODULE_LICENSE("GPL v2");
