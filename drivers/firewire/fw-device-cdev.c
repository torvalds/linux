/*						-*- c-basic-offset: 8 -*-
 *
 * fw-device-cdev.c - Char device for device raw access
 *
 * Copyright (C) 2005-2006  Kristian Hoegsberg <krh@bitplanet.net>
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/vmalloc.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/compat.h>
#include <asm/uaccess.h>
#include "fw-transaction.h"
#include "fw-topology.h"
#include "fw-device.h"
#include "fw-device-cdev.h"

/*
 * todo
 *
 * - bus resets sends a new packet with new generation and node id
 *
 */

/* dequeue_event() just kfree()'s the event, so the event has to be
 * the first field in the struct. */

struct event {
	struct { void *data; size_t size; } v[2];
	struct list_head link;
};

struct response {
	struct event event;
	struct fw_transaction transaction;
	struct client *client;
	struct fw_cdev_event_response response;
};

struct iso_interrupt {
	struct event event;
	struct fw_cdev_event_iso_interrupt interrupt;
};

struct client {
	struct fw_device *device;
	spinlock_t lock;
	struct list_head handler_list;
	struct list_head request_list;
	u32 request_serial;
	struct list_head event_list;
	struct semaphore event_list_sem;
	wait_queue_head_t wait;
	unsigned long vm_start;
	struct fw_iso_context *iso_context;
};

static inline void __user *
u64_to_uptr(__u64 value)
{
	return (void __user *)(unsigned long)value;
}

static inline __u64
uptr_to_u64(void __user *ptr)
{
	return (__u64)(unsigned long)ptr;
}

static int fw_device_op_open(struct inode *inode, struct file *file)
{
	struct fw_device *device;
	struct client *client;

	device = container_of(inode->i_cdev, struct fw_device, cdev);

	client = kzalloc(sizeof *client, GFP_KERNEL);
	if (client == NULL)
		return -ENOMEM;

	client->device = fw_device_get(device);
	INIT_LIST_HEAD(&client->event_list);
	sema_init(&client->event_list_sem, 0);
	INIT_LIST_HEAD(&client->handler_list);
	INIT_LIST_HEAD(&client->request_list);
	spin_lock_init(&client->lock);
	init_waitqueue_head(&client->wait);

	file->private_data = client;

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

	list_add_tail(&event->link, &client->event_list);

	up(&client->event_list_sem);
	wake_up_interruptible(&client->wait);

	spin_unlock_irqrestore(&client->lock, flags);
}

static int dequeue_event(struct client *client, char __user *buffer, size_t count)
{
	unsigned long flags;
	struct event *event;
	size_t size, total;
	int i, retval = -EFAULT;

	if (down_interruptible(&client->event_list_sem) < 0)
		return -EINTR;

	spin_lock_irqsave(&client->lock, flags);

	event = container_of(client->event_list.next, struct event, link);
	list_del(&event->link);

	spin_unlock_irqrestore(&client->lock, flags);

	if (buffer == NULL)
		goto out;

	total = 0;
	for (i = 0; i < ARRAY_SIZE(event->v) && total < count; i++) {
		size = min(event->v[i].size, count - total);
		if (copy_to_user(buffer + total, event->v[i].data, size))
			goto out;
		total += size;
	}
	retval = total;

 out:
	kfree(event);

	return retval;
}

static ssize_t
fw_device_op_read(struct file *file,
		  char __user *buffer, size_t count, loff_t *offset)
{
	struct client *client = file->private_data;

	return dequeue_event(client, buffer, count);
}

static int ioctl_config_rom(struct client *client, void __user *arg)
{
	struct fw_cdev_get_config_rom rom;

	rom.length = client->device->config_rom_length;
	memcpy(rom.data, client->device->config_rom, rom.length * 4);
	if (copy_to_user(arg, &rom,
			 (char *)&rom.data[rom.length] - (char *)&rom))
		return -EFAULT;

	return 0;
}

