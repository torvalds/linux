// SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause
/*
 * PCIe NTB Network Linux driver
 */
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/ntb.h>
#include <linux/ntb_transport.h>
#include <linux/slab.h>

#define NTB_NETDEV_VER	"0.7"

MODULE_DESCRIPTION(KBUILD_MODNAME);
MODULE_VERSION(NTB_NETDEV_VER);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Intel Corporation");

/* Time in usecs for tx resource reaper */
static unsigned int tx_time = 1;

/* Number of descriptors to free before resuming tx */
static unsigned int tx_start = 10;

/* Number of descriptors still available before stop upper layer tx */
static unsigned int tx_stop = 5;

#define NTB_NETDEV_MAX_QUEUES		64
#define NTB_NETDEV_DEFAULT_QUEUES	1

struct ntb_netdev;

struct ntb_netdev_queue {
	struct ntb_netdev *ntdev;
	struct ntb_transport_qp *qp;
	struct timer_list tx_timer;
	u16 qid;
};

struct ntb_netdev {
	struct pci_dev *pdev;
	struct device *client_dev;
	struct net_device *ndev;
	unsigned int num_queues;
	struct ntb_netdev_queue *queues;
};

#define	NTB_TX_TIMEOUT_MS	1000
#define	NTB_RXQ_SIZE		100

static void ntb_netdev_update_carrier(struct ntb_netdev *dev)
{
	struct net_device *ndev;
	bool any_up = false;
	unsigned int i;

	ndev = dev->ndev;

	for (i = 0; i < dev->num_queues; i++) {
		if (ntb_transport_link_query(dev->queues[i].qp)) {
			any_up = true;
			break;
		}
	}

	if (any_up)
		netif_carrier_on(ndev);
	else
		netif_carrier_off(ndev);
}

static void ntb_netdev_queue_rx_drain(struct ntb_netdev_queue *queue)
{
	struct sk_buff *skb;
	int len;

	while ((skb = ntb_transport_rx_remove(queue->qp, &len)))
		dev_kfree_skb(skb);
}

static int ntb_netdev_queue_rx_fill(struct net_device *ndev,
				    struct ntb_netdev_queue *queue)
{
	struct sk_buff *skb;
	int rc, i;

	for (i = 0; i < NTB_RXQ_SIZE; i++) {
		skb = netdev_alloc_skb(ndev, ndev->mtu + ETH_HLEN);
		if (!skb)
			return -ENOMEM;

		rc = ntb_transport_rx_enqueue(queue->qp, skb, skb->data,
					      ndev->mtu + ETH_HLEN);
		if (rc) {
			dev_kfree_skb(skb);
			return rc;
		}
	}

	return 0;
}

static void ntb_netdev_event_handler(void *data, int link_is_up)
{
	struct ntb_netdev_queue *q = data;
	struct ntb_netdev *dev = q->ntdev;
	struct net_device *ndev;

	ndev = dev->ndev;

	netdev_dbg(ndev, "Event %x, Link %x, qp %u\n", link_is_up,
		   ntb_transport_link_query(q->qp), q->qid);

	if (netif_running(ndev)) {
		if (link_is_up)
			netif_wake_subqueue(ndev, q->qid);
		else
			netif_stop_subqueue(ndev, q->qid);
	}

	ntb_netdev_update_carrier(dev);
}

static void ntb_netdev_rx_handler(struct ntb_transport_qp *qp, void *qp_data,
				  void *data, int len)
{
	struct ntb_netdev_queue *q = qp_data;
	struct ntb_netdev *dev = q->ntdev;
	struct net_device *ndev;
	struct sk_buff *skb;
	int rc;

	ndev = dev->ndev;
	skb = data;
	if (!skb)
		return;

	netdev_dbg(ndev, "%s: %d byte payload received\n", __func__, len);

	if (len < 0) {
		ndev->stats.rx_errors++;
		ndev->stats.rx_length_errors++;
		goto enqueue_again;
	}

	skb_put(skb, len);
	skb->protocol = eth_type_trans(skb, ndev);
	skb->ip_summed = CHECKSUM_NONE;
	skb_record_rx_queue(skb, q->qid);

	if (netif_rx(skb) == NET_RX_DROP) {
		ndev->stats.rx_errors++;
		ndev->stats.rx_dropped++;
	} else {
		ndev->stats.rx_packets++;
		ndev->stats.rx_bytes += len;
	}

	skb = netdev_alloc_skb(ndev, ndev->mtu + ETH_HLEN);
	if (!skb) {
		ndev->stats.rx_errors++;
		ndev->stats.rx_frame_errors++;
		return;
	}

enqueue_again:
	rc = ntb_transport_rx_enqueue(qp, skb, skb->data, ndev->mtu + ETH_HLEN);
	if (rc) {
		dev_kfree_skb_any(skb);
		ndev->stats.rx_errors++;
		ndev->stats.rx_fifo_errors++;
	}
}

