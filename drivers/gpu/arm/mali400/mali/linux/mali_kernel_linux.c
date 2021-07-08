/**
 * Copyright (C) 2010-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */


/**
 * @file mali_kernel_linux.c
 * Implementation of the Linux device driver entrypoints
 */
#include "../platform/rk/custom_log.h"
#include "../platform/rk/rk_ext.h"

#include <linux/module.h>   /* kernel module definitions */
#include <linux/fs.h>       /* file system operations */
#include <linux/cdev.h>     /* character device definitions */
#include <linux/mm.h>       /* memory manager definitions */
#include <linux/mali/mali_utgard_ioctl.h>
#include <linux/version.h>
#include <linux/device.h>
#include "mali_kernel_license.h"
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/bug.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/mali/mali_utgard.h>
#include <soc/rockchip/rockchip_opp_select.h>

#include "mali_kernel_common.h"
#include "mali_session.h"
#include "mali_kernel_core.h"
#include "mali_osk.h"
#include "mali_kernel_linux.h"
#include "mali_ukk.h"
#include "mali_ukk_wrappers.h"
#include "mali_kernel_sysfs.h"
#include "mali_pm.h"
#include "mali_kernel_license.h"
#include "mali_memory.h"
#include "mali_memory_dma_buf.h"
#include "mali_memory_manager.h"
#include "mali_memory_swap_alloc.h"
#if defined(CONFIG_MALI400_INTERNAL_PROFILING)
#include "mali_profiling_internal.h"
#endif
#if defined(CONFIG_MALI400_PROFILING) && defined(CONFIG_MALI_DVFS)
#include "mali_osk_profiling.h"
#include "mali_dvfs_policy.h"

static int is_first_resume = 1;
/*Store the clk and vol for boot/insmod and mali_resume*/
static struct mali_gpu_clk_item mali_gpu_clk[2];
#endif

/* Streamline support for the Mali driver */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_MALI400_PROFILING)
/* Ask Linux to create the tracepoints */
#define CREATE_TRACE_POINTS
#include "mali_linux_trace.h"

EXPORT_TRACEPOINT_SYMBOL_GPL(mali_timeline_event);
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_hw_counter);
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_sw_counters);
#endif /* CONFIG_TRACEPOINTS */

#ifdef CONFIG_MALI_DEVFREQ
#include "mali_devfreq.h"
#include "mali_osk_mali.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
#include <linux/pm_opp.h>
#else
/* In 3.13 the OPP include header file, types, and functions were all
 * renamed. Use the old filename for the include, and define the new names to
 * the old, when an old kernel is detected.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
#include <linux/pm_opp.h>
#else
#include <linux/opp.h>
#endif /* Linux >= 3.13*/
#define dev_pm_opp_of_add_table of_init_opp_table
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
#define dev_pm_opp_of_remove_table of_free_opp_table
#endif /* Linux >= 3.19 */
#endif /* Linux >= 4.4.0 */
#endif

/* from the __malidrv_build_info.c file that is generated during build */
extern const char *__malidrv_build_info(void);

/* Module parameter to control log level */
int mali_debug_level = 2;
module_param(mali_debug_level, int, S_IRUSR | S_IWUSR | S_IWGRP | S_IRGRP | S_IROTH); /* rw-rw-r-- */
MODULE_PARM_DESC(mali_debug_level, "Higher number, more dmesg output");

