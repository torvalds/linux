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
 * The subsystem keeps two global lists, dma_device_list and dma_client_list.
 * Both of these are protected by a mutex, dma_list_mutex.
 *
 * Each device has a channels list, which runs unlocked but is never modified
 * once the device is registered, it's just setup by the driver.
 *
 * Each client is responsible for keeping track of the channels it uses.  See
 * the definition of dma_event_callback in dmaengine.h.
 *
 * Each device has a kref, which is initialized to 1 when the device is
 * registered. A kref_get is done for each device registered.  When the
 * device is released, the corresponding kref_put is done in the release
 * method. Every time one of the device's channels is allocated to a client,
 * a kref_get occurs.  When the channel is freed, the corresponding kref_put
 * happens. The device's release function does a completion, so
 * unregister_device does a remove event, device_unregister, a kref_put
 * for the first reference, then waits on the completion for all other
 * references to finish.
 *
 * Each channel has an open-coded implementation of Rusty Russell's "bigref,"
 * with a kref and a per_cpu local_t.  A dma_chan_get is called when a client
 * signals that it wants to use a channel, and dma_chan_put is called when
 * a channel is removed or a client using it is unregistered.  A client can
 * take extra references per outstanding transaction, as is the case with
 * the NET DMA client.  The release function does a kref_put on the device.
 *	-ChrisL, DanW
 */

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

static DEFINE_MUTEX(dma_list_mutex);
static LIST_HEAD(dma_device_list);
static LIST_HEAD(dma_client_list);

/* --- sysfs implementation --- */

static ssize_t show_memcpy_count(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dma_chan *chan = to_dma_chan(dev);
	unsigned long count = 0;
	int i;

	for_each_possible_cpu(i)
		count += per_cpu_ptr(chan->local, i)->memcpy_count;

	return sprintf(buf, "%lu\n", count);
}

static ssize_t show_bytes_transferred(struct device *dev, struct device_attribute *attr,
				      char *buf)
{
	struct dma_chan *chan = to_dma_chan(dev);
	unsigned long count = 0;
	int i;

	for_each_possible_cpu(i)
		count += per_cpu_ptr(chan->local, i)->bytes_transferred;

	return sprintf(buf, "%lu\n", count);
}

static ssize_t show_in_use(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dma_chan *chan = to_dma_chan(dev);
	int in_use = 0;

	if (unlikely(chan->slow_ref) &&
		atomic_read(&chan->refcount.refcount) > 1)
		in_use = 1;
	else {
		if (local_read(&(per_cpu_ptr(chan->local,
			get_cpu())->refcount)) > 0)
			in_use = 1;
		put_cpu();
	}

	return sprintf(buf, "%d\n", in_use);
}

static struct device_attribute dma_attrs[] = {
	__ATTR(memcpy_count, S_IRUGO, show_memcpy_count, NULL),
	__ATTR(bytes_transferred, S_IRUGO, show_bytes_transferred, NULL),
	__ATTR(in_use, S_IRUGO, show_in_use, NULL),
	__ATTR_NULL
};

static void dma_async_device_cleanup(struct kref *kref);

static void dma_dev_release(struct device *dev)
{
	struct dma_chan *chan = to_dma_chan(dev);
	kref_put(&chan->device->refcount, dma_async_device_cleanup);
}

static struct class dma_devclass = {
	.name		= "dma",
	.dev_attrs	= dma_attrs,
	.dev_release	= dma_dev_release,
};

/* --- client and device registration --- */

#define dma_chan_satisfies_mask(chan, mask) \
	__dma_chan_satisfies_mask((chan), &(mask))
static int
__dma_chan_satisfies_mask(struct dma_chan *chan, dma_cap_mask_t *want)
{
	dma_cap_mask_t has;

	bitmap_and(has.bits, want->bits, chan->device->cap_mask.bits,
		DMA_TX_TYPE_END);
	return bitmap_equal(want->bits, has.bits, DMA_TX_TYPE_END);
}

/**
 * dma_client_chan_alloc - try to allocate channels to a client
 * @client: &dma_client
 *
 * Called with dma_list_mutex held.
 */
