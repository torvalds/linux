// SPDX-License-Identifier: GPL-2.0+
/* Copyright (C) 2018 Broadcom */

/**
 * DOC: Broadcom V3D scheduling
 *
 * The shared DRM GPU scheduler is used to coordinate submitting jobs
 * to the hardware.  Each DRM fd (roughly a client process) gets its
 * own scheduler entity, which will process jobs in order.  The GPU
 * scheduler will round-robin between clients to submit the next job.
 *
 * For simplicity, and in order to keep latency low for interactive
 * jobs when bulk background jobs are queued up, we submit a new job
 * to the HW only when it has completed the last one, instead of
 * filling up the CT[01]Q FIFOs with jobs.  Similarly, we use
 * v3d_job_dependency() to manage the dependency between bin and
 * render, instead of having the clients submit jobs using the HW's
 * semaphores to interlock between them.
 */

#include <linux/kthread.h>

#include "v3d_drv.h"
#include "v3d_regs.h"
#include "v3d_trace.h"

static struct v3d_job *
to_v3d_job(struct drm_sched_job *sched_job)
{
	return container_of(sched_job, struct v3d_job, base);
}

static struct v3d_bin_job *
to_bin_job(struct drm_sched_job *sched_job)
{
	return container_of(sched_job, struct v3d_bin_job, base.base);
}

static struct v3d_render_job *
to_render_job(struct drm_sched_job *sched_job)
{
	return container_of(sched_job, struct v3d_render_job, base.base);
}

static struct v3d_tfu_job *
to_tfu_job(struct drm_sched_job *sched_job)
{
	return container_of(sched_job, struct v3d_tfu_job, base.base);
}

static struct v3d_csd_job *
to_csd_job(struct drm_sched_job *sched_job)
{
	return container_of(sched_job, struct v3d_csd_job, base.base);
}

static void
v3d_job_free(struct drm_sched_job *sched_job)
{
	struct v3d_job *job = to_v3d_job(sched_job);

	drm_sched_job_cleanup(sched_job);
	v3d_job_put(job);
}

/**
 * Returns the fences that the job depends on, one by one.
 *
 * If placed in the scheduler's .dependency method, the corresponding
 * .run_job won't be called until all of them have been signaled.
 */
static struct dma_fence *
v3d_job_dependency(struct drm_sched_job *sched_job,
		   struct drm_sched_entity *s_entity)
{
	struct v3d_job *job = to_v3d_job(sched_job);

	/* XXX: Wait on a fence for switching the GMP if necessary,
	 * and then do so.
	 */

	if (!xa_empty(&job->deps))
		return xa_erase(&job->deps, job->last_dep++);

	return NULL;
}

static struct dma_fence *v3d_bin_job_run(struct drm_sched_job *sched_job)
{
	struct v3d_bin_job *job = to_bin_job(sched_job);
	struct v3d_dev *v3d = job->base.v3d;
	struct drm_device *dev = &v3d->drm;
	struct dma_fence *fence;
	unsigned long irqflags;

	if (unlikely(job->base.base.s_fence->finished.error))
		return NULL;

	/* Lock required around bin_job update vs
	 * v3d_overflow_mem_work().
	 */
	spin_lock_irqsave(&v3d->job_lock, irqflags);
	v3d->bin_job = job;
	/* Clear out the overflow allocation, so we don't
	 * reuse the overflow attached to a previous job.
	 */
	V3D_CORE_WRITE(0, V3D_PTB_BPOS, 0);
	spin_unlock_irqrestore(&v3d->job_lock, irqflags);

	v3d_invalidate_caches(v3d);

	fence = v3d_fence_create(v3d, V3D_BIN);
	if (IS_ERR(fence))
		return NULL;

	if (job->base.irq_fence)
		dma_fence_put(job->base.irq_fence);
	job->base.irq_fence = dma_fence_get(fence);

	trace_v3d_submit_cl(dev, false, to_v3d_fence(fence)->seqno,
			    job->start, job->end);

