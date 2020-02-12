/*
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#ifndef __AMDGPU_CTX_H__
#define __AMDGPU_CTX_H__

#include "amdgpu_ring.h"

struct drm_device;
struct drm_file;
struct amdgpu_fpriv;

#define AMDGPU_MAX_ENTITY_NUM 4

struct amdgpu_ctx_entity {
	uint64_t		sequence;
	struct drm_sched_entity	entity;
	struct dma_fence	*fences[];
};

struct amdgpu_ctx {
	struct kref			refcount;
	struct amdgpu_device		*adev;
	unsigned			reset_counter;
	unsigned			reset_counter_query;
	uint32_t			vram_lost_counter;
	spinlock_t			ring_lock;
	struct amdgpu_ctx_entity	*entities[AMDGPU_HW_IP_NUM][AMDGPU_MAX_ENTITY_NUM];
	bool				preamble_presented;
	enum drm_sched_priority		init_priority;
	enum drm_sched_priority		override_priority;
	struct mutex			lock;
	atomic_t			guilty;
	unsigned long			ras_counter_ce;
	unsigned long			ras_counter_ue;
};

struct amdgpu_ctx_mgr {
	struct amdgpu_device	*adev;
	struct mutex		lock;
	/* protected by lock */
	struct idr		ctx_handles;
};

extern const unsigned int amdgpu_ctx_num_entities[AMDGPU_HW_IP_NUM];

struct amdgpu_ctx *amdgpu_ctx_get(struct amdgpu_fpriv *fpriv, uint32_t id);
int amdgpu_ctx_put(struct amdgpu_ctx *ctx);

int amdgpu_ctx_get_entity(struct amdgpu_ctx *ctx, u32 hw_ip, u32 instance,
			  u32 ring, struct drm_sched_entity **entity);
void amdgpu_ctx_add_fence(struct amdgpu_ctx *ctx,
			  struct drm_sched_entity *entity,
			  struct dma_fence *fence, uint64_t *seq);
struct dma_fence *amdgpu_ctx_get_fence(struct amdgpu_ctx *ctx,
				       struct drm_sched_entity *entity,
				       uint64_t seq);
void amdgpu_ctx_priority_override(struct amdgpu_ctx *ctx,
				  enum drm_sched_priority priority);

int amdgpu_ctx_ioctl(struct drm_device *dev, void *data,
		     struct drm_file *filp);

int amdgpu_ctx_wait_prev_fence(struct amdgpu_ctx *ctx,
			       struct drm_sched_entity *entity);

void amdgpu_ctx_mgr_init(struct amdgpu_ctx_mgr *mgr);
void amdgpu_ctx_mgr_entity_fini(struct amdgpu_ctx_mgr *mgr);
long amdgpu_ctx_mgr_entity_flush(struct amdgpu_ctx_mgr *mgr, long timeout);
void amdgpu_ctx_mgr_fini(struct amdgpu_ctx_mgr *mgr);

void amdgpu_ctx_init_sched(struct amdgpu_device *adev);


#endif
