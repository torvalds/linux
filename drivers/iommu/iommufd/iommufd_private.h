/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES
 */
#ifndef __IOMMUFD_PRIVATE_H
#define __IOMMUFD_PRIVATE_H

#include <linux/iommu.h>
#include <linux/iommufd.h>
#include <linux/iova_bitmap.h>
#include <linux/rwsem.h>
#include <linux/uaccess.h>
#include <linux/xarray.h>
#include <uapi/linux/iommufd.h>

#include "../iommu-priv.h"

struct iommu_domain;
struct iommu_group;
struct iommu_option;
struct iommufd_device;

struct iommufd_ctx {
	struct file *file;
	struct xarray objects;
	struct xarray groups;
	wait_queue_head_t destroy_wait;
	struct rw_semaphore ioas_creation_lock;

	u8 account_mode;
	/* Compatibility with VFIO no iommu */
	u8 no_iommu_mode;
	struct iommufd_ioas *vfio_ioas;
};

/*
 * The IOVA to PFN map. The map automatically copies the PFNs into multiple
 * domains and permits sharing of PFNs between io_pagetable instances. This
 * supports both a design where IOAS's are 1:1 with a domain (eg because the
 * domain is HW customized), or where the IOAS is 1:N with multiple generic
 * domains.  The io_pagetable holds an interval tree of iopt_areas which point
 * to shared iopt_pages which hold the pfns mapped to the page table.
 *
 * The locking order is domains_rwsem -> iova_rwsem -> pages::mutex
 */
struct io_pagetable {
	struct rw_semaphore domains_rwsem;
	struct xarray domains;
	struct xarray access_list;
	unsigned int next_domain_id;

	struct rw_semaphore iova_rwsem;
	struct rb_root_cached area_itree;
	/* IOVA that cannot become reserved, struct iopt_allowed */
	struct rb_root_cached allowed_itree;
	/* IOVA that cannot be allocated, struct iopt_reserved */
	struct rb_root_cached reserved_itree;
	u8 disable_large_pages;
	unsigned long iova_alignment;
};

void iopt_init_table(struct io_pagetable *iopt);
void iopt_destroy_table(struct io_pagetable *iopt);
int iopt_get_pages(struct io_pagetable *iopt, unsigned long iova,
		   unsigned long length, struct list_head *pages_list);
void iopt_free_pages_list(struct list_head *pages_list);
enum {
	IOPT_ALLOC_IOVA = 1 << 0,
};
int iopt_map_user_pages(struct iommufd_ctx *ictx, struct io_pagetable *iopt,
			unsigned long *iova, void __user *uptr,
			unsigned long length, int iommu_prot,
			unsigned int flags);
int iopt_map_file_pages(struct iommufd_ctx *ictx, struct io_pagetable *iopt,
			unsigned long *iova, struct file *file,
			unsigned long start, unsigned long length,
			int iommu_prot, unsigned int flags);
int iopt_map_pages(struct io_pagetable *iopt, struct list_head *pages_list,
		   unsigned long length, unsigned long *dst_iova,
		   int iommu_prot, unsigned int flags);
int iopt_unmap_iova(struct io_pagetable *iopt, unsigned long iova,
		    unsigned long length, unsigned long *unmapped);
int iopt_unmap_all(struct io_pagetable *iopt, unsigned long *unmapped);

int iopt_read_and_clear_dirty_data(struct io_pagetable *iopt,
				   struct iommu_domain *domain,
				   unsigned long flags,
				   struct iommu_hwpt_get_dirty_bitmap *bitmap);
int iopt_set_dirty_tracking(struct io_pagetable *iopt,
			    struct iommu_domain *domain, bool enable);

void iommufd_access_notify_unmap(struct io_pagetable *iopt, unsigned long iova,
				 unsigned long length);
int iopt_table_add_domain(struct io_pagetable *iopt,
			  struct iommu_domain *domain);
void iopt_table_remove_domain(struct io_pagetable *iopt,
			      struct iommu_domain *domain);
int iopt_table_enforce_dev_resv_regions(struct io_pagetable *iopt,
					struct device *dev,
					phys_addr_t *sw_msi_start);
int iopt_set_allow_iova(struct io_pagetable *iopt,
			struct rb_root_cached *allowed_iova);
int iopt_reserve_iova(struct io_pagetable *iopt, unsigned long start,
		      unsigned long last, void *owner);
