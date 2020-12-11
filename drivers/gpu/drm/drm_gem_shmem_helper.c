// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018 Noralf Trønnes
 */

#include <linux/dma-buf.h>
#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/shmem_fs.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <drm/drm.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_prime.h>
#include <drm/drm_print.h>

/**
 * DOC: overview
 *
 * This library provides helpers for GEM objects backed by shmem buffers
 * allocated using anonymous pageable memory.
 */

static const struct drm_gem_object_funcs drm_gem_shmem_funcs = {
	.free = drm_gem_shmem_free_object,
	.print_info = drm_gem_shmem_print_info,
	.pin = drm_gem_shmem_pin,
	.unpin = drm_gem_shmem_unpin,
	.get_sg_table = drm_gem_shmem_get_sg_table,
	.vmap = drm_gem_shmem_vmap,
	.vunmap = drm_gem_shmem_vunmap,
	.mmap = drm_gem_shmem_mmap,
};

static struct drm_gem_shmem_object *
__drm_gem_shmem_create(struct drm_device *dev, size_t size, bool private)
{
	struct drm_gem_shmem_object *shmem;
	struct drm_gem_object *obj;
	int ret = 0;

	size = PAGE_ALIGN(size);

	if (dev->driver->gem_create_object)
		obj = dev->driver->gem_create_object(dev, size);
	else
		obj = kzalloc(sizeof(*shmem), GFP_KERNEL);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	if (!obj->funcs)
		obj->funcs = &drm_gem_shmem_funcs;

	if (private)
		drm_gem_private_object_init(dev, obj, size);
	else
		ret = drm_gem_object_init(dev, obj, size);
	if (ret)
		goto err_free;

	ret = drm_gem_create_mmap_offset(obj);
	if (ret)
		goto err_release;

	shmem = to_drm_gem_shmem_obj(obj);
	mutex_init(&shmem->pages_lock);
	mutex_init(&shmem->vmap_lock);
	INIT_LIST_HEAD(&shmem->madv_list);

	if (!private) {
		/*
		 * Our buffers are kept pinned, so allocating them
		 * from the MOVABLE zone is a really bad idea, and
		 * conflicts with CMA. See comments above new_inode()
		 * why this is required _and_ expected if you're
		 * going to pin these pages.
		 */
		mapping_set_gfp_mask(obj->filp->f_mapping, GFP_HIGHUSER |
				     __GFP_RETRY_MAYFAIL | __GFP_NOWARN);
	}

	return shmem;

err_release:
	drm_gem_object_release(obj);
err_free:
	kfree(obj);

	return ERR_PTR(ret);
}
/**
 * drm_gem_shmem_create - Allocate an object with the given size
 * @dev: DRM device
 * @size: Size of the object to allocate
 *
 * This function creates a shmem GEM object.
 *
 * Returns:
 * A struct drm_gem_shmem_object * on success or an ERR_PTR()-encoded negative
 * error code on failure.
 */
struct drm_gem_shmem_object *drm_gem_shmem_create(struct drm_device *dev, size_t size)
{
	return __drm_gem_shmem_create(dev, size, false);
}
EXPORT_SYMBOL_GPL(drm_gem_shmem_create);

/**
 * drm_gem_shmem_free_object - Free resources associated with a shmem GEM object
 * @obj: GEM object to free
 *
 * This function cleans up the GEM object state and frees the memory used to
 * store the object itself. It should be used to implement
 * &drm_gem_object_funcs.free.
 */
void drm_gem_shmem_free_object(struct drm_gem_object *obj)
{
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);

	WARN_ON(shmem->vmap_use_count);

	if (obj->import_attach) {
		drm_prime_gem_destroy(obj, shmem->sgt);
	} else {
		if (shmem->sgt) {
			dma_unmap_sgtable(obj->dev->dev, shmem->sgt,
					  DMA_BIDIRECTIONAL, 0);
			sg_free_table(shmem->sgt);
			kfree(shmem->sgt);
		}
		if (shmem->pages)
			drm_gem_shmem_put_pages(shmem);
	}

	WARN_ON(shmem->pages_use_count);

	drm_gem_object_release(obj);
	mutex_destroy(&shmem->pages_lock);
	mutex_destroy(&shmem->vmap_lock);
	kfree(shmem);
}
EXPORT_SYMBOL_GPL(drm_gem_shmem_free_object);

