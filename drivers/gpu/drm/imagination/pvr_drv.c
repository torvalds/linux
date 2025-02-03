// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#include "pvr_context.h"
#include "pvr_debugfs.h"
#include "pvr_device.h"
#include "pvr_drv.h"
#include "pvr_free_list.h"
#include "pvr_gem.h"
#include "pvr_hwrt.h"
#include "pvr_job.h"
#include "pvr_mmu.h"
#include "pvr_power.h"
#include "pvr_rogue_defs.h"
#include "pvr_rogue_fwif_client.h"
#include "pvr_rogue_fwif_shared.h"
#include "pvr_vm.h"

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
#include <linux/list.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/xarray.h>

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
	struct drm_pvr_ioctl_create_bo_args *args = raw_args;
	struct pvr_device *pvr_dev = to_pvr_device(drm_dev);
	struct pvr_file *pvr_file = to_pvr_file(file);

	struct pvr_gem_object *pvr_obj;
	size_t sanitized_size;

	int idx;
	int err;

	if (!drm_dev_enter(drm_dev, &idx))
		return -EIO;

	/* All padding fields must be zeroed. */
	if (args->_padding_c != 0) {
		err = -EINVAL;
		goto err_drm_dev_exit;
	}

	/*
	 * On 64-bit platforms (our primary target), size_t is a u64. However,
	 * on other architectures we have to check for overflow when casting
	 * down to size_t from u64.
	 *
	 * We also disallow zero-sized allocations, and reserved (kernel-only)
	 * flags.
	 */
	if (args->size > SIZE_MAX || args->size == 0 || args->flags &
	    ~DRM_PVR_BO_FLAGS_MASK || args->size & (PVR_DEVICE_PAGE_SIZE - 1)) {
		err = -EINVAL;
		goto err_drm_dev_exit;
	}

	sanitized_size = (size_t)args->size;

	/*
	 * Create a buffer object and transfer ownership to a userspace-
	 * accessible handle.
	 */
	pvr_obj = pvr_gem_object_create(pvr_dev, sanitized_size, args->flags);
	if (IS_ERR(pvr_obj)) {
		err = PTR_ERR(pvr_obj);
		goto err_drm_dev_exit;
	}

	/* This function will not modify &args->handle unless it succeeds. */
	err = pvr_gem_object_into_handle(pvr_obj, pvr_file, &args->handle);
	if (err)
		goto err_destroy_obj;

	drm_dev_exit(idx);

	return 0;

err_destroy_obj:
	/*
	 * GEM objects are refcounted, so there is no explicit destructor
	 * function. Instead, we release the singular reference we currently
	 * hold on the object and let GEM take care of the rest.
	 */
	pvr_gem_object_put(pvr_obj);

err_drm_dev_exit:
	drm_dev_exit(idx);

	return err;
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
	struct drm_pvr_ioctl_get_bo_mmap_offset_args *args = raw_args;
	struct pvr_file *pvr_file = to_pvr_file(file);
	struct pvr_gem_object *pvr_obj;
	struct drm_gem_object *gem_obj;
	int idx;
	int ret;

	if (!drm_dev_enter(drm_dev, &idx))
		return -EIO;

	/* All padding fields must be zeroed. */
	if (args->_padding_4 != 0) {
		ret = -EINVAL;
		goto err_drm_dev_exit;
	}

	/*
	 * Obtain a kernel reference to the buffer object. This reference is
	 * counted and must be manually dropped before returning. If a buffer
	 * object cannot be found for the specified handle, return -%ENOENT (No
	 * such file or directory).
	 */
	pvr_obj = pvr_gem_object_from_handle(pvr_file, args->handle);
	if (!pvr_obj) {
		ret = -ENOENT;
		goto err_drm_dev_exit;
	}

	gem_obj = gem_from_pvr_gem(pvr_obj);

	/*
	 * Allocate a fake offset which can be used in userspace calls to mmap
	 * on the DRM device file. If this fails, return the error code. This
	 * operation is idempotent.
	 */
	ret = drm_gem_create_mmap_offset(gem_obj);
	if (ret != 0) {
		/* Drop our reference to the buffer object. */
		drm_gem_object_put(gem_obj);
		goto err_drm_dev_exit;
	}

	/*
	 * Read out the fake offset allocated by the earlier call to
	 * drm_gem_create_mmap_offset.
	 */
	args->offset = drm_vma_node_offset_addr(&gem_obj->vma_node);

	/* Drop our reference to the buffer object. */
	pvr_gem_object_put(pvr_obj);

err_drm_dev_exit:
	drm_dev_exit(idx);

	return ret;
}

