/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2008-2015 Intel Corporation
 */

#include <linux/oom.h>
#include <linux/sched/mm.h>
#include <linux/shmem_fs.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/pci.h>
#include <linux/dma-buf.h>
#include <linux/vmalloc.h>

#include "gt/intel_gt_requests.h"

#include "i915_trace.h"

static bool swap_available(void)
{
	return get_nr_swap_pages() > 0;
}

static bool can_release_pages(struct drm_i915_gem_object *obj)
{
	/* Consider only shrinkable ojects. */
	if (!i915_gem_object_is_shrinkable(obj))
		return false;

	/*
	 * We can only return physical pages to the system if we can either
	 * discard the contents (because the user has marked them as being
	 * purgeable) or if we can move their contents out to swap.
	 */
	return swap_available() || obj->mm.madv == I915_MADV_DONTNEED;
}

static bool drop_pages(struct drm_i915_gem_object *obj,
		       unsigned long shrink, bool trylock_vm)
{
	unsigned long flags;

	flags = 0;
	if (shrink & I915_SHRINK_ACTIVE)
		flags |= I915_GEM_OBJECT_UNBIND_ACTIVE;
	if (!(shrink & I915_SHRINK_BOUND))
		flags |= I915_GEM_OBJECT_UNBIND_TEST;
	if (trylock_vm)
		flags |= I915_GEM_OBJECT_UNBIND_VM_TRYLOCK;

	if (i915_gem_object_unbind(obj, flags) == 0)
		return true;

	return false;
}

static int try_to_writeback(struct drm_i915_gem_object *obj, unsigned int flags)
{
	if (obj->ops->shrink) {
		unsigned int shrink_flags = 0;

		if (!(flags & I915_SHRINK_ACTIVE))
			shrink_flags |= I915_GEM_OBJECT_SHRINK_NO_GPU_WAIT;

		if (flags & I915_SHRINK_WRITEBACK)
			shrink_flags |= I915_GEM_OBJECT_SHRINK_WRITEBACK;

		return obj->ops->shrink(obj, shrink_flags);
	}

	return 0;
}

/**
 * i915_gem_shrink - Shrink buffer object caches
 * @ww: i915 gem ww acquire ctx, or NULL
 * @i915: i915 device
 * @target: amount of memory to make available, in pages
 * @nr_scanned: optional output for number of pages scanned (incremental)
 * @shrink: control flags for selecting cache types
 *
 * This function is the main interface to the shrinker. It will try to release
 * up to @target pages of main memory backing storage from buffer objects.
 * Selection of the specific caches can be done with @flags. This is e.g. useful
 * when purgeable objects should be removed from caches preferentially.
 *
 * Note that it's not guaranteed that released amount is actually available as
 * free system memory - the pages might still be in-used to due to other reasons
 * (like cpu mmaps) or the mm core has reused them before we could grab them.
 * Therefore code that needs to explicitly shrink buffer objects caches (e.g. to
 * avoid deadlocks in memory reclaim) must fall back to i915_gem_shrink_all().
 *
 * Also note that any kind of pinning (both per-vma address space pins and
 * backing storage pins at the buffer object level) result in the shrinker code
 * having to skip the object.
 *
 * Returns:
 * The number of pages of backing storage actually released.
 */
