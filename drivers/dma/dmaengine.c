// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright(c) 2004 - 2006 Intel Corporation. All rights reserved.
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
 * See Documentation/driver-api/dmaengine for more details
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/platform_device.h>
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
#include <linux/numa.h>

#include "dmaengine.h"

static DEFINE_MUTEX(dma_list_mutex);
static DEFINE_IDA(dma_ida);
static LIST_HEAD(dma_device_list);
static long dmaengine_ref_count;

/* --- debugfs implementation --- */
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>

static struct dentry *rootdir;

static void dmaengine_debug_register(struct dma_device *dma_dev)
{
	dma_dev->dbg_dev_root = debugfs_create_dir(dev_name(dma_dev->dev),
						   rootdir);
	if (IS_ERR(dma_dev->dbg_dev_root))
		dma_dev->dbg_dev_root = NULL;
}

static void dmaengine_debug_unregister(struct dma_device *dma_dev)
{
	debugfs_remove_recursive(dma_dev->dbg_dev_root);
	dma_dev->dbg_dev_root = NULL;
}

static void dmaengine_dbg_summary_show(struct seq_file *s,
				       struct dma_device *dma_dev)
{
	struct dma_chan *chan;

	list_for_each_entry(chan, &dma_dev->channels, device_node) {
		if (chan->client_count) {
			seq_printf(s, " %-13s| %s", dma_chan_name(chan),
				   chan->dbg_client_name ?: "in-use");

			if (chan->router)
				seq_printf(s, " (via router: %s)\n",
					dev_name(chan->router->dev));
			else
				seq_puts(s, "\n");
		}
	}
}

static int dmaengine_summary_show(struct seq_file *s, void *data)
{
	struct dma_device *dma_dev = NULL;

	mutex_lock(&dma_list_mutex);
	list_for_each_entry(dma_dev, &dma_device_list, global_node) {
		seq_printf(s, "dma%d (%s): number of channels: %u\n",
			   dma_dev->dev_id, dev_name(dma_dev->dev),
			   dma_dev->chancnt);

		if (dma_dev->dbg_summary_show)
			dma_dev->dbg_summary_show(s, dma_dev);
		else
			dmaengine_dbg_summary_show(s, dma_dev);

		if (!list_is_last(&dma_dev->global_node, &dma_device_list))
			seq_puts(s, "\n");
	}
	mutex_unlock(&dma_list_mutex);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(dmaengine_summary);

static void __init dmaengine_debugfs_init(void)
{
	rootdir = debugfs_create_dir("dmaengine", NULL);

	/* /sys/kernel/debug/dmaengine/summary */
	debugfs_create_file("summary", 0444, rootdir, NULL,
			    &dmaengine_summary_fops);
}
#else
static inline void dmaengine_debugfs_init(void) { }
static inline int dmaengine_debug_register(struct dma_device *dma_dev)
{
	return 0;
}

static inline void dmaengine_debug_unregister(struct dma_device *dma_dev) { }
#endif	/* DEBUG_FS */

/* --- sysfs implementation --- */

#define DMA_SLAVE_NAME	"slave"

/**
 * dev_to_dma_chan - convert a device pointer to its sysfs container object
 * @dev:	device node
 *
 * Must be called under dma_list_mutex.
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
	kfree(chan_dev);
}

static struct class dma_devclass = {
	.name		= "dma",
	.dev_groups	= dma_dev_groups,
	.dev_release	= chan_dev_release,
};

/* --- client and device registration --- */

/* enable iteration over all operation types */
static dma_cap_mask_t dma_cap_mask_all;

/**
 * struct dma_chan_tbl_ent - tracks channel allocations per core/operation
 * @chan:	associated channel for this entry
 */
struct dma_chan_tbl_ent {
	struct dma_chan *chan;
};

/* percpu lookup table for memory-to-memory offload providers */
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
		pr_err("dmaengine dma_channel_table_init failure: %d\n", err);
		for_each_dma_cap_mask(cap, dma_cap_mask_all)
			free_percpu(channel_table[cap]);
	}

	return err;
}
arch_initcall(dma_channel_table_init);

/**
 * dma_chan_is_local - checks if the channel is in the same NUMA-node as the CPU
 * @chan:	DMA channel to test
 * @cpu:	CPU index which the channel should be close to
 *
 * Returns true if the channel is in the same NUMA-node as the CPU.
 */
