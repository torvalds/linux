/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
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

static mali_bool mali_power_on = MALI_FALSE;

_mali_osk_errcode_t mali_pm_initialize(void)
{
	_mali_osk_pm_dev_enable();
	return _MALI_OSK_ERR_OK;
}

void mali_pm_terminate(void)
{
	_mali_osk_pm_dev_disable();
}

void mali_pm_core_event(enum mali_core_event core_event)
{
	MALI_DEBUG_ASSERT(MALI_CORE_EVENT_GP_START == core_event ||
	                  MALI_CORE_EVENT_PP_START == core_event ||
	                  MALI_CORE_EVENT_GP_STOP  == core_event ||
	                  MALI_CORE_EVENT_PP_STOP  == core_event);

	if (MALI_CORE_EVENT_GP_START == core_event || MALI_CORE_EVENT_PP_START == core_event)
	{
		_mali_osk_pm_dev_ref_add();
		if (mali_utilization_enabled())
		{
			mali_utilization_core_start(_mali_osk_time_get_ns());
		}
	}
	else
	{
		_mali_osk_pm_dev_ref_dec();
		if (mali_utilization_enabled())
		{
			mali_utilization_core_end(_mali_osk_time_get_ns());
		}
	}
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
	mali_group_power_off();
	mali_power_on = MALI_FALSE;
}

void mali_pm_os_resume(void)
{
	MALI_DEBUG_PRINT(3, ("Mali PM: OS resume\n"));
	if (MALI_TRUE != mali_power_on)
	{
		mali_pm_reset_gpu();
		mali_group_power_on();
	}
	mali_gp_scheduler_resume();
	mali_pp_scheduler_resume();
	mali_power_on = MALI_TRUE;
}

void mali_pm_runtime_suspend(void)
{
	MALI_DEBUG_PRINT(3, ("Mali PM: Runtime suspend\n"));
	mali_group_power_off();
	mali_power_on = MALI_FALSE;
}

void mali_pm_runtime_resume(void)
{
	MALI_DEBUG_PRINT(3, ("Mali PM: Runtime resume\n"));
	if (MALI_TRUE != mali_power_on)
	{
		mali_pm_reset_gpu();
		mali_group_power_on();
	}
	mali_power_on = MALI_TRUE;
}
