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
#include <linux/filter.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <net/gre.h>
#include "efx_common.h"
#include "efx_channels.h"
#include "efx.h"
#include "mcdi.h"
#include "selftest.h"
#include "rx_common.h"
#include "tx_common.h"
#include "nic.h"
#include "mcdi_port_common.h"
#include "io.h"
#include "mcdi_pcol.h"

static unsigned int debug = (NETIF_MSG_DRV | NETIF_MSG_PROBE |
			     NETIF_MSG_LINK | NETIF_MSG_IFDOWN |
			     NETIF_MSG_IFUP | NETIF_MSG_RX_ERR |
			     NETIF_MSG_TX_ERR | NETIF_MSG_HW);
module_param(debug, uint, 0);
MODULE_PARM_DESC(debug, "Bitmapped debugging message enable value");

/* This is the time (in jiffies) between invocations of the hardware
 * monitor.
 * On Falcon-based NICs, this will:
 * - Check the on-board hardware monitor;
 * - Poll the link state and reconfigure the hardware as necessary.
 * On Siena-based NICs for power systems with EEH support, this will give EEH a
 * chance to start.
 */
static unsigned int efx_monitor_interval = 1 * HZ;

/* How often and how many times to poll for a reset while waiting for a
 * BIST that another function started to complete.
 */
#define BIST_WAIT_DELAY_MS	100
#define BIST_WAIT_DELAY_COUNT	100

/* Default stats update time */
#define STATS_PERIOD_MS_DEFAULT 1000

static const unsigned int efx_reset_type_max = RESET_TYPE_MAX;
static const char *const efx_reset_type_names[] = {
	[RESET_TYPE_INVISIBLE]          = "INVISIBLE",
	[RESET_TYPE_ALL]                = "ALL",
	[RESET_TYPE_RECOVER_OR_ALL]     = "RECOVER_OR_ALL",
	[RESET_TYPE_WORLD]              = "WORLD",
	[RESET_TYPE_RECOVER_OR_DISABLE] = "RECOVER_OR_DISABLE",
	[RESET_TYPE_DATAPATH]           = "DATAPATH",
	[RESET_TYPE_MC_BIST]		= "MC_BIST",
	[RESET_TYPE_DISABLE]            = "DISABLE",
	[RESET_TYPE_TX_WATCHDOG]        = "TX_WATCHDOG",
	[RESET_TYPE_INT_ERROR]          = "INT_ERROR",
	[RESET_TYPE_DMA_ERROR]          = "DMA_ERROR",
	[RESET_TYPE_TX_SKIP]            = "TX_SKIP",
	[RESET_TYPE_MC_FAILURE]         = "MC_FAILURE",
	[RESET_TYPE_MCDI_TIMEOUT]	= "MCDI_TIMEOUT (FLR)",
};

#define RESET_TYPE(type) \
	STRING_TABLE_LOOKUP(type, efx_reset_type)

/* Loopback mode names (see LOOPBACK_MODE()) */
const unsigned int efx_siena_loopback_mode_max = LOOPBACK_MAX;
const char *const efx_siena_loopback_mode_names[] = {
	[LOOPBACK_NONE]		= "NONE",
	[LOOPBACK_DATA]		= "DATAPATH",
	[LOOPBACK_GMAC]		= "GMAC",
	[LOOPBACK_XGMII]	= "XGMII",
	[LOOPBACK_XGXS]		= "XGXS",
	[LOOPBACK_XAUI]		= "XAUI",
	[LOOPBACK_GMII]		= "GMII",
	[LOOPBACK_SGMII]	= "SGMII",
	[LOOPBACK_XGBR]		= "XGBR",
	[LOOPBACK_XFI]		= "XFI",
	[LOOPBACK_XAUI_FAR]	= "XAUI_FAR",
	[LOOPBACK_GMII_FAR]	= "GMII_FAR",
	[LOOPBACK_SGMII_FAR]	= "SGMII_FAR",
	[LOOPBACK_XFI_FAR]	= "XFI_FAR",
	[LOOPBACK_GPHY]		= "GPHY",
	[LOOPBACK_PHYXS]	= "PHYXS",
	[LOOPBACK_PCS]		= "PCS",
	[LOOPBACK_PMAPMD]	= "PMA/PMD",
	[LOOPBACK_XPORT]	= "XPORT",
	[LOOPBACK_XGMII_WS]	= "XGMII_WS",
	[LOOPBACK_XAUI_WS]	= "XAUI_WS",
	[LOOPBACK_XAUI_WS_FAR]  = "XAUI_WS_FAR",
	[LOOPBACK_XAUI_WS_NEAR] = "XAUI_WS_NEAR",
	[LOOPBACK_GMII_WS]	= "GMII_WS",
	[LOOPBACK_XFI_WS]	= "XFI_WS",
	[LOOPBACK_XFI_WS_FAR]	= "XFI_WS_FAR",
	[LOOPBACK_PHYXS_WS]	= "PHYXS_WS",
};

/* Reset workqueue. If any NIC has a hardware failure then a reset will be
 * queued onto this work queue. This is not a per-nic work queue, because
 * efx_reset_work() acquires the rtnl lock, so resets are naturally serialised.
 */
static struct workqueue_struct *reset_workqueue;

int efx_siena_create_reset_workqueue(void)
{
	reset_workqueue = create_singlethread_workqueue("sfc_siena_reset");
	if (!reset_workqueue) {
		printk(KERN_ERR "Failed to create reset workqueue\n");
		return -ENOMEM;
	}

	return 0;
}

