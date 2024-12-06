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

#include <drm/drm_drv.h>

#include "amdgpu.h"
#include "amdgpu_trace.h"
#include "amdgpu_reset.h"
#include "amdgpu_dev_coredump.h"
#include "amdgpu_xgmi.h"

static void amdgpu_job_do_core_dump(struct amdgpu_device *adev,
				    struct amdgpu_job *job)
{
	int i;

	dev_info(adev->dev, "Dumping IP State\n");
	for (i = 0; i < adev->num_ip_blocks; i++)
		if (adev->ip_blocks[i].version->funcs->dump_ip_state)
			adev->ip_blocks[i].version->funcs
				->dump_ip_state((void *)&adev->ip_blocks[i]);
	dev_info(adev->dev, "Dumping IP State Completed\n");

	amdgpu_coredump(adev, true, false, job);
}

static void amdgpu_job_core_dump(struct amdgpu_device *adev,
				 struct amdgpu_job *job)
{
	struct list_head device_list, *device_list_handle =  NULL;
	struct amdgpu_device *tmp_adev = NULL;
	struct amdgpu_hive_info *hive = NULL;

	if (!amdgpu_sriov_vf(adev))
		hive = amdgpu_get_xgmi_hive(adev);
	if (hive)
		mutex_lock(&hive->hive_lock);
	/*
	 * Reuse the logic in amdgpu_device_gpu_recover() to build list of
	 * devices for code dump
	 */
	INIT_LIST_HEAD(&device_list);
	if (!amdgpu_sriov_vf(adev) && (adev->gmc.xgmi.num_physical_nodes > 1) && hive) {
		list_for_each_entry(tmp_adev, &hive->device_list, gmc.xgmi.head)
			list_add_tail(&tmp_adev->reset_list, &device_list);
		if (!list_is_first(&adev->reset_list, &device_list))
			list_rotate_to_front(&adev->reset_list, &device_list);
		device_list_handle = &device_list;
	} else {
		list_add_tail(&adev->reset_list, &device_list);
		device_list_handle = &device_list;
	}

	/* Do the coredump for each device */
	list_for_each_entry(tmp_adev, device_list_handle, reset_list)
		amdgpu_job_do_core_dump(tmp_adev, job);

	if (hive) {
		mutex_unlock(&hive->hive_lock);
		amdgpu_put_xgmi_hive(hive);
	}
}

static enum drm_gpu_sched_stat amdgpu_job_timedout(struct drm_sched_job *s_job)
{
	struct amdgpu_ring *ring = to_amdgpu_ring(s_job->sched);
	struct amdgpu_job *job = to_amdgpu_job(s_job);
	struct amdgpu_task_info *ti;
	struct amdgpu_device *adev = ring->adev;
	int idx;
	int r;

	if (!drm_dev_enter(adev_to_drm(adev), &idx)) {
		dev_info(adev->dev, "%s - device unplugged skipping recovery on scheduler:%s",
			 __func__, s_job->sched->name);

		/* Effectively the job is aborted as the device is gone */
		return DRM_GPU_SCHED_STAT_ENODEV;
	}

	adev->job_hang = true;

	/*
	 * Do the coredump immediately after a job timeout to get a very
	 * close dump/snapshot/representation of GPU's current error status
	 * Skip it for SRIOV, since VF FLR will be triggered by host driver
	 * before job timeout
	 */
	if (!amdgpu_sriov_vf(adev))
		amdgpu_job_core_dump(adev, job);

	if (amdgpu_gpu_recovery &&
	    amdgpu_ring_soft_recovery(ring, job->vmid, s_job->s_fence->parent)) {
		dev_err(adev->dev, "ring %s timeout, but soft recovered\n",
			s_job->sched->name);
		goto exit;
	}

	dev_err(adev->dev, "ring %s timeout, signaled seq=%u, emitted seq=%u\n",
		job->base.sched->name, atomic_read(&ring->fence_drv.last_seq),
		ring->fence_drv.sync_seq);

