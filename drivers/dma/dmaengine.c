/*
 * Copyright(c) 2004 - 2006 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING.
 */

/*
 * This code implements the DMA subsystem. It provides a HW-neutral interface
 * for other kernel code to use asynchronous memory copy capabilities,
 * if present, and allows different HW DMA drivers to register as providing
 * this capability.
 *
 * Due to the fact we are accelerating what is already a relatively fast
 * operation, the code goes to great lengths to avoid additional overhead,
 * such as locking.
 *
 * LOCKING:
 *
 * The subsystem keeps a global list of dma_device structs it is protected by a
 * mutex, dma_list_mutex.
 *
 * A subsystem can get access to a channel by calling dmaengine_get() followed
 * by dma_find_channel(), or if it has need for an exclusive channel it can call
 * dma_request_channel().  Once a channel is allocated a reference is taken
 * against its corresponding driver to disable removal.
 *
 * Each device has a channels list, which runs unlocked but is never modified
 * once the device is registered, it's just setup by the driver.
 *
 * See Documentation/dmaengine.txt for more details
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/hardirq.h>
#include <linux/spinlock.h>
#include <linux/percpu.h>
#include <linux/rcupdate.h>
#include <linux/mutex.h>
#include <linux/jiffies.h>
#include <linux/rculist.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/acpi_dma.h>
#include <linux/of_dma.h>
#include <linux/mempool.h>

static DEFINE_MUTEX(dma_list_mutex);
static DEFINE_IDR(dma_idr);
static LIST_HEAD(dma_device_list);
static long dmaengine_ref_count;

/* --- sysfs implementation --- */

/**
 * dev_to_dma_chan - convert a device pointer to the its sysfs container object
 * @dev - device node
 *
 * Must be called under dma_list_mutex
 */
static struct dma_chan *dev_to_dma_chan(struct device *dev)
{
	struct dma_chan_dev *chan_dev;

	chan_dev = container_of(dev, typeof(*chan_dev), device);
	return chan_dev->chan;
}

static ssize_t memcpy_count_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct dma_chan *chan;
	unsigned long count = 0;
	int i;
	int err;

	mutex_lock(&dma_list_mutex);
	chan = dev_to_dma_chan(dev);
	if (chan) {
		for_each_possible_cpu(i)
			count += per_cpu_ptr(chan->local, i)->memcpy_count;
		err = sprintf(buf, "%lu\n", count);
	} else
		err = -ENODEV;
	mutex_unlock(&dma_list_mutex);

	return err;
}
static DEVICE_ATTR_RO(memcpy_count);

static ssize_t bytes_transferred_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct dma_chan *chan;
	unsigned long count = 0;
	int i;
	int err;

	mutex_lock(&dma_list_mutex);
	chan = dev_to_dma_chan(dev);
	if (chan) {
		for_each_possible_cpu(i)
			count += per_cpu_ptr(chan->local, i)->bytes_transferred;
		err = sprintf(buf, "%lu\n", count);
	} else
		err = -ENODEV;
	mutex_unlock(&dma_list_mutex);

	return err;
}
static DEVICE_ATTR_RO(bytes_transferred);

static ssize_t in_use_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct dma_chan *chan;
	int err;

	mutex_lock(&dma_list_mutex);
	chan = dev_to_dma_chan(dev);
	if (chan)
		err = sprintf(buf, "%d\n", chan->client_count);
	else
		err = -ENODEV;
	mutex_unlock(&dma_list_mutex);

	return err;
}
static DEVICE_ATTR_RO(in_use);

static struct attribute *dma_dev_attrs[] = {
	&dev_attr_memcpy_count.attr,
	&dev_attr_bytes_transferred.attr,
	&dev_attr_in_use.attr,
	NULL,
};
ATTRIBUTE_GROUPS(dma_dev);

static void chan_dev_release(struct device *dev)
{
	struct dma_chan_dev *chan_dev;

	chan_dev = container_of(dev, typeof(*chan_dev), device);
	if (atomic_dec_and_test(chan_dev->idr_ref)) {
		mutex_lock(&dma_list_mutex);
		idr_remove(&dma_idr, chan_dev->dev_id);
		mutex_unlock(&dma_list_mutex);
		kfree(chan_dev->idr_ref);
	}
	kfree(chan_dev);
}

static struct class dma_devclass = {
	.name		= "dma",
	.dev_groups	= dma_dev_groups,
	.dev_release	= chan_dev_release,
};

/* --- client and device registration --- */

#define dma_device_satisfies_mask(device, mask) \
	__dma_device_satisfies_mask((device), &(mask))
static int
__dma_device_satisfies_mask(struct dma_device *device,
			    const dma_cap_mask_t *want)
{
	dma_cap_mask_t has;

	bitmap_and(has.bits, want->bits, device->cap_mask.bits,
		DMA_TX_TYPE_END);
	return bitmap_equal(want->bits, has.bits, DMA_TX_TYPE_END);
}

static struct module *dma_chan_to_owner(struct dma_chan *chan)
{
	return chan->device->dev->driver->owner;
}