void iopt_remove_reserved_iova(struct io_pagetable *iopt, void *owner);
int iopt_cut_iova(struct io_pagetable *iopt, unsigned long *iovas,
		  size_t num_iovas);
void iopt_enable_large_pages(struct io_pagetable *iopt);
int iopt_disable_large_pages(struct io_pagetable *iopt);

struct iommufd_ucmd {
	struct iommufd_ctx *ictx;
	void __user *ubuffer;
	u32 user_size;
	void *cmd;
};

int iommufd_vfio_ioctl(struct iommufd_ctx *ictx, unsigned int cmd,
		       unsigned long arg);

/* Copy the response in ucmd->cmd back to userspace. */
static inline int iommufd_ucmd_respond(struct iommufd_ucmd *ucmd,
				       size_t cmd_len)
{
	if (copy_to_user(ucmd->ubuffer, ucmd->cmd,
			 min_t(size_t, ucmd->user_size, cmd_len)))
		return -EFAULT;
	return 0;
}

static inline bool iommufd_lock_obj(struct iommufd_object *obj)
{
	if (!refcount_inc_not_zero(&obj->users))
		return false;
	if (!refcount_inc_not_zero(&obj->shortterm_users)) {
		/*
		 * If the caller doesn't already have a ref on obj this must be
		 * called under the xa_lock. Otherwise the caller is holding a
		 * ref on users. Thus it cannot be one before this decrement.
		 */
		refcount_dec(&obj->users);
		return false;
	}
	return true;
}

struct iommufd_object *iommufd_get_object(struct iommufd_ctx *ictx, u32 id,
					  enum iommufd_object_type type);
static inline void iommufd_put_object(struct iommufd_ctx *ictx,
				      struct iommufd_object *obj)
{
	/*
	 * Users first, then shortterm so that REMOVE_WAIT_SHORTTERM never sees
	 * a spurious !0 users with a 0 shortterm_users.
	 */
	refcount_dec(&obj->users);
	if (refcount_dec_and_test(&obj->shortterm_users))
		wake_up_interruptible_all(&ictx->destroy_wait);
}

void iommufd_object_abort(struct iommufd_ctx *ictx, struct iommufd_object *obj);
void iommufd_object_abort_and_destroy(struct iommufd_ctx *ictx,
				      struct iommufd_object *obj);
void iommufd_object_finalize(struct iommufd_ctx *ictx,
			     struct iommufd_object *obj);

enum {
	REMOVE_WAIT_SHORTTERM = 1,
};
int iommufd_object_remove(struct iommufd_ctx *ictx,
			  struct iommufd_object *to_destroy, u32 id,
			  unsigned int flags);

/*
 * The caller holds a users refcount and wants to destroy the object. At this
 * point the caller has no shortterm_users reference and at least the xarray
 * will be holding one.
 */
static inline void iommufd_object_destroy_user(struct iommufd_ctx *ictx,
					       struct iommufd_object *obj)
{
	int ret;

	ret = iommufd_object_remove(ictx, obj, obj->id, REMOVE_WAIT_SHORTTERM);

	/*
	 * If there is a bug and we couldn't destroy the object then we did put
	 * back the caller's users refcount and will eventually try to free it
	 * again during close.
	 */
	WARN_ON(ret);
}

/*
 * The HWPT allocated by autodomains is used in possibly many devices and
 * is automatically destroyed when its refcount reaches zero.
 *
 * If userspace uses the HWPT manually, even for a short term, then it will
 * disrupt this refcounting and the auto-free in the kernel will not work.
 * Userspace that tries to use the automatically allocated HWPT must be careful
 * to ensure that it is consistently destroyed, eg by not racing accesses
 * and by not attaching an automatic HWPT to a device manually.
 */
static inline void
iommufd_object_put_and_try_destroy(struct iommufd_ctx *ictx,
				   struct iommufd_object *obj)
{
	iommufd_object_remove(ictx, obj, obj->id, 0);
}

#define __iommufd_object_alloc(ictx, ptr, type, obj)                           \
	container_of(_iommufd_object_alloc(                                    \
			     ictx,                                             \
			     sizeof(*(ptr)) + BUILD_BUG_ON_ZERO(               \
						      offsetof(typeof(*(ptr)), \
							       obj) != 0),     \
			     type),                                            \
		     typeof(*(ptr)), obj)

#define iommufd_object_alloc(ictx, ptr, type) \
	__iommufd_object_alloc(ictx, ptr, type, obj)