unsigned long
i915_gem_shrink(struct i915_gem_ww_ctx *ww,
		struct drm_i915_private *i915,
		unsigned long target,
		unsigned long *nr_scanned,
		unsigned int shrink)
{
	const struct {
		struct list_head *list;
		unsigned int bit;
	} phases[] = {
		{ &i915->mm.purge_list, ~0u },
		{
			&i915->mm.shrink_list,
			I915_SHRINK_BOUND | I915_SHRINK_UNBOUND
		},
		{ NULL, 0 },
	}, *phase;
	intel_wakeref_t wakeref = 0;
	unsigned long count = 0;
	unsigned long scanned = 0;
	int err = 0;

	/* CHV + VTD workaround use stop_machine(); need to trylock vm->mutex */
	bool trylock_vm = !ww && intel_vm_no_concurrent_access_wa(i915);

	trace_i915_gem_shrink(i915, target, shrink);

	/*
	 * Unbinding of objects will require HW access; Let us not wake the
	 * device just to recover a little memory. If absolutely necessary,
	 * we will force the wake during oom-notifier.
	 */
	if (shrink & I915_SHRINK_BOUND) {
		wakeref = intel_runtime_pm_get_if_in_use(&i915->runtime_pm);
		if (!wakeref)
			shrink &= ~I915_SHRINK_BOUND;
	}

	/*
	 * When shrinking the active list, we should also consider active
	 * contexts. Active contexts are pinned until they are retired, and
	 * so can not be simply unbound to retire and unpin their pages. To
	 * shrink the contexts, we must wait until the gpu is idle and
	 * completed its switch to the kernel context. In short, we do
	 * not have a good mechanism for idling a specific context, but
	 * what we can do is give them a kick so that we do not keep idle
	 * contexts around longer than is necessary.
	 */
	if (shrink & I915_SHRINK_ACTIVE)
		/* Retire requests to unpin all idle contexts */
		intel_gt_retire_requests(to_gt(i915));

	/*
	 * As we may completely rewrite the (un)bound list whilst unbinding
	 * (due to retiring requests) we have to strictly process only
	 * one element of the list at the time, and recheck the list
	 * on every iteration.
	 *
	 * In particular, we must hold a reference whilst removing the
	 * object as we may end up waiting for and/or retiring the objects.
	 * This might release the final reference (held by the active list)
	 * and result in the object being freed from under us. This is
	 * similar to the precautions the eviction code must take whilst
	 * removing objects.
	 *
	 * Also note that although these lists do not hold a reference to
	 * the object we can safely grab one here: The final object
	 * unreferencing and the bound_list are both protected by the
	 * dev->struct_mutex and so we won't ever be able to observe an
	 * object on the bound_list with a reference count equals 0.
	 */
	for (phase = phases; phase->list; phase++) {
		struct list_head still_in_list;
		struct drm_i915_gem_object *obj;
		unsigned long flags;

		if ((shrink & phase->bit) == 0)
			continue;

		INIT_LIST_HEAD(&still_in_list);

		/*
		 * We serialize our access to unreferenced objects through
		 * the use of the struct_mutex. While the objects are not
		 * yet freed (due to RCU then a workqueue) we still want
		 * to be able to shrink their pages, so they remain on
		 * the unbound/bound list until actually freed.
		 */
		spin_lock_irqsave(&i915->mm.obj_lock, flags);
		while (count < target &&
		       (obj = list_first_entry_or_null(phase->list,
						       typeof(*obj),
						       mm.link))) {
			list_move_tail(&obj->mm.link, &still_in_list);

			if (shrink & I915_SHRINK_VMAPS &&
			    !is_vmalloc_addr(obj->mm.mapping))
				continue;

			if (!(shrink & I915_SHRINK_ACTIVE) &&
			    i915_gem_object_is_framebuffer(obj))
				continue;

			if (!can_release_pages(obj))
				continue;

			if (!kref_get_unless_zero(&obj->base.refcount))
				continue;

			spin_unlock_irqrestore(&i915->mm.obj_lock, flags);

			/* May arrive from get_pages on another bo */
			if (!ww) {
				if (!i915_gem_object_trylock(obj, NULL))
					goto skip;
			} else {
				err = i915_gem_object_lock(obj, ww);
				if (err)
					goto skip;
			}

			if (drop_pages(obj, shrink, trylock_vm) &&
			    !__i915_gem_object_put_pages(obj) &&
			    !try_to_writeback(obj, shrink))
				count += obj->base.size >> PAGE_SHIFT;

			if (!ww)
				i915_gem_object_unlock(obj);

			scanned += obj->base.size >> PAGE_SHIFT;
skip:
			i915_gem_object_put(obj);

			spin_lock_irqsave(&i915->mm.obj_lock, flags);
			if (err)
				break;
		}
		list_splice_tail(&still_in_list, phase->list);
		spin_unlock_irqrestore(&i915->mm.obj_lock, flags);
		if (err)
			break;
	}

	if (shrink & I915_SHRINK_BOUND)
		intel_runtime_pm_put(&i915->runtime_pm, wakeref);

	if (err)
		return err;

	if (nr_scanned)
		*nr_scanned += scanned;
	return count;
}

