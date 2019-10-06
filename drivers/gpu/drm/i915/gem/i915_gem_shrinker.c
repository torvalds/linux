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
#include <drm/i915_drm.h>

#include "i915_trace.h"

static bool shrinker_lock(struct drm_i915_private *i915,
			  unsigned int flags,
			  bool *unlock)
{
	struct mutex *m = &i915->drm.struct_mutex;

	switch (mutex_trylock_recursive(m)) {
	case MUTEX_TRYLOCK_RECURSIVE:
		*unlock = false;
		return true;

	case MUTEX_TRYLOCK_FAILED:
		*unlock = false;
		if (flags & I915_SHRINK_ACTIVE &&
		    mutex_lock_killable_nested(m, I915_MM_SHRINKER) == 0)
			*unlock = true;
		return *unlock;

	case MUTEX_TRYLOCK_SUCCESS:
		*unlock = true;
		return true;
	}

	BUG();
}

static void shrinker_unlock(struct drm_i915_private *i915, bool unlock)
{
	if (!unlock)
		return;

	mutex_unlock(&i915->drm.struct_mutex);
}

static bool swap_available(void)
{
	return get_nr_swap_pages() > 0;
}

static bool can_release_pages(struct drm_i915_gem_object *obj)
{
	/* Consider only shrinkable ojects. */
	if (!i915_gem_object_is_shrinkable(obj))
		return false;

	/* Only report true if by unbinding the object and putting its pages
	 * we can actually make forward progress towards freeing physical
	 * pages.
	 *
	 * If the pages are pinned for any other reason than being bound
	 * to the GPU, simply unbinding from the GPU is not going to succeed
	 * in releasing our pin count on the pages themselves.
	 */
	if (atomic_read(&obj->mm.pages_pin_count) > atomic_read(&obj->bind_count))
		return false;

	/* If any vma are "permanently" pinned, it will prevent us from
	 * reclaiming the obj->mm.pages. We only allow scanout objects to claim
	 * a permanent pin, along with a few others like the context objects.
	 * To simplify the scan, and to avoid walking the list of vma under the
	 * object, we just check the count of its permanently pinned.
	 */
	if (READ_ONCE(obj->pin_global))
		return false;

	/* We can only return physical pages to the system if we can either
	 * discard the contents (because the user has marked them as being
	 * purgeable) or if we can move their contents out to swap.
	 */
	return swap_available() || obj->mm.madv == I915_MADV_DONTNEED;
}

static bool unsafe_drop_pages(struct drm_i915_gem_object *obj,
			      unsigned long shrink)
{
	unsigned long flags;

	flags = 0;
	if (shrink & I915_SHRINK_ACTIVE)
		flags = I915_GEM_OBJECT_UNBIND_ACTIVE;

	if (i915_gem_object_unbind(obj, flags) == 0)
		__i915_gem_object_put_pages(obj, I915_MM_SHRINKER);

	return !i915_gem_object_has_pages(obj);
}

static void try_to_writeback(struct drm_i915_gem_object *obj,
			     unsigned int flags)
{
	switch (obj->mm.madv) {
	case I915_MADV_DONTNEED:
		i915_gem_object_truncate(obj);
	case __I915_MADV_PURGED:
		return;
	}

	if (flags & I915_SHRINK_WRITEBACK)
		i915_gem_object_writeback(obj);
}

