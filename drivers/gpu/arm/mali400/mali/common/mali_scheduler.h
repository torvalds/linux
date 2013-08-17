/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
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
#include "mali_gp_scheduler.h"
#include "mali_pp_scheduler.h"

_mali_osk_errcode_t mali_scheduler_initialize(void);
void mali_scheduler_terminate(void);

u32 mali_scheduler_get_new_id(void);

/**
 * @brief Reset all groups
 *
 * This function resets all groups known by the both the PP and GP scheuduler.
 * This must be called after the Mali HW has been powered on in order to reset
 * the HW.
 */
MALI_STATIC_INLINE void mali_scheduler_reset_all_groups(void)
{
	mali_gp_scheduler_reset_all_groups();
	mali_pp_scheduler_reset_all_groups();
}

/**
 * @brief Zap TLB on all active groups running \a session
 *
 * @param session Pointer to the session to zap
 */
MALI_STATIC_INLINE void mali_scheduler_zap_all_active(struct mali_session_data *session)
{
	mali_gp_scheduler_zap_all_active(session);
	mali_pp_scheduler_zap_all_active(session);
}

#endif /* __MALI_SCHEDULER_H__ */
