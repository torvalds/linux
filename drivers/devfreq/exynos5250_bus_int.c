/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS - INT clock frequency scaling support in DEVFREQ framework
 *	This version supports EXYNOS5250 only. This changes bus frequencies.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/io.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/suspend.h>
#include <linux/opp.h>
#include <linux/clk.h>
#include <linux/devfreq.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/module.h>
#include <linux/pm_qos.h>

#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/asv-exynos.h>
#include <mach/exynos5_bus.h>

#include "exynos_ppmu.h"
#include "exynos5_ppmu.h"
#include "governor.h"

#define MAX_SAFEVOLT	1100000 /* 1.10V */

/* Assume that the bus for mif is saturated if the utilization is 23% */
#define INT_BUS_SATURATION_RATIO	23
#define EXYNOS5_BUS_INT_POLL_TIME	msecs_to_jiffies(100)

enum int_level_idx {
	LV_0,
	LV_1,
	LV_2,
	LV_3,
	LV_4,
	_LV_END
};

struct busfreq_data_int {
	struct device *dev;
	struct devfreq *devfreq;
	bool disabled;
	struct regulator *vdd_int;
	unsigned long curr_freq;
	unsigned long curr_volt;
	unsigned long suspend_freq;

	struct mutex lock;
	struct pm_qos_request mif_req;

	struct clk *int_clk;

	struct exynos5_ppmu_handle *ppmu;
	struct delayed_work work;
	int busy;
};

struct int_bus_opp_table {
	unsigned int idx;
	unsigned long clk;
	unsigned long volt;
};

static struct int_bus_opp_table exynos5_int_opp_table[] = {
	{LV_0, 266000, 1050000},
	{LV_1, 200000, 1050000},
	{LV_2, 160000, 1050000},
	{LV_3, 133000, 1050000},
	{LV_4, 100000, 1050000},
	{0, 0, 0},
};

struct exynos5_bus_int_handle {
	struct list_head node;
	struct delayed_work work;
	bool boost;
	bool poll;
	unsigned long min;
};

static struct busfreq_data_int *exynos5_bus_int_data;
static DEFINE_MUTEX(exynos5_bus_int_data_lock);
static LIST_HEAD(exynos5_bus_int_requests);
static DEFINE_MUTEX(exynos5_bus_int_requests_lock);

static int exynos5_int_setvolt(struct busfreq_data_int *data,
		unsigned long volt)
{
	return regulator_set_voltage(data->vdd_int, volt, MAX_SAFEVOLT);
}

static int exynos5_busfreq_int_target(struct device *dev, unsigned long *_freq,
			      u32 flags)
{
	int err = 0;
	struct platform_device *pdev = container_of(dev, struct platform_device,
						    dev);
	struct busfreq_data_int *data = platform_get_drvdata(pdev);
	struct opp *opp;
	unsigned long old_freq, freq;
	unsigned long volt;
	struct exynos5_bus_int_handle *handle;

	mutex_lock(&exynos5_bus_int_requests_lock);
	list_for_each_entry(handle, &exynos5_bus_int_requests, node) {
		if (handle->boost) {
			*_freq = ULONG_MAX;
			flags |= DEVFREQ_FLAG_LEAST_UPPER_BOUND;
			break;
		}
		if (handle->min > *_freq) {
			*_freq = handle->min;
			flags &= ~DEVFREQ_FLAG_LEAST_UPPER_BOUND;
		}
	}
	mutex_unlock(&exynos5_bus_int_requests_lock);

	rcu_read_lock();
	opp = devfreq_recommended_opp(dev, _freq, flags);
	if (IS_ERR(opp)) {
		rcu_read_unlock();
		dev_err(dev, "%s: Invalid OPP.\n", __func__);
		return PTR_ERR(opp);
	}

	freq = opp_get_freq(opp);
	volt = opp_get_voltage(opp);
	rcu_read_unlock();

	mutex_lock(&data->lock);

	old_freq = data->curr_freq;

	if (old_freq == freq)
		goto out;

	dev_dbg(dev, "targetting %lukHz %luuV\n", freq, volt);

	if (data->disabled)
		goto out;

	if (freq > exynos5_int_opp_table[_LV_END - 1].clk)
		pm_qos_update_request(&data->mif_req,
				data->busy * old_freq * 16 / 100000);
	else
		pm_qos_update_request(&data->mif_req, -1);

	if (old_freq < freq)
		err = exynos5_int_setvolt(data, volt);
	if (err)
		goto out;

	err = clk_set_rate(data->int_clk, freq * 1000);

	if (err)
		goto out;

	if (old_freq > freq)
		err = exynos5_int_setvolt(data, volt);
	if (err)
		goto out;

	data->curr_freq = freq;
out:
	mutex_unlock(&data->lock);
	return err;
}

