/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2012-2014 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#include "mali_gp_scheduler.h"
#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_osk_list.h"
#include "mali_scheduler.h"
#include "mali_gp.h"
#include "mali_gp_job.h"
#include "mali_group.h"
#include "mali_timeline.h"
#include "mali_osk_profiling.h"
#include "mali_kernel_utilization.h"
#if defined(CONFIG_GPU_TRACEPOINTS) && defined(CONFIG_TRACEPOINTS)
#include <linux/sched.h>
#include <trace/events/gpu.h>
#endif

enum mali_gp_slot_state {
	MALI_GP_SLOT_STATE_IDLE,
	MALI_GP_SLOT_STATE_WORKING,
	MALI_GP_SLOT_STATE_DISABLED,
};

/* A render slot is an entity which jobs can be scheduled onto */
struct mali_gp_slot {
	struct mali_group *group;
	/*
	 * We keep track of the state here as well as in the group object
	 * so we don't need to take the group lock so often (and also avoid clutter with the working lock)
	 */
	enum mali_gp_slot_state state;
	u32 returned_cookie;
};

static u32 gp_version = 0;
static _MALI_OSK_LIST_HEAD_STATIC_INIT(job_queue);      /* List of unscheduled jobs. */
static _MALI_OSK_LIST_HEAD_STATIC_INIT(job_queue_high); /* List of unscheduled high priority jobs. */
static struct mali_gp_slot slot;

/* Variables to allow safe pausing of the scheduler */
static _mali_osk_wait_queue_t *gp_scheduler_working_wait_queue = NULL;
static u32 pause_count = 0;

static mali_bool mali_gp_scheduler_is_suspended(void *data);
static void mali_gp_scheduler_job_queued(void);
static void mali_gp_scheduler_job_completed(void);

#if defined(MALI_UPPER_HALF_SCHEDULING)
static _mali_osk_spinlock_irq_t *gp_scheduler_lock = NULL;
#else
static _mali_osk_spinlock_t *gp_scheduler_lock = NULL;
#endif /* defined(MALI_UPPER_HALF_SCHEDULING) */

_mali_osk_errcode_t mali_gp_scheduler_initialize(void)
{
	u32 num_groups;
	u32 i;
	_mali_osk_errcode_t ret = _MALI_OSK_ERR_OK;

#if defined(MALI_UPPER_HALF_SCHEDULING)
	gp_scheduler_lock = _mali_osk_spinlock_irq_init(_MALI_OSK_LOCKFLAG_ORDERED, _MALI_OSK_LOCK_ORDER_SCHEDULER);
#else
	gp_scheduler_lock = _mali_osk_spinlock_init(_MALI_OSK_LOCKFLAG_ORDERED, _MALI_OSK_LOCK_ORDER_SCHEDULER);
#endif /* defined(MALI_UPPER_HALF_SCHEDULING) */
	if (NULL == gp_scheduler_lock) {
		ret = _MALI_OSK_ERR_NOMEM;
		goto cleanup;
	}

	gp_scheduler_working_wait_queue = _mali_osk_wait_queue_init();
	if (NULL == gp_scheduler_working_wait_queue) {
		ret = _MALI_OSK_ERR_NOMEM;
		goto cleanup;
	}

	/* Find all the available GP cores */
	num_groups = mali_group_get_glob_num_groups();
	for (i = 0; i < num_groups; i++) {
		struct mali_group *group = mali_group_get_glob_group(i);
		MALI_DEBUG_ASSERT(NULL != group);
		if (NULL != group) {
			struct mali_gp_core *gp_core = mali_group_get_gp_core(group);
			if (NULL != gp_core) {
				if (0 == gp_version) {
					/* Retrieve GP version */
					gp_version = mali_gp_core_get_version(gp_core);
				}
				slot.group = group;
				slot.state = MALI_GP_SLOT_STATE_IDLE;
				break; /* There is only one GP, no point in looking for more */
			}
		} else {
			ret = _MALI_OSK_ERR_ITEM_NOT_FOUND;
			goto cleanup;
		}
	}

	return _MALI_OSK_ERR_OK;

cleanup:
	if (NULL != gp_scheduler_working_wait_queue) {
		_mali_osk_wait_queue_term(gp_scheduler_working_wait_queue);
		gp_scheduler_working_wait_queue = NULL;
	}

	if (NULL != gp_scheduler_lock) {
#if defined(MALI_UPPER_HALF_SCHEDULING)
		_mali_osk_spinlock_irq_term(gp_scheduler_lock);
#else
		_mali_osk_spinlock_term(gp_scheduler_lock);
#endif /* defined(MALI_UPPER_HALF_SCHEDULING) */
		gp_scheduler_lock = NULL;
	}

	return ret;
}

