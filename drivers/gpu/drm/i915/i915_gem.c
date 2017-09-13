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
#include "i915_gem_clflush.h"
#include "i915_vgpu.h"
#include "i915_trace.h"
#include "intel_drv.h"
#include "intel_frontbuffer.h"
#include "intel_mocs.h"
#include <linux/dma-fence-array.h>
#include <linux/kthread.h>
#include <linux/reservation.h>
#include <linux/shmem_fs.h>
#include <linux/slab.h>
#include <linux/stop_machine.h>
#include <linux/swap.h>
#include <linux/pci.h>
#include <linux/dma-buf.h>

static void i915_gem_flush_free_objects(struct drm_i915_private *i915);

static bool cpu_write_needs_clflush(struct drm_i915_gem_object *obj)
{
	if (obj->cache_dirty)
		return false;

	if (!(obj->cache_coherent & I915_BO_CACHE_COHERENT_FOR_WRITE))
		return true;

	return obj->pin_display;
}

static int
insert_mappable_node(struct i915_ggtt *ggtt,
                     struct drm_mm_node *node, u32 size)
{
	memset(node, 0, sizeof(*node));
	return drm_mm_insert_node_in_range(&ggtt->base.mm, node,
					   size, 0, I915_COLOR_UNEVICTABLE,
					   0, ggtt->mappable_end,
					   DRM_MM_INSERT_LOW);
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

	might_sleep();

	/*
	 * Only wait 10 seconds for the gpu reset to complete to avoid hanging
	 * userspace. If it takes that long something really bad is going on and
	 * we should simply try to bail out and fail as gracefully as possible.
	 */
	ret = wait_event_interruptible_timeout(error->reset_queue,
					       !i915_reset_backoff(error),
					       I915_RESET_TIMEOUT);
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
	u64 pinned;

	pinned = ggtt->base.reserved;
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

static struct sg_table *
i915_gem_object_get_pages_phys(struct drm_i915_gem_object *obj)
{
	struct address_space *mapping = obj->base.filp->f_mapping;
	drm_dma_handle_t *phys;
	struct sg_table *st;
	struct scatterlist *sg;
	char *vaddr;
	int i;

	if (WARN_ON(i915_gem_object_needs_bit17_swizzle(obj)))
		return ERR_PTR(-EINVAL);

	/* Always aligning to the object size, allows a single allocation
	 * to handle all possible callers, and given typical object sizes,
	 * the alignment of the buddy allocation will naturally match.
	 */
	phys = drm_pci_alloc(obj->base.dev,
			     obj->base.size,
			     roundup_pow_of_two(obj->base.size));
	if (!phys)
		return ERR_PTR(-ENOMEM);

	vaddr = phys->vaddr;
	for (i = 0; i < obj->base.size / PAGE_SIZE; i++) {
		struct page *page;
		char *src;

		page = shmem_read_mapping_page(mapping, i);
		if (IS_ERR(page)) {
			st = ERR_CAST(page);
			goto err_phys;
		}

		src = kmap_atomic(page);
		memcpy(vaddr, src, PAGE_SIZE);
		drm_clflush_virt_range(vaddr, PAGE_SIZE);
		kunmap_atomic(src);

		put_page(page);
		vaddr += PAGE_SIZE;
	}

	i915_gem_chipset_flush(to_i915(obj->base.dev));

	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (!st) {
		st = ERR_PTR(-ENOMEM);
		goto err_phys;
	}

	if (sg_alloc_table(st, 1, GFP_KERNEL)) {
		kfree(st);
		st = ERR_PTR(-ENOMEM);
		goto err_phys;
	}

	sg = st->sgl;
	sg->offset = 0;
	sg->length = obj->base.size;

	sg_dma_address(sg) = phys->busaddr;
	sg_dma_len(sg) = obj->base.size;

	obj->phys_handle = phys;
	return st;

err_phys:
	drm_pci_free(obj->base.dev, phys);
	return st;
}

static void __start_cpu_write(struct drm_i915_gem_object *obj)
{
	obj->base.read_domains = I915_GEM_DOMAIN_CPU;
	obj->base.write_domain = I915_GEM_DOMAIN_CPU;
	if (cpu_write_needs_clflush(obj))
		obj->cache_dirty = true;
}

static void
__i915_gem_object_release_shmem(struct drm_i915_gem_object *obj,
				struct sg_table *pages,
				bool needs_clflush)
{
	GEM_BUG_ON(obj->mm.madv == __I915_MADV_PURGED);

	if (obj->mm.madv == I915_MADV_DONTNEED)
		obj->mm.dirty = false;

	if (needs_clflush &&
	    (obj->base.read_domains & I915_GEM_DOMAIN_CPU) == 0 &&
	    !(obj->cache_coherent & I915_BO_CACHE_COHERENT_FOR_READ))
		drm_clflush_sg(pages);

	__start_cpu_write(obj);
}

static void
i915_gem_object_put_pages_phys(struct drm_i915_gem_object *obj,
			       struct sg_table *pages)
{
	__i915_gem_object_release_shmem(obj, pages, false);

	if (obj->mm.dirty) {
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
			if (obj->mm.madv == I915_MADV_WILLNEED)
				mark_page_accessed(page);
			put_page(page);
			vaddr += PAGE_SIZE;
		}
		obj->mm.dirty = false;
	}

	sg_free_table(pages);
	kfree(pages);

	drm_pci_free(obj->base.dev, obj->phys_handle);
}

static void
i915_gem_object_release_phys(struct drm_i915_gem_object *obj)
{
	i915_gem_object_unpin_pages(obj);
}

static const struct drm_i915_gem_object_ops i915_gem_phys_ops = {
	.get_pages = i915_gem_object_get_pages_phys,
	.put_pages = i915_gem_object_put_pages_phys,
	.release = i915_gem_object_release_phys,
};

static const struct drm_i915_gem_object_ops i915_gem_object_ops;

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
	ret = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE |
				   I915_WAIT_LOCKED |
				   I915_WAIT_ALL,
				   MAX_SCHEDULE_TIMEOUT,
				   NULL);
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

static long
i915_gem_object_wait_fence(struct dma_fence *fence,
			   unsigned int flags,
			   long timeout,
			   struct intel_rps_client *rps)
{
	struct drm_i915_gem_request *rq;

	BUILD_BUG_ON(I915_WAIT_INTERRUPTIBLE != 0x1);

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		return timeout;

	if (!dma_fence_is_i915(fence))
		return dma_fence_wait_timeout(fence,
					      flags & I915_WAIT_INTERRUPTIBLE,
					      timeout);

	rq = to_request(fence);
	if (i915_gem_request_completed(rq))
		goto out;

	/* This client is about to stall waiting for the GPU. In many cases
	 * this is undesirable and limits the throughput of the system, as
	 * many clients cannot continue processing user input/output whilst
	 * blocked. RPS autotuning may take tens of milliseconds to respond
	 * to the GPU load and thus incurs additional latency for the client.
	 * We can circumvent that by promoting the GPU frequency to maximum
	 * before we wait. This makes the GPU throttle up much more quickly
	 * (good for benchmarks and user experience, e.g. window animations),
	 * but at a cost of spending more power processing the workload
	 * (bad for battery). Not all clients even want their results
	 * immediately and for them we should just let the GPU select its own
	 * frequency to maximise efficiency. To prevent a single client from
	 * forcing the clocks too high for the whole system, we only allow
	 * each client to waitboost once in a busy period.
	 */
	if (rps) {
		if (INTEL_GEN(rq->i915) >= 6)
			gen6_rps_boost(rq, rps);
		else
			rps = NULL;
	}

	timeout = i915_wait_request(rq, flags, timeout);

out:
	if (flags & I915_WAIT_LOCKED && i915_gem_request_completed(rq))
		i915_gem_request_retire_upto(rq);

	return timeout;
}

static long
i915_gem_object_wait_reservation(struct reservation_object *resv,
				 unsigned int flags,
				 long timeout,
				 struct intel_rps_client *rps)
{
	unsigned int seq = __read_seqcount_begin(&resv->seq);
	struct dma_fence *excl;
	bool prune_fences = false;

	if (flags & I915_WAIT_ALL) {
		struct dma_fence **shared;
		unsigned int count, i;
		int ret;

		ret = reservation_object_get_fences_rcu(resv,
							&excl, &count, &shared);
		if (ret)
			return ret;

		for (i = 0; i < count; i++) {
			timeout = i915_gem_object_wait_fence(shared[i],
							     flags, timeout,
							     rps);
			if (timeout < 0)
				break;

			dma_fence_put(shared[i]);
		}

		for (; i < count; i++)
			dma_fence_put(shared[i]);
		kfree(shared);

		prune_fences = count && timeout >= 0;
	} else {
		excl = reservation_object_get_excl_rcu(resv);
	}

	if (excl && timeout >= 0) {
		timeout = i915_gem_object_wait_fence(excl, flags, timeout, rps);
		prune_fences = timeout >= 0;
	}

	dma_fence_put(excl);

	/* Oportunistically prune the fences iff we know they have *all* been
	 * signaled and that the reservation object has not been changed (i.e.
	 * no new fences have been added).
	 */
	if (prune_fences && !__read_seqcount_retry(&resv->seq, seq)) {
		if (reservation_object_trylock(resv)) {
			if (!__read_seqcount_retry(&resv->seq, seq))
				reservation_object_add_excl_fence(resv, NULL);
			reservation_object_unlock(resv);
		}
	}

	return timeout;
}

static void __fence_set_priority(struct dma_fence *fence, int prio)
{
	struct drm_i915_gem_request *rq;
	struct intel_engine_cs *engine;

	if (!dma_fence_is_i915(fence))
		return;

	rq = to_request(fence);
	engine = rq->engine;
	if (!engine->schedule)
		return;

	engine->schedule(rq, prio);
}

static void fence_set_priority(struct dma_fence *fence, int prio)
{
	/* Recurse once into a fence-array */
	if (dma_fence_is_array(fence)) {
		struct dma_fence_array *array = to_dma_fence_array(fence);
		int i;

		for (i = 0; i < array->num_fences; i++)
			__fence_set_priority(array->fences[i], prio);
	} else {
		__fence_set_priority(fence, prio);
	}
}

int
i915_gem_object_wait_priority(struct drm_i915_gem_object *obj,
			      unsigned int flags,
			      int prio)
{
	struct dma_fence *excl;

	if (flags & I915_WAIT_ALL) {
		struct dma_fence **shared;
		unsigned int count, i;
		int ret;

		ret = reservation_object_get_fences_rcu(obj->resv,
							&excl, &count, &shared);
		if (ret)
			return ret;

		for (i = 0; i < count; i++) {
			fence_set_priority(shared[i], prio);
			dma_fence_put(shared[i]);
		}

		kfree(shared);
	} else {
		excl = reservation_object_get_excl_rcu(obj->resv);
	}

	if (excl) {
		fence_set_priority(excl, prio);
		dma_fence_put(excl);
	}
	return 0;
}

/**
 * Waits for rendering to the object to be completed
 * @obj: i915 gem object
 * @flags: how to wait (under a lock, for all rendering or just for writes etc)
 * @timeout: how long to wait
 * @rps: client (user process) to charge for any waitboosting
 */
int
i915_gem_object_wait(struct drm_i915_gem_object *obj,
		     unsigned int flags,
		     long timeout,
		     struct intel_rps_client *rps)
{
	might_sleep();
#if IS_ENABLED(CONFIG_LOCKDEP)
	GEM_BUG_ON(debug_locks &&
		   !!lockdep_is_held(&obj->base.dev->struct_mutex) !=
		   !!(flags & I915_WAIT_LOCKED));
#endif
	GEM_BUG_ON(timeout < 0);

	timeout = i915_gem_object_wait_reservation(obj->resv,
						   flags, timeout,
						   rps);
	return timeout < 0 ? timeout : 0;
}

static struct intel_rps_client *to_rps_client(struct drm_file *file)
{
	struct drm_i915_file_private *fpriv = file->driver_priv;

	return &fpriv->rps;
}

static int
i915_gem_phys_pwrite(struct drm_i915_gem_object *obj,
		     struct drm_i915_gem_pwrite *args,
		     struct drm_file *file)
{
	void *vaddr = obj->phys_handle->vaddr + args->offset;
	char __user *user_data = u64_to_user_ptr(args->data_ptr);

	/* We manually control the domain here and pretend that it
	 * remains coherent i.e. in the GTT domain, like shmem_pwrite.
	 */
	intel_fb_obj_invalidate(obj, ORIGIN_CPU);
	if (copy_from_user(vaddr, user_data, args->size))
		return -EFAULT;

	drm_clflush_virt_range(vaddr, args->size);
	i915_gem_chipset_flush(to_i915(obj->base.dev));

	intel_fb_obj_flush(obj, ORIGIN_CPU);
	return 0;
}

void *i915_gem_object_alloc(struct drm_i915_private *dev_priv)
{
	return kmem_cache_zalloc(dev_priv->objects, GFP_KERNEL);
}

void i915_gem_object_free(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);
	kmem_cache_free(dev_priv->objects, obj);
}

static int
i915_gem_create(struct drm_file *file,
		struct drm_i915_private *dev_priv,
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
	obj = i915_gem_object_create(dev_priv, size);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	ret = drm_gem_handle_create(file, &obj->base, &handle);
	/* drop reference from allocate - handle holds it now */
	i915_gem_object_put(obj);
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
	return i915_gem_create(file, to_i915(dev),
			       args->size, &args->handle);
}

static bool gpu_write_needs_clflush(struct drm_i915_gem_object *obj)
{
	return !(obj->cache_level == I915_CACHE_NONE ||
		 obj->cache_level == I915_CACHE_WT);
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
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_i915_gem_create *args = data;

	i915_gem_flush_free_objects(dev_priv);

	return i915_gem_create(file, dev_priv,
			       args->size, &args->handle);
}

static inline enum fb_op_origin
fb_write_origin(struct drm_i915_gem_object *obj, unsigned int domain)
{
	return (domain == I915_GEM_DOMAIN_GTT ?
		obj->frontbuffer_ggtt_origin : ORIGIN_CPU);
}