static void dma_client_chan_alloc(struct dma_client *client)
{
	struct dma_device *device;
	struct dma_chan *chan;
	int desc;	/* allocated descriptor count */
	enum dma_state_client ack;

	/* Find a channel */
	list_for_each_entry(device, &dma_device_list, global_node) {
		/* Does the client require a specific DMA controller? */
		if (client->slave && client->slave->dma_dev
				&& client->slave->dma_dev != device->dev)
			continue;

		list_for_each_entry(chan, &device->channels, device_node) {
			if (!dma_chan_satisfies_mask(chan, client->cap_mask))
				continue;

			desc = chan->device->device_alloc_chan_resources(
					chan, client);
			if (desc >= 0) {
				ack = client->event_callback(client,
						chan,
						DMA_RESOURCE_AVAILABLE);

				/* we are done once this client rejects
				 * an available resource
				 */
				if (ack == DMA_ACK) {
					dma_chan_get(chan);
					chan->client_count++;
				} else if (ack == DMA_NAK)
					return;
			}
		}
	}
}

enum dma_status dma_sync_wait(struct dma_chan *chan, dma_cookie_t cookie)
{
	enum dma_status status;
	unsigned long dma_sync_wait_timeout = jiffies + msecs_to_jiffies(5000);

	dma_async_issue_pending(chan);
	do {
		status = dma_async_is_tx_complete(chan, cookie, NULL, NULL);
		if (time_after_eq(jiffies, dma_sync_wait_timeout)) {
			printk(KERN_ERR "dma_sync_wait_timeout!\n");
			return DMA_ERROR;
		}
	} while (status == DMA_IN_PROGRESS);

	return status;
}
EXPORT_SYMBOL(dma_sync_wait);

/**
 * dma_chan_cleanup - release a DMA channel's resources
 * @kref: kernel reference structure that contains the DMA channel device
 */
void dma_chan_cleanup(struct kref *kref)
{
	struct dma_chan *chan = container_of(kref, struct dma_chan, refcount);
	chan->device->device_free_chan_resources(chan);
	kref_put(&chan->device->refcount, dma_async_device_cleanup);
}
EXPORT_SYMBOL(dma_chan_cleanup);

static void dma_chan_free_rcu(struct rcu_head *rcu)
{
	struct dma_chan *chan = container_of(rcu, struct dma_chan, rcu);
	int bias = 0x7FFFFFFF;
	int i;
	for_each_possible_cpu(i)
		bias -= local_read(&per_cpu_ptr(chan->local, i)->refcount);
	atomic_sub(bias, &chan->refcount.refcount);
	kref_put(&chan->refcount, dma_chan_cleanup);
}

static void dma_chan_release(struct dma_chan *chan)
{
	atomic_add(0x7FFFFFFF, &chan->refcount.refcount);
	chan->slow_ref = 1;
	call_rcu(&chan->rcu, dma_chan_free_rcu);
}

/**
 * dma_chans_notify_available - broadcast available channels to the clients
 */
static void dma_clients_notify_available(void)
{
	struct dma_client *client;

	mutex_lock(&dma_list_mutex);

	list_for_each_entry(client, &dma_client_list, global_node)
		dma_client_chan_alloc(client);

	mutex_unlock(&dma_list_mutex);
}

/**
 * dma_chans_notify_available - tell the clients that a channel is going away
 * @chan: channel on its way out
 */
static void dma_clients_notify_removed(struct dma_chan *chan)
{
	struct dma_client *client;
	enum dma_state_client ack;

	mutex_lock(&dma_list_mutex);

	list_for_each_entry(client, &dma_client_list, global_node) {
		ack = client->event_callback(client, chan,
				DMA_RESOURCE_REMOVED);

		/* client was holding resources for this channel so
		 * free it
		 */
		if (ack == DMA_ACK) {
			dma_chan_put(chan);
			chan->client_count--;
		}
	}

	mutex_unlock(&dma_list_mutex);
}

/**
 * dma_async_client_register - register a &dma_client
 * @client: ptr to a client structure with valid 'event_callback' and 'cap_mask'
 */
void dma_async_client_register(struct dma_client *client)
{
	/* validate client data */
	BUG_ON(dma_has_cap(DMA_SLAVE, client->cap_mask) &&
		!client->slave);

	mutex_lock(&dma_list_mutex);
	list_add_tail(&client->global_node, &dma_client_list);
	mutex_unlock(&dma_list_mutex);
}
EXPORT_SYMBOL(dma_async_client_register);

/**
 * dma_async_client_unregister - unregister a client and free the &dma_client
 * @client: &dma_client to free
 *
 * Force frees any allocated DMA channels, frees the &dma_client memory
 */
