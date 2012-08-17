/**
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
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
#include "mali_kernel_common.h"
#include "mali_session.h"
#include "mali_kernel_core.h"
#include "mali_osk.h"
#include "mali_kernel_linux.h"
#include "mali_ukk.h"
#include "mali_ukk_wrappers.h"
#include "mali_kernel_pm.h"
#include "mali_kernel_sysfs.h"
#include "mali_platform.h"
#include "mali_kernel_license.h"

/* Streamline support for the Mali driver */
#if defined(CONFIG_TRACEPOINTS) && MALI_TIMELINE_PROFILING_ENABLED
/* Ask Linux to create the tracepoints */
#define CREATE_TRACE_POINTS
#include "mali_linux_trace.h"
#endif /* CONFIG_TRACEPOINTS */

static _mali_osk_errcode_t initialize_kernel_device(void);
static int initialize_sysfs(void);
static void terminate_kernel_device(void);


/* from the __malidrv_build_info.c file that is generated during build */
extern const char *__malidrv_build_info(void);

/* Module parameter to control log level */
int mali_debug_level = 2;
module_param(mali_debug_level, int, S_IRUSR | S_IWUSR | S_IWGRP | S_IRGRP | S_IROTH); /* rw-rw-r-- */
MODULE_PARM_DESC(mali_debug_level, "Higher number, more dmesg output");

/* By default the module uses any available major, but it's possible to set it at load time to a specific number */
int mali_major = 0;
module_param(mali_major, int, S_IRUGO); /* r--r--r-- */
MODULE_PARM_DESC(mali_major, "Device major number");

