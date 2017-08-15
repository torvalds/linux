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
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/security.h>
#include <linux/notifier.h>
#include <rdma/rdma_netlink.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_cache.h>

#include "core_priv.h"

MODULE_AUTHOR("Roland Dreier");
MODULE_DESCRIPTION("core kernel InfiniBand API");
MODULE_LICENSE("Dual BSD/GPL");

struct ib_client_data {
	struct list_head  list;
	struct ib_client *client;
	void *            data;
	/* The device or client is going down. Do not call client or device
	 * callbacks other than remove(). */
	bool		  going_down;
};

struct workqueue_struct *ib_comp_wq;
struct workqueue_struct *ib_wq;
EXPORT_SYMBOL_GPL(ib_wq);

/* The device_list and client_list contain devices and clients after their
 * registration has completed, and the devices and clients are removed
 * during unregistration. */
static LIST_HEAD(device_list);
static LIST_HEAD(client_list);

/*
 * device_mutex and lists_rwsem protect access to both device_list and
 * client_list.  device_mutex protects writer access by device and client
 * registration / de-registration.  lists_rwsem protects reader access to
 * these lists.  Iterators of these lists must lock it for read, while updates
 * to the lists must be done with a write lock. A special case is when the
 * device_mutex is locked. In this case locking the lists for read access is
 * not necessary as the device_mutex implies it.
 *
 * lists_rwsem also protects access to the client data list.
 */
static DEFINE_MUTEX(device_mutex);
static DECLARE_RWSEM(lists_rwsem);

static int ib_security_change(struct notifier_block *nb, unsigned long event,
			      void *lsm_data);
static void ib_policy_change_task(struct work_struct *work);
static DECLARE_WORK(ib_policy_change_work, ib_policy_change_task);

