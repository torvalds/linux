/*
 * Copyright (C) 2011-2013 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_PM_H__
#define __MALI_PM_H__

#include "mali_osk.h"

_mali_osk_errcode_t mali_pm_initialize(void);
void mali_pm_terminate(void);

/* Callback functions registered for the runtime PMM system */
void mali_pm_os_suspend(void);
void mali_pm_os_resume(void);
void mali_pm_runtime_suspend(void);
void mali_pm_runtime_resume(void);

void mali_pm_set_power_is_on(void);
mali_bool mali_pm_is_power_on(void);

#endif /* __MALI_PM_H__ */
