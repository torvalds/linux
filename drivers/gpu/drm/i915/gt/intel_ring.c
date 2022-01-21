// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "gem/i915_gem_lmem.h"
#include "gem/i915_gem_object.h"

#include "i915_drv.h"
#include "i915_vma.h"
#include "intel_engine.h"
#include "intel_gpu_commands.h"
#include "intel_ring.h"
#include "intel_timeline.h"

unsigned int intel_ring_update_space(struct intel_ring *ring)
{
	unsigned int space;

	space = __intel_ring_space(ring->head, ring->emit, ring->size);

	ring->space = space;
	return space;
}

void __intel_ring_pin(struct intel_ring *ring)
{
	GEM_BUG_ON(!atomic_read(&ring->pin_count));
	atomic_inc(&ring->pin_count);
}

int intel_ring_pin(struct intel_ring *ring, struct i915_gem_ww_ctx *ww)
{
	struct i915_vma *vma = ring->vma;
	unsigned int flags;
	void *addr;
	int ret;

	if (atomic_fetch_inc(&ring->pin_count))
		return 0;

	/* Ring wraparound at offset 0 sometimes hangs. No idea why. */
	flags = PIN_OFFSET_BIAS | i915_ggtt_pin_bias(vma);

	if (i915_gem_object_is_stolen(vma->obj))
		flags |= PIN_MAPPABLE;
	else
		flags |= PIN_HIGH;

	ret = i915_ggtt_pin(vma, ww, 0, flags);
	if (unlikely(ret))
		goto err_unpin;

	if (i915_vma_is_map_and_fenceable(vma)) {
		addr = (void __force *)i915_vma_pin_iomap(vma);
	} else {
		int type = i915_coherent_map_type(vma->vm->i915, vma->obj, false);

		addr = i915_gem_object_pin_map(vma->obj, type);
	}

	if (IS_ERR(addr)) {
		ret = PTR_ERR(addr);
		goto err_ring;
	}

	i915_vma_make_unshrinkable(vma);

	/* Discard any unused bytes beyond that submitted to hw. */
	intel_ring_reset(ring, ring->emit);

	ring->vaddr = addr;
	return 0;

err_ring:
	i915_vma_unpin(vma);
err_unpin:
	atomic_dec(&ring->pin_count);
	return ret;
}

void intel_ring_reset(struct intel_ring *ring, u32 tail)
{
	tail = intel_ring_wrap(ring, tail);
	ring->tail = tail;
	ring->head = tail;
	ring->emit = tail;
	intel_ring_update_space(ring);
}

void intel_ring_unpin(struct intel_ring *ring)
{
	struct i915_vma *vma = ring->vma;

	if (!atomic_dec_and_test(&ring->pin_count))
		return;

	i915_vma_unset_ggtt_write(vma);
	if (i915_vma_is_map_and_fenceable(vma))
		i915_vma_unpin_iomap(vma);
	else
		i915_gem_object_unpin_map(vma->obj);

	i915_vma_make_purgeable(vma);
	i915_vma_unpin(vma);
}

static struct i915_vma *create_ring_vma(struct i915_ggtt *ggtt, int size)
{
	struct i915_address_space *vm = &ggtt->vm;
	struct drm_i915_private *i915 = vm->i915;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;

	obj = i915_gem_object_create_lmem(i915, size, I915_BO_ALLOC_VOLATILE |
					  I915_BO_ALLOC_PM_VOLATILE);
	if (IS_ERR(obj) && i915_ggtt_has_aperture(ggtt))
		obj = i915_gem_object_create_stolen(i915, size);
	if (IS_ERR(obj))
		obj = i915_gem_object_create_internal(i915, size);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	/*
	 * Mark ring buffers as read-only from GPU side (so no stray overwrites)
	 * if supported by the platform's GGTT.
	 */
	if (vm->has_read_only)
		i915_gem_object_set_readonly(obj);

	vma = i915_vma_instance(obj, vm, NULL);
	if (IS_ERR(vma))
		goto err;

	return vma;

err:
	i915_gem_object_put(obj);
	return vma;
}

struct intel_ring *
intel_engine_create_ring(struct intel_engine_cs *engine, int size)
{
	struct drm_i915_private *i915 = engine->i915;
	struct intel_ring *ring;
	struct i915_vma *vma;

	GEM_BUG_ON(!is_power_of_2(size));
	GEM_BUG_ON(RING_CTL_SIZE(size) & ~RING_NR_PAGES);

	ring = kzalloc(sizeof(*ring), GFP_KERNEL);
	if (!ring)
		return ERR_PTR(-ENOMEM);

	kref_init(&ring->ref);
	ring->size = size;
	ring->wrap = BITS_PER_TYPE(ring->size) - ilog2(size);

	/*
	 * Workaround an erratum on the i830 which causes a hang if
	 * the TAIL pointer points to within the last 2 cachelines
	 * of the buffer.
	 */
	ring->effective_size = size;
	if (IS_I830(i915) || IS_I845G(i915))
		ring->effective_size -= 2 * CACHELINE_BYTES;

	intel_ring_update_space(ring);

	vma = create_ring_vma(engine->gt->ggtt, size);
	if (IS_ERR(vma)) {
		kfree(ring);
		return ERR_CAST(vma);
	}
	ring->vma = vma;

	return ring;
}

