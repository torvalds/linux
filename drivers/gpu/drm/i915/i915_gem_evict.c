/*
 * Copyright © 2008-2010 Intel Corporation
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
 *    Chris Wilson <chris@chris-wilson.co.uuk>
 *
 */

#include <drm/drmP.h>
#include <drm/i915_drm.h>

#include "i915_drv.h"
#include "intel_drv.h"
#include "i915_trace.h"

I915_SELFTEST_DECLARE(static struct igt_evict_ctl {
	bool fail_if_busy:1;
} igt_evict_ctl;)

static bool ggtt_is_idle(struct drm_i915_private *i915)
{
       struct intel_engine_cs *engine;
       enum intel_engine_id id;

       if (i915->gt.active_requests)
	       return false;

       for_each_engine(engine, i915, id) {
	       if (engine->last_retired_context != i915->kernel_context)
		       return false;
       }

       return true;
}

static int ggtt_flush(struct drm_i915_private *i915)
{
	int err;

	/* Not everything in the GGTT is tracked via vma (otherwise we
	 * could evict as required with minimal stalling) so we are forced
	 * to idle the GPU and explicitly retire outstanding requests in
	 * the hopes that we can then remove contexts and the like only
	 * bound by their active reference.
	 */
	err = i915_gem_switch_to_kernel_context(i915);
	if (err)
		return err;

	err = i915_gem_wait_for_idle(i915,
				     I915_WAIT_INTERRUPTIBLE |
				     I915_WAIT_LOCKED);
	if (err)
		return err;

	return 0;
}

static bool
mark_free(struct drm_mm_scan *scan,
	  struct i915_vma *vma,
	  unsigned int flags,
	  struct list_head *unwind)
{
	if (i915_vma_is_pinned(vma))
		return false;

	if (flags & PIN_NONFAULT && i915_vma_has_userfault(vma))
		return false;

	list_add(&vma->evict_link, unwind);
	return drm_mm_scan_add_block(scan, &vma->node);
}

/**
 * i915_gem_evict_something - Evict vmas to make room for binding a new one
 * @vm: address space to evict from
 * @min_size: size of the desired free space
 * @alignment: alignment constraint of the desired free space
 * @cache_level: cache_level for the desired space
 * @start: start (inclusive) of the range from which to evict objects
 * @end: end (exclusive) of the range from which to evict objects
 * @flags: additional flags to control the eviction algorithm
 *
 * This function will try to evict vmas until a free space satisfying the
 * requirements is found. Callers must check first whether any such hole exists
 * already before calling this function.
 *
 * This function is used by the object/vma binding code.
 *
 * Since this function is only used to free up virtual address space it only
 * ignores pinned vmas, and not object where the backing storage itself is
 * pinned. Hence obj->pages_pin_count does not protect against eviction.
 *
 * To clarify: This is for freeing up virtual address space, not for freeing
 * memory in e.g. the shrinker.
 */
