/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2011-2014 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#ifndef __MALI_GP_JOB_H__
#define __MALI_GP_JOB_H__

#include "mali_osk.h"
#include "mali_osk_list.h"
#include "mali_uk_types.h"
#include "mali_session.h"
#include "mali_timeline.h"
#include "mali_scheduler_types.h"

/**
 * The structure represents a GP job, including all sub-jobs
 * (This struct unfortunately needs to be public because of how the _mali_osk_list_*
 * mechanism works)
 */
struct mali_gp_job {
	_mali_osk_list_t list;                             /**< Used to link jobs together in the scheduler queue */
	struct mali_session_data *session;                 /**< Session which submitted this job */
	_mali_uk_gp_start_job_s uargs;                     /**< Arguments from user space */
	u32 id;                                            /**< Identifier for this job in kernel space (sequential numbering) */
	u32 cache_order;                                   /**< Cache order used for L2 cache flushing (sequential numbering) */
	u32 heap_current_addr;                             /**< Holds the current HEAP address when the job has completed */
	u32 perf_counter_value0;                           /**< Value of performance counter 0 (to be returned to user space) */
	u32 perf_counter_value1;                           /**< Value of performance counter 1 (to be returned to user space) */
	u32 pid;                                           /**< Process ID of submitting process */
	u32 tid;                                           /**< Thread ID of submitting thread */
	_mali_osk_notification_t *finished_notification;   /**< Notification sent back to userspace on job complete */
	_mali_osk_notification_t *oom_notification;        /**< Notification sent back to userspace on OOM */
	struct mali_timeline_tracker tracker;              /**< Timeline tracker for this job */
	struct mali_timeline_tracker *pp_tracker;          /**< Pointer to Timeline tracker for PP job that depends on this job. */
};

struct mali_gp_job *mali_gp_job_create(struct mali_session_data *session, _mali_uk_gp_start_job_s *uargs, u32 id, struct mali_timeline_tracker *pp_tracker);
void mali_gp_job_delete(struct mali_gp_job *job);

u32 mali_gp_job_get_gp_counter_src0(void);
void mali_gp_job_set_gp_counter_src0(u32 counter);
u32 mali_gp_job_get_gp_counter_src1(void);
void mali_gp_job_set_gp_counter_src1(u32 counter);

MALI_STATIC_INLINE u32 mali_gp_job_get_id(struct mali_gp_job *job)
{
	return (NULL == job) ? 0 : job->id;
}

MALI_STATIC_INLINE u32 mali_gp_job_get_cache_order(struct mali_gp_job *job)
{
	return (NULL == job) ? 0 : job->cache_order;
}

MALI_STATIC_INLINE u64 mali_gp_job_get_user_id(struct mali_gp_job *job)
{
	return job->uargs.user_job_ptr;
}

MALI_STATIC_INLINE u32 mali_gp_job_get_frame_builder_id(struct mali_gp_job *job)
{
	return job->uargs.frame_builder_id;
}

MALI_STATIC_INLINE u32 mali_gp_job_get_flush_id(struct mali_gp_job *job)
{
	return job->uargs.flush_id;
}

MALI_STATIC_INLINE u32 mali_gp_job_get_pid(struct mali_gp_job *job)
{
	return job->pid;
}

MALI_STATIC_INLINE u32 mali_gp_job_get_tid(struct mali_gp_job *job)
{
	return job->tid;
}

MALI_STATIC_INLINE u32 *mali_gp_job_get_frame_registers(struct mali_gp_job *job)
{
	return job->uargs.frame_registers;
}

MALI_STATIC_INLINE struct mali_session_data *mali_gp_job_get_session(struct mali_gp_job *job)
{
	return job->session;
}

MALI_STATIC_INLINE mali_bool mali_gp_job_has_vs_job(struct mali_gp_job *job)
{
	return (job->uargs.frame_registers[0] != job->uargs.frame_registers[1]) ? MALI_TRUE : MALI_FALSE;
}

MALI_STATIC_INLINE mali_bool mali_gp_job_has_plbu_job(struct mali_gp_job *job)
{
	return (job->uargs.frame_registers[2] != job->uargs.frame_registers[3]) ? MALI_TRUE : MALI_FALSE;
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
	return job->uargs.perf_counter_flag;
}

MALI_STATIC_INLINE u32 mali_gp_job_get_perf_counter_src0(struct mali_gp_job *job)
{
	return job->uargs.perf_counter_src0;
}

MALI_STATIC_INLINE u32 mali_gp_job_get_perf_counter_src1(struct mali_gp_job *job)
{
	return job->uargs.perf_counter_src1;
}

MALI_STATIC_INLINE u32 mali_gp_job_get_perf_counter_value0(struct mali_gp_job *job)
{
	return job->perf_counter_value0;
}

MALI_STATIC_INLINE u32 mali_gp_job_get_perf_counter_value1(struct mali_gp_job *job)
{
	return job->perf_counter_value1;
}

MALI_STATIC_INLINE void mali_gp_job_set_perf_counter_src0(struct mali_gp_job *job, u32 src)
{
	job->uargs.perf_counter_src0 = src;
}

MALI_STATIC_INLINE void mali_gp_job_set_perf_counter_src1(struct mali_gp_job *job, u32 src)
{
	job->uargs.perf_counter_src1 = src;
}

MALI_STATIC_INLINE void mali_gp_job_set_perf_counter_value0(struct mali_gp_job *job, u32 value)
{
	job->perf_counter_value0 = value;
}

MALI_STATIC_INLINE void mali_gp_job_set_perf_counter_value1(struct mali_gp_job *job, u32 value)
{
	job->perf_counter_value1 = value;
}

/**
 * Returns MALI_TRUE if first job is after the second job, ordered by job ID.
 *
 * @param first First job.
 * @param second Second job.
 * @return MALI_TRUE if first job should be ordered after the second job, MALI_FALSE if not.
 */
MALI_STATIC_INLINE mali_bool mali_gp_job_is_after(struct mali_gp_job *first, struct mali_gp_job *second)
{
	/* A span is used to handle job ID wrapping. */
	return (mali_gp_job_get_id(first) - mali_gp_job_get_id(second)) < MALI_SCHEDULER_JOB_ID_SPAN;
}

/**
 * Release reference on tracker for PP job that depends on this GP job.
 *
 * @note If GP job has a reference on tracker, this function MUST be called before the GP job is
 * deleted.
 *
 * @param job GP job that is done.
 * @param success MALI_TRUE if job completed successfully, MALI_FALSE if not.
 * @return A scheduling bitmask indicating whether scheduling needs to be done.
 */
mali_scheduler_mask mali_gp_job_signal_pp_tracker(struct mali_gp_job *job, mali_bool success);

#endif /* __MALI_GP_JOB_H__ */
