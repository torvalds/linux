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
	unsigned context;
	/* last_fence == completed_fence --> no pending work */
	uint32_t last_fence;          /* last assigned fence */
	uint32_t completed_fence;     /* last completed fence */
	wait_queue_head_t event;
	spinlock_t spinlock;
};

struct msm_fence_context * msm_fence_context_alloc(struct drm_device *dev,
		const char *name);
void msm_fence_context_free(struct msm_fence_context *fctx);

int msm_wait_fence(struct msm_fence_context *fctx, uint32_t fence,
		ktime_t *timeout, bool interruptible);
int msm_queue_fence_cb(struct msm_fence_context *fctx,
		struct msm_fence_cb *cb, uint32_t fence);
void msm_update_fence(struct msm_fence_context *fctx, uint32_t fence);

struct fence * msm_fence_alloc(struct msm_fence_context *fctx);

#endif
