// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2005-2013 Solarflare Communications Inc.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/notifier.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/in.h>
#include <linux/ethtool.h>
#include <linux/topology.h>
#include <linux/gfp.h>
#include <linux/aer.h>
#include <linux/interrupt.h>
#include "net_driver.h"
#include "efx.h"
#include "nic.h"
#include "selftest.h"

#include "workarounds.h"

/**************************************************************************
 *
 * Type name strings
 *
 **************************************************************************
 */

/* Loopback mode names (see LOOPBACK_MODE()) */
const unsigned int ef4_loopback_mode_max = LOOPBACK_MAX;
const char *const ef4_loopback_mode_names[] = {
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

const unsigned int ef4_reset_type_max = RESET_TYPE_MAX;
const char *const ef4_reset_type_names[] = {
	[RESET_TYPE_INVISIBLE]          = "INVISIBLE",
	[RESET_TYPE_ALL]                = "ALL",
	[RESET_TYPE_RECOVER_OR_ALL]     = "RECOVER_OR_ALL",
	[RESET_TYPE_WORLD]              = "WORLD",
	[RESET_TYPE_RECOVER_OR_DISABLE] = "RECOVER_OR_DISABLE",
	[RESET_TYPE_DATAPATH]           = "DATAPATH",
	[RESET_TYPE_DISABLE]            = "DISABLE",
	[RESET_TYPE_TX_WATCHDOG]        = "TX_WATCHDOG",
	[RESET_TYPE_INT_ERROR]          = "INT_ERROR",
	[RESET_TYPE_RX_RECOVERY]        = "RX_RECOVERY",
	[RESET_TYPE_DMA_ERROR]          = "DMA_ERROR",
	[RESET_TYPE_TX_SKIP]            = "TX_SKIP",
};

/* Reset workqueue. If any NIC has a hardware failure then a reset will be
 * queued onto this work queue. This is not a per-nic work queue, because
 * ef4_reset_work() acquires the rtnl lock, so resets are naturally serialised.
 */
static struct workqueue_struct *reset_workqueue;

/* How often and how many times to poll for a reset while waiting for a
 * BIST that another function started to complete.
 */
#define BIST_WAIT_DELAY_MS	100
#define BIST_WAIT_DELAY_COUNT	100

/**************************************************************************
 *
 * Configurable values
 *
 *************************************************************************/

/*
 * Use separate channels for TX and RX events
 *
 * Set this to 1 to use separate channels for TX and RX. It allows us
 * to control interrupt affinity separately for TX and RX.
 *
 * This is only used in MSI-X interrupt mode
 */
bool ef4_separate_tx_channels;
module_param(ef4_separate_tx_channels, bool, 0444);
MODULE_PARM_DESC(ef4_separate_tx_channels,
		 "Use separate channels for TX and RX");

/* This is the weight assigned to each of the (per-channel) virtual
 * NAPI devices.
 */
static int napi_weight = 64;

/* This is the time (in jiffies) between invocations of the hardware
 * monitor.
 * On Falcon-based NICs, this will:
 * - Check the on-board hardware monitor;
 * - Poll the link state and reconfigure the hardware as necessary.
 * On Siena-based NICs for power systems with EEH support, this will give EEH a
 * chance to start.
 */
static unsigned int ef4_monitor_interval = 1 * HZ;

/* Initial interrupt moderation settings.  They can be modified after
 * module load with ethtool.
 *
 * The default for RX should strike a balance between increasing the
 * round-trip latency and reducing overhead.
 */
static unsigned int rx_irq_mod_usec = 60;

/* Initial interrupt moderation settings.  They can be modified after
 * module load with ethtool.
 *
 * This default is chosen to ensure that a 10G link does not go idle
 * while a TX queue is stopped after it has become full.  A queue is
 * restarted when it drops below half full.  The time this takes (assuming
 * worst case 3 descriptors per packet and 1024 descriptors) is
 *   512 / 3 * 1.2 = 205 usec.
 */
static unsigned int tx_irq_mod_usec = 150;

/* This is the first interrupt mode to try out of:
 * 0 => MSI-X
 * 1 => MSI
 * 2 => legacy
 */
static unsigned int interrupt_mode;

/* This is the requested number of CPUs to use for Receive-Side Scaling (RSS),
 * i.e. the number of CPUs among which we may distribute simultaneous
 * interrupt handling.
 *
 * Cards without MSI-X will only target one CPU via legacy or MSI interrupt.
 * The default (0) means to assign an interrupt to each core.
 */
static unsigned int rss_cpus;
module_param(rss_cpus, uint, 0444);
MODULE_PARM_DESC(rss_cpus, "Number of CPUs to use for Receive-Side Scaling");

static bool phy_flash_cfg;
module_param(phy_flash_cfg, bool, 0644);
MODULE_PARM_DESC(phy_flash_cfg, "Set PHYs into reflash mode initially");

static unsigned irq_adapt_low_thresh = 8000;
module_param(irq_adapt_low_thresh, uint, 0644);
MODULE_PARM_DESC(irq_adapt_low_thresh,
		 "Threshold score for reducing IRQ moderation");

static unsigned irq_adapt_high_thresh = 16000;
module_param(irq_adapt_high_thresh, uint, 0644);
MODULE_PARM_DESC(irq_adapt_high_thresh,
		 "Threshold score for increasing IRQ moderation");

static unsigned debug = (NETIF_MSG_DRV | NETIF_MSG_PROBE |
			 NETIF_MSG_LINK | NETIF_MSG_IFDOWN |
			 NETIF_MSG_IFUP | NETIF_MSG_RX_ERR |
			 NETIF_MSG_TX_ERR | NETIF_MSG_HW);
module_param(debug, uint, 0);
MODULE_PARM_DESC(debug, "Bitmapped debugging message enable value");

/**************************************************************************
 *
 * Utility functions and prototypes
 *
 *************************************************************************/

static int ef4_soft_enable_interrupts(struct ef4_nic *efx);
static void ef4_soft_disable_interrupts(struct ef4_nic *efx);
static void ef4_remove_channel(struct ef4_channel *channel);
static void ef4_remove_channels(struct ef4_nic *efx);
static const struct ef4_channel_type ef4_default_channel_type;
static void ef4_remove_port(struct ef4_nic *efx);
static void ef4_init_napi_channel(struct ef4_channel *channel);
static void ef4_fini_napi(struct ef4_nic *efx);
static void ef4_fini_napi_channel(struct ef4_channel *channel);
static void ef4_fini_struct(struct ef4_nic *efx);
static void ef4_start_all(struct ef4_nic *efx);
static void ef4_stop_all(struct ef4_nic *efx);

#define EF4_ASSERT_RESET_SERIALISED(efx)		\
	do {						\
		if ((efx->state == STATE_READY) ||	\
		    (efx->state == STATE_RECOVERY) ||	\
		    (efx->state == STATE_DISABLED))	\
			ASSERT_RTNL();			\
	} while (0)

static int ef4_check_disabled(struct ef4_nic *efx)
{
	if (efx->state == STATE_DISABLED || efx->state == STATE_RECOVERY) {
		netif_err(efx, drv, efx->net_dev,
			  "device is disabled due to earlier errors\n");
		return -EIO;
	}
	return 0;
}

/**************************************************************************
 *
 * Event queue processing
 *
 *************************************************************************/

/* Process channel's event queue
 *
 * This function is responsible for processing the event queue of a
 * single channel.  The caller must guarantee that this function will
 * never be concurrently called more than once on the same channel,
 * though different channels may be being processed concurrently.
 */
static int ef4_process_channel(struct ef4_channel *channel, int budget)
{
	struct ef4_tx_queue *tx_queue;
	int spent;

	if (unlikely(!channel->enabled))
		return 0;

	ef4_for_each_channel_tx_queue(tx_queue, channel) {
		tx_queue->pkts_compl = 0;
		tx_queue->bytes_compl = 0;
	}

	spent = ef4_nic_process_eventq(channel, budget);
	if (spent && ef4_channel_has_rx_queue(channel)) {
		struct ef4_rx_queue *rx_queue =
			ef4_channel_get_rx_queue(channel);

		ef4_rx_flush_packet(channel);
		ef4_fast_push_rx_descriptors(rx_queue, true);
	}

	/* Update BQL */
	ef4_for_each_channel_tx_queue(tx_queue, channel) {
		if (tx_queue->bytes_compl) {
			netdev_tx_completed_queue(tx_queue->core_txq,
				tx_queue->pkts_compl, tx_queue->bytes_compl);
		}
	}

	return spent;
}

/* NAPI poll handler
 *
 * NAPI guarantees serialisation of polls of the same device, which
 * provides the guarantee required by ef4_process_channel().
 */
static void ef4_update_irq_mod(struct ef4_nic *efx, struct ef4_channel *channel)
{
	int step = efx->irq_mod_step_us;

	if (channel->irq_mod_score < irq_adapt_low_thresh) {
		if (channel->irq_moderation_us > step) {
			channel->irq_moderation_us -= step;
			efx->type->push_irq_moderation(channel);
		}
	} else if (channel->irq_mod_score > irq_adapt_high_thresh) {
		if (channel->irq_moderation_us <
		    efx->irq_rx_moderation_us) {
			channel->irq_moderation_us += step;
			efx->type->push_irq_moderation(channel);
		}
	}

	channel->irq_count = 0;
	channel->irq_mod_score = 0;
}

static int ef4_poll(struct napi_struct *napi, int budget)
{
	struct ef4_channel *channel =
		container_of(napi, struct ef4_channel, napi_str);
	struct ef4_nic *efx = channel->efx;
	int spent;

	netif_vdbg(efx, intr, efx->net_dev,
		   "channel %d NAPI poll executing on CPU %d\n",
		   channel->channel, raw_smp_processor_id());

	spent = ef4_process_channel(channel, budget);

	if (spent < budget) {
		if (ef4_channel_has_rx_queue(channel) &&
		    efx->irq_rx_adaptive &&
		    unlikely(++channel->irq_count == 1000)) {
			ef4_update_irq_mod(efx, channel);
		}

		ef4_filter_rfs_expire(channel);

		/* There is no race here; although napi_disable() will
		 * only wait for napi_complete(), this isn't a problem
		 * since ef4_nic_eventq_read_ack() will have no effect if
		 * interrupts have already been disabled.
		 */
		napi_complete_done(napi, spent);
		ef4_nic_eventq_read_ack(channel);
	}

	return spent;
}

/* Create event queue
 * Event queue memory allocations are done only once.  If the channel
 * is reset, the memory buffer will be reused; this guards against
 * errors during channel reset and also simplifies interrupt handling.
 */
static int ef4_probe_eventq(struct ef4_channel *channel)
{
	struct ef4_nic *efx = channel->efx;
	unsigned long entries;

	netif_dbg(efx, probe, efx->net_dev,
		  "chan %d create event queue\n", channel->channel);

	/* Build an event queue with room for one event per tx and rx buffer,
	 * plus some extra for link state events and MCDI completions. */
	entries = roundup_pow_of_two(efx->rxq_entries + efx->txq_entries + 128);
	EF4_BUG_ON_PARANOID(entries > EF4_MAX_EVQ_SIZE);
	channel->eventq_mask = max(entries, EF4_MIN_EVQ_SIZE) - 1;

	return ef4_nic_probe_eventq(channel);
}

/* Prepare channel's event queue */
static int ef4_init_eventq(struct ef4_channel *channel)
{
	struct ef4_nic *efx = channel->efx;
	int rc;

	EF4_WARN_ON_PARANOID(channel->eventq_init);

	netif_dbg(efx, drv, efx->net_dev,
		  "chan %d init event queue\n", channel->channel);

	rc = ef4_nic_init_eventq(channel);
	if (rc == 0) {
		efx->type->push_irq_moderation(channel);
		channel->eventq_read_ptr = 0;
		channel->eventq_init = true;
	}
	return rc;
}

/* Enable event queue processing and NAPI */
void ef4_start_eventq(struct ef4_channel *channel)
{
	netif_dbg(channel->efx, ifup, channel->efx->net_dev,
		  "chan %d start event queue\n", channel->channel);

	/* Make sure the NAPI handler sees the enabled flag set */
	channel->enabled = true;
	smp_wmb();

	napi_enable(&channel->napi_str);
	ef4_nic_eventq_read_ack(channel);
}

/* Disable event queue processing and NAPI */
void ef4_stop_eventq(struct ef4_channel *channel)
{
	if (!channel->enabled)
		return;

	napi_disable(&channel->napi_str);
	channel->enabled = false;
}

static void ef4_fini_eventq(struct ef4_channel *channel)
{
	if (!channel->eventq_init)
		return;

	netif_dbg(channel->efx, drv, channel->efx->net_dev,
		  "chan %d fini event queue\n", channel->channel);

	ef4_nic_fini_eventq(channel);
	channel->eventq_init = false;
}

static void ef4_remove_eventq(struct ef4_channel *channel)
{
	netif_dbg(channel->efx, drv, channel->efx->net_dev,
		  "chan %d remove event queue\n", channel->channel);

	ef4_nic_remove_eventq(channel);
}

/**************************************************************************
 *
 * Channel handling
 *
 *************************************************************************/

/* Allocate and initialise a channel structure. */
static struct ef4_channel *
ef4_alloc_channel(struct ef4_nic *efx, int i, struct ef4_channel *old_channel)
{
	struct ef4_channel *channel;
	struct ef4_rx_queue *rx_queue;
	struct ef4_tx_queue *tx_queue;
	int j;

	channel = kzalloc(sizeof(*channel), GFP_KERNEL);
	if (!channel)
		return NULL;

	channel->efx = efx;
	channel->channel = i;
	channel->type = &ef4_default_channel_type;

	for (j = 0; j < EF4_TXQ_TYPES; j++) {
		tx_queue = &channel->tx_queue[j];
		tx_queue->efx = efx;
		tx_queue->queue = i * EF4_TXQ_TYPES + j;
		tx_queue->channel = channel;
	}

	rx_queue = &channel->rx_queue;
	rx_queue->efx = efx;
	timer_setup(&rx_queue->slow_fill, ef4_rx_slow_fill, 0);

	return channel;
}

/* Allocate and initialise a channel structure, copying parameters
 * (but not resources) from an old channel structure.
 */
static struct ef4_channel *
ef4_copy_channel(const struct ef4_channel *old_channel)
{
	struct ef4_channel *channel;
	struct ef4_rx_queue *rx_queue;
	struct ef4_tx_queue *tx_queue;
	int j;

	channel = kmalloc(sizeof(*channel), GFP_KERNEL);
	if (!channel)
		return NULL;

	*channel = *old_channel;

	channel->napi_dev = NULL;
	INIT_HLIST_NODE(&channel->napi_str.napi_hash_node);
	channel->napi_str.napi_id = 0;
	channel->napi_str.state = 0;
	memset(&channel->eventq, 0, sizeof(channel->eventq));

	for (j = 0; j < EF4_TXQ_TYPES; j++) {
		tx_queue = &channel->tx_queue[j];
		if (tx_queue->channel)
			tx_queue->channel = channel;
		tx_queue->buffer = NULL;
		memset(&tx_queue->txd, 0, sizeof(tx_queue->txd));
	}

	rx_queue = &channel->rx_queue;
	rx_queue->buffer = NULL;
	memset(&rx_queue->rxd, 0, sizeof(rx_queue->rxd));
	timer_setup(&rx_queue->slow_fill, ef4_rx_slow_fill, 0);

	return channel;
}

static int ef4_probe_channel(struct ef4_channel *channel)
{
	struct ef4_tx_queue *tx_queue;
	struct ef4_rx_queue *rx_queue;
	int rc;

	netif_dbg(channel->efx, probe, channel->efx->net_dev,
		  "creating channel %d\n", channel->channel);

	rc = channel->type->pre_probe(channel);
	if (rc)
		goto fail;

	rc = ef4_probe_eventq(channel);
	if (rc)
		goto fail;

	ef4_for_each_channel_tx_queue(tx_queue, channel) {
		rc = ef4_probe_tx_queue(tx_queue);
		if (rc)
			goto fail;
	}

	ef4_for_each_channel_rx_queue(rx_queue, channel) {
		rc = ef4_probe_rx_queue(rx_queue);
		if (rc)
			goto fail;
	}

	return 0;

fail:
	ef4_remove_channel(channel);
	return rc;
}

static void
ef4_get_channel_name(struct ef4_channel *channel, char *buf, size_t len)
{
	struct ef4_nic *efx = channel->efx;
	const char *type;
	int number;

	number = channel->channel;
	if (efx->tx_channel_offset == 0) {
		type = "";
	} else if (channel->channel < efx->tx_channel_offset) {
		type = "-rx";
	} else {
		type = "-tx";
		number -= efx->tx_channel_offset;
	}
	snprintf(buf, len, "%s%s-%d", efx->name, type, number);
}

static void ef4_set_channel_names(struct ef4_nic *efx)
{
	struct ef4_channel *channel;

	ef4_for_each_channel(channel, efx)
		channel->type->get_name(channel,
					efx->msi_context[channel->channel].name,
					sizeof(efx->msi_context[0].name));
}

static int ef4_probe_channels(struct ef4_nic *efx)
{
	struct ef4_channel *channel;
	int rc;

	/* Restart special buffer allocation */
	efx->next_buffer_table = 0;

	/* Probe channels in reverse, so that any 'extra' channels
	 * use the start of the buffer table. This allows the traffic
	 * channels to be resized without moving them or wasting the
	 * entries before them.
	 */
	ef4_for_each_channel_rev(channel, efx) {
		rc = ef4_probe_channel(channel);
		if (rc) {
			netif_err(efx, probe, efx->net_dev,
				  "failed to create channel %d\n",
				  channel->channel);
			goto fail;
		}
	}
	ef4_set_channel_names(efx);

	return 0;

fail:
	ef4_remove_channels(efx);
	return rc;
}

/* Channels are shutdown and reinitialised whilst the NIC is running
 * to propagate configuration changes (mtu, checksum offload), or
 * to clear hardware error conditions
 */
static void ef4_start_datapath(struct ef4_nic *efx)
{
	netdev_features_t old_features = efx->net_dev->features;
	bool old_rx_scatter = efx->rx_scatter;
	struct ef4_tx_queue *tx_queue;
	struct ef4_rx_queue *rx_queue;
	struct ef4_channel *channel;
	size_t rx_buf_len;

	/* Calculate the rx buffer allocation parameters required to
	 * support the current MTU, including padding for header
	 * alignment and overruns.
	 */
	efx->rx_dma_len = (efx->rx_prefix_size +
			   EF4_MAX_FRAME_LEN(efx->net_dev->mtu) +
			   efx->type->rx_buffer_padding);
	rx_buf_len = (sizeof(struct ef4_rx_page_state) +
		      efx->rx_ip_align + efx->rx_dma_len);
	if (rx_buf_len <= PAGE_SIZE) {
		efx->rx_scatter = efx->type->always_rx_scatter;
		efx->rx_buffer_order = 0;
	} else if (efx->type->can_rx_scatter) {
		BUILD_BUG_ON(EF4_RX_USR_BUF_SIZE % L1_CACHE_BYTES);
		BUILD_BUG_ON(sizeof(struct ef4_rx_page_state) +
			     2 * ALIGN(NET_IP_ALIGN + EF4_RX_USR_BUF_SIZE,
				       EF4_RX_BUF_ALIGNMENT) >
			     PAGE_SIZE);
		efx->rx_scatter = true;
		efx->rx_dma_len = EF4_RX_USR_BUF_SIZE;
		efx->rx_buffer_order = 0;
	} else {
		efx->rx_scatter = false;
		efx->rx_buffer_order = get_order(rx_buf_len);
	}

	ef4_rx_config_page_split(efx);
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
	efx->txq_stop_thresh = efx->txq_entries - ef4_tx_max_skb_descs(efx);
	efx->txq_wake_thresh = efx->txq_stop_thresh / 2;

	/* Initialise the channels */
	ef4_for_each_channel(channel, efx) {
		ef4_for_each_channel_tx_queue(tx_queue, channel) {
			ef4_init_tx_queue(tx_queue);
			atomic_inc(&efx->active_queues);
		}

		ef4_for_each_channel_rx_queue(rx_queue, channel) {
			ef4_init_rx_queue(rx_queue);
			atomic_inc(&efx->active_queues);
			ef4_stop_eventq(channel);
			ef4_fast_push_rx_descriptors(rx_queue, false);
			ef4_start_eventq(channel);
		}

		WARN_ON(channel->rx_pkt_n_frags);
	}

	if (netif_device_present(efx->net_dev))
		netif_tx_wake_all_queues(efx->net_dev);
}

static void ef4_stop_datapath(struct ef4_nic *efx)
{
	struct ef4_channel *channel;
	struct ef4_tx_queue *tx_queue;
	struct ef4_rx_queue *rx_queue;
	int rc;

	EF4_ASSERT_RESET_SERIALISED(efx);
	BUG_ON(efx->port_enabled);

	/* Stop RX refill */
	ef4_for_each_channel(channel, efx) {
		ef4_for_each_channel_rx_queue(rx_queue, channel)
			rx_queue->refill_enabled = false;
	}

	ef4_for_each_channel(channel, efx) {
		/* RX packet processing is pipelined, so wait for the
		 * NAPI handler to complete.  At least event queue 0
		 * might be kept active by non-data events, so don't
		 * use napi_synchronize() but actually disable NAPI
		 * temporarily.
		 */
		if (ef4_channel_has_rx_queue(channel)) {
			ef4_stop_eventq(channel);
			ef4_start_eventq(channel);
		}
	}

	rc = efx->type->fini_dmaq(efx);
	if (rc && EF4_WORKAROUND_7803(efx)) {
		/* Schedule a reset to recover from the flush failure. The
		 * descriptor caches reference memory we're about to free,
		 * but falcon_reconfigure_mac_wrapper() won't reconnect
		 * the MACs because of the pending reset.
		 */
		netif_err(efx, drv, efx->net_dev,
			  "Resetting to recover from flush failure\n");
		ef4_schedule_reset(efx, RESET_TYPE_ALL);
	} else if (rc) {
		netif_err(efx, drv, efx->net_dev, "failed to flush queues\n");
	} else {
		netif_dbg(efx, drv, efx->net_dev,
			  "successfully flushed all queues\n");
	}

	ef4_for_each_channel(channel, efx) {
		ef4_for_each_channel_rx_queue(rx_queue, channel)
			ef4_fini_rx_queue(rx_queue);
		ef4_for_each_possible_channel_tx_queue(tx_queue, channel)
			ef4_fini_tx_queue(tx_queue);
	}
}

static void ef4_remove_channel(struct ef4_channel *channel)
{
	struct ef4_tx_queue *tx_queue;
	struct ef4_rx_queue *rx_queue;

	netif_dbg(channel->efx, drv, channel->efx->net_dev,
		  "destroy chan %d\n", channel->channel);

	ef4_for_each_channel_rx_queue(rx_queue, channel)
		ef4_remove_rx_queue(rx_queue);
	ef4_for_each_possible_channel_tx_queue(tx_queue, channel)
		ef4_remove_tx_queue(tx_queue);
	ef4_remove_eventq(channel);
	channel->type->post_remove(channel);
}

static void ef4_remove_channels(struct ef4_nic *efx)
{
	struct ef4_channel *channel;

	ef4_for_each_channel(channel, efx)
		ef4_remove_channel(channel);
}

int
ef4_realloc_channels(struct ef4_nic *efx, u32 rxq_entries, u32 txq_entries)
{
	struct ef4_channel *other_channel[EF4_MAX_CHANNELS], *channel;
	u32 old_rxq_entries, old_txq_entries;
	unsigned i, next_buffer_table = 0;
	int rc, rc2;

	rc = ef4_check_disabled(efx);
	if (rc)
		return rc;

	/* Not all channels should be reallocated. We must avoid
	 * reallocating their buffer table entries.
	 */
	ef4_for_each_channel(channel, efx) {
		struct ef4_rx_queue *rx_queue;
		struct ef4_tx_queue *tx_queue;

		if (channel->type->copy)
			continue;
		next_buffer_table = max(next_buffer_table,
					channel->eventq.index +
					channel->eventq.entries);
		ef4_for_each_channel_rx_queue(rx_queue, channel)
			next_buffer_table = max(next_buffer_table,
						rx_queue->rxd.index +
						rx_queue->rxd.entries);
		ef4_for_each_channel_tx_queue(tx_queue, channel)
			next_buffer_table = max(next_buffer_table,
						tx_queue->txd.index +
						tx_queue->txd.entries);
	}

	ef4_device_detach_sync(efx);
	ef4_stop_all(efx);
	ef4_soft_disable_interrupts(efx);

	/* Clone channels (where possible) */
	memset(other_channel, 0, sizeof(other_channel));
	for (i = 0; i < efx->n_channels; i++) {
		channel = efx->channel[i];
		if (channel->type->copy)
			channel = channel->type->copy(channel);
		if (!channel) {
			rc = -ENOMEM;
			goto out;
		}
		other_channel[i] = channel;
	}

	/* Swap entry counts and channel pointers */
	old_rxq_entries = efx->rxq_entries;
	old_txq_entries = efx->txq_entries;
	efx->rxq_entries = rxq_entries;
	efx->txq_entries = txq_entries;
	for (i = 0; i < efx->n_channels; i++) {
		channel = efx->channel[i];
		efx->channel[i] = other_channel[i];
		other_channel[i] = channel;
	}

	/* Restart buffer table allocation */
	efx->next_buffer_table = next_buffer_table;

	for (i = 0; i < efx->n_channels; i++) {
		channel = efx->channel[i];
		if (!channel->type->copy)
			continue;
		rc = ef4_probe_channel(channel);
		if (rc)
			goto rollback;
		ef4_init_napi_channel(efx->channel[i]);
	}

out:
	/* Destroy unused channel structures */
	for (i = 0; i < efx->n_channels; i++) {
		channel = other_channel[i];
		if (channel && channel->type->copy) {
			ef4_fini_napi_channel(channel);
			ef4_remove_channel(channel);
			kfree(channel);
		}
	}

	rc2 = ef4_soft_enable_interrupts(efx);
	if (rc2) {
		rc = rc ? rc : rc2;
		netif_err(efx, drv, efx->net_dev,
			  "unable to restart interrupts on channel reallocation\n");
		ef4_schedule_reset(efx, RESET_TYPE_DISABLE);
	} else {
		ef4_start_all(efx);
		netif_device_attach(efx->net_dev);
	}
	return rc;

rollback:
	/* Swap back */
	efx->rxq_entries = old_rxq_entries;
	efx->txq_entries = old_txq_entries;
	for (i = 0; i < efx->n_channels; i++) {
		channel = efx->channel[i];
		efx->channel[i] = other_channel[i];
		other_channel[i] = channel;
	}
	goto out;
}

void ef4_schedule_slow_fill(struct ef4_rx_queue *rx_queue)
{
	mod_timer(&rx_queue->slow_fill, jiffies + msecs_to_jiffies(100));
}

static const struct ef4_channel_type ef4_default_channel_type = {
	.pre_probe		= ef4_channel_dummy_op_int,
	.post_remove		= ef4_channel_dummy_op_void,
	.get_name		= ef4_get_channel_name,
	.copy			= ef4_copy_channel,
	.keep_eventq		= false,
};

int ef4_channel_dummy_op_int(struct ef4_channel *channel)
{
	return 0;
}

void ef4_channel_dummy_op_void(struct ef4_channel *channel)
{
}

/**************************************************************************
 *
 * Port handling
 *
 **************************************************************************/

/* This ensures that the kernel is kept informed (via
 * netif_carrier_on/off) of the link status, and also maintains the
 * link status's stop on the port's TX queue.
 */
void ef4_link_status_changed(struct ef4_nic *efx)
{
	struct ef4_link_state *link_state = &efx->link_state;

	/* SFC Bug 5356: A net_dev notifier is registered, so we must ensure
	 * that no events are triggered between unregister_netdev() and the
	 * driver unloading. A more general condition is that NETDEV_CHANGE
	 * can only be generated between NETDEV_UP and NETDEV_DOWN */
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

void ef4_link_set_advertising(struct ef4_nic *efx, u32 advertising)
{
	efx->link_advertising = advertising;
	if (advertising) {
		if (advertising & ADVERTISED_Pause)
			efx->wanted_fc |= (EF4_FC_TX | EF4_FC_RX);
		else
			efx->wanted_fc &= ~(EF4_FC_TX | EF4_FC_RX);
		if (advertising & ADVERTISED_Asym_Pause)
			efx->wanted_fc ^= EF4_FC_TX;
	}
}

void ef4_link_set_wanted_fc(struct ef4_nic *efx, u8 wanted_fc)
{
	efx->wanted_fc = wanted_fc;
	if (efx->link_advertising) {
		if (wanted_fc & EF4_FC_RX)
			efx->link_advertising |= (ADVERTISED_Pause |
						  ADVERTISED_Asym_Pause);
		else
			efx->link_advertising &= ~(ADVERTISED_Pause |
						   ADVERTISED_Asym_Pause);
		if (wanted_fc & EF4_FC_TX)
			efx->link_advertising ^= ADVERTISED_Asym_Pause;
	}
}

static void ef4_fini_port(struct ef4_nic *efx);

/* We assume that efx->type->reconfigure_mac will always try to sync RX
 * filters and therefore needs to read-lock the filter table against freeing
 */
void ef4_mac_reconfigure(struct ef4_nic *efx)
{
	down_read(&efx->filter_sem);
	efx->type->reconfigure_mac(efx);
	up_read(&efx->filter_sem);
}

/* Push loopback/power/transmit disable settings to the PHY, and reconfigure
 * the MAC appropriately. All other PHY configuration changes are pushed
 * through phy_op->set_link_ksettings(), and pushed asynchronously to the MAC
 * through ef4_monitor().
 *
 * Callers must hold the mac_lock
 */
int __ef4_reconfigure_port(struct ef4_nic *efx)
{
	enum ef4_phy_mode phy_mode;
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
 * disabled. */
int ef4_reconfigure_port(struct ef4_nic *efx)
{
	int rc;

	EF4_ASSERT_RESET_SERIALISED(efx);

	mutex_lock(&efx->mac_lock);
	rc = __ef4_reconfigure_port(efx);
	mutex_unlock(&efx->mac_lock);

	return rc;
}

/* Asynchronous work item for changing MAC promiscuity and multicast
 * hash.  Avoid a drain/rx_ingress enable by reconfiguring the current
 * MAC directly. */
static void ef4_mac_work(struct work_struct *data)
{
	struct ef4_nic *efx = container_of(data, struct ef4_nic, mac_work);

	mutex_lock(&efx->mac_lock);
	if (efx->port_enabled)
		ef4_mac_reconfigure(efx);
	mutex_unlock(&efx->mac_lock);
}

static int ef4_probe_port(struct ef4_nic *efx)
{
	int rc;

	netif_dbg(efx, probe, efx->net_dev, "create port\n");

	if (phy_flash_cfg)
		efx->phy_mode = PHY_MODE_SPECIAL;

	/* Connect up MAC/PHY operations table */
	rc = efx->type->probe_port(efx);
	if (rc)
		return rc;

	/* Initialise MAC address to permanent address */
	ether_addr_copy(efx->net_dev->dev_addr, efx->net_dev->perm_addr);

	return 0;
}

static int ef4_init_port(struct ef4_nic *efx)
{
	int rc;

	netif_dbg(efx, drv, efx->net_dev, "init port\n");

	mutex_lock(&efx->mac_lock);

	rc = efx->phy_op->init(efx);
	if (rc)
		goto fail1;

	efx->port_initialized = true;

	/* Reconfigure the MAC before creating dma queues (required for
	 * Falcon/A1 where RX_INGR_EN/TX_DRAIN_EN isn't supported) */
	ef4_mac_reconfigure(efx);

	/* Ensure the PHY advertises the correct flow control settings */
	rc = efx->phy_op->reconfigure(efx);
	if (rc && rc != -EPERM)
		goto fail2;

	mutex_unlock(&efx->mac_lock);
	return 0;

fail2:
	efx->phy_op->fini(efx);
fail1:
	mutex_unlock(&efx->mac_lock);
	return rc;
}

static void ef4_start_port(struct ef4_nic *efx)
{
	netif_dbg(efx, ifup, efx->net_dev, "start port\n");
	BUG_ON(efx->port_enabled);

	mutex_lock(&efx->mac_lock);
	efx->port_enabled = true;

	/* Ensure MAC ingress/egress is enabled */
	ef4_mac_reconfigure(efx);

	mutex_unlock(&efx->mac_lock);
}

/* Cancel work for MAC reconfiguration, periodic hardware monitoring
 * and the async self-test, wait for them to finish and prevent them
 * being scheduled again.  This doesn't cover online resets, which
 * should only be cancelled when removing the device.
 */
static void ef4_stop_port(struct ef4_nic *efx)
{
	netif_dbg(efx, ifdown, efx->net_dev, "stop port\n");

	EF4_ASSERT_RESET_SERIALISED(efx);

	mutex_lock(&efx->mac_lock);
	efx->port_enabled = false;
	mutex_unlock(&efx->mac_lock);

	/* Serialise against ef4_set_multicast_list() */
	netif_addr_lock_bh(efx->net_dev);
	netif_addr_unlock_bh(efx->net_dev);

	cancel_delayed_work_sync(&efx->monitor_work);
	ef4_selftest_async_cancel(efx);
	cancel_work_sync(&efx->mac_work);
}

static void ef4_fini_port(struct ef4_nic *efx)
{
	netif_dbg(efx, drv, efx->net_dev, "shut down port\n");

	if (!efx->port_initialized)
		return;

	efx->phy_op->fini(efx);
	efx->port_initialized = false;

	efx->link_state.up = false;
	ef4_link_status_changed(efx);
}

static void ef4_remove_port(struct ef4_nic *efx)
{
	netif_dbg(efx, drv, efx->net_dev, "destroying port\n");

	efx->type->remove_port(efx);
}

/**************************************************************************
 *
 * NIC handling
 *
 **************************************************************************/

static LIST_HEAD(ef4_primary_list);
static LIST_HEAD(ef4_unassociated_list);

static bool ef4_same_controller(struct ef4_nic *left, struct ef4_nic *right)
{
	return left->type == right->type &&
		left->vpd_sn && right->vpd_sn &&
		!strcmp(left->vpd_sn, right->vpd_sn);
}

static void ef4_associate(struct ef4_nic *efx)
{
	struct ef4_nic *other, *next;

	if (efx->primary == efx) {
		/* Adding primary function; look for secondaries */

		netif_dbg(efx, probe, efx->net_dev, "adding to primary list\n");
		list_add_tail(&efx->node, &ef4_primary_list);

		list_for_each_entry_safe(other, next, &ef4_unassociated_list,
					 node) {
			if (ef4_same_controller(efx, other)) {
				list_del(&other->node);
				netif_dbg(other, probe, other->net_dev,
					  "moving to secondary list of %s %s\n",
					  pci_name(efx->pci_dev),
					  efx->net_dev->name);
				list_add_tail(&other->node,
					      &efx->secondary_list);
				other->primary = efx;
			}
		}
	} else {
		/* Adding secondary function; look for primary */

		list_for_each_entry(other, &ef4_primary_list, node) {
			if (ef4_same_controller(efx, other)) {
				netif_dbg(efx, probe, efx->net_dev,
					  "adding to secondary list of %s %s\n",
					  pci_name(other->pci_dev),
					  other->net_dev->name);
				list_add_tail(&efx->node,
					      &other->secondary_list);
				efx->primary = other;
				return;
			}
		}

		netif_dbg(efx, probe, efx->net_dev,
			  "adding to unassociated list\n");
		list_add_tail(&efx->node, &ef4_unassociated_list);
	}
}

static void ef4_dissociate(struct ef4_nic *efx)
{
	struct ef4_nic *other, *next;

	list_del(&efx->node);
	efx->primary = NULL;

	list_for_each_entry_safe(other, next, &efx->secondary_list, node) {
		list_del(&other->node);
		netif_dbg(other, probe, other->net_dev,
			  "moving to unassociated list\n");
		list_add_tail(&other->node, &ef4_unassociated_list);
		other->primary = NULL;
	}
}

/* This configures the PCI device to enable I/O and DMA. */
static int ef4_init_io(struct ef4_nic *efx)
{
	struct pci_dev *pci_dev = efx->pci_dev;
	dma_addr_t dma_mask = efx->type->max_dma_mask;
	unsigned int mem_map_size = efx->type->mem_map_size(efx);
	int rc, bar;

	netif_dbg(efx, probe, efx->net_dev, "initialising I/O\n");

	bar = efx->type->mem_bar;

	rc = pci_enable_device(pci_dev);
	if (rc) {
		netif_err(efx, probe, efx->net_dev,
			  "failed to enable PCI device\n");
		goto fail1;
	}

	pci_set_master(pci_dev);

	/* Set the PCI DMA mask.  Try all possibilities from our genuine mask
	 * down to 32 bits, because some architectures will allow 40 bit
	 * masks event though they reject 46 bit masks.
	 */
	while (dma_mask > 0x7fffffffUL) {
		rc = dma_set_mask_and_coherent(&pci_dev->dev, dma_mask);
		if (rc == 0)
			break;
		dma_mask >>= 1;
	}
	if (rc) {
		netif_err(efx, probe, efx->net_dev,
			  "could not find a suitable DMA mask\n");
		goto fail2;
	}
	netif_dbg(efx, probe, efx->net_dev,
		  "using DMA mask %llx\n", (unsigned long long) dma_mask);

	efx->membase_phys = pci_resource_start(efx->pci_dev, bar);
	rc = pci_request_region(pci_dev, bar, "sfc");
	if (rc) {
		netif_err(efx, probe, efx->net_dev,
			  "request for memory BAR failed\n");
		rc = -EIO;
		goto fail3;
	}
	efx->membase = ioremap(efx->membase_phys, mem_map_size);
	if (!efx->membase) {
		netif_err(efx, probe, efx->net_dev,
			  "could not map memory BAR at %llx+%x\n",
			  (unsigned long long)efx->membase_phys, mem_map_size);
		rc = -ENOMEM;
		goto fail4;
	}
	netif_dbg(efx, probe, efx->net_dev,
		  "memory BAR at %llx+%x (virtual %p)\n",
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

static void ef4_fini_io(struct ef4_nic *efx)
{
	int bar;

	netif_dbg(efx, drv, efx->net_dev, "shutting down I/O\n");

	if (efx->membase) {
		iounmap(efx->membase);
		efx->membase = NULL;
	}

	if (efx->membase_phys) {
		bar = efx->type->mem_bar;
		pci_release_region(efx->pci_dev, bar);
		efx->membase_phys = 0;
	}

	/* Don't disable bus-mastering if VFs are assigned */
	if (!pci_vfs_assigned(efx->pci_dev))
		pci_disable_device(efx->pci_dev);
}

void ef4_set_default_rx_indir_table(struct ef4_nic *efx)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(efx->rx_indir_table); i++)
		efx->rx_indir_table[i] =
			ethtool_rxfh_indir_default(i, efx->rss_spread);
}

static unsigned int ef4_wanted_parallelism(struct ef4_nic *efx)
{
	cpumask_var_t thread_mask;
	unsigned int count;
	int cpu;

	if (rss_cpus) {
		count = rss_cpus;
	} else {
		if (unlikely(!zalloc_cpumask_var(&thread_mask, GFP_KERNEL))) {
			netif_warn(efx, probe, efx->net_dev,
				   "RSS disabled due to allocation failure\n");
			return 1;
		}

		count = 0;
		for_each_online_cpu(cpu) {
			if (!cpumask_test_cpu(cpu, thread_mask)) {
				++count;
				cpumask_or(thread_mask, thread_mask,
					   topology_sibling_cpumask(cpu));
			}
		}

		free_cpumask_var(thread_mask);
	}

	if (count > EF4_MAX_RX_QUEUES) {
		netif_cond_dbg(efx, probe, efx->net_dev, !rss_cpus, warn,
			       "Reducing number of rx queues from %u to %u.\n",
			       count, EF4_MAX_RX_QUEUES);
		count = EF4_MAX_RX_QUEUES;
	}

	return count;
}

/* Probe the number and type of interrupts we are able to obtain, and
 * the resulting numbers of channels and RX queues.
 */
static int ef4_probe_interrupts(struct ef4_nic *efx)
{
	unsigned int extra_channels = 0;
	unsigned int i, j;
	int rc;

	for (i = 0; i < EF4_MAX_EXTRA_CHANNELS; i++)
		if (efx->extra_channel_type[i])
			++extra_channels;

	if (efx->interrupt_mode == EF4_INT_MODE_MSIX) {
		struct msix_entry xentries[EF4_MAX_CHANNELS];
		unsigned int n_channels;

		n_channels = ef4_wanted_parallelism(efx);
		if (ef4_separate_tx_channels)
			n_channels *= 2;
		n_channels += extra_channels;
		n_channels = min(n_channels, efx->max_channels);

		for (i = 0; i < n_channels; i++)
			xentries[i].entry = i;
		rc = pci_enable_msix_range(efx->pci_dev,
					   xentries, 1, n_channels);
		if (rc < 0) {
			/* Fall back to single channel MSI */
			efx->interrupt_mode = EF4_INT_MODE_MSI;
			netif_err(efx, drv, efx->net_dev,
				  "could not enable MSI-X\n");
		} else if (rc < n_channels) {
			netif_err(efx, drv, efx->net_dev,
				  "WARNING: Insufficient MSI-X vectors"
				  " available (%d < %u).\n", rc, n_channels);
			netif_err(efx, drv, efx->net_dev,
				  "WARNING: Performance may be reduced.\n");
			n_channels = rc;
		}

		if (rc > 0) {
			efx->n_channels = n_channels;
			if (n_channels > extra_channels)
				n_channels -= extra_channels;
			if (ef4_separate_tx_channels) {
				efx->n_tx_channels = min(max(n_channels / 2,
							     1U),
							 efx->max_tx_channels);
				efx->n_rx_channels = max(n_channels -
							 efx->n_tx_channels,
							 1U);
			} else {
				efx->n_tx_channels = min(n_channels,
							 efx->max_tx_channels);
				efx->n_rx_channels = n_channels;
			}
			for (i = 0; i < efx->n_channels; i++)
				ef4_get_channel(efx, i)->irq =
					xentries[i].vector;
		}
	}

	/* Try single interrupt MSI */
	if (efx->interrupt_mode == EF4_INT_MODE_MSI) {
		efx->n_channels = 1;
		efx->n_rx_channels = 1;
		efx->n_tx_channels = 1;
		rc = pci_enable_msi(efx->pci_dev);
		if (rc == 0) {
			ef4_get_channel(efx, 0)->irq = efx->pci_dev->irq;
		} else {
			netif_err(efx, drv, efx->net_dev,
				  "could not enable MSI\n");
			efx->interrupt_mode = EF4_INT_MODE_LEGACY;
		}
	}

	/* Assume legacy interrupts */
	if (efx->interrupt_mode == EF4_INT_MODE_LEGACY) {
		efx->n_channels = 1 + (ef4_separate_tx_channels ? 1 : 0);
		efx->n_rx_channels = 1;
		efx->n_tx_channels = 1;
		efx->legacy_irq = efx->pci_dev->irq;
	}

	/* Assign extra channels if possible */
	j = efx->n_channels;
	for (i = 0; i < EF4_MAX_EXTRA_CHANNELS; i++) {
		if (!efx->extra_channel_type[i])
			continue;
		if (efx->interrupt_mode != EF4_INT_MODE_MSIX ||
		    efx->n_channels <= extra_channels) {
			efx->extra_channel_type[i]->handle_no_channel(efx);
		} else {
			--j;
			ef4_get_channel(efx, j)->type =
				efx->extra_channel_type[i];
		}
	}

	efx->rss_spread = efx->n_rx_channels;

	return 0;
}

static int ef4_soft_enable_interrupts(struct ef4_nic *efx)
{
	struct ef4_channel *channel, *end_channel;
	int rc;

	BUG_ON(efx->state == STATE_DISABLED);

	efx->irq_soft_enabled = true;
	smp_wmb();

	ef4_for_each_channel(channel, efx) {
		if (!channel->type->keep_eventq) {
			rc = ef4_init_eventq(channel);
			if (rc)
				goto fail;
		}
		ef4_start_eventq(channel);
	}

	return 0;
fail:
	end_channel = channel;
	ef4_for_each_channel(channel, efx) {
		if (channel == end_channel)
			break;
		ef4_stop_eventq(channel);
		if (!channel->type->keep_eventq)
			ef4_fini_eventq(channel);
	}

	return rc;
}

static void ef4_soft_disable_interrupts(struct ef4_nic *efx)
{
	struct ef4_channel *channel;

	if (efx->state == STATE_DISABLED)
		return;

	efx->irq_soft_enabled = false;
	smp_wmb();

	if (efx->legacy_irq)
		synchronize_irq(efx->legacy_irq);

	ef4_for_each_channel(channel, efx) {
		if (channel->irq)
			synchronize_irq(channel->irq);

		ef4_stop_eventq(channel);
		if (!channel->type->keep_eventq)
			ef4_fini_eventq(channel);
	}
}

static int ef4_enable_interrupts(struct ef4_nic *efx)
{
	struct ef4_channel *channel, *end_channel;
	int rc;

	BUG_ON(efx->state == STATE_DISABLED);

	if (efx->eeh_disabled_legacy_irq) {
		enable_irq(efx->legacy_irq);
		efx->eeh_disabled_legacy_irq = false;
	}

	efx->type->irq_enable_master(efx);

	ef4_for_each_channel(channel, efx) {
		if (channel->type->keep_eventq) {
			rc = ef4_init_eventq(channel);
			if (rc)
				goto fail;
		}
	}

	rc = ef4_soft_enable_interrupts(efx);
	if (rc)
		goto fail;

	return 0;

fail:
	end_channel = channel;
	ef4_for_each_channel(channel, efx) {
		if (channel == end_channel)
			break;
		if (channel->type->keep_eventq)
			ef4_fini_eventq(channel);
	}

	efx->type->irq_disable_non_ev(efx);

	return rc;
}

static void ef4_disable_interrupts(struct ef4_nic *efx)
{
	struct ef4_channel *channel;

	ef4_soft_disable_interrupts(efx);

	ef4_for_each_channel(channel, efx) {
		if (channel->type->keep_eventq)
			ef4_fini_eventq(channel);
	}

	efx->type->irq_disable_non_ev(efx);
}

static void ef4_remove_interrupts(struct ef4_nic *efx)
{
	struct ef4_channel *channel;

	/* Remove MSI/MSI-X interrupts */
	ef4_for_each_channel(channel, efx)
		channel->irq = 0;
	pci_disable_msi(efx->pci_dev);
	pci_disable_msix(efx->pci_dev);

	/* Remove legacy interrupt */
	efx->legacy_irq = 0;
}

static void ef4_set_channels(struct ef4_nic *efx)
{
	struct ef4_channel *channel;
	struct ef4_tx_queue *tx_queue;

	efx->tx_channel_offset =
		ef4_separate_tx_channels ?
		efx->n_channels - efx->n_tx_channels : 0;

	/* We need to mark which channels really have RX and TX
	 * queues, and adjust the TX queue numbers if we have separate
	 * RX-only and TX-only channels.
	 */
	ef4_for_each_channel(channel, efx) {
		if (channel->channel < efx->n_rx_channels)
			channel->rx_queue.core_index = channel->channel;
		else
			channel->rx_queue.core_index = -1;

		ef4_for_each_channel_tx_queue(tx_queue, channel)
			tx_queue->queue -= (efx->tx_channel_offset *
					    EF4_TXQ_TYPES);
	}
}

static int ef4_probe_nic(struct ef4_nic *efx)
{
	int rc;

	netif_dbg(efx, probe, efx->net_dev, "creating NIC\n");

	/* Carry out hardware-type specific initialisation */
	rc = efx->type->probe(efx);
	if (rc)
		return rc;

	do {
		if (!efx->max_channels || !efx->max_tx_channels) {
			netif_err(efx, drv, efx->net_dev,
				  "Insufficient resources to allocate"
				  " any channels\n");
			rc = -ENOSPC;
			goto fail1;
		}

		/* Determine the number of channels and queues by trying
		 * to hook in MSI-X interrupts.
		 */
		rc = ef4_probe_interrupts(efx);
		if (rc)
			goto fail1;

		ef4_set_channels(efx);

		/* dimension_resources can fail with EAGAIN */
		rc = efx->type->dimension_resources(efx);
		if (rc != 0 && rc != -EAGAIN)
			goto fail2;

		if (rc == -EAGAIN)
			/* try again with new max_channels */
			ef4_remove_interrupts(efx);

	} while (rc == -EAGAIN);

	if (efx->n_channels > 1)
		netdev_rss_key_fill(&efx->rx_hash_key,
				    sizeof(efx->rx_hash_key));
	ef4_set_default_rx_indir_table(efx);

	netif_set_real_num_tx_queues(efx->net_dev, efx->n_tx_channels);
	netif_set_real_num_rx_queues(efx->net_dev, efx->n_rx_channels);

	/* Initialise the interrupt moderation settings */
	efx->irq_mod_step_us = DIV_ROUND_UP(efx->timer_quantum_ns, 1000);
	ef4_init_irq_moderation(efx, tx_irq_mod_usec, rx_irq_mod_usec, true,
				true);

	return 0;

fail2:
	ef4_remove_interrupts(efx);
fail1:
	efx->type->remove(efx);
	return rc;
}

static void ef4_remove_nic(struct ef4_nic *efx)
{
	netif_dbg(efx, drv, efx->net_dev, "destroying NIC\n");

	ef4_remove_interrupts(efx);
	efx->type->remove(efx);
}

static int ef4_probe_filters(struct ef4_nic *efx)
{
	int rc;

	spin_lock_init(&efx->filter_lock);
	init_rwsem(&efx->filter_sem);
	mutex_lock(&efx->mac_lock);
	down_write(&efx->filter_sem);
	rc = efx->type->filter_table_probe(efx);
	if (rc)
		goto out_unlock;

#ifdef CONFIG_RFS_ACCEL
	if (efx->type->offload_features & NETIF_F_NTUPLE) {
		struct ef4_channel *channel;
		int i, success = 1;

		ef4_for_each_channel(channel, efx) {
			channel->rps_flow_id =
				kcalloc(efx->type->max_rx_ip_filters,
					sizeof(*channel->rps_flow_id),
					GFP_KERNEL);
			if (!channel->rps_flow_id)
				success = 0;
			else
				for (i = 0;
				     i < efx->type->max_rx_ip_filters;
				     ++i)
					channel->rps_flow_id[i] =
						RPS_FLOW_ID_INVALID;
		}

		if (!success) {
			ef4_for_each_channel(channel, efx)
				kfree(channel->rps_flow_id);
			efx->type->filter_table_remove(efx);
			rc = -ENOMEM;
			goto out_unlock;
		}

		efx->rps_expire_index = efx->rps_expire_channel = 0;
	}
#endif
out_unlock:
	up_write(&efx->filter_sem);
	mutex_unlock(&efx->mac_lock);
	return rc;
}

static void ef4_remove_filters(struct ef4_nic *efx)
{
#ifdef CONFIG_RFS_ACCEL
	struct ef4_channel *channel;

	ef4_for_each_channel(channel, efx)
		kfree(channel->rps_flow_id);
#endif
	down_write(&efx->filter_sem);
	efx->type->filter_table_remove(efx);
	up_write(&efx->filter_sem);
}

static void ef4_restore_filters(struct ef4_nic *efx)
{
	down_read(&efx->filter_sem);
	efx->type->filter_table_restore(efx);
	up_read(&efx->filter_sem);
}

/**************************************************************************
 *
 * NIC startup/shutdown
 *
 *************************************************************************/

static int ef4_probe_all(struct ef4_nic *efx)
{
	int rc;

	rc = ef4_probe_nic(efx);
	if (rc) {
		netif_err(efx, probe, efx->net_dev, "failed to create NIC\n");
		goto fail1;
	}

	rc = ef4_probe_port(efx);
	if (rc) {
		netif_err(efx, probe, efx->net_dev, "failed to create port\n");
		goto fail2;
	}

	BUILD_BUG_ON(EF4_DEFAULT_DMAQ_SIZE < EF4_RXQ_MIN_ENT);
	if (WARN_ON(EF4_DEFAULT_DMAQ_SIZE < EF4_TXQ_MIN_ENT(efx))) {
		rc = -EINVAL;
		goto fail3;
	}
	efx->rxq_entries = efx->txq_entries = EF4_DEFAULT_DMAQ_SIZE;

	rc = ef4_probe_filters(efx);
	if (rc) {
		netif_err(efx, probe, efx->net_dev,
			  "failed to create filter tables\n");
		goto fail4;
	}

	rc = ef4_probe_channels(efx);
	if (rc)
		goto fail5;

	return 0;

 fail5:
	ef4_remove_filters(efx);
 fail4:
 fail3:
	ef4_remove_port(efx);
 fail2:
	ef4_remove_nic(efx);
 fail1:
	return rc;
}

/* If the interface is supposed to be running but is not, start
 * the hardware and software data path, regular activity for the port
 * (MAC statistics, link polling, etc.) and schedule the port to be
 * reconfigured.  Interrupts must already be enabled.  This function
 * is safe to call multiple times, so long as the NIC is not disabled.
 * Requires the RTNL lock.
 */
static void ef4_start_all(struct ef4_nic *efx)
{
	EF4_ASSERT_RESET_SERIALISED(efx);
	BUG_ON(efx->state == STATE_DISABLED);

	/* Check that it is appropriate to restart the interface. All
	 * of these flags are safe to read under just the rtnl lock */
	if (efx->port_enabled || !netif_running(efx->net_dev) ||
	    efx->reset_pending)
		return;

	ef4_start_port(efx);
	ef4_start_datapath(efx);

	/* Start the hardware monitor if there is one */
	if (efx->type->monitor != NULL)
		queue_delayed_work(efx->workqueue, &efx->monitor_work,
				   ef4_monitor_interval);

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
static void ef4_stop_all(struct ef4_nic *efx)
{
	EF4_ASSERT_RESET_SERIALISED(efx);

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
	ef4_stop_port(efx);

	/* Stop the kernel transmit interface.  This is only valid if
	 * the device is stopped or detached; otherwise the watchdog
	 * may fire immediately.
	 */
	WARN_ON(netif_running(efx->net_dev) &&
		netif_device_present(efx->net_dev));
	netif_tx_disable(efx->net_dev);

	ef4_stop_datapath(efx);
}

static void ef4_remove_all(struct ef4_nic *efx)
{
	ef4_remove_channels(efx);
	ef4_remove_filters(efx);
	ef4_remove_port(efx);
	ef4_remove_nic(efx);
}

/**************************************************************************
 *
 * Interrupt moderation
 *
 **************************************************************************/
unsigned int ef4_usecs_to_ticks(struct ef4_nic *efx, unsigned int usecs)
{
	if (usecs == 0)
		return 0;
	if (usecs * 1000 < efx->timer_quantum_ns)
		return 1; /* never round down to 0 */
	return usecs * 1000 / efx->timer_quantum_ns;
}

unsigned int ef4_ticks_to_usecs(struct ef4_nic *efx, unsigned int ticks)
{
	/* We must round up when converting ticks to microseconds
	 * because we round down when converting the other way.
	 */
	return DIV_ROUND_UP(ticks * efx->timer_quantum_ns, 1000);
}

/* Set interrupt moderation parameters */
int ef4_init_irq_moderation(struct ef4_nic *efx, unsigned int tx_usecs,
			    unsigned int rx_usecs, bool rx_adaptive,
			    bool rx_may_override_tx)
{
	struct ef4_channel *channel;
	unsigned int timer_max_us;

	EF4_ASSERT_RESET_SERIALISED(efx);

	timer_max_us = efx->timer_max_ns / 1000;

	if (tx_usecs > timer_max_us || rx_usecs > timer_max_us)
		return -EINVAL;

	if (tx_usecs != rx_usecs && efx->tx_channel_offset == 0 &&
	    !rx_may_override_tx) {
		netif_err(efx, drv, efx->net_dev, "Channels are shared. "
			  "RX and TX IRQ moderation must be equal\n");
		return -EINVAL;
	}

	efx->irq_rx_adaptive = rx_adaptive;
	efx->irq_rx_moderation_us = rx_usecs;
	ef4_for_each_channel(channel, efx) {
		if (ef4_channel_has_rx_queue(channel))
			channel->irq_moderation_us = rx_usecs;
		else if (ef4_channel_has_tx_queues(channel))
			channel->irq_moderation_us = tx_usecs;
	}

	return 0;
}

void ef4_get_irq_moderation(struct ef4_nic *efx, unsigned int *tx_usecs,
			    unsigned int *rx_usecs, bool *rx_adaptive)
{
	*rx_adaptive = efx->irq_rx_adaptive;
	*rx_usecs = efx->irq_rx_moderation_us;

	/* If channels are shared between RX and TX, so is IRQ
	 * moderation.  Otherwise, IRQ moderation is the same for all
	 * TX channels and is not adaptive.
	 */
	if (efx->tx_channel_offset == 0) {
		*tx_usecs = *rx_usecs;
	} else {
		struct ef4_channel *tx_channel;

		tx_channel = efx->channel[efx->tx_channel_offset];
		*tx_usecs = tx_channel->irq_moderation_us;
	}
}

/**************************************************************************
 *
 * Hardware monitor
 *
 **************************************************************************/

/* Run periodically off the general workqueue */
static void ef4_monitor(struct work_struct *data)
{
	struct ef4_nic *efx = container_of(data, struct ef4_nic,
					   monitor_work.work);

	netif_vdbg(efx, timer, efx->net_dev,
		   "hardware monitor executing on CPU %d\n",
		   raw_smp_processor_id());
	BUG_ON(efx->type->monitor == NULL);

	/* If the mac_lock is already held then it is likely a port
	 * reconfiguration is already in place, which will likely do
	 * most of the work of monitor() anyway. */
	if (mutex_trylock(&efx->mac_lock)) {
		if (efx->port_enabled)
			efx->type->monitor(efx);
		mutex_unlock(&efx->mac_lock);
	}

	queue_delayed_work(efx->workqueue, &efx->monitor_work,
			   ef4_monitor_interval);
}

/**************************************************************************
 *
 * ioctls
 *
 *************************************************************************/

/* Net device ioctl
 * Context: process, rtnl_lock() held.
 */
static int ef4_ioctl(struct net_device *net_dev, struct ifreq *ifr, int cmd)
{
	struct ef4_nic *efx = netdev_priv(net_dev);
	struct mii_ioctl_data *data = if_mii(ifr);

	/* Convert phy_id from older PRTAD/DEVAD format */
	if ((cmd == SIOCGMIIREG || cmd == SIOCSMIIREG) &&
	    (data->phy_id & 0xfc00) == 0x0400)
		data->phy_id ^= MDIO_PHY_ID_C45 | 0x0400;

	return mdio_mii_ioctl(&efx->mdio, data, cmd);
}

/**************************************************************************
 *
 * NAPI interface
 *
 **************************************************************************/

static void ef4_init_napi_channel(struct ef4_channel *channel)
{
	struct ef4_nic *efx = channel->efx;

	channel->napi_dev = efx->net_dev;
	netif_napi_add(channel->napi_dev, &channel->napi_str,
		       ef4_poll, napi_weight);
}

static void ef4_init_napi(struct ef4_nic *efx)
{
	struct ef4_channel *channel;

	ef4_for_each_channel(channel, efx)
		ef4_init_napi_channel(channel);
}

static void ef4_fini_napi_channel(struct ef4_channel *channel)
{
	if (channel->napi_dev)
		netif_napi_del(&channel->napi_str);

	channel->napi_dev = NULL;
}

static void ef4_fini_napi(struct ef4_nic *efx)
{
	struct ef4_channel *channel;

	ef4_for_each_channel(channel, efx)
		ef4_fini_napi_channel(channel);
}

/**************************************************************************
 *
 * Kernel net device interface
 *
 *************************************************************************/

/* Context: process, rtnl_lock() held. */
int ef4_net_open(struct net_device *net_dev)
{
	struct ef4_nic *efx = netdev_priv(net_dev);
	int rc;

	netif_dbg(efx, ifup, efx->net_dev, "opening device on CPU %d\n",
		  raw_smp_processor_id());

	rc = ef4_check_disabled(efx);
	if (rc)
		return rc;
	if (efx->phy_mode & PHY_MODE_SPECIAL)
		return -EBUSY;

	/* Notify the kernel of the link state polled during driver load,
	 * before the monitor starts running */
	ef4_link_status_changed(efx);

	ef4_start_all(efx);
	ef4_selftest_async_start(efx);
	return 0;
}

/* Context: process, rtnl_lock() held.
 * Note that the kernel will ignore our return code; this method
 * should really be a void.
 */
int ef4_net_stop(struct net_device *net_dev)
{
	struct ef4_nic *efx = netdev_priv(net_dev);

	netif_dbg(efx, ifdown, efx->net_dev, "closing on CPU %d\n",
		  raw_smp_processor_id());

	/* Stop the device and flush all the channels */
	ef4_stop_all(efx);

	return 0;
}

/* Context: process, dev_base_lock or RTNL held, non-blocking. */
static void ef4_net_stats(struct net_device *net_dev,
			  struct rtnl_link_stats64 *stats)
{
	struct ef4_nic *efx = netdev_priv(net_dev);

	spin_lock_bh(&efx->stats_lock);
	efx->type->update_stats(efx, NULL, stats);
	spin_unlock_bh(&efx->stats_lock);
}

/* Context: netif_tx_lock held, BHs disabled. */
static void ef4_watchdog(struct net_device *net_dev, unsigned int txqueue)
{
	struct ef4_nic *efx = netdev_priv(net_dev);

	netif_err(efx, tx_err, efx->net_dev,
		  "TX stuck with port_enabled=%d: resetting channels\n",
		  efx->port_enabled);

	ef4_schedule_reset(efx, RESET_TYPE_TX_WATCHDOG);
}


/* Context: process, rtnl_lock() held. */
static int ef4_change_mtu(struct net_device *net_dev, int new_mtu)
{
	struct ef4_nic *efx = netdev_priv(net_dev);
	int rc;

	rc = ef4_check_disabled(efx);
	if (rc)
		return rc;

	netif_dbg(efx, drv, efx->net_dev, "changing MTU to %d\n", new_mtu);

	ef4_device_detach_sync(efx);
	ef4_stop_all(efx);

	mutex_lock(&efx->mac_lock);
	net_dev->mtu = new_mtu;
	ef4_mac_reconfigure(efx);
	mutex_unlock(&efx->mac_lock);

	ef4_start_all(efx);
	netif_device_attach(efx->net_dev);
	return 0;
}

static int ef4_set_mac_address(struct net_device *net_dev, void *data)
{
	struct ef4_nic *efx = netdev_priv(net_dev);
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
	ether_addr_copy(net_dev->dev_addr, new_addr);
	if (efx->type->set_mac_address) {
		rc = efx->type->set_mac_address(efx);
		if (rc) {
			ether_addr_copy(net_dev->dev_addr, old_addr);
			return rc;
		}
	}

	/* Reconfigure the MAC */
	mutex_lock(&efx->mac_lock);
	ef4_mac_reconfigure(efx);
	mutex_unlock(&efx->mac_lock);

	return 0;
}

/* Context: netif_addr_lock held, BHs disabled. */
static void ef4_set_rx_mode(struct net_device *net_dev)
{
	struct ef4_nic *efx = netdev_priv(net_dev);

	if (efx->port_enabled)
		queue_work(efx->workqueue, &efx->mac_work);
	/* Otherwise ef4_start_port() will do this */
}

static int ef4_set_features(struct net_device *net_dev, netdev_features_t data)
{
	struct ef4_nic *efx = netdev_priv(net_dev);
	int rc;

	/* If disabling RX n-tuple filtering, clear existing filters */
	if (net_dev->features & ~data & NETIF_F_NTUPLE) {
		rc = efx->type->filter_clear_rx(efx, EF4_FILTER_PRI_MANUAL);
		if (rc)
			return rc;
	}

	/* If Rx VLAN filter is changed, update filters via mac_reconfigure */
	if ((net_dev->features ^ data) & NETIF_F_HW_VLAN_CTAG_FILTER) {
		/* ef4_set_rx_mode() will schedule MAC work to update filters
		 * when a new features are finally set in net_dev.
		 */
		ef4_set_rx_mode(net_dev);
	}

	return 0;
}

static const struct net_device_ops ef4_netdev_ops = {
	.ndo_open		= ef4_net_open,
	.ndo_stop		= ef4_net_stop,
	.ndo_get_stats64	= ef4_net_stats,
	.ndo_tx_timeout		= ef4_watchdog,
	.ndo_start_xmit		= ef4_hard_start_xmit,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_do_ioctl		= ef4_ioctl,
	.ndo_change_mtu		= ef4_change_mtu,
	.ndo_set_mac_address	= ef4_set_mac_address,
	.ndo_set_rx_mode	= ef4_set_rx_mode,
	.ndo_set_features	= ef4_set_features,
	.ndo_setup_tc		= ef4_setup_tc,
#ifdef CONFIG_RFS_ACCEL
	.ndo_rx_flow_steer	= ef4_filter_rfs,
#endif
};

static void ef4_update_name(struct ef4_nic *efx)
{
	strcpy(efx->name, efx->net_dev->name);
	ef4_mtd_rename(efx);
	ef4_set_channel_names(efx);
}

static int ef4_netdev_event(struct notifier_block *this,
			    unsigned long event, void *ptr)
{
	struct net_device *net_dev = netdev_notifier_info_to_dev(ptr);

	if ((net_dev->netdev_ops == &ef4_netdev_ops) &&
	    event == NETDEV_CHANGENAME)
		ef4_update_name(netdev_priv(net_dev));

	return NOTIFY_DONE;
}

static struct notifier_block ef4_netdev_notifier = {
	.notifier_call = ef4_netdev_event,
};

static ssize_t
show_phy_type(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ef4_nic *efx = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", efx->phy_type);
}
static DEVICE_ATTR(phy_type, 0444, show_phy_type, NULL);

static int ef4_register_netdev(struct ef4_nic *efx)
{
	struct net_device *net_dev = efx->net_dev;
	struct ef4_channel *channel;
	int rc;

	net_dev->watchdog_timeo = 5 * HZ;
	net_dev->irq = efx->pci_dev->irq;
	net_dev->netdev_ops = &ef4_netdev_ops;
	net_dev->ethtool_ops = &ef4_ethtool_ops;
	net_dev->gso_max_segs = EF4_TSO_MAX_SEGS;
	net_dev->min_mtu = EF4_MIN_MTU;
	net_dev->max_mtu = EF4_MAX_MTU;

	rtnl_lock();

	/* Enable resets to be scheduled and check whether any were
	 * already requested.  If so, the NIC is probably hosed so we
	 * abort.
	 */
	efx->state = STATE_READY;
	smp_mb(); /* ensure we change state before checking reset_pending */
	if (efx->reset_pending) {
		netif_err(efx, probe, efx->net_dev,
			  "aborting probe due to scheduled reset\n");
		rc = -EIO;
		goto fail_locked;
	}

	rc = dev_alloc_name(net_dev, net_dev->name);
	if (rc < 0)
		goto fail_locked;
	ef4_update_name(efx);

	/* Always start with carrier off; PHY events will detect the link */
	netif_carrier_off(net_dev);

	rc = register_netdevice(net_dev);
	if (rc)
		goto fail_locked;

	ef4_for_each_channel(channel, efx) {
		struct ef4_tx_queue *tx_queue;
		ef4_for_each_channel_tx_queue(tx_queue, channel)
			ef4_init_tx_queue_core_txq(tx_queue);
	}

	ef4_associate(efx);

	rtnl_unlock();

	rc = device_create_file(&efx->pci_dev->dev, &dev_attr_phy_type);
	if (rc) {
		netif_err(efx, drv, efx->net_dev,
			  "failed to init net dev attributes\n");
		goto fail_registered;
	}
	return 0;

fail_registered:
	rtnl_lock();
	ef4_dissociate(efx);
	unregister_netdevice(net_dev);
fail_locked:
	efx->state = STATE_UNINIT;
	rtnl_unlock();
	netif_err(efx, drv, efx->net_dev, "could not register net dev\n");
	return rc;
}

static void ef4_unregister_netdev(struct ef4_nic *efx)
{
	if (!efx->net_dev)
		return;

	BUG_ON(netdev_priv(efx->net_dev) != efx);

	if (ef4_dev_registered(efx)) {
		strlcpy(efx->name, pci_name(efx->pci_dev), sizeof(efx->name));
		device_remove_file(&efx->pci_dev->dev, &dev_attr_phy_type);
		unregister_netdev(efx->net_dev);
	}
}

/**************************************************************************
 *
 * Device reset and suspend
 *
 **************************************************************************/

/* Tears down the entire software state and most of the hardware state
 * before reset.  */
void ef4_reset_down(struct ef4_nic *efx, enum reset_type method)
{
	EF4_ASSERT_RESET_SERIALISED(efx);

	ef4_stop_all(efx);
	ef4_disable_interrupts(efx);

	mutex_lock(&efx->mac_lock);
	if (efx->port_initialized && method != RESET_TYPE_INVISIBLE &&
	    method != RESET_TYPE_DATAPATH)
		efx->phy_op->fini(efx);
	efx->type->fini(efx);
}

/* This function will always ensure that the locks acquired in
 * ef4_reset_down() are released. A failure return code indicates
 * that we were unable to reinitialise the hardware, and the
 * driver should be disabled. If ok is false, then the rx and tx
 * engines are not restarted, pending a RESET_DISABLE. */
int ef4_reset_up(struct ef4_nic *efx, enum reset_type method, bool ok)
{
	int rc;

	EF4_ASSERT_RESET_SERIALISED(efx);

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
		rc = efx->phy_op->init(efx);
		if (rc)
			goto fail;
		rc = efx->phy_op->reconfigure(efx);
		if (rc && rc != -EPERM)
			netif_err(efx, drv, efx->net_dev,
				  "could not restore PHY settings\n");
	}

	rc = ef4_enable_interrupts(efx);
	if (rc)
		goto fail;

	down_read(&efx->filter_sem);
	ef4_restore_filters(efx);
	up_read(&efx->filter_sem);

	mutex_unlock(&efx->mac_lock);

	ef4_start_all(efx);

	return 0;

fail:
	efx->port_initialized = false;

	mutex_unlock(&efx->mac_lock);

	return rc;
}

/* Reset the NIC using the specified method.  Note that the reset may
 * fail, in which case the card will be left in an unusable state.
 *
 * Caller must hold the rtnl_lock.
 */
int ef4_reset(struct ef4_nic *efx, enum reset_type method)
{
	int rc, rc2;
	bool disabled;

	netif_info(efx, drv, efx->net_dev, "resetting (%s)\n",
		   RESET_TYPE(method));

	ef4_device_detach_sync(efx);
	ef4_reset_down(efx, method);

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
	 * can respond to requests. */
	pci_set_master(efx->pci_dev);

out:
	/* Leave device stopped if necessary */
	disabled = rc ||
		method == RESET_TYPE_DISABLE ||
		method == RESET_TYPE_RECOVER_OR_DISABLE;
	rc2 = ef4_reset_up(efx, method, !disabled);
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
		netif_device_attach(efx->net_dev);
	}
	return rc;
}

/* Try recovery mechanisms.
 * For now only EEH is supported.
 * Returns 0 if the recovery mechanisms are unsuccessful.
 * Returns a non-zero value otherwise.
 */
int ef4_try_recovery(struct ef4_nic *efx)
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

/* The worker thread exists so that code that cannot sleep can
 * schedule a reset for later.
 */
static void ef4_reset_work(struct work_struct *data)
{
	struct ef4_nic *efx = container_of(data, struct ef4_nic, reset_work);
	unsigned long pending;
	enum reset_type method;

	pending = READ_ONCE(efx->reset_pending);
	method = fls(pending) - 1;

	if ((method == RESET_TYPE_RECOVER_OR_DISABLE ||
	     method == RESET_TYPE_RECOVER_OR_ALL) &&
	    ef4_try_recovery(efx))
		return;

	if (!pending)
		return;

	rtnl_lock();

	/* We checked the state in ef4_schedule_reset() but it may
	 * have changed by now.  Now that we have the RTNL lock,
	 * it cannot change again.
	 */
	if (efx->state == STATE_READY)
		(void)ef4_reset(efx, method);

	rtnl_unlock();
}

void ef4_schedule_reset(struct ef4_nic *efx, enum reset_type type)
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

	queue_work(reset_workqueue, &efx->reset_work);
}

/**************************************************************************
 *
 * List of NICs we support
 *
 **************************************************************************/

/* PCI device ID table */
static const struct pci_device_id ef4_pci_table[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_SOLARFLARE,
		    PCI_DEVICE_ID_SOLARFLARE_SFC4000A_0),
	 .driver_data = (unsigned long) &falcon_a1_nic_type},
	{PCI_DEVICE(PCI_VENDOR_ID_SOLARFLARE,
		    PCI_DEVICE_ID_SOLARFLARE_SFC4000B),
	 .driver_data = (unsigned long) &falcon_b0_nic_type},
	{0}			/* end of list */
};

