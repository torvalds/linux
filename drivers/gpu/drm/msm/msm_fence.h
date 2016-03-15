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

#ifndef __MSM_FENCE_H__
#define __MSM_FENCE_H__

#include "msm_drv.h"

struct msm_fence_context {
	struct drm_device *dev;
	const char *name;
	/* last_fence == completed_fence --> no pending work */
	uint32_t last_fence;          /* last assigned fence */
	uint32_t completed_fence;     /* last completed fence */
	wait_queue_head_t event;
	/* callbacks deferred until bo is inactive: */
	struct list_head fence_cbs;
};

struct msm_fence_context * msm_fence_context_alloc(struct drm_device *dev,
		const char *name);
void msm_fence_context_free(struct msm_fence_context *fctx);

/* callback from wq once fence has passed: */
struct msm_fence_cb {
	struct work_struct work;
	uint32_t fence;
	void (*func)(struct msm_fence_cb *cb);
};

void __msm_fence_worker(struct work_struct *work);

#define INIT_FENCE_CB(_cb, _func)  do {                     \
		INIT_WORK(&(_cb)->work, __msm_fence_worker); \
		(_cb)->func = _func;                         \
	} while (0)

int msm_wait_fence(struct msm_fence_context *fctx, uint32_t fence,
		ktime_t *timeout, bool interruptible);
int msm_queue_fence_cb(struct msm_fence_context *fctx,
		struct msm_fence_cb *cb, uint32_t fence);
void msm_update_fence(struct msm_fence_context *fctx, uint32_t fence);

#endif
