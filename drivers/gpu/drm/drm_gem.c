/*
 * Copyright © 2008 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

#include <linux/dma-buf.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/iosys-map.h>
#include <linux/mem_encrypt.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/shmem_fs.h>
#include <linux/slab.h>
#include <linux/string_helpers.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include <drm/drm.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_managed.h>
#include <drm/drm_print.h>
#include <drm/drm_vma_manager.h>

#include "drm_internal.h"

/** @file drm_gem.c
 *
 * This file provides some of the base ioctls and library routines for
 * the graphics memory manager implemented by each device driver.
 *
 * Because various devices have different requirements in terms of
 * synchronization and migration strategies, implementing that is left up to
 * the driver, and all that the general API provides should be generic --
 * allocating objects, reading/writing data with the cpu, freeing objects.
 * Even there, platform-dependent optimizations for reading/writing data with
 * the CPU mean we'll likely hook those out to driver-specific calls.  However,
 * the DRI2 implementation wants to have at least allocate/mmap be generic.
 *
 * The goal was to have swap-backed object allocation managed through
 * struct file.  However, file descriptors as handles to a struct file have
 * two major failings:
 * - Process limits prevent more than 1024 or so being used at a time by
 *   default.
 * - Inability to allocate high fds will aggravate the X Server's select()
 *   handling, and likely that of many GL client applications as well.
 *
 * This led to a plan of using our own integer IDs (called handles, following
 * DRM terminology) to mimic fds, and implement the fd syscalls we need as
 * ioctls.  The objects themselves will still include the struct file so
 * that we can transition to fds if the required kernel infrastructure shows
 * up at a later date, and as our interface with shmfs for memory allocation.
 */

static void
drm_gem_init_release(struct drm_device *dev, void *ptr)
{
	drm_vma_offset_manager_destroy(dev->vma_offset_manager);
}

/**
 * drm_gem_init - Initialize the GEM device fields
 * @dev: drm_devic structure to initialize
 */
int
drm_gem_init(struct drm_device *dev)
{
	struct drm_vma_offset_manager *vma_offset_manager;

	mutex_init(&dev->object_name_lock);
	idr_init_base(&dev->object_name_idr, 1);

	vma_offset_manager = drmm_kzalloc(dev, sizeof(*vma_offset_manager),
					  GFP_KERNEL);
	if (!vma_offset_manager) {
		DRM_ERROR("out of memory\n");
		return -ENOMEM;
	}

	dev->vma_offset_manager = vma_offset_manager;
	drm_vma_offset_manager_init(vma_offset_manager,
				    DRM_FILE_PAGE_OFFSET_START,
				    DRM_FILE_PAGE_OFFSET_SIZE);

	return drmm_add_action(dev, drm_gem_init_release, NULL);
}

/**
 * drm_gem_object_init - initialize an allocated shmem-backed GEM object
 * @dev: drm_device the object should be initialized for
 * @obj: drm_gem_object to initialize
 * @size: object size
 *
 * Initialize an already allocated GEM object of the specified size with
 * shmfs backing store.
 */
int drm_gem_object_init(struct drm_device *dev,
			struct drm_gem_object *obj, size_t size)
{
	struct file *filp;

	drm_gem_private_object_init(dev, obj, size);

	filp = shmem_file_setup("drm mm object", size, VM_NORESERVE);
	if (IS_ERR(filp))
		return PTR_ERR(filp);

	obj->filp = filp;

	return 0;
}
EXPORT_SYMBOL(drm_gem_object_init);

/**
 * drm_gem_private_object_init - initialize an allocated private GEM object
 * @dev: drm_device the object should be initialized for
 * @obj: drm_gem_object to initialize
 * @size: object size
 *
 * Initialize an already allocated GEM object of the specified size with
 * no GEM provided backing store. Instead the caller is responsible for
 * backing the object and handling it.
 */
void drm_gem_private_object_init(struct drm_device *dev,
				 struct drm_gem_object *obj, size_t size)
{
	BUG_ON((size & (PAGE_SIZE - 1)) != 0);

	obj->dev = dev;
	obj->filp = NULL;

	kref_init(&obj->refcount);
	obj->handle_count = 0;
	obj->size = size;
	dma_resv_init(&obj->_resv);
	if (!obj->resv)
		obj->resv = &obj->_resv;

	if (drm_core_check_feature(dev, DRIVER_GEM_GPUVA))
		drm_gem_gpuva_init(obj);

	drm_vma_node_reset(&obj->vma_node);
	INIT_LIST_HEAD(&obj->lru_node);
}
EXPORT_SYMBOL(drm_gem_private_object_init);

/**
 * drm_gem_private_object_fini - Finalize a failed drm_gem_object
 * @obj: drm_gem_object
 *
 * Uninitialize an already allocated GEM object when it initialized failed
 */
void drm_gem_private_object_fini(struct drm_gem_object *obj)
{
	WARN_ON(obj->dma_buf);

	dma_resv_fini(&obj->_resv);
}
EXPORT_SYMBOL(drm_gem_private_object_fini);

/**
 * drm_gem_object_handle_free - release resources bound to userspace handles
 * @obj: GEM object to clean up.
 *
 * Called after the last handle to the object has been closed
 *
 * Removes any name for the object. Note that this must be
 * called before drm_gem_object_free or we'll be touching
 * freed memory
 */
static void drm_gem_object_handle_free(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;

	/* Remove any name for this object */
	if (obj->name) {
		idr_remove(&dev->object_name_idr, obj->name);
		obj->name = 0;
	}
}

static void drm_gem_object_exported_dma_buf_free(struct drm_gem_object *obj)
{
	/* Unbreak the reference cycle if we have an exported dma_buf. */
	if (obj->dma_buf) {
		dma_buf_put(obj->dma_buf);
		obj->dma_buf = NULL;
	}
}

