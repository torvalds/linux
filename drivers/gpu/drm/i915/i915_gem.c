/*
 * Copyright © 2008-2015 Intel Corporation
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

#include <drm/drmP.h>
#include <drm/drm_vma_manager.h>
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include "i915_gem_dmabuf.h"
#include "i915_vgpu.h"
#include "i915_trace.h"
#include "intel_drv.h"
#include "intel_frontbuffer.h"
#include "intel_mocs.h"
#include <linux/reservation.h>
#include <linux/shmem_fs.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/pci.h>
#include <linux/dma-buf.h>

static void i915_gem_object_flush_gtt_write_domain(struct drm_i915_gem_object *obj);
static void i915_gem_object_flush_cpu_write_domain(struct drm_i915_gem_object *obj);

static bool cpu_cache_is_coherent(struct drm_device *dev,
				  enum i915_cache_level level)
{
	return HAS_LLC(dev) || level != I915_CACHE_NONE;
}

static bool cpu_write_needs_clflush(struct drm_i915_gem_object *obj)
{
	if (obj->base.write_domain == I915_GEM_DOMAIN_CPU)
		return false;

	if (!cpu_cache_is_coherent(obj->base.dev, obj->cache_level))
		return true;

	return obj->pin_display;
}

static int
insert_mappable_node(struct drm_i915_private *i915,
                     struct drm_mm_node *node, u32 size)
{
	memset(node, 0, sizeof(*node));
	return drm_mm_insert_node_in_range_generic(&i915->ggtt.base.mm, node,
						   size, 0, 0, 0,
						   i915->ggtt.mappable_end,
						   DRM_MM_SEARCH_DEFAULT,
						   DRM_MM_CREATE_DEFAULT);
}

static void
remove_mappable_node(struct drm_mm_node *node)
{
	drm_mm_remove_node(node);
}

/* some bookkeeping */
static void i915_gem_info_add_obj(struct drm_i915_private *dev_priv,
				  u64 size)
{
	spin_lock(&dev_priv->mm.object_stat_lock);
	dev_priv->mm.object_count++;
	dev_priv->mm.object_memory += size;
	spin_unlock(&dev_priv->mm.object_stat_lock);
}

static void i915_gem_info_remove_obj(struct drm_i915_private *dev_priv,
				     u64 size)
{
	spin_lock(&dev_priv->mm.object_stat_lock);
	dev_priv->mm.object_count--;
	dev_priv->mm.object_memory -= size;
	spin_unlock(&dev_priv->mm.object_stat_lock);
}

static int
i915_gem_wait_for_error(struct i915_gpu_error *error)
{
	int ret;

	if (!i915_reset_in_progress(error))
		return 0;

	/*
	 * Only wait 10 seconds for the gpu reset to complete to avoid hanging
	 * userspace. If it takes that long something really bad is going on and
	 * we should simply try to bail out and fail as gracefully as possible.
	 */
	ret = wait_event_interruptible_timeout(error->reset_queue,
					       !i915_reset_in_progress(error),
					       10*HZ);
	if (ret == 0) {
		DRM_ERROR("Timed out waiting for the gpu reset to complete\n");
		return -EIO;
	} else if (ret < 0) {
		return ret;
	} else {
		return 0;
	}
}

int i915_mutex_lock_interruptible(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	int ret;

	ret = i915_gem_wait_for_error(&dev_priv->gpu_error);
	if (ret)
		return ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	return 0;
}

int
i915_gem_get_aperture_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	struct drm_i915_gem_get_aperture *args = data;
	struct i915_vma *vma;
	size_t pinned;

	pinned = 0;
	mutex_lock(&dev->struct_mutex);
	list_for_each_entry(vma, &ggtt->base.active_list, vm_link)
		if (i915_vma_is_pinned(vma))
			pinned += vma->node.size;
	list_for_each_entry(vma, &ggtt->base.inactive_list, vm_link)
		if (i915_vma_is_pinned(vma))
			pinned += vma->node.size;
	mutex_unlock(&dev->struct_mutex);

	args->aper_size = ggtt->base.total;
	args->aper_available_size = args->aper_size - pinned;

	return 0;
}

static int
i915_gem_object_get_pages_phys(struct drm_i915_gem_object *obj)
{
	struct address_space *mapping = obj->base.filp->f_mapping;
	char *vaddr = obj->phys_handle->vaddr;
	struct sg_table *st;
	struct scatterlist *sg;
	int i;

	if (WARN_ON(i915_gem_object_needs_bit17_swizzle(obj)))
		return -EINVAL;

	for (i = 0; i < obj->base.size / PAGE_SIZE; i++) {
		struct page *page;
		char *src;

		page = shmem_read_mapping_page(mapping, i);
		if (IS_ERR(page))
			return PTR_ERR(page);

		src = kmap_atomic(page);
		memcpy(vaddr, src, PAGE_SIZE);
		drm_clflush_virt_range(vaddr, PAGE_SIZE);
		kunmap_atomic(src);

		put_page(page);
		vaddr += PAGE_SIZE;
	}

	i915_gem_chipset_flush(to_i915(obj->base.dev));

	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (st == NULL)
		return -ENOMEM;

	if (sg_alloc_table(st, 1, GFP_KERNEL)) {
		kfree(st);
		return -ENOMEM;
	}

	sg = st->sgl;
	sg->offset = 0;
	sg->length = obj->base.size;

	sg_dma_address(sg) = obj->phys_handle->busaddr;
	sg_dma_len(sg) = obj->base.size;

	obj->pages = st;
	return 0;
}

static void
i915_gem_object_put_pages_phys(struct drm_i915_gem_object *obj)
{
	int ret;

	BUG_ON(obj->madv == __I915_MADV_PURGED);

	ret = i915_gem_object_set_to_cpu_domain(obj, true);
	if (WARN_ON(ret)) {
		/* In the event of a disaster, abandon all caches and
		 * hope for the best.
		 */
		obj->base.read_domains = obj->base.write_domain = I915_GEM_DOMAIN_CPU;
	}

	if (obj->madv == I915_MADV_DONTNEED)
		obj->dirty = 0;

	if (obj->dirty) {
		struct address_space *mapping = obj->base.filp->f_mapping;
		char *vaddr = obj->phys_handle->vaddr;
		int i;

		for (i = 0; i < obj->base.size / PAGE_SIZE; i++) {
			struct page *page;
			char *dst;

			page = shmem_read_mapping_page(mapping, i);
			if (IS_ERR(page))
				continue;

			dst = kmap_atomic(page);
			drm_clflush_virt_range(vaddr, PAGE_SIZE);
			memcpy(dst, vaddr, PAGE_SIZE);
			kunmap_atomic(dst);

			set_page_dirty(page);
			if (obj->madv == I915_MADV_WILLNEED)
				mark_page_accessed(page);
			put_page(page);
			vaddr += PAGE_SIZE;
		}
		obj->dirty = 0;
	}

	sg_free_table(obj->pages);
	kfree(obj->pages);
}

static void
i915_gem_object_release_phys(struct drm_i915_gem_object *obj)
{
	drm_pci_free(obj->base.dev, obj->phys_handle);
}

static const struct drm_i915_gem_object_ops i915_gem_phys_ops = {
	.get_pages = i915_gem_object_get_pages_phys,
	.put_pages = i915_gem_object_put_pages_phys,
	.release = i915_gem_object_release_phys,
};

int i915_gem_object_unbind(struct drm_i915_gem_object *obj)
{
	struct i915_vma *vma;
	LIST_HEAD(still_in_list);
	int ret;

	lockdep_assert_held(&obj->base.dev->struct_mutex);

	/* Closed vma are removed from the obj->vma_list - but they may
	 * still have an active binding on the object. To remove those we
	 * must wait for all rendering to complete to the object (as unbinding
	 * must anyway), and retire the requests.
	 */
	ret = i915_gem_object_wait_rendering(obj, false);
	if (ret)
		return ret;

	i915_gem_retire_requests(to_i915(obj->base.dev));

	while ((vma = list_first_entry_or_null(&obj->vma_list,
					       struct i915_vma,
					       obj_link))) {
		list_move_tail(&vma->obj_link, &still_in_list);
		ret = i915_vma_unbind(vma);
		if (ret)
			break;
	}
	list_splice(&still_in_list, &obj->vma_list);

	return ret;
}

/**
 * Ensures that all rendering to the object has completed and the object is
 * safe to unbind from the GTT or access from the CPU.
 * @obj: i915 gem object
 * @readonly: waiting for just read access or read-write access
 */
int
i915_gem_object_wait_rendering(struct drm_i915_gem_object *obj,
			       bool readonly)
{
	struct reservation_object *resv;
	struct i915_gem_active *active;
	unsigned long active_mask;
	int idx;

	lockdep_assert_held(&obj->base.dev->struct_mutex);

	if (!readonly) {
		active = obj->last_read;
		active_mask = i915_gem_object_get_active(obj);
	} else {
		active_mask = 1;
		active = &obj->last_write;
	}

	for_each_active(active_mask, idx) {
		int ret;

		ret = i915_gem_active_wait(&active[idx],
					   &obj->base.dev->struct_mutex);
		if (ret)
			return ret;
	}

	resv = i915_gem_object_get_dmabuf_resv(obj);
	if (resv) {
		long err;

		err = reservation_object_wait_timeout_rcu(resv, !readonly, true,
							  MAX_SCHEDULE_TIMEOUT);
		if (err < 0)
			return err;
	}

	return 0;
}

/* A nonblocking variant of the above wait. Must be called prior to
 * acquiring the mutex for the object, as the object state may change
 * during this call. A reference must be held by the caller for the object.
 */
static __must_check int
__unsafe_wait_rendering(struct drm_i915_gem_object *obj,
			struct intel_rps_client *rps,
			bool readonly)
{
	struct i915_gem_active *active;
	unsigned long active_mask;
	int idx;

	active_mask = __I915_BO_ACTIVE(obj);
	if (!active_mask)
		return 0;

	if (!readonly) {
		active = obj->last_read;
	} else {
		active_mask = 1;
		active = &obj->last_write;
	}

	for_each_active(active_mask, idx) {
		int ret;

		ret = i915_gem_active_wait_unlocked(&active[idx],
						    I915_WAIT_INTERRUPTIBLE,
						    NULL, rps);
		if (ret)
			return ret;
	}

	return 0;
}

static struct intel_rps_client *to_rps_client(struct drm_file *file)
{
	struct drm_i915_file_private *fpriv = file->driver_priv;

	return &fpriv->rps;
}

int
i915_gem_object_attach_phys(struct drm_i915_gem_object *obj,
			    int align)
{
	drm_dma_handle_t *phys;
	int ret;

	if (obj->phys_handle) {
		if ((unsigned long)obj->phys_handle->vaddr & (align -1))
			return -EBUSY;

		return 0;
	}

	if (obj->madv != I915_MADV_WILLNEED)
		return -EFAULT;

	if (obj->base.filp == NULL)
		return -EINVAL;

	ret = i915_gem_object_unbind(obj);
	if (ret)
		return ret;

	ret = i915_gem_object_put_pages(obj);
	if (ret)
		return ret;

	/* create a new object */
	phys = drm_pci_alloc(obj->base.dev, obj->base.size, align);
	if (!phys)
		return -ENOMEM;

	obj->phys_handle = phys;
	obj->ops = &i915_gem_phys_ops;

	return i915_gem_object_get_pages(obj);
}

static int
i915_gem_phys_pwrite(struct drm_i915_gem_object *obj,
		     struct drm_i915_gem_pwrite *args,
		     struct drm_file *file_priv)
{
	struct drm_device *dev = obj->base.dev;
	void *vaddr = obj->phys_handle->vaddr + args->offset;
	char __user *user_data = u64_to_user_ptr(args->data_ptr);
	int ret = 0;

	/* We manually control the domain here and pretend that it
	 * remains coherent i.e. in the GTT domain, like shmem_pwrite.
	 */
	ret = i915_gem_object_wait_rendering(obj, false);
	if (ret)
		return ret;

	intel_fb_obj_invalidate(obj, ORIGIN_CPU);
	if (__copy_from_user_inatomic_nocache(vaddr, user_data, args->size)) {
		unsigned long unwritten;

		/* The physical object once assigned is fixed for the lifetime
		 * of the obj, so we can safely drop the lock and continue
		 * to access vaddr.
		 */
		mutex_unlock(&dev->struct_mutex);
		unwritten = copy_from_user(vaddr, user_data, args->size);
		mutex_lock(&dev->struct_mutex);
		if (unwritten) {
			ret = -EFAULT;
			goto out;
		}
	}

	drm_clflush_virt_range(vaddr, args->size);
	i915_gem_chipset_flush(to_i915(dev));

out:
	intel_fb_obj_flush(obj, false, ORIGIN_CPU);
	return ret;
}

void *i915_gem_object_alloc(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	return kmem_cache_zalloc(dev_priv->objects, GFP_KERNEL);
}

void i915_gem_object_free(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);
	kmem_cache_free(dev_priv->objects, obj);
}

static int
i915_gem_create(struct drm_file *file,
		struct drm_device *dev,
		uint64_t size,
		uint32_t *handle_p)
{
	struct drm_i915_gem_object *obj;
	int ret;
	u32 handle;

	size = roundup(size, PAGE_SIZE);
	if (size == 0)
		return -EINVAL;

	/* Allocate the new object */
	obj = i915_gem_object_create(dev, size);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	ret = drm_gem_handle_create(file, &obj->base, &handle);
	/* drop reference from allocate - handle holds it now */
	i915_gem_object_put_unlocked(obj);
	if (ret)
		return ret;

	*handle_p = handle;
	return 0;
}

int
i915_gem_dumb_create(struct drm_file *file,
		     struct drm_device *dev,
		     struct drm_mode_create_dumb *args)
{
	/* have to work out size/pitch and return them */
	args->pitch = ALIGN(args->width * DIV_ROUND_UP(args->bpp, 8), 64);
	args->size = args->pitch * args->height;
	return i915_gem_create(file, dev,
			       args->size, &args->handle);
}

/**
 * Creates a new mm object and returns a handle to it.
 * @dev: drm device pointer
 * @data: ioctl data blob
 * @file: drm file pointer
 */
int
i915_gem_create_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file)
{
	struct drm_i915_gem_create *args = data;

	return i915_gem_create(file, dev,
			       args->size, &args->handle);
}

static inline int
__copy_to_user_swizzled(char __user *cpu_vaddr,
			const char *gpu_vaddr, int gpu_offset,
			int length)
{
	int ret, cpu_offset = 0;

	while (length > 0) {
		int cacheline_end = ALIGN(gpu_offset + 1, 64);
		int this_length = min(cacheline_end - gpu_offset, length);
		int swizzled_gpu_offset = gpu_offset ^ 64;

		ret = __copy_to_user(cpu_vaddr + cpu_offset,
				     gpu_vaddr + swizzled_gpu_offset,
				     this_length);
		if (ret)
			return ret + length;

		cpu_offset += this_length;
		gpu_offset += this_length;
		length -= this_length;
	}

	return 0;
}

static inline int
__copy_from_user_swizzled(char *gpu_vaddr, int gpu_offset,
			  const char __user *cpu_vaddr,
			  int length)
{
	int ret, cpu_offset = 0;

	while (length > 0) {
		int cacheline_end = ALIGN(gpu_offset + 1, 64);
		int this_length = min(cacheline_end - gpu_offset, length);
		int swizzled_gpu_offset = gpu_offset ^ 64;

		ret = __copy_from_user(gpu_vaddr + swizzled_gpu_offset,
				       cpu_vaddr + cpu_offset,
				       this_length);
		if (ret)
			return ret + length;

		cpu_offset += this_length;
		gpu_offset += this_length;
		length -= this_length;
	}

	return 0;
}

/*
 * Pins the specified object's pages and synchronizes the object with
 * GPU accesses. Sets needs_clflush to non-zero if the caller should
 * flush the object from the CPU cache.
 */
int i915_gem_obj_prepare_shmem_read(struct drm_i915_gem_object *obj,
				    unsigned int *needs_clflush)
{
	int ret;

	*needs_clflush = 0;

	if (!i915_gem_object_has_struct_page(obj))
		return -ENODEV;

	ret = i915_gem_object_wait_rendering(obj, true);
	if (ret)
		return ret;

	ret = i915_gem_object_get_pages(obj);
	if (ret)
		return ret;

	i915_gem_object_pin_pages(obj);

	i915_gem_object_flush_gtt_write_domain(obj);

	/* If we're not in the cpu read domain, set ourself into the gtt
	 * read domain and manually flush cachelines (if required). This
	 * optimizes for the case when the gpu will dirty the data
	 * anyway again before the next pread happens.
	 */
	if (!(obj->base.read_domains & I915_GEM_DOMAIN_CPU))
		*needs_clflush = !cpu_cache_is_coherent(obj->base.dev,
							obj->cache_level);

	if (*needs_clflush && !static_cpu_has(X86_FEATURE_CLFLUSH)) {
		ret = i915_gem_object_set_to_cpu_domain(obj, false);
		if (ret)
			goto err_unpin;

		*needs_clflush = 0;
	}

	/* return with the pages pinned */
	return 0;

err_unpin:
	i915_gem_object_unpin_pages(obj);
	return ret;
}

int i915_gem_obj_prepare_shmem_write(struct drm_i915_gem_object *obj,
				     unsigned int *needs_clflush)
{
	int ret;

	*needs_clflush = 0;
	if (!i915_gem_object_has_struct_page(obj))
		return -ENODEV;

	ret = i915_gem_object_wait_rendering(obj, false);
	if (ret)
		return ret;

	ret = i915_gem_object_get_pages(obj);
	if (ret)
		return ret;

	i915_gem_object_pin_pages(obj);

	i915_gem_object_flush_gtt_write_domain(obj);

	/* If we're not in the cpu write domain, set ourself into the
	 * gtt write domain and manually flush cachelines (as required).
	 * This optimizes for the case when the gpu will use the data
	 * right away and we therefore have to clflush anyway.
	 */
	if (obj->base.write_domain != I915_GEM_DOMAIN_CPU)
		*needs_clflush |= cpu_write_needs_clflush(obj) << 1;

	/* Same trick applies to invalidate partially written cachelines read
	 * before writing.
	 */
	if (!(obj->base.read_domains & I915_GEM_DOMAIN_CPU))
		*needs_clflush |= !cpu_cache_is_coherent(obj->base.dev,
							 obj->cache_level);

	if (*needs_clflush && !static_cpu_has(X86_FEATURE_CLFLUSH)) {
		ret = i915_gem_object_set_to_cpu_domain(obj, true);
		if (ret)
			goto err_unpin;

		*needs_clflush = 0;
	}

	if ((*needs_clflush & CLFLUSH_AFTER) == 0)
		obj->cache_dirty = true;

	intel_fb_obj_invalidate(obj, ORIGIN_CPU);
	obj->dirty = 1;
	/* return with the pages pinned */
	return 0;

err_unpin:
	i915_gem_object_unpin_pages(obj);
	return ret;
}

