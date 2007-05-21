/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id: device.c 1349 2004-12-16 21:09:43Z roland $
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

#include "core_priv.h"

MODULE_AUTHOR("Roland Dreier");
MODULE_DESCRIPTION("core kernel InfiniBand API");
MODULE_LICENSE("Dual BSD/GPL");

struct ib_client_data {
	struct list_head  list;
	struct ib_client *client;
	void *            data;
};

static LIST_HEAD(device_list);
static LIST_HEAD(client_list);

/*
 * device_mutex protects access to both device_list and client_list.
 * There's no real point to using multiple locks or something fancier
 * like an rwsem: we always access both lists, and we're always
 * modifying one list or the other list.  In any case this is not a
 * hot path so there's no point in trying to optimize.
 */
static DEFINE_MUTEX(device_mutex);

static int ib_device_check_mandatory(struct ib_device *device)
{
#define IB_MANDATORY_FUNC(x) { offsetof(struct ib_device, x), #x }
	static const struct {
		size_t offset;
		char  *name;
	} mandatory_table[] = {
		IB_MANDATORY_FUNC(query_device),
		IB_MANDATORY_FUNC(query_port),
		IB_MANDATORY_FUNC(query_pkey),
		IB_MANDATORY_FUNC(query_gid),
		IB_MANDATORY_FUNC(alloc_pd),
		IB_MANDATORY_FUNC(dealloc_pd),
		IB_MANDATORY_FUNC(create_ah),
		IB_MANDATORY_FUNC(destroy_ah),
		IB_MANDATORY_FUNC(create_qp),
		IB_MANDATORY_FUNC(modify_qp),
		IB_MANDATORY_FUNC(destroy_qp),
		IB_MANDATORY_FUNC(post_send),
		IB_MANDATORY_FUNC(post_recv),
		IB_MANDATORY_FUNC(create_cq),
		IB_MANDATORY_FUNC(destroy_cq),
		IB_MANDATORY_FUNC(poll_cq),
		IB_MANDATORY_FUNC(req_notify_cq),
		IB_MANDATORY_FUNC(get_dma_mr),
		IB_MANDATORY_FUNC(dereg_mr)
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(mandatory_table); ++i) {
		if (!*(void **) ((void *) device + mandatory_table[i].offset)) {
			printk(KERN_WARNING "Device %s is missing mandatory function %s\n",
			       device->name, mandatory_table[i].name);
			return -EINVAL;
		}
	}

	return 0;
}

static struct ib_device *__ib_device_get_by_name(const char *name)
{
	struct ib_device *device;

	list_for_each_entry(device, &device_list, core_list)
		if (!strncmp(name, device->name, IB_DEVICE_NAME_MAX))
			return device;

	return NULL;
}


static int alloc_name(char *name)
{
	long *inuse;
	char buf[IB_DEVICE_NAME_MAX];
	struct ib_device *device;
	int i;

	inuse = (long *) get_zeroed_page(GFP_KERNEL);
	if (!inuse)
		return -ENOMEM;

	list_for_each_entry(device, &device_list, core_list) {
		if (!sscanf(device->name, name, &i))
			continue;
		if (i < 0 || i >= PAGE_SIZE * 8)
			continue;
		snprintf(buf, sizeof buf, name, i);
		if (!strncmp(buf, device->name, IB_DEVICE_NAME_MAX))
			set_bit(i, inuse);
	}

	i = find_first_zero_bit(inuse, PAGE_SIZE * 8);
	free_page((unsigned long) inuse);
	snprintf(buf, sizeof buf, name, i);

	if (__ib_device_get_by_name(buf))
		return -ENFILE;

	strlcpy(name, buf, IB_DEVICE_NAME_MAX);
	return 0;
}

static int start_port(struct ib_device *device)
{
	return (device->node_type == RDMA_NODE_IB_SWITCH) ? 0 : 1;
}


static int end_port(struct ib_device *device)
{
	return (device->node_type == RDMA_NODE_IB_SWITCH) ?
		0 : device->phys_port_cnt;
}