static void
flush_write_domain(struct drm_i915_gem_object *obj, unsigned int flush_domains)
{
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);

	if (!(obj->base.write_domain & flush_domains))
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

	switch (obj->base.write_domain) {
	case I915_GEM_DOMAIN_GTT:
		if (INTEL_GEN(dev_priv) >= 6 && !HAS_LLC(dev_priv)) {
			intel_runtime_pm_get(dev_priv);
			spin_lock_irq(&dev_priv->uncore.lock);
			POSTING_READ_FW(RING_ACTHD(dev_priv->engine[RCS]->mmio_base));
			spin_unlock_irq(&dev_priv->uncore.lock);
			intel_runtime_pm_put(dev_priv);
		}

		intel_fb_obj_flush(obj,
				   fb_write_origin(obj, I915_GEM_DOMAIN_GTT));
		break;

	case I915_GEM_DOMAIN_CPU:
		i915_gem_clflush_object(obj, I915_CLFLUSH_SYNC);
		break;

	case I915_GEM_DOMAIN_RENDER:
		if (gpu_write_needs_clflush(obj))
			obj->cache_dirty = true;
		break;
	}

	obj->base.write_domain = 0;
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

	lockdep_assert_held(&obj->base.dev->struct_mutex);

	*needs_clflush = 0;
	if (!i915_gem_object_has_struct_page(obj))
		return -ENODEV;

	ret = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE |
				   I915_WAIT_LOCKED,
				   MAX_SCHEDULE_TIMEOUT,
				   NULL);
	if (ret)
		return ret;

	ret = i915_gem_object_pin_pages(obj);
	if (ret)
		return ret;

	if (obj->cache_coherent & I915_BO_CACHE_COHERENT_FOR_READ ||
	    !static_cpu_has(X86_FEATURE_CLFLUSH)) {
		ret = i915_gem_object_set_to_cpu_domain(obj, false);
		if (ret)
			goto err_unpin;
		else
			goto out;
	}

	flush_write_domain(obj, ~I915_GEM_DOMAIN_CPU);

	/* If we're not in the cpu read domain, set ourself into the gtt
	 * read domain and manually flush cachelines (if required). This
	 * optimizes for the case when the gpu will dirty the data
	 * anyway again before the next pread happens.
	 */
	if (!obj->cache_dirty &&
	    !(obj->base.read_domains & I915_GEM_DOMAIN_CPU))
		*needs_clflush = CLFLUSH_BEFORE;

out:
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

	lockdep_assert_held(&obj->base.dev->struct_mutex);

	*needs_clflush = 0;
	if (!i915_gem_object_has_struct_page(obj))
		return -ENODEV;

	ret = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE |
				   I915_WAIT_LOCKED |
				   I915_WAIT_ALL,
				   MAX_SCHEDULE_TIMEOUT,
				   NULL);
	if (ret)
		return ret;

	ret = i915_gem_object_pin_pages(obj);
	if (ret)
		return ret;

	if (obj->cache_coherent & I915_BO_CACHE_COHERENT_FOR_WRITE ||
	    !static_cpu_has(X86_FEATURE_CLFLUSH)) {
		ret = i915_gem_object_set_to_cpu_domain(obj, true);
		if (ret)
			goto err_unpin;
		else
			goto out;
	}

	flush_write_domain(obj, ~I915_GEM_DOMAIN_CPU);

	/* If we're not in the cpu write domain, set ourself into the
	 * gtt write domain and manually flush cachelines (as required).
	 * This optimizes for the case when the gpu will use the data
	 * right away and we therefore have to clflush anyway.
	 */
	if (!obj->cache_dirty) {
		*needs_clflush |= CLFLUSH_AFTER;

		/*
		 * Same trick applies to invalidate partially written
		 * cachelines read before writing.
		 */
		if (!(obj->base.read_domains & I915_GEM_DOMAIN_CPU))
			*needs_clflush |= CLFLUSH_BEFORE;
	}

out:
	intel_fb_obj_invalidate(obj, ORIGIN_CPU);
	obj->mm.dirty = true;
	/* return with the pages pinned */
	return 0;

err_unpin:
	i915_gem_object_unpin_pages(obj);
	return ret;
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
shmem_pread_slow(struct page *page, int offset, int length,
		 char __user *user_data,
		 bool page_do_bit17_swizzling, bool needs_clflush)
{
	char *vaddr;
	int ret;

	vaddr = kmap(page);
	if (needs_clflush)
		shmem_clflush_swizzled_range(vaddr + offset, length,
					     page_do_bit17_swizzling);

	if (page_do_bit17_swizzling)
		ret = __copy_to_user_swizzled(user_data, vaddr, offset, length);
	else
		ret = __copy_to_user(user_data, vaddr + offset, length);
	kunmap(page);

	return ret ? - EFAULT : 0;
}

static int
shmem_pread(struct page *page, int offset, int length, char __user *user_data,
	    bool page_do_bit17_swizzling, bool needs_clflush)
{
	int ret;

	ret = -ENODEV;
	if (!page_do_bit17_swizzling) {
		char *vaddr = kmap_atomic(page);

		if (needs_clflush)
			drm_clflush_virt_range(vaddr + offset, length);
		ret = __copy_to_user_inatomic(user_data, vaddr + offset, length);
		kunmap_atomic(vaddr);
	}
	if (ret == 0)
		return 0;

	return shmem_pread_slow(page, offset, length, user_data,
				page_do_bit17_swizzling, needs_clflush);
}

static int
i915_gem_shmem_pread(struct drm_i915_gem_object *obj,
		     struct drm_i915_gem_pread *args)
{
	char __user *user_data;
	u64 remain;
	unsigned int obj_do_bit17_swizzling;
	unsigned int needs_clflush;
	unsigned int idx, offset;
	int ret;

	obj_do_bit17_swizzling = 0;
	if (i915_gem_object_needs_bit17_swizzle(obj))
		obj_do_bit17_swizzling = BIT(17);

	ret = mutex_lock_interruptible(&obj->base.dev->struct_mutex);
	if (ret)
		return ret;

	ret = i915_gem_obj_prepare_shmem_read(obj, &needs_clflush);
	mutex_unlock(&obj->base.dev->struct_mutex);
	if (ret)
		return ret;

	remain = args->size;
	user_data = u64_to_user_ptr(args->data_ptr);
	offset = offset_in_page(args->offset);
	for (idx = args->offset >> PAGE_SHIFT; remain; idx++) {
		struct page *page = i915_gem_object_get_page(obj, idx);
		int length;

		length = remain;
		if (offset + length > PAGE_SIZE)
			length = PAGE_SIZE - offset;

		ret = shmem_pread(page, offset, length, user_data,
				  page_to_phys(page) & obj_do_bit17_swizzling,
				  needs_clflush);
		if (ret)
			break;

		remain -= length;
		user_data += length;
		offset = 0;
	}

	i915_gem_obj_finish_shmem_access(obj);
	return ret;
}

static inline bool
gtt_user_read(struct io_mapping *mapping,
	      loff_t base, int offset,
	      char __user *user_data, int length)
{
	void *vaddr;
	unsigned long unwritten;

	/* We can use the cpu mem copy function because this is X86. */
	vaddr = (void __force *)io_mapping_map_atomic_wc(mapping, base);
	unwritten = __copy_to_user_inatomic(user_data, vaddr + offset, length);
	io_mapping_unmap_atomic(vaddr);
	if (unwritten) {
		vaddr = (void __force *)
			io_mapping_map_wc(mapping, base, PAGE_SIZE);
		unwritten = copy_to_user(user_data, vaddr + offset, length);
		io_mapping_unmap(vaddr);
	}
	return unwritten;
}

static int
i915_gem_gtt_pread(struct drm_i915_gem_object *obj,
		   const struct drm_i915_gem_pread *args)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct i915_ggtt *ggtt = &i915->ggtt;
	struct drm_mm_node node;
	struct i915_vma *vma;
	void __user *user_data;
	u64 remain, offset;
	int ret;

	ret = mutex_lock_interruptible(&i915->drm.struct_mutex);
	if (ret)
		return ret;

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
		ret = insert_mappable_node(ggtt, &node, PAGE_SIZE);
		if (ret)
			goto out_unlock;
		GEM_BUG_ON(!node.allocated);
	}

	ret = i915_gem_object_set_to_gtt_domain(obj, false);
	if (ret)
		goto out_unpin;

	mutex_unlock(&i915->drm.struct_mutex);

	user_data = u64_to_user_ptr(args->data_ptr);
	remain = args->size;
	offset = args->offset;

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
					       node.start, I915_CACHE_NONE, 0);
			wmb();
		} else {
			page_base += offset & PAGE_MASK;
		}

		if (gtt_user_read(&ggtt->mappable, page_base, page_offset,
				  user_data, page_length)) {
			ret = -EFAULT;
			break;
		}

		remain -= page_length;
		user_data += page_length;
		offset += page_length;
	}

	mutex_lock(&i915->drm.struct_mutex);
out_unpin:
	if (node.allocated) {
		wmb();
		ggtt->base.clear_range(&ggtt->base,
				       node.start, node.size);
		remove_mappable_node(&node);
	} else {
		i915_vma_unpin(vma);
	}
out_unlock:
	intel_runtime_pm_put(i915);
	mutex_unlock(&i915->drm.struct_mutex);

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
	int ret;

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
	if (range_overflows_t(u64, args->offset, args->size, obj->base.size)) {
		ret = -EINVAL;
		goto out;
	}

	trace_i915_gem_object_pread(obj, args->offset, args->size);

	ret = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE,
				   MAX_SCHEDULE_TIMEOUT,
				   to_rps_client(file));
	if (ret)
		goto out;

	ret = i915_gem_object_pin_pages(obj);
	if (ret)
		goto out;

	ret = i915_gem_shmem_pread(obj, args);
	if (ret == -EFAULT || ret == -ENODEV)
		ret = i915_gem_gtt_pread(obj, args);

	i915_gem_object_unpin_pages(obj);
out:
	i915_gem_object_put(obj);
	return ret;
}

/* This is the fast write path which cannot handle
 * page faults in the source data
 */

static inline bool
ggtt_write(struct io_mapping *mapping,
	   loff_t base, int offset,
	   char __user *user_data, int length)
{
	void *vaddr;
	unsigned long unwritten;

	/* We can use the cpu mem copy function because this is X86. */
	vaddr = (void __force *)io_mapping_map_atomic_wc(mapping, base);
	unwritten = __copy_from_user_inatomic_nocache(vaddr + offset,
						      user_data, length);
	io_mapping_unmap_atomic(vaddr);
	if (unwritten) {
		vaddr = (void __force *)
			io_mapping_map_wc(mapping, base, PAGE_SIZE);
		unwritten = copy_from_user(vaddr + offset, user_data, length);
		io_mapping_unmap(vaddr);
	}

	return unwritten;
}

/**
 * This is the fast pwrite path, where we copy the data directly from the
 * user into the GTT, uncached.
 * @obj: i915 GEM object
 * @args: pwrite arguments structure
 */
static int
i915_gem_gtt_pwrite_fast(struct drm_i915_gem_object *obj,
			 const struct drm_i915_gem_pwrite *args)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct i915_ggtt *ggtt = &i915->ggtt;
	struct drm_mm_node node;
	struct i915_vma *vma;
	u64 remain, offset;
	void __user *user_data;
	int ret;

	ret = mutex_lock_interruptible(&i915->drm.struct_mutex);
	if (ret)
		return ret;

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
		ret = insert_mappable_node(ggtt, &node, PAGE_SIZE);
		if (ret)
			goto out_unlock;
		GEM_BUG_ON(!node.allocated);
	}

	ret = i915_gem_object_set_to_gtt_domain(obj, true);
	if (ret)
		goto out_unpin;

	mutex_unlock(&i915->drm.struct_mutex);

	intel_fb_obj_invalidate(obj, ORIGIN_CPU);

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
		unsigned int page_offset = offset_in_page(offset);
		unsigned int page_length = PAGE_SIZE - page_offset;
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
		if (ggtt_write(&ggtt->mappable, page_base, page_offset,
			       user_data, page_length)) {
			ret = -EFAULT;
			break;
		}

		remain -= page_length;
		user_data += page_length;
		offset += page_length;
	}
	intel_fb_obj_flush(obj, ORIGIN_CPU);

	mutex_lock(&i915->drm.struct_mutex);
out_unpin:
	if (node.allocated) {
		wmb();
		ggtt->base.clear_range(&ggtt->base,
				       node.start, node.size);
		remove_mappable_node(&node);
	} else {
		i915_vma_unpin(vma);
	}
out_unlock:
	intel_runtime_pm_put(i915);
	mutex_unlock(&i915->drm.struct_mutex);
	return ret;
}

static int
shmem_pwrite_slow(struct page *page, int offset, int length,
		  char __user *user_data,
		  bool page_do_bit17_swizzling,
		  bool needs_clflush_before,
		  bool needs_clflush_after)
{
	char *vaddr;
	int ret;

	vaddr = kmap(page);
	if (unlikely(needs_clflush_before || page_do_bit17_swizzling))
		shmem_clflush_swizzled_range(vaddr + offset, length,
					     page_do_bit17_swizzling);
	if (page_do_bit17_swizzling)
		ret = __copy_from_user_swizzled(vaddr, offset, user_data,
						length);
	else
		ret = __copy_from_user(vaddr + offset, user_data, length);
	if (needs_clflush_after)
		shmem_clflush_swizzled_range(vaddr + offset, length,
					     page_do_bit17_swizzling);
	kunmap(page);

	return ret ? -EFAULT : 0;
}

/* Per-page copy function for the shmem pwrite fastpath.
 * Flushes invalid cachelines before writing to the target if
 * needs_clflush_before is set and flushes out any written cachelines after
 * writing if needs_clflush is set.
 */
static int
shmem_pwrite(struct page *page, int offset, int len, char __user *user_data,
	     bool page_do_bit17_swizzling,
	     bool needs_clflush_before,
	     bool needs_clflush_after)
{
	int ret;

	ret = -ENODEV;
	if (!page_do_bit17_swizzling) {
		char *vaddr = kmap_atomic(page);

		if (needs_clflush_before)
			drm_clflush_virt_range(vaddr + offset, len);
		ret = __copy_from_user_inatomic(vaddr + offset, user_data, len);
		if (needs_clflush_after)
			drm_clflush_virt_range(vaddr + offset, len);

		kunmap_atomic(vaddr);
	}
	if (ret == 0)
		return ret;

	return shmem_pwrite_slow(page, offset, len, user_data,
				 page_do_bit17_swizzling,
				 needs_clflush_before,
				 needs_clflush_after);
}

