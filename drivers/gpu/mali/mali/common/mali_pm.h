/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
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

enum mali_core_event
{
	MALI_CORE_EVENT_GP_START,
	MALI_CORE_EVENT_GP_STOP,
	MALI_CORE_EVENT_PP_START,
	MALI_CORE_EVENT_PP_STOP
};

enum mali_pm_event
{
	MALI_PM_EVENT_CORES_WORKING,
	MALI_PM_EVENT_CORES_IDLE,
	MALI_PM_EVENT_TIMER_LIGHT_SLEEP,
	MALI_PM_EVENT_TIMER_DEEP_SLEEP,
	MALI_PM_EVENT_OS_SUSPEND,
	MALI_PM_EVENT_OS_RESUME,
	MALI_PM_EVENT_SCHEME_ALWAYS_ON,
	MALI_PM_EVENT_SCHEME_DYNAMIC_CONTROLL,
};

_mali_osk_errcode_t mali_pm_initialize(void);
void mali_pm_terminate(void);
void mali_pm_always_on(mali_bool enable);

void mali_pm_lock(void);
void mali_pm_unlock(void);
void mali_pm_execute_state_change_lock(void);

void mali_pm_execute_state_change_unlock(void);

mali_bool mali_pm_is_powered_on(void);

void mali_pm_core_event(enum mali_core_event core_event);

void mali_pm_os_suspend(void);
void mali_pm_os_resume(void);
void mali_pm_runtime_suspend(void);
void mali_pm_runtime_resume(void);


#endif /* __MALI_PM_H__ */