static int exynos5_int_get_dev_status(struct device *dev,
				      struct devfreq_dev_status *stat)
{
	struct platform_device *pdev = container_of(dev, struct platform_device,
						    dev);
	struct busfreq_data_int *data = platform_get_drvdata(pdev);

	stat->current_frequency = data->curr_freq;

	stat->busy_time = data->busy;
	stat->total_time = 100;

	return 0;
}

static void exynos5_int_poll_start(struct busfreq_data_int *data)
{
	struct exynos5_bus_int_handle *handle;

	mutex_lock(&exynos5_bus_int_requests_lock);
	list_for_each_entry(handle, &exynos5_bus_int_requests, node) {
		if (handle->poll) {
			schedule_delayed_work(&data->work,
					EXYNOS5_BUS_INT_POLL_TIME);
			break;
		}
	}
	mutex_unlock(&exynos5_bus_int_requests_lock);
}

static void exynos5_int_poll_stop(struct busfreq_data_int *data)
{
	cancel_delayed_work_sync(&data->work);
}

static void exynos5_int_trigger_poll(struct busfreq_data_int *data)
{
	cancel_delayed_work_sync(&data->work);
	schedule_delayed_work(&data->work, 0);
}

static void exynos5_int_update(struct busfreq_data_int *data)
{
	struct exynos5_bus_int_handle *handle;
	int ret = 0;
	bool poll = false;

	mutex_lock(&exynos5_bus_int_requests_lock);
	list_for_each_entry(handle, &exynos5_bus_int_requests, node) {
		if (handle->poll) {
			poll = true;
			break;
		}
	}
	mutex_unlock(&exynos5_bus_int_requests_lock);

	ret = exynos5_ppmu_get_busy(data->ppmu, PPMU_SET_RIGHT);
	if (ret >= 0 && poll)
		data->busy = ret;
	else
		data->busy = 0;

	if (ret >= 0 || !poll) {
		mutex_lock(&data->devfreq->lock);
		update_devfreq(data->devfreq);
		mutex_unlock(&data->devfreq->lock);
	}
}

static void exynos5_int_poll_work(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct busfreq_data_int *data;

	dwork = to_delayed_work(work);
	data = container_of(dwork, struct busfreq_data_int, work);

	exynos5_int_update(data);

	exynos5_int_poll_start(data);
}

static void exynos5_int_cancel_boost(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct exynos5_bus_int_handle *handle;

	handle = container_of(dwork, struct exynos5_bus_int_handle, work);

	handle->boost = false;
}

struct exynos5_bus_int_handle *exynos5_bus_int_get(unsigned long min_freq,
		bool poll)
{
	struct exynos5_bus_int_handle *handle;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return NULL;

	handle->min = min_freq;
	/* If polling, boost the frequency for the first poll cycle */
	handle->boost = poll;
	handle->poll = poll;
	INIT_DELAYED_WORK_DEFERRABLE(&handle->work, exynos5_int_cancel_boost);

	mutex_lock(&exynos5_bus_int_requests_lock);
	list_add_tail(&handle->node, &exynos5_bus_int_requests);
	mutex_unlock(&exynos5_bus_int_requests_lock);

	mutex_lock(&exynos5_bus_int_data_lock);

	if (exynos5_bus_int_data) {
		cancel_delayed_work_sync(&exynos5_bus_int_data->work);
		exynos5_int_update(exynos5_bus_int_data);
		exynos5_int_poll_start(exynos5_bus_int_data);
	}

	mutex_unlock(&exynos5_bus_int_data_lock);

	if (handle->boost)
		schedule_delayed_work(&handle->work, EXYNOS5_BUS_INT_POLL_TIME);

	return handle;
}

int exynos5_bus_int_put(struct exynos5_bus_int_handle *handle)
{
	mutex_lock(&exynos5_bus_int_requests_lock);
	list_del(&handle->node);
	mutex_unlock(&exynos5_bus_int_requests_lock);

	mutex_lock(&exynos5_bus_int_data_lock);
	if (exynos5_bus_int_data)
		exynos5_int_trigger_poll(exynos5_bus_int_data);
	mutex_unlock(&exynos5_bus_int_data_lock);

	cancel_delayed_work_sync(&handle->work);
	kfree(handle);
	return 0;
}

