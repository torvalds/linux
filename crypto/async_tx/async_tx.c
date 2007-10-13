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
 * dma_cap_mask_all - enable iteration over all operation types
 */
static dma_cap_mask_t dma_cap_mask_all;

/**
 * chan_ref_percpu - tracks channel allocations per core/opertion
 */
struct chan_ref_percpu {
	struct dma_chan_ref *ref;
};

static int channel_table_initialized;
static struct chan_ref_percpu *channel_table[DMA_TX_TYPE_END];

/**
 * async_tx_lock - protect modification of async_tx_master_list and serialize
 *	rebalance operations
 */
static spinlock_t async_tx_lock;

static struct list_head
async_tx_master_list = LIST_HEAD_INIT(async_tx_master_list);

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

/* dma_wait_for_async_tx - spin wait for a transcation to complete
 * @tx: transaction to wait on
 */
enum dma_status
dma_wait_for_async_tx(struct dma_async_tx_descriptor *tx)
{
	enum dma_status status;
	struct dma_async_tx_descriptor *iter;
	struct dma_async_tx_descriptor *parent;

	if (!tx)
		return DMA_SUCCESS;

	/* poll through the dependency chain, return when tx is complete */
	do {
		iter = tx;

		/* find the root of the unsubmitted dependency chain */
		while (iter->cookie == -EBUSY) {
			parent = iter->parent;
			if (parent && parent->cookie == -EBUSY)
				iter = iter->parent;
			else
				break;
		}

		status = dma_sync_wait(iter->chan, iter->cookie);
	} while (status == DMA_IN_PROGRESS || (iter != tx));

	return status;
}
EXPORT_SYMBOL_GPL(dma_wait_for_async_tx);

/* async_tx_run_dependencies - helper routine for dma drivers to process
 *	(start) dependent operations on their target channel
 * @tx: transaction with dependencies
 */
void
async_tx_run_dependencies(struct dma_async_tx_descriptor *tx)
{
	struct dma_async_tx_descriptor *dep_tx, *_dep_tx;
	struct dma_device *dev;
	struct dma_chan *chan;

	list_for_each_entry_safe(dep_tx, _dep_tx, &tx->depend_list,
		depend_node) {
		chan = dep_tx->chan;
		dev = chan->device;
		/* we can't depend on ourselves */
		BUG_ON(chan == tx->chan);
		list_del(&dep_tx->depend_node);
		tx->tx_submit(dep_tx);

		/* we need to poke the engine as client code does not
		 * know about dependency submission events
		 */
		dev->device_issue_pending(chan);
	}
}
EXPORT_SYMBOL_GPL(async_tx_run_dependencies);

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

/**
 * get_chan_ref_by_cap - returns the nth channel of the given capability
 * 	defaults to returning the channel with the desired capability and the
 * 	lowest reference count if the index can not be satisfied
 * @cap: capability to match
 * @index: nth channel desired, passing -1 has the effect of forcing the
 *  default return value
 */
static struct dma_chan_ref *
get_chan_ref_by_cap(enum dma_transaction_type cap, int index)
{
	struct dma_chan_ref *ret_ref = NULL, *min_ref = NULL, *ref;

	rcu_read_lock();
	list_for_each_entry_rcu(ref, &async_tx_master_list, node)
		if (dma_has_cap(cap, ref->chan->device->cap_mask)) {
			if (!min_ref)
				min_ref = ref;
			else if (atomic_read(&ref->count) <
				atomic_read(&min_ref->count))
				min_ref = ref;

			if (index-- == 0) {
				ret_ref = ref;
				break;
			}
		}
	rcu_read_unlock();

	if (!ret_ref)
		ret_ref = min_ref;

	if (ret_ref)
		atomic_inc(&ret_ref->count);

	return ret_ref;
}

/**
 * async_tx_rebalance - redistribute the available channels, optimize
 * for cpu isolation in the SMP case, and opertaion isolation in the
 * uniprocessor case
 */
