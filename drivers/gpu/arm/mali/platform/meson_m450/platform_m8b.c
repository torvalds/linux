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
#include <linux/gpu_cooling.h>
#include <linux/gpucore_cooling.h>
#include <common/mali_kernel_common.h>
#include <common/mali_osk_profiling.h>
#include <common/mali_pmu.h>

#include "meson_main.h"

/*
 *    For Meson 8B.
 *
 */

#define CFG_PP 2
#define CFG_CLOCK 4
#define CFG_MIN_PP 2
#define CFG_MIN_CLOCK 4

/* fclk is 2550Mhz. */
#define FCLK_DEV3 (6 << 9)		/*	850   Mhz  */
#define FCLK_DEV4 (5 << 9)		/*	637.5 Mhz  */
#define FCLK_DEV5 (7 << 9)		/*	510   Mhz  */
#define FCLK_DEV7 (4 << 9)		/*	364.3 Mhz  */

static u32 mali_dvfs_clk[] = {
	FCLK_DEV5 | 1,     /* 255 Mhz */
	FCLK_DEV7 | 0,     /* 364 Mhz */
	FCLK_DEV3 | 1,     /* 425 Mhz */
	FCLK_DEV5 | 0,     /* 510 Mhz */
	FCLK_DEV4 | 0,     /* 637.5 Mhz */
};

static u32 mali_dvfs_clk_sample[] = {
	255,     /* 182.1 Mhz */
	364,     /* 318.7 Mhz */
	425,     /* 425 Mhz */
	510,     /* 510 Mhz */
	637,     /* 637.5 Mhz */
};

static mali_dvfs_threshold_table mali_dvfs_table[]={
		{ 0, 0, 5, 0  , 180}, /* for 255  */
		{ 1, 1, 5, 152, 205}, /* for 364  */
		{ 2, 2, 5, 180, 212}, /* for 425  */
		{ 3, 3, 5, 205, 236}, /* for 510  */
		{ 4, 4, 5, 230, 256}, /* for 637  */
		{ 0, 0, 5,   0,   0}
};

static void mali_plat_preheat(void);
static mali_plat_info_t mali_plat_data = {
	.cfg_pp = CFG_PP,  /* number of pp. */
	.cfg_min_pp = CFG_MIN_PP,
	.turbo_clock = 4, /* reserved clock src. */
	.def_clock = 2, /* gpu clock used most of time.*/
	.cfg_clock = CFG_CLOCK, /* max gpu clock. */
	.cfg_clock_bkup = CFG_CLOCK,
	.cfg_min_clock = CFG_MIN_CLOCK,

	.sc_mpp = 2, /* number of pp used most of time.*/
	.bst_gpu = 1, /* threshold for boosting gpu. */
	.bst_pp = 1, /* threshold for boosting PP. */

	.clk = mali_dvfs_clk, /* clock source table. */
	.clk_sample = mali_dvfs_clk_sample, /* freqency table for show. */
	.clk_len = sizeof(mali_dvfs_clk) / sizeof(mali_dvfs_clk[0]),
	.have_switch = 1,

	.dvfs_table = mali_dvfs_table, /* DVFS table. */
	.dvfs_table_size = sizeof(mali_dvfs_table) / sizeof(mali_dvfs_threshold_table),

	.scale_info = {
		CFG_MIN_PP, /* minpp */
		CFG_PP, /* maxpp, should be same as cfg_pp */
		CFG_MIN_CLOCK, /* minclk */
		CFG_CLOCK, /* maxclk should be same as cfg_clock */
	},

	.limit_on = 1,
	.plat_preheat = mali_plat_preheat,
};

static void mali_plat_preheat(void)
{
	u32 pre_fs;
	u32 clk, pp;

	if (get_mali_schel_mode() != MALI_PP_FS_SCALING)
		return;

	get_mali_rt_clkpp(&clk, &pp);
	pre_fs = mali_plat_data.def_clock + 1;
	if (clk < pre_fs)
		clk = pre_fs;
	if (pp < mali_plat_data.sc_mpp)
		pp = mali_plat_data.sc_mpp;
	set_mali_rt_clkpp(clk, pp, 1);
}

mali_plat_info_t* get_mali_plat_data(void) {
	return &mali_plat_data;
}

int get_mali_freq_level(int freq)
{
	int i = 0, level = -1;
	int mali_freq_num;

	if(freq < 0)
		return level;
	mali_freq_num = mali_plat_data.dvfs_table_size - 1;
	if(freq <= mali_plat_data.clk_sample[0])
		level = mali_freq_num-1;
	if(freq >= mali_plat_data.clk_sample[mali_freq_num - 1])
		level = 0;
	for(i=0; i<mali_freq_num - 1 ;i++) {
		if(freq >= mali_plat_data.clk_sample[i] && freq<=mali_plat_data.clk_sample[i + 1]) {
			level = i;
			level = mali_freq_num-level - 1;
		}
	}
	return level;
}

unsigned int get_mali_max_level(void)
{
	return mali_plat_data.dvfs_table_size - 1;
}

static struct resource mali_gpu_resources[] =
{
	MALI_GPU_RESOURCES_MALI450_MP2_PMU(IO_MALI_APB_PHY_BASE, INT_MALI_GP, INT_MALI_GP_MMU, 
				INT_MALI_PP0, INT_MALI_PP0_MMU,
				INT_MALI_PP1, INT_MALI_PP1_MMU,
				INT_MALI_PP)
};

