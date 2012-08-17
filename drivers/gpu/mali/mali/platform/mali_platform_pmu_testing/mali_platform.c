/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_platform.c
 * Platform specific Mali driver functions for a default platform
 */
#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_platform.h"
#include "mali_pmu.h"
#include "linux/mali/mali_utgard.h"

static u32 bPowerOff = 1;

_mali_osk_errcode_t mali_platform_init(void)
{
    MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_deinit(void)
{
    MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_power_mode_change(mali_power_mode power_mode)
{
	switch (power_mode)
	{
		case MALI_POWER_MODE_ON:
			if (bPowerOff == 1)
			{
				mali_pmu_powerup();
				bPowerOff = 0;
			}
		break;
		case MALI_POWER_MODE_LIGHT_SLEEP:
		case MALI_POWER_MODE_DEEP_SLEEP:
			
			if (bPowerOff == 0)
			{
				mali_pmu_powerdown();
				bPowerOff = 1;
			}

		break;
	}
	MALI_SUCCESS;
}

void mali_gpu_utilization_handler(u32 utilization)
{
}

void set_mali_parent_power_domain(void* dev)
{
}


