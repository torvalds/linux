/*
 * Copyright (C) 2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_gp_scheduler.h"
#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_osk_list.h"
#include "mali_scheduler.h"
#include "mali_gp.h"
#include "mali_gp_job.h"
#include "mali_group.h"
#include "mali_cluster.h"

enum mali_gp_slot_state
{
	MALI_GP_SLOT_STATE_IDLE,
	MALI_GP_SLOT_STATE_WORKING,
};

/* A render slot is an entity which jobs can be scheduled onto */
struct mali_gp_slot
{
	struct mali_group *group;
	/*
	 * We keep track of the state here as well as in the group object
	 * so we don't need to take the group lock so often (and also avoid clutter with the working lock)
	 */
	enum mali_gp_slot_state state;
	u32 returned_cookie;
};

static u32 gp_version = 0;
static _MALI_OSK_LIST_HEAD(job_queue);                          /* List of jobs with some unscheduled work */
static struct mali_gp_slot slot;

/* Variables to allow safe pausing of the scheduler */
static _mali_osk_wait_queue_t *gp_scheduler_working_wait_queue = NULL;
static u32 pause_count = 0;

static mali_bool mali_gp_scheduler_is_suspended(void);

static _mali_osk_lock_t *gp_scheduler_lock = NULL;
/* Contains tid of thread that locked the scheduler or 0, if not locked */

_mali_osk_errcode_t mali_gp_scheduler_initialize(void)
{
	u32 i;

	_MALI_OSK_INIT_LIST_HEAD(&job_queue);

	gp_scheduler_lock = _mali_osk_lock_init(_MALI_OSK_LOCKFLAG_ORDERED | _MALI_OSK_LOCKFLAG_NONINTERRUPTABLE, 0, _MALI_OSK_LOCK_ORDER_SCHEDULER);
	gp_scheduler_working_wait_queue = _mali_osk_wait_queue_init();

	if (NULL == gp_scheduler_lock)
	{
		return _MALI_OSK_ERR_NOMEM;
	}

	if (NULL == gp_scheduler_working_wait_queue)
	{
		_mali_osk_lock_term(gp_scheduler_lock);
		return _MALI_OSK_ERR_NOMEM;
	}

	/* Find all the available GP cores */
	for (i = 0; i < mali_cluster_get_glob_num_clusters(); i++)
	{
		u32 group_id = 0;
		struct mali_cluster *curr_cluster = mali_cluster_get_global_cluster(i);
		struct mali_group *group = mali_cluster_get_group(curr_cluster, group_id);
		while (NULL != group)
		{
			struct mali_gp_core *gp_core = mali_group_get_gp_core(group);
			if (NULL != gp_core)
			{
				if (0 == gp_version)
				{
					/* Retrieve GP version */
					gp_version = mali_gp_core_get_version(gp_core);
				}
				slot.group = group;
				slot.state = MALI_GP_SLOT_STATE_IDLE;
				break; /* There are only one GP, no point in looking for more */
			}
			group_id++;
			group = mali_cluster_get_group(curr_cluster, group_id);
		}
	}

	return _MALI_OSK_ERR_OK;
}

void mali_gp_scheduler_terminate(void)
{
	_mali_osk_wait_queue_term(gp_scheduler_working_wait_queue);
	_mali_osk_lock_term(gp_scheduler_lock);
}

MALI_STATIC_INLINE void mali_gp_scheduler_lock(void)
{
	if(_MALI_OSK_ERR_OK != _mali_osk_lock_wait(gp_scheduler_lock, _MALI_OSK_LOCKMODE_RW))
	{
		/* Non-interruptable lock failed: this should never happen. */
		MALI_DEBUG_ASSERT(0);
	}
	MALI_DEBUG_PRINT(5, ("Mali GP scheduler: GP scheduler lock taken\n"));
}

