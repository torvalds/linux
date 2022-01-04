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

#define DRIVER_VERSION	"0.3"
#define DRIVER_AUTHOR	"Alex Williamson <alex.williamson@redhat.com>"
#define DRIVER_DESC	"VFIO - User Level meta-driver"

static struct vfio {
	struct class			*class;
	struct list_head		iommu_drivers_list;
	struct mutex			iommu_drivers_lock;
	struct list_head		group_list;
	struct idr			group_idr;
	struct mutex			group_lock;
	struct cdev			group_cdev;
	dev_t				group_devt;
} vfio;

struct vfio_iommu_driver {
	const struct vfio_iommu_driver_ops	*ops;
	struct list_head			vfio_next;
};

struct vfio_container {
	struct kref			kref;
	struct list_head		group_list;
	struct rw_semaphore		group_lock;
	struct vfio_iommu_driver	*iommu_driver;
	void				*iommu_data;
	bool				noiommu;
};

struct vfio_unbound_dev {
	struct device			*dev;
	struct list_head		unbound_next;
};

struct vfio_group {
	struct kref			kref;
	int				minor;
	atomic_t			container_users;
	struct iommu_group		*iommu_group;
	struct vfio_container		*container;
	struct list_head		device_list;
	struct mutex			device_lock;
	struct device			*dev;
	struct notifier_block		nb;
	struct list_head		vfio_next;
	struct list_head		container_next;
	struct list_head		unbound_list;
	struct mutex			unbound_lock;
	atomic_t			opened;
	wait_queue_head_t		container_q;
	bool				noiommu;
	unsigned int			dev_counter;
	struct kvm			*kvm;
	struct blocking_notifier_head	notifier;
};

#ifdef CONFIG_VFIO_NOIOMMU
static bool noiommu __read_mostly;
module_param_named(enable_unsafe_noiommu_mode,
		   noiommu, bool, S_IRUGO | S_IWUSR);
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

/*
 * vfio_iommu_group_{get,put} are only intended for VFIO bus driver probe
 * and remove functions, any use cases other than acquiring the first
 * reference for the purpose of calling vfio_register_group_dev() or removing
 * that symmetric reference after vfio_unregister_group_dev() should use the raw
 * iommu_group_{get,put} functions.  In particular, vfio_iommu_group_put()
 * removes the device from the dummy group and cannot be nested.
 */
struct iommu_group *vfio_iommu_group_get(struct device *dev)
{
	struct iommu_group *group;
	int __maybe_unused ret;

	group = iommu_group_get(dev);

#ifdef CONFIG_VFIO_NOIOMMU
	/*
	 * With noiommu enabled, an IOMMU group will be created for a device
	 * that doesn't already have one and doesn't have an iommu_ops on their
	 * bus.  We set iommudata simply to be able to identify these groups
	 * as special use and for reclamation later.
	 */
	if (group || !noiommu || iommu_present(dev->bus))
		return group;

	group = iommu_group_alloc();
	if (IS_ERR(group))
		return NULL;

	iommu_group_set_name(group, "vfio-noiommu");
	iommu_group_set_iommudata(group, &noiommu, NULL);
	ret = iommu_group_add_device(group, dev);
	if (ret) {
		iommu_group_put(group);
		return NULL;
	}

	/*
	 * Where to taint?  At this point we've added an IOMMU group for a
	 * device that is not backed by iommu_ops, therefore any iommu_
	 * callback using iommu_ops can legitimately Oops.  So, while we may
	 * be about to give a DMA capable device to a user without IOMMU
	 * protection, which is clearly taint-worthy, let's go ahead and do
	 * it here.
	 */
	add_taint(TAINT_USER, LOCKDEP_STILL_OK);
	dev_warn(dev, "Adding kernel taint for vfio-noiommu group on device\n");
#endif

	return group;
}
EXPORT_SYMBOL_GPL(vfio_iommu_group_get);

void vfio_iommu_group_put(struct iommu_group *group, struct device *dev)
{
#ifdef CONFIG_VFIO_NOIOMMU
	if (iommu_group_get_iommudata(group) == &noiommu)
		iommu_group_remove_device(dev);
#endif

	iommu_group_put(group);
}
EXPORT_SYMBOL_GPL(vfio_iommu_group_put);

#ifdef CONFIG_VFIO_NOIOMMU
static void *vfio_noiommu_open(unsigned long arg)
{
	if (arg != VFIO_NOIOMMU_IOMMU)
		return ERR_PTR(-EINVAL);
	if (!capable(CAP_SYS_RAWIO))
		return ERR_PTR(-EPERM);

	return NULL;
}

static void vfio_noiommu_release(void *iommu_data)
{
}

static long vfio_noiommu_ioctl(void *iommu_data,
			       unsigned int cmd, unsigned long arg)
{
	if (cmd == VFIO_CHECK_EXTENSION)
		return noiommu && (arg == VFIO_NOIOMMU_IOMMU) ? 1 : 0;

	return -ENOTTY;
}

static int vfio_noiommu_attach_group(void *iommu_data,
				     struct iommu_group *iommu_group)
{
	return iommu_group_get_iommudata(iommu_group) == &noiommu ? 0 : -EINVAL;
}

static void vfio_noiommu_detach_group(void *iommu_data,
				      struct iommu_group *iommu_group)
{
}

static const struct vfio_iommu_driver_ops vfio_noiommu_ops = {
	.name = "vfio-noiommu",
	.owner = THIS_MODULE,
	.open = vfio_noiommu_open,
	.release = vfio_noiommu_release,
	.ioctl = vfio_noiommu_ioctl,
	.attach_group = vfio_noiommu_attach_group,
	.detach_group = vfio_noiommu_detach_group,
};
#endif


/**
 * IOMMU driver registration
 */
