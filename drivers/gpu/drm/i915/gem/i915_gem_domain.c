/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2016 Intel Corporation
 */

#include "display/intel_frontbuffer.h"

#include "i915_drv.h"
#include "i915_gem_clflush.h"
#include "i915_gem_gtt.h"
#include "i915_gem_ioctls.h"
#include "i915_gem_object.h"
#include "i915_vma.h"

static void __i915_gem_object_flush_for_display(struct drm_i915_gem_object *obj)
{
	/*
	 * We manually flush the CPU domain so that we can override and
	 * force the flush for the display, and perform it asyncrhonously.
	 */
	i915_gem_object_flush_write_domain(obj, ~I915_GEM_DOMAIN_CPU);
	if (obj->cache_dirty)
		i915_gem_clflush_object(obj, I915_CLFLUSH_FORCE);
	obj->write_domain = 0;
}

void i915_gem_object_flush_if_display(struct drm_i915_gem_object *obj)
{
	if (!READ_ONCE(obj->pin_global))
		return;

	i915_gem_object_lock(obj);
	__i915_gem_object_flush_for_display(obj);
	i915_gem_object_unlock(obj);
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

	i915_gem_object_flush_write_domain(obj, ~I915_GEM_DOMAIN_WC);

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

	i915_gem_object_flush_write_domain(obj, ~I915_GEM_DOMAIN_GTT);

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
		obj->read_domains = I915_GEM_DOMAIN_GTT;
		obj->write_domain = I915_GEM_DOMAIN_GTT;
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

	assert_object_held(obj);

	if (obj->cache_level == cache_level)
		return 0;

	/* Inspect the list of currently bound VMA and unbind any that would
	 * be invalid given the new cache-level. This is principally to
	 * catch the issue of the CS prefetch crossing page boundaries and
	 * reading an invalid PTE on older architectures.
	 */
restart:
	list_for_each_entry(vma, &obj->vma.list, obj_link) {
		if (!drm_mm_node_allocated(&vma->node))
			continue;

		if (i915_vma_is_pinned(vma)) {
			DRM_DEBUG("can not change the cache level of pinned objects\n");
			return -EBUSY;
		}

		if (!i915_vma_is_closed(vma) &&
		    i915_gem_valid_gtt_space(vma, cache_level))
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
	if (atomic_read(&obj->bind_count)) {
		struct drm_i915_private *i915 = to_i915(obj->base.dev);

		/* Before we change the PTE, the GPU must not be accessing it.
		 * If we wait upon the object, we know that all the bound
		 * VMA are no longer active.
		 */
		ret = i915_gem_object_wait(obj,
					   I915_WAIT_INTERRUPTIBLE |
					   I915_WAIT_ALL,
					   MAX_SCHEDULE_TIMEOUT);
		if (ret)
			return ret;

		if (!HAS_LLC(i915) && cache_level != I915_CACHE_NONE) {
			intel_wakeref_t wakeref =
				intel_runtime_pm_get(&i915->runtime_pm);

			/*
			 * Access to snoopable pages through the GTT is
			 * incoherent and on some machines causes a hard
			 * lockup. Relinquish the CPU mmaping to force
			 * userspace to refault in the pages and we can
			 * then double check if the GTT mapping is still
			 * valid for that pointer access.
			 */
			ret = mutex_lock_interruptible(&i915->ggtt.vm.mutex);
			if (ret) {
				intel_runtime_pm_put(&i915->runtime_pm,
						     wakeref);
				return ret;
			}

			if (obj->userfault_count)
				__i915_gem_object_release_mmap(obj);

			/*
			 * As we no longer need a fence for GTT access,
			 * we can relinquish it now (and so prevent having
			 * to steal a fence from someone else on the next
			 * fence request). Note GPU activity would have
			 * dropped the fence as all snoopable access is
			 * supposed to be linear.
			 */
			for_each_ggtt_vma(vma, obj) {
				ret = i915_vma_revoke_fence(vma);
				if (ret)
					break;
			}
			mutex_unlock(&i915->ggtt.vm.mutex);
			intel_runtime_pm_put(&i915->runtime_pm, wakeref);
			if (ret)
				return ret;
		} else {
			/*
			 * We either have incoherent backing store and
			 * so no GTT access or the architecture is fully
			 * coherent. In such cases, existing GTT mmaps
			 * ignore the cache bit in the PTE and we can
			 * rewrite it without confusing the GPU or having
			 * to force userspace to fault back in its mmaps.
			 */
		}

		list_for_each_entry(vma, &obj->vma.list, obj_link) {
			if (!drm_mm_node_allocated(&vma->node))
				continue;

			ret = i915_vma_bind(vma, cache_level, PIN_UPDATE);
			if (ret)
				return ret;
		}
	}

	list_for_each_entry(vma, &obj->vma.list, obj_link)
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

	/*
	 * The caching mode of proxy object is handled by its generator, and
	 * not allowed to be changed by userspace.
	 */
	if (i915_gem_object_is_proxy(obj)) {
		ret = -ENXIO;
		goto out;
	}

	if (obj->cache_level == level)
		goto out;

	ret = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE,
				   MAX_SCHEDULE_TIMEOUT);
	if (ret)
		goto out;

	ret = mutex_lock_interruptible(&i915->drm.struct_mutex);
	if (ret)
		goto out;

	ret = i915_gem_object_lock_interruptible(obj);
	if (ret == 0) {
		ret = i915_gem_object_set_cache_level(obj, level);
		i915_gem_object_unlock(obj);
	}
	mutex_unlock(&i915->drm.struct_mutex);

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
				     u32 alignment,
				     const struct i915_ggtt_view *view,
				     unsigned int flags)
{
	struct i915_vma *vma;
	int ret;

	assert_object_held(obj);

	/* Mark the global pin early so that we account for the
	 * display coherency whilst setting up the cache domains.
	 */
	obj->pin_global++;

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
		goto err_unpin_global;
	}

	/* As the user may map the buffer once pinned in the display plane
	 * (e.g. libkms for the bootup splash), we have to ensure that we
	 * always use map_and_fenceable for all scanout buffers. However,
	 * it may simply be too big to fit into mappable, in which case
	 * put it anyway and hope that userspace can cope (but always first
	 * try to preserve the existing ABI).
	 */
	vma = ERR_PTR(-ENOSPC);
	if ((flags & PIN_MAPPABLE) == 0 &&
	    (!view || view->type == I915_GGTT_VIEW_NORMAL))
		vma = i915_gem_object_ggtt_pin(obj, view, 0, alignment,
					       flags |
					       PIN_MAPPABLE |
					       PIN_NONBLOCK);
	if (IS_ERR(vma))
		vma = i915_gem_object_ggtt_pin(obj, view, 0, alignment, flags);
	if (IS_ERR(vma))
		goto err_unpin_global;

	vma->display_alignment = max_t(u64, vma->display_alignment, alignment);

	__i915_gem_object_flush_for_display(obj);

	/* It should now be out of any other write domains, and we can update
	 * the domain values for our changes.
	 */
	obj->read_domains |= I915_GEM_DOMAIN_GTT;

	return vma;

