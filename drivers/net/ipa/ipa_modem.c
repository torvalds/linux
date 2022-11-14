// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2014-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2022 Linaro Ltd.
 */

#include <linux/errno.h>
#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/if_rmnet.h>
#include <linux/etherdevice.h>
#include <net/pkt_sched.h>
#include <linux/pm_runtime.h>
#include <linux/remoteproc/qcom_rproc.h>

#include "ipa.h"
#include "ipa_data.h"
#include "ipa_endpoint.h"
#include "ipa_table.h"
#include "ipa_mem.h"
#include "ipa_modem.h"
#include "ipa_smp2p.h"
#include "ipa_qmi.h"
#include "ipa_uc.h"
#include "ipa_power.h"

#define IPA_NETDEV_NAME		"rmnet_ipa%d"
#define IPA_NETDEV_TAILROOM	0	/* for padding by mux layer */
#define IPA_NETDEV_TIMEOUT	10	/* seconds */

enum ipa_modem_state {
	IPA_MODEM_STATE_STOPPED	= 0,
	IPA_MODEM_STATE_STARTING,
	IPA_MODEM_STATE_RUNNING,
	IPA_MODEM_STATE_STOPPING,
};

/**
 * struct ipa_priv - IPA network device private data
 * @ipa:	IPA pointer
 * @work:	Work structure used to wake the modem netdev TX queue
 */
struct ipa_priv {
	struct ipa *ipa;
	struct work_struct work;
};

/** ipa_open() - Opens the modem network interface */
static int ipa_open(struct net_device *netdev)
{
	struct ipa_priv *priv = netdev_priv(netdev);
	struct ipa *ipa = priv->ipa;
	struct device *dev;
	int ret;

	dev = &ipa->pdev->dev;
	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		goto err_power_put;

	ret = ipa_endpoint_enable_one(ipa->name_map[IPA_ENDPOINT_AP_MODEM_TX]);
	if (ret)
		goto err_power_put;

	ret = ipa_endpoint_enable_one(ipa->name_map[IPA_ENDPOINT_AP_MODEM_RX]);
	if (ret)
		goto err_disable_tx;

	netif_start_queue(netdev);

	pm_runtime_mark_last_busy(dev);
	(void)pm_runtime_put_autosuspend(dev);

	return 0;

err_disable_tx:
	ipa_endpoint_disable_one(ipa->name_map[IPA_ENDPOINT_AP_MODEM_TX]);
err_power_put:
	pm_runtime_put_noidle(dev);

	return ret;
}

/** ipa_stop() - Stops the modem network interface. */
static int ipa_stop(struct net_device *netdev)
{
	struct ipa_priv *priv = netdev_priv(netdev);
	struct ipa *ipa = priv->ipa;
	struct device *dev;
	int ret;

	dev = &ipa->pdev->dev;
	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		goto out_power_put;

	netif_stop_queue(netdev);

	ipa_endpoint_disable_one(ipa->name_map[IPA_ENDPOINT_AP_MODEM_RX]);
	ipa_endpoint_disable_one(ipa->name_map[IPA_ENDPOINT_AP_MODEM_TX]);
out_power_put:
	pm_runtime_mark_last_busy(dev);
	(void)pm_runtime_put_autosuspend(dev);

	return 0;
}

/** ipa_start_xmit() - Transmits an skb.
 * @skb: skb to be transmitted
 * @dev: network device
 *
 * Return codes:
 * NETDEV_TX_OK: Success
 * NETDEV_TX_BUSY: Error while transmitting the skb. Try again later
 */
