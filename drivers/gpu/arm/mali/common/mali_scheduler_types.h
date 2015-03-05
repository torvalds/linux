/*
 * Copyright (C) 2013-2014 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_SCHEDULER_TYPES_H__
#define __MALI_SCHEDULER_TYPES_H__

#include "mali_osk.h"

#define MALI_SCHEDULER_JOB_ID_SPAN 65535

/**
 * Bitmask used for defered scheduling of subsystems.
 */
typedef u32 mali_scheduler_mask;

#define MALI_SCHEDULER_MASK_GP (1<<0)
#define MALI_SCHEDULER_MASK_PP (1<<1)

#define MALI_SCHEDULER_MASK_EMPTY 0
#define MALI_SCHEDULER_MASK_ALL (MALI_SCHEDULER_MASK_GP | MALI_SCHEDULER_MASK_PP)

#endif /* __MALI_SCHEDULER_TYPES_H__ */
