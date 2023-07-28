// SPDX-License-Identifier: GPL-2.0-only
/*
 * VFIO core
 *
 * Copyright (C) 2012 Red Hat, Inc.  All rights reserved.
 *     Author: Alex Williamson <alex.williamson@redhat.com>
 *
 * Derived from original vfio:
 * Copyright 2010 Cisco Systems, Inc.  All rights reserved.
 * Author: Tom Lyon, pugs@cisco.com
 */

#include <linux/cdev.h>
#include <linux/compat.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/iommu.h>
#ifdef CONFIG_HAVE_KVM
#include <linux/kvm_host.h>
#endif
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/vfio.h>
#include <linux/wait.h>
#include <linux/sched/signal.h>
#include <linux/pm_runtime.h>
#include <linux/interval_tree.h>
#include <linux/iova_bitmap.h>
#include <linux/iommufd.h>
#include "vfio.h"

#define DRIVER_VERSION	"0.3"
#define DRIVER_AUTHOR	"Alex Williamson <alex.williamson@redhat.com>"
#define DRIVER_DESC	"VFIO - User Level meta-driver"

static struct vfio {
	struct class			*device_class;
	struct ida			device_ida;
} vfio;

#ifdef CONFIG_VFIO_NOIOMMU
bool vfio_noiommu __read_mostly;
module_param_named(enable_unsafe_noiommu_mode,
		   vfio_noiommu, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(enable_unsafe_noiommu_mode, "Enable UNSAFE, no-IOMMU mode.  This mode provides no device isolation, no DMA translation, no host kernel protection, cannot be used for device assignment to virtual machines, requires RAWIO permissions, and will taint the kernel.  If you do not know what this is for, step away. (default: false)");
#endif

static DEFINE_XARRAY(vfio_device_set_xa);

int vfio_assign_device_set(struct vfio_device *device, void *set_id)
{
	unsigned long idx = (unsigned long)set_id;
	struct vfio_device_set *new_dev_set;
	struct vfio_device_set *dev_set;

	if (WARN_ON(!set_id))
		return -EINVAL;

	/*
	 * Atomically acquire a singleton object in the xarray for this set_id
	 */
	xa_lock(&vfio_device_set_xa);
	dev_set = xa_load(&vfio_device_set_xa, idx);
	if (dev_set)
		goto found_get_ref;
	xa_unlock(&vfio_device_set_xa);

	new_dev_set = kzalloc(sizeof(*new_dev_set), GFP_KERNEL);
	if (!new_dev_set)
		return -ENOMEM;
	mutex_init(&new_dev_set->lock);
	INIT_LIST_HEAD(&new_dev_set->device_list);
	new_dev_set->set_id = set_id;

	xa_lock(&vfio_device_set_xa);
	dev_set = __xa_cmpxchg(&vfio_device_set_xa, idx, NULL, new_dev_set,
			       GFP_KERNEL);
	if (!dev_set) {
		dev_set = new_dev_set;
		goto found_get_ref;
	}

	kfree(new_dev_set);
	if (xa_is_err(dev_set)) {
		xa_unlock(&vfio_device_set_xa);
		return xa_err(dev_set);
	}

found_get_ref:
	dev_set->device_count++;
	xa_unlock(&vfio_device_set_xa);
	mutex_lock(&dev_set->lock);
	device->dev_set = dev_set;
	list_add_tail(&device->dev_set_list, &dev_set->device_list);
	mutex_unlock(&dev_set->lock);
	return 0;
}
EXPORT_SYMBOL_GPL(vfio_assign_device_set);

static void vfio_release_device_set(struct vfio_device *device)
{
	struct vfio_device_set *dev_set = device->dev_set;

	if (!dev_set)
		return;

	mutex_lock(&dev_set->lock);
	list_del(&device->dev_set_list);
	mutex_unlock(&dev_set->lock);

	xa_lock(&vfio_device_set_xa);
	if (!--dev_set->device_count) {
		__xa_erase(&vfio_device_set_xa,
			   (unsigned long)dev_set->set_id);
		mutex_destroy(&dev_set->lock);
		kfree(dev_set);
	}
	xa_unlock(&vfio_device_set_xa);
}

unsigned int vfio_device_set_open_count(struct vfio_device_set *dev_set)
{
	struct vfio_device *cur;
	unsigned int open_count = 0;

	lockdep_assert_held(&dev_set->lock);

	list_for_each_entry(cur, &dev_set->device_list, dev_set_list)
		open_count += cur->open_count;
	return open_count;
}
EXPORT_SYMBOL_GPL(vfio_device_set_open_count);

struct vfio_device *
vfio_find_device_in_devset(struct vfio_device_set *dev_set,
			   struct device *dev)
{
	struct vfio_device *cur;

	lockdep_assert_held(&dev_set->lock);

	list_for_each_entry(cur, &dev_set->device_list, dev_set_list)
		if (cur->dev == dev)
			return cur;
	return NULL;
}
EXPORT_SYMBOL_GPL(vfio_find_device_in_devset);

/*
 * Device objects - create, release, get, put, search
 */
/* Device reference always implies a group reference */
void vfio_device_put_registration(struct vfio_device *device)
{
	if (refcount_dec_and_test(&device->refcount))
		complete(&device->comp);
}

bool vfio_device_try_get_registration(struct vfio_device *device)
{
	return refcount_inc_not_zero(&device->refcount);
}

/*
 * VFIO driver API
 */
/* Release helper called by vfio_put_device() */
static void vfio_device_release(struct device *dev)
{
	struct vfio_device *device =
			container_of(dev, struct vfio_device, device);

	vfio_release_device_set(device);
	ida_free(&vfio.device_ida, device->index);

	if (device->ops->release)
		device->ops->release(device);

	kvfree(device);
}

static int vfio_init_device(struct vfio_device *device, struct device *dev,
			    const struct vfio_device_ops *ops);

/*
 * Allocate and initialize vfio_device so it can be registered to vfio
 * core.
 *
 * Drivers should use the wrapper vfio_alloc_device() for allocation.
 * @size is the size of the structure to be allocated, including any
 * private data used by the driver.
 *
 * Driver may provide an @init callback to cover device private data.
 *
 * Use vfio_put_device() to release the structure after success return.
 */
struct vfio_device *_vfio_alloc_device(size_t size, struct device *dev,
				       const struct vfio_device_ops *ops)
{
	struct vfio_device *device;
	int ret;

	if (WARN_ON(size < sizeof(struct vfio_device)))
		return ERR_PTR(-EINVAL);

	device = kvzalloc(size, GFP_KERNEL);
	if (!device)
		return ERR_PTR(-ENOMEM);

	ret = vfio_init_device(device, dev, ops);
	if (ret)
		goto out_free;
	return device;

out_free:
	kvfree(device);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(_vfio_alloc_device);

/*
 * Initialize a vfio_device so it can be registered to vfio core.
 */
static int vfio_init_device(struct vfio_device *device, struct device *dev,
			    const struct vfio_device_ops *ops)
{
	int ret;

	ret = ida_alloc_max(&vfio.device_ida, MINORMASK, GFP_KERNEL);
	if (ret < 0) {
		dev_dbg(dev, "Error to alloc index\n");
		return ret;
	}

	device->index = ret;
	init_completion(&device->comp);
	device->dev = dev;
	device->ops = ops;

	if (ops->init) {
		ret = ops->init(device);
		if (ret)
			goto out_uninit;
	}

	device_initialize(&device->device);
	device->device.release = vfio_device_release;
	device->device.class = vfio.device_class;
	device->device.parent = device->dev;
	return 0;

out_uninit:
	vfio_release_device_set(device);
	ida_free(&vfio.device_ida, device->index);
	return ret;
}

static int __vfio_register_dev(struct vfio_device *device,
			       enum vfio_group_type type)
{
	int ret;

	if (WARN_ON(IS_ENABLED(CONFIG_IOMMUFD) &&
		    (!device->ops->bind_iommufd ||
		     !device->ops->unbind_iommufd ||
		     !device->ops->attach_ioas ||
		     !device->ops->detach_ioas)))
		return -EINVAL;

	/*
	 * If the driver doesn't specify a set then the device is added to a
	 * singleton set just for itself.
	 */
	if (!device->dev_set)
		vfio_assign_device_set(device, device);

	ret = dev_set_name(&device->device, "vfio%d", device->index);
	if (ret)
		return ret;

	ret = vfio_device_set_group(device, type);
	if (ret)
		return ret;

	/*
	 * VFIO always sets IOMMU_CACHE because we offer no way for userspace to
	 * restore cache coherency. It has to be checked here because it is only
	 * valid for cases where we are using iommu groups.
	 */
	if (type == VFIO_IOMMU && !vfio_device_is_noiommu(device) &&
	    !device_iommu_capable(device->dev, IOMMU_CAP_CACHE_COHERENCY)) {
		ret = -EINVAL;
		goto err_out;
	}

	ret = vfio_device_add(device);
	if (ret)
		goto err_out;

	/* Refcounting can't start until the driver calls register */
	refcount_set(&device->refcount, 1);

	vfio_device_group_register(device);

	return 0;
err_out:
	vfio_device_remove_group(device);
	return ret;
}

int vfio_register_group_dev(struct vfio_device *device)
{
	return __vfio_register_dev(device, VFIO_IOMMU);
}
EXPORT_SYMBOL_GPL(vfio_register_group_dev);

/*
 * Register a virtual device without IOMMU backing.  The user of this
 * device must not be able to directly trigger unmediated DMA.
 */
int vfio_register_emulated_iommu_dev(struct vfio_device *device)
{
	return __vfio_register_dev(device, VFIO_EMULATED_IOMMU);
}
EXPORT_SYMBOL_GPL(vfio_register_emulated_iommu_dev);

/*
 * Decrement the device reference count and wait for the device to be
 * removed.  Open file descriptors for the device... */
void vfio_unregister_group_dev(struct vfio_device *device)
{
	unsigned int i = 0;
	bool interrupted = false;
	long rc;

	/*
	 * Prevent new device opened by userspace via the
	 * VFIO_GROUP_GET_DEVICE_FD in the group path.
	 */
	vfio_device_group_unregister(device);

	/*
	 * Balances vfio_device_add() in register path, also prevents
	 * new device opened by userspace in the cdev path.
	 */
	vfio_device_del(device);

	vfio_device_put_registration(device);
	rc = try_wait_for_completion(&device->comp);
	while (rc <= 0) {
		if (device->ops->request)
			device->ops->request(device, i++);

		if (interrupted) {
			rc = wait_for_completion_timeout(&device->comp,
							 HZ * 10);
		} else {
			rc = wait_for_completion_interruptible_timeout(
				&device->comp, HZ * 10);
			if (rc < 0) {
				interrupted = true;
				dev_warn(device->dev,
					 "Device is currently in use, task"
					 " \"%s\" (%d) "
					 "blocked until device is released",
					 current->comm, task_pid_nr(current));
			}
		}
	}

	/* Balances vfio_device_set_group in register path */
	vfio_device_remove_group(device);
}
EXPORT_SYMBOL_GPL(vfio_unregister_group_dev);

#ifdef CONFIG_HAVE_KVM
void vfio_device_get_kvm_safe(struct vfio_device *device, struct kvm *kvm)
{
	void (*pfn)(struct kvm *kvm);
	bool (*fn)(struct kvm *kvm);
	bool ret;

	lockdep_assert_held(&device->dev_set->lock);

	if (!kvm)
		return;

	pfn = symbol_get(kvm_put_kvm);
	if (WARN_ON(!pfn))
		return;

	fn = symbol_get(kvm_get_kvm_safe);
	if (WARN_ON(!fn)) {
		symbol_put(kvm_put_kvm);
		return;
	}

	ret = fn(kvm);
	symbol_put(kvm_get_kvm_safe);
	if (!ret) {
		symbol_put(kvm_put_kvm);
		return;
	}

	device->put_kvm = pfn;
	device->kvm = kvm;
}

void vfio_device_put_kvm(struct vfio_device *device)
{
	lockdep_assert_held(&device->dev_set->lock);

	if (!device->kvm)
		return;

	if (WARN_ON(!device->put_kvm))
		goto clear;

	device->put_kvm(device->kvm);
	device->put_kvm = NULL;
	symbol_put(kvm_put_kvm);

clear:
	device->kvm = NULL;
}
#endif

/* true if the vfio_device has open_device() called but not close_device() */
static bool vfio_assert_device_open(struct vfio_device *device)
{
	return !WARN_ON_ONCE(!READ_ONCE(device->open_count));
}

struct vfio_device_file *
vfio_allocate_device_file(struct vfio_device *device)
{
	struct vfio_device_file *df;

	df = kzalloc(sizeof(*df), GFP_KERNEL_ACCOUNT);
	if (!df)
		return ERR_PTR(-ENOMEM);

	df->device = device;
	spin_lock_init(&df->kvm_ref_lock);

	return df;
}

static int vfio_df_device_first_open(struct vfio_device_file *df)
{
	struct vfio_device *device = df->device;
	struct iommufd_ctx *iommufd = df->iommufd;
	int ret;

	lockdep_assert_held(&device->dev_set->lock);

	if (!try_module_get(device->dev->driver->owner))
		return -ENODEV;

	if (iommufd)
		ret = vfio_df_iommufd_bind(df);
	else
		ret = vfio_device_group_use_iommu(device);
	if (ret)
		goto err_module_put;

	if (device->ops->open_device) {
		ret = device->ops->open_device(device);
		if (ret)
			goto err_unuse_iommu;
	}
	return 0;

err_unuse_iommu:
	if (iommufd)
		vfio_df_iommufd_unbind(df);
	else
		vfio_device_group_unuse_iommu(device);
err_module_put:
	module_put(device->dev->driver->owner);
	return ret;
}

static void vfio_df_device_last_close(struct vfio_device_file *df)
{
	struct vfio_device *device = df->device;
	struct iommufd_ctx *iommufd = df->iommufd;

	lockdep_assert_held(&device->dev_set->lock);

	if (device->ops->close_device)
		device->ops->close_device(device);
	if (iommufd)
		vfio_df_iommufd_unbind(df);
	else
		vfio_device_group_unuse_iommu(device);
	module_put(device->dev->driver->owner);
}

int vfio_df_open(struct vfio_device_file *df)
{
	struct vfio_device *device = df->device;
	int ret = 0;

	lockdep_assert_held(&device->dev_set->lock);

	/*
	 * Only the group path allows the device to be opened multiple
	 * times.  The device cdev path doesn't have a secure way for it.
	 */
	if (device->open_count != 0 && !df->group)
		return -EINVAL;

	device->open_count++;
	if (device->open_count == 1) {
		ret = vfio_df_device_first_open(df);
		if (ret)
			device->open_count--;
	}

	return ret;
}

void vfio_df_close(struct vfio_device_file *df)
{
	struct vfio_device *device = df->device;

	lockdep_assert_held(&device->dev_set->lock);

	vfio_assert_device_open(device);
	if (device->open_count == 1)
		vfio_df_device_last_close(df);
	device->open_count--;
}

/*
 * Wrapper around pm_runtime_resume_and_get().
 * Return error code on failure or 0 on success.
 */
static inline int vfio_device_pm_runtime_get(struct vfio_device *device)
{
	struct device *dev = device->dev;

	if (dev->driver && dev->driver->pm) {
		int ret;

		ret = pm_runtime_resume_and_get(dev);
		if (ret) {
			dev_info_ratelimited(dev,
				"vfio: runtime resume failed %d\n", ret);
			return -EIO;
		}
	}

	return 0;
}

/*
 * Wrapper around pm_runtime_put().
 */
static inline void vfio_device_pm_runtime_put(struct vfio_device *device)
{
	struct device *dev = device->dev;

	if (dev->driver && dev->driver->pm)
		pm_runtime_put(dev);
}

/*
 * VFIO Device fd
 */
static int vfio_device_fops_release(struct inode *inode, struct file *filep)
{
	struct vfio_device_file *df = filep->private_data;
	struct vfio_device *device = df->device;

	if (df->group)
		vfio_df_group_close(df);
	else
		vfio_df_unbind_iommufd(df);

	vfio_device_put_registration(device);

	kfree(df);

	return 0;
}

/*
 * vfio_mig_get_next_state - Compute the next step in the FSM
 * @cur_fsm - The current state the device is in
 * @new_fsm - The target state to reach
 * @next_fsm - Pointer to the next step to get to new_fsm
 *
 * Return 0 upon success, otherwise -errno
 * Upon success the next step in the state progression between cur_fsm and
 * new_fsm will be set in next_fsm.
 *
 * This breaks down requests for combination transitions into smaller steps and
 * returns the next step to get to new_fsm. The function may need to be called
 * multiple times before reaching new_fsm.
 *
 */
int vfio_mig_get_next_state(struct vfio_device *device,
			    enum vfio_device_mig_state cur_fsm,
			    enum vfio_device_mig_state new_fsm,
			    enum vfio_device_mig_state *next_fsm)
{
	enum { VFIO_DEVICE_NUM_STATES = VFIO_DEVICE_STATE_PRE_COPY_P2P + 1 };
	/*
	 * The coding in this table requires the driver to implement the
	 * following FSM arcs:
	 *         RESUMING -> STOP
	 *         STOP -> RESUMING
	 *         STOP -> STOP_COPY
	 *         STOP_COPY -> STOP
	 *
	 * If P2P is supported then the driver must also implement these FSM
	 * arcs:
	 *         RUNNING -> RUNNING_P2P
	 *         RUNNING_P2P -> RUNNING
	 *         RUNNING_P2P -> STOP
	 *         STOP -> RUNNING_P2P
	 *
	 * If precopy is supported then the driver must support these additional
	 * FSM arcs:
	 *         RUNNING -> PRE_COPY
	 *         PRE_COPY -> RUNNING
	 *         PRE_COPY -> STOP_COPY
	 * However, if precopy and P2P are supported together then the driver
	 * must support these additional arcs beyond the P2P arcs above:
	 *         PRE_COPY -> RUNNING
	 *         PRE_COPY -> PRE_COPY_P2P
	 *         PRE_COPY_P2P -> PRE_COPY
	 *         PRE_COPY_P2P -> RUNNING_P2P
	 *         PRE_COPY_P2P -> STOP_COPY
	 *         RUNNING -> PRE_COPY
	 *         RUNNING_P2P -> PRE_COPY_P2P
	 *
	 * Without P2P and precopy the driver must implement:
	 *         RUNNING -> STOP
	 *         STOP -> RUNNING
	 *
	 * The coding will step through multiple states for some combination
	 * transitions; if all optional features are supported, this means the
	 * following ones:
	 *         PRE_COPY -> PRE_COPY_P2P -> STOP_COPY
	 *         PRE_COPY -> RUNNING -> RUNNING_P2P
	 *         PRE_COPY -> RUNNING -> RUNNING_P2P -> STOP
	 *         PRE_COPY -> RUNNING -> RUNNING_P2P -> STOP -> RESUMING
	 *         PRE_COPY_P2P -> RUNNING_P2P -> RUNNING
	 *         PRE_COPY_P2P -> RUNNING_P2P -> STOP
	 *         PRE_COPY_P2P -> RUNNING_P2P -> STOP -> RESUMING
	 *         RESUMING -> STOP -> RUNNING_P2P
	 *         RESUMING -> STOP -> RUNNING_P2P -> PRE_COPY_P2P
	 *         RESUMING -> STOP -> RUNNING_P2P -> RUNNING
	 *         RESUMING -> STOP -> RUNNING_P2P -> RUNNING -> PRE_COPY
	 *         RESUMING -> STOP -> STOP_COPY
	 *         RUNNING -> RUNNING_P2P -> PRE_COPY_P2P
	 *         RUNNING -> RUNNING_P2P -> STOP
	 *         RUNNING -> RUNNING_P2P -> STOP -> RESUMING
	 *         RUNNING -> RUNNING_P2P -> STOP -> STOP_COPY
	 *         RUNNING_P2P -> RUNNING -> PRE_COPY
	 *         RUNNING_P2P -> STOP -> RESUMING
	 *         RUNNING_P2P -> STOP -> STOP_COPY
	 *         STOP -> RUNNING_P2P -> PRE_COPY_P2P
	 *         STOP -> RUNNING_P2P -> RUNNING
	 *         STOP -> RUNNING_P2P -> RUNNING -> PRE_COPY
	 *         STOP_COPY -> STOP -> RESUMING
	 *         STOP_COPY -> STOP -> RUNNING_P2P
	 *         STOP_COPY -> STOP -> RUNNING_P2P -> RUNNING
	 *
	 *  The following transitions are blocked:
	 *         STOP_COPY -> PRE_COPY
	 *         STOP_COPY -> PRE_COPY_P2P
	 */
	static const u8 vfio_from_fsm_table[VFIO_DEVICE_NUM_STATES][VFIO_DEVICE_NUM_STATES] = {
		[VFIO_DEVICE_STATE_STOP] = {
			[VFIO_DEVICE_STATE_STOP] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_RUNNING] = VFIO_DEVICE_STATE_RUNNING_P2P,
			[VFIO_DEVICE_STATE_PRE_COPY] = VFIO_DEVICE_STATE_RUNNING_P2P,
			[VFIO_DEVICE_STATE_PRE_COPY_P2P] = VFIO_DEVICE_STATE_RUNNING_P2P,
			[VFIO_DEVICE_STATE_STOP_COPY] = VFIO_DEVICE_STATE_STOP_COPY,
			[VFIO_DEVICE_STATE_RESUMING] = VFIO_DEVICE_STATE_RESUMING,
			[VFIO_DEVICE_STATE_RUNNING_P2P] = VFIO_DEVICE_STATE_RUNNING_P2P,
			[VFIO_DEVICE_STATE_ERROR] = VFIO_DEVICE_STATE_ERROR,
		},
		[VFIO_DEVICE_STATE_RUNNING] = {
			[VFIO_DEVICE_STATE_STOP] = VFIO_DEVICE_STATE_RUNNING_P2P,
			[VFIO_DEVICE_STATE_RUNNING] = VFIO_DEVICE_STATE_RUNNING,
			[VFIO_DEVICE_STATE_PRE_COPY] = VFIO_DEVICE_STATE_PRE_COPY,
			[VFIO_DEVICE_STATE_PRE_COPY_P2P] = VFIO_DEVICE_STATE_RUNNING_P2P,
			[VFIO_DEVICE_STATE_STOP_COPY] = VFIO_DEVICE_STATE_RUNNING_P2P,
			[VFIO_DEVICE_STATE_RESUMING] = VFIO_DEVICE_STATE_RUNNING_P2P,
			[VFIO_DEVICE_STATE_RUNNING_P2P] = VFIO_DEVICE_STATE_RUNNING_P2P,
			[VFIO_DEVICE_STATE_ERROR] = VFIO_DEVICE_STATE_ERROR,
		},
		[VFIO_DEVICE_STATE_PRE_COPY] = {
			[VFIO_DEVICE_STATE_STOP] = VFIO_DEVICE_STATE_RUNNING,
			[VFIO_DEVICE_STATE_RUNNING] = VFIO_DEVICE_STATE_RUNNING,
			[VFIO_DEVICE_STATE_PRE_COPY] = VFIO_DEVICE_STATE_PRE_COPY,
			[VFIO_DEVICE_STATE_PRE_COPY_P2P] = VFIO_DEVICE_STATE_PRE_COPY_P2P,
			[VFIO_DEVICE_STATE_STOP_COPY] = VFIO_DEVICE_STATE_PRE_COPY_P2P,
			[VFIO_DEVICE_STATE_RESUMING] = VFIO_DEVICE_STATE_RUNNING,
			[VFIO_DEVICE_STATE_RUNNING_P2P] = VFIO_DEVICE_STATE_RUNNING,
			[VFIO_DEVICE_STATE_ERROR] = VFIO_DEVICE_STATE_ERROR,
		},
		[VFIO_DEVICE_STATE_PRE_COPY_P2P] = {
			[VFIO_DEVICE_STATE_STOP] = VFIO_DEVICE_STATE_RUNNING_P2P,
			[VFIO_DEVICE_STATE_RUNNING] = VFIO_DEVICE_STATE_RUNNING_P2P,
			[VFIO_DEVICE_STATE_PRE_COPY] = VFIO_DEVICE_STATE_PRE_COPY,
			[VFIO_DEVICE_STATE_PRE_COPY_P2P] = VFIO_DEVICE_STATE_PRE_COPY_P2P,
			[VFIO_DEVICE_STATE_STOP_COPY] = VFIO_DEVICE_STATE_STOP_COPY,
			[VFIO_DEVICE_STATE_RESUMING] = VFIO_DEVICE_STATE_RUNNING_P2P,
			[VFIO_DEVICE_STATE_RUNNING_P2P] = VFIO_DEVICE_STATE_RUNNING_P2P,
			[VFIO_DEVICE_STATE_ERROR] = VFIO_DEVICE_STATE_ERROR,
		},
		[VFIO_DEVICE_STATE_STOP_COPY] = {
			[VFIO_DEVICE_STATE_STOP] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_RUNNING] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_PRE_COPY] = VFIO_DEVICE_STATE_ERROR,
			[VFIO_DEVICE_STATE_PRE_COPY_P2P] = VFIO_DEVICE_STATE_ERROR,
			[VFIO_DEVICE_STATE_STOP_COPY] = VFIO_DEVICE_STATE_STOP_COPY,
			[VFIO_DEVICE_STATE_RESUMING] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_RUNNING_P2P] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_ERROR] = VFIO_DEVICE_STATE_ERROR,
		},
		[VFIO_DEVICE_STATE_RESUMING] = {
			[VFIO_DEVICE_STATE_STOP] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_RUNNING] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_PRE_COPY] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_PRE_COPY_P2P] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_STOP_COPY] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_RESUMING] = VFIO_DEVICE_STATE_RESUMING,
			[VFIO_DEVICE_STATE_RUNNING_P2P] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_ERROR] = VFIO_DEVICE_STATE_ERROR,
		},
		[VFIO_DEVICE_STATE_RUNNING_P2P] = {
			[VFIO_DEVICE_STATE_STOP] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_RUNNING] = VFIO_DEVICE_STATE_RUNNING,
			[VFIO_DEVICE_STATE_PRE_COPY] = VFIO_DEVICE_STATE_RUNNING,
			[VFIO_DEVICE_STATE_PRE_COPY_P2P] = VFIO_DEVICE_STATE_PRE_COPY_P2P,
			[VFIO_DEVICE_STATE_STOP_COPY] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_RESUMING] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_RUNNING_P2P] = VFIO_DEVICE_STATE_RUNNING_P2P,
			[VFIO_DEVICE_STATE_ERROR] = VFIO_DEVICE_STATE_ERROR,
		},
		[VFIO_DEVICE_STATE_ERROR] = {
			[VFIO_DEVICE_STATE_STOP] = VFIO_DEVICE_STATE_ERROR,
			[VFIO_DEVICE_STATE_RUNNING] = VFIO_DEVICE_STATE_ERROR,
			[VFIO_DEVICE_STATE_PRE_COPY] = VFIO_DEVICE_STATE_ERROR,
			[VFIO_DEVICE_STATE_PRE_COPY_P2P] = VFIO_DEVICE_STATE_ERROR,
			[VFIO_DEVICE_STATE_STOP_COPY] = VFIO_DEVICE_STATE_ERROR,
			[VFIO_DEVICE_STATE_RESUMING] = VFIO_DEVICE_STATE_ERROR,
			[VFIO_DEVICE_STATE_RUNNING_P2P] = VFIO_DEVICE_STATE_ERROR,
			[VFIO_DEVICE_STATE_ERROR] = VFIO_DEVICE_STATE_ERROR,
		},
	};

	static const unsigned int state_flags_table[VFIO_DEVICE_NUM_STATES] = {
		[VFIO_DEVICE_STATE_STOP] = VFIO_MIGRATION_STOP_COPY,
		[VFIO_DEVICE_STATE_RUNNING] = VFIO_MIGRATION_STOP_COPY,
		[VFIO_DEVICE_STATE_PRE_COPY] =
			VFIO_MIGRATION_STOP_COPY | VFIO_MIGRATION_PRE_COPY,
		[VFIO_DEVICE_STATE_PRE_COPY_P2P] = VFIO_MIGRATION_STOP_COPY |
						   VFIO_MIGRATION_P2P |
						   VFIO_MIGRATION_PRE_COPY,
		[VFIO_DEVICE_STATE_STOP_COPY] = VFIO_MIGRATION_STOP_COPY,
		[VFIO_DEVICE_STATE_RESUMING] = VFIO_MIGRATION_STOP_COPY,
		[VFIO_DEVICE_STATE_RUNNING_P2P] =
			VFIO_MIGRATION_STOP_COPY | VFIO_MIGRATION_P2P,
		[VFIO_DEVICE_STATE_ERROR] = ~0U,
	};

	if (WARN_ON(cur_fsm >= ARRAY_SIZE(vfio_from_fsm_table) ||
		    (state_flags_table[cur_fsm] & device->migration_flags) !=
			state_flags_table[cur_fsm]))
		return -EINVAL;

	if (new_fsm >= ARRAY_SIZE(vfio_from_fsm_table) ||
	   (state_flags_table[new_fsm] & device->migration_flags) !=
			state_flags_table[new_fsm])
		return -EINVAL;

	/*
	 * Arcs touching optional and unsupported states are skipped over. The
	 * driver will instead see an arc from the original state to the next
	 * logical state, as per the above comment.
	 */
	*next_fsm = vfio_from_fsm_table[cur_fsm][new_fsm];
	while ((state_flags_table[*next_fsm] & device->migration_flags) !=
			state_flags_table[*next_fsm])
		*next_fsm = vfio_from_fsm_table[*next_fsm][new_fsm];

	return (*next_fsm != VFIO_DEVICE_STATE_ERROR) ? 0 : -EINVAL;
}
EXPORT_SYMBOL_GPL(vfio_mig_get_next_state);

