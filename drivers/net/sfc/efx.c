/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2005-2008 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
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
#include <linux/crc32.h>
#include <linux/ethtool.h>
#include <linux/topology.h>
#include "net_driver.h"
#include "gmii.h"
#include "ethtool.h"
#include "tx.h"
#include "rx.h"
#include "efx.h"
#include "mdio_10g.h"
#include "falcon.h"
#include "mac.h"

#define EFX_MAX_MTU (9 * 1024)

/* RX slow fill workqueue. If memory allocation fails in the fast path,
 * a work item is pushed onto this work queue to retry the allocation later,
 * to avoid the NIC being starved of RX buffers. Since this is a per cpu
 * workqueue, there is nothing to be gained in making it per NIC
 */
static struct workqueue_struct *refill_workqueue;

/**************************************************************************
 *
 * Configurable values
 *
 *************************************************************************/

/*
 * Enable large receive offload (LRO) aka soft segment reassembly (SSR)
 *
 * This sets the default for new devices.  It can be controlled later
 * using ethtool.
 */
static int lro = true;
module_param(lro, int, 0644);
MODULE_PARM_DESC(lro, "Large receive offload acceleration");

/*
 * Use separate channels for TX and RX events
 *
 * Set this to 1 to use separate channels for TX and RX. It allows us to
 * apply a higher level of interrupt moderation to TX events.
 *
 * This is forced to 0 for MSI interrupt mode as the interrupt vector
 * is not written
 */
static unsigned int separate_tx_and_rx_channels = true;

/* This is the weight assigned to each of the (per-channel) virtual
 * NAPI devices.
 */
static int napi_weight = 64;

/* This is the time (in jiffies) between invocations of the hardware
 * monitor, which checks for known hardware bugs and resets the
 * hardware and driver as necessary.
 */
unsigned int efx_monitor_interval = 1 * HZ;

/* This controls whether or not the hardware monitor will trigger a
 * reset when it detects an error condition.
 */
static unsigned int monitor_reset = true;

/* This controls whether or not the driver will initialise devices
 * with invalid MAC addresses stored in the EEPROM or flash.  If true,
 * such devices will be initialised with a random locally-generated
 * MAC address.  This allows for loading the sfc_mtd driver to
 * reprogram the flash, even if the flash contents (including the MAC
 * address) have previously been erased.
 */
static unsigned int allow_bad_hwaddr;

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
 * The default (0) means to assign an interrupt to each package (level II cache)
 */
static unsigned int rss_cpus;
module_param(rss_cpus, uint, 0444);
MODULE_PARM_DESC(rss_cpus, "Number of CPUs to use for Receive-Side Scaling");

/**************************************************************************
 *
 * Utility functions and prototypes
 *
 *************************************************************************/
static void efx_remove_channel(struct efx_channel *channel);
static void efx_remove_port(struct efx_nic *efx);
static void efx_fini_napi(struct efx_nic *efx);
static void efx_fini_channels(struct efx_nic *efx);

#define EFX_ASSERT_RESET_SERIALISED(efx)		\
	do {						\
		if (efx->state == STATE_RUNNING)	\
			ASSERT_RTNL();			\
	} while (0)

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
static int efx_process_channel(struct efx_channel *channel, int rx_quota)
{
	struct efx_nic *efx = channel->efx;
	int rx_packets;

	if (unlikely(efx->reset_pending != RESET_TYPE_NONE ||
		     !channel->enabled))
		return 0;

	rx_packets = falcon_process_eventq(channel, rx_quota);
	if (rx_packets == 0)
		return 0;

	/* Deliver last RX packet. */
	if (channel->rx_pkt) {
		__efx_rx_packet(channel, channel->rx_pkt,
				channel->rx_pkt_csummed);
		channel->rx_pkt = NULL;
	}

	efx_flush_lro(channel);
	efx_rx_strategy(channel);

	efx_fast_push_rx_descriptors(&efx->rx_queue[channel->channel]);

	return rx_packets;
}

/* Mark channel as finished processing
 *
 * Note that since we will not receive further interrupts for this
 * channel before we finish processing and call the eventq_read_ack()
 * method, there is no need to use the interrupt hold-off timers.
 */
static inline void efx_channel_processed(struct efx_channel *channel)
{
	/* The interrupt handler for this channel may set work_pending
	 * as soon as we acknowledge the events we've seen.  Make sure
	 * it's cleared before then. */
	channel->work_pending = false;
	smp_wmb();

	falcon_eventq_read_ack(channel);
}

/* NAPI poll handler
 *
 * NAPI guarantees serialisation of polls of the same device, which
 * provides the guarantee required by efx_process_channel().
 */
static int efx_poll(struct napi_struct *napi, int budget)
{
	struct efx_channel *channel =
		container_of(napi, struct efx_channel, napi_str);
	struct net_device *napi_dev = channel->napi_dev;
	int rx_packets;

	EFX_TRACE(channel->efx, "channel %d NAPI poll executing on CPU %d\n",
		  channel->channel, raw_smp_processor_id());

	rx_packets = efx_process_channel(channel, budget);

	if (rx_packets < budget) {
		/* There is no race here; although napi_disable() will
		 * only wait for netif_rx_complete(), this isn't a problem
		 * since efx_channel_processed() will have no effect if
		 * interrupts have already been disabled.
		 */
		netif_rx_complete(napi_dev, napi);
		efx_channel_processed(channel);
	}

	return rx_packets;
}

/* Process the eventq of the specified channel immediately on this CPU
 *
 * Disable hardware generated interrupts, wait for any existing
 * processing to finish, then directly poll (and ack ) the eventq.
 * Finally reenable NAPI and interrupts.
 *
 * Since we are touching interrupts the caller should hold the suspend lock
 */
void efx_process_channel_now(struct efx_channel *channel)
{
	struct efx_nic *efx = channel->efx;

	BUG_ON(!channel->used_flags);
	BUG_ON(!channel->enabled);

	/* Disable interrupts and wait for ISRs to complete */
	falcon_disable_interrupts(efx);
	if (efx->legacy_irq)
		synchronize_irq(efx->legacy_irq);
	if (channel->irq)
		synchronize_irq(channel->irq);

	/* Wait for any NAPI processing to complete */
	napi_disable(&channel->napi_str);

	/* Poll the channel */
	efx_process_channel(channel, efx->type->evq_size);

	/* Ack the eventq. This may cause an interrupt to be generated
	 * when they are reenabled */
	efx_channel_processed(channel);

	napi_enable(&channel->napi_str);
	falcon_enable_interrupts(efx);
}

/* Create event queue
 * Event queue memory allocations are done only once.  If the channel
 * is reset, the memory buffer will be reused; this guards against
 * errors during channel reset and also simplifies interrupt handling.
 */
static int efx_probe_eventq(struct efx_channel *channel)
{
	EFX_LOG(channel->efx, "chan %d create event queue\n", channel->channel);

	return falcon_probe_eventq(channel);
}

/* Prepare channel's event queue */
static void efx_init_eventq(struct efx_channel *channel)
{
	EFX_LOG(channel->efx, "chan %d init event queue\n", channel->channel);

	channel->eventq_read_ptr = 0;

	falcon_init_eventq(channel);
}

static void efx_fini_eventq(struct efx_channel *channel)
{
	EFX_LOG(channel->efx, "chan %d fini event queue\n", channel->channel);

	falcon_fini_eventq(channel);
}

static void efx_remove_eventq(struct efx_channel *channel)
{
	EFX_LOG(channel->efx, "chan %d remove event queue\n", channel->channel);

	falcon_remove_eventq(channel);
}

/**************************************************************************
 *
 * Channel handling
 *
 *************************************************************************/