static int
i915_gem_shmem_pwrite(struct drm_i915_gem_object *obj,
		      const struct drm_i915_gem_pwrite *args)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	void __user *user_data;
	u64 remain;
	unsigned int obj_do_bit17_swizzling;
	unsigned int partial_cacheline_write;
	unsigned int needs_clflush;
	unsigned int offset, idx;
	int ret;

	ret = mutex_lock_interruptible(&i915->drm.struct_mutex);
	if (ret)
		return ret;

	ret = i915_gem_obj_prepare_shmem_write(obj, &needs_clflush);
	mutex_unlock(&i915->drm.struct_mutex);
	if (ret)
		return ret;

	obj_do_bit17_swizzling = 0;
	if (i915_gem_object_needs_bit17_swizzle(obj))
		obj_do_bit17_swizzling = BIT(17);

	/* If we don't overwrite a cacheline completely we need to be
	 * careful to have up-to-date data by first clflushing. Don't
	 * overcomplicate things and flush the entire patch.
	 */
	partial_cacheline_write = 0;
	if (needs_clflush & CLFLUSH_BEFORE)
		partial_cacheline_write = boot_cpu_data.x86_clflush_size - 1;

	user_data = u64_to_user_ptr(args->data_ptr);
	remain = args->size;
	offset = offset_in_page(args->offset);
	for (idx = args->offset >> PAGE_SHIFT; remain; idx++) {
		struct page *page = i915_gem_object_get_page(obj, idx);
		int length;

		length = remain;
		if (offset + length > PAGE_SIZE)
			length = PAGE_SIZE - offset;

		ret = shmem_pwrite(page, offset, length, user_data,
				   page_to_phys(page) & obj_do_bit17_swizzling,
				   (offset | length) & partial_cacheline_write,
				   needs_clflush & CLFLUSH_AFTER);
		if (ret)
			break;

		remain -= length;
		user_data += length;
		offset = 0;
	}

	intel_fb_obj_flush(obj, ORIGIN_CPU);
	i915_gem_obj_finish_shmem_access(obj);
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
	struct drm_i915_gem_pwrite *args = data;
	struct drm_i915_gem_object *obj;
	int ret;

	if (args->size == 0)
		return 0;

	if (!access_ok(VERIFY_READ,
		       u64_to_user_ptr(args->data_ptr),
		       args->size))
		return -EFAULT;

	obj = i915_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	/* Bounds check destination. */
	if (range_overflows_t(u64, args->offset, args->size, obj->base.size)) {
		ret = -EINVAL;
		goto err;
	}

	trace_i915_gem_object_pwrite(obj, args->offset, args->size);

	ret = -ENODEV;
	if (obj->ops->pwrite)
		ret = obj->ops->pwrite(obj, args);
	if (ret != -ENODEV)
		goto err;

	ret = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE |
				   I915_WAIT_ALL,
				   MAX_SCHEDULE_TIMEOUT,
				   to_rps_client(file));
	if (ret)
		goto err;

	ret = i915_gem_object_pin_pages(obj);
	if (ret)
		goto err;

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
		ret = i915_gem_gtt_pwrite_fast(obj, args);

	if (ret == -EFAULT || ret == -ENOSPC) {
		if (obj->phys_handle)
			ret = i915_gem_phys_pwrite(obj, args, file);
		else
			ret = i915_gem_shmem_pwrite(obj, args);
	}

	i915_gem_object_unpin_pages(obj);
err:
	i915_gem_object_put(obj);
	return ret;
}

static void i915_gem_object_bump_inactive_ggtt(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915;
	struct list_head *list;
	struct i915_vma *vma;

	list_for_each_entry(vma, &obj->vma_list, obj_link) {
		if (!i915_vma_is_ggtt(vma))
			break;

		if (i915_vma_is_active(vma))
			continue;

		if (!drm_mm_node_allocated(&vma->node))
			continue;

		list_move_tail(&vma->vm_link, &vma->vm->inactive_list);
	}

	i915 = to_i915(obj->base.dev);
	list = obj->bind_count ? &i915->mm.bound_list : &i915->mm.unbound_list;
	list_move_tail(&obj->global_link, list);
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
	int err;

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
	err = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE |
				   (write_domain ? I915_WAIT_ALL : 0),
				   MAX_SCHEDULE_TIMEOUT,
				   to_rps_client(file));
	if (err)
		goto out;

	/* Flush and acquire obj->pages so that we are coherent through
	 * direct access in memory with previous cached writes through
	 * shmemfs and that our cache domain tracking remains valid.
	 * For example, if the obj->filp was moved to swap without us
	 * being notified and releasing the pages, we would mistakenly
	 * continue to assume that the obj remained out of the CPU cached
	 * domain.
	 */
	err = i915_gem_object_pin_pages(obj);
	if (err)
		goto out;

	err = i915_mutex_lock_interruptible(dev);
	if (err)
		goto out_unpin;

	if (read_domains & I915_GEM_DOMAIN_WC)
		err = i915_gem_object_set_to_wc_domain(obj, write_domain);
	else if (read_domains & I915_GEM_DOMAIN_GTT)
		err = i915_gem_object_set_to_gtt_domain(obj, write_domain);
	else
		err = i915_gem_object_set_to_cpu_domain(obj, write_domain);

	/* And bump the LRU for this access */
	i915_gem_object_bump_inactive_ggtt(obj);

	mutex_unlock(&dev->struct_mutex);

	if (write_domain != 0)
		intel_fb_obj_invalidate(obj,
					fb_write_origin(obj, write_domain));

out_unpin:
	i915_gem_object_unpin_pages(obj);
out:
	i915_gem_object_put(obj);
	return err;
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

	obj = i915_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	/* Pinned buffers may be scanout, so flush the cache */
	i915_gem_object_flush_if_display(obj);
	i915_gem_object_put(obj);

	return 0;
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
		i915_gem_object_put(obj);
		return -EINVAL;
	}

	addr = vm_mmap(obj->base.filp, 0, args->size,
		       PROT_READ | PROT_WRITE, MAP_SHARED,
		       args->offset);
	if (args->flags & I915_MMAP_WC) {
		struct mm_struct *mm = current->mm;
		struct vm_area_struct *vma;

		if (down_write_killable(&mm->mmap_sem)) {
			i915_gem_object_put(obj);
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
	i915_gem_object_put(obj);
	if (IS_ERR((void *)addr))
		return addr;

	args->addr_ptr = (uint64_t) addr;

	return 0;
}

static unsigned int tile_row_pages(struct drm_i915_gem_object *obj)
{
	return i915_gem_object_get_tile_row_size(obj) >> PAGE_SHIFT;
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
 * 2 - Recognise WC as a separate cache domain so that we can flush the
 *     delayed writes via GTT before performing direct access via WC.
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
	return 2;
}

static inline struct i915_ggtt_view
compute_partial_view(struct drm_i915_gem_object *obj,
		     pgoff_t page_offset,
		     unsigned int chunk)
{
	struct i915_ggtt_view view;

	if (i915_gem_object_is_tiled(obj))
		chunk = roundup(chunk, tile_row_pages(obj));

	view.type = I915_GGTT_VIEW_PARTIAL;
	view.partial.offset = rounddown(page_offset, chunk);
	view.partial.size =
		min_t(unsigned int, chunk,
		      (obj->base.size >> PAGE_SHIFT) - view.partial.offset);

	/* If the partial covers the entire object, just create a normal VMA. */
	if (chunk >= obj->base.size >> PAGE_SHIFT)
		view.type = I915_GGTT_VIEW_NORMAL;

	return view;
}

/**
 * i915_gem_fault - fault a page into the GTT
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
int i915_gem_fault(struct vm_fault *vmf)
{
#define MIN_CHUNK_PAGES ((1 << 20) >> PAGE_SHIFT) /* 1 MiB */
	struct vm_area_struct *area = vmf->vma;
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
	page_offset = (vmf->address - area->vm_start) >> PAGE_SHIFT;

	trace_i915_gem_object_fault(obj, page_offset, true, write);

	/* Try to flush the object off the GPU first without holding the lock.
	 * Upon acquiring the lock, we will perform our sanity checks and then
	 * repeat the flush holding the lock in the normal manner to catch cases
	 * where we are gazumped.
	 */
	ret = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE,
				   MAX_SCHEDULE_TIMEOUT,
				   NULL);
	if (ret)
		goto err;

	ret = i915_gem_object_pin_pages(obj);
	if (ret)
		goto err;

	intel_runtime_pm_get(dev_priv);

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		goto err_rpm;

	/* Access to snoopable pages through the GTT is incoherent. */
	if (obj->cache_level != I915_CACHE_NONE && !HAS_LLC(dev_priv)) {
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
		/* Use a partial view if it is bigger than available space */
		struct i915_ggtt_view view =
			compute_partial_view(obj, page_offset, MIN_CHUNK_PAGES);

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
			       area->vm_start + (vma->ggtt_view.partial.offset << PAGE_SHIFT),
			       (ggtt->mappable_base + vma->node.start) >> PAGE_SHIFT,
			       min_t(u64, vma->size, area->vm_end - area->vm_start),
			       &ggtt->mappable);

err_unpin:
	__i915_vma_unpin(vma);
err_unlock:
	mutex_unlock(&dev->struct_mutex);
err_rpm:
	intel_runtime_pm_put(dev_priv);
	i915_gem_object_unpin_pages(obj);
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

		/* Ideally we want to assert that the fence register is not
		 * live at this point (i.e. that no piece of code will be
		 * trying to write through fence + GTT, as that both violates
		 * our tracking of activity and associated locking/barriers,
		 * but also is illegal given that the hw is powered down).
		 *
		 * Previously we used reg->pin_count as a "liveness" indicator.
		 * That is not sufficient, and we need a more fine-grained
		 * tool if we want to have a sanity check here.
		 */

		if (!reg->vma)
			continue;

		GEM_BUG_ON(!list_empty(&reg->vma->obj->userfault_link));
		reg->dirty = true;
	}
}

static int i915_gem_object_create_mmap_offset(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);
	int err;

	err = drm_gem_create_mmap_offset(&obj->base);
	if (likely(!err))
		return 0;

	/* Attempt to reap some mmap space from dead objects */
	do {
		err = i915_gem_wait_for_idle(dev_priv, I915_WAIT_INTERRUPTIBLE);
		if (err)
			break;

		i915_gem_drain_freed_objects(dev_priv);
		err = drm_gem_create_mmap_offset(&obj->base);
		if (!err)
			break;

	} while (flush_delayed_work(&dev_priv->gt.retire_work));

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

	i915_gem_object_put(obj);
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
	obj->mm.madv = __I915_MADV_PURGED;
	obj->mm.pages = ERR_PTR(-EFAULT);
}