static void exynos5_int_exit(struct device *dev)
{
	struct platform_device *pdev = container_of(dev, struct platform_device,
						    dev);
	struct busfreq_data_int *data = platform_get_drvdata(pdev);

	devfreq_unregister_opp_notifier(dev, data->devfreq);
}

static struct devfreq_dev_profile exynos5_devfreq_int_profile = {
	.initial_freq		= 160000,
	.polling_ms		= 0,
	.target			= exynos5_busfreq_int_target,
	.get_dev_status		= exynos5_int_get_dev_status,
	.exit			= exynos5_int_exit,
};

static int exynos5250_init_int_tables(struct busfreq_data_int *data)
{
	int i, err = 0;

	for (i = LV_0; i < _LV_END; i++) {
		exynos5_int_opp_table[i].volt = asv_get_volt(ID_INT, exynos5_int_opp_table[i].clk);
		if (exynos5_int_opp_table[i].volt == 0) {
			dev_err(data->dev, "Invalid value\n");
			return -EINVAL;
		}
	}

	for (i = LV_0; i < _LV_END; i++) {
		err = opp_add(data->dev, exynos5_int_opp_table[i].clk,
				exynos5_int_opp_table[i].volt);
		if (err) {
			dev_err(data->dev, "Cannot add opp entries.\n");
			return err;
		}
	}

	return 0;
}
static struct devfreq_simple_ondemand_data exynos5_int_ondemand_data = {
	.downdifferential = 2,
	.upthreshold = INT_BUS_SATURATION_RATIO,
};

static __devinit int exynos5_busfreq_int_probe(struct platform_device *pdev)
{
	struct busfreq_data_int *data;
	struct opp *opp;
	struct device *dev = &pdev->dev;
	unsigned long initial_freq;
	unsigned long initial_volt;
	int err = 0;

	data = devm_kzalloc(&pdev->dev, sizeof(struct busfreq_data_int), GFP_KERNEL);
	if (data == NULL) {
		dev_err(dev, "Cannot allocate memory.\n");
		return -ENOMEM;
	}

	data->dev = dev;
	mutex_init(&data->lock);

	err = exynos5250_init_int_tables(data);
	if (err)
		goto err_regulator;

	data->vdd_int = regulator_get(dev, "vdd_int");
	if (IS_ERR(data->vdd_int)) {
		dev_err(dev, "Cannot get the regulator \"vdd_int\"\n");
		err = PTR_ERR(data->vdd_int);
		goto err_regulator;
	}

	data->int_clk = clk_get(dev, "int_clk");
	if (IS_ERR(data->int_clk)) {
		dev_err(dev, "Cannot get clock \"int_clk\"\n");
		err = PTR_ERR(data->int_clk);
		goto err_clock;
	}

	rcu_read_lock();
	opp = opp_find_freq_floor(dev, &exynos5_devfreq_int_profile.initial_freq);
	if (IS_ERR(opp)) {
		rcu_read_unlock();
		dev_err(dev, "Invalid initial frequency %lu kHz.\n",
		       exynos5_devfreq_int_profile.initial_freq);
		err = PTR_ERR(opp);
		goto err_opp_add;
	}
	initial_freq = opp_get_freq(opp);
	initial_volt = opp_get_voltage(opp);
	rcu_read_unlock();
	data->curr_freq = initial_freq;
	data->curr_volt = initial_volt;

	err = clk_set_rate(data->int_clk, initial_freq * 1000);
	if (err) {
		dev_err(dev, "Failed to set initial frequency\n");
		goto err_opp_add;
	}

	err = exynos5_int_setvolt(data, initial_volt);
	if (err)
		goto err_opp_add;

	platform_set_drvdata(pdev, data);

	data->ppmu = exynos5_ppmu_get();
	if (!data->ppmu)
		goto err_ppmu_get;

	INIT_DELAYED_WORK_DEFERRABLE(&data->work, exynos5_int_poll_work);

	data->devfreq = devfreq_add_device(dev, &exynos5_devfreq_int_profile,
					   &devfreq_simple_ondemand,
					   &exynos5_int_ondemand_data);

	if (IS_ERR(data->devfreq)) {
		err = PTR_ERR(data->devfreq);
		goto err_devfreq_add;
	}

	devfreq_register_opp_notifier(dev, data->devfreq);

	pm_qos_add_request(&data->mif_req, PM_QOS_MEMORY_THROUGHPUT, -1);

	mutex_lock(&exynos5_bus_int_data_lock);
	exynos5_bus_int_data = data;
	mutex_unlock(&exynos5_bus_int_data_lock);

	exynos5_int_update(data);
	exynos5_int_poll_start(data);

	return 0;

err_devfreq_add:
	devfreq_remove_device(data->devfreq);
err_ppmu_get:
	platform_set_drvdata(pdev, NULL);
err_opp_add:
	clk_put(data->int_clk);
err_clock:
	regulator_put(data->vdd_int);
err_regulator:
	return err;
}