/**
 * i915_gem_shrink - Shrink buffer object caches
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
i915_gem_shrink(struct drm_i915_private *i915,
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
	bool unlock;

	if (!shrinker_lock(i915, shrink, &unlock))
		return 0;

	/*
	 * When shrinking the active list, we should also consider active
	 * contexts. Active contexts are pinned until they are retired, and
	 * so can not be simply unbound to retire and unpin their pages. To
	 * shrink the contexts, we must wait until the gpu is idle and
	 * completed its switch to the kernel context. In short, we do
	 * not have a good mechanism for idling a specific context.
	 */

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

			if (!(shrink & I915_SHRINK_BOUND) &&
			    atomic_read(&obj->bind_count))
				continue;

			if (!can_release_pages(obj))
				continue;

			if (!kref_get_unless_zero(&obj->base.refcount))
				continue;

			spin_unlock_irqrestore(&i915->mm.obj_lock, flags);

			if (unsafe_drop_pages(obj, shrink)) {
				/* May arrive from get_pages on another bo */
				mutex_lock_nested(&obj->mm.lock,
						  I915_MM_SHRINKER);
				if (!i915_gem_object_has_pages(obj)) {
					try_to_writeback(obj, shrink);
					count += obj->base.size >> PAGE_SHIFT;
				}
				mutex_unlock(&obj->mm.lock);
			}

			scanned += obj->base.size >> PAGE_SHIFT;
			i915_gem_object_put(obj);

			spin_lock_irqsave(&i915->mm.obj_lock, flags);
		}
		list_splice_tail(&still_in_list, phase->list);
		spin_unlock_irqrestore(&i915->mm.obj_lock, flags);
	}

	if (shrink & I915_SHRINK_BOUND)
		intel_runtime_pm_put(&i915->runtime_pm, wakeref);

	shrinker_unlock(i915, unlock);

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
		freed = i915_gem_shrink(i915, -1UL, NULL,
					I915_SHRINK_BOUND |
					I915_SHRINK_UNBOUND |
					I915_SHRINK_ACTIVE);
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
	bool unlock;

	sc->nr_scanned = 0;

	if (!shrinker_lock(i915, 0, &unlock))
		return SHRINK_STOP;

	freed = i915_gem_shrink(i915,
				sc->nr_to_scan,
				&sc->nr_scanned,
				I915_SHRINK_BOUND |
				I915_SHRINK_UNBOUND |
				I915_SHRINK_WRITEBACK);
	if (sc->nr_scanned < sc->nr_to_scan && current_is_kswapd()) {
		intel_wakeref_t wakeref;

		with_intel_runtime_pm(&i915->runtime_pm, wakeref) {
			freed += i915_gem_shrink(i915,
						 sc->nr_to_scan - sc->nr_scanned,
						 &sc->nr_scanned,
						 I915_SHRINK_ACTIVE |
						 I915_SHRINK_BOUND |
						 I915_SHRINK_UNBOUND |
						 I915_SHRINK_WRITEBACK);
		}
	}

	shrinker_unlock(i915, unlock);

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
		freed_pages += i915_gem_shrink(i915, -1UL, NULL,
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
	bool unlock;

	if (!shrinker_lock(i915, 0, &unlock))
		return NOTIFY_DONE;

	with_intel_runtime_pm(&i915->runtime_pm, wakeref)
		freed_pages += i915_gem_shrink(i915, -1UL, NULL,
					       I915_SHRINK_BOUND |
					       I915_SHRINK_UNBOUND |
					       I915_SHRINK_VMAPS);

	/* We also want to clear any cached iomaps as they wrap vmap */
	mutex_lock(&i915->ggtt.vm.mutex);
	list_for_each_entry_safe(vma, next,
				 &i915->ggtt.vm.bound_list, vm_link) {
		unsigned long count = vma->node.size >> PAGE_SHIFT;

		if (!vma->iomap || i915_vma_is_active(vma))
			continue;

		mutex_unlock(&i915->ggtt.vm.mutex);
		if (i915_vma_unbind(vma) == 0)
			freed_pages += count;
		mutex_lock(&i915->ggtt.vm.mutex);
	}
	mutex_unlock(&i915->ggtt.vm.mutex);

	shrinker_unlock(i915, unlock);

	*(unsigned long *)ptr += freed_pages;
	return NOTIFY_DONE;
}

void i915_gem_driver_register__shrinker(struct drm_i915_private *i915)
{
	i915->mm.shrinker.scan_objects = i915_gem_shrinker_scan;
	i915->mm.shrinker.count_objects = i915_gem_shrinker_count;
	i915->mm.shrinker.seeks = DEFAULT_SEEKS;
	i915->mm.shrinker.batch = 4096;
	WARN_ON(register_shrinker(&i915->mm.shrinker));

	i915->mm.oom_notifier.notifier_call = i915_gem_shrinker_oom;
	WARN_ON(register_oom_notifier(&i915->mm.oom_notifier));

	i915->mm.vmap_notifier.notifier_call = i915_gem_shrinker_vmap;
	WARN_ON(register_vmap_purge_notifier(&i915->mm.vmap_notifier));
}

