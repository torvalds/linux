/*
 * Copyright (C) 2011-2015 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_PP_JOB_H__
#define __MALI_PP_JOB_H__

#include "mali_osk.h"
#include "mali_osk_list.h"
#include "mali_uk_types.h"
#include "mali_session.h"
#include "mali_kernel_common.h"
#include "regs/mali_200_regs.h"
#include "mali_kernel_core.h"
#include "mali_dlbu.h"
#include "mali_timeline.h"
#include "mali_scheduler.h"
#include "mali_executor.h"
#if defined(CONFIG_DMA_SHARED_BUFFER) && !defined(CONFIG_MALI_DMA_BUF_MAP_ON_ATTACH)
#include "linux/mali_memory_dma_buf.h"
#endif

typedef enum pp_job_status {
	MALI_NO_SWAP_IN,
	MALI_SWAP_IN_FAIL,
	MALI_SWAP_IN_SUCC,
} pp_job_status;

/**
 * This structure represents a PP job, including all sub jobs.
 *
 * The PP job object itself is not protected by any single lock,
 * but relies on other locks instead (scheduler, executor and timeline lock).
 * Think of the job object as moving between these sub systems through-out
 * its lifetime. Different part of the PP job struct is used by different
 * subsystems. Accessor functions ensure that correct lock is taken.
 * Do NOT access any data members directly from outside this module!
 */
struct mali_pp_job {
	/*
	 * These members are typically only set at creation,
	 * and only read later on.
	 * They do not require any lock protection.
	 */
	_mali_uk_pp_start_job_s uargs;                     /**< Arguments from user space */
	struct mali_session_data *session;                 /**< Session which submitted this job */
	u32 pid;                                           /**< Process ID of submitting process */
	u32 tid;                                           /**< Thread ID of submitting thread */
	u32 id;                                            /**< Identifier for this job in kernel space (sequential numbering) */
	u32 cache_order;                                   /**< Cache order used for L2 cache flushing (sequential numbering) */
	struct mali_timeline_tracker tracker;              /**< Timeline tracker for this job */
	_mali_osk_notification_t *finished_notification;   /**< Notification sent back to userspace on job complete */
	u32 perf_counter_per_sub_job_count;                /**< Number of values in the two arrays which is != MALI_HW_CORE_NO_COUNTER */
	u32 perf_counter_per_sub_job_src0[_MALI_PP_MAX_SUB_JOBS]; /**< Per sub job counters src0 */
	u32 perf_counter_per_sub_job_src1[_MALI_PP_MAX_SUB_JOBS]; /**< Per sub job counters src1 */
	u32 sub_jobs_num;                                  /**< Number of subjobs; set to 1 for Mali-450 if DLBU is used, otherwise equals number of PP cores */

	pp_job_status swap_status;                         /**< Used to track each PP job swap status, if fail, we need to drop them in scheduler part */
	mali_bool user_notification;                       /**< When we deferred delete PP job, we need to judge if we need to send job finish notification to user space */
	u32 num_pp_cores_in_virtual;                       /**< How many PP cores we have when job finished */

	/*
	 * These members are used by both scheduler and executor.
	 * They are "protected" by atomic operations.
	 */
	_mali_osk_atomic_t sub_jobs_completed;                            /**< Number of completed sub-jobs in this superjob */
	_mali_osk_atomic_t sub_job_errors;                                /**< Bitfield with errors (errors for each single sub-job is or'ed together) */

	/*
	 * These members are used by scheduler, but only when no one else
	 * knows about this job object but the working function.
	 * No lock is thus needed for these.
	 */
	u32 *memory_cookies;                               /**< Memory cookies attached to job */

	/*
	 * These members are used by the scheduler,
	 * protected by scheduler lock
	 */
	_mali_osk_list_t list;                             /**< Used to link jobs together in the scheduler queue */
	_mali_osk_list_t session_fb_lookup_list;           /**< Used to link jobs together from the same frame builder in the session */
	u32 sub_jobs_started;                              /**< Total number of sub-jobs started (always started in ascending order) */