/**
 * i915_gem_shrink_all - Shrink buffer object caches completely
 * @i915: i915 device
 *
 * This is a simple wraper around i915_gem_shrink() to aggressively shrink all
 * caches completely. It also first waits for and retires all outstanding
 * requests to also be able to release backing storage for active objects.
 *
 * This should only be used in code to intentionally quiescent the gpu or as a
 * last-ditch effort when memory seems to have run out.
 *
 * Returns:
 * The number of pages of backing storage actually released.
 */
unsigned long i915_gem_shrink_all(struct drm_i915_private *i915)
{
	intel_wakeref_t wakeref;
	unsigned long freed = 0;

	with_intel_runtime_pm(&i915->runtime_pm, wakeref) {
		freed = i915_gem_shrink(NULL, i915, -1UL, NULL,
					I915_SHRINK_BOUND |
					I915_SHRINK_UNBOUND);
	}

	return freed;
}

static unsigned long
i915_gem_shrinker_count(struct shrinker *shrinker, struct shrink_control *sc)
{
	struct drm_i915_private *i915 =
		container_of(shrinker, struct drm_i915_private, mm.shrinker);
	unsigned long num_objects;
	unsigned long count;

	count = READ_ONCE(i915->mm.shrink_memory) >> PAGE_SHIFT;
	num_objects = READ_ONCE(i915->mm.shrink_count);

	/*
	 * Update our preferred vmscan batch size for the next pass.
	 * Our rough guess for an effective batch size is roughly 2
	 * available GEM objects worth of pages. That is we don't want
	 * the shrinker to fire, until it is worth the cost of freeing an
	 * entire GEM object.
	 */
	if (num_objects) {
		unsigned long avg = 2 * count / num_objects;

		i915->mm.shrinker.batch =
			max((i915->mm.shrinker.batch + avg) >> 1,
			    128ul /* default SHRINK_BATCH */);
	}

	return count;
}

static unsigned long
i915_gem_shrinker_scan(struct shrinker *shrinker, struct shrink_control *sc)
{
	struct drm_i915_private *i915 =
		container_of(shrinker, struct drm_i915_private, mm.shrinker);
	unsigned long freed;

	sc->nr_scanned = 0;

	freed = i915_gem_shrink(NULL, i915,
				sc->nr_to_scan,
				&sc->nr_scanned,
				I915_SHRINK_BOUND |
				I915_SHRINK_UNBOUND);
	if (sc->nr_scanned < sc->nr_to_scan && current_is_kswapd()) {
		intel_wakeref_t wakeref;

		with_intel_runtime_pm(&i915->runtime_pm, wakeref) {
			freed += i915_gem_shrink(NULL, i915,
						 sc->nr_to_scan - sc->nr_scanned,
						 &sc->nr_scanned,
						 I915_SHRINK_ACTIVE |
						 I915_SHRINK_BOUND |
						 I915_SHRINK_UNBOUND |
						 I915_SHRINK_WRITEBACK);
		}
	}

	return sc->nr_scanned ? freed : SHRINK_STOP;
}

static int
i915_gem_shrinker_oom(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct drm_i915_private *i915 =
		container_of(nb, struct drm_i915_private, mm.oom_notifier);
	struct drm_i915_gem_object *obj;
	unsigned long unevictable, available, freed_pages;
	intel_wakeref_t wakeref;
	unsigned long flags;

	freed_pages = 0;
	with_intel_runtime_pm(&i915->runtime_pm, wakeref)
		freed_pages += i915_gem_shrink(NULL, i915, -1UL, NULL,
					       I915_SHRINK_BOUND |
					       I915_SHRINK_UNBOUND |
					       I915_SHRINK_WRITEBACK);

	/* Because we may be allocating inside our own driver, we cannot
	 * assert that there are no objects with pinned pages that are not
	 * being pointed to by hardware.
	 */
	available = unevictable = 0;
	spin_lock_irqsave(&i915->mm.obj_lock, flags);
	list_for_each_entry(obj, &i915->mm.shrink_list, mm.link) {
		if (!can_release_pages(obj))
			unevictable += obj->base.size >> PAGE_SHIFT;
		else
			available += obj->base.size >> PAGE_SHIFT;
	}
	spin_unlock_irqrestore(&i915->mm.obj_lock, flags);

	if (freed_pages || available)
		pr_info("Purging GPU memory, %lu pages freed, "
			"%lu pages still pinned, %lu pages left available.\n",
			freed_pages, unevictable, available);

	*(unsigned long *)ptr += freed_pages;
	return NOTIFY_DONE;
}

