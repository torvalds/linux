/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS5 INT clock frequency scaling support using DEVFREQ framework
 * Based on work done by Jonghwan Choi <jhbird.choi@samsung.com>
 * Support for only EXYNOS5250 is present.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/devfreq.h>
#include <linux/io.h>
#include <linux/pm_opp.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>
#include <linux/regulator/consumer.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

#include "exynos_ppmu.h"

#define MAX_SAFEVOLT			1100000 /* 1.10V */
/* Assume that the bus is saturated if the utilization is 25% */
#define INT_BUS_SATURATION_RATIO	25

enum int_level_idx {
	LV_0,
	LV_1,
	LV_2,
	LV_3,
	LV_4,
	_LV_END
};

enum exynos_ppmu_list {
	PPMU_RIGHT,
	PPMU_END,
};

struct busfreq_data_int {
	struct device *dev;
	struct devfreq *devfreq;
	struct regulator *vdd_int;
	struct exynos_ppmu ppmu[PPMU_END];
	unsigned long curr_freq;
	bool disabled;

	struct notifier_block pm_notifier;
	struct mutex lock;
	struct pm_qos_request int_req;
	struct clk *int_clk;
};

struct int_bus_opp_table {
	unsigned int idx;
	unsigned long clk;
	unsigned long volt;
};

static struct int_bus_opp_table exynos5_int_opp_table[] = {
	{LV_0, 266000, 1025000},
	{LV_1, 200000, 1025000},
	{LV_2, 160000, 1025000},
	{LV_3, 133000, 1025000},
	{LV_4, 100000, 1025000},
	{0, 0, 0},
};

static void busfreq_mon_reset(struct busfreq_data_int *data)
{
	unsigned int i;

	for (i = PPMU_RIGHT; i < PPMU_END; i++) {
		void __iomem *ppmu_base = data->ppmu[i].hw_base;

		/* Reset the performance and cycle counters */
		exynos_ppmu_reset(ppmu_base);

		/* Setup count registers to monitor read/write transactions */
		data->ppmu[i].event[PPMU_PMNCNT3] = RDWR_DATA_COUNT;
		exynos_ppmu_setevent(ppmu_base, PPMU_PMNCNT3,
					data->ppmu[i].event[PPMU_PMNCNT3]);

		exynos_ppmu_start(ppmu_base);
	}
}

static void exynos5_read_ppmu(struct busfreq_data_int *data)
{
	int i, j;

	for (i = PPMU_RIGHT; i < PPMU_END; i++) {
		void __iomem *ppmu_base = data->ppmu[i].hw_base;

		exynos_ppmu_stop(ppmu_base);

		/* Update local data from PPMU */
		data->ppmu[i].ccnt = __raw_readl(ppmu_base + PPMU_CCNT);

		for (j = PPMU_PMNCNT0; j < PPMU_PMNCNT_MAX; j++) {
			if (data->ppmu[i].event[j] == 0)
				data->ppmu[i].count[j] = 0;
			else
				data->ppmu[i].count[j] =
					exynos_ppmu_read(ppmu_base, j);
		}
	}

	busfreq_mon_reset(data);
}

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
	struct dev_pm_opp *opp;
	unsigned long old_freq, freq;
	unsigned long volt;

	rcu_read_lock();
	opp = devfreq_recommended_opp(dev, _freq, flags);
	if (IS_ERR(opp)) {
		rcu_read_unlock();
		dev_err(dev, "%s: Invalid OPP.\n", __func__);
		return PTR_ERR(opp);
	}

	freq = dev_pm_opp_get_freq(opp);
	volt = dev_pm_opp_get_voltage(opp);
	rcu_read_unlock();

	old_freq = data->curr_freq;

	if (old_freq == freq)
		return 0;

	dev_dbg(dev, "targeting %lukHz %luuV\n", freq, volt);

	mutex_lock(&data->lock);

	if (data->disabled)
		goto out;

	if (freq > exynos5_int_opp_table[0].clk)
		pm_qos_update_request(&data->int_req, freq * 16 / 1000);
	else
		pm_qos_update_request(&data->int_req, -1);

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

static int exynos5_get_busier_dmc(struct busfreq_data_int *data)
{
	int i, j;
	int busy = 0;
	unsigned int temp = 0;

	for (i = PPMU_RIGHT; i < PPMU_END; i++) {
		for (j = PPMU_PMNCNT0; j < PPMU_PMNCNT_MAX; j++) {
			if (data->ppmu[i].count[j] > temp) {
				temp = data->ppmu[i].count[j];
				busy = i;
			}
		}
	}

	return busy;
}

