// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Char device for device raw access
 *
 * Copyright (C) 2005-2007  Kristian Hoegsberg <krh@bitplanet.net>
 */

#include <linux/bug.h>
#include <linux/compat.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/firewire.h>
#include <linux/firewire-cdev.h>
#include <linux/irqflags.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/sched.h> /* required for linux/wait.h */
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/workqueue.h>


#include "core.h"
#include <trace/events/firewire.h>

#include "packet-header-definitions.h"

/*
 * ABI version history is documented in linux/firewire-cdev.h.
 */
#define FW_CDEV_KERNEL_VERSION			6
#define FW_CDEV_VERSION_EVENT_REQUEST2		4
#define FW_CDEV_VERSION_ALLOCATE_REGION_END	4
#define FW_CDEV_VERSION_AUTO_FLUSH_ISO_OVERFLOW	5
#define FW_CDEV_VERSION_EVENT_ASYNC_TSTAMP	6

static DEFINE_SPINLOCK(phy_receiver_list_lock);
static LIST_HEAD(phy_receiver_list);

struct client {
	u32 version;
	struct fw_device *device;

	spinlock_t lock;
	bool in_shutdown;
	struct xarray resource_xa;
	struct list_head event_list;
	wait_queue_head_t wait;
	wait_queue_head_t tx_flush_wait;
	u64 bus_reset_closure;

	struct fw_iso_context *iso_context;
	u64 iso_closure;
	struct fw_iso_buffer buffer;
	unsigned long vm_start;
	bool buffer_is_mapped;

	struct list_head phy_receiver_link;
	u64 phy_receiver_closure;

	struct list_head link;
	struct kref kref;
};

static inline void client_get(struct client *client)
{
	kref_get(&client->kref);
}

static void client_release(struct kref *kref)
{
	struct client *client = container_of(kref, struct client, kref);

	fw_device_put(client->device);
	kfree(client);
}

static void client_put(struct client *client)
{
	kref_put(&client->kref, client_release);
}

struct client_resource;
typedef void (*client_resource_release_fn_t)(struct client *,
					     struct client_resource *);
struct client_resource {
	client_resource_release_fn_t release;
	int handle;
};

struct address_handler_resource {
	struct client_resource resource;
	struct fw_address_handler handler;
	__u64 closure;
	struct client *client;
};

struct outbound_transaction_resource {
	struct client_resource resource;
	struct fw_transaction transaction;
};

struct inbound_transaction_resource {
	struct client_resource resource;
	struct fw_card *card;
	struct fw_request *request;
	bool is_fcp;
	void *data;
	size_t length;
};

struct descriptor_resource {
	struct client_resource resource;
	struct fw_descriptor descriptor;
	u32 data[];
};

struct iso_resource {
	struct client_resource resource;
	struct client *client;
	/* Schedule work and access todo only with client->lock held. */
	struct delayed_work work;
	enum {ISO_RES_ALLOC, ISO_RES_REALLOC, ISO_RES_DEALLOC,
	      ISO_RES_ALLOC_ONCE, ISO_RES_DEALLOC_ONCE,} todo;
	int generation;
	u64 channels;
	s32 bandwidth;
	struct iso_resource_event *e_alloc, *e_dealloc;
};

static struct address_handler_resource *to_address_handler_resource(struct client_resource *resource)
{
	return container_of(resource, struct address_handler_resource, resource);
}

static struct inbound_transaction_resource *to_inbound_transaction_resource(struct client_resource *resource)
{
	return container_of(resource, struct inbound_transaction_resource, resource);
}

static struct descriptor_resource *to_descriptor_resource(struct client_resource *resource)
{
	return container_of(resource, struct descriptor_resource, resource);
}

static struct iso_resource *to_iso_resource(struct client_resource *resource)
{
	return container_of(resource, struct iso_resource, resource);
}

static void release_iso_resource(struct client *, struct client_resource *);

static int is_iso_resource(const struct client_resource *resource)
{
	return resource->release == release_iso_resource;
}

static void release_transaction(struct client *client,
				struct client_resource *resource);

static int is_outbound_transaction_resource(const struct client_resource *resource)
{
	return resource->release == release_transaction;
}

static void schedule_iso_resource(struct iso_resource *r, unsigned long delay)
{
	client_get(r->client);
	if (!queue_delayed_work(fw_workqueue, &r->work, delay))
		client_put(r->client);
}

/*
 * dequeue_event() just kfree()'s the event, so the event has to be
 * the first field in a struct XYZ_event.
 */
struct event {
	struct { void *data; size_t size; } v[2];
	struct list_head link;
};

struct bus_reset_event {
	struct event event;
	struct fw_cdev_event_bus_reset reset;
};

struct outbound_transaction_event {
	struct event event;
	struct client *client;
	struct outbound_transaction_resource r;
	union {
		struct fw_cdev_event_response without_tstamp;
		struct fw_cdev_event_response2 with_tstamp;
	} rsp;
};

struct inbound_transaction_event {
	struct event event;
	union {
		struct fw_cdev_event_request request;
		struct fw_cdev_event_request2 request2;
		struct fw_cdev_event_request3 with_tstamp;
	} req;
};

struct iso_interrupt_event {
	struct event event;
	struct fw_cdev_event_iso_interrupt interrupt;
};

struct iso_interrupt_mc_event {
	struct event event;
	struct fw_cdev_event_iso_interrupt_mc interrupt;
};

struct iso_resource_event {
	struct event event;
	struct fw_cdev_event_iso_resource iso_resource;
};

struct outbound_phy_packet_event {
	struct event event;
	struct client *client;
	struct fw_packet p;
	union {
		struct fw_cdev_event_phy_packet without_tstamp;
		struct fw_cdev_event_phy_packet2 with_tstamp;
	} phy_packet;
};

struct inbound_phy_packet_event {
	struct event event;
	union {
		struct fw_cdev_event_phy_packet without_tstamp;
		struct fw_cdev_event_phy_packet2 with_tstamp;
	} phy_packet;
};

#ifdef CONFIG_COMPAT
static void __user *u64_to_uptr(u64 value)
{
	if (in_compat_syscall())
		return compat_ptr(value);
	else
		return (void __user *)(unsigned long)value;
}

static u64 uptr_to_u64(void __user *ptr)
{
	if (in_compat_syscall())
		return ptr_to_compat(ptr);
	else
		return (u64)(unsigned long)ptr;
}
#else
static inline void __user *u64_to_uptr(u64 value)
{
	return (void __user *)(unsigned long)value;
}

static inline u64 uptr_to_u64(void __user *ptr)
{
	return (u64)(unsigned long)ptr;
}
#endif /* CONFIG_COMPAT */

static int fw_device_op_open(struct inode *inode, struct file *file)
{
	struct fw_device *device;
	struct client *client;

	device = fw_device_get_by_devt(inode->i_rdev);
	if (device == NULL)
		return -ENODEV;

	if (fw_device_is_shutdown(device)) {
		fw_device_put(device);
		return -ENODEV;
	}

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (client == NULL) {
		fw_device_put(device);
		return -ENOMEM;
	}

	client->device = device;
	spin_lock_init(&client->lock);
	xa_init_flags(&client->resource_xa, XA_FLAGS_ALLOC1 | XA_FLAGS_LOCK_BH);
	INIT_LIST_HEAD(&client->event_list);
	init_waitqueue_head(&client->wait);
	init_waitqueue_head(&client->tx_flush_wait);
	INIT_LIST_HEAD(&client->phy_receiver_link);
	INIT_LIST_HEAD(&client->link);
	kref_init(&client->kref);

	file->private_data = client;

	return nonseekable_open(inode, file);
}