MALI_STATIC_INLINE void mali_gp_scheduler_unlock(void)
{
	MALI_DEBUG_PRINT(5, ("Mali GP scheduler: Releasing GP scheduler lock\n"));
	_mali_osk_lock_signal(gp_scheduler_lock, _MALI_OSK_LOCKMODE_RW);
}

#ifdef DEBUG
MALI_STATIC_INLINE void mali_gp_scheduler_assert_locked(void)
{
	MALI_DEBUG_ASSERT_LOCK_HELD(gp_scheduler_lock);
}
#define MALI_ASSERT_GP_SCHEDULER_LOCKED() mali_gp_scheduler_assert_locked()
#else
#define MALI_ASSERT_GP_SCHEDULER_LOCKED()
#endif

static void mali_gp_scheduler_schedule(void)
{
	struct mali_gp_job *job;

	MALI_ASSERT_GP_SCHEDULER_LOCKED();

	if (0 < pause_count || MALI_GP_SLOT_STATE_IDLE != slot.state || _mali_osk_list_empty(&job_queue))
	{
		MALI_DEBUG_PRINT(4, ("Mali GP scheduler: Nothing to schedule (paused=%u, idle slots=%u)\n",
		                     pause_count, MALI_GP_SLOT_STATE_IDLE == slot.state ? 1 : 0));
		return; /* Nothing to do, so early out */
	}

	job = _MALI_OSK_LIST_ENTRY(job_queue.next, struct mali_gp_job, list);

	MALI_DEBUG_PRINT(3, ("Mali GP scheduler: Starting job %u (0x%08X)\n", mali_gp_job_get_id(job), job));
	if (_MALI_OSK_ERR_OK == mali_group_start_gp_job(slot.group, job))
	{
		/* Mark slot as busy */
		slot.state = MALI_GP_SLOT_STATE_WORKING;

		/* Remove from queue of unscheduled jobs */
		_mali_osk_list_del(&job->list);
	}
	else
	{
		MALI_DEBUG_PRINT(3, ("Mali GP scheduler: Failed to start GP job\n"));
	}
}

static void mali_gp_scheduler_return_job_to_user(struct mali_gp_job *job, mali_bool success)
{
	_mali_osk_notification_t *notobj = _mali_osk_notification_create(_MALI_NOTIFICATION_GP_FINISHED, sizeof(_mali_uk_gp_job_finished_s));
	if (NULL != notobj)
	{
		_mali_uk_gp_job_finished_s *jobres = notobj->result_buffer;
		_mali_osk_memset(jobres, 0, sizeof(_mali_uk_gp_job_finished_s)); /* @@@@ can be removed once we initialize all members in this struct */
		jobres->user_job_ptr = mali_gp_job_get_user_id(job);
		if (MALI_TRUE == success)
		{
			jobres->status = _MALI_UK_JOB_STATUS_END_SUCCESS;
		}
		else
		{
			jobres->status = _MALI_UK_JOB_STATUS_END_UNKNOWN_ERR;
		}

		jobres->heap_current_addr = mali_gp_job_get_current_heap_addr(job);
		jobres->perf_counter0 = mali_gp_job_get_perf_counter_value0(job);
		jobres->perf_counter1 = mali_gp_job_get_perf_counter_value1(job);

		mali_session_send_notification(mali_gp_job_get_session(job), notobj);
	}
	else
	{
		MALI_PRINT_ERROR(("Mali GP scheduler: Unable to allocate notification object\n"));
	}

	mali_gp_job_delete(job);
}


void mali_gp_scheduler_do_schedule(void)
{
	mali_gp_scheduler_lock();

	mali_gp_scheduler_schedule();

	mali_gp_scheduler_unlock();
}