/* Try to discard unwanted pages */
void __i915_gem_object_invalidate(struct drm_i915_gem_object *obj)
{
	struct address_space *mapping;

	lockdep_assert_held(&obj->mm.lock);
	GEM_BUG_ON(obj->mm.pages);

	switch (obj->mm.madv) {
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
i915_gem_object_put_pages_gtt(struct drm_i915_gem_object *obj,
			      struct sg_table *pages)
{
	struct sgt_iter sgt_iter;
	struct page *page;

	__i915_gem_object_release_shmem(obj, pages, true);

	i915_gem_gtt_finish_pages(obj, pages);

	if (i915_gem_object_needs_bit17_swizzle(obj))
		i915_gem_object_save_bit_17_swizzle(obj, pages);

	for_each_sgt_page(page, sgt_iter, pages) {
		if (obj->mm.dirty)
			set_page_dirty(page);

		if (obj->mm.madv == I915_MADV_WILLNEED)
			mark_page_accessed(page);

		put_page(page);
	}
	obj->mm.dirty = false;

	sg_free_table(pages);
	kfree(pages);
}

static void __i915_gem_object_reset_page_iter(struct drm_i915_gem_object *obj)
{
	struct radix_tree_iter iter;
	void __rcu **slot;

	radix_tree_for_each_slot(slot, &obj->mm.get_page.radix, &iter, 0)
		radix_tree_delete(&obj->mm.get_page.radix, iter.index);
}

void __i915_gem_object_put_pages(struct drm_i915_gem_object *obj,
				 enum i915_mm_subclass subclass)
{
	struct sg_table *pages;

	if (i915_gem_object_has_pinned_pages(obj))
		return;

	GEM_BUG_ON(obj->bind_count);
	if (!READ_ONCE(obj->mm.pages))
		return;

	/* May be called by shrinker from within get_pages() (on another bo) */
	mutex_lock_nested(&obj->mm.lock, subclass);
	if (unlikely(atomic_read(&obj->mm.pages_pin_count)))
		goto unlock;

	/* ->put_pages might need to allocate memory for the bit17 swizzle
	 * array, hence protect them from being reaped by removing them from gtt
	 * lists early. */
	pages = fetch_and_zero(&obj->mm.pages);
	GEM_BUG_ON(!pages);

	if (obj->mm.mapping) {
		void *ptr;

		ptr = page_mask_bits(obj->mm.mapping);
		if (is_vmalloc_addr(ptr))
			vunmap(ptr);
		else
			kunmap(kmap_to_page(ptr));

		obj->mm.mapping = NULL;
	}

	__i915_gem_object_reset_page_iter(obj);

	if (!IS_ERR(pages))
		obj->ops->put_pages(obj, pages);

unlock:
	mutex_unlock(&obj->mm.lock);
}

static bool i915_sg_trim(struct sg_table *orig_st)
{
	struct sg_table new_st;
	struct scatterlist *sg, *new_sg;
	unsigned int i;

	if (orig_st->nents == orig_st->orig_nents)
		return false;

	if (sg_alloc_table(&new_st, orig_st->nents, GFP_KERNEL | __GFP_NOWARN))
		return false;

	new_sg = new_st.sgl;
	for_each_sg(orig_st->sgl, sg, orig_st->nents, i) {
		sg_set_page(new_sg, sg_page(sg), sg->length, 0);
		/* called before being DMA mapped, no need to copy sg->dma_* */
		new_sg = sg_next(new_sg);
	}
	GEM_BUG_ON(new_sg); /* Should walk exactly nents and hit the end */

	sg_free_table(orig_st);

	*orig_st = new_st;
	return true;
}

static struct sg_table *
i915_gem_object_get_pages_gtt(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);
	const unsigned long page_count = obj->base.size / PAGE_SIZE;
	unsigned long i;
	struct address_space *mapping;
	struct sg_table *st;
	struct scatterlist *sg;
	struct sgt_iter sgt_iter;
	struct page *page;
	unsigned long last_pfn = 0;	/* suppress gcc warning */
	unsigned int max_segment;
	gfp_t noreclaim;
	int ret;

	/* Assert that the object is not currently in any GPU domain. As it
	 * wasn't in the GTT, there shouldn't be any way it could have been in
	 * a GPU cache
	 */
	GEM_BUG_ON(obj->base.read_domains & I915_GEM_GPU_DOMAINS);
	GEM_BUG_ON(obj->base.write_domain & I915_GEM_GPU_DOMAINS);

	max_segment = swiotlb_max_segment();
	if (!max_segment)
		max_segment = rounddown(UINT_MAX, PAGE_SIZE);

	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (st == NULL)
		return ERR_PTR(-ENOMEM);

rebuild_st:
	if (sg_alloc_table(st, page_count, GFP_KERNEL)) {
		kfree(st);
		return ERR_PTR(-ENOMEM);
	}

	/* Get the list of pages out of our struct file.  They'll be pinned
	 * at this point until we release them.
	 *
	 * Fail silently without starting the shrinker
	 */
	mapping = obj->base.filp->f_mapping;
	noreclaim = mapping_gfp_constraint(mapping, ~__GFP_RECLAIM);
	noreclaim |= __GFP_NORETRY | __GFP_NOWARN;

	sg = st->sgl;
	st->nents = 0;
	for (i = 0; i < page_count; i++) {
		const unsigned int shrink[] = {
			I915_SHRINK_BOUND | I915_SHRINK_UNBOUND | I915_SHRINK_PURGEABLE,
			0,
		}, *s = shrink;
		gfp_t gfp = noreclaim;

		do {
			page = shmem_read_mapping_page_gfp(mapping, i, gfp);
			if (likely(!IS_ERR(page)))
				break;

			if (!*s) {
				ret = PTR_ERR(page);
				goto err_sg;
			}

			i915_gem_shrink(dev_priv, 2 * page_count, NULL, *s++);
			cond_resched();

			/* We've tried hard to allocate the memory by reaping
			 * our own buffer, now let the real VM do its job and
			 * go down in flames if truly OOM.
			 *
			 * However, since graphics tend to be disposable,
			 * defer the oom here by reporting the ENOMEM back
			 * to userspace.
			 */
			if (!*s) {
				/* reclaim and warn, but no oom */
				gfp = mapping_gfp_mask(mapping);

				/* Our bo are always dirty and so we require
				 * kswapd to reclaim our pages (direct reclaim
				 * does not effectively begin pageout of our
				 * buffers on its own). However, direct reclaim
				 * only waits for kswapd when under allocation
				 * congestion. So as a result __GFP_RECLAIM is
				 * unreliable and fails to actually reclaim our
				 * dirty pages -- unless you try over and over
				 * again with !__GFP_NORETRY. However, we still
				 * want to fail this allocation rather than
				 * trigger the out-of-memory killer and for
				 * this we want __GFP_RETRY_MAYFAIL.
				 */
				gfp |= __GFP_RETRY_MAYFAIL;
			}
		} while (1);

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

	/* Trim unused sg entries to avoid wasting memory. */
	i915_sg_trim(st);

	ret = i915_gem_gtt_prepare_pages(obj, st);
	if (ret) {
		/* DMA remapping failed? One possible cause is that
		 * it could not reserve enough large entries, asking
		 * for PAGE_SIZE chunks instead may be helpful.
		 */
		if (max_segment > PAGE_SIZE) {
			for_each_sgt_page(page, sgt_iter, st)
				put_page(page);
			sg_free_table(st);

			max_segment = PAGE_SIZE;
			goto rebuild_st;
		} else {
			dev_warn(&dev_priv->drm.pdev->dev,
				 "Failed to DMA remap %lu pages\n",
				 page_count);
			goto err_pages;
		}
	}

	if (i915_gem_object_needs_bit17_swizzle(obj))
		i915_gem_object_do_bit_17_swizzle(obj, st);

	return st;

err_sg:
	sg_mark_end(sg);
err_pages:
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

	return ERR_PTR(ret);
}

void __i915_gem_object_set_pages(struct drm_i915_gem_object *obj,
				 struct sg_table *pages)
{
	lockdep_assert_held(&obj->mm.lock);

	obj->mm.get_page.sg_pos = pages->sgl;
	obj->mm.get_page.sg_idx = 0;

	obj->mm.pages = pages;

	if (i915_gem_object_is_tiled(obj) &&
	    to_i915(obj->base.dev)->quirks & QUIRK_PIN_SWIZZLED_PAGES) {
		GEM_BUG_ON(obj->mm.quirked);
		__i915_gem_object_pin_pages(obj);
		obj->mm.quirked = true;
	}
}

static int ____i915_gem_object_get_pages(struct drm_i915_gem_object *obj)
{
	struct sg_table *pages;

	GEM_BUG_ON(i915_gem_object_has_pinned_pages(obj));

	if (unlikely(obj->mm.madv != I915_MADV_WILLNEED)) {
		DRM_DEBUG("Attempting to obtain a purgeable object\n");
		return -EFAULT;
	}

	pages = obj->ops->get_pages(obj);
	if (unlikely(IS_ERR(pages)))
		return PTR_ERR(pages);

	__i915_gem_object_set_pages(obj, pages);
	return 0;
}

/* Ensure that the associated pages are gathered from the backing storage
 * and pinned into our object. i915_gem_object_pin_pages() may be called
 * multiple times before they are released by a single call to
 * i915_gem_object_unpin_pages() - once the pages are no longer referenced
 * either as a result of memory pressure (reaping pages under the shrinker)
 * or as the object is itself released.
 */
int __i915_gem_object_get_pages(struct drm_i915_gem_object *obj)
{
	int err;

	err = mutex_lock_interruptible(&obj->mm.lock);
	if (err)
		return err;

	if (unlikely(IS_ERR_OR_NULL(obj->mm.pages))) {
		err = ____i915_gem_object_get_pages(obj);
		if (err)
			goto unlock;

		smp_mb__before_atomic();
	}
	atomic_inc(&obj->mm.pages_pin_count);

unlock:
	mutex_unlock(&obj->mm.lock);
	return err;
}

/* The 'mapping' part of i915_gem_object_pin_map() below */
static void *i915_gem_object_map(const struct drm_i915_gem_object *obj,
				 enum i915_map_type type)
{
	unsigned long n_pages = obj->base.size >> PAGE_SHIFT;
	struct sg_table *sgt = obj->mm.pages;
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
		pages = kvmalloc_array(n_pages, sizeof(*pages), GFP_KERNEL);
		if (!pages)
			return NULL;
	}

	for_each_sgt_page(page, sgt_iter, sgt)
		pages[i++] = page;

	/* Check that we have the expected number of pages */
	GEM_BUG_ON(i != n_pages);

	switch (type) {
	default:
		MISSING_CASE(type);
		/* fallthrough to use PAGE_KERNEL anyway */
	case I915_MAP_WB:
		pgprot = PAGE_KERNEL;
		break;
	case I915_MAP_WC:
		pgprot = pgprot_writecombine(PAGE_KERNEL_IO);
		break;
	}
	addr = vmap(pages, n_pages, 0, pgprot);

	if (pages != stack_pages)
		kvfree(pages);

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

	GEM_BUG_ON(!i915_gem_object_has_struct_page(obj));

	ret = mutex_lock_interruptible(&obj->mm.lock);
	if (ret)
		return ERR_PTR(ret);

	pinned = !(type & I915_MAP_OVERRIDE);
	type &= ~I915_MAP_OVERRIDE;

	if (!atomic_inc_not_zero(&obj->mm.pages_pin_count)) {
		if (unlikely(IS_ERR_OR_NULL(obj->mm.pages))) {
			ret = ____i915_gem_object_get_pages(obj);
			if (ret)
				goto err_unlock;

			smp_mb__before_atomic();
		}
		atomic_inc(&obj->mm.pages_pin_count);
		pinned = false;
	}
	GEM_BUG_ON(!obj->mm.pages);

	ptr = page_unpack_bits(obj->mm.mapping, &has_type);
	if (ptr && has_type != type) {
		if (pinned) {
			ret = -EBUSY;
			goto err_unpin;
		}

		if (is_vmalloc_addr(ptr))
			vunmap(ptr);
		else
			kunmap(kmap_to_page(ptr));

		ptr = obj->mm.mapping = NULL;
	}

	if (!ptr) {
		ptr = i915_gem_object_map(obj, type);
		if (!ptr) {
			ret = -ENOMEM;
			goto err_unpin;
		}

		obj->mm.mapping = page_pack_bits(ptr, type);
	}

out_unlock:
	mutex_unlock(&obj->mm.lock);
	return ptr;

err_unpin:
	atomic_dec(&obj->mm.pages_pin_count);
err_unlock:
	ptr = ERR_PTR(ret);
	goto out_unlock;
}

static int
i915_gem_object_pwrite_gtt(struct drm_i915_gem_object *obj,
			   const struct drm_i915_gem_pwrite *arg)
{
	struct address_space *mapping = obj->base.filp->f_mapping;
	char __user *user_data = u64_to_user_ptr(arg->data_ptr);
	u64 remain, offset;
	unsigned int pg;

	/* Before we instantiate/pin the backing store for our use, we
	 * can prepopulate the shmemfs filp efficiently using a write into
	 * the pagecache. We avoid the penalty of instantiating all the
	 * pages, important if the user is just writing to a few and never
	 * uses the object on the GPU, and using a direct write into shmemfs
	 * allows it to avoid the cost of retrieving a page (either swapin
	 * or clearing-before-use) before it is overwritten.
	 */
	if (READ_ONCE(obj->mm.pages))
		return -ENODEV;

	/* Before the pages are instantiated the object is treated as being
	 * in the CPU domain. The pages will be clflushed as required before
	 * use, and we can freely write into the pages directly. If userspace
	 * races pwrite with any other operation; corruption will ensue -
	 * that is userspace's prerogative!
	 */

	remain = arg->size;
	offset = arg->offset;
	pg = offset_in_page(offset);

	do {
		unsigned int len, unwritten;
		struct page *page;
		void *data, *vaddr;
		int err;

		len = PAGE_SIZE - pg;
		if (len > remain)
			len = remain;

		err = pagecache_write_begin(obj->base.filp, mapping,
					    offset, len, 0,
					    &page, &data);
		if (err < 0)
			return err;

		vaddr = kmap(page);
		unwritten = copy_from_user(vaddr + pg, user_data, len);
		kunmap(page);

		err = pagecache_write_end(obj->base.filp, mapping,
					  offset, len, len - unwritten,
					  page, data);
		if (err < 0)
			return err;

		if (unwritten)
			return -EFAULT;

		remain -= len;
		user_data += len;
		offset += len;
		pg = 0;
	} while (remain);

	return 0;
}

static bool ban_context(const struct i915_gem_context *ctx,
			unsigned int score)
{
	return (i915_gem_context_is_bannable(ctx) &&
		score >= CONTEXT_SCORE_BAN_THRESHOLD);
}

static void i915_gem_context_mark_guilty(struct i915_gem_context *ctx)
{
	unsigned int score;
	bool banned;

	atomic_inc(&ctx->guilty_count);

	score = atomic_add_return(CONTEXT_SCORE_GUILTY, &ctx->ban_score);
	banned = ban_context(ctx, score);
	DRM_DEBUG_DRIVER("context %s marked guilty (score %d) banned? %s\n",
			 ctx->name, score, yesno(banned));
	if (!banned)
		return;

	i915_gem_context_set_banned(ctx);
	if (!IS_ERR_OR_NULL(ctx->file_priv)) {
		atomic_inc(&ctx->file_priv->context_bans);
		DRM_DEBUG_DRIVER("client %s has had %d context banned\n",
				 ctx->name, atomic_read(&ctx->file_priv->context_bans));
	}
}

static void i915_gem_context_mark_innocent(struct i915_gem_context *ctx)
{
	atomic_inc(&ctx->active_count);
}

struct drm_i915_gem_request *
i915_gem_find_active_request(struct intel_engine_cs *engine)
{
	struct drm_i915_gem_request *request, *active = NULL;
	unsigned long flags;

	/* We are called by the error capture and reset at a random
	 * point in time. In particular, note that neither is crucially
	 * ordered with an interrupt. After a hang, the GPU is dead and we
	 * assume that no more writes can happen (we waited long enough for
	 * all writes that were in transaction to be flushed) - adding an
	 * extra delay for a recent interrupt is pointless. Hence, we do
	 * not need an engine->irq_seqno_barrier() before the seqno reads.
	 */
	spin_lock_irqsave(&engine->timeline->lock, flags);
	list_for_each_entry(request, &engine->timeline->requests, link) {
		if (__i915_gem_request_completed(request,
						 request->global_seqno))
			continue;

		GEM_BUG_ON(request->engine != engine);
		GEM_BUG_ON(test_bit(DMA_FENCE_FLAG_SIGNALED_BIT,
				    &request->fence.flags));

		active = request;
		break;
	}
	spin_unlock_irqrestore(&engine->timeline->lock, flags);

	return active;
}

static bool engine_stalled(struct intel_engine_cs *engine)
{
	if (!engine->hangcheck.stalled)
		return false;

	/* Check for possible seqno movement after hang declaration */
	if (engine->hangcheck.seqno != intel_engine_get_seqno(engine)) {
		DRM_DEBUG_DRIVER("%s pardoned\n", engine->name);
		return false;
	}

	return true;
}

/*
 * Ensure irq handler finishes, and not run again.
 * Also return the active request so that we only search for it once.
 */
struct drm_i915_gem_request *
i915_gem_reset_prepare_engine(struct intel_engine_cs *engine)
{
	struct drm_i915_gem_request *request = NULL;

	/* Prevent the signaler thread from updating the request
	 * state (by calling dma_fence_signal) as we are processing
	 * the reset. The write from the GPU of the seqno is
	 * asynchronous and the signaler thread may see a different
	 * value to us and declare the request complete, even though
	 * the reset routine have picked that request as the active
	 * (incomplete) request. This conflict is not handled
	 * gracefully!
	 */
	kthread_park(engine->breadcrumbs.signaler);

	/* Prevent request submission to the hardware until we have
	 * completed the reset in i915_gem_reset_finish(). If a request
	 * is completed by one engine, it may then queue a request
	 * to a second via its engine->irq_tasklet *just* as we are
	 * calling engine->init_hw() and also writing the ELSP.
	 * Turning off the engine->irq_tasklet until the reset is over
	 * prevents the race.
	 */
	tasklet_kill(&engine->irq_tasklet);
	tasklet_disable(&engine->irq_tasklet);

	if (engine->irq_seqno_barrier)
		engine->irq_seqno_barrier(engine);

	request = i915_gem_find_active_request(engine);
	if (request && request->fence.error == -EIO)
		request = ERR_PTR(-EIO); /* Previous reset failed! */

	return request;
}

int i915_gem_reset_prepare(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *engine;
	struct drm_i915_gem_request *request;
	enum intel_engine_id id;
	int err = 0;

	for_each_engine(engine, dev_priv, id) {
		request = i915_gem_reset_prepare_engine(engine);
		if (IS_ERR(request)) {
			err = PTR_ERR(request);
			continue;
		}

		engine->hangcheck.active_request = request;
	}

	i915_gem_revoke_fences(dev_priv);

	return err;
}

static void skip_request(struct drm_i915_gem_request *request)
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

	dma_fence_set_error(&request->fence, -EIO);
}