int vfio_register_iommu_driver(const struct vfio_iommu_driver_ops *ops)
{
	struct vfio_iommu_driver *driver, *tmp;

	driver = kzalloc(sizeof(*driver), GFP_KERNEL);
	if (!driver)
		return -ENOMEM;

	driver->ops = ops;

	mutex_lock(&vfio.iommu_drivers_lock);

	/* Check for duplicates */
	list_for_each_entry(tmp, &vfio.iommu_drivers_list, vfio_next) {
		if (tmp->ops == ops) {
			mutex_unlock(&vfio.iommu_drivers_lock);
			kfree(driver);
			return -EINVAL;
		}
	}

	list_add(&driver->vfio_next, &vfio.iommu_drivers_list);

	mutex_unlock(&vfio.iommu_drivers_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(vfio_register_iommu_driver);

void vfio_unregister_iommu_driver(const struct vfio_iommu_driver_ops *ops)
{
	struct vfio_iommu_driver *driver;

	mutex_lock(&vfio.iommu_drivers_lock);
	list_for_each_entry(driver, &vfio.iommu_drivers_list, vfio_next) {
		if (driver->ops == ops) {
			list_del(&driver->vfio_next);
			mutex_unlock(&vfio.iommu_drivers_lock);
			kfree(driver);
			return;
		}
	}
	mutex_unlock(&vfio.iommu_drivers_lock);
}
EXPORT_SYMBOL_GPL(vfio_unregister_iommu_driver);

/**
 * Group minor allocation/free - both called with vfio.group_lock held
 */
static int vfio_alloc_group_minor(struct vfio_group *group)
{
	return idr_alloc(&vfio.group_idr, group, 0, MINORMASK + 1, GFP_KERNEL);
}

static void vfio_free_group_minor(int minor)
{
	idr_remove(&vfio.group_idr, minor);
}

static int vfio_iommu_group_notifier(struct notifier_block *nb,
				     unsigned long action, void *data);
static void vfio_group_get(struct vfio_group *group);

/**
 * Container objects - containers are created when /dev/vfio/vfio is
 * opened, but their lifecycle extends until the last user is done, so
 * it's freed via kref.  Must support container/group/device being
 * closed in any order.
 */
static void vfio_container_get(struct vfio_container *container)
{
	kref_get(&container->kref);
}

static void vfio_container_release(struct kref *kref)
{
	struct vfio_container *container;
	container = container_of(kref, struct vfio_container, kref);

	kfree(container);
}

static void vfio_container_put(struct vfio_container *container)
{
	kref_put(&container->kref, vfio_container_release);
}

static void vfio_group_unlock_and_free(struct vfio_group *group)
{
	mutex_unlock(&vfio.group_lock);
	/*
	 * Unregister outside of lock.  A spurious callback is harmless now
	 * that the group is no longer in vfio.group_list.
	 */
	iommu_group_unregister_notifier(group->iommu_group, &group->nb);
	kfree(group);
}

/**
 * Group objects - create, release, get, put, search
 */
static struct vfio_group *vfio_create_group(struct iommu_group *iommu_group)
{
	struct vfio_group *group, *tmp;
	struct device *dev;
	int ret, minor;

	group = kzalloc(sizeof(*group), GFP_KERNEL);
	if (!group)
		return ERR_PTR(-ENOMEM);

	kref_init(&group->kref);
	INIT_LIST_HEAD(&group->device_list);
	mutex_init(&group->device_lock);
	INIT_LIST_HEAD(&group->unbound_list);
	mutex_init(&group->unbound_lock);
	atomic_set(&group->container_users, 0);
	atomic_set(&group->opened, 0);
	init_waitqueue_head(&group->container_q);
	group->iommu_group = iommu_group;
#ifdef CONFIG_VFIO_NOIOMMU
	group->noiommu = (iommu_group_get_iommudata(iommu_group) == &noiommu);
#endif
	BLOCKING_INIT_NOTIFIER_HEAD(&group->notifier);

	group->nb.notifier_call = vfio_iommu_group_notifier;

	/*
	 * blocking notifiers acquire a rwsem around registering and hold
	 * it around callback.  Therefore, need to register outside of
	 * vfio.group_lock to avoid A-B/B-A contention.  Our callback won't
	 * do anything unless it can find the group in vfio.group_list, so
	 * no harm in registering early.
	 */
	ret = iommu_group_register_notifier(iommu_group, &group->nb);
	if (ret) {
		kfree(group);
		return ERR_PTR(ret);
	}

	mutex_lock(&vfio.group_lock);

	/* Did we race creating this group? */
	list_for_each_entry(tmp, &vfio.group_list, vfio_next) {
		if (tmp->iommu_group == iommu_group) {
			vfio_group_get(tmp);
			vfio_group_unlock_and_free(group);
			return tmp;
		}
	}

	minor = vfio_alloc_group_minor(group);
	if (minor < 0) {
		vfio_group_unlock_and_free(group);
		return ERR_PTR(minor);
	}

	dev = device_create(vfio.class, NULL,
			    MKDEV(MAJOR(vfio.group_devt), minor),
			    group, "%s%d", group->noiommu ? "noiommu-" : "",
			    iommu_group_id(iommu_group));
	if (IS_ERR(dev)) {
		vfio_free_group_minor(minor);
		vfio_group_unlock_and_free(group);
		return ERR_CAST(dev);
	}

	group->minor = minor;
	group->dev = dev;

	list_add(&group->vfio_next, &vfio.group_list);

	mutex_unlock(&vfio.group_lock);

	return group;
}

/* called with vfio.group_lock held */
static void vfio_group_release(struct kref *kref)
{
	struct vfio_group *group = container_of(kref, struct vfio_group, kref);
	struct vfio_unbound_dev *unbound, *tmp;
	struct iommu_group *iommu_group = group->iommu_group;

	WARN_ON(!list_empty(&group->device_list));
	WARN_ON(group->notifier.head);

	list_for_each_entry_safe(unbound, tmp,
				 &group->unbound_list, unbound_next) {
		list_del(&unbound->unbound_next);
		kfree(unbound);
	}

	device_destroy(vfio.class, MKDEV(MAJOR(vfio.group_devt), group->minor));
	list_del(&group->vfio_next);
	vfio_free_group_minor(group->minor);
	vfio_group_unlock_and_free(group);
	iommu_group_put(iommu_group);
}

static void vfio_group_put(struct vfio_group *group)
{
	kref_put_mutex(&group->kref, vfio_group_release, &vfio.group_lock);
}

struct vfio_group_put_work {
	struct work_struct work;
	struct vfio_group *group;
};

static void vfio_group_put_bg(struct work_struct *work)
{
	struct vfio_group_put_work *do_work;

	do_work = container_of(work, struct vfio_group_put_work, work);

	vfio_group_put(do_work->group);
	kfree(do_work);
}

static void vfio_group_schedule_put(struct vfio_group *group)
{
	struct vfio_group_put_work *do_work;

	do_work = kmalloc(sizeof(*do_work), GFP_KERNEL);
	if (WARN_ON(!do_work))
		return;

	INIT_WORK(&do_work->work, vfio_group_put_bg);
	do_work->group = group;
	schedule_work(&do_work->work);
}

/* Assume group_lock or group reference is held */
static void vfio_group_get(struct vfio_group *group)
{
	kref_get(&group->kref);
}

/*
 * Not really a try as we will sleep for mutex, but we need to make
 * sure the group pointer is valid under lock and get a reference.
 */
static struct vfio_group *vfio_group_try_get(struct vfio_group *group)
{
	struct vfio_group *target = group;

	mutex_lock(&vfio.group_lock);
	list_for_each_entry(group, &vfio.group_list, vfio_next) {
		if (group == target) {
			vfio_group_get(group);
			mutex_unlock(&vfio.group_lock);
			return group;
		}
	}
	mutex_unlock(&vfio.group_lock);

	return NULL;
}

static
struct vfio_group *vfio_group_get_from_iommu(struct iommu_group *iommu_group)
{
	struct vfio_group *group;

	mutex_lock(&vfio.group_lock);
	list_for_each_entry(group, &vfio.group_list, vfio_next) {
		if (group->iommu_group == iommu_group) {
			vfio_group_get(group);
			mutex_unlock(&vfio.group_lock);
			return group;
		}
	}
	mutex_unlock(&vfio.group_lock);

	return NULL;
}

static struct vfio_group *vfio_group_get_from_minor(int minor)
{
	struct vfio_group *group;

	mutex_lock(&vfio.group_lock);
	group = idr_find(&vfio.group_idr, minor);
	if (!group) {
		mutex_unlock(&vfio.group_lock);
		return NULL;
	}
	vfio_group_get(group);
	mutex_unlock(&vfio.group_lock);

	return group;
}

static struct vfio_group *vfio_group_get_from_dev(struct device *dev)
{
	struct iommu_group *iommu_group;
	struct vfio_group *group;

	iommu_group = iommu_group_get(dev);
	if (!iommu_group)
		return NULL;

	group = vfio_group_get_from_iommu(iommu_group);
	iommu_group_put(iommu_group);

	return group;
}

/**
 * Device objects - create, release, get, put, search
 */
/* Device reference always implies a group reference */
void vfio_device_put(struct vfio_device *device)
{
	if (refcount_dec_and_test(&device->refcount))
		complete(&device->comp);
}
EXPORT_SYMBOL_GPL(vfio_device_put);

static bool vfio_device_try_get(struct vfio_device *device)
{
	return refcount_inc_not_zero(&device->refcount);
}

static struct vfio_device *vfio_group_get_device(struct vfio_group *group,
						 struct device *dev)
{
	struct vfio_device *device;

	mutex_lock(&group->device_lock);
	list_for_each_entry(device, &group->device_list, group_next) {
		if (device->dev == dev && vfio_device_try_get(device)) {
			mutex_unlock(&group->device_lock);
			return device;
		}
	}
	mutex_unlock(&group->device_lock);
	return NULL;
}

/*
 * Some drivers, like pci-stub, are only used to prevent other drivers from
 * claiming a device and are therefore perfectly legitimate for a user owned
 * group.  The pci-stub driver has no dependencies on DMA or the IOVA mapping
 * of the device, but it does prevent the user from having direct access to
 * the device, which is useful in some circumstances.
 *
 * We also assume that we can include PCI interconnect devices, ie. bridges.
 * IOMMU grouping on PCI necessitates that if we lack isolation on a bridge
 * then all of the downstream devices will be part of the same IOMMU group as
 * the bridge.  Thus, if placing the bridge into the user owned IOVA space
 * breaks anything, it only does so for user owned devices downstream.  Note
 * that error notification via MSI can be affected for platforms that handle
 * MSI within the same IOVA space as DMA.
 */
static const char * const vfio_driver_allowed[] = { "pci-stub" };

static bool vfio_dev_driver_allowed(struct device *dev,
				    struct device_driver *drv)
{
	if (dev_is_pci(dev)) {
		struct pci_dev *pdev = to_pci_dev(dev);

		if (pdev->hdr_type != PCI_HEADER_TYPE_NORMAL)
			return true;
	}

	return match_string(vfio_driver_allowed,
			    ARRAY_SIZE(vfio_driver_allowed),
			    drv->name) >= 0;
}

/*
 * A vfio group is viable for use by userspace if all devices are in
 * one of the following states:
 *  - driver-less
 *  - bound to a vfio driver
 *  - bound to an otherwise allowed driver
 *  - a PCI interconnect device
 *
 * We use two methods to determine whether a device is bound to a vfio
 * driver.  The first is to test whether the device exists in the vfio
 * group.  The second is to test if the device exists on the group
 * unbound_list, indicating it's in the middle of transitioning from
 * a vfio driver to driver-less.
 */
static int vfio_dev_viable(struct device *dev, void *data)
{
	struct vfio_group *group = data;
	struct vfio_device *device;
	struct device_driver *drv = READ_ONCE(dev->driver);
	struct vfio_unbound_dev *unbound;
	int ret = -EINVAL;

	mutex_lock(&group->unbound_lock);
	list_for_each_entry(unbound, &group->unbound_list, unbound_next) {
		if (dev == unbound->dev) {
			ret = 0;
			break;
		}
	}
	mutex_unlock(&group->unbound_lock);

	if (!ret || !drv || vfio_dev_driver_allowed(dev, drv))
		return 0;

	device = vfio_group_get_device(group, dev);
	if (device) {
		vfio_device_put(device);
		return 0;
	}

	return ret;
}

/**
 * Async device support
 */
static int vfio_group_nb_add_dev(struct vfio_group *group, struct device *dev)
{
	struct vfio_device *device;

	/* Do we already know about it?  We shouldn't */
	device = vfio_group_get_device(group, dev);
	if (WARN_ON_ONCE(device)) {
		vfio_device_put(device);
		return 0;
	}

	/* Nothing to do for idle groups */
	if (!atomic_read(&group->container_users))
		return 0;

	/* TODO Prevent device auto probing */
	dev_WARN(dev, "Device added to live group %d!\n",
		 iommu_group_id(group->iommu_group));

	return 0;
}

static int vfio_group_nb_verify(struct vfio_group *group, struct device *dev)
{
	/* We don't care what happens when the group isn't in use */
	if (!atomic_read(&group->container_users))
		return 0;

	return vfio_dev_viable(dev, group);
}

static int vfio_iommu_group_notifier(struct notifier_block *nb,
				     unsigned long action, void *data)
{
	struct vfio_group *group = container_of(nb, struct vfio_group, nb);
	struct device *dev = data;
	struct vfio_unbound_dev *unbound;

	/*
	 * Need to go through a group_lock lookup to get a reference or we
	 * risk racing a group being removed.  Ignore spurious notifies.
	 */
	group = vfio_group_try_get(group);
	if (!group)
		return NOTIFY_OK;

	switch (action) {
	case IOMMU_GROUP_NOTIFY_ADD_DEVICE:
		vfio_group_nb_add_dev(group, dev);
		break;
	case IOMMU_GROUP_NOTIFY_DEL_DEVICE:
		/*
		 * Nothing to do here.  If the device is in use, then the
		 * vfio sub-driver should block the remove callback until
		 * it is unused.  If the device is unused or attached to a
		 * stub driver, then it should be released and we don't
		 * care that it will be going away.
		 */
		break;
	case IOMMU_GROUP_NOTIFY_BIND_DRIVER:
		dev_dbg(dev, "%s: group %d binding to driver\n", __func__,
			iommu_group_id(group->iommu_group));
		break;
	case IOMMU_GROUP_NOTIFY_BOUND_DRIVER:
		dev_dbg(dev, "%s: group %d bound to driver %s\n", __func__,
			iommu_group_id(group->iommu_group), dev->driver->name);
		BUG_ON(vfio_group_nb_verify(group, dev));
		break;
	case IOMMU_GROUP_NOTIFY_UNBIND_DRIVER:
		dev_dbg(dev, "%s: group %d unbinding from driver %s\n",
			__func__, iommu_group_id(group->iommu_group),
			dev->driver->name);
		break;
	case IOMMU_GROUP_NOTIFY_UNBOUND_DRIVER:
		dev_dbg(dev, "%s: group %d unbound from driver\n", __func__,
			iommu_group_id(group->iommu_group));
		/*
		 * XXX An unbound device in a live group is ok, but we'd
		 * really like to avoid the above BUG_ON by preventing other
		 * drivers from binding to it.  Once that occurs, we have to
		 * stop the system to maintain isolation.  At a minimum, we'd
		 * want a toggle to disable driver auto probe for this device.
		 */

		mutex_lock(&group->unbound_lock);
		list_for_each_entry(unbound,
				    &group->unbound_list, unbound_next) {
			if (dev == unbound->dev) {
				list_del(&unbound->unbound_next);
				kfree(unbound);
				break;
			}
		}
		mutex_unlock(&group->unbound_lock);
		break;
	}

	/*
	 * If we're the last reference to the group, the group will be
	 * released, which includes unregistering the iommu group notifier.
	 * We hold a read-lock on that notifier list, unregistering needs
	 * a write-lock... deadlock.  Release our reference asynchronously
	 * to avoid that situation.
	 */
	vfio_group_schedule_put(group);
	return NOTIFY_OK;
}

/**
 * VFIO driver API
 */
void vfio_init_group_dev(struct vfio_device *device, struct device *dev,
			 const struct vfio_device_ops *ops)
{
	init_completion(&device->comp);
	device->dev = dev;
	device->ops = ops;
}
EXPORT_SYMBOL_GPL(vfio_init_group_dev);

void vfio_uninit_group_dev(struct vfio_device *device)
{
	vfio_release_device_set(device);
}
EXPORT_SYMBOL_GPL(vfio_uninit_group_dev);

int vfio_register_group_dev(struct vfio_device *device)
{
	struct vfio_device *existing_device;
	struct iommu_group *iommu_group;
	struct vfio_group *group;

	/*
	 * If the driver doesn't specify a set then the device is added to a
	 * singleton set just for itself.
	 */
	if (!device->dev_set)
		vfio_assign_device_set(device, device);

	iommu_group = iommu_group_get(device->dev);
	if (!iommu_group)
		return -EINVAL;

	group = vfio_group_get_from_iommu(iommu_group);
	if (!group) {
		group = vfio_create_group(iommu_group);
		if (IS_ERR(group)) {
			iommu_group_put(iommu_group);
			return PTR_ERR(group);
		}
	} else {
		/*
		 * A found vfio_group already holds a reference to the
		 * iommu_group.  A created vfio_group keeps the reference.
		 */
		iommu_group_put(iommu_group);
	}

	existing_device = vfio_group_get_device(group, device->dev);
	if (existing_device) {
		dev_WARN(device->dev, "Device already exists on group %d\n",
			 iommu_group_id(iommu_group));
		vfio_device_put(existing_device);
		vfio_group_put(group);
		return -EBUSY;
	}

	/* Our reference on group is moved to the device */
	device->group = group;

	/* Refcounting can't start until the driver calls register */
	refcount_set(&device->refcount, 1);

	mutex_lock(&group->device_lock);
	list_add(&device->group_next, &group->device_list);
	group->dev_counter++;
	mutex_unlock(&group->device_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(vfio_register_group_dev);

/**
 * Get a reference to the vfio_device for a device.  Even if the
 * caller thinks they own the device, they could be racing with a
 * release call path, so we can't trust drvdata for the shortcut.
 * Go the long way around, from the iommu_group to the vfio_group
 * to the vfio_device.
 */
struct vfio_device *vfio_device_get_from_dev(struct device *dev)
{
	struct vfio_group *group;
	struct vfio_device *device;

	group = vfio_group_get_from_dev(dev);
	if (!group)
		return NULL;

	device = vfio_group_get_device(group, dev);
	vfio_group_put(group);

	return device;
}
EXPORT_SYMBOL_GPL(vfio_device_get_from_dev);

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

		if (ret && vfio_device_try_get(it)) {
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
	struct vfio_unbound_dev *unbound;
	unsigned int i = 0;
	bool interrupted = false;
	long rc;

	/*
	 * When the device is removed from the group, the group suddenly
	 * becomes non-viable; the device has a driver (until the unbind
	 * completes), but it's not present in the group.  This is bad news
	 * for any external users that need to re-acquire a group reference
	 * in order to match and release their existing reference.  To
	 * solve this, we track such devices on the unbound_list to bridge
	 * the gap until they're fully unbound.
	 */
	unbound = kzalloc(sizeof(*unbound), GFP_KERNEL);
	if (unbound) {
		unbound->dev = device->dev;
		mutex_lock(&group->unbound_lock);
		list_add(&unbound->unbound_next, &group->unbound_list);
		mutex_unlock(&group->unbound_lock);
	}
	WARN_ON(!unbound);

	vfio_device_put(device);
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
	group->dev_counter--;
	mutex_unlock(&group->device_lock);

	/*
	 * In order to support multiple devices per group, devices can be
	 * plucked from the group while other devices in the group are still
	 * in use.  The container persists with this group and those remaining
	 * devices still attached.  If the user creates an isolation violation
	 * by binding this device to another driver while the group is still in
	 * use, that's their fault.  However, in the case of removing the last,
	 * or potentially the only, device in the group there can be no other
	 * in-use devices in the group.  The user has done their due diligence
	 * and we should lay no claims to those devices.  In order to do that,
	 * we need to make sure the group is detached from the container.
	 * Without this stall, we're potentially racing with a user process
	 * that may attempt to immediately bind this device to another driver.
	 */
	if (list_empty(&group->device_list))
		wait_event(group->container_q, !group->container);

	/* Matches the get in vfio_register_group_dev() */
	vfio_group_put(group);
}
EXPORT_SYMBOL_GPL(vfio_unregister_group_dev);

/**
 * VFIO base fd, /dev/vfio/vfio
 */
static long vfio_ioctl_check_extension(struct vfio_container *container,
				       unsigned long arg)
{
	struct vfio_iommu_driver *driver;
	long ret = 0;

	down_read(&container->group_lock);

	driver = container->iommu_driver;

	switch (arg) {
		/* No base extensions yet */
	default:
		/*
		 * If no driver is set, poll all registered drivers for
		 * extensions and return the first positive result.  If
		 * a driver is already set, further queries will be passed
		 * only to that driver.
		 */
		if (!driver) {
			mutex_lock(&vfio.iommu_drivers_lock);
			list_for_each_entry(driver, &vfio.iommu_drivers_list,
					    vfio_next) {

#ifdef CONFIG_VFIO_NOIOMMU
				if (!list_empty(&container->group_list) &&
				    (container->noiommu !=
				     (driver->ops == &vfio_noiommu_ops)))
					continue;
#endif

				if (!try_module_get(driver->ops->owner))
					continue;

				ret = driver->ops->ioctl(NULL,
							 VFIO_CHECK_EXTENSION,
							 arg);
				module_put(driver->ops->owner);
				if (ret > 0)
					break;
			}
			mutex_unlock(&vfio.iommu_drivers_lock);
		} else
			ret = driver->ops->ioctl(container->iommu_data,
						 VFIO_CHECK_EXTENSION, arg);
	}

	up_read(&container->group_lock);

	return ret;
}

/* hold write lock on container->group_lock */
static int __vfio_container_attach_groups(struct vfio_container *container,
					  struct vfio_iommu_driver *driver,
					  void *data)
{
	struct vfio_group *group;
	int ret = -ENODEV;

	list_for_each_entry(group, &container->group_list, container_next) {
		ret = driver->ops->attach_group(data, group->iommu_group);
		if (ret)
			goto unwind;
	}

	return ret;

unwind:
	list_for_each_entry_continue_reverse(group, &container->group_list,
					     container_next) {
		driver->ops->detach_group(data, group->iommu_group);
	}

	return ret;
}

static long vfio_ioctl_set_iommu(struct vfio_container *container,
				 unsigned long arg)
{
	struct vfio_iommu_driver *driver;
	long ret = -ENODEV;

	down_write(&container->group_lock);

	/*
	 * The container is designed to be an unprivileged interface while
	 * the group can be assigned to specific users.  Therefore, only by
	 * adding a group to a container does the user get the privilege of
	 * enabling the iommu, which may allocate finite resources.  There
	 * is no unset_iommu, but by removing all the groups from a container,
	 * the container is deprivileged and returns to an unset state.
	 */
	if (list_empty(&container->group_list) || container->iommu_driver) {
		up_write(&container->group_lock);
		return -EINVAL;
	}

	mutex_lock(&vfio.iommu_drivers_lock);
	list_for_each_entry(driver, &vfio.iommu_drivers_list, vfio_next) {
		void *data;

#ifdef CONFIG_VFIO_NOIOMMU
		/*
		 * Only noiommu containers can use vfio-noiommu and noiommu
		 * containers can only use vfio-noiommu.
		 */
		if (container->noiommu != (driver->ops == &vfio_noiommu_ops))
			continue;
#endif

		if (!try_module_get(driver->ops->owner))
			continue;

		/*
		 * The arg magic for SET_IOMMU is the same as CHECK_EXTENSION,
		 * so test which iommu driver reported support for this
		 * extension and call open on them.  We also pass them the
		 * magic, allowing a single driver to support multiple
		 * interfaces if they'd like.
		 */
		if (driver->ops->ioctl(NULL, VFIO_CHECK_EXTENSION, arg) <= 0) {
			module_put(driver->ops->owner);
			continue;
		}

		data = driver->ops->open(arg);
		if (IS_ERR(data)) {
			ret = PTR_ERR(data);
			module_put(driver->ops->owner);
			continue;
		}

		ret = __vfio_container_attach_groups(container, driver, data);
		if (ret) {
			driver->ops->release(data);
			module_put(driver->ops->owner);
			continue;
		}

		container->iommu_driver = driver;
		container->iommu_data = data;
		break;
	}

	mutex_unlock(&vfio.iommu_drivers_lock);
	up_write(&container->group_lock);

	return ret;
}

static long vfio_fops_unl_ioctl(struct file *filep,
				unsigned int cmd, unsigned long arg)
{
	struct vfio_container *container = filep->private_data;
	struct vfio_iommu_driver *driver;
	void *data;
	long ret = -EINVAL;

	if (!container)
		return ret;

	switch (cmd) {
	case VFIO_GET_API_VERSION:
		ret = VFIO_API_VERSION;
		break;
	case VFIO_CHECK_EXTENSION:
		ret = vfio_ioctl_check_extension(container, arg);
		break;
	case VFIO_SET_IOMMU:
		ret = vfio_ioctl_set_iommu(container, arg);
		break;
	default:
		driver = container->iommu_driver;
		data = container->iommu_data;

		if (driver) /* passthrough all unrecognized ioctls */
			ret = driver->ops->ioctl(data, cmd, arg);
	}

	return ret;
}

static int vfio_fops_open(struct inode *inode, struct file *filep)
{
	struct vfio_container *container;

	container = kzalloc(sizeof(*container), GFP_KERNEL);
	if (!container)
		return -ENOMEM;

	INIT_LIST_HEAD(&container->group_list);
	init_rwsem(&container->group_lock);
	kref_init(&container->kref);

	filep->private_data = container;

	return 0;
}

static int vfio_fops_release(struct inode *inode, struct file *filep)
{
	struct vfio_container *container = filep->private_data;
	struct vfio_iommu_driver *driver = container->iommu_driver;

	if (driver && driver->ops->notify)
		driver->ops->notify(container->iommu_data,
				    VFIO_IOMMU_CONTAINER_CLOSE);

	filep->private_data = NULL;

	vfio_container_put(container);

	return 0;
}

/*
 * Once an iommu driver is set, we optionally pass read/write/mmap
 * on to the driver, allowing management interfaces beyond ioctl.
 */
static ssize_t vfio_fops_read(struct file *filep, char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct vfio_container *container = filep->private_data;
	struct vfio_iommu_driver *driver;
	ssize_t ret = -EINVAL;

	driver = container->iommu_driver;
	if (likely(driver && driver->ops->read))
		ret = driver->ops->read(container->iommu_data,
					buf, count, ppos);

	return ret;
}

static ssize_t vfio_fops_write(struct file *filep, const char __user *buf,
			       size_t count, loff_t *ppos)
{
	struct vfio_container *container = filep->private_data;
	struct vfio_iommu_driver *driver;
	ssize_t ret = -EINVAL;

	driver = container->iommu_driver;
	if (likely(driver && driver->ops->write))
		ret = driver->ops->write(container->iommu_data,
					 buf, count, ppos);

	return ret;
}

static int vfio_fops_mmap(struct file *filep, struct vm_area_struct *vma)
{
	struct vfio_container *container = filep->private_data;
	struct vfio_iommu_driver *driver;
	int ret = -EINVAL;

	driver = container->iommu_driver;
	if (likely(driver && driver->ops->mmap))
		ret = driver->ops->mmap(container->iommu_data, vma);

	return ret;
}

static const struct file_operations vfio_fops = {
	.owner		= THIS_MODULE,
	.open		= vfio_fops_open,
	.release	= vfio_fops_release,
	.read		= vfio_fops_read,
	.write		= vfio_fops_write,
	.unlocked_ioctl	= vfio_fops_unl_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.mmap		= vfio_fops_mmap,
};

/**
 * VFIO Group fd, /dev/vfio/$GROUP
 */
static void __vfio_group_unset_container(struct vfio_group *group)
{
	struct vfio_container *container = group->container;
	struct vfio_iommu_driver *driver;

	down_write(&container->group_lock);

	driver = container->iommu_driver;
	if (driver)
		driver->ops->detach_group(container->iommu_data,
					  group->iommu_group);

	group->container = NULL;
	wake_up(&group->container_q);
	list_del(&group->container_next);

	/* Detaching the last group deprivileges a container, remove iommu */
	if (driver && list_empty(&container->group_list)) {
		driver->ops->release(container->iommu_data);
		module_put(driver->ops->owner);
		container->iommu_driver = NULL;
		container->iommu_data = NULL;
	}

	up_write(&container->group_lock);

	vfio_container_put(container);
}

/*
 * VFIO_GROUP_UNSET_CONTAINER should fail if there are other users or
 * if there was no container to unset.  Since the ioctl is called on
 * the group, we know that still exists, therefore the only valid
 * transition here is 1->0.
 */
static int vfio_group_unset_container(struct vfio_group *group)
{
	int users = atomic_cmpxchg(&group->container_users, 1, 0);

	if (!users)
		return -EINVAL;
	if (users != 1)
		return -EBUSY;

	__vfio_group_unset_container(group);

	return 0;
}

/*
 * When removing container users, anything that removes the last user
 * implicitly removes the group from the container.  That is, if the
 * group file descriptor is closed, as well as any device file descriptors,
 * the group is free.
 */
static void vfio_group_try_dissolve_container(struct vfio_group *group)
{
	if (0 == atomic_dec_if_positive(&group->container_users))
		__vfio_group_unset_container(group);
}

static int vfio_group_set_container(struct vfio_group *group, int container_fd)
{
	struct fd f;
	struct vfio_container *container;
	struct vfio_iommu_driver *driver;
	int ret = 0;

	if (atomic_read(&group->container_users))
		return -EINVAL;

	if (group->noiommu && !capable(CAP_SYS_RAWIO))
		return -EPERM;

	f = fdget(container_fd);
	if (!f.file)
		return -EBADF;

	/* Sanity check, is this really our fd? */
	if (f.file->f_op != &vfio_fops) {
		fdput(f);
		return -EINVAL;
	}

	container = f.file->private_data;
	WARN_ON(!container); /* fget ensures we don't race vfio_release */

	down_write(&container->group_lock);

	/* Real groups and fake groups cannot mix */
	if (!list_empty(&container->group_list) &&
	    container->noiommu != group->noiommu) {
		ret = -EPERM;
		goto unlock_out;
	}

	driver = container->iommu_driver;
	if (driver) {
		ret = driver->ops->attach_group(container->iommu_data,
						group->iommu_group);
		if (ret)
			goto unlock_out;
	}

	group->container = container;
	container->noiommu = group->noiommu;
	list_add(&group->container_next, &container->group_list);

	/* Get a reference on the container and mark a user within the group */
	vfio_container_get(container);
	atomic_inc(&group->container_users);

unlock_out:
	up_write(&container->group_lock);
	fdput(f);
	return ret;
}

static bool vfio_group_viable(struct vfio_group *group)
{
	return (iommu_group_for_each_dev(group->iommu_group,
					 group, vfio_dev_viable) == 0);
}

static int vfio_group_add_container_user(struct vfio_group *group)
{
	if (!atomic_inc_not_zero(&group->container_users))
		return -EINVAL;

	if (group->noiommu) {
		atomic_dec(&group->container_users);
		return -EPERM;
	}
	if (!group->container->iommu_driver || !vfio_group_viable(group)) {
		atomic_dec(&group->container_users);
		return -EINVAL;
	}

	return 0;
}

static const struct file_operations vfio_device_fops;

static int vfio_group_get_device_fd(struct vfio_group *group, char *buf)
{
	struct vfio_device *device;
	struct file *filep;
	int fdno;
	int ret = 0;

	if (0 == atomic_read(&group->container_users) ||
	    !group->container->iommu_driver || !vfio_group_viable(group))
		return -EINVAL;

	if (group->noiommu && !capable(CAP_SYS_RAWIO))
		return -EPERM;

	device = vfio_device_get_from_name(group, buf);
	if (IS_ERR(device))
		return PTR_ERR(device);

	if (!try_module_get(device->dev->driver->owner)) {
		ret = -ENODEV;
		goto err_device_put;
	}

	mutex_lock(&device->dev_set->lock);
	device->open_count++;
	if (device->open_count == 1 && device->ops->open_device) {
		ret = device->ops->open_device(device);
		if (ret)
			goto err_undo_count;
	}
	mutex_unlock(&device->dev_set->lock);

	/*
	 * We can't use anon_inode_getfd() because we need to modify
	 * the f_mode flags directly to allow more than just ioctls
	 */
	fdno = ret = get_unused_fd_flags(O_CLOEXEC);
	if (ret < 0)
		goto err_close_device;

	filep = anon_inode_getfile("[vfio-device]", &vfio_device_fops,
				   device, O_RDWR);
	if (IS_ERR(filep)) {
		ret = PTR_ERR(filep);
		goto err_fd;
	}

	/*
	 * TODO: add an anon_inode interface to do this.
	 * Appears to be missing by lack of need rather than
	 * explicitly prevented.  Now there's need.
	 */
	filep->f_mode |= (FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE);

	atomic_inc(&group->container_users);

	fd_install(fdno, filep);

	if (group->noiommu)
		dev_warn(device->dev, "vfio-noiommu device opened by user "
			 "(%s:%d)\n", current->comm, task_pid_nr(current));
	return fdno;

err_fd:
	put_unused_fd(fdno);
err_close_device:
	mutex_lock(&device->dev_set->lock);
	if (device->open_count == 1 && device->ops->close_device)
		device->ops->close_device(device);
err_undo_count:
	device->open_count--;
	mutex_unlock(&device->dev_set->lock);
	module_put(device->dev->driver->owner);
err_device_put:
	vfio_device_put(device);
	return ret;
}

static long vfio_group_fops_unl_ioctl(struct file *filep,
				      unsigned int cmd, unsigned long arg)
{
	struct vfio_group *group = filep->private_data;
	long ret = -ENOTTY;

	switch (cmd) {
	case VFIO_GROUP_GET_STATUS:
	{
		struct vfio_group_status status;
		unsigned long minsz;

		minsz = offsetofend(struct vfio_group_status, flags);

		if (copy_from_user(&status, (void __user *)arg, minsz))
			return -EFAULT;

		if (status.argsz < minsz)
			return -EINVAL;

		status.flags = 0;

		if (vfio_group_viable(group))
			status.flags |= VFIO_GROUP_FLAGS_VIABLE;

		if (group->container)
			status.flags |= VFIO_GROUP_FLAGS_CONTAINER_SET;

		if (copy_to_user((void __user *)arg, &status, minsz))
			return -EFAULT;

		ret = 0;
		break;
	}
	case VFIO_GROUP_SET_CONTAINER:
	{
		int fd;

		if (get_user(fd, (int __user *)arg))
			return -EFAULT;

		if (fd < 0)
			return -EINVAL;

		ret = vfio_group_set_container(group, fd);
		break;
	}
	case VFIO_GROUP_UNSET_CONTAINER:
		ret = vfio_group_unset_container(group);
		break;
	case VFIO_GROUP_GET_DEVICE_FD:
	{
		char *buf;

		buf = strndup_user((const char __user *)arg, PAGE_SIZE);
		if (IS_ERR(buf))
			return PTR_ERR(buf);

		ret = vfio_group_get_device_fd(group, buf);
		kfree(buf);
		break;
	}
	}

	return ret;
}

static int vfio_group_fops_open(struct inode *inode, struct file *filep)
{
	struct vfio_group *group;
	int opened;

	group = vfio_group_get_from_minor(iminor(inode));
	if (!group)
		return -ENODEV;

	if (group->noiommu && !capable(CAP_SYS_RAWIO)) {
		vfio_group_put(group);
		return -EPERM;
	}

	/* Do we need multiple instances of the group open?  Seems not. */
	opened = atomic_cmpxchg(&group->opened, 0, 1);
	if (opened) {
		vfio_group_put(group);
		return -EBUSY;
	}

	/* Is something still in use from a previous open? */
	if (group->container) {
		atomic_dec(&group->opened);
		vfio_group_put(group);
		return -EBUSY;
	}

	/* Warn if previous user didn't cleanup and re-init to drop them */
	if (WARN_ON(group->notifier.head))
		BLOCKING_INIT_NOTIFIER_HEAD(&group->notifier);

	filep->private_data = group;

	return 0;
}

static int vfio_group_fops_release(struct inode *inode, struct file *filep)
{
	struct vfio_group *group = filep->private_data;

	filep->private_data = NULL;

	vfio_group_try_dissolve_container(group);

	atomic_dec(&group->opened);

	vfio_group_put(group);

	return 0;
}

static const struct file_operations vfio_group_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= vfio_group_fops_unl_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.open		= vfio_group_fops_open,
	.release	= vfio_group_fops_release,
};

/**
 * VFIO Device fd
 */
static int vfio_device_fops_release(struct inode *inode, struct file *filep)
{
	struct vfio_device *device = filep->private_data;

	mutex_lock(&device->dev_set->lock);
	if (!--device->open_count && device->ops->close_device)
		device->ops->close_device(device);
	mutex_unlock(&device->dev_set->lock);

	module_put(device->dev->driver->owner);

	vfio_group_try_dissolve_container(device->group);

	vfio_device_put(device);

	return 0;
}

static long vfio_device_fops_unl_ioctl(struct file *filep,
				       unsigned int cmd, unsigned long arg)
{
	struct vfio_device *device = filep->private_data;

	if (unlikely(!device->ops->ioctl))
		return -EINVAL;

	return device->ops->ioctl(device, cmd, arg);
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
 * External user API, exported by symbols to be linked dynamically.
 *
 * The protocol includes:
 *  1. do normal VFIO init operation:
 *	- opening a new container;
 *	- attaching group(s) to it;
 *	- setting an IOMMU driver for a container.
 * When IOMMU is set for a container, all groups in it are
 * considered ready to use by an external user.
 *
 * 2. User space passes a group fd to an external user.
 * The external user calls vfio_group_get_external_user()
 * to verify that:
 *	- the group is initialized;
 *	- IOMMU is set for it.
 * If both checks passed, vfio_group_get_external_user()
 * increments the container user counter to prevent
 * the VFIO group from disposal before KVM exits.
 *
 * 3. The external user calls vfio_external_user_iommu_id()
 * to know an IOMMU ID.
 *
 * 4. When the external KVM finishes, it calls
 * vfio_group_put_external_user() to release the VFIO group.
 * This call decrements the container user counter.
 */
struct vfio_group *vfio_group_get_external_user(struct file *filep)
{
	struct vfio_group *group = filep->private_data;
	int ret;

	if (filep->f_op != &vfio_group_fops)
		return ERR_PTR(-EINVAL);

	ret = vfio_group_add_container_user(group);
	if (ret)
		return ERR_PTR(ret);

	vfio_group_get(group);

	return group;
}
EXPORT_SYMBOL_GPL(vfio_group_get_external_user);

/**
 * External user API, exported by symbols to be linked dynamically.
 * The external user passes in a device pointer
 * to verify that:
 *	- A VFIO group is assiciated with the device;
 *	- IOMMU is set for the group.
 * If both checks passed, vfio_group_get_external_user_from_dev()
 * increments the container user counter to prevent the VFIO group
 * from disposal before external user exits and returns the pointer
 * to the VFIO group.
 *
 * When the external user finishes using the VFIO group, it calls
 * vfio_group_put_external_user() to release the VFIO group and
 * decrement the container user counter.
 *
 * @dev [in]	: device
 * Return error PTR or pointer to VFIO group.
 */

struct vfio_group *vfio_group_get_external_user_from_dev(struct device *dev)
{
	struct vfio_group *group;
	int ret;

	group = vfio_group_get_from_dev(dev);
	if (!group)
		return ERR_PTR(-ENODEV);

	ret = vfio_group_add_container_user(group);
	if (ret) {
		vfio_group_put(group);
		return ERR_PTR(ret);
	}

	return group;
}
EXPORT_SYMBOL_GPL(vfio_group_get_external_user_from_dev);

void vfio_group_put_external_user(struct vfio_group *group)
{
	vfio_group_try_dissolve_container(group);
	vfio_group_put(group);
}
EXPORT_SYMBOL_GPL(vfio_group_put_external_user);

bool vfio_external_group_match_file(struct vfio_group *test_group,
				    struct file *filep)
{
	struct vfio_group *group = filep->private_data;

	return (filep->f_op == &vfio_group_fops) && (group == test_group);
}
EXPORT_SYMBOL_GPL(vfio_external_group_match_file);

int vfio_external_user_iommu_id(struct vfio_group *group)
{
	return iommu_group_id(group->iommu_group);
}
EXPORT_SYMBOL_GPL(vfio_external_user_iommu_id);

long vfio_external_check_extension(struct vfio_group *group, unsigned long arg)
{
	return vfio_ioctl_check_extension(group->container, arg);
}
EXPORT_SYMBOL_GPL(vfio_external_check_extension);

/**
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
 * Pin a set of guest PFNs and return their associated host PFNs for local
 * domain only.
 * @dev [in]     : device
 * @user_pfn [in]: array of user/guest PFNs to be pinned.
 * @npage [in]   : count of elements in user_pfn array.  This count should not
 *		   be greater VFIO_PIN_PAGES_MAX_ENTRIES.
 * @prot [in]    : protection flags
 * @phys_pfn[out]: array of host PFNs
 * Return error or number of pages pinned.
 */
int vfio_pin_pages(struct device *dev, unsigned long *user_pfn, int npage,
		   int prot, unsigned long *phys_pfn)
{
	struct vfio_container *container;
	struct vfio_group *group;
	struct vfio_iommu_driver *driver;
	int ret;

	if (!dev || !user_pfn || !phys_pfn || !npage)
		return -EINVAL;

	if (npage > VFIO_PIN_PAGES_MAX_ENTRIES)
		return -E2BIG;

	group = vfio_group_get_from_dev(dev);
	if (!group)
		return -ENODEV;

	if (group->dev_counter > 1) {
		ret = -EINVAL;
		goto err_pin_pages;
	}

	ret = vfio_group_add_container_user(group);
	if (ret)
		goto err_pin_pages;

	container = group->container;
	driver = container->iommu_driver;
	if (likely(driver && driver->ops->pin_pages))
		ret = driver->ops->pin_pages(container->iommu_data,
					     group->iommu_group, user_pfn,
					     npage, prot, phys_pfn);
	else
		ret = -ENOTTY;

	vfio_group_try_dissolve_container(group);

err_pin_pages:
	vfio_group_put(group);
	return ret;
}
EXPORT_SYMBOL(vfio_pin_pages);

/*
 * Unpin set of host PFNs for local domain only.
 * @dev [in]     : device
 * @user_pfn [in]: array of user/guest PFNs to be unpinned. Number of user/guest
 *		   PFNs should not be greater than VFIO_PIN_PAGES_MAX_ENTRIES.
 * @npage [in]   : count of elements in user_pfn array.  This count should not
 *                 be greater than VFIO_PIN_PAGES_MAX_ENTRIES.
 * Return error or number of pages unpinned.
 */
int vfio_unpin_pages(struct device *dev, unsigned long *user_pfn, int npage)
{
	struct vfio_container *container;
	struct vfio_group *group;
	struct vfio_iommu_driver *driver;
	int ret;

	if (!dev || !user_pfn || !npage)
		return -EINVAL;

	if (npage > VFIO_PIN_PAGES_MAX_ENTRIES)
		return -E2BIG;

	group = vfio_group_get_from_dev(dev);
	if (!group)
		return -ENODEV;

	ret = vfio_group_add_container_user(group);
	if (ret)
		goto err_unpin_pages;

	container = group->container;
	driver = container->iommu_driver;
	if (likely(driver && driver->ops->unpin_pages))
		ret = driver->ops->unpin_pages(container->iommu_data, user_pfn,
					       npage);
	else
		ret = -ENOTTY;

	vfio_group_try_dissolve_container(group);

err_unpin_pages:
	vfio_group_put(group);
	return ret;
}
EXPORT_SYMBOL(vfio_unpin_pages);

/*
 * Pin a set of guest IOVA PFNs and return their associated host PFNs for a
 * VFIO group.
 *
 * The caller needs to call vfio_group_get_external_user() or
 * vfio_group_get_external_user_from_dev() prior to calling this interface,
 * so as to prevent the VFIO group from disposal in the middle of the call.
 * But it can keep the reference to the VFIO group for several calls into
 * this interface.
 * After finishing using of the VFIO group, the caller needs to release the
 * VFIO group by calling vfio_group_put_external_user().
 *
 * @group [in]		: VFIO group
 * @user_iova_pfn [in]	: array of user/guest IOVA PFNs to be pinned.
 * @npage [in]		: count of elements in user_iova_pfn array.
 *			  This count should not be greater
 *			  VFIO_PIN_PAGES_MAX_ENTRIES.
 * @prot [in]		: protection flags
 * @phys_pfn [out]	: array of host PFNs
 * Return error or number of pages pinned.
 */
int vfio_group_pin_pages(struct vfio_group *group,
			 unsigned long *user_iova_pfn, int npage,
			 int prot, unsigned long *phys_pfn)
{
	struct vfio_container *container;
	struct vfio_iommu_driver *driver;
	int ret;

	if (!group || !user_iova_pfn || !phys_pfn || !npage)
		return -EINVAL;

	if (group->dev_counter > 1)
		return -EINVAL;

	if (npage > VFIO_PIN_PAGES_MAX_ENTRIES)
		return -E2BIG;

	container = group->container;
	driver = container->iommu_driver;
	if (likely(driver && driver->ops->pin_pages))
		ret = driver->ops->pin_pages(container->iommu_data,
					     group->iommu_group, user_iova_pfn,
					     npage, prot, phys_pfn);
	else
		ret = -ENOTTY;

	return ret;
}
EXPORT_SYMBOL(vfio_group_pin_pages);

/*
 * Unpin a set of guest IOVA PFNs for a VFIO group.
 *
 * The caller needs to call vfio_group_get_external_user() or
 * vfio_group_get_external_user_from_dev() prior to calling this interface,
 * so as to prevent the VFIO group from disposal in the middle of the call.
 * But it can keep the reference to the VFIO group for several calls into
 * this interface.
 * After finishing using of the VFIO group, the caller needs to release the
 * VFIO group by calling vfio_group_put_external_user().
 *
 * @group [in]		: vfio group
 * @user_iova_pfn [in]	: array of user/guest IOVA PFNs to be unpinned.
 * @npage [in]		: count of elements in user_iova_pfn array.
 *			  This count should not be greater than
 *			  VFIO_PIN_PAGES_MAX_ENTRIES.
 * Return error or number of pages unpinned.
 */
int vfio_group_unpin_pages(struct vfio_group *group,
			   unsigned long *user_iova_pfn, int npage)
{
	struct vfio_container *container;
	struct vfio_iommu_driver *driver;
	int ret;

	if (!group || !user_iova_pfn || !npage)
		return -EINVAL;

	if (npage > VFIO_PIN_PAGES_MAX_ENTRIES)
		return -E2BIG;

	container = group->container;
	driver = container->iommu_driver;
	if (likely(driver && driver->ops->unpin_pages))
		ret = driver->ops->unpin_pages(container->iommu_data,
					       user_iova_pfn, npage);
	else
		ret = -ENOTTY;

	return ret;
}
EXPORT_SYMBOL(vfio_group_unpin_pages);


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
 * The caller needs to call vfio_group_get_external_user() or
 * vfio_group_get_external_user_from_dev() prior to calling this interface,
 * so as to prevent the VFIO group from disposal in the middle of the call.
 * But it can keep the reference to the VFIO group for several calls into
 * this interface.
 * After finishing using of the VFIO group, the caller needs to release the
 * VFIO group by calling vfio_group_put_external_user().
 *
 * @group [in]		: VFIO group
 * @user_iova [in]	: base IOVA of a user space buffer
 * @data [in]		: pointer to kernel buffer
 * @len [in]		: kernel buffer length
 * @write		: indicate read or write
 * Return error code on failure or 0 on success.
 */
int vfio_dma_rw(struct vfio_group *group, dma_addr_t user_iova,
		void *data, size_t len, bool write)
{
	struct vfio_container *container;
	struct vfio_iommu_driver *driver;
	int ret = 0;

	if (!group || !data || len <= 0)
		return -EINVAL;

	container = group->container;
	driver = container->iommu_driver;

	if (likely(driver && driver->ops->dma_rw))
		ret = driver->ops->dma_rw(container->iommu_data,
					  user_iova, data, len, write);
	else
		ret = -ENOTTY;

	return ret;
}
EXPORT_SYMBOL(vfio_dma_rw);

static int vfio_register_iommu_notifier(struct vfio_group *group,
					unsigned long *events,
					struct notifier_block *nb)
{
	struct vfio_container *container;
	struct vfio_iommu_driver *driver;
	int ret;

	ret = vfio_group_add_container_user(group);
	if (ret)
		return -EINVAL;

	container = group->container;
	driver = container->iommu_driver;
	if (likely(driver && driver->ops->register_notifier))
		ret = driver->ops->register_notifier(container->iommu_data,
						     events, nb);
	else
		ret = -ENOTTY;

	vfio_group_try_dissolve_container(group);

	return ret;
}

static int vfio_unregister_iommu_notifier(struct vfio_group *group,
					  struct notifier_block *nb)
{
	struct vfio_container *container;
	struct vfio_iommu_driver *driver;
	int ret;

	ret = vfio_group_add_container_user(group);
	if (ret)
		return -EINVAL;

	container = group->container;
	driver = container->iommu_driver;
	if (likely(driver && driver->ops->unregister_notifier))
		ret = driver->ops->unregister_notifier(container->iommu_data,
						       nb);
	else
		ret = -ENOTTY;

	vfio_group_try_dissolve_container(group);

	return ret;
}

void vfio_group_set_kvm(struct vfio_group *group, struct kvm *kvm)
{
	group->kvm = kvm;
	blocking_notifier_call_chain(&group->notifier,
				VFIO_GROUP_NOTIFY_SET_KVM, kvm);
}
EXPORT_SYMBOL_GPL(vfio_group_set_kvm);

static int vfio_register_group_notifier(struct vfio_group *group,
					unsigned long *events,
					struct notifier_block *nb)
{
	int ret;
	bool set_kvm = false;

	if (*events & VFIO_GROUP_NOTIFY_SET_KVM)
		set_kvm = true;

	/* clear known events */
	*events &= ~VFIO_GROUP_NOTIFY_SET_KVM;

	/* refuse to continue if still events remaining */
	if (*events)
		return -EINVAL;

	ret = vfio_group_add_container_user(group);
	if (ret)
		return -EINVAL;

	ret = blocking_notifier_chain_register(&group->notifier, nb);

	/*
	 * The attaching of kvm and vfio_group might already happen, so
	 * here we replay once upon registration.
	 */
	if (!ret && set_kvm && group->kvm)
		blocking_notifier_call_chain(&group->notifier,
					VFIO_GROUP_NOTIFY_SET_KVM, group->kvm);

	vfio_group_try_dissolve_container(group);

	return ret;
}

static int vfio_unregister_group_notifier(struct vfio_group *group,
					 struct notifier_block *nb)
{
	int ret;

	ret = vfio_group_add_container_user(group);
	if (ret)
		return -EINVAL;

	ret = blocking_notifier_chain_unregister(&group->notifier, nb);

	vfio_group_try_dissolve_container(group);

	return ret;
}

int vfio_register_notifier(struct device *dev, enum vfio_notify_type type,
			   unsigned long *events, struct notifier_block *nb)
{
	struct vfio_group *group;
	int ret;

	if (!dev || !nb || !events || (*events == 0))
		return -EINVAL;

	group = vfio_group_get_from_dev(dev);
	if (!group)
		return -ENODEV;

	switch (type) {
	case VFIO_IOMMU_NOTIFY:
		ret = vfio_register_iommu_notifier(group, events, nb);
		break;
	case VFIO_GROUP_NOTIFY:
		ret = vfio_register_group_notifier(group, events, nb);
		break;
	default:
		ret = -EINVAL;
	}

	vfio_group_put(group);
	return ret;
}
EXPORT_SYMBOL(vfio_register_notifier);

int vfio_unregister_notifier(struct device *dev, enum vfio_notify_type type,
			     struct notifier_block *nb)
{
	struct vfio_group *group;
	int ret;

	if (!dev || !nb)
		return -EINVAL;

	group = vfio_group_get_from_dev(dev);
	if (!group)
		return -ENODEV;

	switch (type) {
	case VFIO_IOMMU_NOTIFY:
		ret = vfio_unregister_iommu_notifier(group, nb);
		break;
	case VFIO_GROUP_NOTIFY:
		ret = vfio_unregister_group_notifier(group, nb);
		break;
	default:
		ret = -EINVAL;
	}

	vfio_group_put(group);
	return ret;
}
EXPORT_SYMBOL(vfio_unregister_notifier);

struct iommu_domain *vfio_group_iommu_domain(struct vfio_group *group)
{
	struct vfio_container *container;
	struct vfio_iommu_driver *driver;

	if (!group)
		return ERR_PTR(-EINVAL);

	container = group->container;
	driver = container->iommu_driver;
	if (likely(driver && driver->ops->group_iommu_domain))
		return driver->ops->group_iommu_domain(container->iommu_data,
						       group->iommu_group);

	return ERR_PTR(-ENOTTY);
}
EXPORT_SYMBOL_GPL(vfio_group_iommu_domain);

/**
 * Module/class support
 */
static char *vfio_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "vfio/%s", dev_name(dev));
}

static struct miscdevice vfio_dev = {
	.minor = VFIO_MINOR,
	.name = "vfio",
	.fops = &vfio_fops,
	.nodename = "vfio/vfio",
	.mode = S_IRUGO | S_IWUGO,
};

static int __init vfio_init(void)
{
	int ret;

	idr_init(&vfio.group_idr);
	mutex_init(&vfio.group_lock);
	mutex_init(&vfio.iommu_drivers_lock);
	INIT_LIST_HEAD(&vfio.group_list);
	INIT_LIST_HEAD(&vfio.iommu_drivers_list);

	ret = misc_register(&vfio_dev);
	if (ret) {
		pr_err("vfio: misc device register failed\n");
		return ret;
	}

	/* /dev/vfio/$GROUP */
	vfio.class = class_create(THIS_MODULE, "vfio");
	if (IS_ERR(vfio.class)) {
		ret = PTR_ERR(vfio.class);
		goto err_class;
	}

	vfio.class->devnode = vfio_devnode;

	ret = alloc_chrdev_region(&vfio.group_devt, 0, MINORMASK + 1, "vfio");
	if (ret)
		goto err_alloc_chrdev;

	cdev_init(&vfio.group_cdev, &vfio_group_fops);
	ret = cdev_add(&vfio.group_cdev, vfio.group_devt, MINORMASK + 1);
	if (ret)
		goto err_cdev_add;

	pr_info(DRIVER_DESC " version: " DRIVER_VERSION "\n");

#ifdef CONFIG_VFIO_NOIOMMU
	vfio_register_iommu_driver(&vfio_noiommu_ops);
#endif
	return 0;

err_cdev_add:
	unregister_chrdev_region(vfio.group_devt, MINORMASK + 1);
err_alloc_chrdev:
	class_destroy(vfio.class);
	vfio.class = NULL;
err_class:
	misc_deregister(&vfio_dev);
	return ret;
}

static void __exit vfio_cleanup(void)
{
	WARN_ON(!list_empty(&vfio.group_list));

#ifdef CONFIG_VFIO_NOIOMMU
	vfio_unregister_iommu_driver(&vfio_noiommu_ops);
#endif
	idr_destroy(&vfio.group_idr);
	cdev_del(&vfio.group_cdev);
	unregister_chrdev_region(vfio.group_devt, MINORMASK + 1);
	class_destroy(vfio.class);
	vfio.class = NULL;
	misc_deregister(&vfio_dev);
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