static bool dma_chan_is_local(struct dma_chan *chan, int cpu)
{
	int node = dev_to_node(chan->device->dev);
	return node == NUMA_NO_NODE ||
		cpumask_test_cpu(cpu, cpumask_of_node(node));
}

/**
 * min_chan - finds the channel with min count and in the same NUMA-node as the CPU
 * @cap:	capability to match
 * @cpu:	CPU index which the channel should be close to
 *
 * If some channels are close to the given CPU, the one with the lowest
 * reference count is returned. Otherwise, CPU is ignored and only the
 * reference count is taken into account.
 *
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
 * Optimize for CPU isolation (each CPU gets a dedicated channel for an
 * operation type) in the SMP case, and operation isolation (avoid
 * multi-tasking channels) in the non-SMP case.
 *
 * Must be called under dma_list_mutex.
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

static int dma_device_satisfies_mask(struct dma_device *device,
				     const dma_cap_mask_t *want)
{
	dma_cap_mask_t has;

	bitmap_and(has.bits, want->bits, device->cap_mask.bits,
		DMA_TX_TYPE_END);
	return bitmap_equal(want->bits, has.bits, DMA_TX_TYPE_END);
}

static struct module *dma_chan_to_owner(struct dma_chan *chan)
{
	return chan->device->owner;
}

/**
 * balance_ref_count - catch up the channel reference count
 * @chan:	channel to balance ->client_count versus dmaengine_ref_count
 *
 * Must be called under dma_list_mutex.
 */
static void balance_ref_count(struct dma_chan *chan)
{
	struct module *owner = dma_chan_to_owner(chan);

	while (chan->client_count < dmaengine_ref_count) {
		__module_get(owner);
		chan->client_count++;
	}
}

static void dma_device_release(struct kref *ref)
{
	struct dma_device *device = container_of(ref, struct dma_device, ref);

	list_del_rcu(&device->global_node);
	dma_channel_rebalance();

	if (device->device_release)
		device->device_release(device);
}

static void dma_device_put(struct dma_device *device)
{
	lockdep_assert_held(&dma_list_mutex);
	kref_put(&device->ref, dma_device_release);
}

/**
 * dma_chan_get - try to grab a DMA channel's parent driver module
 * @chan:	channel to grab
 *
 * Must be called under dma_list_mutex.
 */
static int dma_chan_get(struct dma_chan *chan)
{
	struct module *owner = dma_chan_to_owner(chan);
	int ret;

	/* The channel is already in use, update client count */
	if (chan->client_count) {
		__module_get(owner);
		goto out;
	}

	if (!try_module_get(owner))
		return -ENODEV;

	ret = kref_get_unless_zero(&chan->device->ref);
	if (!ret) {
		ret = -ENODEV;
		goto module_put_out;
	}

	/* allocate upon first client reference */
	if (chan->device->device_alloc_chan_resources) {
		ret = chan->device->device_alloc_chan_resources(chan);
		if (ret < 0)
			goto err_out;
	}

	if (!dma_has_cap(DMA_PRIVATE, chan->device->cap_mask))
		balance_ref_count(chan);

out:
	chan->client_count++;
	return 0;

err_out:
	dma_device_put(chan->device);
module_put_out:
	module_put(owner);
	return ret;
}

/**
 * dma_chan_put - drop a reference to a DMA channel's parent driver module
 * @chan:	channel to release
 *
 * Must be called under dma_list_mutex.
 */
static void dma_chan_put(struct dma_chan *chan)
{
	/* This channel is not in use, bail out */
	if (!chan->client_count)
		return;

	chan->client_count--;

	/* This channel is not in use anymore, free it */
	if (!chan->client_count && chan->device->device_free_chan_resources) {
		/* Make sure all operations have completed */
		dmaengine_synchronize(chan);
		chan->device->device_free_chan_resources(chan);
	}

	/* If the channel is used via a DMA request router, free the mapping */
	if (chan->router && chan->router->route_free) {
		chan->router->route_free(chan->router->dev, chan->route_data);
		chan->router = NULL;
		chan->route_data = NULL;
	}

	dma_device_put(chan->device);
	module_put(dma_chan_to_owner(chan));
}

