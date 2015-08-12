/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */

// #define ENABLE_DEBUG_LOG
#include "custom_log.h"

#include <linux/ioport.h>
#include <mali_kbase.h>
#include <mali_kbase_defs.h>
#include <mali_kbase_config.h>
#ifdef CONFIG_UMP
#include <linux/ump-common.h>
#endif				/* CONFIG_UMP */
#include <platform/rk/mali_kbase_platform.h>
#include <platform/rk/mali_kbase_dvfs.h>
#include <linux/pm_runtime.h>
#include <linux/suspend.h>
#include <linux/reboot.h>
/**
 * @file mali_kbase_config_rk.c
 * 对 platform_config_of_rk 的具体实现.
 * 
 * mali_device_driver 包含两部分 : 
 *      .DP : platform_dependent_part_in_mdd : 依赖 platform 部分, 源码在 <mdd_src_dir>/platform/<platform_name> 目录下.
 *			在 mali_device_driver 内部, 记为 platform_dependent_part.
 *      .DP : common_parts_in_mdd : arm 实现的通用的部分, 源码在 <mdd_src_dir> 目录下.
 *			在 mali_device_driver 内部, 记为 common_parts.
 */

int get_cpu_clock_speed(u32 *cpu_clock);

#define HZ_IN_MHZ                           (1000000)
#ifdef CONFIG_MALI_MIDGARD_RT_PM
#define RUNTIME_PM_DELAY_TIME 50
#endif

/* Versatile Express (VE) configuration defaults shared between config_attributes[]
 * and config_attributes_hw_issue_8408[]. Settings are not shared for
 * JS_HARD_STOP_TICKS_SS and JS_RESET_TICKS_SS.
 */
#define KBASE_VE_MEMORY_PER_PROCESS_LIMIT       (512 * 1024 * 1024UL)	/* 512MB */
#define KBASE_VE_MEMORY_OS_SHARED_MAX           (2048 * 1024 * 1024UL)	/* 768MB */
#define KBASE_VE_MEMORY_OS_SHARED_PERF_GPU      KBASE_MEM_PERF_FAST/*KBASE_MEM_PERF_SLOW*/
#define KBASE_VE_GPU_FREQ_KHZ_MAX               500000
#define KBASE_VE_GPU_FREQ_KHZ_MIN               100000
#ifdef CONFIG_UMP
#define KBASE_VE_UMP_DEVICE                     UMP_DEVICE_Z_SHIFT
#endif				/* CONFIG_UMP */

#define KBASE_VE_JS_SCHEDULING_TICK_NS_DEBUG    15000000u      /* 15ms, an agressive tick for testing purposes. This will reduce performance significantly */
#define KBASE_VE_JS_SOFT_STOP_TICKS_DEBUG       1	/* between 15ms and 30ms before soft-stop a job */
#define KBASE_VE_JS_HARD_STOP_TICKS_SS_DEBUG    333	/* 5s before hard-stop */
#define KBASE_VE_JS_HARD_STOP_TICKS_SS_8401_DEBUG 2000	/* 30s before hard-stop, for a certain GLES2 test at 128x128 (bound by combined vertex+tiler job) - for issue 8401 */
#define KBASE_VE_JS_HARD_STOP_TICKS_NSS_DEBUG   100000	/* 1500s (25mins) before NSS hard-stop */
#define KBASE_VE_JS_RESET_TICKS_SS_DEBUG        500	/* 45s before resetting GPU, for a certain GLES2 test at 128x128 (bound by combined vertex+tiler job) */
#define KBASE_VE_JS_RESET_TICKS_SS_8401_DEBUG   3000	/* 7.5s before resetting GPU - for issue 8401 */
#define KBASE_VE_JS_RESET_TICKS_NSS_DEBUG       100166	/* 1502s before resetting GPU */

#define KBASE_VE_JS_SCHEDULING_TICK_NS          2500000000u	/* 2.5s */
#define KBASE_VE_JS_SOFT_STOP_TICKS             1	/* 2.5s before soft-stop a job */
#define KBASE_VE_JS_HARD_STOP_TICKS_SS          2	/* 5s before hard-stop */
#define KBASE_VE_JS_HARD_STOP_TICKS_SS_8401     12	/* 30s before hard-stop, for a certain GLES2 test at 128x128 (bound by combined vertex+tiler job) - for issue 8401 */
#define KBASE_VE_JS_HARD_STOP_TICKS_NSS         600	/* 1500s before NSS hard-stop */
#define KBASE_VE_JS_RESET_TICKS_SS              3	/* 7.5s before resetting GPU */
#define KBASE_VE_JS_RESET_TICKS_SS_8401         18	/* 45s before resetting GPU, for a certain GLES2 test at 128x128 (bound by combined vertex+tiler job) - for issue 8401 */
#define KBASE_VE_JS_RESET_TICKS_NSS             601	/* 1502s before resetting GPU */