/*
 * Convert the drivers's struct file into a FD number and return it to userspace
 */
static int vfio_ioct_mig_return_fd(struct file *filp, void __user *arg,
				   struct vfio_device_feature_mig_state *mig)
{
	int ret;
	int fd;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		ret = fd;
		goto out_fput;
	}

	mig->data_fd = fd;
	if (copy_to_user(arg, mig, sizeof(*mig))) {
		ret = -EFAULT;
		goto out_put_unused;
	}
	fd_install(fd, filp);
	return 0;

out_put_unused:
	put_unused_fd(fd);
out_fput:
	fput(filp);
	return ret;
}

static int
vfio_ioctl_device_feature_mig_device_state(struct vfio_device *device,
					   u32 flags, void __user *arg,
					   size_t argsz)
{
	size_t minsz =
		offsetofend(struct vfio_device_feature_mig_state, data_fd);
	struct vfio_device_feature_mig_state mig;
	struct file *filp = NULL;
	int ret;

	if (!device->mig_ops)
		return -ENOTTY;

	ret = vfio_check_feature(flags, argsz,
				 VFIO_DEVICE_FEATURE_SET |
				 VFIO_DEVICE_FEATURE_GET,
				 sizeof(mig));
	if (ret != 1)
		return ret;

	if (copy_from_user(&mig, arg, minsz))
		return -EFAULT;

	if (flags & VFIO_DEVICE_FEATURE_GET) {
		enum vfio_device_mig_state curr_state;

		ret = device->mig_ops->migration_get_state(device,
							   &curr_state);
		if (ret)
			return ret;
		mig.device_state = curr_state;
		goto out_copy;
	}

	/* Handle the VFIO_DEVICE_FEATURE_SET */
	filp = device->mig_ops->migration_set_state(device, mig.device_state);
	if (IS_ERR(filp) || !filp)
		goto out_copy;

	return vfio_ioct_mig_return_fd(filp, arg, &mig);
out_copy:
	mig.data_fd = -1;
	if (copy_to_user(arg, &mig, sizeof(mig)))
		return -EFAULT;
	if (IS_ERR(filp))
		return PTR_ERR(filp);
	return 0;
}