static void
drm_gem_object_handle_put_unlocked(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	bool final = false;

	if (WARN_ON(READ_ONCE(obj->handle_count) == 0))
		return;

	/*
	* Must bump handle count first as this may be the last
	* ref, in which case the object would disappear before we
	* checked for a name
	*/

	mutex_lock(&dev->object_name_lock);
	if (--obj->handle_count == 0) {
		drm_gem_object_handle_free(obj);
		drm_gem_object_exported_dma_buf_free(obj);
		final = true;
	}
	mutex_unlock(&dev->object_name_lock);

	if (final)
		drm_gem_object_put(obj);
}

/*
 * Called at device or object close to release the file's
 * handle references on objects.
 */
static int
drm_gem_object_release_handle(int id, void *ptr, void *data)
{
	struct drm_file *file_priv = data;
	struct drm_gem_object *obj = ptr;

	if (obj->funcs->close)
		obj->funcs->close(obj, file_priv);

	drm_prime_remove_buf_handle(&file_priv->prime, id);
	drm_vma_node_revoke(&obj->vma_node, file_priv);

	drm_gem_object_handle_put_unlocked(obj);

	return 0;
}

/**
 * drm_gem_handle_delete - deletes the given file-private handle
 * @filp: drm file-private structure to use for the handle look up
 * @handle: userspace handle to delete
 *
 * Removes the GEM handle from the @filp lookup table which has been added with
 * drm_gem_handle_create(). If this is the last handle also cleans up linked
 * resources like GEM names.
 */
int
drm_gem_handle_delete(struct drm_file *filp, u32 handle)
{
	struct drm_gem_object *obj;

	spin_lock(&filp->table_lock);

	/* Check if we currently have a reference on the object */
	obj = idr_replace(&filp->object_idr, NULL, handle);
	spin_unlock(&filp->table_lock);
	if (IS_ERR_OR_NULL(obj))
		return -EINVAL;

	/* Release driver's reference and decrement refcount. */
	drm_gem_object_release_handle(handle, obj, filp);

	/* And finally make the handle available for future allocations. */
	spin_lock(&filp->table_lock);
	idr_remove(&filp->object_idr, handle);
	spin_unlock(&filp->table_lock);

	return 0;
}
EXPORT_SYMBOL(drm_gem_handle_delete);

/**
 * drm_gem_dumb_map_offset - return the fake mmap offset for a gem object
 * @file: drm file-private structure containing the gem object
 * @dev: corresponding drm_device
 * @handle: gem object handle
 * @offset: return location for the fake mmap offset
 *
 * This implements the &drm_driver.dumb_map_offset kms driver callback for
 * drivers which use gem to manage their backing storage.
 *
 * Returns:
 * 0 on success or a negative error code on failure.
 */
int drm_gem_dumb_map_offset(struct drm_file *file, struct drm_device *dev,
			    u32 handle, u64 *offset)
{
	struct drm_gem_object *obj;
	int ret;

	obj = drm_gem_object_lookup(file, handle);
	if (!obj)
		return -ENOENT;

	/* Don't allow imported objects to be mapped */
	if (obj->import_attach) {
		ret = -EINVAL;
		goto out;
	}

	ret = drm_gem_create_mmap_offset(obj);
	if (ret)
		goto out;

	*offset = drm_vma_node_offset_addr(&obj->vma_node);
out:
	drm_gem_object_put(obj);

	return ret;
}
EXPORT_SYMBOL_GPL(drm_gem_dumb_map_offset);

/**
 * drm_gem_handle_create_tail - internal functions to create a handle
 * @file_priv: drm file-private structure to register the handle for
 * @obj: object to register
 * @handlep: pointer to return the created handle to the caller
 *
 * This expects the &drm_device.object_name_lock to be held already and will
 * drop it before returning. Used to avoid races in establishing new handles
 * when importing an object from either an flink name or a dma-buf.
 *
 * Handles must be release again through drm_gem_handle_delete(). This is done
 * when userspace closes @file_priv for all attached handles, or through the
 * GEM_CLOSE ioctl for individual handles.
 */
int
drm_gem_handle_create_tail(struct drm_file *file_priv,
			   struct drm_gem_object *obj,
			   u32 *handlep)
{
	struct drm_device *dev = obj->dev;
	u32 handle;
	int ret;

	WARN_ON(!mutex_is_locked(&dev->object_name_lock));
	if (obj->handle_count++ == 0)
		drm_gem_object_get(obj);

	/*
	 * Get the user-visible handle using idr.  Preload and perform
	 * allocation under our spinlock.
	 */
	idr_preload(GFP_KERNEL);
	spin_lock(&file_priv->table_lock);

	ret = idr_alloc(&file_priv->object_idr, obj, 1, 0, GFP_NOWAIT);

	spin_unlock(&file_priv->table_lock);
	idr_preload_end();

	mutex_unlock(&dev->object_name_lock);
	if (ret < 0)
		goto err_unref;

	handle = ret;

	ret = drm_vma_node_allow(&obj->vma_node, file_priv);
	if (ret)
		goto err_remove;

	if (obj->funcs->open) {
		ret = obj->funcs->open(obj, file_priv);
		if (ret)
			goto err_revoke;
	}

	*handlep = handle;
	return 0;

err_revoke:
	drm_vma_node_revoke(&obj->vma_node, file_priv);
err_remove:
	spin_lock(&file_priv->table_lock);
	idr_remove(&file_priv->object_idr, handle);
	spin_unlock(&file_priv->table_lock);
err_unref:
	drm_gem_object_handle_put_unlocked(obj);
	return ret;
}

