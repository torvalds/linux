// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Red Hat, Inc.  All rights reserved.
 *
 * VFIO container (/dev/vfio/vfio)
 */
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/capability.h>
#include <linux/iommu.h>
#include <linux/miscdevice.h>
#include <linux/vfio.h>
#include <uapi/linux/vfio.h>

#include "vfio.h"

struct vfio_container {
	struct kref			kref;
	struct list_head		group_list;
	struct rw_semaphore		group_lock;
	struct vfio_iommu_driver	*iommu_driver;
	void				*iommu_data;
	bool				noiommu;
};

static struct vfio {
	struct list_head		iommu_drivers_list;
	struct mutex			iommu_drivers_lock;
} vfio;

#ifdef CONFIG_VFIO_NOIOMMU
bool vfio_noiommu __read_mostly;
module_param_named(enable_unsafe_noiommu_mode,
		   vfio_noiommu, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(enable_unsafe_noiommu_mode, "Enable UNSAFE, no-IOMMU mode.  This mode provides no device isolation, no DMA translation, no host kernel protection, cannot be used for device assignment to virtual machines, requires RAWIO permissions, and will taint the kernel.  If you do not know what this is for, step away. (default: false)");
#endif

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
		return vfio_noiommu && (arg == VFIO_NOIOMMU_IOMMU) ? 1 : 0;

	return -ENOTTY;
}