/*
 * The IO Address Space (IOAS) pagetable is a virtual page table backed by the
 * io_pagetable object. It is a user controlled mapping of IOVA -> PFNs. The
 * mapping is copied into all of the associated domains and made available to
 * in-kernel users.
 *
 * Every iommu_domain that is created is wrapped in a iommufd_hw_pagetable
 * object. When we go to attach a device to an IOAS we need to get an
 * iommu_domain and wrapping iommufd_hw_pagetable for it.
 *
 * An iommu_domain & iommfd_hw_pagetable will be automatically selected
 * for a device based on the hwpt_list. If no suitable iommu_domain
 * is found a new iommu_domain will be created.
 */
struct iommufd_ioas {
	struct iommufd_object obj;
	struct io_pagetable iopt;
	struct mutex mutex;
	struct list_head hwpt_list;
};

static inline struct iommufd_ioas *iommufd_get_ioas(struct iommufd_ctx *ictx,
						    u32 id)
{
	return container_of(iommufd_get_object(ictx, id,
					       IOMMUFD_OBJ_IOAS),
			    struct iommufd_ioas, obj);
}

struct iommufd_ioas *iommufd_ioas_alloc(struct iommufd_ctx *ictx);
int iommufd_ioas_alloc_ioctl(struct iommufd_ucmd *ucmd);
void iommufd_ioas_destroy(struct iommufd_object *obj);
int iommufd_ioas_iova_ranges(struct iommufd_ucmd *ucmd);
int iommufd_ioas_allow_iovas(struct iommufd_ucmd *ucmd);
int iommufd_ioas_map(struct iommufd_ucmd *ucmd);
int iommufd_ioas_map_file(struct iommufd_ucmd *ucmd);
int iommufd_ioas_change_process(struct iommufd_ucmd *ucmd);
int iommufd_ioas_copy(struct iommufd_ucmd *ucmd);
int iommufd_ioas_unmap(struct iommufd_ucmd *ucmd);
int iommufd_ioas_option(struct iommufd_ucmd *ucmd);
int iommufd_option_rlimit_mode(struct iommu_option *cmd,
			       struct iommufd_ctx *ictx);

int iommufd_vfio_ioas(struct iommufd_ucmd *ucmd);
int iommufd_check_iova_range(struct io_pagetable *iopt,
			     struct iommu_hwpt_get_dirty_bitmap *bitmap);

/*
 * A HW pagetable is called an iommu_domain inside the kernel. This user object
 * allows directly creating and inspecting the domains. Domains that have kernel
 * owned page tables will be associated with an iommufd_ioas that provides the
 * IOVA to PFN map.
 */
struct iommufd_hw_pagetable {
	struct iommufd_object obj;
	struct iommu_domain *domain;
	struct iommufd_fault *fault;
};

struct iommufd_hwpt_paging {
	struct iommufd_hw_pagetable common;
	struct iommufd_ioas *ioas;
	bool auto_domain : 1;
	bool enforce_cache_coherency : 1;
	bool msi_cookie : 1;
	bool nest_parent : 1;
	/* Head at iommufd_ioas::hwpt_list */
	struct list_head hwpt_item;
};

struct iommufd_hwpt_nested {
	struct iommufd_hw_pagetable common;
	struct iommufd_hwpt_paging *parent;
	struct iommufd_viommu *viommu;
};

static inline bool hwpt_is_paging(struct iommufd_hw_pagetable *hwpt)
{
	return hwpt->obj.type == IOMMUFD_OBJ_HWPT_PAGING;
}

static inline struct iommufd_hwpt_paging *
to_hwpt_paging(struct iommufd_hw_pagetable *hwpt)
{
	return container_of(hwpt, struct iommufd_hwpt_paging, common);
}

static inline struct iommufd_hwpt_nested *
to_hwpt_nested(struct iommufd_hw_pagetable *hwpt)
{
	return container_of(hwpt, struct iommufd_hwpt_nested, common);
}

static inline struct iommufd_hwpt_paging *
find_hwpt_paging(struct iommufd_hw_pagetable *hwpt)
{
	switch (hwpt->obj.type) {
	case IOMMUFD_OBJ_HWPT_PAGING:
		return to_hwpt_paging(hwpt);
	case IOMMUFD_OBJ_HWPT_NESTED:
		return to_hwpt_nested(hwpt)->parent;
	default:
		return NULL;
	}
}

static inline struct iommufd_hwpt_paging *
iommufd_get_hwpt_paging(struct iommufd_ucmd *ucmd, u32 id)
{
	return container_of(iommufd_get_object(ucmd->ictx, id,
					       IOMMUFD_OBJ_HWPT_PAGING),
			    struct iommufd_hwpt_paging, common.obj);
}

