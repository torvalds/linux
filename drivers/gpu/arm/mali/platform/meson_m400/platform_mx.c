/*
 * platform.c
 * 
 * clock source setting and resource config
 *
 *  Created on: Dec 4, 2013
 *      Author: amlogic
 */

#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/pm.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <mach/register.h>
#include <mach/irqs.h>
#include <mach/io.h>
#include <asm/io.h>
#include <linux/mali/mali_utgard.h>

#include <common/mali_kernel_common.h>
#include <common/mali_osk_profiling.h>
#include <common/mali_pmu.h>

#include "meson_main.h"
#include "mali_fix.h"
#include "mali_platform.h"

/**
 *    For Meson 6tvd.
 * 
 */

#if MESON_CPU_TYPE <= MESON_CPU_TYPE_MESON6TV

u32 mali_dvfs_clk[1];
u32 mali_dvfs_clk_sample[1];

#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6TV
#undef INT_MALI_GP
#undef INT_MALI_GP_MMU
#undef INT_MALI_PP
#undef INT_MALI_PP2
#undef INT_MALI_PP3
#undef INT_MALI_PP4
#undef INT_MALI_PP_MMU
#undef INT_MALI_PP2_MMU
#undef INT_MALI_PP3_MMU
#undef INT_MALI_PP4_MMU

#define INT_MALI_GP      (48+32)
#define INT_MALI_GP_MMU  (49+32)
#define INT_MALI_PP      (50+32)
#define INT_MALI_PP2     (58+32)
#define INT_MALI_PP3     (60+32)
#define INT_MALI_PP4     (62+32)
#define INT_MALI_PP_MMU  (51+32)
#define INT_MALI_PP2_MMU (59+32)
#define INT_MALI_PP3_MMU (61+32)
#define INT_MALI_PP4_MMU (63+32)

#ifndef CONFIG_MALI400_4_PP
static struct resource meson_mali_resources[] =
{
	MALI_GPU_RESOURCES_MALI400_MP2(0xd0060000,
			INT_MALI_GP, INT_MALI_GP_MMU,
			INT_MALI_PP, INT_MALI_PP_MMU,
			INT_MALI_PP2, INT_MALI_PP2_MMU)
};
#else
static struct resource meson_mali_resources[] =
{
	MALI_GPU_RESOURCES_MALI400_MP4(0xd0060000,
			INT_MALI_GP, INT_MALI_GP_MMU,
			INT_MALI_PP, INT_MALI_PP_MMU,
			INT_MALI_PP2, INT_MALI_PP2_MMU,
			INT_MALI_PP3, INT_MALI_PP3_MMU,
			INT_MALI_PP4, INT_MALI_PP4_MMU
			)
};
#endif

#elif MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6

#undef INT_MALI_GP
#undef INT_MALI_GP_MMU
#undef INT_MALI_PP
#undef INT_MALI_PP2
#undef INT_MALI_PP_MMU
#undef INT_MALI_PP2_MMU

#define INT_MALI_GP      (48+32)
#define INT_MALI_GP_MMU  (49+32)
#define INT_MALI_PP      (50+32)
#define INT_MALI_PP_MMU  (51+32)
#define INT_MALI_PP2_MMU ( 6+32)

static struct resource meson_mali_resources[] =
{
	MALI_GPU_RESOURCES_MALI400_MP2(0xd0060000, 
			INT_MALI_GP, INT_MALI_GP_MMU,
			INT_MALI_PP, INT_MALI_PP2_MMU,
			INT_MALI_PP_MMU, INT_MALI_PP2_MMU)
};

#else  /* MESON_CPU_TYPE == MESON_CPU_TYPE_MESON3 */

#undef INT_MALI_GP
#undef INT_MALI_GP_MMU
#undef INT_MALI_PP
#undef INT_MALI_PP_MMU

#define INT_MALI_GP	48
#define INT_MALI_GP_MMU 49
#define INT_MALI_PP	50
#define INT_MALI_PP_MMU 51

static struct resource meson_mali_resources[] =
{
	MALI_GPU_RESOURCES_MALI400_MP1(0xd0060000,
			INT_MALI_GP, INT_MALI_GP_MMU, INT_MALI_PP, INT_MALI_PP_MMU)
};
#endif /* MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6TV */

void mali_gpu_utilization_callback(struct mali_gpu_utilization_data *data)
{

}

mali_plat_info_t mali_plat_data = {

};

int mali_meson_init_start(struct platform_device* ptr_plt_dev)
{
	/* for mali platform data. */
	struct mali_gpu_device_data* pdev = ptr_plt_dev->dev.platform_data;
	pdev->utilization_interval = 1000,
	pdev->utilization_callback = mali_gpu_utilization_callback,

	/* for resource data. */
	ptr_plt_dev->num_resources = ARRAY_SIZE(meson_mali_resources);
	ptr_plt_dev->resource = meson_mali_resources;
	return 0;
}

int mali_meson_init_finish(struct platform_device* ptr_plt_dev)
{
	mali_platform_init();
	return 0;
}

int mali_meson_uninit(struct platform_device* ptr_plt_dev)
{
	mali_platform_deinit();
	return 0;
}

int mali_light_suspend(struct device *device)
{
	int ret = 0;

	if (NULL != device->driver &&
	    NULL != device->driver->pm &&
	    NULL != device->driver->pm->runtime_suspend)
	{
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->runtime_suspend(device);
	}

	mali_platform_power_mode_change(MALI_POWER_MODE_LIGHT_SLEEP);
	return ret;
}

int mali_light_resume(struct device *device)
{
	int ret = 0;

	mali_platform_power_mode_change(MALI_POWER_MODE_ON);

	if (NULL != device->driver &&
	    NULL != device->driver->pm &&
	    NULL != device->driver->pm->runtime_resume)
	{
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->runtime_resume(device);
	}
	return ret;
}

int mali_deep_suspend(struct device *device)
{
	int ret = 0;

	if (NULL != device->driver &&
	    NULL != device->driver->pm &&
	    NULL != device->driver->pm->suspend)
	{
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->suspend(device);
	}

	mali_platform_power_mode_change(MALI_POWER_MODE_DEEP_SLEEP);
	return ret;
}

int mali_deep_resume(struct device *device)
{
	int ret = 0;

	mali_platform_power_mode_change(MALI_POWER_MODE_ON);

	if (NULL != device->driver &&
	    NULL != device->driver->pm &&
	    NULL != device->driver->pm->resume)
	{
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->resume(device);
	}
	return ret;

}

void mali_core_scaling_term(void)
{

}
#endif /* MESON_CPU_TYPE <= MESON_CPU_TYPE_MESON6 */