static void
complete_transaction(struct fw_card *card, int rcode,
		     void *payload, size_t length, void *data)
{
	struct response *response = data;
	struct client *client = response->client;

	if (length < response->response.length)
		response->response.length = length;
	if (rcode == RCODE_COMPLETE)
		memcpy(response->response.data, payload,
		       response->response.length);

	response->response.type   = FW_CDEV_EVENT_RESPONSE;
	response->response.rcode  = rcode;
	queue_event(client, &response->event,
		    &response->response, sizeof response->response,
		    response->response.data, response->response.length);
}

static ssize_t ioctl_send_request(struct client *client, void __user *arg)
{
	struct fw_device *device = client->device;
	struct fw_cdev_send_request request;
	struct response *response;

	if (copy_from_user(&request, arg, sizeof request))
		return -EFAULT;

	/* What is the biggest size we'll accept, really? */
	if (request.length > 4096)
		return -EINVAL;

	response = kmalloc(sizeof *response + request.length, GFP_KERNEL);
	if (response == NULL)
		return -ENOMEM;

	response->client = client;
	response->response.length = request.length;
	response->response.closure = request.closure;

	if (request.data &&
	    copy_from_user(response->response.data,
			   u64_to_uptr(request.data), request.length)) {
		kfree(response);
		return -EFAULT;
	}

	fw_send_request(device->card, &response->transaction,
			request.tcode,
			device->node->node_id,
			device->card->generation,
			device->node->max_speed,
			request.offset,
			response->response.data, request.length,
			complete_transaction, response);

	if (request.data)
		return sizeof request + request.length;
	else
		return sizeof request;
}

struct address_handler {
	struct fw_address_handler handler;
	__u64 closure;
	struct client *client;
	struct list_head link;
};

struct request {
	struct fw_request *request;
	void *data;
	size_t length;
	u32 serial;
	struct list_head link;
};

struct request_event {
	struct event event;
	struct fw_cdev_event_request request;
};

static void
handle_request(struct fw_card *card, struct fw_request *r,
	       int tcode, int destination, int source,
	       int generation, int speed,
	       unsigned long long offset,
	       void *payload, size_t length, void *callback_data)
{
	struct address_handler *handler = callback_data;
	struct request *request;
	struct request_event *e;
	unsigned long flags;
	struct client *client = handler->client;

	request = kmalloc(sizeof *request, GFP_ATOMIC);
	e = kmalloc(sizeof *e, GFP_ATOMIC);
	if (request == NULL || e == NULL) {
		kfree(request);
		kfree(e);
		fw_send_response(card, r, RCODE_CONFLICT_ERROR);
		return;
	}

	request->request = r;
	request->data    = payload;
	request->length  = length;

	spin_lock_irqsave(&client->lock, flags);
	request->serial = client->request_serial++;
	list_add_tail(&request->link, &client->request_list);
	spin_unlock_irqrestore(&client->lock, flags);

	e->request.type    = FW_CDEV_EVENT_REQUEST;
	e->request.tcode   = tcode;
	e->request.offset  = offset;
	e->request.length  = length;
	e->request.serial  = request->serial;
	e->request.closure = handler->closure;

	queue_event(client, &e->event,
		    &e->request, sizeof e->request, payload, length);
}

static int ioctl_allocate(struct client *client, void __user *arg)
{
	struct fw_cdev_allocate request;
	struct address_handler *handler;
	unsigned long flags;
	struct fw_address_region region;

	if (copy_from_user(&request, arg, sizeof request))
		return -EFAULT;

	handler = kmalloc(sizeof *handler, GFP_KERNEL);
	if (handler == NULL)
		return -ENOMEM;

	region.start = request.offset;
	region.end = request.offset + request.length;
	handler->handler.length = request.length;
	handler->handler.address_callback = handle_request;
	handler->handler.callback_data = handler;
	handler->closure = request.closure;
	handler->client = client;

	if (fw_core_add_address_handler(&handler->handler, &region) < 0) {
		kfree(handler);
		return -EBUSY;
	}

	spin_lock_irqsave(&client->lock, flags);
	list_add_tail(&handler->link, &client->handler_list);
	spin_unlock_irqrestore(&client->lock, flags);

	return 0;
}