	/* Set the current and end address of the control list.
	 * Writing the end register is what starts the job.
	 */
	if (job->qma) {
		V3D_CORE_WRITE(0, V3D_CLE_CT0QMA, job->qma);
		V3D_CORE_WRITE(0, V3D_CLE_CT0QMS, job->qms);
	}
	if (job->qts) {
		V3D_CORE_WRITE(0, V3D_CLE_CT0QTS,
			       V3D_CLE_CT0QTS_ENABLE |
			       job->qts);
	}
	V3D_CORE_WRITE(0, V3D_CLE_CT0QBA, job->start);
	V3D_CORE_WRITE(0, V3D_CLE_CT0QEA, job->end);

	return fence;
}

static struct dma_fence *v3d_render_job_run(struct drm_sched_job *sched_job)
{
	struct v3d_render_job *job = to_render_job(sched_job);
	struct v3d_dev *v3d = job->base.v3d;
	struct drm_device *dev = &v3d->drm;
	struct dma_fence *fence;

	if (unlikely(job->base.base.s_fence->finished.error))
		return NULL;

	v3d->render_job = job;

	/* Can we avoid this flush?  We need to be careful of
	 * scheduling, though -- imagine job0 rendering to texture and
	 * job1 reading, and them being executed as bin0, bin1,
	 * render0, render1, so that render1's flush at bin time
	 * wasn't enough.
	 */
	v3d_invalidate_caches(v3d);

	fence = v3d_fence_create(v3d, V3D_RENDER);
	if (IS_ERR(fence))
		return NULL;

	if (job->base.irq_fence)
		dma_fence_put(job->base.irq_fence);
	job->base.irq_fence = dma_fence_get(fence);

	trace_v3d_submit_cl(dev, true, to_v3d_fence(fence)->seqno,
			    job->start, job->end);

	/* XXX: Set the QCFG */

	/* Set the current and end address of the control list.
	 * Writing the end register is what starts the job.
	 */
	V3D_CORE_WRITE(0, V3D_CLE_CT1QBA, job->start);
	V3D_CORE_WRITE(0, V3D_CLE_CT1QEA, job->end);

	return fence;
}

static struct dma_fence *
v3d_tfu_job_run(struct drm_sched_job *sched_job)
{
	struct v3d_tfu_job *job = to_tfu_job(sched_job);
	struct v3d_dev *v3d = job->base.v3d;
	struct drm_device *dev = &v3d->drm;
	struct dma_fence *fence;

	fence = v3d_fence_create(v3d, V3D_TFU);
	if (IS_ERR(fence))
		return NULL;

	v3d->tfu_job = job;
	if (job->base.irq_fence)
		dma_fence_put(job->base.irq_fence);
	job->base.irq_fence = dma_fence_get(fence);

	trace_v3d_submit_tfu(dev, to_v3d_fence(fence)->seqno);

	V3D_WRITE(V3D_TFU_IIA, job->args.iia);
	V3D_WRITE(V3D_TFU_IIS, job->args.iis);
	V3D_WRITE(V3D_TFU_ICA, job->args.ica);
	V3D_WRITE(V3D_TFU_IUA, job->args.iua);
	V3D_WRITE(V3D_TFU_IOA, job->args.ioa);
	V3D_WRITE(V3D_TFU_IOS, job->args.ios);
	V3D_WRITE(V3D_TFU_COEF0, job->args.coef[0]);
	if (job->args.coef[0] & V3D_TFU_COEF0_USECOEF) {
		V3D_WRITE(V3D_TFU_COEF1, job->args.coef[1]);
		V3D_WRITE(V3D_TFU_COEF2, job->args.coef[2]);
		V3D_WRITE(V3D_TFU_COEF3, job->args.coef[3]);
	}
	/* ICFG kicks off the job. */
	V3D_WRITE(V3D_TFU_ICFG, job->args.icfg | V3D_TFU_ICFG_IOC);

	return fence;
}