err_unpin_global:
	obj->pin_global--;
	return vma;
}

static void i915_gem_object_bump_inactive_ggtt(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct i915_vma *vma;

	GEM_BUG_ON(!i915_gem_object_has_pinned_pages(obj));

	mutex_lock(&i915->ggtt.vm.mutex);
	for_each_ggtt_vma(vma, obj) {
		if (!drm_mm_node_allocated(&vma->node))
			continue;

		list_move_tail(&vma->vm_link, &vma->vm->bound_list);
	}
	mutex_unlock(&i915->ggtt.vm.mutex);

	if (i915_gem_object_is_shrinkable(obj)) {
		unsigned long flags;

		spin_lock_irqsave(&i915->mm.obj_lock, flags);

		if (obj->mm.madv == I915_MADV_WILLNEED)
			list_move_tail(&obj->mm.link, &i915->mm.shrink_list);

		spin_unlock_irqrestore(&i915->mm.obj_lock, flags);
	}
}

void
i915_gem_object_unpin_from_display_plane(struct i915_vma *vma)
{
	struct drm_i915_gem_object *obj = vma->obj;

	assert_object_held(obj);

	if (WARN_ON(obj->pin_global == 0))
		return;

	if (--obj->pin_global == 0)
		vma->display_alignment = I915_GTT_MIN_ALIGNMENT;

	/* Bump the LRU to try and avoid premature eviction whilst flipping  */
	i915_gem_object_bump_inactive_ggtt(obj);

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

	assert_object_held(obj);

	ret = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE |
				   (write ? I915_WAIT_ALL : 0),
				   MAX_SCHEDULE_TIMEOUT);
	if (ret)
		return ret;

	i915_gem_object_flush_write_domain(obj, ~I915_GEM_DOMAIN_CPU);

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
	 * Already in the desired write domain? Nothing for us to do!
	 *
	 * We apply a little bit of cunning here to catch a broader set of
	 * no-ops. If obj->write_domain is set, we must be in the same
	 * obj->read_domains, and only that domain. Therefore, if that
	 * obj->write_domain matches the request read_domains, we are
	 * already in the same read/write domain and can skip the operation,
	 * without having to further check the requested write_domain.
	 */
	if (READ_ONCE(obj->write_domain) == read_domains) {
		err = 0;
		goto out;
	}

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
		goto out;

	err = i915_gem_object_lock_interruptible(obj);
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

	i915_gem_object_unlock(obj);

	if (write_domain)
		intel_frontbuffer_invalidate(obj->frontbuffer, ORIGIN_CPU);