static int vfio_noiommu_attach_group(void *iommu_data,
		struct iommu_group *iommu_group, enum vfio_group_type type)
{
	return 0;
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

/*
 * Only noiommu containers can use vfio-noiommu and noiommu containers can only
 * use vfio-noiommu.
 */
static bool vfio_iommu_driver_allowed(struct vfio_container *container,
				      const struct vfio_iommu_driver *driver)
{
	if (!IS_ENABLED(CONFIG_VFIO_NOIOMMU))
		return true;
	return container->noiommu == (driver->ops == &vfio_noiommu_ops);
}

/*
 * IOMMU driver registration
 */
int vfio_register_iommu_driver(const struct vfio_iommu_driver_ops *ops)
{
	struct vfio_iommu_driver *driver, *tmp;

	if (WARN_ON(!ops->register_device != !ops->unregister_device))
		return -EINVAL;

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

/*
 * Container objects - containers are created when /dev/vfio/vfio is
 * opened, but their lifecycle extends until the last user is done, so
 * it's freed via kref.  Must support container/group/device being
 * closed in any order.
 */
static void vfio_container_release(struct kref *kref)
{
	struct vfio_container *container;
	container = container_of(kref, struct vfio_container, kref);

	kfree(container);
}

static void vfio_container_get(struct vfio_container *container)
{
	kref_get(&container->kref);
}

static void vfio_container_put(struct vfio_container *container)
{
	kref_put(&container->kref, vfio_container_release);
}

void vfio_device_container_register(struct vfio_device *device)
{
	struct vfio_iommu_driver *iommu_driver =
		device->group->container->iommu_driver;

	if (iommu_driver && iommu_driver->ops->register_device)
		iommu_driver->ops->register_device(
			device->group->container->iommu_data, device);
}

void vfio_device_container_unregister(struct vfio_device *device)
{
	struct vfio_iommu_driver *iommu_driver =
		device->group->container->iommu_driver;

	if (iommu_driver && iommu_driver->ops->unregister_device)
		iommu_driver->ops->unregister_device(
			device->group->container->iommu_data, device);
}

long vfio_container_ioctl_check_extension(struct vfio_container *container,
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

				if (!list_empty(&container->group_list) &&
				    !vfio_iommu_driver_allowed(container,
							       driver))
					continue;
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
		ret = driver->ops->attach_group(data, group->iommu_group,
						group->type);
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

		if (!vfio_iommu_driver_allowed(container, driver))
			continue;
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
		ret = vfio_container_ioctl_check_extension(container, arg);
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

static const struct file_operations vfio_fops = {
	.owner		= THIS_MODULE,
	.open		= vfio_fops_open,
	.release	= vfio_fops_release,
	.unlocked_ioctl	= vfio_fops_unl_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
};

struct vfio_container *vfio_container_from_file(struct file *file)
{
	struct vfio_container *container;

	/* Sanity check, is this really our fd? */
	if (file->f_op != &vfio_fops)
		return NULL;

	container = file->private_data;
	WARN_ON(!container); /* fget ensures we don't race vfio_release */
	return container;
}

static struct miscdevice vfio_dev = {
	.minor = VFIO_MINOR,
	.name = "vfio",
	.fops = &vfio_fops,
	.nodename = "vfio/vfio",
	.mode = S_IRUGO | S_IWUGO,
};

int vfio_container_attach_group(struct vfio_container *container,
				struct vfio_group *group)
{
	struct vfio_iommu_driver *driver;
	int ret = 0;

	lockdep_assert_held(&group->group_lock);

	if (group->type == VFIO_NO_IOMMU && !capable(CAP_SYS_RAWIO))
		return -EPERM;

	down_write(&container->group_lock);

	/* Real groups and fake groups cannot mix */
	if (!list_empty(&container->group_list) &&
	    container->noiommu != (group->type == VFIO_NO_IOMMU)) {
		ret = -EPERM;
		goto out_unlock_container;
	}

	if (group->type == VFIO_IOMMU) {
		ret = iommu_group_claim_dma_owner(group->iommu_group, group);
		if (ret)
			goto out_unlock_container;
	}

	driver = container->iommu_driver;
	if (driver) {
		ret = driver->ops->attach_group(container->iommu_data,
						group->iommu_group,
						group->type);
		if (ret) {
			if (group->type == VFIO_IOMMU)
				iommu_group_release_dma_owner(
					group->iommu_group);
			goto out_unlock_container;
		}
	}

	group->container = container;
	group->container_users = 1;
	container->noiommu = (group->type == VFIO_NO_IOMMU);
	list_add(&group->container_next, &container->group_list);

	/* Get a reference on the container and mark a user within the group */
	vfio_container_get(container);

out_unlock_container:
	up_write(&container->group_lock);
	return ret;
}

void vfio_group_detach_container(struct vfio_group *group)
{
	struct vfio_container *container = group->container;
	struct vfio_iommu_driver *driver;

	lockdep_assert_held(&group->group_lock);
	WARN_ON(group->container_users != 1);

	down_write(&container->group_lock);

	driver = container->iommu_driver;
	if (driver)
		driver->ops->detach_group(container->iommu_data,
					  group->iommu_group);

	if (group->type == VFIO_IOMMU)
		iommu_group_release_dma_owner(group->iommu_group);

	group->container = NULL;
	group->container_users = 0;
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

int vfio_device_assign_container(struct vfio_device *device)
{
	struct vfio_group *group = device->group;

	lockdep_assert_held(&group->group_lock);

	if (!group->container || !group->container->iommu_driver ||
	    WARN_ON(!group->container_users))
		return -EINVAL;

	if (group->type == VFIO_NO_IOMMU && !capable(CAP_SYS_RAWIO))
		return -EPERM;

	get_file(group->opened_file);
	group->container_users++;
	return 0;
}

void vfio_device_unassign_container(struct vfio_device *device)
{
	mutex_lock(&device->group->group_lock);
	WARN_ON(device->group->container_users <= 1);
	device->group->container_users--;
	fput(device->group->opened_file);
	mutex_unlock(&device->group->group_lock);
}

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
 * by vfio_register_emulated_iommu_dev().
 */
int vfio_pin_pages(struct vfio_device *device, dma_addr_t iova,
		   int npage, int prot, struct page **pages)
{
	struct vfio_container *container;
	struct vfio_group *group = device->group;
	struct vfio_iommu_driver *driver;
	int ret;

	if (!pages || !npage || !vfio_assert_device_open(device))
		return -EINVAL;

	if (npage > VFIO_PIN_PAGES_MAX_ENTRIES)
		return -E2BIG;

	/* group->container cannot change while a vfio device is open */
	container = group->container;
	driver = container->iommu_driver;
	if (likely(driver && driver->ops->pin_pages))
		ret = driver->ops->pin_pages(container->iommu_data,
					     group->iommu_group, iova,
					     npage, prot, pages);
	else
		ret = -ENOTTY;

	return ret;
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
	struct vfio_container *container;
	struct vfio_iommu_driver *driver;

	if (WARN_ON(npage <= 0 || npage > VFIO_PIN_PAGES_MAX_ENTRIES))
		return;

	if (WARN_ON(!vfio_assert_device_open(device)))
		return;

	/* group->container cannot change while a vfio device is open */
	container = device->group->container;
	driver = container->iommu_driver;

	driver->ops->unpin_pages(container->iommu_data, iova, npage);
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
	struct vfio_container *container;
	struct vfio_iommu_driver *driver;
	int ret = 0;

	if (!data || len <= 0 || !vfio_assert_device_open(device))
		return -EINVAL;

	/* group->container cannot change while a vfio device is open */
	container = device->group->container;
	driver = container->iommu_driver;

	if (likely(driver && driver->ops->dma_rw))
		ret = driver->ops->dma_rw(container->iommu_data,
					  iova, data, len, write);
	else
		ret = -ENOTTY;
	return ret;
}
EXPORT_SYMBOL(vfio_dma_rw);

int __init vfio_container_init(void)
{
	int ret;

	mutex_init(&vfio.iommu_drivers_lock);
	INIT_LIST_HEAD(&vfio.iommu_drivers_list);

	ret = misc_register(&vfio_dev);
	if (ret) {
		pr_err("vfio: misc device register failed\n");
		return ret;
	}

	if (IS_ENABLED(CONFIG_VFIO_NOIOMMU)) {
		ret = vfio_register_iommu_driver(&vfio_noiommu_ops);
		if (ret)
			goto err_misc;
	}
	return 0;

err_misc:
	misc_deregister(&vfio_dev);
	return ret;
}

void vfio_container_cleanup(void)
{
	if (IS_ENABLED(CONFIG_VFIO_NOIOMMU))
		vfio_unregister_iommu_driver(&vfio_noiommu_ops);
	misc_deregister(&vfio_dev);
	mutex_destroy(&vfio.iommu_drivers_lock);
}