static netdev_tx_t
ipa_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct net_device_stats *stats = &netdev->stats;
	struct ipa_priv *priv = netdev_priv(netdev);
	struct ipa_endpoint *endpoint;
	struct ipa *ipa = priv->ipa;
	u32 skb_len = skb->len;
	struct device *dev;
	int ret;

	if (!skb_len)
		goto err_drop_skb;

	endpoint = ipa->name_map[IPA_ENDPOINT_AP_MODEM_TX];
	if (endpoint->config.qmap && skb->protocol != htons(ETH_P_MAP))
		goto err_drop_skb;

	/* The hardware must be powered for us to transmit */
	dev = &ipa->pdev->dev;
	ret = pm_runtime_get(dev);
	if (ret < 1) {
		/* If a resume won't happen, just drop the packet */
		if (ret < 0 && ret != -EINPROGRESS) {
			ipa_power_modem_queue_active(ipa);
			pm_runtime_put_noidle(dev);
			goto err_drop_skb;
		}

		/* No power (yet).  Stop the network stack from transmitting
		 * until we're resumed; ipa_modem_resume() arranges for the
		 * TX queue to be started again.
		 */
		ipa_power_modem_queue_stop(ipa);

		pm_runtime_put_noidle(dev);

		return NETDEV_TX_BUSY;
	}

	ipa_power_modem_queue_active(ipa);

	ret = ipa_endpoint_skb_tx(endpoint, skb);

	pm_runtime_mark_last_busy(dev);
	(void)pm_runtime_put_autosuspend(dev);

	if (ret) {
		if (ret != -E2BIG)
			return NETDEV_TX_BUSY;
		goto err_drop_skb;
	}

	stats->tx_packets++;
	stats->tx_bytes += skb_len;

	return NETDEV_TX_OK;

err_drop_skb:
	dev_kfree_skb_any(skb);
	stats->tx_dropped++;

	return NETDEV_TX_OK;
}

void ipa_modem_skb_rx(struct net_device *netdev, struct sk_buff *skb)
{
	struct net_device_stats *stats = &netdev->stats;

	if (skb) {
		skb->dev = netdev;
		skb->protocol = htons(ETH_P_MAP);
		stats->rx_packets++;
		stats->rx_bytes += skb->len;

		(void)netif_receive_skb(skb);
	} else {
		stats->rx_dropped++;
	}
}

static const struct net_device_ops ipa_modem_ops = {
	.ndo_open	= ipa_open,
	.ndo_stop	= ipa_stop,
	.ndo_start_xmit	= ipa_start_xmit,
};

/** ipa_modem_netdev_setup() - netdev setup function for the modem */
static void ipa_modem_netdev_setup(struct net_device *netdev)
{
	netdev->netdev_ops = &ipa_modem_ops;

	netdev->header_ops = NULL;
	netdev->type = ARPHRD_RAWIP;
	netdev->hard_header_len = 0;
	netdev->min_header_len = ETH_HLEN;
	netdev->min_mtu = ETH_MIN_MTU;
	netdev->max_mtu = IPA_MTU;
	netdev->mtu = netdev->max_mtu;
	netdev->addr_len = 0;
	netdev->tx_queue_len = DEFAULT_TX_QUEUE_LEN;
	netdev->flags &= ~(IFF_BROADCAST | IFF_MULTICAST);
	netdev->priv_flags |= IFF_TX_SKB_SHARING;
	eth_broadcast_addr(netdev->broadcast);

	/* The endpoint is configured for QMAP */
	netdev->needed_headroom = sizeof(struct rmnet_map_header);
	netdev->needed_tailroom = IPA_NETDEV_TAILROOM;
	netdev->watchdog_timeo = IPA_NETDEV_TIMEOUT * HZ;
	netdev->hw_features = NETIF_F_SG;
}

/** ipa_modem_suspend() - suspend callback
 * @netdev:	Network device
 *
 * Suspend the modem's endpoints.
 */
void ipa_modem_suspend(struct net_device *netdev)
{
	struct ipa_priv *priv = netdev_priv(netdev);
	struct ipa *ipa = priv->ipa;

	if (!(netdev->flags & IFF_UP))
		return;

	ipa_endpoint_suspend_one(ipa->name_map[IPA_ENDPOINT_AP_MODEM_RX]);
	ipa_endpoint_suspend_one(ipa->name_map[IPA_ENDPOINT_AP_MODEM_TX]);
}