static void queue_event(struct client *client, struct event *event,
			void *data0, size_t size0, void *data1, size_t size1)
{
	event->v[0].data = data0;
	event->v[0].size = size0;
	event->v[1].data = data1;
	event->v[1].size = size1;

	scoped_guard(spinlock_irqsave, &client->lock) {
		if (client->in_shutdown)
			kfree(event);
		else
			list_add_tail(&event->link, &client->event_list);
	}

	wake_up_interruptible(&client->wait);
}

static int dequeue_event(struct client *client,
			 char __user *buffer, size_t count)
{
	struct event *event;
	size_t size, total;
	int i, ret;

	ret = wait_event_interruptible(client->wait,
			!list_empty(&client->event_list) ||
			fw_device_is_shutdown(client->device));
	if (ret < 0)
		return ret;

	if (list_empty(&client->event_list) &&
		       fw_device_is_shutdown(client->device))
		return -ENODEV;

	scoped_guard(spinlock_irq, &client->lock) {
		event = list_first_entry(&client->event_list, struct event, link);
		list_del(&event->link);
	}

	total = 0;
	for (i = 0; i < ARRAY_SIZE(event->v) && total < count; i++) {
		size = min(event->v[i].size, count - total);
		if (copy_to_user(buffer + total, event->v[i].data, size)) {
			ret = -EFAULT;
			goto out;
		}
		total += size;
	}
	ret = total;

 out:
	kfree(event);

	return ret;
}

static ssize_t fw_device_op_read(struct file *file, char __user *buffer,
				 size_t count, loff_t *offset)
{
	struct client *client = file->private_data;

	return dequeue_event(client, buffer, count);
}

static void fill_bus_reset_event(struct fw_cdev_event_bus_reset *event,
				 struct client *client)
{
	struct fw_card *card = client->device->card;

	guard(spinlock_irq)(&card->lock);

	event->closure	     = client->bus_reset_closure;
	event->type          = FW_CDEV_EVENT_BUS_RESET;
	event->generation    = client->device->generation;
	event->node_id       = client->device->node_id;
	event->local_node_id = card->local_node->node_id;
	event->bm_node_id    = card->bm_node_id;
	event->irm_node_id   = card->irm_node->node_id;
	event->root_node_id  = card->root_node->node_id;
}

static void for_each_client(struct fw_device *device,
			    void (*callback)(struct client *client))
{
	struct client *c;

	guard(mutex)(&device->client_list_mutex);

	list_for_each_entry(c, &device->client_list, link)
		callback(c);
}

static void queue_bus_reset_event(struct client *client)
{
	struct bus_reset_event *e;
	struct client_resource *resource;
	unsigned long index;

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (e == NULL)
		return;

	fill_bus_reset_event(&e->reset, client);

	queue_event(client, &e->event,
		    &e->reset, sizeof(e->reset), NULL, 0);

	guard(spinlock_irq)(&client->lock);

	xa_for_each(&client->resource_xa, index, resource) {
		if (is_iso_resource(resource))
			schedule_iso_resource(to_iso_resource(resource), 0);
	}
}

void fw_device_cdev_update(struct fw_device *device)
{
	for_each_client(device, queue_bus_reset_event);
}

static void wake_up_client(struct client *client)
{
	wake_up_interruptible(&client->wait);
}

void fw_device_cdev_remove(struct fw_device *device)
{
	for_each_client(device, wake_up_client);
}

union ioctl_arg {
	struct fw_cdev_get_info			get_info;
	struct fw_cdev_send_request		send_request;
	struct fw_cdev_allocate			allocate;
	struct fw_cdev_deallocate		deallocate;
	struct fw_cdev_send_response		send_response;
	struct fw_cdev_initiate_bus_reset	initiate_bus_reset;
	struct fw_cdev_add_descriptor		add_descriptor;
	struct fw_cdev_remove_descriptor	remove_descriptor;
	struct fw_cdev_create_iso_context	create_iso_context;
	struct fw_cdev_queue_iso		queue_iso;
	struct fw_cdev_start_iso		start_iso;
	struct fw_cdev_stop_iso			stop_iso;
	struct fw_cdev_get_cycle_timer		get_cycle_timer;
	struct fw_cdev_allocate_iso_resource	allocate_iso_resource;
	struct fw_cdev_send_stream_packet	send_stream_packet;
	struct fw_cdev_get_cycle_timer2		get_cycle_timer2;
	struct fw_cdev_send_phy_packet		send_phy_packet;
	struct fw_cdev_receive_phy_packets	receive_phy_packets;
	struct fw_cdev_set_iso_channels		set_iso_channels;
	struct fw_cdev_flush_iso		flush_iso;
};

static int ioctl_get_info(struct client *client, union ioctl_arg *arg)
{
	struct fw_cdev_get_info *a = &arg->get_info;
	struct fw_cdev_event_bus_reset bus_reset;
	unsigned long ret = 0;

	client->version = a->version;
	a->version = FW_CDEV_KERNEL_VERSION;
	a->card = client->device->card->index;

	scoped_guard(rwsem_read, &fw_device_rwsem) {
		if (a->rom != 0) {
			size_t want = a->rom_length;
			size_t have = client->device->config_rom_length * 4;

			ret = copy_to_user(u64_to_uptr(a->rom), client->device->config_rom,
					   min(want, have));
			if (ret != 0)
				return -EFAULT;
		}
		a->rom_length = client->device->config_rom_length * 4;
	}

	guard(mutex)(&client->device->client_list_mutex);

	client->bus_reset_closure = a->bus_reset_closure;
	if (a->bus_reset != 0) {
		fill_bus_reset_event(&bus_reset, client);
		/* unaligned size of bus_reset is 36 bytes */
		ret = copy_to_user(u64_to_uptr(a->bus_reset), &bus_reset, 36);
	}
	if (ret == 0 && list_empty(&client->link))
		list_add_tail(&client->link, &client->device->client_list);

	return ret ? -EFAULT : 0;
}

static int add_client_resource(struct client *client, struct client_resource *resource,
			       gfp_t gfp_mask)
{
	int ret;

	scoped_guard(spinlock_irqsave, &client->lock) {
		u32 index;

		if (client->in_shutdown) {
			ret = -ECANCELED;
		} else {
			if (gfpflags_allow_blocking(gfp_mask)) {
				ret = xa_alloc(&client->resource_xa, &index, resource, xa_limit_32b,
					       GFP_NOWAIT);
			} else {
				ret = xa_alloc_bh(&client->resource_xa, &index, resource,
						  xa_limit_32b, GFP_NOWAIT);
			}
		}
		if (ret >= 0) {
			resource->handle = index;
			client_get(client);
			if (is_iso_resource(resource))
				schedule_iso_resource(to_iso_resource(resource), 0);
		}
	}

	return ret < 0 ? ret : 0;
}

static int release_client_resource(struct client *client, u32 handle,
				   client_resource_release_fn_t release,
				   struct client_resource **return_resource)
{
	unsigned long index = handle;
	struct client_resource *resource;

	scoped_guard(spinlock_irq, &client->lock) {
		if (client->in_shutdown)
			return -EINVAL;

		resource = xa_load(&client->resource_xa, index);
		if (!resource || resource->release != release)
			return -EINVAL;

		xa_erase(&client->resource_xa, handle);
	}

	if (return_resource)
		*return_resource = resource;
	else
		resource->release(client, resource);

	client_put(client);

	return 0;
}

static void release_transaction(struct client *client,
				struct client_resource *resource)
{
}