static int efx_probe_channel(struct efx_channel *channel)
{
	struct efx_tx_queue *tx_queue;
	struct efx_rx_queue *rx_queue;
	int rc;

	EFX_LOG(channel->efx, "creating channel %d\n", channel->channel);

	rc = efx_probe_eventq(channel);
	if (rc)
		goto fail1;

	efx_for_each_channel_tx_queue(tx_queue, channel) {
		rc = efx_probe_tx_queue(tx_queue);
		if (rc)
			goto fail2;
	}

	efx_for_each_channel_rx_queue(rx_queue, channel) {
		rc = efx_probe_rx_queue(rx_queue);
		if (rc)
			goto fail3;
	}

	channel->n_rx_frm_trunc = 0;

	return 0;

 fail3:
	efx_for_each_channel_rx_queue(rx_queue, channel)
		efx_remove_rx_queue(rx_queue);
 fail2:
	efx_for_each_channel_tx_queue(tx_queue, channel)
		efx_remove_tx_queue(tx_queue);
 fail1:
	return rc;
}


/* Channels are shutdown and reinitialised whilst the NIC is running
 * to propagate configuration changes (mtu, checksum offload), or
 * to clear hardware error conditions
 */
static void efx_init_channels(struct efx_nic *efx)
{
	struct efx_tx_queue *tx_queue;
	struct efx_rx_queue *rx_queue;
	struct efx_channel *channel;

	/* Calculate the rx buffer allocation parameters required to
	 * support the current MTU, including padding for header
	 * alignment and overruns.
	 */
	efx->rx_buffer_len = (max(EFX_PAGE_IP_ALIGN, NET_IP_ALIGN) +
			      EFX_MAX_FRAME_LEN(efx->net_dev->mtu) +
			      efx->type->rx_buffer_padding);
	efx->rx_buffer_order = get_order(efx->rx_buffer_len);

	/* Initialise the channels */
	efx_for_each_channel(channel, efx) {
		EFX_LOG(channel->efx, "init chan %d\n", channel->channel);

		efx_init_eventq(channel);

		efx_for_each_channel_tx_queue(tx_queue, channel)
			efx_init_tx_queue(tx_queue);

		/* The rx buffer allocation strategy is MTU dependent */
		efx_rx_strategy(channel);

		efx_for_each_channel_rx_queue(rx_queue, channel)
			efx_init_rx_queue(rx_queue);

		WARN_ON(channel->rx_pkt != NULL);
		efx_rx_strategy(channel);
	}
}

/* This enables event queue processing and packet transmission.
 *
 * Note that this function is not allowed to fail, since that would
 * introduce too much complexity into the suspend/resume path.
 */
static void efx_start_channel(struct efx_channel *channel)
{
	struct efx_rx_queue *rx_queue;

	EFX_LOG(channel->efx, "starting chan %d\n", channel->channel);

	if (!(channel->efx->net_dev->flags & IFF_UP))
		netif_napi_add(channel->napi_dev, &channel->napi_str,
			       efx_poll, napi_weight);

	/* The interrupt handler for this channel may set work_pending
	 * as soon as we enable it.  Make sure it's cleared before
	 * then.  Similarly, make sure it sees the enabled flag set. */
	channel->work_pending = false;
	channel->enabled = true;
	smp_wmb();

	napi_enable(&channel->napi_str);

	/* Load up RX descriptors */
	efx_for_each_channel_rx_queue(rx_queue, channel)
		efx_fast_push_rx_descriptors(rx_queue);
}

/* This disables event queue processing and packet transmission.
 * This function does not guarantee that all queue processing
 * (e.g. RX refill) is complete.
 */
static void efx_stop_channel(struct efx_channel *channel)
{
	struct efx_rx_queue *rx_queue;

	if (!channel->enabled)
		return;

	EFX_LOG(channel->efx, "stop chan %d\n", channel->channel);

	channel->enabled = false;
	napi_disable(&channel->napi_str);

	/* Ensure that any worker threads have exited or will be no-ops */
	efx_for_each_channel_rx_queue(rx_queue, channel) {
		spin_lock_bh(&rx_queue->add_lock);
		spin_unlock_bh(&rx_queue->add_lock);
	}
}

static void efx_fini_channels(struct efx_nic *efx)
{
	struct efx_channel *channel;
	struct efx_tx_queue *tx_queue;
	struct efx_rx_queue *rx_queue;

	EFX_ASSERT_RESET_SERIALISED(efx);
	BUG_ON(efx->port_enabled);

	efx_for_each_channel(channel, efx) {
		EFX_LOG(channel->efx, "shut down chan %d\n", channel->channel);

		efx_for_each_channel_rx_queue(rx_queue, channel)
			efx_fini_rx_queue(rx_queue);
		efx_for_each_channel_tx_queue(tx_queue, channel)
			efx_fini_tx_queue(tx_queue);
	}

	/* Do the event queues last so that we can handle flush events
	 * for all DMA queues. */
	efx_for_each_channel(channel, efx) {
		EFX_LOG(channel->efx, "shut down evq %d\n", channel->channel);

		efx_fini_eventq(channel);
	}
}

static void efx_remove_channel(struct efx_channel *channel)
{
	struct efx_tx_queue *tx_queue;
	struct efx_rx_queue *rx_queue;

	EFX_LOG(channel->efx, "destroy chan %d\n", channel->channel);

	efx_for_each_channel_rx_queue(rx_queue, channel)
		efx_remove_rx_queue(rx_queue);
	efx_for_each_channel_tx_queue(tx_queue, channel)
		efx_remove_tx_queue(tx_queue);
	efx_remove_eventq(channel);

	channel->used_flags = 0;
}