/**
 * drm_gem_handle_create - create a gem handle for an object
 * @file_priv: drm file-private structure to register the handle for
 * @obj: object to register
 * @handlep: pointer to return the created handle to the caller
 *
 * Create a handle for this object. This adds a handle reference to the object,
 * which includes a regular reference count. Callers will likely want to
 * dereference the object afterwards.
 *
 * Since this publishes @obj to userspace it must be fully set up by this point,
 * drivers must call this last in their buffer object creation callbacks.
 */
int drm_gem_handle_create(struct drm_file *file_priv,
			  struct drm_gem_object *obj,
			  u32 *handlep)
{
	mutex_lock(&obj->dev->object_name_lock);

	return drm_gem_handle_create_tail(file_priv, obj, handlep);
}
EXPORT_SYMBOL(drm_gem_handle_create);


/**
 * drm_gem_free_mmap_offset - release a fake mmap offset for an object
 * @obj: obj in question
 *
 * This routine frees fake offsets allocated by drm_gem_create_mmap_offset().
 *
 * Note that drm_gem_object_release() already calls this function, so drivers
 * don't have to take care of releasing the mmap offset themselves when freeing
 * the GEM object.
 */
void
drm_gem_free_mmap_offset(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;

	drm_vma_offset_remove(dev->vma_offset_manager, &obj->vma_node);
}
EXPORT_SYMBOL(drm_gem_free_mmap_offset);

/**
 * drm_gem_create_mmap_offset_size - create a fake mmap offset for an object
 * @obj: obj in question
 * @size: the virtual size
 *
 * GEM memory mapping works by handing back to userspace a fake mmap offset
 * it can use in a subsequent mmap(2) call.  The DRM core code then looks
 * up the object based on the offset and sets up the various memory mapping
 * structures.
 *
 * This routine allocates and attaches a fake offset for @obj, in cases where
 * the virtual size differs from the physical size (ie. &drm_gem_object.size).
 * Otherwise just use drm_gem_create_mmap_offset().
 *
 * This function is idempotent and handles an already allocated mmap offset
 * transparently. Drivers do not need to check for this case.
 */
int
drm_gem_create_mmap_offset_size(struct drm_gem_object *obj, size_t size)
{
	struct drm_device *dev = obj->dev;

	return drm_vma_offset_add(dev->vma_offset_manager, &obj->vma_node,
				  size / PAGE_SIZE);
}
EXPORT_SYMBOL(drm_gem_create_mmap_offset_size);

/**
 * drm_gem_create_mmap_offset - create a fake mmap offset for an object
 * @obj: obj in question
 *
 * GEM memory mapping works by handing back to userspace a fake mmap offset
 * it can use in a subsequent mmap(2) call.  The DRM core code then looks
 * up the object based on the offset and sets up the various memory mapping
 * structures.
 *
 * This routine allocates and attaches a fake offset for @obj.
 *
 * Drivers can call drm_gem_free_mmap_offset() before freeing @obj to release
 * the fake offset again.
 */
int drm_gem_create_mmap_offset(struct drm_gem_object *obj)
{
	return drm_gem_create_mmap_offset_size(obj, obj->size);
}
EXPORT_SYMBOL(drm_gem_create_mmap_offset);

/*
 * Move folios to appropriate lru and release the folios, decrementing the
 * ref count of those folios.
 */
static void drm_gem_check_release_batch(struct folio_batch *fbatch)
{
	check_move_unevictable_folios(fbatch);
	__folio_batch_release(fbatch);
	cond_resched();
}

/**
 * drm_gem_get_pages - helper to allocate backing pages for a GEM object
 * from shmem
 * @obj: obj in question
 *
 * This reads the page-array of the shmem-backing storage of the given gem
 * object. An array of pages is returned. If a page is not allocated or
 * swapped-out, this will allocate/swap-in the required pages. Note that the
 * whole object is covered by the page-array and pinned in memory.
 *
 * Use drm_gem_put_pages() to release the array and unpin all pages.
 *
 * This uses the GFP-mask set on the shmem-mapping (see mapping_set_gfp_mask()).
 * If you require other GFP-masks, you have to do those allocations yourself.
 *
 * Note that you are not allowed to change gfp-zones during runtime. That is,
 * shmem_read_mapping_page_gfp() must be called with the same gfp_zone(gfp) as
 * set during initialization. If you have special zone constraints, set them
 * after drm_gem_object_init() via mapping_set_gfp_mask(). shmem-core takes care
 * to keep pages in the required zone during swap-in.
 *
 * This function is only valid on objects initialized with
 * drm_gem_object_init(), but not for those initialized with
 * drm_gem_private_object_init() only.
 */
struct page **drm_gem_get_pages(struct drm_gem_object *obj)
{
	struct address_space *mapping;
	struct page **pages;
	struct folio *folio;
	struct folio_batch fbatch;
	long i, j, npages;

	if (WARN_ON(!obj->filp))
		return ERR_PTR(-EINVAL);

	/* This is the shared memory object that backs the GEM resource */
	mapping = obj->filp->f_mapping;

	/* We already BUG_ON() for non-page-aligned sizes in
	 * drm_gem_object_init(), so we should never hit this unless
	 * driver author is doing something really wrong:
	 */
	WARN_ON((obj->size & (PAGE_SIZE - 1)) != 0);

	npages = obj->size >> PAGE_SHIFT;

	pages = kvmalloc_array(npages, sizeof(struct page *), GFP_KERNEL);
	if (pages == NULL)
		return ERR_PTR(-ENOMEM);

	mapping_set_unevictable(mapping);

	i = 0;
	while (i < npages) {
		long nr;
		folio = shmem_read_folio_gfp(mapping, i,
				mapping_gfp_mask(mapping));
		if (IS_ERR(folio))
			goto fail;
		nr = min(npages - i, folio_nr_pages(folio));
		for (j = 0; j < nr; j++, i++)
			pages[i] = folio_file_page(folio, i);

		/* Make sure shmem keeps __GFP_DMA32 allocated pages in the
		 * correct region during swapin. Note that this requires
		 * __GFP_DMA32 to be set in mapping_gfp_mask(inode->i_mapping)
		 * so shmem can relocate pages during swapin if required.
		 */
		BUG_ON(mapping_gfp_constraint(mapping, __GFP_DMA32) &&
				(folio_pfn(folio) >= 0x00100000UL));
	}