/**
 * ib_alloc_device - allocate an IB device struct
 * @size:size of structure to allocate
 *
 * Low-level drivers should use ib_alloc_device() to allocate &struct
 * ib_device.  @size is the size of the structure to be allocated,
 * including any private data used by the low-level driver.
 * ib_dealloc_device() must be used to free structures allocated with
 * ib_alloc_device().
 */
struct ib_device *ib_alloc_device(size_t size)
{
	BUG_ON(size < sizeof (struct ib_device));

	return kzalloc(size, GFP_KERNEL);
}
EXPORT_SYMBOL(ib_alloc_device);

/**
 * ib_dealloc_device - free an IB device struct
 * @device:structure to free
 *
 * Free a structure allocated with ib_alloc_device().
 */
void ib_dealloc_device(struct ib_device *device)
{
	if (device->reg_state == IB_DEV_UNINITIALIZED) {
		kfree(device);
		return;
	}

	BUG_ON(device->reg_state != IB_DEV_UNREGISTERED);

	ib_device_unregister_sysfs(device);
}
EXPORT_SYMBOL(ib_dealloc_device);

static int add_client_context(struct ib_device *device, struct ib_client *client)
{
	struct ib_client_data *context;
	unsigned long flags;

	context = kmalloc(sizeof *context, GFP_KERNEL);
	if (!context) {
		printk(KERN_WARNING "Couldn't allocate client context for %s/%s\n",
		       device->name, client->name);
		return -ENOMEM;
	}

	context->client = client;
	context->data   = NULL;

	spin_lock_irqsave(&device->client_data_lock, flags);
	list_add(&context->list, &device->client_data_list);
	spin_unlock_irqrestore(&device->client_data_lock, flags);

	return 0;
}

static int read_port_table_lengths(struct ib_device *device)
{
	struct ib_port_attr *tprops = NULL;
	int num_ports, ret = -ENOMEM;
	u8 port_index;

	tprops = kmalloc(sizeof *tprops, GFP_KERNEL);
	if (!tprops)
		goto out;

	num_ports = end_port(device) - start_port(device) + 1;

	device->pkey_tbl_len = kmalloc(sizeof *device->pkey_tbl_len * num_ports,
				       GFP_KERNEL);
	device->gid_tbl_len = kmalloc(sizeof *device->gid_tbl_len * num_ports,
				      GFP_KERNEL);
	if (!device->pkey_tbl_len || !device->gid_tbl_len)
		goto err;

	for (port_index = 0; port_index < num_ports; ++port_index) {
		ret = ib_query_port(device, port_index + start_port(device),
					tprops);
		if (ret)
			goto err;
		device->pkey_tbl_len[port_index] = tprops->pkey_tbl_len;
		device->gid_tbl_len[port_index]  = tprops->gid_tbl_len;
	}

	ret = 0;
	goto out;

err:
	kfree(device->gid_tbl_len);
	kfree(device->pkey_tbl_len);
out:
	kfree(tprops);
	return ret;
}

/**
 * ib_register_device - Register an IB device with IB core
 * @device:Device to register
 *
 * Low-level drivers use ib_register_device() to register their
 * devices with the IB core.  All registered clients will receive a
 * callback for each device that is added. @device must be allocated
 * with ib_alloc_device().
 */
int ib_register_device(struct ib_device *device)
{
	int ret;

	mutex_lock(&device_mutex);

	if (strchr(device->name, '%')) {
		ret = alloc_name(device->name);
		if (ret)
			goto out;
	}

	if (ib_device_check_mandatory(device)) {
		ret = -EINVAL;
		goto out;
	}

	INIT_LIST_HEAD(&device->event_handler_list);
	INIT_LIST_HEAD(&device->client_data_list);
	spin_lock_init(&device->event_handler_lock);
	spin_lock_init(&device->client_data_lock);

	ret = read_port_table_lengths(device);
	if (ret) {
		printk(KERN_WARNING "Couldn't create table lengths cache for device %s\n",
		       device->name);
		goto out;
	}

	ret = ib_device_register_sysfs(device);
	if (ret) {
		printk(KERN_WARNING "Couldn't register device %s with driver model\n",
		       device->name);
		kfree(device->gid_tbl_len);
		kfree(device->pkey_tbl_len);
		goto out;
	}

	list_add_tail(&device->core_list, &device_list);

	device->reg_state = IB_DEV_REGISTERED;

	{
		struct ib_client *client;

		list_for_each_entry(client, &client_list, list)
			if (client->add && !add_client_context(device, client))
				client->add(device);
	}

 out:
	mutex_unlock(&device_mutex);
	return ret;
}
EXPORT_SYMBOL(ib_register_device);

