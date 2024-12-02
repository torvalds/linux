// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2021 Intel Corporation
 * Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES
 *
 * iommufd provides control over the IOMMU HW objects created by IOMMU kernel
 * drivers. IOMMU HW objects revolve around IO page tables that map incoming DMA
 * addresses (IOVA) to CPU addresses.
 */
#define pr_fmt(fmt) "iommufd: " fmt

#include <linux/bug.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/iommufd.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <uapi/linux/iommufd.h>

#include "io_pagetable.h"
#include "iommufd_private.h"
#include "iommufd_test.h"

struct iommufd_object_ops {
	void (*destroy)(struct iommufd_object *obj);
	void (*abort)(struct iommufd_object *obj);
};
static const struct iommufd_object_ops iommufd_object_ops[];
static struct miscdevice vfio_misc_dev;

/*
 * Allow concurrent access to the object.
 *
 * Once another thread can see the object pointer it can prevent object
 * destruction. Expect for special kernel-only objects there is no in-kernel way
 * to reliably destroy a single object. Thus all APIs that are creating objects
 * must use iommufd_object_abort() to handle their errors and only call
 * iommufd_object_finalize() once object creation cannot fail.
 */
void iommufd_object_finalize(struct iommufd_ctx *ictx,
			     struct iommufd_object *obj)
{
	XA_STATE(xas, &ictx->objects, obj->id);
	void *old;

	xa_lock(&ictx->objects);
	old = xas_store(&xas, obj);
	xa_unlock(&ictx->objects);
	/* obj->id was returned from xa_alloc() so the xas_store() cannot fail */
	WARN_ON(old != XA_ZERO_ENTRY);
}

/* Undo _iommufd_object_alloc() if iommufd_object_finalize() was not called */
void iommufd_object_abort(struct iommufd_ctx *ictx, struct iommufd_object *obj)
{
	XA_STATE(xas, &ictx->objects, obj->id);
	void *old;

	xa_lock(&ictx->objects);
	old = xas_store(&xas, NULL);
	xa_unlock(&ictx->objects);
	WARN_ON(old != XA_ZERO_ENTRY);
	kfree(obj);
}

/*
 * Abort an object that has been fully initialized and needs destroy, but has
 * not been finalized.
 */
void iommufd_object_abort_and_destroy(struct iommufd_ctx *ictx,
				      struct iommufd_object *obj)
{
	if (iommufd_object_ops[obj->type].abort)
		iommufd_object_ops[obj->type].abort(obj);
	else
		iommufd_object_ops[obj->type].destroy(obj);
	iommufd_object_abort(ictx, obj);
}

struct iommufd_object *iommufd_get_object(struct iommufd_ctx *ictx, u32 id,
					  enum iommufd_object_type type)
{
	struct iommufd_object *obj;

	if (iommufd_should_fail())
		return ERR_PTR(-ENOENT);

	xa_lock(&ictx->objects);
	obj = xa_load(&ictx->objects, id);
	if (!obj || (type != IOMMUFD_OBJ_ANY && obj->type != type) ||
	    !iommufd_lock_obj(obj))
		obj = ERR_PTR(-ENOENT);
	xa_unlock(&ictx->objects);
	return obj;
}

static int iommufd_object_dec_wait_shortterm(struct iommufd_ctx *ictx,
					     struct iommufd_object *to_destroy)
{
	if (refcount_dec_and_test(&to_destroy->shortterm_users))
		return 0;

	if (wait_event_timeout(ictx->destroy_wait,
				refcount_read(&to_destroy->shortterm_users) ==
					0,
				msecs_to_jiffies(10000)))
		return 0;

	pr_crit("Time out waiting for iommufd object to become free\n");
	refcount_inc(&to_destroy->shortterm_users);
	return -EBUSY;
}

/*
 * Remove the given object id from the xarray if the only reference to the
 * object is held by the xarray.
 */