	return pages;

fail:
	mapping_clear_unevictable(mapping);
	folio_batch_init(&fbatch);
	j = 0;
	while (j < i) {
		struct folio *f = page_folio(pages[j]);
		if (!folio_batch_add(&fbatch, f))
			drm_gem_check_release_batch(&fbatch);
		j += folio_nr_pages(f);
	}
	if (fbatch.nr)
		drm_gem_check_release_batch(&fbatch);

	kvfree(pages);
	return ERR_CAST(folio);
}
EXPORT_SYMBOL(drm_gem_get_pages);

/**
 * drm_gem_put_pages - helper to free backing pages for a GEM object
 * @obj: obj in question
 * @pages: pages to free
 * @dirty: if true, pages will be marked as dirty
 * @accessed: if true, the pages will be marked as accessed
 */
void drm_gem_put_pages(struct drm_gem_object *obj, struct page **pages,
		bool dirty, bool accessed)
{
	int i, npages;
	struct address_space *mapping;
	struct folio_batch fbatch;

	mapping = file_inode(obj->filp)->i_mapping;
	mapping_clear_unevictable(mapping);

	/* We already BUG_ON() for non-page-aligned sizes in
	 * drm_gem_object_init(), so we should never hit this unless
	 * driver author is doing something really wrong:
	 */
	WARN_ON((obj->size & (PAGE_SIZE - 1)) != 0);

	npages = obj->size >> PAGE_SHIFT;

	folio_batch_init(&fbatch);
	for (i = 0; i < npages; i++) {
		struct folio *folio;

		if (!pages[i])
			continue;
		folio = page_folio(pages[i]);

		if (dirty)
			folio_mark_dirty(folio);

		if (accessed)
			folio_mark_accessed(folio);

		/* Undo the reference we took when populating the table */
		if (!folio_batch_add(&fbatch, folio))
			drm_gem_check_release_batch(&fbatch);
		i += folio_nr_pages(folio) - 1;
	}
	if (folio_batch_count(&fbatch))
		drm_gem_check_release_batch(&fbatch);

	kvfree(pages);
}
EXPORT_SYMBOL(drm_gem_put_pages);

static int objects_lookup(struct drm_file *filp, u32 *handle, int count,
			  struct drm_gem_object **objs)
{
	int i, ret = 0;
	struct drm_gem_object *obj;

	spin_lock(&filp->table_lock);

	for (i = 0; i < count; i++) {
		/* Check if we currently have a reference on the object */
		obj = idr_find(&filp->object_idr, handle[i]);
		if (!obj) {
			ret = -ENOENT;
			break;
		}
		drm_gem_object_get(obj);
		objs[i] = obj;
	}
	spin_unlock(&filp->table_lock);

	return ret;
}

/**
 * drm_gem_objects_lookup - look up GEM objects from an array of handles
 * @filp: DRM file private date
 * @bo_handles: user pointer to array of userspace handle
 * @count: size of handle array
 * @objs_out: returned pointer to array of drm_gem_object pointers
 *
 * Takes an array of userspace handles and returns a newly allocated array of
 * GEM objects.
 *
 * For a single handle lookup, use drm_gem_object_lookup().
 *
 * Returns:
 *
 * @objs filled in with GEM object pointers. Returned GEM objects need to be
 * released with drm_gem_object_put(). -ENOENT is returned on a lookup
 * failure. 0 is returned on success.
 *
 */
int drm_gem_objects_lookup(struct drm_file *filp, void __user *bo_handles,
			   int count, struct drm_gem_object ***objs_out)
{
	int ret;
	u32 *handles;
	struct drm_gem_object **objs;

	if (!count)
		return 0;

	objs = kvmalloc_array(count, sizeof(struct drm_gem_object *),
			     GFP_KERNEL | __GFP_ZERO);
	if (!objs)
		return -ENOMEM;

	*objs_out = objs;

	handles = kvmalloc_array(count, sizeof(u32), GFP_KERNEL);
	if (!handles) {
		ret = -ENOMEM;
		goto out;
	}

	if (copy_from_user(handles, bo_handles, count * sizeof(u32))) {
		ret = -EFAULT;
		DRM_DEBUG("Failed to copy in GEM handles\n");
		goto out;
	}

	ret = objects_lookup(filp, handles, count, objs);
out:
	kvfree(handles);
	return ret;

}
EXPORT_SYMBOL(drm_gem_objects_lookup);

/**
 * drm_gem_object_lookup - look up a GEM object from its handle
 * @filp: DRM file private date
 * @handle: userspace handle
 *
 * Returns:
 *
 * A reference to the object named by the handle if such exists on @filp, NULL
 * otherwise.
 *
 * If looking up an array of handles, use drm_gem_objects_lookup().
 */
struct drm_gem_object *
drm_gem_object_lookup(struct drm_file *filp, u32 handle)
{
	struct drm_gem_object *obj = NULL;

	objects_lookup(filp, &handle, 1, &obj);
	return obj;
}
EXPORT_SYMBOL(drm_gem_object_lookup);

/**
 * drm_gem_dma_resv_wait - Wait on GEM object's reservation's objects
 * shared and/or exclusive fences.
 * @filep: DRM file private date
 * @handle: userspace handle
 * @wait_all: if true, wait on all fences, else wait on just exclusive fence
 * @timeout: timeout value in jiffies or zero to return immediately
 *
 * Returns:
 *
 * Returns -ERESTARTSYS if interrupted, 0 if the wait timed out, or
 * greater than 0 on success.
 */
