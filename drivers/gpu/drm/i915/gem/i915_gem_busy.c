// SPDX-License-Identifier: MIT
/*
 * Copyright © 2014-2016 Intel Corporation
 */

#include <linux/dma-fence-array.h>

#include "gt/intel_engine.h"

#include "i915_gem_ioctls.h"
#include "i915_gem_object.h"

static __always_inline u32 __busy_read_flag(u16 id)
{
	if (id == (u16)I915_ENGINE_CLASS_INVALID)
		return 0xffff0000u;

	GEM_BUG_ON(id >= 16);
	return 0x10000u << id;
}

static __always_inline u32 __busy_write_id(u16 id)
{
	/*
	 * The uABI guarantees an active writer is also amongst the read
	 * engines. This would be true if we accessed the activity tracking
	 * under the lock, but as we perform the lookup of the object and
	 * its activity locklessly we can not guarantee that the last_write
	 * being active implies that we have set the same engine flag from
	 * last_read - hence we always set both read and write busy for
	 * last_write.
	 */
	if (id == (u16)I915_ENGINE_CLASS_INVALID)
		return 0xffffffffu;

	return (id + 1) | __busy_read_flag(id);
}

static __always_inline unsigned int
__busy_set_if_active(struct dma_fence *fence, u32 (*flag)(u16 id))
{
	const struct i915_request *rq;

	/*
	 * We have to check the current hw status of the fence as the uABI
	 * guarantees forward progress. We could rely on the idle worker
	 * to eventually flush us, but to minimise latency just ask the
	 * hardware.
	 *
	 * Note we only report on the status of native fences and we currently
	 * have two native fences:
	 *
	 * 1. A composite fence (dma_fence_array) constructed of i915 requests
	 * created during a parallel submission. In this case we deconstruct the
	 * composite fence into individual i915 requests and check the status of
	 * each request.
	 *
	 * 2. A single i915 request.
	 */
	if (dma_fence_is_array(fence)) {
		struct dma_fence_array *array = to_dma_fence_array(fence);
		struct dma_fence **child = array->fences;
		unsigned int nchild = array->num_fences;

		do {
			struct dma_fence *current_fence = *child++;

			/* Not an i915 fence, can't be busy per above */
			if (!dma_fence_is_i915(current_fence) ||
			    !test_bit(I915_FENCE_FLAG_COMPOSITE,
				      &current_fence->flags)) {
				return 0;
			}

			rq = to_request(current_fence);
			if (!i915_request_completed(rq))
				return flag(rq->engine->uabi_class);
		} while (--nchild);

		/* All requests in array complete, not busy */
		return 0;
	} else {
		if (!dma_fence_is_i915(fence))
			return 0;

		rq = to_request(fence);
		if (i915_request_completed(rq))
			return 0;

		/* Beware type-expansion follies! */
		BUILD_BUG_ON(!typecheck(u16, rq->engine->uabi_class));
		return flag(rq->engine->uabi_class);
	}
}

static __always_inline unsigned int
busy_check_reader(struct dma_fence *fence)
{
	return __busy_set_if_active(fence, __busy_read_flag);
}

static __always_inline unsigned int
busy_check_writer(struct dma_fence *fence)
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
	struct dma_resv_iter cursor;
	struct dma_fence *fence;
	int err;

	err = -ENOENT;
	rcu_read_lock();
	obj = i915_gem_object_lookup_rcu(file, args->handle);
	if (!obj)
		goto out;

	/*
	 * A discrepancy here is that we do not report the status of
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
	 *		!dma_resv_test_signaled(obj->resv, DMA_RESV_USAGE_READ);
	 * to report the overall busyness. This is what the wait-ioctl does.
	 *
	 */
	args->busy = 0;
	dma_resv_iter_begin(&cursor, obj->base.resv, DMA_RESV_USAGE_READ);
	dma_resv_for_each_fence_unlocked(&cursor, fence) {
		if (dma_resv_iter_is_restarted(&cursor))
			args->busy = 0;

		if (dma_resv_iter_usage(&cursor) <= DMA_RESV_USAGE_WRITE)
			/* Translate the write fences to the READ *and* WRITE engine */
			args->busy |= busy_check_writer(fence);
		else
			/* Translate read fences to READ set of engines */
			args->busy |= busy_check_reader(fence);
	}
	dma_resv_iter_end(&cursor);

	err = 0;
out:
	rcu_read_unlock();
	return err;
}