void i915_gem_driver_unregister__shrinker(struct drm_i915_private *i915)
{
	WARN_ON(unregister_vmap_purge_notifier(&i915->mm.vmap_notifier));
	WARN_ON(unregister_oom_notifier(&i915->mm.oom_notifier));
	unregister_shrinker(&i915->mm.shrinker);
}

void i915_gem_shrinker_taints_mutex(struct drm_i915_private *i915,
				    struct mutex *mutex)
{
	bool unlock = false;

	if (!IS_ENABLED(CONFIG_LOCKDEP))
		return;

	if (!lockdep_is_held_type(&i915->drm.struct_mutex, -1)) {
		mutex_acquire(&i915->drm.struct_mutex.dep_map,
			      I915_MM_NORMAL, 0, _RET_IP_);
		unlock = true;
	}

	fs_reclaim_acquire(GFP_KERNEL);

	/*
	 * As we invariably rely on the struct_mutex within the shrinker,
	 * but have a complicated recursion dance, taint all the mutexes used
	 * within the shrinker with the struct_mutex. For completeness, we
	 * taint with all subclass of struct_mutex, even though we should
	 * only need tainting by I915_MM_NORMAL to catch possible ABBA
	 * deadlocks from using struct_mutex inside @mutex.
	 */
	mutex_acquire(&i915->drm.struct_mutex.dep_map,
		      I915_MM_SHRINKER, 0, _RET_IP_);

	mutex_acquire(&mutex->dep_map, 0, 0, _RET_IP_);
	mutex_release(&mutex->dep_map, 0, _RET_IP_);

	mutex_release(&i915->drm.struct_mutex.dep_map, 0, _RET_IP_);

	fs_reclaim_release(GFP_KERNEL);

	if (unlock)
		mutex_release(&i915->drm.struct_mutex.dep_map, 0, _RET_IP_);
}

#define obj_to_i915(obj__) to_i915((obj__)->base.dev)

void i915_gem_object_make_unshrinkable(struct drm_i915_gem_object *obj)
{
	/*
	 * We can only be called while the pages are pinned or when
	 * the pages are released. If pinned, we should only be called
	 * from a single caller under controlled conditions; and on release
	 * only one caller may release us. Neither the two may cross.
	 */
	if (!list_empty(&obj->mm.link)) { /* pinned by caller */
		struct drm_i915_private *i915 = obj_to_i915(obj);
		unsigned long flags;

		spin_lock_irqsave(&i915->mm.obj_lock, flags);
		GEM_BUG_ON(list_empty(&obj->mm.link));

		list_del_init(&obj->mm.link);
		i915->mm.shrink_count--;
		i915->mm.shrink_memory -= obj->base.size;

		spin_unlock_irqrestore(&i915->mm.obj_lock, flags);
	}
}

static void __i915_gem_object_make_shrinkable(struct drm_i915_gem_object *obj,
					      struct list_head *head)
{
	GEM_BUG_ON(!i915_gem_object_has_pages(obj));
	GEM_BUG_ON(!list_empty(&obj->mm.link));

	if (i915_gem_object_is_shrinkable(obj)) {
		struct drm_i915_private *i915 = obj_to_i915(obj);
		unsigned long flags;

		spin_lock_irqsave(&i915->mm.obj_lock, flags);
		GEM_BUG_ON(!kref_read(&obj->base.refcount));

		list_add_tail(&obj->mm.link, head);
		i915->mm.shrink_count++;
		i915->mm.shrink_memory += obj->base.size;

		spin_unlock_irqrestore(&i915->mm.obj_lock, flags);
	}
}

void i915_gem_object_make_shrinkable(struct drm_i915_gem_object *obj)
{
	__i915_gem_object_make_shrinkable(obj,
					  &obj_to_i915(obj)->mm.shrink_list);
}

void i915_gem_object_make_purgeable(struct drm_i915_gem_object *obj)
{
	__i915_gem_object_make_shrinkable(obj,
					  &obj_to_i915(obj)->mm.purge_list);
}
