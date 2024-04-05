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

struct vfio_device_file {
	struct vfio_device *device;
	struct vfio_group *group;

	u8 access_granted;
	u32 devid; /* only valid when iommufd is valid */
	spinlock_t kvm_ref_lock; /* protect kvm field */
	struct kvm *kvm;
	struct iommufd_ctx *iommufd; /* protected by struct vfio_device_set::lock */
};

void vfio_device_put_registration(struct vfio_device *device);
bool vfio_device_try_get_registration(struct vfio_device *device);
int vfio_df_open(struct vfio_device_file *df);
void vfio_df_close(struct vfio_device_file *df);
struct vfio_device_file *
vfio_allocate_device_file(struct vfio_device *device);

extern const struct file_operations vfio_device_fops;

#ifdef CONFIG_VFIO_NOIOMMU
extern bool vfio_noiommu __read_mostly;
#else
enum { vfio_noiommu = false };
#endif

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

#if IS_ENABLED(CONFIG_VFIO_GROUP)
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
	unsigned int			cdev_device_open_cnt;
};

int vfio_device_block_group(struct vfio_device *device);
void vfio_device_unblock_group(struct vfio_device *device);
int vfio_device_set_group(struct vfio_device *device,
			  enum vfio_group_type type);
void vfio_device_remove_group(struct vfio_device *device);
void vfio_device_group_register(struct vfio_device *device);
void vfio_device_group_unregister(struct vfio_device *device);
int vfio_device_group_use_iommu(struct vfio_device *device);
void vfio_device_group_unuse_iommu(struct vfio_device *device);
void vfio_df_group_close(struct vfio_device_file *df);
struct vfio_group *vfio_group_from_file(struct file *file);
bool vfio_group_enforced_coherent(struct vfio_group *group);
void vfio_group_set_kvm(struct vfio_group *group, struct kvm *kvm);
bool vfio_device_has_container(struct vfio_device *device);
int __init vfio_group_init(void);
void vfio_group_cleanup(void);

static inline bool vfio_device_is_noiommu(struct vfio_device *vdev)
{
	return IS_ENABLED(CONFIG_VFIO_NOIOMMU) &&
	       vdev->group->type == VFIO_NO_IOMMU;
}
#else
struct vfio_group;

static inline int vfio_device_block_group(struct vfio_device *device)
{
	return 0;
}

static inline void vfio_device_unblock_group(struct vfio_device *device)
{
}

static inline int vfio_device_set_group(struct vfio_device *device,
					enum vfio_group_type type)
{
	return 0;
}

static inline void vfio_device_remove_group(struct vfio_device *device)
{
}

static inline void vfio_device_group_register(struct vfio_device *device)
{
}

static inline void vfio_device_group_unregister(struct vfio_device *device)
{
}

static inline int vfio_device_group_use_iommu(struct vfio_device *device)
{
	return -EOPNOTSUPP;
}

static inline void vfio_device_group_unuse_iommu(struct vfio_device *device)
{
}

static inline void vfio_df_group_close(struct vfio_device_file *df)
{
}

static inline struct vfio_group *vfio_group_from_file(struct file *file)
{
	return NULL;
}

static inline bool vfio_group_enforced_coherent(struct vfio_group *group)
{
	return true;
}

static inline void vfio_group_set_kvm(struct vfio_group *group, struct kvm *kvm)
{
}

static inline bool vfio_device_has_container(struct vfio_device *device)
{
	return false;
}

static inline int __init vfio_group_init(void)
{
	return 0;
}

static inline void vfio_group_cleanup(void)
{
}

static inline bool vfio_device_is_noiommu(struct vfio_device *vdev)
{
	return false;
}
#endif /* CONFIG_VFIO_GROUP */

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
bool vfio_iommufd_device_has_compat_ioas(struct vfio_device *vdev,
					 struct iommufd_ctx *ictx);
int vfio_df_iommufd_bind(struct vfio_device_file *df);
void vfio_df_iommufd_unbind(struct vfio_device_file *df);
int vfio_iommufd_compat_attach_ioas(struct vfio_device *device,
				    struct iommufd_ctx *ictx);
