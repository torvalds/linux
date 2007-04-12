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
 * Each client has a channels list, it's only modified under the client->lock
 * and in an RCU callback, so it's safe to read under rcu_read_lock().
 *
 * Each device has a kref, which is initialized to 1 when the device is
 * registered. A kref_put is done for each class_device registered.  When the
 * class_device is released, the coresponding kref_put is done in the release
 * method. Every time one of the device's channels is allocated to a client,
 * a kref_get occurs.  When the channel is freed, the coresponding kref_put
 * happens. The device's release function does a completion, so
 * unregister_device does a remove event, class_device_unregister, a kref_put
 * for the first reference, then waits on the completion for all other
 * references to finish.
 *
 * Each channel has an open-coded implementation of Rusty Russell's "bigref,"
 * with a kref and a per_cpu local_t.  A single reference is set when on an
 * ADDED event, and removed with a REMOVE event.  Net DMA client takes an
 * extra reference per outstanding transaction.  The relase function does a
 * kref_put on the device. -ChrisL
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/hardirq.h>
#include <linux/spinlock.h>
#include <linux/percpu.h>
#include <linux/rcupdate.h>
#include <linux/mutex.h>

static DEFINE_MUTEX(dma_list_mutex);
static LIST_HEAD(dma_device_list);
static LIST_HEAD(dma_client_list);

/* --- sysfs implementation --- */

static ssize_t show_memcpy_count(struct class_device *cd, char *buf)
{
	struct dma_chan *chan = container_of(cd, struct dma_chan, class_dev);
	unsigned long count = 0;
	int i;

	for_each_possible_cpu(i)
		count += per_cpu_ptr(chan->local, i)->memcpy_count;

	return sprintf(buf, "%lu\n", count);
}

static ssize_t show_bytes_transferred(struct class_device *cd, char *buf)
{
	struct dma_chan *chan = container_of(cd, struct dma_chan, class_dev);
	unsigned long count = 0;
	int i;

	for_each_possible_cpu(i)
		count += per_cpu_ptr(chan->local, i)->bytes_transferred;

	return sprintf(buf, "%lu\n", count);
}

static ssize_t show_in_use(struct class_device *cd, char *buf)
{
	struct dma_chan *chan = container_of(cd, struct dma_chan, class_dev);

	return sprintf(buf, "%d\n", (chan->client ? 1 : 0));
}

static struct class_device_attribute dma_class_attrs[] = {
	__ATTR(memcpy_count, S_IRUGO, show_memcpy_count, NULL),
	__ATTR(bytes_transferred, S_IRUGO, show_bytes_transferred, NULL),
	__ATTR(in_use, S_IRUGO, show_in_use, NULL),
	__ATTR_NULL
};

static void dma_async_device_cleanup(struct kref *kref);

static void dma_class_dev_release(struct class_device *cd)
{
	struct dma_chan *chan = container_of(cd, struct dma_chan, class_dev);
	kref_put(&chan->device->refcount, dma_async_device_cleanup);
}

static struct class dma_devclass = {
	.name            = "dma",
	.class_dev_attrs = dma_class_attrs,
	.release = dma_class_dev_release,
};

/* --- client and device registration --- */

/**
 * dma_client_chan_alloc - try to allocate a channel to a client
 * @client: &dma_client
 *
 * Called with dma_list_mutex held.
 */
static struct dma_chan *dma_client_chan_alloc(struct dma_client *client)
{
	struct dma_device *device;
	struct dma_chan *chan;
	unsigned long flags;
	int desc;	/* allocated descriptor count */

	/* Find a channel, any DMA engine will do */
	list_for_each_entry(device, &dma_device_list, global_node) {
		list_for_each_entry(chan, &device->channels, device_node) {
			if (chan->client)
				continue;

			desc = chan->device->device_alloc_chan_resources(chan);
			if (desc >= 0) {
				kref_get(&device->refcount);
				kref_init(&chan->refcount);
				chan->slow_ref = 0;
				INIT_RCU_HEAD(&chan->rcu);
				chan->client = client;
				spin_lock_irqsave(&client->lock, flags);
				list_add_tail_rcu(&chan->client_node,
				                  &client->channels);
				spin_unlock_irqrestore(&client->lock, flags);
				return chan;
			}
		}
	}

	return NULL;
}

/**
 * dma_chan_cleanup - release a DMA channel's resources
 * @kref: kernel reference structure that contains the DMA channel device
 */
