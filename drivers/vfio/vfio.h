/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Red Hat, Inc.  All rights reserved.
 *     Author: Alex Williamson <alex.williamson@redhat.com>
 */
#ifndef __VFIO_VFIO_H__
#define __VFIO_VFIO_H__

#include <linux/file.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/module.h>
#include <linux/vfio.h>

struct iommufd_ctx;
struct iommu_group;
struct vfio_container;

void vfio_device_put_registration(struct vfio_device *device);
bool vfio_device_try_get_registration(struct vfio_device *device);
int vfio_device_open(struct vfio_device *device, struct iommufd_ctx *iommufd);
void vfio_device_close(struct vfio_device *device,
		       struct iommufd_ctx *iommufd);

extern const struct file_operations vfio_device_fops;

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
#if IS_ENABLED(CONFIG_VFIO_CONTAINER)
	struct list_head		container_next;
#endif
	enum vfio_group_type		type;
	struct mutex			group_lock;
	struct kvm			*kvm;
	struct file			*opened_file;
	struct blocking_notifier_head	notifier;
	struct iommufd_ctx		*iommufd;
	spinlock_t			kvm_ref_lock;
};

int vfio_device_set_group(struct vfio_device *device,
			  enum vfio_group_type type);
void vfio_device_remove_group(struct vfio_device *device);
void vfio_device_group_register(struct vfio_device *device);
void vfio_device_group_unregister(struct vfio_device *device);
int vfio_device_group_use_iommu(struct vfio_device *device);
void vfio_device_group_unuse_iommu(struct vfio_device *device);
void vfio_device_group_close(struct vfio_device *device);
bool vfio_device_has_container(struct vfio_device *device);
int __init vfio_group_init(void);
void vfio_group_cleanup(void);

static inline bool vfio_device_is_noiommu(struct vfio_device *vdev)
{
	return IS_ENABLED(CONFIG_VFIO_NOIOMMU) &&
	       vdev->group->type == VFIO_NO_IOMMU;
}

#if IS_ENABLED(CONFIG_VFIO_CONTAINER)
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
};

struct vfio_iommu_driver {
	const struct vfio_iommu_driver_ops	*ops;
	struct list_head			vfio_next;
};

int vfio_register_iommu_driver(const struct vfio_iommu_driver_ops *ops);
void vfio_unregister_iommu_driver(const struct vfio_iommu_driver_ops *ops);

struct vfio_container *vfio_container_from_file(struct file *filep);
int vfio_group_use_container(struct vfio_group *group);
void vfio_group_unuse_container(struct vfio_group *group);
int vfio_container_attach_group(struct vfio_container *container,
				struct vfio_group *group);
void vfio_group_detach_container(struct vfio_group *group);
void vfio_device_container_register(struct vfio_device *device);
void vfio_device_container_unregister(struct vfio_device *device);
int vfio_device_container_pin_pages(struct vfio_device *device,
				    dma_addr_t iova, int npage,
				    int prot, struct page **pages);
void vfio_device_container_unpin_pages(struct vfio_device *device,
				       dma_addr_t iova, int npage);
int vfio_device_container_dma_rw(struct vfio_device *device,
				 dma_addr_t iova, void *data,
				 size_t len, bool write);

int __init vfio_container_init(void);
void vfio_container_cleanup(void);
#else
static inline struct vfio_container *
vfio_container_from_file(struct file *filep)
{
	return NULL;
}

static inline int vfio_group_use_container(struct vfio_group *group)
{
	return -EOPNOTSUPP;
}

static inline void vfio_group_unuse_container(struct vfio_group *group)
{
}

static inline int vfio_container_attach_group(struct vfio_container *container,
					      struct vfio_group *group)
{
	return -EOPNOTSUPP;
}

static inline void vfio_group_detach_container(struct vfio_group *group)
{
}

static inline void vfio_device_container_register(struct vfio_device *device)
{
}

static inline void vfio_device_container_unregister(struct vfio_device *device)
{
}

static inline int vfio_device_container_pin_pages(struct vfio_device *device,
						  dma_addr_t iova, int npage,
						  int prot, struct page **pages)
{
	return -EOPNOTSUPP;
}

static inline void vfio_device_container_unpin_pages(struct vfio_device *device,
						     dma_addr_t iova, int npage)
{
}

static inline int vfio_device_container_dma_rw(struct vfio_device *device,
					       dma_addr_t iova, void *data,
					       size_t len, bool write)
{
	return -EOPNOTSUPP;
}

static inline int vfio_container_init(void)
{
	return 0;
}
static inline void vfio_container_cleanup(void)
{
}
#endif

#if IS_ENABLED(CONFIG_IOMMUFD)
int vfio_iommufd_bind(struct vfio_device *device, struct iommufd_ctx *ictx);
void vfio_iommufd_unbind(struct vfio_device *device);
#else
static inline int vfio_iommufd_bind(struct vfio_device *device,
				    struct iommufd_ctx *ictx)
{
	return -EOPNOTSUPP;
}

static inline void vfio_iommufd_unbind(struct vfio_device *device)
{
}
#endif

#if IS_ENABLED(CONFIG_VFIO_VIRQFD)
int __init vfio_virqfd_init(void);
void vfio_virqfd_exit(void);
#else
static inline int __init vfio_virqfd_init(void)
{
	return 0;
}
static inline void vfio_virqfd_exit(void)
{
}
#endif

#ifdef CONFIG_VFIO_NOIOMMU
extern bool vfio_noiommu __read_mostly;
#else
enum { vfio_noiommu = false };
#endif

#ifdef CONFIG_HAVE_KVM
void _vfio_device_get_kvm_safe(struct vfio_device *device, struct kvm *kvm);
void vfio_device_put_kvm(struct vfio_device *device);
#else
static inline void _vfio_device_get_kvm_safe(struct vfio_device *device,
					     struct kvm *kvm)
{
}

static inline void vfio_device_put_kvm(struct vfio_device *device)
{
}
#endif

#endif
