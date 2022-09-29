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
#include <linux/file.h>
#include <linux/anon_inodes.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/iommu.h>
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
#include "vfio.h"

#define DRIVER_VERSION	"0.3"
#define DRIVER_AUTHOR	"Alex Williamson <alex.williamson@redhat.com>"
#define DRIVER_DESC	"VFIO - User Level meta-driver"

static struct vfio {
	struct class			*class;
	struct list_head		group_list;
	struct mutex			group_lock; /* locks group_list */
	struct ida			group_ida;
	dev_t				group_devt;
	struct class			*device_class;
	struct ida			device_ida;
} vfio;

static DEFINE_XARRAY(vfio_device_set_xa);
static const struct file_operations vfio_group_fops;

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

/*
 * Group objects - create, release, get, put, search
 */
static struct vfio_group *
__vfio_group_get_from_iommu(struct iommu_group *iommu_group)
{
	struct vfio_group *group;

	list_for_each_entry(group, &vfio.group_list, vfio_next) {
		if (group->iommu_group == iommu_group) {
			refcount_inc(&group->drivers);
			return group;
		}
	}
	return NULL;
}

static struct vfio_group *
vfio_group_get_from_iommu(struct iommu_group *iommu_group)
{
	struct vfio_group *group;

	mutex_lock(&vfio.group_lock);
	group = __vfio_group_get_from_iommu(iommu_group);
	mutex_unlock(&vfio.group_lock);
	return group;
}

static void vfio_group_release(struct device *dev)
{
	struct vfio_group *group = container_of(dev, struct vfio_group, dev);

	mutex_destroy(&group->device_lock);
	mutex_destroy(&group->group_lock);
	iommu_group_put(group->iommu_group);
	ida_free(&vfio.group_ida, MINOR(group->dev.devt));
	kfree(group);
}

static struct vfio_group *vfio_group_alloc(struct iommu_group *iommu_group,
					   enum vfio_group_type type)
{
	struct vfio_group *group;
	int minor;

	group = kzalloc(sizeof(*group), GFP_KERNEL);
	if (!group)
		return ERR_PTR(-ENOMEM);

	minor = ida_alloc_max(&vfio.group_ida, MINORMASK, GFP_KERNEL);
	if (minor < 0) {
		kfree(group);
		return ERR_PTR(minor);
	}

	device_initialize(&group->dev);
	group->dev.devt = MKDEV(MAJOR(vfio.group_devt), minor);
	group->dev.class = vfio.class;
	group->dev.release = vfio_group_release;
	cdev_init(&group->cdev, &vfio_group_fops);
	group->cdev.owner = THIS_MODULE;

	refcount_set(&group->drivers, 1);
	mutex_init(&group->group_lock);
	init_swait_queue_head(&group->opened_file_wait);
	INIT_LIST_HEAD(&group->device_list);
	mutex_init(&group->device_lock);
	group->iommu_group = iommu_group;
	/* put in vfio_group_release() */
	iommu_group_ref_get(iommu_group);
	group->type = type;
	BLOCKING_INIT_NOTIFIER_HEAD(&group->notifier);

	return group;
}

static struct vfio_group *vfio_create_group(struct iommu_group *iommu_group,
		enum vfio_group_type type)
{
	struct vfio_group *group;
	struct vfio_group *ret;
	int err;

	group = vfio_group_alloc(iommu_group, type);
	if (IS_ERR(group))
		return group;

	err = dev_set_name(&group->dev, "%s%d",
			   group->type == VFIO_NO_IOMMU ? "noiommu-" : "",
			   iommu_group_id(iommu_group));
	if (err) {
		ret = ERR_PTR(err);
		goto err_put;
	}

	mutex_lock(&vfio.group_lock);

	/* Did we race creating this group? */
	ret = __vfio_group_get_from_iommu(iommu_group);
	if (ret)
		goto err_unlock;

	err = cdev_device_add(&group->cdev, &group->dev);
	if (err) {
		ret = ERR_PTR(err);
		goto err_unlock;
	}

	list_add(&group->vfio_next, &vfio.group_list);

	mutex_unlock(&vfio.group_lock);
	return group;

err_unlock:
	mutex_unlock(&vfio.group_lock);
err_put:
	put_device(&group->dev);
	return ret;
}