long drm_gem_dma_resv_wait(struct drm_file *filep, u32 handle,
				    bool wait_all, unsigned long timeout)
{
	long ret;
	struct drm_gem_object *obj;

	obj = drm_gem_object_lookup(filep, handle);
	if (!obj) {
		DRM_DEBUG("Failed to look up GEM BO %d\n", handle);
		return -EINVAL;
	}

	ret = dma_resv_wait_timeout(obj->resv, dma_resv_usage_rw(wait_all),
				    true, timeout);
	if (ret == 0)
		ret = -ETIME;
	else if (ret > 0)
		ret = 0;

	drm_gem_object_put(obj);

	return ret;
}
EXPORT_SYMBOL(drm_gem_dma_resv_wait);

/**
 * drm_gem_close_ioctl - implementation of the GEM_CLOSE ioctl
 * @dev: drm_device
 * @data: ioctl data
 * @file_priv: drm file-private structure
 *
 * Releases the handle to an mm object.
 */
int
drm_gem_close_ioctl(struct drm_device *dev, void *data,
		    struct drm_file *file_priv)
{
	struct drm_gem_close *args = data;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_GEM))
		return -EOPNOTSUPP;

	ret = drm_gem_handle_delete(file_priv, args->handle);

	return ret;
}

/**
 * drm_gem_flink_ioctl - implementation of the GEM_FLINK ioctl
 * @dev: drm_device
 * @data: ioctl data
 * @file_priv: drm file-private structure
 *
 * Create a global name for an object, returning the name.
 *
 * Note that the name does not hold a reference; when the object
 * is freed, the name goes away.
 */
int
drm_gem_flink_ioctl(struct drm_device *dev, void *data,
		    struct drm_file *file_priv)
{
	struct drm_gem_flink *args = data;
	struct drm_gem_object *obj;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_GEM))
		return -EOPNOTSUPP;

	obj = drm_gem_object_lookup(file_priv, args->handle);
	if (obj == NULL)
		return -ENOENT;

	mutex_lock(&dev->object_name_lock);
	/* prevent races with concurrent gem_close. */
	if (obj->handle_count == 0) {
		ret = -ENOENT;
		goto err;
	}

	if (!obj->name) {
		ret = idr_alloc(&dev->object_name_idr, obj, 1, 0, GFP_KERNEL);
		if (ret < 0)
			goto err;

		obj->name = ret;
	}

	args->name = (uint64_t) obj->name;
	ret = 0;

err:
	mutex_unlock(&dev->object_name_lock);
	drm_gem_object_put(obj);
	return ret;
}

/**
 * drm_gem_open_ioctl - implementation of the GEM_OPEN ioctl
 * @dev: drm_device
 * @data: ioctl data
 * @file_priv: drm file-private structure
 *
 * Open an object using the global name, returning a handle and the size.
 *
 * This handle (of course) holds a reference to the object, so the object
 * will not go away until the handle is deleted.
 */
int
drm_gem_open_ioctl(struct drm_device *dev, void *data,
		   struct drm_file *file_priv)
{
	struct drm_gem_open *args = data;
	struct drm_gem_object *obj;
	int ret;
	u32 handle;

	if (!drm_core_check_feature(dev, DRIVER_GEM))
		return -EOPNOTSUPP;

	mutex_lock(&dev->object_name_lock);
	obj = idr_find(&dev->object_name_idr, (int) args->name);
	if (obj) {
		drm_gem_object_get(obj);
	} else {
		mutex_unlock(&dev->object_name_lock);
		return -ENOENT;
	}

	/* drm_gem_handle_create_tail unlocks dev->object_name_lock. */
	ret = drm_gem_handle_create_tail(file_priv, obj, &handle);
	if (ret)
		goto err;

	args->handle = handle;
	args->size = obj->size;

err:
	drm_gem_object_put(obj);
	return ret;
}

/**
 * drm_gem_open - initializes GEM file-private structures at devnode open time
 * @dev: drm_device which is being opened by userspace
 * @file_private: drm file-private structure to set up
 *
 * Called at device open time, sets up the structure for handling refcounting
 * of mm objects.
 */
void
drm_gem_open(struct drm_device *dev, struct drm_file *file_private)
{
	idr_init_base(&file_private->object_idr, 1);
	spin_lock_init(&file_private->table_lock);
}

/**
 * drm_gem_release - release file-private GEM resources
 * @dev: drm_device which is being closed by userspace
 * @file_private: drm file-private structure to clean up
 *
 * Called at close time when the filp is going away.
 *
 * Releases any remaining references on objects by this filp.
 */
void
drm_gem_release(struct drm_device *dev, struct drm_file *file_private)
{
	idr_for_each(&file_private->object_idr,
		     &drm_gem_object_release_handle, file_private);
	idr_destroy(&file_private->object_idr);
}

/**
 * drm_gem_object_release - release GEM buffer object resources
 * @obj: GEM buffer object
 *
 * This releases any structures and resources used by @obj and is the inverse of
 * drm_gem_object_init().
 */
void
drm_gem_object_release(struct drm_gem_object *obj)
{
	if (obj->filp)
		fput(obj->filp);

	drm_gem_private_object_fini(obj);

	drm_gem_free_mmap_offset(obj);
	drm_gem_lru_remove(obj);
}
EXPORT_SYMBOL(drm_gem_object_release);

/**
 * drm_gem_object_free - free a GEM object
 * @kref: kref of the object to free
 *
 * Called after the last reference to the object has been lost.
 *
 * Frees the object
 */
void
drm_gem_object_free(struct kref *kref)
{
	struct drm_gem_object *obj =
		container_of(kref, struct drm_gem_object, refcount);

	if (WARN_ON(!obj->funcs->free))
		return;

	obj->funcs->free(obj);
}
EXPORT_SYMBOL(drm_gem_object_free);

