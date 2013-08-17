/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
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
#include <linux/module.h>   /* kernel module definitions */
#include <linux/fs.h>       /* file system operations */
#include <linux/cdev.h>     /* character device definitions */
#include <linux/mm.h>       /* memory manager definitions */
#include <linux/mali/mali_utgard_ioctl.h>
#include <linux/version.h>
#include <linux/device.h>
#include "mali_kernel_license.h"
#include <linux/platform_device.h>
#if MALI_LICENSE_IS_GPL
#include <linux/miscdevice.h>
#endif
#include <linux/mali/mali_utgard.h>
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
#include "mali_dma_buf.h"
#if defined(CONFIG_MALI400_INTERNAL_PROFILING)
#include "mali_profiling_internal.h"
#endif
/* MALI_SEC */
#include "../platform/pegasus-m400/exynos4_pmm.h"

/* Streamline support for the Mali driver */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_MALI400_PROFILING)
/* Ask Linux to create the tracepoints */
#define CREATE_TRACE_POINTS
#include "mali_linux_trace.h"
#endif /* CONFIG_TRACEPOINTS */

/* from the __malidrv_build_info.c file that is generated during build */
extern const char *__malidrv_build_info(void);

/* Module parameter to control log level */
int mali_debug_level = 2;
module_param(mali_debug_level, int, S_IRUSR | S_IWUSR | S_IWGRP | S_IRGRP | S_IROTH); /* rw-rw-r-- */
MODULE_PARM_DESC(mali_debug_level, "Higher number, more dmesg output");

module_param(mali_max_job_runtime, int, S_IRUSR | S_IWUSR | S_IWGRP | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(mali_max_job_runtime, "Maximum allowed job runtime in msecs.\nJobs will be killed after this no matter what");

extern int mali_l2_max_reads;
module_param(mali_l2_max_reads, int, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(mali_l2_max_reads, "Maximum reads for Mali L2 cache");

extern int mali_dedicated_mem_start;
module_param(mali_dedicated_mem_start, int, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(mali_dedicated_mem_start, "Physical start address of dedicated Mali GPU memory.");

extern int mali_dedicated_mem_size;
module_param(mali_dedicated_mem_size, int, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(mali_dedicated_mem_size, "Size of dedicated Mali GPU memory.");

extern int mali_shared_mem_size;
module_param(mali_shared_mem_size, int, S_IRUSR | S_IRGRP | S_IROTH);
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

/* Export symbols from common code: mali_user_settings.c */
#include "mali_user_settings_db.h"
EXPORT_SYMBOL(mali_set_user_setting);
EXPORT_SYMBOL(mali_get_user_setting);
#if CONFIG_MALI_DVFS
/* MALI_SEC */
extern int mali_dvfs_control;
module_param(mali_dvfs_control, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP| S_IROTH); /* rw-rw-r-- */
MODULE_PARM_DESC(mali_dvfs_control, "Mali Current DVFS");

#if 0
extern int mali_gpu_clk;
module_param(mali_gpu_clk, int, S_IRUSR | S_IRGRP | S_IROTH); /* r--r--r-- */
MODULE_PARM_DESC(mali_gpu_clk, "Mali Current Clock");

extern int mali_gpu_vol;
module_param(mali_gpu_vol, int, S_IRUSR | S_IRGRP | S_IROTH); /* r--r--r-- */
MODULE_PARM_DESC(mali_gpu_vol, "Mali Current Voltage");

extern int gpu_power_state;
module_param(gpu_power_state, int, S_IRUSR | S_IRGRP | S_IROTH); /* r--r--r-- */
MODULE_PARM_DESC(gpu_power_state, "Mali Power State");
#endif
#endif

static char mali_dev_name[] = "mali"; /* should be const, but the functions we call requires non-cost */

/* This driver only supports one Mali device, and this variable stores this single platform device */
struct platform_device *mali_platform_device = NULL;

/* This driver only supports one Mali device, and this variable stores the exposed misc device (/dev/mali) */
static struct miscdevice mali_miscdevice = { 0, };

static int mali_miscdevice_register(struct platform_device *pdev);
static void mali_miscdevice_unregister(void);

static int mali_open(struct inode *inode, struct file *filp);
static int mali_release(struct inode *inode, struct file *filp);
#ifdef HAVE_UNLOCKED_IOCTL
static long mali_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
#else
static int mali_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg);
#endif
static int mali_mmap(struct file * filp, struct vm_area_struct * vma);

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
extern int mali_platform_device_register(void);
extern int mali_platform_device_unregister(void);
#endif

/* Linux power management operations provided by the Mali device driver */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29))
struct pm_ext_ops mali_dev_ext_pm_ops =
{
	.base =
	{
		.suspend = mali_driver_suspend_scheduler,
		.resume = mali_driver_resume_scheduler,
		.freeze = mali_driver_suspend_scheduler,
		.thaw =   mali_driver_resume_scheduler,
	},
};
#else
static const struct dev_pm_ops mali_dev_pm_ops =
{
#ifdef CONFIG_PM_RUNTIME
	.runtime_suspend = mali_driver_runtime_suspend,
	.runtime_resume = mali_driver_runtime_resume,
	.runtime_idle = mali_driver_runtime_idle,
#endif
	.suspend = mali_driver_suspend_scheduler,
	.resume = mali_driver_resume_scheduler,
	.freeze = mali_driver_suspend_scheduler,
	.thaw = mali_driver_resume_scheduler,
};
#endif

/* The Mali device driver struct */
static struct platform_driver mali_platform_driver =
{
	.probe  = mali_probe,
	.remove = mali_remove,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29))
	.pm = &mali_dev_ext_pm_ops,