int
i915_gem_evict_something(struct i915_address_space *vm,
			 u64 min_size, u64 alignment,
			 unsigned cache_level,
			 u64 start, u64 end,
			 unsigned flags)
{
	struct drm_i915_private *dev_priv = vm->i915;
	struct drm_mm_scan scan;
	struct list_head eviction_list;
	struct list_head *phases[] = {
		&vm->inactive_list,
		&vm->active_list,
		NULL,
	}, **phase;
	struct i915_vma *vma, *next;
	struct drm_mm_node *node;
	enum drm_mm_insert_mode mode;
	int ret;

	lockdep_assert_held(&vm->i915->drm.struct_mutex);
	trace_i915_gem_evict(vm, min_size, alignment, flags);

	/*
	 * The goal is to evict objects and amalgamate space in LRU order.
	 * The oldest idle objects reside on the inactive list, which is in
	 * retirement order. The next objects to retire are those in flight,
	 * on the active list, again in retirement order.
	 *
	 * The retirement sequence is thus:
	 *   1. Inactive objects (already retired)
	 *   2. Active objects (will stall on unbinding)
	 *
	 * On each list, the oldest objects lie at the HEAD with the freshest
	 * object on the TAIL.
	 */
	mode = DRM_MM_INSERT_BEST;
	if (flags & PIN_HIGH)
		mode = DRM_MM_INSERT_HIGH;
	if (flags & PIN_MAPPABLE)
		mode = DRM_MM_INSERT_LOW;
	drm_mm_scan_init_with_range(&scan, &vm->mm,
				    min_size, alignment, cache_level,
				    start, end, mode);

	/*
	 * Retire before we search the active list. Although we have
	 * reasonable accuracy in our retirement lists, we may have
	 * a stray pin (preventing eviction) that can only be resolved by
	 * retiring.
	 */
	if (!(flags & PIN_NONBLOCK))
		i915_gem_retire_requests(dev_priv);
	else
		phases[1] = NULL;

search_again:
	INIT_LIST_HEAD(&eviction_list);
	phase = phases;
	do {
		list_for_each_entry(vma, *phase, vm_link)
			if (mark_free(&scan, vma, flags, &eviction_list))
				goto found;
	} while (*++phase);

	/* Nothing found, clean up and bail out! */
	list_for_each_entry_safe(vma, next, &eviction_list, evict_link) {
		ret = drm_mm_scan_remove_block(&scan, &vma->node);
		BUG_ON(ret);
	}

	/*
	 * Can we unpin some objects such as idle hw contents,
	 * or pending flips? But since only the GGTT has global entries
	 * such as scanouts, rinbuffers and contexts, we can skip the
	 * purge when inspecting per-process local address spaces.
	 */
	if (!i915_is_ggtt(vm) || flags & PIN_NONBLOCK)
		return -ENOSPC;

	/*
	 * Not everything in the GGTT is tracked via VMA using
	 * i915_vma_move_to_active(), otherwise we could evict as required
	 * with minimal stalling. Instead we are forced to idle the GPU and
	 * explicitly retire outstanding requests which will then remove
	 * the pinning for active objects such as contexts and ring,
	 * enabling us to evict them on the next iteration.
	 *
	 * To ensure that all user contexts are evictable, we perform
	 * a switch to the perma-pinned kernel context. This all also gives
	 * us a termination condition, when the last retired context is
	 * the kernel's there is no more we can evict.
	 */
	if (!ggtt_is_idle(dev_priv)) {
		if (I915_SELFTEST_ONLY(igt_evict_ctl.fail_if_busy))
			return -EBUSY;

		ret = ggtt_flush(dev_priv);
		if (ret)
			return ret;

		goto search_again;
	}

	/*
	 * If we still have pending pageflip completions, drop
	 * back to userspace to give our workqueues time to
	 * acquire our locks and unpin the old scanouts.
	 */
	return intel_has_pending_fb_unpin(dev_priv) ? -EAGAIN : -ENOSPC;

found:
	/* drm_mm doesn't allow any other other operations while
	 * scanning, therefore store to-be-evicted objects on a
	 * temporary list and take a reference for all before
	 * calling unbind (which may remove the active reference
	 * of any of our objects, thus corrupting the list).
	 */
	list_for_each_entry_safe(vma, next, &eviction_list, evict_link) {
		if (drm_mm_scan_remove_block(&scan, &vma->node))
			__i915_vma_pin(vma);
		else
			list_del(&vma->evict_link);
	}

	/* Unbinding will emit any required flushes */
	ret = 0;
	list_for_each_entry_safe(vma, next, &eviction_list, evict_link) {
		__i915_vma_unpin(vma);
		if (ret == 0)
			ret = i915_vma_unbind(vma);
	}

	while (ret == 0 && (node = drm_mm_scan_color_evict(&scan))) {
		vma = container_of(node, struct i915_vma, node);
		ret = i915_vma_unbind(vma);
	}

	return ret;
}

/**
 * i915_gem_evict_for_vma - Evict vmas to make room for binding a new one
 * @vm: address space to evict from
 * @target: range (and color) to evict for
 * @flags: additional flags to control the eviction algorithm
 *
 * This function will try to evict vmas that overlap the target node.
 *
 * To clarify: This is for freeing up virtual address space, not for freeing
 * memory in e.g. the shrinker.
 */
