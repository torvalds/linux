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

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/shmem_fs.h>
#include <linux/dma-buf.h>
#include <drm/drmP.h>
#include <drm/drm_vma_manager.h>
#include <drm/drm_gem.h>
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

/*
 * We make up offsets for buffer objects so we can recognize them at
 * mmap time.
 */

/* pgoff in mmap is an unsigned long, so we need to make sure that
 * the faked up offset will fit
 */

#if BITS_PER_LONG == 64
#define DRM_FILE_PAGE_OFFSET_START ((0xFFFFFFFFUL >> PAGE_SHIFT) + 1)
#define DRM_FILE_PAGE_OFFSET_SIZE ((0xFFFFFFFFUL >> PAGE_SHIFT) * 16)
#else
#define DRM_FILE_PAGE_OFFSET_START ((0xFFFFFFFUL >> PAGE_SHIFT) + 1)
#define DRM_FILE_PAGE_OFFSET_SIZE ((0xFFFFFFFUL >> PAGE_SHIFT) * 16)
#endif

/**
 * drm_gem_init - Initialize the GEM device fields
 * @dev: drm_devic structure to initialize
 */
int
drm_gem_init(struct drm_device *dev)
{
	struct drm_vma_offset_manager *vma_offset_manager;

	mutex_init(&dev->object_name_lock);
	idr_init(&dev->object_name_idr);

	vma_offset_manager = kzalloc(sizeof(*vma_offset_manager), GFP_KERNEL);
	if (!vma_offset_manager) {
		DRM_ERROR("out of memory\n");
		return -ENOMEM;
	}

	dev->vma_offset_manager = vma_offset_manager;
	drm_vma_offset_manager_init(vma_offset_manager,
				    DRM_FILE_PAGE_OFFSET_START,
				    DRM_FILE_PAGE_OFFSET_SIZE);

	return 0;
}

void
drm_gem_destroy(struct drm_device *dev)
{

	drm_vma_offset_manager_destroy(dev->vma_offset_manager);
	kfree(dev->vma_offset_manager);
	dev->vma_offset_manager = NULL;
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
	drm_vma_node_reset(&obj->vma_node);
}
EXPORT_SYMBOL(drm_gem_private_object_init);