/**************************************************************************
 *
 * Dummy PHY/MAC operations
 *
 * Can be used for some unimplemented operations
 * Needed so all function pointers are valid and do not have to be tested
 * before use
 *
 **************************************************************************/
int ef4_port_dummy_op_int(struct ef4_nic *efx)
{
	return 0;
}
void ef4_port_dummy_op_void(struct ef4_nic *efx) {}

static bool ef4_port_dummy_op_poll(struct ef4_nic *efx)
{
	return false;
}

static const struct ef4_phy_operations ef4_dummy_phy_operations = {
	.init		 = ef4_port_dummy_op_int,
	.reconfigure	 = ef4_port_dummy_op_int,
	.poll		 = ef4_port_dummy_op_poll,
	.fini		 = ef4_port_dummy_op_void,
};

/**************************************************************************
 *
 * Data housekeeping
 *
 **************************************************************************/

/* This zeroes out and then fills in the invariants in a struct
 * ef4_nic (including all sub-structures).
 */
static int ef4_init_struct(struct ef4_nic *efx,
			   struct pci_dev *pci_dev, struct net_device *net_dev)
{
	int i;

	/* Initialise common structures */
	INIT_LIST_HEAD(&efx->node);
	INIT_LIST_HEAD(&efx->secondary_list);
	spin_lock_init(&efx->biu_lock);
#ifdef CONFIG_SFC_FALCON_MTD
	INIT_LIST_HEAD(&efx->mtd_list);
#endif
	INIT_WORK(&efx->reset_work, ef4_reset_work);
	INIT_DELAYED_WORK(&efx->monitor_work, ef4_monitor);
	INIT_DELAYED_WORK(&efx->selftest_work, ef4_selftest_async_work);
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
	spin_lock_init(&efx->stats_lock);
	mutex_init(&efx->mac_lock);
	efx->phy_op = &ef4_dummy_phy_operations;
	efx->mdio.dev = net_dev;
	INIT_WORK(&efx->mac_work, ef4_mac_work);
	init_waitqueue_head(&efx->flush_wq);

