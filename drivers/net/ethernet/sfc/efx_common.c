// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2018 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "net_driver.h"
#include <linux/module.h>
#include <linux/netdevice.h>
#include "efx_common.h"
#include "efx_channels.h"
#include "efx.h"
#include "mcdi.h"
#include "selftest.h"
#include "rx_common.h"
#include "tx_common.h"
#include "nic.h"
#include "io.h"
#include "mcdi_pcol.h"

/* Reset workqueue. If any NIC has a hardware failure then a reset will be
 * queued onto this work queue. This is not a per-nic work queue, because
 * efx_reset_work() acquires the rtnl lock, so resets are naturally serialised.
 */
static struct workqueue_struct *reset_workqueue;

int efx_create_reset_workqueue(void)
{
	reset_workqueue = create_singlethread_workqueue("sfc_reset");
	if (!reset_workqueue) {
		printk(KERN_ERR "Failed to create reset workqueue\n");
		return -ENOMEM;
	}

	return 0;
}

void efx_queue_reset_work(struct efx_nic *efx)
{
	queue_work(reset_workqueue, &efx->reset_work);
}

void efx_flush_reset_workqueue(struct efx_nic *efx)
{
	cancel_work_sync(&efx->reset_work);
}

void efx_destroy_reset_workqueue(void)
{
	if (reset_workqueue) {
		destroy_workqueue(reset_workqueue);
		reset_workqueue = NULL;
	}
}

/* We assume that efx->type->reconfigure_mac will always try to sync RX
 * filters and therefore needs to read-lock the filter table against freeing
 */
void efx_mac_reconfigure(struct efx_nic *efx)
{
	down_read(&efx->filter_sem);
	efx->type->reconfigure_mac(efx);
	up_read(&efx->filter_sem);
}

/* This ensures that the kernel is kept informed (via
 * netif_carrier_on/off) of the link status, and also maintains the
 * link status's stop on the port's TX queue.
 */
void efx_link_status_changed(struct efx_nic *efx)
{
	struct efx_link_state *link_state = &efx->link_state;

	/* SFC Bug 5356: A net_dev notifier is registered, so we must ensure
	 * that no events are triggered between unregister_netdev() and the
	 * driver unloading. A more general condition is that NETDEV_CHANGE
	 * can only be generated between NETDEV_UP and NETDEV_DOWN
	 */
	if (!netif_running(efx->net_dev))
		return;

	if (link_state->up != netif_carrier_ok(efx->net_dev)) {
		efx->n_link_state_changes++;

		if (link_state->up)
			netif_carrier_on(efx->net_dev);
		else
			netif_carrier_off(efx->net_dev);
	}

	/* Status message for kernel log */
	if (link_state->up)
		netif_info(efx, link, efx->net_dev,
			   "link up at %uMbps %s-duplex (MTU %d)\n",
			   link_state->speed, link_state->fd ? "full" : "half",
			   efx->net_dev->mtu);
	else
		netif_info(efx, link, efx->net_dev, "link down\n");
}

/**************************************************************************
 *
 * Event queue processing
 *
 *************************************************************************/

void efx_start_channels(struct efx_nic *efx)
{
	struct efx_tx_queue *tx_queue;
	struct efx_rx_queue *rx_queue;
	struct efx_channel *channel;

	efx_for_each_channel(channel, efx) {
		efx_for_each_channel_tx_queue(tx_queue, channel) {
			efx_init_tx_queue(tx_queue);
			atomic_inc(&efx->active_queues);
		}

		efx_for_each_channel_rx_queue(rx_queue, channel) {
			efx_init_rx_queue(rx_queue);
			atomic_inc(&efx->active_queues);
			efx_stop_eventq(channel);
			efx_fast_push_rx_descriptors(rx_queue, false);
			efx_start_eventq(channel);
		}

		WARN_ON(channel->rx_pkt_n_frags);
	}
}

/* Channels are shutdown and reinitialised whilst the NIC is running
 * to propagate configuration changes (mtu, checksum offload), or
 * to clear hardware error conditions
 */