void efx_schedule_slow_fill(struct efx_rx_queue *rx_queue, int delay)
{
	queue_delayed_work(refill_workqueue, &rx_queue->work, delay);
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
static void efx_link_status_changed(struct efx_nic *efx)
{
	/* SFC Bug 5356: A net_dev notifier is registered, so we must ensure
	 * that no events are triggered between unregister_netdev() and the
	 * driver unloading. A more general condition is that NETDEV_CHANGE
	 * can only be generated between NETDEV_UP and NETDEV_DOWN */
	if (!netif_running(efx->net_dev))
		return;

	if (efx->port_inhibited) {
		netif_carrier_off(efx->net_dev);
		return;
	}

	if (efx->link_up != netif_carrier_ok(efx->net_dev)) {
		efx->n_link_state_changes++;

		if (efx->link_up)
			netif_carrier_on(efx->net_dev);
		else
			netif_carrier_off(efx->net_dev);
	}

	/* Status message for kernel log */
	if (efx->link_up) {
		struct mii_if_info *gmii = &efx->mii;
		unsigned adv, lpa;
		/* NONE here means direct XAUI from the controller, with no
		 * MDIO-attached device we can query. */
		if (efx->phy_type != PHY_TYPE_NONE) {
			adv = gmii_advertised(gmii);
			lpa = gmii_lpa(gmii);
		} else {
			lpa = GM_LPA_10000 | LPA_DUPLEX;
			adv = lpa;
		}
		EFX_INFO(efx, "link up at %dMbps %s-duplex "
			 "(adv %04x lpa %04x) (MTU %d)%s\n",
			 (efx->link_options & GM_LPA_10000 ? 10000 :
			  (efx->link_options & GM_LPA_1000 ? 1000 :
			   (efx->link_options & GM_LPA_100 ? 100 :
			    10))),
			 (efx->link_options & GM_LPA_DUPLEX ?
			  "full" : "half"),
			 adv, lpa,
			 efx->net_dev->mtu,
			 (efx->promiscuous ? " [PROMISC]" : ""));
	} else {
		EFX_INFO(efx, "link down\n");
	}

}

/* This call reinitialises the MAC to pick up new PHY settings. The
 * caller must hold the mac_lock */
void __efx_reconfigure_port(struct efx_nic *efx)
{
	WARN_ON(!mutex_is_locked(&efx->mac_lock));

	EFX_LOG(efx, "reconfiguring MAC from PHY settings on CPU %d\n",
		raw_smp_processor_id());

	falcon_reconfigure_xmac(efx);

	/* Inform kernel of loss/gain of carrier */
	efx_link_status_changed(efx);
}

/* Reinitialise the MAC to pick up new PHY settings, even if the port is
 * disabled. */
void efx_reconfigure_port(struct efx_nic *efx)
{
	EFX_ASSERT_RESET_SERIALISED(efx);

	mutex_lock(&efx->mac_lock);
	__efx_reconfigure_port(efx);
	mutex_unlock(&efx->mac_lock);
}

/* Asynchronous efx_reconfigure_port work item. To speed up efx_flush_all()
 * we don't efx_reconfigure_port() if the port is disabled. Care is taken
 * in efx_stop_all() and efx_start_port() to prevent PHY events being lost */
static void efx_reconfigure_work(struct work_struct *data)
{
	struct efx_nic *efx = container_of(data, struct efx_nic,
					   reconfigure_work);

	mutex_lock(&efx->mac_lock);
	if (efx->port_enabled)
		__efx_reconfigure_port(efx);
	mutex_unlock(&efx->mac_lock);
}

static int efx_probe_port(struct efx_nic *efx)
{
	int rc;

	EFX_LOG(efx, "create port\n");

	/* Connect up MAC/PHY operations table and read MAC address */
	rc = falcon_probe_port(efx);
	if (rc)
		goto err;

	/* Sanity check MAC address */
	if (is_valid_ether_addr(efx->mac_address)) {
		memcpy(efx->net_dev->dev_addr, efx->mac_address, ETH_ALEN);
	} else {
		DECLARE_MAC_BUF(mac);

		EFX_ERR(efx, "invalid MAC address %s\n",
			print_mac(mac, efx->mac_address));
		if (!allow_bad_hwaddr) {
			rc = -EINVAL;
			goto err;
		}
		random_ether_addr(efx->net_dev->dev_addr);
		EFX_INFO(efx, "using locally-generated MAC %s\n",
			 print_mac(mac, efx->net_dev->dev_addr));
	}

	return 0;

 err:
	efx_remove_port(efx);
	return rc;
}

static int efx_init_port(struct efx_nic *efx)
{
	int rc;

	EFX_LOG(efx, "init port\n");

	/* Initialise the MAC and PHY */
	rc = falcon_init_xmac(efx);
	if (rc)
		return rc;

	efx->port_initialized = true;
	efx->stats_enabled = true;

	/* Reconfigure port to program MAC registers */
	falcon_reconfigure_xmac(efx);

	return 0;
}

/* Allow efx_reconfigure_port() to be scheduled, and close the window
 * between efx_stop_port and efx_flush_all whereby a previously scheduled
 * efx_reconfigure_port() may have been cancelled */
static void efx_start_port(struct efx_nic *efx)
{
	EFX_LOG(efx, "start port\n");
	BUG_ON(efx->port_enabled);

	mutex_lock(&efx->mac_lock);
	efx->port_enabled = true;
	__efx_reconfigure_port(efx);
	mutex_unlock(&efx->mac_lock);
}

/* Prevent efx_reconfigure_work and efx_monitor() from executing, and
 * efx_set_multicast_list() from scheduling efx_reconfigure_work.
 * efx_reconfigure_work can still be scheduled via NAPI processing
 * until efx_flush_all() is called */
static void efx_stop_port(struct efx_nic *efx)
{
	EFX_LOG(efx, "stop port\n");

	mutex_lock(&efx->mac_lock);
	efx->port_enabled = false;
	mutex_unlock(&efx->mac_lock);

	/* Serialise against efx_set_multicast_list() */
	if (efx_dev_registered(efx)) {
		netif_addr_lock_bh(efx->net_dev);
		netif_addr_unlock_bh(efx->net_dev);
	}
}

static void efx_fini_port(struct efx_nic *efx)
{
	EFX_LOG(efx, "shut down port\n");

	if (!efx->port_initialized)
		return;

	falcon_fini_xmac(efx);
	efx->port_initialized = false;

	efx->link_up = false;
	efx_link_status_changed(efx);
}

static void efx_remove_port(struct efx_nic *efx)
{
	EFX_LOG(efx, "destroying port\n");

	falcon_remove_port(efx);
}

/**************************************************************************
 *
 * NIC handling
 *
 **************************************************************************/

/* This configures the PCI device to enable I/O and DMA. */
static int efx_init_io(struct efx_nic *efx)
{
	struct pci_dev *pci_dev = efx->pci_dev;
	dma_addr_t dma_mask = efx->type->max_dma_mask;
	int rc;

	EFX_LOG(efx, "initialising I/O\n");

	rc = pci_enable_device(pci_dev);
	if (rc) {
		EFX_ERR(efx, "failed to enable PCI device\n");
		goto fail1;
	}

	pci_set_master(pci_dev);

	/* Set the PCI DMA mask.  Try all possibilities from our
	 * genuine mask down to 32 bits, because some architectures
	 * (e.g. x86_64 with iommu_sac_force set) will allow 40 bit
	 * masks event though they reject 46 bit masks.
	 */
	while (dma_mask > 0x7fffffffUL) {
		if (pci_dma_supported(pci_dev, dma_mask) &&
		    ((rc = pci_set_dma_mask(pci_dev, dma_mask)) == 0))
			break;
		dma_mask >>= 1;
	}
	if (rc) {
		EFX_ERR(efx, "could not find a suitable DMA mask\n");
		goto fail2;
	}
	EFX_LOG(efx, "using DMA mask %llx\n", (unsigned long long) dma_mask);
	rc = pci_set_consistent_dma_mask(pci_dev, dma_mask);
	if (rc) {
		/* pci_set_consistent_dma_mask() is not *allowed* to
		 * fail with a mask that pci_set_dma_mask() accepted,
		 * but just in case...
		 */
		EFX_ERR(efx, "failed to set consistent DMA mask\n");
		goto fail2;
	}

	efx->membase_phys = pci_resource_start(efx->pci_dev,
					       efx->type->mem_bar);
	rc = pci_request_region(pci_dev, efx->type->mem_bar, "sfc");
	if (rc) {
		EFX_ERR(efx, "request for memory BAR failed\n");
		rc = -EIO;
		goto fail3;
	}
	efx->membase = ioremap_nocache(efx->membase_phys,
				       efx->type->mem_map_size);
	if (!efx->membase) {
		EFX_ERR(efx, "could not map memory BAR %d at %llx+%x\n",
			efx->type->mem_bar,
			(unsigned long long)efx->membase_phys,
			efx->type->mem_map_size);
		rc = -ENOMEM;
		goto fail4;
	}
	EFX_LOG(efx, "memory BAR %u at %llx+%x (virtual %p)\n",
		efx->type->mem_bar, (unsigned long long)efx->membase_phys,
		efx->type->mem_map_size, efx->membase);

	return 0;

 fail4:
	release_mem_region(efx->membase_phys, efx->type->mem_map_size);
 fail3:
	efx->membase_phys = 0;
 fail2:
	pci_disable_device(efx->pci_dev);
 fail1:
	return rc;
}

static void efx_fini_io(struct efx_nic *efx)
{
	EFX_LOG(efx, "shutting down I/O\n");

	if (efx->membase) {
		iounmap(efx->membase);
		efx->membase = NULL;
	}

	if (efx->membase_phys) {
		pci_release_region(efx->pci_dev, efx->type->mem_bar);
		efx->membase_phys = 0;
	}

	pci_disable_device(efx->pci_dev);
}

/* Get number of RX queues wanted.  Return number of online CPU
 * packages in the expectation that an IRQ balancer will spread
 * interrupts across them. */
static int efx_wanted_rx_queues(void)
{
	cpumask_t core_mask;
	int count;
	int cpu;

	cpus_clear(core_mask);
	count = 0;
	for_each_online_cpu(cpu) {
		if (!cpu_isset(cpu, core_mask)) {
			++count;
			cpus_or(core_mask, core_mask,
				topology_core_siblings(cpu));
		}
	}

	return count;
}

/* Probe the number and type of interrupts we are able to obtain, and
 * the resulting numbers of channels and RX queues.
 */
static void efx_probe_interrupts(struct efx_nic *efx)
{
	int max_channels =
		min_t(int, efx->type->phys_addr_channels, EFX_MAX_CHANNELS);
	int rc, i;

	if (efx->interrupt_mode == EFX_INT_MODE_MSIX) {
		struct msix_entry xentries[EFX_MAX_CHANNELS];
		int wanted_ints;

		/* We want one RX queue and interrupt per CPU package
		 * (or as specified by the rss_cpus module parameter).
		 * We will need one channel per interrupt.
		 */
		wanted_ints = rss_cpus ? rss_cpus : efx_wanted_rx_queues();
		efx->n_rx_queues = min(wanted_ints, max_channels);

		for (i = 0; i < efx->n_rx_queues; i++)
			xentries[i].entry = i;
		rc = pci_enable_msix(efx->pci_dev, xentries, efx->n_rx_queues);
		if (rc > 0) {
			EFX_BUG_ON_PARANOID(rc >= efx->n_rx_queues);
			efx->n_rx_queues = rc;
			rc = pci_enable_msix(efx->pci_dev, xentries,
					     efx->n_rx_queues);
		}

		if (rc == 0) {
			for (i = 0; i < efx->n_rx_queues; i++)
				efx->channel[i].irq = xentries[i].vector;
		} else {
			/* Fall back to single channel MSI */
			efx->interrupt_mode = EFX_INT_MODE_MSI;
			EFX_ERR(efx, "could not enable MSI-X\n");
		}
	}

	/* Try single interrupt MSI */
	if (efx->interrupt_mode == EFX_INT_MODE_MSI) {
		efx->n_rx_queues = 1;
		rc = pci_enable_msi(efx->pci_dev);
		if (rc == 0) {
			efx->channel[0].irq = efx->pci_dev->irq;
		} else {
			EFX_ERR(efx, "could not enable MSI\n");
			efx->interrupt_mode = EFX_INT_MODE_LEGACY;
		}
	}

	/* Assume legacy interrupts */
	if (efx->interrupt_mode == EFX_INT_MODE_LEGACY) {
		efx->n_rx_queues = 1;
		efx->legacy_irq = efx->pci_dev->irq;
	}
}

static void efx_remove_interrupts(struct efx_nic *efx)
{
	struct efx_channel *channel;

	/* Remove MSI/MSI-X interrupts */
	efx_for_each_channel(channel, efx)
		channel->irq = 0;
	pci_disable_msi(efx->pci_dev);
	pci_disable_msix(efx->pci_dev);

	/* Remove legacy interrupt */
	efx->legacy_irq = 0;
}

static void efx_set_channels(struct efx_nic *efx)
{
	struct efx_tx_queue *tx_queue;
	struct efx_rx_queue *rx_queue;

	efx_for_each_tx_queue(tx_queue, efx) {
		if (!EFX_INT_MODE_USE_MSI(efx) && separate_tx_and_rx_channels)
			tx_queue->channel = &efx->channel[1];
		else
			tx_queue->channel = &efx->channel[0];
		tx_queue->channel->used_flags |= EFX_USED_BY_TX;
	}

	efx_for_each_rx_queue(rx_queue, efx) {
		rx_queue->channel = &efx->channel[rx_queue->queue];
		rx_queue->channel->used_flags |= EFX_USED_BY_RX;
	}
}

static int efx_probe_nic(struct efx_nic *efx)
{
	int rc;

	EFX_LOG(efx, "creating NIC\n");

	/* Carry out hardware-type specific initialisation */
	rc = falcon_probe_nic(efx);
	if (rc)
		return rc;

	/* Determine the number of channels and RX queues by trying to hook
	 * in MSI-X interrupts. */
	efx_probe_interrupts(efx);

	efx_set_channels(efx);

	/* Initialise the interrupt moderation settings */
	efx_init_irq_moderation(efx, tx_irq_mod_usec, rx_irq_mod_usec);

	return 0;
}

static void efx_remove_nic(struct efx_nic *efx)
{
	EFX_LOG(efx, "destroying NIC\n");

	efx_remove_interrupts(efx);
	falcon_remove_nic(efx);
}

/**************************************************************************
 *
 * NIC startup/shutdown
 *
 *************************************************************************/

static int efx_probe_all(struct efx_nic *efx)
{
	struct efx_channel *channel;
	int rc;

	/* Create NIC */
	rc = efx_probe_nic(efx);
	if (rc) {
		EFX_ERR(efx, "failed to create NIC\n");
		goto fail1;
	}

	/* Create port */
	rc = efx_probe_port(efx);
	if (rc) {
		EFX_ERR(efx, "failed to create port\n");
		goto fail2;
	}

	/* Create channels */
	efx_for_each_channel(channel, efx) {
		rc = efx_probe_channel(channel);
		if (rc) {
			EFX_ERR(efx, "failed to create channel %d\n",
				channel->channel);
			goto fail3;
		}
	}

	return 0;

 fail3:
	efx_for_each_channel(channel, efx)
		efx_remove_channel(channel);
	efx_remove_port(efx);
 fail2:
	efx_remove_nic(efx);
 fail1:
	return rc;
}

/* Called after previous invocation(s) of efx_stop_all, restarts the
 * port, kernel transmit queue, NAPI processing and hardware interrupts,
 * and ensures that the port is scheduled to be reconfigured.
 * This function is safe to call multiple times when the NIC is in any
 * state. */
static void efx_start_all(struct efx_nic *efx)
{
	struct efx_channel *channel;

	EFX_ASSERT_RESET_SERIALISED(efx);

	/* Check that it is appropriate to restart the interface. All
	 * of these flags are safe to read under just the rtnl lock */
	if (efx->port_enabled)
		return;
	if ((efx->state != STATE_RUNNING) && (efx->state != STATE_INIT))
		return;
	if (efx_dev_registered(efx) && !netif_running(efx->net_dev))
		return;

	/* Mark the port as enabled so port reconfigurations can start, then
	 * restart the transmit interface early so the watchdog timer stops */
	efx_start_port(efx);
	if (efx_dev_registered(efx))
		efx_wake_queue(efx);

	efx_for_each_channel(channel, efx)
		efx_start_channel(channel);

	falcon_enable_interrupts(efx);

	/* Start hardware monitor if we're in RUNNING */
	if (efx->state == STATE_RUNNING)
		queue_delayed_work(efx->workqueue, &efx->monitor_work,
				   efx_monitor_interval);
}

/* Flush all delayed work. Should only be called when no more delayed work
 * will be scheduled. This doesn't flush pending online resets (efx_reset),
 * since we're holding the rtnl_lock at this point. */
static void efx_flush_all(struct efx_nic *efx)
{
	struct efx_rx_queue *rx_queue;

	/* Make sure the hardware monitor is stopped */
	cancel_delayed_work_sync(&efx->monitor_work);

	/* Ensure that all RX slow refills are complete. */
	efx_for_each_rx_queue(rx_queue, efx)
		cancel_delayed_work_sync(&rx_queue->work);

	/* Stop scheduled port reconfigurations */
	cancel_work_sync(&efx->reconfigure_work);

}

/* Quiesce hardware and software without bringing the link down.
 * Safe to call multiple times, when the nic and interface is in any
 * state. The caller is guaranteed to subsequently be in a position
 * to modify any hardware and software state they see fit without
 * taking locks. */
static void efx_stop_all(struct efx_nic *efx)
{
	struct efx_channel *channel;

	EFX_ASSERT_RESET_SERIALISED(efx);

	/* port_enabled can be read safely under the rtnl lock */
	if (!efx->port_enabled)
		return;

	/* Disable interrupts and wait for ISR to complete */
	falcon_disable_interrupts(efx);
	if (efx->legacy_irq)
		synchronize_irq(efx->legacy_irq);
	efx_for_each_channel(channel, efx) {
		if (channel->irq)
			synchronize_irq(channel->irq);
	}

	/* Stop all NAPI processing and synchronous rx refills */
	efx_for_each_channel(channel, efx)
		efx_stop_channel(channel);

	/* Stop all asynchronous port reconfigurations. Since all
	 * event processing has already been stopped, there is no
	 * window to loose phy events */
	efx_stop_port(efx);

	/* Flush reconfigure_work, refill_workqueue, monitor_work */
	efx_flush_all(efx);

	/* Isolate the MAC from the TX and RX engines, so that queue
	 * flushes will complete in a timely fashion. */
	falcon_deconfigure_mac_wrapper(efx);
	falcon_drain_tx_fifo(efx);

	/* Stop the kernel transmit interface late, so the watchdog
	 * timer isn't ticking over the flush */
	if (efx_dev_registered(efx)) {
		efx_stop_queue(efx);
		netif_tx_lock_bh(efx->net_dev);
		netif_tx_unlock_bh(efx->net_dev);
	}
}

static void efx_remove_all(struct efx_nic *efx)
{
	struct efx_channel *channel;

	efx_for_each_channel(channel, efx)
		efx_remove_channel(channel);
	efx_remove_port(efx);
	efx_remove_nic(efx);
}

/* A convinience function to safely flush all the queues */
void efx_flush_queues(struct efx_nic *efx)
{
	EFX_ASSERT_RESET_SERIALISED(efx);

	efx_stop_all(efx);

	efx_fini_channels(efx);
	efx_init_channels(efx);

	efx_start_all(efx);
}

/**************************************************************************
 *
 * Interrupt moderation
 *
 **************************************************************************/

/* Set interrupt moderation parameters */
void efx_init_irq_moderation(struct efx_nic *efx, int tx_usecs, int rx_usecs)
{
	struct efx_tx_queue *tx_queue;
	struct efx_rx_queue *rx_queue;

	EFX_ASSERT_RESET_SERIALISED(efx);

	efx_for_each_tx_queue(tx_queue, efx)
		tx_queue->channel->irq_moderation = tx_usecs;

	efx_for_each_rx_queue(rx_queue, efx)
		rx_queue->channel->irq_moderation = rx_usecs;
}

/**************************************************************************
 *
 * Hardware monitor
 *
 **************************************************************************/

/* Run periodically off the general workqueue. Serialised against
 * efx_reconfigure_port via the mac_lock */
static void efx_monitor(struct work_struct *data)
{
	struct efx_nic *efx = container_of(data, struct efx_nic,
					   monitor_work.work);
	int rc = 0;

	EFX_TRACE(efx, "hardware monitor executing on CPU %d\n",
		  raw_smp_processor_id());


	/* If the mac_lock is already held then it is likely a port
	 * reconfiguration is already in place, which will likely do
	 * most of the work of check_hw() anyway. */
	if (!mutex_trylock(&efx->mac_lock)) {
		queue_delayed_work(efx->workqueue, &efx->monitor_work,
				   efx_monitor_interval);
		return;
	}

	if (efx->port_enabled)
		rc = falcon_check_xmac(efx);
	mutex_unlock(&efx->mac_lock);

	if (rc) {
		if (monitor_reset) {
			EFX_ERR(efx, "hardware monitor detected a fault: "
				"triggering reset\n");
			efx_schedule_reset(efx, RESET_TYPE_MONITOR);
		} else {
			EFX_ERR(efx, "hardware monitor detected a fault, "
				"skipping reset\n");
		}
	}

	queue_delayed_work(efx->workqueue, &efx->monitor_work,
			   efx_monitor_interval);
}

/**************************************************************************
 *
 * ioctls
 *
 *************************************************************************/

/* Net device ioctl
 * Context: process, rtnl_lock() held.
 */
static int efx_ioctl(struct net_device *net_dev, struct ifreq *ifr, int cmd)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	EFX_ASSERT_RESET_SERIALISED(efx);

	return generic_mii_ioctl(&efx->mii, if_mii(ifr), cmd, NULL);
}