void mali_gp_scheduler_terminate(void)
{
	MALI_DEBUG_ASSERT(MALI_GP_SLOT_STATE_IDLE     == slot.state
			  || MALI_GP_SLOT_STATE_DISABLED == slot.state);
	MALI_DEBUG_ASSERT_POINTER(slot.group);
	mali_group_delete(slot.group);

	_mali_osk_wait_queue_term(gp_scheduler_working_wait_queue);

#if defined(MALI_UPPER_HALF_SCHEDULING)
	_mali_osk_spinlock_irq_term(gp_scheduler_lock);
#else
	_mali_osk_spinlock_term(gp_scheduler_lock);
#endif /* defined(MALI_UPPER_HALF_SCHEDULING) */
}

MALI_STATIC_INLINE void mali_gp_scheduler_lock(void)
{
#if defined(MALI_UPPER_HALF_SCHEDULING)
	_mali_osk_spinlock_irq_lock(gp_scheduler_lock);
#else
	_mali_osk_spinlock_lock(gp_scheduler_lock);
#endif /* defined(MALI_UPPER_HALF_SCHEDULING) */
	MALI_DEBUG_PRINT(5, ("Mali GP scheduler: GP scheduler lock taken\n"));
}

MALI_STATIC_INLINE void mali_gp_scheduler_unlock(void)
{
	MALI_DEBUG_PRINT(5, ("Mali GP scheduler: Releasing GP scheduler lock\n"));
#if defined(MALI_UPPER_HALF_SCHEDULING)
	_mali_osk_spinlock_irq_unlock(gp_scheduler_lock);
#else
	_mali_osk_spinlock_unlock(gp_scheduler_lock);
#endif /* defined(MALI_UPPER_HALF_SCHEDULING) */
}

#if defined(DEBUG)
#define MALI_ASSERT_GP_SCHEDULER_LOCKED() MALI_DEBUG_ASSERT_LOCK_HELD(gp_scheduler_lock)
#else
#define MALI_ASSERT_GP_SCHEDULER_LOCKED() do {} while (0)
#endif /* defined(DEBUG) */

/* Group and scheduler must be locked when entering this function.  Both will be unlocked before
 * exiting. */
static void mali_gp_scheduler_schedule_internal_and_unlock(void)
{
	struct mali_gp_job *job = NULL;

	MALI_DEBUG_ASSERT_LOCK_HELD(slot.group->lock);
	MALI_DEBUG_ASSERT_LOCK_HELD(gp_scheduler_lock);

	if (0 < pause_count || MALI_GP_SLOT_STATE_IDLE != slot.state ||
	    (_mali_osk_list_empty(&job_queue) && _mali_osk_list_empty(&job_queue_high))) {
		mali_gp_scheduler_unlock();
		mali_group_unlock(slot.group);
		MALI_DEBUG_PRINT(4, ("Mali GP scheduler: Nothing to schedule (paused=%u, idle slots=%u)\n",
				     pause_count, MALI_GP_SLOT_STATE_IDLE == slot.state ? 1 : 0));
#if defined(CONFIG_GPU_TRACEPOINTS) && defined(CONFIG_TRACEPOINTS)
		trace_gpu_sched_switch(mali_gp_get_hw_core_desc(group->gp_core), sched_clock(), 0, 0, 0);
#endif
		return; /* Nothing to do, so early out */
	}

	/* Get next job in queue */
	if (!_mali_osk_list_empty(&job_queue_high)) {
		job = _MALI_OSK_LIST_ENTRY(job_queue_high.next, struct mali_gp_job, list);
	} else {
		MALI_DEBUG_ASSERT(!_mali_osk_list_empty(&job_queue));
		job = _MALI_OSK_LIST_ENTRY(job_queue.next, struct mali_gp_job, list);
	}

	MALI_DEBUG_ASSERT_POINTER(job);

	/* Remove the job from queue */
	_mali_osk_list_del(&job->list);

	/* Mark slot as busy */
	slot.state = MALI_GP_SLOT_STATE_WORKING;

	mali_gp_scheduler_unlock();

	MALI_DEBUG_PRINT(3, ("Mali GP scheduler: Starting job %u (0x%08X)\n", mali_gp_job_get_id(job), job));

	mali_group_start_gp_job(slot.group, job);
	mali_group_unlock(slot.group);
}

