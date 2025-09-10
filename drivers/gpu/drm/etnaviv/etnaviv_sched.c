// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017 Etnaviv Project
 */

#include <linux/moduleparam.h>

#include "etnaviv_drv.h"
#include "etnaviv_dump.h"
#include "etnaviv_gem.h"
#include "etnaviv_gpu.h"
#include "etnaviv_sched.h"
#include "state.xml.h"
#include "state_hi.xml.h"

static int etnaviv_job_hang_limit = 0;
module_param_named(job_hang_limit, etnaviv_job_hang_limit, int , 0444);
static int etnaviv_hw_jobs_limit = 4;
module_param_named(hw_job_limit, etnaviv_hw_jobs_limit, int , 0444);

static struct dma_fence *etnaviv_sched_run_job(struct drm_sched_job *sched_job)
{
	struct etnaviv_gem_submit *submit = to_etnaviv_submit(sched_job);
	struct dma_fence *fence = NULL;

	if (likely(!sched_job->s_fence->finished.error))
		fence = etnaviv_gpu_submit(submit);
	else
		dev_dbg(submit->gpu->dev, "skipping bad job\n");

	return fence;
}

static enum drm_gpu_sched_stat etnaviv_sched_timedout_job(struct drm_sched_job
							  *sched_job)
{
	struct etnaviv_gem_submit *submit = to_etnaviv_submit(sched_job);
	struct etnaviv_gpu *gpu = submit->gpu;
	u32 dma_addr, primid = 0;
	int change;

	/*
	 * If the GPU managed to complete this jobs fence, the timeout has
	 * fired before free-job worker. The timeout is spurious, so bail out.
	 */
	if (dma_fence_is_signaled(submit->out_fence))
		return DRM_GPU_SCHED_STAT_NO_HANG;

	/*
	 * If the GPU is still making forward progress on the front-end (which
	 * should never loop) we shift out the timeout to give it a chance to
	 * finish the job.
	 */
	dma_addr = gpu_read(gpu, VIVS_FE_DMA_ADDRESS);
	change = dma_addr - gpu->hangcheck_dma_addr;
	if (submit->exec_state == ETNA_PIPE_3D) {
		/* guard against concurrent usage from perfmon_sample */
		mutex_lock(&gpu->lock);
		gpu_write(gpu, VIVS_MC_PROFILE_CONFIG0,
			  VIVS_MC_PROFILE_CONFIG0_FE_CURRENT_PRIM <<
			  VIVS_MC_PROFILE_CONFIG0_FE__SHIFT);
		primid = gpu_read(gpu, VIVS_MC_PROFILE_FE_READ);
		mutex_unlock(&gpu->lock);
	}
	if (gpu->state == ETNA_GPU_STATE_RUNNING &&
	    (gpu->completed_fence != gpu->hangcheck_fence ||
	     change < 0 || change > 16 ||
	     (submit->exec_state == ETNA_PIPE_3D &&
	      gpu->hangcheck_primid != primid))) {
		gpu->hangcheck_dma_addr = dma_addr;
		gpu->hangcheck_primid = primid;
		gpu->hangcheck_fence = gpu->completed_fence;
		return DRM_GPU_SCHED_STAT_NO_HANG;
	}

	/* block scheduler */
	drm_sched_stop(&gpu->sched, sched_job);

	if(sched_job)
		drm_sched_increase_karma(sched_job);

	/* get the GPU back into the init state */
	etnaviv_core_dump(submit);
	etnaviv_gpu_recover_hang(submit);

	drm_sched_resubmit_jobs(&gpu->sched);

	drm_sched_start(&gpu->sched, 0);
	return DRM_GPU_SCHED_STAT_RESET;
}

static void etnaviv_sched_free_job(struct drm_sched_job *sched_job)
{
	struct etnaviv_gem_submit *submit = to_etnaviv_submit(sched_job);

	drm_sched_job_cleanup(sched_job);

	etnaviv_submit_put(submit);
}

static const struct drm_sched_backend_ops etnaviv_sched_ops = {
	.run_job = etnaviv_sched_run_job,
	.timedout_job = etnaviv_sched_timedout_job,
	.free_job = etnaviv_sched_free_job,
};

int etnaviv_sched_push_job(struct etnaviv_gem_submit *submit)
{
	struct etnaviv_gpu *gpu = submit->gpu;
	int ret;

	/*
	 * Hold the sched lock across the whole operation to avoid jobs being
	 * pushed out of order with regard to their sched fence seqnos as
	 * allocated in drm_sched_job_arm.
	 */
	mutex_lock(&gpu->sched_lock);

	drm_sched_job_arm(&submit->sched_job);

	submit->out_fence = dma_fence_get(&submit->sched_job.s_fence->finished);
	ret = xa_alloc_cyclic(&gpu->user_fences, &submit->out_fence_id,
			      submit->out_fence, xa_limit_32b,
			      &gpu->next_user_fence, GFP_KERNEL);
	if (ret < 0) {
		drm_sched_job_cleanup(&submit->sched_job);
		goto out_unlock;
	}

	/* the scheduler holds on to the job now */
	kref_get(&submit->refcount);

	drm_sched_entity_push_job(&submit->sched_job);

out_unlock:
	mutex_unlock(&gpu->sched_lock);

	return ret;
}

int etnaviv_sched_init(struct etnaviv_gpu *gpu)
{
	const struct drm_sched_init_args args = {
		.ops = &etnaviv_sched_ops,
		.num_rqs = DRM_SCHED_PRIORITY_COUNT,
		.credit_limit = etnaviv_hw_jobs_limit,
		.hang_limit = etnaviv_job_hang_limit,
		.timeout = msecs_to_jiffies(500),
		.name = dev_name(gpu->dev),
		.dev = gpu->dev,
	};

	return drm_sched_init(&gpu->sched, &args);
}

void etnaviv_sched_fini(struct etnaviv_gpu *gpu)
{
	drm_sched_fini(&gpu->sched);
}
