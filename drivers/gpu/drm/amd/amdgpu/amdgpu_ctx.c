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

static int amdgput_ctx_total_num_entities(void)
{
	unsigned i, num_entities = 0;

	for (i = 0; i < AMDGPU_HW_IP_NUM; ++i)
		num_entities += amdgpu_ctx_num_entities[i];

	return num_entities;
}

static int amdgpu_ctx_priority_permit(struct drm_file *filp,
				      enum drm_sched_priority priority)
{
	/* NORMAL and below are accessible by everyone */
	if (priority <= DRM_SCHED_PRIORITY_NORMAL)
		return 0;

	if (capable(CAP_SYS_NICE))
		return 0;

	if (drm_is_current_master(filp))
		return 0;

	return -EACCES;
}

static int amdgpu_ctx_init(struct amdgpu_device *adev,
			   enum drm_sched_priority priority,
			   struct drm_file *filp,
			   struct amdgpu_ctx *ctx)
{
	unsigned num_entities = amdgput_ctx_total_num_entities();
	unsigned i, j, k;
	int r;

	if (priority < 0 || priority >= DRM_SCHED_PRIORITY_MAX)
		return -EINVAL;

	r = amdgpu_ctx_priority_permit(filp, priority);
	if (r)
		return r;

	memset(ctx, 0, sizeof(*ctx));
	ctx->adev = adev;

	ctx->fences = kcalloc(amdgpu_sched_jobs * num_entities,
			      sizeof(struct dma_fence*), GFP_KERNEL);
	if (!ctx->fences)
		return -ENOMEM;

	ctx->entities[0] = kcalloc(num_entities,
				   sizeof(struct amdgpu_ctx_entity),
				   GFP_KERNEL);
	if (!ctx->entities[0]) {
		r = -ENOMEM;
		goto error_free_fences;
	}

	for (i = 0; i < num_entities; ++i) {
		struct amdgpu_ctx_entity *entity = &ctx->entities[0][i];

		entity->sequence = 1;
		entity->fences = &ctx->fences[amdgpu_sched_jobs * i];
	}
	for (i = 1; i < AMDGPU_HW_IP_NUM; ++i)
		ctx->entities[i] = ctx->entities[i - 1] +
			amdgpu_ctx_num_entities[i - 1];

	kref_init(&ctx->refcount);
	spin_lock_init(&ctx->ring_lock);
	mutex_init(&ctx->lock);

	ctx->reset_counter = atomic_read(&adev->gpu_reset_counter);
	ctx->reset_counter_query = ctx->reset_counter;
	ctx->vram_lost_counter = atomic_read(&adev->vram_lost_counter);
	ctx->init_priority = priority;
	ctx->override_priority = DRM_SCHED_PRIORITY_UNSET;

	for (i = 0; i < AMDGPU_HW_IP_NUM; ++i) {
		struct amdgpu_ring *rings[AMDGPU_MAX_RINGS];
		struct drm_sched_rq *rqs[AMDGPU_MAX_RINGS];
		unsigned num_rings = 0;
		unsigned num_rqs = 0;

		switch (i) {
		case AMDGPU_HW_IP_GFX:
			rings[0] = &adev->gfx.gfx_ring[0];
			num_rings = 1;
			break;
		case AMDGPU_HW_IP_COMPUTE:
			for (j = 0; j < adev->gfx.num_compute_rings; ++j)
				rings[j] = &adev->gfx.compute_ring[j];
			num_rings = adev->gfx.num_compute_rings;
			break;
		case AMDGPU_HW_IP_DMA:
			for (j = 0; j < adev->sdma.num_instances; ++j)
				rings[j] = &adev->sdma.instance[j].ring;
			num_rings = adev->sdma.num_instances;
			break;
		case AMDGPU_HW_IP_UVD:
			rings[0] = &adev->uvd.inst[0].ring;
			num_rings = 1;
			break;
		case AMDGPU_HW_IP_VCE:
			rings[0] = &adev->vce.ring[0];
			num_rings = 1;
			break;
		case AMDGPU_HW_IP_UVD_ENC:
			rings[0] = &adev->uvd.inst[0].ring_enc[0];
			num_rings = 1;
			break;
		case AMDGPU_HW_IP_VCN_DEC:
			for (j = 0; j < adev->vcn.num_vcn_inst; ++j) {
				if (adev->vcn.harvest_config & (1 << j))
					continue;
				rings[num_rings++] = &adev->vcn.inst[j].ring_dec;
			}
			break;
		case AMDGPU_HW_IP_VCN_ENC:
			for (j = 0; j < adev->vcn.num_vcn_inst; ++j) {
				if (adev->vcn.harvest_config & (1 << j))
					continue;
				for (k = 0; k < adev->vcn.num_enc_rings; ++k)
					rings[num_rings++] = &adev->vcn.inst[j].ring_enc[k];
			}
			break;
		case AMDGPU_HW_IP_VCN_JPEG:
			for (j = 0; j < adev->vcn.num_vcn_inst; ++j) {
				if (adev->vcn.harvest_config & (1 << j))
					continue;
				rings[num_rings++] = &adev->vcn.inst[j].ring_jpeg;
			}
			break;
		}

		for (j = 0; j < num_rings; ++j) {
			if (!rings[j]->adev)
				continue;

			rqs[num_rqs++] = &rings[j]->sched.sched_rq[priority];
		}

		for (j = 0; j < amdgpu_ctx_num_entities[i]; ++j)
			r = drm_sched_entity_init(&ctx->entities[i][j].entity,
						  rqs, num_rqs, &ctx->guilty);
		if (r)
			goto error_cleanup_entities;
	}