	ti = amdgpu_vm_get_task_info_pasid(ring->adev, job->pasid);
	if (ti) {
		dev_err(adev->dev,
			"Process information: process %s pid %d thread %s pid %d\n",
			ti->process_name, ti->tgid, ti->task_name, ti->pid);
		amdgpu_vm_put_task_info(ti);
	}

	dma_fence_set_error(&s_job->s_fence->finished, -ETIME);

	/* attempt a per ring reset */
	if (amdgpu_gpu_recovery &&
	    ring->funcs->reset) {
		dev_err(adev->dev, "Starting %s ring reset\n", s_job->sched->name);
		/* stop the scheduler, but don't mess with the
		 * bad job yet because if ring reset fails
		 * we'll fall back to full GPU reset.
		 */
		drm_sched_wqueue_stop(&ring->sched);
		r = amdgpu_ring_reset(ring, job->vmid);
		if (!r) {
			if (amdgpu_ring_sched_ready(ring))
				drm_sched_stop(&ring->sched, s_job);
			atomic_inc(&ring->adev->gpu_reset_counter);
			amdgpu_fence_driver_force_completion(ring);
			if (amdgpu_ring_sched_ready(ring))
				drm_sched_start(&ring->sched, 0);
			goto exit;
		}
		dev_err(adev->dev, "Ring %s reset failure\n", ring->sched.name);
	}

	if (amdgpu_device_should_recover_gpu(ring->adev)) {
		struct amdgpu_reset_context reset_context;
		memset(&reset_context, 0, sizeof(reset_context));

		reset_context.method = AMD_RESET_METHOD_NONE;
		reset_context.reset_req_dev = adev;
		reset_context.src = AMDGPU_RESET_SRC_JOB;
		clear_bit(AMDGPU_NEED_FULL_RESET, &reset_context.flags);

		/*
		 * To avoid an unnecessary extra coredump, as we have already
		 * got the very close representation of GPU's error status
		 */
		set_bit(AMDGPU_SKIP_COREDUMP, &reset_context.flags);

		r = amdgpu_device_gpu_recover(ring->adev, job, &reset_context);
		if (r)
			dev_err(adev->dev, "GPU Recovery Failed: %d\n", r);
	} else {
		drm_sched_suspend_timeout(&ring->sched);
		if (amdgpu_sriov_vf(adev))
			adev->virt.tdr_debug = true;
	}

exit:
	adev->job_hang = false;
	drm_dev_exit(idx);
	return DRM_GPU_SCHED_STAT_NOMINAL;
}

int amdgpu_job_alloc(struct amdgpu_device *adev, struct amdgpu_vm *vm,
		     struct drm_sched_entity *entity, void *owner,
		     unsigned int num_ibs, struct amdgpu_job **job)
{
	if (num_ibs == 0)
		return -EINVAL;

	*job = kzalloc(struct_size(*job, ibs, num_ibs), GFP_KERNEL);
	if (!*job)
		return -ENOMEM;

	/*
	 * Initialize the scheduler to at least some ring so that we always
	 * have a pointer to adev.
	 */
	(*job)->base.sched = &adev->rings[0]->sched;
	(*job)->vm = vm;

	amdgpu_sync_create(&(*job)->explicit_sync);
	(*job)->generation = amdgpu_vm_generation(adev, vm);
	(*job)->vm_pd_addr = AMDGPU_BO_INVALID_OFFSET;

	if (!entity)
		return 0;

	return drm_sched_job_init(&(*job)->base, entity, 1, owner);
}

int amdgpu_job_alloc_with_ib(struct amdgpu_device *adev,
			     struct drm_sched_entity *entity, void *owner,
			     size_t size, enum amdgpu_ib_pool_type pool_type,
			     struct amdgpu_job **job)
{
	int r;