#define KBASE_VE_JS_RESET_TIMEOUT_MS            500	/* 3s before cancelling stuck jobs */
#define KBASE_VE_JS_CTX_TIMESLICE_NS            1000000	/* 1ms - an agressive timeslice for testing purposes (causes lots of scheduling out for >4 ctxs) */
#define KBASE_VE_SECURE_BUT_LOSS_OF_PERFORMANCE	((uintptr_t)MALI_FALSE)	/* By default we prefer performance over security on r0p0-15dev0 and KBASE_CONFIG_ATTR_ earlier */
/*#define KBASE_VE_POWER_MANAGEMENT_CALLBACKS     ((uintptr_t)&pm_callbacks)*/
#define KBASE_VE_CPU_SPEED_FUNC                 ((uintptr_t)&get_cpu_clock_speed)

static int mali_pm_notifier(struct notifier_block *nb,unsigned long event,void* cmd);
static struct notifier_block mali_pm_nb = {
	.notifier_call = mali_pm_notifier
};
static int mali_reboot_notifier_event(struct notifier_block *this, unsigned long event, void *ptr)
{

	pr_info("%s enter\n",__func__);
	if (kbase_platform_dvfs_enable(false, MALI_DVFS_CURRENT_FREQ)!= MALI_TRUE)
		return -EPERM;
	pr_info("%s exit\n",__func__);
	return NOTIFY_OK;
}

static struct notifier_block mali_reboot_notifier = {
	.notifier_call = mali_reboot_notifier_event,
};

#ifndef CONFIG_OF
static kbase_io_resources io_resources = {
	.job_irq_number = 68,
	.mmu_irq_number = 69,
	.gpu_irq_number = 70,
	.io_memory_region = {
			     .start = 0xFC010000,
			     .end = 0xFC010000 + (4096 * 5) - 1}
};
#endif

int get_cpu_clock_speed(u32 *cpu_clock)
{
#if 0
	struct clk *cpu_clk;
	u32 freq = 0;
	cpu_clk = clk_get(NULL, "armclk");
	if (IS_ERR(cpu_clk))
		return 1;
	freq = clk_get_rate(cpu_clk);
	*cpu_clock = (freq / HZ_IN_MHZ);
#endif
	return 0;
}

static int mali_pm_notifier(struct notifier_block *nb,unsigned long event,void* cmd)
{
	int err = NOTIFY_OK;
	switch (event) {
		case PM_SUSPEND_PREPARE:
#ifdef CONFIG_MALI_MIDGARD_DVFS
			/*
			pr_info("%s,PM_SUSPEND_PREPARE\n",__func__);
			*/
			if (kbase_platform_dvfs_enable(false, p_mali_dvfs_infotbl[0].clock)!= MALI_TRUE)
				err = NOTIFY_BAD;
#endif
			break;
		case PM_POST_SUSPEND:
#ifdef CONFIG_MALI_MIDGARD_DVFS
			/*
			pr_info("%s,PM_POST_SUSPEND\n",__func__);
			*/
			if (kbase_platform_dvfs_enable(true, p_mali_dvfs_infotbl[0].clock)!= MALI_TRUE)
				err = NOTIFY_BAD;
#endif
			break;
		default:
			break;
	}
	return err;
}

/*
  rk3288 hardware specific initialization
 */
int kbase_platform_rk_init(struct kbase_device *kbdev)
{
 	if(MALI_ERROR_NONE == kbase_platform_init(kbdev))
 	{
		if (register_pm_notifier(&mali_pm_nb)) {
                        E("fail to register pm_notifier.");
			return -1;
		}
		
		pr_info("%s,register_reboot_notifier\n",__func__);
		register_reboot_notifier(&mali_reboot_notifier);
 		return 0;
 	}
        
        E("fail to init platform.");
	return -1;
}

/*
 rk3288  hardware specific termination
*/
void kbase_platform_rk_term(struct kbase_device *kbdev)
{
	unregister_pm_notifier(&mali_pm_nb);
#ifdef CONFIG_MALI_MIDGARD_DEBUG_SYS
	kbase_platform_remove_sysfs_file(kbdev->dev);
#endif				/* CONFIG_MALI_MIDGARD_DEBUG_SYS */
	kbase_platform_term(kbdev);
}

struct kbase_platform_funcs_conf platform_funcs = 
{
	.platform_init_func = &kbase_platform_rk_init,
	.platform_term_func = &kbase_platform_rk_term,
};

