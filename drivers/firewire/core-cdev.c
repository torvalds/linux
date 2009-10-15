/*
 * Char device for device raw access
 *
 * Copyright (C) 2005-2007  Kristian Hoegsberg <krh@bitplanet.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/compat.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/firewire.h>
#include <linux/firewire-cdev.h>
#include <linux/idr.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/preempt.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <asm/system.h>

#include "core.h"

struct client {
	u32 version;
	struct fw_device *device;

	spinlock_t lock;
	bool in_shutdown;
	struct idr resource_idr;
	struct list_head event_list;
	wait_queue_head_t wait;
	u64 bus_reset_closure;

	struct fw_iso_context *iso_context;
	u64 iso_closure;
	struct fw_iso_buffer buffer;
	unsigned long vm_start;

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
	struct fw_request *request;
	void *data;
	size_t length;
};

struct descriptor_resource {
	struct client_resource resource;
	struct fw_descriptor descriptor;
	u32 data[0];
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
	__be32 transaction_data[2];
	struct iso_resource_event *e_alloc, *e_dealloc;
};

static void release_iso_resource(struct client *, struct client_resource *);

static void schedule_iso_resource(struct iso_resource *r, unsigned long delay)
{
	client_get(r->client);
	if (!schedule_delayed_work(&r->work, delay))
		client_put(r->client);
}

static void schedule_if_iso_resource(struct client_resource *resource)
{
	if (resource->release == release_iso_resource)
		schedule_iso_resource(container_of(resource,
					struct iso_resource, resource), 0);
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
	struct fw_cdev_event_response response;
};

struct inbound_transaction_event {
	struct event event;
	struct fw_cdev_event_request request;
};

struct iso_interrupt_event {
	struct event event;
	struct fw_cdev_event_iso_interrupt interrupt;
};

struct iso_resource_event {
	struct event event;
	struct fw_cdev_event_iso_resource iso_resource;
};

static inline void __user *u64_to_uptr(__u64 value)
{
	return (void __user *)(unsigned long)value;
}

static inline __u64 uptr_to_u64(void __user *ptr)
{
	return (__u64)(unsigned long)ptr;
}

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
	idr_init(&client->resource_idr);
	INIT_LIST_HEAD(&client->event_list);
	init_waitqueue_head(&client->wait);
	kref_init(&client->kref);

	file->private_data = client;

	mutex_lock(&device->client_list_mutex);
	list_add_tail(&client->link, &device->client_list);
	mutex_unlock(&device->client_list_mutex);

	return 0;
}

static void queue_event(struct client *client, struct event *event,
			void *data0, size_t size0, void *data1, size_t size1)
{
	unsigned long flags;

	event->v[0].data = data0;
	event->v[0].size = size0;
	event->v[1].data = data1;
	event->v[1].size = size1;

	spin_lock_irqsave(&client->lock, flags);
	if (client->in_shutdown)
		kfree(event);
	else
		list_add_tail(&event->link, &client->event_list);
	spin_unlock_irqrestore(&client->lock, flags);

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

	spin_lock_irq(&client->lock);
	event = list_first_entry(&client->event_list, struct event, link);
	list_del(&event->link);
	spin_unlock_irq(&client->lock);

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

	spin_lock_irq(&card->lock);

	event->closure	     = client->bus_reset_closure;
	event->type          = FW_CDEV_EVENT_BUS_RESET;
	event->generation    = client->device->generation;
	event->node_id       = client->device->node_id;
	event->local_node_id = card->local_node->node_id;
	event->bm_node_id    = 0; /* FIXME: We don't track the BM. */
	event->irm_node_id   = card->irm_node->node_id;
	event->root_node_id  = card->root_node->node_id;

	spin_unlock_irq(&card->lock);
}

static void for_each_client(struct fw_device *device,
			    void (*callback)(struct client *client))
{
	struct client *c;

	mutex_lock(&device->client_list_mutex);
	list_for_each_entry(c, &device->client_list, link)
		callback(c);
	mutex_unlock(&device->client_list_mutex);
}

static int schedule_reallocations(int id, void *p, void *data)
{
	schedule_if_iso_resource(p);

	return 0;
}

