/*
 *
 * (C) COPYRIGHT 2010-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



#ifndef _OSK_H_
#define _OSK_H_

/**
 * @addtogroup base_api
 * @{
 */

/**
 * @defgroup base_osk_api Kernel-side OSK APIs
 */

/** @} */ /* end group base_api */

#include "include/mali_osk_types.h"
#include "include/mali_osk_debug.h"
#if (1== MALI_BASE_TRACK_MEMLEAK)
#include "include/mali_osk_failure.h"
#endif
#include "include/mali_osk_math.h"
#include "include/mali_osk_lists.h"
#include "include/mali_osk_lock_order.h"
#include "include/mali_osk_locks.h"
#include "include/mali_osk_atomics.h"
#include "include/mali_osk_timers.h"
#include "include/mali_osk_time.h"
#include "include/mali_osk_bitops.h"
#include "include/mali_osk_workq.h"
#include "include/mali_osk_mem.h"
#include "include/mali_osk_low_level_mem.h"
#include "include/mali_osk_waitq.h"
#include "include/mali_osk_power.h"
#include "include/mali_osk_credentials.h"

#endif /* _OSK_H_ */
