/**
 * Copyright (C) 2010-2013 ARM Limited. All rights reserved.
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

#include <linux/module.h>
#include <linux/mali/mali_utgard.h>
#include "mali_gp_scheduler.h"
#include "mali_pp_scheduler.h"

void mali_dev_pause(void)
{
	mali_gp_scheduler_suspend();
	mali_pp_scheduler_suspend();
	mali_group_power_off(MALI_FALSE);
	mali_l2_cache_pause_all(MALI_TRUE);
}

EXPORT_SYMBOL(mali_dev_pause);

void mali_dev_resume(void)
{
	mali_l2_cache_pause_all(MALI_FALSE);
	mali_gp_scheduler_resume();
	mali_pp_scheduler_resume();
}

EXPORT_SYMBOL(mali_dev_resume);