/* Per-page copy function for the shmem pread fastpath.
 * Flushes invalid cachelines before reading the target if
 * needs_clflush is set. */
static int
shmem_pread_fast(struct page *page, int shmem_page_offset, int page_length,
		 char __user *user_data,
		 bool page_do_bit17_swizzling, bool needs_clflush)
{
	char *vaddr;
	int ret;

	if (unlikely(page_do_bit17_swizzling))
		return -EINVAL;

	vaddr = kmap_atomic(page);
	if (needs_clflush)
		drm_clflush_virt_range(vaddr + shmem_page_offset,
				       page_length);
	ret = __copy_to_user_inatomic(user_data,
				      vaddr + shmem_page_offset,
				      page_length);
	kunmap_atomic(vaddr);

	return ret ? -EFAULT : 0;
}

static void
shmem_clflush_swizzled_range(char *addr, unsigned long length,
			     bool swizzled)
{
	if (unlikely(swizzled)) {
		unsigned long start = (unsigned long) addr;
		unsigned long end = (unsigned long) addr + length;

		/* For swizzling simply ensure that we always flush both
		 * channels. Lame, but simple and it works. Swizzled
		 * pwrite/pread is far from a hotpath - current userspace
		 * doesn't use it at all. */
		start = round_down(start, 128);
		end = round_up(end, 128);

		drm_clflush_virt_range((void *)start, end - start);
	} else {
		drm_clflush_virt_range(addr, length);
	}

}

/* Only difference to the fast-path function is that this can handle bit17
 * and uses non-atomic copy and kmap functions. */
static int
shmem_pread_slow(struct page *page, int shmem_page_offset, int page_length,
		 char __user *user_data,
		 bool page_do_bit17_swizzling, bool needs_clflush)
{
	char *vaddr;
	int ret;

	vaddr = kmap(page);
	if (needs_clflush)
		shmem_clflush_swizzled_range(vaddr + shmem_page_offset,
					     page_length,
					     page_do_bit17_swizzling);

	if (page_do_bit17_swizzling)
		ret = __copy_to_user_swizzled(user_data,
					      vaddr, shmem_page_offset,
					      page_length);
	else
		ret = __copy_to_user(user_data,
				     vaddr + shmem_page_offset,
				     page_length);
	kunmap(page);

	return ret ? - EFAULT : 0;
}

static inline unsigned long
slow_user_access(struct io_mapping *mapping,
		 uint64_t page_base, int page_offset,
		 char __user *user_data,
		 unsigned long length, bool pwrite)
{
	void __iomem *ioaddr;
	void *vaddr;
	uint64_t unwritten;

	ioaddr = io_mapping_map_wc(mapping, page_base, PAGE_SIZE);
	/* We can use the cpu mem copy function because this is X86. */
	vaddr = (void __force *)ioaddr + page_offset;
	if (pwrite)
		unwritten = __copy_from_user(vaddr, user_data, length);
	else
		unwritten = __copy_to_user(user_data, vaddr, length);

	io_mapping_unmap(ioaddr);
	return unwritten;
}

static int
i915_gem_gtt_pread(struct drm_device *dev,
		   struct drm_i915_gem_object *obj, uint64_t size,
		   uint64_t data_offset, uint64_t data_ptr)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	struct i915_vma *vma;
	struct drm_mm_node node;
	char __user *user_data;
	uint64_t remain;
	uint64_t offset;
	int ret;

	intel_runtime_pm_get(to_i915(dev));
	vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0, PIN_MAPPABLE);
	if (!IS_ERR(vma)) {
		node.start = i915_ggtt_offset(vma);
		node.allocated = false;
		ret = i915_vma_put_fence(vma);
		if (ret) {
			i915_vma_unpin(vma);
			vma = ERR_PTR(ret);
		}
	}
	if (IS_ERR(vma)) {
		ret = insert_mappable_node(dev_priv, &node, PAGE_SIZE);
		if (ret)
			goto out;

		ret = i915_gem_object_get_pages(obj);
		if (ret) {
			remove_mappable_node(&node);
			goto out;
		}

		i915_gem_object_pin_pages(obj);
	}

	ret = i915_gem_object_set_to_gtt_domain(obj, false);
	if (ret)
		goto out_unpin;

	user_data = u64_to_user_ptr(data_ptr);
	remain = size;
	offset = data_offset;

	mutex_unlock(&dev->struct_mutex);
	if (likely(!i915.prefault_disable)) {
		ret = fault_in_pages_writeable(user_data, remain);
		if (ret) {
			mutex_lock(&dev->struct_mutex);
			goto out_unpin;
		}
	}

	while (remain > 0) {
		/* Operation in this page
		 *
		 * page_base = page offset within aperture
		 * page_offset = offset within page
		 * page_length = bytes to copy for this page
		 */
		u32 page_base = node.start;
		unsigned page_offset = offset_in_page(offset);
		unsigned page_length = PAGE_SIZE - page_offset;
		page_length = remain < page_length ? remain : page_length;
		if (node.allocated) {
			wmb();
			ggtt->base.insert_page(&ggtt->base,
					       i915_gem_object_get_dma_address(obj, offset >> PAGE_SHIFT),
					       node.start,
					       I915_CACHE_NONE, 0);
			wmb();
		} else {
			page_base += offset & PAGE_MASK;
		}
		/* This is a slow read/write as it tries to read from
		 * and write to user memory which may result into page
		 * faults, and so we cannot perform this under struct_mutex.
		 */
		if (slow_user_access(&ggtt->mappable, page_base,
				     page_offset, user_data,
				     page_length, false)) {
			ret = -EFAULT;
			break;
		}

		remain -= page_length;
		user_data += page_length;
		offset += page_length;
	}

	mutex_lock(&dev->struct_mutex);
	if (ret == 0 && (obj->base.read_domains & I915_GEM_DOMAIN_GTT) == 0) {
		/* The user has modified the object whilst we tried
		 * reading from it, and we now have no idea what domain
		 * the pages should be in. As we have just been touching
		 * them directly, flush everything back to the GTT
		 * domain.
		 */
		ret = i915_gem_object_set_to_gtt_domain(obj, false);
	}

out_unpin:
	if (node.allocated) {
		wmb();
		ggtt->base.clear_range(&ggtt->base,
				       node.start, node.size);
		i915_gem_object_unpin_pages(obj);
		remove_mappable_node(&node);
	} else {
		i915_vma_unpin(vma);
	}
out:
	intel_runtime_pm_put(to_i915(dev));
	return ret;
}

static int
i915_gem_shmem_pread(struct drm_device *dev,
		     struct drm_i915_gem_object *obj,
		     struct drm_i915_gem_pread *args,
		     struct drm_file *file)
{
	char __user *user_data;
	ssize_t remain;
	loff_t offset;
	int shmem_page_offset, page_length, ret = 0;
	int obj_do_bit17_swizzling, page_do_bit17_swizzling;
	int prefaulted = 0;
	int needs_clflush = 0;
	struct sg_page_iter sg_iter;

	ret = i915_gem_obj_prepare_shmem_read(obj, &needs_clflush);
	if (ret)
		return ret;

	obj_do_bit17_swizzling = i915_gem_object_needs_bit17_swizzle(obj);
	user_data = u64_to_user_ptr(args->data_ptr);
	offset = args->offset;
	remain = args->size;

	for_each_sg_page(obj->pages->sgl, &sg_iter, obj->pages->nents,
			 offset >> PAGE_SHIFT) {
		struct page *page = sg_page_iter_page(&sg_iter);

		if (remain <= 0)
			break;

		/* Operation in this page
		 *
		 * shmem_page_offset = offset within page in shmem file
		 * page_length = bytes to copy for this page
		 */
		shmem_page_offset = offset_in_page(offset);
		page_length = remain;
		if ((shmem_page_offset + page_length) > PAGE_SIZE)
			page_length = PAGE_SIZE - shmem_page_offset;

		page_do_bit17_swizzling = obj_do_bit17_swizzling &&
			(page_to_phys(page) & (1 << 17)) != 0;

		ret = shmem_pread_fast(page, shmem_page_offset, page_length,
				       user_data, page_do_bit17_swizzling,
				       needs_clflush);
		if (ret == 0)
			goto next_page;

		mutex_unlock(&dev->struct_mutex);

		if (likely(!i915.prefault_disable) && !prefaulted) {
			ret = fault_in_pages_writeable(user_data, remain);
			/* Userspace is tricking us, but we've already clobbered
			 * its pages with the prefault and promised to write the
			 * data up to the first fault. Hence ignore any errors
			 * and just continue. */
			(void)ret;
			prefaulted = 1;
		}

		ret = shmem_pread_slow(page, shmem_page_offset, page_length,
				       user_data, page_do_bit17_swizzling,
				       needs_clflush);

		mutex_lock(&dev->struct_mutex);

		if (ret)
			goto out;

next_page:
		remain -= page_length;
		user_data += page_length;
		offset += page_length;
	}

out:
	i915_gem_obj_finish_shmem_access(obj);

	return ret;
}

/**
 * Reads data from the object referenced by handle.
 * @dev: drm device pointer
 * @data: ioctl data blob
 * @file: drm file pointer
 *
 * On error, the contents of *data are undefined.
 */
int
i915_gem_pread_ioctl(struct drm_device *dev, void *data,
		     struct drm_file *file)
{
	struct drm_i915_gem_pread *args = data;
	struct drm_i915_gem_object *obj;
	int ret = 0;

	if (args->size == 0)
		return 0;

	if (!access_ok(VERIFY_WRITE,
		       u64_to_user_ptr(args->data_ptr),
		       args->size))
		return -EFAULT;

	obj = i915_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	/* Bounds check source.  */
	if (args->offset > obj->base.size ||
	    args->size > obj->base.size - args->offset) {
		ret = -EINVAL;
		goto err;
	}

	trace_i915_gem_object_pread(obj, args->offset, args->size);

	ret = __unsafe_wait_rendering(obj, to_rps_client(file), true);
	if (ret)
		goto err;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		goto err;

	ret = i915_gem_shmem_pread(dev, obj, args, file);

	/* pread for non shmem backed objects */
	if (ret == -EFAULT || ret == -ENODEV)
		ret = i915_gem_gtt_pread(dev, obj, args->size,
					args->offset, args->data_ptr);

	i915_gem_object_put(obj);
	mutex_unlock(&dev->struct_mutex);

	return ret;

err:
	i915_gem_object_put_unlocked(obj);
	return ret;
}

/* This is the fast write path which cannot handle
 * page faults in the source data
 */

static inline int
fast_user_write(struct io_mapping *mapping,
		loff_t page_base, int page_offset,
		char __user *user_data,
		int length)
{
	void __iomem *vaddr_atomic;
	void *vaddr;
	unsigned long unwritten;

	vaddr_atomic = io_mapping_map_atomic_wc(mapping, page_base);
	/* We can use the cpu mem copy function because this is X86. */
	vaddr = (void __force*)vaddr_atomic + page_offset;
	unwritten = __copy_from_user_inatomic_nocache(vaddr,
						      user_data, length);
	io_mapping_unmap_atomic(vaddr_atomic);
	return unwritten;
}

/**
 * This is the fast pwrite path, where we copy the data directly from the
 * user into the GTT, uncached.
 * @i915: i915 device private data
 * @obj: i915 gem object
 * @args: pwrite arguments structure
 * @file: drm file pointer
 */
static int
i915_gem_gtt_pwrite_fast(struct drm_i915_private *i915,
			 struct drm_i915_gem_object *obj,
			 struct drm_i915_gem_pwrite *args,
			 struct drm_file *file)
{
	struct i915_ggtt *ggtt = &i915->ggtt;
	struct drm_device *dev = obj->base.dev;
	struct i915_vma *vma;
	struct drm_mm_node node;
	uint64_t remain, offset;
	char __user *user_data;
	int ret;
	bool hit_slow_path = false;

	if (i915_gem_object_is_tiled(obj))
		return -EFAULT;

	intel_runtime_pm_get(i915);
	vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0,
				       PIN_MAPPABLE | PIN_NONBLOCK);
	if (!IS_ERR(vma)) {
		node.start = i915_ggtt_offset(vma);
		node.allocated = false;
		ret = i915_vma_put_fence(vma);
		if (ret) {
			i915_vma_unpin(vma);
			vma = ERR_PTR(ret);
		}
	}
	if (IS_ERR(vma)) {
		ret = insert_mappable_node(i915, &node, PAGE_SIZE);
		if (ret)
			goto out;

		ret = i915_gem_object_get_pages(obj);
		if (ret) {
			remove_mappable_node(&node);
			goto out;
		}

		i915_gem_object_pin_pages(obj);
	}

	ret = i915_gem_object_set_to_gtt_domain(obj, true);
	if (ret)
		goto out_unpin;

	intel_fb_obj_invalidate(obj, ORIGIN_CPU);
	obj->dirty = true;

	user_data = u64_to_user_ptr(args->data_ptr);
	offset = args->offset;
	remain = args->size;
	while (remain) {
		/* Operation in this page
		 *
		 * page_base = page offset within aperture
		 * page_offset = offset within page
		 * page_length = bytes to copy for this page
		 */
		u32 page_base = node.start;
		unsigned page_offset = offset_in_page(offset);
		unsigned page_length = PAGE_SIZE - page_offset;
		page_length = remain < page_length ? remain : page_length;
		if (node.allocated) {
			wmb(); /* flush the write before we modify the GGTT */
			ggtt->base.insert_page(&ggtt->base,
					       i915_gem_object_get_dma_address(obj, offset >> PAGE_SHIFT),
					       node.start, I915_CACHE_NONE, 0);
			wmb(); /* flush modifications to the GGTT (insert_page) */
		} else {
			page_base += offset & PAGE_MASK;
		}
		/* If we get a fault while copying data, then (presumably) our
		 * source page isn't available.  Return the error and we'll
		 * retry in the slow path.
		 * If the object is non-shmem backed, we retry again with the
		 * path that handles page fault.
		 */
		if (fast_user_write(&ggtt->mappable, page_base,
				    page_offset, user_data, page_length)) {
			hit_slow_path = true;
			mutex_unlock(&dev->struct_mutex);
			if (slow_user_access(&ggtt->mappable,
					     page_base,
					     page_offset, user_data,
					     page_length, true)) {
				ret = -EFAULT;
				mutex_lock(&dev->struct_mutex);
				goto out_flush;
			}

			mutex_lock(&dev->struct_mutex);
		}

		remain -= page_length;
		user_data += page_length;
		offset += page_length;
	}

out_flush:
	if (hit_slow_path) {
		if (ret == 0 &&
		    (obj->base.read_domains & I915_GEM_DOMAIN_GTT) == 0) {
			/* The user has modified the object whilst we tried
			 * reading from it, and we now have no idea what domain
			 * the pages should be in. As we have just been touching
			 * them directly, flush everything back to the GTT
			 * domain.
			 */
			ret = i915_gem_object_set_to_gtt_domain(obj, false);
		}
	}

	intel_fb_obj_flush(obj, false, ORIGIN_CPU);
out_unpin:
	if (node.allocated) {
		wmb();
		ggtt->base.clear_range(&ggtt->base,
				       node.start, node.size);
		i915_gem_object_unpin_pages(obj);
		remove_mappable_node(&node);
	} else {
		i915_vma_unpin(vma);
	}
out:
	intel_runtime_pm_put(i915);
	return ret;
}

/* Per-page copy function for the shmem pwrite fastpath.
 * Flushes invalid cachelines before writing to the target if
 * needs_clflush_before is set and flushes out any written cachelines after
 * writing if needs_clflush is set. */
static int
shmem_pwrite_fast(struct page *page, int shmem_page_offset, int page_length,
		  char __user *user_data,
		  bool page_do_bit17_swizzling,
		  bool needs_clflush_before,
		  bool needs_clflush_after)
{
	char *vaddr;
	int ret;

	if (unlikely(page_do_bit17_swizzling))
		return -EINVAL;

	vaddr = kmap_atomic(page);
	if (needs_clflush_before)
		drm_clflush_virt_range(vaddr + shmem_page_offset,
				       page_length);
	ret = __copy_from_user_inatomic(vaddr + shmem_page_offset,
					user_data, page_length);
	if (needs_clflush_after)
		drm_clflush_virt_range(vaddr + shmem_page_offset,
				       page_length);
	kunmap_atomic(vaddr);

	return ret ? -EFAULT : 0;
}

/* Only difference to the fast-path function is that this can handle bit17
 * and uses non-atomic copy and kmap functions. */
static int
shmem_pwrite_slow(struct page *page, int shmem_page_offset, int page_length,
		  char __user *user_data,
		  bool page_do_bit17_swizzling,
		  bool needs_clflush_before,
		  bool needs_clflush_after)
{
	char *vaddr;
	int ret;

	vaddr = kmap(page);
	if (unlikely(needs_clflush_before || page_do_bit17_swizzling))
		shmem_clflush_swizzled_range(vaddr + shmem_page_offset,
					     page_length,
					     page_do_bit17_swizzling);
	if (page_do_bit17_swizzling)
		ret = __copy_from_user_swizzled(vaddr, shmem_page_offset,
						user_data,
						page_length);
	else
		ret = __copy_from_user(vaddr + shmem_page_offset,
				       user_data,
				       page_length);
	if (needs_clflush_after)
		shmem_clflush_swizzled_range(vaddr + shmem_page_offset,
					     page_length,
					     page_do_bit17_swizzling);
	kunmap(page);

	return ret ? -EFAULT : 0;
}

static int
i915_gem_shmem_pwrite(struct drm_device *dev,
		      struct drm_i915_gem_object *obj,
		      struct drm_i915_gem_pwrite *args,
		      struct drm_file *file)
{
	ssize_t remain;
	loff_t offset;
	char __user *user_data;
	int shmem_page_offset, page_length, ret = 0;
	int obj_do_bit17_swizzling, page_do_bit17_swizzling;
	int hit_slowpath = 0;
	unsigned int needs_clflush;
	struct sg_page_iter sg_iter;

	ret = i915_gem_obj_prepare_shmem_write(obj, &needs_clflush);
	if (ret)
		return ret;

	obj_do_bit17_swizzling = i915_gem_object_needs_bit17_swizzle(obj);
	user_data = u64_to_user_ptr(args->data_ptr);
	offset = args->offset;
	remain = args->size;