static int
i915_gem_shrinker_vmap(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct drm_i915_private *i915 =
		container_of(nb, struct drm_i915_private, mm.vmap_notifier);
	struct i915_vma *vma, *next;
	unsigned long freed_pages = 0;
	intel_wakeref_t wakeref;

	with_intel_runtime_pm(&i915->runtime_pm, wakeref)
		freed_pages += i915_gem_shrink(NULL, i915, -1UL, NULL,
					       I915_SHRINK_BOUND |
					       I915_SHRINK_UNBOUND |
					       I915_SHRINK_VMAPS);

	/* We also want to clear any cached iomaps as they wrap vmap */
	mutex_lock(&to_gt(i915)->ggtt->vm.mutex);
	list_for_each_entry_safe(vma, next,
				 &to_gt(i915)->ggtt->vm.bound_list, vm_link) {
		unsigned long count = i915_vma_size(vma) >> PAGE_SHIFT;
		struct drm_i915_gem_object *obj = vma->obj;

		if (!vma->iomap || i915_vma_is_active(vma))
			continue;

		if (!i915_gem_object_trylock(obj, NULL))
			continue;

		if (__i915_vma_unbind(vma) == 0)
			freed_pages += count;

		i915_gem_object_unlock(obj);
	}
	mutex_unlock(&to_gt(i915)->ggtt->vm.mutex);

	*(unsigned long *)ptr += freed_pages;
	return NOTIFY_DONE;
}

void i915_gem_driver_register__shrinker(struct drm_i915_private *i915)
{
	i915->mm.shrinker.scan_objects = i915_gem_shrinker_scan;
	i915->mm.shrinker.count_objects = i915_gem_shrinker_count;
	i915->mm.shrinker.seeks = DEFAULT_SEEKS;
	i915->mm.shrinker.batch = 4096;
	drm_WARN_ON(&i915->drm, register_shrinker(&i915->mm.shrinker,
						  "drm-i915_gem"));

	i915->mm.oom_notifier.notifier_call = i915_gem_shrinker_oom;
	drm_WARN_ON(&i915->drm, register_oom_notifier(&i915->mm.oom_notifier));

	i915->mm.vmap_notifier.notifier_call = i915_gem_shrinker_vmap;
	drm_WARN_ON(&i915->drm,
		    register_vmap_purge_notifier(&i915->mm.vmap_notifier));
}

void i915_gem_driver_unregister__shrinker(struct drm_i915_private *i915)
{
	drm_WARN_ON(&i915->drm,
		    unregister_vmap_purge_notifier(&i915->mm.vmap_notifier));
	drm_WARN_ON(&i915->drm,
		    unregister_oom_notifier(&i915->mm.oom_notifier));
	unregister_shrinker(&i915->mm.shrinker);
}

void i915_gem_shrinker_taints_mutex(struct drm_i915_private *i915,
				    struct mutex *mutex)
{
	if (!IS_ENABLED(CONFIG_LOCKDEP))
		return;

	fs_reclaim_acquire(GFP_KERNEL);

	mutex_acquire(&mutex->dep_map, 0, 0, _RET_IP_);
	mutex_release(&mutex->dep_map, _RET_IP_);

	fs_reclaim_release(GFP_KERNEL);
}

/**
 * i915_gem_object_make_unshrinkable - Hide the object from the shrinker. By
 * default all object types that support shrinking(see IS_SHRINKABLE), will also
 * make the object visible to the shrinker after allocating the system memory
 * pages.
 * @obj: The GEM object.
 *
 * This is typically used for special kernel internal objects that can't be
 * easily processed by the shrinker, like if they are perma-pinned.
 */