static void complete_transaction(struct fw_card *card, int rcode, u32 request_tstamp,
				 u32 response_tstamp, void *payload, size_t length, void *data)
{
	struct outbound_transaction_event *e = data;
	struct client *client = e->client;
	unsigned long index = e->r.resource.handle;

	scoped_guard(spinlock_irqsave, &client->lock) {
		xa_erase(&client->resource_xa, index);
		if (client->in_shutdown)
			wake_up(&client->tx_flush_wait);
	}

	switch (e->rsp.without_tstamp.type) {
	case FW_CDEV_EVENT_RESPONSE:
	{
		struct fw_cdev_event_response *rsp = &e->rsp.without_tstamp;

		if (length < rsp->length)
			rsp->length = length;
		if (rcode == RCODE_COMPLETE)
			memcpy(rsp->data, payload, rsp->length);

		rsp->rcode = rcode;

		// In the case that sizeof(*rsp) doesn't align with the position of the
		// data, and the read is short, preserve an extra copy of the data
		// to stay compatible with a pre-2.6.27 bug.  Since the bug is harmless
		// for short reads and some apps depended on it, this is both safe
		// and prudent for compatibility.
		if (rsp->length <= sizeof(*rsp) - offsetof(typeof(*rsp), data))
			queue_event(client, &e->event, rsp, sizeof(*rsp), rsp->data, rsp->length);
		else
			queue_event(client, &e->event, rsp, sizeof(*rsp) + rsp->length, NULL, 0);

		break;
	}
	case FW_CDEV_EVENT_RESPONSE2:
	{
		struct fw_cdev_event_response2 *rsp = &e->rsp.with_tstamp;

		if (length < rsp->length)
			rsp->length = length;
		if (rcode == RCODE_COMPLETE)
			memcpy(rsp->data, payload, rsp->length);

		rsp->rcode = rcode;
		rsp->request_tstamp = request_tstamp;
		rsp->response_tstamp = response_tstamp;

		queue_event(client, &e->event, rsp, sizeof(*rsp) + rsp->length, NULL, 0);

		break;
	}
	default:
		WARN_ON(1);
		break;
	}

	// Drop the xarray's reference.
	client_put(client);
}

static int init_request(struct client *client,
			struct fw_cdev_send_request *request,
			int destination_id, int speed)
{
	struct outbound_transaction_event *e;
	void *payload;
	int ret;

	if (request->tcode != TCODE_STREAM_DATA &&
	    (request->length > 4096 || request->length > 512 << speed))
		return -EIO;

	if (request->tcode == TCODE_WRITE_QUADLET_REQUEST &&
	    request->length < 4)
		return -EINVAL;

	e = kmalloc(sizeof(*e) + request->length, GFP_KERNEL);
	if (e == NULL)
		return -ENOMEM;
	e->client = client;

	if (client->version < FW_CDEV_VERSION_EVENT_ASYNC_TSTAMP) {
		struct fw_cdev_event_response *rsp = &e->rsp.without_tstamp;

		rsp->type = FW_CDEV_EVENT_RESPONSE;
		rsp->length = request->length;
		rsp->closure = request->closure;
		payload = rsp->data;
	} else {
		struct fw_cdev_event_response2 *rsp = &e->rsp.with_tstamp;

		rsp->type = FW_CDEV_EVENT_RESPONSE2;
		rsp->length = request->length;
		rsp->closure = request->closure;
		payload = rsp->data;
	}

	if (request->data && copy_from_user(payload, u64_to_uptr(request->data), request->length)) {
		ret = -EFAULT;
		goto failed;
	}

	e->r.resource.release = release_transaction;
	ret = add_client_resource(client, &e->r.resource, GFP_KERNEL);
	if (ret < 0)
		goto failed;

	fw_send_request_with_tstamp(client->device->card, &e->r.transaction, request->tcode,
				    destination_id, request->generation, speed, request->offset,
				    payload, request->length, complete_transaction, e);
	return 0;

 failed:
	kfree(e);

	return ret;
}

static int ioctl_send_request(struct client *client, union ioctl_arg *arg)
{
	switch (arg->send_request.tcode) {
	case TCODE_WRITE_QUADLET_REQUEST:
	case TCODE_WRITE_BLOCK_REQUEST:
	case TCODE_READ_QUADLET_REQUEST:
	case TCODE_READ_BLOCK_REQUEST:
	case TCODE_LOCK_MASK_SWAP:
	case TCODE_LOCK_COMPARE_SWAP:
	case TCODE_LOCK_FETCH_ADD:
	case TCODE_LOCK_LITTLE_ADD:
	case TCODE_LOCK_BOUNDED_ADD:
	case TCODE_LOCK_WRAP_ADD:
	case TCODE_LOCK_VENDOR_DEPENDENT:
		break;
	default:
		return -EINVAL;
	}

	return init_request(client, &arg->send_request, client->device->node_id,
			    client->device->max_speed);
}

static void release_request(struct client *client,
			    struct client_resource *resource)
{
	struct inbound_transaction_resource *r = to_inbound_transaction_resource(resource);

	if (r->is_fcp)
		fw_request_put(r->request);
	else
		fw_send_response(r->card, r->request, RCODE_CONFLICT_ERROR);

	fw_card_put(r->card);
	kfree(r);
}

static void handle_request(struct fw_card *card, struct fw_request *request,
			   int tcode, int destination, int source,
			   int generation, unsigned long long offset,
			   void *payload, size_t length, void *callback_data)
{
	struct address_handler_resource *handler = callback_data;
	bool is_fcp = is_in_fcp_region(offset, length);
	struct inbound_transaction_resource *r;
	struct inbound_transaction_event *e;
	size_t event_size0;
	int ret;

	/* card may be different from handler->client->device->card */
	fw_card_get(card);

	// Extend the lifetime of data for request so that its payload is safely accessible in
	// the process context for the client.
	if (is_fcp)
		fw_request_get(request);

	r = kmalloc(sizeof(*r), GFP_ATOMIC);
	e = kmalloc(sizeof(*e), GFP_ATOMIC);
	if (r == NULL || e == NULL)
		goto failed;

	r->card    = card;
	r->request = request;
	r->is_fcp  = is_fcp;
	r->data    = payload;
	r->length  = length;

	r->resource.release = release_request;
	ret = add_client_resource(handler->client, &r->resource, GFP_ATOMIC);
	if (ret < 0)
		goto failed;

	if (handler->client->version < FW_CDEV_VERSION_EVENT_REQUEST2) {
		struct fw_cdev_event_request *req = &e->req.request;

		if (tcode & 0x10)
			tcode = TCODE_LOCK_REQUEST;

		req->type	= FW_CDEV_EVENT_REQUEST;
		req->tcode	= tcode;
		req->offset	= offset;
		req->length	= length;
		req->handle	= r->resource.handle;
		req->closure	= handler->closure;
		event_size0	= sizeof(*req);
	} else if (handler->client->version < FW_CDEV_VERSION_EVENT_ASYNC_TSTAMP) {
		struct fw_cdev_event_request2 *req = &e->req.request2;

		req->type	= FW_CDEV_EVENT_REQUEST2;
		req->tcode	= tcode;
		req->offset	= offset;
		req->source_node_id = source;
		req->destination_node_id = destination;
		req->card	= card->index;
		req->generation	= generation;
		req->length	= length;
		req->handle	= r->resource.handle;
		req->closure	= handler->closure;
		event_size0	= sizeof(*req);
	} else {
		struct fw_cdev_event_request3 *req = &e->req.with_tstamp;

		req->type	= FW_CDEV_EVENT_REQUEST3;
		req->tcode	= tcode;
		req->offset	= offset;
		req->source_node_id = source;
		req->destination_node_id = destination;
		req->card	= card->index;
		req->generation	= generation;
		req->length	= length;
		req->handle	= r->resource.handle;
		req->closure	= handler->closure;
		req->tstamp	= fw_request_get_timestamp(request);
		event_size0	= sizeof(*req);
	}

	queue_event(handler->client, &e->event,
		    &e->req, event_size0, r->data, length);
	return;

 failed:
	kfree(r);
	kfree(e);

	if (!is_fcp)
		fw_send_response(card, request, RCODE_CONFLICT_ERROR);
	else
		fw_request_put(request);

	fw_card_put(card);
}

