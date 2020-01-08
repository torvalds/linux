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
