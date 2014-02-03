/*
 * Copyright (C) 2011-2013 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_pm.h"
#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_gp_scheduler.h"
#include "mali_pp_scheduler.h"
#include "mali_scheduler.h"
#include "mali_kernel_utilization.h"
#include "mali_group.h"
#include "mali_pm_domain.h"
#include "mali_pmu.h"

#include <linux/pm_runtime.h>

extern struct platform_device *mali_platform_device;

static mali_bool mali_power_on = MALI_FALSE;

_mali_osk_errcode_t mali_pm_initialize(void)
{
#ifdef CONFIG_PM_RUNTIME
	pm_runtime_set_autosuspend_delay(&(mali_platform_device->dev), 1000);
	pm_runtime_use_autosuspend(&(mali_platform_device->dev));
	pm_runtime_enable(&(mali_platform_device->dev));
#endif
	_mali_osk_pm_dev_enable();
	return _MALI_OSK_ERR_OK;
}

void mali_pm_terminate(void)
{
#ifdef CONFIG_PM_RUNTIME
	pm_runtime_disable(&(mali_platform_device->dev));
#endif
	mali_pm_domain_terminate();
	_mali_osk_pm_dev_disable();
}

/* Reset GPU after power up */
static void mali_pm_reset_gpu(void)
{
	/* Reset all L2 caches */
	mali_l2_cache_reset_all();

	/* Reset all groups */
	mali_scheduler_reset_all_groups();
}

void mali_pm_os_suspend(void)
{
	MALI_DEBUG_PRINT(3, ("Mali PM: OS suspend\n"));
	mali_gp_scheduler_suspend();
	mali_pp_scheduler_suspend();
	mali_utilization_suspend();
	mali_group_power_off(MALI_TRUE);
	mali_power_on = MALI_FALSE;
}

void mali_pm_os_resume(void)
{
	struct mali_pmu_core *pmu = mali_pmu_get_global_pmu_core();
	mali_bool do_reset = MALI_FALSE;

	MALI_DEBUG_PRINT(3, ("Mali PM: OS resume\n"));

	if (MALI_TRUE != mali_power_on) {
		do_reset = MALI_TRUE;
	}

	if (NULL != pmu) {
		mali_pmu_reset(pmu);
	}

	mali_power_on = MALI_TRUE;
	_mali_osk_write_mem_barrier();

	if (do_reset) {
		mali_pm_reset_gpu();
		mali_group_power_on();
	}

	mali_gp_scheduler_resume();
	mali_pp_scheduler_resume();
}

void mali_pm_runtime_suspend(void)
{
	MALI_DEBUG_PRINT(3, ("Mali PM: Runtime suspend\n"));
	mali_group_power_off(MALI_TRUE);
	mali_power_on = MALI_FALSE;
}

void mali_pm_runtime_resume(void)
{
	struct mali_pmu_core *pmu = mali_pmu_get_global_pmu_core();
	mali_bool do_reset = MALI_FALSE;

	MALI_DEBUG_PRINT(3, ("Mali PM: Runtime resume\n"));

	if (MALI_TRUE != mali_power_on) {
		do_reset = MALI_TRUE;
	}

	if (NULL != pmu) {
		mali_pmu_reset(pmu);
	}

	mali_power_on = MALI_TRUE;
	_mali_osk_write_mem_barrier();

	if (do_reset) {
		mali_pm_reset_gpu();
		mali_group_power_on();
	}
}

void mali_pm_set_power_is_on(void)
{
	mali_power_on = MALI_TRUE;
}

mali_bool mali_pm_is_power_on(void)
{
	return mali_power_on;
}
