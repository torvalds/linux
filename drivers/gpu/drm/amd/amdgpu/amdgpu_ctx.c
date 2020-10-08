/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 * Authors: monk liu <monk.liu@amd.com>
 */

#include <drm/drm_auth.h>
#include "amdgpu.h"
#include "amdgpu_sched.h"
#include "amdgpu_ras.h"
#include <linux/nospec.h>

#define to_amdgpu_ctx_entity(e)	\
	container_of((e), struct amdgpu_ctx_entity, entity)

const unsigned int amdgpu_ctx_num_entities[AMDGPU_HW_IP_NUM] = {
	[AMDGPU_HW_IP_GFX]	=	1,
	[AMDGPU_HW_IP_COMPUTE]	=	4,
	[AMDGPU_HW_IP_DMA]	=	2,
	[AMDGPU_HW_IP_UVD]	=	1,
	[AMDGPU_HW_IP_VCE]	=	1,
	[AMDGPU_HW_IP_UVD_ENC]	=	1,
	[AMDGPU_HW_IP_VCN_DEC]	=	1,
	[AMDGPU_HW_IP_VCN_ENC]	=	1,
	[AMDGPU_HW_IP_VCN_JPEG]	=	1,
};

static int amdgpu_ctx_priority_permit(struct drm_file *filp,
				      enum drm_sched_priority priority)
{
	if (priority < 0 || priority >= DRM_SCHED_PRIORITY_COUNT)
		return -EINVAL;

	/* NORMAL and below are accessible by everyone */
	if (priority <= DRM_SCHED_PRIORITY_NORMAL)
		return 0;

	if (capable(CAP_SYS_NICE))
		return 0;

	if (drm_is_current_master(filp))
		return 0;

	return -EACCES;
}

static enum gfx_pipe_priority amdgpu_ctx_sched_prio_to_compute_prio(enum drm_sched_priority prio)
{
	switch (prio) {
	case DRM_SCHED_PRIORITY_HIGH:
	case DRM_SCHED_PRIORITY_KERNEL:
		return AMDGPU_GFX_PIPE_PRIO_HIGH;
	default:
		return AMDGPU_GFX_PIPE_PRIO_NORMAL;
	}
}

static unsigned int amdgpu_ctx_prio_sched_to_hw(struct amdgpu_device *adev,
						 enum drm_sched_priority prio,
						 u32 hw_ip)
{
	unsigned int hw_prio;

	hw_prio = (hw_ip == AMDGPU_HW_IP_COMPUTE) ?
			amdgpu_ctx_sched_prio_to_compute_prio(prio) :
			AMDGPU_RING_PRIO_DEFAULT;
	hw_ip = array_index_nospec(hw_ip, AMDGPU_HW_IP_NUM);
	if (adev->gpu_sched[hw_ip][hw_prio].num_scheds == 0)
		hw_prio = AMDGPU_RING_PRIO_DEFAULT;

	return hw_prio;
}

static int amdgpu_ctx_init_entity(struct amdgpu_ctx *ctx, u32 hw_ip,
				   const u32 ring)
{
	struct amdgpu_device *adev = ctx->adev;
	struct amdgpu_ctx_entity *entity;
	struct drm_gpu_scheduler **scheds = NULL, *sched = NULL;
	unsigned num_scheds = 0;
	unsigned int hw_prio;
	enum drm_sched_priority priority;
	int r;

	entity = kzalloc(struct_size(entity, fences, amdgpu_sched_jobs),
			 GFP_KERNEL);
	if (!entity)
		return  -ENOMEM;

	entity->sequence = 1;
	priority = (ctx->override_priority == DRM_SCHED_PRIORITY_UNSET) ?
				ctx->init_priority : ctx->override_priority;
	hw_prio = amdgpu_ctx_prio_sched_to_hw(adev, priority, hw_ip);

	hw_ip = array_index_nospec(hw_ip, AMDGPU_HW_IP_NUM);
	scheds = adev->gpu_sched[hw_ip][hw_prio].sched;
	num_scheds = adev->gpu_sched[hw_ip][hw_prio].num_scheds;

	/* disable load balance if the hw engine retains context among dependent jobs */
	if (hw_ip == AMDGPU_HW_IP_VCN_ENC ||
	    hw_ip == AMDGPU_HW_IP_VCN_DEC ||
	    hw_ip == AMDGPU_HW_IP_UVD_ENC ||
	    hw_ip == AMDGPU_HW_IP_UVD) {
		sched = drm_sched_pick_best(scheds, num_scheds);
		scheds = &sched;
		num_scheds = 1;
	}

	r = drm_sched_entity_init(&entity->entity, priority, scheds, num_scheds,
				  &ctx->guilty);
	if (r)
		goto error_free_entity;

	ctx->entities[hw_ip][ring] = entity;
	return 0;

error_free_entity:
	kfree(entity);

	return r;
}

