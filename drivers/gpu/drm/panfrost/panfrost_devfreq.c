// SPDX-License-Identifier: GPL-2.0
/* Copyright 2019 Collabora ltd. */
#include <linux/devfreq.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>

#include "panfrost_device.h"
#include "panfrost_devfreq.h"
#include "panfrost_features.h"
#include "panfrost_issues.h"
#include "panfrost_gpu.h"
#include "panfrost_regs.h"

static void panfrost_devfreq_update_utilization(struct panfrost_device *pfdev);

static int panfrost_devfreq_target(struct device *dev, unsigned long *freq,
				   u32 flags)
{
	struct panfrost_device *pfdev = dev_get_drvdata(dev);
	int err;

	err = dev_pm_opp_set_rate(dev, *freq);
	if (err)
		return err;

	*freq = clk_get_rate(pfdev->clock);

	return 0;
}

static void panfrost_devfreq_reset(struct panfrost_device *pfdev)
{
	pfdev->devfreq.busy_time = 0;
	pfdev->devfreq.idle_time = 0;
	pfdev->devfreq.time_last_update = ktime_get();
}

static int panfrost_devfreq_get_dev_status(struct device *dev,
					   struct devfreq_dev_status *status)
{
	struct panfrost_device *pfdev = dev_get_drvdata(dev);

	panfrost_devfreq_update_utilization(pfdev);

	status->current_frequency = clk_get_rate(pfdev->clock);
	status->total_time = ktime_to_ns(ktime_add(pfdev->devfreq.busy_time,
						   pfdev->devfreq.idle_time));

	status->busy_time = ktime_to_ns(pfdev->devfreq.busy_time);

	panfrost_devfreq_reset(pfdev);

	dev_dbg(pfdev->dev, "busy %lu total %lu %lu %% freq %lu MHz\n", status->busy_time,
		status->total_time,
		status->busy_time / (status->total_time / 100),
		status->current_frequency / 1000 / 1000);

	return 0;
}

static int panfrost_devfreq_get_cur_freq(struct device *dev, unsigned long *freq)
{
	struct panfrost_device *pfdev = platform_get_drvdata(to_platform_device(dev));

	*freq = clk_get_rate(pfdev->clock);

	return 0;
}

static struct devfreq_dev_profile panfrost_devfreq_profile = {
	.polling_ms = 50, /* ~3 frames */
	.target = panfrost_devfreq_target,
	.get_dev_status = panfrost_devfreq_get_dev_status,
	.get_cur_freq = panfrost_devfreq_get_cur_freq,
};

int panfrost_devfreq_init(struct panfrost_device *pfdev)
{
	int ret;
	struct dev_pm_opp *opp;
	unsigned long cur_freq;

	ret = dev_pm_opp_of_add_table(&pfdev->pdev->dev);
	if (ret == -ENODEV) /* Optional, continue without devfreq */
		return 0;
	else if (ret)
		return ret;

	panfrost_devfreq_reset(pfdev);

	cur_freq = clk_get_rate(pfdev->clock);

	opp = devfreq_recommended_opp(&pfdev->pdev->dev, &cur_freq, 0);
	if (IS_ERR(opp))
		return PTR_ERR(opp);

	panfrost_devfreq_profile.initial_freq = cur_freq;
	dev_pm_opp_put(opp);

	pfdev->devfreq.devfreq = devm_devfreq_add_device(&pfdev->pdev->dev,
			&panfrost_devfreq_profile, DEVFREQ_GOV_SIMPLE_ONDEMAND,
			NULL);
	if (IS_ERR(pfdev->devfreq.devfreq)) {
		DRM_DEV_ERROR(&pfdev->pdev->dev, "Couldn't initialize GPU devfreq\n");
		ret = PTR_ERR(pfdev->devfreq.devfreq);
		pfdev->devfreq.devfreq = NULL;
		dev_pm_opp_of_remove_table(&pfdev->pdev->dev);
		return ret;
	}

	return 0;
}

void panfrost_devfreq_fini(struct panfrost_device *pfdev)
{
	dev_pm_opp_of_remove_table(&pfdev->pdev->dev);
}

void panfrost_devfreq_resume(struct panfrost_device *pfdev)
{
	if (!pfdev->devfreq.devfreq)
		return;

	panfrost_devfreq_reset(pfdev);

	devfreq_resume_device(pfdev->devfreq.devfreq);
}

void panfrost_devfreq_suspend(struct panfrost_device *pfdev)
{
	if (!pfdev->devfreq.devfreq)
		return;

	devfreq_suspend_device(pfdev->devfreq.devfreq);
}

static void panfrost_devfreq_update_utilization(struct panfrost_device *pfdev)
{
	ktime_t now;
	ktime_t last;

	if (!pfdev->devfreq.devfreq)
		return;

	now = ktime_get();
	last = pfdev->devfreq.time_last_update;

	if (atomic_read(&pfdev->devfreq.busy_count) > 0)
		pfdev->devfreq.busy_time += ktime_sub(now, last);
	else
		pfdev->devfreq.idle_time += ktime_sub(now, last);

	pfdev->devfreq.time_last_update = now;
}

void panfrost_devfreq_record_busy(struct panfrost_device *pfdev)
{
	panfrost_devfreq_update_utilization(pfdev);
	atomic_inc(&pfdev->devfreq.busy_count);
}

void panfrost_devfreq_record_idle(struct panfrost_device *pfdev)
{
	int count;

	panfrost_devfreq_update_utilization(pfdev);
	count = atomic_dec_if_positive(&pfdev->devfreq.busy_count);
	WARN_ON(count < 0);
}
