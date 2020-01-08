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
#include "efx_channels.h"
#include "efx.h"
#include "efx_common.h"
#include "tx_common.h"
#include "rx_common.h"
#include "nic.h"
#include "sriov.h"

/* This is the first interrupt mode to try out of:
 * 0 => MSI-X
 * 1 => MSI
 * 2 => legacy
 */
static unsigned int interrupt_mode;
module_param(interrupt_mode, uint, 0444);
MODULE_PARM_DESC(interrupt_mode,
		 "Interrupt mode (0=>MSIX 1=>MSI 2=>legacy)");

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

static unsigned int irq_adapt_low_thresh = 8000;
module_param(irq_adapt_low_thresh, uint, 0644);
MODULE_PARM_DESC(irq_adapt_low_thresh,
		 "Threshold score for reducing IRQ moderation");

static unsigned int irq_adapt_high_thresh = 16000;
module_param(irq_adapt_high_thresh, uint, 0644);
MODULE_PARM_DESC(irq_adapt_high_thresh,
		 "Threshold score for increasing IRQ moderation");

/* This is the weight assigned to each of the (per-channel) virtual
 * NAPI devices.
 */
static int napi_weight = 64;

/***************
 * Housekeeping
 ***************/

int efx_channel_dummy_op_int(struct efx_channel *channel)
{
	return 0;
}

void efx_channel_dummy_op_void(struct efx_channel *channel)
{
}

static const struct efx_channel_type efx_default_channel_type = {
	.pre_probe		= efx_channel_dummy_op_int,
	.post_remove		= efx_channel_dummy_op_void,
	.get_name		= efx_get_channel_name,
	.copy			= efx_copy_channel,
	.want_txqs		= efx_default_channel_want_txqs,
	.keep_eventq		= false,
	.want_pio		= true,
};

/*************
 * INTERRUPTS
 *************/

static unsigned int efx_wanted_parallelism(struct efx_nic *efx)
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

	if (count > EFX_MAX_RX_QUEUES) {
		netif_cond_dbg(efx, probe, efx->net_dev, !rss_cpus, warn,
			       "Reducing number of rx queues from %u to %u.\n",
			       count, EFX_MAX_RX_QUEUES);
		count = EFX_MAX_RX_QUEUES;
	}

	/* If RSS is requested for the PF *and* VFs then we can't write RSS
	 * table entries that are inaccessible to VFs
	 */
#ifdef CONFIG_SFC_SRIOV
	if (efx->type->sriov_wanted) {
		if (efx->type->sriov_wanted(efx) && efx_vf_size(efx) > 1 &&
		    count > efx_vf_size(efx)) {
			netif_warn(efx, probe, efx->net_dev,
				   "Reducing number of RSS channels from %u to %u for "
				   "VF support. Increase vf-msix-limit to use more "
				   "channels on the PF.\n",
				   count, efx_vf_size(efx));
			count = efx_vf_size(efx);
		}
	}
#endif

	return count;
}