/**
 * ib_unregister_device - Unregister an IB device
 * @device:Device to unregister
 *
 * Unregister an IB device.  All clients will receive a remove callback.
 */
void ib_unregister_device(struct ib_device *device)
{
	struct ib_client *client;
	struct ib_client_data *context, *tmp;
	unsigned long flags;

	mutex_lock(&device_mutex);

	list_for_each_entry_reverse(client, &client_list, list)
		if (client->remove)
			client->remove(device);

	list_del(&device->core_list);

	kfree(device->gid_tbl_len);
	kfree(device->pkey_tbl_len);

	mutex_unlock(&device_mutex);

	spin_lock_irqsave(&device->client_data_lock, flags);
	list_for_each_entry_safe(context, tmp, &device->client_data_list, list)
		kfree(context);
	spin_unlock_irqrestore(&device->client_data_lock, flags);

	device->reg_state = IB_DEV_UNREGISTERED;
}
EXPORT_SYMBOL(ib_unregister_device);

/**
 * ib_register_client - Register an IB client
 * @client:Client to register
 *
 * Upper level users of the IB drivers can use ib_register_client() to
 * register callbacks for IB device addition and removal.  When an IB
 * device is added, each registered client's add method will be called
 * (in the order the clients were registered), and when a device is
 * removed, each client's remove method will be called (in the reverse
 * order that clients were registered).  In addition, when
 * ib_register_client() is called, the client will receive an add
 * callback for all devices already registered.
 */
int ib_register_client(struct ib_client *client)
{
	struct ib_device *device;

	mutex_lock(&device_mutex);

	list_add_tail(&client->list, &client_list);
	list_for_each_entry(device, &device_list, core_list)
		if (client->add && !add_client_context(device, client))
			client->add(device);

	mutex_unlock(&device_mutex);

	return 0;
}
EXPORT_SYMBOL(ib_register_client);

/**
 * ib_unregister_client - Unregister an IB client
 * @client:Client to unregister
 *
 * Upper level users use ib_unregister_client() to remove their client
 * registration.  When ib_unregister_client() is called, the client
 * will receive a remove callback for each IB device still registered.
 */
void ib_unregister_client(struct ib_client *client)
{
	struct ib_client_data *context, *tmp;
	struct ib_device *device;
	unsigned long flags;

	mutex_lock(&device_mutex);

	list_for_each_entry(device, &device_list, core_list) {
		if (client->remove)
			client->remove(device);

		spin_lock_irqsave(&device->client_data_lock, flags);
		list_for_each_entry_safe(context, tmp, &device->client_data_list, list)
			if (context->client == client) {
				list_del(&context->list);
				kfree(context);
			}
		spin_unlock_irqrestore(&device->client_data_lock, flags);
	}
	list_del(&client->list);

	mutex_unlock(&device_mutex);
}
EXPORT_SYMBOL(ib_unregister_client);

/**
 * ib_get_client_data - Get IB client context
 * @device:Device to get context for
 * @client:Client to get context for
 *
 * ib_get_client_data() returns client context set with
 * ib_set_client_data().
 */
void *ib_get_client_data(struct ib_device *device, struct ib_client *client)
{
	struct ib_client_data *context;
	void *ret = NULL;
	unsigned long flags;

	spin_lock_irqsave(&device->client_data_lock, flags);
	list_for_each_entry(context, &device->client_data_list, list)
		if (context->client == client) {
			ret = context->data;
			break;
		}
	spin_unlock_irqrestore(&device->client_data_lock, flags);

	return ret;
}
EXPORT_SYMBOL(ib_get_client_data);

/**
 * ib_set_client_data - Set IB client context
 * @device:Device to set context for
 * @client:Client to set context for
 * @data:Context to set
 *
 * ib_set_client_data() sets client context that can be retrieved with
 * ib_get_client_data().
 */