static int ioctl_send_response(struct client *client, void __user *arg)
{
	struct fw_cdev_send_response request;
	struct request *r;
	unsigned long flags;

	if (copy_from_user(&request, arg, sizeof request))
		return -EFAULT;

	spin_lock_irqsave(&client->lock, flags);
	list_for_each_entry(r, &client->request_list, link) {
		if (r->serial == request.serial) {
			list_del(&r->link);
			break;
		}
	}
	spin_unlock_irqrestore(&client->lock, flags);

	if (&r->link == &client->request_list)
		return -EINVAL;

	if (request.length < r->length)
		r->length = request.length;
	if (copy_from_user(r->data, u64_to_uptr(request.data), r->length))
		return -EFAULT;

	fw_send_response(client->device->card, r->request, request.rcode);

	kfree(r);

	return 0;
}

static void
iso_callback(struct fw_iso_context *context, int status, u32 cycle, void *data)
{
	struct client *client = data;
	struct iso_interrupt *interrupt;

	interrupt = kzalloc(sizeof *interrupt, GFP_ATOMIC);
	if (interrupt == NULL)
		return;

	interrupt->interrupt.type      = FW_CDEV_EVENT_ISO_INTERRUPT;
	interrupt->interrupt.closure   = 0;
	interrupt->interrupt.cycle     = cycle;
	queue_event(client, &interrupt->event,
		    &interrupt->interrupt, sizeof interrupt->interrupt, NULL, 0);
}

static int ioctl_create_iso_context(struct client *client, void __user *arg)
{
	struct fw_cdev_create_iso_context request;

	if (copy_from_user(&request, arg, sizeof request))
		return -EFAULT;

	client->iso_context = fw_iso_context_create(client->device->card,
						    FW_ISO_CONTEXT_TRANSMIT,
						    request.buffer_size,
						    iso_callback, client);
	if (IS_ERR(client->iso_context))
		return PTR_ERR(client->iso_context);

	return 0;
}

static int ioctl_queue_iso(struct client *client, void __user *arg)
{
	struct fw_cdev_queue_iso request;
	struct fw_cdev_iso_packet __user *p, *end, *next;
	void *payload, *payload_end;
	unsigned long index;
	int count;
	struct {
		struct fw_iso_packet packet;
		u8 header[256];
	} u;

	if (client->iso_context == NULL)
		return -EINVAL;
	if (copy_from_user(&request, arg, sizeof request))
		return -EFAULT;

	/* If the user passes a non-NULL data pointer, has mmap()'ed
	 * the iso buffer, and the pointer points inside the buffer,
	 * we setup the payload pointers accordingly.  Otherwise we
	 * set them both to NULL, which will still let packets with
	 * payload_length == 0 through.  In other words, if no packets
	 * use the indirect payload, the iso buffer need not be mapped
	 * and the request.data pointer is ignored.*/

	index = (unsigned long)request.data - client->vm_start;
	if (request.data != 0 && client->vm_start != 0 &&
	    index <= client->iso_context->buffer_size) {
		payload = client->iso_context->buffer + index;
		payload_end = client->iso_context->buffer +
			client->iso_context->buffer_size;
	} else {
		payload = NULL;
		payload_end = NULL;
	}

	if (!access_ok(VERIFY_READ, request.packets, request.size))
		return -EFAULT;

	p = (struct fw_cdev_iso_packet __user *)u64_to_uptr(request.packets);
	end = (void __user *)p + request.size;
	count = 0;
	while (p < end) {
		if (__copy_from_user(&u.packet, p, sizeof *p))
			return -EFAULT;
		next = (struct fw_cdev_iso_packet __user *)
			&p->header[u.packet.header_length / 4];
		if (next > end)
			return -EINVAL;
		if (__copy_from_user
		    (u.packet.header, p->header, u.packet.header_length))
			return -EFAULT;
		if (u.packet.skip &&
		    u.packet.header_length + u.packet.payload_length > 0)
			return -EINVAL;
		if (payload + u.packet.payload_length > payload_end)
			return -EINVAL;

		if (fw_iso_context_queue(client->iso_context,
					 &u.packet, payload))
			break;

		p = next;
		payload += u.packet.payload_length;
		count++;
	}

	request.size    -= uptr_to_u64(p) - request.packets;
	request.packets  = uptr_to_u64(p);
	request.data     =
		client->vm_start + (payload - client->iso_context->buffer);

	if (copy_to_user(arg, &request, sizeof request))
		return -EFAULT;

	return count;
}