	/*
	 * Set by executor/group on job completion, read by scheduler when
	 * returning job to user. Hold executor lock when setting,
	 * no lock needed when reading
	 */
	u32 perf_counter_value0[_MALI_PP_MAX_SUB_JOBS];    /**< Value of performance counter 0 (to be returned to user space), one for each sub job */
	u32 perf_counter_value1[_MALI_PP_MAX_SUB_JOBS];    /**< Value of performance counter 1 (to be returned to user space), one for each sub job */
};

void mali_pp_job_initialize(void);
void mali_pp_job_terminate(void);

struct mali_pp_job *mali_pp_job_create(struct mali_session_data *session, _mali_uk_pp_start_job_s *uargs, u32 id);
void mali_pp_job_delete(struct mali_pp_job *job);

u32 mali_pp_job_get_perf_counter_src0(struct mali_pp_job *job, u32 sub_job);
u32 mali_pp_job_get_perf_counter_src1(struct mali_pp_job *job, u32 sub_job);

void mali_pp_job_set_pp_counter_global_src0(u32 counter);
void mali_pp_job_set_pp_counter_global_src1(u32 counter);
void mali_pp_job_set_pp_counter_sub_job_src0(u32 sub_job, u32 counter);
void mali_pp_job_set_pp_counter_sub_job_src1(u32 sub_job, u32 counter);

u32 mali_pp_job_get_pp_counter_global_src0(void);
u32 mali_pp_job_get_pp_counter_global_src1(void);
u32 mali_pp_job_get_pp_counter_sub_job_src0(u32 sub_job);
u32 mali_pp_job_get_pp_counter_sub_job_src1(u32 sub_job);

MALI_STATIC_INLINE u32 mali_pp_job_get_id(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return (NULL == job) ? 0 : job->id;
}

MALI_STATIC_INLINE void mali_pp_job_set_cache_order(struct mali_pp_job *job,
		u32 cache_order)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	MALI_DEBUG_ASSERT_SCHEDULER_LOCK_HELD();
	job->cache_order = cache_order;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_cache_order(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return (NULL == job) ? 0 : job->cache_order;
}

