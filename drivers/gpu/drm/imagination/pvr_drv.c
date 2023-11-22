// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#include "pvr_device.h"
#include "pvr_drv.h"

#include <uapi/drm/pvr_drm.h>

#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_ioctl.h>

#include <linux/err.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

/**
 * DOC: PowerVR (Series 6 and later) and IMG Graphics Driver
 *
 * This driver supports the following PowerVR/IMG graphics cores from Imagination Technologies:
 *
 * * AXE-1-16M (found in Texas Instruments AM62)
 */

/**
 * pvr_ioctl_create_bo() - IOCTL to create a GEM buffer object.
 * @drm_dev: [IN] Target DRM device.
 * @raw_args: [IN/OUT] Arguments passed to this IOCTL. This must be of type
 * &struct drm_pvr_ioctl_create_bo_args.
 * @file: [IN] DRM file-private data.
 *
 * Called from userspace with %DRM_IOCTL_PVR_CREATE_BO.
 *
 * Return:
 *  * 0 on success,
 *  * -%EINVAL if the value of &drm_pvr_ioctl_create_bo_args.size is zero
 *    or wider than &typedef size_t,
 *  * -%EINVAL if any bits in &drm_pvr_ioctl_create_bo_args.flags that are
 *    reserved or undefined are set,
 *  * -%EINVAL if any padding fields in &drm_pvr_ioctl_create_bo_args are not
 *    zero,
 *  * Any error encountered while creating the object (see
 *    pvr_gem_object_create()), or
 *  * Any error encountered while transferring ownership of the object into a
 *    userspace-accessible handle (see pvr_gem_object_into_handle()).
 */
static int
pvr_ioctl_create_bo(struct drm_device *drm_dev, void *raw_args,
		    struct drm_file *file)
{
	return -ENOTTY;
}

/**
 * pvr_ioctl_get_bo_mmap_offset() - IOCTL to generate a "fake" offset to be
 * used when calling mmap() from userspace to map the given GEM buffer object
 * @drm_dev: [IN] DRM device (unused).
 * @raw_args: [IN/OUT] Arguments passed to this IOCTL. This must be of type
 *                     &struct drm_pvr_ioctl_get_bo_mmap_offset_args.
 * @file: [IN] DRM file private data.
 *
 * Called from userspace with %DRM_IOCTL_PVR_GET_BO_MMAP_OFFSET.
 *
 * This IOCTL does *not* perform an mmap. See the docs on
 * &struct drm_pvr_ioctl_get_bo_mmap_offset_args for details.
 *
 * Return:
 *  * 0 on success,
 *  * -%ENOENT if the handle does not reference a valid GEM buffer object,
 *  * -%EINVAL if any padding fields in &struct
 *    drm_pvr_ioctl_get_bo_mmap_offset_args are not zero, or
 *  * Any error returned by drm_gem_create_mmap_offset().
 */
static int
pvr_ioctl_get_bo_mmap_offset(struct drm_device *drm_dev, void *raw_args,
			     struct drm_file *file)
{
	return -ENOTTY;
}

/**
 * pvr_ioctl_dev_query() - IOCTL to copy information about a device
 * @drm_dev: [IN] DRM device.
 * @raw_args: [IN/OUT] Arguments passed to this IOCTL. This must be of type
 *                     &struct drm_pvr_ioctl_dev_query_args.
 * @file: [IN] DRM file private data.
 *
 * Called from userspace with %DRM_IOCTL_PVR_DEV_QUERY.
 * If the given receiving struct pointer is NULL, or the indicated size is too
 * small, the expected size of the struct type will be returned in the size
 * argument field.
 *
 * Return:
 *  * 0 on success or when fetching the size with args->pointer == NULL, or
 *  * -%E2BIG if the indicated size of the receiving struct is less than is
 *    required to contain the copied data, or
 *  * -%EINVAL if the indicated struct type is unknown, or
 *  * -%ENOMEM if local memory could not be allocated, or
 *  * -%EFAULT if local memory could not be copied to userspace.
 */