int iommufd_object_remove(struct iommufd_ctx *ictx,
			  struct iommufd_object *to_destroy, u32 id,
			  unsigned int flags)
{
	struct iommufd_object *obj;
	XA_STATE(xas, &ictx->objects, id);
	bool zerod_shortterm = false;
	int ret;

	/*
	 * The purpose of the shortterm_users is to ensure deterministic
	 * destruction of objects used by external drivers and destroyed by this
	 * function. Any temporary increment of the refcount must increment
	 * shortterm_users, such as during ioctl execution.
	 */
	if (flags & REMOVE_WAIT_SHORTTERM) {
		ret = iommufd_object_dec_wait_shortterm(ictx, to_destroy);
		if (ret) {
			/*
			 * We have a bug. Put back the callers reference and
			 * defer cleaning this object until close.
			 */
			refcount_dec(&to_destroy->users);
			return ret;
		}
		zerod_shortterm = true;
	}

	xa_lock(&ictx->objects);
	obj = xas_load(&xas);
	if (to_destroy) {
		/*
		 * If the caller is holding a ref on obj we put it here under
		 * the spinlock.
		 */
		refcount_dec(&obj->users);

		if (WARN_ON(obj != to_destroy)) {
			ret = -ENOENT;
			goto err_xa;
		}
	} else if (xa_is_zero(obj) || !obj) {
		ret = -ENOENT;
		goto err_xa;
	}

	if (!refcount_dec_if_one(&obj->users)) {
		ret = -EBUSY;
		goto err_xa;
	}

	xas_store(&xas, NULL);
	if (ictx->vfio_ioas == container_of(obj, struct iommufd_ioas, obj))
		ictx->vfio_ioas = NULL;
	xa_unlock(&ictx->objects);

	/*
	 * Since users is zero any positive users_shortterm must be racing
	 * iommufd_put_object(), or we have a bug.
	 */
	if (!zerod_shortterm) {
		ret = iommufd_object_dec_wait_shortterm(ictx, obj);
		if (WARN_ON(ret))
			return ret;
	}

	iommufd_object_ops[obj->type].destroy(obj);
	kfree(obj);
	return 0;

err_xa:
	if (zerod_shortterm) {
		/* Restore the xarray owned reference */
		refcount_set(&obj->shortterm_users, 1);
	}
	xa_unlock(&ictx->objects);

	/* The returned object reference count is zero */
	return ret;
}

static int iommufd_destroy(struct iommufd_ucmd *ucmd)
{
	struct iommu_destroy *cmd = ucmd->cmd;

	return iommufd_object_remove(ucmd->ictx, NULL, cmd->id, 0);
}

static int iommufd_fops_open(struct inode *inode, struct file *filp)
{
	struct iommufd_ctx *ictx;

	ictx = kzalloc(sizeof(*ictx), GFP_KERNEL_ACCOUNT);
	if (!ictx)
		return -ENOMEM;

	/*
	 * For compatibility with VFIO when /dev/vfio/vfio is opened we default
	 * to the same rlimit accounting as vfio uses.
	 */
	if (IS_ENABLED(CONFIG_IOMMUFD_VFIO_CONTAINER) &&
	    filp->private_data == &vfio_misc_dev) {
		ictx->account_mode = IOPT_PAGES_ACCOUNT_MM;
		pr_info_once("IOMMUFD is providing /dev/vfio/vfio, not VFIO.\n");
	}

	init_rwsem(&ictx->ioas_creation_lock);
	xa_init_flags(&ictx->objects, XA_FLAGS_ALLOC1 | XA_FLAGS_ACCOUNT);
	xa_init(&ictx->groups);
	ictx->file = filp;
	init_waitqueue_head(&ictx->destroy_wait);
	filp->private_data = ictx;
	return 0;
}

static int iommufd_fops_release(struct inode *inode, struct file *filp)
{
	struct iommufd_ctx *ictx = filp->private_data;
	struct iommufd_object *obj;

	/*
	 * The objects in the xarray form a graph of "users" counts, and we have
	 * to destroy them in a depth first manner. Leaf objects will reduce the
	 * users count of interior objects when they are destroyed.
	 *
	 * Repeatedly destroying all the "1 users" leaf objects will progress
	 * until the entire list is destroyed. If this can't progress then there
	 * is some bug related to object refcounting.
	 */
	while (!xa_empty(&ictx->objects)) {
		unsigned int destroyed = 0;
		unsigned long index;

		xa_for_each(&ictx->objects, index, obj) {
			if (!refcount_dec_if_one(&obj->users))
				continue;
			destroyed++;
			xa_erase(&ictx->objects, index);
			iommufd_object_ops[obj->type].destroy(obj);
			kfree(obj);
		}
		/* Bug related to users refcount */
		if (WARN_ON(!destroyed))
			break;
	}
	WARN_ON(!xa_empty(&ictx->groups));
	kfree(ictx);
	return 0;
}

