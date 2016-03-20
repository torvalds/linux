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

#include <drm/drmP.h>
#include "amdgpu.h"

int amdgpu_ctx_init(struct amdgpu_device *adev, enum amd_sched_priority pri,
		    struct amdgpu_ctx *ctx)
{
	unsigned i, j;
	int r;

	memset(ctx, 0, sizeof(*ctx));
	ctx->adev = adev;
	kref_init(&ctx->refcount);
	spin_lock_init(&ctx->ring_lock);
	ctx->fences = kzalloc(sizeof(struct fence *) * amdgpu_sched_jobs *
			 AMDGPU_MAX_RINGS, GFP_KERNEL);
	if (!ctx->fences)
		return -ENOMEM;

	for (i = 0; i < AMDGPU_MAX_RINGS; ++i) {
		ctx->rings[i].sequence = 1;
		ctx->rings[i].fences = (void *)ctx->fences + sizeof(struct fence *) *
			amdgpu_sched_jobs * i;
	}
	if (amdgpu_enable_scheduler) {
		/* create context entity for each ring */
		for (i = 0; i < adev->num_rings; i++) {
			struct amd_sched_rq *rq;
			if (pri >= AMD_SCHED_MAX_PRIORITY) {
				kfree(ctx->fences);
				return -EINVAL;
			}
			rq = &adev->rings[i]->sched.sched_rq[pri];
			r = amd_sched_entity_init(&adev->rings[i]->sched,
						  &ctx->rings[i].entity,
						  rq, amdgpu_sched_jobs);
			if (r)
				break;
		}

		if (i < adev->num_rings) {
			for (j = 0; j < i; j++)
				amd_sched_entity_fini(&adev->rings[j]->sched,
						      &ctx->rings[j].entity);
			kfree(ctx->fences);
			return r;
		}
	}
	return 0;
}

void amdgpu_ctx_fini(struct amdgpu_ctx *ctx)
{
	struct amdgpu_device *adev = ctx->adev;
	unsigned i, j;

	if (!adev)
		return;

	for (i = 0; i < AMDGPU_MAX_RINGS; ++i)
		for (j = 0; j < amdgpu_sched_jobs; ++j)
			fence_put(ctx->rings[i].fences[j]);
	kfree(ctx->fences);

	if (amdgpu_enable_scheduler) {
		for (i = 0; i < adev->num_rings; i++)
			amd_sched_entity_fini(&adev->rings[i]->sched,
					      &ctx->rings[i].entity);
	}
}

static int amdgpu_ctx_alloc(struct amdgpu_device *adev,
			    struct amdgpu_fpriv *fpriv,
			    uint32_t *id)
{
	struct amdgpu_ctx_mgr *mgr = &fpriv->ctx_mgr;
	struct amdgpu_ctx *ctx;
	int r;

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mutex_lock(&mgr->lock);
	r = idr_alloc(&mgr->ctx_handles, ctx, 1, 0, GFP_KERNEL);
	if (r < 0) {
		mutex_unlock(&mgr->lock);
		kfree(ctx);
		return r;
	}
	*id = (uint32_t)r;
	r = amdgpu_ctx_init(adev, AMD_SCHED_PRIORITY_NORMAL, ctx);
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

	ctx = container_of(ref, struct amdgpu_ctx, refcount);

	amdgpu_ctx_fini(ctx);

	kfree(ctx);
}

static int amdgpu_ctx_free(struct amdgpu_fpriv *fpriv, uint32_t id)
{
	struct amdgpu_ctx_mgr *mgr = &fpriv->ctx_mgr;
	struct amdgpu_ctx *ctx;

	mutex_lock(&mgr->lock);
	ctx = idr_find(&mgr->ctx_handles, id);
	if (ctx) {
		idr_remove(&mgr->ctx_handles, id);
		kref_put(&ctx->refcount, amdgpu_ctx_do_release);
		mutex_unlock(&mgr->lock);
		return 0;
	}
	mutex_unlock(&mgr->lock);
	return -EINVAL;
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
	if (ctx->reset_counter == reset_counter)
		out->state.reset_status = AMDGPU_CTX_NO_RESET;
	else
		out->state.reset_status = AMDGPU_CTX_UNKNOWN_RESET;
	ctx->reset_counter = reset_counter;

	mutex_unlock(&mgr->lock);
	return 0;
}

int amdgpu_ctx_ioctl(struct drm_device *dev, void *data,
		     struct drm_file *filp)
{
	int r;
	uint32_t id;

	union drm_amdgpu_ctx *args = data;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_fpriv *fpriv = filp->driver_priv;

	r = 0;
	id = args->in.ctx_id;

	switch (args->in.op) {
		case AMDGPU_CTX_OP_ALLOC_CTX:
			r = amdgpu_ctx_alloc(adev, fpriv, &id);
			args->out.alloc.ctx_id = id;
			break;
		case AMDGPU_CTX_OP_FREE_CTX:
			r = amdgpu_ctx_free(fpriv, id);
			break;
		case AMDGPU_CTX_OP_QUERY_STATE:
			r = amdgpu_ctx_query(adev, fpriv, id, &args->out);
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

uint64_t amdgpu_ctx_add_fence(struct amdgpu_ctx *ctx, struct amdgpu_ring *ring,
			      struct fence *fence)
{
	struct amdgpu_ctx_ring *cring = & ctx->rings[ring->idx];
	uint64_t seq = cring->sequence;
	unsigned idx = 0;
	struct fence *other = NULL;

	idx = seq & (amdgpu_sched_jobs - 1);
	other = cring->fences[idx];
	if (other) {
		signed long r;
		r = fence_wait_timeout(other, false, MAX_SCHEDULE_TIMEOUT);
		if (r < 0)
			DRM_ERROR("Error (%ld) waiting for fence!\n", r);
	}

	fence_get(fence);

	spin_lock(&ctx->ring_lock);
	cring->fences[idx] = fence;
	cring->sequence++;
	spin_unlock(&ctx->ring_lock);

	fence_put(other);

	return seq;
}

struct fence *amdgpu_ctx_get_fence(struct amdgpu_ctx *ctx,
				   struct amdgpu_ring *ring, uint64_t seq)
{
	struct amdgpu_ctx_ring *cring = & ctx->rings[ring->idx];
	struct fence *fence;

	spin_lock(&ctx->ring_lock);

	if (seq >= cring->sequence) {
		spin_unlock(&ctx->ring_lock);
		return ERR_PTR(-EINVAL);
	}


	if (seq + amdgpu_sched_jobs < cring->sequence) {
		spin_unlock(&ctx->ring_lock);
		return NULL;
	}

	fence = fence_get(cring->fences[seq & (amdgpu_sched_jobs - 1)]);
	spin_unlock(&ctx->ring_lock);

	return fence;
}

void amdgpu_ctx_mgr_init(struct amdgpu_ctx_mgr *mgr)
{
	mutex_init(&mgr->lock);
	idr_init(&mgr->ctx_handles);
}

void amdgpu_ctx_mgr_fini(struct amdgpu_ctx_mgr *mgr)
{
	struct amdgpu_ctx *ctx;
	struct idr *idp;
	uint32_t id;

	idp = &mgr->ctx_handles;

	idr_for_each_entry(idp, ctx, id) {
		if (kref_put(&ctx->refcount, amdgpu_ctx_do_release) != 1)
			DRM_ERROR("ctx %p is still alive\n", ctx);
	}

	idr_destroy(&mgr->ctx_handles);
	mutex_destroy(&mgr->lock);
}
