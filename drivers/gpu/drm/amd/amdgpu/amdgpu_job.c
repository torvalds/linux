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
 *
 */
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_trace.h"

static void amdgpu_job_timedout(struct amd_sched_job *s_job)
{
	struct amdgpu_job *job = container_of(s_job, struct amdgpu_job, base);

	DRM_ERROR("ring %s timeout, last signaled seq=%u, last emitted seq=%u\n",
		  job->base.sched->name,
		  atomic_read(&job->ring->fence_drv.last_seq),
		  job->ring->fence_drv.sync_seq);

	amdgpu_gpu_recover(job->adev, job);
}

int amdgpu_job_alloc(struct amdgpu_device *adev, unsigned num_ibs,
		     struct amdgpu_job **job, struct amdgpu_vm *vm)
{
	size_t size = sizeof(struct amdgpu_job);

	if (num_ibs == 0)
		return -EINVAL;

	size += sizeof(struct amdgpu_ib) * num_ibs;

	*job = kzalloc(size, GFP_KERNEL);
	if (!*job)
		return -ENOMEM;

	(*job)->adev = adev;
	(*job)->vm = vm;
	(*job)->ibs = (void *)&(*job)[1];
	(*job)->num_ibs = num_ibs;

	amdgpu_sync_create(&(*job)->sync);
	amdgpu_sync_create(&(*job)->dep_sync);
	amdgpu_sync_create(&(*job)->sched_sync);
	(*job)->vram_lost_counter = atomic_read(&adev->vram_lost_counter);

	return 0;
}

int amdgpu_job_alloc_with_ib(struct amdgpu_device *adev, unsigned size,
			     struct amdgpu_job **job)
{
	int r;

	r = amdgpu_job_alloc(adev, 1, job, NULL);
	if (r)
		return r;

	r = amdgpu_ib_get(adev, NULL, size, &(*job)->ibs[0]);
	if (r)
		kfree(*job);
	else
		(*job)->vm_pd_addr = adev->gart.table_addr;

	return r;
}

void amdgpu_job_free_resources(struct amdgpu_job *job)
{
	struct dma_fence *f;
	unsigned i;

	/* use sched fence if available */
	f = job->base.s_fence ? &job->base.s_fence->finished : job->fence;

	for (i = 0; i < job->num_ibs; ++i)
		amdgpu_ib_free(job->adev, &job->ibs[i], f);
}

static void amdgpu_job_free_cb(struct amd_sched_job *s_job)
{
	struct amdgpu_job *job = container_of(s_job, struct amdgpu_job, base);

	amdgpu_ring_priority_put(job->ring, s_job->s_priority);
	dma_fence_put(job->fence);
	amdgpu_sync_free(&job->sync);
	amdgpu_sync_free(&job->dep_sync);
	amdgpu_sync_free(&job->sched_sync);
	kfree(job);
}

void amdgpu_job_free(struct amdgpu_job *job)
{
	amdgpu_job_free_resources(job);

	dma_fence_put(job->fence);
	amdgpu_sync_free(&job->sync);
	amdgpu_sync_free(&job->dep_sync);
	amdgpu_sync_free(&job->sched_sync);
	kfree(job);
}

int amdgpu_job_submit(struct amdgpu_job *job, struct amdgpu_ring *ring,
		      struct amd_sched_entity *entity, void *owner,
		      struct dma_fence **f)
{
	int r;
	job->ring = ring;

	if (!f)
		return -EINVAL;

	r = amd_sched_job_init(&job->base, &ring->sched, entity, owner);
	if (r)
		return r;

	job->owner = owner;
	job->fence_ctx = entity->fence_context;
	*f = dma_fence_get(&job->base.s_fence->finished);
	amdgpu_job_free_resources(job);
	amdgpu_ring_priority_get(job->ring, job->base.s_priority);
	amd_sched_entity_push_job(&job->base, entity);

	return 0;
}

static struct dma_fence *amdgpu_job_dependency(struct amd_sched_job *sched_job,
					       struct amd_sched_entity *s_entity)
{
	struct amdgpu_job *job = to_amdgpu_job(sched_job);
	struct amdgpu_vm *vm = job->vm;

	struct dma_fence *fence = amdgpu_sync_get_fence(&job->dep_sync);
	int r;

	if (amd_sched_dependency_optimized(fence, s_entity)) {
		r = amdgpu_sync_fence(job->adev, &job->sched_sync, fence);
		if (r)
			DRM_ERROR("Error adding fence to sync (%d)\n", r);
	}
	if (!fence)
		fence = amdgpu_sync_get_fence(&job->sync);
	while (fence == NULL && vm && !job->vm_id) {
		struct amdgpu_ring *ring = job->ring;

		r = amdgpu_vm_grab_id(vm, ring, &job->sync,
				      &job->base.s_fence->finished,
				      job);
		if (r)
			DRM_ERROR("Error getting VM ID (%d)\n", r);

		fence = amdgpu_sync_get_fence(&job->sync);
	}

	return fence;
}

static struct dma_fence *amdgpu_job_run(struct amd_sched_job *sched_job)
{
	struct dma_fence *fence = NULL, *finished;
	struct amdgpu_device *adev;
	struct amdgpu_job *job;
	int r;

	if (!sched_job) {
		DRM_ERROR("job is null\n");
		return NULL;
	}
	job = to_amdgpu_job(sched_job);
	finished = &job->base.s_fence->finished;
	adev = job->adev;

	BUG_ON(amdgpu_sync_peek_fence(&job->sync, NULL));

	trace_amdgpu_sched_run_job(job);

	if (job->vram_lost_counter != atomic_read(&adev->vram_lost_counter))
		dma_fence_set_error(finished, -ECANCELED);/* skip IB as well if VRAM lost */

	if (finished->error < 0) {
		DRM_INFO("Skip scheduling IBs!\n");
	} else {
		r = amdgpu_ib_schedule(job->ring, job->num_ibs, job->ibs, job,
				       &fence);
		if (r)
			DRM_ERROR("Error scheduling IBs (%d)\n", r);
	}
	/* if gpu reset, hw fence will be replaced here */
	dma_fence_put(job->fence);
	job->fence = dma_fence_get(fence);

	amdgpu_job_free_resources(job);
	return fence;
}

const struct amd_sched_backend_ops amdgpu_sched_ops = {
	.dependency = amdgpu_job_dependency,
	.run_job = amdgpu_job_run,
	.timedout_job = amdgpu_job_timedout,
	.free_job = amdgpu_job_free_cb
};