extern int mali_max_job_runtime;
module_param(mali_max_job_runtime, int, S_IRUSR | S_IWUSR | S_IWGRP | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(mali_max_job_runtime, "Maximum allowed job runtime in msecs.\nJobs will be killed after this no matter what");

extern int mali_l2_max_reads;
module_param(mali_l2_max_reads, int, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(mali_l2_max_reads, "Maximum reads for Mali L2 cache");

extern unsigned int mali_dedicated_mem_start;
module_param(mali_dedicated_mem_start, uint, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(mali_dedicated_mem_start, "Physical start address of dedicated Mali GPU memory.");

extern unsigned int mali_dedicated_mem_size;
module_param(mali_dedicated_mem_size, uint, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(mali_dedicated_mem_size, "Size of dedicated Mali GPU memory.");

extern unsigned int mali_shared_mem_size;
module_param(mali_shared_mem_size, uint, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(mali_shared_mem_size, "Size of shared Mali GPU memory.");

#if defined(CONFIG_MALI400_PROFILING)
extern int mali_boot_profiling;
module_param(mali_boot_profiling, int, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(mali_boot_profiling, "Start profiling as a part of Mali driver initialization");
#endif

extern int mali_max_pp_cores_group_1;
module_param(mali_max_pp_cores_group_1, int, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(mali_max_pp_cores_group_1, "Limit the number of PP cores to use from first PP group.");

extern int mali_max_pp_cores_group_2;
module_param(mali_max_pp_cores_group_2, int, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(mali_max_pp_cores_group_2, "Limit the number of PP cores to use from second PP group (Mali-450 only).");

extern unsigned int mali_mem_swap_out_threshold_value;
module_param(mali_mem_swap_out_threshold_value, uint, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(mali_mem_swap_out_threshold_value, "Threshold value used to limit how much swappable memory cached in Mali driver.");

#if defined(CONFIG_MALI_DVFS)
/** the max fps the same as display vsync default 60, can set by module insert parameter */
extern int mali_max_system_fps;
module_param(mali_max_system_fps, int, S_IRUSR | S_IWUSR | S_IWGRP | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(mali_max_system_fps, "Max system fps the same as display VSYNC.");

/** a lower limit on their desired FPS default 58, can set by module insert parameter*/
extern int mali_desired_fps;
module_param(mali_desired_fps, int, S_IRUSR | S_IWUSR | S_IWGRP | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(mali_desired_fps, "A bit lower than max_system_fps which user desired fps");
#endif

#if MALI_ENABLE_CPU_CYCLES
#include <linux/cpumask.h>
#include <linux/timer.h>
#include <asm/smp.h>
static struct timer_list mali_init_cpu_clock_timers[8];
static u32 mali_cpu_clock_last_value[8] = {0,};
#endif

/* Export symbols from common code: mali_user_settings.c */
#include "mali_user_settings_db.h"
EXPORT_SYMBOL(mali_set_user_setting);
EXPORT_SYMBOL(mali_get_user_setting);

static char mali_dev_name[] = "mali"; /* should be const, but the functions we call requires non-cost */

/* This driver only supports one Mali device, and this variable stores this single platform device */
struct platform_device *mali_platform_device = NULL;

/* This driver only supports one Mali device, and this variable stores the exposed misc device (/dev/mali) */
static struct miscdevice mali_miscdevice = { 0, };

static int mali_miscdevice_register(struct platform_device *pdev);
static void mali_miscdevice_unregister(void);

static int mali_open(struct inode *inode, struct file *filp);
static int mali_release(struct inode *inode, struct file *filp);
static long mali_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

static int mali_probe(struct platform_device *pdev);
static int mali_remove(struct platform_device *pdev);

static int mali_driver_suspend_scheduler(struct device *dev);
static int mali_driver_resume_scheduler(struct device *dev);

#ifdef CONFIG_PM_RUNTIME
static int mali_driver_runtime_suspend(struct device *dev);
static int mali_driver_runtime_resume(struct device *dev);
static int mali_driver_runtime_idle(struct device *dev);
#endif

#if defined(MALI_FAKE_PLATFORM_DEVICE)
#if defined(CONFIG_MALI_DT)
extern int mali_platform_device_init(struct platform_device *device);
extern int mali_platform_device_deinit(struct platform_device *device);
#else
extern int mali_platform_device_register(void);
extern int mali_platform_device_unregister(void);
#endif
#endif

extern int rk_platform_init_opp_table(struct device *dev);

/* Linux power management operations provided by the Mali device driver */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 29))
struct pm_ext_ops mali_dev_ext_pm_ops = {
	.base =
	{
		.suspend = mali_driver_suspend_scheduler,
		.resume = mali_driver_resume_scheduler,
		.freeze = mali_driver_suspend_scheduler,
		.thaw =   mali_driver_resume_scheduler,
	},
};
#else
static const struct dev_pm_ops mali_dev_pm_ops = {
#ifdef CONFIG_PM_RUNTIME
	.runtime_suspend = mali_driver_runtime_suspend,
	.runtime_resume = mali_driver_runtime_resume,
	.runtime_idle = mali_driver_runtime_idle,
#endif
	.suspend = mali_driver_suspend_scheduler,
	.resume = mali_driver_resume_scheduler,
	.freeze = mali_driver_suspend_scheduler,
	.thaw = mali_driver_resume_scheduler,
	.poweroff = mali_driver_suspend_scheduler,
};
#endif

#ifdef CONFIG_MALI_DT
static struct of_device_id base_dt_ids[] = {
	{.compatible = "arm,mali-300"},
    /*-------------------------------------------------------*/
    /* rk_ext : to use dts_for_mali_ko_befor_r5p0-01rel0. */
	// {.compatible = "arm,mali-400"},
	{.compatible = "arm,mali400"},
    /*-------------------------------------------------------*/
	{.compatible = "arm,mali-450"},
	{.compatible = "arm,mali-470"},
	{},
};

MODULE_DEVICE_TABLE(of, base_dt_ids);
#endif

/* The Mali device driver struct */
static struct platform_driver mali_platform_driver = {
	.probe  = mali_probe,
	.remove = mali_remove,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 29))
	.pm = &mali_dev_ext_pm_ops,
#endif
	.driver =
	{
		.name   = MALI_GPU_NAME_UTGARD,
		.owner  = THIS_MODULE,
		.bus = &platform_bus_type,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
		.pm = &mali_dev_pm_ops,
#endif
#ifdef CONFIG_MALI_DT
		.of_match_table = of_match_ptr(base_dt_ids),
#endif
	},
};

/* Linux misc device operations (/dev/mali) */
struct file_operations mali_fops = {
	.owner = THIS_MODULE,
	.open = mali_open,
	.release = mali_release,
	.unlocked_ioctl = mali_ioctl,
	.compat_ioctl = mali_ioctl,
	.mmap = mali_mmap
};

#if MALI_ENABLE_CPU_CYCLES
void mali_init_cpu_time_counters(int reset, int enable_divide_by_64)
{
	/* The CPU assembly reference used is: ARM Architecture Reference Manual ARMv7-AR C.b */
	u32 write_value;

	/* See B4.1.116 PMCNTENSET, Performance Monitors Count Enable Set register, VMSA */
	/* setting p15 c9 c12 1 to 0x8000000f==CPU_CYCLE_ENABLE |EVENT_3_ENABLE|EVENT_2_ENABLE|EVENT_1_ENABLE|EVENT_0_ENABLE */
	asm volatile("mcr p15, 0, %0, c9, c12, 1" :: "r"(0x8000000f));


	/* See B4.1.117 PMCR, Performance Monitors Control Register. Writing to p15, c9, c12, 0 */
	write_value = 1 << 0; /* Bit 0 set. Enable counters */
	if (reset) {
		write_value |= 1 << 1; /* Reset event counters */
		write_value |= 1 << 2; /* Reset cycle counter  */
	}
	if (enable_divide_by_64) {
		write_value |= 1 << 3; /* Enable the Clock divider by 64 */
	}
	write_value |= 1 << 4; /* Export enable. Not needed */
	asm volatile("MCR p15, 0, %0, c9, c12, 0\t\n" :: "r"(write_value));

	/* PMOVSR Overflow Flag Status Register - Clear Clock and Event overflows */
	asm volatile("MCR p15, 0, %0, c9, c12, 3\t\n" :: "r"(0x8000000f));


	/* See B4.1.124 PMUSERENR - setting p15 c9 c14 to 1" */
	/* User mode access to the Performance Monitors enabled. */
	/* Lets User space read cpu clock cycles */
	asm volatile("mcr p15, 0, %0, c9, c14, 0" :: "r"(1));
}

/** A timer function that configures the cycle clock counter on current CPU.
 * The function \a mali_init_cpu_time_counters_on_all_cpus sets up this
 * function to trigger on all Cpus during module load.
 */
static void mali_init_cpu_clock_timer_func(unsigned long data)
{
	int reset_counters, enable_divide_clock_counter_by_64;
	int current_cpu = raw_smp_processor_id();
	unsigned int sample0;
	unsigned int sample1;

	MALI_IGNORE(data);

	reset_counters = 1;
	enable_divide_clock_counter_by_64 = 0;
	mali_init_cpu_time_counters(reset_counters, enable_divide_clock_counter_by_64);

	sample0 = mali_get_cpu_cyclecount();
	sample1 = mali_get_cpu_cyclecount();

	MALI_DEBUG_PRINT(3, ("Init Cpu %d cycle counter- First two samples: %08x %08x \n", current_cpu, sample0, sample1));
}

/** A timer functions for storing current time on all cpus.
 * Used for checking if the clocks have similar values or if they are drifting.
 */
static void mali_print_cpu_clock_timer_func(unsigned long data)
{
	int current_cpu = raw_smp_processor_id();
	unsigned int sample0;

	MALI_IGNORE(data);
	sample0 = mali_get_cpu_cyclecount();
	if (current_cpu < 8) {
		mali_cpu_clock_last_value[current_cpu] = sample0;
	}
}

/** Init the performance registers on all CPUs to count clock cycles.
 * For init \a print_only should be 0.
 * If \a print_only is 1, it will intead print the current clock value of all CPUs.
 */
void mali_init_cpu_time_counters_on_all_cpus(int print_only)
{
	int i = 0;
	int cpu_number;
	int jiffies_trigger;
	int jiffies_wait;

	jiffies_wait = 2;
	jiffies_trigger = jiffies + jiffies_wait;

	for (i = 0 ; i < 8 ; i++) {
		init_timer(&mali_init_cpu_clock_timers[i]);
		if (print_only) mali_init_cpu_clock_timers[i].function = mali_print_cpu_clock_timer_func;
		else            mali_init_cpu_clock_timers[i].function = mali_init_cpu_clock_timer_func;
		mali_init_cpu_clock_timers[i].expires = jiffies_trigger ;
	}
	cpu_number = cpumask_first(cpu_online_mask);
	for (i = 0 ; i < 8 ; i++) {
		int next_cpu;
		add_timer_on(&mali_init_cpu_clock_timers[i], cpu_number);
		next_cpu = cpumask_next(cpu_number, cpu_online_mask);
		if (next_cpu >= nr_cpu_ids) break;
		cpu_number = next_cpu;
	}

	while (jiffies_wait) jiffies_wait = schedule_timeout_uninterruptible(jiffies_wait);

	for (i = 0 ; i < 8 ; i++) {
		del_timer_sync(&mali_init_cpu_clock_timers[i]);
	}

	if (print_only) {
		if ((0 == mali_cpu_clock_last_value[2]) && (0 == mali_cpu_clock_last_value[3])) {
			/* Diff can be printed if we want to check if the clocks are in sync
			int diff = mali_cpu_clock_last_value[0] - mali_cpu_clock_last_value[1];*/
			MALI_DEBUG_PRINT(2, ("CPU cycle counters readout all: %08x %08x\n", mali_cpu_clock_last_value[0], mali_cpu_clock_last_value[1]));
		} else {
			MALI_DEBUG_PRINT(2, ("CPU cycle counters readout all: %08x %08x %08x %08x\n", mali_cpu_clock_last_value[0], mali_cpu_clock_last_value[1], mali_cpu_clock_last_value[2], mali_cpu_clock_last_value[3]));
		}
	}
}
#endif

int mali_module_init(void)
{
	int err = 0;

	MALI_DEBUG_PRINT(2, ("Inserting Mali v%d device driver. \n", _MALI_API_VERSION));
	MALI_DEBUG_PRINT(2, ("Compiled: %s, time: %s.\n", __DATE__, __TIME__));
	MALI_DEBUG_PRINT(2, ("Driver revision: %s\n", SVN_REV_STRING));
    
        I("svn_rev_string_from_arm of this mali_ko is '%s', rk_ko_ver is '%d', built at '%s', on '%s'.",
                SVN_REV_STRING,
                RK_KO_VER,
                __TIME__,
                __DATE__);

#if MALI_ENABLE_CPU_CYCLES
	mali_init_cpu_time_counters_on_all_cpus(0);
	MALI_DEBUG_PRINT(2, ("CPU cycle counter setup complete\n"));
	/* Printing the current cpu counters */
	mali_init_cpu_time_counters_on_all_cpus(1);
#endif

	/* Initialize module wide settings */
#ifdef MALI_FAKE_PLATFORM_DEVICE
#ifndef CONFIG_MALI_DT
	MALI_DEBUG_PRINT(2, ("mali_module_init() registering device\n"));
	err = mali_platform_device_register();
	if (0 != err) {
		return err;
	}
#endif
#endif

	MALI_DEBUG_PRINT(2, ("mali_module_init() registering driver\n"));

	err = platform_driver_register(&mali_platform_driver);

	if (0 != err) {
		MALI_DEBUG_PRINT(2, ("mali_module_init() Failed to register driver (%d)\n", err));
#ifdef MALI_FAKE_PLATFORM_DEVICE
#ifndef CONFIG_MALI_DT
		mali_platform_device_unregister();
#endif
#endif
		mali_platform_device = NULL;
		return err;
	}

#if defined(CONFIG_MALI400_INTERNAL_PROFILING)
	err = _mali_internal_profiling_init(mali_boot_profiling ? MALI_TRUE : MALI_FALSE);
	if (0 != err) {
		/* No biggie if we wheren't able to initialize the profiling */
		MALI_PRINT_ERROR(("Failed to initialize profiling, feature will be unavailable\n"));
	}
#endif

	/* Tracing the current frequency and voltage from boot/insmod*/
#if defined(CONFIG_MALI400_PROFILING) && defined(CONFIG_MALI_DVFS)
	/* Just call mali_get_current_gpu_clk_item(),to record current clk info.*/
	mali_get_current_gpu_clk_item(&mali_gpu_clk[0]);
	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE |
				      MALI_PROFILING_EVENT_CHANNEL_GPU |
				      MALI_PROFILING_EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE,
				      mali_gpu_clk[0].clock,
				      mali_gpu_clk[0].vol / 1000,
				      0, 0, 0);
#endif

	MALI_PRINT(("Mali device driver loaded\n"));

	return 0; /* Success */
}

void mali_module_exit(void)
{
	MALI_DEBUG_PRINT(2, ("Unloading Mali v%d device driver.\n", _MALI_API_VERSION));

	MALI_DEBUG_PRINT(2, ("mali_module_exit() unregistering driver\n"));

	platform_driver_unregister(&mali_platform_driver);

#if defined(MALI_FAKE_PLATFORM_DEVICE)
#ifndef CONFIG_MALI_DT
	MALI_DEBUG_PRINT(2, ("mali_module_exit() unregistering device\n"));
	mali_platform_device_unregister();
#endif
#endif

	/* Tracing the current frequency and voltage from rmmod*/
	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE |
				      MALI_PROFILING_EVENT_CHANNEL_GPU |
				      MALI_PROFILING_EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE,
				      0,
				      0,
				      0, 0, 0);

#if defined(CONFIG_MALI400_INTERNAL_PROFILING)
	_mali_internal_profiling_term();
#endif

	MALI_PRINT(("Mali device driver unloaded\n"));
}

#ifdef CONFIG_MALI_DEVFREQ
struct mali_device *mali_device_alloc(void)
{
	return kzalloc(sizeof(struct mali_device), GFP_KERNEL);
}

void mali_device_free(struct mali_device *mdev)
{
	kfree(mdev);
}
#endif

static int mali_probe(struct platform_device *pdev)
{
	int err;
#ifdef CONFIG_MALI_DEVFREQ
	struct mali_device *mdev;
#endif

	MALI_DEBUG_PRINT(2, ("mali_probe(): Called for platform device %s\n", pdev->name));

	if (NULL != mali_platform_device) {
		/* Already connected to a device, return error */
		MALI_PRINT_ERROR(("mali_probe(): The Mali driver is already connected with a Mali device."));
		return -EEXIST;
	}

	mali_platform_device = pdev;

	dev_info(&pdev->dev, "mali_platform_device->num_resources = %d\n",
		mali_platform_device->num_resources);
	
	{
		int i = 0;

		for(i = 0; i < mali_platform_device->num_resources; i++)
			dev_info(&pdev->dev,
				 "resource[%d].start = 0x%pa\n",
				 i,
				 &mali_platform_device->resource[i].start);
	}

#ifdef CONFIG_MALI_DT
	/* If we use DT to initialize our DDK, we have to prepare somethings. */
	err = mali_platform_device_init(mali_platform_device);
	if (0 != err) {
		MALI_PRINT_ERROR(("mali_probe(): Failed to initialize platform device."));
		mali_platform_device = NULL;
		return -EFAULT;
	}
#endif

#ifdef CONFIG_MALI_DEVFREQ
	mdev = mali_device_alloc();
	if (!mdev) {
		MALI_PRINT_ERROR(("Can't allocate mali device private data\n"));
		return -ENOMEM;
	}

	mdev->dev = &pdev->dev;
	dev_set_drvdata(mdev->dev, mdev);

	/*Initilization clock and regulator*/
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)) && defined(CONFIG_OF) \
                        && defined(CONFIG_REGULATOR)
	mdev->regulator = regulator_get_optional(mdev->dev, "mali");
	if (IS_ERR_OR_NULL(mdev->regulator)) {
		MALI_DEBUG_PRINT(2, ("Continuing without Mali regulator control\n"));
		mdev->regulator = NULL;
		/* Allow probe to continue without regulator */
	}
#endif /* LINUX_VERSION_CODE >= 3, 12, 0 */

	err = rk_platform_init_opp_table(mdev->dev);
	if (err)
		MALI_DEBUG_PRINT(3, ("Failed to init_opp_table\n"));

	/* Need to name the gpu clock "clk_mali" in the device tree */
	mdev->clock = clk_get(mdev->dev, "clk_mali");
	if (IS_ERR_OR_NULL(mdev->clock)) {
		MALI_DEBUG_PRINT(2, ("Continuing without Mali clock control\n"));
		mdev->clock = NULL;
		/* Allow probe to continue without clock. */
	} else {
		err = clk_prepare(mdev->clock);
		if (err) {
			MALI_PRINT_ERROR(("Failed to prepare clock (%d)\n", err));
			goto clock_prepare_failed;
		}
	}

	/* initilize pm metrics related */
	if (mali_pm_metrics_init(mdev) < 0) {
		MALI_DEBUG_PRINT(2, ("mali pm metrics init failed\n"));
		goto pm_metrics_init_failed;
	}

	if (mali_devfreq_init(mdev) < 0) {
		MALI_DEBUG_PRINT(2, ("mali devfreq init failed\n"));
		goto devfreq_init_failed;
	}
#endif


	if (_MALI_OSK_ERR_OK == _mali_osk_wq_init()) {
		/* Initialize the Mali GPU HW specified by pdev */
		if (_MALI_OSK_ERR_OK == mali_initialize_subsystems()) {
			/* Register a misc device (so we are accessible from user space) */
			err = mali_miscdevice_register(pdev);
			if (0 == err) {
				/* Setup sysfs entries */
				err = mali_sysfs_register(mali_dev_name);

				if (0 == err) {
					MALI_DEBUG_PRINT(2, ("mali_probe(): Successfully initialized driver for platform device %s\n", pdev->name));

					return 0;
				} else {
					MALI_PRINT_ERROR(("mali_probe(): failed to register sysfs entries"));
				}
				mali_miscdevice_unregister();
			} else {
				MALI_PRINT_ERROR(("mali_probe(): failed to register Mali misc device."));
			}
			mali_terminate_subsystems();
		} else {
			MALI_PRINT_ERROR(("mali_probe(): Failed to initialize Mali device driver."));
		}
		_mali_osk_wq_term();
	}

#ifdef CONFIG_MALI_DEVFREQ
	mali_devfreq_term(mdev);
devfreq_init_failed:
	mali_pm_metrics_term(mdev);
pm_metrics_init_failed:
	clk_unprepare(mdev->clock);
clock_prepare_failed:
	clk_put(mdev->clock);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)) && defined(CONFIG_OF) \
                        && defined(CONFIG_PM_OPP)
	dev_pm_opp_of_remove_table(mdev->dev);
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)) && defined(CONFIG_OF) \
                        && defined(CONFIG_REGULATOR)
	regulator_put(mdev->regulator);