static int efx_allocate_msix_channels(struct efx_nic *efx,
				      unsigned int max_channels,
				      unsigned int extra_channels,
				      unsigned int parallelism)
{
	unsigned int n_channels = parallelism;
	int vec_count;
	int n_xdp_tx;
	int n_xdp_ev;

	if (efx_separate_tx_channels)
		n_channels *= 2;
	n_channels += extra_channels;

	/* To allow XDP transmit to happen from arbitrary NAPI contexts
	 * we allocate a TX queue per CPU. We share event queues across
	 * multiple tx queues, assuming tx and ev queues are both
	 * maximum size.
	 */

	n_xdp_tx = num_possible_cpus();
	n_xdp_ev = DIV_ROUND_UP(n_xdp_tx, EFX_TXQ_TYPES);

	vec_count = pci_msix_vec_count(efx->pci_dev);
	if (vec_count < 0)
		return vec_count;

	max_channels = min_t(unsigned int, vec_count, max_channels);

	/* Check resources.
	 * We need a channel per event queue, plus a VI per tx queue.
	 * This may be more pessimistic than it needs to be.
	 */
	if (n_channels + n_xdp_ev > max_channels) {
		netif_err(efx, drv, efx->net_dev,
			  "Insufficient resources for %d XDP event queues (%d other channels, max %d)\n",
			  n_xdp_ev, n_channels, max_channels);
		efx->n_xdp_channels = 0;
		efx->xdp_tx_per_channel = 0;
		efx->xdp_tx_queue_count = 0;
	} else {
		efx->n_xdp_channels = n_xdp_ev;
		efx->xdp_tx_per_channel = EFX_TXQ_TYPES;
		efx->xdp_tx_queue_count = n_xdp_tx;
		n_channels += n_xdp_ev;
		netif_dbg(efx, drv, efx->net_dev,
			  "Allocating %d TX and %d event queues for XDP\n",
			  n_xdp_tx, n_xdp_ev);
	}

	if (vec_count < n_channels) {
		netif_err(efx, drv, efx->net_dev,
			  "WARNING: Insufficient MSI-X vectors available (%d < %u).\n",
			  vec_count, n_channels);
		netif_err(efx, drv, efx->net_dev,
			  "WARNING: Performance may be reduced.\n");
		n_channels = vec_count;
	}

	n_channels = min(n_channels, max_channels);

	efx->n_channels = n_channels;

	/* Ignore XDP tx channels when creating rx channels. */
	n_channels -= efx->n_xdp_channels;

	if (efx_separate_tx_channels) {
		efx->n_tx_channels =
			min(max(n_channels / 2, 1U),
			    efx->max_tx_channels);
		efx->tx_channel_offset =
			n_channels - efx->n_tx_channels;
		efx->n_rx_channels =
			max(n_channels -
			    efx->n_tx_channels, 1U);
	} else {
		efx->n_tx_channels = min(n_channels, efx->max_tx_channels);
		efx->tx_channel_offset = 0;
		efx->n_rx_channels = n_channels;
	}

	efx->n_rx_channels = min(efx->n_rx_channels, parallelism);
	efx->n_tx_channels = min(efx->n_tx_channels, parallelism);

	efx->xdp_channel_offset = n_channels;

	netif_dbg(efx, drv, efx->net_dev,
		  "Allocating %u RX channels\n",
		  efx->n_rx_channels);

	return efx->n_channels;
}

/* Probe the number and type of interrupts we are able to obtain, and
 * the resulting numbers of channels and RX queues.
 */
