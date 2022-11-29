/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES
 */
#ifndef __IOMMUFD_PRIVATE_H
#define __IOMMUFD_PRIVATE_H

#include <linux/rwsem.h>
#include <linux/xarray.h>
#include <linux/refcount.h>
#include <linux/uaccess.h>

struct iommufd_ctx {
	struct file *file;
	struct xarray objects;
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
	unsigned int next_domain_id;

	struct rw_semaphore iova_rwsem;
	struct rb_root_cached area_itree;
	/* IOVA that cannot become reserved, struct iopt_allowed */
	struct rb_root_cached allowed_itree;
	/* IOVA that cannot be allocated, struct iopt_reserved */
	struct rb_root_cached reserved_itree;
	u8 disable_large_pages;
};

struct iommufd_ucmd {
	struct iommufd_ctx *ictx;
	void __user *ubuffer;
	u32 user_size;
	void *cmd;
};

/* Copy the response in ucmd->cmd back to userspace. */
static inline int iommufd_ucmd_respond(struct iommufd_ucmd *ucmd,
				       size_t cmd_len)
{
	if (copy_to_user(ucmd->ubuffer, ucmd->cmd,
			 min_t(size_t, ucmd->user_size, cmd_len)))
		return -EFAULT;
	return 0;
}

enum iommufd_object_type {
	IOMMUFD_OBJ_NONE,
	IOMMUFD_OBJ_ANY = IOMMUFD_OBJ_NONE,
};

/* Base struct for all objects with a userspace ID handle. */
struct iommufd_object {
	struct rw_semaphore destroy_rwsem;
	refcount_t users;
	enum iommufd_object_type type;
	unsigned int id;
};

static inline bool iommufd_lock_obj(struct iommufd_object *obj)
{
	if (!down_read_trylock(&obj->destroy_rwsem))
		return false;
	if (!refcount_inc_not_zero(&obj->users)) {
		up_read(&obj->destroy_rwsem);
		return false;
	}
	return true;
}

struct iommufd_object *iommufd_get_object(struct iommufd_ctx *ictx, u32 id,
					  enum iommufd_object_type type);
static inline void iommufd_put_object(struct iommufd_object *obj)
{
	refcount_dec(&obj->users);
	up_read(&obj->destroy_rwsem);
}

/**
 * iommufd_ref_to_users() - Switch from destroy_rwsem to users refcount
 *        protection
 * @obj - Object to release
 *
 * Objects have two refcount protections (destroy_rwsem and the refcount_t
 * users). Holding either of these will prevent the object from being destroyed.
 *
 * Depending on the use case, one protection or the other is appropriate.  In
 * most cases references are being protected by the destroy_rwsem. This allows
 * orderly destruction of the object because iommufd_object_destroy_user() will
 * wait for it to become unlocked. However, as a rwsem, it cannot be held across
 * a system call return. So cases that have longer term needs must switch
 * to the weaker users refcount_t.
 *
 * With users protection iommufd_object_destroy_user() will return false,
 * refusing to destroy the object, causing -EBUSY to userspace.
 */
static inline void iommufd_ref_to_users(struct iommufd_object *obj)
{
	up_read(&obj->destroy_rwsem);
	/* iommufd_lock_obj() obtains users as well */
}
void iommufd_object_abort(struct iommufd_ctx *ictx, struct iommufd_object *obj);
void iommufd_object_abort_and_destroy(struct iommufd_ctx *ictx,
				      struct iommufd_object *obj);
void iommufd_object_finalize(struct iommufd_ctx *ictx,
			     struct iommufd_object *obj);
bool iommufd_object_destroy_user(struct iommufd_ctx *ictx,
				 struct iommufd_object *obj);
struct iommufd_object *_iommufd_object_alloc(struct iommufd_ctx *ictx,
					     size_t size,
					     enum iommufd_object_type type);

#define iommufd_object_alloc(ictx, ptr, type)                                  \
	container_of(_iommufd_object_alloc(                                    \
			     ictx,                                             \
			     sizeof(*(ptr)) + BUILD_BUG_ON_ZERO(               \
						      offsetof(typeof(*(ptr)), \
							       obj) != 0),     \
			     type),                                            \
		     typeof(*(ptr)), obj)

#endif