	for (i = 0; i < EF4_MAX_CHANNELS; i++) {
		efx->channel[i] = ef4_alloc_channel(efx, i, NULL);
		if (!efx->channel[i])
			goto fail;
		efx->msi_context[i].efx = efx;
		efx->msi_context[i].index = i;
	}

	/* Higher numbered interrupt modes are less capable! */
	efx->interrupt_mode = max(efx->type->max_interrupt_mode,
				  interrupt_mode);

	/* Would be good to use the net_dev name, but we're too early */
	snprintf(efx->workqueue_name, sizeof(efx->workqueue_name), "sfc%s",
		 pci_name(pci_dev));
	efx->workqueue = create_singlethread_workqueue(efx->workqueue_name);
	if (!efx->workqueue)
		goto fail;

	return 0;

fail:
	ef4_fini_struct(efx);
	return -ENOMEM;
}

static void ef4_fini_struct(struct ef4_nic *efx)
{
	int i;

	for (i = 0; i < EF4_MAX_CHANNELS; i++)
		kfree(efx->channel[i]);

	kfree(efx->vpd_sn);

	if (efx->workqueue) {
		destroy_workqueue(efx->workqueue);
		efx->workqueue = NULL;
	}
}

void ef4_update_sw_stats(struct ef4_nic *efx, u64 *stats)
{
	u64 n_rx_nodesc_trunc = 0;
	struct ef4_channel *channel;

	ef4_for_each_channel(channel, efx)
		n_rx_nodesc_trunc += channel->n_rx_nodesc_trunc;
	stats[GENERIC_STAT_rx_nodesc_trunc] = n_rx_nodesc_trunc;
	stats[GENERIC_STAT_rx_noskb_drops] = atomic_read(&efx->n_rx_noskb_drops);
}