static int
vfio_ioctl_device_feature_migration_data_size(struct vfio_device *device,
					      u32 flags, void __user *arg,
					      size_t argsz)
{
	struct vfio_device_feature_mig_data_size data_size = {};
	unsigned long stop_copy_length;
	int ret;

	if (!device->mig_ops)
		return -ENOTTY;

	ret = vfio_check_feature(flags, argsz, VFIO_DEVICE_FEATURE_GET,
				 sizeof(data_size));
	if (ret != 1)
		return ret;

	ret = device->mig_ops->migration_get_data_size(device, &stop_copy_length);
	if (ret)
		return ret;

	data_size.stop_copy_length = stop_copy_length;
	if (copy_to_user(arg, &data_size, sizeof(data_size)))
		return -EFAULT;

	return 0;
}

static int vfio_ioctl_device_feature_migration(struct vfio_device *device,
					       u32 flags, void __user *arg,
					       size_t argsz)
{
	struct vfio_device_feature_migration mig = {
		.flags = device->migration_flags,
	};
	int ret;

	if (!device->mig_ops)
		return -ENOTTY;

	ret = vfio_check_feature(flags, argsz, VFIO_DEVICE_FEATURE_GET,
				 sizeof(mig));
	if (ret != 1)
		return ret;
	if (copy_to_user(arg, &mig, sizeof(mig)))
		return -EFAULT;
	return 0;
}