static int drm_gem_shmem_get_pages_locked(struct drm_gem_shmem_object *shmem)
{
	struct drm_gem_object *obj = &shmem->base;
	struct page **pages;

	if (shmem->pages_use_count++ > 0)
		return 0;

	pages = drm_gem_get_pages(obj);
	if (IS_ERR(pages)) {
		DRM_DEBUG_KMS("Failed to get pages (%ld)\n", PTR_ERR(pages));
		shmem->pages_use_count = 0;
		return PTR_ERR(pages);
	}

	shmem->pages = pages;

	return 0;
}

/*
 * drm_gem_shmem_get_pages - Allocate backing pages for a shmem GEM object
 * @shmem: shmem GEM object
 *
 * This function makes sure that backing pages exists for the shmem GEM object
 * and increases the use count.
 *
 * Returns:
 * 0 on success or a negative error code on failure.
 */
int drm_gem_shmem_get_pages(struct drm_gem_shmem_object *shmem)
{
	int ret;

	WARN_ON(shmem->base.import_attach);

	ret = mutex_lock_interruptible(&shmem->pages_lock);
	if (ret)
		return ret;
	ret = drm_gem_shmem_get_pages_locked(shmem);
	mutex_unlock(&shmem->pages_lock);

	return ret;
}
EXPORT_SYMBOL(drm_gem_shmem_get_pages);

static void drm_gem_shmem_put_pages_locked(struct drm_gem_shmem_object *shmem)
{
	struct drm_gem_object *obj = &shmem->base;

	if (WARN_ON_ONCE(!shmem->pages_use_count))
		return;

	if (--shmem->pages_use_count > 0)
		return;

	drm_gem_put_pages(obj, shmem->pages,
			  shmem->pages_mark_dirty_on_put,
			  shmem->pages_mark_accessed_on_put);
	shmem->pages = NULL;
}

/*
 * drm_gem_shmem_put_pages - Decrease use count on the backing pages for a shmem GEM object
 * @shmem: shmem GEM object
 *
 * This function decreases the use count and puts the backing pages when use drops to zero.
 */
void drm_gem_shmem_put_pages(struct drm_gem_shmem_object *shmem)
{
	mutex_lock(&shmem->pages_lock);
	drm_gem_shmem_put_pages_locked(shmem);
	mutex_unlock(&shmem->pages_lock);
}
EXPORT_SYMBOL(drm_gem_shmem_put_pages);

/**
 * drm_gem_shmem_pin - Pin backing pages for a shmem GEM object
 * @obj: GEM object
 *
 * This function makes sure the backing pages are pinned in memory while the
 * buffer is exported. It should only be used to implement
 * &drm_gem_object_funcs.pin.
 *
 * Returns:
 * 0 on success or a negative error code on failure.
 */
int drm_gem_shmem_pin(struct drm_gem_object *obj)
{
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);

	WARN_ON(shmem->base.import_attach);

	return drm_gem_shmem_get_pages(shmem);
}
EXPORT_SYMBOL(drm_gem_shmem_pin);

/**
 * drm_gem_shmem_unpin - Unpin backing pages for a shmem GEM object
 * @obj: GEM object
 *
 * This function removes the requirement that the backing pages are pinned in
 * memory. It should only be used to implement &drm_gem_object_funcs.unpin.
 */
void drm_gem_shmem_unpin(struct drm_gem_object *obj)
{
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);

	WARN_ON(shmem->base.import_attach);

	drm_gem_shmem_put_pages(shmem);
}
EXPORT_SYMBOL(drm_gem_shmem_unpin);

