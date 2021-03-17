/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#ifndef __MSM_RINGBUFFER_H__
#define __MSM_RINGBUFFER_H__

#include "msm_drv.h"

#define rbmemptr(ring, member)  \
	((ring)->memptrs_iova + offsetof(struct msm_rbmemptrs, member))

#define rbmemptr_stats(ring, index, member) \
	(rbmemptr((ring), stats) + \
	 ((index) * sizeof(struct msm_gpu_submit_stats)) + \
	 offsetof(struct msm_gpu_submit_stats, member))

struct msm_gpu_submit_stats {
	u64 cpcycles_start;
	u64 cpcycles_end;
	u64 alwayson_start;
	u64 alwayson_end;
};

#define MSM_GPU_SUBMIT_STATS_COUNT 64

struct msm_rbmemptrs {
	volatile uint32_t rptr;
	volatile uint32_t fence;

	volatile struct msm_gpu_submit_stats stats[MSM_GPU_SUBMIT_STATS_COUNT];
	volatile u64 ttbr0;
};

struct msm_ringbuffer {
	struct msm_gpu *gpu;
	int id;
	struct drm_gem_object *bo;
	uint32_t *start, *end, *cur, *next;
	struct list_head submits;
	uint64_t iova;
	uint32_t seqno;
	uint32_t hangcheck_fence;
	struct msm_rbmemptrs *memptrs;
	uint64_t memptrs_iova;
	struct msm_fence_context *fctx;
	spinlock_t lock;
};

struct msm_ringbuffer *msm_ringbuffer_new(struct msm_gpu *gpu, int id,
		void *memptrs, uint64_t memptrs_iova);
void msm_ringbuffer_destroy(struct msm_ringbuffer *ring);

/* ringbuffer helpers (the parts that are same for a3xx/a2xx/z180..) */

static inline void
OUT_RING(struct msm_ringbuffer *ring, uint32_t data)
{
	/*
	 * ring->next points to the current command being written - it won't be
	 * committed as ring->cur until the flush
	 */
	if (ring->next == ring->end)
		ring->next = ring->start;
	*(ring->next++) = data;
}

#endif /* __MSM_RINGBUFFER_H__ */
