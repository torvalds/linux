/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2016 Intel Corporation
 */

#include <linux/jiffies.h>

#include <drm/drm_file.h>

#include "i915_drv.h"
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
	struct drm_i915_file_private *file_priv = file->driver_priv;
	unsigned long recent_enough = jiffies - DRM_I915_THROTTLE_JIFFIES;
	struct i915_request *request, *target = NULL;
	long ret;

	/* ABI: return -EIO if already wedged */
	ret = i915_terminally_wedged(to_i915(dev));
	if (ret)
		return ret;

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
		i915_request_get(target);
	spin_unlock(&file_priv->mm.lock);

	if (!target)
		return 0;

	ret = i915_request_wait(target,
				I915_WAIT_INTERRUPTIBLE,
				MAX_SCHEDULE_TIMEOUT);
	i915_request_put(target);

	return ret < 0 ? ret : 0;
}