static void *drm_gem_shmem_vmap_locked(struct drm_gem_shmem_object *shmem)
{
	struct drm_gem_object *obj = &shmem->base;
	int ret;

	if (shmem->vmap_use_count++ > 0)
		return shmem->vaddr;

	if (obj->import_attach) {
		shmem->vaddr = dma_buf_vmap(obj->import_attach->dmabuf);
	} else {
		pgprot_t prot = PAGE_KERNEL;

		ret = drm_gem_shmem_get_pages(shmem);
		if (ret)
			goto err_zero_use;

		if (!shmem->map_cached)
			prot = pgprot_writecombine(prot);
		shmem->vaddr = vmap(shmem->pages, obj->size >> PAGE_SHIFT,
				    VM_MAP, prot);
	}

	if (!shmem->vaddr) {
		DRM_DEBUG_KMS("Failed to vmap pages\n");
		ret = -ENOMEM;
		goto err_put_pages;
	}

	return shmem->vaddr;

err_put_pages:
	if (!obj->import_attach)
		drm_gem_shmem_put_pages(shmem);
err_zero_use:
	shmem->vmap_use_count = 0;

	return ERR_PTR(ret);
}

/*
 * drm_gem_shmem_vmap - Create a virtual mapping for a shmem GEM object
 * @shmem: shmem GEM object
 *
 * This function makes sure that a contiguous kernel virtual address mapping
 * exists for the buffer backing the shmem GEM object.
 *
 * This function can be used to implement &drm_gem_object_funcs.vmap. But it can
 * also be called by drivers directly, in which case it will hide the
 * differences between dma-buf imported and natively allocated objects.
 *
 * Acquired mappings should be cleaned up by calling drm_gem_shmem_vunmap().
 *
 * Returns:
 * 0 on success or a negative error code on failure.
 */
void *drm_gem_shmem_vmap(struct drm_gem_object *obj)
{
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);
	void *vaddr;
	int ret;

	ret = mutex_lock_interruptible(&shmem->vmap_lock);
	if (ret)
		return ERR_PTR(ret);
	vaddr = drm_gem_shmem_vmap_locked(shmem);
	mutex_unlock(&shmem->vmap_lock);

	return vaddr;
}
EXPORT_SYMBOL(drm_gem_shmem_vmap);

static void drm_gem_shmem_vunmap_locked(struct drm_gem_shmem_object *shmem)
{
	struct drm_gem_object *obj = &shmem->base;

	if (WARN_ON_ONCE(!shmem->vmap_use_count))
		return;

	if (--shmem->vmap_use_count > 0)
		return;

	if (obj->import_attach)
		dma_buf_vunmap(obj->import_attach->dmabuf, shmem->vaddr);
	else
		vunmap(shmem->vaddr);

	shmem->vaddr = NULL;
	drm_gem_shmem_put_pages(shmem);
}

/*
 * drm_gem_shmem_vunmap - Unmap a virtual mapping fo a shmem GEM object
 * @shmem: shmem GEM object
 *
 * This function cleans up a kernel virtual address mapping acquired by
 * drm_gem_shmem_vmap(). The mapping is only removed when the use count drops to
 * zero.
 *
 * This function can be used to implement &drm_gem_object_funcs.vmap. But it can
 * also be called by drivers directly, in which case it will hide the
 * differences between dma-buf imported and natively allocated objects.
 */
void drm_gem_shmem_vunmap(struct drm_gem_object *obj, void *vaddr)
{
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);

	mutex_lock(&shmem->vmap_lock);
	drm_gem_shmem_vunmap_locked(shmem);
	mutex_unlock(&shmem->vmap_lock);
}
EXPORT_SYMBOL(drm_gem_shmem_vunmap);

struct drm_gem_shmem_object *
drm_gem_shmem_create_with_handle(struct drm_file *file_priv,
				 struct drm_device *dev, size_t size,
				 uint32_t *handle)
{
	struct drm_gem_shmem_object *shmem;
	int ret;

	shmem = drm_gem_shmem_create(dev, size);
	if (IS_ERR(shmem))
		return shmem;

	/*
	 * Allocate an id of idr table where the obj is registered
	 * and handle has the id what user can see.
	 */
	ret = drm_gem_handle_create(file_priv, &shmem->base, handle);
	/* drop reference from allocate - handle holds it now. */
	drm_gem_object_put(&shmem->base);
	if (ret)
		return ERR_PTR(ret);

	return shmem;
}
EXPORT_SYMBOL(drm_gem_shmem_create_with_handle);