static void vfio_device_remove_group(struct vfio_device *device)
{
	struct vfio_group *group = device->group;

	if (group->type == VFIO_NO_IOMMU || group->type == VFIO_EMULATED_IOMMU)
		iommu_group_remove_device(device->dev);

	/* Pairs with vfio_create_group() / vfio_group_get_from_iommu() */
	if (!refcount_dec_and_mutex_lock(&group->drivers, &vfio.group_lock))
		return;
	list_del(&group->vfio_next);

	/*
	 * We could concurrently probe another driver in the group that might
	 * race vfio_device_remove_group() with vfio_get_group(), so we have to
	 * ensure that the sysfs is all cleaned up under lock otherwise the
	 * cdev_device_add() will fail due to the name aready existing.
	 */
	cdev_device_del(&group->cdev, &group->dev);

	/*
	 * Before we allow the last driver in the group to be unplugged the
	 * group must be sanitized so nothing else is or can reference it. This
	 * is because the group->iommu_group pointer should only be used so long
	 * as a device driver is attached to a device in the group.
	 */
	while (group->opened_file) {
		mutex_unlock(&vfio.group_lock);
		swait_event_idle_exclusive(group->opened_file_wait,
					   !group->opened_file);
		mutex_lock(&vfio.group_lock);
	}
	mutex_unlock(&vfio.group_lock);

	/*
	 * These data structures all have paired operations that can only be
	 * undone when the caller holds a live reference on the group. Since all
	 * pairs must be undone these WARN_ON's indicate some caller did not
	 * properly hold the group reference.
	 */
	WARN_ON(!list_empty(&group->device_list));
	WARN_ON(group->container || group->container_users);
	WARN_ON(group->notifier.head);
	group->iommu_group = NULL;

	put_device(&group->dev);
}

/*
 * Device objects - create, release, get, put, search
 */
/* Device reference always implies a group reference */
static void vfio_device_put_registration(struct vfio_device *device)
{
	if (refcount_dec_and_test(&device->refcount))
		complete(&device->comp);
}

static bool vfio_device_try_get_registration(struct vfio_device *device)
{
	return refcount_inc_not_zero(&device->refcount);
}

static struct vfio_device *vfio_group_get_device(struct vfio_group *group,
						 struct device *dev)
{
	struct vfio_device *device;

	mutex_lock(&group->device_lock);
	list_for_each_entry(device, &group->device_list, group_next) {
		if (device->dev == dev &&
		    vfio_device_try_get_registration(device)) {
			mutex_unlock(&group->device_lock);
			return device;
		}
	}
	mutex_unlock(&group->device_lock);
	return NULL;
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

	/*
	 * kvfree() cannot be done here due to a life cycle mess in
	 * vfio-ccw. Before the ccw part is fixed all drivers are
	 * required to support @release and call vfio_free_device()
	 * from there.
	 */
	device->ops->release(device);
}

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
 *
 * Only vfio-ccw driver should call this interface.
 */
