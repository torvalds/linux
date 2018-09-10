// SPDX-License-Identifier: GPL-2.0
/* Copyright 2019 Collabora ltd. */
#include <linux/devfreq.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>

#include "panfrost_device.h"
#include "panfrost_features.h"
#include "panfrost_issues.h"
#include "panfrost_gpu.h"
#include "panfrost_regs.h"

static void panfrost_devfreq_update_utilization(struct panfrost_device *pfdev, int slot);

static int panfrost_devfreq_target(struct device *dev, unsigned long *freq,
				   u32 flags)
{
	struct panfrost_device *pfdev = platform_get_drvdata(to_platform_device(dev));
	struct dev_pm_opp *opp;
	unsigned long old_clk_rate = pfdev->devfreq.cur_freq;
	unsigned long target_volt, target_rate;
	int err;

	opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(opp))
		return PTR_ERR(opp);

	target_rate = dev_pm_opp_get_freq(opp);
	target_volt = dev_pm_opp_get_voltage(opp);
	dev_pm_opp_put(opp);

	if (old_clk_rate == target_rate)
		return 0;

	/*
	 * If frequency scaling from low to high, adjust voltage first.
	 * If frequency scaling from high to low, adjust frequency first.
	 */
	if (old_clk_rate < target_rate) {
		err = regulator_set_voltage(pfdev->regulator, target_volt,
					    target_volt);
		if (err) {
			dev_err(dev, "Cannot set voltage %lu uV\n",
				target_volt);
			return err;
		}
	}

	err = clk_set_rate(pfdev->clock, target_rate);
	if (err) {
		dev_err(dev, "Cannot set frequency %lu (%d)\n", target_rate,
			err);
		regulator_set_voltage(pfdev->regulator, pfdev->devfreq.cur_volt,
				      pfdev->devfreq.cur_volt);
		return err;
	}

	if (old_clk_rate > target_rate) {
		err = regulator_set_voltage(pfdev->regulator, target_volt,
					    target_volt);
		if (err)
			dev_err(dev, "Cannot set voltage %lu uV\n", target_volt);
	}

	pfdev->devfreq.cur_freq = target_rate;
	pfdev->devfreq.cur_volt = target_volt;

	return 0;
}

static void panfrost_devfreq_reset(struct panfrost_device *pfdev)
{
	ktime_t now = ktime_get();
	int i;

	for (i = 0; i < NUM_JOB_SLOTS; i++) {
		pfdev->devfreq.slot[i].busy_time = 0;
		pfdev->devfreq.slot[i].idle_time = 0;
		pfdev->devfreq.slot[i].time_last_update = now;
	}
}

static int panfrost_devfreq_get_dev_status(struct device *dev,
					   struct devfreq_dev_status *status)
{
	struct panfrost_device *pfdev = platform_get_drvdata(to_platform_device(dev));
	int i;

	for (i = 0; i < NUM_JOB_SLOTS; i++) {
		panfrost_devfreq_update_utilization(pfdev, i);
	}

	status->current_frequency = clk_get_rate(pfdev->clock);
	status->total_time = ktime_to_ns(ktime_add(pfdev->devfreq.slot[0].busy_time,
						   pfdev->devfreq.slot[0].idle_time));

	status->busy_time = 0;
	for (i = 0; i < NUM_JOB_SLOTS; i++) {
		status->busy_time += ktime_to_ns(pfdev->devfreq.slot[i].busy_time);
	}

	/* We're scheduling only to one core atm, so don't divide for now */
	/* status->busy_time /= NUM_JOB_SLOTS; */

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

	*freq = pfdev->devfreq.cur_freq;

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

	if (!pfdev->regulator)
		return 0;

	ret = dev_pm_opp_of_add_table(&pfdev->pdev->dev);
	if (ret == -ENODEV) /* Optional, continue without devfreq */
		return 0;

	panfrost_devfreq_reset(pfdev);

	pfdev->devfreq.cur_freq = clk_get_rate(pfdev->clock);

	opp = devfreq_recommended_opp(&pfdev->pdev->dev, &pfdev->devfreq.cur_freq, 0);
	if (IS_ERR(opp))
		return PTR_ERR(opp);

	panfrost_devfreq_profile.initial_freq = pfdev->devfreq.cur_freq;
	dev_pm_opp_put(opp);

	pfdev->devfreq.devfreq = devm_devfreq_add_device(&pfdev->pdev->dev,
			&panfrost_devfreq_profile, "simple_ondemand", NULL);
	if (IS_ERR(pfdev->devfreq.devfreq)) {
		DRM_DEV_ERROR(&pfdev->pdev->dev, "Couldn't initialize GPU devfreq\n");
		ret = PTR_ERR(pfdev->devfreq.devfreq);
		pfdev->devfreq.devfreq = NULL;
		return ret;
	}

	return 0;
}

void panfrost_devfreq_resume(struct panfrost_device *pfdev)
{
	int i;

	if (!pfdev->devfreq.devfreq)
		return;

	panfrost_devfreq_reset(pfdev);
	for (i = 0; i < NUM_JOB_SLOTS; i++)
		pfdev->devfreq.slot[i].busy = false;

	devfreq_resume_device(pfdev->devfreq.devfreq);
}

void panfrost_devfreq_suspend(struct panfrost_device *pfdev)
{
	if (!pfdev->devfreq.devfreq)
		return;

	devfreq_suspend_device(pfdev->devfreq.devfreq);
}

static void panfrost_devfreq_update_utilization(struct panfrost_device *pfdev, int slot)
{
	struct panfrost_devfreq_slot *devfreq_slot = &pfdev->devfreq.slot[slot];
	ktime_t now;
	ktime_t last;

	if (!pfdev->devfreq.devfreq)
		return;

	now = ktime_get();
	last = pfdev->devfreq.slot[slot].time_last_update;

	/* If we last recorded a transition to busy, we have been idle since */
	if (devfreq_slot->busy)
		pfdev->devfreq.slot[slot].busy_time += ktime_sub(now, last);
	else
		pfdev->devfreq.slot[slot].idle_time += ktime_sub(now, last);

	pfdev->devfreq.slot[slot].time_last_update = now;
}

/* The job scheduler is expected to call this at every transition busy <-> idle */
void panfrost_devfreq_record_transition(struct panfrost_device *pfdev, int slot)
{
	struct panfrost_devfreq_slot *devfreq_slot = &pfdev->devfreq.slot[slot];

	panfrost_devfreq_update_utilization(pfdev, slot);
	devfreq_slot->busy = !devfreq_slot->busy;
}
