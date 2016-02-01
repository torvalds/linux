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
	(*job)->free_job = NULL;

	return 0;
}

void amdgpu_job_free(struct amdgpu_job *job)
{
	unsigned i;

	for (i = 0; i < job->num_ibs; ++i)
		amdgpu_ib_free(job->adev, &job->ibs[i]);

	amdgpu_bo_unref(&job->uf.bo);
	/* TODO: Free the job structure here as well */
}

static struct fence *amdgpu_sched_dependency(struct amd_sched_job *sched_job)
{
	struct amdgpu_job *job = to_amdgpu_job(sched_job);
	struct amdgpu_sync *sync = &job->ibs->sync;
	struct amdgpu_vm *vm = job->ibs->vm;

	struct fence *fence = amdgpu_sync_get_fence(sync);

	if (fence == NULL && vm && !job->ibs->grabbed_vmid) {
		struct amdgpu_ring *ring = job->ring;
		int r;

		r = amdgpu_vm_grab_id(vm, ring, sync,
				      &job->base.s_fence->base);
		if (r)
			DRM_ERROR("Error getting VM ID (%d)\n", r);
		else
			job->ibs->grabbed_vmid = true;

		fence = amdgpu_sync_get_fence(sync);
	}

	return fence;
}

static struct fence *amdgpu_sched_run_job(struct amd_sched_job *sched_job)
{
	struct fence *fence = NULL;
	struct amdgpu_job *job;
	int r;

	if (!sched_job) {
		DRM_ERROR("job is null\n");
		return NULL;
	}
	job = to_amdgpu_job(sched_job);
	trace_amdgpu_sched_run_job(job);
	r = amdgpu_ib_schedule(job->ring, job->num_ibs, job->ibs,
			       job->owner, &fence);
	if (r) {
		DRM_ERROR("Error scheduling IBs (%d)\n", r);
		goto err;
	}

err:
	if (job->free_job)
		job->free_job(job);

	kfree(job);
	return fence;
}

struct amd_sched_backend_ops amdgpu_sched_ops = {
	.dependency = amdgpu_sched_dependency,
	.run_job = amdgpu_sched_run_job,
};

int amdgpu_sched_ib_submit_kernel_helper(struct amdgpu_device *adev,
					 struct amdgpu_ring *ring,
					 struct amdgpu_ib *ibs,
					 unsigned num_ibs,
					 int (*free_job)(struct amdgpu_job *),
					 void *owner,
					 struct fence **f)
{
	struct amdgpu_job *job =
		kzalloc(sizeof(struct amdgpu_job), GFP_KERNEL);
	if (!job)
		return -ENOMEM;
	job->base.sched = &ring->sched;
	job->base.s_entity = &adev->kernel_ctx.rings[ring->idx].entity;
	job->base.s_fence = amd_sched_fence_create(job->base.s_entity, owner);
	if (!job->base.s_fence) {
		kfree(job);
		return -ENOMEM;
	}
	*f = fence_get(&job->base.s_fence->base);

	job->adev = adev;
	job->ring = ring;
	job->ibs = ibs;
	job->num_ibs = num_ibs;
	job->owner = owner;
	job->free_job = free_job;
	amd_sched_entity_push_job(&job->base);

	return 0;
}
