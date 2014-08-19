/**
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2010-2014 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/**
 * @file mali_osk_pm.c
 * Implementation of the callback functions from common power management
 */

#include <linux/sched.h>

#ifdef CONFIG_PM_RUNTIME
#include <linux/pm_runtime.h>
#endif /* CONFIG_PM_RUNTIME */
#include <linux/platform_device.h>
#include <linux/version.h>
#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_kernel_linux.h"

static _mali_osk_atomic_t mali_pm_ref_count;

void _mali_osk_pm_dev_enable(void)
{
	_mali_osk_atomic_init(&mali_pm_ref_count, 0);
}

void _mali_osk_pm_dev_disable(void)
{
	_mali_osk_atomic_term(&mali_pm_ref_count);
}

/* Can NOT run in atomic context */
_mali_osk_errcode_t _mali_osk_pm_dev_ref_add(void)
{
#ifdef CONFIG_PM_RUNTIME
	int err;
	MALI_DEBUG_ASSERT_POINTER(mali_platform_device);
	err = pm_runtime_get_sync(&(mali_platform_device->dev));
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	pm_runtime_mark_last_busy(&(mali_platform_device->dev));
#endif
	if (0 > err) {
		MALI_PRINT_ERROR(("Mali OSK PM: pm_runtime_get_sync() returned error code %d\n", err));
		return _MALI_OSK_ERR_FAULT;
	}
	_mali_osk_atomic_inc(&mali_pm_ref_count);
	MALI_DEBUG_PRINT(4, ("Mali OSK PM: Power ref taken (%u)\n", _mali_osk_atomic_read(&mali_pm_ref_count)));
#endif
	return _MALI_OSK_ERR_OK;
}

/* Can run in atomic context */
void _mali_osk_pm_dev_ref_dec(void)
{
#ifdef CONFIG_PM_RUNTIME
	MALI_DEBUG_ASSERT_POINTER(mali_platform_device);
	_mali_osk_atomic_dec(&mali_pm_ref_count);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	pm_runtime_mark_last_busy(&(mali_platform_device->dev));
	pm_runtime_put_autosuspend(&(mali_platform_device->dev));
#else
	pm_runtime_put(&(mali_platform_device->dev));
#endif
	MALI_DEBUG_PRINT(4, ("Mali OSK PM: Power ref released (%u)\n", _mali_osk_atomic_read(&mali_pm_ref_count)));
#endif
}

/* Can run in atomic context */
mali_bool _mali_osk_pm_dev_ref_add_no_power_on(void)
{
#ifdef CONFIG_PM_RUNTIME
	u32 ref;
	MALI_DEBUG_ASSERT_POINTER(mali_platform_device);
	pm_runtime_get_noresume(&(mali_platform_device->dev));
	ref = _mali_osk_atomic_read(&mali_pm_ref_count);
	MALI_DEBUG_PRINT(4, ("Mali OSK PM: No-power ref taken (%u)\n", _mali_osk_atomic_read(&mali_pm_ref_count)));
	return ref > 0 ? MALI_TRUE : MALI_FALSE;
#else
	return MALI_TRUE;
#endif
}

/* Can run in atomic context */
void _mali_osk_pm_dev_ref_dec_no_power_on(void)
{
#ifdef CONFIG_PM_RUNTIME
	MALI_DEBUG_ASSERT_POINTER(mali_platform_device);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	pm_runtime_put_autosuspend(&(mali_platform_device->dev));
#else
	pm_runtime_put(&(mali_platform_device->dev));
#endif
	MALI_DEBUG_PRINT(4, ("Mali OSK PM: No-power ref released (%u)\n", _mali_osk_atomic_read(&mali_pm_ref_count)));
#endif
}

void _mali_osk_pm_dev_barrier(void)
{
#ifdef CONFIG_PM_RUNTIME
	pm_runtime_barrier(&(mali_platform_device->dev));
#endif
}
