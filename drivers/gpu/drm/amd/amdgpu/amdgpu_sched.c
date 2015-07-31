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
				    struct amd_context_entity *c_entity,
				    void *job)
{
	int r = 0;
	struct amdgpu_cs_parser *sched_job = (struct amdgpu_cs_parser *)job;
	if (sched_job->prepare_job)
		r = sched_job->prepare_job(sched_job);
	if (r) {
		DRM_ERROR("Prepare job error\n");
		schedule_work(&sched_job->job_work);
	}
	return r;
}

static void amdgpu_sched_run_job(struct amd_gpu_scheduler *sched,
				 struct amd_context_entity *c_entity,
				 void *job)
{
	int r = 0;
	struct amdgpu_cs_parser *sched_job = (struct amdgpu_cs_parser *)job;

	mutex_lock(&sched_job->job_lock);
	r = amdgpu_ib_schedule(sched_job->adev,
			       sched_job->num_ibs,
			       sched_job->ibs,
			       sched_job->filp);
	if (r)
		goto err;
	if (sched_job->run_job) {
		r = sched_job->run_job(sched_job);
		if (r)
			goto err;
	}
	atomic64_set(&c_entity->last_emitted_v_seq,
		     sched_job->ibs[sched_job->num_ibs - 1].sequence);
	wake_up_all(&c_entity->wait_emit);

	mutex_unlock(&sched_job->job_lock);
	return;
err:
	DRM_ERROR("Run job error\n");
	mutex_unlock(&sched_job->job_lock);
	schedule_work(&sched_job->job_work);
}

static void amdgpu_sched_process_job(struct amd_gpu_scheduler *sched, void *job)
{
	struct amdgpu_cs_parser *sched_job = NULL;
	struct amdgpu_fence *fence = NULL;
	struct amdgpu_ring *ring = NULL;
	struct amdgpu_device *adev = NULL;
	struct amd_context_entity *c_entity = NULL;

	if (!job)
		return;
	sched_job = (struct amdgpu_cs_parser *)job;
	fence = sched_job->ibs[sched_job->num_ibs - 1].fence;
	if (!fence)
		return;
	ring = fence->ring;
	adev = ring->adev;

	/* wake up users waiting for time stamp */
	wake_up_all(&c_entity->wait_queue);

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
					 void *owner)
{
	int r = 0;
	if (amdgpu_enable_scheduler) {
		uint64_t v_seq;
		struct amdgpu_cs_parser *sched_job =
			amdgpu_cs_parser_create(adev,
						owner,
						adev->kernel_ctx,
						ibs, 1);
		if(!sched_job) {
			return -ENOMEM;
		}
		sched_job->free_job = free_job;
		v_seq = atomic64_inc_return(&adev->kernel_ctx->rings[ring->idx].c_entity.last_queued_v_seq);
		ibs[num_ibs - 1].sequence = v_seq;
		amd_sched_push_job(ring->scheduler,
				   &adev->kernel_ctx->rings[ring->idx].c_entity,
				   sched_job);
		r = amd_sched_wait_emit(
			&adev->kernel_ctx->rings[ring->idx].c_entity,
			v_seq,
			false,
			-1);
		if (r)
			WARN(true, "emit timeout\n");
	} else
		r = amdgpu_ib_schedule(adev, 1, ibs, owner);
	return r;
}