	for_each_sg_page(obj->pages->sgl, &sg_iter, obj->pages->nents,
			 offset >> PAGE_SHIFT) {
		struct page *page = sg_page_iter_page(&sg_iter);
		int partial_cacheline_write;

		if (remain <= 0)
			break;

		/* Operation in this page
		 *
		 * shmem_page_offset = offset within page in shmem file
		 * page_length = bytes to copy for this page
		 */
		shmem_page_offset = offset_in_page(offset);

		page_length = remain;
		if ((shmem_page_offset + page_length) > PAGE_SIZE)
			page_length = PAGE_SIZE - shmem_page_offset;

		/* If we don't overwrite a cacheline completely we need to be
		 * careful to have up-to-date data by first clflushing. Don't
		 * overcomplicate things and flush the entire patch. */
		partial_cacheline_write = needs_clflush & CLFLUSH_BEFORE &&
			((shmem_page_offset | page_length)
				& (boot_cpu_data.x86_clflush_size - 1));

		page_do_bit17_swizzling = obj_do_bit17_swizzling &&
			(page_to_phys(page) & (1 << 17)) != 0;

		ret = shmem_pwrite_fast(page, shmem_page_offset, page_length,
					user_data, page_do_bit17_swizzling,
					partial_cacheline_write,
					needs_clflush & CLFLUSH_AFTER);
		if (ret == 0)
			goto next_page;

		hit_slowpath = 1;
		mutex_unlock(&dev->struct_mutex);
		ret = shmem_pwrite_slow(page, shmem_page_offset, page_length,
					user_data, page_do_bit17_swizzling,
					partial_cacheline_write,
					needs_clflush & CLFLUSH_AFTER);

		mutex_lock(&dev->struct_mutex);

		if (ret)
			goto out;

next_page:
		remain -= page_length;
		user_data += page_length;
		offset += page_length;
	}

out:
	i915_gem_obj_finish_shmem_access(obj);

	if (hit_slowpath) {
		/*
		 * Fixup: Flush cpu caches in case we didn't flush the dirty
		 * cachelines in-line while writing and the object moved
		 * out of the cpu write domain while we've dropped the lock.
		 */
		if (!(needs_clflush & CLFLUSH_AFTER) &&
		    obj->base.write_domain != I915_GEM_DOMAIN_CPU) {
			if (i915_gem_clflush_object(obj, obj->pin_display))
				needs_clflush |= CLFLUSH_AFTER;
		}
	}

	if (needs_clflush & CLFLUSH_AFTER)
		i915_gem_chipset_flush(to_i915(dev));

	intel_fb_obj_flush(obj, false, ORIGIN_CPU);
	return ret;
}

/**
 * Writes data to the object referenced by handle.
 * @dev: drm device
 * @data: ioctl data blob
 * @file: drm file
 *
 * On error, the contents of the buffer that were to be modified are undefined.
 */
int
i915_gem_pwrite_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_i915_gem_pwrite *args = data;
	struct drm_i915_gem_object *obj;
	int ret;

	if (args->size == 0)
		return 0;

	if (!access_ok(VERIFY_READ,
		       u64_to_user_ptr(args->data_ptr),
		       args->size))
		return -EFAULT;

	if (likely(!i915.prefault_disable)) {
		ret = fault_in_pages_readable(u64_to_user_ptr(args->data_ptr),
						   args->size);
		if (ret)
			return -EFAULT;
	}

	obj = i915_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	/* Bounds check destination. */
	if (args->offset > obj->base.size ||
	    args->size > obj->base.size - args->offset) {
		ret = -EINVAL;
		goto err;
	}

	trace_i915_gem_object_pwrite(obj, args->offset, args->size);

	ret = __unsafe_wait_rendering(obj, to_rps_client(file), false);
	if (ret)
		goto err;

	intel_runtime_pm_get(dev_priv);

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		goto err_rpm;

	ret = -EFAULT;
	/* We can only do the GTT pwrite on untiled buffers, as otherwise
	 * it would end up going through the fenced access, and we'll get
	 * different detiling behavior between reading and writing.
	 * pread/pwrite currently are reading and writing from the CPU
	 * perspective, requiring manual detiling by the client.
	 */
	if (!i915_gem_object_has_struct_page(obj) ||
	    cpu_write_needs_clflush(obj))
		/* Note that the gtt paths might fail with non-page-backed user
		 * pointers (e.g. gtt mappings when moving data between
		 * textures). Fallback to the shmem path in that case.
		 */
		ret = i915_gem_gtt_pwrite_fast(dev_priv, obj, args, file);

	if (ret == -EFAULT || ret == -ENOSPC) {
		if (obj->phys_handle)
			ret = i915_gem_phys_pwrite(obj, args, file);
		else
			ret = i915_gem_shmem_pwrite(dev, obj, args, file);
	}

	i915_gem_object_put(obj);
	mutex_unlock(&dev->struct_mutex);
	intel_runtime_pm_put(dev_priv);

	return ret;

err_rpm:
	intel_runtime_pm_put(dev_priv);
err:
	i915_gem_object_put_unlocked(obj);
	return ret;
}

static inline enum fb_op_origin
write_origin(struct drm_i915_gem_object *obj, unsigned domain)
{
	return (domain == I915_GEM_DOMAIN_GTT ?
		obj->frontbuffer_ggtt_origin : ORIGIN_CPU);
}

/**
 * Called when user space prepares to use an object with the CPU, either
 * through the mmap ioctl's mapping or a GTT mapping.
 * @dev: drm device
 * @data: ioctl data blob
 * @file: drm file
 */
int
i915_gem_set_domain_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file)
{
	struct drm_i915_gem_set_domain *args = data;
	struct drm_i915_gem_object *obj;
	uint32_t read_domains = args->read_domains;
	uint32_t write_domain = args->write_domain;
	int ret;

	/* Only handle setting domains to types used by the CPU. */
	if ((write_domain | read_domains) & I915_GEM_GPU_DOMAINS)
		return -EINVAL;

	/* Having something in the write domain implies it's in the read
	 * domain, and only that read domain.  Enforce that in the request.
	 */
	if (write_domain != 0 && read_domains != write_domain)
		return -EINVAL;

	obj = i915_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	/* Try to flush the object off the GPU without holding the lock.
	 * We will repeat the flush holding the lock in the normal manner
	 * to catch cases where we are gazumped.
	 */
	ret = __unsafe_wait_rendering(obj, to_rps_client(file), !write_domain);
	if (ret)
		goto err;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		goto err;

	if (read_domains & I915_GEM_DOMAIN_GTT)
		ret = i915_gem_object_set_to_gtt_domain(obj, write_domain != 0);
	else
		ret = i915_gem_object_set_to_cpu_domain(obj, write_domain != 0);

	if (write_domain != 0)
		intel_fb_obj_invalidate(obj, write_origin(obj, write_domain));

	i915_gem_object_put(obj);
	mutex_unlock(&dev->struct_mutex);
	return ret;

err:
	i915_gem_object_put_unlocked(obj);
	return ret;
}

/**
 * Called when user space has done writes to this buffer
 * @dev: drm device
 * @data: ioctl data blob
 * @file: drm file
 */
int
i915_gem_sw_finish_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file)
{
	struct drm_i915_gem_sw_finish *args = data;
	struct drm_i915_gem_object *obj;
	int err = 0;

	obj = i915_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	/* Pinned buffers may be scanout, so flush the cache */
	if (READ_ONCE(obj->pin_display)) {
		err = i915_mutex_lock_interruptible(dev);
		if (!err) {
			i915_gem_object_flush_cpu_write_domain(obj);
			mutex_unlock(&dev->struct_mutex);
		}
	}

	i915_gem_object_put_unlocked(obj);
	return err;
}

/**
 * i915_gem_mmap_ioctl - Maps the contents of an object, returning the address
 *			 it is mapped to.
 * @dev: drm device
 * @data: ioctl data blob
 * @file: drm file
 *
 * While the mapping holds a reference on the contents of the object, it doesn't
 * imply a ref on the object itself.
 *
 * IMPORTANT:
 *
 * DRM driver writers who look a this function as an example for how to do GEM
 * mmap support, please don't implement mmap support like here. The modern way
 * to implement DRM mmap support is with an mmap offset ioctl (like
 * i915_gem_mmap_gtt) and then using the mmap syscall on the DRM fd directly.
 * That way debug tooling like valgrind will understand what's going on, hiding
 * the mmap call in a driver private ioctl will break that. The i915 driver only
 * does cpu mmaps this way because we didn't know better.
 */
int
i915_gem_mmap_ioctl(struct drm_device *dev, void *data,
		    struct drm_file *file)
{
	struct drm_i915_gem_mmap *args = data;
	struct drm_i915_gem_object *obj;
	unsigned long addr;

	if (args->flags & ~(I915_MMAP_WC))
		return -EINVAL;

	if (args->flags & I915_MMAP_WC && !boot_cpu_has(X86_FEATURE_PAT))
		return -ENODEV;

	obj = i915_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	/* prime objects have no backing filp to GEM mmap
	 * pages from.
	 */
	if (!obj->base.filp) {
		i915_gem_object_put_unlocked(obj);
		return -EINVAL;
	}

	addr = vm_mmap(obj->base.filp, 0, args->size,
		       PROT_READ | PROT_WRITE, MAP_SHARED,
		       args->offset);
	if (args->flags & I915_MMAP_WC) {
		struct mm_struct *mm = current->mm;
		struct vm_area_struct *vma;

		if (down_write_killable(&mm->mmap_sem)) {
			i915_gem_object_put_unlocked(obj);
			return -EINTR;
		}
		vma = find_vma(mm, addr);
		if (vma)
			vma->vm_page_prot =
				pgprot_writecombine(vm_get_page_prot(vma->vm_flags));
		else
			addr = -ENOMEM;
		up_write(&mm->mmap_sem);

		/* This may race, but that's ok, it only gets set */
		WRITE_ONCE(obj->frontbuffer_ggtt_origin, ORIGIN_CPU);
	}
	i915_gem_object_put_unlocked(obj);
	if (IS_ERR((void *)addr))
		return addr;

	args->addr_ptr = (uint64_t) addr;

	return 0;
}

static unsigned int tile_row_pages(struct drm_i915_gem_object *obj)
{
	u64 size;

	size = i915_gem_object_get_stride(obj);
	size *= i915_gem_object_get_tiling(obj) == I915_TILING_Y ? 32 : 8;

	return size >> PAGE_SHIFT;
}

/**
 * i915_gem_mmap_gtt_version - report the current feature set for GTT mmaps
 *
 * A history of the GTT mmap interface:
 *
 * 0 - Everything had to fit into the GTT. Both parties of a memcpy had to
 *     aligned and suitable for fencing, and still fit into the available
 *     mappable space left by the pinned display objects. A classic problem
 *     we called the page-fault-of-doom where we would ping-pong between
 *     two objects that could not fit inside the GTT and so the memcpy
 *     would page one object in at the expense of the other between every
 *     single byte.
 *
 * 1 - Objects can be any size, and have any compatible fencing (X Y, or none
 *     as set via i915_gem_set_tiling() [DRM_I915_GEM_SET_TILING]). If the
 *     object is too large for the available space (or simply too large
 *     for the mappable aperture!), a view is created instead and faulted
 *     into userspace. (This view is aligned and sized appropriately for
 *     fenced access.)
 *
 * Restrictions:
 *
 *  * snoopable objects cannot be accessed via the GTT. It can cause machine
 *    hangs on some architectures, corruption on others. An attempt to service
 *    a GTT page fault from a snoopable object will generate a SIGBUS.
 *
 *  * the object must be able to fit into RAM (physical memory, though no
 *    limited to the mappable aperture).
 *
 *
 * Caveats:
 *
 *  * a new GTT page fault will synchronize rendering from the GPU and flush
 *    all data to system memory. Subsequent access will not be synchronized.
 *
 *  * all mappings are revoked on runtime device suspend.
 *
 *  * there are only 8, 16 or 32 fence registers to share between all users
 *    (older machines require fence register for display and blitter access
 *    as well). Contention of the fence registers will cause the previous users
 *    to be unmapped and any new access will generate new page faults.
 *
 *  * running out of memory while servicing a fault may generate a SIGBUS,
 *    rather than the expected SIGSEGV.
 */
int i915_gem_mmap_gtt_version(void)
{
	return 1;
}

/**
 * i915_gem_fault - fault a page into the GTT
 * @area: CPU VMA in question
 * @vmf: fault info
 *
 * The fault handler is set up by drm_gem_mmap() when a object is GTT mapped
 * from userspace.  The fault handler takes care of binding the object to
 * the GTT (if needed), allocating and programming a fence register (again,
 * only if needed based on whether the old reg is still valid or the object
 * is tiled) and inserting a new PTE into the faulting process.
 *
 * Note that the faulting process may involve evicting existing objects
 * from the GTT and/or fence registers to make room.  So performance may
 * suffer if the GTT working set is large or there are few fence registers
 * left.
 *
 * The current feature set supported by i915_gem_fault() and thus GTT mmaps
 * is exposed via I915_PARAM_MMAP_GTT_VERSION (see i915_gem_mmap_gtt_version).
 */
int i915_gem_fault(struct vm_area_struct *area, struct vm_fault *vmf)
{
#define MIN_CHUNK_PAGES ((1 << 20) >> PAGE_SHIFT) /* 1 MiB */
	struct drm_i915_gem_object *obj = to_intel_bo(area->vm_private_data);
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	bool write = !!(vmf->flags & FAULT_FLAG_WRITE);
	struct i915_vma *vma;
	pgoff_t page_offset;
	unsigned int flags;
	int ret;

	/* We don't use vmf->pgoff since that has the fake offset */
	page_offset = ((unsigned long)vmf->virtual_address - area->vm_start) >>
		PAGE_SHIFT;

	trace_i915_gem_object_fault(obj, page_offset, true, write);

	/* Try to flush the object off the GPU first without holding the lock.
	 * Upon acquiring the lock, we will perform our sanity checks and then
	 * repeat the flush holding the lock in the normal manner to catch cases
	 * where we are gazumped.
	 */
	ret = __unsafe_wait_rendering(obj, NULL, !write);
	if (ret)
		goto err;

	intel_runtime_pm_get(dev_priv);

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		goto err_rpm;

	/* Access to snoopable pages through the GTT is incoherent. */
	if (obj->cache_level != I915_CACHE_NONE && !HAS_LLC(dev)) {
		ret = -EFAULT;
		goto err_unlock;
	}

	/* If the object is smaller than a couple of partial vma, it is
	 * not worth only creating a single partial vma - we may as well
	 * clear enough space for the full object.
	 */
	flags = PIN_MAPPABLE;
	if (obj->base.size > 2 * MIN_CHUNK_PAGES << PAGE_SHIFT)
		flags |= PIN_NONBLOCK | PIN_NONFAULT;

	/* Now pin it into the GTT as needed */
	vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0, flags);
	if (IS_ERR(vma)) {
		struct i915_ggtt_view view;
		unsigned int chunk_size;

		/* Use a partial view if it is bigger than available space */
		chunk_size = MIN_CHUNK_PAGES;
		if (i915_gem_object_is_tiled(obj))
			chunk_size = max(chunk_size, tile_row_pages(obj));

		memset(&view, 0, sizeof(view));
		view.type = I915_GGTT_VIEW_PARTIAL;
		view.params.partial.offset = rounddown(page_offset, chunk_size);
		view.params.partial.size =
			min_t(unsigned int, chunk_size,
			      vma_pages(area) - view.params.partial.offset);

		/* If the partial covers the entire object, just create a
		 * normal VMA.
		 */
		if (chunk_size >= obj->base.size >> PAGE_SHIFT)
			view.type = I915_GGTT_VIEW_NORMAL;

		/* Userspace is now writing through an untracked VMA, abandon
		 * all hope that the hardware is able to track future writes.
		 */
		obj->frontbuffer_ggtt_origin = ORIGIN_CPU;

		vma = i915_gem_object_ggtt_pin(obj, &view, 0, 0, PIN_MAPPABLE);
	}
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto err_unlock;
	}

	ret = i915_gem_object_set_to_gtt_domain(obj, write);
	if (ret)
		goto err_unpin;

	ret = i915_vma_get_fence(vma);
	if (ret)
		goto err_unpin;

	/* Mark as being mmapped into userspace for later revocation */
	assert_rpm_wakelock_held(dev_priv);
	if (list_empty(&obj->userfault_link))
		list_add(&obj->userfault_link, &dev_priv->mm.userfault_list);

	/* Finally, remap it using the new GTT offset */
	ret = remap_io_mapping(area,
			       area->vm_start + (vma->ggtt_view.params.partial.offset << PAGE_SHIFT),
			       (ggtt->mappable_base + vma->node.start) >> PAGE_SHIFT,
			       min_t(u64, vma->size, area->vm_end - area->vm_start),
			       &ggtt->mappable);

err_unpin:
	__i915_vma_unpin(vma);
err_unlock:
	mutex_unlock(&dev->struct_mutex);
err_rpm:
	intel_runtime_pm_put(dev_priv);
err:
	switch (ret) {
	case -EIO:
		/*
		 * We eat errors when the gpu is terminally wedged to avoid
		 * userspace unduly crashing (gl has no provisions for mmaps to
		 * fail). But any other -EIO isn't ours (e.g. swap in failure)
		 * and so needs to be reported.
		 */
		if (!i915_terminally_wedged(&dev_priv->gpu_error)) {
			ret = VM_FAULT_SIGBUS;
			break;
		}
	case -EAGAIN:
		/*
		 * EAGAIN means the gpu is hung and we'll wait for the error
		 * handler to reset everything when re-faulting in
		 * i915_mutex_lock_interruptible.
		 */
	case 0:
	case -ERESTARTSYS:
	case -EINTR:
	case -EBUSY:
		/*
		 * EBUSY is ok: this just means that another thread
		 * already did the job.
		 */
		ret = VM_FAULT_NOPAGE;
		break;
	case -ENOMEM:
		ret = VM_FAULT_OOM;
		break;
	case -ENOSPC:
	case -EFAULT:
		ret = VM_FAULT_SIGBUS;
		break;
	default:
		WARN_ONCE(ret, "unhandled error in i915_gem_fault: %i\n", ret);
		ret = VM_FAULT_SIGBUS;
		break;
	}
	return ret;
}

/**
 * i915_gem_release_mmap - remove physical page mappings
 * @obj: obj in question
 *
 * Preserve the reservation of the mmapping with the DRM core code, but
 * relinquish ownership of the pages back to the system.
 *
 * It is vital that we remove the page mapping if we have mapped a tiled
 * object through the GTT and then lose the fence register due to
 * resource pressure. Similarly if the object has been moved out of the
 * aperture, than pages mapped into userspace must be revoked. Removing the
 * mapping will then trigger a page fault on the next user access, allowing
 * fixup by i915_gem_fault().
 */