static __always_inline __maybe_unused u64
pvr_fw_version_packed(u32 major, u32 minor)
{
	return ((u64)major << 32) | minor;
}

static u32
rogue_get_common_store_partition_space_size(struct pvr_device *pvr_dev)
{
	u32 max_partitions = 0;
	u32 tile_size_x = 0;
	u32 tile_size_y = 0;

	PVR_FEATURE_VALUE(pvr_dev, tile_size_x, &tile_size_x);
	PVR_FEATURE_VALUE(pvr_dev, tile_size_y, &tile_size_y);
	PVR_FEATURE_VALUE(pvr_dev, max_partitions, &max_partitions);

	if (tile_size_x == 16 && tile_size_y == 16) {
		u32 usc_min_output_registers_per_pix = 0;

		PVR_FEATURE_VALUE(pvr_dev, usc_min_output_registers_per_pix,
				  &usc_min_output_registers_per_pix);

		return tile_size_x * tile_size_y * max_partitions *
		       usc_min_output_registers_per_pix;
	}

	return max_partitions * 1024;
}

static u32
rogue_get_common_store_alloc_region_size(struct pvr_device *pvr_dev)
{
	u32 common_store_size_in_dwords = 512 * 4 * 4;
	u32 alloc_region_size;

	PVR_FEATURE_VALUE(pvr_dev, common_store_size_in_dwords, &common_store_size_in_dwords);

	alloc_region_size = common_store_size_in_dwords - (256U * 4U) -
			    rogue_get_common_store_partition_space_size(pvr_dev);

	if (PVR_HAS_QUIRK(pvr_dev, 44079)) {
		u32 common_store_split_point = (768U * 4U * 4U);

		return min(common_store_split_point - (256U * 4U), alloc_region_size);
	}

	return alloc_region_size;
}

static inline u32
rogue_get_num_phantoms(struct pvr_device *pvr_dev)
{
	u32 num_clusters = 1;

	PVR_FEATURE_VALUE(pvr_dev, num_clusters, &num_clusters);

	return ROGUE_REQ_NUM_PHANTOMS(num_clusters);
}

static inline u32
rogue_get_max_coeffs(struct pvr_device *pvr_dev)
{
	u32 max_coeff_additional_portion = ROGUE_MAX_VERTEX_SHARED_REGISTERS;
	u32 pending_allocation_shared_regs = 2U * 1024U;
	u32 pending_allocation_coeff_regs = 0U;
	u32 num_phantoms = rogue_get_num_phantoms(pvr_dev);
	u32 tiles_in_flight = 0;
	u32 max_coeff_pixel_portion;

	PVR_FEATURE_VALUE(pvr_dev, isp_max_tiles_in_flight, &tiles_in_flight);
	max_coeff_pixel_portion = DIV_ROUND_UP(tiles_in_flight, num_phantoms);
	max_coeff_pixel_portion *= ROGUE_MAX_PIXEL_SHARED_REGISTERS;

	/*
	 * Compute tasks on cores with BRN48492 and without compute overlap may lock
	 * up without two additional lines of coeffs.
	 */
	if (PVR_HAS_QUIRK(pvr_dev, 48492) && !PVR_HAS_FEATURE(pvr_dev, compute_overlap))
		pending_allocation_coeff_regs = 2U * 1024U;

	if (PVR_HAS_ENHANCEMENT(pvr_dev, 38748))
		pending_allocation_shared_regs = 0;

	if (PVR_HAS_ENHANCEMENT(pvr_dev, 38020))
		max_coeff_additional_portion += ROGUE_MAX_COMPUTE_SHARED_REGISTERS;

	return rogue_get_common_store_alloc_region_size(pvr_dev) + pending_allocation_coeff_regs -
		(max_coeff_pixel_portion + max_coeff_additional_portion +
		 pending_allocation_shared_regs);
}

static inline u32
rogue_get_cdm_max_local_mem_size_regs(struct pvr_device *pvr_dev)
{
	u32 available_coeffs_in_dwords = rogue_get_max_coeffs(pvr_dev);

	if (PVR_HAS_QUIRK(pvr_dev, 48492) && PVR_HAS_FEATURE(pvr_dev, roguexe) &&
	    !PVR_HAS_FEATURE(pvr_dev, compute_overlap)) {
		/* Driver must not use the 2 reserved lines. */
		available_coeffs_in_dwords -= ROGUE_CSRM_LINE_SIZE_IN_DWORDS * 2;
	}

	/*
	 * The maximum amount of local memory available to a kernel is the minimum
	 * of the total number of coefficient registers available and the max common
	 * store allocation size which can be made by the CDM.
	 *
	 * If any coeff lines are reserved for tessellation or pixel then we need to
	 * subtract those too.
	 */
	return min(available_coeffs_in_dwords, (u32)ROGUE_MAX_PER_KERNEL_LOCAL_MEM_SIZE_REGS);
}