/**************************************************************************
 *
 * PCI interface
 *
 **************************************************************************/

/* Main body of final NIC shutdown code
 * This is called only at module unload (or hotplug removal).
 */
static void ef4_pci_remove_main(struct ef4_nic *efx)
{
	/* Flush reset_work. It can no longer be scheduled since we
	 * are not READY.
	 */
	BUG_ON(efx->state == STATE_READY);
	cancel_work_sync(&efx->reset_work);

	ef4_disable_interrupts(efx);
	ef4_nic_fini_interrupt(efx);
	ef4_fini_port(efx);
	efx->type->fini(efx);
	ef4_fini_napi(efx);
	ef4_remove_all(efx);
}

/* Final NIC shutdown
 * This is called only at module unload (or hotplug removal).  A PF can call
 * this on its VFs to ensure they are unbound first.
 */
static void ef4_pci_remove(struct pci_dev *pci_dev)
{
	struct ef4_nic *efx;

	efx = pci_get_drvdata(pci_dev);
	if (!efx)
		return;

	/* Mark the NIC as fini, then stop the interface */
	rtnl_lock();
	ef4_dissociate(efx);
	dev_close(efx->net_dev);
	ef4_disable_interrupts(efx);
	efx->state = STATE_UNINIT;
	rtnl_unlock();

	ef4_unregister_netdev(efx);

	ef4_mtd_remove(efx);

	ef4_pci_remove_main(efx);

	ef4_fini_io(efx);
	netif_dbg(efx, drv, efx->net_dev, "shutdown successful\n");

	ef4_fini_struct(efx);
	free_netdev(efx->net_dev);

	pci_disable_pcie_error_reporting(pci_dev);
};

