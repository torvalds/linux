/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
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
#ifdef CONFIG_SYNC
/* MALI_SEC */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
#include <linux/sync.h>
#endif
#endif
#include "mali_dlbu.h"

/**
 * The structure represents a PP job, including all sub-jobs
 * (This struct unfortunately needs to be public because of how the _mali_osk_list_*
 * mechanism works)
 */
struct mali_pp_job
{
	_mali_osk_list_t list;                             /**< Used to link jobs together in the scheduler queue */
	struct mali_session_data *session;                 /**< Session which submitted this job */
	_mali_osk_list_t session_list;                     /**< Used to link jobs together in the session job list */
	_mali_uk_pp_start_job_s uargs;                     /**< Arguments from user space */
	u32 id;                                            /**< Identifier for this job in kernel space (sequential numbering) */
	u32 perf_counter_value0[_MALI_PP_MAX_SUB_JOBS];    /**< Value of performance counter 0 (to be returned to user space), one for each sub job */
	u32 perf_counter_value1[_MALI_PP_MAX_SUB_JOBS];    /**< Value of performance counter 1 (to be returned to user space), one for each sub job */
	u32 sub_jobs_num;                                  /**< Number of subjobs; set to 1 for Mali-450 if DLBU is used, otherwise equals number of PP cores */
	u32 sub_jobs_started;                              /**< Total number of sub-jobs started (always started in ascending order) */
	u32 sub_jobs_completed;                            /**< Number of completed sub-jobs in this superjob */
	u32 sub_job_errors;                                /**< Bitfield with errors (errors for each single sub-job is or'ed together) */
	u32 pid;                                           /**< Process ID of submitting process */
	u32 tid;                                           /**< Thread ID of submitting thread */
	_mali_osk_notification_t *finished_notification;   /**< Notification sent back to userspace on job complete */
#ifdef CONFIG_SYNC
/* MALI_SEC */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
	mali_sync_pt *sync_point;                          /**< Sync point to signal on completion */
	struct sync_fence_waiter sync_waiter;              /**< Sync waiter for async wait */
	_mali_osk_wq_work_t *sync_work;                    /**< Work to schedule in callback */
	struct sync_fence *pre_fence;                      /**< Sync fence this job must wait for */
#endif
#endif
};

struct mali_pp_job *mali_pp_job_create(struct mali_session_data *session, _mali_uk_pp_start_job_s *uargs, u32 id);
void mali_pp_job_delete(struct mali_pp_job *job);

u32 mali_pp_job_get_pp_counter_src0(void);
mali_bool mali_pp_job_set_pp_counter_src0(u32 counter);
u32 mali_pp_job_get_pp_counter_src1(void);
mali_bool mali_pp_job_set_pp_counter_src1(u32 counter);

