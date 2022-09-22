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

	if (gpu->funcs->gpu_set_freq) {
		mutex_lock(&df->lock);
		gpu->funcs->gpu_set_freq(gpu, opp, df->suspended);
		mutex_unlock(&df->lock);
	} else {
		clk_set_rate(gpu->core_clk, *freq);
	}

	dev_pm_opp_put(opp);

	return 0;
}

static unsigned long get_freq(struct msm_gpu *gpu)
{
	if (gpu->funcs->gpu_get_freq)
		return gpu->funcs->gpu_get_freq(gpu);

	return clk_get_rate(gpu->core_clk);
}

static void get_raw_dev_status(struct msm_gpu *gpu,
		struct devfreq_dev_status *status)
{
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
		return;
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
}

static void update_average_dev_status(struct msm_gpu *gpu,
		const struct devfreq_dev_status *raw)
{
	struct msm_gpu_devfreq *df = &gpu->devfreq;
	const u32 polling_ms = df->devfreq->profile->polling_ms;
	const u32 max_history_ms = polling_ms * 11 / 10;
	struct devfreq_dev_status *avg = &df->average_status;
	u64 avg_freq;

	/* simple_ondemand governor interacts poorly with gpu->clamp_to_idle.
	 * When we enforce the constraint on idle, it calls get_dev_status
	 * which would normally reset the stats.  When we remove the
	 * constraint on active, it calls get_dev_status again where busy_time
	 * would be 0.
	 *
	 * To remedy this, we always return the average load over the past
	 * polling_ms.
	 */

	/* raw is longer than polling_ms or avg has no history */
	if (div_u64(raw->total_time, USEC_PER_MSEC) >= polling_ms ||
	    !avg->total_time) {
		*avg = *raw;
		return;
	}

	/* Truncate the oldest history first.
	 *
	 * Because we keep the history with a single devfreq_dev_status,
	 * rather than a list of devfreq_dev_status, we have to assume freq
	 * and load are the same over avg->total_time.  We can scale down
	 * avg->busy_time and avg->total_time by the same factor to drop
	 * history.
	 */
	if (div_u64(avg->total_time + raw->total_time, USEC_PER_MSEC) >=
			max_history_ms) {
		const u32 new_total_time = polling_ms * USEC_PER_MSEC -
			raw->total_time;
		avg->busy_time = div_u64(
				mul_u32_u32(avg->busy_time, new_total_time),
				avg->total_time);
		avg->total_time = new_total_time;
	}

	/* compute the average freq over avg->total_time + raw->total_time */
	avg_freq = mul_u32_u32(avg->current_frequency, avg->total_time);
	avg_freq += mul_u32_u32(raw->current_frequency, raw->total_time);
	do_div(avg_freq, avg->total_time + raw->total_time);

	avg->current_frequency = avg_freq;
	avg->busy_time += raw->busy_time;
	avg->total_time += raw->total_time;
}

static int msm_devfreq_get_dev_status(struct device *dev,
		struct devfreq_dev_status *status)
{
	struct msm_gpu *gpu = dev_to_gpu(dev);
	struct devfreq_dev_status raw;

	get_raw_dev_status(gpu, &raw);
	update_average_dev_status(gpu, &raw);
	*status = gpu->devfreq.average_status;

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

	/* We need target support to do devfreq */
	if (!gpu->funcs->gpu_busy)
		return;

	mutex_init(&df->lock);

	dev_pm_qos_add_request(&gpu->pdev->dev, &df->idle_freq,
			       DEV_PM_QOS_MAX_FREQUENCY,
			       PM_QOS_MAX_FREQUENCY_DEFAULT_VALUE);
	dev_pm_qos_add_request(&gpu->pdev->dev, &df->boost_freq,
			       DEV_PM_QOS_MIN_FREQUENCY, 0);

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
		dev_pm_qos_remove_request(&df->idle_freq);
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
	dev_pm_qos_remove_request(&df->idle_freq);
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

	if (!has_devfreq(gpu))
		return;

	/*
	 * Cancel any pending transition to idle frequency:
	 */
	cancel_idle_work(df);

	idle_time = ktime_to_ms(ktime_sub(ktime_get(), df->idle_time));

	/*
	 * If we've been idle for a significant fraction of a polling
	 * interval, then we won't meet the threshold of busyness for
	 * the governor to ramp up the freq.. so give some boost
	 */
	if (idle_time > msm_devfreq_profile.polling_ms) {
		msm_devfreq_boost(gpu, 2);
	}

	dev_pm_qos_update_request(&df->idle_freq,
				  PM_QOS_MAX_FREQUENCY_DEFAULT_VALUE);
}


static void msm_devfreq_idle_work(struct kthread_work *work)
{
	struct msm_gpu_devfreq *df = container_of(work,
			struct msm_gpu_devfreq, idle_work.work);
	struct msm_gpu *gpu = container_of(df, struct msm_gpu, devfreq);

	df->idle_time = ktime_get();

	if (gpu->clamp_to_idle)
		dev_pm_qos_update_request(&df->idle_freq, 0);
}

void msm_devfreq_idle(struct msm_gpu *gpu)
{
	struct msm_gpu_devfreq *df = &gpu->devfreq;

	if (!has_devfreq(gpu))
		return;

	msm_hrtimer_queue_work(&df->idle_work, ms_to_ktime(1),
			       HRTIMER_MODE_REL);
}
