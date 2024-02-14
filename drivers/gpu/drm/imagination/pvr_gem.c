// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#include "pvr_device.h"
#include "pvr_gem.h"
#include "pvr_vm.h"

#include <drm/drm_gem.h>
#include <drm/drm_prime.h>

#include <linux/compiler.h>
#include <linux/compiler_attributes.h>
#include <linux/dma-buf.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/iosys-map.h>
#include <linux/log2.h>
#include <linux/mutex.h>
#include <linux/pagemap.h>
#include <linux/refcount.h>
#include <linux/scatterlist.h>

static void pvr_gem_object_free(struct drm_gem_object *obj)
{
	drm_gem_shmem_object_free(obj);
}

static int pvr_gem_mmap(struct drm_gem_object *gem_obj, struct vm_area_struct *vma)
{
	struct pvr_gem_object *pvr_obj = gem_to_pvr_gem(gem_obj);
	struct drm_gem_shmem_object *shmem_obj = shmem_gem_from_pvr_gem(pvr_obj);

	if (!(pvr_obj->flags & DRM_PVR_BO_ALLOW_CPU_USERSPACE_ACCESS))
		return -EINVAL;

	return drm_gem_shmem_mmap(shmem_obj, vma);
}

static const struct drm_gem_object_funcs pvr_gem_object_funcs = {
	.free = pvr_gem_object_free,
	.print_info = drm_gem_shmem_object_print_info,
	.pin = drm_gem_shmem_object_pin,
	.unpin = drm_gem_shmem_object_unpin,
	.get_sg_table = drm_gem_shmem_object_get_sg_table,
	.vmap = drm_gem_shmem_object_vmap,
	.vunmap = drm_gem_shmem_object_vunmap,
	.mmap = pvr_gem_mmap,
	.vm_ops = &drm_gem_shmem_vm_ops,
};

/**
 * pvr_gem_object_flags_validate() - Verify that a collection of PowerVR GEM
 * mapping and/or creation flags form a valid combination.
 * @flags: PowerVR GEM mapping/creation flags to validate.
 *
 * This function explicitly allows kernel-only flags. All ioctl entrypoints
 * should do their own validation as well as relying on this function.
 *
 * Return:
 *  * %true if @flags contains valid mapping and/or creation flags, or
 *  * %false otherwise.
 */
static bool
pvr_gem_object_flags_validate(u64 flags)
{
	static const u64 invalid_combinations[] = {
		/*
		 * Memory flagged as PM/FW-protected cannot be mapped to
		 * userspace. To make this explicit, we require that the two
		 * flags allowing each of these respective features are never
		 * specified together.
		 */
		(DRM_PVR_BO_PM_FW_PROTECT |
		 DRM_PVR_BO_ALLOW_CPU_USERSPACE_ACCESS),
	};

	int i;

	/*
	 * Check for bits set in undefined regions. Reserved regions refer to
	 * options that can only be set by the kernel. These are explicitly
	 * allowed in most cases, and must be checked specifically in IOCTL
	 * callback code.
	 */
	if ((flags & PVR_BO_UNDEFINED_MASK) != 0)
		return false;

	/*
	 * Check for all combinations of flags marked as invalid in the array
	 * above.
	 */
	for (i = 0; i < ARRAY_SIZE(invalid_combinations); ++i) {
		u64 combo = invalid_combinations[i];

		if ((flags & combo) == combo)
			return false;
	}

	return true;
}

/**
 * pvr_gem_object_into_handle() - Convert a reference to an object into a
 * userspace-accessible handle.
 * @pvr_obj: [IN] Target PowerVR-specific object.
 * @pvr_file: [IN] File to associate the handle with.
 * @handle: [OUT] Pointer to store the created handle in. Remains unmodified if
 * an error is encountered.
 *
 * If an error is encountered, ownership of @pvr_obj will not have been
 * transferred. If this function succeeds, however, further use of @pvr_obj is
 * considered undefined behaviour unless another reference to it is explicitly
 * held.
 *
 * Return:
 *  * 0 on success, or
 *  * Any error encountered while attempting to allocate a handle on @pvr_file.
 */
int
pvr_gem_object_into_handle(struct pvr_gem_object *pvr_obj,
			   struct pvr_file *pvr_file, u32 *handle)
{
	struct drm_gem_object *gem_obj = gem_from_pvr_gem(pvr_obj);
	struct drm_file *file = from_pvr_file(pvr_file);

	u32 new_handle;
	int err;

	err = drm_gem_handle_create(file, gem_obj, &new_handle);
	if (err)
		return err;

	/*
	 * Release our reference to @pvr_obj, effectively transferring
	 * ownership to the handle.
	 */
	pvr_gem_object_put(pvr_obj);

	/*
	 * Do not store the new handle in @handle until no more errors can
	 * occur.
	 */
	*handle = new_handle;

	return 0;
}