static int iommufd_option(struct iommufd_ucmd *ucmd)
{
	struct iommu_option *cmd = ucmd->cmd;
	int rc;

	if (cmd->__reserved)
		return -EOPNOTSUPP;

	switch (cmd->option_id) {
	case IOMMU_OPTION_RLIMIT_MODE:
		rc = iommufd_option_rlimit_mode(cmd, ucmd->ictx);
		break;
	case IOMMU_OPTION_HUGE_PAGES:
		rc = iommufd_ioas_option(ucmd);
		break;
	default:
		return -EOPNOTSUPP;
	}
	if (rc)
		return rc;
	if (copy_to_user(&((struct iommu_option __user *)ucmd->ubuffer)->val64,
			 &cmd->val64, sizeof(cmd->val64)))
		return -EFAULT;
	return 0;
}

union ucmd_buffer {
	struct iommu_destroy destroy;
	struct iommu_fault_alloc fault;
	struct iommu_hw_info info;
	struct iommu_hwpt_alloc hwpt;
	struct iommu_hwpt_get_dirty_bitmap get_dirty_bitmap;
	struct iommu_hwpt_invalidate cache;
	struct iommu_hwpt_set_dirty_tracking set_dirty_tracking;
	struct iommu_ioas_alloc alloc;
	struct iommu_ioas_allow_iovas allow_iovas;
	struct iommu_ioas_copy ioas_copy;
	struct iommu_ioas_iova_ranges iova_ranges;
	struct iommu_ioas_map map;
	struct iommu_ioas_unmap unmap;
	struct iommu_option option;
	struct iommu_vfio_ioas vfio_ioas;
	struct iommu_viommu_alloc viommu;
	struct iommu_vdevice_alloc vdev;
#ifdef CONFIG_IOMMUFD_TEST
	struct iommu_test_cmd test;
#endif
};

struct iommufd_ioctl_op {
	unsigned int size;
	unsigned int min_size;
	unsigned int ioctl_num;
	int (*execute)(struct iommufd_ucmd *ucmd);
};

#define IOCTL_OP(_ioctl, _fn, _struct, _last)                                  \
	[_IOC_NR(_ioctl) - IOMMUFD_CMD_BASE] = {                               \
		.size = sizeof(_struct) +                                      \
			BUILD_BUG_ON_ZERO(sizeof(union ucmd_buffer) <          \
					  sizeof(_struct)),                    \
		.min_size = offsetofend(_struct, _last),                       \
		.ioctl_num = _ioctl,                                           \
		.execute = _fn,                                                \
	}
static const struct iommufd_ioctl_op iommufd_ioctl_ops[] = {
	IOCTL_OP(IOMMU_DESTROY, iommufd_destroy, struct iommu_destroy, id),
	IOCTL_OP(IOMMU_FAULT_QUEUE_ALLOC, iommufd_fault_alloc, struct iommu_fault_alloc,
		 out_fault_fd),
	IOCTL_OP(IOMMU_GET_HW_INFO, iommufd_get_hw_info, struct iommu_hw_info,
		 __reserved),
	IOCTL_OP(IOMMU_HWPT_ALLOC, iommufd_hwpt_alloc, struct iommu_hwpt_alloc,
		 __reserved),
	IOCTL_OP(IOMMU_HWPT_GET_DIRTY_BITMAP, iommufd_hwpt_get_dirty_bitmap,
		 struct iommu_hwpt_get_dirty_bitmap, data),
	IOCTL_OP(IOMMU_HWPT_INVALIDATE, iommufd_hwpt_invalidate,
		 struct iommu_hwpt_invalidate, __reserved),
	IOCTL_OP(IOMMU_HWPT_SET_DIRTY_TRACKING, iommufd_hwpt_set_dirty_tracking,
		 struct iommu_hwpt_set_dirty_tracking, __reserved),
	IOCTL_OP(IOMMU_IOAS_ALLOC, iommufd_ioas_alloc_ioctl,
		 struct iommu_ioas_alloc, out_ioas_id),
	IOCTL_OP(IOMMU_IOAS_ALLOW_IOVAS, iommufd_ioas_allow_iovas,
		 struct iommu_ioas_allow_iovas, allowed_iovas),
	IOCTL_OP(IOMMU_IOAS_CHANGE_PROCESS, iommufd_ioas_change_process,
		 struct iommu_ioas_change_process, __reserved),
	IOCTL_OP(IOMMU_IOAS_COPY, iommufd_ioas_copy, struct iommu_ioas_copy,
		 src_iova),
	IOCTL_OP(IOMMU_IOAS_IOVA_RANGES, iommufd_ioas_iova_ranges,
		 struct iommu_ioas_iova_ranges, out_iova_alignment),
	IOCTL_OP(IOMMU_IOAS_MAP, iommufd_ioas_map, struct iommu_ioas_map,
		 iova),
	IOCTL_OP(IOMMU_IOAS_MAP_FILE, iommufd_ioas_map_file,
		 struct iommu_ioas_map_file, iova),
	IOCTL_OP(IOMMU_IOAS_UNMAP, iommufd_ioas_unmap, struct iommu_ioas_unmap,
		 length),
	IOCTL_OP(IOMMU_OPTION, iommufd_option, struct iommu_option,
		 val64),
	IOCTL_OP(IOMMU_VFIO_IOAS, iommufd_vfio_ioas, struct iommu_vfio_ioas,
		 __reserved),
	IOCTL_OP(IOMMU_VIOMMU_ALLOC, iommufd_viommu_alloc_ioctl,
		 struct iommu_viommu_alloc, out_viommu_id),
	IOCTL_OP(IOMMU_VDEVICE_ALLOC, iommufd_vdevice_alloc_ioctl,
		 struct iommu_vdevice_alloc, virt_id),
#ifdef CONFIG_IOMMUFD_TEST
	IOCTL_OP(IOMMU_TEST_CMD, iommufd_test, struct iommu_test_cmd, last),
#endif
};