void ib_set_client_data(struct ib_device *device, struct ib_client *client,
			void *data)
{
	struct ib_client_data *context;
	unsigned long flags;

	spin_lock_irqsave(&device->client_data_lock, flags);
	list_for_each_entry(context, &device->client_data_list, list)
		if (context->client == client) {
			context->data = data;
			goto out;
		}

	printk(KERN_WARNING "No client context found for %s/%s\n",
	       device->name, client->name);

out:
	spin_unlock_irqrestore(&device->client_data_lock, flags);
}
EXPORT_SYMBOL(ib_set_client_data);

/**
 * ib_register_event_handler - Register an IB event handler
 * @event_handler:Handler to register
 *
 * ib_register_event_handler() registers an event handler that will be
 * called back when asynchronous IB events occur (as defined in
 * chapter 11 of the InfiniBand Architecture Specification).  This
 * callback may occur in interrupt context.
 */
int ib_register_event_handler  (struct ib_event_handler *event_handler)
{
	unsigned long flags;

	spin_lock_irqsave(&event_handler->device->event_handler_lock, flags);
	list_add_tail(&event_handler->list,
		      &event_handler->device->event_handler_list);
	spin_unlock_irqrestore(&event_handler->device->event_handler_lock, flags);

	return 0;
}
EXPORT_SYMBOL(ib_register_event_handler);

/**
 * ib_unregister_event_handler - Unregister an event handler
 * @event_handler:Handler to unregister
 *
 * Unregister an event handler registered with
 * ib_register_event_handler().
 */
int ib_unregister_event_handler(struct ib_event_handler *event_handler)
{
	unsigned long flags;

	spin_lock_irqsave(&event_handler->device->event_handler_lock, flags);
	list_del(&event_handler->list);
	spin_unlock_irqrestore(&event_handler->device->event_handler_lock, flags);

	return 0;
}
EXPORT_SYMBOL(ib_unregister_event_handler);

/**
 * ib_dispatch_event - Dispatch an asynchronous event
 * @event:Event to dispatch
 *
 * Low-level drivers must call ib_dispatch_event() to dispatch the
 * event to all registered event handlers when an asynchronous event
 * occurs.
 */
void ib_dispatch_event(struct ib_event *event)
{
	unsigned long flags;
	struct ib_event_handler *handler;

	spin_lock_irqsave(&event->device->event_handler_lock, flags);

	list_for_each_entry(handler, &event->device->event_handler_list, list)
		handler->handler(handler, event);

	spin_unlock_irqrestore(&event->device->event_handler_lock, flags);
}
EXPORT_SYMBOL(ib_dispatch_event);

/**
 * ib_query_device - Query IB device attributes
 * @device:Device to query
 * @device_attr:Device attributes
 *
 * ib_query_device() returns the attributes of a device through the
 * @device_attr pointer.
 */
int ib_query_device(struct ib_device *device,
		    struct ib_device_attr *device_attr)
{
	return device->query_device(device, device_attr);
}
EXPORT_SYMBOL(ib_query_device);

/**
 * ib_query_port - Query IB port attributes
 * @device:Device to query
 * @port_num:Port number to query
 * @port_attr:Port attributes
 *
 * ib_query_port() returns the attributes of a port through the
 * @port_attr pointer.
 */
int ib_query_port(struct ib_device *device,
		  u8 port_num,
		  struct ib_port_attr *port_attr)
{
	if (port_num < start_port(device) || port_num > end_port(device))
		return -EINVAL;

	return device->query_port(device, port_num, port_attr);
}
EXPORT_SYMBOL(ib_query_port);

/**
 * ib_query_gid - Get GID table entry
 * @device:Device to query
 * @port_num:Port number to query
 * @index:GID table index to query
 * @gid:Returned GID
 *
 * ib_query_gid() fetches the specified GID table entry.
 */
int ib_query_gid(struct ib_device *device,
		 u8 port_num, int index, union ib_gid *gid)
{
	return device->query_gid(device, port_num, index, gid);
}
EXPORT_SYMBOL(ib_query_gid);

/**
 * ib_query_pkey - Get P_Key table entry
 * @device:Device to query
 * @port_num:Port number to query
 * @index:P_Key table index to query
 * @pkey:Returned P_Key
 *
 * ib_query_pkey() fetches the specified P_Key table entry.
 */