/**
 * pvr_dev_query_gpu_info_get()
 * @pvr_dev: Device pointer.
 * @args: [IN] Device query arguments containing a pointer to a userspace
 *        struct drm_pvr_dev_query_gpu_info.
 *
 * If the query object pointer is NULL, the size field is updated with the
 * expected size of the query object.
 *
 * Returns:
 *  * 0 on success, or if size is requested using a NULL pointer, or
 *  * -%E2BIG if the indicated length of the allocation is less than is
 *    required to contain the copied data, or
 *  * -%EFAULT if local memory could not be copied to userspace.
 */
static int
pvr_dev_query_gpu_info_get(struct pvr_device *pvr_dev,
			   struct drm_pvr_ioctl_dev_query_args *args)
{
	struct drm_pvr_dev_query_gpu_info gpu_info = {0};
	int err;

	if (!args->pointer) {
		args->size = sizeof(struct drm_pvr_dev_query_gpu_info);
		return 0;
	}

	gpu_info.gpu_id =
		pvr_gpu_id_to_packed_bvnc(&pvr_dev->gpu_id);
	gpu_info.num_phantoms = rogue_get_num_phantoms(pvr_dev);

	err = PVR_UOBJ_SET(args->pointer, args->size, gpu_info);
	if (err < 0)
		return err;

	if (args->size > sizeof(gpu_info))
		args->size = sizeof(gpu_info);
	return 0;
}

/**
 * pvr_dev_query_runtime_info_get()
 * @pvr_dev: Device pointer.
 * @args: [IN] Device query arguments containing a pointer to a userspace
 *        struct drm_pvr_dev_query_runtime_info.
 *
 * If the query object pointer is NULL, the size field is updated with the
 * expected size of the query object.
 *
 * Returns:
 *  * 0 on success, or if size is requested using a NULL pointer, or
 *  * -%E2BIG if the indicated length of the allocation is less than is
 *    required to contain the copied data, or
 *  * -%EFAULT if local memory could not be copied to userspace.
 */
static int
pvr_dev_query_runtime_info_get(struct pvr_device *pvr_dev,
			       struct drm_pvr_ioctl_dev_query_args *args)
{
	struct drm_pvr_dev_query_runtime_info runtime_info = {0};
	int err;

	if (!args->pointer) {
		args->size = sizeof(struct drm_pvr_dev_query_runtime_info);
		return 0;
	}

	runtime_info.free_list_min_pages =
		pvr_get_free_list_min_pages(pvr_dev);
	runtime_info.free_list_max_pages =
		ROGUE_PM_MAX_FREELIST_SIZE / ROGUE_PM_PAGE_SIZE;
	runtime_info.common_store_alloc_region_size =
		rogue_get_common_store_alloc_region_size(pvr_dev);
	runtime_info.common_store_partition_space_size =
		rogue_get_common_store_partition_space_size(pvr_dev);
	runtime_info.max_coeffs = rogue_get_max_coeffs(pvr_dev);
	runtime_info.cdm_max_local_mem_size_regs =
		rogue_get_cdm_max_local_mem_size_regs(pvr_dev);

	err = PVR_UOBJ_SET(args->pointer, args->size, runtime_info);
	if (err < 0)
		return err;

	if (args->size > sizeof(runtime_info))
		args->size = sizeof(runtime_info);
	return 0;
}

/**
 * pvr_dev_query_quirks_get() - Unpack array of quirks at the address given
 * in a struct drm_pvr_dev_query_quirks, or gets the amount of space required
 * for it.
 * @pvr_dev: Device pointer.
 * @args: [IN] Device query arguments containing a pointer to a userspace
 *        struct drm_pvr_dev_query_query_quirks.
 *
 * If the query object pointer is NULL, the size field is updated with the
 * expected size of the query object.
 * If the userspace pointer in the query object is NULL, or the count is
 * short, no data is copied.
 * The count field will be updated to that copied, or if either pointer is
 * NULL, that which would have been copied.
 * The size field in the query object will be updated to the size copied.
 *
 * Returns:
 *  * 0 on success, or if size/count is requested using a NULL pointer, or
 *  * -%EINVAL if args contained non-zero reserved fields, or
 *  * -%E2BIG if the indicated length of the allocation is less than is
 *    required to contain the copied data, or
 *  * -%EFAULT if local memory could not be copied to userspace.
 */