/**
 * drm_gem_vm_open - vma->ops->open implementation for GEM
 * @vma: VM area structure
 *
 * This function implements the #vm_operations_struct open() callback for GEM
 * drivers. This must be used together with drm_gem_vm_close().
 */
void drm_gem_vm_open(struct vm_area_struct *vma)
{
	struct drm_gem_object *obj = vma->vm_private_data;

	drm_gem_object_get(obj);
}
EXPORT_SYMBOL(drm_gem_vm_open);

/**
 * drm_gem_vm_close - vma->ops->close implementation for GEM
 * @vma: VM area structure
 *
 * This function implements the #vm_operations_struct close() callback for GEM
 * drivers. This must be used together with drm_gem_vm_open().
 */
void drm_gem_vm_close(struct vm_area_struct *vma)
{
	struct drm_gem_object *obj = vma->vm_private_data;

	drm_gem_object_put(obj);
}
EXPORT_SYMBOL(drm_gem_vm_close);

/**
 * drm_gem_mmap_obj - memory map a GEM object
 * @obj: the GEM object to map
 * @obj_size: the object size to be mapped, in bytes
 * @vma: VMA for the area to be mapped
 *
 * Set up the VMA to prepare mapping of the GEM object using the GEM object's
 * vm_ops. Depending on their requirements, GEM objects can either
 * provide a fault handler in their vm_ops (in which case any accesses to
 * the object will be trapped, to perform migration, GTT binding, surface
 * register allocation, or performance monitoring), or mmap the buffer memory
 * synchronously after calling drm_gem_mmap_obj.
 *
 * This function is mainly intended to implement the DMABUF mmap operation, when
 * the GEM object is not looked up based on its fake offset. To implement the
 * DRM mmap operation, drivers should use the drm_gem_mmap() function.
 *
 * drm_gem_mmap_obj() assumes the user is granted access to the buffer while
 * drm_gem_mmap() prevents unprivileged users from mapping random objects. So
 * callers must verify access restrictions before calling this helper.
 *
 * Return 0 or success or -EINVAL if the object size is smaller than the VMA
 * size, or if no vm_ops are provided.
 */
int drm_gem_mmap_obj(struct drm_gem_object *obj, unsigned long obj_size,
		     struct vm_area_struct *vma)
{
	int ret;

	/* Check for valid size. */
	if (obj_size < vma->vm_end - vma->vm_start)
		return -EINVAL;

	/* Take a ref for this mapping of the object, so that the fault
	 * handler can dereference the mmap offset's pointer to the object.
	 * This reference is cleaned up by the corresponding vm_close
	 * (which should happen whether the vma was created by this call, or
	 * by a vm_open due to mremap or partial unmap or whatever).
	 */
	drm_gem_object_get(obj);

	vma->vm_private_data = obj;
	vma->vm_ops = obj->funcs->vm_ops;

	if (obj->funcs->mmap) {
		ret = obj->funcs->mmap(obj, vma);
		if (ret)
			goto err_drm_gem_object_put;
		WARN_ON(!(vma->vm_flags & VM_DONTEXPAND));
	} else {
		if (!vma->vm_ops) {
			ret = -EINVAL;
			goto err_drm_gem_object_put;
		}

		vm_flags_set(vma, VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP);
		vma->vm_page_prot = pgprot_writecombine(vm_get_page_prot(vma->vm_flags));
		vma->vm_page_prot = pgprot_decrypted(vma->vm_page_prot);
	}

	return 0;

err_drm_gem_object_put:
	drm_gem_object_put(obj);
	return ret;
}
EXPORT_SYMBOL(drm_gem_mmap_obj);

/**
 * drm_gem_mmap - memory map routine for GEM objects
 * @filp: DRM file pointer
 * @vma: VMA for the area to be mapped
 *
 * If a driver supports GEM object mapping, mmap calls on the DRM file
 * descriptor will end up here.
 *
 * Look up the GEM object based on the offset passed in (vma->vm_pgoff will
 * contain the fake offset we created when the GTT map ioctl was called on
 * the object) and map it with a call to drm_gem_mmap_obj().
 *
 * If the caller is not granted access to the buffer object, the mmap will fail
 * with EACCES. Please see the vma manager for more information.
 */
int drm_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *priv = filp->private_data;
	struct drm_device *dev = priv->minor->dev;
	struct drm_gem_object *obj = NULL;
	struct drm_vma_offset_node *node;
	int ret;

	if (drm_dev_is_unplugged(dev))
		return -ENODEV;

	drm_vma_offset_lock_lookup(dev->vma_offset_manager);
	node = drm_vma_offset_exact_lookup_locked(dev->vma_offset_manager,
						  vma->vm_pgoff,
						  vma_pages(vma));
	if (likely(node)) {
		obj = container_of(node, struct drm_gem_object, vma_node);
		/*
		 * When the object is being freed, after it hits 0-refcnt it
		 * proceeds to tear down the object. In the process it will
		 * attempt to remove the VMA offset and so acquire this
		 * mgr->vm_lock.  Therefore if we find an object with a 0-refcnt
		 * that matches our range, we know it is in the process of being
		 * destroyed and will be freed as soon as we release the lock -
		 * so we have to check for the 0-refcnted object and treat it as
		 * invalid.
		 */
		if (!kref_get_unless_zero(&obj->refcount))
			obj = NULL;
	}
	drm_vma_offset_unlock_lookup(dev->vma_offset_manager);

	if (!obj)
		return -EINVAL;

	if (!drm_vma_node_is_allowed(node, priv)) {
		drm_gem_object_put(obj);
		return -EACCES;
	}

	ret = drm_gem_mmap_obj(obj, drm_vma_node_size(node) << PAGE_SHIFT,
			       vma);

	drm_gem_object_put(obj);

	return ret;
}
EXPORT_SYMBOL(drm_gem_mmap);

