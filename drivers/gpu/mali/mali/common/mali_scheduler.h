/*
 * Copyright (C) 2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_SCHEDULER_H__
#define __MALI_SCHEDULER_H__

#include "mali_osk.h"

_mali_osk_errcode_t mali_scheduler_initialize(void);
void mali_scheduler_terminate(void);

u32 mali_scheduler_get_new_id(void);

#endif /* __MALI_SCHEDULER_H__ */