static inline struct iommufd_hw_pagetable *
iommufd_get_hwpt_nested(struct iommufd_ucmd *ucmd, u32 id)
{
	return container_of(iommufd_get_object(ucmd->ictx, id,
					       IOMMUFD_OBJ_HWPT_NESTED),
			    struct iommufd_hw_pagetable, obj);
}

int iommufd_hwpt_set_dirty_tracking(struct iommufd_ucmd *ucmd);
int iommufd_hwpt_get_dirty_bitmap(struct iommufd_ucmd *ucmd);

struct iommufd_hwpt_paging *
iommufd_hwpt_paging_alloc(struct iommufd_ctx *ictx, struct iommufd_ioas *ioas,
			  struct iommufd_device *idev, u32 flags,
			  bool immediate_attach,
			  const struct iommu_user_data *user_data);
int iommufd_hw_pagetable_attach(struct iommufd_hw_pagetable *hwpt,
				struct iommufd_device *idev);
struct iommufd_hw_pagetable *
iommufd_hw_pagetable_detach(struct iommufd_device *idev);
void iommufd_hwpt_paging_destroy(struct iommufd_object *obj);
void iommufd_hwpt_paging_abort(struct iommufd_object *obj);
void iommufd_hwpt_nested_destroy(struct iommufd_object *obj);
void iommufd_hwpt_nested_abort(struct iommufd_object *obj);
int iommufd_hwpt_alloc(struct iommufd_ucmd *ucmd);
int iommufd_hwpt_invalidate(struct iommufd_ucmd *ucmd);

static inline void iommufd_hw_pagetable_put(struct iommufd_ctx *ictx,
					    struct iommufd_hw_pagetable *hwpt)
{
	if (hwpt->obj.type == IOMMUFD_OBJ_HWPT_PAGING) {
		struct iommufd_hwpt_paging *hwpt_paging = to_hwpt_paging(hwpt);

		lockdep_assert_not_held(&hwpt_paging->ioas->mutex);

		if (hwpt_paging->auto_domain) {
			iommufd_object_put_and_try_destroy(ictx, &hwpt->obj);
			return;
		}
	}
	refcount_dec(&hwpt->obj.users);
}

struct iommufd_group {
	struct kref ref;
	struct mutex lock;
	struct iommufd_ctx *ictx;
	struct iommu_group *group;
	struct iommufd_hw_pagetable *hwpt;
	struct list_head device_list;
	phys_addr_t sw_msi_start;
};

/*
 * A iommufd_device object represents the binding relationship between a
 * consuming driver and the iommufd. These objects are created/destroyed by
 * external drivers, not by userspace.
 */
struct iommufd_device {
	struct iommufd_object obj;
	struct iommufd_ctx *ictx;
	struct iommufd_group *igroup;
	struct list_head group_item;
	/* always the physical device */
	struct device *dev;
	bool enforce_cache_coherency;
	/* protect iopf_enabled counter */
	struct mutex iopf_lock;
	unsigned int iopf_enabled;
};

static inline struct iommufd_device *
iommufd_get_device(struct iommufd_ucmd *ucmd, u32 id)
{
	return container_of(iommufd_get_object(ucmd->ictx, id,
					       IOMMUFD_OBJ_DEVICE),
			    struct iommufd_device, obj);
}

void iommufd_device_destroy(struct iommufd_object *obj);
int iommufd_get_hw_info(struct iommufd_ucmd *ucmd);

struct iommufd_access {
	struct iommufd_object obj;
	struct iommufd_ctx *ictx;
	struct iommufd_ioas *ioas;
	struct iommufd_ioas *ioas_unpin;
	struct mutex ioas_lock;
	const struct iommufd_access_ops *ops;
	void *data;
	unsigned long iova_alignment;
	u32 iopt_access_list_id;
};

int iopt_add_access(struct io_pagetable *iopt, struct iommufd_access *access);
void iopt_remove_access(struct io_pagetable *iopt,
			struct iommufd_access *access,
			u32 iopt_access_list_id);
void iommufd_access_destroy_object(struct iommufd_object *obj);

/*
 * An iommufd_fault object represents an interface to deliver I/O page faults
 * to the user space. These objects are created/destroyed by the user space and
 * associated with hardware page table objects during page-table allocation.
 */
struct iommufd_fault {
	struct iommufd_object obj;
	struct iommufd_ctx *ictx;
	struct file *filep;