static struct dma_fence *
v3d_csd_job_run(struct drm_sched_job *sched_job)
{
	struct v3d_csd_job *job = to_csd_job(sched_job);
	struct v3d_dev *v3d = job->base.v3d;
	struct drm_device *dev = &v3d->drm;
	struct dma_fence *fence;
	int i;

	v3d->csd_job = job;

	v3d_invalidate_caches(v3d);

	fence = v3d_fence_create(v3d, V3D_CSD);
	if (IS_ERR(fence))
		return NULL;

	if (job->base.irq_fence)
		dma_fence_put(job->base.irq_fence);
	job->base.irq_fence = dma_fence_get(fence);

	trace_v3d_submit_csd(dev, to_v3d_fence(fence)->seqno);

	for (i = 1; i <= 6; i++)
		V3D_CORE_WRITE(0, V3D_CSD_QUEUED_CFG0 + 4 * i, job->args.cfg[i]);
	/* CFG0 write kicks off the job. */
	V3D_CORE_WRITE(0, V3D_CSD_QUEUED_CFG0, job->args.cfg[0]);

	return fence;
}

static struct dma_fence *
v3d_cache_clean_job_run(struct drm_sched_job *sched_job)
{
	struct v3d_job *job = to_v3d_job(sched_job);
	struct v3d_dev *v3d = job->v3d;

	v3d_clean_caches(v3d);

	return NULL;
}

static void
v3d_gpu_reset_for_timeout(struct v3d_dev *v3d, struct drm_sched_job *sched_job)
{
	enum v3d_queue q;

	mutex_lock(&v3d->reset_lock);

	/* block scheduler */
	for (q = 0; q < V3D_MAX_QUEUES; q++)
		drm_sched_stop(&v3d->queue[q].sched, sched_job);

	if (sched_job)
		drm_sched_increase_karma(sched_job);

	/* get the GPU back into the init state */
	v3d_reset(v3d);

	for (q = 0; q < V3D_MAX_QUEUES; q++)
		drm_sched_resubmit_jobs(&v3d->queue[q].sched);

	/* Unblock schedulers and restart their jobs. */
	for (q = 0; q < V3D_MAX_QUEUES; q++) {
		drm_sched_start(&v3d->queue[q].sched, true);
	}

	mutex_unlock(&v3d->reset_lock);
}

/* If the current address or return address have changed, then the GPU
 * has probably made progress and we should delay the reset.  This
 * could fail if the GPU got in an infinite loop in the CL, but that
 * is pretty unlikely outside of an i-g-t testcase.
 */
static void
v3d_cl_job_timedout(struct drm_sched_job *sched_job, enum v3d_queue q,
		    u32 *timedout_ctca, u32 *timedout_ctra)
{
	struct v3d_job *job = to_v3d_job(sched_job);
	struct v3d_dev *v3d = job->v3d;
	u32 ctca = V3D_CORE_READ(0, V3D_CLE_CTNCA(q));
	u32 ctra = V3D_CORE_READ(0, V3D_CLE_CTNRA(q));

	if (*timedout_ctca != ctca || *timedout_ctra != ctra) {
		*timedout_ctca = ctca;
		*timedout_ctra = ctra;
		return;
	}

	v3d_gpu_reset_for_timeout(v3d, sched_job);
}

static void
v3d_bin_job_timedout(struct drm_sched_job *sched_job)
{
	struct v3d_bin_job *job = to_bin_job(sched_job);

	v3d_cl_job_timedout(sched_job, V3D_BIN,
			    &job->timedout_ctca, &job->timedout_ctra);
}

static void
v3d_render_job_timedout(struct drm_sched_job *sched_job)
{
	struct v3d_render_job *job = to_render_job(sched_job);

	v3d_cl_job_timedout(sched_job, V3D_RENDER,
			    &job->timedout_ctca, &job->timedout_ctra);
}

static void
v3d_generic_job_timedout(struct drm_sched_job *sched_job)
{
	struct v3d_job *job = to_v3d_job(sched_job);

	v3d_gpu_reset_for_timeout(job->v3d, sched_job);
}

static void
v3d_csd_job_timedout(struct drm_sched_job *sched_job)
{
	struct v3d_csd_job *job = to_csd_job(sched_job);
	struct v3d_dev *v3d = job->base.v3d;
	u32 batches = V3D_CORE_READ(0, V3D_CSD_CURRENT_CFG4);

	/* If we've made progress, skip reset and let the timer get
	 * rearmed.
	 */
	if (job->timedout_batches != batches) {
		job->timedout_batches = batches;
		return;
	}

	v3d_gpu_reset_for_timeout(v3d, sched_job);
}