static int
pvr_dev_query_quirks_get(struct pvr_device *pvr_dev,
			 struct drm_pvr_ioctl_dev_query_args *args)
{
	/*
	 * @FIXME - hardcoding of numbers here is intended as an
	 * intermediate step so the UAPI can be fixed, but requires a
	 * a refactor in the future to store them in a more appropriate
	 * location
	 */
	static const u32 umd_quirks_musthave[] = {
		47217,
		49927,
		62269,
	};
	static const u32 umd_quirks[] = {
		48545,
		51764,
	};
	struct drm_pvr_dev_query_quirks query;
	u32 out[ARRAY_SIZE(umd_quirks_musthave) + ARRAY_SIZE(umd_quirks)];
	size_t out_musthave_count = 0;
	size_t out_count = 0;
	int err;

	if (!args->pointer) {
		args->size = sizeof(struct drm_pvr_dev_query_quirks);
		return 0;
	}

	err = PVR_UOBJ_GET(query, args->size, args->pointer);

	if (err < 0)
		return err;
	if (query._padding_c)
		return -EINVAL;

	for (int i = 0; i < ARRAY_SIZE(umd_quirks_musthave); i++) {
		if (pvr_device_has_uapi_quirk(pvr_dev, umd_quirks_musthave[i])) {
			out[out_count++] = umd_quirks_musthave[i];
			out_musthave_count++;
		}
	}

	for (int i = 0; i < ARRAY_SIZE(umd_quirks); i++) {
		if (pvr_device_has_uapi_quirk(pvr_dev, umd_quirks[i]))
			out[out_count++] = umd_quirks[i];
	}

	if (!query.quirks)
		goto copy_out;
	if (query.count < out_count)
		return -E2BIG;

	if (copy_to_user(u64_to_user_ptr(query.quirks), out,
			 out_count * sizeof(u32))) {
		return -EFAULT;
	}

	query.musthave_count = out_musthave_count;

copy_out:
	query.count = out_count;
	err = PVR_UOBJ_SET(args->pointer, args->size, query);
	if (err < 0)
		return err;

	args->size = sizeof(query);
	return 0;
}

/**
 * pvr_dev_query_enhancements_get() - Unpack array of enhancements at the
 * address given in a struct drm_pvr_dev_query_enhancements, or gets the amount
 * of space required for it.
 * @pvr_dev: Device pointer.
 * @args: [IN] Device query arguments containing a pointer to a userspace
 *        struct drm_pvr_dev_query_enhancements.
 *
 * If the query object pointer is NULL, the size field is updated with the
 * expected size of the query object.
 * If the userspace pointer in the query object is NULL, or the count is
 * short, no data is copied.
 * The count field will be updated to that copied, or if either pointer is
 * NULL, that which would have been copied.
 * The size field in the query object will be updated to the size copied.
 *
 * Returns:
 *  * 0 on success, or if size/count is requested using a NULL pointer, or
 *  * -%EINVAL if args contained non-zero reserved fields, or
 *  * -%E2BIG if the indicated length of the allocation is less than is
 *    required to contain the copied data, or
 *  * -%EFAULT if local memory could not be copied to userspace.
 */
static int
pvr_dev_query_enhancements_get(struct pvr_device *pvr_dev,
			       struct drm_pvr_ioctl_dev_query_args *args)
{
	/*
	 * @FIXME - hardcoding of numbers here is intended as an
	 * intermediate step so the UAPI can be fixed, but requires a
	 * a refactor in the future to store them in a more appropriate
	 * location
	 */
	const u32 umd_enhancements[] = {
		35421,
		42064,
	};
	struct drm_pvr_dev_query_enhancements query;
	u32 out[ARRAY_SIZE(umd_enhancements)];
	size_t out_idx = 0;
	int err;

	if (!args->pointer) {
		args->size = sizeof(struct drm_pvr_dev_query_enhancements);
		return 0;
	}

	err = PVR_UOBJ_GET(query, args->size, args->pointer);

	if (err < 0)
		return err;
	if (query._padding_a)
		return -EINVAL;
	if (query._padding_c)
		return -EINVAL;

	for (int i = 0; i < ARRAY_SIZE(umd_enhancements); i++) {
		if (pvr_device_has_uapi_enhancement(pvr_dev, umd_enhancements[i]))
			out[out_idx++] = umd_enhancements[i];
	}

	if (!query.enhancements)
		goto copy_out;
	if (query.count < out_idx)
		return -E2BIG;