void efx_siena_queue_reset_work(struct efx_nic *efx)
{
	queue_work(reset_workqueue, &efx->reset_work);
}

void efx_siena_flush_reset_workqueue(struct efx_nic *efx)
{
	cancel_work_sync(&efx->reset_work);
}

void efx_siena_destroy_reset_workqueue(void)
{
	if (reset_workqueue) {
		destroy_workqueue(reset_workqueue);
		reset_workqueue = NULL;
	}
}

/* We assume that efx->type->reconfigure_mac will always try to sync RX
 * filters and therefore needs to read-lock the filter table against freeing
 */
void efx_siena_mac_reconfigure(struct efx_nic *efx, bool mtu_only)
{
	if (efx->type->reconfigure_mac) {
		down_read(&efx->filter_sem);
		efx->type->reconfigure_mac(efx, mtu_only);
		up_read(&efx->filter_sem);
	}
}

/* Asynchronous work item for changing MAC promiscuity and multicast
 * hash.  Avoid a drain/rx_ingress enable by reconfiguring the current
 * MAC directly.
 */
static void efx_mac_work(struct work_struct *data)
{
	struct efx_nic *efx = container_of(data, struct efx_nic, mac_work);

	mutex_lock(&efx->mac_lock);
	if (efx->port_enabled)
		efx_siena_mac_reconfigure(efx, false);
	mutex_unlock(&efx->mac_lock);
}

int efx_siena_set_mac_address(struct net_device *net_dev, void *data)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	struct sockaddr *addr = data;
	u8 *new_addr = addr->sa_data;
	u8 old_addr[6];
	int rc;

	if (!is_valid_ether_addr(new_addr)) {
		netif_err(efx, drv, efx->net_dev,
			  "invalid ethernet MAC address requested: %pM\n",
			  new_addr);
		return -EADDRNOTAVAIL;
	}

	/* save old address */
	ether_addr_copy(old_addr, net_dev->dev_addr);
	eth_hw_addr_set(net_dev, new_addr);
	if (efx->type->set_mac_address) {
		rc = efx->type->set_mac_address(efx);
		if (rc) {
			eth_hw_addr_set(net_dev, old_addr);
			return rc;
		}
	}

	/* Reconfigure the MAC */
	mutex_lock(&efx->mac_lock);
	efx_siena_mac_reconfigure(efx, false);
	mutex_unlock(&efx->mac_lock);

	return 0;
}

/* Context: netif_addr_lock held, BHs disabled. */
void efx_siena_set_rx_mode(struct net_device *net_dev)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	if (efx->port_enabled)
		queue_work(efx->workqueue, &efx->mac_work);
	/* Otherwise efx_start_port() will do this */
}

int efx_siena_set_features(struct net_device *net_dev, netdev_features_t data)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	int rc;

	/* If disabling RX n-tuple filtering, clear existing filters */
	if (net_dev->features & ~data & NETIF_F_NTUPLE) {
		rc = efx->type->filter_clear_rx(efx, EFX_FILTER_PRI_MANUAL);
		if (rc)
			return rc;
	}

	/* If Rx VLAN filter is changed, update filters via mac_reconfigure.
	 * If rx-fcs is changed, mac_reconfigure updates that too.
	 */
	if ((net_dev->features ^ data) & (NETIF_F_HW_VLAN_CTAG_FILTER |
					  NETIF_F_RXFCS)) {
		/* efx_siena_set_rx_mode() will schedule MAC work to update filters
		 * when a new features are finally set in net_dev.
		 */
		efx_siena_set_rx_mode(net_dev);
	}

	return 0;
}

/* This ensures that the kernel is kept informed (via
 * netif_carrier_on/off) of the link status, and also maintains the
 * link status's stop on the port's TX queue.
 */
void efx_siena_link_status_changed(struct efx_nic *efx)
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

unsigned int efx_siena_xdp_max_mtu(struct efx_nic *efx)
{
	/* The maximum MTU that we can fit in a single page, allowing for
	 * framing, overhead and XDP headroom + tailroom.
	 */
	int overhead = EFX_MAX_FRAME_LEN(0) + sizeof(struct efx_rx_page_state) +
		       efx->rx_prefix_size + efx->type->rx_buffer_padding +
		       efx->rx_ip_align + EFX_XDP_HEADROOM + EFX_XDP_TAILROOM;

	return PAGE_SIZE - overhead;
}

/* Context: process, rtnl_lock() held. */
int efx_siena_change_mtu(struct net_device *net_dev, int new_mtu)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	int rc;

	rc = efx_check_disabled(efx);
	if (rc)
		return rc;

	if (rtnl_dereference(efx->xdp_prog) &&
	    new_mtu > efx_siena_xdp_max_mtu(efx)) {
		netif_err(efx, drv, efx->net_dev,
			  "Requested MTU of %d too big for XDP (max: %d)\n",
			  new_mtu, efx_siena_xdp_max_mtu(efx));
		return -EINVAL;
	}

	netif_dbg(efx, drv, efx->net_dev, "changing MTU to %d\n", new_mtu);

	efx_device_detach_sync(efx);
	efx_siena_stop_all(efx);

	mutex_lock(&efx->mac_lock);
	net_dev->mtu = new_mtu;
	efx_siena_mac_reconfigure(efx, true);
	mutex_unlock(&efx->mac_lock);

	efx_siena_start_all(efx);
	efx_device_attach_if_not_resetting(efx);
	return 0;
}

/**************************************************************************
 *
 * Hardware monitor
 *
 **************************************************************************/