void
i915_gem_release_mmap(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);

	/* Serialisation between user GTT access and our code depends upon
	 * revoking the CPU's PTE whilst the mutex is held. The next user
	 * pagefault then has to wait until we release the mutex.
	 *
	 * Note that RPM complicates somewhat by adding an additional
	 * requirement that operations to the GGTT be made holding the RPM
	 * wakeref.
	 */
	lockdep_assert_held(&i915->drm.struct_mutex);
	intel_runtime_pm_get(i915);

	if (list_empty(&obj->userfault_link))
		goto out;

	list_del_init(&obj->userfault_link);
	drm_vma_node_unmap(&obj->base.vma_node,
			   obj->base.dev->anon_inode->i_mapping);

	/* Ensure that the CPU's PTE are revoked and there are not outstanding
	 * memory transactions from userspace before we return. The TLB
	 * flushing implied above by changing the PTE above *should* be
	 * sufficient, an extra barrier here just provides us with a bit
	 * of paranoid documentation about our requirement to serialise
	 * memory writes before touching registers / GSM.
	 */
	wmb();

out:
	intel_runtime_pm_put(i915);
}

void i915_gem_runtime_suspend(struct drm_i915_private *dev_priv)
{
	struct drm_i915_gem_object *obj, *on;
	int i;

	/*
	 * Only called during RPM suspend. All users of the userfault_list
	 * must be holding an RPM wakeref to ensure that this can not
	 * run concurrently with themselves (and use the struct_mutex for
	 * protection between themselves).
	 */

	list_for_each_entry_safe(obj, on,
				 &dev_priv->mm.userfault_list, userfault_link) {
		list_del_init(&obj->userfault_link);
		drm_vma_node_unmap(&obj->base.vma_node,
				   obj->base.dev->anon_inode->i_mapping);
	}

	/* The fence will be lost when the device powers down. If any were
	 * in use by hardware (i.e. they are pinned), we should not be powering
	 * down! All other fences will be reacquired by the user upon waking.
	 */
	for (i = 0; i < dev_priv->num_fence_regs; i++) {
		struct drm_i915_fence_reg *reg = &dev_priv->fence_regs[i];

		if (WARN_ON(reg->pin_count))
			continue;

		if (!reg->vma)
			continue;

		GEM_BUG_ON(!list_empty(&reg->vma->obj->userfault_link));
		reg->dirty = true;
	}
}

/**
 * i915_gem_get_ggtt_size - return required global GTT size for an object
 * @dev_priv: i915 device
 * @size: object size
 * @tiling_mode: tiling mode
 *
 * Return the required global GTT size for an object, taking into account
 * potential fence register mapping.
 */
u64 i915_gem_get_ggtt_size(struct drm_i915_private *dev_priv,
			   u64 size, int tiling_mode)
{
	u64 ggtt_size;

	GEM_BUG_ON(size == 0);

	if (INTEL_GEN(dev_priv) >= 4 ||
	    tiling_mode == I915_TILING_NONE)
		return size;

	/* Previous chips need a power-of-two fence region when tiling */
	if (IS_GEN3(dev_priv))
		ggtt_size = 1024*1024;
	else
		ggtt_size = 512*1024;

	while (ggtt_size < size)
		ggtt_size <<= 1;

	return ggtt_size;
}

/**
 * i915_gem_get_ggtt_alignment - return required global GTT alignment
 * @dev_priv: i915 device
 * @size: object size
 * @tiling_mode: tiling mode
 * @fenced: is fenced alignment required or not
 *
 * Return the required global GTT alignment for an object, taking into account
 * potential fence register mapping.
 */
u64 i915_gem_get_ggtt_alignment(struct drm_i915_private *dev_priv, u64 size,
				int tiling_mode, bool fenced)
{
	GEM_BUG_ON(size == 0);

	/*
	 * Minimum alignment is 4k (GTT page size), but might be greater
	 * if a fence register is needed for the object.
	 */
	if (INTEL_GEN(dev_priv) >= 4 || (!fenced && IS_G33(dev_priv)) ||
	    tiling_mode == I915_TILING_NONE)
		return 4096;

	/*
	 * Previous chips need to be aligned to the size of the smallest
	 * fence register that can contain the object.
	 */
	return i915_gem_get_ggtt_size(dev_priv, size, tiling_mode);
}

static int i915_gem_object_create_mmap_offset(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);
	int err;

	err = drm_gem_create_mmap_offset(&obj->base);
	if (!err)
		return 0;

	/* We can idle the GPU locklessly to flush stale objects, but in order
	 * to claim that space for ourselves, we need to take the big
	 * struct_mutex to free the requests+objects and allocate our slot.
	 */
	err = i915_gem_wait_for_idle(dev_priv, I915_WAIT_INTERRUPTIBLE);
	if (err)
		return err;

	err = i915_mutex_lock_interruptible(&dev_priv->drm);
	if (!err) {
		i915_gem_retire_requests(dev_priv);
		err = drm_gem_create_mmap_offset(&obj->base);
		mutex_unlock(&dev_priv->drm.struct_mutex);
	}

	return err;
}

static void i915_gem_object_free_mmap_offset(struct drm_i915_gem_object *obj)
{
	drm_gem_free_mmap_offset(&obj->base);
}

int
i915_gem_mmap_gtt(struct drm_file *file,
		  struct drm_device *dev,
		  uint32_t handle,
		  uint64_t *offset)
{
	struct drm_i915_gem_object *obj;
	int ret;

	obj = i915_gem_object_lookup(file, handle);
	if (!obj)
		return -ENOENT;

	ret = i915_gem_object_create_mmap_offset(obj);
	if (ret == 0)
		*offset = drm_vma_node_offset_addr(&obj->base.vma_node);

	i915_gem_object_put_unlocked(obj);
	return ret;
}

/**
 * i915_gem_mmap_gtt_ioctl - prepare an object for GTT mmap'ing
 * @dev: DRM device
 * @data: GTT mapping ioctl data
 * @file: GEM object info
 *
 * Simply returns the fake offset to userspace so it can mmap it.
 * The mmap call will end up in drm_gem_mmap(), which will set things
 * up so we can get faults in the handler above.
 *
 * The fault handler will take care of binding the object into the GTT
 * (since it may have been evicted to make room for something), allocating
 * a fence register, and mapping the appropriate aperture address into
 * userspace.
 */
int
i915_gem_mmap_gtt_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file)
{
	struct drm_i915_gem_mmap_gtt *args = data;

	return i915_gem_mmap_gtt(file, dev, args->handle, &args->offset);
}

/* Immediately discard the backing storage */
static void
i915_gem_object_truncate(struct drm_i915_gem_object *obj)
{
	i915_gem_object_free_mmap_offset(obj);

	if (obj->base.filp == NULL)
		return;

	/* Our goal here is to return as much of the memory as
	 * is possible back to the system as we are called from OOM.
	 * To do this we must instruct the shmfs to drop all of its
	 * backing pages, *now*.
	 */
	shmem_truncate_range(file_inode(obj->base.filp), 0, (loff_t)-1);
	obj->madv = __I915_MADV_PURGED;
}

/* Try to discard unwanted pages */
static void
i915_gem_object_invalidate(struct drm_i915_gem_object *obj)
{
	struct address_space *mapping;

	switch (obj->madv) {
	case I915_MADV_DONTNEED:
		i915_gem_object_truncate(obj);
	case __I915_MADV_PURGED:
		return;
	}

	if (obj->base.filp == NULL)
		return;

	mapping = obj->base.filp->f_mapping,
	invalidate_mapping_pages(mapping, 0, (loff_t)-1);
}

static void
i915_gem_object_put_pages_gtt(struct drm_i915_gem_object *obj)
{
	struct sgt_iter sgt_iter;
	struct page *page;
	int ret;

	BUG_ON(obj->madv == __I915_MADV_PURGED);

	ret = i915_gem_object_set_to_cpu_domain(obj, true);
	if (WARN_ON(ret)) {
		/* In the event of a disaster, abandon all caches and
		 * hope for the best.
		 */
		i915_gem_clflush_object(obj, true);
		obj->base.read_domains = obj->base.write_domain = I915_GEM_DOMAIN_CPU;
	}

	i915_gem_gtt_finish_object(obj);

	if (i915_gem_object_needs_bit17_swizzle(obj))
		i915_gem_object_save_bit_17_swizzle(obj);

	if (obj->madv == I915_MADV_DONTNEED)
		obj->dirty = 0;

	for_each_sgt_page(page, sgt_iter, obj->pages) {
		if (obj->dirty)
			set_page_dirty(page);

		if (obj->madv == I915_MADV_WILLNEED)
			mark_page_accessed(page);

		put_page(page);
	}
	obj->dirty = 0;

	sg_free_table(obj->pages);
	kfree(obj->pages);
}

int
i915_gem_object_put_pages(struct drm_i915_gem_object *obj)
{
	const struct drm_i915_gem_object_ops *ops = obj->ops;

	if (obj->pages == NULL)
		return 0;

	if (obj->pages_pin_count)
		return -EBUSY;

	GEM_BUG_ON(obj->bind_count);

	/* ->put_pages might need to allocate memory for the bit17 swizzle
	 * array, hence protect them from being reaped by removing them from gtt
	 * lists early. */
	list_del(&obj->global_list);

	if (obj->mapping) {
		void *ptr;

		ptr = ptr_mask_bits(obj->mapping);
		if (is_vmalloc_addr(ptr))
			vunmap(ptr);
		else
			kunmap(kmap_to_page(ptr));

		obj->mapping = NULL;
	}

	ops->put_pages(obj);
	obj->pages = NULL;

	i915_gem_object_invalidate(obj);

	return 0;
}

static unsigned int swiotlb_max_size(void)
{
#if IS_ENABLED(CONFIG_SWIOTLB)
	return rounddown(swiotlb_nr_tbl() << IO_TLB_SHIFT, PAGE_SIZE);
#else
	return 0;
#endif
}

static int
i915_gem_object_get_pages_gtt(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);
	int page_count, i;
	struct address_space *mapping;
	struct sg_table *st;
	struct scatterlist *sg;
	struct sgt_iter sgt_iter;
	struct page *page;
	unsigned long last_pfn = 0;	/* suppress gcc warning */
	unsigned int max_segment;
	int ret;
	gfp_t gfp;

	/* Assert that the object is not currently in any GPU domain. As it
	 * wasn't in the GTT, there shouldn't be any way it could have been in
	 * a GPU cache
	 */
	BUG_ON(obj->base.read_domains & I915_GEM_GPU_DOMAINS);
	BUG_ON(obj->base.write_domain & I915_GEM_GPU_DOMAINS);

	max_segment = swiotlb_max_size();
	if (!max_segment)
		max_segment = rounddown(UINT_MAX, PAGE_SIZE);

	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (st == NULL)
		return -ENOMEM;

	page_count = obj->base.size / PAGE_SIZE;
	if (sg_alloc_table(st, page_count, GFP_KERNEL)) {
		kfree(st);
		return -ENOMEM;
	}

	/* Get the list of pages out of our struct file.  They'll be pinned
	 * at this point until we release them.
	 *
	 * Fail silently without starting the shrinker
	 */
	mapping = obj->base.filp->f_mapping;
	gfp = mapping_gfp_constraint(mapping, ~(__GFP_IO | __GFP_RECLAIM));
	gfp |= __GFP_NORETRY | __GFP_NOWARN;
	sg = st->sgl;
	st->nents = 0;
	for (i = 0; i < page_count; i++) {
		page = shmem_read_mapping_page_gfp(mapping, i, gfp);
		if (IS_ERR(page)) {
			i915_gem_shrink(dev_priv,
					page_count,
					I915_SHRINK_BOUND |
					I915_SHRINK_UNBOUND |
					I915_SHRINK_PURGEABLE);
			page = shmem_read_mapping_page_gfp(mapping, i, gfp);
		}
		if (IS_ERR(page)) {
			/* We've tried hard to allocate the memory by reaping
			 * our own buffer, now let the real VM do its job and
			 * go down in flames if truly OOM.
			 */
			page = shmem_read_mapping_page(mapping, i);
			if (IS_ERR(page)) {
				ret = PTR_ERR(page);
				goto err_pages;
			}
		}
		if (!i ||
		    sg->length >= max_segment ||
		    page_to_pfn(page) != last_pfn + 1) {
			if (i)
				sg = sg_next(sg);
			st->nents++;
			sg_set_page(sg, page, PAGE_SIZE, 0);
		} else {
			sg->length += PAGE_SIZE;
		}
		last_pfn = page_to_pfn(page);

		/* Check that the i965g/gm workaround works. */
		WARN_ON((gfp & __GFP_DMA32) && (last_pfn >= 0x00100000UL));
	}
	if (sg) /* loop terminated early; short sg table */
		sg_mark_end(sg);
	obj->pages = st;

	ret = i915_gem_gtt_prepare_object(obj);
	if (ret)
		goto err_pages;

	if (i915_gem_object_needs_bit17_swizzle(obj))
		i915_gem_object_do_bit_17_swizzle(obj);

	if (i915_gem_object_is_tiled(obj) &&
	    dev_priv->quirks & QUIRK_PIN_SWIZZLED_PAGES)
		i915_gem_object_pin_pages(obj);

	return 0;

err_pages:
	sg_mark_end(sg);
	for_each_sgt_page(page, sgt_iter, st)
		put_page(page);
	sg_free_table(st);
	kfree(st);

	/* shmemfs first checks if there is enough memory to allocate the page
	 * and reports ENOSPC should there be insufficient, along with the usual
	 * ENOMEM for a genuine allocation failure.
	 *
	 * We use ENOSPC in our driver to mean that we have run out of aperture
	 * space and so want to translate the error from shmemfs back to our
	 * usual understanding of ENOMEM.
	 */
	if (ret == -ENOSPC)
		ret = -ENOMEM;

	return ret;
}

/* Ensure that the associated pages are gathered from the backing storage
 * and pinned into our object. i915_gem_object_get_pages() may be called
 * multiple times before they are released by a single call to
 * i915_gem_object_put_pages() - once the pages are no longer referenced
 * either as a result of memory pressure (reaping pages under the shrinker)
 * or as the object is itself released.
 */
int
i915_gem_object_get_pages(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);
	const struct drm_i915_gem_object_ops *ops = obj->ops;
	int ret;

	if (obj->pages)
		return 0;

	if (obj->madv != I915_MADV_WILLNEED) {
		DRM_DEBUG("Attempting to obtain a purgeable object\n");
		return -EFAULT;
	}

	BUG_ON(obj->pages_pin_count);

	ret = ops->get_pages(obj);
	if (ret)
		return ret;

	list_add_tail(&obj->global_list, &dev_priv->mm.unbound_list);

	obj->get_page.sg = obj->pages->sgl;
	obj->get_page.last = 0;

	return 0;
}

/* The 'mapping' part of i915_gem_object_pin_map() below */
static void *i915_gem_object_map(const struct drm_i915_gem_object *obj,
				 enum i915_map_type type)
{
	unsigned long n_pages = obj->base.size >> PAGE_SHIFT;
	struct sg_table *sgt = obj->pages;
	struct sgt_iter sgt_iter;
	struct page *page;
	struct page *stack_pages[32];
	struct page **pages = stack_pages;
	unsigned long i = 0;
	pgprot_t pgprot;
	void *addr;

	/* A single page can always be kmapped */
	if (n_pages == 1 && type == I915_MAP_WB)
		return kmap(sg_page(sgt->sgl));

	if (n_pages > ARRAY_SIZE(stack_pages)) {
		/* Too big for stack -- allocate temporary array instead */
		pages = drm_malloc_gfp(n_pages, sizeof(*pages), GFP_TEMPORARY);
		if (!pages)
			return NULL;
	}

	for_each_sgt_page(page, sgt_iter, sgt)
		pages[i++] = page;

	/* Check that we have the expected number of pages */
	GEM_BUG_ON(i != n_pages);

	switch (type) {
	case I915_MAP_WB:
		pgprot = PAGE_KERNEL;
		break;
	case I915_MAP_WC:
		pgprot = pgprot_writecombine(PAGE_KERNEL_IO);
		break;
	}
	addr = vmap(pages, n_pages, 0, pgprot);

	if (pages != stack_pages)
		drm_free_large(pages);

	return addr;
}

/* get, pin, and map the pages of the object into kernel space */
void *i915_gem_object_pin_map(struct drm_i915_gem_object *obj,
			      enum i915_map_type type)
{
	enum i915_map_type has_type;
	bool pinned;
	void *ptr;
	int ret;

	lockdep_assert_held(&obj->base.dev->struct_mutex);
	GEM_BUG_ON(!i915_gem_object_has_struct_page(obj));

	ret = i915_gem_object_get_pages(obj);
	if (ret)
		return ERR_PTR(ret);

	i915_gem_object_pin_pages(obj);
	pinned = obj->pages_pin_count > 1;

	ptr = ptr_unpack_bits(obj->mapping, has_type);
	if (ptr && has_type != type) {
		if (pinned) {
			ret = -EBUSY;
			goto err;
		}

		if (is_vmalloc_addr(ptr))
			vunmap(ptr);
		else
			kunmap(kmap_to_page(ptr));

		ptr = obj->mapping = NULL;
	}

	if (!ptr) {
		ptr = i915_gem_object_map(obj, type);
		if (!ptr) {
			ret = -ENOMEM;
			goto err;
		}

		obj->mapping = ptr_pack_bits(ptr, type);
	}

	return ptr;

err:
	i915_gem_object_unpin_pages(obj);
	return ERR_PTR(ret);
}

static void
i915_gem_object_retire__write(struct i915_gem_active *active,
			      struct drm_i915_gem_request *request)
{
	struct drm_i915_gem_object *obj =
		container_of(active, struct drm_i915_gem_object, last_write);

	intel_fb_obj_flush(obj, true, ORIGIN_CS);
}

static void
i915_gem_object_retire__read(struct i915_gem_active *active,
			     struct drm_i915_gem_request *request)
{
	int idx = request->engine->id;
	struct drm_i915_gem_object *obj =
		container_of(active, struct drm_i915_gem_object, last_read[idx]);

	GEM_BUG_ON(!i915_gem_object_has_active_engine(obj, idx));

	i915_gem_object_clear_active(obj, idx);
	if (i915_gem_object_is_active(obj))
		return;

	/* Bump our place on the bound list to keep it roughly in LRU order
	 * so that we don't steal from recently used but inactive objects
	 * (unless we are forced to ofc!)
	 */
	if (obj->bind_count)
		list_move_tail(&obj->global_list,
			       &request->i915->mm.bound_list);

	i915_gem_object_put(obj);
}

static bool i915_context_is_banned(const struct i915_gem_context *ctx)
{
	unsigned long elapsed;

	if (ctx->hang_stats.banned)
		return true;

	elapsed = get_seconds() - ctx->hang_stats.guilty_ts;
	if (ctx->hang_stats.ban_period_seconds &&
	    elapsed <= ctx->hang_stats.ban_period_seconds) {
		DRM_DEBUG("context hanging too fast, banning!\n");
		return true;
	}

	return false;
}

