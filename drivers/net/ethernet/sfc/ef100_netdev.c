// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2018 Solarflare Communications Inc.
 * Copyright 2019-2020 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */
#include "net_driver.h"
#include "mcdi_port_common.h"
#include "mcdi_functions.h"
#include "efx_common.h"
#include "efx_channels.h"
#include "tx_common.h"
#include "ef100_netdev.h"
#include "ef100_ethtool.h"
#include "efx_common.h"
#include "nic_common.h"
#include "ef100_nic.h"
#include "ef100_tx.h"
#include "ef100_regs.h"
#include "mcdi_filters.h"
#include "rx_common.h"

static void ef100_update_name(struct efx_nic *efx)
{
	strcpy(efx->name, efx->net_dev->name);
}

/* Initiate a packet transmission.  We use one channel per CPU
 * (sharing when we have more CPUs than channels).
 *
 * Context: non-blocking.
 * Note that returning anything other than NETDEV_TX_OK will cause the
 * OS to free the skb.
 */
static netdev_tx_t ef100_hard_start_xmit(struct sk_buff *skb,
					 struct net_device *net_dev)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	struct efx_tx_queue *tx_queue;
	struct efx_channel *channel;
	int rc;

	channel = efx_get_tx_channel(efx, skb_get_queue_mapping(skb));
	netif_vdbg(efx, tx_queued, efx->net_dev,
		   "%s len %d data %d channel %d\n", __func__,
		   skb->len, skb->data_len, channel->channel);
	if (!efx->n_channels || !efx->n_tx_channels || !channel) {
		netif_stop_queue(net_dev);
		goto err;
	}

	tx_queue = &channel->tx_queue[0];
	rc = ef100_enqueue_skb(tx_queue, skb);
	if (rc == 0)
		return NETDEV_TX_OK;

err:
	net_dev->stats.tx_dropped++;
	return NETDEV_TX_OK;
}

static const struct net_device_ops ef100_netdev_ops = {
	.ndo_start_xmit         = ef100_hard_start_xmit,
};

/*	Netdev registration
 */
int ef100_netdev_event(struct notifier_block *this,
		       unsigned long event, void *ptr)
{
	struct efx_nic *efx = container_of(this, struct efx_nic, netdev_notifier);
	struct net_device *net_dev = netdev_notifier_info_to_dev(ptr);

	if (netdev_priv(net_dev) == efx && event == NETDEV_CHANGENAME)
		ef100_update_name(efx);

	return NOTIFY_DONE;
}

int ef100_register_netdev(struct efx_nic *efx)
{
	struct net_device *net_dev = efx->net_dev;
	int rc;

	net_dev->watchdog_timeo = 5 * HZ;
	net_dev->irq = efx->pci_dev->irq;
	net_dev->netdev_ops = &ef100_netdev_ops;
	net_dev->min_mtu = EFX_MIN_MTU;
	net_dev->max_mtu = EFX_MAX_MTU;
	net_dev->ethtool_ops = &ef100_ethtool_ops;

	rtnl_lock();

	rc = dev_alloc_name(net_dev, net_dev->name);
	if (rc < 0)
		goto fail_locked;
	ef100_update_name(efx);

	rc = register_netdevice(net_dev);
	if (rc)
		goto fail_locked;

	/* Always start with carrier off; PHY events will detect the link */
	netif_carrier_off(net_dev);

	efx->state = STATE_READY;
	rtnl_unlock();
	efx_init_mcdi_logging(efx);

	return 0;

fail_locked:
	rtnl_unlock();
	netif_err(efx, drv, efx->net_dev, "could not register net dev\n");
	return rc;
}

void ef100_unregister_netdev(struct efx_nic *efx)
{
	if (efx_dev_registered(efx)) {
		efx_fini_mcdi_logging(efx);
		efx->state = STATE_UNINIT;
		unregister_netdev(efx->net_dev);
	}
}