/* Run periodically off the general workqueue */
static void efx_monitor(struct work_struct *data)
{
	struct efx_nic *efx = container_of(data, struct efx_nic,
					   monitor_work.work);

	netif_vdbg(efx, timer, efx->net_dev,
		   "hardware monitor executing on CPU %d\n",
		   raw_smp_processor_id());
	BUG_ON(efx->type->monitor == NULL);

	/* If the mac_lock is already held then it is likely a port
	 * reconfiguration is already in place, which will likely do
	 * most of the work of monitor() anyway.
	 */
	if (mutex_trylock(&efx->mac_lock)) {
		if (efx->port_enabled && efx->type->monitor)
			efx->type->monitor(efx);
		mutex_unlock(&efx->mac_lock);
	}

	efx_siena_start_monitor(efx);
}

void efx_siena_start_monitor(struct efx_nic *efx)
{
	if (efx->type->monitor)
		queue_delayed_work(efx->workqueue, &efx->monitor_work,
				   efx_monitor_interval);
}

/**************************************************************************
 *
 * Event queue processing
 *
 *************************************************************************/

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
	rx_buf_len = (sizeof(struct efx_rx_page_state)   + EFX_XDP_HEADROOM +
		      efx->rx_ip_align + efx->rx_dma_len + EFX_XDP_TAILROOM);

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

	efx_siena_rx_config_page_split(efx);
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
	if ((efx->rx_scatter != old_rx_scatter) &&
	    efx->type->filter_update_rx_scatter)
		efx->type->filter_update_rx_scatter(efx);

	/* We must keep at least one descriptor in a TX ring empty.
	 * We could avoid this when the queue size does not exactly
	 * match the hardware ring size, but it's not that important.
	 * Therefore we stop the queue when one more skb might fill
	 * the ring completely.  We wake it when half way back to
	 * empty.
	 */
	efx->txq_stop_thresh = efx->txq_entries - efx_siena_tx_max_skb_descs(efx);
	efx->txq_wake_thresh = efx->txq_stop_thresh / 2;

	/* Initialise the channels */
	efx_siena_start_channels(efx);

	efx_siena_ptp_start_datapath(efx);

	if (netif_device_present(efx->net_dev))
		netif_tx_wake_all_queues(efx->net_dev);
}

static void efx_stop_datapath(struct efx_nic *efx)
{
	EFX_ASSERT_RESET_SERIALISED(efx);
	BUG_ON(efx->port_enabled);

	efx_siena_ptp_stop_datapath(efx);

	efx_siena_stop_channels(efx);
}

/**************************************************************************
 *
 * Port handling
 *
 **************************************************************************/

/* Equivalent to efx_siena_link_set_advertising with all-zeroes, except does not
 * force the Autoneg bit on.
 */
void efx_siena_link_clear_advertising(struct efx_nic *efx)
{
	bitmap_zero(efx->link_advertising, __ETHTOOL_LINK_MODE_MASK_NBITS);
	efx->wanted_fc &= ~(EFX_FC_TX | EFX_FC_RX);
}

void efx_siena_link_set_wanted_fc(struct efx_nic *efx, u8 wanted_fc)
{
	efx->wanted_fc = wanted_fc;
	if (efx->link_advertising[0]) {
		if (wanted_fc & EFX_FC_RX)
			efx->link_advertising[0] |= (ADVERTISED_Pause |
						     ADVERTISED_Asym_Pause);
		else
			efx->link_advertising[0] &= ~(ADVERTISED_Pause |
						      ADVERTISED_Asym_Pause);
		if (wanted_fc & EFX_FC_TX)
			efx->link_advertising[0] ^= ADVERTISED_Asym_Pause;
	}
}

static void efx_start_port(struct efx_nic *efx)
{
	netif_dbg(efx, ifup, efx->net_dev, "start port\n");
	BUG_ON(efx->port_enabled);

	mutex_lock(&efx->mac_lock);
	efx->port_enabled = true;

	/* Ensure MAC ingress/egress is enabled */
	efx_siena_mac_reconfigure(efx, false);

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
	efx_siena_selftest_async_cancel(efx);
	cancel_work_sync(&efx->mac_work);
}

/* If the interface is supposed to be running but is not, start
 * the hardware and software data path, regular activity for the port
 * (MAC statistics, link polling, etc.) and schedule the port to be
 * reconfigured.  Interrupts must already be enabled.  This function
 * is safe to call multiple times, so long as the NIC is not disabled.
 * Requires the RTNL lock.
 */
void efx_siena_start_all(struct efx_nic *efx)
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
	efx_siena_start_monitor(efx);

	/* Link state detection is normally event-driven; we have
	 * to poll now because we could have missed a change
	 */
	mutex_lock(&efx->mac_lock);
	if (efx_siena_mcdi_phy_poll(efx))
		efx_siena_link_status_changed(efx);
	mutex_unlock(&efx->mac_lock);

	if (efx->type->start_stats) {
		efx->type->start_stats(efx);
		efx->type->pull_stats(efx);
		spin_lock_bh(&efx->stats_lock);
		efx->type->update_stats(efx, NULL, NULL);
		spin_unlock_bh(&efx->stats_lock);
	}
}

/* Quiesce the hardware and software data path, and regular activity
 * for the port without bringing the link down.  Safe to call multiple
 * times with the NIC in almost any state, but interrupts should be
 * enabled.  Requires the RTNL lock.
 */