static int __ntb_netdev_maybe_stop_tx(struct net_device *netdev,
				      struct ntb_netdev_queue *q, int size)
{
	netif_stop_subqueue(netdev, q->qid);

	/* Make sure to see the latest value of ntb_transport_tx_free_entry()
	 * since the queue was last started.
	 */
	smp_mb();

	if (likely(ntb_transport_tx_free_entry(q->qp) < size)) {
		mod_timer(&q->tx_timer, jiffies + usecs_to_jiffies(tx_time));
		return -EBUSY;
	}

	/* The subqueue must be kept stopped if the link is down */
	if (ntb_transport_link_query(q->qp))
		netif_start_subqueue(netdev, q->qid);

	return 0;
}

static int ntb_netdev_maybe_stop_tx(struct net_device *ndev,
				    struct ntb_netdev_queue *q, int size)
{
	if (__netif_subqueue_stopped(ndev, q->qid) ||
	    (ntb_transport_tx_free_entry(q->qp) >= size))
		return 0;

	return __ntb_netdev_maybe_stop_tx(ndev, q, size);
}

static void ntb_netdev_tx_handler(struct ntb_transport_qp *qp, void *qp_data,
				  void *data, int len)
{
	struct ntb_netdev_queue *q = qp_data;
	struct ntb_netdev *dev = q->ntdev;
	struct net_device *ndev;
	struct sk_buff *skb;

	ndev = dev->ndev;
	skb = data;
	if (!skb || !ndev)
		return;

	if (len > 0) {
		ndev->stats.tx_packets++;
		ndev->stats.tx_bytes += skb->len;
	} else {
		ndev->stats.tx_errors++;
		ndev->stats.tx_aborted_errors++;
	}

	dev_kfree_skb_any(skb);

	if (ntb_transport_tx_free_entry(qp) >= tx_start) {
		/* Make sure anybody stopping the queue after this sees the new
		 * value of ntb_transport_tx_free_entry()
		 */
		smp_mb();
		if (__netif_subqueue_stopped(ndev, q->qid) &&
		    ntb_transport_link_query(q->qp))
			netif_wake_subqueue(ndev, q->qid);
	}
}

static const struct ntb_queue_handlers ntb_netdev_handlers = {
	.tx_handler = ntb_netdev_tx_handler,
	.rx_handler = ntb_netdev_rx_handler,
	.event_handler = ntb_netdev_event_handler,
};

static netdev_tx_t ntb_netdev_start_xmit(struct sk_buff *skb,
					 struct net_device *ndev)
{
	struct ntb_netdev *dev = netdev_priv(ndev);
	u16 qid = skb_get_queue_mapping(skb);
	struct ntb_netdev_queue *q;
	int rc;

	q = &dev->queues[qid];

	ntb_netdev_maybe_stop_tx(ndev, q, tx_stop);

	rc = ntb_transport_tx_enqueue(q->qp, skb, skb->data, skb->len);
	if (rc)
		goto err;

	/* check for next submit */
	ntb_netdev_maybe_stop_tx(ndev, q, tx_stop);

	return NETDEV_TX_OK;

err:
	ndev->stats.tx_dropped++;
	ndev->stats.tx_errors++;
	return NETDEV_TX_BUSY;
}

static void ntb_netdev_tx_timer(struct timer_list *t)
{
	struct ntb_netdev_queue *q = timer_container_of(q, t, tx_timer);
	struct ntb_netdev *dev = q->ntdev;
	struct net_device *ndev;

	ndev = dev->ndev;

	if (ntb_transport_tx_free_entry(q->qp) < tx_stop) {
		mod_timer(&q->tx_timer, jiffies + usecs_to_jiffies(tx_time));
	} else {
		/* Make sure anybody stopping the queue after this sees the new
		 * value of ntb_transport_tx_free_entry()
		 */
		smp_mb();

		/* The subqueue must be kept stopped if the link is down */
		if (__netif_subqueue_stopped(ndev, q->qid) &&
		    ntb_transport_link_query(q->qp))
			netif_wake_subqueue(ndev, q->qid);
	}
}