MALI_STATIC_INLINE u32 mali_pp_job_get_id(struct mali_pp_job *job)
{
	return (NULL == job) ? 0 : job->id;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_user_id(struct mali_pp_job *job)
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

MALI_STATIC_INLINE u32* mali_pp_job_get_frame_registers(struct mali_pp_job *job)
{
	return job->uargs.frame_registers;
}

MALI_STATIC_INLINE u32* mali_pp_job_get_dlbu_registers(struct mali_pp_job *job)
{
	return job->uargs.dlbu_registers;
}

MALI_STATIC_INLINE mali_bool mali_pp_job_is_virtual(struct mali_pp_job *job)
{
	return 0 == job->uargs.num_cores;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_addr_frame(struct mali_pp_job *job, u32 sub_job)
{
	if (mali_pp_job_is_virtual(job))
	{
		return MALI_DLBU_VIRT_ADDR;
	}
	else if (0 == sub_job)
	{
		return job->uargs.frame_registers[MALI200_REG_ADDR_FRAME / sizeof(u32)];
	}
	else if (sub_job < _MALI_PP_MAX_SUB_JOBS)
	{
		return job->uargs.frame_registers_addr_frame[sub_job - 1];
	}

	return 0;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_addr_stack(struct mali_pp_job *job, u32 sub_job)
{
	if (0 == sub_job)
	{
		return job->uargs.frame_registers[MALI200_REG_ADDR_STACK / sizeof(u32)];
	}
	else if (sub_job < _MALI_PP_MAX_SUB_JOBS)
	{
		return job->uargs.frame_registers_addr_stack[sub_job - 1];
	}

	return 0;
}

MALI_STATIC_INLINE u32* mali_pp_job_get_wb0_registers(struct mali_pp_job *job)
{
	return job->uargs.wb0_registers;
}

MALI_STATIC_INLINE u32* mali_pp_job_get_wb1_registers(struct mali_pp_job *job)
{
	return job->uargs.wb1_registers;
}

MALI_STATIC_INLINE u32* mali_pp_job_get_wb2_registers(struct mali_pp_job *job)
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

MALI_STATIC_INLINE void mali_pp_job_mark_sub_job_started(struct mali_pp_job *job, u32 sub_job)
{
	/* Assert that we are marking the "first unstarted sub job" as started */
	MALI_DEBUG_ASSERT(job->sub_jobs_started == sub_job);
	job->sub_jobs_started++;
}

MALI_STATIC_INLINE void mali_pp_job_mark_sub_job_not_stated(struct mali_pp_job *job, u32 sub_job)
{
	/* This is only safe on Mali-200. */
	MALI_DEBUG_ASSERT(_MALI_PRODUCT_ID_MALI200 == mali_kernel_core_get_product_id());

	job->sub_jobs_started--;
}

MALI_STATIC_INLINE void mali_pp_job_mark_sub_job_completed(struct mali_pp_job *job, mali_bool success)
{
	job->sub_jobs_completed++;
	if ( MALI_FALSE == success )
	{
		job->sub_job_errors++;
	}
}

MALI_STATIC_INLINE mali_bool mali_pp_job_was_success(struct mali_pp_job *job)
{
	if ( 0 == job->sub_job_errors )
	{
		return MALI_TRUE;
	}
	return MALI_FALSE;
}

MALI_STATIC_INLINE mali_bool mali_pp_job_has_active_barrier(struct mali_pp_job *job)
{
	return job->uargs.flags & _MALI_PP_JOB_FLAG_BARRIER ? MALI_TRUE : MALI_FALSE;
}

MALI_STATIC_INLINE void mali_pp_job_barrier_enforced(struct mali_pp_job *job)
{
	job->uargs.flags &= ~_MALI_PP_JOB_FLAG_BARRIER;
}

MALI_STATIC_INLINE mali_bool mali_pp_job_use_no_notification(struct mali_pp_job *job)
{
	return job->uargs.flags & _MALI_PP_JOB_FLAG_NO_NOTIFICATION ? MALI_TRUE : MALI_FALSE;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_perf_counter_flag(struct mali_pp_job *job)
{
	return job->uargs.perf_counter_flag;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_perf_counter_src0(struct mali_pp_job *job)
{
	return job->uargs.perf_counter_src0;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_perf_counter_src1(struct mali_pp_job *job)
{
	return job->uargs.perf_counter_src1;
}

MALI_STATIC_INLINE u32 mali_pp_job_get_perf_counter_value0(struct mali_pp_job *job, u32 sub_job)
{
	return job->perf_counter_value0[sub_job];
}

MALI_STATIC_INLINE u32 mali_pp_job_get_perf_counter_value1(struct mali_pp_job *job, u32 sub_job)
{
	return job->perf_counter_value1[sub_job];
}

MALI_STATIC_INLINE void mali_pp_job_set_perf_counter_src0(struct mali_pp_job *job, u32 src)
{
	job->uargs.perf_counter_src0 = src;
}

MALI_STATIC_INLINE void mali_pp_job_set_perf_counter_src1(struct mali_pp_job *job, u32 src)
{
	job->uargs.perf_counter_src1 = src;
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
	if (mali_pp_job_is_virtual(job) && job->sub_jobs_num != 1)
	{
		return _MALI_OSK_ERR_FAULT;
	}
	return _MALI_OSK_ERR_OK;
}

#endif /* __MALI_PP_JOB_H__ */
