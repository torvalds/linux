/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 */

#define ENABLE_DEBUG_LOG
#include "custom_log.h"

#include <mali_kbase.h>
#include <mali_kbase_defs.h>
#include <mali_kbase_config.h>

#include <linux/pm_runtime.h>
#include <linux/suspend.h>

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

int kbase_platform_rk_init(struct kbase_device *kbdev)
{
	return 0;
}

void kbase_platform_rk_term(struct kbase_device *kbdev)
{
}

struct kbase_platform_funcs_conf platform_funcs = {
	.platform_init_func = &kbase_platform_rk_init,
	.platform_term_func = &kbase_platform_rk_term,
};

/*---------------------------------------------------------------------------*/

static int pm_callback_power_on(struct kbase_device *kbdev)
{
	return 0;
}

static void pm_callback_power_off(struct kbase_device *kbdev)
{
}

int kbase_device_runtime_init(struct kbase_device *kbdev)
{
	return 0;
}

void kbase_device_runtime_disable(struct kbase_device *kbdev)
{
}

static int pm_callback_runtime_on(struct kbase_device *kbdev)
{
	return 0;
}

static void pm_callback_runtime_off(struct kbase_device *kbdev)
{
}

struct kbase_pm_callback_conf pm_callbacks = {
	.power_on_callback =  pm_callback_power_on,
	.power_off_callback = pm_callback_power_off,
#ifdef CONFIG_PM
	.power_runtime_init_callback = kbase_device_runtime_init,
	.power_runtime_term_callback = kbase_device_runtime_disable,
	.power_runtime_on_callback = pm_callback_runtime_on,
	.power_runtime_off_callback = pm_callback_runtime_off,
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