static int amdgpu_ctx_init(struct amdgpu_device *adev,
			   enum drm_sched_priority priority,
			   struct drm_file *filp,
			   struct amdgpu_ctx *ctx)
{
	int r;

	r = amdgpu_ctx_priority_permit(filp, priority);
	if (r)
		return r;

	memset(ctx, 0, sizeof(*ctx));

	ctx->adev = adev;

	kref_init(&ctx->refcount);
	spin_lock_init(&ctx->ring_lock);
	mutex_init(&ctx->lock);

	ctx->reset_counter = atomic_read(&adev->gpu_reset_counter);
	ctx->reset_counter_query = ctx->reset_counter;
	ctx->vram_lost_counter = atomic_read(&adev->vram_lost_counter);
	ctx->init_priority = priority;
	ctx->override_priority = DRM_SCHED_PRIORITY_UNSET;

	return 0;
}

static void amdgpu_ctx_fini_entity(struct amdgpu_ctx_entity *entity)
{

	int i;

	if (!entity)
		return;

	for (i = 0; i < amdgpu_sched_jobs; ++i)
		dma_fence_put(entity->fences[i]);

	kfree(entity);
}

static void amdgpu_ctx_fini(struct kref *ref)
{
	struct amdgpu_ctx *ctx = container_of(ref, struct amdgpu_ctx, refcount);
	struct amdgpu_device *adev = ctx->adev;
	unsigned i, j;

	if (!adev)
		return;

	for (i = 0; i < AMDGPU_HW_IP_NUM; ++i) {
		for (j = 0; j < AMDGPU_MAX_ENTITY_NUM; ++j) {
			amdgpu_ctx_fini_entity(ctx->entities[i][j]);
			ctx->entities[i][j] = NULL;
		}
	}

	mutex_destroy(&ctx->lock);
	kfree(ctx);
}

int amdgpu_ctx_get_entity(struct amdgpu_ctx *ctx, u32 hw_ip, u32 instance,
			  u32 ring, struct drm_sched_entity **entity)
{
	int r;

	if (hw_ip >= AMDGPU_HW_IP_NUM) {
		DRM_ERROR("unknown HW IP type: %d\n", hw_ip);
		return -EINVAL;
	}

	/* Right now all IPs have only one instance - multiple rings. */
	if (instance != 0) {
		DRM_DEBUG("invalid ip instance: %d\n", instance);
		return -EINVAL;
	}

	if (ring >= amdgpu_ctx_num_entities[hw_ip]) {
		DRM_DEBUG("invalid ring: %d %d\n", hw_ip, ring);
		return -EINVAL;
	}

	if (ctx->entities[hw_ip][ring] == NULL) {
		r = amdgpu_ctx_init_entity(ctx, hw_ip, ring);
		if (r)
			return r;
	}

	*entity = &ctx->entities[hw_ip][ring]->entity;
	return 0;
}

static int amdgpu_ctx_alloc(struct amdgpu_device *adev,
			    struct amdgpu_fpriv *fpriv,
			    struct drm_file *filp,
			    enum drm_sched_priority priority,
			    uint32_t *id)
{
	struct amdgpu_ctx_mgr *mgr = &fpriv->ctx_mgr;
	struct amdgpu_ctx *ctx;
	int r;

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mutex_lock(&mgr->lock);
	r = idr_alloc(&mgr->ctx_handles, ctx, 1, AMDGPU_VM_MAX_NUM_CTX, GFP_KERNEL);
	if (r < 0) {
		mutex_unlock(&mgr->lock);
		kfree(ctx);
		return r;
	}

	*id = (uint32_t)r;
	r = amdgpu_ctx_init(adev, priority, filp, ctx);
	if (r) {
		idr_remove(&mgr->ctx_handles, *id);
		*id = 0;
		kfree(ctx);
	}
	mutex_unlock(&mgr->lock);
	return r;
}