	if (copy_to_user(u64_to_user_ptr(query.enhancements), out,
			 out_idx * sizeof(u32))) {
		return -EFAULT;
	}

copy_out:
	query.count = out_idx;
	err = PVR_UOBJ_SET(args->pointer, args->size, query);
	if (err < 0)
		return err;

	args->size = sizeof(query);
	return 0;
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
	struct pvr_device *pvr_dev = to_pvr_device(drm_dev);
	struct drm_pvr_ioctl_dev_query_args *args = raw_args;
	int idx;
	int ret = -EINVAL;

	if (!drm_dev_enter(drm_dev, &idx))
		return -EIO;

	switch ((enum drm_pvr_dev_query)args->type) {
	case DRM_PVR_DEV_QUERY_GPU_INFO_GET:
		ret = pvr_dev_query_gpu_info_get(pvr_dev, args);
		break;

	case DRM_PVR_DEV_QUERY_RUNTIME_INFO_GET:
		ret = pvr_dev_query_runtime_info_get(pvr_dev, args);
		break;

	case DRM_PVR_DEV_QUERY_QUIRKS_GET:
		ret = pvr_dev_query_quirks_get(pvr_dev, args);
		break;

	case DRM_PVR_DEV_QUERY_ENHANCEMENTS_GET:
		ret = pvr_dev_query_enhancements_get(pvr_dev, args);
		break;

	case DRM_PVR_DEV_QUERY_HEAP_INFO_GET:
		ret = pvr_heap_info_get(pvr_dev, args);
		break;

	case DRM_PVR_DEV_QUERY_STATIC_DATA_AREAS_GET:
		ret = pvr_static_data_areas_get(pvr_dev, args);
		break;
	}

	drm_dev_exit(idx);

	return ret;
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
	struct drm_pvr_ioctl_create_context_args *args = raw_args;
	struct pvr_file *pvr_file = file->driver_priv;
	int idx;
	int ret;

	if (!drm_dev_enter(drm_dev, &idx))
		return -EIO;

	ret = pvr_context_create(pvr_file, args);

	drm_dev_exit(idx);

	return ret;
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
	struct drm_pvr_ioctl_destroy_context_args *args = raw_args;
	struct pvr_file *pvr_file = file->driver_priv;

	if (args->_padding_4)
		return -EINVAL;

	return pvr_context_destroy(pvr_file, args->handle);
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
	struct drm_pvr_ioctl_create_free_list_args *args = raw_args;
	struct pvr_file *pvr_file = to_pvr_file(file);
	struct pvr_free_list *free_list;
	int idx;
	int err;

	if (!drm_dev_enter(drm_dev, &idx))
		return -EIO;

	free_list = pvr_free_list_create(pvr_file, args);
	if (IS_ERR(free_list)) {
		err = PTR_ERR(free_list);
		goto err_drm_dev_exit;
	}

	/* Allocate object handle for userspace. */
	err = xa_alloc(&pvr_file->free_list_handles,
		       &args->handle,
		       free_list,
		       xa_limit_32b,
		       GFP_KERNEL);
	if (err < 0)
		goto err_cleanup;

	drm_dev_exit(idx);

	return 0;

err_cleanup:
	pvr_free_list_put(free_list);

err_drm_dev_exit:
	drm_dev_exit(idx);

	return err;
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
	struct drm_pvr_ioctl_destroy_free_list_args *args = raw_args;
	struct pvr_file *pvr_file = to_pvr_file(file);
	struct pvr_free_list *free_list;

	if (args->_padding_4)
		return -EINVAL;

	free_list = xa_erase(&pvr_file->free_list_handles, args->handle);
	if (!free_list)
		return -EINVAL;

	pvr_free_list_put(free_list);
	return 0;
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
	struct drm_pvr_ioctl_create_hwrt_dataset_args *args = raw_args;
	struct pvr_file *pvr_file = to_pvr_file(file);
	struct pvr_hwrt_dataset *hwrt;
	int idx;
	int err;

	if (!drm_dev_enter(drm_dev, &idx))
		return -EIO;

	hwrt = pvr_hwrt_dataset_create(pvr_file, args);
	if (IS_ERR(hwrt)) {
		err = PTR_ERR(hwrt);
		goto err_drm_dev_exit;
	}

	/* Allocate object handle for userspace. */
	err = xa_alloc(&pvr_file->hwrt_handles,
		       &args->handle,
		       hwrt,
		       xa_limit_32b,
		       GFP_KERNEL);
	if (err < 0)
		goto err_cleanup;

	drm_dev_exit(idx);

