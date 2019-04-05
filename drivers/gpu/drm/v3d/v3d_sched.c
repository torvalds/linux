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

static struct v3d_tfu_job *
to_tfu_job(struct drm_sched_job *sched_job)
{
	return container_of(sched_job, struct v3d_tfu_job, base);
}

static void
v3d_job_free(struct drm_sched_job *sched_job)
{
	struct v3d_job *job = to_v3d_job(sched_job);

	drm_sched_job_cleanup(sched_job);

	v3d_exec_put(job->exec);
}

static void
v3d_tfu_job_free(struct drm_sched_job *sched_job)
{
	struct v3d_tfu_job *job = to_tfu_job(sched_job);

	drm_sched_job_cleanup(sched_job);

	v3d_tfu_job_put(job);
}

/**
 * Returns the fences that the bin or render job depends on, one by one.
 * v3d_job_run() won't be called until all of them have been signaled.
 */
static struct dma_fence *
v3d_job_dependency(struct drm_sched_job *sched_job,
		   struct drm_sched_entity *s_entity)
{
	struct v3d_job *job = to_v3d_job(sched_job);
	struct v3d_exec_info *exec = job->exec;
	enum v3d_queue q = job == &exec->bin ? V3D_BIN : V3D_RENDER;
	struct dma_fence *fence;

	fence = job->in_fence;
	if (fence) {
		job->in_fence = NULL;
		return fence;
	}

	if (q == V3D_RENDER) {
		/* If we had a bin job, the render job definitely depends on
		 * it. We first have to wait for bin to be scheduled, so that
		 * its done_fence is created.
		 */
		fence = exec->bin_done_fence;
		if (fence) {
			exec->bin_done_fence = NULL;
			return fence;
		}
	}

	/* XXX: Wait on a fence for switching the GMP if necessary,
	 * and then do so.
	 */

	return fence;
}

/**
 * Returns the fences that the TFU job depends on, one by one.
 * v3d_tfu_job_run() won't be called until all of them have been
 * signaled.
 */
static struct dma_fence *
v3d_tfu_job_dependency(struct drm_sched_job *sched_job,
		       struct drm_sched_entity *s_entity)
{
	struct v3d_tfu_job *job = to_tfu_job(sched_job);
	struct dma_fence *fence;

	fence = job->in_fence;
	if (fence) {
		job->in_fence = NULL;
		return fence;
	}

	return NULL;
}

static struct dma_fence *v3d_job_run(struct drm_sched_job *sched_job)
{
	struct v3d_job *job = to_v3d_job(sched_job);
	struct v3d_exec_info *exec = job->exec;
	enum v3d_queue q = job == &exec->bin ? V3D_BIN : V3D_RENDER;
	struct v3d_dev *v3d = exec->v3d;
	struct drm_device *dev = &v3d->drm;
	struct dma_fence *fence;
	unsigned long irqflags;

	if (unlikely(job->base.s_fence->finished.error))
		return NULL;

	/* Lock required around bin_job update vs
	 * v3d_overflow_mem_work().
	 */
	spin_lock_irqsave(&v3d->job_lock, irqflags);
	if (q == V3D_BIN) {
		v3d->bin_job = job->exec;

		/* Clear out the overflow allocation, so we don't
		 * reuse the overflow attached to a previous job.
		 */
		V3D_CORE_WRITE(0, V3D_PTB_BPOS, 0);
	} else {
		v3d->render_job = job->exec;
	}
	spin_unlock_irqrestore(&v3d->job_lock, irqflags);

	/* Can we avoid this flush when q==RENDER?  We need to be
	 * careful of scheduling, though -- imagine job0 rendering to
	 * texture and job1 reading, and them being executed as bin0,
	 * bin1, render0, render1, so that render1's flush at bin time
	 * wasn't enough.
	 */
	v3d_invalidate_caches(v3d);

	fence = v3d_fence_create(v3d, q);
	if (IS_ERR(fence))
		return NULL;

	if (job->irq_fence)
		dma_fence_put(job->irq_fence);
	job->irq_fence = dma_fence_get(fence);

	trace_v3d_submit_cl(dev, q == V3D_RENDER, to_v3d_fence(fence)->seqno,
			    job->start, job->end);

	if (q == V3D_BIN) {
		if (exec->qma) {
			V3D_CORE_WRITE(0, V3D_CLE_CT0QMA, exec->qma);
			V3D_CORE_WRITE(0, V3D_CLE_CT0QMS, exec->qms);
		}
		if (exec->qts) {
			V3D_CORE_WRITE(0, V3D_CLE_CT0QTS,
				       V3D_CLE_CT0QTS_ENABLE |
				       exec->qts);
		}
	} else {
		/* XXX: Set the QCFG */
	}