/* NIC VPD information
 * Called during probe to display the part number of the
 * installed NIC.  VPD is potentially very large but this should
 * always appear within the first 512 bytes.
 */
#define SFC_VPD_LEN 512
static void ef4_probe_vpd_strings(struct ef4_nic *efx)
{
	struct pci_dev *dev = efx->pci_dev;
	char vpd_data[SFC_VPD_LEN];
	ssize_t vpd_size;
	int ro_start, ro_size, i, j;

	/* Get the vpd data from the device */
	vpd_size = pci_read_vpd(dev, 0, sizeof(vpd_data), vpd_data);
	if (vpd_size <= 0) {
		netif_err(efx, drv, efx->net_dev, "Unable to read VPD\n");
		return;
	}

	/* Get the Read only section */
	ro_start = pci_vpd_find_tag(vpd_data, 0, vpd_size, PCI_VPD_LRDT_RO_DATA);
	if (ro_start < 0) {
		netif_err(efx, drv, efx->net_dev, "VPD Read-only not found\n");
		return;
	}

	ro_size = pci_vpd_lrdt_size(&vpd_data[ro_start]);
	j = ro_size;
	i = ro_start + PCI_VPD_LRDT_TAG_SIZE;
	if (i + j > vpd_size)
		j = vpd_size - i;

	/* Get the Part number */
	i = pci_vpd_find_info_keyword(vpd_data, i, j, "PN");
	if (i < 0) {
		netif_err(efx, drv, efx->net_dev, "Part number not found\n");
		return;
	}

	j = pci_vpd_info_field_size(&vpd_data[i]);
	i += PCI_VPD_INFO_FLD_HDR_SIZE;
	if (i + j > vpd_size) {
		netif_err(efx, drv, efx->net_dev, "Incomplete part number\n");
		return;
	}

	netif_info(efx, drv, efx->net_dev,
		   "Part Number : %.*s\n", j, &vpd_data[i]);

	i = ro_start + PCI_VPD_LRDT_TAG_SIZE;
	j = ro_size;
	i = pci_vpd_find_info_keyword(vpd_data, i, j, "SN");
	if (i < 0) {
		netif_err(efx, drv, efx->net_dev, "Serial number not found\n");
		return;
	}

	j = pci_vpd_info_field_size(&vpd_data[i]);
	i += PCI_VPD_INFO_FLD_HDR_SIZE;
	if (i + j > vpd_size) {
		netif_err(efx, drv, efx->net_dev, "Incomplete serial number\n");
		return;
	}

	efx->vpd_sn = kmalloc(j + 1, GFP_KERNEL);
	if (!efx->vpd_sn)
		return;

	snprintf(efx->vpd_sn, j + 1, "%s", &vpd_data[i]);
}


