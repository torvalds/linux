// SPDX-License-Identifier: GPL-2.0-only
/*
 * core routines for the asynchronous memory transfer/transform api
 *
 * Copyright © 2006, Intel Corporation.
 *
 *	Dan Williams <dan.j.williams@intel.com>
 *
 *	with architecture considerations by:
 *	Neil Brown <neilb@suse.de>
 *	Jeff Garzik <jeff@garzik.org>
 */
#include <linux/rculist.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/async_tx.h>

#ifdef CONFIG_DMA_ENGINE
static int __init async_tx_init(void)
{
	async_dmaengine_get();

	printk(KERN_INFO "async_tx: api initialized (async)\n");

	return 0;
}

static void __exit async_tx_exit(void)
{
	async_dmaengine_put();
}

module_init(async_tx_init);
module_exit(async_tx_exit);

/**
 * __async_tx_find_channel - find a channel to carry out the operation or let
 *	the transaction execute synchronously
 * @submit: transaction dependency and submission modifiers
 * @tx_type: transaction type
 */
struct dma_chan *
__async_tx_find_channel(struct async_submit_ctl *submit,
			enum dma_transaction_type tx_type)
{
	struct dma_async_tx_descriptor *depend_tx = submit->depend_tx;

	/* see if we can keep the chain on one channel */
	if (depend_tx &&
	    dma_has_cap(tx_type, depend_tx->chan->device->cap_mask))
		return depend_tx->chan;
	return async_dma_find_channel(tx_type);
}
EXPORT_SYMBOL_GPL(__async_tx_find_channel);
#endif


/**
 * async_tx_channel_switch - queue an interrupt descriptor with a dependency
 * 	pre-attached.
 * @depend_tx: the operation that must finish before the new operation runs
 * @tx: the new operation
 */
static void
async_tx_channel_switch(struct dma_async_tx_descriptor *depend_tx,
			struct dma_async_tx_descriptor *tx)
{
	struct dma_chan *chan = depend_tx->chan;
	struct dma_device *device = chan->device;
	struct dma_async_tx_descriptor *intr_tx = (void *) ~0;

	/* first check to see if we can still append to depend_tx */
	txd_lock(depend_tx);
	if (txd_parent(depend_tx) && depend_tx->chan == tx->chan) {
		txd_chain(depend_tx, tx);
		intr_tx = NULL;
	}
	txd_unlock(depend_tx);

	/* attached dependency, flush the parent channel */
	if (!intr_tx) {
		device->device_issue_pending(chan);
		return;
	}

	/* see if we can schedule an interrupt
	 * otherwise poll for completion
	 */
	if (dma_has_cap(DMA_INTERRUPT, device->cap_mask))
		intr_tx = device->device_prep_dma_interrupt(chan, 0);
	else
		intr_tx = NULL;

	if (intr_tx) {
		intr_tx->callback = NULL;
		intr_tx->callback_param = NULL;
		/* safe to chain outside the lock since we know we are
		 * not submitted yet
		 */
		txd_chain(intr_tx, tx);

		/* check if we need to append */
		txd_lock(depend_tx);
		if (txd_parent(depend_tx)) {
			txd_chain(depend_tx, intr_tx);
			async_tx_ack(intr_tx);
			intr_tx = NULL;
		}
		txd_unlock(depend_tx);

		if (intr_tx) {
			txd_clear_parent(intr_tx);
			intr_tx->tx_submit(intr_tx);
			async_tx_ack(intr_tx);
		}
		device->device_issue_pending(chan);
	} else {
		if (dma_wait_for_async_tx(depend_tx) != DMA_COMPLETE)
			panic("%s: DMA error waiting for depend_tx\n",
			      __func__);
		tx->tx_submit(tx);
	}
}


/**
 * enum submit_disposition - flags for routing an incoming operation
 * @ASYNC_TX_SUBMITTED: we were able to append the new operation under the lock
 * @ASYNC_TX_CHANNEL_SWITCH: when the lock is dropped schedule a channel switch
 * @ASYNC_TX_DIRECT_SUBMIT: when the lock is dropped submit directly
 *
 * while holding depend_tx->lock we must avoid submitting new operations
 * to prevent a circular locking dependency with drivers that already
 * hold a channel lock when calling async_tx_run_dependencies.
 */
enum submit_disposition {
	ASYNC_TX_SUBMITTED,
	ASYNC_TX_CHANNEL_SWITCH,
	ASYNC_TX_DIRECT_SUBMIT,
};