static void
drm_gem_remove_prime_handles(struct drm_gem_object *obj, struct drm_file *filp)
{
	/*
	 * Note: obj->dma_buf can't disappear as long as we still hold a
	 * handle reference in obj->handle_count.
	 */
	mutex_lock(&filp->prime.lock);
	if (obj->dma_buf) {
		drm_prime_remove_buf_handle_locked(&filp->prime,
						   obj->dma_buf);
	}
	mutex_unlock(&filp->prime.lock);
}

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
drm_gem_object_handle_unreference_unlocked(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	bool final = false;

	if (WARN_ON(obj->handle_count == 0))
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
		drm_gem_object_unreference_unlocked(obj);
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
	struct drm_device *dev = obj->dev;

	if (drm_core_check_feature(dev, DRIVER_PRIME))
		drm_gem_remove_prime_handles(obj, file_priv);
	drm_vma_node_revoke(&obj->vma_node, file_priv->filp);

	if (dev->driver->gem_close_object)
		dev->driver->gem_close_object(obj, file_priv);

	drm_gem_object_handle_unreference_unlocked(obj);

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

	/* This is gross. The idr system doesn't let us try a delete and
	 * return an error code.  It just spews if you fail at deleting.
	 * So, we have to grab a lock around finding the object and then
	 * doing the delete on it and dropping the refcount, or the user
	 * could race us to double-decrement the refcount and cause a
	 * use-after-free later.  Given the frequency of our handle lookups,
	 * we may want to use ida for number allocation and a hash table
	 * for the pointers, anyway.
	 */
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
 * drm_gem_dumb_destroy - dumb fb callback helper for gem based drivers
 * @file: drm file-private structure to remove the dumb handle from
 * @dev: corresponding drm_device
 * @handle: the dumb handle to remove
 * 
 * This implements the ->dumb_destroy kms driver callback for drivers which use
 * gem to manage their backing storage.
 */
int drm_gem_dumb_destroy(struct drm_file *file,
			 struct drm_device *dev,
			 uint32_t handle)
{
	return drm_gem_handle_delete(file, handle);
}
EXPORT_SYMBOL(drm_gem_dumb_destroy);

/**
 * drm_gem_handle_create_tail - internal functions to create a handle
 * @file_priv: drm file-private structure to register the handle for
 * @obj: object to register
 * @handlep: pointer to return the created handle to the caller
 * 
 * This expects the dev->object_name_lock to be held already and will drop it
 * before returning. Used to avoid races in establishing new handles when
 * importing an object from either an flink name or a dma-buf.
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
		drm_gem_object_reference(obj);

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

	ret = drm_vma_node_allow(&obj->vma_node, file_priv->filp);
	if (ret)
		goto err_remove;

	if (dev->driver->gem_open_object) {
		ret = dev->driver->gem_open_object(obj, file_priv);
		if (ret)
			goto err_revoke;
	}

	*handlep = handle;
	return 0;

err_revoke:
	drm_vma_node_revoke(&obj->vma_node, file_priv->filp);
err_remove:
	spin_lock(&file_priv->table_lock);
	idr_remove(&file_priv->object_idr, handle);
	spin_unlock(&file_priv->table_lock);
err_unref:
	drm_gem_object_handle_unreference_unlocked(obj);
	return ret;
}

/**
 * drm_gem_handle_create - create a gem handle for an object
 * @file_priv: drm file-private structure to register the handle for
 * @obj: object to register
 * @handlep: pionter to return the created handle to the caller
 *
 * Create a handle for this object. This adds a handle reference
 * to the object, which includes a regular reference count. Callers
 * will likely want to dereference the object afterwards.
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
 * the virtual size differs from the physical size (ie. obj->size).  Otherwise
 * just use drm_gem_create_mmap_offset().
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
 * after drm_gem_init_object() via mapping_set_gfp_mask(). shmem-core takes care
 * to keep pages in the required zone during swap-in.
 */
struct page **drm_gem_get_pages(struct drm_gem_object *obj)
{
	struct address_space *mapping;
	struct page *p, **pages;
	int i, npages;

	/* This is the shared memory object that backs the GEM resource */
	mapping = obj->filp->f_mapping;

	/* We already BUG_ON() for non-page-aligned sizes in
	 * drm_gem_object_init(), so we should never hit this unless
	 * driver author is doing something really wrong:
	 */
	WARN_ON((obj->size & (PAGE_SIZE - 1)) != 0);

	npages = obj->size >> PAGE_SHIFT;

	pages = drm_malloc_ab(npages, sizeof(struct page *));
	if (pages == NULL)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < npages; i++) {
		p = shmem_read_mapping_page(mapping, i);
		if (IS_ERR(p))
			goto fail;
		pages[i] = p;

		/* Make sure shmem keeps __GFP_DMA32 allocated pages in the
		 * correct region during swapin. Note that this requires
		 * __GFP_DMA32 to be set in mapping_gfp_mask(inode->i_mapping)
		 * so shmem can relocate pages during swapin if required.
		 */
		BUG_ON(mapping_gfp_constraint(mapping, __GFP_DMA32) &&
				(page_to_pfn(p) >= 0x00100000UL));
	}

	return pages;

fail:
	while (i--)
		put_page(pages[i]);

	drm_free_large(pages);
	return ERR_CAST(p);
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

	/* We already BUG_ON() for non-page-aligned sizes in
	 * drm_gem_object_init(), so we should never hit this unless
	 * driver author is doing something really wrong:
	 */
	WARN_ON((obj->size & (PAGE_SIZE - 1)) != 0);

	npages = obj->size >> PAGE_SHIFT;

	for (i = 0; i < npages; i++) {
		if (dirty)
			set_page_dirty(pages[i]);

		if (accessed)
			mark_page_accessed(pages[i]);

		/* Undo the reference we took when populating the table */
		put_page(pages[i]);
	}

	drm_free_large(pages);
}
EXPORT_SYMBOL(drm_gem_put_pages);

/**
 * drm_gem_object_lookup - look up a GEM object from it's handle
 * @filp: DRM file private date
 * @handle: userspace handle
 *
 * Returns:
 *
 * A reference to the object named by the handle if such exists on @filp, NULL
 * otherwise.
 */
struct drm_gem_object *
drm_gem_object_lookup(struct drm_file *filp, u32 handle)
{
	struct drm_gem_object *obj;

	spin_lock(&filp->table_lock);

	/* Check if we currently have a reference on the object */
	obj = idr_find(&filp->object_idr, handle);
	if (obj)
		drm_gem_object_reference(obj);

	spin_unlock(&filp->table_lock);

	return obj;
}
EXPORT_SYMBOL(drm_gem_object_lookup);

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
		return -ENODEV;

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
		return -ENODEV;

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
	drm_gem_object_unreference_unlocked(obj);
	return ret;
}

/**
 * drm_gem_open - implementation of the GEM_OPEN ioctl
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
		return -ENODEV;

	mutex_lock(&dev->object_name_lock);
	obj = idr_find(&dev->object_name_idr, (int) args->name);
	if (obj) {
		drm_gem_object_reference(obj);
	} else {
		mutex_unlock(&dev->object_name_lock);
		return -ENOENT;
	}

	/* drm_gem_handle_create_tail unlocks dev->object_name_lock. */
	ret = drm_gem_handle_create_tail(file_priv, obj, &handle);
	drm_gem_object_unreference_unlocked(obj);
	if (ret)
		return ret;

	args->handle = handle;
	args->size = obj->size;

	return 0;
}

/**
 * gem_gem_open - initalizes GEM file-private structures at devnode open time
 * @dev: drm_device which is being opened by userspace
 * @file_private: drm file-private structure to set up
 *
 * Called at device open time, sets up the structure for handling refcounting
 * of mm objects.
 */
