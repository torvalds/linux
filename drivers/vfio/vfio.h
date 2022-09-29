/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Red Hat, Inc.  All rights reserved.
 *     Author: Alex Williamson <alex.williamson@redhat.com>
 */
#ifndef __VFIO_VFIO_H__
#define __VFIO_VFIO_H__

#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/module.h>

struct iommu_group;
struct vfio_device;
struct vfio_container;

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

struct vfio_group {
	struct device 			dev;
	struct cdev			cdev;
	/*
	 * When drivers is non-zero a driver is attached to the struct device
	 * that provided the iommu_group and thus the iommu_group is a valid
	 * pointer. When drivers is 0 the driver is being detached. Once users
	 * reaches 0 then the iommu_group is invalid.
	 */
	refcount_t			drivers;
	unsigned int			container_users;
	struct iommu_group		*iommu_group;
	struct vfio_container		*container;
	struct list_head		device_list;
	struct mutex			device_lock;
	struct list_head		vfio_next;
	struct list_head		container_next;
	enum vfio_group_type		type;
	struct mutex			group_lock;
	struct kvm			*kvm;
	struct file			*opened_file;
	struct swait_queue_head		opened_file_wait;
	struct blocking_notifier_head	notifier;
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
				     dma_addr_t user_iova,
				     int npage, int prot,
				     struct page **pages);
	void		(*unpin_pages)(void *iommu_data,
				       dma_addr_t user_iova, int npage);
	void		(*register_device)(void *iommu_data,
					   struct vfio_device *vdev);
	void		(*unregister_device)(void *iommu_data,
					     struct vfio_device *vdev);
	int		(*dma_rw)(void *iommu_data, dma_addr_t user_iova,
				  void *data, size_t count, bool write);
	struct iommu_domain *(*group_iommu_domain)(void *iommu_data,
						   struct iommu_group *group);
	void		(*notify)(void *iommu_data,
				  enum vfio_iommu_notify_type event);
};

struct vfio_iommu_driver {
	const struct vfio_iommu_driver_ops	*ops;
	struct list_head			vfio_next;
};

int vfio_register_iommu_driver(const struct vfio_iommu_driver_ops *ops);
void vfio_unregister_iommu_driver(const struct vfio_iommu_driver_ops *ops);

bool vfio_assert_device_open(struct vfio_device *device);

struct vfio_container *vfio_container_from_file(struct file *filep);
int vfio_device_assign_container(struct vfio_device *device);
void vfio_device_unassign_container(struct vfio_device *device);
int vfio_container_attach_group(struct vfio_container *container,
				struct vfio_group *group);
void vfio_group_detach_container(struct vfio_group *group);
void vfio_device_container_register(struct vfio_device *device);
void vfio_device_container_unregister(struct vfio_device *device);
long vfio_container_ioctl_check_extension(struct vfio_container *container,
					  unsigned long arg);
int __init vfio_container_init(void);
void vfio_container_cleanup(void);

#ifdef CONFIG_VFIO_NOIOMMU
extern bool vfio_noiommu __read_mostly;
#else
enum { vfio_noiommu = false };
#endif

#endif