	return 0;

error_cleanup_entities:
	for (i = 0; i < num_entities; ++i)
		drm_sched_entity_destroy(&ctx->entities[0][i].entity);
	kfree(ctx->entities[0]);

error_free_fences:
	kfree(ctx->fences);
	ctx->fences = NULL;
	return r;
}

static void amdgpu_ctx_fini(struct kref *ref)
{
	struct amdgpu_ctx *ctx = container_of(ref, struct amdgpu_ctx, refcount);
	unsigned num_entities = amdgput_ctx_total_num_entities();
	struct amdgpu_device *adev = ctx->adev;
	unsigned i, j;

	if (!adev)
		return;

	for (i = 0; i < num_entities; ++i)
		for (j = 0; j < amdgpu_sched_jobs; ++j)
			dma_fence_put(ctx->entities[0][i].fences[j]);
	kfree(ctx->fences);
	kfree(ctx->entities[0]);

	mutex_destroy(&ctx->lock);

	kfree(ctx);
}

int amdgpu_ctx_get_entity(struct amdgpu_ctx *ctx, u32 hw_ip, u32 instance,
			  u32 ring, struct drm_sched_entity **entity)
{
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

	*entity = &ctx->entities[hw_ip][ring].entity;
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
	unsigned num_entities;
	u32 i;

	ctx = container_of(ref, struct amdgpu_ctx, refcount);

	num_entities = 0;
	for (i = 0; i < AMDGPU_HW_IP_NUM; i++)
		num_entities += amdgpu_ctx_num_entities[i];

	for (i = 0; i < num_entities; i++)
		drm_sched_entity_destroy(&ctx->entities[0][i].entity);

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
	uint32_t ras_counter;

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
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_fpriv *fpriv = filp->driver_priv;

	r = 0;
	id = args->in.ctx_id;
	priority = amdgpu_to_sched_priority(args->in.priority);

	/* For backwards compatibility reasons, we need to accept
	 * ioctls with garbage in the priority field */
	if (priority == DRM_SCHED_PRIORITY_INVALID)
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

void amdgpu_ctx_priority_override(struct amdgpu_ctx *ctx,
				  enum drm_sched_priority priority)
{
	unsigned num_entities = amdgput_ctx_total_num_entities();
	enum drm_sched_priority ctx_prio;
	unsigned i;

	ctx->override_priority = priority;

	ctx_prio = (ctx->override_priority == DRM_SCHED_PRIORITY_UNSET) ?
			ctx->init_priority : ctx->override_priority;

	for (i = 0; i < num_entities; i++) {
		struct drm_sched_entity *entity = &ctx->entities[0][i].entity;

		drm_sched_entity_set_priority(entity, ctx_prio);
	}
}

int amdgpu_ctx_wait_prev_fence(struct amdgpu_ctx *ctx,
			       struct drm_sched_entity *entity)
{
	struct amdgpu_ctx_entity *centity = to_amdgpu_ctx_entity(entity);
	unsigned idx = centity->sequence & (amdgpu_sched_jobs - 1);
	struct dma_fence *other = centity->fences[idx];

	if (other) {
		signed long r;
		r = dma_fence_wait(other, true);
		if (r < 0) {
			if (r != -ERESTARTSYS)
				DRM_ERROR("Error (%ld) waiting for fence!\n", r);

			return r;
		}
	}

	return 0;
}

void amdgpu_ctx_mgr_init(struct amdgpu_ctx_mgr *mgr)
{
	mutex_init(&mgr->lock);
	idr_init(&mgr->ctx_handles);
}

long amdgpu_ctx_mgr_entity_flush(struct amdgpu_ctx_mgr *mgr, long timeout)
{
	unsigned num_entities = amdgput_ctx_total_num_entities();
	struct amdgpu_ctx *ctx;
	struct idr *idp;
	uint32_t id, i;

	idp = &mgr->ctx_handles;

	mutex_lock(&mgr->lock);
	idr_for_each_entry(idp, ctx, id) {
		for (i = 0; i < num_entities; i++) {
			struct drm_sched_entity *entity;

			entity = &ctx->entities[0][i].entity;
			timeout = drm_sched_entity_flush(entity, timeout);
		}
	}
	mutex_unlock(&mgr->lock);
	return timeout;
}

void amdgpu_ctx_mgr_entity_fini(struct amdgpu_ctx_mgr *mgr)
{
	unsigned num_entities = amdgput_ctx_total_num_entities();
	struct amdgpu_ctx *ctx;
	struct idr *idp;
	uint32_t id, i;

	idp = &mgr->ctx_handles;

	idr_for_each_entry(idp, ctx, id) {
		if (kref_read(&ctx->refcount) != 1) {
			DRM_ERROR("ctx %p is still alive\n", ctx);
			continue;
		}

		for (i = 0; i < num_entities; i++)
			drm_sched_entity_fini(&ctx->entities[0][i].entity);
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
