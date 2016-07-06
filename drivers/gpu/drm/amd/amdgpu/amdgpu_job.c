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

static void amdgpu_job_free_handler(struct work_struct *ws)
{
	struct amdgpu_job *job = container_of(ws, struct amdgpu_job, base.work_free_job);
	amd_sched_job_put(&job->base);
}

void amdgpu_job_timeout_func(struct work_struct *work)
{
	struct amdgpu_job *job = container_of(work, struct amdgpu_job, base.work_tdr.work);
	DRM_ERROR("ring %s timeout, last signaled seq=%u, last emitted seq=%u\n",
				job->base.sched->name,
				(uint32_t)atomic_read(&job->ring->fence_drv.last_seq),
				job->ring->fence_drv.sync_seq);

	amd_sched_job_put(&job->base);
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
	INIT_WORK(&(*job)->base.work_free_job, amdgpu_job_free_handler);

	amdgpu_sync_create(&(*job)->sync);

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

	amdgpu_bo_unref(&job->uf_bo);
	amdgpu_sync_free(&job->sync);

	if (!job->base.use_sched)
		kfree(job);
}

void amdgpu_job_free_func(struct kref *refcount)
{
	struct amdgpu_job *job = container_of(refcount, struct amdgpu_job, base.refcount);
	kfree(job);
}

int amdgpu_job_submit(struct amdgpu_job *job, struct amdgpu_ring *ring,
		      struct amd_sched_entity *entity, void *owner,
		      struct fence **f)
{
	struct fence *fence;
	int r;
	job->ring = ring;

	if (!f)
		return -EINVAL;

	r = amd_sched_job_init(&job->base, &ring->sched,
			       entity, amdgpu_job_timeout_func,
			       amdgpu_job_free_func, owner, &fence);
	if (r)
		return r;

	job->owner = owner;
	job->ctx = entity->fence_context;
	*f = fence_get(fence);
	amd_sched_entity_push_job(&job->base);

	return 0;
}

static struct fence *amdgpu_job_dependency(struct amd_sched_job *sched_job)
{
	struct amdgpu_job *job = to_amdgpu_job(sched_job);
	struct amdgpu_vm *vm = job->vm;

	struct fence *fence = amdgpu_sync_get_fence(&job->sync);

	if (fence == NULL && vm && !job->vm_id) {
		struct amdgpu_ring *ring = job->ring;
		int r;

		r = amdgpu_vm_grab_id(vm, ring, &job->sync,
				      &job->base.s_fence->base,
				      &job->vm_id, &job->vm_pd_addr);
		if (r)
			DRM_ERROR("Error getting VM ID (%d)\n", r);

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
			       job->sync.last_vm_update, job, &fence);
	if (r) {
		DRM_ERROR("Error scheduling IBs (%d)\n", r);
		goto err;
	}

err:
	job->fence = fence;
	amdgpu_job_free(job);
	return fence;
}

const struct amd_sched_backend_ops amdgpu_sched_ops = {
	.dependency = amdgpu_job_dependency,
	.run_job = amdgpu_job_run,
	.begin_job = amd_sched_job_begin,
	.finish_job = amd_sched_job_finish,
};