/* Ranges should fit into a single kernel page */
#define LOG_MAX_RANGES \
	(PAGE_SIZE / sizeof(struct vfio_device_feature_dma_logging_range))

static int
vfio_ioctl_device_feature_logging_start(struct vfio_device *device,
					u32 flags, void __user *arg,
					size_t argsz)
{
	size_t minsz =
		offsetofend(struct vfio_device_feature_dma_logging_control,
			    ranges);
	struct vfio_device_feature_dma_logging_range __user *ranges;
	struct vfio_device_feature_dma_logging_control control;
	struct vfio_device_feature_dma_logging_range range;
	struct rb_root_cached root = RB_ROOT_CACHED;
	struct interval_tree_node *nodes;
	u64 iova_end;
	u32 nnodes;
	int i, ret;

	if (!device->log_ops)
		return -ENOTTY;

	ret = vfio_check_feature(flags, argsz,
				 VFIO_DEVICE_FEATURE_SET,
				 sizeof(control));
	if (ret != 1)
		return ret;

	if (copy_from_user(&control, arg, minsz))
		return -EFAULT;

	nnodes = control.num_ranges;
	if (!nnodes)
		return -EINVAL;

	if (nnodes > LOG_MAX_RANGES)
		return -E2BIG;

	ranges = u64_to_user_ptr(control.ranges);
	nodes = kmalloc_array(nnodes, sizeof(struct interval_tree_node),
			      GFP_KERNEL);
	if (!nodes)
		return -ENOMEM;

	for (i = 0; i < nnodes; i++) {
		if (copy_from_user(&range, &ranges[i], sizeof(range))) {
			ret = -EFAULT;
			goto end;
		}
		if (!IS_ALIGNED(range.iova, control.page_size) ||
		    !IS_ALIGNED(range.length, control.page_size)) {
			ret = -EINVAL;
			goto end;
		}

		if (check_add_overflow(range.iova, range.length, &iova_end) ||
		    iova_end > ULONG_MAX) {
			ret = -EOVERFLOW;
			goto end;
		}

		nodes[i].start = range.iova;
		nodes[i].last = range.iova + range.length - 1;
		if (interval_tree_iter_first(&root, nodes[i].start,
					     nodes[i].last)) {
			/* Range overlapping */
			ret = -EINVAL;
			goto end;
		}
		interval_tree_insert(nodes + i, &root);
	}

	ret = device->log_ops->log_start(device, &root, nnodes,
					 &control.page_size);
	if (ret)
		goto end;

	if (copy_to_user(arg, &control, sizeof(control))) {
		ret = -EFAULT;
		device->log_ops->log_stop(device);
	}

end:
	kfree(nodes);
	return ret;
}