/**************************************************************************
 *
 * NAPI interface
 *
 **************************************************************************/

static int efx_init_napi(struct efx_nic *efx)
{
	struct efx_channel *channel;
	int rc;

	efx_for_each_channel(channel, efx) {
		channel->napi_dev = efx->net_dev;
		rc = efx_lro_init(&channel->lro_mgr, efx);
		if (rc)
			goto err;
	}
	return 0;
 err:
	efx_fini_napi(efx);
	return rc;
}

static void efx_fini_napi(struct efx_nic *efx)
{
	struct efx_channel *channel;

	efx_for_each_channel(channel, efx) {
		efx_lro_fini(&channel->lro_mgr);
		channel->napi_dev = NULL;
	}
}

/**************************************************************************
 *
 * Kernel netpoll interface
 *
 *************************************************************************/

#ifdef CONFIG_NET_POLL_CONTROLLER

/* Although in the common case interrupts will be disabled, this is not
 * guaranteed. However, all our work happens inside the NAPI callback,
 * so no locking is required.
 */
static void efx_netpoll(struct net_device *net_dev)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	struct efx_channel *channel;

	efx_for_each_channel(channel, efx)
		efx_schedule_channel(channel);
}

#endif

/**************************************************************************
 *
 * Kernel net device interface
 *
 *************************************************************************/

