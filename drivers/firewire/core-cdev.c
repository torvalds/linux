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

#include <linux/bug.h>
#include <linux/compat.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/firewire.h>
#include <linux/firewire-cdev.h>
#include <linux/idr.h>
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

/*
 * ABI version history is documented in linux/firewire-cdev.h.
 */
#define FW_CDEV_KERNEL_VERSION			5
#define FW_CDEV_VERSION_EVENT_REQUEST2		4
#define FW_CDEV_VERSION_ALLOCATE_REGION_END	4
#define FW_CDEV_VERSION_AUTO_FLUSH_ISO_OVERFLOW	5

struct client {
	u32 version;
	struct fw_device *device;

	spinlock_t lock;
	bool in_shutdown;
	struct idr resource_idr;
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
	struct iso_resource_event *e_alloc, *e_dealloc;
};

static void release_iso_resource(struct client *, struct client_resource *);

static void schedule_iso_resource(struct iso_resource *r, unsigned long delay)
{
	client_get(r->client);
	if (!queue_delayed_work(fw_workqueue, &r->work, delay))
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
	union {
		struct fw_cdev_event_request request;
		struct fw_cdev_event_request2 request2;
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
	struct fw_cdev_event_phy_packet phy_packet;
};

struct inbound_phy_packet_event {
	struct event event;
	struct fw_cdev_event_phy_packet phy_packet;
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
	idr_init(&client->resource_idr);
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
	event->bm_node_id    = card->bm_node_id;
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
	if (e == NULL)
		return;

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

	down_read(&fw_device_rwsem);

	if (a->rom != 0) {
		size_t want = a->rom_length;
		size_t have = client->device->config_rom_length * 4;

		ret = copy_to_user(u64_to_uptr(a->rom),
				   client->device->config_rom, min(want, have));
	}
	a->rom_length = client->device->config_rom_length * 4;

	up_read(&fw_device_rwsem);

	if (ret != 0)
		return -EFAULT;

	mutex_lock(&client->device->client_list_mutex);

	client->bus_reset_closure = a->bus_reset_closure;
	if (a->bus_reset != 0) {
		fill_bus_reset_event(&bus_reset, client);
		/* unaligned size of bus_reset is 36 bytes */
		ret = copy_to_user(u64_to_uptr(a->bus_reset), &bus_reset, 36);
	}
	if (ret == 0 && list_empty(&client->link))
		list_add_tail(&client->link, &client->device->client_list);

	mutex_unlock(&client->device->client_list_mutex);

	return ret ? -EFAULT : 0;
}

static int add_client_resource(struct client *client,
			       struct client_resource *resource, gfp_t gfp_mask)
{
	bool preload = gfpflags_allow_blocking(gfp_mask);
	unsigned long flags;
	int ret;

	if (preload)
		idr_preload(gfp_mask);
	spin_lock_irqsave(&client->lock, flags);

	if (client->in_shutdown)
		ret = -ECANCELED;
	else
		ret = idr_alloc(&client->resource_idr, resource, 0, 0,
				GFP_NOWAIT);
	if (ret >= 0) {
		resource->handle = ret;
		client_get(client);
		schedule_if_iso_resource(resource);
	}

	spin_unlock_irqrestore(&client->lock, flags);
	if (preload)
		idr_preload_end();

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
	idr_remove(&client->resource_idr, e->r.resource.handle);
	if (client->in_shutdown)
		wake_up(&client->tx_flush_wait);
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

	/* Drop the idr's reference */
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

	if (request->tcode == TCODE_WRITE_QUADLET_REQUEST &&
	    request->length < 4)
		return -EINVAL;

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

	fw_send_request(client->device->card, &e->r.transaction,
			request->tcode, destination_id, request->generation,
			speed, request->offset, e->response.data,
			request->length, complete_transaction, e);
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

static inline bool is_fcp_request(struct fw_request *request)
{
	return request == NULL;
}

static void release_request(struct client *client,
			    struct client_resource *resource)
{
	struct inbound_transaction_resource *r = container_of(resource,
			struct inbound_transaction_resource, resource);

	if (is_fcp_request(r->request))
		kfree(r->data);
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
	struct inbound_transaction_resource *r;
	struct inbound_transaction_event *e;
	size_t event_size0;
	void *fcp_frame = NULL;
	int ret;

	/* card may be different from handler->client->device->card */
	fw_card_get(card);

	r = kmalloc(sizeof(*r), GFP_ATOMIC);
	e = kmalloc(sizeof(*e), GFP_ATOMIC);
	if (r == NULL || e == NULL)
		goto failed;

	r->card    = card;
	r->request = request;
	r->data    = payload;
	r->length  = length;

	if (is_fcp_request(request)) {
		/*
		 * FIXME: Let core-transaction.c manage a
		 * single reference-counted copy?
		 */
		fcp_frame = kmemdup(payload, length, GFP_ATOMIC);
		if (fcp_frame == NULL)
			goto failed;

		r->data = fcp_frame;
	}

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
	} else {
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
	}

	queue_event(handler->client, &e->event,
		    &e->req, event_size0, r->data, length);
	return;

 failed:
	kfree(r);
	kfree(e);
	kfree(fcp_frame);

	if (!is_fcp_request(request))
		fw_send_response(card, request, RCODE_CONFLICT_ERROR);

	fw_card_put(card);
}

static void release_address_handler(struct client *client,
				    struct client_resource *resource)
{
	struct address_handler_resource *r =
	    container_of(resource, struct address_handler_resource, resource);

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

	r = container_of(resource, struct inbound_transaction_resource,
			 resource);
	if (is_fcp_request(r->request))
		goto out;

	if (a->length != fw_get_response_length(r->request)) {
		ret = -EINVAL;
		kfree(r->request);
		goto out;
	}
	if (copy_from_user(r->data, u64_to_uptr(a->data), a->length)) {
		ret = -EFAULT;
		kfree(r->request);
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
	struct descriptor_resource *r =
		container_of(resource, struct descriptor_resource, resource);

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

	r = kmalloc(sizeof(*r) + a->length * 4, GFP_KERNEL);
	if (r == NULL)
		return -ENOMEM;

	if (copy_from_user(r->data, u64_to_uptr(a->data), a->length * 4)) {
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

	e = kmalloc(sizeof(*e) + header_length, GFP_ATOMIC);
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

	e = kmalloc(sizeof(*e), GFP_ATOMIC);
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

static int ioctl_create_iso_context(struct client *client, union ioctl_arg *arg)
{
	struct fw_cdev_create_iso_context *a = &arg->create_iso_context;
	struct fw_iso_context *context;
	fw_iso_callback_t cb;
	int ret;

	BUILD_BUG_ON(FW_CDEV_ISO_CONTEXT_TRANSMIT != FW_ISO_CONTEXT_TRANSMIT ||
		     FW_CDEV_ISO_CONTEXT_RECEIVE  != FW_ISO_CONTEXT_RECEIVE  ||
		     FW_CDEV_ISO_CONTEXT_RECEIVE_MULTICHANNEL !=
					FW_ISO_CONTEXT_RECEIVE_MULTICHANNEL);

	switch (a->type) {
	case FW_ISO_CONTEXT_TRANSMIT:
		if (a->speed > SCODE_3200 || a->channel > 63)
			return -EINVAL;

		cb = iso_callback;
		break;

	case FW_ISO_CONTEXT_RECEIVE:
		if (a->header_size < 4 || (a->header_size & 3) ||
		    a->channel > 63)
			return -EINVAL;

		cb = iso_callback;
		break;

	case FW_ISO_CONTEXT_RECEIVE_MULTICHANNEL:
		cb = (fw_iso_callback_t)iso_mc_callback;
		break;

	default:
		return -EINVAL;
	}

	context = fw_iso_context_create(client->device->card, a->type,
			a->channel, a->speed, a->header_size, cb, client);
	if (IS_ERR(context))
		return PTR_ERR(context);
	if (client->version < FW_CDEV_VERSION_AUTO_FLUSH_ISO_OVERFLOW)
		context->drop_overflow_headers = true;

	/* We only support one context at this time. */
	spin_lock_irq(&client->lock);
	if (client->iso_context != NULL) {
		spin_unlock_irq(&client->lock);
		fw_iso_context_destroy(context);

		return -EBUSY;
	}
	if (!client->buffer_is_mapped) {
		ret = fw_iso_buffer_map_dma(&client->buffer,
					    client->device->card,
					    iso_dma_direction(context));
		if (ret < 0) {
			spin_unlock_irq(&client->lock);
			fw_iso_context_destroy(context);

			return ret;
		}
		client->buffer_is_mapped = true;
	}
	client->iso_closure = a->closure;
	client->iso_context = context;
	spin_unlock_irq(&client->lock);

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
	struct {
		struct fw_iso_packet packet;
		u8 header[256];
	} u;

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
	if (!access_ok(VERIFY_READ, p, a->size))
		return -EFAULT;

	end = (void __user *)p + a->size;
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

		switch (ctx->type) {
		case FW_ISO_CONTEXT_TRANSMIT:
			if (u.packet.header_length & 3)
				return -EINVAL;
			transmit_header_bytes = u.packet.header_length;
			break;

		case FW_ISO_CONTEXT_RECEIVE:
			if (u.packet.header_length == 0 ||
			    u.packet.header_length % ctx->header_size != 0)
				return -EINVAL;
			break;

		case FW_ISO_CONTEXT_RECEIVE_MULTICHANNEL:
			if (u.packet.payload_length == 0 ||
			    u.packet.payload_length & 3)
				return -EINVAL;
			break;
		}

		next = (struct fw_cdev_iso_packet __user *)
			&p->header[transmit_header_bytes / 4];
		if (next > end)
			return -EINVAL;
		if (__copy_from_user
		    (u.packet.header, p->header, transmit_header_bytes))
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
	struct timespec ts = {0, 0};
	u32 cycle_time;
	int ret = 0;

	local_irq_disable();

	cycle_time = card->driver->read_csr(card, CSR_CYCLE_TIME);

	switch (a->clk_id) {
	case CLOCK_REALTIME:      getnstimeofday(&ts);	break;
	case CLOCK_MONOTONIC:     ktime_get_ts(&ts);	break;
	case CLOCK_MONOTONIC_RAW: getrawmonotonic(&ts);	break;
	default:
		ret = -EINVAL;
	}

	local_irq_enable();

	a->tv_sec      = ts.tv_sec;
	a->tv_nsec     = ts.tv_nsec;
	a->cycle_timer = cycle_time;

	return ret;
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
	    time_before64(get_jiffies_64(),
			  client->device->card->reset_jiffies + HZ)) {
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
			todo == ISO_RES_ALLOC_ONCE);
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
	    idr_remove(&client->resource_idr, r->resource.handle)) {
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

	switch (status) {
	/* expected: */
	case ACK_COMPLETE:	e->phy_packet.rcode = RCODE_COMPLETE;	break;
	/* should never happen with PHY packets: */
	case ACK_PENDING:	e->phy_packet.rcode = RCODE_COMPLETE;	break;
	case ACK_BUSY_X:
	case ACK_BUSY_A:
	case ACK_BUSY_B:	e->phy_packet.rcode = RCODE_BUSY;	break;
	case ACK_DATA_ERROR:	e->phy_packet.rcode = RCODE_DATA_ERROR;	break;
	case ACK_TYPE_ERROR:	e->phy_packet.rcode = RCODE_TYPE_ERROR;	break;
	/* stale generation; cancelled; on certain controllers: no ack */
	default:		e->phy_packet.rcode = status;		break;
	}
	e->phy_packet.data[0] = packet->timestamp;

	queue_event(e->client, &e->event, &e->phy_packet,
		    sizeof(e->phy_packet) + e->phy_packet.length, NULL, 0);
	client_put(e->client);
}

static int ioctl_send_phy_packet(struct client *client, union ioctl_arg *arg)
{
	struct fw_cdev_send_phy_packet *a = &arg->send_phy_packet;
	struct fw_card *card = client->device->card;
	struct outbound_phy_packet_event *e;

	/* Access policy: Allow this ioctl only on local nodes' device files. */
	if (!client->device->is_local)
		return -ENOSYS;

	e = kzalloc(sizeof(*e) + 4, GFP_KERNEL);
	if (e == NULL)
		return -ENOMEM;

	client_get(client);
	e->client		= client;
	e->p.speed		= SCODE_100;
	e->p.generation		= a->generation;
	e->p.header[0]		= TCODE_LINK_INTERNAL << 4;
	e->p.header[1]		= a->data[0];
	e->p.header[2]		= a->data[1];
	e->p.header_length	= 12;
	e->p.callback		= outbound_phy_packet_callback;
	e->phy_packet.closure	= a->closure;
	e->phy_packet.type	= FW_CDEV_EVENT_PHY_PACKET_SENT;
	if (is_ping_packet(a->data))
			e->phy_packet.length = 4;

	card->driver->send_request(card, &e->p);

	return 0;
}

static int ioctl_receive_phy_packets(struct client *client, union ioctl_arg *arg)
{
	struct fw_cdev_receive_phy_packets *a = &arg->receive_phy_packets;
	struct fw_card *card = client->device->card;

	/* Access policy: Allow this ioctl only on local nodes' device files. */
	if (!client->device->is_local)
		return -ENOSYS;

	spin_lock_irq(&card->lock);

	list_move_tail(&client->phy_receiver_link, &card->phy_receiver_list);
	client->phy_receiver_closure = a->closure;

	spin_unlock_irq(&card->lock);

	return 0;
}

void fw_cdev_handle_phy_packet(struct fw_card *card, struct fw_packet *p)
{
	struct client *client;
	struct inbound_phy_packet_event *e;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);

	list_for_each_entry(client, &card->phy_receiver_list, phy_receiver_link) {
		e = kmalloc(sizeof(*e) + 8, GFP_ATOMIC);
		if (e == NULL)
			break;

		e->phy_packet.closure	= client->phy_receiver_closure;
		e->phy_packet.type	= FW_CDEV_EVENT_PHY_PACKET_RECEIVED;
		e->phy_packet.rcode	= RCODE_COMPLETE;
		e->phy_packet.length	= 8;
		e->phy_packet.data[0]	= p->header[1];
		e->phy_packet.data[1]	= p->header[2];
		queue_event(client, &e->event,
			    &e->phy_packet, sizeof(e->phy_packet) + 8, NULL, 0);
	}

	spin_unlock_irqrestore(&card->lock, flags);
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

#ifdef CONFIG_COMPAT
static long fw_device_op_compat_ioctl(struct file *file,
				      unsigned int cmd, unsigned long arg)
{
	return dispatch_ioctl(file->private_data, cmd, compat_ptr(arg));
}
#endif

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

	spin_lock_irq(&client->lock);
	if (client->iso_context) {
		ret = fw_iso_buffer_map_dma(&client->buffer,
				client->device->card,
				iso_dma_direction(client->iso_context));
		client->buffer_is_mapped = (ret == 0);
	}
	spin_unlock_irq(&client->lock);
	if (ret < 0)
		goto fail;

	ret = fw_iso_buffer_map_vma(&client->buffer, vma);
	if (ret < 0)
		goto fail;

	return 0;
 fail:
	fw_iso_buffer_destroy(&client->buffer, client->device->card);
	return ret;
}

static int is_outbound_transaction_resource(int id, void *p, void *data)
{
	struct client_resource *resource = p;

	return resource->release == release_transaction;
}

static int has_outbound_transactions(struct client *client)
{
	int ret;

	spin_lock_irq(&client->lock);
	ret = idr_for_each(&client->resource_idr,
			   is_outbound_transaction_resource, NULL);
	spin_unlock_irq(&client->lock);

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

	spin_lock_irq(&client->device->card->lock);
	list_del(&client->phy_receiver_link);
	spin_unlock_irq(&client->device->card->lock);

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

	wait_event(client->tx_flush_wait, !has_outbound_transactions(client));

	idr_for_each(&client->resource_idr, shutdown_resource, client);
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
	.llseek		= no_llseek,
	.open		= fw_device_op_open,
	.read		= fw_device_op_read,
	.unlocked_ioctl	= fw_device_op_ioctl,
	.mmap		= fw_device_op_mmap,
	.release	= fw_device_op_release,
	.poll		= fw_device_op_poll,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= fw_device_op_compat_ioctl,
#endif
};