int vfio_init_device(struct vfio_device *device, struct device *dev,
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
EXPORT_SYMBOL_GPL(vfio_init_device);

/*
 * The helper called by driver @release callback to free the device
 * structure. Drivers which don't have private data to clean can
 * simply use this helper as its @release.
 */
void vfio_free_device(struct vfio_device *device)
{
	kvfree(device);
}
EXPORT_SYMBOL_GPL(vfio_free_device);

static struct vfio_group *vfio_noiommu_group_alloc(struct device *dev,
		enum vfio_group_type type)
{
	struct iommu_group *iommu_group;
	struct vfio_group *group;
	int ret;

	iommu_group = iommu_group_alloc();
	if (IS_ERR(iommu_group))
		return ERR_CAST(iommu_group);

	ret = iommu_group_set_name(iommu_group, "vfio-noiommu");
	if (ret)
		goto out_put_group;
	ret = iommu_group_add_device(iommu_group, dev);
	if (ret)
		goto out_put_group;

	group = vfio_create_group(iommu_group, type);
	if (IS_ERR(group)) {
		ret = PTR_ERR(group);
		goto out_remove_device;
	}
	iommu_group_put(iommu_group);
	return group;

out_remove_device:
	iommu_group_remove_device(dev);
out_put_group:
	iommu_group_put(iommu_group);
	return ERR_PTR(ret);
}

static struct vfio_group *vfio_group_find_or_alloc(struct device *dev)
{
	struct iommu_group *iommu_group;
	struct vfio_group *group;

	iommu_group = iommu_group_get(dev);
	if (!iommu_group && vfio_noiommu) {
		/*
		 * With noiommu enabled, create an IOMMU group for devices that
		 * don't already have one, implying no IOMMU hardware/driver
		 * exists.  Taint the kernel because we're about to give a DMA
		 * capable device to a user without IOMMU protection.
		 */
		group = vfio_noiommu_group_alloc(dev, VFIO_NO_IOMMU);
		if (!IS_ERR(group)) {
			add_taint(TAINT_USER, LOCKDEP_STILL_OK);
			dev_warn(dev, "Adding kernel taint for vfio-noiommu group on device\n");
		}
		return group;
	}

	if (!iommu_group)
		return ERR_PTR(-EINVAL);

	/*
	 * VFIO always sets IOMMU_CACHE because we offer no way for userspace to
	 * restore cache coherency. It has to be checked here because it is only
	 * valid for cases where we are using iommu groups.
	 */
	if (!device_iommu_capable(dev, IOMMU_CAP_CACHE_COHERENCY)) {
		iommu_group_put(iommu_group);
		return ERR_PTR(-EINVAL);
	}

	group = vfio_group_get_from_iommu(iommu_group);
	if (!group)
		group = vfio_create_group(iommu_group, VFIO_IOMMU);

	/* The vfio_group holds a reference to the iommu_group */
	iommu_group_put(iommu_group);
	return group;
}

static int __vfio_register_dev(struct vfio_device *device,
		struct vfio_group *group)
{
	struct vfio_device *existing_device;
	int ret;

	/*
	 * In all cases group is the output of one of the group allocation
	 * functions and we have group->drivers incremented for us.
	 */
	if (IS_ERR(group))
		return PTR_ERR(group);

	/*
	 * If the driver doesn't specify a set then the device is added to a
	 * singleton set just for itself.
	 */
	if (!device->dev_set)
		vfio_assign_device_set(device, device);

	existing_device = vfio_group_get_device(group, device->dev);
	if (existing_device) {
		dev_WARN(device->dev, "Device already exists on group %d\n",
			 iommu_group_id(group->iommu_group));
		vfio_device_put_registration(existing_device);
		ret = -EBUSY;
		goto err_out;
	}

	/* Our reference on group is moved to the device */
	device->group = group;

	ret = dev_set_name(&device->device, "vfio%d", device->index);
	if (ret)
		goto err_out;

	ret = device_add(&device->device);
	if (ret)
		goto err_out;

	/* Refcounting can't start until the driver calls register */
	refcount_set(&device->refcount, 1);

	mutex_lock(&group->device_lock);
	list_add(&device->group_next, &group->device_list);
	mutex_unlock(&group->device_lock);

	return 0;
err_out:
	vfio_device_remove_group(device);
	return ret;
}

int vfio_register_group_dev(struct vfio_device *device)
{
	return __vfio_register_dev(device,
		vfio_group_find_or_alloc(device->dev));
}
EXPORT_SYMBOL_GPL(vfio_register_group_dev);

/*
 * Register a virtual device without IOMMU backing.  The user of this
 * device must not be able to directly trigger unmediated DMA.
 */
int vfio_register_emulated_iommu_dev(struct vfio_device *device)
{
	return __vfio_register_dev(device,
		vfio_noiommu_group_alloc(device->dev, VFIO_EMULATED_IOMMU));
}
EXPORT_SYMBOL_GPL(vfio_register_emulated_iommu_dev);

static struct vfio_device *vfio_device_get_from_name(struct vfio_group *group,
						     char *buf)
{
	struct vfio_device *it, *device = ERR_PTR(-ENODEV);

	mutex_lock(&group->device_lock);
	list_for_each_entry(it, &group->device_list, group_next) {
		int ret;

		if (it->ops->match) {
			ret = it->ops->match(it, buf);
			if (ret < 0) {
				device = ERR_PTR(ret);
				break;
			}
		} else {
			ret = !strcmp(dev_name(it->dev), buf);
		}

		if (ret && vfio_device_try_get_registration(it)) {
			device = it;
			break;
		}
	}
	mutex_unlock(&group->device_lock);

	return device;
}

/*
 * Decrement the device reference count and wait for the device to be
 * removed.  Open file descriptors for the device... */
void vfio_unregister_group_dev(struct vfio_device *device)
{
	struct vfio_group *group = device->group;
	unsigned int i = 0;
	bool interrupted = false;
	long rc;

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

	mutex_lock(&group->device_lock);
	list_del(&device->group_next);
	mutex_unlock(&group->device_lock);

	/* Balances device_add in register path */
	device_del(&device->device);

	vfio_device_remove_group(device);
}
EXPORT_SYMBOL_GPL(vfio_unregister_group_dev);

/*
 * VFIO Group fd, /dev/vfio/$GROUP
 */
/*
 * VFIO_GROUP_UNSET_CONTAINER should fail if there are other users or
 * if there was no container to unset.  Since the ioctl is called on
 * the group, we know that still exists, therefore the only valid
 * transition here is 1->0.
 */
static int vfio_group_ioctl_unset_container(struct vfio_group *group)
{
	int ret = 0;

	mutex_lock(&group->group_lock);
	if (!group->container) {
		ret = -EINVAL;
		goto out_unlock;
	}
	if (group->container_users != 1) {
		ret = -EBUSY;
		goto out_unlock;
	}
	vfio_group_detach_container(group);

out_unlock:
	mutex_unlock(&group->group_lock);
	return ret;
}

static int vfio_group_ioctl_set_container(struct vfio_group *group,
					  int __user *arg)
{
	struct vfio_container *container;
	struct fd f;
	int ret;
	int fd;

	if (get_user(fd, arg))
		return -EFAULT;

	f = fdget(fd);
	if (!f.file)
		return -EBADF;

	mutex_lock(&group->group_lock);
	if (group->container || WARN_ON(group->container_users)) {
		ret = -EINVAL;
		goto out_unlock;
	}
	container = vfio_container_from_file(f.file);
	ret = -EINVAL;
	if (container) {
		ret = vfio_container_attach_group(container, group);
		goto out_unlock;
	}

out_unlock:
	mutex_unlock(&group->group_lock);
	fdput(f);
	return ret;
}

static const struct file_operations vfio_device_fops;

/* true if the vfio_device has open_device() called but not close_device() */
bool vfio_assert_device_open(struct vfio_device *device)
{
	return !WARN_ON_ONCE(!READ_ONCE(device->open_count));
}

static struct file *vfio_device_open(struct vfio_device *device)
{
	struct file *filep;
	int ret;

	mutex_lock(&device->group->group_lock);
	ret = vfio_device_assign_container(device);
	mutex_unlock(&device->group->group_lock);
	if (ret)
		return ERR_PTR(ret);

	if (!try_module_get(device->dev->driver->owner)) {
		ret = -ENODEV;
		goto err_unassign_container;
	}

	mutex_lock(&device->dev_set->lock);
	device->open_count++;
	if (device->open_count == 1) {
		/*
		 * Here we pass the KVM pointer with the group under the read
		 * lock.  If the device driver will use it, it must obtain a
		 * reference and release it during close_device.
		 */
		mutex_lock(&device->group->group_lock);
		device->kvm = device->group->kvm;

		if (device->ops->open_device) {
			ret = device->ops->open_device(device);
			if (ret)
				goto err_undo_count;
		}
		vfio_device_container_register(device);
		mutex_unlock(&device->group->group_lock);
	}
	mutex_unlock(&device->dev_set->lock);

	/*
	 * We can't use anon_inode_getfd() because we need to modify
	 * the f_mode flags directly to allow more than just ioctls
	 */
	filep = anon_inode_getfile("[vfio-device]", &vfio_device_fops,
				   device, O_RDWR);
	if (IS_ERR(filep)) {
		ret = PTR_ERR(filep);
		goto err_close_device;
	}

	/*
	 * TODO: add an anon_inode interface to do this.
	 * Appears to be missing by lack of need rather than
	 * explicitly prevented.  Now there's need.
	 */
	filep->f_mode |= (FMODE_PREAD | FMODE_PWRITE);

	if (device->group->type == VFIO_NO_IOMMU)
		dev_warn(device->dev, "vfio-noiommu device opened by user "
			 "(%s:%d)\n", current->comm, task_pid_nr(current));
	/*
	 * On success the ref of device is moved to the file and
	 * put in vfio_device_fops_release()
	 */
	return filep;

err_close_device:
	mutex_lock(&device->dev_set->lock);
	mutex_lock(&device->group->group_lock);
	if (device->open_count == 1 && device->ops->close_device) {
		device->ops->close_device(device);

		vfio_device_container_unregister(device);
	}
err_undo_count:
	mutex_unlock(&device->group->group_lock);
	device->open_count--;
	if (device->open_count == 0 && device->kvm)
		device->kvm = NULL;
	mutex_unlock(&device->dev_set->lock);
	module_put(device->dev->driver->owner);
err_unassign_container:
	vfio_device_unassign_container(device);
	return ERR_PTR(ret);
}

static int vfio_group_ioctl_get_device_fd(struct vfio_group *group,
					  char __user *arg)
{
	struct vfio_device *device;
	struct file *filep;
	char *buf;
	int fdno;
	int ret;

	buf = strndup_user(arg, PAGE_SIZE);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	device = vfio_device_get_from_name(group, buf);
	kfree(buf);
	if (IS_ERR(device))
		return PTR_ERR(device);

	fdno = get_unused_fd_flags(O_CLOEXEC);
	if (fdno < 0) {
		ret = fdno;
		goto err_put_device;
	}

	filep = vfio_device_open(device);
	if (IS_ERR(filep)) {
		ret = PTR_ERR(filep);
		goto err_put_fdno;
	}

	fd_install(fdno, filep);
	return fdno;

err_put_fdno:
	put_unused_fd(fdno);
err_put_device:
	vfio_device_put_registration(device);
	return ret;
}

static int vfio_group_ioctl_get_status(struct vfio_group *group,
				       struct vfio_group_status __user *arg)
{
	unsigned long minsz = offsetofend(struct vfio_group_status, flags);
	struct vfio_group_status status;

	if (copy_from_user(&status, arg, minsz))
		return -EFAULT;

	if (status.argsz < minsz)
		return -EINVAL;

	status.flags = 0;

	mutex_lock(&group->group_lock);
	if (group->container)
		status.flags |= VFIO_GROUP_FLAGS_CONTAINER_SET |
				VFIO_GROUP_FLAGS_VIABLE;
	else if (!iommu_group_dma_owner_claimed(group->iommu_group))
		status.flags |= VFIO_GROUP_FLAGS_VIABLE;
	mutex_unlock(&group->group_lock);

	if (copy_to_user(arg, &status, minsz))
		return -EFAULT;
	return 0;
}

static long vfio_group_fops_unl_ioctl(struct file *filep,
				      unsigned int cmd, unsigned long arg)
{
	struct vfio_group *group = filep->private_data;
	void __user *uarg = (void __user *)arg;

	switch (cmd) {
	case VFIO_GROUP_GET_DEVICE_FD:
		return vfio_group_ioctl_get_device_fd(group, uarg);
	case VFIO_GROUP_GET_STATUS:
		return vfio_group_ioctl_get_status(group, uarg);
	case VFIO_GROUP_SET_CONTAINER:
		return vfio_group_ioctl_set_container(group, uarg);
	case VFIO_GROUP_UNSET_CONTAINER:
		return vfio_group_ioctl_unset_container(group);
	default:
		return -ENOTTY;
	}
}

static int vfio_group_fops_open(struct inode *inode, struct file *filep)
{
	struct vfio_group *group =
		container_of(inode->i_cdev, struct vfio_group, cdev);
	int ret;

	mutex_lock(&group->group_lock);

	/*
	 * drivers can be zero if this races with vfio_device_remove_group(), it
	 * will be stable at 0 under the group rwsem
	 */
	if (refcount_read(&group->drivers) == 0) {
		ret = -ENODEV;
		goto out_unlock;
	}

	if (group->type == VFIO_NO_IOMMU && !capable(CAP_SYS_RAWIO)) {
		ret = -EPERM;
		goto out_unlock;
	}

	/*
	 * Do we need multiple instances of the group open?  Seems not.
	 */
	if (group->opened_file) {
		ret = -EBUSY;
		goto out_unlock;
	}
	group->opened_file = filep;
	filep->private_data = group;
	ret = 0;
out_unlock:
	mutex_unlock(&group->group_lock);
	return ret;
}

static int vfio_group_fops_release(struct inode *inode, struct file *filep)
{
	struct vfio_group *group = filep->private_data;

	filep->private_data = NULL;

	mutex_lock(&group->group_lock);
	/*
	 * Device FDs hold a group file reference, therefore the group release
	 * is only called when there are no open devices.
	 */
	WARN_ON(group->notifier.head);
	if (group->container)
		vfio_group_detach_container(group);
	group->opened_file = NULL;
	mutex_unlock(&group->group_lock);
	swake_up_one(&group->opened_file_wait);

	return 0;
}

static const struct file_operations vfio_group_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= vfio_group_fops_unl_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.open		= vfio_group_fops_open,
	.release	= vfio_group_fops_release,
};

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
	struct vfio_device *device = filep->private_data;

	mutex_lock(&device->dev_set->lock);
	vfio_assert_device_open(device);
	mutex_lock(&device->group->group_lock);
	if (device->open_count == 1 && device->ops->close_device)
		device->ops->close_device(device);

	vfio_device_container_unregister(device);
	mutex_unlock(&device->group->group_lock);
	device->open_count--;
	if (device->open_count == 0)
		device->kvm = NULL;
	mutex_unlock(&device->dev_set->lock);

	module_put(device->dev->driver->owner);

	vfio_device_unassign_container(device);

	vfio_device_put_registration(device);

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
	enum { VFIO_DEVICE_NUM_STATES = VFIO_DEVICE_STATE_RUNNING_P2P + 1 };
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
	 * Without P2P the driver must implement:
	 *         RUNNING -> STOP
	 *         STOP -> RUNNING
	 *
	 * The coding will step through multiple states for some combination
	 * transitions; if all optional features are supported, this means the
	 * following ones:
	 *         RESUMING -> STOP -> RUNNING_P2P
	 *         RESUMING -> STOP -> RUNNING_P2P -> RUNNING
	 *         RESUMING -> STOP -> STOP_COPY
	 *         RUNNING -> RUNNING_P2P -> STOP
	 *         RUNNING -> RUNNING_P2P -> STOP -> RESUMING
	 *         RUNNING -> RUNNING_P2P -> STOP -> STOP_COPY
	 *         RUNNING_P2P -> STOP -> RESUMING
	 *         RUNNING_P2P -> STOP -> STOP_COPY
	 *         STOP -> RUNNING_P2P -> RUNNING
	 *         STOP_COPY -> STOP -> RESUMING
	 *         STOP_COPY -> STOP -> RUNNING_P2P
	 *         STOP_COPY -> STOP -> RUNNING_P2P -> RUNNING
	 */
	static const u8 vfio_from_fsm_table[VFIO_DEVICE_NUM_STATES][VFIO_DEVICE_NUM_STATES] = {
		[VFIO_DEVICE_STATE_STOP] = {
			[VFIO_DEVICE_STATE_STOP] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_RUNNING] = VFIO_DEVICE_STATE_RUNNING_P2P,
			[VFIO_DEVICE_STATE_STOP_COPY] = VFIO_DEVICE_STATE_STOP_COPY,
			[VFIO_DEVICE_STATE_RESUMING] = VFIO_DEVICE_STATE_RESUMING,
			[VFIO_DEVICE_STATE_RUNNING_P2P] = VFIO_DEVICE_STATE_RUNNING_P2P,
			[VFIO_DEVICE_STATE_ERROR] = VFIO_DEVICE_STATE_ERROR,
		},
		[VFIO_DEVICE_STATE_RUNNING] = {
			[VFIO_DEVICE_STATE_STOP] = VFIO_DEVICE_STATE_RUNNING_P2P,
			[VFIO_DEVICE_STATE_RUNNING] = VFIO_DEVICE_STATE_RUNNING,
			[VFIO_DEVICE_STATE_STOP_COPY] = VFIO_DEVICE_STATE_RUNNING_P2P,
			[VFIO_DEVICE_STATE_RESUMING] = VFIO_DEVICE_STATE_RUNNING_P2P,
			[VFIO_DEVICE_STATE_RUNNING_P2P] = VFIO_DEVICE_STATE_RUNNING_P2P,
			[VFIO_DEVICE_STATE_ERROR] = VFIO_DEVICE_STATE_ERROR,
		},
		[VFIO_DEVICE_STATE_STOP_COPY] = {
			[VFIO_DEVICE_STATE_STOP] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_RUNNING] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_STOP_COPY] = VFIO_DEVICE_STATE_STOP_COPY,
			[VFIO_DEVICE_STATE_RESUMING] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_RUNNING_P2P] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_ERROR] = VFIO_DEVICE_STATE_ERROR,
		},
		[VFIO_DEVICE_STATE_RESUMING] = {
			[VFIO_DEVICE_STATE_STOP] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_RUNNING] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_STOP_COPY] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_RESUMING] = VFIO_DEVICE_STATE_RESUMING,
			[VFIO_DEVICE_STATE_RUNNING_P2P] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_ERROR] = VFIO_DEVICE_STATE_ERROR,
		},
		[VFIO_DEVICE_STATE_RUNNING_P2P] = {
			[VFIO_DEVICE_STATE_STOP] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_RUNNING] = VFIO_DEVICE_STATE_RUNNING,
			[VFIO_DEVICE_STATE_STOP_COPY] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_RESUMING] = VFIO_DEVICE_STATE_STOP,
			[VFIO_DEVICE_STATE_RUNNING_P2P] = VFIO_DEVICE_STATE_RUNNING_P2P,
			[VFIO_DEVICE_STATE_ERROR] = VFIO_DEVICE_STATE_ERROR,
		},
		[VFIO_DEVICE_STATE_ERROR] = {
			[VFIO_DEVICE_STATE_STOP] = VFIO_DEVICE_STATE_ERROR,
			[VFIO_DEVICE_STATE_RUNNING] = VFIO_DEVICE_STATE_ERROR,
			[VFIO_DEVICE_STATE_STOP_COPY] = VFIO_DEVICE_STATE_ERROR,
			[VFIO_DEVICE_STATE_RESUMING] = VFIO_DEVICE_STATE_ERROR,
			[VFIO_DEVICE_STATE_RUNNING_P2P] = VFIO_DEVICE_STATE_ERROR,
			[VFIO_DEVICE_STATE_ERROR] = VFIO_DEVICE_STATE_ERROR,
		},
	};

	static const unsigned int state_flags_table[VFIO_DEVICE_NUM_STATES] = {
		[VFIO_DEVICE_STATE_STOP] = VFIO_MIGRATION_STOP_COPY,
		[VFIO_DEVICE_STATE_RUNNING] = VFIO_MIGRATION_STOP_COPY,
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
	struct vfio_device *device = filep->private_data;
	int ret;

	ret = vfio_device_pm_runtime_get(device);
	if (ret)
		return ret;

	switch (cmd) {
	case VFIO_DEVICE_FEATURE:
		ret = vfio_ioctl_device_feature(device, (void __user *)arg);
		break;

	default:
		if (unlikely(!device->ops->ioctl))
			ret = -EINVAL;
		else
			ret = device->ops->ioctl(device, cmd, arg);
		break;
	}

	vfio_device_pm_runtime_put(device);
	return ret;
}

