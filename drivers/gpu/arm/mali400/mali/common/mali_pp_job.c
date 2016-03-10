/*
 * Copyright (C) 2011-2016 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_pp.h"
#include "mali_pp_job.h"
#include "mali_osk.h"
#include "mali_osk_list.h"
#include "mali_kernel_common.h"
#include "mali_uk_types.h"
#include "mali_executor.h"
#if defined(CONFIG_DMA_SHARED_BUFFER) && !defined(CONFIG_MALI_DMA_BUF_MAP_ON_ATTACH)
#include "linux/mali_memory_dma_buf.h"
#endif
#include "mali_memory_swap_alloc.h"
#include "mali_scheduler.h"

static u32 pp_counter_src0 = MALI_HW_CORE_NO_COUNTER;   /**< Performance counter 0, MALI_HW_CORE_NO_COUNTER for disabled */
static u32 pp_counter_src1 = MALI_HW_CORE_NO_COUNTER;   /**< Performance counter 1, MALI_HW_CORE_NO_COUNTER for disabled */
static _mali_osk_atomic_t pp_counter_per_sub_job_count; /**< Number of values in the two arrays which is != MALI_HW_CORE_NO_COUNTER */
static u32 pp_counter_per_sub_job_src0[_MALI_PP_MAX_SUB_JOBS] = { MALI_HW_CORE_NO_COUNTER, MALI_HW_CORE_NO_COUNTER, MALI_HW_CORE_NO_COUNTER, MALI_HW_CORE_NO_COUNTER, MALI_HW_CORE_NO_COUNTER, MALI_HW_CORE_NO_COUNTER, MALI_HW_CORE_NO_COUNTER, MALI_HW_CORE_NO_COUNTER };
static u32 pp_counter_per_sub_job_src1[_MALI_PP_MAX_SUB_JOBS] = { MALI_HW_CORE_NO_COUNTER, MALI_HW_CORE_NO_COUNTER, MALI_HW_CORE_NO_COUNTER, MALI_HW_CORE_NO_COUNTER, MALI_HW_CORE_NO_COUNTER, MALI_HW_CORE_NO_COUNTER, MALI_HW_CORE_NO_COUNTER, MALI_HW_CORE_NO_COUNTER };

void mali_pp_job_initialize(void)
{
	_mali_osk_atomic_init(&pp_counter_per_sub_job_count, 0);
}

void mali_pp_job_terminate(void)
{
	_mali_osk_atomic_term(&pp_counter_per_sub_job_count);
}