static void efx_start_datapath(struct efx_nic *efx)
{
	netdev_features_t old_features = efx->net_dev->features;
	bool old_rx_scatter = efx->rx_scatter;
	size_t rx_buf_len;

	/* Calculate the rx buffer allocation parameters required to
	 * support the current MTU, including padding for header
	 * alignment and overruns.
	 */
	efx->rx_dma_len = (efx->rx_prefix_size +
			   EFX_MAX_FRAME_LEN(efx->net_dev->mtu) +
			   efx->type->rx_buffer_padding);
	rx_buf_len = (sizeof(struct efx_rx_page_state) + XDP_PACKET_HEADROOM +
		      efx->rx_ip_align + efx->rx_dma_len);
	if (rx_buf_len <= PAGE_SIZE) {
		efx->rx_scatter = efx->type->always_rx_scatter;
		efx->rx_buffer_order = 0;
	} else if (efx->type->can_rx_scatter) {
		BUILD_BUG_ON(EFX_RX_USR_BUF_SIZE % L1_CACHE_BYTES);
		BUILD_BUG_ON(sizeof(struct efx_rx_page_state) +
			     2 * ALIGN(NET_IP_ALIGN + EFX_RX_USR_BUF_SIZE,
				       EFX_RX_BUF_ALIGNMENT) >
			     PAGE_SIZE);
		efx->rx_scatter = true;
		efx->rx_dma_len = EFX_RX_USR_BUF_SIZE;
		efx->rx_buffer_order = 0;
	} else {
		efx->rx_scatter = false;
		efx->rx_buffer_order = get_order(rx_buf_len);
	}

	efx_rx_config_page_split(efx);
	if (efx->rx_buffer_order)
		netif_dbg(efx, drv, efx->net_dev,
			  "RX buf len=%u; page order=%u batch=%u\n",
			  efx->rx_dma_len, efx->rx_buffer_order,
			  efx->rx_pages_per_batch);
	else
		netif_dbg(efx, drv, efx->net_dev,
			  "RX buf len=%u step=%u bpp=%u; page batch=%u\n",
			  efx->rx_dma_len, efx->rx_page_buf_step,
			  efx->rx_bufs_per_page, efx->rx_pages_per_batch);

	/* Restore previously fixed features in hw_features and remove
	 * features which are fixed now
	 */
	efx->net_dev->hw_features |= efx->net_dev->features;
	efx->net_dev->hw_features &= ~efx->fixed_features;
	efx->net_dev->features |= efx->fixed_features;
	if (efx->net_dev->features != old_features)
		netdev_features_change(efx->net_dev);

	/* RX filters may also have scatter-enabled flags */
	if (efx->rx_scatter != old_rx_scatter)
		efx->type->filter_update_rx_scatter(efx);

	/* We must keep at least one descriptor in a TX ring empty.
	 * We could avoid this when the queue size does not exactly
	 * match the hardware ring size, but it's not that important.
	 * Therefore we stop the queue when one more skb might fill
	 * the ring completely.  We wake it when half way back to
	 * empty.
	 */
	efx->txq_stop_thresh = efx->txq_entries - efx_tx_max_skb_descs(efx);
	efx->txq_wake_thresh = efx->txq_stop_thresh / 2;

	/* Initialise the channels */
	efx_start_channels(efx);

	efx_ptp_start_datapath(efx);

	if (netif_device_present(efx->net_dev))
		netif_tx_wake_all_queues(efx->net_dev);
}

void efx_stop_channels(struct efx_nic *efx)
{
	struct efx_tx_queue *tx_queue;
	struct efx_rx_queue *rx_queue;
	struct efx_channel *channel;
	int rc = 0;

	/* Stop RX refill */
	efx_for_each_channel(channel, efx) {
		efx_for_each_channel_rx_queue(rx_queue, channel)
			rx_queue->refill_enabled = false;
	}

	efx_for_each_channel(channel, efx) {
		/* RX packet processing is pipelined, so wait for the
		 * NAPI handler to complete.  At least event queue 0
		 * might be kept active by non-data events, so don't
		 * use napi_synchronize() but actually disable NAPI
		 * temporarily.
		 */
		if (efx_channel_has_rx_queue(channel)) {
			efx_stop_eventq(channel);
			efx_start_eventq(channel);
		}
	}

	if (efx->type->fini_dmaq)
		rc = efx->type->fini_dmaq(efx);

	if (rc) {
		netif_err(efx, drv, efx->net_dev, "failed to flush queues\n");
	} else {
		netif_dbg(efx, drv, efx->net_dev,
			  "successfully flushed all queues\n");
	}

	efx_for_each_channel(channel, efx) {
		efx_for_each_channel_rx_queue(rx_queue, channel)
			efx_fini_rx_queue(rx_queue);
		efx_for_each_possible_channel_tx_queue(tx_queue, channel)
			efx_fini_tx_queue(tx_queue);
	}
	efx->xdp_rxq_info_failed = false;
}

static void efx_stop_datapath(struct efx_nic *efx)
{
	EFX_ASSERT_RESET_SERIALISED(efx);
	BUG_ON(efx->port_enabled);

	efx_ptp_stop_datapath(efx);

	efx_stop_channels(efx);
}

/**************************************************************************
 *
 * Port handling
 *
 **************************************************************************/

static void efx_start_port(struct efx_nic *efx)
{
	netif_dbg(efx, ifup, efx->net_dev, "start port\n");
	BUG_ON(efx->port_enabled);

	mutex_lock(&efx->mac_lock);
	efx->port_enabled = true;

	/* Ensure MAC ingress/egress is enabled */
	efx_mac_reconfigure(efx);

	mutex_unlock(&efx->mac_lock);
}