	/* Set the current and end address of the control list.
	 * Writing the end register is what starts the job.
	 */
	V3D_CORE_WRITE(0, V3D_CLE_CTNQBA(q), job->start);
	V3D_CORE_WRITE(0, V3D_CLE_CTNQEA(q), job->end);

	return fence;
}

static struct dma_fence *
v3d_tfu_job_run(struct drm_sched_job *sched_job)
{
	struct v3d_tfu_job *job = to_tfu_job(sched_job);
	struct v3d_dev *v3d = job->v3d;
	struct drm_device *dev = &v3d->drm;
	struct dma_fence *fence;

	fence = v3d_fence_create(v3d, V3D_TFU);
	if (IS_ERR(fence))
		return NULL;

	v3d->tfu_job = job;
	if (job->irq_fence)
		dma_fence_put(job->irq_fence);
	job->irq_fence = dma_fence_get(fence);

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

static void
v3d_gpu_reset_for_timeout(struct v3d_dev *v3d, struct drm_sched_job *sched_job)
{
	enum v3d_queue q;

	mutex_lock(&v3d->reset_lock);

	/* block scheduler */
	for (q = 0; q < V3D_MAX_QUEUES; q++)
		drm_sched_stop(&v3d->queue[q].sched);

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

static void
v3d_job_timedout(struct drm_sched_job *sched_job)
{
	struct v3d_job *job = to_v3d_job(sched_job);
	struct v3d_exec_info *exec = job->exec;
	struct v3d_dev *v3d = exec->v3d;
	enum v3d_queue job_q = job == &exec->bin ? V3D_BIN : V3D_RENDER;
	u32 ctca = V3D_CORE_READ(0, V3D_CLE_CTNCA(job_q));
	u32 ctra = V3D_CORE_READ(0, V3D_CLE_CTNRA(job_q));

	/* If the current address or return address have changed, then
	 * the GPU has probably made progress and we should delay the
	 * reset.  This could fail if the GPU got in an infinite loop
	 * in the CL, but that is pretty unlikely outside of an i-g-t
	 * testcase.
	 */
	if (job->timedout_ctca != ctca || job->timedout_ctra != ctra) {
		job->timedout_ctca = ctca;
		job->timedout_ctra = ctra;
		return;
	}

	v3d_gpu_reset_for_timeout(v3d, sched_job);
}

static void
v3d_tfu_job_timedout(struct drm_sched_job *sched_job)
{
	struct v3d_tfu_job *job = to_tfu_job(sched_job);

	v3d_gpu_reset_for_timeout(job->v3d, sched_job);
}

static const struct drm_sched_backend_ops v3d_sched_ops = {
	.dependency = v3d_job_dependency,
	.run_job = v3d_job_run,
	.timedout_job = v3d_job_timedout,
	.free_job = v3d_job_free
};

static const struct drm_sched_backend_ops v3d_tfu_sched_ops = {
	.dependency = v3d_tfu_job_dependency,
	.run_job = v3d_tfu_job_run,
	.timedout_job = v3d_tfu_job_timedout,
	.free_job = v3d_tfu_job_free
};

int
v3d_sched_init(struct v3d_dev *v3d)
{
	int hw_jobs_limit = 1;
	int job_hang_limit = 0;
	int hang_limit_ms = 500;
	int ret;

	ret = drm_sched_init(&v3d->queue[V3D_BIN].sched,
			     &v3d_sched_ops,
			     hw_jobs_limit, job_hang_limit,
			     msecs_to_jiffies(hang_limit_ms),
			     "v3d_bin");
	if (ret) {
		dev_err(v3d->dev, "Failed to create bin scheduler: %d.", ret);
		return ret;
	}

	ret = drm_sched_init(&v3d->queue[V3D_RENDER].sched,
			     &v3d_sched_ops,
			     hw_jobs_limit, job_hang_limit,
			     msecs_to_jiffies(hang_limit_ms),
			     "v3d_render");
	if (ret) {
		dev_err(v3d->dev, "Failed to create render scheduler: %d.",
			ret);
		drm_sched_fini(&v3d->queue[V3D_BIN].sched);
		return ret;
	}

	ret = drm_sched_init(&v3d->queue[V3D_TFU].sched,
			     &v3d_tfu_sched_ops,
			     hw_jobs_limit, job_hang_limit,
			     msecs_to_jiffies(hang_limit_ms),
			     "v3d_tfu");
	if (ret) {
		dev_err(v3d->dev, "Failed to create TFU scheduler: %d.",
			ret);
		drm_sched_fini(&v3d->queue[V3D_RENDER].sched);
		drm_sched_fini(&v3d->queue[V3D_BIN].sched);
		return ret;
	}

	return 0;
}

void
v3d_sched_fini(struct v3d_dev *v3d)
{
	enum v3d_queue q;

	for (q = 0; q < V3D_MAX_QUEUES; q++)
		drm_sched_fini(&v3d->queue[q].sched);
}
