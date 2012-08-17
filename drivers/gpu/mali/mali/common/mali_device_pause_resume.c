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
#include "mali_pm.h"

void mali_dev_pause(mali_bool *power_is_on)
{
	mali_gp_scheduler_suspend();
	mali_pp_scheduler_suspend();

	/*
	 * Take and hold the PM lock to be sure we don't change power state as well.
	 * (it might be unsafe to for instance change frequency if Mali GPU is powered off)
	 */
	mali_pm_execute_state_change_lock();
	if (NULL != power_is_on)
	{
		*power_is_on = mali_pm_is_powered_on();
	}
}

void mali_dev_resume(void)
{
	mali_pm_execute_state_change_unlock();
	mali_gp_scheduler_resume();
	mali_pp_scheduler_resume();
}

/*
EXPORT_SYMBOL(mali_dev_pause);
EXPORT_SYMBOL(mali_dev_resume);
*/
