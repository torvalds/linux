// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include "msm_gpu.h"
#include "msm_gpu_trace.h"

#include <linux/devfreq.h>
#include <linux/devfreq_cooling.h>

/*
 * Power Management:
 */

static int msm_devfreq_target(struct device *dev, unsigned long *freq,
		u32 flags)
{
	struct msm_gpu *gpu = dev_to_gpu(dev);
	struct dev_pm_opp *opp;

	/*
	 * Note that devfreq_recommended_opp() can modify the freq
	 * to something that actually is in the opp table:
	 */
	opp = devfreq_recommended_opp(dev, freq, flags);

	/*
	 * If the GPU is idle, devfreq is not aware, so just ignore
	 * it's requests
	 */
	if (gpu->devfreq.idle_freq) {
		gpu->devfreq.idle_freq = *freq;
		dev_pm_opp_put(opp);
		return 0;
	}

	if (IS_ERR(opp))
		return PTR_ERR(opp);

	trace_msm_gpu_freq_change(dev_pm_opp_get_freq(opp));

	if (gpu->funcs->gpu_set_freq)
		gpu->funcs->gpu_set_freq(gpu, opp);
	else
		clk_set_rate(gpu->core_clk, *freq);

	dev_pm_opp_put(opp);

	return 0;
}

static unsigned long get_freq(struct msm_gpu *gpu)
{
	if (gpu->devfreq.idle_freq)
		return gpu->devfreq.idle_freq;

	if (gpu->funcs->gpu_get_freq)
		return gpu->funcs->gpu_get_freq(gpu);

	return clk_get_rate(gpu->core_clk);
}

static int msm_devfreq_get_dev_status(struct device *dev,
		struct devfreq_dev_status *status)
{
	struct msm_gpu *gpu = dev_to_gpu(dev);
	ktime_t time;

	status->current_frequency = get_freq(gpu);
	status->busy_time = gpu->funcs->gpu_busy(gpu);

	time = ktime_get();
	status->total_time = ktime_us_delta(time, gpu->devfreq.time);
	gpu->devfreq.time = time;

	return 0;
}

static int msm_devfreq_get_cur_freq(struct device *dev, unsigned long *freq)
{
	*freq = get_freq(dev_to_gpu(dev));

	return 0;
}

static struct devfreq_dev_profile msm_devfreq_profile = {
	.timer = DEVFREQ_TIMER_DELAYED,
	.polling_ms = 50,
	.target = msm_devfreq_target,
	.get_dev_status = msm_devfreq_get_dev_status,
	.get_cur_freq = msm_devfreq_get_cur_freq,
};

static void msm_devfreq_idle_work(struct kthread_work *work);

void msm_devfreq_init(struct msm_gpu *gpu)
{
	struct msm_gpu_devfreq *df = &gpu->devfreq;

	/* We need target support to do devfreq */
	if (!gpu->funcs->gpu_busy)
		return;

	msm_devfreq_profile.initial_freq = gpu->fast_rate;

	/*
	 * Don't set the freq_table or max_state and let devfreq build the table
	 * from OPP
	 * After a deferred probe, these may have be left to non-zero values,
	 * so set them back to zero before creating the devfreq device
	 */
	msm_devfreq_profile.freq_table = NULL;
	msm_devfreq_profile.max_state = 0;

	df->devfreq = devm_devfreq_add_device(&gpu->pdev->dev,
			&msm_devfreq_profile, DEVFREQ_GOV_SIMPLE_ONDEMAND,
			NULL);

	if (IS_ERR(df->devfreq)) {
		DRM_DEV_ERROR(&gpu->pdev->dev, "Couldn't initialize GPU devfreq\n");
		df->devfreq = NULL;
		return;
	}

	devfreq_suspend_device(df->devfreq);

	gpu->cooling = of_devfreq_cooling_register(gpu->pdev->dev.of_node, df->devfreq);
	if (IS_ERR(gpu->cooling)) {
		DRM_DEV_ERROR(&gpu->pdev->dev,
				"Couldn't register GPU cooling device\n");
		gpu->cooling = NULL;
	}

	msm_hrtimer_work_init(&df->idle_work, gpu->worker, msm_devfreq_idle_work,
			      CLOCK_MONOTONIC, HRTIMER_MODE_REL);
}

void msm_devfreq_cleanup(struct msm_gpu *gpu)
{
	devfreq_cooling_unregister(gpu->cooling);
}

void msm_devfreq_resume(struct msm_gpu *gpu)
{
	gpu->devfreq.busy_cycles = 0;
	gpu->devfreq.time = ktime_get();

	devfreq_resume_device(gpu->devfreq.devfreq);
}

void msm_devfreq_suspend(struct msm_gpu *gpu)
{
	devfreq_suspend_device(gpu->devfreq.devfreq);
}

void msm_devfreq_active(struct msm_gpu *gpu)
{
	struct msm_gpu_devfreq *df = &gpu->devfreq;
	struct devfreq_dev_status status;
	unsigned int idle_time;
	unsigned long target_freq = df->idle_freq;

	if (!df->devfreq)
		return;

	/*
	 * Cancel any pending transition to idle frequency:
	 */
	hrtimer_cancel(&df->idle_work.timer);

	/*
	 * Hold devfreq lock to synchronize with get_dev_status()/
	 * target() callbacks
	 */
	mutex_lock(&df->devfreq->lock);

	idle_time = ktime_to_ms(ktime_sub(ktime_get(), df->idle_time));

	/*
	 * If we've been idle for a significant fraction of a polling
	 * interval, then we won't meet the threshold of busyness for
	 * the governor to ramp up the freq.. so give some boost
	 */
	if (idle_time > msm_devfreq_profile.polling_ms/2) {
		target_freq *= 2;
	}

	df->idle_freq = 0;

	msm_devfreq_target(&gpu->pdev->dev, &target_freq, 0);

	/*
	 * Reset the polling interval so we aren't inconsistent
	 * about freq vs busy/total cycles
	 */
	msm_devfreq_get_dev_status(&gpu->pdev->dev, &status);

	mutex_unlock(&df->devfreq->lock);
}


static void msm_devfreq_idle_work(struct kthread_work *work)
{
	struct msm_gpu_devfreq *df = container_of(work,
			struct msm_gpu_devfreq, idle_work.work);
	struct msm_gpu *gpu = container_of(df, struct msm_gpu, devfreq);
	unsigned long idle_freq, target_freq = 0;

	/*
	 * Hold devfreq lock to synchronize with get_dev_status()/
	 * target() callbacks
	 */
	mutex_lock(&df->devfreq->lock);

	idle_freq = get_freq(gpu);

	if (gpu->clamp_to_idle)
		msm_devfreq_target(&gpu->pdev->dev, &target_freq, 0);

	df->idle_time = ktime_get();
	df->idle_freq = idle_freq;

	mutex_unlock(&df->devfreq->lock);
}

void msm_devfreq_idle(struct msm_gpu *gpu)
{
	struct msm_gpu_devfreq *df = &gpu->devfreq;

	if (!df->devfreq)
		return;

	msm_hrtimer_queue_work(&df->idle_work, ms_to_ktime(1),
			       HRTIMER_MODE_REL);
}