	return 0;

err_cleanup:
	pvr_hwrt_dataset_put(hwrt);

err_drm_dev_exit:
	drm_dev_exit(idx);

	return err;
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
	struct drm_pvr_ioctl_destroy_hwrt_dataset_args *args = raw_args;
	struct pvr_file *pvr_file = to_pvr_file(file);
	struct pvr_hwrt_dataset *hwrt;

	if (args->_padding_4)
		return -EINVAL;

	hwrt = xa_erase(&pvr_file->hwrt_handles, args->handle);
	if (!hwrt)
		return -EINVAL;

	pvr_hwrt_dataset_put(hwrt);
	return 0;
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
	struct drm_pvr_ioctl_create_vm_context_args *args = raw_args;
	struct pvr_file *pvr_file = to_pvr_file(file);
	struct pvr_vm_context *vm_ctx;
	int idx;
	int err;

	if (!drm_dev_enter(drm_dev, &idx))
		return -EIO;

	if (args->_padding_4) {
		err = -EINVAL;
		goto err_drm_dev_exit;
	}

	vm_ctx = pvr_vm_create_context(pvr_file->pvr_dev, true);
	if (IS_ERR(vm_ctx)) {
		err = PTR_ERR(vm_ctx);
		goto err_drm_dev_exit;
	}

	/* Allocate object handle for userspace. */
	err = xa_alloc(&pvr_file->vm_ctx_handles,
		       &args->handle,
		       vm_ctx,
		       xa_limit_32b,
		       GFP_KERNEL);
	if (err < 0)
		goto err_cleanup;

	drm_dev_exit(idx);

	return 0;

err_cleanup:
	pvr_vm_context_put(vm_ctx);

err_drm_dev_exit:
	drm_dev_exit(idx);

	return err;
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
	struct drm_pvr_ioctl_destroy_vm_context_args *args = raw_args;
	struct pvr_file *pvr_file = to_pvr_file(file);
	struct pvr_vm_context *vm_ctx;

	if (args->_padding_4)
		return -EINVAL;

	vm_ctx = xa_erase(&pvr_file->vm_ctx_handles, args->handle);
	if (!vm_ctx)
		return -EINVAL;

	pvr_vm_context_put(vm_ctx);
	return 0;
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
	struct pvr_device *pvr_dev = to_pvr_device(drm_dev);
	struct drm_pvr_ioctl_vm_map_args *args = raw_args;
	struct pvr_file *pvr_file = to_pvr_file(file);
	struct pvr_vm_context *vm_ctx;

	struct pvr_gem_object *pvr_obj;
	size_t pvr_obj_size;

	u64 offset_plus_size;
	int idx;
	int err;

	if (!drm_dev_enter(drm_dev, &idx))
		return -EIO;

	/* Initial validation of args. */
	if (args->_padding_14) {
		err = -EINVAL;
		goto err_drm_dev_exit;
	}

	if (args->flags != 0 ||
	    check_add_overflow(args->offset, args->size, &offset_plus_size) ||
	    !pvr_find_heap_containing(pvr_dev, args->device_addr, args->size)) {
		err = -EINVAL;
		goto err_drm_dev_exit;
	}

	vm_ctx = pvr_vm_context_lookup(pvr_file, args->vm_context_handle);
	if (!vm_ctx) {
		err = -EINVAL;
		goto err_drm_dev_exit;
	}

	pvr_obj = pvr_gem_object_from_handle(pvr_file, args->handle);
	if (!pvr_obj) {
		err = -ENOENT;
		goto err_put_vm_context;
	}

	pvr_obj_size = pvr_gem_object_size(pvr_obj);

	/*
	 * Validate offset and size args. The alignment of these will be
	 * checked when mapping; for now just check that they're within valid
	 * bounds
	 */
	if (args->offset >= pvr_obj_size || offset_plus_size > pvr_obj_size) {
		err = -EINVAL;
		goto err_put_pvr_object;
	}

	err = pvr_vm_map(vm_ctx, pvr_obj, args->offset,
			 args->device_addr, args->size);
	if (err)
		goto err_put_pvr_object;

	/*
	 * In order to set up the mapping, we needed a reference to &pvr_obj.
	 * However, pvr_vm_map() obtains and stores its own reference, so we
	 * must release ours before returning.
	 */

err_put_pvr_object:
	pvr_gem_object_put(pvr_obj);

err_put_vm_context:
	pvr_vm_context_put(vm_ctx);

err_drm_dev_exit:
	drm_dev_exit(idx);

