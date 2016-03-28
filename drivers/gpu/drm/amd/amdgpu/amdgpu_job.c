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

int amdgpu_job_alloc(struct amdgpu_device *adev, unsigned num_ibs,
		     struct amdgpu_job **job)
{
	size_t size = sizeof(struct amdgpu_job);

	if (num_ibs == 0)
		return -EINVAL;

	size += sizeof(struct amdgpu_ib) * num_ibs;

	*job = kzalloc(size, GFP_KERNEL);
	if (!*job)
		return -ENOMEM;

	(*job)->adev = adev;
	(*job)->ibs = (void *)&(*job)[1];
	(*job)->num_ibs = num_ibs;

	amdgpu_sync_create(&(*job)->sync);

	return 0;
}

int amdgpu_job_alloc_with_ib(struct amdgpu_device *adev, unsigned size,
			     struct amdgpu_job **job)
{
	int r;

	r = amdgpu_job_alloc(adev, 1, job);
	if (r)
		return r;

	r = amdgpu_ib_get(adev, NULL, size, &(*job)->ibs[0]);
	if (r)
		kfree(*job);

	return r;
}

void amdgpu_job_free(struct amdgpu_job *job)
{
	unsigned i;
	struct fence *f;
	/* use sched fence if available */
	f = (job->base.s_fence)? &job->base.s_fence->base : job->fence;

	for (i = 0; i < job->num_ibs; ++i)
		amdgpu_sa_bo_free(job->adev, &job->ibs[i].sa_bo, f);
	fence_put(job->fence);

	amdgpu_bo_unref(&job->uf.bo);
	amdgpu_sync_free(&job->sync);
	kfree(job);
}

int amdgpu_job_submit(struct amdgpu_job *job, struct amdgpu_ring *ring,
		      struct amd_sched_entity *entity, void *owner,
		      struct fence **f)
{
	job->ring = ring;
	job->base.sched = &ring->sched;
	job->base.s_entity = entity;
	job->base.s_fence = amd_sched_fence_create(job->base.s_entity, owner);
	if (!job->base.s_fence)
		return -ENOMEM;

	*f = fence_get(&job->base.s_fence->base);

	job->owner = owner;
	amd_sched_entity_push_job(&job->base);

	return 0;
}

static struct fence *amdgpu_job_dependency(struct amd_sched_job *sched_job)
{
	struct amdgpu_job *job = to_amdgpu_job(sched_job);
	struct amdgpu_vm *vm = job->ibs->vm;

	struct fence *fence = amdgpu_sync_get_fence(&job->sync);

	if (fence == NULL && vm && !job->ibs->vm_id) {
		struct amdgpu_ring *ring = job->ring;
		unsigned i, vm_id;
		uint64_t vm_pd_addr;
		int r;

		r = amdgpu_vm_grab_id(vm, ring, &job->sync,
				      &job->base.s_fence->base,
				      &vm_id, &vm_pd_addr);
		if (r)
			DRM_ERROR("Error getting VM ID (%d)\n", r);
		else {
			for (i = 0; i < job->num_ibs; ++i) {
				job->ibs[i].vm_id = vm_id;
				job->ibs[i].vm_pd_addr = vm_pd_addr;
			}
		}

		fence = amdgpu_sync_get_fence(&job->sync);
	}

	return fence;
}

static struct fence *amdgpu_job_run(struct amd_sched_job *sched_job)
{
	struct fence *fence = NULL;
	struct amdgpu_job *job;
	int r;

	if (!sched_job) {
		DRM_ERROR("job is null\n");
		return NULL;
	}
	job = to_amdgpu_job(sched_job);

	r = amdgpu_sync_wait(&job->sync);
	if (r) {
		DRM_ERROR("failed to sync wait (%d)\n", r);
		return NULL;
	}

	trace_amdgpu_sched_run_job(job);
	r = amdgpu_ib_schedule(job->ring, job->num_ibs, job->ibs,
			       job->sync.last_vm_update, &fence);
	if (r) {
		DRM_ERROR("Error scheduling IBs (%d)\n", r);
		goto err;
	}

err:
	job->fence = fence;
	amdgpu_job_free(job);
	return fence;
}

struct amd_sched_backend_ops amdgpu_sched_ops = {
	.dependency = amdgpu_job_dependency,
	.run_job = amdgpu_job_run,
};
