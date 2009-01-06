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
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#include <linux/rculist.h>
#include <linux/kernel.h>
#include <linux/async_tx.h>

#ifdef CONFIG_DMA_ENGINE
static enum dma_state_client
dma_channel_add_remove(struct dma_client *client,
	struct dma_chan *chan, enum dma_state state);

static struct dma_client async_tx_dma = {
	.event_callback = dma_channel_add_remove,
	/* .cap_mask == 0 defaults to all channels */
};

/**
 * async_tx_lock - protect modification of async_tx_master_list and serialize
 *	rebalance operations
 */
static DEFINE_SPINLOCK(async_tx_lock);

static LIST_HEAD(async_tx_master_list);

/* async_tx_issue_pending_all - start all transactions on all channels */
void async_tx_issue_pending_all(void)
{
	struct dma_chan_ref *ref;

	rcu_read_lock();
	list_for_each_entry_rcu(ref, &async_tx_master_list, node)
		ref->chan->device->device_issue_pending(ref->chan);
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(async_tx_issue_pending_all);

static void
free_dma_chan_ref(struct rcu_head *rcu)
{
	struct dma_chan_ref *ref;
	ref = container_of(rcu, struct dma_chan_ref, rcu);
	kfree(ref);
}

static void
init_dma_chan_ref(struct dma_chan_ref *ref, struct dma_chan *chan)
{
	INIT_LIST_HEAD(&ref->node);
	INIT_RCU_HEAD(&ref->rcu);
	ref->chan = chan;
	atomic_set(&ref->count, 0);
}

static enum dma_state_client
dma_channel_add_remove(struct dma_client *client,
	struct dma_chan *chan, enum dma_state state)
{
	unsigned long found, flags;
	struct dma_chan_ref *master_ref, *ref;
	enum dma_state_client ack = DMA_DUP; /* default: take no action */

	switch (state) {
	case DMA_RESOURCE_AVAILABLE:
		found = 0;
		rcu_read_lock();
		list_for_each_entry_rcu(ref, &async_tx_master_list, node)
			if (ref->chan == chan) {
				found = 1;
				break;
			}
		rcu_read_unlock();

		pr_debug("async_tx: dma resource available [%s]\n",
			found ? "old" : "new");

		if (!found)
			ack = DMA_ACK;
		else
			break;

		/* add the channel to the generic management list */
		master_ref = kmalloc(sizeof(*master_ref), GFP_KERNEL);
		if (master_ref) {
			init_dma_chan_ref(master_ref, chan);
			spin_lock_irqsave(&async_tx_lock, flags);
			list_add_tail_rcu(&master_ref->node,
				&async_tx_master_list);
			spin_unlock_irqrestore(&async_tx_lock,
				flags);
		} else {
			printk(KERN_WARNING "async_tx: unable to create"
				" new master entry in response to"
				" a DMA_RESOURCE_ADDED event"
				" (-ENOMEM)\n");
			return 0;
		}
		break;
	case DMA_RESOURCE_REMOVED:
		found = 0;
		spin_lock_irqsave(&async_tx_lock, flags);
		list_for_each_entry(ref, &async_tx_master_list, node)
			if (ref->chan == chan) {
				list_del_rcu(&ref->node);
				call_rcu(&ref->rcu, free_dma_chan_ref);
				found = 1;
				break;
			}
		spin_unlock_irqrestore(&async_tx_lock, flags);

		pr_debug("async_tx: dma resource removed [%s]\n",
			found ? "ours" : "not ours");

		if (found)
			ack = DMA_ACK;
		else
			break;
		break;
	case DMA_RESOURCE_SUSPEND:
	case DMA_RESOURCE_RESUME:
		printk(KERN_WARNING "async_tx: does not support dma channel"
			" suspend/resume\n");
		break;
	default:
		BUG();
	}

	return ack;
}

static int __init async_tx_init(void)
{
	dma_async_client_register(&async_tx_dma);
	dma_async_client_chan_request(&async_tx_dma);

	printk(KERN_INFO "async_tx: api initialized (async)\n");

	return 0;
}

static void __exit async_tx_exit(void)
{
	dma_async_client_unregister(&async_tx_dma);
}

/**
 * __async_tx_find_channel - find a channel to carry out the operation or let
 *	the transaction execute synchronously
 * @depend_tx: transaction dependency
 * @tx_type: transaction type
 */
struct dma_chan *
__async_tx_find_channel(struct dma_async_tx_descriptor *depend_tx,
	enum dma_transaction_type tx_type)
{
	/* see if we can keep the chain on one channel */
	if (depend_tx &&
	    dma_has_cap(tx_type, depend_tx->chan->device->cap_mask))
		return depend_tx->chan;
	return dma_find_channel(tx_type);
}
EXPORT_SYMBOL_GPL(__async_tx_find_channel);
#else
static int __init async_tx_init(void)
{
	printk(KERN_INFO "async_tx: api initialized (sync-only)\n");
	return 0;
}

static void __exit async_tx_exit(void)
{
	do { } while (0);
}
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
	struct dma_chan *chan;
	struct dma_device *device;
	struct dma_async_tx_descriptor *intr_tx = (void *) ~0;

	/* first check to see if we can still append to depend_tx */
	spin_lock_bh(&depend_tx->lock);
	if (depend_tx->parent && depend_tx->chan == tx->chan) {
		tx->parent = depend_tx;
		depend_tx->next = tx;
		intr_tx = NULL;
	}
	spin_unlock_bh(&depend_tx->lock);

	if (!intr_tx)
		return;

	chan = depend_tx->chan;
	device = chan->device;

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
		tx->parent = intr_tx;
		/* safe to set ->next outside the lock since we know we are
		 * not submitted yet
		 */
		intr_tx->next = tx;

		/* check if we need to append */
		spin_lock_bh(&depend_tx->lock);
		if (depend_tx->parent) {
			intr_tx->parent = depend_tx;
			depend_tx->next = intr_tx;
			async_tx_ack(intr_tx);
			intr_tx = NULL;
		}
		spin_unlock_bh(&depend_tx->lock);

		if (intr_tx) {
			intr_tx->parent = NULL;
			intr_tx->tx_submit(intr_tx);
			async_tx_ack(intr_tx);
		}
	} else {
		if (dma_wait_for_async_tx(depend_tx) == DMA_ERROR)
			panic("%s: DMA_ERROR waiting for depend_tx\n",
			      __func__);
		tx->tx_submit(tx);
	}
}