static ssize_t vfio_device_fops_read(struct file *filep, char __user *buf,
				     size_t count, loff_t *ppos)
{
	struct vfio_device *device = filep->private_data;

	if (unlikely(!device->ops->read))
		return -EINVAL;

	return device->ops->read(device, buf, count, ppos);
}

static ssize_t vfio_device_fops_write(struct file *filep,
				      const char __user *buf,
				      size_t count, loff_t *ppos)
{
	struct vfio_device *device = filep->private_data;

	if (unlikely(!device->ops->write))
		return -EINVAL;

	return device->ops->write(device, buf, count, ppos);
}

static int vfio_device_fops_mmap(struct file *filep, struct vm_area_struct *vma)
{
	struct vfio_device *device = filep->private_data;

	if (unlikely(!device->ops->mmap))
		return -EINVAL;

	return device->ops->mmap(device, vma);
}

static const struct file_operations vfio_device_fops = {
	.owner		= THIS_MODULE,
	.release	= vfio_device_fops_release,
	.read		= vfio_device_fops_read,
	.write		= vfio_device_fops_write,
	.unlocked_ioctl	= vfio_device_fops_unl_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.mmap		= vfio_device_fops_mmap,
};

/**
 * vfio_file_iommu_group - Return the struct iommu_group for the vfio group file
 * @file: VFIO group file
 *
 * The returned iommu_group is valid as long as a ref is held on the file.
 */
