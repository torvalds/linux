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
#include <linux/netdevice.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <linux/security.h>
#include <linux/notifier.h>
#include <linux/hashtable.h>
#include <rdma/rdma_netlink.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_cache.h>

#include "core_priv.h"
#include "restrack.h"

MODULE_AUTHOR("Roland Dreier");
MODULE_DESCRIPTION("core kernel InfiniBand API");
MODULE_LICENSE("Dual BSD/GPL");

struct workqueue_struct *ib_comp_wq;
struct workqueue_struct *ib_comp_unbound_wq;
struct workqueue_struct *ib_wq;
EXPORT_SYMBOL_GPL(ib_wq);

/*
 * Each of the three rwsem locks (devices, clients, client_data) protects the
 * xarray of the same name. Specifically it allows the caller to assert that
 * the MARK will/will not be changing under the lock, and for devices and
 * clients, that the value in the xarray is still a valid pointer. Change of
 * the MARK is linked to the object state, so holding the lock and testing the
 * MARK also asserts that the contained object is in a certain state.
 *
 * This is used to build a two stage register/unregister flow where objects
 * can continue to be in the xarray even though they are still in progress to
 * register/unregister.
 *
 * The xarray itself provides additional locking, and restartable iteration,
 * which is also relied on.
 *
 * Locks should not be nested, with the exception of client_data, which is
 * allowed to nest under the read side of the other two locks.
 *
 * The devices_rwsem also protects the device name list, any change or
 * assignment of device name must also hold the write side to guarantee unique
 * names.
 */

/*
 * devices contains devices that have had their names assigned. The
 * devices may not be registered. Users that care about the registration
 * status need to call ib_device_try_get() on the device to ensure it is
 * registered, and keep it registered, for the required duration.
 *
 */
static DEFINE_XARRAY_FLAGS(devices, XA_FLAGS_ALLOC);
static DECLARE_RWSEM(devices_rwsem);
#define DEVICE_REGISTERED XA_MARK_1

static LIST_HEAD(client_list);
#define CLIENT_REGISTERED XA_MARK_1
static DEFINE_XARRAY_FLAGS(clients, XA_FLAGS_ALLOC);
static DECLARE_RWSEM(clients_rwsem);

/*
 * If client_data is registered then the corresponding client must also still
 * be registered.
 */
#define CLIENT_DATA_REGISTERED XA_MARK_1

/**
 * struct rdma_dev_net - rdma net namespace metadata for a net
 * @net:	Pointer to owner net namespace
 * @id:		xarray id to identify the net namespace.
 */
struct rdma_dev_net {
	possible_net_t net;
	u32 id;
};

static unsigned int rdma_dev_net_id;

/*
 * A list of net namespaces is maintained in an xarray. This is necessary
 * because we can't get the locking right using the existing net ns list. We
 * would require a init_net callback after the list is updated.
 */
static DEFINE_XARRAY_FLAGS(rdma_nets, XA_FLAGS_ALLOC);
/*
 * rwsem to protect accessing the rdma_nets xarray entries.
 */
static DECLARE_RWSEM(rdma_nets_rwsem);

bool ib_devices_shared_netns = true;
module_param_named(netns_mode, ib_devices_shared_netns, bool, 0444);
MODULE_PARM_DESC(netns_mode,
		 "Share device among net namespaces; default=1 (shared)");
/**
 * rdma_dev_access_netns() - Return whether a rdma device can be accessed
 *			     from a specified net namespace or not.
 * @device:	Pointer to rdma device which needs to be checked
 * @net:	Pointer to net namesapce for which access to be checked
 *
 * rdma_dev_access_netns() - Return whether a rdma device can be accessed
 *			     from a specified net namespace or not. When
 *			     rdma device is in shared mode, it ignores the
 *			     net namespace. When rdma device is exclusive
 *			     to a net namespace, rdma device net namespace is
 *			     checked against the specified one.
 */
bool rdma_dev_access_netns(const struct ib_device *dev, const struct net *net)
{
	return (ib_devices_shared_netns ||
		net_eq(read_pnet(&dev->coredev.rdma_net), net));
}
EXPORT_SYMBOL(rdma_dev_access_netns);

/*
 * xarray has this behavior where it won't iterate over NULL values stored in
 * allocated arrays.  So we need our own iterator to see all values stored in
 * the array. This does the same thing as xa_for_each except that it also
 * returns NULL valued entries if the array is allocating. Simplified to only
 * work on simple xarrays.
 */
static void *xan_find_marked(struct xarray *xa, unsigned long *indexp,
			     xa_mark_t filter)
{
	XA_STATE(xas, xa, *indexp);
	void *entry;

	rcu_read_lock();
	do {
		entry = xas_find_marked(&xas, ULONG_MAX, filter);
		if (xa_is_zero(entry))
			break;
	} while (xas_retry(&xas, entry));
	rcu_read_unlock();

	if (entry) {
		*indexp = xas.xa_index;
		if (xa_is_zero(entry))
			return NULL;
		return entry;
	}
	return XA_ERROR(-ENOENT);
}
#define xan_for_each_marked(xa, index, entry, filter)                          \
	for (index = 0, entry = xan_find_marked(xa, &(index), filter);         \
	     !xa_is_err(entry);                                                \
	     (index)++, entry = xan_find_marked(xa, &(index), filter))

/* RCU hash table mapping netdevice pointers to struct ib_port_data */
static DEFINE_SPINLOCK(ndev_hash_lock);
static DECLARE_HASHTABLE(ndev_hash, 5);

static void free_netdevs(struct ib_device *ib_dev);
static void ib_unregister_work(struct work_struct *work);
static void __ib_unregister_device(struct ib_device *device);
static int ib_security_change(struct notifier_block *nb, unsigned long event,
			      void *lsm_data);
static void ib_policy_change_task(struct work_struct *work);
static DECLARE_WORK(ib_policy_change_work, ib_policy_change_task);

static void __ibdev_printk(const char *level, const struct ib_device *ibdev,
			   struct va_format *vaf)
{
	if (ibdev && ibdev->dev.parent)
		dev_printk_emit(level[1] - '0',
				ibdev->dev.parent,
				"%s %s %s: %pV",
				dev_driver_string(ibdev->dev.parent),
				dev_name(ibdev->dev.parent),
				dev_name(&ibdev->dev),
				vaf);
	else if (ibdev)
		printk("%s%s: %pV",
		       level, dev_name(&ibdev->dev), vaf);
	else
		printk("%s(NULL ib_device): %pV", level, vaf);
}

void ibdev_printk(const char *level, const struct ib_device *ibdev,
		  const char *format, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, format);

	vaf.fmt = format;
	vaf.va = &args;

	__ibdev_printk(level, ibdev, &vaf);

	va_end(args);
}
EXPORT_SYMBOL(ibdev_printk);

#define define_ibdev_printk_level(func, level)                  \
void func(const struct ib_device *ibdev, const char *fmt, ...)  \
{                                                               \
	struct va_format vaf;                                   \
	va_list args;                                           \
								\
	va_start(args, fmt);                                    \
								\
	vaf.fmt = fmt;                                          \
	vaf.va = &args;                                         \
								\
	__ibdev_printk(level, ibdev, &vaf);                     \
								\
	va_end(args);                                           \
}                                                               \
EXPORT_SYMBOL(func);

define_ibdev_printk_level(ibdev_emerg, KERN_EMERG);
define_ibdev_printk_level(ibdev_alert, KERN_ALERT);
define_ibdev_printk_level(ibdev_crit, KERN_CRIT);
define_ibdev_printk_level(ibdev_err, KERN_ERR);
define_ibdev_printk_level(ibdev_warn, KERN_WARNING);
define_ibdev_printk_level(ibdev_notice, KERN_NOTICE);
define_ibdev_printk_level(ibdev_info, KERN_INFO);

static struct notifier_block ibdev_lsm_nb = {
	.notifier_call = ib_security_change,
};

static int rdma_dev_change_netns(struct ib_device *device, struct net *cur_net,
				 struct net *net);

/* Pointer to the RCU head at the start of the ib_port_data array */
struct ib_port_data_rcu {
	struct rcu_head rcu_head;
	struct ib_port_data pdata[];
};