static void i915_set_reset_status(struct i915_gem_context *ctx,
				  const bool guilty)
{
	struct i915_ctx_hang_stats *hs = &ctx->hang_stats;

	if (guilty) {
		hs->banned = i915_context_is_banned(ctx);
		hs->batch_active++;
		hs->guilty_ts = get_seconds();
	} else {
		hs->batch_pending++;
	}
}

struct drm_i915_gem_request *
i915_gem_find_active_request(struct intel_engine_cs *engine)
{
	struct drm_i915_gem_request *request;

	/* We are called by the error capture and reset at a random
	 * point in time. In particular, note that neither is crucially
	 * ordered with an interrupt. After a hang, the GPU is dead and we
	 * assume that no more writes can happen (we waited long enough for
	 * all writes that were in transaction to be flushed) - adding an
	 * extra delay for a recent interrupt is pointless. Hence, we do
	 * not need an engine->irq_seqno_barrier() before the seqno reads.
	 */
	list_for_each_entry(request, &engine->request_list, link) {
		if (i915_gem_request_completed(request))
			continue;

		if (!i915_sw_fence_done(&request->submit))
			break;

		return request;
	}

	return NULL;
}

static void reset_request(struct drm_i915_gem_request *request)
{
	void *vaddr = request->ring->vaddr;
	u32 head;

	/* As this request likely depends on state from the lost
	 * context, clear out all the user operations leaving the
	 * breadcrumb at the end (so we get the fence notifications).
	 */
	head = request->head;
	if (request->postfix < head) {
		memset(vaddr + head, 0, request->ring->size - head);
		head = 0;
	}
	memset(vaddr + head, 0, request->postfix - head);
}

static void i915_gem_reset_engine(struct intel_engine_cs *engine)
{
	struct drm_i915_gem_request *request;
	struct i915_gem_context *incomplete_ctx;
	bool ring_hung;

	if (engine->irq_seqno_barrier)
		engine->irq_seqno_barrier(engine);

	request = i915_gem_find_active_request(engine);
	if (!request)
		return;

	ring_hung = engine->hangcheck.score >= HANGCHECK_SCORE_RING_HUNG;
	if (engine->hangcheck.seqno != intel_engine_get_seqno(engine))
		ring_hung = false;

	i915_set_reset_status(request->ctx, ring_hung);
	if (!ring_hung)
		return;

	DRM_DEBUG_DRIVER("resetting %s to restart from tail of request 0x%x\n",
			 engine->name, request->fence.seqno);

	/* Setup the CS to resume from the breadcrumb of the hung request */
	engine->reset_hw(engine, request);

	/* Users of the default context do not rely on logical state
	 * preserved between batches. They have to emit full state on
	 * every batch and so it is safe to execute queued requests following
	 * the hang.
	 *
	 * Other contexts preserve state, now corrupt. We want to skip all
	 * queued requests that reference the corrupt context.
	 */
	incomplete_ctx = request->ctx;
	if (i915_gem_context_is_default(incomplete_ctx))
		return;

	list_for_each_entry_continue(request, &engine->request_list, link)
		if (request->ctx == incomplete_ctx)
			reset_request(request);
}

void i915_gem_reset(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	i915_gem_retire_requests(dev_priv);

	for_each_engine(engine, dev_priv, id)
		i915_gem_reset_engine(engine);

	i915_gem_restore_fences(&dev_priv->drm);

	if (dev_priv->gt.awake) {
		intel_sanitize_gt_powersave(dev_priv);
		intel_enable_gt_powersave(dev_priv);
		if (INTEL_GEN(dev_priv) >= 6)
			gen6_rps_busy(dev_priv);
	}
}

static void nop_submit_request(struct drm_i915_gem_request *request)
{
}

static void i915_gem_cleanup_engine(struct intel_engine_cs *engine)
{
	engine->submit_request = nop_submit_request;

	/* Mark all pending requests as complete so that any concurrent
	 * (lockless) lookup doesn't try and wait upon the request as we
	 * reset it.
	 */
	intel_engine_init_seqno(engine, engine->last_submitted_seqno);

	/*
	 * Clear the execlists queue up before freeing the requests, as those
	 * are the ones that keep the context and ringbuffer backing objects
	 * pinned in place.
	 */

	if (i915.enable_execlists) {
		spin_lock(&engine->execlist_lock);
		INIT_LIST_HEAD(&engine->execlist_queue);
		i915_gem_request_put(engine->execlist_port[0].request);
		i915_gem_request_put(engine->execlist_port[1].request);
		memset(engine->execlist_port, 0, sizeof(engine->execlist_port));
		spin_unlock(&engine->execlist_lock);
	}

	engine->i915->gt.active_engines &= ~intel_engine_flag(engine);
}

void i915_gem_set_wedged(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	lockdep_assert_held(&dev_priv->drm.struct_mutex);
	set_bit(I915_WEDGED, &dev_priv->gpu_error.flags);

	i915_gem_context_lost(dev_priv);
	for_each_engine(engine, dev_priv, id)
		i915_gem_cleanup_engine(engine);
	mod_delayed_work(dev_priv->wq, &dev_priv->gt.idle_work, 0);

	i915_gem_retire_requests(dev_priv);
}

static void
i915_gem_retire_work_handler(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, typeof(*dev_priv), gt.retire_work.work);
	struct drm_device *dev = &dev_priv->drm;

	/* Come back later if the device is busy... */
	if (mutex_trylock(&dev->struct_mutex)) {
		i915_gem_retire_requests(dev_priv);
		mutex_unlock(&dev->struct_mutex);
	}

	/* Keep the retire handler running until we are finally idle.
	 * We do not need to do this test under locking as in the worst-case
	 * we queue the retire worker once too often.
	 */
	if (READ_ONCE(dev_priv->gt.awake)) {
		i915_queue_hangcheck(dev_priv);
		queue_delayed_work(dev_priv->wq,
				   &dev_priv->gt.retire_work,
				   round_jiffies_up_relative(HZ));
	}
}

static void
i915_gem_idle_work_handler(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, typeof(*dev_priv), gt.idle_work.work);
	struct drm_device *dev = &dev_priv->drm;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	bool rearm_hangcheck;

	if (!READ_ONCE(dev_priv->gt.awake))
		return;

	if (READ_ONCE(dev_priv->gt.active_engines))
		return;

	rearm_hangcheck =
		cancel_delayed_work_sync(&dev_priv->gpu_error.hangcheck_work);

	if (!mutex_trylock(&dev->struct_mutex)) {
		/* Currently busy, come back later */
		mod_delayed_work(dev_priv->wq,
				 &dev_priv->gt.idle_work,
				 msecs_to_jiffies(50));
		goto out_rearm;
	}

	if (dev_priv->gt.active_engines)
		goto out_unlock;

	for_each_engine(engine, dev_priv, id)
		i915_gem_batch_pool_fini(&engine->batch_pool);

	GEM_BUG_ON(!dev_priv->gt.awake);
	dev_priv->gt.awake = false;
	rearm_hangcheck = false;

	if (INTEL_GEN(dev_priv) >= 6)
		gen6_rps_idle(dev_priv);
	intel_runtime_pm_put(dev_priv);
out_unlock:
	mutex_unlock(&dev->struct_mutex);

out_rearm:
	if (rearm_hangcheck) {
		GEM_BUG_ON(!dev_priv->gt.awake);
		i915_queue_hangcheck(dev_priv);
	}
}

void i915_gem_close_object(struct drm_gem_object *gem, struct drm_file *file)
{
	struct drm_i915_gem_object *obj = to_intel_bo(gem);
	struct drm_i915_file_private *fpriv = file->driver_priv;
	struct i915_vma *vma, *vn;

	mutex_lock(&obj->base.dev->struct_mutex);
	list_for_each_entry_safe(vma, vn, &obj->vma_list, obj_link)
		if (vma->vm->file == fpriv)
			i915_vma_close(vma);
	mutex_unlock(&obj->base.dev->struct_mutex);
}

/**
 * i915_gem_wait_ioctl - implements DRM_IOCTL_I915_GEM_WAIT
 * @dev: drm device pointer
 * @data: ioctl data blob
 * @file: drm file pointer
 *
 * Returns 0 if successful, else an error is returned with the remaining time in
 * the timeout parameter.
 *  -ETIME: object is still busy after timeout
 *  -ERESTARTSYS: signal interrupted the wait
 *  -ENONENT: object doesn't exist
 * Also possible, but rare:
 *  -EAGAIN: GPU wedged
 *  -ENOMEM: damn
 *  -ENODEV: Internal IRQ fail
 *  -E?: The add request failed
 *
 * The wait ioctl with a timeout of 0 reimplements the busy ioctl. With any
 * non-zero timeout parameter the wait ioctl will wait for the given number of
 * nanoseconds on an object becoming unbusy. Since the wait itself does so
 * without holding struct_mutex the object may become re-busied before this
 * function completes. A similar but shorter * race condition exists in the busy
 * ioctl
 */
int
i915_gem_wait_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_i915_gem_wait *args = data;
	struct intel_rps_client *rps = to_rps_client(file);
	struct drm_i915_gem_object *obj;
	unsigned long active;
	int idx, ret = 0;

	if (args->flags != 0)
		return -EINVAL;

	obj = i915_gem_object_lookup(file, args->bo_handle);
	if (!obj)
		return -ENOENT;

	active = __I915_BO_ACTIVE(obj);
	for_each_active(active, idx) {
		s64 *timeout = args->timeout_ns >= 0 ? &args->timeout_ns : NULL;
		ret = i915_gem_active_wait_unlocked(&obj->last_read[idx],
						    I915_WAIT_INTERRUPTIBLE,
						    timeout, rps);
		if (ret)
			break;
	}

	i915_gem_object_put_unlocked(obj);
	return ret;
}

static void __i915_vma_iounmap(struct i915_vma *vma)
{
	GEM_BUG_ON(i915_vma_is_pinned(vma));

	if (vma->iomap == NULL)
		return;

	io_mapping_unmap(vma->iomap);
	vma->iomap = NULL;
}

int i915_vma_unbind(struct i915_vma *vma)
{
	struct drm_i915_gem_object *obj = vma->obj;
	unsigned long active;
	int ret;

	/* First wait upon any activity as retiring the request may
	 * have side-effects such as unpinning or even unbinding this vma.
	 */
	active = i915_vma_get_active(vma);
	if (active) {
		int idx;

		/* When a closed VMA is retired, it is unbound - eek.
		 * In order to prevent it from being recursively closed,
		 * take a pin on the vma so that the second unbind is
		 * aborted.
		 */
		__i915_vma_pin(vma);

		for_each_active(active, idx) {
			ret = i915_gem_active_retire(&vma->last_read[idx],
						   &vma->vm->dev->struct_mutex);
			if (ret)
				break;
		}

		__i915_vma_unpin(vma);
		if (ret)
			return ret;

		GEM_BUG_ON(i915_vma_is_active(vma));
	}

	if (i915_vma_is_pinned(vma))
		return -EBUSY;

	if (!drm_mm_node_allocated(&vma->node))
		goto destroy;

	GEM_BUG_ON(obj->bind_count == 0);
	GEM_BUG_ON(!obj->pages);

	if (i915_vma_is_map_and_fenceable(vma)) {
		/* release the fence reg _after_ flushing */
		ret = i915_vma_put_fence(vma);
		if (ret)
			return ret;

		/* Force a pagefault for domain tracking on next user access */
		i915_gem_release_mmap(obj);

		__i915_vma_iounmap(vma);
		vma->flags &= ~I915_VMA_CAN_FENCE;
	}

	if (likely(!vma->vm->closed)) {
		trace_i915_vma_unbind(vma);
		vma->vm->unbind_vma(vma);
	}
	vma->flags &= ~(I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND);

	drm_mm_remove_node(&vma->node);
	list_move_tail(&vma->vm_link, &vma->vm->unbound_list);

	if (vma->pages != obj->pages) {
		GEM_BUG_ON(!vma->pages);
		sg_free_table(vma->pages);
		kfree(vma->pages);
	}
	vma->pages = NULL;

	/* Since the unbound list is global, only move to that list if
	 * no more VMAs exist. */
	if (--obj->bind_count == 0)
		list_move_tail(&obj->global_list,
			       &to_i915(obj->base.dev)->mm.unbound_list);

	/* And finally now the object is completely decoupled from this vma,
	 * we can drop its hold on the backing storage and allow it to be
	 * reaped by the shrinker.
	 */
	i915_gem_object_unpin_pages(obj);

destroy:
	if (unlikely(i915_vma_is_closed(vma)))
		i915_vma_destroy(vma);

	return 0;
}

int i915_gem_wait_for_idle(struct drm_i915_private *dev_priv,
			   unsigned int flags)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int ret;

	for_each_engine(engine, dev_priv, id) {
		if (engine->last_context == NULL)
			continue;

		ret = intel_engine_idle(engine, flags);
		if (ret)
			return ret;
	}

	return 0;
}

static bool i915_gem_valid_gtt_space(struct i915_vma *vma,
				     unsigned long cache_level)
{
	struct drm_mm_node *gtt_space = &vma->node;
	struct drm_mm_node *other;

	/*
	 * On some machines we have to be careful when putting differing types
	 * of snoopable memory together to avoid the prefetcher crossing memory
	 * domains and dying. During vm initialisation, we decide whether or not
	 * these constraints apply and set the drm_mm.color_adjust
	 * appropriately.
	 */
	if (vma->vm->mm.color_adjust == NULL)
		return true;

	if (!drm_mm_node_allocated(gtt_space))
		return true;

	if (list_empty(&gtt_space->node_list))
		return true;

	other = list_entry(gtt_space->node_list.prev, struct drm_mm_node, node_list);
	if (other->allocated && !other->hole_follows && other->color != cache_level)
		return false;

	other = list_entry(gtt_space->node_list.next, struct drm_mm_node, node_list);
	if (other->allocated && !gtt_space->hole_follows && other->color != cache_level)
		return false;

	return true;
}

/**
 * i915_vma_insert - finds a slot for the vma in its address space
 * @vma: the vma
 * @size: requested size in bytes (can be larger than the VMA)
 * @alignment: required alignment
 * @flags: mask of PIN_* flags to use
 *
 * First we try to allocate some free space that meets the requirements for
 * the VMA. Failiing that, if the flags permit, it will evict an old VMA,
 * preferrably the oldest idle entry to make room for the new VMA.
 *
 * Returns:
 * 0 on success, negative error code otherwise.
 */
static int
i915_vma_insert(struct i915_vma *vma, u64 size, u64 alignment, u64 flags)
{
	struct drm_i915_private *dev_priv = to_i915(vma->vm->dev);
	struct drm_i915_gem_object *obj = vma->obj;
	u64 start, end;
	int ret;

	GEM_BUG_ON(vma->flags & (I915_VMA_GLOBAL_BIND | I915_VMA_LOCAL_BIND));
	GEM_BUG_ON(drm_mm_node_allocated(&vma->node));

	size = max(size, vma->size);
	if (flags & PIN_MAPPABLE)
		size = i915_gem_get_ggtt_size(dev_priv, size,
					      i915_gem_object_get_tiling(obj));

	alignment = max(max(alignment, vma->display_alignment),
			i915_gem_get_ggtt_alignment(dev_priv, size,
						    i915_gem_object_get_tiling(obj),
						    flags & PIN_MAPPABLE));

	start = flags & PIN_OFFSET_BIAS ? flags & PIN_OFFSET_MASK : 0;

	end = vma->vm->total;
	if (flags & PIN_MAPPABLE)
		end = min_t(u64, end, dev_priv->ggtt.mappable_end);
	if (flags & PIN_ZONE_4G)
		end = min_t(u64, end, (1ULL << 32) - PAGE_SIZE);

	/* If binding the object/GGTT view requires more space than the entire
	 * aperture has, reject it early before evicting everything in a vain
	 * attempt to find space.
	 */
	if (size > end) {
		DRM_DEBUG("Attempting to bind an object larger than the aperture: request=%llu [object=%zd] > %s aperture=%llu\n",
			  size, obj->base.size,
			  flags & PIN_MAPPABLE ? "mappable" : "total",
			  end);
		return -E2BIG;
	}

	ret = i915_gem_object_get_pages(obj);
	if (ret)
		return ret;

	i915_gem_object_pin_pages(obj);

	if (flags & PIN_OFFSET_FIXED) {
		u64 offset = flags & PIN_OFFSET_MASK;
		if (offset & (alignment - 1) || offset > end - size) {
			ret = -EINVAL;
			goto err_unpin;
		}

		vma->node.start = offset;
		vma->node.size = size;
		vma->node.color = obj->cache_level;
		ret = drm_mm_reserve_node(&vma->vm->mm, &vma->node);
		if (ret) {
			ret = i915_gem_evict_for_vma(vma);
			if (ret == 0)
				ret = drm_mm_reserve_node(&vma->vm->mm, &vma->node);
			if (ret)
				goto err_unpin;
		}
	} else {
		u32 search_flag, alloc_flag;

		if (flags & PIN_HIGH) {
			search_flag = DRM_MM_SEARCH_BELOW;
			alloc_flag = DRM_MM_CREATE_TOP;
		} else {
			search_flag = DRM_MM_SEARCH_DEFAULT;
			alloc_flag = DRM_MM_CREATE_DEFAULT;
		}

		/* We only allocate in PAGE_SIZE/GTT_PAGE_SIZE (4096) chunks,
		 * so we know that we always have a minimum alignment of 4096.
		 * The drm_mm range manager is optimised to return results
		 * with zero alignment, so where possible use the optimal
		 * path.
		 */
		if (alignment <= 4096)
			alignment = 0;

search_free:
		ret = drm_mm_insert_node_in_range_generic(&vma->vm->mm,
							  &vma->node,
							  size, alignment,
							  obj->cache_level,
							  start, end,
							  search_flag,
							  alloc_flag);
		if (ret) {
			ret = i915_gem_evict_something(vma->vm, size, alignment,
						       obj->cache_level,
						       start, end,
						       flags);
			if (ret == 0)
				goto search_free;

			goto err_unpin;
		}

		GEM_BUG_ON(vma->node.start < start);
		GEM_BUG_ON(vma->node.start + vma->node.size > end);
	}
	GEM_BUG_ON(!i915_gem_valid_gtt_space(vma, obj->cache_level));

	list_move_tail(&obj->global_list, &dev_priv->mm.bound_list);
	list_move_tail(&vma->vm_link, &vma->vm->inactive_list);
	obj->bind_count++;

	return 0;

err_unpin:
	i915_gem_object_unpin_pages(obj);
	return ret;
}