static void set_limit_mali_freq(u32 idx)
{
	if (mali_plat_data.limit_on == 0)
		return;
	if (idx > mali_plat_data.turbo_clock || idx < mali_plat_data.scale_info.minclk)
		return;
	mali_plat_data.scale_info.maxclk= idx;
	revise_mali_rt();
}

static u32 get_limit_mali_freq(void)
{
	return mali_plat_data.scale_info.maxclk;
}

static u32 set_limit_pp_num(u32 num)
{
	u32 ret = -1;
	if (mali_plat_data.limit_on == 0)
		goto quit;
	if (num > mali_plat_data.cfg_pp ||
				num < mali_plat_data.scale_info.minpp)
		goto quit;
	mali_plat_data.scale_info.maxpp = num;
	revise_mali_rt();
	ret = 0;
quit:
	return ret;
}

void mali_gpu_utilization_callback(struct mali_gpu_utilization_data *data);
int mali_meson_init_start(struct platform_device* ptr_plt_dev)
{
	struct mali_gpu_device_data* pdev = ptr_plt_dev->dev.platform_data;

	/* for mali platform data. */
	pdev->utilization_interval = 200,
	pdev->utilization_callback = mali_gpu_utilization_callback,

	/* for resource data. */
	ptr_plt_dev->num_resources = ARRAY_SIZE(mali_gpu_resources);
	ptr_plt_dev->resource = mali_gpu_resources;
	return mali_clock_init(&mali_plat_data);
}

int mali_meson_init_finish(struct platform_device* ptr_plt_dev)
{
#ifdef CONFIG_GPU_THERMAL
	int err;
	struct gpufreq_cooling_device *gcdev = NULL;
#endif
	if (mali_core_scaling_init(&mali_plat_data) < 0)
		return -1;

#ifdef CONFIG_GPU_THERMAL
	gcdev = gpufreq_cooling_alloc();
	if(IS_ERR(gcdev))
		printk("malloc gpu cooling buffer error!!\n");
	else if(!gcdev)
		printk("system does not enable thermal driver\n");
	else {
		gcdev->get_gpu_freq_level = get_mali_freq_level;
		gcdev->get_gpu_max_level = get_mali_max_level;
		gcdev->set_gpu_freq_idx = set_limit_mali_freq;
		gcdev->get_gpu_current_max_level = get_limit_mali_freq;
		err = gpufreq_cooling_register(gcdev);
		if(err < 0)
			printk("register GPU  cooling error\n");
		printk("gpu cooling register okay with err=%d\n",err);
	}

#endif
	return 0;
}

int mali_meson_uninit(struct platform_device* ptr_plt_dev)
{
	return 0;
}

static int mali_cri_light_suspend(size_t param)
{
	int ret;
	struct device *device;
	struct mali_pmu_core *pmu;

	ret = 0;
	mali_pm_statue = 0;
	device = (struct device *)param;
	pmu = mali_pmu_get_global_pmu_core();

	if (NULL != device->driver &&
	    NULL != device->driver->pm &&
	    NULL != device->driver->pm->runtime_suspend)
	{
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->runtime_suspend(device);
	}
	mali_pmu_power_down_all(pmu);
	return ret;
}

static int mali_cri_light_resume(size_t param)
{
	int ret;
	struct device *device;
	struct mali_pmu_core *pmu;

	ret = 0;
	device = (struct device *)param;
	pmu = mali_pmu_get_global_pmu_core();

	mali_pmu_power_up_all(pmu);
	if (NULL != device->driver &&
	    NULL != device->driver->pm &&
	    NULL != device->driver->pm->runtime_resume)
	{
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->runtime_resume(device);
	}
	mali_pm_statue = 1;
	return ret;
}

static int mali_cri_deep_suspend(size_t param)
{
	int ret;
	struct device *device;
	struct mali_pmu_core *pmu;

	ret = 0;
	device = (struct device *)param;
	pmu = mali_pmu_get_global_pmu_core();

	if (NULL != device->driver &&
	    NULL != device->driver->pm &&
	    NULL != device->driver->pm->suspend)
	{
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->suspend(device);
	}
	mali_pmu_power_down_all(pmu);
	return ret;
}

static int mali_cri_deep_resume(size_t param)
{
	int ret;
	struct device *device;
	struct mali_pmu_core *pmu;

	ret = 0;
	device = (struct device *)param;
	pmu = mali_pmu_get_global_pmu_core();

	mali_pmu_power_up_all(pmu);
	if (NULL != device->driver &&
	    NULL != device->driver->pm &&
	    NULL != device->driver->pm->resume)
	{
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->resume(device);
	}
	return ret;

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

	/* clock scaling. Kasin..*/
	ret = mali_clock_critical(mali_cri_light_suspend, (size_t)device);
	disable_clock();
	return ret;
}

int mali_light_resume(struct device *device)
{
	int ret = 0;
	enable_clock();
	ret = mali_clock_critical(mali_cri_light_resume, (size_t)device);
#ifdef CONFIG_MALI400_PROFILING
	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE |
					MALI_PROFILING_EVENT_CHANNEL_GPU |
					MALI_PROFILING_EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE,
					get_current_frequency(), 0,	0,	0,	0);
#endif
	return ret;
}

int mali_deep_suspend(struct device *device)
{
	int ret = 0;
	enable_clock();
	flush_scaling_job();

	/* clock scaling off. Kasin... */
	ret = mali_clock_critical(mali_cri_deep_suspend, (size_t)device);
	disable_clock();
	return ret;
}

int mali_deep_resume(struct device *device)
{
	int ret = 0;

	/* clock scaling up. Kasin.. */
	enable_clock();
	ret = mali_clock_critical(mali_cri_deep_resume, (size_t)device);
	return ret;

}

