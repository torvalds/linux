/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2016 Intel Corporation
 */

#include <linux/jiffies.h>

#include <drm/drm_file.h>

#include "i915_drv.h"
#include "i915_gem_context.h"
#include "i915_gem_ioctls.h"
#include "i915_gem_object.h"

/*
 * 20ms is a fairly arbitrary limit (greater than the average frame time)
 * chosen to prevent the CPU getting more than a frame ahead of the GPU
 * (when using lax throttling for the frontbuffer). We also use it to
 * offer free GPU waitboosts for severely congested workloads.
 */
#define DRM_I915_THROTTLE_JIFFIES msecs_to_jiffies(20)

/*
 * Throttle our rendering by waiting until the ring has completed our requests
 * emitted over 20 msec ago.
 *
 * Note that if we were to use the current jiffies each time around the loop,
 * we wouldn't escape the function with any frames outstanding if the time to
 * render a frame was over 20ms.
 *
 * This should get us reasonable parallelism between CPU and GPU but also
 * relatively low latency when blocking on a particular request to finish.
 */
int
i915_gem_throttle_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file)
{
	const unsigned long recent_enough = jiffies - DRM_I915_THROTTLE_JIFFIES;
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct drm_i915_private *i915 = to_i915(dev);
	struct i915_gem_context *ctx;
	unsigned long idx;
	long ret;

	/* ABI: return -EIO if already wedged */
	ret = intel_gt_terminally_wedged(to_gt(i915));
	if (ret)
		return ret;

	rcu_read_lock();
	xa_for_each(&file_priv->context_xa, idx, ctx) {
		struct i915_gem_engines_iter it;
		struct intel_context *ce;

		if (!kref_get_unless_zero(&ctx->ref))
			continue;
		rcu_read_unlock();

		for_each_gem_engine(ce,
				    i915_gem_context_lock_engines(ctx),
				    it) {
			struct i915_request *rq, *target = NULL;

			if (!ce->timeline)
				continue;

			mutex_lock(&ce->timeline->mutex);
			list_for_each_entry_reverse(rq,
						    &ce->timeline->requests,
						    link) {
				if (i915_request_completed(rq))
					break;

				if (time_after(rq->emitted_jiffies,
					       recent_enough))
					continue;

				target = i915_request_get(rq);
				break;
			}
			mutex_unlock(&ce->timeline->mutex);
			if (!target)
				continue;

			ret = i915_request_wait(target,
						I915_WAIT_INTERRUPTIBLE,
						MAX_SCHEDULE_TIMEOUT);
			i915_request_put(target);
			if (ret < 0)
				break;
		}
		i915_gem_context_unlock_engines(ctx);
		i915_gem_context_put(ctx);

		rcu_read_lock();
	}
	rcu_read_unlock();

	return ret < 0 ? ret : 0;
}
