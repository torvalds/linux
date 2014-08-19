/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2011-2014 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
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

static mali_bool mali_power_on = MALI_FALSE;

_mali_osk_errcode_t mali_pm_initialize(void)
{
	_mali_osk_pm_dev_enable();
	return _MALI_OSK_ERR_OK;
}

void mali_pm_terminate(void)
{
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