bool
i915_gem_clflush_object(struct drm_i915_gem_object *obj,
			bool force)
{
	/* If we don't have a page list set up, then we're not pinned
	 * to GPU, and we can ignore the cache flush because it'll happen
	 * again at bind time.
	 */
	if (obj->pages == NULL)
		return false;

	/*
	 * Stolen memory is always coherent with the GPU as it is explicitly
	 * marked as wc by the system, or the system is cache-coherent.
	 */
	if (obj->stolen || obj->phys_handle)
		return false;

	/* If the GPU is snooping the contents of the CPU cache,
	 * we do not need to manually clear the CPU cache lines.  However,
	 * the caches are only snooped when the render cache is
	 * flushed/invalidated.  As we always have to emit invalidations
	 * and flushes when moving into and out of the RENDER domain, correct
	 * snooping behaviour occurs naturally as the result of our domain
	 * tracking.
	 */
	if (!force && cpu_cache_is_coherent(obj->base.dev, obj->cache_level)) {
		obj->cache_dirty = true;
		return false;
	}

	trace_i915_gem_object_clflush(obj);
	drm_clflush_sg(obj->pages);
	obj->cache_dirty = false;

	return true;
}

/** Flushes the GTT write domain for the object if it's dirty. */
static void
i915_gem_object_flush_gtt_write_domain(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);

	if (obj->base.write_domain != I915_GEM_DOMAIN_GTT)
		return;

	/* No actual flushing is required for the GTT write domain.  Writes
	 * to it "immediately" go to main memory as far as we know, so there's
	 * no chipset flush.  It also doesn't land in render cache.
	 *
	 * However, we do have to enforce the order so that all writes through
	 * the GTT land before any writes to the device, such as updates to
	 * the GATT itself.
	 *
	 * We also have to wait a bit for the writes to land from the GTT.
	 * An uncached read (i.e. mmio) seems to be ideal for the round-trip
	 * timing. This issue has only been observed when switching quickly
	 * between GTT writes and CPU reads from inside the kernel on recent hw,
	 * and it appears to only affect discrete GTT blocks (i.e. on LLC
	 * system agents we cannot reproduce this behaviour).
	 */
	wmb();
	if (INTEL_GEN(dev_priv) >= 6 && !HAS_LLC(dev_priv))
		POSTING_READ(RING_ACTHD(dev_priv->engine[RCS]->mmio_base));

	intel_fb_obj_flush(obj, false, write_origin(obj, I915_GEM_DOMAIN_GTT));

	obj->base.write_domain = 0;
	trace_i915_gem_object_change_domain(obj,
					    obj->base.read_domains,
					    I915_GEM_DOMAIN_GTT);
}

/** Flushes the CPU write domain for the object if it's dirty. */
static void
i915_gem_object_flush_cpu_write_domain(struct drm_i915_gem_object *obj)
{
	if (obj->base.write_domain != I915_GEM_DOMAIN_CPU)
		return;

	if (i915_gem_clflush_object(obj, obj->pin_display))
		i915_gem_chipset_flush(to_i915(obj->base.dev));

	intel_fb_obj_flush(obj, false, ORIGIN_CPU);

	obj->base.write_domain = 0;
	trace_i915_gem_object_change_domain(obj,
					    obj->base.read_domains,
					    I915_GEM_DOMAIN_CPU);
}

static void i915_gem_object_bump_inactive_ggtt(struct drm_i915_gem_object *obj)
{
	struct i915_vma *vma;

	list_for_each_entry(vma, &obj->vma_list, obj_link) {
		if (!i915_vma_is_ggtt(vma))
			continue;

		if (i915_vma_is_active(vma))
			continue;

		if (!drm_mm_node_allocated(&vma->node))
			continue;

		list_move_tail(&vma->vm_link, &vma->vm->inactive_list);
	}
}

/**
 * Moves a single object to the GTT read, and possibly write domain.
 * @obj: object to act on
 * @write: ask for write access or read only
 *
 * This function returns when the move is complete, including waiting on
 * flushes to occur.
 */
int
i915_gem_object_set_to_gtt_domain(struct drm_i915_gem_object *obj, bool write)
{
	uint32_t old_write_domain, old_read_domains;
	int ret;

	ret = i915_gem_object_wait_rendering(obj, !write);
	if (ret)
		return ret;

	if (obj->base.write_domain == I915_GEM_DOMAIN_GTT)
		return 0;

	/* Flush and acquire obj->pages so that we are coherent through
	 * direct access in memory with previous cached writes through
	 * shmemfs and that our cache domain tracking remains valid.
	 * For example, if the obj->filp was moved to swap without us
	 * being notified and releasing the pages, we would mistakenly
	 * continue to assume that the obj remained out of the CPU cached
	 * domain.
	 */
	ret = i915_gem_object_get_pages(obj);
	if (ret)
		return ret;

	i915_gem_object_flush_cpu_write_domain(obj);

	/* Serialise direct access to this object with the barriers for
	 * coherent writes from the GPU, by effectively invalidating the
	 * GTT domain upon first access.
	 */
	if ((obj->base.read_domains & I915_GEM_DOMAIN_GTT) == 0)
		mb();

	old_write_domain = obj->base.write_domain;
	old_read_domains = obj->base.read_domains;

	/* It should now be out of any other write domains, and we can update
	 * the domain values for our changes.
	 */
	BUG_ON((obj->base.write_domain & ~I915_GEM_DOMAIN_GTT) != 0);
	obj->base.read_domains |= I915_GEM_DOMAIN_GTT;
	if (write) {
		obj->base.read_domains = I915_GEM_DOMAIN_GTT;
		obj->base.write_domain = I915_GEM_DOMAIN_GTT;
		obj->dirty = 1;
	}

	trace_i915_gem_object_change_domain(obj,
					    old_read_domains,
					    old_write_domain);

	/* And bump the LRU for this access */
	i915_gem_object_bump_inactive_ggtt(obj);

	return 0;
}

/**
 * Changes the cache-level of an object across all VMA.
 * @obj: object to act on
 * @cache_level: new cache level to set for the object
 *
 * After this function returns, the object will be in the new cache-level
 * across all GTT and the contents of the backing storage will be coherent,
 * with respect to the new cache-level. In order to keep the backing storage
 * coherent for all users, we only allow a single cache level to be set
 * globally on the object and prevent it from being changed whilst the
 * hardware is reading from the object. That is if the object is currently
 * on the scanout it will be set to uncached (or equivalent display
 * cache coherency) and all non-MOCS GPU access will also be uncached so
 * that all direct access to the scanout remains coherent.
 */
int i915_gem_object_set_cache_level(struct drm_i915_gem_object *obj,
				    enum i915_cache_level cache_level)
{
	struct i915_vma *vma;
	int ret = 0;

	if (obj->cache_level == cache_level)
		goto out;

	/* Inspect the list of currently bound VMA and unbind any that would
	 * be invalid given the new cache-level. This is principally to
	 * catch the issue of the CS prefetch crossing page boundaries and
	 * reading an invalid PTE on older architectures.
	 */
restart:
	list_for_each_entry(vma, &obj->vma_list, obj_link) {
		if (!drm_mm_node_allocated(&vma->node))
			continue;

		if (i915_vma_is_pinned(vma)) {
			DRM_DEBUG("can not change the cache level of pinned objects\n");
			return -EBUSY;
		}

		if (i915_gem_valid_gtt_space(vma, cache_level))
			continue;

		ret = i915_vma_unbind(vma);
		if (ret)
			return ret;

		/* As unbinding may affect other elements in the
		 * obj->vma_list (due to side-effects from retiring
		 * an active vma), play safe and restart the iterator.
		 */
		goto restart;
	}

	/* We can reuse the existing drm_mm nodes but need to change the
	 * cache-level on the PTE. We could simply unbind them all and
	 * rebind with the correct cache-level on next use. However since
	 * we already have a valid slot, dma mapping, pages etc, we may as
	 * rewrite the PTE in the belief that doing so tramples upon less
	 * state and so involves less work.
	 */
	if (obj->bind_count) {
		/* Before we change the PTE, the GPU must not be accessing it.
		 * If we wait upon the object, we know that all the bound
		 * VMA are no longer active.
		 */
		ret = i915_gem_object_wait_rendering(obj, false);
		if (ret)
			return ret;

		if (!HAS_LLC(obj->base.dev) && cache_level != I915_CACHE_NONE) {
			/* Access to snoopable pages through the GTT is
			 * incoherent and on some machines causes a hard
			 * lockup. Relinquish the CPU mmaping to force
			 * userspace to refault in the pages and we can
			 * then double check if the GTT mapping is still
			 * valid for that pointer access.
			 */
			i915_gem_release_mmap(obj);

			/* As we no longer need a fence for GTT access,
			 * we can relinquish it now (and so prevent having
			 * to steal a fence from someone else on the next
			 * fence request). Note GPU activity would have
			 * dropped the fence as all snoopable access is
			 * supposed to be linear.
			 */
			list_for_each_entry(vma, &obj->vma_list, obj_link) {
				ret = i915_vma_put_fence(vma);
				if (ret)
					return ret;
			}
		} else {
			/* We either have incoherent backing store and
			 * so no GTT access or the architecture is fully
			 * coherent. In such cases, existing GTT mmaps
			 * ignore the cache bit in the PTE and we can
			 * rewrite it without confusing the GPU or having
			 * to force userspace to fault back in its mmaps.
			 */
		}

		list_for_each_entry(vma, &obj->vma_list, obj_link) {
			if (!drm_mm_node_allocated(&vma->node))
				continue;

			ret = i915_vma_bind(vma, cache_level, PIN_UPDATE);
			if (ret)
				return ret;
		}
	}

	list_for_each_entry(vma, &obj->vma_list, obj_link)
		vma->node.color = cache_level;
	obj->cache_level = cache_level;

out:
	/* Flush the dirty CPU caches to the backing storage so that the
	 * object is now coherent at its new cache level (with respect
	 * to the access domain).
	 */
	if (obj->cache_dirty && cpu_write_needs_clflush(obj)) {
		if (i915_gem_clflush_object(obj, true))
			i915_gem_chipset_flush(to_i915(obj->base.dev));
	}

	return 0;
}

int i915_gem_get_caching_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file)
{
	struct drm_i915_gem_caching *args = data;
	struct drm_i915_gem_object *obj;

	obj = i915_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	switch (obj->cache_level) {
	case I915_CACHE_LLC:
	case I915_CACHE_L3_LLC:
		args->caching = I915_CACHING_CACHED;
		break;

	case I915_CACHE_WT:
		args->caching = I915_CACHING_DISPLAY;
		break;

	default:
		args->caching = I915_CACHING_NONE;
		break;
	}

	i915_gem_object_put_unlocked(obj);
	return 0;
}

int i915_gem_set_caching_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file)
{
	struct drm_i915_private *i915 = to_i915(dev);
	struct drm_i915_gem_caching *args = data;
	struct drm_i915_gem_object *obj;
	enum i915_cache_level level;
	int ret;

	switch (args->caching) {
	case I915_CACHING_NONE:
		level = I915_CACHE_NONE;
		break;
	case I915_CACHING_CACHED:
		/*
		 * Due to a HW issue on BXT A stepping, GPU stores via a
		 * snooped mapping may leave stale data in a corresponding CPU
		 * cacheline, whereas normally such cachelines would get
		 * invalidated.
		 */
		if (!HAS_LLC(i915) && !HAS_SNOOP(i915))
			return -ENODEV;

		level = I915_CACHE_LLC;
		break;
	case I915_CACHING_DISPLAY:
		level = HAS_WT(i915) ? I915_CACHE_WT : I915_CACHE_NONE;
		break;
	default:
		return -EINVAL;
	}

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = i915_gem_object_lookup(file, args->handle);
	if (!obj) {
		ret = -ENOENT;
		goto unlock;
	}

	ret = i915_gem_object_set_cache_level(obj, level);
	i915_gem_object_put(obj);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

/*
 * Prepare buffer for display plane (scanout, cursors, etc).
 * Can be called from an uninterruptible phase (modesetting) and allows
 * any flushes to be pipelined (for pageflips).
 */
struct i915_vma *
i915_gem_object_pin_to_display_plane(struct drm_i915_gem_object *obj,
				     u32 alignment,
				     const struct i915_ggtt_view *view)
{
	struct i915_vma *vma;
	u32 old_read_domains, old_write_domain;
	int ret;

	/* Mark the pin_display early so that we account for the
	 * display coherency whilst setting up the cache domains.
	 */
	obj->pin_display++;

	/* The display engine is not coherent with the LLC cache on gen6.  As
	 * a result, we make sure that the pinning that is about to occur is
	 * done with uncached PTEs. This is lowest common denominator for all
	 * chipsets.
	 *
	 * However for gen6+, we could do better by using the GFDT bit instead
	 * of uncaching, which would allow us to flush all the LLC-cached data
	 * with that bit in the PTE to main memory with just one PIPE_CONTROL.
	 */
	ret = i915_gem_object_set_cache_level(obj,
					      HAS_WT(to_i915(obj->base.dev)) ?
					      I915_CACHE_WT : I915_CACHE_NONE);
	if (ret) {
		vma = ERR_PTR(ret);
		goto err_unpin_display;
	}

	/* As the user may map the buffer once pinned in the display plane
	 * (e.g. libkms for the bootup splash), we have to ensure that we
	 * always use map_and_fenceable for all scanout buffers. However,
	 * it may simply be too big to fit into mappable, in which case
	 * put it anyway and hope that userspace can cope (but always first
	 * try to preserve the existing ABI).
	 */
	vma = ERR_PTR(-ENOSPC);
	if (view->type == I915_GGTT_VIEW_NORMAL)
		vma = i915_gem_object_ggtt_pin(obj, view, 0, alignment,
					       PIN_MAPPABLE | PIN_NONBLOCK);
	if (IS_ERR(vma))
		vma = i915_gem_object_ggtt_pin(obj, view, 0, alignment, 0);
	if (IS_ERR(vma))
		goto err_unpin_display;

	vma->display_alignment = max_t(u64, vma->display_alignment, alignment);

	WARN_ON(obj->pin_display > i915_vma_pin_count(vma));

	i915_gem_object_flush_cpu_write_domain(obj);

	old_write_domain = obj->base.write_domain;
	old_read_domains = obj->base.read_domains;

	/* It should now be out of any other write domains, and we can update
	 * the domain values for our changes.
	 */
	obj->base.write_domain = 0;
	obj->base.read_domains |= I915_GEM_DOMAIN_GTT;

	trace_i915_gem_object_change_domain(obj,
					    old_read_domains,
					    old_write_domain);

	return vma;

err_unpin_display:
	obj->pin_display--;
	return vma;
}

void
i915_gem_object_unpin_from_display_plane(struct i915_vma *vma)
{
	if (WARN_ON(vma->obj->pin_display == 0))
		return;

	if (--vma->obj->pin_display == 0)
		vma->display_alignment = 0;

	/* Bump the LRU to try and avoid premature eviction whilst flipping  */
	if (!i915_vma_is_active(vma))
		list_move_tail(&vma->vm_link, &vma->vm->inactive_list);

	i915_vma_unpin(vma);
	WARN_ON(vma->obj->pin_display > i915_vma_pin_count(vma));
}

/**
 * Moves a single object to the CPU read, and possibly write domain.
 * @obj: object to act on
 * @write: requesting write or read-only access
 *
 * This function returns when the move is complete, including waiting on
 * flushes to occur.
 */
int
i915_gem_object_set_to_cpu_domain(struct drm_i915_gem_object *obj, bool write)
{
	uint32_t old_write_domain, old_read_domains;
	int ret;

	ret = i915_gem_object_wait_rendering(obj, !write);
	if (ret)
		return ret;

	if (obj->base.write_domain == I915_GEM_DOMAIN_CPU)
		return 0;

	i915_gem_object_flush_gtt_write_domain(obj);

	old_write_domain = obj->base.write_domain;
	old_read_domains = obj->base.read_domains;

	/* Flush the CPU cache if it's still invalid. */
	if ((obj->base.read_domains & I915_GEM_DOMAIN_CPU) == 0) {
		i915_gem_clflush_object(obj, false);

		obj->base.read_domains |= I915_GEM_DOMAIN_CPU;
	}

	/* It should now be out of any other write domains, and we can update
	 * the domain values for our changes.
	 */
	BUG_ON((obj->base.write_domain & ~I915_GEM_DOMAIN_CPU) != 0);

	/* If we're writing through the CPU, then the GPU read domains will
	 * need to be invalidated at next use.
	 */
	if (write) {
		obj->base.read_domains = I915_GEM_DOMAIN_CPU;
		obj->base.write_domain = I915_GEM_DOMAIN_CPU;
	}

	trace_i915_gem_object_change_domain(obj,
					    old_read_domains,
					    old_write_domain);

	return 0;
}

/* Throttle our rendering by waiting until the ring has completed our requests
 * emitted over 20 msec ago.
 *
 * Note that if we were to use the current jiffies each time around the loop,
 * we wouldn't escape the function with any frames outstanding if the time to
 * render a frame was over 20ms.
 *
 * This should get us reasonable parallelism between CPU and GPU but also
 * relatively low latency when blocking on a particular request to finish.
 */
static int
i915_gem_ring_throttle(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_i915_file_private *file_priv = file->driver_priv;
	unsigned long recent_enough = jiffies - DRM_I915_THROTTLE_JIFFIES;
	struct drm_i915_gem_request *request, *target = NULL;
	int ret;

	ret = i915_gem_wait_for_error(&dev_priv->gpu_error);
	if (ret)
		return ret;

	/* ABI: return -EIO if already wedged */
	if (i915_terminally_wedged(&dev_priv->gpu_error))
		return -EIO;

	spin_lock(&file_priv->mm.lock);
	list_for_each_entry(request, &file_priv->mm.request_list, client_list) {
		if (time_after_eq(request->emitted_jiffies, recent_enough))
			break;

		/*
		 * Note that the request might not have been submitted yet.
		 * In which case emitted_jiffies will be zero.
		 */
		if (!request->emitted_jiffies)
			continue;

		target = request;
	}
	if (target)
		i915_gem_request_get(target);
	spin_unlock(&file_priv->mm.lock);

	if (target == NULL)
		return 0;

	ret = i915_wait_request(target, I915_WAIT_INTERRUPTIBLE, NULL, NULL);
	i915_gem_request_put(target);

	return ret;
}

static bool
i915_vma_misplaced(struct i915_vma *vma, u64 size, u64 alignment, u64 flags)
{
	if (!drm_mm_node_allocated(&vma->node))
		return false;

	if (vma->node.size < size)
		return true;

	if (alignment && vma->node.start & (alignment - 1))
		return true;

	if (flags & PIN_MAPPABLE && !i915_vma_is_map_and_fenceable(vma))
		return true;

	if (flags & PIN_OFFSET_BIAS &&
	    vma->node.start < (flags & PIN_OFFSET_MASK))
		return true;

	if (flags & PIN_OFFSET_FIXED &&
	    vma->node.start != (flags & PIN_OFFSET_MASK))
		return true;

	return false;
}

void __i915_vma_set_map_and_fenceable(struct i915_vma *vma)
{
	struct drm_i915_gem_object *obj = vma->obj;
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);
	bool mappable, fenceable;
	u32 fence_size, fence_alignment;

	fence_size = i915_gem_get_ggtt_size(dev_priv,
					    vma->size,
					    i915_gem_object_get_tiling(obj));
	fence_alignment = i915_gem_get_ggtt_alignment(dev_priv,
						      vma->size,
						      i915_gem_object_get_tiling(obj),
						      true);

	fenceable = (vma->node.size == fence_size &&
		     (vma->node.start & (fence_alignment - 1)) == 0);

	mappable = (vma->node.start + fence_size <=
		    dev_priv->ggtt.mappable_end);

	if (mappable && fenceable)
		vma->flags |= I915_VMA_CAN_FENCE;
	else
		vma->flags &= ~I915_VMA_CAN_FENCE;
}

