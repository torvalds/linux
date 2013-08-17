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
 * @file mali_pmu_power_up_down.c
 */

#include <linux/version.h>
#include <linux/sched.h>
#include <linux/module.h>
#include "mali_osk.h"
#include "mali_kernel_common.h"
#include "mali_pmu.h"
#include "linux/mali/mali_utgard.h"

/* Mali PMU power up/down APIs */

int mali_pmu_powerup(void)
{
	struct mali_pmu_core *pmu = mali_pmu_get_global_pmu_core();

	MALI_DEBUG_PRINT(5, ("Mali PMU: Power up\n"));

	if (NULL == pmu)
	{
		return -ENXIO;
	}

	if (_MALI_OSK_ERR_OK != mali_pmu_powerup_all(pmu))
	{
		return -EFAULT;
	}

	return 0;
}

EXPORT_SYMBOL(mali_pmu_powerup);

int mali_pmu_powerdown(void)
{
	struct mali_pmu_core *pmu = mali_pmu_get_global_pmu_core();

	MALI_DEBUG_PRINT(5, ("Mali PMU: Power down\n"));

	if (NULL == pmu)
	{
		return -ENXIO;
	}

	if (_MALI_OSK_ERR_OK != mali_pmu_powerdown_all(pmu))
	{
		return -EFAULT;
	}

	return 0;
}

EXPORT_SYMBOL(mali_pmu_powerdown);
