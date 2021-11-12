/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Red Hat, Inc.  All rights reserved.
 *     Author: Alex Williamson <alex.williamson@redhat.com>
 */

enum vfio_group_type {
	/*
	 * Physical device with IOMMU backing.
	 */
	VFIO_IOMMU,

	/*
	 * Virtual device without IOMMU backing. The VFIO core fakes up an
	 * iommu_group as the iommu_group sysfs interface is part of the
	 * userspace ABI.  The user of these devices must not be able to
	 * directly trigger unmediated DMA.
	 */
	VFIO_EMULATED_IOMMU,

	/*
	 * Physical device without IOMMU backing. The VFIO core fakes up an
	 * iommu_group as the iommu_group sysfs interface is part of the
	 * userspace ABI.  Users can trigger unmediated DMA by the device,
	 * usage is highly dangerous, requires an explicit opt-in and will
	 * taint the kernel.
	 */
	VFIO_NO_IOMMU,
};

/* events for the backend driver notify callback */
enum vfio_iommu_notify_type {
	VFIO_IOMMU_CONTAINER_CLOSE = 0,
};

/**
 * struct vfio_iommu_driver_ops - VFIO IOMMU driver callbacks
 */
struct vfio_iommu_driver_ops {
	char		*name;
	struct module	*owner;
	void		*(*open)(unsigned long arg);
	void		(*release)(void *iommu_data);
	long		(*ioctl)(void *iommu_data, unsigned int cmd,
				 unsigned long arg);
	int		(*attach_group)(void *iommu_data,
					struct iommu_group *group,
					enum vfio_group_type);
	void		(*detach_group)(void *iommu_data,
					struct iommu_group *group);
	int		(*pin_pages)(void *iommu_data,
				     struct iommu_group *group,
				     unsigned long *user_pfn,
				     int npage, int prot,
				     unsigned long *phys_pfn);
	int		(*unpin_pages)(void *iommu_data,
				       unsigned long *user_pfn, int npage);
	int		(*register_notifier)(void *iommu_data,
					     unsigned long *events,
					     struct notifier_block *nb);
	int		(*unregister_notifier)(void *iommu_data,
					       struct notifier_block *nb);
	int		(*dma_rw)(void *iommu_data, dma_addr_t user_iova,
				  void *data, size_t count, bool write);
	struct iommu_domain *(*group_iommu_domain)(void *iommu_data,
						   struct iommu_group *group);
	void		(*notify)(void *iommu_data,
				  enum vfio_iommu_notify_type event);
};

int vfio_register_iommu_driver(const struct vfio_iommu_driver_ops *ops);
void vfio_unregister_iommu_driver(const struct vfio_iommu_driver_ops *ops);