static void engine_skip_context(struct drm_i915_gem_request *request)
{
	struct intel_engine_cs *engine = request->engine;
	struct i915_gem_context *hung_ctx = request->ctx;
	struct intel_timeline *timeline;
	unsigned long flags;

	timeline = i915_gem_context_lookup_timeline(hung_ctx, engine);

	spin_lock_irqsave(&engine->timeline->lock, flags);
	spin_lock(&timeline->lock);

	list_for_each_entry_continue(request, &engine->timeline->requests, link)
		if (request->ctx == hung_ctx)
			skip_request(request);

	list_for_each_entry(request, &timeline->requests, link)
		skip_request(request);

	spin_unlock(&timeline->lock);
	spin_unlock_irqrestore(&engine->timeline->lock, flags);
}

/* Returns the request if it was guilty of the hang */
static struct drm_i915_gem_request *
i915_gem_reset_request(struct intel_engine_cs *engine,
		       struct drm_i915_gem_request *request)
{
	/* The guilty request will get skipped on a hung engine.
	 *
	 * Users of client default contexts do not rely on logical
	 * state preserved between batches so it is safe to execute
	 * queued requests following the hang. Non default contexts
	 * rely on preserved state, so skipping a batch loses the
	 * evolution of the state and it needs to be considered corrupted.
	 * Executing more queued batches on top of corrupted state is
	 * risky. But we take the risk by trying to advance through
	 * the queued requests in order to make the client behaviour
	 * more predictable around resets, by not throwing away random
	 * amount of batches it has prepared for execution. Sophisticated
	 * clients can use gem_reset_stats_ioctl and dma fence status
	 * (exported via sync_file info ioctl on explicit fences) to observe
	 * when it loses the context state and should rebuild accordingly.
	 *
	 * The context ban, and ultimately the client ban, mechanism are safety
	 * valves if client submission ends up resulting in nothing more than
	 * subsequent hangs.
	 */

	if (engine_stalled(engine)) {
		i915_gem_context_mark_guilty(request->ctx);
		skip_request(request);

		/* If this context is now banned, skip all pending requests. */
		if (i915_gem_context_is_banned(request->ctx))
			engine_skip_context(request);
	} else {
		/*
		 * Since this is not the hung engine, it may have advanced
		 * since the hang declaration. Double check by refinding
		 * the active request at the time of the reset.
		 */
		request = i915_gem_find_active_request(engine);
		if (request) {
			i915_gem_context_mark_innocent(request->ctx);
			dma_fence_set_error(&request->fence, -EAGAIN);

			/* Rewind the engine to replay the incomplete rq */
			spin_lock_irq(&engine->timeline->lock);
			request = list_prev_entry(request, link);
			if (&request->link == &engine->timeline->requests)
				request = NULL;
			spin_unlock_irq(&engine->timeline->lock);
		}
	}

	return request;
}

void i915_gem_reset_engine(struct intel_engine_cs *engine,
			   struct drm_i915_gem_request *request)
{
	engine->irq_posted = 0;

	if (request)
		request = i915_gem_reset_request(engine, request);

	if (request) {
		DRM_DEBUG_DRIVER("resetting %s to restart from tail of request 0x%x\n",
				 engine->name, request->global_seqno);
	}

	/* Setup the CS to resume from the breadcrumb of the hung request */
	engine->reset_hw(engine, request);
}

void i915_gem_reset(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	lockdep_assert_held(&dev_priv->drm.struct_mutex);

	i915_gem_retire_requests(dev_priv);

	for_each_engine(engine, dev_priv, id) {
		struct i915_gem_context *ctx;

		i915_gem_reset_engine(engine, engine->hangcheck.active_request);
		ctx = fetch_and_zero(&engine->last_retired_context);
		if (ctx)
			engine->context_unpin(engine, ctx);
	}

	i915_gem_restore_fences(dev_priv);

	if (dev_priv->gt.awake) {
		intel_sanitize_gt_powersave(dev_priv);
		intel_enable_gt_powersave(dev_priv);
		if (INTEL_GEN(dev_priv) >= 6)
			gen6_rps_busy(dev_priv);
	}
}

void i915_gem_reset_finish_engine(struct intel_engine_cs *engine)
{
	tasklet_enable(&engine->irq_tasklet);
	kthread_unpark(engine->breadcrumbs.signaler);
}

void i915_gem_reset_finish(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	lockdep_assert_held(&dev_priv->drm.struct_mutex);

	for_each_engine(engine, dev_priv, id) {
		engine->hangcheck.active_request = NULL;
		i915_gem_reset_finish_engine(engine);
	}
}

static void nop_submit_request(struct drm_i915_gem_request *request)
{
	GEM_BUG_ON(!i915_terminally_wedged(&request->i915->gpu_error));
	dma_fence_set_error(&request->fence, -EIO);
	i915_gem_request_submit(request);
	intel_engine_init_global_seqno(request->engine, request->global_seqno);
}

static void engine_set_wedged(struct intel_engine_cs *engine)
{
	struct drm_i915_gem_request *request;
	unsigned long flags;

	/* We need to be sure that no thread is running the old callback as
	 * we install the nop handler (otherwise we would submit a request
	 * to hardware that will never complete). In order to prevent this
	 * race, we wait until the machine is idle before making the swap
	 * (using stop_machine()).
	 */
	engine->submit_request = nop_submit_request;

	/* Mark all executing requests as skipped */
	spin_lock_irqsave(&engine->timeline->lock, flags);
	list_for_each_entry(request, &engine->timeline->requests, link)
		if (!i915_gem_request_completed(request))
			dma_fence_set_error(&request->fence, -EIO);
	spin_unlock_irqrestore(&engine->timeline->lock, flags);

	/*
	 * Clear the execlists queue up before freeing the requests, as those
	 * are the ones that keep the context and ringbuffer backing objects
	 * pinned in place.
	 */

	if (i915.enable_execlists) {
		struct execlist_port *port = engine->execlist_port;
		unsigned long flags;
		unsigned int n;

		spin_lock_irqsave(&engine->timeline->lock, flags);

		for (n = 0; n < ARRAY_SIZE(engine->execlist_port); n++)
			i915_gem_request_put(port_request(&port[n]));
		memset(engine->execlist_port, 0, sizeof(engine->execlist_port));
		engine->execlist_queue = RB_ROOT;
		engine->execlist_first = NULL;

		spin_unlock_irqrestore(&engine->timeline->lock, flags);

		/* The port is checked prior to scheduling a tasklet, but
		 * just in case we have suspended the tasklet to do the
		 * wedging make sure that when it wakes, it decides there
		 * is no work to do by clearing the irq_posted bit.
		 */
		clear_bit(ENGINE_IRQ_EXECLIST, &engine->irq_posted);
	}

	/* Mark all pending requests as complete so that any concurrent
	 * (lockless) lookup doesn't try and wait upon the request as we
	 * reset it.
	 */
	intel_engine_init_global_seqno(engine,
				       intel_engine_last_submit(engine));
}

static int __i915_gem_set_wedged_BKL(void *data)
{
	struct drm_i915_private *i915 = data;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, i915, id)
		engine_set_wedged(engine);

	set_bit(I915_WEDGED, &i915->gpu_error.flags);
	wake_up_all(&i915->gpu_error.reset_queue);

	return 0;
}

void i915_gem_set_wedged(struct drm_i915_private *dev_priv)
{
	stop_machine(__i915_gem_set_wedged_BKL, dev_priv, NULL);
}

bool i915_gem_unset_wedged(struct drm_i915_private *i915)
{
	struct i915_gem_timeline *tl;
	int i;

	lockdep_assert_held(&i915->drm.struct_mutex);
	if (!test_bit(I915_WEDGED, &i915->gpu_error.flags))
		return true;

	/* Before unwedging, make sure that all pending operations
	 * are flushed and errored out - we may have requests waiting upon
	 * third party fences. We marked all inflight requests as EIO, and
	 * every execbuf since returned EIO, for consistency we want all
	 * the currently pending requests to also be marked as EIO, which
	 * is done inside our nop_submit_request - and so we must wait.
	 *
	 * No more can be submitted until we reset the wedged bit.
	 */
	list_for_each_entry(tl, &i915->gt.timelines, link) {
		for (i = 0; i < ARRAY_SIZE(tl->engine); i++) {
			struct drm_i915_gem_request *rq;

			rq = i915_gem_active_peek(&tl->engine[i].last_request,
						  &i915->drm.struct_mutex);
			if (!rq)
				continue;

			/* We can't use our normal waiter as we want to
			 * avoid recursively trying to handle the current
			 * reset. The basic dma_fence_default_wait() installs
			 * a callback for dma_fence_signal(), which is
			 * triggered by our nop handler (indirectly, the
			 * callback enables the signaler thread which is
			 * woken by the nop_submit_request() advancing the seqno
			 * and when the seqno passes the fence, the signaler
			 * then signals the fence waking us up).
			 */
			if (dma_fence_default_wait(&rq->fence, true,
						   MAX_SCHEDULE_TIMEOUT) < 0)
				return false;
		}
	}

	/* Undo nop_submit_request. We prevent all new i915 requests from
	 * being queued (by disallowing execbuf whilst wedged) so having
	 * waited for all active requests above, we know the system is idle
	 * and do not have to worry about a thread being inside
	 * engine->submit_request() as we swap over. So unlike installing
	 * the nop_submit_request on reset, we can do this from normal
	 * context and do not require stop_machine().
	 */
	intel_engines_reset_default_submission(i915);
	i915_gem_contexts_lost(i915);

	smp_mb__before_atomic(); /* complete takeover before enabling execbuf */
	clear_bit(I915_WEDGED, &i915->gpu_error.flags);

	return true;
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
	bool rearm_hangcheck;

	if (!READ_ONCE(dev_priv->gt.awake))
		return;

	/*
	 * Wait for last execlists context complete, but bail out in case a
	 * new request is submitted.
	 */
	wait_for(intel_engines_are_idle(dev_priv), 10);
	if (READ_ONCE(dev_priv->gt.active_requests))
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

	/*
	 * New request retired after this work handler started, extend active
	 * period until next instance of the work.
	 */
	if (work_pending(work))
		goto out_unlock;

	if (dev_priv->gt.active_requests)
		goto out_unlock;

	if (wait_for(intel_engines_are_idle(dev_priv), 10))
		DRM_ERROR("Timeout waiting for engines to idle\n");

	intel_engines_mark_idle(dev_priv);
	i915_gem_timelines_mark_idle(dev_priv);

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
	struct drm_i915_private *i915 = to_i915(gem->dev);
	struct drm_i915_gem_object *obj = to_intel_bo(gem);
	struct drm_i915_file_private *fpriv = file->driver_priv;
	struct i915_lut_handle *lut, *ln;

	mutex_lock(&i915->drm.struct_mutex);

	list_for_each_entry_safe(lut, ln, &obj->lut_list, obj_link) {
		struct i915_gem_context *ctx = lut->ctx;
		struct i915_vma *vma;

		if (ctx->file_priv != fpriv)
			continue;

		vma = radix_tree_delete(&ctx->handles_vma, lut->handle);

		GEM_BUG_ON(vma->obj != obj);

		/* We allow the process to have multiple handles to the same
		 * vma, in the same fd namespace, by virtue of flink/open.
		 */
		GEM_BUG_ON(!vma->open_count);
		if (!--vma->open_count && !i915_vma_is_ggtt(vma))
			i915_vma_close(vma);

		list_del(&lut->obj_link);
		list_del(&lut->ctx_link);

		kmem_cache_free(i915->luts, lut);
		__i915_gem_object_release_unless_active(obj);
	}

	mutex_unlock(&i915->drm.struct_mutex);
}

static unsigned long to_wait_timeout(s64 timeout_ns)
{
	if (timeout_ns < 0)
		return MAX_SCHEDULE_TIMEOUT;

	if (timeout_ns == 0)
		return 0;

	return nsecs_to_jiffies_timeout(timeout_ns);
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
 *  -EAGAIN: incomplete, restart syscall
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
	struct drm_i915_gem_object *obj;
	ktime_t start;
	long ret;

	if (args->flags != 0)
		return -EINVAL;

	obj = i915_gem_object_lookup(file, args->bo_handle);
	if (!obj)
		return -ENOENT;

	start = ktime_get();

	ret = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE | I915_WAIT_ALL,
				   to_wait_timeout(args->timeout_ns),
				   to_rps_client(file));

	if (args->timeout_ns > 0) {
		args->timeout_ns -= ktime_to_ns(ktime_sub(ktime_get(), start));
		if (args->timeout_ns < 0)
			args->timeout_ns = 0;

		/*
		 * Apparently ktime isn't accurate enough and occasionally has a
		 * bit of mismatch in the jiffies<->nsecs<->ktime loop. So patch
		 * things up to make the test happy. We allow up to 1 jiffy.
		 *
		 * This is a regression from the timespec->ktime conversion.
		 */
		if (ret == -ETIME && !nsecs_to_jiffies(args->timeout_ns))
			args->timeout_ns = 0;

		/* Asked to wait beyond the jiffie/scheduler precision? */
		if (ret == -ETIME && args->timeout_ns)
			ret = -EAGAIN;
	}

	i915_gem_object_put(obj);
	return ret;
}

static int wait_for_timeline(struct i915_gem_timeline *tl, unsigned int flags)
{
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(tl->engine); i++) {
		ret = i915_gem_active_wait(&tl->engine[i].last_request, flags);
		if (ret)
			return ret;
	}

	return 0;
}

static int wait_for_engine(struct intel_engine_cs *engine, int timeout_ms)
{
	return wait_for(intel_engine_is_idle(engine), timeout_ms);
}

static int wait_for_engines(struct drm_i915_private *i915)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, i915, id) {
		if (GEM_WARN_ON(wait_for_engine(engine, 50))) {
			i915_gem_set_wedged(i915);
			return -EIO;
		}

		GEM_BUG_ON(intel_engine_get_seqno(engine) !=
			   intel_engine_last_submit(engine));
	}

	return 0;
}