/**
 * balance_ref_count - catch up the channel reference count
 * @chan - channel to balance ->client_count versus dmaengine_ref_count
 *
 * balance_ref_count must be called under dma_list_mutex
 */
static void balance_ref_count(struct dma_chan *chan)
{
	struct module *owner = dma_chan_to_owner(chan);

	while (chan->client_count < dmaengine_ref_count) {
		__module_get(owner);
		chan->client_count++;
	}
}

/**
 * dma_chan_get - try to grab a dma channel's parent driver module
 * @chan - channel to grab
 *
 * Must be called under dma_list_mutex
 */
static int dma_chan_get(struct dma_chan *chan)
{
	int err = -ENODEV;
	struct module *owner = dma_chan_to_owner(chan);

	if (chan->client_count) {
		__module_get(owner);
		err = 0;
	} else if (try_module_get(owner))
		err = 0;

	if (err == 0)
		chan->client_count++;

	/* allocate upon first client reference */
	if (chan->client_count == 1 && err == 0) {
		int desc_cnt = chan->device->device_alloc_chan_resources(chan);

		if (desc_cnt < 0) {
			err = desc_cnt;
			chan->client_count = 0;
			module_put(owner);
		} else if (!dma_has_cap(DMA_PRIVATE, chan->device->cap_mask))
			balance_ref_count(chan);
	}

	return err;
}

/**
 * dma_chan_put - drop a reference to a dma channel's parent driver module
 * @chan - channel to release
 *
 * Must be called under dma_list_mutex
 */
static void dma_chan_put(struct dma_chan *chan)
{
	if (!chan->client_count)
		return; /* this channel failed alloc_chan_resources */
	chan->client_count--;
	module_put(dma_chan_to_owner(chan));
	if (chan->client_count == 0)
		chan->device->device_free_chan_resources(chan);
}

enum dma_status dma_sync_wait(struct dma_chan *chan, dma_cookie_t cookie)
{
	enum dma_status status;
	unsigned long dma_sync_wait_timeout = jiffies + msecs_to_jiffies(5000);

	dma_async_issue_pending(chan);
	do {
		status = dma_async_is_tx_complete(chan, cookie, NULL, NULL);
		if (time_after_eq(jiffies, dma_sync_wait_timeout)) {
			pr_err("%s: timeout!\n", __func__);
			return DMA_ERROR;
		}
		if (status != DMA_IN_PROGRESS)
			break;
		cpu_relax();
	} while (1);

	return status;
}
EXPORT_SYMBOL(dma_sync_wait);

/**
 * dma_cap_mask_all - enable iteration over all operation types
 */
static dma_cap_mask_t dma_cap_mask_all;

/**
 * dma_chan_tbl_ent - tracks channel allocations per core/operation
 * @chan - associated channel for this entry
 */
struct dma_chan_tbl_ent {
	struct dma_chan *chan;
};

/**
 * channel_table - percpu lookup table for memory-to-memory offload providers
 */
static struct dma_chan_tbl_ent __percpu *channel_table[DMA_TX_TYPE_END];

static int __init dma_channel_table_init(void)
{
	enum dma_transaction_type cap;
	int err = 0;

	bitmap_fill(dma_cap_mask_all.bits, DMA_TX_TYPE_END);

	/* 'interrupt', 'private', and 'slave' are channel capabilities,
	 * but are not associated with an operation so they do not need
	 * an entry in the channel_table
	 */
	clear_bit(DMA_INTERRUPT, dma_cap_mask_all.bits);
	clear_bit(DMA_PRIVATE, dma_cap_mask_all.bits);
	clear_bit(DMA_SLAVE, dma_cap_mask_all.bits);

	for_each_dma_cap_mask(cap, dma_cap_mask_all) {
		channel_table[cap] = alloc_percpu(struct dma_chan_tbl_ent);
		if (!channel_table[cap]) {
			err = -ENOMEM;
			break;
		}
	}

	if (err) {
		pr_err("initialization failure\n");
		for_each_dma_cap_mask(cap, dma_cap_mask_all)
			if (channel_table[cap])
				free_percpu(channel_table[cap]);
	}

	return err;
}
arch_initcall(dma_channel_table_init);

/**
 * dma_find_channel - find a channel to carry out the operation
 * @tx_type: transaction type
 */
struct dma_chan *dma_find_channel(enum dma_transaction_type tx_type)
{
	return this_cpu_read(channel_table[tx_type]->chan);
}
EXPORT_SYMBOL(dma_find_channel);

/*
 * net_dma_find_channel - find a channel for net_dma
 * net_dma has alignment requirements
 */
struct dma_chan *net_dma_find_channel(void)
{
	struct dma_chan *chan = dma_find_channel(DMA_MEMCPY);
	if (chan && !is_dma_copy_aligned(chan->device, 1, 1, 1))
		return NULL;

	return chan;
}
EXPORT_SYMBOL(net_dma_find_channel);

/**
 * dma_issue_pending_all - flush all pending operations across all channels
 */
