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

#ifndef __MSM_RINGBUFFER_H__
#define __MSM_RINGBUFFER_H__

#include "msm_drv.h"

struct msm_ringbuffer {
	struct msm_gpu *gpu;
	int size;
	struct drm_gem_object *bo;
	uint32_t *start, *end, *cur;
};

struct msm_ringbuffer *msm_ringbuffer_new(struct msm_gpu *gpu, int size);
void msm_ringbuffer_destroy(struct msm_ringbuffer *ring);

/* ringbuffer helpers (the parts that are same for a3xx/a2xx/z180..) */

static inline void
OUT_RING(struct msm_ringbuffer *ring, uint32_t data)
{
	if (ring->cur == ring->end)
		ring->cur = ring->start;
	*(ring->cur++) = data;
}

#endif /* __MSM_RINGBUFFER_H__ */