#endif /* LINUX_VERSION_CODE >= 3, 12, 0 */
	mali_device_free(mdev);
#endif

#ifdef CONFIG_MALI_DT
	mali_platform_device_deinit(mali_platform_device);
#endif
	mali_platform_device = NULL;
	return -EFAULT;
}

static int mali_remove(struct platform_device *pdev)
{
#ifdef CONFIG_MALI_DEVFREQ
	struct mali_device *mdev = dev_get_drvdata(&pdev->dev);
#endif

	MALI_DEBUG_PRINT(2, ("mali_remove() called for platform device %s\n", pdev->name));
	mali_sysfs_unregister();
	mali_miscdevice_unregister();
	mali_terminate_subsystems();
	_mali_osk_wq_term();

#ifdef CONFIG_MALI_DEVFREQ
	mali_devfreq_term(mdev);

	mali_pm_metrics_term(mdev);

	if (mdev->clock) {
		clk_unprepare(mdev->clock);
		clk_put(mdev->clock);
		mdev->clock = NULL;
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)) && defined(CONFIG_OF) \
                        && defined(CONFIG_PM_OPP)
	dev_pm_opp_of_remove_table(mdev->dev);
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)) && defined(CONFIG_OF) \
                        && defined(CONFIG_REGULATOR)
	regulator_put(mdev->regulator);
