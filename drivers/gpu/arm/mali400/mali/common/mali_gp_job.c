/*
 * Copyright (C) 2011-2014 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_gp_job.h"
#include "mali_osk.h"
#include "mali_osk_list.h"
#include "mali_uk_types.h"

static u32 gp_counter_src0 = MALI_HW_CORE_NO_COUNTER;      /**< Performance counter 0, MALI_HW_CORE_NO_COUNTER for disabled */
static u32 gp_counter_src1 = MALI_HW_CORE_NO_COUNTER;           /**< Performance counter 1, MALI_HW_CORE_NO_COUNTER for disabled */

struct mali_gp_job *mali_gp_job_create(struct mali_session_data *session, _mali_uk_gp_start_job_s *uargs, u32 id, struct mali_timeline_tracker *pp_tracker)
{
	struct mali_gp_job *job;
	u32 perf_counter_flag;

	job = _mali_osk_malloc(sizeof(struct mali_gp_job));
	if (NULL != job) {
		job->finished_notification = _mali_osk_notification_create(_MALI_NOTIFICATION_GP_FINISHED, sizeof(_mali_uk_gp_job_finished_s));
		if (NULL == job->finished_notification) {
			_mali_osk_free(job);
			return NULL;
		}

		job->oom_notification = _mali_osk_notification_create(_MALI_NOTIFICATION_GP_STALLED, sizeof(_mali_uk_gp_job_suspended_s));
		if (NULL == job->oom_notification) {
			_mali_osk_notification_delete(job->finished_notification);
			_mali_osk_free(job);
			return NULL;
		}

		if (0 != _mali_osk_copy_from_user(&job->uargs, uargs, sizeof(_mali_uk_gp_start_job_s))) {
			_mali_osk_notification_delete(job->finished_notification);
			_mali_osk_notification_delete(job->oom_notification);
			_mali_osk_free(job);
			return NULL;
		}

		perf_counter_flag = mali_gp_job_get_perf_counter_flag(job);

		/* case when no counters came from user space
		 * so pass the debugfs / DS-5 provided global ones to the job object */
		if (!((perf_counter_flag & _MALI_PERFORMANCE_COUNTER_FLAG_SRC0_ENABLE) ||
		      (perf_counter_flag & _MALI_PERFORMANCE_COUNTER_FLAG_SRC1_ENABLE))) {
			mali_gp_job_set_perf_counter_src0(job, mali_gp_job_get_gp_counter_src0());
			mali_gp_job_set_perf_counter_src1(job, mali_gp_job_get_gp_counter_src1());
		}

		_mali_osk_list_init(&job->list);
		job->session = session;
		job->id = id;
		job->heap_current_addr = job->uargs.frame_registers[4];
		job->perf_counter_value0 = 0;
		job->perf_counter_value1 = 0;
		job->pid = _mali_osk_get_pid();
		job->tid = _mali_osk_get_tid();

		job->pp_tracker = pp_tracker;
		if (NULL != job->pp_tracker) {
			/* Take a reference on PP job's tracker that will be released when the GP
			   job is done. */
			mali_timeline_system_tracker_get(session->timeline_system, pp_tracker);
		}

		mali_timeline_tracker_init(&job->tracker, MALI_TIMELINE_TRACKER_GP, NULL, job);
		mali_timeline_fence_copy_uk_fence(&(job->tracker.fence), &(job->uargs.fence));

		return job;
	}

	return NULL;
}

void mali_gp_job_delete(struct mali_gp_job *job)
{
	MALI_DEBUG_ASSERT_POINTER(job);
	MALI_DEBUG_ASSERT(NULL == job->pp_tracker);
	MALI_DEBUG_ASSERT(_mali_osk_list_empty(&job->list));

	/* de-allocate the pre-allocated oom notifications */
	if (NULL != job->oom_notification) {
		_mali_osk_notification_delete(job->oom_notification);
		job->oom_notification = NULL;
	}
	if (NULL != job->finished_notification) {
		_mali_osk_notification_delete(job->finished_notification);
		job->finished_notification = NULL;
	}

	_mali_osk_free(job);
}

void mali_gp_job_list_add(struct mali_gp_job *job, _mali_osk_list_t *list)
{
	struct mali_gp_job *iter;
	struct mali_gp_job *tmp;

	MALI_DEBUG_ASSERT_POINTER(job);
	MALI_DEBUG_ASSERT_SCHEDULER_LOCK_HELD();

	/* Find position in list/queue where job should be added. */
	_MALI_OSK_LIST_FOREACHENTRY_REVERSE(iter, tmp, list,
					    struct mali_gp_job, list) {

		/* A span is used to handle job ID wrapping. */
		bool job_is_after = (mali_gp_job_get_id(job) -
				     mali_gp_job_get_id(iter)) <
				    MALI_SCHEDULER_JOB_ID_SPAN;

		if (job_is_after) {
			break;
		}
	}

	_mali_osk_list_add(&job->list, &iter->list);
}

u32 mali_gp_job_get_gp_counter_src0(void)
{
	return gp_counter_src0;
}

void mali_gp_job_set_gp_counter_src0(u32 counter)
{
	gp_counter_src0 = counter;
}

u32 mali_gp_job_get_gp_counter_src1(void)
{
	return gp_counter_src1;
}

void mali_gp_job_set_gp_counter_src1(u32 counter)
{
	gp_counter_src1 = counter;
}

mali_scheduler_mask mali_gp_job_signal_pp_tracker(struct mali_gp_job *job, mali_bool success)
{
	mali_scheduler_mask schedule_mask = MALI_SCHEDULER_MASK_EMPTY;

	MALI_DEBUG_ASSERT_POINTER(job);

	if (NULL != job->pp_tracker) {
		schedule_mask |= mali_timeline_system_tracker_put(job->session->timeline_system, job->pp_tracker, MALI_FALSE == success);
		job->pp_tracker = NULL;
	}

	return schedule_mask;
}
