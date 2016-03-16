/*
 * Copyright (C) 2013-2016 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "msm_drv.h"
#include "msm_fence.h"
#include "msm_gpu.h"

static inline bool fence_completed(struct drm_device *dev, uint32_t fence)
{
	struct msm_drm_private *priv = dev->dev_private;
	return (int32_t)(priv->completed_fence - fence) >= 0;
}

int msm_wait_fence(struct drm_device *dev, uint32_t fence,
		ktime_t *timeout , bool interruptible)
{
	struct msm_drm_private *priv = dev->dev_private;
	int ret;

	if (!priv->gpu)
		return 0;

	if (fence > priv->gpu->submitted_fence) {
		DRM_ERROR("waiting on invalid fence: %u (of %u)\n",
				fence, priv->gpu->submitted_fence);
		return -EINVAL;
	}

	if (!timeout) {
		/* no-wait: */
		ret = fence_completed(dev, fence) ? 0 : -EBUSY;
	} else {
		unsigned long remaining_jiffies = timeout_to_jiffies(timeout);

		if (interruptible)
			ret = wait_event_interruptible_timeout(priv->fence_event,
				fence_completed(dev, fence),
				remaining_jiffies);
		else
			ret = wait_event_timeout(priv->fence_event,
				fence_completed(dev, fence),
				remaining_jiffies);

		if (ret == 0) {
			DBG("timeout waiting for fence: %u (completed: %u)",
					fence, priv->completed_fence);
			ret = -ETIMEDOUT;
		} else if (ret != -ERESTARTSYS) {
			ret = 0;
		}
	}

	return ret;
}

int msm_queue_fence_cb(struct drm_device *dev,
		struct msm_fence_cb *cb, uint32_t fence)
{
	struct msm_drm_private *priv = dev->dev_private;
	int ret = 0;

	mutex_lock(&dev->struct_mutex);
	if (!list_empty(&cb->work.entry)) {
		ret = -EINVAL;
	} else if (fence > priv->completed_fence) {
		cb->fence = fence;
		list_add_tail(&cb->work.entry, &priv->fence_cbs);
	} else {
		queue_work(priv->wq, &cb->work);
	}
	mutex_unlock(&dev->struct_mutex);

	return ret;
}

/* called from workqueue */
void msm_update_fence(struct drm_device *dev, uint32_t fence)
{
	struct msm_drm_private *priv = dev->dev_private;

	mutex_lock(&dev->struct_mutex);
	priv->completed_fence = max(fence, priv->completed_fence);

	while (!list_empty(&priv->fence_cbs)) {
		struct msm_fence_cb *cb;

		cb = list_first_entry(&priv->fence_cbs,
				struct msm_fence_cb, work.entry);

		if (cb->fence > priv->completed_fence)
			break;

		list_del_init(&cb->work.entry);
		queue_work(priv->wq, &cb->work);
	}

	mutex_unlock(&dev->struct_mutex);

	wake_up_all(&priv->fence_event);
}

void __msm_fence_worker(struct work_struct *work)
{
	struct msm_fence_cb *cb = container_of(work, struct msm_fence_cb, work);
	cb->func(cb);
}