static int
vfio_ioctl_device_feature_logging_stop(struct vfio_device *device,
				       u32 flags, void __user *arg,
				       size_t argsz)
{
	int ret;

	if (!device->log_ops)
		return -ENOTTY;

	ret = vfio_check_feature(flags, argsz,
				 VFIO_DEVICE_FEATURE_SET, 0);
	if (ret != 1)
		return ret;

	return device->log_ops->log_stop(device);
}

static int vfio_device_log_read_and_clear(struct iova_bitmap *iter,
					  unsigned long iova, size_t length,
					  void *opaque)
{
	struct vfio_device *device = opaque;

	return device->log_ops->log_read_and_clear(device, iova, length, iter);
}

static int
vfio_ioctl_device_feature_logging_report(struct vfio_device *device,
					 u32 flags, void __user *arg,
					 size_t argsz)
{
	size_t minsz =
		offsetofend(struct vfio_device_feature_dma_logging_report,
			    bitmap);
	struct vfio_device_feature_dma_logging_report report;
	struct iova_bitmap *iter;
	u64 iova_end;
	int ret;

	if (!device->log_ops)
		return -ENOTTY;

	ret = vfio_check_feature(flags, argsz,
				 VFIO_DEVICE_FEATURE_GET,
				 sizeof(report));
	if (ret != 1)
		return ret;

	if (copy_from_user(&report, arg, minsz))
		return -EFAULT;

	if (report.page_size < SZ_4K || !is_power_of_2(report.page_size))
		return -EINVAL;

	if (check_add_overflow(report.iova, report.length, &iova_end) ||
	    iova_end > ULONG_MAX)
		return -EOVERFLOW;

	iter = iova_bitmap_alloc(report.iova, report.length,
				 report.page_size,
				 u64_to_user_ptr(report.bitmap));
	if (IS_ERR(iter))
		return PTR_ERR(iter);

	ret = iova_bitmap_for_each(iter, device,
				   vfio_device_log_read_and_clear);

	iova_bitmap_free(iter);
	return ret;
}