#endif /* LINUX_VERSION_CODE >= 3, 12, 0 */
	mali_device_free(mdev);
#endif

#ifdef CONFIG_MALI_DT
	mali_platform_device_deinit(mali_platform_device);
#endif
	mali_platform_device = NULL;
	return 0;
}

static int mali_miscdevice_register(struct platform_device *pdev)
{
	int err;

	mali_miscdevice.minor = MISC_DYNAMIC_MINOR;
	mali_miscdevice.name = mali_dev_name;
	mali_miscdevice.fops = &mali_fops;
	mali_miscdevice.parent = get_device(&pdev->dev);

	err = misc_register(&mali_miscdevice);
	if (0 != err) {
		MALI_PRINT_ERROR(("Failed to register misc device, misc_register() returned %d\n", err));
	}

	return err;
}

static void mali_miscdevice_unregister(void)
{
	misc_deregister(&mali_miscdevice);
}

static int mali_driver_suspend_scheduler(struct device *dev)
{
#ifdef CONFIG_MALI_DEVFREQ
	struct mali_device *mdev = dev_get_drvdata(dev);
	if (!mdev)
		return -ENODEV;
#endif

#if defined(CONFIG_MALI_DEVFREQ) && \
                (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	devfreq_suspend_device(mdev->devfreq);
#endif

	mali_pm_os_suspend(MALI_TRUE);
	/* Tracing the frequency and voltage after mali is suspended */
	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE |
				      MALI_PROFILING_EVENT_CHANNEL_GPU |
				      MALI_PROFILING_EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE,
				      0,
				      0,
				      0, 0, 0);
	return 0;
}

