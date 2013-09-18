/*
 * VFIO: IOMMU DMA mapping support for TCE on POWER
 *
 * Copyright (C) 2013 IBM Corp.  All rights reserved.
 *     Author: Alexey Kardashevskiy <aik@ozlabs.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Derived from original vfio_iommu_type1.c:
 * Copyright (C) 2012 Red Hat, Inc.  All rights reserved.
 *     Author: Alex Williamson <alex.williamson@redhat.com>
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/err.h>
#include <linux/vfio.h>
#include <asm/iommu.h>
#include <asm/tce.h>

#define DRIVER_VERSION  "0.1"
#define DRIVER_AUTHOR   "aik@ozlabs.ru"
#define DRIVER_DESC     "VFIO IOMMU SPAPR TCE"

static void tce_iommu_detach_group(void *iommu_data,
		struct iommu_group *iommu_group);

/*
 * VFIO IOMMU fd for SPAPR_TCE IOMMU implementation
 *
 * This code handles mapping and unmapping of user data buffers
 * into DMA'ble space using the IOMMU
 */

/*
 * The container descriptor supports only a single group per container.
 * Required by the API as the container is not supplied with the IOMMU group
 * at the moment of initialization.
 */
struct tce_container {
	struct mutex lock;
	struct iommu_table *tbl;
	bool enabled;
};

static int tce_iommu_enable(struct tce_container *container)
{
	int ret = 0;
	unsigned long locked, lock_limit, npages;
	struct iommu_table *tbl = container->tbl;

	if (!container->tbl)
		return -ENXIO;

	if (!current->mm)
		return -ESRCH; /* process exited */

	if (container->enabled)
		return -EBUSY;

	/*
	 * When userspace pages are mapped into the IOMMU, they are effectively
	 * locked memory, so, theoretically, we need to update the accounting
	 * of locked pages on each map and unmap.  For powerpc, the map unmap
	 * paths can be very hot, though, and the accounting would kill
	 * performance, especially since it would be difficult to impossible
	 * to handle the accounting in real mode only.
	 *
	 * To address that, rather than precisely accounting every page, we
	 * instead account for a worst case on locked memory when the iommu is
	 * enabled and disabled.  The worst case upper bound on locked memory
	 * is the size of the whole iommu window, which is usually relatively
	 * small (compared to total memory sizes) on POWER hardware.
	 *
	 * Also we don't have a nice way to fail on H_PUT_TCE due to ulimits,
	 * that would effectively kill the guest at random points, much better
	 * enforcing the limit based on the max that the guest can map.
	 */
	down_write(&current->mm->mmap_sem);
	npages = (tbl->it_size << IOMMU_PAGE_SHIFT) >> PAGE_SHIFT;
	locked = current->mm->locked_vm + npages;
	lock_limit = rlimit(RLIMIT_MEMLOCK) >> PAGE_SHIFT;
	if (locked > lock_limit && !capable(CAP_IPC_LOCK)) {
		pr_warn("RLIMIT_MEMLOCK (%ld) exceeded\n",
				rlimit(RLIMIT_MEMLOCK));
		ret = -ENOMEM;
	} else {

		current->mm->locked_vm += npages;
		container->enabled = true;
	}
	up_write(&current->mm->mmap_sem);

	return ret;
}

static void tce_iommu_disable(struct tce_container *container)
{
	if (!container->enabled)
		return;

	container->enabled = false;

	if (!container->tbl || !current->mm)
		return;

	down_write(&current->mm->mmap_sem);
	current->mm->locked_vm -= (container->tbl->it_size <<
			IOMMU_PAGE_SHIFT) >> PAGE_SHIFT;
	up_write(&current->mm->mmap_sem);
}