void efx_siena_stop_all(struct efx_nic *efx)
{
	EFX_ASSERT_RESET_SERIALISED(efx);

	/* port_enabled can be read safely under the rtnl lock */
	if (!efx->port_enabled)
		return;

	if (efx->type->update_stats) {
		/* update stats before we go down so we can accurately count
		 * rx_nodesc_drops
		 */
		efx->type->pull_stats(efx);
		spin_lock_bh(&efx->stats_lock);
		efx->type->update_stats(efx, NULL, NULL);
		spin_unlock_bh(&efx->stats_lock);
		efx->type->stop_stats(efx);
	}

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

static size_t efx_siena_update_stats_atomic(struct efx_nic *efx, u64 *full_stats,
					    struct rtnl_link_stats64 *core_stats)
{
	if (efx->type->update_stats_atomic)
		return efx->type->update_stats_atomic(efx, full_stats, core_stats);
	return efx->type->update_stats(efx, full_stats, core_stats);
}

/* Context: process, dev_base_lock or RTNL held, non-blocking. */
void efx_siena_net_stats(struct net_device *net_dev,
			 struct rtnl_link_stats64 *stats)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	spin_lock_bh(&efx->stats_lock);
	efx_siena_update_stats_atomic(efx, NULL, stats);
	spin_unlock_bh(&efx->stats_lock);
}

/* Push loopback/power/transmit disable settings to the PHY, and reconfigure
 * the MAC appropriately. All other PHY configuration changes are pushed
 * through phy_op->set_settings(), and pushed asynchronously to the MAC
 * through efx_monitor().
 *
 * Callers must hold the mac_lock
 */
int __efx_siena_reconfigure_port(struct efx_nic *efx)
{
	enum efx_phy_mode phy_mode;
	int rc = 0;

	WARN_ON(!mutex_is_locked(&efx->mac_lock));

	/* Disable PHY transmit in mac level loopbacks */
	phy_mode = efx->phy_mode;
	if (LOOPBACK_INTERNAL(efx))
		efx->phy_mode |= PHY_MODE_TX_DISABLED;
	else
		efx->phy_mode &= ~PHY_MODE_TX_DISABLED;

	if (efx->type->reconfigure_port)
		rc = efx->type->reconfigure_port(efx);

	if (rc)
		efx->phy_mode = phy_mode;

	return rc;
}

/* Reinitialise the MAC to pick up new PHY settings, even if the port is
 * disabled.
 */
int efx_siena_reconfigure_port(struct efx_nic *efx)
{
	int rc;

	EFX_ASSERT_RESET_SERIALISED(efx);

	mutex_lock(&efx->mac_lock);
	rc = __efx_siena_reconfigure_port(efx);
	mutex_unlock(&efx->mac_lock);

	return rc;
}

/**************************************************************************
 *
 * Device reset and suspend
 *
 **************************************************************************/

static void efx_wait_for_bist_end(struct efx_nic *efx)
{
	int i;

	for (i = 0; i < BIST_WAIT_DELAY_COUNT; ++i) {
		if (efx_siena_mcdi_poll_reboot(efx))
			goto out;
		msleep(BIST_WAIT_DELAY_MS);
	}

	netif_err(efx, drv, efx->net_dev, "Warning: No MC reboot after BIST mode\n");
out:
	/* Either way unset the BIST flag. If we found no reboot we probably
	 * won't recover, but we should try.
	 */
	efx->mc_bist_for_other_fn = false;
}

/* Try recovery mechanisms.
 * For now only EEH is supported.
 * Returns 0 if the recovery mechanisms are unsuccessful.
 * Returns a non-zero value otherwise.
 */
int efx_siena_try_recovery(struct efx_nic *efx)
{
#ifdef CONFIG_EEH
	/* A PCI error can occur and not be seen by EEH because nothing
	 * happens on the PCI bus. In this case the driver may fail and
	 * schedule a 'recover or reset', leading to this recovery handler.
	 * Manually call the eeh failure check function.
	 */
	struct eeh_dev *eehdev = pci_dev_to_eeh_dev(efx->pci_dev);
	if (eeh_dev_check_failure(eehdev)) {
		/* The EEH mechanisms will handle the error and reset the
		 * device if necessary.
		 */
		return 1;
	}
#endif
	return 0;
}

/* Tears down the entire software state and most of the hardware state
 * before reset.
 */
void efx_siena_reset_down(struct efx_nic *efx, enum reset_type method)
{
	EFX_ASSERT_RESET_SERIALISED(efx);

	if (method == RESET_TYPE_MCDI_TIMEOUT)
		efx->type->prepare_flr(efx);

	efx_siena_stop_all(efx);
	efx_siena_disable_interrupts(efx);

	mutex_lock(&efx->mac_lock);
	down_write(&efx->filter_sem);
	mutex_lock(&efx->rss_lock);
	efx->type->fini(efx);
}

/* Context: netif_tx_lock held, BHs disabled. */
void efx_siena_watchdog(struct net_device *net_dev, unsigned int txqueue)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	netif_err(efx, tx_err, efx->net_dev,
		  "TX stuck with port_enabled=%d: resetting channels\n",
		  efx->port_enabled);

	efx_siena_schedule_reset(efx, RESET_TYPE_TX_WATCHDOG);
}

/* This function will always ensure that the locks acquired in
 * efx_siena_reset_down() are released. A failure return code indicates
 * that we were unable to reinitialise the hardware, and the
 * driver should be disabled. If ok is false, then the rx and tx
 * engines are not restarted, pending a RESET_DISABLE.
 */