static int ib_device_check_mandatory(struct ib_device *device)
{
#define IB_MANDATORY_FUNC(x) { offsetof(struct ib_device_ops, x), #x }
	static const struct {
		size_t offset;
		char  *name;
	} mandatory_table[] = {
		IB_MANDATORY_FUNC(query_device),
		IB_MANDATORY_FUNC(query_port),
		IB_MANDATORY_FUNC(query_pkey),
		IB_MANDATORY_FUNC(alloc_pd),
		IB_MANDATORY_FUNC(dealloc_pd),
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

	device->kverbs_provider = true;
	for (i = 0; i < ARRAY_SIZE(mandatory_table); ++i) {
		if (!*(void **) ((void *) &device->ops +
				 mandatory_table[i].offset)) {
			device->kverbs_provider = false;
			break;
		}
	}

	return 0;
}

/*
 * Caller must perform ib_device_put() to return the device reference count
 * when ib_device_get_by_index() returns valid device pointer.
 */
struct ib_device *ib_device_get_by_index(const struct net *net, u32 index)
{
	struct ib_device *device;

	down_read(&devices_rwsem);
	device = xa_load(&devices, index);
	if (device) {
		if (!rdma_dev_access_netns(device, net)) {
			device = NULL;
			goto out;
		}

		if (!ib_device_try_get(device))
			device = NULL;
	}
out:
	up_read(&devices_rwsem);
	return device;
}

/**
 * ib_device_put - Release IB device reference
 * @device: device whose reference to be released
 *
 * ib_device_put() releases reference to the IB device to allow it to be
 * unregistered and eventually free.
 */
void ib_device_put(struct ib_device *device)
{
	if (refcount_dec_and_test(&device->refcount))
		complete(&device->unreg_completion);
}
EXPORT_SYMBOL(ib_device_put);

static struct ib_device *__ib_device_get_by_name(const char *name)
{
	struct ib_device *device;
	unsigned long index;

	xa_for_each (&devices, index, device)
		if (!strcmp(name, dev_name(&device->dev)))
			return device;

	return NULL;
}

/**
 * ib_device_get_by_name - Find an IB device by name
 * @name: The name to look for
 * @driver_id: The driver ID that must match (RDMA_DRIVER_UNKNOWN matches all)
 *
 * Find and hold an ib_device by its name. The caller must call
 * ib_device_put() on the returned pointer.
 */
struct ib_device *ib_device_get_by_name(const char *name,
					enum rdma_driver_id driver_id)
{
	struct ib_device *device;

	down_read(&devices_rwsem);
	device = __ib_device_get_by_name(name);
	if (device && driver_id != RDMA_DRIVER_UNKNOWN &&
	    device->driver_id != driver_id)
		device = NULL;

	if (device) {
		if (!ib_device_try_get(device))
			device = NULL;
	}
	up_read(&devices_rwsem);
	return device;
}
EXPORT_SYMBOL(ib_device_get_by_name);

static int rename_compat_devs(struct ib_device *device)
{
	struct ib_core_device *cdev;
	unsigned long index;
	int ret = 0;

	mutex_lock(&device->compat_devs_mutex);
	xa_for_each (&device->compat_devs, index, cdev) {
		ret = device_rename(&cdev->dev, dev_name(&device->dev));
		if (ret) {
			dev_warn(&cdev->dev,
				 "Fail to rename compatdev to new name %s\n",
				 dev_name(&device->dev));
			break;
		}
	}
	mutex_unlock(&device->compat_devs_mutex);
	return ret;
}

int ib_device_rename(struct ib_device *ibdev, const char *name)
{
	int ret;

	down_write(&devices_rwsem);
	if (!strcmp(name, dev_name(&ibdev->dev))) {
		ret = 0;
		goto out;
	}

	if (__ib_device_get_by_name(name)) {
		ret = -EEXIST;
		goto out;
	}

	ret = device_rename(&ibdev->dev, name);
	if (ret)
		goto out;
	strlcpy(ibdev->name, name, IB_DEVICE_NAME_MAX);
	ret = rename_compat_devs(ibdev);
out:
	up_write(&devices_rwsem);
	return ret;
}

static int alloc_name(struct ib_device *ibdev, const char *name)
{
	struct ib_device *device;
	unsigned long index;
	struct ida inuse;
	int rc;
	int i;

	lockdep_assert_held_exclusive(&devices_rwsem);
	ida_init(&inuse);
	xa_for_each (&devices, index, device) {
		char buf[IB_DEVICE_NAME_MAX];

		if (sscanf(dev_name(&device->dev), name, &i) != 1)
			continue;
		if (i < 0 || i >= INT_MAX)
			continue;
		snprintf(buf, sizeof buf, name, i);
		if (strcmp(buf, dev_name(&device->dev)) != 0)
			continue;

		rc = ida_alloc_range(&inuse, i, i, GFP_KERNEL);
		if (rc < 0)
			goto out;
	}

	rc = ida_alloc(&inuse, GFP_KERNEL);
	if (rc < 0)
		goto out;

	rc = dev_set_name(&ibdev->dev, name, rc);
out:
	ida_destroy(&inuse);
	return rc;
}

static void ib_device_release(struct device *device)
{
	struct ib_device *dev = container_of(device, struct ib_device, dev);

	free_netdevs(dev);
	WARN_ON(refcount_read(&dev->refcount));
	ib_cache_release_one(dev);
	ib_security_release_port_pkey_list(dev);
	xa_destroy(&dev->compat_devs);
	xa_destroy(&dev->client_data);
	if (dev->port_data)
		kfree_rcu(container_of(dev->port_data, struct ib_port_data_rcu,
				       pdata[0]),
			  rcu_head);
	kfree_rcu(dev, rcu_head);
}

static int ib_device_uevent(struct device *device,
			    struct kobj_uevent_env *env)
{
	if (add_uevent_var(env, "NAME=%s", dev_name(device)))
		return -ENOMEM;

	/*
	 * It would be nice to pass the node GUID with the event...
	 */

	return 0;
}

static const void *net_namespace(struct device *d)
{
	struct ib_core_device *coredev =
			container_of(d, struct ib_core_device, dev);

	return read_pnet(&coredev->rdma_net);
}

static struct class ib_class = {
	.name    = "infiniband",
	.dev_release = ib_device_release,
	.dev_uevent = ib_device_uevent,
	.ns_type = &net_ns_type_operations,
	.namespace = net_namespace,
};

static void rdma_init_coredev(struct ib_core_device *coredev,
			      struct ib_device *dev, struct net *net)
{
	/* This BUILD_BUG_ON is intended to catch layout change
	 * of union of ib_core_device and device.
	 * dev must be the first element as ib_core and providers
	 * driver uses it. Adding anything in ib_core_device before
	 * device will break this assumption.
	 */
	BUILD_BUG_ON(offsetof(struct ib_device, coredev.dev) !=
		     offsetof(struct ib_device, dev));

	coredev->dev.class = &ib_class;
	coredev->dev.groups = dev->groups;
	device_initialize(&coredev->dev);
	coredev->owner = dev;
	INIT_LIST_HEAD(&coredev->port_list);
	write_pnet(&coredev->rdma_net, net);
}

/**
 * _ib_alloc_device - allocate an IB device struct
 * @size:size of structure to allocate
 *
 * Low-level drivers should use ib_alloc_device() to allocate &struct
 * ib_device.  @size is the size of the structure to be allocated,
 * including any private data used by the low-level driver.
 * ib_dealloc_device() must be used to free structures allocated with
 * ib_alloc_device().
 */
struct ib_device *_ib_alloc_device(size_t size)
{
	struct ib_device *device;

	if (WARN_ON(size < sizeof(struct ib_device)))
		return NULL;

	device = kzalloc(size, GFP_KERNEL);
	if (!device)
		return NULL;

	if (rdma_restrack_init(device)) {
		kfree(device);
		return NULL;
	}

	device->groups[0] = &ib_dev_attr_group;
	rdma_init_coredev(&device->coredev, device, &init_net);

	INIT_LIST_HEAD(&device->event_handler_list);
	spin_lock_init(&device->event_handler_lock);
	mutex_init(&device->unregistration_lock);
	/*
	 * client_data needs to be alloc because we don't want our mark to be
	 * destroyed if the user stores NULL in the client data.
	 */
	xa_init_flags(&device->client_data, XA_FLAGS_ALLOC);
	init_rwsem(&device->client_data_rwsem);
	xa_init_flags(&device->compat_devs, XA_FLAGS_ALLOC);
	mutex_init(&device->compat_devs_mutex);
	init_completion(&device->unreg_completion);
	INIT_WORK(&device->unregistration_work, ib_unregister_work);

	return device;
}
EXPORT_SYMBOL(_ib_alloc_device);

/**
 * ib_dealloc_device - free an IB device struct
 * @device:structure to free
 *
 * Free a structure allocated with ib_alloc_device().
 */
void ib_dealloc_device(struct ib_device *device)
{
	if (device->ops.dealloc_driver)
		device->ops.dealloc_driver(device);

	/*
	 * ib_unregister_driver() requires all devices to remain in the xarray
	 * while their ops are callable. The last op we call is dealloc_driver
	 * above.  This is needed to create a fence on op callbacks prior to
	 * allowing the driver module to unload.
	 */
	down_write(&devices_rwsem);
	if (xa_load(&devices, device->index) == device)
		xa_erase(&devices, device->index);
	up_write(&devices_rwsem);

	/* Expedite releasing netdev references */
	free_netdevs(device);

	WARN_ON(!xa_empty(&device->compat_devs));
	WARN_ON(!xa_empty(&device->client_data));
	WARN_ON(refcount_read(&device->refcount));
	rdma_restrack_clean(device);
	/* Balances with device_initialize */
	put_device(&device->dev);
}
EXPORT_SYMBOL(ib_dealloc_device);

/*
 * add_client_context() and remove_client_context() must be safe against
 * parallel calls on the same device - registration/unregistration of both the
 * device and client can be occurring in parallel.
 *
 * The routines need to be a fence, any caller must not return until the add
 * or remove is fully completed.
 */
static int add_client_context(struct ib_device *device,
			      struct ib_client *client)
{
	int ret = 0;

	if (!device->kverbs_provider && !client->no_kverbs_req)
		return 0;

	down_write(&device->client_data_rwsem);
	/*
	 * Another caller to add_client_context got here first and has already
	 * completely initialized context.
	 */
	if (xa_get_mark(&device->client_data, client->client_id,
		    CLIENT_DATA_REGISTERED))
		goto out;

	ret = xa_err(xa_store(&device->client_data, client->client_id, NULL,
			      GFP_KERNEL));
	if (ret)
		goto out;
	downgrade_write(&device->client_data_rwsem);
	if (client->add)
		client->add(device);

	/* Readers shall not see a client until add has been completed */
	xa_set_mark(&device->client_data, client->client_id,
		    CLIENT_DATA_REGISTERED);
	up_read(&device->client_data_rwsem);
	return 0;

out:
	up_write(&device->client_data_rwsem);
	return ret;
}

static void remove_client_context(struct ib_device *device,
				  unsigned int client_id)
{
	struct ib_client *client;
	void *client_data;

	down_write(&device->client_data_rwsem);
	if (!xa_get_mark(&device->client_data, client_id,
			 CLIENT_DATA_REGISTERED)) {
		up_write(&device->client_data_rwsem);
		return;
	}
	client_data = xa_load(&device->client_data, client_id);
	xa_clear_mark(&device->client_data, client_id, CLIENT_DATA_REGISTERED);
	client = xa_load(&clients, client_id);
	downgrade_write(&device->client_data_rwsem);

	/*
	 * Notice we cannot be holding any exclusive locks when calling the
	 * remove callback as the remove callback can recurse back into any
	 * public functions in this module and thus try for any locks those
	 * functions take.
	 *
	 * For this reason clients and drivers should not call the
	 * unregistration functions will holdling any locks.
	 *
	 * It tempting to drop the client_data_rwsem too, but this is required
	 * to ensure that unregister_client does not return until all clients
	 * are completely unregistered, which is required to avoid module
	 * unloading races.
	 */
	if (client->remove)
		client->remove(device, client_data);

	xa_erase(&device->client_data, client_id);
	up_read(&device->client_data_rwsem);
}

static int alloc_port_data(struct ib_device *device)
{
	struct ib_port_data_rcu *pdata_rcu;
	unsigned int port;

	if (device->port_data)
		return 0;

	/* This can only be called once the physical port range is defined */
	if (WARN_ON(!device->phys_port_cnt))
		return -EINVAL;

	/*
	 * device->port_data is indexed directly by the port number to make
	 * access to this data as efficient as possible.
	 *
	 * Therefore port_data is declared as a 1 based array with potential
	 * empty slots at the beginning.
	 */
	pdata_rcu = kzalloc(struct_size(pdata_rcu, pdata,
					rdma_end_port(device) + 1),
			    GFP_KERNEL);
	if (!pdata_rcu)
		return -ENOMEM;
	/*
	 * The rcu_head is put in front of the port data array and the stored
	 * pointer is adjusted since we never need to see that member until
	 * kfree_rcu.
	 */
	device->port_data = pdata_rcu->pdata;

	rdma_for_each_port (device, port) {
		struct ib_port_data *pdata = &device->port_data[port];

		pdata->ib_dev = device;
		spin_lock_init(&pdata->pkey_list_lock);
		INIT_LIST_HEAD(&pdata->pkey_list);
		spin_lock_init(&pdata->netdev_lock);
		INIT_HLIST_NODE(&pdata->ndev_hash_link);
	}
	return 0;
}

static int verify_immutable(const struct ib_device *dev, u8 port)
{
	return WARN_ON(!rdma_cap_ib_mad(dev, port) &&
			    rdma_max_mad_size(dev, port) != 0);
}

static int setup_port_data(struct ib_device *device)
{
	unsigned int port;
	int ret;

	ret = alloc_port_data(device);
	if (ret)
		return ret;

	rdma_for_each_port (device, port) {
		struct ib_port_data *pdata = &device->port_data[port];

		ret = device->ops.get_port_immutable(device, port,
						     &pdata->immutable);
		if (ret)
			return ret;

		if (verify_immutable(device, port))
			return -EINVAL;
	}
	return 0;
}

void ib_get_device_fw_str(struct ib_device *dev, char *str)
{
	if (dev->ops.get_dev_fw_str)
		dev->ops.get_dev_fw_str(dev, str);
	else
		str[0] = '\0';
}
EXPORT_SYMBOL(ib_get_device_fw_str);

static void ib_policy_change_task(struct work_struct *work)
{
	struct ib_device *dev;
	unsigned long index;

	down_read(&devices_rwsem);
	xa_for_each_marked (&devices, index, dev, DEVICE_REGISTERED) {
		unsigned int i;

		rdma_for_each_port (dev, i) {
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
	up_read(&devices_rwsem);
}

static int ib_security_change(struct notifier_block *nb, unsigned long event,
			      void *lsm_data)
{
	if (event != LSM_POLICY_CHANGE)
		return NOTIFY_DONE;

	schedule_work(&ib_policy_change_work);
	ib_mad_agent_security_change();

	return NOTIFY_OK;
}

static void compatdev_release(struct device *dev)
{
	struct ib_core_device *cdev =
		container_of(dev, struct ib_core_device, dev);

	kfree(cdev);
}

static int add_one_compat_dev(struct ib_device *device,
			      struct rdma_dev_net *rnet)
{
	struct ib_core_device *cdev;
	int ret;

	lockdep_assert_held(&rdma_nets_rwsem);
	if (!ib_devices_shared_netns)
		return 0;

	/*
	 * Create and add compat device in all namespaces other than where it
	 * is currently bound to.
	 */
	if (net_eq(read_pnet(&rnet->net),
		   read_pnet(&device->coredev.rdma_net)))
		return 0;

	/*
	 * The first of init_net() or ib_register_device() to take the
	 * compat_devs_mutex wins and gets to add the device. Others will wait
	 * for completion here.
	 */
	mutex_lock(&device->compat_devs_mutex);
	cdev = xa_load(&device->compat_devs, rnet->id);
	if (cdev) {
		ret = 0;
		goto done;
	}
	ret = xa_reserve(&device->compat_devs, rnet->id, GFP_KERNEL);
	if (ret)
		goto done;

	cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);
	if (!cdev) {
		ret = -ENOMEM;
		goto cdev_err;
	}

	cdev->dev.parent = device->dev.parent;
	rdma_init_coredev(cdev, device, read_pnet(&rnet->net));
	cdev->dev.release = compatdev_release;
	dev_set_name(&cdev->dev, "%s", dev_name(&device->dev));

	ret = device_add(&cdev->dev);
	if (ret)
		goto add_err;
	ret = ib_setup_port_attrs(cdev);
	if (ret)
		goto port_err;

	ret = xa_err(xa_store(&device->compat_devs, rnet->id,
			      cdev, GFP_KERNEL));
	if (ret)
		goto insert_err;

	mutex_unlock(&device->compat_devs_mutex);
	return 0;

insert_err:
	ib_free_port_attrs(cdev);
port_err:
	device_del(&cdev->dev);
add_err:
	put_device(&cdev->dev);
cdev_err:
	xa_release(&device->compat_devs, rnet->id);
done:
	mutex_unlock(&device->compat_devs_mutex);
	return ret;
}

static void remove_one_compat_dev(struct ib_device *device, u32 id)
{
	struct ib_core_device *cdev;

	mutex_lock(&device->compat_devs_mutex);
	cdev = xa_erase(&device->compat_devs, id);
	mutex_unlock(&device->compat_devs_mutex);
	if (cdev) {
		ib_free_port_attrs(cdev);
		device_del(&cdev->dev);
		put_device(&cdev->dev);
	}
}

static void remove_compat_devs(struct ib_device *device)
{
	struct ib_core_device *cdev;
	unsigned long index;

	xa_for_each (&device->compat_devs, index, cdev)
		remove_one_compat_dev(device, index);
}

static int add_compat_devs(struct ib_device *device)
{
	struct rdma_dev_net *rnet;
	unsigned long index;
	int ret = 0;

	lockdep_assert_held(&devices_rwsem);

	down_read(&rdma_nets_rwsem);
	xa_for_each (&rdma_nets, index, rnet) {
		ret = add_one_compat_dev(device, rnet);
		if (ret)
			break;
	}
	up_read(&rdma_nets_rwsem);
	return ret;
}

static void remove_all_compat_devs(void)
{
	struct ib_compat_device *cdev;
	struct ib_device *dev;
	unsigned long index;

	down_read(&devices_rwsem);
	xa_for_each (&devices, index, dev) {
		unsigned long c_index = 0;

		/* Hold nets_rwsem so that any other thread modifying this
		 * system param can sync with this thread.
		 */
		down_read(&rdma_nets_rwsem);
		xa_for_each (&dev->compat_devs, c_index, cdev)
			remove_one_compat_dev(dev, c_index);
		up_read(&rdma_nets_rwsem);
	}
	up_read(&devices_rwsem);
}

static int add_all_compat_devs(void)
{
	struct rdma_dev_net *rnet;
	struct ib_device *dev;
	unsigned long index;
	int ret = 0;

	down_read(&devices_rwsem);
	xa_for_each_marked (&devices, index, dev, DEVICE_REGISTERED) {
		unsigned long net_index = 0;

		/* Hold nets_rwsem so that any other thread modifying this
		 * system param can sync with this thread.
		 */
		down_read(&rdma_nets_rwsem);
		xa_for_each (&rdma_nets, net_index, rnet) {
			ret = add_one_compat_dev(dev, rnet);
			if (ret)
				break;
		}
		up_read(&rdma_nets_rwsem);
	}
	up_read(&devices_rwsem);
	if (ret)
		remove_all_compat_devs();
	return ret;
}

int rdma_compatdev_set(u8 enable)
{
	struct rdma_dev_net *rnet;
	unsigned long index;
	int ret = 0;

	down_write(&rdma_nets_rwsem);
	if (ib_devices_shared_netns == enable) {
		up_write(&rdma_nets_rwsem);
		return 0;
	}

	/* enable/disable of compat devices is not supported
	 * when more than default init_net exists.
	 */
	xa_for_each (&rdma_nets, index, rnet) {
		ret++;
		break;
	}
	if (!ret)
		ib_devices_shared_netns = enable;
	up_write(&rdma_nets_rwsem);
	if (ret)
		return -EBUSY;

	if (enable)
		ret = add_all_compat_devs();
	else
		remove_all_compat_devs();
	return ret;
}

static void rdma_dev_exit_net(struct net *net)
{
	struct rdma_dev_net *rnet = net_generic(net, rdma_dev_net_id);
	struct ib_device *dev;
	unsigned long index;
	int ret;

	down_write(&rdma_nets_rwsem);
	/*
	 * Prevent the ID from being re-used and hide the id from xa_for_each.
	 */
	ret = xa_err(xa_store(&rdma_nets, rnet->id, NULL, GFP_KERNEL));
	WARN_ON(ret);
	up_write(&rdma_nets_rwsem);

	down_read(&devices_rwsem);
	xa_for_each (&devices, index, dev) {
		get_device(&dev->dev);
		/*
		 * Release the devices_rwsem so that pontentially blocking
		 * device_del, doesn't hold the devices_rwsem for too long.
		 */
		up_read(&devices_rwsem);

		remove_one_compat_dev(dev, rnet->id);

		/*
		 * If the real device is in the NS then move it back to init.
		 */
		rdma_dev_change_netns(dev, net, &init_net);

		put_device(&dev->dev);
		down_read(&devices_rwsem);
	}
	up_read(&devices_rwsem);

	xa_erase(&rdma_nets, rnet->id);
}

static __net_init int rdma_dev_init_net(struct net *net)
{
	struct rdma_dev_net *rnet = net_generic(net, rdma_dev_net_id);
	unsigned long index;
	struct ib_device *dev;
	int ret;

	/* No need to create any compat devices in default init_net. */
	if (net_eq(net, &init_net))
		return 0;

	write_pnet(&rnet->net, net);

	ret = xa_alloc(&rdma_nets, &rnet->id, rnet, xa_limit_32b, GFP_KERNEL);
	if (ret)
		return ret;

	down_read(&devices_rwsem);
	xa_for_each_marked (&devices, index, dev, DEVICE_REGISTERED) {
		/* Hold nets_rwsem so that netlink command cannot change
		 * system configuration for device sharing mode.
		 */
		down_read(&rdma_nets_rwsem);
		ret = add_one_compat_dev(dev, rnet);
		up_read(&rdma_nets_rwsem);
		if (ret)
			break;
	}
	up_read(&devices_rwsem);

	if (ret)
		rdma_dev_exit_net(net);

	return ret;
}

/*
 * Assign the unique string device name and the unique device index. This is
 * undone by ib_dealloc_device.
 */
static int assign_name(struct ib_device *device, const char *name)
{
	static u32 last_id;
	int ret;

	down_write(&devices_rwsem);
	/* Assign a unique name to the device */
	if (strchr(name, '%'))
		ret = alloc_name(device, name);
	else
		ret = dev_set_name(&device->dev, name);
	if (ret)
		goto out;

	if (__ib_device_get_by_name(dev_name(&device->dev))) {
		ret = -ENFILE;
		goto out;
	}
	strlcpy(device->name, dev_name(&device->dev), IB_DEVICE_NAME_MAX);

	ret = xa_alloc_cyclic(&devices, &device->index, device, xa_limit_31b,
			&last_id, GFP_KERNEL);
	if (ret > 0)
		ret = 0;

out:
	up_write(&devices_rwsem);
	return ret;
}

static void setup_dma_device(struct ib_device *device)
{
	struct device *parent = device->dev.parent;

	WARN_ON_ONCE(device->dma_device);
	if (device->dev.dma_ops) {
		/*
		 * The caller provided custom DMA operations. Copy the
		 * DMA-related fields that are used by e.g. dma_alloc_coherent()
		 * into device->dev.
		 */
		device->dma_device = &device->dev;
		if (!device->dev.dma_mask) {
			if (parent)
				device->dev.dma_mask = parent->dma_mask;
			else
				WARN_ON_ONCE(true);
		}
		if (!device->dev.coherent_dma_mask) {
			if (parent)
				device->dev.coherent_dma_mask =
					parent->coherent_dma_mask;
			else
				WARN_ON_ONCE(true);
		}
	} else {
		/*
		 * The caller did not provide custom DMA operations. Use the
		 * DMA mapping operations of the parent device.
		 */
		WARN_ON_ONCE(!parent);
		device->dma_device = parent;
	}
	/* Setup default max segment size for all IB devices */
	dma_set_max_seg_size(device->dma_device, SZ_2G);

}

/*
 * setup_device() allocates memory and sets up data that requires calling the
 * device ops, this is the only reason these actions are not done during
 * ib_alloc_device. It is undone by ib_dealloc_device().
 */
static int setup_device(struct ib_device *device)
{
	struct ib_udata uhw = {.outlen = 0, .inlen = 0};
	int ret;

	setup_dma_device(device);

	ret = ib_device_check_mandatory(device);
	if (ret)
		return ret;

	ret = setup_port_data(device);
	if (ret) {
		dev_warn(&device->dev, "Couldn't create per-port data\n");
		return ret;
	}

	memset(&device->attrs, 0, sizeof(device->attrs));
	ret = device->ops.query_device(device, &device->attrs, &uhw);
	if (ret) {
		dev_warn(&device->dev,
			 "Couldn't query the device attributes\n");
		return ret;
	}

	return 0;
}

static void disable_device(struct ib_device *device)
{
	struct ib_client *client;

	WARN_ON(!refcount_read(&device->refcount));

	down_write(&devices_rwsem);
	xa_clear_mark(&devices, device->index, DEVICE_REGISTERED);
	up_write(&devices_rwsem);

	down_read(&clients_rwsem);
	list_for_each_entry_reverse(client, &client_list, list)
		remove_client_context(device, client->client_id);
	up_read(&clients_rwsem);

	/* Pairs with refcount_set in enable_device */
	ib_device_put(device);
	wait_for_completion(&device->unreg_completion);

	/*
	 * compat devices must be removed after device refcount drops to zero.
	 * Otherwise init_net() may add more compatdevs after removing compat
	 * devices and before device is disabled.
	 */
	remove_compat_devs(device);
}

/*
 * An enabled device is visible to all clients and to all the public facing
 * APIs that return a device pointer. This always returns with a new get, even
 * if it fails.
 */
static int enable_device_and_get(struct ib_device *device)
{
	struct ib_client *client;
	unsigned long index;
	int ret = 0;

	/*
	 * One ref belongs to the xa and the other belongs to this
	 * thread. This is needed to guard against parallel unregistration.
	 */
	refcount_set(&device->refcount, 2);
	down_write(&devices_rwsem);
	xa_set_mark(&devices, device->index, DEVICE_REGISTERED);

	/*
	 * By using downgrade_write() we ensure that no other thread can clear
	 * DEVICE_REGISTERED while we are completing the client setup.
	 */
	downgrade_write(&devices_rwsem);

	if (device->ops.enable_driver) {
		ret = device->ops.enable_driver(device);
		if (ret)
			goto out;
	}

	down_read(&clients_rwsem);
	xa_for_each_marked (&clients, index, client, CLIENT_REGISTERED) {
		ret = add_client_context(device, client);
		if (ret)
			break;
	}
	up_read(&clients_rwsem);
	if (!ret)
		ret = add_compat_devs(device);
out:
	up_read(&devices_rwsem);
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
 *
 * If the driver uses ops.dealloc_driver and calls any ib_unregister_device()
 * asynchronously then the device pointer may become freed as soon as this
 * function returns.
 */
int ib_register_device(struct ib_device *device, const char *name)
{
	int ret;

	ret = assign_name(device, name);
	if (ret)
		return ret;

	ret = setup_device(device);
	if (ret)
		return ret;

	ret = ib_cache_setup_one(device);
	if (ret) {
		dev_warn(&device->dev,
			 "Couldn't set up InfiniBand P_Key/GID cache\n");
		return ret;
	}

	ib_device_register_rdmacg(device);

	ret = device_add(&device->dev);
	if (ret)
		goto cg_cleanup;

	ret = ib_device_register_sysfs(device);
	if (ret) {
		dev_warn(&device->dev,
			 "Couldn't register device with driver model\n");
		goto dev_cleanup;
	}

	ret = enable_device_and_get(device);
	if (ret) {
		void (*dealloc_fn)(struct ib_device *);

		/*
		 * If we hit this error flow then we don't want to
		 * automatically dealloc the device since the caller is
		 * expected to call ib_dealloc_device() after
		 * ib_register_device() fails. This is tricky due to the
		 * possibility for a parallel unregistration along with this
		 * error flow. Since we have a refcount here we know any
		 * parallel flow is stopped in disable_device and will see the
		 * NULL pointers, causing the responsibility to
		 * ib_dealloc_device() to revert back to this thread.
		 */
		dealloc_fn = device->ops.dealloc_driver;
		device->ops.dealloc_driver = NULL;
		ib_device_put(device);
		__ib_unregister_device(device);
		device->ops.dealloc_driver = dealloc_fn;
		return ret;
	}
	ib_device_put(device);

	return 0;

dev_cleanup:
	device_del(&device->dev);
cg_cleanup:
	ib_device_unregister_rdmacg(device);
	ib_cache_cleanup_one(device);
	return ret;
}
EXPORT_SYMBOL(ib_register_device);

/* Callers must hold a get on the device. */
static void __ib_unregister_device(struct ib_device *ib_dev)
{
	/*
	 * We have a registration lock so that all the calls to unregister are
	 * fully fenced, once any unregister returns the device is truely
	 * unregistered even if multiple callers are unregistering it at the
	 * same time. This also interacts with the registration flow and
	 * provides sane semantics if register and unregister are racing.
	 */
	mutex_lock(&ib_dev->unregistration_lock);
	if (!refcount_read(&ib_dev->refcount))
		goto out;

	disable_device(ib_dev);

	/* Expedite removing unregistered pointers from the hash table */
	free_netdevs(ib_dev);

	ib_device_unregister_sysfs(ib_dev);
	device_del(&ib_dev->dev);
	ib_device_unregister_rdmacg(ib_dev);
	ib_cache_cleanup_one(ib_dev);

	/*
	 * Drivers using the new flow may not call ib_dealloc_device except
	 * in error unwind prior to registration success.
	 */
	if (ib_dev->ops.dealloc_driver) {
		WARN_ON(kref_read(&ib_dev->dev.kobj.kref) <= 1);
		ib_dealloc_device(ib_dev);
	}
out:
	mutex_unlock(&ib_dev->unregistration_lock);
}

/**
 * ib_unregister_device - Unregister an IB device
 * @device: The device to unregister
 *
 * Unregister an IB device.  All clients will receive a remove callback.
 *
 * Callers should call this routine only once, and protect against races with
 * registration. Typically it should only be called as part of a remove
 * callback in an implementation of driver core's struct device_driver and
 * related.
 *
 * If ops.dealloc_driver is used then ib_dev will be freed upon return from
 * this function.
 */
void ib_unregister_device(struct ib_device *ib_dev)
{
	get_device(&ib_dev->dev);
	__ib_unregister_device(ib_dev);
	put_device(&ib_dev->dev);
}
EXPORT_SYMBOL(ib_unregister_device);

/**
 * ib_unregister_device_and_put - Unregister a device while holding a 'get'
 * device: The device to unregister
 *
 * This is the same as ib_unregister_device(), except it includes an internal
 * ib_device_put() that should match a 'get' obtained by the caller.
 *
 * It is safe to call this routine concurrently from multiple threads while
 * holding the 'get'. When the function returns the device is fully
 * unregistered.
 *
 * Drivers using this flow MUST use the driver_unregister callback to clean up
 * their resources associated with the device and dealloc it.
 */
void ib_unregister_device_and_put(struct ib_device *ib_dev)
{
	WARN_ON(!ib_dev->ops.dealloc_driver);
	get_device(&ib_dev->dev);
	ib_device_put(ib_dev);
	__ib_unregister_device(ib_dev);
	put_device(&ib_dev->dev);
}
EXPORT_SYMBOL(ib_unregister_device_and_put);

/**
 * ib_unregister_driver - Unregister all IB devices for a driver
 * @driver_id: The driver to unregister
 *
 * This implements a fence for device unregistration. It only returns once all
 * devices associated with the driver_id have fully completed their
 * unregistration and returned from ib_unregister_device*().
 *
 * If device's are not yet unregistered it goes ahead and starts unregistering
 * them.
 *
 * This does not block creation of new devices with the given driver_id, that
 * is the responsibility of the caller.
 */
void ib_unregister_driver(enum rdma_driver_id driver_id)
{
	struct ib_device *ib_dev;
	unsigned long index;

	down_read(&devices_rwsem);
	xa_for_each (&devices, index, ib_dev) {
		if (ib_dev->driver_id != driver_id)
			continue;

		get_device(&ib_dev->dev);
		up_read(&devices_rwsem);

		WARN_ON(!ib_dev->ops.dealloc_driver);
		__ib_unregister_device(ib_dev);

		put_device(&ib_dev->dev);
		down_read(&devices_rwsem);
	}
	up_read(&devices_rwsem);
}
EXPORT_SYMBOL(ib_unregister_driver);

static void ib_unregister_work(struct work_struct *work)
{
	struct ib_device *ib_dev =
		container_of(work, struct ib_device, unregistration_work);

	__ib_unregister_device(ib_dev);
	put_device(&ib_dev->dev);
}

/**
 * ib_unregister_device_queued - Unregister a device using a work queue
 * device: The device to unregister
 *
 * This schedules an asynchronous unregistration using a WQ for the device. A
 * driver should use this to avoid holding locks while doing unregistration,
 * such as holding the RTNL lock.
 *
 * Drivers using this API must use ib_unregister_driver before module unload
 * to ensure that all scheduled unregistrations have completed.
 */
void ib_unregister_device_queued(struct ib_device *ib_dev)
{
	WARN_ON(!refcount_read(&ib_dev->refcount));
	WARN_ON(!ib_dev->ops.dealloc_driver);
	get_device(&ib_dev->dev);
	if (!queue_work(system_unbound_wq, &ib_dev->unregistration_work))
		put_device(&ib_dev->dev);
}
EXPORT_SYMBOL(ib_unregister_device_queued);

/*
 * The caller must pass in a device that has the kref held and the refcount
 * released. If the device is in cur_net and still registered then it is moved
 * into net.
 */
static int rdma_dev_change_netns(struct ib_device *device, struct net *cur_net,
				 struct net *net)
{
	int ret2 = -EINVAL;
	int ret;

	mutex_lock(&device->unregistration_lock);

	/*
	 * If a device not under ib_device_get() or if the unregistration_lock
	 * is not held, the namespace can be changed, or it can be unregistered.
	 * Check again under the lock.
	 */
	if (refcount_read(&device->refcount) == 0 ||
	    !net_eq(cur_net, read_pnet(&device->coredev.rdma_net))) {
		ret = -ENODEV;
		goto out;
	}

	kobject_uevent(&device->dev.kobj, KOBJ_REMOVE);
	disable_device(device);

	/*
	 * At this point no one can be using the device, so it is safe to
	 * change the namespace.
	 */
	write_pnet(&device->coredev.rdma_net, net);

	down_read(&devices_rwsem);
	/*
	 * Currently rdma devices are system wide unique. So the device name
	 * is guaranteed free in the new namespace. Publish the new namespace
	 * at the sysfs level.
	 */
	ret = device_rename(&device->dev, dev_name(&device->dev));
	up_read(&devices_rwsem);
	if (ret) {
		dev_warn(&device->dev,
			 "%s: Couldn't rename device after namespace change\n",
			 __func__);
		/* Try and put things back and re-enable the device */
		write_pnet(&device->coredev.rdma_net, cur_net);
	}

	ret2 = enable_device_and_get(device);
	if (ret2) {
		/*
		 * This shouldn't really happen, but if it does, let the user
		 * retry at later point. So don't disable the device.
		 */
		dev_warn(&device->dev,
			 "%s: Couldn't re-enable device after namespace change\n",
			 __func__);
	}
	kobject_uevent(&device->dev.kobj, KOBJ_ADD);

	ib_device_put(device);
out:
	mutex_unlock(&device->unregistration_lock);
	if (ret)
		return ret;
	return ret2;
}

int ib_device_set_netns_put(struct sk_buff *skb,
			    struct ib_device *dev, u32 ns_fd)
{
	struct net *net;
	int ret;

	net = get_net_ns_by_fd(ns_fd);
	if (IS_ERR(net)) {
		ret = PTR_ERR(net);
		goto net_err;
	}

	if (!netlink_ns_capable(skb, net->user_ns, CAP_NET_ADMIN)) {
		ret = -EPERM;
		goto ns_err;
	}

	/*
	 * Currently supported only for those providers which support
	 * disassociation and don't do port specific sysfs init. Once a
	 * port_cleanup infrastructure is implemented, this limitation will be
	 * removed.
	 */
	if (!dev->ops.disassociate_ucontext || dev->ops.init_port ||
	    ib_devices_shared_netns) {
		ret = -EOPNOTSUPP;
		goto ns_err;
	}

	get_device(&dev->dev);
	ib_device_put(dev);
	ret = rdma_dev_change_netns(dev, current->nsproxy->net_ns, net);
	put_device(&dev->dev);

	put_net(net);
	return ret;

ns_err:
	put_net(net);
net_err:
	ib_device_put(dev);
	return ret;
}

static struct pernet_operations rdma_dev_net_ops = {
	.init = rdma_dev_init_net,
	.exit = rdma_dev_exit_net,
	.id = &rdma_dev_net_id,
	.size = sizeof(struct rdma_dev_net),
};

static int assign_client_id(struct ib_client *client)
{
	int ret;

	down_write(&clients_rwsem);
	/*
	 * The add/remove callbacks must be called in FIFO/LIFO order. To
	 * achieve this we assign client_ids so they are sorted in
	 * registration order, and retain a linked list we can reverse iterate
	 * to get the LIFO order. The extra linked list can go away if xarray
	 * learns to reverse iterate.
	 */
	if (list_empty(&client_list)) {
		client->client_id = 0;
	} else {
		struct ib_client *last;

		last = list_last_entry(&client_list, struct ib_client, list);
		client->client_id = last->client_id + 1;
	}
	ret = xa_insert(&clients, client->client_id, client, GFP_KERNEL);
	if (ret)
		goto out;

	xa_set_mark(&clients, client->client_id, CLIENT_REGISTERED);
	list_add_tail(&client->list, &client_list);

out:
	up_write(&clients_rwsem);
	return ret;
}

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
	unsigned long index;
	int ret;

	ret = assign_client_id(client);
	if (ret)
		return ret;

	down_read(&devices_rwsem);
	xa_for_each_marked (&devices, index, device, DEVICE_REGISTERED) {
		ret = add_client_context(device, client);
		if (ret) {
			up_read(&devices_rwsem);
			ib_unregister_client(client);
			return ret;
		}
	}
	up_read(&devices_rwsem);
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
 *
 * This is a full fence, once it returns no client callbacks will be called,
 * or are running in another thread.
 */
void ib_unregister_client(struct ib_client *client)
{
	struct ib_device *device;
	unsigned long index;

	down_write(&clients_rwsem);
	xa_clear_mark(&clients, client->client_id, CLIENT_REGISTERED);
	up_write(&clients_rwsem);
	/*
	 * Every device still known must be serialized to make sure we are
	 * done with the client callbacks before we return.
	 */
	down_read(&devices_rwsem);
	xa_for_each (&devices, index, device)
		remove_client_context(device, client->client_id);
	up_read(&devices_rwsem);

	down_write(&clients_rwsem);
	list_del(&client->list);
	xa_erase(&clients, client->client_id);
	up_write(&clients_rwsem);
}
EXPORT_SYMBOL(ib_unregister_client);

/**
 * ib_set_client_data - Set IB client context
 * @device:Device to set context for
 * @client:Client to set context for
 * @data:Context to set
 *
 * ib_set_client_data() sets client context data that can be retrieved with
 * ib_get_client_data(). This can only be called while the client is
 * registered to the device, once the ib_client remove() callback returns this
 * cannot be called.
 */
void ib_set_client_data(struct ib_device *device, struct ib_client *client,
			void *data)
{
	void *rc;

	if (WARN_ON(IS_ERR(data)))
		data = NULL;

	rc = xa_store(&device->client_data, client->client_id, data,
		      GFP_KERNEL);
	WARN_ON(xa_is_err(rc));
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
void ib_register_event_handler(struct ib_event_handler *event_handler)
{
	unsigned long flags;

	spin_lock_irqsave(&event_handler->device->event_handler_lock, flags);
	list_add_tail(&event_handler->list,
		      &event_handler->device->event_handler_list);
	spin_unlock_irqrestore(&event_handler->device->event_handler_lock, flags);
}
EXPORT_SYMBOL(ib_register_event_handler);

/**
 * ib_unregister_event_handler - Unregister an event handler
 * @event_handler:Handler to unregister
 *
 * Unregister an event handler registered with
 * ib_register_event_handler().
 */
void ib_unregister_event_handler(struct ib_event_handler *event_handler)
{
	unsigned long flags;

	spin_lock_irqsave(&event_handler->device->event_handler_lock, flags);
	list_del(&event_handler->list);
	spin_unlock_irqrestore(&event_handler->device->event_handler_lock, flags);
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
	err = device->ops.query_port(device, port_num, port_attr);
	if (err || port_attr->subnet_prefix)
		return err;

	if (rdma_port_get_link_layer(device, port_num) != IB_LINK_LAYER_INFINIBAND)
		return 0;

	err = device->ops.query_gid(device, port_num, 0, &gid);
	if (err)
		return err;

	port_attr->subnet_prefix = be64_to_cpu(gid.global.subnet_prefix);
	return 0;
}
EXPORT_SYMBOL(ib_query_port);

static void add_ndev_hash(struct ib_port_data *pdata)
{
	unsigned long flags;

	might_sleep();

	spin_lock_irqsave(&ndev_hash_lock, flags);
	if (hash_hashed(&pdata->ndev_hash_link)) {
		hash_del_rcu(&pdata->ndev_hash_link);
		spin_unlock_irqrestore(&ndev_hash_lock, flags);
		/*
		 * We cannot do hash_add_rcu after a hash_del_rcu until the
		 * grace period
		 */
		synchronize_rcu();
		spin_lock_irqsave(&ndev_hash_lock, flags);
	}
	if (pdata->netdev)
		hash_add_rcu(ndev_hash, &pdata->ndev_hash_link,
			     (uintptr_t)pdata->netdev);
	spin_unlock_irqrestore(&ndev_hash_lock, flags);
}

/**
 * ib_device_set_netdev - Associate the ib_dev with an underlying net_device
 * @ib_dev: Device to modify
 * @ndev: net_device to affiliate, may be NULL
 * @port: IB port the net_device is connected to
 *
 * Drivers should use this to link the ib_device to a netdev so the netdev
 * shows up in interfaces like ib_enum_roce_netdev. Only one netdev may be
 * affiliated with any port.
 *
 * The caller must ensure that the given ndev is not unregistered or
 * unregistering, and that either the ib_device is unregistered or
 * ib_device_set_netdev() is called with NULL when the ndev sends a
 * NETDEV_UNREGISTER event.
 */
int ib_device_set_netdev(struct ib_device *ib_dev, struct net_device *ndev,
			 unsigned int port)
{
	struct net_device *old_ndev;
	struct ib_port_data *pdata;
	unsigned long flags;
	int ret;

	/*
	 * Drivers wish to call this before ib_register_driver, so we have to
	 * setup the port data early.
	 */
	ret = alloc_port_data(ib_dev);
	if (ret)
		return ret;

	if (!rdma_is_port_valid(ib_dev, port))
		return -EINVAL;

	pdata = &ib_dev->port_data[port];
	spin_lock_irqsave(&pdata->netdev_lock, flags);
	old_ndev = rcu_dereference_protected(
		pdata->netdev, lockdep_is_held(&pdata->netdev_lock));
	if (old_ndev == ndev) {
		spin_unlock_irqrestore(&pdata->netdev_lock, flags);
		return 0;
	}

	if (ndev)
		dev_hold(ndev);
	rcu_assign_pointer(pdata->netdev, ndev);
	spin_unlock_irqrestore(&pdata->netdev_lock, flags);

	add_ndev_hash(pdata);
	if (old_ndev)
		dev_put(old_ndev);

	return 0;
}
EXPORT_SYMBOL(ib_device_set_netdev);

static void free_netdevs(struct ib_device *ib_dev)
{
	unsigned long flags;
	unsigned int port;

	rdma_for_each_port (ib_dev, port) {
		struct ib_port_data *pdata = &ib_dev->port_data[port];
		struct net_device *ndev;

		spin_lock_irqsave(&pdata->netdev_lock, flags);
		ndev = rcu_dereference_protected(
			pdata->netdev, lockdep_is_held(&pdata->netdev_lock));
		if (ndev) {
			spin_lock(&ndev_hash_lock);
			hash_del_rcu(&pdata->ndev_hash_link);
			spin_unlock(&ndev_hash_lock);

			/*
			 * If this is the last dev_put there is still a
			 * synchronize_rcu before the netdev is kfreed, so we
			 * can continue to rely on unlocked pointer
			 * comparisons after the put
			 */
			rcu_assign_pointer(pdata->netdev, NULL);
			dev_put(ndev);
		}
		spin_unlock_irqrestore(&pdata->netdev_lock, flags);
	}
}

struct net_device *ib_device_get_netdev(struct ib_device *ib_dev,
					unsigned int port)
{
	struct ib_port_data *pdata;
	struct net_device *res;

	if (!rdma_is_port_valid(ib_dev, port))
		return NULL;

	pdata = &ib_dev->port_data[port];

	/*
	 * New drivers should use ib_device_set_netdev() not the legacy
	 * get_netdev().
	 */
	if (ib_dev->ops.get_netdev)
		res = ib_dev->ops.get_netdev(ib_dev, port);
	else {
		spin_lock(&pdata->netdev_lock);
		res = rcu_dereference_protected(
			pdata->netdev, lockdep_is_held(&pdata->netdev_lock));
		if (res)
			dev_hold(res);
		spin_unlock(&pdata->netdev_lock);
	}

	/*
	 * If we are starting to unregister expedite things by preventing
	 * propagation of an unregistering netdev.
	 */
	if (res && res->reg_state != NETREG_REGISTERED) {
		dev_put(res);
		return NULL;
	}

	return res;
}

/**
 * ib_device_get_by_netdev - Find an IB device associated with a netdev
 * @ndev: netdev to locate
 * @driver_id: The driver ID that must match (RDMA_DRIVER_UNKNOWN matches all)
 *
 * Find and hold an ib_device that is associated with a netdev via
 * ib_device_set_netdev(). The caller must call ib_device_put() on the
 * returned pointer.
 */
struct ib_device *ib_device_get_by_netdev(struct net_device *ndev,
					  enum rdma_driver_id driver_id)
{
	struct ib_device *res = NULL;
	struct ib_port_data *cur;

	rcu_read_lock();
	hash_for_each_possible_rcu (ndev_hash, cur, ndev_hash_link,
				    (uintptr_t)ndev) {
		if (rcu_access_pointer(cur->netdev) == ndev &&
		    (driver_id == RDMA_DRIVER_UNKNOWN ||
		     cur->ib_dev->driver_id == driver_id) &&
		    ib_device_try_get(cur->ib_dev)) {
			res = cur->ib_dev;
			break;
		}
	}
	rcu_read_unlock();

	return res;
}
EXPORT_SYMBOL(ib_device_get_by_netdev);

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
	unsigned int port;

	rdma_for_each_port (ib_dev, port)
		if (rdma_protocol_roce(ib_dev, port)) {
			struct net_device *idev =
				ib_device_get_netdev(ib_dev, port);

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
	unsigned long index;

	down_read(&devices_rwsem);
	xa_for_each_marked (&devices, index, dev, DEVICE_REGISTERED)
		ib_enum_roce_netdev(dev, filter, filter_cookie, cb, cookie);
	up_read(&devices_rwsem);
}

/**
 * ib_enum_all_devs - enumerate all ib_devices
 * @cb: Callback to call for each found ib_device
 *
 * Enumerates all ib_devices and calls callback() on each device.
 */
int ib_enum_all_devs(nldev_callback nldev_cb, struct sk_buff *skb,
		     struct netlink_callback *cb)
{
	unsigned long index;
	struct ib_device *dev;
	unsigned int idx = 0;
	int ret = 0;

	down_read(&devices_rwsem);
	xa_for_each_marked (&devices, index, dev, DEVICE_REGISTERED) {
		if (!rdma_dev_access_netns(dev, sock_net(skb->sk)))
			continue;

		ret = nldev_cb(dev, skb, cb, idx);
		if (ret)
			break;
		idx++;
	}
	up_read(&devices_rwsem);
	return ret;
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
	if (!rdma_is_port_valid(device, port_num))
		return -EINVAL;

	return device->ops.query_pkey(device, port_num, index, pkey);
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
	if (!device->ops.modify_device)
		return -ENOSYS;

	return device->ops.modify_device(device, device_modify_mask,
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
	int rc;

	if (!rdma_is_port_valid(device, port_num))
		return -EINVAL;

	if (device->ops.modify_port)
		rc = device->ops.modify_port(device, port_num,
					     port_modify_mask,
					     port_modify);
	else
		rc = rdma_protocol_roce(device, port_num) ? 0 : -ENOSYS;
	return rc;
}
EXPORT_SYMBOL(ib_modify_port);

/**
 * ib_find_gid - Returns the port number and GID table index where
 *   a specified GID value occurs. Its searches only for IB link layer.
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
	unsigned int port;
	int ret, i;

	rdma_for_each_port (device, port) {
		if (!rdma_protocol_ib(device, port))
			continue;

		for (i = 0; i < device->port_data[port].immutable.gid_tbl_len;
		     ++i) {
			ret = rdma_query_gid(device, port, i, &tmp_gid);
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

	for (i = 0; i < device->port_data[port_num].immutable.pkey_tbl_len;
	     ++i) {
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
 *
 */
struct net_device *ib_get_net_dev_by_params(struct ib_device *dev,
					    u8 port,
					    u16 pkey,
					    const union ib_gid *gid,
					    const struct sockaddr *addr)
{
	struct net_device *net_dev = NULL;
	unsigned long index;
	void *client_data;

	if (!rdma_protocol_ib(dev, port))
		return NULL;

	/*
	 * Holding the read side guarantees that the client will not become
	 * unregistered while we are calling get_net_dev_by_params()
	 */
	down_read(&dev->client_data_rwsem);
	xan_for_each_marked (&dev->client_data, index, client_data,
			     CLIENT_DATA_REGISTERED) {
		struct ib_client *client = xa_load(&clients, index);

		if (!client || !client->get_net_dev_by_params)
			continue;

		net_dev = client->get_net_dev_by_params(dev, port, pkey, gid,
							addr, client_data);
		if (net_dev)
			break;
	}
	up_read(&dev->client_data_rwsem);

	return net_dev;
}
EXPORT_SYMBOL(ib_get_net_dev_by_params);

void ib_set_device_ops(struct ib_device *dev, const struct ib_device_ops *ops)
{
	struct ib_device_ops *dev_ops = &dev->ops;
#define SET_DEVICE_OP(ptr, name)                                               \
	do {                                                                   \
		if (ops->name)                                                 \
			if (!((ptr)->name))				       \
				(ptr)->name = ops->name;                       \
	} while (0)

#define SET_OBJ_SIZE(ptr, name) SET_DEVICE_OP(ptr, size_##name)

	SET_DEVICE_OP(dev_ops, add_gid);
	SET_DEVICE_OP(dev_ops, advise_mr);
	SET_DEVICE_OP(dev_ops, alloc_dm);
	SET_DEVICE_OP(dev_ops, alloc_fmr);
	SET_DEVICE_OP(dev_ops, alloc_hw_stats);
	SET_DEVICE_OP(dev_ops, alloc_mr);
	SET_DEVICE_OP(dev_ops, alloc_mw);
	SET_DEVICE_OP(dev_ops, alloc_pd);
	SET_DEVICE_OP(dev_ops, alloc_rdma_netdev);
	SET_DEVICE_OP(dev_ops, alloc_ucontext);
	SET_DEVICE_OP(dev_ops, alloc_xrcd);
	SET_DEVICE_OP(dev_ops, attach_mcast);
	SET_DEVICE_OP(dev_ops, check_mr_status);
	SET_DEVICE_OP(dev_ops, create_ah);
	SET_DEVICE_OP(dev_ops, create_counters);
	SET_DEVICE_OP(dev_ops, create_cq);
	SET_DEVICE_OP(dev_ops, create_flow);
	SET_DEVICE_OP(dev_ops, create_flow_action_esp);
	SET_DEVICE_OP(dev_ops, create_qp);
	SET_DEVICE_OP(dev_ops, create_rwq_ind_table);
	SET_DEVICE_OP(dev_ops, create_srq);
	SET_DEVICE_OP(dev_ops, create_wq);
	SET_DEVICE_OP(dev_ops, dealloc_dm);
	SET_DEVICE_OP(dev_ops, dealloc_driver);
	SET_DEVICE_OP(dev_ops, dealloc_fmr);
	SET_DEVICE_OP(dev_ops, dealloc_mw);
	SET_DEVICE_OP(dev_ops, dealloc_pd);
	SET_DEVICE_OP(dev_ops, dealloc_ucontext);
	SET_DEVICE_OP(dev_ops, dealloc_xrcd);
	SET_DEVICE_OP(dev_ops, del_gid);
	SET_DEVICE_OP(dev_ops, dereg_mr);
	SET_DEVICE_OP(dev_ops, destroy_ah);
	SET_DEVICE_OP(dev_ops, destroy_counters);
	SET_DEVICE_OP(dev_ops, destroy_cq);
	SET_DEVICE_OP(dev_ops, destroy_flow);
	SET_DEVICE_OP(dev_ops, destroy_flow_action);
	SET_DEVICE_OP(dev_ops, destroy_qp);
	SET_DEVICE_OP(dev_ops, destroy_rwq_ind_table);
	SET_DEVICE_OP(dev_ops, destroy_srq);
	SET_DEVICE_OP(dev_ops, destroy_wq);
	SET_DEVICE_OP(dev_ops, detach_mcast);
	SET_DEVICE_OP(dev_ops, disassociate_ucontext);
	SET_DEVICE_OP(dev_ops, drain_rq);
	SET_DEVICE_OP(dev_ops, drain_sq);
	SET_DEVICE_OP(dev_ops, enable_driver);
	SET_DEVICE_OP(dev_ops, fill_res_entry);
	SET_DEVICE_OP(dev_ops, get_dev_fw_str);
	SET_DEVICE_OP(dev_ops, get_dma_mr);
	SET_DEVICE_OP(dev_ops, get_hw_stats);
	SET_DEVICE_OP(dev_ops, get_link_layer);
	SET_DEVICE_OP(dev_ops, get_netdev);
	SET_DEVICE_OP(dev_ops, get_port_immutable);
	SET_DEVICE_OP(dev_ops, get_vector_affinity);
	SET_DEVICE_OP(dev_ops, get_vf_config);
	SET_DEVICE_OP(dev_ops, get_vf_stats);
	SET_DEVICE_OP(dev_ops, init_port);
	SET_DEVICE_OP(dev_ops, iw_accept);
	SET_DEVICE_OP(dev_ops, iw_add_ref);
	SET_DEVICE_OP(dev_ops, iw_connect);
	SET_DEVICE_OP(dev_ops, iw_create_listen);
	SET_DEVICE_OP(dev_ops, iw_destroy_listen);
	SET_DEVICE_OP(dev_ops, iw_get_qp);
	SET_DEVICE_OP(dev_ops, iw_reject);
	SET_DEVICE_OP(dev_ops, iw_rem_ref);
	SET_DEVICE_OP(dev_ops, map_mr_sg);
	SET_DEVICE_OP(dev_ops, map_phys_fmr);
	SET_DEVICE_OP(dev_ops, mmap);
	SET_DEVICE_OP(dev_ops, modify_ah);
	SET_DEVICE_OP(dev_ops, modify_cq);
	SET_DEVICE_OP(dev_ops, modify_device);
	SET_DEVICE_OP(dev_ops, modify_flow_action_esp);
	SET_DEVICE_OP(dev_ops, modify_port);
	SET_DEVICE_OP(dev_ops, modify_qp);
	SET_DEVICE_OP(dev_ops, modify_srq);
	SET_DEVICE_OP(dev_ops, modify_wq);
	SET_DEVICE_OP(dev_ops, peek_cq);
	SET_DEVICE_OP(dev_ops, poll_cq);
	SET_DEVICE_OP(dev_ops, post_recv);
	SET_DEVICE_OP(dev_ops, post_send);
	SET_DEVICE_OP(dev_ops, post_srq_recv);
	SET_DEVICE_OP(dev_ops, process_mad);
	SET_DEVICE_OP(dev_ops, query_ah);
	SET_DEVICE_OP(dev_ops, query_device);
	SET_DEVICE_OP(dev_ops, query_gid);
	SET_DEVICE_OP(dev_ops, query_pkey);
	SET_DEVICE_OP(dev_ops, query_port);
	SET_DEVICE_OP(dev_ops, query_qp);
	SET_DEVICE_OP(dev_ops, query_srq);
	SET_DEVICE_OP(dev_ops, rdma_netdev_get_params);
	SET_DEVICE_OP(dev_ops, read_counters);
	SET_DEVICE_OP(dev_ops, reg_dm_mr);
	SET_DEVICE_OP(dev_ops, reg_user_mr);
	SET_DEVICE_OP(dev_ops, req_ncomp_notif);
	SET_DEVICE_OP(dev_ops, req_notify_cq);
	SET_DEVICE_OP(dev_ops, rereg_user_mr);
	SET_DEVICE_OP(dev_ops, resize_cq);
	SET_DEVICE_OP(dev_ops, set_vf_guid);
	SET_DEVICE_OP(dev_ops, set_vf_link_state);
	SET_DEVICE_OP(dev_ops, unmap_fmr);

	SET_OBJ_SIZE(dev_ops, ib_ah);
	SET_OBJ_SIZE(dev_ops, ib_pd);
	SET_OBJ_SIZE(dev_ops, ib_srq);
	SET_OBJ_SIZE(dev_ops, ib_ucontext);
}
EXPORT_SYMBOL(ib_set_device_ops);

static const struct rdma_nl_cbs ibnl_ls_cb_table[RDMA_NL_LS_NUM_OPS] = {
	[RDMA_NL_LS_OP_RESOLVE] = {
		.doit = ib_nl_handle_resolve_resp,
		.flags = RDMA_NL_ADMIN_PERM,
	},
	[RDMA_NL_LS_OP_SET_TIMEOUT] = {
		.doit = ib_nl_handle_set_timeout,
		.flags = RDMA_NL_ADMIN_PERM,
	},
	[RDMA_NL_LS_OP_IP_RESOLVE] = {
		.doit = ib_nl_handle_ip_res_resp,
		.flags = RDMA_NL_ADMIN_PERM,
	},
};

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

	ib_comp_unbound_wq =
		alloc_workqueue("ib-comp-unb-wq",
				WQ_UNBOUND | WQ_HIGHPRI | WQ_MEM_RECLAIM |
				WQ_SYSFS, WQ_UNBOUND_MAX_ACTIVE);
	if (!ib_comp_unbound_wq) {
		ret = -ENOMEM;
		goto err_comp;
	}

	ret = class_register(&ib_class);
	if (ret) {
		pr_warn("Couldn't create InfiniBand device class\n");
		goto err_comp_unbound;
	}

	ret = rdma_nl_init();
	if (ret) {
		pr_warn("Couldn't init IB netlink interface: err %d\n", ret);
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

	ret = register_lsm_notifier(&ibdev_lsm_nb);
	if (ret) {
		pr_warn("Couldn't register LSM notifier. ret %d\n", ret);
		goto err_sa;
	}

	ret = register_pernet_device(&rdma_dev_net_ops);
	if (ret) {
		pr_warn("Couldn't init compat dev. ret %d\n", ret);
		goto err_compat;
	}

	nldev_init();
	rdma_nl_register(RDMA_NL_LS, ibnl_ls_cb_table);
	roce_gid_mgmt_init();

	return 0;

err_compat:
	unregister_lsm_notifier(&ibdev_lsm_nb);
err_sa:
	ib_sa_cleanup();
err_mad:
	ib_mad_cleanup();
err_addr:
	addr_cleanup();
err_ibnl:
	rdma_nl_exit();
err_sysfs:
	class_unregister(&ib_class);
err_comp_unbound:
	destroy_workqueue(ib_comp_unbound_wq);
err_comp:
	destroy_workqueue(ib_comp_wq);
err:
	destroy_workqueue(ib_wq);
	return ret;
}

static void __exit ib_core_cleanup(void)
{
	roce_gid_mgmt_cleanup();
	nldev_exit();
	rdma_nl_unregister(RDMA_NL_LS);
	unregister_pernet_device(&rdma_dev_net_ops);
	unregister_lsm_notifier(&ibdev_lsm_nb);
	ib_sa_cleanup();
	ib_mad_cleanup();
	addr_cleanup();
	rdma_nl_exit();
	class_unregister(&ib_class);
	destroy_workqueue(ib_comp_unbound_wq);
	destroy_workqueue(ib_comp_wq);
	/* Make sure that any pending umem accounting work is done. */
	destroy_workqueue(ib_wq);
	flush_workqueue(system_unbound_wq);
	WARN_ON(!xa_empty(&clients));
	WARN_ON(!xa_empty(&devices));
}

MODULE_ALIAS_RDMA_NETLINK(RDMA_NL_LS, 4);

/* ib core relies on netdev stack to first register net_ns_type_operations
 * ns kobject type before ib_core initialization.
 */
fs_initcall(ib_core_init);
module_exit(ib_core_cleanup);