/**
 * pvr_gem_object_from_handle() - Obtain a reference to an object from a
 * userspace handle.
 * @pvr_file: PowerVR-specific file to which @handle is associated.
 * @handle: Userspace handle referencing the target object.
 *
 * On return, @handle always maintains its reference to the requested object
 * (if it had one in the first place). If this function succeeds, the returned
 * object will hold an additional reference. When the caller is finished with
 * the returned object, they should call pvr_gem_object_put() on it to release
 * this reference.
 *
 * Return:
 *  * A pointer to the requested PowerVR-specific object on success, or
 *  * %NULL otherwise.
 */
struct pvr_gem_object *
pvr_gem_object_from_handle(struct pvr_file *pvr_file, u32 handle)
{
	struct drm_file *file = from_pvr_file(pvr_file);
	struct drm_gem_object *gem_obj;

	gem_obj = drm_gem_object_lookup(file, handle);
	if (!gem_obj)
		return NULL;

	return gem_to_pvr_gem(gem_obj);
}

/**
 * pvr_gem_object_vmap() - Map a PowerVR GEM object into CPU virtual address
 * space.
 * @pvr_obj: Target PowerVR GEM object.
 *
 * Once the caller is finished with the CPU mapping, they must call
 * pvr_gem_object_vunmap() on @pvr_obj.
 *
 * If @pvr_obj is CPU-cached, dma_sync_sgtable_for_cpu() is called to make
 * sure the CPU mapping is consistent.
 *
 * Return:
 *  * A pointer to the CPU mapping on success,
 *  * -%ENOMEM if the mapping fails, or
 *  * Any error encountered while attempting to acquire a reference to the
 *    backing pages for @pvr_obj.
 */
void *
pvr_gem_object_vmap(struct pvr_gem_object *pvr_obj)
{
	struct drm_gem_shmem_object *shmem_obj = shmem_gem_from_pvr_gem(pvr_obj);
	struct drm_gem_object *obj = gem_from_pvr_gem(pvr_obj);
	struct iosys_map map;
	int err;

	dma_resv_lock(obj->resv, NULL);

	err = drm_gem_shmem_vmap(shmem_obj, &map);
	if (err)
		goto err_unlock;

	if (pvr_obj->flags & PVR_BO_CPU_CACHED) {
		struct device *dev = shmem_obj->base.dev->dev;

		/* If shmem_obj->sgt is NULL, that means the buffer hasn't been mapped
		 * in GPU space yet.
		 */
		if (shmem_obj->sgt)
			dma_sync_sgtable_for_cpu(dev, shmem_obj->sgt, DMA_BIDIRECTIONAL);
	}

	dma_resv_unlock(obj->resv);

	return map.vaddr;

err_unlock:
	dma_resv_unlock(obj->resv);

	return ERR_PTR(err);
}

/**
 * pvr_gem_object_vunmap() - Unmap a PowerVR memory object from CPU virtual
 * address space.
 * @pvr_obj: Target PowerVR GEM object.
 *
 * If @pvr_obj is CPU-cached, dma_sync_sgtable_for_device() is called to make
 * sure the GPU mapping is consistent.
 */
void
pvr_gem_object_vunmap(struct pvr_gem_object *pvr_obj)
{
	struct drm_gem_shmem_object *shmem_obj = shmem_gem_from_pvr_gem(pvr_obj);
	struct iosys_map map = IOSYS_MAP_INIT_VADDR(shmem_obj->vaddr);
	struct drm_gem_object *obj = gem_from_pvr_gem(pvr_obj);

	if (WARN_ON(!map.vaddr))
		return;

	dma_resv_lock(obj->resv, NULL);

	if (pvr_obj->flags & PVR_BO_CPU_CACHED) {
		struct device *dev = shmem_obj->base.dev->dev;

		/* If shmem_obj->sgt is NULL, that means the buffer hasn't been mapped
		 * in GPU space yet.
		 */
		if (shmem_obj->sgt)
			dma_sync_sgtable_for_device(dev, shmem_obj->sgt, DMA_BIDIRECTIONAL);
	}

	drm_gem_shmem_vunmap(shmem_obj, &map);

	dma_resv_unlock(obj->resv);
}

/**
 * pvr_gem_object_zero() - Zeroes the physical memory behind an object.
 * @pvr_obj: Target PowerVR GEM object.
 *
 * Return:
 *  * 0 on success, or
 *  * Any error encountered while attempting to map @pvr_obj to the CPU (see
 *    pvr_gem_object_vmap()).
 */
static int
pvr_gem_object_zero(struct pvr_gem_object *pvr_obj)
{
	void *cpu_ptr;

	cpu_ptr = pvr_gem_object_vmap(pvr_obj);
	if (IS_ERR(cpu_ptr))
		return PTR_ERR(cpu_ptr);

	memset(cpu_ptr, 0, pvr_gem_object_size(pvr_obj));

	/* Make sure the zero-ing is done before vumap-ing the object. */
	wmb();

	pvr_gem_object_vunmap(pvr_obj);

	return 0;
}