void dma_issue_pending_all(void)
{
	struct dma_device *device;
	struct dma_chan *chan;

	rcu_read_lock();
	list_for_each_entry_rcu(device, &dma_device_list, global_node) {
		if (dma_has_cap(DMA_PRIVATE, device->cap_mask))
			continue;
		list_for_each_entry(chan, &device->channels, device_node)
			if (chan->client_count)
				device->device_issue_pending(chan);
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL(dma_issue_pending_all);

/**
 * dma_chan_is_local - returns true if the channel is in the same numa-node as the cpu
 */
static bool dma_chan_is_local(struct dma_chan *chan, int cpu)
{
	int node = dev_to_node(chan->device->dev);
	return node == -1 || cpumask_test_cpu(cpu, cpumask_of_node(node));
}

/**
 * min_chan - returns the channel with min count and in the same numa-node as the cpu
 * @cap: capability to match
 * @cpu: cpu index which the channel should be close to
 *
 * If some channels are close to the given cpu, the one with the lowest
 * reference count is returned. Otherwise, cpu is ignored and only the
 * reference count is taken into account.
 * Must be called under dma_list_mutex.
 */
static struct dma_chan *min_chan(enum dma_transaction_type cap, int cpu)
{
	struct dma_device *device;
	struct dma_chan *chan;
	struct dma_chan *min = NULL;
	struct dma_chan *localmin = NULL;

	list_for_each_entry(device, &dma_device_list, global_node) {
		if (!dma_has_cap(cap, device->cap_mask) ||
		    dma_has_cap(DMA_PRIVATE, device->cap_mask))
			continue;
		list_for_each_entry(chan, &device->channels, device_node) {
			if (!chan->client_count)
				continue;
			if (!min || chan->table_count < min->table_count)
				min = chan;

			if (dma_chan_is_local(chan, cpu))
				if (!localmin ||
				    chan->table_count < localmin->table_count)
					localmin = chan;
		}
	}

	chan = localmin ? localmin : min;

	if (chan)
		chan->table_count++;

	return chan;
}

/**
 * dma_channel_rebalance - redistribute the available channels
 *
 * Optimize for cpu isolation (each cpu gets a dedicated channel for an
 * operation type) in the SMP case,  and operation isolation (avoid
 * multi-tasking channels) in the non-SMP case.  Must be called under
 * dma_list_mutex.
 */
static void dma_channel_rebalance(void)
{
	struct dma_chan *chan;
	struct dma_device *device;
	int cpu;
	int cap;

	/* undo the last distribution */
	for_each_dma_cap_mask(cap, dma_cap_mask_all)
		for_each_possible_cpu(cpu)
			per_cpu_ptr(channel_table[cap], cpu)->chan = NULL;

	list_for_each_entry(device, &dma_device_list, global_node) {
		if (dma_has_cap(DMA_PRIVATE, device->cap_mask))
			continue;
		list_for_each_entry(chan, &device->channels, device_node)
			chan->table_count = 0;
	}

	/* don't populate the channel_table if no clients are available */
	if (!dmaengine_ref_count)
		return;

	/* redistribute available channels */
	for_each_dma_cap_mask(cap, dma_cap_mask_all)
		for_each_online_cpu(cpu) {
			chan = min_chan(cap, cpu);
			per_cpu_ptr(channel_table[cap], cpu)->chan = chan;
		}
}

static struct dma_chan *private_candidate(const dma_cap_mask_t *mask,
					  struct dma_device *dev,
					  dma_filter_fn fn, void *fn_param)
{
	struct dma_chan *chan;

	if (!__dma_device_satisfies_mask(dev, mask)) {
		pr_debug("%s: wrong capabilities\n", __func__);
		return NULL;
	}
	/* devices with multiple channels need special handling as we need to
	 * ensure that all channels are either private or public.
	 */
	if (dev->chancnt > 1 && !dma_has_cap(DMA_PRIVATE, dev->cap_mask))
		list_for_each_entry(chan, &dev->channels, device_node) {
			/* some channels are already publicly allocated */
			if (chan->client_count)
				return NULL;
		}

	list_for_each_entry(chan, &dev->channels, device_node) {
		if (chan->client_count) {
			pr_debug("%s: %s busy\n",
				 __func__, dma_chan_name(chan));
			continue;
		}
		if (fn && !fn(chan, fn_param)) {
			pr_debug("%s: %s filter said false\n",
				 __func__, dma_chan_name(chan));
			continue;
		}
		return chan;
	}

	return NULL;
}

/**
 * dma_request_slave_channel - try to get specific channel exclusively
 * @chan: target channel
 */
struct dma_chan *dma_get_slave_channel(struct dma_chan *chan)
{
	int err = -EBUSY;

	/* lock against __dma_request_channel */
	mutex_lock(&dma_list_mutex);

	if (chan->client_count == 0) {
		err = dma_chan_get(chan);
		if (err)
			pr_debug("%s: failed to get %s: (%d)\n",
				__func__, dma_chan_name(chan), err);
	} else
		chan = NULL;

	mutex_unlock(&dma_list_mutex);


	return chan;
}
EXPORT_SYMBOL_GPL(dma_get_slave_channel);

struct dma_chan *dma_get_any_slave_channel(struct dma_device *device)
{
	dma_cap_mask_t mask;
	struct dma_chan *chan;
	int err;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	/* lock against __dma_request_channel */
	mutex_lock(&dma_list_mutex);

	chan = private_candidate(&mask, device, NULL, NULL);
	if (chan) {
		err = dma_chan_get(chan);
		if (err) {
			pr_debug("%s: failed to get %s: (%d)\n",
				__func__, dma_chan_name(chan), err);
			chan = NULL;
		}
	}

	mutex_unlock(&dma_list_mutex);

	return chan;
}
EXPORT_SYMBOL_GPL(dma_get_any_slave_channel);

/**
 * __dma_request_channel - try to allocate an exclusive channel
 * @mask: capabilities that the channel must satisfy
 * @fn: optional callback to disposition available channels
 * @fn_param: opaque parameter to pass to dma_filter_fn
 *
 * Returns pointer to appropriate DMA channel on success or NULL.
 */
struct dma_chan *__dma_request_channel(const dma_cap_mask_t *mask,
				       dma_filter_fn fn, void *fn_param)
{
	struct dma_device *device, *_d;
	struct dma_chan *chan = NULL;
	int err;

	/* Find a channel */
	mutex_lock(&dma_list_mutex);
	list_for_each_entry_safe(device, _d, &dma_device_list, global_node) {
		chan = private_candidate(mask, device, fn, fn_param);
		if (chan) {
			/* Found a suitable channel, try to grab, prep, and
			 * return it.  We first set DMA_PRIVATE to disable
			 * balance_ref_count as this channel will not be
			 * published in the general-purpose allocator
			 */
			dma_cap_set(DMA_PRIVATE, device->cap_mask);
			device->privatecnt++;
			err = dma_chan_get(chan);

			if (err == -ENODEV) {
				pr_debug("%s: %s module removed\n",
					 __func__, dma_chan_name(chan));
				list_del_rcu(&device->global_node);
			} else if (err)
				pr_debug("%s: failed to get %s: (%d)\n",
					 __func__, dma_chan_name(chan), err);
			else
				break;
			if (--device->privatecnt == 0)
				dma_cap_clear(DMA_PRIVATE, device->cap_mask);
			chan = NULL;
		}
	}
	mutex_unlock(&dma_list_mutex);

	pr_debug("%s: %s (%s)\n",
		 __func__,
		 chan ? "success" : "fail",
		 chan ? dma_chan_name(chan) : NULL);

	return chan;
}
EXPORT_SYMBOL_GPL(__dma_request_channel);

/**
 * dma_request_slave_channel - try to allocate an exclusive slave channel
 * @dev:	pointer to client device structure
 * @name:	slave channel name
 *
 * Returns pointer to appropriate DMA channel on success or an error pointer.
 */
struct dma_chan *dma_request_slave_channel_reason(struct device *dev,
						  const char *name)
{
	/* If device-tree is present get slave info from here */
	if (dev->of_node)
		return of_dma_request_slave_channel(dev->of_node, name);

	/* If device was enumerated by ACPI get slave info from here */
	if (ACPI_HANDLE(dev))
		return acpi_dma_request_slave_chan_by_name(dev, name);

	return ERR_PTR(-ENODEV);
}
EXPORT_SYMBOL_GPL(dma_request_slave_channel_reason);

/**
 * dma_request_slave_channel - try to allocate an exclusive slave channel
 * @dev:	pointer to client device structure
 * @name:	slave channel name
 *
 * Returns pointer to appropriate DMA channel on success or NULL.
 */
struct dma_chan *dma_request_slave_channel(struct device *dev,
					   const char *name)
{
	struct dma_chan *ch = dma_request_slave_channel_reason(dev, name);
	if (IS_ERR(ch))
		return NULL;
	return ch;
}
EXPORT_SYMBOL_GPL(dma_request_slave_channel);

void dma_release_channel(struct dma_chan *chan)
{
	mutex_lock(&dma_list_mutex);
	WARN_ONCE(chan->client_count != 1,
		  "chan reference count %d != 1\n", chan->client_count);
	dma_chan_put(chan);
	/* drop PRIVATE cap enabled by __dma_request_channel() */
	if (--chan->device->privatecnt == 0)
		dma_cap_clear(DMA_PRIVATE, chan->device->cap_mask);
	mutex_unlock(&dma_list_mutex);
}
EXPORT_SYMBOL_GPL(dma_release_channel);

/**
 * dmaengine_get - register interest in dma_channels
 */
void dmaengine_get(void)
{
	struct dma_device *device, *_d;
	struct dma_chan *chan;
	int err;

	mutex_lock(&dma_list_mutex);
	dmaengine_ref_count++;

	/* try to grab channels */
	list_for_each_entry_safe(device, _d, &dma_device_list, global_node) {
		if (dma_has_cap(DMA_PRIVATE, device->cap_mask))
			continue;
		list_for_each_entry(chan, &device->channels, device_node) {
			err = dma_chan_get(chan);
			if (err == -ENODEV) {
				/* module removed before we could use it */
				list_del_rcu(&device->global_node);
				break;
			} else if (err)
				pr_debug("%s: failed to get %s: (%d)\n",
				       __func__, dma_chan_name(chan), err);
		}
	}

	/* if this is the first reference and there were channels
	 * waiting we need to rebalance to get those channels
	 * incorporated into the channel table
	 */
	if (dmaengine_ref_count == 1)
		dma_channel_rebalance();
	mutex_unlock(&dma_list_mutex);
}
EXPORT_SYMBOL(dmaengine_get);

/**
 * dmaengine_put - let dma drivers be removed when ref_count == 0
 */
void dmaengine_put(void)
{
	struct dma_device *device;
	struct dma_chan *chan;

	mutex_lock(&dma_list_mutex);
	dmaengine_ref_count--;
	BUG_ON(dmaengine_ref_count < 0);
	/* drop channel references */
	list_for_each_entry(device, &dma_device_list, global_node) {
		if (dma_has_cap(DMA_PRIVATE, device->cap_mask))
			continue;
		list_for_each_entry(chan, &device->channels, device_node)
			dma_chan_put(chan);
	}
	mutex_unlock(&dma_list_mutex);
}
EXPORT_SYMBOL(dmaengine_put);

static bool device_has_all_tx_types(struct dma_device *device)
{
	/* A device that satisfies this test has channels that will never cause
	 * an async_tx channel switch event as all possible operation types can
	 * be handled.
	 */
	#ifdef CONFIG_ASYNC_TX_DMA
	if (!dma_has_cap(DMA_INTERRUPT, device->cap_mask))
		return false;
	#endif

	#if defined(CONFIG_ASYNC_MEMCPY) || defined(CONFIG_ASYNC_MEMCPY_MODULE)
	if (!dma_has_cap(DMA_MEMCPY, device->cap_mask))
		return false;
	#endif

	#if defined(CONFIG_ASYNC_XOR) || defined(CONFIG_ASYNC_XOR_MODULE)
	if (!dma_has_cap(DMA_XOR, device->cap_mask))
		return false;

	#ifndef CONFIG_ASYNC_TX_DISABLE_XOR_VAL_DMA
	if (!dma_has_cap(DMA_XOR_VAL, device->cap_mask))
		return false;
	#endif
	#endif

	#if defined(CONFIG_ASYNC_PQ) || defined(CONFIG_ASYNC_PQ_MODULE)
	if (!dma_has_cap(DMA_PQ, device->cap_mask))
		return false;

	#ifndef CONFIG_ASYNC_TX_DISABLE_PQ_VAL_DMA
	if (!dma_has_cap(DMA_PQ_VAL, device->cap_mask))
		return false;
	#endif
	#endif

	return true;
}

static int get_dma_id(struct dma_device *device)
{
	int rc;

	mutex_lock(&dma_list_mutex);

	rc = idr_alloc(&dma_idr, NULL, 0, 0, GFP_KERNEL);
	if (rc >= 0)
		device->dev_id = rc;

	mutex_unlock(&dma_list_mutex);
	return rc < 0 ? rc : 0;
}

/**
 * dma_async_device_register - registers DMA devices found
 * @device: &dma_device
 */
int dma_async_device_register(struct dma_device *device)
{
	int chancnt = 0, rc;
	struct dma_chan* chan;
	atomic_t *idr_ref;

	if (!device)
		return -ENODEV;

	/* validate device routines */
	BUG_ON(dma_has_cap(DMA_MEMCPY, device->cap_mask) &&
		!device->device_prep_dma_memcpy);
	BUG_ON(dma_has_cap(DMA_XOR, device->cap_mask) &&
		!device->device_prep_dma_xor);
	BUG_ON(dma_has_cap(DMA_XOR_VAL, device->cap_mask) &&
		!device->device_prep_dma_xor_val);
	BUG_ON(dma_has_cap(DMA_PQ, device->cap_mask) &&
		!device->device_prep_dma_pq);
	BUG_ON(dma_has_cap(DMA_PQ_VAL, device->cap_mask) &&
		!device->device_prep_dma_pq_val);
	BUG_ON(dma_has_cap(DMA_INTERRUPT, device->cap_mask) &&
		!device->device_prep_dma_interrupt);
	BUG_ON(dma_has_cap(DMA_SG, device->cap_mask) &&
		!device->device_prep_dma_sg);
	BUG_ON(dma_has_cap(DMA_CYCLIC, device->cap_mask) &&
		!device->device_prep_dma_cyclic);
	BUG_ON(dma_has_cap(DMA_SLAVE, device->cap_mask) &&
		!device->device_control);
	BUG_ON(dma_has_cap(DMA_INTERLEAVE, device->cap_mask) &&
		!device->device_prep_interleaved_dma);

	BUG_ON(!device->device_alloc_chan_resources);
	BUG_ON(!device->device_free_chan_resources);
	BUG_ON(!device->device_tx_status);
	BUG_ON(!device->device_issue_pending);
	BUG_ON(!device->dev);

	/* note: this only matters in the
	 * CONFIG_ASYNC_TX_ENABLE_CHANNEL_SWITCH=n case
	 */
	if (device_has_all_tx_types(device))
		dma_cap_set(DMA_ASYNC_TX, device->cap_mask);

	idr_ref = kmalloc(sizeof(*idr_ref), GFP_KERNEL);
	if (!idr_ref)
		return -ENOMEM;
	rc = get_dma_id(device);
	if (rc != 0) {
		kfree(idr_ref);
		return rc;
	}

	atomic_set(idr_ref, 0);

	/* represent channels in sysfs. Probably want devs too */
	list_for_each_entry(chan, &device->channels, device_node) {
		rc = -ENOMEM;
		chan->local = alloc_percpu(typeof(*chan->local));
		if (chan->local == NULL)
			goto err_out;
		chan->dev = kzalloc(sizeof(*chan->dev), GFP_KERNEL);
		if (chan->dev == NULL) {
			free_percpu(chan->local);
			chan->local = NULL;
			goto err_out;
		}

		chan->chan_id = chancnt++;
		chan->dev->device.class = &dma_devclass;
		chan->dev->device.parent = device->dev;
		chan->dev->chan = chan;
		chan->dev->idr_ref = idr_ref;
		chan->dev->dev_id = device->dev_id;
		atomic_inc(idr_ref);
		dev_set_name(&chan->dev->device, "dma%dchan%d",
			     device->dev_id, chan->chan_id);

		rc = device_register(&chan->dev->device);
		if (rc) {
			free_percpu(chan->local);
			chan->local = NULL;
			kfree(chan->dev);
			atomic_dec(idr_ref);
			goto err_out;
		}
		chan->client_count = 0;
	}
	device->chancnt = chancnt;

	mutex_lock(&dma_list_mutex);
	/* take references on public channels */
	if (dmaengine_ref_count && !dma_has_cap(DMA_PRIVATE, device->cap_mask))
		list_for_each_entry(chan, &device->channels, device_node) {
			/* if clients are already waiting for channels we need
			 * to take references on their behalf
			 */
			if (dma_chan_get(chan) == -ENODEV) {
				/* note we can only get here for the first
				 * channel as the remaining channels are
				 * guaranteed to get a reference
				 */
				rc = -ENODEV;
				mutex_unlock(&dma_list_mutex);
				goto err_out;
			}
		}
	list_add_tail_rcu(&device->global_node, &dma_device_list);
	if (dma_has_cap(DMA_PRIVATE, device->cap_mask))
		device->privatecnt++;	/* Always private */
	dma_channel_rebalance();
	mutex_unlock(&dma_list_mutex);

	return 0;

err_out:
	/* if we never registered a channel just release the idr */
	if (atomic_read(idr_ref) == 0) {
		mutex_lock(&dma_list_mutex);
		idr_remove(&dma_idr, device->dev_id);
		mutex_unlock(&dma_list_mutex);
		kfree(idr_ref);
		return rc;
	}

	list_for_each_entry(chan, &device->channels, device_node) {
		if (chan->local == NULL)
			continue;
		mutex_lock(&dma_list_mutex);
		chan->dev->chan = NULL;
		mutex_unlock(&dma_list_mutex);
		device_unregister(&chan->dev->device);
		free_percpu(chan->local);
	}
	return rc;
}
EXPORT_SYMBOL(dma_async_device_register);

/**
 * dma_async_device_unregister - unregister a DMA device
 * @device: &dma_device
 *
 * This routine is called by dma driver exit routines, dmaengine holds module
 * references to prevent it being called while channels are in use.
 */
void dma_async_device_unregister(struct dma_device *device)
{
	struct dma_chan *chan;

	mutex_lock(&dma_list_mutex);
	list_del_rcu(&device->global_node);
	dma_channel_rebalance();
	mutex_unlock(&dma_list_mutex);

	list_for_each_entry(chan, &device->channels, device_node) {
		WARN_ONCE(chan->client_count,
			  "%s called while %d clients hold a reference\n",
			  __func__, chan->client_count);
		mutex_lock(&dma_list_mutex);
		chan->dev->chan = NULL;
		mutex_unlock(&dma_list_mutex);
		device_unregister(&chan->dev->device);
		free_percpu(chan->local);
	}
}
EXPORT_SYMBOL(dma_async_device_unregister);

struct dmaengine_unmap_pool {
	struct kmem_cache *cache;
	const char *name;
	mempool_t *pool;
	size_t size;
};

#define __UNMAP_POOL(x) { .size = x, .name = "dmaengine-unmap-" __stringify(x) }
static struct dmaengine_unmap_pool unmap_pool[] = {
	__UNMAP_POOL(2),
	#if IS_ENABLED(CONFIG_DMA_ENGINE_RAID)
	__UNMAP_POOL(16),
	__UNMAP_POOL(128),
	__UNMAP_POOL(256),
	#endif
};

static struct dmaengine_unmap_pool *__get_unmap_pool(int nr)
{
	int order = get_count_order(nr);

	switch (order) {
	case 0 ... 1:
		return &unmap_pool[0];
	case 2 ... 4:
		return &unmap_pool[1];
	case 5 ... 7:
		return &unmap_pool[2];
	case 8:
		return &unmap_pool[3];
	default:
		BUG();
		return NULL;
	}
}

static void dmaengine_unmap(struct kref *kref)
{
	struct dmaengine_unmap_data *unmap = container_of(kref, typeof(*unmap), kref);
	struct device *dev = unmap->dev;
	int cnt, i;

	cnt = unmap->to_cnt;
	for (i = 0; i < cnt; i++)
		dma_unmap_page(dev, unmap->addr[i], unmap->len,
			       DMA_TO_DEVICE);
	cnt += unmap->from_cnt;
	for (; i < cnt; i++)
		dma_unmap_page(dev, unmap->addr[i], unmap->len,
			       DMA_FROM_DEVICE);
	cnt += unmap->bidi_cnt;
	for (; i < cnt; i++) {
		if (unmap->addr[i] == 0)
			continue;
		dma_unmap_page(dev, unmap->addr[i], unmap->len,
			       DMA_BIDIRECTIONAL);
	}
	cnt = unmap->map_cnt;
	mempool_free(unmap, __get_unmap_pool(cnt)->pool);
}

void dmaengine_unmap_put(struct dmaengine_unmap_data *unmap)
{
	if (unmap)
		kref_put(&unmap->kref, dmaengine_unmap);
}
EXPORT_SYMBOL_GPL(dmaengine_unmap_put);

static void dmaengine_destroy_unmap_pool(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(unmap_pool); i++) {
		struct dmaengine_unmap_pool *p = &unmap_pool[i];

		if (p->pool)
			mempool_destroy(p->pool);
		p->pool = NULL;
		if (p->cache)
			kmem_cache_destroy(p->cache);
		p->cache = NULL;
	}
}

static int __init dmaengine_init_unmap_pool(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(unmap_pool); i++) {
		struct dmaengine_unmap_pool *p = &unmap_pool[i];
		size_t size;

		size = sizeof(struct dmaengine_unmap_data) +
		       sizeof(dma_addr_t) * p->size;

		p->cache = kmem_cache_create(p->name, size, 0,
					     SLAB_HWCACHE_ALIGN, NULL);
		if (!p->cache)
			break;
		p->pool = mempool_create_slab_pool(1, p->cache);
		if (!p->pool)
			break;
	}

	if (i == ARRAY_SIZE(unmap_pool))
		return 0;

	dmaengine_destroy_unmap_pool();
	return -ENOMEM;
}