static void *tce_iommu_open(unsigned long arg)
{
	struct tce_container *container;

	if (arg != VFIO_SPAPR_TCE_IOMMU) {
		pr_err("tce_vfio: Wrong IOMMU type\n");
		return ERR_PTR(-EINVAL);
	}

	container = kzalloc(sizeof(*container), GFP_KERNEL);
	if (!container)
		return ERR_PTR(-ENOMEM);

	mutex_init(&container->lock);

	return container;
}

static void tce_iommu_release(void *iommu_data)
{
	struct tce_container *container = iommu_data;

	WARN_ON(container->tbl && !container->tbl->it_group);
	tce_iommu_disable(container);

	if (container->tbl && container->tbl->it_group)
		tce_iommu_detach_group(iommu_data, container->tbl->it_group);

	mutex_destroy(&container->lock);

	kfree(container);
}

static long tce_iommu_ioctl(void *iommu_data,
				 unsigned int cmd, unsigned long arg)
{
	struct tce_container *container = iommu_data;
	unsigned long minsz;
	long ret;

	switch (cmd) {
	case VFIO_CHECK_EXTENSION:
		return (arg == VFIO_SPAPR_TCE_IOMMU) ? 1 : 0;

	case VFIO_IOMMU_SPAPR_TCE_GET_INFO: {
		struct vfio_iommu_spapr_tce_info info;
		struct iommu_table *tbl = container->tbl;

		if (WARN_ON(!tbl))
			return -ENXIO;

		minsz = offsetofend(struct vfio_iommu_spapr_tce_info,
				dma32_window_size);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		info.dma32_window_start = tbl->it_offset << IOMMU_PAGE_SHIFT;
		info.dma32_window_size = tbl->it_size << IOMMU_PAGE_SHIFT;
		info.flags = 0;

		if (copy_to_user((void __user *)arg, &info, minsz))
			return -EFAULT;

		return 0;
	}
	case VFIO_IOMMU_MAP_DMA: {
		struct vfio_iommu_type1_dma_map param;
		struct iommu_table *tbl = container->tbl;
		unsigned long tce, i;

		if (!tbl)
			return -ENXIO;

		BUG_ON(!tbl->it_group);

		minsz = offsetofend(struct vfio_iommu_type1_dma_map, size);

		if (copy_from_user(&param, (void __user *)arg, minsz))
			return -EFAULT;

		if (param.argsz < minsz)
			return -EINVAL;

		if (param.flags & ~(VFIO_DMA_MAP_FLAG_READ |
				VFIO_DMA_MAP_FLAG_WRITE))
			return -EINVAL;

		if ((param.size & ~IOMMU_PAGE_MASK) ||
				(param.vaddr & ~IOMMU_PAGE_MASK))
			return -EINVAL;

		/* iova is checked by the IOMMU API */
		tce = param.vaddr;
		if (param.flags & VFIO_DMA_MAP_FLAG_READ)
			tce |= TCE_PCI_READ;
		if (param.flags & VFIO_DMA_MAP_FLAG_WRITE)
			tce |= TCE_PCI_WRITE;

		ret = iommu_tce_put_param_check(tbl, param.iova, tce);
		if (ret)
			return ret;

		for (i = 0; i < (param.size >> IOMMU_PAGE_SHIFT); ++i) {
			ret = iommu_put_tce_user_mode(tbl,
					(param.iova >> IOMMU_PAGE_SHIFT) + i,
					tce);
			if (ret)
				break;
			tce += IOMMU_PAGE_SIZE;
		}
		if (ret)
			iommu_clear_tces_and_put_pages(tbl,
					param.iova >> IOMMU_PAGE_SHIFT,	i);

		iommu_flush_tce(tbl);

		return ret;
	}
	case VFIO_IOMMU_UNMAP_DMA: {
		struct vfio_iommu_type1_dma_unmap param;
		struct iommu_table *tbl = container->tbl;

		if (WARN_ON(!tbl))
			return -ENXIO;

		minsz = offsetofend(struct vfio_iommu_type1_dma_unmap,
				size);

		if (copy_from_user(&param, (void __user *)arg, minsz))
			return -EFAULT;

		if (param.argsz < minsz)
			return -EINVAL;

		/* No flag is supported now */
		if (param.flags)
			return -EINVAL;

		if (param.size & ~IOMMU_PAGE_MASK)
			return -EINVAL;

		ret = iommu_tce_clear_param_check(tbl, param.iova, 0,
				param.size >> IOMMU_PAGE_SHIFT);
		if (ret)
			return ret;

		ret = iommu_clear_tces_and_put_pages(tbl,
				param.iova >> IOMMU_PAGE_SHIFT,
				param.size >> IOMMU_PAGE_SHIFT);
		iommu_flush_tce(tbl);

		return ret;
	}
	case VFIO_IOMMU_ENABLE:
		mutex_lock(&container->lock);
		ret = tce_iommu_enable(container);
		mutex_unlock(&container->lock);
		return ret;


	case VFIO_IOMMU_DISABLE:
		mutex_lock(&container->lock);
		tce_iommu_disable(container);
		mutex_unlock(&container->lock);
		return 0;
	}

	return -ENOTTY;
}