void
async_tx_submit(struct dma_chan *chan, struct dma_async_tx_descriptor *tx,
		struct async_submit_ctl *submit)
{
	struct dma_async_tx_descriptor *depend_tx = submit->depend_tx;

	tx->callback = submit->cb_fn;
	tx->callback_param = submit->cb_param;

	if (depend_tx) {
		enum submit_disposition s;

		/* sanity check the dependency chain:
		 * 1/ if ack is already set then we cannot be sure
		 * we are referring to the correct operation
		 * 2/ dependencies are 1:1 i.e. two transactions can
		 * not depend on the same parent
		 */
		BUG_ON(async_tx_test_ack(depend_tx) || txd_next(depend_tx) ||
		       txd_parent(tx));

		/* the lock prevents async_tx_run_dependencies from missing
		 * the setting of ->next when ->parent != NULL
		 */
		txd_lock(depend_tx);
		if (txd_parent(depend_tx)) {
			/* we have a parent so we can not submit directly
			 * if we are staying on the same channel: append
			 * else: channel switch
			 */
			if (depend_tx->chan == chan) {
				txd_chain(depend_tx, tx);
				s = ASYNC_TX_SUBMITTED;
			} else
				s = ASYNC_TX_CHANNEL_SWITCH;
		} else {
			/* we do not have a parent so we may be able to submit
			 * directly if we are staying on the same channel
			 */
			if (depend_tx->chan == chan)
				s = ASYNC_TX_DIRECT_SUBMIT;
			else
				s = ASYNC_TX_CHANNEL_SWITCH;
		}
		txd_unlock(depend_tx);

		switch (s) {
		case ASYNC_TX_SUBMITTED:
			break;
		case ASYNC_TX_CHANNEL_SWITCH:
			async_tx_channel_switch(depend_tx, tx);
			break;
		case ASYNC_TX_DIRECT_SUBMIT:
			txd_clear_parent(tx);
			tx->tx_submit(tx);
			break;
		}
	} else {
		txd_clear_parent(tx);
		tx->tx_submit(tx);
	}

	if (submit->flags & ASYNC_TX_ACK)
		async_tx_ack(tx);

	if (depend_tx)
		async_tx_ack(depend_tx);
}
EXPORT_SYMBOL_GPL(async_tx_submit);

/**
 * async_trigger_callback - schedules the callback function to be run
 * @submit: submission and completion parameters
 *
 * honored flags: ASYNC_TX_ACK
 *
 * The callback is run after any dependent operations have completed.
 */
struct dma_async_tx_descriptor *
async_trigger_callback(struct async_submit_ctl *submit)
{
	struct dma_chan *chan;
	struct dma_device *device;
	struct dma_async_tx_descriptor *tx;
	struct dma_async_tx_descriptor *depend_tx = submit->depend_tx;

	if (depend_tx) {
		chan = depend_tx->chan;
		device = chan->device;

		/* see if we can schedule an interrupt
		 * otherwise poll for completion
		 */
		if (device && !dma_has_cap(DMA_INTERRUPT, device->cap_mask))
			device = NULL;

		tx = device ? device->device_prep_dma_interrupt(chan, 0) : NULL;
	} else
		tx = NULL;

	if (tx) {
		pr_debug("%s: (async)\n", __func__);

		async_tx_submit(chan, tx, submit);
	} else {
		pr_debug("%s: (sync)\n", __func__);

		/* wait for any prerequisite operations */
		async_tx_quiesce(&submit->depend_tx);

		async_tx_sync_epilog(submit);
	}

	return tx;
}
EXPORT_SYMBOL_GPL(async_trigger_callback);

/**
 * async_tx_quiesce - ensure tx is complete and freeable upon return
 * @tx: transaction to quiesce
 */
void async_tx_quiesce(struct dma_async_tx_descriptor **tx)
{
	if (*tx) {
		/* if ack is already set then we cannot be sure
		 * we are referring to the correct operation
		 */
		BUG_ON(async_tx_test_ack(*tx));
		if (dma_wait_for_async_tx(*tx) != DMA_COMPLETE)
			panic("%s: DMA error waiting for transaction\n",
			      __func__);
		async_tx_ack(*tx);
		*tx = NULL;
	}
}
EXPORT_SYMBOL_GPL(async_tx_quiesce);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Asynchronous Bulk Memory Transactions API");
MODULE_LICENSE("GPL");