int i915_gem_evict_for_node(struct i915_address_space *vm,
			    struct drm_mm_node *target,
			    unsigned int flags)
{
	LIST_HEAD(eviction_list);
	struct drm_mm_node *node;
	u64 start = target->start;
	u64 end = start + target->size;
	struct i915_vma *vma, *next;
	bool check_color;
	int ret = 0;

	lockdep_assert_held(&vm->i915->drm.struct_mutex);
	GEM_BUG_ON(!IS_ALIGNED(start, I915_GTT_PAGE_SIZE));
	GEM_BUG_ON(!IS_ALIGNED(end, I915_GTT_PAGE_SIZE));

	trace_i915_gem_evict_node(vm, target, flags);

	/* Retire before we search the active list. Although we have
	 * reasonable accuracy in our retirement lists, we may have
	 * a stray pin (preventing eviction) that can only be resolved by
	 * retiring.
	 */
	if (!(flags & PIN_NONBLOCK))
		i915_gem_retire_requests(vm->i915);

	check_color = vm->mm.color_adjust;
	if (check_color) {
		/* Expand search to cover neighbouring guard pages (or lack!) */
		if (start)
			start -= I915_GTT_PAGE_SIZE;

		/* Always look at the page afterwards to avoid the end-of-GTT */
		end += I915_GTT_PAGE_SIZE;
	}
	GEM_BUG_ON(start >= end);

	drm_mm_for_each_node_in_range(node, &vm->mm, start, end) {
		/* If we find any non-objects (!vma), we cannot evict them */
		if (node->color == I915_COLOR_UNEVICTABLE) {
			ret = -ENOSPC;
			break;
		}

		GEM_BUG_ON(!node->allocated);
		vma = container_of(node, typeof(*vma), node);

		/* If we are using coloring to insert guard pages between
		 * different cache domains within the address space, we have
		 * to check whether the objects on either side of our range
		 * abutt and conflict. If they are in conflict, then we evict
		 * those as well to make room for our guard pages.
		 */
		if (check_color) {
			if (node->start + node->size == target->start) {
				if (node->color == target->color)
					continue;
			}
			if (node->start == target->start + target->size) {
				if (node->color == target->color)
					continue;
			}
		}

		if (flags & PIN_NONBLOCK &&
		    (i915_vma_is_pinned(vma) || i915_vma_is_active(vma))) {
			ret = -ENOSPC;
			break;
		}

		if (flags & PIN_NONFAULT && i915_vma_has_userfault(vma)) {
			ret = -ENOSPC;
			break;
		}

		/* Overlap of objects in the same batch? */
		if (i915_vma_is_pinned(vma)) {
			ret = -ENOSPC;
			if (vma->exec_flags &&
			    *vma->exec_flags & EXEC_OBJECT_PINNED)
				ret = -EINVAL;
			break;
		}

		/* Never show fear in the face of dragons!
		 *
		 * We cannot directly remove this node from within this
		 * iterator and as with i915_gem_evict_something() we employ
		 * the vma pin_count in order to prevent the action of
		 * unbinding one vma from freeing (by dropping its active
		 * reference) another in our eviction list.
		 */
		__i915_vma_pin(vma);
		list_add(&vma->evict_link, &eviction_list);
	}

	list_for_each_entry_safe(vma, next, &eviction_list, evict_link) {
		__i915_vma_unpin(vma);
		if (ret == 0)
			ret = i915_vma_unbind(vma);
	}

	return ret;
}

/**
 * i915_gem_evict_vm - Evict all idle vmas from a vm
 * @vm: Address space to cleanse
 *
 * This function evicts all vmas from a vm.
 *
 * This is used by the execbuf code as a last-ditch effort to defragment the
 * address space.
 *
 * To clarify: This is for freeing up virtual address space, not for freeing
 * memory in e.g. the shrinker.
 */
int i915_gem_evict_vm(struct i915_address_space *vm)
{
	struct list_head *phases[] = {
		&vm->inactive_list,
		&vm->active_list,
		NULL
	}, **phase;
	struct list_head eviction_list;
	struct i915_vma *vma, *next;
	int ret;

	lockdep_assert_held(&vm->i915->drm.struct_mutex);
	trace_i915_gem_evict_vm(vm);

	/* Switch back to the default context in order to unpin
	 * the existing context objects. However, such objects only
	 * pin themselves inside the global GTT and performing the
	 * switch otherwise is ineffective.
	 */
	if (i915_is_ggtt(vm)) {
		ret = ggtt_flush(vm->i915);
		if (ret)
			return ret;
	}

	INIT_LIST_HEAD(&eviction_list);
	phase = phases;
	do {
		list_for_each_entry(vma, *phase, vm_link) {
			if (i915_vma_is_pinned(vma))
				continue;

			__i915_vma_pin(vma);
			list_add(&vma->evict_link, &eviction_list);
		}
	} while (*++phase);

	ret = 0;
	list_for_each_entry_safe(vma, next, &eviction_list, evict_link) {
		__i915_vma_unpin(vma);
		if (ret == 0)
			ret = i915_vma_unbind(vma);
	}
	return ret;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/i915_gem_evict.c"
#endif
