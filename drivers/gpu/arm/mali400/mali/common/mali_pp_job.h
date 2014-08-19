/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2011-2014 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
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
#include "mali_dma.h"
#include "mali_dlbu.h"
#include "mali_timeline.h"
#if defined(CONFIG_DMA_SHARED_BUFFER) && !defined(CONFIG_MALI_DMA_BUF_MAP_ON_ATTACH)
#include "linux/mali_memory_dma_buf.h"
#endif

/**
 * The structure represents a PP job, including all sub-jobs
 * (This struct unfortunately needs to be public because of how the _mali_osk_list_*
 * mechanism works)
 */
struct mali_pp_job {
	_mali_osk_list_t list;                             /**< Used to link jobs together in the scheduler queue */
	struct mali_session_data *session;                 /**< Session which submitted this job */
	_mali_osk_list_t session_list;                     /**< Used to link jobs together in the session job list */
	_mali_osk_list_t session_fb_lookup_list;           /**< Used to link jobs together from the same frame builder in the session */
	_mali_uk_pp_start_job_s uargs;                     /**< Arguments from user space */
	mali_dma_cmd_buf dma_cmd_buf;                      /**< Command buffer for starting job using Mali-450 DMA unit */
	u32 id;                                            /**< Identifier for this job in kernel space (sequential numbering) */
	u32 cache_order;                                   /**< Cache order used for L2 cache flushing (sequential numbering) */
	u32 perf_counter_value0[_MALI_PP_MAX_SUB_JOBS];    /**< Value of performance counter 0 (to be returned to user space), one for each sub job */
	u32 perf_counter_value1[_MALI_PP_MAX_SUB_JOBS];    /**< Value of performance counter 1 (to be returned to user space), one for each sub job */
	u32 sub_jobs_num;                                  /**< Number of subjobs; set to 1 for Mali-450 if DLBU is used, otherwise equals number of PP cores */
	u32 sub_jobs_started;                              /**< Total number of sub-jobs started (always started in ascending order) */
	u32 sub_jobs_completed;                            /**< Number of completed sub-jobs in this superjob */
	u32 sub_job_errors;                                /**< Bitfield with errors (errors for each single sub-job is or'ed together) */
	u32 pid;                                           /**< Process ID of submitting process */
	u32 tid;                                           /**< Thread ID of submitting thread */
	_mali_osk_notification_t *finished_notification;   /**< Notification sent back to userspace on job complete */
	u32 num_memory_cookies;                            /**< Number of memory cookies attached to job */
	u32 *memory_cookies;                               /**< Memory cookies attached to job */
#if defined(CONFIG_DMA_SHARED_BUFFER) && !defined(CONFIG_MALI_DMA_BUF_MAP_ON_ATTACH)
	struct mali_dma_buf_attachment **dma_bufs;         /**< Array of DMA-bufs used by job */
	u32 num_dma_bufs;                                  /**< Number of DMA-bufs used by job */
#endif
	struct mali_timeline_tracker tracker;              /**< Timeline tracker for this job */
	u32 perf_counter_per_sub_job_count;                /**< Number of values in the two arrays which is != MALI_HW_CORE_NO_COUNTER */
	u32 perf_counter_per_sub_job_src0[_MALI_PP_MAX_SUB_JOBS]; /**< Per sub job counters src0 */
	u32 perf_counter_per_sub_job_src1[_MALI_PP_MAX_SUB_JOBS]; /**< Per sub job counters src1 */
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
	return (NULL == job) ? 0 : job->id;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_cache_order(struct mali_pp_job *job)
{
	return (NULL == job) ? 0 : job->cache_order;
}

MALI_STATIC_INLINE u64 mali_pp_job_get_user_id(struct mali_pp_job *job)
{
	return job->uargs.user_job_ptr;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_frame_builder_id(struct mali_pp_job *job)
{
	return job->uargs.frame_builder_id;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_flush_id(struct mali_pp_job *job)
{
	return job->uargs.flush_id;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_pid(struct mali_pp_job *job)
{
	return job->pid;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_tid(struct mali_pp_job *job)
{
	return job->tid;
}

MALI_STATIC_INLINE u32 *mali_pp_job_get_frame_registers(struct mali_pp_job *job)
{
	return job->uargs.frame_registers;
}

MALI_STATIC_INLINE u32 *mali_pp_job_get_dlbu_registers(struct mali_pp_job *job)
{
	return job->uargs.dlbu_registers;
}

MALI_STATIC_INLINE mali_bool mali_pp_job_is_virtual_group_job(struct mali_pp_job *job)
{
	if (mali_is_mali450()) {
		return 1 != job->uargs.num_cores;
	}

	return MALI_FALSE;
}

MALI_STATIC_INLINE mali_bool mali_pp_job_is_with_dlbu(struct mali_pp_job *job)
{
#if defined(CONFIG_MALI450)
	return 0 == job->uargs.num_cores;
#else
	return MALI_FALSE;
#endif
}

MALI_STATIC_INLINE u32 mali_pp_job_get_addr_frame(struct mali_pp_job *job, u32 sub_job)
{
	if (mali_pp_job_is_with_dlbu(job)) {
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
	if (0 == sub_job) {
		return job->uargs.frame_registers[MALI200_REG_ADDR_STACK / sizeof(u32)];
	} else if (sub_job < _MALI_PP_MAX_SUB_JOBS) {
		return job->uargs.frame_registers_addr_stack[sub_job - 1];
	}

	return 0;
}

MALI_STATIC_INLINE u32 *mali_pp_job_get_wb0_registers(struct mali_pp_job *job)
{
	return job->uargs.wb0_registers;
}

MALI_STATIC_INLINE u32 *mali_pp_job_get_wb1_registers(struct mali_pp_job *job)
{
	return job->uargs.wb1_registers;
}

MALI_STATIC_INLINE u32 *mali_pp_job_get_wb2_registers(struct mali_pp_job *job)
{
	return job->uargs.wb2_registers;
}

MALI_STATIC_INLINE void mali_pp_job_disable_wb0(struct mali_pp_job *job)
{
	job->uargs.wb0_registers[MALI200_REG_ADDR_WB_SOURCE_SELECT] = 0;
}

MALI_STATIC_INLINE void mali_pp_job_disable_wb1(struct mali_pp_job *job)
{
	job->uargs.wb1_registers[MALI200_REG_ADDR_WB_SOURCE_SELECT] = 0;
}

MALI_STATIC_INLINE void mali_pp_job_disable_wb2(struct mali_pp_job *job)
{
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

MALI_STATIC_INLINE u32 mali_pp_job_get_fb_lookup_id(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);

	return MALI_PP_JOB_FB_LOOKUP_LIST_MASK & job->uargs.frame_builder_id;
}

MALI_STATIC_INLINE struct mali_session_data *mali_pp_job_get_session(struct mali_pp_job *job)
{
	return job->session;
}

MALI_STATIC_INLINE mali_bool mali_pp_job_has_unstarted_sub_jobs(struct mali_pp_job *job)
{
	return (job->sub_jobs_started < job->sub_jobs_num) ? MALI_TRUE : MALI_FALSE;
}

/* Function used when we are terminating a session with jobs. Return TRUE if it has a rendering job.
   Makes sure that no new subjobs are started. */
MALI_STATIC_INLINE void mali_pp_job_mark_unstarted_failed(struct mali_pp_job *job)
{
	u32 jobs_remaining = job->sub_jobs_num - job->sub_jobs_started;
	job->sub_jobs_started   += jobs_remaining;
	job->sub_jobs_completed += jobs_remaining;
	job->sub_job_errors     += jobs_remaining;
}

MALI_STATIC_INLINE void mali_pp_job_mark_unstarted_success(struct mali_pp_job *job)
{
	u32 jobs_remaining = job->sub_jobs_num - job->sub_jobs_started;
	job->sub_jobs_started   += jobs_remaining;
	job->sub_jobs_completed += jobs_remaining;
}

MALI_STATIC_INLINE mali_bool mali_pp_job_is_complete(struct mali_pp_job *job)
{
	return (job->sub_jobs_num == job->sub_jobs_completed) ? MALI_TRUE : MALI_FALSE;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_first_unstarted_sub_job(struct mali_pp_job *job)
{
	return job->sub_jobs_started;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_sub_job_count(struct mali_pp_job *job)
{
	return job->sub_jobs_num;
}

MALI_STATIC_INLINE mali_bool mali_pp_job_needs_dma_buf_mapping(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT(job);

	if (0 != job->num_memory_cookies) {
		return MALI_TRUE;
	}

	return MALI_FALSE;
}

MALI_STATIC_INLINE void mali_pp_job_mark_sub_job_started(struct mali_pp_job *job, u32 sub_job)
{
	MALI_DEBUG_ASSERT_POINTER(job);

	/* Assert that we are marking the "first unstarted sub job" as started */
	MALI_DEBUG_ASSERT(job->sub_jobs_started == sub_job);

	job->sub_jobs_started++;
}

MALI_STATIC_INLINE void mali_pp_job_mark_sub_job_completed(struct mali_pp_job *job, mali_bool success)
{
	job->sub_jobs_completed++;
	if (MALI_FALSE == success) {
		job->sub_job_errors++;
	}
}

MALI_STATIC_INLINE mali_bool mali_pp_job_was_success(struct mali_pp_job *job)
{
	if (0 == job->sub_job_errors) {
		return MALI_TRUE;
	}
	return MALI_FALSE;
}

MALI_STATIC_INLINE mali_bool mali_pp_job_use_no_notification(struct mali_pp_job *job)
{
	return job->uargs.flags & _MALI_PP_JOB_FLAG_NO_NOTIFICATION ? MALI_TRUE : MALI_FALSE;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_perf_counter_flag(struct mali_pp_job *job)
{
	return job->uargs.perf_counter_flag;
}


MALI_STATIC_INLINE u32 mali_pp_job_get_perf_counter_value0(struct mali_pp_job *job, u32 sub_job)
{
	return job->perf_counter_value0[sub_job];
}

MALI_STATIC_INLINE u32 mali_pp_job_get_perf_counter_value1(struct mali_pp_job *job, u32 sub_job)
{
	return job->perf_counter_value1[sub_job];
}

MALI_STATIC_INLINE void mali_pp_job_set_perf_counter_value0(struct mali_pp_job *job, u32 sub_job, u32 value)
{
	job->perf_counter_value0[sub_job] = value;
}

MALI_STATIC_INLINE void mali_pp_job_set_perf_counter_value1(struct mali_pp_job *job, u32 sub_job, u32 value)
{
	job->perf_counter_value1[sub_job] = value;
}

MALI_STATIC_INLINE _mali_osk_errcode_t mali_pp_job_check(struct mali_pp_job *job)
{
	if (mali_pp_job_is_with_dlbu(job) && job->sub_jobs_num != 1) {
		return _MALI_OSK_ERR_FAULT;
	}
	return _MALI_OSK_ERR_OK;
}

/**
 * Returns MALI_TRUE if first job should be started after second job.
 *
 * @param first First job.
 * @param second Second job.
 * @return MALI_TRUE if first job should be started after second job, MALI_FALSE if not.
 */
MALI_STATIC_INLINE mali_bool mali_pp_job_should_start_after(struct mali_pp_job *first, struct mali_pp_job *second)
{
	MALI_DEBUG_ASSERT_POINTER(first);
	MALI_DEBUG_ASSERT_POINTER(second);

	/* First job should be started after second job if second job is in progress. */
	if (0 < second->sub_jobs_started) {
		return MALI_TRUE;
	}

	/* First job should be started after second job if first job has a higher job id.  A span is
	   used to handle job id wrapping. */
	if ((mali_pp_job_get_id(first) - mali_pp_job_get_id(second)) < MALI_SCHEDULER_JOB_ID_SPAN) {
		return MALI_TRUE;
	}

	/* Second job should be started after first job. */
	return MALI_FALSE;
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
	MALI_DEBUG_ASSERT(!mali_pp_job_is_virtual_group_job(job));

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

#endif /* __MALI_PP_JOB_H__ */