enum dma_status dma_sync_wait(struct dma_chan *chan, dma_cookie_t cookie)
{
	enum dma_status status;
	unsigned long dma_sync_wait_timeout = jiffies + msecs_to_jiffies(5000);

	dma_async_issue_pending(chan);
	do {
		status = dma_async_is_tx_complete(chan, cookie, NULL, NULL);
		if (time_after_eq(jiffies, dma_sync_wait_timeout)) {
			dev_err(chan->device->dev, "%s: timeout!\n", __func__);
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
 * dma_find_channel - find a channel to carry out the operation
 * @tx_type:	transaction type
 */
struct dma_chan *dma_find_channel(enum dma_transaction_type tx_type)
{
	return this_cpu_read(channel_table[tx_type]->chan);
}
EXPORT_SYMBOL(dma_find_channel);

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

int dma_get_slave_caps(struct dma_chan *chan, struct dma_slave_caps *caps)
{
	struct dma_device *device;

	if (!chan || !caps)
		return -EINVAL;

	device = chan->device;

	/* check if the channel supports slave transactions */
	if (!(test_bit(DMA_SLAVE, device->cap_mask.bits) ||
	      test_bit(DMA_CYCLIC, device->cap_mask.bits)))
		return -ENXIO;

	/*
	 * Check whether it reports it uses the generic slave
	 * capabilities, if not, that means it doesn't support any
	 * kind of slave capabilities reporting.
	 */
	if (!device->directions)
		return -ENXIO;

	caps->src_addr_widths = device->src_addr_widths;
	caps->dst_addr_widths = device->dst_addr_widths;
	caps->directions = device->directions;
	caps->max_burst = device->max_burst;
	caps->residue_granularity = device->residue_granularity;
	caps->descriptor_reuse = device->descriptor_reuse;
	caps->cmd_pause = !!device->device_pause;
	caps->cmd_resume = !!device->device_resume;
	caps->cmd_terminate = !!device->device_terminate_all;

	return 0;
}
EXPORT_SYMBOL_GPL(dma_get_slave_caps);

static struct dma_chan *private_candidate(const dma_cap_mask_t *mask,
					  struct dma_device *dev,
					  dma_filter_fn fn, void *fn_param)
{
	struct dma_chan *chan;

	if (mask && !dma_device_satisfies_mask(dev, mask)) {
		dev_dbg(dev->dev, "%s: wrong capabilities\n", __func__);
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
			dev_dbg(dev->dev, "%s: %s busy\n",
				 __func__, dma_chan_name(chan));
			continue;
		}
		if (fn && !fn(chan, fn_param)) {
			dev_dbg(dev->dev, "%s: %s filter said false\n",
				 __func__, dma_chan_name(chan));
			continue;
		}
		return chan;
	}

	return NULL;
}

static struct dma_chan *find_candidate(struct dma_device *device,
				       const dma_cap_mask_t *mask,
				       dma_filter_fn fn, void *fn_param)
{
	struct dma_chan *chan = private_candidate(mask, device, fn, fn_param);
	int err;

	if (chan) {
		/* Found a suitable channel, try to grab, prep, and return it.
		 * We first set DMA_PRIVATE to disable balance_ref_count as this
		 * channel will not be published in the general-purpose
		 * allocator
		 */
		dma_cap_set(DMA_PRIVATE, device->cap_mask);
		device->privatecnt++;
		err = dma_chan_get(chan);

		if (err) {
			if (err == -ENODEV) {
				dev_dbg(device->dev, "%s: %s module removed\n",
					__func__, dma_chan_name(chan));
				list_del_rcu(&device->global_node);
			} else
				dev_dbg(device->dev,
					"%s: failed to get %s: (%d)\n",
					 __func__, dma_chan_name(chan), err);

			if (--device->privatecnt == 0)
				dma_cap_clear(DMA_PRIVATE, device->cap_mask);

			chan = ERR_PTR(err);
		}
	}

	return chan ? chan : ERR_PTR(-EPROBE_DEFER);
}

/**
 * dma_get_slave_channel - try to get specific channel exclusively
 * @chan:	target channel
 */
struct dma_chan *dma_get_slave_channel(struct dma_chan *chan)
{
	int err = -EBUSY;

	/* lock against __dma_request_channel */
	mutex_lock(&dma_list_mutex);

	if (chan->client_count == 0) {
		struct dma_device *device = chan->device;

		dma_cap_set(DMA_PRIVATE, device->cap_mask);
		device->privatecnt++;
		err = dma_chan_get(chan);
		if (err) {
			dev_dbg(chan->device->dev,
				"%s: failed to get %s: (%d)\n",
				__func__, dma_chan_name(chan), err);
			chan = NULL;
			if (--device->privatecnt == 0)
				dma_cap_clear(DMA_PRIVATE, device->cap_mask);
		}
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

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	/* lock against __dma_request_channel */
	mutex_lock(&dma_list_mutex);

	chan = find_candidate(device, &mask, NULL, NULL);

	mutex_unlock(&dma_list_mutex);

	return IS_ERR(chan) ? NULL : chan;
}
EXPORT_SYMBOL_GPL(dma_get_any_slave_channel);

/**
 * __dma_request_channel - try to allocate an exclusive channel
 * @mask:	capabilities that the channel must satisfy
 * @fn:		optional callback to disposition available channels
 * @fn_param:	opaque parameter to pass to dma_filter_fn()
 * @np:		device node to look for DMA channels
 *
 * Returns pointer to appropriate DMA channel on success or NULL.
 */
struct dma_chan *__dma_request_channel(const dma_cap_mask_t *mask,
				       dma_filter_fn fn, void *fn_param,
				       struct device_node *np)
{
	struct dma_device *device, *_d;
	struct dma_chan *chan = NULL;

	/* Find a channel */
	mutex_lock(&dma_list_mutex);
	list_for_each_entry_safe(device, _d, &dma_device_list, global_node) {
		/* Finds a DMA controller with matching device node */
		if (np && device->dev->of_node && np != device->dev->of_node)
			continue;

		chan = find_candidate(device, mask, fn, fn_param);
		if (!IS_ERR(chan))
			break;

		chan = NULL;
	}
	mutex_unlock(&dma_list_mutex);

	pr_debug("%s: %s (%s)\n",
		 __func__,
		 chan ? "success" : "fail",
		 chan ? dma_chan_name(chan) : NULL);

	return chan;
}
EXPORT_SYMBOL_GPL(__dma_request_channel);

static const struct dma_slave_map *dma_filter_match(struct dma_device *device,
						    const char *name,
						    struct device *dev)
{
	int i;

	if (!device->filter.mapcnt)
		return NULL;

	for (i = 0; i < device->filter.mapcnt; i++) {
		const struct dma_slave_map *map = &device->filter.map[i];

		if (!strcmp(map->devname, dev_name(dev)) &&
		    !strcmp(map->slave, name))
			return map;
	}

	return NULL;
}

/**
 * dma_request_chan - try to allocate an exclusive slave channel
 * @dev:	pointer to client device structure
 * @name:	slave channel name
 *
 * Returns pointer to appropriate DMA channel on success or an error pointer.
 */
struct dma_chan *dma_request_chan(struct device *dev, const char *name)
{
	struct dma_device *d, *_d;
	struct dma_chan *chan = NULL;

	/* If device-tree is present get slave info from here */
	if (dev->of_node)
		chan = of_dma_request_slave_channel(dev->of_node, name);

	/* If device was enumerated by ACPI get slave info from here */
	if (has_acpi_companion(dev) && !chan)
		chan = acpi_dma_request_slave_chan_by_name(dev, name);

	if (PTR_ERR(chan) == -EPROBE_DEFER)
		return chan;

	if (!IS_ERR_OR_NULL(chan))
		goto found;

	/* Try to find the channel via the DMA filter map(s) */
	mutex_lock(&dma_list_mutex);
	list_for_each_entry_safe(d, _d, &dma_device_list, global_node) {
		dma_cap_mask_t mask;
		const struct dma_slave_map *map = dma_filter_match(d, name, dev);

		if (!map)
			continue;

		dma_cap_zero(mask);
		dma_cap_set(DMA_SLAVE, mask);

		chan = find_candidate(d, &mask, d->filter.fn, map->param);
		if (!IS_ERR(chan))
			break;
	}
	mutex_unlock(&dma_list_mutex);

	if (IS_ERR_OR_NULL(chan))
		return chan ? chan : ERR_PTR(-EPROBE_DEFER);

found:
#ifdef CONFIG_DEBUG_FS
	chan->dbg_client_name = kasprintf(GFP_KERNEL, "%s:%s", dev_name(dev),
					  name);
#endif

	chan->name = kasprintf(GFP_KERNEL, "dma:%s", name);
	if (!chan->name)
		return chan;
	chan->slave = dev;

	if (sysfs_create_link(&chan->dev->device.kobj, &dev->kobj,
			      DMA_SLAVE_NAME))
		dev_warn(dev, "Cannot create DMA %s symlink\n", DMA_SLAVE_NAME);
	if (sysfs_create_link(&dev->kobj, &chan->dev->device.kobj, chan->name))
		dev_warn(dev, "Cannot create DMA %s symlink\n", chan->name);

	return chan;
}
EXPORT_SYMBOL_GPL(dma_request_chan);

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
	struct dma_chan *ch = dma_request_chan(dev, name);
	if (IS_ERR(ch))
		return NULL;

	return ch;
}
EXPORT_SYMBOL_GPL(dma_request_slave_channel);

/**
 * dma_request_chan_by_mask - allocate a channel satisfying certain capabilities
 * @mask:	capabilities that the channel must satisfy
 *
 * Returns pointer to appropriate DMA channel on success or an error pointer.
 */
struct dma_chan *dma_request_chan_by_mask(const dma_cap_mask_t *mask)
{
	struct dma_chan *chan;

	if (!mask)
		return ERR_PTR(-ENODEV);

	chan = __dma_request_channel(mask, NULL, NULL, NULL);
	if (!chan) {
		mutex_lock(&dma_list_mutex);
		if (list_empty(&dma_device_list))
			chan = ERR_PTR(-EPROBE_DEFER);
		else
			chan = ERR_PTR(-ENODEV);
		mutex_unlock(&dma_list_mutex);
	}

	return chan;
}
EXPORT_SYMBOL_GPL(dma_request_chan_by_mask);

void dma_release_channel(struct dma_chan *chan)
{
	mutex_lock(&dma_list_mutex);
	WARN_ONCE(chan->client_count != 1,
		  "chan reference count %d != 1\n", chan->client_count);
	dma_chan_put(chan);
	/* drop PRIVATE cap enabled by __dma_request_channel() */
	if (--chan->device->privatecnt == 0)
		dma_cap_clear(DMA_PRIVATE, chan->device->cap_mask);

	if (chan->slave) {
		sysfs_remove_link(&chan->dev->device.kobj, DMA_SLAVE_NAME);
		sysfs_remove_link(&chan->slave->kobj, chan->name);
		kfree(chan->name);
		chan->name = NULL;
		chan->slave = NULL;
	}

#ifdef CONFIG_DEBUG_FS
	kfree(chan->dbg_client_name);
	chan->dbg_client_name = NULL;
#endif
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
				dev_dbg(chan->device->dev,
					"%s: failed to get %s: (%d)\n",
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
 * dmaengine_put - let DMA drivers be removed when ref_count == 0
 */
void dmaengine_put(void)
{
	struct dma_device *device, *_d;
	struct dma_chan *chan;

	mutex_lock(&dma_list_mutex);
	dmaengine_ref_count--;
	BUG_ON(dmaengine_ref_count < 0);
	/* drop channel references */
	list_for_each_entry_safe(device, _d, &dma_device_list, global_node) {
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

	#if IS_ENABLED(CONFIG_ASYNC_MEMCPY)
	if (!dma_has_cap(DMA_MEMCPY, device->cap_mask))
		return false;
	#endif

	#if IS_ENABLED(CONFIG_ASYNC_XOR)
	if (!dma_has_cap(DMA_XOR, device->cap_mask))
		return false;

	#ifndef CONFIG_ASYNC_TX_DISABLE_XOR_VAL_DMA
	if (!dma_has_cap(DMA_XOR_VAL, device->cap_mask))
		return false;
	#endif
	#endif

	#if IS_ENABLED(CONFIG_ASYNC_PQ)
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
	int rc = ida_alloc(&dma_ida, GFP_KERNEL);

	if (rc < 0)
		return rc;
	device->dev_id = rc;
	return 0;
}

static int __dma_async_device_channel_register(struct dma_device *device,
					       struct dma_chan *chan)
{
	int rc = 0;

	chan->local = alloc_percpu(typeof(*chan->local));
	if (!chan->local)
		goto err_out;
	chan->dev = kzalloc(sizeof(*chan->dev), GFP_KERNEL);
	if (!chan->dev) {
		free_percpu(chan->local);
		chan->local = NULL;
		goto err_out;
	}

	/*
	 * When the chan_id is a negative value, we are dynamically adding
	 * the channel. Otherwise we are static enumerating.
	 */
	mutex_lock(&device->chan_mutex);
	chan->chan_id = ida_alloc(&device->chan_ida, GFP_KERNEL);
	mutex_unlock(&device->chan_mutex);
	if (chan->chan_id < 0) {
		pr_err("%s: unable to alloc ida for chan: %d\n",
		       __func__, chan->chan_id);
		goto err_out;
	}

	chan->dev->device.class = &dma_devclass;
	chan->dev->device.parent = device->dev;
	chan->dev->chan = chan;
	chan->dev->dev_id = device->dev_id;
	dev_set_name(&chan->dev->device, "dma%dchan%d",
		     device->dev_id, chan->chan_id);
	rc = device_register(&chan->dev->device);
	if (rc)
		goto err_out_ida;
	chan->client_count = 0;
	device->chancnt++;

	return 0;

 err_out_ida:
	mutex_lock(&device->chan_mutex);
	ida_free(&device->chan_ida, chan->chan_id);
	mutex_unlock(&device->chan_mutex);
 err_out:
	free_percpu(chan->local);
	kfree(chan->dev);
	return rc;
}

int dma_async_device_channel_register(struct dma_device *device,
				      struct dma_chan *chan)
{
	int rc;

	rc = __dma_async_device_channel_register(device, chan);
	if (rc < 0)
		return rc;

	dma_channel_rebalance();
	return 0;
}
EXPORT_SYMBOL_GPL(dma_async_device_channel_register);

static void __dma_async_device_channel_unregister(struct dma_device *device,
						  struct dma_chan *chan)
{
	WARN_ONCE(!device->device_release && chan->client_count,
		  "%s called while %d clients hold a reference\n",
		  __func__, chan->client_count);
	mutex_lock(&dma_list_mutex);
	list_del(&chan->device_node);
	device->chancnt--;
	chan->dev->chan = NULL;
	mutex_unlock(&dma_list_mutex);
	mutex_lock(&device->chan_mutex);
	ida_free(&device->chan_ida, chan->chan_id);
	mutex_unlock(&device->chan_mutex);
	device_unregister(&chan->dev->device);
	free_percpu(chan->local);
}

void dma_async_device_channel_unregister(struct dma_device *device,
					 struct dma_chan *chan)
{
	__dma_async_device_channel_unregister(device, chan);
	dma_channel_rebalance();
}
EXPORT_SYMBOL_GPL(dma_async_device_channel_unregister);

/**
 * dma_async_device_register - registers DMA devices found
 * @device:	pointer to &struct dma_device
 *
 * After calling this routine the structure should not be freed except in the
 * device_release() callback which will be called after
 * dma_async_device_unregister() is called and no further references are taken.
 */
int dma_async_device_register(struct dma_device *device)
{
	int rc;
	struct dma_chan* chan;

	if (!device)
		return -ENODEV;

	/* validate device routines */
	if (!device->dev) {
		pr_err("DMAdevice must have dev\n");
		return -EIO;
	}

	device->owner = device->dev->driver->owner;

	if (dma_has_cap(DMA_MEMCPY, device->cap_mask) && !device->device_prep_dma_memcpy) {
		dev_err(device->dev,
			"Device claims capability %s, but op is not defined\n",
			"DMA_MEMCPY");
		return -EIO;
	}

	if (dma_has_cap(DMA_XOR, device->cap_mask) && !device->device_prep_dma_xor) {
		dev_err(device->dev,
			"Device claims capability %s, but op is not defined\n",
			"DMA_XOR");
		return -EIO;
	}

	if (dma_has_cap(DMA_XOR_VAL, device->cap_mask) && !device->device_prep_dma_xor_val) {
		dev_err(device->dev,
			"Device claims capability %s, but op is not defined\n",
			"DMA_XOR_VAL");
		return -EIO;
	}

	if (dma_has_cap(DMA_PQ, device->cap_mask) && !device->device_prep_dma_pq) {
		dev_err(device->dev,
			"Device claims capability %s, but op is not defined\n",
			"DMA_PQ");
		return -EIO;
	}

	if (dma_has_cap(DMA_PQ_VAL, device->cap_mask) && !device->device_prep_dma_pq_val) {
		dev_err(device->dev,
			"Device claims capability %s, but op is not defined\n",
			"DMA_PQ_VAL");
		return -EIO;
	}

	if (dma_has_cap(DMA_MEMSET, device->cap_mask) && !device->device_prep_dma_memset) {
		dev_err(device->dev,
			"Device claims capability %s, but op is not defined\n",
			"DMA_MEMSET");
		return -EIO;
	}

	if (dma_has_cap(DMA_INTERRUPT, device->cap_mask) && !device->device_prep_dma_interrupt) {
		dev_err(device->dev,
			"Device claims capability %s, but op is not defined\n",
			"DMA_INTERRUPT");
		return -EIO;
	}

	if (dma_has_cap(DMA_CYCLIC, device->cap_mask) && !device->device_prep_dma_cyclic) {
		dev_err(device->dev,
			"Device claims capability %s, but op is not defined\n",
			"DMA_CYCLIC");
		return -EIO;
	}

	if (dma_has_cap(DMA_INTERLEAVE, device->cap_mask) && !device->device_prep_interleaved_dma) {
		dev_err(device->dev,
			"Device claims capability %s, but op is not defined\n",
			"DMA_INTERLEAVE");
		return -EIO;
	}


	if (!device->device_tx_status) {
		dev_err(device->dev, "Device tx_status is not defined\n");
		return -EIO;
	}


	if (!device->device_issue_pending) {
		dev_err(device->dev, "Device issue_pending is not defined\n");
		return -EIO;
	}

	if (!device->device_release)
		dev_dbg(device->dev,
			 "WARN: Device release is not defined so it is not safe to unbind this driver while in use\n");

	kref_init(&device->ref);

	/* note: this only matters in the
	 * CONFIG_ASYNC_TX_ENABLE_CHANNEL_SWITCH=n case
	 */
	if (device_has_all_tx_types(device))
		dma_cap_set(DMA_ASYNC_TX, device->cap_mask);

	rc = get_dma_id(device);
	if (rc != 0)
		return rc;

	mutex_init(&device->chan_mutex);
	ida_init(&device->chan_ida);

	/* represent channels in sysfs. Probably want devs too */
	list_for_each_entry(chan, &device->channels, device_node) {
		rc = __dma_async_device_channel_register(device, chan);
		if (rc < 0)
			goto err_out;
	}

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

	dmaengine_debug_register(device);

	return 0;

err_out:
	/* if we never registered a channel just release the idr */
	if (!device->chancnt) {
		ida_free(&dma_ida, device->dev_id);
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
 * @device:	pointer to &struct dma_device
 *
 * This routine is called by dma driver exit routines, dmaengine holds module
 * references to prevent it being called while channels are in use.
 */
void dma_async_device_unregister(struct dma_device *device)
{
	struct dma_chan *chan, *n;

	dmaengine_debug_unregister(device);

	list_for_each_entry_safe(chan, n, &device->channels, device_node)
		__dma_async_device_channel_unregister(device, chan);

	mutex_lock(&dma_list_mutex);
	/*
	 * setting DMA_PRIVATE ensures the device being torn down will not
	 * be used in the channel_table
	 */
	dma_cap_set(DMA_PRIVATE, device->cap_mask);
	dma_channel_rebalance();
	ida_free(&dma_ida, device->dev_id);
	dma_device_put(device);
	mutex_unlock(&dma_list_mutex);
}
EXPORT_SYMBOL(dma_async_device_unregister);

static void dmam_device_release(struct device *dev, void *res)
{
	struct dma_device *device;

	device = *(struct dma_device **)res;
	dma_async_device_unregister(device);
}

/**
 * dmaenginem_async_device_register - registers DMA devices found
 * @device:	pointer to &struct dma_device
 *
 * The operation is managed and will be undone on driver detach.
 */
int dmaenginem_async_device_register(struct dma_device *device)
{
	void *p;
	int ret;

	p = devres_alloc(dmam_device_release, sizeof(void *), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	ret = dma_async_device_register(device);
	if (!ret) {
		*(struct dma_device **)p = device;
		devres_add(device->dev, p);
	} else {
		devres_free(p);
	}

	return ret;
}
EXPORT_SYMBOL(dmaenginem_async_device_register);

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
#if IS_ENABLED(CONFIG_DMA_ENGINE_RAID)
	case 2 ... 4:
		return &unmap_pool[1];
	case 5 ... 7:
		return &unmap_pool[2];
	case 8:
		return &unmap_pool[3];
#endif
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

		mempool_destroy(p->pool);
		p->pool = NULL;
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

void dma_async_tx_descriptor_init(struct dma_async_tx_descriptor *tx,
	struct dma_chan *chan)
{
	tx->chan = chan;
	#ifdef CONFIG_ASYNC_TX_ENABLE_CHANNEL_SWITCH
	spin_lock_init(&tx->lock);
	#endif
}
EXPORT_SYMBOL(dma_async_tx_descriptor_init);

static inline int desc_check_and_set_metadata_mode(
	struct dma_async_tx_descriptor *desc, enum dma_desc_metadata_mode mode)
{
	/* Make sure that the metadata mode is not mixed */
	if (!desc->desc_metadata_mode) {
		if (dmaengine_is_metadata_mode_supported(desc->chan, mode))
			desc->desc_metadata_mode = mode;
		else
			return -ENOTSUPP;
	} else if (desc->desc_metadata_mode != mode) {
		return -EINVAL;
	}

	return 0;
}

int dmaengine_desc_attach_metadata(struct dma_async_tx_descriptor *desc,
				   void *data, size_t len)
{
	int ret;

	if (!desc)
		return -EINVAL;

	ret = desc_check_and_set_metadata_mode(desc, DESC_METADATA_CLIENT);
	if (ret)
		return ret;

	if (!desc->metadata_ops || !desc->metadata_ops->attach)
		return -ENOTSUPP;

	return desc->metadata_ops->attach(desc, data, len);
}
EXPORT_SYMBOL_GPL(dmaengine_desc_attach_metadata);

void *dmaengine_desc_get_metadata_ptr(struct dma_async_tx_descriptor *desc,
				      size_t *payload_len, size_t *max_len)
{
	int ret;

	if (!desc)
		return ERR_PTR(-EINVAL);

	ret = desc_check_and_set_metadata_mode(desc, DESC_METADATA_ENGINE);
	if (ret)
		return ERR_PTR(ret);

	if (!desc->metadata_ops || !desc->metadata_ops->get_ptr)
		return ERR_PTR(-ENOTSUPP);

	return desc->metadata_ops->get_ptr(desc, payload_len, max_len);
}
EXPORT_SYMBOL_GPL(dmaengine_desc_get_metadata_ptr);

int dmaengine_desc_set_metadata_len(struct dma_async_tx_descriptor *desc,
				    size_t payload_len)
{
	int ret;

	if (!desc)
		return -EINVAL;

	ret = desc_check_and_set_metadata_mode(desc, DESC_METADATA_ENGINE);
	if (ret)
		return ret;

	if (!desc->metadata_ops || !desc->metadata_ops->set_len)
		return -ENOTSUPP;

	return desc->metadata_ops->set_len(desc, payload_len);
}
EXPORT_SYMBOL_GPL(dmaengine_desc_set_metadata_len);

/**
 * dma_wait_for_async_tx - spin wait for a transaction to complete
 * @tx:		in-flight transaction to wait on
 */
enum dma_status
dma_wait_for_async_tx(struct dma_async_tx_descriptor *tx)
{
	unsigned long dma_sync_wait_timeout = jiffies + msecs_to_jiffies(5000);

	if (!tx)
		return DMA_COMPLETE;

	while (tx->cookie == -EBUSY) {
		if (time_after_eq(jiffies, dma_sync_wait_timeout)) {
			dev_err(tx->chan->device->dev,
				"%s timeout waiting for descriptor submission\n",
				__func__);
			return DMA_ERROR;
		}
		cpu_relax();
	}
	return dma_sync_wait(tx->chan, tx->cookie);
}
EXPORT_SYMBOL_GPL(dma_wait_for_async_tx);

/**
 * dma_run_dependencies - process dependent operations on the target channel
 * @tx:		transaction with dependencies
 *
 * Helper routine for DMA drivers to process (start) dependent operations
 * on their target channel.
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

	err = class_register(&dma_devclass);
	if (!err)
		dmaengine_debugfs_init();

	return err;
}
arch_initcall(dma_bus_init);
