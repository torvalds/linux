/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2009-2010, 2012 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */


/**
 * @file rk3066.c
 * 实现 rk30_platform 中的 platform_specific_strategy_callbacks,
 * 实际上也是 platform_dependent_part 的顶层.
 *
 * mali_device_driver(mdd) 包含两部分 :
 *	.DP : platform_dependent_part :
 *		依赖 platform 部分,
 *		源码在 <mdd_src_dir>/mali/platform/<platform_name> 目录下.
 *	.DP : common_parts : ARM 实现的通用的部分.
 */

#define ENABLE_DEBUG_LOG
#include "custom_log.h"

#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/pm.h>
#include <linux/of.h>
#ifdef CONFIG_PM_RUNTIME
#include <linux/pm_runtime.h>
#endif
#include <linux/workqueue.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <linux/rockchip/cpu.h>

#include <linux/mali/mali_utgard.h>
#include "mali_kernel_common.h"
#include "mali_platform.h"
#include "arm_core_scaling.h"

#ifdef CONFIG_PM_RUNTIME
static int mali_runtime_suspend(struct device *device)
{
	int ret = 0;

	MALI_DEBUG_PRINT(4, ("mali_runtime_suspend() called\n"));

	if (NULL != device->driver &&
	    NULL != device->driver->pm &&
	    NULL != device->driver->pm->runtime_suspend) {
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->runtime_suspend(device);
	}

	mali_platform_power_mode_change(MALI_POWER_MODE_LIGHT_SLEEP);

	return ret;
}

static int mali_runtime_resume(struct device *device)
{
	int ret = 0;

	MALI_DEBUG_PRINT(4, ("mali_runtime_resume() called\n"));

	mali_platform_power_mode_change(MALI_POWER_MODE_ON);

	if (NULL != device->driver &&
	    NULL != device->driver->pm &&
	    NULL != device->driver->pm->runtime_resume) {
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->runtime_resume(device);
	}

	return ret;
}

static int mali_runtime_idle(struct device *device)
{
	int ret = 0;

	MALI_DEBUG_PRINT(4, ("mali_runtime_idle() called\n"));

	if (NULL != device->driver &&
	    NULL != device->driver->pm &&
	    NULL != device->driver->pm->runtime_idle) {
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->runtime_idle(device);
		if (0 != ret)
			return ret;
	}

	pm_runtime_suspend(device);

	return 0;
}
#endif

static int mali_os_suspend(struct device *device)
{
	int ret = 0;

	MALI_DEBUG_PRINT(4, ("mali_os_suspend() called\n"));

	if (NULL != device->driver &&
	    NULL != device->driver->pm &&
	    NULL != device->driver->pm->suspend) {
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->suspend(device);
	}

	mali_platform_power_mode_change(MALI_POWER_MODE_DEEP_SLEEP);

	return ret;
}

static int mali_os_resume(struct device *device)
{
	int ret = 0;

	MALI_DEBUG_PRINT(4, ("mali_os_resume() called\n"));

	mali_platform_power_mode_change(MALI_POWER_MODE_ON);

	if (NULL != device->driver &&
	    NULL != device->driver->pm &&
	    NULL != device->driver->pm->resume) {
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->resume(device);
	}

	return ret;
}

static int mali_os_freeze(struct device *device)
{
	int ret = 0;

	MALI_DEBUG_PRINT(4, ("mali_os_freeze() called\n"));

	if (NULL != device->driver &&
	    NULL != device->driver->pm &&
	    NULL != device->driver->pm->freeze) {
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->freeze(device);
	}

	return ret;
}

static int mali_os_thaw(struct device *device)
{
	int ret = 0;

	MALI_DEBUG_PRINT(4, ("mali_os_thaw() called\n"));

	if (NULL != device->driver &&
	    NULL != device->driver->pm &&
	    NULL != device->driver->pm->thaw) {
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->thaw(device);
	}

	return ret;
}

static const struct dev_pm_ops mali_gpu_device_type_pm_ops = {
	.suspend = mali_os_suspend,
	.resume = mali_os_resume,
	.freeze = mali_os_freeze,
	.thaw = mali_os_thaw,
#ifdef CONFIG_PM_RUNTIME
	.runtime_suspend = mali_runtime_suspend,
	.runtime_resume = mali_runtime_resume,
	.runtime_idle = mali_runtime_idle,
#endif
};

static const struct device_type mali_gpu_device_device_type = {
	.pm = &mali_gpu_device_type_pm_ops,
};

/**
 * platform_specific_data_of_platform_device_of_mali_gpu.
 *
 * 类型 'struct mali_gpu_device_data' 由 common_part 定义,
 * 实例也将被 common_part 引用,
 * 比如通知 mali_utilization_event 等.
 */
static const struct mali_gpu_device_data mali_gpu_data = {
	.shared_mem_size = 1024 * 1024 * 1024, /* 1GB */
	.fb_start = 0x40000000,
	.fb_size = 0xb1000000,
	.max_job_runtime = 100, /* 100 ms */
	/* .utilization_interval = 0, */ /* 0ms */
	.utilization_callback = mali_gpu_utilization_handler,
};

static void mali_platform_device_add_config(struct platform_device *pdev)
{
	pdev->name = MALI_GPU_NAME_UTGARD,
	pdev->id = 0;
	pdev->dev.type = &mali_gpu_device_device_type;
	pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask,
	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
}

/*---------------------------------------------------------------------------*/

/**
 * 将被 common_part 回调的, 对 platform_device_of_mali_gpu 初始化的策略回调实现.
 *
 * .DP : platform_specific_strategy_callbacks_called_by_common_part,
 *       platform_specific_strategy_callbacks :
 *              被 common_part 调用的 平台相关的策略回调.
 */
int mali_platform_device_init(struct platform_device *pdev)
{
// error
	int err = 0;
	int num_pp_cores = 0;

	D("mali_platform_device_register() called\n");

	if (of_machine_is_compatible("rockchip,rk3036"))
		num_pp_cores = 1;
	else if (of_machine_is_compatible("rockchip,rk3228h"))
		num_pp_cores = 2;
	else if (of_machine_is_compatible("rockchip,rk3328h"))
		num_pp_cores = 2;
	else
		num_pp_cores = 2;

	D("to add config.");
	mali_platform_device_add_config(pdev);

	D("to add data to platform_device..");
	/* 将 platform_specific_data 添加到 platform_device_of_mali_gpu.
	 * 这里的 platform_specific_data 的类型由 common_part 定义. */
	err = platform_device_add_data(pdev, &mali_gpu_data,
				       sizeof(mali_gpu_data));
	if (err == 0) {
		D("to init internal_platform_specific_code.");
		/* .KP : 初始化 platform_device_of_mali_gpu 中,
		 * 仅和 platform_dependent_part 相关的部分. */
		err = mali_platform_init(pdev);
		if (err == 0) {
#ifdef CONFIG_PM_RUNTIME
			pm_runtime_set_autosuspend_delay(&(pdev->dev), 1000);
			pm_runtime_use_autosuspend(&(pdev->dev));
			pm_runtime_enable(&(pdev->dev));
#endif
			MALI_DEBUG_ASSERT(0 < num_pp_cores);
			mali_core_scaling_init(num_pp_cores);
			return 0;
		}
	}

	return err;
}

/**
 * 将被 common_part 回调的, 对 platform_device_of_mali_gpu 终止化的策略回调实现.
 */
void mali_platform_device_deinit(struct platform_device *pdev)
{
	MALI_DEBUG_PRINT(4, ("mali_platform_device_unregister() called\n"));

	mali_platform_deinit(pdev);
}