#endif
	.driver =
	{
		.name   = "mali_dev", /* MALI_SEC MALI_GPU_NAME_UTGARD, */
		.owner  = THIS_MODULE,
		.bus = &platform_bus_type,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,29))
		.pm = &mali_dev_pm_ops,
#endif
	},
};

/* Linux misc device operations (/dev/mali) */
struct file_operations mali_fops =
{
	.owner = THIS_MODULE,
	.open = mali_open,
	.release = mali_release,
#ifdef HAVE_UNLOCKED_IOCTL
	.unlocked_ioctl = mali_ioctl,
#else
	.ioctl = mali_ioctl,
#endif
	.mmap = mali_mmap
};






int mali_module_init(void)
{
	int err = 0;

	MALI_DEBUG_PRINT(2, ("Inserting Mali v%d device driver. \n",_MALI_API_VERSION));
	MALI_DEBUG_PRINT(2, ("Compiled: %s, time: %s.\n", __DATE__, __TIME__));
	MALI_DEBUG_PRINT(2, ("Driver revision: %s\n", SVN_REV_STRING));

	/* Initialize module wide settings */
	mali_osk_low_level_mem_init();

#if defined(MALI_FAKE_PLATFORM_DEVICE)
	MALI_DEBUG_PRINT(2, ("mali_module_init() registering device\n"));
	err = mali_platform_device_register();
	if (0 != err)
	{
		return err;
	}
#endif

	MALI_DEBUG_PRINT(2, ("mali_module_init() registering driver\n"));

	err = platform_driver_register(&mali_platform_driver);

	if (0 != err)
	{
		MALI_DEBUG_PRINT(2, ("mali_module_init() Failed to register driver (%d)\n", err));
#if defined(MALI_FAKE_PLATFORM_DEVICE)
		mali_platform_device_unregister();
#endif
		mali_platform_device = NULL;
		return err;
	}

#if defined(CONFIG_MALI400_INTERNAL_PROFILING)
        err = _mali_internal_profiling_init(mali_boot_profiling ? MALI_TRUE : MALI_FALSE);
        if (0 != err)
        {
                /* No biggie if we wheren't able to initialize the profiling */
                MALI_PRINT_ERROR(("Failed to initialize profiling, feature will be unavailable\n"));
        }
#endif

	MALI_PRINT(("Mali device driver loaded\n"));

	return 0; /* Success */
}

void mali_module_exit(void)
{
	MALI_DEBUG_PRINT(2, ("Unloading Mali v%d device driver.\n",_MALI_API_VERSION));

	MALI_DEBUG_PRINT(2, ("mali_module_exit() unregistering driver\n"));

#if defined(CONFIG_MALI400_INTERNAL_PROFILING)
        _mali_internal_profiling_term();
#endif

	platform_driver_unregister(&mali_platform_driver);

#if defined(MALI_FAKE_PLATFORM_DEVICE)
	MALI_DEBUG_PRINT(2, ("mali_module_exit() unregistering device\n"));
	mali_platform_device_unregister();
#endif

	mali_osk_low_level_mem_term();

	MALI_PRINT(("Mali device driver unloaded\n"));
}