struct dmaengine_unmap_data *
dmaengine_get_unmap_data(struct device *dev, int nr, gfp_t flags)
{
	struct dmaengine_unmap_data *unmap;

	unmap = mempool_alloc(__get_unmap_pool(nr)->pool, flags);
	if (!unmap)
		return NULL;

	memset(unmap, 0, sizeof(*unmap));
	kref_init(&unmap->kref);
	unmap->dev = dev;
	unmap->map_cnt = nr;

	return unmap;
}
EXPORT_SYMBOL(dmaengine_get_unmap_data);

/**
 * dma_async_memcpy_pg_to_pg - offloaded copy from page to page
 * @chan: DMA channel to offload copy to
 * @dest_pg: destination page
 * @dest_off: offset in page to copy to
 * @src_pg: source page
 * @src_off: offset in page to copy from
 * @len: length
 *
 * Both @dest_page/@dest_off and @src_page/@src_off must be mappable to a bus
 * address according to the DMA mapping API rules for streaming mappings.
 * Both @dest_page/@dest_off and @src_page/@src_off must stay memory resident
 * (kernel memory or locked user space pages).
 */
dma_cookie_t
dma_async_memcpy_pg_to_pg(struct dma_chan *chan, struct page *dest_pg,
	unsigned int dest_off, struct page *src_pg, unsigned int src_off,
	size_t len)
{
	struct dma_device *dev = chan->device;
	struct dma_async_tx_descriptor *tx;
	struct dmaengine_unmap_data *unmap;
	dma_cookie_t cookie;
	unsigned long flags;

	unmap = dmaengine_get_unmap_data(dev->dev, 2, GFP_NOWAIT);
	if (!unmap)
		return -ENOMEM;

	unmap->to_cnt = 1;
	unmap->from_cnt = 1;
	unmap->addr[0] = dma_map_page(dev->dev, src_pg, src_off, len,
				      DMA_TO_DEVICE);
	unmap->addr[1] = dma_map_page(dev->dev, dest_pg, dest_off, len,
				      DMA_FROM_DEVICE);
	unmap->len = len;
	flags = DMA_CTRL_ACK;
	tx = dev->device_prep_dma_memcpy(chan, unmap->addr[1], unmap->addr[0],
					 len, flags);

	if (!tx) {
		dmaengine_unmap_put(unmap);
		return -ENOMEM;
	}

	dma_set_unmap(tx, unmap);
	cookie = tx->tx_submit(tx);
	dmaengine_unmap_put(unmap);

	preempt_disable();
	__this_cpu_add(chan->local->bytes_transferred, len);
	__this_cpu_inc(chan->local->memcpy_count);
	preempt_enable();

	return cookie;
}
EXPORT_SYMBOL(dma_async_memcpy_pg_to_pg);