void mali_gp_scheduler_schedule(void)
{
	mali_group_lock(slot.group);
	mali_gp_scheduler_lock();

	mali_gp_scheduler_schedule_internal_and_unlock();
}

static void mali_gp_scheduler_return_job_to_user(struct mali_gp_job *job, mali_bool success)
{
	_mali_uk_gp_job_finished_s *jobres = job->finished_notification->result_buffer;
	_mali_osk_memset(jobres, 0, sizeof(_mali_uk_gp_job_finished_s)); /* @@@@ can be removed once we initialize all members in this struct */
	jobres->user_job_ptr = mali_gp_job_get_user_id(job);
	if (MALI_TRUE == success) {
		jobres->status = _MALI_UK_JOB_STATUS_END_SUCCESS;
	} else {
		jobres->status = _MALI_UK_JOB_STATUS_END_UNKNOWN_ERR;
	}

	jobres->heap_current_addr = mali_gp_job_get_current_heap_addr(job);
	jobres->perf_counter0 = mali_gp_job_get_perf_counter_value0(job);
	jobres->perf_counter1 = mali_gp_job_get_perf_counter_value1(job);

	mali_session_send_notification(mali_gp_job_get_session(job), job->finished_notification);
	job->finished_notification = NULL;

	mali_gp_job_delete(job);
	mali_gp_scheduler_job_completed();
}

/* Group must be locked when entering this function.  Will be unlocked before exiting. */
void mali_gp_scheduler_job_done(struct mali_group *group, struct mali_gp_job *job, mali_bool success)
{
	mali_scheduler_mask schedule_mask = MALI_SCHEDULER_MASK_EMPTY;

	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT_POINTER(job);

	MALI_DEBUG_ASSERT_LOCK_HELD(group->lock);
	MALI_DEBUG_ASSERT(slot.group == group);

	MALI_DEBUG_PRINT(3, ("Mali GP scheduler: Job %u (0x%08X) completed (%s)\n", mali_gp_job_get_id(job), job, success ? "success" : "failure"));

	/* Release tracker. */
	schedule_mask |= mali_timeline_tracker_release(&job->tracker);

	/* Signal PP job. */
	schedule_mask |= mali_gp_job_signal_pp_tracker(job, success);

	mali_gp_scheduler_lock();

	/* Mark slot as idle again */
	slot.state = MALI_GP_SLOT_STATE_IDLE;

	/* If paused, then this was the last job, so wake up sleeping workers */
	if (pause_count > 0) {
		_mali_osk_wait_queue_wake_up(gp_scheduler_working_wait_queue);
	}

	/* Schedule any queued GP jobs on this group. */
	mali_gp_scheduler_schedule_internal_and_unlock();

	/* GP is now scheduled, removing it from the mask. */
	schedule_mask &= ~MALI_SCHEDULER_MASK_GP;

	if (MALI_SCHEDULER_MASK_EMPTY != schedule_mask) {
		/* Releasing the tracker activated other jobs that need scheduling. */
		mali_scheduler_schedule_from_mask(schedule_mask, MALI_FALSE);
	}

	/* Sends the job end message to user space and free the job object */
	mali_gp_scheduler_return_job_to_user(job, success);
}

