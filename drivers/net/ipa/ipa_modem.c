// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2014-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2021 Linaro Ltd.
 */

#include <linux/errno.h>
#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/if_rmnet.h>
#include <linux/remoteproc/qcom_rproc.h>

#include "ipa.h"
#include "ipa_data.h"
#include "ipa_endpoint.h"
#include "ipa_table.h"
#include "ipa_mem.h"
#include "ipa_modem.h"
#include "ipa_smp2p.h"
#include "ipa_qmi.h"

#define IPA_NETDEV_NAME		"rmnet_ipa%d"
#define IPA_NETDEV_TAILROOM	0	/* for padding by mux layer */
#define IPA_NETDEV_TIMEOUT	10	/* seconds */

enum ipa_modem_state {
	IPA_MODEM_STATE_STOPPED	= 0,
	IPA_MODEM_STATE_STARTING,
	IPA_MODEM_STATE_RUNNING,
	IPA_MODEM_STATE_STOPPING,
};

/** struct ipa_priv - IPA network device private data */
struct ipa_priv {
	struct ipa *ipa;
};

/** ipa_open() - Opens the modem network interface */
static int ipa_open(struct net_device *netdev)
{
	struct ipa_priv *priv = netdev_priv(netdev);
	struct ipa *ipa = priv->ipa;
	int ret;

	ret = ipa_endpoint_enable_one(ipa->name_map[IPA_ENDPOINT_AP_MODEM_TX]);
	if (ret)
		return ret;
	ret = ipa_endpoint_enable_one(ipa->name_map[IPA_ENDPOINT_AP_MODEM_RX]);
	if (ret)
		goto err_disable_tx;

	netif_start_queue(netdev);

	return 0;

err_disable_tx:
	ipa_endpoint_disable_one(ipa->name_map[IPA_ENDPOINT_AP_MODEM_TX]);

	return ret;
}

/** ipa_stop() - Stops the modem network interface. */
static int ipa_stop(struct net_device *netdev)
{
	struct ipa_priv *priv = netdev_priv(netdev);
	struct ipa *ipa = priv->ipa;

	netif_stop_queue(netdev);

	ipa_endpoint_disable_one(ipa->name_map[IPA_ENDPOINT_AP_MODEM_RX]);
	ipa_endpoint_disable_one(ipa->name_map[IPA_ENDPOINT_AP_MODEM_TX]);

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
static int ipa_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct net_device_stats *stats = &netdev->stats;
	struct ipa_priv *priv = netdev_priv(netdev);
	struct ipa_endpoint *endpoint;
	struct ipa *ipa = priv->ipa;
	u32 skb_len = skb->len;
	int ret;

	if (!skb_len)
		goto err_drop_skb;

	endpoint = ipa->name_map[IPA_ENDPOINT_AP_MODEM_TX];
	if (endpoint->data->qmap && skb->protocol != htons(ETH_P_MAP))
		goto err_drop_skb;

	ret = ipa_endpoint_skb_tx(endpoint, skb);
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
	ether_setup(netdev);
	/* No header ops (override value set by ether_setup()) */
	netdev->header_ops = NULL;
	netdev->type = ARPHRD_RAWIP;
	netdev->hard_header_len = 0;
	netdev->max_mtu = IPA_MTU;
	netdev->mtu = netdev->max_mtu;
	netdev->addr_len = 0;
	netdev->flags &= ~(IFF_BROADCAST | IFF_MULTICAST);
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

	netif_stop_queue(netdev);

	ipa_endpoint_suspend_one(ipa->name_map[IPA_ENDPOINT_AP_MODEM_RX]);
	ipa_endpoint_suspend_one(ipa->name_map[IPA_ENDPOINT_AP_MODEM_TX]);
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

	ipa_endpoint_resume_one(ipa->name_map[IPA_ENDPOINT_AP_MODEM_TX]);
	ipa_endpoint_resume_one(ipa->name_map[IPA_ENDPOINT_AP_MODEM_RX]);

	netif_wake_queue(netdev);
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

	ret = register_netdev(netdev);
	if (!ret) {
		ipa->modem_netdev = netdev;
		ipa->name_map[IPA_ENDPOINT_AP_MODEM_TX]->netdev = netdev;
		ipa->name_map[IPA_ENDPOINT_AP_MODEM_RX]->netdev = netdev;
	} else {
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

	/* Prevent the modem from triggering a call to ipa_setup() */
	ipa_smp2p_disable(ipa);

	/* Stop the queue and disable the endpoints if it's open */
	if (netdev) {
		(void)ipa_stop(netdev);
		ipa->name_map[IPA_ENDPOINT_AP_MODEM_RX]->netdev = NULL;
		ipa->name_map[IPA_ENDPOINT_AP_MODEM_TX]->netdev = NULL;
		ipa->modem_netdev = NULL;
		unregister_netdev(netdev);
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

int ipa_modem_init(struct ipa *ipa, bool modem_init)
{
	return ipa_smp2p_init(ipa, modem_init);
}

void ipa_modem_exit(struct ipa *ipa)
{
	ipa_smp2p_exit(ipa);
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

int ipa_modem_setup(struct ipa *ipa)
{
	return ipa_qmi_setup(ipa);
}

void ipa_modem_teardown(struct ipa *ipa)
{
	ipa_qmi_teardown(ipa);
}