int __i915_vma_do_pin(struct i915_vma *vma,
		      u64 size, u64 alignment, u64 flags)
{
	unsigned int bound = vma->flags;
	int ret;

	GEM_BUG_ON((flags & (PIN_GLOBAL | PIN_USER)) == 0);
	GEM_BUG_ON((flags & PIN_GLOBAL) && !i915_vma_is_ggtt(vma));

	if (WARN_ON(bound & I915_VMA_PIN_OVERFLOW)) {
		ret = -EBUSY;
		goto err;
	}

	if ((bound & I915_VMA_BIND_MASK) == 0) {
		ret = i915_vma_insert(vma, size, alignment, flags);
		if (ret)
			goto err;
	}

	ret = i915_vma_bind(vma, vma->obj->cache_level, flags);
	if (ret)
		goto err;

	if ((bound ^ vma->flags) & I915_VMA_GLOBAL_BIND)
		__i915_vma_set_map_and_fenceable(vma);

	GEM_BUG_ON(i915_vma_misplaced(vma, size, alignment, flags));
	return 0;

err:
	__i915_vma_unpin(vma);
	return ret;
}

struct i915_vma *
i915_gem_object_ggtt_pin(struct drm_i915_gem_object *obj,
			 const struct i915_ggtt_view *view,
			 u64 size,
			 u64 alignment,
			 u64 flags)
{
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);
	struct i915_address_space *vm = &dev_priv->ggtt.base;
	struct i915_vma *vma;
	int ret;

	vma = i915_gem_obj_lookup_or_create_vma(obj, vm, view);
	if (IS_ERR(vma))
		return vma;

	if (i915_vma_misplaced(vma, size, alignment, flags)) {
		if (flags & PIN_NONBLOCK &&
		    (i915_vma_is_pinned(vma) || i915_vma_is_active(vma)))
			return ERR_PTR(-ENOSPC);

		if (flags & PIN_MAPPABLE) {
			u32 fence_size;

			fence_size = i915_gem_get_ggtt_size(dev_priv, vma->size,
							    i915_gem_object_get_tiling(obj));
			/* If the required space is larger than the available
			 * aperture, we will not able to find a slot for the
			 * object and unbinding the object now will be in
			 * vain. Worse, doing so may cause us to ping-pong
			 * the object in and out of the Global GTT and
			 * waste a lot of cycles under the mutex.
			 */
			if (fence_size > dev_priv->ggtt.mappable_end)
				return ERR_PTR(-E2BIG);

			/* If NONBLOCK is set the caller is optimistically
			 * trying to cache the full object within the mappable
			 * aperture, and *must* have a fallback in place for
			 * situations where we cannot bind the object. We
			 * can be a little more lax here and use the fallback
			 * more often to avoid costly migrations of ourselves
			 * and other objects within the aperture.
			 *
			 * Half-the-aperture is used as a simple heuristic.
			 * More interesting would to do search for a free
			 * block prior to making the commitment to unbind.
			 * That caters for the self-harm case, and with a
			 * little more heuristics (e.g. NOFAULT, NOEVICT)
			 * we could try to minimise harm to others.
			 */
			if (flags & PIN_NONBLOCK &&
			    fence_size > dev_priv->ggtt.mappable_end / 2)
				return ERR_PTR(-ENOSPC);
		}

		WARN(i915_vma_is_pinned(vma),
		     "bo is already pinned in ggtt with incorrect alignment:"
		     " offset=%08x, req.alignment=%llx,"
		     " req.map_and_fenceable=%d, vma->map_and_fenceable=%d\n",
		     i915_ggtt_offset(vma), alignment,
		     !!(flags & PIN_MAPPABLE),
		     i915_vma_is_map_and_fenceable(vma));
		ret = i915_vma_unbind(vma);
		if (ret)
			return ERR_PTR(ret);
	}

	ret = i915_vma_pin(vma, size, alignment, flags | PIN_GLOBAL);
	if (ret)
		return ERR_PTR(ret);

	return vma;
}

static __always_inline unsigned int __busy_read_flag(unsigned int id)
{
	/* Note that we could alias engines in the execbuf API, but
	 * that would be very unwise as it prevents userspace from
	 * fine control over engine selection. Ahem.
	 *
	 * This should be something like EXEC_MAX_ENGINE instead of
	 * I915_NUM_ENGINES.
	 */
	BUILD_BUG_ON(I915_NUM_ENGINES > 16);
	return 0x10000 << id;
}

static __always_inline unsigned int __busy_write_id(unsigned int id)
{
	/* The uABI guarantees an active writer is also amongst the read
	 * engines. This would be true if we accessed the activity tracking
	 * under the lock, but as we perform the lookup of the object and
	 * its activity locklessly we can not guarantee that the last_write
	 * being active implies that we have set the same engine flag from
	 * last_read - hence we always set both read and write busy for
	 * last_write.
	 */
	return id | __busy_read_flag(id);
}

static __always_inline unsigned int
__busy_set_if_active(const struct i915_gem_active *active,
		     unsigned int (*flag)(unsigned int id))
{
	struct drm_i915_gem_request *request;

	request = rcu_dereference(active->request);
	if (!request || i915_gem_request_completed(request))
		return 0;

	/* This is racy. See __i915_gem_active_get_rcu() for an in detail
	 * discussion of how to handle the race correctly, but for reporting
	 * the busy state we err on the side of potentially reporting the
	 * wrong engine as being busy (but we guarantee that the result
	 * is at least self-consistent).
	 *
	 * As we use SLAB_DESTROY_BY_RCU, the request may be reallocated
	 * whilst we are inspecting it, even under the RCU read lock as we are.
	 * This means that there is a small window for the engine and/or the
	 * seqno to have been overwritten. The seqno will always be in the
	 * future compared to the intended, and so we know that if that
	 * seqno is idle (on whatever engine) our request is idle and the
	 * return 0 above is correct.
	 *
	 * The issue is that if the engine is switched, it is just as likely
	 * to report that it is busy (but since the switch happened, we know
	 * the request should be idle). So there is a small chance that a busy
	 * result is actually the wrong engine.
	 *
	 * So why don't we care?
	 *
	 * For starters, the busy ioctl is a heuristic that is by definition
	 * racy. Even with perfect serialisation in the driver, the hardware
	 * state is constantly advancing - the state we report to the user
	 * is stale.
	 *
	 * The critical information for the busy-ioctl is whether the object
	 * is idle as userspace relies on that to detect whether its next
	 * access will stall, or if it has missed submitting commands to
	 * the hardware allowing the GPU to stall. We never generate a
	 * false-positive for idleness, thus busy-ioctl is reliable at the
	 * most fundamental level, and we maintain the guarantee that a
	 * busy object left to itself will eventually become idle (and stay
	 * idle!).
	 *
	 * We allow ourselves the leeway of potentially misreporting the busy
	 * state because that is an optimisation heuristic that is constantly
	 * in flux. Being quickly able to detect the busy/idle state is much
	 * more important than accurate logging of exactly which engines were
	 * busy.
	 *
	 * For accuracy in reporting the engine, we could use
	 *
	 *	result = 0;
	 *	request = __i915_gem_active_get_rcu(active);
	 *	if (request) {
	 *		if (!i915_gem_request_completed(request))
	 *			result = flag(request->engine->exec_id);
	 *		i915_gem_request_put(request);
	 *	}
	 *
	 * but that still remains susceptible to both hardware and userspace
	 * races. So we accept making the result of that race slightly worse,
	 * given the rarity of the race and its low impact on the result.
	 */
	return flag(READ_ONCE(request->engine->exec_id));
}

static __always_inline unsigned int
busy_check_reader(const struct i915_gem_active *active)
{
	return __busy_set_if_active(active, __busy_read_flag);
}

static __always_inline unsigned int
busy_check_writer(const struct i915_gem_active *active)
{
	return __busy_set_if_active(active, __busy_write_id);
}

int
i915_gem_busy_ioctl(struct drm_device *dev, void *data,
		    struct drm_file *file)
{
	struct drm_i915_gem_busy *args = data;
	struct drm_i915_gem_object *obj;
	unsigned long active;

	obj = i915_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	args->busy = 0;
	active = __I915_BO_ACTIVE(obj);
	if (active) {
		int idx;

		/* Yes, the lookups are intentionally racy.
		 *
		 * First, we cannot simply rely on __I915_BO_ACTIVE. We have
		 * to regard the value as stale and as our ABI guarantees
		 * forward progress, we confirm the status of each active
		 * request with the hardware.
		 *
		 * Even though we guard the pointer lookup by RCU, that only
		 * guarantees that the pointer and its contents remain
		 * dereferencable and does *not* mean that the request we
		 * have is the same as the one being tracked by the object.
		 *
		 * Consider that we lookup the request just as it is being
		 * retired and freed. We take a local copy of the pointer,
		 * but before we add its engine into the busy set, the other
		 * thread reallocates it and assigns it to a task on another
		 * engine with a fresh and incomplete seqno. Guarding against
		 * that requires careful serialisation and reference counting,
		 * i.e. using __i915_gem_active_get_request_rcu(). We don't,
		 * instead we expect that if the result is busy, which engines
		 * are busy is not completely reliable - we only guarantee
		 * that the object was busy.
		 */
		rcu_read_lock();

		for_each_active(active, idx)
			args->busy |= busy_check_reader(&obj->last_read[idx]);

		/* For ABI sanity, we only care that the write engine is in
		 * the set of read engines. This should be ensured by the
		 * ordering of setting last_read/last_write in
		 * i915_vma_move_to_active(), and then in reverse in retire.
		 * However, for good measure, we always report the last_write
		 * request as a busy read as well as being a busy write.
		 *
		 * We don't care that the set of active read/write engines
		 * may change during construction of the result, as it is
		 * equally liable to change before userspace can inspect
		 * the result.
		 */
		args->busy |= busy_check_writer(&obj->last_write);

		rcu_read_unlock();
	}

	i915_gem_object_put_unlocked(obj);
	return 0;
}

int
i915_gem_throttle_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	return i915_gem_ring_throttle(dev, file_priv);
}

int
i915_gem_madvise_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_i915_gem_madvise *args = data;
	struct drm_i915_gem_object *obj;
	int ret;

	switch (args->madv) {
	case I915_MADV_DONTNEED:
	case I915_MADV_WILLNEED:
	    break;
	default:
	    return -EINVAL;
	}

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = i915_gem_object_lookup(file_priv, args->handle);
	if (!obj) {
		ret = -ENOENT;
		goto unlock;
	}

	if (obj->pages &&
	    i915_gem_object_is_tiled(obj) &&
	    dev_priv->quirks & QUIRK_PIN_SWIZZLED_PAGES) {
		if (obj->madv == I915_MADV_WILLNEED)
			i915_gem_object_unpin_pages(obj);
		if (args->madv == I915_MADV_WILLNEED)
			i915_gem_object_pin_pages(obj);
	}

	if (obj->madv != __I915_MADV_PURGED)
		obj->madv = args->madv;

	/* if the object is no longer attached, discard its backing storage */
	if (obj->madv == I915_MADV_DONTNEED && obj->pages == NULL)
		i915_gem_object_truncate(obj);

	args->retained = obj->madv != __I915_MADV_PURGED;

	i915_gem_object_put(obj);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

void i915_gem_object_init(struct drm_i915_gem_object *obj,
			  const struct drm_i915_gem_object_ops *ops)
{
	int i;

	INIT_LIST_HEAD(&obj->global_list);
	INIT_LIST_HEAD(&obj->userfault_link);
	for (i = 0; i < I915_NUM_ENGINES; i++)
		init_request_active(&obj->last_read[i],
				    i915_gem_object_retire__read);
	init_request_active(&obj->last_write,
			    i915_gem_object_retire__write);
	INIT_LIST_HEAD(&obj->obj_exec_link);
	INIT_LIST_HEAD(&obj->vma_list);
	INIT_LIST_HEAD(&obj->batch_pool_link);

	obj->ops = ops;

	obj->frontbuffer_ggtt_origin = ORIGIN_GTT;
	obj->madv = I915_MADV_WILLNEED;

	i915_gem_info_add_obj(to_i915(obj->base.dev), obj->base.size);
}

static const struct drm_i915_gem_object_ops i915_gem_object_ops = {
	.flags = I915_GEM_OBJECT_HAS_STRUCT_PAGE,
	.get_pages = i915_gem_object_get_pages_gtt,
	.put_pages = i915_gem_object_put_pages_gtt,
};

/* Note we don't consider signbits :| */
#define overflows_type(x, T) \
	(sizeof(x) > sizeof(T) && (x) >> (sizeof(T) * BITS_PER_BYTE))

struct drm_i915_gem_object *
i915_gem_object_create(struct drm_device *dev, u64 size)
{
	struct drm_i915_gem_object *obj;
	struct address_space *mapping;
	gfp_t mask;
	int ret;

	/* There is a prevalence of the assumption that we fit the object's
	 * page count inside a 32bit _signed_ variable. Let's document this and
	 * catch if we ever need to fix it. In the meantime, if you do spot
	 * such a local variable, please consider fixing!
	 */
	if (WARN_ON(size >> PAGE_SHIFT > INT_MAX))
		return ERR_PTR(-E2BIG);

	if (overflows_type(size, obj->base.size))
		return ERR_PTR(-E2BIG);

	obj = i915_gem_object_alloc(dev);
	if (obj == NULL)
		return ERR_PTR(-ENOMEM);

	ret = drm_gem_object_init(dev, &obj->base, size);
	if (ret)
		goto fail;

	mask = GFP_HIGHUSER | __GFP_RECLAIMABLE;
	if (IS_CRESTLINE(dev) || IS_BROADWATER(dev)) {
		/* 965gm cannot relocate objects above 4GiB. */
		mask &= ~__GFP_HIGHMEM;
		mask |= __GFP_DMA32;
	}

	mapping = obj->base.filp->f_mapping;
	mapping_set_gfp_mask(mapping, mask);

	i915_gem_object_init(obj, &i915_gem_object_ops);

	obj->base.write_domain = I915_GEM_DOMAIN_CPU;
	obj->base.read_domains = I915_GEM_DOMAIN_CPU;

	if (HAS_LLC(dev)) {
		/* On some devices, we can have the GPU use the LLC (the CPU
		 * cache) for about a 10% performance improvement
		 * compared to uncached.  Graphics requests other than
		 * display scanout are coherent with the CPU in
		 * accessing this cache.  This means in this mode we
		 * don't need to clflush on the CPU side, and on the
		 * GPU side we only need to flush internal caches to
		 * get data visible to the CPU.
		 *
		 * However, we maintain the display planes as UC, and so
		 * need to rebind when first used as such.
		 */
		obj->cache_level = I915_CACHE_LLC;
	} else
		obj->cache_level = I915_CACHE_NONE;

	trace_i915_gem_object_create(obj);

	return obj;

fail:
	i915_gem_object_free(obj);

	return ERR_PTR(ret);
}

static bool discard_backing_storage(struct drm_i915_gem_object *obj)
{
	/* If we are the last user of the backing storage (be it shmemfs
	 * pages or stolen etc), we know that the pages are going to be
	 * immediately released. In this case, we can then skip copying
	 * back the contents from the GPU.
	 */

	if (obj->madv != I915_MADV_WILLNEED)
		return false;

	if (obj->base.filp == NULL)
		return true;

	/* At first glance, this looks racy, but then again so would be
	 * userspace racing mmap against close. However, the first external
	 * reference to the filp can only be obtained through the
	 * i915_gem_mmap_ioctl() which safeguards us against the user
	 * acquiring such a reference whilst we are in the middle of
	 * freeing the object.
	 */
	return atomic_long_read(&obj->base.filp->f_count) == 1;
}

void i915_gem_free_object(struct drm_gem_object *gem_obj)
{
	struct drm_i915_gem_object *obj = to_intel_bo(gem_obj);
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct i915_vma *vma, *next;

	intel_runtime_pm_get(dev_priv);

	trace_i915_gem_object_destroy(obj);

	/* All file-owned VMA should have been released by this point through
	 * i915_gem_close_object(), or earlier by i915_gem_context_close().
	 * However, the object may also be bound into the global GTT (e.g.
	 * older GPUs without per-process support, or for direct access through
	 * the GTT either for the user or for scanout). Those VMA still need to
	 * unbound now.
	 */
	list_for_each_entry_safe(vma, next, &obj->vma_list, obj_link) {
		GEM_BUG_ON(!i915_vma_is_ggtt(vma));
		GEM_BUG_ON(i915_vma_is_active(vma));
		vma->flags &= ~I915_VMA_PIN_MASK;
		i915_vma_close(vma);
	}
	GEM_BUG_ON(obj->bind_count);

	/* Stolen objects don't hold a ref, but do hold pin count. Fix that up
	 * before progressing. */
	if (obj->stolen)
		i915_gem_object_unpin_pages(obj);

	WARN_ON(atomic_read(&obj->frontbuffer_bits));

	if (obj->pages && obj->madv == I915_MADV_WILLNEED &&
	    dev_priv->quirks & QUIRK_PIN_SWIZZLED_PAGES &&
	    i915_gem_object_is_tiled(obj))
		i915_gem_object_unpin_pages(obj);

	if (WARN_ON(obj->pages_pin_count))
		obj->pages_pin_count = 0;
	if (discard_backing_storage(obj))
		obj->madv = I915_MADV_DONTNEED;
	i915_gem_object_put_pages(obj);

	BUG_ON(obj->pages);

	if (obj->base.import_attach)
		drm_prime_gem_destroy(&obj->base, NULL);

	if (obj->ops->release)
		obj->ops->release(obj);

	drm_gem_object_release(&obj->base);
	i915_gem_info_remove_obj(dev_priv, obj->base.size);

	kfree(obj->bit_17);
	i915_gem_object_free(obj);

	intel_runtime_pm_put(dev_priv);
}

