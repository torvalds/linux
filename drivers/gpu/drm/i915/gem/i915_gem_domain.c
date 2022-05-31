/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2016 Intel Corporation
 */

#include "display/intel_frontbuffer.h"
#include "gt/intel_gt.h"

#include "i915_drv.h"
#include "i915_gem_clflush.h"
#include "i915_gem_domain.h"
#include "i915_gem_gtt.h"
#include "i915_gem_ioctls.h"
#include "i915_gem_lmem.h"
#include "i915_gem_mman.h"
#include "i915_gem_object.h"
#include "i915_vma.h"

static bool gpu_write_needs_clflush(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);

	if (IS_DGFX(i915))
		return false;

	return !(obj->cache_level == I915_CACHE_NONE ||
		 obj->cache_level == I915_CACHE_WT);
}

bool i915_gem_cpu_write_needs_clflush(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);

	if (obj->cache_dirty)
		return false;

	if (!(obj->cache_coherent & I915_BO_CACHE_COHERENT_FOR_WRITE))
		return true;

	if (IS_DGFX(i915))
		return false;

	/* Currently in use by HW (display engine)? Keep flushed. */
	return i915_gem_object_is_framebuffer(obj);
}

static void
flush_write_domain(struct drm_i915_gem_object *obj, unsigned int flush_domains)
{
	struct i915_vma *vma;

	assert_object_held(obj);

	if (!(obj->write_domain & flush_domains))
		return;

	switch (obj->write_domain) {
	case I915_GEM_DOMAIN_GTT:
		spin_lock(&obj->vma.lock);
		for_each_ggtt_vma(vma, obj) {
			if (i915_vma_unset_ggtt_write(vma))
				intel_gt_flush_ggtt_writes(vma->vm->gt);
		}
		spin_unlock(&obj->vma.lock);

		i915_gem_object_flush_frontbuffer(obj, ORIGIN_CPU);
		break;

	case I915_GEM_DOMAIN_WC:
		wmb();
		break;

	case I915_GEM_DOMAIN_CPU:
		i915_gem_clflush_object(obj, I915_CLFLUSH_SYNC);
		break;

	case I915_GEM_DOMAIN_RENDER:
		if (gpu_write_needs_clflush(obj))
			obj->cache_dirty = true;
		break;
	}

	obj->write_domain = 0;
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
	obj->write_domain = 0;
}

void i915_gem_object_flush_if_display(struct drm_i915_gem_object *obj)
{
	if (!i915_gem_object_is_framebuffer(obj))
		return;

	i915_gem_object_lock(obj, NULL);
	__i915_gem_object_flush_for_display(obj);
	i915_gem_object_unlock(obj);
}

void i915_gem_object_flush_if_display_locked(struct drm_i915_gem_object *obj)
{
	if (i915_gem_object_is_framebuffer(obj))
		__i915_gem_object_flush_for_display(obj);
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

	assert_object_held(obj);

	ret = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE |
				   (write ? I915_WAIT_ALL : 0),
				   MAX_SCHEDULE_TIMEOUT);
	if (ret)
		return ret;

	if (obj->write_domain == I915_GEM_DOMAIN_WC)
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
	if ((obj->read_domains & I915_GEM_DOMAIN_WC) == 0)
		mb();

	/* It should now be out of any other write domains, and we can update
	 * the domain values for our changes.
	 */
	GEM_BUG_ON((obj->write_domain & ~I915_GEM_DOMAIN_WC) != 0);
	obj->read_domains |= I915_GEM_DOMAIN_WC;
	if (write) {
		obj->read_domains = I915_GEM_DOMAIN_WC;
		obj->write_domain = I915_GEM_DOMAIN_WC;
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

	assert_object_held(obj);

	ret = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE |
				   (write ? I915_WAIT_ALL : 0),
				   MAX_SCHEDULE_TIMEOUT);
	if (ret)
		return ret;

	if (obj->write_domain == I915_GEM_DOMAIN_GTT)
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
	if ((obj->read_domains & I915_GEM_DOMAIN_GTT) == 0)
		mb();

	/* It should now be out of any other write domains, and we can update
	 * the domain values for our changes.
	 */
	GEM_BUG_ON((obj->write_domain & ~I915_GEM_DOMAIN_GTT) != 0);
	obj->read_domains |= I915_GEM_DOMAIN_GTT;
	if (write) {
		struct i915_vma *vma;

		obj->read_domains = I915_GEM_DOMAIN_GTT;
		obj->write_domain = I915_GEM_DOMAIN_GTT;
		obj->mm.dirty = true;

		spin_lock(&obj->vma.lock);
		for_each_ggtt_vma(vma, obj)
			if (i915_vma_is_bound(vma, I915_VMA_GLOBAL_BIND))
				i915_vma_set_ggtt_write(vma);
		spin_unlock(&obj->vma.lock);
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
	int ret;

	if (obj->cache_level == cache_level)
		return 0;

	ret = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE |
				   I915_WAIT_ALL,
				   MAX_SCHEDULE_TIMEOUT);
	if (ret)
		return ret;

	/* Always invalidate stale cachelines */
	if (obj->cache_level != cache_level) {
		i915_gem_object_set_cache_coherency(obj, cache_level);
		obj->cache_dirty = true;
	}

	/* The cache-level will be applied when each vma is rebound. */
	return i915_gem_object_unbind(obj,
				      I915_GEM_OBJECT_UNBIND_ACTIVE |
				      I915_GEM_OBJECT_UNBIND_BARRIER);
}