void dma_async_client_unregister(struct dma_client *client)
{
	struct dma_device *device;
	struct dma_chan *chan;
	enum dma_state_client ack;

	if (!client)
		return;

	mutex_lock(&dma_list_mutex);
	/* free all channels the client is holding */
	list_for_each_entry(device, &dma_device_list, global_node)
		list_for_each_entry(chan, &device->channels, device_node) {
			ack = client->event_callback(client, chan,
				DMA_RESOURCE_REMOVED);

			if (ack == DMA_ACK) {
				dma_chan_put(chan);
				chan->client_count--;
			}
		}

	list_del(&client->global_node);
	mutex_unlock(&dma_list_mutex);
}
EXPORT_SYMBOL(dma_async_client_unregister);

/**
 * dma_async_client_chan_request - send all available channels to the
 * client that satisfy the capability mask
 * @client - requester
 */
void dma_async_client_chan_request(struct dma_client *client)
{
	mutex_lock(&dma_list_mutex);
	dma_client_chan_alloc(client);
	mutex_unlock(&dma_list_mutex);
}
EXPORT_SYMBOL(dma_async_client_chan_request);

/**
 * dma_async_device_register - registers DMA devices found
 * @device: &dma_device
 */
int dma_async_device_register(struct dma_device *device)
{
	static int id;
	int chancnt = 0, rc;
	struct dma_chan* chan;

	if (!device)
		return -ENODEV;

	/* validate device routines */
	BUG_ON(dma_has_cap(DMA_MEMCPY, device->cap_mask) &&
		!device->device_prep_dma_memcpy);
	BUG_ON(dma_has_cap(DMA_XOR, device->cap_mask) &&
		!device->device_prep_dma_xor);
	BUG_ON(dma_has_cap(DMA_ZERO_SUM, device->cap_mask) &&
		!device->device_prep_dma_zero_sum);
	BUG_ON(dma_has_cap(DMA_MEMSET, device->cap_mask) &&
		!device->device_prep_dma_memset);
	BUG_ON(dma_has_cap(DMA_INTERRUPT, device->cap_mask) &&
		!device->device_prep_dma_interrupt);
	BUG_ON(dma_has_cap(DMA_SLAVE, device->cap_mask) &&
		!device->device_prep_slave_sg);
	BUG_ON(dma_has_cap(DMA_SLAVE, device->cap_mask) &&
		!device->device_terminate_all);

	BUG_ON(!device->device_alloc_chan_resources);
	BUG_ON(!device->device_free_chan_resources);
	BUG_ON(!device->device_is_tx_complete);
	BUG_ON(!device->device_issue_pending);
	BUG_ON(!device->dev);

	init_completion(&device->done);
	kref_init(&device->refcount);
	device->dev_id = id++;

	/* represent channels in sysfs. Probably want devs too */
	list_for_each_entry(chan, &device->channels, device_node) {
		chan->local = alloc_percpu(typeof(*chan->local));
		if (chan->local == NULL)
			continue;

		chan->chan_id = chancnt++;
		chan->dev.class = &dma_devclass;
		chan->dev.parent = device->dev;
		snprintf(chan->dev.bus_id, BUS_ID_SIZE, "dma%dchan%d",
		         device->dev_id, chan->chan_id);

		rc = device_register(&chan->dev);
		if (rc) {
			chancnt--;
			free_percpu(chan->local);
			chan->local = NULL;
			goto err_out;
		}

		/* One for the channel, one of the class device */
		kref_get(&device->refcount);
		kref_get(&device->refcount);
		kref_init(&chan->refcount);
		chan->client_count = 0;
		chan->slow_ref = 0;
		INIT_RCU_HEAD(&chan->rcu);
	}

	mutex_lock(&dma_list_mutex);
	list_add_tail(&device->global_node, &dma_device_list);
	mutex_unlock(&dma_list_mutex);

	dma_clients_notify_available();

	return 0;

err_out:
	list_for_each_entry(chan, &device->channels, device_node) {
		if (chan->local == NULL)
			continue;
		kref_put(&device->refcount, dma_async_device_cleanup);
		device_unregister(&chan->dev);
		chancnt--;
		free_percpu(chan->local);
	}
	return rc;
}
EXPORT_SYMBOL(dma_async_device_register);

/**
 * dma_async_device_cleanup - function called when all references are released
 * @kref: kernel reference object
 */
static void dma_async_device_cleanup(struct kref *kref)
{
	struct dma_device *device;

	device = container_of(kref, struct dma_device, refcount);
	complete(&device->done);
}

