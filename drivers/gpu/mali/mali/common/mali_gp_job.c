/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
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

struct mali_gp_job *mali_gp_job_create(struct mali_session_data *session, _mali_uk_gp_start_job_s *args, u32 id)
{
	struct mali_gp_job *job;

	job = _mali_osk_malloc(sizeof(struct mali_gp_job));
	if (NULL != job)
	{
		_mali_osk_list_init(&job->list);
		job->session = session;
		job->id = id;
		job->user_id = args->user_job_ptr;
		_mali_osk_memcpy(job->frame_registers, args->frame_registers, sizeof(job->frame_registers));
		job->heap_current_addr = args->frame_registers[4];
		job->perf_counter_flag = args->perf_counter_flag;
		job->perf_counter_src0 = args->perf_counter_src0;
		job->perf_counter_src1 = args->perf_counter_src1;
		job->perf_counter_value0 = 0;
		job->perf_counter_value1 = 0;

		job->pid = _mali_osk_get_pid();
		job->tid = _mali_osk_get_tid();
		job->frame_builder_id = args->frame_builder_id;
		job->flush_id = args->flush_id;

		return job;
	}

	return NULL;
}

void mali_gp_job_delete(struct mali_gp_job *job)
{
	_mali_osk_free(job);
}