/**
 * pvr_gem_create_object() - Allocate and pre-initializes a pvr_gem_object
 * @drm_dev: DRM device creating this object.
 * @size: Size of the object to allocate in bytes.
 *
 * Return:
 *  * The new pre-initialized GEM object on success,
 *  * -ENOMEM if the allocation failed.
 */
struct drm_gem_object *pvr_gem_create_object(struct drm_device *drm_dev, size_t size)
{
	struct drm_gem_object *gem_obj;
	struct pvr_gem_object *pvr_obj;

	pvr_obj = kzalloc(sizeof(*pvr_obj), GFP_KERNEL);
	if (!pvr_obj)
		return ERR_PTR(-ENOMEM);

	gem_obj = gem_from_pvr_gem(pvr_obj);
	gem_obj->funcs = &pvr_gem_object_funcs;

	return gem_obj;
}

/**
 * pvr_gem_object_create() - Creates a PowerVR-specific buffer object.
 * @pvr_dev: Target PowerVR device.
 * @size: Size of the object to allocate in bytes. Must be greater than zero.
 * Any value which is not an exact multiple of the system page size will be
 * rounded up to satisfy this condition.
 * @flags: Options which affect both this operation and future mapping
 * operations performed on the returned object. Must be a combination of
 * DRM_PVR_BO_* and/or PVR_BO_* flags.
 *
 * The created object may be larger than @size, but can never be smaller. To
 * get the exact size, call pvr_gem_object_size() on the returned pointer.
 *
 * Return:
 *  * The newly-minted PowerVR-specific buffer object on success,
 *  * -%EINVAL if @size is zero or @flags is not valid,
 *  * -%ENOMEM if sufficient physical memory cannot be allocated, or
 *  * Any other error returned by drm_gem_create_mmap_offset().
 */
struct pvr_gem_object *
pvr_gem_object_create(struct pvr_device *pvr_dev, size_t size, u64 flags)
{
	struct drm_gem_shmem_object *shmem_obj;
	struct pvr_gem_object *pvr_obj;
	struct sg_table *sgt;
	int err;

	/* Verify @size and @flags before continuing. */
	if (size == 0 || !pvr_gem_object_flags_validate(flags))
		return ERR_PTR(-EINVAL);

	shmem_obj = drm_gem_shmem_create(from_pvr_device(pvr_dev), size);
	if (IS_ERR(shmem_obj))
		return ERR_CAST(shmem_obj);

	shmem_obj->pages_mark_dirty_on_put = true;
	shmem_obj->map_wc = !(flags & PVR_BO_CPU_CACHED);
	pvr_obj = shmem_gem_to_pvr_gem(shmem_obj);
	pvr_obj->flags = flags;

	sgt = drm_gem_shmem_get_pages_sgt(shmem_obj);
	if (IS_ERR(sgt)) {
		err = PTR_ERR(sgt);
		goto err_shmem_object_free;
	}

	dma_sync_sgtable_for_device(shmem_obj->base.dev->dev, sgt,
				    DMA_BIDIRECTIONAL);

	/*
	 * Do this last because pvr_gem_object_zero() requires a fully
	 * configured instance of struct pvr_gem_object.
	 */
	pvr_gem_object_zero(pvr_obj);

	return pvr_obj;

err_shmem_object_free:
	drm_gem_shmem_free(shmem_obj);

	return ERR_PTR(err);
}

/**
 * pvr_gem_get_dma_addr() - Get DMA address for given offset in object
 * @pvr_obj: Pointer to object to lookup address in.
 * @offset: Offset within object to lookup address at.
 * @dma_addr_out: Pointer to location to store DMA address.
 *
 * Returns:
 *  * 0 on success, or
 *  * -%EINVAL if object is not currently backed, or if @offset is out of valid
 *    range for this object.
 */
int
pvr_gem_get_dma_addr(struct pvr_gem_object *pvr_obj, u32 offset,
		     dma_addr_t *dma_addr_out)
{
	struct drm_gem_shmem_object *shmem_obj = shmem_gem_from_pvr_gem(pvr_obj);
	u32 accumulated_offset = 0;
	struct scatterlist *sgl;
	unsigned int sgt_idx;

	WARN_ON(!shmem_obj->sgt);
	for_each_sgtable_dma_sg(shmem_obj->sgt, sgl, sgt_idx) {
		u32 new_offset = accumulated_offset + sg_dma_len(sgl);

		if (offset >= accumulated_offset && offset < new_offset) {
			*dma_addr_out = sg_dma_address(sgl) +
					(offset - accumulated_offset);
			return 0;
		}

		accumulated_offset = new_offset;
	}

	return -EINVAL;
}