struct mali_pp_job *mali_pp_job_create(struct mali_session_data *session,
				       _mali_uk_pp_start_job_s __user *uargs, u32 id)
{
	struct mali_pp_job *job;
	u32 perf_counter_flag;

	job = _mali_osk_calloc(1, sizeof(struct mali_pp_job));
	if (NULL != job) {

		_mali_osk_list_init(&job->list);
		_mali_osk_list_init(&job->session_fb_lookup_list);

		if (0 != _mali_osk_copy_from_user(&job->uargs, uargs, sizeof(_mali_uk_pp_start_job_s))) {
			goto fail;
		}

		if (job->uargs.num_cores > _MALI_PP_MAX_SUB_JOBS) {
			MALI_PRINT_ERROR(("Mali PP job: Too many sub jobs specified in job object\n"));
			goto fail;
		}

		if (!mali_pp_job_use_no_notification(job)) {
			job->finished_notification = _mali_osk_notification_create(_MALI_NOTIFICATION_PP_FINISHED, sizeof(_mali_uk_pp_job_finished_s));
			if (NULL == job->finished_notification) goto fail;
		}

		perf_counter_flag = mali_pp_job_get_perf_counter_flag(job);

		/* case when no counters came from user space
		 * so pass the debugfs / DS-5 provided global ones to the job object */
		if (!((perf_counter_flag & _MALI_PERFORMANCE_COUNTER_FLAG_SRC0_ENABLE) ||
		      (perf_counter_flag & _MALI_PERFORMANCE_COUNTER_FLAG_SRC1_ENABLE))) {
			u32 sub_job_count = _mali_osk_atomic_read(&pp_counter_per_sub_job_count);

			/* These counters apply for all virtual jobs, and where no per sub job counter is specified */
			job->uargs.perf_counter_src0 = pp_counter_src0;
			job->uargs.perf_counter_src1 = pp_counter_src1;

			/* We only copy the per sub job array if it is enabled with at least one counter */
			if (0 < sub_job_count) {
				job->perf_counter_per_sub_job_count = sub_job_count;
				_mali_osk_memcpy(job->perf_counter_per_sub_job_src0, pp_counter_per_sub_job_src0, sizeof(pp_counter_per_sub_job_src0));
				_mali_osk_memcpy(job->perf_counter_per_sub_job_src1, pp_counter_per_sub_job_src1, sizeof(pp_counter_per_sub_job_src1));
			}
		}

		job->session = session;
		job->id = id;

		job->sub_jobs_num = job->uargs.num_cores ? job->uargs.num_cores : 1;
		job->pid = _mali_osk_get_pid();
		job->tid = _mali_osk_get_tid();

		_mali_osk_atomic_init(&job->sub_jobs_completed, 0);
		_mali_osk_atomic_init(&job->sub_job_errors, 0);
		job->swap_status = MALI_NO_SWAP_IN;
		job->user_notification = MALI_FALSE;
		job->num_pp_cores_in_virtual = 0;

		if (job->uargs.num_memory_cookies > session->allocation_mgr.mali_allocation_num) {
			MALI_PRINT_ERROR(("Mali PP job: The number of memory cookies is invalid !\n"));
			goto fail;
		}

		if (job->uargs.num_memory_cookies > 0) {
			u32 size;
			u32 __user *memory_cookies = (u32 __user *)(uintptr_t)job->uargs.memory_cookies;

			size = sizeof(*memory_cookies) * (job->uargs.num_memory_cookies);

			job->memory_cookies = _mali_osk_malloc(size);
			if (NULL == job->memory_cookies) {
				MALI_PRINT_ERROR(("Mali PP job: Failed to allocate %d bytes of memory cookies!\n", size));
				goto fail;
			}

			if (0 != _mali_osk_copy_from_user(job->memory_cookies, memory_cookies, size)) {
				MALI_PRINT_ERROR(("Mali PP job: Failed to copy %d bytes of memory cookies from user!\n", size));
				goto fail;
			}
		}

		if (_MALI_OSK_ERR_OK != mali_pp_job_check(job)) {
			/* Not a valid job. */
			goto fail;
		}

		mali_timeline_tracker_init(&job->tracker, MALI_TIMELINE_TRACKER_PP, NULL, job);
		mali_timeline_fence_copy_uk_fence(&(job->tracker.fence), &(job->uargs.fence));

		mali_mem_swap_in_pages(job);

		return job;
	}

fail:
	if (NULL != job) {
		mali_pp_job_delete(job);
	}

	return NULL;
}

void mali_pp_job_delete(struct mali_pp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	MALI_DEBUG_ASSERT(_mali_osk_list_empty(&job->list));
	MALI_DEBUG_ASSERT(_mali_osk_list_empty(&job->session_fb_lookup_list));

	if (NULL != job->memory_cookies) {
#if defined(CONFIG_DMA_SHARED_BUFFER) && !defined(CONFIG_MALI_DMA_BUF_MAP_ON_ATTACH)
		/* Unmap buffers attached to job */
		mali_dma_buf_unmap_job(job);
#endif
		if (MALI_NO_SWAP_IN != job->swap_status) {
			mali_mem_swap_out_pages(job);
		}

		_mali_osk_free(job->memory_cookies);
	}

	if (job->user_notification) {
		mali_scheduler_return_pp_job_to_user(job,
						     job->num_pp_cores_in_virtual);
	}

	if (NULL != job->finished_notification) {
		_mali_osk_notification_delete(job->finished_notification);
	}

	_mali_osk_atomic_term(&job->sub_jobs_completed);
	_mali_osk_atomic_term(&job->sub_job_errors);

	_mali_osk_free(job);
}

void mali_pp_job_list_add(struct mali_pp_job *job, _mali_osk_list_t *list)
{
	struct mali_pp_job *iter;
	struct mali_pp_job *tmp;

	MALI_DEBUG_ASSERT_POINTER(job);
	MALI_DEBUG_ASSERT_SCHEDULER_LOCK_HELD();

	/* Find position in list/queue where job should be added. */
	_MALI_OSK_LIST_FOREACHENTRY_REVERSE(iter, tmp, list,
					    struct mali_pp_job, list) {
		/* job should be started after iter if iter is in progress. */
		if (0 < iter->sub_jobs_started) {
			break;
		}

		/*
		 * job should be started after iter if it has a higher
		 * job id. A span is used to handle job id wrapping.
		 */
		if ((mali_pp_job_get_id(job) -
		     mali_pp_job_get_id(iter)) <
		    MALI_SCHEDULER_JOB_ID_SPAN) {
			break;
		}
	}

	_mali_osk_list_add(&job->list, &iter->list);
}


