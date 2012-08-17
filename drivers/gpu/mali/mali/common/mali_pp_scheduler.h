/*
 * Copyright (C) 2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_PP_SCHEDULER_H__
#define __MALI_PP_SCHEDULER_H__

#include "mali_osk.h"
#include "mali_cluster.h"
#include "mali_pp_job.h"

_mali_osk_errcode_t mali_pp_scheduler_initialize(void);
void mali_pp_scheduler_terminate(void);

void mali_pp_scheduler_do_schedule(void);
void mali_pp_scheduler_job_done(struct mali_group *group, struct mali_pp_job *job, u32 sub_job, mali_bool success);

void mali_pp_scheduler_suspend(void);
void mali_pp_scheduler_resume(void);

/** @brief Abort all PP jobs from session running or queued
 *
 * This functions aborts all PP jobs from the specified session. Queued jobs are removed from the queue and jobs
 * currently running on a core will be aborted.
 *
 * @param session Pointer to session whose jobs should be aborted
 */
void mali_pp_scheduler_abort_session(struct mali_session_data *session);

u32 mali_pp_scheduler_dump_state(char *buf, u32 size);

#endif /* __MALI_PP_SCHEDULER_H__ */
