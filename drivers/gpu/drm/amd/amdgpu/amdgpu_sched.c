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

static struct fence *amdgpu_sched_dependency(struct amd_sched_job *job)
{
	struct amdgpu_job *sched_job = (struct amdgpu_job *)job;
	return amdgpu_sync_get_fence(&sched_job->ibs->sync);
}

static struct fence *amdgpu_sched_run_job(struct amd_sched_job *job)
{
	struct amdgpu_job *sched_job;
	struct amdgpu_fence *fence;
	int r;

	if (!job) {
		DRM_ERROR("job is null\n");
		return NULL;
	}
	sched_job = (struct amdgpu_job *)job;
	mutex_lock(&sched_job->job_lock);
	r = amdgpu_ib_schedule(sched_job->adev,
			       sched_job->num_ibs,
			       sched_job->ibs,
			       sched_job->base.owner);
	if (r)
		goto err;
	fence = amdgpu_fence_ref(sched_job->ibs[sched_job->num_ibs - 1].fence);

	if (sched_job->free_job)
		sched_job->free_job(sched_job);

	mutex_unlock(&sched_job->job_lock);
	return &fence->base;

err:
	DRM_ERROR("Run job error\n");
	mutex_unlock(&sched_job->job_lock);
	job->sched->ops->process_job(job);
	return NULL;
}

static void amdgpu_sched_process_job(struct amd_sched_job *job)
{
	struct amdgpu_job *sched_job;

	if (!job) {
		DRM_ERROR("job is null\n");
		return;
	}
	sched_job = (struct amdgpu_job *)job;
	/* after processing job, free memory */
	fence_put(&sched_job->base.s_fence->base);
	kfree(sched_job);
}

struct amd_sched_backend_ops amdgpu_sched_ops = {
	.dependency = amdgpu_sched_dependency,
	.run_job = amdgpu_sched_run_job,
	.process_job = amdgpu_sched_process_job
};

int amdgpu_sched_ib_submit_kernel_helper(struct amdgpu_device *adev,
					 struct amdgpu_ring *ring,
					 struct amdgpu_ib *ibs,
					 unsigned num_ibs,
					 int (*free_job)(struct amdgpu_job *),
					 void *owner,
					 struct fence **f)
{
	int r = 0;
	if (amdgpu_enable_scheduler) {
		struct amdgpu_job *job =
			kzalloc(sizeof(struct amdgpu_job), GFP_KERNEL);
		if (!job)
			return -ENOMEM;
		job->base.sched = ring->scheduler;
		job->base.s_entity = &adev->kernel_ctx.rings[ring->idx].entity;
		job->adev = adev;
		job->ibs = ibs;
		job->num_ibs = num_ibs;
		job->base.owner = owner;
		mutex_init(&job->job_lock);
		job->free_job = free_job;
		mutex_lock(&job->job_lock);
		r = amd_sched_entity_push_job((struct amd_sched_job *)job);
		if (r) {
			mutex_unlock(&job->job_lock);
			kfree(job);
			return r;
		}
		*f = fence_get(&job->base.s_fence->base);
		mutex_unlock(&job->job_lock);
	} else {
		r = amdgpu_ib_schedule(adev, num_ibs, ibs, owner);
		if (r)
			return r;
		*f = fence_get(&ibs[num_ibs - 1].fence->base);
	}

	return 0;
}