static int tce_iommu_attach_group(void *iommu_data,
		struct iommu_group *iommu_group)
{
	int ret;
	struct tce_container *container = iommu_data;
	struct iommu_table *tbl = iommu_group_get_iommudata(iommu_group);

	BUG_ON(!tbl);
	mutex_lock(&container->lock);

	/* pr_debug("tce_vfio: Attaching group #%u to iommu %p\n",
			iommu_group_id(iommu_group), iommu_group); */
	if (container->tbl) {
		pr_warn("tce_vfio: Only one group per IOMMU container is allowed, existing id=%d, attaching id=%d\n",
				iommu_group_id(container->tbl->it_group),
				iommu_group_id(iommu_group));
		ret = -EBUSY;
	} else if (container->enabled) {
		pr_err("tce_vfio: attaching group #%u to enabled container\n",
				iommu_group_id(iommu_group));
		ret = -EBUSY;
	} else {
		ret = iommu_take_ownership(tbl);
		if (!ret)
			container->tbl = tbl;
	}

	mutex_unlock(&container->lock);

	return ret;
}

static void tce_iommu_detach_group(void *iommu_data,
		struct iommu_group *iommu_group)
{
	struct tce_container *container = iommu_data;
	struct iommu_table *tbl = iommu_group_get_iommudata(iommu_group);

	BUG_ON(!tbl);
	mutex_lock(&container->lock);
	if (tbl != container->tbl) {
		pr_warn("tce_vfio: detaching group #%u, expected group is #%u\n",
				iommu_group_id(iommu_group),
				iommu_group_id(tbl->it_group));
	} else {
		if (container->enabled) {
			pr_warn("tce_vfio: detaching group #%u from enabled container, forcing disable\n",
					iommu_group_id(tbl->it_group));
			tce_iommu_disable(container);
		}

		/* pr_debug("tce_vfio: detaching group #%u from iommu %p\n",
				iommu_group_id(iommu_group), iommu_group); */
		container->tbl = NULL;
		iommu_release_ownership(tbl);
	}
	mutex_unlock(&container->lock);
}

const struct vfio_iommu_driver_ops tce_iommu_driver_ops = {
	.name		= "iommu-vfio-powerpc",
	.owner		= THIS_MODULE,
	.open		= tce_iommu_open,
	.release	= tce_iommu_release,
	.ioctl		= tce_iommu_ioctl,
	.attach_group	= tce_iommu_attach_group,
	.detach_group	= tce_iommu_detach_group,
};

static int __init tce_iommu_init(void)
{
	return vfio_register_iommu_driver(&tce_iommu_driver_ops);
}

static void __exit tce_iommu_cleanup(void)
{
	vfio_unregister_iommu_driver(&tce_iommu_driver_ops);
}

module_init(tce_iommu_init);
module_exit(tce_iommu_cleanup);

MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);