void
drm_gem_open(struct drm_device *dev, struct drm_file *file_private)
{
	idr_init(&file_private->object_idr);
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
 * This releases any structures and resources used by @obj and is the invers of
 * drm_gem_object_init().
 */
void
drm_gem_object_release(struct drm_gem_object *obj)
{
	WARN_ON(obj->dma_buf);

	if (obj->filp)
		fput(obj->filp);

	drm_gem_free_mmap_offset(obj);
}
EXPORT_SYMBOL(drm_gem_object_release);

/**
 * drm_gem_object_free - free a GEM object
 * @kref: kref of the object to free
 *
 * Called after the last reference to the object has been lost.
 * Must be called holding &drm_device->struct_mutex.
 *
 * Frees the object
 */
void
drm_gem_object_free(struct kref *kref)
{
	struct drm_gem_object *obj =
		container_of(kref, struct drm_gem_object, refcount);
	struct drm_device *dev = obj->dev;

	if (dev->driver->gem_free_object_unlocked) {
		dev->driver->gem_free_object_unlocked(obj);
	} else if (dev->driver->gem_free_object) {
		WARN_ON(!mutex_is_locked(&dev->struct_mutex));

		dev->driver->gem_free_object(obj);
	}
}
EXPORT_SYMBOL(drm_gem_object_free);

/**
 * drm_gem_object_unreference_unlocked - release a GEM BO reference
 * @obj: GEM buffer object
 *
 * This releases a reference to @obj. Callers must not hold the
 * dev->struct_mutex lock when calling this function.
 *
 * See also __drm_gem_object_unreference().
 */
void
drm_gem_object_unreference_unlocked(struct drm_gem_object *obj)
{
	struct drm_device *dev;

	if (!obj)
		return;

	dev = obj->dev;
	might_lock(&dev->struct_mutex);

	if (dev->driver->gem_free_object_unlocked)
		kref_put(&obj->refcount, drm_gem_object_free);
	else if (kref_put_mutex(&obj->refcount, drm_gem_object_free,
				&dev->struct_mutex))
		mutex_unlock(&dev->struct_mutex);
}
EXPORT_SYMBOL(drm_gem_object_unreference_unlocked);

/**
 * drm_gem_object_unreference - release a GEM BO reference
 * @obj: GEM buffer object
 *
 * This releases a reference to @obj. Callers must hold the dev->struct_mutex
 * lock when calling this function, even when the driver doesn't use
 * dev->struct_mutex for anything.
 *
 * For drivers not encumbered with legacy locking use
 * drm_gem_object_unreference_unlocked() instead.
 */
void
drm_gem_object_unreference(struct drm_gem_object *obj)
{
	if (obj) {
		WARN_ON(!mutex_is_locked(&obj->dev->struct_mutex));

		kref_put(&obj->refcount, drm_gem_object_free);
	}
}
EXPORT_SYMBOL(drm_gem_object_unreference);

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

	drm_gem_object_reference(obj);
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

	drm_gem_object_unreference_unlocked(obj);
}
EXPORT_SYMBOL(drm_gem_vm_close);

/**
 * drm_gem_mmap_obj - memory map a GEM object
 * @obj: the GEM object to map
 * @obj_size: the object size to be mapped, in bytes
 * @vma: VMA for the area to be mapped
 *
 * Set up the VMA to prepare mapping of the GEM object using the gem_vm_ops
 * provided by the driver. Depending on their requirements, drivers can either
 * provide a fault handler in their gem_vm_ops (in which case any accesses to
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
 * size, or if no gem_vm_ops are provided.
 */
int drm_gem_mmap_obj(struct drm_gem_object *obj, unsigned long obj_size,
		     struct vm_area_struct *vma)
{
	struct drm_device *dev = obj->dev;

	/* Check for valid size. */
	if (obj_size < vma->vm_end - vma->vm_start)
		return -EINVAL;

	if (!dev->driver->gem_vm_ops)
		return -EINVAL;

	vma->vm_flags |= VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP;
	vma->vm_ops = dev->driver->gem_vm_ops;
	vma->vm_private_data = obj;
	vma->vm_page_prot = pgprot_writecombine(vm_get_page_prot(vma->vm_flags));

	/* Take a ref for this mapping of the object, so that the fault
	 * handler can dereference the mmap offset's pointer to the object.
	 * This reference is cleaned up by the corresponding vm_close
	 * (which should happen whether the vma was created by this call, or
	 * by a vm_open due to mremap or partial unmap or whatever).
	 */
	drm_gem_object_reference(obj);

	return 0;
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

	if (drm_device_is_unplugged(dev))
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

	if (!drm_vma_node_is_allowed(node, filp)) {
		drm_gem_object_unreference_unlocked(obj);
		return -EACCES;
	}

	ret = drm_gem_mmap_obj(obj, drm_vma_node_size(node) << PAGE_SHIFT,
			       vma);

	drm_gem_object_unreference_unlocked(obj);

	return ret;
}
EXPORT_SYMBOL(drm_gem_mmap);