MALI_STATIC_INLINE u64 mali_pp_job_get_user_id(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return job->uargs.user_job_ptr;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_frame_builder_id(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return job->uargs.frame_builder_id;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_flush_id(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return job->uargs.flush_id;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_pid(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return job->pid;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_tid(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return job->tid;
}

MALI_STATIC_INLINE u32 *mali_pp_job_get_frame_registers(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return job->uargs.frame_registers;
}

MALI_STATIC_INLINE u32 *mali_pp_job_get_dlbu_registers(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return job->uargs.dlbu_registers;
}

MALI_STATIC_INLINE mali_bool mali_pp_job_is_virtual(struct mali_pp_job *job)
{
#if (defined(CONFIG_MALI450) || defined(CONFIG_MALI470))
	MALI_DEBUG_ASSERT_POINTER(job);
	return (0 == job->uargs.num_cores) ? MALI_TRUE : MALI_FALSE;
#else
	return MALI_FALSE;
#endif
}

MALI_STATIC_INLINE u32 mali_pp_job_get_addr_frame(struct mali_pp_job *job, u32 sub_job)
{
	MALI_DEBUG_ASSERT_POINTER(job);

	if (mali_pp_job_is_virtual(job)) {
		return MALI_DLBU_VIRT_ADDR;
	} else if (0 == sub_job) {
		return job->uargs.frame_registers[MALI200_REG_ADDR_FRAME / sizeof(u32)];
	} else if (sub_job < _MALI_PP_MAX_SUB_JOBS) {
		return job->uargs.frame_registers_addr_frame[sub_job - 1];
	}

	return 0;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_addr_stack(struct mali_pp_job *job, u32 sub_job)
{
	MALI_DEBUG_ASSERT_POINTER(job);

	if (0 == sub_job) {
		return job->uargs.frame_registers[MALI200_REG_ADDR_STACK / sizeof(u32)];
	} else if (sub_job < _MALI_PP_MAX_SUB_JOBS) {
		return job->uargs.frame_registers_addr_stack[sub_job - 1];
	}

	return 0;
}

void mali_pp_job_list_add(struct mali_pp_job *job, _mali_osk_list_t *list);

MALI_STATIC_INLINE void mali_pp_job_list_addtail(struct mali_pp_job *job,
		_mali_osk_list_t *list)
{
	_mali_osk_list_addtail(&job->list, list);
}

MALI_STATIC_INLINE void mali_pp_job_list_move(struct mali_pp_job *job,
		_mali_osk_list_t *list)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	MALI_DEBUG_ASSERT_SCHEDULER_LOCK_HELD();
	MALI_DEBUG_ASSERT(!_mali_osk_list_empty(&job->list));
	_mali_osk_list_move(&job->list, list);
}

MALI_STATIC_INLINE void mali_pp_job_list_remove(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	MALI_DEBUG_ASSERT_SCHEDULER_LOCK_HELD();
	_mali_osk_list_delinit(&job->list);
}

MALI_STATIC_INLINE u32 *mali_pp_job_get_wb0_registers(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return job->uargs.wb0_registers;
}

MALI_STATIC_INLINE u32 *mali_pp_job_get_wb1_registers(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return job->uargs.wb1_registers;
}

MALI_STATIC_INLINE u32 *mali_pp_job_get_wb2_registers(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return job->uargs.wb2_registers;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_wb0_source_addr(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return job->uargs.wb0_registers[MALI200_REG_ADDR_WB_SOURCE_ADDR / sizeof(u32)];
}

MALI_STATIC_INLINE u32 mali_pp_job_get_wb1_source_addr(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return job->uargs.wb1_registers[MALI200_REG_ADDR_WB_SOURCE_ADDR / sizeof(u32)];
}

MALI_STATIC_INLINE u32 mali_pp_job_get_wb2_source_addr(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return job->uargs.wb2_registers[MALI200_REG_ADDR_WB_SOURCE_ADDR / sizeof(u32)];
}

MALI_STATIC_INLINE void mali_pp_job_disable_wb0(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	job->uargs.wb0_registers[MALI200_REG_ADDR_WB_SOURCE_SELECT] = 0;
}

MALI_STATIC_INLINE void mali_pp_job_disable_wb1(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	job->uargs.wb1_registers[MALI200_REG_ADDR_WB_SOURCE_SELECT] = 0;
}

MALI_STATIC_INLINE void mali_pp_job_disable_wb2(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	job->uargs.wb2_registers[MALI200_REG_ADDR_WB_SOURCE_SELECT] = 0;
}

MALI_STATIC_INLINE mali_bool mali_pp_job_all_writeback_unit_disabled(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);

	if (job->uargs.wb0_registers[MALI200_REG_ADDR_WB_SOURCE_SELECT] ||
	    job->uargs.wb1_registers[MALI200_REG_ADDR_WB_SOURCE_SELECT] ||
	    job->uargs.wb2_registers[MALI200_REG_ADDR_WB_SOURCE_SELECT]
	   ) {
		/* At least one output unit active */
		return MALI_FALSE;
	}

	/* All outputs are disabled - we can abort the job */
	return MALI_TRUE;
}

MALI_STATIC_INLINE void mali_pp_job_fb_lookup_add(struct mali_pp_job *job)
{
	u32 fb_lookup_id;

	MALI_DEBUG_ASSERT_POINTER(job);
	MALI_DEBUG_ASSERT_SCHEDULER_LOCK_HELD();

	fb_lookup_id = MALI_PP_JOB_FB_LOOKUP_LIST_MASK & job->uargs.frame_builder_id;

	MALI_DEBUG_ASSERT(MALI_PP_JOB_FB_LOOKUP_LIST_SIZE > fb_lookup_id);

	_mali_osk_list_addtail(&job->session_fb_lookup_list,
			       &job->session->pp_job_fb_lookup_list[fb_lookup_id]);
}

MALI_STATIC_INLINE void mali_pp_job_fb_lookup_remove(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	MALI_DEBUG_ASSERT_SCHEDULER_LOCK_HELD();
	_mali_osk_list_delinit(&job->session_fb_lookup_list);
}

MALI_STATIC_INLINE struct mali_session_data *mali_pp_job_get_session(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return job->session;
}

MALI_STATIC_INLINE mali_bool mali_pp_job_has_started_sub_jobs(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	MALI_DEBUG_ASSERT_SCHEDULER_LOCK_HELD();
	return (0 < job->sub_jobs_started) ? MALI_TRUE : MALI_FALSE;
}

MALI_STATIC_INLINE mali_bool mali_pp_job_has_unstarted_sub_jobs(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	MALI_DEBUG_ASSERT_SCHEDULER_LOCK_HELD();
	return (job->sub_jobs_started < job->sub_jobs_num) ? MALI_TRUE : MALI_FALSE;
}

/* Function used when we are terminating a session with jobs. Return TRUE if it has a rendering job.
   Makes sure that no new subjobs are started. */
MALI_STATIC_INLINE void mali_pp_job_mark_unstarted_failed(struct mali_pp_job *job)
{
	u32 jobs_remaining;
	u32 i;

	MALI_DEBUG_ASSERT_POINTER(job);
	MALI_DEBUG_ASSERT_SCHEDULER_LOCK_HELD();

	jobs_remaining = job->sub_jobs_num - job->sub_jobs_started;
	job->sub_jobs_started += jobs_remaining;

	/* Not the most optimal way, but this is only used in error cases */
	for (i = 0; i < jobs_remaining; i++) {
		_mali_osk_atomic_inc(&job->sub_jobs_completed);
		_mali_osk_atomic_inc(&job->sub_job_errors);
	}
}

MALI_STATIC_INLINE mali_bool mali_pp_job_is_complete(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return (job->sub_jobs_num ==
		_mali_osk_atomic_read(&job->sub_jobs_completed)) ?
	       MALI_TRUE : MALI_FALSE;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_first_unstarted_sub_job(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	MALI_DEBUG_ASSERT_SCHEDULER_LOCK_HELD();
	return job->sub_jobs_started;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_sub_job_count(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return job->sub_jobs_num;
}

MALI_STATIC_INLINE u32 mali_pp_job_unstarted_sub_job_count(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	MALI_DEBUG_ASSERT_SCHEDULER_LOCK_HELD();
	MALI_DEBUG_ASSERT(job->sub_jobs_num >= job->sub_jobs_started);
	return (job->sub_jobs_num - job->sub_jobs_started);
}

MALI_STATIC_INLINE u32 mali_pp_job_num_memory_cookies(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return job->uargs.num_memory_cookies;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_memory_cookie(
	struct mali_pp_job *job, u32 index)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	MALI_DEBUG_ASSERT(index < job->uargs.num_memory_cookies);
	MALI_DEBUG_ASSERT_POINTER(job->memory_cookies);
	return job->memory_cookies[index];
}

MALI_STATIC_INLINE mali_bool mali_pp_job_needs_dma_buf_mapping(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);

	if (0 < job->uargs.num_memory_cookies) {
		return MALI_TRUE;
	}

	return MALI_FALSE;
}

MALI_STATIC_INLINE void mali_pp_job_mark_sub_job_started(struct mali_pp_job *job, u32 sub_job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	MALI_DEBUG_ASSERT_SCHEDULER_LOCK_HELD();

	/* Assert that we are marking the "first unstarted sub job" as started */
	MALI_DEBUG_ASSERT(job->sub_jobs_started == sub_job);

	job->sub_jobs_started++;
}

MALI_STATIC_INLINE void mali_pp_job_mark_sub_job_completed(struct mali_pp_job *job, mali_bool success)
{
	MALI_DEBUG_ASSERT_POINTER(job);

	_mali_osk_atomic_inc(&job->sub_jobs_completed);
	if (MALI_FALSE == success) {
		_mali_osk_atomic_inc(&job->sub_job_errors);
	}
}

MALI_STATIC_INLINE mali_bool mali_pp_job_was_success(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	if (0 == _mali_osk_atomic_read(&job->sub_job_errors)) {
		return MALI_TRUE;
	}
	return MALI_FALSE;
}

MALI_STATIC_INLINE mali_bool mali_pp_job_use_no_notification(
	struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return (job->uargs.flags & _MALI_PP_JOB_FLAG_NO_NOTIFICATION) ?
	       MALI_TRUE : MALI_FALSE;
}

MALI_STATIC_INLINE mali_bool mali_pp_job_is_pilot_job(struct mali_pp_job *job)
{
	/*
	 * A pilot job is currently identified as jobs which
	 * require no callback notification.
	 */
	return mali_pp_job_use_no_notification(job);
}

MALI_STATIC_INLINE _mali_osk_notification_t *
mali_pp_job_get_finished_notification(struct mali_pp_job *job)
{
	_mali_osk_notification_t *notification;

	MALI_DEBUG_ASSERT_POINTER(job);
	MALI_DEBUG_ASSERT_POINTER(job->finished_notification);

	notification = job->finished_notification;
	job->finished_notification = NULL;

	return notification;
}

MALI_STATIC_INLINE mali_bool mali_pp_job_is_window_surface(
	struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return (job->uargs.flags & _MALI_PP_JOB_FLAG_IS_WINDOW_SURFACE)
	       ? MALI_TRUE : MALI_FALSE;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_perf_counter_flag(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return job->uargs.perf_counter_flag;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_perf_counter_value0(struct mali_pp_job *job, u32 sub_job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return job->perf_counter_value0[sub_job];
}

MALI_STATIC_INLINE u32 mali_pp_job_get_perf_counter_value1(struct mali_pp_job *job, u32 sub_job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return job->perf_counter_value1[sub_job];
}

MALI_STATIC_INLINE void mali_pp_job_set_perf_counter_value0(struct mali_pp_job *job, u32 sub_job, u32 value)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	job->perf_counter_value0[sub_job] = value;
}

MALI_STATIC_INLINE void mali_pp_job_set_perf_counter_value1(struct mali_pp_job *job, u32 sub_job, u32 value)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	MALI_DEBUG_ASSERT_EXECUTOR_LOCK_HELD();
	job->perf_counter_value1[sub_job] = value;
}

MALI_STATIC_INLINE _mali_osk_errcode_t mali_pp_job_check(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	if (mali_pp_job_is_virtual(job) && job->sub_jobs_num != 1) {
		return _MALI_OSK_ERR_FAULT;
	}
	return _MALI_OSK_ERR_OK;
}

/**
 * Returns MALI_TRUE if this job has more than two sub jobs and all sub jobs are unstarted.
 *
 * @param job Job to check.
 * @return MALI_TRUE if job has more than two sub jobs and all sub jobs are unstarted, MALI_FALSE if not.
 */
MALI_STATIC_INLINE mali_bool mali_pp_job_is_large_and_unstarted(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	MALI_DEBUG_ASSERT_SCHEDULER_LOCK_HELD();
	MALI_DEBUG_ASSERT(!mali_pp_job_is_virtual(job));

	return (0 == job->sub_jobs_started && 2 < job->sub_jobs_num);
}

/**
 * Get PP job's Timeline tracker.
 *
 * @param job PP job.
 * @return Pointer to Timeline tracker for the job.
 */
MALI_STATIC_INLINE struct mali_timeline_tracker *mali_pp_job_get_tracker(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return &(job->tracker);
}

MALI_STATIC_INLINE u32 *mali_pp_job_get_timeline_point_ptr(
	struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	return (u32 __user *)(uintptr_t)job->uargs.timeline_point_ptr;
}


#endif /* __MALI_PP_JOB_H__ */