/* Update madvise status, returns true if not purged, else
 * false or -errno.
 */
int drm_gem_shmem_madvise(struct drm_gem_object *obj, int madv)
{
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);

	mutex_lock(&shmem->pages_lock);

	if (shmem->madv >= 0)
		shmem->madv = madv;

	madv = shmem->madv;

	mutex_unlock(&shmem->pages_lock);

	return (madv >= 0);
}
EXPORT_SYMBOL(drm_gem_shmem_madvise);

void drm_gem_shmem_purge_locked(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);

	WARN_ON(!drm_gem_shmem_is_purgeable(shmem));

	dma_unmap_sgtable(obj->dev->dev, shmem->sgt, DMA_BIDIRECTIONAL, 0);
	sg_free_table(shmem->sgt);
	kfree(shmem->sgt);
	shmem->sgt = NULL;

	drm_gem_shmem_put_pages_locked(shmem);

	shmem->madv = -1;

	drm_vma_node_unmap(&obj->vma_node, dev->anon_inode->i_mapping);
	drm_gem_free_mmap_offset(obj);

	/* Our goal here is to return as much of the memory as
	 * is possible back to the system as we are called from OOM.
	 * To do this we must instruct the shmfs to drop all of its
	 * backing pages, *now*.
	 */
	shmem_truncate_range(file_inode(obj->filp), 0, (loff_t)-1);

	invalidate_mapping_pages(file_inode(obj->filp)->i_mapping,
			0, (loff_t)-1);
}
EXPORT_SYMBOL(drm_gem_shmem_purge_locked);

bool drm_gem_shmem_purge(struct drm_gem_object *obj)
{
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);

	if (!mutex_trylock(&shmem->pages_lock))
		return false;
	drm_gem_shmem_purge_locked(obj);
	mutex_unlock(&shmem->pages_lock);

	return true;
}
EXPORT_SYMBOL(drm_gem_shmem_purge);

/**
 * drm_gem_shmem_create_object_cached - Create a shmem buffer object with
 *                                      cached mappings
 * @dev: DRM device
 * @size: Size of the object to allocate
 *
 * By default, shmem buffer objects use writecombine mappings. This
 * function implements struct drm_driver.gem_create_object for shmem
 * buffer objects with cached mappings.
 *
 * Returns:
 * A struct drm_gem_shmem_object * on success or NULL negative on failure.
 */
struct drm_gem_object *
drm_gem_shmem_create_object_cached(struct drm_device *dev, size_t size)
{
	struct drm_gem_shmem_object *shmem;

	shmem = kzalloc(sizeof(*shmem), GFP_KERNEL);
	if (!shmem)
		return NULL;
	shmem->map_cached = true;

	return &shmem->base;
}
EXPORT_SYMBOL(drm_gem_shmem_create_object_cached);

/**
 * drm_gem_shmem_dumb_create - Create a dumb shmem buffer object
 * @file: DRM file structure to create the dumb buffer for
 * @dev: DRM device
 * @args: IOCTL data
 *
 * This function computes the pitch of the dumb buffer and rounds it up to an
 * integer number of bytes per pixel. Drivers for hardware that doesn't have
 * any additional restrictions on the pitch can directly use this function as
 * their &drm_driver.dumb_create callback.
 *
 * For hardware with additional restrictions, drivers can adjust the fields
 * set up by userspace before calling into this function.
 *
 * Returns:
 * 0 on success or a negative error code on failure.
 */
int drm_gem_shmem_dumb_create(struct drm_file *file, struct drm_device *dev,
			      struct drm_mode_create_dumb *args)
{
	u32 min_pitch = DIV_ROUND_UP(args->width * args->bpp, 8);
	struct drm_gem_shmem_object *shmem;

