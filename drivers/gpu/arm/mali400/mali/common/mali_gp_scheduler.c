/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
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
#include "mali_pm.h"

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
	u32 num_groups;
	u32 i;

	_MALI_OSK_INIT_LIST_HEAD(&job_queue);

	gp_scheduler_lock = _mali_osk_lock_init(_MALI_OSK_LOCKFLAG_ORDERED | _MALI_OSK_LOCKFLAG_SPINLOCK | _MALI_OSK_LOCKFLAG_NONINTERRUPTABLE, 0, _MALI_OSK_LOCK_ORDER_SCHEDULER);
	if (NULL == gp_scheduler_lock)
	{
		return _MALI_OSK_ERR_NOMEM;
	}

	gp_scheduler_working_wait_queue = _mali_osk_wait_queue_init();
	if (NULL == gp_scheduler_working_wait_queue)
	{
		_mali_osk_lock_term(gp_scheduler_lock);
		return _MALI_OSK_ERR_NOMEM;
	}

	/* Find all the available GP cores */
	num_groups = mali_group_get_glob_num_groups();
	for (i = 0; i < num_groups; i++)
	{
		struct mali_group *group = mali_group_get_glob_group(i);

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
			break; /* There is only one GP, no point in looking for more */
		}
	}

	return _MALI_OSK_ERR_OK;
}

void mali_gp_scheduler_terminate(void)
{
	MALI_DEBUG_ASSERT(MALI_GP_SLOT_STATE_IDLE == slot.state);
	MALI_DEBUG_ASSERT_POINTER(slot.group);
	mali_group_delete(slot.group);

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

	mali_gp_scheduler_lock();

	if (0 < pause_count || MALI_GP_SLOT_STATE_IDLE != slot.state || _mali_osk_list_empty(&job_queue))
	{
		MALI_DEBUG_PRINT(4, ("Mali GP scheduler: Nothing to schedule (paused=%u, idle slots=%u)\n",
		                     pause_count, MALI_GP_SLOT_STATE_IDLE == slot.state ? 1 : 0));
		mali_gp_scheduler_unlock();
		return; /* Nothing to do, so early out */
	}

	/* Get (and remove) next job in queue */
	job = _MALI_OSK_LIST_ENTRY(job_queue.next, struct mali_gp_job, list);
	_mali_osk_list_del(&job->list);

	/* Mark slot as busy */
	slot.state = MALI_GP_SLOT_STATE_WORKING;

	mali_gp_scheduler_unlock();

	MALI_DEBUG_PRINT(3, ("Mali GP scheduler: Starting job %u (0x%08X)\n", mali_gp_job_get_id(job), job));

	mali_group_lock(slot.group);

	if (_MALI_OSK_ERR_OK != mali_group_start_gp_job(slot.group, job))
	{
		MALI_DEBUG_PRINT(3, ("Mali GP scheduler: Failed to start GP job\n"));
		MALI_DEBUG_ASSERT(0); /* @@@@ todo: this cant fail on Mali-300+, no need to implement put back of job */
	}

	mali_group_unlock(slot.group);
}

/* @@@@ todo: pass the job in as a param to this function, so that we don't have to take the scheduler lock again */
static void mali_gp_scheduler_schedule_on_group(struct mali_group *group)
{
	struct mali_gp_job *job;

	MALI_DEBUG_ASSERT_LOCK_HELD(group->lock);
	MALI_DEBUG_ASSERT_LOCK_HELD(gp_scheduler_lock);

	if (0 < pause_count || MALI_GP_SLOT_STATE_IDLE != slot.state || _mali_osk_list_empty(&job_queue))
	{
		mali_gp_scheduler_unlock();
		MALI_DEBUG_PRINT(4, ("Mali GP scheduler: Nothing to schedule (paused=%u, idle slots=%u)\n",
		                     pause_count, MALI_GP_SLOT_STATE_IDLE == slot.state ? 1 : 0));
		return; /* Nothing to do, so early out */
	}

	/* Get (and remove) next job in queue */
	job = _MALI_OSK_LIST_ENTRY(job_queue.next, struct mali_gp_job, list);
	_mali_osk_list_del(&job->list);

	/* Mark slot as busy */
	slot.state = MALI_GP_SLOT_STATE_WORKING;

	mali_gp_scheduler_unlock();

	MALI_DEBUG_PRINT(3, ("Mali GP scheduler: Starting job %u (0x%08X)\n", mali_gp_job_get_id(job), job));

	if (_MALI_OSK_ERR_OK != mali_group_start_gp_job(slot.group, job))
	{
		MALI_DEBUG_PRINT(3, ("Mali GP scheduler: Failed to start GP job\n"));
		MALI_DEBUG_ASSERT(0); /* @@@@ todo: this cant fail on Mali-300+, no need to implement put back of job */
	}
}

static void mali_gp_scheduler_return_job_to_user(struct mali_gp_job *job, mali_bool success)
{
	_mali_uk_gp_job_finished_s *jobres = job->finished_notification->result_buffer;
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

	mali_session_send_notification(mali_gp_job_get_session(job), job->finished_notification);
	job->finished_notification = NULL;

	mali_gp_job_delete(job);
}

