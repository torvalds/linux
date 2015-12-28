/*
 * Copyright (C) 2010-2012, 2014-2015 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_CONTROL_TIMER_H__
#define __MALI_CONTROL_TIMER_H__

#include "mali_osk.h"

_mali_osk_errcode_t mali_control_timer_init(void);

void mali_control_timer_term(void);

mali_bool mali_control_timer_resume(u64 time_now);

void mali_control_timer_suspend(mali_bool suspend);
void mali_control_timer_pause(void);

void mali_control_timer_add(u32 timeout);

#endif /* __MALI_CONTROL_TIMER_H__ */

