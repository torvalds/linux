/*
 * (C) COPYRIGHT RockChip Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 */

/* #define ENABLE_DEBUG_LOG */
#include "custom_log.h"

#include <mali_kbase.h>
#include <mali_kbase_defs.h>
#include <mali_kbase_config.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <backend/gpu/mali_kbase_pm_defs.h>

#include <linux/pm_runtime.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/nvmem-consumer.h>
#include <linux/soc/rockchip/pvtm.h>
#include <linux/thermal.h>
#include <soc/rockchip/rockchip_opp_select.h>

#include "mali_kbase_rk.h"

/**
 * @file mali_kbase_config_rk.c
 * 对 platform_config_of_rk 的具体实现.
 *
 * mali_device_driver 包含两部分 :
 *      .DP : platform_dependent_part_in_mdd :
 *		依赖 platform 部分,
 *		源码在 <mdd_src_dir>/platform/<platform_name>/
 *		在 mali_device_driver 内部,
 *			记为 platform_dependent_part,
 *			也被记为 platform_specific_code.
 *      .DP : common_parts_in_mdd :
 *		arm 实现的通用的部分,
 *		源码在 <mdd_src_dir>/ 下.
 *		在 mali_device_driver 内部, 记为 common_parts.
 */

/*---------------------------------------------------------------------------*/

#ifdef CONFIG_REGULATOR
static int rk_pm_enable_regulator(struct kbase_device *kbdev);
static void rk_pm_disable_regulator(struct kbase_device *kbdev);
#else
static inline int rk_pm_enable_regulator(struct kbase_device *kbdev)
{
	return 0;
}

static inline void rk_pm_disable_regulator(struct kbase_device *kbdev)
{
}
#endif

static int rk_pm_enable_clk(struct kbase_device *kbdev);

static void rk_pm_disable_clk(struct kbase_device *kbdev);

static int kbase_platform_rk_create_sysfs_files(struct device *dev);

static void kbase_platform_rk_remove_sysfs_files(struct device *dev);

/*---------------------------------------------------------------------------*/

static void rk_pm_power_off_delay_work(struct work_struct *work)
{
	struct rk_context *platform =
		container_of(to_delayed_work(work), struct rk_context, work);
	struct kbase_device *kbdev = platform->kbdev;

	if (!platform->is_powered) {
		D("mali_dev is already powered off.");
		return;
	}

	if (pm_runtime_enabled(kbdev->dev)) {
		D("to put_sync_suspend mali_dev.");
		pm_runtime_put_sync_suspend(kbdev->dev);
	}

	rk_pm_disable_regulator(kbdev);

	platform->is_powered = false;
	KBASE_TIMELINE_GPU_POWER(kbdev, 0);
	wake_unlock(&platform->wake_lock);
}

static int kbase_platform_rk_init(struct kbase_device *kbdev)
{
	int ret = 0;
	struct rk_context *platform;

	platform = kzalloc(sizeof(*platform), GFP_KERNEL);
	if (!platform) {
		E("err.");
		return -ENOMEM;
	}

	platform->is_powered = false;
	platform->kbdev = kbdev;

	platform->delay_ms = 200;
	if (of_property_read_u32(kbdev->dev->of_node, "power-off-delay-ms",
				 &platform->delay_ms))
		W("power-off-delay-ms not available.");

	platform->power_off_wq = create_freezable_workqueue("gpu_power_off_wq");
	if (!platform->power_off_wq) {
		E("couldn't create workqueue");
		ret = -ENOMEM;
		goto err_wq;
	}
	INIT_DEFERRABLE_WORK(&platform->work, rk_pm_power_off_delay_work);

	wake_lock_init(&platform->wake_lock, WAKE_LOCK_SUSPEND, "gpu");

	platform->utilisation_period = DEFAULT_UTILISATION_PERIOD_IN_MS;

	ret = kbase_platform_rk_create_sysfs_files(kbdev->dev);
	if (ret) {
		E("fail to create sysfs_files. ret = %d.", ret);
		goto err_sysfs_files;
	}

	kbdev->platform_context = (void *)platform;
	pm_runtime_enable(kbdev->dev);

	return 0;

err_sysfs_files:
	wake_lock_destroy(&platform->wake_lock);
	destroy_workqueue(platform->power_off_wq);
err_wq:
	return ret;
}

