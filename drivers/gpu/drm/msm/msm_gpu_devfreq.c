// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include "msm_gpu.h"
#include "msm_gpu_trace.h"

#include <linux/devfreq.h>
#include <linux/devfreq_cooling.h>
#include <linux/math64.h>
#include <linux/units.h>

/*
 * Power Management:
 */

static int msm_devfreq_target(struct device *dev, unsigned long *freq,
		u32 flags)
{
	struct msm_gpu *gpu = dev_to_gpu(dev);
	struct msm_gpu_devfreq *df = &gpu->devfreq;
	struct dev_pm_opp *opp;

	/*
	 * Note that devfreq_recommended_opp() can modify the freq
	 * to something that actually is in the opp table:
	 */
	opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(opp))
		return PTR_ERR(opp);

	trace_msm_gpu_freq_change(dev_pm_opp_get_freq(opp));

	/*
	 * If the GPU is idle, devfreq is not aware, so just stash
	 * the new target freq (to use when we return to active)
	 */
	if (df->idle_freq) {
		df->idle_freq = *freq;
		dev_pm_opp_put(opp);
		return 0;
	}

	if (gpu->funcs->gpu_set_freq) {
		mutex_lock(&df->lock);
		gpu->funcs->gpu_set_freq(gpu, opp, df->suspended);
		mutex_unlock(&df->lock);
	} else {
		dev_pm_opp_set_rate(dev, *freq);
	}

	dev_pm_opp_put(opp);

	return 0;
}

static unsigned long get_freq(struct msm_gpu *gpu)
{
	struct msm_gpu_devfreq *df = &gpu->devfreq;

	/*
	 * If the GPU is idle, use the shadow/saved freq to avoid
	 * confusing devfreq (which is unaware that we are switching
	 * to lowest freq until the device is active again)
	 */
	if (df->idle_freq)
		return df->idle_freq;

	if (gpu->funcs->gpu_get_freq)
		return gpu->funcs->gpu_get_freq(gpu);

	return clk_get_rate(gpu->core_clk);
}

static int msm_devfreq_get_dev_status(struct device *dev,
		struct devfreq_dev_status *status)
{
	struct msm_gpu *gpu = dev_to_gpu(dev);
	struct msm_gpu_devfreq *df = &gpu->devfreq;
	u64 busy_cycles, busy_time;
	unsigned long sample_rate;
	ktime_t time;

	mutex_lock(&df->lock);

	status->current_frequency = get_freq(gpu);
	time = ktime_get();
	status->total_time = ktime_us_delta(time, df->time);
	df->time = time;

	if (df->suspended) {
		mutex_unlock(&df->lock);
		status->busy_time = 0;
		return 0;
	}

	busy_cycles = gpu->funcs->gpu_busy(gpu, &sample_rate);
	busy_time = busy_cycles - df->busy_cycles;
	df->busy_cycles = busy_cycles;

	mutex_unlock(&df->lock);

	busy_time *= USEC_PER_SEC;
	busy_time = div64_ul(busy_time, sample_rate);
	if (WARN_ON(busy_time > ~0LU))
		busy_time = ~0LU;

	status->busy_time = busy_time;

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

static void msm_devfreq_boost_work(struct kthread_work *work);
static void msm_devfreq_idle_work(struct kthread_work *work);

static bool has_devfreq(struct msm_gpu *gpu)
{
	struct msm_gpu_devfreq *df = &gpu->devfreq;
	return !!df->devfreq;
}

void msm_devfreq_init(struct msm_gpu *gpu)
{
	struct msm_gpu_devfreq *df = &gpu->devfreq;
	struct msm_drm_private *priv = gpu->dev->dev_private;
	int ret;

	/* We need target support to do devfreq */
	if (!gpu->funcs->gpu_busy)
		return;

	/*
	 * Setup default values for simple_ondemand governor tuning.  We
	 * want to throttle up at 50% load for the double-buffer case,
	 * where due to stalling waiting for vblank we could get stuck
	 * at (for ex) 30fps at 50% utilization.
	 */
	priv->gpu_devfreq_config.upthreshold = 50;
	priv->gpu_devfreq_config.downdifferential = 10;

	mutex_init(&df->lock);
	df->suspended = true;

	ret = dev_pm_qos_add_request(&gpu->pdev->dev, &df->boost_freq,
				     DEV_PM_QOS_MIN_FREQUENCY, 0);
	if (ret < 0) {
		DRM_DEV_ERROR(&gpu->pdev->dev, "Couldn't initialize QoS\n");
		return;
	}

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
			&priv->gpu_devfreq_config);