void drm_gem_print_info(struct drm_printer *p, unsigned int indent,
			const struct drm_gem_object *obj)
{
	drm_printf_indent(p, indent, "name=%d\n", obj->name);
	drm_printf_indent(p, indent, "refcount=%u\n",
			  kref_read(&obj->refcount));
	drm_printf_indent(p, indent, "start=%08lx\n",
			  drm_vma_node_start(&obj->vma_node));
	drm_printf_indent(p, indent, "size=%zu\n", obj->size);
	drm_printf_indent(p, indent, "imported=%s\n",
			  str_yes_no(obj->import_attach));

	if (obj->funcs->print_info)
		obj->funcs->print_info(p, indent, obj);
}

int drm_gem_pin_locked(struct drm_gem_object *obj)
{
	if (obj->funcs->pin)
		return obj->funcs->pin(obj);

	return 0;
}

void drm_gem_unpin_locked(struct drm_gem_object *obj)
{
	if (obj->funcs->unpin)
		obj->funcs->unpin(obj);
}

int drm_gem_pin(struct drm_gem_object *obj)
{
	int ret;

	dma_resv_lock(obj->resv, NULL);
	ret = drm_gem_pin_locked(obj);
	dma_resv_unlock(obj->resv);

	return ret;
}

void drm_gem_unpin(struct drm_gem_object *obj)
{
	dma_resv_lock(obj->resv, NULL);
	drm_gem_unpin_locked(obj);
	dma_resv_unlock(obj->resv);
}