void mali_gp_scheduler_job_done(struct mali_group *group, struct mali_gp_job *job, mali_bool success)
{
	MALI_DEBUG_PRINT(3, ("Mali GP scheduler: Job %u (0x%08X) completed (%s)\n", mali_gp_job_get_id(job), job, success ? "success" : "failure"));

	mali_gp_scheduler_lock();

	/* Mark slot as idle again */
	slot.state = MALI_GP_SLOT_STATE_IDLE;

	/* If paused, then this was the last job, so wake up sleeping workers */
	if (pause_count > 0)
	{
		_mali_osk_wait_queue_wake_up(gp_scheduler_working_wait_queue);
	}
	else
	{
		mali_gp_scheduler_schedule();
	}

	mali_gp_scheduler_unlock();

	mali_gp_scheduler_return_job_to_user(job, success);
}

void mali_gp_scheduler_oom(struct mali_group *group, struct mali_gp_job *job)
{
	_mali_osk_notification_t *notobj;

	notobj = _mali_osk_notification_create(_MALI_NOTIFICATION_GP_STALLED, sizeof(_mali_uk_gp_job_suspended_s));

	if (NULL != notobj)
	{
		_mali_uk_gp_job_suspended_s * jobres;

		mali_gp_scheduler_lock();

		jobres = (_mali_uk_gp_job_suspended_s *)notobj->result_buffer;

		jobres->user_job_ptr = mali_gp_job_get_user_id(job);
		jobres->reason = _MALIGP_JOB_SUSPENDED_OUT_OF_MEMORY;
		jobres->cookie = mali_gp_job_get_id(job);
		slot.returned_cookie = jobres->cookie;

		mali_session_send_notification(mali_gp_job_get_session(job), notobj);

		mali_gp_scheduler_unlock();
	}

	/*
	* If this function failed, then we could return the job to user space right away,
	* but there is a job timer anyway that will do that eventually.
	* This is not exactly a common case anyway.
	*/
}

void mali_gp_scheduler_suspend(void)
{
	mali_gp_scheduler_lock();
	pause_count++; /* Increment the pause_count so that no more jobs will be scheduled */
	mali_gp_scheduler_unlock();

	_mali_osk_wait_queue_wait_event(gp_scheduler_working_wait_queue, mali_gp_scheduler_is_suspended);
}

void mali_gp_scheduler_resume(void)
{
	mali_gp_scheduler_lock();
	pause_count--; /* Decrement pause_count to allow scheduling again (if it reaches 0) */
	if (0 == pause_count)
	{
		mali_gp_scheduler_schedule();
	}
	mali_gp_scheduler_unlock();
}