	/* The lists of outstanding faults protected by below mutex. */
	struct mutex mutex;
	struct list_head deliver;
	struct xarray response;

	struct wait_queue_head wait_queue;
};

struct iommufd_attach_handle {
	struct iommu_attach_handle handle;
	struct iommufd_device *idev;
};

/* Convert an iommu attach handle to iommufd handle. */
#define to_iommufd_handle(hdl)	container_of(hdl, struct iommufd_attach_handle, handle)

static inline struct iommufd_fault *
iommufd_get_fault(struct iommufd_ucmd *ucmd, u32 id)
{
	return container_of(iommufd_get_object(ucmd->ictx, id,
					       IOMMUFD_OBJ_FAULT),
			    struct iommufd_fault, obj);
}

int iommufd_fault_alloc(struct iommufd_ucmd *ucmd);
void iommufd_fault_destroy(struct iommufd_object *obj);
int iommufd_fault_iopf_handler(struct iopf_group *group);

int iommufd_fault_domain_attach_dev(struct iommufd_hw_pagetable *hwpt,
				    struct iommufd_device *idev);
void iommufd_fault_domain_detach_dev(struct iommufd_hw_pagetable *hwpt,
				     struct iommufd_device *idev);
int iommufd_fault_domain_replace_dev(struct iommufd_device *idev,
				     struct iommufd_hw_pagetable *hwpt,
				     struct iommufd_hw_pagetable *old);

static inline int iommufd_hwpt_attach_device(struct iommufd_hw_pagetable *hwpt,
					     struct iommufd_device *idev)
{
	if (hwpt->fault)
		return iommufd_fault_domain_attach_dev(hwpt, idev);

	return iommu_attach_group(hwpt->domain, idev->igroup->group);
}

static inline void iommufd_hwpt_detach_device(struct iommufd_hw_pagetable *hwpt,
					      struct iommufd_device *idev)
{
	if (hwpt->fault) {
		iommufd_fault_domain_detach_dev(hwpt, idev);
		return;
	}

	iommu_detach_group(hwpt->domain, idev->igroup->group);
}

static inline int iommufd_hwpt_replace_device(struct iommufd_device *idev,
					      struct iommufd_hw_pagetable *hwpt,
					      struct iommufd_hw_pagetable *old)
{
	if (old->fault || hwpt->fault)
		return iommufd_fault_domain_replace_dev(idev, hwpt, old);

	return iommu_group_replace_domain(idev->igroup->group, hwpt->domain);
}

static inline struct iommufd_viommu *
iommufd_get_viommu(struct iommufd_ucmd *ucmd, u32 id)
{
	return container_of(iommufd_get_object(ucmd->ictx, id,
					       IOMMUFD_OBJ_VIOMMU),
			    struct iommufd_viommu, obj);
}

int iommufd_viommu_alloc_ioctl(struct iommufd_ucmd *ucmd);
void iommufd_viommu_destroy(struct iommufd_object *obj);
int iommufd_vdevice_alloc_ioctl(struct iommufd_ucmd *ucmd);
void iommufd_vdevice_destroy(struct iommufd_object *obj);

struct iommufd_vdevice {
	struct iommufd_object obj;
	struct iommufd_ctx *ictx;
	struct iommufd_viommu *viommu;
	struct device *dev;
	u64 id; /* per-vIOMMU virtual ID */
};

#ifdef CONFIG_IOMMUFD_TEST
int iommufd_test(struct iommufd_ucmd *ucmd);
void iommufd_selftest_destroy(struct iommufd_object *obj);
extern size_t iommufd_test_memory_limit;
void iommufd_test_syz_conv_iova_id(struct iommufd_ucmd *ucmd,
				   unsigned int ioas_id, u64 *iova, u32 *flags);
bool iommufd_should_fail(void);
int __init iommufd_test_init(void);
void iommufd_test_exit(void);
bool iommufd_selftest_is_mock_dev(struct device *dev);
#else
static inline void iommufd_test_syz_conv_iova_id(struct iommufd_ucmd *ucmd,
						 unsigned int ioas_id,
						 u64 *iova, u32 *flags)
{
}
static inline bool iommufd_should_fail(void)
{
	return false;
}
static inline int __init iommufd_test_init(void)
{
	return 0;
}
static inline void iommufd_test_exit(void)
{
}
static inline bool iommufd_selftest_is_mock_dev(struct device *dev)
{
	return false;
}
#endif
#endif