static int
pvr_ioctl_dev_query(struct drm_device *drm_dev, void *raw_args,
		    struct drm_file *file)
{
	return -ENOTTY;
}

/**
 * pvr_ioctl_create_context() - IOCTL to create a context
 * @drm_dev: [IN] DRM device.
 * @raw_args: [IN/OUT] Arguments passed to this IOCTL. This must be of type
 *                     &struct drm_pvr_ioctl_create_context_args.
 * @file: [IN] DRM file private data.
 *
 * Called from userspace with %DRM_IOCTL_PVR_CREATE_CONTEXT.
 *
 * Return:
 *  * 0 on success, or
 *  * -%EINVAL if provided arguments are invalid, or
 *  * -%EFAULT if arguments can't be copied from userspace, or
 *  * Any error returned by pvr_create_render_context().
 */
static int
pvr_ioctl_create_context(struct drm_device *drm_dev, void *raw_args,
			 struct drm_file *file)
{
	return -ENOTTY;
}

/**
 * pvr_ioctl_destroy_context() - IOCTL to destroy a context
 * @drm_dev: [IN] DRM device.
 * @raw_args: [IN/OUT] Arguments passed to this IOCTL. This must be of type
 *                     &struct drm_pvr_ioctl_destroy_context_args.
 * @file: [IN] DRM file private data.
 *
 * Called from userspace with %DRM_IOCTL_PVR_DESTROY_CONTEXT.
 *
 * Return:
 *  * 0 on success, or
 *  * -%EINVAL if context not in context list.
 */
static int
pvr_ioctl_destroy_context(struct drm_device *drm_dev, void *raw_args,
			  struct drm_file *file)
{
	return -ENOTTY;
}

/**
 * pvr_ioctl_create_free_list() - IOCTL to create a free list
 * @drm_dev: [IN] DRM device.
 * @raw_args: [IN/OUT] Arguments passed to this IOCTL. This must be of type
 *                     &struct drm_pvr_ioctl_create_free_list_args.
 * @file: [IN] DRM file private data.
 *
 * Called from userspace with %DRM_IOCTL_PVR_CREATE_FREE_LIST.
 *
 * Return:
 *  * 0 on success, or
 *  * Any error returned by pvr_free_list_create().
 */
static int
pvr_ioctl_create_free_list(struct drm_device *drm_dev, void *raw_args,
			   struct drm_file *file)
{
	return -ENOTTY;
}

/**
 * pvr_ioctl_destroy_free_list() - IOCTL to destroy a free list
 * @drm_dev: [IN] DRM device.
 * @raw_args: [IN] Arguments passed to this IOCTL. This must be of type
 *                 &struct drm_pvr_ioctl_destroy_free_list_args.
 * @file: [IN] DRM file private data.
 *
 * Called from userspace with %DRM_IOCTL_PVR_DESTROY_FREE_LIST.
 *
 * Return:
 *  * 0 on success, or
 *  * -%EINVAL if free list not in object list.
 */
static int
pvr_ioctl_destroy_free_list(struct drm_device *drm_dev, void *raw_args,
			    struct drm_file *file)
{
	return -ENOTTY;
}

/**
 * pvr_ioctl_create_hwrt_dataset() - IOCTL to create a HWRT dataset
 * @drm_dev: [IN] DRM device.
 * @raw_args: [IN/OUT] Arguments passed to this IOCTL. This must be of type
 *                     &struct drm_pvr_ioctl_create_hwrt_dataset_args.
 * @file: [IN] DRM file private data.
 *
 * Called from userspace with %DRM_IOCTL_PVR_CREATE_HWRT_DATASET.
 *
 * Return:
 *  * 0 on success, or
 *  * Any error returned by pvr_hwrt_dataset_create().
 */
static int
pvr_ioctl_create_hwrt_dataset(struct drm_device *drm_dev, void *raw_args,
			      struct drm_file *file)
{
	return -ENOTTY;
}