static void release_address_handler(struct client *client,
				    struct client_resource *resource)
{
	struct address_handler_resource *r = to_address_handler_resource(resource);

	fw_core_remove_address_handler(&r->handler);
	kfree(r);
}

static int ioctl_allocate(struct client *client, union ioctl_arg *arg)
{
	struct fw_cdev_allocate *a = &arg->allocate;
	struct address_handler_resource *r;
	struct fw_address_region region;
	int ret;

	r = kmalloc(sizeof(*r), GFP_KERNEL);
	if (r == NULL)
		return -ENOMEM;

	region.start = a->offset;
	if (client->version < FW_CDEV_VERSION_ALLOCATE_REGION_END)
		region.end = a->offset + a->length;
	else
		region.end = a->region_end;

	r->handler.length           = a->length;
	r->handler.address_callback = handle_request;
	r->handler.callback_data    = r;
	r->closure   = a->closure;
	r->client    = client;

	ret = fw_core_add_address_handler(&r->handler, &region);
	if (ret < 0) {
		kfree(r);
		return ret;
	}
	a->offset = r->handler.offset;

	r->resource.release = release_address_handler;
	ret = add_client_resource(client, &r->resource, GFP_KERNEL);
	if (ret < 0) {
		release_address_handler(client, &r->resource);
		return ret;
	}
	a->handle = r->resource.handle;

	return 0;
}

static int ioctl_deallocate(struct client *client, union ioctl_arg *arg)
{
	return release_client_resource(client, arg->deallocate.handle,
				       release_address_handler, NULL);
}

static int ioctl_send_response(struct client *client, union ioctl_arg *arg)
{
	struct fw_cdev_send_response *a = &arg->send_response;
	struct client_resource *resource;
	struct inbound_transaction_resource *r;
	int ret = 0;

	if (release_client_resource(client, a->handle,
				    release_request, &resource) < 0)
		return -EINVAL;

	r = to_inbound_transaction_resource(resource);
	if (r->is_fcp) {
		fw_request_put(r->request);
		goto out;
	}

	if (a->length != fw_get_response_length(r->request)) {
		ret = -EINVAL;
		fw_request_put(r->request);
		goto out;
	}
	if (copy_from_user(r->data, u64_to_uptr(a->data), a->length)) {
		ret = -EFAULT;
		fw_request_put(r->request);
		goto out;
	}
	fw_send_response(r->card, r->request, a->rcode);
 out:
	fw_card_put(r->card);
	kfree(r);

	return ret;
}

static int ioctl_initiate_bus_reset(struct client *client, union ioctl_arg *arg)
{
	fw_schedule_bus_reset(client->device->card, true,
			arg->initiate_bus_reset.type == FW_CDEV_SHORT_RESET);
	return 0;
}

static void release_descriptor(struct client *client,
			       struct client_resource *resource)
{
	struct descriptor_resource *r = to_descriptor_resource(resource);

	fw_core_remove_descriptor(&r->descriptor);
	kfree(r);
}

static int ioctl_add_descriptor(struct client *client, union ioctl_arg *arg)
{
	struct fw_cdev_add_descriptor *a = &arg->add_descriptor;
	struct descriptor_resource *r;
	int ret;

	/* Access policy: Allow this ioctl only on local nodes' device files. */
	if (!client->device->is_local)
		return -ENOSYS;

	if (a->length > 256)
		return -EINVAL;

	r = kmalloc(struct_size(r, data, a->length), GFP_KERNEL);
	if (r == NULL)
		return -ENOMEM;

	if (copy_from_user(r->data, u64_to_uptr(a->data),
			   flex_array_size(r, data, a->length))) {
		ret = -EFAULT;
		goto failed;
	}

	r->descriptor.length    = a->length;
	r->descriptor.immediate = a->immediate;
	r->descriptor.key       = a->key;
	r->descriptor.data      = r->data;

	ret = fw_core_add_descriptor(&r->descriptor);
	if (ret < 0)
		goto failed;

	r->resource.release = release_descriptor;
	ret = add_client_resource(client, &r->resource, GFP_KERNEL);
	if (ret < 0) {
		fw_core_remove_descriptor(&r->descriptor);
		goto failed;
	}
	a->handle = r->resource.handle;

	return 0;
 failed:
	kfree(r);

	return ret;
}

static int ioctl_remove_descriptor(struct client *client, union ioctl_arg *arg)
{
	return release_client_resource(client, arg->remove_descriptor.handle,
				       release_descriptor, NULL);
}

static void iso_callback(struct fw_iso_context *context, u32 cycle,
			 size_t header_length, void *header, void *data)
{
	struct client *client = data;
	struct iso_interrupt_event *e;

	e = kmalloc(sizeof(*e) + header_length, GFP_KERNEL);
	if (e == NULL)
		return;

	e->interrupt.type      = FW_CDEV_EVENT_ISO_INTERRUPT;
	e->interrupt.closure   = client->iso_closure;
	e->interrupt.cycle     = cycle;
	e->interrupt.header_length = header_length;
	memcpy(e->interrupt.header, header, header_length);
	queue_event(client, &e->event, &e->interrupt,
		    sizeof(e->interrupt) + header_length, NULL, 0);
}

static void iso_mc_callback(struct fw_iso_context *context,
			    dma_addr_t completed, void *data)
{
	struct client *client = data;
	struct iso_interrupt_mc_event *e;

	e = kmalloc(sizeof(*e), GFP_KERNEL);
	if (e == NULL)
		return;

	e->interrupt.type      = FW_CDEV_EVENT_ISO_INTERRUPT_MULTICHANNEL;
	e->interrupt.closure   = client->iso_closure;
	e->interrupt.completed = fw_iso_buffer_lookup(&client->buffer,
						      completed);
	queue_event(client, &e->event, &e->interrupt,
		    sizeof(e->interrupt), NULL, 0);
}

static enum dma_data_direction iso_dma_direction(struct fw_iso_context *context)
{
		if (context->type == FW_ISO_CONTEXT_TRANSMIT)
			return DMA_TO_DEVICE;
		else
			return DMA_FROM_DEVICE;
}

static struct fw_iso_context *fw_iso_mc_context_create(struct fw_card *card,
						fw_iso_mc_callback_t callback,
						void *callback_data)
{
	struct fw_iso_context *ctx;

	ctx = fw_iso_context_create(card, FW_ISO_CONTEXT_RECEIVE_MULTICHANNEL,
				    0, 0, 0, NULL, callback_data);
	if (!IS_ERR(ctx))
		ctx->callback.mc = callback;

	return ctx;
}