static long iommufd_fops_ioctl(struct file *filp, unsigned int cmd,
			       unsigned long arg)
{
	struct iommufd_ctx *ictx = filp->private_data;
	const struct iommufd_ioctl_op *op;
	struct iommufd_ucmd ucmd = {};
	union ucmd_buffer buf;
	unsigned int nr;
	int ret;

	nr = _IOC_NR(cmd);
	if (nr < IOMMUFD_CMD_BASE ||
	    (nr - IOMMUFD_CMD_BASE) >= ARRAY_SIZE(iommufd_ioctl_ops))
		return iommufd_vfio_ioctl(ictx, cmd, arg);

	ucmd.ictx = ictx;
	ucmd.ubuffer = (void __user *)arg;
	ret = get_user(ucmd.user_size, (u32 __user *)ucmd.ubuffer);
	if (ret)
		return ret;

	op = &iommufd_ioctl_ops[nr - IOMMUFD_CMD_BASE];
	if (op->ioctl_num != cmd)
		return -ENOIOCTLCMD;
	if (ucmd.user_size < op->min_size)
		return -EINVAL;

	ucmd.cmd = &buf;
	ret = copy_struct_from_user(ucmd.cmd, op->size, ucmd.ubuffer,
				    ucmd.user_size);
	if (ret)
		return ret;
	ret = op->execute(&ucmd);
	return ret;
}

static const struct file_operations iommufd_fops = {
	.owner = THIS_MODULE,
	.open = iommufd_fops_open,
	.release = iommufd_fops_release,
	.unlocked_ioctl = iommufd_fops_ioctl,
};

/**
 * iommufd_ctx_get - Get a context reference
 * @ictx: Context to get
 *
 * The caller must already hold a valid reference to ictx.
 */
void iommufd_ctx_get(struct iommufd_ctx *ictx)
{
	get_file(ictx->file);
}
EXPORT_SYMBOL_NS_GPL(iommufd_ctx_get, IOMMUFD);

/**
 * iommufd_ctx_from_file - Acquires a reference to the iommufd context
 * @file: File to obtain the reference from
 *
 * Returns a pointer to the iommufd_ctx, otherwise ERR_PTR. The struct file
 * remains owned by the caller and the caller must still do fput. On success
 * the caller is responsible to call iommufd_ctx_put().
 */
struct iommufd_ctx *iommufd_ctx_from_file(struct file *file)
{
	struct iommufd_ctx *ictx;

	if (file->f_op != &iommufd_fops)
		return ERR_PTR(-EBADFD);
	ictx = file->private_data;
	iommufd_ctx_get(ictx);
	return ictx;
}
EXPORT_SYMBOL_NS_GPL(iommufd_ctx_from_file, IOMMUFD);