struct iommu_group *vfio_file_iommu_group(struct file *file)
{
	struct vfio_group *group = file->private_data;

	if (file->f_op != &vfio_group_fops)
		return NULL;
	return group->iommu_group;
}
EXPORT_SYMBOL_GPL(vfio_file_iommu_group);

/**
 * vfio_file_enforced_coherent - True if the DMA associated with the VFIO file
 *        is always CPU cache coherent
 * @file: VFIO group file
 *
 * Enforced coherency means that the IOMMU ignores things like the PCIe no-snoop
 * bit in DMA transactions. A return of false indicates that the user has
 * rights to access additional instructions such as wbinvd on x86.
 */
bool vfio_file_enforced_coherent(struct file *file)
{
	struct vfio_group *group = file->private_data;
	bool ret;

	if (file->f_op != &vfio_group_fops)
		return true;

	mutex_lock(&group->group_lock);
	if (group->container) {
		ret = vfio_container_ioctl_check_extension(group->container,
							   VFIO_DMA_CC_IOMMU);
	} else {
		/*
		 * Since the coherency state is determined only once a container
		 * is attached the user must do so before they can prove they
		 * have permission.
		 */
		ret = true;
	}
	mutex_unlock(&group->group_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(vfio_file_enforced_coherent);

/**
 * vfio_file_set_kvm - Link a kvm with VFIO drivers
 * @file: VFIO group file
 * @kvm: KVM to link
 *
 * When a VFIO device is first opened the KVM will be available in
 * device->kvm if one was associated with the group.
 */
void vfio_file_set_kvm(struct file *file, struct kvm *kvm)
{
	struct vfio_group *group = file->private_data;

	if (file->f_op != &vfio_group_fops)
		return;

	mutex_lock(&group->group_lock);
	group->kvm = kvm;
	mutex_unlock(&group->group_lock);
}
EXPORT_SYMBOL_GPL(vfio_file_set_kvm);

/**
 * vfio_file_has_dev - True if the VFIO file is a handle for device
 * @file: VFIO file to check
 * @device: Device that must be part of the file
 *
 * Returns true if given file has permission to manipulate the given device.
 */
bool vfio_file_has_dev(struct file *file, struct vfio_device *device)
{
	struct vfio_group *group = file->private_data;

	if (file->f_op != &vfio_group_fops)
		return false;

	return group == device->group;
}
EXPORT_SYMBOL_GPL(vfio_file_has_dev);

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
 * Module/class support
 */
static char *vfio_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "vfio/%s", dev_name(dev));
}