	if (!args->pitch || !args->size) {
		args->pitch = min_pitch;
		args->size = args->pitch * args->height;
	} else {
		/* ensure sane minimum values */
		if (args->pitch < min_pitch)
			args->pitch = min_pitch;
		if (args->size < args->pitch * args->height)
			args->size = args->pitch * args->height;
	}

	shmem = drm_gem_shmem_create_with_handle(file, dev, args->size, &args->handle);

	return PTR_ERR_OR_ZERO(shmem);
}
EXPORT_SYMBOL_GPL(drm_gem_shmem_dumb_create);

static vm_fault_t drm_gem_shmem_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct drm_gem_object *obj = vma->vm_private_data;
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);
	loff_t num_pages = obj->size >> PAGE_SHIFT;
	struct page *page;

	if (vmf->pgoff >= num_pages || WARN_ON_ONCE(!shmem->pages))
		return VM_FAULT_SIGBUS;

	page = shmem->pages[vmf->pgoff];

	return vmf_insert_page(vma, vmf->address, page);
}

static void drm_gem_shmem_vm_open(struct vm_area_struct *vma)
{
	struct drm_gem_object *obj = vma->vm_private_data;
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);
	int ret;

	WARN_ON(shmem->base.import_attach);

	ret = drm_gem_shmem_get_pages(shmem);
	WARN_ON_ONCE(ret != 0);

	drm_gem_vm_open(vma);
}

static void drm_gem_shmem_vm_close(struct vm_area_struct *vma)
{
	struct drm_gem_object *obj = vma->vm_private_data;
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);

	drm_gem_shmem_put_pages(shmem);
	drm_gem_vm_close(vma);
}

static const struct vm_operations_struct drm_gem_shmem_vm_ops = {
	.fault = drm_gem_shmem_fault,
	.open = drm_gem_shmem_vm_open,
	.close = drm_gem_shmem_vm_close,
};

/**
 * drm_gem_shmem_mmap - Memory-map a shmem GEM object
 * @obj: gem object
 * @vma: VMA for the area to be mapped
 *
 * This function implements an augmented version of the GEM DRM file mmap
 * operation for shmem objects. Drivers which employ the shmem helpers should
 * use this function as their &drm_gem_object_funcs.mmap handler.
 *
 * Returns:
 * 0 on success or a negative error code on failure.
 */
int drm_gem_shmem_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	struct drm_gem_shmem_object *shmem;
	int ret;

	/* Remove the fake offset */
	vma->vm_pgoff -= drm_vma_node_start(&obj->vma_node);

	if (obj->import_attach) {
		/* Drop the reference drm_gem_mmap_obj() acquired.*/
		drm_gem_object_put(obj);
		vma->vm_private_data = NULL;

		return dma_buf_mmap(obj->dma_buf, vma, 0);
	}

	shmem = to_drm_gem_shmem_obj(obj);

	ret = drm_gem_shmem_get_pages(shmem);
	if (ret) {
		drm_gem_vm_close(vma);
		return ret;
	}

	vma->vm_flags |= VM_MIXEDMAP | VM_DONTEXPAND;
	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
	if (!shmem->map_cached)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	vma->vm_ops = &drm_gem_shmem_vm_ops;

	return 0;
}
EXPORT_SYMBOL_GPL(drm_gem_shmem_mmap);

/**
 * drm_gem_shmem_print_info() - Print &drm_gem_shmem_object info for debugfs
 * @p: DRM printer
 * @indent: Tab indentation level
 * @obj: GEM object
 *
 * This implements the &drm_gem_object_funcs.info callback.
 */
void drm_gem_shmem_print_info(struct drm_printer *p, unsigned int indent,
			      const struct drm_gem_object *obj)
{
	const struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);

	drm_printf_indent(p, indent, "pages_use_count=%u\n", shmem->pages_use_count);
	drm_printf_indent(p, indent, "vmap_use_count=%u\n", shmem->vmap_use_count);
	drm_printf_indent(p, indent, "vaddr=%p\n", shmem->vaddr);
}
EXPORT_SYMBOL(drm_gem_shmem_print_info);