static void amdgpu_ctx_do_release(struct kref *ref)
{
	struct amdgpu_ctx *ctx;
	u32 i, j;

	ctx = container_of(ref, struct amdgpu_ctx, refcount);
	for (i = 0; i < AMDGPU_HW_IP_NUM; ++i) {
		for (j = 0; j < amdgpu_ctx_num_entities[i]; ++j) {
			if (!ctx->entities[i][j])
				continue;

			drm_sched_entity_destroy(&ctx->entities[i][j]->entity);
		}
	}

	amdgpu_ctx_fini(ref);
}

static int amdgpu_ctx_free(struct amdgpu_fpriv *fpriv, uint32_t id)
{
	struct amdgpu_ctx_mgr *mgr = &fpriv->ctx_mgr;
	struct amdgpu_ctx *ctx;

	mutex_lock(&mgr->lock);
	ctx = idr_remove(&mgr->ctx_handles, id);
	if (ctx)
		kref_put(&ctx->refcount, amdgpu_ctx_do_release);
	mutex_unlock(&mgr->lock);
	return ctx ? 0 : -EINVAL;
}

static int amdgpu_ctx_query(struct amdgpu_device *adev,
			    struct amdgpu_fpriv *fpriv, uint32_t id,
			    union drm_amdgpu_ctx_out *out)
{
	struct amdgpu_ctx *ctx;
	struct amdgpu_ctx_mgr *mgr;
	unsigned reset_counter;

	if (!fpriv)
		return -EINVAL;

	mgr = &fpriv->ctx_mgr;
	mutex_lock(&mgr->lock);
	ctx = idr_find(&mgr->ctx_handles, id);
	if (!ctx) {
		mutex_unlock(&mgr->lock);
		return -EINVAL;
	}

	/* TODO: these two are always zero */
	out->state.flags = 0x0;
	out->state.hangs = 0x0;

	/* determine if a GPU reset has occured since the last call */
	reset_counter = atomic_read(&adev->gpu_reset_counter);
	/* TODO: this should ideally return NO, GUILTY, or INNOCENT. */
	if (ctx->reset_counter_query == reset_counter)
		out->state.reset_status = AMDGPU_CTX_NO_RESET;
	else
		out->state.reset_status = AMDGPU_CTX_UNKNOWN_RESET;
	ctx->reset_counter_query = reset_counter;

	mutex_unlock(&mgr->lock);
	return 0;
}

static int amdgpu_ctx_query2(struct amdgpu_device *adev,
	struct amdgpu_fpriv *fpriv, uint32_t id,
	union drm_amdgpu_ctx_out *out)
{
	struct amdgpu_ctx *ctx;
	struct amdgpu_ctx_mgr *mgr;
	unsigned long ras_counter;

	if (!fpriv)
		return -EINVAL;

	mgr = &fpriv->ctx_mgr;
	mutex_lock(&mgr->lock);
	ctx = idr_find(&mgr->ctx_handles, id);
	if (!ctx) {
		mutex_unlock(&mgr->lock);
		return -EINVAL;
	}

	out->state.flags = 0x0;
	out->state.hangs = 0x0;

	if (ctx->reset_counter != atomic_read(&adev->gpu_reset_counter))
		out->state.flags |= AMDGPU_CTX_QUERY2_FLAGS_RESET;

	if (ctx->vram_lost_counter != atomic_read(&adev->vram_lost_counter))
		out->state.flags |= AMDGPU_CTX_QUERY2_FLAGS_VRAMLOST;

	if (atomic_read(&ctx->guilty))
		out->state.flags |= AMDGPU_CTX_QUERY2_FLAGS_GUILTY;

	/*query ue count*/
	ras_counter = amdgpu_ras_query_error_count(adev, false);
	/*ras counter is monotonic increasing*/
	if (ras_counter != ctx->ras_counter_ue) {
		out->state.flags |= AMDGPU_CTX_QUERY2_FLAGS_RAS_UE;
		ctx->ras_counter_ue = ras_counter;
	}

	/*query ce count*/
	ras_counter = amdgpu_ras_query_error_count(adev, true);
	if (ras_counter != ctx->ras_counter_ce) {
		out->state.flags |= AMDGPU_CTX_QUERY2_FLAGS_RAS_CE;
		ctx->ras_counter_ce = ras_counter;
	}

	mutex_unlock(&mgr->lock);
	return 0;
}

int amdgpu_ctx_ioctl(struct drm_device *dev, void *data,
		     struct drm_file *filp)
{
	int r;
	uint32_t id;
	enum drm_sched_priority priority;