int drm_gem_vmap(struct drm_gem_object *obj, struct iosys_map *map)
{
	int ret;

	dma_resv_assert_held(obj->resv);

	if (!obj->funcs->vmap)
		return -EOPNOTSUPP;

	ret = obj->funcs->vmap(obj, map);
	if (ret)
		return ret;
	else if (iosys_map_is_null(map))
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL(drm_gem_vmap);

void drm_gem_vunmap(struct drm_gem_object *obj, struct iosys_map *map)
{
	dma_resv_assert_held(obj->resv);

	if (iosys_map_is_null(map))
		return;

	if (obj->funcs->vunmap)
		obj->funcs->vunmap(obj, map);

	/* Always set the mapping to NULL. Callers may rely on this. */
	iosys_map_clear(map);
}
EXPORT_SYMBOL(drm_gem_vunmap);

void drm_gem_lock(struct drm_gem_object *obj)
{
	dma_resv_lock(obj->resv, NULL);
}
EXPORT_SYMBOL(drm_gem_lock);

void drm_gem_unlock(struct drm_gem_object *obj)
{
	dma_resv_unlock(obj->resv);
}
EXPORT_SYMBOL(drm_gem_unlock);

int drm_gem_vmap_unlocked(struct drm_gem_object *obj, struct iosys_map *map)
{
	int ret;

	dma_resv_lock(obj->resv, NULL);
	ret = drm_gem_vmap(obj, map);
	dma_resv_unlock(obj->resv);

	return ret;
}
EXPORT_SYMBOL(drm_gem_vmap_unlocked);

void drm_gem_vunmap_unlocked(struct drm_gem_object *obj, struct iosys_map *map)
{
	dma_resv_lock(obj->resv, NULL);
	drm_gem_vunmap(obj, map);
	dma_resv_unlock(obj->resv);
}
EXPORT_SYMBOL(drm_gem_vunmap_unlocked);

/**
 * drm_gem_lock_reservations - Sets up the ww context and acquires
 * the lock on an array of GEM objects.
 *
 * Once you've locked your reservations, you'll want to set up space
 * for your shared fences (if applicable), submit your job, then
 * drm_gem_unlock_reservations().
 *
 * @objs: drm_gem_objects to lock
 * @count: Number of objects in @objs
 * @acquire_ctx: struct ww_acquire_ctx that will be initialized as
 * part of tracking this set of locked reservations.
 */
int
drm_gem_lock_reservations(struct drm_gem_object **objs, int count,
			  struct ww_acquire_ctx *acquire_ctx)
{
	int contended = -1;
	int i, ret;

	ww_acquire_init(acquire_ctx, &reservation_ww_class);

retry:
	if (contended != -1) {
		struct drm_gem_object *obj = objs[contended];

		ret = dma_resv_lock_slow_interruptible(obj->resv,
								 acquire_ctx);
		if (ret) {
			ww_acquire_fini(acquire_ctx);
			return ret;
		}
	}

	for (i = 0; i < count; i++) {
		if (i == contended)
			continue;

		ret = dma_resv_lock_interruptible(objs[i]->resv,
							    acquire_ctx);
		if (ret) {
			int j;

			for (j = 0; j < i; j++)
				dma_resv_unlock(objs[j]->resv);

			if (contended != -1 && contended >= i)
				dma_resv_unlock(objs[contended]->resv);

			if (ret == -EDEADLK) {
				contended = i;
				goto retry;
			}

			ww_acquire_fini(acquire_ctx);
			return ret;
		}
	}

	ww_acquire_done(acquire_ctx);

	return 0;
}
EXPORT_SYMBOL(drm_gem_lock_reservations);

void
drm_gem_unlock_reservations(struct drm_gem_object **objs, int count,
			    struct ww_acquire_ctx *acquire_ctx)
{
	int i;

	for (i = 0; i < count; i++)
		dma_resv_unlock(objs[i]->resv);

	ww_acquire_fini(acquire_ctx);
}
EXPORT_SYMBOL(drm_gem_unlock_reservations);

/**
 * drm_gem_lru_init - initialize a LRU
 *
 * @lru: The LRU to initialize
 * @lock: The lock protecting the LRU
 */
void
drm_gem_lru_init(struct drm_gem_lru *lru, struct mutex *lock)
{
	lru->lock = lock;
	lru->count = 0;
	INIT_LIST_HEAD(&lru->list);
}
EXPORT_SYMBOL(drm_gem_lru_init);

static void
drm_gem_lru_remove_locked(struct drm_gem_object *obj)
{
	obj->lru->count -= obj->size >> PAGE_SHIFT;
	WARN_ON(obj->lru->count < 0);
	list_del(&obj->lru_node);
	obj->lru = NULL;
}

/**
 * drm_gem_lru_remove - remove object from whatever LRU it is in
 *
 * If the object is currently in any LRU, remove it.
 *
 * @obj: The GEM object to remove from current LRU
 */
void
drm_gem_lru_remove(struct drm_gem_object *obj)
{
	struct drm_gem_lru *lru = obj->lru;

	if (!lru)
		return;

	mutex_lock(lru->lock);
	drm_gem_lru_remove_locked(obj);
	mutex_unlock(lru->lock);
}
EXPORT_SYMBOL(drm_gem_lru_remove);

/**
 * drm_gem_lru_move_tail_locked - move the object to the tail of the LRU
 *
 * Like &drm_gem_lru_move_tail but lru lock must be held
 *
 * @lru: The LRU to move the object into.
 * @obj: The GEM object to move into this LRU
 */
void
drm_gem_lru_move_tail_locked(struct drm_gem_lru *lru, struct drm_gem_object *obj)
{
	lockdep_assert_held_once(lru->lock);

	if (obj->lru)
		drm_gem_lru_remove_locked(obj);

	lru->count += obj->size >> PAGE_SHIFT;
	list_add_tail(&obj->lru_node, &lru->list);
	obj->lru = lru;
}
EXPORT_SYMBOL(drm_gem_lru_move_tail_locked);

/**
 * drm_gem_lru_move_tail - move the object to the tail of the LRU
 *
 * If the object is already in this LRU it will be moved to the
 * tail.  Otherwise it will be removed from whichever other LRU
 * it is in (if any) and moved into this LRU.
 *
 * @lru: The LRU to move the object into.
 * @obj: The GEM object to move into this LRU
 */
void
drm_gem_lru_move_tail(struct drm_gem_lru *lru, struct drm_gem_object *obj)
{
	mutex_lock(lru->lock);
	drm_gem_lru_move_tail_locked(lru, obj);
	mutex_unlock(lru->lock);
}
EXPORT_SYMBOL(drm_gem_lru_move_tail);

/**
 * drm_gem_lru_scan - helper to implement shrinker.scan_objects
 *
 * If the shrink callback succeeds, it is expected that the driver
 * move the object out of this LRU.
 *
 * If the LRU possibly contain active buffers, it is the responsibility
 * of the shrink callback to check for this (ie. dma_resv_test_signaled())
 * or if necessary block until the buffer becomes idle.
 *
 * @lru: The LRU to scan
 * @nr_to_scan: The number of pages to try to reclaim
 * @remaining: The number of pages left to reclaim, should be initialized by caller
 * @shrink: Callback to try to shrink/reclaim the object.
 */
unsigned long
drm_gem_lru_scan(struct drm_gem_lru *lru,
		 unsigned int nr_to_scan,
		 unsigned long *remaining,
		 bool (*shrink)(struct drm_gem_object *obj))
{
	struct drm_gem_lru still_in_lru;
	struct drm_gem_object *obj;
	unsigned freed = 0;

	drm_gem_lru_init(&still_in_lru, lru->lock);

	mutex_lock(lru->lock);

	while (freed < nr_to_scan) {
		obj = list_first_entry_or_null(&lru->list, typeof(*obj), lru_node);

		if (!obj)
			break;

		drm_gem_lru_move_tail_locked(&still_in_lru, obj);

		/*
		 * If it's in the process of being freed, gem_object->free()
		 * may be blocked on lock waiting to remove it.  So just
		 * skip it.
		 */
		if (!kref_get_unless_zero(&obj->refcount))
			continue;

		/*
		 * Now that we own a reference, we can drop the lock for the
		 * rest of the loop body, to reduce contention with other
		 * code paths that need the LRU lock
		 */
		mutex_unlock(lru->lock);

		/*
		 * Note that this still needs to be trylock, since we can
		 * hit shrinker in response to trying to get backing pages
		 * for this obj (ie. while it's lock is already held)
		 */
		if (!dma_resv_trylock(obj->resv)) {
			*remaining += obj->size >> PAGE_SHIFT;
			goto tail;
		}

		if (shrink(obj)) {
			freed += obj->size >> PAGE_SHIFT;

			/*
			 * If we succeeded in releasing the object's backing
			 * pages, we expect the driver to have moved the object
			 * out of this LRU
			 */
			WARN_ON(obj->lru == &still_in_lru);
			WARN_ON(obj->lru == lru);
		}

		dma_resv_unlock(obj->resv);

tail:
		drm_gem_object_put(obj);
		mutex_lock(lru->lock);
	}

	/*
	 * Move objects we've skipped over out of the temporary still_in_lru
	 * back into this LRU
	 */
	list_for_each_entry (obj, &still_in_lru.list, lru_node)
		obj->lru = lru;
	list_splice_tail(&still_in_lru.list, &lru->list);
	lru->count += still_in_lru.count;

	mutex_unlock(lru->lock);

	return freed;
}
EXPORT_SYMBOL(drm_gem_lru_scan);

/**
 * drm_gem_evict - helper to evict backing pages for a GEM object
 * @obj: obj in question
 */
int drm_gem_evict(struct drm_gem_object *obj)
{
	dma_resv_assert_held(obj->resv);

	if (!dma_resv_test_signaled(obj->resv, DMA_RESV_USAGE_READ))
		return -EBUSY;

	if (obj->funcs->evict)
		return obj->funcs->evict(obj);

	return 0;
}
EXPORT_SYMBOL(drm_gem_evict);