void mali_gp_scheduler_oom(struct mali_group *group, struct mali_gp_job *job)
{
	_mali_uk_gp_job_suspended_s *jobres;
	_mali_osk_notification_t *notification;

	mali_gp_scheduler_lock();

	notification = job->oom_notification;
	job->oom_notification = NULL;
	slot.returned_cookie = mali_gp_job_get_id(job);

	jobres = (_mali_uk_gp_job_suspended_s *)notification->result_buffer;
	jobres->user_job_ptr = mali_gp_job_get_user_id(job);
	jobres->cookie = mali_gp_job_get_id(job);

	mali_gp_scheduler_unlock();

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

	_mali_osk_wait_queue_wait_event(gp_scheduler_working_wait_queue, mali_gp_scheduler_is_suspended, NULL);
}

void mali_gp_scheduler_resume(void)
{
	mali_gp_scheduler_lock();
	pause_count--; /* Decrement pause_count to allow scheduling again (if it reaches 0) */
	mali_gp_scheduler_unlock();
	if (0 == pause_count) {
		mali_gp_scheduler_schedule();
	}
}

mali_timeline_point mali_gp_scheduler_submit_job(struct mali_session_data *session, struct mali_gp_job *job)
{
	mali_timeline_point point;

	MALI_DEBUG_ASSERT_POINTER(session);
	MALI_DEBUG_ASSERT_POINTER(job);

	/* We hold a PM reference for every job we hold queued (and running) */
	_mali_osk_pm_dev_ref_add();

	/* Add job to Timeline system. */
	point = mali_timeline_system_add_tracker(session->timeline_system, &job->tracker, MALI_TIMELINE_GP);

	return point;
}