static __devexit int exynos5_busfreq_int_remove(struct platform_device *pdev)
{
	struct busfreq_data_int *data = platform_get_drvdata(pdev);

	mutex_lock(&exynos5_bus_int_data_lock);
	exynos5_bus_int_data = NULL;
	mutex_unlock(&exynos5_bus_int_data_lock);

	pm_qos_remove_request(&data->mif_req);
	devfreq_remove_device(data->devfreq);
	exynos5_int_poll_stop(data);
	regulator_put(data->vdd_int);
	clk_put(data->int_clk);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int exynos5_busfreq_int_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct opp *max_opp;
	struct opp *opp;
	unsigned long maxfreq = ULONG_MAX;
	unsigned long volt;
	unsigned long freq;
	int err = 0;
	struct busfreq_data_int *data = platform_get_drvdata(pdev);

	exynos5_int_poll_stop(data);
	/*
	 * Set the frequency to the maximum enabled frequency, but set the
	 * voltage to the maximum possible voltage in case the bootloader
	 * sets the frequency to maximum during resume.
	 */
	mutex_lock(&data->lock);

	data->disabled = true;

	rcu_read_lock();
	max_opp = opp_find_freq_floor(data->dev, &maxfreq);
	if (IS_ERR(max_opp)) {
		rcu_read_unlock();
		err = PTR_ERR(max_opp);
		goto unlock;
	}

	maxfreq = ULONG_MAX;
	if (data->devfreq->max_freq)
		maxfreq = data->devfreq->max_freq;

	opp = opp_find_freq_floor(data->dev, &maxfreq);
	if (IS_ERR(opp)) {
		rcu_read_unlock();
		err = PTR_ERR(opp);
		goto unlock;
	}

	freq = opp_get_freq(opp);
	volt = opp_get_voltage(max_opp);
	rcu_read_unlock();

	err = exynos5_int_setvolt(data, volt);
	if (err)
		goto unlock;

	err = clk_set_rate(data->int_clk, freq * 1000);
	if (err)
		goto unlock;

	data->suspend_freq = freq;

unlock:
	mutex_unlock(&data->lock);
	return err;
}

static int exynos5_busfreq_int_resume_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	int err = 0;
	struct busfreq_data_int *data = platform_get_drvdata(pdev);

	/*
	 * Set the frequency to the maximum enabled frequency in case the
	 * bootloader raised it during resume.
	 */
	mutex_lock(&data->lock);

	err = clk_set_rate(data->int_clk, data->suspend_freq * 1000);
	if (err)
		goto unlock;

unlock:
	mutex_unlock(&data->lock);
	return err;
}

static int exynos5_busfreq_int_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct busfreq_data_int *data = platform_get_drvdata(pdev);
	int err = 0;

	/*
	 * Restore the frequency and voltage to the values when suspend was
	 * started.
	 */
	mutex_lock(&data->lock);

	data->disabled = false;

	err = clk_set_rate(data->int_clk, data->curr_freq * 1000);
	if (err)
		goto unlock;

	err = exynos5_int_setvolt(data, data->curr_volt);
	if (err)
		goto unlock;

unlock:
	mutex_unlock(&data->lock);

	if (!err)
		exynos5_int_poll_start(data);

	return err;
}
#endif

static const struct dev_pm_ops exynos5_busfreq_int_pm = {
	.suspend = exynos5_busfreq_int_suspend,
	.resume_noirq = exynos5_busfreq_int_resume_noirq,
	.resume = exynos5_busfreq_int_resume,
};

static struct platform_driver exynos5_busfreq_int_driver = {
	.probe		= exynos5_busfreq_int_probe,
	.remove		= __devexit_p(exynos5_busfreq_int_remove),
	.driver		= {
		.name		= "exynos5-bus-int",
		.owner		= THIS_MODULE,
		.pm		= &exynos5_busfreq_int_pm,
	},
};

static int __init exynos5_busfreq_int_init(void)
{
	return platform_driver_register(&exynos5_busfreq_int_driver);
}
late_initcall(exynos5_busfreq_int_init);

static void __exit exynos5_busfreq_int_exit(void)
{
	platform_driver_unregister(&exynos5_busfreq_int_driver);
}
module_exit(exynos5_busfreq_int_exit);