static void kbase_platform_rk_term(struct kbase_device *kbdev)
{
	struct rk_context *platform =
		(struct rk_context *)kbdev->platform_context;

	pm_runtime_disable(kbdev->dev);
	kbdev->platform_context = NULL;

	if (platform) {
		cancel_delayed_work_sync(&platform->work);
		wake_lock_destroy(&platform->wake_lock);
		destroy_workqueue(platform->power_off_wq);
		platform->is_powered = false;
		platform->kbdev = NULL;
		kfree(platform);
	}
	kbase_platform_rk_remove_sysfs_files(kbdev->dev);
}

struct kbase_platform_funcs_conf platform_funcs = {
	.platform_init_func = &kbase_platform_rk_init,
	.platform_term_func = &kbase_platform_rk_term,
};

/*---------------------------------------------------------------------------*/

static int rk_pm_callback_runtime_on(struct kbase_device *kbdev)
{
	return 0;
}

static void rk_pm_callback_runtime_off(struct kbase_device *kbdev)
{
}

static int rk_pm_callback_power_on(struct kbase_device *kbdev)
{
	int ret = 1; /* Assume GPU has been powered off */
	int err = 0;
	struct rk_context *platform = get_rk_context(kbdev);

	cancel_delayed_work_sync(&platform->work);

	err = rk_pm_enable_clk(kbdev);
	if (err) {
		E("failed to enable clk: %d", err);
		return err;
	}

	if (platform->is_powered) {
		D("mali_device is already powered.");
		return 0;
	}

	/* we must enable vdd_gpu before pd_gpu_in_chip. */
	err = rk_pm_enable_regulator(kbdev);
	if (err) {
		E("fail to enable regulator, err : %d.", err);
		return err;
	}

	/* 若 mali_dev 的 runtime_pm 是 enabled 的, 则... */
	if (pm_runtime_enabled(kbdev->dev)) {
		D("to resume mali_dev syncly.");
		/* 对 pd_in_chip 的 on 操作,
		 * 将在 pm_domain 的 runtime_pm_callbacks 中完成.
		 */
		err = pm_runtime_get_sync(kbdev->dev);
		if (err < 0) {
			E("failed to runtime resume device: %d.", err);
			return err;
		} else if (err == 1) { /* runtime_pm_status is still active */
			D("chip has NOT been powered off, no need to re-init.");
			ret = 0;
		}
	}

	platform->is_powered = true;
	KBASE_TIMELINE_GPU_POWER(kbdev, 1);
	wake_lock(&platform->wake_lock);

	return ret;
}

static void rk_pm_callback_power_off(struct kbase_device *kbdev)
{
	struct rk_context *platform = get_rk_context(kbdev);

	rk_pm_disable_clk(kbdev);
	queue_delayed_work(platform->power_off_wq, &platform->work,
			   msecs_to_jiffies(platform->delay_ms));
}

int rk_kbase_device_runtime_init(struct kbase_device *kbdev)
{
	return 0;
}

void rk_kbase_device_runtime_disable(struct kbase_device *kbdev)
{
}

struct kbase_pm_callback_conf pm_callbacks = {
	.power_on_callback = rk_pm_callback_power_on,
	.power_off_callback = rk_pm_callback_power_off,
#ifdef CONFIG_PM
	.power_runtime_init_callback = rk_kbase_device_runtime_init,
	.power_runtime_term_callback = rk_kbase_device_runtime_disable,
	.power_runtime_on_callback = rk_pm_callback_runtime_on,
	.power_runtime_off_callback = rk_pm_callback_runtime_off,
#else				/* CONFIG_PM */
	.power_runtime_init_callback = NULL,
	.power_runtime_term_callback = NULL,
	.power_runtime_on_callback = NULL,
	.power_runtime_off_callback = NULL,
#endif				/* CONFIG_PM */
};

int kbase_platform_early_init(void)
{
	/* Nothing needed at this stage */
	return 0;
}

/*---------------------------------------------------------------------------*/

void kbase_platform_rk_shutdown(struct kbase_device *kbdev)
{
	I("to make vdd_gpu enabled for turning off pd_gpu in pm_framework.");
	rk_pm_enable_regulator(kbdev);
}

/*---------------------------------------------------------------------------*/