static const struct drm_sched_backend_ops v3d_bin_sched_ops = {
	.dependency = v3d_job_dependency,
	.run_job = v3d_bin_job_run,
	.timedout_job = v3d_bin_job_timedout,
	.free_job = v3d_job_free,
};

static const struct drm_sched_backend_ops v3d_render_sched_ops = {
	.dependency = v3d_job_dependency,
	.run_job = v3d_render_job_run,
	.timedout_job = v3d_render_job_timedout,
	.free_job = v3d_job_free,
};

static const struct drm_sched_backend_ops v3d_tfu_sched_ops = {
	.dependency = v3d_job_dependency,
	.run_job = v3d_tfu_job_run,
	.timedout_job = v3d_generic_job_timedout,
	.free_job = v3d_job_free,
};

static const struct drm_sched_backend_ops v3d_csd_sched_ops = {
	.dependency = v3d_job_dependency,
	.run_job = v3d_csd_job_run,
	.timedout_job = v3d_csd_job_timedout,
	.free_job = v3d_job_free
};

static const struct drm_sched_backend_ops v3d_cache_clean_sched_ops = {
	.dependency = v3d_job_dependency,
	.run_job = v3d_cache_clean_job_run,
	.timedout_job = v3d_generic_job_timedout,
	.free_job = v3d_job_free
};

int
v3d_sched_init(struct v3d_dev *v3d)
{
	int hw_jobs_limit = 1;
	int job_hang_limit = 0;
	int hang_limit_ms = 500;
	int ret;

	ret = drm_sched_init(&v3d->queue[V3D_BIN].sched,
			     &v3d_bin_sched_ops,
			     hw_jobs_limit, job_hang_limit,
			     msecs_to_jiffies(hang_limit_ms),
			     "v3d_bin");
	if (ret) {
		dev_err(v3d->drm.dev, "Failed to create bin scheduler: %d.", ret);
		return ret;
	}

	ret = drm_sched_init(&v3d->queue[V3D_RENDER].sched,
			     &v3d_render_sched_ops,
			     hw_jobs_limit, job_hang_limit,
			     msecs_to_jiffies(hang_limit_ms),
			     "v3d_render");
	if (ret) {
		dev_err(v3d->drm.dev, "Failed to create render scheduler: %d.",
			ret);
		v3d_sched_fini(v3d);
		return ret;
	}

	ret = drm_sched_init(&v3d->queue[V3D_TFU].sched,
			     &v3d_tfu_sched_ops,
			     hw_jobs_limit, job_hang_limit,
			     msecs_to_jiffies(hang_limit_ms),
			     "v3d_tfu");
	if (ret) {
		dev_err(v3d->drm.dev, "Failed to create TFU scheduler: %d.",
			ret);
		v3d_sched_fini(v3d);
		return ret;
	}

	if (v3d_has_csd(v3d)) {
		ret = drm_sched_init(&v3d->queue[V3D_CSD].sched,
				     &v3d_csd_sched_ops,
				     hw_jobs_limit, job_hang_limit,
				     msecs_to_jiffies(hang_limit_ms),
				     "v3d_csd");
		if (ret) {
			dev_err(v3d->drm.dev, "Failed to create CSD scheduler: %d.",
				ret);
			v3d_sched_fini(v3d);
			return ret;
		}

		ret = drm_sched_init(&v3d->queue[V3D_CACHE_CLEAN].sched,
				     &v3d_cache_clean_sched_ops,
				     hw_jobs_limit, job_hang_limit,
				     msecs_to_jiffies(hang_limit_ms),
				     "v3d_cache_clean");
		if (ret) {
			dev_err(v3d->drm.dev, "Failed to create CACHE_CLEAN scheduler: %d.",
				ret);
			v3d_sched_fini(v3d);
			return ret;
		}
	}

	return 0;
}

void
v3d_sched_fini(struct v3d_dev *v3d)
{
	enum v3d_queue q;

	for (q = 0; q < V3D_MAX_QUEUES; q++) {
		if (v3d->queue[q].sched.ready)
			drm_sched_fini(&v3d->queue[q].sched);
	}
}