	return err;
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
	struct drm_pvr_ioctl_vm_unmap_args *args = raw_args;
	struct pvr_file *pvr_file = to_pvr_file(file);
	struct pvr_vm_context *vm_ctx;
	int err;

	/* Initial validation of args. */
	if (args->_padding_4)
		return -EINVAL;

	vm_ctx = pvr_vm_context_lookup(pvr_file, args->vm_context_handle);
	if (!vm_ctx)
		return -EINVAL;

	err = pvr_vm_unmap(vm_ctx, args->device_addr, args->size);

	pvr_vm_context_put(vm_ctx);

	return err;
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
	struct drm_pvr_ioctl_submit_jobs_args *args = raw_args;
	struct pvr_device *pvr_dev = to_pvr_device(drm_dev);
	struct pvr_file *pvr_file = to_pvr_file(file);
	int idx;
	int err;

	if (!drm_dev_enter(drm_dev, &idx))
		return -EIO;

	err = pvr_submit_jobs(pvr_dev, pvr_file, args);

	drm_dev_exit(idx);

	return err;
}

int
pvr_get_uobj(u64 usr_ptr, u32 usr_stride, u32 min_stride, u32 obj_size, void *out)
{
	if (usr_stride < min_stride)
		return -EINVAL;

	return copy_struct_from_user(out, obj_size, u64_to_user_ptr(usr_ptr), usr_stride);
}

int
pvr_set_uobj(u64 usr_ptr, u32 usr_stride, u32 min_stride, u32 obj_size, const void *in)
{
	if (usr_stride < min_stride)
		return -EINVAL;

	if (copy_to_user(u64_to_user_ptr(usr_ptr), in, min_t(u32, usr_stride, obj_size)))
		return -EFAULT;

	if (usr_stride > obj_size &&
	    clear_user(u64_to_user_ptr(usr_ptr + obj_size), usr_stride - obj_size)) {
		return -EFAULT;
	}

	return 0;
}

int
pvr_get_uobj_array(const struct drm_pvr_obj_array *in, u32 min_stride, u32 obj_size, void **out)
{
	int ret = 0;
	void *out_alloc;

	if (in->stride < min_stride)
		return -EINVAL;

	if (!in->count)
		return 0;

	out_alloc = kvmalloc_array(in->count, obj_size, GFP_KERNEL);
	if (!out_alloc)
		return -ENOMEM;

	if (obj_size == in->stride) {
		if (copy_from_user(out_alloc, u64_to_user_ptr(in->array),
				   (unsigned long)obj_size * in->count))
			ret = -EFAULT;
	} else {
		void __user *in_ptr = u64_to_user_ptr(in->array);
		void *out_ptr = out_alloc;

		for (u32 i = 0; i < in->count; i++) {
			ret = copy_struct_from_user(out_ptr, obj_size, in_ptr, in->stride);
			if (ret)
				break;

			out_ptr += obj_size;
			in_ptr += in->stride;
		}
	}

	if (ret) {
		kvfree(out_alloc);
		return ret;
	}

	*out = out_alloc;
	return 0;
}

int
pvr_set_uobj_array(const struct drm_pvr_obj_array *out, u32 min_stride, u32 obj_size,
		   const void *in)
{
	if (out->stride < min_stride)
		return -EINVAL;

	if (!out->count)
		return 0;

	if (obj_size == out->stride) {
		if (copy_to_user(u64_to_user_ptr(out->array), in,
				 (unsigned long)obj_size * out->count))
			return -EFAULT;
	} else {
		u32 cpy_elem_size = min_t(u32, out->stride, obj_size);
		void __user *out_ptr = u64_to_user_ptr(out->array);
		const void *in_ptr = in;

		for (u32 i = 0; i < out->count; i++) {
			if (copy_to_user(out_ptr, in_ptr, cpy_elem_size))
				return -EFAULT;

			out_ptr += obj_size;
			in_ptr += out->stride;
		}

		if (out->stride > obj_size &&
		    clear_user(u64_to_user_ptr(out->array + obj_size),
			       out->stride - obj_size)) {
			return -EFAULT;
		}
	}

	return 0;
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

	INIT_LIST_HEAD(&pvr_file->contexts);

	xa_init_flags(&pvr_file->ctx_handles, XA_FLAGS_ALLOC1);
	xa_init_flags(&pvr_file->free_list_handles, XA_FLAGS_ALLOC1);
	xa_init_flags(&pvr_file->hwrt_handles, XA_FLAGS_ALLOC1);
	xa_init_flags(&pvr_file->vm_ctx_handles, XA_FLAGS_ALLOC1);

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

	/* Kill remaining contexts. */
	pvr_destroy_contexts_for_file(pvr_file);

	/* Drop references on any remaining objects. */
	pvr_destroy_free_lists_for_file(pvr_file);
	pvr_destroy_hwrt_datasets_for_file(pvr_file);
	pvr_destroy_vm_contexts_for_file(pvr_file);

	kfree(pvr_file);
	file->driver_priv = NULL;
}

