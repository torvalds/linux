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
 * @file mali_platform.c
 * Platform specific Mali driver functions for the exynos 4XXX based platforms
 */
#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/pm.h>
#ifdef CONFIG_PM_RUNTIME
#include <linux/pm_runtime.h>
#endif
#include <linux/mali/mali_utgard.h>
#include "mali_kernel_common.h"

#include <plat/pd.h>

#include "exynos4_pmm.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
struct exynos_pm_domain;
extern struct exynos_pm_domain exynos4_pd_g3d;
void exynos_pm_add_dev_to_genpd(struct platform_device *pdev, struct exynos_pm_domain *pd);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)
extern struct platform_device exynos4_device_pd[];
#else
extern struct platform_device s5pv310_device_pd[];
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0) */

static void mali_platform_device_release(struct device *device);
static int mali_os_suspend(struct device *device);
static int mali_os_resume(struct device *device);
static int mali_os_freeze(struct device *device);
static int mali_os_thaw(struct device *device);
#ifdef CONFIG_PM_RUNTIME
static int mali_runtime_suspend(struct device *device);
static int mali_runtime_resume(struct device *device);
static int mali_runtime_idle(struct device *device);
#endif

#if defined(CONFIG_ARCH_S5PV310) && !defined(CONFIG_BOARD_HKDKC210)

/* This is for other SMDK boards */
#define MALI_BASE_IRQ 232

#else

/* This is for the Odroid boards */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0))
#define MALI_BASE_IRQ 182
#else
#define MALI_BASE_IRQ 150
#endif

#endif

#define MALI_GP_IRQ       MALI_BASE_IRQ + 9
#define MALI_PP0_IRQ      MALI_BASE_IRQ + 5
#define MALI_PP1_IRQ      MALI_BASE_IRQ + 6
#define MALI_PP2_IRQ      MALI_BASE_IRQ + 7
#define MALI_PP3_IRQ      MALI_BASE_IRQ + 8
#define MALI_GP_MMU_IRQ   MALI_BASE_IRQ + 4
#define MALI_PP0_MMU_IRQ  MALI_BASE_IRQ + 0
#define MALI_PP1_MMU_IRQ  MALI_BASE_IRQ + 1
#define MALI_PP2_MMU_IRQ  MALI_BASE_IRQ + 2
#define MALI_PP3_MMU_IRQ  MALI_BASE_IRQ + 3

static struct resource mali_gpu_resources[] =
{
	MALI_GPU_RESOURCES_MALI400_MP4(0x13000000,
	                               MALI_GP_IRQ, MALI_GP_MMU_IRQ,
	                               MALI_PP0_IRQ, MALI_PP0_MMU_IRQ,
	                               MALI_PP1_IRQ, MALI_PP1_MMU_IRQ,
	                               MALI_PP2_IRQ, MALI_PP2_MMU_IRQ,
	                               MALI_PP3_IRQ, MALI_PP3_MMU_IRQ)
};

static struct dev_pm_ops mali_gpu_device_type_pm_ops =
{
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

static struct device_type mali_gpu_device_device_type =
{
	.pm = &mali_gpu_device_type_pm_ops,
};

static struct platform_device mali_gpu_device =
{
	.name = MALI_GPU_NAME_UTGARD,
	.id = 0,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
	/* Set in mali_platform_device_register() for these kernels */
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)
	.dev.parent = &exynos4_device_pd[PD_G3D].dev,
#else
	.dev.parent = &s5pv310_device_pd[PD_G3D].dev,
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0) */
	.dev.release = mali_platform_device_release,
	/*
	 * We temporarily make use of a device type so that we can control the Mali power
	 * from within the mali.ko (since the default platform bus implementation will not do that).
	 * Ideally .dev.pm_domain should be used instead, as this is the new framework designed
	 * to control the power of devices.
	 */
	.dev.type = &mali_gpu_device_device_type, /* We should probably use the pm_domain instead of type on newer kernels */
};

static struct mali_gpu_device_data mali_gpu_data =
{
	.shared_mem_size = 256 * 1024 * 1024, /* 256MB */
	.fb_start = 0x40000000,
	.fb_size = 0xb1000000,
	.utilization_interval = 1000, /* 1000ms */
	.utilization_handler = mali_gpu_utilization_handler,
};

int mali_platform_device_register(void)
{
	int err;

	MALI_DEBUG_PRINT(4, ("mali_platform_device_register() called\n"));

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
	exynos_pm_add_dev_to_genpd(&mali_gpu_device, &exynos4_pd_g3d);
#endif

	/* Connect resources to the device */
	err = platform_device_add_resources(&mali_gpu_device, mali_gpu_resources, sizeof(mali_gpu_resources) / sizeof(mali_gpu_resources[0]));
	if (0 == err)
	{
		err = platform_device_add_data(&mali_gpu_device, &mali_gpu_data, sizeof(mali_gpu_data));
		if (0 == err)
		{
			/* Register the platform device */
			err = platform_device_register(&mali_gpu_device);
			if (0 == err)
			{
				mali_platform_init();

#ifdef CONFIG_PM_RUNTIME
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
				pm_runtime_set_autosuspend_delay(&(mali_gpu_device.dev), 1000);
				pm_runtime_use_autosuspend(&(mali_gpu_device.dev));
#endif
				pm_runtime_enable(&(mali_gpu_device.dev));
#endif

				return 0;
			}
		}

		platform_device_unregister(&mali_gpu_device);
	}

	return err;
}

void mali_platform_device_unregister(void)
{
	MALI_DEBUG_PRINT(4, ("mali_platform_device_unregister() called\n"));

	mali_platform_deinit();

	platform_device_unregister(&mali_gpu_device);
}

static void mali_platform_device_release(struct device *device)
{
	MALI_DEBUG_PRINT(4, ("mali_platform_device_release() called\n"));
}

static int mali_os_suspend(struct device *device)
{
	int ret = 0;

	MALI_DEBUG_PRINT(4, ("mali_os_suspend() called\n"));

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

static int mali_os_resume(struct device *device)
{
	int ret = 0;

	MALI_DEBUG_PRINT(4, ("mali_os_resume() called\n"));

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

static int mali_os_freeze(struct device *device)
{
	int ret = 0;

	MALI_DEBUG_PRINT(4, ("mali_os_freeze() called\n"));

	if (NULL != device->driver &&
	    NULL != device->driver->pm &&
	    NULL != device->driver->pm->freeze)
	{
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
	    NULL != device->driver->pm->thaw)
	{
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->thaw(device);
	}

	return ret;
}

#ifdef CONFIG_PM_RUNTIME
static int mali_runtime_suspend(struct device *device)
{
	int ret = 0;

	MALI_DEBUG_PRINT(4, ("mali_runtime_suspend() called\n"));

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

static int mali_runtime_resume(struct device *device)
{
	int ret = 0;

	MALI_DEBUG_PRINT(4, ("mali_runtime_resume() called\n"));

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

static int mali_runtime_idle(struct device *device)
{
	MALI_DEBUG_PRINT(4, ("mali_runtime_idle() called\n"));

	if (NULL != device->driver &&
	    NULL != device->driver->pm &&
	    NULL != device->driver->pm->runtime_idle)
	{
		/* Need to notify Mali driver about this event */
		int ret = device->driver->pm->runtime_idle(device);
		if (0 != ret)
		{
			return ret;
		}
	}

	pm_runtime_suspend(device);

	return 0;
}
#endif