int efx_siena_reset_up(struct efx_nic *efx, enum reset_type method, bool ok)
{
	int rc;

	EFX_ASSERT_RESET_SERIALISED(efx);

	if (method == RESET_TYPE_MCDI_TIMEOUT)
		efx->type->finish_flr(efx);

	/* Ensure that SRAM is initialised even if we're disabling the device */
	rc = efx->type->init(efx);
	if (rc) {
		netif_err(efx, drv, efx->net_dev, "failed to initialise NIC\n");
		goto fail;
	}

	if (!ok)
		goto fail;

	if (efx->port_initialized && method != RESET_TYPE_INVISIBLE &&
	    method != RESET_TYPE_DATAPATH) {
		rc = efx_siena_mcdi_port_reconfigure(efx);
		if (rc && rc != -EPERM)
			netif_err(efx, drv, efx->net_dev,
				  "could not restore PHY settings\n");
	}

	rc = efx_siena_enable_interrupts(efx);
	if (rc)
		goto fail;

#ifdef CONFIG_SFC_SIENA_SRIOV
	rc = efx->type->vswitching_restore(efx);
	if (rc) /* not fatal; the PF will still work fine */
		netif_warn(efx, probe, efx->net_dev,
			   "failed to restore vswitching rc=%d;"
			   " VFs may not function\n", rc);
#endif

	if (efx->type->rx_restore_rss_contexts)
		efx->type->rx_restore_rss_contexts(efx);
	mutex_unlock(&efx->rss_lock);
	efx->type->filter_table_restore(efx);
	up_write(&efx->filter_sem);
	if (efx->type->sriov_reset)
		efx->type->sriov_reset(efx);

	mutex_unlock(&efx->mac_lock);

	efx_siena_start_all(efx);

	if (efx->type->udp_tnl_push_ports)
		efx->type->udp_tnl_push_ports(efx);

	return 0;

fail:
	efx->port_initialized = false;

	mutex_unlock(&efx->rss_lock);
	up_write(&efx->filter_sem);
	mutex_unlock(&efx->mac_lock);

	return rc;
}

/* Reset the NIC using the specified method.  Note that the reset may
 * fail, in which case the card will be left in an unusable state.
 *
 * Caller must hold the rtnl_lock.
 */
int efx_siena_reset(struct efx_nic *efx, enum reset_type method)
{
	int rc, rc2 = 0;
	bool disabled;

	netif_info(efx, drv, efx->net_dev, "resetting (%s)\n",
		   RESET_TYPE(method));

	efx_device_detach_sync(efx);
	/* efx_siena_reset_down() grabs locks that prevent recovery on EF100.
	 * EF100 reset is handled in the efx_nic_type callback below.
	 */
	if (efx_nic_rev(efx) != EFX_REV_EF100)
		efx_siena_reset_down(efx, method);

	rc = efx->type->reset(efx, method);
	if (rc) {
		netif_err(efx, drv, efx->net_dev, "failed to reset hardware\n");
		goto out;
	}

	/* Clear flags for the scopes we covered.  We assume the NIC and
	 * driver are now quiescent so that there is no race here.
	 */
	if (method < RESET_TYPE_MAX_METHOD)
		efx->reset_pending &= -(1 << (method + 1));
	else /* it doesn't fit into the well-ordered scope hierarchy */
		__clear_bit(method, &efx->reset_pending);

	/* Reinitialise bus-mastering, which may have been turned off before
	 * the reset was scheduled. This is still appropriate, even in the
	 * RESET_TYPE_DISABLE since this driver generally assumes the hardware
	 * can respond to requests.
	 */
	pci_set_master(efx->pci_dev);

out:
	/* Leave device stopped if necessary */
	disabled = rc ||
		method == RESET_TYPE_DISABLE ||
		method == RESET_TYPE_RECOVER_OR_DISABLE;
	if (efx_nic_rev(efx) != EFX_REV_EF100)
		rc2 = efx_siena_reset_up(efx, method, !disabled);
	if (rc2) {
		disabled = true;
		if (!rc)
			rc = rc2;
	}

	if (disabled) {
		dev_close(efx->net_dev);
		netif_err(efx, drv, efx->net_dev, "has been disabled\n");
		efx->state = STATE_DISABLED;
	} else {
		netif_dbg(efx, drv, efx->net_dev, "reset complete\n");
		efx_device_attach_if_not_resetting(efx);
	}
	return rc;
}

/* The worker thread exists so that code that cannot sleep can
 * schedule a reset for later.
 */
static void efx_reset_work(struct work_struct *data)
{
	struct efx_nic *efx = container_of(data, struct efx_nic, reset_work);
	unsigned long pending;
	enum reset_type method;

	pending = READ_ONCE(efx->reset_pending);
	method = fls(pending) - 1;

	if (method == RESET_TYPE_MC_BIST)
		efx_wait_for_bist_end(efx);

	if ((method == RESET_TYPE_RECOVER_OR_DISABLE ||
	     method == RESET_TYPE_RECOVER_OR_ALL) &&
	    efx_siena_try_recovery(efx))
		return;

	if (!pending)
		return;

	rtnl_lock();

	/* We checked the state in efx_siena_schedule_reset() but it may
	 * have changed by now.  Now that we have the RTNL lock,
	 * it cannot change again.
	 */
	if (efx->state == STATE_READY)
		(void)efx_siena_reset(efx, method);

	rtnl_unlock();
}