int i915_gem_suspend(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	int ret;

	intel_suspend_gt_powersave(dev_priv);

	mutex_lock(&dev->struct_mutex);

	/* We have to flush all the executing contexts to main memory so
	 * that they can saved in the hibernation image. To ensure the last
	 * context image is coherent, we have to switch away from it. That
	 * leaves the dev_priv->kernel_context still active when
	 * we actually suspend, and its image in memory may not match the GPU
	 * state. Fortunately, the kernel_context is disposable and we do
	 * not rely on its state.
	 */
	ret = i915_gem_switch_to_kernel_context(dev_priv);
	if (ret)
		goto err;

	ret = i915_gem_wait_for_idle(dev_priv,
				     I915_WAIT_INTERRUPTIBLE |
				     I915_WAIT_LOCKED);
	if (ret)
		goto err;

	i915_gem_retire_requests(dev_priv);

	i915_gem_context_lost(dev_priv);
	mutex_unlock(&dev->struct_mutex);

	cancel_delayed_work_sync(&dev_priv->gpu_error.hangcheck_work);
	cancel_delayed_work_sync(&dev_priv->gt.retire_work);
	flush_delayed_work(&dev_priv->gt.idle_work);

	/* Assert that we sucessfully flushed all the work and
	 * reset the GPU back to its idle, low power state.
	 */
	WARN_ON(dev_priv->gt.awake);

	/*
	 * Neither the BIOS, ourselves or any other kernel
	 * expects the system to be in execlists mode on startup,
	 * so we need to reset the GPU back to legacy mode. And the only
	 * known way to disable logical contexts is through a GPU reset.
	 *
	 * So in order to leave the system in a known default configuration,
	 * always reset the GPU upon unload and suspend. Afterwards we then
	 * clean up the GEM state tracking, flushing off the requests and
	 * leaving the system in a known idle state.
	 *
	 * Note that is of the upmost importance that the GPU is idle and
	 * all stray writes are flushed *before* we dismantle the backing
	 * storage for the pinned objects.
	 *
	 * However, since we are uncertain that resetting the GPU on older
	 * machines is a good idea, we don't - just in case it leaves the
	 * machine in an unusable condition.
	 */
	if (HAS_HW_CONTEXTS(dev)) {
		int reset = intel_gpu_reset(dev_priv, ALL_ENGINES);
		WARN_ON(reset && reset != -ENODEV);
	}

	return 0;

err:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

void i915_gem_resume(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	mutex_lock(&dev->struct_mutex);
	i915_gem_restore_gtt_mappings(dev);

	/* As we didn't flush the kernel context before suspend, we cannot
	 * guarantee that the context image is complete. So let's just reset
	 * it and start again.
	 */
	dev_priv->gt.resume(dev_priv);

	mutex_unlock(&dev->struct_mutex);
}

void i915_gem_init_swizzling(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	if (INTEL_INFO(dev)->gen < 5 ||
	    dev_priv->mm.bit_6_swizzle_x == I915_BIT_6_SWIZZLE_NONE)
		return;

	I915_WRITE(DISP_ARB_CTL, I915_READ(DISP_ARB_CTL) |
				 DISP_TILE_SURFACE_SWIZZLING);

	if (IS_GEN5(dev_priv))
		return;

	I915_WRITE(TILECTL, I915_READ(TILECTL) | TILECTL_SWZCTL);
	if (IS_GEN6(dev_priv))
		I915_WRITE(ARB_MODE, _MASKED_BIT_ENABLE(ARB_MODE_SWIZZLE_SNB));
	else if (IS_GEN7(dev_priv))
		I915_WRITE(ARB_MODE, _MASKED_BIT_ENABLE(ARB_MODE_SWIZZLE_IVB));
	else if (IS_GEN8(dev_priv))
		I915_WRITE(GAMTARBMODE, _MASKED_BIT_ENABLE(ARB_MODE_SWIZZLE_BDW));
	else
		BUG();
}

static void init_unused_ring(struct drm_i915_private *dev_priv, u32 base)
{
	I915_WRITE(RING_CTL(base), 0);
	I915_WRITE(RING_HEAD(base), 0);
	I915_WRITE(RING_TAIL(base), 0);
	I915_WRITE(RING_START(base), 0);
}

static void init_unused_rings(struct drm_i915_private *dev_priv)
{
	if (IS_I830(dev_priv)) {
		init_unused_ring(dev_priv, PRB1_BASE);
		init_unused_ring(dev_priv, SRB0_BASE);
		init_unused_ring(dev_priv, SRB1_BASE);
		init_unused_ring(dev_priv, SRB2_BASE);
		init_unused_ring(dev_priv, SRB3_BASE);
	} else if (IS_GEN2(dev_priv)) {
		init_unused_ring(dev_priv, SRB0_BASE);
		init_unused_ring(dev_priv, SRB1_BASE);
	} else if (IS_GEN3(dev_priv)) {
		init_unused_ring(dev_priv, PRB1_BASE);
		init_unused_ring(dev_priv, PRB2_BASE);
	}
}

int
i915_gem_init_hw(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int ret;

	dev_priv->gt.last_init_time = ktime_get();

	/* Double layer security blanket, see i915_gem_init() */
	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);

	if (HAS_EDRAM(dev) && INTEL_GEN(dev_priv) < 9)
		I915_WRITE(HSW_IDICR, I915_READ(HSW_IDICR) | IDIHASHMSK(0xf));

	if (IS_HASWELL(dev_priv))
		I915_WRITE(MI_PREDICATE_RESULT_2, IS_HSW_GT3(dev_priv) ?
			   LOWER_SLICE_ENABLED : LOWER_SLICE_DISABLED);

	if (HAS_PCH_NOP(dev_priv)) {
		if (IS_IVYBRIDGE(dev_priv)) {
			u32 temp = I915_READ(GEN7_MSG_CTL);
			temp &= ~(WAIT_FOR_PCH_FLR_ACK | WAIT_FOR_PCH_RESET_ACK);
			I915_WRITE(GEN7_MSG_CTL, temp);
		} else if (INTEL_INFO(dev)->gen >= 7) {
			u32 temp = I915_READ(HSW_NDE_RSTWRN_OPT);
			temp &= ~RESET_PCH_HANDSHAKE_ENABLE;
			I915_WRITE(HSW_NDE_RSTWRN_OPT, temp);
		}
	}

	i915_gem_init_swizzling(dev);

	/*
	 * At least 830 can leave some of the unused rings
	 * "active" (ie. head != tail) after resume which
	 * will prevent c3 entry. Makes sure all unused rings
	 * are totally idle.
	 */
	init_unused_rings(dev_priv);

	BUG_ON(!dev_priv->kernel_context);

	ret = i915_ppgtt_init_hw(dev);
	if (ret) {
		DRM_ERROR("PPGTT enable HW failed %d\n", ret);
		goto out;
	}

	/* Need to do basic initialisation of all rings first: */
	for_each_engine(engine, dev_priv, id) {
		ret = engine->init_hw(engine);
		if (ret)
			goto out;
	}

	intel_mocs_init_l3cc_table(dev);

	/* We can't enable contexts until all firmware is loaded */
	ret = intel_guc_setup(dev);
	if (ret)
		goto out;

out:
	intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);
	return ret;
}

bool intel_sanitize_semaphores(struct drm_i915_private *dev_priv, int value)
{
	if (INTEL_INFO(dev_priv)->gen < 6)
		return false;

	/* TODO: make semaphores and Execlists play nicely together */
	if (i915.enable_execlists)
		return false;

	if (value >= 0)
		return value;

#ifdef CONFIG_INTEL_IOMMU
	/* Enable semaphores on SNB when IO remapping is off */
	if (INTEL_INFO(dev_priv)->gen == 6 && intel_iommu_gfx_mapped)
		return false;
#endif

	return true;
}

int i915_gem_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	int ret;

	mutex_lock(&dev->struct_mutex);

	if (!i915.enable_execlists) {
		dev_priv->gt.resume = intel_legacy_submission_resume;
		dev_priv->gt.cleanup_engine = intel_engine_cleanup;
	} else {
		dev_priv->gt.resume = intel_lr_context_resume;
		dev_priv->gt.cleanup_engine = intel_logical_ring_cleanup;
	}

	/* This is just a security blanket to placate dragons.
	 * On some systems, we very sporadically observe that the first TLBs
	 * used by the CS may be stale, despite us poking the TLB reset. If
	 * we hold the forcewake during initialisation these problems
	 * just magically go away.
	 */
	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);

	i915_gem_init_userptr(dev_priv);

	ret = i915_gem_init_ggtt(dev_priv);
	if (ret)
		goto out_unlock;

	ret = i915_gem_context_init(dev);
	if (ret)
		goto out_unlock;

	ret = intel_engines_init(dev);
	if (ret)
		goto out_unlock;

	ret = i915_gem_init_hw(dev);
	if (ret == -EIO) {
		/* Allow engine initialisation to fail by marking the GPU as
		 * wedged. But we only want to do this where the GPU is angry,
		 * for all other failure, such as an allocation failure, bail.
		 */
		DRM_ERROR("Failed to initialize GPU, declaring it wedged\n");
		i915_gem_set_wedged(dev_priv);
		ret = 0;
	}

out_unlock:
	intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);
	mutex_unlock(&dev->struct_mutex);

	return ret;
}

void
i915_gem_cleanup_engines(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, dev_priv, id)
		dev_priv->gt.cleanup_engine(engine);
}

void
i915_gem_load_init_fences(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = &dev_priv->drm;
	int i;

	if (INTEL_INFO(dev_priv)->gen >= 7 && !IS_VALLEYVIEW(dev_priv) &&
	    !IS_CHERRYVIEW(dev_priv))
		dev_priv->num_fence_regs = 32;
	else if (INTEL_INFO(dev_priv)->gen >= 4 || IS_I945G(dev_priv) ||
		 IS_I945GM(dev_priv) || IS_G33(dev_priv))
		dev_priv->num_fence_regs = 16;
	else
		dev_priv->num_fence_regs = 8;

	if (intel_vgpu_active(dev_priv))
		dev_priv->num_fence_regs =
				I915_READ(vgtif_reg(avail_rs.fence_num));

	/* Initialize fence registers to zero */
	for (i = 0; i < dev_priv->num_fence_regs; i++) {
		struct drm_i915_fence_reg *fence = &dev_priv->fence_regs[i];

		fence->i915 = dev_priv;
		fence->id = i;
		list_add_tail(&fence->link, &dev_priv->mm.fence_list);
	}
	i915_gem_restore_fences(dev);

	i915_gem_detect_bit_6_swizzle(dev);
}

void
i915_gem_load_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	dev_priv->objects =
		kmem_cache_create("i915_gem_object",
				  sizeof(struct drm_i915_gem_object), 0,
				  SLAB_HWCACHE_ALIGN,
				  NULL);
	dev_priv->vmas =
		kmem_cache_create("i915_gem_vma",
				  sizeof(struct i915_vma), 0,
				  SLAB_HWCACHE_ALIGN,
				  NULL);
	dev_priv->requests =
		kmem_cache_create("i915_gem_request",
				  sizeof(struct drm_i915_gem_request), 0,
				  SLAB_HWCACHE_ALIGN |
				  SLAB_RECLAIM_ACCOUNT |
				  SLAB_DESTROY_BY_RCU,
				  NULL);

	INIT_LIST_HEAD(&dev_priv->context_list);
	INIT_LIST_HEAD(&dev_priv->mm.unbound_list);
	INIT_LIST_HEAD(&dev_priv->mm.bound_list);
	INIT_LIST_HEAD(&dev_priv->mm.fence_list);
	INIT_LIST_HEAD(&dev_priv->mm.userfault_list);
	INIT_DELAYED_WORK(&dev_priv->gt.retire_work,
			  i915_gem_retire_work_handler);
	INIT_DELAYED_WORK(&dev_priv->gt.idle_work,
			  i915_gem_idle_work_handler);
	init_waitqueue_head(&dev_priv->gpu_error.wait_queue);
	init_waitqueue_head(&dev_priv->gpu_error.reset_queue);

	dev_priv->relative_constants_mode = I915_EXEC_CONSTANTS_REL_GENERAL;

	init_waitqueue_head(&dev_priv->pending_flip_queue);

	dev_priv->mm.interruptible = true;

	atomic_set(&dev_priv->mm.bsd_engine_dispatch_index, 0);

	spin_lock_init(&dev_priv->fb_tracking.lock);
}

void i915_gem_load_cleanup(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	kmem_cache_destroy(dev_priv->requests);
	kmem_cache_destroy(dev_priv->vmas);
	kmem_cache_destroy(dev_priv->objects);

	/* And ensure that our DESTROY_BY_RCU slabs are truly destroyed */
	rcu_barrier();
}

int i915_gem_freeze(struct drm_i915_private *dev_priv)
{
	intel_runtime_pm_get(dev_priv);

	mutex_lock(&dev_priv->drm.struct_mutex);
	i915_gem_shrink_all(dev_priv);
	mutex_unlock(&dev_priv->drm.struct_mutex);

	intel_runtime_pm_put(dev_priv);

	return 0;
}

int i915_gem_freeze_late(struct drm_i915_private *dev_priv)
{
	struct drm_i915_gem_object *obj;
	struct list_head *phases[] = {
		&dev_priv->mm.unbound_list,
		&dev_priv->mm.bound_list,
		NULL
	}, **p;

	/* Called just before we write the hibernation image.
	 *
	 * We need to update the domain tracking to reflect that the CPU
	 * will be accessing all the pages to create and restore from the
	 * hibernation, and so upon restoration those pages will be in the
	 * CPU domain.
	 *
	 * To make sure the hibernation image contains the latest state,
	 * we update that state just before writing out the image.
	 *
	 * To try and reduce the hibernation image, we manually shrink
	 * the objects as well.
	 */

	mutex_lock(&dev_priv->drm.struct_mutex);
	i915_gem_shrink(dev_priv, -1UL, I915_SHRINK_UNBOUND);

	for (p = phases; *p; p++) {
		list_for_each_entry(obj, *p, global_list) {
			obj->base.read_domains = I915_GEM_DOMAIN_CPU;
			obj->base.write_domain = I915_GEM_DOMAIN_CPU;
		}
	}
	mutex_unlock(&dev_priv->drm.struct_mutex);

	return 0;
}

void i915_gem_release(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct drm_i915_gem_request *request;

	/* Clean up our request list when the client is going away, so that
	 * later retire_requests won't dereference our soon-to-be-gone
	 * file_priv.
	 */
	spin_lock(&file_priv->mm.lock);
	list_for_each_entry(request, &file_priv->mm.request_list, client_list)
		request->file_priv = NULL;
	spin_unlock(&file_priv->mm.lock);

	if (!list_empty(&file_priv->rps.link)) {
		spin_lock(&to_i915(dev)->rps.client_lock);
		list_del(&file_priv->rps.link);
		spin_unlock(&to_i915(dev)->rps.client_lock);
	}
}

int i915_gem_open(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_file_private *file_priv;
	int ret;

	DRM_DEBUG_DRIVER("\n");

	file_priv = kzalloc(sizeof(*file_priv), GFP_KERNEL);
	if (!file_priv)
		return -ENOMEM;

	file->driver_priv = file_priv;
	file_priv->dev_priv = to_i915(dev);
	file_priv->file = file;
	INIT_LIST_HEAD(&file_priv->rps.link);

	spin_lock_init(&file_priv->mm.lock);
	INIT_LIST_HEAD(&file_priv->mm.request_list);

	file_priv->bsd_engine = -1;

	ret = i915_gem_context_open(dev, file);
	if (ret)
		kfree(file_priv);

	return ret;
}

/**
 * i915_gem_track_fb - update frontbuffer tracking
 * @old: current GEM buffer for the frontbuffer slots
 * @new: new GEM buffer for the frontbuffer slots
 * @frontbuffer_bits: bitmask of frontbuffer slots
 *
 * This updates the frontbuffer tracking bits @frontbuffer_bits by clearing them
 * from @old and setting them in @new. Both @old and @new can be NULL.
 */
void i915_gem_track_fb(struct drm_i915_gem_object *old,
		       struct drm_i915_gem_object *new,
		       unsigned frontbuffer_bits)
{
	/* Control of individual bits within the mask are guarded by
	 * the owning plane->mutex, i.e. we can never see concurrent
	 * manipulation of individual bits. But since the bitfield as a whole
	 * is updated using RMW, we need to use atomics in order to update
	 * the bits.
	 */
	BUILD_BUG_ON(INTEL_FRONTBUFFER_BITS_PER_PIPE * I915_MAX_PIPES >
		     sizeof(atomic_t) * BITS_PER_BYTE);

	if (old) {
		WARN_ON(!(atomic_read(&old->frontbuffer_bits) & frontbuffer_bits));
		atomic_andnot(frontbuffer_bits, &old->frontbuffer_bits);
	}

	if (new) {
		WARN_ON(atomic_read(&new->frontbuffer_bits) & frontbuffer_bits);
		atomic_or(frontbuffer_bits, &new->frontbuffer_bits);
	}
}

/* Like i915_gem_object_get_page(), but mark the returned page dirty */
struct page *
i915_gem_object_get_dirty_page(struct drm_i915_gem_object *obj, int n)
{
	struct page *page;

	/* Only default objects have per-page dirty tracking */
	if (WARN_ON(!i915_gem_object_has_struct_page(obj)))
		return NULL;

	page = i915_gem_object_get_page(obj, n);
	set_page_dirty(page);
	return page;
}

/* Allocate a new GEM object and fill it with the supplied data */
struct drm_i915_gem_object *
i915_gem_object_create_from_data(struct drm_device *dev,
			         const void *data, size_t size)
{
	struct drm_i915_gem_object *obj;
	struct sg_table *sg;
	size_t bytes;
	int ret;

	obj = i915_gem_object_create(dev, round_up(size, PAGE_SIZE));
	if (IS_ERR(obj))
		return obj;

	ret = i915_gem_object_set_to_cpu_domain(obj, true);
	if (ret)
		goto fail;

	ret = i915_gem_object_get_pages(obj);
	if (ret)
		goto fail;

	i915_gem_object_pin_pages(obj);
	sg = obj->pages;
	bytes = sg_copy_from_buffer(sg->sgl, sg->nents, (void *)data, size);
	obj->dirty = 1;		/* Backing store is now out of date */
	i915_gem_object_unpin_pages(obj);

	if (WARN_ON(bytes != size)) {
		DRM_ERROR("Incomplete copy, wrote %zu of %zu", bytes, size);
		ret = -EFAULT;
		goto fail;
	}

	return obj;

fail:
	i915_gem_object_put(obj);
	return ERR_PTR(ret);
}