static int exynos5_int_get_dev_status(struct device *dev,
				      struct devfreq_dev_status *stat)
{
	struct platform_device *pdev = container_of(dev, struct platform_device,
						    dev);
	struct busfreq_data_int *data = platform_get_drvdata(pdev);
	int busier_dmc;

	exynos5_read_ppmu(data);
	busier_dmc = exynos5_get_busier_dmc(data);

	stat->current_frequency = data->curr_freq;

	/* Number of cycles spent on memory access */
	stat->busy_time = data->ppmu[busier_dmc].count[PPMU_PMNCNT3];
	stat->busy_time *= 100 / INT_BUS_SATURATION_RATIO;
	stat->total_time = data->ppmu[busier_dmc].ccnt;

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
	.polling_ms		= 100,
	.target			= exynos5_busfreq_int_target,
	.get_dev_status		= exynos5_int_get_dev_status,
	.exit			= exynos5_int_exit,
};

static int exynos5250_init_int_tables(struct busfreq_data_int *data)
{
	int i, err = 0;

	for (i = LV_0; i < _LV_END; i++) {
		err = dev_pm_opp_add(data->dev, exynos5_int_opp_table[i].clk,
				exynos5_int_opp_table[i].volt);
		if (err) {
			dev_err(data->dev, "Cannot add opp entries.\n");
			return err;
		}
	}

	return 0;
}

static int exynos5_busfreq_int_pm_notifier_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	struct busfreq_data_int *data = container_of(this,
					struct busfreq_data_int, pm_notifier);
	struct dev_pm_opp *opp;
	unsigned long maxfreq = ULONG_MAX;
	unsigned long freq;
	unsigned long volt;
	int err = 0;

	switch (event) {
	case PM_SUSPEND_PREPARE:
		/* Set Fastest and Deactivate DVFS */
		mutex_lock(&data->lock);

		data->disabled = true;

		rcu_read_lock();
		opp = dev_pm_opp_find_freq_floor(data->dev, &maxfreq);
		if (IS_ERR(opp)) {
			rcu_read_unlock();
			err = PTR_ERR(opp);
			goto unlock;
		}
		freq = dev_pm_opp_get_freq(opp);
		volt = dev_pm_opp_get_voltage(opp);
		rcu_read_unlock();

		err = exynos5_int_setvolt(data, volt);
		if (err)
			goto unlock;

		err = clk_set_rate(data->int_clk, freq * 1000);

		if (err)
			goto unlock;

		data->curr_freq = freq;
unlock:
		mutex_unlock(&data->lock);
		if (err)
			return NOTIFY_BAD;
		return NOTIFY_OK;
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		/* Reactivate */
		mutex_lock(&data->lock);
		data->disabled = false;
		mutex_unlock(&data->lock);
		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
}