	r = amdgpu_job_alloc(adev, NULL, entity, owner, 1, job);
	if (r)
		return r;

	(*job)->num_ibs = 1;
	r = amdgpu_ib_get(adev, NULL, size, pool_type, &(*job)->ibs[0]);
	if (r) {
		if (entity)
			drm_sched_job_cleanup(&(*job)->base);
		kfree(*job);
	}

	return r;
}

void amdgpu_job_set_resources(struct amdgpu_job *job, struct amdgpu_bo *gds,
			      struct amdgpu_bo *gws, struct amdgpu_bo *oa)
{
	if (gds) {
		job->gds_base = amdgpu_bo_gpu_offset(gds) >> PAGE_SHIFT;
		job->gds_size = amdgpu_bo_size(gds) >> PAGE_SHIFT;
	}
	if (gws) {
		job->gws_base = amdgpu_bo_gpu_offset(gws) >> PAGE_SHIFT;
		job->gws_size = amdgpu_bo_size(gws) >> PAGE_SHIFT;
	}
	if (oa) {
		job->oa_base = amdgpu_bo_gpu_offset(oa) >> PAGE_SHIFT;
		job->oa_size = amdgpu_bo_size(oa) >> PAGE_SHIFT;
	}
}

void amdgpu_job_free_resources(struct amdgpu_job *job)
{
	struct dma_fence *f;
	unsigned i;

	/* Check if any fences where initialized */
	if (job->base.s_fence && job->base.s_fence->finished.ops)
		f = &job->base.s_fence->finished;
	else if (job->hw_fence.ops)
		f = &job->hw_fence;
	else
		f = NULL;

	for (i = 0; i < job->num_ibs; ++i)
		amdgpu_ib_free(NULL, &job->ibs[i], f);
}

static void amdgpu_job_free_cb(struct drm_sched_job *s_job)
{
	struct amdgpu_job *job = to_amdgpu_job(s_job);

	drm_sched_job_cleanup(s_job);

	amdgpu_sync_free(&job->explicit_sync);

	/* only put the hw fence if has embedded fence */
	if (!job->hw_fence.ops)
		kfree(job);
	else
		dma_fence_put(&job->hw_fence);
}

void amdgpu_job_set_gang_leader(struct amdgpu_job *job,
				struct amdgpu_job *leader)
{
	struct dma_fence *fence = &leader->base.s_fence->scheduled;

	WARN_ON(job->gang_submit);

	/*
	 * Don't add a reference when we are the gang leader to avoid circle
	 * dependency.
	 */
	if (job != leader)
		dma_fence_get(fence);
	job->gang_submit = fence;
}

void amdgpu_job_free(struct amdgpu_job *job)
{
	if (job->base.entity)
		drm_sched_job_cleanup(&job->base);

	amdgpu_job_free_resources(job);
	amdgpu_sync_free(&job->explicit_sync);
	if (job->gang_submit != &job->base.s_fence->scheduled)
		dma_fence_put(job->gang_submit);

	if (!job->hw_fence.ops)
		kfree(job);
	else
		dma_fence_put(&job->hw_fence);
}

struct dma_fence *amdgpu_job_submit(struct amdgpu_job *job)
{
	struct dma_fence *f;

	drm_sched_job_arm(&job->base);
	f = dma_fence_get(&job->base.s_fence->finished);
	amdgpu_job_free_resources(job);
	drm_sched_entity_push_job(&job->base);

	return f;
}

int amdgpu_job_submit_direct(struct amdgpu_job *job, struct amdgpu_ring *ring,
			     struct dma_fence **fence)
{
	int r;

	job->base.sched = &ring->sched;
	r = amdgpu_ib_schedule(ring, job->num_ibs, job->ibs, job, fence);

	if (r)
		return r;

	amdgpu_job_free(job);
	return 0;
}

