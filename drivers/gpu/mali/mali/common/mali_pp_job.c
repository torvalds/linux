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

struct mali_pp_job *mali_pp_job_create(struct mali_session_data *session, _mali_uk_pp_start_job_s *args, u32 id)
{
	struct mali_pp_job *job;

	if (args->num_cores > _MALI_PP_MAX_SUB_JOBS)
	{
		MALI_PRINT_ERROR(("Mali PP job: Too many sub jobs specified in job object\n"));
		return NULL;
	}

	job = _mali_osk_malloc(sizeof(struct mali_pp_job));
	if (NULL != job)
	{
		u32 i;
		_mali_osk_list_init(&job->list);
		job->session = session;
		job->id = id;
		job->user_id = args->user_job_ptr;
		_mali_osk_memcpy(job->frame_registers, args->frame_registers, sizeof(job->frame_registers));
		_mali_osk_memcpy(job->frame_registers_addr_frame, args->frame_registers_addr_frame, sizeof(job->frame_registers_addr_frame));
		_mali_osk_memcpy(job->frame_registers_addr_stack, args->frame_registers_addr_stack, sizeof(job->frame_registers_addr_stack));

		/* Only copy write back registers for the units that are enabled */
		job->wb0_registers[0] = 0;
		job->wb1_registers[0] = 0;
		job->wb2_registers[0] = 0;
		if (args->wb0_registers[0]) /* M200_WB0_REG_SOURCE_SELECT register */
		{
			_mali_osk_memcpy(job->wb0_registers, args->wb0_registers, sizeof(job->wb0_registers));
		}
		if (args->wb1_registers[0]) /* M200_WB1_REG_SOURCE_SELECT register */
		{
			_mali_osk_memcpy(job->wb1_registers, args->wb1_registers, sizeof(job->wb1_registers));
		}
		if (args->wb2_registers[0]) /* M200_WB2_REG_SOURCE_SELECT register */
		{
			_mali_osk_memcpy(job->wb2_registers, args->wb2_registers, sizeof(job->wb2_registers));
		}

		job->perf_counter_flag = args->perf_counter_flag;
		job->perf_counter_src0 = args->perf_counter_src0;
		job->perf_counter_src1 = args->perf_counter_src1;
		for (i = 0; i < args->num_cores; i++)
		{
			job->perf_counter_value0[i] = 0;
			job->perf_counter_value1[i] = 0;
		}
		job->sub_job_count = args->num_cores;
		job->sub_jobs_started = 0;
		job->sub_jobs_completed = 0;
		job->sub_job_errors = 0;

		job->pid = _mali_osk_get_pid();
		job->tid = _mali_osk_get_tid();
		job->frame_builder_id = args->frame_builder_id;
		job->flush_id = args->flush_id;

		return job;
	}

	return NULL;
}

void mali_pp_job_delete(struct mali_pp_job *job)
{
	_mali_osk_free(job);
}

_mali_osk_errcode_t mali_pp_job_check(struct mali_pp_job *job)
{
	if ((0 == job->frame_registers[0]) || (0 == job->frame_registers[1]))
	{
		return _MALI_OSK_ERR_FAULT;
	}
	return _MALI_OSK_ERR_OK;
}