int i915_gem_wait_for_idle(struct drm_i915_private *i915, unsigned int flags)
{
	int ret;

	/* If the device is asleep, we have no requests outstanding */
	if (!READ_ONCE(i915->gt.awake))
		return 0;

	if (flags & I915_WAIT_LOCKED) {
		struct i915_gem_timeline *tl;

		lockdep_assert_held(&i915->drm.struct_mutex);

		list_for_each_entry(tl, &i915->gt.timelines, link) {
			ret = wait_for_timeline(tl, flags);
			if (ret)
				return ret;
		}

		i915_gem_retire_requests(i915);
		GEM_BUG_ON(i915->gt.active_requests);

		ret = wait_for_engines(i915);
	} else {
		ret = wait_for_timeline(&i915->gt.global_timeline, flags);
	}

	return ret;
}

static void __i915_gem_object_flush_for_display(struct drm_i915_gem_object *obj)
{
	/*
	 * We manually flush the CPU domain so that we can override and
	 * force the flush for the display, and perform it asyncrhonously.
	 */
	flush_write_domain(obj, ~I915_GEM_DOMAIN_CPU);
	if (obj->cache_dirty)
		i915_gem_clflush_object(obj, I915_CLFLUSH_FORCE);
	obj->base.write_domain = 0;
}

void i915_gem_object_flush_if_display(struct drm_i915_gem_object *obj)
{
	if (!READ_ONCE(obj->pin_display))
		return;

	mutex_lock(&obj->base.dev->struct_mutex);
	__i915_gem_object_flush_for_display(obj);
	mutex_unlock(&obj->base.dev->struct_mutex);
}

/**
 * Moves a single object to the WC read, and possibly write domain.
 * @obj: object to act on
 * @write: ask for write access or read only
 *
 * This function returns when the move is complete, including waiting on
 * flushes to occur.
 */
int
i915_gem_object_set_to_wc_domain(struct drm_i915_gem_object *obj, bool write)
{
	int ret;

	lockdep_assert_held(&obj->base.dev->struct_mutex);

	ret = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE |
				   I915_WAIT_LOCKED |
				   (write ? I915_WAIT_ALL : 0),
				   MAX_SCHEDULE_TIMEOUT,
				   NULL);
	if (ret)
		return ret;

	if (obj->base.write_domain == I915_GEM_DOMAIN_WC)
		return 0;

	/* Flush and acquire obj->pages so that we are coherent through
	 * direct access in memory with previous cached writes through
	 * shmemfs and that our cache domain tracking remains valid.
	 * For example, if the obj->filp was moved to swap without us
	 * being notified and releasing the pages, we would mistakenly
	 * continue to assume that the obj remained out of the CPU cached
	 * domain.
	 */
	ret = i915_gem_object_pin_pages(obj);
	if (ret)
		return ret;

	flush_write_domain(obj, ~I915_GEM_DOMAIN_WC);

	/* Serialise direct access to this object with the barriers for
	 * coherent writes from the GPU, by effectively invalidating the
	 * WC domain upon first access.
	 */
	if ((obj->base.read_domains & I915_GEM_DOMAIN_WC) == 0)
		mb();

	/* It should now be out of any other write domains, and we can update
	 * the domain values for our changes.
	 */
	GEM_BUG_ON((obj->base.write_domain & ~I915_GEM_DOMAIN_WC) != 0);
	obj->base.read_domains |= I915_GEM_DOMAIN_WC;
	if (write) {
		obj->base.read_domains = I915_GEM_DOMAIN_WC;
		obj->base.write_domain = I915_GEM_DOMAIN_WC;
		obj->mm.dirty = true;
	}

	i915_gem_object_unpin_pages(obj);
	return 0;
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
	int ret;

	lockdep_assert_held(&obj->base.dev->struct_mutex);

	ret = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE |
				   I915_WAIT_LOCKED |
				   (write ? I915_WAIT_ALL : 0),
				   MAX_SCHEDULE_TIMEOUT,
				   NULL);
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
	ret = i915_gem_object_pin_pages(obj);
	if (ret)
		return ret;

	flush_write_domain(obj, ~I915_GEM_DOMAIN_GTT);

	/* Serialise direct access to this object with the barriers for
	 * coherent writes from the GPU, by effectively invalidating the
	 * GTT domain upon first access.
	 */
	if ((obj->base.read_domains & I915_GEM_DOMAIN_GTT) == 0)
		mb();

	/* It should now be out of any other write domains, and we can update
	 * the domain values for our changes.
	 */
	GEM_BUG_ON((obj->base.write_domain & ~I915_GEM_DOMAIN_GTT) != 0);
	obj->base.read_domains |= I915_GEM_DOMAIN_GTT;
	if (write) {
		obj->base.read_domains = I915_GEM_DOMAIN_GTT;
		obj->base.write_domain = I915_GEM_DOMAIN_GTT;
		obj->mm.dirty = true;
	}

	i915_gem_object_unpin_pages(obj);
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
	int ret;

	lockdep_assert_held(&obj->base.dev->struct_mutex);

	if (obj->cache_level == cache_level)
		return 0;

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
		ret = i915_gem_object_wait(obj,
					   I915_WAIT_INTERRUPTIBLE |
					   I915_WAIT_LOCKED |
					   I915_WAIT_ALL,
					   MAX_SCHEDULE_TIMEOUT,
					   NULL);
		if (ret)
			return ret;

		if (!HAS_LLC(to_i915(obj->base.dev)) &&
		    cache_level != I915_CACHE_NONE) {
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
	i915_gem_object_set_cache_coherency(obj, cache_level);
	obj->cache_dirty = true; /* Always invalidate stale cachelines */

	return 0;
}

int i915_gem_get_caching_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file)
{
	struct drm_i915_gem_caching *args = data;
	struct drm_i915_gem_object *obj;
	int err = 0;

	rcu_read_lock();
	obj = i915_gem_object_lookup_rcu(file, args->handle);
	if (!obj) {
		err = -ENOENT;
		goto out;
	}

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
out:
	rcu_read_unlock();
	return err;
}

int i915_gem_set_caching_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file)
{
	struct drm_i915_private *i915 = to_i915(dev);
	struct drm_i915_gem_caching *args = data;
	struct drm_i915_gem_object *obj;
	enum i915_cache_level level;
	int ret = 0;

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

	obj = i915_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	if (obj->cache_level == level)
		goto out;

	ret = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE,
				   MAX_SCHEDULE_TIMEOUT,
				   to_rps_client(file));
	if (ret)
		goto out;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		goto out;

	ret = i915_gem_object_set_cache_level(obj, level);
	mutex_unlock(&dev->struct_mutex);

out:
	i915_gem_object_put(obj);
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
	int ret;

	lockdep_assert_held(&obj->base.dev->struct_mutex);

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
	if (!view || view->type == I915_GGTT_VIEW_NORMAL)
		vma = i915_gem_object_ggtt_pin(obj, view, 0, alignment,
					       PIN_MAPPABLE | PIN_NONBLOCK);
	if (IS_ERR(vma)) {
		struct drm_i915_private *i915 = to_i915(obj->base.dev);
		unsigned int flags;

		/* Valleyview is definitely limited to scanning out the first
		 * 512MiB. Lets presume this behaviour was inherited from the
		 * g4x display engine and that all earlier gen are similarly
		 * limited. Testing suggests that it is a little more
		 * complicated than this. For example, Cherryview appears quite
		 * happy to scanout from anywhere within its global aperture.
		 */
		flags = 0;
		if (HAS_GMCH_DISPLAY(i915))
			flags = PIN_MAPPABLE;
		vma = i915_gem_object_ggtt_pin(obj, view, 0, alignment, flags);
	}
	if (IS_ERR(vma))
		goto err_unpin_display;

	vma->display_alignment = max_t(u64, vma->display_alignment, alignment);

	/* Treat this as an end-of-frame, like intel_user_framebuffer_dirty() */
	__i915_gem_object_flush_for_display(obj);
	intel_fb_obj_flush(obj, ORIGIN_DIRTYFB);

	/* It should now be out of any other write domains, and we can update
	 * the domain values for our changes.
	 */
	obj->base.read_domains |= I915_GEM_DOMAIN_GTT;

	return vma;

err_unpin_display:
	obj->pin_display--;
	return vma;
}