void i915_gem_object_make_unshrinkable(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915 = obj_to_i915(obj);
	unsigned long flags;

	/*
	 * We can only be called while the pages are pinned or when
	 * the pages are released. If pinned, we should only be called
	 * from a single caller under controlled conditions; and on release
	 * only one caller may release us. Neither the two may cross.
	 */
	if (atomic_add_unless(&obj->mm.shrink_pin, 1, 0))
		return;

	spin_lock_irqsave(&i915->mm.obj_lock, flags);
	if (!atomic_fetch_inc(&obj->mm.shrink_pin) &&
	    !list_empty(&obj->mm.link)) {
		list_del_init(&obj->mm.link);
		i915->mm.shrink_count--;
		i915->mm.shrink_memory -= obj->base.size;
	}
	spin_unlock_irqrestore(&i915->mm.obj_lock, flags);
}

static void ___i915_gem_object_make_shrinkable(struct drm_i915_gem_object *obj,
					       struct list_head *head)
{
	struct drm_i915_private *i915 = obj_to_i915(obj);
	unsigned long flags;

	if (!i915_gem_object_is_shrinkable(obj))
		return;

	if (atomic_add_unless(&obj->mm.shrink_pin, -1, 1))
		return;

	spin_lock_irqsave(&i915->mm.obj_lock, flags);
	GEM_BUG_ON(!kref_read(&obj->base.refcount));
	if (atomic_dec_and_test(&obj->mm.shrink_pin)) {
		GEM_BUG_ON(!list_empty(&obj->mm.link));

		list_add_tail(&obj->mm.link, head);
		i915->mm.shrink_count++;
		i915->mm.shrink_memory += obj->base.size;

	}
	spin_unlock_irqrestore(&i915->mm.obj_lock, flags);
}

/**
 * __i915_gem_object_make_shrinkable - Move the object to the tail of the
 * shrinkable list. Objects on this list might be swapped out. Used with
 * WILLNEED objects.
 * @obj: The GEM object.
 *
 * DO NOT USE. This is intended to be called on very special objects that don't
 * yet have mm.pages, but are guaranteed to have potentially reclaimable pages
 * underneath.
 */
void __i915_gem_object_make_shrinkable(struct drm_i915_gem_object *obj)
{
	___i915_gem_object_make_shrinkable(obj,
					   &obj_to_i915(obj)->mm.shrink_list);
}

/**
 * __i915_gem_object_make_purgeable - Move the object to the tail of the
 * purgeable list. Objects on this list might be swapped out. Used with
 * DONTNEED objects.
 * @obj: The GEM object.
 *
 * DO NOT USE. This is intended to be called on very special objects that don't
 * yet have mm.pages, but are guaranteed to have potentially reclaimable pages
 * underneath.
 */
void __i915_gem_object_make_purgeable(struct drm_i915_gem_object *obj)
{
	___i915_gem_object_make_shrinkable(obj,
					   &obj_to_i915(obj)->mm.purge_list);
}

/**
 * i915_gem_object_make_shrinkable - Move the object to the tail of the
 * shrinkable list. Objects on this list might be swapped out. Used with
 * WILLNEED objects.
 * @obj: The GEM object.
 *
 * MUST only be called on objects which have backing pages.
 *
 * MUST be balanced with previous call to i915_gem_object_make_unshrinkable().
 */
void i915_gem_object_make_shrinkable(struct drm_i915_gem_object *obj)
{
	GEM_BUG_ON(!i915_gem_object_has_pages(obj));
	__i915_gem_object_make_shrinkable(obj);
}

/**
 * i915_gem_object_make_purgeable - Move the object to the tail of the purgeable
 * list. Used with DONTNEED objects. Unlike with shrinkable objects, the
 * shrinker will attempt to discard the backing pages, instead of trying to swap
 * them out.
 * @obj: The GEM object.
 *
 * MUST only be called on objects which have backing pages.
 *
 * MUST be balanced with previous call to i915_gem_object_make_unshrinkable().
 */
void i915_gem_object_make_purgeable(struct drm_i915_gem_object *obj)
{
	GEM_BUG_ON(!i915_gem_object_has_pages(obj));
	__i915_gem_object_make_purgeable(obj);
}