static int ioctl_create_iso_context(struct client *client, union ioctl_arg *arg)
{
	struct fw_cdev_create_iso_context *a = &arg->create_iso_context;
	struct fw_iso_context *context;
	union fw_iso_callback cb;
	int ret;

	BUILD_BUG_ON(FW_CDEV_ISO_CONTEXT_TRANSMIT != FW_ISO_CONTEXT_TRANSMIT ||
		     FW_CDEV_ISO_CONTEXT_RECEIVE  != FW_ISO_CONTEXT_RECEIVE  ||
		     FW_CDEV_ISO_CONTEXT_RECEIVE_MULTICHANNEL !=
					FW_ISO_CONTEXT_RECEIVE_MULTICHANNEL);

	switch (a->type) {
	case FW_ISO_CONTEXT_TRANSMIT:
		if (a->speed > SCODE_3200 || a->channel > 63)
			return -EINVAL;

		cb.sc = iso_callback;
		break;

	case FW_ISO_CONTEXT_RECEIVE:
		if (a->header_size < 4 || (a->header_size & 3) ||
		    a->channel > 63)
			return -EINVAL;

		cb.sc = iso_callback;
		break;

	case FW_ISO_CONTEXT_RECEIVE_MULTICHANNEL:
		cb.mc = iso_mc_callback;
		break;

	default:
		return -EINVAL;
	}

	if (a->type == FW_ISO_CONTEXT_RECEIVE_MULTICHANNEL)
		context = fw_iso_mc_context_create(client->device->card, cb.mc,
						   client);
	else
		context = fw_iso_context_create(client->device->card, a->type,
						a->channel, a->speed,
						a->header_size, cb.sc, client);
	if (IS_ERR(context))
		return PTR_ERR(context);
	if (client->version < FW_CDEV_VERSION_AUTO_FLUSH_ISO_OVERFLOW)
		context->drop_overflow_headers = true;

	// We only support one context at this time.
	guard(spinlock_irq)(&client->lock);

	if (client->iso_context != NULL) {
		fw_iso_context_destroy(context);

		return -EBUSY;
	}
	if (!client->buffer_is_mapped) {
		ret = fw_iso_buffer_map_dma(&client->buffer,
					    client->device->card,
					    iso_dma_direction(context));
		if (ret < 0) {
			fw_iso_context_destroy(context);

			return ret;
		}
		client->buffer_is_mapped = true;
	}
	client->iso_closure = a->closure;
	client->iso_context = context;

	a->handle = 0;

	return 0;
}

static int ioctl_set_iso_channels(struct client *client, union ioctl_arg *arg)
{
	struct fw_cdev_set_iso_channels *a = &arg->set_iso_channels;
	struct fw_iso_context *ctx = client->iso_context;

	if (ctx == NULL || a->handle != 0)
		return -EINVAL;

	return fw_iso_context_set_channels(ctx, &a->channels);
}

/* Macros for decoding the iso packet control header. */
#define GET_PAYLOAD_LENGTH(v)	((v) & 0xffff)
#define GET_INTERRUPT(v)	(((v) >> 16) & 0x01)
#define GET_SKIP(v)		(((v) >> 17) & 0x01)
#define GET_TAG(v)		(((v) >> 18) & 0x03)
#define GET_SY(v)		(((v) >> 20) & 0x0f)
#define GET_HEADER_LENGTH(v)	(((v) >> 24) & 0xff)

static int ioctl_queue_iso(struct client *client, union ioctl_arg *arg)
{
	struct fw_cdev_queue_iso *a = &arg->queue_iso;
	struct fw_cdev_iso_packet __user *p, *end, *next;
	struct fw_iso_context *ctx = client->iso_context;
	unsigned long payload, buffer_end, transmit_header_bytes = 0;
	u32 control;
	int count;
	DEFINE_RAW_FLEX(struct fw_iso_packet, u, header, 64);

	if (ctx == NULL || a->handle != 0)
		return -EINVAL;

	/*
	 * If the user passes a non-NULL data pointer, has mmap()'ed
	 * the iso buffer, and the pointer points inside the buffer,
	 * we setup the payload pointers accordingly.  Otherwise we
	 * set them both to 0, which will still let packets with
	 * payload_length == 0 through.  In other words, if no packets
	 * use the indirect payload, the iso buffer need not be mapped
	 * and the a->data pointer is ignored.
	 */
	payload = (unsigned long)a->data - client->vm_start;
	buffer_end = client->buffer.page_count << PAGE_SHIFT;
	if (a->data == 0 || client->buffer.pages == NULL ||
	    payload >= buffer_end) {
		payload = 0;
		buffer_end = 0;
	}

	if (ctx->type == FW_ISO_CONTEXT_RECEIVE_MULTICHANNEL && payload & 3)
		return -EINVAL;

	p = (struct fw_cdev_iso_packet __user *)u64_to_uptr(a->packets);

	end = (void __user *)p + a->size;
	count = 0;
	while (p < end) {
		if (get_user(control, &p->control))
			return -EFAULT;
		u->payload_length = GET_PAYLOAD_LENGTH(control);
		u->interrupt = GET_INTERRUPT(control);
		u->skip = GET_SKIP(control);
		u->tag = GET_TAG(control);
		u->sy = GET_SY(control);
		u->header_length = GET_HEADER_LENGTH(control);

		switch (ctx->type) {
		case FW_ISO_CONTEXT_TRANSMIT:
			if (u->header_length & 3)
				return -EINVAL;
			transmit_header_bytes = u->header_length;
			break;

		case FW_ISO_CONTEXT_RECEIVE:
			if (u->header_length == 0 ||
			    u->header_length % ctx->header_size != 0)
				return -EINVAL;
			break;

		case FW_ISO_CONTEXT_RECEIVE_MULTICHANNEL:
			if (u->payload_length == 0 ||
			    u->payload_length & 3)
				return -EINVAL;
			break;
		}

		next = (struct fw_cdev_iso_packet __user *)
			&p->header[transmit_header_bytes / 4];
		if (next > end)
			return -EINVAL;
		if (copy_from_user
		    (u->header, p->header, transmit_header_bytes))
			return -EFAULT;
		if (u->skip && ctx->type == FW_ISO_CONTEXT_TRANSMIT &&
		    u->header_length + u->payload_length > 0)
			return -EINVAL;
		if (payload + u->payload_length > buffer_end)
			return -EINVAL;

		if (fw_iso_context_queue(ctx, u, &client->buffer, payload))
			break;

		p = next;
		payload += u->payload_length;
		count++;
	}
	fw_iso_context_queue_flush(ctx);

	a->size    -= uptr_to_u64(p) - a->packets;
	a->packets  = uptr_to_u64(p);
	a->data     = client->vm_start + payload;

	return count;
}

static int ioctl_start_iso(struct client *client, union ioctl_arg *arg)
{
	struct fw_cdev_start_iso *a = &arg->start_iso;

	BUILD_BUG_ON(
	    FW_CDEV_ISO_CONTEXT_MATCH_TAG0 != FW_ISO_CONTEXT_MATCH_TAG0 ||
	    FW_CDEV_ISO_CONTEXT_MATCH_TAG1 != FW_ISO_CONTEXT_MATCH_TAG1 ||
	    FW_CDEV_ISO_CONTEXT_MATCH_TAG2 != FW_ISO_CONTEXT_MATCH_TAG2 ||
	    FW_CDEV_ISO_CONTEXT_MATCH_TAG3 != FW_ISO_CONTEXT_MATCH_TAG3 ||
	    FW_CDEV_ISO_CONTEXT_MATCH_ALL_TAGS != FW_ISO_CONTEXT_MATCH_ALL_TAGS);

	if (client->iso_context == NULL || a->handle != 0)
		return -EINVAL;

	if (client->iso_context->type == FW_ISO_CONTEXT_RECEIVE &&
	    (a->tags == 0 || a->tags > 15 || a->sync > 15))
		return -EINVAL;

	return fw_iso_context_start(client->iso_context,
				    a->cycle, a->sync, a->tags);
}