static int ntb_netdev_open(struct net_device *ndev)
{
	struct ntb_netdev *dev = netdev_priv(ndev);
	struct ntb_netdev_queue *queue;
	unsigned int q;
	int rc = 0;

	/* Add some empty rx bufs for each queue */
	for (q = 0; q < dev->num_queues; q++) {
		queue = &dev->queues[q];

		rc = ntb_netdev_queue_rx_fill(ndev, queue);
		if (rc)
			goto err;

		timer_setup(&queue->tx_timer, ntb_netdev_tx_timer, 0);
	}

	netif_carrier_off(ndev);
	netif_tx_stop_all_queues(ndev);

	for (q = 0; q < dev->num_queues; q++)
		ntb_transport_link_up(dev->queues[q].qp);

	return 0;

err:
	for (q = 0; q < dev->num_queues; q++) {
		queue = &dev->queues[q];
		ntb_netdev_queue_rx_drain(queue);
	}
	return rc;
}

static int ntb_netdev_close(struct net_device *ndev)
{
	struct ntb_netdev *dev = netdev_priv(ndev);
	struct ntb_netdev_queue *queue;
	unsigned int q;

	netif_tx_stop_all_queues(ndev);
	netif_carrier_off(ndev);

	for (q = 0; q < dev->num_queues; q++) {
		queue = &dev->queues[q];

		ntb_transport_link_down(queue->qp);
		ntb_netdev_queue_rx_drain(queue);
		timer_delete_sync(&queue->tx_timer);
	}

	return 0;
}

static int ntb_netdev_change_mtu(struct net_device *ndev, int new_mtu)
{
	struct ntb_netdev *dev = netdev_priv(ndev);
	struct ntb_netdev_queue *queue;
	struct sk_buff *skb;
	unsigned int q, i;
	int len, rc = 0;

	if (new_mtu > ntb_transport_max_size(dev->queues[0].qp) - ETH_HLEN)
		return -EINVAL;

	if (!netif_running(ndev)) {
		WRITE_ONCE(ndev->mtu, new_mtu);
		return 0;
	}

	/* Bring down the link and dispose of posted rx entries */
	for (q = 0; q < dev->num_queues; q++)
		ntb_transport_link_down(dev->queues[q].qp);

	if (ndev->mtu < new_mtu) {
		for (q = 0; q < dev->num_queues; q++) {
			queue = &dev->queues[q];

			for (i = 0;
			     (skb = ntb_transport_rx_remove(queue->qp, &len));
			     i++)
				dev_kfree_skb(skb);

			for (; i; i--) {
				skb = netdev_alloc_skb(ndev,
						       new_mtu + ETH_HLEN);
				if (!skb) {
					rc = -ENOMEM;
					goto err;
				}

				rc = ntb_transport_rx_enqueue(queue->qp, skb,
							      skb->data,
							      new_mtu +
							      ETH_HLEN);
				if (rc) {
					dev_kfree_skb(skb);
					goto err;
				}
			}
		}
	}

	WRITE_ONCE(ndev->mtu, new_mtu);

	for (q = 0; q < dev->num_queues; q++)
		ntb_transport_link_up(dev->queues[q].qp);

	return 0;

err:
	for (q = 0; q < dev->num_queues; q++) {
		struct ntb_netdev_queue *queue = &dev->queues[q];

		ntb_transport_link_down(queue->qp);

		ntb_netdev_queue_rx_drain(queue);
	}

	netdev_err(ndev, "Error changing MTU, device inoperable\n");
	return rc;
}

static const struct net_device_ops ntb_netdev_ops = {
	.ndo_open = ntb_netdev_open,
	.ndo_stop = ntb_netdev_close,
	.ndo_start_xmit = ntb_netdev_start_xmit,
	.ndo_change_mtu = ntb_netdev_change_mtu,
	.ndo_set_mac_address = eth_mac_addr,
};

static void ntb_get_drvinfo(struct net_device *ndev,
			    struct ethtool_drvinfo *info)
{
	struct ntb_netdev *dev = netdev_priv(ndev);

	strscpy(info->driver, KBUILD_MODNAME, sizeof(info->driver));
	strscpy(info->version, NTB_NETDEV_VER, sizeof(info->version));
	strscpy(info->bus_info, pci_name(dev->pdev), sizeof(info->bus_info));
}