/* Context: process, rtnl_lock() held. */
static int efx_net_open(struct net_device *net_dev)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	EFX_ASSERT_RESET_SERIALISED(efx);

	EFX_LOG(efx, "opening device %s on CPU %d\n", net_dev->name,
		raw_smp_processor_id());

	if (efx->phy_mode & PHY_MODE_SPECIAL)
		return -EBUSY;

	efx_start_all(efx);
	return 0;
}

/* Context: process, rtnl_lock() held.
 * Note that the kernel will ignore our return code; this method
 * should really be a void.
 */
static int efx_net_stop(struct net_device *net_dev)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	EFX_LOG(efx, "closing %s on CPU %d\n", net_dev->name,
		raw_smp_processor_id());

	/* Stop the device and flush all the channels */
	efx_stop_all(efx);
	efx_fini_channels(efx);
	efx_init_channels(efx);

	return 0;
}

/* Context: process, dev_base_lock or RTNL held, non-blocking. */
static struct net_device_stats *efx_net_stats(struct net_device *net_dev)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	struct efx_mac_stats *mac_stats = &efx->mac_stats;
	struct net_device_stats *stats = &net_dev->stats;

	/* Update stats if possible, but do not wait if another thread
	 * is updating them (or resetting the NIC); slightly stale
	 * stats are acceptable.
	 */
	if (!spin_trylock(&efx->stats_lock))
		return stats;
	if (efx->stats_enabled) {
		falcon_update_stats_xmac(efx);
		falcon_update_nic_stats(efx);
	}
	spin_unlock(&efx->stats_lock);

	stats->rx_packets = mac_stats->rx_packets;
	stats->tx_packets = mac_stats->tx_packets;
	stats->rx_bytes = mac_stats->rx_bytes;
	stats->tx_bytes = mac_stats->tx_bytes;
	stats->multicast = mac_stats->rx_multicast;
	stats->collisions = mac_stats->tx_collision;
	stats->rx_length_errors = (mac_stats->rx_gtjumbo +
				   mac_stats->rx_length_error);
	stats->rx_over_errors = efx->n_rx_nodesc_drop_cnt;
	stats->rx_crc_errors = mac_stats->rx_bad;
	stats->rx_frame_errors = mac_stats->rx_align_error;
	stats->rx_fifo_errors = mac_stats->rx_overflow;
	stats->rx_missed_errors = mac_stats->rx_missed;
	stats->tx_window_errors = mac_stats->tx_late_collision;

	stats->rx_errors = (stats->rx_length_errors +
			    stats->rx_over_errors +
			    stats->rx_crc_errors +
			    stats->rx_frame_errors +
			    stats->rx_fifo_errors +
			    stats->rx_missed_errors +
			    mac_stats->rx_symbol_error);
	stats->tx_errors = (stats->tx_window_errors +
			    mac_stats->tx_bad);

	return stats;
}

/* Context: netif_tx_lock held, BHs disabled. */
static void efx_watchdog(struct net_device *net_dev)
{
	struct efx_nic *efx = netdev_priv(net_dev);

	EFX_ERR(efx, "TX stuck with stop_count=%d port_enabled=%d: %s\n",
		atomic_read(&efx->netif_stop_count), efx->port_enabled,
		monitor_reset ? "resetting channels" : "skipping reset");

	if (monitor_reset)
		efx_schedule_reset(efx, RESET_TYPE_MONITOR);
}


/* Context: process, rtnl_lock() held. */
static int efx_change_mtu(struct net_device *net_dev, int new_mtu)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	int rc = 0;

	EFX_ASSERT_RESET_SERIALISED(efx);

	if (new_mtu > EFX_MAX_MTU)
		return -EINVAL;

	efx_stop_all(efx);

	EFX_LOG(efx, "changing MTU to %d\n", new_mtu);

	efx_fini_channels(efx);
	net_dev->mtu = new_mtu;
	efx_init_channels(efx);

	efx_start_all(efx);
	return rc;
}

