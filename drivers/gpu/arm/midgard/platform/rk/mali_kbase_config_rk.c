/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
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

#include <linux/pm_runtime.h>
#include <linux/suspend.h>

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

static int rk_pm_enable_regulator(struct kbase_device *kbdev);

static void rk_pm_disable_regulator(struct kbase_device *kbdev);

static int rk_pm_enable_clk(struct kbase_device *kbdev);

static void rk_pm_disable_clk(struct kbase_device *kbdev);

/*---------------------------------------------------------------------------*/

static int kbase_platform_rk_init(struct kbase_device *kbdev)
{
	struct rk_context *platform;

	platform = kzalloc(sizeof(*platform), GFP_KERNEL);
	if (!platform) {
		E("err.");
		return -ENOMEM;
	}

	platform->is_powered = false;

	kbdev->platform_context = (void *)platform;

	return 0;
}

static void kbase_platform_rk_term(struct kbase_device *kbdev)
{
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
	struct rk_context *platform;

	platform = (struct rk_context *)kbdev->platform_context;
	if (platform->is_powered) {
		W("mali_device is already powered.");
		return 0;
	}

	D("powering on.");

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

	err = rk_pm_enable_clk(kbdev); /* clk is not relative to pd. */
	if (err) {
		E("failed to enable clk: %d", err);
		return err;
	}

	platform->is_powered = true;
	KBASE_TIMELINE_GPU_POWER(kbdev, 1);

	return ret;
}

static void rk_pm_callback_power_off(struct kbase_device *kbdev)
{
	struct rk_context *platform =
		(struct rk_context *)kbdev->platform_context;

	if (!platform->is_powered) {
		W("mali_dev is already powered off.");
		return;
	}

	D("powering off.");

	platform->is_powered = false;
	KBASE_TIMELINE_GPU_POWER(kbdev, 0);

	rk_pm_disable_clk(kbdev);

	if (pm_runtime_enabled(kbdev->dev)) {
		pm_runtime_mark_last_busy(kbdev->dev);
		D("to put_sync_suspend mali_dev.");
		pm_runtime_put_sync_suspend(kbdev->dev);
	}

	rk_pm_disable_regulator(kbdev);
}

int rk_kbase_device_runtime_init(struct kbase_device *kbdev)
{
	pm_runtime_set_autosuspend_delay(kbdev->dev, 200);
	pm_runtime_use_autosuspend(kbdev->dev);

	/* no need to call pm_runtime_set_active here. */

	D("to enable pm_runtime.");
	pm_runtime_enable(kbdev->dev);

	return 0;
}

void rk_kbase_device_runtime_disable(struct kbase_device *kbdev)
{
	D("to disable pm_runtime.");
	pm_runtime_disable(kbdev->dev);
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

