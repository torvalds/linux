/*
 * Copyright (C) 2012 ARM Limited. All rights reserved.
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
#include "mali_cluster.h"
#include "mali_gp_job.h"

_mali_osk_errcode_t mali_gp_scheduler_initialize(void);
void mali_gp_scheduler_terminate(void);

void mali_gp_scheduler_do_schedule(void);
void mali_gp_scheduler_job_done(struct mali_group *group, struct mali_gp_job *job, mali_bool success);
void mali_gp_scheduler_oom(struct mali_group *group, struct mali_gp_job *job);
void mali_gp_scheduler_abort_session(struct mali_session_data *session);
u32 mali_gp_scheduler_dump_state(char *buf, u32 size);

void mali_gp_scheduler_suspend(void);
void mali_gp_scheduler_resume(void);

#endif /* __MALI_GP_SCHEDULER_H__ */