/**
 * pvr_ioctl_destroy_hwrt_dataset() - IOCTL to destroy a HWRT dataset
 * @drm_dev: [IN] DRM device.
 * @raw_args: [IN] Arguments passed to this IOCTL. This must be of type
 *                 &struct drm_pvr_ioctl_destroy_hwrt_dataset_args.
 * @file: [IN] DRM file private data.
 *
 * Called from userspace with %DRM_IOCTL_PVR_DESTROY_HWRT_DATASET.
 *
 * Return:
 *  * 0 on success, or
 *  * -%EINVAL if HWRT dataset not in object list.
 */
static int
pvr_ioctl_destroy_hwrt_dataset(struct drm_device *drm_dev, void *raw_args,
			       struct drm_file *file)
{
	return -ENOTTY;
}

/**
 * pvr_ioctl_create_vm_context() - IOCTL to create a VM context
 * @drm_dev: [IN] DRM device.
 * @raw_args: [IN/OUT] Arguments passed to this IOCTL. This must be of type
 *                     &struct drm_pvr_ioctl_create_vm_context_args.
 * @file: [IN] DRM file private data.
 *
 * Called from userspace with %DRM_IOCTL_PVR_CREATE_VM_CONTEXT.
 *
 * Return:
 *  * 0 on success, or
 *  * Any error returned by pvr_vm_create_context().
 */
static int
pvr_ioctl_create_vm_context(struct drm_device *drm_dev, void *raw_args,
			    struct drm_file *file)
{
	return -ENOTTY;
}

/**
 * pvr_ioctl_destroy_vm_context() - IOCTL to destroy a VM context
￼* @drm_dev: [IN] DRM device.
￼* @raw_args: [IN] Arguments passed to this IOCTL. This must be of type
￼*                 &struct drm_pvr_ioctl_destroy_vm_context_args.
￼* @file: [IN] DRM file private data.
￼*
￼* Called from userspace with %DRM_IOCTL_PVR_DESTROY_VM_CONTEXT.
￼*
￼* Return:
￼*  * 0 on success, or
￼*  * -%EINVAL if object not in object list.
 */
static int
pvr_ioctl_destroy_vm_context(struct drm_device *drm_dev, void *raw_args,
			     struct drm_file *file)
{
	return -ENOTTY;
}

/**
 * pvr_ioctl_vm_map() - IOCTL to map buffer to GPU address space.
 * @drm_dev: [IN] DRM device.
 * @raw_args: [IN] Arguments passed to this IOCTL. This must be of type
 *                 &struct drm_pvr_ioctl_vm_map_args.
 * @file: [IN] DRM file private data.
 *
 * Called from userspace with %DRM_IOCTL_PVR_VM_MAP.
 *
 * Return:
 *  * 0 on success,
 *  * -%EINVAL if &drm_pvr_ioctl_vm_op_map_args.flags is not zero,
 *  * -%EINVAL if the bounds specified by &drm_pvr_ioctl_vm_op_map_args.offset
 *    and &drm_pvr_ioctl_vm_op_map_args.size are not valid or do not fall
 *    within the buffer object specified by
 *    &drm_pvr_ioctl_vm_op_map_args.handle,
 *  * -%EINVAL if the bounds specified by
 *    &drm_pvr_ioctl_vm_op_map_args.device_addr and
 *    &drm_pvr_ioctl_vm_op_map_args.size do not form a valid device-virtual
 *    address range which falls entirely within a single heap, or
 *  * -%ENOENT if &drm_pvr_ioctl_vm_op_map_args.handle does not refer to a
 *    valid PowerVR buffer object.
 */
static int
pvr_ioctl_vm_map(struct drm_device *drm_dev, void *raw_args,
		 struct drm_file *file)
{
	return -ENOTTY;
}