void efx_siena_schedule_reset(struct efx_nic *efx, enum reset_type type)
{
	enum reset_type method;

	if (efx->state == STATE_RECOVERY) {
		netif_dbg(efx, drv, efx->net_dev,
			  "recovering: skip scheduling %s reset\n",
			  RESET_TYPE(type));
		return;
	}

	switch (type) {
	case RESET_TYPE_INVISIBLE:
	case RESET_TYPE_ALL:
	case RESET_TYPE_RECOVER_OR_ALL:
	case RESET_TYPE_WORLD:
	case RESET_TYPE_DISABLE:
	case RESET_TYPE_RECOVER_OR_DISABLE:
	case RESET_TYPE_DATAPATH:
	case RESET_TYPE_MC_BIST:
	case RESET_TYPE_MCDI_TIMEOUT:
		method = type;
		netif_dbg(efx, drv, efx->net_dev, "scheduling %s reset\n",
			  RESET_TYPE(method));
		break;
	default:
		method = efx->type->map_reset_reason(type);
		netif_dbg(efx, drv, efx->net_dev,
			  "scheduling %s reset for %s\n",
			  RESET_TYPE(method), RESET_TYPE(type));
		break;
	}

	set_bit(method, &efx->reset_pending);
	smp_mb(); /* ensure we change reset_pending before checking state */

	/* If we're not READY then just leave the flags set as the cue
	 * to abort probing or reschedule the reset later.
	 */
	if (READ_ONCE(efx->state) != STATE_READY)
		return;

	/* efx_process_channel() will no longer read events once a
	 * reset is scheduled. So switch back to poll'd MCDI completions.
	 */
	efx_siena_mcdi_mode_poll(efx);

	efx_siena_queue_reset_work(efx);
}

/**************************************************************************
 *
 * Dummy NIC operations
 *
 * Can be used for some unimplemented operations
 * Needed so all function pointers are valid and do not have to be tested
 * before use
 *
 **************************************************************************/
int efx_siena_port_dummy_op_int(struct efx_nic *efx)
{
	return 0;
}

void efx_siena_port_dummy_op_void(struct efx_nic *efx) {}

/**************************************************************************
 *
 * Data housekeeping
 *
 **************************************************************************/

/* This zeroes out and then fills in the invariants in a struct
 * efx_nic (including all sub-structures).
 */
int efx_siena_init_struct(struct efx_nic *efx,
			  struct pci_dev *pci_dev, struct net_device *net_dev)
{
	int rc = -ENOMEM;

	/* Initialise common structures */
	INIT_LIST_HEAD(&efx->node);
	INIT_LIST_HEAD(&efx->secondary_list);
	spin_lock_init(&efx->biu_lock);
#ifdef CONFIG_SFC_SIENA_MTD
	INIT_LIST_HEAD(&efx->mtd_list);
#endif
	INIT_WORK(&efx->reset_work, efx_reset_work);
	INIT_DELAYED_WORK(&efx->monitor_work, efx_monitor);
	efx_siena_selftest_async_init(efx);
	efx->pci_dev = pci_dev;
	efx->msg_enable = debug;
	efx->state = STATE_UNINIT;
	strlcpy(efx->name, pci_name(pci_dev), sizeof(efx->name));

	efx->net_dev = net_dev;
	efx->rx_prefix_size = efx->type->rx_prefix_size;
	efx->rx_ip_align =
		NET_IP_ALIGN ? (efx->rx_prefix_size + NET_IP_ALIGN) % 4 : 0;
	efx->rx_packet_hash_offset =
		efx->type->rx_hash_offset - efx->type->rx_prefix_size;
	efx->rx_packet_ts_offset =
		efx->type->rx_ts_offset - efx->type->rx_prefix_size;
	INIT_LIST_HEAD(&efx->rss_context.list);
	efx->rss_context.context_id = EFX_MCDI_RSS_CONTEXT_INVALID;
	mutex_init(&efx->rss_lock);
	efx->vport_id = EVB_PORT_ID_ASSIGNED;
	spin_lock_init(&efx->stats_lock);
	efx->vi_stride = EFX_DEFAULT_VI_STRIDE;
	efx->num_mac_stats = MC_CMD_MAC_NSTATS;
	BUILD_BUG_ON(MC_CMD_MAC_NSTATS - 1 != MC_CMD_MAC_GENERATION_END);
	mutex_init(&efx->mac_lock);
	init_rwsem(&efx->filter_sem);
#ifdef CONFIG_RFS_ACCEL
	mutex_init(&efx->rps_mutex);
	spin_lock_init(&efx->rps_hash_lock);
	/* Failure to allocate is not fatal, but may degrade ARFS performance */
	efx->rps_hash_table = kcalloc(EFX_ARFS_HASH_TABLE_SIZE,
				      sizeof(*efx->rps_hash_table), GFP_KERNEL);
#endif
	efx->mdio.dev = net_dev;
	INIT_WORK(&efx->mac_work, efx_mac_work);
	init_waitqueue_head(&efx->flush_wq);

	efx->tx_queues_per_channel = 1;
	efx->rxq_entries = EFX_DEFAULT_DMAQ_SIZE;
	efx->txq_entries = EFX_DEFAULT_DMAQ_SIZE;

	efx->mem_bar = UINT_MAX;

	rc = efx_siena_init_channels(efx);
	if (rc)
		goto fail;

	/* Would be good to use the net_dev name, but we're too early */
	snprintf(efx->workqueue_name, sizeof(efx->workqueue_name), "sfc%s",
		 pci_name(pci_dev));
	efx->workqueue = create_singlethread_workqueue(efx->workqueue_name);
	if (!efx->workqueue) {
		rc = -ENOMEM;
		goto fail;
	}

	return 0;

fail:
	efx_siena_fini_struct(efx);
	return rc;
}