static int vfio_ioctl_device_feature(struct vfio_device *device,
				     struct vfio_device_feature __user *arg)
{
	size_t minsz = offsetofend(struct vfio_device_feature, flags);
	struct vfio_device_feature feature;

	if (copy_from_user(&feature, arg, minsz))
		return -EFAULT;

	if (feature.argsz < minsz)
		return -EINVAL;

	/* Check unknown flags */
	if (feature.flags &
	    ~(VFIO_DEVICE_FEATURE_MASK | VFIO_DEVICE_FEATURE_SET |
	      VFIO_DEVICE_FEATURE_GET | VFIO_DEVICE_FEATURE_PROBE))
		return -EINVAL;

	/* GET & SET are mutually exclusive except with PROBE */
	if (!(feature.flags & VFIO_DEVICE_FEATURE_PROBE) &&
	    (feature.flags & VFIO_DEVICE_FEATURE_SET) &&
	    (feature.flags & VFIO_DEVICE_FEATURE_GET))
		return -EINVAL;

	switch (feature.flags & VFIO_DEVICE_FEATURE_MASK) {
	case VFIO_DEVICE_FEATURE_MIGRATION:
		return vfio_ioctl_device_feature_migration(
			device, feature.flags, arg->data,
			feature.argsz - minsz);
	case VFIO_DEVICE_FEATURE_MIG_DEVICE_STATE:
		return vfio_ioctl_device_feature_mig_device_state(
			device, feature.flags, arg->data,
			feature.argsz - minsz);
	case VFIO_DEVICE_FEATURE_DMA_LOGGING_START:
		return vfio_ioctl_device_feature_logging_start(
			device, feature.flags, arg->data,
			feature.argsz - minsz);
	case VFIO_DEVICE_FEATURE_DMA_LOGGING_STOP:
		return vfio_ioctl_device_feature_logging_stop(
			device, feature.flags, arg->data,
			feature.argsz - minsz);
	case VFIO_DEVICE_FEATURE_DMA_LOGGING_REPORT:
		return vfio_ioctl_device_feature_logging_report(
			device, feature.flags, arg->data,
			feature.argsz - minsz);
	case VFIO_DEVICE_FEATURE_MIG_DATA_SIZE:
		return vfio_ioctl_device_feature_migration_data_size(
			device, feature.flags, arg->data,
			feature.argsz - minsz);
	default:
		if (unlikely(!device->ops->device_feature))
			return -EINVAL;
		return device->ops->device_feature(device, feature.flags,
						   arg->data,
						   feature.argsz - minsz);
	}
}

static long vfio_device_fops_unl_ioctl(struct file *filep,
				       unsigned int cmd, unsigned long arg)
{
	struct vfio_device_file *df = filep->private_data;
	struct vfio_device *device = df->device;
	void __user *uptr = (void __user *)arg;
	int ret;

	if (cmd == VFIO_DEVICE_BIND_IOMMUFD)
		return vfio_df_ioctl_bind_iommufd(df, uptr);

	/* Paired with smp_store_release() following vfio_df_open() */
	if (!smp_load_acquire(&df->access_granted))
		return -EINVAL;

	ret = vfio_device_pm_runtime_get(device);
	if (ret)
		return ret;

	/* cdev only ioctls */
	if (IS_ENABLED(CONFIG_VFIO_DEVICE_CDEV) && !df->group) {
		switch (cmd) {
		case VFIO_DEVICE_ATTACH_IOMMUFD_PT:
			ret = vfio_df_ioctl_attach_pt(df, uptr);
			goto out;

		case VFIO_DEVICE_DETACH_IOMMUFD_PT:
			ret = vfio_df_ioctl_detach_pt(df, uptr);
			goto out;
		}
	}

	switch (cmd) {
	case VFIO_DEVICE_FEATURE:
		ret = vfio_ioctl_device_feature(device, uptr);
		break;

	default:
		if (unlikely(!device->ops->ioctl))
			ret = -EINVAL;
		else
			ret = device->ops->ioctl(device, cmd, arg);
		break;
	}
out:
	vfio_device_pm_runtime_put(device);
	return ret;
}