static int ioctl_stop_iso(struct client *client, union ioctl_arg *arg)
{
	struct fw_cdev_stop_iso *a = &arg->stop_iso;

	if (client->iso_context == NULL || a->handle != 0)
		return -EINVAL;

	return fw_iso_context_stop(client->iso_context);
}

static int ioctl_flush_iso(struct client *client, union ioctl_arg *arg)
{
	struct fw_cdev_flush_iso *a = &arg->flush_iso;

	if (client->iso_context == NULL || a->handle != 0)
		return -EINVAL;

	return fw_iso_context_flush_completions(client->iso_context);
}

static int ioctl_get_cycle_timer2(struct client *client, union ioctl_arg *arg)
{
	struct fw_cdev_get_cycle_timer2 *a = &arg->get_cycle_timer2;
	struct fw_card *card = client->device->card;
	struct timespec64 ts = {0, 0};
	u32 cycle_time = 0;
	int ret;

	guard(irq)();

	ret = fw_card_read_cycle_time(card, &cycle_time);
	if (ret < 0)
		return ret;

	switch (a->clk_id) {
	case CLOCK_REALTIME:      ktime_get_real_ts64(&ts);	break;
	case CLOCK_MONOTONIC:     ktime_get_ts64(&ts);		break;
	case CLOCK_MONOTONIC_RAW: ktime_get_raw_ts64(&ts);	break;
	default:
		return -EINVAL;
	}

	a->tv_sec      = ts.tv_sec;
	a->tv_nsec     = ts.tv_nsec;
	a->cycle_timer = cycle_time;

	return 0;
}

static int ioctl_get_cycle_timer(struct client *client, union ioctl_arg *arg)
{
	struct fw_cdev_get_cycle_timer *a = &arg->get_cycle_timer;
	struct fw_cdev_get_cycle_timer2 ct2;

	ct2.clk_id = CLOCK_REALTIME;
	ioctl_get_cycle_timer2(client, (union ioctl_arg *)&ct2);

	a->local_time = ct2.tv_sec * USEC_PER_SEC + ct2.tv_nsec / NSEC_PER_USEC;
	a->cycle_timer = ct2.cycle_timer;

	return 0;
}

static void iso_resource_work(struct work_struct *work)
{
	struct iso_resource_event *e;
	struct iso_resource *r = from_work(r, work, work.work);
	struct client *client = r->client;
	unsigned long index = r->resource.handle;
	int generation, channel, bandwidth, todo;
	bool skip, free, success;

	scoped_guard(spinlock_irq, &client->lock) {
		generation = client->device->generation;
		todo = r->todo;
		// Allow 1000ms grace period for other reallocations.
		if (todo == ISO_RES_ALLOC &&
		    time_is_after_jiffies64(client->device->card->reset_jiffies + secs_to_jiffies(1))) {
			schedule_iso_resource(r, msecs_to_jiffies(333));
			skip = true;
		} else {
			// We could be called twice within the same generation.
			skip = todo == ISO_RES_REALLOC &&
			       r->generation == generation;
		}
		free = todo == ISO_RES_DEALLOC ||
		       todo == ISO_RES_ALLOC_ONCE ||
		       todo == ISO_RES_DEALLOC_ONCE;
		r->generation = generation;
	}

	if (skip)
		goto out;

	bandwidth = r->bandwidth;

	fw_iso_resource_manage(client->device->card, generation,
			r->channels, &channel, &bandwidth,
			todo == ISO_RES_ALLOC ||
			todo == ISO_RES_REALLOC ||
			todo == ISO_RES_ALLOC_ONCE);
	/*
	 * Is this generation outdated already?  As long as this resource sticks
	 * in the xarray, it will be scheduled again for a newer generation or at
	 * shutdown.
	 */
	if (channel == -EAGAIN &&
	    (todo == ISO_RES_ALLOC || todo == ISO_RES_REALLOC))
		goto out;

	success = channel >= 0 || bandwidth > 0;

	scoped_guard(spinlock_irq, &client->lock) {
		// Transit from allocation to reallocation, except if the client
		// requested deallocation in the meantime.
		if (r->todo == ISO_RES_ALLOC)
			r->todo = ISO_RES_REALLOC;
		// Allocation or reallocation failure?  Pull this resource out of the
		// xarray and prepare for deletion, unless the client is shutting down.
		if (r->todo == ISO_RES_REALLOC && !success &&
		    !client->in_shutdown &&
		    xa_erase(&client->resource_xa, index)) {
			client_put(client);
			free = true;
		}
	}

	if (todo == ISO_RES_ALLOC && channel >= 0)
		r->channels = 1ULL << channel;

	if (todo == ISO_RES_REALLOC && success)
		goto out;

	if (todo == ISO_RES_ALLOC || todo == ISO_RES_ALLOC_ONCE) {
		e = r->e_alloc;
		r->e_alloc = NULL;
	} else {
		e = r->e_dealloc;
		r->e_dealloc = NULL;
	}
	e->iso_resource.handle    = r->resource.handle;
	e->iso_resource.channel   = channel;
	e->iso_resource.bandwidth = bandwidth;

	queue_event(client, &e->event,
		    &e->iso_resource, sizeof(e->iso_resource), NULL, 0);

	if (free) {
		cancel_delayed_work(&r->work);
		kfree(r->e_alloc);
		kfree(r->e_dealloc);
		kfree(r);
	}
 out:
	client_put(client);
}

static void release_iso_resource(struct client *client,
				 struct client_resource *resource)
{
	struct iso_resource *r = to_iso_resource(resource);

	guard(spinlock_irq)(&client->lock);

	r->todo = ISO_RES_DEALLOC;
	schedule_iso_resource(r, 0);
}

static int init_iso_resource(struct client *client,
		struct fw_cdev_allocate_iso_resource *request, int todo)
{
	struct iso_resource_event *e1, *e2;
	struct iso_resource *r;
	int ret;

	if ((request->channels == 0 && request->bandwidth == 0) ||
	    request->bandwidth > BANDWIDTH_AVAILABLE_INITIAL)
		return -EINVAL;

	r  = kmalloc(sizeof(*r), GFP_KERNEL);
	e1 = kmalloc(sizeof(*e1), GFP_KERNEL);
	e2 = kmalloc(sizeof(*e2), GFP_KERNEL);
	if (r == NULL || e1 == NULL || e2 == NULL) {
		ret = -ENOMEM;
		goto fail;
	}

	INIT_DELAYED_WORK(&r->work, iso_resource_work);
	r->client	= client;
	r->todo		= todo;
	r->generation	= -1;
	r->channels	= request->channels;
	r->bandwidth	= request->bandwidth;
	r->e_alloc	= e1;
	r->e_dealloc	= e2;

	e1->iso_resource.closure = request->closure;
	e1->iso_resource.type    = FW_CDEV_EVENT_ISO_RESOURCE_ALLOCATED;
	e2->iso_resource.closure = request->closure;
	e2->iso_resource.type    = FW_CDEV_EVENT_ISO_RESOURCE_DEALLOCATED;

	if (todo == ISO_RES_ALLOC) {
		r->resource.release = release_iso_resource;
		ret = add_client_resource(client, &r->resource, GFP_KERNEL);
		if (ret < 0)
			goto fail;
	} else {
		r->resource.release = NULL;
		r->resource.handle = -1;
		schedule_iso_resource(r, 0);
	}
	request->handle = r->resource.handle;

	return 0;
 fail:
	kfree(r);
	kfree(e1);
	kfree(e2);

	return ret;
}

static int ioctl_allocate_iso_resource(struct client *client,
				       union ioctl_arg *arg)
{
	return init_iso_resource(client,
			&arg->allocate_iso_resource, ISO_RES_ALLOC);
}