void
i915_gem_object_unpin_from_display_plane(struct i915_vma *vma)
{
	lockdep_assert_held(&vma->vm->i915->drm.struct_mutex);

	if (WARN_ON(vma->obj->pin_display == 0))
		return;

	if (--vma->obj->pin_display == 0)
		vma->display_alignment = I915_GTT_MIN_ALIGNMENT;

	/* Bump the LRU to try and avoid premature eviction whilst flipping  */
	i915_gem_object_bump_inactive_ggtt(vma->obj);

	i915_vma_unpin(vma);
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
	int ret;

	lockdep_assert_held(&obj->base.dev->struct_mutex);

	ret = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE |
				   I915_WAIT_LOCKED |
				   (write ? I915_WAIT_ALL : 0),
				   MAX_SCHEDULE_TIMEOUT,
				   NULL);
	if (ret)
		return ret;

	flush_write_domain(obj, ~I915_GEM_DOMAIN_CPU);

	/* Flush the CPU cache if it's still invalid. */
	if ((obj->base.read_domains & I915_GEM_DOMAIN_CPU) == 0) {
		i915_gem_clflush_object(obj, I915_CLFLUSH_SYNC);
		obj->base.read_domains |= I915_GEM_DOMAIN_CPU;
	}

	/* It should now be out of any other write domains, and we can update
	 * the domain values for our changes.
	 */
	GEM_BUG_ON(obj->base.write_domain & ~I915_GEM_DOMAIN_CPU);

	/* If we're writing through the CPU, then the GPU read domains will
	 * need to be invalidated at next use.
	 */
	if (write)
		__start_cpu_write(obj);

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
	long ret;

	/* ABI: return -EIO if already wedged */
	if (i915_terminally_wedged(&dev_priv->gpu_error))
		return -EIO;

	spin_lock(&file_priv->mm.lock);
	list_for_each_entry(request, &file_priv->mm.request_list, client_link) {
		if (time_after_eq(request->emitted_jiffies, recent_enough))
			break;

		if (target) {
			list_del(&target->client_link);
			target->file_priv = NULL;
		}

		target = request;
	}
	if (target)
		i915_gem_request_get(target);
	spin_unlock(&file_priv->mm.lock);

	if (target == NULL)
		return 0;

	ret = i915_wait_request(target,
				I915_WAIT_INTERRUPTIBLE,
				MAX_SCHEDULE_TIMEOUT);
	i915_gem_request_put(target);

	return ret < 0 ? ret : 0;
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

	lockdep_assert_held(&obj->base.dev->struct_mutex);

	vma = i915_vma_instance(obj, vm, view);
	if (unlikely(IS_ERR(vma)))
		return vma;

	if (i915_vma_misplaced(vma, size, alignment, flags)) {
		if (flags & PIN_NONBLOCK &&
		    (i915_vma_is_pinned(vma) || i915_vma_is_active(vma)))
			return ERR_PTR(-ENOSPC);

		if (flags & PIN_MAPPABLE) {
			/* If the required space is larger than the available
			 * aperture, we will not able to find a slot for the
			 * object and unbinding the object now will be in
			 * vain. Worse, doing so may cause us to ping-pong
			 * the object in and out of the Global GTT and
			 * waste a lot of cycles under the mutex.
			 */
			if (vma->fence_size > dev_priv->ggtt.mappable_end)
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
			    vma->fence_size > dev_priv->ggtt.mappable_end / 2)
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
__busy_set_if_active(const struct dma_fence *fence,
		     unsigned int (*flag)(unsigned int id))
{
	struct drm_i915_gem_request *rq;

	/* We have to check the current hw status of the fence as the uABI
	 * guarantees forward progress. We could rely on the idle worker
	 * to eventually flush us, but to minimise latency just ask the
	 * hardware.
	 *
	 * Note we only report on the status of native fences.
	 */
	if (!dma_fence_is_i915(fence))
		return 0;

	/* opencode to_request() in order to avoid const warnings */
	rq = container_of(fence, struct drm_i915_gem_request, fence);
	if (i915_gem_request_completed(rq))
		return 0;

	return flag(rq->engine->uabi_id);
}

static __always_inline unsigned int
busy_check_reader(const struct dma_fence *fence)
{
	return __busy_set_if_active(fence, __busy_read_flag);
}

static __always_inline unsigned int
busy_check_writer(const struct dma_fence *fence)
{
	if (!fence)
		return 0;

	return __busy_set_if_active(fence, __busy_write_id);
}

int
i915_gem_busy_ioctl(struct drm_device *dev, void *data,
		    struct drm_file *file)
{
	struct drm_i915_gem_busy *args = data;
	struct drm_i915_gem_object *obj;
	struct reservation_object_list *list;
	unsigned int seq;
	int err;

	err = -ENOENT;
	rcu_read_lock();
	obj = i915_gem_object_lookup_rcu(file, args->handle);
	if (!obj)
		goto out;

	/* A discrepancy here is that we do not report the status of
	 * non-i915 fences, i.e. even though we may report the object as idle,
	 * a call to set-domain may still stall waiting for foreign rendering.
	 * This also means that wait-ioctl may report an object as busy,
	 * where busy-ioctl considers it idle.
	 *
	 * We trade the ability to warn of foreign fences to report on which
	 * i915 engines are active for the object.
	 *
	 * Alternatively, we can trade that extra information on read/write
	 * activity with
	 *	args->busy =
	 *		!reservation_object_test_signaled_rcu(obj->resv, true);
	 * to report the overall busyness. This is what the wait-ioctl does.
	 *
	 */
retry:
	seq = raw_read_seqcount(&obj->resv->seq);

	/* Translate the exclusive fence to the READ *and* WRITE engine */
	args->busy = busy_check_writer(rcu_dereference(obj->resv->fence_excl));

	/* Translate shared fences to READ set of engines */
	list = rcu_dereference(obj->resv->fence);
	if (list) {
		unsigned int shared_count = list->shared_count, i;

		for (i = 0; i < shared_count; ++i) {
			struct dma_fence *fence =
				rcu_dereference(list->shared[i]);

			args->busy |= busy_check_reader(fence);
		}
	}

	if (args->busy && read_seqcount_retry(&obj->resv->seq, seq))
		goto retry;

	err = 0;
out:
	rcu_read_unlock();
	return err;
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
	int err;

	switch (args->madv) {
	case I915_MADV_DONTNEED:
	case I915_MADV_WILLNEED:
	    break;
	default:
	    return -EINVAL;
	}

	obj = i915_gem_object_lookup(file_priv, args->handle);
	if (!obj)
		return -ENOENT;

	err = mutex_lock_interruptible(&obj->mm.lock);
	if (err)
		goto out;

	if (obj->mm.pages &&
	    i915_gem_object_is_tiled(obj) &&
	    dev_priv->quirks & QUIRK_PIN_SWIZZLED_PAGES) {
		if (obj->mm.madv == I915_MADV_WILLNEED) {
			GEM_BUG_ON(!obj->mm.quirked);
			__i915_gem_object_unpin_pages(obj);
			obj->mm.quirked = false;
		}
		if (args->madv == I915_MADV_WILLNEED) {
			GEM_BUG_ON(obj->mm.quirked);
			__i915_gem_object_pin_pages(obj);
			obj->mm.quirked = true;
		}
	}

	if (obj->mm.madv != __I915_MADV_PURGED)
		obj->mm.madv = args->madv;

	/* if the object is no longer attached, discard its backing storage */
	if (obj->mm.madv == I915_MADV_DONTNEED && !obj->mm.pages)
		i915_gem_object_truncate(obj);

	args->retained = obj->mm.madv != __I915_MADV_PURGED;
	mutex_unlock(&obj->mm.lock);

out:
	i915_gem_object_put(obj);
	return err;
}

static void
frontbuffer_retire(struct i915_gem_active *active,
		   struct drm_i915_gem_request *request)
{
	struct drm_i915_gem_object *obj =
		container_of(active, typeof(*obj), frontbuffer_write);

	intel_fb_obj_flush(obj, ORIGIN_CS);
}

void i915_gem_object_init(struct drm_i915_gem_object *obj,
			  const struct drm_i915_gem_object_ops *ops)
{
	mutex_init(&obj->mm.lock);

	INIT_LIST_HEAD(&obj->global_link);
	INIT_LIST_HEAD(&obj->userfault_link);
	INIT_LIST_HEAD(&obj->vma_list);
	INIT_LIST_HEAD(&obj->lut_list);
	INIT_LIST_HEAD(&obj->batch_pool_link);

	obj->ops = ops;

	reservation_object_init(&obj->__builtin_resv);
	obj->resv = &obj->__builtin_resv;

	obj->frontbuffer_ggtt_origin = ORIGIN_GTT;
	init_request_active(&obj->frontbuffer_write, frontbuffer_retire);

	obj->mm.madv = I915_MADV_WILLNEED;
	INIT_RADIX_TREE(&obj->mm.get_page.radix, GFP_KERNEL | __GFP_NOWARN);
	mutex_init(&obj->mm.get_page.lock);

	i915_gem_info_add_obj(to_i915(obj->base.dev), obj->base.size);
}

static const struct drm_i915_gem_object_ops i915_gem_object_ops = {
	.flags = I915_GEM_OBJECT_HAS_STRUCT_PAGE |
		 I915_GEM_OBJECT_IS_SHRINKABLE,

	.get_pages = i915_gem_object_get_pages_gtt,
	.put_pages = i915_gem_object_put_pages_gtt,

	.pwrite = i915_gem_object_pwrite_gtt,
};

struct drm_i915_gem_object *
i915_gem_object_create(struct drm_i915_private *dev_priv, u64 size)
{
	struct drm_i915_gem_object *obj;
	struct address_space *mapping;
	unsigned int cache_level;
	gfp_t mask;
	int ret;

	/* There is a prevalence of the assumption that we fit the object's
	 * page count inside a 32bit _signed_ variable. Let's document this and
	 * catch if we ever need to fix it. In the meantime, if you do spot
	 * such a local variable, please consider fixing!
	 */
	if (size >> PAGE_SHIFT > INT_MAX)
		return ERR_PTR(-E2BIG);

	if (overflows_type(size, obj->base.size))
		return ERR_PTR(-E2BIG);

	obj = i915_gem_object_alloc(dev_priv);
	if (obj == NULL)
		return ERR_PTR(-ENOMEM);

	ret = drm_gem_object_init(&dev_priv->drm, &obj->base, size);
	if (ret)
		goto fail;

	mask = GFP_HIGHUSER | __GFP_RECLAIMABLE;
	if (IS_I965GM(dev_priv) || IS_I965G(dev_priv)) {
		/* 965gm cannot relocate objects above 4GiB. */
		mask &= ~__GFP_HIGHMEM;
		mask |= __GFP_DMA32;
	}

	mapping = obj->base.filp->f_mapping;
	mapping_set_gfp_mask(mapping, mask);
	GEM_BUG_ON(!(mapping_gfp_mask(mapping) & __GFP_RECLAIM));

	i915_gem_object_init(obj, &i915_gem_object_ops);

	obj->base.write_domain = I915_GEM_DOMAIN_CPU;
	obj->base.read_domains = I915_GEM_DOMAIN_CPU;

	if (HAS_LLC(dev_priv))
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
		cache_level = I915_CACHE_LLC;
	else
		cache_level = I915_CACHE_NONE;

	i915_gem_object_set_cache_coherency(obj, cache_level);

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

	if (obj->mm.madv != I915_MADV_WILLNEED)
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

static void __i915_gem_free_objects(struct drm_i915_private *i915,
				    struct llist_node *freed)
{
	struct drm_i915_gem_object *obj, *on;

	mutex_lock(&i915->drm.struct_mutex);
	intel_runtime_pm_get(i915);
	llist_for_each_entry(obj, freed, freed) {
		struct i915_vma *vma, *vn;

		trace_i915_gem_object_destroy(obj);

		GEM_BUG_ON(i915_gem_object_is_active(obj));
		list_for_each_entry_safe(vma, vn,
					 &obj->vma_list, obj_link) {
			GEM_BUG_ON(i915_vma_is_active(vma));
			vma->flags &= ~I915_VMA_PIN_MASK;
			i915_vma_close(vma);
		}
		GEM_BUG_ON(!list_empty(&obj->vma_list));
		GEM_BUG_ON(!RB_EMPTY_ROOT(&obj->vma_tree));

		list_del(&obj->global_link);
	}
	intel_runtime_pm_put(i915);
	mutex_unlock(&i915->drm.struct_mutex);

	cond_resched();

	llist_for_each_entry_safe(obj, on, freed, freed) {
		GEM_BUG_ON(obj->bind_count);
		GEM_BUG_ON(atomic_read(&obj->frontbuffer_bits));

		if (obj->ops->release)
			obj->ops->release(obj);

		if (WARN_ON(i915_gem_object_has_pinned_pages(obj)))
			atomic_set(&obj->mm.pages_pin_count, 0);
		__i915_gem_object_put_pages(obj, I915_MM_NORMAL);
		GEM_BUG_ON(obj->mm.pages);

		if (obj->base.import_attach)
			drm_prime_gem_destroy(&obj->base, NULL);

		reservation_object_fini(&obj->__builtin_resv);
		drm_gem_object_release(&obj->base);
		i915_gem_info_remove_obj(i915, obj->base.size);

		kfree(obj->bit_17);
		i915_gem_object_free(obj);
	}
}

static void i915_gem_flush_free_objects(struct drm_i915_private *i915)
{
	struct llist_node *freed;

	freed = llist_del_all(&i915->mm.free_list);
	if (unlikely(freed))
		__i915_gem_free_objects(i915, freed);
}

static void __i915_gem_free_work(struct work_struct *work)
{
	struct drm_i915_private *i915 =
		container_of(work, struct drm_i915_private, mm.free_work);
	struct llist_node *freed;

	/* All file-owned VMA should have been released by this point through
	 * i915_gem_close_object(), or earlier by i915_gem_context_close().
	 * However, the object may also be bound into the global GTT (e.g.
	 * older GPUs without per-process support, or for direct access through
	 * the GTT either for the user or for scanout). Those VMA still need to
	 * unbound now.
	 */

	while ((freed = llist_del_all(&i915->mm.free_list))) {
		__i915_gem_free_objects(i915, freed);
		if (need_resched())
			break;
	}
}

static void __i915_gem_free_object_rcu(struct rcu_head *head)
{
	struct drm_i915_gem_object *obj =
		container_of(head, typeof(*obj), rcu);
	struct drm_i915_private *i915 = to_i915(obj->base.dev);

	/* We can't simply use call_rcu() from i915_gem_free_object()
	 * as we need to block whilst unbinding, and the call_rcu
	 * task may be called from softirq context. So we take a
	 * detour through a worker.
	 */
	if (llist_add(&obj->freed, &i915->mm.free_list))
		schedule_work(&i915->mm.free_work);
}

void i915_gem_free_object(struct drm_gem_object *gem_obj)
{
	struct drm_i915_gem_object *obj = to_intel_bo(gem_obj);

	if (obj->mm.quirked)
		__i915_gem_object_unpin_pages(obj);

	if (discard_backing_storage(obj))
		obj->mm.madv = I915_MADV_DONTNEED;

	/* Before we free the object, make sure any pure RCU-only
	 * read-side critical sections are complete, e.g.
	 * i915_gem_busy_ioctl(). For the corresponding synchronized
	 * lookup see i915_gem_object_lookup_rcu().
	 */
	call_rcu(&obj->rcu, __i915_gem_free_object_rcu);
}

void __i915_gem_object_release_unless_active(struct drm_i915_gem_object *obj)
{
	lockdep_assert_held(&obj->base.dev->struct_mutex);

	if (!i915_gem_object_has_active_reference(obj) &&
	    i915_gem_object_is_active(obj))
		i915_gem_object_set_active_reference(obj);
	else
		i915_gem_object_put(obj);
}

static void assert_kernel_context_is_current(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, dev_priv, id)
		GEM_BUG_ON(engine->last_retired_context &&
			   !i915_gem_context_is_kernel(engine->last_retired_context));
}

void i915_gem_sanitize(struct drm_i915_private *i915)
{
	/*
	 * If we inherit context state from the BIOS or earlier occupants
	 * of the GPU, the GPU may be in an inconsistent state when we
	 * try to take over. The only way to remove the earlier state
	 * is by resetting. However, resetting on earlier gen is tricky as
	 * it may impact the display and we are uncertain about the stability
	 * of the reset, so this could be applied to even earlier gen.
	 */
	if (INTEL_GEN(i915) >= 5) {
		int reset = intel_gpu_reset(i915, ALL_ENGINES);
		WARN_ON(reset && reset != -ENODEV);
	}
}

int i915_gem_suspend(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = &dev_priv->drm;
	int ret;

	intel_runtime_pm_get(dev_priv);
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
		goto err_unlock;

	ret = i915_gem_wait_for_idle(dev_priv,
				     I915_WAIT_INTERRUPTIBLE |
				     I915_WAIT_LOCKED);
	if (ret)
		goto err_unlock;

	assert_kernel_context_is_current(dev_priv);
	i915_gem_contexts_lost(dev_priv);
	mutex_unlock(&dev->struct_mutex);

	intel_guc_suspend(dev_priv);

	cancel_delayed_work_sync(&dev_priv->gpu_error.hangcheck_work);
	cancel_delayed_work_sync(&dev_priv->gt.retire_work);

	/* As the idle_work is rearming if it detects a race, play safe and
	 * repeat the flush until it is definitely idle.
	 */
	while (flush_delayed_work(&dev_priv->gt.idle_work))
		;

	/* Assert that we sucessfully flushed all the work and
	 * reset the GPU back to its idle, low power state.
	 */
	WARN_ON(dev_priv->gt.awake);
	WARN_ON(!intel_engines_are_idle(dev_priv));

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
	i915_gem_sanitize(dev_priv);
	goto out_rpm_put;

err_unlock:
	mutex_unlock(&dev->struct_mutex);
out_rpm_put:
	intel_runtime_pm_put(dev_priv);
	return ret;
}

void i915_gem_resume(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = &dev_priv->drm;

	WARN_ON(dev_priv->gt.awake);

	mutex_lock(&dev->struct_mutex);
	i915_gem_restore_gtt_mappings(dev_priv);

	/* As we didn't flush the kernel context before suspend, we cannot
	 * guarantee that the context image is complete. So let's just reset
	 * it and start again.
	 */
	dev_priv->gt.resume(dev_priv);

	mutex_unlock(&dev->struct_mutex);
}

void i915_gem_init_swizzling(struct drm_i915_private *dev_priv)
{
	if (INTEL_GEN(dev_priv) < 5 ||
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

static int __i915_gem_restart_engines(void *data)
{
	struct drm_i915_private *i915 = data;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err;

	for_each_engine(engine, i915, id) {
		err = engine->init_hw(engine);
		if (err)
			return err;
	}

	return 0;
}

int i915_gem_init_hw(struct drm_i915_private *dev_priv)
{
	int ret;

	dev_priv->gt.last_init_time = ktime_get();

	/* Double layer security blanket, see i915_gem_init() */
	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);

	if (HAS_EDRAM(dev_priv) && INTEL_GEN(dev_priv) < 9)
		I915_WRITE(HSW_IDICR, I915_READ(HSW_IDICR) | IDIHASHMSK(0xf));

	if (IS_HASWELL(dev_priv))
		I915_WRITE(MI_PREDICATE_RESULT_2, IS_HSW_GT3(dev_priv) ?
			   LOWER_SLICE_ENABLED : LOWER_SLICE_DISABLED);

	if (HAS_PCH_NOP(dev_priv)) {
		if (IS_IVYBRIDGE(dev_priv)) {
			u32 temp = I915_READ(GEN7_MSG_CTL);
			temp &= ~(WAIT_FOR_PCH_FLR_ACK | WAIT_FOR_PCH_RESET_ACK);
			I915_WRITE(GEN7_MSG_CTL, temp);
		} else if (INTEL_GEN(dev_priv) >= 7) {
			u32 temp = I915_READ(HSW_NDE_RSTWRN_OPT);
			temp &= ~RESET_PCH_HANDSHAKE_ENABLE;
			I915_WRITE(HSW_NDE_RSTWRN_OPT, temp);
		}
	}

	i915_gem_init_swizzling(dev_priv);

	/*
	 * At least 830 can leave some of the unused rings
	 * "active" (ie. head != tail) after resume which
	 * will prevent c3 entry. Makes sure all unused rings
	 * are totally idle.
	 */
	init_unused_rings(dev_priv);

	BUG_ON(!dev_priv->kernel_context);

	ret = i915_ppgtt_init_hw(dev_priv);
	if (ret) {
		DRM_ERROR("PPGTT enable HW failed %d\n", ret);
		goto out;
	}

	/* Need to do basic initialisation of all rings first: */
	ret = __i915_gem_restart_engines(dev_priv);
	if (ret)
		goto out;

	intel_mocs_init_l3cc_table(dev_priv);

	/* We can't enable contexts until all firmware is loaded */
	ret = intel_uc_init_hw(dev_priv);
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

	/* Enable semaphores on SNB when IO remapping is off */
	if (IS_GEN6(dev_priv) && intel_vtd_active())
		return false;

	return true;
}

int i915_gem_init(struct drm_i915_private *dev_priv)
{
	int ret;

	mutex_lock(&dev_priv->drm.struct_mutex);

	dev_priv->mm.unordered_timeline = dma_fence_context_alloc(1);

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

	ret = i915_gem_init_userptr(dev_priv);
	if (ret)
		goto out_unlock;

	ret = i915_gem_init_ggtt(dev_priv);
	if (ret)
		goto out_unlock;

	ret = i915_gem_contexts_init(dev_priv);
	if (ret)
		goto out_unlock;

	ret = intel_engines_init(dev_priv);
	if (ret)
		goto out_unlock;

	ret = i915_gem_init_hw(dev_priv);
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
	mutex_unlock(&dev_priv->drm.struct_mutex);

	return ret;
}

void i915_gem_init_mmio(struct drm_i915_private *i915)
{
	i915_gem_sanitize(i915);
}

void
i915_gem_cleanup_engines(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, dev_priv, id)
		dev_priv->gt.cleanup_engine(engine);
}

void
i915_gem_load_init_fences(struct drm_i915_private *dev_priv)
{
	int i;

	if (INTEL_INFO(dev_priv)->gen >= 7 && !IS_VALLEYVIEW(dev_priv) &&
	    !IS_CHERRYVIEW(dev_priv))
		dev_priv->num_fence_regs = 32;
	else if (INTEL_INFO(dev_priv)->gen >= 4 ||
		 IS_I945G(dev_priv) || IS_I945GM(dev_priv) ||
		 IS_G33(dev_priv) || IS_PINEVIEW(dev_priv))
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
	i915_gem_restore_fences(dev_priv);

	i915_gem_detect_bit_6_swizzle(dev_priv);
}

int
i915_gem_load_init(struct drm_i915_private *dev_priv)
{
	int err = -ENOMEM;

	dev_priv->objects = KMEM_CACHE(drm_i915_gem_object, SLAB_HWCACHE_ALIGN);
	if (!dev_priv->objects)
		goto err_out;

	dev_priv->vmas = KMEM_CACHE(i915_vma, SLAB_HWCACHE_ALIGN);
	if (!dev_priv->vmas)
		goto err_objects;

	dev_priv->luts = KMEM_CACHE(i915_lut_handle, 0);
	if (!dev_priv->luts)
		goto err_vmas;

	dev_priv->requests = KMEM_CACHE(drm_i915_gem_request,
					SLAB_HWCACHE_ALIGN |
					SLAB_RECLAIM_ACCOUNT |
					SLAB_TYPESAFE_BY_RCU);
	if (!dev_priv->requests)
		goto err_luts;

	dev_priv->dependencies = KMEM_CACHE(i915_dependency,
					    SLAB_HWCACHE_ALIGN |
					    SLAB_RECLAIM_ACCOUNT);
	if (!dev_priv->dependencies)
		goto err_requests;

	dev_priv->priorities = KMEM_CACHE(i915_priolist, SLAB_HWCACHE_ALIGN);
	if (!dev_priv->priorities)
		goto err_dependencies;

	mutex_lock(&dev_priv->drm.struct_mutex);
	INIT_LIST_HEAD(&dev_priv->gt.timelines);
	err = i915_gem_timeline_init__global(dev_priv);
	mutex_unlock(&dev_priv->drm.struct_mutex);
	if (err)
		goto err_priorities;

	INIT_WORK(&dev_priv->mm.free_work, __i915_gem_free_work);
	init_llist_head(&dev_priv->mm.free_list);
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

	atomic_set(&dev_priv->mm.bsd_engine_dispatch_index, 0);

	spin_lock_init(&dev_priv->fb_tracking.lock);

	return 0;

err_priorities:
	kmem_cache_destroy(dev_priv->priorities);
err_dependencies:
	kmem_cache_destroy(dev_priv->dependencies);
err_requests:
	kmem_cache_destroy(dev_priv->requests);
err_luts:
	kmem_cache_destroy(dev_priv->luts);
err_vmas:
	kmem_cache_destroy(dev_priv->vmas);
err_objects:
	kmem_cache_destroy(dev_priv->objects);
err_out:
	return err;
}

void i915_gem_load_cleanup(struct drm_i915_private *dev_priv)
{
	i915_gem_drain_freed_objects(dev_priv);
	WARN_ON(!llist_empty(&dev_priv->mm.free_list));
	WARN_ON(dev_priv->mm.object_count);

	mutex_lock(&dev_priv->drm.struct_mutex);
	i915_gem_timeline_fini(&dev_priv->gt.global_timeline);
	WARN_ON(!list_empty(&dev_priv->gt.timelines));
	mutex_unlock(&dev_priv->drm.struct_mutex);

	kmem_cache_destroy(dev_priv->priorities);
	kmem_cache_destroy(dev_priv->dependencies);
	kmem_cache_destroy(dev_priv->requests);
	kmem_cache_destroy(dev_priv->luts);
	kmem_cache_destroy(dev_priv->vmas);
	kmem_cache_destroy(dev_priv->objects);

	/* And ensure that our DESTROY_BY_RCU slabs are truly destroyed */
	rcu_barrier();
}

int i915_gem_freeze(struct drm_i915_private *dev_priv)
{
	/* Discard all purgeable objects, let userspace recover those as
	 * required after resuming.
	 */
	i915_gem_shrink_all(dev_priv);

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
	 * the objects as well, see i915_gem_freeze()
	 */

	i915_gem_shrink(dev_priv, -1UL, NULL, I915_SHRINK_UNBOUND);
	i915_gem_drain_freed_objects(dev_priv);

	mutex_lock(&dev_priv->drm.struct_mutex);
	for (p = phases; *p; p++) {
		list_for_each_entry(obj, *p, global_link)
			__start_cpu_write(obj);
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
	list_for_each_entry(request, &file_priv->mm.request_list, client_link)
		request->file_priv = NULL;
	spin_unlock(&file_priv->mm.lock);
}

int i915_gem_open(struct drm_i915_private *i915, struct drm_file *file)
{
	struct drm_i915_file_private *file_priv;
	int ret;

	DRM_DEBUG("\n");

	file_priv = kzalloc(sizeof(*file_priv), GFP_KERNEL);
	if (!file_priv)
		return -ENOMEM;

	file->driver_priv = file_priv;
	file_priv->dev_priv = i915;
	file_priv->file = file;

	spin_lock_init(&file_priv->mm.lock);
	INIT_LIST_HEAD(&file_priv->mm.request_list);

	file_priv->bsd_engine = -1;

	ret = i915_gem_context_open(i915, file);
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

/* Allocate a new GEM object and fill it with the supplied data */
struct drm_i915_gem_object *
i915_gem_object_create_from_data(struct drm_i915_private *dev_priv,
			         const void *data, size_t size)
{
	struct drm_i915_gem_object *obj;
	struct file *file;
	size_t offset;
	int err;

	obj = i915_gem_object_create(dev_priv, round_up(size, PAGE_SIZE));
	if (IS_ERR(obj))
		return obj;

	GEM_BUG_ON(obj->base.write_domain != I915_GEM_DOMAIN_CPU);

	file = obj->base.filp;
	offset = 0;
	do {
		unsigned int len = min_t(typeof(size), size, PAGE_SIZE);
		struct page *page;
		void *pgdata, *vaddr;

		err = pagecache_write_begin(file, file->f_mapping,
					    offset, len, 0,
					    &page, &pgdata);
		if (err < 0)
			goto fail;

		vaddr = kmap(page);
		memcpy(vaddr, data, len);
		kunmap(page);

		err = pagecache_write_end(file, file->f_mapping,
					  offset, len, len,
					  page, pgdata);
		if (err < 0)
			goto fail;

		size -= len;
		data += len;
		offset += len;
	} while (size);

	return obj;

fail:
	i915_gem_object_put(obj);
	return ERR_PTR(err);
}

struct scatterlist *
i915_gem_object_get_sg(struct drm_i915_gem_object *obj,
		       unsigned int n,
		       unsigned int *offset)
{
	struct i915_gem_object_page_iter *iter = &obj->mm.get_page;
	struct scatterlist *sg;
	unsigned int idx, count;

	might_sleep();
	GEM_BUG_ON(n >= obj->base.size >> PAGE_SHIFT);
	GEM_BUG_ON(!i915_gem_object_has_pinned_pages(obj));

	/* As we iterate forward through the sg, we record each entry in a
	 * radixtree for quick repeated (backwards) lookups. If we have seen
	 * this index previously, we will have an entry for it.
	 *
	 * Initial lookup is O(N), but this is amortized to O(1) for
	 * sequential page access (where each new request is consecutive
	 * to the previous one). Repeated lookups are O(lg(obj->base.size)),
	 * i.e. O(1) with a large constant!
	 */
	if (n < READ_ONCE(iter->sg_idx))
		goto lookup;

	mutex_lock(&iter->lock);

	/* We prefer to reuse the last sg so that repeated lookup of this
	 * (or the subsequent) sg are fast - comparing against the last
	 * sg is faster than going through the radixtree.
	 */

	sg = iter->sg_pos;
	idx = iter->sg_idx;
	count = __sg_page_count(sg);

	while (idx + count <= n) {
		unsigned long exception, i;
		int ret;

		/* If we cannot allocate and insert this entry, or the
		 * individual pages from this range, cancel updating the
		 * sg_idx so that on this lookup we are forced to linearly
		 * scan onwards, but on future lookups we will try the
		 * insertion again (in which case we need to be careful of
		 * the error return reporting that we have already inserted
		 * this index).
		 */
		ret = radix_tree_insert(&iter->radix, idx, sg);
		if (ret && ret != -EEXIST)
			goto scan;

		exception =
			RADIX_TREE_EXCEPTIONAL_ENTRY |
			idx << RADIX_TREE_EXCEPTIONAL_SHIFT;
		for (i = 1; i < count; i++) {
			ret = radix_tree_insert(&iter->radix, idx + i,
						(void *)exception);
			if (ret && ret != -EEXIST)
				goto scan;
		}

		idx += count;
		sg = ____sg_next(sg);
		count = __sg_page_count(sg);
	}

scan:
	iter->sg_pos = sg;
	iter->sg_idx = idx;

	mutex_unlock(&iter->lock);

	if (unlikely(n < idx)) /* insertion completed by another thread */
		goto lookup;

	/* In case we failed to insert the entry into the radixtree, we need
	 * to look beyond the current sg.
	 */
	while (idx + count <= n) {
		idx += count;
		sg = ____sg_next(sg);
		count = __sg_page_count(sg);
	}

	*offset = n - idx;
	return sg;

lookup:
	rcu_read_lock();

	sg = radix_tree_lookup(&iter->radix, n);
	GEM_BUG_ON(!sg);

	/* If this index is in the middle of multi-page sg entry,
	 * the radixtree will contain an exceptional entry that points
	 * to the start of that range. We will return the pointer to
	 * the base page and the offset of this page within the
	 * sg entry's range.
	 */
	*offset = 0;
	if (unlikely(radix_tree_exception(sg))) {
		unsigned long base =
			(unsigned long)sg >> RADIX_TREE_EXCEPTIONAL_SHIFT;

		sg = radix_tree_lookup(&iter->radix, base);
		GEM_BUG_ON(!sg);

		*offset = n - base;
	}

	rcu_read_unlock();

	return sg;
}

struct page *
i915_gem_object_get_page(struct drm_i915_gem_object *obj, unsigned int n)
{
	struct scatterlist *sg;
	unsigned int offset;

	GEM_BUG_ON(!i915_gem_object_has_struct_page(obj));

	sg = i915_gem_object_get_sg(obj, n, &offset);
	return nth_page(sg_page(sg), offset);
}

/* Like i915_gem_object_get_page(), but mark the returned page dirty */
struct page *
i915_gem_object_get_dirty_page(struct drm_i915_gem_object *obj,
			       unsigned int n)
{
	struct page *page;

	page = i915_gem_object_get_page(obj, n);
	if (!obj->mm.dirty)
		set_page_dirty(page);

	return page;
}

dma_addr_t
i915_gem_object_get_dma_address(struct drm_i915_gem_object *obj,
				unsigned long n)
{
	struct scatterlist *sg;
	unsigned int offset;

	sg = i915_gem_object_get_sg(obj, n, &offset);
	return sg_dma_address(sg) + (offset << PAGE_SHIFT);
}

int i915_gem_object_attach_phys(struct drm_i915_gem_object *obj, int align)
{
	struct sg_table *pages;
	int err;

	if (align > obj->base.size)
		return -EINVAL;

	if (obj->ops == &i915_gem_phys_ops)
		return 0;

	if (obj->ops != &i915_gem_object_ops)
		return -EINVAL;

	err = i915_gem_object_unbind(obj);
	if (err)
		return err;

	mutex_lock(&obj->mm.lock);

	if (obj->mm.madv != I915_MADV_WILLNEED) {
		err = -EFAULT;
		goto err_unlock;
	}

	if (obj->mm.quirked) {
		err = -EFAULT;
		goto err_unlock;
	}

	if (obj->mm.mapping) {
		err = -EBUSY;
		goto err_unlock;
	}

	pages = obj->mm.pages;
	obj->ops = &i915_gem_phys_ops;

	err = ____i915_gem_object_get_pages(obj);
	if (err)
		goto err_xfer;

	/* Perma-pin (until release) the physical set of pages */
	__i915_gem_object_pin_pages(obj);

	if (!IS_ERR_OR_NULL(pages))
		i915_gem_object_ops.put_pages(obj, pages);
	mutex_unlock(&obj->mm.lock);
	return 0;

err_xfer:
	obj->ops = &i915_gem_object_ops;
	obj->mm.pages = pages;
err_unlock:
	mutex_unlock(&obj->mm.lock);
	return err;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/scatterlist.c"
#include "selftests/mock_gem_device.c"
#include "selftests/huge_gem_object.c"
#include "selftests/i915_gem_object.c"
#include "selftests/i915_gem_coherency.c"
#endif