/**
 * dma_async_device_unregister - unregisters DMA devices
 * @device: &dma_device
 */
void dma_async_device_unregister(struct dma_device *device)
{
	struct dma_chan *chan;

	mutex_lock(&dma_list_mutex);
	list_del(&device->global_node);
	mutex_unlock(&dma_list_mutex);

	list_for_each_entry(chan, &device->channels, device_node) {
		dma_clients_notify_removed(chan);
		device_unregister(&chan->dev);
		dma_chan_release(chan);
	}

	kref_put(&device->refcount, dma_async_device_cleanup);
	wait_for_completion(&device->done);
}
EXPORT_SYMBOL(dma_async_device_unregister);

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
	struct dma_device *dev = chan->device;
	struct dma_async_tx_descriptor *tx;
	dma_addr_t dma_dest, dma_src;
	dma_cookie_t cookie;
	int cpu;

	dma_src = dma_map_single(dev->dev, src, len, DMA_TO_DEVICE);
	dma_dest = dma_map_single(dev->dev, dest, len, DMA_FROM_DEVICE);
	tx = dev->device_prep_dma_memcpy(chan, dma_dest, dma_src, len,
					 DMA_CTRL_ACK);

	if (!tx) {
		dma_unmap_single(dev->dev, dma_src, len, DMA_TO_DEVICE);
		dma_unmap_single(dev->dev, dma_dest, len, DMA_FROM_DEVICE);
		return -ENOMEM;
	}

	tx->callback = NULL;
	cookie = tx->tx_submit(tx);

	cpu = get_cpu();
	per_cpu_ptr(chan->local, cpu)->bytes_transferred += len;
	per_cpu_ptr(chan->local, cpu)->memcpy_count++;
	put_cpu();

	return cookie;
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
	struct dma_device *dev = chan->device;
	struct dma_async_tx_descriptor *tx;
	dma_addr_t dma_dest, dma_src;
	dma_cookie_t cookie;
	int cpu;

	dma_src = dma_map_single(dev->dev, kdata, len, DMA_TO_DEVICE);
	dma_dest = dma_map_page(dev->dev, page, offset, len, DMA_FROM_DEVICE);
	tx = dev->device_prep_dma_memcpy(chan, dma_dest, dma_src, len,
					 DMA_CTRL_ACK);

	if (!tx) {
		dma_unmap_single(dev->dev, dma_src, len, DMA_TO_DEVICE);
		dma_unmap_page(dev->dev, dma_dest, len, DMA_FROM_DEVICE);
		return -ENOMEM;
	}

	tx->callback = NULL;
	cookie = tx->tx_submit(tx);

	cpu = get_cpu();
	per_cpu_ptr(chan->local, cpu)->bytes_transferred += len;
	per_cpu_ptr(chan->local, cpu)->memcpy_count++;
	put_cpu();

	return cookie;
}
EXPORT_SYMBOL(dma_async_memcpy_buf_to_pg);

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
	dma_addr_t dma_dest, dma_src;
	dma_cookie_t cookie;
	int cpu;

	dma_src = dma_map_page(dev->dev, src_pg, src_off, len, DMA_TO_DEVICE);
	dma_dest = dma_map_page(dev->dev, dest_pg, dest_off, len,
				DMA_FROM_DEVICE);
	tx = dev->device_prep_dma_memcpy(chan, dma_dest, dma_src, len,
					 DMA_CTRL_ACK);

	if (!tx) {
		dma_unmap_page(dev->dev, dma_src, len, DMA_TO_DEVICE);
		dma_unmap_page(dev->dev, dma_dest, len, DMA_FROM_DEVICE);
		return -ENOMEM;
	}

	tx->callback = NULL;
	cookie = tx->tx_submit(tx);

	cpu = get_cpu();
	per_cpu_ptr(chan->local, cpu)->bytes_transferred += len;
	per_cpu_ptr(chan->local, cpu)->memcpy_count++;
	put_cpu();

	return cookie;
}
EXPORT_SYMBOL(dma_async_memcpy_pg_to_pg);

void dma_async_tx_descriptor_init(struct dma_async_tx_descriptor *tx,
	struct dma_chan *chan)
{
	tx->chan = chan;
	spin_lock_init(&tx->lock);
}
EXPORT_SYMBOL(dma_async_tx_descriptor_init);

static int __init dma_bus_init(void)
{
	mutex_init(&dma_list_mutex);
	return class_register(&dma_devclass);
}
subsys_initcall(dma_bus_init);

