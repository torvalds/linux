/**
 * Copyright (C) 2010-2015 ARM Limited. All rights reserved.
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
#include "mali_pm.h"

void mali_dev_pause(void)
{
	/*
	 * Deactive all groups to prevent hardware being touched
	 * during the period of mali device pausing
	 */
	mali_pm_os_suspend(MALI_FALSE);
}

EXPORT_SYMBOL(mali_dev_pause);

void mali_dev_resume(void)
{
	mali_pm_os_resume();
}

EXPORT_SYMBOL(mali_dev_resume);