static int ntb_get_link_ksettings(struct net_device *dev,
				  struct ethtool_link_ksettings *cmd)
{
	ethtool_link_ksettings_zero_link_mode(cmd, supported);
	ethtool_link_ksettings_add_link_mode(cmd, supported, Backplane);
	ethtool_link_ksettings_zero_link_mode(cmd, advertising);
	ethtool_link_ksettings_add_link_mode(cmd, advertising, Backplane);

	cmd->base.speed = SPEED_UNKNOWN;
	cmd->base.duplex = DUPLEX_FULL;
	cmd->base.port = PORT_OTHER;
	cmd->base.phy_address = 0;
	cmd->base.autoneg = AUTONEG_ENABLE;

	return 0;
}

static void ntb_get_channels(struct net_device *ndev,
			     struct ethtool_channels *channels)
{
	struct ntb_netdev *dev = netdev_priv(ndev);

	channels->combined_count = dev->num_queues;
	channels->max_combined = ndev->num_tx_queues;
}

static int ntb_inc_channels(struct net_device *ndev,
			    unsigned int old, unsigned int new)
{
	struct ntb_netdev *dev = netdev_priv(ndev);
	bool running = netif_running(ndev);
	struct ntb_netdev_queue *queue;
	unsigned int q, created;
	int rc;

	created = old;
	for (q = old; q < new; q++) {
		queue = &dev->queues[q];

		queue->ntdev = dev;
		queue->qid = q;
		queue->qp = ntb_transport_create_queue(queue, dev->client_dev,
						       &ntb_netdev_handlers);
		if (!queue->qp) {
			rc = -ENOSPC;
			goto err_new;
		}
		created++;

		if (!running)
			continue;

		timer_setup(&queue->tx_timer, ntb_netdev_tx_timer, 0);

		rc = ntb_netdev_queue_rx_fill(ndev, queue);
		if (rc)
			goto err_new;

		/*
		 * Carrier may already be on due to other QPs. Keep the new
		 * subqueue stopped until we get a Link Up event for this QP.
		 */
		netif_stop_subqueue(ndev, q);
	}

	rc = netif_set_real_num_queues(ndev, new, new);
	if (rc)
		goto err_new;

	dev->num_queues = new;

	if (running)
		for (q = old; q < new; q++)
			ntb_transport_link_up(dev->queues[q].qp);

	return 0;

err_new:
	if (running) {
		unsigned int rollback = created;

		while (rollback-- > old) {
			queue = &dev->queues[rollback];
			ntb_transport_link_down(queue->qp);
			ntb_netdev_queue_rx_drain(queue);
			timer_delete_sync(&queue->tx_timer);
		}
	}
	while (created-- > old) {
		queue = &dev->queues[created];
		ntb_transport_free_queue(queue->qp);
		queue->qp = NULL;
	}
	return rc;
}

static int ntb_dec_channels(struct net_device *ndev,
			    unsigned int old, unsigned int new)
{
	struct ntb_netdev *dev = netdev_priv(ndev);
	bool running = netif_running(ndev);
	struct ntb_netdev_queue *queue;
	unsigned int q;
	int rc;

	if (running)
		for (q = new; q < old; q++)
			netif_stop_subqueue(ndev, q);

	rc = netif_set_real_num_queues(ndev, new, new);
	if (rc)
		goto err;

	/* Publish new queue count before invalidating QP pointers */
	dev->num_queues = new;

	for (q = new; q < old; q++) {
		queue = &dev->queues[q];

		if (running) {
			ntb_transport_link_down(queue->qp);
			ntb_netdev_queue_rx_drain(queue);
			timer_delete_sync(&queue->tx_timer);
		}

		ntb_transport_free_queue(queue->qp);
		queue->qp = NULL;
	}

	/*
	 * It might be the case that the removed queues are the only queues that
	 * were up, so see if the global carrier needs to change.
	 */
	ntb_netdev_update_carrier(dev);
	return 0;

err:
	if (running) {
		for (q = new; q < old; q++)
			netif_wake_subqueue(ndev, q);
	}
	return rc;
}

static int ntb_set_channels(struct net_device *ndev,
			    struct ethtool_channels *channels)
{
	struct ntb_netdev *dev = netdev_priv(ndev);
	unsigned int new = channels->combined_count;
	unsigned int old = dev->num_queues;

	if (new == old)
		return 0;

	if (new < old)
		return ntb_dec_channels(ndev, old, new);
	else
		return ntb_inc_channels(ndev, old, new);
}