/* Main body of NIC initialisation
 * This is called at module load (or hotplug insertion, theoretically).
 */
static int ef4_pci_probe_main(struct ef4_nic *efx)
{
	int rc;

	/* Do start-of-day initialisation */
	rc = ef4_probe_all(efx);
	if (rc)
		goto fail1;

	ef4_init_napi(efx);

	rc = efx->type->init(efx);
	if (rc) {
		netif_err(efx, probe, efx->net_dev,
			  "failed to initialise NIC\n");
		goto fail3;
	}

	rc = ef4_init_port(efx);
	if (rc) {
		netif_err(efx, probe, efx->net_dev,
			  "failed to initialise port\n");
		goto fail4;
	}

	rc = ef4_nic_init_interrupt(efx);
	if (rc)
		goto fail5;
	rc = ef4_enable_interrupts(efx);
	if (rc)
		goto fail6;

	return 0;

 fail6:
	ef4_nic_fini_interrupt(efx);
 fail5:
	ef4_fini_port(efx);
 fail4:
	efx->type->fini(efx);
 fail3:
	ef4_fini_napi(efx);
	ef4_remove_all(efx);
 fail1:
	return rc;
}

/* NIC initialisation
 *
 * This is called at module load (or hotplug insertion,
 * theoretically).  It sets up PCI mappings, resets the NIC,
 * sets up and registers the network devices with the kernel and hooks
 * the interrupt service routine.  It does not prepare the device for
 * transmission; this is left to the first time one of the network
 * interfaces is brought up (i.e. ef4_net_open).
 */