void efx_siena_fini_struct(struct efx_nic *efx)
{
#ifdef CONFIG_RFS_ACCEL
	kfree(efx->rps_hash_table);
#endif

	efx_siena_fini_channels(efx);

	kfree(efx->vpd_sn);

	if (efx->workqueue) {
		destroy_workqueue(efx->workqueue);
		efx->workqueue = NULL;
	}
}

/* This configures the PCI device to enable I/O and DMA. */
int efx_siena_init_io(struct efx_nic *efx, int bar, dma_addr_t dma_mask,
		      unsigned int mem_map_size)
{
	struct pci_dev *pci_dev = efx->pci_dev;
	int rc;

	efx->mem_bar = UINT_MAX;

	netif_dbg(efx, probe, efx->net_dev, "initialising I/O bar=%d\n", bar);

	rc = pci_enable_device(pci_dev);
	if (rc) {
		netif_err(efx, probe, efx->net_dev,
			  "failed to enable PCI device\n");
		goto fail1;
	}

	pci_set_master(pci_dev);

	rc = dma_set_mask_and_coherent(&pci_dev->dev, dma_mask);
	if (rc) {
		netif_err(efx, probe, efx->net_dev,
			  "could not find a suitable DMA mask\n");
		goto fail2;
	}
	netif_dbg(efx, probe, efx->net_dev,
		  "using DMA mask %llx\n", (unsigned long long)dma_mask);

	efx->membase_phys = pci_resource_start(efx->pci_dev, bar);
	if (!efx->membase_phys) {
		netif_err(efx, probe, efx->net_dev,
			  "ERROR: No BAR%d mapping from the BIOS. "
			  "Try pci=realloc on the kernel command line\n", bar);
		rc = -ENODEV;
		goto fail3;
	}

	rc = pci_request_region(pci_dev, bar, "sfc");
	if (rc) {
		netif_err(efx, probe, efx->net_dev,
			  "request for memory BAR[%d] failed\n", bar);
		rc = -EIO;
		goto fail3;
	}
	efx->mem_bar = bar;
	efx->membase = ioremap(efx->membase_phys, mem_map_size);
	if (!efx->membase) {
		netif_err(efx, probe, efx->net_dev,
			  "could not map memory BAR[%d] at %llx+%x\n", bar,
			  (unsigned long long)efx->membase_phys, mem_map_size);
		rc = -ENOMEM;
		goto fail4;
	}
	netif_dbg(efx, probe, efx->net_dev,
		  "memory BAR[%d] at %llx+%x (virtual %p)\n", bar,
		  (unsigned long long)efx->membase_phys, mem_map_size,
		  efx->membase);

	return 0;

fail4:
	pci_release_region(efx->pci_dev, bar);
fail3:
	efx->membase_phys = 0;
fail2:
	pci_disable_device(efx->pci_dev);
fail1:
	return rc;
}

void efx_siena_fini_io(struct efx_nic *efx)
{
	netif_dbg(efx, drv, efx->net_dev, "shutting down I/O\n");

	if (efx->membase) {
		iounmap(efx->membase);
		efx->membase = NULL;
	}

	if (efx->membase_phys) {
		pci_release_region(efx->pci_dev, efx->mem_bar);
		efx->membase_phys = 0;
		efx->mem_bar = UINT_MAX;
	}

	/* Don't disable bus-mastering if VFs are assigned */
	if (!pci_vfs_assigned(efx->pci_dev))
		pci_disable_device(efx->pci_dev);
}

#ifdef CONFIG_SFC_SIENA_MCDI_LOGGING
static ssize_t mcdi_logging_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct efx_nic *efx = dev_get_drvdata(dev);
	struct efx_mcdi_iface *mcdi = efx_mcdi(efx);

	return scnprintf(buf, PAGE_SIZE, "%d\n", mcdi->logging_enabled);
}

static ssize_t mcdi_logging_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct efx_nic *efx = dev_get_drvdata(dev);
	struct efx_mcdi_iface *mcdi = efx_mcdi(efx);
	bool enable = count > 0 && *buf != '0';

	mcdi->logging_enabled = enable;
	return count;
}

static DEVICE_ATTR_RW(mcdi_logging);

void efx_siena_init_mcdi_logging(struct efx_nic *efx)
{
	int rc = device_create_file(&efx->pci_dev->dev, &dev_attr_mcdi_logging);

	if (rc) {
		netif_warn(efx, drv, efx->net_dev,
			   "failed to init net dev attributes\n");
	}
}

void efx_siena_fini_mcdi_logging(struct efx_nic *efx)
{
	device_remove_file(&efx->pci_dev->dev, &dev_attr_mcdi_logging);
}
#endif

/* A PCI error affecting this device was detected.
 * At this point MMIO and DMA may be disabled.
 * Stop the software path and request a slot reset.
 */
static pci_ers_result_t efx_io_error_detected(struct pci_dev *pdev,
					      pci_channel_state_t state)
{
	pci_ers_result_t status = PCI_ERS_RESULT_RECOVERED;
	struct efx_nic *efx = pci_get_drvdata(pdev);

	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	rtnl_lock();

	if (efx->state != STATE_DISABLED) {
		efx->state = STATE_RECOVERY;
		efx->reset_pending = 0;

		efx_device_detach_sync(efx);

		efx_siena_stop_all(efx);
		efx_siena_disable_interrupts(efx);

		status = PCI_ERS_RESULT_NEED_RESET;
	} else {
		/* If the interface is disabled we don't want to do anything
		 * with it.
		 */
		status = PCI_ERS_RESULT_RECOVERED;
	}

	rtnl_unlock();

	pci_disable_device(pdev);

	return status;
}