static ssize_t vfio_device_fops_read(struct file *filep, char __user *buf,
				     size_t count, loff_t *ppos)
{
	struct vfio_device_file *df = filep->private_data;
	struct vfio_device *device = df->device;

	/* Paired with smp_store_release() following vfio_df_open() */
	if (!smp_load_acquire(&df->access_granted))
		return -EINVAL;

	if (unlikely(!device->ops->read))
		return -EINVAL;

	return device->ops->read(device, buf, count, ppos);
}

static ssize_t vfio_device_fops_write(struct file *filep,
				      const char __user *buf,
				      size_t count, loff_t *ppos)
{
	struct vfio_device_file *df = filep->private_data;
	struct vfio_device *device = df->device;

	/* Paired with smp_store_release() following vfio_df_open() */
	if (!smp_load_acquire(&df->access_granted))
		return -EINVAL;

	if (unlikely(!device->ops->write))
		return -EINVAL;

	return device->ops->write(device, buf, count, ppos);
}

static int vfio_device_fops_mmap(struct file *filep, struct vm_area_struct *vma)
{
	struct vfio_device_file *df = filep->private_data;
	struct vfio_device *device = df->device;

	/* Paired with smp_store_release() following vfio_df_open() */
	if (!smp_load_acquire(&df->access_granted))
		return -EINVAL;

	if (unlikely(!device->ops->mmap))
		return -EINVAL;

	return device->ops->mmap(device, vma);
}

const struct file_operations vfio_device_fops = {
	.owner		= THIS_MODULE,
	.open		= vfio_device_fops_cdev_open,
	.release	= vfio_device_fops_release,
	.read		= vfio_device_fops_read,
	.write		= vfio_device_fops_write,
	.unlocked_ioctl	= vfio_device_fops_unl_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.mmap		= vfio_device_fops_mmap,
};

static struct vfio_device *vfio_device_from_file(struct file *file)
{
	struct vfio_device_file *df = file->private_data;

	if (file->f_op != &vfio_device_fops)
		return NULL;
	return df->device;
}

/**
 * vfio_file_is_valid - True if the file is valid vfio file
 * @file: VFIO group file or VFIO device file
 */
bool vfio_file_is_valid(struct file *file)
{
	return vfio_group_from_file(file) ||
	       vfio_device_from_file(file);
}
EXPORT_SYMBOL_GPL(vfio_file_is_valid);

/**
 * vfio_file_enforced_coherent - True if the DMA associated with the VFIO file
 *        is always CPU cache coherent
 * @file: VFIO group file or VFIO device file
 *
 * Enforced coherency means that the IOMMU ignores things like the PCIe no-snoop
 * bit in DMA transactions. A return of false indicates that the user has
 * rights to access additional instructions such as wbinvd on x86.
 */
bool vfio_file_enforced_coherent(struct file *file)
{
	struct vfio_device *device;
	struct vfio_group *group;

	group = vfio_group_from_file(file);
	if (group)
		return vfio_group_enforced_coherent(group);

	device = vfio_device_from_file(file);
	if (device)
		return device_iommu_capable(device->dev,
					    IOMMU_CAP_ENFORCE_CACHE_COHERENCY);

	return true;
}
EXPORT_SYMBOL_GPL(vfio_file_enforced_coherent);

static void vfio_device_file_set_kvm(struct file *file, struct kvm *kvm)
{
	struct vfio_device_file *df = file->private_data;

	/*
	 * The kvm is first recorded in the vfio_device_file, and will
	 * be propagated to vfio_device::kvm when the file is bound to
	 * iommufd successfully in the vfio device cdev path.
	 */
	spin_lock(&df->kvm_ref_lock);
	df->kvm = kvm;
	spin_unlock(&df->kvm_ref_lock);
}

/**
 * vfio_file_set_kvm - Link a kvm with VFIO drivers
 * @file: VFIO group file or VFIO device file
 * @kvm: KVM to link
 *
 * When a VFIO device is first opened the KVM will be available in
 * device->kvm if one was associated with the file.
 */
void vfio_file_set_kvm(struct file *file, struct kvm *kvm)
{
	struct vfio_group *group;

	group = vfio_group_from_file(file);
	if (group)
		vfio_group_set_kvm(group, kvm);

	if (vfio_device_from_file(file))
		vfio_device_file_set_kvm(file, kvm);
}
EXPORT_SYMBOL_GPL(vfio_file_set_kvm);

/*
 * Sub-module support
 */
/*
 * Helper for managing a buffer of info chain capabilities, allocate or
 * reallocate a buffer with additional @size, filling in @id and @version
 * of the capability.  A pointer to the new capability is returned.
 *
 * NB. The chain is based at the head of the buffer, so new entries are
 * added to the tail, vfio_info_cap_shift() should be called to fixup the
 * next offsets prior to copying to the user buffer.
 */
struct vfio_info_cap_header *vfio_info_cap_add(struct vfio_info_cap *caps,
					       size_t size, u16 id, u16 version)
{
	void *buf;
	struct vfio_info_cap_header *header, *tmp;

	buf = krealloc(caps->buf, caps->size + size, GFP_KERNEL);
	if (!buf) {
		kfree(caps->buf);
		caps->buf = NULL;
		caps->size = 0;
		return ERR_PTR(-ENOMEM);
	}

	caps->buf = buf;
	header = buf + caps->size;

	/* Eventually copied to user buffer, zero */
	memset(header, 0, size);

	header->id = id;
	header->version = version;

	/* Add to the end of the capability chain */
	for (tmp = buf; tmp->next; tmp = buf + tmp->next)
		; /* nothing */

	tmp->next = caps->size;
	caps->size += size;

	return header;
}
EXPORT_SYMBOL_GPL(vfio_info_cap_add);

void vfio_info_cap_shift(struct vfio_info_cap *caps, size_t offset)
{
	struct vfio_info_cap_header *tmp;
	void *buf = (void *)caps->buf;

	for (tmp = buf; tmp->next; tmp = buf + tmp->next - offset)
		tmp->next += offset;
}
EXPORT_SYMBOL(vfio_info_cap_shift);

int vfio_info_add_capability(struct vfio_info_cap *caps,
			     struct vfio_info_cap_header *cap, size_t size)
{
	struct vfio_info_cap_header *header;

	header = vfio_info_cap_add(caps, size, cap->id, cap->version);
	if (IS_ERR(header))
		return PTR_ERR(header);

	memcpy(header + 1, cap + 1, size - sizeof(*header));

	return 0;
}
EXPORT_SYMBOL(vfio_info_add_capability);