int ib_query_pkey(struct ib_device *device,
		  u8 port_num, u16 index, u16 *pkey)
{
	return device->query_pkey(device, port_num, index, pkey);
}
EXPORT_SYMBOL(ib_query_pkey);

/**
 * ib_modify_device - Change IB device attributes
 * @device:Device to modify
 * @device_modify_mask:Mask of attributes to change
 * @device_modify:New attribute values
 *
 * ib_modify_device() changes a device's attributes as specified by
 * the @device_modify_mask and @device_modify structure.
 */
int ib_modify_device(struct ib_device *device,
		     int device_modify_mask,
		     struct ib_device_modify *device_modify)
{
	return device->modify_device(device, device_modify_mask,
				     device_modify);
}
EXPORT_SYMBOL(ib_modify_device);

/**
 * ib_modify_port - Modifies the attributes for the specified port.
 * @device: The device to modify.
 * @port_num: The number of the port to modify.
 * @port_modify_mask: Mask used to specify which attributes of the port
 *   to change.
 * @port_modify: New attribute values for the port.
 *
 * ib_modify_port() changes a port's attributes as specified by the
 * @port_modify_mask and @port_modify structure.
 */
int ib_modify_port(struct ib_device *device,
		   u8 port_num, int port_modify_mask,
		   struct ib_port_modify *port_modify)
{
	if (port_num < start_port(device) || port_num > end_port(device))
		return -EINVAL;

	return device->modify_port(device, port_num, port_modify_mask,
				   port_modify);
}
EXPORT_SYMBOL(ib_modify_port);

/**
 * ib_find_gid - Returns the port number and GID table index where
 *   a specified GID value occurs.
 * @device: The device to query.
 * @gid: The GID value to search for.
 * @port_num: The port number of the device where the GID value was found.
 * @index: The index into the GID table where the GID was found.  This
 *   parameter may be NULL.
 */
int ib_find_gid(struct ib_device *device, union ib_gid *gid,
		u8 *port_num, u16 *index)
{
	union ib_gid tmp_gid;
	int ret, port, i;

	for (port = start_port(device); port <= end_port(device); ++port) {
		for (i = 0; i < device->gid_tbl_len[port - start_port(device)]; ++i) {
			ret = ib_query_gid(device, port, i, &tmp_gid);
			if (ret)
				return ret;
			if (!memcmp(&tmp_gid, gid, sizeof *gid)) {
				*port_num = port;
				if (index)
					*index = i;
				return 0;
			}
		}
	}

	return -ENOENT;
}
EXPORT_SYMBOL(ib_find_gid);

/**
 * ib_find_pkey - Returns the PKey table index where a specified
 *   PKey value occurs.
 * @device: The device to query.
 * @port_num: The port number of the device to search for the PKey.
 * @pkey: The PKey value to search for.
 * @index: The index into the PKey table where the PKey was found.
 */
int ib_find_pkey(struct ib_device *device,
		 u8 port_num, u16 pkey, u16 *index)
{
	int ret, i;
	u16 tmp_pkey;

	for (i = 0; i < device->pkey_tbl_len[port_num - start_port(device)]; ++i) {
		ret = ib_query_pkey(device, port_num, i, &tmp_pkey);
		if (ret)
			return ret;

		if (pkey == tmp_pkey) {
			*index = i;
			return 0;
		}
	}

	return -ENOENT;
}
EXPORT_SYMBOL(ib_find_pkey);

static int __init ib_core_init(void)
{
	int ret;

	ret = ib_sysfs_setup();
	if (ret)
		printk(KERN_WARNING "Couldn't create InfiniBand device class\n");

	ret = ib_cache_setup();
	if (ret) {
		printk(KERN_WARNING "Couldn't set up InfiniBand P_Key/GID cache\n");
		ib_sysfs_cleanup();
	}

	return ret;
}

static void __exit ib_core_cleanup(void)
{
	ib_cache_cleanup();
	ib_sysfs_cleanup();
	/* Make sure that any pending umem accounting work is done. */
	flush_scheduled_work();
}

module_init(ib_core_init);
module_exit(ib_core_cleanup);