void mali_gp_scheduler_job_done(struct mali_group *group, struct mali_gp_job *job, mali_bool success)
{
	MALI_DEBUG_PRINT(3, ("Mali GP scheduler: Job %u (0x%08X) completed (%s)\n", mali_gp_job_get_id(job), job, success ? "success" : "failure"));

	mali_gp_scheduler_return_job_to_user(job, success);

	mali_gp_scheduler_lock();

	/* Mark slot as idle again */
	slot.state = MALI_GP_SLOT_STATE_IDLE;

	/* If paused, then this was the last job, so wake up sleeping workers */
	if (pause_count > 0)
	{
		_mali_osk_wait_queue_wake_up(gp_scheduler_working_wait_queue);
	}

	mali_gp_scheduler_schedule_on_group(group);

	/* It is ok to do this after schedule, since START/STOP is simply ++ and -- anyways */
	mali_pm_core_event(MALI_CORE_EVENT_GP_STOP);
}

void mali_gp_scheduler_oom(struct mali_group *group, struct mali_gp_job *job)
{
	_mali_uk_gp_job_suspended_s * jobres;
	_mali_osk_notification_t * notification;

	mali_gp_scheduler_lock();

	notification = job->oom_notification;
	job->oom_notification = NULL;
	slot.returned_cookie = mali_gp_job_get_id(job);

	jobres = (_mali_uk_gp_job_suspended_s *)notification->result_buffer;
	jobres->user_job_ptr = mali_gp_job_get_user_id(job);
	jobres->cookie = mali_gp_job_get_id(job);

	mali_gp_scheduler_unlock();

	jobres->reason = _MALIGP_JOB_SUSPENDED_OUT_OF_MEMORY;

	mali_session_send_notification(mali_gp_job_get_session(job), notification);

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
	mali_gp_scheduler_unlock();
	if (0 == pause_count)
	{
		mali_gp_scheduler_schedule();
	}
}

_mali_osk_errcode_t _mali_ukk_gp_start_job(void *ctx, _mali_uk_gp_start_job_s *uargs)
{
	struct mali_session_data *session;
	struct mali_gp_job *job;

	MALI_DEBUG_ASSERT_POINTER(uargs);
	MALI_DEBUG_ASSERT_POINTER(ctx);

	session = (struct mali_session_data*)ctx;

	job = mali_gp_job_create(session, uargs, mali_scheduler_get_new_id());
	if (NULL == job)
	{
		return _MALI_OSK_ERR_NOMEM;
	}

#if PROFILING_SKIP_PP_AND_GP_JOBS
#warning GP jobs will not be executed
	mali_gp_scheduler_return_job_to_user(job, MALI_TRUE);
	return _MALI_OSK_ERR_OK;
#endif

	mali_pm_core_event(MALI_CORE_EVENT_GP_START);

	mali_gp_scheduler_lock();
	_mali_osk_list_addtail(&job->list, &job_queue);
	mali_gp_scheduler_unlock();

	MALI_DEBUG_PRINT(3, ("Mali GP scheduler: Job %u (0x%08X) queued\n", mali_gp_job_get_id(job), job));

	mali_gp_scheduler_schedule();

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
	struct mali_gp_job *resumed_job;
	_mali_osk_notification_t *new_notification = 0;

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

	if (_MALIGP_JOB_RESUME_WITH_NEW_HEAP == args->code)
	{
		new_notification = _mali_osk_notification_create(_MALI_NOTIFICATION_GP_STALLED, sizeof(_mali_uk_gp_job_suspended_s));

		if (NULL == new_notification)
		{
			MALI_PRINT_ERROR(("Mali GP scheduler: Failed to allocate notification object. Will abort GP job.\n"));
			mali_group_lock(slot.group);
			mali_group_abort_gp_job(slot.group, args->cookie);
			mali_group_unlock(slot.group);
			return _MALI_OSK_ERR_FAULT;
		}
	}

	mali_group_lock(slot.group);

	if (_MALIGP_JOB_RESUME_WITH_NEW_HEAP == args->code)
	{
		MALI_DEBUG_PRINT(3, ("Mali GP scheduler: Resuming job %u with new heap; 0x%08X - 0x%08X\n", args->cookie, args->arguments[0], args->arguments[1]));

		resumed_job = mali_group_resume_gp_with_new_heap(slot.group, args->cookie, args->arguments[0], args->arguments[1]);
		if (NULL != resumed_job)
		{
			/* @@@@ todo: move this and other notification handling into the job object itself */
			resumed_job->oom_notification = new_notification;
			mali_group_unlock(slot.group);
			return _MALI_OSK_ERR_OK;
		}
		else
		{
			mali_group_unlock(slot.group);
			_mali_osk_notification_delete(new_notification);
			return _MALI_OSK_ERR_FAULT;
		}
	}

	MALI_DEBUG_PRINT(3, ("Mali GP scheduler: Aborting job %u, no new heap provided\n", args->cookie));
	mali_group_abort_gp_job(slot.group, args->cookie);
	mali_group_unlock(slot.group);
	return _MALI_OSK_ERR_OK;
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

			mali_pm_core_event(MALI_CORE_EVENT_GP_STOP);
		}
	}

	mali_gp_scheduler_unlock();

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
	n += _mali_osk_snprintf(buf + n, size - n, "\n");

	return n;
}
#endif

void mali_gp_scheduler_reset_all_groups(void)
{
	if (NULL != slot.group)
	{
		mali_group_reset(slot.group);
	}
}

void mali_gp_scheduler_zap_all_active(struct mali_session_data *session)
{
	if (NULL != slot.group)
	{
		mali_group_zap_session(slot.group, session);
	}
}
