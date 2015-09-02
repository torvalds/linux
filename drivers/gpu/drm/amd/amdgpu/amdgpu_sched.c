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

static int amdgpu_sched_prepare_job(struct amd_gpu_scheduler *sched,
				    struct amd_sched_entity *entity,
				    struct amd_sched_job *job)
{
	int r = 0;
	struct amdgpu_cs_parser *sched_job;
	if (!job || !job->data) {
		DRM_ERROR("job is null\n");
		return -EINVAL;
	}

	sched_job = (struct amdgpu_cs_parser *)job->data;
	if (sched_job->prepare_job) {
		r = sched_job->prepare_job(sched_job);
		if (r) {
			DRM_ERROR("Prepare job error\n");
			schedule_work(&sched_job->job_work);
		}
	}
	return r;
}

static struct fence *amdgpu_sched_run_job(struct amd_gpu_scheduler *sched,
					  struct amd_sched_entity *entity,
					  struct amd_sched_job *job)
{
	int r = 0;
	struct amdgpu_cs_parser *sched_job;
	struct amdgpu_fence *fence;

	if (!job || !job->data) {
		DRM_ERROR("job is null\n");
		return NULL;
	}
	sched_job = (struct amdgpu_cs_parser *)job->data;
	mutex_lock(&sched_job->job_lock);
	r = amdgpu_ib_schedule(sched_job->adev,
			       sched_job->num_ibs,
			       sched_job->ibs,
			       sched_job->filp);
	if (r)
		goto err;
	fence = amdgpu_fence_ref(sched_job->ibs[sched_job->num_ibs - 1].fence);

	if (sched_job->run_job) {
		r = sched_job->run_job(sched_job);
		if (r)
			goto err;
	}

	mutex_unlock(&sched_job->job_lock);
	return &fence->base;

err:
	DRM_ERROR("Run job error\n");
	mutex_unlock(&sched_job->job_lock);
	schedule_work(&sched_job->job_work);
	return NULL;
}

static void amdgpu_sched_process_job(struct amd_gpu_scheduler *sched,
				     struct amd_sched_job *job)
{
	struct amdgpu_cs_parser *sched_job;

	if (!job || !job->data) {
		DRM_ERROR("job is null\n");
		return;
	}
	sched_job = (struct amdgpu_cs_parser *)job->data;
	schedule_work(&sched_job->job_work);
}

struct amd_sched_backend_ops amdgpu_sched_ops = {
	.prepare_job = amdgpu_sched_prepare_job,
	.run_job = amdgpu_sched_run_job,
	.process_job = amdgpu_sched_process_job
};

int amdgpu_sched_ib_submit_kernel_helper(struct amdgpu_device *adev,
					 struct amdgpu_ring *ring,
					 struct amdgpu_ib *ibs,
					 unsigned num_ibs,
					 int (*free_job)(struct amdgpu_cs_parser *),
					 void *owner,
					 struct fence **f)
{
	int r = 0;
	if (amdgpu_enable_scheduler) {
		struct amdgpu_cs_parser *sched_job =
			amdgpu_cs_parser_create(adev, owner, &adev->kernel_ctx,
						ibs, num_ibs);
		if(!sched_job) {
			return -ENOMEM;
		}
		sched_job->free_job = free_job;
		mutex_lock(&sched_job->job_lock);
		r = amd_sched_push_job(ring->scheduler,
				       &adev->kernel_ctx.rings[ring->idx].entity,
				       sched_job, &sched_job->s_fence);
		if (r) {
			mutex_unlock(&sched_job->job_lock);
			kfree(sched_job);
			return r;
		}
		ibs[num_ibs - 1].sequence = sched_job->s_fence->v_seq;
		*f = fence_get(&sched_job->s_fence->base);
		mutex_unlock(&sched_job->job_lock);
	} else {
		r = amdgpu_ib_schedule(adev, num_ibs, ibs, owner);
		if (r)
			return r;
		*f = fence_get(&ibs[num_ibs - 1].fence->base);
	}
	return 0;
}
