// SPDX-License-Identifier: GPL-2.0
/* Copyright 2019 Collabora ltd. */

#include <linux/clk.h>
#include <linux/devfreq.h>
#include <linux/devfreq_cooling.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>

#include "panfrost_device.h"
#include "panfrost_devfreq.h"

static void panfrost_devfreq_update_utilization(struct panfrost_devfreq *pfdevfreq)
{
	ktime_t now, last;

	now = ktime_get();
	last = pfdevfreq->time_last_update;

	if (pfdevfreq->busy_count > 0)
		pfdevfreq->busy_time += ktime_sub(now, last);
	else
		pfdevfreq->idle_time += ktime_sub(now, last);

	pfdevfreq->time_last_update = now;
}

static int panfrost_devfreq_target(struct device *dev, unsigned long *freq,
				   u32 flags)
{
	struct dev_pm_opp *opp;

	opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(opp))
		return PTR_ERR(opp);
	dev_pm_opp_put(opp);

	return dev_pm_opp_set_rate(dev, *freq);
}

static void panfrost_devfreq_reset(struct panfrost_devfreq *pfdevfreq)
{
	pfdevfreq->busy_time = 0;
	pfdevfreq->idle_time = 0;
	pfdevfreq->time_last_update = ktime_get();
}

static int panfrost_devfreq_get_dev_status(struct device *dev,
					   struct devfreq_dev_status *status)
{
	struct panfrost_device *pfdev = dev_get_drvdata(dev);
	struct panfrost_devfreq *pfdevfreq = &pfdev->pfdevfreq;
	unsigned long irqflags;

	status->current_frequency = clk_get_rate(pfdev->clock);

	spin_lock_irqsave(&pfdevfreq->lock, irqflags);

	panfrost_devfreq_update_utilization(pfdevfreq);

	status->total_time = ktime_to_ns(ktime_add(pfdevfreq->busy_time,
						   pfdevfreq->idle_time));

	status->busy_time = ktime_to_ns(pfdevfreq->busy_time);

	panfrost_devfreq_reset(pfdevfreq);

	spin_unlock_irqrestore(&pfdevfreq->lock, irqflags);

	dev_dbg(pfdev->dev, "busy %lu total %lu %lu %% freq %lu MHz\n",
		status->busy_time, status->total_time,
		status->busy_time / (status->total_time / 100),
		status->current_frequency / 1000 / 1000);

	return 0;
}

static struct devfreq_dev_profile panfrost_devfreq_profile = {
	.timer = DEVFREQ_TIMER_DELAYED,
	.polling_ms = 50, /* ~3 frames */
	.target = panfrost_devfreq_target,
	.get_dev_status = panfrost_devfreq_get_dev_status,
};

int panfrost_devfreq_init(struct panfrost_device *pfdev)
{
	int ret;
	struct dev_pm_opp *opp;
	unsigned long cur_freq;
	struct device *dev = &pfdev->pdev->dev;
	struct devfreq *devfreq;
	struct thermal_cooling_device *cooling;
	struct panfrost_devfreq *pfdevfreq = &pfdev->pfdevfreq;

	if (pfdev->comp->num_supplies > 1) {
		/*
		 * GPUs with more than 1 supply require platform-specific handling:
		 * continue without devfreq
		 */
		DRM_DEV_INFO(dev, "More than 1 supply is not supported yet\n");
		return 0;
	}

	ret = devm_pm_opp_set_regulators(dev, pfdev->comp->supply_names,
					 pfdev->comp->num_supplies);
	if (ret) {
		/* Continue if the optional regulator is missing */
		if (ret != -ENODEV) {
			if (ret != -EPROBE_DEFER)
				DRM_DEV_ERROR(dev, "Couldn't set OPP regulators\n");
			return ret;
		}
	}

	ret = devm_pm_opp_of_add_table(dev);
	if (ret) {
		/* Optional, continue without devfreq */
		if (ret == -ENODEV)
			ret = 0;
		return ret;
	}
	pfdevfreq->opp_of_table_added = true;

	spin_lock_init(&pfdevfreq->lock);

	panfrost_devfreq_reset(pfdevfreq);

	cur_freq = clk_get_rate(pfdev->clock);

	opp = devfreq_recommended_opp(dev, &cur_freq, 0);
	if (IS_ERR(opp))
		return PTR_ERR(opp);

	panfrost_devfreq_profile.initial_freq = cur_freq;

	/*
	 * Set the recommend OPP this will enable and configure the regulator
	 * if any and will avoid a switch off by regulator_late_cleanup()
	 */
	ret = dev_pm_opp_set_opp(dev, opp);
	if (ret) {
		DRM_DEV_ERROR(dev, "Couldn't set recommended OPP\n");
		return ret;
	}

	dev_pm_opp_put(opp);

	/*
	 * Setup default thresholds for the simple_ondemand governor.
	 * The values are chosen based on experiments.
	 */
	pfdevfreq->gov_data.upthreshold = 45;
	pfdevfreq->gov_data.downdifferential = 5;

	devfreq = devm_devfreq_add_device(dev, &panfrost_devfreq_profile,
					  DEVFREQ_GOV_SIMPLE_ONDEMAND,
					  &pfdevfreq->gov_data);
	if (IS_ERR(devfreq)) {
		DRM_DEV_ERROR(dev, "Couldn't initialize GPU devfreq\n");
		return PTR_ERR(devfreq);
	}
	pfdevfreq->devfreq = devfreq;

	cooling = devfreq_cooling_em_register(devfreq, NULL);
	if (IS_ERR(cooling))
		DRM_DEV_INFO(dev, "Failed to register cooling device\n");
	else
		pfdevfreq->cooling = cooling;

	return 0;
}

void panfrost_devfreq_fini(struct panfrost_device *pfdev)
{
	struct panfrost_devfreq *pfdevfreq = &pfdev->pfdevfreq;

	if (pfdevfreq->cooling) {
		devfreq_cooling_unregister(pfdevfreq->cooling);
		pfdevfreq->cooling = NULL;
	}
}

void panfrost_devfreq_resume(struct panfrost_device *pfdev)
{
	struct panfrost_devfreq *pfdevfreq = &pfdev->pfdevfreq;

	if (!pfdevfreq->devfreq)
		return;

	panfrost_devfreq_reset(pfdevfreq);

	devfreq_resume_device(pfdevfreq->devfreq);
}

void panfrost_devfreq_suspend(struct panfrost_device *pfdev)
{
	struct panfrost_devfreq *pfdevfreq = &pfdev->pfdevfreq;

	if (!pfdevfreq->devfreq)
		return;

	devfreq_suspend_device(pfdevfreq->devfreq);
}

void panfrost_devfreq_record_busy(struct panfrost_devfreq *pfdevfreq)
{
	unsigned long irqflags;

	if (!pfdevfreq->devfreq)
		return;

	spin_lock_irqsave(&pfdevfreq->lock, irqflags);

	panfrost_devfreq_update_utilization(pfdevfreq);

	pfdevfreq->busy_count++;

	spin_unlock_irqrestore(&pfdevfreq->lock, irqflags);
}

void panfrost_devfreq_record_idle(struct panfrost_devfreq *pfdevfreq)
{
	unsigned long irqflags;

	if (!pfdevfreq->devfreq)
		return;

	spin_lock_irqsave(&pfdevfreq->lock, irqflags);

	panfrost_devfreq_update_utilization(pfdevfreq);

	WARN_ON(--pfdevfreq->busy_count < 0);

	spin_unlock_irqrestore(&pfdevfreq->lock, irqflags);
}
