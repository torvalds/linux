/*
 * drm gem CMA (contiguous memory allocator) helper functions
 *
 * Copyright (C) 2012 Sascha Hauer, Pengutronix
 *
 * Based on Samsung Exynos code
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/export.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>

#include <drm/drmP.h>
#include <drm/drm.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_vma_manager.h>

/**
 * DOC: cma helpers
 *
 * The Contiguous Memory Allocator reserves a pool of memory at early boot
 * that is used to service requests for large blocks of contiguous memory.
 *
 * The DRM GEM/CMA helpers use this allocator as a means to provide buffer
 * objects that are physically contiguous in memory. This is useful for
 * display drivers that are unable to map scattered buffers via an IOMMU.
 */

/**
 * __drm_gem_cma_create - Create a GEM CMA object without allocating memory
 * @drm: DRM device
 * @size: size of the object to allocate
 *
 * This function creates and initializes a GEM CMA object of the given size,
 * but doesn't allocate any memory to back the object.
 *
 * Returns:
 * A struct drm_gem_cma_object * on success or an ERR_PTR()-encoded negative
 * error code on failure.
 */
static struct drm_gem_cma_object *
__drm_gem_cma_create(struct drm_device *drm, size_t size)
{
	struct drm_gem_cma_object *cma_obj;
	struct drm_gem_object *gem_obj;
	int ret;

	cma_obj = kzalloc(sizeof(*cma_obj), GFP_KERNEL);
	if (!cma_obj)
		return ERR_PTR(-ENOMEM);

	gem_obj = &cma_obj->base;

	ret = drm_gem_object_init(drm, gem_obj, size);
	if (ret)
		goto error;

	ret = drm_gem_create_mmap_offset(gem_obj);
	if (ret) {
		drm_gem_object_release(gem_obj);
		goto error;
	}

	return cma_obj;

error:
	kfree(cma_obj);
	return ERR_PTR(ret);
}

/**
 * drm_gem_cma_create - allocate an object with the given size
 * @drm: DRM device
 * @size: size of the object to allocate
 *
 * This function creates a CMA GEM object and allocates a contiguous chunk of
 * memory as backing store. The backing memory has the writecombine attribute
 * set.
 *
 * Returns:
 * A struct drm_gem_cma_object * on success or an ERR_PTR()-encoded negative
 * error code on failure.
 */