static int ef4_pci_probe(struct pci_dev *pci_dev,
			 const struct pci_device_id *entry)
{
	struct net_device *net_dev;
	struct ef4_nic *efx;
	int rc;

	/* Allocate and initialise a struct net_device and struct ef4_nic */
	net_dev = alloc_etherdev_mqs(sizeof(*efx), EF4_MAX_CORE_TX_QUEUES,
				     EF4_MAX_RX_QUEUES);
	if (!net_dev)
		return -ENOMEM;
	efx = netdev_priv(net_dev);
	efx->type = (const struct ef4_nic_type *) entry->driver_data;
	efx->fixed_features |= NETIF_F_HIGHDMA;

	pci_set_drvdata(pci_dev, efx);
	SET_NETDEV_DEV(net_dev, &pci_dev->dev);
	rc = ef4_init_struct(efx, pci_dev, net_dev);
	if (rc)
		goto fail1;

	netif_info(efx, probe, efx->net_dev,
		   "Solarflare NIC detected\n");

	ef4_probe_vpd_strings(efx);

	/* Set up basic I/O (BAR mappings etc) */
	rc = ef4_init_io(efx);
	if (rc)
		goto fail2;

	rc = ef4_pci_probe_main(efx);
	if (rc)
		goto fail3;

	net_dev->features |= (efx->type->offload_features | NETIF_F_SG |
			      NETIF_F_RXCSUM);
	/* Mask for features that also apply to VLAN devices */
	net_dev->vlan_features |= (NETIF_F_HW_CSUM | NETIF_F_SG |
				   NETIF_F_HIGHDMA | NETIF_F_RXCSUM);

	net_dev->hw_features = net_dev->features & ~efx->fixed_features;

	/* Disable VLAN filtering by default.  It may be enforced if
	 * the feature is fixed (i.e. VLAN filters are required to
	 * receive VLAN tagged packets due to vPort restrictions).
	 */
	net_dev->features &= ~NETIF_F_HW_VLAN_CTAG_FILTER;
	net_dev->features |= efx->fixed_features;

	rc = ef4_register_netdev(efx);
	if (rc)
		goto fail4;

	netif_dbg(efx, probe, efx->net_dev, "initialisation successful\n");

	/* Try to create MTDs, but allow this to fail */
	rtnl_lock();
	rc = ef4_mtd_probe(efx);
	rtnl_unlock();
	if (rc && rc != -EPERM)
		netif_warn(efx, probe, efx->net_dev,
			   "failed to create MTDs (%d)\n", rc);

	rc = pci_enable_pcie_error_reporting(pci_dev);
	if (rc && rc != -EINVAL)
		netif_notice(efx, probe, efx->net_dev,
			     "PCIE error reporting unavailable (%d).\n",
			     rc);

	return 0;

 fail4:
	ef4_pci_remove_main(efx);
 fail3:
	ef4_fini_io(efx);
 fail2:
	ef4_fini_struct(efx);
 fail1:
	WARN_ON(rc > 0);
	netif_dbg(efx, drv, efx->net_dev, "initialisation failed. rc=%d\n", rc);
	free_netdev(net_dev);
	return rc;
}