int i915_gem_get_caching_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file)
{
	struct drm_i915_gem_caching *args = data;
	struct drm_i915_gem_object *obj;
	int err = 0;

	if (IS_DGFX(to_i915(dev)))
		return -ENODEV;

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

	if (IS_DGFX(i915))
		return -ENODEV;

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

	/*
	 * The caching mode of proxy object is handled by its generator, and
	 * not allowed to be changed by userspace.
	 */
	if (i915_gem_object_is_proxy(obj)) {
		/*
		 * Silently allow cached for userptr; the vulkan driver
		 * sets all objects to cached
		 */
		if (!i915_gem_object_is_userptr(obj) ||
		    args->caching != I915_CACHING_CACHED)
			ret = -ENXIO;

		goto out;
	}

	ret = i915_gem_object_lock_interruptible(obj, NULL);
	if (ret)
		goto out;

	ret = i915_gem_object_set_cache_level(obj, level);
	i915_gem_object_unlock(obj);

out:
	i915_gem_object_put(obj);
	return ret;
}

/*
 * Prepare buffer for display plane (scanout, cursors, etc). Can be called from
 * an uninterruptible phase (modesetting) and allows any flushes to be pipelined
 * (for pageflips). We only flush the caches while preparing the buffer for
 * display, the callers are responsible for frontbuffer flush.
 */
struct i915_vma *
i915_gem_object_pin_to_display_plane(struct drm_i915_gem_object *obj,
				     struct i915_gem_ww_ctx *ww,
				     u32 alignment,
				     const struct i915_ggtt_view *view,
				     unsigned int flags)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct i915_vma *vma;
	int ret;

	/* Frame buffer must be in LMEM */
	if (HAS_LMEM(i915) && !i915_gem_object_is_lmem(obj))
		return ERR_PTR(-EINVAL);

	/*
	 * The display engine is not coherent with the LLC cache on gen6.  As
	 * a result, we make sure that the pinning that is about to occur is
	 * done with uncached PTEs. This is lowest common denominator for all
	 * chipsets.
	 *
	 * However for gen6+, we could do better by using the GFDT bit instead
	 * of uncaching, which would allow us to flush all the LLC-cached data
	 * with that bit in the PTE to main memory with just one PIPE_CONTROL.
	 */
	ret = i915_gem_object_set_cache_level(obj,
					      HAS_WT(i915) ?
					      I915_CACHE_WT : I915_CACHE_NONE);
	if (ret)
		return ERR_PTR(ret);

	/*
	 * As the user may map the buffer once pinned in the display plane
	 * (e.g. libkms for the bootup splash), we have to ensure that we
	 * always use map_and_fenceable for all scanout buffers. However,
	 * it may simply be too big to fit into mappable, in which case
	 * put it anyway and hope that userspace can cope (but always first
	 * try to preserve the existing ABI).
	 */
	vma = ERR_PTR(-ENOSPC);
	if ((flags & PIN_MAPPABLE) == 0 &&
	    (!view || view->type == I915_GGTT_VIEW_NORMAL))
		vma = i915_gem_object_ggtt_pin_ww(obj, ww, view, 0, alignment,
						  flags | PIN_MAPPABLE |
						  PIN_NONBLOCK);
	if (IS_ERR(vma) && vma != ERR_PTR(-EDEADLK))
		vma = i915_gem_object_ggtt_pin_ww(obj, ww, view, 0,
						  alignment, flags);
	if (IS_ERR(vma))
		return vma;

	vma->display_alignment = max_t(u64, vma->display_alignment, alignment);
	i915_vma_mark_scanout(vma);

	i915_gem_object_flush_if_display_locked(obj);

	return vma;
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

	assert_object_held(obj);

	ret = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE |
				   (write ? I915_WAIT_ALL : 0),
				   MAX_SCHEDULE_TIMEOUT);
	if (ret)
		return ret;

	flush_write_domain(obj, ~I915_GEM_DOMAIN_CPU);

	/* Flush the CPU cache if it's still invalid. */
	if ((obj->read_domains & I915_GEM_DOMAIN_CPU) == 0) {
		i915_gem_clflush_object(obj, I915_CLFLUSH_SYNC);
		obj->read_domains |= I915_GEM_DOMAIN_CPU;
	}

	/* It should now be out of any other write domains, and we can update
	 * the domain values for our changes.
	 */
	GEM_BUG_ON(obj->write_domain & ~I915_GEM_DOMAIN_CPU);

	/* If we're writing through the CPU, then the GPU read domains will
	 * need to be invalidated at next use.
	 */
	if (write)
		__start_cpu_write(obj);

	return 0;
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
	u32 read_domains = args->read_domains;
	u32 write_domain = args->write_domain;
	int err;

	if (IS_DGFX(to_i915(dev)))
		return -ENODEV;

	/* Only handle setting domains to types used by the CPU. */
	if ((write_domain | read_domains) & I915_GEM_GPU_DOMAINS)
		return -EINVAL;

	/*
	 * Having something in the write domain implies it's in the read
	 * domain, and only that read domain.  Enforce that in the request.
	 */
	if (write_domain && read_domains != write_domain)
		return -EINVAL;

	if (!read_domains)
		return 0;

	obj = i915_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	/*
	 * Try to flush the object off the GPU without holding the lock.
	 * We will repeat the flush holding the lock in the normal manner
	 * to catch cases where we are gazumped.
	 */
	err = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE |
				   I915_WAIT_PRIORITY |
				   (write_domain ? I915_WAIT_ALL : 0),
				   MAX_SCHEDULE_TIMEOUT);
	if (err)
		goto out;

	if (i915_gem_object_is_userptr(obj)) {
		/*
		 * Try to grab userptr pages, iris uses set_domain to check
		 * userptr validity
		 */
		err = i915_gem_object_userptr_validate(obj);
		if (!err)
			err = i915_gem_object_wait(obj,
						   I915_WAIT_INTERRUPTIBLE |
						   I915_WAIT_PRIORITY |
						   (write_domain ? I915_WAIT_ALL : 0),
						   MAX_SCHEDULE_TIMEOUT);
		goto out;
	}

	/*
	 * Proxy objects do not control access to the backing storage, ergo
	 * they cannot be used as a means to manipulate the cache domain
	 * tracking for that backing storage. The proxy object is always
	 * considered to be outside of any cache domain.
	 */
	if (i915_gem_object_is_proxy(obj)) {
		err = -ENXIO;
		goto out;
	}

	err = i915_gem_object_lock_interruptible(obj, NULL);
	if (err)
		goto out;

	/*
	 * Flush and acquire obj->pages so that we are coherent through
	 * direct access in memory with previous cached writes through
	 * shmemfs and that our cache domain tracking remains valid.
	 * For example, if the obj->filp was moved to swap without us
	 * being notified and releasing the pages, we would mistakenly
	 * continue to assume that the obj remained out of the CPU cached
	 * domain.
	 */
	err = i915_gem_object_pin_pages(obj);
	if (err)
		goto out_unlock;

	/*
	 * Already in the desired write domain? Nothing for us to do!
	 *
	 * We apply a little bit of cunning here to catch a broader set of
	 * no-ops. If obj->write_domain is set, we must be in the same
	 * obj->read_domains, and only that domain. Therefore, if that
	 * obj->write_domain matches the request read_domains, we are
	 * already in the same read/write domain and can skip the operation,
	 * without having to further check the requested write_domain.
	 */
	if (READ_ONCE(obj->write_domain) == read_domains)
		goto out_unpin;

	if (read_domains & I915_GEM_DOMAIN_WC)
		err = i915_gem_object_set_to_wc_domain(obj, write_domain);
	else if (read_domains & I915_GEM_DOMAIN_GTT)
		err = i915_gem_object_set_to_gtt_domain(obj, write_domain);
	else
		err = i915_gem_object_set_to_cpu_domain(obj, write_domain);