	union drm_amdgpu_ctx *args = data;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_fpriv *fpriv = filp->driver_priv;

	id = args->in.ctx_id;
	r = amdgpu_to_sched_priority(args->in.priority, &priority);

	/* For backwards compatibility reasons, we need to accept
	 * ioctls with garbage in the priority field */
	if (r == -EINVAL)
		priority = DRM_SCHED_PRIORITY_NORMAL;

	switch (args->in.op) {
	case AMDGPU_CTX_OP_ALLOC_CTX:
		r = amdgpu_ctx_alloc(adev, fpriv, filp, priority, &id);
		args->out.alloc.ctx_id = id;
		break;
	case AMDGPU_CTX_OP_FREE_CTX:
		r = amdgpu_ctx_free(fpriv, id);
		break;
	case AMDGPU_CTX_OP_QUERY_STATE:
		r = amdgpu_ctx_query(adev, fpriv, id, &args->out);
		break;
	case AMDGPU_CTX_OP_QUERY_STATE2:
		r = amdgpu_ctx_query2(adev, fpriv, id, &args->out);
		break;
	default:
		return -EINVAL;
	}

	return r;
}

struct amdgpu_ctx *amdgpu_ctx_get(struct amdgpu_fpriv *fpriv, uint32_t id)
{
	struct amdgpu_ctx *ctx;
	struct amdgpu_ctx_mgr *mgr;

	if (!fpriv)
		return NULL;

	mgr = &fpriv->ctx_mgr;

	mutex_lock(&mgr->lock);
	ctx = idr_find(&mgr->ctx_handles, id);
	if (ctx)
		kref_get(&ctx->refcount);
	mutex_unlock(&mgr->lock);
	return ctx;
}

int amdgpu_ctx_put(struct amdgpu_ctx *ctx)
{
	if (ctx == NULL)
		return -EINVAL;

	kref_put(&ctx->refcount, amdgpu_ctx_do_release);
	return 0;
}

void amdgpu_ctx_add_fence(struct amdgpu_ctx *ctx,
			  struct drm_sched_entity *entity,
			  struct dma_fence *fence, uint64_t* handle)
{
	struct amdgpu_ctx_entity *centity = to_amdgpu_ctx_entity(entity);
	uint64_t seq = centity->sequence;
	struct dma_fence *other = NULL;
	unsigned idx = 0;

	idx = seq & (amdgpu_sched_jobs - 1);
	other = centity->fences[idx];
	if (other)
		BUG_ON(!dma_fence_is_signaled(other));

	dma_fence_get(fence);

	spin_lock(&ctx->ring_lock);
	centity->fences[idx] = fence;
	centity->sequence++;
	spin_unlock(&ctx->ring_lock);

	dma_fence_put(other);
	if (handle)
		*handle = seq;
}

struct dma_fence *amdgpu_ctx_get_fence(struct amdgpu_ctx *ctx,
				       struct drm_sched_entity *entity,
				       uint64_t seq)
{
	struct amdgpu_ctx_entity *centity = to_amdgpu_ctx_entity(entity);
	struct dma_fence *fence;

	spin_lock(&ctx->ring_lock);

	if (seq == ~0ull)
		seq = centity->sequence - 1;

	if (seq >= centity->sequence) {
		spin_unlock(&ctx->ring_lock);
		return ERR_PTR(-EINVAL);
	}


	if (seq + amdgpu_sched_jobs < centity->sequence) {
		spin_unlock(&ctx->ring_lock);
		return NULL;
	}

	fence = dma_fence_get(centity->fences[seq & (amdgpu_sched_jobs - 1)]);
	spin_unlock(&ctx->ring_lock);

	return fence;
}

static void amdgpu_ctx_set_entity_priority(struct amdgpu_ctx *ctx,
					    struct amdgpu_ctx_entity *aentity,
					    int hw_ip,
					    enum drm_sched_priority priority)
{
	struct amdgpu_device *adev = ctx->adev;
	unsigned int hw_prio;
	struct drm_gpu_scheduler **scheds = NULL;
	unsigned num_scheds;

	/* set sw priority */
	drm_sched_entity_set_priority(&aentity->entity, priority);