	if (IS_ERR(df->devfreq)) {
		DRM_DEV_ERROR(&gpu->pdev->dev, "Couldn't initialize GPU devfreq\n");
		dev_pm_qos_remove_request(&df->boost_freq);
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

	msm_hrtimer_work_init(&df->boost_work, gpu->worker, msm_devfreq_boost_work,
			      CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	msm_hrtimer_work_init(&df->idle_work, gpu->worker, msm_devfreq_idle_work,
			      CLOCK_MONOTONIC, HRTIMER_MODE_REL);
}

static void cancel_idle_work(struct msm_gpu_devfreq *df)
{
	hrtimer_cancel(&df->idle_work.timer);
	kthread_cancel_work_sync(&df->idle_work.work);
}

static void cancel_boost_work(struct msm_gpu_devfreq *df)
{
	hrtimer_cancel(&df->boost_work.timer);
	kthread_cancel_work_sync(&df->boost_work.work);
}

void msm_devfreq_cleanup(struct msm_gpu *gpu)
{
	struct msm_gpu_devfreq *df = &gpu->devfreq;

	if (!has_devfreq(gpu))
		return;

	devfreq_cooling_unregister(gpu->cooling);
	dev_pm_qos_remove_request(&df->boost_freq);
}

void msm_devfreq_resume(struct msm_gpu *gpu)
{
	struct msm_gpu_devfreq *df = &gpu->devfreq;
	unsigned long sample_rate;

	if (!has_devfreq(gpu))
		return;

	mutex_lock(&df->lock);
	df->busy_cycles = gpu->funcs->gpu_busy(gpu, &sample_rate);
	df->time = ktime_get();
	df->suspended = false;
	mutex_unlock(&df->lock);

	devfreq_resume_device(df->devfreq);
}

void msm_devfreq_suspend(struct msm_gpu *gpu)
{
	struct msm_gpu_devfreq *df = &gpu->devfreq;

	if (!has_devfreq(gpu))
		return;

	mutex_lock(&df->lock);
	df->suspended = true;
	mutex_unlock(&df->lock);

	devfreq_suspend_device(df->devfreq);

	cancel_idle_work(df);
	cancel_boost_work(df);
}

static void msm_devfreq_boost_work(struct kthread_work *work)
{
	struct msm_gpu_devfreq *df = container_of(work,
			struct msm_gpu_devfreq, boost_work.work);

	dev_pm_qos_update_request(&df->boost_freq, 0);
}

void msm_devfreq_boost(struct msm_gpu *gpu, unsigned factor)
{
	struct msm_gpu_devfreq *df = &gpu->devfreq;
	uint64_t freq;

	if (!has_devfreq(gpu))
		return;

	freq = get_freq(gpu);
	freq *= factor;

	/*
	 * A nice little trap is that PM QoS operates in terms of KHz,
	 * while devfreq operates in terms of Hz:
	 */
	do_div(freq, HZ_PER_KHZ);

	dev_pm_qos_update_request(&df->boost_freq, freq);

	msm_hrtimer_queue_work(&df->boost_work,
			       ms_to_ktime(msm_devfreq_profile.polling_ms),
			       HRTIMER_MODE_REL);
}

void msm_devfreq_active(struct msm_gpu *gpu)
{
	struct msm_gpu_devfreq *df = &gpu->devfreq;
	unsigned int idle_time;
	unsigned long target_freq;

	if (!has_devfreq(gpu))
		return;

	/*
	 * Cancel any pending transition to idle frequency:
	 */
	cancel_idle_work(df);

	/*
	 * Hold devfreq lock to synchronize with get_dev_status()/
	 * target() callbacks
	 */
	mutex_lock(&df->devfreq->lock);

	target_freq = df->idle_freq;

	idle_time = ktime_to_ms(ktime_sub(ktime_get(), df->idle_time));

	df->idle_freq = 0;

	/*
	 * We could have become active again before the idle work had a
	 * chance to run, in which case the df->idle_freq would have
	 * still been zero.  In this case, no need to change freq.
	 */
	if (target_freq)
		msm_devfreq_target(&gpu->pdev->dev, &target_freq, 0);

	mutex_unlock(&df->devfreq->lock);

	/*
	 * If we've been idle for a significant fraction of a polling
	 * interval, then we won't meet the threshold of busyness for
	 * the governor to ramp up the freq.. so give some boost
	 */
	if (idle_time > msm_devfreq_profile.polling_ms) {
		msm_devfreq_boost(gpu, 2);
	}
}


static void msm_devfreq_idle_work(struct kthread_work *work)
{
	struct msm_gpu_devfreq *df = container_of(work,
			struct msm_gpu_devfreq, idle_work.work);
	struct msm_gpu *gpu = container_of(df, struct msm_gpu, devfreq);
	struct msm_drm_private *priv = gpu->dev->dev_private;
	unsigned long idle_freq, target_freq = 0;

	/*
	 * Hold devfreq lock to synchronize with get_dev_status()/
	 * target() callbacks
	 */
	mutex_lock(&df->devfreq->lock);

	idle_freq = get_freq(gpu);

	if (priv->gpu_clamp_to_idle)
		msm_devfreq_target(&gpu->pdev->dev, &target_freq, 0);

	df->idle_time = ktime_get();
	df->idle_freq = idle_freq;

	mutex_unlock(&df->devfreq->lock);
}

void msm_devfreq_idle(struct msm_gpu *gpu)
{
	struct msm_gpu_devfreq *df = &gpu->devfreq;

	if (!has_devfreq(gpu))
		return;

	msm_hrtimer_queue_work(&df->idle_work, ms_to_ktime(1),
			       HRTIMER_MODE_REL);
}