void dma_chan_cleanup(struct kref *kref)
{
	struct dma_chan *chan = container_of(kref, struct dma_chan, refcount);
	chan->device->device_free_chan_resources(chan);
	chan->client = NULL;
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

static void dma_client_chan_free(struct dma_chan *chan)
{
	atomic_add(0x7FFFFFFF, &chan->refcount.refcount);
	chan->slow_ref = 1;
	call_rcu(&chan->rcu, dma_chan_free_rcu);
}

/**
 * dma_chans_rebalance - reallocate channels to clients
 *
 * When the number of DMA channel in the system changes,
 * channels need to be rebalanced among clients.
 */
static void dma_chans_rebalance(void)
{
	struct dma_client *client;
	struct dma_chan *chan;
	unsigned long flags;

	mutex_lock(&dma_list_mutex);

	list_for_each_entry(client, &dma_client_list, global_node) {
		while (client->chans_desired > client->chan_count) {
			chan = dma_client_chan_alloc(client);
			if (!chan)
				break;
			client->chan_count++;
			client->event_callback(client,
	                                       chan,
	                                       DMA_RESOURCE_ADDED);
		}
		while (client->chans_desired < client->chan_count) {
			spin_lock_irqsave(&client->lock, flags);
			chan = list_entry(client->channels.next,
			                  struct dma_chan,
			                  client_node);
			list_del_rcu(&chan->client_node);
			spin_unlock_irqrestore(&client->lock, flags);
			client->chan_count--;
			client->event_callback(client,
			                       chan,
			                       DMA_RESOURCE_REMOVED);
			dma_client_chan_free(chan);
		}
	}

	mutex_unlock(&dma_list_mutex);
}

/**
 * dma_async_client_register - allocate and register a &dma_client
 * @event_callback: callback for notification of channel addition/removal
 */
struct dma_client *dma_async_client_register(dma_event_callback event_callback)
{
	struct dma_client *client;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return NULL;

	INIT_LIST_HEAD(&client->channels);
	spin_lock_init(&client->lock);
	client->chans_desired = 0;
	client->chan_count = 0;
	client->event_callback = event_callback;

	mutex_lock(&dma_list_mutex);
	list_add_tail(&client->global_node, &dma_client_list);
	mutex_unlock(&dma_list_mutex);

	return client;
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
	struct dma_chan *chan;

	if (!client)
		return;

	rcu_read_lock();
	list_for_each_entry_rcu(chan, &client->channels, client_node)
		dma_client_chan_free(chan);
	rcu_read_unlock();

	mutex_lock(&dma_list_mutex);
	list_del(&client->global_node);
	mutex_unlock(&dma_list_mutex);

	kfree(client);
	dma_chans_rebalance();
}
EXPORT_SYMBOL(dma_async_client_unregister);

/**
 * dma_async_client_chan_request - request DMA channels
 * @client: &dma_client
 * @number: count of DMA channels requested
 *
 * Clients call dma_async_client_chan_request() to specify how many
 * DMA channels they need, 0 to free all currently allocated.
 * The resulting allocations/frees are indicated to the client via the
 * event callback.
 */
void dma_async_client_chan_request(struct dma_client *client,
			unsigned int number)
{
	client->chans_desired = number;
	dma_chans_rebalance();
}
EXPORT_SYMBOL(dma_async_client_chan_request);

/**
 * dma_async_device_register - registers DMA devices found
 * @device: &dma_device
 */
int dma_async_device_register(struct dma_device *device)
{
	static int id;
	int chancnt = 0;
	struct dma_chan* chan;

	if (!device)
		return -ENODEV;

	init_completion(&device->done);
	kref_init(&device->refcount);
	device->dev_id = id++;

	/* represent channels in sysfs. Probably want devs too */
	list_for_each_entry(chan, &device->channels, device_node) {
		chan->local = alloc_percpu(typeof(*chan->local));
		if (chan->local == NULL)
			continue;

		chan->chan_id = chancnt++;
		chan->class_dev.class = &dma_devclass;
		chan->class_dev.dev = NULL;
		snprintf(chan->class_dev.class_id, BUS_ID_SIZE, "dma%dchan%d",
		         device->dev_id, chan->chan_id);

		kref_get(&device->refcount);
		class_device_register(&chan->class_dev);
	}

	mutex_lock(&dma_list_mutex);
	list_add_tail(&device->global_node, &dma_device_list);
	mutex_unlock(&dma_list_mutex);

	dma_chans_rebalance();

	return 0;
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
	unsigned long flags;

	mutex_lock(&dma_list_mutex);
	list_del(&device->global_node);
	mutex_unlock(&dma_list_mutex);

	list_for_each_entry(chan, &device->channels, device_node) {
		if (chan->client) {
			spin_lock_irqsave(&chan->client->lock, flags);
			list_del(&chan->client_node);
			chan->client->chan_count--;
			spin_unlock_irqrestore(&chan->client->lock, flags);
			chan->client->event_callback(chan->client,
			                             chan,
			                             DMA_RESOURCE_REMOVED);
			dma_client_chan_free(chan);
		}
		class_device_unregister(&chan->class_dev);
	}
	dma_chans_rebalance();

	kref_put(&device->refcount, dma_async_device_cleanup);
	wait_for_completion(&device->done);
}
EXPORT_SYMBOL(dma_async_device_unregister);

static int __init dma_bus_init(void)
{
	mutex_init(&dma_list_mutex);
	return class_register(&dma_devclass);
}
subsys_initcall(dma_bus_init);

