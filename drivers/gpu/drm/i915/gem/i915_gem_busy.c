/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2016 Intel Corporation
 */

#include "gt/intel_engine.h"

#include "i915_gem_ioctls.h"
#include "i915_gem_object.h"

static __always_inline u32 __busy_read_flag(u8 id)
{
	if (id == (u8)I915_ENGINE_CLASS_INVALID)
		return 0xffff0000u;

	GEM_BUG_ON(id >= 16);
	return 0x10000u << id;
}

static __always_inline u32 __busy_write_id(u8 id)
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
	if (id == (u8)I915_ENGINE_CLASS_INVALID)
		return 0xffffffffu;

	return (id + 1) | __busy_read_flag(id);
}

static __always_inline unsigned int
__busy_set_if_active(const struct dma_fence *fence, u32 (*flag)(u8 id))
{
	const struct i915_request *rq;

	/*
	 * We have to check the current hw status of the fence as the uABI
	 * guarantees forward progress. We could rely on the idle worker
	 * to eventually flush us, but to minimise latency just ask the
	 * hardware.
	 *
	 * Note we only report on the status of native fences.
	 */
	if (!dma_fence_is_i915(fence))
		return 0;

	/* opencode to_request() in order to avoid const warnings */
	rq = container_of(fence, const struct i915_request, fence);
	if (i915_request_completed(rq))
		return 0;

	/* Beware type-expansion follies! */
	BUILD_BUG_ON(!typecheck(u8, rq->engine->uabi_class));
	return flag(rq->engine->uabi_class);
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
	struct dma_resv_list *list;
	unsigned int seq;
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
	 *		!dma_resv_test_signaled_rcu(obj->resv, true);
	 * to report the overall busyness. This is what the wait-ioctl does.
	 *
	 */
retry:
	seq = raw_read_seqcount(&obj->base.resv->seq);

	/* Translate the exclusive fence to the READ *and* WRITE engine */
	args->busy =
		busy_check_writer(rcu_dereference(obj->base.resv->fence_excl));

	/* Translate shared fences to READ set of engines */
	list = rcu_dereference(obj->base.resv->fence);
	if (list) {
		unsigned int shared_count = list->shared_count, i;

		for (i = 0; i < shared_count; ++i) {
			struct dma_fence *fence =
				rcu_dereference(list->shared[i]);

			args->busy |= busy_check_reader(fence);
		}
	}

	if (args->busy && read_seqcount_retry(&obj->base.resv->seq, seq))
		goto retry;

	err = 0;
out:
	rcu_read_unlock();
	return err;
}