static int ioctl_send_iso(struct client *client, void __user *arg)
{
	struct fw_cdev_send_iso request;

	if (copy_from_user(&request, arg, sizeof request))
		return -EFAULT;

	return fw_iso_context_send(client->iso_context, request.channel,
				   request.speed, request.cycle);
}

static int
dispatch_ioctl(struct client *client, unsigned int cmd, void __user *arg)
{
	switch (cmd) {
	case FW_CDEV_IOC_GET_CONFIG_ROM:
		return ioctl_config_rom(client, arg);
	case FW_CDEV_IOC_SEND_REQUEST:
		return ioctl_send_request(client, arg);
	case FW_CDEV_IOC_ALLOCATE:
		return ioctl_allocate(client, arg);
	case FW_CDEV_IOC_SEND_RESPONSE:
		return ioctl_send_response(client, arg);
	case FW_CDEV_IOC_CREATE_ISO_CONTEXT:
		return ioctl_create_iso_context(client, arg);
	case FW_CDEV_IOC_QUEUE_ISO:
		return ioctl_queue_iso(client, arg);
	case FW_CDEV_IOC_SEND_ISO:
		return ioctl_send_iso(client, arg);
	default:
		return -EINVAL;
	}
}

static long
fw_device_op_ioctl(struct file *file,
		   unsigned int cmd, unsigned long arg)
{
	struct client *client = file->private_data;

	return dispatch_ioctl(client, cmd, (void __user *) arg);
}

#ifdef CONFIG_COMPAT
static long
fw_device_op_compat_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	struct client *client = file->private_data;

	return dispatch_ioctl(client, cmd, compat_ptr(arg));
}
#endif

static int fw_device_op_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct client *client = file->private_data;

	if (client->iso_context->buffer == NULL)
		return -EINVAL;

	client->vm_start = vma->vm_start;

	return remap_vmalloc_range(vma, client->iso_context->buffer, 0);
}

static int fw_device_op_release(struct inode *inode, struct file *file)
{
	struct client *client = file->private_data;
	struct address_handler *h, *next;
	struct request *r, *next_r;

	if (client->iso_context)
		fw_iso_context_destroy(client->iso_context);

	list_for_each_entry_safe(h, next, &client->handler_list, link) {
		fw_core_remove_address_handler(&h->handler);
		kfree(h);
	}

	list_for_each_entry_safe(r, next_r, &client->request_list, link) {
		fw_send_response(client->device->card, r->request,
				 RCODE_CONFLICT_ERROR);
		kfree(r);
	}

	/* TODO: wait for all transactions to finish so
	 * complete_transaction doesn't try to queue up responses
	 * after we free client. */
	while (!list_empty(&client->event_list))
		dequeue_event(client, NULL, 0);

	fw_device_put(client->device);
	kfree(client);

	return 0;
}

static unsigned int fw_device_op_poll(struct file *file, poll_table * pt)
{
	struct client *client = file->private_data;

	poll_wait(file, &client->wait, pt);

	if (!list_empty(&client->event_list))
		return POLLIN | POLLRDNORM;
	else
		return 0;
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
