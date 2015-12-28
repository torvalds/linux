/*
 * Copyright (C) 2011-2015 ARM Limited. All rights reserved.
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
#include "mali_memory_virtual.h"
#include "mali_memory_defer_bind.h"

static u32 gp_counter_src0 = MALI_HW_CORE_NO_COUNTER;      /**< Performance counter 0, MALI_HW_CORE_NO_COUNTER for disabled */
static u32 gp_counter_src1 = MALI_HW_CORE_NO_COUNTER;           /**< Performance counter 1, MALI_HW_CORE_NO_COUNTER for disabled */
static void _mali_gp_del_varying_allocations(struct mali_gp_job *job);


static int _mali_gp_add_varying_allocations(struct mali_session_data *session,
		struct mali_gp_job *job,
		u32 *alloc,
		u32 num)
{
	int i = 0;
	struct mali_gp_allocation_node *alloc_node;
	mali_mem_allocation *mali_alloc = NULL;
	struct mali_vma_node *mali_vma_node = NULL;

	for (i = 0 ; i < num ; i++) {
		MALI_DEBUG_ASSERT(alloc[i]);
		alloc_node = _mali_osk_calloc(1, sizeof(struct mali_gp_allocation_node));
		if (alloc_node) {
			INIT_LIST_HEAD(&alloc_node->node);
			/* find mali allocation structure by vaddress*/
			mali_vma_node = mali_vma_offset_search(&session->allocation_mgr, alloc[i], 0);

			if (likely(mali_vma_node)) {
				mali_alloc = container_of(mali_vma_node, struct mali_mem_allocation, mali_vma_node);
				MALI_DEBUG_ASSERT(alloc[i] == mali_vma_node->vm_node.start);
			} else {
				MALI_DEBUG_PRINT(1, ("ERROE!_mali_gp_add_varying_allocations,can't find allocation %d by address =0x%x, num=%d\n", i, alloc[i], num));
				MALI_DEBUG_ASSERT(0);
			}
			alloc_node->alloc = mali_alloc;
			/* add to gp job varying alloc list*/
			list_move(&alloc_node->node, &job->varying_alloc);
		} else
			goto fail;
	}

	return 0;
fail:
	MALI_DEBUG_PRINT(1, ("ERROE!_mali_gp_add_varying_allocations,failed to alloc memory!\n"));
	_mali_gp_del_varying_allocations(job);
	return -1;
}


static void _mali_gp_del_varying_allocations(struct mali_gp_job *job)
{
	struct mali_gp_allocation_node *alloc_node, *tmp_node;

	list_for_each_entry_safe(alloc_node, tmp_node, &job->varying_alloc, node) {
		list_del(&alloc_node->node);
		kfree(alloc_node);
	}
	INIT_LIST_HEAD(&job->varying_alloc);
}

struct mali_gp_job *mali_gp_job_create(struct mali_session_data *session, _mali_uk_gp_start_job_s *uargs, u32 id, struct mali_timeline_tracker *pp_tracker)
{
	struct mali_gp_job *job;
	u32 perf_counter_flag;
	u32 __user *memory_list = NULL;
	struct mali_gp_allocation_node *alloc_node, *tmp_node;