static const struct ethtool_ops ntb_ethtool_ops = {
	.get_drvinfo = ntb_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_link_ksettings = ntb_get_link_ksettings,
	.get_channels = ntb_get_channels,
	.set_channels = ntb_set_channels,
};

static int ntb_netdev_probe(struct device *client_dev)
{
	struct ntb_dev *ntb;
	struct net_device *ndev;
	struct pci_dev *pdev;
	struct ntb_netdev *dev;
	unsigned int q;
	int rc;

	ntb = dev_ntb(client_dev->parent);
	pdev = ntb->pdev;
	if (!pdev)
		return -ENODEV;

	ndev = alloc_etherdev_mq(sizeof(*dev), NTB_NETDEV_MAX_QUEUES);
	if (!ndev)
		return -ENOMEM;

	SET_NETDEV_DEV(ndev, client_dev);

	dev = netdev_priv(ndev);
	dev->ndev = ndev;
	dev->pdev = pdev;
	dev->client_dev = client_dev;
	dev->num_queues = 0;

	dev->queues = kzalloc_objs(*dev->queues, NTB_NETDEV_MAX_QUEUES,
				   GFP_KERNEL);
	if (!dev->queues) {
		rc = -ENOMEM;
		goto err_free_netdev;
	}

	ndev->features = NETIF_F_HIGHDMA;

	ndev->priv_flags |= IFF_LIVE_ADDR_CHANGE;

	ndev->hw_features = ndev->features;
	ndev->watchdog_timeo = msecs_to_jiffies(NTB_TX_TIMEOUT_MS);

	eth_random_addr(ndev->perm_addr);
	dev_addr_set(ndev, ndev->perm_addr);

	ndev->netdev_ops = &ntb_netdev_ops;
	ndev->ethtool_ops = &ntb_ethtool_ops;

	ndev->min_mtu = 0;
	ndev->max_mtu = ETH_MAX_MTU;

	for (q = 0; q < NTB_NETDEV_DEFAULT_QUEUES; q++) {
		struct ntb_netdev_queue *queue = &dev->queues[q];

		queue->ntdev = dev;
		queue->qid = q;
		queue->qp = ntb_transport_create_queue(queue, client_dev,
						       &ntb_netdev_handlers);
		if (!queue->qp)
			break;

		dev->num_queues++;
	}

	if (!dev->num_queues) {
		rc = -EIO;
		goto err_free_queues;
	}

	rc = netif_set_real_num_queues(ndev, dev->num_queues, dev->num_queues);
	if (rc)
		goto err_free_qps;

	ndev->mtu = ntb_transport_max_size(dev->queues[0].qp) - ETH_HLEN;

	rc = register_netdev(ndev);
	if (rc)
		goto err_free_qps;

	dev_set_drvdata(client_dev, ndev);
	dev_info(&pdev->dev, "%s created with %u queue pairs\n",
		 ndev->name, dev->num_queues);
	return 0;

err_free_qps:
	for (q = 0; q < dev->num_queues; q++)
		ntb_transport_free_queue(dev->queues[q].qp);

err_free_queues:
	kfree(dev->queues);

err_free_netdev:
	free_netdev(ndev);
	return rc;
}

static void ntb_netdev_remove(struct device *client_dev)
{
	struct net_device *ndev = dev_get_drvdata(client_dev);
	struct ntb_netdev *dev = netdev_priv(ndev);
	unsigned int q;

	unregister_netdev(ndev);
	for (q = 0; q < dev->num_queues; q++)
		ntb_transport_free_queue(dev->queues[q].qp);

	kfree(dev->queues);
	free_netdev(ndev);
}

static struct ntb_transport_client ntb_netdev_client = {
	.driver.name = KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.probe = ntb_netdev_probe,
	.remove = ntb_netdev_remove,
};

static int __init ntb_netdev_init_module(void)
{
	int rc;

	rc = ntb_transport_register_client_dev(KBUILD_MODNAME);
	if (rc)
		return rc;

	rc = ntb_transport_register_client(&ntb_netdev_client);
	if (rc) {
		ntb_transport_unregister_client_dev(KBUILD_MODNAME);
		return rc;
	}

	return 0;
}
late_initcall(ntb_netdev_init_module);

static void __exit ntb_netdev_exit_module(void)
{
	ntb_transport_unregister_client(&ntb_netdev_client);
	ntb_transport_unregister_client_dev(KBUILD_MODNAME);
}
module_exit(ntb_netdev_exit_module);