void intel_ring_free(struct kref *ref)
{
	struct intel_ring *ring = container_of(ref, typeof(*ring), ref);

	i915_vma_put(ring->vma);
	kfree(ring);
}

static noinline int
wait_for_space(struct intel_ring *ring,
	       struct intel_timeline *tl,
	       unsigned int bytes)
{
	struct i915_request *target;
	long timeout;

	if (intel_ring_update_space(ring) >= bytes)
		return 0;

	GEM_BUG_ON(list_empty(&tl->requests));
	list_for_each_entry(target, &tl->requests, link) {
		if (target->ring != ring)
			continue;

		/* Would completion of this request free enough space? */
		if (bytes <= __intel_ring_space(target->postfix,
						ring->emit, ring->size))
			break;
	}

	if (GEM_WARN_ON(&target->link == &tl->requests))
		return -ENOSPC;

	timeout = i915_request_wait(target,
				    I915_WAIT_INTERRUPTIBLE,
				    MAX_SCHEDULE_TIMEOUT);
	if (timeout < 0)
		return timeout;

	i915_request_retire_upto(target);

	intel_ring_update_space(ring);
	GEM_BUG_ON(ring->space < bytes);
	return 0;
}

u32 *intel_ring_begin(struct i915_request *rq, unsigned int num_dwords)
{
	struct intel_ring *ring = rq->ring;
	const unsigned int remain_usable = ring->effective_size - ring->emit;
	const unsigned int bytes = num_dwords * sizeof(u32);
	unsigned int need_wrap = 0;
	unsigned int total_bytes;
	u32 *cs;

	/* Packets must be qword aligned. */
	GEM_BUG_ON(num_dwords & 1);

	total_bytes = bytes + rq->reserved_space;
	GEM_BUG_ON(total_bytes > ring->effective_size);

	if (unlikely(total_bytes > remain_usable)) {
		const int remain_actual = ring->size - ring->emit;

		if (bytes > remain_usable) {
			/*
			 * Not enough space for the basic request. So need to
			 * flush out the remainder and then wait for
			 * base + reserved.
			 */
			total_bytes += remain_actual;
			need_wrap = remain_actual | 1;
		} else  {
			/*
			 * The base request will fit but the reserved space
			 * falls off the end. So we don't need an immediate
			 * wrap and only need to effectively wait for the
			 * reserved size from the start of ringbuffer.
			 */
			total_bytes = rq->reserved_space + remain_actual;
		}
	}

	if (unlikely(total_bytes > ring->space)) {
		int ret;

		/*
		 * Space is reserved in the ringbuffer for finalising the
		 * request, as that cannot be allowed to fail. During request
		 * finalisation, reserved_space is set to 0 to stop the
		 * overallocation and the assumption is that then we never need
		 * to wait (which has the risk of failing with EINTR).
		 *
		 * See also i915_request_alloc() and i915_request_add().
		 */
		GEM_BUG_ON(!rq->reserved_space);

		ret = wait_for_space(ring,
				     i915_request_timeline(rq),
				     total_bytes);
		if (unlikely(ret))
			return ERR_PTR(ret);
	}

	if (unlikely(need_wrap)) {
		need_wrap &= ~1;
		GEM_BUG_ON(need_wrap > ring->space);
		GEM_BUG_ON(ring->emit + need_wrap > ring->size);
		GEM_BUG_ON(!IS_ALIGNED(need_wrap, sizeof(u64)));

		/* Fill the tail with MI_NOOP */
		memset64(ring->vaddr + ring->emit, 0, need_wrap / sizeof(u64));
		ring->space -= need_wrap;
		ring->emit = 0;
	}

	GEM_BUG_ON(ring->emit > ring->size - bytes);
	GEM_BUG_ON(ring->space < bytes);
	cs = ring->vaddr + ring->emit;
	GEM_DEBUG_EXEC(memset32(cs, POISON_INUSE, bytes / sizeof(*cs)));
	ring->emit += bytes;
	ring->space -= bytes;

	return cs;
}

/* Align the ring tail to a cacheline boundary */
int intel_ring_cacheline_align(struct i915_request *rq)
{
	int num_dwords;
	void *cs;

	num_dwords = (rq->ring->emit & (CACHELINE_BYTES - 1)) / sizeof(u32);
	if (num_dwords == 0)
		return 0;

	num_dwords = CACHELINE_DWORDS - num_dwords;
	GEM_BUG_ON(num_dwords & 1);

	cs = intel_ring_begin(rq, num_dwords);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	memset64(cs, (u64)MI_NOOP << 32 | MI_NOOP, num_dwords / 2);
	intel_ring_advance(rq, cs + num_dwords);

	GEM_BUG_ON(rq->ring->emit & (CACHELINE_BYTES - 1));
	return 0;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftest_ring.c"
#endif