static void queue_bus_reset_event(struct client *client)
{
	struct bus_reset_event *e;

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (e == NULL) {
		fw_notify("Out of memory when allocating bus reset event\n");
		return;
	}

	fill_bus_reset_event(&e->reset, client);

	queue_event(client, &e->event,
		    &e->reset, sizeof(e->reset), NULL, 0);

	spin_lock_irq(&client->lock);
	idr_for_each(&client->resource_idr, schedule_reallocations, client);
	spin_unlock_irq(&client->lock);
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

static int ioctl_get_info(struct client *client, void *buffer)
{
	struct fw_cdev_get_info *get_info = buffer;
	struct fw_cdev_event_bus_reset bus_reset;
	unsigned long ret = 0;

	client->version = get_info->version;
	get_info->version = FW_CDEV_VERSION;
	get_info->card = client->device->card->index;

	down_read(&fw_device_rwsem);

	if (get_info->rom != 0) {
		void __user *uptr = u64_to_uptr(get_info->rom);
		size_t want = get_info->rom_length;
		size_t have = client->device->config_rom_length * 4;

		ret = copy_to_user(uptr, client->device->config_rom,
				   min(want, have));
	}
	get_info->rom_length = client->device->config_rom_length * 4;

	up_read(&fw_device_rwsem);

	if (ret != 0)
		return -EFAULT;

	client->bus_reset_closure = get_info->bus_reset_closure;
	if (get_info->bus_reset != 0) {
		void __user *uptr = u64_to_uptr(get_info->bus_reset);

		fill_bus_reset_event(&bus_reset, client);
		if (copy_to_user(uptr, &bus_reset, sizeof(bus_reset)))
			return -EFAULT;
	}

	return 0;
}

static int add_client_resource(struct client *client,
			       struct client_resource *resource, gfp_t gfp_mask)
{
	unsigned long flags;
	int ret;

 retry:
	if (idr_pre_get(&client->resource_idr, gfp_mask) == 0)
		return -ENOMEM;

	spin_lock_irqsave(&client->lock, flags);
	if (client->in_shutdown)
		ret = -ECANCELED;
	else
		ret = idr_get_new(&client->resource_idr, resource,
				  &resource->handle);
	if (ret >= 0) {
		client_get(client);
		schedule_if_iso_resource(resource);
	}
	spin_unlock_irqrestore(&client->lock, flags);

	if (ret == -EAGAIN)
		goto retry;

	return ret < 0 ? ret : 0;
}

static int release_client_resource(struct client *client, u32 handle,
				   client_resource_release_fn_t release,
				   struct client_resource **return_resource)
{
	struct client_resource *resource;

	spin_lock_irq(&client->lock);
	if (client->in_shutdown)
		resource = NULL;
	else
		resource = idr_find(&client->resource_idr, handle);
	if (resource && resource->release == release)
		idr_remove(&client->resource_idr, handle);
	spin_unlock_irq(&client->lock);

	if (!(resource && resource->release == release))
		return -EINVAL;

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
	struct outbound_transaction_resource *r = container_of(resource,
			struct outbound_transaction_resource, resource);

	fw_cancel_transaction(client->device->card, &r->transaction);
}

static void complete_transaction(struct fw_card *card, int rcode,
				 void *payload, size_t length, void *data)
{
	struct outbound_transaction_event *e = data;
	struct fw_cdev_event_response *rsp = &e->response;
	struct client *client = e->client;
	unsigned long flags;

	if (length < rsp->length)
		rsp->length = length;
	if (rcode == RCODE_COMPLETE)
		memcpy(rsp->data, payload, rsp->length);

	spin_lock_irqsave(&client->lock, flags);
	/*
	 * 1. If called while in shutdown, the idr tree must be left untouched.
	 *    The idr handle will be removed and the client reference will be
	 *    dropped later.
	 * 2. If the call chain was release_client_resource ->
	 *    release_transaction -> complete_transaction (instead of a normal
	 *    conclusion of the transaction), i.e. if this resource was already
	 *    unregistered from the idr, the client reference will be dropped
	 *    by release_client_resource and we must not drop it here.
	 */
	if (!client->in_shutdown &&
	    idr_find(&client->resource_idr, e->r.resource.handle)) {
		idr_remove(&client->resource_idr, e->r.resource.handle);
		/* Drop the idr's reference */
		client_put(client);
	}
	spin_unlock_irqrestore(&client->lock, flags);

	rsp->type = FW_CDEV_EVENT_RESPONSE;
	rsp->rcode = rcode;

	/*
	 * In the case that sizeof(*rsp) doesn't align with the position of the
	 * data, and the read is short, preserve an extra copy of the data
	 * to stay compatible with a pre-2.6.27 bug.  Since the bug is harmless
	 * for short reads and some apps depended on it, this is both safe
	 * and prudent for compatibility.
	 */
	if (rsp->length <= sizeof(*rsp) - offsetof(typeof(*rsp), data))
		queue_event(client, &e->event, rsp, sizeof(*rsp),
			    rsp->data, rsp->length);
	else
		queue_event(client, &e->event, rsp, sizeof(*rsp) + rsp->length,
			    NULL, 0);

	/* Drop the transaction callback's reference */
	client_put(client);
}

static int init_request(struct client *client,
			struct fw_cdev_send_request *request,
			int destination_id, int speed)
{
	struct outbound_transaction_event *e;
	int ret;

	if (request->tcode != TCODE_STREAM_DATA &&
	    (request->length > 4096 || request->length > 512 << speed))
		return -EIO;

	e = kmalloc(sizeof(*e) + request->length, GFP_KERNEL);
	if (e == NULL)
		return -ENOMEM;

	e->client = client;
	e->response.length = request->length;
	e->response.closure = request->closure;

	if (request->data &&
	    copy_from_user(e->response.data,
			   u64_to_uptr(request->data), request->length)) {
		ret = -EFAULT;
		goto failed;
	}

	e->r.resource.release = release_transaction;
	ret = add_client_resource(client, &e->r.resource, GFP_KERNEL);
	if (ret < 0)
		goto failed;

	/* Get a reference for the transaction callback */
	client_get(client);

	fw_send_request(client->device->card, &e->r.transaction,
			request->tcode, destination_id, request->generation,
			speed, request->offset, e->response.data,
			request->length, complete_transaction, e);
	return 0;

 failed:
	kfree(e);

	return ret;
}

static int ioctl_send_request(struct client *client, void *buffer)
{
	struct fw_cdev_send_request *request = buffer;

	switch (request->tcode) {
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

	return init_request(client, request, client->device->node_id,
			    client->device->max_speed);
}

static void release_request(struct client *client,
			    struct client_resource *resource)
{
	struct inbound_transaction_resource *r = container_of(resource,
			struct inbound_transaction_resource, resource);

	fw_send_response(client->device->card, r->request,
			 RCODE_CONFLICT_ERROR);
	kfree(r);
}

static void handle_request(struct fw_card *card, struct fw_request *request,
			   int tcode, int destination, int source,
			   int generation, int speed,
			   unsigned long long offset,
			   void *payload, size_t length, void *callback_data)
{
	struct address_handler_resource *handler = callback_data;
	struct inbound_transaction_resource *r;
	struct inbound_transaction_event *e;
	int ret;

	r = kmalloc(sizeof(*r), GFP_ATOMIC);
	e = kmalloc(sizeof(*e), GFP_ATOMIC);
	if (r == NULL || e == NULL)
		goto failed;

	r->request = request;
	r->data    = payload;
	r->length  = length;

	r->resource.release = release_request;
	ret = add_client_resource(handler->client, &r->resource, GFP_ATOMIC);
	if (ret < 0)
		goto failed;

	e->request.type    = FW_CDEV_EVENT_REQUEST;
	e->request.tcode   = tcode;
	e->request.offset  = offset;
	e->request.length  = length;
	e->request.handle  = r->resource.handle;
	e->request.closure = handler->closure;

	queue_event(handler->client, &e->event,
		    &e->request, sizeof(e->request), payload, length);
	return;

 failed:
	kfree(r);
	kfree(e);
	fw_send_response(card, request, RCODE_CONFLICT_ERROR);
}

static void release_address_handler(struct client *client,
				    struct client_resource *resource)
{
	struct address_handler_resource *r =
	    container_of(resource, struct address_handler_resource, resource);

	fw_core_remove_address_handler(&r->handler);
	kfree(r);
}

static int ioctl_allocate(struct client *client, void *buffer)
{
	struct fw_cdev_allocate *request = buffer;
	struct address_handler_resource *r;
	struct fw_address_region region;
	int ret;

	r = kmalloc(sizeof(*r), GFP_KERNEL);
	if (r == NULL)
		return -ENOMEM;

	region.start = request->offset;
	region.end = request->offset + request->length;
	r->handler.length = request->length;
	r->handler.address_callback = handle_request;
	r->handler.callback_data = r;
	r->closure = request->closure;
	r->client = client;

	ret = fw_core_add_address_handler(&r->handler, &region);
	if (ret < 0) {
		kfree(r);
		return ret;
	}

	r->resource.release = release_address_handler;
	ret = add_client_resource(client, &r->resource, GFP_KERNEL);
	if (ret < 0) {
		release_address_handler(client, &r->resource);
		return ret;
	}
	request->handle = r->resource.handle;

	return 0;
}

static int ioctl_deallocate(struct client *client, void *buffer)
{
	struct fw_cdev_deallocate *request = buffer;

	return release_client_resource(client, request->handle,
				       release_address_handler, NULL);
}

static int ioctl_send_response(struct client *client, void *buffer)
{
	struct fw_cdev_send_response *request = buffer;
	struct client_resource *resource;
	struct inbound_transaction_resource *r;
	int ret = 0;

	if (release_client_resource(client, request->handle,
				    release_request, &resource) < 0)
		return -EINVAL;

	r = container_of(resource, struct inbound_transaction_resource,
			 resource);
	if (request->length < r->length)
		r->length = request->length;

	if (copy_from_user(r->data, u64_to_uptr(request->data), r->length)) {
		ret = -EFAULT;
		goto out;
	}

	fw_send_response(client->device->card, r->request, request->rcode);
 out:
	kfree(r);

	return ret;
}

static int ioctl_initiate_bus_reset(struct client *client, void *buffer)
{
	struct fw_cdev_initiate_bus_reset *request = buffer;
	int short_reset;

	short_reset = (request->type == FW_CDEV_SHORT_RESET);

	return fw_core_initiate_bus_reset(client->device->card, short_reset);
}

static void release_descriptor(struct client *client,
			       struct client_resource *resource)
{
	struct descriptor_resource *r =
		container_of(resource, struct descriptor_resource, resource);

	fw_core_remove_descriptor(&r->descriptor);
	kfree(r);
}

static int ioctl_add_descriptor(struct client *client, void *buffer)
{
	struct fw_cdev_add_descriptor *request = buffer;
	struct descriptor_resource *r;
	int ret;

	/* Access policy: Allow this ioctl only on local nodes' device files. */
	if (!client->device->is_local)
		return -ENOSYS;

	if (request->length > 256)
		return -EINVAL;

	r = kmalloc(sizeof(*r) + request->length * 4, GFP_KERNEL);
	if (r == NULL)
		return -ENOMEM;

	if (copy_from_user(r->data,
			   u64_to_uptr(request->data), request->length * 4)) {
		ret = -EFAULT;
		goto failed;
	}

	r->descriptor.length    = request->length;
	r->descriptor.immediate = request->immediate;
	r->descriptor.key       = request->key;
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
	request->handle = r->resource.handle;

	return 0;
 failed:
	kfree(r);

	return ret;
}

static int ioctl_remove_descriptor(struct client *client, void *buffer)
{
	struct fw_cdev_remove_descriptor *request = buffer;

	return release_client_resource(client, request->handle,
				       release_descriptor, NULL);
}

static void iso_callback(struct fw_iso_context *context, u32 cycle,
			 size_t header_length, void *header, void *data)
{
	struct client *client = data;
	struct iso_interrupt_event *e;

	e = kzalloc(sizeof(*e) + header_length, GFP_ATOMIC);
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

static int ioctl_create_iso_context(struct client *client, void *buffer)
{
	struct fw_cdev_create_iso_context *request = buffer;
	struct fw_iso_context *context;

	/* We only support one context at this time. */
	if (client->iso_context != NULL)
		return -EBUSY;

	if (request->channel > 63)
		return -EINVAL;

	switch (request->type) {
	case FW_ISO_CONTEXT_RECEIVE:
		if (request->header_size < 4 || (request->header_size & 3))
			return -EINVAL;

		break;

	case FW_ISO_CONTEXT_TRANSMIT:
		if (request->speed > SCODE_3200)
			return -EINVAL;

		break;

	default:
		return -EINVAL;
	}

	context =  fw_iso_context_create(client->device->card,
					 request->type,
					 request->channel,
					 request->speed,
					 request->header_size,
					 iso_callback, client);
	if (IS_ERR(context))
		return PTR_ERR(context);

	client->iso_closure = request->closure;
	client->iso_context = context;

	/* We only support one context at this time. */
	request->handle = 0;

	return 0;
}

/* Macros for decoding the iso packet control header. */
#define GET_PAYLOAD_LENGTH(v)	((v) & 0xffff)
#define GET_INTERRUPT(v)	(((v) >> 16) & 0x01)
#define GET_SKIP(v)		(((v) >> 17) & 0x01)
#define GET_TAG(v)		(((v) >> 18) & 0x03)
#define GET_SY(v)		(((v) >> 20) & 0x0f)
#define GET_HEADER_LENGTH(v)	(((v) >> 24) & 0xff)

static int ioctl_queue_iso(struct client *client, void *buffer)
{
	struct fw_cdev_queue_iso *request = buffer;
	struct fw_cdev_iso_packet __user *p, *end, *next;
	struct fw_iso_context *ctx = client->iso_context;
	unsigned long payload, buffer_end, header_length;
	u32 control;
	int count;
	struct {
		struct fw_iso_packet packet;
		u8 header[256];
	} u;

	if (ctx == NULL || request->handle != 0)
		return -EINVAL;

	/*
	 * If the user passes a non-NULL data pointer, has mmap()'ed
	 * the iso buffer, and the pointer points inside the buffer,
	 * we setup the payload pointers accordingly.  Otherwise we
	 * set them both to 0, which will still let packets with
	 * payload_length == 0 through.  In other words, if no packets
	 * use the indirect payload, the iso buffer need not be mapped
	 * and the request->data pointer is ignored.
	 */

	payload = (unsigned long)request->data - client->vm_start;
	buffer_end = client->buffer.page_count << PAGE_SHIFT;
	if (request->data == 0 || client->buffer.pages == NULL ||
	    payload >= buffer_end) {
		payload = 0;
		buffer_end = 0;
	}

	p = (struct fw_cdev_iso_packet __user *)u64_to_uptr(request->packets);

	if (!access_ok(VERIFY_READ, p, request->size))
		return -EFAULT;

	end = (void __user *)p + request->size;
	count = 0;
	while (p < end) {
		if (get_user(control, &p->control))
			return -EFAULT;
		u.packet.payload_length = GET_PAYLOAD_LENGTH(control);
		u.packet.interrupt = GET_INTERRUPT(control);
		u.packet.skip = GET_SKIP(control);
		u.packet.tag = GET_TAG(control);
		u.packet.sy = GET_SY(control);
		u.packet.header_length = GET_HEADER_LENGTH(control);

		if (ctx->type == FW_ISO_CONTEXT_TRANSMIT) {
			header_length = u.packet.header_length;
		} else {
			/*
			 * We require that header_length is a multiple of
			 * the fixed header size, ctx->header_size.
			 */
			if (ctx->header_size == 0) {
				if (u.packet.header_length > 0)
					return -EINVAL;
			} else if (u.packet.header_length % ctx->header_size != 0) {
				return -EINVAL;
			}
			header_length = 0;
		}

		next = (struct fw_cdev_iso_packet __user *)
			&p->header[header_length / 4];
		if (next > end)
			return -EINVAL;
		if (__copy_from_user
		    (u.packet.header, p->header, header_length))
			return -EFAULT;
		if (u.packet.skip && ctx->type == FW_ISO_CONTEXT_TRANSMIT &&
		    u.packet.header_length + u.packet.payload_length > 0)
			return -EINVAL;
		if (payload + u.packet.payload_length > buffer_end)
			return -EINVAL;

		if (fw_iso_context_queue(ctx, &u.packet,
					 &client->buffer, payload))
			break;

		p = next;
		payload += u.packet.payload_length;
		count++;
	}

	request->size    -= uptr_to_u64(p) - request->packets;
	request->packets  = uptr_to_u64(p);
	request->data     = client->vm_start + payload;

	return count;
}

static int ioctl_start_iso(struct client *client, void *buffer)
{
	struct fw_cdev_start_iso *request = buffer;

	if (client->iso_context == NULL || request->handle != 0)
		return -EINVAL;

	if (client->iso_context->type == FW_ISO_CONTEXT_RECEIVE) {
		if (request->tags == 0 || request->tags > 15)
			return -EINVAL;

		if (request->sync > 15)
			return -EINVAL;
	}

	return fw_iso_context_start(client->iso_context, request->cycle,
				    request->sync, request->tags);
}

static int ioctl_stop_iso(struct client *client, void *buffer)
{
	struct fw_cdev_stop_iso *request = buffer;

	if (client->iso_context == NULL || request->handle != 0)
		return -EINVAL;

	return fw_iso_context_stop(client->iso_context);
}

static int ioctl_get_cycle_timer(struct client *client, void *buffer)
{
	struct fw_cdev_get_cycle_timer *request = buffer;
	struct fw_card *card = client->device->card;
	unsigned long long bus_time;
	struct timeval tv;
	unsigned long flags;

	preempt_disable();
	local_irq_save(flags);

	bus_time = card->driver->get_bus_time(card);
	do_gettimeofday(&tv);

	local_irq_restore(flags);
	preempt_enable();

	request->local_time = tv.tv_sec * 1000000ULL + tv.tv_usec;
	request->cycle_timer = bus_time & 0xffffffff;
	return 0;
}

static void iso_resource_work(struct work_struct *work)
{
	struct iso_resource_event *e;
	struct iso_resource *r =
			container_of(work, struct iso_resource, work.work);
	struct client *client = r->client;
	int generation, channel, bandwidth, todo;
	bool skip, free, success;

	spin_lock_irq(&client->lock);
	generation = client->device->generation;
	todo = r->todo;
	/* Allow 1000ms grace period for other reallocations. */
	if (todo == ISO_RES_ALLOC &&
	    time_is_after_jiffies(client->device->card->reset_jiffies + HZ)) {
		schedule_iso_resource(r, DIV_ROUND_UP(HZ, 3));
		skip = true;
	} else {
		/* We could be called twice within the same generation. */
		skip = todo == ISO_RES_REALLOC &&
		       r->generation == generation;
	}
	free = todo == ISO_RES_DEALLOC ||
	       todo == ISO_RES_ALLOC_ONCE ||
	       todo == ISO_RES_DEALLOC_ONCE;
	r->generation = generation;
	spin_unlock_irq(&client->lock);

	if (skip)
		goto out;

	bandwidth = r->bandwidth;

	fw_iso_resource_manage(client->device->card, generation,
			r->channels, &channel, &bandwidth,
			todo == ISO_RES_ALLOC ||
			todo == ISO_RES_REALLOC ||
			todo == ISO_RES_ALLOC_ONCE,
			r->transaction_data);
	/*
	 * Is this generation outdated already?  As long as this resource sticks
	 * in the idr, it will be scheduled again for a newer generation or at
	 * shutdown.
	 */
	if (channel == -EAGAIN &&
	    (todo == ISO_RES_ALLOC || todo == ISO_RES_REALLOC))
		goto out;

	success = channel >= 0 || bandwidth > 0;

	spin_lock_irq(&client->lock);
	/*
	 * Transit from allocation to reallocation, except if the client
	 * requested deallocation in the meantime.
	 */
	if (r->todo == ISO_RES_ALLOC)
		r->todo = ISO_RES_REALLOC;
	/*
	 * Allocation or reallocation failure?  Pull this resource out of the
	 * idr and prepare for deletion, unless the client is shutting down.
	 */
	if (r->todo == ISO_RES_REALLOC && !success &&
	    !client->in_shutdown &&
	    idr_find(&client->resource_idr, r->resource.handle)) {
		idr_remove(&client->resource_idr, r->resource.handle);
		client_put(client);
		free = true;
	}
	spin_unlock_irq(&client->lock);

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
	struct iso_resource *r =
		container_of(resource, struct iso_resource, resource);

	spin_lock_irq(&client->lock);
	r->todo = ISO_RES_DEALLOC;
	schedule_iso_resource(r, 0);
	spin_unlock_irq(&client->lock);
}

static int init_iso_resource(struct client *client,
		struct fw_cdev_allocate_iso_resource *request, int todo)
{
	struct iso_resource_event *e1, *e2;
	struct iso_resource *r;
	int ret;

	if ((request->channels == 0 && request->bandwidth == 0) ||
	    request->bandwidth > BANDWIDTH_AVAILABLE_INITIAL ||
	    request->bandwidth < 0)
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

static int ioctl_allocate_iso_resource(struct client *client, void *buffer)
{
	struct fw_cdev_allocate_iso_resource *request = buffer;

	return init_iso_resource(client, request, ISO_RES_ALLOC);
}

static int ioctl_deallocate_iso_resource(struct client *client, void *buffer)
{
	struct fw_cdev_deallocate *request = buffer;

	return release_client_resource(client, request->handle,
				       release_iso_resource, NULL);
}

static int ioctl_allocate_iso_resource_once(struct client *client, void *buffer)
{
	struct fw_cdev_allocate_iso_resource *request = buffer;

	return init_iso_resource(client, request, ISO_RES_ALLOC_ONCE);
}

static int ioctl_deallocate_iso_resource_once(struct client *client, void *buffer)
{
	struct fw_cdev_allocate_iso_resource *request = buffer;

	return init_iso_resource(client, request, ISO_RES_DEALLOC_ONCE);
}

/*
 * Returns a speed code:  Maximum speed to or from this device,
 * limited by the device's link speed, the local node's link speed,
 * and all PHY port speeds between the two links.
 */
static int ioctl_get_speed(struct client *client, void *buffer)
{
	return client->device->max_speed;
}

static int ioctl_send_broadcast_request(struct client *client, void *buffer)
{
	struct fw_cdev_send_request *request = buffer;

	switch (request->tcode) {
	case TCODE_WRITE_QUADLET_REQUEST:
	case TCODE_WRITE_BLOCK_REQUEST:
		break;
	default:
		return -EINVAL;
	}

	/* Security policy: Only allow accesses to Units Space. */
	if (request->offset < CSR_REGISTER_BASE + CSR_CONFIG_ROM_END)
		return -EACCES;

	return init_request(client, request, LOCAL_BUS | 0x3f, SCODE_100);
}

static int ioctl_send_stream_packet(struct client *client, void *buffer)
{
	struct fw_cdev_send_stream_packet *p = buffer;
	struct fw_cdev_send_request request;
	int dest;

	if (p->speed > client->device->card->link_speed ||
	    p->length > 1024 << p->speed)
		return -EIO;

	if (p->tag > 3 || p->channel > 63 || p->sy > 15)
		return -EINVAL;

	dest = fw_stream_packet_destination_id(p->tag, p->channel, p->sy);
	request.tcode		= TCODE_STREAM_DATA;
	request.length		= p->length;
	request.closure		= p->closure;
	request.data		= p->data;
	request.generation	= p->generation;

	return init_request(client, &request, dest, p->speed);
}

static int (* const ioctl_handlers[])(struct client *client, void *buffer) = {
	ioctl_get_info,
	ioctl_send_request,
	ioctl_allocate,
	ioctl_deallocate,
	ioctl_send_response,
	ioctl_initiate_bus_reset,
	ioctl_add_descriptor,
	ioctl_remove_descriptor,
	ioctl_create_iso_context,
	ioctl_queue_iso,
	ioctl_start_iso,
	ioctl_stop_iso,
	ioctl_get_cycle_timer,
	ioctl_allocate_iso_resource,
	ioctl_deallocate_iso_resource,
	ioctl_allocate_iso_resource_once,
	ioctl_deallocate_iso_resource_once,
	ioctl_get_speed,
	ioctl_send_broadcast_request,
	ioctl_send_stream_packet,
};

static int dispatch_ioctl(struct client *client,
			  unsigned int cmd, void __user *arg)
{
	char buffer[sizeof(union {
		struct fw_cdev_get_info			_00;
		struct fw_cdev_send_request		_01;
		struct fw_cdev_allocate			_02;
		struct fw_cdev_deallocate		_03;
		struct fw_cdev_send_response		_04;
		struct fw_cdev_initiate_bus_reset	_05;
		struct fw_cdev_add_descriptor		_06;
		struct fw_cdev_remove_descriptor	_07;
		struct fw_cdev_create_iso_context	_08;
		struct fw_cdev_queue_iso		_09;
		struct fw_cdev_start_iso		_0a;
		struct fw_cdev_stop_iso			_0b;
		struct fw_cdev_get_cycle_timer		_0c;
		struct fw_cdev_allocate_iso_resource	_0d;
		struct fw_cdev_send_stream_packet	_13;
	})];
	int ret;

	if (_IOC_TYPE(cmd) != '#' ||
	    _IOC_NR(cmd) >= ARRAY_SIZE(ioctl_handlers))
		return -EINVAL;

	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		if (_IOC_SIZE(cmd) > sizeof(buffer) ||
		    copy_from_user(buffer, arg, _IOC_SIZE(cmd)))
			return -EFAULT;
	}

	ret = ioctl_handlers[_IOC_NR(cmd)](client, buffer);
	if (ret < 0)
		return ret;

	if (_IOC_DIR(cmd) & _IOC_READ) {
		if (_IOC_SIZE(cmd) > sizeof(buffer) ||
		    copy_to_user(arg, buffer, _IOC_SIZE(cmd)))
			return -EFAULT;
	}

	return ret;
}

static long fw_device_op_ioctl(struct file *file,
			       unsigned int cmd, unsigned long arg)
{
	struct client *client = file->private_data;

	if (fw_device_is_shutdown(client->device))
		return -ENODEV;

	return dispatch_ioctl(client, cmd, (void __user *) arg);
}

#ifdef CONFIG_COMPAT
static long fw_device_op_compat_ioctl(struct file *file,
				      unsigned int cmd, unsigned long arg)
{
	struct client *client = file->private_data;

	if (fw_device_is_shutdown(client->device))
		return -ENODEV;

	return dispatch_ioctl(client, cmd, compat_ptr(arg));
}
#endif

static int fw_device_op_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct client *client = file->private_data;
	enum dma_data_direction direction;
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

	if (vma->vm_flags & VM_WRITE)
		direction = DMA_TO_DEVICE;
	else
		direction = DMA_FROM_DEVICE;

	ret = fw_iso_buffer_init(&client->buffer, client->device->card,
				 page_count, direction);
	if (ret < 0)
		return ret;

	ret = fw_iso_buffer_map(&client->buffer, vma);
	if (ret < 0)
		fw_iso_buffer_destroy(&client->buffer, client->device->card);

	return ret;
}

static int shutdown_resource(int id, void *p, void *data)
{
	struct client_resource *resource = p;
	struct client *client = data;

	resource->release(client, resource);
	client_put(client);

	return 0;
}

static int fw_device_op_release(struct inode *inode, struct file *file)
{
	struct client *client = file->private_data;
	struct event *event, *next_event;

	mutex_lock(&client->device->client_list_mutex);
	list_del(&client->link);
	mutex_unlock(&client->device->client_list_mutex);

	if (client->iso_context)
		fw_iso_context_destroy(client->iso_context);

	if (client->buffer.pages)
		fw_iso_buffer_destroy(&client->buffer, client->device->card);

	/* Freeze client->resource_idr and client->event_list */
	spin_lock_irq(&client->lock);
	client->in_shutdown = true;
	spin_unlock_irq(&client->lock);

	idr_for_each(&client->resource_idr, shutdown_resource, client);
	idr_remove_all(&client->resource_idr);
	idr_destroy(&client->resource_idr);

	list_for_each_entry_safe(event, next_event, &client->event_list, link)
		kfree(event);

	client_put(client);

	return 0;
}

static unsigned int fw_device_op_poll(struct file *file, poll_table * pt)
{
	struct client *client = file->private_data;
	unsigned int mask = 0;

	poll_wait(file, &client->wait, pt);

	if (fw_device_is_shutdown(client->device))
		mask |= POLLHUP | POLLERR;
	if (!list_empty(&client->event_list))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

const struct file_operations fw_device_ops = {
	.owner		= THIS_MODULE,
	.open		= fw_device_op_open,
	.read		= fw_device_op_read,
	.unlocked_ioctl	= fw_device_op_ioctl,
	.poll		= fw_device_op_poll,
	.release	= fw_device_op_release,
	.mmap		= fw_device_op_mmap,

#ifdef CONFIG_COMPAT
	.compat_ioctl	= fw_device_op_compat_ioctl,
#endif
};