module_param(mali_max_job_runtime, int, S_IRUSR | S_IWUSR | S_IWGRP | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(mali_max_job_runtime, "Maximum allowed job runtime in msecs.\nJobs will be killed after this no matter what");

extern int mali_l2_max_reads;
module_param(mali_l2_max_reads, int, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(mali_l2_max_reads, "Maximum reads for Mali L2 cache");

#if MALI_TIMELINE_PROFILING_ENABLED
extern int mali_boot_profiling;
module_param(mali_boot_profiling, int, S_IRUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(mali_boot_profiling, "Start profiling as a part of Mali driver initialization");
#endif

/* Export symbols from common code: mali_user_settings.c */
#include "mali_user_settings_db.h"
EXPORT_SYMBOL(mali_set_user_setting);
EXPORT_SYMBOL(mali_get_user_setting);

static char mali_dev_name[] = "mali"; /* should be const, but the functions we call requires non-cost */

/* the mali device */
static struct mali_dev device;


static int mali_open(struct inode *inode, struct file *filp);
static int mali_release(struct inode *inode, struct file *filp);
#ifdef HAVE_UNLOCKED_IOCTL
static long mali_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
#else
static int mali_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg);
#endif

static int mali_mmap(struct file * filp, struct vm_area_struct * vma);

/* Linux char file operations provided by the Mali module */
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


int mali_driver_init(void)
{
	int ret = 0;

	MALI_DEBUG_PRINT(2, ("\n"));
	MALI_DEBUG_PRINT(2, ("Inserting Mali v%d device driver. \n",_MALI_API_VERSION));
	MALI_DEBUG_PRINT(2, ("Compiled: %s, time: %s.\n", __DATE__, __TIME__));
	MALI_DEBUG_PRINT(2, ("Driver revision: %s\n", SVN_REV_STRING));

	ret = _mali_dev_platform_register();
	if (0 != ret) goto platform_register_failed;
	ret = map_errcode(initialize_kernel_device());
	if (0 != ret) goto initialize_kernel_device_failed;

	ret = map_errcode(mali_platform_init());
	if (0 != ret) goto platform_init_failed;

	mali_osk_low_level_mem_init();

	ret = map_errcode(mali_initialize_subsystems());
	if (0 != ret) goto initialize_subsystems_failed;

	ret = initialize_sysfs();
	if (0 != ret) goto initialize_sysfs_failed;

	MALI_PRINT(("Mali device driver loaded\n"));

	return 0; /* Success */

	/* Error handling */
initialize_sysfs_failed:
	mali_terminate_subsystems();
initialize_subsystems_failed:
	mali_osk_low_level_mem_term();
	mali_platform_deinit();
platform_init_failed:
	terminate_kernel_device();
initialize_kernel_device_failed:
	_mali_dev_platform_unregister();
platform_register_failed:
	return ret;
}

void mali_driver_exit(void)
{
	MALI_DEBUG_PRINT(2, ("\n"));
	MALI_DEBUG_PRINT(2, ("Unloading Mali v%d device driver.\n",_MALI_API_VERSION));

	/* No need to terminate sysfs, this will be done automatically along with device termination */

	mali_terminate_subsystems();

	mali_osk_low_level_mem_term();

	mali_platform_deinit();

	terminate_kernel_device();
	_mali_dev_platform_unregister();

#if MALI_LICENSE_IS_GPL
	/* @@@@ clean up the work queues! This should not be terminated here, since it isn't inited in the function above! */
	flush_workqueue(mali_wq);
	destroy_workqueue(mali_wq);
	mali_wq = NULL;
#endif

	MALI_PRINT(("Mali device driver unloaded\n"));
}

static int initialize_kernel_device(void)
{
	int err;
	dev_t dev = 0;
	if (0 == mali_major)
	{
		/* auto select a major */
		err = alloc_chrdev_region(&dev, 0/*first minor*/, 1/*count*/, mali_dev_name);
		mali_major = MAJOR(dev);
	}
	else
	{
		/* use load time defined major number */
		dev = MKDEV(mali_major, 0);
		err = register_chrdev_region(dev, 1/*count*/, mali_dev_name);
	}

	if (err)
	{
			goto init_chrdev_err;
	}

	memset(&device, 0, sizeof(device));

	/* initialize our char dev data */
	cdev_init(&device.cdev, &mali_fops);
	device.cdev.owner = THIS_MODULE;
	device.cdev.ops = &mali_fops;

	/* register char dev with the kernel */
	err = cdev_add(&device.cdev, dev, 1/*count*/);
	if (err)
	{
			goto init_cdev_err;
	}

	/* Success! */
	return 0;

init_cdev_err:
	unregister_chrdev_region(dev, 1/*count*/);
init_chrdev_err:
	return err;
}

static int initialize_sysfs(void)
{
	dev_t dev = MKDEV(mali_major, 0);
	return mali_sysfs_register(&device, dev, mali_dev_name);
}

static void terminate_kernel_device(void)
{
	dev_t dev = MKDEV(mali_major, 0);

	mali_sysfs_unregister(&device, dev, mali_dev_name);

	/* unregister char device */
	cdev_del(&device.cdev);
	/* free major */
	unregister_chrdev_region(dev, 1/*count*/);
	return;
}

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

	MALI_DEBUG_PRINT(4, ("MMap() handler: start=0x%08X, phys=0x%08X, size=0x%08X\n", (unsigned int)vma->vm_start, (unsigned int)(vma->vm_pgoff << PAGE_SHIFT), (unsigned int)(vma->vm_end - vma->vm_start)) );

	/* Re-pack the arguments that mmap() packed for us */
	args.ctx = session_data;
	args.phys_addr = vma->vm_pgoff << PAGE_SHIFT;
	args.size = vma->vm_end - vma->vm_start;
	args.ukk_private = vma;

	/* Call the common mmap handler */
	MALI_CHECK(_MALI_OSK_ERR_OK ==_mali_ukk_mem_mmap( &args ), -EFAULT);

    return 0;
}

static int mali_open(struct inode *inode, struct file *filp)
{
	struct mali_session_data * session_data;
    _mali_osk_errcode_t err;

	/* input validation */
	if (0 != MINOR(inode->i_rdev)) return -ENODEV;

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
	if (0 != MINOR(inode->i_rdev)) return -ENODEV;

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
		case MALI_IOC_GET_SYSTEM_INFO_SIZE:
			err = get_system_info_size_wrapper(session_data, (_mali_uk_get_system_info_size_s __user *)arg);
			break;

		case MALI_IOC_GET_SYSTEM_INFO:
			err = get_system_info_wrapper(session_data, (_mali_uk_get_system_info_s __user *)arg);
			break;

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

#if MALI_TIMELINE_PROFILING_ENABLED
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

#if MALI_USE_UNIFIED_MEMORY_PROVIDER != 0

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


module_init(mali_driver_init);
module_exit(mali_driver_exit);

MODULE_LICENSE(MALI_KERNEL_LINUX_LICENSE);
MODULE_AUTHOR("ARM Ltd.");
MODULE_VERSION(SVN_REV_STRING);