static int efx_set_mac_address(struct net_device *net_dev, void *data)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	struct sockaddr *addr = data;
	char *new_addr = addr->sa_data;

	EFX_ASSERT_RESET_SERIALISED(efx);

	if (!is_valid_ether_addr(new_addr)) {
		DECLARE_MAC_BUF(mac);
		EFX_ERR(efx, "invalid ethernet MAC address requested: %s\n",
			print_mac(mac, new_addr));
		return -EINVAL;
	}

	memcpy(net_dev->dev_addr, new_addr, net_dev->addr_len);

	/* Reconfigure the MAC */
	efx_reconfigure_port(efx);

	return 0;
}

/* Context: netif_tx_lock held, BHs disabled. */
static void efx_set_multicast_list(struct net_device *net_dev)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	struct dev_mc_list *mc_list = net_dev->mc_list;
	union efx_multicast_hash *mc_hash = &efx->multicast_hash;
	bool promiscuous;
	u32 crc;
	int bit;
	int i;

	/* Set per-MAC promiscuity flag and reconfigure MAC if necessary */
	promiscuous = !!(net_dev->flags & IFF_PROMISC);
	if (efx->promiscuous != promiscuous) {
		efx->promiscuous = promiscuous;
		/* Close the window between efx_stop_port() and efx_flush_all()
		 * by only queuing work when the port is enabled. */
		if (efx->port_enabled)
			queue_work(efx->workqueue, &efx->reconfigure_work);
	}

	/* Build multicast hash table */
	if (promiscuous || (net_dev->flags & IFF_ALLMULTI)) {
		memset(mc_hash, 0xff, sizeof(*mc_hash));
	} else {
		memset(mc_hash, 0x00, sizeof(*mc_hash));
		for (i = 0; i < net_dev->mc_count; i++) {
			crc = ether_crc_le(ETH_ALEN, mc_list->dmi_addr);
			bit = crc & (EFX_MCAST_HASH_ENTRIES - 1);
			set_bit_le(bit, mc_hash->byte);
			mc_list = mc_list->next;
		}
	}

	/* Create and activate new global multicast hash table */
	falcon_set_multicast_hash(efx);
}

static int efx_netdev_event(struct notifier_block *this,
			    unsigned long event, void *ptr)
{
	struct net_device *net_dev = ptr;

	if (net_dev->open == efx_net_open && event == NETDEV_CHANGENAME) {
		struct efx_nic *efx = netdev_priv(net_dev);

		strcpy(efx->name, net_dev->name);
	}

	return NOTIFY_DONE;
}

static struct notifier_block efx_netdev_notifier = {
	.notifier_call = efx_netdev_event,
};

static int efx_register_netdev(struct efx_nic *efx)
{
	struct net_device *net_dev = efx->net_dev;
	int rc;

	net_dev->watchdog_timeo = 5 * HZ;
	net_dev->irq = efx->pci_dev->irq;
	net_dev->open = efx_net_open;
	net_dev->stop = efx_net_stop;
	net_dev->get_stats = efx_net_stats;
	net_dev->tx_timeout = &efx_watchdog;
	net_dev->hard_start_xmit = efx_hard_start_xmit;
	net_dev->do_ioctl = efx_ioctl;
	net_dev->change_mtu = efx_change_mtu;
	net_dev->set_mac_address = efx_set_mac_address;
	net_dev->set_multicast_list = efx_set_multicast_list;
#ifdef CONFIG_NET_POLL_CONTROLLER
	net_dev->poll_controller = efx_netpoll;
#endif
	SET_NETDEV_DEV(net_dev, &efx->pci_dev->dev);
	SET_ETHTOOL_OPS(net_dev, &efx_ethtool_ops);

	/* Always start with carrier off; PHY events will detect the link */
	netif_carrier_off(efx->net_dev);

	/* Clear MAC statistics */
	falcon_update_stats_xmac(efx);
	memset(&efx->mac_stats, 0, sizeof(efx->mac_stats));

	rc = register_netdev(net_dev);
	if (rc) {
		EFX_ERR(efx, "could not register net dev\n");
		return rc;
	}
	strcpy(efx->name, net_dev->name);

	return 0;
}