_mali_osk_errcode_t _mali_ukk_gp_start_job(void *ctx, _mali_uk_gp_start_job_s *uargs)
{
	struct mali_session_data *session;
	struct mali_gp_job *job;
	mali_timeline_point point;
	u32 __user *timeline_point_ptr = NULL;

	MALI_DEBUG_ASSERT_POINTER(uargs);
	MALI_DEBUG_ASSERT_POINTER(ctx);

	session = (struct mali_session_data *)ctx;

	job = mali_gp_job_create(session, uargs, mali_scheduler_get_new_id(), NULL);
	if (NULL == job) {
		MALI_PRINT_ERROR(("Failed to create GP job.\n"));
		return _MALI_OSK_ERR_NOMEM;
	}

	timeline_point_ptr = (u32 __user *)(uintptr_t)job->uargs.timeline_point_ptr;

	point = mali_gp_scheduler_submit_job(session, job);

	if (0 != _mali_osk_put_user(((u32) point), timeline_point_ptr)) {
		/* Let user space know that something failed after the job was started. */
		return _MALI_OSK_ERR_ITEM_NOT_FOUND;
	}

	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _mali_ukk_get_gp_number_of_cores(_mali_uk_get_gp_number_of_cores_s *args)
{
	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_DEBUG_ASSERT_POINTER((struct mali_session_data *)(uintptr_t)args->ctx);

	args->number_of_cores = 1;
	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _mali_ukk_get_gp_core_version(_mali_uk_get_gp_core_version_s *args)
{
	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_DEBUG_ASSERT_POINTER((struct mali_session_data *)(uintptr_t)args->ctx);

	args->version = gp_version;
	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _mali_ukk_gp_suspend_response(_mali_uk_gp_suspend_response_s *args)
{
	struct mali_gp_job *resumed_job;
	_mali_osk_notification_t *new_notification = NULL;

	MALI_DEBUG_ASSERT_POINTER(args);

	if (_MALIGP_JOB_RESUME_WITH_NEW_HEAP == args->code) {
		new_notification = _mali_osk_notification_create(_MALI_NOTIFICATION_GP_STALLED, sizeof(_mali_uk_gp_job_suspended_s));

		if (NULL == new_notification) {
			MALI_PRINT_ERROR(("Mali GP scheduler: Failed to allocate notification object. Will abort GP job.\n"));
			mali_group_lock(slot.group);
			mali_group_abort_gp_job(slot.group, args->cookie);
			mali_group_unlock(slot.group);
			return _MALI_OSK_ERR_FAULT;
		}
	}

	mali_group_lock(slot.group);

	if (_MALIGP_JOB_RESUME_WITH_NEW_HEAP == args->code) {
		MALI_DEBUG_PRINT(3, ("Mali GP scheduler: Resuming job %u with new heap; 0x%08X - 0x%08X\n", args->cookie, args->arguments[0], args->arguments[1]));

		resumed_job = mali_group_resume_gp_with_new_heap(slot.group, args->cookie, args->arguments[0], args->arguments[1]);
		if (NULL != resumed_job) {
			resumed_job->oom_notification = new_notification;
			mali_group_unlock(slot.group);
			return _MALI_OSK_ERR_OK;
		} else {
			mali_group_unlock(slot.group);
			_mali_osk_notification_delete(new_notification);
			return _MALI_OSK_ERR_FAULT;
		}
	}

	MALI_DEBUG_PRINT(2, ("Mali GP scheduler: Aborting job %u, no new heap provided\n", args->cookie));
	mali_group_abort_gp_job(slot.group, args->cookie);
	mali_group_unlock(slot.group);
	return _MALI_OSK_ERR_OK;
}

void mali_gp_scheduler_abort_session(struct mali_session_data *session)
{
	struct mali_gp_job *job, *tmp;
	_MALI_OSK_LIST_HEAD_STATIC_INIT(removed_jobs);

	MALI_DEBUG_ASSERT_POINTER(session);
	MALI_DEBUG_ASSERT(session->is_aborting);

	MALI_DEBUG_PRINT(3, ("Mali GP scheduler: Aborting all jobs from session 0x%08X.\n", session));

	mali_gp_scheduler_lock();

	/* Find all jobs from the aborting session. */
	_MALI_OSK_LIST_FOREACHENTRY(job, tmp, &job_queue, struct mali_gp_job, list) {
		if (job->session == session) {
			MALI_DEBUG_PRINT(3, ("Mali GP scheduler: Removing job %u (0x%08X) from queue.\n", mali_gp_job_get_id(job), job));
			_mali_osk_list_move(&job->list, &removed_jobs);
		}
	}

	/* Find all high priority jobs from the aborting session. */
	_MALI_OSK_LIST_FOREACHENTRY(job, tmp, &job_queue_high, struct mali_gp_job, list) {
		if (job->session == session) {
			MALI_DEBUG_PRINT(3, ("Mali GP scheduler: Removing job %u (0x%08X) from queue.\n", mali_gp_job_get_id(job), job));
			_mali_osk_list_move(&job->list, &removed_jobs);
		}
	}

	mali_gp_scheduler_unlock();

	/* Release and delete all found jobs from the aborting session. */
	_MALI_OSK_LIST_FOREACHENTRY(job, tmp, &removed_jobs, struct mali_gp_job, list) {
		mali_timeline_tracker_release(&job->tracker);
		mali_gp_job_signal_pp_tracker(job, MALI_FALSE);
		mali_gp_job_delete(job);
		mali_gp_scheduler_job_completed();
	}

	/* Abort any running jobs from the session. */
	mali_group_abort_session(slot.group, session);
}

static mali_bool mali_gp_scheduler_is_suspended(void *data)
{
	mali_bool ret;

	/* This callback does not use the data pointer. */
	MALI_IGNORE(data);

	mali_gp_scheduler_lock();
	ret = pause_count > 0 && (slot.state == MALI_GP_SLOT_STATE_IDLE || slot.state == MALI_GP_SLOT_STATE_DISABLED);
	mali_gp_scheduler_unlock();

	return ret;
}


#if MALI_STATE_TRACKING
u32 mali_gp_scheduler_dump_state(char *buf, u32 size)
{
	int n = 0;

	n += _mali_osk_snprintf(buf + n, size - n, "GP\n");
	n += _mali_osk_snprintf(buf + n, size - n, "\tQueue is %s\n", _mali_osk_list_empty(&job_queue) ? "empty" : "not empty");
	n += _mali_osk_snprintf(buf + n, size - n, "\tHigh priority queue is %s\n", _mali_osk_list_empty(&job_queue_high) ? "empty" : "not empty");

	n += mali_group_dump_state(slot.group, buf + n, size - n);
	n += _mali_osk_snprintf(buf + n, size - n, "\n");

	return n;
}
#endif

void mali_gp_scheduler_reset_all_groups(void)
{
	if (NULL != slot.group) {
		mali_group_lock(slot.group);
		mali_group_reset(slot.group);
		mali_group_unlock(slot.group);
	}
}

void mali_gp_scheduler_zap_all_active(struct mali_session_data *session)
{
	if (NULL != slot.group) {
		mali_group_zap_session(slot.group, session);
	}
}

void mali_gp_scheduler_enable_group(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT(slot.group == group);
	MALI_DEBUG_PRINT(2, ("Mali GP scheduler: enabling gp group %p\n", group));

	mali_group_lock(group);

	if (MALI_GROUP_STATE_DISABLED != group->state) {
		mali_group_unlock(group);
		MALI_DEBUG_PRINT(2, ("Mali GP scheduler: gp group %p already enabled\n", group));
		return;
	}

	mali_gp_scheduler_lock();

	MALI_DEBUG_ASSERT(MALI_GROUP_STATE_DISABLED == group->state);
	MALI_DEBUG_ASSERT(MALI_GP_SLOT_STATE_DISABLED == slot.state);
	slot.state = MALI_GP_SLOT_STATE_IDLE;
	group->state = MALI_GROUP_STATE_IDLE;

	mali_group_power_on_group(group);
	mali_group_reset(group);

	/* Pick up any jobs that might have been queued while the GP group was disabled. */
	mali_gp_scheduler_schedule_internal_and_unlock();
}

void mali_gp_scheduler_disable_group(struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(group);
	MALI_DEBUG_ASSERT(slot.group == group);
	MALI_DEBUG_PRINT(2, ("Mali GP scheduler: disabling gp group %p\n", group));

	mali_gp_scheduler_suspend();
	mali_group_lock(group);
	mali_gp_scheduler_lock();

	MALI_DEBUG_ASSERT(MALI_GROUP_STATE_IDLE     == group->state
			  || MALI_GROUP_STATE_DISABLED == group->state);

	if (MALI_GROUP_STATE_DISABLED == group->state) {
		MALI_DEBUG_ASSERT(MALI_GP_SLOT_STATE_DISABLED == slot.state);
		MALI_DEBUG_PRINT(2, ("Mali GP scheduler: gp group %p already disabled\n", group));
	} else {
		MALI_DEBUG_ASSERT(MALI_GP_SLOT_STATE_IDLE == slot.state);
		slot.state = MALI_GP_SLOT_STATE_DISABLED;
		group->state = MALI_GROUP_STATE_DISABLED;

		mali_group_power_off_group(group, MALI_TRUE);
	}

	mali_gp_scheduler_unlock();
	mali_group_unlock(group);
	mali_gp_scheduler_resume();
}

static mali_scheduler_mask mali_gp_scheduler_queue_job(struct mali_gp_job *job)
{
	_mali_osk_list_t *queue = NULL;
	mali_scheduler_mask schedule_mask = MALI_SCHEDULER_MASK_EMPTY;
	struct mali_gp_job *iter, *tmp;

	MALI_DEBUG_ASSERT_POINTER(job);
	MALI_DEBUG_ASSERT_POINTER(job->session);

	MALI_DEBUG_ASSERT_LOCK_HELD(gp_scheduler_lock);

	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE | MALI_PROFILING_EVENT_CHANNEL_SOFTWARE | MALI_PROFILING_EVENT_REASON_SINGLE_SW_GP_ENQUEUE, job->pid, job->tid, job->uargs.frame_builder_id, job->uargs.flush_id, 0);

	job->cache_order = mali_scheduler_get_new_cache_order();

	/* Determine which queue the job should be added to. */
	if (job->session->use_high_priority_job_queue) {
		queue = &job_queue_high;
	} else {
		queue = &job_queue;
	}

	/* Find position in queue where job should be added. */
	_MALI_OSK_LIST_FOREACHENTRY_REVERSE(iter, tmp, queue, struct mali_gp_job, list) {
		if (mali_gp_job_is_after(job, iter)) {
			break;
		}
	}

	/* Add job to queue. */
	_mali_osk_list_add(&job->list, &iter->list);

	/* Set schedule bitmask if the GP core is idle. */
	if (MALI_GP_SLOT_STATE_IDLE == slot.state) {
		schedule_mask |= MALI_SCHEDULER_MASK_GP;
	}

	mali_gp_scheduler_job_queued();

#if defined(CONFIG_GPU_TRACEPOINTS) && defined(CONFIG_TRACEPOINTS)
	trace_gpu_job_enqueue(mali_gp_job_get_tid(job), mali_gp_job_get_id(job), "GP");
#endif

	MALI_DEBUG_PRINT(3, ("Mali GP scheduler: Job %u (0x%08X) queued\n", mali_gp_job_get_id(job), job));

	return schedule_mask;
}

mali_scheduler_mask mali_gp_scheduler_activate_job(struct mali_gp_job *job)
{
	mali_scheduler_mask schedule_mask = MALI_SCHEDULER_MASK_EMPTY;

	MALI_DEBUG_ASSERT_POINTER(job);
	MALI_DEBUG_ASSERT_POINTER(job->session);

	MALI_DEBUG_PRINT(4, ("Mali GP scheduler: Timeline activation for job %u (0x%08X).\n", mali_gp_job_get_id(job), job));

	mali_gp_scheduler_lock();

	if (unlikely(job->session->is_aborting)) {
		/* Before checking if the session is aborting, the scheduler must be locked. */
		MALI_DEBUG_ASSERT_LOCK_HELD(gp_scheduler_lock);

		MALI_DEBUG_PRINT(3, ("Mali GP scheduler: Job %u (0x%08X) activated while session is aborting.\n", mali_gp_job_get_id(job), job));

		/* This job should not be on any list. */
		MALI_DEBUG_ASSERT(_mali_osk_list_empty(&job->list));

		mali_gp_scheduler_unlock();

		/* Release tracker and delete job. */
		mali_timeline_tracker_release(&job->tracker);
		mali_gp_job_signal_pp_tracker(job, MALI_FALSE);
		mali_gp_job_delete(job);

		/* Release the PM ref taken in mali_gp_scheduler_submit_job */
		_mali_osk_pm_dev_ref_dec();

		/* Since we are aborting we ignore the scheduler mask. */
		return MALI_SCHEDULER_MASK_EMPTY;
	}

	/* GP job is ready to run, queue it. */
	schedule_mask = mali_gp_scheduler_queue_job(job);

	mali_gp_scheduler_unlock();

	return schedule_mask;
}

static void mali_gp_scheduler_job_queued(void)
{
	if (mali_utilization_enabled()) {
		/*
		 * We cheat a little bit by counting the PP as busy from the time a GP job is queued.
		 * This will be fine because we only loose the tiny idle gap between jobs, but
		 * we will instead get less utilization work to do (less locks taken)
		 */
		mali_utilization_gp_start();
	}
}

static void mali_gp_scheduler_job_completed(void)
{
	/* Release the PM reference we got in the mali_gp_scheduler_job_queued() function */
	_mali_osk_pm_dev_ref_dec();

	if (mali_utilization_enabled()) {
		mali_utilization_gp_end();
	}
}