static int mali_driver_resume_scheduler(struct device *dev)
{
#ifdef CONFIG_MALI_DEVFREQ
	struct mali_device *mdev = dev_get_drvdata(dev);
	if (!mdev)
		return -ENODEV;
#endif

	/* Tracing the frequency and voltage after mali is resumed */
#if defined(CONFIG_MALI400_PROFILING) && defined(CONFIG_MALI_DVFS)
	/* Just call mali_get_current_gpu_clk_item() once,to record current clk info.*/
	if (is_first_resume == 1) {
		mali_get_current_gpu_clk_item(&mali_gpu_clk[1]);
		is_first_resume = 0;
	}
	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE |
				      MALI_PROFILING_EVENT_CHANNEL_GPU |
				      MALI_PROFILING_EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE,
				      mali_gpu_clk[1].clock,
				      mali_gpu_clk[1].vol / 1000,
				      0, 0, 0);
#endif
	mali_pm_os_resume();

#if defined(CONFIG_MALI_DEVFREQ) && \
                (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	devfreq_resume_device(mdev->devfreq);
#endif

	return 0;
}

#ifdef CONFIG_PM_RUNTIME
static int mali_driver_runtime_suspend(struct device *dev)
{
#ifdef CONFIG_MALI_DEVFREQ
	struct mali_device *mdev = dev_get_drvdata(dev);
	if (!mdev)
		return -ENODEV;
#endif

	if (MALI_TRUE == mali_pm_runtime_suspend()) {
		/* Tracing the frequency and voltage after mali is suspended */
		_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE |
					      MALI_PROFILING_EVENT_CHANNEL_GPU |
					      MALI_PROFILING_EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE,
					      0,
					      0,
					      0, 0, 0);

#if defined(CONFIG_MALI_DEVFREQ) && \
                (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
		MALI_DEBUG_PRINT(4, ("devfreq_suspend_device: stop devfreq monitor\n"));
		devfreq_suspend_device(mdev->devfreq);
#endif

		return 0;
	} else {
		return -EBUSY;
	}
}

