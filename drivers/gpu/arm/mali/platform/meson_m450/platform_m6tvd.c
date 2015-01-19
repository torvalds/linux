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
#include <linux/module.h>            /* kernel module definitions */
#include <linux/ioport.h>            /* request_mem_region */
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

/*
 *    For Meson 8TVD.
 *
 */

#define CFG_PP 2
#define CFG_CLOCK 3
#define CFG_MIN_PP 1
#define CFG_MIN_CLOCK 0

/* fclk is 2Ghz. */
#define FCLK_DEV5 (7 << 9)		/*	400   Mhz  */
#define FCLK_DEV3 (6 << 9)		/*	666   Mhz  */
#define FCLK_DEV2 (5 << 9)		/*	1000  Mhz  */
#define FCLK_DEV7 (4 << 9)		/*	285   Mhz  */

u32 mali_dvfs_clk[] = {
	FCLK_DEV7 | 9,     /* 100 Mhz */
	FCLK_DEV2 | 4,     /* 200 Mhz */
	FCLK_DEV3 | 1,     /* 333 Mhz */
	FCLK_DEV5 | 0,     /* 400 Mhz */
};

u32 mali_dvfs_clk_sample[] = {
	100,     /* 182.1 Mhz */
	200,     /* 318.7 Mhz */
	333,     /* 425 Mhz */
	400,     /* 510 Mhz */
};

static mali_plat_info_t mali_plat_data = {
	.cfg_pp = CFG_PP,  /* number of pp. */
	.cfg_min_pp = CFG_MIN_PP,
	.def_clock = CFG_CLOCK, /* gpu clock used most of time.*/
	.cfg_clock = CFG_CLOCK, /* max gpu clock. */
	.cfg_min_clock = CFG_MIN_CLOCK,

	.clk = mali_dvfs_clk, /* clock source table. */
	.clk_sample = mali_dvfs_clk_sample, /* freqency table for show. */
	.clk_len = sizeof(mali_dvfs_clk) / sizeof(mali_dvfs_clk[0]),
	.have_switch = 0,
};

#define MALI_USER_PP0	AM_IRQ4(31)

static struct resource mali_gpu_resources[] =
{
MALI_GPU_RESOURCES_MALI450_MP2_PMU(0xC9140000, INT_MALI_GP, INT_MALI_GP_MMU,
				MALI_USER_PP0, INT_MALI_PP_MMU,
				INT_MALI_PP1, INT_MALI_PP_MMU1,
				INT_MALI_PP)
};

int mali_meson_init_start(struct platform_device* ptr_plt_dev)
{
	ptr_plt_dev->num_resources = ARRAY_SIZE(mali_gpu_resources);
	ptr_plt_dev->resource = mali_gpu_resources;
	return mali_clock_init(&mali_plat_data);
}

int mali_meson_init_finish(struct platform_device* ptr_plt_dev)
{
	return 0;
}

int mali_meson_uninit(struct platform_device* ptr_plt_dev)
{
	return 0;
}

static int mali_cri_pmu_on_off(size_t param)
{
	struct mali_pmu_core *pmu;

	MALI_DEBUG_PRINT(4, ("mali_os_suspend() called\n"));
	pmu = mali_pmu_get_global_pmu_core();
	if (param == 0)
		mali_pmu_power_down_all(pmu);
	else
		mali_pmu_power_up_all(pmu);
	return 0;
}

int mali_light_suspend(struct device *device)
{
	int ret = 0;
#ifdef CONFIG_MALI400_PROFILING
	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE |
					MALI_PROFILING_EVENT_CHANNEL_GPU |
					MALI_PROFILING_EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE,
					0, 0,	0,	0,	0);
#endif
	if (NULL != device->driver &&
	    NULL != device->driver->pm &&
	    NULL != device->driver->pm->runtime_suspend)
	{
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->runtime_suspend(device);
	}

	/* clock scaling. Kasin..*/
	mali_clock_critical(mali_cri_pmu_on_off, 0);
	disable_clock();
	return ret;
}

int mali_light_resume(struct device *device)
{
	int ret = 0;
	/* clock scaling. Kasin..*/
	enable_clock();

	mali_clock_critical(mali_cri_pmu_on_off, 1);
#ifdef CONFIG_MALI400_PROFILING
	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE |
					MALI_PROFILING_EVENT_CHANNEL_GPU |
					MALI_PROFILING_EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE,
					0, 0,	0,	0,	0);
#endif

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
	//enable_clock();
	//flush_scaling_job();
	if (NULL != device->driver &&
	    NULL != device->driver->pm &&
	    NULL != device->driver->pm->suspend)
	{
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->suspend(device);
	}

	/* clock scaling off. Kasin... */
	mali_clock_critical(mali_cri_pmu_on_off, 0);
	disable_clock();
	return ret;
}

int mali_deep_resume(struct device *device)
{
	int ret = 0;
	/* clock scaling up. Kasin.. */
	enable_clock();
	mali_clock_critical(mali_cri_pmu_on_off, 1);
	if (NULL != device->driver &&
	    NULL != device->driver->pm &&
	    NULL != device->driver->pm->resume)
	{
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->resume(device);
	}
	return ret;

}