static int exynos5_busfreq_int_probe(struct platform_device *pdev)
{
	struct busfreq_data_int *data;
	struct dev_pm_opp *opp;
	struct device *dev = &pdev->dev;
	struct device_node *np;
	unsigned long initial_freq;
	unsigned long initial_volt;
	int err = 0;
	int i;

	data = devm_kzalloc(&pdev->dev, sizeof(struct busfreq_data_int),
				GFP_KERNEL);
	if (data == NULL) {
		dev_err(dev, "Cannot allocate memory.\n");
		return -ENOMEM;
	}

	np = of_find_compatible_node(NULL, NULL, "samsung,exynos5250-ppmu");
	if (np == NULL) {
		pr_err("Unable to find PPMU node\n");
		return -ENOENT;
	}

	for (i = PPMU_RIGHT; i < PPMU_END; i++) {
		/* map PPMU memory region */
		data->ppmu[i].hw_base = of_iomap(np, i);
		if (data->ppmu[i].hw_base == NULL) {
			dev_err(&pdev->dev, "failed to map memory region\n");
			return -ENOMEM;
		}
	}
	data->pm_notifier.notifier_call = exynos5_busfreq_int_pm_notifier_event;
	data->dev = dev;
	mutex_init(&data->lock);

	err = exynos5250_init_int_tables(data);
	if (err)
		return err;

	data->vdd_int = devm_regulator_get(dev, "vdd_int");
	if (IS_ERR(data->vdd_int)) {
		dev_err(dev, "Cannot get the regulator \"vdd_int\"\n");
		return PTR_ERR(data->vdd_int);
	}

	data->int_clk = devm_clk_get(dev, "int_clk");
	if (IS_ERR(data->int_clk)) {
		dev_err(dev, "Cannot get clock \"int_clk\"\n");
		return PTR_ERR(data->int_clk);
	}

	rcu_read_lock();
	opp = dev_pm_opp_find_freq_floor(dev,
			&exynos5_devfreq_int_profile.initial_freq);
	if (IS_ERR(opp)) {
		rcu_read_unlock();
		dev_err(dev, "Invalid initial frequency %lu kHz.\n",
		       exynos5_devfreq_int_profile.initial_freq);
		return PTR_ERR(opp);
	}
	initial_freq = dev_pm_opp_get_freq(opp);
	initial_volt = dev_pm_opp_get_voltage(opp);
	rcu_read_unlock();
	data->curr_freq = initial_freq;

	err = clk_set_rate(data->int_clk, initial_freq * 1000);
	if (err) {
		dev_err(dev, "Failed to set initial frequency\n");
		return err;
	}

	err = exynos5_int_setvolt(data, initial_volt);
	if (err)
		return err;

	platform_set_drvdata(pdev, data);

	busfreq_mon_reset(data);

	data->devfreq = devfreq_add_device(dev, &exynos5_devfreq_int_profile,
					   "simple_ondemand", NULL);

	if (IS_ERR(data->devfreq)) {
		err = PTR_ERR(data->devfreq);
		goto err_devfreq_add;
	}

	devfreq_register_opp_notifier(dev, data->devfreq);

	err = register_pm_notifier(&data->pm_notifier);
	if (err) {
		dev_err(dev, "Failed to setup pm notifier\n");
		goto err_devfreq_add;
	}

	/* TODO: Add a new QOS class for int/mif bus */
	pm_qos_add_request(&data->int_req, PM_QOS_NETWORK_THROUGHPUT, -1);

	return 0;

err_devfreq_add:
	devfreq_remove_device(data->devfreq);
	return err;
}

static int exynos5_busfreq_int_remove(struct platform_device *pdev)
{
	struct busfreq_data_int *data = platform_get_drvdata(pdev);

	pm_qos_remove_request(&data->int_req);
	unregister_pm_notifier(&data->pm_notifier);
	devfreq_remove_device(data->devfreq);

	return 0;
}

static int exynos5_busfreq_int_resume(struct device *dev)
{
	struct platform_device *pdev = container_of(dev, struct platform_device,
						    dev);
	struct busfreq_data_int *data = platform_get_drvdata(pdev);

	busfreq_mon_reset(data);
	return 0;
}

static const struct dev_pm_ops exynos5_busfreq_int_pm = {
	.resume	= exynos5_busfreq_int_resume,
};

/* platform device pointer for exynos5 devfreq device. */
static struct platform_device *exynos5_devfreq_pdev;

static struct platform_driver exynos5_busfreq_int_driver = {
	.probe		= exynos5_busfreq_int_probe,
	.remove		= exynos5_busfreq_int_remove,
	.driver		= {
		.name		= "exynos5-bus-int",
		.owner		= THIS_MODULE,
		.pm		= &exynos5_busfreq_int_pm,
	},
};

static int __init exynos5_busfreq_int_init(void)
{
	int ret;

	ret = platform_driver_register(&exynos5_busfreq_int_driver);
	if (ret < 0)
		goto out;

	exynos5_devfreq_pdev =
		platform_device_register_simple("exynos5-bus-int", -1, NULL, 0);
	if (IS_ERR(exynos5_devfreq_pdev)) {
		ret = PTR_ERR(exynos5_devfreq_pdev);
		goto out1;
	}

	return 0;
out1:
	platform_driver_unregister(&exynos5_busfreq_int_driver);
out:
	return ret;
}
late_initcall(exynos5_busfreq_int_init);

static void __exit exynos5_busfreq_int_exit(void)
{
	platform_device_unregister(exynos5_devfreq_pdev);
	platform_driver_unregister(&exynos5_busfreq_int_driver);
}
module_exit(exynos5_busfreq_int_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("EXYNOS5 busfreq driver with devfreq framework");