_mali_osk_errcode_t _mali_ukk_gp_start_job(_mali_uk_gp_start_job_s *args)
{
	struct mali_session_data *session;
	struct mali_gp_job *job;

	MALI_DEBUG_ASSERT_POINTER(args);

	if (NULL == args->ctx)
	{
		return _MALI_OSK_ERR_INVALID_ARGS;
	}

	session = (struct mali_session_data*)args->ctx;
	if (NULL == session)
	{
		return _MALI_OSK_ERR_FAULT;
	}

	job = mali_gp_job_create(session, args, mali_scheduler_get_new_id());
	if (NULL == job)
	{
		return _MALI_OSK_ERR_NOMEM;
	}

	mali_gp_scheduler_lock();

	_mali_osk_list_addtail(&job->list, &job_queue);

	MALI_DEBUG_PRINT(3, ("Mali GP scheduler: Job %u (0x%08X) queued\n", mali_gp_job_get_id(job), job));

	mali_gp_scheduler_schedule();

	mali_gp_scheduler_unlock();

	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _mali_ukk_get_gp_number_of_cores(_mali_uk_get_gp_number_of_cores_s *args)
{
	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);
	args->number_of_cores = 1;
	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _mali_ukk_get_gp_core_version(_mali_uk_get_gp_core_version_s *args)
{
	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);
	args->version = gp_version;
	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _mali_ukk_gp_suspend_response(_mali_uk_gp_suspend_response_s *args)
{
	struct mali_session_data *session;
	_mali_osk_errcode_t ret = _MALI_OSK_ERR_FAULT;

	MALI_DEBUG_ASSERT_POINTER(args);

	if (NULL == args->ctx)
	{
		return _MALI_OSK_ERR_INVALID_ARGS;
	}

	session = (struct mali_session_data*)args->ctx;
	if (NULL == session)
	{
		return _MALI_OSK_ERR_FAULT;
	}

	mali_gp_scheduler_lock();

	/* Make sure that the cookie returned by user space is the same as we provided in the first place */
	if (args->cookie != slot.returned_cookie)
	{
		MALI_DEBUG_PRINT(2, ("Mali GP scheduler: Got an illegal cookie from user space, expected %u but got %u (job id)\n", slot.returned_cookie, args->cookie)) ;
		mali_gp_scheduler_unlock();
		return _MALI_OSK_ERR_FAULT;
	}

	mali_gp_scheduler_unlock();

	switch (args->code)
	{
		case _MALIGP_JOB_RESUME_WITH_NEW_HEAP:
			MALI_DEBUG_PRINT(3, ("Mali GP scheduler: Resuming job %u with new heap; 0x%08X - 0x%08X\n", args->cookie, args->arguments[0], args->arguments[1]));
			mali_group_resume_gp_with_new_heap(slot.group, args->cookie, args->arguments[0], args->arguments[1]);
			ret = _MALI_OSK_ERR_OK;
			break;

		case _MALIGP_JOB_ABORT:
			MALI_DEBUG_PRINT(3, ("Mali GP scheduler: Aborting job %u, no new heap provided\n", args->cookie));
			mali_group_abort_gp_job(slot.group, args->cookie);
			ret = _MALI_OSK_ERR_OK;
			break;

		default:
			MALI_PRINT_ERROR(("Mali GP scheduler: Wrong suspend response from user space\n"));
			ret = _MALI_OSK_ERR_FAULT;
			break;
	}

    return ret;

}

void mali_gp_scheduler_abort_session(struct mali_session_data *session)
{
	struct mali_gp_job *job, *tmp;

	mali_gp_scheduler_lock();
	MALI_DEBUG_PRINT(3, ("Mali GP scheduler: Aborting all jobs from session 0x%08x\n", session));

	/* Check queue for jobs and remove */
	_MALI_OSK_LIST_FOREACHENTRY(job, tmp, &job_queue, struct mali_gp_job, list)
	{
		if (mali_gp_job_get_session(job) == session)
		{
			MALI_DEBUG_PRINT(4, ("Mali GP scheduler: Removing GP job 0x%08x from queue\n", job));
			_mali_osk_list_del(&(job->list));
			mali_gp_job_delete(job);
		}
	}

	mali_gp_scheduler_unlock();

	/* Abort running jobs from this session. It is safe to do this outside
	 * the scheduler lock as there is only one GP core, and the queue has
	 * already been emptied, as long as there are no new jobs coming in
	 * from user space. */
	mali_group_abort_session(slot.group, session);
}

static mali_bool mali_gp_scheduler_is_suspended(void)
{
	mali_bool ret;

	mali_gp_scheduler_lock();
	ret = pause_count > 0 && slot.state == MALI_GP_SLOT_STATE_IDLE;
	mali_gp_scheduler_unlock();

	return ret;
}


#if MALI_STATE_TRACKING
u32 mali_gp_scheduler_dump_state(char *buf, u32 size)
{
	int n = 0;

	n += _mali_osk_snprintf(buf + n, size - n, "GP\n");
	n += _mali_osk_snprintf(buf + n, size - n, "\tQueue is %s\n", _mali_osk_list_empty(&job_queue) ? "empty" : "not empty");

	n += mali_group_dump_state(slot.group, buf + n, size - n);
	n += _mali_osk_snprintf(buf + n, size - n, "\t\tState: %d\n", mali_group_gp_state(slot.group));
	n += _mali_osk_snprintf(buf + n, size - n, "\n");

	return n;
}
#endif