static struct notifier_block ibdev_lsm_nb = {
	.notifier_call = ib_security_change,
};

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
		IB_MANDATORY_FUNC(dereg_mr),
		IB_MANDATORY_FUNC(get_port_immutable)
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(mandatory_table); ++i) {
		if (!*(void **) ((void *) device + mandatory_table[i].offset)) {
			pr_warn("Device %s is missing mandatory function %s\n",
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
	unsigned long *inuse;
	char buf[IB_DEVICE_NAME_MAX];
	struct ib_device *device;
	int i;

	inuse = (unsigned long *) get_zeroed_page(GFP_KERNEL);
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

static void ib_device_release(struct device *device)
{
	struct ib_device *dev = container_of(device, struct ib_device, dev);

	WARN_ON(dev->reg_state == IB_DEV_REGISTERED);
	if (dev->reg_state == IB_DEV_UNREGISTERED) {
		/*
		 * In IB_DEV_UNINITIALIZED state, cache or port table
		 * is not even created. Free cache and port table only when
		 * device reaches UNREGISTERED state.
		 */
		ib_cache_release_one(dev);
		kfree(dev->port_immutable);
	}
	kfree(dev);
}

static int ib_device_uevent(struct device *device,
			    struct kobj_uevent_env *env)
{
	struct ib_device *dev = container_of(device, struct ib_device, dev);

	if (add_uevent_var(env, "NAME=%s", dev->name))
		return -ENOMEM;

	/*
	 * It would be nice to pass the node GUID with the event...
	 */

	return 0;
}

static struct class ib_class = {
	.name    = "infiniband",
	.dev_release = ib_device_release,
	.dev_uevent = ib_device_uevent,
};

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
	struct ib_device *device;

	if (WARN_ON(size < sizeof(struct ib_device)))
		return NULL;

	device = kzalloc(size, GFP_KERNEL);
	if (!device)
		return NULL;

	device->dev.class = &ib_class;
	device_initialize(&device->dev);

	dev_set_drvdata(&device->dev, device);

	INIT_LIST_HEAD(&device->event_handler_list);
	spin_lock_init(&device->event_handler_lock);
	spin_lock_init(&device->client_data_lock);
	INIT_LIST_HEAD(&device->client_data_list);
	INIT_LIST_HEAD(&device->port_list);

	return device;
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
	WARN_ON(device->reg_state != IB_DEV_UNREGISTERED &&
		device->reg_state != IB_DEV_UNINITIALIZED);
	kobject_put(&device->dev.kobj);
}
EXPORT_SYMBOL(ib_dealloc_device);

static int add_client_context(struct ib_device *device, struct ib_client *client)
{
	struct ib_client_data *context;
	unsigned long flags;

	context = kmalloc(sizeof *context, GFP_KERNEL);
	if (!context)
		return -ENOMEM;

	context->client = client;
	context->data   = NULL;
	context->going_down = false;

	down_write(&lists_rwsem);
	spin_lock_irqsave(&device->client_data_lock, flags);
	list_add(&context->list, &device->client_data_list);
	spin_unlock_irqrestore(&device->client_data_lock, flags);
	up_write(&lists_rwsem);

	return 0;
}

static int verify_immutable(const struct ib_device *dev, u8 port)
{
	return WARN_ON(!rdma_cap_ib_mad(dev, port) &&
			    rdma_max_mad_size(dev, port) != 0);
}

static int read_port_immutable(struct ib_device *device)
{
	int ret;
	u8 start_port = rdma_start_port(device);
	u8 end_port = rdma_end_port(device);
	u8 port;

	/**
	 * device->port_immutable is indexed directly by the port number to make
	 * access to this data as efficient as possible.
	 *
	 * Therefore port_immutable is declared as a 1 based array with
	 * potential empty slots at the beginning.
	 */
	device->port_immutable = kzalloc(sizeof(*device->port_immutable)
					 * (end_port + 1),
					 GFP_KERNEL);
	if (!device->port_immutable)
		return -ENOMEM;

	for (port = start_port; port <= end_port; ++port) {
		ret = device->get_port_immutable(device, port,
						 &device->port_immutable[port]);
		if (ret)
			return ret;

		if (verify_immutable(device, port))
			return -EINVAL;
	}
	return 0;
}

void ib_get_device_fw_str(struct ib_device *dev, char *str, size_t str_len)
{
	if (dev->get_dev_fw_str)
		dev->get_dev_fw_str(dev, str, str_len);
	else
		str[0] = '\0';
}
EXPORT_SYMBOL(ib_get_device_fw_str);

static int setup_port_pkey_list(struct ib_device *device)
{
	int i;

	/**
	 * device->port_pkey_list is indexed directly by the port number,
	 * Therefore it is declared as a 1 based array with potential empty
	 * slots at the beginning.
	 */
	device->port_pkey_list = kcalloc(rdma_end_port(device) + 1,
					 sizeof(*device->port_pkey_list),
					 GFP_KERNEL);

	if (!device->port_pkey_list)
		return -ENOMEM;

	for (i = 0; i < (rdma_end_port(device) + 1); i++) {
		spin_lock_init(&device->port_pkey_list[i].list_lock);
		INIT_LIST_HEAD(&device->port_pkey_list[i].pkey_list);
	}

	return 0;
}

static void ib_policy_change_task(struct work_struct *work)
{
	struct ib_device *dev;

	down_read(&lists_rwsem);
	list_for_each_entry(dev, &device_list, core_list) {
		int i;

		for (i = rdma_start_port(dev); i <= rdma_end_port(dev); i++) {
			u64 sp;
			int ret = ib_get_cached_subnet_prefix(dev,
							      i,
							      &sp);

			WARN_ONCE(ret,
				  "ib_get_cached_subnet_prefix err: %d, this should never happen here\n",
				  ret);
			if (!ret)
				ib_security_cache_change(dev, i, sp);
		}
	}
	up_read(&lists_rwsem);
}

static int ib_security_change(struct notifier_block *nb, unsigned long event,
			      void *lsm_data)
{
	if (event != LSM_POLICY_CHANGE)
		return NOTIFY_DONE;

	schedule_work(&ib_policy_change_work);

	return NOTIFY_OK;
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
int ib_register_device(struct ib_device *device,
		       int (*port_callback)(struct ib_device *,
					    u8, struct kobject *))
{
	int ret;
	struct ib_client *client;
	struct ib_udata uhw = {.outlen = 0, .inlen = 0};
	struct device *parent = device->dev.parent;

	WARN_ON_ONCE(!parent);
	WARN_ON_ONCE(device->dma_device);
	if (device->dev.dma_ops) {
		/*
		 * The caller provided custom DMA operations. Copy the
		 * DMA-related fields that are used by e.g. dma_alloc_coherent()
		 * into device->dev.
		 */
		device->dma_device = &device->dev;
		if (!device->dev.dma_mask)
			device->dev.dma_mask = parent->dma_mask;
		if (!device->dev.coherent_dma_mask)
			device->dev.coherent_dma_mask =
				parent->coherent_dma_mask;
	} else {
		/*
		 * The caller did not provide custom DMA operations. Use the
		 * DMA mapping operations of the parent device.
		 */
		device->dma_device = parent;
	}

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

	ret = read_port_immutable(device);
	if (ret) {
		pr_warn("Couldn't create per port immutable data %s\n",
			device->name);
		goto out;
	}

	ret = setup_port_pkey_list(device);
	if (ret) {
		pr_warn("Couldn't create per port_pkey_list\n");
		goto out;
	}

	ret = ib_cache_setup_one(device);
	if (ret) {
		pr_warn("Couldn't set up InfiniBand P_Key/GID cache\n");
		goto port_cleanup;
	}

	ret = ib_device_register_rdmacg(device);
	if (ret) {
		pr_warn("Couldn't register device with rdma cgroup\n");
		goto cache_cleanup;
	}

	memset(&device->attrs, 0, sizeof(device->attrs));
	ret = device->query_device(device, &device->attrs, &uhw);
	if (ret) {
		pr_warn("Couldn't query the device attributes\n");
		goto cache_cleanup;
	}

	ret = ib_device_register_sysfs(device, port_callback);
	if (ret) {
		pr_warn("Couldn't register device %s with driver model\n",
			device->name);
		goto cache_cleanup;
	}

	device->reg_state = IB_DEV_REGISTERED;

	list_for_each_entry(client, &client_list, list)
		if (client->add && !add_client_context(device, client))
			client->add(device);

	down_write(&lists_rwsem);
	list_add_tail(&device->core_list, &device_list);
	up_write(&lists_rwsem);
	mutex_unlock(&device_mutex);
	return 0;

cache_cleanup:
	ib_cache_cleanup_one(device);
	ib_cache_release_one(device);
port_cleanup:
	kfree(device->port_immutable);
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
	struct ib_client_data *context, *tmp;
	unsigned long flags;

	mutex_lock(&device_mutex);

	down_write(&lists_rwsem);
	list_del(&device->core_list);
	spin_lock_irqsave(&device->client_data_lock, flags);
	list_for_each_entry_safe(context, tmp, &device->client_data_list, list)
		context->going_down = true;
	spin_unlock_irqrestore(&device->client_data_lock, flags);
	downgrade_write(&lists_rwsem);

	list_for_each_entry_safe(context, tmp, &device->client_data_list,
				 list) {
		if (context->client->remove)
			context->client->remove(device, context->data);
	}
	up_read(&lists_rwsem);

	mutex_unlock(&device_mutex);

	ib_device_unregister_rdmacg(device);
	ib_device_unregister_sysfs(device);
	ib_cache_cleanup_one(device);

	ib_security_destroy_port_pkey_list(device);
	kfree(device->port_pkey_list);

	down_write(&lists_rwsem);
	spin_lock_irqsave(&device->client_data_lock, flags);
	list_for_each_entry_safe(context, tmp, &device->client_data_list, list)
		kfree(context);
	spin_unlock_irqrestore(&device->client_data_lock, flags);
	up_write(&lists_rwsem);

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

	list_for_each_entry(device, &device_list, core_list)
		if (client->add && !add_client_context(device, client))
			client->add(device);

	down_write(&lists_rwsem);
	list_add_tail(&client->list, &client_list);
	up_write(&lists_rwsem);

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

	down_write(&lists_rwsem);
	list_del(&client->list);
	up_write(&lists_rwsem);

	list_for_each_entry(device, &device_list, core_list) {
		struct ib_client_data *found_context = NULL;

		down_write(&lists_rwsem);
		spin_lock_irqsave(&device->client_data_lock, flags);
		list_for_each_entry_safe(context, tmp, &device->client_data_list, list)
			if (context->client == client) {
				context->going_down = true;
				found_context = context;
				break;
			}
		spin_unlock_irqrestore(&device->client_data_lock, flags);
		up_write(&lists_rwsem);

		if (client->remove)
			client->remove(device, found_context ?
					       found_context->data : NULL);

		if (!found_context) {
			pr_warn("No client context found for %s/%s\n",
				device->name, client->name);
			continue;
		}

		down_write(&lists_rwsem);
		spin_lock_irqsave(&device->client_data_lock, flags);
		list_del(&found_context->list);
		kfree(found_context);
		spin_unlock_irqrestore(&device->client_data_lock, flags);
		up_write(&lists_rwsem);
	}

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

	pr_warn("No client context found for %s/%s\n",
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
	union ib_gid gid;
	int err;

	if (!rdma_is_port_valid(device, port_num))
		return -EINVAL;

	memset(port_attr, 0, sizeof(*port_attr));
	err = device->query_port(device, port_num, port_attr);
	if (err || port_attr->subnet_prefix)
		return err;

	if (rdma_port_get_link_layer(device, port_num) != IB_LINK_LAYER_INFINIBAND)
		return 0;

	err = ib_query_gid(device, port_num, 0, &gid, NULL);
	if (err)
		return err;

	port_attr->subnet_prefix = be64_to_cpu(gid.global.subnet_prefix);
	return 0;
}
EXPORT_SYMBOL(ib_query_port);

/**
 * ib_query_gid - Get GID table entry
 * @device:Device to query
 * @port_num:Port number to query
 * @index:GID table index to query
 * @gid:Returned GID
 * @attr: Returned GID attributes related to this GID index (only in RoCE).
 *   NULL means ignore.
 *
 * ib_query_gid() fetches the specified GID table entry.
 */
int ib_query_gid(struct ib_device *device,
		 u8 port_num, int index, union ib_gid *gid,
		 struct ib_gid_attr *attr)
{
	if (rdma_cap_roce_gid_table(device, port_num))
		return ib_get_cached_gid(device, port_num, index, gid, attr);

	if (attr)
		return -EINVAL;

	return device->query_gid(device, port_num, index, gid);
}
EXPORT_SYMBOL(ib_query_gid);

/**
 * ib_enum_roce_netdev - enumerate all RoCE ports
 * @ib_dev : IB device we want to query
 * @filter: Should we call the callback?
 * @filter_cookie: Cookie passed to filter
 * @cb: Callback to call for each found RoCE ports
 * @cookie: Cookie passed back to the callback
 *
 * Enumerates all of the physical RoCE ports of ib_dev
 * which are related to netdevice and calls callback() on each
 * device for which filter() function returns non zero.
 */
void ib_enum_roce_netdev(struct ib_device *ib_dev,
			 roce_netdev_filter filter,
			 void *filter_cookie,
			 roce_netdev_callback cb,
			 void *cookie)
{
	u8 port;

	for (port = rdma_start_port(ib_dev); port <= rdma_end_port(ib_dev);
	     port++)
		if (rdma_protocol_roce(ib_dev, port)) {
			struct net_device *idev = NULL;

			if (ib_dev->get_netdev)
				idev = ib_dev->get_netdev(ib_dev, port);

			if (idev &&
			    idev->reg_state >= NETREG_UNREGISTERED) {
				dev_put(idev);
				idev = NULL;
			}

			if (filter(ib_dev, port, idev, filter_cookie))
				cb(ib_dev, port, idev, cookie);

			if (idev)
				dev_put(idev);
		}
}

/**
 * ib_enum_all_roce_netdevs - enumerate all RoCE devices
 * @filter: Should we call the callback?
 * @filter_cookie: Cookie passed to filter
 * @cb: Callback to call for each found RoCE ports
 * @cookie: Cookie passed back to the callback
 *
 * Enumerates all RoCE devices' physical ports which are related
 * to netdevices and calls callback() on each device for which
 * filter() function returns non zero.
 */
void ib_enum_all_roce_netdevs(roce_netdev_filter filter,
			      void *filter_cookie,
			      roce_netdev_callback cb,
			      void *cookie)
{
	struct ib_device *dev;

	down_read(&lists_rwsem);
	list_for_each_entry(dev, &device_list, core_list)
		ib_enum_roce_netdev(dev, filter, filter_cookie, cb, cookie);
	up_read(&lists_rwsem);
}

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
	if (!device->modify_device)
		return -ENOSYS;

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
	if (!device->modify_port)
		return -ENOSYS;

	if (!rdma_is_port_valid(device, port_num))
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
 * @gid_type: Type of GID.
 * @ndev: The ndev related to the GID to search for.
 * @port_num: The port number of the device where the GID value was found.
 * @index: The index into the GID table where the GID was found.  This
 *   parameter may be NULL.
 */
int ib_find_gid(struct ib_device *device, union ib_gid *gid,
		enum ib_gid_type gid_type, struct net_device *ndev,
		u8 *port_num, u16 *index)
{
	union ib_gid tmp_gid;
	int ret, port, i;

	for (port = rdma_start_port(device); port <= rdma_end_port(device); ++port) {
		if (rdma_cap_roce_gid_table(device, port)) {
			if (!ib_find_cached_gid_by_port(device, gid, gid_type, port,
							ndev, index)) {
				*port_num = port;
				return 0;
			}
		}

		if (gid_type != IB_GID_TYPE_IB)
			continue;

		for (i = 0; i < device->port_immutable[port].gid_tbl_len; ++i) {
			ret = ib_query_gid(device, port, i, &tmp_gid, NULL);
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
	int partial_ix = -1;

	for (i = 0; i < device->port_immutable[port_num].pkey_tbl_len; ++i) {
		ret = ib_query_pkey(device, port_num, i, &tmp_pkey);
		if (ret)
			return ret;
		if ((pkey & 0x7fff) == (tmp_pkey & 0x7fff)) {
			/* if there is full-member pkey take it.*/
			if (tmp_pkey & 0x8000) {
				*index = i;
				return 0;
			}
			if (partial_ix < 0)
				partial_ix = i;
		}
	}

	/*no full-member, if exists take the limited*/
	if (partial_ix >= 0) {
		*index = partial_ix;
		return 0;
	}
	return -ENOENT;
}
EXPORT_SYMBOL(ib_find_pkey);

/**
 * ib_get_net_dev_by_params() - Return the appropriate net_dev
 * for a received CM request
 * @dev:	An RDMA device on which the request has been received.
 * @port:	Port number on the RDMA device.
 * @pkey:	The Pkey the request came on.
 * @gid:	A GID that the net_dev uses to communicate.
 * @addr:	Contains the IP address that the request specified as its
 *		destination.
 */
struct net_device *ib_get_net_dev_by_params(struct ib_device *dev,
					    u8 port,
					    u16 pkey,
					    const union ib_gid *gid,
					    const struct sockaddr *addr)
{
	struct net_device *net_dev = NULL;
	struct ib_client_data *context;

	if (!rdma_protocol_ib(dev, port))
		return NULL;

	down_read(&lists_rwsem);

	list_for_each_entry(context, &dev->client_data_list, list) {
		struct ib_client *client = context->client;

		if (context->going_down)
			continue;

		if (client->get_net_dev_by_params) {
			net_dev = client->get_net_dev_by_params(dev, port, pkey,
								gid, addr,
								context->data);
			if (net_dev)
				break;
		}
	}

	up_read(&lists_rwsem);

	return net_dev;
}
EXPORT_SYMBOL(ib_get_net_dev_by_params);

static struct ibnl_client_cbs ibnl_ls_cb_table[] = {
	[RDMA_NL_LS_OP_RESOLVE] = {
		.dump = ib_nl_handle_resolve_resp,
		.module = THIS_MODULE },
	[RDMA_NL_LS_OP_SET_TIMEOUT] = {
		.dump = ib_nl_handle_set_timeout,
		.module = THIS_MODULE },
	[RDMA_NL_LS_OP_IP_RESOLVE] = {
		.dump = ib_nl_handle_ip_res_resp,
		.module = THIS_MODULE },
};

static int ib_add_ibnl_clients(void)
{
	return ibnl_add_client(RDMA_NL_LS, ARRAY_SIZE(ibnl_ls_cb_table),
			       ibnl_ls_cb_table);
}

static void ib_remove_ibnl_clients(void)
{
	ibnl_remove_client(RDMA_NL_LS);
}

static int __init ib_core_init(void)
{
	int ret;

	ib_wq = alloc_workqueue("infiniband", 0, 0);
	if (!ib_wq)
		return -ENOMEM;

	ib_comp_wq = alloc_workqueue("ib-comp-wq",
			WQ_HIGHPRI | WQ_MEM_RECLAIM | WQ_SYSFS, 0);
	if (!ib_comp_wq) {
		ret = -ENOMEM;
		goto err;
	}

	ret = class_register(&ib_class);
	if (ret) {
		pr_warn("Couldn't create InfiniBand device class\n");
		goto err_comp;
	}

	ret = ibnl_init();
	if (ret) {
		pr_warn("Couldn't init IB netlink interface\n");
		goto err_sysfs;
	}

	ret = addr_init();
	if (ret) {
		pr_warn("Could't init IB address resolution\n");
		goto err_ibnl;
	}

	ret = ib_mad_init();
	if (ret) {
		pr_warn("Couldn't init IB MAD\n");
		goto err_addr;
	}

	ret = ib_sa_init();
	if (ret) {
		pr_warn("Couldn't init SA\n");
		goto err_mad;
	}

	ret = ib_add_ibnl_clients();
	if (ret) {
		pr_warn("Couldn't register ibnl clients\n");
		goto err_sa;
	}

	ret = register_lsm_notifier(&ibdev_lsm_nb);
	if (ret) {
		pr_warn("Couldn't register LSM notifier. ret %d\n", ret);
		goto err_ibnl_clients;
	}

	ib_cache_setup();

	return 0;

err_ibnl_clients:
	ib_remove_ibnl_clients();
err_sa:
	ib_sa_cleanup();
err_mad:
	ib_mad_cleanup();
err_addr:
	addr_cleanup();
err_ibnl:
	ibnl_cleanup();
err_sysfs:
	class_unregister(&ib_class);
err_comp:
	destroy_workqueue(ib_comp_wq);
err:
	destroy_workqueue(ib_wq);
	return ret;
}

static void __exit ib_core_cleanup(void)
{
	unregister_lsm_notifier(&ibdev_lsm_nb);
	ib_cache_cleanup();
	ib_remove_ibnl_clients();
	ib_sa_cleanup();
	ib_mad_cleanup();
	addr_cleanup();
	ibnl_cleanup();
	class_unregister(&ib_class);
	destroy_workqueue(ib_comp_wq);
	/* Make sure that any pending umem accounting work is done. */
	destroy_workqueue(ib_wq);
}

module_init(ib_core_init);
module_exit(ib_core_cleanup);