/**
 * ipa_modem_wake_queue_work() - enable modem netdev queue
 * @work:	Work structure
 *
 * Re-enable transmit on the modem network device.  This is called
 * in (power management) work queue context, scheduled when resuming
 * the modem.  We can't enable the queue directly in ipa_modem_resume()
 * because transmits restart the instant the queue is awakened; but the
 * device power state won't be ACTIVE until *after* ipa_modem_resume()
 * returns.
 */
static void ipa_modem_wake_queue_work(struct work_struct *work)
{
	struct ipa_priv *priv = container_of(work, struct ipa_priv, work);

	ipa_power_modem_queue_wake(priv->ipa);
}

/** ipa_modem_resume() - resume callback for runtime_pm
 * @dev: pointer to device
 *
 * Resume the modem's endpoints.
 */
void ipa_modem_resume(struct net_device *netdev)
{
	struct ipa_priv *priv = netdev_priv(netdev);
	struct ipa *ipa = priv->ipa;

	if (!(netdev->flags & IFF_UP))
		return;

	ipa_endpoint_resume_one(ipa->name_map[IPA_ENDPOINT_AP_MODEM_TX]);
	ipa_endpoint_resume_one(ipa->name_map[IPA_ENDPOINT_AP_MODEM_RX]);

	/* Arrange for the TX queue to be restarted */
	(void)queue_pm_work(&priv->work);
}

int ipa_modem_start(struct ipa *ipa)
{
	enum ipa_modem_state state;
	struct net_device *netdev;
	struct ipa_priv *priv;
	int ret;

	/* Only attempt to start the modem if it's stopped */
	state = atomic_cmpxchg(&ipa->modem_state, IPA_MODEM_STATE_STOPPED,
			       IPA_MODEM_STATE_STARTING);

	/* Silently ignore attempts when running, or when changing state */
	if (state != IPA_MODEM_STATE_STOPPED)
		return 0;

	netdev = alloc_netdev(sizeof(struct ipa_priv), IPA_NETDEV_NAME,
			      NET_NAME_UNKNOWN, ipa_modem_netdev_setup);
	if (!netdev) {
		ret = -ENOMEM;
		goto out_set_state;
	}

	SET_NETDEV_DEV(netdev, &ipa->pdev->dev);
	priv = netdev_priv(netdev);
	priv->ipa = ipa;
	INIT_WORK(&priv->work, ipa_modem_wake_queue_work);
	ipa->name_map[IPA_ENDPOINT_AP_MODEM_TX]->netdev = netdev;
	ipa->name_map[IPA_ENDPOINT_AP_MODEM_RX]->netdev = netdev;
	ipa->modem_netdev = netdev;

	ret = register_netdev(netdev);
	if (ret) {
		ipa->modem_netdev = NULL;
		ipa->name_map[IPA_ENDPOINT_AP_MODEM_RX]->netdev = NULL;
		ipa->name_map[IPA_ENDPOINT_AP_MODEM_TX]->netdev = NULL;
		free_netdev(netdev);
	}

out_set_state:
	if (ret)
		atomic_set(&ipa->modem_state, IPA_MODEM_STATE_STOPPED);
	else
		atomic_set(&ipa->modem_state, IPA_MODEM_STATE_RUNNING);
	smp_mb__after_atomic();

	return ret;
}

int ipa_modem_stop(struct ipa *ipa)
{
	struct net_device *netdev = ipa->modem_netdev;
	enum ipa_modem_state state;

	/* Only attempt to stop the modem if it's running */
	state = atomic_cmpxchg(&ipa->modem_state, IPA_MODEM_STATE_RUNNING,
			       IPA_MODEM_STATE_STOPPING);

	/* Silently ignore attempts when already stopped */
	if (state == IPA_MODEM_STATE_STOPPED)
		return 0;

	/* If we're somewhere between stopped and starting, we're busy */
	if (state != IPA_MODEM_STATE_RUNNING)
		return -EBUSY;

	/* Clean up the netdev and endpoints if it was started */
	if (netdev) {
		struct ipa_priv *priv = netdev_priv(netdev);

		cancel_work_sync(&priv->work);
		/* If it was opened, stop it first */
		if (netdev->flags & IFF_UP)
			(void)ipa_stop(netdev);
		unregister_netdev(netdev);
		ipa->modem_netdev = NULL;
		ipa->name_map[IPA_ENDPOINT_AP_MODEM_RX]->netdev = NULL;
		ipa->name_map[IPA_ENDPOINT_AP_MODEM_TX]->netdev = NULL;
		free_netdev(netdev);
	}

	atomic_set(&ipa->modem_state, IPA_MODEM_STATE_STOPPED);
	smp_mb__after_atomic();

	return 0;
}