static int ioctl_deallocate_iso_resource(struct client *client,
					 union ioctl_arg *arg)
{
	return release_client_resource(client,
			arg->deallocate.handle, release_iso_resource, NULL);
}

static int ioctl_allocate_iso_resource_once(struct client *client,
					    union ioctl_arg *arg)
{
	return init_iso_resource(client,
			&arg->allocate_iso_resource, ISO_RES_ALLOC_ONCE);
}

static int ioctl_deallocate_iso_resource_once(struct client *client,
					      union ioctl_arg *arg)
{
	return init_iso_resource(client,
			&arg->allocate_iso_resource, ISO_RES_DEALLOC_ONCE);
}

/*
 * Returns a speed code:  Maximum speed to or from this device,
 * limited by the device's link speed, the local node's link speed,
 * and all PHY port speeds between the two links.
 */
static int ioctl_get_speed(struct client *client, union ioctl_arg *arg)
{
	return client->device->max_speed;
}

static int ioctl_send_broadcast_request(struct client *client,
					union ioctl_arg *arg)
{
	struct fw_cdev_send_request *a = &arg->send_request;

	switch (a->tcode) {
	case TCODE_WRITE_QUADLET_REQUEST:
	case TCODE_WRITE_BLOCK_REQUEST:
		break;
	default:
		return -EINVAL;
	}

	/* Security policy: Only allow accesses to Units Space. */
	if (a->offset < CSR_REGISTER_BASE + CSR_CONFIG_ROM_END)
		return -EACCES;

	return init_request(client, a, LOCAL_BUS | 0x3f, SCODE_100);
}

static int ioctl_send_stream_packet(struct client *client, union ioctl_arg *arg)
{
	struct fw_cdev_send_stream_packet *a = &arg->send_stream_packet;
	struct fw_cdev_send_request request;
	int dest;

	if (a->speed > client->device->card->link_speed ||
	    a->length > 1024 << a->speed)
		return -EIO;

	if (a->tag > 3 || a->channel > 63 || a->sy > 15)
		return -EINVAL;

	dest = fw_stream_packet_destination_id(a->tag, a->channel, a->sy);
	request.tcode		= TCODE_STREAM_DATA;
	request.length		= a->length;
	request.closure		= a->closure;
	request.data		= a->data;
	request.generation	= a->generation;

	return init_request(client, &request, dest, a->speed);
}

static void outbound_phy_packet_callback(struct fw_packet *packet,
					 struct fw_card *card, int status)
{
	struct outbound_phy_packet_event *e =
		container_of(packet, struct outbound_phy_packet_event, p);
	struct client *e_client = e->client;
	u32 rcode;

	trace_async_phy_outbound_complete((uintptr_t)packet, card->index, status, packet->generation,
					  packet->timestamp);

	switch (status) {
	// expected:
	case ACK_COMPLETE:
		rcode = RCODE_COMPLETE;
		break;
	// should never happen with PHY packets:
	case ACK_PENDING:
		rcode = RCODE_COMPLETE;
		break;
	case ACK_BUSY_X:
	case ACK_BUSY_A:
	case ACK_BUSY_B:
		rcode = RCODE_BUSY;
		break;
	case ACK_DATA_ERROR:
		rcode = RCODE_DATA_ERROR;
		break;
	case ACK_TYPE_ERROR:
		rcode = RCODE_TYPE_ERROR;
		break;
	// stale generation; cancelled; on certain controllers: no ack
	default:
		rcode = status;
		break;
	}

	switch (e->phy_packet.without_tstamp.type) {
	case FW_CDEV_EVENT_PHY_PACKET_SENT:
	{
		struct fw_cdev_event_phy_packet *pp = &e->phy_packet.without_tstamp;

		pp->rcode = rcode;
		pp->data[0] = packet->timestamp;
		queue_event(e->client, &e->event, &e->phy_packet, sizeof(*pp) + pp->length,
			    NULL, 0);
		break;
	}
	case FW_CDEV_EVENT_PHY_PACKET_SENT2:
	{
		struct fw_cdev_event_phy_packet2 *pp = &e->phy_packet.with_tstamp;

		pp->rcode = rcode;
		pp->tstamp = packet->timestamp;
		queue_event(e->client, &e->event, &e->phy_packet, sizeof(*pp) + pp->length,
			    NULL, 0);
		break;
	}
	default:
		WARN_ON(1);
		break;
	}

	client_put(e_client);
}

static int ioctl_send_phy_packet(struct client *client, union ioctl_arg *arg)
{
	struct fw_cdev_send_phy_packet *a = &arg->send_phy_packet;
	struct fw_card *card = client->device->card;
	struct outbound_phy_packet_event *e;

	/* Access policy: Allow this ioctl only on local nodes' device files. */
	if (!client->device->is_local)
		return -ENOSYS;

	e = kzalloc(sizeof(*e) + sizeof(a->data), GFP_KERNEL);
	if (e == NULL)
		return -ENOMEM;

	client_get(client);
	e->client		= client;
	e->p.speed		= SCODE_100;
	e->p.generation		= a->generation;
	async_header_set_tcode(e->p.header, TCODE_LINK_INTERNAL);
	e->p.header[1]		= a->data[0];
	e->p.header[2]		= a->data[1];
	e->p.header_length	= 12;
	e->p.callback		= outbound_phy_packet_callback;

	if (client->version < FW_CDEV_VERSION_EVENT_ASYNC_TSTAMP) {
		struct fw_cdev_event_phy_packet *pp = &e->phy_packet.without_tstamp;

		pp->closure = a->closure;
		pp->type = FW_CDEV_EVENT_PHY_PACKET_SENT;
		if (is_ping_packet(a->data))
			pp->length = 4;
	} else {
		struct fw_cdev_event_phy_packet2 *pp = &e->phy_packet.with_tstamp;

		pp->closure = a->closure;
		pp->type = FW_CDEV_EVENT_PHY_PACKET_SENT2;
		// Keep the data field so that application can match the response event to the
		// request.
		pp->length = sizeof(a->data);
		memcpy(pp->data, a->data, sizeof(a->data));
	}

	trace_async_phy_outbound_initiate((uintptr_t)&e->p, card->index, e->p.generation,
					  e->p.header[1], e->p.header[2]);

	card->driver->send_request(card, &e->p);

	return 0;
}

static int ioctl_receive_phy_packets(struct client *client, union ioctl_arg *arg)
{
	struct fw_cdev_receive_phy_packets *a = &arg->receive_phy_packets;

	/* Access policy: Allow this ioctl only on local nodes' device files. */
	if (!client->device->is_local)
		return -ENOSYS;

	// NOTE: This can be without irq when we can guarantee that __fw_send_request() for local
	// destination never runs in any type of IRQ context.
	scoped_guard(spinlock_irq, &phy_receiver_list_lock)
		list_move_tail(&client->phy_receiver_link, &phy_receiver_list);

	client->phy_receiver_closure = a->closure;

	return 0;
}