#ifdef CONFIG_MALI_MIDGARD_RT_PM
static int pm_callback_power_on(struct kbase_device *kbdev)
{
	int result;
	int ret_val;
	struct device *dev = kbdev->dev;
	struct rk_context *platform;
	platform = (struct rk_context *)kbdev->platform_context;

	/* 若 mali_device 是 suspended 的, 则... */
	if (pm_runtime_status_suspended(dev))
	{
		/* 预置返回 1, 表征 gpu_state 可能已经 lost 了. */
		ret_val = 1;
	}
	else
	{
		ret_val = 0;
	}

	if(dev->power.disable_depth > 0) {
		if(platform->cmu_pmu_status == 0)
		{
			/* 使能 gpu_power_domain 和 clk_of_gpu_dvfs_node. */
			kbase_platform_cmu_pmu_control(kbdev, 1);
		}
		return ret_val;
	}

	result = pm_runtime_resume(dev);

	// if (result < 0 && result == -EAGAIN)
	if ( -EAGAIN == result )
		kbase_platform_cmu_pmu_control(kbdev, 1);
	else if (result < 0)
		printk(KERN_ERR "pm_runtime_get_sync failed (%d)\n", result);

	return ret_val;
}

static void pm_callback_power_off(struct kbase_device *kbdev)
{
	struct device *dev = kbdev->dev;
	pm_schedule_suspend(dev, RUNTIME_PM_DELAY_TIME);
}

int kbase_device_runtime_init(struct kbase_device *kbdev)
{
	pm_suspend_ignore_children(kbdev->dev, true);
	/* 对 mali_device 使能 runtime_pm. */
	pm_runtime_enable(kbdev->dev);
#ifdef CONFIG_MALI_MIDGARD_DEBUG_SYS
	if (kbase_platform_create_sysfs_file(kbdev->dev))
		return MALI_ERROR_FUNCTION_FAILED;
#endif				/* CONFIG_MALI_MIDGARD_DEBUG_SYS */
	return MALI_ERROR_NONE;
}

void kbase_device_runtime_disable(struct kbase_device *kbdev)
{
	pm_runtime_disable(kbdev->dev);
}

static int pm_callback_runtime_on(struct kbase_device *kbdev)
{
#ifdef CONFIG_MALI_MIDGARD_DVFS	
	struct rk_context *platform = (struct rk_context *)kbdev->platform_context;
	unsigned long flags;
	unsigned int clock;
#endif

	kbase_platform_power_on(kbdev);

	kbase_platform_clock_on(kbdev);
#ifdef CONFIG_MALI_MIDGARD_DVFS
	if (platform->dvfs_enabled) {
		if(platform->gpu_in_touch) {
			clock = p_mali_dvfs_infotbl[MALI_DVFS_STEP-1].clock;
			spin_lock_irqsave(&platform->gpu_in_touch_lock, flags);
			platform->gpu_in_touch = false;
			spin_unlock_irqrestore(&platform->gpu_in_touch_lock, flags);
		} else {
			clock = MALI_DVFS_CURRENT_FREQ;
		}
		/*
		pr_info("%s,clock = %d\n",__func__,clock);
		*/
		if (kbase_platform_dvfs_enable(true, clock)!= MALI_TRUE)
			return -EPERM;

	} else {
		if (kbase_platform_dvfs_enable(false, MALI_DVFS_CURRENT_FREQ)!= MALI_TRUE)
			return -EPERM;
	}
#endif	
	return 0;
}

static void pm_callback_runtime_off(struct kbase_device *kbdev)
{
#ifdef CONFIG_MALI_MIDGARD_DVFS	
	struct rk_context *platform = (struct rk_context *)kbdev->platform_context;
	unsigned long flags;
#endif

	kbase_platform_clock_off(kbdev);
	kbase_platform_power_off(kbdev);
#ifdef CONFIG_MALI_MIDGARD_DVFS
	if (platform->dvfs_enabled)
	{
		/*printk("%s\n",__func__);*/
		if (kbase_platform_dvfs_enable(false, p_mali_dvfs_infotbl[0].clock)!= MALI_TRUE)
			printk("[err] disabling dvfs is faled\n");
		spin_lock_irqsave(&platform->gpu_in_touch_lock, flags);
		platform->gpu_in_touch = false;
		spin_unlock_irqrestore(&platform->gpu_in_touch_lock, flags);
	}
#endif
}

struct kbase_pm_callback_conf pm_callbacks = {
	.power_on_callback = pm_callback_power_on,
	.power_off_callback = pm_callback_power_off,
#ifdef CONFIG_PM_RUNTIME
	.power_runtime_init_callback = kbase_device_runtime_init,
	.power_runtime_term_callback = kbase_device_runtime_disable,
	.power_runtime_on_callback = pm_callback_runtime_on,
	.power_runtime_off_callback = pm_callback_runtime_off,

#else				/* CONFIG_PM_RUNTIME */
	.power_runtime_init_callback = NULL,
	.power_runtime_term_callback = NULL,
	.power_runtime_on_callback = NULL,
	.power_runtime_off_callback = NULL,

#endif				/* CONFIG_PM_RUNTIME */
};
#endif

int kbase_platform_early_init(void)
{
	/* Nothing needed at this stage */
	return 0;
}