/**
 * submit_disposition - while holding depend_tx->lock we must avoid submitting
 * 	new operations to prevent a circular locking dependency with
 * 	drivers that already hold a channel lock when calling
 * 	async_tx_run_dependencies.
 * @ASYNC_TX_SUBMITTED: we were able to append the new operation under the lock
 * @ASYNC_TX_CHANNEL_SWITCH: when the lock is dropped schedule a channel switch
 * @ASYNC_TX_DIRECT_SUBMIT: when the lock is dropped submit directly
 */
enum submit_disposition {
	ASYNC_TX_SUBMITTED,
	ASYNC_TX_CHANNEL_SWITCH,
	ASYNC_TX_DIRECT_SUBMIT,
};

void
async_tx_submit(struct dma_chan *chan, struct dma_async_tx_descriptor *tx,
	enum async_tx_flags flags, struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_param)
{
	tx->callback = cb_fn;
	tx->callback_param = cb_param;

	if (depend_tx) {
		enum submit_disposition s;

		/* sanity check the dependency chain:
		 * 1/ if ack is already set then we cannot be sure
		 * we are referring to the correct operation
		 * 2/ dependencies are 1:1 i.e. two transactions can
		 * not depend on the same parent
		 */
		BUG_ON(async_tx_test_ack(depend_tx) || depend_tx->next ||
		       tx->parent);

		/* the lock prevents async_tx_run_dependencies from missing
		 * the setting of ->next when ->parent != NULL
		 */
		spin_lock_bh(&depend_tx->lock);
		if (depend_tx->parent) {
			/* we have a parent so we can not submit directly
			 * if we are staying on the same channel: append
			 * else: channel switch
			 */
			if (depend_tx->chan == chan) {
				tx->parent = depend_tx;
				depend_tx->next = tx;
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
		spin_unlock_bh(&depend_tx->lock);

		switch (s) {
		case ASYNC_TX_SUBMITTED:
			break;
		case ASYNC_TX_CHANNEL_SWITCH:
			async_tx_channel_switch(depend_tx, tx);
			break;
		case ASYNC_TX_DIRECT_SUBMIT:
			tx->parent = NULL;
			tx->tx_submit(tx);
			break;
		}
	} else {
		tx->parent = NULL;
		tx->tx_submit(tx);
	}

	if (flags & ASYNC_TX_ACK)
		async_tx_ack(tx);

	if (depend_tx && (flags & ASYNC_TX_DEP_ACK))
		async_tx_ack(depend_tx);
}
EXPORT_SYMBOL_GPL(async_tx_submit);

/**
 * async_trigger_callback - schedules the callback function to be run after
 * any dependent operations have been completed.
 * @flags: ASYNC_TX_ACK, ASYNC_TX_DEP_ACK
 * @depend_tx: 'callback' requires the completion of this transaction
 * @cb_fn: function to call after depend_tx completes
 * @cb_param: parameter to pass to the callback routine
 */
struct dma_async_tx_descriptor *
async_trigger_callback(enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_param)
{
	struct dma_chan *chan;
	struct dma_device *device;
	struct dma_async_tx_descriptor *tx;

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

		async_tx_submit(chan, tx, flags, depend_tx, cb_fn, cb_param);
	} else {
		pr_debug("%s: (sync)\n", __func__);

		/* wait for any prerequisite operations */
		async_tx_quiesce(&depend_tx);

		async_tx_sync_epilog(cb_fn, cb_param);
	}

	return tx;
}
EXPORT_SYMBOL_GPL(async_trigger_callback);

/**
 * async_tx_quiesce - ensure tx is complete and freeable upon return
 * @tx - transaction to quiesce
 */
void async_tx_quiesce(struct dma_async_tx_descriptor **tx)
{
	if (*tx) {
		/* if ack is already set then we cannot be sure
		 * we are referring to the correct operation
		 */
		BUG_ON(async_tx_test_ack(*tx));
		if (dma_wait_for_async_tx(*tx) == DMA_ERROR)
			panic("DMA_ERROR waiting for transaction\n");
		async_tx_ack(*tx);
		*tx = NULL;
	}
}
EXPORT_SYMBOL_GPL(async_tx_quiesce);

module_init(async_tx_init);
module_exit(async_tx_exit);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Asynchronous Bulk Memory Transactions API");
MODULE_LICENSE("GPL");