u32 mali_pp_job_get_perf_counter_src0(struct mali_pp_job *job, u32 sub_job)
{
	/* Virtual jobs always use the global job counter (or if there are per sub job counters at all) */
	if (mali_pp_job_is_virtual(job) || 0 == job->perf_counter_per_sub_job_count) {
		return job->uargs.perf_counter_src0;
	}

	/* Use per sub job counter if enabled... */
	if (MALI_HW_CORE_NO_COUNTER != job->perf_counter_per_sub_job_src0[sub_job]) {
		return job->perf_counter_per_sub_job_src0[sub_job];
	}

	/* ...else default to global job counter */
	return job->uargs.perf_counter_src0;
}

u32 mali_pp_job_get_perf_counter_src1(struct mali_pp_job *job, u32 sub_job)
{
	/* Virtual jobs always use the global job counter (or if there are per sub job counters at all) */
	if (mali_pp_job_is_virtual(job) || 0 == job->perf_counter_per_sub_job_count) {
		/* Virtual jobs always use the global job counter */
		return job->uargs.perf_counter_src1;
	}

	/* Use per sub job counter if enabled... */
	if (MALI_HW_CORE_NO_COUNTER != job->perf_counter_per_sub_job_src1[sub_job]) {
		return job->perf_counter_per_sub_job_src1[sub_job];
	}

	/* ...else default to global job counter */
	return job->uargs.perf_counter_src1;
}

void mali_pp_job_set_pp_counter_global_src0(u32 counter)
{
	pp_counter_src0 = counter;
}

void mali_pp_job_set_pp_counter_global_src1(u32 counter)
{
	pp_counter_src1 = counter;
}

void mali_pp_job_set_pp_counter_sub_job_src0(u32 sub_job, u32 counter)
{
	MALI_DEBUG_ASSERT(sub_job < _MALI_PP_MAX_SUB_JOBS);

	if (MALI_HW_CORE_NO_COUNTER == pp_counter_per_sub_job_src0[sub_job]) {
		/* increment count since existing counter was disabled */
		_mali_osk_atomic_inc(&pp_counter_per_sub_job_count);
	}

	if (MALI_HW_CORE_NO_COUNTER == counter) {
		/* decrement count since new counter is disabled */
		_mali_osk_atomic_dec(&pp_counter_per_sub_job_count);
	}

	/* PS: A change from MALI_HW_CORE_NO_COUNTER to MALI_HW_CORE_NO_COUNTER will inc and dec, result will be 0 change */

	pp_counter_per_sub_job_src0[sub_job] = counter;
}

void mali_pp_job_set_pp_counter_sub_job_src1(u32 sub_job, u32 counter)
{
	MALI_DEBUG_ASSERT(sub_job < _MALI_PP_MAX_SUB_JOBS);

	if (MALI_HW_CORE_NO_COUNTER == pp_counter_per_sub_job_src1[sub_job]) {
		/* increment count since existing counter was disabled */
		_mali_osk_atomic_inc(&pp_counter_per_sub_job_count);
	}

	if (MALI_HW_CORE_NO_COUNTER == counter) {
		/* decrement count since new counter is disabled */
		_mali_osk_atomic_dec(&pp_counter_per_sub_job_count);
	}

	/* PS: A change from MALI_HW_CORE_NO_COUNTER to MALI_HW_CORE_NO_COUNTER will inc and dec, result will be 0 change */

	pp_counter_per_sub_job_src1[sub_job] = counter;
}

u32 mali_pp_job_get_pp_counter_global_src0(void)
{
	return pp_counter_src0;
}

u32 mali_pp_job_get_pp_counter_global_src1(void)
{
	return pp_counter_src1;
}

u32 mali_pp_job_get_pp_counter_sub_job_src0(u32 sub_job)
{
	MALI_DEBUG_ASSERT(sub_job < _MALI_PP_MAX_SUB_JOBS);
	return pp_counter_per_sub_job_src0[sub_job];
}

u32 mali_pp_job_get_pp_counter_sub_job_src1(u32 sub_job)
{
	MALI_DEBUG_ASSERT(sub_job < _MALI_PP_MAX_SUB_JOBS);
	return pp_counter_per_sub_job_src1[sub_job];
}
