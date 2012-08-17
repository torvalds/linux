/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_GP_JOB_H__
#define __MALI_GP_JOB_H__

#include "mali_osk.h"
#include "mali_osk_list.h"
#include "mali_uk_types.h"
#include "mali_session.h"

/**
 * The structure represends a GP job, including all sub-jobs
 * (This struct unfortunatly needs to be public because of how the _mali_osk_list_*
 * mechanism works)
 */
struct mali_gp_job
{
	_mali_osk_list_t list;                             /**< Used to link jobs together in the scheduler queue */
	struct mali_session_data *session;                 /**< Session which submitted this job */
	u32 id;                                            /**< identifier for this job in kernel space (sequential numbering) */
	u32 user_id;                                       /**< identifier for the job in user space */
	u32 frame_registers[MALIGP2_NUM_REGS_FRAME];       /**< core specific registers associated with this job, see ARM DDI0415A */
	u32 heap_current_addr;                             /**< Holds the current HEAP address when the job has completed */
	u32 perf_counter_flag;                             /**< bitmask indicating which performance counters to enable, see \ref _MALI_PERFORMANCE_COUNTER_FLAG_SRC0_ENABLE and related macro definitions */
	u32 perf_counter_src0;                             /**< source id for performance counter 0 (see ARM DDI0415A, Table 3-60) */
	u32 perf_counter_src1;                             /**< source id for performance counter 1 (see ARM DDI0415A, Table 3-60) */
	u32 perf_counter_value0;                           /**< Value of performance counter 0 (to be returned to user space) */
	u32 perf_counter_value1;                           /**< Value of performance counter 1 (to be returned to user space) */
	u32 pid;                                           /**< Process ID of submitting process */
	u32 tid;                                           /**< Thread ID of submitting thread */
	u32 frame_builder_id;                              /**< id of the originating frame builder */
	u32 flush_id;                                      /**< flush id within the originating frame builder */
};

struct mali_gp_job *mali_gp_job_create(struct mali_session_data *session, _mali_uk_gp_start_job_s *args, u32 id);
void mali_gp_job_delete(struct mali_gp_job *job);

MALI_STATIC_INLINE u32 mali_gp_job_get_id(struct mali_gp_job *job)
{
	return (NULL == job) ? 0 : job->id;
}

MALI_STATIC_INLINE u32 mali_gp_job_get_user_id(struct mali_gp_job *job)
{
	return job->user_id;
}

MALI_STATIC_INLINE u32 mali_gp_job_get_frame_builder_id(struct mali_gp_job *job)
{
	return job->frame_builder_id;
}

MALI_STATIC_INLINE u32 mali_gp_job_get_flush_id(struct mali_gp_job *job)
{
	return job->flush_id;
}

MALI_STATIC_INLINE u32* mali_gp_job_get_frame_registers(struct mali_gp_job *job)
{
	return job->frame_registers;
}

MALI_STATIC_INLINE struct mali_session_data *mali_gp_job_get_session(struct mali_gp_job *job)
{
	return job->session;
}

MALI_STATIC_INLINE mali_bool mali_gp_job_has_vs_job(struct mali_gp_job *job)
{
	return (job->frame_registers[0] != job->frame_registers[1]) ? MALI_TRUE : MALI_FALSE;
}

MALI_STATIC_INLINE mali_bool mali_gp_job_has_plbu_job(struct mali_gp_job *job)
{
	return (job->frame_registers[2] != job->frame_registers[3]) ? MALI_TRUE : MALI_FALSE;
}

MALI_STATIC_INLINE u32 mali_gp_job_get_current_heap_addr(struct mali_gp_job *job)
{
	return job->heap_current_addr;
}

MALI_STATIC_INLINE void mali_gp_job_set_current_heap_addr(struct mali_gp_job *job, u32 heap_addr)
{
	job->heap_current_addr = heap_addr;
}

MALI_STATIC_INLINE u32 mali_gp_job_get_perf_counter_flag(struct mali_gp_job *job)
{
	return job->perf_counter_flag;
}

MALI_STATIC_INLINE u32 mali_gp_job_get_perf_counter_src0(struct mali_gp_job *job)
{
	return job->perf_counter_src0;
}

MALI_STATIC_INLINE u32 mali_gp_job_get_perf_counter_src1(struct mali_gp_job *job)
{
	return job->perf_counter_src1;
}

MALI_STATIC_INLINE u32 mali_gp_job_get_perf_counter_value0(struct mali_gp_job *job)
{
	return job->perf_counter_value0;
}

MALI_STATIC_INLINE u32 mali_gp_job_get_perf_counter_value1(struct mali_gp_job *job)
{
	return job->perf_counter_value1;
}

MALI_STATIC_INLINE void mali_gp_job_set_perf_counter_value0(struct mali_gp_job *job, u32 value)
{
	job->perf_counter_value0 = value;
}

MALI_STATIC_INLINE void mali_gp_job_set_perf_counter_value1(struct mali_gp_job *job, u32 value)
{
	job->perf_counter_value1 = value;
}

#endif /* __MALI_GP_JOB_H__ */