DEFINE_DRM_GEM_FOPS(pvr_drm_driver_fops);

static struct drm_driver pvr_drm_driver = {
	.driver_features = DRIVER_GEM | DRIVER_GEM_GPUVA | DRIVER_RENDER |
			   DRIVER_SYNCOBJ | DRIVER_SYNCOBJ_TIMELINE,
	.open = pvr_drm_driver_open,
	.postclose = pvr_drm_driver_postclose,
	.ioctls = pvr_drm_driver_ioctls,
	.num_ioctls = ARRAY_SIZE(pvr_drm_driver_ioctls),
	.fops = &pvr_drm_driver_fops,
#if defined(CONFIG_DEBUG_FS)
	.debugfs_init = pvr_debugfs_init,
#endif

	.name = PVR_DRIVER_NAME,
	.desc = PVR_DRIVER_DESC,
	.major = PVR_DRIVER_MAJOR,
	.minor = PVR_DRIVER_MINOR,
	.patchlevel = PVR_DRIVER_PATCHLEVEL,

	.gem_prime_import_sg_table = drm_gem_shmem_prime_import_sg_table,
	.gem_create_object = pvr_gem_create_object,
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

	init_rwsem(&pvr_dev->reset_sem);

	pvr_context_device_init(pvr_dev);

	err = pvr_queue_device_init(pvr_dev);
	if (err)
		goto err_context_fini;

	devm_pm_runtime_enable(&plat_dev->dev);
	pm_runtime_mark_last_busy(&plat_dev->dev);

	pm_runtime_set_autosuspend_delay(&plat_dev->dev, 50);
	pm_runtime_use_autosuspend(&plat_dev->dev);
	pvr_watchdog_init(pvr_dev);

	err = pvr_device_init(pvr_dev);
	if (err)
		goto err_watchdog_fini;

	err = drm_dev_register(drm_dev, 0);
	if (err)
		goto err_device_fini;

	xa_init_flags(&pvr_dev->free_list_ids, XA_FLAGS_ALLOC1);
	xa_init_flags(&pvr_dev->job_ids, XA_FLAGS_ALLOC1);

	return 0;

err_device_fini:
	pvr_device_fini(pvr_dev);

err_watchdog_fini:
	pvr_watchdog_fini(pvr_dev);

	pvr_queue_device_fini(pvr_dev);

err_context_fini:
	pvr_context_device_fini(pvr_dev);

	return err;
}

static void pvr_remove(struct platform_device *plat_dev)
{
	struct drm_device *drm_dev = platform_get_drvdata(plat_dev);
	struct pvr_device *pvr_dev = to_pvr_device(drm_dev);

	WARN_ON(!xa_empty(&pvr_dev->job_ids));
	WARN_ON(!xa_empty(&pvr_dev->free_list_ids));

	xa_destroy(&pvr_dev->job_ids);
	xa_destroy(&pvr_dev->free_list_ids);

	pm_runtime_suspend(drm_dev->dev);
	pvr_device_fini(pvr_dev);
	drm_dev_unplug(drm_dev);
	pvr_watchdog_fini(pvr_dev);
	pvr_queue_device_fini(pvr_dev);
	pvr_context_device_fini(pvr_dev);
}

static const struct of_device_id dt_match[] = {
	{ .compatible = "img,img-axe", .data = NULL },
	{}
};
MODULE_DEVICE_TABLE(of, dt_match);

static const struct dev_pm_ops pvr_pm_ops = {
	RUNTIME_PM_OPS(pvr_power_device_suspend, pvr_power_device_resume, pvr_power_device_idle)
};

static struct platform_driver pvr_driver = {
	.probe = pvr_probe,
	.remove = pvr_remove,
	.driver = {
		.name = PVR_DRIVER_NAME,
		.pm = &pvr_pm_ops,
		.of_match_table = dt_match,
	},
};
module_platform_driver(pvr_driver);

MODULE_AUTHOR("Imagination Technologies Ltd.");
MODULE_DESCRIPTION(PVR_DRIVER_DESC);
MODULE_LICENSE("Dual MIT/GPL");
MODULE_IMPORT_NS("DMA_BUF");
MODULE_FIRMWARE("powervr/rogue_33.15.11.3_v1.fw");