/**
 * iommufd_ctx_from_fd - Acquires a reference to the iommufd context
 * @fd: File descriptor to obtain the reference from
 *
 * Returns a pointer to the iommufd_ctx, otherwise ERR_PTR. On success
 * the caller is responsible to call iommufd_ctx_put().
 */
struct iommufd_ctx *iommufd_ctx_from_fd(int fd)
{
	struct file *file;

	file = fget(fd);
	if (!file)
		return ERR_PTR(-EBADF);

	if (file->f_op != &iommufd_fops) {
		fput(file);
		return ERR_PTR(-EBADFD);
	}
	/* fget is the same as iommufd_ctx_get() */
	return file->private_data;
}
EXPORT_SYMBOL_NS_GPL(iommufd_ctx_from_fd, IOMMUFD);

/**
 * iommufd_ctx_put - Put back a reference
 * @ictx: Context to put back
 */
void iommufd_ctx_put(struct iommufd_ctx *ictx)
{
	fput(ictx->file);
}
EXPORT_SYMBOL_NS_GPL(iommufd_ctx_put, IOMMUFD);

static const struct iommufd_object_ops iommufd_object_ops[] = {
	[IOMMUFD_OBJ_ACCESS] = {
		.destroy = iommufd_access_destroy_object,
	},
	[IOMMUFD_OBJ_DEVICE] = {
		.destroy = iommufd_device_destroy,
	},
	[IOMMUFD_OBJ_IOAS] = {
		.destroy = iommufd_ioas_destroy,
	},
	[IOMMUFD_OBJ_HWPT_PAGING] = {
		.destroy = iommufd_hwpt_paging_destroy,
		.abort = iommufd_hwpt_paging_abort,
	},
	[IOMMUFD_OBJ_HWPT_NESTED] = {
		.destroy = iommufd_hwpt_nested_destroy,
		.abort = iommufd_hwpt_nested_abort,
	},
	[IOMMUFD_OBJ_FAULT] = {
		.destroy = iommufd_fault_destroy,
	},
	[IOMMUFD_OBJ_VIOMMU] = {
		.destroy = iommufd_viommu_destroy,
	},
	[IOMMUFD_OBJ_VDEVICE] = {
		.destroy = iommufd_vdevice_destroy,
	},
#ifdef CONFIG_IOMMUFD_TEST
	[IOMMUFD_OBJ_SELFTEST] = {
		.destroy = iommufd_selftest_destroy,
	},
#endif
};

static struct miscdevice iommu_misc_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "iommu",
	.fops = &iommufd_fops,
	.nodename = "iommu",
	.mode = 0660,
};


static struct miscdevice vfio_misc_dev = {
	.minor = VFIO_MINOR,
	.name = "vfio",
	.fops = &iommufd_fops,
	.nodename = "vfio/vfio",
	.mode = 0666,
};

static int __init iommufd_init(void)
{
	int ret;

	ret = misc_register(&iommu_misc_dev);
	if (ret)
		return ret;

	if (IS_ENABLED(CONFIG_IOMMUFD_VFIO_CONTAINER)) {
		ret = misc_register(&vfio_misc_dev);
		if (ret)
			goto err_misc;
	}
	ret = iommufd_test_init();
	if (ret)
		goto err_vfio_misc;
	return 0;

err_vfio_misc:
	if (IS_ENABLED(CONFIG_IOMMUFD_VFIO_CONTAINER))
		misc_deregister(&vfio_misc_dev);
err_misc:
	misc_deregister(&iommu_misc_dev);
	return ret;
}

static void __exit iommufd_exit(void)
{
	iommufd_test_exit();
	if (IS_ENABLED(CONFIG_IOMMUFD_VFIO_CONTAINER))
		misc_deregister(&vfio_misc_dev);
	misc_deregister(&iommu_misc_dev);
}

module_init(iommufd_init);
module_exit(iommufd_exit);

#if IS_ENABLED(CONFIG_IOMMUFD_VFIO_CONTAINER)
MODULE_ALIAS_MISCDEV(VFIO_MINOR);
MODULE_ALIAS("devname:vfio/vfio");
#endif
MODULE_IMPORT_NS(IOMMUFD_INTERNAL);
MODULE_IMPORT_NS(IOMMUFD);
MODULE_DESCRIPTION("I/O Address Space Management for passthrough devices");
MODULE_LICENSE("GPL");