out_unpin:
	i915_gem_object_unpin_pages(obj);
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

	ret = i915_gem_object_lock_interruptible(obj);
	if (ret)
		return ret;

	ret = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE,
				   MAX_SCHEDULE_TIMEOUT);
	if (ret)
		goto err_unlock;

	ret = i915_gem_object_pin_pages(obj);
	if (ret)
		goto err_unlock;

	if (obj->cache_coherent & I915_BO_CACHE_COHERENT_FOR_READ ||
	    !static_cpu_has(X86_FEATURE_CLFLUSH)) {
		ret = i915_gem_object_set_to_cpu_domain(obj, false);
		if (ret)
			goto err_unpin;
		else
			goto out;
	}

	i915_gem_object_flush_write_domain(obj, ~I915_GEM_DOMAIN_CPU);

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
err_unlock:
	i915_gem_object_unlock(obj);
	return ret;
}

int i915_gem_object_prepare_write(struct drm_i915_gem_object *obj,
				  unsigned int *needs_clflush)
{
	int ret;

	*needs_clflush = 0;
	if (!i915_gem_object_has_struct_page(obj))
		return -ENODEV;

	ret = i915_gem_object_lock_interruptible(obj);
	if (ret)
		return ret;

	ret = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE |
				   I915_WAIT_ALL,
				   MAX_SCHEDULE_TIMEOUT);
	if (ret)
		goto err_unlock;

	ret = i915_gem_object_pin_pages(obj);
	if (ret)
		goto err_unlock;

	if (obj->cache_coherent & I915_BO_CACHE_COHERENT_FOR_WRITE ||
	    !static_cpu_has(X86_FEATURE_CLFLUSH)) {
		ret = i915_gem_object_set_to_cpu_domain(obj, true);
		if (ret)
			goto err_unpin;
		else
			goto out;
	}

	i915_gem_object_flush_write_domain(obj, ~I915_GEM_DOMAIN_CPU);

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
	intel_frontbuffer_invalidate(obj->frontbuffer, ORIGIN_CPU);
	obj->mm.dirty = true;
	/* return with the pages pinned */
	return 0;

err_unpin:
	i915_gem_object_unpin_pages(obj);
err_unlock:
	i915_gem_object_unlock(obj);
	return ret;
}