static int __init vfio_init(void)
{
	int ret;

	ida_init(&vfio.group_ida);
	ida_init(&vfio.device_ida);
	mutex_init(&vfio.group_lock);
	INIT_LIST_HEAD(&vfio.group_list);

	ret = vfio_container_init();
	if (ret)
		return ret;

	/* /dev/vfio/$GROUP */
	vfio.class = class_create(THIS_MODULE, "vfio");
	if (IS_ERR(vfio.class)) {
		ret = PTR_ERR(vfio.class);
		goto err_group_class;
	}

	vfio.class->devnode = vfio_devnode;

	/* /sys/class/vfio-dev/vfioX */
	vfio.device_class = class_create(THIS_MODULE, "vfio-dev");
	if (IS_ERR(vfio.device_class)) {
		ret = PTR_ERR(vfio.device_class);
		goto err_dev_class;
	}

	ret = alloc_chrdev_region(&vfio.group_devt, 0, MINORMASK + 1, "vfio");
	if (ret)
		goto err_alloc_chrdev;

	pr_info(DRIVER_DESC " version: " DRIVER_VERSION "\n");
	return 0;

err_alloc_chrdev:
	class_destroy(vfio.device_class);
	vfio.device_class = NULL;
err_dev_class:
	class_destroy(vfio.class);
	vfio.class = NULL;
err_group_class:
	vfio_container_cleanup();
	return ret;
}

static void __exit vfio_cleanup(void)
{
	WARN_ON(!list_empty(&vfio.group_list));

	ida_destroy(&vfio.device_ida);
	ida_destroy(&vfio.group_ida);
	unregister_chrdev_region(vfio.group_devt, MINORMASK + 1);
	class_destroy(vfio.device_class);
	vfio.device_class = NULL;
	class_destroy(vfio.class);
	vfio_container_cleanup();
	vfio.class = NULL;
	xa_destroy(&vfio_device_set_xa);
}

module_init(vfio_init);
module_exit(vfio_cleanup);

MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_ALIAS_MISCDEV(VFIO_MINOR);
MODULE_ALIAS("devname:vfio/vfio");
MODULE_SOFTDEP("post: vfio_iommu_type1 vfio_iommu_spapr_tce");