static struct dma_fence *
amdgpu_job_prepare_job(struct drm_sched_job *sched_job,
		      struct drm_sched_entity *s_entity)
{
	struct amdgpu_ring *ring = to_amdgpu_ring(s_entity->rq->sched);
	struct amdgpu_job *job = to_amdgpu_job(sched_job);
	struct dma_fence *fence = NULL;
	int r;

	r = drm_sched_entity_error(s_entity);
	if (r)
		goto error;

	if (job->gang_submit)
		fence = amdgpu_device_switch_gang(ring->adev, job->gang_submit);

	if (!fence && job->vm && !job->vmid) {
		r = amdgpu_vmid_grab(job->vm, ring, job, &fence);
		if (r) {
			dev_err(ring->adev->dev, "Error getting VM ID (%d)\n", r);
			goto error;
		}
	}

	return fence;

error:
	dma_fence_set_error(&job->base.s_fence->finished, r);
	return NULL;
}

static struct dma_fence *amdgpu_job_run(struct drm_sched_job *sched_job)
{
	struct amdgpu_ring *ring = to_amdgpu_ring(sched_job->sched);
	struct amdgpu_device *adev = ring->adev;
	struct dma_fence *fence = NULL, *finished;
	struct amdgpu_job *job;
	int r = 0;

	job = to_amdgpu_job(sched_job);
	finished = &job->base.s_fence->finished;

	trace_amdgpu_sched_run_job(job);

	/* Skip job if VRAM is lost and never resubmit gangs */
	if (job->generation != amdgpu_vm_generation(adev, job->vm) ||
	    (job->job_run_counter && job->gang_submit))
		dma_fence_set_error(finished, -ECANCELED);

	if (finished->error < 0) {
		dev_dbg(adev->dev, "Skip scheduling IBs in ring(%s)",
			ring->name);
	} else {
		r = amdgpu_ib_schedule(ring, job->num_ibs, job->ibs, job,
				       &fence);
		if (r)
			dev_err(adev->dev,
				"Error scheduling IBs (%d) in ring(%s)", r,
				ring->name);
	}

	job->job_run_counter++;
	amdgpu_job_free_resources(job);

	fence = r ? ERR_PTR(r) : fence;
	return fence;
}

#define to_drm_sched_job(sched_job)		\
		container_of((sched_job), struct drm_sched_job, queue_node)

void amdgpu_job_stop_all_jobs_on_sched(struct drm_gpu_scheduler *sched)
{
	struct drm_sched_job *s_job;
	struct drm_sched_entity *s_entity = NULL;
	int i;

	/* Signal all jobs not yet scheduled */
	for (i = DRM_SCHED_PRIORITY_KERNEL; i < sched->num_rqs; i++) {
		struct drm_sched_rq *rq = sched->sched_rq[i];
		spin_lock(&rq->lock);
		list_for_each_entry(s_entity, &rq->entities, list) {
			while ((s_job = to_drm_sched_job(spsc_queue_pop(&s_entity->job_queue)))) {
				struct drm_sched_fence *s_fence = s_job->s_fence;

				dma_fence_signal(&s_fence->scheduled);
				dma_fence_set_error(&s_fence->finished, -EHWPOISON);
				dma_fence_signal(&s_fence->finished);
			}
		}
		spin_unlock(&rq->lock);
	}

	/* Signal all jobs already scheduled to HW */
	list_for_each_entry(s_job, &sched->pending_list, list) {
		struct drm_sched_fence *s_fence = s_job->s_fence;

		dma_fence_set_error(&s_fence->finished, -EHWPOISON);
		dma_fence_signal(&s_fence->finished);
	}
}

const struct drm_sched_backend_ops amdgpu_sched_ops = {
	.prepare_job = amdgpu_job_prepare_job,
	.run_job = amdgpu_job_run,
	.timedout_job = amdgpu_job_timedout,
	.free_job = amdgpu_job_free_cb
};