int efx_probe_interrupts(struct efx_nic *efx)
{
	unsigned int extra_channels = 0;
	unsigned int rss_spread;
	unsigned int i, j;
	int rc;

	for (i = 0; i < EFX_MAX_EXTRA_CHANNELS; i++)
		if (efx->extra_channel_type[i])
			++extra_channels;

	if (efx->interrupt_mode == EFX_INT_MODE_MSIX) {
		unsigned int parallelism = efx_wanted_parallelism(efx);
		struct msix_entry xentries[EFX_MAX_CHANNELS];
		unsigned int n_channels;

		rc = efx_allocate_msix_channels(efx, efx->max_channels,
						extra_channels, parallelism);
		if (rc >= 0) {
			n_channels = rc;
			for (i = 0; i < n_channels; i++)
				xentries[i].entry = i;
			rc = pci_enable_msix_range(efx->pci_dev, xentries, 1,
						   n_channels);
		}
		if (rc < 0) {
			/* Fall back to single channel MSI */
			netif_err(efx, drv, efx->net_dev,
				  "could not enable MSI-X\n");
			if (efx->type->min_interrupt_mode >= EFX_INT_MODE_MSI)
				efx->interrupt_mode = EFX_INT_MODE_MSI;
			else
				return rc;
		} else if (rc < n_channels) {
			netif_err(efx, drv, efx->net_dev,
				  "WARNING: Insufficient MSI-X vectors"
				  " available (%d < %u).\n", rc, n_channels);
			netif_err(efx, drv, efx->net_dev,
				  "WARNING: Performance may be reduced.\n");
			n_channels = rc;
		}

		if (rc > 0) {
			for (i = 0; i < efx->n_channels; i++)
				efx_get_channel(efx, i)->irq =
					xentries[i].vector;
		}
	}

	/* Try single interrupt MSI */
	if (efx->interrupt_mode == EFX_INT_MODE_MSI) {
		efx->n_channels = 1;
		efx->n_rx_channels = 1;
		efx->n_tx_channels = 1;
		efx->n_xdp_channels = 0;
		efx->xdp_channel_offset = efx->n_channels;
		rc = pci_enable_msi(efx->pci_dev);
		if (rc == 0) {
			efx_get_channel(efx, 0)->irq = efx->pci_dev->irq;
		} else {
			netif_err(efx, drv, efx->net_dev,
				  "could not enable MSI\n");
			if (efx->type->min_interrupt_mode >= EFX_INT_MODE_LEGACY)
				efx->interrupt_mode = EFX_INT_MODE_LEGACY;
			else
				return rc;
		}
	}

	/* Assume legacy interrupts */
	if (efx->interrupt_mode == EFX_INT_MODE_LEGACY) {
		efx->n_channels = 1 + (efx_separate_tx_channels ? 1 : 0);
		efx->n_rx_channels = 1;
		efx->n_tx_channels = 1;
		efx->n_xdp_channels = 0;
		efx->xdp_channel_offset = efx->n_channels;
		efx->legacy_irq = efx->pci_dev->irq;
	}

	/* Assign extra channels if possible, before XDP channels */
	efx->n_extra_tx_channels = 0;
	j = efx->xdp_channel_offset;
	for (i = 0; i < EFX_MAX_EXTRA_CHANNELS; i++) {
		if (!efx->extra_channel_type[i])
			continue;
		if (j <= efx->tx_channel_offset + efx->n_tx_channels) {
			efx->extra_channel_type[i]->handle_no_channel(efx);
		} else {
			--j;
			efx_get_channel(efx, j)->type =
				efx->extra_channel_type[i];
			if (efx_channel_has_tx_queues(efx_get_channel(efx, j)))
				efx->n_extra_tx_channels++;
		}
	}

	rss_spread = efx->n_rx_channels;
	/* RSS might be usable on VFs even if it is disabled on the PF */
#ifdef CONFIG_SFC_SRIOV
	if (efx->type->sriov_wanted) {
		efx->rss_spread = ((rss_spread > 1 ||
				    !efx->type->sriov_wanted(efx)) ?
				   rss_spread : efx_vf_size(efx));
		return 0;
	}
#endif
	efx->rss_spread = rss_spread;

	return 0;
}

#if defined(CONFIG_SMP)
void efx_set_interrupt_affinity(struct efx_nic *efx)
{
	struct efx_channel *channel;
	unsigned int cpu;

	efx_for_each_channel(channel, efx) {
		cpu = cpumask_local_spread(channel->channel,
					   pcibus_to_node(efx->pci_dev->bus));
		irq_set_affinity_hint(channel->irq, cpumask_of(cpu));
	}
}

void efx_clear_interrupt_affinity(struct efx_nic *efx)
{
	struct efx_channel *channel;

	efx_for_each_channel(channel, efx)
		irq_set_affinity_hint(channel->irq, NULL);
}
#else
void
efx_set_interrupt_affinity(struct efx_nic *efx __attribute__ ((unused)))
{
}

void
efx_clear_interrupt_affinity(struct efx_nic *efx __attribute__ ((unused)))
{
}
#endif /* CONFIG_SMP */

void efx_remove_interrupts(struct efx_nic *efx)
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

/**************************************************************************
 *
 * Channel handling
 *
 *************************************************************************/

/* Allocate and initialise a channel structure. */
struct efx_channel *
efx_alloc_channel(struct efx_nic *efx, int i, struct efx_channel *old_channel)
{
	struct efx_rx_queue *rx_queue;
	struct efx_tx_queue *tx_queue;
	struct efx_channel *channel;
	int j;

	channel = kzalloc(sizeof(*channel), GFP_KERNEL);
	if (!channel)
		return NULL;

	channel->efx = efx;
	channel->channel = i;
	channel->type = &efx_default_channel_type;

	for (j = 0; j < EFX_TXQ_TYPES; j++) {
		tx_queue = &channel->tx_queue[j];
		tx_queue->efx = efx;
		tx_queue->queue = i * EFX_TXQ_TYPES + j;
		tx_queue->channel = channel;
	}

#ifdef CONFIG_RFS_ACCEL
	INIT_DELAYED_WORK(&channel->filter_work, efx_filter_rfs_expire);
#endif

	rx_queue = &channel->rx_queue;
	rx_queue->efx = efx;
	timer_setup(&rx_queue->slow_fill, efx_rx_slow_fill, 0);

	return channel;
}