/**
 * dma_async_memcpy_buf_to_buf - offloaded copy between virtual addresses
 * @chan: DMA channel to offload copy to
 * @dest: destination address (virtual)
 * @src: source address (virtual)
 * @len: length
 *
 * Both @dest and @src must be mappable to a bus address according to the
 * DMA mapping API rules for streaming mappings.
 * Both @dest and @src must stay memory resident (kernel memory or locked
 * user space pages).
 */
dma_cookie_t
dma_async_memcpy_buf_to_buf(struct dma_chan *chan, void *dest,
			    void *src, size_t len)
{
	return dma_async_memcpy_pg_to_pg(chan, virt_to_page(dest),
					 (unsigned long) dest & ~PAGE_MASK,
					 virt_to_page(src),
					 (unsigned long) src & ~PAGE_MASK, len);
}
EXPORT_SYMBOL(dma_async_memcpy_buf_to_buf);

/**
 * dma_async_memcpy_buf_to_pg - offloaded copy from address to page
 * @chan: DMA channel to offload copy to
 * @page: destination page
 * @offset: offset in page to copy to
 * @kdata: source address (virtual)
 * @len: length
 *
 * Both @page/@offset and @kdata must be mappable to a bus address according
 * to the DMA mapping API rules for streaming mappings.
 * Both @page/@offset and @kdata must stay memory resident (kernel memory or
 * locked user space pages)
 */