/**
 * pvr_ioctl_vm_unmap() - IOCTL to unmap buffer from GPU address space.
 * @drm_dev: [IN] DRM device.
 * @raw_args: [IN] Arguments passed to this IOCTL. This must be of type
 *                 &struct drm_pvr_ioctl_vm_unmap_args.
 * @file: [IN] DRM file private data.
 *
 * Called from userspace with %DRM_IOCTL_PVR_VM_UNMAP.
 *
 * Return:
 *  * 0 on success,
 *  * -%EINVAL if &drm_pvr_ioctl_vm_op_unmap_args.device_addr is not a valid
 *    device page-aligned device-virtual address, or
 *  * -%ENOENT if there is currently no PowerVR buffer object mapped at
 *    &drm_pvr_ioctl_vm_op_unmap_args.device_addr.
 */
static int
pvr_ioctl_vm_unmap(struct drm_device *drm_dev, void *raw_args,
		   struct drm_file *file)
{
	return -ENOTTY;
}

/*
 * pvr_ioctl_submit_job() - IOCTL to submit a job to the GPU
 * @drm_dev: [IN] DRM device.
 * @raw_args: [IN] Arguments passed to this IOCTL. This must be of type
 *                 &struct drm_pvr_ioctl_submit_job_args.
 * @file: [IN] DRM file private data.
 *
 * Called from userspace with %DRM_IOCTL_PVR_SUBMIT_JOB.
 *
 * Return:
 *  * 0 on success, or
 *  * -%EINVAL if arguments are invalid.
 */
static int
pvr_ioctl_submit_jobs(struct drm_device *drm_dev, void *raw_args,
		      struct drm_file *file)
{
	return -ENOTTY;
}

