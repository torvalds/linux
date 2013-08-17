/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_pp_job.h"
#include "mali_osk.h"
#include "mali_osk_list.h"
#include "mali_kernel_common.h"
#include "mali_uk_types.h"

static u32 pp_counter_src0 = MALI_HW_CORE_NO_COUNTER;      /**< Performance counter 0, MALI_HW_CORE_NO_COUNTER for disabled */
static u32 pp_counter_src1 = MALI_HW_CORE_NO_COUNTER;      /**< Performance counter 1, MALI_HW_CORE_NO_COUNTER for disabled */

struct mali_pp_job *mali_pp_job_create(struct mali_session_data *session, _mali_uk_pp_start_job_s *uargs, u32 id)
{
	struct mali_pp_job *job;
	u32 perf_counter_flag;

	job = _mali_osk_malloc(sizeof(struct mali_pp_job));
	if (NULL != job)
	{
		u32 i;

		if (0 != _mali_osk_copy_from_user(&job->uargs, uargs, sizeof(_mali_uk_pp_start_job_s)))
		{
			_mali_osk_free(job);
			return NULL;
		}

		if (job->uargs.num_cores > _MALI_PP_MAX_SUB_JOBS)
		{
			MALI_PRINT_ERROR(("Mali PP job: Too many sub jobs specified in job object\n"));
			_mali_osk_free(job);
			return NULL;
		}

		if (!mali_pp_job_use_no_notification(job))
		{
			job->finished_notification = _mali_osk_notification_create(_MALI_NOTIFICATION_PP_FINISHED, sizeof(_mali_uk_pp_job_finished_s));
			if (NULL == job->finished_notification)
			{
				_mali_osk_free(job);
				return NULL;
			}
		}
		else
		{
			job->finished_notification = NULL;
		}

		perf_counter_flag = mali_pp_job_get_perf_counter_flag(job);

		/* case when no counters came from user space
		 * so pass the debugfs / DS-5 provided global ones to the job object */
		if (!((perf_counter_flag & _MALI_PERFORMANCE_COUNTER_FLAG_SRC0_ENABLE) ||
				(perf_counter_flag & _MALI_PERFORMANCE_COUNTER_FLAG_SRC1_ENABLE)))
		{
			mali_pp_job_set_perf_counter_src0(job, mali_pp_job_get_pp_counter_src0());
			mali_pp_job_set_perf_counter_src1(job, mali_pp_job_get_pp_counter_src1());
		}

		_mali_osk_list_init(&job->list);
		job->session = session;
		_mali_osk_list_init(&job->session_list);
		job->id = id;

		for (i = 0; i < job->uargs.num_cores; i++)
		{
			job->perf_counter_value0[i] = 0;
			job->perf_counter_value1[i] = 0;
		}
		job->sub_jobs_num = job->uargs.num_cores ? job->uargs.num_cores : 1;
		job->sub_jobs_started = 0;
		job->sub_jobs_completed = 0;
		job->sub_job_errors = 0;
		job->pid = _mali_osk_get_pid();
		job->tid = _mali_osk_get_tid();
#if defined(CONFIG_SYNC)
/* MALI_SEC */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
		job->sync_point = NULL;
		job->pre_fence = NULL;
		job->sync_work = NULL;
#endif
#endif

		return job;
	}

	return NULL;
}

void mali_pp_job_delete(struct mali_pp_job *job)
{
#ifdef CONFIG_SYNC
/* MALI_SEC */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
	if (NULL != job->pre_fence) sync_fence_put(job->pre_fence);
	if (NULL != job->sync_point) sync_fence_put(job->sync_point->fence);
#endif
#endif
	if (NULL != job->finished_notification)
	{
		_mali_osk_notification_delete(job->finished_notification);
	}
	_mali_osk_free(job);
}

u32 mali_pp_job_get_pp_counter_src0(void)
{
	return pp_counter_src0;
}

mali_bool mali_pp_job_set_pp_counter_src0(u32 counter)
{
	pp_counter_src0 = counter;

	return MALI_TRUE;
}

u32 mali_pp_job_get_pp_counter_src1(void)
{
	return pp_counter_src1;
}

mali_bool mali_pp_job_set_pp_counter_src1(u32 counter)
{
	pp_counter_src1 = counter;

	return MALI_TRUE;
}