int vfio_set_irqs_validate_and_prepare(struct vfio_irq_set *hdr, int num_irqs,
				       int max_irq_type, size_t *data_size)
{
	unsigned long minsz;
	size_t size;

	minsz = offsetofend(struct vfio_irq_set, count);

	if ((hdr->argsz < minsz) || (hdr->index >= max_irq_type) ||
	    (hdr->count >= (U32_MAX - hdr->start)) ||
	    (hdr->flags & ~(VFIO_IRQ_SET_DATA_TYPE_MASK |
				VFIO_IRQ_SET_ACTION_TYPE_MASK)))
		return -EINVAL;

	if (data_size)
		*data_size = 0;

	if (hdr->start >= num_irqs || hdr->start + hdr->count > num_irqs)
		return -EINVAL;

	switch (hdr->flags & VFIO_IRQ_SET_DATA_TYPE_MASK) {
	case VFIO_IRQ_SET_DATA_NONE:
		size = 0;
		break;
	case VFIO_IRQ_SET_DATA_BOOL:
		size = sizeof(uint8_t);
		break;
	case VFIO_IRQ_SET_DATA_EVENTFD:
		size = sizeof(int32_t);
		break;
	default:
		return -EINVAL;
	}

	if (size) {
		if (hdr->argsz - minsz < hdr->count * size)
			return -EINVAL;

		if (!data_size)
			return -EINVAL;

		*data_size = hdr->count * size;
	}

	return 0;
}
EXPORT_SYMBOL(vfio_set_irqs_validate_and_prepare);

/*
 * Pin contiguous user pages and return their associated host pages for local
 * domain only.
 * @device [in]  : device
 * @iova [in]    : starting IOVA of user pages to be pinned.
 * @npage [in]   : count of pages to be pinned.  This count should not
 *		   be greater than VFIO_PIN_PAGES_MAX_ENTRIES.
 * @prot [in]    : protection flags
 * @pages[out]   : array of host pages
 * Return error or number of pages pinned.
 *
 * A driver may only call this function if the vfio_device was created
 * by vfio_register_emulated_iommu_dev() due to vfio_device_container_pin_pages().
 */
int vfio_pin_pages(struct vfio_device *device, dma_addr_t iova,
		   int npage, int prot, struct page **pages)
{
	/* group->container cannot change while a vfio device is open */
	if (!pages || !npage || WARN_ON(!vfio_assert_device_open(device)))
		return -EINVAL;
	if (!device->ops->dma_unmap)
		return -EINVAL;
	if (vfio_device_has_container(device))
		return vfio_device_container_pin_pages(device, iova,
						       npage, prot, pages);
	if (device->iommufd_access) {
		int ret;

		if (iova > ULONG_MAX)
			return -EINVAL;
		/*
		 * VFIO ignores the sub page offset, npages is from the start of
		 * a PAGE_SIZE chunk of IOVA. The caller is expected to recover
		 * the sub page offset by doing:
		 *     pages[0] + (iova % PAGE_SIZE)
		 */
		ret = iommufd_access_pin_pages(
			device->iommufd_access, ALIGN_DOWN(iova, PAGE_SIZE),
			npage * PAGE_SIZE, pages,
			(prot & IOMMU_WRITE) ? IOMMUFD_ACCESS_RW_WRITE : 0);
		if (ret)
			return ret;
		return npage;
	}
	return -EINVAL;
}
EXPORT_SYMBOL(vfio_pin_pages);

/*
 * Unpin contiguous host pages for local domain only.
 * @device [in]  : device
 * @iova [in]    : starting address of user pages to be unpinned.
 * @npage [in]   : count of pages to be unpinned.  This count should not
 *                 be greater than VFIO_PIN_PAGES_MAX_ENTRIES.
 */
void vfio_unpin_pages(struct vfio_device *device, dma_addr_t iova, int npage)
{
	if (WARN_ON(!vfio_assert_device_open(device)))
		return;
	if (WARN_ON(!device->ops->dma_unmap))
		return;

	if (vfio_device_has_container(device)) {
		vfio_device_container_unpin_pages(device, iova, npage);
		return;
	}
	if (device->iommufd_access) {
		if (WARN_ON(iova > ULONG_MAX))
			return;
		iommufd_access_unpin_pages(device->iommufd_access,
					   ALIGN_DOWN(iova, PAGE_SIZE),
					   npage * PAGE_SIZE);
		return;
	}
}
EXPORT_SYMBOL(vfio_unpin_pages);

/*
 * This interface allows the CPUs to perform some sort of virtual DMA on
 * behalf of the device.
 *
 * CPUs read/write from/into a range of IOVAs pointing to user space memory
 * into/from a kernel buffer.
 *
 * As the read/write of user space memory is conducted via the CPUs and is
 * not a real device DMA, it is not necessary to pin the user space memory.
 *
 * @device [in]		: VFIO device
 * @iova [in]		: base IOVA of a user space buffer
 * @data [in]		: pointer to kernel buffer
 * @len [in]		: kernel buffer length
 * @write		: indicate read or write
 * Return error code on failure or 0 on success.
 */
int vfio_dma_rw(struct vfio_device *device, dma_addr_t iova, void *data,
		size_t len, bool write)
{
	if (!data || len <= 0 || !vfio_assert_device_open(device))
		return -EINVAL;

	if (vfio_device_has_container(device))
		return vfio_device_container_dma_rw(device, iova,
						    data, len, write);

	if (device->iommufd_access) {
		unsigned int flags = 0;

		if (iova > ULONG_MAX)
			return -EINVAL;

		/* VFIO historically tries to auto-detect a kthread */
		if (!current->mm)
			flags |= IOMMUFD_ACCESS_RW_KTHREAD;
		if (write)
			flags |= IOMMUFD_ACCESS_RW_WRITE;
		return iommufd_access_rw(device->iommufd_access, iova, data,
					 len, flags);
	}
	return -EINVAL;
}
EXPORT_SYMBOL(vfio_dma_rw);

/*
 * Module/class support
 */
static int __init vfio_init(void)
{
	int ret;

	ida_init(&vfio.device_ida);

	ret = vfio_group_init();
	if (ret)
		return ret;

	ret = vfio_virqfd_init();
	if (ret)
		goto err_virqfd;

	/* /sys/class/vfio-dev/vfioX */
	vfio.device_class = class_create("vfio-dev");
	if (IS_ERR(vfio.device_class)) {
		ret = PTR_ERR(vfio.device_class);
		goto err_dev_class;
	}

	ret = vfio_cdev_init(vfio.device_class);
	if (ret)
		goto err_alloc_dev_chrdev;

	pr_info(DRIVER_DESC " version: " DRIVER_VERSION "\n");
	return 0;

err_alloc_dev_chrdev:
	class_destroy(vfio.device_class);
	vfio.device_class = NULL;
err_dev_class:
	vfio_virqfd_exit();
err_virqfd:
	vfio_group_cleanup();
	return ret;
}

static void __exit vfio_cleanup(void)
{
	ida_destroy(&vfio.device_ida);
	vfio_cdev_cleanup();
	class_destroy(vfio.device_class);
	vfio.device_class = NULL;
	vfio_virqfd_exit();
	vfio_group_cleanup();
	xa_destroy(&vfio_device_set_xa);
}

module_init(vfio_init);
module_exit(vfio_cleanup);

MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_SOFTDEP("post: vfio_iommu_type1 vfio_iommu_spapr_tce");