static void efx_unregister_netdev(struct efx_nic *efx)
{
	struct efx_tx_queue *tx_queue;

	if (!efx->net_dev)
		return;

	BUG_ON(netdev_priv(efx->net_dev) != efx);

	/* Free up any skbs still remaining. This has to happen before
	 * we try to unregister the netdev as running their destructors
	 * may be needed to get the device ref. count to 0. */
	efx_for_each_tx_queue(tx_queue, efx)
		efx_release_tx_buffers(tx_queue);

	if (efx_dev_registered(efx)) {
		strlcpy(efx->name, pci_name(efx->pci_dev), sizeof(efx->name));
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
void efx_reset_down(struct efx_nic *efx, struct ethtool_cmd *ecmd)
{
	int rc;

	EFX_ASSERT_RESET_SERIALISED(efx);

	/* The net_dev->get_stats handler is quite slow, and will fail
	 * if a fetch is pending over reset. Serialise against it. */
	spin_lock(&efx->stats_lock);
	efx->stats_enabled = false;
	spin_unlock(&efx->stats_lock);

	efx_stop_all(efx);
	mutex_lock(&efx->mac_lock);

	rc = falcon_xmac_get_settings(efx, ecmd);
	if (rc)
		EFX_ERR(efx, "could not back up PHY settings\n");

	efx_fini_channels(efx);
}

/* This function will always ensure that the locks acquired in
 * efx_reset_down() are released. A failure return code indicates
 * that we were unable to reinitialise the hardware, and the
 * driver should be disabled. If ok is false, then the rx and tx
 * engines are not restarted, pending a RESET_DISABLE. */
int efx_reset_up(struct efx_nic *efx, struct ethtool_cmd *ecmd, bool ok)
{
	int rc;

	EFX_ASSERT_RESET_SERIALISED(efx);

	rc = falcon_init_nic(efx);
	if (rc) {
		EFX_ERR(efx, "failed to initialise NIC\n");
		ok = false;
	}

	if (ok) {
		efx_init_channels(efx);

		if (falcon_xmac_set_settings(efx, ecmd))
			EFX_ERR(efx, "could not restore PHY settings\n");
	}

	mutex_unlock(&efx->mac_lock);

	if (ok) {
		efx_start_all(efx);
		efx->stats_enabled = true;
	}
	return rc;
}

/* Reset the NIC as transparently as possible. Do not reset the PHY
 * Note that the reset may fail, in which case the card will be left
 * in a most-probably-unusable state.
 *
 * This function will sleep.  You cannot reset from within an atomic
 * state; use efx_schedule_reset() instead.
 *
 * Grabs the rtnl_lock.
 */
static int efx_reset(struct efx_nic *efx)
{
	struct ethtool_cmd ecmd;
	enum reset_type method = efx->reset_pending;
	int rc;

	/* Serialise with kernel interfaces */
	rtnl_lock();

	/* If we're not RUNNING then don't reset. Leave the reset_pending
	 * flag set so that efx_pci_probe_main will be retried */
	if (efx->state != STATE_RUNNING) {
		EFX_INFO(efx, "scheduled reset quenched. NIC not RUNNING\n");
		goto unlock_rtnl;
	}

	EFX_INFO(efx, "resetting (%d)\n", method);

	efx_reset_down(efx, &ecmd);

	rc = falcon_reset_hw(efx, method);
	if (rc) {
		EFX_ERR(efx, "failed to reset hardware\n");
		goto fail;
	}

	/* Allow resets to be rescheduled. */
	efx->reset_pending = RESET_TYPE_NONE;

	/* Reinitialise bus-mastering, which may have been turned off before
	 * the reset was scheduled. This is still appropriate, even in the
	 * RESET_TYPE_DISABLE since this driver generally assumes the hardware
	 * can respond to requests. */
	pci_set_master(efx->pci_dev);

	/* Leave device stopped if necessary */
	if (method == RESET_TYPE_DISABLE) {
		rc = -EIO;
		goto fail;
	}

	rc = efx_reset_up(efx, &ecmd, true);
	if (rc)
		goto disable;

	EFX_LOG(efx, "reset complete\n");
 unlock_rtnl:
	rtnl_unlock();
	return 0;

 fail:
	efx_reset_up(efx, &ecmd, false);
 disable:
	EFX_ERR(efx, "has been disabled\n");
	efx->state = STATE_DISABLED;

	rtnl_unlock();
	efx_unregister_netdev(efx);
	efx_fini_port(efx);
	return rc;
}

/* The worker thread exists so that code that cannot sleep can
 * schedule a reset for later.
 */
static void efx_reset_work(struct work_struct *data)
{
	struct efx_nic *nic = container_of(data, struct efx_nic, reset_work);

	efx_reset(nic);
}

void efx_schedule_reset(struct efx_nic *efx, enum reset_type type)
{
	enum reset_type method;

	if (efx->reset_pending != RESET_TYPE_NONE) {
		EFX_INFO(efx, "quenching already scheduled reset\n");
		return;
	}

	switch (type) {
	case RESET_TYPE_INVISIBLE:
	case RESET_TYPE_ALL:
	case RESET_TYPE_WORLD:
	case RESET_TYPE_DISABLE:
		method = type;
		break;
	case RESET_TYPE_RX_RECOVERY:
	case RESET_TYPE_RX_DESC_FETCH:
	case RESET_TYPE_TX_DESC_FETCH:
	case RESET_TYPE_TX_SKIP:
		method = RESET_TYPE_INVISIBLE;
		break;
	default:
		method = RESET_TYPE_ALL;
		break;
	}

	if (method != type)
		EFX_LOG(efx, "scheduling reset (%d:%d)\n", type, method);
	else
		EFX_LOG(efx, "scheduling reset (%d)\n", method);

	efx->reset_pending = method;

	queue_work(efx->reset_workqueue, &efx->reset_work);
}

/**************************************************************************
 *
 * List of NICs we support
 *
 **************************************************************************/

/* PCI device ID table */
static struct pci_device_id efx_pci_table[] __devinitdata = {
	{PCI_DEVICE(EFX_VENDID_SFC, FALCON_A_P_DEVID),
	 .driver_data = (unsigned long) &falcon_a_nic_type},
	{PCI_DEVICE(EFX_VENDID_SFC, FALCON_B_P_DEVID),
	 .driver_data = (unsigned long) &falcon_b_nic_type},
	{0}			/* end of list */
};

/**************************************************************************
 *
 * Dummy PHY/MAC/Board operations
 *
 * Can be used for some unimplemented operations
 * Needed so all function pointers are valid and do not have to be tested
 * before use
 *
 **************************************************************************/
int efx_port_dummy_op_int(struct efx_nic *efx)
{
	return 0;
}
void efx_port_dummy_op_void(struct efx_nic *efx) {}
void efx_port_dummy_op_blink(struct efx_nic *efx, bool blink) {}

static struct efx_phy_operations efx_dummy_phy_operations = {
	.init		 = efx_port_dummy_op_int,
	.reconfigure	 = efx_port_dummy_op_void,
	.check_hw        = efx_port_dummy_op_int,
	.fini		 = efx_port_dummy_op_void,
	.clear_interrupt = efx_port_dummy_op_void,
	.reset_xaui      = efx_port_dummy_op_void,
};

static struct efx_board efx_dummy_board_info = {
	.init		= efx_port_dummy_op_int,
	.init_leds	= efx_port_dummy_op_int,
	.set_fault_led	= efx_port_dummy_op_blink,
	.blink		= efx_port_dummy_op_blink,
	.fini		= efx_port_dummy_op_void,
};

/**************************************************************************
 *
 * Data housekeeping
 *
 **************************************************************************/

/* This zeroes out and then fills in the invariants in a struct
 * efx_nic (including all sub-structures).
 */
static int efx_init_struct(struct efx_nic *efx, struct efx_nic_type *type,
			   struct pci_dev *pci_dev, struct net_device *net_dev)
{
	struct efx_channel *channel;
	struct efx_tx_queue *tx_queue;
	struct efx_rx_queue *rx_queue;
	int i, rc;

	/* Initialise common structures */
	memset(efx, 0, sizeof(*efx));
	spin_lock_init(&efx->biu_lock);
	spin_lock_init(&efx->phy_lock);
	INIT_WORK(&efx->reset_work, efx_reset_work);
	INIT_DELAYED_WORK(&efx->monitor_work, efx_monitor);
	efx->pci_dev = pci_dev;
	efx->state = STATE_INIT;
	efx->reset_pending = RESET_TYPE_NONE;
	strlcpy(efx->name, pci_name(pci_dev), sizeof(efx->name));
	efx->board_info = efx_dummy_board_info;

	efx->net_dev = net_dev;
	efx->rx_checksum_enabled = true;
	spin_lock_init(&efx->netif_stop_lock);
	spin_lock_init(&efx->stats_lock);
	mutex_init(&efx->mac_lock);
	efx->phy_op = &efx_dummy_phy_operations;
	efx->mii.dev = net_dev;
	INIT_WORK(&efx->reconfigure_work, efx_reconfigure_work);
	atomic_set(&efx->netif_stop_count, 1);

	for (i = 0; i < EFX_MAX_CHANNELS; i++) {
		channel = &efx->channel[i];
		channel->efx = efx;
		channel->channel = i;
		channel->work_pending = false;
	}
	for (i = 0; i < EFX_TX_QUEUE_COUNT; i++) {
		tx_queue = &efx->tx_queue[i];
		tx_queue->efx = efx;
		tx_queue->queue = i;
		tx_queue->buffer = NULL;
		tx_queue->channel = &efx->channel[0]; /* for safety */
		tx_queue->tso_headers_free = NULL;
	}
	for (i = 0; i < EFX_MAX_RX_QUEUES; i++) {
		rx_queue = &efx->rx_queue[i];
		rx_queue->efx = efx;
		rx_queue->queue = i;
		rx_queue->channel = &efx->channel[0]; /* for safety */
		rx_queue->buffer = NULL;
		spin_lock_init(&rx_queue->add_lock);
		INIT_DELAYED_WORK(&rx_queue->work, efx_rx_work);
	}

	efx->type = type;

	/* Sanity-check NIC type */
	EFX_BUG_ON_PARANOID(efx->type->txd_ring_mask &
			    (efx->type->txd_ring_mask + 1));
	EFX_BUG_ON_PARANOID(efx->type->rxd_ring_mask &
			    (efx->type->rxd_ring_mask + 1));
	EFX_BUG_ON_PARANOID(efx->type->evq_size &
			    (efx->type->evq_size - 1));
	/* As close as we can get to guaranteeing that we don't overflow */
	EFX_BUG_ON_PARANOID(efx->type->evq_size <
			    (efx->type->txd_ring_mask + 1 +
			     efx->type->rxd_ring_mask + 1));
	EFX_BUG_ON_PARANOID(efx->type->phys_addr_channels > EFX_MAX_CHANNELS);

	/* Higher numbered interrupt modes are less capable! */
	efx->interrupt_mode = max(efx->type->max_interrupt_mode,
				  interrupt_mode);

	efx->workqueue = create_singlethread_workqueue("sfc_work");
	if (!efx->workqueue) {
		rc = -ENOMEM;
		goto fail1;
	}

	efx->reset_workqueue = create_singlethread_workqueue("sfc_reset");
	if (!efx->reset_workqueue) {
		rc = -ENOMEM;
		goto fail2;
	}

	return 0;

 fail2:
	destroy_workqueue(efx->workqueue);
	efx->workqueue = NULL;

 fail1:
	return rc;
}

static void efx_fini_struct(struct efx_nic *efx)
{
	if (efx->reset_workqueue) {
		destroy_workqueue(efx->reset_workqueue);
		efx->reset_workqueue = NULL;
	}
	if (efx->workqueue) {
		destroy_workqueue(efx->workqueue);
		efx->workqueue = NULL;
	}
}

/**************************************************************************
 *
 * PCI interface
 *
 **************************************************************************/

/* Main body of final NIC shutdown code
 * This is called only at module unload (or hotplug removal).
 */
static void efx_pci_remove_main(struct efx_nic *efx)
{
	EFX_ASSERT_RESET_SERIALISED(efx);

	/* Skip everything if we never obtained a valid membase */
	if (!efx->membase)
		return;

	efx_fini_channels(efx);
	efx_fini_port(efx);

	/* Shutdown the board, then the NIC and board state */
	efx->board_info.fini(efx);
	falcon_fini_interrupt(efx);

	efx_fini_napi(efx);
	efx_remove_all(efx);
}

/* Final NIC shutdown
 * This is called only at module unload (or hotplug removal).
 */
static void efx_pci_remove(struct pci_dev *pci_dev)
{
	struct efx_nic *efx;

	efx = pci_get_drvdata(pci_dev);
	if (!efx)
		return;

	/* Mark the NIC as fini, then stop the interface */
	rtnl_lock();
	efx->state = STATE_FINI;
	dev_close(efx->net_dev);

	/* Allow any queued efx_resets() to complete */
	rtnl_unlock();

	if (efx->membase == NULL)
		goto out;

	efx_unregister_netdev(efx);

	/* Wait for any scheduled resets to complete. No more will be
	 * scheduled from this point because efx_stop_all() has been
	 * called, we are no longer registered with driverlink, and
	 * the net_device's have been removed. */
	flush_workqueue(efx->reset_workqueue);

	efx_pci_remove_main(efx);

out:
	efx_fini_io(efx);
	EFX_LOG(efx, "shutdown successful\n");

	pci_set_drvdata(pci_dev, NULL);
	efx_fini_struct(efx);
	free_netdev(efx->net_dev);
};

/* Main body of NIC initialisation
 * This is called at module load (or hotplug insertion, theoretically).
 */
static int efx_pci_probe_main(struct efx_nic *efx)
{
	int rc;

	/* Do start-of-day initialisation */
	rc = efx_probe_all(efx);
	if (rc)
		goto fail1;

	rc = efx_init_napi(efx);
	if (rc)
		goto fail2;

	/* Initialise the board */
	rc = efx->board_info.init(efx);
	if (rc) {
		EFX_ERR(efx, "failed to initialise board\n");
		goto fail3;
	}

	rc = falcon_init_nic(efx);
	if (rc) {
		EFX_ERR(efx, "failed to initialise NIC\n");
		goto fail4;
	}

	rc = efx_init_port(efx);
	if (rc) {
		EFX_ERR(efx, "failed to initialise port\n");
		goto fail5;
	}

	efx_init_channels(efx);

	rc = falcon_init_interrupt(efx);
	if (rc)
		goto fail6;

	return 0;

 fail6:
	efx_fini_channels(efx);
	efx_fini_port(efx);
 fail5:
 fail4:
 fail3:
	efx_fini_napi(efx);
 fail2:
	efx_remove_all(efx);
 fail1:
	return rc;
}

/* NIC initialisation
 *
 * This is called at module load (or hotplug insertion,
 * theoretically).  It sets up PCI mappings, tests and resets the NIC,
 * sets up and registers the network devices with the kernel and hooks
 * the interrupt service routine.  It does not prepare the device for
 * transmission; this is left to the first time one of the network
 * interfaces is brought up (i.e. efx_net_open).
 */
static int __devinit efx_pci_probe(struct pci_dev *pci_dev,
				   const struct pci_device_id *entry)
{
	struct efx_nic_type *type = (struct efx_nic_type *) entry->driver_data;
	struct net_device *net_dev;
	struct efx_nic *efx;
	int i, rc;

	/* Allocate and initialise a struct net_device and struct efx_nic */
	net_dev = alloc_etherdev(sizeof(*efx));
	if (!net_dev)
		return -ENOMEM;
	net_dev->features |= (NETIF_F_IP_CSUM | NETIF_F_SG |
			      NETIF_F_HIGHDMA | NETIF_F_TSO);
	if (lro)
		net_dev->features |= NETIF_F_LRO;
	/* Mask for features that also apply to VLAN devices */
	net_dev->vlan_features |= (NETIF_F_ALL_CSUM | NETIF_F_SG |
				   NETIF_F_HIGHDMA | NETIF_F_TSO);
	efx = netdev_priv(net_dev);
	pci_set_drvdata(pci_dev, efx);
	rc = efx_init_struct(efx, type, pci_dev, net_dev);
	if (rc)
		goto fail1;

	EFX_INFO(efx, "Solarflare Communications NIC detected\n");

	/* Set up basic I/O (BAR mappings etc) */
	rc = efx_init_io(efx);
	if (rc)
		goto fail2;

	/* No serialisation is required with the reset path because
	 * we're in STATE_INIT. */
	for (i = 0; i < 5; i++) {
		rc = efx_pci_probe_main(efx);
		if (rc == 0)
			break;

		/* Serialise against efx_reset(). No more resets will be
		 * scheduled since efx_stop_all() has been called, and we
		 * have not and never have been registered with either
		 * the rtnetlink or driverlink layers. */
		flush_workqueue(efx->reset_workqueue);

		/* Retry if a recoverably reset event has been scheduled */
		if ((efx->reset_pending != RESET_TYPE_INVISIBLE) &&
		    (efx->reset_pending != RESET_TYPE_ALL))
			goto fail3;

		efx->reset_pending = RESET_TYPE_NONE;
	}

	if (rc) {
		EFX_ERR(efx, "Could not reset NIC\n");
		goto fail4;
	}

	/* Switch to the running state before we expose the device to
	 * the OS.  This is to ensure that the initial gathering of
	 * MAC stats succeeds. */
	rtnl_lock();
	efx->state = STATE_RUNNING;
	rtnl_unlock();

	rc = efx_register_netdev(efx);
	if (rc)
		goto fail5;

	EFX_LOG(efx, "initialisation successful\n");

	return 0;

 fail5:
	efx_pci_remove_main(efx);
 fail4:
 fail3:
	efx_fini_io(efx);
 fail2:
	efx_fini_struct(efx);
 fail1:
	EFX_LOG(efx, "initialisation failed. rc=%d\n", rc);
	free_netdev(net_dev);
	return rc;
}

static struct pci_driver efx_pci_driver = {
	.name		= EFX_DRIVER_NAME,
	.id_table	= efx_pci_table,
	.probe		= efx_pci_probe,
	.remove		= efx_pci_remove,
};

/**************************************************************************
 *
 * Kernel module interface
 *
 *************************************************************************/

module_param(interrupt_mode, uint, 0444);
MODULE_PARM_DESC(interrupt_mode,
		 "Interrupt mode (0=>MSIX 1=>MSI 2=>legacy)");

static int __init efx_init_module(void)
{
	int rc;

	printk(KERN_INFO "Solarflare NET driver v" EFX_DRIVER_VERSION "\n");

	rc = register_netdevice_notifier(&efx_netdev_notifier);
	if (rc)
		goto err_notifier;

	refill_workqueue = create_workqueue("sfc_refill");
	if (!refill_workqueue) {
		rc = -ENOMEM;
		goto err_refill;
	}

	rc = pci_register_driver(&efx_pci_driver);
	if (rc < 0)
		goto err_pci;

	return 0;

 err_pci:
	destroy_workqueue(refill_workqueue);
 err_refill:
	unregister_netdevice_notifier(&efx_netdev_notifier);
 err_notifier:
	return rc;
}

static void __exit efx_exit_module(void)
{
	printk(KERN_INFO "Solarflare NET driver unloading\n");

	pci_unregister_driver(&efx_pci_driver);
	destroy_workqueue(refill_workqueue);
	unregister_netdevice_notifier(&efx_netdev_notifier);

}

module_init(efx_init_module);
module_exit(efx_exit_module);

MODULE_AUTHOR("Michael Brown <mbrown@fensystems.co.uk> and "
	      "Solarflare Communications");
MODULE_DESCRIPTION("Solarflare Communications network driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, efx_pci_table);