dma_cookie_t
dma_async_memcpy_buf_to_pg(struct dma_chan *chan, struct page *page,
			   unsigned int offset, void *kdata, size_t len)
{
	return dma_async_memcpy_pg_to_pg(chan, page, offset,
					 virt_to_page(kdata),
					 (unsigned long) kdata & ~PAGE_MASK, len);
}
EXPORT_SYMBOL(dma_async_memcpy_buf_to_pg);

void dma_async_tx_descriptor_init(struct dma_async_tx_descriptor *tx,
	struct dma_chan *chan)
{
	tx->chan = chan;
	#ifdef CONFIG_ASYNC_TX_ENABLE_CHANNEL_SWITCH
	spin_lock_init(&tx->lock);
	#endif
}
EXPORT_SYMBOL(dma_async_tx_descriptor_init);

/* dma_wait_for_async_tx - spin wait for a transaction to complete
 * @tx: in-flight transaction to wait on
 */
enum dma_status
dma_wait_for_async_tx(struct dma_async_tx_descriptor *tx)
{
	unsigned long dma_sync_wait_timeout = jiffies + msecs_to_jiffies(5000);

	if (!tx)
		return DMA_COMPLETE;

	while (tx->cookie == -EBUSY) {
		if (time_after_eq(jiffies, dma_sync_wait_timeout)) {
			pr_err("%s timeout waiting for descriptor submission\n",
			       __func__);
			return DMA_ERROR;
		}
		cpu_relax();
	}
	return dma_sync_wait(tx->chan, tx->cookie);
}
EXPORT_SYMBOL_GPL(dma_wait_for_async_tx);