int efx_init_channels(struct efx_nic *efx)
{
	unsigned int i;

	for (i = 0; i < EFX_MAX_CHANNELS; i++) {
		efx->channel[i] = efx_alloc_channel(efx, i, NULL);
		if (!efx->channel[i])
			return -ENOMEM;
		efx->msi_context[i].efx = efx;
		efx->msi_context[i].index = i;
	}

	/* Higher numbered interrupt modes are less capable! */
	if (WARN_ON_ONCE(efx->type->max_interrupt_mode >
			 efx->type->min_interrupt_mode)) {
		return -EIO;
	}
	efx->interrupt_mode = max(efx->type->max_interrupt_mode,
				  interrupt_mode);
	efx->interrupt_mode = min(efx->type->min_interrupt_mode,
				  interrupt_mode);

	return 0;
}

void efx_fini_channels(struct efx_nic *efx)
{
	unsigned int i;

	for (i = 0; i < EFX_MAX_CHANNELS; i++)
		if (efx->channel[i]) {
			kfree(efx->channel[i]);
			efx->channel[i] = NULL;
		}
}

/* Allocate and initialise a channel structure, copying parameters
 * (but not resources) from an old channel structure.
 */
struct efx_channel *efx_copy_channel(const struct efx_channel *old_channel)
{
	struct efx_rx_queue *rx_queue;
	struct efx_tx_queue *tx_queue;
	struct efx_channel *channel;
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

	for (j = 0; j < EFX_TXQ_TYPES; j++) {
		tx_queue = &channel->tx_queue[j];
		if (tx_queue->channel)
			tx_queue->channel = channel;
		tx_queue->buffer = NULL;
		memset(&tx_queue->txd, 0, sizeof(tx_queue->txd));
	}

	rx_queue = &channel->rx_queue;
	rx_queue->buffer = NULL;
	memset(&rx_queue->rxd, 0, sizeof(rx_queue->rxd));
	timer_setup(&rx_queue->slow_fill, efx_rx_slow_fill, 0);
#ifdef CONFIG_RFS_ACCEL
	INIT_DELAYED_WORK(&channel->filter_work, efx_filter_rfs_expire);
#endif

	return channel;
}

static int efx_probe_channel(struct efx_channel *channel)
{
	struct efx_tx_queue *tx_queue;
	struct efx_rx_queue *rx_queue;
	int rc;

	netif_dbg(channel->efx, probe, channel->efx->net_dev,
		  "creating channel %d\n", channel->channel);

	rc = channel->type->pre_probe(channel);
	if (rc)
		goto fail;

	rc = efx_probe_eventq(channel);
	if (rc)
		goto fail;

	efx_for_each_channel_tx_queue(tx_queue, channel) {
		rc = efx_probe_tx_queue(tx_queue);
		if (rc)
			goto fail;
	}

	efx_for_each_channel_rx_queue(rx_queue, channel) {
		rc = efx_probe_rx_queue(rx_queue);
		if (rc)
			goto fail;
	}

	channel->rx_list = NULL;

	return 0;

fail:
	efx_remove_channel(channel);
	return rc;
}

void efx_get_channel_name(struct efx_channel *channel, char *buf, size_t len)
{
	struct efx_nic *efx = channel->efx;
	const char *type;
	int number;

	number = channel->channel;

	if (number >= efx->xdp_channel_offset &&
	    !WARN_ON_ONCE(!efx->n_xdp_channels)) {
		type = "-xdp";
		number -= efx->xdp_channel_offset;
	} else if (efx->tx_channel_offset == 0) {
		type = "";
	} else if (number < efx->tx_channel_offset) {
		type = "-rx";
	} else {
		type = "-tx";
		number -= efx->tx_channel_offset;
	}
	snprintf(buf, len, "%s%s-%d", efx->name, type, number);
}

void efx_set_channel_names(struct efx_nic *efx)
{
	struct efx_channel *channel;

	efx_for_each_channel(channel, efx)
		channel->type->get_name(channel,
					efx->msi_context[channel->channel].name,
					sizeof(efx->msi_context[0].name));
}