static int mali_probe(struct platform_device *pdev)
{
	int err;

	MALI_DEBUG_PRINT(2, ("mali_probe(): Called for platform device %s\n", pdev->name));

	if (NULL != mali_platform_device)
	{
		/* Already connected to a device, return error */
		MALI_PRINT_ERROR(("mali_probe(): The Mali driver is already connected with a Mali device."));
		return -EEXIST;
	}

	mali_platform_device = pdev;

	if (_MALI_OSK_ERR_OK == _mali_osk_wq_init())
	{
		/* Initialize the Mali GPU HW specified by pdev */
		if (_MALI_OSK_ERR_OK == mali_initialize_subsystems())
		{
			/* Register a misc device (so we are accessible from user space) */
			err = mali_miscdevice_register(pdev);
			if (0 == err)
			{
				/* Setup sysfs entries */
				err = mali_sysfs_register(mali_dev_name);
				if (0 == err)
				{
					MALI_DEBUG_PRINT(2, ("mali_probe(): Successfully initialized driver for platform device %s\n", pdev->name));
					return 0;
				}
				else
				{
					MALI_PRINT_ERROR(("mali_probe(): failed to register sysfs entries"));
				}
				mali_miscdevice_unregister();
			}
			else
			{
				MALI_PRINT_ERROR(("mali_probe(): failed to register Mali misc device."));
			}
			mali_terminate_subsystems();
		}
		else
		{
			MALI_PRINT_ERROR(("mali_probe(): Failed to initialize Mali device driver."));
		}
		_mali_osk_wq_term();
	}

	mali_platform_device = NULL;
	return -EFAULT;
}