#define DRM_PVR_IOCTL(_name, _func, _flags) \
	DRM_IOCTL_DEF_DRV(PVR_##_name, pvr_ioctl_##_func, _flags)

/* clang-format off */

static const struct drm_ioctl_desc pvr_drm_driver_ioctls[] = {
	DRM_PVR_IOCTL(DEV_QUERY, dev_query, DRM_RENDER_ALLOW),
	DRM_PVR_IOCTL(CREATE_BO, create_bo, DRM_RENDER_ALLOW),
	DRM_PVR_IOCTL(GET_BO_MMAP_OFFSET, get_bo_mmap_offset, DRM_RENDER_ALLOW),
	DRM_PVR_IOCTL(CREATE_VM_CONTEXT, create_vm_context, DRM_RENDER_ALLOW),
	DRM_PVR_IOCTL(DESTROY_VM_CONTEXT, destroy_vm_context, DRM_RENDER_ALLOW),
	DRM_PVR_IOCTL(VM_MAP, vm_map, DRM_RENDER_ALLOW),
	DRM_PVR_IOCTL(VM_UNMAP, vm_unmap, DRM_RENDER_ALLOW),
	DRM_PVR_IOCTL(CREATE_CONTEXT, create_context, DRM_RENDER_ALLOW),
	DRM_PVR_IOCTL(DESTROY_CONTEXT, destroy_context, DRM_RENDER_ALLOW),
	DRM_PVR_IOCTL(CREATE_FREE_LIST, create_free_list, DRM_RENDER_ALLOW),
	DRM_PVR_IOCTL(DESTROY_FREE_LIST, destroy_free_list, DRM_RENDER_ALLOW),
	DRM_PVR_IOCTL(CREATE_HWRT_DATASET, create_hwrt_dataset, DRM_RENDER_ALLOW),
	DRM_PVR_IOCTL(DESTROY_HWRT_DATASET, destroy_hwrt_dataset, DRM_RENDER_ALLOW),
	DRM_PVR_IOCTL(SUBMIT_JOBS, submit_jobs, DRM_RENDER_ALLOW),
};

/* clang-format on */

#undef DRM_PVR_IOCTL

/**
 * pvr_drm_driver_open() - Driver callback when a new &struct drm_file is opened
 * @drm_dev: [IN] DRM device.
 * @file: [IN] DRM file private data.
 *
 * Allocates powervr-specific file private data (&struct pvr_file).
 *
 * Registered in &pvr_drm_driver.
 *
 * Return:
 *  * 0 on success,
 *  * -%ENOMEM if the allocation of a &struct ipvr_file fails, or
 *  * Any error returned by pvr_memory_context_init().
 */
static int
pvr_drm_driver_open(struct drm_device *drm_dev, struct drm_file *file)
{
	struct pvr_device *pvr_dev = to_pvr_device(drm_dev);
	struct pvr_file *pvr_file;

	pvr_file = kzalloc(sizeof(*pvr_file), GFP_KERNEL);
	if (!pvr_file)
		return -ENOMEM;

	/*
	 * Store reference to base DRM file private data for use by
	 * from_pvr_file.
	 */
	pvr_file->file = file;

	/*
	 * Store reference to powervr-specific outer device struct in file
	 * private data for convenient access.
	 */
	pvr_file->pvr_dev = pvr_dev;

	/*
	 * Store reference to powervr-specific file private data in DRM file
	 * private data.
	 */
	file->driver_priv = pvr_file;

	return 0;
}

/**
 * pvr_drm_driver_postclose() - One of the driver callbacks when a &struct
 * drm_file is closed.
 * @drm_dev: [IN] DRM device (unused).
 * @file: [IN] DRM file private data.
 *
 * Frees powervr-specific file private data (&struct pvr_file).
 *
 * Registered in &pvr_drm_driver.
 */
static void
pvr_drm_driver_postclose(__always_unused struct drm_device *drm_dev,
			 struct drm_file *file)
{
	struct pvr_file *pvr_file = to_pvr_file(file);

	kfree(pvr_file);
	file->driver_priv = NULL;
}

DEFINE_DRM_GEM_FOPS(pvr_drm_driver_fops);

static struct drm_driver pvr_drm_driver = {
	.driver_features = DRIVER_RENDER,
	.open = pvr_drm_driver_open,
	.postclose = pvr_drm_driver_postclose,
	.ioctls = pvr_drm_driver_ioctls,
	.num_ioctls = ARRAY_SIZE(pvr_drm_driver_ioctls),
	.fops = &pvr_drm_driver_fops,

	.name = PVR_DRIVER_NAME,
	.desc = PVR_DRIVER_DESC,
	.date = PVR_DRIVER_DATE,
	.major = PVR_DRIVER_MAJOR,
	.minor = PVR_DRIVER_MINOR,
	.patchlevel = PVR_DRIVER_PATCHLEVEL,

};

static int
pvr_probe(struct platform_device *plat_dev)
{
	struct pvr_device *pvr_dev;
	struct drm_device *drm_dev;
	int err;

	pvr_dev = devm_drm_dev_alloc(&plat_dev->dev, &pvr_drm_driver,
				     struct pvr_device, base);
	if (IS_ERR(pvr_dev))
		return PTR_ERR(pvr_dev);

	drm_dev = &pvr_dev->base;

	platform_set_drvdata(plat_dev, drm_dev);

	err = pvr_device_init(pvr_dev);
	if (err)
		return err;

	err = drm_dev_register(drm_dev, 0);
	if (err)
		goto err_device_fini;

	return 0;

err_device_fini:
	pvr_device_fini(pvr_dev);

	return err;
}

static int
pvr_remove(struct platform_device *plat_dev)
{
	struct drm_device *drm_dev = platform_get_drvdata(plat_dev);
	struct pvr_device *pvr_dev = to_pvr_device(drm_dev);

	pvr_device_fini(pvr_dev);
	drm_dev_unplug(drm_dev);

	return 0;
}

static const struct of_device_id dt_match[] = {
	{ .compatible = "img,img-axe", .data = NULL },
	{}
};
MODULE_DEVICE_TABLE(of, dt_match);

static struct platform_driver pvr_driver = {
	.probe = pvr_probe,
	.remove = pvr_remove,
	.driver = {
		.name = PVR_DRIVER_NAME,
		.of_match_table = dt_match,
	},
};
module_platform_driver(pvr_driver);

MODULE_AUTHOR("Imagination Technologies Ltd.");
MODULE_DESCRIPTION(PVR_DRIVER_DESC);
MODULE_LICENSE("Dual MIT/GPL");
MODULE_IMPORT_NS(DMA_BUF);