void fw_cdev_handle_phy_packet(struct fw_card *card, struct fw_packet *p)
{
	struct client *client;

	// NOTE: This can be without irqsave when we can guarantee that __fw_send_request() for local
	// destination never runs in any type of IRQ context.
	guard(spinlock_irqsave)(&phy_receiver_list_lock);

	list_for_each_entry(client, &phy_receiver_list, phy_receiver_link) {
		struct inbound_phy_packet_event *e;

		if (client->device->card != card)
			continue;

		e = kmalloc(sizeof(*e) + 8, GFP_ATOMIC);
		if (e == NULL)
			break;

		if (client->version < FW_CDEV_VERSION_EVENT_ASYNC_TSTAMP) {
			struct fw_cdev_event_phy_packet *pp = &e->phy_packet.without_tstamp;

			pp->closure = client->phy_receiver_closure;
			pp->type = FW_CDEV_EVENT_PHY_PACKET_RECEIVED;
			pp->rcode = RCODE_COMPLETE;
			pp->length = 8;
			pp->data[0] = p->header[1];
			pp->data[1] = p->header[2];
			queue_event(client, &e->event, &e->phy_packet, sizeof(*pp) + 8, NULL, 0);
		} else {
			struct fw_cdev_event_phy_packet2 *pp = &e->phy_packet.with_tstamp;

			pp = &e->phy_packet.with_tstamp;
			pp->closure = client->phy_receiver_closure;
			pp->type = FW_CDEV_EVENT_PHY_PACKET_RECEIVED2;
			pp->rcode = RCODE_COMPLETE;
			pp->length = 8;
			pp->tstamp = p->timestamp;
			pp->data[0] = p->header[1];
			pp->data[1] = p->header[2];
			queue_event(client, &e->event, &e->phy_packet, sizeof(*pp) + 8, NULL, 0);
		}
	}
}

static int (* const ioctl_handlers[])(struct client *, union ioctl_arg *) = {
	[0x00] = ioctl_get_info,
	[0x01] = ioctl_send_request,
	[0x02] = ioctl_allocate,
	[0x03] = ioctl_deallocate,
	[0x04] = ioctl_send_response,
	[0x05] = ioctl_initiate_bus_reset,
	[0x06] = ioctl_add_descriptor,
	[0x07] = ioctl_remove_descriptor,
	[0x08] = ioctl_create_iso_context,
	[0x09] = ioctl_queue_iso,
	[0x0a] = ioctl_start_iso,
	[0x0b] = ioctl_stop_iso,
	[0x0c] = ioctl_get_cycle_timer,
	[0x0d] = ioctl_allocate_iso_resource,
	[0x0e] = ioctl_deallocate_iso_resource,
	[0x0f] = ioctl_allocate_iso_resource_once,
	[0x10] = ioctl_deallocate_iso_resource_once,
	[0x11] = ioctl_get_speed,
	[0x12] = ioctl_send_broadcast_request,
	[0x13] = ioctl_send_stream_packet,
	[0x14] = ioctl_get_cycle_timer2,
	[0x15] = ioctl_send_phy_packet,
	[0x16] = ioctl_receive_phy_packets,
	[0x17] = ioctl_set_iso_channels,
	[0x18] = ioctl_flush_iso,
};

static int dispatch_ioctl(struct client *client,
			  unsigned int cmd, void __user *arg)
{
	union ioctl_arg buffer;
	int ret;

	if (fw_device_is_shutdown(client->device))
		return -ENODEV;

	if (_IOC_TYPE(cmd) != '#' ||
	    _IOC_NR(cmd) >= ARRAY_SIZE(ioctl_handlers) ||
	    _IOC_SIZE(cmd) > sizeof(buffer))
		return -ENOTTY;

	memset(&buffer, 0, sizeof(buffer));

	if (_IOC_DIR(cmd) & _IOC_WRITE)
		if (copy_from_user(&buffer, arg, _IOC_SIZE(cmd)))
			return -EFAULT;

	ret = ioctl_handlers[_IOC_NR(cmd)](client, &buffer);
	if (ret < 0)
		return ret;

	if (_IOC_DIR(cmd) & _IOC_READ)
		if (copy_to_user(arg, &buffer, _IOC_SIZE(cmd)))
			return -EFAULT;

	return ret;
}

static long fw_device_op_ioctl(struct file *file,
			       unsigned int cmd, unsigned long arg)
{
	return dispatch_ioctl(file->private_data, cmd, (void __user *)arg);
}

static int fw_device_op_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct client *client = file->private_data;
	unsigned long size;
	int page_count, ret;

	if (fw_device_is_shutdown(client->device))
		return -ENODEV;

	/* FIXME: We could support multiple buffers, but we don't. */
	if (client->buffer.pages != NULL)
		return -EBUSY;

	if (!(vma->vm_flags & VM_SHARED))
		return -EINVAL;

	if (vma->vm_start & ~PAGE_MASK)
		return -EINVAL;

	client->vm_start = vma->vm_start;
	size = vma->vm_end - vma->vm_start;
	page_count = size >> PAGE_SHIFT;
	if (size & ~PAGE_MASK)
		return -EINVAL;

	ret = fw_iso_buffer_alloc(&client->buffer, page_count);
	if (ret < 0)
		return ret;

	scoped_guard(spinlock_irq, &client->lock) {
		if (client->iso_context) {
			ret = fw_iso_buffer_map_dma(&client->buffer, client->device->card,
						    iso_dma_direction(client->iso_context));
			if (ret < 0)
				goto fail;
			client->buffer_is_mapped = true;
		}
	}

	ret = vm_map_pages_zero(vma, client->buffer.pages,
				client->buffer.page_count);
	if (ret < 0)
		goto fail;

	return 0;
 fail:
	fw_iso_buffer_destroy(&client->buffer, client->device->card);
	return ret;
}

static bool has_outbound_transactions(struct client *client)
{
	struct client_resource *resource;
	unsigned long index;

	guard(spinlock_irq)(&client->lock);

	xa_for_each(&client->resource_xa, index, resource) {
		if (is_outbound_transaction_resource(resource))
			return true;
	}

	return false;
}

static int fw_device_op_release(struct inode *inode, struct file *file)
{
	struct client *client = file->private_data;
	struct event *event, *next_event;
	struct client_resource *resource;
	unsigned long index;

	// NOTE: This can be without irq when we can guarantee that __fw_send_request() for local
	// destination never runs in any type of IRQ context.
	scoped_guard(spinlock_irq, &phy_receiver_list_lock)
		list_del(&client->phy_receiver_link);

	scoped_guard(mutex, &client->device->client_list_mutex)
		list_del(&client->link);

	if (client->iso_context)
		fw_iso_context_destroy(client->iso_context);

	if (client->buffer.pages)
		fw_iso_buffer_destroy(&client->buffer, client->device->card);

	// Freeze client->resource_xa and client->event_list.
	scoped_guard(spinlock_irq, &client->lock)
		client->in_shutdown = true;

	wait_event(client->tx_flush_wait, !has_outbound_transactions(client));

	xa_for_each(&client->resource_xa, index, resource) {
		resource->release(client, resource);
		client_put(client);
	}
	xa_destroy(&client->resource_xa);

	list_for_each_entry_safe(event, next_event, &client->event_list, link)
		kfree(event);

	client_put(client);

	return 0;
}

static __poll_t fw_device_op_poll(struct file *file, poll_table * pt)
{
	struct client *client = file->private_data;
	__poll_t mask = 0;

	poll_wait(file, &client->wait, pt);

	if (fw_device_is_shutdown(client->device))
		mask |= EPOLLHUP | EPOLLERR;
	if (!list_empty(&client->event_list))
		mask |= EPOLLIN | EPOLLRDNORM;

	return mask;
}

const struct file_operations fw_device_ops = {
	.owner		= THIS_MODULE,
	.open		= fw_device_op_open,
	.read		= fw_device_op_read,
	.unlocked_ioctl	= fw_device_op_ioctl,
	.mmap		= fw_device_op_mmap,
	.release	= fw_device_op_release,
	.poll		= fw_device_op_poll,
	.compat_ioctl	= compat_ptr_ioctl,
};