out_unpin:
	i915_gem_object_unpin_pages(obj);

out_unlock:
	i915_gem_object_unlock(obj);

	if (!err && write_domain)
		i915_gem_object_invalidate_frontbuffer(obj, ORIGIN_CPU);

out:
	i915_gem_object_put(obj);
	return err;
}

/*
 * Pins the specified object's pages and synchronizes the object with
 * GPU accesses. Sets needs_clflush to non-zero if the caller should
 * flush the object from the CPU cache.
 */
int i915_gem_object_prepare_read(struct drm_i915_gem_object *obj,
				 unsigned int *needs_clflush)
{
	int ret;

	*needs_clflush = 0;
	if (!i915_gem_object_has_struct_page(obj))
		return -ENODEV;

	assert_object_held(obj);

	ret = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE,
				   MAX_SCHEDULE_TIMEOUT);
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
	    !(obj->read_domains & I915_GEM_DOMAIN_CPU))
		*needs_clflush = CLFLUSH_BEFORE;

out:
	/* return with the pages pinned */
	return 0;

err_unpin:
	i915_gem_object_unpin_pages(obj);
	return ret;
}

int i915_gem_object_prepare_write(struct drm_i915_gem_object *obj,
				  unsigned int *needs_clflush)
{
	int ret;

	*needs_clflush = 0;
	if (!i915_gem_object_has_struct_page(obj))
		return -ENODEV;

	assert_object_held(obj);

	ret = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE |
				   I915_WAIT_ALL,
				   MAX_SCHEDULE_TIMEOUT);
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
		if (!(obj->read_domains & I915_GEM_DOMAIN_CPU))
			*needs_clflush |= CLFLUSH_BEFORE;
	}

out:
	i915_gem_object_invalidate_frontbuffer(obj, ORIGIN_CPU);
	obj->mm.dirty = true;
	/* return with the pages pinned */
	return 0;

err_unpin:
	i915_gem_object_unpin_pages(obj);
	return ret;
}