/* Treat a "clean" modem stop the same as a crash */
static void ipa_modem_crashed(struct ipa *ipa)
{
	struct device *dev = &ipa->pdev->dev;
	int ret;

	/* Prevent the modem from triggering a call to ipa_setup() */
	ipa_smp2p_irq_disable_setup(ipa);

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "error %d getting power to handle crash\n", ret);
		goto out_power_put;
	}

	ipa_endpoint_modem_pause_all(ipa, true);

	ipa_endpoint_modem_hol_block_clear_all(ipa);

	ipa_table_reset(ipa, true);

	ret = ipa_table_hash_flush(ipa);
	if (ret)
		dev_err(dev, "error %d flushing hash caches\n", ret);

	ret = ipa_endpoint_modem_exception_reset_all(ipa);
	if (ret)
		dev_err(dev, "error %d resetting exception endpoint\n", ret);

	ipa_endpoint_modem_pause_all(ipa, false);

	ret = ipa_modem_stop(ipa);
	if (ret)
		dev_err(dev, "error %d stopping modem\n", ret);

	/* Now prepare for the next modem boot */
	ret = ipa_mem_zero_modem(ipa);
	if (ret)
		dev_err(dev, "error %d zeroing modem memory regions\n", ret);

out_power_put:
	pm_runtime_mark_last_busy(dev);
	(void)pm_runtime_put_autosuspend(dev);
}

static int ipa_modem_notify(struct notifier_block *nb, unsigned long action,
			    void *data)
{
	struct ipa *ipa = container_of(nb, struct ipa, nb);
	struct qcom_ssr_notify_data *notify_data = data;
	struct device *dev = &ipa->pdev->dev;

	switch (action) {
	case QCOM_SSR_BEFORE_POWERUP:
		dev_info(dev, "received modem starting event\n");
		ipa_uc_power(ipa);
		ipa_smp2p_notify_reset(ipa);
		break;

	case QCOM_SSR_AFTER_POWERUP:
		dev_info(dev, "received modem running event\n");
		break;

	case QCOM_SSR_BEFORE_SHUTDOWN:
		dev_info(dev, "received modem %s event\n",
			 notify_data->crashed ? "crashed" : "stopping");
		if (ipa->setup_complete)
			ipa_modem_crashed(ipa);
		break;

	case QCOM_SSR_AFTER_SHUTDOWN:
		dev_info(dev, "received modem offline event\n");
		break;

	default:
		dev_err(dev, "received unrecognized event %lu\n", action);
		break;
	}

	return NOTIFY_OK;
}

int ipa_modem_config(struct ipa *ipa)
{
	void *notifier;

	ipa->nb.notifier_call = ipa_modem_notify;

	notifier = qcom_register_ssr_notifier("mpss", &ipa->nb);
	if (IS_ERR(notifier))
		return PTR_ERR(notifier);

	ipa->notifier = notifier;

	return 0;
}

void ipa_modem_deconfig(struct ipa *ipa)
{
	struct device *dev = &ipa->pdev->dev;
	int ret;

	ret = qcom_unregister_ssr_notifier(ipa->notifier, &ipa->nb);
	if (ret)
		dev_err(dev, "error %d unregistering notifier", ret);

	ipa->notifier = NULL;
	memset(&ipa->nb, 0, sizeof(ipa->nb));
}
