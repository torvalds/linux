/*
 * Copyright (C) 2013 Red Hat
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

#include "msm_ringbuffer.h"
#include "msm_gpu.h"

struct msm_ringbuffer *msm_ringbuffer_new(struct msm_gpu *gpu, int size)
{
	struct msm_ringbuffer *ring;
	int ret;

	size = ALIGN(size, 4);   /* size should be dword aligned */

	ring = kzalloc(sizeof(*ring), GFP_KERNEL);
	if (!ring) {
		ret = -ENOMEM;
		goto fail;
	}

	ring->gpu = gpu;
	ring->bo = msm_gem_new(gpu->dev, size, MSM_BO_WC);
	if (IS_ERR(ring->bo)) {
		ret = PTR_ERR(ring->bo);
		ring->bo = NULL;
		goto fail;
	}

	ring->start = msm_gem_vaddr_locked(ring->bo);
	if (IS_ERR(ring->start)) {
		ret = PTR_ERR(ring->start);
		goto fail;
	}
	ring->end   = ring->start + (size / 4);
	ring->cur   = ring->start;

	ring->size = size;

	return ring;

fail:
	if (ring)
		msm_ringbuffer_destroy(ring);
	return ERR_PTR(ret);
}

void msm_ringbuffer_destroy(struct msm_ringbuffer *ring)
{
	if (ring->bo)
		drm_gem_object_unreference_unlocked(ring->bo);
	kfree(ring);
}