static int mali_driver_runtime_resume(struct device *dev)
{
#ifdef CONFIG_MALI_DEVFREQ
	struct mali_device *mdev = dev_get_drvdata(dev);
	if (!mdev)
		return -ENODEV;
#endif

	/* Tracing the frequency and voltage after mali is resumed */
#if defined(CONFIG_MALI400_PROFILING) && defined(CONFIG_MALI_DVFS)
	/* Just call mali_get_current_gpu_clk_item() once,to record current clk info.*/
	if (is_first_resume == 1) {
		mali_get_current_gpu_clk_item(&mali_gpu_clk[1]);
		is_first_resume = 0;
	}
	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE |
				      MALI_PROFILING_EVENT_CHANNEL_GPU |
				      MALI_PROFILING_EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE,
				      mali_gpu_clk[1].clock,
				      mali_gpu_clk[1].vol / 1000,
				      0, 0, 0);
#endif

	mali_pm_runtime_resume();

#if defined(CONFIG_MALI_DEVFREQ) && \
                (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	MALI_DEBUG_PRINT(4, ("devfreq_resume_device: start devfreq monitor\n"));
	devfreq_resume_device(mdev->devfreq);
#endif
	return 0;
}

static int mali_driver_runtime_idle(struct device *dev)
{
	/* Nothing to do */
	return 0;
}
#endif

static int mali_open(struct inode *inode, struct file *filp)
{
	struct mali_session_data *session_data;
	_mali_osk_errcode_t err;

	/* input validation */
	if (mali_miscdevice.minor != iminor(inode)) {
		MALI_PRINT_ERROR(("mali_open() Minor does not match\n"));
		return -ENODEV;
	}

	/* allocated struct to track this session */
	err = _mali_ukk_open((void **)&session_data);
	if (_MALI_OSK_ERR_OK != err) return map_errcode(err);

	/* initialize file pointer */
	filp->f_pos = 0;

	/* link in our session data */
	filp->private_data = (void *)session_data;

	filp->f_mapping = mali_mem_swap_get_global_swap_file()->f_mapping;

	return 0;
}

static int mali_release(struct inode *inode, struct file *filp)
{
	_mali_osk_errcode_t err;

	/* input validation */
	if (mali_miscdevice.minor != iminor(inode)) {
		MALI_PRINT_ERROR(("mali_release() Minor does not match\n"));
		return -ENODEV;
	}

	err = _mali_ukk_close((void **)&filp->private_data);
	if (_MALI_OSK_ERR_OK != err) return map_errcode(err);

	return 0;
}

int map_errcode(_mali_osk_errcode_t err)
{
	switch (err) {
	case _MALI_OSK_ERR_OK :
		return 0;
	case _MALI_OSK_ERR_FAULT:
		return -EFAULT;
	case _MALI_OSK_ERR_INVALID_FUNC:
		return -ENOTTY;
	case _MALI_OSK_ERR_INVALID_ARGS:
		return -EINVAL;
	case _MALI_OSK_ERR_NOMEM:
		return -ENOMEM;
	case _MALI_OSK_ERR_TIMEOUT:
		return -ETIMEDOUT;
	case _MALI_OSK_ERR_RESTARTSYSCALL:
		return -ERESTARTSYS;
	case _MALI_OSK_ERR_ITEM_NOT_FOUND:
		return -ENOENT;
	default:
		return -EFAULT;
	}
}