/* dma_run_dependencies - helper routine for dma drivers to process
 *	(start) dependent operations on their target channel
 * @tx: transaction with dependencies
 */
void dma_run_dependencies(struct dma_async_tx_descriptor *tx)
{
	struct dma_async_tx_descriptor *dep = txd_next(tx);
	struct dma_async_tx_descriptor *dep_next;
	struct dma_chan *chan;

	if (!dep)
		return;

	/* we'll submit tx->next now, so clear the link */
	txd_clear_next(tx);
	chan = dep->chan;

	/* keep submitting up until a channel switch is detected
	 * in that case we will be called again as a result of
	 * processing the interrupt from async_tx_channel_switch
	 */
	for (; dep; dep = dep_next) {
		txd_lock(dep);
		txd_clear_parent(dep);
		dep_next = txd_next(dep);
		if (dep_next && dep_next->chan == chan)
			txd_clear_next(dep); /* ->next will be submitted */
		else
			dep_next = NULL; /* submit current dep and terminate */
		txd_unlock(dep);

		dep->tx_submit(dep);
	}

	chan->device->device_issue_pending(chan);
}
EXPORT_SYMBOL_GPL(dma_run_dependencies);

static int __init dma_bus_init(void)
{
	int err = dmaengine_init_unmap_pool();

	if (err)
		return err;
	return class_register(&dma_devclass);
}
arch_initcall(dma_bus_init);