struct drm_gem_cma_object *drm_gem_cma_create(struct drm_device *drm,
					      size_t size)
{
	struct drm_gem_cma_object *cma_obj;
	int ret;

	size = round_up(size, PAGE_SIZE);

	cma_obj = __drm_gem_cma_create(drm, size);
	if (IS_ERR(cma_obj))
		return cma_obj;

	cma_obj->vaddr = dma_alloc_writecombine(drm->dev, size,
			&cma_obj->paddr, GFP_KERNEL | __GFP_NOWARN);
	if (!cma_obj->vaddr) {
		dev_err(drm->dev, "failed to allocate buffer with size %d\n",
			size);
		ret = -ENOMEM;
		goto error;
	}

	return cma_obj;

error:
	drm_gem_cma_free_object(&cma_obj->base);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(drm_gem_cma_create);

/**
 * drm_gem_cma_create_with_handle - allocate an object with the given size and
 *     return a GEM handle to it
 * @file_priv: DRM file-private structure to register the handle for
 * @drm: DRM device
 * @size: size of the object to allocate
 * @handle: return location for the GEM handle
 *
 * This function creates a CMA GEM object, allocating a physically contiguous
 * chunk of memory as backing store. The GEM object is then added to the list
 * of object associated with the given file and a handle to it is returned.
 *
 * Returns:
 * A struct drm_gem_cma_object * on success or an ERR_PTR()-encoded negative
 * error code on failure.
 */
static struct drm_gem_cma_object *
drm_gem_cma_create_with_handle(struct drm_file *file_priv,
			       struct drm_device *drm, size_t size,
			       uint32_t *handle)
{
	struct drm_gem_cma_object *cma_obj;
	struct drm_gem_object *gem_obj;
	int ret;

	cma_obj = drm_gem_cma_create(drm, size);
	if (IS_ERR(cma_obj))
		return cma_obj;

	gem_obj = &cma_obj->base;

	/*
	 * allocate a id of idr table where the obj is registered
	 * and handle has the id what user can see.
	 */
	ret = drm_gem_handle_create(file_priv, gem_obj, handle);
	if (ret)
		goto err_handle_create;

	/* drop reference from allocate - handle holds it now. */
	drm_gem_object_unreference_unlocked(gem_obj);

	return cma_obj;

err_handle_create:
	drm_gem_cma_free_object(gem_obj);

	return ERR_PTR(ret);
}

/**
 * drm_gem_cma_free_object - free resources associated with a CMA GEM object
 * @gem_obj: GEM object to free
 *
 * This function frees the backing memory of the CMA GEM object, cleans up the
 * GEM object state and frees the memory used to store the object itself.
 * Drivers using the CMA helpers should set this as their DRM driver's
 * ->gem_free_object() callback.
 */
void drm_gem_cma_free_object(struct drm_gem_object *gem_obj)
{
	struct drm_gem_cma_object *cma_obj;

	cma_obj = to_drm_gem_cma_obj(gem_obj);

	if (cma_obj->vaddr) {
		dma_free_writecombine(gem_obj->dev->dev, cma_obj->base.size,
				      cma_obj->vaddr, cma_obj->paddr);
	} else if (gem_obj->import_attach) {
		drm_prime_gem_destroy(gem_obj, cma_obj->sgt);
	}

	drm_gem_object_release(gem_obj);

	kfree(cma_obj);
}
EXPORT_SYMBOL_GPL(drm_gem_cma_free_object);

/**
 * drm_gem_cma_dumb_create_internal - create a dumb buffer object
 * @file_priv: DRM file-private structure to create the dumb buffer for
 * @drm: DRM device
 * @args: IOCTL data
 *
 * This aligns the pitch and size arguments to the minimum required. This is
 * an internal helper that can be wrapped by a driver to account for hardware
 * with more specific alignment requirements. It should not be used directly
 * as the ->dumb_create() callback in a DRM driver.
 *
 * Returns:
 * 0 on success or a negative error code on failure.
 */
int drm_gem_cma_dumb_create_internal(struct drm_file *file_priv,
				     struct drm_device *drm,
				     struct drm_mode_create_dumb *args)
{
	unsigned int min_pitch = DIV_ROUND_UP(args->width * args->bpp, 8);
	struct drm_gem_cma_object *cma_obj;

	if (args->pitch < min_pitch)
		args->pitch = min_pitch;

	if (args->size < args->pitch * args->height)
		args->size = args->pitch * args->height;

	cma_obj = drm_gem_cma_create_with_handle(file_priv, drm, args->size,
						 &args->handle);
	return PTR_ERR_OR_ZERO(cma_obj);
}
EXPORT_SYMBOL_GPL(drm_gem_cma_dumb_create_internal);

/**
 * drm_gem_cma_dumb_create - create a dumb buffer object
 * @file_priv: DRM file-private structure to create the dumb buffer for
 * @drm: DRM device
 * @args: IOCTL data
 *
 * This function computes the pitch of the dumb buffer and rounds it up to an
 * integer number of bytes per pixel. Drivers for hardware that doesn't have
 * any additional restrictions on the pitch can directly use this function as
 * their ->dumb_create() callback.
 *
 * For hardware with additional restrictions, drivers can adjust the fields
 * set up by userspace and pass the IOCTL data along to the
 * drm_gem_cma_dumb_create_internal() function.
 *
 * Returns:
 * 0 on success or a negative error code on failure.
 */
int drm_gem_cma_dumb_create(struct drm_file *file_priv,
			    struct drm_device *drm,
			    struct drm_mode_create_dumb *args)
{
	struct drm_gem_cma_object *cma_obj;

	args->pitch = DIV_ROUND_UP(args->width * args->bpp, 8);
	args->size = args->pitch * args->height;

	cma_obj = drm_gem_cma_create_with_handle(file_priv, drm, args->size,
						 &args->handle);
	return PTR_ERR_OR_ZERO(cma_obj);
}
EXPORT_SYMBOL_GPL(drm_gem_cma_dumb_create);

/**
 * drm_gem_cma_dumb_map_offset - return the fake mmap offset for a CMA GEM
 *     object
 * @file_priv: DRM file-private structure containing the GEM object
 * @drm: DRM device
 * @handle: GEM object handle
 * @offset: return location for the fake mmap offset
 *
 * This function look up an object by its handle and returns the fake mmap
 * offset associated with it. Drivers using the CMA helpers should set this
 * as their DRM driver's ->dumb_map_offset() callback.
 *
 * Returns:
 * 0 on success or a negative error code on failure.
 */
int drm_gem_cma_dumb_map_offset(struct drm_file *file_priv,
				struct drm_device *drm, u32 handle,
				u64 *offset)
{
	struct drm_gem_object *gem_obj;

	mutex_lock(&drm->struct_mutex);

	gem_obj = drm_gem_object_lookup(drm, file_priv, handle);
	if (!gem_obj) {
		dev_err(drm->dev, "failed to lookup GEM object\n");
		mutex_unlock(&drm->struct_mutex);
		return -EINVAL;
	}

	*offset = drm_vma_node_offset_addr(&gem_obj->vma_node);

	drm_gem_object_unreference(gem_obj);

	mutex_unlock(&drm->struct_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(drm_gem_cma_dumb_map_offset);

const struct vm_operations_struct drm_gem_cma_vm_ops = {
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};
EXPORT_SYMBOL_GPL(drm_gem_cma_vm_ops);

static int drm_gem_cma_mmap_obj(struct drm_gem_cma_object *cma_obj,
				struct vm_area_struct *vma)
{
	int ret;

	/*
	 * Clear the VM_PFNMAP flag that was set by drm_gem_mmap(), and set the
	 * vm_pgoff (used as a fake buffer offset by DRM) to 0 as we want to map
	 * the whole buffer.
	 */
	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_pgoff = 0;

	ret = dma_mmap_writecombine(cma_obj->base.dev->dev, vma,
				    cma_obj->vaddr, cma_obj->paddr,
				    vma->vm_end - vma->vm_start);
	if (ret)
		drm_gem_vm_close(vma);

	return ret;
}

/**
 * drm_gem_cma_mmap - memory-map a CMA GEM object
 * @filp: file object
 * @vma: VMA for the area to be mapped
 *
 * This function implements an augmented version of the GEM DRM file mmap
 * operation for CMA objects: In addition to the usual GEM VMA setup it
 * immediately faults in the entire object instead of using on-demaind
 * faulting. Drivers which employ the CMA helpers should use this function
 * as their ->mmap() handler in the DRM device file's file_operations
 * structure.
 *
 * Returns:
 * 0 on success or a negative error code on failure.
 */
int drm_gem_cma_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_gem_cma_object *cma_obj;
	struct drm_gem_object *gem_obj;
	int ret;

	ret = drm_gem_mmap(filp, vma);
	if (ret)
		return ret;

	gem_obj = vma->vm_private_data;
	cma_obj = to_drm_gem_cma_obj(gem_obj);

	return drm_gem_cma_mmap_obj(cma_obj, vma);
}
EXPORT_SYMBOL_GPL(drm_gem_cma_mmap);

#ifdef CONFIG_DEBUG_FS
/**
 * drm_gem_cma_describe - describe a CMA GEM object for debugfs
 * @cma_obj: CMA GEM object
 * @m: debugfs file handle
 *
 * This function can be used to dump a human-readable representation of the
 * CMA GEM object into a synthetic file.
 */
void drm_gem_cma_describe(struct drm_gem_cma_object *cma_obj,
			  struct seq_file *m)
{
	struct drm_gem_object *obj = &cma_obj->base;
	struct drm_device *dev = obj->dev;
	uint64_t off;

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	off = drm_vma_node_start(&obj->vma_node);

	seq_printf(m, "%2d (%2d) %08llx %pad %p %d",
			obj->name, obj->refcount.refcount.counter,
			off, &cma_obj->paddr, cma_obj->vaddr, obj->size);

	seq_printf(m, "\n");
}
EXPORT_SYMBOL_GPL(drm_gem_cma_describe);
#endif

/**
 * drm_gem_cma_prime_get_sg_table - provide a scatter/gather table of pinned
 *     pages for a CMA GEM object
 * @obj: GEM object
 *
 * This function exports a scatter/gather table suitable for PRIME usage by
 * calling the standard DMA mapping API. Drivers using the CMA helpers should
 * set this as their DRM driver's ->gem_prime_get_sg_table() callback.
 *
 * Returns:
 * A pointer to the scatter/gather table of pinned pages or NULL on failure.
 */
struct sg_table *drm_gem_cma_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct drm_gem_cma_object *cma_obj = to_drm_gem_cma_obj(obj);
	struct sg_table *sgt;
	int ret;

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return NULL;

	ret = dma_get_sgtable(obj->dev->dev, sgt, cma_obj->vaddr,
			      cma_obj->paddr, obj->size);
	if (ret < 0)
		goto out;

	return sgt;

out:
	kfree(sgt);
	return NULL;
}
EXPORT_SYMBOL_GPL(drm_gem_cma_prime_get_sg_table);

/**
 * drm_gem_cma_prime_import_sg_table - produce a CMA GEM object from another
 *     driver's scatter/gather table of pinned pages
 * @dev: device to import into
 * @attach: DMA-BUF attachment
 * @sgt: scatter/gather table of pinned pages
 *
 * This function imports a scatter/gather table exported via DMA-BUF by
 * another driver. Imported buffers must be physically contiguous in memory
 * (i.e. the scatter/gather table must contain a single entry). Drivers that
 * use the CMA helpers should set this as their DRM driver's
 * ->gem_prime_import_sg_table() callback.
 *
 * Returns:
 * A pointer to a newly created GEM object or an ERR_PTR-encoded negative
 * error code on failure.
 */
struct drm_gem_object *
drm_gem_cma_prime_import_sg_table(struct drm_device *dev,
				  struct dma_buf_attachment *attach,
				  struct sg_table *sgt)
{
	struct drm_gem_cma_object *cma_obj;

	if (sgt->nents != 1)
		return ERR_PTR(-EINVAL);

	/* Create a CMA GEM buffer. */
	cma_obj = __drm_gem_cma_create(dev, attach->dmabuf->size);
	if (IS_ERR(cma_obj))
		return ERR_CAST(cma_obj);

	cma_obj->paddr = sg_dma_address(sgt->sgl);
	cma_obj->sgt = sgt;

	DRM_DEBUG_PRIME("dma_addr = %pad, size = %zu\n", &cma_obj->paddr, attach->dmabuf->size);

	return &cma_obj->base;
}
EXPORT_SYMBOL_GPL(drm_gem_cma_prime_import_sg_table);

/**
 * drm_gem_cma_prime_mmap - memory-map an exported CMA GEM object
 * @obj: GEM object
 * @vma: VMA for the area to be mapped
 *
 * This function maps a buffer imported via DRM PRIME into a userspace
 * process's address space. Drivers that use the CMA helpers should set this
 * as their DRM driver's ->gem_prime_mmap() callback.
 *
 * Returns:
 * 0 on success or a negative error code on failure.
 */
int drm_gem_cma_prime_mmap(struct drm_gem_object *obj,
			   struct vm_area_struct *vma)
{
	struct drm_gem_cma_object *cma_obj;
	struct drm_device *dev = obj->dev;
	int ret;

	mutex_lock(&dev->struct_mutex);
	ret = drm_gem_mmap_obj(obj, obj->size, vma);
	mutex_unlock(&dev->struct_mutex);
	if (ret < 0)
		return ret;

	cma_obj = to_drm_gem_cma_obj(obj);
	return drm_gem_cma_mmap_obj(cma_obj, vma);
}
EXPORT_SYMBOL_GPL(drm_gem_cma_prime_mmap);

/**
 * drm_gem_cma_prime_vmap - map a CMA GEM object into the kernel's virtual
 *     address space
 * @obj: GEM object
 *
 * This function maps a buffer exported via DRM PRIME into the kernel's
 * virtual address space. Since the CMA buffers are already mapped into the
 * kernel virtual address space this simply returns the cached virtual
 * address. Drivers using the CMA helpers should set this as their DRM
 * driver's ->gem_prime_vmap() callback.
 *
 * Returns:
 * The kernel virtual address of the CMA GEM object's backing store.
 */
void *drm_gem_cma_prime_vmap(struct drm_gem_object *obj)
{
	struct drm_gem_cma_object *cma_obj = to_drm_gem_cma_obj(obj);

	return cma_obj->vaddr;
}
EXPORT_SYMBOL_GPL(drm_gem_cma_prime_vmap);

/**
 * drm_gem_cma_prime_vunmap - unmap a CMA GEM object from the kernel's virtual
 *     address space
 * @obj: GEM object
 * @vaddr: kernel virtual address where the CMA GEM object was mapped
 *
 * This function removes a buffer exported via DRM PRIME from the kernel's
 * virtual address space. This is a no-op because CMA buffers cannot be
 * unmapped from kernel space. Drivers using the CMA helpers should set this
 * as their DRM driver's ->gem_prime_vunmap() callback.
 */
void drm_gem_cma_prime_vunmap(struct drm_gem_object *obj, void *vaddr)
{
	/* Nothing to do */
}
EXPORT_SYMBOL_GPL(drm_gem_cma_prime_vunmap);
