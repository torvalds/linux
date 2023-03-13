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

static int ef100_alloc_vis(struct efx_nic *efx, unsigned int *allocated_vis)
{
	/* EF100 uses a single TXQ per channel, as all checksum offloading
	 * is configured in the TX descriptor, and there is no TX Pacer for
	 * HIGHPRI queues.
	 */
	unsigned int tx_vis = efx->n_tx_channels + efx->n_extra_tx_channels;
	unsigned int rx_vis = efx->n_rx_channels;
	unsigned int min_vis, max_vis;

	EFX_WARN_ON_PARANOID(efx->tx_queues_per_channel != 1);

	tx_vis += efx->n_xdp_channels * efx->xdp_tx_per_channel;

	max_vis = max(rx_vis, tx_vis);
	/* Currently don't handle resource starvation and only accept
	 * our maximum needs and no less.
	 */
	min_vis = max_vis;

	return efx_mcdi_alloc_vis(efx, min_vis, max_vis,
				  NULL, allocated_vis);
}

static int ef100_remap_bar(struct efx_nic *efx, int max_vis)
{
	unsigned int uc_mem_map_size;
	void __iomem *membase;

	efx->max_vis = max_vis;
	uc_mem_map_size = PAGE_ALIGN(max_vis * efx->vi_stride);

	/* Extend the original UC mapping of the memory BAR */
	membase = ioremap(efx->membase_phys, uc_mem_map_size);
	if (!membase) {
		netif_err(efx, probe, efx->net_dev,
			  "could not extend memory BAR to %x\n",
			  uc_mem_map_size);
		return -ENOMEM;
	}
	iounmap(efx->membase);
	efx->membase = membase;
	return 0;
}

/* Context: process, rtnl_lock() held.
 * Note that the kernel will ignore our return code; this method
 * should really be a void.
 */
static int ef100_net_stop(struct net_device *net_dev)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	netif_dbg(efx, ifdown, efx->net_dev, "closing on CPU %d\n",
		  raw_smp_processor_id());

	netif_stop_queue(net_dev);
	efx_stop_all(efx);
	efx_mcdi_mac_fini_stats(efx);
	efx_disable_interrupts(efx);
	efx_clear_interrupt_affinity(efx);
	efx_nic_fini_interrupt(efx);
	efx_remove_filters(efx);
	efx_fini_napi(efx);
	efx_remove_channels(efx);
	efx_mcdi_free_vis(efx);
	efx_remove_interrupts(efx);

	return 0;
}

/* Context: process, rtnl_lock() held. */
static int ef100_net_open(struct net_device *net_dev)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	unsigned int allocated_vis;
	int rc;

	ef100_update_name(efx);
	netif_dbg(efx, ifup, net_dev, "opening device on CPU %d\n",
		  raw_smp_processor_id());

	rc = efx_check_disabled(efx);
	if (rc)
		goto fail;

	rc = efx_probe_interrupts(efx);
	if (rc)
		goto fail;

	rc = efx_set_channels(efx);
	if (rc)
		goto fail;

	rc = efx_mcdi_free_vis(efx);
	if (rc)
		goto fail;

	rc = ef100_alloc_vis(efx, &allocated_vis);
	if (rc)
		goto fail;

	rc = efx_probe_channels(efx);
	if (rc)
		return rc;

	rc = ef100_remap_bar(efx, allocated_vis);
	if (rc)
		goto fail;

	efx_init_napi(efx);

	rc = efx_probe_filters(efx);
	if (rc)
		goto fail;

	rc = efx_nic_init_interrupt(efx);
	if (rc)
		goto fail;
	efx_set_interrupt_affinity(efx);

	rc = efx_enable_interrupts(efx);
	if (rc)
		goto fail;

	/* in case the MC rebooted while we were stopped, consume the change
	 * to the warm reboot count
	 */
	(void) efx_mcdi_poll_reboot(efx);

	rc = efx_mcdi_mac_init_stats(efx);
	if (rc)
		goto fail;

	efx_start_all(efx);

	/* Link state detection is normally event-driven; we have
	 * to poll now because we could have missed a change
	 */
	mutex_lock(&efx->mac_lock);
	if (efx_mcdi_phy_poll(efx))
		efx_link_status_changed(efx);
	mutex_unlock(&efx->mac_lock);

	return 0;

fail:
	ef100_net_stop(net_dev);
	return rc;
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
		dev_kfree_skb_any(skb);
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
	.ndo_open               = ef100_net_open,
	.ndo_stop               = ef100_net_stop,
	.ndo_start_xmit         = ef100_hard_start_xmit,
	.ndo_tx_timeout         = efx_watchdog,
	.ndo_get_stats64        = efx_net_stats,
	.ndo_change_mtu         = efx_change_mtu,
	.ndo_validate_addr      = eth_validate_addr,
	.ndo_set_mac_address    = efx_set_mac_address,
	.ndo_set_rx_mode        = efx_set_rx_mode, /* Lookout */
	.ndo_set_features       = efx_set_features,
	.ndo_get_phys_port_id   = efx_get_phys_port_id,
	.ndo_get_phys_port_name = efx_get_phys_port_name,
#ifdef CONFIG_RFS_ACCEL
	.ndo_rx_flow_steer      = efx_filter_rfs,
#endif
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