static long mali_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int err;
	struct mali_session_data *session_data;

	MALI_DEBUG_PRINT(7, ("Ioctl received 0x%08X 0x%08lX\n", cmd, arg));

	session_data = (struct mali_session_data *)filp->private_data;
	if (NULL == session_data) {
		MALI_DEBUG_PRINT(7, ("filp->private_data was NULL\n"));
		return -ENOTTY;
	}

	if (NULL == (void *)arg) {
		MALI_DEBUG_PRINT(7, ("arg was NULL\n"));
		return -ENOTTY;
	}

	switch (cmd) {
	case MALI_IOC_WAIT_FOR_NOTIFICATION:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_wait_for_notification_s), sizeof(u64)));
		err = wait_for_notification_wrapper(session_data, (_mali_uk_wait_for_notification_s __user *)arg);
		break;

	case MALI_IOC_GET_API_VERSION_V2:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_get_api_version_v2_s), sizeof(u64)));
		err = get_api_version_v2_wrapper(session_data, (_mali_uk_get_api_version_v2_s __user *)arg);
		break;

	case MALI_IOC_GET_API_VERSION:
		err = get_api_version_wrapper(session_data, (_mali_uk_get_api_version_s __user *)arg);
		break;

	case MALI_IOC_POST_NOTIFICATION:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_post_notification_s), sizeof(u64)));
		err = post_notification_wrapper(session_data, (_mali_uk_post_notification_s __user *)arg);
		break;

    /* rk_ext : 从对 r5p0-01rel0 集成开始, 不再使用. */
#if 0
	case MALI_IOC_GET_MALI_VERSION_IN_RK30:
		err = get_mali_version_in_rk30_wrapper(session_data, (_mali_uk_get_mali_version_in_rk30_s __user *)arg);
		break;
#else
    case MALI_IOC_GET_RK_KO_VERSION:
		err = get_rk_ko_version_wrapper(session_data, (_mali_rk_ko_version_s __user *)arg);
		break;
#endif
        
	case MALI_IOC_GET_USER_SETTINGS:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_get_user_settings_s), sizeof(u64)));
		err = get_user_settings_wrapper(session_data, (_mali_uk_get_user_settings_s __user *)arg);
		break;

	case MALI_IOC_REQUEST_HIGH_PRIORITY:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_request_high_priority_s), sizeof(u64)));
		err = request_high_priority_wrapper(session_data, (_mali_uk_request_high_priority_s __user *)arg);
		break;

	case MALI_IOC_PENDING_SUBMIT:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_pending_submit_s), sizeof(u64)));
		err = pending_submit_wrapper(session_data, (_mali_uk_pending_submit_s __user *)arg);
		break;

#if defined(CONFIG_MALI400_PROFILING)
	case MALI_IOC_PROFILING_ADD_EVENT:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_profiling_add_event_s), sizeof(u64)));
		err = profiling_add_event_wrapper(session_data, (_mali_uk_profiling_add_event_s __user *)arg);
		break;

	case MALI_IOC_PROFILING_REPORT_SW_COUNTERS:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_sw_counters_report_s), sizeof(u64)));
		err = profiling_report_sw_counters_wrapper(session_data, (_mali_uk_sw_counters_report_s __user *)arg);
		break;

	case MALI_IOC_PROFILING_STREAM_FD_GET:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_profiling_stream_fd_get_s), sizeof(u64)));
		err = profiling_get_stream_fd_wrapper(session_data, (_mali_uk_profiling_stream_fd_get_s __user *)arg);
		break;

	case MALI_IOC_PROILING_CONTROL_SET:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_profiling_control_set_s), sizeof(u64)));
		err = profiling_control_set_wrapper(session_data, (_mali_uk_profiling_control_set_s __user *)arg);
		break;
#else

	case MALI_IOC_PROFILING_ADD_EVENT:          /* FALL-THROUGH */
	case MALI_IOC_PROFILING_REPORT_SW_COUNTERS: /* FALL-THROUGH */
		MALI_DEBUG_PRINT(2, ("Profiling not supported\n"));
		err = -ENOTTY;
		break;
#endif

	case MALI_IOC_PROFILING_MEMORY_USAGE_GET:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_profiling_memory_usage_get_s), sizeof(u64)));
		err = mem_usage_get_wrapper(session_data, (_mali_uk_profiling_memory_usage_get_s __user *)arg);
		break;

	case MALI_IOC_MEM_ALLOC:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_alloc_mem_s), sizeof(u64)));
		err = mem_alloc_wrapper(session_data, (_mali_uk_alloc_mem_s __user *)arg);
		break;

	case MALI_IOC_MEM_FREE:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_free_mem_s), sizeof(u64)));
		err = mem_free_wrapper(session_data, (_mali_uk_free_mem_s __user *)arg);
		break;

	case MALI_IOC_MEM_BIND:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_bind_mem_s), sizeof(u64)));
		err = mem_bind_wrapper(session_data, (_mali_uk_bind_mem_s __user *)arg);
		break;

	case MALI_IOC_MEM_UNBIND:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_unbind_mem_s), sizeof(u64)));
		err = mem_unbind_wrapper(session_data, (_mali_uk_unbind_mem_s __user *)arg);
		break;

	case MALI_IOC_MEM_COW:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_cow_mem_s), sizeof(u64)));
		err = mem_cow_wrapper(session_data, (_mali_uk_cow_mem_s __user *)arg);
		break;

	case MALI_IOC_MEM_COW_MODIFY_RANGE:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_cow_modify_range_s), sizeof(u64)));
		err = mem_cow_modify_range_wrapper(session_data, (_mali_uk_cow_modify_range_s __user *)arg);
		break;

	case MALI_IOC_MEM_RESIZE:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_mem_resize_s), sizeof(u64)));
		err = mem_resize_mem_wrapper(session_data, (_mali_uk_mem_resize_s __user *)arg);
		break;

	case MALI_IOC_MEM_WRITE_SAFE:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_mem_write_safe_s), sizeof(u64)));
		err = mem_write_safe_wrapper(session_data, (_mali_uk_mem_write_safe_s __user *)arg);
		break;

	case MALI_IOC_MEM_QUERY_MMU_PAGE_TABLE_DUMP_SIZE:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_query_mmu_page_table_dump_size_s), sizeof(u64)));
		err = mem_query_mmu_page_table_dump_size_wrapper(session_data, (_mali_uk_query_mmu_page_table_dump_size_s __user *)arg);
		break;

	case MALI_IOC_MEM_DUMP_MMU_PAGE_TABLE:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_dump_mmu_page_table_s), sizeof(u64)));
		err = mem_dump_mmu_page_table_wrapper(session_data, (_mali_uk_dump_mmu_page_table_s __user *)arg);
		break;

	case MALI_IOC_MEM_DMA_BUF_GET_SIZE:
#ifdef CONFIG_DMA_SHARED_BUFFER
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_dma_buf_get_size_s), sizeof(u64)));
		err = mali_dma_buf_get_size(session_data, (_mali_uk_dma_buf_get_size_s __user *)arg);
#else
		MALI_DEBUG_PRINT(2, ("DMA-BUF not supported\n"));
		err = -ENOTTY;
#endif
		break;

	case MALI_IOC_PP_START_JOB:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_pp_start_job_s), sizeof(u64)));
		err = pp_start_job_wrapper(session_data, (_mali_uk_pp_start_job_s __user *)arg);
		break;

	case MALI_IOC_PP_AND_GP_START_JOB:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_pp_and_gp_start_job_s), sizeof(u64)));
		err = pp_and_gp_start_job_wrapper(session_data, (_mali_uk_pp_and_gp_start_job_s __user *)arg);
		break;

	case MALI_IOC_PP_NUMBER_OF_CORES_GET:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_get_pp_number_of_cores_s), sizeof(u64)));
		err = pp_get_number_of_cores_wrapper(session_data, (_mali_uk_get_pp_number_of_cores_s __user *)arg);
		break;

	case MALI_IOC_PP_CORE_VERSION_GET:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_get_pp_core_version_s), sizeof(u64)));
		err = pp_get_core_version_wrapper(session_data, (_mali_uk_get_pp_core_version_s __user *)arg);
		break;

	case MALI_IOC_PP_DISABLE_WB:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_pp_disable_wb_s), sizeof(u64)));
		err = pp_disable_wb_wrapper(session_data, (_mali_uk_pp_disable_wb_s __user *)arg);
		break;

	case MALI_IOC_GP2_START_JOB:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_gp_start_job_s), sizeof(u64)));
		err = gp_start_job_wrapper(session_data, (_mali_uk_gp_start_job_s __user *)arg);
		break;

	case MALI_IOC_GP2_NUMBER_OF_CORES_GET:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_get_gp_number_of_cores_s), sizeof(u64)));
		err = gp_get_number_of_cores_wrapper(session_data, (_mali_uk_get_gp_number_of_cores_s __user *)arg);
		break;

	case MALI_IOC_GP2_CORE_VERSION_GET:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_get_gp_core_version_s), sizeof(u64)));
		err = gp_get_core_version_wrapper(session_data, (_mali_uk_get_gp_core_version_s __user *)arg);
		break;

	case MALI_IOC_GP2_SUSPEND_RESPONSE:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_gp_suspend_response_s), sizeof(u64)));
		err = gp_suspend_response_wrapper(session_data, (_mali_uk_gp_suspend_response_s __user *)arg);
		break;

	case MALI_IOC_VSYNC_EVENT_REPORT:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_vsync_event_report_s), sizeof(u64)));
		err = vsync_event_report_wrapper(session_data, (_mali_uk_vsync_event_report_s __user *)arg);
		break;

	case MALI_IOC_TIMELINE_GET_LATEST_POINT:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_timeline_get_latest_point_s), sizeof(u64)));
		err = timeline_get_latest_point_wrapper(session_data, (_mali_uk_timeline_get_latest_point_s __user *)arg);
		break;
	case MALI_IOC_TIMELINE_WAIT:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_timeline_wait_s), sizeof(u64)));
		err = timeline_wait_wrapper(session_data, (_mali_uk_timeline_wait_s __user *)arg);
		break;
	case MALI_IOC_TIMELINE_CREATE_SYNC_FENCE:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_timeline_create_sync_fence_s), sizeof(u64)));
		err = timeline_create_sync_fence_wrapper(session_data, (_mali_uk_timeline_create_sync_fence_s __user *)arg);
		break;
	case MALI_IOC_SOFT_JOB_START:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_soft_job_start_s), sizeof(u64)));
		err = soft_job_start_wrapper(session_data, (_mali_uk_soft_job_start_s __user *)arg);
		break;
	case MALI_IOC_SOFT_JOB_SIGNAL:
		BUILD_BUG_ON(!IS_ALIGNED(sizeof(_mali_uk_soft_job_signal_s), sizeof(u64)));
		err = soft_job_signal_wrapper(session_data, (_mali_uk_soft_job_signal_s __user *)arg);
		break;

	default:
		MALI_DEBUG_PRINT(2, ("No handler for ioctl 0x%08X 0x%08lX\n", cmd, arg));
		err = -ENOTTY;
	};

	return err;
}

late_initcall_sync(mali_module_init);
module_exit(mali_module_exit);

MODULE_LICENSE(MALI_KERNEL_LINUX_LICENSE);
MODULE_AUTHOR("ARM Ltd.");
MODULE_VERSION(SVN_REV_STRING);