	job = _mali_osk_calloc(1, sizeof(struct mali_gp_job));
	if (NULL != job) {
		job->finished_notification = _mali_osk_notification_create(_MALI_NOTIFICATION_GP_FINISHED, sizeof(_mali_uk_gp_job_finished_s));
		if (NULL == job->finished_notification) {
			goto fail3;
		}

		job->oom_notification = _mali_osk_notification_create(_MALI_NOTIFICATION_GP_STALLED, sizeof(_mali_uk_gp_job_suspended_s));
		if (NULL == job->oom_notification) {
			goto fail2;
		}

		if (0 != _mali_osk_copy_from_user(&job->uargs, uargs, sizeof(_mali_uk_gp_start_job_s))) {
			goto fail1;
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
		job->heap_base_addr = job->uargs.frame_registers[4];
		job->heap_current_addr = job->uargs.frame_registers[4];
		job->heap_grow_size = job->uargs.heap_grow_size;
		job->perf_counter_value0 = 0;
		job->perf_counter_value1 = 0;
		job->pid = _mali_osk_get_pid();
		job->tid = _mali_osk_get_tid();


		INIT_LIST_HEAD(&job->varying_alloc);
		INIT_LIST_HEAD(&job->vary_todo);
		job->dmem = NULL;
		/* add varying allocation list*/
		if (uargs->varying_alloc_num) {
			/* copy varying list from user space*/
			job->varying_list = _mali_osk_calloc(1, sizeof(u32) * uargs->varying_alloc_num);
			if (!job->varying_list) {
				MALI_PRINT_ERROR(("Mali GP job: allocate varying_list failed varying_alloc_num = %d !\n", uargs->varying_alloc_num));
				goto fail1;
			}

			memory_list = (u32 __user *)(uintptr_t)uargs->varying_alloc_list;

			if (0 != _mali_osk_copy_from_user(job->varying_list, memory_list, sizeof(u32)*uargs->varying_alloc_num)) {
				MALI_PRINT_ERROR(("Mali GP job: Failed to copy varying list from user space!\n"));
				goto fail;
			}

			if (unlikely(_mali_gp_add_varying_allocations(session, job, job->varying_list,
					uargs->varying_alloc_num))) {
				MALI_PRINT_ERROR(("Mali GP job: _mali_gp_add_varying_allocations failed!\n"));
				goto fail;
			}

			/* do preparetion for each allocation */
			list_for_each_entry_safe(alloc_node, tmp_node, &job->varying_alloc, node) {
				if (unlikely(_MALI_OSK_ERR_OK != mali_mem_defer_bind_allocation_prepare(alloc_node->alloc, &job->vary_todo))) {
					MALI_PRINT_ERROR(("Mali GP job: mali_mem_defer_bind_allocation_prepare failed!\n"));
					goto fail;
				}
			}

			_mali_gp_del_varying_allocations(job);

			/* bind varying here, to avoid memory latency issue. */
			{
				struct mali_defer_mem_block dmem_block;

				INIT_LIST_HEAD(&dmem_block.free_pages);
				atomic_set(&dmem_block.num_free_pages, 0);

				if (mali_mem_prepare_mem_for_job(job, &dmem_block)) {
					MALI_PRINT_ERROR(("Mali GP job: mali_mem_prepare_mem_for_job failed!\n"));
					goto fail;
				}
				if (_MALI_OSK_ERR_OK != mali_mem_defer_bind(job->uargs.varying_memsize / _MALI_OSK_MALI_PAGE_SIZE, job, &dmem_block)) {
					MALI_PRINT_ERROR(("gp job create, mali_mem_defer_bind failed! GP %x fail!", job));
					goto fail;
				}
			}

			if (uargs->varying_memsize > MALI_UK_BIG_VARYING_SIZE) {
				job->big_job = 1;
			}
		}
		job->pp_tracker = pp_tracker;
		if (NULL != job->pp_tracker) {
			/* Take a reference on PP job's tracker that will be released when the GP
			   job is done. */
			mali_timeline_system_tracker_get(session->timeline_system, pp_tracker);
		}

		mali_timeline_tracker_init(&job->tracker, MALI_TIMELINE_TRACKER_GP, NULL, job);
		mali_timeline_fence_copy_uk_fence(&(job->tracker.fence), &(job->uargs.fence));

		return job;
	} else {
		MALI_PRINT_ERROR(("Mali GP job: _mali_osk_calloc failed!\n"));
		return NULL;
	}


fail:
	_mali_osk_free(job->varying_list);
	/* Handle allocate fail here, free all varying node */
	{
		struct mali_backend_bind_list *bkn, *bkn_tmp;
		list_for_each_entry_safe(bkn, bkn_tmp , &job->vary_todo, node) {
			list_del(&bkn->node);
			_mali_osk_free(bkn);
		}
	}
fail1:
	_mali_osk_notification_delete(job->oom_notification);
fail2:
	_mali_osk_notification_delete(job->finished_notification);
fail3:
	_mali_osk_free(job);
	return NULL;
}

void mali_gp_job_delete(struct mali_gp_job *job)
{
	struct mali_backend_bind_list *bkn, *bkn_tmp;
	MALI_DEBUG_ASSERT_POINTER(job);
	MALI_DEBUG_ASSERT(NULL == job->pp_tracker);
	MALI_DEBUG_ASSERT(_mali_osk_list_empty(&job->list));
	_mali_osk_free(job->varying_list);

	/* Handle allocate fail here, free all varying node */
	list_for_each_entry_safe(bkn, bkn_tmp , &job->vary_todo, node) {
		list_del(&bkn->node);
		_mali_osk_free(bkn);
	}

	if (!list_empty(&job->vary_todo)) {
		MALI_DEBUG_ASSERT(0);
	}

	mali_mem_defer_dmem_free(job);

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