/* Cancel work for MAC reconfiguration, periodic hardware monitoring
 * and the async self-test, wait for them to finish and prevent them
 * being scheduled again.  This doesn't cover online resets, which
 * should only be cancelled when removing the device.
 */
static void efx_stop_port(struct efx_nic *efx)
{
	netif_dbg(efx, ifdown, efx->net_dev, "stop port\n");

	EFX_ASSERT_RESET_SERIALISED(efx);

	mutex_lock(&efx->mac_lock);
	efx->port_enabled = false;
	mutex_unlock(&efx->mac_lock);

	/* Serialise against efx_set_multicast_list() */
	netif_addr_lock_bh(efx->net_dev);
	netif_addr_unlock_bh(efx->net_dev);

	cancel_delayed_work_sync(&efx->monitor_work);
	efx_selftest_async_cancel(efx);
	cancel_work_sync(&efx->mac_work);
}

/* If the interface is supposed to be running but is not, start
 * the hardware and software data path, regular activity for the port
 * (MAC statistics, link polling, etc.) and schedule the port to be
 * reconfigured.  Interrupts must already be enabled.  This function
 * is safe to call multiple times, so long as the NIC is not disabled.
 * Requires the RTNL lock.
 */
void efx_start_all(struct efx_nic *efx)
{
	EFX_ASSERT_RESET_SERIALISED(efx);
	BUG_ON(efx->state == STATE_DISABLED);

	/* Check that it is appropriate to restart the interface. All
	 * of these flags are safe to read under just the rtnl lock
	 */
	if (efx->port_enabled || !netif_running(efx->net_dev) ||
	    efx->reset_pending)
		return;

	efx_start_port(efx);
	efx_start_datapath(efx);

	/* Start the hardware monitor if there is one */
	efx_start_monitor(efx);

	/* Link state detection is normally event-driven; we have
	 * to poll now because we could have missed a change
	 */
	mutex_lock(&efx->mac_lock);
	if (efx->phy_op->poll(efx))
		efx_link_status_changed(efx);
	mutex_unlock(&efx->mac_lock);

	efx->type->start_stats(efx);
	efx->type->pull_stats(efx);
	spin_lock_bh(&efx->stats_lock);
	efx->type->update_stats(efx, NULL, NULL);
	spin_unlock_bh(&efx->stats_lock);
}

/* Quiesce the hardware and software data path, and regular activity
 * for the port without bringing the link down.  Safe to call multiple
 * times with the NIC in almost any state, but interrupts should be
 * enabled.  Requires the RTNL lock.
 */
void efx_stop_all(struct efx_nic *efx)
{
	EFX_ASSERT_RESET_SERIALISED(efx);

	/* port_enabled can be read safely under the rtnl lock */
	if (!efx->port_enabled)
		return;

	/* update stats before we go down so we can accurately count
	 * rx_nodesc_drops
	 */
	efx->type->pull_stats(efx);
	spin_lock_bh(&efx->stats_lock);
	efx->type->update_stats(efx, NULL, NULL);
	spin_unlock_bh(&efx->stats_lock);
	efx->type->stop_stats(efx);
	efx_stop_port(efx);

	/* Stop the kernel transmit interface.  This is only valid if
	 * the device is stopped or detached; otherwise the watchdog
	 * may fire immediately.
	 */
	WARN_ON(netif_running(efx->net_dev) &&
		netif_device_present(efx->net_dev));
	netif_tx_disable(efx->net_dev);

	efx_stop_datapath(efx);
}

/* Push loopback/power/transmit disable settings to the PHY, and reconfigure
 * the MAC appropriately. All other PHY configuration changes are pushed
 * through phy_op->set_settings(), and pushed asynchronously to the MAC
 * through efx_monitor().
 *
 * Callers must hold the mac_lock
 */
int __efx_reconfigure_port(struct efx_nic *efx)
{
	enum efx_phy_mode phy_mode;
	int rc;

	WARN_ON(!mutex_is_locked(&efx->mac_lock));

	/* Disable PHY transmit in mac level loopbacks */
	phy_mode = efx->phy_mode;
	if (LOOPBACK_INTERNAL(efx))
		efx->phy_mode |= PHY_MODE_TX_DISABLED;
	else
		efx->phy_mode &= ~PHY_MODE_TX_DISABLED;

	rc = efx->type->reconfigure_port(efx);

	if (rc)
		efx->phy_mode = phy_mode;

	return rc;
}

/* Reinitialise the MAC to pick up new PHY settings, even if the port is
 * disabled.
 */
int efx_reconfigure_port(struct efx_nic *efx)
{
	int rc;

	EFX_ASSERT_RESET_SERIALISED(efx);

	mutex_lock(&efx->mac_lock);
	rc = __efx_reconfigure_port(efx);
	mutex_unlock(&efx->mac_lock);

	return rc;
}