#else
static inline bool
vfio_iommufd_device_has_compat_ioas(struct vfio_device *vdev,
				    struct iommufd_ctx *ictx)
{
	return false;
}

static inline int vfio_df_iommufd_bind(struct vfio_device_file *fd)
{
	return -EOPNOTSUPP;
}

static inline void vfio_df_iommufd_unbind(struct vfio_device_file *df)
{
}

static inline int
vfio_iommufd_compat_attach_ioas(struct vfio_device *device,
				struct iommufd_ctx *ictx)
{
	return -EOPNOTSUPP;
}
#endif

int vfio_df_ioctl_attach_pt(struct vfio_device_file *df,
			    struct vfio_device_attach_iommufd_pt __user *arg);
int vfio_df_ioctl_detach_pt(struct vfio_device_file *df,
			    struct vfio_device_detach_iommufd_pt __user *arg);

#if IS_ENABLED(CONFIG_VFIO_DEVICE_CDEV)
void vfio_init_device_cdev(struct vfio_device *device);

static inline int vfio_device_add(struct vfio_device *device)
{
	/* cdev does not support noiommu device */
	if (vfio_device_is_noiommu(device))
		return device_add(&device->device);
	vfio_init_device_cdev(device);
	return cdev_device_add(&device->cdev, &device->device);
}

static inline void vfio_device_del(struct vfio_device *device)
{
	if (vfio_device_is_noiommu(device))
		device_del(&device->device);
	else
		cdev_device_del(&device->cdev, &device->device);
}

int vfio_device_fops_cdev_open(struct inode *inode, struct file *filep);
long vfio_df_ioctl_bind_iommufd(struct vfio_device_file *df,
				struct vfio_device_bind_iommufd __user *arg);
void vfio_df_unbind_iommufd(struct vfio_device_file *df);
int vfio_cdev_init(struct class *device_class);
void vfio_cdev_cleanup(void);
#else
static inline void vfio_init_device_cdev(struct vfio_device *device)
{
}

static inline int vfio_device_add(struct vfio_device *device)
{
	return device_add(&device->device);
}

static inline void vfio_device_del(struct vfio_device *device)
{
	device_del(&device->device);
}

static inline int vfio_device_fops_cdev_open(struct inode *inode,
					     struct file *filep)
{
	return 0;
}

static inline long vfio_df_ioctl_bind_iommufd(struct vfio_device_file *df,
					      struct vfio_device_bind_iommufd __user *arg)
{
	return -ENOTTY;
}

static inline void vfio_df_unbind_iommufd(struct vfio_device_file *df)
{
}

static inline int vfio_cdev_init(struct class *device_class)
{
	return 0;
}

static inline void vfio_cdev_cleanup(void)
{
}
#endif /* CONFIG_VFIO_DEVICE_CDEV */

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

#if IS_ENABLED(CONFIG_KVM)
void vfio_device_get_kvm_safe(struct vfio_device *device, struct kvm *kvm);
void vfio_device_put_kvm(struct vfio_device *device);
#else
static inline void vfio_device_get_kvm_safe(struct vfio_device *device,
					    struct kvm *kvm)
{
}

static inline void vfio_device_put_kvm(struct vfio_device *device)
{
}
#endif

#ifdef CONFIG_VFIO_DEBUGFS
void vfio_debugfs_create_root(void);
void vfio_debugfs_remove_root(void);

void vfio_device_debugfs_init(struct vfio_device *vdev);
void vfio_device_debugfs_exit(struct vfio_device *vdev);
#else
static inline void vfio_debugfs_create_root(void) { }
static inline void vfio_debugfs_remove_root(void) { }

static inline void vfio_device_debugfs_init(struct vfio_device *vdev) { }
static inline void vfio_device_debugfs_exit(struct vfio_device *vdev) { }
#endif /* CONFIG_VFIO_DEBUGFS */

#endif