int efx_probe_channels(struct efx_nic *efx)
{
	struct efx_channel *channel;
	int rc;

	/* Restart special buffer allocation */
	efx->next_buffer_table = 0;

	/* Probe channels in reverse, so that any 'extra' channels
	 * use the start of the buffer table. This allows the traffic
	 * channels to be resized without moving them or wasting the
	 * entries before them.
	 */
	efx_for_each_channel_rev(channel, efx) {
		rc = efx_probe_channel(channel);
		if (rc) {
			netif_err(efx, probe, efx->net_dev,
				  "failed to create channel %d\n",
				  channel->channel);
			goto fail;
		}
	}
	efx_set_channel_names(efx);

	return 0;

fail:
	efx_remove_channels(efx);
	return rc;
}

void efx_remove_channel(struct efx_channel *channel)
{
	struct efx_tx_queue *tx_queue;
	struct efx_rx_queue *rx_queue;

	netif_dbg(channel->efx, drv, channel->efx->net_dev,
		  "destroy chan %d\n", channel->channel);

	efx_for_each_channel_rx_queue(rx_queue, channel)
		efx_remove_rx_queue(rx_queue);
	efx_for_each_possible_channel_tx_queue(tx_queue, channel)
		efx_remove_tx_queue(tx_queue);
	efx_remove_eventq(channel);
	channel->type->post_remove(channel);
}

void efx_remove_channels(struct efx_nic *efx)
{
	struct efx_channel *channel;

	efx_for_each_channel(channel, efx)
		efx_remove_channel(channel);

	kfree(efx->xdp_tx_queues);
}

int efx_realloc_channels(struct efx_nic *efx, u32 rxq_entries, u32 txq_entries)
{
	struct efx_channel *other_channel[EFX_MAX_CHANNELS], *channel;
	unsigned int i, next_buffer_table = 0;
	u32 old_rxq_entries, old_txq_entries;
	int rc, rc2;

	rc = efx_check_disabled(efx);
	if (rc)
		return rc;

	/* Not all channels should be reallocated. We must avoid
	 * reallocating their buffer table entries.
	 */
	efx_for_each_channel(channel, efx) {
		struct efx_rx_queue *rx_queue;
		struct efx_tx_queue *tx_queue;

		if (channel->type->copy)
			continue;
		next_buffer_table = max(next_buffer_table,
					channel->eventq.index +
					channel->eventq.entries);
		efx_for_each_channel_rx_queue(rx_queue, channel)
			next_buffer_table = max(next_buffer_table,
						rx_queue->rxd.index +
						rx_queue->rxd.entries);
		efx_for_each_channel_tx_queue(tx_queue, channel)
			next_buffer_table = max(next_buffer_table,
						tx_queue->txd.index +
						tx_queue->txd.entries);
	}

	efx_device_detach_sync(efx);
	efx_stop_all(efx);
	efx_soft_disable_interrupts(efx);

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
		rc = efx_probe_channel(channel);
		if (rc)
			goto rollback;
		efx_init_napi_channel(efx->channel[i]);
	}