static int ef4_pm_freeze(struct device *dev)
{
	struct ef4_nic *efx = dev_get_drvdata(dev);

	rtnl_lock();

	if (efx->state != STATE_DISABLED) {
		efx->state = STATE_UNINIT;

		ef4_device_detach_sync(efx);

		ef4_stop_all(efx);
		ef4_disable_interrupts(efx);
	}

	rtnl_unlock();

	return 0;
}

static int ef4_pm_thaw(struct device *dev)
{
	int rc;
	struct ef4_nic *efx = dev_get_drvdata(dev);

	rtnl_lock();

	if (efx->state != STATE_DISABLED) {
		rc = ef4_enable_interrupts(efx);
		if (rc)
			goto fail;

		mutex_lock(&efx->mac_lock);
		efx->phy_op->reconfigure(efx);
		mutex_unlock(&efx->mac_lock);

		ef4_start_all(efx);

		netif_device_attach(efx->net_dev);

		efx->state = STATE_READY;

		efx->type->resume_wol(efx);
	}

	rtnl_unlock();

	/* Reschedule any quenched resets scheduled during ef4_pm_freeze() */
	queue_work(reset_workqueue, &efx->reset_work);

	return 0;

fail:
	rtnl_unlock();

	return rc;
}

static int ef4_pm_poweroff(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct ef4_nic *efx = pci_get_drvdata(pci_dev);

	efx->type->fini(efx);

	efx->reset_pending = 0;

	pci_save_state(pci_dev);
	return pci_set_power_state(pci_dev, PCI_D3hot);
}

/* Used for both resume and restore */
static int ef4_pm_resume(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct ef4_nic *efx = pci_get_drvdata(pci_dev);
	int rc;

	rc = pci_set_power_state(pci_dev, PCI_D0);
	if (rc)
		return rc;
	pci_restore_state(pci_dev);
	rc = pci_enable_device(pci_dev);
	if (rc)
		return rc;
	pci_set_master(efx->pci_dev);
	rc = efx->type->reset(efx, RESET_TYPE_ALL);
	if (rc)
		return rc;
	rc = efx->type->init(efx);
	if (rc)
		return rc;
	rc = ef4_pm_thaw(dev);
	return rc;
}

static int ef4_pm_suspend(struct device *dev)
{
	int rc;

	ef4_pm_freeze(dev);
	rc = ef4_pm_poweroff(dev);
	if (rc)
		ef4_pm_resume(dev);
	return rc;
}

static const struct dev_pm_ops ef4_pm_ops = {
	.suspend	= ef4_pm_suspend,
	.resume		= ef4_pm_resume,
	.freeze		= ef4_pm_freeze,
	.thaw		= ef4_pm_thaw,
	.poweroff	= ef4_pm_poweroff,
	.restore	= ef4_pm_resume,
};

/* A PCI error affecting this device was detected.
 * At this point MMIO and DMA may be disabled.
 * Stop the software path and request a slot reset.
 */
static pci_ers_result_t ef4_io_error_detected(struct pci_dev *pdev,
					      pci_channel_state_t state)
{
	pci_ers_result_t status = PCI_ERS_RESULT_RECOVERED;
	struct ef4_nic *efx = pci_get_drvdata(pdev);

	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	rtnl_lock();

	if (efx->state != STATE_DISABLED) {
		efx->state = STATE_RECOVERY;
		efx->reset_pending = 0;

		ef4_device_detach_sync(efx);

		ef4_stop_all(efx);
		ef4_disable_interrupts(efx);

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

/* Fake a successful reset, which will be performed later in ef4_io_resume. */
static pci_ers_result_t ef4_io_slot_reset(struct pci_dev *pdev)
{
	struct ef4_nic *efx = pci_get_drvdata(pdev);
	pci_ers_result_t status = PCI_ERS_RESULT_RECOVERED;

	if (pci_enable_device(pdev)) {
		netif_err(efx, hw, efx->net_dev,
			  "Cannot re-enable PCI device after reset.\n");
		status =  PCI_ERS_RESULT_DISCONNECT;
	}

	return status;
}

/* Perform the actual reset and resume I/O operations. */
static void ef4_io_resume(struct pci_dev *pdev)
{
	struct ef4_nic *efx = pci_get_drvdata(pdev);
	int rc;

	rtnl_lock();

	if (efx->state == STATE_DISABLED)
		goto out;

	rc = ef4_reset(efx, RESET_TYPE_ALL);
	if (rc) {
		netif_err(efx, hw, efx->net_dev,
			  "ef4_reset failed after PCI error (%d)\n", rc);
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
static const struct pci_error_handlers ef4_err_handlers = {
	.error_detected = ef4_io_error_detected,
	.slot_reset	= ef4_io_slot_reset,
	.resume		= ef4_io_resume,
};

static struct pci_driver ef4_pci_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= ef4_pci_table,
	.probe		= ef4_pci_probe,
	.remove		= ef4_pci_remove,
	.driver.pm	= &ef4_pm_ops,
	.err_handler	= &ef4_err_handlers,
};

/**************************************************************************
 *
 * Kernel module interface
 *
 *************************************************************************/

module_param(interrupt_mode, uint, 0444);
MODULE_PARM_DESC(interrupt_mode,
		 "Interrupt mode (0=>MSIX 1=>MSI 2=>legacy)");

static int __init ef4_init_module(void)
{
	int rc;

	printk(KERN_INFO "Solarflare Falcon driver v" EF4_DRIVER_VERSION "\n");

	rc = register_netdevice_notifier(&ef4_netdev_notifier);
	if (rc)
		goto err_notifier;

	reset_workqueue = create_singlethread_workqueue("sfc_reset");
	if (!reset_workqueue) {
		rc = -ENOMEM;
		goto err_reset;
	}

	rc = pci_register_driver(&ef4_pci_driver);
	if (rc < 0)
		goto err_pci;

	return 0;

 err_pci:
	destroy_workqueue(reset_workqueue);
 err_reset:
	unregister_netdevice_notifier(&ef4_netdev_notifier);
 err_notifier:
	return rc;
}

static void __exit ef4_exit_module(void)
{
	printk(KERN_INFO "Solarflare Falcon driver unloading\n");

	pci_unregister_driver(&ef4_pci_driver);
	destroy_workqueue(reset_workqueue);
	unregister_netdevice_notifier(&ef4_netdev_notifier);

}

module_init(ef4_init_module);
module_exit(ef4_exit_module);

MODULE_AUTHOR("Solarflare Communications and "
	      "Michael Brown <mbrown@fensystems.co.uk>");
MODULE_DESCRIPTION("Solarflare Falcon network driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, ef4_pci_table);
MODULE_VERSION(EF4_DRIVER_VERSION);