	/* set hw priority */
	if (hw_ip == AMDGPU_HW_IP_COMPUTE) {
		hw_prio = amdgpu_ctx_prio_sched_to_hw(adev, priority,
						      AMDGPU_HW_IP_COMPUTE);
		hw_prio = array_index_nospec(hw_prio, AMDGPU_RING_PRIO_MAX);
		scheds = adev->gpu_sched[hw_ip][hw_prio].sched;
		num_scheds = adev->gpu_sched[hw_ip][hw_prio].num_scheds;
		drm_sched_entity_modify_sched(&aentity->entity, scheds,
					      num_scheds);
	}
}

void amdgpu_ctx_priority_override(struct amdgpu_ctx *ctx,
				  enum drm_sched_priority priority)
{
	enum drm_sched_priority ctx_prio;
	unsigned i, j;

	ctx->override_priority = priority;

	ctx_prio = (ctx->override_priority == DRM_SCHED_PRIORITY_UNSET) ?
			ctx->init_priority : ctx->override_priority;
	for (i = 0; i < AMDGPU_HW_IP_NUM; ++i) {
		for (j = 0; j < amdgpu_ctx_num_entities[i]; ++j) {
			if (!ctx->entities[i][j])
				continue;

			amdgpu_ctx_set_entity_priority(ctx, ctx->entities[i][j],
						       i, ctx_prio);
		}
	}
}

int amdgpu_ctx_wait_prev_fence(struct amdgpu_ctx *ctx,
			       struct drm_sched_entity *entity)
{
	struct amdgpu_ctx_entity *centity = to_amdgpu_ctx_entity(entity);
	struct dma_fence *other;
	unsigned idx;
	long r;

	spin_lock(&ctx->ring_lock);
	idx = centity->sequence & (amdgpu_sched_jobs - 1);
	other = dma_fence_get(centity->fences[idx]);
	spin_unlock(&ctx->ring_lock);

	if (!other)
		return 0;

	r = dma_fence_wait(other, true);
	if (r < 0 && r != -ERESTARTSYS)
		DRM_ERROR("Error (%ld) waiting for fence!\n", r);

	dma_fence_put(other);
	return r;
}

void amdgpu_ctx_mgr_init(struct amdgpu_ctx_mgr *mgr)
{
	mutex_init(&mgr->lock);
	idr_init(&mgr->ctx_handles);
}

long amdgpu_ctx_mgr_entity_flush(struct amdgpu_ctx_mgr *mgr, long timeout)
{
	struct amdgpu_ctx *ctx;
	struct idr *idp;
	uint32_t id, i, j;

	idp = &mgr->ctx_handles;

	mutex_lock(&mgr->lock);
	idr_for_each_entry(idp, ctx, id) {
		for (i = 0; i < AMDGPU_HW_IP_NUM; ++i) {
			for (j = 0; j < amdgpu_ctx_num_entities[i]; ++j) {
				struct drm_sched_entity *entity;

				if (!ctx->entities[i][j])
					continue;

				entity = &ctx->entities[i][j]->entity;
				timeout = drm_sched_entity_flush(entity, timeout);
			}
		}
	}
	mutex_unlock(&mgr->lock);
	return timeout;
}

void amdgpu_ctx_mgr_entity_fini(struct amdgpu_ctx_mgr *mgr)
{
	struct amdgpu_ctx *ctx;
	struct idr *idp;
	uint32_t id, i, j;

	idp = &mgr->ctx_handles;

	idr_for_each_entry(idp, ctx, id) {
		if (kref_read(&ctx->refcount) != 1) {
			DRM_ERROR("ctx %p is still alive\n", ctx);
			continue;
		}

		for (i = 0; i < AMDGPU_HW_IP_NUM; ++i) {
			for (j = 0; j < amdgpu_ctx_num_entities[i]; ++j) {
				struct drm_sched_entity *entity;

				if (!ctx->entities[i][j])
					continue;

				entity = &ctx->entities[i][j]->entity;
				drm_sched_entity_fini(entity);
			}
		}
	}
}

void amdgpu_ctx_mgr_fini(struct amdgpu_ctx_mgr *mgr)
{
	struct amdgpu_ctx *ctx;
	struct idr *idp;
	uint32_t id;

	amdgpu_ctx_mgr_entity_fini(mgr);

	idp = &mgr->ctx_handles;

	idr_for_each_entry(idp, ctx, id) {
		if (kref_put(&ctx->refcount, amdgpu_ctx_fini) != 1)
			DRM_ERROR("ctx %p is still alive\n", ctx);
	}

	idr_destroy(&mgr->ctx_handles);
	mutex_destroy(&mgr->lock);
}
