/*
 * Copyright (C) 2012-2013 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_GP_SCHEDULER_H__
#define __MALI_GP_SCHEDULER_H__

#include "mali_osk.h"
#include "mali_gp_job.h"
#include "mali_group.h"

_mali_osk_errcode_t mali_gp_scheduler_initialize(void);
void mali_gp_scheduler_terminate(void);

void mali_gp_scheduler_job_done(struct mali_group *group, struct mali_gp_job *job, mali_bool success);
void mali_gp_scheduler_oom(struct mali_group *group, struct mali_gp_job *job);
u32 mali_gp_scheduler_dump_state(char *buf, u32 size);

void mali_gp_scheduler_suspend(void);
void mali_gp_scheduler_resume(void);

/**
 * @brief Abort all running and queued GP jobs from session.
 *
* This functions aborts all GP jobs from the specified session. Queued jobs are removed from the
* queue and jobs currently running on a core will be aborted.
 *
 * @param session Session that is aborting.
 */
void mali_gp_scheduler_abort_session(struct mali_session_data *session);

/**
 * @brief Reset all groups
 *
 * This function resets all groups known by the GP scheuduler. This must be
 * called after the Mali HW has been powered on in order to reset the HW.
 */
void mali_gp_scheduler_reset_all_groups(void);

/**
 * @brief Zap TLB on all groups with \a session active
 *
 * The scheculer will zap the session on all groups it owns.
 */
void mali_gp_scheduler_zap_all_active(struct mali_session_data *session);

/**
 * @brief Re-enable a group that has been disabled with mali_gp_scheduler_disable_group
 *
 * If a Mali PMU is present, the group will be powered back on and added back
 * into the GP scheduler.
 *
 * @param group Pointer to the group to enable
 */
void mali_gp_scheduler_enable_group(struct mali_group *group);

/**
 * @brief Disable a group
 *
 * The group will be taken out of the GP scheduler and powered off, if a Mali
 * PMU is present.
 *
 * @param group Pointer to the group to disable
 */
void mali_gp_scheduler_disable_group(struct mali_group *group);

/**
 * @brief Used by the Timeline system to queue a GP job.
 *
 * @note @ref mali_scheduler_schedule_from_mask() should be called if this function returns non-zero.
 *
 * @param job The GP job that is being activated.
 *
 * @return A scheduling bitmask that can be used to decide if scheduling is necessary after this
 * call.
 */
mali_scheduler_mask mali_gp_scheduler_activate_job(struct mali_gp_job *job);

/**
 * @brief Schedule queued jobs on idle cores.
 */
void mali_gp_scheduler_schedule(void);

/**
 * @brief Submit a GP job to the GP scheduler.
 *
 * This will add the GP job to the Timeline system.
 *
 * @param session Session this job belongs to.
 * @param job GP job that will be submitted
 * @return Point on GP timeline for job.
 */
mali_timeline_point mali_gp_scheduler_submit_job(struct mali_session_data *session, struct mali_gp_job *job);

#endif /* __MALI_GP_SCHEDULER_H__ */