static void async_tx_rebalance(void)
{
	int cpu, cap, cpu_idx = 0;
	unsigned long flags;

	if (!channel_table_initialized)
		return;

	spin_lock_irqsave(&async_tx_lock, flags);

	/* undo the last distribution */
	for_each_dma_cap_mask(cap, dma_cap_mask_all)
		for_each_possible_cpu(cpu) {
			struct dma_chan_ref *ref =
				per_cpu_ptr(channel_table[cap], cpu)->ref;
			if (ref) {
				atomic_set(&ref->count, 0);
				per_cpu_ptr(channel_table[cap], cpu)->ref =
									NULL;
			}
		}

	for_each_dma_cap_mask(cap, dma_cap_mask_all)
		for_each_online_cpu(cpu) {
			struct dma_chan_ref *new;
			if (NR_CPUS > 1)
				new = get_chan_ref_by_cap(cap, cpu_idx++);
			else
				new = get_chan_ref_by_cap(cap, -1);

			per_cpu_ptr(channel_table[cap], cpu)->ref = new;
		}

	spin_unlock_irqrestore(&async_tx_lock, flags);
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
			/* keep a reference until async_tx is unloaded */
			dma_chan_get(chan);
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

		async_tx_rebalance();
		break;
	case DMA_RESOURCE_REMOVED:
		found = 0;
		spin_lock_irqsave(&async_tx_lock, flags);
		list_for_each_entry_rcu(ref, &async_tx_master_list, node)
			if (ref->chan == chan) {
				/* permit backing devices to go away */
				dma_chan_put(ref->chan);
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

		async_tx_rebalance();
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

static int __init
async_tx_init(void)
{
	enum dma_transaction_type cap;

	spin_lock_init(&async_tx_lock);
	bitmap_fill(dma_cap_mask_all.bits, DMA_TX_TYPE_END);

	/* an interrupt will never be an explicit operation type.
	 * clearing this bit prevents allocation to a slot in 'channel_table'
	 */
	clear_bit(DMA_INTERRUPT, dma_cap_mask_all.bits);

	for_each_dma_cap_mask(cap, dma_cap_mask_all) {
		channel_table[cap] = alloc_percpu(struct chan_ref_percpu);
		if (!channel_table[cap])
			goto err;
	}

	channel_table_initialized = 1;
	dma_async_client_register(&async_tx_dma);
	dma_async_client_chan_request(&async_tx_dma);

	printk(KERN_INFO "async_tx: api initialized (async)\n");

	return 0;
err:
	printk(KERN_ERR "async_tx: initialization failure\n");

	while (--cap >= 0)
		free_percpu(channel_table[cap]);

	return 1;
}

static void __exit async_tx_exit(void)
{
	enum dma_transaction_type cap;

	channel_table_initialized = 0;

	for_each_dma_cap_mask(cap, dma_cap_mask_all)
		if (channel_table[cap])
			free_percpu(channel_table[cap]);

	dma_async_client_unregister(&async_tx_dma);
}

/**
 * async_tx_find_channel - find a channel to carry out the operation or let
 *	the transaction execute synchronously
 * @depend_tx: transaction dependency
 * @tx_type: transaction type
 */
struct dma_chan *
async_tx_find_channel(struct dma_async_tx_descriptor *depend_tx,
	enum dma_transaction_type tx_type)
{
	/* see if we can keep the chain on one channel */
	if (depend_tx &&
		dma_has_cap(tx_type, depend_tx->chan->device->cap_mask))
		return depend_tx->chan;
	else if (likely(channel_table_initialized)) {
		struct dma_chan_ref *ref;
		int cpu = get_cpu();
		ref = per_cpu_ptr(channel_table[tx_type], cpu)->ref;
		put_cpu();
		return ref ? ref->chan : NULL;
	} else
		return NULL;
}
EXPORT_SYMBOL_GPL(async_tx_find_channel);
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

void
async_tx_submit(struct dma_chan *chan, struct dma_async_tx_descriptor *tx,
	enum async_tx_flags flags, struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_param)
{
	tx->callback = cb_fn;
	tx->callback_param = cb_param;

	/* set this new tx to run after depend_tx if:
	 * 1/ a dependency exists (depend_tx is !NULL)
	 * 2/ the tx can not be submitted to the current channel
	 */
	if (depend_tx && depend_tx->chan != chan) {
		/* if ack is already set then we cannot be sure
		 * we are referring to the correct operation
		 */
		BUG_ON(depend_tx->ack);

		tx->parent = depend_tx;
		spin_lock_bh(&depend_tx->lock);
		list_add_tail(&tx->depend_node, &depend_tx->depend_list);
		if (depend_tx->cookie == 0) {
			struct dma_chan *dep_chan = depend_tx->chan;
			struct dma_device *dep_dev = dep_chan->device;
			dep_dev->device_dependency_added(dep_chan);
		}
		spin_unlock_bh(&depend_tx->lock);

		/* schedule an interrupt to trigger the channel switch */
		async_trigger_callback(ASYNC_TX_ACK, depend_tx, NULL, NULL);
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

		tx = device ? device->device_prep_dma_interrupt(chan) : NULL;
	} else
		tx = NULL;

	if (tx) {
		pr_debug("%s: (async)\n", __FUNCTION__);

		async_tx_submit(chan, tx, flags, depend_tx, cb_fn, cb_param);
	} else {
		pr_debug("%s: (sync)\n", __FUNCTION__);

		/* wait for any prerequisite operations */
		if (depend_tx) {
			/* if ack is already set then we cannot be sure
			 * we are referring to the correct operation
			 */
			BUG_ON(depend_tx->ack);
			if (dma_wait_for_async_tx(depend_tx) == DMA_ERROR)
				panic("%s: DMA_ERROR waiting for depend_tx\n",
					__FUNCTION__);
		}

		async_tx_sync_epilog(flags, depend_tx, cb_fn, cb_param);
	}

	return tx;
}
EXPORT_SYMBOL_GPL(async_trigger_callback);

module_init(async_tx_init);
module_exit(async_tx_exit);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Asynchronous Bulk Memory Transactions API");
MODULE_LICENSE("GPL");