/* Fake a successful reset, which will be performed later in efx_io_resume. */
static pci_ers_result_t efx_io_slot_reset(struct pci_dev *pdev)
{
	struct efx_nic *efx = pci_get_drvdata(pdev);
	pci_ers_result_t status = PCI_ERS_RESULT_RECOVERED;

	if (pci_enable_device(pdev)) {
		netif_err(efx, hw, efx->net_dev,
			  "Cannot re-enable PCI device after reset.\n");
		status =  PCI_ERS_RESULT_DISCONNECT;
	}

	return status;
}

/* Perform the actual reset and resume I/O operations. */
static void efx_io_resume(struct pci_dev *pdev)
{
	struct efx_nic *efx = pci_get_drvdata(pdev);
	int rc;

	rtnl_lock();

	if (efx->state == STATE_DISABLED)
		goto out;

	rc = efx_siena_reset(efx, RESET_TYPE_ALL);
	if (rc) {
		netif_err(efx, hw, efx->net_dev,
			  "efx_siena_reset failed after PCI error (%d)\n", rc);
	} else {
		efx->state = STATE_READY;
		netif_dbg(efx, hw, efx->net_dev,
			  "Done resetting and resuming IO after PCI error.\n");
	}

out:
	rtnl_unlock();
}

/* For simplicity and reliability, we always require a slot reset and try to
 * reset the hardware when a pci error affecting the device is detected.
 * We leave both the link_reset and mmio_enabled callback unimplemented:
 * with our request for slot reset the mmio_enabled callback will never be
 * called, and the link_reset callback is not used by AER or EEH mechanisms.
 */
const struct pci_error_handlers efx_siena_err_handlers = {
	.error_detected = efx_io_error_detected,
	.slot_reset	= efx_io_slot_reset,
	.resume		= efx_io_resume,
};

/* Determine whether the NIC will be able to handle TX offloads for a given
 * encapsulated packet.
 */
static bool efx_can_encap_offloads(struct efx_nic *efx, struct sk_buff *skb)
{
	struct gre_base_hdr *greh;
	__be16 dst_port;
	u8 ipproto;

	/* Does the NIC support encap offloads?
	 * If not, we should never get here, because we shouldn't have
	 * advertised encap offload feature flags in the first place.
	 */
	if (WARN_ON_ONCE(!efx->type->udp_tnl_has_port))
		return false;

	/* Determine encapsulation protocol in use */
	switch (skb->protocol) {
	case htons(ETH_P_IP):
		ipproto = ip_hdr(skb)->protocol;
		break;
	case htons(ETH_P_IPV6):
		/* If there are extension headers, this will cause us to
		 * think we can't offload something that we maybe could have.
		 */
		ipproto = ipv6_hdr(skb)->nexthdr;
		break;
	default:
		/* Not IP, so can't offload it */
		return false;
	}
	switch (ipproto) {
	case IPPROTO_GRE:
		/* We support NVGRE but not IP over GRE or random gretaps.
		 * Specifically, the NIC will accept GRE as encapsulated if
		 * the inner protocol is Ethernet, but only handle it
		 * correctly if the GRE header is 8 bytes long.  Moreover,
		 * it will not update the Checksum or Sequence Number fields
		 * if they are present.  (The Routing Present flag,
		 * GRE_ROUTING, cannot be set else the header would be more
		 * than 8 bytes long; so we don't have to worry about it.)
		 */
		if (skb->inner_protocol_type != ENCAP_TYPE_ETHER)
			return false;
		if (ntohs(skb->inner_protocol) != ETH_P_TEB)
			return false;
		if (skb_inner_mac_header(skb) - skb_transport_header(skb) != 8)
			return false;
		greh = (struct gre_base_hdr *)skb_transport_header(skb);
		return !(greh->flags & (GRE_CSUM | GRE_SEQ));
	case IPPROTO_UDP:
		/* If the port is registered for a UDP tunnel, we assume the
		 * packet is for that tunnel, and the NIC will handle it as
		 * such.  If not, the NIC won't know what to do with it.
		 */
		dst_port = udp_hdr(skb)->dest;
		return efx->type->udp_tnl_has_port(efx, dst_port);
	default:
		return false;
	}
}

netdev_features_t efx_siena_features_check(struct sk_buff *skb,
					   struct net_device *dev,
					   netdev_features_t features)
{
	struct efx_nic *efx = netdev_priv(dev);

	if (skb->encapsulation) {
		if (features & NETIF_F_GSO_MASK)
			/* Hardware can only do TSO with at most 208 bytes
			 * of headers.
			 */
			if (skb_inner_transport_offset(skb) >
			    EFX_TSO2_MAX_HDRLEN)
				features &= ~(NETIF_F_GSO_MASK);
		if (features & (NETIF_F_GSO_MASK | NETIF_F_CSUM_MASK))
			if (!efx_can_encap_offloads(efx, skb))
				features &= ~(NETIF_F_GSO_MASK |
					      NETIF_F_CSUM_MASK);
	}
	return features;
}

int efx_siena_get_phys_port_id(struct net_device *net_dev,
			       struct netdev_phys_item_id *ppid)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	if (efx->type->get_phys_port_id)
		return efx->type->get_phys_port_id(efx, ppid);
	else
		return -EOPNOTSUPP;
}

int efx_siena_get_phys_port_name(struct net_device *net_dev,
				 char *name, size_t len)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	if (snprintf(name, len, "p%u", efx->port_num) >= len)
		return -EINVAL;
	return 0;
}