#ifdef CONFIG_REGULATOR
static int rk_pm_enable_regulator(struct kbase_device *kbdev)
{
	int ret = 0;

	if (!kbdev->regulator) {
		W("no mali regulator control, no need to enable.");
		goto EXIT;
	}

	D("to enable regulator.");
	ret = regulator_enable(kbdev->regulator);
	if (ret) {
		E("fail to enable regulator, ret : %d.", ret);
		goto EXIT;
	}

EXIT:
	return ret;
}

static void rk_pm_disable_regulator(struct kbase_device *kbdev)
{
	if (!(kbdev->regulator)) {
		W("no mali regulator control, no need to disable.");
		return;
	}

	D("to disable regulator.");
	regulator_disable(kbdev->regulator);
}
#endif

static int rk_pm_enable_clk(struct kbase_device *kbdev)
{
	int err = 0;

	if (!(kbdev->clock)) {
		W("no mali clock control, no need to enable.");
	} else {
		D("to enable clk.");
		err = clk_enable(kbdev->clock);
		if (err)
			E("failed to enable clk: %d.", err);
	}

	return err;
}

static void rk_pm_disable_clk(struct kbase_device *kbdev)
{
	if (!(kbdev->clock)) {
		W("no mali clock control, no need to disable.");
	} else {
		D("to disable clk.");
		clk_disable(kbdev->clock);
	}
}

/*---------------------------------------------------------------------------*/

static ssize_t utilisation_period_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	struct rk_context *platform = get_rk_context(kbdev);
	ssize_t ret = 0;

	ret += snprintf(buf, PAGE_SIZE, "%u\n", platform->utilisation_period);

	return ret;
}

static ssize_t utilisation_period_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf,
					size_t count)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	struct rk_context *platform = get_rk_context(kbdev);
	int ret = 0;

	ret = kstrtouint(buf, 0, &platform->utilisation_period);
	if (ret) {
		E("invalid input period : %s.", buf);
		return ret;
	}
	D("set utilisation_period to '%d'.", platform->utilisation_period);

	return count;
}

static ssize_t utilisation_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	struct rk_context *platform = get_rk_context(kbdev);
	ssize_t ret = 0;
	unsigned long period_in_us = platform->utilisation_period * 1000;
	u32 utilisation;
	struct kbasep_pm_metrics metrics_when_start;
	struct kbasep_pm_metrics metrics_diff; /* between start and end. */
	u32 total_time = 0;
	u32 busy_time = 0;

	/* get current metrics data. */
	kbase_pm_get_dvfs_metrics(kbdev, &metrics_when_start, &metrics_diff);
	/* sleep for 'period_in_us'. */
	usleep_range(period_in_us, period_in_us + 100);
	/* get metrics data between start and end. */
	kbase_pm_get_dvfs_metrics(kbdev, &metrics_when_start, &metrics_diff);

	total_time = metrics_diff.time_busy + metrics_diff.time_idle;
	busy_time = metrics_diff.time_busy;
	D("total_time : %u, busy_time : %u.", total_time, busy_time);

	utilisation = busy_time * 100 / total_time;
	ret += snprintf(buf, PAGE_SIZE, "%d\n", utilisation);

	return ret;
}

static DEVICE_ATTR_RW(utilisation_period);
static DEVICE_ATTR_RO(utilisation);

static int kbase_platform_rk_create_sysfs_files(struct device *dev)
{
	int ret = 0;

	ret = device_create_file(dev, &dev_attr_utilisation_period);
	if (ret) {
		E("fail to create sysfs file 'utilisation_period'.");
		goto out;
	}

	ret = device_create_file(dev, &dev_attr_utilisation);
	if (ret) {
		E("fail to create sysfs file 'utilisation'.");
		goto remove_utilisation_period;
	}

	return 0;

remove_utilisation_period:
	device_remove_file(dev, &dev_attr_utilisation_period);
out:
	return ret;
}

static void kbase_platform_rk_remove_sysfs_files(struct device *dev)
{
	device_remove_file(dev, &dev_attr_utilisation_period);
	device_remove_file(dev, &dev_attr_utilisation);
}

int kbase_platform_rk_init_opp_table(struct kbase_device *kbdev)
{
	return rockchip_init_opp_table(kbdev->dev, NULL,
				       "gpu_leakage", "mali");
}