/**
 * drm_gem_shmem_get_sg_table - Provide a scatter/gather table of pinned
 *                              pages for a shmem GEM object
 * @obj: GEM object
 *
 * This function exports a scatter/gather table suitable for PRIME usage by
 * calling the standard DMA mapping API. Drivers should not call this function
 * directly, instead it should only be used as an implementation for
 * &drm_gem_object_funcs.get_sg_table.
 *
 * Drivers who need to acquire an scatter/gather table for objects need to call
 * drm_gem_shmem_get_pages_sgt() instead.
 *
 * Returns:
 * A pointer to the scatter/gather table of pinned pages or NULL on failure.
 */
struct sg_table *drm_gem_shmem_get_sg_table(struct drm_gem_object *obj)
{
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);

	WARN_ON(shmem->base.import_attach);

	return drm_prime_pages_to_sg(obj->dev, shmem->pages, obj->size >> PAGE_SHIFT);
}
EXPORT_SYMBOL_GPL(drm_gem_shmem_get_sg_table);

/**
 * drm_gem_shmem_get_pages_sgt - Pin pages, dma map them, and return a
 *				 scatter/gather table for a shmem GEM object.
 * @obj: GEM object
 *
 * This function returns a scatter/gather table suitable for driver usage. If
 * the sg table doesn't exist, the pages are pinned, dma-mapped, and a sg
 * table created.
 *
 * This is the main function for drivers to get at backing storage, and it hides
 * and difference between dma-buf imported and natively allocated objects.
 * drm_gem_shmem_get_sg_table() should not be directly called by drivers.
 *
 * Returns:
 * A pointer to the scatter/gather table of pinned pages or errno on failure.
 */
struct sg_table *drm_gem_shmem_get_pages_sgt(struct drm_gem_object *obj)
{
	int ret;
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);
	struct sg_table *sgt;

	if (shmem->sgt)
		return shmem->sgt;

	WARN_ON(obj->import_attach);

	ret = drm_gem_shmem_get_pages(shmem);
	if (ret)
		return ERR_PTR(ret);

	sgt = drm_gem_shmem_get_sg_table(&shmem->base);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto err_put_pages;
	}
	/* Map the pages for use by the h/w. */
	ret = dma_map_sgtable(obj->dev->dev, sgt, DMA_BIDIRECTIONAL, 0);
	if (ret)
		goto err_free_sgt;

	shmem->sgt = sgt;

	return sgt;

err_free_sgt:
	sg_free_table(sgt);
	kfree(sgt);
err_put_pages:
	drm_gem_shmem_put_pages(shmem);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(drm_gem_shmem_get_pages_sgt);

/**
 * drm_gem_shmem_prime_import_sg_table - Produce a shmem GEM object from
 *                 another driver's scatter/gather table of pinned pages
 * @dev: Device to import into
 * @attach: DMA-BUF attachment
 * @sgt: Scatter/gather table of pinned pages
 *
 * This function imports a scatter/gather table exported via DMA-BUF by
 * another driver. Drivers that use the shmem helpers should set this as their
 * &drm_driver.gem_prime_import_sg_table callback.
 *
 * Returns:
 * A pointer to a newly created GEM object or an ERR_PTR-encoded negative
 * error code on failure.
 */
struct drm_gem_object *
drm_gem_shmem_prime_import_sg_table(struct drm_device *dev,
				    struct dma_buf_attachment *attach,
				    struct sg_table *sgt)
{
	size_t size = PAGE_ALIGN(attach->dmabuf->size);
	struct drm_gem_shmem_object *shmem;

	shmem = __drm_gem_shmem_create(dev, size, true);
	if (IS_ERR(shmem))
		return ERR_CAST(shmem);

	shmem->sgt = sgt;

	DRM_DEBUG_PRIME("size = %zu\n", size);

	return &shmem->base;
}
EXPORT_SYMBOL_GPL(drm_gem_shmem_prime_import_sg_table);