out:
	/* Destroy unused channel structures */
	for (i = 0; i < efx->n_channels; i++) {
		channel = other_channel[i];
		if (channel && channel->type->copy) {
			efx_fini_napi_channel(channel);
			efx_remove_channel(channel);
			kfree(channel);
		}
	}

	rc2 = efx_soft_enable_interrupts(efx);
	if (rc2) {
		rc = rc ? rc : rc2;
		netif_err(efx, drv, efx->net_dev,
			  "unable to restart interrupts on channel reallocation\n");
		efx_schedule_reset(efx, RESET_TYPE_DISABLE);
	} else {
		efx_start_all(efx);
		efx_device_attach_if_not_resetting(efx);
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

int efx_set_channels(struct efx_nic *efx)
{
	struct efx_channel *channel;
	struct efx_tx_queue *tx_queue;
	int xdp_queue_number;

	efx->tx_channel_offset =
		efx_separate_tx_channels ?
		efx->n_channels - efx->n_tx_channels : 0;

	if (efx->xdp_tx_queue_count) {
		EFX_WARN_ON_PARANOID(efx->xdp_tx_queues);

		/* Allocate array for XDP TX queue lookup. */
		efx->xdp_tx_queues = kcalloc(efx->xdp_tx_queue_count,
					     sizeof(*efx->xdp_tx_queues),
					     GFP_KERNEL);
		if (!efx->xdp_tx_queues)
			return -ENOMEM;
	}

	/* We need to mark which channels really have RX and TX
	 * queues, and adjust the TX queue numbers if we have separate
	 * RX-only and TX-only channels.
	 */
	xdp_queue_number = 0;
	efx_for_each_channel(channel, efx) {
		if (channel->channel < efx->n_rx_channels)
			channel->rx_queue.core_index = channel->channel;
		else
			channel->rx_queue.core_index = -1;

		efx_for_each_channel_tx_queue(tx_queue, channel) {
			tx_queue->queue -= (efx->tx_channel_offset *
					    EFX_TXQ_TYPES);

			if (efx_channel_is_xdp_tx(channel) &&
			    xdp_queue_number < efx->xdp_tx_queue_count) {
				efx->xdp_tx_queues[xdp_queue_number] = tx_queue;
				xdp_queue_number++;
			}
		}
	}
	return 0;
}

bool efx_default_channel_want_txqs(struct efx_channel *channel)
{
	return channel->channel - channel->efx->tx_channel_offset <
		channel->efx->n_tx_channels;
}

/*************
 * START/STOP
 *************/

int efx_soft_enable_interrupts(struct efx_nic *efx)
{
	struct efx_channel *channel, *end_channel;
	int rc;

	BUG_ON(efx->state == STATE_DISABLED);

	efx->irq_soft_enabled = true;
	smp_wmb();

	efx_for_each_channel(channel, efx) {
		if (!channel->type->keep_eventq) {
			rc = efx_init_eventq(channel);
			if (rc)
				goto fail;
		}
		efx_start_eventq(channel);
	}

	efx_mcdi_mode_event(efx);

	return 0;
fail:
	end_channel = channel;
	efx_for_each_channel(channel, efx) {
		if (channel == end_channel)
			break;
		efx_stop_eventq(channel);
		if (!channel->type->keep_eventq)
			efx_fini_eventq(channel);
	}

	return rc;
}

void efx_soft_disable_interrupts(struct efx_nic *efx)
{
	struct efx_channel *channel;

	if (efx->state == STATE_DISABLED)
		return;

	efx_mcdi_mode_poll(efx);

	efx->irq_soft_enabled = false;
	smp_wmb();

	if (efx->legacy_irq)
		synchronize_irq(efx->legacy_irq);

	efx_for_each_channel(channel, efx) {
		if (channel->irq)
			synchronize_irq(channel->irq);

		efx_stop_eventq(channel);
		if (!channel->type->keep_eventq)
			efx_fini_eventq(channel);
	}

	/* Flush the asynchronous MCDI request queue */
	efx_mcdi_flush_async(efx);
}

int efx_enable_interrupts(struct efx_nic *efx)
{
	struct efx_channel *channel, *end_channel;
	int rc;

	/* TODO: Is this really a bug? */
	BUG_ON(efx->state == STATE_DISABLED);

	if (efx->eeh_disabled_legacy_irq) {
		enable_irq(efx->legacy_irq);
		efx->eeh_disabled_legacy_irq = false;
	}

	efx->type->irq_enable_master(efx);

	efx_for_each_channel(channel, efx) {
		if (channel->type->keep_eventq) {
			rc = efx_init_eventq(channel);
			if (rc)
				goto fail;
		}
	}

	rc = efx_soft_enable_interrupts(efx);
	if (rc)
		goto fail;

	return 0;

fail:
	end_channel = channel;
	efx_for_each_channel(channel, efx) {
		if (channel == end_channel)
			break;
		if (channel->type->keep_eventq)
			efx_fini_eventq(channel);
	}

	efx->type->irq_disable_non_ev(efx);

	return rc;
}

void efx_disable_interrupts(struct efx_nic *efx)
{
	struct efx_channel *channel;

	efx_soft_disable_interrupts(efx);

	efx_for_each_channel(channel, efx) {
		if (channel->type->keep_eventq)
			efx_fini_eventq(channel);
	}

	efx->type->irq_disable_non_ev(efx);
}

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

void efx_stop_channels(struct efx_nic *efx)
{
	struct efx_tx_queue *tx_queue;
	struct efx_rx_queue *rx_queue;
	struct efx_channel *channel;
	int rc;

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
}

/**************************************************************************
 *
 * NAPI interface
 *
 *************************************************************************/

/* Process channel's event queue
 *
 * This function is responsible for processing the event queue of a
 * single channel.  The caller must guarantee that this function will
 * never be concurrently called more than once on the same channel,
 * though different channels may be being processed concurrently.
 */
static int efx_process_channel(struct efx_channel *channel, int budget)
{
	struct efx_tx_queue *tx_queue;
	struct list_head rx_list;
	int spent;

	if (unlikely(!channel->enabled))
		return 0;

	/* Prepare the batch receive list */
	EFX_WARN_ON_PARANOID(channel->rx_list != NULL);
	INIT_LIST_HEAD(&rx_list);
	channel->rx_list = &rx_list;

	efx_for_each_channel_tx_queue(tx_queue, channel) {
		tx_queue->pkts_compl = 0;
		tx_queue->bytes_compl = 0;
	}

	spent = efx_nic_process_eventq(channel, budget);
	if (spent && efx_channel_has_rx_queue(channel)) {
		struct efx_rx_queue *rx_queue =
			efx_channel_get_rx_queue(channel);

		efx_rx_flush_packet(channel);
		efx_fast_push_rx_descriptors(rx_queue, true);
	}

	/* Update BQL */
	efx_for_each_channel_tx_queue(tx_queue, channel) {
		if (tx_queue->bytes_compl) {
			netdev_tx_completed_queue(tx_queue->core_txq,
						  tx_queue->pkts_compl,
						  tx_queue->bytes_compl);
		}
	}

	/* Receive any packets we queued up */
	netif_receive_skb_list(channel->rx_list);
	channel->rx_list = NULL;

	return spent;
}

static void efx_update_irq_mod(struct efx_nic *efx, struct efx_channel *channel)
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

/* NAPI poll handler
 *
 * NAPI guarantees serialisation of polls of the same device, which
 * provides the guarantee required by efx_process_channel().
 */
static int efx_poll(struct napi_struct *napi, int budget)
{
	struct efx_channel *channel =
		container_of(napi, struct efx_channel, napi_str);
	struct efx_nic *efx = channel->efx;
	int spent;

	netif_vdbg(efx, intr, efx->net_dev,
		   "channel %d NAPI poll executing on CPU %d\n",
		   channel->channel, raw_smp_processor_id());

	spent = efx_process_channel(channel, budget);

	xdp_do_flush_map();

	if (spent < budget) {
		if (efx_channel_has_rx_queue(channel) &&
		    efx->irq_rx_adaptive &&
		    unlikely(++channel->irq_count == 1000)) {
			efx_update_irq_mod(efx, channel);
		}

#ifdef CONFIG_RFS_ACCEL
		/* Perhaps expire some ARFS filters */
		mod_delayed_work(system_wq, &channel->filter_work, 0);
#endif

		/* There is no race here; although napi_disable() will
		 * only wait for napi_complete(), this isn't a problem
		 * since efx_nic_eventq_read_ack() will have no effect if
		 * interrupts have already been disabled.
		 */
		if (napi_complete_done(napi, spent))
			efx_nic_eventq_read_ack(channel);
	}

	return spent;
}

void efx_init_napi_channel(struct efx_channel *channel)
{
	struct efx_nic *efx = channel->efx;

	channel->napi_dev = efx->net_dev;
	netif_napi_add(channel->napi_dev, &channel->napi_str,
		       efx_poll, napi_weight);
}

void efx_init_napi(struct efx_nic *efx)
{
	struct efx_channel *channel;

	efx_for_each_channel(channel, efx)
		efx_init_napi_channel(channel);
}

void efx_fini_napi_channel(struct efx_channel *channel)
{
	if (channel->napi_dev)
		netif_napi_del(&channel->napi_str);

	channel->napi_dev = NULL;
}

void efx_fini_napi(struct efx_nic *efx)
{
	struct efx_channel *channel;

	efx_for_each_channel(channel, efx)
		efx_fini_napi_channel(channel);
}
