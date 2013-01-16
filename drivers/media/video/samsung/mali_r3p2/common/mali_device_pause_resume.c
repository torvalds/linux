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
 * @file mali_device_pause_resume.c
 * Implementation of the Mali pause/resume functionality
 */

#include "mali_gp_scheduler.h"
#include "mali_pp_scheduler.h"
#include "mali_group.h"

void mali_dev_pause(mali_bool *power_is_on)
{
	mali_bool power_is_on_tmp;

	/* Locking the current power state - so it will not switch from being ON to OFF, but it might remain OFF */
	power_is_on_tmp = _mali_osk_pm_dev_ref_add_no_power_on();
	if (NULL != power_is_on)
	{
		*power_is_on = power_is_on_tmp;
	}

	mali_gp_scheduler_suspend();
	mali_pp_scheduler_suspend();
}

void mali_dev_resume(void)
{
	mali_gp_scheduler_resume();
	mali_pp_scheduler_resume();

	/* Release our PM reference, as it is now safe to turn of the GPU again */
	_mali_osk_pm_dev_ref_dec_no_power_on();
}