static int mali_remove(struct platform_device *pdev)
{
	MALI_DEBUG_PRINT(2, ("mali_remove() called for platform device %s\n", pdev->name));
	mali_sysfs_unregister();
	mali_miscdevice_unregister();
	mali_terminate_subsystems();
	_mali_osk_wq_term();
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
	if (0 != err)
	{
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
	mali_pm_os_suspend();
	/* MALI_SEC */
	mali_platform_power_mode_change(MALI_POWER_MODE_DEEP_SLEEP);
	return 0;
}

static int mali_driver_resume_scheduler(struct device *dev)
{
	/* MALI_SEC */
	mali_platform_power_mode_change(MALI_POWER_MODE_ON);
	mali_pm_os_resume();
	return 0;
}

#ifdef CONFIG_PM_RUNTIME
static int mali_driver_runtime_suspend(struct device *dev)
{
	mali_pm_runtime_suspend();
	/* MALI_SEC */
	mali_platform_power_mode_change(MALI_POWER_MODE_LIGHT_SLEEP);
	return 0;
}

static int mali_driver_runtime_resume(struct device *dev)
{
	/* MALI_SEC */
	mali_platform_power_mode_change(MALI_POWER_MODE_ON);
	mali_pm_runtime_resume();
	return 0;
}

static int mali_driver_runtime_idle(struct device *dev)
{
	/* Nothing to do */
	return 0;
}
#endif

/** @note munmap handler is done by vma close handler */
static int mali_mmap(struct file * filp, struct vm_area_struct * vma)
{
	struct mali_session_data * session_data;
	_mali_uk_mem_mmap_s args = {0, };

    session_data = (struct mali_session_data *)filp->private_data;
	if (NULL == session_data)
	{
		MALI_PRINT_ERROR(("mmap called without any session data available\n"));
		return -EFAULT;
	}

	MALI_DEBUG_PRINT(4, ("MMap() handler: start=0x%08X, phys=0x%08X, size=0x%08X vma->flags 0x%08x\n", (unsigned int)vma->vm_start, (unsigned int)(vma->vm_pgoff << PAGE_SHIFT), (unsigned int)(vma->vm_end - vma->vm_start), vma->vm_flags));

	/* Re-pack the arguments that mmap() packed for us */
	args.ctx = session_data;
	args.phys_addr = vma->vm_pgoff << PAGE_SHIFT;
	args.size = vma->vm_end - vma->vm_start;
	args.ukk_private = vma;

	if ( VM_SHARED== (VM_SHARED  & vma->vm_flags))
	{
		args.cache_settings = MALI_CACHE_STANDARD ;
		MALI_DEBUG_PRINT(3,("Allocate - Standard - Size: %d kb\n", args.size/1024));
	}
	else
	{
		args.cache_settings = MALI_CACHE_GP_READ_ALLOCATE;
		MALI_DEBUG_PRINT(3,("Allocate - GP Cached - Size: %d kb\n", args.size/1024));
	}
	/* Setting it equal to VM_SHARED and not Private, which would have made the later io_remap fail for MALI_CACHE_GP_READ_ALLOCATE */
	vma->vm_flags = 0x000000fb;

	/* Call the common mmap handler */
	MALI_CHECK(_MALI_OSK_ERR_OK ==_mali_ukk_mem_mmap( &args ), -EFAULT);

    return 0;
}

static int mali_open(struct inode *inode, struct file *filp)
{
	struct mali_session_data * session_data;
    _mali_osk_errcode_t err;

	/* input validation */
	if (mali_miscdevice.minor != iminor(inode))
	{
		MALI_PRINT_ERROR(("mali_open() Minor does not match\n"));
		return -ENODEV;
	}

	/* allocated struct to track this session */
    err = _mali_ukk_open((void **)&session_data);
    if (_MALI_OSK_ERR_OK != err) return map_errcode(err);

	/* initialize file pointer */
	filp->f_pos = 0;

	/* link in our session data */
	filp->private_data = (void*)session_data;

	return 0;
}

static int mali_release(struct inode *inode, struct file *filp)
{
    _mali_osk_errcode_t err;

	/* input validation */
	if (mali_miscdevice.minor != iminor(inode))
	{
		MALI_PRINT_ERROR(("mali_release() Minor does not match\n"));
		return -ENODEV;
	}

    err = _mali_ukk_close((void **)&filp->private_data);
    if (_MALI_OSK_ERR_OK != err) return map_errcode(err);

	return 0;
}

int map_errcode( _mali_osk_errcode_t err )
{
    switch(err)
    {
        case _MALI_OSK_ERR_OK : return 0;
        case _MALI_OSK_ERR_FAULT: return -EFAULT;
        case _MALI_OSK_ERR_INVALID_FUNC: return -ENOTTY;
        case _MALI_OSK_ERR_INVALID_ARGS: return -EINVAL;
        case _MALI_OSK_ERR_NOMEM: return -ENOMEM;
        case _MALI_OSK_ERR_TIMEOUT: return -ETIMEDOUT;
        case _MALI_OSK_ERR_RESTARTSYSCALL: return -ERESTARTSYS;
        case _MALI_OSK_ERR_ITEM_NOT_FOUND: return -ENOENT;
        default: return -EFAULT;
    }
}

#ifdef HAVE_UNLOCKED_IOCTL
static long mali_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
#else
static int mali_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
#endif
{
	int err;
	struct mali_session_data *session_data;

#ifndef HAVE_UNLOCKED_IOCTL
	/* inode not used */
	(void)inode;
#endif

	MALI_DEBUG_PRINT(7, ("Ioctl received 0x%08X 0x%08lX\n", cmd, arg));

	session_data = (struct mali_session_data *)filp->private_data;
	if (NULL == session_data)
	{
		MALI_DEBUG_PRINT(7, ("filp->private_data was NULL\n"));
		return -ENOTTY;
	}

	if (NULL == (void *)arg)
	{
		MALI_DEBUG_PRINT(7, ("arg was NULL\n"));
		return -ENOTTY;
	}

	switch(cmd)
	{
		case MALI_IOC_WAIT_FOR_NOTIFICATION:
			err = wait_for_notification_wrapper(session_data, (_mali_uk_wait_for_notification_s __user *)arg);
			break;

		case MALI_IOC_GET_API_VERSION:
			err = get_api_version_wrapper(session_data, (_mali_uk_get_api_version_s __user *)arg);
			break;

		case MALI_IOC_POST_NOTIFICATION:
			err = post_notification_wrapper(session_data, (_mali_uk_post_notification_s __user *)arg);
			break;

		case MALI_IOC_GET_USER_SETTINGS:
			err = get_user_settings_wrapper(session_data, (_mali_uk_get_user_settings_s __user *)arg);
			break;

#if defined(CONFIG_MALI400_PROFILING)
		case MALI_IOC_PROFILING_START:
			err = profiling_start_wrapper(session_data, (_mali_uk_profiling_start_s __user *)arg);
			break;

		case MALI_IOC_PROFILING_ADD_EVENT:
			err = profiling_add_event_wrapper(session_data, (_mali_uk_profiling_add_event_s __user *)arg);
			break;

		case MALI_IOC_PROFILING_STOP:
			err = profiling_stop_wrapper(session_data, (_mali_uk_profiling_stop_s __user *)arg);
			break;

		case MALI_IOC_PROFILING_GET_EVENT:
			err = profiling_get_event_wrapper(session_data, (_mali_uk_profiling_get_event_s __user *)arg);
			break;

		case MALI_IOC_PROFILING_CLEAR:
			err = profiling_clear_wrapper(session_data, (_mali_uk_profiling_clear_s __user *)arg);
			break;

		case MALI_IOC_PROFILING_GET_CONFIG:
			/* Deprecated: still compatible with get_user_settings */
			err = get_user_settings_wrapper(session_data, (_mali_uk_get_user_settings_s __user *)arg);
			break;

		case MALI_IOC_PROFILING_REPORT_SW_COUNTERS:
			err = profiling_report_sw_counters_wrapper(session_data, (_mali_uk_sw_counters_report_s __user *)arg);
			break;

#else

		case MALI_IOC_PROFILING_START:              /* FALL-THROUGH */
		case MALI_IOC_PROFILING_ADD_EVENT:          /* FALL-THROUGH */
		case MALI_IOC_PROFILING_STOP:               /* FALL-THROUGH */
		case MALI_IOC_PROFILING_GET_EVENT:          /* FALL-THROUGH */
		case MALI_IOC_PROFILING_CLEAR:              /* FALL-THROUGH */
		case MALI_IOC_PROFILING_GET_CONFIG:         /* FALL-THROUGH */
		case MALI_IOC_PROFILING_REPORT_SW_COUNTERS: /* FALL-THROUGH */
			MALI_DEBUG_PRINT(2, ("Profiling not supported\n"));
			err = -ENOTTY;
			break;

#endif

		case MALI_IOC_MEM_INIT:
			err = mem_init_wrapper(session_data, (_mali_uk_init_mem_s __user *)arg);
			break;

		case MALI_IOC_MEM_TERM:
			err = mem_term_wrapper(session_data, (_mali_uk_term_mem_s __user *)arg);
			break;

		case MALI_IOC_MEM_MAP_EXT:
			err = mem_map_ext_wrapper(session_data, (_mali_uk_map_external_mem_s __user *)arg);
			break;

		case MALI_IOC_MEM_UNMAP_EXT:
			err = mem_unmap_ext_wrapper(session_data, (_mali_uk_unmap_external_mem_s __user *)arg);
			break;

		case MALI_IOC_MEM_QUERY_MMU_PAGE_TABLE_DUMP_SIZE:
			err = mem_query_mmu_page_table_dump_size_wrapper(session_data, (_mali_uk_query_mmu_page_table_dump_size_s __user *)arg);
			break;

		case MALI_IOC_MEM_DUMP_MMU_PAGE_TABLE:
			err = mem_dump_mmu_page_table_wrapper(session_data, (_mali_uk_dump_mmu_page_table_s __user *)arg);
			break;

#if defined(CONFIG_MALI400_UMP)

		case MALI_IOC_MEM_ATTACH_UMP:
			err = mem_attach_ump_wrapper(session_data, (_mali_uk_attach_ump_mem_s __user *)arg);
			break;

		case MALI_IOC_MEM_RELEASE_UMP:
			err = mem_release_ump_wrapper(session_data, (_mali_uk_release_ump_mem_s __user *)arg);
			break;

#else

		case MALI_IOC_MEM_ATTACH_UMP:
		case MALI_IOC_MEM_RELEASE_UMP: /* FALL-THROUGH */
			MALI_DEBUG_PRINT(2, ("UMP not supported\n"));
			err = -ENOTTY;
			break;
#endif

#ifdef CONFIG_DMA_SHARED_BUFFER
		case MALI_IOC_MEM_ATTACH_DMA_BUF:
			err = mali_attach_dma_buf(session_data, (_mali_uk_attach_dma_buf_s __user *)arg);
			break;

		case MALI_IOC_MEM_RELEASE_DMA_BUF:
			err = mali_release_dma_buf(session_data, (_mali_uk_release_dma_buf_s __user *)arg);
			break;

		case MALI_IOC_MEM_DMA_BUF_GET_SIZE:
			err = mali_dma_buf_get_size(session_data, (_mali_uk_dma_buf_get_size_s __user *)arg);
			break;
#else

		case MALI_IOC_MEM_ATTACH_DMA_BUF:   /* FALL-THROUGH */
		case MALI_IOC_MEM_RELEASE_DMA_BUF:  /* FALL-THROUGH */
		case MALI_IOC_MEM_DMA_BUF_GET_SIZE: /* FALL-THROUGH */
			MALI_DEBUG_PRINT(2, ("DMA-BUF not supported\n"));
			err = -ENOTTY;
			break;
#endif

		case MALI_IOC_PP_START_JOB:
			err = pp_start_job_wrapper(session_data, (_mali_uk_pp_start_job_s __user *)arg);
			break;

		case MALI_IOC_PP_NUMBER_OF_CORES_GET:
			err = pp_get_number_of_cores_wrapper(session_data, (_mali_uk_get_pp_number_of_cores_s __user *)arg);
			break;

		case MALI_IOC_PP_CORE_VERSION_GET:
			err = pp_get_core_version_wrapper(session_data, (_mali_uk_get_pp_core_version_s __user *)arg);
			break;

		case MALI_IOC_PP_DISABLE_WB:
			err = pp_disable_wb_wrapper(session_data, (_mali_uk_pp_disable_wb_s __user *)arg);
			break;

		case MALI_IOC_GP2_START_JOB:
			err = gp_start_job_wrapper(session_data, (_mali_uk_gp_start_job_s __user *)arg);
			break;

		case MALI_IOC_GP2_NUMBER_OF_CORES_GET:
			err = gp_get_number_of_cores_wrapper(session_data, (_mali_uk_get_gp_number_of_cores_s __user *)arg);
			break;

		case MALI_IOC_GP2_CORE_VERSION_GET:
			err = gp_get_core_version_wrapper(session_data, (_mali_uk_get_gp_core_version_s __user *)arg);
			break;

		case MALI_IOC_GP2_SUSPEND_RESPONSE:
			err = gp_suspend_response_wrapper(session_data, (_mali_uk_gp_suspend_response_s __user *)arg);
			break;

		case MALI_IOC_VSYNC_EVENT_REPORT:
			err = vsync_event_report_wrapper(session_data, (_mali_uk_vsync_event_report_s __user *)arg);
			break;

		case MALI_IOC_STREAM_CREATE:
#if defined(CONFIG_SYNC)
/* MALI_SEC */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
			err = stream_create_wrapper(session_data, (_mali_uk_stream_create_s __user *)arg);
#else
			err = -ENOTTY;
#endif
			break;
#endif
		case MALI_IOC_FENCE_VALIDATE:
#if defined(CONFIG_SYNC)
/* MALI_SEC */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
			err = sync_fence_validate_wrapper(session_data, (_mali_uk_fence_validate_s __user *)arg);
#else
			MALI_DEBUG_PRINT(2, ("Sync objects not supported\n"));
			err = -ENOTTY;
#endif
			break;
#else
			MALI_DEBUG_PRINT(2, ("Sync objects not supported\n"));
			err = -ENOTTY;
			break;
#endif

		case MALI_IOC_MEM_GET_BIG_BLOCK: /* Fallthrough */
		case MALI_IOC_MEM_FREE_BIG_BLOCK:
			MALI_PRINT_ERROR(("Non-MMU mode is no longer supported.\n"));
			err = -ENOTTY;
			break;

		default:
			MALI_DEBUG_PRINT(2, ("No handler for ioctl 0x%08X 0x%08lX\n", cmd, arg));
			err = -ENOTTY;
	};

	return err;
}


module_init(mali_module_init);
module_exit(mali_module_exit);

MODULE_LICENSE(MALI_KERNEL_LINUX_LICENSE);
MODULE_AUTHOR("ARM Ltd.");
MODULE_VERSION(SVN_REV_STRING);
